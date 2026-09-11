param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug', [switch]$BuildOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
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
cmake -S $projectRoot -B "$projectRoot/build/native" -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
if ($BuildOnly) {
    cmake --build "$projectRoot/build/native" --target mgs_coop_probe --parallel
} else {
    cmake --build "$projectRoot/build/native" --parallel
}
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if (!$BuildOnly) {
    ctest --test-dir "$projectRoot/build/native" --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Checks failed' }
}
