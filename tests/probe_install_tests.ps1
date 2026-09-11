[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$testProjectRoot = Split-Path -Parent $PSScriptRoot
$testBuildRoot = [IO.Path]::GetFullPath((Join-Path $testProjectRoot 'build'))
$fixtureNonce = [Guid]::NewGuid().ToString('N')
$fixtureRoot = Join-Path $testBuildRoot ('probe-install-tests-' + $fixtureNonce)
$fixtureGamesRoot = Join-Path $fixtureRoot 'games'
$fixtureScript = Join-Path $fixtureRoot 'tools/probe.ps1'
$fixtureMarker = Join-Path $fixtureRoot '.fixture-owner'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (!$Condition) { throw ('Assertion failed: ' + $Message) }
}

function Assert-Fails {
    param([scriptblock]$Operation, [string]$ExpectedMessage)
    $observedFailure = $null
    try { & $Operation | Out-Null } catch { $observedFailure = $_ }
    Assert-True ($null -ne $observedFailure) ('Expected failure: ' + $ExpectedMessage)
    Assert-True ($observedFailure.Exception.Message.Contains($ExpectedMessage)) (
        'Unexpected failure: ' + $observedFailure.Exception.Message)
}

function New-GameFixture {
    param([string]$Name)
    if ($Name -notmatch '^[a-z-]+$') { throw 'Invalid fixture name' }
    $directory = Join-Path $fixtureGamesRoot $Name
    New-Item -ItemType Directory -Path $directory | Out-Null
    [IO.File]::WriteAllText((Join-Path $directory 'mgsvtpp.exe'), 'INERT executable fixture; never executed')
    return $directory
}

function Invoke-FixtureProbe {
    param(
        [ValidateSet('Status', 'Install', 'Remove')][string]$Action,
        [string]$Game,
        [switch]$FailManifestWrite
    )
    # This check precedes the invocation and the mock. No caller can direct the
    # installer at a real installation through this helper.
    $resolvedGame = (Resolve-Path -LiteralPath $Game).Path
    $allowedPrefix = [IO.Path]::GetFullPath($fixtureGamesRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (!$resolvedGame.StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Probe test invocation escaped its inert game fixtures'
    }
    if ((Get-Item -LiteralPath $resolvedGame).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Fixture game directory must not be a filesystem link'
    }

    & {
        param($ProbeScript, $ProbeAction, $ProbeGame, $InjectWriteFailure)

        # Local to this invocation, solely because the actual game may be running.
        # All writes still go to the checked inert fixture supplied explicitly.
        function Get-Process {
            [CmdletBinding()]
            param([Parameter(Position = 0)][string[]]$Name)
        }

        if ($InjectWriteFailure) {
            $expectedStageDirectory = Join-Path $ProbeGame 'mgs-coop'
            function Set-Content {
                [CmdletBinding()]
                param(
                    [string]$LiteralPath,
                    [string]$Encoding,
                    [Parameter(ValueFromPipeline = $true)][object]$Value
                )
                process {
                    if ((Split-Path -Parent $LiteralPath) -ne $expectedStageDirectory -or
                        [IO.Path]::GetFileName($LiteralPath) -notmatch '^probe-install\.[0-9a-f]{32}\.tmp$') {
                        throw 'Failure injection attempted a non-staging write'
                    }
                    [IO.File]::WriteAllText($LiteralPath, '{partially-written')
                    throw 'Injected partial staging write failure'
                }
            }
        }
        & $ProbeScript -Action $ProbeAction -GameDirectory $ProbeGame
    } $fixtureScript $Action $resolvedGame $FailManifestWrite.IsPresent
}

function Remove-OwnedFixture {
    if (!(Test-Path -LiteralPath $fixtureRoot)) { return }
    $resolvedFixture = (Resolve-Path -LiteralPath $fixtureRoot).Path
    $resolvedBuild = (Resolve-Path -LiteralPath $testBuildRoot).Path
    $expectedName = 'probe-install-tests-' + $fixtureNonce
    if ((Split-Path -Parent $resolvedFixture) -ne $resolvedBuild -or
        (Split-Path -Leaf $resolvedFixture) -ne $expectedName -or
        !(Test-Path -LiteralPath $fixtureMarker -PathType Leaf) -or
        [IO.File]::ReadAllText($fixtureMarker) -ne $fixtureNonce) {
        throw 'Refusing cleanup: fixture ownership or absolute boundary check failed'
    }
    $fixtureItems = @((Get-Item -LiteralPath $resolvedFixture)) +
        @(Get-ChildItem -LiteralPath $resolvedFixture -Force -Recurse)
    foreach ($item in $fixtureItems) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Refusing recursive cleanup of a fixture containing filesystem links'
        }
    }
    # The exact absolute target and every owned descendant were checked above.
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
}

if (!(Test-Path -LiteralPath $testBuildRoot)) {
    New-Item -ItemType Directory -Path $testBuildRoot | Out-Null
}
if ((Get-Item -LiteralPath $testBuildRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw 'Test build directory must not be a filesystem link'
}
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
[IO.File]::WriteAllText($fixtureMarker, $fixtureNonce)

try {
    New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'tools') | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'build/native') -Force | Out-Null
    New-Item -ItemType Directory -Path $fixtureGamesRoot | Out-Null
    # Copy only the installer under test. Its derived project root and source DLL
    # now refer to this self-contained fixture; no compiled or installed DLL runs.
    [IO.File]::Copy((Join-Path $testProjectRoot 'tools/probe.ps1'), $fixtureScript, $false)
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'build/native/dinput8.dll'), 'INERT original probe payload')

    $ordinary = New-GameFixture 'ordinary'
    $ordinarySteam = Join-Path $ordinary 'steam_api64.dll'
    [IO.File]::WriteAllText($ordinarySteam, 'INERT existing Steam DLL fixture')
    $exeHash = (Get-FileHash -LiteralPath (Join-Path $ordinary 'mgsvtpp.exe') -Algorithm SHA256).Hash
    $steamHash = (Get-FileHash -LiteralPath $ordinarySteam -Algorithm SHA256).Hash
    Invoke-FixtureProbe -Action Install -Game $ordinary | Out-Null
    $ordinaryDll = Join-Path $ordinary 'dinput8.dll'
    $ordinaryRecord = Join-Path $ordinary 'mgs-coop/probe-install.json'
    $installedRecord = Get-Content -LiteralPath $ordinaryRecord -Raw | ConvertFrom-Json
    Assert-True ($installedRecord.dll_sha256 -eq (Get-FileHash -LiteralPath $ordinaryDll -Algorithm SHA256).Hash) 'Installation records actual loader hash'
    Assert-True ($installedRecord.game_directory -eq $ordinary) 'Installation records exact fixture directory'
    $retainedLog = Join-Path $ordinary 'mgs-coop/probe.log'
    [IO.File]::WriteAllText($retainedLog, 'retained diagnostics')
    Invoke-FixtureProbe -Action Remove -Game $ordinary | Out-Null
    Assert-True (!(Test-Path -LiteralPath $ordinaryDll)) 'Normal removal deletes owned loader'
    Assert-True (!(Test-Path -LiteralPath $ordinaryRecord)) 'Normal removal deletes ownership record'
    Assert-True ([IO.File]::ReadAllText($retainedLog) -eq 'retained diagnostics') 'Normal removal preserves diagnostic log'
    Assert-True ((Get-FileHash -LiteralPath (Join-Path $ordinary 'mgsvtpp.exe') -Algorithm SHA256).Hash -eq $exeHash) 'Executable is unchanged'
    Assert-True ((Get-FileHash -LiteralPath $ordinarySteam -Algorithm SHA256).Hash -eq $steamHash) 'Steam DLL is unchanged'

    $foreign = New-GameFixture 'foreign-loader'
    $foreignDll = Join-Path $foreign 'dinput8.dll'
    [IO.File]::WriteAllText($foreignDll, 'INERT foreign loader')
    $foreignHash = (Get-FileHash -LiteralPath $foreignDll -Algorithm SHA256).Hash
    Assert-Fails { Invoke-FixtureProbe -Action Install -Game $foreign } 'An existing loader or installation record is present'
    Assert-Fails { Invoke-FixtureProbe -Action Remove -Game $foreign } 'No owned installation record'
    Assert-True ((Get-FileHash -LiteralPath $foreignDll -Algorithm SHA256).Hash -eq $foreignHash) 'Foreign loader is preserved'
    Assert-True (!(Test-Path -LiteralPath (Join-Path $foreign 'mgs-coop/probe-install.json'))) 'Refusal cannot claim ownership of foreign loader'

    $modified = New-GameFixture 'modified-loader'
    Invoke-FixtureProbe -Action Install -Game $modified | Out-Null
    $modifiedDll = Join-Path $modified 'dinput8.dll'
    $modifiedRecord = Join-Path $modified 'mgs-coop/probe-install.json'
    $originalRecordHash = (Get-FileHash -LiteralPath $modifiedRecord -Algorithm SHA256).Hash
    [IO.File]::WriteAllText($modifiedDll, 'INERT changed loader after installation')
    $modifiedHash = (Get-FileHash -LiteralPath $modifiedDll -Algorithm SHA256).Hash
    Assert-Fails { Invoke-FixtureProbe -Action Remove -Game $modified } 'The loader has changed'
    Assert-True ((Get-FileHash -LiteralPath $modifiedDll -Algorithm SHA256).Hash -eq $modifiedHash) 'Changed loader is preserved'
    Assert-True ((Get-FileHash -LiteralPath $modifiedRecord -Algorithm SHA256).Hash -eq $originalRecordHash) 'Refusal preserves installation evidence'

    $missingExe = New-GameFixture 'missing-executable'
    Invoke-FixtureProbe -Action Install -Game $missingExe | Out-Null
    Remove-Item -LiteralPath (Join-Path $missingExe 'mgsvtpp.exe')
    Invoke-FixtureProbe -Action Remove -Game $missingExe | Out-Null
    Assert-True (!(Test-Path -LiteralPath (Join-Path $missingExe 'dinput8.dll'))) 'Owned loader can be removed without game executable'
    Assert-True (!(Test-Path -LiteralPath (Join-Path $missingExe 'mgs-coop/probe-install.json'))) 'Missing executable uninstall removes record'

    $failedStage = New-GameFixture 'partial-stage-failure'
    Assert-Fails { Invoke-FixtureProbe -Action Install -Game $failedStage -FailManifestWrite } 'Injected partial staging write failure'
    Assert-True (!(Test-Path -LiteralPath (Join-Path $failedStage 'dinput8.dll'))) 'Partial staging failure leaves no loader'
    Assert-True (!(Test-Path -LiteralPath (Join-Path $failedStage 'mgs-coop/probe-install.json'))) 'Partial staging failure leaves no malformed final record'
    $stageFiles = @(Get-ChildItem -LiteralPath (Join-Path $failedStage 'mgs-coop') -Filter 'probe-install.*.tmp' -Force)
    Assert-True ($stageFiles.Count -eq 0) 'Partial staging failure cleans its unique temporary record'
    Invoke-FixtureProbe -Action Install -Game $failedStage | Out-Null
    Invoke-FixtureProbe -Action Remove -Game $failedStage | Out-Null
    Assert-True (!(Test-Path -LiteralPath (Join-Path $failedStage 'dinput8.dll'))) 'Normal reinstall and removal succeed after staging failure'

    Write-Output 'probe_install_tests: all 5 isolated installation and recovery scenarios passed'
} finally {
    Remove-OwnedFixture
}
