#Requires -Version 7
# Configure an inert LANGUAGES NONE project with real CMake. Compilation and
# CTest are recorded no-ops; no package, installation or game script runs.
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
$nonce = [Guid]::NewGuid().ToString('N')
$fixture = Join-Path $buildRoot ('build-profile-tests-' + $nonce)
$marker = Join-Path $fixture '.fixture-owner'
$cmakeExe = (Get-Command cmake -CommandType Application).Source
$utf8 = [Text.UTF8Encoding]::new($false)
$checks = 0
$savedEnvironment = @{}
foreach ($name in @('PATH', 'INCLUDE', 'LIB')) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }

function Assert-True([bool]$Condition, [string]$Message) {
    $script:checks++
    if (!$Condition) { throw "Assertion failed: $Message" }
}
function Assert-Cache([string]$Expected, [string]$Directory = (Join-Path $fixture 'build\native')) {
    $cache = [IO.File]::ReadAllText((Join-Path $Directory 'CMakeCache.txt'))
    foreach ($name in $experimentNames) {
        Assert-True ($cache -match "(?m)^${name}:BOOL=$Expected`r?$") "$name is $Expected in the real fixture cache"
    }
}
function Invoke-FixtureBuild([switch]$Disable, [switch]$AllTargets, [switch]$FailConfigure, [string]$Directory, [switch]$Gun) {
    $calls = [Collections.Generic.List[object]]::new()
    function cmake {
        $nativeArguments = @($args)
        $calls.Add([pscustomobject]@{ Tool = 'cmake'; Arguments = $nativeArguments })
        if ($nativeArguments[0] -eq '-S') {
            if ($FailConfigure) { $global:LASTEXITCODE = 23 }
            else {
                & $cmakeExe @nativeArguments | Out-Null
                $global:LASTEXITCODE = $LASTEXITCODE
            }
        } else { $global:LASTEXITCODE = 0 }
    }
    function ctest {
        $calls.Add([pscustomobject]@{ Tool = 'ctest'; Arguments = @($args) })
        $global:LASTEXITCODE = 0
    }
    $parameters = @{ Configuration = 'Release'; BuildOnly = !$AllTargets; DisableLocalExperiments = $Disable.IsPresent }
    if ($Gun) { $parameters.ReleaseGun = $true }
    if ($Directory) { $parameters.BuildDirectory = $Directory }
    $failure = $null
    try { & (Join-Path $fixture 'tools\build.ps1') @parameters | Out-Null } catch { $failure = $_ }
    return [pscustomobject]@{ Calls = $calls.ToArray(); Failure = $failure }
}

try {
    $null = New-Item -ItemType Directory -Path (Join-Path $fixture 'tools')
    [IO.File]::WriteAllText($marker, $nonce, $utf8)
    [IO.File]::Copy((Join-Path $projectRoot 'tools\build.ps1'), (Join-Path $fixture 'tools\build.ps1'), $false)
    $cmakeSource = [IO.File]::ReadAllText((Join-Path $projectRoot 'CMakeLists.txt'))
    $options = [regex]::Matches($cmakeSource, '(?m)^option\((MGS_LOCAL_[A-Z0-9_]+) [^\r\n]+\)')
    $backgroundOption = [regex]::Match($cmakeSource, '(?m)^option\(MGS_BACKGROUND_WORKER [^\r\n]+\)')
    $experimentNames = @($options | ForEach-Object { $_.Groups[1].Value })
    Assert-True ($experimentNames.Count -gt 0) 'the fixture covers the current local research options'
    $fixtureCmake = "cmake_minimum_required(VERSION 3.20)`nproject(build_profile_fixture LANGUAGES NONE)`n" +
        (($options | ForEach-Object { $_.Value }) -join "`n") + "`n" + $backgroundOption.Value + "`n"
    [IO.File]::WriteAllText((Join-Path $fixture 'CMakeLists.txt'), $fixtureCmake, $utf8)

    $first = Invoke-FixtureBuild
    Assert-True ($null -eq $first.Failure) 'the normal development configure succeeds'
    Assert-Cache 'OFF'
    $seedArguments = @('-S', $fixture, '-B', (Join-Path $fixture 'build\native')) +
        @($experimentNames | ForEach-Object { "-D$_=ON" })
    if ($backgroundOption.Success) { $seedArguments += '-DMGS_BACKGROUND_WORKER=ON' }
    & $cmakeExe @seedArguments | Out-Null
    Assert-True ($LASTEXITCODE -eq 0) 'real CMake accepts the seeded ON cache'
    Assert-Cache 'ON'
    if ($backgroundOption.Success) {
        Assert-True ([IO.File]::ReadAllText((Join-Path $fixture 'build\native\CMakeCache.txt')) -match '(?m)^MGS_BACKGROUND_WORKER:BOOL=ON\r?$') 'seeding records the explicit runtime-worker cache value'
    }

    $ordinary = Invoke-FixtureBuild
    Assert-True ($null -eq $ordinary.Failure) 'ordinary development builds retain existing cache behavior'
    Assert-Cache 'ON'
    if ($backgroundOption.Success) {
        Assert-True ([IO.File]::ReadAllText((Join-Path $fixture 'build\native\CMakeCache.txt')) -match '(?m)^MGS_BACKGROUND_WORKER:BOOL=ON\r?$') 'ordinary development retains the explicit runtime-worker cache value'
    }
    Assert-True (@($ordinary.Calls[0].Arguments | Where-Object { $_ -like '-DMGS_LOCAL_*' }).Count -eq 0) 'no research overrides are sent by default'

    # The actual script configures a second real CMake cache. All compilation
    # and CTest calls remain inert, including the full-build directory checks.
    $alternateRelative = 'build\worker cache ' + [char]0x96EA
    $alternate = Join-Path $fixture $alternateRelative
    $alternateBuild = Invoke-FixtureBuild -Disable -Directory $alternateRelative -AllTargets
    Assert-True ($null -eq $alternateBuild.Failure) 'a relative spaced/non-ASCII worker directory configures'
    Assert-Cache 'OFF' $alternate
    Assert-Cache 'ON'
    Assert-True ($alternateBuild.Calls[0].Arguments[3] -ceq $alternate) 'configure resolves its directory against the checkout'
    Assert-True ($alternateBuild.Calls[1].Arguments[1] -ceq $alternate) 'all-target compilation uses that exact directory'
    Assert-True ($alternateBuild.Calls[2].Arguments[1] -ceq $alternate) 'CTest uses that exact directory'
    Assert-True (@($alternateBuild.Calls[0].Arguments | Where-Object { $_ -like '-DCMAKE_MAKE_PROGRAM=*ninja.exe' }).Count -eq 1) 'a worker directory preserves the selected toolset'
    Assert-True ($alternateBuild.Calls[0].Arguments -contains '-DCMAKE_BUILD_TYPE=Release') 'a worker directory preserves the selected configuration'
    $absoluteBuild = Invoke-FixtureBuild -Disable -Directory $alternate
    Assert-True ($null -eq $absoluteBuild.Failure -and $absoluteBuild.Calls[0].Arguments[3] -ceq $alternate -and
        $absoluteBuild.Calls[1].Arguments[1] -ceq $alternate -and $absoluteBuild.Calls.Count -eq 2) 'an absolute worker directory preserves loader-only build behavior'
    Assert-Cache 'ON'

    $disabled = Invoke-FixtureBuild -Disable
    Assert-True ($null -eq $disabled.Failure) 'the package profile configure succeeds'
    Assert-Cache 'OFF'
    if ($backgroundOption.Success) {
        Assert-True ([IO.File]::ReadAllText((Join-Path $fixture 'build\native\CMakeCache.txt')) -match '(?m)^MGS_BACKGROUND_WORKER:BOOL=OFF\r?$') 'normal packaging clears cached runtime-worker ON'
    }
    $sent = @($disabled.Calls[0].Arguments | Where-Object { $_ -like '-DMGS_LOCAL_*' })
    Assert-True ($sent.Count -eq $experimentNames.Count) 'all current research options receive exactly one override'
    foreach ($name in $experimentNames) { Assert-True (@($sent | Where-Object { $_ -ceq "-D$name=OFF" }).Count -eq 1) "$name receives OFF exactly once" }
    Assert-True ($disabled.Calls.Count -eq 2 -and $disabled.Calls[1].Tool -eq 'cmake' -and
        ($disabled.Calls[1].Arguments -join ' ') -match '--target mgs_coop_probe --parallel') 'BuildOnly still builds only the loader target'
    Assert-True ($disabled.Calls[0].Arguments -contains '-DCMAKE_BUILD_TYPE=Release') 'configuration still reaches CMake literally'
    Assert-True (@($disabled.Calls[0].Arguments | Where-Object { $_ -ceq '-DMGS_BACKGROUND_WORKER=OFF' }).Count -eq 1) 'normal packaging explicitly disables the separate runtime worker option'
    Assert-True (@($disabled.Calls[0].Arguments | Where-Object { $_ -like '-DCMAKE_MAKE_PROGRAM=*ninja.exe' }).Count -eq 1) 'the existing selected Ninja toolset is preserved'

    # The friend/release loader (-ReleaseGun): exactly the stock46 gun pieces ON, everything else and the worker OFF.
    $gunDirectory = 'build\release-friend'
    $gun = Invoke-FixtureBuild -Gun -Directory $gunDirectory
    Assert-True ($null -eq $gun.Failure) 'the release-gun configure succeeds'
    $gunSent = @($gun.Calls[0].Arguments | Where-Object { $_ -like '-DMGS_LOCAL_*' })
    $gunOn = @('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST', 'MGS_LOCAL_STOCK46_PRELOAD_TEST', 'MGS_LOCAL_STOCK46_WRITER_TEST')
    Assert-True ($gunSent.Count -eq $experimentNames.Count) 'release-gun: every research option receives exactly one override'
    foreach ($name in $experimentNames) {
        $want = if ($gunOn -contains $name) { 'ON' } else { 'OFF' }
        Assert-True (@($gunSent | Where-Object { $_ -ceq "-D$name=$want" }).Count -eq 1) "release-gun: $name is $want"
    }
    Assert-True (@($gun.Calls[0].Arguments | Where-Object { $_ -ceq '-DMGS_BACKGROUND_WORKER=OFF' }).Count -eq 1) 'release-gun never enables the background worker'
    Assert-True ($gun.Calls[0].Arguments[3] -ceq (Join-Path $fixture $gunDirectory)) 'release-gun builds into its own directory'
    $both = Invoke-FixtureBuild -Gun -Disable
    Assert-True ($null -ne $both.Failure -and $both.Failure.Exception.Message -match 'either' -and $both.Calls.Count -eq 0) '-ReleaseGun and -DisableLocalExperiments are exclusive'
    $all = Invoke-FixtureBuild -Disable -AllTargets
    Assert-True ($null -eq $all.Failure -and $all.Calls.Count -eq 3 -and $all.Calls[2].Tool -eq 'ctest') 'the full build path still schedules its required checks'
    $failed = Invoke-FixtureBuild -Disable -FailConfigure
    Assert-True ($null -ne $failed.Failure -and $failed.Failure.Exception.Message -eq 'CMake configuration failed' -and
        $failed.Calls.Count -eq 1) 'a failed configure cannot reach compilation or checks'

    $parseTokens = $null; $parseErrors = $null
    $packageAst = [Management.Automation.Language.Parser]::ParseFile((Join-Path $projectRoot 'tools\package.ps1'), [ref]$parseTokens, [ref]$parseErrors)
    Assert-True ($parseErrors.Count -eq 0) 'the package script parses without errors'
    $buildCalls = @($packageAst.FindAll({ param($node)
        $node -is [Management.Automation.Language.CommandAst] -and
            $node.CommandElements[0].Extent.Text -ceq '(Join-Path $PSScriptRoot ''build.ps1'')'
    }, $true))
    Assert-True ($buildCalls.Count -eq 1) 'the package has one normal build invocation'
    $parameterNames = @($buildCalls[0].CommandElements | Where-Object { $_ -is [Management.Automation.Language.CommandParameterAst] } | ForEach-Object { $_.ParameterName })
    Assert-True ($parameterNames -contains 'DisableLocalExperiments' -and $parameterNames -contains 'BuildOnly') 'normal packaging requests the explicit OFF loader build'
    $branch = $buildCalls[0].Parent.Parent.Parent
    Assert-True ($branch -is [Management.Automation.Language.IfStatementAst] -and $branch.Clauses[0].Item1.Extent.Text -eq '$LoaderPath') 'a supplied loader continues to bypass the build branch'
    # Run only that exact branch with a typed inert build stub, never the
    # package script's enumeration, archive, installation or cleanup flow.
    $buildReceipt = Join-Path $fixture 'tools\build-invocation.json'
    $mockBuild = @'
param([string]$Configuration, [switch]$BuildOnly, [switch]$DisableLocalExperiments, [string]$BuildDirectory)
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'build-invocation.json'), (
    [ordered]@{ configuration = $Configuration; build_only = $BuildOnly.IsPresent; disable_local_experiments = $DisableLocalExperiments.IsPresent; build_directory = $BuildDirectory } | ConvertTo-Json
), [Text.UTF8Encoding]::new($false))
'@
    [IO.File]::WriteAllText((Join-Path $fixture 'tools\build.ps1'), $mockBuild, $utf8)
    $branchScript = Join-Path $fixture 'tools\package-build-branch.ps1'
    # The branch also holds the -Friend Release-receipt clause (not taken here: no -Friend).
    [IO.File]::WriteAllText($branchScript, "param([string]`$LoaderPath, [switch]`$Friend)`n`$ErrorActionPreference = 'Stop'`n" + $branch.Extent.Text + "`n", $utf8)
    & $branchScript | Out-Null
    $receipt = [IO.File]::ReadAllText($buildReceipt) | ConvertFrom-Json
    Assert-True ($receipt.configuration -ceq 'Release' -and $receipt.build_only -and $receipt.disable_local_experiments) 'the exact normal package branch binds all three typed build parameters'
    Assert-True ($receipt.build_directory -ceq 'build\package-loader') 'the local package builds its own loader directory (never build/native, the lab research loader)'
    Remove-Item -LiteralPath $buildReceipt
    $suppliedLoader = Join-Path $fixture 'supplied-loader.dll'
    [IO.File]::WriteAllText($suppliedLoader, 'INERT supplied loader; settings unknown', $utf8)
    $suppliedOutput = & $branchScript -LoaderPath $suppliedLoader | Out-String
    Assert-True (!(Test-Path -LiteralPath $buildReceipt) -and $suppliedOutput.Contains('(no build).') -and
        !$suppliedOutput.Contains('disabled')) 'a supplied artifact is honestly reported without configuring or claiming its flags are OFF'
    Write-Output "Build profile checks passed: $checks. No compilation, package, installation or game actions ran."
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    if (Test-Path -LiteralPath $fixture) {
        $resolvedFixture = (Resolve-Path -LiteralPath $fixture).Path
        $resolvedBuild = (Resolve-Path -LiteralPath $buildRoot).Path
        if ((Split-Path -Parent $resolvedFixture) -ne $resolvedBuild -or
            (Split-Path -Leaf $resolvedFixture) -ne ('build-profile-tests-' + $nonce) -or
            !(Test-Path -LiteralPath $marker -PathType Leaf) -or [IO.File]::ReadAllText($marker) -ne $nonce) { throw 'Fixture cleanup ownership check failed' }
        foreach ($item in @((Get-Item -LiteralPath $resolvedFixture)) + @(Get-ChildItem -LiteralPath $resolvedFixture -Recurse -Force)) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Fixture cleanup refuses filesystem links' }
        }
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    }
}
