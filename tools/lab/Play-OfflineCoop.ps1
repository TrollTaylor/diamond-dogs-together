<#
.SYNOPSIS
One-click offline co-op test on this PC: two MGSV copies side by side, each
showing the other player as a second character.

.DESCRIPTION
Runs the offline lab path (docs/testing-2026-10-04.md) with the appearance
sync of docs/partner-anim-appearance.md:
  1. Prepare-LabInstance.ps1 -Offline with the partner synced over our own link
     from the start: stage 3, source net, look net (each copy shows the other
     player's own character). Default: the ANIMATED partner (dm=map, live pass
     2026-10-05) -- the smooth xform movement plus direct-motion clips that
     follow the other player's stance and speed (walk, run, dash, crouch,
     prone, crawl; docs/partner-anim-appearance.md "D-map"); -Animated is the
     same. -ProvenWarp (or [W]): the proven Warp movement (Lua Warp every tick,
     input off, dm off; no animation). Other experiments:
     -InputDrive (or [I]): the round 4 input drive
     (apply=input input=net isend=1, words hpr, hold own, recon track), which
     does not start locomotion yet. -Experimental: the round 2 smooth movement
     (hybrid) with stance sync (anim net, aset qs). Test condition: the GUEST
     plays Quiet (lab.json offline.characters.guest), the host keeps its
     character. Your real saves are backed up; both copies play on save
     COPIES. No server, no login, no game session.
  2. Start-Lab.ps1 -Offline: host window left, guest window right.
  3. Presses Enter through startup until the saved supported scene loads
     (skip with -NoAutoTitle and click through yourself).
  4. Waits until both copies report the partner system ready, then shows a live
     status line. Console keys: [S] screenshots, [D] animated partner
     (dm=map), [I] input drive, [W] back to the proven Warp movement (input
     off, dm off), [P] input drive with the partner pinned to the remote pose
     (recon pin), [M] smooth movement on/off, [A] stance sync on/off, [R]
     re-send the current settings (also re-arms a failed dm=map), [C]
     component census, [O] partner off, [Q] quit and clean up.
  5. On exit (Q, closing a game window, or Ctrl+C): closes both games, checks
     your real save files and switches the lab config off again so normal
     launches behave normally.

Rehearsing the two-PC play mode on this PC (docs/two-pc-play.md):
  -Lan        both copies bind all addresses and the guest reaches the host
              through this PC's LAN IPv4 (-LanAddress, default: detected),
              with a random link secret in both copies (the lab secret only
              works on 127.0.0.1). Windows Firewall may ask about mgsvtpp.exe:
              allow it, or the copies cannot reach each other.
  -WanSim good|bad|awful
              each copy impairs its own outbound packets with the lab.json
              wanSim preset (latency, jitter, burst loss, duplicates,
              reordering) and uses the adaptive partner delay.
Without these switches the lab.ini and coop.ini are exactly as before.

World sync (docs/world-sync-protocol.md; default level 3):
  -World 1..3 shared world between the copies: 1 plumbing only, 2 also shared
              enemy outcomes and alert in free roam, 3 also clock/weather,
              host-driven mission travel and Continue alignment.
              Adds world=N to both lab.ini files. -World 0 opts out.
              Console [G]: travel to Afghanistan through the game's APIs.
              At level 3 the host travels and the guest follows automatically.
  -WorldProbe <probe>
              a lab probe 10 s after each copy's first mission start (needs
              -World); tools/lab/Set-WorldTrigger.ps1 sends probes at runtime.

Controls while playing: keyboard and mouse go to whichever game window you
clicked last. An Xbox-style controller always drives the GUEST window (right),
even while the host window has focus, so one person can test both at once.

.EXAMPLE
pwsh -File tools/lab/Play-OfflineCoop.ps1
pwsh -File tools/lab/Play-OfflineCoop.ps1 -ProvenWarp
pwsh -File tools/lab/Play-OfflineCoop.ps1 -Experimental
pwsh -File tools/lab/Play-OfflineCoop.ps1 -Lan -WanSim bad
pwsh -File tools/lab/Play-OfflineCoop.ps1 -World 3 -KeepLabEnabled
#>
[CmdletBinding()]
param(
    # Do not press Enter through the startup screens automatically.
    [switch]$NoAutoTitle,
    # How long to wait for both copies to reach a supported scene and report ready.
    [int]$ReadyTimeoutSec = 900,
    # Leave lab.ini/coop.ini enabled after quitting (default: switch them off).
    [switch]$KeepLabEnabled,
    # Automation: quit automatically this many seconds after both are ready (0 = interactive).
    [int]$ExitAfterReadySec = 0,
    # Start with the round 2 native paths on (smooth movement and stance
    # sync) instead of the input drive.
    [switch]$Experimental,
    # Start with the proven Warp movement (input off, dm off, no animation)
    # instead of the animated partner.
    [switch]$ProvenWarp,
    # Start with the experimental input drive (round 4) instead of the proven Warp movement.
    [switch]$InputDrive,
    # dm=map: xform movement + direct-motion clips following the other player.
    # The default since the 2026-10-05 live pass; kept for compatibility.
    [switch]$Animated,
    # Native puppet (lab only, opt-in; docs/native-puppet.md): the animated
    # partner as the fallback plus partner_drive=native with the job deferral
    # (loco stage). Unqualified until the coordinator's hidden runs pass.
    [switch]$Native,
    # One-PC LAN rehearsal of the two-PC play mode (net_bind=any, LAN address, link secret).
    [switch]$Lan,
    # -Lan: this PC's LAN IPv4 (default: the first IPv4 of an Up adapter with a gateway).
    [string]$LanAddress,
    # WAN rehearsal preset from lab.json wanSim (one-way impairment per copy).
    [ValidateSet('off', 'good', 'bad', 'awful')][string]$WanSim = 'off',
    # Shared enemies, time/weather and host travel by default; -World 0 opts out.
    [ValidateRange(0, 3)][int]$World = 3,
    # A world sync lab probe (needs -World 1..3).
    [ValidateSet('none', 'p1', 'p3', 'p3b', 'p4', 'p12', 'p17', 'race_kill', 'remote_area_kill', 'kill_near', 'sleep_near',
        'fulton_near', 'alert_near', 'weather_rain', 'clock_jump', 'clock_fast',
        'shared_death', 'host_continue', 'shared_checkpoint')][string]$WorldProbe = 'none'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# The same refusal as Prepare-LabInstance and Get-LabInstanceSettings, before anything is prepared.
if ($LanAddress -and !$Lan) { throw '-LanAddress applies only with -Lan.' }
if ($WorldProbe -ne 'none' -and $World -lt 1) { throw '-WorldProbe needs -World 1..3.' }
$lab = $PSScriptRoot
$repo = Split-Path -Parent (Split-Path -Parent $lab)
Import-Module (Join-Path $lab 'LabCommon.psm1') -Force

function Write-Step([string]$Text) { Write-Host "`n== $Text" -ForegroundColor Cyan }
function Write-Note([string]$Text) { Write-Host "   $Text" -ForegroundColor Gray }

$config = Get-LabConfig -ConfigPath (Join-Path $lab 'lab.json')
$dirs = [ordered]@{ host = $config.Paths.hostDir; guest = $config.Paths.guestDir }

# ------------------------------------------------------------------ preflight
Write-Step 'Checking prerequisites'
if (!(Get-Process steam -ErrorAction SilentlyContinue)) { throw 'Steam is not running. Start Steam and log in, then run this again.' }
foreach ($role in $dirs.Keys) {
    if (!(Test-Path -LiteralPath (Join-Path $dirs[$role] 'mgsvtpp.exe'))) { throw "[$role] mgsvtpp.exe not found in $($dirs[$role])." }
    $running = Select-ProcessInDirectory -Processes @(Get-Process mgsvtpp -ErrorAction SilentlyContinue) -Directory $dirs[$role]
    if (@($running).Count) { throw "[$role] MGSV is already running from $($dirs[$role]). Close it first." }
}
$loader = Join-Path $repo 'build\native\dinput8.dll'
if (!(Test-Path -LiteralPath $loader)) {
    Write-Note 'The mod is not built yet; building it now (a few minutes)...'
    & pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'tools\build.ps1') -Configuration Release -BuildOnly
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $loader)) { throw 'Build failed; run tools/build.ps1 to see why.' }
}
Write-Note 'Steam is running, both game folders are present and closed.'

# Log reader: only lines written after this point count.
$logs = [ordered]@{}
foreach ($role in $dirs.Keys) {
    $path = Join-Path $dirs[$role] 'mgs-coop\script-observer.log'
    $logs[$role] = [pscustomobject]@{ Path = $path; Offset = (Get-FileLengthOrZero $path); Tail = '' }
}
$state = [ordered]@{}
foreach ($role in $dirs.Keys) { $state[$role] = New-CoopPartnerDetails }
function Update-State {
    foreach ($role in $logs.Keys) {
        $log = $logs[$role]; $s = $state[$role]
        if (!(Test-Path -LiteralPath $log.Path)) { continue }
        $stream = [IO.File]::Open($log.Path, 'Open', 'Read', 'ReadWrite')
        try {
            if ($stream.Length -lt $log.Offset) { $log.Offset = 0 }
            [void]$stream.Seek($log.Offset, 'Begin')
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::ASCII)
            $text = $log.Tail + $reader.ReadToEnd()
            $log.Offset = $stream.Position
        } finally { $stream.Dispose() }
        $cut = $text.LastIndexOf("`n")
        if ($cut -lt 0) { $log.Tail = $text; continue }
        $log.Tail = $text.Substring($cut + 1)
        # The line parser (partner state, look, clip, input, recon) is shared with
        # the two-PC launcher: tools/CoopPlay.psm1 Update-CoopPartnerDetails.
        foreach ($line in $text.Substring(0, $cut).Split("`n")) { Update-CoopPartnerDetails -Details $s -Line $line }
    }
    foreach ($role in $dirs.Keys) {
        $alive = @(Select-ProcessInDirectory -Processes @(Get-Process mgsvtpp -ErrorAction SilentlyContinue) -Directory $dirs[$role]).Count -gt 0
        $state[$role].Exited = !$alive
    }
}
function Format-Role([string]$role) { return Format-CoopPartnerDetails -Details $state[$role] -Role $role }

# The partner settings: look net always. Movement: the animated partner (dm=map)
# unless -Experimental, -InputDrive or -ProvenWarp; [D] / [I] / [P] / [W] / [M] / [A]
# switch, [R] re-sends. Every key sends the whole line to both copies.
$startMove = if ($Experimental) { 'hybrid' } elseif ($InputDrive) { 'input' } elseif ($ProvenWarp) { 'warp' } else { 'map' }
$experiment = [ordered]@{ Move = $startMove; Anim = $(if ($Experimental) { 'net' } else { 'off' }); Recon = 'track' }
# The input drive (both copies): the remote pad drives the partner's real
# locomotion; hold own (prone), words hpr (stance taps), keyboard dash and walk.
# correct=3000 runaway=pin are explicit: a correct=0 left by an earlier
# diagnostic (R0, I3) would otherwise turn the reconciliation off silently.
$script:InputTrigger = 'stage=3 source=net apply=input anim=off input=net cam=sandbox hold=own words=hpr bits=0x2200006 isend=1 recon={0} correct=3000 runaway=pin slot=body dm=off look=net'
# dm=map (the animated partner): xform movement, no anim arrays, no input
# window; every other line turns dm off (the mapper releases its clip first).
function Get-SyncTrigger {
    if ($experiment.Move -eq 'input') { return ($script:InputTrigger -f $experiment.Recon) }
    if ($experiment.Move -eq 'map') { return 'stage=3 source=net apply=xform reset=n anim=off input=off isend=0 dm=map look=net' }
    $move = if ($experiment.Move -eq 'hybrid') { 'apply=hybrid reset=n' } else { 'apply=warp' }
    $anim = if ($experiment.Anim -eq 'net') { 'anim=net aset=qs' } else { 'anim=off' }
    return "stage=3 source=net $move $anim input=off isend=0 dm=off look=net"
}
function Send-SyncTrigger([string]$Why) {
    $text = Get-SyncTrigger
    Write-Note "$Why -> both copies: $text"
    & (Join-Path $lab 'Set-PartnerTrigger.ps1') -Role both -Text $text -Force | ForEach-Object { Write-Note $_ }
}
$started = $false
try {
    # ------------------------------------------------------------- prepare
    $mode = if ($Experimental) { 'smooth movement + stance sync ON (-Experimental)' }
            elseif ($InputDrive) { 'INPUT DRIVE (experimental, -InputDrive); [W] falls back to the proven Warp movement' }
            elseif ($ProvenWarp) { 'proven Warp movement (-ProvenWarp, no animation); [D] switches to the animated partner (dm=map)' }
            else { 'ANIMATED partner (dm=map, default); [W] falls back to the proven Warp movement' }
    Write-Step "Preparing both game copies (offline, partner sync on, look net; $mode)"
    $partnerSettings = @('partner_stage=3', 'partner_source=net', 'partner_look=net',
        "partner_apply=$($experiment.Move)", "partner_anim=$($experiment.Anim)")
    if ($Experimental) { $partnerSettings += @('partner_aset=qs', 'partner_reset=n') }
    if ($experiment.Move -eq 'map') {
        # dm=map: only the dm=map keys; everything else comes from the lab defaults
        # and lab.json "partner", as before. With the shipped lab.json this equals the
        # two-PC play keys (tools/CoopPlay.psm1 Get-PlayPartnerDefaults; lab_scripts_tests).
        $partnerSettings = @('partner_stage=3', 'partner_source=net', 'partner_look=net', 'partner_apply=xform', 'partner_anim=off', 'partner_reset=n', 'partner_dm=map')
    }
    if ($Native) {
        if ($experiment.Move -ne 'map') { throw '-Native runs on the animated partner (dm=map); drop -InputDrive/-ProvenWarp/-Experimental.' }
        $partnerSettings += @('partner_drive=native', 'partner_input_send=1', 'partner_input_hook=1', 'partner_input_hold=own',
            'partner_input_words=hpr', 'partner_input_wrap=window', 'partner_input_cam=sandbox', 'partner_input_local=main',
            'partner_recon=track', 'partner_input_slot=body', 'partner_input_defer=on', 'partner_input_mask=0x2200006', 'partner_native_aim=off')
        Write-Note 'NATIVE PUPPET (lab, unqualified): partner_drive=native with the job deferral; dm=map stays the automatic fallback.'
    }
    if ($experiment.Move -eq 'input') {
        $partnerSettings += @('partner_input=net', 'partner_input_send=1', 'partner_input_cam=sandbox', 'partner_input_hold=own',
            'partner_input_words=hpr', 'partner_input_mask=0x2200006', 'partner_recon=track', 'partner_input_correct_mm=3000',
            'partner_recon_runaway=pin', 'partner_input_slot=body')
    }
    # Shared world by default; the explicit level 0 opt-out writes no world key.
    if ($World -gt 0) { $partnerSettings += "world=$World" }
    if ($WorldProbe -ne 'none') { $partnerSettings += "world_probe=$WorldProbe" }
    $rehearsal = @{}
    if ($Lan) { $rehearsal['Lan'] = $true; if ($LanAddress) { $rehearsal['LanAddress'] = $LanAddress } }
    if ($WanSim -ne 'off') { $rehearsal['WanSim'] = $WanSim; Write-Note "WAN rehearsal: lab.json wanSim.$WanSim on both copies (one-way impairment each, adaptive partner delay)." }
    & (Join-Path $lab 'Prepare-LabInstance.ps1') -Offline -PartnerSetting $partnerSettings @rehearsal |
        ForEach-Object { Write-Note $_ }

    # --------------------------------------------------------------- start
    Write-Step 'Starting the host (left) and the guest (right)'
    $started = $true
    & (Join-Path $lab 'Start-Lab.ps1') -Offline -Lan:$Lan | ForEach-Object { Write-Note $_ }

    # ---------------------------------------------------------- title screens
    $deadline = (Get-Date).AddSeconds($ReadyTimeoutSec)
    if (!$NoAutoTitle) {
        Write-Step 'Pressing Enter through the startup screens (a "Failed to log in to server" notice is normal offline)'
        Write-Note 'Please leave startup input to the launcher until both copies reach their saved scene.'
        while ((Get-Date) -lt $deadline) {
            Update-State
            $pending = @($dirs.Keys | Where-Object { !$state[$_].Mission -and !$state[$_].Exited })
            if (!$pending.Count) { break }
            foreach ($role in $pending) {
                try { & (Join-Path $lab 'Send-LabInput.ps1') -Role $role -Keys enter -GapMs 200 *> $null } catch { Write-Verbose "[$role] Enter not delivered: $_" }
            }
            Start-Sleep -Seconds 3
        }
    } else {
        Write-Step 'Click through the startup screens in BOTH windows: OK, OK, then Continue'
    }

    # ---------------------------------------------------------------- ready
    Write-Step 'Waiting for a supported scene and the partner system in both copies'
    $spaceAt = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        Update-State
        if (@($dirs.Keys | Where-Object { $state[$_].Exited }).Count) { throw 'A game window closed before it was ready.' }
        if (@($dirs.Keys | Where-Object { !$state[$_].Ready }).Count -eq 0) { break }
        # After loading, Mother Base can wait on a "Space: Resume Game" tips screen;
        # the partner system only starts once the mission really begins.
        if (!$NoAutoTitle -and (Get-Date) -ge $spaceAt) {
            foreach ($role in @($dirs.Keys | Where-Object { $state[$_].Mission -and !$state[$_].Ready })) {
                try { & (Join-Path $lab 'Send-LabInput.ps1') -Role $role -Keys space -GapMs 200 *> $null } catch { Write-Verbose "[$role] Space not delivered: $_" }
            }
            $spaceAt = (Get-Date).AddSeconds(6)
        }
        Start-Sleep -Seconds 2
    }
    Update-State
    foreach ($role in $dirs.Keys) { if (!$state[$role].Ready) { Write-Warning ("Not ready yet: " + (Format-Role $role)) } }

    Write-Host ''
    $bothReady = @($dirs.Keys | Where-Object { !$state[$_].Ready }).Count -eq 0
    if ($bothReady) {
        Write-Host "  BOTH COPIES READY: host mission $($state['host'].Mission), guest mission $($state['guest'].Mission)" -ForegroundColor Green
    } else {
        Write-Host '  WAITING FOR A SUPPORTED SCENE; the status line will show progress.' -ForegroundColor Yellow
    }
    Write-Host '  - If a window shows "Resume Game", click into it and press Space.'
    Write-Host '  - Keyboard + mouse: click the window you want to control (host = left, guest = right).'
    Write-Host '  - A controller always drives the GUEST (right) window, even while the host has focus.'
    Write-Host '  - Walk around: each window shows the OTHER player as a second character.'
    Write-Host '    Test condition: the guest (right) plays QUIET, so the host window should show a Quiet partner'
    Write-Host '    and the guest window the host''s own character.'
    Write-Host "  Partner movement: $($experiment.Move); stance sync: $($experiment.Anim) (docs/partner-anim-appearance.md 'Round 4')."
    Write-Host '    Input drive: the partner moves with its own locomotion from the other player''s pad: walk, run, crouch (tap),'
    Write-Host '    prone (hold the stance key ~0.2 s). The status line shows recon err (cm), snaps/places and RUNAWAY(pinned).'
    Write-Host '    Animated partner ([D], dm=map): smooth movement plus walk/run/dash/crouch/prone clips that follow the other'
    Write-Host '    player; the status line shows the clip. This is the default (live pass 2026-10-05).'
    Write-Host '    If the partner misbehaves or a game hangs: [W] switches both copies back to the proven Warp movement.'
    Write-Host '  Console keys (click this window first):  [S] screenshots   [D] animated partner (dm=map)   [I] input drive'
    Write-Host '                                           [P] input drive, partner pinned'
    Write-Host '                                           [W] proven Warp (input off)   [M] smooth movement   [A] stance sync'
    Write-Host '                                           [R] re-send settings   [C] component census   [O] partner off   [Q] quit + clean up'
    if ($World -gt 0) {
        Write-Host "  WORLD SYNC level $World (experimental, docs/world-sync-protocol.md): shared enemies and alert in free roam (2+)," -ForegroundColor Yellow
        Write-Host '    clock/weather, host-driven travel and Continue alignment (3). [G] travels to Afghanistan (30010); a reward' -ForegroundColor Yellow
        Write-Host '    screen after the boarding may need Space in both windows (Send-LabInput.ps1 -Role both -Keys space).' -ForegroundColor Yellow
        if ($WorldProbe -ne 'none') { Write-Host "    Probe $WorldProbe runs 10 s after each copy's first mission start." -ForegroundColor Yellow }
    }
    Write-Host ''

    $readyAt = Get-Date
    $last = ''
    while ($true) {
        Update-State
        $line = ($dirs.Keys | ForEach-Object { Format-Role $_ }) -join '   |   '
        if ($line -ne $last) { Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $line); $last = $line }
        if (@($dirs.Keys | Where-Object { $state[$_].Exited }).Count) { Write-Host 'A game window was closed; cleaning up.' -ForegroundColor Yellow; break }
        if ($ExitAfterReadySec -gt 0 -and ((Get-Date) - $readyAt).TotalSeconds -ge $ExitAfterReadySec) { Write-Host 'Automatic exit.'; break }
        $key = $null
        try { if ([Console]::KeyAvailable) { $key = [Console]::ReadKey($true).Key } } catch { $key = $null }
        switch ($key) {
            'Q' { Write-Host 'Quitting...' -ForegroundColor Yellow; break }
            'S' { & (Join-Path $lab 'Get-LabScreens.ps1') -Label play | ForEach-Object { Write-Note $_ } }
            'R' { Send-SyncTrigger 're-sync' }
            'M' { $experiment.Move = if ($experiment.Move -eq 'hybrid') { 'warp' } else { 'hybrid' }; Send-SyncTrigger "movement $($experiment.Move)" }
            'A' {
                if ($experiment.Move -eq 'input') { Write-Note 'stance sync applies to the Warp/smooth movement; the input drive animates the partner itself ([W] first)' }
                elseif ($experiment.Move -eq 'map') { Write-Note 'stance sync is off while dm=map animates the partner ([W] or [M] first)' }
                else { $experiment.Anim = if ($experiment.Anim -eq 'net') { 'off' } else { 'net' }; Send-SyncTrigger "stance sync $($experiment.Anim)" }
            }
            'D' { $experiment.Move = 'map'; $experiment.Anim = 'off'; Send-SyncTrigger 'animated partner (dm=map)' }
            'W' {
                # Back to the proven path AND turn the input window off.
                $experiment.Move = 'warp'; $experiment.Anim = 'off'
                Send-SyncTrigger 'proven Warp movement'
            }
            'I' { $experiment.Move = 'input'; $experiment.Anim = 'off'; $experiment.Recon = 'track'; Send-SyncTrigger 'input drive' }
            'P' { $experiment.Move = 'input'; $experiment.Anim = 'off'; $experiment.Recon = 'pin'; Send-SyncTrigger 'input drive, partner pinned (recon pin)' }
            'C' { & (Join-Path $lab 'Set-PartnerTrigger.ps1') -Role both -Text 'census=1' -Force | ForEach-Object { Write-Note $_ } }
            'O' { & (Join-Path $lab 'Set-PartnerTrigger.ps1') -Role both -Text 'stop' -Force | ForEach-Object { Write-Note $_ } }
            'G' {
                if ($World -gt 0) {
                    $travelRole = if ($World -ge 3) { 'host' } else { 'both' }
                    & (Join-Path $lab 'Set-WorldTrigger.ps1') -Role $travelRole -Text 'goto=30010' -Force | ForEach-Object { Write-Note $_ }
                }
                else { Write-Note 'world sync is off (start with -World 1..3)' }
            }
        }
        if ($key -eq 'Q') { break }
        Start-Sleep -Milliseconds 500
    }
} finally {
    if ($started) {
        Write-Step 'Closing both games and checking your real saves'
        try { & (Join-Path $lab 'Stop-Lab.ps1') *>&1 | ForEach-Object { Write-Note "$_" } } catch { Write-Warning "Stop-Lab: $_" }
    }
    if (!$KeepLabEnabled) {
        Write-Step 'Switching the lab config off (normal launches behave normally again)'
        try { & (Join-Path $lab 'Prepare-LabInstance.ps1') -Disable *>&1 | Select-Object -Last 1 | ForEach-Object { Write-Note "$_" } } catch { Write-Warning "Disable: $_" }
    }
    Write-Host "`nDone. Logs: <game folder>\mgs-coop\script-observer.log; screenshots: build\lab\runs\<latest>\screens" -ForegroundColor Cyan
}
