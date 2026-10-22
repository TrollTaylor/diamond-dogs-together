#Requires -Version 7
# Tests for the two-PC play package (tools/friend, tools/CoopPlay.psm1,
# tools/package.ps1 -Friend; docs/two-pc-play.md).
# Everything runs inside an owned temporary folder under build/: fixture game
# folders hold inert stand-in files, a fixture Steam folder holds fixture saves.
# No real game folder, Steam folder, router, firewall setting or game process
# is touched, and nothing contacts the network (Play-Coop runs with
# -HostAddress, its optional steps answered No, and the launch skipped).
# The friend scripts run as child processes under Windows PowerShell 5.1 (what
# the .bat files start on a friend's PC) and, for one flow, PowerShell 7.
[CmdletBinding()]
param([string]$IniCheck, [string]$FakeProcess)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectRoot 'tools\worker\WorkerSource.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $projectRoot 'tools/lab/LabCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $projectRoot 'tools/friend/FriendCommon.psm1') -Force -DisableNameChecking
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
function Invoke-Friend([string]$Script, [string[]]$Arguments, [string]$HostExe = $winPs) {
    # Child process, like the .bat files; output flattened for message checks.
    foreach ($a in $Arguments) { if ($a -and [IO.Path]::IsPathRooted($a) -and $a -notmatch '^[0-9]') { Assert-InFixture $a } }
    $raw = & $HostExe -NoProfile -ExecutionPolicy Bypass -NonInteractive -File $Script @Arguments 2>&1 | Out-String
    $code = $LASTEXITCODE
    # Windows PowerShell 5.1 wraps error records at the console width, even
    # inside words: messages are compared with all whitespace removed.
    return [pscustomobject]@{ Exit = $code; Raw = $raw; Text = ($raw -replace '\s', '') }
}
function Invoke-FriendAnswers([string]$Script, [string[]]$Arguments, [string[]]$Answers) {
    # The prompt path: answers arrive on stdin, one per Read-Host
    # (MGS_COOP_TEST_INTERACTIVE makes the scripts treat redirected input as a console).
    foreach ($a in $Arguments) { if ($a -and [IO.Path]::IsPathRooted($a) -and $a -notmatch '^[0-9]') { Assert-InFixture $a } }
    $env:MGS_COOP_TEST_INTERACTIVE = '1'
    try { $raw = $Answers | & $winPs -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments 2>&1 | Out-String }
    finally { Remove-Item Env:MGS_COOP_TEST_INTERACTIVE -ErrorAction SilentlyContinue }
    $code = $LASTEXITCODE
    return [pscustomobject]@{ Exit = $code; Raw = $raw; Text = ($raw -replace '\s', '') }
}
function Invoke-Play([string]$Script, [string[]]$Arguments, [string[]]$Answers = @(''), [switch]$NoWatchdog, [switch]$Full) {
    # Play-Coop through its prompt path; it leaves right after the activation
    # (like a closed window) unless -Full, so no Steam launch ever happens.
    if (!$Full) { $env:MGS_COOP_TEST_EXIT_AFTER_ACTIVATE = '1' }
    if ($NoWatchdog) { $env:MGS_COOP_TEST_NO_WATCHDOG = '1' }
    try { return Invoke-FriendAnswers $Script $Arguments $Answers }
    finally {
        Remove-Item Env:MGS_COOP_TEST_EXIT_AFTER_ACTIVATE -ErrorAction SilentlyContinue
        Remove-Item Env:MGS_COOP_TEST_NO_WATCHDOG -ErrorAction SilentlyContinue
    }
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
function Get-CodeFromOutput([string]$Raw) {
    $m = [regex]::Match($Raw, 'MGS1-[0-9A-Z]{5}-[0-9A-Z]{5}-[0-9A-Z]{5}-[0-9A-Z]{5}-[0-9A-Z]{2}@[^\s]+')
    if (!$m.Success) { throw "No join code in the output: $Raw" }
    return $m.Value
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
$fakeGame = $null
$watchdogs = New-Object Collections.Generic.List[int]
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
    foreach ($ch in 'default', 'quiet', 'dd_male', 'dd_female', 'snake', 'avatar', 'ocelot') { Assert-True (Test-FriendCharacter $ch) "character $ch" }
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
            'net_latency_ms', 'net_loss_permille', 'net_jitter_ms', 'net_dup_permille', 'net_reorder_permille', 'net_link', 'listen_port', 'peer_port') {
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
            # Traversal replay (docs/native-puppet.md "Traversal") is lab only and default 0; Play-Coop never names it.
            Assert-True (!$hostIni.Contains('partner_native_traverse') -and !$guestIni.Contains('partner_native_traverse') -and
                !$clipsIni.Contains('partner_native_traverse')) 'Play-Coop lab.ini never names partner_native_traverse'
            Assert-Equal "$($n['partner_native_traverse'])|$($n['partner_native_traverse_given'])" '0|0' 'native: play leaves the traversal replay off'
            $file = Join-Path $fixture 'play-traverse-lab.ini'; [IO.File]::WriteAllText($file, $hostIni + "partner_native_traverse=1`n", $utf8)
            $m = Invoke-IniCheck lab $file
            Assert-Equal "$($m['partner_native_traverse'])|$($m['partner_native_traverse_given'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" '0|1|0x0|0/0' 'native: play forces a named traversal replay off'
            # F1 vehicles (docs/native-puppet.md "Vehicle boarding"): the board hook is lab only and default off; Play-Coop never names it.
            Assert-True (!$hostIni.Contains('partner_ride_board') -and !$guestIni.Contains('partner_ride_board') -and
                !$clipsIni.Contains('partner_ride_board')) 'Play-Coop lab.ini never names partner_ride_board'
            Assert-Equal "$($n['partner_ride_board'])|$($n['partner_ride_board_given'])" 'off|0' 'native: play leaves the board hook off'
            foreach ($value in @('block', 'passenger')) {
                $file = Join-Path $fixture "play-board-$value-lab.ini"; [IO.File]::WriteAllText($file, $hostIni + "partner_ride_board=$value`n", $utf8)
                $m = Invoke-IniCheck lab $file
                Assert-Equal "$($m['partner_ride_board'])|$($m['partner_ride_board_given'])|$($m['play_refused'])|$($m['unknown_keys'])/$($m['invalid_values'])" 'off|1|0x0|0/0' "native: play forces a named board $value off"
            }
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
    $steamVectors = @(@('ABCDE12345FGHJK67890', '76561198058195100', '6Z'), @('C00PC00PC00PC00PC00P', '76561197960265729', 'D1'),
        @('ZZZZZYYYYYXXXXXWWWWW', '76561202255233023', '5D'), @('0123456789ABCDEFGHJK', '76561198057664236', '2R'))
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
    Assert-Equal (ConvertFrom-CoopSteamJoinCode ' mgs2 abcde i2345 fghjk 67890 6z @ 76561198058195100').Status 'ok' 'MGS2: blanks, case and I -> 1'
    # The native IsBlank set: space, tab, CR, LF, VT and FF (a pasted code may carry a line break).
    Assert-Equal (ConvertFrom-CoopSteamJoinCode ("`tMGS2-ABCDE-12345-`r`nFGHJK-67890-6Z@76561198058195100`v`f`n")).Status 'ok' 'MGS2: tab, CR, LF, VT and FF are blanks'
    $steamBad = @(@('', 'empty'), @('MGS1-ABCDE-12345-FGHJK-67890-6Z@203.0.113.7', 'bad_prefix'), @('MGS2-ABCDE-12345-FGHJK-67890-6Z', 'bad_steamid'),
        @('MGS2-ABCDU-12345-FGHJK-67890-6Z@76561198058195100', 'bad_char'), @('MGS2-ABCDE-12345-FGHJK-6789-6Z@76561198058195100', 'bad_length'),
        @('MGS2-ABCDE-12345-FGHJK-67890-6Y@76561198058195100', 'bad_check'), @('MGS2-ABCDE-12345-FGHJK-67890-6Z@76561198058195101', 'bad_check'),
        @('MGS2-ABCDE-12345-FGHJK-67890-6Z@076561198058195100', 'bad_steamid'), @('MGS2-ABCDE-12345-FGHJK-67890-6Z@103582791429521408', 'bad_steamid'),
        @('MGS2-ABCDE-12345-FGHJK-67890-6Z@76561197960265728', 'bad_steamid'))
    foreach ($b in $steamBad) {
        Assert-Equal (ConvertFrom-CoopSteamJoinCode $b[0]).Status $b[1] "MGS2 '$($b[0])' -> $($b[1])"
        if ($steamKeys -and $b[0]) { Assert-Equal (Invoke-IniCheck steamcode $b[0])['steamcode_status'] $b[1] "native: MGS2 '$($b[0])' -> $($b[1])" }
        Assert-True ([bool](Get-CoopSteamCodeProblem -Status $b[1])) "sentence for $($b[1])"
    }
    Assert-Equal (Get-CoopSteamId64 -ActiveUser 0) '' 'no Steam account: no id'
    Assert-Equal (Get-CoopSteamId64 -ActiveUser 1) '76561197960265729' 'SteamID64 of account 1'
    Assert-Equal (Get-CoopSteamId64 -ActiveUser 4294967295) '76561202255233023' 'SteamID64 of the last account'
    Assert-True (!(Test-CoopSteamId64 '76561197960265728') -and (Test-CoopSteamId64 '76561197960265729') -and !(Test-CoopSteamId64 '7656119796026572')) 'individual SteamID64 rule'
    $steamHostIni = New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Transport steam -SteamSelf '76561198058195100'
    Assert-Equal $steamHostIni ("$header`nenabled=1`nmode=play`nrole=host`nsave_dir=$saveDir`nnet=1`nnet_transport=steam`nnet_steam_self=76561198058195100`npartner_interp=auto`n$defaultsText`nworld=3`n") 'online host lab.ini: no UDP key'
    $steamGuestIni = New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -Transport steam -SteamSelf '76561198057664236' -SteamPeer '76561198058195100'
    Assert-True ($steamGuestIni.Contains("net_transport=steam`nnet_steam_self=76561198057664236`nnet_peer_steam=76561198058195100`n") -and !$steamGuestIni.Contains('net_bind') -and !$steamGuestIni.Contains('net_peer_addr')) 'online guest lab.ini'
    $steamWaitIni = New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -Transport steam -SteamSelf '76561198057664236' -SteamWait
    Assert-True ($steamWaitIni.Contains("net_steam_wait=1`n") -and !$steamWaitIni.Contains('net_peer_steam')) 'waiting guest lab.ini'
    Assert-Throws { New-CoopPlayLabIniText -Role guest -SaveDir $saveDir -Transport steam -SteamSelf '76561198057664236' -SteamPeer '76561198057664236' } 'own code' 'own code refused'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Transport steam -SteamSelf '76561198058195100' -SteamPeer '76561198057664236' } 'no Steam peer' 'a host has no Steam peer'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -SteamSelf '76561198058195100' } 'Transport steam' 'Steam keys need -Transport steam'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Transport steam -SteamSelf '1' } 'SteamID64' 'own SteamID64 validated'
    # The two-PC A/B variant of the code-join sources (docs/steam-online.md 20.5): only with -SteamDispatch, lower case.
    $dispatchIni = New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Transport steam -SteamSelf '76561198058195100' -SteamDispatch FnPtr
    Assert-True ($dispatchIni.Contains("net_steam_self=76561198058195100`nnet_steam_dispatch=fnptr`npartner_interp=auto`n")) 'online A/B variant key'
    Assert-True (!$steamHostIni.Contains('net_steam_dispatch')) 'no variant key by default'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -SteamDispatch poll } 'Transport steam' 'the variant needs -Transport steam'
    Assert-Throws { New-CoopPlayLabIniText -Role host -SaveDir $saveDir -Transport steam -SteamSelf '76561198058195100' -SteamDispatch fast } 'SteamDispatch' 'unknown variant refused'
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
        foreach ($case in @(@('host', $steamHostIni, 'steam|0|0|0'), @('guest', $steamGuestIni, 'steam|0|76561198058195100|0'), @('guest', $steamWaitIni, 'steam|1|0|0'))) {
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
    Assert-True ((Get-CoopLinkReasonText -Reason steam_iface_shape).Contains('LAN/VPN')) 'a Steam networking change points to the LAN/VPN code'
    $steamStatus = '{"v":1,"pid":4242,"mode":"play","role":"host","state":"connected","reason":"","verified":1,"session":1,"peer":"steam:...5100","local_port":0,"rtt_ms":61,"jitter_ms":3,"loss_permille":0,"resend":0,"tx_Bps":3900,"rx_Bps":3800,"interp_ms":80,"remote_build":"0.12.0","partner":"live","partner_shown":1,"look":1,"mission":30050,"saves":"redirected","transport":"steam","route":"relay","accepting":1,"steam":"ok","time_utc":"2026-10-05T12:00:00Z"}'
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
    $steamCodeText = New-CoopSteamJoinCode -Secret $s7 -SteamId '76561198058195100'
    $steamLeaky = "code $steamCodeText`nlower " + $steamCodeText.ToLowerInvariant() + "`nconnect +mgscoop_join $steamCodeText`nsteam=join; via=invite; host=steam:...5100`n"
    $steamClean = Hide-CoopNetworkText $steamLeaky
    Assert-True (!$steamClean.ToUpperInvariant().Contains('7K3QZ') -and !$steamClean.Contains('76561198058195100')) 'MGS2 codes and invite strings redacted'
    Assert-Equal ([regex]::Matches($steamClean, 'MGS2-<redacted>').Count) 2 'each MGS2 code replaced'
    Assert-True ($steamClean.Contains('+mgscoop_join <redacted>') -and $steamClean.Contains('host=steam:...5100')) 'the invite string replaced; masked SteamIDs kept'

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
    Assert-True ((Format-CoopStatusLine -Status $rejected -Role guest -NowUtc $now -OwnBuild '0.12.0').Contains('Your friend runs mgs-coop 0.12.1; you run 0.12.0. Both must install the same package.')) 'build mismatch sentence'
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
    $playText = [IO.File]::ReadAllText((Join-Path $projectRoot 'tools/friend/Play-Coop.ps1'))
    Assert-True ($playText.Contains('[ValidateRange(0, 3)][int]$World = 3') -and
        $playText.Contains('New-CoopPlayLabIniText -Role $labRole -SaveDir $saveDir -Port $Port -PeerAddress $peerAddress -World $World') -and
        $playText.Contains('World sync level $World (experimental)')) 'Play-Coop -World 0..3 (default 3) reaches lab.ini and is announced'
    Assert-Equal ([regex]::Matches($playText, [regex]::Escape('-Partner $Partner -SaveHandoff $SaveHandoff -TravelLock $TravelLock')).Count) 2 'both lab.ini calls get -SaveHandoff and -TravelLock'
    Assert-True ($playText.Contains("[ValidateSet('scene', 'always', 'off')][string]`$SaveHandoff = 'scene'") -and $playText.Contains("[ValidateSet('on', 'off')][string]`$TravelLock = 'on'")) 'Play-Coop -SaveHandoff (default scene) and -TravelLock (default on)'
    $supported = [regex]::Match($playText, "SupportedLoaderVersions = @\(([^)]*)\)").Groups[1].Value
    Assert-True ($supported.Contains("'$packageVersion'") -and $supported.Contains("'0.13.0'")) 'Play-Coop supports this package version and the phase-2 loader 0.13.0'
    $shareText = [IO.File]::ReadAllText((Join-Path $projectRoot 'tools/friend/Share-Save.ps1'))
    Assert-True ($shareText.Contains("'  [R] Restore the co-op copy the last import or automatic save handoff replaced'")) 'Share-Save menu names the save handoff for [R]'
    $collectText = [IO.File]::ReadAllText((Join-Path $projectRoot 'tools/friend/Collect-Logs.ps1'))
    Assert-True ($collectText.Contains('link-rejoin.txt (a join code)') -and !$collectText.Contains("'link-rejoin.txt'")) 'Collect-Logs names link-rejoin.txt as never collected and never reads it'
    Assert-True ($playText.Contains("`$display = 'mgs-coop play'") -and !$playText.Contains('mgs-coop play UDP')) 'one fixed firewall rule name: a later [F] on another port replaces the earlier rule'
    $fixedScript = New-CoopFirewallScript -Port 47900 -ExePath $quoteExe -DisplayName 'mgs-coop play'
    Assert-True ($fixedScript.Contains("Get-NetFirewallRule -DisplayName 'mgs-coop play' -ErrorAction SilentlyContinue | Remove-NetFirewallRule") -and
        $fixedScript.IndexOf('Remove-NetFirewallRule') -lt $fixedScript.IndexOf('New-NetFirewallRule')) 'the [F] script replaces the rule of the same name'
    # [Q] in the status loop hands the cleanup only to a live helper (a key press cannot be scripted here).
    Assert-True ($playText.Contains("if (Test-CoopProcessAlive -Id ([int]`$session.watchdog_pid) -StartUtc ([string]`$session.watchdog_start_utc)) { Write-Host 'Closing this window") -and
        $playText.Contains('No helper is running: keep this window open') -and $playText.Contains('MGSV is still running and no helper is')) '[Q] and an early loop end never skip the cleanup without a live helper'
    $tokens = $null; $errors = $null
    $null = [System.Management.Automation.Language.Parser]::ParseInput($fwScript, [ref]$tokens, [ref]$errors)
    Assert-Equal @($errors).Count 0 'firewall script parses (a quote in the path cannot break it)'
    Assert-Equal ([Text.Encoding]::Unicode.GetString([Convert]::FromBase64String((ConvertTo-CoopEncodedCommand -Script $fwScript)))) $fwScript 'the elevated command is passed with -EncodedCommand (UTF-16LE base64)'
    $friendCommonText = [IO.File]::ReadAllText((Join-Path $projectRoot 'tools/friend/FriendCommon.psm1'))
    Assert-True ($friendCommonText.Contains("'-EncodedCommand', (ConvertTo-CoopEncodedCommand -Script `$Script)") -and $friendCommonText.Contains('-Verb RunAs')) 'Invoke-CoopElevated uses RunAs with -EncodedCommand'
    Assert-True ($friendCommonText.Contains('Start-Job') -and $friendCommonText.Contains('Wait-Job -Job $job -Timeout $TimeoutSec') -and $friendCommonText.Contains("if (`$null -eq `$maps) { return 'unavailable|' }")) 'UPnP runs in a job with a timeout and handles a missing mapping collection'

    # ------------------------------------------------------------ package contents
    $fakeLoader = Join-Path $fixture 'fake-loader.dll'
    [IO.File]::WriteAllText($fakeLoader, 'INERT loader stand-in for the package test')
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
    Remove-Item -LiteralPath $sourceFile
    Assert-Throws { Assert-WorkerReceiptSource $sourceReceipt (Get-WorkerSourceHashes $sourceFx) } 'Source file set changed' 'removed source refused'
    [IO.File]::WriteAllText($sourceFile, '// checked source')
    $changedDependency = $sourceReceipt | ConvertTo-Json -Depth 8 | ConvertFrom-Json -AsHashtable
    $changedDependency.source_dependencies['third_party\minhook'].resolved_path += '-other'
    Assert-Throws { Assert-WorkerReceiptSource $changedDependency (Get-WorkerSourceHashes $sourceFx) } 'dependency target changed' 'changed dependency target refused with same source hashes'

    # The contents test supplies inert DLL bytes with a synthetic passing receipt.
    # Even the override route must match that receipt and today's exact source scope.
    $packageScript = Join-Path $projectRoot 'tools\package.ps1'
    $distFx = Join-Path $fixture 'dist'
    Assert-Throws { & $packageScript -Friend -LoaderPath $fakeLoader -OutputRoot $distFx } 'requires a passing Release check receipt' 'friend override cannot bypass receipt'
    Assert-True (-not (Test-Path -LiteralPath $distFx)) 'missing receipt refuses before staging'
    $receiptFx = Join-Path $fixture 'release-receipt'
    $null = New-Item -ItemType Directory -Path $receiptFx
    [IO.File]::Copy($fakeLoader, (Join-Path $receiptFx 'dinput8.dll'))
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
    $releaseReceipt = [ordered]@{schema=1;status='passed';plan=[ordered]@{profile='Release';flags=$releaseFlags;tests=@('inert-fixture-only')};
        artifacts=[ordered]@{loader_sha256=(Get-Sha256 $fakeLoader)};source_hashes=$currentSource.hashes;source_dependencies=$currentSource.dependencies}
    $receiptPath = Join-Path $receiptFx 'receipt.json'
    [IO.File]::WriteAllText($receiptPath, ($releaseReceipt | ConvertTo-Json -Depth 12), $utf8)
    $wrongLoader = Join-Path $fixture 'wrong-loader.dll'
    [IO.File]::WriteAllText($wrongLoader, 'OTHER inert loader')
    Assert-Throws { & $packageScript -Friend -LoaderPath $wrongLoader -CheckReceipt $receiptPath -OutputRoot $distFx } 'does not match the Release receipt loader' 'friend override hash mismatch refused'
    Assert-True (-not (Test-Path -LiteralPath $distFx)) 'mismatched override refuses before staging'
    $staleReceipt = $releaseReceipt | ConvertTo-Json -Depth 12 | ConvertFrom-Json -AsHashtable
    $staleReceipt.source_hashes['CMakeLists.txt'] = '0' * 64
    $stalePath = Join-Path $receiptFx 'stale.json'
    [IO.File]::WriteAllText($stalePath, ($staleReceipt | ConvertTo-Json -Depth 12), $utf8)
    Assert-Throws { & $packageScript -Friend -CheckReceipt $stalePath -OutputRoot $distFx } 'Release source does not match check receipt' 'real package admission rejects stale source receipt'
    Assert-True (-not (Test-Path -LiteralPath $distFx)) 'stale source refuses before staging'
    $null = & $packageScript -Friend -LoaderPath $fakeLoader -CheckReceipt $receiptPath -OutputRoot $distFx
    $packages = @(Get-ChildItem -LiteralPath $distFx -Directory)
    Assert-Equal $packages.Count 1 'one friend package staged'
    $pkg = $packages[0].FullName
    Assert-True ($packages[0].Name -cmatch ('^mgs-coop-friend-' + [regex]::Escape($packageVersion) + '-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{8}$')) 'package folder name'
    Assert-True (Test-Path -LiteralPath ($pkg + '.zip') -PathType Leaf) 'package zip written'
    $required = @('Install-Coop.bat', 'Play-Coop.bat', 'Play-Offline-Coop.bat', 'Uninstall-Coop.bat', 'Collect-Logs.bat', 'Share-Save.bat', 'scripts/Share-Save.ps1', 'FRIEND-GUIDE.txt', 'RUNTIME-ARCHIVE.txt', 'package.json', 'source-files.json',
        'payload/dinput8.dll', 'payload/DYNAMITE-LICENSE', 'payload/MinHook-LICENSE.txt', 'payload/ImGui-LICENSE.txt', 'payload/tpp-mod-LICENSE', 'payload/THIRD_PARTY_NOTICES.md',
        'scripts/Install-Coop.ps1', 'scripts/Play-Coop.ps1', 'scripts/Play-LocalCoop.ps1', 'scripts/LocalCoopSession.psm1', 'scripts/LocalCoopWindows.psm1', 'scripts/Uninstall-Coop.ps1', 'scripts/Collect-Logs.ps1', 'scripts/FriendCommon.psm1', 'scripts/CoopPlay.psm1',
        'scripts/install.ps1', 'source/tools/friend/Play-Coop.ps1', 'source/tools/friend/Play-LocalCoop.ps1', 'source/tools/friend/LocalCoopSession.psm1', 'source/tools/friend/LocalCoopWindows.psm1', 'source/tools/CoopPlay.psm1', 'source/lua/coop/session_feedback.lua',
        'source/cmake/lua_checks.cmake', 'source/native/debug_overlay.cpp', 'source/lua/coop/debug_menu.lua', 'source/third_party/imgui/imgui.cpp')
    foreach ($r in $required) { Assert-True (Test-Path -LiteralPath (Join-Path $pkg $r) -PathType Leaf) "package contains $r" }
    # The build dependency is source, not just its payload license. Verify the
    # actual checked files and one manifest row even when rg skips a junction.
    foreach ($relative in @('third_party\minhook\CMakeLists.txt', 'third_party\minhook\include\MinHook.h', 'third_party\minhook\src\hook.c')) {
        Assert-True (Test-Path -LiteralPath (Join-Path $pkg ('source\' + $relative)) -PathType Leaf) "package includes checked dependency $relative"
        Assert-Equal (Get-Sha256 (Join-Path $pkg ('source\' + $relative))) $currentSource.hashes[$relative] "package dependency matches Release snapshot $relative"
    }
    $wantScripts = @('Install-Coop.ps1', 'Play-Coop.ps1', 'Play-LocalCoop.ps1', 'LocalCoopSession.psm1', 'LocalCoopWindows.psm1', 'Uninstall-Coop.ps1', 'Collect-Logs.ps1', 'Share-Save.ps1', 'FriendCommon.psm1', 'CoopPlay.psm1', 'install.ps1') | Sort-Object
    Assert-Equal ((@(Get-ChildItem -LiteralPath (Join-Path $pkg 'scripts') -File | ForEach-Object { $_.Name } | Sort-Object)) -join ',') ($wantScripts -join ',') 'scripts: exactly the play package pieces (no LabCommon.psm1, no set-session-mode.ps1)'
    Assert-Equal ((@(Get-ChildItem -LiteralPath $pkg -File -Filter '*.bat' | ForEach-Object { $_.Name } | Sort-Object)) -join ',') 'Collect-Logs.bat,Install-Coop.bat,Play-Coop.bat,Play-Offline-Coop.bat,Share-Save.bat,Uninstall-Coop.bat' 'the six double-click entry points'
    $manifest = Get-Content -LiteralPath (Join-Path $pkg 'package.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($manifest.product)|$($manifest.version)|$($manifest.flavor)" "mgs-coop-local-package|$packageVersion|friend-play" 'package manifest identity'
    # A matching override still packages the receipt's immutable checked copy.
    Assert-Equal "$($manifest.build.source)|$($manifest.build.profile)" 'release_receipt|Release' 'friend build always records the Release receipt'
    Assert-Equal $manifest.build.receipt ([IO.Path]::GetRelativePath($projectRoot, $receiptPath)) 'package.json records the supplied matching receipt'
    Assert-Equal $manifest.build.receipt_sha256 (Get-Sha256 $receiptPath) 'package.json records the receipt hash'
    Assert-Equal $manifest.build.loader_sha256 (Get-Sha256 $fakeLoader) 'package.json build: the packaged loader hash'
    Assert-Equal @($manifest.files).Count 6 'six payload files (loader + licenses)'
    Assert-Equal ($manifest.files | Where-Object target -eq 'mgs-coop/ImGui-LICENSE.txt').sha256 (Get-Sha256 (Join-Path $projectRoot 'third_party/imgui/LICENSE.txt')) 'overlay MIT license packaged intact'
    Assert-Equal ($manifest.files | Where-Object target -eq 'dinput8.dll').sha256 (Get-Sha256 $fakeLoader) 'payload loader hash'
    Assert-Equal $manifest.runtime_archive.sha256 '87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96' 'runtime archive referenced by hash'
    Assert-True (-not (@($manifest.files.target) -contains 'mgs-coop/mgs-coop-player-runtime.dat')) 'runtime archive not bundled'
    $all = @(Get-ChildItem -LiteralPath $pkg -File -Recurse -Force)
    $gameExtensions = @('.dat', '.fpk', '.fpkd', '.fox2', '.mtar', '.qar', '.ftex', '.fmdl', '.subp', '.lng', '.lng2', '.sbp', '.gani', '.frig', '.fv2', '.fsm', '.fpkl')
    Assert-Equal @($all | Where-Object { $gameExtensions -contains $_.Extension.ToLowerInvariant() }).Count 0 'no game archive or asset file types'
    Assert-Equal @($all | Where-Object { $_.Name -like 'mgs-coop-player-runtime*' }).Count 0 'no runtime archive by name'
    Assert-Equal @($all | Where-Object { $_.Length -eq 3720192 }).Count 0 'no file of the runtime archive size'
    Assert-Equal @($all | Where-Object { $_.Name -like 'link-secret*' }).Count 0 'no link secret anywhere in the package'
    $zipEntries = @()
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($pkg + '.zip')
    try { $zipEntries = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') }) } finally { $zip.Dispose() }
    $prefix = $packages[0].Name + '/'
    foreach ($r in $required) { Assert-True ($zipEntries -contains ($prefix + $r)) "zip contains $r" }
    Assert-Equal @($zipEntries | Where-Object { $_ -like '*/scripts/LabCommon.psm1' -or $_ -like '*/scripts/set-session-mode.ps1' -or $_ -match '(^|/)link-secret' -or $_ -like '*mgs-coop-player-runtime*' }).Count 0 'zip: no lab module or role command among the scripts, no link secret, no runtime archive'
    Assert-Equal @($zipEntries | Where-Object { $gameExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() }).Count 0 'zip has no game file types'
    # Regression: an owned, ordinary dependency lives outside the staged source
    # root, reached only by the exact supported third_party/minhook junction.
    # No source is compiled, no game is installed and no external target is used.
    & {
        $linkedRoot = Join-Path $fixture 'junction-package-source'
        $dependency = Join-Path $fixture 'junction-package-dependency'
        $junction = Join-Path $linkedRoot 'third_party\minhook'
        foreach ($path in @($linkedRoot, $dependency, $junction)) { Assert-InFixture $path }
        foreach ($dir in @('native','lua','tests','tools\friend','tools\worker','third_party','data','cmake','docs')) {
            $null = New-Item -ItemType Directory -Path (Join-Path $linkedRoot $dir) -Force
        }
        $null = New-Item -ItemType Directory -Path (Join-Path $dependency 'src'), (Join-Path $dependency 'include') -Force
        foreach ($file in @('CMakeLists.txt','README.md','THIRD_PARTY_NOTICES.md','tools\package.ps1','tools\package-install.ps1',
            'tools\CoopPlay.psm1','tools\worker\WorkerSource.psm1','tools\friend\Install-Coop.ps1','tools\friend\Play-Coop.ps1',
            'tools\friend\Uninstall-Coop.ps1','tools\friend\Collect-Logs.ps1','tools\friend\Share-Save.ps1',
            'tools\friend\FriendCommon.psm1','tools\friend\Play-LocalCoop.ps1','tools\friend\LocalCoopSession.psm1','tools\friend\LocalCoopWindows.psm1','tools\friend\FRIEND-GUIDE.txt','tools\friend\RUNTIME-ARCHIVE.txt')) {
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
            $linkedReceipt = [ordered]@{schema=1;status='passed';plan=[ordered]@{profile='Release';flags=$releaseFlags;tests=@('owned-junction-fixture-only')};
                artifacts=[ordered]@{loader_sha256=(Get-Sha256 $fakeLoader)};source_hashes=$snapshot.hashes;source_dependencies=$snapshot.dependencies}
            $receipt = Join-Path $receiptFx 'junction.json'
            [IO.File]::WriteAllText($receipt, ($linkedReceipt | ConvertTo-Json -Depth 12), $utf8)
            $dist = Join-Path $fixture 'junction-package-dist'
            $null = & (Join-Path $linkedRoot 'tools\package.ps1') -Friend -LoaderPath $fakeLoader -CheckReceipt $receipt -OutputRoot $dist
            $packages = @(Get-ChildItem -LiteralPath $dist -Directory)
            Assert-Equal $packages.Count 1 'junction fixture stages exactly one package'
            $package = $packages[0].FullName
            $sourceRows = @(Get-Content -LiteralPath (Join-Path $package 'source-files.json') -Raw | ConvertFrom-Json)
            $zip = [IO.Compression.ZipFile]::OpenRead($package + '.zip')
            try {
                foreach ($relative in $minhookFiles.Keys) {
                    $logical = 'third_party\minhook\' + $relative
                    $expected = Get-Sha256 (Join-Path $dependency $relative)
                    Assert-Equal $snapshot.hashes[$logical] $expected "Release checks linked dependency $relative"
                    Assert-Equal (Get-Sha256 (Join-Path $package ('source\' + $logical))) $expected "staged linked dependency is intact $relative"
                    $rows = @($sourceRows | Where-Object { $_.path.Replace('\','/') -ceq $logical.Replace('\','/') })
                    Assert-Equal $rows.Count 1 "source manifest includes linked dependency once $relative"
                    Assert-Equal $rows[0].sha256 $expected "source manifest hash matches linked dependency $relative"
                    $name = $packages[0].Name + '/source/' + $logical.Replace('\','/')
                    $entries = @($zip.Entries | Where-Object { $_.FullName.Replace('\','/') -ceq $name })
                    Assert-Equal $entries.Count 1 "ZIP includes linked dependency once $relative"
                    $stream = $entries[0].Open()
                    $sha = [Security.Cryptography.SHA256]::Create()
                    try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
                    finally { $sha.Dispose(); $stream.Dispose() }
                    Assert-Equal $hash $expected "ZIP source bytes match linked dependency $relative"
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
    foreach ($f in @(Get-ChildItem -LiteralPath (Join-Path $pkg 'scripts') -File) + @(Get-Item -LiteralPath (Join-Path $pkg 'FRIEND-GUIDE.txt'), (Join-Path $pkg 'RUNTIME-ARCHIVE.txt'))) {
        $bytes = [IO.File]::ReadAllBytes($f.FullName)
        Assert-Equal @($bytes | Where-Object { $_ -gt 127 }).Count 0 "$($f.Name) is ASCII (Windows PowerShell 5.1 / Notepad)"
        $text = [Text.Encoding]::ASCII.GetString($bytes)
        Assert-True (!$text.Contains('@VERSION@') -and !$text.Contains('@ARCHIVE_SHA256@')) "$($f.Name) placeholders replaced"
        Assert-True ($text.Contains("`r`n") -and !($text -replace "`r`n", '').Contains("`n")) "$($f.Name) has CRLF line endings"
    }
    # Every shipped script and module parses under Windows PowerShell 5.1 itself.
    $scriptsDir = (Join-Path $pkg 'scripts').Replace("'", "''")
    $parseCheck = "foreach (`$f in Get-ChildItem -LiteralPath '$scriptsDir' -File) { `$t = `$null; `$e = `$null; `$null = [System.Management.Automation.Language.Parser]::ParseFile(`$f.FullName, [ref]`$t, [ref]`$e); if (`$e) { Write-Output (`$f.Name + ': ' + `$e[0].Message) } }; Write-Output 'parsed'"
    $parsed = @(& $winPs -NoProfile -NonInteractive -Command $parseCheck 2>&1 | ForEach-Object { "$_" })
    Assert-Equal ($parsed -join ' | ') 'parsed' 'every shipped script and module parses under Windows PowerShell 5.1'
    foreach ($b in 'Install-Coop', 'Play-Coop', 'Uninstall-Coop', 'Collect-Logs', 'Share-Save') {
        $text = [IO.File]::ReadAllText((Join-Path $pkg "$b.bat"))
        Assert-True ($text.Contains("powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0scripts\$b.ps1`" %*") -and $text.Contains("`r`n") -and $text.Contains('pause') -and $text.Contains('set "PSModulePath="')) "$b.bat runs its script with Windows PowerShell (own module path)"
    }
    $offlineBat = [IO.File]::ReadAllText((Join-Path $pkg 'Play-Offline-Coop.bat'))
    Assert-True ($offlineBat.Contains('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Play-Coop.ps1" -LocalTest %*') -and
        $offlineBat.Contains("`r`n") -and $offlineBat.Contains('pause') -and $offlineBat.Contains('set "PSModulePath="')) 'offline BAT delegates to the unified launcher with Windows PowerShell and all caller arguments'
    $localSourceRows = @(Get-Content -LiteralPath (Join-Path $pkg 'source-files.json') -Raw | ConvertFrom-Json)
    $localZip = [IO.Compression.ZipFile]::OpenRead($pkg + '.zip')
    try {
        foreach ($localPiece in 'Play-LocalCoop.ps1', 'LocalCoopSession.psm1', 'LocalCoopWindows.psm1') {
            $relative = 'tools\friend\' + $localPiece
            $expected = Get-Sha256 (Join-Path $projectRoot $relative)
            Assert-Equal $currentSource.hashes[$relative] $expected "Release source snapshot covers local test $localPiece"
            Assert-Equal (Get-Sha256 (Join-Path $pkg ('source\' + $relative))) $expected "local test source bytes preserved $localPiece"
            $rows = @($localSourceRows | Where-Object { $_.path.Replace('\','/') -ceq $relative.Replace('\','/') })
            Assert-Equal $rows.Count 1 "local test source listed exactly once $localPiece"
            Assert-Equal $rows[0].sha256 $expected "local test source manifest hash $localPiece"
            foreach ($path in @(('scripts/' + $localPiece), ('source/' + $relative.Replace('\','/')))) {
                $entries = @($localZip.Entries | Where-Object { $_.FullName.Replace('\','/') -ceq ($prefix + $path) })
                Assert-Equal $entries.Count 1 "local test ZIP includes exactly once $path"
                $stream = $entries[0].Open()
                $sha = [Security.Cryptography.SHA256]::Create()
                try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
                finally { $sha.Dispose(); $stream.Dispose() }
                $expectedEntry = Get-Sha256 (Join-Path $pkg $path)
                Assert-Equal $hash $expectedEntry "local test ZIP bytes match staged piece $path"
            }
        }
    } finally { $localZip.Dispose() }
    $guide = [IO.File]::ReadAllText((Join-Path $pkg 'FRIEND-GUIDE.txt'))
    foreach ($needle in 'Play-Coop.bat', 'UDP 47830', 'Tailscale', 'Mother Base', 'Install-Coop.bat', 'Uninstall-Coop.bat', 'Collect-Logs.bat', 'RUNTIME-ARCHIVE.txt', "Version $packageVersion", 'Failed to log in', 'Public networks', 'Block rules win over Allow rules', 'REAL saves',
            'Share-Save.bat', 'SAME PROGRESS FOR BOTH', 'SAME PROGRESS AUTOMATICALLY', '-SaveHandoff off', 'TRAVEL LOCK', '-TravelLock off', 'Keep my own save') {
        Assert-True $guide.Contains($needle) "guide mentions $needle"
    }
    # Section 8 describes everything the logs ZIP holds (Collect-Logs coop-saves.txt, 3 sessions).
    $logsSection = $guide.Substring($guide.IndexOf('8. COLLECT AND SEND THE LOGS'), $guide.IndexOf('9. UNINSTALL') - $guide.IndexOf('8. COLLECT AND SEND THE LOGS')) -replace '\s+', ' '
    Assert-True ($logsSection.Contains('a list of the co-op save folders (file names, sizes and times only, never save contents)') -and
        $logsSection.Contains('the script log covers your last 3 game starts')) 'guide section 8 names the save-folder list and the 3 game starts'
    foreach ($needle in 'Ctrl+F8', 'Ctrl+F9', '7656119', 'SteamID64', 'normal online service', 'Account details') { Assert-True (!$guide.Contains($needle)) "guide no longer mentions $needle" }
    $statusExample = [regex]::Match($guide, '\[HOST\] link=connected[^\r\n]+').Value
    Assert-Equal $statusExample (Format-CoopStatusLine -Status $st -Role host -NowUtc $now) 'the guide quotes the launcher status line exactly'
    # The online example (Steam relay, the native partner on both PCs; docs/two-pc-play.md "Quick start").
    $onlineExample = [regex]::Match($guide, '\[JOIN\] link=connected via Steam[^\r\n]+').Value
    $onlineText = $statusText.Replace('"role":"host"', '"role":"guest"').Replace('"rtt_ms":42,"jitter_ms":3,"loss_permille":4', '"rtt_ms":61,"jitter_ms":4,"loss_permille":0')
    $onlineText = $onlineText.Replace('"interp_ms":80', '"interp_ms":100').Replace('"mission":30050', '"mission":30010')
    $onlineText = $onlineText.Replace('"saves":"redirected"', '"saves":"redirected","transport":"steam","route":"relay","partner_drive":"native","partner_reason":"ok","remote_drive":"native","remote_reason":"ok"')
    $onlineSt = ConvertFrom-CoopStatusText -Text $onlineText
    Assert-True ([bool]$onlineExample) 'the guide shows an online status line'
    Assert-Equal $onlineExample (Format-CoopStatusLine -Status $onlineSt -Role guest -NowUtc $now) 'the guide quotes the online status line exactly'
    foreach ($needle in 'Host online', 'Join online', 'Invite a friend', 'start Play-Coop.bat FIRST', 'Play-Coop.bat -Partner clips', 'native partner', '[T] Local two-window test', 'Play-Offline-Coop.bat', 'local-test-saves', '-GuestDirectory', '[R]', 'Continue') {
        Assert-True $guide.Contains($needle) "guide describes the online flow: $needle"
    }
    $archiveNote = [IO.File]::ReadAllText((Join-Path $pkg 'RUNTIME-ARCHIVE.txt'))
    Assert-True ($archiveNote.Contains('87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96') -and $archiveNote.Contains('16,333,824') -and !$archiveNote.Contains('Ctrl+F8') -and !$archiveNote.Contains('SteamID')) 'archive note names hash and size, nothing of the online route'

    # ------------------------------------------------------------ installer fixtures
    # Steam fixture: account 7 with saves; staged package with fixture hashes and
    # the packaged scripts\ layout from the friend package above.
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
    $wrongArchive = Join-Path $fixture 'wrong-archive.bin'
    [IO.File]::WriteAllText($wrongArchive, 'not the archive')
    $templateGame = New-GameFixture 'template'
    $exeHash = Get-Sha256 (Join-Path $templateGame 'mgsvtpp.exe')
    $steamHash = Get-Sha256 (Join-Path $templateGame 'steam_api64.dll')
    $loader1 = Join-Path $fixture 'loader1.dll'; [IO.File]::WriteAllText($loader1, 'INERT friend loader')
    $loaderPrev = Join-Path $fixture 'loader-prev.dll'; [IO.File]::WriteAllText($loaderPrev, 'INERT earlier loader')
    $stage = Join-Path $fixture 'stage'
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $stage -LoaderPath $loader1 -Version '0.12.0' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 (Get-Sha256 $archiveFx)
    Copy-Item -LiteralPath (Join-Path $pkg 'scripts') -Destination (Join-Path $stage 'scripts') -Recurse
    foreach ($b in 'Install-Coop.bat', 'Play-Coop.bat', 'Play-Offline-Coop.bat', 'Uninstall-Coop.bat', 'Collect-Logs.bat') { Copy-Item -LiteralPath (Join-Path $pkg $b) -Destination $stage }
    $install = Join-Path $stage 'scripts/Install-Coop.ps1'
    $play = Join-Path $stage 'scripts/Play-Coop.ps1'
    $uninstall = Join-Path $stage 'scripts/Uninstall-Coop.ps1'
    $collect = Join-Path $stage 'scripts/Collect-Logs.ps1'
    $common = @('-SteamPath', $steam, '-NonInteractive', '-Yes', '-SkipNetworkReport')
    function Assert-PassThrough($Paths, [string]$What) {
        Assert-True (-not (Test-Path -LiteralPath $Paths.Marker) -and -not (Test-Path -LiteralPath $Paths.LabIni) -and -not (Test-Path -LiteralPath $Paths.CoopIni) -and
            -not (Test-Path -LiteralPath $Paths.LinkSecret) -and -not (Test-Path -LiteralPath $Paths.ClientHost)) "${What}: pass-through (no marker, lab.ini, coop.ini, link secret or client-host.txt)"
    }

    # Refusals change nothing.
    $refuse = New-GameFixture 'refuse'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-Role', 'Host', '-Character', 'dog', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r "Unknown character 'dog'" 'unknown character refused'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-Role', 'Host', '-RuntimeArchive', $wrongArchive) + $common)
    Assert-Refused $r 'not the expected runtime archive' 'wrong archive hash refused'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-Role', 'Host') + $common)
    Assert-True ($r.Exit -ne 0 -and (Has $r 'Missing input') -and (Has $r 'mgs-coop-player-runtime.dat')) 'no archive in non-interactive mode: no prompt, clear error'
    $r = Invoke-Friend $install (@('-GameDirectory', $refuse, '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r 'Missing input' 'no role in non-interactive mode'
    $modified = New-GameFixture 'modified'
    [IO.File]::WriteAllText((Join-Path $modified 'mgsvtpp.exe'), 'a different executable')
    $r = Invoke-Friend $install (@('-GameDirectory', $modified, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r '1.0.15.4' 'unexpected executable refused'
    [IO.File]::WriteAllText((Join-Path $modified 'mgsvtpp.exe'), 'INERT executable fixture; never executed')
    [IO.File]::WriteAllText((Join-Path $modified 'steam_api64.dll'), 'patched steam dll')
    $r = Invoke-Friend $install (@('-GameDirectory', $modified, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r 'steam_api64.dll' 'modified Steam DLL refused'
    $emptySteam = Join-Path $fixture 'steam-empty'
    $null = New-Item -ItemType Directory -Path (Join-Path $emptySteam 'userdata') -Force
    $r = Invoke-Friend $install @('-GameDirectory', $refuse, '-Role', 'Host', '-RuntimeArchive', $archiveFx, '-SteamPath', $emptySteam, '-NonInteractive', '-Yes', '-SkipNetworkReport')
    Assert-Refused $r 'Steam account' 'no Steam account with saves refused'
    foreach ($g in $refuse, $modified) {
        Assert-Equal @(Get-ChildItem -LiteralPath $g -Force).Count 2 "refusals left $([IO.Path]::GetFileName($g)) untouched"
    }
    $foreign = New-GameFixture 'foreign'
    [IO.File]::WriteAllText((Join-Path $foreign 'dinput8.dll'), 'another mod')
    $r = Invoke-Friend $install (@('-GameDirectory', $foreign, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r 'Another mod''s dinput8.dll' 'unrelated loader refused before any change'
    Assert-Equal @(Get-ChildItem -LiteralPath $foreign -Force).Count 3 'unrelated loader: no backup or mod folder created'
    $gamesRoot = [IO.Path]::GetFullPath((Join-Path $fixture 'games'))
    $longGame = New-GameFixture ('long-' + ('x' * (180 - $gamesRoot.Length - 1 - 5)))
    Assert-Equal $longGame.Length 180 'long game path fixture length'
    $r = Invoke-Friend $install (@('-GameDirectory', $longGame, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Refused $r 'game folder path is too long' 'game path too long for the installer paths refused'
    Assert-Equal @(Get-ChildItem -LiteralPath $longGame -Force).Count 2 'too long: folder untouched'

    # Host install (Windows PowerShell 5.1, packaged layout): pass-through.
    $hostGame = New-GameFixture 'host'
    $hp = Get-InstancePaths -GameDirectory $hostGame
    $r = Invoke-Friend $install (@('-GameDirectory', $hostGame, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Equal $r.Exit 0 "host install succeeds: $($r.Raw)"
    Assert-True ((Has $r 'Play-Coop.bat') -and !(Has $r 'SteamID64') -and !(Has $r 'Ctrl+F8')) 'install points to Play-Coop.bat; no SteamID exchange'
    Assert-Equal (Get-Sha256 $hp.Loader) (Get-Sha256 $loader1) 'loader installed'
    Assert-Equal (Get-Sha256 $hp.Archive) (Get-Sha256 $archiveFx) 'runtime archive installed'
    Assert-PassThrough $hp 'host install'
    $hostSaveDir = Join-Path $hp.Mod 'coop-saves'
    Assert-True (Test-Path -LiteralPath (Join-Path $hostSaveDir '287700/local/TPP_GAME_DATA0') -PathType Leaf) 'save copy seeded (local)'
    Assert-True (Test-Path -LiteralPath (Join-Path $hostSaveDir 'remote/TPP_GAME_DATA') -PathType Leaf) 'save copy seeded (remote)'
    $state = Get-Content -LiteralPath (Join-Path $hp.Mod 'friend-install.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($state.role_hint)|$($state.character)|$($state.status)|$(@($state.rollback_ids).Count)|$(@($state.online_archived).Count)" 'Host|default|installed|1|0' 'install record (role_hint, character)'
    $backupManifest = Get-Content -LiteralPath (Join-Path $state.latest_backup 'manifest.json') -Raw | ConvertFrom-Json
    Assert-Equal @($backupManifest.verified).Count 3 'backup manifest hashes the verified save files'
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'real saves untouched by the install'
    Assert-Equal (Get-Sha256 $hp.Exe) $exeHash 'executable untouched'
    Assert-Equal (Get-Content -LiteralPath (Join-Path $stage 'installed-location.json') -Raw | ConvertFrom-Json).game_directory $hostGame 'installed location recorded in the package'

    # Reinstall (character change) reuses the installed archive and keeps the save copy.
    [IO.File]::WriteAllText((Join-Path $hostSaveDir '287700/local/COOP_PROGRESS'), 'progress made in the co-op copy')
    $r = Invoke-Friend $install (@('-GameDirectory', $hostGame, '-Role', 'Host', '-Character', 'quiet') + $common)
    Assert-Equal $r.Exit 0 "host reinstall succeeds: $($r.Raw)"
    Assert-True (Test-Path -LiteralPath (Join-Path $hostSaveDir '287700/local/COOP_PROGRESS')) 'reinstall keeps the existing save copy'
    $state = Get-Content -LiteralPath (Join-Path $hp.Mod 'friend-install.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($state.character)|$(@($state.rollback_ids).Count)|$(@($state.backups).Count)" 'quiet|2|2' 'reinstall: new character, both install states and backups recorded'
    Assert-PassThrough $hp 'reinstall'

    # Join install under PowerShell 7 (same scripts).
    $joinGame = New-GameFixture 'join'
    $jp = Get-InstancePaths -GameDirectory $joinGame
    $r = Invoke-Friend $install (@('-GameDirectory', $joinGame, '-Role', 'Join', '-RuntimeArchive', $archiveFx) + $common) -HostExe $pwshPath
    Assert-Equal $r.Exit 0 "join install succeeds (PowerShell 7): $($r.Raw)"
    $js = Get-Content -LiteralPath (Join-Path $jp.Mod 'friend-install.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($js.role_hint)|$($js.character)" 'Join|quiet' 'joiner defaults to Quiet'
    Assert-PassThrough $jp 'join install'

    # ------------------------------------------------------------ Share-Save (same progress for both; docs/two-pc-play.md)
    # The host exports its co-op copy, the joiner imports it (with the import
    # marker, K18) and restores its own copy again. The real saves are never touched.
    $shareSave = Join-Path $stage 'scripts/Share-Save.ps1'
    $shareOut = Join-Path $fixture 'share-out'
    $null = New-Item -ItemType Directory -Path $shareOut
    $joinCoop = Join-Path $jp.Mod 'coop-saves'
    $joinLocal = Join-Path $joinCoop '287700/local'
    $joinReplaced = Join-Path $jp.Mod 'coop-saves-replaced'
    $joinOwnData = [IO.File]::ReadAllBytes((Join-Path $joinLocal 'TPP_GAME_DATA0'))
    [IO.File]::WriteAllText((Join-Path $joinLocal 'TPP_GAME_DATA0'), 'join own early progress')
    [IO.File]::WriteAllText((Join-Path $joinLocal 'TPP_CONFIG_DATA'), 'join own settings')
    $hostCoopBefore = Get-TreeHashes $hostSaveDir
    $r = Invoke-Friend $shareSave @('-GameDirectory', $hostGame, '-Mode', 'Export', '-NonInteractive', '-OutputDirectory', $shareOut)
    Assert-Equal $r.Exit 0 "Share-Save export: $($r.Raw)"
    $shareZips = @(Get-ChildItem -LiteralPath $shareOut -Filter 'mgs-coop-save-*.zip')
    Assert-Equal $shareZips.Count 1 'Share-Save export writes one zip'
    Assert-Equal (Get-TreeHashes $hostSaveDir) $hostCoopBefore 'Share-Save export only reads the co-op copy'
    $shareZip = [IO.Compression.ZipFile]::OpenRead($shareZips[0].FullName)
    try {
        $reader = [IO.StreamReader]::new($shareZip.GetEntry('share-save.json').Open())
        try { $shareManifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    } finally { $shareZip.Dispose() }
    $manifestRows = (@($shareManifest.files) | ForEach-Object { "$($_.entry)|$($_.bytes)|$(([string]$_.sha256).ToUpperInvariant())" } | Sort-Object) -join ','
    Assert-True ($manifestRows.Contains('287700/local/TPP_GAME_DATA0|') -and !$manifestRows.Contains('CONFIG')) 'the export carries the progress files only'
    # The export re-check (Assert-FriendShareZip) and its cleanup (Write-FriendShareZip):
    # a damaged or partial export never stays behind; a same-named file is never ours to delete.
    $shareRows = @($shareManifest.files)
    Assert-FriendShareZip -ZipPath $shareZips[0].FullName -ManifestFiles $shareRows
    $checkDir = Join-Path $fixture 'share-check'
    $null = New-Item -ItemType Directory -Path $checkDir
    $tampered = Join-Path $checkDir 'tampered.zip'
    Copy-Item -LiteralPath $shareZips[0].FullName -Destination $tampered
    $tamperZip = [IO.Compression.ZipFile]::Open($tampered, [IO.Compression.ZipArchiveMode]::Update)
    try {
        $tamperZip.GetEntry('287700/local/TPP_GAME_DATA0').Delete()
        $tamperWriter = [IO.StreamWriter]::new($tamperZip.CreateEntry('287700/local/TPP_GAME_DATA0').Open())
        try { $tamperWriter.Write('other bytes') } finally { $tamperWriter.Dispose() }
    } finally { $tamperZip.Dispose() }
    Assert-Throws { Assert-FriendShareZip -ZipPath $tampered -ManifestFiles $shareRows } 'differs from the save file' 'export check: a rewritten entry is damage'
    $incomplete = Join-Path $checkDir 'incomplete.zip'
    Copy-Item -LiteralPath $shareZips[0].FullName -Destination $incomplete
    $tamperZip = [IO.Compression.ZipFile]::Open($incomplete, [IO.Compression.ZipArchiveMode]::Update)
    try { $tamperZip.GetEntry('287700/local/TPP_GAME_DATA0').Delete() } finally { $tamperZip.Dispose() }
    Assert-Throws { Assert-FriendShareZip -ZipPath $incomplete -ManifestFiles $shareRows } 'The written zip is incomplete' 'export check: a missing entry'
    $exportSource = Join-Path $hostSaveDir '287700/local/TPP_GAME_DATA0'
    $exportFiles = @([pscustomobject]@{ Path = $exportSource; Entry = '287700/local/TPP_GAME_DATA0' })
    $goodRows = @([ordered]@{ entry = '287700/local/TPP_GAME_DATA0'; bytes = 1; sha256 = (Get-Sha256 $exportSource) })
    $badRows = @([ordered]@{ entry = '287700/local/TPP_GAME_DATA0'; bytes = 1; sha256 = ('0' * 64) })
    $verified = Join-Path $checkDir 'mgs-coop-save-verified.zip'
    Write-FriendShareZip -ZipPath $verified -Files $exportFiles -ManifestFiles $goodRows -ManifestText '{"format":"mgs-coop-save-share"}'
    Assert-True (Test-Path -LiteralPath $verified -PathType Leaf) 'export: a verified zip stays'
    $damaged = Join-Path $checkDir 'mgs-coop-save-damaged.zip'
    Assert-Throws { Write-FriendShareZip -ZipPath $damaged -Files $exportFiles -ManifestFiles $badRows -ManifestText '{}' } 'differs from the save file' 'export: a hash mismatch fails'
    Assert-True (!(Test-Path -LiteralPath $damaged)) 'export: the damaged zip is deleted'
    $partial = Join-Path $checkDir 'mgs-coop-save-partial.zip'
    $missingFiles = @([pscustomobject]@{ Path = (Join-Path $checkDir 'NO_SUCH_SAVE'); Entry = '287700/local/TPP_GAME_DATA0' })
    Assert-Throws { Write-FriendShareZip -ZipPath $partial -Files $missingFiles -ManifestFiles $goodRows -ManifestText '{}' } '' 'export: a write error fails'
    Assert-True (!(Test-Path -LiteralPath $partial)) 'export: the partial zip is deleted'
    $verifiedHash = Get-Sha256 $verified
    Assert-Throws { Write-FriendShareZip -ZipPath $verified -Files $exportFiles -ManifestFiles $badRows -ManifestText '{}' } '' 'export: an existing file is refused'
    Assert-Equal (Get-Sha256 $verified) $verifiedHash 'export: a file of the same name is never deleted'
    $r = Invoke-Friend $shareSave @('-GameDirectory', $joinGame, '-Mode', 'Import', '-ZipPath', $shareZips[0].FullName, '-NonInteractive')
    Assert-Equal $r.Exit 0 "Share-Save import: $($r.Raw)"
    Assert-True (Has $r 'Keep the zip: if this copy is ever replaced, import it again.') 'import: keep the zip'
    $joinMarker = Join-Path $joinCoop 'imported-progress.json'
    $importMarker = [IO.File]::ReadAllText($joinMarker) | ConvertFrom-Json
    Assert-Equal "$($importMarker.format)|$($importMarker.v)|$($importMarker.source)" 'mgs-coop-imported-progress|1|share-save' 'import: the marker is written into the new copy'
    $markerRows = (@($importMarker.files) | ForEach-Object { "$($_.entry)|$($_.bytes)|$($_.sha256)" } | Sort-Object) -join ','
    Assert-Equal $markerRows $manifestRows 'import: the marker hashes equal the zip manifest'
    Assert-True (![IO.File]::ReadAllText($joinMarker).Contains('fixture save')) 'import: the marker holds no save bytes'
    Assert-Equal (Get-Sha256 (Join-Path $joinLocal 'TPP_GAME_DATA0')) (Get-Sha256 (Join-Path $hostSaveDir '287700/local/TPP_GAME_DATA0')) 'import: the partner''s progress is in the co-op copy'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $joinLocal 'TPP_CONFIG_DATA'))) 'join own settings' 'import: own settings kept'
    Assert-Equal @(Get-ChildItem -LiteralPath $joinReplaced -Directory -Filter '*-before-import').Count 1 'import: the previous copy is kept aside'
    Assert-Equal (Get-FriendImportedSaveInfo -SaveDir $joinCoop -ReplacedRoot $joinReplaced).Source 'marker' 'import: detected by its marker'
    $r = Invoke-Friend $shareSave @('-GameDirectory', $joinGame, '-Mode', 'Restore', '-NonInteractive')
    Assert-Equal $r.Exit 0 "Share-Save restore: $($r.Raw)"
    $restoreAside = @(Get-ChildItem -LiteralPath $joinReplaced -Directory -Filter '*-before-restore')
    Assert-True (!(Test-Path -LiteralPath $joinMarker) -and $restoreAside.Count -eq 1 -and (Test-Path -LiteralPath (Join-Path $restoreAside[0].FullName 'imported-progress.json'))) 'restore: the marker is moved aside with the imported copy'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $joinLocal 'TPP_GAME_DATA0'))) 'join own early progress' 'restore: the own copy is back'
    Assert-True ($null -eq (Get-FriendImportedSaveInfo -SaveDir $joinCoop -ReplacedRoot $joinReplaced)) 'restore: the copy no longer counts as imported'
    [IO.File]::WriteAllBytes((Join-Path $joinLocal 'TPP_GAME_DATA0'), $joinOwnData)
    Remove-Item -LiteralPath (Join-Path $joinLocal 'TPP_CONFIG_DATA')
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'Share-Save never touches the real saves'

    # ------------------------------------------------------------ the import core with a save handoff stage (Import-CoopProgressFiles, P2-18)
    $coreGame = New-GameFixture 'handoff-core'
    $kp = Get-InstancePaths -GameDirectory $coreGame
    $r = Invoke-Friend $install (@('-GameDirectory', $coreGame, '-Role', 'Join', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Equal $r.Exit 0 "install for the import core: $($r.Raw)"
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
    # Share-Save [R] restores the newest of -before-import and -before-handoff and names which one.
    $r = Invoke-Friend $shareSave @('-GameDirectory', $coreGame, '-Mode', 'Restore', '-NonInteractive')
    Assert-True ($r.Exit -eq 0 -and (Has $r "Restoring the co-op save copy kept before the automatic save handoff at $coreAside")) "[R] picks the newer handoff copy: $($r.Raw)"
    Assert-Equal (Get-RelativeTreeHashes $kp.CoopSaves) $coreBefore '[R] brings back the copy from before the handoff'
    $r = Invoke-Friend $shareSave @('-GameDirectory', $coreGame, '-Mode', 'Restore', '-NonInteractive')
    Assert-True ($r.Exit -eq 0 -and (Has $r "Restoring the co-op save copy kept before the import at $olderImport")) "[R] again: the older import copy: $($r.Raw)"
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $coreLocal 'TPP_GAME_DATA0'))) 'an old imported copy' '[R] again restored the import copy'
    $r = Invoke-Friend $shareSave @('-GameDirectory', $coreGame, '-Mode', 'Restore', '-NonInteractive')
    Assert-Refused $r 'No co-op save copy from before an import or an automatic save handoff was found' '[R] with nothing left changes nothing'
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
    } finally {
        Remove-Item Env:MGS_COOP_TEST_NO_LAUNCH -ErrorAction SilentlyContinue
        Remove-Item Env:MGS_COOP_TEST_RELAUNCH_DELAY_SEC -ErrorAction SilentlyContinue
        if ($sleeper -and !$sleeper.HasExited) { Stop-Process -Id $sleeper.Id -Force -ErrorAction SilentlyContinue }
    }

    # The reseed guard (Install-Coop): a copy that holds the partner's imported
    # progress is replaced only after two answers; the marker leaves with it.
    $impGame = New-GameFixture 'imported'
    $ip = Get-InstancePaths -GameDirectory $impGame
    $r = Invoke-Friend $install (@('-GameDirectory', $impGame, '-Role', 'Join', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Equal $r.Exit 0 "install for the reseed guard: $($r.Raw)"
    $impCoop = Join-Path $ip.Mod 'coop-saves'
    $impReplaced = Join-Path $ip.Mod 'coop-saves-replaced'
    $impMarker = Join-Path $impCoop 'imported-progress.json'
    [IO.File]::WriteAllText((Join-Path $impCoop '287700/local/TPP_GAME_DATA0'), 'own early progress')
    $r = Invoke-Friend $shareSave @('-GameDirectory', $impGame, '-Mode', 'Import', '-ZipPath', $shareZips[0].FullName, '-NonInteractive')
    Assert-Equal $r.Exit 0 "import for the reseed guard: $($r.Raw)"
    $importedTree = Get-TreeHashes $impCoop
    $r = Invoke-Friend $install (@('-GameDirectory', $impGame, '-Role', 'Join') + $common)
    Assert-True ($r.Exit -eq 0 -and (Has $r 'Your co-op save copy holds your partner''s progress (imported with Share-Save on') -and
        (Has $r 'Kept the co-op save copy with your partner''s imported progress (run with -ReseedSaves to replace it).')) "non-interactive reinstall keeps an imported copy: $($r.Text)"
    Assert-Equal (Get-TreeHashes $impCoop) $importedTree 'non-interactive reinstall: copy and marker unchanged'
    $r = Invoke-FriendAnswers $install @('-GameDirectory', $impGame, '-SteamPath', $steam, '-SkipNetworkReport') @('1', '', 'y', 'y', 'KEEP')
    Assert-True ($r.Exit -eq 0 -and (Has $r 'removes that progress from co-op games (it is only moved aside; Share-Save [R] cannot bring it back)') -and
        (Has $r 'Kept the co-op save copy with your partner''s progress.')) "prompted reinstall, KEEP: $($r.Text)"
    Assert-Equal (Get-TreeHashes $impCoop) $importedTree 'KEEP: copy and marker unchanged'
    $r = Invoke-FriendAnswers $install @('-GameDirectory', $impGame, '-SteamPath', $steam, '-SkipNetworkReport') @('1', '', 'y', 'y', 'REPLACE')
    Assert-True ($r.Exit -eq 0 -and (Has $r 'To play on it again, import your partner''s zip again with Share-Save.bat (option I).')) "prompted reinstall, REPLACE: $($r.Text)"
    Assert-True (!(Test-Path -LiteralPath $impMarker)) 'REPLACE: no marker in the fresh copy'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $impCoop '287700/local/TPP_GAME_DATA0'))) 'fixture save 287700/local/TPP_GAME_DATA0' 'REPLACE: a fresh copy of the real save'
    $movedMarkers = @(Get-ChildItem -LiteralPath $impReplaced -Directory | Where-Object { $_.Name -cmatch '^[0-9]{8}-[0-9]{6}(-[0-9]+)?$' -and (Test-Path -LiteralPath (Join-Path $_.FullName 'imported-progress.json')) })
    Assert-Equal $movedMarkers.Count 1 'REPLACE: the marker moved to coop-saves-replaced\<stamp> with the imported copy'
    Assert-True ($null -eq (Get-FriendImportedSaveInfo -SaveDir $impCoop -ReplacedRoot $impReplaced)) 'REPLACE: the fresh copy is not imported'
    # An import made before the marker existed: the newest '-before-import' folder counts the same way.
    $r = Invoke-Friend $shareSave @('-GameDirectory', $impGame, '-Mode', 'Import', '-ZipPath', $shareZips[0].FullName, '-NonInteractive')
    Assert-Equal $r.Exit 0 "second import for the folder fallback: $($r.Raw)"
    Remove-Item -LiteralPath $impMarker
    Assert-Equal (Get-FriendImportedSaveInfo -SaveDir $impCoop -ReplacedRoot $impReplaced).Source 'folder' 'folder fallback: detected without a marker'
    $fallbackTree = Get-TreeHashes $impCoop
    $r = Invoke-Friend $install (@('-GameDirectory', $impGame, '-Role', 'Join') + $common)
    Assert-True ($r.Exit -eq 0 -and (Has $r 'Kept the co-op save copy with your partner''s imported progress (run with -ReseedSaves to replace it).')) "folder fallback keeps the copy: $($r.Text)"
    Assert-Equal (Get-TreeHashes $impCoop) $fallbackTree 'folder fallback: copy unchanged'
    # The first install after Uninstall-Coop (no active install record, which otherwise
    # always reseeds) keeps an imported copy the same way.
    $r = Invoke-Friend $shareSave @('-GameDirectory', $impGame, '-Mode', 'Import', '-ZipPath', $shareZips[0].FullName, '-NonInteractive')
    Assert-Equal $r.Exit 0 "import before the uninstall: $($r.Raw)"
    Assert-Equal (Get-FriendImportedSaveInfo -SaveDir $impCoop -ReplacedRoot $impReplaced).Source 'marker' 'import before the uninstall: detected by its marker'
    $uninstalledTree = Get-TreeHashes $impCoop
    $r = Invoke-Friend $uninstall @('-GameDirectory', $impGame, '-NonInteractive', '-Yes')
    Assert-Equal $r.Exit 0 "uninstall with an imported copy: $($r.Raw)"
    Assert-True (!(Test-Path -LiteralPath (Join-Path $ip.Mod 'friend-install.json'))) 'uninstall: no active install record'
    Assert-Equal (Get-TreeHashes $impCoop) $uninstalledTree 'uninstall keeps the imported copy and its marker'
    $r = Invoke-Friend $install (@('-GameDirectory', $impGame, '-Role', 'Join', '-RuntimeArchive', $archiveFx) + $common)
    Assert-True ($r.Exit -eq 0 -and (Has $r 'Kept the co-op save copy with your partner''s imported progress (run with -ReseedSaves to replace it).') -and
        !(Has $r 'A co-op save copy from an earlier test exists')) "first install after an uninstall keeps an imported copy: $($r.Text)"
    Assert-Equal (Get-TreeHashes $impCoop) $uninstalledTree 'first install after an uninstall: copy and marker unchanged'
    # An automatic save handoff is guarded the same way; the prompt names the partner's save.
    Remove-Item -LiteralPath $impMarker
    $futureHandoff = Join-Path $impReplaced '20991231-235959-before-handoff'
    $null = New-Item -ItemType Directory -Path $futureHandoff
    Assert-Equal "$((Get-FriendImportedSaveInfo -SaveDir $impCoop -ReplacedRoot $impReplaced).Kind)" 'handoff' 'a newest -before-handoff folder is detected'
    $handoffTree = Get-TreeHashes $impCoop
    $r = Invoke-Friend $install (@('-GameDirectory', $impGame, '-Role', 'Join') + $common)
    Assert-True ($r.Exit -eq 0 -and (Has $r "Your co-op save copy holds your partner's save (automatic save handoff on 2099-12-31 23:59).") -and
        (Has $r 'Kept the co-op save copy with your partner''s imported progress (run with -ReseedSaves to replace it).')) "install keeps a copy from an automatic save handoff: $($r.Text)"
    Assert-Equal (Get-TreeHashes $impCoop) $handoffTree 'the handoff copy is unchanged'
    $r = Invoke-FriendAnswers $install @('-GameDirectory', $impGame, '-SteamPath', $steam, '-SkipNetworkReport') @('1', '', 'y', 'y', 'KEEP')
    Assert-True ($r.Exit -eq 0 -and (Has $r "Your co-op save copy holds your partner's save (automatic save handoff on") -and (Has $r 'Kept the co-op save copy with your partner''s progress.')) "prompted reinstall with a handoff copy, KEEP: $($r.Text)"
    Assert-Equal (Get-TreeHashes $impCoop) $handoffTree 'KEEP: the handoff copy is unchanged'
    Remove-Item -LiteralPath $futureHandoff
    [IO.File]::WriteAllText($impMarker, (New-FriendImportMarkerText -Source 'handoff' -Files @()))
    $r = Invoke-Friend $install (@('-GameDirectory', $impGame, '-Role', 'Join') + $common)
    Assert-True ($r.Exit -eq 0 -and (Has $r "Your co-op save copy holds your partner's save (automatic save handoff on")) "a handoff marker is detected too: $($r.Text)"
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'reseed guard: real saves untouched'

    # ------------------------------------------------------------ packaged local two-window flow
    # Dedicated inert copies: only the host uses the friend installer; the
    # guest has the owned core installation and its earlier offline baseline.
    $localHost = New-GameFixture 'local-host'
    $localGuest = New-GameFixture 'local-host_guest'
    $lhp = Get-InstancePaths -GameDirectory $localHost
    $lgp = Get-InstancePaths -GameDirectory $localGuest
    $r = Invoke-Friend $install (@('-GameDirectory', $localHost, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Equal $r.Exit 0 "local host fixture installs: $($r.Raw)"
    $r = Invoke-Friend (Join-Path $stage 'scripts/install.ps1') @('-GameDirectory', $localGuest, '-PackageDirectory', $stage, '-RuntimeArchive', $archiveFx, '-NoPrompt', '-NoInitialMode')
    Assert-Equal $r.Exit 0 "local guest core fixture installs: $($r.Raw)"
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $lgp.Mod 'friend-install.json'))) 'local guest needs the owned core install, not a second friend-account install'
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
    $localFixedHashes = [ordered]@{}
    foreach ($p in @($lhp, $lgp)) { foreach ($file in @($p.Exe, $p.SteamApi, $p.Loader, $p.Archive, $p.Record)) { $localFixedHashes[$file] = Get-Sha256 $file } }
    $env:MGS_COOP_TEST_NO_LAUNCH = '1'
    $env:MGS_COOP_TEST_NO_WATCHDOG = '1'
    try {
        # Invalid selection refuses before any local save or activation mutation.
        $beforeHost = Get-TreeHashes $localHost; $beforeGuest = Get-TreeHashes $localGuest
        $r = Invoke-Friend $play @('-GameDirectory', $localHost, '-LocalTest', '-GuestDirectory', $localHost, '-NonInteractive')
        Assert-Refused $r 'Choose two separate game folders' 'local test rejects using the same game twice'
        Assert-Equal (Get-TreeHashes $localHost) $beforeHost 'same-folder refusal preserves the host'
        Assert-Equal (Get-TreeHashes $localGuest) $beforeGuest 'same-folder refusal preserves the guest'
        [IO.File]::WriteAllText((Join-Path $lgp.Mod 'worker.enabled'), 'INERT foreign developer marker', $utf8)
        $beforeHost = Get-TreeHashes $localHost; $beforeGuest = Get-TreeHashes $localGuest
        $r = Invoke-Friend $play @('-GameDirectory', $localHost, '-LocalTest', '-GuestDirectory', $localGuest, '-NonInteractive')
        Assert-Refused $r 'belongs to a developer test' 'local play leaves an active developer fixture alone'
        Assert-Equal (Get-TreeHashes $localHost) $beforeHost 'developer refusal preserves host configuration and saves'
        Assert-Equal (Get-TreeHashes $localGuest) $beforeGuest 'developer refusal preserves guest configuration and saves'
        Remove-Item -LiteralPath (Join-Path $lgp.Mod 'worker.enabled')
        $originalGuestLoader = [IO.File]::ReadAllBytes($lgp.Loader)
        [IO.File]::WriteAllText($lgp.Loader, 'INERT unrelated loader', $utf8)
        $beforeHost = Get-TreeHashes $localHost; $beforeGuest = Get-TreeHashes $localGuest
        $r = Invoke-Friend $play @('-GameDirectory', $localHost, '-LocalTest', '-GuestDirectory', $localGuest, '-NonInteractive')
        Assert-Refused $r 'no matching owned co-op loader' 'local play refuses a guest loader that no longer matches ownership'
        Assert-Equal (Get-TreeHashes $localHost) $beforeHost 'loader refusal preserves the other game before activation'
        Assert-Equal (Get-TreeHashes $localGuest) $beforeGuest 'loader refusal never replaces an unrelated guest loader'
        [IO.File]::WriteAllBytes($lgp.Loader, $originalGuestLoader)

        $r = Invoke-Friend $play @('-GameDirectory', $localHost, '-LocalTest', '-GuestDirectory', $localGuest, '-World', '3', '-Partner', 'native', '-NonInteractive')
        Assert-True ($r.Exit -eq 0 -and (Has $r 'TEST: local two-window activation checked; no games launched.')) "packaged explicit LocalTest flow: $($r.Raw)"
        Assert-True (Has $r 'Both game folders returned to their previous setup') 'fixture launch path restores both baselines before returning'
        Assert-PassThrough $lhp 'local explicit flow host returned to pass-through'
        foreach ($name in $localBaseline.Keys) { Assert-Equal ([IO.File]::ReadAllText((Join-Path $lgp.Mod $name), [Text.Encoding]::UTF8)) $localBaseline[$name] "local guest restores its earlier offline bytes $name" }
        $localHostSaves = Join-Path $lhp.Mod 'local-test-saves'
        $localGuestSaves = Join-Path $lgp.Mod 'local-test-saves'
        foreach ($save in @($localHostSaves, $localGuestSaves)) {
            Assert-Equal (Get-Sha256 (Join-Path $save '287700/local/TPP_GAME_DATA0')) (Get-Sha256 (Join-Path $lhp.CoopSaves '287700/local/TPP_GAME_DATA0')) 'local tests clone the host ordinary co-op save independently'
            $info = Get-GraphicsConfigInfo ([IO.File]::ReadAllText((Join-Path $save '287700/local/TPP_GRAPHICS_CONFIG')))
            Assert-True ($info.Valid -and $info.WindowMode -ceq 'Windowed' -and $info.Width -ge 640 -and $info.Height -ge 480) 'only the local test saves get tagged windowed graphics'
        }
        $localJournals = @(Get-ChildItem -LiteralPath (Join-Path $lhp.Mod 'local-test-runs') -Directory)
        Assert-Equal $localJournals.Count 1 'one explicit local test journal'
        $localJournal = Get-Content -LiteralPath (Join-Path $localJournals[0].FullName 'session.json') -Raw | ConvertFrom-Json
        Assert-Equal $localJournal.state 'off' 'explicit local activation journal is off after fixture cleanup'
        Assert-Equal @($localJournal.files).Count 8 'local activation records four files separately for each role'
        Assert-Equal @($localJournal.files | Where-Object { $_.role -ceq 'guest' -and $_.original_exists }).Count 4 'guest journal preserves all four earlier offline activation files'
        Assert-Equal @($localJournal.files | Where-Object { $_.phase -cne 'restored' }).Count 0 'all local activation entries are restored'
        Assert-True (![IO.File]::ReadAllText((Join-Path $localJournals[0].FullName 'session.json')).Contains($s7)) 'guest original secret is not printed as plain text in the local journal'
        [IO.File]::WriteAllText((Join-Path $localGuestSaves '287700/local/LOCAL_PROGRESS'), 'guest local test progress', $utf8)
        $savedLocalHost = Get-TreeHashes $localHostSaves; $savedLocalGuest = Get-TreeHashes $localGuestSaves
        # A real menu choice, followed by sibling _guest autodetection.
        $r = Invoke-FriendAnswers $play @('-GameDirectory', $localHost) @('T')
        Assert-True ($r.Exit -eq 0 -and (Has $r '[T] Local two-window test') -and (Has $r 'TEST: local two-window activation checked; no games launched.')) "packaged T menu flow finds the sibling guest: $($r.Raw)"
        Assert-Equal (Get-TreeHashes $localHostSaves) $savedLocalHost 'repeat local test retains the host local progress'
        Assert-Equal (Get-TreeHashes $localGuestSaves) $savedLocalGuest 'repeat local test retains the guest local progress'
        # The old BAT name is an alias, not another independent launcher.
        $batOut = '' | & cmd.exe /c (Join-Path $stage 'Play-Offline-Coop.bat') -GameDirectory $localHost -GuestDirectory $localGuest -NonInteractive 2>&1 | Out-String
        Assert-True ($LASTEXITCODE -eq 0 -and $batOut.Contains('TEST: local two-window activation checked; no games launched.')) "offline BAT uses the unified inert local flow: $batOut"
        $localJournals = @(Get-ChildItem -LiteralPath (Join-Path $lhp.Mod 'local-test-runs') -Directory)
        Assert-Equal $localJournals.Count 3 'explicit, menu and offline alias each journal their own local run'
        foreach ($journal in $localJournals) { Assert-Equal (Get-Content -LiteralPath (Join-Path $journal.FullName 'session.json') -Raw | ConvertFrom-Json).state 'off' 'each packaged local path cleans up its session' }
        Assert-PassThrough $lhp 'all local fixture paths leave host pass-through'
        foreach ($name in $localBaseline.Keys) { Assert-Equal ([IO.File]::ReadAllText((Join-Path $lgp.Mod $name), [Text.Encoding]::UTF8)) $localBaseline[$name] "all local fixture paths preserve guest baseline $name" }
        Assert-Equal (Get-TreeHashes $lhp.CoopSaves) $localNormalSaves 'local flow never changes ordinary online/LAN co-op progress or graphics'
        Assert-Equal (Get-TreeHashes $userdata) $localRealSaves 'local flow never reads or changes fixture real save data'
        foreach ($file in $localFixedHashes.Keys) { Assert-Equal (Get-Sha256 $file) $localFixedHashes[$file] "local flow preserves exact game, archive and owned install bytes $([IO.Path]::GetFileName($file))" }
        # A launcher owns both folders before either game starts. Install,
        # Uninstall, online Play and the core installer must leave that live
        # local journal, configuration and copied saves alone during this gap.
        & {
            Import-Module (Join-Path $stage 'scripts/LocalCoopSession.psm1') -Force -DisableNameChecking
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
                foreach ($operation in @(
                    @{Name='friend install';Script=$install;Arguments=(@('-GameDirectory', $localHost, '-Role', 'Host') + $common);Needle='Another local two-window launcher'},
                    @{Name='friend uninstall';Script=$uninstall;Arguments=@('-GameDirectory', $localHost, '-NonInteractive', '-Yes');Needle='Another local two-window launcher'},
                    @{Name='online play';Script=$play;Arguments=@('-GameDirectory', $localHost, '-Role', 'Host', '-Transport', 'steam', '-NonInteractive');Needle='Another local two-window launcher'},
                    @{Name='core guest install';Script=(Join-Path $stage 'scripts/install.ps1');Arguments=@('-GameDirectory', $localGuest, '-PackageDirectory', $stage, '-NoPrompt', '-NoInitialMode');Needle='local two-window session owns this folder'}
                )) {
                    $result = Invoke-Friend $operation.Script $operation.Arguments
                    Assert-Refused $result $operation.Needle ("$($operation.Name) refuses a live local launcher before mutation")
                    Assert-Equal (Get-TreeHashes $localHost) $activeHostHashes "$($operation.Name) leaves the entire host fixture unchanged"
                    Assert-Equal (Get-TreeHashes $localGuest) $activeGuestHashes "$($operation.Name) leaves the entire guest fixture unchanged"
                    Assert-Equal (Get-TreeHashes $userdata) $localRealSaves "$($operation.Name) leaves fixture real saves unchanged"
                }
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

    # The read-only network report (no -SkipNetworkReport): reported, nothing changed.
    $reportGame = New-GameFixture 'report'
    $r = Invoke-Friend $install @('-GameDirectory', $reportGame, '-Role', 'Host', '-RuntimeArchive', $archiveFx, '-SteamPath', $steam, '-NonInteractive', '-Yes')
    Assert-True ($r.Exit -eq 0 -and (Has $r 'Network check (read-only; nothing is changed)') -and (Has $r 'Block rule')) "install prints the read-only firewall report: $($r.Text)"

    # Prompt path (a double-clicked Install-Coop.bat): usually Join, character 4, the archive in quotes, Enter.
    $askGame = New-GameFixture 'prompts'
    $ap = Get-InstancePaths -GameDirectory $askGame
    $r = Invoke-FriendAnswers $install @('-GameDirectory', $askGame, '-SteamPath', $steam, '-SkipNetworkReport') @('2', '4', ('"' + $archiveFx + '"'), '')
    Assert-Equal $r.Exit 0 "prompted join install succeeds: $($r.Raw)"
    $as = Get-Content -LiteralPath (Join-Path $ap.Mod 'friend-install.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($as.role_hint)|$($as.character)" 'Join|dd_female' 'prompted: usually Join, character chosen by number'
    # Reinstall through the prompts: Host, Enter for the default character, yes, refresh the save copy.
    [IO.File]::WriteAllText((Join-Path $ap.Mod 'coop-saves/287700/local/COOP_PROGRESS'), 'old copy progress')
    $r = Invoke-FriendAnswers $install @('-GameDirectory', $askGame, '-SteamPath', $steam, '-SkipNetworkReport') @('1', '', 'y', 'y')
    Assert-Equal $r.Exit 0 "prompted reinstall succeeds: $($r.Raw)"
    $as = Get-Content -LiteralPath (Join-Path $ap.Mod 'friend-install.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($as.role_hint)|$($as.character)" 'Host|default' 'Enter keeps the host default'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $ap.Mod 'coop-saves/287700/local/COOP_PROGRESS'))) 'save copy refreshed on request'
    Assert-Equal @(Get-ChildItem -LiteralPath (Join-Path $ap.Mod 'coop-saves-replaced') -Recurse -File -Filter COOP_PROGRESS).Count 1 'old save copy moved aside, not deleted'
    $before = Get-TreeHashes $askGame
    $r = Invoke-FriendAnswers $install @('-GameDirectory', $askGame, '-SteamPath', $steam, '-SkipNetworkReport') @('2', '', 'n')
    Assert-True ($r.Exit -eq 1 -and (Has $r 'Cancelled. Nothing was changed.')) "cancel at the confirmation: $($r.Text)"
    Assert-Equal (Get-TreeHashes $askGame) $before 'cancel left the folder unchanged'
    $r = Invoke-FriendAnswers $uninstall @('-GameDirectory', $askGame) @('')
    Assert-Equal $r.Exit 0 "prompted uninstall succeeds: $($r.Raw)"
    Assert-True (-not (Test-Path -LiteralPath $ap.Loader)) 'prompted uninstall removed the loader'

    # The install record cannot be written: nothing was changed yet, everything is put back.
    $failGame = New-GameFixture 'fail'
    $fp = Get-InstancePaths -GameDirectory $failGame
    $null = New-Item -ItemType Directory -Path (Join-Path $fp.Mod 'friend-install.json') -Force
    $r = Invoke-Friend $install (@('-GameDirectory', $failGame, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-True ($r.Exit -ne 0 -and (Has $r 'undoing the changes') -and (Has $r 'back to its state before this run')) "failed record write is undone: $($r.Text)"
    Assert-True (-not (Test-Path -LiteralPath $fp.Loader) -and -not (Test-Path -LiteralPath $fp.Record)) 'record failure: the loader was never installed'

    # Failures AFTER the loader install, through a staged copy of Install-Coop.ps1
    # with a stand-in inserted after the install (test only): it fails, damages the
    # loader first (so the automatic restore fails too), or ends the process (window closed).
    $stageFaulty = Join-Path $fixture 'stage-faulty'
    Copy-Item -LiteralPath $stage -Destination $stageFaulty -Recurse
    $installFaulty = Join-Path $stageFaulty 'scripts/Install-Coop.ps1'
    $faultyText = [IO.File]::ReadAllText($installFaulty)
    $faultPoint = '    # (tests/friend_package_tests.ps1 inserts its failure stand-ins after this line.)'
    Assert-Equal ([regex]::Matches($faultyText, [regex]::Escape($faultPoint)).Count) 1 'Install-Coop has one fault injection point'
    $faultCode = @($faultPoint,
        '    switch ($env:MGS_COOP_TEST_INSTALL_FAULT) {',
        '        ''throw'' { throw ''simulated failure after the loader install'' }',
        '        ''damage'' { [IO.File]::AppendAllText($paths.Loader, ''changed''); throw ''simulated failure after the loader changed'' }',
        '        ''exit'' { [Environment]::Exit(9) }',
        '    }') -join "`r`n"
    [IO.File]::WriteAllText($installFaulty, $faultyText.Replace($faultPoint, $faultCode), [Text.Encoding]::ASCII)
    function Invoke-Faulty([string]$Mode, [string]$Game) {
        $env:MGS_COOP_TEST_INSTALL_FAULT = $Mode
        try { return Invoke-Friend $installFaulty (@('-GameDirectory', $Game, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common) }
        finally { Remove-Item Env:MGS_COOP_TEST_INSTALL_FAULT -ErrorAction SilentlyContinue }
    }
    # 1. A failure after the install; the restore works: everything is put back.
    $throwGame = New-GameFixture 'fault-throw'
    $tp = Get-InstancePaths -GameDirectory $throwGame
    $null = New-Item -ItemType Directory -Path $tp.Mod -Force
    [IO.File]::WriteAllText((Join-Path $tp.Mod 'coop.ini'), "character=snake`n")
    $r = Invoke-Faulty 'throw' $throwGame
    Assert-True ($r.Exit -ne 0 -and (Has $r 'previous loader state was restored') -and (Has $r 'back to its state before this run') -and (Has $r 'simulated failure after the loader install')) "failure after the loader install is rolled back: $($r.Text)"
    Assert-True (-not (Test-Path -LiteralPath $tp.Loader) -and -not (Test-Path -LiteralPath $tp.Record)) 'rollback removed the loader and record'
    Assert-Equal ([IO.File]::ReadAllText($tp.CoopIni)) "character=snake`n" 'rollback put the user coop.ini back'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $tp.Mod 'friend-install.json')) -and -not (Test-Path -LiteralPath $tp.Marker)) 'no install record or marker after a rollback'
    # 2. The restore fails as well: the loader stays (pass-through) with a pending record.
    $damageGame = New-GameFixture 'fault-damage'
    $dp = Get-InstancePaths -GameDirectory $damageGame
    $r = Invoke-Faulty 'damage' $damageGame
    Assert-True ($r.Exit -ne 0 -and (Has $r 'could NOT be removed automatically') -and (Has $r 'stays in pass-through') -and (Has $r 'run Uninstall-Coop.bat')) "failed restore: pass-through, uninstall advised: $($r.Text)"
    Assert-True (Test-Path -LiteralPath $dp.Loader -PathType Leaf) 'loader still present after the failed restore'
    Assert-PassThrough $dp 'failed restore'
    $pending = Get-Content -LiteralPath (Join-Path $dp.Mod 'friend-install.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($pending.status)|$($pending.pending_record_before)|$(@($pending.rollback_ids).Count)" 'pending||0' 'pending install record kept'
    $r = Invoke-Friend $uninstall @('-GameDirectory', $damageGame, '-NonInteractive', '-Yes')
    Assert-True ($r.Exit -eq 2 -and (Has $r 'did not finish') -and (Has $r 'could not be restored') -and (Has $r 'stays in pass-through')) "uninstall adopts the pending loader, reports the failed restore: $($r.Text)"
    Copy-Item -LiteralPath $loader1 -Destination $dp.Loader -Force
    $r = Invoke-Friend $uninstall @('-GameDirectory', $damageGame, '-NonInteractive', '-Yes')
    Assert-True ($r.Exit -eq 0 -and (Has $r 'did not finish') -and (Has $r 'Loader removed (restored 1 install state(s))')) "uninstall succeeds once the cause is gone: $($r.Text)"
    Assert-True (-not (Test-Path -LiteralPath $dp.Loader) -and -not (Test-Path -LiteralPath $dp.Record)) 'pending install fully undone'
    # 3. The process ends halfway (window closed): pending record; the next
    #    Install-Coop adopts that loader state and Uninstall-Coop undoes both.
    $exitGame = New-GameFixture 'fault-exit'
    $xp = Get-InstancePaths -GameDirectory $exitGame
    $r = Invoke-Faulty 'exit' $exitGame
    Assert-Equal $r.Exit 9 "process ended after the loader install: $($r.Text)"
    Assert-True (Test-Path -LiteralPath $xp.Loader -PathType Leaf) 'interrupted run: loader present'
    Assert-Equal (Get-Content -LiteralPath (Join-Path $xp.Mod 'friend-install.json') -Raw | ConvertFrom-Json).status 'pending' 'interrupted run left a pending record'
    $r = Invoke-Friend $install (@('-GameDirectory', $exitGame, '-Role', 'Host') + $common)
    Assert-True ($r.Exit -eq 0 -and (Has $r 'did not finish')) "install after an interrupted run: $($r.Text)"
    Assert-PassThrough $xp 'install after an interrupted run'
    $xs = Get-Content -LiteralPath (Join-Path $xp.Mod 'friend-install.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($xs.status)|$(@($xs.rollback_ids).Count)" 'installed|2' 'interrupted run adopted into the install record'
    $r = Invoke-Friend $uninstall @('-GameDirectory', $exitGame, '-NonInteractive', '-Yes')
    Assert-True ($r.Exit -eq 0 -and (Has $r 'Loader removed (restored 2 install state(s))')) "uninstall undoes the interrupted and the later run: $($r.Text)"
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'real saves untouched by the failure tests'

    # ------------------------------------------------------------ Play-Coop (Windows PowerShell 5.1, packaged)
    # Not installed: refused, nothing written.
    $bare = New-GameFixture 'play-bare'
    $r = Invoke-Play $play @('-GameDirectory', $bare, '-Role', 'Host', '-HostAddress', '192.0.2.10')
    Assert-Refused $r 'not installed in this folder' 'Play-Coop refuses a folder without an install'
    Assert-Equal @(Get-ChildItem -LiteralPath $bare -Force).Count 2 'Play-Coop refusal wrote nothing'
    # The online launcher must start this package's exact loader, including
    # different builds with the same version; ownership alone is insufficient.
    $ownedRecordBytes = [IO.File]::ReadAllBytes($hp.Record)
    $ownedLoaderBytes = [IO.File]::ReadAllBytes($hp.Loader)
    $releaseManifestPath = Join-Path $stage 'package.json'
    $releaseManifestBytes = [IO.File]::ReadAllBytes($releaseManifestPath)
    $releaseLoaderPath = Join-Path $stage 'payload\dinput8.dll'
    $releaseLoaderBytes = [IO.File]::ReadAllBytes($releaseLoaderPath)
    $ownedBefore = Get-TreeHashes $hostGame
    try {
        $newRelease = Read-FriendJson $releaseManifestPath
        $newRelease.version = '0.12.2'
        [IO.File]::WriteAllText($releaseManifestPath, ($newRelease | ConvertTo-Json -Depth 8), $utf8)
        $oldBefore = Get-TreeHashes $hostGame
        $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-Role', 'Host', '-HostAddress', '192.0.2.10') -NoWatchdog
        Assert-Refused $r 'not this exact release' '0.12.2 package refuses an older owned 0.12.0 loader'
        Assert-Equal (Get-TreeHashes $hostGame) $oldBefore 'different version refusal changes no game or save files'
        [IO.File]::WriteAllBytes($releaseManifestPath, $releaseManifestBytes)

        [IO.File]::Copy($loaderPrev, $hp.Loader, $true)
        $otherRecord = Read-FriendJson $hp.Record
        $otherRecord.dll_sha256 = Get-Sha256 $hp.Loader
        [IO.File]::WriteAllText($hp.Record, ($otherRecord | ConvertTo-Json -Depth 8), $utf8)
        $otherBefore = Get-TreeHashes $hostGame
        $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-Role', 'Host', '-HostAddress', '192.0.2.10') -NoWatchdog
        Assert-Refused $r 'not this exact release' 'same version with a different owned loader refuses'
        Assert-Equal (Get-TreeHashes $hostGame) $otherBefore 'different build refusal changes no game or save files'
        [IO.File]::WriteAllBytes($hp.Record, $ownedRecordBytes)
        [IO.File]::WriteAllBytes($hp.Loader, $ownedLoaderBytes)

        [IO.File]::AppendAllText($releaseLoaderPath, 'changed')
        $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-Role', 'Host', '-HostAddress', '192.0.2.10') -NoWatchdog
        Assert-Refused $r 'release package loader has changed' 'tampered package payload refuses before activation'
        Assert-Equal (Get-TreeHashes $hostGame) $ownedBefore 'tampered package refusal changes no game or save files'
        [IO.File]::WriteAllBytes($releaseLoaderPath, $releaseLoaderBytes)

        $duplicateManifest = Read-FriendJson $releaseManifestPath
        $duplicateManifest.files = @($duplicateManifest.files) + @($duplicateManifest.files | Where-Object target -ceq 'dinput8.dll')
        [IO.File]::WriteAllText($releaseManifestPath, ($duplicateManifest | ConvertTo-Json -Depth 8), $utf8)
        $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-Role', 'Host', '-HostAddress', '192.0.2.10') -NoWatchdog
        Assert-Refused $r 'no unique checked loader entry' 'ambiguous loader payload refuses before activation'
        Assert-Equal (Get-TreeHashes $hostGame) $ownedBefore 'ambiguous package refusal changes no game or save files'
    } finally {
        [IO.File]::WriteAllBytes($hp.Record, $ownedRecordBytes)
        [IO.File]::WriteAllBytes($hp.Loader, $ownedLoaderBytes)
        [IO.File]::WriteAllBytes($releaseManifestPath, $releaseManifestBytes)
        [IO.File]::WriteAllBytes($releaseLoaderPath, $releaseLoaderBytes)
    }
    # Change the actual manifest only after preflight and the first activation
    # files, immediately before the marker. The fresh check refuses and the
    # launcher's ordinary finally removes its partial activation.
    $stagePlayRace = Join-Path $fixture 'stage-play-release-race'
    Copy-Item -LiteralPath $stage -Destination $stagePlayRace -Recurse
    $playRace = Join-Path $stagePlayRace 'scripts\Play-Coop.ps1'
    $playRaceText = [IO.File]::ReadAllText($playRace)
    $playRacePoint = '            if ($w.Name -eq ''script-observe.enabled'') {'
    Assert-Equal ([regex]::Matches($playRaceText, [regex]::Escape($playRacePoint)).Count) 1 'Play-Coop has one pre-marker fixture injection point'
    $playRaceCode = $playRacePoint + "`r`n" + '                [IO.File]::AppendAllText((Join-Path $PackageDirectory ''package.json''), '' '')'
    [IO.File]::WriteAllText($playRace, $playRaceText.Replace($playRacePoint, $playRaceCode), $utf8)
    $r = Invoke-Play $playRace @('-GameDirectory', $hostGame, '-Role', 'Host', '-HostAddress', '192.0.2.10') -NoWatchdog
    Assert-Refused $r 'release package changed while Play-Coop was preparing' 'manifest race refuses immediately before the activation marker'
    Assert-PassThrough $hp 'release race removes partial activation'
    Assert-Equal (Get-Sha256 $hp.Loader) (Get-Sha256 $loader1) 'release race preserves the installed owned loader'
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'release identity refusals preserve real saves'

    # Host activation (the window "closes" right after it; the watchdog cleans up).
    $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-Role', 'Host', '-HostAddress', '192.0.2.10', '-Port', '47835')
    Assert-True ($r.Exit -eq 0 -and (Has $r 'TEST: leaving after the activation.')) "Play-Coop host activates: $($r.Raw)"
    $code = Get-CodeFromOutput $r.Raw
    $parsedCode = ConvertFrom-CoopJoinCode $code
    Assert-Equal "$($parsedCode.Status)|$($parsedCode.HostName)|$($parsedCode.Port)" 'ok|192.0.2.10|47835' 'the host shows a valid code for its address and port'
    Assert-Equal ([regex]::Matches($r.Raw, [regex]::Escape($code)).Count) 1 'the code (the secret) is shown once, to the host'
    Assert-True (!$r.Raw.Contains($parsedCode.Secret)) 'the bare secret is never printed'
    Assert-True ((Has $r 'Mother Base') -and (Has $r 'Tailscale')) 'host guidance: Mother Base only, VPN recommended'
    Assert-Equal ([IO.File]::ReadAllText($hp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role host -SaveDir $hostSaveDir -Port 47835 -SaveHandoff scene) 'host play lab.ini (save_handoff=scene by default)'
    Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($hp.LinkSecret))) ([Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes($parsedCode.Secret + "`r`n"))) 'link-secret.txt: the code''s secret + CRLF (ASCII)'
    Assert-Equal ([IO.File]::ReadAllText($hp.CoopIni)) (New-CoopPlayCoopIniText -Character quiet) 'play coop.ini: closed master port, the installed character'
    Assert-Equal ([IO.File]::ReadAllText($hp.Marker)) "offline-v1`n" 'marker offline-v1'
    $session = Get-SessionJson $hp
    $watchdogs.Add([int]$session.watchdog_pid)
    Assert-Equal "$($session.v)|$($session.state)|$($session.role)|$($session.port)" '2|active|host|47835' 'play-session.json'
    Assert-Equal (@($session.files | ForEach-Object { $_.name }) -join ',') 'lab.ini,link-secret.txt,coop.ini,script-observe.enabled' 'activation order recorded'
    foreach ($f in @($session.files)) { Assert-Equal $f.sha256 (Get-Sha256 (Join-Path $hp.Mod $f.name)) "recorded hash of $($f.name)" }
    Assert-True ([int]$session.watchdog_pid -gt 0 -and [int]$session.launcher_pid -gt 0) 'launcher and watchdog recorded'
    Assert-True (![IO.File]::ReadAllText($hp.PlaySession).Contains($parsedCode.Secret)) 'play-session.json holds no secret'
    # The launcher is gone and no game started: the watchdog switches co-op off.
    Assert-True (Wait-Until { (Get-PlayFilesPresent $hp) -eq 0 } 45) 'watchdog cleanup after the launcher died without a game'
    Assert-True (Wait-Until { (Get-SessionJson $hp).state -eq 'off' } 15) 'watchdog set the session off'
    Assert-True (Wait-Until { $null -eq (Get-Process -Id $session.watchdog_pid -ErrorAction SilentlyContinue) } 15) 'watchdog exits after its cleanup'
    Assert-PassThrough $hp 'after the watchdog cleanup'

    # Join: a typo first, then the code with blanks and lower case; the host address from the code.
    $joinCode = New-CoopJoinCode -Secret $s7 -HostName '192.0.2.44' -Port 47831
    $typo = $joinCode.Replace('-RVN6B-', '-RVN6C-')
    $joinAnswers = @($typo, (' ' + $joinCode.ToLowerInvariant().Replace('-', ' - ') + ' '))
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') $joinAnswers -NoWatchdog
    Assert-True ($r.Exit -eq 0 -and (Has $r 'The code has a typo; copy it again.') -and (Has $r 'Code OK: host 192.0.2.44:47831')) "Play-Coop join: $($r.Raw)"
    # (Windows PowerShell echoes redirected Read-Host input; in a window that is the pasted text itself.)
    $printed = $r.Raw
    foreach ($a in $joinAnswers) { $printed = $printed.Replace($a, '') }
    Assert-True (!$printed.Contains($s7) -and !$printed.ToUpperInvariant().Contains('7K3QZ')) 'the joiner never prints the secret'
    $jSaveDir = Join-Path $jp.Mod 'coop-saves'
    Assert-Equal ([IO.File]::ReadAllText($jp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role guest -SaveDir $jSaveDir -Port 47831 -PeerAddress '192.0.2.44' -SaveHandoff scene) 'guest play lab.ini (listen 0, peer address and port from the code)'
    Assert-True (Has $r "Save sharing: if your co-op save starts somewhere else than the host's, you get the host's save at the title screen and MGSV restarts once (your own copy is kept; Share-Save.bat [R]). -SaveHandoff off turns this off.") 'the joiner is told about the automatic save handoff'
    $joinLab = (Get-SessionJson $jp).lab
    Assert-Equal "$($joinLab.role)|$($joinLab.transport)|$($joinLab.port)|$($joinLab.peer)|$($joinLab.world)|$($joinLab.partner)|$($joinLab.save_handoff)|$($joinLab.travel_lock)" 'guest|udp|47831|192.0.2.44|3|native|scene|on' 'play-session.json records the lab.ini settings'
    Assert-Equal ([IO.File]::ReadAllText($jp.LinkSecret)) "$s7`r`n" 'guest link secret from the code'
    Assert-Equal ([IO.File]::ReadAllText($jp.CoopIni)) (New-CoopPlayCoopIniText -Character quiet) 'guest coop.ini: Quiet'
    # Deactivation keeps a file that changed after Play-Coop wrote it.
    [IO.File]::AppendAllText($jp.LabIni, "partner_lost=hide`n")
    $r = Invoke-Friend $play @('-GameDirectory', $joinGame, '-Cleanup', '-NonInteractive')
    Assert-True ($r.Exit -eq 0 -and (Has $r 'lab.ini was changed after Play-Coop wrote it')) "cleanup reports the changed file: $($r.Text)"
    Assert-True ((Test-Path -LiteralPath $jp.LabIni) -and !(Test-Path -LiteralPath $jp.LinkSecret) -and !(Test-Path -LiteralPath $jp.CoopIni) -and !(Test-Path -LiteralPath $jp.Marker)) 'only the owned, unchanged files were deleted (marker first)'
    Assert-Equal (Get-SessionJson $jp).state 'off' 'cleanup set the session off'
    $r = Invoke-Friend $play @('-GameDirectory', $joinGame, '-Cleanup', '-NonInteractive')
    Assert-True ($r.Exit -eq 0 -and (Has $r 'nothing to clean up')) 'cleanup is idempotent'
    # The changed lab.ini is not ours any more: the next start refuses to overwrite it.
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($joinCode) -NoWatchdog
    Assert-Refused $r 'a Play-Coop file that was changed afterwards' 'a modified play lab.ini is refused'
    Remove-Item -LiteralPath $jp.LabIni
    # A foreign lab.ini / coop.ini / marker is refused and never overwritten.
    $labIniText = "# Generated by tools/lab/Prepare-LabInstance.ps1 - single-PC lab harness only.`nenabled=1`nrole=guest`n"
    [IO.File]::WriteAllText($jp.LabIni, $labIniText)
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($joinCode) -NoWatchdog
    Assert-Refused $r 'a file Play-Coop did not write' 'a lab lab.ini is refused'
    Assert-Equal ([IO.File]::ReadAllText($jp.LabIni)) $labIniText 'the foreign lab.ini is unchanged'
    Remove-Item -LiteralPath $jp.LabIni
    [IO.File]::WriteAllText($jp.CoopIni, "character=snake`n")
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($joinCode) -NoWatchdog
    Assert-Refused $r 'coop.ini exists' 'a user coop.ini is refused'
    Remove-Item -LiteralPath $jp.CoopIni
    [IO.File]::WriteAllText($jp.Marker, "client-v1`n")
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($joinCode) -NoWatchdog
    Assert-Refused $r 'script-observe.enabled exists but was not written by this Play-Coop session' 'a foreign marker is refused'
    Assert-Equal ([IO.File]::ReadAllText($jp.Marker)) "client-v1`n" 'the foreign marker is unchanged'
    Remove-Item -LiteralPath $jp.Marker
    # A marker that is not exactly offline-v1 is kept by the cleanup.
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($joinCode) -NoWatchdog
    Assert-Equal $r.Exit 0 "join again: $($r.Raw)"
    [IO.File]::WriteAllText($jp.Marker, "offline-v1`r`n")
    $r = Invoke-Friend $play @('-GameDirectory', $joinGame, '-Cleanup', '-NonInteractive')
    Assert-True ((Has $r 'script-observe.enabled was changed after Play-Coop wrote it') -and (Test-Path -LiteralPath $jp.Marker) -and !(Test-Path -LiteralPath $jp.LabIni)) 'a non-ours marker is kept, the owned files go'
    Remove-Item -LiteralPath $jp.Marker
    # Leftover cleanup at the next start: a session whose window and helper are gone.
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($joinCode) -NoWatchdog
    Assert-Equal $r.Exit 0 "join without a helper: $($r.Raw)"
    $firstSession = (Get-SessionJson $jp).session_id
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($joinCode) -NoWatchdog
    Assert-True ($r.Exit -eq 0 -and (Has $r 'did not end cleanly; its files were cleaned up')) "the next Play-Coop cleans up first: $($r.Text)"
    Assert-True ((Get-SessionJson $jp).session_id -cne $firstSession -and (Get-PlayFilesPresent $jp) -eq 4) 'a new session after the leftover cleanup'
    # Install-Coop cleans up an active session too (and stays pass-through).
    $r = Invoke-Friend $install (@('-GameDirectory', $joinGame, '-Role', 'Join') + $common)
    Assert-True ($r.Exit -eq 0 -and (Has $r 'Co-op files of an earlier Play-Coop session were removed')) "install cleans up a play session: $($r.Text)"
    Assert-PassThrough $jp 'install after a play session'
    # A session whose launcher window still runs is not a leftover: Play-Coop,
    # Install-Coop and Uninstall-Coop refuse; -Cleanup still forces it.
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($joinCode) -NoWatchdog
    Assert-Equal $r.Exit 0 "join for the live-session test: $($r.Raw)"
    $liveTable = ConvertTo-CoopSessionTable -Session (Get-SessionJson $jp)
    $liveTable['launcher_pid'] = $PID
    $liveTable['launcher_start_utc'] = (Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')
    Save-CoopPlaySession -GameRoot $joinGame -Session $liveTable
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($joinCode) -NoWatchdog
    Assert-Refused $r 'Another Play-Coop window is active for this folder' 'a second Play-Coop leaves a live session alone'
    $r = Invoke-Friend $install (@('-GameDirectory', $joinGame, '-Role', 'Join') + $common)
    Assert-Refused $r 'Another Play-Coop window is active for this folder' 'Install-Coop leaves a live session alone'
    $r = Invoke-Friend $uninstall @('-GameDirectory', $joinGame, '-NonInteractive', '-Yes')
    Assert-Refused $r 'Another Play-Coop window is active for this folder' 'Uninstall-Coop leaves a live session alone'
    Assert-True ((Get-PlayFilesPresent $jp) -eq 4 -and (Get-SessionJson $jp).state -eq 'active') 'the live session keeps its files'
    $r = Invoke-Friend $play @('-GameDirectory', $joinGame, '-Cleanup', '-NonInteractive')
    Assert-True ($r.Exit -eq 0 -and (Get-PlayFilesPresent $jp) -eq 0) "-Cleanup forces a live session off: $($r.Text)"
    # A stray link-secret.txt (for example from the single-PC lab -Lan) stops
    # Play-Coop; Install-Coop sets it aside (never copied), then Play-Coop works.
    $secretsBefore = @(Get-ChildItem -LiteralPath $jp.Mod -Filter 'link-secret.txt.*' -Force | ForEach-Object { $_.Name })
    [IO.File]::WriteAllText($jp.LinkSecret, "$s7`r`n")
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($joinCode) -NoWatchdog
    Assert-Refused $r 'link-secret.txt exists but was not written by this Play-Coop session' 'a stray link secret is refused'
    $r = Invoke-Friend $install (@('-GameDirectory', $joinGame, '-Role', 'Join') + $common)
    $asideSecrets = @(Get-ChildItem -LiteralPath $jp.Mod -Filter 'link-secret.txt.*' -Force | Where-Object { $secretsBefore -notcontains $_.Name })
    Assert-True ($r.Exit -eq 0 -and !(Test-Path -LiteralPath $jp.LinkSecret) -and $asideSecrets.Count -eq 1 -and $asideSecrets[0].Name -like 'link-secret.txt.disabled-*') "install sets a stray link secret aside (never copied): $($r.Text)"
    $asideRecords = @((Get-Content -LiteralPath (Join-Path $jp.Mod 'friend-install.json') -Raw | ConvertFrom-Json).set_aside | Where-Object { $_ -and $_.name -eq 'link-secret.txt' })
    Assert-True ($asideRecords.Count -ge 1) 'set_aside records the link secret'
    Assert-True (!$r.Raw.Contains($s7)) 'the set-aside secret is never printed'
    Remove-Item -LiteralPath $asideSecrets[0].FullName
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($joinCode) -NoWatchdog
    Assert-Equal $r.Exit 0 "Play-Coop works after the install set the secret aside: $($r.Raw)"
    $r = Invoke-Friend $play @('-GameDirectory', $joinGame, '-Cleanup', '-NonInteractive')
    Assert-PassThrough $jp 'cleanup after the stray-secret test'
    # A DNS name that points to this PC is refused (the launcher resolves names, IPv4 only).
    $localCode = New-CoopJoinCode -Secret $s7 -HostName 'localhost' -Port 47830
    $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join') @($localCode) -NoWatchdog
    Assert-True ($r.Exit -ne 0 -and ((Has $r 'cannot be used') -or (Has $r 'no IPv4 address'))) "a name resolving to this PC is refused: $($r.Text)"
    Assert-PassThrough $jp 'refused join'
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'real saves untouched by Play-Coop'

    # ------------------------------------------------------------ Play-Coop online (Steam; docs/steam-online.md 10.1)
    try {
        # Steam not running / not logged in: refused, nothing written.
        $env:MGS_COOP_TEST_STEAM_ACTIVE_USER = '0'
        $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-Role', 'Host', '-Transport', 'steam') -NoWatchdog
        Assert-Refused $r 'Steam is not running or not logged in' 'online host needs Steam logged in'
        Assert-PassThrough $hp 'refused online host'
        # Host online: an MGS2 code for this Steam account; no UDP, UPnP or firewall steps.
        $env:MGS_COOP_TEST_STEAM_ACTIVE_USER = '97000001'
        $hostSteam = Get-CoopSteamId64 -ActiveUser 97000001
        $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-Role', 'Host', '-Transport', 'steam') -NoWatchdog
        Assert-True ($r.Exit -eq 0 -and (Has $r 'TEST: leaving after the activation.')) "online host activates: $($r.Raw)"
        $steamMatch = [regex]::Match($r.Raw, 'MGS2-[0-9A-Z]{5}-[0-9A-Z]{5}-[0-9A-Z]{5}-[0-9A-Z]{5}-[0-9A-Z]{2}@[0-9]{17}')
        Assert-True $steamMatch.Success "the online host shows an MGS2 code: $($r.Raw)"
        $steamParsed = ConvertFrom-CoopSteamJoinCode $steamMatch.Value
        Assert-Equal "$($steamParsed.Status)|$($steamParsed.SteamId)" "ok|$hostSteam" 'the code names this Steam account'
        Assert-Equal ([regex]::Matches($r.Raw, [regex]::Escape($steamMatch.Value)).Count) 1 'the online code is shown once'
        Assert-True ((Has $r 'F9 -> Partner / Co-op -> Online -> Invite') -and !(Has $r 'Tailscale') -and !(Has $r 'api.ipify.org') -and !(Has $r 'UPnP') -and !(Has $r 'Firewall rule')) 'online host: invite hint; no VPN, public IP, UPnP or firewall steps'
        Assert-Equal ([IO.File]::ReadAllText($hp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role host -SaveDir $hostSaveDir -Transport steam -SteamSelf $hostSteam -SaveHandoff scene) 'online host lab.ini'
        Assert-Equal ([IO.File]::ReadAllText($hp.LinkSecret)) "$($steamParsed.Secret)`r`n" 'online host link secret = the code''s secret'
        $session = Get-SessionJson $hp
        Assert-Equal "$($session.role)|$($session.transport)|$(@($session.files | ForEach-Object { $_.name }) -join ',')" 'host|steam|lab.ini,link-secret.txt,coop.ini,script-observe.enabled' 'online host session'
        Assert-True (![IO.File]::ReadAllText($hp.PlaySession).Contains($steamParsed.Secret)) 'play-session.json holds no secret'
        $r = Invoke-Friend $play @('-GameDirectory', $hostGame, '-Cleanup', '-NonInteractive')
        Assert-PassThrough $hp 'online host cleanup'
        # Join online with the code (blanks and lower case).
        $env:MGS_COOP_TEST_STEAM_ACTIVE_USER = '97000002'
        $guestSteam = Get-CoopSteamId64 -ActiveUser 97000002
        $jSaveDirOnline = Join-Path $jp.Mod 'coop-saves'
        $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join', '-Transport', 'steam') @(' ' + $steamMatch.Value.ToLowerInvariant() + ' ') -NoWatchdog
        Assert-True ($r.Exit -eq 0 -and (Has $r ('Code OK: host Steam account ...' + $hostSteam.Substring(13)))) "online join with a code: $($r.Raw)"
        Assert-Equal ([IO.File]::ReadAllText($jp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role guest -SaveDir $jSaveDirOnline -Transport steam -SteamSelf $guestSteam -SteamPeer $hostSteam -SaveHandoff scene) 'online guest lab.ini'
        Assert-Equal ([IO.File]::ReadAllText($jp.LinkSecret)) "$($steamParsed.Secret)`r`n" 'online guest secret from the code'
        $r = Invoke-Friend $play @('-GameDirectory', $joinGame, '-Cleanup', '-NonInteractive')
        Assert-PassThrough $jp 'online join cleanup'
        # Wait for an invite (Enter): no secret is written.
        $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join', '-Transport', 'steam') @('') -NoWatchdog
        Assert-True ($r.Exit -eq 0 -and (Has $r 'Waiting for an invite')) "online join waits for an invite: $($r.Raw)"
        Assert-Equal ([IO.File]::ReadAllText($jp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role guest -SaveDir $jSaveDirOnline -Transport steam -SteamSelf $guestSteam -SteamWait -SaveHandoff scene) 'waiting guest lab.ini'
        $waitLab = (Get-SessionJson $jp).lab
        Assert-Equal "$($waitLab.transport)|$($waitLab.steam_self)|$($waitLab.steam_peer)|$($waitLab.steam_wait)|$($waitLab.peer)" "steam|$guestSteam||True|" 'a waiting guest records steam_wait (the save handoff rejoin needs it)'
        Assert-True (!(Test-Path -LiteralPath $jp.LinkSecret)) 'a waiting guest has no link secret'
        Assert-Equal (@((Get-SessionJson $jp).files | ForEach-Object { $_.name }) -join ',') 'lab.ini,coop.ini,script-observe.enabled' 'waiting guest session files'
        $r = Invoke-Friend $play @('-GameDirectory', $joinGame, '-Cleanup', '-NonInteractive')
        Assert-PassThrough $jp 'waiting join cleanup'
        # One's own code is refused (then wait).
        $ownCode = New-CoopSteamJoinCode -Secret $s7 -SteamId $guestSteam
        $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join', '-Transport', 'steam') @($ownCode, '') -NoWatchdog
        Assert-True ($r.Exit -eq 0 -and (Has $r 'That is your own code') -and !(Test-Path -LiteralPath $jp.LinkSecret)) "own code refused: $($r.Raw)"
        $r = Invoke-Friend $play @('-GameDirectory', $joinGame, '-Cleanup', '-NonInteractive')
        # An MGS1 code in Join online joins over LAN/VPN.
        $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join', '-Transport', 'steam') @($joinCode) -NoWatchdog
        Assert-True ($r.Exit -eq 0 -and (Has $r 'joining over the LAN/VPN link')) "MGS1 falls through to LAN/VPN: $($r.Raw)"
        Assert-Equal ([IO.File]::ReadAllText($jp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role guest -SaveDir $jSaveDirOnline -Port 47831 -PeerAddress '192.0.2.44' -SaveHandoff scene) 'MGS1 in Join online: the UDP lab.ini'
        $r = Invoke-Friend $play @('-GameDirectory', $joinGame, '-Cleanup', '-NonInteractive')
        # The menu: O hosts online; L then H is the LAN/VPN host; Q changes nothing.
        $env:MGS_COOP_TEST_STEAM_ACTIVE_USER = '97000001'
        $r = Invoke-Play $play @('-GameDirectory', $hostGame) @('O') -NoWatchdog
        Assert-True ($r.Exit -eq 0 -and (Has $r '[O] Host online (Steam, recommended)') -and [regex]::IsMatch($r.Raw, 'MGS2-[0-9A-Z]{5}-')) "menu O: $($r.Raw)"
        $r = Invoke-Friend $play @('-GameDirectory', $hostGame, '-Cleanup', '-NonInteractive')
        $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-HostAddress', '192.0.2.10') @('L', 'H') -NoWatchdog
        Assert-True ($r.Exit -eq 0 -and (Has $r 'Host or Join?') -and [regex]::IsMatch($r.Raw, 'MGS1-[0-9A-Z]{5}-')) "menu L, H: the LAN/VPN host: $($r.Raw)"
        Assert-Equal ([IO.File]::ReadAllText($hp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role host -SaveDir $hostSaveDir -Port 47830 -SaveHandoff scene) 'menu L, H: UDP lab.ini unchanged'
        $r = Invoke-Friend $play @('-GameDirectory', $hostGame, '-Cleanup', '-NonInteractive')
        Assert-PassThrough $hp 'menu cleanup'
        $r = Invoke-Play $play @('-GameDirectory', $hostGame) @('Q') -NoWatchdog
        Assert-True ($r.Exit -eq 0 -and (Has $r 'Nothing was changed.')) 'menu Q'
        Assert-PassThrough $hp 'menu Q writes nothing'
    } finally { Remove-Item Env:MGS_COOP_TEST_STEAM_ACTIVE_USER -ErrorAction SilentlyContinue }
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'real saves untouched by Play-Coop online'

    # The helper waits for the game: a stand-in mgsvtpp.exe runs from the folder.
    if ($FakeProcess) {
        $realExe = [IO.File]::ReadAllBytes($hp.Exe)
        $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-Role', 'Host', '-HostAddress', '192.0.2.10')
        Assert-Equal $r.Exit 0 "activation for the game-exit test: $($r.Raw)"
        Copy-Item -LiteralPath $FakeProcess -Destination $hp.Exe -Force
        $port = 0
        foreach ($i in 1..50) { $candidate = Get-Random -Minimum 49152 -Maximum 65000; if (Test-LocalPortFree -Port $candidate) { $port = $candidate; break } }
        $fakeGame = Start-Process -FilePath $hp.Exe -ArgumentList '-standalone', '-listenAddr', "127.0.0.1:$port" -WorkingDirectory $hostGame -PassThru
        $watchdogs.Add([int](Get-SessionJson $hp).watchdog_pid)
        Start-Sleep -Seconds 8
        Assert-True (!$fakeGame.HasExited -and (Get-PlayFilesPresent $hp) -eq 4) 'while the game runs, co-op stays on (window gone)'
        $r = Invoke-Friend $play @('-GameDirectory', $hostGame, '-Cleanup', '-NonInteractive')
        Assert-Refused $r 'MGSV is running from this folder' '-Cleanup waits for the game to exit'
        Stop-Process -Id $fakeGame.Id -Force
        $fakeGame.WaitForExit(5000) | Out-Null
        $fakeGame = $null
        Assert-True (Wait-Until { (Get-PlayFilesPresent $hp) -eq 0 } 30) 'the helper switches co-op off when the game exits'
        [IO.File]::WriteAllBytes($hp.Exe, $realExe)

        # The whole launcher: activation, launch (a stand-in game instead of Steam),
        # the start evidence in the new log bytes, the status line, cleanup at game exit.
        function Invoke-PlayLaunch([string]$Name, [string[]]$EarlyLines, [string[]]$SteamLines, [string[]]$ObserverLines, [string]$StatusJson, [string]$WaitFor) {
            $in = Join-Path $fixture "$Name-in.txt"; $outFile = Join-Path $fixture "$Name-out.txt"; $errFile = Join-Path $fixture "$Name-err.txt"
            [IO.File]::WriteAllText($in, "`r`n`r`n")
            $env:MGS_COOP_TEST_INTERACTIVE = '1'; $env:MGS_COOP_TEST_NO_LAUNCH = '1'; $env:MGS_COOP_TEST_NO_WATCHDOG = '1'
            # Windows PowerShell uses its own module path (as the .bat files arrange).
            $savedModulePath = $env:PSModulePath
            $env:PSModulePath = ''
            try {
                $launcher = Start-Process -FilePath $winPs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $play + '"'), '-GameDirectory', ('"' + $hostGame + '"'), '-Role', 'Host', '-HostAddress', '192.0.2.10') `
                    -RedirectStandardInput $in -RedirectStandardOutput $outFile -RedirectStandardError $errFile -NoNewWindow -PassThru
            } finally {
                $env:PSModulePath = $savedModulePath
                foreach ($v in 'MGS_COOP_TEST_INTERACTIVE', 'MGS_COOP_TEST_NO_LAUNCH', 'MGS_COOP_TEST_NO_WATCHDOG') { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
            }
            $read = { if (Test-Path -LiteralPath $outFile) { Read-SharedText -Path $outFile } else { '' } }
            try {
                Assert-True (Wait-Until { (& $read).Contains('Waiting for MGSV') } 90) "[$Name] the launcher waits for the game: $(& $read)"
                Copy-Item -LiteralPath $FakeProcess -Destination $hp.Exe -Force
                $gamePort = 0
                foreach ($i in 1..50) { $candidate = Get-Random -Minimum 49152 -Maximum 65000; if (Test-LocalPortFree -Port $candidate) { $gamePort = $candidate; break } }
                $script:fakeGame = Start-Process -FilePath $hp.Exe -ArgumentList '-standalone', '-listenAddr', "127.0.0.1:$gamePort" -WorkingDirectory $hostGame -PassThru
                Assert-True (Wait-Until { (& $read).Contains('MGSV started') } 30) "[$Name] the launcher sees the game"
                [IO.File]::AppendAllText((Join-Path $hp.Mod 'early.log'), (($EarlyLines -join "`r`n") + "`r`n"))
                [IO.File]::AppendAllText((Join-Path $hp.Mod 'steam-observer.log'), (($SteamLines -join "`r`n") + "`r`n"))
                [IO.File]::AppendAllText((Join-Path $hp.Mod 'script-observer.log'), (($ObserverLines -join "`r`n") + "`r`n"))
                [IO.File]::WriteAllText($hp.LinkStatus, $StatusJson.Replace('@PID@', [string]$script:fakeGame.Id).Replace('@NOW@', [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')))
                Assert-True (Wait-Until { (& $read).Contains($WaitFor) } 30) "[$Name] the launcher shows '$WaitFor': $(& $read)"
                Assert-True ((Get-PlayFilesPresent $hp) -eq 4) "[$Name] co-op stays on while the game runs"
                Stop-Process -Id $script:fakeGame.Id -Force
                $script:fakeGame.WaitForExit(5000) | Out-Null
                $script:fakeGame = $null
                Assert-True ($launcher.WaitForExit(30000)) "[$Name] the launcher ends when the game exits"
                return (& $read)
            } finally {
                if (!$launcher.HasExited) { Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue }
                [IO.File]::WriteAllBytes($hp.Exe, $realExe)
            }
        }
        $liveStatus = '{"v":1,"pid":@PID@,"mode":"play","role":"host","state":"connected","reason":"","verified":1,"session":1,"peer":"192.0.2.44:51234","local_port":47830,"rtt_ms":42,"jitter_ms":3,"loss_permille":4,"resend":0,"tx_Bps":3900,"rx_Bps":3800,"interp_ms":80,"remote_build":"0.12.0","partner":"live","partner_shown":1,"look":1,"mission":30050,"saves":"redirected","time_utc":"@NOW@"}'
        $out = Invoke-PlayLaunch 'launch-ok' @('early=start; version=0.12.0', "saves=redirected; dir=$hostSaveDir", 'play=1; loopback=0; mutex=unchanged; refused=0x0; saves=redirected') `
            @('master_server=redirected; method=test; url=http://127.0.0.1:47899/tppstm/gate') @('play=active; role=host; partner=1; refused=0x0') $liveStatus '[HOST] link=connected rtt=42ms'
        Assert-True ($out.Contains('Co-op is active and the game plays from the save COPY.') -and $out.Contains('| partner=live shown look | mission 30050') -and $out.Contains('MGSV exited.') -and !$out.Contains('NOT active')) 'launcher: evidence ok, status line, game exit'
        Assert-True ((Get-PlayFilesPresent $hp) -eq 0 -and (Get-SessionJson $hp).state -eq 'off') 'launcher: co-op switched off at game exit'
        $out = Invoke-PlayLaunch 'launch-refused' @('early=start; version=0.12.0', 'saves=unchanged') @('master_server=redirected; method=test; url=http://127.0.0.1:47899/tppstm/gate') `
            @('play=refused; reason=saves_not_redirected; refused=0x0') ($liveStatus.Replace('"state":"connected"', '"state":"off"')) 'save protection is NOT active'
        Assert-True ($out.Contains('Co-op did NOT start and save protection is NOT active: this game is normal offline play on your REAL saves. Quit MGSV now if you do not want that.')) 'launcher: the red banner when the saves are not redirected'
        Assert-True ((Get-PlayFilesPresent $hp) -eq 0) 'launcher: cleanup after the refused start as well'

        # The automatic save handoff through the whole launcher (a joiner, docs/two-pc-play.md
        # "Save handoff"): the stand-in game stages the host's save (handoff.json, then
        # request.json with its own pid and start) and exits; Play-Coop applies it and starts
        # MGSV again with no cleanup in between, attaches to a second stand-in and switches
        # co-op off only after that one exits. Steam is never started (MGS_COOP_TEST_NO_LAUNCH=1).
        function Start-StandIn([string]$Game) {
            $exe = Join-Path $Game 'mgsvtpp.exe'
            Copy-Item -LiteralPath $FakeProcess -Destination $exe -Force
            $standInPort = 0
            foreach ($i in 1..50) { $candidate = Get-Random -Minimum 49152 -Maximum 65000; if (Test-LocalPortFree -Port $candidate) { $standInPort = $candidate; break } }
            return (Start-Process -FilePath $exe -ArgumentList '-standalone', '-listenAddr', "127.0.0.1:$standInPort" -WorkingDirectory $Game -PassThru)
        }
        function Stop-StandIn($Process) { if ($Process -and !$Process.HasExited) { Stop-Process -Id $Process.Id -Force; $null = $Process.WaitForExit(5000) } }
        function Get-StandInStartUtc($Process) { return $Process.StartTime.ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ') }
        function Add-StandInEvidence($Paths) {
            [IO.File]::AppendAllText((Join-Path $Paths.Mod 'early.log'), "early=start; version=$packageVersion`r`nsaves=redirected; dir=$($Paths.CoopSaves)`r`nplay=1; loopback=0; mutex=unchanged; refused=0x0; saves=redirected`r`n")
            [IO.File]::AppendAllText((Join-Path $Paths.Mod 'steam-observer.log'), "master_server=redirected; method=test; url=http://127.0.0.1:47899/tppstm/gate`r`n")
            [IO.File]::AppendAllText((Join-Path $Paths.Mod 'script-observer.log'), "play=active; role=guest; partner=1; refused=0x0`r`n")
        }
        $hoGame = New-GameFixture 'handoff-launch'
        $hop = Get-InstancePaths -GameDirectory $hoGame
        $r = Invoke-Friend $install (@('-GameDirectory', $hoGame, '-Role', 'Join', '-RuntimeArchive', $archiveFx) + $common)
        Assert-Equal $r.Exit 0 "install for the handoff launcher test: $($r.Raw)"
        $hoRealExe = [IO.File]::ReadAllBytes($hop.Exe)
        $hoIn = Join-Path $fixture 'handoff-launch-in.txt'; $hoOut = Join-Path $fixture 'handoff-launch-out.txt'; $hoErr = Join-Path $fixture 'handoff-launch-err.txt'
        [IO.File]::WriteAllText($hoIn, "$joinCode`r`n`r`n`r`n")
        $env:MGS_COOP_TEST_INTERACTIVE = '1'; $env:MGS_COOP_TEST_NO_LAUNCH = '1'; $env:MGS_COOP_TEST_NO_WATCHDOG = '1'; $env:MGS_COOP_TEST_RELAUNCH_DELAY_SEC = '1'
        $savedModulePath = $env:PSModulePath
        $env:PSModulePath = ''
        try {
            $hoLauncher = Start-Process -FilePath $winPs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $play + '"'), '-GameDirectory', ('"' + $hoGame + '"'), '-Role', 'Join') `
                -RedirectStandardInput $hoIn -RedirectStandardOutput $hoOut -RedirectStandardError $hoErr -NoNewWindow -PassThru
        } finally {
            $env:PSModulePath = $savedModulePath
            foreach ($v in 'MGS_COOP_TEST_INTERACTIVE', 'MGS_COOP_TEST_NO_LAUNCH', 'MGS_COOP_TEST_NO_WATCHDOG', 'MGS_COOP_TEST_RELAUNCH_DELAY_SEC') { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
        }
        $hoRead = { if (Test-Path -LiteralPath $hoOut) { Read-SharedText -Path $hoOut } else { '' } }
        $standIn1 = $null; $standIn2 = $null
        try {
            Assert-True (Wait-Until { (& $hoRead).Contains('Waiting for MGSV') } 90) "[handoff] the launcher waits for the game: $(& $hoRead)"
            $hoOwnBefore = Get-RelativeTreeHashes $hop.CoopSaves
            $standIn1 = Start-StandIn $hoGame
            Assert-True (Wait-Until { (& $hoRead).Contains('MGSV started') } 30) '[handoff] the launcher sees the first game'
            Add-StandInEvidence $hop
            $hoStatus = '{"v":1,"pid":@PID@,"mode":"play","role":"guest","state":"connected","reason":"","verified":1,"session":1,"peer":"192.0.2.44:47831","local_port":0,"rtt_ms":42,"jitter_ms":3,"loss_permille":4,"resend":0,"tx_Bps":3900,"rx_Bps":3800,"interp_ms":80,"remote_build":"0.12.0","partner":"live","partner_shown":1,"look":1,"mission":40050,"saves":"redirected","handoff":"countdown","handoff_pct":100,"time_utc":"@NOW@"}'
            [IO.File]::WriteAllText($hop.LinkStatus, $hoStatus.Replace('@PID@', [string]$standIn1.Id).Replace('@NOW@', [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')))
            Assert-True (Wait-Until { $t = & $hoRead; $t.Contains('| host save ready, MGSV restarts | mission 40050') -and $t.Contains("Host's save ready: MGSV restarts at the title screen") } 30) "[handoff] the status segment and the one-time sentence: $(& $hoRead)"
            # The stand-in stages the host's save, writes its request last and exits.
            $hoStage = New-HandoffStage $hop.CoopSavesHandoff '9CADBECF' @{ '287700/local/TPP_GAME_DATA0' = 'host progress for the launcher'; 'remote/TPP_GAME_DATA' = 'host remote for the launcher' }
            $null = New-HandoffRequest $hop.CoopSavesHandoff $hoStage $standIn1.Id (Get-StandInStartUtc $standIn1)
            Stop-StandIn $standIn1
            Assert-True (Wait-Until { (& $hoRead).Contains('Starting MGSV again with the same join code...') } 60) "[handoff] the launcher applies and restarts: $(& $hoRead)"
            $hoText = & $hoRead
            Assert-True ($hoText.Contains("MGSV closed to load the host's save.") -and $hoText.Contains("Applying the host's save (2 files, 1 KB)...") -and
                [regex]::IsMatch($hoText, 'Your previous co-op save is kept at mgs-coop\\coop-saves-replaced\\[0-9]{8}-[0-9]{6}(-[0-9]+)?-before-handoff \(Share-Save\.bat \[R\] brings it back\)\.') -and
                !$hoText.Contains('MGSV exited.')) "[handoff] the restart lines and no exit line: $hoText"
            Assert-True ((Get-PlayFilesPresent $hop) -eq 4) '[handoff] no cleanup in between: all four co-op files stay'
            $hoSession = Get-SessionJson $hop
            Assert-Equal "$($hoSession.state)|$(Get-HandoffField $hop 'state')|$(Get-HandoffField $hop 'count')|$(Get-HandoffField $hop 'claimed_by_pid')" "active|relaunching|1|$($hoLauncher.Id)" '[handoff] the launcher claimed and restarted'
            Assert-True ([IO.File]::ReadAllText($hop.LabIni).Contains("save_handoff=off`n") -and (@($hoSession.files | Where-Object { $_.name -eq 'lab.ini' })[0]).sha256 -eq (Get-Sha256 $hop.LabIni)) '[handoff] lab.ini again with save_handoff=off, its hash recorded'
            foreach ($row in $hoStage.Rows) { Assert-Equal (Get-Sha256 (Join-Path $hop.CoopSaves $row.entry.Replace('/', '\'))) $row.sha256 "[handoff] the host's $($row.entry) is in the co-op copy" }
            $hoAside = @(Get-CoopProgressBackups -ReplacedRoot $hop.CoopSavesReplaced | Where-Object { $_.Kind -eq 'handoff' })
            Assert-True ($hoAside.Count -eq 1 -and (Get-RelativeTreeHashes $hoAside[0].Path) -eq $hoOwnBefore) '[handoff] the own copy is kept aside'
            # Steam would start MGSV now: the second stand-in.
            $standIn2 = Start-StandIn $hoGame
            Assert-True (Wait-Until { (& $hoRead).Contains("MGSV restarted (process $($standIn2.Id)).") } 30) "[handoff] the launcher attaches to the restarted game: $(& $hoRead)"
            Add-StandInEvidence $hop
            Assert-True (Wait-Until { (Get-HandoffField $hop 'state') -eq 'relaunched' -and [int](Get-FriendProperty (Get-SessionJson $hop) 'game_pid' 0) -eq $standIn2.Id } 15) '[handoff] state relaunched with the new game recorded'
            [IO.File]::WriteAllText($hop.LinkStatus, $hoStatus.Replace('"handoff":"countdown","handoff_pct":100', '"handoff":"applied","handoff_pct":0').Replace('"mission":40050', '"mission":30010').Replace('@PID@', [string]$standIn2.Id).Replace('@NOW@', [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')))
            Assert-True (Wait-Until { (& $hoRead).Contains("| playing on the host's save | mission 30010") } 30) "[handoff] the status line after the restart: $(& $hoRead)"
            Assert-True ((Get-PlayFilesPresent $hop) -eq 4) '[handoff] co-op stays on while the restarted game runs'
            Stop-StandIn $standIn2
            Assert-True ($hoLauncher.WaitForExit(60000)) '[handoff] the launcher ends when the restarted game exits'
            $hoText = & $hoRead
            Assert-True ($hoText.Contains('MGSV exited.') -and (Get-PlayFilesPresent $hop) -eq 0 -and (Get-SessionJson $hop).state -eq 'off') "[handoff] 'MGSV exited.' and the cleanup after the second game: $hoText"
        } finally {
            Stop-StandIn $standIn1; Stop-StandIn $standIn2
            if (!$hoLauncher.HasExited) { Stop-Process -Id $hoLauncher.Id -Force -ErrorAction SilentlyContinue }
            [IO.File]::WriteAllBytes($hop.Exe, $hoRealExe)
        }

        # Review SS-4: the launcher waits for a restart the helper claimed (claimed_by_other); the
        # helper's restart then fails (state failed, co-op still on) or the helper switches co-op off.
        # The window stops waiting at once, never says "start MGS V from Steam yourself", and exits 0
        # with co-op off.
        foreach ($outcome in @('failed', 'off')) {
            $coIn = Join-Path $fixture "handoff-claimed-$outcome-in.txt"; $coOut = Join-Path $fixture "handoff-claimed-$outcome-out.txt"; $coErr = Join-Path $fixture "handoff-claimed-$outcome-err.txt"
            [IO.File]::WriteAllText($coIn, "$joinCode`r`n`r`n`r`n")
            $env:MGS_COOP_TEST_INTERACTIVE = '1'; $env:MGS_COOP_TEST_NO_LAUNCH = '1'; $env:MGS_COOP_TEST_NO_WATCHDOG = '1'; $env:MGS_COOP_TEST_RELAUNCH_DELAY_SEC = '1'
            $savedModulePath = $env:PSModulePath
            $env:PSModulePath = ''
            try {
                $coLauncher = Start-Process -FilePath $winPs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $play + '"'), '-GameDirectory', ('"' + $hoGame + '"'), '-Role', 'Join') `
                    -RedirectStandardInput $coIn -RedirectStandardOutput $coOut -RedirectStandardError $coErr -NoNewWindow -PassThru
                $null = $coLauncher.Handle  # keeps the exit code readable after WaitForExit
            } finally {
                $env:PSModulePath = $savedModulePath
                foreach ($v in 'MGS_COOP_TEST_INTERACTIVE', 'MGS_COOP_TEST_NO_LAUNCH', 'MGS_COOP_TEST_NO_WATCHDOG', 'MGS_COOP_TEST_RELAUNCH_DELAY_SEC') { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
            }
            $coRead = { if (Test-Path -LiteralPath $coOut) { Read-SharedText -Path $coOut } else { '' } }
            $standIn1 = $null; $coHelper = $null
            try {
                Assert-True (Wait-Until { (& $coRead).Contains('Waiting for MGSV') } 90) "[claimed $outcome] the launcher waits for the game: $(& $coRead)"
                $standIn1 = Start-StandIn $hoGame
                Assert-True (Wait-Until { (& $coRead).Contains('MGSV started') } 30) "[claimed $outcome] the launcher sees the game"
                Add-StandInEvidence $hop
                # The helper (a live stand-in process) claimed the restart while this window was busy.
                $coHelper = Start-Process -FilePath $winPs -ArgumentList '-NoProfile', '-Command', 'Start-Sleep -Seconds 300' -WindowStyle Hidden -PassThru
                $coHelperStart = Get-CoopProcessStartUtc -Id $coHelper.Id
                $null = Update-CoopPlaySession -GameRoot $hoGame -Update {
                    param($t)
                    $t['handoff'] = [ordered]@{ state = 'relaunching'; claimed_by_pid = $coHelper.Id; claimed_start_utc = $coHelperStart; claimed_utc = [DateTime]::UtcNow.ToString('o')
                        xfer = '8E9FA0B1'; set_sha256 = ('C' * 64); count = 1; aside = '20261015-120000-before-handoff'; message = '' }
                }
                Stop-StandIn $standIn1
                Assert-True (Wait-Until { (& $coRead).Contains("The background helper is restarting MGSV with the host's save...") } 60) "[claimed $outcome] claimed_by_other: the window waits: $(& $coRead)"
                Start-Sleep -Seconds 2
                Assert-True (!$coLauncher.HasExited -and (Get-PlayFilesPresent $hop) -eq 4) "[claimed $outcome] the restart is pending: co-op stays on"
                if ($outcome -eq 'failed') {
                    # The helper's own Start-CoopRelaunch failed; its cleanup has not run yet.
                    $null = Update-CoopPlaySession -GameRoot $hoGame -Update { param($t) $h = Get-CoopSessionHandoff $t; $h['state'] = 'failed'; $h['message'] = 'mgs-coop\coop.ini changed'; $t['handoff'] = $h }
                } else {
                    $null = Invoke-CoopPlayCleanup -GameRoot $hoGame -Force
                }
                Assert-True ($coLauncher.WaitForExit(15000)) "[claimed $outcome] the window stops waiting at once: $(& $coRead)"
                $coText = & $coRead
                Assert-Equal $coLauncher.ExitCode 0 "[claimed $outcome] exit code 0"
                Assert-True (!$coText.Contains('start MGS V from Steam yourself') -and !$coText.Contains('co-op stays on for')) "[claimed $outcome] never asks for a manual start: $coText"
                if ($outcome -eq 'failed') {
                    Assert-True ($coText.Contains('MGSV could not be started again: mgs-coop\coop.ini changed. Do not start MGS V from Steam now; start Play-Coop.bat again.') -and
                        $coText.Contains('Your previous co-op save is kept at mgs-coop\coop-saves-replaced\20261015-120000-before-handoff (Share-Save.bat [R] brings it back).')) "[claimed failed] the reason and the kept copy: $coText"
                } else {
                    Assert-True ($coText.Contains('Co-op was switched off while MGSV was restarting (the helper could not start it again); do not start MGS V from Steam now. Start Play-Coop.bat again.')) "[claimed off] co-op was switched off: $coText"
                }
                Assert-True ((Get-PlayFilesPresent $hop) -eq 0 -and (Get-SessionJson $hop).state -eq 'off') "[claimed $outcome] co-op is off (the window's finally block switches a failed restart off)"
            } finally {
                Stop-StandIn $standIn1
                if ($coHelper -and !$coHelper.HasExited) { Stop-Process -Id $coHelper.Id -Force -ErrorAction SilentlyContinue }
                if (!$coLauncher.HasExited) { Stop-Process -Id $coLauncher.Id -Force -ErrorAction SilentlyContinue }
                [IO.File]::WriteAllBytes($hop.Exe, $hoRealExe)
            }
        }

        # The same with the window gone (-Watch): the helper applies, waits for the restarted game
        # and switches co-op off only after that one exits.
        $wdGame = New-GameFixture 'handoff-watch'
        $wdp = Get-InstancePaths -GameDirectory $wdGame
        $r = Invoke-Friend $install (@('-GameDirectory', $wdGame, '-Role', 'Join', '-RuntimeArchive', $archiveFx) + $common)
        Assert-Equal $r.Exit 0 "install for the handoff helper test: $($r.Raw)"
        $wdRealExe = [IO.File]::ReadAllBytes($wdp.Exe)
        $r = Invoke-Play $play @('-GameDirectory', $wdGame, '-Role', 'Join') @($joinCode) -NoWatchdog
        Assert-Equal $r.Exit 0 "activation for the handoff helper test (the window closes right after it): $($r.Raw)"
        $wdSessionId = [string](Get-SessionJson $wdp).session_id
        $standIn1 = $null; $standIn2 = $null; $wdHelper = $null
        try {
            $standIn1 = Start-StandIn $wdGame
            # Started like Start-CoopWatchdog's command (Start-Process keeps this test's environment).
            $env:MGS_COOP_TEST_NO_LAUNCH = '1'; $env:MGS_COOP_TEST_RELAUNCH_DELAY_SEC = '1'; $env:MGS_COOP_TEST_RESTART_WAIT_SEC = '120'
            $savedModulePath = $env:PSModulePath
            $env:PSModulePath = ''
            try {
                $wdHelper = Start-Process -FilePath $winPs -ArgumentList @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', ('"' + $play + '"'), '-GameDirectory', ('"' + $wdGame + '"'), '-Watch', $wdSessionId, '-TestNoLaunch') -WindowStyle Hidden -PassThru
            } finally {
                $env:PSModulePath = $savedModulePath
                foreach ($v in 'MGS_COOP_TEST_NO_LAUNCH', 'MGS_COOP_TEST_RELAUNCH_DELAY_SEC', 'MGS_COOP_TEST_RESTART_WAIT_SEC') { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
            }
            $watchdogs.Add($wdHelper.Id)
            $wdHelperStart = Get-CoopProcessStartUtc -Id $wdHelper.Id
            $null = Update-CoopPlaySession -GameRoot $wdGame -Update { param($t) $t['watchdog_pid'] = $wdHelper.Id; $t['watchdog_start_utc'] = $wdHelperStart }
            Assert-True (Wait-Until { [int](Get-FriendProperty (Get-SessionJson $wdp) 'game_pid' 0) -eq $standIn1.Id } 30) '[helper] the helper records the running game'
            $wdOwnBefore = Get-RelativeTreeHashes $wdp.CoopSaves
            $wdStage = New-HandoffStage $wdp.CoopSavesHandoff 'ADBECFD0' @{ '287700/local/TPP_GAME_DATA0' = 'host progress for the helper' }
            $null = New-HandoffRequest $wdp.CoopSavesHandoff $wdStage $standIn1.Id (Get-StandInStartUtc $standIn1)
            Stop-StandIn $standIn1
            Assert-True (Wait-Until { (Get-HandoffField $wdp 'state') -eq 'relaunching' -and (Get-HandoffField $wdp 'count') -eq '1' } 60) "[helper] the helper applies the host's save: $([IO.File]::ReadAllText($wdp.PlaySession))"
            Assert-Equal (Get-HandoffField $wdp 'claimed_by_pid') ([string]$wdHelper.Id) '[helper] the helper claimed it'
            Assert-True ([IO.File]::ReadAllText($wdp.LabIni).Contains("save_handoff=off`n") -and (Get-Sha256 (Join-Path $wdp.CoopSaves '287700\local\TPP_GAME_DATA0')) -eq $wdStage.Rows[0].sha256) "[helper] applied: lab.ini and the host's progress"
            $wdAside = @(Get-CoopProgressBackups -ReplacedRoot $wdp.CoopSavesReplaced | Where-Object { $_.Kind -eq 'handoff' })
            Assert-True ($wdAside.Count -eq 1 -and (Get-RelativeTreeHashes $wdAside[0].Path) -eq $wdOwnBefore) '[helper] the own copy is kept aside'
            Start-Sleep -Seconds 6
            Assert-True ((Get-PlayFilesPresent $wdp) -eq 4 -and !$wdHelper.HasExited) '[helper] while the restart is pending, co-op stays on and the helper waits'
            $standIn2 = Start-StandIn $wdGame
            Assert-True (Wait-Until { (Get-HandoffField $wdp 'state') -eq 'relaunched' -and [int](Get-FriendProperty (Get-SessionJson $wdp) 'game_pid' 0) -eq $standIn2.Id } 30) '[helper] the restarted game is recorded (relaunched)'
            Start-Sleep -Seconds 4
            Assert-True ((Get-PlayFilesPresent $wdp) -eq 4 -and !$wdHelper.HasExited) '[helper] co-op stays on while the restarted game runs'
            Stop-StandIn $standIn2
            Assert-True (Wait-Until { (Get-PlayFilesPresent $wdp) -eq 0 -and (Get-SessionJson $wdp).state -eq 'off' } 45) '[helper] co-op is switched off after the restarted game exits'
            Assert-True ($wdHelper.WaitForExit(15000)) '[helper] the helper exits after its cleanup'
        } finally {
            Stop-StandIn $standIn1; Stop-StandIn $standIn2
            if ($wdHelper -and !$wdHelper.HasExited) { Stop-Process -Id $wdHelper.Id -Force -ErrorAction SilentlyContinue }
            [IO.File]::WriteAllBytes($wdp.Exe, $wdRealExe)
        }
        Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'the save handoff launcher tests never touch the real saves'
    } else { Write-Warning 'lab_fake_process not supplied; the helper''s game-exit path and the launcher are not tested.' }

    # ------------------------------------------------------------ Collect-Logs
    $secretOnDisk = 'ZZZZZZZZZZZZZZZZZZZZ'
    [IO.File]::WriteAllText($hp.LinkSecret, "$secretOnDisk`r`n")
    $hostCode = New-CoopJoinCode -Secret $s7 -HostName '203.0.113.7'
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'script-observer.log'), "`r`nMGS Co-op script adapter 0.12.0; first run`r`nfirst_session_secret_line`r`n`r`nMGS Co-op script adapter 0.12.0; second run`r`nsecond_session_line`r`nMGS Co-op script adapter 0.12.0; third run`r`nthird_session_line`r`n`r`nMGS Co-op script adapter 0.12.0; executable_fingerprint=accepted`r`nmode=offline_v1; x`r`nplay=active; role=host; partner=1; refused=0x0`r`nlink=connected; session=1; peer=203.0.113.7:51234; rtt_ms=40; remote_build=0.12.0`r`npasted $hostCode and " + $hostCode.ToLowerInvariant() + "`r`nlobby=76561198012345678 path=C:\Users\Alice\AppData\x exe 1.0.15.4`r`n")
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'probe.log'), ((1..40000 | ForEach-Object { "probe line $_ steam=76561197960265735" }) -join "`r`n") + "`r`n")
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'early.log'), "early=start; version=0.12.0; lab=1; role=host`r`nsaves=redirected; dir=$hostSaveDir`r`n")
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'link-status.json'), '{"v":1,"pid":1,"state":"connected","peer":"203.0.113.7:51234","mission":30050}')
    # The automatic save handoff's records are collected; link-rejoin.txt (a join code) never is.
    $rejoinOnDisk = 'QQQQQRRRRRSSSSSTTTTT'
    [IO.File]::WriteAllText($hp.LinkRejoin, (New-CoopSteamJoinCode -Secret $rejoinOnDisk -SteamId '76561198058195100') + "`r`n")
    $null = New-Item -ItemType Directory -Path $hp.CoopSavesHandoff -Force
    [IO.File]::WriteAllText((Join-Path $hp.CoopSavesHandoff 'provenance.json'), '{"format":"mgs-coop-save-provenance","v":1,"entries":[{"set":"' + ('A' * 64) + '","mission":30010,"source":"continue","utc":"2026-10-15T12:00:00Z"}]}')
    [IO.File]::WriteAllText((Join-Path $hp.CoopSavesHandoff 'applied.json'), '{"format":"mgs-coop-save-handoff-applied","v":1,"set_sha256":"' + ('B' * 64) + '","applied_utc":"2026-10-15T12:01:00Z","aside":"20000101-000000-before-handoff"}')
    $collectStage = New-HandoffStage $hp.CoopSavesHandoff '6A7B8C9D' @{ '287700/local/TPP_GAME_DATA0' = 'staged host progress, never collected' }
    $null = New-HandoffRequest $hp.CoopSavesHandoff $collectStage 4242 '2026-10-15T11:59:00Z'
    $handoffAside = Join-Path $hp.CoopSavesReplaced '20000101-000000-before-handoff'
    $null = New-Item -ItemType Directory -Path $handoffAside -Force
    $outDir = Join-Path $fixture 'desktop'
    $null = New-Item -ItemType Directory -Path $outDir
    $r = Invoke-Friend $collect @('-GameDirectory', $hostGame, '-OutputDirectory', $outDir, '-NonInteractive', '-MaxLogBytes', '65536')
    Assert-Equal $r.Exit 0 "collect logs succeeds: $($r.Raw)"
    Assert-True (Has $r 'Co-op save folders are listed by name, size and time only; no save file is included.') 'collect logs: the closing text names the save-folder list'
    $zips = @(Get-ChildItem -LiteralPath $outDir -Filter '*.zip')
    Assert-True ($zips.Count -eq 1 -and $zips[0].Name -cmatch '^mgs-coop-logs-host-[0-9]{8}-[0-9]{6}\.zip$') 'zip named by the last play role and time'
    Assert-True ($zips[0].Length -lt 400KB) 'zip size bounded'
    $extract = Join-Path $fixture 'collected'
    Expand-Archive -LiteralPath $zips[0].FullName -DestinationPath $extract
    $collected = @(Get-ChildItem -LiteralPath $extract -Directory)[0].FullName
    foreach ($n in 'summary.txt', 'script-observer.log', 'probe.log', 'early.log', 'link-status.json', 'play-session.json', 'probe-install.json', 'friend-install.json', 'coop-saves.txt') {
        Assert-True (Test-Path -LiteralPath (Join-Path $collected $n) -PathType Leaf) "collected $n"
    }
    Assert-True (!(Test-Path -LiteralPath (Join-Path $collected 'link-secret.txt'))) 'link-secret.txt is never collected'
    foreach ($n in 'handoff-provenance.json', 'handoff-applied.json', 'handoff-request.json') { Assert-True (Test-Path -LiteralPath (Join-Path $collected $n) -PathType Leaf) "collected $n" }
    Assert-True (!@(Get-ChildItem -LiteralPath $collected -File | Where-Object { $_.Name -like '*rejoin*' -or $_.Name -like 'TPP_GAME_DATA*' }).Count) 'link-rejoin.txt and staged save files are never collected'
    $everything = (Get-ChildItem -LiteralPath $collected -File | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
    Assert-True (!$everything.Contains($secretOnDisk) -and !$everything.ToUpperInvariant().Contains('7K3QZ')) 'neither the link key nor a pasted code is in the logs ZIP'
    Assert-True (!$everything.Contains($rejoinOnDisk) -and !$everything.Contains('staged host progress')) 'neither the rejoin code nor staged save bytes are in the logs ZIP'
    Assert-True ($everything.Contains('MGS1-<redacted>') -and $everything.Contains('peer=203.0.x.x:51234') -and $everything.Contains('"peer":"203.0.x.x:51234"') -and $everything.Contains('1.0.15.4')) 'codes redacted, public peers masked, versions kept'
    Assert-True ($everything -notmatch '(?<![0-9])7656119[0-9]{10}(?![0-9])' -and !$everything.Contains('Alice')) 'no complete SteamID or user name'
    $observer = [IO.File]::ReadAllText((Join-Path $collected 'script-observer.log'))
    Assert-True ($observer.StartsWith('MGS Co-op script adapter 0.12.0; second run') -and $observer.Contains('third_session_line') -and $observer.Contains('executable_fingerprint=accepted') -and
        !$observer.Contains('first_session_secret_line')) 'script-observer.log: the last 3 sessions (from the 3rd-last adapter start)'
    # The co-op save folders: names, sizes and times, never contents (K19).
    $saveListing = [IO.File]::ReadAllText((Join-Path $collected 'coop-saves.txt'))
    Assert-True ($saveListing.StartsWith("# MGS Co-op save folders: names, sizes and times only (no save contents)`r`n# path`tbytes|dir`tlast_write_utc`r`n")) 'coop-saves.txt header'
    Assert-True ($saveListing -cmatch "(?m)^coop-saves/287700/local/COOP_PROGRESS`t[0-9]+`t[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z\r?$" -and
        $saveListing -cmatch "(?m)^coop-saves/287700/local`tdir`t") 'coop-saves.txt lists files with sizes and folders'
    Assert-True (!$saveListing.Contains('progress made in the co-op copy') -and !$saveListing.Contains('fixture save') -and !$saveListing.Contains($hostGame)) 'coop-saves.txt holds no save contents and no absolute path'
    Assert-True ($saveListing -cmatch "(?m)^coop-saves-handoff`tdir`t" -and $saveListing -cmatch "(?m)^coop-saves-handoff/6A7B8C9D/287700/local/TPP_GAME_DATA0`t[0-9]+`t" -and
        $saveListing -cmatch "(?m)^coop-saves-handoff/request.json`t[0-9]+`t" -and !$saveListing.Contains('staged host progress')) 'coop-saves.txt lists coop-saves-handoff (names and sizes only)'
    $summary = [IO.File]::ReadAllText((Join-Path $collected 'summary.txt'))
    Assert-True ($summary -cmatch 'coop_saves=files=[0-9]+; import_marker=absent; newest_replaced=') 'summary: the co-op save state'
    Assert-True ($summary.Contains('handoff=none; asides_handoff=1')) 'summary: the save handoff state and its asides'
    Assert-True ($summary.Contains('real_saves=UNCHANGED') -and $summary.Contains('mgsvtpp.exe sha256=' + $exeHash) -and $summary.Contains('matches_install_record=True') -and $summary.Contains('play_session=off; role=host') -and $summary.Contains('link_secret_present=True (never collected)')) 'summary: save check, hashes, play session, link secret not collected'
    Remove-Item -LiteralPath $hp.LinkSecret
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'collecting logs reads the saves only'
    $batOut = '' | & cmd.exe /c (Join-Path $stage 'Collect-Logs.bat') -GameDirectory $joinGame -OutputDirectory $outDir -NonInteractive 2>&1 | Out-String
    Assert-True ($LASTEXITCODE -eq 0 -and @(Get-ChildItem -LiteralPath $outDir -Filter 'mgs-coop-logs-join-*.zip').Count -eq 1) "Collect-Logs.bat works: $batOut"
    # The save listing itself (K19): the row cap with its truncation line; a link is listed, never followed.
    $listMod = [IO.Path]::GetFullPath((Join-Path $fixture 'listing\mgs-coop'))
    $listOutside = [IO.Path]::GetFullPath((Join-Path $fixture 'listing\outside'))
    $listLink = Join-Path $listMod 'coop-saves-replaced\link'
    foreach ($path in @($listMod, $listOutside, $listLink)) { Assert-InFixture $path }
    foreach ($name in 'F1', 'F2', 'F3', 'F4') {
        $path = Join-Path $listMod "coop-saves\287700\local\$name"
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
        [IO.File]::WriteAllText($path, "listing fixture $name")
    }
    $null = New-Item -ItemType Directory -Path (Join-Path $listMod 'coop-saves-replaced'), $listOutside -Force
    [IO.File]::WriteAllText((Join-Path $listOutside 'LINKTARGET'), 'outside the save folders')
    try {
        $null = New-Item -ItemType Junction -Path $listLink -Target $listOutside
        $fullListing = @(Get-FriendSaveListing -ModDirectory $listMod)
        Assert-Equal $fullListing.Count 11 'listing: 2 header lines and 9 rows (4 folders, 4 files, the link)'
        Assert-Equal @($fullListing | Where-Object { $_ -cmatch "^coop-saves-replaced/link`tdir`t" }).Count 1 'listing: the link is one dir row'
        Assert-True (!@($fullListing | Where-Object { $_.StartsWith('coop-saves-replaced/link/') }).Count -and !($fullListing -join "`n").Contains('LINKTARGET')) 'listing: the link is never followed'
        Assert-True (!($fullListing -join "`n").Contains('# truncated')) 'listing: no truncation line under the cap'
        $capped = @(Get-FriendSaveListing -ModDirectory $listMod -MaxEntries 3)
        Assert-Equal $capped.Count 6 'capped listing: 2 header lines, 3 rows and the truncation line'
        Assert-Equal (($capped[0..4]) -join "`n") (($fullListing[0..4]) -join "`n") 'capped listing: the first rows in order'
        Assert-Equal $capped[5] '# truncated: 6 more' 'capped listing: the truncation line counts the rest'
    } finally {
        # Remove only the owned junction before the no-links fixture cleanup; never recurse into its target.
        if (Test-Path -LiteralPath $listLink) {
            $link = Get-Item -LiteralPath $listLink -Force
            if (!($link.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $link.LinkType -cne 'Junction' -or
                $link.ResolveLinkTarget($true).FullName -cne $listOutside) { throw 'Listing junction cleanup ownership changed' }
            [IO.Directory]::Delete($listLink)
        }
    }
    Assert-True (Test-Path -LiteralPath (Join-Path $listOutside 'LINKTARGET') -PathType Leaf) 'listing: the link target is untouched'

    # ------------------------------------------------------------ Uninstall
    if ($FakeProcess) {
        # A stand-in process named mgsvtpp.exe running from the folder blocks the uninstall.
        $realExe = [IO.File]::ReadAllBytes($hp.Exe)
        Copy-Item -LiteralPath $FakeProcess -Destination $hp.Exe -Force
        $port = 0
        foreach ($i in 1..50) { $candidate = Get-Random -Minimum 49152 -Maximum 65000; if (Test-LocalPortFree -Port $candidate) { $port = $candidate; break } }
        $fakeGame = Start-Process -FilePath $hp.Exe -ArgumentList '-standalone', '-listenAddr', "127.0.0.1:$port" -WorkingDirectory $hostGame -PassThru
        Start-Sleep -Milliseconds 500
        Assert-True (-not $fakeGame.HasExited) 'stand-in game process runs'
        $r = Invoke-Friend $uninstall @('-GameDirectory', $hostGame, '-NonInteractive', '-Yes')
        Assert-Refused $r 'MGSV is running from this folder' 'uninstall refused while the game runs'
        Assert-True (Test-Path -LiteralPath $hp.Loader) 'nothing removed while the game runs'
        Stop-Process -Id $fakeGame.Id -Force
        $fakeGame.WaitForExit(5000) | Out-Null
        $fakeGame = $null
        Start-Sleep -Milliseconds 300
        [IO.File]::WriteAllBytes($hp.Exe, $realExe)
    } else { Write-Warning 'lab_fake_process not supplied; running-game refusal not tested.' }
    # Uninstall switches off an active play session first. (The Play-Coop preflight first
    # removes what an unapplied save handoff left: request, stage and link-rejoin.txt.)
    $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-Role', 'Host', '-HostAddress', '192.0.2.10') -NoWatchdog
    Assert-Equal $r.Exit 0 "activation before the uninstall: $($r.Raw)"
    Assert-True ((Has $r 'A host save from an earlier session was not applied and was removed.') -and !(Test-Path -LiteralPath $hp.LinkRejoin) -and
        !(Test-Path -LiteralPath (Join-Path $hp.CoopSavesHandoff 'request.json')) -and !(Test-Path -LiteralPath $collectStage.Stage) -and
        (Test-Path -LiteralPath (Join-Path $hp.CoopSavesHandoff 'provenance.json')) -and (Test-Path -LiteralPath $handoffAside)) "Play-Coop removes an unapplied save handoff first: $($r.Text)"
    [IO.File]::WriteAllText($hp.LinkRejoin, (New-CoopSteamJoinCode -Secret $rejoinOnDisk -SteamId '76561198058195100') + "`r`n")
    $uninstallStage = New-HandoffStage $hp.CoopSavesHandoff '7B8C9DAE.partial' @{ '287700/local/TPP_GAME_DATA0' = 'half a host save' }
    $r = Invoke-Friend $uninstall @('-GameDirectory', $hostGame, '-NonInteractive', '-Yes')
    Assert-Equal $r.Exit 0 "host uninstall succeeds: $($r.Raw)"
    Assert-True ((Has $r 'Step 0: switching off the last Play-Coop session') -and (Has $r 'restored 2 install state(s)') -and (Has $r 'UNCHANGED')) 'play session switched off, both installs undone, saves verified'
    Assert-True (!(Test-Path -LiteralPath $hp.LinkRejoin) -and !(Test-Path -LiteralPath $uninstallStage.Stage) -and (Test-Path -LiteralPath (Join-Path $hp.CoopSavesHandoff 'provenance.json')) -and
        (Test-Path -LiteralPath (Join-Path $hp.CoopSavesHandoff 'applied.json')) -and (Test-Path -LiteralPath $handoffAside) -and (Has $r 'link-rejoin.txt')) "uninstall removes link-rejoin.txt and staged handoff files, keeps provenance, applied and coop-saves-replaced: $($r.Text)"
    Remove-Item -LiteralPath $handoffAside
    Assert-True (-not (Test-Path -LiteralPath $hp.Loader) -and -not (Test-Path -LiteralPath $hp.Record)) 'loader and record removed (none before)'
    Assert-PassThrough $hp 'after the uninstall'
    Assert-Equal @(Get-ChildItem -LiteralPath $hp.Mod -Filter 'friend-install.json.uninstalled-*').Count 1 'install record retired'
    Assert-True ((Test-Path -LiteralPath (Join-Path $hostSaveDir '287700/local/COOP_PROGRESS')) -and (Test-Path -LiteralPath $state.latest_backup)) 'save copy and backups kept'
    Assert-True (-not (Test-Path -LiteralPath $hp.Archive)) 'runtime archive removed with the managed files'
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'real saves untouched by the uninstall'
    $r = Invoke-Friend $install (@('-GameDirectory', $hostGame, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-True ($r.Exit -eq 0 -and (Has $r 'co-op save copy from an earlier test exists')) "install after an uninstall: $($r.Text)"
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $hostSaveDir '287700/local/COOP_PROGRESS'))) 'install after an uninstall refreshes the save copy'
    $r = Invoke-Friend $uninstall @('-GameDirectory', $hostGame, '-NonInteractive', '-Yes')
    Assert-Equal $r.Exit 0 "second host uninstall: $($r.Raw)"
    $r = Invoke-Friend $uninstall @('-GameDirectory', $hostGame, '-NonInteractive', '-Yes')
    Assert-Refused $r 'No friend-test install record' 'uninstall without an install changes nothing'

    # Changed real saves are reported, never overwritten.
    $savePath = Join-Path $userdata '287700/local/TPP_GAME_DATA0'
    [IO.File]::WriteAllText($savePath, 'played normally in between')
    $r = Invoke-Friend $uninstall @('-GameDirectory', $joinGame, '-NonInteractive', '-Yes')
    Assert-Equal $r.Exit 4 "join uninstall reports changed saves (exit 4): $($r.Raw)"
    Assert-True ((Has $r 'DIFFERENT') -and (Has $r 'changed: 287700/local/TPP_GAME_DATA0') -and (Has $r 'Nothing was overwritten')) 'difference listed'
    Assert-True ((Has $r 'files\287700\local  ->') -and (Has $r 'files\311340\remote  ->') -and (Has $r 'leave remotecache.vdf')) 'restore instructions name the two verified folders, not remotecache.vdf'
    Assert-Equal ([IO.File]::ReadAllText($savePath)) 'played normally in between' 'real save not overwritten'
    [IO.File]::WriteAllText($savePath, 'fixture save 287700/local/TPP_GAME_DATA0')
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'fixture saves back to the start'

    # Real saves that change while the mod is installed (normal play in
    # pass-through): a reinstall reports it and proceeds, but never takes a new
    # reference; uninstall compares with the FIRST backup.
    $chgGame = New-GameFixture 'changed'
    $cp = Get-InstancePaths -GameDirectory $chgGame
    $r = Invoke-Friend $install (@('-GameDirectory', $chgGame, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Equal $r.Exit 0 "install for the changed-save test: $($r.Raw)"
    $chgStatePath = Join-Path $cp.Mod 'friend-install.json'
    $firstBackup = [string](Get-Content -LiteralPath $chgStatePath -Raw | ConvertFrom-Json).backups[0]
    [IO.File]::WriteAllText($savePath, 'changed while the mod was installed')
    $r = Invoke-Friend $install (@('-GameDirectory', $chgGame, '-Role', 'Host', '-Character', 'quiet') + $common)
    Assert-True ($r.Exit -eq 0 -and (Has $r 'Real save files changed since the backup made before the friend test (expected after normal play') -and
        !(Has $r 'WARNING: your REAL')) "reinstall after normal play reports the change and proceeds: $($r.Text)"
    Assert-True ((Has $r 'changed: 287700/local/TPP_GAME_DATA0') -and (Has $r $firstBackup)) 'reinstall lists the change and the first backup'
    $r = Invoke-Friend $install (@('-GameDirectory', $chgGame, '-Role', 'Host', '-Character', 'quiet', '-AcceptChangedSaves') + $common)
    Assert-Equal $r.Exit 0 "-AcceptChangedSaves is still accepted: $($r.Text)"
    $cs = Get-Content -LiteralPath $chgStatePath -Raw | ConvertFrom-Json
    Assert-Equal "$(@($cs.backups).Count)|$($cs.first_backup)" "3|$firstBackup" 'the first backup stays the reference'
    $r = Invoke-Friend $uninstall @('-GameDirectory', $chgGame, '-NonInteractive', '-Yes')
    Assert-True ($r.Exit -eq 4 -and (Has $r 'DIFFERENT from the backup made before the friend test') -and (Has $r ($firstBackup + '\files\287700\local'))) "uninstall compares with the first backup, not the latest: $($r.Text)"
    [IO.File]::WriteAllText($savePath, 'fixture save 287700/local/TPP_GAME_DATA0')
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'fixture saves back to the start (changed-save test)'

    # Upgrade from the online friend-test package: its files are archived as
    # *.online-old-<time> and never restored; the configuration from before the
    # first friend install (previous_files) keeps its restore semantics.
    $upGame = New-GameFixture 'upgrade'
    $up = Get-InstancePaths -GameDirectory $upGame
    $r = Invoke-Friend $install (@('-GameDirectory', $upGame, '-Role', 'Join', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Equal $r.Exit 0 "install for the upgrade test: $($r.Raw)"
    $onlineLab = "$friendHeader`n# Two-PC friend test, diagnostic mode`nenabled=1`nrole=guest`nloopback=0`nself_steamid=real`nsave_dir=$($up.Mod)\coop-saves`n"
    [IO.File]::WriteAllText($up.LabIni, $onlineLab)
    [IO.File]::WriteAllText($up.CoopIni, "$friendHeader`ncharacter=quiet`n")
    [IO.File]::WriteAllText($up.Marker, "client-v1`n")
    [IO.File]::WriteAllText($up.ClientHost, "76561198012345678`n")
    $r = Invoke-Friend $install (@('-GameDirectory', $upGame, '-Role', 'Join') + $common)
    Assert-True ($r.Exit -eq 0 -and (Has $r 'kept, never restored')) "upgrade over the online files: $($r.Text)"
    Assert-PassThrough $up 'upgrade'
    foreach ($n in 'lab.ini', 'coop.ini', 'script-observe.enabled', 'client-host.txt') {
        Assert-Equal @(Get-ChildItem -LiteralPath $up.Mod -Filter "$n.online-old-*").Count 1 "$n archived as *.online-old-*"
    }
    $us = Get-Content -LiteralPath (Join-Path $up.Mod 'friend-install.json') -Raw | ConvertFrom-Json
    Assert-Equal (@($us.online_archived | ForEach-Object { $_.name }) -join ',') 'lab.ini,coop.ini,script-observe.enabled,client-host.txt' 'online_archived recorded'
    $r = Invoke-Friend $uninstall @('-GameDirectory', $upGame, '-NonInteractive', '-Yes')
    Assert-True ($r.Exit -eq 0 -and (Has $r 'they are not restored')) "uninstall after the upgrade: $($r.Text)"
    Assert-PassThrough $up 'uninstall after the upgrade (no online file restored)'
    Assert-Equal @(Get-ChildItem -LiteralPath $up.Mod -Filter '*.online-old-*').Count 4 'the archived online files are kept'

    # An earlier MGS Co-op install (owned loader, own role and lab.ini) is put back.
    $prevGame = New-GameFixture 'previous'
    $pp = Get-InstancePaths -GameDirectory $prevGame
    $stagePrev = Join-Path $fixture 'stage-prev'
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $stagePrev -LoaderPath $loaderPrev -Version '0.12.0' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 (Get-Sha256 $archiveFx)
    $null = & (Join-Path $projectRoot 'tools/package-install.ps1') -Action Install -GameDirectory $prevGame -PackageDirectory $stagePrev -RuntimeArchive $archiveFx -NoPrompt
    [IO.File]::WriteAllText($pp.Marker, "host-v3`n")
    $earlierLab = "# my own lab.ini`nenabled=1`nrole=host`nsave_dir=C:\lab\saves`n"
    $earlierCoop = "master_url=http://127.0.0.1:6667/tppstm/gate`ncharacter=snake`n"
    [IO.File]::WriteAllText($pp.LabIni, $earlierLab)
    [IO.File]::WriteAllText($pp.CoopIni, $earlierCoop)
    $r = Invoke-Friend $install (@('-GameDirectory', $prevGame, '-Role', 'Join', '-Character', 'dd_female') + $common)
    Assert-Equal $r.Exit 0 "install over an earlier owned loader: $($r.Raw)"
    Assert-PassThrough $pp 'install over an earlier loader'
    Assert-Equal @(Get-ChildItem -LiteralPath $pp.Mod -Filter 'coop.ini.before-friend-*').Count 1 'earlier coop.ini copied aside'
    Assert-Equal @(Get-ChildItem -LiteralPath $pp.Mod -Filter 'lab.ini.disabled-*').Count 1 'earlier lab.ini set aside (pass-through)'
    $r = Invoke-Friend $uninstall @('-GameDirectory', $prevGame, '-NonInteractive', '-Yes')
    Assert-Equal $r.Exit 0 "uninstall back to the earlier loader: $($r.Raw)"
    Assert-True ((Has $r 'from BEFORE the friend test is back in place (restored 1 friend install state(s))')) 'earlier loader reported as such'
    Assert-Equal (Get-Sha256 $pp.Loader) (Get-Sha256 $loaderPrev) 'earlier loader restored'
    Assert-Equal ([IO.File]::ReadAllText($pp.Marker)) "host-v3`n" 'earlier role marker restored (previous_files)'
    Assert-Equal ([IO.File]::ReadAllText($pp.LabIni)) $earlierLab 'earlier lab.ini restored with the earlier loader'
    Assert-Equal ([IO.File]::ReadAllText($pp.CoopIni)) $earlierCoop 'earlier coop.ini restored with the earlier loader'
    Assert-True ((Has $r 'stays ACTIVE') -and (Has $r 'redirects the saves to: C:\lab\saves')) 'remaining loader named active, with its save redirection'

    # A loader installed ON TOP of the friend install (developer package) is not
    # called the earlier one; the output says it stays active on the real saves.
    $topGame = New-GameFixture 'on-top'
    $op = Get-InstancePaths -GameDirectory $topGame
    $r = Invoke-Friend $install (@('-GameDirectory', $topGame, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $common)
    Assert-Equal $r.Exit 0 "friend install before a developer install: $($r.Raw)"
    $null = & (Join-Path $projectRoot 'tools/package-install.ps1') -Action Install -GameDirectory $topGame -PackageDirectory $stagePrev -NoPrompt
    $r = Invoke-Friend $uninstall @('-GameDirectory', $topGame, '-NonInteractive', '-Yes')
    Assert-Equal $r.Exit 0 "uninstall with a developer loader on top: $($r.Raw)"
    Assert-True ((Has $r 'that the friend test did NOT install') -and (Has $r 'restored 0 friend install state(s)')) 'loader on top named as not installed by the friend test'
    Assert-True ((Has $r 'stays ACTIVE') -and (Has $r 'uses your REAL save files')) 'remaining loader without lab.ini: real saves stated'
    Assert-Equal (Get-Sha256 $op.Loader) (Get-Sha256 $loaderPrev) 'developer loader left unchanged'
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'real saves untouched overall'

    # Non-ASCII game, Steam and package folders under Windows PowerShell 5.1
    # (all JSON records are read as UTF-8, lab.ini is UTF-8), uninstall through the .bat file.
    $e = [string][char]0x00E9
    $u = [string][char]0x00FC
    $uniSteam = Join-Path $fixture ('steam-M' + $u + 'ller')
    $null = New-Item -ItemType Directory -Path $uniSteam
    Copy-Item -LiteralPath (Join-Path $steam 'userdata') -Destination (Join-Path $uniSteam 'userdata') -Recurse
    $uniUserdata = Join-Path $uniSteam 'userdata/7'
    $uniBefore = Get-TreeHashes $uniUserdata
    $uniStage = Join-Path $fixture ('stage-Pak' + $e + 't')
    Copy-Item -LiteralPath $stage -Destination $uniStage -Recurse
    $uniInstall = Join-Path $uniStage 'scripts/Install-Coop.ps1'
    $uniGame = New-GameFixture ('Jeux Vid' + $e + 'o/MGS_TPP')
    $unp = Get-InstancePaths -GameDirectory $uniGame
    $uniCommon = @('-SteamPath', $uniSteam, '-NonInteractive', '-Yes', '-SkipNetworkReport')
    $r = Invoke-Friend $uniInstall (@('-GameDirectory', $uniGame, '-Role', 'Host', '-RuntimeArchive', $archiveFx) + $uniCommon)
    Assert-Equal $r.Exit 0 "host install in a non-ASCII folder (Windows PowerShell 5.1): $($r.Raw)"
    Assert-Equal ([IO.File]::ReadAllText($unp.Record, [Text.Encoding]::UTF8) | ConvertFrom-Json).game_directory $uniGame 'non-ASCII: ownership record names the folder'
    $uns = [IO.File]::ReadAllText((Join-Path $unp.Mod 'friend-install.json'), [Text.Encoding]::UTF8) | ConvertFrom-Json
    Assert-True ($uns.game_directory -ceq $uniGame -and $uns.status -eq 'installed') 'non-ASCII: install record'
    $r = Invoke-Play (Join-Path $uniStage 'scripts/Play-Coop.ps1') @('-GameDirectory', $uniGame, '-Role', 'Host', '-HostAddress', '192.0.2.10') -NoWatchdog
    Assert-Equal $r.Exit 0 "non-ASCII: Play-Coop host: $($r.Raw)"
    $uniLabBytes = [IO.File]::ReadAllBytes($unp.LabIni)
    Assert-Equal ([Text.Encoding]::UTF8.GetString($uniLabBytes)) (New-CoopPlayLabIniText -Role host -SaveDir (Join-Path $unp.Mod 'coop-saves') -Port 47830 -SaveHandoff scene) 'non-ASCII: play lab.ini save_dir in UTF-8'
    Assert-True ($uniLabBytes[0] -ne 0xEF -and @($uniLabBytes | Where-Object { $_ -eq 0x3F }).Count -eq 0) 'non-ASCII: no BOM and no ? in the play lab.ini'
    if ($linkKeys) {
        # The native helper opens its argument with the ANSI API: check a copy
        # with an ASCII file name (the content keeps the UTF-8 save_dir).
        $uniLabCopy = Join-Path $fixture 'uni-lab.ini'
        Copy-Item -LiteralPath $unp.LabIni -Destination $uniLabCopy
        $n = Invoke-IniCheck lab $uniLabCopy
        # Play-Coop writes save_handoff=scene, a key the loader parses.
        Assert-Equal "$($n['present'])|$($n['mode'])|$($n['play_refused'])|$($n['unknown_keys'])/$($n['invalid_values'])|$($n['save_handoff'])" '1|play|0x0|0/0|scene' 'native: play lab.ini with a UTF-8 save_dir accepted'
        Assert-Equal $n['save_dir'] (Join-Path $unp.Mod 'coop-saves') 'native: UTF-8 save_dir kept'
    }
    $r = Invoke-Friend (Join-Path $uniStage 'scripts/Play-Coop.ps1') @('-GameDirectory', $uniGame, '-Cleanup', '-NonInteractive')
    Assert-True ($r.Exit -eq 0 -and (Get-PlayFilesPresent $unp) -eq 0) "non-ASCII: cleanup: $($r.Text)"
    $uniOut = Join-Path $fixture ('desktop-' + $e)
    $null = New-Item -ItemType Directory -Path $uniOut
    $r = Invoke-Friend (Join-Path $uniStage 'scripts/Collect-Logs.ps1') @('-GameDirectory', $uniGame, '-OutputDirectory', $uniOut, '-NonInteractive')
    Assert-Equal $r.Exit 0 "non-ASCII: collect logs: $($r.Raw)"
    $uniZip = @(Get-ChildItem -LiteralPath $uniOut -Filter 'mgs-coop-logs-host-*.zip')
    Assert-Equal $uniZip.Count 1 'non-ASCII: log zip written'
    $uniExtract = Join-Path $fixture 'collected-uni'
    Expand-Archive -LiteralPath $uniZip[0].FullName -DestinationPath $uniExtract
    $uniSummary = [IO.File]::ReadAllText((Get-ChildItem -LiteralPath $uniExtract -Recurse -Filter summary.txt)[0].FullName)
    Assert-True ($uniSummary.Contains('real_saves=UNCHANGED') -and $uniSummary.Contains('matches_install_record=True') -and $uniSummary.Contains('status=installed')) 'non-ASCII: summary save check and loader match'
    $batOut = '' | & cmd.exe /c (Join-Path $uniStage 'Uninstall-Coop.bat') -GameDirectory $uniGame -NonInteractive -Yes 2>&1 | Out-String
    Assert-True ($LASTEXITCODE -eq 0 -and -not (Test-Path -LiteralPath $unp.Loader)) "non-ASCII: Uninstall-Coop.bat removes the install: $batOut"
    Assert-Equal @(Get-ChildItem -LiteralPath $unp.Mod -Filter 'friend-install.json.uninstalled-*').Count 1 'non-ASCII: install record retired'
    Assert-Equal (Get-TreeHashes $uniUserdata) $uniBefore 'non-ASCII: real saves untouched'
    Write-Output "friend package tests passed: $script:checks checks (scripts run under $winPs and $pwshPath; fixture only, no game, Steam or network)"
} finally {
    if ($fakeGame -and !$fakeGame.HasExited) { Stop-Process -Id $fakeGame.Id -Force -ErrorAction SilentlyContinue; $fakeGame.WaitForExit(5000) | Out-Null }
    foreach ($w in $watchdogs) {
        # A helper of this test that is still waiting (a failed assertion) is stopped.
        $wp = Get-Process -Id $w -ErrorAction SilentlyContinue
        if ($wp -and $wp.ProcessName -eq 'powershell') { Stop-Process -Id $w -Force -ErrorAction SilentlyContinue }
    }
    Start-Sleep -Milliseconds 300
    Remove-OwnedFixture
}
