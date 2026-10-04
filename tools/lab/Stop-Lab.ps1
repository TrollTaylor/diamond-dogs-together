#Requires -Version 7
<#
.SYNOPSIS
Closes this run's lab games and fuse, then verifies the real save files' hashes.

.DESCRIPTION
Only processes this run started are closed and, if needed, terminated: the pid
recorded in run.json, still running from the recorded executable path, with the
recorded process start time (older run.json: started inside the run's launch
window). Each gets WM_CLOSE; whatever is still running after -TimeoutSec is
terminated. Any other mgsvtpp/mgsvmgo running from a lab folder (for example a
normal session started from Steam after the run) is listed and left alone;
-IncludeUnrecordedProcesses sends those WM_CLOSE only (never a forced
termination) and also stops other fuse processes started from the lab fuse.exe.
Afterwards it hashes the real Steam\userdata\<id>\287700\local and 311340\remote
files and compares them with the manifest taken by Start-Lab.ps1 at run start
and with the latest backup taken by Prepare-LabInstance.ps1. Writes stop.json
(and a redacted copy of fuse's console log) into the run folder. Leftover
mgs-coop/partner.trigger files (offline partner spike, Set-PartnerTrigger.ps1)
in both lab folders are deleted after the games are closed, so a stale command
cannot fire in the next run. stop.json also records each instance's log
lengths after the close (log_end_offsets), which bound this run's lines for
Test-PartnerSpike.ps1 -RunDir once later runs append to the same logs.
Exit code 0 = real saves unchanged, 2 = a real save file changed (restore it
from build/lab/save-backups/<ts>/files/), 1 = error.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    [string]$RunDir,
    [int]$TimeoutSec = 60,
    [switch]$KeepFuse,
    [switch]$IncludeUnrecordedProcesses
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force

$config = Get-LabConfig -ConfigPath $ConfigPath
$current = $null
try { $current = Get-CurrentRun -Config $config -RunDir $RunDir } catch { Write-Warning "$($_.Exception.Message) Only processes found in the lab folders are listed; none is closed without -IncludeUnrecordedProcesses." }

function Test-SamePath([string]$A, [string]$B) {
    return $A -and $B -and [IO.Path]::GetFullPath($A).Equals([IO.Path]::GetFullPath($B), [StringComparison]::OrdinalIgnoreCase)
}

# ---------------------------------------------------------------- targets
$recorded = @()
if ($current) {
    $runStart = Get-ConfigValue $current.Run 'started_utc' $null
    foreach ($name in $current.Run.instances.PSObject.Properties.Name) {
        $i = $current.Run.instances.$name
        $id = [int](Get-ConfigValue $i 'pid' 0)
        if (!$id) { continue }
        $p = Get-Process -Id $id -ErrorAction SilentlyContinue
        $path = if ($p) { Get-ProcessPathSafe $p } else { $null }
        $recorded += [pscustomobject]@{
            Role = $name; Id = $id; Alive = [bool]$p
            PathMatches = [bool](Test-SamePath $path ([string]$i.exe))
            IdentityVerified = [bool]($p -and (Test-RunProcessIdentity -ActualStartUtc (Get-ProcessStartUtc $p) `
                        -RecordedStartUtc (Get-ConfigValue $i 'process_start_utc' $null) -NotBeforeUtc $runStart -NotAfterUtc (Get-ConfigValue $i 'started_utc' $null)))
        }
        if ($p -and !$recorded[-1].IdentityVerified) { Write-Warning "[$name] pid $id is running but is not the process this run started (path or start time differs)." }
    }
}
$found = @()
foreach ($pair in @(@('host', $config.Paths.hostDir), @('guest', $config.Paths.guestDir))) {
    foreach ($row in @(Get-GameProcessInDirectory -Directory $pair[1])) {
        if (!$row.Unknown) { $found += [pscustomobject]@{ Role = $pair[0]; Id = [int]$row.Id } }
    }
}
$targets = @(Select-LabStopTargets -Recorded $recorded -Found $found -IncludeUnrecorded:$IncludeUnrecordedProcesses)

# ---------------------------------------------------------------- close games
$outcome = [ordered]@{}
$closing = [Collections.Generic.List[object]]::new()
foreach ($t in $targets) {
    $key = if ($t.Action -eq 'close_then_terminate') { $t.Role } else { "unrecorded-$($t.Role)-$($t.Id)" }
    if ($t.Action -eq 'leave') {
        $outcome[$key] = [ordered]@{ pid = $t.Id; action = 'left_running'; exit = 'running' }
        Write-Warning "[$($t.Role)] pid $($t.Id) runs from the $($t.Role) lab folder but was not started by this run: left running. -IncludeUnrecordedProcesses sends it WM_CLOSE (never a forced termination)."
        continue
    }
    $proc = Get-Process -Id $t.Id -ErrorAction SilentlyContinue
    if (!$proc) { continue }
    $posted = $false
    if ($PSCmdlet.ShouldProcess("$($t.Role) pid $($t.Id)", 'Send WM_CLOSE')) { $posted = Close-GameWindow -Handle (Get-ProcessMainWindow -Id $t.Id) }
    $outcome[$key] = [ordered]@{ pid = $t.Id; action = $t.Action; wm_close_posted = [bool]$posted; exit = 'running' }
    $closing.Add([pscustomobject]@{ Key = $key; Target = $t; Process = $proc })
    Write-Output "[$($t.Role)] pid $($t.Id) ($($t.Reason)): WM_CLOSE posted: $posted"
}
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
while ($closing.Count -and [DateTime]::UtcNow -lt $deadline -and @($closing | Where-Object { !$_.Process.HasExited }).Count) { Start-Sleep -Milliseconds 500 }
foreach ($c in $closing) {
    if ($c.Process.HasExited) { $outcome[$c.Key].exit = 'closed'; continue }
    if ($c.Target.Action -ne 'close_then_terminate') {
        Write-Warning "[$($c.Target.Role)] pid $($c.Target.Id) (not started by this run) is still running after WM_CLOSE; it is not terminated."
        continue
    }
    if ($PSCmdlet.ShouldProcess("$($c.Target.Role) pid $($c.Target.Id)", "Terminate after $TimeoutSec s")) {
        try { $c.Process.Kill() } catch { Write-Warning "[$($c.Target.Role)] could not terminate pid $($c.Target.Id): $($_.Exception.Message)" }
        $null = $c.Process.WaitForExit(5000)
        $outcome[$c.Key].exit = 'terminated'
        Write-Warning "[$($c.Target.Role)] did not close within $TimeoutSec s; terminated. Its lab save copy may be incomplete (the real saves are separate)."
    }
}

# ---------------------------------------------------------------- fuse
$fuseState = 'not_running'
$fuseRun = if ($current) { Get-ConfigValue $current.Run 'fuse' $null } else { $null }
if (!$KeepFuse) {
    $recordedFuse = 0
    if ($fuseRun) {
        $id = [int](Get-ConfigValue $fuseRun 'pid' 0)
        $p = if ($id) { Get-Process -Id $id -ErrorAction SilentlyContinue } else { $null }
        $hostStart = if ($current.Run.instances.PSObject.Properties.Name -contains 'host') { Get-ConfigValue $current.Run.instances.host 'started_utc' $null } else { $null }
        $notAfter = if ($hostStart) { $hostStart } else { (ConvertTo-LabUtc $current.Run.started_utc).AddSeconds(60) }
        if ($p -and (Test-SamePath (Get-ProcessPathSafe $p) $config.Paths.fuseExe) -and
            (Test-RunProcessIdentity -ActualStartUtc (Get-ProcessStartUtc $p) -RecordedStartUtc (Get-ConfigValue $fuseRun 'process_start_utc' $null) `
                -NotBeforeUtc $current.Run.started_utc -NotAfterUtc $notAfter)) {
            $recordedFuse = $id
            if ($PSCmdlet.ShouldProcess("fuse pid $id", 'Stop')) { $p.Kill(); $null = $p.WaitForExit(5000); $fuseState = 'stopped'; Write-Output "fuse (pid $id) stopped." }
        }
    }
    foreach ($p in @(Get-Process -Name fuse -ErrorAction SilentlyContinue)) {
        if ($p.Id -eq $recordedFuse -or !(Test-SamePath (Get-ProcessPathSafe $p) $config.Paths.fuseExe)) { continue }
        if ($IncludeUnrecordedProcesses) {
            if ($PSCmdlet.ShouldProcess("fuse pid $($p.Id) (not this run's)", 'Stop')) { $p.Kill(); $null = $p.WaitForExit(5000); Write-Output "fuse (pid $($p.Id), not this run's) stopped." }
        } else { Write-Warning "fuse pid $($p.Id) (lab fuse.exe, not this run's) left running; -IncludeUnrecordedProcesses stops it." }
    }
} else {
    $fuseState = 'kept'
    Write-Warning 'fuse kept running (-KeepFuse). While it runs, the lab coop.ini sends a normal game start from the host folder to it.'
}
if ($current -and $fuseRun) {
    foreach ($pair in @(@('stdout', 'fuse-stdout.redacted.log'), @('stderr', 'fuse-stderr.redacted.log'))) {
        $source = [string](Get-ConfigValue $fuseRun $pair[0] '')
        if ($source -and ![IO.Path]::IsPathRooted($source)) { $source = Join-Path $current.Dir $source }
        if ($source -and (Test-Path -LiteralPath $source -PathType Leaf)) {
            Write-RedactedText -Path (Join-Path $current.Dir $pair[1]) -Text (Read-SharedText -Path $source) -Keep @([string](Get-ConfigValue $config.Identity 'guestSteamId' ''))
        }
    }
}

# ---------------------------------------------------------------- real saves
$saves = [ordered]@{ checked = $false; unchanged_since_run_start = $null; unchanged_since_backup = $null; details = @() }
$beforeFile = if ($current) { [string](Get-ConfigValue $current.Run 'real_saves_before' '') } else { '' }
if ($beforeFile) {
    $before = Get-Content -LiteralPath (Join-Path $current.Dir $beforeFile) -Raw | ConvertFrom-Json
    # The account folder lives in build/lab/private/ (older runs kept it in the manifest).
    $privateState = [string](Get-ConfigValue $current.Run 'private_state' '')
    $sourceRoot = if ($privateState -and (Test-Path -LiteralPath $privateState -PathType Leaf)) { [string](Get-Content -LiteralPath $privateState -Raw | ConvertFrom-Json).source_root }
    elseif ([IO.Path]::IsPathRooted([string]$before.source_root)) { [string]$before.source_root } else { $null }
    if ($sourceRoot) {
        $comparison = Compare-HashManifest -Expected @($before.files) -Actual (Get-RealSaveManifest -UserData $sourceRoot)
        $saves.checked = $true
        $saves.unchanged_since_run_start = $comparison.Same
        $saves.details += [ordered]@{ against = 'run_start'; changed = $comparison.Changed; missing = $comparison.Missing; added = $comparison.Added }
    } else { Write-Warning 'The run-start save manifest has no local source root (build/lab/private missing); comparing with the backup only.' }
}
$backupDir = if ($current -and (Get-ConfigValue $current.Run 'latest_save_backup' $null)) { [string]$current.Run.latest_save_backup } else {
    $pointer = Join-Path $config.Paths.runDir 'latest-save-backup.json'
    if (Test-Path -LiteralPath $pointer -PathType Leaf) { [string](Get-Content -LiteralPath $pointer -Raw | ConvertFrom-Json).backup_dir } else { $null }
}
if ($backupDir -and (Test-Path -LiteralPath (Join-Path $backupDir 'manifest.json'))) {
    $check = Test-RealSavesAgainstBackup -BackupDirectory $backupDir
    $saves.checked = $true
    $saves.unchanged_since_backup = $check.Comparison.Same
    $saves.details += [ordered]@{ against = 'latest_backup'; backup = (Split-Path -Leaf $backupDir); changed = $check.Comparison.Changed; missing = $check.Comparison.Missing; added = $check.Comparison.Added }
}
foreach ($d in $saves.details) {
    $same = (@($d.changed).Count + @($d.missing).Count + @($d.added).Count) -eq 0
    Write-Output ("Real saves vs {0}: {1}" -f $d.against, $(if ($same) { 'UNCHANGED' } else { 'CHANGED' }))
    foreach ($kind in 'changed', 'missing', 'added') { foreach ($f in @($d[$kind])) { Write-Warning "  $kind`: $f" } }
}
if (!$saves.checked) { Write-Warning 'No save manifest to compare against (run Prepare-LabInstance.ps1 / Start-Lab.ps1 first).' }

# ---------------------------------------------------------------- log end offsets
# Each instance's log lengths now that its game is closed: Test-PartnerSpike.ps1
# reads this run's lines from run.json log_offsets up to here, so a later run
# appending to the same logs never mixes into this run's verdicts.
$logEnds = [ordered]@{}
if ($current) {
    foreach ($name in $current.Run.instances.PSObject.Properties.Name) {
        $dir = [string](Get-ConfigValue $current.Run.instances.$name 'dir' '')
        if (!$dir) { continue }
        $mod = (Get-InstancePaths -GameDirectory $dir).Mod
        $ends = [ordered]@{}
        foreach ($log in Get-LabLogNames) { $ends[$log] = Get-FileLengthOrZero (Join-Path $mod $log) }
        $logEnds[$name] = $ends
    }
}

# ---------------------------------------------------------------- leftover partner triggers
$triggersRemoved = @()
foreach ($pair in @(@('host', $config.Paths.hostDir), @('guest', $config.Paths.guestDir))) {
    $trigger = (Get-InstancePaths -GameDirectory $pair[1]).PartnerTrigger
    if (!(Test-Path -LiteralPath $trigger -PathType Leaf)) { continue }
    if ((Get-Item -LiteralPath $trigger -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { Write-Warning "[$($pair[0])] $trigger is a filesystem link; left alone."; continue }
    if ($PSCmdlet.ShouldProcess($trigger, 'Delete leftover partner trigger')) {
        Remove-Item -LiteralPath $trigger -Force
        $triggersRemoved += $pair[0]
        Write-Output "[$($pair[0])] deleted a leftover (unconsumed) partner.trigger."
    }
}

# ---------------------------------------------------------------- lingering lab redirect
$hostCoop = (Get-InstancePaths -GameDirectory $config.Paths.hostDir).CoopIni
$hostCoopText = Read-SharedText -Path $hostCoop
$labCoop = (Test-Path -LiteralPath $hostCoop -PathType Leaf) -and (Test-LabGeneratedText $hostCoopText)
if ($labCoop) {
    $where = if ([string](Get-ConfigValue (ConvertFrom-IniText $hostCoopText) 'master_url' '') -ceq (Get-LabOfflineExpectation -Role host -Config $config).MasterUrl) {
        'pointed at a closed local port (the game runs offline)'
    } else { 'sent to the local fuse (or runs offline when fuse is stopped)' }
    Write-Warning "The host folder still has the lab coop.ini: a normal game start from $($config.Paths.hostDir) is $where. Run tools/lab/Prepare-LabInstance.ps1 -Disable before normal online play."
}

$stop = [ordered]@{
    product = 'mgs-coop-lab-stop'; finished_utc = [DateTime]::UtcNow.ToString('o')
    games = $outcome; fuse = $fuseState; real_saves = $saves; host_lab_coop_ini_present = [bool]$labCoop
    partner_triggers_removed = $triggersRemoved; log_end_offsets = $logEnds
}
if ($current) { Write-JsonFile -Path (Join-Path $current.Dir 'stop.json') -Value $stop }
# The run-start manifest is authoritative (normal play between Prepare and Start
# legitimately changes the real saves); the backup comparison is the fallback.
$changed = if ($null -ne $saves.unchanged_since_run_start) { !$saves.unchanged_since_run_start }
elseif ($null -ne $saves.unchanged_since_backup) { !$saves.unchanged_since_backup } else { $false }
if ($changed) {
    Write-Warning 'A real save file changed. The lab never writes there; check whether the save redirect was active (early.log saves=redirected) and restore from the backup if needed.'
    exit 2
}
exit 0
