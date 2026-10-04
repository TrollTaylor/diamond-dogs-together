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
    # REG_DWORD ActiveUser arrives as Int32; accounts from 2^31 up are negative there.
    Assert-Equal (ConvertFrom-RegistryDword ([int]97930372)) ([UInt32]97930372) 'registry dword: positive'
    Assert-Equal (ConvertFrom-RegistryDword ([int]-5)) ([UInt32]4294967291) 'registry dword: negative Int32 is an account >= 2^31'
    Assert-Equal (ConvertFrom-RegistryDword ([int]::MinValue)) ([UInt32]2147483648) 'registry dword: Int32 minimum'
    Assert-Equal (ConvertFrom-RegistryDword ([Int64]4294967295)) ([UInt32]4294967295) 'registry dword: Int64 in range'
    Assert-Equal (ConvertFrom-RegistryDword $null) ([UInt32]0) 'registry dword: absent'
    Assert-Throws { ConvertFrom-RegistryDword ([Int64]-1) } 'Not a 32-bit' 'registry dword: out of range'
    Assert-Equal (ConvertTo-SteamId64 -AccountId (ConvertFrom-RegistryDword ([int]-5))) '76561202255233019' 'large account id resolves'
    Assert-Equal (Resolve-SteamAccount -ActiveUser (ConvertFrom-RegistryDword ([int]-5))) ([UInt32]4294967291) 'large ActiveUser accepted'

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
    Assert-True ($hostIni.Contains('-Disable') -and $hostIni.Contains('even without this file') -and !$hostIni.Contains('Delete this file')) 'lab.ini header: deleting lab.ini alone keeps the coop.ini redirect; points to -Disable'
    Assert-Equal (Select-NewestUserBackup -FileName 'coop.ini' -Candidates @('coop.ini.user-20261001-120000', 'coop.ini.user-20261003-090000', 'coop.ini.user-20261002-235959', 'lab.ini.user-20261009-000000', 'coop.ini.user-x', 'coop.ini.disabled-20261010-000000')) 'coop.ini.user-20261003-090000' 'newest user backup'
    Assert-True ($null -eq (Select-NewestUserBackup -FileName 'lab.ini' -Candidates @('coop.ini.user-20261001-120000'))) 'no user backup'
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
    Assert-True ((Test-LabGeneratedText $coopText) -and $coopText.Contains('applies even without lab.ini') -and $coopText.Contains('-Disable')) 'coop.ini header explains the lingering redirect'
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
        "install_result=0 installed=1 pid=4 thread=5 tick_ms=6 target_rva=0x1a178e0 lifetime=process`r`n" +
        "dynamite_host=returned; result=1 thread=83844 tick_ms=1 peer_acceptance_requested=0`r`n" +
        "host_admission=authorized; owned_host=1 same_thread=1 lua_interface=SessionEnableAccept`r`n" +
        "dynamite_session=observed; owner=0000000000000001 session=0000000000000002 manager=0000000000000003 job=0000000000000004 session_read=1 state=3 accept=1 manager_heap=1 job_heap=1 consistent=1 calls=0 writes=0 atomic=0`r`n" +
        "session_status=host_ready`r`n"
    $probeOk = "MGS Co-op loader probe 0.11.0`r`narchitecture=x64`r`ntime_utc=2026-10-03T10:00:00Z`r`nDirectInput8Create_hresult=0x00000000`r`n"
    $guestArmed = "mode=client_v1; configured_host=1; join=ctrl_f8_after_mission_start; max_players=2; connected=unverified`r`nclient_join=armed; source=trigger_file`r`n"
    $guestStarted = $guestArmed + "client_join=input_received; key=trigger_file`r`ndynamite_client=returned; request_started=1`r`nsession_status=client_requested`r`n"
    $labId = '76561202255233008'
    $fuseUrl = 'http://127.0.0.1:6667/tppstm/gate'
    $hostSave = 'D:\Lab\MGS_TPP\mgs-coop\lab\saves'
    $guestSave = 'D:\Lab\MGS_TPP_guest\mgs-coop\lab\saves'
    $hostEarly = "early=start; version=0.11.0; lab=1; role=host`r`nmutex=unchanged`r`nidentity=real`r`nsaves=redirected; dir=$hostSave`r`nxinput=none`r`n"
    $guestEarly = "early=start; version=0.11.0; lab=1; role=guest`r`nmutex=renamed; suffix=-guest`r`nidentity=fake; steamid=$labId`r`nsaves=redirected; dir=$guestSave`r`nxinput=pad 0`r`n"
    $steamOk = "`r`nMGS Co-op original Steam import observer 0.4.0; fingerprint=accepted`r`nmaster_server=redirected; method=in_place; url=$fuseUrl`r`n"
    function Get-HostSafetyFor([string]$Early = $hostEarly, [string]$Steam = $steamOk) {
        Get-LabSafetyVerdict -Role host -EarlyLog $Early -SteamLog $Steam -ExpectedSaveDir $hostSave -ExpectedUrl $fuseUrl
    }
    function Get-GuestSafetyFor([string]$Early = $guestEarly, [string]$Steam = $steamOk) {
        Get-LabSafetyVerdict -Role guest -EarlyLog $Early -SteamLog $Steam -ExpectedSaveDir $guestSave -ExpectedUrl $fuseUrl -ExpectedSteamId $labId -ExpectedMutexSuffix '-guest'
    }
    function Get-SafetyText($Verdict) { "$($Verdict.State)/$($Verdict.Reason)/$(@($Verdict.Missing) -join ',')" }

    # Safety evidence (docs/lab-protocol.md safety rules).
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor)) 'ok//' 'host safety ok'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor)) 'ok//' 'guest safety ok'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early '' -Steam '')) 'pending//saves,master_server' 'host: nothing written yet'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early '' -Steam '')) 'pending//saves,mutex,identity,master_server' 'guest: nothing written yet'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam '')) 'pending//master_server' 'redirect line not written yet'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace("saves=redirected; dir=$hostSave", 'saves=unchanged'))) 'unsafe/saves_unchanged/' 'saves=unchanged is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace($hostSave, 'C:\Program Files (x86)\Steam\userdata\7'))) 'unsafe/saves_wrong_dir/' 'redirect to another folder is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace("dir=$hostSave", 'dir='))) 'unsafe/saves_wrong_dir/' 'empty save dir is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace("; dir=$hostSave", ''))) 'unsafe/saves_wrong_dir/' 'redirect without a dir is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace($hostSave, 'd:/lab/mgs_tpp/MGS-COOP/lab/saves/'))) 'ok//' 'save dir compare ignores case, slash style and a trailing separator'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace($hostSave, '\\?\' + $hostSave))) 'ok//' 'save dir compare ignores a \\?\ prefix'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early ($hostEarly + "saves=redirected; dir=D:\elsewhere`r`n"))) 'unsafe/saves_wrong_dir/' 'any wrong redirect line is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace('lab=1', 'lab=0'))) 'unsafe/lab_disabled/' 'lab=0 is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace('role=host', 'role=guest'))) 'unsafe/role_mismatch/' 'role mismatch is unsafe'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace('mutex=renamed; suffix=-guest', 'mutex=unchanged'))) 'unsafe/mutex_unchanged/' 'guest mutex unchanged'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace('suffix=-guest', 'suffix=-other'))) 'unsafe/mutex_wrong_suffix/' 'guest mutex suffix differs'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace('mutex=renamed; suffix=-guest', 'mutex=renamed'))) 'unsafe/mutex_wrong_suffix/' 'guest mutex without suffix'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace("identity=fake; steamid=$labId", 'identity=real'))) 'unsafe/identity_real/' 'guest real identity is unsafe'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace($labId, '76561197960265729'))) 'unsafe/identity_wrong_id/' 'guest with another id is unsafe'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace("; steamid=$labId", ''))) 'unsafe/identity_wrong_id/' 'guest fake identity without an id'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace('identity=real', "identity=fake; steamid=$labId"))) 'ok//' 'host identity line is evidence only'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam "master_server=refused; reason=instruction_bytes`r`n")) 'unsafe/master_server_refused/' 'redirect refused is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam "master_server=failed; reason=verify; method=in_place`r`n")) 'unsafe/master_server_failed/' 'redirect failure is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam "master_server=refused; reason=invalid_url; detail=bad_suffix`r`n")) 'unsafe/master_server_refused/' 'invalid url refusal is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam ($steamOk + "master_server=refused; reason=already_redirected`r`n"))) 'ok//' 'a later refusal of another URL keeps the verified redirect'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam $steamOk.Replace('6667', '6668'))) 'unsafe/master_server_wrong_url/' 'redirect to another URL is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam "master_server=redirected; method=in_place`r`n")) 'unsafe/master_server_wrong_url/' 'redirect line without url'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam "xmaster_server=refused; reason=x`r`n")) 'pending//master_server' 'prefix anchored at line start'
    Assert-Throws { Get-LabSafetyVerdict -Role guest -ExpectedSaveDir $guestSave -ExpectedUrl $fuseUrl } 'ExpectedSteamId' 'guest checks need the expected id'
    Assert-Equal (Get-InstanceSafety -Role guest -Expectation ([pscustomobject]@{ SaveDir = $guestSave; Url = $fuseUrl; SteamId = ''; MutexSuffix = '-guest' }) -EarlyLog $guestEarly -SteamLog $steamOk).Reason 'no_expectation' 'missing expectation is unsafe'
    $hostSafety = Get-HostSafetyFor
    $guestSafety = Get-GuestSafetyFor

    Assert-True (Test-LogLine $hostReady 'dynamite_host=returned; result=1') 'prefix followed by space'
    Assert-True (-not (Test-LogLine "dynamite_host=returned; result=10`r`n" 'dynamite_host=returned; result=1')) 'result=10 is not result=1'
    Assert-True (-not (Test-LogLine "xsession_status=host_ready`n" 'session_status=host_ready')) 'prefix anchored at line start'
    Assert-True (-not (Test-LogLine "session_status=host_ready_extra`n" 'session_status=host_ready')) 'value must end'
    Assert-True (Test-LogLine 'session_status=host_ready' 'session_status=host_ready') 'end of text counts'
    Assert-Equal (Get-LastSessionStatus "session_status=host_ready`nsession_status=host_peer_present`n") 'host_peer_present' 'last session status'
    Assert-Equal (Get-FirstLogLine $hostReady 'host_admission=authorized') 'host_admission=authorized; owned_host=1 same_thread=1 lua_interface=SessionEnableAccept' 'first line evidence'

    $fuseBoth = @{ CMD_GET_URLLIST = 2; CMD_AUTH_STEAMTICKET = 2 }
    $fuseOne = @{ CMD_GET_URLLIST = 1; CMD_AUTH_STEAMTICKET = 1 }
    function Get-V([hashtable]$Override = @{}) {
        $arguments = @{ HostLog = $hostReady; GuestLog = ''; HostProbeLog = $probeOk; HostSafety = $hostSafety; GuestSafety = $guestSafety; FuseCounts = $fuseBoth }
        foreach ($key in $Override.Keys) { $arguments[$key] = $Override[$key] }
        Get-LabSmokeVerdict @arguments
    }
    function Get-VText($Verdict) { "$($Verdict.Verdict)/$($Verdict.Phase)/$($Verdict.Reason)" }

    $v = Get-LabSmokeVerdict
    Assert-Equal "$($v.Verdict)/$($v.Phase)" 'pending/waiting_safety_evidence' 'nothing yet: no safety evidence'
    Assert-Equal (Get-VText (Get-V @{ GuestSafety = $null })) 'pending/waiting_safety_evidence/' 'guest safety unknown blocks readiness'
    $v = Get-V @{ HostSafety = (Get-HostSafetyFor -Steam ''); GuestSafety = (Get-GuestSafetyFor -Early '') }
    Assert-Equal (@($v.Missing) -join ',') 'host:master_server,guest:saves,guest:mutex,guest:identity' 'missing safety evidence listed'
    $v = Get-V @{ HostSafety = (Get-HostSafetyFor -Early ''); ReadyElapsedSec = 900 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/timeout_waiting_safety_evidence' 'missing save evidence times out'
    $v = Get-V @{ HostSafety = (Get-HostSafetyFor -Steam ''); GuestSafety = (Get-GuestSafetyFor -Steam ''); ReadyElapsedSec = 900 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/not_under_fuse' 'missing master_server redirect at the timeout = not_under_fuse'
    $v = Get-V @{ HostLog = '' }
    Assert-Equal "$($v.Phase)/$($v.HostReady)" 'waiting_host_ready/False' 'host prerequisites needed'
    foreach ($line in @('mode=host_v3', 'install_result=0 installed=1', 'dynamite_host=returned; result=1', 'host_admission=authorized', 'session_status=host_ready')) {
        $without = (($hostReady -split "`r`n") | Where-Object { !$_.StartsWith($line) }) -join "`r`n"
        $v = Get-V @{ HostLog = $without }
        Assert-True ($v.Phase -eq 'waiting_host_ready' -and (@($v.Missing) -contains "host:$line")) "host prerequisite required: $line"
    }
    $v = Get-V @{ HostLog = $hostReady.Replace('state=3 accept=1', 'state=2 accept=1') }
    Assert-True (@($v.Missing) -contains 'host:dynamite_session=observed; state=3 accept=1 consistent=1') 'dynamite_session state=3 accept=1 required'
    $v = Get-V @{ HostLog = $hostReady.Replace('consistent=1', 'consistent=0') }
    Assert-True (@($v.Missing) -contains 'host:dynamite_session=observed; state=3 accept=1 consistent=1') 'dynamite_session consistent=1 required'
    $v = Get-V @{ HostProbeLog = $probeOk.Replace('0x00000000', '0x80004005') }
    Assert-True (@($v.Missing) -contains 'host:DirectInput8Create_hresult=0x00000000') 'DirectInput8Create success required'
    Assert-True (-not (Get-V @{ HostLog = $hostReady.Replace('host_admission=authorized', 'host_admission=refused') }).HostReady) 'admission required'
    $v = Get-V @{ FuseCounts = @{} }
    Assert-Equal "$($v.Phase)/$($v.HostReady)" 'waiting_host_fuse/False' 'host readiness needs fuse traffic'
    Assert-Equal (Get-V @{ FuseCounts = @{}; ReadyElapsedSec = 900 }).Reason 'not_under_fuse' 'no fuse traffic at the timeout = not_under_fuse'
    Assert-Equal (Get-V @{ FuseCounts = @{ CMD_GET_URLLIST = 1 } }).Phase 'waiting_host_fuse' 'auth through fuse required'
    $v = Get-V
    Assert-Equal "$($v.Verdict)/$($v.Phase)/$($v.HostReady)" 'pending/waiting_guest_armed/True' 'host ready'
    $v = Get-V @{ GuestLog = $guestArmed.Replace('mode=client_v1; configured_host=1', 'mode=client_v1; refused=x') }
    Assert-True ($v.Phase -eq 'waiting_guest_armed' -and (@($v.Missing) -contains 'guest:mode=client_v1; configured_host=1')) 'guest configured_host required'
    $v = Get-V @{ GuestLog = $guestArmed; FuseCounts = $fuseOne }
    Assert-Equal $v.Phase 'waiting_guest_fuse' 'full run needs fuse traffic from both instances'
    Assert-Equal (Get-V @{ GuestLog = $guestArmed; FuseCounts = $fuseOne; ReadyElapsedSec = 900 }).Reason 'not_under_fuse' 'guest never reached fuse'
    $v = Get-V @{ GuestLog = $guestArmed }
    Assert-Equal "$($v.Phase)/$($v.ShouldTrigger)" 'ready_to_trigger/True' 'trigger when both ready'
    $v = Get-V @{ GuestLog = $guestArmed; TriggerCreated = $true }
    Assert-Equal "$($v.Phase)/$($v.ShouldTrigger)" 'waiting_request_started/False' 'no second trigger'
    $v = Get-V @{ GuestLog = $guestStarted; TriggerCreated = $true }
    Assert-Equal "$($v.Verdict)/$($v.Phase)" 'pending/waiting_established' 'request started'
    $hostPeer = $hostReady + "session_status=host_peer_present`r`n"
    $guestEstablished = $guestStarted + "session_status=client_established`r`n"
    $v = Get-V @{ HostLog = $hostPeer; GuestLog = $guestEstablished; TriggerCreated = $true }
    Assert-Equal "$($v.Verdict)/$($v.PassPolls)" 'pending/1' 'first established poll'
    $v = Get-V @{ HostLog = $hostPeer; GuestLog = $guestEstablished; TriggerCreated = $true; PreviousPassPolls = 1 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'pass/established_stable' 'two consecutive polls pass'
    $v = Get-V @{ HostLog = $hostReady; GuestLog = $guestEstablished; TriggerCreated = $true; PreviousPassPolls = 1 }
    Assert-Equal "$($v.Verdict)/$($v.PassPolls)" 'pending/0' 'host peer missing resets the count'
    $guestStatus = ConvertFrom-LabStatusText -Text '{"version":1,"pid":77,"role":"guest","session_status":"client_established","join":"established"}' -ExpectedPid 77
    $hostStatus = ConvertFrom-LabStatusText -Text '{"version":1,"pid":55,"role":"host","session_status":"host_peer_present","join":"idle"}' -ExpectedPid 55
    $v = Get-V @{ GuestLog = $guestStarted; HostStatus = $hostStatus; GuestStatus = $guestStatus; TriggerCreated = $true; PreviousPassPolls = 1 }
    Assert-Equal $v.Verdict 'pass' 'lab-status.json drives the session state'
    Assert-True ($null -eq (ConvertFrom-LabStatusText -Text '{"pid":1,"session_status":"none"}' -ExpectedPid 2)) 'status from another process ignored'
    Assert-True ($null -eq (ConvertFrom-LabStatusText -Text '{"pid":1,' -ExpectedPid 1)) 'partial json ignored'
    Assert-True ($null -eq (ConvertFrom-LabStatusText -Text '{"pid":1}' -ExpectedPid 1)) 'json without session_status ignored'
    $failures = @(
        @{ Guest = $guestArmed + "dynamite_client=refused; reason=x`r`n"; Reason = 'dynamite_client_refused' },
        @{ Guest = $guestArmed + "dynamite_client=returned; request_started=0`r`n"; Reason = 'request_started_0' },
        @{ Guest = $guestArmed + "client_join=refused; reason=placeholder_host_id`r`n"; Reason = 'client_join_refused' },
        @{ Guest = "mode=client_v1; refused=missing_or_invalid_client_host_txt; hooks=0 game_calls=0`r`n"; Reason = 'missing_or_invalid_client_host_txt' },
        @{ Guest = $guestStarted + "session_status=session_error`r`n"; Reason = 'guest_session_error' }
    )
    foreach ($case in $failures) {
        $v = Get-V @{ GuestLog = $case.Guest; TriggerCreated = $true }
        Assert-Equal "$($v.Verdict)/$($v.Reason)" "fail/$($case.Reason)" "failure rule $($case.Reason)"
    }
    Assert-Equal (Get-V @{ HostLog = ($hostReady + "session_status=session_error`n") }).Reason 'host_session_error' 'host session error'
    Assert-Equal (Get-V @{ HostLog = "dynamite_host=returned; result=0 thread=1`n" }).Reason 'host_creation_failed' 'host creation failed'
    Assert-Equal (Get-V @{ HostAlive = $false }).Reason 'process_exit_host' 'host exit'
    Assert-Equal (Get-V @{ GuestAlive = $false }).Reason 'process_exit_guest' 'guest exit'
    Assert-Equal (Get-V @{ HostSafety = (Get-HostSafetyFor -Early $hostEarly.Replace("saves=redirected; dir=$hostSave", 'saves=unchanged')) }).Reason 'unsafe_host_saves_unchanged' 'unsafe host fails at once'
    Assert-Equal (Get-V @{ GuestSafety = (Get-GuestSafetyFor -Early $guestEarly.Replace("identity=fake; steamid=$labId", 'identity=real')) }).Reason 'unsafe_guest_identity_real' 'unsafe guest fails at once'
    Assert-Equal (Get-V @{ HostSafety = (Get-HostSafetyFor -Steam "master_server=failed; reason=verify; method=in_place`r`n"); HostOnly = $true }).Reason 'unsafe_host_master_server_failed' 'unsafe host fails host-only runs'
    Assert-Equal (Get-V @{ HostIdCheck = 'mismatch' }).Reason 'host_id_mismatch' 'host id mismatch fails at once'
    Assert-Equal (Get-V @{ HostIdCheck = 'ok' }).Verdict 'pending' 'matching host id'
    Assert-Equal (Get-V @{ GuestAlive = $false; GuestSafety = $null; HostIdCheck = 'mismatch'; HostOnly = $true; FuseCounts = $fuseOne }).Verdict 'pending' 'guest ignored in host-only runs'
    $v = Get-V @{ ReadyElapsedSec = 900; ReadyTimeoutSec = 900 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/timeout_waiting_guest_armed' 'readiness timeout'
    $v = Get-V @{ GuestLog = $guestStarted; TriggerCreated = $true; ReadyElapsedSec = 5000; TriggerElapsedSec = 179; TimeoutSec = 180 }
    Assert-Equal $v.Verdict 'pending' 'readiness timeout does not apply after the trigger'
    $v = Get-V @{ GuestLog = $guestStarted; TriggerCreated = $true; TriggerElapsedSec = 180; TimeoutSec = 180 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/timeout_waiting_established' 'session timeout after the trigger'
    # Host-only (M0): passes only with the host prerequisites AND evidence of fuse.
    $v = Get-V @{ HostOnly = $true; GuestSafety = $null; FuseCounts = $fuseOne }
    Assert-Equal "$($v.Verdict)/$($v.PassPolls)" 'pending/1' 'host-only first poll'
    $v = Get-V @{ HostOnly = $true; GuestSafety = $null; FuseCounts = $fuseOne; PreviousPassPolls = 1 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'pass/host_ready_stable' 'host-only pass (M0) under fuse'
    $v = Get-V @{ HostOnly = $true; GuestSafety = $null; FuseCounts = @{}; PreviousPassPolls = 1 }
    Assert-Equal "$($v.Verdict)/$($v.Phase)/$($v.PassPolls)" 'pending/waiting_host_fuse/0' 'host-only without fuse traffic never passes'
    $v = Get-V @{ HostOnly = $true; GuestSafety = $null; FuseCounts = @{}; ReadyElapsedSec = 900 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/not_under_fuse' 'host-only on Konami (no fuse traffic) reports not_under_fuse'
    $v = Get-V @{ HostOnly = $true; GuestSafety = $null; HostSafety = (Get-HostSafetyFor -Steam ''); ReadyElapsedSec = 900 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/not_under_fuse' 'host-only without the master_server redirect reports not_under_fuse'

    # Host identity cross-check (host-steamid.txt vs client-host.txt / peer_steamid).
    $realHost = '76561197960265729'
    Assert-Equal (Get-HostIdCheck -HostIdText "$realHost`n" -ClientHostText "$realHost`n" -PeerSteamId $realHost) 'ok' 'host id ok'
    Assert-Equal (Get-HostIdCheck -HostIdText "$realHost`n" -ClientHostText "$realHost`n" -PeerSteamId 'learn') 'ok' 'peer learn ignored'
    Assert-Equal (Get-HostIdCheck -HostIdText '' -ClientHostText "$realHost`n" -PeerSteamId $realHost) 'not_written' 'host id not written yet'
    Assert-Equal (Get-HostIdCheck -HostIdText '76561197960265730' -ClientHostText $realHost -PeerSteamId $realHost) 'mismatch' 'client-host differs'
    Assert-Equal (Get-HostIdCheck -HostIdText $realHost -ClientHostText $realHost -PeerSteamId '76561197960265730') 'mismatch' 'peer_steamid differs'
    Assert-Equal (Get-HostIdCheck -HostIdText 'garbage' -ClientHostText $realHost -PeerSteamId $realHost) 'mismatch' 'invalid host id'
    $freshFile = Join-Path $fixture 'host-steamid.txt'
    [IO.File]::WriteAllText($freshFile, "$realHost`n")
    Assert-Equal (Read-FreshText -Path $freshFile -NotBeforeUtc ([DateTime]::UtcNow.AddMinutes(-5))) "$realHost`n" 'fresh host-steamid.txt read'
    Assert-Equal (Read-FreshText -Path $freshFile -NotBeforeUtc ([DateTime]::UtcNow.AddMinutes(5).ToString('o'))) '' 'file older than the host process ignored'
    Assert-Equal (Read-FreshText -Path (Join-Path $fixture 'absent.txt') -NotBeforeUtc $null) '' 'absent file'

    # Redaction: run folders are shareable.
    $sample = "time=1 msg=CMD_AUTH_STEAMTICKET steamID=$realHost`nloopback=listening; port=47810; peer_port=47811; self=$realHost`n" +
        "identity=fake; steamid=$labId`nsource_root=C:\Steam\userdata\97930372\287700 and C:/Steam/userdata/97930372`n" +
        "msg=CMD_CREATE_PLAYER player_name=`"Big Boss`"`nmsg=x player_name=Solo`nother=12345678901234567 clan=103582791429521408`n"
    $redacted = Hide-SteamIds -Text $sample -Keep @($labId)
    Assert-True (!$redacted.Contains($realHost) -and !$redacted.Contains('97930372') -and !$redacted.Contains('Big Boss') -and !$redacted.Contains('Solo')) 'real ids, account folders and player names removed'
    Assert-True ($redacted.Contains("steamid=$labId") -and $redacted.Contains('steamID=<steamid>') -and $redacted.Contains('userdata\<account>') -and $redacted.Contains('userdata/<account>')) 'lab id kept, placeholders written'
    Assert-True ($redacted.Contains('12345678901234567') -and $redacted.Contains('103582791429521408')) 'numbers that are not individual SteamIDs kept'
    Assert-True ((Hide-SteamIds -Text $sample).Contains('steamid=<steamid>')) 'without Keep the lab id is redacted too'

    # fuse console output and counts.
    $console = "time=1 level=INFO msg=`"client request handled`" type=CMD_GET_URLLIST ip=127.0.0.1:5000`n" +
        "time=2 level=INFO msg=CMD_AUTH_STEAMTICKET steamID=$realHost`n" +
        "time=3 level=INFO msg=`"client request handled`" type=CMD_AUTH_STEAMTICKET ip=127.0.0.1:5000`n" +
        "time=4 level=INFO msg=CMD_AUTH_STEAMTICKET steamID=$labId`n" +
        "time=5 level=INFO msg=`"client request handled`" type=CMD_AUTH_STEAMTICKET ip=127.0.0.1:5001`n" +
        "time=6 level=INFO msg=CMD_AUTH_STEAMTICKET steamID=$realHost`n"
    $fs = Get-FuseConsoleSummary -Text $console -LabSteamId $labId
    Assert-Equal "$($fs.Counts.CMD_GET_URLLIST)/$($fs.Counts.CMD_AUTH_STEAMTICKET)/$($fs.DistinctAuthIds)/$($fs.LabIdSeen)/$($fs.OtherAuthIds)" '1/2/2/True/1' 'fuse console summary'
    Assert-True (-not (Get-FuseConsoleSummary -Text $console.Replace($labId, $realHost) -LabSteamId $labId).LabIdSeen) 'lab id not seen when the guest presented the real id'
    $merged = Merge-FuseCounts @{ CMD_GET_URLLIST = 2; CMD_X = 1 } $fs.Counts
    Assert-Equal "$($merged.CMD_GET_URLLIST)/$($merged.CMD_AUTH_STEAMTICKET)/$($merged.CMD_X)" '2/2/1' 'fuse counts merged by maximum'
    Assert-Equal (Get-CountValue $null 'CMD_GET_URLLIST') 0 'count of nothing'

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
    # Windows PowerShell 5.1 (friend package) has no [IO.Path]::GetRelativePath.
    Assert-Equal (Get-RelativeChildPath -Root 'C:\a\b\' -Path 'C:\a\b\c\d.txt') 'c/d.txt' 'relative child path'
    Assert-Equal (Get-RelativeChildPath -Root 'c:/A/B' -Path 'C:\a\b\x') 'x' 'relative child path: separators and case'
    Assert-Throws { Get-RelativeChildPath -Root 'C:\a\b' -Path 'C:\a\bc\d.txt' } 'not below' 'a sibling with the same prefix is not a child'
    Assert-Throws { Get-RelativeChildPath -Root 'C:\a\b' -Path 'C:\a\b' } 'not below' 'the root itself is not a child'
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
    # Stop-Lab only terminates this run's own processes.
    $t0 = [DateTime]::UtcNow
    Assert-True (Test-RunProcessIdentity -ActualStartUtc $t0 -RecordedStartUtc $t0.AddMilliseconds(400).ToString('o')) 'recorded start time matches within 1 s'
    Assert-True (-not (Test-RunProcessIdentity -ActualStartUtc $t0 -RecordedStartUtc $t0.AddSeconds(5))) 'other start time: not this run''s process'
    Assert-True (Test-RunProcessIdentity -ActualStartUtc $t0 -NotBeforeUtc $t0.AddSeconds(-30) -NotAfterUtc $t0.AddSeconds(1)) 'older run.json: inside the launch window'
    Assert-True (-not (Test-RunProcessIdentity -ActualStartUtc $t0.AddHours(2) -NotBeforeUtc $t0.AddSeconds(-30) -NotAfterUtc $t0.AddSeconds(1))) 'older run.json: started hours later = a normal session'
    Assert-True (-not (Test-RunProcessIdentity -ActualStartUtc $null -RecordedStartUtc $t0)) 'unknown start time not verified'
    Assert-True (-not (Test-RunProcessIdentity -ActualStartUtc $t0)) 'nothing recorded: not verified'
    $recordedRows = @(
        [pscustomobject]@{ Role = 'host'; Id = 10; Alive = $true; PathMatches = $true; IdentityVerified = $true },
        [pscustomobject]@{ Role = 'guest'; Id = 11; Alive = $true; PathMatches = $true; IdentityVerified = $false },
        [pscustomobject]@{ Role = 'guest'; Id = 12; Alive = $false; PathMatches = $false; IdentityVerified = $false })
    $foundRows = @([pscustomobject]@{ Role = 'host'; Id = 10 }, [pscustomobject]@{ Role = 'guest'; Id = 11 }, [pscustomobject]@{ Role = 'host'; Id = 20 })
    $plan = @(Select-LabStopTargets -Recorded $recordedRows -Found $foundRows)
    Assert-Equal (@($plan | ForEach-Object { "$($_.Id):$($_.Action)" }) -join ',') '10:close_then_terminate,11:leave,20:leave' 'only the verified run process may be terminated'
    $plan = @(Select-LabStopTargets -Recorded $recordedRows -Found $foundRows -IncludeUnrecorded)
    Assert-Equal (@($plan | ForEach-Object { "$($_.Id):$($_.Action)" }) -join ',') '10:close_then_terminate,11:close_only,20:close_only' 'unrecorded processes: WM_CLOSE only'
    Assert-Equal @(Select-LabStopTargets).Count 0 'nothing to stop'
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
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $stage1 -LoaderPath $loader1 -Version '0.11.0' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 $archiveHash
    $package = Get-Content -LiteralPath (Join-Path $stage1 'package.json') -Raw | ConvertFrom-Json
    Assert-Equal @($package.files).Count 5 'package has no runtime archive payload'
    Assert-True (-not (@($package.files.target) -contains 'mgs-coop/mgs-coop-player-runtime.dat')) 'archive referenced by hash only'
    Assert-Equal $package.runtime_archive.sha256 $archiveHash 'archive hash recorded'
    Assert-InFixture $hostGame
    Assert-Throws { & $installer -Action Install -GameDirectory $hostGame -PackageDirectory $stage1 -NoPrompt } 'does not contain game-derived files' 'archive required on a fresh install'
    # A non-console caller (redirected input) is never prompted; it gets the explanation.
    $childOut = ('' | & (Get-Process -Id $PID).Path -NoProfile -File $installer -Action Install -GameDirectory $hostGame -PackageDirectory $stage1 2>&1 | Out-String)
    $childExit = $LASTEXITCODE
    $childOut = $childOut -replace '\s*\r?\n\s*\|?\s*', ' '  # undo the error view's line wrapping
    Assert-True ($childExit -ne 0 -and $childOut.Contains('install.bat asks for it') -and !$childOut.Contains('No runtime archive path')) 'redirected input: no prompt, clear error'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $hostGame 'dinput8.dll'))) 'nothing installed without the archive'
    $null = & $installer -Action Install -GameDirectory $hostGame -PackageDirectory $stage1 -RuntimeArchive $archiveSource -NoPrompt
    $hostRecord = Get-Content -LiteralPath (Join-Path $hostGame 'mgs-coop/probe-install.json') -Raw | ConvertFrom-Json
    Assert-Equal $hostRecord.game_directory $hostGame 'host record names the host folder'
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'dinput8.dll')) (Get-Sha256 $loader1) 'host loader installed'
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'mgs-coop/mgs-coop-player-runtime.dat')) $archiveHash 'host archive installed'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $hostGame 'mgs-coop/script-observe.enabled'))) "client-check-v1`n" 'first install defaults to diagnostics'
    # Upgrade reuses the installed archive.
    $stage2 = Join-Path $fixture 'stage2'
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $stage2 -LoaderPath $loader2 -Version '0.11.0' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 $archiveHash
    $null = & $installer -Action Install -GameDirectory $hostGame -PackageDirectory $stage2 -NoPrompt
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'dinput8.dll')) (Get-Sha256 $loader2) 'upgrade without -RuntimeArchive reuses the installed archive'
    # The guest folder starts as a copy of the host folder (record names the host).
    $guestGame = Join-Path $games 'MGS_TPP_guest'
    Assert-InFixture $guestGame
    $null = New-Item -ItemType Directory -Path $guestGame -Force
    Copy-Item -Path (Join-Path $hostGame '*') -Destination $guestGame -Recurse
    Assert-Throws { & $installer -Action Install -GameDirectory $guestGame -PackageDirectory $stage1 -NoPrompt } 'AcceptRecordCopiedFrom' 'copied record refused by default'
    Assert-Throws { & $installer -Action Install -GameDirectory $guestGame -PackageDirectory $stage1 -AcceptRecordCopiedFrom $guestGame -NoPrompt } 'ownership' 'copied-from must be the recorded folder'
    $null = & $installer -Action Install -GameDirectory $guestGame -PackageDirectory $stage1 -AcceptRecordCopiedFrom $hostGame -NoPrompt
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
    # set-session-mode validates the host id before touching the folder; account 0 is refused.
    $sessionMode = Join-Path $projectRoot 'tools/set-session-mode.ps1'
    Assert-Throws { & $sessionMode -Role Join -HostSteamId '76561197960265728' -GameDirectory $hostGame } 'Supply the host player' 'set-session-mode: account 0 refused'
    Assert-Throws { & $sessionMode -Role Join -HostSteamId '7656119796026572' -GameDirectory $hostGame } 'Supply the host player' 'set-session-mode: 16 digits refused'
    Assert-Throws { & $sessionMode -Role Join -HostSteamId '76561197960265729' -GameDirectory (Join-Path $fixture 'no-such-folder') } 'Cannot find path' 'set-session-mode: a valid id passes the identity check'
    Assert-Throws { & $sessionMode -Role Host -HostSteamId '76561197960265729' -GameDirectory $hostGame } 'only with -Role Join' 'set-session-mode: id only with Join'

    # ------------------------------------------------------------ end to end on a simulated run (no game)
    # Two idle pwsh processes stand in for the game instances; a small simulator
    # plays the native side (consumes join.trigger, appends the protocol lines).
    function Get-PrivateLeaks([string]$Dir, [string[]]$Secrets) {
        # Files in a run folder that contain any of $Secrets (raw or JSON-escaped).
        $forms = @($Secrets | Where-Object { $_ } | ForEach-Object { $_; $_.Replace('\', '\\'); $_.Replace('\', '/') } | Select-Object -Unique)
        @(Get-ChildItem -LiteralPath $Dir -File -Recurse | Where-Object { $_.Extension -ne '.png' } | Where-Object {
                $text = [IO.File]::ReadAllText($_.FullName)
                @($forms | Where-Object { $text.Contains($_) }).Count -gt 0
            } | ForEach-Object { $_.Name })
    }
    $pwshPath = (Get-Process -Id $PID).Path
    $e2e = Join-Path $fixture 'e2e'
    $e2eHost = Join-Path $e2e 'MGS_TPP'
    $e2eGuest = Join-Path $e2e 'MGS_TPP_guest'
    foreach ($d in $e2eHost, $e2eGuest) { $null = New-Item -ItemType Directory -Path (Join-Path $d 'mgs-coop') -Force }
    $e2eHostPaths = Get-InstancePaths -GameDirectory $e2eHost
    $e2eGuestPaths = Get-InstancePaths -GameDirectory $e2eGuest
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
    # This run's safety evidence, probe line and loopback lines (whole files belong to the run).
    $guestEarlyPath = Join-Path $e2eGuestPaths.Mod 'early.log'
    [IO.File]::WriteAllText((Join-Path $e2eHostPaths.Mod 'early.log'), $hostEarly.Replace($hostSave, $e2eHostPaths.SaveDir))
    [IO.File]::WriteAllText($guestEarlyPath, $guestEarly.Replace($guestSave, $e2eGuestPaths.SaveDir))
    foreach ($m in $e2eHostPaths.Mod, $e2eGuestPaths.Mod) { [IO.File]::WriteAllText((Join-Path $m 'steam-observer.log'), $steamOk) }
    [IO.File]::WriteAllText((Join-Path $e2eHostPaths.Mod 'probe.log'), $probeOk)
    [IO.File]::WriteAllText((Join-Path $e2eHostPaths.Mod 'loopback.log'), "loopback=listening; port=47810; peer_port=47811; self=$realHost`r`n")
    [IO.File]::WriteAllText((Join-Path $e2eGuestPaths.Mod 'loopback.log'), "loopback=listening; port=47811; peer_port=47810; self=$labId`r`nloopback=hello; peer=$realHost`r`n")
    [IO.File]::WriteAllText($e2eGuestPaths.ClientHost, "$realHost`n")
    [IO.File]::WriteAllText($e2eGuestPaths.LabIni, "peer_steamid=$realHost`n")
    # fuse: two logins in its -writeLog files and console log (kept outside the run folder).
    $e2eFuse = Join-Path $e2e 'lab/fuse'
    $since = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() - 5
    $fuseLogDir = Join-Path $e2eFuse ('log/' + ($since + 1))
    $null = New-Item -ItemType Directory -Path $fuseLogDir -Force
    foreach ($n in '1_CMD_GET_URLLIST_request', '2_CMD_AUTH_STEAMTICKET_request', '3_CMD_GET_URLLIST_request', '4_CMD_AUTH_STEAMTICKET_request') {
        [IO.File]::WriteAllText((Join-Path $fuseLogDir $n), "{`"ticket`":`"secret`"}")
    }
    $e2eConsole = Join-Path $e2eFuse 'console/e2e-stdout.log'
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $e2eConsole) -Force
    [IO.File]::WriteAllText($e2eConsole, $console + "time=7 level=INFO msg=CMD_CREATE_PLAYER player_name=`"Big Boss`"`n")
    $dummies = @()
    try {
        foreach ($i in 1, 2) {
            $dummies += Start-Process -FilePath $pwshPath -ArgumentList '-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 300' -WindowStyle Hidden -PassThru
        }
        $runStartedUtc = [DateTime]::UtcNow.AddSeconds(-30).ToString('o')
        [IO.File]::WriteAllText($e2eHostPaths.HostId, "$realHost`n")  # written by this run's host
        function New-FixtureRun([string]$Name) {
            $dir = Join-Path $e2e "lab/runs/$Name"
            $null = New-Item -ItemType Directory -Path $dir -Force
            $privateState = Join-Path $e2e "lab/private/run-$Name.json"
            Write-JsonFile -Path $privateState -Value ([ordered]@{ source_root = $userdata })
            Write-JsonFile -Path (Join-Path $dir 'real-saves-before.json') -Value ([ordered]@{ source_root = 'Steam/userdata/<account>'; files = (Get-RealSaveManifest -UserData $userdata) })
            $instances = [ordered]@{}
            foreach ($row in @(@('host', $e2eHostPaths, $dummies[0], $offsets.host, ''), @('guest', $e2eGuestPaths, $dummies[1], $offsets.guest, $labId))) {
                $instances[$row[0]] = [ordered]@{
                    role = $row[0]; dir = $row[1].Game; exe = $pwshPath; pid = $row[2].Id
                    process_start_utc = $row[2].StartTime.ToUniversalTime().ToString('o'); started_utc = [DateTime]::UtcNow.ToString('o')
                    log_offsets = [ordered]@{ 'script-observer.log' = $row[3] }
                    expected = [ordered]@{ save_dir = $row[1].SaveDir; master_url = $fuseUrl; steamid = $row[4]; mutex_suffix = $(if ($row[0] -eq 'guest') { '-guest' } else { '' }) }
                }
            }
            $run = [ordered]@{
                product = 'mgs-coop-lab-run'; schema = 2; stamp = $Name; run_dir = $dir; started_utc = $runStartedUtc; host_only = $false
                fuse = [ordered]@{ pid = 0; cwd = $e2eFuse; since_unix = $since; stdout = $e2eConsole; url = $fuseUrl }
                real_saves_before = 'real-saves-before.json'; private_state = $privateState; latest_save_backup = $backup
                instances = $instances
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
        Assert-Equal "$($result.evidence.host.safety.state)/$($result.evidence.guest.safety.state)" 'ok/ok' 'safety evidence recorded'
        Assert-Equal $result.host_id_check 'ok' 'host id cross-check'
        Assert-Equal "$($result.fuse_counts.CMD_GET_URLLIST)/$($result.fuse_counts.CMD_AUTH_STEAMTICKET)" '2/2' 'fuse traffic counted'
        Assert-Equal "$($result.fuse_identities.distinct_auth_ids)/$($result.fuse_identities.lab_guest_id_seen)" '2/True' 'fuse identities summarised without ids'
        $excerpt = [IO.File]::ReadAllText((Join-Path $runDir 'host-script-observer.log'))
        Assert-True (-not $excerpt.Contains('previous run line') -and $excerpt.Contains('host_peer_present')) 'excerpt covers only this run'
        Assert-True (@($result.files) -contains 'guest-script-observer.log') 'guest excerpt copied'
        Assert-True ([IO.File]::ReadAllText((Join-Path $runDir 'host-loopback.log')).Contains('self=<steamid>')) 'loopback excerpt redacted'
        $redactedConsole = [IO.File]::ReadAllText((Join-Path $runDir 'fuse-stdout.redacted.log'))
        Assert-True ($redactedConsole.Contains('steamID=<steamid>') -and $redactedConsole.Contains($labId) -and $redactedConsole.Contains('player_name=<redacted>')) 'fuse console copied redacted'
        Assert-True ([IO.File]::ReadAllText($e2eConsole).Contains($realHost)) 'raw fuse console (outside the run folder) untouched'

        # A refusal after the run's offsets fails the smoke.
        [IO.File]::AppendAllText($guestLog, "dynamite_client=refused; reason=not_ready`r`n")
        $runDir2 = New-FixtureRun 'fail'
        $null = & $pwshPath -NoProfile -NonInteractive -File $smoke -ConfigPath $e2eConfigPath -RunDir $runDir2 -NoScreens -PollMs 200 -TimeoutSec 5 -ReadyTimeoutSec 5 2>&1
        Assert-Equal $LASTEXITCODE 1 'smoke exit code on failure'
        $result = Get-Content -LiteralPath (Join-Path $runDir2 'result.json') -Raw | ConvertFrom-Json
        Assert-Equal "$($result.verdict)/$($result.reason)" 'fail/dynamite_client_refused' 'failure verdict'
        # The host's own id differs from what the guest targets.
        [IO.File]::WriteAllText($e2eHostPaths.HostId, "76561197960265730`n")
        $runDir3 = New-FixtureRun 'mismatch'
        $null = & $pwshPath -NoProfile -NonInteractive -File $smoke -ConfigPath $e2eConfigPath -RunDir $runDir3 -NoScreens -PollMs 200 -TimeoutSec 5 -ReadyTimeoutSec 5 2>&1
        $result = Get-Content -LiteralPath (Join-Path $runDir3 'result.json') -Raw | ConvertFrom-Json
        Assert-Equal "$($result.verdict)/$($result.reason)/$($result.host_id_check)" 'fail/host_id_mismatch/mismatch' 'host id mismatch fails the smoke'
        Assert-True (-not ([IO.File]::ReadAllText((Join-Path $runDir3 'result.json')).Contains('76561197960265730'))) 'mismatching id not written to result.json'
        [IO.File]::WriteAllText($e2eHostPaths.HostId, "$realHost`n")
        # A rule shown NOT in force mid-run fails the smoke at once.
        [IO.File]::AppendAllText($guestEarlyPath, "saves=unchanged`r`n")
        $runDir4 = New-FixtureRun 'unsafe'
        $null = & $pwshPath -NoProfile -NonInteractive -File $smoke -ConfigPath $e2eConfigPath -RunDir $runDir4 -NoScreens -PollMs 200 -TimeoutSec 5 -ReadyTimeoutSec 5 2>&1
        $result = Get-Content -LiteralPath (Join-Path $runDir4 'result.json') -Raw | ConvertFrom-Json
        Assert-Equal "$($result.verdict)/$($result.reason)" 'fail/unsafe_guest_saves_unchanged' 'unsafe guest fails the smoke'

        # Stop-Lab closes/terminates only the run's processes and checks the (fixture) real saves.
        $stopScript = Join-Path $projectRoot 'tools/lab/Stop-Lab.ps1'
        $null = & $pwshPath -NoProfile -NonInteractive -File $stopScript -ConfigPath $e2eConfigPath -RunDir $runDir -TimeoutSec 3 2>&1
        Assert-Equal $LASTEXITCODE 0 'Stop-Lab exit code with unchanged saves'
        foreach ($d in $dummies) { Assert-True $d.WaitForExit(5000) "stand-in process $($d.Id) stopped" }
        $stop = Get-Content -LiteralPath (Join-Path $runDir 'stop.json') -Raw | ConvertFrom-Json
        Assert-True $stop.real_saves.unchanged_since_run_start 'saves unchanged since run start (source root from build/lab/private)'
        Assert-True ($stop.games.host.action -eq 'close_then_terminate' -and $stop.games.host.exit -in @('closed', 'terminated')) 'recorded process verified, closed or terminated'
        foreach ($dir in $runDir, $runDir2, $runDir3, $runDir4) {
            $leaks = Get-PrivateLeaks -Dir $dir -Secrets @($realHost, 'userdata', '"secret"')
            Assert-Equal ($leaks -join ',') '' "run folder $(Split-Path -Leaf $dir) holds no SteamID, account folder or ticket"
        }
        [IO.File]::WriteAllText((Join-Path $userdata '311340/remote/TPP_GAME_DATA'), 'modified during the run')
        $null = & $pwshPath -NoProfile -NonInteractive -File $stopScript -ConfigPath $e2eConfigPath -RunDir $runDir -TimeoutSec 1 2>&1
        Assert-Equal $LASTEXITCODE 2 'Stop-Lab reports a changed real save'
    } finally {
        foreach ($d in $dummies) { if (!$d.HasExited) { Stop-Process -Id $d.Id -Force -ErrorAction SilentlyContinue } }
    }

    # ------------------------------------------------------------ full harness dry run with stand-in processes
    # Prepare -> Start -> Smoke -> Screens -> Stop -> (unrecorded process left alone)
    # -> Start refusals at the safety gate -> Disable against fixture folders.
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
        # The user's own coop.ini in the host folder: Prepare sets it aside, -Disable restores it.
        $userCoop = "character=snake`n"
        $null = New-Item -ItemType Directory -Path (Join-Path $dryHost 'mgs-coop') -Force
        [IO.File]::WriteAllText((Join-Path $dryHost 'mgs-coop/coop.ini'), $userCoop)
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
        $dryJson.smoke.safetyTimeoutSec = 30
        $dryConfig = Join-Path $dry 'lab.json'
        [IO.File]::WriteAllText($dryConfig, ($dryJson | ConvertTo-Json -Depth 8))
        $labDir = Join-Path $projectRoot 'tools/lab'
        $dryUrl = "http://127.0.0.1:$base/tppstm/gate"
        function Invoke-DryStart {
            $text = (& $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Start-Lab.ps1') -ConfigPath $dryConfig 2>&1 | Out-String)
            $code = $LASTEXITCODE
            $runPath = (Get-Content -LiteralPath (Join-Path $dry 'lab/current-run.json') -Raw | ConvertFrom-Json).run_dir
            [pscustomobject]@{ Code = $code; Text = $text; Run = (Get-Content -LiteralPath (Join-Path $runPath 'run.json') -Raw | ConvertFrom-Json); Dir = $runPath }
        }
        function Test-DryProcessesGone {
            @(Get-Process -Name mgsvtpp, fuse -ErrorAction SilentlyContinue | Where-Object { $p = Get-ProcessPathSafe $_; $p -and (Test-PathInside -Child $p -Parent $dry) }).Count -eq 0
        }
        $env:MGS_COOP_FAKE_HOST_STEAMID = $dryHostId
        $env:MGS_COOP_FAKE_UNSAFE = ''
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
            Assert-Equal (ConvertFrom-IniText ([IO.File]::ReadAllText($hp.CoopIni)))['master_url'] $dryUrl 'coop.ini fuse url'
            $userBackups = @(Get-ChildItem -LiteralPath $hp.Mod -File -Filter 'coop.ini.user-*')
            Assert-True ($userBackups.Count -eq 1 -and [IO.File]::ReadAllText($userBackups[0].FullName) -ceq $userCoop) 'user coop.ini set aside'
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
            Assert-Equal @(Get-ChildItem -LiteralPath $hp.Mod -File -Filter 'coop.ini.user-*').Count 1 'the lab coop.ini is not mistaken for a user file'

            $start = Invoke-DryStart
            Assert-Equal $start.Code 0 ('Start-Lab succeeds: ' + $start.Text)
            $dryRun = $start.Dir
            $runJson = $start.Run
            Assert-True ($runJson.instances.host.pid -gt 0 -and $runJson.instances.guest.pid -gt 0 -and $runJson.fuse.pid -gt 0) 'run.json records the pids'
            Assert-Equal ($runJson.fuse.args -join ' ') "-standalone -listenAddr 127.0.0.1:$base -baseURL http://127.0.0.1:$base/ -writeLog" 'fuse started with -standalone'
            Assert-Equal "$($runJson.instances.host.safety.state)/$($runJson.instances.guest.safety.state)" 'ok/ok' 'both instances passed the safety gate'
            Assert-Equal $runJson.host_id_check_at_guest_start 'ok' 'host id checked before the guest started'
            Assert-True ($runJson.instances.host.process_start_utc -and $runJson.instances.guest.process_start_utc) 'process start times recorded'
            Assert-True (-not (Test-PathInside -Child $runJson.fuse.stdout -Parent $dryRun)) 'fuse console log kept outside the run folder'
            Assert-True (Test-Path -LiteralPath (Join-Path $dryRun 'real-saves-before.json')) 'real-save manifest at run start'
            Assert-Equal (Get-Content -LiteralPath (Join-Path $dryRun 'real-saves-before.json') -Raw | ConvertFrom-Json).source_root 'Steam/userdata/<account>' 'run folder does not name the account folder'

            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Invoke-LabSmoke.ps1') -ConfigPath $dryConfig -PollMs 200 -TimeoutSec 30 -ReadyTimeoutSec 60 2>&1
            Assert-Equal $LASTEXITCODE 0 ('smoke passes against the stand-ins: ' + ($out -join ' | '))
            $result = Get-Content -LiteralPath (Join-Path $dryRun 'result.json') -Raw | ConvertFrom-Json
            Assert-Equal $result.verdict 'pass' 'dry-run verdict'
            Assert-Equal $result.evidence.guest.final_status.session_status 'client_established' 'lab-status.json read with the expected pid'
            Assert-True ([int]$result.fuse_messages.CMD_GET_URLLIST -ge 2 -and [int]$result.fuse_counts.CMD_AUTH_STEAMTICKET -ge 2) 'fuse traffic from both instances counted'
            Assert-Equal "$($result.fuse_identities.distinct_auth_ids)/$($result.fuse_identities.lab_guest_id_seen)" '2/True' 'the guest reached fuse with the lab id'
            Assert-Equal $result.host_id_check 'ok' 'dry-run host id cross-check'
            Assert-Equal @($result.screenshots).Count 2 'one screenshot per window'

            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Stop-Lab.ps1') -ConfigPath $dryConfig -TimeoutSec 10 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Stop-Lab succeeds with unchanged saves: ' + ($out -join ' | '))
            $stop = Get-Content -LiteralPath (Join-Path $dryRun 'stop.json') -Raw | ConvertFrom-Json
            Assert-Equal "$($stop.games.host.exit)/$($stop.games.guest.exit)/$($stop.fuse)" 'closed/closed/stopped' 'games closed by WM_CLOSE, fuse stopped'
            Assert-True $stop.host_lab_coop_ini_present 'Stop-Lab reports the lingering lab coop.ini'
            foreach ($id in $runJson.instances.host.pid, $runJson.instances.guest.pid, $runJson.fuse.pid) {
                Assert-True ($null -eq (Get-Process -Id $id -ErrorAction SilentlyContinue)) "process $id gone"
            }
            Assert-True ([IO.File]::ReadAllText($runJson.fuse.stdout).Contains($dryHostId)) 'raw fuse console names the host'
            $leaks = Get-PrivateLeaks -Dir $dryRun -Secrets @($dryHostId, 'userdataS45')
            Assert-Equal ($leaks -join ',') '' 'dry run folder holds no SteamID or account folder'

            # A normal session started later from the host folder is not this run's: left alone.
            $later = Start-Process -FilePath $hp.Exe -WorkingDirectory $hp.Game -PassThru
            $deadline = [DateTime]::UtcNow.AddSeconds(15)
            while ((Get-ProcessMainWindow -Id $later.Id) -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline -and !$later.HasExited) { Start-Sleep -Milliseconds 200 }
            Assert-True (!$later.HasExited) 'later session running'
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Stop-Lab.ps1') -ConfigPath $dryConfig -TimeoutSec 3 2>&1
            Assert-Equal $LASTEXITCODE 0 'Stop-Lab with an unrecorded session'
            Start-Sleep -Milliseconds 500
            Assert-True (!$later.HasExited) 'Stop-Lab leaves a process it did not start running'
            $stop = Get-Content -LiteralPath (Join-Path $dryRun 'stop.json') -Raw | ConvertFrom-Json
            Assert-Equal $stop.games."unrecorded-host-$($later.Id)".action 'left_running' 'unrecorded session reported as left running'
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Stop-Lab.ps1') -ConfigPath $dryConfig -TimeoutSec 10 -IncludeUnrecordedProcesses 2>&1
            Assert-True $later.WaitForExit(10000) '-IncludeUnrecordedProcesses closes it with WM_CLOSE'
            Assert-Equal (Get-Content -LiteralPath (Join-Path $dryRun 'stop.json') -Raw | ConvertFrom-Json).games."unrecorded-host-$($later.Id)".exit 'closed' 'closed, not terminated'

            # Safety gate refusals: Start-Lab stops what it started and never starts the guest early.
            $env:MGS_COOP_FAKE_UNSAFE = 'host:saves'
            $start = Invoke-DryStart
            Assert-True ($start.Code -ne 0 -and $start.Text.Contains('unsafe_saves_unchanged')) ('host saves=unchanged refused: ' + $start.Text)
            Assert-True (!($start.Run.instances.PSObject.Properties.Name -contains 'guest') -or !($start.Run.instances.guest.PSObject.Properties.Name -contains 'pid')) 'guest never started after an unsafe host'
            Assert-True ((@($start.Run.aborted.stopped) -contains 'host:terminated_immediately') -and (@($start.Run.aborted.stopped) -contains 'fuse:stopped')) 'unsafe host terminated at once, fuse stopped'
            Assert-True (Test-DryProcessesGone) 'nothing left running after the host refusal'
            $env:MGS_COOP_FAKE_UNSAFE = 'host:master'
            $start = Invoke-DryStart
            Assert-True ($start.Code -ne 0 -and $start.Text.Contains('unsafe_master_server_refused')) 'master_server=refused stops the host'
            Assert-True (Test-DryProcessesGone) 'nothing left running after the redirect refusal'
            $env:MGS_COOP_FAKE_UNSAFE = 'guest:identity'
            $start = Invoke-DryStart
            Assert-True ($start.Code -ne 0 -and $start.Text.Contains('unsafe_identity_real')) 'guest identity=real refused'
            Assert-True ((@($start.Run.aborted.stopped) -contains 'guest:terminated_immediately') -and (@($start.Run.aborted.stopped) -contains 'host:closed')) 'unsafe guest terminated at once, host closed'
            Assert-True (Test-DryProcessesGone) 'nothing left running after the guest refusal'
            $env:MGS_COOP_FAKE_UNSAFE = ''
            $env:MGS_COOP_FAKE_HOST_STEAMID = ConvertTo-SteamId64 -AccountId 54321
            $start = Invoke-DryStart
            Assert-True ($start.Code -ne 0 -and $start.Text.Contains('host_id_mismatch')) 'host id mismatch stops the start before the guest'
            Assert-True (!($start.Run.instances.guest.PSObject.Properties.Name -contains 'pid')) 'guest not started on a host id mismatch'
            Assert-Equal $start.Run.host_id_check_at_guest_start 'mismatch' 'mismatch recorded'
            Assert-True (Test-DryProcessesGone) 'nothing left running after the id mismatch'
            $env:MGS_COOP_FAKE_HOST_STEAMID = $dryHostId

            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig -Disable 2>&1
            Assert-Equal $LASTEXITCODE 0 'Disable succeeds'
            Assert-True ((-not (Test-Path -LiteralPath $hp.LabIni)) -and (-not (Test-Path -LiteralPath $gp.CoopIni)) -and
                @(Get-ChildItem -LiteralPath $gp.Mod -Filter 'lab.ini.disabled-*').Count -eq 1) 'Disable renames lab.ini and coop.ini'
            Assert-Equal ([IO.File]::ReadAllText($hp.CoopIni)) $userCoop 'Disable restores the user coop.ini'
            Assert-Equal @(Get-ChildItem -LiteralPath $hp.Mod -File -Filter 'coop.ini.user-*').Count 0 'user backup moved back'
        } finally {
            Remove-Item Env:MGS_COOP_FAKE_HOST_STEAMID -ErrorAction SilentlyContinue
            Remove-Item Env:MGS_COOP_FAKE_UNSAFE -ErrorAction SilentlyContinue
            foreach ($p in @(Get-Process -Name mgsvtpp, fuse -ErrorAction SilentlyContinue)) {
                $path = Get-ProcessPathSafe $p
                if ($path -and (Test-PathInside -Child $path -Parent $dry)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
            }
        }
    } else { Write-Warning 'lab_fake_process not supplied; full harness dry run skipped.' }

    Write-Output "$script:checks lab script checks passed; temporary fixtures only, no game, Steam or fuse started"
} finally {
    Remove-OwnedFixture
}
