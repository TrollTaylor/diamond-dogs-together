param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [switch]$BuildOnly,
    # Package builds must override any ON values retained in the lab cache.
    [switch]$DisableLocalExperiments,
    # Relative paths are resolved against the checkout, never the caller's cwd.
    [ValidateNotNullOrEmpty()][string]$BuildDirectory
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$nativeBuildDirectory = if ($BuildDirectory) {
    $requestedBuildDirectory = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $projectRoot $BuildDirectory }
    [IO.Path]::GetFullPath($requestedBuildDirectory)
} else { Join-Path $projectRoot 'build\native' }
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Install Visual Studio C++ build tools first' }
$compilerVersion = (Get-Content (Join-Path $vsRoot 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
$compilerRoot = Join-Path $vsRoot "VC/Tools/MSVC/$compilerVersion"
$sdkRoot = "${env:ProgramFiles(x86)}/Windows Kits/10"
$sdkVersion = (Get-ChildItem "$sdkRoot/Lib" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
$env:PATH = "$compilerRoot/bin/Hostx64/x64;$sdkRoot/bin/$sdkVersion/x64;$env:PATH"
$env:INCLUDE = "$compilerRoot/include;$sdkRoot/Include/$sdkVersion/ucrt;$sdkRoot/Include/$sdkVersion/shared;$sdkRoot/Include/$sdkVersion/um;$sdkRoot/Include/$sdkVersion/winrt"
$env:LIB = "$compilerRoot/lib/x64;$sdkRoot/Lib/$sdkVersion/ucrt/x64;$sdkRoot/Lib/$sdkVersion/um/x64"
$ninja = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$configureArguments = @('-S', $projectRoot, '-B', $nativeBuildDirectory, '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_BUILD_TYPE=$Configuration")
if ($DisableLocalExperiments) {
    $localExperiments = @(
        'MGS_LOCAL_GUN_PRELOAD_TEST',
        'MGS_LOCAL_OFFLINE_EQUIPMENT_TEST',
        'MGS_LOCAL_STOCK46_PRELOAD_TEST',
        'MGS_LOCAL_STOCK46_WRITER_TEST',
        'MGS_LOCAL_ACTION_EVENTS',
        'MGS_LOCAL_UPPER_END_OBSERVE',
        'MGS_LOCAL_UPPER_PHASE_OBSERVE',
        'MGS_LOCAL_UPPER_MANUAL_IDLE',
        'MGS_LOCAL_UPPER_PROBE',
        'MGS_LOCAL_UPPER_CHANNEL2',
        'MGS_LOCAL_UPPER_FIRE_ZERO_BLEND'
    )
    $configureArguments += $localExperiments | ForEach-Object { "-D$_=OFF" }
    # Runtime automation is a separate opt-in, outside the local experiment list.
    $configureArguments += '-DMGS_BACKGROUND_WORKER=OFF'
}
cmake @configureArguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
if ($BuildOnly) {
    cmake --build $nativeBuildDirectory --target mgs_coop_probe --parallel
} else {
    cmake --build $nativeBuildDirectory --parallel
}
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if (!$BuildOnly) {
    ctest --test-dir $nativeBuildDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Checks failed' }
}
