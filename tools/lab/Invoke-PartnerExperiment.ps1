#Requires -Version 7
<#
.SYNOPSIS
Runs one offline partner spike experiment on the current lab run: triggers,
keyboard input and screenshots, then polls Test-PartnerSpike.ps1 until the
verdict is PASS or FAIL or the timeout passes.

.DESCRIPTION
The steps come from Get-PartnerExperimentPlan (PartnerSpike.psm1), following
docs/offline-partner-spike-plan.md section 3:
  E0  host walks a circle (w+a:6000) for the units check; verdict on both
      instances (needs 5 minutes of probes, so the default timeout is 7 min)
  E1  host trigger 'stage=1 source=fixed apply=warp'
  E2  host trigger 'stage=2'
  E3  host trigger 'stage=3' (bits, parts, realization; 120 s stability)
  E4  host trigger 'stage=3 source=orbit', 4 screenshots 2 s apart
  E5  host 'stage=3 source=orbit apply=slot', then 'apply=both'
  E6  both 'stage=3 source=net', the guest walks forward (w:4000), host screenshots
  E8  host 'stop', then 'stage=3'; then leave and re-enter Mother Base by hand
Run it after Start-Lab.ps1 -Offline once both instances are in Mother Base
(Test-PartnerSpike.ps1 -Experiment E0 shows partner_cfg=ready). Keyboard input
needs window focus and is best effort; -NoInput prints the keys to press
instead. -NoTrigger skips the triggers (for a run whose lab.ini already has the
stage), -NoScreens the screenshots. The final verdict is written (redacted) to
<run>/partner-<experiment>-<HHmmss>.json and printed.
Exit code: 0 PASS, 1 FAIL, 2 INCOMPLETE at the timeout.

.EXAMPLE
pwsh -File tools/lab/Invoke-PartnerExperiment.ps1 -Experiment E1
pwsh -File tools/lab/Invoke-PartnerExperiment.ps1 -Experiment E6 -TimeoutSec 180
pwsh -File tools/lab/Invoke-PartnerExperiment.ps1 -Experiment E0 -NoInput
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('E0', 'E1', 'E2', 'E3', 'E4', 'E5', 'E6', 'E8')][string]$Experiment,
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    [string]$RunDir,
    [ValidateRange(0, 7200)][int]$TimeoutSec = 0,
    [ValidateRange(1, 60)][int]$PollSec = 5,
    [switch]$NoInput,
    [switch]$NoScreens,
    [switch]$NoTrigger,
    [switch]$NoProcessCheck
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'PartnerSpike.psm1') -Force

$config = Get-LabConfig -ConfigPath $ConfigPath
$current = Get-CurrentRun -Config $config -RunDir $RunDir
if (![bool](Get-ConfigValue $current.Run 'offline' $false)) { throw 'This run was not started with Start-Lab.ps1 -Offline; the partner experiments need the offline lab mode.' }
$available = @($current.Run.instances.PSObject.Properties.Name)
$plan = Get-PartnerExperimentPlan -Experiment $Experiment
if (!$TimeoutSec) { $TimeoutSec = $plan.TimeoutSec }
function Write-Step([string]$Message) { Write-Output ('[{0:HH:mm:ss}] {1}' -f (Get-Date), $Message) }
function Get-StepRoles([string]$Role) { if ($Role -eq 'both') { return @(@('host', 'guest') | Where-Object { $available -contains $_ }) } return @($Role) }

Write-Step "$Experiment on $($current.Dir) (timeout $TimeoutSec s)"
foreach ($step in $plan.Steps) {
    switch ($step.Kind) {
        'trigger' {
            if ($NoTrigger) { Write-Step "trigger skipped (-NoTrigger): $($step.Role) '$($step.Value)'"; break }
            $global:LASTEXITCODE = 0
            & (Join-Path $PSScriptRoot 'Set-PartnerTrigger.ps1') -Role $step.Role -Text $step.Value -ConfigPath $ConfigPath -RunDir $current.Dir -Force -WaitSec 10
            if ($LASTEXITCODE -eq 2) { Write-Warning "The trigger '$($step.Value)' was not consumed within 10 s; is Mother Base (30050) loaded on that instance?" }
        }
        'input' {
            foreach ($role in (Get-StepRoles $step.Role)) {
                if ($NoInput) { Write-Step "MANUAL: in the $role window press $($step.Value) (key spec: keys joined by +, :ms = hold time)"; continue }
                & (Join-Path $PSScriptRoot 'Send-LabInput.ps1') -Role $role -Keys $step.Value -ConfigPath $ConfigPath -RunDir $current.Dir
            }
        }
        'screens' {
            if ($NoScreens) { break }
            try { $null = & (Join-Path $PSScriptRoot 'Get-LabScreens.ps1') -ConfigPath $ConfigPath -RunDir $current.Dir -Label $step.Value -PassThru }
            catch { Write-Warning "Screenshots failed: $($_.Exception.Message)" }
        }
        'manual' { Write-Step "MANUAL: $($step.Value)" }
    }
    if ($step.WaitSec -gt 0) { Start-Sleep -Seconds $step.WaitSec }
}

$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
$lastMissing = $null
while ($true) {
    $verdict = & (Join-Path $PSScriptRoot 'Test-PartnerSpike.ps1') -Experiment $Experiment -RunDir $current.Dir -ConfigPath $ConfigPath -NoProcessCheck:$NoProcessCheck -PassThru
    if ($verdict.verdict -ne 'INCOMPLETE') { break }
    $missing = @($verdict.missing) -join '; '
    if ($missing -cne $lastMissing) { Write-Step "waiting: $missing"; $lastMissing = $missing }
    if ([DateTime]::UtcNow -ge $deadline) { break }
    Start-Sleep -Seconds $PollSec
}
$json = $verdict | ConvertTo-Json -Depth 8
$out = Join-Path $current.Dir ('partner-{0}-{1}.json' -f $Experiment, (Get-Date).ToString('HHmmss'))
Write-RedactedText -Path $out -Text ($json + "`n") -Keep @([string](Get-ConfigValue $config.Identity 'guestSteamId' ''))
Write-Output $json
Write-Step "RESULT: $Experiment $($verdict.verdict); details: $out"
switch ($verdict.verdict) {
    'PASS' { exit 0 }
    'FAIL' { exit 1 }
    default { exit 2 }
}
