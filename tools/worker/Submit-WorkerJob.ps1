#Requires -Version 7
<#
.SYNOPSIS
Queues one hidden-worker run (one session, one or several scenarios) instead of waiting for the verify lock yourself.
.DESCRIPTION
Writes a job file to %TEMP%\mgs-coop-queue\jobs (tools/worker/README.md "Test queue"): the job's repo (your worktree),
its scenarios (absolute paths; relative ones are taken from -Repo) and the run flags, with an owner label. The single
queue runner (tools/worker/Invoke-WorkerQueue.ps1) takes the jobs oldest first, waits for the resource floors and the
verify lock, runs tools/worker/Invoke-WorkerScenario.ps1 of the job's repo with all its scenarios in one session, and
writes done\<id>.result.json (status passed|failed|error|refused|interrupted, run id, run folder, per-part results).
A runner is started (hidden, no window) when none is alive, unless -NoStart. -Wait waits for the result and prints it.
.EXAMPLE
pwsh -NoProfile -File tools/worker/Submit-WorkerJob.ps1 -Owner wp-vspawn -ScenarioPath tools/worker/scenarios/a.txt,tools/worker/scenarios/b.txt -Mode play -WanSim bad -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed afgh-ledge -Wait
#>
[CmdletBinding()]
param(
    # Who asked (a branch or round name); shown in the lock owner and the result.
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9._-]{1,40}$')][string]$Owner,
    # The worktree whose tools/worker/Invoke-WorkerScenario.ps1 and build run the job ('' = this script's repo).
    [string]$Repo,
    # Scenario files (a.txt,b.txt also as one pwsh -File argument) and/or a batch file listing them, one per line.
    [string[]]$ScenarioPath=@(),
    [string]$ScenarioList,
    # The run flags, passed to Invoke-WorkerScenario as given (it validates them again); unnamed ones keep its defaults.
    [ValidateSet('lab','play')][string]$Mode,
    [ValidateSet('off','good','bad','awful')][string]$WanSim,
    [ValidateSet('animated','input','native')][string]$PartnerProfile,
    [ValidateSet('','census','loco','gun')][string]$NativeStage,
    [ValidateSet('normal','research')][string]$BuildProfile,
    [ValidatePattern('^[a-z][a-z0-9-]{1,31}$')][string]$SaveSeed,
    [ValidatePattern('^$|^[a-z][a-z0-9-]{1,31}$')][string]$HostSaveSeed,
    [ValidatePattern('^$|^[a-z][a-z0-9-]{1,31}$')][string]$GuestSaveSeed,
    [ValidatePattern('^$|^[0-9a-f]{32}$')][string]$GuestSaveSeedRun,
    [ValidateSet('','preserve','low')][string]$GraphicsQuality,
    # key=value or host:/guest:key=value; several as separate values or joined by commas.
    [string[]]$LabSetting=@(),
    [switch]$FreshChecks,
    [switch]$AmmoExperiment,
    [ValidateRange(60,600)][int]$TimeoutSec,
    # Wait for the result and print it (the job's phase is shown while it waits for the floors or the lock).
    [switch]$Wait,
    [ValidateRange(1,1440)][int]$WaitTimeoutMin=360,
    # Only queue the job; start no runner.
    [switch]$NoStart,
    [string]$QueueRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'WorkerQueueCommon.psm1') -Force -DisableNameChecking
if(!$Repo){$Repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)}
$Repo=[IO.Path]::GetFullPath($Repo)
if(!(Test-Path -LiteralPath (Join-Path $Repo 'tools\worker\Invoke-WorkerScenario.ps1') -PathType Leaf)){throw "Not a mgs-coop worktree (no tools\worker\Invoke-WorkerScenario.ps1): $Repo"}
$scenarios=@(Resolve-WorkerQueueScenarios $Repo $ScenarioPath $ScenarioList)
if($scenarios.Count -gt 32){throw 'At most 32 scenarios in one job.'}
$runArgs=[ordered]@{}
foreach($name in 'Mode','WanSim','PartnerProfile','NativeStage','BuildProfile','SaveSeed','HostSaveSeed','GuestSaveSeed','GuestSaveSeedRun','GraphicsQuality','TimeoutSec'){
    if($PSBoundParameters.ContainsKey($name)){$runArgs[$name]=$PSBoundParameters[$name]}
}
$settings=@($LabSetting|ForEach-Object{$_ -split ','}|ForEach-Object{$_.Trim()}|Where-Object{$_})
if($settings.Count){$runArgs['LabSetting']=$settings}
if($FreshChecks){$runArgs['FreshChecks']=$true}
if($AmmoExperiment){$runArgs['AmmoExperiment']=$true}

$root=Get-WorkerQueueRoot $QueueRoot
foreach($folder in 'jobs','running','done','logs'){$null=New-Item -ItemType Directory -Force -Path (Join-Path $root $folder)}
$id=New-WorkerQueueJobId
$job=[ordered]@{format='mgs-coop-worker-job';v=1;id=$id;owner=$Owner;repo=$Repo;scenarios=$scenarios;args=$runArgs;
    submitted_utc=[DateTime]::UtcNow.ToString('o');submitter_pid=$PID}
$jobPath=Join-Path $root "jobs\$id.json"
Write-WorkerQueueJson $jobPath $job
Write-Host "Job $id queued: $($scenarios.Count) scenario(s) for $Owner -> $jobPath"

$runnerScript=Join-Path $PSScriptRoot 'Invoke-WorkerQueue.ps1'
$script:runnerStartedAt=$null
function Start-QueueRunnerIfIdle{
    if($NoStart -or (Get-WorkerQueueRunner $root)){return}
    # A runner started here may still be starting (pwsh, then runner.lock): give it a minute before another.
    if($script:runnerStartedAt -and ((Get-Date)-$script:runnerStartedAt).TotalSeconds -lt 60){return}
    $script:runnerStartedAt=Get-Date
    $pwsh=(Get-Process -Id $PID).Path
    # Hidden and detached (no inherited handles): the runner outlives this call and logs to runner.log.
    $null=Start-Process -FilePath $pwsh -WindowStyle Hidden -ArgumentList "-NoProfile -NonInteractive -File `"$runnerScript`" -QueueRoot `"$root`""
    Write-Host "Started the queue runner ($runnerScript)."
}
Start-QueueRunnerIfIdle
if(!$Wait){return}

$resultPath=Join-Path $root "done\$id.result.json"
$limit=(Get-Date).AddMinutes($WaitTimeoutMin);$shown=''
while(!(Test-Path -LiteralPath $resultPath)){
    if((Get-Date) -gt $limit){throw "Job $id has no result after $WaitTimeoutMin min; it stays queued ($root)."}
    $runningPath=Join-Path $root "running\$id.json"
    $state=if(Test-Path -LiteralPath $runningPath){try{Read-WorkerQueueJson $runningPath}catch{$null}}else{$null}
    $now=if($state){"$($state['phase']): $($state['reason'])"}elseif(Test-Path -LiteralPath $jobPath){"queued behind $(@(Get-ChildItem -LiteralPath (Join-Path $root 'jobs') -Filter '*.json' -File|Where-Object{$_.Name -lt "$id.json"}).Count) job(s)"}else{''}
    if($now -and $now -cne $shown){Write-Host "  $now";$shown=$now}
    # A runner that ended while this job still waits (e.g. it was stopped): start another.
    if(Test-Path -LiteralPath $jobPath){Start-QueueRunnerIfIdle}
    Start-Sleep -Seconds 5
}
$result=Read-WorkerQueueJson $resultPath
Write-Host "Job $id finished: run $($result['run_id']) ($($result['run_dir']))"
Write-Host "status: $($result['status'])"
foreach($p in @($result['parts'])){
    if(!$p){continue}
    Write-Host ("  {0} {1}: {2}{3}" -f $p['index'],$p['name'],$p['status'],$(if($p['failure']){' - '+$p['failure']}else{''}))
    foreach($x in @($p['expect_failures'])){if($x){Write-Host "      expect failed: $x"}}
}
if($result['failure']){Write-Host "failure: $($result['failure'])"}
Write-Host "result: $resultPath"
Write-Host "log: $($result['log'])"
