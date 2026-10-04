#Requires -Version 7
# Pester-free tests for tools/lab (pure helpers) and the -GameDirectory /
# -PackageDirectory installer path. Everything runs inside an owned temporary
# folder under build/; no real game folder, Steam folder or process is touched.
[CmdletBinding()]
param([string]$IniCheck, [string]$FakeProcess)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectRoot 'tools/lab/LabCommon.psm1') -Force
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
$nonce = [Guid]::NewGuid().ToString('N')
$fixture = Join-Path $buildRoot ('lab-scripts-tests-' + $nonce)
$ownerMarker = Join-Path $fixture '.fixture-owner'
$script:checks = 0

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
function Assert-InFixture([string]$Path) {
    if (!(Test-PathInside -Child $Path -Parent $fixture)) { throw "Test path escaped its fixture: $Path" }
}
function Invoke-IniCheck([string]$Mode, [string]$Value) {
    $output = & $IniCheck $Mode $Value
    if ($LASTEXITCODE -ne 0) { throw "lab_ini_check $Mode failed ($LASTEXITCODE)" }
    $map = @{}
    foreach ($line in $output) { $i = $line.IndexOf('='); if ($i -gt 0) { $map[$line.Substring(0, $i)] = $line.Substring($i + 1) } }
    return $map
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

try {
    # ------------------------------------------------------------ identity
    Assert-Equal (ConvertTo-SteamId64 -AccountId 1) '76561197960265729' 'account 1'
    Assert-Equal (ConvertTo-SteamId64 -AccountId 4294967280) '76561202255233008' 'lab guest account 0xFFFFFFF0'
    Assert-Equal (ConvertTo-SteamId64 -AccountId 97930372) '76561198058196100' 'ActiveUser arithmetic'
    Assert-Throws { ConvertTo-SteamId64 -AccountId 0 } 'Invalid Steam account id' 'account 0'
    Assert-Throws { ConvertTo-SteamId64 -AccountId 4294967296 } 'Invalid Steam account id' 'account > 32 bits'
    Assert-True (Test-IndividualSteamId64 '76561202255233008') 'guest id is individual'
    Assert-True (-not (Test-IndividualSteamId64 '76561197960265728')) 'account 0 is not individual'
    Assert-True (-not (Test-IndividualSteamId64 '7656119796026572')) '16 digits refused'
    Assert-True (-not (Test-IndividualSteamId64 '103582791429521408')) 'clan id refused'
    Assert-True (-not (Test-IndividualSteamId64 '')) 'empty refused'
    Assert-True (-not (Test-IndividualSteamId64 '7656119796026572x')) 'non-digit refused'
    $now = [DateTime]::UtcNow
    Assert-Equal (Resolve-SteamAccount -ActiveUser 42 -Candidates @([pscustomobject]@{ Account = 7; LastWriteUtc = $now })) ([UInt32]42) 'ActiveUser wins'
    Assert-Equal (Resolve-SteamAccount -ActiveUser 0 -Candidates @([pscustomobject]@{ Account = 7; LastWriteUtc = $now })) ([UInt32]7) 'single userdata account'
    Assert-Throws { Resolve-SteamAccount -ActiveUser 0 -Candidates @() } 'No Steam account' 'no account'
    Assert-Throws { Resolve-SteamAccount -ActiveUser 0 -Candidates @([pscustomobject]@{ Account = 7; LastWriteUtc = $now }, [pscustomobject]@{ Account = 8; LastWriteUtc = $now.AddDays(-1) }) } 'Ambiguous' 'two accounts refused'

    # ------------------------------------------------------------ lab config and ini text
    $labJson = Get-LabConfig  # the repository's tools/lab/lab.json (read only)
    Assert-True ([IO.Path]::IsPathRooted($labJson.Paths.hostDir) -and [IO.Path]::IsPathRooted($labJson.Paths.runDir)) 'lab.json paths absolute'
    Assert-Equal ([int]$labJson.Ports.fuse) 6667 'fuse port'
    Assert-Equal ([string]$labJson.Identity.guestSteamId) '76561202255233008' 'guest fake id'
    Assert-Equal ([string]$labJson.Hashes.exe) '085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45' 'exe hash'

    $configText = (Get-Content -LiteralPath (Join-Path $projectRoot 'tools/lab/lab.json') -Raw | ConvertFrom-Json)
    $configText.paths.hostDir = (Join-Path $fixture 'games/MGS_TPP')
    $configText.paths.guestDir = (Join-Path $fixture 'games/MGS_TPP_guest')
    $fixtureConfigPath = Join-Path $fixture 'lab.json'
    [IO.File]::WriteAllText($fixtureConfigPath, ($configText | ConvertTo-Json -Depth 8))
    $config = Get-LabConfig -ConfigPath $fixtureConfigPath -RepoRoot $fixture
    Assert-Equal $config.Paths.runDir (Join-Path $fixture 'build\lab') 'relative runDir resolves against the repo root'
    $hostId = '76561197960265729'
    $hostSettings = Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 $hostId
    $guestSettings = Get-LabInstanceSettings -Role guest -Config $config -GameDirectory $config.Paths.guestDir -HostSteamId64 $hostId
    Assert-Equal $hostSettings.Marker 'host-v3' 'host marker'
    Assert-Equal $guestSettings.Marker 'client-v1' 'guest marker'
    Assert-Equal $guestSettings.ClientHost $hostId 'client-host = host id'
    Assert-Equal $hostSettings.SaveDir (Join-Path $config.Paths.hostDir 'mgs-coop\lab\saves') 'host save_dir'
    Assert-Equal $guestSettings.MasterUrl 'http://127.0.0.1:6667/tppstm/gate' 'fuse url'
    Assert-Throws { Get-LabInstanceSettings -Role guest -Config $config -GameDirectory $config.Paths.guestDir -HostSteamId64 '76561202255233008' } 'must differ' 'guest id equal to host id refused'
    Assert-Throws { Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 '123' } 'not a valid' 'bad host id'

    $hostIni = New-LabIniFromSettings -Settings $hostSettings
    $guestIni = New-LabIniFromSettings -Settings $guestSettings
    Assert-True (Test-LabGeneratedText $hostIni) 'generated header'
    Assert-True (-not (Test-LabGeneratedText "enabled=1`n")) 'user file not generated'
    $h = ConvertFrom-IniText $hostIni
    $g = ConvertFrom-IniText $guestIni
    Assert-Equal $h['role'] 'host' 'host role'
    Assert-Equal $h['self_steamid'] 'real' 'host real identity'
    Assert-Equal $h['peer_steamid'] '76561202255233008' 'host peer = guest fake id'
    Assert-Equal $h['listen_port'] '47810' 'host listen'
    Assert-Equal $h['peer_port'] '47811' 'host peer port'
    Assert-Equal $h['xinput'] 'none' 'host xinput from lab.json'
    Assert-True (-not $h.Contains('mutex_suffix')) 'host keeps the original mutex'
    Assert-Equal $g['self_steamid'] '76561202255233008' 'guest fake id'
    Assert-Equal $g['self_name'] 'Guest' 'guest name'
    Assert-Equal $g['peer_steamid'] $hostId 'guest peer = host'
    Assert-Equal $g['mutex_suffix'] '-guest' 'guest mutex suffix'
    Assert-Equal $g['listen_port'] '47811' 'guest listen'
    Assert-Equal $g['join_trigger'] '1' 'guest join trigger'
    Assert-Equal $g['loopback'] '1' 'guest loopback'
    Assert-True ($guestIni.EndsWith("`n") -and !$guestIni.Contains("`r")) 'LF line endings'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId real -ListenPort 47811 -PeerPort 47811 -SaveDir 'C:\x' } 'must differ' 'equal ports'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId real -ListenPort 80 -PeerPort 47811 -SaveDir 'C:\x' } 'Invalid lab port' 'low port'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId real -ListenPort 47810 -PeerPort 47811 -SaveDir 'relative\x' } 'absolute' 'relative save_dir'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId real -ListenPort 47810 -PeerPort 47811 -SaveDir 'C:\x' -MutexSuffix 'a b' } 'mutex_suffix' 'bad suffix'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId real -ListenPort 47810 -PeerPort 47811 -SaveDir 'C:\x' -XInput '4' } 'xinput' 'bad xinput'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId '76561197960265728' -ListenPort 47810 -PeerPort 47811 -SaveDir 'C:\x' } 'self_steamid' 'non-individual id'
    $coopText = New-CoopIniText -MasterUrl $guestSettings.MasterUrl -Character quiet
    $coop = ConvertFrom-IniText $coopText
    Assert-Equal $coop['master_url'] 'http://127.0.0.1:6667/tppstm/gate' 'coop master_url'
    Assert-Equal $coop['character'] 'quiet' 'coop character'
    Assert-Throws { New-CoopIniText -MasterUrl 'http://127.0.0.1:6667/tppstm/main' } 'Invalid master_url' 'bad suffix url'
    Assert-Throws { New-CoopIniText -MasterUrl 'http://127.0.0.1:6667/tppstm/gate' -Character dog } 'Invalid character' 'bad character'
    $parsed = ConvertFrom-IniText ([string][char]0xFEFF + "# c`r`n; c`r`n key = value `r`nnoequals`r`n=x`r`nkey=later`r`n")
    Assert-Equal $parsed['key'] 'later' 'ini: BOM, CRLF, comments, trimming, later wins'
    Assert-Equal $parsed.Count 1 'ini: lines without key ignored'

    $urls = @('http://127.0.0.1:6667/tppstm/gate', 'https://mgstpp-game.konamionline.com/tppstm/gate', 'http://h/tppstm/gate',
        'http:///tppstm/gate', 'http://tppstm/gate', 'ftp://h/tppstm/gate', 'HTTP://h/tppstm/gate', 'http://h/tppstm/gate/',
        'http://h st/tppstm/gate', 'http://h"/tppstm/gate', ('http://' + ('a' * 50) + '/tppstm/gate'), ('http://' + ('a' * 51) + '/tppstm/gate'), '')
    if ($IniCheck) {
        # Cross-check the scripts against the native parsers in the loader.
        foreach ($url in $urls) {
            if (!$url) { continue }
            $native = Invoke-IniCheck url $url
            Assert-Equal (Test-LabUrl $url) ($native['url_status'] -eq 'ok') "Test-LabUrl matches native ValidateUrl for $url"
        }
        $file = Join-Path $fixture 'host-lab.ini'; [IO.File]::WriteAllText($file, $hostIni)
        $n = Invoke-IniCheck lab $file
        Assert-Equal $n['present'] '1' 'native: host lab present'
        Assert-Equal $n['role'] 'host' 'native: host role'
        Assert-Equal $n['loopback'] '1' 'native: host loopback'
        Assert-Equal $n['self_steamid'] '0' 'native: host keeps real identity'
        Assert-Equal $n['peer_steamid'] '76561202255233008' 'native: host peer'
        Assert-Equal $n['listen_port'] '47810' 'native: host listen'
        Assert-Equal $n['xinput'] '-1' 'native: host xinput none'
        Assert-Equal $n['save_dir'] $hostSettings.SaveDir 'native: host save_dir'
        Assert-Equal "$($n['unknown_keys'])/$($n['invalid_values'])" '0/0' 'native: host ini clean'
        $file = Join-Path $fixture 'guest-lab.ini'; [IO.File]::WriteAllText($file, $guestIni)
        $n = Invoke-IniCheck lab $file
        Assert-Equal $n['role'] 'guest' 'native: guest role'
        Assert-Equal $n['self_steamid'] '76561202255233008' 'native: guest fake id'
        Assert-Equal $n['self_name'] 'Guest' 'native: guest name'
        Assert-Equal $n['peer_steamid'] $hostId 'native: guest peer'
        Assert-Equal $n['mutex_suffix'] '-guest' 'native: guest suffix'
        Assert-Equal "$($n['listen_port'])/$($n['peer_port'])" '47811/47810' 'native: guest ports'
        Assert-Equal $n['xinput'] '0' 'native: guest pad 0'
        Assert-Equal $n['join_trigger'] '1' 'native: guest join trigger'
        Assert-Equal "$($n['unknown_keys'])/$($n['invalid_values'])" '0/0' 'native: guest ini clean'
        $file = Join-Path $fixture 'coop.ini'; [IO.File]::WriteAllText($file, $coopText)
        $n = Invoke-IniCheck coop $file
        Assert-Equal $n['master_url'] 'http://127.0.0.1:6667/tppstm/gate' 'native: coop master_url'
        Assert-Equal $n['character'] 'quiet' 'native: coop character'
        Assert-Equal $n['character_fallback'] 'dd_male' 'native: coop fallback'
        Assert-Equal "$($n['unknown_keys'])/$($n['invalid_values'])" '0/0' 'native: coop ini clean'
    } else { Write-Warning 'lab_ini_check not supplied; native cross-checks skipped.' }

    # ------------------------------------------------------------ TPP_GRAPHICS_CONFIG tag
    # A real (graphics settings only) config written by mgsvtpp 1.0.15.4 on this PC.
    $realConfig = @'
{
   "as" : "UzF+nvqVP+4=",
   "graphics" : {
      "quality_setting" : {
         "depth_of_field" : "Enable",
         "effect" : "ExtraHigh",
         "lighting" : "ExtraHigh",
         "model_detail" : "ExtraHigh",
         "motion_blur_amount" : "Large",
         "postprocess" : "ExtraHigh",
         "shadow" : "ExtraHigh",
         "ssao" : "ExtraHigh",
         "texture" : "ExtraHigh",
         "texture_filtering" : "ExtraHigh",
         "volumetric_clouds" : "On"
      },
      "version" : 1,
      "videoout_setting" : {
         "display_index" : 0,
         "framerate_control" : "Auto",
         "height" : 1440,
         "vsync" : "Enable",
         "width" : 2560,
         "window_mode" : "FullScreenWindowed"
      }
   },
   "project" : "tpp"
}
'@.Replace("`r`n", "`n")
    $info = Get-GraphicsConfigInfo -Text $realConfig
    Assert-True $info.Valid 'real config tag verifies (FastWriter + "-%x" length salt + MD5[0..8])'
    Assert-Equal $info.ComputedTag 'UzF+nvqVP+4=' 'computed tag'
    Assert-Equal "$($info.WindowMode) $($info.Width)x$($info.Height)" 'FullScreenWindowed 2560x1440' 'parsed video settings'
    $windowed = Set-GraphicsConfigWindowedText -Text $realConfig -Width 1280 -Height 720
    $after = Get-GraphicsConfigInfo -Text $windowed
    Assert-True $after.Valid 'edited config verifies'
    Assert-Equal $after.StoredTag 'a8ID1auvq+8=' 'edited tag (independently computed with Python hashlib)'
    Assert-Equal "$($after.WindowMode) $($after.Width)x$($after.Height)" 'Windowed 1280x720' 'edited video settings'
    Assert-Equal (($windowed -split "`n").Count) (($realConfig -split "`n").Count) 'formatting preserved'
    $tampered = $realConfig.Replace('"width" : 2560', '"width" : 2561')
    Assert-True (-not (Get-GraphicsConfigInfo -Text $tampered).Valid) 'tampered config does not verify'
    Assert-Throws { Set-GraphicsConfigWindowedText -Text $tampered } 'does not verify' 'refuse to edit an unverifiable config'
    Assert-Throws { Set-GraphicsConfigWindowedText -Text '{"as":"x"}' } 'does not verify' 'refuse unexpected layout'
    $synthetic = '{"b":1,"a":{"z":"q\"\\\n","y":-5},"c":true}' | ConvertFrom-Json
    Assert-Equal (ConvertTo-JsonCppFastText -Value $synthetic -Document) ("{`"a`":{`"y`":-5,`"z`":`"q\`"\\\n`"},`"b`":1,`"c`":true}" + "`n") 'FastWriter: sorted keys, escapes, ints, bools'
    Assert-Equal (Get-GraphicsConfigTag -Object $synthetic) 'vrSYXqLC+9k=' 'synthetic tag (independently computed with Python)'
    Assert-Throws { ConvertTo-JsonCppFastText -Value 1.5 } 'Floating-point' 'doubles refused'

    # ------------------------------------------------------------ log matching and verdicts
    $hostReady = "mode=host_v3; peer_acceptance=after_mission_start; max_players=2`r`n" +
        "dynamite_host=returned; result=1 thread=83844 tick_ms=1 peer_acceptance_requested=0`r`n" +
        "host_admission=authorized; owned_host=1 same_thread=1 lua_interface=SessionEnableAccept`r`n" +
        "session_status=host_ready`r`n"
    $guestArmed = "mode=client_v1; configured_host=1`r`nclient_join=armed; source=trigger_file`r`n"
    $guestStarted = $guestArmed + "client_join=input_received; key=trigger_file`r`ndynamite_client=returned; request_started=1`r`nsession_status=client_requested`r`n"
    Assert-True (Test-LogLine $hostReady 'dynamite_host=returned; result=1') 'prefix followed by space'
    Assert-True (-not (Test-LogLine "dynamite_host=returned; result=10`r`n" 'dynamite_host=returned; result=1')) 'result=10 is not result=1'
    Assert-True (-not (Test-LogLine "xsession_status=host_ready`n" 'session_status=host_ready')) 'prefix anchored at line start'
    Assert-True (-not (Test-LogLine "session_status=host_ready_extra`n" 'session_status=host_ready')) 'value must end'
    Assert-True (Test-LogLine 'session_status=host_ready' 'session_status=host_ready') 'end of text counts'
    Assert-Equal (Get-LastSessionStatus "session_status=host_ready`nsession_status=host_peer_present`n") 'host_peer_present' 'last session status'
    Assert-Equal (Get-FirstLogLine $hostReady 'host_admission=authorized') 'host_admission=authorized; owned_host=1 same_thread=1 lua_interface=SessionEnableAccept' 'first line evidence'

    $v = Get-LabSmokeVerdict
    Assert-Equal "$($v.Verdict)/$($v.Phase)" 'pending/waiting_host_ready' 'nothing yet'
    $v = Get-LabSmokeVerdict -HostLog $hostReady
    Assert-Equal "$($v.Verdict)/$($v.Phase)" 'pending/waiting_guest_armed' 'host ready'
    Assert-True (-not (Get-LabSmokeVerdict -HostLog ($hostReady.Replace('host_admission=authorized', 'host_admission=refused'))).HostReady) 'admission required'
    $v = Get-LabSmokeVerdict -HostLog $hostReady -GuestLog $guestArmed
    Assert-Equal "$($v.Phase)/$($v.ShouldTrigger)" 'ready_to_trigger/True' 'trigger when both ready'
    $v = Get-LabSmokeVerdict -HostLog $hostReady -GuestLog $guestArmed -TriggerCreated $true
    Assert-Equal "$($v.Phase)/$($v.ShouldTrigger)" 'waiting_request_started/False' 'no second trigger'
    $v = Get-LabSmokeVerdict -HostLog $hostReady -GuestLog $guestStarted -TriggerCreated $true
    Assert-Equal "$($v.Verdict)/$($v.Phase)" 'pending/waiting_established' 'request started'
    $hostPeer = $hostReady + "session_status=host_peer_present`r`n"
    $guestEstablished = $guestStarted + "session_status=client_established`r`n"
    $v = Get-LabSmokeVerdict -HostLog $hostPeer -GuestLog $guestEstablished -TriggerCreated $true
    Assert-Equal "$($v.Verdict)/$($v.PassPolls)" 'pending/1' 'first established poll'
    $v = Get-LabSmokeVerdict -HostLog $hostPeer -GuestLog $guestEstablished -TriggerCreated $true -PreviousPassPolls 1
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'pass/established_stable' 'two consecutive polls pass'
    $v = Get-LabSmokeVerdict -HostLog $hostReady -GuestLog $guestEstablished -TriggerCreated $true -PreviousPassPolls 1
    Assert-Equal "$($v.Verdict)/$($v.PassPolls)" 'pending/0' 'host peer missing resets the count'
    $guestStatus = ConvertFrom-LabStatusText -Text '{"version":1,"pid":77,"role":"guest","session_status":"client_established","join":"established"}' -ExpectedPid 77
    $hostStatus = ConvertFrom-LabStatusText -Text '{"version":1,"pid":55,"role":"host","session_status":"host_peer_present","join":"idle"}' -ExpectedPid 55
    $v = Get-LabSmokeVerdict -HostLog $hostReady -GuestLog $guestStarted -HostStatus $hostStatus -GuestStatus $guestStatus -TriggerCreated $true -PreviousPassPolls 1
    Assert-Equal $v.Verdict 'pass' 'lab-status.json drives the session state'
    Assert-True ($null -eq (ConvertFrom-LabStatusText -Text '{"pid":1,"session_status":"none"}' -ExpectedPid 2)) 'status from another process ignored'
    Assert-True ($null -eq (ConvertFrom-LabStatusText -Text '{"pid":1,' -ExpectedPid 1)) 'partial json ignored'
    Assert-True ($null -eq (ConvertFrom-LabStatusText -Text '{"pid":1}' -ExpectedPid 1)) 'json without session_status ignored'
    $failures = @(
        @{ Guest = $guestArmed + "dynamite_client=refused; reason=x`r`n"; Reason = 'dynamite_client_refused' },
        @{ Guest = $guestArmed + "dynamite_client=returned; request_started=0`r`n"; Reason = 'request_started_0' },
        @{ Guest = $guestArmed + "client_join=refused; reason=placeholder_host_id`r`n"; Reason = 'client_join_refused' },
        @{ Guest = "mode=client_v1; refused=missing_or_invalid_client_host_txt`r`n"; Reason = 'missing_or_invalid_client_host_txt' },
        @{ Guest = $guestStarted + "session_status=session_error`r`n"; Reason = 'guest_session_error' }
    )
    foreach ($case in $failures) {
        $v = Get-LabSmokeVerdict -HostLog $hostReady -GuestLog $case.Guest -TriggerCreated $true
        Assert-Equal "$($v.Verdict)/$($v.Reason)" "fail/$($case.Reason)" "failure rule $($case.Reason)"
    }
    Assert-Equal (Get-LabSmokeVerdict -HostLog ($hostReady + "session_status=session_error`n")).Reason 'host_session_error' 'host session error'
    Assert-Equal (Get-LabSmokeVerdict -HostLog "dynamite_host=returned; result=0 thread=1`n").Reason 'host_creation_failed' 'host creation failed'
    Assert-Equal (Get-LabSmokeVerdict -HostLog $hostReady -HostAlive $false).Reason 'process_exit_host' 'host exit'
    Assert-Equal (Get-LabSmokeVerdict -HostLog $hostReady -GuestAlive $false).Reason 'process_exit_guest' 'guest exit'
    Assert-Equal (Get-LabSmokeVerdict -HostLog $hostReady -GuestAlive $false -HostOnly).Verdict 'pending' 'guest ignored in host-only runs'
    $v = Get-LabSmokeVerdict -HostLog $hostReady -ReadyElapsedSec 900 -ReadyTimeoutSec 900
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/timeout_waiting_guest_armed' 'readiness timeout'
    $v = Get-LabSmokeVerdict -HostLog $hostReady -GuestLog $guestStarted -TriggerCreated $true -ReadyElapsedSec 5000 -TriggerElapsedSec 179 -TimeoutSec 180
    Assert-Equal $v.Verdict 'pending' 'readiness timeout does not apply after the trigger'
    $v = Get-LabSmokeVerdict -HostLog $hostReady -GuestLog $guestStarted -TriggerCreated $true -TriggerElapsedSec 180 -TimeoutSec 180
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/timeout_waiting_established' 'session timeout after the trigger'
    $v = Get-LabSmokeVerdict -HostLog $hostReady -HostOnly
    Assert-Equal "$($v.Verdict)/$($v.PassPolls)" 'pending/1' 'host-only first poll'
    $v = Get-LabSmokeVerdict -HostLog $hostReady -HostOnly -PreviousPassPolls 1
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'pass/host_ready_stable' 'host-only pass (M0)'

    # ------------------------------------------------------------ offset log reader
    $log = Join-Path $fixture 'observer.log'
    [IO.File]::WriteAllText($log, "a`nb`npartial")
    $r = Read-LogFromOffset -Path $log -Offset 0
    Assert-Equal "$($r.Text)|$($r.NewOffset)" "a`nb`n|4" 'complete lines only'
    $writer = [IO.FileStream]::new($log, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
    try {
        $bytes = [Text.Encoding]::ASCII.GetBytes("X`n")
        $writer.Write($bytes, 0, $bytes.Length); $writer.Flush()
        $r = Read-LogFromOffset -Path $log -Offset 4  # while the "game" still holds the file open for writing
        Assert-Equal "$($r.Text)|$($r.NewOffset)" "partialX`n|13" 'reads appended data while the writer is open'
    } finally { $writer.Dispose() }
    [IO.File]::WriteAllText($log, "new`n")
    $r = Read-LogFromOffset -Path $log -Offset 13
    Assert-True ($r.Truncated -and $r.Text -eq "new`n") 'replaced file restarts at 0'
    Assert-True (-not (Read-LogFromOffset -Path (Join-Path $fixture 'missing.log')).Exists) 'missing log'
    Assert-Equal (Get-FileLengthOrZero (Join-Path $fixture 'missing.log')) ([Int64]0) 'missing length'

    # ------------------------------------------------------------ manifests, backup and seeding
    $userdata = Join-Path $fixture 'steam/userdata/7'
    foreach ($f in '287700/local/TPP_GAME_DATA0', '287700/local/TPP_GRAPHICS_CONFIG', '311340/remote/TPP_GAME_DATA', '287700/remotecache.vdf') {
        $path = Join-Path $userdata $f
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
        [IO.File]::WriteAllText($path, "fixture $f")
    }
    $backup = Join-Path $fixture 'build/lab/save-backups/1'
    $manifest = Backup-RealSaves -UserData $userdata -Destination $backup
    Assert-Equal @($manifest.verified).Count 3 'verified rows = 287700/local + 311340/remote'
    Assert-Equal @($manifest.all_files).Count 4 'informational backup includes the rest of 287700'
    Assert-True (Test-Path -LiteralPath (Join-Path $backup 'files/287700/remotecache.vdf')) 'backup copy written'
    Assert-Throws { Backup-RealSaves -UserData $userdata -Destination $backup } 'already exists' 'no backup overwrite'
    Assert-Throws { Backup-RealSaves -UserData $userdata -Destination (Join-Path $userdata 'backup') } 'outside Steam userdata' 'no backup inside userdata'
    Assert-True (Test-RealSavesAgainstBackup -BackupDirectory $backup).Comparison.Same 'unchanged saves'
    [IO.File]::WriteAllText((Join-Path $userdata '287700/remotecache.vdf'), 'Steam rewrites this; not verified')
    Assert-True (Test-RealSavesAgainstBackup -BackupDirectory $backup).Comparison.Same 'files outside the verified folders are ignored'
    [IO.File]::WriteAllText((Join-Path $userdata '287700/local/TPP_GAME_DATA0'), 'changed')
    [IO.File]::WriteAllText((Join-Path $userdata '311340/remote/NEW'), 'added')
    Remove-Item -LiteralPath (Join-Path $userdata '287700/local/TPP_GRAPHICS_CONFIG')
    $cmp = (Test-RealSavesAgainstBackup -BackupDirectory $backup).Comparison
    Assert-True (-not $cmp.Same) 'changes detected'
    Assert-Equal ($cmp.Changed -join ',') '287700/local/TPP_GAME_DATA0' 'changed file'
    Assert-Equal ($cmp.Missing -join ',') '287700/local/TPP_GRAPHICS_CONFIG' 'missing file'
    Assert-Equal ($cmp.Added -join ',') '311340/remote/NEW' 'added file'
    $pure = Compare-HashManifest -Expected @([pscustomobject]@{ path = 'a'; sha256 = '1' }) -Actual @([pscustomobject]@{ path = 'a'; sha256 = '1' })
    Assert-True $pure.Same 'pure compare same'
    Assert-True (-not (Compare-HashManifest -Expected @() -Actual @([pscustomobject]@{ path = 'a'; sha256 = '1' })).Same) 'pure compare added'
    $saveDir = Join-Path $fixture 'games/MGS_TPP_guest/mgs-coop/lab/saves'
    $seed = Copy-SaveSeed -BackupDirectory $backup -SaveDir $saveDir
    Assert-Equal "$($seed.Seeded)/$($seed.Files)" 'True/3' 'seeded from the backup copy'
    Assert-True (Test-Path -LiteralPath (Join-Path $saveDir '287700/local/TPP_GRAPHICS_CONFIG')) 'local -> <save_dir>/287700/local'
    Assert-True (Test-Path -LiteralPath (Join-Path $saveDir 'remote/TPP_GAME_DATA')) 'remote -> <save_dir>/remote'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $saveDir '287700/local/TPP_GAME_DATA0'))) 'fixture 287700/local/TPP_GAME_DATA0' 'seed comes from the backup, not the live folder'
    Assert-Equal (Copy-SaveSeed -BackupDirectory $backup -SaveDir $saveDir).Reason 'already_seeded' 'seeding is idempotent'
    [IO.File]::WriteAllText((Join-Path $saveDir '287700/local/LAB_PROGRESS'), 'lab play')
    $archive = Join-Path $fixture 'build/lab/save-archive/1/guest'
    $seed = Copy-SaveSeed -BackupDirectory $backup -SaveDir $saveDir -Reseed -ArchiveDirectory $archive
    Assert-True ($seed.Seeded -and !(Test-Path -LiteralPath (Join-Path $saveDir '287700/local/LAB_PROGRESS'))) 'reseed replaces lab copies'
    Assert-Equal @(Get-ChildItem -LiteralPath $archive -Recurse -File -Filter LAB_PROGRESS).Count 1 'reseed archives old lab copies'
    Assert-True (Write-TextFileIfChanged -Path (Join-Path $fixture 'w.txt') -Text "x`n") 'first write changes'
    Assert-True (-not (Write-TextFileIfChanged -Path (Join-Path $fixture 'w.txt') -Text "x`n")) 'same text is a no-op'

    # ------------------------------------------------------------ processes, fuse
    $dir = Join-Path $fixture 'games/MGS_TPP'
    $rows = Select-ProcessInDirectory -Directory $dir -Processes @(
        [pscustomobject]@{ Id = 1; Path = (Join-Path $dir 'mgsvtpp.exe') },
        [pscustomobject]@{ Id = 2; Path = (Join-Path $fixture 'games/MGS_TPP_guest/mgsvtpp.exe') },
        [pscustomobject]@{ Id = 3; Path = $null },
        [pscustomobject]@{ Id = 4; Path = (Join-Path $dir 'sub/mgsvtpp.exe') })
    Assert-Equal (@($rows | ForEach-Object { "$($_.Id):$($_.Unknown)" }) -join ',') '1:False,3:True' 'process matching by folder path'
    $fuseArgs = New-FuseArguments -Port 6667
    Assert-Equal ($fuseArgs -join ' ') '-standalone -listenAddr 127.0.0.1:6667 -baseURL http://127.0.0.1:6667/ -writeLog' 'fuse arguments'
    Assert-Throws { Assert-FuseArgumentsSafe -Arguments @('-listenAddr', '127.0.0.1:6667') } 'standalone' 'fuse without -standalone refused'
    Assert-Throws { Assert-FuseArgumentsSafe -Arguments @('-standalone', '-listenAddr', '0.0.0.0:6667') } '127.0.0.1' 'fuse on all interfaces refused'
    Assert-Throws { Assert-FuseArgumentsSafe -Arguments @('-standalone', '-listenAddr', '127.0.0.1:6667', '-clientURL', 'x') } 'clientURL' 'fuse -clientURL refused'
    $fuseLog = Join-Path $fixture 'fuse/log/1700000000'
    $null = New-Item -ItemType Directory -Path $fuseLog -Force
    foreach ($n in '1700000000001_CMD_GET_URLLIST_request', '1700000000002_CMD_AUTH_STEAMTICKET_request', '1700000000003_CMD_GET_URLLIST_request', 'junk') {
        [IO.File]::WriteAllText((Join-Path $fuseLog $n), '{}')
    }
    $null = New-Item -ItemType Directory -Path (Join-Path $fixture 'fuse/log/1600000000') -Force
    [IO.File]::WriteAllText((Join-Path $fixture 'fuse/log/1600000000/1_CMD_OLD_request'), '{}')
    $summary = Get-FuseMessageSummary -FuseDirectory (Join-Path $fixture 'fuse') -SinceUnix 1699999999
    Assert-Equal "$($summary.CMD_GET_URLLIST)/$($summary.CMD_AUTH_STEAMTICKET)" '2/1' 'fuse message counts'
    Assert-True (-not ($summary.PSObject.Properties.Name -contains 'CMD_OLD')) 'older fuse runs excluded'
    $trigger = Join-Path $fixture 'join.trigger'
    New-TriggerFile -Path $trigger
    Assert-True ((Test-Path -LiteralPath $trigger) -and @(Get-ChildItem -LiteralPath $fixture -Filter 'join.trigger.*.tmp').Count -eq 0) 'trigger created atomically'

    # ------------------------------------------------------------ owned install into a host and a guest copy
    $games = Join-Path $fixture 'install'
    $hostGame = Join-Path $games 'MGS_TPP'
    $null = New-Item -ItemType Directory -Path (Join-Path $hostGame 'mgs-coop') -Force
    [IO.File]::WriteAllText((Join-Path $hostGame 'mgsvtpp.exe'), 'INERT executable fixture; never executed')
    [IO.File]::WriteAllText((Join-Path $hostGame 'steam_api64.dll'), 'INERT Steam DLL fixture')
    $archiveSource = Join-Path $fixture 'runtime-archive.dat'
    [IO.File]::WriteAllText($archiveSource, 'INERT runtime archive fixture')
    $loader1 = Join-Path $fixture 'loader1.dll'; [IO.File]::WriteAllText($loader1, 'INERT loader 1')
    $loader2 = Join-Path $fixture 'loader2.dll'; [IO.File]::WriteAllText($loader2, 'INERT loader 2')
    $exeHash = Get-Sha256 (Join-Path $hostGame 'mgsvtpp.exe')
    $steamHash = Get-Sha256 (Join-Path $hostGame 'steam_api64.dll')
    $archiveHash = Get-Sha256 $archiveSource
    $installer = Join-Path $projectRoot 'tools/package-install.ps1'
    $stage1 = Join-Path $fixture 'stage1'
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $stage1 -LoaderPath $loader1 -Version '0.10.33' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 $archiveHash
    $package = Get-Content -LiteralPath (Join-Path $stage1 'package.json') -Raw | ConvertFrom-Json
    Assert-Equal @($package.files).Count 5 'package has no runtime archive payload'
    Assert-True (-not (@($package.files.target) -contains 'mgs-coop/mgs-coop-player-runtime.dat')) 'archive referenced by hash only'
    Assert-Equal $package.runtime_archive.sha256 $archiveHash 'archive hash recorded'
    Assert-InFixture $hostGame
    Assert-Throws { & $installer -Action Install -GameDirectory $hostGame -PackageDirectory $stage1 } 'does not contain game-derived files' 'archive required on a fresh install'
    $null = & $installer -Action Install -GameDirectory $hostGame -PackageDirectory $stage1 -RuntimeArchive $archiveSource
    $hostRecord = Get-Content -LiteralPath (Join-Path $hostGame 'mgs-coop/probe-install.json') -Raw | ConvertFrom-Json
    Assert-Equal $hostRecord.game_directory $hostGame 'host record names the host folder'
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'dinput8.dll')) (Get-Sha256 $loader1) 'host loader installed'
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'mgs-coop/mgs-coop-player-runtime.dat')) $archiveHash 'host archive installed'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $hostGame 'mgs-coop/script-observe.enabled'))) "client-check-v1`n" 'first install defaults to diagnostics'
    # Upgrade reuses the installed archive.
    $stage2 = Join-Path $fixture 'stage2'
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $stage2 -LoaderPath $loader2 -Version '0.10.33' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 $archiveHash
    $null = & $installer -Action Install -GameDirectory $hostGame -PackageDirectory $stage2
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'dinput8.dll')) (Get-Sha256 $loader2) 'upgrade without -RuntimeArchive reuses the installed archive'
    # The guest folder starts as a copy of the host folder (record names the host).
    $guestGame = Join-Path $games 'MGS_TPP_guest'
    Assert-InFixture $guestGame
    $null = New-Item -ItemType Directory -Path $guestGame -Force
    Copy-Item -Path (Join-Path $hostGame '*') -Destination $guestGame -Recurse
    Assert-Throws { & $installer -Action Install -GameDirectory $guestGame -PackageDirectory $stage1 } 'AcceptRecordCopiedFrom' 'copied record refused by default'
    Assert-Throws { & $installer -Action Install -GameDirectory $guestGame -PackageDirectory $stage1 -AcceptRecordCopiedFrom $guestGame } 'ownership' 'copied-from must be the recorded folder'
    $null = & $installer -Action Install -GameDirectory $guestGame -PackageDirectory $stage1 -AcceptRecordCopiedFrom $hostGame
    $guestRecord = Get-Content -LiteralPath (Join-Path $guestGame 'mgs-coop/probe-install.json') -Raw | ConvertFrom-Json
    Assert-Equal $guestRecord.game_directory $guestGame 'guest record names the guest folder'
    Assert-Equal (Get-Sha256 (Join-Path $guestGame 'dinput8.dll')) (Get-Sha256 $loader1) 'guest loader installed'
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'dinput8.dll')) (Get-Sha256 $loader2) 'host untouched by the guest install'
    $rollback = Get-Content -LiteralPath (Join-Path $guestGame ("mgs-coop/install-backups/$($guestRecord.rollback_id)/rollback.json")) -Raw | ConvertFrom-Json
    Assert-Equal @($rollback.files).Count 7 'guest rollback inventory complete'
    $null = & $installer -Action Restore -GameDirectory $guestGame
    Assert-Equal (Get-Sha256 (Join-Path $guestGame 'dinput8.dll')) (Get-Sha256 $loader2) 'guest restore returns the copied loader'
    Assert-Equal (Get-Content -LiteralPath (Join-Path $guestGame 'mgs-coop/probe-install.json') -Raw | ConvertFrom-Json).game_directory $hostGame 'guest restore returns the copied record'
    $null = & $installer -Action Restore -GameDirectory $hostGame
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'dinput8.dll')) (Get-Sha256 $loader1) 'host restore returns loader 1'
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'mgsvtpp.exe')) $exeHash 'executable fixture unchanged'

    # ------------------------------------------------------------ end to end on a simulated run (no game)
    # Two idle pwsh processes stand in for the game instances; a small simulator
    # plays the native side (consumes join.trigger, appends the protocol lines).
    $pwshPath = (Get-Process -Id $PID).Path
    $e2e = Join-Path $fixture 'e2e'
    $e2eHost = Join-Path $e2e 'MGS_TPP'
    $e2eGuest = Join-Path $e2e 'MGS_TPP_guest'
    foreach ($d in $e2eHost, $e2eGuest) { $null = New-Item -ItemType Directory -Path (Join-Path $d 'mgs-coop') -Force }
    $e2eJson = Get-Content -LiteralPath (Join-Path $projectRoot 'tools/lab/lab.json') -Raw | ConvertFrom-Json
    $e2eJson.paths.hostDir = $e2eHost
    $e2eJson.paths.guestDir = $e2eGuest
    $e2eJson.paths.runDir = (Join-Path $e2e 'lab')
    $e2eJson.paths.fuseSource = (Join-Path $e2e 'fuse')
    $e2eJson.paths.fuseExe = (Join-Path $e2e 'fuse/fuse.exe')
    $e2eConfigPath = Join-Path $e2e 'lab.json'
    [IO.File]::WriteAllText($e2eConfigPath, ($e2eJson | ConvertTo-Json -Depth 8))
    $hostLog = Join-Path $e2eHost 'mgs-coop/script-observer.log'
    $guestLog = Join-Path $e2eGuest 'mgs-coop/script-observer.log'
    [IO.File]::WriteAllText($hostLog, "previous run line`r`nsession_status=session_error`r`n")  # before the offset: ignored
    [IO.File]::WriteAllText($guestLog, "dynamite_client=refused; reason=previous_run`r`n")
    $offsets = @{ host = (Get-Item -LiteralPath $hostLog).Length; guest = (Get-Item -LiteralPath $guestLog).Length }
    [IO.File]::AppendAllText($hostLog, $hostReady)
    [IO.File]::AppendAllText($guestLog, $guestArmed)
    $dummies = @()
    try {
        foreach ($i in 1, 2) {
            $dummies += Start-Process -FilePath $pwshPath -ArgumentList '-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 300' -WindowStyle Hidden -PassThru
        }
        function New-FixtureRun([string]$Name) {
            $dir = Join-Path $e2e "lab/runs/$Name"
            $null = New-Item -ItemType Directory -Path $dir -Force
            Write-JsonFile -Path (Join-Path $dir 'real-saves-before.json') -Value ([ordered]@{ source_root = $userdata; files = (Get-RealSaveManifest -UserData $userdata) })
            $run = [ordered]@{
                product = 'mgs-coop-lab-run'; schema = 1; stamp = $Name; run_dir = $dir; host_only = $false; fuse = $null
                real_saves_before = 'real-saves-before.json'; latest_save_backup = $backup
                instances = [ordered]@{
                    host = [ordered]@{ role = 'host'; dir = $e2eHost; exe = $pwshPath; pid = $dummies[0].Id; log_offsets = [ordered]@{ 'script-observer.log' = $offsets.host } }
                    guest = [ordered]@{ role = 'guest'; dir = $e2eGuest; exe = $pwshPath; pid = $dummies[1].Id; log_offsets = [ordered]@{ 'script-observer.log' = $offsets.guest } }
                }
            }
            Write-JsonFile -Path (Join-Path $dir 'run.json') -Value $run
            return $dir
        }
        $simulator = Join-Path $e2e 'simulate-native.ps1'
        [IO.File]::WriteAllText($simulator, @'
param([string]$Trigger, [string]$HostLog, [string]$GuestLog)
$deadline = [DateTime]::UtcNow.AddSeconds(60)
while (!(Test-Path -LiteralPath $Trigger)) { if ([DateTime]::UtcNow -gt $deadline) { exit 3 }; Start-Sleep -Milliseconds 100 }
Remove-Item -LiteralPath $Trigger
[IO.File]::AppendAllText($GuestLog, "client_join=input_received; key=trigger_file`r`ndynamite_client=returned; request_started=1`r`nsession_status=client_requested`r`n")
Start-Sleep -Milliseconds 500
[IO.File]::AppendAllText($HostLog, "session_status=host_peer_present`r`n")
[IO.File]::AppendAllText($GuestLog, "session_status=client_established`r`n")
'@)
        $runDir = New-FixtureRun 'pass'
        $trigger = Join-Path $e2eGuest 'mgs-coop/join.trigger'
        $sim = Start-Process -FilePath $pwshPath -ArgumentList '-NoProfile', '-NonInteractive', '-File', $simulator, $trigger, $hostLog, $guestLog -WindowStyle Hidden -PassThru
        $smoke = Join-Path $projectRoot 'tools/lab/Invoke-LabSmoke.ps1'
        $output = & $pwshPath -NoProfile -NonInteractive -File $smoke -ConfigPath $e2eConfigPath -RunDir $runDir -NoScreens -PollMs 200 -TimeoutSec 20 -ReadyTimeoutSec 30 2>&1
        $exitCode = $LASTEXITCODE
        $null = $sim.WaitForExit(10000)
        Assert-Equal $exitCode 0 ("smoke passes on the simulated run: " + ($output -join ' | '))
        $result = Get-Content -LiteralPath (Join-Path $runDir 'result.json') -Raw | ConvertFrom-Json
        Assert-Equal "$($result.verdict)/$($result.reason)" 'pass/established_stable' 'result.json verdict'
        Assert-True $result.trigger.created 'trigger recorded'
        Assert-True ($null -ne $result.timings.trigger_sec -and $null -ne $result.timings.established_sec) 'timings recorded'
        Assert-True (-not (Test-Path -LiteralPath $trigger)) 'trigger consumed by the simulated native side'
        Assert-Equal $result.evidence.host.observer.'host_admission=authorized' 'host_admission=authorized; owned_host=1 same_thread=1 lua_interface=SessionEnableAccept' 'evidence line'
        $excerpt = [IO.File]::ReadAllText((Join-Path $runDir 'host-script-observer.log'))
        Assert-True (-not $excerpt.Contains('previous run line') -and $excerpt.Contains('host_peer_present')) 'excerpt covers only this run'
        Assert-True (@($result.files) -contains 'guest-script-observer.log') 'guest excerpt copied'

        # A refusal after the run's offsets fails the smoke.
        [IO.File]::AppendAllText($guestLog, "dynamite_client=refused; reason=not_ready`r`n")
        $runDir2 = New-FixtureRun 'fail'
        $null = & $pwshPath -NoProfile -NonInteractive -File $smoke -ConfigPath $e2eConfigPath -RunDir $runDir2 -NoScreens -PollMs 200 -TimeoutSec 5 -ReadyTimeoutSec 5 2>&1
        Assert-Equal $LASTEXITCODE 1 'smoke exit code on failure'
        $result = Get-Content -LiteralPath (Join-Path $runDir2 'result.json') -Raw | ConvertFrom-Json
        Assert-Equal "$($result.verdict)/$($result.reason)" 'fail/dynamite_client_refused' 'failure verdict'

        # Stop-Lab closes/terminates only the run's processes and checks the (fixture) real saves.
        $stopScript = Join-Path $projectRoot 'tools/lab/Stop-Lab.ps1'
        $null = & $pwshPath -NoProfile -NonInteractive -File $stopScript -ConfigPath $e2eConfigPath -RunDir $runDir -TimeoutSec 3 2>&1
        Assert-Equal $LASTEXITCODE 0 'Stop-Lab exit code with unchanged saves'
        foreach ($d in $dummies) { Assert-True $d.WaitForExit(5000) "stand-in process $($d.Id) stopped" }
        $stop = Get-Content -LiteralPath (Join-Path $runDir 'stop.json') -Raw | ConvertFrom-Json
        Assert-True $stop.real_saves.unchanged_since_run_start 'saves unchanged since run start'
        [IO.File]::WriteAllText((Join-Path $userdata '311340/remote/TPP_GAME_DATA'), 'modified during the run')
        $null = & $pwshPath -NoProfile -NonInteractive -File $stopScript -ConfigPath $e2eConfigPath -RunDir $runDir -TimeoutSec 1 2>&1
        Assert-Equal $LASTEXITCODE 2 'Stop-Lab reports a changed real save'
    } finally {
        foreach ($d in $dummies) { if (!$d.HasExited) { Stop-Process -Id $d.Id -Force -ErrorAction SilentlyContinue } }
    }

    # ------------------------------------------------------------ full harness dry run with stand-in processes
    # Prepare -> Start -> Smoke -> Screens -> Stop -> Disable against fixture folders.
    # lab_fake_process.exe is copied as the fixture's mgsvtpp.exe and fuse.exe; it
    # only runs inside a folder with mgs-coop\lab.ini and plays the native side.
    if ($FakeProcess) {
        $dry = Join-Path $fixture 'dry'
        $dryHost = Join-Path $dry 'MGS_TPP'
        $dryGuest = Join-Path $dry 'MGS_TPP_guest'
        $drySteam = Join-Path $dry 'Steam'
        $dryHostId = ConvertTo-SteamId64 -AccountId 12345
        $dryUser = Join-Path $drySteam 'userdata/12345'
        Assert-InFixture $dry
        foreach ($f in '287700/local/TPP_GAME_DATA0', '311340/remote/TPP_GAME_DATA') {
            $path = Join-Path $dryUser $f
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
            [IO.File]::WriteAllText($path, "dry $f")
        }
        $realGraphics = Join-Path $dryUser '287700/local/TPP_GRAPHICS_CONFIG'
        [IO.File]::WriteAllText($realGraphics, $realConfig)
        $realGraphicsHash = Get-Sha256 $realGraphics
        foreach ($d in $dryHost, $dryGuest) {
            $null = New-Item -ItemType Directory -Path $d -Force
            Copy-Item -LiteralPath $FakeProcess -Destination (Join-Path $d 'mgsvtpp.exe')
            [IO.File]::WriteAllText((Join-Path $d 'steam_api64.dll'), 'INERT Steam DLL fixture')
        }
        $null = New-Item -ItemType Directory -Path (Join-Path $dry 'fuse') -Force
        Copy-Item -LiteralPath $FakeProcess -Destination (Join-Path $dry 'fuse/fuse.exe')
        $dryArchive = Join-Path $dry 'runtime-archive.dat'; [IO.File]::WriteAllText($dryArchive, 'INERT dry archive')
        $dryLoader = Join-Path $dry 'built-dinput8.dll'; [IO.File]::WriteAllText($dryLoader, 'INERT dry loader')
        $base = 0
        for ($attempt = 0; $attempt -lt 20 -and !$base; $attempt++) {
            $candidate = Get-Random -Minimum 20000 -Maximum 40000
            if ((Test-LocalPortFree -Port $candidate -Protocol Tcp) -and (Test-LocalPortFree -Port ($candidate + 1) -Protocol Udp) -and
                (Test-LocalPortFree -Port ($candidate + 2) -Protocol Udp)) { $base = $candidate }
        }
        Assert-True ($base -ne 0) 'free fixture ports'
        $dryJson = Get-Content -LiteralPath (Join-Path $projectRoot 'tools/lab/lab.json') -Raw | ConvertFrom-Json
        $dryJson.paths.hostDir = $dryHost; $dryJson.paths.guestDir = $dryGuest
        $dryJson.paths.fuseSource = (Join-Path $dry 'fuse'); $dryJson.paths.fuseExe = (Join-Path $dry 'fuse/fuse.exe')
        $dryJson.paths.runDir = (Join-Path $dry 'lab'); $dryJson.paths.loader = $dryLoader; $dryJson.paths.runtimeArchive = $dryArchive
        $dryJson.paths | Add-Member -NotePropertyName steamRoot -NotePropertyValue $drySteam
        $dryJson.hashes.exe = Get-Sha256 (Join-Path $dryHost 'mgsvtpp.exe')
        $dryJson.hashes.steamApi = Get-Sha256 (Join-Path $dryHost 'steam_api64.dll')
        $dryJson.hashes.runtimeArchive = Get-Sha256 $dryArchive
        $dryJson.ports.fuse = $base; $dryJson.ports.host = $base + 1; $dryJson.ports.guest = $base + 2
        $dryJson.identity.hostSteamId = $dryHostId
        $dryJson.window.host.x = -6000; $dryJson.window.host.y = -6000
        $dryJson.window.guest.x = -5600; $dryJson.window.guest.y = -6000
        $dryJson.smoke.windowTimeoutSec = 30
        $dryConfig = Join-Path $dry 'lab.json'
        [IO.File]::WriteAllText($dryConfig, ($dryJson | ConvertTo-Json -Depth 8))
        $labDir = Join-Path $projectRoot 'tools/lab'
        $dryRun = $null
        try {
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Prepare succeeds: ' + ($out -join ' | '))
            $hp = Get-InstancePaths -GameDirectory $dryHost
            $gp = Get-InstancePaths -GameDirectory $dryGuest
            Assert-Equal (Get-Content -LiteralPath $gp.Record -Raw | ConvertFrom-Json).game_directory $dryGuest 'guest ownership record names the guest folder'
            Assert-Equal (Get-Sha256 $gp.Loader) (Get-Sha256 $dryLoader) 'guest loader installed'
            Assert-Equal ([IO.File]::ReadAllText($hp.Marker)) "host-v3`n" 'host marker'
            Assert-Equal ([IO.File]::ReadAllText($gp.Marker)) "client-v1`n" 'guest marker'
            Assert-Equal ([IO.File]::ReadAllText($gp.ClientHost)) "$dryHostId`n" 'guest client-host.txt = host SteamID64'
            Assert-Equal (ConvertFrom-IniText ([IO.File]::ReadAllText($gp.LabIni)))['peer_steamid'] $dryHostId 'guest lab.ini peer'
            Assert-Equal (ConvertFrom-IniText ([IO.File]::ReadAllText($hp.CoopIni)))['master_url'] "http://127.0.0.1:$base/tppstm/gate" 'coop.ini fuse url'
            $labGraphics = Join-Path $gp.SaveDir '287700/local/TPP_GRAPHICS_CONFIG'
            $g = Get-GraphicsConfigInfo -Text ([IO.File]::ReadAllText($labGraphics))
            Assert-True ($g.Valid -and $g.WindowMode -eq 'Windowed' -and $g.Width -eq 1280) 'lab graphics copy set to Windowed 1280x720 with a valid tag'
            Assert-Equal (Get-Sha256 $realGraphics) $realGraphicsHash 'the (fixture) real graphics config is untouched'
            Assert-True (Test-Path -LiteralPath (Join-Path $gp.SaveDir 'remote/TPP_GAME_DATA')) 'guest remote save copy seeded'
            $backups = @(Get-ChildItem -LiteralPath (Join-Path $hp.Mod 'install-backups') -Directory).Count
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig 2>&1
            Assert-Equal $LASTEXITCODE 0 'second Prepare succeeds'
            Assert-True (($out -join "`n").Contains('[host] loader already installed')) 'second Prepare is idempotent (loader)'
            Assert-Equal @(Get-ChildItem -LiteralPath (Join-Path $hp.Mod 'install-backups') -Directory).Count $backups 'no new install backup on a repeat'

            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Start-Lab.ps1') -ConfigPath $dryConfig 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Start-Lab succeeds: ' + ($out -join ' | '))
            $dryRun = (Get-Content -LiteralPath (Join-Path $dry 'lab/current-run.json') -Raw | ConvertFrom-Json).run_dir
            $runJson = Get-Content -LiteralPath (Join-Path $dryRun 'run.json') -Raw | ConvertFrom-Json
            Assert-True ($runJson.instances.host.pid -gt 0 -and $runJson.instances.guest.pid -gt 0 -and $runJson.fuse.pid -gt 0) 'run.json records the pids'
            Assert-Equal ($runJson.fuse.args -join ' ') "-standalone -listenAddr 127.0.0.1:$base -baseURL http://127.0.0.1:$base/ -writeLog" 'fuse started with -standalone'
            Assert-True (Test-Path -LiteralPath (Join-Path $dryRun 'real-saves-before.json')) 'real-save manifest at run start'

            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Invoke-LabSmoke.ps1') -ConfigPath $dryConfig -PollMs 200 -TimeoutSec 30 -ReadyTimeoutSec 60 2>&1
            Assert-Equal $LASTEXITCODE 0 ('smoke passes against the stand-ins: ' + ($out -join ' | '))
            $result = Get-Content -LiteralPath (Join-Path $dryRun 'result.json') -Raw | ConvertFrom-Json
            Assert-Equal $result.verdict 'pass' 'dry-run verdict'
            Assert-Equal $result.evidence.guest.final_status.session_status 'client_established' 'lab-status.json read with the expected pid'
            Assert-True ([int]$result.fuse_messages.CMD_GET_URLLIST -ge 1) 'fuse -writeLog message names counted'
            Assert-Equal @($result.screenshots).Count 2 'one screenshot per window'

            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Stop-Lab.ps1') -ConfigPath $dryConfig -TimeoutSec 10 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Stop-Lab succeeds with unchanged saves: ' + ($out -join ' | '))
            $stop = Get-Content -LiteralPath (Join-Path $dryRun 'stop.json') -Raw | ConvertFrom-Json
            Assert-Equal "$($stop.games.host.exit)/$($stop.games.guest.exit)/$($stop.fuse)" 'closed/closed/stopped' 'games closed by WM_CLOSE, fuse stopped'
            foreach ($id in $runJson.instances.host.pid, $runJson.instances.guest.pid, $runJson.fuse.pid) {
                Assert-True ($null -eq (Get-Process -Id $id -ErrorAction SilentlyContinue)) "process $id gone"
            }
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig -Disable 2>&1
            Assert-Equal $LASTEXITCODE 0 'Disable succeeds'
            Assert-True ((-not (Test-Path -LiteralPath $hp.LabIni)) -and (-not (Test-Path -LiteralPath $gp.CoopIni)) -and
                @(Get-ChildItem -LiteralPath $gp.Mod -Filter 'lab.ini.disabled-*').Count -eq 1) 'Disable renames lab.ini and coop.ini'
        } finally {
            if ($dryRun -and (Test-Path -LiteralPath (Join-Path $dryRun 'run.json'))) {
                $leftover = Get-Content -LiteralPath (Join-Path $dryRun 'run.json') -Raw | ConvertFrom-Json
                $ids = @($leftover.instances.host.pid, $leftover.instances.guest.pid)
                if ($leftover.fuse) { $ids += $leftover.fuse.pid }
                foreach ($id in $ids) {
                    $p = Get-Process -Id ([int]$id) -ErrorAction SilentlyContinue
                    if ($p -and (Test-PathInside -Child (Get-ProcessPathSafe $p) -Parent $dry)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
                }
            }
        }
    } else { Write-Warning 'lab_fake_process not supplied; full harness dry run skipped.' }

    Write-Output "$script:checks lab script checks passed; temporary fixtures only, no game, Steam or fuse started"
} finally {
    Remove-OwnedFixture
}
