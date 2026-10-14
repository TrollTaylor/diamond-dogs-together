#Requires -Version 7
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$stage=Split-Path -Parent $PSScriptRoot
$cases=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Okay){if(!$Okay){throw $Name};$cases.Add([ordered]@{name=$Name;passed=$true})}
function Refuses([string]$Name,[scriptblock]$Action){$rejected=$false;try{&$Action}catch{$rejected=$true};Check $Name $rejected}
foreach($relative in @('tools/build.ps1','tools/worker/WorkerSessionCommon.psm1','tools/worker/Start-WorkerSession.ps1','tools/worker/Invoke-WorkerScenario.ps1','tools/worker/Invoke-WorkerChecks.ps1')){
 $tokens=$null;$errors=$null;$null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $stage (''+$relative)),[ref]$tokens,[ref]$errors)
 Check ('parse '+$relative) (!$errors.Count)
}
function Import-OnlyFunctions([string]$Path,[string[]]$Names){
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
 $definitions=$ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst]},$true)
 foreach($name in $Names){$definition=@($definitions|Where-Object{$_.Name -ceq $name});if($definition.Count -ne 1){throw "Cannot isolate $name"}
  . ([scriptblock]::Create($definition[0].Extent.Text.Replace("function $name", "function script:$name")))
 }
}
Import-OnlyFunctions (Join-Path $stage 'tools/worker/WorkerSessionCommon.psm1') @('Get-WorkerMgoStandingCycleExperiments')
Import-OnlyFunctions (Join-Path $stage 'tools/worker/Invoke-WorkerChecks.ps1') @('Get-WorkerExperiments','Get-WorkerReleaseExperiments','Get-WorkerHash','Test-WorkerUnder','Assert-WorkerPath','Get-WorkerPlan','Get-WorkerTests')
$flag='MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER'
$base=@(Get-WorkerMgoStandingCycleExperiments)
Check 'default exact four flags' ($base.Count -eq 4 -and $flag -cnotin $base)
Refuses 'frame requires explicit lookup' {Get-WorkerMgoStandingCycleExperiments -FrameObserver}
$lookup=@(Get-WorkerMgoStandingCycleExperiments -LookupObserver)
Check 'lookup remains distinct five flags' ($lookup.Count -eq 5 -and $flag -cnotin $lookup)
$frame=@(Get-WorkerMgoStandingCycleExperiments -LookupObserver -FrameObserver)
Check 'frame distinct six flags' ($frame.Count -eq 6 -and $flag -cin $frame)
$directory=Join-Path $stage 'build/only-policy-no-build'
function Plan([string[]]$Flags,[string]$Profile='Research',[bool]$Background=$true){Get-WorkerPlan -Root $stage -SelectedProfile $Profile -SelectedConfiguration Release -Experiments $Flags -Background $Background -Directory $directory}
$null=Plan $frame;Check 'six explicit worker flags accepted' $true
$null=Plan (@($frame)+@('MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER'));Check 'activation observer separately accepted' $true
Refuses 'normal refuses frame experiment' {Plan $frame Normal $true}
Refuses 'release refuses frame experiment' {Plan $frame Release $false}
Refuses 'research workerOFF refuses frame' {Plan $frame Research $false}
Refuses 'frame without lookup refused' {Plan (@($base)+@($flag))}
Refuses 'frame without standing-cycle refused' {Plan @('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST','MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER',$flag)}
Refuses 'mixed old unique refused' {Plan (@($frame)+@('MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER'))}
Refuses 'duplicate frame refused' {Plan (@($frame)+@($flag))}
Refuses 'selection has no experiment admission' {Plan (@($frame)+@('MGS_LOCAL_MGO_STANDING_SELECTION'))}
Refuses 'case-changed frame refused' {Plan (@($base)+@('mgs_local_mgo_standing_frame_observer'))}
foreach($name in @('Start-WorkerSession.ps1','Invoke-WorkerScenario.ps1')){
 $text=Get-Content -LiteralPath (Join-Path $stage ('tools/worker/'+$name)) -Raw
 Check ($name+' explicit frame switch') ($text.Contains('[switch]$StandingFrameObserver'))
 Check ($name+' exact standing+lookup scope') ($text.Contains("if(`$StandingFrameObserver -and (`$BuildProfile -cne 'mgo-standing-cycle' -or !`$StandingLookupObserver))"))
 Check ($name+' exact receipt scope') ($text.Contains('-FrameObserver:$StandingFrameObserver'))
}
$invoke=Get-Content (Join-Path $stage 'tools/worker/Invoke-WorkerScenario.ps1') -Raw
Check 'invoke forwards exact switch' ($invoke.Contains('-StandingFrameObserver:$StandingFrameObserver -NativeActivationObserver:$NativeActivationObserver'))
$cmake=Get-Content (Join-Path $stage 'CMakeLists.txt') -Raw
Check 'build default OFF' ($cmake.Contains('option(MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER "Private native motion callback diagnostics; original always forwarded, no selection" OFF)'))
Check 'CMake refuses any missing prerequisite' ($cmake.Contains('if(MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER AND (NOT MGS_BACKGROUND_WORKER OR NOT MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER OR NOT MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER))'))
$build=Get-Content (Join-Path $stage 'tools/build.ps1') -Raw
Check 'normal/release forcibly turn new observer OFF' ($build.Contains("        '$flag',"))
"Frame runtime policy fixtures passed: $($cases.Count)"
