#Requires -Version 7
# Detached pure protocol/policy/INI/AST fixtures. Never invokes a controller,
# native worker, installer, process enumeration or window/input API.
[CmdletBinding()]
param([string]$SourceDirectory,[string]$RepoRoot)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if(!$RepoRoot){$RepoRoot=if((Split-Path -Leaf $PSScriptRoot) -ceq 'tests'){Split-Path -Parent $PSScriptRoot}else{[IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))}}
if(!$SourceDirectory){$SourceDirectory=if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'WorkerSessionCommon.psm1')){$PSScriptRoot}else{Join-Path $RepoRoot 'tools\worker'}}
Import-Module (Join-Path $RepoRoot 'tools\lab\LabCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $SourceDirectory 'WorkerSessionCommon.psm1') -Force -DisableNameChecking
$script:checks=0
function Assert-True([bool]$Value,[string]$Name){++$script:checks;if(!$Value){throw "Fixture failed: $Name"}}
function Assert-Throws([scriptblock]$Action,[string]$Name){$threw=$false;try{& $Action|Out-Null}catch{$threw=$true};Assert-True $threw $Name}
$id='0123456789abcdef0123456789abcdef'
Assert-True (Test-WorkerRunId $id) 'run32'
foreach($bad in @('','0123',$id.ToUpperInvariant(),($id+'0'),($id+"`n"))){Assert-True (!(Test-WorkerRunId $bad)) 'invalid run id'}
Assert-True (Test-WorkerDescendant 'C:\repo\build\worker-runtime\abc\host-saves' 'C:\repo\build\worker-runtime\abc') 'strict descendant'
foreach($path in @('C:\repo\build\worker-runtime\abc','C:\repo\build\worker-runtime\abcd\host','C:\repo\build\worker-runtime\abc\..\foreign')){Assert-True (!(Test-WorkerDescendant $path 'C:\repo\build\worker-runtime\abc')) 'reject escaped or equal descendant'}
$lease=@{pid=41;creation_filetime='12345';exe='C:\build\worker_supervisor.exe'}
Assert-True (Test-WorkerProcessIdentity $lease $lease) 'exact process lease'
foreach($replace in @(@{pid=42;creation_filetime='12345';exe=$lease.exe},@{pid=41;creation_filetime='12346';exe=$lease.exe},@{pid=41;creation_filetime='12345';exe='C:\foreign\worker_supervisor.exe'})){Assert-True (!(Test-WorkerProcessIdentity $replace $lease)) 'reject replacement process'}
$hash='A'*64
$state=@{run_id=$id;supervisor=@{pid=41;creation_filetime='12345';exe=$lease.exe;sha256=$hash}}
$status=@{schema=1;run_id=$id;desktop='MGSWorker_'+$id;supervisor_pid=41;supervisor_start_filetime='12345';supervisor_sha256=$hash;
    desktop_ownership_violation=$false;input_desktop_private=$false;switch_desktop_called=$false;system_input_called=$false;focus_called=$false;
    visible_desktop_unchanged_at_observations=$true;job_kill_on_close=$true;handle_inheritance=$false}
Assert-True (Test-WorkerSupervisorReceipt $status $state) 'safe supervisor receipt'
foreach($key in @('desktop_ownership_violation','input_desktop_private','switch_desktop_called','system_input_called','focus_called','handle_inheritance')){
    $changed=$status.Clone();$changed[$key]=$true
    Assert-True (!(Test-WorkerSupervisorReceipt $changed $state)) ('isolation refusal '+$key)
    Assert-True (Test-WorkerSupervisorIdentity $changed $state) 'identity permits owned failure cleanup only'
}
foreach($key in @('visible_desktop_unchanged_at_observations','job_kill_on_close')){$changed=$status.Clone();$changed[$key]=$false;Assert-True (!(Test-WorkerSupervisorReceipt $changed $state)) 'missing isolation capability'}
$changed=$status.Clone();$changed.supervisor_start_filetime='99999';Assert-True (!(Test-WorkerSupervisorIdentity $changed $state)) 'cleanup rejects reused PID'
$context="MGSWORKERCTX1 $id host 41 12345 MGSWorker_$id admitted`n"
Assert-True (Test-WorkerContextText $context $id 'host' $lease) 'native context exact owner'
foreach($changed in @($context.Replace('host','guest'),$context.Replace('41','42'),$context.Replace('12345','99999'),$context.Replace('admitted','pending'),$context.TrimEnd([char]10),$context.Replace("`n","`r`n"),($context+"`n"))){Assert-True (!(Test-WorkerContextText $changed $id 'host' $lease)) 'context mismatch'}
$flags=[ordered]@{}
foreach($name in @('MGS_LOCAL_GUN_PRELOAD_TEST','MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST','MGS_LOCAL_STOCK46_AMMO_TEST','MGS_LOCAL_ACTION_EVENTS','MGS_LOCAL_UPPER_END_OBSERVE','MGS_LOCAL_UPPER_PHASE_OBSERVE','MGS_LOCAL_UPPER_MANUAL_IDLE','MGS_LOCAL_UPPER_PROBE','MGS_LOCAL_UPPER_CHANNEL2','MGS_LOCAL_UPPER_FIRE_ZERO_BLEND')){$flags[$name]='OFF'}
$flags.MGS_BACKGROUND_WORKER='ON'
$receipt=@{schema=1;status='passed';plan=@{profile='Normal';project='C:\repo';build_directory='C:\repo\build\worker';flags=$flags};artifacts=@{loader_sha256=$hash}}
Assert-True (Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' $hash) 'normal21 worker build checks'
$receipt.plan.profile='Research';Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' $hash)) 'reject research profile';$receipt.plan.profile='Normal'
foreach($name in @($flags.Keys|Where-Object {$_ -cne 'MGS_BACKGROUND_WORKER'})){$flags[$name]='ON';Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' $hash)) 'reject every experiment';$flags[$name]='OFF'}
$flags.MGS_BACKGROUND_WORKER='OFF';Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' $hash)) 'worker disabled';$flags.MGS_BACKGROUND_WORKER='ON'
Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\other' 'C:\repo' $hash)) 'different build'
Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' ('B'*64))) 'different loader'
# Native puppet worker profile (docs/native-puppet.md 13.1): exact settings per stage.
$animatedSettings=@('partner=1','net=1','partner_stage=3','partner_source=net','partner_look=net','partner_apply=xform','partner_anim=off',
    'partner_reset=n','partner_input=off','partner_dm=map','world=3')
Assert-True (((Get-WorkerPartnerSettings 'animated') -join ' ') -ceq ($animatedSettings -join ' ')) 'animated profile unchanged'
Assert-True ((Get-WorkerPartnerSettings 'input') -ccontains 'partner_apply=input' -and (Get-WorkerPartnerSettings 'input') -ccontains 'partner_input=net') 'input profile unchanged'
$nativeBase=$animatedSettings+@('partner_drive=native','partner_input_send=1','partner_input_hook=1','partner_input_hold=own','partner_input_words=hpr',
    'partner_input_wrap=window','partner_input_cam=sandbox','partner_input_local=main','partner_recon=track','partner_input_slot=body')
$stages=@{
    census=@('partner_input_defer=census','partner_input_mask=0x2240006','partner_native_aim=off')
    loco=@('partner_input_defer=on','partner_input_mask=0x2240006','partner_native_aim=off','partner_native_rates=mirror')
    gun=@('partner_input_defer=on','partner_input_mask=0x224004E','partner_native_gun=1','partner_native_fire=0','partner_native_aim=look',
        'partner_input_triggers=1','partner_input_rstick=1','partner_native_rates=mirror')
}
# EVADE 0x40000 (dive/evade/roll) is in every native mask; ACTION 0x20 (ride, knife kill, pickup) and FIRE 0x10 never.
foreach($stage in @('census','loco','gun')){
    $maskSetting=@((Get-WorkerPartnerSettings 'native' $stage)|Where-Object{$_ -clike 'partner_input_mask=*'})
    Assert-True ($maskSetting.Count -eq 1) ('one mask in native stage '+$stage)
    $maskValue=[Convert]::ToUInt32($maskSetting[0].Substring('partner_input_mask=0x'.Length),16)
    Assert-True ((($maskValue -band 0x40000) -ne 0) -and (($maskValue -band 0x30) -eq 0)) ('native stage '+$stage+' passes EVADE, never ACTION/FIRE')
}
foreach($stage in @('census','loco','gun')){
    Assert-True (((Get-WorkerPartnerSettings 'native' $stage) -join ' ') -ceq (($nativeBase+$stages[$stage]) -join ' ')) ('native stage settings '+$stage)
}
Assert-True (((Get-WorkerPartnerSettings 'native') -join ' ') -ceq (($nativeBase+$stages.loco) -join ' ')) 'native default stage is loco'
foreach($setting in (Get-WorkerPartnerSettings 'native' 'gun')+(Get-WorkerPartnerSettings 'native' 'loco')){$kv=$setting.Split('=',2);$null=ConvertTo-LabPartnerValue -Key $kv[0] -Value $kv[1]}

Assert-True $true 'every native setting is a known LabCommon key with a valid value'
Assert-Throws {Get-WorkerPartnerSettings 'native' 'fire'} 'unknown native stage throws'
Assert-Throws {Get-WorkerPartnerSettings 'animated' 'loco'} 'NativeStage needs the native profile'
Assert-Throws {Get-WorkerPartnerSettings 'puppet'} 'unknown profile throws'
$research=@(Get-WorkerResearchExperiments)
Assert-True (($research -join ',') -ceq 'MGS_LOCAL_OFFLINE_EQUIPMENT_TEST,MGS_LOCAL_STOCK46_PRELOAD_TEST,MGS_LOCAL_STOCK46_WRITER_TEST') 'research experiments: offline equipment + stock46 preload + writer'
Assert-True ((@(Get-WorkerResearchExperiments -Ammo)) -ccontains 'MGS_LOCAL_STOCK46_AMMO_TEST') 'research experiments: optional ammo'
$researchDirectory=Get-WorkerResearchBuildDirectory 'C:\repo' $research
$expectedHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes(($research -join "`n")))).Substring(0,12)
Assert-True ($researchDirectory -ceq ('C:\repo\build\worker-checks\research-'+$expectedHash+'-worker-on-release')) 'research cache directory = Invoke-WorkerChecks profile key'
$researchFlags=[ordered]@{}
foreach($name in @($flags.Keys)){$researchFlags[$name]='OFF'}
$researchFlags.MGS_BACKGROUND_WORKER='ON';$researchFlags.MGS_LOCAL_OFFLINE_EQUIPMENT_TEST='ON';$researchFlags.MGS_LOCAL_STOCK46_PRELOAD_TEST='ON';$researchFlags.MGS_LOCAL_STOCK46_WRITER_TEST='ON'
$researchReceipt=@{schema=1;status='passed';plan=@{profile='Research';project='C:\repo';build_directory=$researchDirectory;flags=$researchFlags};artifacts=@{loader_sha256=$hash}}
Assert-True (Test-WorkerResearchReceipt $researchReceipt $researchDirectory 'C:\repo' $hash $research) 'research receipt with exactly the stock46 experiments'
$researchFlags.MGS_LOCAL_UPPER_PROBE='ON';Assert-True (!(Test-WorkerResearchReceipt $researchReceipt $researchDirectory 'C:\repo' $hash $research)) 'research receipt refuses an upper_* experiment';$researchFlags.MGS_LOCAL_UPPER_PROBE='OFF'
$researchFlags.MGS_LOCAL_STOCK46_WRITER_TEST='OFF';Assert-True (!(Test-WorkerResearchReceipt $researchReceipt $researchDirectory 'C:\repo' $hash $research)) 'research receipt needs the writer';$researchFlags.MGS_LOCAL_STOCK46_WRITER_TEST='ON'
$researchReceipt.plan.profile='Normal';Assert-True (!(Test-WorkerResearchReceipt $researchReceipt $researchDirectory 'C:\repo' $hash $research)) 'research receipt needs the Research profile';$researchReceipt.plan.profile='Research'
Assert-True (!(Test-WorkerCheckReceipt $researchReceipt $researchDirectory 'C:\repo' $hash)) 'a research receipt never passes the normal check'
$startText=[IO.File]::ReadAllText((Join-Path $SourceDirectory 'Start-WorkerSession.ps1'))
Assert-True ($startText.Contains('$settings=Get-WorkerPartnerSettings $PartnerProfile $NativeStage') -and
    $startText.Contains("[ValidateSet('animated','input','native')][string]`$PartnerProfile='animated'") -and
    $startText.Contains("throw '-NativeStage gun needs -BuildProfile research (a weapon on body 1).'")) 'Start-WorkerSession uses the pure profile settings and refuses gun without research'
$ini="# Generated by tools/lab/Prepare-LabInstance.ps1`nenabled=1`nrole=host`nloopback=1`nxinput=none`nsave_dir=C:\copies`nmutex_suffix=host`n"
$updated=Set-WorkerIniValue $ini 'save_dir' 'C:\fresh\host'
Assert-True ($updated.Contains("save_dir=C:\fresh\host`n")) 'literal INI path rewrite'
Assert-True ($updated.Contains("role=host`n")) 'local unaffected fields'
Assert-Throws {Set-WorkerIniValue ($ini+"save_dir=C:\duplicate`n") 'save_dir' 'C:\fresh'} 'duplicate save key'
Assert-Throws {Set-WorkerIniValue $ini 'missing' 'value'} 'missing generated key'
Assert-Throws {Set-WorkerIniValue $ini 'save_dir' "C:\copies`nforeign=1"} 'control character replacement'
$url='http://127.0.0.1:47899/tppstm/gate';$steam='76561197960265729'
Assert-True (Test-WorkerPreparedConfigText 'lab.ini' $ini 'host' $steam $url @('C:\copies')) 'partial owned Prepare output'
Assert-True (!(Test-WorkerPreparedConfigText 'lab.ini' $ini 'guest' $steam $url @('C:\copies'))) 'foreign role output'
Assert-True (!(Test-WorkerPreparedConfigText 'lab.ini' $ini 'host' $steam $url @('C:\foreign'))) 'foreign save output'
Assert-True (Test-WorkerPreparedConfigText 'script-observe.enabled' "offline-v1`n" 'host' $steam $url @()) 'exact LF marker'
Assert-True (!(Test-WorkerPreparedConfigText 'script-observe.enabled' "offline-v1`r`n" 'host' $steam $url @())) 'CRLF marker refused'
Assert-True (Test-WorkerPreparedConfigText 'client-host.txt' ($steam+"`n") 'guest' $steam $url @()) 'owned guest client identity'
Assert-True (!(Test-WorkerPreparedConfigText 'client-host.txt' ($steam+"`n") 'host' $steam $url @())) 'host client identity not adopted'
foreach($name in @('Start-WorkerSession.ps1','Stop-WorkerSession.ps1','WorkerSessionCommon.psm1')) {
    $path=Join-Path $SourceDirectory $name;$tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    Assert-True (@($errors).Count -eq 0) ('syntax '+$name)
    $text=[IO.File]::ReadAllText($path)
    Assert-True ($text -notmatch '(?i)SwitchDesktop\s*\(|SendInput\s*\(|SetForegroundWindow\s*\(|SetFocus\s*\(') 'no global UI API'
    $commands=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst]},$true)|ForEach-Object {$_.GetCommandName()})
    Assert-True (@($commands|Where-Object {$_ -in @('Start-Lab','Start-Lab.ps1','Send-LabInput','Send-LabInput.ps1','Get-LabScreens','Get-LabScreens.ps1','Start-Process','Invoke-Expression')}).Count -eq 0) 'no visible launch or arbitrary interpreter'
}
$start=[IO.File]::ReadAllText((Join-Path $SourceDirectory 'Start-WorkerSession.ps1'))
Assert-True ($start.Contains('Invoke-WorkerProbe.ps1') -and $start.Contains('-PassThru')) 'fresh probe every actual start'
Assert-True ($start.Contains('$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden')) 'supervisor hidden process launch'
Assert-True ($start.Contains('Get-LabSafetyVerdict') -and $start.Contains('Test-WorkerContextText')) 'actual context and safety required'
Assert-True ($start.Contains("`$raw.xinput.host='none';`$raw.xinput.guest='none'")) 'both virtual controller masks'
$tokens=$null;$errors=$null
$startAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $SourceDirectory 'Start-WorkerSession.ps1'),[ref]$tokens,[ref]$errors)
$prepareCommands=@($startAst.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst] -and $node.Extent.Text -match '^&\s+\(Join-Path\s+\$repo\s+''tools\\lab\\Prepare-LabInstance\.ps1''\)'},$true))
Assert-True ($prepareCommands.Count -eq 1) 'one exact Prepare boundary'
$prepareParameters=@($prepareCommands[0].CommandElements|Where-Object {$_ -is [Management.Automation.Language.CommandParameterAst]}|ForEach-Object {$_.ParameterName})
Assert-True ('SkipSaves' -cin $prepareParameters -and 'SkipGraphics' -cin $prepareParameters) 'Prepare cannot copy stale lab saves or edit existing lab graphics'
Assert-True (@($startAst.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Copy-WorkerSaveTree'},$true)).Count -eq 0) 'Start never copies previous lab progress through the old tree helper'
Assert-True ($start.Contains('Assert-WorkerRealSaveSource $identity.UserData') -and $start.Contains('backup_receipt_sha256=Get-WorkerHash $backupReceipt') -and $start.Contains("backup_manifest_sha256=Get-WorkerHash (Join-Path `$freshBackup 'manifest.json')")) 'fresh save source and backup receipts are qualified'
$startAssignments=@{}
foreach($name in @('freshBackup','manifestBackup','saveRoot','graphics')) {
    $definition=@($startAst.FindAll({param($node)$node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and $node.Left.VariablePath.UserPath -ceq $name},$true))
    Assert-True ($definition.Count -eq 1) ('actual fresh-save assignment '+$name)
    $startAssignments[$name]=$definition[0].Extent.Text
}
# Two seed forms: the default reads only this run's verified real-save backup;
# the explicit -SaveSeed lab opt-in copies each folder's own lab save copy
# (never the real saves) into the fresh run directory.
$seedDefinitions=@($startAst.FindAll({param($node)$node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and $node.Left.VariablePath.UserPath -ceq 'seed'},$true)|ForEach-Object{$_.Extent.Text})
Assert-True ($seedDefinitions.Count -eq 2) 'two seed forms'
Assert-True ($seedDefinitions -ccontains "`$seed=Copy-SaveSeed -BackupDirectory `$freshBackup -SaveDir `$saveRoot -LocalSubdir '287700\local' -RemoteSubdir 'remote'") 'default role seed reads only this run verified backup'
Assert-True ($start.Contains("[ValidateSet('real','lab')][string]`$SaveSeed='real'") -and $start.Contains("`$labSaves=Assert-WorkerPlainPath `$p.SaveDir -MustExist")) 'lab seed is an explicit opt-in from the folder lab save copy'
Assert-True ($startAssignments.graphics -ceq "`$graphics=Join-Path `$saveRoot '287700\local\TPP_GRAPHICS_CONFIG'") 'graphics edits target the fresh role copy'
$stop=[IO.File]::ReadAllText((Join-Path $SourceDirectory 'Stop-WorkerSession.ps1'))
Assert-True ($stop.Contains('Get-WorkerLeasedProcess') -and $stop.Contains('MGSWORKERCTL1')) 'owned control shutdown'
Assert-True ($stop.Contains('-Action Restore') -and !$stop.Contains('-Disable -Role')) 'owned package restore without generic lab disable'
Assert-True (!$stop.Contains('Copy-SaveSeed') -and !$stop.Contains('Backup-RealSaves')) 'no writes to real-save recovery'

# Execute the ACTUAL staged Stop restoration orchestration, with only the
# process enumeration and package-install boundary replaced. Files, hashes,
# snapshot preflight and snapshot restoration use owned temporary bytes.
$tokens=$null;$errors=$null
$stopAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $SourceDirectory 'Stop-WorkerSession.ps1'),[ref]$tokens,[ref]$errors)
foreach($name in @('Assert-WorkerConfigSnapshotPreflight','Invoke-WorkerRestoreOwnedSession','Get-WorkerRealSaveComparison')) {
    $definition=@($stopAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
    Assert-True ($definition.Count -eq 1) ('actual cleanup boundary '+$name)
    . ([scriptblock]::Create($definition[0].Extent.Text))
}
# Extract only the reader helper. The scenario controller is never invoked.
$tokens=$null;$errors=$null
$scenarioAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $RepoRoot 'tools\worker\Invoke-WorkerScenario.ps1'),[ref]$tokens,[ref]$errors)
Assert-True (@($errors).Count -eq 0) 'scenario reader source syntax'
$receiptReader=@($scenarioAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Read-NativeReceipt'},$true))
Assert-True ($receiptReader.Count -eq 1) 'one actual native receipt reader boundary'
. ([scriptblock]::Create($receiptReader[0].Extent.Text))
$landingDefinitions=@{}
foreach($name in @('Wait-ViewerLanding','Reset-ScenarioPose')) {
    $definition=@($scenarioAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
    Assert-True ($definition.Count -eq 1) ('actual placement boundary '+$name)
    $landingDefinitions[$name]=$definition[0].Extent.Text
}
# Exercise trajectories through the actual helper bodies. The scoped mocks
# supply copied telemetry; no scenario, input writer or game is invoked.
& {
    . ([scriptblock]::Create($landingDefinitions['Wait-ViewerLanding']))
    . ([scriptblock]::Create($landingDefinitions['Reset-ScenarioPose']))
    $fixtureLandingState=[pscustomobject]@{tick=[uint64]1000;sample=0;case='settled'}
    $fixtureSleeps=[Collections.Generic.List[int]]::new()
    function Start-Sleep([int]$Milliseconds){$fixtureSleeps.Add($Milliseconds)}
    function Read-FreshRole([string]$Role){
        $fixtureLandingState.tick+=250;$fixtureLandingState.sample++
        $y=if($fixtureLandingState.case -ceq 'falling'){350.0-$fixtureLandingState.sample}else{348.0}
        $state=@{tick_ms=$fixtureLandingState.tick;mission=30010;generation=3;realized=$true;body_valid=$true;pose_valid=$true;
            player_pos=@(10.0,$y,20.0);local_yaw_valid=$true;player_yaw=-60.0}
        if($fixtureLandingState.case -ceq 'wrong-place'){$state.player_pos[0]=30.0}
        if($fixtureLandingState.case -ceq 'natural-slide'){$state.player_pos[0]=11.44}
        if($fixtureLandingState.case -ceq 'vanishing'){$state.pose_valid=$false}
        if($fixtureLandingState.case -ceq 'transition' -and $fixtureLandingState.sample -ge 2){$state.generation=4}
        if($fixtureLandingState.case -ceq 'nonfinite'){$state.player_pos[1]=[double]::NaN}
        return $state
    }
    $settled=Wait-ViewerLanding 'guest' 10.0 20.0
    Assert-True ($settled.player_pos[1] -eq 348.0 -and $fixtureSleeps.Count -eq 3) 'landing requires three spaced stable observations'
    Assert-True (@($fixtureSleeps|Where-Object{$_ -ne 250}).Count -eq 0) 'landing samples stay250ms apart'
    $fixtureLandingState.case='natural-slide';$fixtureLandingState.sample=0;$fixtureSleeps.Clear()
    $settled=Wait-ViewerLanding 'guest' 10.0 20.0
    Assert-True ($settled.player_pos[0] -eq 11.44 -and $fixtureSleeps.Count -eq 3) 'viewer accepts1.44m natural horizontal landing slide'
    $fixtureLandingState.sample=0;$fixtureSleeps.Clear()
    Assert-Throws {Wait-ViewerLanding 'host' 10.0 20.0 -MaximumHorizontalError 0.75} 'same landing slide cannot satisfy strict benchmark arrival'
    foreach($case in @('falling','wrong-place','vanishing','transition','nonfinite')){
        $fixtureLandingState.case=$case;$fixtureLandingState.sample=0;$fixtureSleeps.Clear()
        Assert-Throws {Wait-ViewerLanding 'guest' 10.0 20.0} ('landing refuses '+$case)
        Assert-True ($fixtureSleeps.Count -le 12) 'landing observation count stays bounded'
    }
    # Restore the telemetry mocks to test the real benchmark formatter and
    # its fresh post-teleport readback, independently of the landing algorithm.
    $poseAnchors=@{};$fixturePosition=@(800.860,339.262,1639.299);$fixtureYaw=-60.0;$fixtureGeneration=3
    $fixtureTeleports=[Collections.Generic.List[string]]::new();$fixtureBenchmarkState=[pscustomobject]@{releases=0};$fixtureBadArrival=$false
    function Read-FreshRole([string]$Role){return @{mission=30010;generation=$fixtureGeneration;
        player_pos=$fixturePosition.Clone();local_yaw_valid=$true;player_yaw=$fixtureYaw}}
    function Release-RoleInput([string]$Role){$fixtureBenchmarkState.releases++}
    function Invoke-DebugResult([string]$Role,[string]$Command,[int]$TimeoutMs){
        $fixtureTeleports.Add($Command);return 'warp_position_readback;error_m=0.003'
    }
    function Wait-ViewerLanding([string]$Role,[double]$ExpectedX,[double]$ExpectedZ,[double]$MaximumHorizontalError=2.0){
        Assert-True ($MaximumHorizontalError -eq 0.75) 'benchmark explicitly keeps strict horizontal arrival tolerance'
        $anchor=$poseAnchors[$Role]
        return @{player_pos=$anchor.position.Clone();local_yaw_valid=$true;player_yaw=$(if($fixtureBadArrival){10.0}else{$anchor.yaw})}
    }
    Reset-ScenarioPose 'host'
    Assert-True ($fixtureTeleports[0] -ceq 'teleport 800.860 339.262 1639.299 -60.000') 'benchmark uses exact existing debug teleport grammar'
    $fixturePosition=@(810.0,345.0,1650.0);$fixtureYaw=80.0
    Reset-ScenarioPose 'host'
    Assert-True ($fixtureTeleports.Count -eq 2 -and $fixtureTeleports[1] -ceq $fixtureTeleports[0]) 'later phase returns to recorded initial pose rather than current drift'
    Assert-True ($fixtureBenchmarkState.releases -eq 2) 'benchmark clears previous simulated input'
    $fixtureBadArrival=$true
    Assert-Throws {Reset-ScenarioPose 'host'} 'benchmark refuses mismatched fresh heading readback'
    $fixtureBadArrival=$false;$fixtureGeneration=4;$beforeCount=$fixtureTeleports.Count
    Assert-Throws {Reset-ScenarioPose 'host'} 'benchmark refuses mission generation change'
    Assert-True ($fixtureTeleports.Count -eq $beforeCount) 'generation refusal happens before teleport submission'
}
$aimDefinitions=@{}
foreach($name in @('Send-AimTurnCommand','Read-AimTurnState','Wait-AimTurnReady','Invoke-AimTurn')){
    $definition=@($scenarioAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
    Assert-True ($definition.Count -eq 1) ('actual aim-turn boundary '+$name)
    $aimDefinitions[$name]=$definition[0].Extent.Text
}
# Recorded time/telemetry and input effects exercise the actual helper bodies.
# Sleep advances a detached clock only; no input writer or controller is called.
& {
    foreach($definition in $aimDefinitions.Values){. ([scriptblock]::Create($definition))}
    function New-AimFixture([string]$Case,[string]$Source='host'){
        return [pscustomobject]@{now=[int64]10000;case=$Case;source=$Source;firstHold=[int64]-1;lastHold=[int64]-1;
            mouseAt=[int64]-1;sequence=[uint64]0;receipts=@{};commands=[Collections.Generic.List[hashtable]]::new();
            captures=[Collections.Generic.List[hashtable]]::new();reads=[Collections.Generic.List[string]]::new()}
    }
    function Get-ScenarioTick{return $aimFixture.now}
    function Start-Sleep([int]$Milliseconds){$aimFixture.now+=$Milliseconds}
    function Send-Step([string]$Role,[hashtable]$Arguments){
        $aimFixture.sequence++
        $nativeAt=$aimFixture.now+$(if($aimFixture.case -ceq 'controller-io'){50}else{0})
        $row=@{role=$Role;sequence=$aimFixture.sequence;tick=$aimFixture.now;accepted_tick_ms=$nativeAt;arguments=$Arguments.Clone()}
        $aimFixture.commands.Add($row)
        if($Arguments.Operation -ceq 'hold'){
            if($aimFixture.firstHold -lt 0){$aimFixture.firstHold=$nativeAt}
            $aimFixture.lastHold=$nativeAt
        }
        if($Arguments.Operation -ceq 'mouse'){$aimFixture.mouseAt=$nativeAt}
        $aimFixture.receipts[[string]$row.sequence]=@{event='command_accepted';tick_ms=$nativeAt}
        if($aimFixture.case -ceq 'controller-io'){$aimFixture.now+=150}
        return @{sequence=$row.sequence}
    }
    function Read-NativeReceipt([string]$Role,[uint64]$Sequence,[string[]]$Events){
        if($aimFixture.case -ceq 'missing-acceptance' -and $aimFixture.firstHold -ge 0){return $null}
        return $aimFixture.receipts[[string]$Sequence]
    }
    function Release-RoleFinally([string]$Role){$null=Send-Step $Role @{Operation='release'}}
    function Capture-Step([string]$Role,[string]$Label,[hashtable]$Hold=$null){
        $aimFixture.captures.Add(@{role=$Role;label=$Label;hold=$Hold;tick=$aimFixture.now})
    }
    function Read-FreshRole([string]$Role){
        $aimFixture.reads.Add($Role)
        $elapsed=if($aimFixture.firstHold -ge 0){$aimFixture.now-$aimFixture.firstHold}else{-1}
        $aimDelay=if($aimFixture.case -ceq 'late-aim'){1800}else{800}
        $flags=if($elapsed -ge $aimDelay -and $aimFixture.now -le $aimFixture.lastHold+1500){1}else{0}
        if($aimFixture.case -ceq 'old-aim'){$flags=1}
        if($aimFixture.case -ceq 'never-aim'){$flags=0}
        if($aimFixture.case -ceq 'busy-aim' -and $flags){$flags=3}
        if($aimFixture.case -ceq 'aim-lost' -and $aimFixture.mouseAt -ge 0){$flags=0}
        $yaw=if($elapsed -lt 500){-60.0}elseif($elapsed -lt 700){20.0}else{30.0}
        if($aimFixture.case -ceq 'unsettled' -and $elapsed -ge 0){$yaw=$elapsed*0.1}
        if($aimFixture.mouseAt -ge 0 -and $aimFixture.now-$aimFixture.mouseAt -ge 150){
            $yaw=if($aimFixture.case -ceq 'small-pivot'){35.0}else{110.0}
        }
        $invalid=$aimFixture.case -ceq 'invalid-yaw' -or
            ($aimFixture.case -ceq 'invalid-post-yaw' -and $aimFixture.mouseAt -ge 0)
        $generation=if($Role -ceq 'host'){3}else{4}
        if($aimFixture.case -ceq 'scene-change' -and $elapsed -ge 600){$generation++}
        $tick=$aimFixture.now-($aimFixture.now%100)
        if($aimFixture.case -ceq 'repeated-tick' -and $Role -ceq $aimFixture.source){$tick=10000}
        if(($aimFixture.case -ceq 'stale-source' -and $Role -ceq $aimFixture.source) -or
            ($aimFixture.case -ceq 'stale-observer' -and $Role -cne $aimFixture.source)){$tick=$aimFixture.now-300}
        return @{mission=30010;generation=$generation;tick_ms=$tick;local_yaw_valid=(!$invalid);
            player_yaw=$(if($invalid){[double]::NaN}else{$yaw});
            # The source's own remote flags deliberately claim aim. Looking at
            # Viewer==Source instead of the independent receiver must not pass.
            remote_action_flags=$(if($Role -ceq $aimFixture.source){1}else{$flags});remote_equip=873;
            remote_age_ms=$(if($aimFixture.case -ceq 'stale-aim'){300}else{50})}
    }
    $aimFixture=New-AimFixture 'pass'
    Invoke-AimTurn 'host' 'host' 700 'fixture-turn'
    Assert-True ($aimFixture.captures.Count -eq 2) 'genuine aim pivot and settled frame captured'
    $turn=$aimFixture.captures[0].hold.turn
    Assert-True ($turn.observer_role -ceq 'guest' -and @($aimFixture.reads|Where-Object{$_ -ceq 'guest'}).Count -gt 3) 'self-viewer still uses opposite source observer'
    Assert-True ($turn.baseline_yaw -eq 30 -and $turn.excursion_degrees -eq 80) 'pre-aim90degree camera alignment excluded from settled baseline'
    Assert-True ($turn.baseline_samples.Count -ge 3 -and $turn.baseline_samples[-1].tick_ms-$turn.baseline_samples[0].tick_ms -ge 250) 'distinct settled baseline samples span250ms'
    Assert-True ($turn.mouse_dispatched_tick_ms -ge $turn.readiness_tick_ms -and $turn.measured_tick_ms -gt $turn.mouse_accepted_tick_ms) 'pivot measurement follows ready baseline and mouse acceptance'
    Assert-True ($turn.latest_hold_sequence -eq $aimFixture.captures[0].hold.sequence) 'capture uses latest accepted hold sequence'
    Assert-True (@($aimFixture.commands|Where-Object{$_.arguments.Operation -ceq 'release'}).Count -eq 2) 'successful aim turn releases before and finally after input'
    $aimFixture=New-AimFixture 'late-aim' 'guest'
    Invoke-AimTurn 'guest' 'guest' -700 'fixture-late'
    $holds=@($aimFixture.commands|Where-Object{$_.arguments.Operation -ceq 'hold'})
    Assert-True ($holds.Count -ge 4 -and $aimFixture.captures[0].hold.turn.observer_role -ceq 'host') 'late genuine aim renews holds and reverses independent observer role'
    for($i=0;$i -lt $holds.Count;$i++){
        Assert-True ($holds[$i].arguments.Keys -ceq 'rmb' -and $holds[$i].arguments.DurationMs -eq 1500) 'renewal retains identical bounded RMB hold'
        if($i){Assert-True ($holds[$i].tick-$holds[$i-1].tick -le 750) 'readiness hold renewal interval bounded750ms'}
    }
    Assert-True (@($aimFixture.commands|Where-Object{$_.arguments.Operation -ceq 'release'}).Count -eq 2) 'renewal never releases between readiness holds'
    $aimFixture=New-AimFixture 'controller-io'
    Invoke-AimTurn 'host' 'guest' 700 'fixture-controller-io'
    $holds=@($aimFixture.commands|Where-Object{$_.arguments.Operation -ceq 'hold'})
    Assert-True ($aimFixture.captures.Count -eq 2 -and $holds.Count -ge 3) 'readiness and pivot tolerate150ms controller I/O per command'
    for($i=1;$i -lt $holds.Count;$i++){
        Assert-True ($holds[$i].accepted_tick_ms-$holds[$i-1].accepted_tick_ms -le 750) 'controller I/O still leaves each native renewal within750ms'
    }
    foreach($caseName in @('old-aim','never-aim','busy-aim','stale-aim','stale-source','stale-observer','unsettled','repeated-tick','invalid-yaw',
        'invalid-post-yaw','scene-change','aim-lost','small-pivot','missing-acceptance')){
        $aimFixture=New-AimFixture $caseName
        Assert-Throws {Invoke-AimTurn 'host' 'host' 700 'fixture-refused'} ('aim timeline refuses '+$caseName)
        Assert-True ($aimFixture.captures.Count -eq 0) ('refused timeline cannot create misleading pivot frame '+$caseName)
        Assert-True (@($aimFixture.commands|Where-Object{$_.arguments.Operation -ceq 'release'}).Count -eq 2) ('failure finally releases '+$caseName)
        Assert-True ($aimFixture.now -le 15000) ('readiness plus pivot deadline bounded '+$caseName)
        if($caseName -notin 'invalid-post-yaw','aim-lost','small-pivot'){
            Assert-True (@($aimFixture.commands|Where-Object{$_.arguments.Operation -ceq 'mouse'}).Count -eq 0) ('unready aim cannot dispatch mouse '+$caseName)
        }
    }
    # Native admission can precede a delayed controller return. Its timestamp,
    # rather than receipt-reader entry time, decides whether the hold was timely.
    $ioFixture=[pscustomobject]@{now=[int64]10000;case='timely';reads=0;sleeps=0}
    function Get-ScenarioTick{return $ioFixture.now}
    function Send-Step([string]$Role,[hashtable]$Arguments){$ioFixture.now+=800;return @{sequence=41}}
    function Read-NativeReceipt([string]$Role,[uint64]$Sequence,[string[]]$Events){
        $ioFixture.reads++
        if($ioFixture.case -ceq 'missing'){return $null}
        return @{event='command_accepted';tick_ms=$(if($ioFixture.case -ceq 'late'){10501}else{10499})}
    }
    function Start-Sleep([int]$Milliseconds){$ioFixture.sleeps++;$ioFixture.now+=$Milliseconds}
    $accepted=Send-AimTurnCommand 'host' @{Operation='hold';Keys='rmb';DurationMs=1500} 10750
    Assert-True ($accepted.accepted_tick_ms -eq 10499 -and $ioFixture.reads -eq 1) 'controller late return still reads and accepts native hold within its500ms bound'
    Assert-True ($ioFixture.sleeps -eq 0) 'timely native acknowledgement after controller return needs no additional wait'
    foreach($caseName in 'late','missing'){
        $ioFixture.now=10000;$ioFixture.case=$caseName;$ioFixture.reads=0;$ioFixture.sleeps=0
        Assert-Throws {Send-AimTurnCommand 'host' @{Operation='hold';Keys='rmb';DurationMs=1500} 10750} ('delayed controller refuses '+$caseName+' native acknowledgement')
        Assert-True ($ioFixture.reads -eq 1 -and $ioFixture.sleeps -eq 0) ('deadline-expired first receipt cannot start another wait '+$caseName)
    }
}
$nonce=[Guid]::NewGuid().ToString('N')
$fixture=Join-Path $RepoRoot ('build\worker-session-tests-'+$nonce)
$marker=Join-Path $fixture '.fixture-owner'
$script:packageCalls=0;$script:processCalls=0;$script:fakeActiveGame='';$script:fixtureSession=$null
function Get-GameProcessInDirectory([string]$Directory) {
    ++$script:processCalls
    if($Directory -ieq $script:fakeActiveGame){[pscustomobject]@{pid=731;fixture_only=$true}}
}
function Invoke-WorkerPackageRestore([string]$Repo,[string]$Game,[string]$Package,[string]$Role) {
    ++$script:packageCalls
    # No installer/controller/process is called. Simulate its owned rollback.
    $row=$script:fixtureSession.roles[$Role]
    Assert-True ($Game -ceq $row.game -and $Package -ceq $row.baseline_package) 'fake installer receives exact captured role paths'
    $p=Get-InstancePaths -GameDirectory $Game
    [IO.File]::Copy((Join-Path $Package 'payload\dinput8.dll'),$p.Loader,$true)
    [IO.File]::Copy($row.baseline_record,$p.Record,$true)
}
function New-WorkerCleanupFixture([string]$Name) {
    $run=Join-Path $fixture $Name
    $null=New-Item -ItemType Directory -Path $run
    $baselineHash=$null;$candidateHash=$null;$pins=$null
    $session=[ordered]@{roles=[ordered]@{};prepare_started=$true;prepare_completed=$false;baseline_sha256=$null;candidate_sha256=$null}
    foreach($role in @('host','guest')) {
        $game=Join-Path $run ($role+'-game');$backup=Join-Path $run ($role+'-baseline')
        $null=New-Item -ItemType Directory -Path (Join-Path $game 'mgs-coop'),(Join-Path $backup 'package\payload')
        $p=Get-InstancePaths -GameDirectory $game
        [IO.File]::WriteAllText($p.Exe,"fixture retail executable`n")
        [IO.File]::WriteAllText($p.SteamApi,"fixture Steam library`n")
        [IO.File]::WriteAllText($p.Archive,"fixture baseline archive`n")
        $pins=[ordered]@{Exe=Get-WorkerHash $p.Exe;Steam=Get-WorkerHash $p.SteamApi;Archive=Get-WorkerHash $p.Archive}
        [IO.File]::WriteAllText($p.Loader,"fixture normal loader`n")
        $baselineHash=Get-WorkerHash $p.Loader
        [IO.File]::Copy($p.Loader,(Join-Path $backup 'package\payload\dinput8.dll'),$false)
        [IO.File]::WriteAllText((Join-Path $backup 'package\package.json'),('{"fixture":true}'+"`n"))
        Write-WorkerSessionJson $p.Record ([ordered]@{product='mgs-coop-original-loader-probe';game_directory=$game;dll_sha256=$baselineHash;fixture=$true})
        $recordCopy=Join-Path $backup 'probe-install.json'
        [IO.File]::Copy($p.Record,$recordCopy,$false)
        $snapshots=@()
        # Guest generated baseline plus historical user backup: generic Disable
        # would select the unrelated historical bytes on partial preparation.
        [IO.File]::WriteAllText($p.LabIni,"# Generated by tools/lab/Prepare-LabInstance.ps1`nenabled=0`nrole=$role`n")
        [IO.File]::WriteAllText($p.CoopIni,"baseline coop $role`n")
        [IO.File]::WriteAllText($p.LinkSecret,"baseline secret $role`n")
        if($role -ceq 'guest') {
            [IO.File]::WriteAllText($p.ClientHost,"baseline client host`n")
            [IO.File]::WriteAllText(($p.LabIni+'.user-20260101-000000'),"historical unrelated user INI`n")
        }
        foreach($key in @('LabIni','CoopIni','Marker','ClientHost','LinkSecret')) {
            $snapshot=Get-WorkerConfigSnapshot $p.$key $backup $key
            if($role -ceq 'host' -and $key -in @('LabIni','Marker')) {
                [IO.File]::WriteAllText($p.$key,"fixture generated worker $key`n")
                $snapshot.worker_sha256=Get-WorkerHash $p.$key
            }
            $snapshots+=@($snapshot)
        }
        # Prepare without LAN removes old secrets. This exact absence may be
        # restored from this run's backup without generic Disable side effects.
        Remove-Item -LiteralPath $p.LinkSecret
        $session.roles[$role]=[ordered]@{game=$game;baseline_package=Join-Path $backup 'package';baseline_package_sha256=Get-WorkerHash (Join-Path $backup 'package\package.json');baseline_record=$recordCopy;baseline_record_sha256=Get-WorkerHash $recordCopy;configs=$snapshots}
        if($role -ceq 'host') {
            [IO.File]::WriteAllText($p.Loader,"fixture worker loader`n")
            $candidateHash=Get-WorkerHash $p.Loader
            Write-WorkerSessionJson $p.Record ([ordered]@{product='mgs-coop-original-loader-probe';game_directory=$game;dll_sha256=$candidateHash;fixture=$true})
        }
    }
    $session.baseline_sha256=$baselineHash;$session.candidate_sha256=$candidateHash
    return [pscustomobject]@{run=$run;session=$session;pins=$pins;report=[ordered]@{roles=[ordered]@{}}}
}
try {
    $null=New-Item -ItemType Directory -Path $fixture
    [IO.File]::WriteAllText($marker,$nonce)
    # Native receipts use a persistent append writer that shares READ only.
    # The real failed run wrote this frame, but ReadAllLines could not open its
    # log while that writer was alive. Reproduce the OS sharing contract using
    # actual receipt-shaped bytes and the production reader, without a game.
    $run=Join-Path $fixture 'native-receipt-sharing'
    $runId='c646217606fd4b2aae1d4860ef235bcc'
    $null=New-Item -ItemType Directory -Path $run
    $receiptPath=Join-Path $run 'host.worker.jsonl'
    $frameLine='{"run_id":"c646217606fd4b2aae1d4860ef235bcc","role":"host","pid":61704,"event":"frame_written","reason":"dx11_backbuffer_bmp","sequence":25,"tick_ms":477756593,"thread":67116,"api_result":0,"width":1280,"height":720,"requested_flags":0,"forwarded_flags":0,"gameplay_accepted":null}'
    $writer=$null
    try {
        $writer=[IO.FileStream]::new($receiptPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
        $bytes=[Text.Encoding]::UTF8.GetBytes($frameLine+[char]10)
        $writer.Write($bytes,0,$bytes.Length);$writer.Flush()
        Assert-Throws {[IO.File]::ReadAllLines($receiptPath)} 'old receipt open refuses while native-style writer remains open'
        $frame=Read-NativeReceipt 'host' 25 @('frame_written')
        Assert-True ($writer.CanWrite -and $frame -and $frame['tick_ms'] -eq 477756593 -and $frame['width'] -eq 1280) 'actual reader finds exact frame while writer is still open'
        Assert-True ($null -eq (Read-NativeReceipt 'host' 26 @('frame_written'))) 'different sequence cannot satisfy capture'
        Assert-True ($null -eq (Read-NativeReceipt 'host' 25 @('command_refused'))) 'different event cannot satisfy capture'
        $runId=$id
        Assert-True ($null -eq (Read-NativeReceipt 'host' 25 @('frame_written'))) 'foreign run cannot satisfy capture'
        $runId='c646217606fd4b2aae1d4860ef235bcc'
        Assert-True ($null -eq (Read-NativeReceipt 'guest' 25 @('frame_written'))) 'missing other-role log does not adopt host frame'
        $partial=$frameLine.Replace('"sequence":25','"sequence":26')
        $bytes=[Text.Encoding]::UTF8.GetBytes($partial)
        $writer.Write($bytes,0,$bytes.Length);$writer.Flush()
        Assert-True ($null -eq (Read-NativeReceipt 'host' 26 @('frame_written'))) 'unterminated append tail is not a completed receipt'
        Assert-True ((Read-NativeReceipt 'host' 25 @('frame_written'))['tick_ms'] -eq 477756593) 'partial tail preserves earlier complete frame'
        $writer.WriteByte(10);$writer.Flush()
        Assert-True ((Read-NativeReceipt 'host' 26 @('frame_written'))['tick_ms'] -eq 477756593) 'completed append becomes readable without closing writer'
        $refused=$frameLine.Replace('"event":"frame_written"','"event":"command_refused"').Replace('"sequence":25','"sequence":27')
        $bytes=[Text.Encoding]::UTF8.GetBytes($refused+[char]10+'null'+[char]10+'[]'+[char]10+'{invalid'+[char]10)
        $writer.Write($bytes,0,$bytes.Length);$writer.Flush()
        Assert-True ((Read-NativeReceipt 'host' 27 @('frame_written','command_refused'))['event'] -ceq 'command_refused') 'exact rejection is readable through malformed trailing rows'
        $writer.SetLength(8388609);$writer.Flush()
        Assert-True ($null -eq (Read-NativeReceipt 'host' 25 @('frame_written'))) 'oversized receipt log refuses before parsing'
        $writer.SetLength(0);$writer.Position=0
        $bytes=[Text.Encoding]::UTF8.GetBytes(([string]::new([char]10,16384))+$frameLine+[char]10)
        $writer.Write($bytes,0,$bytes.Length);$writer.Flush()
        Assert-True ($null -eq (Read-NativeReceipt 'host' 25 @('frame_written'))) 'excessive receipt row count refuses before parsing'
    } finally {if($writer){$writer.Dispose()}}
    # Run the actual Start backup/seed assignments on fake source/save bytes.
    # This catches stale lab-copy reuse while exercising the real copier/hash
    # implementation; the Start controller itself is never invoked.
    $runDirectory=Join-Path $fixture 'fresh-seed'
    $source=Join-Path $fixture 'fake-userdata'
    $stale=Join-Path $fixture 'stale-game\mgs-coop\lab\saves\287700\local'
    $null=New-Item -ItemType Directory -Path $runDirectory,(Join-Path $source '287700\local'),(Join-Path $source '311340\remote'),$stale -Force
    [IO.File]::WriteAllText((Join-Path $source '287700\local\TPP_GAME_DATA'),"current fake real local progress`n")
    [IO.File]::WriteAllText((Join-Path $source '287700\local\TPP_GRAPHICS_CONFIG'),"current fake real graphics bytes`n")
    [IO.File]::WriteAllText((Join-Path $source '311340\remote\TPP_GAME_DATA'),"current fake real remote progress`n")
    [IO.File]::WriteAllText((Join-Path $stale 'TPP_GAME_DATA'),"stale lab progress must be preserved`n")
    $staleHash=Get-WorkerHash (Join-Path $stale 'TPP_GAME_DATA')
    $identity=[pscustomobject]@{UserData=$source}
    Assert-WorkerRealSaveSource $identity.UserData
    # Get-RealSaveManifest preserves New-HashManifest's wrapped row-array.
    # Assign it first, then enumerate its rows instead of wrapping the whole
    # result again. Every path/hash remains part of the comparison.
    $sourceRows=Get-RealSaveManifest -UserData $source
    $before=@($sourceRows|ForEach-Object {$_})
    Assert-True ($before.Count -eq 3 -and @($before|Where-Object {$_.path -isnot [string] -or $_.sha256 -isnot [string]}).Count -eq 0) 'source manifest retains three exact file rows'
    . ([scriptblock]::Create($startAssignments.freshBackup))
    . ([scriptblock]::Create($startAssignments.manifestBackup))
    Assert-True (Test-WorkerDescendant $freshBackup $runDirectory) 'actual Start creates this run fresh backup'
    Assert-True ((Compare-HashManifest -Expected $before -Actual @($manifestBackup.verified)).Same) 'backup receipt describes current source progress'
    foreach($role in @('host','guest')) {
        . ([scriptblock]::Create($startAssignments.saveRoot))
        Assert-True (!(Test-Path -LiteralPath $saveRoot)) 'each actual role seed path is initially absent'
        . ([scriptblock]::Create(@($seedDefinitions|Where-Object{$_ -clike '*Copy-SaveSeed*'})[0]))
        . ([scriptblock]::Create($startAssignments.graphics))
        Assert-True ($seed.Seeded -and $seed.Files -eq 3 -and (Test-WorkerDescendant $saveRoot $runDirectory)) 'actual role seed copies exactly the fresh backup files'
        Assert-True ((Get-WorkerHash (Join-Path $saveRoot '287700\local\TPP_GAME_DATA')) -ceq (Get-WorkerHash (Join-Path $source '287700\local\TPP_GAME_DATA'))) 'each role begins with current local progress'
        Assert-True ((Get-WorkerHash (Join-Path $saveRoot 'remote\TPP_GAME_DATA')) -ceq (Get-WorkerHash (Join-Path $source '311340\remote\TPP_GAME_DATA'))) 'each role begins with current remote progress'
        Assert-True ((Get-WorkerHash $graphics) -ceq (Get-WorkerHash (Join-Path $source '287700\local\TPP_GRAPHICS_CONFIG'))) 'graphics bytes copied into each fresh role directory'
    }
    [IO.File]::WriteAllText((Join-Path $runDirectory 'host-saves\287700\local\TPP_GAME_DATA'),"host copy may progress independently`n")
    Assert-True ((Get-WorkerHash (Join-Path $runDirectory 'guest-saves\287700\local\TPP_GAME_DATA')) -ceq (Get-WorkerHash (Join-Path $source '287700\local\TPP_GAME_DATA'))) 'fresh role copies have independent files'
    $realSaves=[ordered]@{root=$source;before=$before}
    Assert-True ((Get-WorkerRealSaveComparison $realSaves).Same) 'actual Stop comparison accepts unchanged three-file source manifest'
    [IO.File]::WriteAllText((Join-Path $source '287700\local\TPP_GAME_DATA'),"changed fake source must be detected`n")
    $changed=Get-WorkerRealSaveComparison $realSaves
    Assert-True (!$changed.Same -and $changed.Changed.Count -eq 1 -and $changed.Changed[0] -ceq '287700/local/TPP_GAME_DATA') 'actual Stop comparison detects exact changed real-source file'
    [IO.File]::WriteAllText((Join-Path $source '287700\local\TPP_GAME_DATA'),"current fake real local progress`n")
    Assert-True ((Get-WorkerRealSaveComparison $realSaves).Same) 'source fixture returns to its original exact manifest'
    Assert-True ((Get-WorkerHash (Join-Path $stale 'TPP_GAME_DATA')) -ceq $staleHash) 'existing lab progress remains untouched'

    $case=New-WorkerCleanupFixture 'partial-prepare'
    $script:fixtureSession=$case.session;$script:packageCalls=0;$script:processCalls=0
    Invoke-WorkerRestoreOwnedSession $case.session $case.run $RepoRoot $case.pins $case.report
    Assert-True ($script:packageCalls -eq 1 -and $script:processCalls -eq 2) 'partial Prepare restores only the upgraded host and checks both fake process contexts'
    foreach($role in @('host','guest')) {
        $p=Get-InstancePaths -GameDirectory $case.session.roles[$role].game
        Assert-True ((Get-WorkerHash $p.Loader) -ceq $case.session.baseline_sha256) 'exact baseline loader restored'
        Assert-True ((Get-WorkerHash $p.Record) -ceq $case.session.roles[$role].baseline_record_sha256) 'exact baseline ownership record restored'
        Assert-True ($case.report.roles[$role].loader_restored -and $case.report.roles[$role].configuration_restored) 'role restoration report reflects completed actual byte restoration'
        foreach($snapshot in $case.session.roles[$role].configs) {
            if($snapshot.present){Assert-True ((Get-WorkerHash $snapshot.path) -ceq $snapshot.sha256) 'each original configuration byte hash restored'}
            else{Assert-True (!(Test-Path -LiteralPath $snapshot.path)) 'originally absent worker configuration removed'}
        }
    }
    $guest=Get-InstancePaths -GameDirectory $case.session.roles.guest.game
    Assert-True ([IO.File]::ReadAllText(($guest.LabIni+'.user-20260101-000000')) -ceq "historical unrelated user INI`n") 'historical user backup is untouched and never selected'

    # A changed config in the SECOND role must stop the whole restoration before
    # the FIRST role installer/config mutations. Unknown secret bytes survive.
    $case=New-WorkerCleanupFixture 'changed-secret'
    $script:fixtureSession=$case.session;$script:packageCalls=0
    $fixtureHostPaths=Get-InstancePaths -GameDirectory $case.session.roles.host.game
    $guest=Get-InstancePaths -GameDirectory $case.session.roles.guest.game
    [IO.File]::WriteAllText($guest.LinkSecret,"unknown changed secret must survive`n")
    $before=@{loader=Get-WorkerHash $fixtureHostPaths.Loader;record=Get-WorkerHash $fixtureHostPaths.Record;lab=Get-WorkerHash $fixtureHostPaths.LabIni;secret=Get-WorkerHash $guest.LinkSecret}
    Assert-Throws {Invoke-WorkerRestoreOwnedSession $case.session $case.run $RepoRoot $case.pins $case.report} 'unknown changed LinkSecret refuses before all restoration'
    Assert-True ($script:packageCalls -eq 0 -and $case.report.roles.Count -eq 0) 'all-role config preflight precedes package restoration'
    Assert-True ((Get-WorkerHash $fixtureHostPaths.Loader) -ceq $before.loader -and (Get-WorkerHash $fixtureHostPaths.Record) -ceq $before.record -and (Get-WorkerHash $fixtureHostPaths.LabIni) -ceq $before.lab) 'earlier role bytes remain unchanged on later-role refusal'
    Assert-True ((Get-WorkerHash $guest.LinkSecret) -ceq $before.secret) 'unknown secret is neither deleted nor overwritten'

    $case=New-WorkerCleanupFixture 'bad-backup'
    $script:fixtureSession=$case.session;$script:packageCalls=0
    $snapshot=@($case.session.roles.guest.configs|Where-Object {$_.path.EndsWith('coop.ini')})[0]
    [IO.File]::WriteAllText($snapshot.backup,"corrupted original backup`n")
    Assert-Throws {Invoke-WorkerRestoreOwnedSession $case.session $case.run $RepoRoot $case.pins $case.report} 'bad config backup refuses before installer mutation'
    Assert-True ($script:packageCalls -eq 0) 'all snapshot backup hashes preflight even if current target is baseline'

    $case=New-WorkerCleanupFixture 'live-game'
    $script:fixtureSession=$case.session;$script:packageCalls=0;$script:fakeActiveGame=$case.session.roles.host.game
    Assert-Throws {Invoke-WorkerRestoreOwnedSession $case.session $case.run $RepoRoot $case.pins $case.report} 'fake live owned game blocks restoration'
    Assert-True ($script:packageCalls -eq 0 -and $case.report.roles.Count -eq 0) 'process-context refusal cannot restore package or configs'
} finally {
    $script:fakeActiveGame=''
    if(Test-Path -LiteralPath $fixture) {
        $resolved=(Resolve-Path -LiteralPath $fixture).Path
        if([IO.Path]::GetDirectoryName($resolved) -ine (Join-Path $RepoRoot 'build') -or
            [IO.Path]::GetFileName($resolved) -cne ('worker-session-tests-'+$nonce) -or
            !(Test-Path -LiteralPath $marker -PathType Leaf) -or [IO.File]::ReadAllText($marker) -cne $nonce){throw 'Fixture cleanup ownership differs.'}
        foreach($item in @((Get-Item -LiteralPath $resolved))+@(Get-ChildItem -LiteralPath $resolved -Recurse -Force)) {
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Fixture refuses linked cleanup.'}
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
Write-Output ("worker_session_tests: $script:checks detached checks passed; no worker/game/process/window operations")
