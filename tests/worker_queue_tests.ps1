#Requires -Version 7
# The batched hidden run and the hidden-run queue, without any game:
#  - Invoke-WorkerScenario with several scenario parts in one session: a staged copy of the real runner with fake
#    Start-/Stop-WorkerSession and Send-WorkerCommand (they write files in the stage only), so the real step loop,
#    validation, labsetting union, per-part results and the session-fatal stop are exercised end to end; a single
#    scenario's scenario.json must equal the golden one the runner wrote before parts existed;
#  - the queue (Submit-WorkerJob.ps1, Invoke-WorkerQueue.ps1) in a sandbox: the runner child processes get a private
#    %TEMP%, so the verify lock, the CTest reader folder and the queue are the sandbox's own; the jobs run a fake job
#    repo's Invoke-WorkerScenario.ps1. Never touches the real %TEMP%\mgs-coop-verify.lock, a game folder or a save.
# -WriteGolden (maintenance only): rewrites tests/fixtures/worker_scenario_single_golden.json from the current runner.
param([string]$RepoRoot=(Split-Path -Parent $PSScriptRoot),[switch]$WriteGolden)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$script:checks=0
function Check([bool]$Value,[string]$Label){$script:checks++;if(!$Value){throw "FAIL $Label"}}
$pwsh=(Get-Process -Id $PID).Path
$sandbox=Join-Path ([IO.Path]::GetTempPath()) ('mgs-coop-queue-tests-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
$null=New-Item -ItemType Directory -Path $sandbox
$golden=Join-Path $RepoRoot 'tests\fixtures\worker_scenario_single_golden.json'
$runId='0123456789abcdef0123456789abcdef'

# ---- Fakes of the session scripts (stage only) -------------------------------------------------------------------
$fakeStart=@'
# Fake Start-WorkerSession: a ready session in the stage. Records its arguments and that it started.
$stage=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runId='0123456789abcdef0123456789abcdef'
$run=Join-Path $stage "build\worker-runtime\$runId"
$null=New-Item -ItemType Directory -Force -Path $run
Add-Content -LiteralPath (Join-Path $stage 'fake-starts.txt') -Value 'start'
$named=[ordered]@{};$name=$null
foreach($a in $args){if($a -is [string] -and $a -cmatch '^-([A-Za-z]+):?$'){$name=$Matches[1];$named[$name]=$true}elseif($name){$named[$name]=$a;$name=$null}}
[IO.File]::WriteAllText((Join-Path $stage 'fake-start-args.json'),($named|ConvertTo-Json -Depth 5))
$roles=[ordered]@{}
foreach($role in 'host','guest'){
    $mod=Join-Path $stage "games\$role\mgs-coop";$null=New-Item -ItemType Directory -Force -Path $mod
    $log=Join-Path $mod 'script-observer.log'
    if(!(Test-Path -LiteralPath $log)){[IO.File]::WriteAllText($log,"before the session`n")}
    $offset=(Get-Item -LiteralPath $log).Length
    $lines=[Environment]::GetEnvironmentVariable('FAKE_LOG_'+$role.ToUpperInvariant())
    if($lines){[IO.File]::AppendAllText($log,($lines -replace '\|',"`n")+"`n")}
    $roles[$role]=[ordered]@{game=(Join-Path $stage "games\$role");log_offsets=[ordered]@{'script-observer.log'=[ordered]@{path=$log;offset=$offset}}}
}
([ordered]@{run_id=$runId;status='ready';roles=$roles}|ConvertTo-Json -Depth 10)|Set-Content -LiteralPath (Join-Path $run 'session.json') -Encoding utf8NoBOM
$owned=@()
if($env:FAKE_LEASES){
    $me=Get-Process -Id $PID
    foreach($role in 'host','guest'){$owned+=[ordered]@{role=$role;pid=$PID;creation_filetime=[string]$me.StartTime.ToUniversalTime().ToFileTimeUtc();exe=$me.Path;admitted=$true}}
}
([ordered]@{run_id=$runId;stage='running';owned_processes=$owned}|ConvertTo-Json -Depth 10)|Set-Content -LiteralPath (Join-Path $run 'supervisor.json') -Encoding utf8NoBOM
Write-Host "fake session started: $run"
[pscustomobject]@{RunDirectory=$run;RunId=$runId}
'@
$fakeSend=@'
# Fake Send-WorkerCommand: records the command; 'press f' with FAKE_FATAL_KEY ends the fake supervisor (fatal).
param([string]$RunDirectory,[string]$Role,[string]$Operation,[string]$Keys='enter',[int]$DurationMs=100,[int]$Dx=0,[int]$Dy=0,[string]$DebugCommand)
$counter=Join-Path $RunDirectory 'fake-sequence.txt'
$sequence=1+$(if(Test-Path -LiteralPath $counter){[int](Get-Content -LiteralPath $counter -Raw)}else{0})
Set-Content -LiteralPath $counter -Value $sequence -NoNewline
Add-Content -LiteralPath (Join-Path $RunDirectory 'fake-commands.jsonl') -Value (([ordered]@{role=$Role;operation=$Operation;keys=$Keys})|ConvertTo-Json -Compress)
if($env:FAKE_FATAL_KEY -and $Operation -ceq 'press' -and $Keys -ceq $env:FAKE_FATAL_KEY){
    $path=Join-Path $RunDirectory 'supervisor.json'
    $s=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json -AsHashtable;$s.stage='stopped'
    ($s|ConvertTo-Json -Depth 10)|Set-Content -LiteralPath $path -Encoding utf8NoBOM
    throw 'Worker has no fresh, admitted private-desktop supervisor.'
}
[pscustomobject]@{sequence=$sequence;expires_tick_ms=[Environment]::TickCount64+2000}
'@
$fakeStop=@'
# Fake Stop-WorkerSession: records the stop.
param([string]$RunDirectory)
$stage=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Add-Content -LiteralPath (Join-Path $stage 'fake-stops.txt') -Value 'stop'
[pscustomobject]@{RunDirectory=$RunDirectory;Closed=$true;BaselineRestored=$true;RealSavesUnchanged=$true;Forced=$false;
    IsolationFailed=$false;SupervisorReason='shutdown';SupervisorError='';ExitedRoles=@()}
'@
function New-ScenarioStage([string]$Name){
    $stage=Join-Path $sandbox $Name
    $worker=Join-Path $stage 'tools\worker';$null=New-Item -ItemType Directory -Force -Path $worker,(Join-Path $stage 'tools\lab'),(Join-Path $stage 'scen')
    foreach($file in 'Invoke-WorkerScenario.ps1','WorkerSessionCommon.psm1','NativeActivationProfile.psm1'){
        Copy-Item -LiteralPath (Join-Path $RepoRoot "tools\worker\$file") -Destination $worker
    }
    Copy-Item -LiteralPath (Join-Path $RepoRoot 'tools\lab\WorldSync.psm1') -Destination (Join-Path $stage 'tools\lab')
    [IO.File]::WriteAllText((Join-Path $worker 'Start-WorkerSession.ps1'),$fakeStart)
    [IO.File]::WriteAllText((Join-Path $worker 'Send-WorkerCommand.ps1'),$fakeSend)
    [IO.File]::WriteAllText((Join-Path $worker 'Stop-WorkerSession.ps1'),$fakeStop)
    [IO.File]::WriteAllText((Join-Path $stage 'receipt.json'),'{"status":"passed"}')
    return $stage
}
function Write-Scenario([string]$Stage,[string]$Name,[string[]]$Lines){
    $path=Join-Path $Stage "scen\$Name";[IO.File]::WriteAllText($path,(($Lines -join "`r`n")+"`r`n"));return $path
}
# Runs the staged runner in its own pwsh (the real script's Set-StrictMode, modules and exit stay there).
function Invoke-Staged([string]$Stage,[hashtable]$Parameters,[hashtable]$Environment=@{}){
    $Parameters.CheckReceipt=Join-Path $Stage 'receipt.json';$Parameters.BaselineLoaderSha256='A'*64
    $paramFile=Join-Path $Stage ('params-'+[Guid]::NewGuid().ToString('N').Substring(0,6)+'.json')
    [IO.File]::WriteAllText($paramFile,($Parameters|ConvertTo-Json -Depth 5))
    $script=Join-Path $Stage 'tools\worker\Invoke-WorkerScenario.ps1'
    $text="`$ErrorActionPreference='Stop';`$p=Get-Content -LiteralPath '$paramFile' -Raw|ConvertFrom-Json -AsHashtable;`$null=& '$script' @p"
    $saved=@{}
    foreach($k in @('FAKE_LEASES','FAKE_FATAL_KEY','FAKE_LOG_HOST','FAKE_LOG_GUEST')){$saved[$k]=[Environment]::GetEnvironmentVariable($k);[Environment]::SetEnvironmentVariable($k,$Environment[$k])}
    try{
        $output=@(& $pwsh -NoProfile -NonInteractive -EncodedCommand ([Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($text))) 2>&1|ForEach-Object{[string]$_})
        $code=$LASTEXITCODE
    }finally{foreach($k in $saved.Keys){[Environment]::SetEnvironmentVariable($k,$saved[$k])}}
    $run=Join-Path $Stage "build\worker-runtime\$runId"
    $scenario=Join-Path $run 'scenario.json'
    return [pscustomobject]@{Code=$code;Output=($output -join "`n");Run=$run;
        Json=$(if(Test-Path -LiteralPath $scenario){[IO.File]::ReadAllText($scenario)}else{$null});
        Starts=$(if(Test-Path -LiteralPath (Join-Path $Stage 'fake-starts.txt')){@(Get-Content -LiteralPath (Join-Path $Stage 'fake-starts.txt')).Count}else{0});
        Stops=$(if(Test-Path -LiteralPath (Join-Path $Stage 'fake-stops.txt')){@(Get-Content -LiteralPath (Join-Path $Stage 'fake-stops.txt')).Count}else{0});
        StartArgs=$(if(Test-Path -LiteralPath (Join-Path $Stage 'fake-start-args.json')){Get-Content -LiteralPath (Join-Path $Stage 'fake-start-args.json') -Raw|ConvertFrom-Json -AsHashtable}else{$null})}
}
function Get-NormalizedScenarioJson($Result){
    return $Result.Json.Replace(($Result.Run|ConvertTo-Json).Trim('"'),'<RUN>')
}

try {
# ---- 1. One scenario: unchanged ------------------------------------------------------------------------------------
$singleLines=@('# the golden single scenario','labsetting nametag=1','state','waitlog host ^alpha 2','waitlog guest ^absent 1',
    'host press enter','host expect ^nomatch','logmark both','host require ^y','wait 10')
$stage=New-ScenarioStage 'single'
$path=Write-Scenario $stage 'single.txt' $singleLines
$r=Invoke-Staged $stage @{ScenarioPath=$path} @{FAKE_LOG_HOST='alpha one|other';FAKE_LOG_GUEST='beta one'}
Check ($r.Code -eq 0 -and $r.Json) "single scenario ran ($($r.Code)): $($r.Output)"
if($WriteGolden){[IO.File]::WriteAllText($golden,(Get-NormalizedScenarioJson $r));Write-Host "golden written: $golden"}
Check ((Get-NormalizedScenarioJson $r).Replace("`r`n","`n") -ceq [IO.File]::ReadAllText($golden).Replace("`r`n","`n")) 'a single scenario writes the golden scenario.json byte for byte'
Check ($r.Starts -eq 1 -and $r.Stops -eq 1) 'a single scenario starts and stops one session'
Check ((@($r.StartArgs['LabSetting']) -join ',') -ceq 'nametag=1') 'a single scenario passes its labsetting lines as before'
Check (!$r.Output.Contains('=== part')) 'a single scenario prints no part markers'
# The same through -Step lines (the old -ScenarioPath + -Step form): one part, the old result keys.
$stage=New-ScenarioStage 'steps'
$r=Invoke-Staged $stage @{Step=@('wait 0','state')}
$doc=$r.Json|ConvertFrom-Json -AsHashtable
Check ($r.Code -eq 0 -and !$doc.Contains('Parts') -and $null -eq $doc.Failure) 'a -Step scenario keeps the old result keys'

# ---- 2. Several parts in one session ---------------------------------------------------------------------------------
$tokens=$null;$errors=$null
$runnerAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $RepoRoot 'tools\worker\Invoke-WorkerScenario.ps1'),[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'the runner parses'
$merge=@($runnerAst.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Merge-ScenarioLabSettings'},$true))
Check ($merge.Count -eq 1) 'one labsetting union helper'
. ([scriptblock]::Create($merge[0].Extent.Text))
$u=@(Merge-ScenarioLabSettings @(@{Source='call';Settings=@('guest:coop_hud=off')},@{Source='a.txt';Settings=@('nametag=1')},
    @{Source='b.txt';Settings=@('host:coop_hud=full','nametag=1','host:nametag=1')}))
Check (($u -join ',') -ceq 'guest:coop_hud=off,nametag=1,host:coop_hud=full,host:nametag=1') 'the union keeps the first of equal entries in order'
$threw=$null;try{$null=Merge-ScenarioLabSettings @(@{Source='a.txt';Settings=@('nametag=1')},@{Source='n0.txt';Settings=@('guest:nametag=0')})}catch{$threw=$_.Exception.Message}
Check ($threw -and $threw.Contains('guest') -and $threw.Contains('nametag') -and $threw.Contains('a.txt') -and $threw.Contains('n0.txt')) "a conflicting value for one role is refused clearly ($threw)"
$threw=$null;try{$null=Merge-ScenarioLabSettings @(@{Source='a';Settings=@('lab_boot=30020')},@{Source='b';Settings=@('lab_boot=30010')})}catch{$threw=$_.Exception.Message}
Check ([bool]$threw) 'two boot spots in one session are refused'

$stage=New-ScenarioStage 'multi'
$a=Write-Scenario $stage 'a.txt' @('# part a: one expectation fails','labsetting nametag=1','waitlog host ^alpha 2','host press enter','host expect ^nomatch','state')
$b=Write-Scenario $stage 'b.txt' @('labsetting host coop_hud=full','labsetting nametag=1','waitlog guest ^beta 2','logmark both','host require ^x','wait 10')
$c=Write-Scenario $stage 'c.txt' @('waitlog host ^alpha 1','wait 0')
$r=Invoke-Staged $stage @{ScenarioPath=@($a,$b,$c);LabSetting=@('guest:coop_hud=off')} @{FAKE_LEASES='1';FAKE_LOG_HOST='alpha one';FAKE_LOG_GUEST='beta one'}
Check ($r.Code -eq 0 -and $r.Json) "three parts ran ($($r.Code)): $($r.Output)"
$doc=$r.Json|ConvertFrom-Json -AsHashtable
Check ($r.Starts -eq 1 -and $r.Stops -eq 1) 'three parts share one session'
Check ((@($r.StartArgs['LabSetting']) -join ',') -ceq 'guest:coop_hud=off,nametag=1,host:coop_hud=full') 'the session gets the union of the parts'' labsettings'
$parts=@($doc.Parts)
Check ($parts.Count -eq 3 -and (@($parts|ForEach-Object{$_.name}) -join ',') -ceq 'a.txt,b.txt,c.txt') 'scenario.json lists the parts in order'
Check ((@($parts|ForEach-Object{$_.status}) -join ',') -ceq 'failed,failed,passed') 'each part has its own pass/fail'
Check ($null -eq $parts[0].failure -and (@($parts[0].expect_failures) -join '|') -ceq 'host expect ^nomatch <- ') 'part a: its expectation failure only'
Check ($parts[1].failure -ceq 'Required scenario check failed: host require ^x <- ' -and !$parts[1].fatal) 'part b: a failed require ends that part only'
Check ((@($parts[1].expect_failures) -join '|') -ceq 'host require ^x <- ') 'part b: its own expectation failures'
Check ($null -eq $parts[2].failure -and (@($parts[2].expect_failures)).Count -eq 0 -and (@($parts[2].expectations) -join '|') -ceq 'host waitlog ^alpha <- alpha one') 'part c runs after a failed part, with its own log marks'
Check ((@($doc.ExpectFailures) -join '|') -ceq 'host expect ^nomatch <- |host require ^x <- ') 'the top-level lists hold every part'
Check ($doc.Failure -and ([string]$doc.Failure).Contains('b.txt') -and ([string]$doc.Failure).Contains('host require ^x')) "the top-level failure names the failed part ($($doc.Failure))"
Check ((@($doc.Timings|ForEach-Object{[string]$_.part}) -join ',') -ceq '1,1,1,1,2,2,2,3,3') 'each timing row names its part; part b stops at its require'
foreach($p in $parts){
    Check ($p.start_utc -and $p.end_utc -and $p.path -and $p.steps -ge 2) ('part '+$p.name+' marks its start and end')
    foreach($role in 'host','guest'){Check ($p.log_offsets[$role].start -is [long] -and $p.log_offsets[$role].end -ge $p.log_offsets[$role].start) ('part '+$p.name+' marks the '+$role+' log')}
}
Check ($r.Output.Contains('=== part 1/3 start: a.txt') -and $r.Output.Contains('=== part 2/3 end: b.txt failed') -and $r.Output.Contains('=== part 3/3 end: c.txt passed')) 'the console marks each part'

# A session-fatal error stops the batch; the parts after it are skipped.
$stage=New-ScenarioStage 'fatal'
$x=Write-Scenario $stage 'x.txt' @('wait 0','host press f','wait 0')
$y=Write-Scenario $stage 'y.txt' @('wait 0')
$r=Invoke-Staged $stage @{ScenarioPath=@($x,$y)} @{FAKE_LEASES='1';FAKE_FATAL_KEY='f'}
$doc=$r.Json|ConvertFrom-Json -AsHashtable
Check ($r.Starts -eq 1 -and $r.Stops -eq 1) 'a fatal batch still stops its one session'
Check ((@($doc.Parts|ForEach-Object{$_.status}) -join ',') -ceq 'failed,skipped' -and $doc.Parts[0].fatal) 'a fatal session error skips the remaining parts'
Check (([string]$doc.Failure).Contains('private-desktop supervisor')) 'the fatal error is the top-level failure'

# Refusals before any session starts.
$stage=New-ScenarioStage 'refuse'
$a=Write-Scenario $stage 'a.txt' @('labsetting nametag=1','wait 0')
$n0=Write-Scenario $stage 'n0.txt' @('labsetting nametag=0','wait 0')
$c=Write-Scenario $stage 'c.txt' @('wait 0')
$r=Invoke-Staged $stage @{ScenarioPath=@($a,$n0)}
Check ($r.Code -ne 0 -and $r.Starts -eq 0 -and $r.Output.Contains('nametag')) 'parts with conflicting labsettings are refused before the session'
$r=Invoke-Staged $stage @{ScenarioPath=@($a,$c);LabSetting=@('host:nametag=0')}
Check ($r.Code -ne 0 -and $r.Starts -eq 0) 'a call -LabSetting that conflicts with a part is refused'
$r=Invoke-Staged $stage @{ScenarioPath=@($a,$c);Step=@('wait 0')}
Check ($r.Code -ne 0 -and $r.Starts -eq 0) '-Step is refused with several parts'
$bad=Write-Scenario $stage 'bad.txt' @('wait 0','host fly')
$r=Invoke-Staged $stage @{ScenarioPath=@($a,$bad)}
Check ($r.Code -ne 0 -and $r.Starts -eq 0 -and $r.Output.Contains('host fly')) 'every part is validated before the session'

# The batch file (-ScenarioList: one path per line, relative to the repo) and the comma form of -ScenarioPath.
$stage=New-ScenarioStage 'list'
$a=Write-Scenario $stage 'a.txt' @('wait 0')
$c=Write-Scenario $stage 'c.txt' @('state')
$list=Join-Path $stage 'batch.txt'
[IO.File]::WriteAllText($list,"# a batch`r`n$a`r`n`r`nscen\c.txt`r`n")
$r=Invoke-Staged $stage @{ScenarioList=$list} @{FAKE_LEASES='1'}
$doc=$r.Json|ConvertFrom-Json -AsHashtable
Check ($r.Starts -eq 1 -and (@($doc.Parts|ForEach-Object{$_.name}) -join ',') -ceq 'a.txt,c.txt' -and (@($doc.Parts|ForEach-Object{$_.status}) -join ',') -ceq 'passed,passed' -and $null -eq $doc.Failure) 'a batch file runs its scenarios in one session'
Remove-Item -LiteralPath (Join-Path $stage 'fake-starts.txt')
$r=Invoke-Staged $stage @{ScenarioPath="$a,$c"} @{FAKE_LEASES='1'}
$doc=$r.Json|ConvertFrom-Json -AsHashtable
Check ($r.Starts -eq 1 -and @($doc.Parts).Count -eq 2) 'pwsh -File form: -ScenarioPath a.txt,b.txt'

# ---- 3. The hidden-run queue ---------------------------------------------------------------------------------------
$submit=Join-Path $RepoRoot 'tools\worker\Submit-WorkerJob.ps1'
$runner=Join-Path $RepoRoot 'tools\worker\Invoke-WorkerQueue.ps1'
Check ((Test-Path -LiteralPath $submit) -and (Test-Path -LiteralPath $runner)) 'the queue scripts exist'
$tokens=$null;$errors=$null
$queueAst=[Management.Automation.Language.Parser]::ParseFile($runner,[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'the queue runner parses'
$floorFn=@($queueAst.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Get-QueueFloorShortfall'},$true))
Check ($floorFn.Count -eq 1) 'one floor check'
. ([scriptblock]::Create($floorFn[0].Extent.Text))
$floors=@{min_free_c_gb=6;min_free_d_gb=3;min_commit_free_gb=15}
Check (@(Get-QueueFloorShortfall @{c_gb=29;d_gb=9.7;commit_gb=20} $floors).Count -eq 0) 'floors met: nothing short'
$short=@(Get-QueueFloorShortfall @{c_gb=5.9;d_gb=2;commit_gb=14.9} $floors)
Check ($short.Count -eq 3 -and ($short -join ';').Contains('C:') -and ($short -join ';').Contains('D:') -and ($short -join ';').Contains('commit')) "every short floor is named ($($short -join '; '))"
Check (@(Get-QueueFloorShortfall @{c_gb=29;d_gb=$null;commit_gb=20} $floors).Count -eq 0) 'a machine without D: has no D: floor'

# The sandbox: its own %TEMP% (verify lock, CTest readers, queue) and a fake job repo.
$temp=Join-Path $sandbox 'temp';$null=New-Item -ItemType Directory -Path $temp
$queue=Join-Path $temp 'mgs-coop-queue'
$lockDir=Join-Path $temp 'mgs-coop-verify.lock'
$jobRepo=Join-Path $sandbox 'jobrepo';$null=New-Item -ItemType Directory -Force -Path (Join-Path $jobRepo 'tools\worker\scenarios')
$fakeScenario=@'
# Fake Invoke-WorkerScenario of a job repo: records what the queue gave it and writes a run like the real one.
[CmdletBinding()]
param([string[]]$ScenarioPath=@(),[string]$ScenarioList,[string[]]$Step=@(),[string]$Mode='lab',[string]$WanSim='off',[string]$PartnerProfile='animated',
    [string]$NativeStage='',[string]$BuildProfile='normal',[string]$SaveSeed='lab',[string]$HostSaveSeed='',[string]$GuestSaveSeed='',
    [string]$GraphicsQuality='',[string[]]$LabSetting=@(),[switch]$FreshChecks,[int]$TimeoutSec=600)
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$files=if($ScenarioList){@([IO.File]::ReadAllLines($ScenarioList)|Where-Object{$_ -and !$_.StartsWith('#')})}else{@($ScenarioPath)}
$owner=$(try{[IO.File]::ReadAllText((Join-Path $env:TEMP 'mgs-coop-verify.lock\owner.txt'))}catch{''})
$stdin=$(if([Console]::IsInputRedirected){[Console]::In.ReadToEnd()}else{'console'})
$seen=[ordered]@{files=$files;list=[bool]$ScenarioList;mode=$Mode;wan=$WanSim;profile=$PartnerProfile;stage=$NativeStage;build=$BuildProfile;
    seed=$SaveSeed;labsetting=$LabSetting;fresh=[bool]$FreshChecks;lock_owner=$owner;stdin=$stdin;lua=[string]$env:MGS_GAME_LUA_DIR;cwd=(Get-Location).Path}
Add-Content -LiteralPath (Join-Path $repo 'fake-runs.jsonl') -Value ($seen|ConvertTo-Json -Compress -Depth 5)
if(@($files|Where-Object{(Get-Content -LiteralPath $_ -Raw).Contains('THROW')}).Count){throw 'Worker checks did not pass: fake'}
if(@($files|Where-Object{(Get-Content -LiteralPath $_ -Raw).Contains('SLOW')}).Count){Start-Sleep -Seconds 4}
$runId=[Guid]::NewGuid().ToString('N')
$run=Join-Path $repo "build\worker-runtime\$runId";$null=New-Item -ItemType Directory -Force -Path $run
([ordered]@{run_id=$runId;status='closed'}|ConvertTo-Json)|Set-Content -LiteralPath (Join-Path $run 'session.json')
$i=0
$parts=@(foreach($f in $files){$i++;$bad=(Get-Content -LiteralPath $f -Raw).Contains('FAIL')
    [ordered]@{index=$i;name=[IO.Path]::GetFileName($f);path=$f;status=$(if($bad){'failed'}else{'passed'});failure=$null;fatal=$false;
        expectations=@('host expect x <- y');expect_failures=@($(if($bad){'host expect ^ok <- bad'}))}})
$xf=@($parts|ForEach-Object{$_.expect_failures})
$result=[ordered]@{RunDirectory=$run;Frames=@();Failure=$null;Expectations=@();ExpectFailures=$xf;Closed=$true;BaselineRestored=$true;RealSavesUnchanged=$true;Forced=$false}
if($ScenarioList -or $files.Count -gt 1){$result['Parts']=$parts}
($result|ConvertTo-Json -Depth 8)|Set-Content -LiteralPath (Join-Path $run 'scenario.json')
Write-Host "fake run $runId"
[pscustomobject]$result
'@
[IO.File]::WriteAllText((Join-Path $jobRepo 'tools\worker\Invoke-WorkerScenario.ps1'),$fakeScenario)
function Write-JobScenario([string]$Name,[string]$Text='wait 0'){$p=Join-Path $jobRepo "tools\worker\scenarios\$Name";[IO.File]::WriteAllText($p,$Text+"`r`n");return $p}
$pass1=Write-JobScenario 'one.txt';$pass2=Write-JobScenario 'two.txt';$failing=Write-JobScenario 'bad.txt' "# FAIL`r`nwait 0"
$throwing=Write-JobScenario 'throw.txt' "# THROW`r`nwait 0";$slow=Write-JobScenario 'slow.txt' "# SLOW`r`nwait 0"
$resources=Join-Path $sandbox 'resources.json'
function Set-Resources([double]$Commit){[IO.File]::WriteAllText($resources,(@{c_gb=50;d_gb=50;commit_gb=$Commit}|ConvertTo-Json))}
Set-Resources 40
# A fast queue for the tests (the real one checks every 3 min and polls the verify lock every 30 s).
function Write-QueueConfig{
    $null=New-Item -ItemType Directory -Force -Path $queue
    [IO.File]::WriteAllText((Join-Path $queue 'queue.json'),(@{floor_retry_sec=1;lock_retry_sec=1;resource_file=$resources}|ConvertTo-Json))
}
Write-QueueConfig
# Runs a queue script in its own pwsh with the sandbox's %TEMP% (and an MGS_GAME_LUA_DIR the jobs must not see).
function Invoke-Queue([string]$Script,[string[]]$Arguments,[switch]$Background){
    $saved=@{TEMP=$env:TEMP;TMP=$env:TMP;MGS_GAME_LUA_DIR=$env:MGS_GAME_LUA_DIR}
    $env:TEMP=$temp;$env:TMP=$temp;$env:MGS_GAME_LUA_DIR='C:\lua-dir-the-job-must-not-see'
    try{
        if($Background){
            return Start-Process -FilePath $pwsh -ArgumentList (@('-NoProfile','-NonInteractive','-File',$Script)+$Arguments) -PassThru -WindowStyle Hidden
        }
        $out=@(& $pwsh -NoProfile -NonInteractive -File $Script @Arguments 2>&1|ForEach-Object{[string]$_})
        return [pscustomobject]@{Code=$LASTEXITCODE;Output=($out -join "`n")}
    }finally{foreach($k in $saved.Keys){[Environment]::SetEnvironmentVariable($k,$saved[$k])}}
}
function Get-Jobs([string]$Folder,[string]$Filter='*.json'){
    $dir=Join-Path $queue $Folder
    if(!(Test-Path -LiteralPath $dir)){return @()}
    return @(Get-ChildItem -LiteralPath $dir -Filter $Filter -File|Sort-Object Name)
}
function Read-Result([string]$Id){return Get-Content -LiteralPath (Join-Path $queue "done\$Id.result.json") -Raw|ConvertFrom-Json -AsHashtable}
function Wait-Until([scriptblock]$Condition,[int]$Seconds,[string]$Label){
    $limit=(Get-Date).AddSeconds($Seconds)
    while(!(& $Condition)){if((Get-Date) -gt $limit){throw "FAIL timed out: $Label"};Start-Sleep -Milliseconds 200}
    Check $true $Label
}
function Get-JobId([string]$Output){$m=[regex]::Match($Output,'Job ([0-9]{8}-[0-9]{6}-[0-9]{3}-[0-9a-f]{6}) ');Check $m.Success "the job id is printed: $Output";return $m.Groups[1].Value}

# Submit writes a job and nothing else (-NoStart); the queue folders sit under %TEMP%.
$r=Invoke-Queue $submit @('-Owner','tester-1','-Repo',$jobRepo,'-ScenarioPath','tools\worker\scenarios\one.txt,tools\worker\scenarios\two.txt',
    '-Mode','play','-WanSim','bad','-PartnerProfile','native','-NativeStage','gun','-BuildProfile','research','-SaveSeed','afgh-ledge','-NoStart')
Check ($r.Code -eq 0) "submit -NoStart ($($r.Output))"
$id1=Get-JobId $r.Output
$jobs=Get-Jobs 'jobs'
Check ($jobs.Count -eq 1 -and $jobs[0].Name -ceq "$id1.json") 'the job waits in %TEMP%\mgs-coop-queue\jobs'
$job=Get-Content -LiteralPath $jobs[0].FullName -Raw|ConvertFrom-Json -AsHashtable
Check ($job.owner -ceq 'tester-1' -and $job.repo -ceq $jobRepo -and (@($job.scenarios) -join '|') -ceq "$pass1|$pass2") 'the job names its repo and its scenarios (relative to the repo)'
Check ($job.args.Mode -ceq 'play' -and $job.args.WanSim -ceq 'bad' -and $job.args.PartnerProfile -ceq 'native' -and $job.args.NativeStage -ceq 'gun' -and
    $job.args.BuildProfile -ceq 'research' -and $job.args.SaveSeed -ceq 'afgh-ledge') 'the job keeps the run flags'
$r=Invoke-Queue $submit @('-Owner','tester-2','-Repo',$jobRepo,'-ScenarioPath',$failing,'-LabSetting','nametag=1','-NoStart')
$id2=Get-JobId $r.Output
Check ((@(Get-Jobs 'jobs'|ForEach-Object{$_.BaseName}) -join ',') -ceq "$id1,$id2") 'jobs queue oldest first'
foreach($bad in @(@('-Owner','tester','-Repo',$jobRepo,'-ScenarioPath','tools\worker\scenarios\missing.txt','-NoStart'),
                  @('-Owner','bad owner','-Repo',$jobRepo,'-ScenarioPath',$pass1,'-NoStart'),
                  @('-Owner','tester','-Repo',$sandbox,'-ScenarioPath',$pass1,'-NoStart'))){
    $r=Invoke-Queue $submit $bad
    Check ($r.Code -ne 0) ('submit refuses: '+($bad -join ' '))
}
Check ((Get-Jobs 'jobs').Count -eq 2) 'a refused submit writes no job'

# The runner takes the jobs in order under the verify lock, then releases it.
$r=Invoke-Queue $runner @()
Check ($r.Code -eq 0) "the runner ran ($($r.Output))"
Check ((Get-Jobs 'jobs').Count -eq 0 -and (Get-Jobs 'running').Count -eq 0) 'the runner empties the queue'
Check (!(Test-Path -LiteralPath $lockDir) -and !(Test-Path -LiteralPath (Join-Path $queue 'runner.lock'))) 'the runner releases the verify lock and its own lock'
$runs=@(Get-Content -LiteralPath (Join-Path $jobRepo 'fake-runs.jsonl')|ConvertFrom-Json -AsHashtable)
Check ($runs.Count -eq 2 -and (@($runs[0].files) -join '|') -ceq "$pass1|$pass2" -and (@($runs[1].files) -join '|') -ceq $failing) 'jobs run oldest first, each in one call'
Check ($runs[0].list -and $runs[0].mode -ceq 'play' -and $runs[0].wan -ceq 'bad' -and $runs[0].profile -ceq 'native' -and $runs[0].stage -ceq 'gun' -and
    $runs[0].build -ceq 'research' -and $runs[0].seed -ceq 'afgh-ledge') 'the run gets the job''s scenario list and flags'
Check ((@($runs[1].labsetting) -join ',') -ceq 'nametag=1') 'the run gets the job''s labsettings'
Check ($runs[0].lock_owner.StartsWith('queue pid=') -and $runs[0].lock_owner.Contains("job=$id1") -and $runs[0].lock_owner.Contains('owner=tester-1')) "the run holds the verify lock as the queue ($($runs[0].lock_owner))"
Check ($runs[0].stdin -ceq '' -and $runs[0].lua -ceq '' -and $runs[0].cwd -ieq $jobRepo) 'the run reads stdin from NUL, without MGS_GAME_LUA_DIR, in the job''s repo'
$res1=Read-Result $id1;$res2=Read-Result $id2
Check ($res1.status -ceq 'passed' -and $res1.run_id -cmatch '^[0-9a-f]{32}$' -and $res1.run_dir -and (Test-Path -LiteralPath $res1.run_dir)) 'the result names the passed run'
Check ((@($res1.parts|ForEach-Object{$_.name+'='+$_.status}) -join ',') -ceq 'one.txt=passed,two.txt=passed') 'the result lists each part'
Check ($res2.status -ceq 'failed' -and (@($res2.parts|ForEach-Object{$_.status}) -join ',') -ceq 'failed' -and (@($res2.parts[0].expect_failures) -join '|') -ceq 'host expect ^ok <- bad') 'a failed expectation fails its part and the job'
Check ($res1.owner -ceq 'tester-1' -and $res1.log -and (Test-Path -LiteralPath $res1.log) -and $res1.started_utc -and $res1.finished_utc) 'the result keeps owner, log and times'
Check ((Get-Content -LiteralPath $res1.log -Raw).Contains('fake run')) 'the job log holds the run''s output'

# A job whose run fails before a session: an error result; the verify lock is still released.
$r=Invoke-Queue $submit @('-Owner','tester','-Repo',$jobRepo,'-ScenarioPath',$throwing,'-NoStart');$id=Get-JobId $r.Output
$null=Invoke-Queue $runner @()
$res=Read-Result $id
Check ($res.status -ceq 'error' -and ([string]$res.failure).Contains('Worker checks did not pass') -and !(Test-Path -LiteralPath $lockDir)) "a run that throws is an error result ($($res.failure))"

# The verify lock held by someone else: the job waits, then runs once it is free (never removes a foreign lock).
$null=New-Item -ItemType Directory -Path $lockDir;[IO.File]::WriteAllText((Join-Path $lockDir 'owner.txt'),'other-agent pid=1 start=1 time=2026-11-01T00:00:00Z')
$r=Invoke-Queue $submit @('-Owner','tester','-Repo',$jobRepo,'-ScenarioPath',$pass1,'-NoStart');$id=Get-JobId $r.Output
$p=Invoke-Queue $runner @() -Background
Wait-Until {$f=Join-Path $queue "running\$id.json";(Test-Path -LiteralPath $f) -and (Get-Content -LiteralPath $f -Raw).Contains('waiting_lock')} 20 'the job waits for a foreign verify lock'
Start-Sleep -Seconds 2
Check ((Get-Content -LiteralPath (Join-Path $lockDir 'owner.txt') -Raw).StartsWith('other-agent')) 'the queue leaves a foreign verify lock alone'
Remove-Item -LiteralPath $lockDir -Recurse -Force
Wait-Until {Test-Path -LiteralPath (Join-Path $queue "done\$id.result.json")} 30 'the job runs once the verify lock is free'
$p.WaitForExit(30000)|Out-Null
$res=Read-Result $id
Check ($res.status -ceq 'passed' -and [double]$res.lock_wait_s -ge 1) "the result records the lock wait ($($res.lock_wait_s) s)"

# The floors: the job waits (and says why) until C:, D: and the commit charge allow a run.
Set-Resources 4
$r=Invoke-Queue $submit @('-Owner','tester','-Repo',$jobRepo,'-ScenarioPath',$pass1,'-NoStart');$id=Get-JobId $r.Output
$p=Invoke-Queue $runner @() -Background
Wait-Until {$f=Join-Path $queue "running\$id.json";(Test-Path -LiteralPath $f) -and (Get-Content -LiteralPath $f -Raw).Contains('waiting_floors')} 20 'the job waits for the floors'
Check ((Get-Content -LiteralPath (Join-Path $queue "running\$id.json") -Raw).Contains('commit')) 'the waiting job names the short floor'
Check (!(Test-Path -LiteralPath $lockDir)) 'a job waiting for the floors does not hold the verify lock'
Set-Resources 40
Wait-Until {Test-Path -LiteralPath (Join-Path $queue "done\$id.result.json")} 30 'the job runs once the floors are met'
$p.WaitForExit(30000)|Out-Null
Check ([double](Read-Result $id).floor_wait_s -ge 1) 'the result records the floor wait'

# One runner at a time: a live runner's lock keeps a second runner out; a dead runner's lock is taken over.
$me=Get-Process -Id $PID
[IO.File]::WriteAllText((Join-Path $queue 'runner.lock'),"pid=$PID start=$($me.StartTime.ToUniversalTime().ToFileTimeUtc()) time=x")
$r=Invoke-Queue $submit @('-Owner','tester','-Repo',$jobRepo,'-ScenarioPath',$pass1,'-NoStart');$id=Get-JobId $r.Output
$r=Invoke-Queue $runner @()
Check ($r.Code -eq 0 -and (Get-Jobs 'jobs').Count -eq 1 -and $r.Output.Contains('already')) 'a second runner leaves the queue to the live one'
[IO.File]::WriteAllText((Join-Path $queue 'runner.lock'),"pid=$PID start=1 time=x")
$r=Invoke-Queue $runner @()
Check ((Get-Jobs 'jobs').Count -eq 0 -and (Read-Result $id).status -ceq 'passed' -and !(Test-Path -LiteralPath (Join-Path $queue 'runner.lock'))) 'a stale runner lock (pid gone or reused) is taken over'

# A job left in running by a runner that died is closed as interrupted (never rerun on its own).
$null=New-Item -ItemType Directory -Force -Path (Join-Path $queue 'running')
$ghost='20261101-000000-000-abcdef'
[IO.File]::WriteAllText((Join-Path $queue "running\$ghost.json"),(@{format='mgs-coop-worker-job';v=1;id=$ghost;owner='ghost';repo=$jobRepo;scenarios=@($pass1);args=@{};
    submitted_utc='2026-11-01T00:00:00Z';phase='running';runner_pid=$PID;runner_start='1'}|ConvertTo-Json))
$null=Invoke-Queue $runner @()
Check ((Read-Result $ghost).status -ceq 'interrupted' -and (Get-Jobs 'running').Count -eq 0) 'an orphaned running job is closed as interrupted'

# A job repo whose runner predates several parts: one scenario still runs (-ScenarioPath), several are refused.
$oldRepo=Join-Path $sandbox 'oldrepo';$null=New-Item -ItemType Directory -Force -Path (Join-Path $oldRepo 'tools\worker')
[IO.File]::WriteAllText((Join-Path $oldRepo 'tools\worker\Invoke-WorkerScenario.ps1'),$fakeScenario.Replace('[string[]]$ScenarioPath=@(),[string]$ScenarioList,','[string]$ScenarioPath,'))
$r=Invoke-Queue $submit @('-Owner','tester','-Repo',$oldRepo,'-ScenarioPath',"$pass1,$pass2",'-NoStart');$idOld2=Get-JobId $r.Output
$r=Invoke-Queue $submit @('-Owner','tester','-Repo',$oldRepo,'-ScenarioPath',$pass1,'-NoStart');$idOld1=Get-JobId $r.Output
$null=Invoke-Queue $runner @()
Check ((Read-Result $idOld2).status -ceq 'refused' -and ([string](Read-Result $idOld2).failure).Contains('one scenario')) 'several parts need a runner that takes them'
$old1=Read-Result $idOld1
Check ($old1.status -ceq 'passed' -and @($old1.parts).Count -eq 1 -and $old1.parts[0].name -ceq 'one.txt') 'one scenario runs through -ScenarioPath on an older runner'

# Submit -Wait starts a runner when none is alive and prints the result.
$r=Invoke-Queue $submit @('-Owner','tester-wait','-Repo',$jobRepo,'-ScenarioPath',"$pass1,$failing",'-Wait','-WaitTimeoutMin','2')
Check ($r.Code -eq 0 -and $r.Output.Contains('status: failed') -and $r.Output.Contains('one.txt: passed') -and $r.Output.Contains('bad.txt: failed')) "submit -Wait starts the runner and prints the parts ($($r.Output))"
Wait-Until {!(Test-Path -LiteralPath (Join-Path $queue 'runner.lock'))} 20 'the started runner exits once the queue is empty'
"worker_queue_tests: $script:checks checks passed"
} finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force -ErrorAction SilentlyContinue
}
