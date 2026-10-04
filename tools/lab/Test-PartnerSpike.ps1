#Requires -Version 7
<#
.SYNOPSIS
Log-only PASS / FAIL / INCOMPLETE verdict for one offline partner spike
experiment (docs/offline-partner-spike-plan.md section 3) of a lab run.

.DESCRIPTION
Reads each selected instance's script-observer.log, early.log and
steam-observer.log from the offsets Start-Lab.ps1 recorded in run.json (only
this run's lines, FileShare.ReadWrite so the game keeps writing), applies the
experiment's rules with the exact line spellings of section 2.7
(tools/lab/PartnerSpike.psm1) and prints one JSON object:

  { experiment, role, verdict, matched[], missing[], failed[], notes[],
    per_role{ <role>: { verdict, matched, missing, failed, notes, data } }, run{} }

PASS: every log criterion is met. FAIL: a criterion is shown not met (for
example online_bit=1, crash_probe=av, err_mm>5, partner_realized=0), or a
deadline measured on the probe clock (t_ms) passed without the evidence.
INCOMPLETE: evidence is still missing; run it again later. Screenshot criteria
are listed in notes as manual. Experiments are judged on the segment of the log
in which the experiment's configuration was active (from the partner=configured
or partner_stage=changed line that started it), so E1..E5 can follow each other
in one boot. Default role: both for E0 and E6, host otherwise.

The instances' liveness is part of the verdict while the run is up; after
Stop-Lab.ps1 (stop.json present) or with -NoProcessCheck it is not checked.
Stop-Lab.ps1 records each log's length in stop.json (log_end_offsets); a
stopped run is read only up to there, so judging an older run with -RunDir
never mixes in the lines of later runs (an older run without those offsets is
read to the end of the logs, with a note).
-OutFile also writes the JSON (redacted) to a file. -PassThru returns the object
instead of printing it (used by Invoke-PartnerExperiment.ps1).
Exit code: 0 PASS, 1 FAIL, 2 INCOMPLETE.

.EXAMPLE
pwsh -File tools/lab/Test-PartnerSpike.ps1 -Experiment E0
pwsh -File tools/lab/Test-PartnerSpike.ps1 -Experiment E3 -Role guest
pwsh -File tools/lab/Test-PartnerSpike.ps1 -Experiment E6 -RunDir build/lab/runs/20261004-120000 -OutFile e6.json
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('E0', 'E1', 'E2', 'E3', 'E4', 'E5', 'E6', 'E8', 'A0', 'A1', 'A3', 'M1', 'N1', 'B1', 'B4', 'C1', 'L1', 'L2', 'L3', 'L4', 'I0', 'I1', 'I2', 'I3', 'I3x', 'I4', 'I5', 'I6', 'I7', 'D0', 'D1', 'D2', 'D3')][string]$Experiment,
    [ValidateSet('host', 'guest', 'both')][string]$Role,
    [string]$RunDir,
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    [string]$OutFile,
    [switch]$NoProcessCheck,
    [switch]$PassThru
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'PartnerSpike.psm1') -Force

$config = Get-LabConfig -ConfigPath $ConfigPath
$current = Get-CurrentRun -Config $config -RunDir $RunDir
$run = $current.Run
if (!$Role) { $Role = Get-PartnerExperimentDefaultRole -Experiment $Experiment }
$available = @($run.instances.PSObject.Properties.Name)
$roles = @(if ($Role -eq 'both') { @('host', 'guest') | Where-Object { $available -contains $_ } } else { $Role })
foreach ($name in $roles) { if ($available -notcontains $name) { throw "run.json has no $name instance." } }
if (!$roles.Count) { throw 'run.json has no instances.' }
$offline = [bool](Get-ConfigValue $run 'offline' $false)
# E0 and E6 need both instances: a run without a guest instance counts as host-only.
$hostOnly = [bool](Get-ConfigValue $run 'host_only' $false) -or $available -notcontains 'guest'
$stopFile = Join-Path $current.Dir 'stop.json'
$stopped = Test-Path -LiteralPath $stopFile -PathType Leaf
$endOffsets = $null
if ($stopped) {
    try { $endOffsets = Get-ConfigValue (Get-Content -LiteralPath $stopFile -Raw -Encoding UTF8 | ConvertFrom-Json) 'log_end_offsets' $null } catch { $endOffsets = $null }
}
$latest = $false
try {
    $pointer = Join-Path $config.Paths.runDir 'current-run.json'
    $latestDir = [string](Get-Content -LiteralPath $pointer -Raw -Encoding UTF8 | ConvertFrom-Json).run_dir
    $latest = [IO.Path]::GetFullPath($latestDir).TrimEnd([char]92, [char]47).Equals([IO.Path]::GetFullPath($current.Dir).TrimEnd([char]92, [char]47), [StringComparison]::OrdinalIgnoreCase)
} catch { $latest = $false }
$readNotes = [Collections.Generic.List[string]]::new()
if (!$latest -and !$endOffsets) { $readNotes.Add('this run is not the current one and stop.json has no log_end_offsets: lines of later runs in the same logs are included') }

# This run's log text per instance (both instances: E6 compares them).
$texts = @{}
foreach ($name in @(@('host', 'guest') | Where-Object { $available -contains $_ })) {
    $instance = $run.instances.$name
    $paths = Get-InstancePaths -GameDirectory ([string]$instance.dir)
    $logs = @{}
    $ends = if ($endOffsets) { Get-ConfigValue $endOffsets $name $null } else { $null }
    foreach ($log in 'script-observer.log', 'early.log', 'steam-observer.log') {
        $offset = [Int64](Get-ConfigValue $instance.log_offsets $log 0)
        $max = [Int64]64MB
        $end = if ($ends) { Get-ConfigValue $ends $log $null } else { $null }
        if ($null -ne $end) {
            # stop.json: the log length when the run was stopped (later runs append after it).
            if ([Int64]$end -lt $offset) { $readNotes.Add("[$name] $log is shorter at the stop than at the start (replaced?): not read"); $logs[$log] = ''; continue }
            $max = [Math]::Min($max, [Int64]$end - $offset)
        }
        $logs[$log] = if ($max -gt 0) { (Read-LogFromOffset -Path (Join-Path $paths.Mod $log) -Offset $offset -MaxBytes ([int]$max)).Text } else { '' }
    }
    $texts[$name] = $logs
}

$perRole = [ordered]@{}
foreach ($name in $roles) {
    $instance = $run.instances.$name
    $expectation = Get-InstanceExpectation -Role $name -Instance $instance
    $safety = Get-InstanceSafety -Role $name -Expectation $expectation -EarlyLog $texts[$name]['early.log'] -SteamLog $texts[$name]['steam-observer.log']
    $alive = $null
    if (!$NoProcessCheck -and !$stopped) {
        $alive = $null -ne (Get-LiveProcess -Id ([int](Get-ConfigValue $instance 'pid' 0)) -ExpectedPath ([string](Get-ConfigValue $instance 'exe' '')))
    }
    $peerName = if ($name -eq 'host') { 'guest' } else { 'host' }
    $peerText = if ($texts.ContainsKey($peerName)) { $texts[$peerName]['script-observer.log'] } else { '' }
    $perRole[$name] = Get-PartnerSpikeVerdict -Experiment $Experiment -Role $name -ObserverLog $texts[$name]['script-observer.log'] `
        -EarlyLog $texts[$name]['early.log'] -SteamLog $texts[$name]['steam-observer.log'] -PeerObserverLog $peerText `
        -Safety $safety -ExpectedMasterUrl $expectation.Url -OfflineRun $offline -HostOnly $hostOnly -Alive $alive
}
if ($Role -eq 'both' -and $roles.Count -lt 2) { $perRole[$roles[0]].notes = @($perRole[$roles[0]].notes) + 'host-only run: the guest was not evaluated' }

$result = Merge-PartnerSpikeVerdict -Experiment $Experiment -Role $Role -PerRole $perRole
if (!$offline) { $result.notes = @($result.notes) + 'this run was not started with Start-Lab.ps1 -Offline' }
if ($readNotes.Count) { $result.notes = @($result.notes) + $readNotes.ToArray() }
if ($stopped -and !$NoProcessCheck) { $result.notes = @($result.notes) + 'run stopped (stop.json present): process liveness not checked' }
$result | Add-Member -NotePropertyName run -NotePropertyValue ([ordered]@{
        run_dir = $current.Dir; stamp = [string](Get-ConfigValue $run 'stamp' ''); offline = $offline; host_only = $hostOnly
        current = $latest; bounded_by_stop = [bool]$endOffsets
        checked_utc = [DateTime]::UtcNow.ToString('o')
    })

$json = $result | ConvertTo-Json -Depth 8
if ($OutFile) { Write-RedactedText -Path ([IO.Path]::GetFullPath($OutFile)) -Text ($json + "`n") -Keep @([string](Get-ConfigValue $config.Identity 'guestSteamId' '')) }
if ($PassThru) { return $result }
Write-Output $json
switch ($result.verdict) {
    'PASS' { exit 0 }
    'FAIL' { exit 1 }
    default { exit 2 }
}
