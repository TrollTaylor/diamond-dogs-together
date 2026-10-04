#Requires -Version 7
<#
.SYNOPSIS
Starts fuse (always -standalone), the host instance and then the guest instance,
each behind a safety gate.

.DESCRIPTION
Preflight: no mgsvtpp.exe running from either lab folder, fuse/loopback ports
free, executable/Steam DLL/loader/archive hashes, lab.ini/coop.ini/markers
present and consistent (save_dir is the folder's own lab copy, loopback=1, the
guest uses the fake SteamID and the -guest mutex suffix). Then records log
offsets (logs are append-only; the smoke test reads only what this run appends)
and a hash manifest of the real saves, and starts fuse with its working
directory (database fuse.dat and log/) in build/lab/fuse.

Safety gate (docs/lab-protocol.md safety rules), per instance, before the next
one starts: within smoke.safetyTimeoutSec of the start, this run's early.log and
steam-observer.log must show saves=redirected with the folder's lab save_dir and
master_server=redirected with the lab fuse URL; the guest must also show
mutex=renamed (suffix -guest) and identity=fake with the lab SteamID. A line
showing a rule is NOT in force (saves=unchanged, master_server=refused/failed,
identity=real, a different dir/url/id, lab=0) terminates that instance at once.
Missing evidence at the deadline, a missing window or an early exit stops it
too. Before the guest starts, a host-steamid.txt written by this run's host must
match the guest's client-host.txt (host_id_mismatch otherwise). On any failure
after fuse started, Start-Lab stops every process it started (games and fuse)
and records the reason in run.json. The windows are then placed side by side.
Run metadata: build/lab/runs/<ts>/run.json. fuse's console output (it prints
SteamIDs) stays in build/lab/fuse/console/, outside the shareable run folder.

-Offline (docs/offline-partner-spike-plan.md 2.5, section 4; prepare with
Prepare-LabInstance.ps1 -Offline): fuse is neither required nor started. The
preflight instead requires NO listener on the offline master port (TCP
lab.json offline.masterPort, default 47899; the game's master URL points there,
so it reaches no server), free UDP partner ports (ports.netHost/netGuest,
default 47820/47821) besides the loopback ports, the offline-v1 marker on both
folders, lab.ini net=1, partner=1 and the role's partner ports, and coop.ini
master_url = http://127.0.0.1:<masterPort>/tppstm/gate. The safety gate is
unchanged: master_server=redirected with that closed-port URL, saves=redirected,
and for the guest mutex=renamed and identity=fake. run.json records
offline=true; leftover partner.trigger files are removed before the start.

.EXAMPLE
pwsh -File tools/lab/Start-Lab.ps1 -WhatIf
pwsh -File tools/lab/Start-Lab.ps1
pwsh -File tools/lab/Start-Lab.ps1 -HostOnly      # milestone M0: host + fuse only
pwsh -File tools/lab/Start-Lab.ps1 -Offline       # offline partner spike: no fuse, closed master port
pwsh -File tools/lab/Start-Lab.ps1 -LayoutOnly    # re-apply window positions
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    [switch]$HostOnly,
    [switch]$LayoutOnly,
    [string]$RunDir,
    # Offline lab mode: no fuse; closed master port, offline-v1 markers, partner keys.
    [switch]$Offline
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force

$config = Get-LabConfig -ConfigPath $ConfigPath
$labRoot = $config.Paths.runDir
$dirs = [ordered]@{ host = $config.Paths.hostDir; guest = $config.Paths.guestDir }

function Set-LabLayout($Instances) {
    $resize = [bool](Get-ConfigValue $config.Window 'resize' $false)
    $width = if ($resize) { [int](Get-ConfigValue $config.Window 'width' 0) } else { 0 }
    $height = if ($resize) { [int](Get-ConfigValue $config.Window 'height' 0) } else { 0 }
    foreach ($name in $Instances.Keys) {
        $pid_ = [int](Get-ConfigValue $Instances[$name] 'pid' 0)
        if (!$pid_) { continue }
        $handle = Get-ProcessMainWindow -Id $pid_
        if ($handle -eq [IntPtr]::Zero) { Write-Warning "[$name] no main window to place."; continue }
        $pos = Get-ConfigValue $config.Window $name $null
        if (!$pos) { continue }
        $ok = Move-GameWindow -Handle $handle -X ([int]$pos.x) -Y ([int]$pos.y) -Width $width -Height $height
        Write-Output "[$name] window placed at $($pos.x),$($pos.y): $ok"
    }
}

if ($LayoutOnly) {
    $current = Get-CurrentRun -Config $config -RunDir $RunDir
    $live = [ordered]@{}
    foreach ($name in $current.Run.instances.PSObject.Properties.Name) { $live[$name] = $current.Run.instances.$name }
    Set-LabLayout $live
    return
}

$roles = if ($HostOnly) { @('host') } else { @('host', 'guest') }
$offlineExpect = @{}
if ($Offline) { foreach ($name in 'host', 'guest') { $offlineExpect[$name] = Get-LabOfflineExpectation -Role $name -Config $config } }
$expectedUrl = if ($Offline) { $offlineExpect.host.MasterUrl } else { 'http://127.0.0.1:' + [int]$config.Ports.fuse + '/tppstm/gate' }
$guestId = [string](Get-ConfigValue $config.Identity 'guestSteamId' '76561202255233008')
$guestSuffix = '-guest'
$problems = [Collections.Generic.List[string]]::new()

# ---------------------------------------------------------------- preflight
foreach ($name in $dirs.Keys) {
    foreach ($row in @(Get-GameProcessInDirectory -Directory $dirs[$name])) {
        $problems.Add("[$name] mgsvtpp is already running from $($dirs[$name]) (pid $($row.Id)).")
    }
}
if ($Offline) {
    # The game's master URL points at this port: nothing may listen there, or the
    # "offline" game would reach whatever does.
    $masterPort = [int]$offlineExpect.host.MasterPort
    if (!(Test-LocalPortFree -Port $masterPort -Protocol Tcp)) { $problems.Add("TCP port $masterPort (offline master URL $expectedUrl) has a listener or cannot be bound; the offline lab needs it closed.") }
} elseif (!(Test-LocalPortFree -Port ([int]$config.Ports.fuse) -Protocol Tcp)) { $problems.Add("TCP port $($config.Ports.fuse) (fuse) is in use.") }
foreach ($port in [int]$config.Ports.host, [int]$config.Ports.guest) {
    if (!(Test-LocalPortFree -Port $port -Protocol Udp)) { $problems.Add("UDP port $port (loopback) is in use.") }
}
if ($Offline) {
    foreach ($port in [int]$offlineExpect.host.NetListenPort, [int]$offlineExpect.guest.NetListenPort) {
        if (!(Test-LocalPortFree -Port $port -Protocol Udp)) { $problems.Add("UDP port $port (partner net) is in use.") }
    }
} elseif (!(Test-Path -LiteralPath $config.Paths.fuseExe -PathType Leaf)) { $problems.Add("fuse.exe not found: $($config.Paths.fuseExe) (run tools/lab/Build-Fuse.ps1).") }
if (!@(Get-Process -Name steam -ErrorAction SilentlyContinue).Count) { Write-Warning 'Steam is not running. Start Steam and log in first: the game calls SteamAPI_Init and exits without it.' }

$instances = [ordered]@{}
foreach ($name in $roles) {
    $p = Get-InstancePaths -GameDirectory $dirs[$name]
    $info = [ordered]@{ role = $name; dir = $p.Game; exe = $p.Exe }
    if (!(Test-FileHash -Path $p.Exe -Expected ([string]$config.Hashes.exe))) { $problems.Add("[$name] mgsvtpp.exe missing or hash differs.") }
    if (!(Test-FileHash -Path $p.SteamApi -Expected ([string]$config.Hashes.steamApi))) { $problems.Add("[$name] steam_api64.dll missing or hash differs.") }
    if (!(Test-FileHash -Path $p.Archive -Expected ([string]$config.Hashes.runtimeArchive))) { $problems.Add("[$name] runtime archive missing or hash differs.") }
    $record = if (Test-Path -LiteralPath $p.Record -PathType Leaf) { Get-Content -LiteralPath $p.Record -Raw | ConvertFrom-Json } else { $null }
    if (!$record -or [string](Get-ConfigValue $record 'game_directory' '') -ne $p.Game -or !(Test-FileHash -Path $p.Loader -Expected ([string](Get-ConfigValue $record 'dll_sha256' '0')))) {
        $problems.Add("[$name] loader/ownership record does not match this folder (run Prepare-LabInstance.ps1).")
    }
    $lab = if (Test-Path -LiteralPath $p.LabIni -PathType Leaf) { ConvertFrom-IniText ([IO.File]::ReadAllText($p.LabIni)) } else { $null }
    if (!$lab -or (Get-ConfigValue $lab 'enabled' '') -ne '1' -or (Get-ConfigValue $lab 'role' '') -ne $name) { $problems.Add("[$name] lab.ini missing or not enabled for role $name.") }
    if ($lab) {
        # The save redirect must target this folder's own lab copy (never Steam\userdata).
        if ((ConvertTo-LabComparablePath ([string](Get-ConfigValue $lab 'save_dir' ''))) -cne (ConvertTo-LabComparablePath $p.SaveDir)) {
            $problems.Add("[$name] lab.ini save_dir is not this folder's lab save copy ($($p.SaveDir)).")
        }
        if ((Get-ConfigValue $lab 'loopback' '') -ne '1') { $problems.Add("[$name] lab.ini loopback is not 1.") }
        if ($name -eq 'guest') {
            if ([string](Get-ConfigValue $lab 'self_steamid' '') -cne $guestId) { $problems.Add('[guest] lab.ini self_steamid is not the lab fake SteamID from lab.json.') }
            if ([string](Get-ConfigValue $lab 'mutex_suffix' '') -cne $guestSuffix) { $problems.Add("[guest] lab.ini mutex_suffix is not $guestSuffix.") }
        }
    }
    if ($Offline) { foreach ($problem in (Get-LabOfflineIniProblems -Role $name -Lab $lab -Expectation $offlineExpect[$name])) { $problems.Add($problem) } }
    $coop = if (Test-Path -LiteralPath $p.CoopIni -PathType Leaf) { ConvertFrom-IniText ([IO.File]::ReadAllText($p.CoopIni)) } else { $null }
    if (!$coop -or (Get-ConfigValue $coop 'master_url' '') -ne $expectedUrl) { $problems.Add("[$name] coop.ini master_url is not $expectedUrl.") }
    $marker = if (Test-Path -LiteralPath $p.Marker -PathType Leaf) { [IO.File]::ReadAllText($p.Marker) } else { '' }
    $wantMarker = if ($Offline) { $offlineExpect[$name].Marker + "`n" } elseif ($name -eq 'host') { "host-v3`n" } else { "client-v1`n" }
    if ($marker -cne $wantMarker) {
        $hint = if (!$Offline -and $marker -ceq "offline-v1`n") { ' (prepared for the offline lab mode: use -Offline, or re-run Prepare-LabInstance.ps1 without -Offline)' }
        elseif ($Offline) { ' (run Prepare-LabInstance.ps1 -Offline)' } else { '' }
        $problems.Add("[$name] script-observe.enabled is not $($wantMarker.Trim()).$hint")
    }
    if ($name -eq 'guest') {
        $clientHost = if (Test-Path -LiteralPath $p.ClientHost -PathType Leaf) { [IO.File]::ReadAllText($p.ClientHost).Trim() } else { '' }
        if (!(Test-IndividualSteamId64 $clientHost) -or ($lab -and (Get-ConfigValue $lab 'peer_steamid' '') -ne $clientHost)) {
            $problems.Add('[guest] client-host.txt is not a valid SteamID64 or differs from lab.ini peer_steamid.')
        }
    }
    # What the safety gate and the smoke test require this instance's logs to show.
    $info.expected = [ordered]@{
        save_dir = $p.SaveDir; master_url = $expectedUrl
        steamid = $(if ($name -eq 'guest') { $guestId } else { '' })
        mutex_suffix = $(if ($name -eq 'guest') { $guestSuffix } else { '' })
    }
    if ($Offline -and $lab) {
        # The partner/net lab.ini values this instance boots with (Test-PartnerSpike context).
        $partner = [ordered]@{}
        foreach ($key in @($lab.Keys)) { if ([string]$key -cmatch '^(net|partner|crash_probe)') { $partner[[string]$key] = [string]$lab[$key] } }
        $info.partner = $partner
    }
    $instances[$name] = [pscustomobject]@{ Paths = $p; Info = $info }
}
if ($problems.Count) {
    $problems | ForEach-Object { Write-Warning $_ }
    throw "Preflight failed ($($problems.Count) problem(s)); nothing was started."
}
Write-Output 'Preflight passed.'
foreach ($name in $roles) {
    $p = $instances[$name].Paths
    $triggers = @($p.JoinTrigger, $p.ResetTrigger)
    if ($Offline) { $triggers += $p.PartnerTrigger }
    foreach ($trigger in $triggers) {
        if ((Test-Path -LiteralPath $trigger) -and $PSCmdlet.ShouldProcess($trigger, 'Remove leftover trigger')) { Remove-Item -LiteralPath $trigger -Force }
    }
}
$startAction = if ($Offline) { 'Start the game instance(s) offline (no fuse; closed master port)' } else { 'Start fuse -standalone and the game instance(s)' }
if (!$PSCmdlet.ShouldProcess(($roles -join ' + '), $startAction)) { return }

# ---------------------------------------------------------------- run folder and baselines
$stamp = New-RunStamp
$runPath = Join-Path $labRoot ('runs/' + $stamp)
$null = New-Item -ItemType Directory -Path $runPath -Force
$run = [ordered]@{
    product = 'mgs-coop-lab-run'; schema = 2; stamp = $stamp; run_dir = $runPath
    started_utc = [DateTime]::UtcNow.ToString('o'); host_only = [bool]$HostOnly
    config_path = $config.Path; repo = $config.Paths.repo; version = (Get-ProjectVersion -RepoRoot $config.Paths.repo)
    ports = [ordered]@{ fuse = [int]$config.Ports.fuse; host = [int]$config.Ports.host; guest = [int]$config.Ports.guest }
    fuse = $null; instances = [ordered]@{}; real_saves_before = $null; private_state = $null; latest_save_backup = $null
    host_id_check_at_guest_start = $null; aborted = $null
    notes = 'Shareable: no SteamIDs or Steam account folders are recorded in this folder (see private_state).'
}
if ($Offline) {
    $run.offline = $true
    $run.offline_master_url = $expectedUrl
    $run.ports.offline_master = [int]$offlineExpect.host.MasterPort
    $run.ports.net_host = [int]$offlineExpect.host.NetListenPort
    $run.ports.net_guest = [int]$offlineExpect.guest.NetListenPort
}
foreach ($name in $roles) {
    $p = $instances[$name].Paths
    $offsets = [ordered]@{}
    foreach ($log in Get-LabLogNames) { $offsets[$log] = Get-FileLengthOrZero (Join-Path $p.Mod $log) }
    $info = $instances[$name].Info
    $info.exe_sha256 = Get-Sha256 $p.Exe
    $info.steam_api_sha256 = Get-Sha256 $p.SteamApi
    $info.loader_sha256 = Get-Sha256 $p.Loader
    $info.archive_sha256 = Get-Sha256 $p.Archive
    $info.lab_ini_sha256 = Get-Sha256 $p.LabIni
    $info.log_offsets = $offsets
    $run.instances[$name] = $info
}
try {
    $identity = Get-HostIdentity -OverrideSteamId ([string](Get-ConfigValue $config.Identity 'hostSteamId' 'auto')) -SteamPath $config.Paths.steamRoot
    # The Steam account folder identifies the user: it stays in build/lab/private/.
    $privateState = Join-Path $labRoot ("private/run-$stamp.json")
    Write-JsonFile -Path $privateState -Value ([ordered]@{ source_root = $identity.UserData; note = 'Local only; never copy into run folders.' })
    Write-JsonFile -Path (Join-Path $runPath 'real-saves-before.json') -Value ([ordered]@{ source_root = 'Steam/userdata/<account>'; files = (Get-RealSaveManifest -UserData $identity.UserData) })
    $run.real_saves_before = 'real-saves-before.json'
    $run.private_state = $privateState
} catch { Write-Warning "Could not hash the real saves before the run: $($_.Exception.Message)" }
$pointer = Join-Path $labRoot 'latest-save-backup.json'
if (Test-Path -LiteralPath $pointer -PathType Leaf) { $run.latest_save_backup = (Get-Content -LiteralPath $pointer -Raw | ConvertFrom-Json).backup_dir }
Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
Write-JsonFile -Path (Join-Path $labRoot 'current-run.json') -Value ([ordered]@{ run_dir = $runPath; stamp = $stamp })

$windowTimeout = [int](Get-ConfigValue $config.Smoke 'windowTimeoutSec' 180)
$safetyTimeout = [int](Get-ConfigValue $config.Smoke 'safetyTimeoutSec' 120)

function Wait-InstanceGate {
    # Polls the instance's early.log and steam-observer.log (from this run's
    # offsets) and its main window until the safety evidence is complete and the
    # window exists, or something is wrong.
    param([string]$Name, $Process, $Paths, $Info, [datetime]$StartUtc)
    $expectation = Get-InstanceExpectation -Role $Name -Instance $Info
    $files = @{ early = (Join-Path $Paths.Mod 'early.log'); steam = (Join-Path $Paths.Mod 'steam-observer.log') }
    $offsets = @{ early = [Int64]$Info.log_offsets['early.log']; steam = [Int64]$Info.log_offsets['steam-observer.log'] }
    $texts = @{ early = [Text.StringBuilder]::new(); steam = [Text.StringBuilder]::new() }
    $windowDeadline = $StartUtc.AddSeconds($windowTimeout)
    $safetyDeadline = $StartUtc.AddSeconds($safetyTimeout)
    $handle = [IntPtr]::Zero
    while ($true) {
        foreach ($key in 'early', 'steam') {
            $chunk = Read-LogFromOffset -Path $files[$key] -Offset $offsets[$key]
            if ($chunk.Text) { $null = $texts[$key].Append($chunk.Text) }
            $offsets[$key] = $chunk.NewOffset
        }
        $safety = Get-InstanceSafety -Role $Name -Expectation $expectation -EarlyLog $texts.early.ToString() -SteamLog $texts.steam.ToString()
        $result = [ordered]@{ Ok = $false; Unsafe = $false; Reason = ''; Safety = $safety }
        if ($safety.State -eq 'unsafe') { $result.Unsafe = $true; $result.Reason = 'unsafe_' + $safety.Reason; return [pscustomobject]$result }
        if ($handle -eq [IntPtr]::Zero) { $handle = Get-ProcessMainWindow -Id $Process.Id }
        if ($safety.State -eq 'ok' -and $handle -ne [IntPtr]::Zero) { $result.Ok = $true; return [pscustomobject]$result }
        if ($Process.HasExited) { $result.Reason = "exited_early (code $($Process.ExitCode))"; return [pscustomobject]$result }
        $now = [DateTime]::UtcNow
        if ($handle -eq [IntPtr]::Zero -and $now -gt $windowDeadline) { $result.Reason = "no_main_window_after_$($windowTimeout)s"; return [pscustomobject]$result }
        if ($safety.State -ne 'ok' -and $now -gt $safetyDeadline) {
            $result.Reason = "safety_evidence_missing_after_$($safetyTimeout)s (" + (@($safety.Missing) -join ',') + ')'
            return [pscustomobject]$result
        }
        Start-Sleep -Milliseconds 500
    }
}

$started = [Collections.Generic.List[object]]::new()
$fuse = $null
try {
    # ------------------------------------------------------------ fuse
    if ($Offline) {
        Write-Output "Offline mode: fuse is not started; the master URL $expectedUrl points at a closed port."
    } else {
        $fuseDir = Join-Path $labRoot 'fuse'
        $consoleDir = Join-Path $fuseDir 'console'
        $null = New-Item -ItemType Directory -Path $consoleDir -Force
        $fuseArgs = New-FuseArguments -Port ([int]$config.Ports.fuse) -WriteLog ([bool](Get-ConfigValue $config.Fuse 'writeLog' $true))
        Assert-FuseArgumentsSafe -Arguments $fuseArgs
        $sinceUnix = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() - 1
        $fuseStdout = Join-Path $consoleDir "$stamp-stdout.log"
        $fuseStderr = Join-Path $consoleDir "$stamp-stderr.log"
        $fuse = Invoke-WithoutStdHandleInheritance {
            Start-Process -FilePath $config.Paths.fuseExe -ArgumentList $fuseArgs -WorkingDirectory $fuseDir -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput $fuseStdout -RedirectStandardError $fuseStderr
        }
        $run.fuse = [ordered]@{
            exe = $config.Paths.fuseExe; sha256 = (Get-Sha256 $config.Paths.fuseExe); pid = $fuse.Id
            process_start_utc = $(if ($t = Get-ProcessStartUtc $fuse) { $t.ToString('o') } else { $null })
            cwd = $fuseDir; args = $fuseArgs; database = (Join-Path $fuseDir 'fuse.dat'); log_root = (Join-Path $fuseDir 'log')
            since_unix = $sinceUnix; stdout = $fuseStdout; stderr = $fuseStderr; url = $expectedUrl
        }
        Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
        $deadline = [DateTime]::UtcNow.AddSeconds([int](Get-ConfigValue $config.Fuse 'startupTimeoutSec' 20))
        while (!(Test-TcpListening -Port ([int]$config.Ports.fuse))) {
            if ($fuse.HasExited) { throw "fuse exited during startup (code $($fuse.ExitCode)); see $fuseStdout" }
            if ([DateTime]::UtcNow -gt $deadline) { throw 'fuse did not start listening in time.' }
            Start-Sleep -Milliseconds 250
        }
        Write-Output "fuse listening on 127.0.0.1:$($config.Ports.fuse) (pid $($fuse.Id), -standalone, cwd $fuseDir)."
    }

    # ------------------------------------------------------------ games, one at a time behind the gate
    foreach ($name in $roles) {
        $p = $instances[$name].Paths
        if ($name -eq 'guest') {
            $hostIdText = Read-FreshText -Path $instances.host.Paths.HostId -NotBeforeUtc $run.instances.host.process_start_utc
            $guestLab = ConvertFrom-IniText (Read-SharedText -Path $p.LabIni)
            $check = Get-HostIdCheck -HostIdText $hostIdText -ClientHostText (Read-SharedText -Path $p.ClientHost) -PeerSteamId ([string](Get-ConfigValue $guestLab 'peer_steamid' ''))
            $run.host_id_check_at_guest_start = $check
            if ($check -eq 'mismatch') {
                throw "[guest] not started: host_id_mismatch. The host's host-steamid.txt (this run) differs from the guest's client-host.txt or lab.ini peer_steamid. Re-run Prepare-LabInstance.ps1 while Steam is logged into the account the host uses."
            }
        }
        $process = Invoke-WithoutStdHandleInheritance { Start-Process -FilePath $p.Exe -WorkingDirectory $p.Game -PassThru }
        $startUtc = [DateTime]::UtcNow
        $started.Add([pscustomobject]@{ Name = $name; Process = $process; Unsafe = $false })
        $run.instances[$name].pid = $process.Id
        $run.instances[$name].process_start_utc = $(if ($t = Get-ProcessStartUtc $process) { $t.ToString('o') } else { $null })
        $run.instances[$name].started_utc = $startUtc.ToString('o')
        Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
        Write-Output "[$name] started pid $($process.Id); waiting for its safety evidence and main window..."
        $gate = Wait-InstanceGate -Name $name -Process $process -Paths $p -Info $run.instances[$name] -StartUtc $startUtc
        $run.instances[$name].safety = [ordered]@{
            state = $gate.Safety.State; reason = $gate.Reason; missing = @($gate.Safety.Missing); checked_utc = [DateTime]::UtcNow.ToString('o')
        }
        Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
        if (!$gate.Ok) {
            if ($gate.Unsafe) { $started[$started.Count - 1].Unsafe = $true }
            throw "[$name] safety gate failed: $($gate.Reason). See $($p.Mod)\early.log and steam-observer.log."
        }
        $run.instances[$name].window_seen_utc = [DateTime]::UtcNow.ToString('o')
        Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
        $masterNote = if ($Offline) { 'closed local port' } else { 'lab fuse' }
        Write-Output "[$name] safety evidence complete (saves redirected, master server -> $masterNote$(if ($name -eq 'guest') { ', mutex renamed, fake identity' })); main window is up."
    }
} catch {
    $failure = $_
    # Stop everything this run started. An unsafe instance is terminated at once;
    # the others get WM_CLOSE first (their saves were shown to be redirected).
    $stopped = [Collections.Generic.List[string]]::new()
    for ($i = $started.Count - 1; $i -ge 0; $i--) {
        $entry = $started[$i]
        $proc = $entry.Process
        if ($proc.HasExited) { $stopped.Add("$($entry.Name):already_exited"); continue }
        if (!$entry.Unsafe) {
            $handle = Get-ProcessMainWindow -Id $proc.Id
            if ($handle -ne [IntPtr]::Zero -and (Close-GameWindow -Handle $handle)) { $null = $proc.WaitForExit(10000) }
        }
        if (!$proc.HasExited) {
            try { $proc.Kill() } catch { Write-Warning "[$($entry.Name)] could not terminate pid $($proc.Id): $($_.Exception.Message)" }
            $null = $proc.WaitForExit(5000)
            $stopped.Add("$($entry.Name):" + $(if ($entry.Unsafe) { 'terminated_immediately' } else { 'terminated' }))
        } else { $stopped.Add("$($entry.Name):closed") }
    }
    if ($fuse -and !$fuse.HasExited) {
        try { $fuse.Kill(); $null = $fuse.WaitForExit(5000); $stopped.Add('fuse:stopped') } catch { Write-Warning "Could not stop fuse pid $($fuse.Id)." }
    }
    $run.aborted = [ordered]@{ utc = [DateTime]::UtcNow.ToString('o'); reason = $failure.Exception.Message; stopped = $stopped.ToArray() }
    Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
    Write-Warning ("Start-Lab stopped what it had started: " + ($stopped -join ', ') + ". Run Stop-Lab.ps1 to verify the real saves.")
    throw $failure
}
Set-LabLayout $run.instances
Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
Write-Output "Run folder: $runPath"
if ($Offline) {
    Write-Output 'Next: click through the title (a server-failure notice is expected offline), reach Mother Base, then'
    Write-Output '      pwsh -File tools/lab/Test-PartnerSpike.ps1 -Experiment E0   (Stop-Lab.ps1 closes everything)'
} else {
    Write-Output 'Next: pwsh -File tools/lab/Invoke-LabSmoke.ps1 -Assist   (Stop-Lab.ps1 closes everything)'
}
