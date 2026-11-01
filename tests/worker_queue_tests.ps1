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
"worker_queue_tests: $script:checks checks passed"
} finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force -ErrorAction SilentlyContinue
}
