#Requires -Version 7
<#
.SYNOPSIS
Runs one scripted test of both MGSV copies on a private, never-displayed desktop.
.DESCRIPTION
The single entry point for background game tests (AGENTS.md "Background agent
development"): audited worker checks, Start-WorkerSession (fresh probe, owned
install, copied saves, private desktop), the scenario steps through
Send-WorkerCommand only, then Stop-WorkerSession in a finally block, so the
baseline loader and configuration are restored and the real saves re-checked
even when a step fails or the session times out. Nothing is shown on, focused
on or sent to the user's desktop.

Scenario: one step per line (a file via -ScenarioPath or lines via -Step);
blank lines and lines starting with # are ignored. Roles: host, guest, both.

  title [timeoutSec]               press Enter on each role until it reports gameplay ready
  <role> press <keys> [ms]         keys joined by +: w a s d enter space escape e r shift ctrl 1 2 f lmb rmb
  <role> hold <keys> <ms>          hold for 1..1500 ms (repeat the step for longer holds)
  <role> mouse <dx> <dy>           relative mouse move, each -1500..1500
  <role> capture [label]           save the role's next rendered frame (BMP in the run folder)
  <role> debug <command>           one F9 debug-menu command through the game's own queue
  wait <ms>                        sleep up to 60000 ms
  state                            print both roles' mission / gameplay-ready state

The session has a hard 600 s limit (supervisor). The result object lists the
run folder, the frames (renamed with labels) and whether everything closed.
.EXAMPLE
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -Step 'title','both capture start','host hold w 1500','host capture walked'
#>
[CmdletBinding()]
param(
    [string]$ScenarioPath,
    [string[]]$Step=@(),
    [string]$CheckReceipt,
    [ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$BaselineLoaderSha256,
    [ValidateRange(60,600)][int]$TimeoutSec=600,
    [ValidateRange(10,180)][int]$ReadyTimeoutSec=180
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$worker=$PSScriptRoot
$build=Join-Path $repo 'build\worker-checks\normal-worker-on-release'

$lines=@()
if($ScenarioPath){$lines+=[IO.File]::ReadAllLines([IO.Path]::GetFullPath($ScenarioPath))}
$lines+=$Step
$steps=@($lines|ForEach-Object{$_.Trim()}|Where-Object{$_ -and !$_.StartsWith('#')})
if(!$steps.Count){throw 'No scenario steps.'}
# Validate every step before anything starts.
$roleNames=@('host','guest','both')
foreach($line in $steps){
    $t=$line -split '\s+'
    $ok=switch -Regex ($t[0]) {
        '^title$' {$t.Count -le 2}
        '^wait$' {$t.Count -eq 2 -and [int]$t[1] -ge 0 -and [int]$t[1] -le 60000}
        '^state$' {$t.Count -eq 1}
        default {
            $roleNames -ccontains $t[0] -and $t.Count -ge 2 -and $(switch($t[1]){
                'press'{$t.Count -in 3,4}; 'hold'{$t.Count -eq 4}; 'mouse'{$t.Count -eq 4}
                'capture'{$t.Count -le 3}; 'debug'{$t.Count -ge 3}; default{$false}})
        }
    }
    if(!$ok){throw "Invalid scenario step: '$line'"}
}

if(!$BaselineLoaderSha256){
    $record=Get-Content -LiteralPath 'D:\Games\steamapps\common\MGS_TPP\mgs-coop\probe-install.json' -Raw|ConvertFrom-Json
    $BaselineLoaderSha256=[string]$record.dll_sha256
}
if(!$CheckReceipt){
    $out=@(& (Join-Path $worker 'Invoke-WorkerChecks.ps1') -EnableBackgroundWorker 2>&1)
    $CheckReceipt=@($out|ForEach-Object{[string]$_}|Select-String -Pattern 'Worker check receipt: (.+receipt\.json)'|ForEach-Object{$_.Matches[0].Groups[1].Value})[-1]
    if(!$CheckReceipt){throw "Worker checks produced no receipt:`n$($out -join "`n")"}
}
$receipt=Get-Content -LiteralPath $CheckReceipt -Raw|ConvertFrom-Json
if($receipt.status -cne 'passed'){throw "Worker checks did not pass: $($receipt.failure)"}

$session=& (Join-Path $worker 'Start-WorkerSession.ps1') -BuildDirectory $build -CheckReceipt $CheckReceipt -BaselineLoaderSha256 $BaselineLoaderSha256 -TimeoutSec $TimeoutSec -ReadyTimeoutSec $ReadyTimeoutSec
$session=@($session|Where-Object{$_ -isnot [string]})[-1]
$run=[string]$session.RunDirectory
$frames=[Collections.Generic.List[object]]::new();$failure=$null;$stop=$null
function Read-RoleState([string]$Role){
    # Early state files lack later fields; always return the three used here.
    $s=try{Get-Content -LiteralPath (Join-Path $run "$Role.state.json") -Raw|ConvertFrom-Json -AsHashtable}catch{$null}
    if(!$s){$s=@{}}
    [pscustomobject]@{mission=$s['mission'];gameplay_ready=[bool]$s['gameplay_ready'];generation=$s['generation']}
}
function Send-Step([string]$Role,[hashtable]$Arguments){
    $result=& (Join-Path $worker 'Send-WorkerCommand.ps1') -RunDirectory $run -Role $Role @Arguments
    return @($result|Where-Object{$_ -isnot [string]})[-1]
}
function Wait-Frame([string]$Role,[uint64]$Sequence,[string]$Label){
    $path=Join-Path $run "$Role-frame-$Sequence.bmp"
    $deadline=[Environment]::TickCount64+5000
    while([Environment]::TickCount64 -lt $deadline -and !(Test-Path -LiteralPath $path)){Start-Sleep -Milliseconds 100}
    if(!(Test-Path -LiteralPath $path)){Write-Warning "[$Role] frame $Sequence not written";return}
    $name=if($Label){"$Role-$Label.bmp"}else{"$Role-frame-$Sequence.bmp"}
    if($Label){Copy-Item -LiteralPath $path -Destination (Join-Path $run $name)}
    $frames.Add([pscustomobject]@{Role=$Role;Label=$Label;Path=(Join-Path $run $name)})
}
try {
    foreach($line in $steps){
        $t=$line -split '\s+'
        Write-Host "step: $line"
        # if/elseif, not switch: 'continue' inside a PowerShell switch only leaves the switch.
        if($t[0] -ceq 'wait'){Start-Sleep -Milliseconds ([int]$t[1]);continue}
        if($t[0] -ceq 'state'){foreach($r in 'host','guest'){$s=Read-RoleState $r;Write-Host "  $r mission=$($s.mission) ready=$($s.gameplay_ready) generation=$($s.generation)"};continue}
        if($t[0] -ceq 'title'){
            $limit=[Environment]::TickCount64+1000*($(if($t.Count -eq 2){[int]$t[1]}else{240}))
            while([Environment]::TickCount64 -lt $limit){
                $pending=@('host','guest'|Where-Object{!((Read-RoleState $_).gameplay_ready)})
                if(!$pending.Count){break}
                foreach($r in $pending){$null=Send-Step $r @{Operation='press';Keys='enter';DurationMs=120}}
                Start-Sleep -Seconds 3
            }
            foreach($r in 'host','guest'){$s=Read-RoleState $r;Write-Host "  $r mission=$($s.mission) ready=$($s.gameplay_ready)"}
            continue
        }
        $targets=if($t[0] -ceq 'both'){@('host','guest')}else{@($t[0])}
        foreach($r in $targets){
            switch($t[1]){
                'press' {$null=Send-Step $r @{Operation='press';Keys=$t[2];DurationMs=$(if($t.Count -eq 4){[int]$t[3]}else{120})}}
                'hold' {$null=Send-Step $r @{Operation='hold';Keys=$t[2];DurationMs=[int]$t[3]}}
                'mouse' {$null=Send-Step $r @{Operation='mouse';Dx=[int]$t[2];Dy=[int]$t[3]}}
                'capture' {$c=Send-Step $r @{Operation='capture'};Wait-Frame $r ([uint64]$c.sequence) $(if($t.Count -eq 3){$t[2]}else{''})}
                'debug' {$null=Send-Step $r @{Operation='debug';DebugCommand=(($t|Select-Object -Skip 2) -join ' ')}}
            }
        }
        if($t[1] -in 'hold','press'){Start-Sleep -Milliseconds ($(if($t[1] -eq 'hold'){[int]$t[3]}else{150})+100)}
    }
} catch {
    $failure=$_.Exception.Message
    Write-Warning "Scenario stopped: $failure"
} finally {
    $stop=& (Join-Path $worker 'Stop-WorkerSession.ps1') -RunDirectory $run
    $stop=@($stop|Where-Object{$_ -isnot [string]})[-1]
}
[pscustomobject]@{
    RunDirectory=$run;Frames=$frames.ToArray();Failure=$failure
    Closed=$stop.Closed;BaselineRestored=$stop.BaselineRestored;RealSavesUnchanged=$stop.RealSavesUnchanged;Forced=$stop.Forced
}
