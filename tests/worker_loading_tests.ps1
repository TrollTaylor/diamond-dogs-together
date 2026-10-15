#Requires -Version 7
# Exact helper bodies with recorded lifecycle/clock/queue only. Never calls a
# worker controller, retail runtime, installer, process/window or input API.
param([string]$RepoRoot=(Split-Path -Parent $PSScriptRoot))
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop';$checks=0
function Check([bool]$v,[string]$label){$script:checks++;if(!$v){throw "FAIL $label"}}
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $RepoRoot 'tools/worker/Invoke-WorkerScenario.ps1'),[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'scenario parses'
foreach($name in 'Resume-TripLoading','Get-ScenarioWaitAction','Read-ContinueGenerations','Wait-ScenarioMission','Invoke-ScenarioWorldTravel'){
 $f=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true))
 Check ($f.Count -eq 1) ('actual helper '+$name);. ([scriptblock]::Create($f[0].Extent.Text))
}
$runId='0123456789abcdef0123456789abcdef';$loadingAttempts=@{};$continueRequirements=@{}
$c=[pscustomobject]@{now=[int64]10000;loading=@{};commands=[Collections.Generic.List[hashtable]]::new();readyAt=[int64]11000;case='pass';states=@{};advance=@{};reads=[Collections.Generic.List[string]]::new();runAfter=0}
function Get-ScenarioTick{return $c.now}
function Start-Sleep([int]$Milliseconds){$c.now+=$Milliseconds}
# Recorded metadata with a phase answers only that phase; runAfter commands later
# the recorded allocation enters its runtime phase (the tips pause was resumed).
function Read-RoleLoading([string]$Role,[string]$Phase='initialized'){
 $s=$c.loading[$Role]
 if($s -is [hashtable] -and $s.ContainsKey('phase')){
  if($c.runAfter -and $c.commands.Count -ge $c.runAfter){$s=$s.Clone();$s.phase='running'}
  if($s.phase -cne $Phase){return $null}
 }
 return $s
}
function Send-Step([string]$Role,[hashtable]$Arguments){
 $c.commands.Add(@{role=$Role;operation=$Arguments.Operation;tick=$c.now;args=$Arguments.Clone()})
 if($c.case -ceq 'refused'){throw 'native loading token changed'}
 return @{sequence=$c.commands.Count}
}
function Read-RoleRaw([string]$Role){
 $c.reads.Add($Role)
 if($c.states.ContainsKey($Role)){
  $s=$c.states[$Role].Clone()
  if($c.advance.ContainsKey($Role) -and $c.now -ge $c.advance[$Role].at){$s.generation=$c.advance[$Role].generation}
  if($s.ContainsKey('tick_ms') -and $s.tick_ms -ceq 'fresh'){$s.tick_ms=$c.now}
  return $s
 }
 return @{run_id=$runId;role=$Role;mission=$(if($c.now -ge $c.readyAt){30020}else{30050});generation=3;
  gameplay_ready=$true;terminating=$false;tick_ms=$(if($c.case -ceq 'fresh-wrong' -or $c.now -ge $c.readyAt){$c.now}else{1})}
}
$c.loading=@{host=@{generation=3};guest=@{generation=4}}
Wait-ScenarioMission 'host' 30020 5
Check ($c.commands.Count -eq 2) 'waitmission resumes both owned loading roles'
Check ($c.commands[0].role -ceq 'host' -and $c.commands[1].role -ceq 'guest') 'each role dispatches explicitly'
Check (@($c.commands|Where-Object{$_.operation -cne 'resume_loading' -or $_.args.ContainsKey('Keys')}).Count -eq 0) 'no generic Space press in waitmission'
$c.now=20000;$c.readyAt=20000;$c.commands.Clear();Wait-ScenarioMission 'guest' 30020 5
Check ($c.commands.Count -eq 0) 'fresh arrived gameplay never requests resume'
$c.now=30000;$c.readyAt=200000;$c.commands.Clear();$loadingAttempts=@{};$c.loading=@{host=@{generation=5};guest=@{generation=6}}
$threw=$false;try{Wait-ScenarioMission 'host' 30020 61}catch{$threw=$true}
Check $threw 'destination timeout remains failure'
foreach($role in 'host','guest'){
 $rows=@($c.commands|Where-Object{$_.role -ceq $role})
 Check ($rows.Count -eq 12) ('attempt budget12 '+$role)
 for($i=1;$i -lt $rows.Count;$i++){Check ($rows[$i].tick-$rows[$i-1].tick -ge 5000) ('retry cadence '+$role)}
}
$c.loading=@{};$c.commands.Clear();Resume-TripLoading
Check ($c.commands.Count -eq 0) 'runtime/unavailable metadata never dispatches input'
$c.loading=@{host=@{generation=7}};Resume-TripLoading
Check ($c.commands.Count -eq 1 -and $c.commands[0].role -ceq 'host') 'new allocation receives independent attempt budget'
$c.case='refused';$c.now+=5000;Resume-TripLoading
Check ($c.commands.Count -eq 2 -and $loadingAttempts['host:7'].count -eq 2) 'refusal recorded and consumes bounded attempt'
$c.case='fresh-wrong';$c.commands.Clear();$c.loading=@{host=@{generation=12};guest=@{generation=13}}
Resume-TripLoading
Check ($c.commands.Count -eq 0) 'fresh live wrong-mission telemetry blocks needless loading attempts too'
# Same-mission Continue must be causal to both allocations, not the old ready
# snapshot. These execute the actual helper bodies with recorded states only.
function New-RecordedState([string]$Role,[uint32]$Generation=8){
 return @{run_id=$runId;role=$Role;mission=30020;generation=$Generation;gameplay_ready=$false;terminating=$false;tick_ms='fresh'}
}
$c.now=100000;$c.loading=@{};$c.states=@{host=(New-RecordedState 'host' 8);guest=(New-RecordedState 'guest' 19)};$c.advance=@{}
$captured=Read-ContinueGenerations
Check ($captured.host.generation -eq 8 -and $captured.guest.generation -eq 19) 'capture both positive generations before Continue even at game-over'
foreach($role in 'host','guest'){
 foreach($case in 'missing','zero','negative','fraction','overflow','bad','stale','future','run','role','terminating','mission','tick-missing','tick-bad'){
  $c.states=@{host=(New-RecordedState 'host' 8);guest=(New-RecordedState 'guest' 19)}
  $s=$c.states[$role]
  switch($case){
   'missing'{$s.Remove('generation')};'zero'{$s.generation=0};'negative'{$s.generation=-1}
   'fraction'{$s.generation=8.5};'overflow'{$s.generation='4294967296'};'bad'{$s.generation='8x'}
   'stale'{$s.tick_ms=$c.now-1001};'future'{$s.tick_ms=$c.now+1};'run'{$s.run_id='f'*32}
   'role'{$s.role='other'};'terminating'{$s.terminating=$true};'mission'{$s.mission=10100}
   'tick-missing'{$s.Remove('tick_ms')};'tick-bad'{$s.tick_ms='bad'}
  }
  $threw=$false;try{$null=Read-ContinueGenerations}catch{$threw=$true}
  Check $threw ('Continue pre-submit refuses '+$role+' '+$case)
 }
}
$c.states=@{host=(New-RecordedState 'host' 8);guest=(New-RecordedState 'guest' 19)}
foreach($role in 'host','guest'){$c.states[$role].gameplay_ready=$true}
$continueRequirements=@{host=@{mission=30020;generation=8};guest=@{mission=30020;generation=19}}
$c.advance=@{host=@{at=$c.now+500;generation=9}}
$before=$c.now;Wait-ScenarioMission 'host' 30020 5
Check ($c.now-$before -eq 500) 'fresh same-mission old ready cannot admit Continue'
Check (!$continueRequirements.ContainsKey('host') -and $continueRequirements.ContainsKey('guest')) 'host admission clears only host requirement'
$c.advance=@{}
$threw=$false;try{Wait-ScenarioMission 'guest' 30020 5}catch{$threw=$true}
Check ($threw -and $continueRequirements.ContainsKey('guest')) 'guest old generation timeout retains its requirement'
$c.states.guest.generation=20;Wait-ScenarioMission 'guest' 30020 5
Check ($continueRequirements.Count -eq 0) 'guest clears independently only after its new allocation'
foreach($case in 'missing','zero','overflow','lower','equal','stale','future','wrong-run','wrong-role','terminating','not-ready'){
 $c.states=@{host=(New-RecordedState 'host' 9)};$c.states.host.gameplay_ready=$true;$c.advance=@{}
 $continueRequirements=@{host=@{mission=30020;generation=8};guest=@{mission=30020;generation=19}}
 switch($case){
  'missing'{$c.states.host.Remove('generation')};'zero'{$c.states.host.generation=0};'overflow'{$c.states.host.generation='4294967296'}
  'lower'{$c.states.host.generation=7};'equal'{$c.states.host.generation=8};'stale'{$c.states.host.tick_ms=$c.now-1001}
  'future'{$c.states.host.tick_ms=$c.now+100000};'wrong-run'{$c.states.host.run_id='f'*32};'wrong-role'{$c.states.host.role='guest'}
  'terminating'{$c.states.host.terminating=$true};'not-ready'{$c.states.host.gameplay_ready=$false}
 }
 $threw=$false;try{Wait-ScenarioMission 'host' 30020 5}catch{$threw=$true}
 Check ($threw -and $continueRequirements.Count -eq 2) ('same-mission wait refuses/retains '+$case)
}
$c.states=@{host=(New-RecordedState 'host' 8)};$c.states.host.mission=30010;$c.states.host.gameplay_ready=$true
$continueRequirements=@{host=@{mission=30020;generation=8};guest=@{mission=30020;generation=19}}
$before=$c.now;Wait-ScenarioMission 'host' 30010 5
Check ($c.now -eq $before -and !$continueRequirements.ContainsKey('host') -and $continueRequirements.ContainsKey('guest')) 'different-destination goto stays immediate and clears only accepted role'
# Helicopter space never reports gameplay_ready: waitmission 40050 resumes the
# owned tips pause, then arrives only on that allocation's runtime-phase metadata.
function New-HeliLoading([uint32]$Generation=4){
 return @{run_id=$runId;role='host';phase='initialized';mission=40050;generation=$Generation;tick_ms=$c.now}
}
$savedReadyAt=$c.readyAt;$c.readyAt=0;$c.case='pass';$c.advance=@{};$c.commands.Clear();$loadingAttempts=@{};$continueRequirements=@{}
$c.states=@{host=(New-RecordedState 'host' 3)};$c.states.host.mission=30050
$c.loading=@{host=(New-HeliLoading)};$c.runAfter=1
$before=$c.now;Wait-ScenarioMission 'host' 40050 30
Check ($c.commands.Count -eq 1 -and $c.commands[0].role -ceq 'host' -and $c.commands[0].operation -ceq 'resume_loading') 'heli-space wait resumes the owned tips pause once'
Check ($c.now-$before -eq 250) 'heli-space wait arrives on the runtime phase after the resume'
$c.commands.Clear();$c.loading=@{host=(New-HeliLoading)};$c.loading.host.phase='running';$c.runAfter=0
Wait-ScenarioMission 'host' 40050 5
Check ($c.commands.Count -eq 0) 'heli-space runtime phase needs no resume'
foreach($case in 'initialized','onfoot-ready','mission','run','role','zero','future','old-continue'){
 $c.commands.Clear();$loadingAttempts=@{};$continueRequirements=@{};$c.runAfter=1
 $c.states=@{host=(New-RecordedState 'host' 3)};$c.states.host.mission=30050
 $c.loading=@{host=(New-HeliLoading)}
 switch($case){
  'initialized'{$c.runAfter=0}
  'onfoot-ready'{$c.runAfter=0;$c.states.host.mission=40050;$c.states.host.gameplay_ready=$true}
  'mission'{$c.loading.host.mission=30050};'run'{$c.loading.host.run_id='f'*32};'role'{$c.loading.host.role='guest'}
  'zero'{$c.loading.host.generation=0};'future'{$c.loading.host.tick_ms=$c.now+100000}
  'old-continue'{$continueRequirements=@{host=@{mission=40050;generation=4}}}
 }
 $threw=$false;try{Wait-ScenarioMission 'host' 40050 5}catch{$threw=$true}
 Check $threw ('heli-space wait refuses '+$case)
}
$c.runAfter=0;$c.loading=@{};$c.states=@{};$continueRequirements=@{};$loadingAttempts=@{};$c.commands.Clear();$c.readyAt=$savedReadyAt
# Run the exact pre-submit path. The atomic writer records then stops before
# any filesystem log polling; no worker path or native controller is invoked.
$Mode='lab';$run='C:\fixture';$writes=[Collections.Generic.List[object]]::new()
function ConvertTo-WorldTriggerText([string]$Text){return $Text}
function Read-WorkerSessionJson([string]$Path){return @{run_id=$runId;status='ready';roles=@{host=@{game='C:\fixture\host'};guest=@{game='C:\fixture\guest'}}}}
function Assert-WorkerPlainPath {param([string]$Path,[switch]$MustExist) return $Path}
function Test-Path {param([string]$LiteralPath) return $false}
function Get-Item {param([string]$LiteralPath) return @{Length=0}}
function Write-WorkerAtomicText([string]$Path,[string]$Text){
 $writes.Add(@{path=$Path;text=$Text;reads=$c.reads.ToArray()});throw 'recorded trigger stop'
}
$c.states=@{host=(New-RecordedState 'host' 8);guest=(New-RecordedState 'guest' 19)};$c.reads.Clear()
$threw=$false;try{Invoke-ScenarioWorldTravel 'world_probe=host_continue' 'host'}catch{$threw=$_.Exception.Message -ceq 'recorded trigger stop'}
Check ($threw -and $writes.Count -eq 1 -and ($writes[0].reads -join ',') -ceq 'host,guest') 'both baselines captured before actual trigger writer'
$c.states.guest.Remove('generation');$writes.Clear()
$threw=$false;try{Invoke-ScenarioWorldTravel 'world_probe=host_continue' 'host'}catch{$threw=$true}
Check ($threw -and $writes.Count -eq 0) 'missing guest baseline refuses before any trigger write'
$c.states=@{};$c.reads.Clear()
$threw=$false;try{Invoke-ScenarioWorldTravel 'goto=30010' 'host'}catch{$threw=$_.Exception.Message -ceq 'recorded trigger stop'}
Check ($threw -and $writes.Count -eq 1 -and $c.reads.Count -eq 0) 'ordinary goto does not acquire Continue baseline'
# Exact world=0 cleanup uses the same admitted role release and private writer.
# These execute the real helper until its recorded atomic-write boundary only.
$c.states=@{};$c.reads.Clear();$continueRequirements=@{}
foreach($role in 'host','guest'){
 $writes.Clear();$beforeCommands=$c.commands.Count
 $threw=$false;try{Invoke-ScenarioWorldTravel 'world=0' $role}catch{$threw=$_.Exception.Message -ceq 'recorded trigger stop'}
 Check ($threw -and $writes.Count -eq 1 -and $writes[0].text -ceq 'world=0') ('exact world disable reaches owned writer '+$role)
 Check ($writes[0].path -ceq ('C:\fixture\'+$role+'\mgs-coop\world.trigger')) ('world disable stays in exact owned role '+$role)
 Check ($c.commands.Count -eq $beforeCommands+1 -and $c.commands[-1].role -ceq $role -and $c.commands[-1].operation -ceq 'release') ('world disable obtains role admission through release '+$role)
 Check ($c.reads.Count -eq 0 -and $continueRequirements.Count -eq 0) ('world disable cannot fabricate Continue baselines '+$role)
}
foreach($bad in 'world=1','world=2','world=3','world=0 world_probe=shared_death','world=0 goto=30020','world=00','WORLD=0','world=0 extra'){
 $writes.Clear();$threw=$false;try{Invoke-ScenarioWorldTravel $bad 'host'}catch{$threw=$true}
 Check ($threw -and $writes.Count -eq 0) ('world-disable helper rejects broader payload '+$bad)
}
foreach($role in 'both','other','HOST'){
 $writes.Clear();$threw=$false;try{Invoke-ScenarioWorldTravel 'world=0' $role}catch{$threw=$true}
 Check ($threw -and $writes.Count -eq 0) ('world-disable helper rejects role '+$role)
}
$Mode='play';$writes.Clear();$beforeCommands=$c.commands.Count
$threw=$false;try{Invoke-ScenarioWorldTravel 'world=0' 'host'}catch{$threw=$true}
Check ($threw -and $writes.Count -eq 0 -and $c.commands.Count -eq $beforeCommands) 'play refuses before role dispatch or write'
$Mode='lab';$c.case='refused';$writes.Clear()
$threw=$false;try{Invoke-ScenarioWorldTravel 'world=0' 'guest'}catch{$threw=$true}
Check ($threw -and $writes.Count -eq 0) 'native role admission refusal cannot write cleanup trigger'
$c.case='pass'
# Execute the entire real prelaunch scenario-validation loop, not a copied regex.
$validation=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.ForEachStatementAst] -and
 $n.Variable.VariablePath.UserPath -ceq 'line' -and $n.Condition.Extent.Text -ceq '$steps' -and
 $n.Body.Extent.Text.Contains('Invalid scenario step:')},$true))
Check ($validation.Count -eq 1) 'one real prelaunch scenario validation loop'
$validate=[scriptblock]::Create($validation[0].Extent.Text);$roleNames=@('host','guest','both')
foreach($role in 'host','guest'){
 $steps=@($role+' worldtrigger world=0');$threw=$false;try{. $validate}catch{$threw=$true}
 Check (!$threw) ('prelaunch admits exact world disable '+$role)
}
foreach($line in 'both worldtrigger world=0','other worldtrigger world=0','host worldtrigger world=00',
 'host worldtrigger world=1','guest worldtrigger world=3','host worldtrigger world=0 goto=30020',
 'guest worldtrigger world=0 world_probe=shared_death','host worldtrigger WORLD=0'){
 $steps=@($line);$threw=$false;try{. $validate}catch{$threw=$true}
 Check $threw ('prelaunch refuses broader cleanup scenario '+$line)
}
$Mode='play';$steps=@('host worldtrigger world=0');$threw=$false;try{. $validate}catch{$threw=$true}
Check $threw 'prelaunch play refuses world-disable test'
$Mode='lab'
# Execute the actual applied-receipt branch: pending state is installed only
# after acceptance, and logs name the real role rather than a literal host.
$applied=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.IfStatementAst] -and
 $n.Clauses[0].Item1.Extent.Text -ceq "`$result.State -ceq 'applied'"},$true))
Check ($applied.Count -eq 1) 'one actual applied travel receipt branch'
$result=@{State='applied'};$captured=@{host=@{mission=30020;generation=8};guest=@{mission=30020;generation=19}}
$continueRequirements=@{};$canonical='world_probe=host_continue';$Role='host';$printed=[Collections.Generic.List[string]]::new()
function Write-Host([object]$Object){$printed.Add([string]$Object)}
. ([scriptblock]::Create($applied[0].Extent.Text))
Check ($continueRequirements.Count -eq 2 -and $continueRequirements.guest.generation -eq 19) 'accepted Continue installs both captured requirements'
$captured=$null;$Role='guest';$canonical='world_probe=shared_death'
. ([scriptblock]::Create($applied[0].Extent.Text))
Check ($printed[-1] -ceq '  guest world travel: world_probe=shared_death applied') 'receipt prints actual guest role'
Check ($continueRequirements.Count -eq 2) 'non-Continue applied receipt leaves pending requirements intact'
$c.states=@{};$c.advance=@{};$continueRequirements=@{}
Remove-Item function:Test-Path
Remove-Item function:Write-Host
# No pending Continue still requires a valid loaded generation; corrupted
# state cannot become arrival evidence just because a goto changed mission.
foreach($case in 'missing','zero'){
 $c.states=@{host=(New-RecordedState 'host' 8)};$c.states.host.gameplay_ready=$true
 if($case -ceq 'missing'){$c.states.host.Remove('generation')}else{$c.states.host.generation=0}
 $threw=$false;try{Wait-ScenarioMission 'host' 30020 5}catch{$threw=$true}
 Check $threw ('ordinary destination refuses '+$case+' generation')
}
$c.states=@{}
# Pure disembark classification uses the actual helper, with no input calls.
$heli=@{run_id=$runId;role='guest';mission=30020;gameplay_ready=$true;terminating=$false;local_anim_valid=$true;local_act=98;tick_ms=10000}
foreach($act in 98,103,108){
 $heli.local_act=$act
 Check ((Get-ScenarioWaitAction 'guest' 'helicopter' $heli 10000) -ceq 'e') ('exact heli act accepted '+$act)
}
foreach($mission in 30010,30020,30050){
 $heli.mission=$mission
 Check ((Get-ScenarioWaitAction 'guest' 'helicopter' $heli 11000) -ceq 'e') ('supported mission and age1000 accepted '+$mission)
}
$heli.mission=30020;$heli.local_act=103
foreach($case in 'null','result','result-prefix','result-case','onfoot','run','role','request-role','acc','unsupported','act-low','act-high','act-bad','act-missing','anim-invalid','stale','future','tick-missing','tick-bad','tick-negative','terminated','not-ready'){
 $s=$heli.Clone();$result='helicopter';$role='guest'
 switch($case){
  'null'{$s=$null};'result'{$result='demo'};'result-prefix'{$result='helicopter extra'};'result-case'{$result='Helicopter'}
  'onfoot'{$result='position=1,2,3;yaw=0'};'run'{$s.run_id='f'*32};'role'{$s.role='host'};'request-role'{$role='both'}
  'acc'{$s.mission=40050};'unsupported'{$s.mission=10100};'act-low'{$s.local_act=97};'act-high'{$s.local_act=109}
  'act-bad'{$s.local_act='98x'};'act-missing'{$s.Remove('local_act')};'anim-invalid'{$s.local_anim_valid=$false}
  'stale'{$s.tick_ms=8999};'future'{$s.tick_ms=10001};'tick-missing'{$s.Remove('tick_ms')};'tick-bad'{$s.tick_ms='x'}
  'tick-negative'{$s.tick_ms=-1};'terminated'{$s.terminating=$true};'not-ready'{$s.gameplay_ready=$false}
 }
 Check ((Get-ScenarioWaitAction $role $result $s 10000) -ceq 'skip') ('disembark refuses E '+$case)
}
# Execute the actual waitgameplay loop with recorded debug results and queue:
# position breaks before dispatch; helicopter emits only E120, all others
# retain the previous Escape/Enter skip. No real clock sleep or input is used.
$waitLoop=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.WhileStatementAst] -and
 $n.Extent.Text.Contains("Invoke-DebugResult `$t[1] 'position' 6000")},$true))
Check ($waitLoop.Count -eq 1) 'one actual on-foot wait loop'
$readRoleBody=${function:Read-RoleRaw}
$wait=[pscustomobject]@{state=$heli.Clone();results=@();index=0}
function Read-RoleRaw([string]$Role){
 $s=$wait.state.Clone();$s.tick_ms=[Environment]::TickCount64;return $s
}
function Invoke-DebugResult([string]$Role,[string]$Command,[int]$TimeoutMs){$v=$wait.results[$wait.index];$wait.index++;return $v}
foreach($case in 'heli','other','onfoot','acc'){
 $wait.state=$heli.Clone();$wait.index=0;$c.commands.Clear();$c.case='pass'
 $wait.results=@(if($case -ceq 'onfoot'){'position=1,2,3;yaw=0'}elseif($case -ceq 'other'){'demo';'position=1,2,3;yaw=0'}else{'helicopter';'position=1,2,3;yaw=0'})
 if($case -ceq 'acc'){$wait.state.mission=40050}
 $t=@('waitgameplay','guest','5');$limit=[Environment]::TickCount64+5000;$last=0;$result=''
 . ([scriptblock]::Create($waitLoop[0].Extent.Text))
 $keys=@($c.commands|ForEach-Object{$_.args.Keys}) -join ','
 if($case -ceq 'heli'){
  Check ($c.commands.Count -eq 1 -and $keys -ceq 'e' -and $c.commands[0].args.DurationMs -eq 120 -and $c.commands[0].role -ceq 'guest') 'actual helicopter loop queues only bounded owned E'
 }elseif($case -ceq 'onfoot'){
  Check ($c.commands.Count -eq 0) 'actual on-foot result breaks before any press or boarding toggle'
 }else{
  Check ($c.commands.Count -eq 2 -and $keys -ceq 'escape,enter') ('actual loop retains old skip '+$case)
 }
}
Set-Item -LiteralPath 'Function:\Read-RoleRaw' -Value $readRoleBody
$c.states=@{};$c.advance=@{};$continueRequirements=@{}
# Source-level lifecycle contract: publication uses existing accepted return
# and revocation precedes runtime entry; the pure native fixture tests delivery.
$native=[IO.File]::ReadAllText((Join-Path $RepoRoot 'native/game_script_observer.cpp'))
Check ($native.Contains('if(accepted&&initialization_scope.Complete()&&!termination_scope.Active())')) 'loading admission requires accepted original initialization return'
$at=$native.IndexOf('boundary="runtime_enter";');$runtime=$native.Substring($at,230)
Check ($runtime.Contains('LoadingPhase::running')) 'runtime boundary revokes before gameplay callback'
Check ($native.Contains('LoadingPhase::terminating')) 'termination boundary revokes'
$senderPath=Join-Path $RepoRoot 'tools/worker/Send-WorkerCommand.ps1'
$ast=[Management.Automation.Language.Parser]::ParseFile($senderPath,[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'sender parses'
$sender=[IO.File]::ReadAllText($senderPath)
Check ($sender.Contains('resume_loading $($loading.mission) $($loading.generation) 0')) 'wire uses loading token not stale gameplay snapshot'
Check ($sender.Contains('$loading.pid -ne $game.Id') -and $sender.Contains('$loading.process_start_filetime')) 'existing owned process lease checked against loading metadata'
# Exercise the actual inline loading validation block with mocked owned file,
# without evaluating the script's process/controller/writer code.
$gate=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.IfStatementAst] -and
 $n.Clauses[0].Item1.Extent.Text -ceq "`$Operation -eq 'resume_loading'"},$true))
Check ($gate.Count -eq 1) 'one exact loading gate'
$Operation='resume_loading';$run='C:\fixture';$Role='host'
$status=@{run_id=$runId};$game=@{Id=12};$owned=@(@{creation_filetime='345'})
$good=@{run_id=$runId;role='host';pid=12;process_start_filetime='345';tick_ms=[Environment]::TickCount64;phase='initialized';mission=30020;generation=8}
$fileAttributes=[IO.FileAttributes]::Normal;$loadingFixture=$good.Clone()
function Get-Item {param([string]$LiteralPath,[switch]$Force) @{Attributes=$fileAttributes}}
function Get-Content {param([string]$LiteralPath,[switch]$Raw) $loadingFixture|ConvertTo-Json -Compress}
. ([scriptblock]::Create($gate[0].Extent.Text))
Check ($loading.generation -eq 8) 'current initialized owned file accepted'
foreach($case in 'run','role','pid','process','phase','mission','generation','future','stale','link'){
 $loadingFixture=$good.Clone();$fileAttributes=[IO.FileAttributes]::Normal
 switch($case){
 'run'{$loadingFixture.run_id='f'*32};'role'{$loadingFixture.role='guest'};'pid'{$loadingFixture.pid=13}
 'process'{$loadingFixture.process_start_filetime='999'};'phase'{$loadingFixture.phase='running'}
 'mission'{$loadingFixture.mission=10100};'generation'{$loadingFixture.generation=0}
 'future'{$loadingFixture.tick_ms=[Environment]::TickCount64+100000}
 'stale'{$loadingFixture.tick_ms=[Environment]::TickCount64-400001}
 'link'{$fileAttributes=[IO.FileAttributes]::ReparsePoint}
 }
 $threw=$false;try{. ([scriptblock]::Create($gate[0].Extent.Text))}catch{$threw=$true}
 Check $threw ('actual loading lease refuses '+$case)
}
Write-Host "worker_loading checks=$checks passed; detached source/recording only"
