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
    # Actual Win32 replacement locks, inert files only. The wait hook makes race boundaries
    # deterministic; native File.Replace still produces the first real failure.
    $atomicRoot = Join-Path $fixture 'atomic'
    $null = New-Item -ItemType Directory -Path $atomicRoot
    function New-AtomicControl([string]$Name, [string]$Action, [IO.FileShare]$Sharing = [IO.FileShare]::Read) {
        $dir = Join-Path $atomicRoot $Name
        $null = New-Item -ItemType Directory -Path $dir
        $path = Join-Path $dir 'session.json'
        [IO.File]::WriteAllBytes($path, [byte[]]@(1, 2, 3))
        $access = if ($Sharing -eq [IO.FileShare]::Read) { [IO.FileAccess]::Read } else { [IO.FileAccess]::ReadWrite }
        $held = New-Object IO.FileStream($path, [IO.FileMode]::Open, $access, $Sharing)
        return [pscustomobject]@{ Path = $path; Directory = $dir; Action = $Action; Held = $held;
            Attempts = 0; Waits = 0; Errors = (New-Object Collections.Generic.List[int]); Backup = ''; Temporary = ''; Retained = '' }
    }
    function Set-AtomicControl($Control) {
        & $module {
            param($Control)
            $script:AtomicTest = $Control
            $script:AtomicOriginalReplace = (Get-Command Invoke-LocalCoopAtomicReplace -CommandType Function).ScriptBlock
            function script:Invoke-LocalCoopAtomicReplace {
                param([string]$Temporary, [string]$Path, [string]$Backup)
                $script:AtomicTest.Attempts++
                $script:AtomicTest.Backup = $Backup; $script:AtomicTest.Temporary = $Temporary
                if ($script:AtomicTest.Action -ceq 'removeOnce' -and $script:AtomicTest.Attempts -eq 1) {
                    throw (New-Object IO.IOException -ArgumentList 'Injected documented unchanged-name Win32 result1175', (-2147024896 + 1175))
                }
                if ($script:AtomicTest.Action -like 'partial*') {
                    $script:AtomicTest.Held.Dispose()
                    [IO.File]::Move($Path, $Backup)
                    if ($script:AtomicTest.Action -ceq 'partialForeign') { [IO.File]::WriteAllBytes($Path, [byte[]]@(99)) }
                    if ($script:AtomicTest.Action -ceq 'partialDirectory') { $null = New-Item -ItemType Directory -Path $Path }
                    $code = if ($script:AtomicTest.Action -ceq 'partialAbsent') { 1176 } else { 1177 }
                    throw (New-Object IO.IOException -ArgumentList ('Injected documented partial Win32 result ' + $code), (-2147024896 + $code))
                }
                try {
                    & $script:AtomicOriginalReplace -Temporary $Temporary -Path $Path -Backup $Backup
                    if ($script:AtomicTest.Action -ceq 'backupChanged') { [IO.File]::WriteAllBytes($Backup, [byte[]]@(66)) }
                }
                catch {
                    $cause = $_.Exception
                    while ($cause.InnerException) { $cause = $cause.InnerException }
                    $script:AtomicTest.Errors.Add(($cause.HResult -band 65535))
                    throw
                }
            }
            function script:Start-Sleep {
                param([int]$Milliseconds)
                $script:AtomicTest.Waits++
                if ($script:AtomicTest.Waits -eq 1) {
                    switch -CaseSensitive ($script:AtomicTest.Action) {
                        'release' { $script:AtomicTest.Held.Dispose() }
                        'change' {
                            $script:AtomicTest.Held.SetLength(0)
                            $script:AtomicTest.Held.Write([byte[]]@(99), 0, 1); $script:AtomicTest.Held.Flush($true)
                            $script:AtomicTest.Held.Dispose()
                        }
                        'replaceSame' {
                            $script:AtomicTest.Held.Dispose()
                            $script:AtomicTest.Retained = $script:AtomicTest.Path + '.old'
                            [IO.File]::Move($script:AtomicTest.Path, $script:AtomicTest.Retained)
                            [IO.File]::WriteAllBytes($script:AtomicTest.Path, [byte[]]@(1, 2, 3))
                        }
                        'disappear' {
                            $script:AtomicTest.Held.Dispose()
                            $script:AtomicTest.Retained = $script:AtomicTest.Path + '.old'
                            [IO.File]::Move($script:AtomicTest.Path, $script:AtomicTest.Retained)
                        }
                        'changeTemp' {
                            [IO.File]::WriteAllBytes($script:AtomicTest.Temporary, [byte[]]@(88))
                            $script:AtomicTest.Held.Dispose()
                        }
                        'replaceTempSame' {
                            $script:AtomicTest.Retained = $script:AtomicTest.Temporary + '.old'
                            [IO.File]::Move($script:AtomicTest.Temporary, $script:AtomicTest.Retained)
                            [IO.File]::WriteAllBytes($script:AtomicTest.Temporary, [byte[]]@(4, 5, 6))
                            $script:AtomicTest.Held.Dispose()
                        }
                    }
                }
                Microsoft.PowerShell.Utility\Start-Sleep -Milliseconds $Milliseconds
            }
        } $Control
    }
    function Clear-AtomicControl($Control) {
        $Control.Held.Dispose()
        & $module {
            Remove-Item -LiteralPath Function:Start-Sleep
            Set-Item -Path Function:script:Invoke-LocalCoopAtomicReplace -Value $script:AtomicOriginalReplace
            Remove-Variable AtomicTest, AtomicOriginalReplace -Scope Script
        }
    }
    function Write-AtomicControl($Control) {
        & $module { param($Path, $Root); Write-LocalCoopAtomicBytes -Path $Path -Bytes ([byte[]]@(4, 5, 6)) -StopAt $Root } $Control.Path $atomicRoot
    }
    foreach ($code in @(32, 33, 1175, 5, 87, 1176, 1177, 1392)) {
        $exception = New-Object IO.IOException -ArgumentList ('fixture ' + $code), (-2147024896 + $code)
        $retry = & $module { param($Exception); Test-LocalCoopAtomicRetryError -Exception $Exception } $exception
        Assert-Equal $retry ($code -in @(32, 33, 1175)) "only documented safe Win32 retry code $code"
    }
    $foreignFacility = New-Object IO.IOException -ArgumentList 'foreign facility same low code', (-2146238432)
    Assert-Equal ($foreignFacility.HResult -band 65535) 32 'foreign-facility fixture really has the same low sharing error code'
    Assert-True (!(& $module { param($Exception); Test-LocalCoopAtomicRetryError -Exception $Exception } $foreignFacility)) 'matching low error bits from another facility have no retry authority'
    $control = New-AtomicControl 'release' 'release'
    Set-AtomicControl $control
    try {
        Write-AtomicControl $control
        Assert-Equal $control.Attempts 2 'real held file succeeds on one bounded retry after release'
        Assert-Equal $control.Waits 1 'actual sharing failure schedules one wait'
        Assert-Equal $control.Errors.Count 1 'native File.Replace actually failed once'
        Assert-True ($control.Errors[0] -in @(32, 33, 1175)) 'actual Windows failure is in the documented safe retry set'
        $observedAtomicLockError = $control.Errors[0]
        Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($control.Path))) 'BAUG' 'verified replacement contents after lock release'
        Assert-Equal @(Get-ChildItem -LiteralPath $control.Directory -File).Count 1 'successful replacement cleans only owned temporary and original backup'
    } finally { Clear-AtomicControl $control }
    $control = New-AtomicControl 'removeOnce' 'removeOnce'
    $control.Held.Dispose()
    Set-AtomicControl $control
    try {
        Write-AtomicControl $control
        Assert-Equal $control.Attempts 2 'documented unchanged-name1175 fixture retries once then uses real native replacement'
        Assert-Equal $control.Waits 1 '1175 fixture gets one bounded wait'
        Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($control.Path))) 'BAUG' '1175 retry fixture commits verified intended bytes'
        Assert-Equal @(Get-ChildItem -LiteralPath $control.Directory -File).Count 1 '1175 retry fixture cleans only owned files'
    } finally { Clear-AtomicControl $control }
    $control = New-AtomicControl 'noBeforeWitness' 'exhaust' ([IO.FileShare]::None)
    Set-AtomicControl $control
    try {
        Assert-Throws { Write-AtomicControl $control } 'Atomic write failed' 'unreadable target has no before-hash retry authority'
        Assert-Equal $control.Attempts 0 'unreadable target never reaches native replacement'
        Assert-Equal $control.Waits 0 'unreadable target never waits and later adopts new bytes'
        $control.Held.Dispose()
        Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($control.Path))) 'AQID' 'unreadable original remains intact'
        Assert-Equal @(Get-ChildItem -LiteralPath $control.Directory -File).Count 1 'missing before-witness creates no orphan temporary'
    } finally { Clear-AtomicControl $control }
    $control = New-AtomicControl 'backupChanged' 'backupChanged'
    $control.Held.Dispose()
    Set-AtomicControl $control
    try {
        Write-AtomicControl $control
        Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($control.Path))) 'BAUG' 'real committed target remains verified when saved backup changes'
        Assert-Equal ([IO.File]::ReadAllBytes($control.Backup)[0]) 66 'successful cleanup never deletes changed backup bytes'
        Assert-Equal $control.Attempts 1 'backup mutation needs no replacement retry'
    } finally { Clear-AtomicControl $control }
    $control = New-AtomicControl 'exhaust' 'exhaust'
    Set-AtomicControl $control
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        Assert-Throws { Write-AtomicControl $control } 'Atomic write failed' 'persistent native lock fails closed'
        Assert-True ($control.Attempts -ge 1 -and $control.Attempts -le 10) 'retry attempt count is bounded by attempt or deadline exhaustion'
        Assert-Equal $control.Waits ($control.Attempts - 1) 'persistent lock never waits after its final refused attempt'
        $observedAtomicExhaustAttempts = $control.Attempts
        Assert-True ($clock.ElapsedMilliseconds -lt 5000) 'native exhaustion completes without an unbounded wait'
        Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($control.Path))) 'AQID' 'exhaustion preserves original bytes'
        Assert-Equal @(Get-ChildItem -LiteralPath $control.Directory -File).Count 1 'exhaustion cleans owned temporary without removing target'
    } finally { Clear-AtomicControl $control }
    foreach ($action in @('change', 'replaceSame', 'disappear', 'changeTemp', 'replaceTempSame')) {
        $control = New-AtomicControl $action $action ([IO.FileShare]::ReadWrite)
        Set-AtomicControl $control
        try {
            Assert-Throws { Write-AtomicControl $control } 'Atomic write failed' "$action during retry is refused"
            Assert-Equal $control.Attempts 1 "$action never gets a second native replacement call"
            if ($action -ceq 'disappear') {
                Assert-True (!(Test-Path -LiteralPath $control.Path)) 'disappeared target never changes to absent-create fallback'
                Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($control.Retained))) 'AQID' 'moved old target remains intact'
            } else {
                $expected = if ($action -ceq 'change') { 'Yw==' } else { 'AQID' }
                Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($control.Path))) $expected "$action preserves current target"
            }
            if ($action -in @('changeTemp', 'replaceTempSame')) {
                $expected = if ($action -ceq 'changeTemp') { 'WA==' } else { 'BAUG' }
                Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($control.Temporary))) $expected "$action cleanup preserves foreign temporary identity or bytes"
            } else { Assert-True (!(Test-Path -LiteralPath $control.Temporary)) "$action cleans the unchanged owned temporary" }
        } finally { Clear-AtomicControl $control }
    }
    foreach ($action in @('partialAbsent', 'partialForeign', 'partialDirectory')) {
        $control = New-AtomicControl $action $action
        Set-AtomicControl $control
        try {
            $expected = if ($action -ceq 'partialAbsent') { 'verified original restored' } else { 'backup retained for recovery: ' }
            Assert-Throws { Write-AtomicControl $control } $expected "$action reports concrete rollback or retained backup"
            Assert-Equal $control.Attempts 1 "$action never retries a partial replacement result"
            Assert-Equal $control.Waits 0 "$action never waits after an unsafe native result"
            Assert-True (!(Test-Path -LiteralPath $control.Temporary)) "$action cleans unchanged owned temporary"
            if ($action -ceq 'partialAbsent') {
                Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($control.Path))) 'AQID' 'absent target rollback restores original bytes'
                Assert-True (!(Test-Path -LiteralPath $control.Backup)) 'successful rollback consumes only its original backup'
            } else {
                Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($control.Backup))) 'AQID' "$action retains original bytes for recovery"
                if ($action -ceq 'partialForeign') { Assert-Equal ([IO.File]::ReadAllBytes($control.Path)[0]) 99 'rollback never overwrites a foreign target' }
                else { Assert-True (Test-Path -LiteralPath $control.Path -PathType Container) 'rollback never removes a foreign directory' }
            }
        } finally { Clear-AtomicControl $control }
    }
    $path = Join-Path $atomicRoot 'strict-existing'
    [IO.File]::WriteAllBytes($path, [byte[]]@(7))
    Assert-Throws { & $module { param($Path, $Root); Write-LocalCoopAtomicBytes -Path $Path -Bytes ([byte[]]@(8)) -StopAt $Root -RequireAbsent } $path $atomicRoot } 'Atomic write failed' 'RequireAbsent never replaces an existing target'
    Assert-Equal ([IO.File]::ReadAllBytes($path)[0]) 7 'RequireAbsent keeps existing bytes'
    Assert-Equal @(Get-ChildItem -LiteralPath $atomicRoot -File -Filter '*.tmp').Count 0 'strict-create failure cleans its own temporary'
    $path = Join-Path $atomicRoot 'strict-new'
    & $module { param($Path, $Root); Write-LocalCoopAtomicBytes -Path $Path -Bytes ([byte[]]@(8)) -StopAt $Root -RequireAbsent } $path $atomicRoot
    Assert-Equal ([IO.File]::ReadAllBytes($path)[0]) 8 'RequireAbsent still creates a genuinely absent file'
    Write-Host "Atomic lock evidence: actual Win32 error=$observedAtomicLockError, release attempts=2, persistent attempts=$observedAtomicExhaustAttempts;1175/1176/1177 cases are explicit fault fixtures."

    Write-Host "PASS $script:checks local co-op session checks (PowerShell $($PSVersionTable.PSVersion), inert fixtures only)."
} finally {
    $resolvedFixture = [IO.Path]::GetFullPath($fixture)
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repo 'build'))
    if (!(Test-PathInside -Child $resolvedFixture -Parent $buildRoot) -or [IO.File]::ReadAllText((Join-Path $resolvedFixture '.fixture-owner')) -cne $token) { throw 'Refusing fixture cleanup outside the owned build directory.' }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
}
