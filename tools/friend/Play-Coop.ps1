<#
MGS Co-op two-PC launcher: Host or Join, then MGSV starts through Steam with
co-op active for this one game session (docs/two-pc-play.md).

Double-click Play-Coop.bat and choose:
  [O] Host online (Steam, recommended): no port forwarding, VPN or extra
        program. Steam connects the two games directly or through Valve's
        relays (docs/steam-online.md). You get an MGS2 code for your friend
        (copied to the clipboard), or invite them in game: F9 -> Partner /
        Co-op -> Online -> Invite.
  [J] Join online: paste the host's MGS2 code, or press Enter and wait for
        the host's Steam invite (accept it in the Steam overlay or chat while
        MGSV runs). An MGS1 code here joins over LAN/VPN instead.
  [L] LAN/VPN (UDP code): the MGS1 flow below, for the same network or a VPN.
        Host  shows a join code for each address of this PC (and copies the one
              you choose to the clipboard). Send it to your friend over chat.
        Join  asks for the host's code (paste it; it is never a command-line
              parameter, so it does not end up in process lists or shell history).

What it changes, only while the game runs (all in <game>\mgs-coop):
  lab.ini, link-secret.txt, coop.ini and script-observe.enabled (offline-v1)
  are written in this order, each recorded with its SHA-256 in
  play-session.json first. A hidden watchdog (this script with -Watch) and
  this window delete them again when the game exits - only the files whose
  hash still matches. Closing this window does not stop that: the watchdog
  cleans up when MGSV exits (or right away when the game never started).
  The next Play-Coop, Install-Coop and Uninstall-Coop also clean up leftovers.

The other player's body (docs/two-pc-play.md "Native partner"): -Partner native
(the default) moves it with the game's own movement, aiming, reloading and
(visual-only) firing from the other player's controls, and falls back to
animation clips by itself when the network or this PC cannot keep up;
-Partner clips always uses the clips. The status line shows which one runs.

While co-op is active the game uses the save COPY in mgs-coop\coop-saves and
plays offline ("Failed to log in" is expected; Konami's servers are never
contacted). The partner body supports Mother Base and Afghanistan/Africa free roam.

LAN/VPN only, each asked with default No: look up the public IP
(contacts api.ipify.org), a UPnP port mapping on the router (removed again at
the end), a Windows Firewall rule (administrator prompt). Nothing else
contacts the network, and nothing changes firewall or router settings
without a "y". The online flow has none of these steps.

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Play-Coop.ps1 [-GameDirectory <MGSV folder>] [-Role Host|Join] [-Transport steam|udp] [-Code <MGS2 code>] [-Port 47830] [-HostAddress <ip or name>] [-World 0..3] [-Partner native|clips] [-SteamDispatch both|fnptr|callback|poll] [-Hud compact|full|off]
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Play-Coop.ps1 -Cleanup
#>
[CmdletBinding()]
param(
    [string]$GameDirectory,
    # Local two-window rehearsal; both games start at their normal title screens.
    [switch]$LocalTest,
    [string]$GuestDirectory,
    # Skip the menu. -Role without -Transport keeps the LAN/VPN (UDP) flow.
    [ValidateSet('Host', 'Join')][string]$Role,
    # steam: Steam online (docs/steam-online.md); udp: the LAN/VPN code.
    [ValidateSet('steam', 'udp')][string]$Transport,
    # Online join: the host's MGS2 code (pasting it at the prompt keeps it out of process lists).
    [string]$Code,
    # UDP port of the host (the join code carries it).
    [ValidateRange(1024, 65535)][int]$Port = 47830,
    # Host: the address to put in the join code (default: this PC's addresses).
    [string]$HostAddress,
    # World sync level (docs/two-pc-play.md): 3 by default, 0 opts out,
    # 1 plumbing, 2 shared enemies and alert in free roam, 3 also clock and
    # weather and host travel/recovery. The effective level is the lower one.
    [ValidateRange(0, 3)][int]$World = 3,
    # The partner body: native (default; the game's own movement and gun
    # systems driven by the other player's controls, clips as the automatic
    # fallback) or clips (the animation clips only).
    [ValidateSet('native', 'clips')][string]$Partner = 'native',
    # Steam online, for the developer's two-PC tests (docs/steam-online.md 20.5):
    # how the host finds a code join (both = function pointer + callback, the
    # default; fnptr; callback; poll). The menu never asks for it.
    [ValidateSet('', 'both', 'fnptr', 'callback', 'poll')][string]$SteamDispatch = '',
    # The in-game co-op status line (docs/two-pc-play.md "In game"): compact
    # (the default), full (more detail) or off.
    [ValidateSet('', 'compact', 'full', 'off')][string]$Hud = '',
    # Remove the files of an earlier session that did not end cleanly, then exit.
    [switch]$Cleanup,
    # Internal: the hidden watchdog started by this script (value: the session id).
    [string]$Watch,
    [switch]$NonInteractive,
    # Folder with package.json (default: the package folder).
    [string]$PackageDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = $PSScriptRoot
# A failure ends with one plain line (and exit code 1) instead of a raw
# Windows PowerShell error record.
trap {
    Write-Host ''
    Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
# The loader versions whose native side implements the play mode. The
# integrator keeps this list matched to the packaged loader version.
$script:SupportedLoaderVersions = @('0.12.0','0.12.1','0.12.2','0.12.3','0.12.4','0.12.5','0.12.6')

function Resolve-Piece([string]$PackagedName, [string]$RepoRelative) {
    # Packaged layout: scripts\<name>. Repository layout: tools\friend\..\<path>.
    $packaged = Join-Path $scriptRoot $PackagedName
    if (Test-Path -LiteralPath $packaged -PathType Leaf) { return $packaged }
    $repo = Join-Path $scriptRoot $RepoRelative
    if (Test-Path -LiteralPath $repo -PathType Leaf) { return (Resolve-Path -LiteralPath $repo).Path }
    throw "This package is incomplete: $PackagedName is missing. Extract the whole ZIP again."
}
Import-Module (Resolve-Piece 'FriendCommon.psm1' 'FriendCommon.psm1') -Force -DisableNameChecking
if (!$PackageDirectory) { $PackageDirectory = Split-Path -Parent $scriptRoot }

# ---------------------------------------------------------------- game folder
if (!$GameDirectory) {
    $location = Read-FriendJson (Join-Path $PackageDirectory 'installed-location.json')
    $recorded = [string](Get-FriendProperty $location 'game_directory' '')
    if ($recorded -and (Test-Path -LiteralPath (Join-Path $recorded 'mgs-coop\friend-install.json') -PathType Leaf)) { $GameDirectory = $recorded }
    else {
        $registry = Get-RegistryMgsvCandidates
        $found = @(Find-MgsvGameFolders -SteamRoots @($registry.SteamRoots) -Candidates @($registry.Candidates) |
                Where-Object { Test-Path -LiteralPath (Join-Path $_ 'mgs-coop\friend-install.json') -PathType Leaf })
        if ($found.Count -eq 1) { $GameDirectory = $found[0] }
        elseif ($Watch) { exit 0 }
        else { throw 'No MGS Co-op install was found. Run Install-Coop.bat first (or pass -GameDirectory).' }
    }
}
$GameDirectory = $GameDirectory.Trim().Trim('"')
if (!(Test-Path -LiteralPath $GameDirectory -PathType Container)) { throw "Folder not found: $GameDirectory" }
$gameRoot = [IO.Path]::GetFullPath((Get-Item -LiteralPath $GameDirectory -Force).FullName).TrimEnd('\', '/')
$paths = Get-InstancePaths -GameDirectory $gameRoot

# ---------------------------------------------------------------- watchdog
if ($Watch) {
    # Cleans up when the game started from this folder exits, or when no game
    # started and the launcher window is gone. Never prints anything that matters.
    $seen = $false
    while ($true) {
        $session = Read-CoopPlaySession -GameRoot $gameRoot
        if (!$session -or [string](Get-FriendProperty $session 'state' '') -ne 'active' -or [string](Get-FriendProperty $session 'session_id' '') -cne $Watch) { exit 0 }
        $games = @(Get-GameProcessInDirectory -Directory $gameRoot)
        if ($games.Count) { $seen = $true }
        elseif ($seen) { break }
        elseif (!(Test-CoopProcessAlive -Id ([int](Get-FriendProperty $session 'launcher_pid' 0)) -StartUtc ([string](Get-FriendProperty $session 'launcher_start_utc' '')))) {
            # The window was closed before the game started. A game that is just
            # starting gets a moment to appear first.
            Start-Sleep -Seconds 3
            if (@(Get-GameProcessInDirectory -Directory $gameRoot).Count) { $seen = $true; continue }
            break
        }
        Start-Sleep -Seconds 2
    }
    $null = Invoke-CoopPlayCleanup -GameRoot $gameRoot
    exit 0
}

function Start-LocalTest {
    $local = Resolve-Piece 'Play-LocalCoop.ps1' 'Play-LocalCoop.ps1'
    $settings = @{GameDirectory=$gameRoot;PackageDirectory=$PackageDirectory;World=$World;Partner=$Partner;Hud=$Hud;NonInteractive=$NonInteractive}
    if ($GuestDirectory) { $settings['GuestDirectory'] = $GuestDirectory }
    & $local @settings
    exit $LASTEXITCODE
}
if ($LocalTest) { Start-LocalTest }
Import-Module (Resolve-Piece 'LocalCoopSession.psm1' 'LocalCoopSession.psm1') -Force -DisableNameChecking
if (Test-Path -LiteralPath $paths.Mod -PathType Container) { $null = Invoke-LocalCoopRecovery -HostRoot $gameRoot }
$interactive = Test-FriendInteractive -NonInteractive:$NonInteractive
function Ask([string]$Prompt) {
    if (!$interactive) { throw "Missing input: $Prompt (run Play-Coop.bat in a window)." }
    $answer = Read-Host $Prompt
    if ($null -eq $answer) { return '' }
    return ([string]$answer).Trim()
}
function Section([string]$Text) { Write-Host ''; Write-Host $Text -ForegroundColor Cyan }
function Write-Lines([string[]]$Lines, [string]$Color = '') {
    foreach ($l in @($Lines)) { if ($Color) { Write-Host $l -ForegroundColor $Color } else { Write-Host $l } }
}
function Assert-CoopReleaseLoader([string]$ExpectedManifestSha256) {
    # The installed ownership record alone identifies an owned loader; this
    # package's exact manifest and payload identify the release being started.
    $manifestPath = Join-Path $PackageDirectory 'package.json'
    Assert-NoReparsePoint -Path $manifestPath -StopAt $PackageDirectory
    $manifestHash = Get-Sha256 $manifestPath
    if ($manifestHash -cne $ExpectedManifestSha256) { throw 'The release package changed while Play-Coop was preparing. Extract the ZIP again.' }
    $current = Read-FriendJson $manifestPath
    if (!$current -or [string](Get-FriendProperty $current 'product' '') -cne 'mgs-coop-local-package' -or
        $script:SupportedLoaderVersions -notcontains [string](Get-FriendProperty $current 'version' '')) {
        throw 'The release package manifest is invalid. Extract the whole ZIP again.'
    }
    $payload = @(Get-FriendProperty $current 'files' @() | Where-Object { [string](Get-FriendProperty $_ 'target' '') -ceq 'dinput8.dll' })
    if ($payload.Count -ne 1 -or [string](Get-FriendProperty $payload[0] 'source' '') -cne 'payload/dinput8.dll' -or
        [string](Get-FriendProperty $payload[0] 'sha256' '') -cnotmatch '^[A-Fa-f0-9]{64}$') {
        throw 'The release package has no unique checked loader entry. Extract the whole ZIP again.'
    }
    $expected = ([string]$payload[0].sha256).ToUpperInvariant()
    $source = Join-Path $PackageDirectory 'payload\dinput8.dll'
    Assert-NoReparsePoint -Path $source -StopAt $PackageDirectory
    if (!(Test-FileHash -Path $source -Expected $expected)) { throw 'The release package loader has changed. Extract the ZIP again.' }
    $owned = Read-FriendJson $paths.Record
    if (!$owned -or [string](Get-FriendProperty $owned 'product' '') -cne 'mgs-coop-original-loader-probe' -or
        [string](Get-FriendProperty $owned 'game_directory' '') -cne $gameRoot -or
        [string](Get-FriendProperty $owned 'version' '') -cne [string]$current.version -or
        ([string](Get-FriendProperty $owned 'dll_sha256' '')).ToUpperInvariant() -cne $expected -or
        !(Test-FileHash -Path $paths.Loader -Expected $expected)) {
        throw 'The installed loader is not this exact release. Run Install-Coop.bat from this package before playing.'
    }
    if ((Get-Sha256 $manifestPath) -cne $manifestHash) { throw 'The release package changed while Play-Coop was preparing. Extract the ZIP again.' }
}

function Write-CleanupResult($Result) {
    if (!$Result.Ran) { return }
    if (@($Result.Removed).Count) { Write-Host ('Co-op files removed: ' + (@($Result.Removed) -join ', ') + '. Normal launches play as usual (real saves).') }
    Write-Lines -Lines @($Result.Messages) -Color Yellow
}

Write-Host 'MGS Co-op - online or local play' -ForegroundColor Cyan
Write-Host "Game folder: $gameRoot"

if ($Cleanup) {
    if (@(Get-GameProcessInDirectory -Directory $gameRoot).Count) { throw 'MGSV is running from this folder. The co-op files are removed when it exits; run -Cleanup again after quitting it.' }
    $r = Invoke-CoopPlayCleanup -GameRoot $gameRoot
    if ($r.Ran) { Write-CleanupResult $r } else { Write-Host 'No active co-op session; nothing to clean up.' }
    exit 0
}

# ---------------------------------------------------------------- 1: preflight
Section 'Checking the install'
$state = Read-FriendJson (Join-Path $paths.Mod 'friend-install.json')
if (!$state -or (Get-FriendProperty $state 'product') -ne 'mgs-coop-friend-install' -or [string](Get-FriendProperty $state 'status' '') -ne 'installed') {
    throw 'MGS Co-op is not installed in this folder (or its install did not finish). Run Install-Coop.bat first.'
}
$packageManifestSha256 = Get-Sha256 (Join-Path $PackageDirectory 'package.json')
$package = Read-FriendJson (Join-Path $PackageDirectory 'package.json')
if (!$package -or (Get-FriendProperty $package 'product') -ne 'mgs-coop-local-package') { throw "package.json not found in $PackageDirectory. Run Play-Coop.bat from the extracted package folder." }
$ownBuild = [string]$package.version
if (Test-Path -LiteralPath $paths.Mod) { Assert-NoReparsePoint -Path $paths.Mod -StopAt $gameRoot }
foreach ($p in @($paths.LabIni, $paths.CoopIni, $paths.LinkSecret, $paths.Marker, $paths.PlaySession)) {
    if (Test-Path -LiteralPath $p) { Assert-NoReparsePoint -Path $p -StopAt $gameRoot }
}
if (@(Get-GameProcessInDirectory -Directory $gameRoot).Count) { throw 'MGSV is already running from this folder. Quit it normally, then start Play-Coop again.' }
# A session that did not end cleanly (crash, power loss): its files go first.
# One whose launcher window or helper still runs is in use (-Cleanup forces it).
if (Test-CoopPlaySessionLive -GameRoot $gameRoot) { throw 'Another Play-Coop window is active for this folder; close it first (or quit MGSV). Nothing was changed.' }
$leftover = Invoke-CoopPlayCleanup -GameRoot $gameRoot
if ($leftover.Ran) { Write-Host 'An earlier co-op session did not end cleanly; its files were cleaned up.' -ForegroundColor Yellow; Write-CleanupResult $leftover }
$record = Read-FriendJson $paths.Record
if (!$record -or (Get-FriendProperty $record 'product') -ne 'mgs-coop-original-loader-probe' -or [string](Get-FriendProperty $record 'game_directory' '') -ne $gameRoot) {
    throw 'The MGS Co-op loader in this folder is not the one Install-Coop installed. Run Install-Coop.bat again.'
}
if ($script:SupportedLoaderVersions -notcontains [string](Get-FriendProperty $record 'version' '')) {
    throw ("The installed loader (" + [string](Get-FriendProperty $record 'version' 'unknown') + ") does not support two-PC play. Run Install-Coop.bat from this package.")
}
if (!(Test-Path -LiteralPath $paths.Loader -PathType Leaf) -or (Get-Sha256 $paths.Loader) -ne ([string](Get-FriendProperty $record 'dll_sha256' '')).ToUpperInvariant()) {
    throw 'dinput8.dll differs from the one Install-Coop installed. Run Install-Coop.bat again.'
}
Assert-CoopReleaseLoader -ExpectedManifestSha256 $packageManifestSha256
Write-Host 'Checking the game files (a few seconds)...'
if (!(Test-FileHash -Path $paths.Exe -Expected ([string]$package.executable_sha256))) { throw 'mgsvtpp.exe is not the expected Steam version 1.0.15.4. Verify the game files in Steam, then run Install-Coop.bat again.' }
if (!(Test-FileHash -Path $paths.SteamApi -Expected ([string]$package.steam_dll_sha256))) { throw 'steam_api64.dll is not the original file. Verify the game files in Steam, then run Install-Coop.bat again.' }
$archiveSha256 = [string]$package.runtime_archive.sha256
if (!(Test-FileHash -Path $paths.Archive -Expected $archiveSha256)) { throw 'mgs-coop-player-runtime.dat is missing or changed. Run Install-Coop.bat again.' }
$saveDir = $paths.CoopSaves
if (!(Test-Path -LiteralPath $saveDir -PathType Container)) { throw 'The co-op save copy (mgs-coop\coop-saves) is missing. Run Install-Coop.bat again.' }
$saveProblem = Get-FriendSaveDirProblem $saveDir
if ($saveProblem) { throw $saveProblem }
# ---------------------------------------------------------------- 2: menu
if (!$Role) {
    Section 'How do you want to play?'
    Write-Host '  [O] Host online (Steam, recommended) - no port forwarding, VPN or extra program'
    Write-Host "  [J] Join online - paste the host's code, or wait for their Steam invite"
    Write-Host '  [L] LAN/VPN (UDP code) - the same network, or a VPN such as Tailscale'
    Write-Host '  [T] Local two-window test - both players on this PC, manual startup'
    Write-Host '  [Q] Quit'
    switch -Regex (Ask 'Choose O, J, L, T or Q') {
        '^[Tt]' { Start-LocalTest }
        '^[Oo]' { $Role = 'Host'; $Transport = 'steam' }
        '^[Jj]' { $Role = 'Join'; $Transport = 'steam' }
        '^[Ll]' {
            $Transport = 'udp'
            Section 'Host or Join?'
            Write-Host '  [H] Host - your friend joins you (you send them a code)'
            Write-Host '  [J] Join - you join your friend (you need their code)'
            Write-Host '  [Q] Quit'
            switch -Regex (Ask 'Choose H, J or Q') {
                '^[Hh]' { $Role = 'Host' }
                '^[Jj]' { $Role = 'Join' }
                default { Write-Host 'Nothing was changed.'; exit 0 }
            }
        }
        default { Write-Host 'Nothing was changed.'; exit 0 }
    }
}
# Files Play-Coop would write must not belong to anything else.
foreach ($pair in @(@($paths.LabIni, 'lab.ini'), @($paths.CoopIni, 'coop.ini'))) {
    if (!(Test-Path -LiteralPath $pair[0])) { continue }
    $text = Read-SharedText -Path $pair[0]
    $what = 'a file Play-Coop did not write'
    if (Test-CoopPlayGeneratedText $text) { $what = 'a Play-Coop file that was changed afterwards' }
    throw ("mgs-coop\" + $pair[1] + " exists and is $what (for example from the single-PC lab or an earlier package). Play-Coop does not overwrite it: move it out of $($paths.Mod), then start Play-Coop again.")
}
foreach ($pair in @(@($paths.Marker, 'script-observe.enabled'), @($paths.LinkSecret, 'link-secret.txt'))) {
    if (Test-Path -LiteralPath $pair[0]) { throw ("mgs-coop\" + $pair[1] + " exists but was not written by this Play-Coop session. Run Install-Coop.bat again (it sets the folder back to normal), then start Play-Coop.") }
}
$steamFolders = @()
try { $registry = Get-RegistryMgsvCandidates; $steamFolders = @(Find-MgsvGameFolders -SteamRoots @($registry.SteamRoots) -Candidates @($registry.Candidates)) } catch { $steamFolders = @() }
if ($steamFolders.Count -and !(@($steamFolders | Where-Object { $_.Equals($gameRoot, [StringComparison]::OrdinalIgnoreCase) }).Count)) {
    Write-Host "Warning: Steam's MGSV folder seems to be $($steamFolders[0]), not $gameRoot. Steam starts the game from its own folder; co-op is prepared only in $gameRoot." -ForegroundColor Yellow
}
$character = [string](Get-FriendProperty $state 'character' '')
if (!(Test-FriendCharacter $character)) { $character = 'default' }
Write-Host "Install OK. Your character: $character (Install-Coop.bat changes it)."

if (!$Transport) { $Transport = 'udp' }
$labRole = 'guest'
if ($Role -eq 'Host') { $labRole = 'host' }
# Steam online needs Steam running and logged in: its account is this PC's
# identity in the code (docs/steam-online.md 10.1).
$steamSelf = ''
$steamPeer = ''
$steamWait = $false
if ($Transport -eq 'steam') {
    $activeUser = Get-SteamActiveUser
    # (tests/friend_package_tests.ps1 only: a stand-in Steam account.)
    if ($env:MGS_COOP_TEST_STEAM_ACTIVE_USER) { $activeUser = [UInt32]$env:MGS_COOP_TEST_STEAM_ACTIVE_USER }
    $steamSelf = Get-CoopSteamId64 -ActiveUser $activeUser
    if (!$steamSelf) { throw 'Steam is not running or not logged in. Start Steam and log in (online), then start Play-Coop again. Nothing was changed.' }
}

$session = $null
$clipSha = ''
function Save-Session { Save-CoopPlaySession -GameRoot $gameRoot -Session $script:session }
function New-Session {
    $me = Get-Process -Id $PID
    $start = ''
    try { $start = $me.StartTime.ToUniversalTime().ToString('o') } catch { $start = '' }
    $script:session = [ordered]@{
        v = 2; state = 'active'; role = $labRole; port = $Port; started_utc = [DateTime]::UtcNow.ToString('o')
        launcher_pid = $PID; launcher_start_utc = $start; watchdog_pid = 0; watchdog_start_utc = ''; session_id = [Guid]::NewGuid().ToString('N')
        transport = $Transport
        files = @(); upnp = [ordered]@{ added = $false; port = 0 }; clip_sha256 = ''
        note = 'Written by Play-Coop.ps1; no secrets. The listed files are deleted when the game exits, only while their SHA-256 matches.'
    }
}

$activated = $false
$gamePid = 0
$skipCleanup = $false
try {
    if ($Role -eq 'Host' -and $Transport -eq 'steam') {
        # ------------------------------------------------------------ 3a: host online
        Section 'Host online (Steam)'
        $secret = New-CoopSecret
        $steamCode = New-CoopSteamJoinCode -Secret $secret -SteamId $steamSelf
        Section 'Your join code (send it to your friend; keep it private)'
        Write-Host ("     $steamCode") -ForegroundColor Green
        # (Not in the scripted tests: they must not touch the test machine's clipboard.)
        if ($interactive -and $env:MGS_COOP_TEST_INTERACTIVE -ne '1') {
            try {
                Set-Clipboard -Value $steamCode
                $clipSha = Get-CoopTextSha256 -Bytes ([Text.Encoding]::UTF8.GetBytes($steamCode))
                Write-Host 'The code is copied to the clipboard. Windows clipboard history (Win+V) may keep it; it is cleared from the clipboard when the game exits.'
            } catch { Write-Host 'Could not copy to the clipboard; select the code above and copy it.' -ForegroundColor Yellow }
        }
        Write-Host ''
        Write-Host 'Send this code to your friend, or invite them in game: F9 -> Partner / Co-op -> Online -> Invite.'
        Write-Host 'No port forwarding, VPN or firewall rule is needed: Steam connects the two games, directly or through Valve''s relays.'
        Write-Host 'Your friend starts Play-Coop, chooses [J] Join online, and pastes the code (or waits for your invite).'
        Write-Host 'Both players must have installed the same package version. Partner bodies support Mother Base, Afghanistan and Africa free roam.'
        Write-Host 'A new code is made every time you host; an old code stops working.'
        $peerAddress = ''
    } elseif ($Role -eq 'Host') {
        # ------------------------------------------------------------ 3a: host (LAN/VPN)
        Section "Host on UDP port $Port"
        $secret = New-CoopSecret
        $candidates = New-Object Collections.Generic.List[object]
        if ($HostAddress) {
            $h = $HostAddress.Trim().ToLowerInvariant()
            if (!(Test-CoopCanonicalHost $h)) { throw "-HostAddress must be an IPv4 address (no leading zeros) or a host name: $HostAddress" }
            $candidates.Add([pscustomobject]@{ Address = $h; Kind = 'given'; Alias = '' })
        } else {
            foreach ($c in @(Get-CoopLocalConfigurations)) {
                foreach ($a in @($c.Addresses)) {
                    $kind = Get-CoopAddressKind -Address $a -InterfaceAlias $c.Alias
                    if ($kind -ne 'skip' -and [string]$c.Status -eq 'Up') { $candidates.Add([pscustomobject]@{ Address = $a; Kind = $kind; Alias = $c.Alias }) }
                }
            }
            if ($interactive -and (Ask '[P] Look up your public IP address? This contacts api.ipify.org. [y/N]') -match '^(y|yes)$') {
                try {
                    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
                    $ip = ([string](Invoke-RestMethod -Uri 'https://api.ipify.org' -TimeoutSec 10)).Trim()
                    if (Test-CoopIPv4Text $ip) { $candidates.Add([pscustomobject]@{ Address = $ip; Kind = 'public (api.ipify.org)'; Alias = '' }) }
                    else { Write-Host 'The lookup did not return an IPv4 address.' -ForegroundColor Yellow }
                } catch { Write-Host "Public IP lookup failed: $($_.Exception.Message)" -ForegroundColor Yellow }
            }
        }
        $lan = @($candidates | Where-Object { $_.Kind -eq 'lan' } | Select-Object -First 1)
        if ($interactive -and $lan.Count -and (Ask "[U] Ask your router to forward UDP $Port to this PC (UPnP; removed again when the game exits)? [y/N]") -match '^(y|yes)$') {
            if (!$session) { New-Session }
            # Recorded before the router is asked, so a crash still removes it later.
            $session.upnp = [ordered]@{ added = $true; port = $Port }
            Save-Session
            Write-Host 'Asking the router (up to 15 seconds)...'
            $u = Invoke-CoopUpnp -Port $Port -LanAddress $lan[0].Address
            Write-Host ("UPnP: " + $u.Message)
            if (!$u.Ok) { $session.upnp = [ordered]@{ added = $false; port = 0 }; Save-Session }
            elseif ((Test-CoopIPv4Text $u.ExternalAddress) -and (Get-CoopAddressKind -Address $u.ExternalAddress) -eq 'public') {
                $candidates.Add([pscustomobject]@{ Address = $u.ExternalAddress; Kind = 'public (router)'; Alias = '' })
            }
        }
        if (!$candidates.Count) {
            Write-Host 'No network address was found on this PC.' -ForegroundColor Yellow
            $typed = (Ask 'Type the address your friend should use (IPv4 or name), or press Enter to quit').ToLowerInvariant()
            if (!$typed) { Write-Host 'Nothing was changed.'; exit 0 }
            if (!(Test-CoopCanonicalHost $typed)) { throw "Not an IPv4 address or host name: $typed" }
            $candidates.Add([pscustomobject]@{ Address = $typed; Kind = 'given'; Alias = '' })
        }
        Section 'Your join codes (send ONE to your friend; keep them private)'
        $labels = @{ lan = 'same network (LAN)'; tailscale = 'Tailscale VPN'; zerotier = 'ZeroTier VPN'; radmin = 'Radmin VPN'; public = 'internet (needs port forwarding)'; given = 'the address you gave' }
        $codes = @()
        for ($i = 0; $i -lt $candidates.Count; $i++) {
            $c = $candidates[$i]
            $code = New-CoopJoinCode -Secret $secret -HostName $c.Address -Port $Port
            $codes += @($code)
            $label = [string]$c.Kind
            if ($labels.ContainsKey($label)) { $label = $labels[$label] }
            if ($c.Alias) { $label += " - $($c.Alias)" }
            Write-Host ("  {0}: {1}" -f ($i + 1), $label) -ForegroundColor Cyan
            Write-Host ("     $code") -ForegroundColor Green
        }
        $choice = 1
        if ($interactive -and $codes.Count -gt 1) {
            $answer = Ask "Copy which code to the clipboard? [1-$($codes.Count), Enter = 1]"
            $n = 0
            if ($answer -and [int]::TryParse($answer, [ref]$n) -and $n -ge 1 -and $n -le $codes.Count) { $choice = $n }
        }
        # (Not in the scripted tests: they must not touch the test machine's clipboard.)
        if ($interactive -and $env:MGS_COOP_TEST_INTERACTIVE -ne '1') {
            try {
                Set-Clipboard -Value $codes[$choice - 1]
                $clipSha = Get-CoopTextSha256 -Bytes ([Text.Encoding]::UTF8.GetBytes($codes[$choice - 1]))
                Write-Host "Code $choice copied to the clipboard. Windows clipboard history (Win+V) may keep it; it is cleared from the clipboard when the game exits."
            } catch { Write-Host 'Could not copy to the clipboard; select the code above and copy it.' -ForegroundColor Yellow }
        }
        Write-Host ''
        Write-Host 'How your friend reaches this PC (pick one):'
        Write-Host '  - a VPN such as Tailscale, ZeroTier or Radmin VPN on both PCs (recommended; use the VPN code);'
        Write-Host "  - port forwarding of UDP $Port on your router to this PC's LAN address (use the internet code);"
        Write-Host '  - the same home network (use the LAN code).'
        Write-Host 'Both players must have installed the same package version. Partner bodies support Mother Base, Afghanistan and Africa free roam.'
        Write-Host 'A new code is made every time you host; an old code stops working.'
        $rules = @(Get-CoopFirewallRows -ExePath $paths.Exe)
        Write-Lines -Lines (Format-CoopFirewallReport -Rules $rules -Profiles @(Get-CoopNetworkProfiles) -Port $Port)
        if ($interactive -and (Ask "[F] Add a Windows Firewall rule for MGSV on UDP $Port (administrator prompt)? [y/N]") -match '^(y|yes)$') {
            $block = @($rules | Where-Object { $_.Direction -eq 'Inbound' -and $_.Action -eq 'Block' })
            $remove = @()
            if ($block.Count) {
                foreach ($b in $block) { Write-Host "  Block rule: '$($b.DisplayName)'" -ForegroundColor Yellow }
                if ((Ask "Also remove these $($block.Count) Block rule(s)? They stop MGSV from receiving anything. [y/N]") -match '^(y|yes)$') { $remove = @($block | ForEach-Object { $_.Name }) }
            }
            # One fixed name: a later [F] on another port replaces the rule, and
            # friend-install.json records the only one Uninstall-Coop must offer.
            $display = 'mgs-coop play'
            if (Invoke-CoopElevated -Script (New-CoopFirewallScript -Port $Port -ExePath $paths.Exe -RemoveRuleNames $remove -DisplayName $display)) {
                Write-Host "Firewall rule '$display' added (Uninstall-Coop.bat offers to remove it)." -ForegroundColor Green
                $statePath = Join-Path $paths.Mod 'friend-install.json'
                $table = ConvertTo-CoopSessionTable -Session (Read-FriendJson $statePath)
                $table['firewall_rule'] = [ordered]@{ display_name = $display; port = $Port; added_utc = [DateTime]::UtcNow.ToString('o') }
                Write-FriendTextFile -Path $statePath -Text (($table | ConvertTo-Json -Depth 6) + "`n")
            } else { Write-Host 'The firewall rule was not added (cancelled or failed).' -ForegroundColor Yellow }
        }
        $peerAddress = ''
    } else {
        $parsed = $null
        if ($Transport -eq 'steam') {
            # ------------------------------------------------------------ 3b: join online
            Section 'Join online (Steam)'
            $steamParsed = $null
            for ($attempt = 1; $attempt -le 5; $attempt++) {
                $text = $Code
                if (!$text -or $attempt -gt 1) { $text = Ask "Paste the host's code (MGS2-...), or press Enter to wait for the host's Steam invite" }
                if (![string]$text -or !([string]$text).Trim()) { $steamWait = $true; break }
                # An MGS1 code is a LAN/VPN code: that join continues below.
                $udpParsed = ConvertFrom-CoopJoinCode $text
                if ($udpParsed.Status -eq 'ok') { $parsed = $udpParsed; $Transport = 'udp'; break }
                $steamParsed = ConvertFrom-CoopSteamJoinCode $text
                if ($steamParsed.Status -eq 'ok') {
                    if ($steamParsed.SteamId -ceq $steamSelf) { Write-Host 'That is your own code: ask your friend for theirs.' -ForegroundColor Yellow; $steamParsed = $null; continue }
                    break
                }
                Write-Host (Get-CoopSteamCodeProblem -Status $steamParsed.Status) -ForegroundColor Yellow
                $steamParsed = $null
            }
            if ($Transport -eq 'steam') {
                $peerAddress = ''
                if ($steamWait) {
                    $secret = ''
                    Write-Host 'Waiting for an invite: when MGSV runs, accept the host''s invite in the Steam overlay (Shift+Tab) or the Steam chat,' -ForegroundColor Green
                    Write-Host 'or paste a code in F9 -> Partner / Co-op -> Online.'
                } elseif ($steamParsed) {
                    $secret = $steamParsed.Secret
                    $steamPeer = $steamParsed.SteamId
                    Write-Host ('Code OK: host Steam account ...' + $steamPeer.Substring($steamPeer.Length - 4)) -ForegroundColor Green
                } else { throw 'No valid join code. Nothing was changed.' }
            } else {
                Write-Host 'That is a LAN/VPN code (MGS1): joining over the LAN/VPN link instead of Steam.' -ForegroundColor Yellow
            }
        }
    }
    if ($Role -eq 'Join' -and $Transport -eq 'udp') {
        # ------------------------------------------------------------ 3b: join (LAN/VPN)
        Section 'Join'
        for ($attempt = 1; !$parsed -and $attempt -le 5; $attempt++) {
            $text = Ask "Paste the host's join code (MGS1-...@...)"
            $candidate = ConvertFrom-CoopJoinCode $text
            if ($candidate.Status -eq 'ok') { $parsed = $candidate; break }
            Write-Host (Get-CoopCodeProblem -Status $candidate.Status) -ForegroundColor Yellow
            if ($candidate.Status -eq 'empty') { Write-Host 'Nothing was changed.'; exit 0 }
        }
        if (!$parsed -or $parsed.Status -ne 'ok') { throw 'No valid join code. Nothing was changed.' }
        $secret = $parsed.Secret
        $Port = $parsed.Port
        Write-Host "Code OK: host $($parsed.HostName):$($parsed.Port)" -ForegroundColor Green
        $peerAddress = $parsed.HostName
        if (!$parsed.IsIPv4) {
            Write-Host "Looking up $($parsed.HostName)..."
            $resolved = $null
            try { $resolved = @([Net.Dns]::GetHostAddresses($parsed.HostName) | Where-Object { $_.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork } | Select-Object -First 1) } catch { $resolved = $null }
            if (!$resolved -or !$resolved.Count) { throw 'That name has no IPv4 address; ask the host for an IPv4 or VPN code.' }
            $peerAddress = [string]$resolved[0].IPAddressToString
            Write-Host "$($parsed.HostName) is $peerAddress. If the host's address changes, run Join again."
        }
        if (!(Test-CoopPlayPeerAddress $peerAddress)) { throw "The host address $peerAddress cannot be used (it points to this PC or is not a normal address)." }
        $labRole = 'guest'
    }

    # ---------------------------------------------------------------- 4: activate
    Section 'Switching co-op on for the next game start'
    if ($World -gt 0) {
        Write-Host "World sync level $World (experimental): shared enemies/alert in free roam (2+), clock and weather (3)."
        Write-Host 'Shared world is on by default; -World 0 opts out. The lower level applies. Co-op save copies only.'
    }
    if ($Partner -eq 'native') {
        Write-Host 'Partner: native (the other player''s own movement, aiming and reloading; clips are the automatic fallback). -Partner clips turns it off.'
    } else {
        Write-Host 'Partner: animation clips only (-Partner clips).'
    }
    if ($Transport -eq 'steam') {
        $labText = New-CoopPlayLabIniText -Role $labRole -SaveDir $saveDir -World $World -Transport steam -SteamSelf $steamSelf -SteamPeer $steamPeer -SteamWait:$steamWait -SteamDispatch $SteamDispatch -Partner $Partner
    } else {
        $labText = New-CoopPlayLabIniText -Role $labRole -SaveDir $saveDir -Port $Port -PeerAddress $peerAddress -World $World -Partner $Partner
    }
    $coopText = New-CoopPlayCoopIniText -Character $character -Hud $Hud
    $utf8 = New-Object Text.UTF8Encoding $false
    $writes = @(@{ Name = 'lab.ini'; Path = $paths.LabIni; Bytes = $utf8.GetBytes($labText) })
    # A guest waiting for an invite has no secret yet (the invite or F9 brings the code).
    if (!$steamWait) { $writes += @(@{ Name = 'link-secret.txt'; Path = $paths.LinkSecret; Bytes = [Text.Encoding]::ASCII.GetBytes($secret + "`r`n") }) }
    $writes += @(
        @{ Name = 'coop.ini'; Path = $paths.CoopIni; Bytes = $utf8.GetBytes($coopText) },
        @{ Name = 'script-observe.enabled'; Path = $paths.Marker; Bytes = [Text.Encoding]::ASCII.GetBytes("offline-v1`n") }
    )
    Use-CoopPlayMutex -GameRoot $gameRoot -Script {
        if (!$script:session) { New-Session }
        $script:session.role = $labRole
        $script:session.port = $Port
        $script:session.transport = $Transport
        $script:session.clip_sha256 = $clipSha
        foreach ($w in $writes) {
            if (Test-Path -LiteralPath $w.Path) { throw "mgs-coop\$($w.Name) appeared while Play-Coop was preparing; nothing more was written." }
            if ($w.Name -eq 'script-observe.enabled') {
                # The checks of tools/set-session-mode.ps1 before a marker is written.
                $rec = Read-FriendJson $paths.Record
                if (!$rec -or (Get-FriendProperty $rec 'product') -ne 'mgs-coop-original-loader-probe' -or [string](Get-FriendProperty $rec 'game_directory' '') -ne $gameRoot) { throw 'Installation record does not match this mod and directory.' }
                if ($script:SupportedLoaderVersions -notcontains [string](Get-FriendProperty $rec 'version' '')) { throw 'The installed loader does not support two-PC play.' }
                if ((Get-Sha256 $paths.Loader) -ne ([string](Get-FriendProperty $rec 'dll_sha256' '')).ToUpperInvariant()) { throw 'Installed loader differs from its ownership record.' }
                Assert-CoopReleaseLoader -ExpectedManifestSha256 $packageManifestSha256
                if (!(Test-FileHash -Path $paths.Archive -Expected $archiveSha256)) { throw 'Expected two-player archive is missing or changed.' }
            }
            # Recorded first: a crash right after the write still leaves an owned file.
            $script:session.files = @($script:session.files) + @([ordered]@{ name = $w.Name; sha256 = (Get-CoopTextSha256 -Bytes $w.Bytes) })
            Save-Session
            $temp = $w.Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
            [IO.File]::WriteAllBytes($temp, [byte[]]$w.Bytes)
            [IO.File]::Move($temp, $w.Path)
        }
    }
    $activated = $true
    $watchdog = 0
    # (tests/friend_package_tests.ps1 starts some sessions without the helper.)
    if ($env:MGS_COOP_TEST_NO_WATCHDOG -ne '1') { $watchdog = Start-CoopWatchdog -ScriptPath $PSCommandPath -GameRoot $gameRoot -SessionId $session.session_id }
    $session.watchdog_pid = $watchdog
    $session.watchdog_start_utc = Get-CoopProcessStartUtc -Id $watchdog
    Save-Session
    if ($watchdog) { Write-Host 'Co-op is switched on until the game exits (a hidden helper switches it off even if this window is closed).' }
    else { Write-Host 'Warning: the background helper could not start. Keep this window open until you quit MGSV; it switches co-op off.' -ForegroundColor Yellow }
    if ($env:MGS_COOP_TEST_EXIT_AFTER_ACTIVATE -eq '1') {
        # tests/friend_package_tests.ps1 only: behave like a closed window.
        $skipCleanup = $true
        Write-Host 'TEST: leaving after the activation.'
        exit 0
    }

    # ---------------------------------------------------------------- 5: launch
    Section 'Starting MGSV through Steam'
    $logNames = @('early.log', 'steam-observer.log', 'script-observer.log')
    $offsets = @{}
    foreach ($n in $logNames) { $offsets[$n] = Get-FileLengthOrZero (Join-Path $paths.Mod $n) }
    # (tests/friend_package_tests.ps1 starts a stand-in game itself.)
    if ($env:MGS_COOP_TEST_NO_LAUNCH -ne '1') { Start-Process 'steam://rungameid/287700' }
    Write-Host 'Waiting for MGSV (answer any Steam dialog). [Q] stops and switches co-op off.'
    while ($true) {
        $games = @(Get-GameProcessInDirectory -Directory $gameRoot | Where-Object { !$_.Unknown })
        if ($games.Count) { $gamePid = [int]$games[0].Id; break }
        $key = $null
        try { if ([Console]::KeyAvailable) { $key = [Console]::ReadKey($true).Key } } catch { $key = $null }
        if ($key -eq 'Q') { Write-Host 'Stopped before the game started.'; exit 0 }
        Start-Sleep -Milliseconds 500
    }
    Write-Host "MGSV started (process $gamePid). Checking that co-op and the save copy are active..."
    $texts = @{}
    foreach ($n in $logNames) { $texts[$n] = '' }
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    $evidence = $null
    while ($true) {
        foreach ($n in $logNames) {
            $d = Read-CoopLogDelta -Path (Join-Path $paths.Mod $n) -Offset $offsets[$n]
            $texts[$n] += $d.Text
            $offsets[$n] = $d.NewOffset
        }
        $final = [DateTime]::UtcNow -ge $deadline
        $evidence = Get-CoopLaunchEvidence -EarlyLog $texts['early.log'] -SteamLog $texts['steam-observer.log'] -ObserverLog $texts['script-observer.log'] -SaveDir $saveDir -Final:$final -Transport $Transport
        if ($evidence.State -ne 'pending') { break }
        if (!(Test-CoopProcessAlive -Id $gamePid)) { break }
        Start-Sleep -Seconds 1
    }
    $banner = $null
    switch ($evidence.State) {
        'ok' { Write-Host 'Co-op is active and the game plays from the save COPY.' -ForegroundColor Green }
        'unsafe' {
            $banner = 'Co-op did NOT start and save protection is NOT active: this game is normal offline play on your REAL saves. Quit MGSV now if you do not want that.'
        }
        'old_loader' { Write-Host 'The save copy is active, but the installed loader is too old for two-PC play (no play= line). Install the package your friend uses.' -ForegroundColor Yellow }
        default { Write-Host 'Co-op did not confirm its start:' -ForegroundColor Yellow }
    }
    foreach ($m in @($evidence.Missing)) { Write-Host "  missing: $m" -ForegroundColor Yellow }
    foreach ($m in @($evidence.Problems)) { Write-Host "  $m" -ForegroundColor Yellow }
    if ($banner) { Write-Host ''; Write-Host $banner -ForegroundColor White -BackgroundColor DarkRed; Write-Host '' }

    # ---------------------------------------------------------------- status
    Write-Host 'Not in the same place? Both games must Continue into the same Mother Base or free roam; Share-Save.bat gives both players the same progress (FRIEND-GUIDE.txt section 3).'
    Write-Host 'Meet in Mother Base or Afghanistan/Africa free roam. [Q] closes this window (co-op stays on until MGSV exits).'
    $last = ''
    $bannerAt = [DateTime]::UtcNow
    while (Test-CoopProcessAlive -Id $gamePid) {
        $status = Read-CoopStatusFile -Path $paths.LinkStatus -ExpectedPid $gamePid
        $line = Format-CoopStatusLine -Status $status -Role $labRole -OwnBuild $ownBuild
        if ($line -ne $last) { Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $line); $last = $line }
        if ($banner -and ([DateTime]::UtcNow - $bannerAt).TotalSeconds -ge 30) { Write-Host $banner -ForegroundColor White -BackgroundColor DarkRed; $bannerAt = [DateTime]::UtcNow }
        $key = $null
        try { if ([Console]::KeyAvailable) { $key = [Console]::ReadKey($true).Key } } catch { $key = $null }
        if ($key -eq 'Q') {
            # Only a live helper may take over the cleanup.
            if (Test-CoopProcessAlive -Id ([int]$session.watchdog_pid) -StartUtc ([string]$session.watchdog_start_utc)) { Write-Host 'Closing this window; the helper switches co-op off when MGSV exits.'; $skipCleanup = $true; break }
            Write-Host 'No helper is running: keep this window open until MGSV exits (it switches co-op off then).' -ForegroundColor Yellow
        }
        Start-Sleep -Seconds 1
    }
    if (!$skipCleanup) { Write-Host 'MGSV exited.' }
} finally {
    if ($activated -or $session) {
        if ($skipCleanup) { }
        elseif (@(Get-GameProcessInDirectory -Directory $gameRoot).Count) {
            # The loop ended early (an error) while the game runs: the cleanup
            # stays with a live helper, or this window waits for the game.
            $helper = $session -and (Test-CoopProcessAlive -Id ([int]$session.watchdog_pid) -StartUtc ([string]$session.watchdog_start_utc))
            if (!$helper -and $session -and $env:MGS_COOP_TEST_NO_WATCHDOG -ne '1') {
                $session.watchdog_pid = Start-CoopWatchdog -ScriptPath $PSCommandPath -GameRoot $gameRoot -SessionId $session.session_id
                $session.watchdog_start_utc = Get-CoopProcessStartUtc -Id $session.watchdog_pid
                try { Save-Session } catch { }
                $helper = Test-CoopProcessAlive -Id ([int]$session.watchdog_pid) -StartUtc ([string]$session.watchdog_start_utc)
            }
            if ($helper) { Write-Host 'MGSV is still running: co-op is switched off when it exits.' -ForegroundColor Yellow }
            else {
                Write-Host 'MGSV is still running and no helper is: keep this window open; co-op is switched off when MGSV exits.' -ForegroundColor Yellow
                while (@(Get-GameProcessInDirectory -Directory $gameRoot).Count) { Start-Sleep -Seconds 2 }
                $r = Invoke-CoopPlayCleanup -GameRoot $gameRoot
                Write-CleanupResult $r
            }
        } else {
            $r = Invoke-CoopPlayCleanup -GameRoot $gameRoot
            Write-CleanupResult $r
        }
    }
}
exit 0
