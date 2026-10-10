#Requires -Version 5.1
# Inert owned fixtures only: no game, Steam, network or visible windows.
[CmdletBinding()]
param([string]$IniCheck)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $repo 'tools\friend\FriendCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repo 'tools\friend\LocalCoopSession.psm1') -Force -DisableNameChecking
$module = Get-Module LocalCoopSession
$fixture = Join-Path (Join-Path $repo 'build') ('local-coop-session-tests-' + [Guid]::NewGuid().ToString('N'))
$token = [Guid]::NewGuid().ToString('N')
$utf8 = New-Object Text.UTF8Encoding $false
$script:checks = 0
function Assert-True([bool]$Condition, [string]$Message) { $script:checks++; if (!$Condition) { throw "Assertion failed: $Message" } }
function Assert-Equal($Actual, $Expected, [string]$Message) { $script:checks++; if ($Actual -cne $Expected) { throw "Assertion failed: $Message (expected '$Expected', got '$Actual')" } }
function Assert-Throws([scriptblock]$Action, [string]$Expected, [string]$Message) {
    $script:checks++; $failure = $null
    try { & $Action | Out-Null } catch { $failure = $_ }
    if (!$failure -or ($Expected -and !$failure.Exception.Message.Contains($Expected))) { throw "Assertion failed: $Message ($failure)" }
}
function New-FixturePair([string]$Name) {
    $pair = [ordered]@{}
    foreach ($role in @('host', 'guest')) {
        $root = Join-Path (Join-Path $fixture $Name) $role
        $null = New-Item -ItemType Directory -Path (Join-Path $root 'mgs-coop') -Force
        [IO.File]::WriteAllText((Join-Path $root 'mgsvtpp.exe'), 'INERT NEVER EXECUTE', [Text.Encoding]::ASCII)
        foreach ($folder in @('coop-saves', 'lab\saves', 'local-test-saves')) {
            $save = Join-Path (Join-Path $root 'mgs-coop') $folder
            $null = New-Item -ItemType Directory -Path $save -Force
            [IO.File]::WriteAllText((Join-Path $save 'fixture-save'), ($Name + '/' + $role + '/' + $folder), [Text.Encoding]::ASCII)
        }
        $pair[$role] = $root
    }
    return [pscustomobject]$pair
}
function New-FixtureSession($Pair) {
    $run = Join-Path (Join-Path (Join-Path $Pair.host 'mgs-coop') 'local-test-runs') ([Guid]::NewGuid().ToString('N'))
    return New-LocalCoopSession -HostRoot $Pair.host -GuestRoot $Pair.guest -RunDirectory $run
}
function Invoke-NativeCheck([string]$Path) {
    $output = @(& $IniCheck lab $Path)
    if ($LASTEXITCODE -ne 0) { throw 'Native lab_ini_check failed.' }
    $map = @{}
    foreach ($line in $output) { $split = $line.IndexOf('='); if ($split -gt 0) { $map[$line.Substring(0, $split)] = $line.Substring($split + 1) } }
    return $map
}
$null = New-Item -ItemType Directory -Path $fixture
[IO.File]::WriteAllText((Join-Path $fixture '.fixture-owner'), $token, [Text.Encoding]::ASCII)
try {
    $saveDir = 'D:\inert\local-saves'
    $early = 'early=start; version=fixture; lab=1; role=host' + [char]10 + 'saves=redirected; dir=' + $saveDir + [char]10
    $steam = 'master_server=redirected; reason=fixture; url=http://127.0.0.1:47899/tppstm/gate' + [char]10
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role host -SaveDir $saveDir).State 'pending' 'empty fresh logs cannot establish startup'
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role host -SaveDir $saveDir -Final).State 'unsafe' 'missing startup safety times out unsafe'
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role host -SaveDir $saveDir -EarlyLog $early -SteamLog $steam).State 'ok' 'host startup safe before user Continue'
    $guestEarly = $early.Replace('role=host', 'role=guest') + 'mutex=renamed; suffix=-guest' + [char]10 + 'identity=fake; steamid=76561202255233008' + [char]10
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role guest -SaveDir $saveDir -EarlyLog $guestEarly -SteamLog $steam).State 'ok' 'guest startup requires both qualified coexistence gates'
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role guest -SaveDir $saveDir -EarlyLog $early.Replace('role=host', 'role=guest') -SteamLog $steam).State 'pending' 'guest missing mutex and fake identity stays pending'
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role guest -SaveDir $saveDir -EarlyLog $guestEarly.Replace('suffix=-guest', 'suffix=-other') -SteamLog $steam).State 'unsafe' 'wrong guest mutex refuses immediately'
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role guest -SaveDir $saveDir -EarlyLog $guestEarly.Replace('steamid=76561202255233008', 'steamid=76561197960265729') -SteamLog $steam).State 'unsafe' 'wrong guest identity refuses immediately'
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role host -SaveDir $saveDir -EarlyLog $early.Replace('lab=1', 'lab=0') -SteamLog $steam).State 'unsafe' 'disabled lab refuses immediately'
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role host -SaveDir $saveDir -EarlyLog $early.Replace('role=host', 'role=guest') -SteamLog $steam).State 'unsafe' 'wrong role refuses immediately'
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role host -SaveDir $saveDir -EarlyLog $early.Replace($saveDir, 'D:\other') -SteamLog $steam).State 'unsafe' 'different save copy refuses immediately'
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role host -SaveDir $saveDir -EarlyLog ($early + 'saves=unchanged' + [char]10) -SteamLog $steam).State 'unsafe' 'unchanged real-save routing refuses immediately'
    Assert-Equal (Get-LocalCoopLaunchEvidence -Role host -SaveDir $saveDir -EarlyLog $early -SteamLog $steam.Replace(':47899/', ':12345/')).State 'unsafe' 'different master URL refuses immediately'
    $pair = New-FixturePair 'normal'
    $savePaths = @(Get-ChildItem -LiteralPath (Join-Path $fixture 'normal') -Recurse -File | Where-Object { $_.Name -ceq 'fixture-save' })
    $saveBefore = @($savePaths | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName).Hash }) -join ','
    foreach ($role in @('host', 'guest')) {
        $ini = New-LocalCoopLabIniText -Role $role -SaveDir (Join-Path $pair.$role 'mgs-coop\local-test-saves') -HostSteamId '76561197960265729'
        $parsed = ConvertFrom-IniText $ini
        Assert-Equal $parsed.mode 'lab' "$role uses qualified two-instance lab mode"
        Assert-Equal $parsed.join_trigger '0' "$role has no automatic native session join"
        Assert-Equal $parsed.loopback '1' "$role native networking remains local"
        Assert-Equal $parsed.net_bind 'loopback' "$role partner networking remains local"
        Assert-Equal $parsed.partner_native_profile 'gun' "$role uses the shipping native gun profile"
        Assert-Equal $parsed.world '3' "$role defaults to world level 3"
        Assert-True (!$parsed.Contains('world_probe') -and !$parsed.Contains('auto_join_delay_s')) "$role has no development probes or automatic title actions"
        Assert-Equal $parsed.net_listen_port $(if ($role -eq 'host') { '47820' } else { '47821' }) "$role partner port"
        Assert-Equal $parsed.net_peer_port $(if ($role -eq 'host') { '47821' } else { '47820' }) "$role peer port"
        Assert-Equal $parsed.xinput $(if ($role -eq 'host') { 'none' } else { '0' }) "$role controller routing"
        Assert-Equal $parsed.peer_steamid $(if ($role -eq 'host') { '76561202255233008' } else { '76561197960265729' }) "$role peer identity"
        if ($role -eq 'guest') { Assert-Equal $parsed.mutex_suffix '-guest' 'second-instance mutex'; Assert-Equal $parsed.self_name 'Guest' 'guest display name' }
        if ($IniCheck) {
            $file = Join-Path $fixture ($role + '.ini')
            [IO.File]::WriteAllText($file, $ini, $utf8)
            $native = Invoke-NativeCheck $file
            Assert-Equal $native.invalid_values '0' "$role native parser admits every key"
            Assert-Equal $native.unknown_keys '0' "$role native parser recognizes every key"
            Assert-Equal $native.mode 'lab' "$role native mode"
            Assert-Equal $native.partner_drive 'native' "$role native profile expanded"
            Assert-Equal $native.partner_input_defer 'on' "$role native deferred input expanded"
            Assert-Equal $native.partner_native_gun '1' "$role native gun default expanded"
        }
    }
    $clips = ConvertFrom-IniText (New-LocalCoopLabIniText -Role host -SaveDir (Join-Path $pair.host 'mgs-coop\local-test-saves') -Partner clips -World 0)
    Assert-True (!$clips.Contains('partner_native_profile') -and !$clips.Contains('world')) 'clip and world opt-outs remain explicit'
    Assert-Throws { New-LocalCoopLabIniText -Role host -SaveDir 'relative' } 'absolute' 'relative save path refused'
    Assert-Throws { New-LocalCoopLabIniText -Role host -SaveDir 'D:\copy' -HostPort 47820 -GuestPort 47820 } 'ports' 'colliding partner ports refused'
    Assert-Throws { New-LocalCoopLabIniText -Role host -SaveDir 'D:\copy' -HostPort 47810 } 'ports' 'legacy port collision refused'
    Assert-Throws { New-LocalCoopLabIniText -Role guest -SaveDir 'D:\copy' -HostSteamId '76561202255233008' } 'different' 'fake guest cannot equal real host'
    Assert-Throws { New-LocalCoopSession -HostRoot $pair.host -GuestRoot $pair.host -RunDirectory (Join-Path $fixture 'bad') } 'distinct' 'same game root refused'
    Assert-Throws { New-LocalCoopSession -HostRoot $pair.host -GuestRoot $pair.guest -RunDirectory (Join-Path $fixture 'bad') } 'owned' 'run location outside host mod refused'

    $originals = @{}
    $names = @('lab.ini', 'coop.ini', 'link-secret.txt', 'script-observe.enabled')
    foreach ($role in @('host', 'guest')) {
        foreach ($name in $names) {
            $file = Join-Path (Join-Path $pair.$role 'mgs-coop') $name
            # Host absent files and the guest's existing binary config exercise both paths.
            if ($role -eq 'guest') {
                $bytes = [byte[]]@(0, 255, 13, 10, 34, 65)
                [IO.File]::WriteAllBytes($file, $bytes)
                $originals[$role + '/' + $name] = [Convert]::ToBase64String($bytes)
            }
        }
    }
    $session = New-FixtureSession $pair
    Assert-Equal $session.state 'prepared' 'session starts prepared'
    Assert-Equal @($session.pointers).Count 2 'both game folders receive ownership pointers'
    Assert-True (Test-LocalCoopSessionOwnerLive $session) 'current owner is live'
    Assert-Throws { Invoke-LocalCoopRecovery -HostRoot $pair.guest } 'still active' 'guest preflight finds live host session'
    Assert-Throws { New-FixtureSession $pair } 'pointer' 'another session cannot replace ownership pointers'
    foreach ($role in @('host', 'guest')) {
        foreach ($name in $names) {
            Set-LocalCoopSessionFile -Session $session -Role $role -Name $name -Bytes $utf8.GetBytes('test/' + $role + '/' + $name)
            Assert-Equal $session.files[-1].phase 'written' "$role/$name hash verified after activation"
        }
    }
    Assert-Throws { Set-LocalCoopSessionFile -Session $session -Role host -Name 'lab.ini' -Bytes ([byte[]]@(1)) } 'already' 'repeated activation record refused'
    Assert-Throws { Set-LocalCoopSessionFile -Session $session -Role host -Name '..\coop-saves\fixture-save' -Bytes ([byte[]]@(1)) } '' 'journal refuses save or traversal target'
    $loaded = Read-LocalCoopSession -RunDirectory $session.run_directory
    Assert-Equal @($loaded.files).Count 8 'journal round-trips all eight activation records'
    $result = Restore-LocalCoopSession -Session $loaded
    Assert-Equal $result.State 'off' 'successful restore turns session off'
    Assert-Equal @($result.Restored).Count 4 'all guest originals restored'
    foreach ($role in @('host', 'guest')) {
        foreach ($name in $names) {
            $file = Join-Path (Join-Path $pair.$role 'mgs-coop') $name
            if ($role -eq 'host') { Assert-True (!(Test-Path -LiteralPath $file)) "$role/$name newly created file removed" }
            else { Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($file))) $originals[$role + '/' + $name] "$role/$name original bytes restored" }
        }
        Assert-True (!(Test-Path -LiteralPath (Join-Path $pair.$role 'mgs-coop\local-test-session.json'))) "$role pointer removed after complete restore"
    }
    Assert-Equal (Restore-LocalCoopSession -Session $loaded).State 'off' 'restore is idempotent'
    Assert-Equal (@($savePaths | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName).Hash }) -join ',') $saveBefore 'co-op, lab and local save fixture bytes unchanged'

    $pair = New-FixturePair 'crash'
    $session = New-FixtureSession $pair
    $path = Join-Path $pair.host 'mgs-coop\lab.ini'
    Set-LocalCoopSessionFile -Session $session -Role host -Name lab.ini -Bytes ([byte[]]@(1, 2, 3))
    $session.files[0].phase = 'planned'; Save-LocalCoopSession $session
    # Crash after write but before recording phase: written SHA still restores safely.
    Assert-Equal (Restore-LocalCoopSession (Read-LocalCoopSession $session.run_directory)).State 'off' 'crash after file write is recovered'
    Assert-True (!(Test-Path -LiteralPath $path)) 'new activation removed after crash'
    $session = New-FixtureSession $pair
    [IO.File]::WriteAllBytes($path, [byte[]]@(7, 8))
    Set-LocalCoopSessionFile -Session $session -Role host -Name lab.ini -Bytes ([byte[]]@(1, 2))
    [IO.File]::WriteAllBytes($path, [byte[]]@(7, 8))
    $session.files[0].phase = 'planned'; Save-LocalCoopSession $session
    Assert-Equal (Restore-LocalCoopSession (Read-LocalCoopSession $session.run_directory)).State 'off' 'planned write with original still present is safe'
    Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path))) 'Bwg=' 'planned-write original remains untouched'

    $pair = New-FixturePair 'changed'
    $session = New-FixtureSession $pair
    Set-LocalCoopSessionFile -Session $session -Role guest -Name coop.ini -Bytes ([byte[]]@(5))
    $path = Join-Path $pair.guest 'mgs-coop\coop.ini'
    [IO.File]::WriteAllBytes($path, [byte[]]@(99))
    $result = Restore-LocalCoopSession $session
    Assert-Equal $result.State 'partial' 'changed activation keeps partial session'
    Assert-Equal $result.Kept[0] 'guest/coop.ini' 'changed activation identified'
    Assert-Equal ([IO.File]::ReadAllBytes($path)[0]) 99 'changed activation preserved'
    Assert-True (Test-Path -LiteralPath (Join-Path $pair.host 'mgs-coop\local-test-session.json')) 'partial restore retains ownership pointers'
    $session.launcher_pid = 2147483646; Save-LocalCoopSession $session
    Assert-Throws { Invoke-LocalCoopRecovery -HostRoot $pair.guest } 'left alone' 'guest stale recovery refuses changed activation'
    [IO.File]::WriteAllBytes($path, [byte[]]@(5))
    Assert-Equal @((Invoke-LocalCoopRecovery -HostRoot $pair.guest).Recovered).Count 1 'guest can recover stale host-owned session'
    Assert-True (!(Test-Path -LiteralPath $path)) 'stale recovery removed owned activation'

    $pair = New-FixturePair 'empty'
    $path = Join-Path $pair.guest 'mgs-coop\lab.ini'
    [IO.File]::WriteAllBytes($path, [byte[]]@())
    $session = New-FixtureSession $pair
    Set-LocalCoopSessionFile -Session $session -Role guest -Name lab.ini -Bytes ([byte[]]@())
    Assert-Equal (Restore-LocalCoopSession $session).State 'off' 'empty activation and backup are accepted'
    Assert-Equal (Get-Item -LiteralPath $path).Length 0 'empty existing file stays empty'

    $pair = New-FixturePair 'tampered'
    $session = New-FixtureSession $pair
    $session.run_directory = Join-Path $fixture 'escape'
    Assert-Throws { Save-LocalCoopSession $session } 'escaped' 'journal run-directory traversal refused'
    $session = New-FixtureSession (New-FixturePair 'tampered-record')
    Set-LocalCoopSessionFile -Session $session -Role host -Name lab.ini -Bytes ([byte[]]@(7))
    $session.files[0].name = '..\real-save'
    Assert-Throws { Restore-LocalCoopSession $session } 'activation' 'tampered journal cannot target unrelated files'

    $pair = New-FixturePair 'helper'
    $session = New-FixtureSession $pair
    $session.watchdog_pid = $session.launcher_pid; $session.watchdog_start_utc = $session.launcher_start_utc
    $session.launcher_pid = 2147483646; Save-LocalCoopSession $session
    Assert-True (Test-LocalCoopSessionOwnerLive $session) 'live cleanup helper retains session ownership after launcher exits'
    Assert-Throws { Invoke-LocalCoopRecovery -HostRoot $pair.guest } 'still active' 'guest preflight refuses live cleanup helper'
    $session.watchdog_pid = 0; $session.watchdog_start_utc = ''; Save-LocalCoopSession $session
    Assert-True (!(Test-LocalCoopSessionOwnerLive $session)) 'dead launcher and helper permit recovery'
    Assert-Equal @((Invoke-LocalCoopRecovery -HostRoot $pair.host).Recovered).Count 1 'host recovers abandoned prepared session'
    Assert-Equal @((Invoke-LocalCoopRecovery -HostRoot $pair.host).Recovered).Count 0 'completed recovery is idempotent'

    $pair = New-FixturePair 'pointer-tamper'
    $session = New-FixtureSession $pair
    $pointer = Join-Path $pair.guest 'mgs-coop\local-test-session.json'
    [IO.File]::WriteAllText($pointer, 'foreign pointer', $utf8)
    Assert-Equal (Restore-LocalCoopSession $session).State 'partial' 'changed ownership pointer keeps session partial'
    Assert-Equal ([IO.File]::ReadAllText($pointer)) 'foreign pointer' 'changed ownership pointer is never replaced or removed'
    Assert-Throws { Invoke-LocalCoopRecovery -HostRoot $pair.guest } '' 'unknown pointer refuses recovery'
    $pair = New-FixturePair 'backup-tamper'
    $original = Join-Path $pair.guest 'mgs-coop\lab.ini'
    [IO.File]::WriteAllBytes($original, [byte[]]@(9))
    $session = New-FixtureSession $pair
    Set-LocalCoopSessionFile -Session $session -Role guest -Name lab.ini -Bytes ([byte[]]@(7))
    $session.files[0].original_base64 = [Convert]::ToBase64String([byte[]]@(99))
    Assert-Throws { Restore-LocalCoopSession $session } 'backup hash' 'changed embedded backup refuses every restoration mutation'
    Assert-Equal ([IO.File]::ReadAllBytes($original)[0]) 7 'failed backup audit preserves written activation bytes'
    $pair = New-FixturePair 'missing-pointer'
    $session = New-FixtureSession $pair
    $session.launcher_pid = 2147483646; Save-LocalCoopSession $session
    foreach ($role in @('host', 'guest')) { Remove-Item -LiteralPath (Join-Path $pair.$role 'mgs-coop\local-test-session.json') }
    Assert-Equal @((Invoke-LocalCoopRecovery -HostRoot $pair.host).Recovered).Count 1 'host scans owned journals even if both pointers were removed'

    $pair = New-FixturePair 'busy'
    $session = New-FixtureSession $pair
    & $module { function script:Get-GameProcessInDirectory { param([string]$Directory); return [pscustomobject]@{ Id = 1 } } }
    Assert-Throws { Restore-LocalCoopSession $session } 'both game' 'cleanup refuses a running game before mutation'
    Assert-Throws { Set-LocalCoopSessionFile -Session $session -Role host -Name lab.ini -Bytes ([byte[]]@(7)) } 'both game' 'activation refuses a running game before mutation'
    & $module { Remove-Item -LiteralPath Function:Get-GameProcessInDirectory }
    Assert-Equal (Restore-LocalCoopSession $session).State 'off' 'normal helper restored after running-game fixture'
    Write-Host "PASS $script:checks local co-op session checks (PowerShell $($PSVersionTable.PSVersion), inert fixtures only)."
} finally {
    $resolvedFixture = [IO.Path]::GetFullPath($fixture)
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repo 'build'))
    if (!(Test-PathInside -Child $resolvedFixture -Parent $buildRoot) -or [IO.File]::ReadAllText((Join-Path $resolvedFixture '.fixture-owner')) -cne $token) { throw 'Refusing fixture cleanup outside the owned build directory.' }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
}
