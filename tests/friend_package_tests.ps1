#Requires -Version 7
# Tests for the Diamond Dogs Together package (tools/package.ps1 -Friend: the
# drop-in zip whose root is the game folder) and for the developer tools that
# remain after the PowerShell player tooling was retired in 0.14.0:
# tools/CoopPlay.psm1, tools/dev/FriendCommon.psm1 (the session, cleanup and
# save-handoff helpers that the local test and the hidden worker use), the owned
# installer's drop-in support and the local two-window test (tools/dev).
# DiamondDogsTogether.exe's own behaviour is tested in C++ (tests/ddt_core_tests.cpp).
# Everything runs inside an owned temporary folder under build/: fixture game
# folders hold inert stand-in files, a fixture Steam folder holds fixture saves.
# No real game folder, Steam folder or game process is touched, and nothing
# contacts the network. The developer scripts run as child processes under
# Windows PowerShell 5.1 (what Play-Offline-Coop.bat starts).
[CmdletBinding()]
param([string]$IniCheck, [string]$FakeProcess)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectRoot 'tools\worker\WorkerSource.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $projectRoot 'tools/lab/LabCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $projectRoot 'tools/dev/FriendCommon.psm1') -Force -DisableNameChecking
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
$nonce = [Guid]::NewGuid().ToString('N')
$fixture = Join-Path $buildRoot ('friend-package-tests-' + $nonce)
$ownerMarker = Join-Path $fixture '.fixture-owner'
$script:checks = 0
$pwshPath = (Get-Process -Id $PID).Path
$winPs = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
if (!(Test-Path -LiteralPath $winPs -PathType Leaf)) {
    Write-Warning 'Windows PowerShell 5.1 not found: the friend scripts are tested under PowerShell 7 only.'
    $winPs = $pwshPath
}
$utf8 = [Text.UTF8Encoding]::new($false)
# The package version tools/package.ps1 stamps into package.json, the folder name and the guide.
$packageVersion = [regex]::Match([IO.File]::ReadAllText((Join-Path $projectRoot 'tools/package.ps1')), "(?m)^\`$version='([0-9]+\.[0-9]+\.[0-9]+)'").Groups[1].Value
if (!$packageVersion) { throw 'tools/package.ps1 names no $version' }

function Assert-True([bool]$Condition, [string]$Message) {
    $script:checks++
    if (!$Condition) { throw "Assertion failed: $Message" }
}
function Assert-Equal($Actual, $Expected, [string]$Message) {
    $script:checks++
    if ($Actual -cne $Expected) { throw "Assertion failed: $Message (expected '$Expected', got '$Actual')" }
}
function Assert-Throws([scriptblock]$Operation, [string]$Expected, [string]$Message) {
    $script:checks++
    $failure = $null
    try { & $Operation | Out-Null } catch { $failure = $_ }
    if (!$failure) { throw "Assertion failed: expected failure: $Message" }
    if ($Expected -and !$failure.Exception.Message.Contains($Expected)) { throw "Assertion failed: $Message (unexpected error: $($failure.Exception.Message))" }
}
function Assert-Refused($Result, [string]$Needle, [string]$Message) {
    $script:checks++
    if ($Result.Exit -eq 0 -or !(Has $Result $Needle)) { throw "Assertion failed: $Message (exit $($Result.Exit); output: $($Result.Text))" }
}
function Assert-InFixture([string]$Path) {
    if (!(Test-PathInside -Child $Path -Parent $fixture)) { throw "Test path escaped its fixture: $Path" }
}
function Invoke-IniCheck([string]$Mode, [string]$Value) {
    # The helper prints the ini bytes as they are (save_dir is UTF-8): decode
    # its output as UTF-8, not with whatever console code page CTest inherits.
    $psi = [Diagnostics.ProcessStartInfo]::new($IniCheck)
    $psi.ArgumentList.Add($Mode); $psi.ArgumentList.Add($Value)
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true
    $psi.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $proc = [Diagnostics.Process]::Start($psi)
    $output = $proc.StandardOutput.ReadToEnd() -split "`r?`n"
    $proc.WaitForExit()
    if ($proc.ExitCode -ne 0) { throw "lab_ini_check $Mode failed ($($proc.ExitCode))" }
    $map = @{}
    foreach ($line in $output) { $i = $line.IndexOf('='); if ($i -gt 0) { $map[$line.Substring(0, $i)] = $line.Substring($i + 1) } }
    return $map
}
# tools/package-install.ps1 refuses at once while another installer holds Local\MgsCoopPackageInstall.
# CTest runs this suite next to lab_scripts_tests (and the suites of other checkouts), so the
# parts that install take turns through one machine-wide test mutex; an exited holder abandons it.
function Enter-TestInstallTurn {
    $mutex = [Threading.Mutex]::new($false, 'Local\mgs-coop-test-package-install')
    $owned = $false
    try { $owned = $mutex.WaitOne([TimeSpan]::FromMinutes(10)) } catch [Threading.AbandonedMutexException] { $owned = $true }
    if (!$owned) { $mutex.Dispose(); throw 'Another test suite kept the package-install turn for 10 minutes.' }
    return $mutex
}
function Exit-TestInstallTurn($Mutex) { if ($Mutex) { $Mutex.ReleaseMutex(); $Mutex.Dispose() } }
function Invoke-Friend([string]$Script, [string[]]$Arguments, [string]$HostExe = $winPs) {
    # Child process, like the .bat files; output flattened for message checks.
    # The child scripts install (package-install.ps1, Start-LocalCoop.ps1): one install turn each.
    foreach ($a in $Arguments) { if ($a -and [IO.Path]::IsPathRooted($a) -and $a -notmatch '^[0-9]') { Assert-InFixture $a } }
    $turn = Enter-TestInstallTurn
    try {
        $raw = & $HostExe -NoProfile -ExecutionPolicy Bypass -NonInteractive -File $Script @Arguments 2>&1 | Out-String
        $code = $LASTEXITCODE
    } finally { Exit-TestInstallTurn $turn }
    # Windows PowerShell 5.1 wraps error records at the console width, even
    # inside words: messages are compared with all whitespace removed.
    return [pscustomobject]@{ Exit = $code; Raw = $raw; Text = ($raw -replace '\s', '') }
}
function Has($Result, [string]$Needle) { return $Result.Text.Contains(($Needle -replace '\s', '')) }
function Get-TreeHashes([string]$Root) {
    $rows = [ordered]@{}
    foreach ($f in @(Get-ChildItem -LiteralPath $Root -File -Recurse -Force | Sort-Object FullName)) { $rows[$f.FullName] = Get-Sha256 $f.FullName }
    return ($rows.Keys | ForEach-Object { $_ + '=' + $rows[$_] }) -join "`n"
}
function Get-RelativeTreeHashes([string]$Root) {
    # The files below $Root as "<relative path>=<SHA-256>" lines (a folder moved elsewhere compares equal).
    $full = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $rows = foreach ($f in @(Get-ChildItem -LiteralPath $Root -File -Recurse -Force | Sort-Object FullName)) { $f.FullName.Substring($full.Length + 1) + '=' + (Get-Sha256 $f.FullName) }
    return (@($rows) -join "`n")
}
function New-HandoffStage([string]$HandoffDir, [string]$Xfer, [hashtable]$Files) {
    # A host save staged as the loader leaves it (P2-9): <xfer>\<entries> and handoff.json.
    $stage = Join-Path $HandoffDir $Xfer
    Assert-InFixture $stage
    $rows = @()
    foreach ($entry in @($Files.Keys | Sort-Object { [Array]::IndexOf(@(Get-CoopHandoffSlots), $_) })) {
        $path = Join-Path $stage ([string]$entry).Replace('/', '\')
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
        [IO.File]::WriteAllText($path, [string]$Files[$entry])
        $rows += [pscustomobject]@{ entry = [string]$entry; bytes = (Get-Item -LiteralPath $path).Length; sha256 = (Get-Sha256 $path) }
    }
    $set = Get-CoopHandoffSetSha256 -Entries $rows
    $manifest = [ordered]@{ format = 'mgs-coop-save-handoff'; v = 1; xfer = $Xfer.Replace('.partial', ''); created_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'); host_build = 'fixture'
        host_mission = 30010; set_sha256 = $set; files = $rows }
    [IO.File]::WriteAllText((Join-Path $stage 'handoff.json'), ($manifest | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{ Stage = $stage; Set = $set; Rows = $rows; Xfer = $Xfer }
}
function New-HandoffRequest([string]$HandoffDir, $Stage, [int]$GamePid, [string]$StartUtc, $CreatedUtc = $null, [int]$Rejoin = 0) {
    # request.json, written last by the loader (P2-9).
    if ($null -eq $CreatedUtc) { $CreatedUtc = [DateTime]::UtcNow }
    $req = [ordered]@{ format = 'mgs-coop-save-handoff-request'; v = 1; pid = $GamePid; process_start_utc = $StartUtc; xfer = $Stage.Xfer; set_sha256 = $Stage.Set
        mode = 'scene'; rejoin = $Rejoin; created_utc = ([DateTime]$CreatedUtc).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ') }
    $path = Join-Path $HandoffDir 'request.json'
    [IO.File]::WriteAllText($path, ($req | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    return $path
}
function Get-HandoffField($Paths, [string]$Name) {
    # play-session.json "handoff".<Name> ('' while absent; StrictMode-safe).
    $h = Get-FriendProperty (Get-SessionJson $Paths) 'handoff' $null
    return [string](Get-FriendProperty $h $Name '')
}
function New-GameFixture([string]$Name) {
    $game = Join-Path $fixture "games/$Name"
    Assert-InFixture $game
    $null = New-Item -ItemType Directory -Path $game -Force
    [IO.File]::WriteAllText((Join-Path $game 'mgsvtpp.exe'), 'INERT executable fixture; never executed')
    [IO.File]::WriteAllText((Join-Path $game 'steam_api64.dll'), 'INERT Steam DLL fixture')
    return [IO.Path]::GetFullPath($game)
}
function Wait-Until([scriptblock]$Condition, [int]$TimeoutSec = 30) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
    while ([DateTime]::UtcNow -lt $deadline) { if (& $Condition) { return $true }; Start-Sleep -Milliseconds 250 }
    return [bool](& $Condition)
}
function Get-PlayFilesPresent($Paths) {
    return @(@($Paths.LabIni, $Paths.LinkSecret, $Paths.CoopIni, $Paths.Marker) | Where-Object { Test-Path -LiteralPath $_ }).Count
}
function Get-SessionJson($Paths) {
    # The watchdog briefly owns the file while writing its next complete JSON.
    # Retry sharing failures only; parsing and all evidence checks stay strict.
    for ($sessionReadAttempt = 0; $sessionReadAttempt -lt 250; $sessionReadAttempt++) {
        try {
            $sessionText = [IO.File]::ReadAllText($Paths.PlaySession, [Text.Encoding]::UTF8)
            return $sessionText | ConvertFrom-Json
        } catch [IO.IOException] {
            if ($sessionReadAttempt -eq 249) { throw }
            Start-Sleep -Milliseconds 20
        }
    }
}
function Remove-OwnedFixture {
    if (!(Test-Path -LiteralPath $fixture)) { return }
    if ((Split-Path -Parent $fixture) -ne $buildRoot -or !(Test-Path -LiteralPath $ownerMarker -PathType Leaf) -or
        [IO.File]::ReadAllText($ownerMarker) -ne $nonce) { throw 'Refusing cleanup: fixture ownership check failed' }
    foreach ($item in @(Get-ChildItem -LiteralPath $fixture -Force -Recurse)) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing cleanup of a fixture containing links' }
    }
    Remove-Item -LiteralPath $fixture -Recurse -Force
}

if (!(Test-Path -LiteralPath $buildRoot)) { $null = New-Item -ItemType Directory -Path $buildRoot }
$null = New-Item -ItemType Directory -Path $fixture
[IO.File]::WriteAllText($ownerMarker, $nonce)
# Steam's running flag for MGSV (Get-CoopSteamAppRunning, online 2026-10-22 P5)
# comes from this fixture file in this process and every script it starts, never
# from the registry.
$steamRunningFile = Join-Path $fixture 'steam-running.txt'
[IO.File]::WriteAllText($steamRunningFile, "0`n")
$env:MGS_COOP_TEST_STEAM_RUNNING_FILE = $steamRunningFile
try {
    # ------------------------------------------------------------ native link keys (merge gate)
    $linkKeys = $false
    $steamKeys = $false
    # Whether the loader parses save_handoff/travel_lock yet (the native save handoff side).
    $handoffKeys = $false
    if ($IniCheck) {
        $probeIni = Join-Path $fixture 'probe-lab.ini'
        [IO.File]::WriteAllText($probeIni, "enabled=1`nrole=host`n")
        $probeMap = Invoke-IniCheck lab $probeIni
        $linkKeys = $probeMap['link_keys'] -eq '1'
        $steamKeys = $probeMap.ContainsKey('net_transport')
    }
    if (!$linkKeys) { Write-Output 'link_keys missing: native link cross-checks skipped' }
    if (!$steamKeys) { Write-Output 'net_transport missing: native Steam online cross-checks skipped' }

    # ------------------------------------------------------------ small pure helpers
    foreach ($id in '76561198012345678', '76561197960265729', '76561197960265728', '76561202255233008', '7656119801234567', '76561200000000005', '76561202255233024') {
        Assert-Equal (Test-FriendIndividualSteamId64 $id) (Test-IndividualSteamId64 $id) "individual check matches LabCommon for $id"
    }
    Assert-Equal (Get-FriendDefaultCharacter -Role Host) 'default' 'host default character'
    Assert-Equal (Get-FriendDefaultCharacter -Role Join) 'quiet' 'joiner default character'
    foreach ($ch in 'default', 'quiet', 'dd_male', 'dd_female', 'snake', 'avatar', 'ocelot', 'story') { Assert-True (Test-FriendCharacter $ch) "character $ch" }
    Assert-True (-not (Test-FriendCharacter 'Story')) 'story is lower case like every character'
    Assert-True (-not (Test-FriendCharacter 'dog')) 'unknown character'
    Assert-True (-not (Test-FriendCharacter 'Quiet')) 'character names are lower case'
    Assert-Equal ((Get-FriendCharacters) -join ',') ((Get-CoopCharacters) -join ',') 'FriendCommon and CoopPlay know the same characters'
    Assert-True ($null -eq (Get-FriendSaveDirProblem ('C:\' + ('a' * 210)))) 'save_dir of 213 bytes accepted'
    Assert-True ($null -ne (Get-FriendSaveDirProblem ('C:\' + ('a' * 220)))) 'save_dir over 213 bytes refused'
    Assert-True ($null -ne (Get-FriendSaveDirProblem ("C:\x`tz"))) 'control characters refused'
    Assert-True ($null -ne (Get-FriendSaveDirProblem 'relative\saves')) 'relative save_dir refused'
    # Installer paths: the longest fixed one is a save handoff stage's temp file
    # (<game> + 119 characters); a save file name can make the backup longer.
    Assert-True ($null -eq (Get-FriendPathLengthProblem -GameRoot ('C:\' + ('a' * 137)))) 'game path of 140 characters: installer and save handoff paths fit 259'
    Assert-True ((Get-FriendPathLengthProblem -GameRoot ('C:\' + ('a' * 138))).Contains('too long')) 'game path of 141 characters refused (the save handoff temp file)'
    Assert-True ($null -eq (Get-FriendPathLengthProblem -GameRoot ('C:\' + ('a' * 137)) -SaveFiles @('287700/local/TPP_GRAPHICS_CONFIG'))) 'the usual save file names fit at 140 characters'
    Assert-True ((Get-FriendPathLengthProblem -GameRoot ('C:\' + ('a' * 137)) -SaveFiles @('287700/local/' + ('X' * 100))).Contains('too long')) 'a long save file name makes the backup path too long'
    Assert-Equal (ConvertTo-FriendRoleName "host-v3`n") 'host' 'marker host'
    Assert-Equal (ConvertTo-FriendRoleName "client-v1`n") 'join' 'marker join'
    Assert-Equal (ConvertTo-FriendRoleName "client-check-v1`n") 'diagnostic' 'marker diagnostic'
    Assert-Equal (ConvertTo-FriendRoleName "offline-v1`n") 'play' 'marker play'
    Assert-Equal (ConvertTo-FriendRoleName '') 'none' 'no marker'
    $friendHeader = '# Generated by the MGS Co-op friend-test installer (Install-Coop.ps1).'
    Assert-Equal (Get-FriendOnlineArchiveReason -Name 'lab.ini' -Text "$friendHeader`nenabled=1`n") 'online friend-test configuration' 'online friend lab.ini is archived'
    Assert-Equal (Get-FriendOnlineArchiveReason -Name 'script-observe.enabled' -Text "client-v1`n") 'online role marker' 'client-v1 marker is archived'
    Assert-Equal (Get-FriendOnlineArchiveReason -Name 'client-host.txt' -Text "76561198012345678`n") 'online join target' 'client-host.txt is archived'
    Assert-True ($null -eq (Get-FriendOnlineArchiveReason -Name 'lab.ini' -Text "# Generated by tools/lab/Prepare-LabInstance.ps1`n")) 'a lab lab.ini is not an online file'
    Assert-True ($null -eq (Get-FriendOnlineArchiveReason -Name 'script-observe.enabled' -Text "offline-v1`n")) 'offline-v1 is not an online marker'
    $mutexA = Get-CoopPlayMutexName -GameRoot 'C:\Games\MGS_TPP'
    Assert-True ($mutexA -cmatch '^Local\\mgs-coop-play-[0-9A-F]{16}$' -and $mutexA -ceq (Get-CoopPlayMutexName -GameRoot 'c:\games\mgs_tpp\')) 'play mutex: Local\mgs-coop-play-<SHA-256 prefix>, case and slash insensitive'

    # ------------------------------------------------------------ join code (spec 3.1, 3.7)
    $vectors = @(
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:47830', 'ok', '203.0.113.7', 47830),
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-2M@my-pc.tail1234.ts.net:47830', 'ok', 'my-pc.tail1234.ts.net', 47830),
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-MN@192.168.1.20:50001', 'ok', '192.168.1.20', 50001),
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-M0@100.101.102.103:47830', 'ok', '100.101.102.103', 47830),
        @('MGS1-C00PC-00PC0-0PC00-PC00P-V7@127.0.0.1:47820', 'ok', '127.0.0.1', 47820),
        @('mgs1 7k3qz-9m2xw-d4ht8-rvn6b-dd@203.0.113.7:47830', 'ok', '203.0.113.7', 47830),
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7', 'ok', '203.0.113.7', 47830),
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6C-DD@203.0.113.7:47830', 'bad_check', '', 0),
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.8:47830', 'bad_check', '', 0),
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-QR@192.168.1.20:47831', 'bad_check', '', 0),
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.000.113.7:47830', 'bad_host', '', 0),
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:047830', 'bad_port', '', 0),
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@[::1]:47830', 'ipv6_unsupported', '', 0),
        @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6U-DD@203.0.113.7:47830', 'bad_char', '', 0))
    foreach ($v in $vectors) {
        $r = ConvertFrom-CoopJoinCode $v[0]
        Assert-Equal "$($r.Status)|$($r.HostName)|$($r.Port)" "$($v[1])|$($v[2])|$($v[3])" "join code vector $($v[0])"
        if ($v[1] -eq 'ok') { Assert-Equal $r.Secret $(if ($v[0].Contains('C00PC')) { 'C00PC00PC00PC00PC00P' } else { '7K3QZ9M2XWD4HT8RVN6B' }) "secret of $($v[0])" }
    }
    $s7 = '7K3QZ9M2XWD4HT8RVN6B'
    Assert-Equal (Get-CoopCodeCheck -Secret '7K3QZ9M2XWD4HT8RVN6C' -HostName '203.0.113.7' -Port 47830) 'PY' 'check of the altered secret (PY)'
    Assert-Equal (Get-CoopCodeCheck -Secret $s7 -HostName '203.0.113.8' -Port 47830) '2Q' 'check of 203.0.113.8 (2Q)'
    Assert-Equal (Get-CoopCodeCheck -Secret $s7 -HostName '192.168.1.20' -Port 47831) 'BB' 'check of port 47831 (BB)'
    Assert-Equal (Get-CoopCodeCheck -Secret $s7 -HostName '192.168.1.20' -Port 47830) 'QR' 'check of port 47830 (QR)'
    Assert-Equal (New-CoopJoinCode -Secret $s7 -HostName '203.0.113.7') 'MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:47830' 'New-CoopJoinCode: vector 1 with the default port'
    Assert-Equal (New-CoopJoinCode -Secret (Get-CoopLabSecret) -HostName '127.0.0.1' -Port 47820) 'MGS1-C00PC-00PC0-0PC00-PC00P-V7@127.0.0.1:47820' 'the lab secret vector'
    # Tolerant forms: case, blanks, dashes, O/I/L for 0/1/1, a trailing newline.
    foreach ($form in @('  MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD @ 203.0.113.7 : 47830 ', "mgs17k3qz9m2xwd4ht8rvn6bdd@203.0.113.7:47830`r`n", 'MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:47830')) {
        Assert-Equal (ConvertFrom-CoopJoinCode $form).Status 'ok' "tolerant form '$($form.Trim())'"
    }
    $plainCode = New-CoopJoinCode -Secret '01234567890123456789' -HostName 'host-a.example' -Port 50000
    $atSign = $plainCode.IndexOf('@')
    $mapped = ConvertFrom-CoopJoinCode ('MGS1-' + $plainCode.Substring(5, $atSign - 5).Replace('0', 'O').Replace('1', 'l') + $plainCode.Substring($atSign))
    Assert-Equal "$($mapped.Status)|$($mapped.Secret)" 'ok|01234567890123456789' 'O -> 0 and l -> 1 are read as digits'
    foreach ($bad in @(@('', 'empty'), @('   ', 'empty'), @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD', 'bad_host'), @('MGS2-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7', 'bad_prefix'),
            @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6-DD@203.0.113.7', 'bad_length'), @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DDD@203.0.113.7', 'bad_length'),
            @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-D!@203.0.113.7', 'bad_char'), @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7.:47830', 'bad_host'),
            @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@my-pc.:47830', 'bad_host'), @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@-pc.example:47830', 'bad_host'),
            @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@1.2.3:47830', 'bad_host'), @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@256.0.0.1:47830', 'bad_host'),
            @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@my_pc:47830', 'bad_host'), @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@:47830', 'bad_host'),
            @(('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@' + ('a' * 64) + '.example'), 'bad_host'),
            @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:1023', 'bad_port'), @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:65536', 'bad_port'),
            @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:', 'bad_port'), @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@203.0.113.7:4783x', 'bad_port'),
            @('MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-DD@fe80::1', 'ipv6_unsupported'))) {
        Assert-Equal (ConvertFrom-CoopJoinCode $bad[0]).Status $bad[1] "join code '$($bad[0])' -> $($bad[1])"
    }
    # The check covers the canonical forms only: an upper-case host is lowered before the check.
    Assert-Equal (ConvertFrom-CoopJoinCode 'MGS1-7K3QZ-9M2XW-D4HT8-RVN6B-2M@MY-PC.Tail1234.TS.NET:47830').Status 'ok' 'host names are compared lower-case'
    Assert-Throws { New-CoopJoinCode -Secret $s7 -HostName '203.000.113.7' } 'canonical' 'New-CoopJoinCode refuses a non-canonical IPv4'
    Assert-Throws { New-CoopJoinCode -Secret $s7 -HostName 'My-PC' } 'canonical' 'New-CoopJoinCode refuses an upper-case name'
    Assert-Throws { New-CoopJoinCode -Secret 'SHORT' -HostName 'a.b' } 'secret' 'New-CoopJoinCode refuses a bad secret'
    Assert-Throws { New-CoopJoinCode -Secret $s7 -HostName 'a.b' -Port 80 } 'port' 'New-CoopJoinCode refuses a low port'
    # Round trip with fresh secrets (RandomNumberGenerator) over every host kind.
    $hostsRt = @('192.168.1.20', '100.64.0.1', '26.1.2.3', '8.8.4.4', 'my-pc.tail1234.ts.net', 'a', 'x1.example')
    $secrets = @{}
    for ($i = 0; $i -lt 60; $i++) {
        $sec = New-CoopSecret
        Assert-True (Test-CoopSecret $sec) "fresh secret $i is canonical"
        $secrets[$sec] = $true
        $h = $hostsRt[$i % $hostsRt.Count]
        $port = 1024 + ($i * 1097) % 64512
        $rt = ConvertFrom-CoopJoinCode (New-CoopJoinCode -Secret $sec -HostName $h -Port $port)
        Assert-Equal "$($rt.Status)|$($rt.Secret)|$($rt.HostName)|$($rt.Port)" "ok|$sec|$h|$port" "join code round trip $i"
    }
    Assert-Equal $secrets.Count 60 'fresh secrets differ'
    Assert-Equal (Get-CoopCodeProblem -Status 'bad_check') 'The code has a typo; copy it again.' 'bad_check sentence'
    foreach ($st in 'empty', 'bad_prefix', 'bad_char', 'bad_length', 'bad_host', 'bad_port', 'ipv6_unsupported') { Assert-True ([bool](Get-CoopCodeProblem -Status $st)) "sentence for $st" }
    # link-secret.txt (spec 3.2).
    $ascii = [Text.Encoding]::ASCII
    Assert-Equal (Get-CoopSecretFileStatus $ascii.GetBytes("$s7`n")) 'ok' 'secret file: LF'
    Assert-Equal (Get-CoopSecretFileStatus $ascii.GetBytes("$s7`r`n")) 'ok' 'secret file: CRLF'
    Assert-Equal (Get-CoopSecretFileStatus $null) 'missing' 'secret file: missing'
    Assert-Equal (Get-CoopSecretFileStatus $ascii.GetBytes($s7)) 'bad_length' 'secret file: no newline'
    Assert-Equal (Get-CoopSecretFileStatus $ascii.GetBytes("$s7`n`n")) 'bad_length' 'secret file: extra line'
    Assert-Equal (Get-CoopSecretFileStatus $ascii.GetBytes("7K3QZ9M2XWD4HT8RVN6U`n")) 'bad_char' 'secret file: U is not in the alphabet'
    Assert-Equal (Get-CoopSecretFileStatus $ascii.GetBytes("7k3qz9m2xwd4ht8rvn6b`n")) 'bad_char' 'secret file: lower case is not canonical'
    if ($linkKeys) {
        foreach ($v in $vectors) {
            $n = Invoke-IniCheck code $v[0]
            Assert-Equal "$($n['code_status'])" $v[1] "native: code status of $($v[0])"
            if ($v[1] -eq 'ok') { Assert-Equal "$($n['host'])|$($n['port'])" "$($v[2])|$($v[3])" "native: host and port of $($v[0])" }
            Assert-True (!(($n.Values -join ' ').Contains('7K3QZ9M2XWD4HT8RVN6B'))) "native: the code check never prints the secret ($($v[0]))"
        }
        foreach ($case in @(@("$s7`n", 'ok'), @("$s7`r`n", 'ok'), @($s7, 'bad_length'), @("7K3QZ9M2XWD4HT8RVN6U`n", 'bad_char'))) {
            $file = Join-Path $fixture 'secret-case.txt'
            [IO.File]::WriteAllBytes($file, $ascii.GetBytes($case[0]))
            Assert-Equal (Invoke-IniCheck secret $file)['secret_status'] $case[1] "native: secret file status $($case[1])"
        }
        Assert-Equal (Invoke-IniCheck secret (Join-Path $fixture 'no-such-secret.txt'))['secret_status'] 'missing' 'native: missing secret file'
    }

    # ------------------------------------------------------------ play lab.ini / coop.ini (spec 9.2 step 4)
    $saveDir = 'C:\Games\MGS_TPP\mgs-coop\coop-saves'
    $header = Get-CoopPlayHeader
    Assert-Equal $header '# Generated by mgs-coop Play-Coop.ps1 (two-PC play); removed when the game exits.' 'play header'
    $defaultsText = (@((Get-PlayPartnerDefaults).GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join "`n")
    # Online play (docs/two-pc-play.md "Native partner"): every play copy sends its input frames; -Partner native
    # (the default) adds the one native key play accepts, -Partner clips does not.
    $defaultsText += "`npartner_input_send=1`npartner_native_profile=gun"
    $hostIni = New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Port 47830
    Assert-Equal $hostIni ("$header`nenabled=1`nmode=play`nrole=host`nsave_dir=$saveDir`nnet=1`nnet_bind=any`nnet_listen_port=47830`npartner_interp=auto`n$defaultsText`nworld=3`n") 'host lab.ini text: shared world by default'
    $guestIni = New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -Port 47831 -PeerAddress '203.0.113.7'
    Assert-Equal $guestIni ("$header`nenabled=1`nmode=play`nrole=guest`nsave_dir=$saveDir`nnet=1`nnet_bind=any`nnet_listen_port=0`nnet_peer_addr=203.0.113.7`nnet_peer_port=47831`npartner_interp=auto`n$defaultsText`nworld=3`n") 'guest lab.ini text: shared world by default'
    foreach ($ini in $hostIni, $guestIni) {
        $k = ConvertFrom-IniText $ini
        foreach ($forbidden in 'loopback', 'self_steamid', 'peer_steamid', 'self_name', 'mutex_suffix', 'xinput', 'join_trigger', 'auto_join_delay_s',
            'net_latency_ms', 'net_loss_permille', 'net_jitter_ms', 'net_dup_permille', 'net_reorder_permille', 'net_link', 'listen_port', 'peer_port',
            'link_session_skew') {
            Assert-True (!$k.Contains($forbidden)) "play lab.ini never writes $forbidden"
        }
        Assert-True (!$ini.Contains("`r") -and (Test-CoopPlayGeneratedText $ini)) 'play lab.ini: LF and the play header'
    }
    Assert-Equal ((Get-PlayPartnerDefaults).Keys -join ',') 'partner,partner_stage,partner_source,partner_apply,partner_anim,partner_aset,partner_reset,partner_look,partner_rate_hz,partner_interp_ms,partner_offset_cm,partner_teardown,partner_force_exists,partner_policy_relax,partner_anim_probe,partner_dm,crash_probe' 'play partner keys (no net ports, partner_interp or impairments)'
    Assert-Throws { New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -PeerAddress '127.0.0.1' } 'Invalid host address' 'play guest refuses 127/8'
    Assert-Throws { New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -PeerAddress 'my-pc.example' } 'Invalid host address' 'play guest refuses a name (the launcher resolves it)'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -PeerAddress '203.0.113.7' } 'no peer' 'play host has no peer address'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir 'relative\x' } 'absolute' 'play save_dir absolute'
    # World sync (docs/world-sync-protocol.md; experimental): world=N after the partner defaults, never world_probe.
    $worldIni = New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Port 47830 -World 2
    Assert-Equal $worldIni ($hostIni.Replace("world=3`n", "world=2`n")) 'play -World 2 overrides the default shared world level'
    Assert-Equal (New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Port 47830 -World 0) ($hostIni.Replace("world=3`n", '')) 'play -World 0 explicitly retains partner-only text'
    Assert-True ($hostIni.EndsWith("world=3`n") -and $guestIni.EndsWith("world=3`n")) 'both default play roles enable time/weather and mission following'
    foreach ($w in 1, 2, 3) {
        $t = New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -Port 47831 -PeerAddress '203.0.113.7' -World $w
        Assert-True ($t.EndsWith("partner_native_profile=gun`nworld=$w`n") -and !$t.Contains('world_probe')) "play -World ${w}: world=${w}, never world_probe"
    }
    $clipsIni = New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Port 47830 -Partner clips
    Assert-Equal $clipsIni ($hostIni.Replace("partner_native_profile=gun`n", '')) 'play -Partner clips: the clip partner, still sending input'
    Assert-True ($hostIni.Contains("partner_input_send=1`npartner_native_profile=gun`n") -and !$hostIni.Contains('partner_drive') -and !$hostIni.Contains('partner_input_defer')) 'play -Partner native: only the profile key, never the raw native keys'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Partner puppet } '' 'play -Partner validated'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -World 4 } '' 'play -World 4 refused'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -World -1 } '' 'play -World -1 refused'
    $coopIni = New-CoopPlayCoopIniText -Character quiet
    Assert-Equal $coopIni "$header`nmaster_url=http://127.0.0.1:47899/tppstm/gate`ncharacter=quiet`ncharacter_fallback=dd_male`n" 'play coop.ini text (closed-port master URL)'
    Assert-True (Test-LabUrl (ConvertFrom-IniText $coopIni)['master_url']) 'play master URL passes the redirect validator'
    Assert-Throws { New-CoopPlayCoopIniText -Character dog } 'Invalid character' 'play coop.ini character validated'
    # The in-game status line mode (docs/two-pc-play.md "In game"): only with -Hud.
    $hudCoop = New-CoopPlayCoopIniText -Character quiet -Hud Off
    Assert-Equal $hudCoop ($coopIni + "hud=off`n") 'play coop.ini: -Hud appends hud= (lower case)'
    Assert-Throws { New-CoopPlayCoopIniText -Character quiet -Hud big } 'Hud' 'play coop.ini hud validated'
    if ($IniCheck) {
        $file = Join-Path $fixture 'hud-coop.ini'; [IO.File]::WriteAllText($file, $hudCoop, [Text.UTF8Encoding]::new($false))
        $n = Invoke-IniCheck coop $file
        if ($n.ContainsKey('hud')) { Assert-Equal "$($n['hud'])|$($n['unknown_keys'])/$($n['invalid_values'])" 'off|0/0' 'native: coop.ini hud' }
    }
    # The automatic save handoff and the travel lock (docs/two-pc-play.md "Save handoff"): one
    # line each just before the world line; without the parameters the text above, byte for byte.
    Assert-Equal (New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Port 47830 -SaveHandoff '' -TravelLock '') $hostIni 'no -SaveHandoff/-TravelLock: the default text byte for byte'
    Assert-Equal (New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Port 47830 -TravelLock on) $hostIni '-TravelLock on writes nothing (the play default)'
    $sceneIni = New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -Port 47831 -PeerAddress '203.0.113.7' -SaveHandoff scene
    Assert-Equal $sceneIni ($guestIni.Replace("world=3`n", "save_handoff=scene`nworld=3`n")) '-SaveHandoff scene: exactly save_handoff=scene before the world line'
    $lockIni = New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -Port 47831 -PeerAddress '203.0.113.7' -SaveHandoff Off -TravelLock off
    Assert-Equal $lockIni ($guestIni.Replace("world=3`n", "save_handoff=off`ntravel_lock=0`nworld=3`n")) '-SaveHandoff off -TravelLock off: lower case, before the world line'
    Assert-True ((New-CoopPlayLabIniText -Role host -SaveDir $saveDir -World 0 -SaveHandoff always -TravelLock off).EndsWith("partner_native_profile=gun`nsave_handoff=always`ntravel_lock=0`n")) '-World 0: the two keys end the text'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -SaveHandoff sometimes } '' '-SaveHandoff validated'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -TravelLock maybe } '' '-TravelLock validated'
    $handoffPaths = Get-InstancePaths -GameDirectory 'C:\Games\MGS_TPP'
    Assert-Equal "$($handoffPaths.CoopSavesHandoff)|$($handoffPaths.CoopSavesReplaced)|$($handoffPaths.LinkRejoin)" 'C:\Games\MGS_TPP\mgs-coop\coop-saves-handoff|C:\Games\MGS_TPP\mgs-coop\coop-saves-replaced|C:\Games\MGS_TPP\mgs-coop\link-rejoin.txt' 'instance paths of the save handoff'
    if ($IniCheck) {
        # The loader parses both keys (lab_config, P2-11): the texts are clean and
        # give the policy and the effective travel lock (absent = on in play).
        $probeHandoff = Join-Path $fixture 'probe-handoff-lab.ini'
        [IO.File]::WriteAllText($probeHandoff, "enabled=1`nrole=guest`nsave_handoff=scene`ntravel_lock=0`n")
        $handoffKeys = [string](Invoke-IniCheck lab $probeHandoff)['unknown_keys'] -eq '0'
        Assert-True $handoffKeys 'native: lab.ini save_handoff and travel_lock are known keys'
        foreach ($case in @(@('scene', $sceneIni, 'scene|1'), @('lock', $lockIni, 'off|0'))) {
            $file = Join-Path $fixture "play-$($case[0])-lab.ini"; [IO.File]::WriteAllText($file, $case[1], $utf8)
            $n = Invoke-IniCheck lab $file
            Assert-Equal "$($n['present'])|$($n['mode'])|$($n['play_refused'])|$($n['unknown_keys'])/$($n['invalid_values'])" '1|play|0x0|0/0' "native: play lab.ini with the $($case[0]) keys is clean"
            Assert-Equal "$($n['save_handoff'])|$($n['travel_lock'])" $case[2] "native: play lab.ini with the $($case[0]) keys parses them"
        }
    }
    # Non-ASCII save_dir: UTF-8 bytes, never '?'.
    $uniSave = 'D:\Jeux Vid' + [char]0x00E9 + 'o\MGS_TPP\mgs-coop\coop-saves'
    $uniBytes = $utf8.GetBytes((New-CoopPlayLabIniText -Role host -SaveDir $uniSave))
    Assert-True ([Text.Encoding]::UTF8.GetString($uniBytes).Contains("save_dir=$uniSave") -and @($uniBytes | Where-Object { $_ -eq 0xC3 }).Count -eq 1) 'play lab.ini keeps a non-ASCII save_dir as UTF-8'
    if ($linkKeys) {
        foreach ($case in @(@('host', $hostIni), @('guest', $guestIni))) {
            $file = Join-Path $fixture "play-$($case[0])-lab.ini"; [IO.File]::WriteAllText($file, $case[1], $utf8)
            $n = Invoke-IniCheck lab $file
            Assert-Equal "$($n['present'])|$($n['mode'])|$($n['role'])|$($n['play_refused'])|$($n['unknown_keys'])/$($n['invalid_values'])" "1|play|$($case[0])|0x0|0/0" "native: play $($case[0]) lab.ini clean"
            Assert-Equal "$($n['net'])|$($n['net_bind'])|$($n['partner_interp'])|$($n['loopback'])" '1|any|auto|0' "native: play $($case[0]) net and interp"
        }
        $n = Invoke-IniCheck lab (Join-Path $fixture 'play-guest-lab.ini')
        Assert-Equal "$($n['net_listen_port'])|$($n['net_peer_addr'])|$($n['net_peer_port'])" '0|203.0.113.7|47831' 'native: play guest ports and peer'
        $file = Join-Path $fixture 'play-world-lab.ini'; [IO.File]::WriteAllText($file, $worldIni, $utf8)
        $n = Invoke-IniCheck lab $file
        if ($n.ContainsKey('world')) {
            Assert-Equal "$($n['world'])|$($n['world_probe'])|$($n['play_refused'])|$($n['unknown_keys'])/$($n['invalid_values'])" '2|none|0x0|0/0' 'native: play world=2 accepted'
        }
        # The native partner as the DLL expands it (lab_ini_check prints the online keys after the Steam keys).
        $n = Invoke-IniCheck lab (Join-Path $fixture 'play-host-lab.ini')
        if ($n.ContainsKey('partner_native_profile')) {
            Assert-Equal "$($n['partner_native_profile'])|$($n['partner_drive'])|$($n['partner_input_defer'])|$($n['partner_input_send'])|$($n['partner_input_mask'])|$($n['partner_native_gun'])|$($n['partner_native_fire'])|$($n['partner_native_aim'])|$($n['partner_native_rates'])|$($n['partner_input_words'])" 'gun|native|on|1|0x224005E|1|visual|look|mirror|hpr' 'native: play -Partner native expands to the qualified gun stage'
            Assert-Equal "$($n['partner_native_net'])|$($n['partner_native_stall'])|$($n['partner_native_stall_ms'])|$($n['partner_native_hold'])|$($n['partner_input_edge_repeats'])|$($n['play_rehearsal'])|$($n['play_refused'])" 'online|hold|400|own|5|0|0x0' 'native: play defaults online, stall hold, 5 edge repeats, no refusal'
            if ($n.ContainsKey('partner_ride_tilt')) {
                # WS3 vehicles (docs/vehicle-ws3.md): play runs the seat by id and RIDE_TILT by default; the census stays lab only.
                Assert-Equal "$($n['partner_ride_census'])|$($n['partner_ride_seat_id'])|$($n['partner_ride_tilt'])" '0|1|1' 'native: play admits the seat by id and RIDE_TILT by default, no census'
            }
            # Stage B (docs/anim-round2-throws.md): native partner throws are on by default with the play gun profile.
            # Play-Coop writes no partner_native_support key; the loader resolves it, and a named 0 opts out.
            Assert-True (!$hostIni.Contains('partner_native_support') -and !$guestIni.Contains('partner_native_support') -and !$clipsIni.Contains('partner_native_support')) 'Play-Coop lab.ini never names partner_native_support'
            Assert-Equal "$($n['partner_native_support'])|$($n['partner_native_support_given'])" '1|0' 'native: play -Partner native defaults native throws on (Stage B)'
            # WS4 v1 (docs/anim-round2-throws.md "WS4 v1 on by default"): native C4 placing and arm holds follow the play
            # gun profile too; Play-Coop names neither key, and a named 0 opts out of either.
            Assert-True (!$hostIni.Contains('partner_native_placed') -and !$hostIni.Contains('partner_native_arm') -and
                !$guestIni.Contains('partner_native_placed') -and !$guestIni.Contains('partner_native_arm') -and
                !$clipsIni.Contains('partner_native_placed') -and !$clipsIni.Contains('partner_native_arm')) 'Play-Coop lab.ini never names the WS4 v1 keys'
            Assert-Equal "$($n['partner_native_placed'])|$($n['partner_native_arm'])|$($n['partner_native_placed_given'])|$($n['partner_native_arm_given'])" '1|1|0|0' 'native: play -Partner native defaults C4 and arm holds on (WS4 v1)'
            # WS4 v2: the C4 place / detonate mirror is lab only until its hidden run; Play-Coop never names it.
            Assert-True (!$hostIni.Contains('partner_native_place_mirror') -and !$guestIni.Contains('partner_native_place_mirror') -and
                !$clipsIni.Contains('partner_native_place_mirror')) 'Play-Coop lab.ini never names the WS4 v2 mirror'
            Assert-Equal "$($n['partner_native_place_mirror'])" '0' 'native: play leaves the WS4 v2 mirror off'
            $file = Join-Path $fixture 'play-ws4v2-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "partner_native_place_mirror=1`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['partner_native_place_mirror'])|$($m['partner_native_placed'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" '0|1|0x0|0/0' 'native: play forces a named WS4 v2 mirror off'
            # 0.13.6 online-net-epoch: link_session_skew is a lab / worker reproduction knob (a link impairment); a
            # friend's play refuses it (kPlayRefusedImpairment 0x4000, counted as one invalid value) and forces 0.
            Assert-Equal "$($n['link_session_skew'])" '0' 'native: play has no link-session skew'
            $file = Join-Path $fixture 'play-skew-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "link_session_skew=2`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['link_session_skew'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" '0|0x4000|0/1' 'native: play refuses a named link_session_skew (impairment, forced 0)'
            # Traversal replay (docs/native-puppet.md "Traversal icon") follows the native gun profile in play (user decision
            # 2026-10-22; step-on proven in hidden run 104e9a47); Play-Coop never names it, and a named 0 opts out.
            Assert-True (!$hostIni.Contains('partner_native_traverse') -and !$guestIni.Contains('partner_native_traverse') -and
                !$clipsIni.Contains('partner_native_traverse')) 'Play-Coop lab.ini never names partner_native_traverse'
            Assert-Equal "$($n['partner_native_traverse'])|$($n['partner_native_traverse_given'])" '1|0' 'native: play defaults the traversal replay on'
            $file = Join-Path $fixture 'play-traverse-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "partner_native_traverse=0`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['partner_native_traverse'])|$($m['partner_native_traverse_given'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" '0|1|0x0|0/0' 'native: a named traversal 0 opts out in play'
            # F1 vehicles (docs/native-puppet.md "Vehicle boarding"): the board follows the native gun profile in play as
            # passenger (the back seat with the passenger camera; hidden runs 495e7c3a lab and de949f80 play; block:
            # 0348c4d8, fad26e25); a named block or passenger runs as named. Play-Coop never names the key.
            Assert-True (!$hostIni.Contains('partner_ride_board') -and !$guestIni.Contains('partner_ride_board') -and
                !$clipsIni.Contains('partner_ride_board')) 'Play-Coop lab.ini never names partner_ride_board'
            Assert-Equal "$($n['partner_ride_board'])|$($n['partner_ride_board_given'])" 'passenger|0' 'native: play defaults the board to passenger'
            foreach ($value in @('block', 'passenger')) {
                $file = Join-Path $fixture "play-board-$value-lab.ini"; [IO.File]::WriteAllText($file, $hostIni + "partner_ride_board=$value`n", $utf8)
                $m = Invoke-IniCheck lab $file
                Assert-Equal "$($m['partner_ride_board'])|$($m['partner_ride_board_given'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" "$value|1|0x0|0/0" "native: play admits a named board $value"
            }
            # F2: the native cardboard box on body 1 (docs/native-puppet.md "Cardboard box") follows the native gun profile in
            # play (hidden runs 05cffdd1 lab and the play rehearsal; the observer admits play only with link v2 connected).
            # Play-Coop never names the key, and a named 0 opts out.
            Assert-True (!$hostIni.Contains('partner_native_cbox') -and !$guestIni.Contains('partner_native_cbox') -and
                !$clipsIni.Contains('partner_native_cbox')) 'Play-Coop lab.ini never names partner_native_cbox'
            Assert-Equal "$($n['partner_native_cbox'])|$($n['partner_native_cbox_given'])" '1|0' 'native: play defaults the native box on'
            $file = Join-Path $fixture 'play-cbox-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "partner_native_cbox=0`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['partner_native_cbox'])|$($m['partner_native_cbox_given'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" '0|1|0x0|0/0' 'native: a named native box 0 opts out in play'
            # Gestures (docs/native-puppet.md "Gestures") follow the native gun profile in play (hidden runs 0e343f72 lab and
            # the play rehearsal); Play-Coop never names the key, and a named 0 opts out.
            Assert-True (!$hostIni.Contains('partner_native_gesture') -and !$guestIni.Contains('partner_native_gesture') -and
                !$clipsIni.Contains('partner_native_gesture')) 'Play-Coop lab.ini never names partner_native_gesture'
            Assert-Equal "$($n['partner_native_gesture'])|$($n['partner_native_gesture_given'])" '1|0' 'native: play defaults gestures on'
            $file = Join-Path $fixture 'play-gesture-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "partner_native_gesture=0`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['partner_native_gesture'])|$($m['partner_native_gesture_given'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" '0|1|0x0|0/0' 'native: a named gesture 0 opts out in play'
            # G1 partner HUD hints (docs/native-puppet.md "Partner HUD hints") are lab only and default 0; Play-Coop never
            # names the key, and play forces a named 1 off (not a play refusal).
            Assert-True (!$hostIni.Contains('partner_native_ui_filter') -and !$guestIni.Contains('partner_native_ui_filter') -and
                !$clipsIni.Contains('partner_native_ui_filter')) 'Play-Coop lab.ini never names partner_native_ui_filter'
            Assert-Equal "$($n['partner_native_ui_filter'])|$($n['partner_native_ui_filter_given'])" '0|0' 'native: play leaves the ui filter off'
            $file = Join-Path $fixture 'play-ui-filter-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "partner_native_ui_filter=1`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['partner_native_ui_filter'])|$($m['partner_native_ui_filter_given'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" '0|1|0x0|0/0' 'native: play forces a named ui filter off'
            # The gesture lab lever (docs/native-puppet.md "Gestures", "Lab lever: knock and hand signs") is lab only and
            # default 0; Play-Coop never names it, and play forces a named 1 off.
            Assert-True (!$hostIni.Contains('partner_gesture_lever') -and !$guestIni.Contains('partner_gesture_lever') -and
                !$clipsIni.Contains('partner_gesture_lever')) 'Play-Coop lab.ini never names partner_gesture_lever'
            Assert-Equal "$($n['partner_gesture_lever'])|$($n['partner_gesture_lever_given'])" '0|0' 'native: play leaves the gesture lever off'
            $file = Join-Path $fixture 'play-gesture-lever-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "partner_gesture_lever=1`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['partner_gesture_lever'])|$($m['partner_gesture_lever_given'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" '0|1|0x0|0/0' 'native: play forces a named gesture lever off'
            # Horse / D-Walker riders (docs/mounts.md): hide with the native gun profile in lab and play (hidden runs
            # bfd6fcc4 / 2ab88411 lab and the play rehearsal 5a044a4d); Play-Coop never names the key, and a named off opts out.
            Assert-True (!$hostIni.Contains('partner_native_mount') -and !$guestIni.Contains('partner_native_mount') -and
                !$clipsIni.Contains('partner_native_mount')) 'Play-Coop lab.ini never names partner_native_mount'
            Assert-Equal "$($n['partner_native_mount'])|$($n['partner_native_mount_given'])" 'hide|0' 'native: play defaults the mount hide on'
            $file = Join-Path $fixture 'play-mount-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "partner_native_mount=off`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['partner_native_mount'])|$($m['partner_native_mount_given'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" 'off|1|0x0|0/0' 'native: a named mount off opts out in play'
            # G4 truck test levers (docs/native-puppet.md "Truck test levers"): a lab tool, off in play; Play-Coop never
            # names the key, and a named 1 is forced off in play (no refusal bit).
            Assert-True (!$hostIni.Contains('debug_vehicle_door') -and !$guestIni.Contains('debug_vehicle_door') -and
                !$clipsIni.Contains('debug_vehicle_door')) 'Play-Coop lab.ini never names debug_vehicle_door'
            Assert-Equal "$($n['debug_vehicle_door'])|$($n['debug_vehicle_door_given'])" '0|0' 'native: play leaves the door levers off'
            $file = Join-Path $fixture 'play-door-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "debug_vehicle_door=1`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['debug_vehicle_door'])|$($m['debug_vehicle_door_given'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" '0|1|0x0|0/0' 'native: play forces a named door key off'
            # G5 (docs/native-puppet.md "Ride hides"): the other ride hides' unrealize is lab only and default 0; Play-Coop
            # never names it, and play forces a named 1 off.
            Assert-True (!$hostIni.Contains('partner_native_unrealize_all') -and !$guestIni.Contains('partner_native_unrealize_all') -and
                !$clipsIni.Contains('partner_native_unrealize_all')) 'Play-Coop lab.ini never names partner_native_unrealize_all'
            Assert-Equal "$($n['partner_native_unrealize_all'])|$($n['partner_native_unrealize_all_given'])" '0|0' 'native: play leaves the ride-hide unrealize off'
            $file = Join-Path $fixture 'play-unrealize-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "partner_native_unrealize_all=1`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['partner_native_unrealize_all'])|$($m['partner_native_unrealize_all_given'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" '0|1|0x0|0/0' 'native: play forces a named ride-hide unrealize off'
            # The developer test boot (docs/test-boot.md): lab only. Play-Coop never names its keys, and play forces both
            # off with kPlayRefusedLabBoot (0x1000000; a play rehearsal gate then refuses the session).
            Assert-True (!$hostIni.Contains('lab_fast_boot') -and !$guestIni.Contains('lab_fast_boot') -and !$hostIni.Contains('lab_boot') -and
                !$guestIni.Contains('lab_boot') -and !$clipsIni.Contains('lab_boot')) 'Play-Coop lab.ini never names the test boot'
            Assert-Equal "$($n['lab_fast_boot'])|$($n['lab_boot_mission'])" '0|0' 'native: play leaves the test boot off'
            $file = Join-Path $fixture 'play-boot-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "lab_fast_boot=1`nlab_boot=30020 time=12:00`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['lab_fast_boot'])|$($m['lab_boot_mission'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" '0|0|0x1000000|0/1' 'native: play forces the test boot off with its refusal bit'
            $n = Invoke-IniCheck lab (Join-Path $fixture 'play-guest-lab.ini')
            Assert-Equal "$($n['partner_native_support'])|$($n['partner_native_support_given'])|$($n['play_refused'])" '1|0|0x0' 'native: play guest defaults native throws on (Stage B)'
            Assert-Equal "$($n['partner_native_placed'])|$($n['partner_native_arm'])|$($n['partner_native_placed_given'])|$($n['partner_native_arm_given'])" '1|1|0|0' 'native: play guest defaults C4 and arm holds on (WS4 v1)'
            Assert-Equal "$($n['partner_ride_census'])|$($n['partner_ride_seat_id'])|$($n['partner_ride_tilt'])" '0|1|1' 'native: play guest admits the seat by id and RIDE_TILT by default, no census'
            $file = Join-Path $fixture 'play-support-off-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "partner_native_support=0`n", $utf8)
            $n = Invoke-IniCheck lab $file
            Assert-Equal "$($n['partner_native_support'])|$($n['partner_native_support_given'])|$($n['partner_native_profile'])|$($n['play_refused'])|$($n['unknown_keys'])/$($n['invalid_values'])" '0|1|gun|0x0|0/0' 'native: a named partner_native_support=0 opts play out of native throws'
            $file = Join-Path $fixture 'play-ws4-off-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "partner_native_placed=0`npartner_native_arm=0`n", $utf8)
            $n = Invoke-IniCheck lab $file
            Assert-Equal "$($n['partner_native_placed'])|$($n['partner_native_arm'])|$($n['partner_native_placed_given'])|$($n['partner_native_arm_given'])|$($n['partner_native_support'])|$($n['play_refused'])|$($n['unknown_keys'])/$($n['invalid_values'])" '0|0|1|1|1|0x0|0/0' 'native: named WS4 v1 0s opt play out of C4 and arm holds, throws stay on'
            $file = Join-Path $fixture 'play-clips-lab.ini'; [IO.File]::WriteAllText($file, $clipsIni, $utf8)
            $n = Invoke-IniCheck lab $file
            Assert-Equal "$($n['partner_native_profile'])|$($n['partner_drive'])|$($n['partner_input_send'])|$($n['play_refused'])|$($n['invalid_values'])" 'off|legacy|1|0x0|0' 'native: play -Partner clips keeps the clip partner'
            Assert-Equal "$($n['partner_native_support'])|$($n['partner_native_support_given'])" '0|0' 'native: play -Partner clips has no native throws'
            Assert-Equal "$($n['partner_native_placed'])|$($n['partner_native_arm'])" '0|0' 'native: play -Partner clips has no C4 or arm holds'
        }
        $file = Join-Path $fixture 'play-coop.ini'; [IO.File]::WriteAllText($file, $coopIni)
        $n = Invoke-IniCheck coop $file
        Assert-Equal "$($n['master_url'])|$($n['character'])|$($n['unknown_keys'])/$($n['invalid_values'])" 'http://127.0.0.1:47899/tppstm/gate|quiet|0/0' 'native: play coop.ini'
    }

    # ------------------------------------------------------------ Steam online: MGS2 codes and lab.ini (docs/steam-online.md 9)
    # Shared vectors (computed independently; tests/steam_transport_tests.cpp checks the same).
    $steamVectors = @(@('ABCDE12345FGHJK67890', '76561198011111111', '1A'), @('C00PC00PC00PC00PC00P', '76561197960265729', 'D1'),
        @('ZZZZZYYYYYXXXXXWWWWW', '76561202255233023', '5D'), @('0123456789ABCDEFGHJK', '76561198022222222', 'VF'))
    foreach ($v in $steamVectors) {
        $c = New-CoopSteamJoinCode -Secret $v[0] -SteamId $v[1]
        Assert-Equal $c ('MGS2-' + $v[0].Substring(0, 5) + '-' + $v[0].Substring(5, 5) + '-' + $v[0].Substring(10, 5) + '-' + $v[0].Substring(15, 5) + '-' + $v[2] + '@' + $v[1]) "MGS2 vector $($v[1])"
        $rt = ConvertFrom-CoopSteamJoinCode $c
        Assert-Equal "$($rt.Status)|$($rt.Secret)|$($rt.SteamId)" "ok|$($v[0])|$($v[1])" "MGS2 round trip $($v[1])"
        if ($steamKeys) {
            $n = Invoke-IniCheck steamcode $c
            Assert-Equal "$($n['steamcode_status'])|$($n['host'])" "ok|$($v[1])" "native: MGS2 vector $($v[1])"
            Assert-True (!(($n.Values -join ' ').Contains($v[0]))) 'native: the MGS2 check never prints the secret'
        }
    }
    Assert-Equal (ConvertFrom-CoopSteamJoinCode ' mgs2 abcde i2345 fghjk 67890 1a @ 76561198011111111').Status 'ok' 'MGS2: blanks, case and I -> 1'
    # The native IsBlank set: space, tab, CR, LF, VT and FF (a pasted code may carry a line break).
    Assert-Equal (ConvertFrom-CoopSteamJoinCode ("`tMGS2-ABCDE-12345-`r`nFGHJK-67890-1A@76561198011111111`v`f`n")).Status 'ok' 'MGS2: tab, CR, LF, VT and FF are blanks'
    $steamBad = @(@('', 'empty'), @('MGS1-ABCDE-12345-FGHJK-67890-1A@203.0.113.7', 'bad_prefix'), @('MGS2-ABCDE-12345-FGHJK-67890-1A', 'bad_steamid'),
        @('MGS2-ABCDU-12345-FGHJK-67890-1A@76561198011111111', 'bad_char'), @('MGS2-ABCDE-12345-FGHJK-6789-1A@76561198011111111', 'bad_length'),
        @('MGS2-ABCDE-12345-FGHJK-67890-1B@76561198011111111', 'bad_check'), @('MGS2-ABCDE-12345-FGHJK-67890-1A@76561198011111112', 'bad_check'),
        @('MGS2-ABCDE-12345-FGHJK-67890-1A@076561198011111111', 'bad_steamid'), @('MGS2-ABCDE-12345-FGHJK-67890-1A@103582791429521408', 'bad_steamid'),
        @('MGS2-ABCDE-12345-FGHJK-67890-1A@76561197960265728', 'bad_steamid'))
    foreach ($b in $steamBad) {
        Assert-Equal (ConvertFrom-CoopSteamJoinCode $b[0]).Status $b[1] "MGS2 '$($b[0])' -> $($b[1])"
        if ($steamKeys -and $b[0]) { Assert-Equal (Invoke-IniCheck steamcode $b[0])['steamcode_status'] $b[1] "native: MGS2 '$($b[0])' -> $($b[1])" }
        Assert-True ([bool](Get-CoopSteamCodeProblem -Status $b[1])) "sentence for $($b[1])"
    }
    Assert-Equal (Get-CoopSteamId64 -ActiveUser 0) '' 'no Steam account: no id'
    Assert-Equal (Get-CoopSteamId64 -ActiveUser 1) '76561197960265729' 'SteamID64 of account 1'
    Assert-Equal (Get-CoopSteamId64 -ActiveUser 4294967295) '76561202255233023' 'SteamID64 of the last account'
    Assert-True (!(Test-CoopSteamId64 '76561197960265728') -and (Test-CoopSteamId64 '76561197960265729') -and !(Test-CoopSteamId64 '7656119796026572')) 'individual SteamID64 rule'
    $steamHostIni = New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Transport steam -SteamSelf '76561198011111111'
    Assert-Equal $steamHostIni ("$header`nenabled=1`nmode=play`nrole=host`nsave_dir=$saveDir`nnet=1`nnet_transport=steam`nnet_steam_self=76561198011111111`npartner_interp=auto`n$defaultsText`nworld=3`n") 'online host lab.ini: no UDP key'
    $steamGuestIni = New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -Transport steam -SteamSelf '76561198022222222' -SteamPeer '76561198011111111'
    Assert-True ($steamGuestIni.Contains("net_transport=steam`nnet_steam_self=76561198022222222`nnet_peer_steam=76561198011111111`n") -and !$steamGuestIni.Contains('net_bind') -and !$steamGuestIni.Contains('net_peer_addr')) 'online guest lab.ini'
    $steamWaitIni = New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -Transport steam -SteamSelf '76561198022222222' -SteamWait
    Assert-True ($steamWaitIni.Contains("net_steam_wait=1`n") -and !$steamWaitIni.Contains('net_peer_steam')) 'waiting guest lab.ini'
    Assert-Throws { New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -Transport steam -SteamSelf '76561198022222222' -SteamPeer '76561198022222222' } 'own code' 'own code refused'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Transport steam -SteamSelf '76561198011111111' -SteamPeer '76561198022222222' } 'no Steam peer' 'a host has no Steam peer'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -SteamSelf '76561198011111111' } 'Transport steam' 'Steam keys need -Transport steam'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Transport steam -SteamSelf '1' } 'SteamID64' 'own SteamID64 validated'
    # The two-PC A/B variant of the code-join sources (docs/steam-online.md 20.5): only with -SteamDispatch, lower case.
    $dispatchIni = New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Transport steam -SteamSelf '76561198011111111' -SteamDispatch FnPtr
    Assert-True ($dispatchIni.Contains("net_steam_self=76561198011111111`nnet_steam_dispatch=fnptr`npartner_interp=auto`n")) 'online A/B variant key'
    Assert-True (!$steamHostIni.Contains('net_steam_dispatch')) 'no variant key by default'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -SteamDispatch poll } 'Transport steam' 'the variant needs -Transport steam'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Transport steam -SteamSelf '76561198011111111' -SteamDispatch fast } 'SteamDispatch' 'unknown variant refused'
    if ($steamKeys) {
        $file = Join-Path $fixture 'steam-dispatch-lab.ini'; [IO.File]::WriteAllText($file, $dispatchIni, $utf8)
        $n = Invoke-IniCheck lab $file
        if ($n.ContainsKey('net_steam_dispatch')) {
            Assert-Equal "$($n['net_steam_dispatch'])|$($n['play_refused'])|$($n['unknown_keys'])/$($n['invalid_values'])" 'fnptr|0x0|0/0' 'native: the A/B variant key is clean'
            $file = Join-Path $fixture 'steam-default-lab.ini'; [IO.File]::WriteAllText($file, $steamHostIni, $utf8)
            $n = Invoke-IniCheck lab $file
            Assert-Equal "$($n['net_steam_dispatch'])|$($n['steam_selftest_fn'])|$($n['coop_hud'])" 'both|1|' 'native: online defaults (both; no HUD key: compact in play)'
        }
    }
    # The shared plain-language vectors (native/coop_reason_text.cpp reads the same file in steam_transport_tests).
    $vectors = Get-Content -LiteralPath (Join-Path $projectRoot 'tests/fixtures/link-reasons.json') -Raw | ConvertFrom-Json
    Assert-True (@($vectors.link_reasons).Count -ge 25 -and @($vectors.steam_end).Count -ge 15) 'the shared vector file'
    foreach ($v in @($vectors.link_reasons)) {
        Assert-Equal (Get-CoopLinkReasonText -Reason ([string]$v.reason) -RemoteBuild ([string]$v.remote) -OwnBuild ([string]$v.own) -Verified ([int]$v.verified)) ([string]$v.text) "shared vector: $($v.reason)/$($v.verified)"
    }
    foreach ($v in @($vectors.steam_end)) {
        Assert-Equal (Get-CoopSteamEndText -End ([int]$v.end)) ([string]$v.text) "shared vector: steam_end $($v.end)"
    }
    if ($steamKeys) {
        foreach ($case in @(@('host', $steamHostIni, 'steam|0|0|0'), @('guest', $steamGuestIni, 'steam|0|76561198011111111|0'), @('guest', $steamWaitIni, 'steam|1|0|0'))) {
            $file = Join-Path $fixture "steam-$($case[0])-lab.ini"; [IO.File]::WriteAllText($file, $case[1], $utf8)
            $n = Invoke-IniCheck lab $file
            Assert-Equal "$($n['present'])|$($n['mode'])|$($n['role'])|$($n['play_refused'])|$($n['unknown_keys'])/$($n['invalid_values'])|$($n['net'])" "1|play|$($case[0])|0x0|0/0|1" "native: online $($case[0]) lab.ini clean"
            Assert-Equal "$($n['net_transport'])|$($n['net_steam_wait'])|$($n['net_peer_steam'])|$($n['steam_selftest'])" $case[2] "native: online $($case[0]) Steam keys"
        }
    }
    # The Steam reasons and the status line.
    foreach ($reason in 'no_peer_steam', 'steam_offline', 'steam_self_mismatch', 'steam_peer_is_self', 'steam_api', 'steam_not_initialized', 'steam_client_module',
            'steam_client_iface', 'steam_iface_missing', 'steam_iface_module', 'steam_iface_rtti', 'steam_iface_shape', 'steam_owner_unavailable', 'steam_fault',
            'steam_acquire_timeout', 'steam_transport', 'steam_something_new') {
        $sentence = Get-CoopLinkReasonText -Reason $reason
        Assert-True ([bool]$sentence -and !$sentence.StartsWith('The connection was refused')) "Steam sentence for $reason"
    }
    Assert-True ((Get-CoopLinkReasonText -Reason steam_iface_shape).Contains('Tools > Collect logs') -and !(Get-CoopLinkReasonText -Reason steam_iface_shape).Contains('LAN/VPN')) 'a Steam networking change points to Collect logs (Steam only: no LAN/VPN code)'
    $steamStatus = '{"v":1,"pid":4242,"mode":"play","role":"host","state":"connected","reason":"","verified":1,"session":1,"peer":"steam:...1111","local_port":0,"rtt_ms":61,"jitter_ms":3,"loss_permille":0,"resend":0,"tx_Bps":3900,"rx_Bps":3800,"interp_ms":80,"remote_build":"0.12.0","partner":"live","partner_shown":1,"look":1,"mission":30050,"saves":"redirected","transport":"steam","route":"relay","accepting":1,"steam":"ok","time_utc":"2026-10-05T12:00:00Z"}'
    $sst = ConvertFrom-CoopStatusText -Text $steamStatus -ExpectedPid 4242
    $snow = [DateTime]::Parse('2026-10-05T12:00:03Z').ToUniversalTime()
    Assert-Equal (Format-CoopStatusLine -Status $sst -Role host -NowUtc $snow) '[HOST] link=connected via Steam (relay) rtt=61ms jitter=3ms loss=0.0% interp=80ms | partner=live shown look | mission 30050' 'online status line'
    $waitStatus = ConvertFrom-CoopStatusText -Text ($steamStatus.Replace('"role":"host","state":"connected"', '"role":"guest","state":"waiting"').Replace('"route":"relay"', '"route":"none"'))
    Assert-True ((Format-CoopStatusLine -Status $waitStatus -Role guest -NowUtc $snow).StartsWith('[JOIN] link=waiting via Steam - waiting for an invite or a code')) 'waiting guest status line'
    $offStatus = ConvertFrom-CoopStatusText -Text ($steamStatus.Replace('"state":"connected","reason":""', '"state":"disabled","reason":"steam_offline"').Replace('"steam":"ok"', '"steam":"offline"'))
    Assert-True ((Format-CoopStatusLine -Status $offStatus -Role host -NowUtc $snow.AddSeconds(600)).Contains('Steam is offline or not logged in')) 'disabled online status explains the reason'
    # The partner's goodbye and the guest's Steam end reason (1252).
    $leftStatus = ConvertFrom-CoopStatusText -Text ($steamStatus.Replace('"state":"connected"', '"state":"lost"').Replace('"steam":"ok"', '"steam":"ok","steam_end":0,"left":1'))
    Assert-True ((Format-CoopStatusLine -Status $leftStatus -Role host -NowUtc $snow).Contains(' - your friend left; waiting for them to come back')) 'host: the partner left'
    $endStatus = ConvertFrom-CoopStatusText -Text ($steamStatus.Replace('"role":"host","state":"connected"', '"role":"guest","state":"connecting"').Replace('"steam":"ok"', '"steam":"ok","steam_end":5003,"left":0'))
    $endLine = Format-CoopStatusLine -Status $endStatus -Role guest -NowUtc $snow
    Assert-True ($endLine.Contains("(Steam: The host's game did not answer.") -and !$endLine.Contains('left')) 'guest: the Steam end reason in plain language'
    $liveStatus = ConvertFrom-CoopStatusText -Text ($steamStatus.Replace('"steam":"ok"', '"steam":"ok","steam_end":5003,"left":1'))
    Assert-True (!(Format-CoopStatusLine -Status $liveStatus -Role host -NowUtc $snow).Contains('Steam:') -and !(Format-CoopStatusLine -Status $liveStatus -Role host -NowUtc $snow).Contains('left')) 'connected: neither is shown'
    # Launch evidence for an online start.
    $okEarly = "saves=redirected; dir=$saveDir`r`nplay=1; loopback=0; mutex=unchanged; refused=0x0; saves=redirected`r`n"
    $okSteam = "master_server=redirected; method=test; url=http://127.0.0.1:47899/tppstm/gate`r`n"
    $okObserver = "play=active; role=host; partner=1; refused=0x0`r`nlink=config; mode=play; role=host; proto=2; bind=steam; port=0; peer=learned; auth=code; interp=auto`r`n"
    Assert-Equal (Get-CoopLaunchEvidence -EarlyLog $okEarly -SteamLog $okSteam -ObserverLog $okObserver -SaveDir $saveDir -Transport steam).State 'ok' 'online launch evidence ok'
    $ev = Get-CoopLaunchEvidence -EarlyLog $okEarly -SteamLog $okSteam -ObserverLog "play=active; role=host; partner=1; refused=0x0`r`n" -SaveDir $saveDir -Transport steam
    Assert-True ($ev.State -eq 'pending' -and (@($ev.Missing) -join ' ').Contains('bind=steam')) 'online launch evidence waits for the Steam link line'
    $ev = Get-CoopLaunchEvidence -EarlyLog $okEarly -SteamLog $okSteam -ObserverLog ($okObserver + "link=disabled; reason=steam_offline`r`n") -SaveDir $saveDir -Transport steam
    Assert-True ($ev.State -eq 'incomplete' -and $ev.LinkReason -eq 'steam_offline' -and (@($ev.Problems) -join ' ').Contains('Steam is offline')) 'a Steam refusal is reported with its sentence'
    Assert-Equal (Get-CoopLaunchEvidence -EarlyLog $okEarly -SteamLog $okSteam -ObserverLog "play=active; role=host; partner=1; refused=0x0`r`n" -SaveDir $saveDir).State 'ok' 'UDP launch evidence unchanged'

    # ------------------------------------------------------------ addresses and redaction (spec 7.1, C30)
    Assert-Equal (Get-CoopMaskedAddress '203.0.113.7:51234') '203.0.x.x:51234' 'public address masked'
    foreach ($a in '127.0.0.1', '10.1.2.3', '172.16.0.9', '172.31.255.1', '192.168.1.20:47830', '100.64.0.1', '100.127.255.255', '26.1.2.3') { Assert-Equal (Get-CoopMaskedAddress $a) $a "shown in full: $a" }
    foreach ($a in '172.32.0.1', '100.128.0.1', '11.0.0.1', '192.169.0.1') { Assert-True ((Get-CoopMaskedAddress $a).Contains('.x.x')) "masked: $a" }
    Assert-Equal "$(Get-CoopAddressKind '100.101.1.2')/$(Get-CoopAddressKind '10.147.17.5' -InterfaceAlias 'ZeroTier One [abc]')/$(Get-CoopAddressKind '26.4.5.6')/$(Get-CoopAddressKind '192.168.0.4')/$(Get-CoopAddressKind '8.8.8.8')/$(Get-CoopAddressKind '169.254.1.1')/$(Get-CoopAddressKind '127.0.0.1')" 'tailscale/zerotier/radmin/lan/public/skip/skip' 'address kinds for the host code list'
    Assert-True ((Test-CoopPlayPeerAddress '203.0.113.7') -and !(Test-CoopPlayPeerAddress '127.0.0.1') -and !(Test-CoopPlayPeerAddress '0.0.0.0') -and !(Test-CoopPlayPeerAddress '224.0.0.5') -and !(Test-CoopPlayPeerAddress '255.255.255.255')) 'play peer addresses'
    $secretCode = New-CoopJoinCode -Secret $s7 -HostName '203.0.113.7'
    $leaky = "code=$secretCode`nlower " + $secretCode.ToLowerInvariant() + "`nspaced MGS1 7K3QZ 9M2XW D4HT8 RVN6B DD @ 203.0.113.7`n" +
        "link=connected; peer=203.0.113.7:51234; x`nnet_peer_addr=198.51.100.23`n{`"peer`":`"203.0.113.9:47830`"}`nhost=8.8.4.4`nexe 1.0.15.4 version=10.0.19045.1 at 203.0.113.50`npeer=192.168.1.20:47830`n"
    $clean = Hide-CoopNetworkText $leaky
    Assert-True (!$clean.Contains('7K3QZ') -and !$clean.ToUpperInvariant().Contains('7K3QZ') -and !$clean.Contains('RVN6B')) 'join codes redacted (upper, lower, spaced)'
    Assert-Equal ([regex]::Matches($clean, 'MGS1-<redacted>').Count) 3 'each join code replaced'
    Assert-True ($clean.Contains('peer=203.0.x.x:51234') -and $clean.Contains('net_peer_addr=198.51.x.x') -and $clean.Contains('"peer":"203.0.x.x:47830"') -and $clean.Contains('host=8.8.x.x')) 'public addresses masked in address contexts'
    Assert-True ($clean.Contains('1.0.15.4') -and $clean.Contains('10.0.19045.1') -and $clean.Contains('at 203.0.113.50') -and $clean.Contains('peer=192.168.1.20:47830')) 'versions, other text and private addresses kept'
    $steamCodeText = New-CoopSteamJoinCode -Secret $s7 -SteamId '76561198011111111'
    $steamLeaky = "code $steamCodeText`nlower " + $steamCodeText.ToLowerInvariant() + "`nconnect +mgscoop_join $steamCodeText`nsteam=join; via=invite; host=steam:...1111`n"
    $steamClean = Hide-CoopNetworkText $steamLeaky
    Assert-True (!$steamClean.ToUpperInvariant().Contains('7K3QZ') -and !$steamClean.Contains('76561198011111111')) 'MGS2 codes and invite strings redacted'
    Assert-Equal ([regex]::Matches($steamClean, 'MGS2-<redacted>').Count) 2 'each MGS2 code replaced'
    Assert-True ($steamClean.Contains('+mgscoop_join <redacted>') -and $steamClean.Contains('host=steam:...1111')) 'the invite string replaced; masked SteamIDs kept'

    # ------------------------------------------------------------ status, log delta, launch evidence, status line
    $statusText = '{"v":1,"pid":4242,"mode":"play","role":"host","state":"connected","reason":"","verified":1,"session":1,"peer":"203.0.x.x:51234","local_port":47830,"rtt_ms":42,"jitter_ms":3,"loss_permille":4,"resend":0,"tx_Bps":3900,"rx_Bps":3800,"interp_ms":80,"remote_build":"0.12.0","partner":"live","partner_shown":1,"look":1,"mission":30050,"saves":"redirected","time_utc":"2026-10-05T12:00:00Z"}'
    $now = [DateTime]::Parse('2026-10-05T12:00:03Z').ToUniversalTime()
    $st = ConvertFrom-CoopStatusText -Text $statusText -ExpectedPid 4242
    Assert-Equal (Format-CoopStatusLine -Status $st -Role host -NowUtc $now) '[HOST] link=connected rtt=42ms jitter=3ms loss=0.4% interp=80ms | partner=live shown look | mission 30050' 'status line (spec example)'
    Assert-True ($null -eq (ConvertFrom-CoopStatusText -Text $statusText -ExpectedPid 4243)) 'status of another process ignored (pid check)'
    Assert-True ($null -eq (ConvertFrom-CoopStatusText -Text '{"v":2,"state":"x"}') -and $null -eq (ConvertFrom-CoopStatusText -Text '{"v":1,') -and $null -eq (ConvertFrom-CoopStatusText -Text '')) 'unknown, partial or empty status ignored'
    Assert-True ((Format-CoopStatusLine -Status $st -Role guest -NowUtc $now.AddSeconds(30)).StartsWith('[JOIN] game not responding')) 'a status older than 15 s: game not responding'
    # The loader writes a disabled link once (link-protocol.md 7.2): an old one still names its reason.
    $disabled = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"state":"connected","reason":""', '"state":"disabled","reason":"secret_missing"'))
    Assert-True ((Format-CoopStatusLine -Status $disabled -Role guest -NowUtc $now.AddSeconds(600)).StartsWith('[JOIN] link=disabled (secret_missing: ')) 'a disabled status is never reported as not responding'
    $elsewhere = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"mission":30050', '"mission":10020'))
    Assert-True ((Format-CoopStatusLine -Status $elsewhere -Role host -NowUtc $now).EndsWith('mission 10020 - Partner body unavailable in this mission; use Mother Base or free roam.')) 'story mission reports unavailable partner body'
    $rejected = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"state":"connected","reason":""', '"state":"rejected","reason":"build_mismatch"').Replace('"remote_build":"0.12.0"', '"remote_build":"0.12.1"'))
    Assert-True ((Format-CoopStatusLine -Status $rejected -Role guest -NowUtc $now -OwnBuild '0.12.0').Contains('Your partner runs Diamond Dogs Together 0.12.1; you run 0.12.0. Both must use the same version.')) 'build mismatch sentence'
    Assert-Equal (Get-CoopLinkReasonText -Reason busy) 'The host is already connected to someone.' 'busy sentence'
    Assert-True ((Get-CoopLinkReasonText -Reason bad_code -Verified 0).Contains('Possibly a wrong code')) 'bad_code sentence'
    foreach ($reason in 'proto_mismatch', 'mode_mismatch', 'role_mismatch', 'secret_missing', 'secret_bad_char', 'crypto', 'no_peer_addr', 'transport_bind', 'transport_socket', 'something_new') {
        Assert-True ([bool](Get-CoopLinkReasonText -Reason $reason)) "sentence for $reason"
    }
    Assert-True ((Format-CoopStatusLine -Status (ConvertFrom-CoopStatusText -Text '{"v":1,"state":"connecting"}') -Role guest) -ceq '[JOIN] link=connecting | partner=none | mission 0') 'a sparse status still formats (StrictMode-safe)'
    foreach ($freeMission in @(30010, 30020)) {
        $elsewhere.mission = $freeMission
        Assert-True ((Format-CoopStatusLine -Status $elsewhere -Role host -NowUtc $now).EndsWith("mission $freeMission")) 'free roam does not send the player back to Mother Base'
    }
    # A loading screen, the start screen and the helicopters are no "mission without a body" (K20).
    foreach ($quietMission in @(40050, 1, 40010, 40020)) {
        $quiet = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"mission":30050', ('"mission":' + $quietMission)))
        Assert-True ((Format-CoopStatusLine -Status $quiet -Role host -NowUtc $now).EndsWith("| mission $quietMission")) "no partner body suffix at mission $quietMission"
    }
    # The world-sync keys of link-status.json (link-protocol.md 7.2): the partner's settled mission.
    $worldKeys = ',"world":3,"paired":0,"partner_mission":30010,"pair_wait":"different_mission","progress":"ok","time_utc"'
    $apart = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"mission":30050', '"mission":10020').Replace(',"time_utc"', $worldKeys))
    Assert-Equal (Format-CoopStatusLine -Status $apart -Role host -NowUtc $now) '[HOST] link=connected rtt=42ms jitter=3ms loss=0.4% interp=80ms | partner=live shown look | mission 10020 - your partner is in Afghanistan free roam. Different missions never pair: meet in the same Mother Base, free roam or helicopter.' 'different missions: where the partner is and the pairing rule'
    $apartConnecting = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"state":"connected"', '"state":"connecting"').Replace('"mission":30050', '"mission":10020').Replace(',"time_utc"', $worldKeys))
    $apartLine = Format-CoopStatusLine -Status $apartConnecting -Role host -NowUtc $now
    Assert-True (!$apartLine.Contains('your partner is in') -and $apartLine.EndsWith('| mission 10020 - Partner body unavailable in this mission; use Mother Base or free roam.')) 'not connected: no different-missions explanation'
    $settling = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"mission":30050', '"mission":40050').Replace(',"time_utc"', $worldKeys.Replace('"different_mission"', '"settling"')))
    Assert-True ((Format-CoopStatusLine -Status $settling -Role host -NowUtc $now).EndsWith('| mission 40050')) 'settling: no explanation yet'
    $storyStart = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"mission":30050', '"mission":40010').Replace(',"time_utc"', ',"world":3,"paired":0,"partner_mission":0,"pair_wait":"no_hello","progress":"story_start","time_utc"'))
    Assert-True ((Format-CoopStatusLine -Status $storyStart -Role host -NowUtc $now).EndsWith('| mission 40010 Your co-op save is at the story start: quit, then import your partner''s save with Share-Save.bat (option I).')) 'story start: the Share-Save sentence'
    $storyApart = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"mission":30050', '"mission":10020').Replace(',"time_utc"', $worldKeys.Replace('"progress":"ok"', '"progress":"story_start"')))
    Assert-True ((Format-CoopStatusLine -Status $storyApart -Role host -NowUtc $now).EndsWith('free roam or helicopter. Your co-op save is at the story start: quit, then import your partner''s save with Share-Save.bat (option I).')) 'story start follows the different-missions explanation'
    # The guest whose host restarted hosting: the old code is refused (bad_code, unverified).
    $staleText = $statusText.Replace('"role":"host","state":"connected","reason":"","verified":1', '"role":"guest","state":"reconnecting","reason":"bad_code","verified":0').Replace('"saves":"redirected"', '"saves":"redirected","left":1')
    $staleLine = Format-CoopStatusLine -Status (ConvertFrom-CoopStatusText -Text $staleText) -Role guest -NowUtc $now
    Assert-True ($staleLine.Contains(' - the host restarted with a new code: paste it in F9 > Partner / Co-op > Online') -and !$staleLine.Contains('the host left')) 'guest: the host restarted with a new code'
    Assert-True ((Format-CoopStatusLine -Status (ConvertFrom-CoopStatusText -Text $staleText.Replace('"verified":0', '"verified":1')) -Role guest -NowUtc $now).Contains(' - the host left; waiting for them to come back')) 'guest: a verified bad_code keeps the left clause'
    Assert-True ((Format-CoopStatusLine -Status (ConvertFrom-CoopStatusText -Text $staleText.Replace('"reason":"bad_code"', '"reason":"busy"')) -Role guest -NowUtc $now).Contains(' - the host left; waiting for them to come back')) 'guest: another reason keeps the left clause'
    $hostStale = Format-CoopStatusLine -Status (ConvertFrom-CoopStatusText -Text $staleText) -Role host -NowUtc $now
    Assert-True ($hostStale.Contains(' - your friend left; waiting for them to come back') -and !$hostStale.Contains('restarted with a new code')) 'host: the left clause is unchanged'
    # Mission names (the long table shared with native coop_text::FormatMission, K11).
    foreach ($row in @(@(0, 'a loading screen'), @(1, 'the start screen'), @(30010, 'Afghanistan free roam'), @(30020, 'Africa free roam'), @(30050, 'Mother Base'),
            @(40010, 'the Afghanistan helicopter'), @(40020, 'the Africa helicopter'), @(40050, 'the Mother Base helicopter'), @(10000, 'story mission 10000'),
            @(10020, 'story mission 10020'), @(11043, 'story mission 11043'), @(11999, 'story mission 11999'), @(12000, 'mission 12000'), @(12345, 'mission 12345'),
            @(9999, 'mission 9999'), @(4294967295, 'mission 4294967295'))) {
        Assert-Equal (Get-CoopMissionName -Mission $row[0]) $row[1] "mission name $($row[0])"
    }
    # The automatic save handoff in the status line (link-status "handoff"/"handoff_pct", P2-13;
    # absent = off): one segment before the mission, and one sentence per token change.
    foreach ($row in @(
            @('guest', 'wait_host', 0, " | waiting for the host's save"), @('guest', 'receiving', 40, " | getting the host's save 40%"),
            @('guest', 'staged', 100, ' | host save ready, MGSV restarts'), @('guest', 'countdown', 100, ' | host save ready, MGSV restarts'),
            @('guest', 'exiting', 100, ' | host save ready, MGSV restarts'), @('host', 'offered', 0, ' | sending your save to your friend 0%'),
            @('host', 'sending', 75, ' | sending your save to your friend 75%'), @('host', 'partner_staged', 100, ' | your friend is restarting on your save'),
            @('host', 'partner_restarting', 0, ' | your friend is restarting on your save'), @('guest', 'kept', 0, ' | save not shared: you kept your own save'),
            @('host', 'kept', 0, ' | save not shared: your friend kept their own save'), @('guest', 'failed', 0, ' | save not shared: the transfer failed'),
            @('host', 'failed', 0, ' | save not shared: the transfer failed'), @('guest', 'applied', 0, " | playing on the host's save"),
            @('guest', 'refused', 0, " | save not shared: the host's save does not continue into Mother Base or free roam yet"),
            @('host', 'refused', 0, ' | save not shared: your save does not continue into Mother Base or free roam yet'))) {
        $ht = Get-CoopHandoffText -Token $row[1] -Pct $row[2] -Role $row[0]
        Assert-Equal $ht.Segment $row[3] "handoff segment: $($row[0]) $($row[1])"
        Assert-True ([bool]$ht.Line -and $ht.Line -cnotmatch '[^\x20-\x7E]') "handoff sentence: $($row[0]) $($row[1])"
        $tag = '[JOIN]'
        if ($row[0] -eq 'host') { $tag = '[HOST]' }
        $handoffStatus = ConvertFrom-CoopStatusText -Text ($statusText.Replace(',"time_utc"', (',"handoff":"' + $row[1] + '","handoff_pct":' + $row[2] + ',"time_utc"')))
        Assert-Equal (Format-CoopStatusLine -Status $handoffStatus -Role $row[0] -NowUtc $now) ($tag + ' link=connected rtt=42ms jitter=3ms loss=0.4% interp=80ms | partner=live shown look' + $row[3] + ' | mission 30050') "status line with the handoff token $($row[1]) ($($row[0]))"
    }
    Assert-Equal (Get-CoopHandoffText -Token 'receiving' -Pct 250 -Role guest).Segment " | getting the host's save 100%" 'handoff percentage clamped to 100'
    Assert-True ((Get-CoopHandoffText -Token 'partner_restarting' -Role host).Line.StartsWith('Your friend is restarting MGSV on your save (about a minute).')) 'host sentence while the friend restarts'
    foreach ($quietToken in 'off', 'idle', 'same', 'something_new', '') {
        $ht = Get-CoopHandoffText -Token $quietToken -Role guest
        Assert-True ($ht.Segment -ceq '' -and $ht.Line -ceq '') "no handoff text for '$quietToken'"
        $quietStatus = ConvertFrom-CoopStatusText -Text ($statusText.Replace(',"time_utc"', (',"handoff":"' + $quietToken + '","handoff_pct":0,"time_utc"')))
        Assert-Equal (Format-CoopStatusLine -Status $quietStatus -Role host -NowUtc $now) (Format-CoopStatusLine -Status $st -Role host -NowUtc $now) "handoff '$quietToken' keeps the line without the keys"
    }
    # While the joiner waits for or gets the host's save, the Share-Save sentence would point the wrong way.
    foreach ($busy in 'wait_host', 'receiving', 'staged', 'countdown', 'exiting') {
        $busyStatus = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"mission":30050', '"mission":40010').Replace(',"time_utc"', (',"world":3,"paired":0,"partner_mission":0,"pair_wait":"no_hello","progress":"story_start","handoff":"' + $busy + '","handoff_pct":10,"time_utc"')))
        $busyLine = Format-CoopStatusLine -Status $busyStatus -Role guest -NowUtc $now
        Assert-True (!$busyLine.Contains('Share-Save.bat') -and $busyLine.EndsWith('| mission 40010')) "no Share-Save sentence while the handoff is $busy"
    }
    $keptStory = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"mission":30050', '"mission":40010').Replace(',"time_utc"', ',"world":3,"paired":0,"partner_mission":0,"pair_wait":"no_hello","progress":"story_start","handoff":"kept","handoff_pct":0,"time_utc"'))
    Assert-True ((Format-CoopStatusLine -Status $keptStory -Role guest -NowUtc $now).EndsWith('| save not shared: you kept your own save | mission 40010 Your co-op save is at the story start: quit, then import your partner''s save with Share-Save.bat (option I).')) 'a kept own save keeps the Share-Save sentence'
    # The P2-6 set hash (golden vectors computed independently; tests/fixtures/handoff-set.json
    # is the vector shared with the native tests).
    $slotRows = @(for ($i = 0; $i -lt 6; $i++) {
            [pscustomobject]@{ entry = (Get-CoopHandoffSlots)[$i]; bytes = 1000 * ($i + 1) + $i; sha256 = (Get-CoopTextSha256 -Bytes ([Text.Encoding]::ASCII.GetBytes("slot$i"))) }
        })
    Assert-Equal ((Get-CoopHandoffSlots) -join ',') '287700/local/TPP_GAME_DATA0,287700/local/TPP_GAME_DATA1,287700/local/PERSONAL_DATA0,287700/local/PERSONAL_DATA1,remote/TPP_GAME_DATA,remote/PERSONAL_DATA' 'the P2-6 slot table'
    Assert-Equal $slotRows[0].sha256 '4C4B4A1F341A258DB6343A420E19828162ACC54084240949ACA5A919C9100378' 'set hash vector: the file hash of slot 0'
    Assert-Equal (Get-CoopHandoffSetSha256 -Entries $slotRows) 'AB42AF5AF964DB2E1B9E273CF7802D6C0390078C545919B607D93CEC3CE7F2E5' 'set hash: all six slots'
    Assert-Equal (Get-CoopHandoffSetSha256 -Entries @($slotRows[5], $slotRows[1])) '8FE00587663109D28A030C7E74F1472B545798D6C6A31E58BEC2F3B4C981FD1D' 'set hash: slots 1 and 5, any input order'
    Assert-Equal (Get-CoopHandoffSetSha256 -Entries @([pscustomobject]@{ slot = 5; size = 6005; sha256 = $slotRows[5].sha256 }, [pscustomobject]@{ slot = 1; size = 2001; sha256 = $slotRows[1].sha256.ToLowerInvariant() })) '8FE00587663109D28A030C7E74F1472B545798D6C6A31E58BEC2F3B4C981FD1D' 'set hash: slot and size rows, lower-case hex'
    Assert-Throws { Get-CoopHandoffSetSha256 -Entries @([pscustomobject]@{ entry = '287700/local/TPP_GAME_DATA2'; bytes = 1; sha256 = $slotRows[0].sha256 }) } 'Not a save handoff file' 'set hash: a name outside the slot table'
    Assert-Throws { Get-CoopHandoffSetSha256 -Entries @($slotRows[0], $slotRows[0]) } 'Duplicate' 'set hash: a duplicate slot'
    Assert-Throws { Get-CoopHandoffSetSha256 -Entries @([pscustomobject]@{ entry = $slotRows[0].entry; bytes = 0; sha256 = $slotRows[0].sha256 }) } 'Invalid size' 'set hash: size 0'
    Assert-Throws { Get-CoopHandoffSetSha256 -Entries @([pscustomobject]@{ entry = $slotRows[0].entry; bytes = 4MB + 1; sha256 = $slotRows[0].sha256 }) } 'Invalid size' 'set hash: 4 MB + 1'
    Assert-Throws { Get-CoopHandoffSetSha256 -Entries @([pscustomobject]@{ entry = $slotRows[0].entry; bytes = 1; sha256 = 'XYZ' }) } 'Invalid SHA-256' 'set hash: a malformed hash'
    Assert-Throws { Get-CoopHandoffSetSha256 -Entries @() } 'No save handoff file' 'set hash: no file'
    $setFixture = Join-Path $projectRoot 'tests/fixtures/handoff-set.json'
    Assert-True (Test-Path -LiteralPath $setFixture -PathType Leaf) 'tests/fixtures/handoff-set.json exists'
    $setVector = Get-Content -LiteralPath $setFixture -Raw | ConvertFrom-Json
    $setRows = @(Get-FriendProperty $setVector 'entries' (Get-FriendProperty $setVector 'files' @()))
    $setExpected = [string](Get-FriendProperty $setVector 'set_sha256' (Get-FriendProperty $setVector 'expected' ''))
    Assert-True ($setRows.Count -eq 6 -and $setExpected -cmatch '^[0-9A-F]{64}$') 'tests/fixtures/handoff-set.json has six rows and an expected set_sha256'
    Assert-Equal (Get-CoopHandoffSetSha256 -Entries $setRows) $setExpected 'Get-CoopHandoffSetSha256 equals tests/fixtures/handoff-set.json'
    foreach ($subset in @(Get-FriendProperty $setVector 'subsets' @())) {
        $want = @(Get-FriendProperty $subset 'slots' @() | ForEach-Object { [int]$_ })
        $rows = @($setRows | Where-Object { $want -contains [int](Get-FriendProperty $_ 'slot' -1) })
        Assert-Equal (Get-CoopHandoffSetSha256 -Entries $rows) ([string](Get-FriendProperty $subset 'set_sha256' '')) "Get-CoopHandoffSetSha256 equals the fixture subset $($want -join ',')"
    }
    # Online play: how each PC shows the partner body (link-status.json partner_drive/_reason, remote_drive/_reason).
    $nativeStatus = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"saves":"redirected"', '"saves":"redirected","partner_drive":"native","partner_reason":"ok","remote_drive":"clips","remote_reason":"thread"'))
    Assert-Equal (Format-CoopStatusLine -Status $nativeStatus -Role host -NowUtc $now) ('[HOST] link=connected rtt=42ms jitter=3ms loss=0.4% interp=80ms | partner=live shown look native; you on the other PC: clips (' + (Get-CoopPartnerReasonText -Reason thread) + ') | mission 30050') 'status line: native here, clips on the other PC with the reason'
    $clipsStatus = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"saves":"redirected"', '"saves":"redirected","partner_drive":"clips","partner_reason":"net_quiet","remote_drive":"native","remote_reason":"ok"'))
    Assert-True ((Format-CoopStatusLine -Status $clipsStatus -Role guest -NowUtc $now).Contains('partner=live shown look clips (the other player is paused or loading); you on the other PC: native |')) 'status line: clips with the reason sentence'
    foreach ($reason in 'legacy', 'thread', 'cost', 'install_bytes', 'hook_lost', 'faulted', 'net_input', 'net_link', 'net_quiet', 'remote_quiet', 'net_quality',
            'net_flap', 'remote_caps', 'range_hidden', 'settling', 'terminating', 'diverging', 'health', 'something_new') {
        $sentence = Get-CoopPartnerReasonText -Reason $reason
        Assert-True ([bool]$sentence -and $sentence -cnotmatch '[^\x20-\x7E]') "partner reason sentence for $reason"
    }
    Assert-Equal (Get-CoopPartnerReasonText -Reason 'legacy') 'clip partner chosen (Play-Coop -Partner clips)' 'clips by choice'
    # A refused partner pack (Mother Base demo stage, hangar, story variant): no body, said plainly.
    $noBodyStatus = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"saves":"redirected"', '"saves":"redirected","partner_drive":"off","partner_reason":"no_body_pack","remote_drive":"native","remote_reason":"ok"'))
    $noBodyLine = Format-CoopStatusLine -Status $noBodyStatus -Role host -NowUtc $now
    Assert-True ($noBodyLine.Contains(' - no partner body in this scene (a Mother Base demo, the battle-gear hangar or a story variant;') -and
        !$noBodyLine.Contains('clips (') -and $noBodyLine.Contains('; you on the other PC: native |')) 'status line: no partner body in this scene instead of clips'
    Assert-True ((Get-CoopPartnerReasonText -Reason 'no_body_pack') -cnotmatch '[^\x20-\x7E]') 'no_body_pack sentence is ASCII'
    # Fresh cached owner status wins over the historical drive/gun snapshots.
    foreach ($entry in @(@('match', 'matched'), @('substitute', 'substitute'), @('standin', 'stand-in'), @('switching', 'switching'),
                         @('unavailable', 'unavailable'))) {
        $cached = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"saves":"redirected"',
            ('"saves":"redirected","partner_drive":"clips","partner_view":"native","partner_gun":"{0}","partner_gun_present":1,"partner_view_fresh":1' -f $entry[0])))
        Assert-True ((Format-CoopStatusLine -Status $cached -Role host -NowUtc $now).Contains(('look native; gun {0} |' -f $entry[1]))) 'launcher uses the current copied HUD gun status'
    }
    $staleCached = ConvertFrom-CoopStatusText -Text ($statusText.Replace('"saves":"redirected"',
        '"saves":"redirected","partner_drive":"native","partner_view":"native","partner_gun":"match","partner_gun_present":1,"partner_view_fresh":0'))
    Assert-True ((Format-CoopStatusLine -Status $staleCached -Role host -NowUtc $now).Contains('look loading; gun unavailable |')) 'stale cached owner status cannot advertise native/matched gun'
    $sparseCached = ConvertFrom-CoopStatusText -Text '{"v":1,"state":"connected","partner_view":"clips","partner_view_fresh":1}'
    Assert-True ((Format-CoopStatusLine -Status $sparseCached -Role guest -NowUtc $now).Contains('partner=none clips; gun unavailable |')) 'sparse cached view is StrictMode-safe'
    Assert-True ((Format-CoopStatusLine -Status $staleCached -Role host -NowUtc $now.AddSeconds(30)).StartsWith('[HOST] game not responding')) 'cache fields preserve file staleness error'
    # The reader shares ReadWrite|Delete, so the game's MoveFileExW(REPLACE_EXISTING) never fails because of it.
    $statusFile = Join-Path $fixture 'link-status.json'
    [IO.File]::WriteAllText($statusFile, $statusText)
    Assert-Equal (Read-CoopStatusFile -Path $statusFile -ExpectedPid 4242).rtt_ms 42 'Read-CoopStatusFile'
    Assert-True ($null -eq (Read-CoopStatusFile -Path (Join-Path $fixture 'absent.json'))) 'absent status file'
    # A writer that holds the file (write access, sharing read and delete) does not block the reader.
    $held = [IO.FileStream]::new($statusFile, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::Read -bor [IO.FileShare]::Delete)
    try { Assert-Equal (Read-CoopStatusFile -Path $statusFile).rtt_ms 42 'status read while the writer holds the file' } finally { $held.Dispose() }
    # The reader keeps no handle open, so the next atomic replace succeeds right after a read.
    $null = Read-CoopStatusFile -Path $statusFile
    $tmp = $statusFile + '.tmp'
    [IO.File]::WriteAllText($tmp, $statusText.Replace('"rtt_ms":42', '"rtt_ms":43'))
    [IO.File]::Move($tmp, $statusFile, $true)
    Assert-Equal (Read-CoopStatusFile -Path $statusFile).rtt_ms 43 'atomic replace succeeds right after a read'
    $coopPlayText = [IO.File]::ReadAllText((Join-Path $projectRoot 'tools/CoopPlay.psm1'))
    Assert-True ($coopPlayText.Contains('return ConvertFrom-CoopStatusText -Text (Read-SharedText -Path $Path)') -and $coopPlayText.Contains('[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete')) 'Read-CoopStatusFile reads through the ReadWrite|Delete share'
    $deltaLog = Join-Path $fixture 'early.log'
    [IO.File]::WriteAllText($deltaLog, "early=start; old`r`nsaves=redirected; dir=C:\old`r`nplay=1; loopback=0; mutex=unchanged; refused=0x0; saves=redirected`r`n")
    $offset = Get-FileLengthOrZero $deltaLog
    [IO.File]::AppendAllText($deltaLog, "early=start; new`r`nsaves=redirected; dir=$saveDir`r`npartial")
    $d = Read-CoopLogDelta -Path $deltaLog -Offset $offset
    Assert-True ($d.Text.Contains("dir=$saveDir") -and !$d.Text.Contains('C:\old') -and !$d.Text.Contains('partial')) 'log delta: only the new complete lines'
    $ev = Get-CoopLaunchEvidence -EarlyLog $d.Text -SaveDir $saveDir
    Assert-True ($ev.State -eq 'pending' -and @($ev.Missing).Count -eq 3 -and (@($ev.Missing) -join '|').Contains('early.log play=1')) 'evidence from an earlier session never counts: the old play line is outside the delta'
    $goodEarly = "early=start; version=0.12.0`r`nsaves=redirected; dir=$saveDir`r`nplay=1; loopback=0; mutex=unchanged; refused=0x0; saves=redirected`r`n"
    $goodSteam = "master_server=redirected; method=x; url=http://127.0.0.1:47899/tppstm/gate`r`n"
    $goodObserver = "mode=offline_v1; x`r`nplay=active; role=host; partner=1; refused=0x0`r`n"
    Assert-Equal (Get-CoopLaunchEvidence -EarlyLog $goodEarly -SteamLog $goodSteam -ObserverLog $goodObserver -SaveDir $saveDir).State 'ok' 'evidence ok'
    Assert-Equal (Get-CoopLaunchEvidence -EarlyLog $goodEarly -SteamLog $goodSteam -ObserverLog "play=refused; reason=saves_not_redirected; refused=0x0`r`n" -SaveDir $saveDir).State 'unsafe' 'play=refused: red banner'
    Assert-Equal (Get-CoopLaunchEvidence -EarlyLog "saves=unchanged`r`n" -SaveDir $saveDir).State 'unsafe' 'saves=unchanged: red banner at once'
    Assert-Equal (Get-CoopLaunchEvidence -EarlyLog "saves=redirected; dir=C:\elsewhere`r`n" -SaveDir $saveDir).State 'unsafe' 'saves redirected elsewhere: red banner'
    Assert-Equal (Get-CoopLaunchEvidence -EarlyLog '' -SteamLog $goodSteam -ObserverLog $goodObserver -SaveDir $saveDir -Final).State 'unsafe' 'no saves= line at the deadline: red banner'
    $old = Get-CoopLaunchEvidence -EarlyLog "early=start; version=0.11.1`r`nsaves=redirected; dir=$saveDir`r`n" -SteamLog $goodSteam -SaveDir $saveDir -Final
    Assert-True ($old.State -eq 'old_loader' -and (@($old.Missing) -join '|').Contains('script-observer.log play=active')) 'saves redirected, no play line at the deadline: a pre-0.12 loader'
    $refusedBits = Get-CoopLaunchEvidence -EarlyLog $goodEarly.Replace('refused=0x0', 'refused=0x4000') -SteamLog $goodSteam -ObserverLog $goodObserver -SaveDir $saveDir -Final
    Assert-True ($refusedBits.State -eq 'incomplete' -and (@($refusedBits.Problems) -join ' ').Contains('refused=0x4000')) 'refused play settings reported'
    $noMaster = Get-CoopLaunchEvidence -EarlyLog $goodEarly -ObserverLog $goodObserver -SaveDir $saveDir -Final
    Assert-True ($noMaster.State -eq 'incomplete' -and @($noMaster.Missing).Count -eq 1 -and $noMaster.Missing[0].StartsWith('steam-observer.log')) 'each missing piece is reported separately'
    Assert-Equal (Get-CoopLaunchEvidence -EarlyLog $goodEarly -SteamLog "master_server=redirected; method=x; url=https://mgstpp-game.konamionline.com/tppstm/gate`r`n" -ObserverLog $goodObserver -SaveDir $saveDir).State 'incomplete' 'a master URL other than the closed port is reported'

    # ------------------------------------------------------------ Steam library discovery
    $vdf = @'
"libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"apps"
		{
			"287700"		"25000000000"
		}
	}
	"1"
	{
		"path"		"E:\\SteamLibrary"
	}
}
'@
    Assert-Equal ((Get-SteamLibraryPathsFromVdf $vdf) -join '|') 'C:\Program Files (x86)\Steam|E:\SteamLibrary' 'libraryfolders.vdf (current format)'
    Assert-Equal (Get-MgsvInstallDir "`"AppState`"`n{`n`t`"installdir`"`t`t`"MGS_TPP_custom`"`n}") 'MGS_TPP_custom' 'appmanifest installdir'
    Assert-Equal (Get-MgsvInstallDir '') 'MGS_TPP' 'default installdir'
    $tail = Limit-TextTail -Text (("line`n") * 100) -MaxChars 50
    Assert-True ($tail.StartsWith('[... ') -and $tail.Length -lt 100) 'tail limited with a note'
    Assert-Equal (Get-TextFromLastMarker -Text "x`nMGS Co-op script adapter 0.12.0; b`ny`n" -Marker 'MGS Co-op script adapter') "MGS Co-op script adapter 0.12.0; b`ny`n" 'from the last adapter start'
    $sessions = "partial`nMGS Co-op script adapter A`na`nMGS Co-op script adapter B`nb`nMGS Co-op script adapter C`nc`nMGS Co-op script adapter D`nd`n"
    Assert-Equal (Get-TextFromLastMarkers -Text $sessions -Marker 'MGS Co-op script adapter' -Count 3) "MGS Co-op script adapter B`nb`nMGS Co-op script adapter C`nc`nMGS Co-op script adapter D`nd`n" 'from the 3rd-last adapter start'
    Assert-Equal (Get-TextFromLastMarkers -Text $sessions -Marker 'MGS Co-op script adapter' -Count 1) (Get-TextFromLastMarker -Text $sessions -Marker 'MGS Co-op script adapter') 'Count 1 is the last start'
    Assert-Equal (Get-TextFromLastMarkers -Text $sessions -Marker 'MGS Co-op script adapter' -Count 5) $sessions 'fewer starts than asked: the whole text'
    Assert-Equal (Get-TextFromLastMarkers -Text "x`nMGS Co-op script adapter; MGS Co-op script adapter`ny`n" -Marker 'MGS Co-op script adapter' -Count 2) "x`nMGS Co-op script adapter; MGS Co-op script adapter`ny`n" 'two matches on one line count once'
    Assert-Equal (Get-TextFromLastMarkers -Text "MGS Co-op script adapter A`na`nMGS Co-op script adapter B`n" -Marker 'MGS Co-op script adapter' -Count 2) "MGS Co-op script adapter A`na`nMGS Co-op script adapter B`n" 'a start on the first line'
    Assert-Equal (Get-TextFromLastMarkers -Text '' -Marker 'MGS Co-op script adapter' -Count 3) '' 'empty text'
    # Share-Save import detection (K18): a valid marker wins, else the newest set-aside folder by name.
    $infoRoot = Join-Path $fixture 'import-info'
    $infoSaves = Join-Path $infoRoot 'coop-saves'
    $infoReplaced = Join-Path $infoRoot 'coop-saves-replaced'
    $null = New-Item -ItemType Directory -Path $infoSaves -Force
    Assert-True ($null -eq (Get-FriendImportedSaveInfo -SaveDir $infoSaves -ReplacedRoot $infoReplaced)) 'no marker, no set-aside folder: not imported'
    $null = New-Item -ItemType Directory -Path (Join-Path $infoReplaced '20261009-171000-before-import'), (Join-Path $infoReplaced '20261015-155533'), (Join-Path $infoReplaced 'notes')
    Assert-True ($null -eq (Get-FriendImportedSaveInfo -SaveDir $infoSaves -ReplacedRoot $infoReplaced)) 'a later Install-Coop reseed: not imported'
    $null = New-Item -ItemType Directory -Path (Join-Path $infoReplaced '20261016-090000-before-import')
    $info = Get-FriendImportedSaveInfo -SaveDir $infoSaves -ReplacedRoot $infoReplaced
    Assert-Equal "$($info.Source)|$($info.Imported)|$([IO.Path]::GetFileName($info.Folder))" 'folder|2026-10-16 09:00|20261016-090000-before-import' 'the newest folder is an import'
    $null = New-Item -ItemType Directory -Path (Join-Path $infoReplaced '20261016-093000-before-handoff')
    Assert-Equal (Get-FriendImportedSaveInfo -SaveDir $infoSaves -ReplacedRoot $infoReplaced).Source 'folder' 'a handoff counts as an import (Phase 2)'
    $null = New-Item -ItemType Directory -Path (Join-Path $infoReplaced '20261016-100000-before-restore')
    Assert-True ($null -eq (Get-FriendImportedSaveInfo -SaveDir $infoSaves -ReplacedRoot $infoReplaced)) 'after a Share-Save [R]: not imported'
    $importedAt = [DateTime]::new(2026, 10, 16, 8, 30, 0, [DateTimeKind]::Utc)
    $markerText = New-FriendImportMarkerText -Source 'share-save' -ExportedUtc '2026-10-16T08:00:00.0000000Z' -Files @([pscustomobject]@{ entry = '287700/local/TPP_GAME_DATA0'; bytes = 42; sha256 = 'abc' }) -ImportedUtc $importedAt
    $markerJson = $markerText | ConvertFrom-Json
    $markerExported = $markerJson.exported_utc
    if ($markerExported -is [DateTime]) { $markerExported = $markerExported.ToUniversalTime().ToString('o') }
    Assert-Equal "$($markerJson.format)|$($markerJson.v)|$($markerJson.source)|$markerExported|$(@($markerJson.files).Count)|$($markerJson.files[0].entry)|$($markerJson.files[0].bytes)|$($markerJson.files[0].sha256)" 'mgs-coop-imported-progress|1|share-save|2026-10-16T08:00:00.0000000Z|1|287700/local/TPP_GAME_DATA0|42|ABC' 'import marker fields (K18)'
    Assert-True ($markerText.Contains('"imported_utc":') -and $markerText.Contains('2026-10-16T08:30:00Z') -and $markerText -cnotmatch '[^\x00-\x7F]') 'import marker time and ASCII'
    [IO.File]::WriteAllText((Join-Path $infoSaves 'imported-progress.json'), $markerText)
    $info = Get-FriendImportedSaveInfo -SaveDir $infoSaves -ReplacedRoot $infoReplaced
    Assert-Equal "$($info.Source)|$($info.Imported)|$([IO.Path]::GetFileName($info.MarkerPath))" ('marker|' + $importedAt.ToLocalTime().ToString('yyyy-MM-dd HH:mm') + '|imported-progress.json') 'a valid marker wins'
    [IO.File]::WriteAllText((Join-Path $infoSaves 'imported-progress.json'), '{"format":"something-else","v":1}')
    Assert-True ($null -eq (Get-FriendImportedSaveInfo -SaveDir $infoSaves -ReplacedRoot $infoReplaced)) 'an invalid marker falls back to the folders'
    Assert-Equal $info.Kind 'share-save' 'a Share-Save marker is an import'
    [IO.File]::WriteAllText((Join-Path $infoSaves 'imported-progress.json'), (New-FriendImportMarkerText -Source 'handoff' -Files @()))
    Assert-Equal "$((Get-FriendImportedSaveInfo -SaveDir $infoSaves -ReplacedRoot $infoReplaced).Source)|$((Get-FriendImportedSaveInfo -SaveDir $infoSaves -ReplacedRoot $infoReplaced).Kind)" 'marker|handoff' 'a handoff marker names the save handoff'
    Remove-Item -LiteralPath (Join-Path $infoSaves 'imported-progress.json')
    $null = New-Item -ItemType Directory -Path (Join-Path $infoReplaced '20261016-110000-before-handoff')
    Assert-Equal "$((Get-FriendImportedSaveInfo -SaveDir $infoSaves -ReplacedRoot $infoReplaced).Source)|$((Get-FriendImportedSaveInfo -SaveDir $infoSaves -ReplacedRoot $infoReplaced).Kind)" 'folder|handoff' 'a newest -before-handoff folder names the save handoff'
    # The copies Share-Save [I] and the save handoff moved aside, newest first (Share-Save [R]).
    $orderRoot = Join-Path $fixture 'backups-order'
    foreach ($name in '20261016-090000-before-import', '20261016-090000-1-before-import', '20261016-093000-before-handoff', '20261015-235959-before-handoff', '20261017-000000-before-restore', 'notes', '20261018-000000') {
        $null = New-Item -ItemType Directory -Path (Join-Path $orderRoot $name) -Force
    }
    Assert-Equal ((@(Get-CoopProgressBackups -ReplacedRoot $orderRoot) | ForEach-Object { "$($_.Name)/$($_.Kind)" }) -join ',') '20261016-093000-before-handoff/handoff,20261016-090000-1-before-import/import,20261016-090000-before-import/import,20261015-235959-before-handoff/handoff' 'progress backups: both suffixes, newest first'
    Assert-Equal @(Get-CoopProgressBackups -ReplacedRoot (Join-Path $fixture 'no-such-replaced')).Count 0 'no replaced folder: no backups'
    # Test-CoopProgressStage (P2-18): a staged progress tree, alone or against its file list.
    $stageChecks = Join-Path $fixture 'stage-checks'
    function New-StageFixture([string]$Name, [hashtable]$Files) {
        $dir = Join-Path $stageChecks $Name
        Assert-InFixture $dir
        foreach ($k in $Files.Keys) {
            $p = Join-Path $dir $k
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $p) -Force
            [IO.File]::WriteAllText($p, [string]$Files[$k])
        }
        return $dir
    }
    $goodStage = New-StageFixture 'good' @{ '287700\local\TPP_GAME_DATA0' = 'host progress 0'; '287700\local\PERSONAL_DATA0' = 'host personal 0'; 'remote\TPP_GAME_DATA' = 'host remote'; 'handoff.json' = '{}' }
    $goodCheck = Test-CoopProgressStage -StageRoot $goodStage
    Assert-True ($goodCheck.Ok -and $goodCheck.Bytes -eq 41) "a good stage passes (handoff.json at its root is allowed): $($goodCheck.Problem)"
    Assert-Equal ((@($goodCheck.Files) | ForEach-Object { $_.entry }) -join ',') '287700/local/TPP_GAME_DATA0,287700/local/PERSONAL_DATA0,remote/TPP_GAME_DATA' 'stage files in slot order'
    $goodList = @($goodCheck.Files | ForEach-Object { [pscustomobject]@{ entry = $_.entry; bytes = $_.bytes; sha256 = $_.sha256 } })
    Assert-True (Test-CoopProgressStage -StageRoot $goodStage -Manifest $goodList).Ok 'a stage equal to its file list passes'
    $badList = @($goodList | ForEach-Object { [pscustomobject]@{ entry = $_.entry; bytes = $_.bytes; sha256 = $_.sha256 } })
    $badList[1].sha256 = '0' * 64
    Assert-True ((Test-CoopProgressStage -StageRoot $goodStage -Manifest $badList).Problem -ceq '287700/local/PERSONAL_DATA0 is damaged (hash mismatch).') 'stage: a hash mismatch'
    Assert-True ((Test-CoopProgressStage -StageRoot $goodStage -Manifest @($goodList[0], $goodList[2])).Problem -ceq "The save's file list does not name 287700/local/PERSONAL_DATA0.") 'stage: a file the list does not name (set mismatch)'
    Assert-True ((Test-CoopProgressStage -StageRoot $goodStage -Manifest (@($goodList) + @([pscustomobject]@{ entry = 'remote/PERSONAL_DATA'; bytes = 1; sha256 = '0' * 64 }))).Problem -ceq 'remote/PERSONAL_DATA is missing from the save.') 'stage: a listed file is missing (set mismatch)'
    $sizeList = @($goodList | ForEach-Object { [pscustomobject]@{ entry = $_.entry; bytes = $_.bytes; sha256 = $_.sha256 } })
    $sizeList[0].bytes = 99
    Assert-True ((Test-CoopProgressStage -StageRoot $goodStage -Manifest $sizeList).Problem.Contains('another size')) 'stage: a size mismatch'
    foreach ($case in @(
            @('bad-name', @{ '287700\local\TPP_GAME_DATA0' = 'x'; '287700\local\TPP_CONFIG_DATA' = 'settings' }, 'Unexpected file in the save: 287700/local/TPP_CONFIG_DATA.'),
            @('lower-case', @{ '287700\local\TPP_GAME_DATA0' = 'x'; 'remote\tpp_game_data' = 'y' }, 'Unexpected file in the save: remote/tpp_game_data.'),
            @('root-file', @{ '287700\local\TPP_GAME_DATA0' = 'x'; 'notes.txt' = 'n' }, 'Unexpected file in the save: notes.txt.'),
            @('other-dir', @{ '287700\local\TPP_GAME_DATA0' = 'x'; '287700\remote\TPP_GAME_DATA' = 'y' }, 'Unexpected file in the save: 287700/remote.'),
            @('empty-file', @{ '287700\local\TPP_GAME_DATA0' = '' }, 'Unexpected save file size: 287700/local/TPP_GAME_DATA0.'),
            @('no-game-data', @{ '287700\local\PERSONAL_DATA0' = 'p'; 'remote\TPP_GAME_DATA' = 'r' }, 'The save has no TPP_GAME_DATA file.'))) {
        $dir = New-StageFixture $case[0] $case[1]
        $check = Test-CoopProgressStage -StageRoot $dir
        Assert-True (!$check.Ok -and $check.Problem -ceq $case[2]) "stage refused ($($case[0])): $($check.Problem)"
    }
    $bigStage = New-StageFixture 'oversize' @{ '287700\local\TPP_GAME_DATA0' = 'x' }
    $bigStream = [IO.File]::Create((Join-Path $bigStage '287700\local\TPP_GAME_DATA1'))
    try { $bigStream.SetLength(4MB + 1) } finally { $bigStream.Dispose() }
    Assert-True ((Test-CoopProgressStage -StageRoot $bigStage).Problem -ceq 'Unexpected save file size: 287700/local/TPP_GAME_DATA1.') 'stage refused: a file over 4 MB'
    Assert-True ((Test-CoopProgressStage -StageRoot (Join-Path $stageChecks 'absent')).Problem -ceq 'The save to import is missing.') 'stage refused: no folder'
    $linkStage = New-StageFixture 'linked' @{ '287700\local\TPP_GAME_DATA0' = 'x' }
    $linkTarget = New-StageFixture 'link-target' @{ 'TPP_GAME_DATA' = 'outside' }
    $stageLink = Join-Path $linkStage 'remote'
    try {
        $null = New-Item -ItemType Junction -Path $stageLink -Target $linkTarget
        Assert-True ((Test-CoopProgressStage -StageRoot $linkStage).Problem -ceq 'Filesystem links are not supported here: remote.') 'stage refused: a filesystem link'
        Assert-True (!(Remove-CoopProgressStage -StageRoot $linkStage) -and (Test-Path -LiteralPath (Join-Path $linkTarget 'TPP_GAME_DATA'))) 'stage removal never follows a link'
    } finally {
        if (Test-Path -LiteralPath $stageLink) { [IO.Directory]::Delete($stageLink) }
    }
    # Removal by known names: the stage files, handoff.json and the loader's temp copies go; anything else stays with its folders.
    $tempName = 'TPP_GAME_DATA0.mgs-coop-' + ('0123456789abcdef' * 2) + '.tmp'
    $removeStage = New-StageFixture 'remove' @{ '287700\local\TPP_GAME_DATA0' = 'x'; ('287700\local\' + $tempName) = 't'; 'remote\TPP_GAME_DATA' = 'r'; 'handoff.json' = '{}' }
    Assert-True ((Remove-CoopProgressStage -StageRoot $removeStage) -and !(Test-Path -LiteralPath $removeStage)) 'a stage is removed by its known names'
    $keepStage = New-StageFixture 'keep' @{ '287700\local\TPP_GAME_DATA0' = 'x'; 'remote\NOTES.txt' = 'not ours' }
    Assert-True (!(Remove-CoopProgressStage -StageRoot $keepStage) -and (Test-Path -LiteralPath (Join-Path $keepStage 'remote\NOTES.txt')) -and !(Test-Path -LiteralPath (Join-Path $keepStage '287700'))) 'an unknown file stays with its folders'

    # ------------------------------------------------------------ firewall report and elevated command (pure)
    $report = Format-CoopFirewallReport -Rules @(
        [pscustomobject]@{ Name = '{b}'; DisplayName = 'mgsvtpp blocked'; Direction = 'Inbound'; Action = 'Block'; Enabled = 'True'; Profile = 'Public' },
        [pscustomobject]@{ Name = '{a}'; DisplayName = 'METAL GEAR'; Direction = 'Inbound'; Action = 'Allow'; Enabled = 'True'; Profile = 'Private' }) `
        -Profiles @([pscustomobject]@{ Name = 'Home'; Alias = 'Ethernet'; Category = 'Public' }) -Port 47830
    $reportText = $report -join "`n"
    Assert-True ($reportText.Contains('Public') -and $reportText.Contains('BLOCK rule') -and $reportText.Contains('Block rules win over Allow rules') -and $reportText.Contains('covers Private networks only')) 'firewall report: Block rules and the Public profile explained'
    Assert-True ((Format-CoopFirewallReport -Rules @() -Profiles @()) -join ' ').Contains('No Windows Firewall rule') 'firewall report without rules'
    $quoteExe = "C:\Games\O'Brien`$x\MGS_TPP\mgsvtpp.exe"
    $fwScript = New-CoopFirewallScript -Port 47830 -ExePath $quoteExe -RemoveRuleNames @('{b}') -DisplayName 'mgs-coop play UDP 47830'
    Assert-True ($fwScript.Contains("-Program 'C:\Games\O''Brien`$x\MGS_TPP\mgsvtpp.exe'") -and $fwScript.Contains('-Profile Any') -and $fwScript.Contains('-Protocol UDP -LocalPort 47830') -and $fwScript.Contains("Remove-NetFirewallRule -Name '{b}'")) 'firewall script: quoted path, Profile Any, Block rules removed first'
    Assert-True ($fwScript.IndexOf('Remove-NetFirewallRule -Name') -lt $fwScript.IndexOf('New-NetFirewallRule')) 'firewall script removes the Block rules before adding the Allow rule'
    $fixedScript = New-CoopFirewallScript -Port 47900 -ExePath $quoteExe -DisplayName 'mgs-coop play'
    Assert-True ($fixedScript.Contains("Get-NetFirewallRule -DisplayName 'mgs-coop play' -ErrorAction SilentlyContinue | Remove-NetFirewallRule") -and
        $fixedScript.IndexOf('Remove-NetFirewallRule') -lt $fixedScript.IndexOf('New-NetFirewallRule')) 'the [F] script replaces the rule of the same name'
    $tokens = $null; $errors = $null
    $null = [System.Management.Automation.Language.Parser]::ParseInput($fwScript, [ref]$tokens, [ref]$errors)
    Assert-Equal @($errors).Count 0 'firewall script parses (a quote in the path cannot break it)'
    Assert-Equal ([Text.Encoding]::Unicode.GetString([Convert]::FromBase64String((ConvertTo-CoopEncodedCommand -Script $fwScript)))) $fwScript 'the elevated command is passed with -EncodedCommand (UTF-16LE base64)'
    $friendCommonText = [IO.File]::ReadAllText((Join-Path $projectRoot 'tools/dev/FriendCommon.psm1'))
    Assert-True ($friendCommonText.Contains("'-EncodedCommand', (ConvertTo-CoopEncodedCommand -Script `$Script)") -and $friendCommonText.Contains('-Verb RunAs')) 'Invoke-CoopElevated uses RunAs with -EncodedCommand'
    Assert-True ($friendCommonText.Contains('Start-Job') -and $friendCommonText.Contains('Wait-Job -Job $job -Timeout $TimeoutSec') -and $friendCommonText.Contains("if (`$null -eq `$maps) { return 'unavailable|' }")) 'UPnP runs in a job with a timeout and handles a missing mapping collection'

    # ------------------------------------------------------------ the Diamond Dogs Together package (tools/package.ps1 -Friend)
    # The drop-in zip: its root is what goes into the game folder. Inert loader and
    # launcher bytes with a synthetic passing Release receipt; nothing is built,
    # installed or started.
    $fakeLoader = Join-Path $fixture 'fake-loader.dll'
    [IO.File]::WriteAllText($fakeLoader, 'INERT loader stand-in for the package test')
    $fakeLauncher = Join-Path $fixture 'fake-launcher.exe'
    [IO.File]::WriteAllText($fakeLauncher, 'INERT launcher stand-in for the package test; never executed')
    # Actual shared snapshot policy on small inert source trees: no check runner,
    # build, installer, native fixture or game process is invoked.
    $sourceFx = Join-Path $fixture 'source-snapshot'
    $null = New-Item -ItemType Directory -Path (Join-Path $sourceFx 'native'), (Join-Path $sourceFx 'third_party\minhook') -Force
    [IO.File]::WriteAllText((Join-Path $sourceFx 'CMakeLists.txt'), '# inert source fixture')
    $sourceFile = Join-Path $sourceFx 'native\fixture.cpp'
    [IO.File]::WriteAllText($sourceFile, '// checked source')
    [IO.File]::WriteAllText((Join-Path $sourceFx 'third_party\minhook\CMakeLists.txt'), '# inert dependency fixture')
    $sourceSnap = Get-WorkerSourceHashes $sourceFx
    $sourceReceipt = [ordered]@{ source_hashes=$sourceSnap.hashes; source_dependencies=$sourceSnap.dependencies }
    Assert-WorkerReceiptSource ($sourceReceipt | ConvertTo-Json -Depth 8 | ConvertFrom-Json -AsHashtable) (Get-WorkerSourceHashes $sourceFx)
    Assert-True $true 'serialized matching receipt source snapshot admitted'
    Assert-Throws { Assert-WorkerReceiptSource @{} $sourceSnap } 'no complete source snapshot' 'missing source snapshot refused'
    Assert-Throws { Assert-WorkerReceiptSource @{source_hashes=@{};source_dependencies=$sourceSnap.dependencies} $sourceSnap } 'no complete source snapshot' 'empty source snapshot refused'
    Assert-Throws { Assert-WorkerReceiptSource @{source_hashes=$sourceSnap.hashes} $sourceSnap } 'no complete source snapshot' 'missing dependency snapshot refused'
    Assert-Throws { Assert-WorkerReceiptSource @{source_hashes=@{'CMakeLists.txt'='not-a-hash'};source_dependencies=$sourceSnap.dependencies} $sourceSnap } 'invalid source snapshot' 'malformed source hash refused'
    [IO.File]::WriteAllText($sourceFile, '// untested source')
    Assert-Throws { Assert-WorkerReceiptSource $sourceReceipt (Get-WorkerSourceHashes $sourceFx) } 'Source changed' 'changed source content refused'
    [IO.File]::WriteAllText($sourceFile, '// checked source')
    $addedSource = Join-Path $sourceFx 'native\added.cpp'
    [IO.File]::WriteAllText($addedSource, '// new untested source')
    Assert-Throws { Assert-WorkerReceiptSource $sourceReceipt (Get-WorkerSourceHashes $sourceFx) } 'Source file set changed' 'added source refused'
    Remove-Item -LiteralPath $addedSource
    # The launcher's embedded assets are compiled into the exe: the snapshot covers them.
    $assetFile = Join-Path $sourceFx 'native\launcher\assets\fixture.fpkd.bin'
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $assetFile) -Force
    [IO.File]::WriteAllBytes($assetFile, [byte[]](1, 2, 3))
    Assert-Throws { Assert-WorkerReceiptSource $sourceReceipt (Get-WorkerSourceHashes $sourceFx) } 'Source file set changed' 'an embedded launcher asset is part of the checked source'
    Remove-Item -LiteralPath (Join-Path $sourceFx 'native\launcher') -Recurse
    # So are the .inc fragments that native sources #include into the loader.
    $incFile = Join-Path $sourceFx 'native\fixture_impl.inc'
    [IO.File]::WriteAllText($incFile, '// #included loader fragment')
    Assert-Throws { Assert-WorkerReceiptSource $sourceReceipt (Get-WorkerSourceHashes $sourceFx) } 'Source file set changed' 'an #included .inc loader fragment is part of the checked source'
    Remove-Item -LiteralPath $incFile
    Remove-Item -LiteralPath $sourceFile
    Assert-Throws { Assert-WorkerReceiptSource $sourceReceipt (Get-WorkerSourceHashes $sourceFx) } 'Source file set changed' 'removed source refused'
    [IO.File]::WriteAllText($sourceFile, '// checked source')
    $changedDependency = $sourceReceipt | ConvertTo-Json -Depth 8 | ConvertFrom-Json -AsHashtable
    $changedDependency.source_dependencies['third_party\minhook'].resolved_path += '-other'
    Assert-Throws { Assert-WorkerReceiptSource $changedDependency (Get-WorkerSourceHashes $sourceFx) } 'dependency target changed' 'changed dependency target refused with same source hashes'

    # Receipt admission: the loader AND the launcher come from one passing Release run.
    $packageScript = Join-Path $projectRoot 'tools\package.ps1'
    $distFx = Join-Path $fixture 'dist'
    Assert-Throws { & $packageScript -Friend -LoaderPath $fakeLoader -OutputRoot $distFx } 'requires a passing Release check receipt' 'friend loader override cannot bypass the receipt'
    Assert-Throws { & $packageScript -Friend -LauncherPath $fakeLauncher -OutputRoot $distFx } 'LauncherPath requires a passing Release check receipt' 'friend launcher override cannot bypass the receipt'
    Assert-True (-not (Test-Path -LiteralPath $distFx)) 'missing receipt refuses before staging'
    $receiptFx = Join-Path $fixture 'release-receipt'
    $null = New-Item -ItemType Directory -Path $receiptFx
    [IO.File]::Copy($fakeLoader, (Join-Path $receiptFx 'dinput8.dll'))
    [IO.File]::Copy($fakeLauncher, (Join-Path $receiptFx 'DiamondDogsTogether.exe'))
    $releaseFlags = [ordered]@{}
    foreach ($name in @('MGS_LOCAL_BUDDY_CAPACITY_TEST','MGS_LOCAL_GUN_PRELOAD_TEST','MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST',
            'MGS_LOCAL_STOCK46_AMMO_TEST','MGS_LOCAL_ACTION_EVENTS','MGS_LOCAL_UPPER_END_OBSERVE','MGS_LOCAL_UPPER_PHASE_OBSERVE',
            'MGS_LOCAL_UPPER_MANUAL_IDLE','MGS_LOCAL_UPPER_PROBE','MGS_LOCAL_UPPER_CHANNEL2','MGS_LOCAL_UPPER_FIRE_ZERO_BLEND',
            'MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER','MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER','MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER',
            'MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER','MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER')) {
        $releaseFlags[$name] = if ($name -in @('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST')) { 'ON' } else { 'OFF' }
    }
    $releaseFlags['MGS_BACKGROUND_WORKER'] = 'OFF'
    $currentSource = Get-WorkerSourceHashes $projectRoot
    # The .inc fragments native/*.cpp #include are loader source: the Release snapshot binds every one.
    $nativeRoot = Join-Path $projectRoot 'native'
    $incSources = @(Get-ChildItem -LiteralPath $nativeRoot -Recurse -File -Filter '*.inc' | Where-Object { $_.Extension -ceq '.inc' } |
        ForEach-Object { 'native/' + [IO.Path]::GetRelativePath($nativeRoot, $_.FullName).Replace('\', '/') } | Sort-Object)
    Assert-True ($incSources -ccontains 'native/partner_driver_seat.inc' -and $incSources -ccontains 'native/native_pose_window_impl.inc') 'the loader has #included .inc sources'
    foreach ($relative in $incSources) { Assert-True ($currentSource.hashes.Contains($relative.Replace('/', '\'))) "the Release source snapshot binds $relative" }
    function New-FixtureReceipt([string]$Directory, [string]$Name, $Artifacts, $Source = $currentSource) {
        $receipt = [ordered]@{schema=1;status='passed';plan=[ordered]@{profile='Release';flags=$releaseFlags;tests=@('inert-fixture-only')};
            artifacts=$Artifacts;source_hashes=$Source.hashes;source_dependencies=$Source.dependencies}
        $path = Join-Path $Directory $Name
        [IO.File]::WriteAllText($path, ($receipt | ConvertTo-Json -Depth 12), $utf8)
        return $path
    }
    $bothArtifacts = [ordered]@{loader_sha256=(Get-Sha256 $fakeLoader);launcher_sha256=(Get-Sha256 $fakeLauncher)}
    $receiptPath = New-FixtureReceipt $receiptFx 'receipt.json' $bothArtifacts
    $wrongLoader = Join-Path $fixture 'wrong-loader.dll'
    [IO.File]::WriteAllText($wrongLoader, 'OTHER inert loader')
    Assert-Throws { & $packageScript -Friend -LoaderPath $wrongLoader -CheckReceipt $receiptPath -OutputRoot $distFx } 'does not match the Release receipt loader' 'friend loader override hash mismatch refused'
    $wrongLauncher = Join-Path $fixture 'wrong-launcher.exe'
    [IO.File]::WriteAllText($wrongLauncher, 'OTHER inert launcher')
    Assert-Throws { & $packageScript -Friend -LauncherPath $wrongLauncher -CheckReceipt $receiptPath -OutputRoot $distFx } 'does not match the Release receipt launcher' 'friend launcher override hash mismatch refused'
    $noLauncher = New-FixtureReceipt $receiptFx 'no-launcher.json' ([ordered]@{loader_sha256=(Get-Sha256 $fakeLoader)})
    Assert-Throws { & $packageScript -Friend -CheckReceipt $noLauncher -OutputRoot $distFx } 'has no launcher' 'a receipt from before the launcher (loader only) is refused'
    $otherFx = Join-Path $fixture 'release-receipt-changed'
    $null = New-Item -ItemType Directory -Path $otherFx
    [IO.File]::Copy($fakeLoader, (Join-Path $otherFx 'dinput8.dll'))
    [IO.File]::Copy($wrongLauncher, (Join-Path $otherFx 'DiamondDogsTogether.exe'))
    $changedLauncher = New-FixtureReceipt $otherFx 'receipt.json' $bothArtifacts
    Assert-Throws { & $packageScript -Friend -CheckReceipt $changedLauncher -OutputRoot $distFx } 'The receipt launcher changed since its checks' 'a launcher changed after its checks is refused'
    $staleReceipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json -AsHashtable
    $staleReceipt.source_hashes['CMakeLists.txt'] = '0' * 64
    $stalePath = Join-Path $receiptFx 'stale.json'
    [IO.File]::WriteAllText($stalePath, ($staleReceipt | ConvertTo-Json -Depth 12), $utf8)
    Assert-Throws { & $packageScript -Friend -CheckReceipt $stalePath -OutputRoot $distFx } 'Release source does not match check receipt' 'real package admission rejects stale source receipt'
    Assert-True (-not (Test-Path -LiteralPath $distFx)) 'every refusal comes before staging'

    $null = & $packageScript -Friend -LoaderPath $fakeLoader -LauncherPath $fakeLauncher -CheckReceipt $receiptPath -OutputRoot $distFx
    $runs = @(Get-ChildItem -LiteralPath $distFx -Directory)
    Assert-Equal $runs.Count 1 'one package run folder'
    Assert-True ($runs[0].Name -cmatch ('^DiamondDogsTogether-' + [regex]::Escape($packageVersion) + '-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{8}$')) "package run folder name: $($runs[0].Name)"
    $pkg = Join-Path $runs[0].FullName 'package'
    $zipPath = Join-Path $runs[0].FullName "DiamondDogsTogether-$packageVersion.zip"
    Assert-True (Test-Path -LiteralPath $zipPath -PathType Leaf) 'the player zip is DiamondDogsTogether-<version>.zip'
    Assert-Equal ((@(Get-ChildItem -LiteralPath $pkg -Force | ForEach-Object { $_.Name } | Sort-Object)) -join ',') 'DiamondDogsTogether.exe,dinput8.dll,mgs-coop' 'the package root: the exe, the loader and the mgs-coop folder'
    $modFiles = @('DYNAMITE-LICENSE', 'ImGui-LICENSE.txt', 'MinHook-LICENSE.txt', 'package.json', 'README.txt', 'THIRD_PARTY_NOTICES.md', 'tpp-mod-LICENSE')
    Assert-Equal ((@(Get-ChildItem -LiteralPath (Join-Path $pkg 'mgs-coop') -Force | ForEach-Object { $_.Name } | Sort-Object)) -join ',') (($modFiles | Sort-Object) -join ',') 'mgs-coop: the licences, notices, README.txt and package.json (no source.zip)'
    # The GPL source is its own release asset next to the player zip, never inside it.
    $sourceZipPath = Join-Path $runs[0].FullName "DDT-source-$packageVersion.zip"
    Assert-True (Test-Path -LiteralPath $sourceZipPath -PathType Leaf) 'the source zip DDT-source-<version>.zip is written next to the player zip'
    Assert-Equal ((@(Get-ChildItem -LiteralPath $runs[0].FullName -File -Filter '*.zip' | ForEach-Object { $_.Name } | Sort-Object)) -join ',') "DDT-source-$packageVersion.zip,DiamondDogsTogether-$packageVersion.zip" 'two release assets: the player zip and the source zip'
    $sourceAssetName = [IO.Path]::GetFileName($sourceZipPath)
    Assert-True ($sourceAssetName -cnotmatch '^(DiamondDogsTogether-|mgs-coop-friend-)') 'no updater takes the source zip for the update (the 0.14.3 prefix rule; from 0.14.4 the exact names, core_update.cpp)'
    # tools/check-release.ps1 (read only): a GitHub release is ready only with both zips, as built.
    # A saved `gh release view --json tagName,isDraft,assets` answer stands in for GitHub here.
    $checkRelease = Join-Path $projectRoot 'tools/check-release.ps1'
    $releaseFx = Join-Path $fixture 'release-check'
    $null = New-Item -ItemType Directory -Path $releaseFx -Force
    $playerAsset = [ordered]@{ name = "DiamondDogsTogether-$packageVersion.zip"; size = (Get-Item -LiteralPath $zipPath).Length; digest = 'sha256:' + (Get-Sha256 $zipPath).ToLowerInvariant() }
    $sourceAsset = [ordered]@{ name = $sourceAssetName; size = (Get-Item -LiteralPath $sourceZipPath).Length; digest = 'sha256:' + (Get-Sha256 $sourceZipPath).ToLowerInvariant() }
    function Write-ReleaseAnswer([string]$Name, [string]$Tag, [object[]]$Assets) {
        $path = Join-Path $releaseFx "$Name.json"
        [IO.File]::WriteAllText($path, (ConvertTo-Json -InputObject ([ordered]@{ tagName = $Tag; isDraft = $true; assets = @($Assets) }) -Depth 5), [Text.UTF8Encoding]::new($false))
        return $path
    }
    $tagName = "v$packageVersion"
    $okAnswer = Write-ReleaseAnswer 'both' $tagName @($sourceAsset, $playerAsset)
    $okText = (& $checkRelease -Version $packageVersion -PackageDir $runs[0].FullName -ReleaseJson $okAnswer) -join ' '
    Assert-True ($okText.Contains("DiamondDogsTogether-$packageVersion.zip") -and $okText.Contains($sourceAssetName) -and $okText.Contains('matching the local files')) 'check-release: both zips as built, the release is ready'
    Assert-Throws { & $checkRelease -Version $packageVersion -PackageDir $runs[0].FullName -ReleaseJson (Write-ReleaseAnswer 'player-only' $tagName @($playerAsset)) } "$sourceAssetName is not attached" 'check-release: the player zip alone is refused (no source offered)'
    Assert-Throws { & $checkRelease -Version $packageVersion -ReleaseJson (Write-ReleaseAnswer 'source-only' $tagName @($sourceAsset)) } "DiamondDogsTogether-$packageVersion.zip is not attached" 'check-release: the source zip alone is refused'
    $short = [ordered]@{ name = $playerAsset.name; size = $playerAsset.size - 1; digest = $playerAsset.digest }
    Assert-Throws { & $checkRelease -Version $packageVersion -PackageDir $runs[0].FullName -ReleaseJson (Write-ReleaseAnswer 'short' $tagName @($short, $sourceAsset)) } 'bytes' 'check-release: an upload of another size is refused'
    $otherHash = [ordered]@{ name = $sourceAsset.name; size = $sourceAsset.size; digest = 'sha256:' + ('0' * 64) }
    Assert-Throws { & $checkRelease -Version $packageVersion -PackageDir $runs[0].FullName -ReleaseJson (Write-ReleaseAnswer 'hash' $tagName @($playerAsset, $otherHash)) } 'SHA-256' 'check-release: an upload with another SHA-256 is refused'
    $decoy = [ordered]@{ name = "DiamondDogsTogether-$packageVersion-debug.zip"; size = 1 }
    Assert-Throws { & $checkRelease -Version $packageVersion -ReleaseJson (Write-ReleaseAnswer 'decoy' $tagName @($playerAsset, $sourceAsset, $decoy)) } 'looks like a player zip too' 'check-release: a second DiamondDogsTogether-*.zip is refused (the 0.14.3 updater might take it)'
    Assert-Throws { & $checkRelease -Version $packageVersion -ReleaseJson (Write-ReleaseAnswer 'tag' 'v0.0.1' @($playerAsset, $sourceAsset)) } "not $tagName" 'check-release: another tag is refused'
    Assert-Equal @(Get-ChildItem -LiteralPath $pkg -File -Recurse -Force | Where-Object { $_.Name -like '*source*' }).Count 0 'the player package holds no source archive'
    Assert-Equal (Get-Sha256 (Join-Path $pkg 'DiamondDogsTogether.exe')) (Get-Sha256 $fakeLauncher) 'the receipt launcher is packaged'
    Assert-Equal (Get-Sha256 (Join-Path $pkg 'dinput8.dll')) (Get-Sha256 $fakeLoader) 'the receipt loader is packaged'
    $licenseSources = [ordered]@{ 'DYNAMITE-LICENSE' = 'third_party/dynamite/LICENSE'; 'MinHook-LICENSE.txt' = 'third_party/minhook/LICENSE.txt'; 'ImGui-LICENSE.txt' = 'third_party/imgui/LICENSE.txt'
        'tpp-mod-LICENSE' = 'third_party/tpp-mod/LICENSE'; 'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md' }
    foreach ($name in $licenseSources.Keys) { Assert-Equal (Get-Sha256 (Join-Path $pkg "mgs-coop/$name")) (Get-Sha256 (Join-Path $projectRoot $licenseSources[$name])) "licence packaged intact: $name" }
    # package.json in the format the launcher core reads (core_install.cpp ReadPackage).
    $manifest = Get-Content -LiteralPath (Join-Path $pkg 'mgs-coop/package.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($manifest.product)|$($manifest.version)|$($manifest.layout)|$($manifest.flavor)" "mgs-coop-local-package|$packageVersion|drop-in|friend-play" 'package manifest identity'
    Assert-Equal $manifest.version (Get-ProjectVersion $projectRoot) 'package.json names the CMake project version (the exe refuses any other)'
    Assert-Equal "$($manifest.executable_sha256)|$($manifest.steam_dll_sha256)" '085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45|08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A' 'the pinned game hashes'
    Assert-Equal "$($manifest.runtime_archive.target)|$($manifest.runtime_archive.sha256)|$($manifest.runtime_archive.bytes)" 'mgs-coop/mgs-coop-player-runtime.dat|87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96|16333824' 'runtime archive referenced by hash and size only'
    $rows = @($manifest.files)
    $wantTargets = @('DiamondDogsTogether.exe', 'dinput8.dll') + @($modFiles | Where-Object { $_ -cne 'package.json' } | ForEach-Object { "mgs-coop/$_" })
    Assert-Equal @($rows | Where-Object { $_.target -like '*source*' }).Count 0 'files[] lists no source archive'
    Assert-Equal "$($manifest.source_archive.name)|$($manifest.source_archive.sha256)" "DDT-source-$packageVersion.zip|$(Get-Sha256 $sourceZipPath)" 'package.json names the source zip and its hash'
    Assert-True ([string]$manifest.source_archive.where -match "v$([regex]::Escape($packageVersion))") 'package.json says where the source is (the release of this version)'
    Assert-Equal ((@($rows | ForEach-Object { $_.target }) | Sort-Object) -join ',') (($wantTargets | Sort-Object) -join ',') 'files[]: one row per file of the zip (package.json itself excepted)'
    Assert-Equal @($rows | Where-Object { $_.target -ceq 'dinput8.dll' }).Count 1 'exactly one dinput8.dll row'
    foreach ($row in $rows) {
        Assert-Equal $row.source $row.target "drop-in row: source = target ($($row.target))"
        Assert-Equal $row.sha256 (Get-Sha256 (Join-Path $pkg $row.target)) "files[] hash of $($row.target)"
    }
    Assert-Equal "$($manifest.build.source)|$($manifest.build.profile)" 'release_receipt|Release' 'the build is the Release receipt'
    Assert-Equal $manifest.build.receipt ([IO.Path]::GetRelativePath($projectRoot, $receiptPath)) 'package.json records the receipt'
    Assert-Equal $manifest.build.receipt_sha256 (Get-Sha256 $receiptPath) 'package.json records the receipt hash'
    Assert-Equal "$($manifest.build.loader_sha256)|$($manifest.build.launcher_sha256)" "$(Get-Sha256 $fakeLoader)|$(Get-Sha256 $fakeLauncher)" 'package.json build: the loader and launcher hashes'
    # The player zip: exactly the staged files at its root, and nothing to run but the exe.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zipNames = @()
    $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $zipNames = @($zip.Entries | ForEach-Object { $_.FullName })
        foreach ($entry in $zip.Entries) {
            $stream = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
            try { $hash = [Convert]::ToHexString($sha.ComputeHash($stream)) } finally { $sha.Dispose(); $stream.Dispose() }
            Assert-Equal $hash (Get-Sha256 (Join-Path $pkg $entry.FullName)) "zip entry bytes: $($entry.FullName)"
        }
    } finally { $zip.Dispose() }
    $wantEntries = @('DiamondDogsTogether.exe', 'dinput8.dll') + @($modFiles | ForEach-Object { "mgs-coop/$_" })
    Assert-Equal (($zipNames | Sort-Object) -join ',') (($wantEntries | Sort-Object) -join ',') 'the zip root is the game folder: the exe, the loader and mgs-coop/...'
    Assert-Equal @($zipNames | Where-Object { $_.ToLowerInvariant().Contains('source') }).Count 0 'the player zip holds no source archive'
    Assert-Equal @($zipNames | Where-Object { $_.Contains('\') }).Count 0 'zip entry names use forward slashes'
    Assert-Equal @($zipNames | Where-Object { [IO.Path]::GetExtension($_).ToLowerInvariant() -in @('.bat', '.cmd', '.ps1', '.psm1') }).Count 0 'no .bat, .ps1 or .psm1 for players'
    $gameExtensions = @('.dat', '.fpk', '.fpkd', '.fox2', '.mtar', '.qar', '.ftex', '.fmdl', '.subp', '.lng', '.lng2', '.sbp', '.gani', '.frig', '.fv2', '.fsm', '.fpkl')
    Assert-Equal @($zipNames | Where-Object { $gameExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() }).Count 0 'no game archive or asset file types'
    Assert-Equal @($zipNames | Where-Object { $_ -match '(^|/)link-secret' -or $_ -like '*mgs-coop-player-runtime*' -or $_ -like '*FRIEND-GUIDE*' -or $_ -like '*RUNTIME-ARCHIVE*' }).Count 0 'no link secret, runtime archive or old guide'
    Assert-Equal @(Get-ChildItem -LiteralPath $pkg -File -Recurse -Force | Where-Object { $_.Length -eq 16333824 }).Count 0 'no file of the runtime archive size'
    # README.txt for players: ASCII + CRLF, the drag-and-drop flow, no PowerShell tool.
    $readmeBytes = [IO.File]::ReadAllBytes((Join-Path $pkg 'mgs-coop/README.txt'))
    Assert-Equal @($readmeBytes | Where-Object { $_ -gt 127 }).Count 0 'README.txt is ASCII'
    $readme = [Text.Encoding]::ASCII.GetString($readmeBytes)
    Assert-True ($readme.Contains("`r`n") -and !($readme -replace "`r`n", '').Contains("`n")) 'README.txt has CRLF line endings'
    foreach ($needle in "Diamond Dogs Together $packageVersion", 'DiamondDogsTogether.exe', 'mgsvtpp.exe', 'dinput8.dll', 'Host game', 'Join game', 'Wait for a Steam invite',
            'Tools > Share save', 'Tools > Collect logs', 'Uninstall', "DDT-source-$packageVersion.zip", 'github.com/TrollTaylor/diamond-dogs-together/releases', "v$packageVersion", 'THIRD_PARTY_NOTICES.md', '1.0.15.4', 'Failed to log in') {
        Assert-True $readme.Contains($needle) "README.txt mentions $needle"
    }
    foreach ($needle in '.bat', 'Play-Coop', 'Install-Coop', 'Share-Save', 'LAN', 'VPN', 'UDP', '@VERSION@', 'SteamID64', 'mgs-coop\source.zip') { Assert-True (!$readme.Contains($needle)) "README.txt no longer mentions $needle" }
    # DDT-source-<version>.zip: the complete corresponding source (GPL), launcher and its embedded assets included.
    $sourceZip = [IO.Compression.ZipFile]::OpenRead($sourceZipPath)
    try {
        $sourceNames = @($sourceZip.Entries | ForEach-Object { $_.FullName })
        $reader = [IO.StreamReader]::new($sourceZip.GetEntry('source-files.json').Open())
        try { $sourceRows = @($reader.ReadToEnd() | ConvertFrom-Json) } finally { $reader.Dispose() }
        foreach ($relative in @('CMakeLists.txt', 'README.md', 'THIRD_PARTY_NOTICES.md', 'native/probe.cpp', 'native/coop_reason_text.cpp', 'native/launcher/launcher_main.cpp',
                'native/launcher/runtime_dat.cpp', 'native/launcher/core/core_model.cpp', 'native/launcher/assets/mgs_coop_four_player_controller.fpkd.bin',
                'native/launcher/assets/mgs_coop_two_player_scarf_controller.fpkd.bin', 'native/launcher/resources/ddt_launcher.rc', 'native/launcher/resources/ddt_launcher.ico',
                'lua/coop/character.lua', 'cmake/launcher_gui.cmake', 'third_party/imgui/imgui.cpp', 'third_party/dynamite/LICENSE', 'tools/package.ps1', 'tools/dev/Play-LocalCoop.ps1',
                'tests/fixtures/partner-lab/e0-host.log', 'tools/archive_probe/main.go', 'tools/archive_probe/go.mod', 'tools/archive_probe/go.sum',
                # The icon's source image: tools/launcher/make_launcher_assets.py and the launcher icon CTests read it.
                'docs/readme/logo.png') + $incSources) {
            Assert-True ($sourceNames -ccontains $relative) "the source zip holds $relative"
            $row = @($sourceRows | Where-Object { $_.path -ceq $relative })
            Assert-True ($row.Count -eq 1 -and $row[0].sha256 -ceq (Get-Sha256 (Join-Path $projectRoot $relative))) "source-files.json lists $relative with its hash"
        }
        foreach ($relative in @('third_party/minhook/CMakeLists.txt', 'third_party/minhook/include/MinHook.h', 'third_party/minhook/src/hook.c')) {
            Assert-True ($sourceNames -ccontains $relative) "the source zip includes the checked dependency $relative"
            $row = @($sourceRows | Where-Object { $_.path -ceq $relative })
            Assert-Equal $row[0].sha256 $currentSource.hashes[$relative.Replace('/', '\')] "source dependency matches the Release snapshot $relative"
        }
        Assert-Equal @($sourceNames | Where-Object { $_.Contains('\') -or $_ -like '*mgs-coop-player-runtime*' -or $_ -match '(^|/)link-secret' -or [IO.Path]::GetExtension($_) -in @('.exe', '.dll', '.obj') }).Count 0 'the source zip: forward slashes, no binaries, no runtime archive, no link secret'
        Assert-Equal @($sourceNames | Where-Object { $_ -like 'tools/friend/*' -or $_ -like 'build/*' }).Count 0 'the source zip: no retired player scripts, no build output'
        Assert-Equal @($sourceNames | Where-Object { [IO.Path]::GetExtension($_) -in @('.log', '.go', '.mod', '.sum') -and
            $_ -cnotmatch '^(tests/fixtures/partner-lab/[^/]+\.log|tools/archive_probe/(main\.go|go\.mod|go\.sum))$' }).Count 0 'the source zip: logs and Go files only at their exact places'
    } finally { $sourceZip.Dispose() }
    # Regression: an owned, ordinary dependency lives outside the staged source
    # root, reached only by the exact supported third_party/minhook junction.
    # No source is compiled, no game is installed and no external target is used.
    & {
        $linkedRoot = Join-Path $fixture 'junction-package-source'
        $dependency = Join-Path $fixture 'junction-package-dependency'
        $junction = Join-Path $linkedRoot 'third_party\minhook'
        foreach ($path in @($linkedRoot, $dependency, $junction)) { Assert-InFixture $path }
        foreach ($dir in @('native','lua','tests','tools\worker','third_party','cmake','docs\readme')) {
            $null = New-Item -ItemType Directory -Path (Join-Path $linkedRoot $dir) -Force
        }
        $null = New-Item -ItemType Directory -Path (Join-Path $dependency 'src'), (Join-Path $dependency 'include') -Force
        foreach ($file in @('CMakeLists.txt','README.md','THIRD_PARTY_NOTICES.md','docs\readme\logo.png','tools\package.ps1','tools\package-readme-player.txt','tools\worker\WorkerSource.psm1')) {
            [IO.File]::Copy((Join-Path $projectRoot $file), (Join-Path $linkedRoot $file), $false)
        }
        [IO.File]::WriteAllText((Join-Path $linkedRoot 'docs\fixture.md'), '# Owned source-only package fixture', $utf8)
        foreach ($file in @('third_party\dynamite\LICENSE','third_party\imgui\LICENSE.txt','third_party\tpp-mod\LICENSE')) {
            $destination = Join-Path $linkedRoot $file
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
            [IO.File]::Copy((Join-Path $projectRoot $file), $destination, $false)
        }
        $minhookFiles = [ordered]@{
            'CMakeLists.txt' = '# Owned MinHook CMake fixture'
            'include\MinHook.h' = '// Owned MinHook header fixture'
            'src\hook.c' = '/* Owned MinHook C source fixture */'
            'LICENSE.txt' = 'Owned fixture license; not a compiled dependency'
        }
        foreach ($relative in $minhookFiles.Keys) {
            [IO.File]::WriteAllText((Join-Path $dependency $relative), $minhookFiles[$relative], $utf8)
        }
        try {
            $null = New-Item -ItemType Junction -Path $junction -Target $dependency
            $link = Get-Item -LiteralPath $junction -Force
            Assert-True (($link.Attributes -band [IO.FileAttributes]::ReparsePoint) -and $link.LinkType -ceq 'Junction') 'package regression uses a real junction'
            Assert-Equal $link.ResolveLinkTarget($true).FullName $dependency 'junction target is the separately owned dependency'
            $snapshot = Get-WorkerSourceHashes $linkedRoot
            Assert-True $snapshot.dependencies['third_party\minhook'].linked 'Release snapshot records the linked dependency'
            $receipt = New-FixtureReceipt $receiptFx 'junction.json' $bothArtifacts $snapshot
            $dist = Join-Path $fixture 'junction-package-dist'
            $null = & (Join-Path $linkedRoot 'tools\package.ps1') -Friend -LoaderPath $fakeLoader -LauncherPath $fakeLauncher -CheckReceipt $receipt -OutputRoot $dist
            $linkedRuns = @(Get-ChildItem -LiteralPath $dist -Directory)
            Assert-Equal $linkedRuns.Count 1 'junction fixture stages exactly one package'
            Assert-True (!(Test-Path -LiteralPath (Join-Path $linkedRuns[0].FullName 'package\mgs-coop\source.zip'))) 'junction fixture: no source.zip in the player package'
            $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $linkedRuns[0].FullName "DDT-source-$packageVersion.zip"))
            try {
                $reader = [IO.StreamReader]::new($zip.GetEntry('source-files.json').Open())
                try { $linkedRows = @($reader.ReadToEnd() | ConvertFrom-Json) } finally { $reader.Dispose() }
                foreach ($relative in $minhookFiles.Keys) {
                    $logical = 'third_party\minhook\' + $relative
                    $name = $logical.Replace('\', '/')
                    $expected = Get-Sha256 (Join-Path $dependency $relative)
                    Assert-Equal $snapshot.hashes[$logical] $expected "Release checks linked dependency $relative"
                    $rows = @($linkedRows | Where-Object { $_.path -ceq $name })
                    Assert-Equal $rows.Count 1 "source manifest includes linked dependency once $relative"
                    Assert-Equal $rows[0].sha256 $expected "source manifest hash matches linked dependency $relative"
                    $entries = @($zip.Entries | Where-Object { $_.FullName -ceq $name })
                    Assert-Equal $entries.Count 1 "the source zip includes linked dependency once $relative"
                    $stream = $entries[0].Open()
                    $sha = [Security.Cryptography.SHA256]::Create()
                    try { $hash = [Convert]::ToHexString($sha.ComputeHash($stream)) }
                    finally { $sha.Dispose(); $stream.Dispose() }
                    Assert-Equal $hash $expected "the source zip bytes match linked dependency $relative"
                }
            } finally { $zip.Dispose() }
        } finally {
            # Remove only the owned junction entry before the outer fixture's
            # no-links recursive cleanup; never recurse into its target.
            if (Test-Path -LiteralPath $junction) {
                $link = Get-Item -LiteralPath $junction -Force
                if (!($link.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $link.LinkType -cne 'Junction' -or
                    $link.ResolveLinkTarget($true).FullName -cne $dependency) { throw 'Junction fixture cleanup ownership changed' }
                Assert-InFixture $junction
                [IO.Directory]::Delete($junction)
            }
        }
        Assert-True (Test-Path -LiteralPath (Join-Path $dependency 'src\hook.c') -PathType Leaf) 'junction removal preserves owned dependency files'
    }
    # 0.14.4 review (package-untracked-files-published-in-source-zip): the public source zip never carries a file git
    # neither tracks nor ignores (a scratch note, a run's JSON with a join code or a local path). package.ps1 refuses
    # it by name before anything is written, and build.source_dirty counts untracked files too. A git checkout of its
    # own (inside the ignored build folder); MinHook is untracked there, as the real submodule's files are to the
    # repository, and stays in the zip (the Release receipt pins it).
    & {
        $gitRoot = Join-Path $fixture 'git-package-source'
        Assert-InFixture $gitRoot
        foreach ($dir in @('native','lua','tests','tools\worker','third_party\minhook\src','cmake','docs\readme')) {
            $null = New-Item -ItemType Directory -Path (Join-Path $gitRoot $dir) -Force
        }
        foreach ($file in @('CMakeLists.txt','README.md','THIRD_PARTY_NOTICES.md','docs\readme\logo.png','tools\package.ps1','tools\package-readme-player.txt','tools\worker\WorkerSource.psm1',
                'third_party\dynamite\LICENSE','third_party\imgui\LICENSE.txt','third_party\tpp-mod\LICENSE')) {
            $destination = Join-Path $gitRoot $file
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
            [IO.File]::Copy((Join-Path $projectRoot $file), $destination, $false)
        }
        [IO.File]::WriteAllText((Join-Path $gitRoot 'native\fixture.cpp'), '// owned package-source fixture', $utf8)
        [IO.File]::WriteAllText((Join-Path $gitRoot 'docs\fixture.md'), '# Owned git package-source fixture', $utf8)
        [IO.File]::WriteAllText((Join-Path $gitRoot 'third_party\minhook\CMakeLists.txt'), '# Owned MinHook CMake fixture', $utf8)
        [IO.File]::WriteAllText((Join-Path $gitRoot 'third_party\minhook\src\hook.c'), '/* Owned MinHook C source fixture */', $utf8)
        [IO.File]::WriteAllText((Join-Path $gitRoot 'third_party\minhook\LICENSE.txt'), 'Owned fixture license', $utf8)
        function Invoke-FixtureGit([string[]]$GitArgs) {
            $out = @(& git -C $gitRoot -c user.name=fixture -c user.email=fixture@example.invalid @GitArgs 2>$null)
            if ($LASTEXITCODE -ne 0) { throw "git $($GitArgs -join ' ') failed: $($out -join ' ')" }
            return $out
        }
        $null = Invoke-FixtureGit @('init', '-q')
        $null = Invoke-FixtureGit @('add', '--', 'CMakeLists.txt', 'README.md', 'THIRD_PARTY_NOTICES.md', 'docs', 'tools', 'native', 'third_party/dynamite', 'third_party/imgui', 'third_party/tpp-mod')
        $null = Invoke-FixtureGit @('commit', '-q', '-m', 'fixture')
        $gitScript = Join-Path $gitRoot 'tools\package.ps1'
        function Get-FixtureRun([string]$Dist) {
            $made = @(Get-ChildItem -LiteralPath $Dist -Directory)
            Assert-Equal $made.Count 1 "one package run in $Dist"
            $pkgJson = Get-Content -LiteralPath (Join-Path $made[0].FullName 'package\mgs-coop\package.json') -Raw | ConvertFrom-Json
            $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $made[0].FullName "DDT-source-$packageVersion.zip"))
            try { $names = @($zip.Entries | ForEach-Object { $_.FullName }) } finally { $zip.Dispose() }
            return [pscustomobject]@{ package=$pkgJson; names=$names }
        }
        # Clean: packaged, not dirty; the untracked MinHook dependency files are still in the zip.
        $receipt = New-FixtureReceipt $receiptFx 'git-clean.json' $bothArtifacts (Get-WorkerSourceHashes $gitRoot)
        $dist = Join-Path $fixture 'git-package-dist-clean'
        $null = & $gitScript -Friend -LoaderPath $fakeLoader -LauncherPath $fakeLauncher -CheckReceipt $receipt -OutputRoot $dist
        $clean = Get-FixtureRun $dist
        Assert-True ($clean.package.build.source_commit -cmatch '^[0-9a-f]{40}$' -and $clean.package.build.source_dirty -eq $false) 'a clean git checkout packages with source_dirty false'
        Assert-True ($clean.names -ccontains 'native/fixture.cpp' -and $clean.names -ccontains 'third_party/minhook/src/hook.c' -and $clean.names -ccontains 'third_party/minhook/LICENSE.txt') 'tracked source and the MinHook dependency are in the source zip'
        # An untracked note: refused by name before anything is staged.
        $note = Join-Path $gitRoot 'tools\notes.txt'
        [IO.File]::WriteAllText($note, 'scratch: a join code and C:\Users\someone\path', $utf8)
        $dist = Join-Path $fixture 'git-package-dist-untracked'
        Assert-Throws { & $gitScript -Friend -LoaderPath $fakeLoader -LauncherPath $fakeLauncher -CheckReceipt $receipt -OutputRoot $dist } 'Untracked file would be published in the source zip: tools\notes.txt' 'an untracked file under a published folder is refused by name'
        Assert-True (!(Test-Path -LiteralPath $dist) -or @(Get-ChildItem -LiteralPath $dist -Force).Count -eq 0) 'the refusal comes before anything is staged'
        $code = Join-Path $gitRoot 'native\scratch_probe.cpp'
        [IO.File]::WriteAllText($code, '// untracked experiment', $utf8)
        $receiptCode = New-FixtureReceipt $receiptFx 'git-code.json' $bothArtifacts (Get-WorkerSourceHashes $gitRoot)
        Assert-Throws { & $gitScript -Friend -LoaderPath $fakeLoader -LauncherPath $fakeLauncher -CheckReceipt $receiptCode -OutputRoot $dist } 'Untracked file would be published in the source zip: native\scratch_probe.cpp' 'untracked code is refused too, even when the receipt hashed it'
        Remove-Item -LiteralPath $code
        # Tracked (added, not committed): published, and the package says the tree differed from the commit.
        $null = Invoke-FixtureGit @('add', '--', 'tools/notes.txt')
        $dist = Join-Path $fixture 'git-package-dist-added'
        $null = & $gitScript -Friend -LoaderPath $fakeLoader -LauncherPath $fakeLauncher -CheckReceipt $receipt -OutputRoot $dist
        $added = Get-FixtureRun $dist
        Assert-True ($added.names -ccontains 'tools/notes.txt' -and $added.package.build.source_dirty -eq $true) 'a tracked, uncommitted file is published and marks source_dirty'
        $null = Invoke-FixtureGit @('rm', '-q', '--cached', '--', 'tools/notes.txt')
        Remove-Item -LiteralPath $note
        # An untracked file that is never published (a log): not refused, not in the zip, but the tree is not clean.
        [IO.File]::WriteAllText((Join-Path $gitRoot 'tools\run.log'), 'local log', $utf8)
        $dist = Join-Path $fixture 'git-package-dist-log'
        $null = & $gitScript -Friend -LoaderPath $fakeLoader -LauncherPath $fakeLauncher -CheckReceipt $receipt -OutputRoot $dist
        $logged = Get-FixtureRun $dist
        Assert-True ($logged.names -cnotcontains 'tools/run.log' -and $logged.package.build.source_dirty -eq $true) 'an untracked unpublished file is left out and counts as dirty'
    }

    # The repository keeps no player scripts: the PowerShell player tooling was
    # retired in 0.14.0 (DiamondDogsTogether.exe replaces it, tests/ddt_core_tests).
    foreach ($retired in @('tools/friend', 'Play-Coop.bat', 'Play-World-Coop.bat')) {
        Assert-True (!(Test-Path -LiteralPath (Join-Path $projectRoot $retired))) "retired player tooling is gone: $retired"
    }
    $offlineBatText = [IO.File]::ReadAllText((Join-Path $projectRoot 'Play-Offline-Coop.bat'))
    Assert-True ($offlineBatText.Contains('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\dev\Start-LocalCoop.ps1" %*') -and $offlineBatText.Contains('set "PSModulePath="') -and
        $offlineBatText.Contains("`r`n") -and $offlineBatText.Contains('pause')) 'Play-Offline-Coop.bat: the developer local test in tools\dev with Windows PowerShell and all arguments'

    # ------------------------------------------------------------ installed-game fixtures (in process)
    # Steam fixture: account 7 with saves (only ever compared, never written);
    # a lab-staged package with fixture hashes for the owned installer.
    $steam = Join-Path $fixture 'steam'
    $userdata = Join-Path $steam 'userdata/7'
    foreach ($f in '287700/local/TPP_GAME_DATA0', '287700/local/PERSONAL_DATA0', '287700/remotecache.vdf', '311340/remote/TPP_GAME_DATA') {
        $p = Join-Path $userdata $f
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $p) -Force
        [IO.File]::WriteAllText($p, "fixture save $f")
    }
    $userdataBefore = Get-TreeHashes $userdata
    $archiveFx = Join-Path $fixture 'runtime-archive-fixture.dat'
    [IO.File]::WriteAllText($archiveFx, 'INERT runtime archive fixture')
    $templateGame = New-GameFixture 'template'
    $exeHash = Get-Sha256 (Join-Path $templateGame 'mgsvtpp.exe')
    $steamHash = Get-Sha256 (Join-Path $templateGame 'steam_api64.dll')
    $loader1 = Join-Path $fixture 'loader1.dll'; [IO.File]::WriteAllText($loader1, 'INERT earlier loader')
    $stage = Join-Path $fixture 'stage'
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $stage -LoaderPath $loader1 -Version '0.13.6' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 (Get-Sha256 $archiveFx)
    $coreInstaller = Join-Path $projectRoot 'tools/package-install.ps1'
    function Assert-PassThrough($Paths, [string]$What) {
        Assert-True (-not (Test-Path -LiteralPath $Paths.Marker) -and -not (Test-Path -LiteralPath $Paths.LabIni) -and -not (Test-Path -LiteralPath $Paths.CoopIni) -and
            -not (Test-Path -LiteralPath $Paths.LinkSecret) -and -not (Test-Path -LiteralPath $Paths.ClientHost)) "${What}: pass-through (no marker, lab.ini, coop.ini, link secret or client-host.txt)"
    }
    function New-InstalledGame([string]$Name, [switch]$Record) {
        # A game folder as DiamondDogsTogether.exe's setup leaves it (owned loader,
        # runtime archive, co-op save copy and, with -Record, friend-install.json).
        $g = New-GameFixture $Name
        $r = Invoke-Friend $coreInstaller @('-GameDirectory', $g, '-PackageDirectory', $stage, '-RuntimeArchive', $archiveFx, '-NoPrompt', '-NoInitialMode') $pwshPath
        Assert-Equal $r.Exit 0 "owned installer for the $Name fixture: $($r.Raw)"
        $gp = Get-InstancePaths -GameDirectory $g
        foreach ($f in @(@('287700\local\TPP_GAME_DATA0', 'fixture co-op progress'), @('287700\local\PERSONAL_DATA0', 'fixture co-op personal'), @('remote\TPP_GAME_DATA', 'fixture co-op remote'))) {
            $p = Join-Path $gp.CoopSaves $f[0]
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $p) -Force
            [IO.File]::WriteAllText($p, $f[1])
        }
        if ($Record) {
            $state = [ordered]@{ product = 'mgs-coop-friend-install'; version = $packageVersion; game_directory = $g; role_hint = 'Join'; character = 'story'
                save_dir = $gp.CoopSaves; status = 'installed'; launcher = 'Diamond Dogs Together' }
            [IO.File]::WriteAllText((Join-Path $gp.Mod 'friend-install.json'), ($state | ConvertTo-Json) + "`n", $utf8)
        }
        return $gp
    }

    # ------------------------------------------------------------ the import core with a save handoff stage (Import-CoopProgressFiles, P2-18)
    $kp = New-InstalledGame 'handoff-core'
    $coreGame = $kp.Game
    $coreLocal = Join-Path $kp.CoopSaves '287700\local'
    foreach ($f in @(@('TPP_GAME_DATA0', 'core own progress'), @('PERSONAL_DATA1', 'core own old personal slot'), @('TPP_CONFIG_DATA', 'core own settings'), @('TPP_GRAPHICS_CONFIG', 'core own graphics'))) {
        [IO.File]::WriteAllText((Join-Path $coreLocal $f[0]), $f[1])
    }
    # An older copy an import moved aside: [R] restores the newer handoff copy first.
    $olderImport = Join-Path $kp.CoopSavesReplaced '20000101-000000-before-import'
    $null = New-Item -ItemType Directory -Path (Join-Path $olderImport '287700\local') -Force
    [IO.File]::WriteAllText((Join-Path $olderImport '287700\local\TPP_GAME_DATA0'), 'an old imported copy')
    $coreBefore = Get-RelativeTreeHashes $kp.CoopSaves
    $coreStage = New-HandoffStage $kp.CoopSavesHandoff '7A8B9CAD' @{ '287700/local/TPP_GAME_DATA0' = 'host progress 0'; '287700/local/PERSONAL_DATA0' = 'host personal 0'; 'remote/TPP_GAME_DATA' = 'host remote' }
    $coreAside = Import-CoopProgressFiles -StageRoot $coreStage.Stage -SaveDir $kp.CoopSaves -ReplacedRoot $kp.CoopSavesReplaced -Suffix '-before-handoff' `
        -Marker @{ Source = 'handoff'; ExportedUtc = '2026-10-15T12:00:00Z' } -Manifest $coreStage.Rows
    Assert-True ((Split-Path -Parent $coreAside) -eq $kp.CoopSavesReplaced -and (Split-Path -Leaf $coreAside) -cmatch '^[0-9]{8}-[0-9]{6}(-[0-9]+)?-before-handoff$') "the old copy goes to coop-saves-replaced\<stamp>-before-handoff: $coreAside"
    Assert-Equal (Get-RelativeTreeHashes $coreAside) $coreBefore 'the aside folder holds the old tree'
    foreach ($row in $coreStage.Rows) { Assert-Equal (Get-Sha256 (Join-Path $kp.CoopSaves $row.entry.Replace('/', '\'))) $row.sha256 "progress replaced: $($row.entry)" }
    Assert-True ([IO.File]::ReadAllText((Join-Path $coreLocal 'TPP_CONFIG_DATA')) -ceq 'core own settings' -and [IO.File]::ReadAllText((Join-Path $coreLocal 'TPP_GRAPHICS_CONFIG')) -ceq 'core own graphics') 'settings are kept'
    Assert-True (!(Test-Path -LiteralPath (Join-Path $coreLocal 'PERSONAL_DATA1'))) 'an extra old PERSONAL_DATA is dropped'
    $coreMarker = Get-Content -LiteralPath (Join-Path $kp.CoopSaves 'imported-progress.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($coreMarker.source)|$((@($coreMarker.files) | ForEach-Object { $_.entry }) -join ',')" 'handoff|287700/local/TPP_GAME_DATA0,287700/local/PERSONAL_DATA0,remote/TPP_GAME_DATA' 'the marker says handoff and lists the files'
    Assert-True (!(Test-Path -LiteralPath $coreStage.Stage) -and !@(Get-ChildItem -LiteralPath $kp.Mod -Directory -Filter 'coop-saves-new-*').Count) 'the stage is removed and no new copy is left over'
    $coreInfo = Get-FriendImportedSaveInfo -SaveDir $kp.CoopSaves -ReplacedRoot $kp.CoopSavesReplaced
    Assert-Equal "$($coreInfo.Source)|$($coreInfo.Kind)" 'marker|handoff' 'the copy counts as a save handoff'
    # Rollback: the final rename fails (a handle is held on a file of the new copy); the current copy is put back.
    $afterHandoff = Get-RelativeTreeHashes $kp.CoopSaves
    $asidesBefore = @(Get-ChildItem -LiteralPath $kp.CoopSavesReplaced -Directory).Count
    $rollStage = New-HandoffStage $kp.CoopSavesHandoff '8B9CADBE' @{ '287700/local/TPP_GAME_DATA0' = 'never applied' }
    $script:heldCopy = $null
    try {
        Assert-Throws { Import-CoopProgressFiles -StageRoot $rollStage.Stage -SaveDir $kp.CoopSaves -ReplacedRoot $kp.CoopSavesReplaced -Suffix '-before-handoff' -Marker @{ Source = 'handoff' } -Manifest $rollStage.Rows `
                -TestBeforeFinalMove { param($newCopy) $script:heldCopy = [IO.FileStream]::new((Join-Path $newCopy '287700\local\TPP_GAME_DATA0'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read) } } '' 'a blocked final rename fails'
    } finally { if ($script:heldCopy) { $script:heldCopy.Dispose() } }
    Assert-Equal (Get-RelativeTreeHashes $kp.CoopSaves) $afterHandoff 'rollback: the current copy is back unchanged'
    Assert-Equal @(Get-ChildItem -LiteralPath $kp.CoopSavesReplaced -Directory).Count $asidesBefore 'rollback: no aside folder is left behind'
    Assert-True (Test-Path -LiteralPath (Join-Path $rollStage.Stage 'handoff.json')) 'a failed import keeps its stage'
    foreach ($left in @(Get-ChildItem -LiteralPath $kp.Mod -Directory -Filter 'coop-saves-new-*')) { Assert-InFixture $left.FullName; Remove-Item -LiteralPath $left.FullName -Recurse -Force }
    Assert-True (Remove-CoopProgressStage -StageRoot $rollStage.Stage) 'the stage is removed by its known names'
    Assert-Throws { Import-CoopProgressFiles -StageRoot $rollStage.Stage -SaveDir $kp.CoopSaves -ReplacedRoot $kp.CoopSavesReplaced -Suffix '-before-handoff' -Marker @{ Source = 'handoff' } } 'missing' 'a missing stage is refused'
    Assert-Equal (Get-RelativeTreeHashes $kp.CoopSaves) $afterHandoff 'a refused stage changes nothing'
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'the import core never touches the real saves'

    # ------------------------------------------------------------ the save handoff after the joiner's game exits (Invoke-CoopHandoffAfterExit, P2-17/18)
    # In-process on fixture folders: no game runs, and MGS_COOP_TEST_NO_LAUNCH=1 checks every
    # file a restart needs but starts nothing.
    $guestSteamId = Get-CoopSteamId64 -ActiveUser 97000002
    $hostSteamId = Get-CoopSteamId64 -ActiveUser 97000001
    function New-HandoffSession([string]$Name, [switch]$SteamWait) {
        # A joiner's active Play-Coop session as Play-Coop leaves it (lab.ini from the recorded
        # settings, every file's hash recorded first) with its own co-op copy.
        $g = New-GameFixture $Name
        $gp = Get-InstancePaths -GameDirectory $g
        foreach ($f in @(@('287700\local\TPP_GAME_DATA0', 'own progress'), @('287700\local\PERSONAL_DATA1', 'own old personal slot'), @('287700\local\TPP_CONFIG_DATA', 'own settings'), @('remote\TPP_GAME_DATA', 'own remote'))) {
            $p = Join-Path $gp.CoopSaves $f[0]
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $p) -Force
            [IO.File]::WriteAllText($p, $f[1])
        }
        $lab = [ordered]@{ role = 'guest'; save_dir = $gp.CoopSaves; transport = 'udp'; port = 47831; peer = '192.0.2.44'; world = 3; partner = 'native'
            steam_self = ''; steam_peer = ''; steam_wait = $false; steam_dispatch = ''; save_handoff = 'scene'; travel_lock = 'on' }
        if ($SteamWait) {
            $lab.transport = 'steam'; $lab.peer = ''; $lab.steam_self = $guestSteamId; $lab.steam_wait = $true
            $labText = New-CoopPlayLabIniText -Role guest -SaveDir $gp.CoopSaves -Transport steam -SteamSelf $guestSteamId -SteamWait -SaveHandoff scene -TravelLock on
        } else {
            $labText = New-CoopPlayLabIniText -Role guest -SaveDir $gp.CoopSaves -Port 47831 -PeerAddress '192.0.2.44' -SaveHandoff scene -TravelLock on
        }
        $writes = New-Object Collections.Generic.List[object]
        $writes.Add([pscustomobject]@{ Name = 'lab.ini'; Bytes = $utf8.GetBytes($labText) })
        if (!$SteamWait) { $writes.Add([pscustomobject]@{ Name = 'link-secret.txt'; Bytes = [Text.Encoding]::ASCII.GetBytes("$s7`r`n") }) }
        $writes.Add([pscustomobject]@{ Name = 'coop.ini'; Bytes = $utf8.GetBytes((New-CoopPlayCoopIniText -Character quiet)) })
        $writes.Add([pscustomobject]@{ Name = 'script-observe.enabled'; Bytes = [Text.Encoding]::ASCII.GetBytes("offline-v1`n") })
        $rows = @()
        foreach ($w in $writes) {
            [IO.File]::WriteAllBytes((Join-Path $gp.Mod $w.Name), [byte[]]$w.Bytes)
            $rows += [ordered]@{ name = $w.Name; sha256 = (Get-CoopTextSha256 -Bytes ([byte[]]$w.Bytes)) }
        }
        Save-CoopPlaySession -GameRoot $g -Session ([ordered]@{ v = 2; state = 'active'; role = 'guest'; port = 47831; started_utc = [DateTime]::UtcNow.AddMinutes(-10).ToString('o')
                launcher_pid = 0; launcher_start_utc = ''; watchdog_pid = 0; watchdog_start_utc = ''; session_id = [Guid]::NewGuid().ToString('N'); transport = $lab.transport
                files = $rows; upnp = [ordered]@{ added = $false; port = 0 }; clip_sha256 = ''; lab = $lab })
        return $gp
    }
    $exitedStart = '2026-10-15T11:59:00.0000000Z'
    $env:MGS_COOP_TEST_NO_LAUNCH = '1'
    $env:MGS_COOP_TEST_RELAUNCH_DELAY_SEC = '0'
    $sleeper = $null
    try {
        # 0. Online 2026-10-22 P5: Steam's running flag (read only; the fixture file here) and
        #    the wait for it, and play-coop.log.
        foreach ($row in @(@("1`n", $true, $true), @("0`n", $true, $false), @("unknown`n", $false, $false), @('', $false, $false))) {
            [IO.File]::WriteAllText($steamRunningFile, $row[0])
            $sr = Get-CoopSteamAppRunning
            Assert-True ($sr.Known -eq $row[1] -and $sr.Running -eq $row[2]) "Get-CoopSteamAppRunning reads '$($row[0].Trim())'"
        }
        $srSource = (Get-Command Get-CoopSteamAppRunning).Definition + (Get-Command Wait-CoopSteamAppStopped).Definition
        Assert-True ($srSource.Contains("HKCU:\Software\Valve\Steam\Apps\{0}") -and $srSource.Contains("'RunningAppID'") -and
            $srSource -notmatch 'Set-ItemProperty|New-ItemProperty|Remove-ItemProperty|New-Item |Remove-Item|reg(\.exe)? ') 'Steam''s running flag is only read (Apps\287700 Running, RunningAppID)'
        [IO.File]::WriteAllText($steamRunningFile, "1`n")
        $ws = Wait-CoopSteamAppStopped -TimeoutSec 1 -PollMs 200
        Assert-True (!$ws.Stopped -and $ws.Known -and $ws.WaitedMs -ge 900 -and $ws.Last -ceq '1') 'Steam still marks MGSV running: the wait gives up after its timeout'
        [IO.File]::WriteAllText($steamRunningFile, "unknown`n")
        $ws = Wait-CoopSteamAppStopped -TimeoutSec 5
        Assert-True ($ws.Stopped -and !$ws.Known -and $ws.WaitedMs -lt 1000) 'an unreadable flag: nothing to wait for'
        [IO.File]::WriteAllText($steamRunningFile, "0`n")
        $logGame = New-GameFixture 'play-coop-log'
        $logPaths = Get-InstancePaths -GameDirectory $logGame
        $null = New-Item -ItemType Directory -Path $logPaths.Mod -Force
        Write-CoopPlayLog -GameRoot $logGame -Text "first; a=1`r`nsecond"
        $logText = [IO.File]::ReadAllText($logPaths.PlayCoopLog)
        Assert-True ($logText -cmatch "^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}Z pid=$PID first; a=1  second`r`n$") "play-coop.log: one stamped line per call ($logText)"
        [IO.File]::WriteAllText($logPaths.PlayCoopLog, ('x' * 1048600))
        Write-CoopPlayLog -GameRoot $logGame -Text 'after rotation'
        Assert-True ((Get-Item -LiteralPath (Join-Path $logPaths.Mod 'play-coop.old.log')).Length -eq 1048600 -and ([IO.File]::ReadAllText($logPaths.PlayCoopLog)).EndsWith("after rotation`r`n") -and
            (Get-Item -LiteralPath $logPaths.PlayCoopLog).Length -lt 200) 'play-coop.log over 1 MB moves to play-coop.old.log'
        Write-CoopPlayLog -GameRoot (Join-Path $fixture 'no-such-game') -Text 'nowhere'
        Assert-True (!(Test-Path -LiteralPath (Join-Path $fixture 'no-such-game'))) 'no mgs-coop folder: nothing is written, nothing created'

        # 1. A valid request is applied: lab.ini again with save_handoff=off (its new hash recorded),
        #    the session stays active with handoff.count=1 and every co-op file in place.
        $hA = New-HandoffSession 'handoff-apply'
        $ownA = Get-RelativeTreeHashes $hA.CoopSaves
        $stA = New-HandoffStage $hA.CoopSavesHandoff '0A1B2C3D' @{ '287700/local/TPP_GAME_DATA0' = 'host progress 0'; '287700/local/PERSONAL_DATA0' = 'host personal 0'; 'remote/TPP_GAME_DATA' = 'host remote' }
        $null = New-HandoffRequest $hA.CoopSavesHandoff $stA 4242 '2026-10-15T11:59:00Z'
        $labBefore = [IO.File]::ReadAllText($hA.LabIni)
        $res = Invoke-CoopHandoffAfterExit -GameRoot $hA.Game -ExitedPid 4242 -ExitedStartUtc $exitedStart -Owner console
        Assert-Equal "$($res.Action)|$($res.Files)|$($res.Bytes)" 'relaunched|3|41' "a valid request is applied: $(@($res.Messages) -join ' ')"
        $sA = Get-SessionJson $hA
        Assert-Equal "$($sA.state)|$($sA.handoff.state)|$($sA.handoff.count)|$($sA.handoff.xfer)|$($sA.handoff.set_sha256)|$($sA.handoff.claimed_by_pid)|$($sA.handoff.aside)" "active|relaunching|1|0A1B2C3D|$($stA.Set)|$PID|$($res.Aside)" 'the session: active, relaunching, count 1, the claim'
        Assert-Equal ([IO.File]::ReadAllText($hA.LabIni)) $labBefore.Replace("save_handoff=scene`n", "save_handoff=off`n") 'lab.ini again with save_handoff=off'
        $labRowA = @($sA.files | Where-Object { $_.name -eq 'lab.ini' })[0]
        Assert-Equal "$($labRowA.sha256)|$($labRowA.prev_sha256)" "$(Get-Sha256 $hA.LabIni)|$(Get-CoopTextSha256 -Bytes $utf8.GetBytes($labBefore))" 'the new lab.ini hash is recorded (the old one as prev_sha256)'
        Assert-Equal $sA.lab.save_handoff 'off' 'the recorded settings say save_handoff=off'
        Assert-Equal (Get-PlayFilesPresent $hA) 4 'all four co-op files stay for the restart'
        foreach ($row in $stA.Rows) { Assert-Equal (Get-Sha256 (Join-Path $hA.CoopSaves $row.entry.Replace('/', '\'))) $row.sha256 "the host's $($row.entry) is in the co-op copy" }
        Assert-True ([IO.File]::ReadAllText((Join-Path $hA.CoopSaves '287700\local\TPP_CONFIG_DATA')) -ceq 'own settings' -and !(Test-Path -LiteralPath (Join-Path $hA.CoopSaves '287700\local\PERSONAL_DATA1'))) 'own settings kept, the old extra progress slot dropped'
        Assert-True ($res.Aside -cmatch '^[0-9]{8}-[0-9]{6}(-[0-9]+)?-before-handoff$' -and (Get-RelativeTreeHashes (Join-Path $hA.CoopSavesReplaced $res.Aside)) -eq $ownA) 'the own copy is kept whole in coop-saves-replaced\<stamp>-before-handoff'
        Assert-Equal (Get-Content -LiteralPath (Join-Path $hA.CoopSaves 'imported-progress.json') -Raw | ConvertFrom-Json).source 'handoff' 'the import marker says handoff'
        $appliedA = Get-Content -LiteralPath (Join-Path $hA.CoopSavesHandoff 'applied.json') -Raw | ConvertFrom-Json
        Assert-Equal "$($appliedA.format)|$($appliedA.v)|$($appliedA.set_sha256)|$($appliedA.aside)" "mgs-coop-save-handoff-applied|1|$($stA.Set)|$($res.Aside)" 'applied.json'
        Assert-True (!(Test-Path -LiteralPath (Join-Path $hA.CoopSavesHandoff 'request.json')) -and !(Test-Path -LiteralPath $stA.Stage)) 'the request and the stage are gone'
        $logA = [IO.File]::ReadAllText($hA.PlayCoopLog)
        Assert-True ($logA -cmatch "(?m) pid=$PID handoff=claimed; owner=console; exited_pid=4242; xfer=0A1B2C3D\r$" -and $logA.Contains("handoff=applied; files=3; bytes=41; aside=$($res.Aside)") -and
            $logA -cmatch '(?m) steam_wait; stopped=1; known=1; ms=[0-9]+; last=0\r$' -and $logA.Contains('relaunch=requested; started=0; steam=0') -and
            $logA.IndexOf('handoff=claimed') -lt $logA.IndexOf('steam_wait') -and $logA.IndexOf('steam_wait') -lt $logA.IndexOf('relaunch=requested')) "play-coop.log: the claim, the apply, Steam's flag, the request (in order): $logA"
        # 2. The cleanup guard: a live claim keeps every file.
        $guard = Invoke-CoopPlayCleanup -GameRoot $hA.Game
        Assert-True (!$guard.Ran -and (@($guard.Messages) -join '|') -ceq 'a save handoff restart is in progress' -and (Get-PlayFilesPresent $hA) -eq 4 -and (Get-SessionJson $hA).state -eq 'active') 'cleanup during the restart does nothing'
        Assert-True ((Test-CoopHandoffClaimLive -Session (Get-SessionJson $hA)) -and (Test-CoopHandoffPending -Session (Get-SessionJson $hA))) 'the claim is live and the restart pending'
        # A second request in the same session: limit.
        $stA2 = New-HandoffStage $hA.CoopSavesHandoff '0A1B2C3E' @{ '287700/local/TPP_GAME_DATA0' = 'host progress later' }
        $null = New-HandoffRequest $hA.CoopSavesHandoff $stA2 4343 '2026-10-15T12:10:00Z'
        $res = Invoke-CoopHandoffAfterExit -GameRoot $hA.Game -ExitedPid 4343 -ExitedStartUtc '2026-10-15T12:10:00Z' -Owner console
        Assert-True ($res.Action -eq 'limit' -and (@($res.Messages) -join ' ') -ceq "The host's save was already applied once this session; the newer one was not used.") "a second handoff in one session: limit ($($res.Action))"
        Assert-True (!(Test-Path -LiteralPath (Join-Path $hA.CoopSavesHandoff 'request.json')) -and !(Test-Path -LiteralPath $stA2.Stage) -and (Get-HandoffField $hA 'state') -eq 'limit' -and (Get-HandoffField $hA 'count') -eq '1') 'limit: request and stage deleted, state limit, count stays 1'
        $done = Invoke-CoopPlayCleanup -GameRoot $hA.Game
        Assert-True ($done.Ran -and (Get-PlayFilesPresent $hA) -eq 0 -and (Get-SessionJson $hA).state -eq 'off' -and (Test-Path -LiteralPath (Join-Path $hA.CoopSavesHandoff 'applied.json'))) 'without a live claim the cleanup removes every play file (the new lab.ini hash); applied.json stays'
        # -Force skips the guard.
        $hF = New-HandoffSession 'handoff-force'
        $stF = New-HandoffStage $hF.CoopSavesHandoff '1A2B3C4D' @{ '287700/local/TPP_GAME_DATA0' = 'host progress f' }
        $null = New-HandoffRequest $hF.CoopSavesHandoff $stF 4444 '2026-10-15T11:59:00Z'
        Assert-Equal (Invoke-CoopHandoffAfterExit -GameRoot $hF.Game -ExitedPid 4444 -ExitedStartUtc $exitedStart -Owner console).Action 'relaunched' 'a second fixture applies'
        Assert-True (!(Invoke-CoopPlayCleanup -GameRoot $hF.Game).Ran) 'the guard holds'
        $forced = Invoke-CoopPlayCleanup -GameRoot $hF.Game -Force
        Assert-True ($forced.Ran -and (Get-PlayFilesPresent $hF) -eq 0 -and (Get-SessionJson $hF).state -eq 'off') '-Force cleans up every play file during a claim'
        # A lab.ini still holding the old text (a crash between the record and the write) is ours as well.
        $hG = New-HandoffSession 'handoff-prev'
        $stG = New-HandoffStage $hG.CoopSavesHandoff '2B3C4D5E' @{ '287700/local/TPP_GAME_DATA0' = 'host progress g' }
        $null = New-HandoffRequest $hG.CoopSavesHandoff $stG 4545 '2026-10-15T11:59:00Z'
        $labG = [IO.File]::ReadAllBytes($hG.LabIni)
        Assert-Equal (Invoke-CoopHandoffAfterExit -GameRoot $hG.Game -ExitedPid 4545 -ExitedStartUtc $exitedStart -Owner console).Action 'relaunched' 'a third fixture applies'
        [IO.File]::WriteAllBytes($hG.LabIni, $labG)
        $prevClean = Invoke-CoopPlayCleanup -GameRoot $hG.Game -Force
        Assert-True ($prevClean.Ran -and (@($prevClean.Removed) -contains 'lab.ini') -and (Get-PlayFilesPresent $hG) -eq 0) 'the cleanup accepts the old lab.ini hash (prev_sha256)'
        # Online 2026-10-22 P5: Steam still marks MGSV as running after the exit (it ignored the
        # request for 3 minutes in the session): the relaunch waits for the flag to clear.
        $hS = New-HandoffSession 'handoff-steam-wait'
        $stS = New-HandoffStage $hS.CoopSavesHandoff '3C4D5E6F' @{ '287700/local/TPP_GAME_DATA0' = 'host progress s' }
        $null = New-HandoffRequest $hS.CoopSavesHandoff $stS 4646 '2026-10-15T11:59:00Z'
        [IO.File]::WriteAllText($steamRunningFile, "1`n")
        $flip = Start-Job -ScriptBlock { param($f) Start-Sleep -Milliseconds 2500; [IO.File]::WriteAllText($f, "0`n") } -ArgumentList $steamRunningFile
        try {
            $waitStart = [DateTime]::UtcNow
            Assert-Equal (Invoke-CoopHandoffAfterExit -GameRoot $hS.Game -ExitedPid 4646 -ExitedStartUtc $exitedStart -Owner console).Action 'relaunched' 'applied once Steam cleared its running flag'
            $waitedMs = ([DateTime]::UtcNow - $waitStart).TotalMilliseconds
        } finally { $null = Wait-Job $flip -Timeout 30; Remove-Job $flip -Force; [IO.File]::WriteAllText($steamRunningFile, "0`n") }
        $logS = [IO.File]::ReadAllText($hS.PlayCoopLog)
        $waitLine = [regex]::Match($logS, 'steam_wait; stopped=1; known=1; ms=([0-9]+); last=0')
        Assert-True ($waitedMs -ge 2400 -and $waitLine.Success -and [int]$waitLine.Groups[1].Value -ge 2400 -and $logS.IndexOf('steam_wait') -lt $logS.IndexOf('relaunch=requested')) "the relaunch waited for Steam's flag ($([int]$waitedMs) ms): $logS"
        $null = Invoke-CoopPlayCleanup -GameRoot $hS.Game -Force

        # 3. A waiting Steam joiner with link-rejoin.txt: net_peer_steam and link-secret.txt, both hashes recorded.
        $hB = New-HandoffSession 'handoff-rejoin' -SteamWait
        $rejoinSecret = 'ABCDE12345FGHJK67890'
        [IO.File]::WriteAllText($hB.LinkRejoin, (New-CoopSteamJoinCode -Secret $rejoinSecret -SteamId $hostSteamId) + "`r`n")
        $stB = New-HandoffStage $hB.CoopSavesHandoff '1B2C3D4E' @{ '287700/local/TPP_GAME_DATA1' = 'host progress 1' }
        $null = New-HandoffRequest $hB.CoopSavesHandoff $stB 5151 '2026-10-15T11:59:00Z' -Rejoin 1
        $res = Invoke-CoopHandoffAfterExit -GameRoot $hB.Game -ExitedPid 5151 -ExitedStartUtc $exitedStart -Owner console
        Assert-Equal $res.Action 'relaunched' "a waiting Steam joiner: applied ($(@($res.Messages) -join ' '))"
        Assert-Equal ([IO.File]::ReadAllText($hB.LabIni)) (New-CoopPlayLabIniText -Role guest -SaveDir $hB.CoopSaves -Transport steam -SteamSelf $guestSteamId -SteamPeer $hostSteamId -SaveHandoff off) 'the rejoin lab.ini names the host (net_peer_steam), no net_steam_wait'
        Assert-Equal ([IO.File]::ReadAllText($hB.LinkSecret)) "$rejoinSecret`r`n" 'link-secret.txt from link-rejoin.txt'
        $logB = [IO.File]::ReadAllText($hB.PlayCoopLog)
        Assert-True ($logB.Contains('handoff=applied;') -and !$logB.Contains($rejoinSecret) -and !$logB.Contains($hostSteamId) -and $logB -notmatch 'MGS[12]-') 'play-coop.log holds no code, secret or SteamID'
        $sB = Get-SessionJson $hB
        Assert-Equal "$((@($sB.files | Where-Object { $_.name -eq 'lab.ini' })[0]).sha256)|$((@($sB.files | Where-Object { $_.name -eq 'link-secret.txt' })[0]).sha256)" "$(Get-Sha256 $hB.LabIni)|$(Get-Sha256 $hB.LinkSecret)" 'both new hashes are recorded'
        Assert-True (!(Test-Path -LiteralPath $hB.LinkRejoin) -and ![IO.File]::ReadAllText($hB.PlaySession).Contains($rejoinSecret)) 'link-rejoin.txt is deleted; no secret in play-session.json'
        Assert-Equal "$($sB.lab.steam_wait)|$($sB.lab.steam_peer)|$($sB.lab.save_handoff)" "False|$hostSteamId|off" 'the recorded settings name the host'
        # A dead claimant lifts the guard: the cleanup removes the four files, link-secret.txt included.
        $deadProc = Start-Process -FilePath $env:ComSpec -ArgumentList '/c', 'exit' -WindowStyle Hidden -PassThru
        $deadProc.WaitForExit()
        $deadTable = ConvertTo-CoopSessionTable -Session $sB
        $deadHandoff = ConvertTo-CoopHandoffTable $sB.handoff
        $deadHandoff['claimed_by_pid'] = $deadProc.Id
        $deadTable['handoff'] = $deadHandoff
        Save-CoopPlaySession -GameRoot $hB.Game -Session $deadTable
        $c = Invoke-CoopPlayCleanup -GameRoot $hB.Game
        Assert-True ($c.Ran -and (Get-PlayFilesPresent $hB) -eq 0 -and (@($c.Removed) -contains 'link-secret.txt')) "a dead claimant: the cleanup removes every play file ($(@($c.Removed) -join ','))"

        # 4. Other processes: a live claimant elsewhere, the watchdog's 20 s defer, the restarted game.
        $hC = New-HandoffSession 'handoff-claims'
        $stC = New-HandoffStage $hC.CoopSavesHandoff '2C3D4E5F' @{ '287700/local/TPP_GAME_DATA0' = 'host progress c' }
        $null = New-HandoffRequest $hC.CoopSavesHandoff $stC 6161 '2026-10-15T11:59:00Z'
        $sleeper = Start-Process -FilePath $winPs -ArgumentList '-NoProfile', '-Command', 'Start-Sleep -Seconds 300' -WindowStyle Hidden -PassThru
        $claimTable = ConvertTo-CoopSessionTable -Session (Get-SessionJson $hC)
        $claimTable['handoff'] = [ordered]@{ state = 'applying'; claimed_by_pid = $sleeper.Id; claimed_start_utc = (Get-CoopProcessStartUtc -Id $sleeper.Id); claimed_utc = [DateTime]::UtcNow.ToString('o')
            xfer = '2C3D4E5F'; set_sha256 = $stC.Set; count = 0; aside = ''; message = '' }
        Save-CoopPlaySession -GameRoot $hC.Game -Session $claimTable
        $res = Invoke-CoopHandoffAfterExit -GameRoot $hC.Game -ExitedPid 6161 -ExitedStartUtc $exitedStart -Owner console
        Assert-True ($res.Action -eq 'claimed_by_other' -and (Test-Path -LiteralPath (Join-Path $hC.CoopSavesHandoff 'request.json'))) "a live claimant elsewhere: claimed_by_other ($($res.Action))"
        Assert-True (!(Invoke-CoopPlayCleanup -GameRoot $hC.Game).Ran -and (Get-PlayFilesPresent $hC) -eq 4) 'the cleanup refuses while another process applies'
        Assert-True (Test-CoopHandoffPending -Session (Get-SessionJson $hC)) 'applying with a live claimant is pending'
        Stop-Process -Id $sleeper.Id -Force
        $null = $sleeper.WaitForExit(5000)
        Assert-True (!(Test-CoopHandoffPending -Session (Get-SessionJson $hC)) -and !(Test-CoopHandoffClaimLive -Session (Get-SessionJson $hC))) 'applying with a dead claimant: neither pending nor live'
        $res = Invoke-CoopHandoffAfterExit -GameRoot $hC.Game -ExitedPid 6161 -ExitedStartUtc $exitedStart -Owner watchdog -LauncherAlive -ExitSeenUtc ([DateTime]::UtcNow)
        Assert-True ($res.Action -eq 'deferred' -and (Test-Path -LiteralPath (Join-Path $hC.CoopSavesHandoff 'request.json'))) "a watchdog with the launcher alive defers ($($res.Action))"
        $res = Invoke-CoopHandoffAfterExit -GameRoot $hC.Game -ExitedPid 6161 -ExitedStartUtc $exitedStart -Owner watchdog -LauncherAlive -ExitSeenUtc ([DateTime]::UtcNow.AddSeconds(-25))
        Assert-Equal $res.Action 'relaunched' 'the watchdog applies when the live launcher did not act within 20 s'
        Assert-True (Set-CoopPlayGame -GameRoot $hC.Game -GamePid 7171 -GameStartUtc '2026-10-15T12:05:00Z') 'the restarted game marks the handoff relaunched'
        Assert-Equal "$(Get-HandoffField $hC 'state')|$((Get-SessionJson $hC).game_pid)" 'relaunched|7171' 'state relaunched, the new game recorded'
        Assert-True (!(Test-CoopHandoffPending -Session (Get-SessionJson $hC)) -and (Invoke-CoopPlayCleanup -GameRoot $hC.Game).Ran -and (Get-PlayFilesPresent $hC) -eq 0) 'after the restarted game the cleanup runs'

        # 5. Stale requests (another game start, too old, a changed stage): deleted with their stage, nothing applied.
        $hD = New-HandoffSession 'handoff-stale'
        $treeD = Get-RelativeTreeHashes $hD.CoopSaves
        foreach ($case in @(@('pid', 8182, '2026-10-15T11:59:00Z', 0), @('start', 8181, '2026-10-15T11:58:00Z', 0), @('age', 8181, '2026-10-15T11:59:00Z', -31), @('stage', 8181, '2026-10-15T11:59:00Z', 0))) {
            $stD = New-HandoffStage $hD.CoopSavesHandoff '3D4E5F60' @{ '287700/local/TPP_GAME_DATA0' = 'host progress d' }
            $null = New-HandoffRequest $hD.CoopSavesHandoff $stD $case[1] $case[2] ([DateTime]::UtcNow.AddMinutes($case[3]))
            if ($case[0] -eq 'stage') { [IO.File]::WriteAllText((Join-Path $stD.Stage '287700\local\TPP_GAME_DATA0'), 'host progress X') }
            $res = Invoke-CoopHandoffAfterExit -GameRoot $hD.Game -ExitedPid 8181 -ExitedStartUtc $exitedStart -Owner console
            Assert-True ($res.Action -eq 'stale' -and !(Test-Path -LiteralPath (Join-Path $hD.CoopSavesHandoff 'request.json')) -and !(Test-Path -LiteralPath $stD.Stage)) "a stale request ($($case[0])): deleted with its stage ($($res.Action): $(@($res.Messages) -join ' '))"
        }
        Assert-True ((Get-RelativeTreeHashes $hD.CoopSaves) -eq $treeD -and (Get-SessionJson $hD).state -eq 'active' -and (Get-PlayFilesPresent $hD) -eq 4) 'stale requests change neither the co-op copy nor the session'
        Assert-Equal (Invoke-CoopHandoffAfterExit -GameRoot $hD.Game -ExitedPid 8181 -ExitedStartUtc $exitedStart -Owner console).Action 'none' 'no request: none'

        # 6. A failure before the apply (lab.ini changed): nothing applied, the request goes, state failed.
        $hE = New-HandoffSession 'handoff-fail'
        $treeE = Get-RelativeTreeHashes $hE.CoopSaves
        [IO.File]::AppendAllText($hE.LabIni, "partner_lost=hide`n")
        $stE = New-HandoffStage $hE.CoopSavesHandoff '4E5F6071' @{ '287700/local/TPP_GAME_DATA0' = 'host progress e' }
        $null = New-HandoffRequest $hE.CoopSavesHandoff $stE 9191 '2026-10-15T11:59:00Z'
        $res = Invoke-CoopHandoffAfterExit -GameRoot $hE.Game -ExitedPid 9191 -ExitedStartUtc $exitedStart -Owner console
        Assert-Equal "$($res.Action)|$(@($res.Messages) -join ' ')" "failed|Could not apply the host's save: mgs-coop\lab.ini changed after Play-Coop wrote it. Your co-op save is unchanged." 'a failure is reported'
        Assert-True ((Get-RelativeTreeHashes $hE.CoopSaves) -eq $treeE -and !(Test-Path -LiteralPath (Join-Path $hE.CoopSavesHandoff 'request.json')) -and (Get-HandoffField $hE 'state') -eq 'failed') 'a failure: the co-op copy unchanged, the request deleted, state failed'
        Assert-True (!(Test-CoopHandoffClaimLive -Session (Get-SessionJson $hE))) 'a failed handoff holds no claim (the caller cleans up)'
        # What an unfinished handoff leaves is removed by the next Play-Coop (not while a session is live).
        $null = Invoke-CoopPlayCleanup -GameRoot $hE.Game
        [IO.File]::WriteAllText($hE.LinkRejoin, (New-CoopSteamJoinCode -Secret $rejoinSecret -SteamId $hostSteamId) + "`r`n")
        $null = New-HandoffStage $hE.CoopSavesHandoff '5F607182.partial' @{ '287700/local/TPP_GAME_DATA0' = 'half a host save' }
        $null = New-HandoffRequest $hE.CoopSavesHandoff $stE 9292 '2026-10-15T11:59:00Z'
        [IO.File]::WriteAllText((Join-Path $hE.CoopSavesHandoff 'provenance.json'), '{"format":"mgs-coop-save-provenance","v":1,"entries":[]}')
        [IO.File]::WriteAllText((Join-Path $hE.CoopSavesHandoff 'notes.txt'), 'not ours')
        $stale = Remove-CoopStaleHandoff -GameRoot $hE.Game
        Assert-Equal ((@($stale.Removed) | Sort-Object) -join ',') 'coop-saves-handoff/4E5F6071,coop-saves-handoff/5F607182.partial,coop-saves-handoff/request.json,link-rejoin.txt' 'stale handoff files removed'
        Assert-Equal (@($stale.Messages) -join '|') 'A host save from an earlier session was not applied and was removed.' 'the preflight sentence'
        Assert-True ((Test-Path -LiteralPath (Join-Path $hE.CoopSavesHandoff 'provenance.json')) -and (Test-Path -LiteralPath (Join-Path $hE.CoopSavesHandoff 'notes.txt'))) 'provenance.json and unknown files stay'
        Assert-Equal @((Remove-CoopStaleHandoff -GameRoot $hE.Game).Removed).Count 0 'nothing left to remove'
        # A host session is never handed off.
        $hH = New-HandoffSession 'handoff-host'
        $hostTable = ConvertTo-CoopSessionTable -Session (Get-SessionJson $hH)
        $hostTable['role'] = 'host'
        Save-CoopPlaySession -GameRoot $hH.Game -Session $hostTable
        $stH = New-HandoffStage $hH.CoopSavesHandoff '6F708193' @{ '287700/local/TPP_GAME_DATA0' = 'never' }
        $null = New-HandoffRequest $hH.CoopSavesHandoff $stH 9393 '2026-10-15T11:59:00Z'
        Assert-True ((Invoke-CoopHandoffAfterExit -GameRoot $hH.Game -ExitedPid 9393 -ExitedStartUtc $exitedStart -Owner console).Action -eq 'none' -and (Test-Path -LiteralPath $stH.Stage)) 'a host session: none'
        # A live session (its launcher runs) keeps its handoff files.
        $sleeper = Start-Process -FilePath $winPs -ArgumentList '-NoProfile', '-Command', 'Start-Sleep -Seconds 120' -WindowStyle Hidden -PassThru
        $hostTable['launcher_pid'] = $sleeper.Id
        $hostTable['launcher_start_utc'] = Get-CoopProcessStartUtc -Id $sleeper.Id
        Save-CoopPlaySession -GameRoot $hH.Game -Session $hostTable
        Assert-True (@((Remove-CoopStaleHandoff -GameRoot $hH.Game).Removed).Count -eq 0 -and (Test-Path -LiteralPath $stH.Stage)) 'a live session keeps its handoff files'
        if ($sleeper -and !$sleeper.HasExited) { Stop-Process -Id $sleeper.Id -Force; $null = $sleeper.WaitForExit(5000) }

        # 7. Review SS-3: link-rejoin.txt (the host's join code) never outlives the session.
        $hR = New-HandoffSession 'handoff-rejoin-stale' -SteamWait
        [IO.File]::WriteAllText($hR.LinkRejoin, (New-CoopSteamJoinCode -Secret $rejoinSecret -SteamId $hostSteamId) + "`r`n")
        $stR = New-HandoffStage $hR.CoopSavesHandoff '7A8B9CAD' @{ '287700/local/TPP_GAME_DATA0' = 'host progress r' }
        $null = New-HandoffRequest $hR.CoopSavesHandoff $stR 8282 '2026-10-15T11:59:00Z' -Rejoin 1
        $res = Invoke-CoopHandoffAfterExit -GameRoot $hR.Game -ExitedPid 8281 -ExitedStartUtc $exitedStart -Owner console
        Assert-True ($res.Action -eq 'stale' -and !(Test-Path -LiteralPath $hR.LinkRejoin)) "a stale request (another game start) removes link-rejoin.txt too ($($res.Action))"
        Assert-Equal (Invoke-CoopHandoffAfterExit -GameRoot $hR.Game -ExitedPid 0 -ExitedStartUtc $exitedStart -Owner console).Action 'none' 'an unknown exited game: none'
        [IO.File]::WriteAllText($hR.LinkRejoin, (New-CoopSteamJoinCode -Secret $rejoinSecret -SteamId $hostSteamId) + "`r`n")
        Assert-Equal (Invoke-CoopHandoffAfterExit -GameRoot $hR.Game -ExitedPid 0 -ExitedStartUtc $exitedStart -Owner console).Action 'none' 'none again'
        Assert-True (Test-Path -LiteralPath $hR.LinkRejoin) 'a none outside the mutex never deletes link-rejoin.txt (the helper may still apply it)'
        $cR = Invoke-CoopPlayCleanup -GameRoot $hR.Game
        Assert-True ($cR.Ran -and (@($cR.Removed) -contains 'link-rejoin.txt') -and !(Test-Path -LiteralPath $hR.LinkRejoin) -and (Get-SessionJson $hR).state -eq 'off') "the session's cleanup removes link-rejoin.txt ($(@($cR.Removed) -join ','))"
        # A failed apply (lab.ini changed) of a waiting Steam joiner: no rejoin follows, the code goes.
        $hRf = New-HandoffSession 'handoff-rejoin-fail' -SteamWait
        [IO.File]::AppendAllText($hRf.LabIni, "partner_lost=hide`n")
        [IO.File]::WriteAllText($hRf.LinkRejoin, (New-CoopSteamJoinCode -Secret $rejoinSecret -SteamId $hostSteamId) + "`r`n")
        $stRf = New-HandoffStage $hRf.CoopSavesHandoff '7B8C9DAE' @{ '287700/local/TPP_GAME_DATA0' = 'host progress rf' }
        $null = New-HandoffRequest $hRf.CoopSavesHandoff $stRf 8383 '2026-10-15T11:59:00Z' -Rejoin 1
        $res = Invoke-CoopHandoffAfterExit -GameRoot $hRf.Game -ExitedPid 8383 -ExitedStartUtc $exitedStart -Owner console
        Assert-True ($res.Action -eq 'failed' -and !(Test-Path -LiteralPath $hRf.LinkRejoin) -and !(Test-Path -LiteralPath $hRf.LinkSecret) -and
            !@(Get-ChildItem -LiteralPath $hRf.Mod -Filter 'link-secret.txt.*.tmp' -Force).Count) "a failed apply removes link-rejoin.txt and leaves no secret behind ($($res.Action))"
        # Under a live claim the guarded cleanup keeps it (the restarted game rejoins with it); -Force removes it.
        $hRc = New-HandoffSession 'handoff-rejoin-claim' -SteamWait
        $sleeper = Start-Process -FilePath $winPs -ArgumentList '-NoProfile', '-Command', 'Start-Sleep -Seconds 300' -WindowStyle Hidden -PassThru
        $claimRc = ConvertTo-CoopSessionTable -Session (Get-SessionJson $hRc)
        $claimRc['handoff'] = [ordered]@{ state = 'relaunching'; claimed_by_pid = $sleeper.Id; claimed_start_utc = (Get-CoopProcessStartUtc -Id $sleeper.Id); claimed_utc = [DateTime]::UtcNow.ToString('o')
            xfer = '7C8DAEBF'; set_sha256 = ('A' * 64); count = 1; aside = ''; message = '' }
        Save-CoopPlaySession -GameRoot $hRc.Game -Session $claimRc
        [IO.File]::WriteAllText($hRc.LinkRejoin, (New-CoopSteamJoinCode -Secret $rejoinSecret -SteamId $hostSteamId) + "`r`n")
        Assert-True (!(Invoke-CoopPlayCleanup -GameRoot $hRc.Game).Ran -and (Test-Path -LiteralPath $hRc.LinkRejoin)) 'a live claim: the guarded cleanup keeps link-rejoin.txt'
        $forcedRc = Invoke-CoopPlayCleanup -GameRoot $hRc.Game -Force
        Assert-True ($forcedRc.Ran -and (@($forcedRc.Removed) -contains 'link-rejoin.txt') -and !(Test-Path -LiteralPath $hRc.LinkRejoin)) '-Force removes it'

        # 8. Review SS-4: the console's run-1 wait (Get-CoopRelaunchWait) stops once co-op is off or
        #    the restart failed, and never while the restart is pending.
        $hW = New-HandoffSession 'handoff-relaunch-wait'
        $wId = [string](Get-SessionJson $hW).session_id
        $setW = {
            param([string]$State, [string]$Message = '', [string]$Aside = '', [int]$ClaimPid = 0, [string]$ClaimStart = '')
            $t = ConvertTo-CoopSessionTable -Session (Get-SessionJson $hW)
            $t['handoff'] = [ordered]@{ state = $State; claimed_by_pid = $ClaimPid; claimed_start_utc = $ClaimStart; claimed_utc = [DateTime]::UtcNow.AddSeconds(-10).ToString('o')
                xfer = '7D8EBFC0'; set_sha256 = ('B' * 64); count = 1; aside = $Aside; message = $Message }
            Save-CoopPlaySession -GameRoot $hW.Game -Session $t
        }
        foreach ($pendingState in @('relaunching', 'relaunched')) {
            & $setW $pendingState
            $w = Get-CoopRelaunchWait -GameRoot $hW.Game -SessionId $wId
            Assert-True (!$w.Stop -and $null -ne $w.ClaimedUtc -and ([DateTime]::UtcNow - $w.ClaimedUtc).TotalSeconds -ge 9) "$pendingState`: keep waiting, the claim time known"
        }
        & $setW 'applying' '' '' $sleeper.Id (Get-CoopProcessStartUtc -Id $sleeper.Id)
        Assert-True (!(Get-CoopRelaunchWait -GameRoot $hW.Game -SessionId $wId).Stop) 'applying with a live claimant: keep waiting'
        & $setW 'failed' 'mgs-coop\coop.ini changed' '20261015-120000-before-handoff'
        $w = Get-CoopRelaunchWait -GameRoot $hW.Game -SessionId $wId
        Assert-Equal "$($w.Stop)|$($w.Off)|$(@($w.Lines) -join ' ')" ("True|False|MGSV could not be started again: mgs-coop\coop.ini changed. Do not start MGS V from Steam now; start Play-Coop.bat again. " +
            'Your previous co-op save is kept at mgs-coop\coop-saves-replaced\20261015-120000-before-handoff (Share-Save.bat [R] brings it back).') 'failed: stop, the reason and the kept copy'
        & $setW 'limit'
        Assert-True ((Get-CoopRelaunchWait -GameRoot $hW.Game -SessionId $wId).Stop) 'limit: stop'
        Assert-True ((Get-CoopRelaunchWait -GameRoot $hW.Game -SessionId 'another').Off) 'another session: co-op off for this window'
        $null = Invoke-CoopPlayCleanup -GameRoot $hW.Game -Force
        $w = Get-CoopRelaunchWait -GameRoot $hW.Game -SessionId $wId
        Assert-True ($w.Stop -and $w.Off -and (@($w.Lines) -join ' ') -ceq 'Co-op was switched off while MGSV was restarting (the helper could not start it again); do not start MGS V from Steam now. Start Play-Coop.bat again.') 'off: stop, never a manual start'
        # Online 2026-10-22 (the P5 restart wait): a read that races a write of play-session.json
        # (the helper's File.Replace) is no switch-off. The file is held exclusively for about 0.6 s.
        $hX = New-HandoffSession 'handoff-relaunch-race'
        $xId = [string](Get-SessionJson $hX).session_id
        $tX = ConvertTo-CoopSessionTable -Session (Get-SessionJson $hX)
        $tX['handoff'] = [ordered]@{ state = 'relaunching'; claimed_by_pid = 0; claimed_start_utc = ''; claimed_utc = [DateTime]::UtcNow.ToString('o')
            xfer = '7D8EBFC1'; set_sha256 = ('B' * 64); count = 1; aside = ''; message = '' }
        Save-CoopPlaySession -GameRoot $hX.Game -Session $tX
        $raceFlag = Join-Path $fixture 'race-locked.flag'
        $lockJob = Start-Job -ScriptBlock { param($p, $flag) $f = [IO.File]::Open($p, 'Open', 'Read', 'None'); [IO.File]::WriteAllText($flag, '1'); Start-Sleep -Milliseconds 600; $f.Dispose() } -ArgumentList $hX.PlaySession, $raceFlag
        try {
            Assert-True (Wait-Until { Test-Path -LiteralPath $raceFlag } 30) 'the session file is held by the stand-in writer'
            $w = Get-CoopRelaunchWait -GameRoot $hX.Game -SessionId $xId
            Assert-True (!$w.Stop -and !$w.Off) 'a session read that races a write keeps waiting (no false switch-off)'
        } finally { $null = Wait-Job $lockJob -Timeout 30; Remove-Job $lockJob -Force }
        $null = Invoke-CoopPlayCleanup -GameRoot $hX.Game -Force
    } finally {
        Remove-Item Env:MGS_COOP_TEST_NO_LAUNCH -ErrorAction SilentlyContinue
        Remove-Item Env:MGS_COOP_TEST_RELAUNCH_DELAY_SEC -ErrorAction SilentlyContinue
        if ($sleeper -and !$sleeper.HasExited) { Stop-Process -Id $sleeper.Id -Force -ErrorAction SilentlyContinue }
    }

    # ------------------------------------------------------------ the developer's local two-window test (tools/dev; never shipped)
    # A drop-in package as tools/package.ps1 -Friend stages it, with fixture game
    # hashes and a newer inert loader; dedicated inert copies; MGS_COOP_TEST_NO_LAUNCH
    # checks the activation and restores it without starting a game.
    $loader2 = Join-Path $fixture 'loader2.dll'; [IO.File]::WriteAllText($loader2, 'INERT drop-in package loader')
    $devPkg = Join-Path $fixture 'dev-package'
    Copy-Item -LiteralPath $pkg -Destination $devPkg -Recurse
    [IO.File]::Copy($loader2, (Join-Path $devPkg 'dinput8.dll'), $true)
    $devManifest = Get-Content -LiteralPath (Join-Path $devPkg 'mgs-coop/package.json') -Raw | ConvertFrom-Json
    $devManifest.executable_sha256 = $exeHash; $devManifest.steam_dll_sha256 = $steamHash; $devManifest.runtime_archive.sha256 = Get-Sha256 $archiveFx
    ($devManifest.files | Where-Object { $_.target -ceq 'dinput8.dll' }).sha256 = Get-Sha256 $loader2
    [IO.File]::WriteAllText((Join-Path $devPkg 'mgs-coop/package.json'), ($devManifest | ConvertTo-Json -Depth 6) + "`n", $utf8)
    $startLocal = Join-Path $projectRoot 'tools/dev/Start-LocalCoop.ps1'
    $lhp = New-InstalledGame 'local-host' -Record
    $lgp = New-InstalledGame 'local-host_guest'
    $localHost = $lhp.Game; $localGuest = $lgp.Game
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $lgp.Mod 'friend-install.json'))) 'the local guest needs only the owned core install'
    # Reuse the existing validated retail graphics fixture, never a real save.
    $graphicsTokens = $null; $graphicsErrors = $null
    $graphicsAst = [Management.Automation.Language.Parser]::ParseFile((Join-Path $projectRoot 'tests/lab_scripts_tests.ps1'), [ref]$graphicsTokens, [ref]$graphicsErrors)
    $graphicsLiterals = @($graphicsAst.FindAll({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$realConfig'}, $true))
    Assert-True (@($graphicsErrors).Count -eq 0 -and $graphicsLiterals.Count -eq 1) 'local test reuses the one checked graphics fixture'
    $localGraphics = & ([scriptblock]::Create($graphicsLiterals[0].Right.Extent.Text))
    Assert-True (Get-GraphicsConfigInfo $localGraphics).Valid 'local source graphics fixture has the retail tag'
    [IO.File]::WriteAllText((Join-Path $lhp.CoopSaves '287700/local/TPP_GRAPHICS_CONFIG'), $localGraphics, $utf8)
    $localBaseline = [ordered]@{
        'lab.ini' = "# Generated by tools/lab/Prepare-LabInstance.ps1 - single-PC lab harness only.`nenabled=1`nmode=lab`nrole=guest`nloopback=1`n"
        'coop.ini' = "character=quiet`nmaster_url=http://127.0.0.1:47899/tppstm/gate`n"
        'link-secret.txt' = "$s7`r`n"
        'script-observe.enabled' = "offline-v1`n"
        'client-host.txt' = "76561198012345678`n"
    }
    foreach ($name in $localBaseline.Keys) { [IO.File]::WriteAllText((Join-Path $lgp.Mod $name), $localBaseline[$name], $utf8) }
    $localNormalSaves = Get-TreeHashes $lhp.CoopSaves
    $localRealSaves = Get-TreeHashes $userdata
    $localArgs = @('-GameDirectory', $localHost, '-GuestDirectory', $localGuest, '-PackageDirectory', $devPkg, '-NonInteractive')
    $env:MGS_COOP_TEST_NO_LAUNCH = '1'
    $env:MGS_COOP_TEST_NO_WATCHDOG = '1'
    try {
        # Refusals change nothing.
        $beforeHost = Get-TreeHashes $localHost; $beforeGuest = Get-TreeHashes $localGuest
        $r = Invoke-Friend $startLocal @('-GameDirectory', $localHost, '-GuestDirectory', $localHost, '-PackageDirectory', $devPkg, '-NonInteractive')
        Assert-Refused $r 'Choose two separate game folders' 'local test rejects using the same game twice'
        Assert-Equal (Get-TreeHashes $localHost) $beforeHost 'same-folder refusal preserves the host'
        Assert-Equal (Get-TreeHashes $localGuest) $beforeGuest 'same-folder refusal preserves the guest'
        [IO.File]::WriteAllText((Join-Path $lgp.Mod 'worker.enabled'), 'INERT foreign developer marker', $utf8)
        $r = Invoke-Friend $startLocal $localArgs
        Assert-Refused $r 'belongs to a developer test' 'local play leaves an active developer fixture alone'
        Assert-Equal (Get-TreeHashes $localHost) $beforeHost 'developer refusal preserves host configuration and saves'
        Remove-Item -LiteralPath (Join-Path $lgp.Mod 'worker.enabled')
        Assert-Equal (Get-TreeHashes $localGuest) $beforeGuest 'developer refusal preserves guest configuration and saves'
        $originalGuestLoader = [IO.File]::ReadAllBytes($lgp.Loader)
        [IO.File]::WriteAllText($lgp.Loader, 'INERT unrelated loader', $utf8)
        $beforeGuest = Get-TreeHashes $localGuest
        $r = Invoke-Friend $startLocal $localArgs
        Assert-Refused $r 'no matching owned co-op loader' 'local play refuses a guest loader that is neither owned nor the package one'
        Assert-Equal (Get-TreeHashes $localHost) $beforeHost 'loader refusal preserves the other game before activation'
        Assert-Equal (Get-TreeHashes $localGuest) $beforeGuest 'loader refusal never replaces an unrelated guest loader'
        [IO.File]::WriteAllBytes($lgp.Loader, $originalGuestLoader)

        # Both owned loaders are upgraded to the package's through the owned installer
        # (a drop-in package: package.json in mgs-coop\, the launcher-only rows skipped).
        $r = Invoke-Friend $startLocal (@('-World', '3', '-Partner', 'native') + $localArgs)
        Assert-True ($r.Exit -eq 0 -and (Has $r 'TEST: local two-window activation checked; no games launched.')) "developer local test flow: $($r.Raw)"
        Assert-True (Has $r 'Both game folders returned to their previous setup') 'fixture launch path restores both baselines before returning'
        foreach ($p in @($lhp, $lgp)) {
            Assert-Equal (Get-Sha256 $p.Loader) (Get-Sha256 $loader2) "the owned loader was upgraded to the package's ($([IO.Path]::GetFileName($p.Game)))"
            $record = Get-Content -LiteralPath $p.Record -Raw | ConvertFrom-Json
            Assert-Equal "$($record.version)|$($record.dll_sha256)" "$packageVersion|$(Get-Sha256 $loader2)" 'the ownership record names the drop-in package'
            Assert-True (!(Test-Path -LiteralPath (Join-Path $p.Game 'DiamondDogsTogether.exe')) -and !(Test-Path -LiteralPath (Join-Path $p.Mod 'source.zip'))) 'the owned installer installs only the loader and its licences'
        }
        Assert-PassThrough $lhp 'local flow host returned to pass-through'
        foreach ($name in $localBaseline.Keys) { Assert-Equal ([IO.File]::ReadAllText((Join-Path $lgp.Mod $name), [Text.Encoding]::UTF8)) $localBaseline[$name] "local guest restores its earlier offline bytes $name" }
        $localHostSaves = Join-Path $lhp.Mod 'local-test-saves'
        $localGuestSaves = Join-Path $lgp.Mod 'local-test-saves'
        foreach ($save in @($localHostSaves, $localGuestSaves)) {
            Assert-Equal (Get-Sha256 (Join-Path $save '287700/local/TPP_GAME_DATA0')) (Get-Sha256 (Join-Path $lhp.CoopSaves '287700/local/TPP_GAME_DATA0')) 'local tests clone the host ordinary co-op save independently'
            $info = Get-GraphicsConfigInfo ([IO.File]::ReadAllText((Join-Path $save '287700/local/TPP_GRAPHICS_CONFIG')))
            Assert-True ($info.Valid -and $info.WindowMode -ceq 'Windowed' -and $info.Width -ge 640 -and $info.Height -ge 480) 'only the local test saves get tagged windowed graphics'
        }
        $localJournals = @(Get-ChildItem -LiteralPath (Join-Path $lhp.Mod 'local-test-runs') -Directory)
        Assert-Equal $localJournals.Count 1 'one local test journal'
        $localJournal = Get-Content -LiteralPath (Join-Path $localJournals[0].FullName 'session.json') -Raw | ConvertFrom-Json
        Assert-Equal $localJournal.state 'off' 'local activation journal is off after fixture cleanup'
        Assert-Equal @($localJournal.files).Count 8 'local activation records four files separately for each role'
        Assert-Equal @($localJournal.files | Where-Object { $_.role -ceq 'guest' -and $_.original_exists }).Count 4 'guest journal preserves all four earlier offline activation files'
        Assert-Equal @($localJournal.files | Where-Object { $_.phase -cne 'restored' }).Count 0 'all local activation entries are restored'
        Assert-True (![IO.File]::ReadAllText((Join-Path $localJournals[0].FullName 'session.json')).Contains($s7)) 'guest original secret is not printed as plain text in the local journal'
        # The host plays Snake (as with Diamond Dogs Together), the guest Quiet.
        foreach ($row in @(@('host', 'snake'), @('guest', 'quiet'))) {
            $want = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($utf8.GetBytes((New-CoopPlayCoopIniText -Character $row[1]))))
            $entry = @($localJournal.files | Where-Object { $_.role -ceq $row[0] -and $_.name -ceq 'coop.ini' })
            Assert-True ($entry.Count -eq 1 -and ([string]$entry[0].written_sha256) -ieq $want) "local test coop.ini: the $($row[0]) plays $($row[1])"
        }
        [IO.File]::WriteAllText((Join-Path $localGuestSaves '287700/local/LOCAL_PROGRESS'), 'guest local test progress', $utf8)
        $savedLocalHost = Get-TreeHashes $localHostSaves; $savedLocalGuest = Get-TreeHashes $localGuestSaves
        # A drag-and-drop install has no ownership record: the package's own loader is accepted.
        $recordAside = $lhp.Record + '.aside'
        Move-Item -LiteralPath $lhp.Record -Destination $recordAside
        try {
            $r = Invoke-Friend $startLocal @('-GameDirectory', $localHost, '-PackageDirectory', $devPkg, '-NonInteractive')
            Assert-True ($r.Exit -eq 0 -and (Has $r 'TEST: local two-window activation checked; no games launched.')) "a drop-in host (no ownership record) and the sibling _guest copy: $($r.Raw)"
        } finally { Move-Item -LiteralPath $recordAside -Destination $lhp.Record }
        Assert-Equal (Get-TreeHashes $localHostSaves) $savedLocalHost 'repeat local test retains the host local progress'
        Assert-Equal (Get-TreeHashes $localGuestSaves) $savedLocalGuest 'repeat local test retains the guest local progress'
        # The repository's Play-Offline-Coop.bat is the developer entry point (all arguments pass through).
        $turn = Enter-TestInstallTurn
        try {
            $batOut = '' | & cmd.exe /c (Join-Path $projectRoot 'Play-Offline-Coop.bat') -GameDirectory $localHost -GuestDirectory $localGuest -PackageDirectory $devPkg -NonInteractive 2>&1 | Out-String
        } finally { Exit-TestInstallTurn $turn }
        Assert-True ($LASTEXITCODE -eq 0 -and $batOut.Contains('TEST: local two-window activation checked; no games launched.')) "Play-Offline-Coop.bat runs the developer local test: $batOut"
        $localJournals = @(Get-ChildItem -LiteralPath (Join-Path $lhp.Mod 'local-test-runs') -Directory)
        Assert-Equal $localJournals.Count 3 'each local test run journals its own session'
        foreach ($journal in $localJournals) { Assert-Equal (Get-Content -LiteralPath (Join-Path $journal.FullName 'session.json') -Raw | ConvertFrom-Json).state 'off' 'each local run cleans up its session' }
        Assert-PassThrough $lhp 'all local runs leave the host pass-through'
        foreach ($name in $localBaseline.Keys) { Assert-Equal ([IO.File]::ReadAllText((Join-Path $lgp.Mod $name), [Text.Encoding]::UTF8)) $localBaseline[$name] "all local runs preserve guest baseline $name" }
        Assert-Equal (Get-TreeHashes $lhp.CoopSaves) $localNormalSaves 'the local test never changes the ordinary co-op progress or graphics'
        Assert-Equal (Get-TreeHashes $userdata) $localRealSaves 'the local test never reads or changes fixture real save data'
        # A live local launcher owns both folders before either game starts: the
        # owned installer leaves that journal, configuration and copied saves alone.
        & {
            Import-Module (Join-Path $projectRoot 'tools/dev/LocalCoopSession.psm1') -Force -DisableNameChecking
            $activeRun = Join-Path $lhp.Mod ('local-test-runs\' + [Guid]::NewGuid().ToString('N'))
            $liveLocal = New-LocalCoopSession -HostRoot $localHost -GuestRoot $localGuest -RunDirectory $activeRun
            try {
                $activeText = "# Owned inert local launcher fixture`nenabled=1`nmode=lab`n"
                foreach ($role in 'host', 'guest') {
                    Set-LocalCoopSessionFile -Session $liveLocal -Role $role -Name 'lab.ini' -Bytes $utf8.GetBytes($activeText)
                    Set-LocalCoopSessionFile -Session $liveLocal -Role $role -Name 'script-observe.enabled' -Bytes ([Text.Encoding]::ASCII.GetBytes("offline-v1`n"))
                }
                Assert-True (Test-LocalCoopSessionOwnerLive -Session $liveLocal) 'local activation fixture has an exact live launcher process identity'
                $activeHostHashes = Get-TreeHashes $localHost; $activeGuestHashes = Get-TreeHashes $localGuest
                $result = Invoke-Friend $coreInstaller @('-GameDirectory', $localGuest, '-PackageDirectory', $devPkg, '-NoPrompt', '-NoInitialMode')
                Assert-Refused $result 'local two-window session owns this folder' 'the owned installer refuses a live local launcher before mutation'
                $result = Invoke-Friend $startLocal $localArgs
                Assert-Refused $result 'still active' 'a second local test refuses a live local launcher'
                Assert-Equal (Get-TreeHashes $localHost) $activeHostHashes 'the refusals leave the entire host fixture unchanged'
                Assert-Equal (Get-TreeHashes $localGuest) $activeGuestHashes 'the refusals leave the entire guest fixture unchanged'
            } finally {
                $restored = Restore-LocalCoopSession -Session (Read-LocalCoopSession -RunDirectory $activeRun)
                Assert-Equal $restored.State 'off' 'the exact owner can clean up its own live local fixture after both games remain stopped'
            }
            Assert-PassThrough $lhp 'active local launcher regression restored host pass-through'
            foreach ($name in $localBaseline.Keys) { Assert-Equal ([IO.File]::ReadAllText((Join-Path $lgp.Mod $name), [Text.Encoding]::UTF8)) $localBaseline[$name] "active local launcher regression restores guest baseline $name" }
        }
    } finally {
        Remove-Item Env:MGS_COOP_TEST_NO_LAUNCH -ErrorAction SilentlyContinue
        Remove-Item Env:MGS_COOP_TEST_NO_WATCHDOG -ErrorAction SilentlyContinue
    }
    Write-Output "friend package tests passed: $script:checks checks (the Diamond Dogs Together package, the developer tools in tools/dev and tools/CoopPlay.psm1; scripts run under $winPs and $pwshPath; fixture only, no game, Steam or network)"
} finally {
    Start-Sleep -Milliseconds 300
    Remove-Item Env:MGS_COOP_TEST_STEAM_RUNNING_FILE -ErrorAction SilentlyContinue
    Remove-OwnedFixture
}
