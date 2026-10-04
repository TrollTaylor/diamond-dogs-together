<#
.SYNOPSIS
One-click offline co-op test on this PC: two MGSV copies side by side, each
showing the other player as a second character.

.DESCRIPTION
Runs the proven offline lab path (docs/testing-2026-10-04.md):
  1. Prepare-LabInstance.ps1 -Offline with the partner synced over our own link
     from the start (stage 3, source net). Your real saves are backed up; both
     copies play on save COPIES. No server, no login, no game session.
  2. Start-Lab.ps1 -Offline: host window left, guest window right.
  3. Presses Enter through the startup screens of both until Mother Base loads
     (skip with -NoAutoTitle and click through yourself).
  4. Waits until both copies report the partner system ready, then shows a live
     status line. Console keys: [S] screenshots, [R] re-sync partner,
     [O] partner off, [Q] quit and clean up.
  5. On exit (Q, closing a game window, or Ctrl+C): closes both games, checks
     your real save files and switches the lab config off again so normal
     launches behave normally.

Controls while playing: keyboard and mouse go to whichever game window you
clicked last. An Xbox-style controller always drives the GUEST window (right),
even while the host window has focus, so one person can test both at once.

.EXAMPLE
pwsh -File tools/lab/Play-OfflineCoop.ps1
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
    [int]$ExitAfterReadySec = 0
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
            elseif ($line -match 'mission_termination=entered') { $s.Mission = $false; $s.Ready = $false; $s.Shown = $false; $s.Loaded = $false }
        }
    }
    foreach ($role in $dirs.Keys) {
        $alive = @(Select-ProcessInDirectory -Processes @(Get-Process mgsvtpp -ErrorAction SilentlyContinue) -Directory $dirs[$role]).Count -gt 0
        $state[$role].Exited = !$alive
    }
}
function Format-Role([string]$role) {
    $s = $state[$role]
    $partner = if ($s.Loaded) { 'partner visible' } elseif ($s.Shown) { 'partner loading' } elseif ($s.Ready) { 'waiting for partner' } elseif ($s.Refused) { "partner refused ($($s.Refused))" } elseif ($s.Mission) { 'loading Mother Base' } else { 'starting' }
    $link = if ($s.Link -eq 'live') { "link live ($($s.Age) ms)" } else { "link $($s.Link)" }
    $crash = if ($s.Crash) { "  CRASH-LOG x$($s.Crash)" } else { '' }
    $exit = if ($s.Exited) { '  [game closed]' } else { '' }
    return "${role}: $partner, $link$crash$exit"
}

$started = $false
try {
    # ------------------------------------------------------------- prepare
    Write-Step 'Preparing both game copies (offline, partner sync on)'
    & (Join-Path $lab 'Prepare-LabInstance.ps1') -Offline -PartnerSetting 'partner_stage=3', 'partner_source=net', 'partner_apply=warp' |
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
    while ((Get-Date) -lt $deadline) {
        Update-State
        if (@($dirs.Keys | Where-Object { $state[$_].Exited }).Count) { throw 'A game window closed before it was ready.' }
        if (@($dirs.Keys | Where-Object { !$state[$_].Ready }).Count -eq 0) { break }
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
    Write-Host '    (It slides between positions in one pose for now - animations come next.)'
    Write-Host '  Console keys (click this window first):  [S] screenshots   [R] re-sync partner   [O] partner off   [Q] quit + clean up'
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
            'R' { & (Join-Path $lab 'Set-PartnerTrigger.ps1') -Role both -Text 'stage=3 source=net apply=warp' -Force | ForEach-Object { Write-Note $_ } }
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
