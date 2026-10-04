#Requires -Version 7
<#
.SYNOPSIS
Log-only pass/fail smoke test of a running lab (synthesis section 3.5).

.DESCRIPTION
Reads the instances' logs from the offsets recorded by Start-Lab.ps1 (with
FileShare.ReadWrite, the game keeps writing) and lab-status.json, then:
  1. waits for the host (dynamite_host=returned; result=1, host_admission=authorized,
     session_status=host_ready) and the guest (client_join=armed), bounded by
     -ReadyTimeoutSec (people need time to click through the title screens);
  2. creates the guest's mgs-coop/join.trigger (lab.ini join_trigger=1);
  3. PASS when guest session_status=client_established AND host
     session_status=host_peer_present hold for 2 consecutive polls within
     -TimeoutSec of the trigger. FAIL on dynamite_client=refused,
     request_started=0, client_join=refused, session_error, a process exit or a timeout.
Writes result.json, log excerpts (script-observer, steam-observer, probe,
early, loopback), lab-status.json copies and a fuse message summary into the
run folder; captures screenshots unless -NoScreens. Exit code 0 = PASS.
With -HostOnly runs (Start-Lab.ps1 -HostOnly) it checks host readiness only (M0).

-AutoTitle is BEST EFFORT: it focuses each window and sends scan-code key
presses (~150 ms holds). Games often ignore synthetic input; use -Assist.
#>
[CmdletBinding()]
param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    [string]$RunDir,
    [int]$TimeoutSec,
    [int]$ReadyTimeoutSec,
    [int]$PollMs,
    [switch]$Assist,
    [switch]$AutoTitle,
    # Do not create join.trigger; wait for a manual Ctrl+F8 in the guest window instead.
    [switch]$NoTrigger,
    [switch]$NoScreens
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force

$config = Get-LabConfig -ConfigPath $ConfigPath
$current = Get-CurrentRun -Config $config -RunDir $RunDir
$run = $current.Run
$runPath = $current.Dir
if (!$TimeoutSec) { $TimeoutSec = [int](Get-ConfigValue $config.Smoke 'timeoutSec' 180) }
if (!$ReadyTimeoutSec) { $ReadyTimeoutSec = [int](Get-ConfigValue $config.Smoke 'readyTimeoutSec' 900) }
if (!$PollMs) { $PollMs = [int](Get-ConfigValue $config.Smoke 'pollMs' 2000) }
$hostOnly = [bool](Get-ConfigValue $run 'host_only' $false)
$roles = @($run.instances.PSObject.Properties.Name)
if (!$hostOnly -and ($roles -notcontains 'guest')) { throw 'run.json has no guest instance.' }

$state = @{}
foreach ($name in $roles) {
    $instance = $run.instances.$name
    $paths = Get-InstancePaths -GameDirectory ([string]$instance.dir)
    $offset = [Int64](Get-ConfigValue $instance.log_offsets 'script-observer.log' 0)
    $state[$name] = [pscustomobject]@{
        Paths = $paths; Pid = [int](Get-ConfigValue $instance 'pid' 0); Exe = [string]$instance.exe
        Offset = $offset; Text = [Text.StringBuilder]::new(); Status = $null; Alive = $true
    }
    if (!$state[$name].Pid) { throw "[$name] run.json has no pid; was the instance started?" }
}

function Write-Phase([string]$Message) { Write-Output ('[{0:HH:mm:ss}] {1}' -f (Get-Date), $Message) }

if ($Assist) {
    Write-Output ''
    Write-Output 'MANUAL STEPS (keyboard/mouse input goes only to the focused window):'
    Write-Output '  Host window (left):  press a key at the title screen -> CONTINUE -> wait until Mother Base has loaded.'
    if (!$hostOnly) {
        Write-Output '  Guest window (right): same: title screen -> CONTINUE -> reach Mother Base.'
        Write-Output '  Do NOT press Ctrl+F8: the harness creates join.trigger once the host is ready and the guest is armed.'
    }
    Write-Output '  The game windows must be in Windowed mode to keep running while unfocused.'
    Write-Output "  Waiting up to $ReadyTimeoutSec s for readiness, then $TimeoutSec s for the session."
    Write-Output ''
}

$autoTitleLog = @()
if ($AutoTitle) {
    Write-Phase 'AutoTitle (best effort): sending scan-code key presses to each window in turn.'
    $codes = @(Get-ConfigValue $config.Smoke 'autoTitleScanCodes' @(28))
    $gap = [int](Get-ConfigValue $config.Smoke 'autoTitleGapMs' 4000)
    foreach ($code in $codes) {
        foreach ($name in $roles) {
            $handle = Get-ProcessMainWindow -Id $state[$name].Pid
            $ok = if ($handle -ne [IntPtr]::Zero) { Send-ScanCodeKey -Handle $handle -ScanCode ([uint16]$code) } else { $false }
            $autoTitleLog += [ordered]@{ role = $name; scan_code = [int]$code; delivered = [bool]$ok; utc = [DateTime]::UtcNow.ToString('o') }
        }
        Start-Sleep -Milliseconds $gap
    }
}

$started = [DateTime]::UtcNow
$timings = [ordered]@{}
$triggerCreated = $false
$triggerAt = $null
$passPolls = 0
$lastPhase = ''
$verdict = $null
$aborted = $true
try {
    while ($true) {
        foreach ($name in $roles) {
            $s = $state[$name]
            $chunk = Read-LogFromOffset -Path (Join-Path $s.Paths.Mod 'script-observer.log') -Offset $s.Offset
            if ($chunk.Truncated) { Write-Warning "[$name] script-observer.log shrank; reading from the start." }
            if ($chunk.Text) { $null = $s.Text.Append($chunk.Text) }
            $s.Offset = $chunk.NewOffset
            $s.Status = Read-LabStatus -Path $s.Paths.Status -ExpectedPid $s.Pid
            $s.Alive = $null -ne (Get-LiveProcess -Id $s.Pid -ExpectedPath $s.Exe)
        }
        $now = [DateTime]::UtcNow
        $hostText = $state.host.Text.ToString()
        $guestText = if ($hostOnly) { '' } else { $state.guest.Text.ToString() }
        if ($NoTrigger -and !$hostOnly -and !$triggerCreated -and (Test-LogLine $guestText 'client_join=input_received')) {
            $triggerCreated = $true; $triggerAt = $now
        }
        $verdict = Get-LabSmokeVerdict -HostLog $hostText -GuestLog $guestText `
            -HostStatus $state.host.Status -GuestStatus $(if ($hostOnly) { $null } else { $state.guest.Status }) `
            -HostAlive $state.host.Alive -GuestAlive $(if ($hostOnly) { $true } else { $state.guest.Alive }) `
            -TriggerCreated $triggerCreated -PreviousPassPolls $passPolls `
            -ReadyElapsedSec ($now - $started).TotalSeconds -ReadyTimeoutSec $ReadyTimeoutSec `
            -TriggerElapsedSec $(if ($triggerAt) { ($now - $triggerAt).TotalSeconds } else { 0 }) -TimeoutSec $TimeoutSec `
            -HostOnly:$hostOnly
        $passPolls = $verdict.PassPolls
        $elapsed = [Math]::Round(($now - $started).TotalSeconds, 1)
        foreach ($mark in @(@('host_ready', $verdict.HostReady), @('guest_armed', $verdict.GuestArmed), @('request_started', $verdict.RequestStarted),
                @('established', ($verdict.PassPolls -ge 1)))) {
            if ($mark[1] -and !$timings.Contains($mark[0] + '_sec')) { $timings[$mark[0] + '_sec'] = $elapsed }
        }
        if ($verdict.Phase -ne $lastPhase) {
            Write-Phase ("phase={0} host_session={1} guest_session={2}" -f $verdict.Phase, $verdict.HostSession, $verdict.GuestSession)
            $lastPhase = $verdict.Phase
        }
        if ($verdict.Verdict -ne 'pending') { break }
        if ($verdict.ShouldTrigger -and !$NoTrigger) {
            New-TriggerFile -Path $state.guest.Paths.JoinTrigger
            $triggerCreated = $true; $triggerAt = [DateTime]::UtcNow
            $timings['trigger_sec'] = [Math]::Round(($triggerAt - $started).TotalSeconds, 1)
            Write-Phase 'created guest mgs-coop/join.trigger'
        }
        Start-Sleep -Milliseconds $PollMs
    }
    $aborted = $false
} finally {
    $finished = [DateTime]::UtcNow
    $verdictName = if ($aborted -or !$verdict) { 'aborted' } else { $verdict.Verdict }
    $reason = if ($aborted -or !$verdict) { 'interrupted' } else { $verdict.Reason }

    # ------------------------------------------------------------ copy log excerpts
    $files = [Collections.Generic.List[string]]::new()
    $evidence = [ordered]@{}
    foreach ($name in $roles) {
        $s = $state[$name]
        $instance = $run.instances.$name
        $texts = @{}
        foreach ($log in Get-LabLogNames) {
            $start = [Int64](Get-ConfigValue $instance.log_offsets $log 0)
            $chunk = Read-LogFromOffset -Path (Join-Path $s.Paths.Mod $log) -Offset $start -MaxBytes 64MB
            $texts[$log] = $chunk.Text
            if ($chunk.Exists -and $chunk.Text) {
                $target = "$name-$log"
                [IO.File]::WriteAllText((Join-Path $runPath $target), $chunk.Text, [Text.UTF8Encoding]::new($false))
                $files.Add($target)
            }
        }
        if (Test-Path -LiteralPath $s.Paths.Status -PathType Leaf) {
            Copy-Item -LiteralPath $s.Paths.Status -Destination (Join-Path $runPath "$name-lab-status.json") -Force
            $files.Add("$name-lab-status.json")
        }
        $observer = $texts['script-observer.log']
        $common = Get-LabEvidence -Text $observer -Prefixes @('install_result=0 installed=1', 'session_status=session_error')
        $roleEvidence = if ($name -eq 'host') {
            Get-LabEvidence -Text $observer -Prefixes @('mode=host_v3', 'dynamite_host=returned', 'host_admission=authorized',
                'session_status=host_ready', 'session_status=host_peer_present')
        } else {
            Get-LabEvidence -Text $observer -Prefixes @('mode=client_v1', 'client_join=armed', 'client_join=input_received',
                'dynamite_client=returned', 'dynamite_client=refused', 'client_join=refused', 'client_join=reset',
                'session_status=client_requested', 'session_status=client_established')
        }
        $sessionLine = [regex]::Match([string]$observer, '(?m)^dynamite_session=observed;[^\r\n]*\bstate=3 accept=1\b[^\r\n]*\bconsistent=1\b[^\r\n]*')
        $evidence[$name] = [ordered]@{
            observer = $roleEvidence; common = $common
            dynamite_session_state3_accept1 = $(if ($sessionLine.Success) { $sessionLine.Value } else { $null })
            probe = (Get-LabEvidence -Text $texts['probe.log'] -Prefixes @('DirectInput8Create_hresult=0x00000000'))
            master_server = (Get-LabEvidence -Text $texts['steam-observer.log'] -Prefixes @('master_server=redirected', 'master_server=refused', 'master_server=failed'))
            early = (Get-LabEvidence -Text $texts['early.log'] -Prefixes @('early=start', 'mutex=renamed', 'mutex=unchanged', 'identity=fake', 'identity=real', 'saves=redirected', 'saves=unchanged', 'xinput='))
            loopback = (Get-LabEvidence -Text $texts['loopback.log'] -Prefixes @('loopback=listening', 'loopback=hello', 'loopback=session_request_posted', 'loopback=accepted', 'loopback=connect_fail'))
            final_status = $s.Status
            alive = $s.Alive
        }
    }
    $fuseMessages = $null
    $fuseRun = Get-ConfigValue $run 'fuse' $null
    if ($fuseRun) {
        $fuseMessages = Get-FuseMessageSummary -FuseDirectory ([string]$fuseRun.cwd) -SinceUnix ([Int64]$fuseRun.since_unix)
        foreach ($f in 'fuse-stdout.log', 'fuse-stderr.log') { if (Test-Path -LiteralPath (Join-Path $runPath $f)) { $files.Add($f) } }
    }
    $hashes = [ordered]@{}
    foreach ($name in $roles) {
        $i = $run.instances.$name
        $hashes[$name] = [ordered]@{ exe = (Get-ConfigValue $i 'exe_sha256'); steam_api = (Get-ConfigValue $i 'steam_api_sha256'); loader = (Get-ConfigValue $i 'loader_sha256'); archive = (Get-ConfigValue $i 'archive_sha256') }
    }
    if ($fuseRun) { $hashes.fuse = Get-ConfigValue $fuseRun 'sha256' }

    $screens = @()
    if (!$NoScreens -and !$aborted) {
        try {
            $screens = @(& (Join-Path $PSScriptRoot 'Get-LabScreens.ps1') -ConfigPath $ConfigPath -RunDir $runPath -Label ('smoke-' + $verdictName) -PassThru |
                    ForEach-Object { Split-Path -Leaf $_ })
        } catch { Write-Warning "Screenshots failed: $($_.Exception.Message)" }
    }
    $result = [ordered]@{
        product = 'mgs-coop-lab-smoke-result'; schema = 1
        verdict = $verdictName; reason = $reason; phase = $(if ($verdict) { $verdict.Phase } else { '' })
        host_only = $hostOnly; trigger_mode = $(if ($NoTrigger) { 'manual_ctrl_f8' } else { 'join_trigger_file' })
        started_utc = $started.ToString('o'); finished_utc = $finished.ToString('o')
        duration_sec = [Math]::Round(($finished - $started).TotalSeconds, 1)
        ready_timeout_sec = $ReadyTimeoutSec; timeout_sec = $TimeoutSec; poll_ms = $PollMs
        timings = $timings
        trigger = [ordered]@{ created = $triggerCreated; utc = $(if ($triggerAt) { $triggerAt.ToString('o') } else { $null }) }
        sessions = [ordered]@{ host = $(if ($verdict) { $verdict.HostSession } else { $null }); guest = $(if ($verdict -and !$hostOnly) { $verdict.GuestSession } else { $null }) }
        pids = [ordered]@{}; hashes = $hashes; evidence = $evidence; fuse_messages = $fuseMessages
        auto_title = $autoTitleLog; files = $files; screenshots = $screens
        limits = 'Single-PC loopback run: proves session and mod logic only, not Steam NAT, relay, latency or real-account behaviour.'
    }
    foreach ($name in $roles) { $result.pids[$name] = $state[$name].Pid }
    Write-JsonFile -Path (Join-Path $runPath 'result.json') -Value $result
    Write-Output ''
    Write-Output ("RESULT: {0} ({1}); details: {2}" -f $verdictName.ToUpperInvariant(), $reason, (Join-Path $runPath 'result.json'))
}
if ($verdictName -ne 'pass') { exit 1 }
exit 0
