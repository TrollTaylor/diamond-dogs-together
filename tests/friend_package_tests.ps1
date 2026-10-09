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
    # Installer paths: the longest fixed one is the runtime archive temp file
    # (<game> + 83 characters); a save file name can make the backup longer.
    Assert-True ($null -eq (Get-FriendPathLengthProblem -GameRoot ('C:\' + ('a' * 173)))) 'game path of 176 characters: installer paths fit 259'
    Assert-True ((Get-FriendPathLengthProblem -GameRoot ('C:\' + ('a' * 174))).Contains('too long')) 'game path of 177 characters refused'
    Assert-True ((Get-FriendPathLengthProblem -GameRoot ('C:\' + ('a' * 173)) -SaveFiles @('287700/local/TPP_GRAPHICS_CONFIG')).Contains('too long')) 'a long save file name makes the backup path too long'
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
            $file = Join-Path $fixture 'play-clips-lab.ini'; [IO.File]::WriteAllText($file, $clipsIni, $utf8)
            $n = Invoke-IniCheck lab $file
            Assert-Equal "$($n['partner_native_profile'])|$($n['partner_drive'])|$($n['partner_input_send'])|$($n['play_refused'])|$($n['invalid_values'])" 'off|legacy|1|0x0|0' 'native: play -Partner clips keeps the clip partner'
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
    Assert-True ((Format-CoopStatusLine -Status (ConvertFrom-CoopStatusText -Text '{"v":1,"state":"connecting"}') -Role guest) -ceq '[JOIN] link=connecting | partner=none | mission 0 - Partner body unavailable in this mission; use Mother Base or free roam.') 'a sparse status still formats (StrictMode-safe)'
    foreach ($freeMission in @(30010, 30020)) {
        $elsewhere.mission = $freeMission
        Assert-True ((Format-CoopStatusLine -Status $elsewhere -Role host -NowUtc $now).EndsWith("mission $freeMission")) 'free roam does not send the player back to Mother Base'
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
    foreach ($name in @('MGS_LOCAL_GUN_PRELOAD_TEST','MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST',
            'MGS_LOCAL_STOCK46_AMMO_TEST','MGS_LOCAL_ACTION_EVENTS','MGS_LOCAL_UPPER_END_OBSERVE','MGS_LOCAL_UPPER_PHASE_OBSERVE',
            'MGS_LOCAL_UPPER_MANUAL_IDLE','MGS_LOCAL_UPPER_PROBE','MGS_LOCAL_UPPER_CHANNEL2','MGS_LOCAL_UPPER_FIRE_ZERO_BLEND')) {
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
    Assert-True ($packages[0].Name -cmatch '^mgs-coop-friend-0\.12\.1-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{8}$') 'package folder name'
    Assert-True (Test-Path -LiteralPath ($pkg + '.zip') -PathType Leaf) 'package zip written'
    $required = @('Install-Coop.bat', 'Play-Coop.bat', 'Uninstall-Coop.bat', 'Collect-Logs.bat', 'Share-Save.bat', 'scripts/Share-Save.ps1', 'FRIEND-GUIDE.txt', 'RUNTIME-ARCHIVE.txt', 'package.json', 'source-files.json',
        'payload/dinput8.dll', 'payload/DYNAMITE-LICENSE', 'payload/MinHook-LICENSE.txt', 'payload/ImGui-LICENSE.txt', 'payload/tpp-mod-LICENSE', 'payload/THIRD_PARTY_NOTICES.md',
        'scripts/Install-Coop.ps1', 'scripts/Play-Coop.ps1', 'scripts/Uninstall-Coop.ps1', 'scripts/Collect-Logs.ps1', 'scripts/FriendCommon.psm1', 'scripts/CoopPlay.psm1',
        'scripts/install.ps1', 'source/tools/friend/Play-Coop.ps1', 'source/tools/CoopPlay.psm1', 'source/lua/coop/session_feedback.lua',
        'source/cmake/lua_checks.cmake', 'source/native/debug_overlay.cpp', 'source/lua/coop/debug_menu.lua', 'source/third_party/imgui/imgui.cpp')
    foreach ($r in $required) { Assert-True (Test-Path -LiteralPath (Join-Path $pkg $r) -PathType Leaf) "package contains $r" }
    $wantScripts = @('Install-Coop.ps1', 'Play-Coop.ps1', 'Uninstall-Coop.ps1', 'Collect-Logs.ps1', 'Share-Save.ps1', 'FriendCommon.psm1', 'CoopPlay.psm1', 'install.ps1') | Sort-Object
    Assert-Equal ((@(Get-ChildItem -LiteralPath (Join-Path $pkg 'scripts') -File | ForEach-Object { $_.Name } | Sort-Object)) -join ',') ($wantScripts -join ',') 'scripts: exactly the play package pieces (no LabCommon.psm1, no set-session-mode.ps1)'
    Assert-Equal ((@(Get-ChildItem -LiteralPath $pkg -File -Filter '*.bat' | ForEach-Object { $_.Name } | Sort-Object)) -join ',') 'Collect-Logs.bat,Install-Coop.bat,Play-Coop.bat,Share-Save.bat,Uninstall-Coop.bat' 'the five double-click entry points'
    $manifest = Get-Content -LiteralPath (Join-Path $pkg 'package.json') -Raw | ConvertFrom-Json
    Assert-Equal "$($manifest.product)|$($manifest.version)|$($manifest.flavor)" 'mgs-coop-local-package|0.12.1|friend-play' 'package manifest identity'
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
    $guide = [IO.File]::ReadAllText((Join-Path $pkg 'FRIEND-GUIDE.txt'))
    foreach ($needle in 'Play-Coop.bat', 'UDP 47830', 'Tailscale', 'Mother Base', 'Install-Coop.bat', 'Uninstall-Coop.bat', 'Collect-Logs.bat', 'RUNTIME-ARCHIVE.txt', 'Version 0.12.1', 'Failed to log in', 'Public networks', 'Block rules win over Allow rules', 'REAL saves') {
        Assert-True $guide.Contains($needle) "guide mentions $needle"
    }
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
    foreach ($needle in 'Host online', 'Join online', 'Invite a friend', 'start Play-Coop.bat FIRST', 'Play-Coop.bat -Partner clips', 'native partner') {
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
    foreach ($b in 'Install-Coop.bat', 'Play-Coop.bat', 'Uninstall-Coop.bat', 'Collect-Logs.bat') { Copy-Item -LiteralPath (Join-Path $pkg $b) -Destination $stage }
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
    # Host activation (the window "closes" right after it; the watchdog cleans up).
    $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-Role', 'Host', '-HostAddress', '192.0.2.10', '-Port', '47835')
    Assert-True ($r.Exit -eq 0 -and (Has $r 'TEST: leaving after the activation.')) "Play-Coop host activates: $($r.Raw)"
    $code = Get-CodeFromOutput $r.Raw
    $parsedCode = ConvertFrom-CoopJoinCode $code
    Assert-Equal "$($parsedCode.Status)|$($parsedCode.HostName)|$($parsedCode.Port)" 'ok|192.0.2.10|47835' 'the host shows a valid code for its address and port'
    Assert-Equal ([regex]::Matches($r.Raw, [regex]::Escape($code)).Count) 1 'the code (the secret) is shown once, to the host'
    Assert-True (!$r.Raw.Contains($parsedCode.Secret)) 'the bare secret is never printed'
    Assert-True ((Has $r 'Mother Base') -and (Has $r 'Tailscale')) 'host guidance: Mother Base only, VPN recommended'
    Assert-Equal ([IO.File]::ReadAllText($hp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role host -SaveDir $hostSaveDir -Port 47835) 'host play lab.ini'
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
    Assert-Equal ([IO.File]::ReadAllText($jp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role guest -SaveDir $jSaveDir -Port 47831 -PeerAddress '192.0.2.44') 'guest play lab.ini (listen 0, peer address and port from the code)'
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
        Assert-Equal ([IO.File]::ReadAllText($hp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role host -SaveDir $hostSaveDir -Transport steam -SteamSelf $hostSteam) 'online host lab.ini'
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
        Assert-Equal ([IO.File]::ReadAllText($jp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role guest -SaveDir $jSaveDirOnline -Transport steam -SteamSelf $guestSteam -SteamPeer $hostSteam) 'online guest lab.ini'
        Assert-Equal ([IO.File]::ReadAllText($jp.LinkSecret)) "$($steamParsed.Secret)`r`n" 'online guest secret from the code'
        $r = Invoke-Friend $play @('-GameDirectory', $joinGame, '-Cleanup', '-NonInteractive')
        Assert-PassThrough $jp 'online join cleanup'
        # Wait for an invite (Enter): no secret is written.
        $r = Invoke-Play $play @('-GameDirectory', $joinGame, '-Role', 'Join', '-Transport', 'steam') @('') -NoWatchdog
        Assert-True ($r.Exit -eq 0 -and (Has $r 'Waiting for an invite')) "online join waits for an invite: $($r.Raw)"
        Assert-Equal ([IO.File]::ReadAllText($jp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role guest -SaveDir $jSaveDirOnline -Transport steam -SteamSelf $guestSteam -SteamWait) 'waiting guest lab.ini'
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
        Assert-Equal ([IO.File]::ReadAllText($jp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role guest -SaveDir $jSaveDirOnline -Port 47831 -PeerAddress '192.0.2.44') 'MGS1 in Join online: the UDP lab.ini'
        $r = Invoke-Friend $play @('-GameDirectory', $joinGame, '-Cleanup', '-NonInteractive')
        # The menu: O hosts online; L then H is the LAN/VPN host; Q changes nothing.
        $env:MGS_COOP_TEST_STEAM_ACTIVE_USER = '97000001'
        $r = Invoke-Play $play @('-GameDirectory', $hostGame) @('O') -NoWatchdog
        Assert-True ($r.Exit -eq 0 -and (Has $r '[O] Host online (Steam, recommended)') -and [regex]::IsMatch($r.Raw, 'MGS2-[0-9A-Z]{5}-')) "menu O: $($r.Raw)"
        $r = Invoke-Friend $play @('-GameDirectory', $hostGame, '-Cleanup', '-NonInteractive')
        $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-HostAddress', '192.0.2.10') @('L', 'H') -NoWatchdog
        Assert-True ($r.Exit -eq 0 -and (Has $r 'Host or Join?') -and [regex]::IsMatch($r.Raw, 'MGS1-[0-9A-Z]{5}-')) "menu L, H: the LAN/VPN host: $($r.Raw)"
        Assert-Equal ([IO.File]::ReadAllText($hp.LabIni, [Text.Encoding]::UTF8)) (New-CoopPlayLabIniText -Role host -SaveDir $hostSaveDir -Port 47830) 'menu L, H: UDP lab.ini unchanged'
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
    } else { Write-Warning 'lab_fake_process not supplied; the helper''s game-exit path and the launcher are not tested.' }

    # ------------------------------------------------------------ Collect-Logs
    $secretOnDisk = 'ZZZZZZZZZZZZZZZZZZZZ'
    [IO.File]::WriteAllText($hp.LinkSecret, "$secretOnDisk`r`n")
    $hostCode = New-CoopJoinCode -Secret $s7 -HostName '203.0.113.7'
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'script-observer.log'), "`r`nMGS Co-op script adapter 0.12.0; first run`r`nfirst_session_secret_line`r`n`r`nMGS Co-op script adapter 0.12.0; executable_fingerprint=accepted`r`nmode=offline_v1; x`r`nplay=active; role=host; partner=1; refused=0x0`r`nlink=connected; session=1; peer=203.0.113.7:51234; rtt_ms=40; remote_build=0.12.0`r`npasted $hostCode and " + $hostCode.ToLowerInvariant() + "`r`nlobby=76561198012345678 path=C:\Users\Alice\AppData\x exe 1.0.15.4`r`n")
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'probe.log'), ((1..40000 | ForEach-Object { "probe line $_ steam=76561197960265735" }) -join "`r`n") + "`r`n")
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'early.log'), "early=start; version=0.12.0; lab=1; role=host`r`nsaves=redirected; dir=$hostSaveDir`r`n")
    [IO.File]::WriteAllText((Join-Path $hp.Mod 'link-status.json'), '{"v":1,"pid":1,"state":"connected","peer":"203.0.113.7:51234","mission":30050}')
    $outDir = Join-Path $fixture 'desktop'
    $null = New-Item -ItemType Directory -Path $outDir
    $r = Invoke-Friend $collect @('-GameDirectory', $hostGame, '-OutputDirectory', $outDir, '-NonInteractive', '-MaxLogBytes', '65536')
    Assert-Equal $r.Exit 0 "collect logs succeeds: $($r.Raw)"
    $zips = @(Get-ChildItem -LiteralPath $outDir -Filter '*.zip')
    Assert-True ($zips.Count -eq 1 -and $zips[0].Name -cmatch '^mgs-coop-logs-host-[0-9]{8}-[0-9]{6}\.zip$') 'zip named by the last play role and time'
    Assert-True ($zips[0].Length -lt 400KB) 'zip size bounded'
    $extract = Join-Path $fixture 'collected'
    Expand-Archive -LiteralPath $zips[0].FullName -DestinationPath $extract
    $collected = @(Get-ChildItem -LiteralPath $extract -Directory)[0].FullName
    foreach ($n in 'summary.txt', 'script-observer.log', 'probe.log', 'early.log', 'link-status.json', 'play-session.json', 'probe-install.json', 'friend-install.json') {
        Assert-True (Test-Path -LiteralPath (Join-Path $collected $n) -PathType Leaf) "collected $n"
    }
    Assert-True (!(Test-Path -LiteralPath (Join-Path $collected 'link-secret.txt'))) 'link-secret.txt is never collected'
    $everything = (Get-ChildItem -LiteralPath $collected -File | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
    Assert-True (!$everything.Contains($secretOnDisk) -and !$everything.ToUpperInvariant().Contains('7K3QZ')) 'neither the link key nor a pasted code is in the logs ZIP'
    Assert-True ($everything.Contains('MGS1-<redacted>') -and $everything.Contains('peer=203.0.x.x:51234') -and $everything.Contains('"peer":"203.0.x.x:51234"') -and $everything.Contains('1.0.15.4')) 'codes redacted, public peers masked, versions kept'
    Assert-True ($everything -notmatch '(?<![0-9])7656119[0-9]{10}(?![0-9])' -and !$everything.Contains('Alice')) 'no complete SteamID or user name'
    $observer = [IO.File]::ReadAllText((Join-Path $collected 'script-observer.log'))
    Assert-True ($observer.StartsWith('MGS Co-op script adapter 0.12.0; executable_fingerprint') -and !$observer.Contains('first_session_secret_line')) 'script-observer.log from the last adapter start'
    $summary = [IO.File]::ReadAllText((Join-Path $collected 'summary.txt'))
    Assert-True ($summary.Contains('real_saves=UNCHANGED') -and $summary.Contains('mgsvtpp.exe sha256=' + $exeHash) -and $summary.Contains('matches_install_record=True') -and $summary.Contains('play_session=off; role=host') -and $summary.Contains('link_secret_present=True (never collected)')) 'summary: save check, hashes, play session, link secret not collected'
    Remove-Item -LiteralPath $hp.LinkSecret
    Assert-Equal (Get-TreeHashes $userdata) $userdataBefore 'collecting logs reads the saves only'
    $batOut = '' | & cmd.exe /c (Join-Path $stage 'Collect-Logs.bat') -GameDirectory $joinGame -OutputDirectory $outDir -NonInteractive 2>&1 | Out-String
    Assert-True ($LASTEXITCODE -eq 0 -and @(Get-ChildItem -LiteralPath $outDir -Filter 'mgs-coop-logs-join-*.zip').Count -eq 1) "Collect-Logs.bat works: $batOut"

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
    # Uninstall switches off an active play session first.
    $r = Invoke-Play $play @('-GameDirectory', $hostGame, '-Role', 'Host', '-HostAddress', '192.0.2.10') -NoWatchdog
    Assert-Equal $r.Exit 0 "activation before the uninstall: $($r.Raw)"
    $r = Invoke-Friend $uninstall @('-GameDirectory', $hostGame, '-NonInteractive', '-Yes')
    Assert-Equal $r.Exit 0 "host uninstall succeeds: $($r.Raw)"
    Assert-True ((Has $r 'Step 0: switching off the last Play-Coop session') -and (Has $r 'restored 2 install state(s)') -and (Has $r 'UNCHANGED')) 'play session switched off, both installs undone, saves verified'
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
    Assert-Equal ([Text.Encoding]::UTF8.GetString($uniLabBytes)) (New-CoopPlayLabIniText -Role host -SaveDir (Join-Path $unp.Mod 'coop-saves') -Port 47830) 'non-ASCII: play lab.ini save_dir in UTF-8'
    Assert-True ($uniLabBytes[0] -ne 0xEF -and @($uniLabBytes | Where-Object { $_ -eq 0x3F }).Count -eq 0) 'non-ASCII: no BOM and no ? in the play lab.ini'
    if ($linkKeys) {
        # The native helper opens its argument with the ANSI API: check a copy
        # with an ASCII file name (the content keeps the UTF-8 save_dir).
        $uniLabCopy = Join-Path $fixture 'uni-lab.ini'
        Copy-Item -LiteralPath $unp.LabIni -Destination $uniLabCopy
        $n = Invoke-IniCheck lab $uniLabCopy
        Assert-Equal "$($n['present'])|$($n['mode'])|$($n['play_refused'])|$($n['unknown_keys'])/$($n['invalid_values'])" '1|play|0x0|0/0' 'native: play lab.ini with a UTF-8 save_dir accepted'
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
