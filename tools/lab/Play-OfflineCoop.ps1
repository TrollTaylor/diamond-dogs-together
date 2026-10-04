<#
.SYNOPSIS
One-click offline co-op test on this PC: two MGSV copies side by side, each
showing the other player as a second character.

.DESCRIPTION
Runs the offline lab path (docs/testing-2026-10-04.md) with the appearance
sync and, step by step, the animation experiments of
docs/partner-anim-appearance.md:
  1. Prepare-LabInstance.ps1 -Offline with the partner synced over our own link
     from the start: stage 3, source net, look net (each copy shows the other
     player's own character) and the PROVEN movement (Lua Warp every tick, no
     native animation calls). Test condition: the GUEST plays Quiet (lab.json
     offline.characters.guest), the host keeps its character. Your real saves
     are backed up; both copies play on save COPIES. No server, no login, no
     game session.
     The unverified native paths stay off until you turn them on, one at a
     time, so a crash or hang can be attributed to the step that caused it
     (the log shows each partner_stage=changed): [M] smooth movement (hybrid:
     one Warp to place the partner, then the native body transform without
     Warp's resets; falls back to Warp on any refusal), [A] stance sync (the
     partner's stance/locomotion fields from the network frames: anim net,
     aset qs). -Experimental starts with both on.
  2. Start-Lab.ps1 -Offline: host window left, guest window right.
  3. Presses Enter through the startup screens of both until Mother Base loads
     (skip with -NoAutoTitle and click through yourself).
  4. Waits until both copies report the partner system ready, then shows a live
     status line. Console keys: [S] screenshots, [M] smooth movement on/off,
     [A] stance sync on/off, [W] back to the proven Warp movement without
     animation calls, [R] re-send the current settings, [C] component census,
     [O] partner off, [Q] quit and clean up.
  5. On exit (Q, closing a game window, or Ctrl+C): closes both games, checks
     your real save files and switches the lab config off again so normal
     launches behave normally.

Controls while playing: keyboard and mouse go to whichever game window you
clicked last. An Xbox-style controller always drives the GUEST window (right),
even while the host window has focus, so one person can test both at once.

.EXAMPLE
pwsh -File tools/lab/Play-OfflineCoop.ps1
pwsh -File tools/lab/Play-OfflineCoop.ps1 -Experimental
#>
[CmdletBinding()]
param(
    # Do not press Enter through the startup screens automatically.
    [switch]$NoAutoTitle,
    # How long to wait for both copies to reach Mother Base and report ready.
    [int]$ReadyTimeoutSec = 900,
    # Leave lab.ini/coop.ini enabled after quitting (default: switch them off).
    [switch]$KeepLabEnabled,
    # Automation: quit automatically this many seconds after both are ready (0 = interactive).
    [int]$ExitAfterReadySec = 0,
    # Start with the unverified native paths on (smooth movement and stance
    # sync) instead of turning them on one at a time with [M] / [A].
    [switch]$Experimental
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
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
foreach ($role in $dirs.Keys) {
    $state[$role] = [pscustomobject]@{
        Mission = $false; Ready = $false; Refused = ''; Shown = $false; Loaded = $false
        Link = 'none'; Rx = 0; Age = 0; Crash = 0; Exited = $false
        Look = ''; Move = ''; Stance = ''; Character = ''; Fault = $false; Mode = ''
        Input = 'off'; InputArmed = $false; InputPerS = 0; InputStale = $false; InputFault = ''
    }
}
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
        foreach ($line in $text.Substring(0, $cut).Split("`n")) {
            if ($line -match 'allocation_enter.*mission=30050') { $s.Mission = $true }
            elseif ($line -match '^partner_cfg=ready') { $s.Ready = $true; $s.Refused = '' }
            elseif ($line -match '^partner_cfg=refused; reason=([^;\s]+)') { $s.Refused = $Matches[1] }
            elseif ($line -match '^partner_realized=1') { $s.Shown = $true }
            elseif ($line -match '^partner_realized=0') { $s.Shown = $false; $s.Loaded = $false }
            elseif ($line -match '^partner_parts=loaded') { $s.Loaded = $true }
            elseif ($line -match '^partner_net=stats;.*rx=(\d+);.*status=([a-z]+); last_age_ms=(\d+)') {
                $s.Rx = [int64]$Matches[1]; $s.Link = $Matches[2]; $s.Age = [int]$Matches[3]
            }
            elseif ($line -match '^crash_probe=av') { $s.Crash++ }
            elseif ($line -match '^partner_look=loaded; R=1; q40=(\d+)') { $s.Look = Get-LookName $Matches[1] }
            elseif ($line -match '^partner_look=(timeout|refused)') { $s.Look = "look $($Matches[1])" }
            elseif ($line -match '^partner_xform=applied') { $s.Move = 'smooth' }
            elseif ($line -match '^partner_xform=refused; reason=([^;\s]+)') { $s.Move = "warp ($($Matches[1]))" }
            elseif ($line -match '^partner_stance=set; .*to=(\d)') { $s.Stance = @('', 'stand', 'crouch', 'prone')[[int]$Matches[1]] }
            elseif ($line -match '^partner_stance=summary; last=([0-3]);') { if ([int]$Matches[1]) { $s.Stance = @('', 'stand', 'crouch', 'prone')[[int]$Matches[1]] } }
            elseif ($line -match '^partner_stage=changed; .*apply=([a-z]+); anim=([a-z]+);') {
                $s.Mode = "$($Matches[1])/$($Matches[2])"
                if ($Matches[1] -notin @('xform', 'hybrid')) { $s.Move = '' }
                if ($Matches[2] -eq 'off') { $s.Stance = '' }
            }
            elseif ($line -match '^partner_input=armed; mode=([a-z]+)') { $s.Input = $Matches[1]; $s.InputArmed = $true }
            elseif ($line -match '^partner_input=disarmed') { $s.InputArmed = $false }
            elseif ($line -match '^partner_input; mode=([a-z]+); windows=\d+; per_s=(\d+)') { $s.Input = $Matches[1]; $s.InputPerS = [int]$Matches[2] }
            elseif ($line -match '^partner_input=stale') { $s.InputStale = $true }
            elseif ($line -match '^partner_input=live') { $s.InputStale = $false }
            elseif ($line -match '^partner_input=fault; phase=([a-z]+)') { $s.InputFault = $Matches[1] }
            elseif ($line -match '^partner_stage=changed; .*; input=([a-z]+);') { $s.Input = $Matches[1]; if ($Matches[1] -eq 'off') { $s.InputArmed = $false } }
            elseif ($line -match '^partner_call=fault') { $s.Fault = $true }
            elseif ($line -match '^character=allocated; .*vars=(\d+)') { $s.Character = Get-LookName $Matches[1] }
            elseif ($line -match 'mission_termination=entered') { $s.Mission = $false; $s.Ready = $false; $s.Shown = $false; $s.Loaded = $false }
        }
    }
    foreach ($role in $dirs.Keys) {
        $alive = @(Select-ProcessInDirectory -Processes @(Get-Process mgsvtpp -ErrorAction SilentlyContinue) -Directory $dirs[$role]).Count -gt 0
        $state[$role].Exited = !$alive
    }
}
function Get-LookName([string]$type) {
    switch ($type) { '0' { 'Snake' } '1' { 'DD male' } '2' { 'DD female' } '3' { 'Avatar' } '5' { 'Ocelot' } '6' { 'Quiet' } default { "type $type" } }
}
function Format-Role([string]$role) {
    $s = $state[$role]
    $partner = if ($s.Loaded) { 'partner visible' } elseif ($s.Shown) { 'partner loading' } elseif ($s.Ready) { 'waiting for partner' } elseif ($s.Refused) { "partner refused ($($s.Refused))" } elseif ($s.Mission) { 'loading Mother Base' } else { 'starting' }
    $link = if ($s.Link -eq 'live') { "link live ($($s.Age) ms)" } else { "link $($s.Link)" }
    $crash = if ($s.Crash) { "  CRASH-LOG x$($s.Crash)" } else { '' }
    $exit = if ($s.Exited) { '  [game closed]' } else { '' }
    $extra = @()
    if ($s.Character) { $extra += "plays $($s.Character)" }
    if ($s.Look) { $extra += "partner looks like $($s.Look)" }
    if ($s.Mode) { $extra += "settings $($s.Mode)" }
    if ($s.Move) { $extra += "movement $($s.Move)" }
    if ($s.Stance) { $extra += "partner $($s.Stance)" }
    if ($s.Input -ne 'off') {
        $armed = if ($s.InputArmed) { 'armed' } else { 'off' }
        $perS = if ($s.InputPerS) { " $($s.InputPerS)/s" } else { '' }
        $stale = if ($s.InputStale) { ' STALE' } else { '' }
        $extra += "input $($s.Input)/$armed$perS$stale"
    }
    if ($s.InputFault) { $extra += "INPUT FAULT ($($s.InputFault))" }
    if ($s.Fault) { $extra += 'NATIVE CALL FAULT (Warp only)' }
    $details = if ($extra.Count) { ' [' + ($extra -join ', ') + ']' } else { '' }
    return "${role}: $partner, $link$details$crash$exit"
}

# The partner settings: look net always; movement and stance sync start on the
# proven path unless -Experimental ([M] / [A] toggle them, [R] re-sends).
$experiment = [ordered]@{ Move = $(if ($Experimental) { 'hybrid' } else { 'warp' }); Anim = $(if ($Experimental) { 'net' } else { 'off' }) }
function Get-SyncTrigger {
    $move = if ($experiment.Move -eq 'hybrid') { 'apply=hybrid reset=n' } else { 'apply=warp' }
    $anim = if ($experiment.Anim -eq 'net') { 'anim=net aset=qs' } else { 'anim=off' }
    return "stage=3 source=net $move $anim look=net"
}
function Send-SyncTrigger([string]$Why) {
    $text = Get-SyncTrigger
    Write-Note "$Why -> both copies: $text"
    & (Join-Path $lab 'Set-PartnerTrigger.ps1') -Role both -Text $text -Force | ForEach-Object { Write-Note $_ }
}
$started = $false
try {
    # ------------------------------------------------------------- prepare
    $mode = if ($Experimental) { 'smooth movement + stance sync ON (-Experimental)' } else { 'proven Warp movement; [M]/[A] turn the experiments on' }
    Write-Step "Preparing both game copies (offline, partner sync on, look net; $mode)"
    $partnerSettings = @('partner_stage=3', 'partner_source=net', 'partner_look=net', "partner_apply=$($experiment.Move)", "partner_anim=$($experiment.Anim)")
    if ($Experimental) { $partnerSettings += @('partner_aset=qs', 'partner_reset=n') }
    & (Join-Path $lab 'Prepare-LabInstance.ps1') -Offline -PartnerSetting $partnerSettings |
        ForEach-Object { Write-Note $_ }

    # --------------------------------------------------------------- start
    Write-Step 'Starting the host (left) and the guest (right)'
    $started = $true
    & (Join-Path $lab 'Start-Lab.ps1') -Offline | ForEach-Object { Write-Note $_ }

    # ---------------------------------------------------------- title screens
    $deadline = (Get-Date).AddSeconds($ReadyTimeoutSec)
    if (!$NoAutoTitle) {
        Write-Step 'Pressing Enter through the startup screens (a "Failed to log in to server" notice is normal offline)'
        Write-Note 'Please do not click into the game windows until both reach Mother Base.'
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
    Write-Step 'Waiting for Mother Base and the partner system in both copies'
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
    Write-Host '  BOTH COPIES ARE IN MOTHER BASE' -ForegroundColor Green
    Write-Host '  - If a window shows "Resume Game", click into it and press Space.'
    Write-Host '  - Keyboard + mouse: click the window you want to control (host = left, guest = right).'
    Write-Host '  - A controller always drives the GUEST (right) window, even while the host has focus.'
    Write-Host '  - Walk around: each window shows the OTHER player as a second character.'
    Write-Host '    Test condition: the guest (right) plays QUIET, so the host window should show a Quiet partner'
    Write-Host '    and the guest window the host''s own character.'
    Write-Host "  Partner movement: $($experiment.Move); stance sync: $($experiment.Anim) (experimental paths: docs/partner-anim-appearance.md)."
    Write-Host '    Turn ONE experiment on at a time and play a minute: if a game crashes or hangs, the last [M] / [A] is the cause.'
    Write-Host '    [M] smooth movement (does the partner walk instead of sliding?)   [A] stance sync (crouch / prone on the partner)'
    Write-Host '    [I] EXPERIMENTAL input drive (round 3): both copies drive the partner from the remote INPUT (apply=input input=net).'
    Write-Host '  Console keys (click this window first):  [S] screenshots   [M] smooth movement on/off   [A] stance sync on/off'
    Write-Host '                                           [I] input drive (experimental)   [W] proven Warp, no animation calls'
    Write-Host '                                           [R] re-send settings   [C] component census   [O] partner off   [Q] quit + clean up'
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
            'A' { $experiment.Anim = if ($experiment.Anim -eq 'net') { 'off' } else { 'net' }; Send-SyncTrigger "stance sync $($experiment.Anim)" }
            'W' {
                $experiment.Move = 'warp'; $experiment.Anim = 'off'
                # Back to the proven path AND turn the input window off.
                & (Join-Path $lab 'Set-PartnerTrigger.ps1') -Role both -Text 'stage=3 source=net apply=warp anim=off input=off isend=0 look=net' -Force |
                    ForEach-Object { Write-Note $_ }
            }
            'I' {
                # Experimental round 3 input drive on both copies (docs/partner-anim-appearance.md "Round 3").
                Write-Note 'input drive (experimental) -> both copies'
                & (Join-Path $lab 'Set-PartnerTrigger.ps1') -Role both -Text 'stage=3 source=net apply=input input=net isend=1 cam=sandbox hold=none look=net' -Force |
                    ForEach-Object { Write-Note $_ }
            }
            'C' { & (Join-Path $lab 'Set-PartnerTrigger.ps1') -Role both -Text 'census=1' -Force | ForEach-Object { Write-Note $_ } }
            'O' { & (Join-Path $lab 'Set-PartnerTrigger.ps1') -Role both -Text 'stop' -Force | ForEach-Object { Write-Note $_ } }
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
