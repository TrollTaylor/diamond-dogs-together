#Requires -Version 7
<#
.SYNOPSIS
The single runner of the hidden-run queue: takes the queued jobs oldest first and runs each as one hidden session.
.DESCRIPTION
tools/worker/README.md "Test queue". Started on demand by Submit-WorkerJob.ps1 (or by hand); only one runner runs
(runner.lock: pid and start time; a lock whose process is gone is taken over). For each job in
%TEMP%\mgs-coop-queue\jobs (oldest first):
  1. moves it to running\ and checks it (its repo, scenarios and flags);
  2. checks the floors once: C: >= 6 GB free, D: >= 3 GB free (when there is a D:), commit free >= 15 GB; while one
     is short the job waits (phase waiting_floors, the reason in the job file) and they are checked again every 3 min;
  3. takes the shared verify lock %TEMP%\mgs-coop-verify.lock exactly as by hand (mkdir, then owner.txt
     "queue pid=.. start=.. time=.. job=.. owner=.. repo=..") and waits for running CTest suites (VerifyLock.psm1
     Wait-CtestReaders); a lock held by anyone else is waited for (polled every 30 s) and never removed;
  4. runs the job repo's tools/worker/Invoke-WorkerScenario.ps1 in that repo, all the job's scenarios as parts of one
     session (-ScenarioList), stdin from NUL, without MGS_GAME_LUA_DIR, output to logs\<id>.log;
  5. releases the verify lock (only its own) and writes done\<id>.result.json: status passed|failed|error|refused, run id,
     run folder, per-part results, floor and lock waits.
A job found in running\ whose runner is gone is closed as interrupted (never rerun on its own). The runner exits when
the queue is empty. Optional %TEMP%\mgs-coop-queue\queue.json: min_free_c_gb, min_free_d_gb, min_commit_free_gb,
floor_retry_sec, lock_retry_sec, resource_file (fixture measurements {c_gb,d_gb,commit_gb} for tests).
#>
[CmdletBinding()]
param([string]$QueueRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $PSScriptRoot 'WorkerQueueCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repo 'tools\VerifyLock.psm1') -Force -DisableNameChecking
$root=Get-WorkerQueueRoot $QueueRoot
foreach($folder in 'jobs','running','done','logs'){$null=New-Item -ItemType Directory -Force -Path (Join-Path $root $folder)}
$pwsh=(Get-Process -Id $PID).Path
$myStart=Get-WorkerQueueStartKey $PID

$config=[ordered]@{min_free_c_gb=6.0;min_free_d_gb=3.0;min_commit_free_gb=15.0;floor_retry_sec=180;lock_retry_sec=30;resource_file=$null}
$configPath=Join-Path $root 'queue.json'
if(Test-Path -LiteralPath $configPath){
    $custom=Read-WorkerQueueJson $configPath
    foreach($key in @($custom.Keys)){
        if(!$config.Contains($key)){throw "Unknown queue.json key: $key"}
        $config[$key]=if($key -ceq 'resource_file'){[string]$custom[$key]}elseif($key -clike '*_sec'){[Math]::Max(1,[int]$custom[$key])}else{[double]$custom[$key]}
    }
}

function Write-QueueLog([string]$Text){
    $line="[$([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))] $Text"
    Write-Host $line
    try{Add-Content -LiteralPath (Join-Path $root 'runner.log') -Value $line}catch{}
}

# Pure: the floors a measurement ({c_gb,d_gb,commit_gb}; $null = not measured, e.g. no D:) falls short of.
function Get-QueueFloorShortfall($Measure,$Floors){
    $short=@()
    $inv=[Globalization.CultureInfo]::InvariantCulture
    if($null -ne $Measure['c_gb'] -and [double]$Measure['c_gb'] -lt [double]$Floors['min_free_c_gb']){
        $short+=[string]::Format($inv,'C: {0:0.0} GB free < {1} GB',[double]$Measure['c_gb'],$Floors['min_free_c_gb'])}
    if($null -ne $Measure['d_gb'] -and [double]$Measure['d_gb'] -lt [double]$Floors['min_free_d_gb']){
        $short+=[string]::Format($inv,'D: {0:0.0} GB free < {1} GB',[double]$Measure['d_gb'],$Floors['min_free_d_gb'])}
    if($null -ne $Measure['commit_gb'] -and [double]$Measure['commit_gb'] -lt [double]$Floors['min_commit_free_gb']){
        $short+=[string]::Format($inv,'commit free {0:0.0} GB < {1} GB',[double]$Measure['commit_gb'],$Floors['min_commit_free_gb'])}
    return $short
}
# The machine now: free space of C: and D: and the free commit (commit limit minus commit charge).
function Get-QueueResources{
    if($config.resource_file){
        $m=Read-WorkerQueueJson $config.resource_file
        return @{c_gb=$m['c_gb'];d_gb=$m['d_gb'];commit_gb=$m['commit_gb']}
    }
    $free={param($letter)try{$d=[IO.DriveInfo]::new($letter);if($d.IsReady){$d.AvailableFreeSpace/1GB}else{$null}}catch{$null}}
    $os=Get-CimInstance -ClassName Win32_OperatingSystem
    return @{c_gb=(& $free 'C');d_gb=(& $free 'D');commit_gb=([double]$os.FreeVirtualMemory*1KB/1GB)}
}

# ---- One runner at a time ------------------------------------------------------------------------------------------
$runnerLock=Join-Path $root 'runner.lock'
$runnerText="pid=$PID start=$myStart time=$([DateTime]::UtcNow.ToString('o')) repo=$repo"
function Enter-QueueRunnerLock{
    for($attempt=0;$attempt -lt 3;$attempt++){
        try{
            $stream=[IO.File]::Open($runnerLock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
            try{$bytes=[Text.Encoding]::UTF8.GetBytes($runnerText);$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
            return $true
        }catch [IO.IOException]{}
        $holder=Read-WorkerQueueText $runnerLock
        if($null -eq $holder){continue}
        if(Test-WorkerQueueOwnerAlive $holder){return $false}
        # A lock being written right now has no text yet: not stale.
        if(!$holder.Trim() -and ((Get-Date)-(Get-Item -LiteralPath $runnerLock).LastWriteTime).TotalSeconds -lt 10){return $false}
        # Stale (its runner is gone): move it aside (one taker wins), then try again.
        $aside="$runnerLock.stale-$([Guid]::NewGuid().ToString('N').Substring(0,8))"
        try{[IO.File]::Move($runnerLock,$aside);Write-QueueLog "Took over a stale runner lock: $($holder.Trim())";Remove-Item -LiteralPath $aside -Force}catch{}
    }
    return $false
}
function Exit-QueueRunnerLock{
    if((Read-WorkerQueueText $runnerLock) -ceq $runnerText){Remove-Item -LiteralPath $runnerLock -Force -ErrorAction SilentlyContinue}
}

# ---- The verify lock (the same protocol as by hand: mkdir, owner.txt; only our own is ever removed) -----------------
Add-Type -Namespace MgsCoopQueue -Name Native -MemberDefinition '[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern bool CreateDirectoryW(string path, IntPtr security);'
function Enter-QueueVerifyLock([string]$Owner){
    $lock=Get-VerifyLockPath
    # CreateDirectoryW fails when the folder exists: exactly one taker wins.
    if(![MgsCoopQueue.Native]::CreateDirectoryW($lock,[IntPtr]::Zero)){return $false}
    [IO.File]::WriteAllText((Join-Path $lock 'owner.txt'),$Owner)
    return $true
}
function Exit-QueueVerifyLock([string]$Owner){
    $lock=Get-VerifyLockPath
    $text=Read-WorkerQueueText (Join-Path $lock 'owner.txt')
    if($text -ceq $Owner){Remove-Item -LiteralPath $lock -Recurse -Force;return $true}
    Write-QueueLog "The verify lock is no longer ours; left as it is: $text"
    return $false
}

# ---- Jobs -----------------------------------------------------------------------------------------------------------
$runArgNames=@{Mode='string';WanSim='string';PartnerProfile='string';NativeStage='string';BuildProfile='string';SaveSeed='string';
    HostSaveSeed='string';GuestSaveSeed='string';GuestSaveSeedRun='string';GraphicsQuality='string';TimeoutSec='int';
    LabSetting='strings';FreshChecks='bool';AmmoExperiment='bool'}
function Get-QueueJobFiles([string]$Folder){
    return @(Get-ChildItem -LiteralPath (Join-Path $root $Folder) -Filter '*.json' -File|Where-Object{Test-WorkerQueueJobId $_.BaseName}|Sort-Object Name)
}
function Save-QueueJobState([string]$Path,$Job,[string]$Phase,[string]$Reason){
    $Job['phase']=$Phase;$Job['reason']=$Reason;$Job['phase_utc']=[DateTime]::UtcNow.ToString('o')
    Write-WorkerQueueJson $Path $Job
}
function Complete-QueueJob([string]$Id,[string]$RunningPath,$Job,$Result){
    $Result['finished_utc']=[DateTime]::UtcNow.ToString('o')
    Write-WorkerQueueJson (Join-Path $root "done\$Id.result.json") $Result
    foreach($suffix in '.splat','.out','.scenarios.txt'){
        $extra=Join-Path $root "running\$Id$suffix"
        if(Test-Path -LiteralPath $extra){Move-Item -LiteralPath $extra -Destination (Join-Path $root "done\$Id$suffix") -Force}
    }
    if($Job){Save-QueueJobState $RunningPath $Job 'done' $Result['status']}
    Move-Item -LiteralPath $RunningPath -Destination (Join-Path $root "done\$Id.json") -Force
    Write-QueueLog "Job $Id ($($Result['owner'])): $($Result['status'])$(if($Result['run_id']){' run '+$Result['run_id']})$(if($Result['failure']){' - '+$Result['failure']})"
}
function New-QueueResult([string]$Id,$Job){
    $get={param($k)if($Job -is [Collections.IDictionary] -and $Job.Contains($k)){$Job[$k]}else{$null}}
    return [ordered]@{format='mgs-coop-worker-job-result';v=1;id=$Id;owner=(& $get 'owner');repo=(& $get 'repo');scenarios=@(& $get 'scenarios');
        args=(& $get 'args');status=$null;failure=$null;run_id=$null;run_dir=$null;exit_code=$null;parts=@();expect_failures=@();
        closed=$null;baseline_restored=$null;real_saves_unchanged=$null;forced=$null;submitted_utc=(& $get 'submitted_utc');
        started_utc=[DateTime]::UtcNow.ToString('o');finished_utc=$null;floor_wait_s=0;lock_wait_s=0;
        log=(Join-Path $root "logs\$Id.log");runner_pid=$PID}
}
# A job file is data: its repo, scenarios and run flags must be exactly the expected shapes. Returns the parameter
# names of the repo's runner (Invoke-WorkerScenario.ps1), or throws the refusal.
function Assert-QueueJob([string]$Id,$Job){
    if($Job -isnot [Collections.IDictionary] -or $Job['format'] -cne 'mgs-coop-worker-job' -or [int]$Job['v'] -ne 1 -or $Job['id'] -cne $Id){throw 'not a v1 job file of this id'}
    if([string]$Job['owner'] -cnotmatch '^[A-Za-z0-9._-]{1,40}$'){throw 'bad owner label'}
    $jobRepo=[string]$Job['repo']
    if(![IO.Path]::IsPathFullyQualified($jobRepo) -or !(Test-Path -LiteralPath (Join-Path $jobRepo 'tools\worker\Invoke-WorkerScenario.ps1') -PathType Leaf)){throw "no tools\worker\Invoke-WorkerScenario.ps1 in the job repo $jobRepo"}
    $scenarios=@($Job['scenarios'])
    if($scenarios.Count -lt 1 -or $scenarios.Count -gt 32){throw 'a job has 1..32 scenarios'}
    foreach($s in $scenarios){if($s -isnot [string] -or ![IO.Path]::IsPathFullyQualified($s) -or !(Test-Path -LiteralPath $s -PathType Leaf)){throw "scenario not found: $s"}}
    $runArgs=$Job['args']
    if($null -ne $runArgs -and $runArgs -isnot [Collections.IDictionary]){throw 'args is not an object'}
    foreach($name in @(if($runArgs){$runArgs.Keys})){
        $kind=$runArgNames[$name]
        $value=$runArgs[$name]
        $ok=switch($kind){
            'string'{$value -is [string] -and $value -cnotmatch '[\x00-\x1f]'}
            'int'{$value -is [long] -or $value -is [int]}
            'bool'{$value -is [bool]}
            'strings'{@($value|Where-Object{$_ -isnot [string] -or $_ -cmatch '[\x00-\x1f]'}).Count -eq 0}
            default{$false}
        }
        if(!$ok){throw "unknown or malformed run flag: $name"}
    }
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $jobRepo 'tools\worker\Invoke-WorkerScenario.ps1'),[ref]$tokens,[ref]$errors)
    if($errors.Count -or !$ast.ParamBlock){throw "the job repo's Invoke-WorkerScenario.ps1 does not parse"}
    $names=@($ast.ParamBlock.Parameters|ForEach-Object{$_.Name.VariablePath.UserPath})
    if($scenarios.Count -gt 1 -and $names -cnotcontains 'ScenarioList'){
        throw "the job repo's Invoke-WorkerScenario.ps1 runs one scenario per session (no -ScenarioList); merge dev into it or submit one scenario per job"
    }
    return $names
}
function ConvertTo-QueueLiteral([string]$Text){return "'"+$Text.Replace("'","''")+"'"}
# Runs the job in its repo: a child pwsh (stdin from NUL, output appended to the job log, no MGS_GAME_LUA_DIR).
# Returns the exit code; the run's last result object (or {Error}) is in running\<id>.out.
function Invoke-QueueRun([string]$Id,$Job,[string[]]$RunnerParameters){
    $splat=[ordered]@{}
    foreach($name in @(if($Job['args']){$Job['args'].Keys})){$splat[$name]=$Job['args'][$name]}
    $scenarios=@($Job['scenarios'])
    if($RunnerParameters -ccontains 'ScenarioList'){
        $list=Join-Path $root "running\$Id.scenarios.txt"
        [IO.File]::WriteAllText($list,"# queue job $Id ($($Job['owner']))`r`n"+(($scenarios -join "`r`n")+"`r`n"))
        $splat['ScenarioList']=$list
    }else{$splat['ScenarioPath']=$scenarios[0]}
    $splatPath=Join-Path $root "running\$Id.splat";Write-WorkerQueueJson $splatPath $splat
    $outPath=Join-Path $root "running\$Id.out"
    $script=Join-Path ([string]$Job['repo']) 'tools\worker\Invoke-WorkerScenario.ps1'
    $wrapper=@"
`$ErrorActionPreference='Stop'
`$p=Get-Content -LiteralPath $(ConvertTo-QueueLiteral $splatPath) -Raw|ConvertFrom-Json -AsHashtable
try{
    `$out=@(& $(ConvertTo-QueueLiteral $script) @p)
    `$last=@(`$out|Where-Object{`$_ -isnot [string]})|Select-Object -Last 1
    [IO.File]::WriteAllText($(ConvertTo-QueueLiteral $outPath),(ConvertTo-Json -InputObject `$last -Depth 12))
    if(`$last){`$last|Format-List|Out-String -Width 220|Write-Host}
}catch{
    [IO.File]::WriteAllText($(ConvertTo-QueueLiteral $outPath),(ConvertTo-Json -InputObject @{Error=`$_.Exception.Message}))
    Write-Host ('Run failed: '+`$_.Exception.Message)
    exit 1
}
"@
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($wrapper))
    $log=Join-Path $root "logs\$Id.log"
    Add-Content -LiteralPath $log -Value "=== queue job $Id ($($Job['owner'])) $([DateTime]::UtcNow.ToString('o')): $script $(@($scenarios) -join ' ')"
    $info=[Diagnostics.ProcessStartInfo]::new((Join-Path $env:SystemRoot 'System32\cmd.exe'))
    # cmd /s strips the outer quotes; <NUL gives the run an empty stdin, >> keeps every line in the job log.
    $info.Arguments="/d /s /c `"`"$pwsh`" -NoProfile -NonInteractive -EncodedCommand $encoded <NUL >>`"$log`" 2>&1`""
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WorkingDirectory=[string]$Job['repo']
    $null=$info.Environment.Remove('MGS_GAME_LUA_DIR')
    $process=[Diagnostics.Process]::Start($info)
    try{$process.WaitForExit();return $process.ExitCode}finally{$process.Dispose()}
}
# The finished run -> result fields: run id and folder, status and the parts (one synthesized part for a runner that
# wrote no Parts).
function Read-QueueRunResult($Result,[string]$Id,$Job){
    $outPath=Join-Path $root "running\$Id.out"
    $out=if(Test-Path -LiteralPath $outPath){try{Read-WorkerQueueJson $outPath}catch{$null}}else{$null}
    $runDir=if($out -is [Collections.IDictionary] -and $out.Contains('RunDirectory')){[string]$out['RunDirectory']}else{$null}
    $scenarioPath=if($runDir){Join-Path $runDir 'scenario.json'}else{$null}
    if(!$scenarioPath -or !(Test-Path -LiteralPath $scenarioPath -PathType Leaf)){
        $Result['status']='error'
        $Result['failure']=if($out -is [Collections.IDictionary] -and $out.Contains('Error')){[string]$out['Error']}else{"no scenario.json (exit code $($Result['exit_code'])); see the log"}
        return
    }
    $sc=Read-WorkerQueueJson $scenarioPath
    $Result['run_dir']=$runDir
    $session=try{Read-WorkerQueueJson (Join-Path $runDir 'session.json')}catch{$null}
    $Result['run_id']=if($session -and $session['run_id']){[string]$session['run_id']}else{Split-Path -Leaf $runDir}
    foreach($pair in @(@('closed','Closed'),@('baseline_restored','BaselineRestored'),@('real_saves_unchanged','RealSavesUnchanged'),@('forced','Forced'))){
        $Result[$pair[0]]=if($sc.Contains($pair[1])){$sc[$pair[1]]}else{$null}
    }
    $Result['failure']=$sc['Failure']
    $Result['expect_failures']=@($sc['ExpectFailures']|Where-Object{$_})
    $parts=if($sc.Contains('Parts')){@($sc['Parts'])}else{
        @([ordered]@{index=1;name=[IO.Path]::GetFileName([string]@($Job['scenarios'])[0]);path=[string]@($Job['scenarios'])[0];
            status=$(if($sc['Failure'] -or $Result['expect_failures'].Count){'failed'}else{'passed'});failure=$sc['Failure'];fatal=$false;
            expect_failures=$Result['expect_failures']})
    }
    $Result['parts']=@($parts|ForEach-Object{[ordered]@{index=$_['index'];name=$_['name'];path=$_['path'];status=$_['status'];
        failure=$_['failure'];fatal=[bool]$_['fatal'];expect_failures=@($_['expect_failures']|Where-Object{$_})}})
    $clean=$sc['Closed'] -eq $true -and $sc['BaselineRestored'] -eq $true -and $sc['RealSavesUnchanged'] -eq $true
    $Result['status']=if(!$sc['Failure'] -and !$Result['expect_failures'].Count -and $clean -and
        !@($Result['parts']|Where-Object{$_['status'] -cne 'passed'}).Count){'passed'}else{'failed'}
}
function Invoke-QueueJob([IO.FileInfo]$File){
    $id=$File.BaseName
    $runningPath=Join-Path $root "running\$id.json"
    try{[IO.File]::Move($File.FullName,$runningPath)}catch{return}
    $job=try{Read-WorkerQueueJson $runningPath}catch{$null}
    $result=New-QueueResult $id $job
    $lockOwner=$null
    try{
        try{$runnerParameters=Assert-QueueJob $id $job}catch{$result['status']='refused';throw}
        $job['runner_pid']=$PID;$job['runner_start']=$myStart;$job['started_utc']=$result['started_utc']
        Write-QueueLog "Job $id ($($job['owner'])): $(@($job['scenarios']).Count) scenario(s) from $($job['repo'])"
        # 2. The floors, once; while short, wait and look again.
        $since=Get-Date;$said=''
        while($true){
            $short=@(Get-QueueFloorShortfall (Get-QueueResources) $config)
            if(!$short.Count){break}
            $reason=$short -join '; '
            Save-QueueJobState $runningPath $job 'waiting_floors' $reason
            if($reason -cne $said){Write-QueueLog "Job ${id} waits for the floors: $reason";$said=$reason}
            Start-Sleep -Seconds $config.floor_retry_sec
        }
        $result['floor_wait_s']=[Math]::Round(((Get-Date)-$since).TotalSeconds,1)
        # 3. The verify lock, then the running CTest suites.
        $since=Get-Date;$said=''
        $owner="queue pid=$PID start=$myStart time=$([DateTime]::UtcNow.ToString('o')) job=$id owner=$($job['owner']) repo=$($job['repo'])"
        while(!(Enter-QueueVerifyLock $owner)){
            $holder=Read-WorkerQueueText (Join-Path (Get-VerifyLockPath) 'owner.txt')
            $reason="verify lock held: $(if($holder){$holder.Trim()}else{'(no owner.txt yet)'})"
            if($holder -and $holder -cmatch '^queue ' -and !(Test-WorkerQueueOwnerAlive $holder)){$reason+=' (its queue runner is gone: remove the lock by hand once nothing runs)'}
            Save-QueueJobState $runningPath $job 'waiting_lock' $reason
            if($reason -cne $said){Write-QueueLog "Job ${id} waits: $reason";$said=$reason}
            Start-Sleep -Seconds $config.lock_retry_sec
        }
        $lockOwner=$owner
        $result['lock_wait_s']=[Math]::Round(((Get-Date)-$since).TotalSeconds,1)
        Save-QueueJobState $runningPath $job 'waiting_ctest' 'verify lock taken; waiting for running CTest suites'
        Wait-CtestReaders -TimeoutMinutes 1440
        # 4. The run.
        Save-QueueJobState $runningPath $job 'running' "Invoke-WorkerScenario in $($job['repo'])"
        $result['exit_code']=Invoke-QueueRun $id $job $runnerParameters
        Read-QueueRunResult $result $id $job
    }catch{
        if(!$result['status']){$result['status']='error'}
        $result['failure']=$_.Exception.Message
    }finally{
        # 5. Only our own verify lock is released.
        if($lockOwner){$null=Exit-QueueVerifyLock $lockOwner}
        Complete-QueueJob $id $runningPath $job $result
    }
}
# Jobs left in running\ by a runner that is gone: closed as interrupted (their session, if any, needs a manual look).
function Close-InterruptedQueueJobs{
    foreach($file in @(Get-QueueJobFiles 'running')){
        $job=try{Read-WorkerQueueJson $file.FullName}catch{$null}
        $owner=if($job -is [Collections.IDictionary]){"pid=$($job['runner_pid']) start=$($job['runner_start'])"}else{''}
        if($owner -and (Test-WorkerQueueOwnerAlive $owner)){continue}
        $result=New-QueueResult $file.BaseName $job
        $result['status']='interrupted'
        $result['failure']="its queue runner ($owner) ended while the job was $(if($job -is [Collections.IDictionary]){$job['phase']}else{'unreadable'}); check build\worker-runtime of its repo for an open session"
        Complete-QueueJob $file.BaseName $file.FullName $null $result
    }
}

# ---- Main: run until the queue is empty (look once more after letting go, so a job submitted meanwhile is not left) --
while($true){
    if(!(Enter-QueueRunnerLock)){
        Write-Host "A queue runner is already active: $(Get-WorkerQueueRunner $root)"
        exit 0
    }
    try{
        Write-QueueLog "Queue runner pid $PID ($repo) started on $root"
        Close-InterruptedQueueJobs
        while($true){
            $next=@(Get-QueueJobFiles 'jobs')|Select-Object -First 1
            if(!$next){break}
            Invoke-QueueJob $next
        }
    }finally{Exit-QueueRunnerLock}
    if(!@(Get-QueueJobFiles 'jobs').Count){break}
}
Write-QueueLog "Queue runner pid ${PID}: the queue is empty."
