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
# Exact candidate policy is exercised separately from file rollback mocks.
$pinned=Get-WorkerRuntimePins;$capacityPins=Get-WorkerBuddyCapacityPins
Assert-True ($pinned.Archive -ceq '87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96') 'production runtime pin unchanged'
$normalSession=@{candidate_sha256='B'*64}
$normalCatalog=Get-WorkerSessionRuntimeCatalog $normalSession $pinned
Assert-True (!$normalCatalog.Experimental -and $normalCatalog.Archive -ceq $pinned.Archive -and $normalCatalog.Bytes -eq 16333824) 'old baseline session unchanged'
$capacitySession=@{mode='lab';candidate_sha256='B'*64;runtime_profile=$capacityPins.Profile;runtime_archive_sha256=$capacityPins.Archive;
    runtime_archive_bytes=$capacityPins.Bytes;runtime_motion_count=21;runtime_buddy_capacity=2;runtime_catalog_sha256=$capacityPins.Catalog}
$capacityCatalog=Get-WorkerSessionRuntimeCatalog $capacitySession $pinned
Assert-True ($capacityPins.Archive -ceq '4B290CDDC14F1146DCE5A64A1008365D572B424B319874EF77E9496634D75EC9' -and $capacityPins.Bytes -eq 17501184 -and $capacityPins.Catalog -ceq '4A7E7EA890ADF8F261368CD546CCFD3DBFDC4407EF6AF44AFF308B98E53D0989') 'pinned seven-horse capacity archive'
Assert-True ($capacityCatalog.Experimental -and $capacityCatalog.Archive -ceq $capacityPins.Archive -and $capacityCatalog.Bytes -eq 17501184 -and $capacityCatalog.Capacity -eq 2) 'exact capacity catalog readback'
foreach($key in @('runtime_profile','runtime_archive_sha256','runtime_archive_bytes','runtime_motion_count','runtime_buddy_capacity','runtime_catalog_sha256')){
    $changed=$capacitySession.Clone();$changed.Remove($key)
    Assert-Throws {Get-WorkerSessionRuntimeCatalog $changed $pinned} ('missing candidate field '+$key)
}
foreach($case in @(@('runtime_profile','buddy_capacity_two_v2'),@('runtime_archive_sha256',('A'*64)),@('runtime_archive_bytes',16333824),@('runtime_archive_bytes',16659456),
    @('runtime_motion_count',31),@('runtime_buddy_capacity',1),@('runtime_catalog_sha256',('A'*64)),@('mode','play'))){
    $changed=$capacitySession.Clone();$changed[$case[0]]=$case[1]
    Assert-Throws {Get-WorkerSessionRuntimeCatalog $changed $pinned} ('changed candidate field '+$case[0])
}
Assert-True ((Get-WorkerRestorePair $capacitySession $pinned ('A'*64) ('A'*64) $pinned.Archive) -ceq 'baseline') 'candidate session unprepared role admits original pair'
Assert-True ((Get-WorkerRestorePair $capacitySession $pinned ('A'*64) ('B'*64) $capacityPins.Archive) -ceq 'worker') 'candidate session prepared role admits exact candidate pair'
Assert-True ((Get-WorkerRestorePair $normalSession $pinned ('A'*64) ('B'*64) $pinned.Archive) -ceq 'worker') 'normal worker pair unchanged'
foreach($case in @(@(('A'*64),$capacityPins.Archive),@(('B'*64),$pinned.Archive),@(('C'*64),$capacityPins.Archive),@(('B'*64),('C'*64)))){
    Assert-Throws {Get-WorkerRestorePair $capacitySession $pinned ('A'*64) $case[0] $case[1]} 'candidate rejects mixed/foreign pair'
}
$restPins=Get-WorkerMgoSleepRestPins
$restSession=@{mode='lab';candidate_sha256='B'*64;runtime_profile=$restPins.Profile;runtime_archive_sha256=$restPins.Archive;
    runtime_archive_bytes=$restPins.Bytes;runtime_motion_count=21;runtime_buddy_capacity=0;runtime_catalog_sha256=$restPins.Catalog;
    runtime_build_profile='research';runtime_native_stage='gun'}
$restCatalog=Get-WorkerSessionRuntimeCatalog $restSession $pinned
Assert-True ($restCatalog.Experimental -and $restCatalog.Bytes -eq 26949632 -and $restCatalog.Capacity -eq 0 -and $restCatalog.Motions -eq 21) 'MGO rest original21 catalog readback'
foreach($key in @('runtime_profile','runtime_archive_sha256','runtime_archive_bytes','runtime_motion_count','runtime_buddy_capacity','runtime_catalog_sha256','runtime_build_profile','runtime_native_stage')){
    $changed=$restSession.Clone();$changed.Remove($key)
    Assert-Throws {Get-WorkerSessionRuntimeCatalog $changed $pinned} ('MGO rest missing field '+$key)
}
foreach($case in @(@('runtime_profile','mgo_sleep_rest_v2'),@('runtime_archive_sha256',$pinned.Archive),@('runtime_archive_bytes',16333824),
    @('runtime_motion_count',31),@('runtime_motion_count','021'),@('runtime_motion_count',21.4),@('runtime_archive_bytes',26949632.4),
    @('runtime_buddy_capacity',2),@('runtime_buddy_capacity',0.4),@('runtime_buddy_capacity','00'),
    @('runtime_catalog_sha256',('A'*64)),@('runtime_build_profile','normal'),@('runtime_native_stage','loco'),@('mode','play'))){
    $changed=$restSession.Clone();$changed[$case[0]]=$case[1]
    Assert-Throws {Get-WorkerSessionRuntimeCatalog $changed $pinned} ('MGO rest changed field '+$case[0])
}
Assert-True ((Get-WorkerRestorePair $restSession $pinned ('A'*64) ('A'*64) $pinned.Archive) -ceq 'baseline') 'MGO rest unprepared role restores original pair'
Assert-True ((Get-WorkerRestorePair $restSession $pinned ('A'*64) ('B'*64) $restPins.Archive) -ceq 'worker') 'MGO rest prepared role restores exact candidate pair'
foreach($case in @(@(('A'*64),$restPins.Archive),@(('B'*64),$pinned.Archive),@(('C'*64),$restPins.Archive),@(('B'*64),$capacityPins.Archive))){
    Assert-Throws {Get-WorkerRestorePair $restSession $pinned ('A'*64) $case[0] $case[1]} 'MGO rest rejects mixed/foreign pair'
}
Assert-Throws {Get-WorkerRestorePair $capacitySession $pinned ('A'*64) ('B'*64) $restPins.Archive} 'capacity session cannot restore MGO pair'
Assert-Throws {Get-WorkerRestorePair $normalSession $pinned ('A'*64) ('B'*64) $restPins.Archive} 'normal session cannot restore MGO pair'
$orphan=$normalSession.Clone();$orphan.runtime_build_profile='research';Assert-Throws {Get-WorkerSessionRuntimeCatalog $orphan $pinned} 'MGO scope without profile refused'
Assert-WorkerMgoSleepRestScope 'mgo-sleep-rest' 'lab' 'native' 'gun' $false 'D:\fixture\rest.dat' ''
Assert-True $true 'MGO rest explicit lab gun scope admitted'
foreach($case in @(@('normal','lab','native','gun',$false,'D:\rest.dat',''),@('research','lab','native','gun',$false,'D:\rest.dat',''),
    @('mgo-sleep-rest','play','native','gun',$false,'D:\rest.dat',''),@('mgo-sleep-rest','lab','animated','gun',$false,'D:\rest.dat',''),
    @('mgo-sleep-rest','lab','native','loco',$false,'D:\rest.dat',''),@('mgo-sleep-rest','lab','native','gun',$true,'D:\rest.dat',''),
    @('mgo-sleep-rest','lab','native','gun',$false,'',''),@('mgo-sleep-rest','lab','native','gun',$false,'D:\rest.dat','D:\buddy.dat'))){
    Assert-Throws {Assert-WorkerMgoSleepRestScope $case[0] $case[1] $case[2] $case[3] $case[4] $case[5] $case[6]} 'MGO rest scope refuses unsupported mode/profile/stage/source'
}
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
# The flags exactly as Invoke-WorkerChecks writes them (Get-WorkerExperiments), so a new experiment there cannot silently break Normal admission.
$checkSource=[IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\worker\Invoke-WorkerChecks.ps1'))
$checkedExperiments=@([regex]::Matches([regex]::Match($checkSource,'(?s)function Get-WorkerExperiments \{.*?\n\}').Value,"'(MGS_LOCAL_[A-Z0-9_]+)'")|ForEach-Object{$_.Groups[1].Value})
Assert-True ($checkedExperiments.Count -ge 18 -and $checkedExperiments -ccontains 'MGS_LOCAL_UPPER_FIRE_ZERO_BLEND' -and $checkedExperiments -ccontains 'MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER') 'worker check experiments parsed'
foreach($name in $checkedExperiments){$flags[$name]='OFF'}
$flags.MGS_BACKGROUND_WORKER='ON'
$receipt=@{schema=1;status='passed';plan=@{profile='Normal';project='C:\repo';build_directory='C:\repo\build\worker';flags=$flags};artifacts=@{loader_sha256=$hash}}
Assert-True (Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' $hash) 'normal21 worker build checks'
$receipt.plan.profile='Research';Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' $hash)) 'reject research profile';$receipt.plan.profile='Normal'
foreach($name in @($flags.Keys|Where-Object {$_ -cne 'MGS_BACKGROUND_WORKER'})){$flags[$name]='ON';Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' $hash)) 'reject every experiment';$flags[$name]='OFF'}
$capacityFlags=@{};foreach($flag in $flags.Keys){$capacityFlags[$flag]=$flags[$flag]};$capacityFlags.MGS_LOCAL_BUDDY_CAPACITY_TEST='ON'
$capacityReceipt=@{schema=1;status='passed';plan=@{profile='Research';project='C:\repo';build_directory='C:\repo\build\buddy-capacity';flags=$capacityFlags};artifacts=@{loader_sha256=$hash}}
Assert-True (Test-WorkerResearchReceipt $capacityReceipt 'C:\repo\build\buddy-capacity' 'C:\repo' $hash @('MGS_LOCAL_BUDDY_CAPACITY_TEST')) 'capacity receipt enables only exact flag'
Assert-True (!(Test-WorkerCheckReceipt $capacityReceipt 'C:\repo\build\buddy-capacity' 'C:\repo' $hash)) 'capacity never admits normal receipt'
$capacityFlags.MGS_LOCAL_STOCK46_WRITER_TEST='ON'
Assert-True (!(Test-WorkerResearchReceipt $capacityReceipt 'C:\repo\build\buddy-capacity' 'C:\repo' $hash @('MGS_LOCAL_BUDDY_CAPACITY_TEST'))) 'capacity start refuses unaudited extra weapon flag'
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
    gun=@('partner_input_defer=on','partner_input_mask=0x224005E','partner_native_gun=1','partner_native_fire=visual','partner_native_aim=look',
        'partner_input_triggers=1','partner_input_rstick=1','partner_native_rates=mirror','partner_native_aimpt=point','partner_native_reload=event',
        'partner_native_slot=write','partner_native_equip=substitute')
}
# The -LabSetting allow-lists (Invoke-WorkerScenario -LabSetting and its scenario "labsetting" lines,
# Start-WorkerSession -LabSetting) are identical and take the reload/switch A/B keys.
$scenarioText=[IO.File]::ReadAllText((Join-Path $SourceDirectory 'Invoke-WorkerScenario.ps1'))
$sessionText=[IO.File]::ReadAllText((Join-Path $SourceDirectory 'Start-WorkerSession.ps1'))
$patternOf={param($text)$m=[regex]::Match($text,"\[ValidatePattern\('(\^\(\(host\|guest\):\)\?[^']+)'\)\]\[string\[\]\]\`$LabSetting");if($m.Success){$m.Groups[1].Value}else{''}}
$scenarioPattern=& $patternOf $scenarioText
$sessionPattern=& $patternOf $sessionText
Assert-True ($scenarioPattern -and $scenarioPattern -ceq $sessionPattern) 'the two -LabSetting allow-lists are identical'
foreach($ok in @('partner_native_reload=mirror','partner_native_reload=raw','partner_native_reload=event','partner_native_slot=off',
    'partner_native_slot=write','partner_native_slot=call','guest:partner_native_reload=event','host:partner_native_slot=write',
    'partner_native_equip=stock46','partner_native_equip=manifest','partner_native_equip=substitute','guest:partner_native_equip=manifest',
    'partner_native_face=aim','partner_native_aim=looksrc',
    # every track's keys share the one allow-list (aim parity, online play, Steam join, weapons)
    'partner_native_aimpt=look','host:partner_native_aimpt=point','partner_native_net=online','partner_input_edge_repeats=5',
    'steam_selftest_fn=1','coop_hud=compact',
    # the seated vehicle rider (docs/anim-round1-seat.md)
    'partner_native_ride=seat','guest:partner_ride_seat_enter=getin','partner_ride_seat_exit=getoff','partner_native_ride=hide',
    # anim round 1 weapons (docs/anim-round1-weapons.md)
    'partner_fire_census=1','partner_native_aim_intent=pad','guest:partner_native_reload_rate=adapt','partner_native_unsupported=0',
    'host:partner_native_dry_stop=1','partner_native_aim_intent=state','partner_native_reload_rate=off',
    'partner_native_support=1','partner_native_support=0','guest:partner_native_support=1','host:partner_native_support=0',
    # WS3 vehicles (docs/vehicle-ws3.md): the ride census, seat by id and tilt keys
    'partner_ride_census=1','partner_ride_census=0','guest:partner_ride_seat_id=1','partner_ride_seat_id=0','host:partner_ride_tilt=1',
    'partner_ride_tilt=0',
    # WS4 step 0: the read-only placed / arm census
    'partner_placed_census=1','partner_placed_census=0','guest:partner_placed_census=1','host:partner_placed_census=0',
    # WS4 v1: native C4 placing and arm holds
    'partner_native_placed=1','partner_native_placed=0','guest:partner_native_placed=1','host:partner_native_placed=0',
    'partner_native_arm=1','partner_native_arm=0','guest:partner_native_arm=1','host:partner_native_arm=0',
    # WS4 v2: the C4 place / detonate mirror
    'partner_native_place_mirror=1','partner_native_place_mirror=0','guest:partner_native_place_mirror=1','host:partner_native_place_mirror=0',
    # Traversal replay (lab only, default 0)
    'partner_native_traverse=1','partner_native_traverse=0','guest:partner_native_traverse=1','host:partner_native_traverse=0',
    # F1 vehicles: the board block / back-seat passenger hook (lab only, default off)
    'partner_ride_board=off','partner_ride_board=block','partner_ride_board=passenger','guest:partner_ride_board=passenger','host:partner_ride_board=block',
    # F2: the native cardboard box on body 1 (lab only, default 0)
    'partner_native_cbox=1','partner_native_cbox=0','guest:partner_native_cbox=1','host:partner_native_cbox=0',
    # Gestures (on with the gun profile, lab and play)
    'partner_native_gesture=1','partner_native_gesture=0','guest:partner_native_gesture=1','host:partner_native_gesture=0',
    # Horse / D-Walker riders (lab only, default off)
    'partner_native_mount=hide','partner_native_mount=off','guest:partner_native_mount=hide','host:partner_native_mount=off',
    # join-fix phase 2 (P2-19): the save handoff and the travel lock
    'save_handoff=off','save_handoff=stage','host:save_handoff=stage','guest:save_handoff=scene','save_handoff=always','travel_lock=0','travel_lock=1','guest:travel_lock=1')){
    Assert-True ($ok -cmatch $scenarioPattern) ('-LabSetting admits '+$ok)
}
foreach($bad in @('partner_native_reload=tap','partner_native_slot=on','partner_native_reload=','partner_native_equip=any','partner_native_equip=','partner_native_gun=0',
    'partner_native_ride=drive','partner_ride_seat_exit=jump','partner_ride_seat_enter=',
    'partner_fire_census=2','partner_native_aim_intent=on','partner_native_reload_rate=fast','partner_native_unsupported=yes',
    'partner_native_support=2','partner_native_support=','partner_native_support=yes',
    'partner_ride_census=2','partner_ride_seat_id=on','partner_ride_tilt=','partner_ride_tilt=yes','both:partner_ride_census=1',
    'partner_placed_census=2','partner_placed_census=','partner_placed_census=on','both:partner_placed_census=1',
    'partner_native_placed=2','partner_native_placed=','partner_native_placed=on','both:partner_native_placed=1',
    'partner_native_arm=2','partner_native_arm=','partner_native_arm=yes','both:partner_native_arm=1','partner_native_arms=1',
    'partner_native_place_mirror=2','partner_native_place_mirror=','partner_native_place_mirror=on','both:partner_native_place_mirror=1',
    'partner_native_traverse=2','partner_native_traverse=','partner_native_traverse=on','both:partner_native_traverse=1','partner_native_traversal=1',
    'partner_ride_board=1','partner_ride_board=','partner_ride_board=on','partner_ride_board=Block','partner_ride_board=passengers','both:partner_ride_board=block',
    'partner_native_cbox=2','partner_native_cbox=','partner_native_cbox=on','both:partner_native_cbox=1','partner_native_box=1',
    'partner_native_gesture=2','partner_native_gesture=','partner_native_gesture=on','both:partner_native_gesture=1','partner_native_gestures=1',
    'partner_native_mount=1','partner_native_mount=','partner_native_mount=seat','both:partner_native_mount=hide','partner_native_mounts=hide',
    'save_handoff=auto','save_handoff=','save_handoff=SCENE','travel_lock=2','travel_lock=on','both:save_handoff=stage')){
    Assert-True ($bad -cnotmatch $scenarioPattern) ('-LabSetting refuses '+$bad)
}
$lineMatch=[regex]::Match($scenarioText,"if\(\`$line -cnotmatch '(\^labsetting[^']+)'\)")
Assert-True ($lineMatch.Success) 'the scenario labsetting regex'
foreach($ok in @('labsetting partner_native_reload=raw','labsetting guest partner_native_slot=call','labsetting partner_native_equip=manifest',
        'labsetting partner_fire_census=1','labsetting host partner_native_aim_intent=state','labsetting partner_native_reload_rate=adapt',
        'labsetting partner_native_support=1','labsetting guest partner_native_support=0',
        'labsetting partner_ride_census=1','labsetting guest partner_ride_seat_id=1','labsetting host partner_ride_tilt=0',
        'labsetting partner_placed_census=1','labsetting host partner_placed_census=0',
        'labsetting partner_native_placed=1','labsetting guest partner_native_placed=0','labsetting partner_native_arm=1','labsetting host partner_native_arm=0',
        'labsetting partner_native_place_mirror=1','labsetting guest partner_native_place_mirror=0',
        'labsetting partner_native_traverse=1','labsetting host partner_native_traverse=0',
        'labsetting partner_ride_board=passenger','labsetting guest partner_ride_board=block','labsetting host partner_ride_board=off',
        'labsetting partner_native_cbox=1','labsetting guest partner_native_cbox=0',
        'labsetting partner_native_gesture=1','labsetting guest partner_native_gesture=0',
        'labsetting partner_native_mount=hide','labsetting guest partner_native_mount=hide','labsetting host partner_native_mount=off',
        'labsetting save_handoff=stage','labsetting guest save_handoff=scene','labsetting travel_lock=1','labsetting host travel_lock=0')){
    Assert-True ($ok -cmatch $lineMatch.Groups[1].Value) ('scenario labsetting admits '+$ok)
}
Assert-True ('labsetting partner_native_slot=on' -cnotmatch $lineMatch.Groups[1].Value) 'scenario labsetting refuses partner_native_slot=on'
Assert-True ('labsetting partner_native_equip=all' -cnotmatch $lineMatch.Groups[1].Value) 'scenario labsetting refuses partner_native_equip=all'
foreach($bad in @('labsetting save_handoff=auto','labsetting travel_lock=2','labsetting both save_handoff=stage',
        'labsetting partner_native_mount=seat','labsetting partner_native_mount=1')){
    Assert-True ($bad -cnotmatch $lineMatch.Groups[1].Value) ('scenario labsetting refuses '+$bad)
}
Assert-True ('labsetting partner_native_support=2' -cnotmatch $lineMatch.Groups[1].Value) 'scenario labsetting refuses invalid native support bool'
foreach($bad in @('labsetting partner_ride_census=2','labsetting partner_ride_seat_id=on','labsetting both partner_ride_tilt=1',
        'labsetting partner_ride_board=1','labsetting partner_ride_board=drive','labsetting both partner_ride_board=block')){
    Assert-True ($bad -cnotmatch $lineMatch.Groups[1].Value) ('scenario labsetting refuses '+$bad)
}
Assert-True ('labsetting partner_placed_census=on' -cnotmatch $lineMatch.Groups[1].Value) 'scenario labsetting refuses an invalid placed census bool'
Assert-True ('labsetting partner_native_placed=2' -cnotmatch $lineMatch.Groups[1].Value -and 'labsetting partner_native_arm=on' -cnotmatch $lineMatch.Groups[1].Value) 'scenario labsetting refuses invalid WS4 v1 bools'
Assert-True ($scenarioText.Contains("throw '-LabSetting partner_native_reload / partner_native_slot / partner_native_equip need -NativeStage gun.'") -and
    $sessionText.Contains("throw '-LabSetting partner_native_reload / partner_native_slot / partner_native_equip need -NativeStage gun.'")) 'the reload/slot/equip keys need the gun stage'
# EVADE 0x40000 (dive/evade/roll) is in every native mask; ACTION 0x20 (ride, knife kill, pickup) never; FIRE 0x10
# only in the gun stage and only together with partner_native_fire=visual (the RequestBullet owner filter).
foreach($stage in @('census','loco','gun')){
    $settings=@(Get-WorkerPartnerSettings 'native' $stage)
    $maskSetting=@($settings|Where-Object{$_ -clike 'partner_input_mask=*'})
    Assert-True ($maskSetting.Count -eq 1) ('one mask in native stage '+$stage)
    $maskValue=[Convert]::ToUInt32($maskSetting[0].Substring('partner_input_mask=0x'.Length),16)
    Assert-True ((($maskValue -band 0x40000) -ne 0) -and (($maskValue -band 0x20) -eq 0)) ('native stage '+$stage+' passes EVADE, never ACTION')
    $fire=($maskValue -band 0x10) -ne 0
    Assert-True ($fire -eq ($stage -ceq 'gun') -and $fire -eq ($settings -ccontains 'partner_native_fire=visual')) ('native stage '+$stage+': FIRE only as visual fire in the gun stage')
}
foreach($stage in @('census','loco','gun')){
    Assert-True (((Get-WorkerPartnerSettings 'native' $stage) -join ' ') -ceq (($nativeBase+$stages[$stage]) -join ' ')) ('native stage settings '+$stage)
}
Assert-True (((Get-WorkerPartnerSettings 'native') -join ' ') -ceq (($nativeBase+$stages.loco) -join ' ')) 'native default stage is loco'
# The supervisor gate (worker_game_gate.hpp) compares exactly the masks the native stages emit (worker_protocol_tests
# parses the stage texts through it).
$gateText=[IO.File]::ReadAllText((Join-Path $SourceDirectory 'worker_game_gate.hpp'))
$gateMasks=@([regex]::Matches($gateText,'partner_input_mask == (0x[0-9A-Fa-f]+)u')|ForEach-Object{$_.Groups[1].Value.ToUpperInvariant()}|Sort-Object -Unique)
$stageMasks=@(@(foreach($stage in @('census','loco','gun')){
    @((Get-WorkerPartnerSettings 'native' $stage)|Where-Object{$_ -clike 'partner_input_mask=*'})[0].Substring('partner_input_mask='.Length).ToUpperInvariant()
})|Sort-Object -Unique)
Assert-True ($gateMasks.Count -eq 2 -and ($gateMasks -join ',') -ceq ($stageMasks -join ',')) ('the gate admits exactly the native stage masks: '+($gateMasks -join ','))
Assert-True ($gateText.Contains('lab.partner_native_fire == mgscoop::PartnerNativeFire::visual') -and
    !$gateText.Contains('0x224004E') -and !$gateText.Contains('0x220')) 'the gate: visual fire only in the gun stage, no pre-EVADE mask'
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
# The -LabSetting allow-list (docs/native-puppet.md "Online"): one regex, identical in
# Start-WorkerSession and Invoke-WorkerScenario, and the scenario "labsetting" line form.
$labSettingPatterns=@(foreach($script in @((Join-Path $SourceDirectory 'Start-WorkerSession.ps1'),(Join-Path $RepoRoot 'tools\worker\Invoke-WorkerScenario.ps1'))){
    $tokens=$null;$errors=$null
    $scriptAst=[Management.Automation.Language.Parser]::ParseFile($script,[ref]$tokens,[ref]$errors)
    $param=@($scriptAst.ParamBlock.Parameters|Where-Object{$_.Name.VariablePath.UserPath -ceq 'LabSetting'})
    Assert-True ($param.Count -eq 1) ('one -LabSetting parameter in '+[IO.Path]::GetFileName($script))
    $attribute=@($param[0].Attributes|Where-Object{$_.TypeName.Name -ceq 'ValidatePattern'})
    Assert-True ($attribute.Count -eq 1) ('-LabSetting is pattern-validated in '+[IO.Path]::GetFileName($script))
    $attribute[0].PositionalArguments[0].Value
})
Assert-True ($labSettingPatterns.Count -eq 2 -and $labSettingPatterns[0] -ceq $labSettingPatterns[1]) 'the -LabSetting allow-list is identical in both scripts'
foreach($ok in @('partner_native_net=online','partner_native_net=lab','host:partner_native_stall=hold','partner_native_stall=neutral','guest:partner_native_hold=net',
        'partner_native_hold=own','partner_input_edge_repeats=2','partner_input_edge_repeats=8','partner_native_aim=looksrc','steam_selftest=2','nametag=1')){
    Assert-True ($ok -cmatch $labSettingPatterns[0]) ('allow-listed: '+$ok)
}
foreach($bad in @('partner_native_net=wan','partner_native_stall=keep','partner_native_hold=both','partner_input_edge_repeats=9','partner_input_edge_repeats=1',
        'partner_native_profile=gun','play_rehearsal=1','partner_drive=native','net_latency_ms=100','other:partner_native_net=lab')){
    Assert-True ($bad -cnotmatch $labSettingPatterns[0]) ('not allow-listed: '+$bad)
}
$scenarioSource=[IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\worker\Invoke-WorkerScenario.ps1'))
$scenarioLine=[regex]::Match($scenarioSource,"if\(\`$line -cnotmatch '(\^labsetting[^']+)'\)").Groups[1].Value
Assert-True ([bool]$scenarioLine) 'the scenario labsetting line regex'
foreach($ok in @('labsetting partner_native_net=online','labsetting host partner_native_stall=hold','labsetting guest partner_input_edge_repeats=5','labsetting partner_native_hold=net')){
    Assert-True ($ok -cmatch $scenarioLine) ('scenario labsetting accepts: '+$ok)
}
Assert-True ('labsetting partner_native_net=wan' -cnotmatch $scenarioLine -and 'labsetting play_rehearsal=1' -cnotmatch $scenarioLine) 'scenario labsetting refuses other values'
# The keys of the scenario form equal the parameter form (the same alternatives, ?: groups aside).
$normalize={param($t) ($t -replace '\(\?:','(' -replace '^\^labsetting\\s\+\(\(host\|guest\)\\s\+\)\?','' -replace '^\^\(\(host\|guest\):\)\?','')}
Assert-True ((& $normalize $scenarioLine) -ceq (& $normalize $labSettingPatterns[0])) 'scenario labsetting keys = -LabSetting keys'

# The play rehearsal (docs/two-pc-play.md "Hidden play rehearsal").
Assert-True (@(Get-WorkerPlayPartnerSettings 'animated').Count -eq 0) 'play rehearsal clips: no partner line (Play-Coop -Partner clips)'
Assert-True ((@(Get-WorkerPlayPartnerSettings 'native' 'gun') -join ',') -ceq 'partner_native_profile=gun' -and
    (@(Get-WorkerPlayPartnerSettings 'native') -join ',') -ceq 'partner_native_profile=loco') 'play rehearsal native: the profile key only'
Assert-Throws {Get-WorkerPlayPartnerSettings 'native' 'census'} 'play rehearsal has no census stage'
Assert-Throws {Get-WorkerPlayPartnerSettings 'input'} 'play rehearsal refuses the input profile'
$playSave='C:\repo\build\worker-runtime\'+$id+'\host-saves'
$playHostBase=New-CoopPlayLabIniText -Role host -SaveDir 'C:\Games\MGS_TPP\mgs-coop\coop-saves' -Partner clips
$playGuestBase=New-CoopPlayLabIniText -Role guest -SaveDir 'C:\Games\MGS_TPP\mgs-coop\coop-saves' -PeerAddress '192.0.2.1' -Partner clips
$playHost=ConvertTo-WorkerPlayLabIniText $playHostBase 'host' @('partner_native_profile=gun','net_latency_ms=60','partner_interp=auto','net_seed=1','partner_native_net=lab')
$playGuest=ConvertTo-WorkerPlayLabIniText $playGuestBase 'guest' @('partner_native_profile=gun')
$hostKeys=ConvertFrom-IniText $playHost;$guestKeys=ConvertFrom-IniText $playGuest
Assert-True ((Test-CoopPlayGeneratedText $playHost) -and $hostKeys['mode'] -ceq 'play' -and $hostKeys['net_bind'] -ceq 'loopback' -and $hostKeys['net_listen_port'] -ceq '47830' -and
    $hostKeys['net_peer_port'] -ceq '47831' -and $hostKeys['play_rehearsal'] -ceq '1' -and $hostKeys['xinput'] -ceq 'none' -and $hostKeys['partner_native_profile'] -ceq 'gun' -and
    $hostKeys['net_latency_ms'] -ceq '60' -and $hostKeys['partner_native_net'] -ceq 'lab' -and $hostKeys['partner_input_send'] -ceq '1' -and
    !$hostKeys.Contains('net_peer_addr') -and !$hostKeys.Contains('loopback') -and !$hostKeys.Contains('self_steamid')) 'play rehearsal host lab.ini'
Assert-True ([regex]::Matches($playHost,'(?m)^partner_interp=').Count -eq 1) 'a key already in the play text is replaced, never duplicated'
Assert-True ($guestKeys['net_bind'] -ceq 'loopback' -and $guestKeys['net_listen_port'] -ceq '47831' -and $guestKeys['net_peer_addr'] -ceq '127.0.0.1' -and
    $guestKeys['net_peer_port'] -ceq '47830' -and $guestKeys['role'] -ceq 'guest' -and !$guestKeys.Contains('self_steamid')) 'play rehearsal guest lab.ini: 127.0.0.1, fixed ports, real identity'
Assert-Throws {ConvertTo-WorkerPlayLabIniText $playHostBase 'host' @('self_steamid=76561202255233008')} 'the rehearsal never sets an identity key'
Assert-Throws {ConvertTo-WorkerPlayLabIniText $playHostBase 'host' @('loopback=1')} 'the rehearsal never enables the loopback emulator'
Assert-Throws {ConvertTo-WorkerPlayLabIniText ($ini) 'host' @()} 'the rehearsal starts only from the play text'
Assert-Throws {ConvertTo-WorkerPlayLabIniText (New-CoopPlayLabIniText -Role host -SaveDir 'C:\x\saves' -Transport steam -SteamSelf $steam) 'host' @()} 'the rehearsal is UDP only'
$owned=(Set-WorkerIniValue $playHost 'save_dir' $playSave)
Assert-True (Test-WorkerPreparedConfigText 'lab.ini' $owned 'host' $steam $url @($playSave) 'play') 'partial owned play rehearsal output'
Assert-True (!(Test-WorkerPreparedConfigText 'lab.ini' $owned 'host' $steam $url @($playSave))) 'the lab check never adopts a play lab.ini'
Assert-True (!(Test-WorkerPreparedConfigText 'lab.ini' $owned 'guest' $steam $url @($playSave) 'play')) 'play rehearsal: foreign role'
Assert-True (Test-WorkerPreparedConfigText 'link-secret.txt' ("7K3QZ9M2XWD4HT8RVN6B`r`n") 'host' $steam $url @() 'play') 'the rehearsal secret is owned output in play'
Assert-True (!(Test-WorkerPreparedConfigText 'link-secret.txt' ("7K3QZ9M2XWD4HT8RVN6B`r`n") 'host' $steam $url @())) 'never a lab secret'
Assert-True (!(Test-WorkerPreparedConfigText 'client-host.txt' ($steam+"`n") 'guest' $steam $url @() 'play')) 'play has no client-host.txt'
# The play safety verdict from early.log / steam-observer.log.
$playEarly="early=start; lab=1; role=guest`nsaves=redirected; dir=$playSave`nplay=1; loopback=0; mutex=renamed; refused=0x0; saves=redirected; rehearsal=1`nmutex=renamed; suffix=worker_guest_01234567`nidentity=real`n"
$playSteam="master_server=redirected; reason=offline; url=$url`n"
$verdict=Get-WorkerPlaySafetyVerdict -Role guest -EarlyLog $playEarly -SteamLog $playSteam -ExpectedSaveDir $playSave -ExpectedUrl $url -ExpectedMutexSuffix 'worker_guest_01234567'
Assert-True ($verdict.State -ceq 'ok') ('play safety ok: '+$verdict.State+' '+$verdict.Reason+' '+($verdict.Missing -join ','))
foreach($case in @(@('refused=0x0','refused=0x20000','play_refused'),@('; rehearsal=1','','not_a_rehearsal_build'),@('identity=real','identity=fake; steamid=1','identity_fake'),
        @("saves=redirected; dir=$playSave","saves=redirected; dir=C:\Steam\userdata\7",'saves_wrong_dir'),@('loopback=0','loopback=1','loopback_emulator'),
        @('suffix=worker_guest_01234567','suffix=other','mutex_wrong_suffix'),@('role=guest','role=host','role_mismatch'))){
    $v=Get-WorkerPlaySafetyVerdict -Role guest -EarlyLog ($playEarly.Replace($case[0],$case[1])) -SteamLog $playSteam -ExpectedSaveDir $playSave -ExpectedUrl $url -ExpectedMutexSuffix 'worker_guest_01234567'
    Assert-True ($v.State -ceq 'unsafe' -and $v.Reason -ceq $case[2]) ('play safety refuses '+$case[2]+' (got '+$v.State+' '+$v.Reason+')')
}
$v=Get-WorkerPlaySafetyVerdict -Role guest -EarlyLog "early=start; lab=1; role=guest`n" -SteamLog '' -ExpectedSaveDir $playSave -ExpectedUrl $url -ExpectedMutexSuffix 'x'
Assert-True ($v.State -ceq 'pending' -and ($v.Missing -join ',') -ceq 'saves,play,mutex,master_server') 'play safety pending until every line arrived'
$v=Get-WorkerPlaySafetyVerdict -Role guest -EarlyLog $playEarly -SteamLog "master_server=redirected; url=http://other/tppstm/gate`n" -ExpectedSaveDir $playSave -ExpectedUrl $url -ExpectedMutexSuffix 'worker_guest_01234567'
Assert-True ($v.State -ceq 'unsafe' -and $v.Reason -ceq 'master_server_wrong_url') 'play safety: the closed-port master URL'
$startPlay=[IO.File]::ReadAllText((Join-Path $SourceDirectory 'Start-WorkerSession.ps1'))
Assert-True ($startPlay.Contains("if(`$Mode -ceq 'play'){`$manifest['lab_mode']='play'}") -and $startPlay.Contains('Get-WorkerPlaySafetyVerdict') -and
    $startPlay.Contains('ConvertTo-WorkerPlayLabIniText') -and $startPlay.Contains("[ValidateSet('lab','play')][string]`$Mode='lab'") -and
    $startPlay.Contains("[ValidateSet('off','good','bad','awful')][string]`$WanSim='off'")) 'Start-WorkerSession: -Mode play (PlayRehearsal manifest, play safety) and -WanSim'

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
# Three forms since join-fix phase 2 (P2-19): real (backup), lab and run (an earlier closed run's guest copy).
Assert-True ($seedDefinitions.Count -eq 3) 'three seed forms'
Assert-True ($seedDefinitions -ccontains "`$seed=Copy-SaveSeed -BackupDirectory `$freshBackup -SaveDir `$saveRoot -LocalSubdir '287700\local' -RemoteSubdir 'remote'") 'default role seed reads only this run verified backup'
Assert-True ($start.Contains("[ValidateSet('real','lab')][string]`$SaveSeed='real'") -and $start.Contains("`$labSaves=Assert-WorkerPlainPath `$p.SaveDir -MustExist")) 'lab seed is an explicit opt-in from the folder lab save copy'
Assert-True ($start.Contains("[ValidateSet('','real','lab')][string]`$HostSaveSeed=''") -and $start.Contains("[ValidateSet('','real','lab')][string]`$GuestSaveSeed=''") -and
    $start.Contains("[ValidatePattern('^`$|^[0-9a-f]{32}`$')][string]`$GuestSaveSeedRun=''")) 'per-role seed parameters (P2-19)'
Assert-True ($start.Contains('$seedRunSaves=Get-WorkerSeedRunSaves $repo $GuestSaveSeedRun $runId') -and $start.Contains('$roleSeed=$roleSeeds[$role]') -and
    $start.Contains('$row.save_seed_kind=$roleSeed')) 'the role loop picks each role''s seed and records it'
foreach($name in @('Start-WorkerSession.ps1','Invoke-WorkerScenario.ps1')){
    $seedAst=if($name -ceq 'Start-WorkerSession.ps1'){$startAst}else{[Management.Automation.Language.Parser]::ParseFile((Join-Path $RepoRoot 'tools\worker\Invoke-WorkerScenario.ps1'),[ref]$tokens,[ref]$errors)}
    $seedParams=@($seedAst.ParamBlock.Parameters|Where-Object{$_.Name.VariablePath.UserPath -cin 'HostSaveSeed','GuestSaveSeed','GuestSaveSeedRun'})
    Assert-True ($seedParams.Count -eq 3) ('per-role seed parameters in '+$name)
    $fixtureArgs=if($name -ceq 'Start-WorkerSession.ps1'){@{BuildDirectory='C:\fixture';CheckReceipt='C:\fixture';BaselineLoaderSha256=('A'*64)}}else{@{}}
    $bind=[scriptblock]::Create($seedAst.ParamBlock.Extent.Text+'; return "$HostSaveSeed|$GuestSaveSeed|$GuestSaveSeedRun"')
    Assert-True ((& $bind @fixtureArgs) -ceq '||') ('seed defaults are empty in '+$name)
    Assert-True ((& $bind @fixtureArgs -HostSaveSeed lab -GuestSaveSeed real) -ceq 'lab|real|') ('seeds bind in '+$name)
    Assert-True ((& $bind @fixtureArgs -GuestSaveSeedRun $id) -ceq ('||'+$id)) ('seed run binds in '+$name)
    foreach($bad in @(@{HostSaveSeed='run'},@{GuestSaveSeed='both'},@{GuestSaveSeedRun='0123'},@{GuestSaveSeedRun=($id+'0')})){
        Assert-Throws {& $bind @fixtureArgs @bad} ('seed parameter refuses '+(($bad.Keys|ForEach-Object{$_+'='+$bad[$_]}) -join ',')+' in '+$name)
    }
}
# The pure seed choice of Start-WorkerSession (executed, not copied).
& {
    $definition=@($startAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-WorkerRoleSeeds'},$true))
    Assert-True ($definition.Count -eq 1) 'one actual role seed boundary'
    . ([scriptblock]::Create($definition[0].Extent.Text))
    $s=Get-WorkerRoleSeeds 'real' '' '' '';Assert-True ($s.host -ceq 'real' -and $s.guest -ceq 'real') 'empty role seeds take -SaveSeed'
    $s=Get-WorkerRoleSeeds 'lab' '' 'real' '';Assert-True ($s.host -ceq 'lab' -and $s.guest -ceq 'real') 'guest seed overrides only the guest'
    $s=Get-WorkerRoleSeeds 'real' 'lab' '' '';Assert-True ($s.host -ceq 'lab' -and $s.guest -ceq 'real') 'host seed overrides only the host'
    $s=Get-WorkerRoleSeeds 'lab' '' '' $id;Assert-True ($s.host -ceq 'lab' -and $s.guest -ceq 'run') 'a seed run seeds the guest only'
    Assert-Throws {Get-WorkerRoleSeeds 'lab' '' 'real' $id} 'a seed run excludes -GuestSaveSeed'
    Assert-Throws {Get-WorkerRoleSeeds 'lab' '' '' $id.ToUpperInvariant()} 'a seed run id is exact lower case'
}
# LabCommon admits the two phase-2 keys for Prepare-LabInstance -PartnerSetting (lab runs).
Assert-True ((ConvertTo-LabPartnerValue -Key save_handoff -Value stage) -ceq 'stage' -and (ConvertTo-LabPartnerValue -Key save_handoff -Value always) -ceq 'always' -and
    (ConvertTo-LabPartnerValue -Key travel_lock -Value 1) -ceq '1' -and (ConvertTo-LabPartnerValue -Key travel_lock -Value 0) -ceq '0') 'save_handoff and travel_lock are LabCommon keys'
Assert-Throws {ConvertTo-LabPartnerValue -Key save_handoff -Value auto} 'save_handoff values'
Assert-Throws {ConvertTo-LabPartnerValue -Key travel_lock -Value 2} 'travel_lock values'
$roleSettings=ConvertFrom-LabPartnerSettingArgs -Items @('host:save_handoff=stage','guest:save_handoff=scene','travel_lock=1') -Role guest
Assert-True ($roleSettings['save_handoff'] -ceq 'scene' -and $roleSettings['travel_lock'] -ceq '1') 'role-scoped save_handoff reaches only its role'
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
# Execute the actual required-result helper; a failed prerequisite must prevent
# the next simulated input while preserving failure evidence. No games are run.
& {
    $definition=@($scenarioAst.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Assert-ScenarioResult'},$true))
    Assert-True ($definition.Count -eq 1) 'one fail-fast scenario result helper'
    . ([scriptblock]::Create($definition[0].Extent.Text))
    $expectations=[Collections.Generic.List[string]]::new()
    $expectFailures=[Collections.Generic.List[string]]::new()
    $script:nextScenarioInput=0;$failed=$false
    try {Assert-ScenarioResult 'host' '^k1 a2 r2$' 'k1 a1 r1' $true;$script:nextScenarioInput++}catch{$failed=$true}
    Assert-True ($failed -and $script:nextScenarioInput -eq 0) 'failed prerequisite stops following input'
    Assert-True ($expectations.Count -eq 1 -and $expectFailures.Count -eq 1 -and $expectFailures[0].Contains('k1 a1 r1')) 'required failure retains actual evidence'
    Assert-ScenarioResult 'host' '^k1 a2 r2$' 'k1 a2 r2' $true;$script:nextScenarioInput++
    Assert-True ($script:nextScenarioInput -eq 1 -and $expectFailures.Count -eq 1) 'passing required check continues'
    Assert-ScenarioResult 'guest' '^READY$' 'ready' $false -WarningAction SilentlyContinue;$script:nextScenarioInput++
    Assert-True ($script:nextScenarioInput -eq 2 -and $expectFailures.Count -eq 2) 'expect preserves case-sensitive nonfatal behavior'
}
# Every shipped scenario passes the runner's own step validation (its helper
# functions, key/role lists and validation loop, extracted; nothing runs).
& {
    $parts=@()
    foreach($name in @('Test-ScenarioKeys','Test-ScenarioLabel','ConvertFrom-ScenarioPatternTokens')) {
        $definition=@($scenarioAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
        Assert-True ($definition.Count -eq 1) ('scenario validator helper '+$name)
        $parts+=$definition[0].Extent.Text
    }
    foreach($variable in @('roleNames','keyNames')) {
        $assignment=@($scenarioAst.FindAll({param($node)$node -is [Management.Automation.Language.AssignmentStatementAst] -and
            $node.Left.Extent.Text -ceq ('$'+$variable)},$false))
        Assert-True ($assignment.Count -eq 1) ('scenario validator list '+$variable)
        $parts+=$assignment[0].Extent.Text
    }
    $loop=@($scenarioAst.FindAll({param($node)$node -is [Management.Automation.Language.ForEachStatementAst] -and
        $node.Condition.Extent.Text -ceq '$steps' -and $node.Body.Extent.Text.Contains('switch -Regex')},$false))
    Assert-True ($loop.Count -eq 1) 'one scenario validation loop'
    $validator=[scriptblock]::Create(($parts -join "`n")+"`n"+$loop[0].Extent.Text)
    $Mode='lab'
    $scenarioFiles=@(Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'tools\worker\scenarios') -Filter '*.txt' -File)
    Assert-True ($scenarioFiles.Count -ge 6 -and @($scenarioFiles|Where-Object{$_.Name -ceq 'partner-native-actions.txt'}).Count -eq 1) 'the scenarios include partner-native-actions.txt'
    foreach($file in $scenarioFiles) {
        $steps=@([IO.File]::ReadAllLines($file.FullName)|ForEach-Object{$_.Trim()}|Where-Object{$_ -and !$_.StartsWith('#') -and $_ -cnotmatch '^labsetting(\s|$)'})
        Assert-True ($steps.Count -gt 0) ('scenario has steps: '+$file.Name)
        $failure=$null
        try { & $validator } catch { $failure=$_.Exception.Message }
        Assert-True (!$failure) ('scenario steps valid: '+$file.Name+' '+$failure)
    }
    # WS2 (native throws in two-PC play, Stage A): the play rehearsal validates in PLAY mode (no lab-only step such
    # as worldtrigger), names no labsetting (a scenario labsetting line overrides -LabSetting, and the Stage B
    # re-run is the same file without the command-line key), throws three times, then runs the viewer's own throw
    # during a partner episode LAST (critic #5). Its checkpoint save + reload half is the lab companion.
    & {
        $Mode='play'
        $playPath=Join-Path $RepoRoot 'tools\worker\scenarios\partner-support-play.txt'
        Assert-True (Test-Path -LiteralPath $playPath -PathType Leaf) 'shipped WS2 scenario partner-support-play.txt'
        $playLines=[IO.File]::ReadAllLines($playPath)
        $steps=@($playLines|ForEach-Object{$_.Trim()}|Where-Object{$_ -and !$_.StartsWith('#') -and $_ -cnotmatch '^labsetting(\s|$)'})
        $failure=$null
        try { & $validator } catch { $failure=$_.Exception.Message }
        Assert-True (!$failure) ('partner-support-play steps valid in play mode '+$failure)
        Assert-True (@($playLines|Where-Object{$_.Trim() -cmatch '^labsetting(\s|$)'}).Count -eq 0) 'partner-support-play names no labsetting line'
        Assert-True (@($steps|Where-Object{$_ -cmatch '(^|\s)worldtrigger(\s|$)'}).Count -eq 0) 'partner-support-play has no lab-only world trigger'
        $playText=[IO.File]::ReadAllText($playPath)
        foreach($needle in @('-Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab -WanSim off',
            '-WanSim bad','-WanSim awful','partner_support=mode; mode=play; reason=ok','reason=throw_release','reason=viewer_own','stall_freeze',
            'NO partner_support=stock_gap','stock_leak_mission_latched','fire_reload_off','writer_uncertain_mission_latched','hard_leaves 0',
            'rehearsal=1','partner-support-checkpoint.txt')){
            Assert-True ($playText.Contains($needle)) ('partner-support-play covers: '+$needle)
        }
        $hostThrows=@(for($i=0;$i -lt $steps.Count;$i++){if($steps[$i] -ceq 'host hold rmb+lmb 150 nowait'){$i}})
        $ownThrow=@(for($i=0;$i -lt $steps.Count;$i++){if($steps[$i] -ceq 'guest hold rmb+lmb 150 nowait'){$i}})
        Assert-True ($hostThrows.Count -eq 4 -and $ownThrow.Count -eq 1) 'partner-support-play: three partner throws, the viewer-own overlap and one throw after it'
        Assert-True ($ownThrow[0] -gt $hostThrows[2] -and $ownThrow[0] -lt $hostThrows[3]) 'partner-support-play runs the viewer-own throw after the three throws'
        $guestCounts=@($steps|Where-Object{$_ -cmatch '^guest require support;.*counts=12,16383,12,8,12,12,8,8;'})
        Assert-True ($guestCounts.Count -ge 4) 'partner-support-play requires the guest seed counts before and after the throws'
        Assert-True ($steps[-1] -cmatch '^guest require support;.*counts=11,16383,12,8,12,12,8,8;') 'partner-support-play ends on the guest spending exactly its own grenade'
        $cpPath=Join-Path $RepoRoot 'tools\worker\scenarios\partner-support-checkpoint.txt'
        $cpText=[IO.File]::ReadAllText($cpPath)
        foreach($needle in @('host worldtrigger world_probe=shared_checkpoint','guest worldtrigger world_probe=shared_death',
            'host worldtrigger world_probe=host_continue','-PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab')){
            Assert-True ($cpText.Contains($needle)) ('partner-support-checkpoint covers: '+$needle)
        }
        Assert-True (!$cpText.Contains('-Mode play')) 'partner-support-checkpoint runs in lab mode (world triggers are lab-only)'
        $cpSteps=@([IO.File]::ReadAllLines($cpPath)|ForEach-Object{$_.Trim()}|Where-Object{$_ -and !$_.StartsWith('#')})
        $continueAt=[array]::IndexOf($cpSteps,'host worldtrigger world_probe=host_continue')
        Assert-True ($continueAt -gt 0 -and @($cpSteps[$continueAt..($cpSteps.Count-1)]|Where-Object{$_ -cmatch '^guest require support;.*counts=12,16383,12,8,12,12,8,8;'}).Count -ge 1) 'partner-support-checkpoint re-reads the guest counts after the reload'
        $steps=@($cpSteps|Where-Object{$_ -cnotmatch '^labsetting(\s|$)'});$failed=$false
        try { & $validator } catch { $failed=$true }
        Assert-True $failed 'partner-support-checkpoint is refused in play mode (its world triggers)'
    }
    # WS4 v2 (docs/anim-round2-throws.md "WS4 v2"): partner-c4-v2.txt names the mirror key with the WS4 v1 C4 key and
    # the census, places C4 at several spots and detonates, framing body 1 from the side before every phase.
    & {
        $Mode='lab'
        $v2Path=Join-Path $RepoRoot 'tools\worker\scenarios\partner-c4-v2.txt'
        Assert-True (Test-Path -LiteralPath $v2Path -PathType Leaf) 'shipped WS4 v2 scenario partner-c4-v2.txt'
        $v2Lines=[IO.File]::ReadAllLines($v2Path)
        $labsettings=@($v2Lines|ForEach-Object{$_.Trim()}|Where-Object{$_ -cmatch '^labsetting(\s|$)'})
        foreach($line in @('labsetting partner_native_place_mirror=1','labsetting partner_native_placed=1','labsetting partner_placed_census=1')){
            Assert-True ($labsettings -ccontains $line) ('partner-c4-v2 names '+$line)
        }
        foreach($line in $labsettings){ Assert-True ($line -cmatch $lineMatch.Groups[1].Value) ('partner-c4-v2 labsetting is allow-listed: '+$line) }
        $steps=@($v2Lines|ForEach-Object{$_.Trim()}|Where-Object{$_ -and !$_.StartsWith('#') -and $_ -cnotmatch '^labsetting(\s|$)'})
        $failure=$null
        try { & $validator } catch { $failure=$_.Exception.Message }
        Assert-True (!$failure) ('partner-c4-v2 steps valid in lab mode '+$failure)
        $v2Text=[IO.File]::ReadAllText($v2Path)
        foreach($needle in @('-PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab','place_mirror=1',
            'partner_place_mirror=edge; act=place','partner_place_mirror=result; act=place; taken=1','reason=place','reason=detonate',
            'mode=46','mode=47','owners=65535:0','NO partner_support=refused; ...; reason=placed_shared_write','reverted=2',
            'counts=12,16383,12,8,12,12,8,8','v7_peer','RealSavesUnchanged')){
            Assert-True ($v2Text.Contains($needle)) ('partner-c4-v2 covers: '+$needle)
        }
        $placeAttempts=@($steps|Where-Object{$_ -ceq 'host hold lmb 300 nowait'})
        $detonate=@($steps|Where-Object{$_ -ceq 'host press e'})
        Assert-True ($placeAttempts.Count -ge 3 -and $detonate.Count -ge 1) 'partner-c4-v2: several place attempts and a detonation'
        $reframes=@($steps|Where-Object{$_ -cmatch '^placebehind guest host [0-9]+ [0-9]+$'})
        Assert-True ($reframes.Count -ge ($placeAttempts.Count+$detonate.Count+1)) 'partner-c4-v2 re-frames the guest before every phase'
        $guestCounts=@($steps|Where-Object{$_ -cmatch '^guest require support;.*counts=12,16383,12,8,12,12,8,8;'})
        Assert-True ($guestCounts.Count -ge 3) 'partner-c4-v2 requires the guest seed counts unchanged around the C4'
        Assert-True (@($steps|Where-Object{$_ -cmatch '^guest burst c4-(place|det)'}).Count -ge 5) 'partner-c4-v2 films every place and detonation'
    }
    # WS4 v1 (docs/anim-round2-throws.md "WS4 v1"): partner-c4.txt, a lab scenario that names both WS4 v1 keys and
    # the census, places C4 at several spots, detonates, then aims the arm, with every evidence line in its header.
    & {
        $Mode='lab'
        $c4Path=Join-Path $RepoRoot 'tools\worker\scenarios\partner-c4.txt'
        Assert-True (Test-Path -LiteralPath $c4Path -PathType Leaf) 'shipped WS4 v1 scenario partner-c4.txt'
        $c4Lines=[IO.File]::ReadAllLines($c4Path)
        $labsettings=@($c4Lines|ForEach-Object{$_.Trim()}|Where-Object{$_ -cmatch '^labsetting(\s|$)'})
        foreach($line in @('labsetting partner_native_placed=1','labsetting partner_native_arm=1','labsetting partner_placed_census=1')){
            Assert-True ($labsettings -ccontains $line) ('partner-c4 names '+$line)
        }
        foreach($line in $labsettings){ Assert-True ($line -cmatch $lineMatch.Groups[1].Value) ('partner-c4 labsetting is allow-listed: '+$line) }
        $steps=@($c4Lines|ForEach-Object{$_.Trim()}|Where-Object{$_ -and !$_.StartsWith('#') -and $_ -cnotmatch '^labsetting(\s|$)'})
        $failure=$null
        try { & $validator } catch { $failure=$_.Exception.Message }
        Assert-True (!$failure) ('partner-c4 steps valid in lab mode '+$failure)
        $c4Text=[IO.File]::ReadAllText($c4Path)
        foreach($needle in @('-PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab','partner_support=lanes; support=1; placed=1; arm=1',
            'partner_placed_census=gun; k=1','mode=43','mode=46','reason=place','partner_support=placed_check','owners=65535:0',
            'NO partner_support=refused; ...; reason=placed_shared_write','partner_arm=check','NO partner_arm=refused','use_mode_mission_latched',
            'counts=12,16383,12,8,12,12,8,8','obs_att','ACTION','RealSavesUnchanged')){
            Assert-True ($c4Text.Contains($needle)) ('partner-c4 covers: '+$needle)
        }
        $placeAttempts=@($steps|Where-Object{$_ -ceq 'host hold lmb 300 nowait'})
        $detonate=@($steps|Where-Object{$_ -ceq 'host press e'})
        $armSelect=@($steps|Where-Object{$_ -ceq 'host debug slot hand 0'})
        Assert-True ($placeAttempts.Count -ge 3 -and $detonate.Count -ge 1 -and $armSelect.Count -eq 1) 'partner-c4: several place attempts, a detonation, the arm'
        # The host walks between attempts: the guest is placed behind it again before every place, the detonation and
        # the arm, or the static watcher loses body 1 from the frame (run 4e5ab735).
        $reframes=@($steps|Where-Object{$_ -cmatch '^placebehind guest host [0-9]+ [0-9]+$'})
        Assert-True ($reframes.Count -ge ($placeAttempts.Count+$detonate.Count+$armSelect.Count+1)) 'partner-c4 re-frames the guest before every phase'
        $guestCounts=@($steps|Where-Object{$_ -cmatch '^guest require support;.*counts=12,16383,12,8,12,12,8,8;'})
        Assert-True ($guestCounts.Count -ge 3) 'partner-c4 requires the guest seed counts unchanged around the C4 and the arm'
        Assert-True (@($steps|Where-Object{$_ -cmatch '(^|\s)capture(\s|$)'}).Count -ge 6) 'partner-c4 captures every phase'
    }
    # WS2 follow-up (docs/anim-round2-throws.md "Stall test step"): the worker-only 'stall <role> <ms>' step
    # (50..1000 ms, one role) and partner-support-stall.txt, a play rehearsal whose stalls fall inside a held grenade.
    & {
        $Mode='play'
        foreach($step in @('stall host 320','stall guest 50','stall host 1000','stall guest 400')){
            $steps=@($step);$failure=$null;try{& $validator}catch{$failure=$_.Exception.Message}
            Assert-True (!$failure) ('stall step admitted: '+$step+' '+$failure)
        }
        foreach($step in @('stall both 320','stall host 49','stall host 1001','stall host','stall host 320 x','STALL host 320','Stall host 320',
            'stall HOST 320','stall host 0320','stall host abc','stall host -320','stall host 3.5','stall host 99999','host stall 320')){
            $steps=@($step);$failed=$false;try{& $validator}catch{$failed=$true}
            Assert-True $failed ('stall step refused: '+$step)
        }
        $stallPath=Join-Path $RepoRoot 'tools\worker\scenarios\partner-support-stall.txt'
        Assert-True (Test-Path -LiteralPath $stallPath -PathType Leaf) 'shipped scenario partner-support-stall.txt'
        $stallLines=[IO.File]::ReadAllLines($stallPath)
        $steps=@($stallLines|ForEach-Object{$_.Trim()}|Where-Object{$_ -and !$_.StartsWith('#') -and $_ -cnotmatch '^labsetting(\s|$)'})
        $failure=$null
        try { & $validator } catch { $failure=$_.Exception.Message }
        Assert-True (!$failure) ('partner-support-stall steps valid in play mode '+$failure)
        Assert-True (@($stallLines|Where-Object{$_.Trim() -cmatch '^labsetting(\s|$)'}).Count -eq 0) 'partner-support-stall names no labsetting line'
        $stallText=[IO.File]::ReadAllText($stallPath)
        foreach($needle in @('-Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab -WanSim off',
            'partner_net=input_stall; reason=start; ms=320','partner_net=input_stall; reason=end; ms=320','reason=start; level=0x8','level=0x18',
            'reason=fresh','exactly ONE partner_support=armed','no second throw','NO partner_support=stock_gap','stock_leak_mission_latched',
            'fire_reload_off','rec240_foreign','background worker','friend/Release','Closed, BaselineRestored and RealSavesUnchanged')){
            Assert-True ($stallText.Contains($needle)) ('partner-support-stall covers: '+$needle)
        }
        $stalls=@(for($i=0;$i -lt $steps.Count;$i++){if($steps[$i] -ceq 'stall host 320'){$i}})
        Assert-True ($stalls.Count -eq 2) 'partner-support-stall: two 320 ms host stalls (in stall_ms 400, past stale_ms 250)'
        # A: inside the aim (rmb held), before the throw's FIRE; B: inside the FIRE hold, before its release.
        $aim=[array]::IndexOf($steps,'host hold rmb 1500 nowait')
        $fireA=[array]::IndexOf($steps,'host hold rmb+lmb 150 nowait')
        Assert-True ($aim -ge 0 -and $aim -lt $stalls[0] -and $stalls[0] -lt $fireA) 'partner-support-stall A: the stall falls between the aim and the FIRE press'
        Assert-True ($steps[$stalls[0]+1] -ceq 'host hold rmb 1500 nowait') 'partner-support-stall A: the aim is extended past the stall (no new press)'
        $holdB=[array]::IndexOf($steps,'host hold rmb+lmb 1500 nowait')
        $releaseB=@(for($i=$stalls[1];$i -lt $steps.Count;$i++){if($steps[$i] -ceq 'host release'){$i}})
        Assert-True ($holdB -gt $fireA -and $holdB -lt $stalls[1] -and $releaseB.Count -ge 1) 'partner-support-stall B: the stall falls inside the FIRE hold, then the release'
        Assert-True ($steps[$stalls[1]+1] -ceq 'host hold rmb+lmb 1500 nowait') 'partner-support-stall B: the FIRE hold is extended past the stall (no new press)'
        $guestCounts=@($steps|Where-Object{$_ -cmatch '^guest require support;.*counts=12,16383,12,8,12,12,8,8;'})
        Assert-True ($guestCounts.Count -ge 3) 'partner-support-stall requires the guest seed counts before and after both phases'
        foreach($needle in @("waitlog host 'partner_net=input_stall; reason=end; ms=320;' 10",
            "waitlog guest 'partner_support=stall_freeze; slot=3; equip=354; type=12; reason=start;' 10",
            "waitlog guest 'partner_support=stall_freeze; slot=3; equip=354; type=12; reason=fresh;' 10",
            "waitlog guest 'partner_support=armed; slot=3; equip=354; type=12; reason=throw_release' 10",
            "waitlog guest 'partner_support=stall_freeze; slot=3; equip=354; type=12; reason=start; level=0x18;' 10")){
            Assert-True ($steps -ccontains $needle) ('partner-support-stall waits for: '+$needle)
        }
        # The runner sends the native input_stall command (paired by its result) and records an expectation.
        $runnerText=[IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\worker\Invoke-WorkerScenario.ps1'))
        Assert-True ($runnerText.Contains('$command="input_stall $($t[2])"') -and $runnerText.Contains('$pattern="^input_stall=started;ms=$($t[2]);"') -and
            $runnerText.Contains("Add-ScenarioExpectation `$t[1] 'stall'")) 'the stall step sends input_stall and records its result'
        Assert-True ($runnerText.Contains("'waitmission','waitgameplay','stance','resetpose','waitexit','expectstate','stall'")) 'the stall step drives its one role'
        # Worker-only (never in friend/Release builds): the native command is gated on the compiled background
        # worker and the owned private worker; partner_net refuses without the worker build (link_udp_tests checks
        # the refusal in a non-worker build); only the worker build defines MGS_BACKGROUND_WORKER for partner_net;
        # and the Release profile never builds the background worker (Get-WorkerPlan, worker_checks_tests).
        $observerText=[IO.File]::ReadAllText((Join-Path $RepoRoot 'native\game_script_observer.cpp'))
        $at=$observerText.IndexOf('std::strncmp(pending.text, "input_stall ", 12) == 0')
        Assert-True ($at -gt 0) 'the native input_stall command exists'
        $branch=$observerText.Substring($at,[Math]::Min(1600,$observerText.Length-$at))
        Assert-True ($branch.Contains('mgscoop::worker_scenario::Compiled() && mgscoop::worker_scenario::Active()') -and
            $branch.Contains('lab::SavesRedirected()') -and $branch.Contains('"owned_worker_required"') -and
            $branch.Contains('mgscoop::partner_net::StallInput(ms)')) 'input_stall needs the compiled background worker and the owned private worker'
        $netText=[IO.File]::ReadAllText((Join-Path $RepoRoot 'native\partner_net.cpp'))
        Assert-True ($netText.Contains('constexpr bool kStallCompiled = MGS_BACKGROUND_WORKER != 0;') -and $netText.Contains('"not_worker_build"')) 'partner_net refuses the stall without the worker build'
        $cmakeText=[IO.File]::ReadAllText((Join-Path $RepoRoot 'CMakeLists.txt'))
        Assert-True ([regex]::IsMatch($cmakeText,'if\(MGS_BACKGROUND_WORKER\)\s+target_compile_definitions\(mgs_coop_lab PRIVATE MGS_BACKGROUND_WORKER=1\)\s+endif\(\)')) 'only the worker build compiles the stall into partner_net'
        Assert-True ($cmakeText.Contains('option(MGS_BACKGROUND_WORKER "Permit an explicitly admitted private-desktop offline scenario worker; no visible desktop input" OFF)')) 'the background worker stays default OFF'
    }
    foreach($step in @('host worldtrigger goto=30020','host worldtrigger goto=30010 goto_lz=1','waitmission guest 30020 120',
        'host worldtrigger world_probe=shared_checkpoint','guest worldtrigger world_probe=shared_death','host worldtrigger world_probe=host_continue',
        'host worldtrigger world=0','guest worldtrigger world=0')){
        $steps=@($step); & $validator; Assert-True $true ('qualified hidden travel admits '+$step)
    }
    foreach($step in @('both worldtrigger goto=30020','guest worldtrigger goto=30020','both worldtrigger world=0','host worldtrigger world=1','host worldtrigger world=0 extra',
        'host worldtrigger goto=30050','guest worldtrigger world_probe=host_continue','host worldtrigger world_probe=shared_death',
        'both worldtrigger world_probe=shared_checkpoint','host worldtrigger world_probe=kill_near','waitmission both 30020 120','waitmission host 30060 120','waitmission host 30020 401')){
        $steps=@($step);$failed=$false;try{& $validator}catch{$failed=$true}
        Assert-True $failed ('hidden travel refuses '+$step)
    }
    # Join-fix phase 2 steps (P2-19): titles, log and state checks, one role's exit and the handoff apply.
    foreach($step in @('titlemenu both 240','titlemenu host 5','titlemenu guest 400','title guest 200','title host','title','title 240',
        'waitlog host ^save_handoff= 30',"waitlog guest 'save_handoff=exit; how=wm_close' 60","waitlog host 'save_handoff=offer; ' 60",
        'expectstate guest handoff ^staged$','expectstate guest mission ^40050$','expectstate host travel_unlocked ^0$','expectstate guest handoff_pct ^(100|[1-9]?[0-9])$',
        'waitexit guest 90','waitexit host 5','applyhandoff guest')){
        $steps=@($step);$failure=$null;try{& $validator}catch{$failure=$_.Exception.Message}
        Assert-True (!$failure) ('phase-2 step admitted: '+$step+' '+$failure)
    }
    foreach($step in @('title both','title both 240','title guest 1','title guest x','titlemenu guest 1','titlemenu guest','titlemenu guest 401','titlemenu all 240',
        'TITLEMENU both 240','waitexit both 60','waitexit guest 4','waitexit guest','applyhandoff host','applyhandoff both','applyhandoff','applyhandoff guest now',
        'expectstate guest secret x','expectstate both mission 1','expectstate guest mission','expectstate guest mission (','expectstate guest Mission 1',
        'waitlog both x 30','waitlog host x','waitlog host ( 30',"waitlog host '' 30",'waitlog host x 401','waitlog host x 0','waitlog host x y')){
        $steps=@($step);$failed=$false;try{& $validator}catch{$failed=$true}
        Assert-True $failed ('phase-2 step refused: '+$step)
    }
    foreach($name in @('handoff-stage.txt','handoff-restart.txt','handoff-after-restart.txt','travel-lock.txt')){
        Assert-True (@($scenarioFiles|Where-Object{$_.Name -ceq $name}).Count -eq 1) ('shipped phase-2 scenario '+$name)
    }
    $restart=[IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\worker\scenarios\handoff-restart.txt'))
    foreach($needle in @("waitlog guest 'save_handoff=exit; how=wm_close' 60",'waitexit guest 90','applyhandoff guest','-LabSetting guest:save_handoff=scene')){
        Assert-True ($restart.Contains($needle)) ('handoff-restart covers: '+$needle)
    }
    $after=[IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\worker\scenarios\handoff-after-restart.txt'))
    Assert-True ($after.Contains('-GuestSaveSeedRun <R4 runId>') -and $after.Contains("waitlog guest 'save_handoff=need; need=0' 120")) 'handoff-after-restart seeds the guest from the restart run'
    # Execute the actual dispatch clause with a recording helper: grammar alone cannot catch a wrong role variable.
    $dispatch=@($scenarioAst.FindAll({param($node)$node -is [Management.Automation.Language.SwitchStatementAst]},$true) |
        ForEach-Object {$_.Clauses} | Where-Object {$_.Item2.Extent.Text.Contains('Invoke-ScenarioWorldTravel')})
    Assert-True ($dispatch.Count -eq 1) 'one actual owned world-trigger dispatch clause'
    $dispatchText=$dispatch[0].Item2.Extent.Text
    $dispatchBlock=[scriptblock]::Create($dispatchText.Substring(1,$dispatchText.Length-2))
    function Invoke-ScenarioWorldTravel([string]$Text,[string]$Role){$script:triggerDispatchRecord=$Role+'|'+$Text}
    $role='wrong-role'
    foreach($r in @('host','guest')){
        $t=@($r,'worldtrigger',$(if($r -ceq 'host'){'goto=30020'}else{'world_probe=shared_death'}))
        & $dispatchBlock
        Assert-True ($script:triggerDispatchRecord -ceq ($r+'|'+$t[2])) ('world trigger passes actual target '+$r)
    }
    $Mode='play';$steps=@('host worldtrigger goto=30020');$failed=$false;try{& $validator}catch{$failed=$true}
    Assert-True $failed 'hidden travel refuses play mode'
    $steps=@('host worldtrigger world=0');$failed=$false;try{& $validator}catch{$failed=$true}
    Assert-True $failed 'hidden travel refuses play-mode world disable';$Mode='lab'
    foreach($r in @('host','guest')){
        $t=@($r,'worldtrigger','world=0')
        & $dispatchBlock
        Assert-True ($script:triggerDispatchRecord -ceq ($r+'|world=0')) ('world disable passes actual target '+$r)
    }
    $steps=@('host hold shift+w+space 200 nowait','guest burst dive 8 120','host hold w+jump 200')
    $failure=$null
    try { & $validator } catch { $failure=$_.Exception.Message }
    Assert-True ($failure -and $failure.Contains("host hold w+jump 200")) 'the validator refuses an unknown key'
    $actions=[IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\worker\scenarios\partner-native-actions.txt'))
    foreach($label in @('heldcapture host guest shift+w sprint 5','guest burst sprint-stop 4 300','heldcapture guest host shift+w g-sprint 5',
        'heldcapture host guest w crawl 5','host hold shift+w+space 200 nowait','guest burst dive 8 120','heldcapture host host space leak-evade-host 1',
        'host hold shift+d 600 nowait','host hold space+a 300 nowait','guest burst action-census 4 150','guest burst vault-probe 6 150')) {
        Assert-True ($actions.Contains($label)) ('partner-native-actions covers: '+$label)
    }
}
# -LabSetting: one allow-list in Start-WorkerSession.ps1 and Invoke-WorkerScenario.ps1,
# and the scenario labsetting step admits exactly the same keys.
& {
    $patterns=foreach($ast in @($startAst,$scenarioAst)){
        $param=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.ParameterAst] -and $node.Name.VariablePath.UserPath -ceq 'LabSetting'},$false))
        Assert-True ($param.Count -eq 1) 'one -LabSetting parameter'
        $validate=@($param[0].Attributes|Where-Object{$_ -is [Management.Automation.Language.AttributeAst] -and $_.TypeName.Name -ceq 'ValidatePattern'})
        Assert-True ($validate.Count -eq 1) '-LabSetting ValidatePattern'
        [string]$validate[0].PositionalArguments[0].Value
    }
    Assert-True (@($patterns).Count -eq 2 -and $patterns[0] -ceq $patterns[1]) 'Start-WorkerSession and Invoke-WorkerScenario share one -LabSetting allow-list'
    $scenarioText=[IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\worker\Invoke-WorkerScenario.ps1'))
    $step=[regex]::Match($scenarioText,'if\(\$line -cnotmatch ''(\^labsetting[^'']+)''\)')
    Assert-True $step.Success 'the scenario labsetting step regex'
    foreach($ok in @('steam_selftest=1','steam_selftest_fn=0','steam_selftest_fn=1','coop_hud=compact','coop_hud=full','coop_hud=off','host:coop_hud=full','guest:nametag=0')){
        Assert-True ($ok -cmatch $patterns[0]) ('-LabSetting admits '+$ok)
        $line='labsetting '+($ok -creplace '^(host|guest):','$1 ')
        Assert-True ($line -cmatch $step.Groups[1].Value) ('labsetting step admits '+$line)
    }
    foreach($bad in @('coop_hud=big','coop_hud=','steam_selftest_fn=2','net_steam_dispatch=both','net_transport=steam','coop_hud=full;x=1','steam_selftest=3')){
        Assert-True ($bad -cnotmatch $patterns[0]) ('-LabSetting refuses '+$bad)
        Assert-True (('labsetting '+$bad) -cnotmatch $step.Groups[1].Value) ('labsetting step refuses '+$bad)
    }
}
# The aimturn stimulus is the qualified +-1500 only (dx 700/900 never turned a settled aimed body).
& {
    $parts=@()
    foreach($name in @('Test-ScenarioKeys','Test-ScenarioLabel','ConvertFrom-ScenarioPatternTokens')) {
        $parts+=@($scenarioAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))[0].Extent.Text
    }
    foreach($variable in @('roleNames','keyNames')) {
        $parts+=@($scenarioAst.FindAll({param($node)$node -is [Management.Automation.Language.AssignmentStatementAst] -and
            $node.Left.Extent.Text -ceq ('$'+$variable)},$false))[0].Extent.Text
    }
    $loop=@($scenarioAst.FindAll({param($node)$node -is [Management.Automation.Language.ForEachStatementAst] -and
        $node.Condition.Extent.Text -ceq '$steps' -and $node.Body.Extent.Text.Contains('switch -Regex')},$false))[0]
    $validator=[scriptblock]::Create(($parts -join "`n")+"`n"+$loop.Extent.Text)
    foreach($case in @(@('aimturn host guest 1500 a',$true),@('aimturn guest host -1500 b',$true),@('aimturn host guest 900 c',$false),
                       @('aimturn host guest -700 d',$false),@('aimturn host guest 1501 e',$false))) {
        $steps=@($case[0]);$failure=$null
        try { & $validator } catch { $failure=$_.Exception.Message }
        Assert-True ([bool]!$failure -eq $case[1]) ('aimturn stimulus '+$case[0])
    }
    $facing=[IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\worker\scenarios\partner-native-facing.txt'))
    foreach($line in @('aimturn host guest 1500 face-aimturn','aimturn host guest -1500 face-aimturn-l','heldcapture host guest rmb+d face-aim-strafe 4',
        'heldcapture host guest rmb face-crouch-aim 4','guest debug aimpt off','guest debug aimpt lab')) {
        Assert-True ($facing.Contains($line)) ('partner-native-facing covers: '+$line)
    }
}
# The -LabSetting allow-list: Invoke-WorkerScenario and Start-WorkerSession carry the identical pattern; the
# scenario's labsetting line takes the same keys; the aim-target mirror's off|look|point (gun stage only).
& {
    $patterns=@(foreach($path in @((Join-Path $RepoRoot 'tools\worker\Invoke-WorkerScenario.ps1'),(Join-Path $SourceDirectory 'Start-WorkerSession.ps1'))){
        $tokens=$null;$errors=$null
        $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
        $param=@($ast.ParamBlock.Parameters|Where-Object{$_.Name.VariablePath.UserPath -ceq 'LabSetting'})[0]
        @($param.Attributes|Where-Object{$_.TypeName.Name -ceq 'ValidatePattern'})[0].PositionalArguments[0].Value
    })
    Assert-True ($patterns.Count -eq 2 -and $patterns[0] -ceq $patterns[1]) 'the two -LabSetting allow-lists are identical'
    foreach($value in @('partner_native_aimpt=off','partner_native_aimpt=look','partner_native_aimpt=point','guest:partner_native_aimpt=look',
        'partner_native_aim=looksrc','partner_native_face=aim')){Assert-True ($value -cmatch $patterns[0]) ('-LabSetting admits '+$value)}
    foreach($value in @('partner_native_aimpt=on','partner_native_aimpt=','partner_native_aimpt=LOOK','partner_native_aimpt=look ')){
        Assert-True ($value -cnotmatch $patterns[0]) ('-LabSetting refuses '+$value)
    }
    $scenarioText=[IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\worker\Invoke-WorkerScenario.ps1'))
    Assert-True ($scenarioText.Contains('partner_native_aimpt=(?:off|look|point)|steam_selftest=[012]') -and
        $scenarioText.Contains("throw '-LabSetting partner_native_aimpt needs -NativeStage gun.'")) 'the scenario labsetting line takes partner_native_aimpt (gun stage)'
    Assert-True ($startText.Contains("throw '-LabSetting partner_native_aimpt needs -NativeStage gun.'")) 'Start-WorkerSession: partner_native_aimpt in the gun stage only'
    Assert-True ((ConvertTo-LabPartnerValue -Key partner_native_aimpt -Value point) -ceq 'point') 'partner_native_aimpt is a LabCommon key'
    Assert-Throws {ConvertTo-LabPartnerValue -Key partner_native_aimpt -Value on} 'partner_native_aimpt values'
}
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
    $triggerGame=Join-Path $fixture 'trigger-game'
    $triggerMod=Join-Path $triggerGame 'mgs-coop'
    $null=New-Item -ItemType Directory -Path $triggerMod
    [IO.File]::WriteAllText((Join-Path $triggerMod 'world.trigger'),'goto=30020')
    [IO.File]::WriteAllText((Join-Path $triggerMod ('world.trigger.'+('a'*32)+'.tmp')),'partial')
    [IO.File]::WriteAllText((Join-Path $triggerMod 'world.trigger.user.tmp'),'keep')
    Assert-True ((Remove-WorkerWorldTrigger $triggerGame) -eq 2) 'only exact stale trigger names removed'
    Assert-True ((Test-Path -LiteralPath (Join-Path $triggerMod 'world.trigger.user.tmp'))) 'unrelated trigger file retained'
    Assert-True ((Remove-WorkerWorldTrigger $triggerGame) -eq 0) 'trigger cleanup is idempotent'
    $null=New-Item -ItemType Directory -Path (Join-Path $triggerMod 'world.trigger')
    Assert-Throws {Remove-WorkerWorldTrigger $triggerGame} 'directory pretending to be trigger refuses'
    Remove-Item -LiteralPath (Join-Path $triggerMod 'world.trigger')
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

    # A foreign archive/loader in the SECOND role blocks every mutation,
    # even while the first role remains a coherent owned worker.
    foreach($tamper in @('Archive','Loader')){
        $case=New-WorkerCleanupFixture ('changed-pair-'+$tamper)
        $script:fixtureSession=$case.session;$script:packageCalls=0
        $hostPair=Get-InstancePaths -GameDirectory $case.session.roles.host.game
        $guestPair=Get-InstancePaths -GameDirectory $case.session.roles.guest.game
        [IO.File]::WriteAllText($guestPair.$tamper,"foreign pair bytes must survive`n")
        $beforeLoader=Get-WorkerHash $hostPair.Loader
        Assert-Throws {Invoke-WorkerRestoreOwnedSession $case.session $case.run $RepoRoot $case.pins $case.report} ('changed second role pair '+$tamper)
        Assert-True ($script:packageCalls -eq 0 -and (Get-WorkerHash $hostPair.Loader) -ceq $beforeLoader) 'all-role loader/archive preflight before first rollback'
    }
    # Actual Stop function with a recording catalog/parser boundary, on tiny
    # synthetic archive bytes. Exact real hash admission was checked above.
    & {
        $case=New-WorkerCleanupFixture 'candidate-archive-rollback'
        $script:fixtureSession=$case.session;$script:packageCalls=0
        $hostPair=Get-InstancePaths -GameDirectory $case.session.roles.host.game
        $guestPair=Get-InstancePaths -GameDirectory $case.session.roles.guest.game
        $archiveBackup=Join-Path $case.run 'baseline-archive.dat'
        [IO.File]::Copy($hostPair.Archive,$archiveBackup,$false)
        [IO.File]::WriteAllText($hostPair.Archive,"fixture capacity-two archive`n")
        $mockHash=Get-WorkerHash $hostPair.Archive;$mockLength=(Get-Item -LiteralPath $hostPair.Archive).Length
        function Get-WorkerSessionRuntimeCatalog($Session,$Pins){
            [ordered]@{Profile='fixture-only-recording-catalog';Archive=$mockHash;Bytes=$mockLength;Motions=21;Capacity=2;Experimental=$true}
        }
        # The policy itself executes with this recording catalog, not a fake
        # acceptance boolean. No installer, game or process boundary is called.
        $pairDefinition=[scriptblock]::Create((Get-Command Get-WorkerRestorePair).ScriptBlock.ToString())
        function Get-WorkerRestorePair($Session,$Pins,[string]$BaselineLoader,[string]$CurrentLoader,[string]$CurrentArchive){
            & $pairDefinition $Session $Pins $BaselineLoader $CurrentLoader $CurrentArchive
        }
        function Invoke-WorkerPackageRestore([string]$Repo,[string]$Game,[string]$Package,[string]$Role){
            ++$script:packageCalls
            Assert-True ($Role -ceq 'host' -and (Get-WorkerHash (Get-InstancePaths $Game).Archive) -ceq $mockHash) 'candidate archive remains installed until transaction validates after hash'
            $p=Get-InstancePaths $Game
            [IO.File]::Copy((Join-Path $Package 'payload\dinput8.dll'),$p.Loader,$true)
            [IO.File]::Copy($script:fixtureSession.roles[$Role].baseline_record,$p.Record,$true)
            [IO.File]::Copy($archiveBackup,$p.Archive,$true)
        }
        Invoke-WorkerRestoreOwnedSession $case.session $case.run $RepoRoot $case.pins $case.report
        Assert-True ($script:packageCalls -eq 1 -and (Get-WorkerHash $hostPair.Archive) -ceq $case.pins.Archive -and
            (Get-WorkerHash $guestPair.Archive) -ceq $case.pins.Archive) 'partial candidate preparation restores host archive transaction and keeps guest baseline'
    }
    $case=New-WorkerCleanupFixture 'live-game'
    $script:fixtureSession=$case.session;$script:packageCalls=0;$script:fakeActiveGame=$case.session.roles.host.game
    Assert-Throws {Invoke-WorkerRestoreOwnedSession $case.session $case.run $RepoRoot $case.pins $case.report} 'fake live owned game blocks restoration'
    Assert-True ($script:packageCalls -eq 0 -and $case.report.roles.Count -eq 0) 'process-context refusal cannot restore package or configs'
    # ---- Join-fix phase 2, track E (docs/join-fix-phase2-spec.md P2-19): the actual helper
    # bodies on owned fixture bytes below this test's build folder; recorded clock and leases.
    # Stop: a role recorded as exited by waitexit tolerates a reused PID; every other child refuses.
    & {
        foreach($name in @('Test-WorkerRoleExited','Get-WorkerStopChildProcess')){
            $definition=@($stopAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
            Assert-True ($definition.Count -eq 1) ('actual exited-role boundary '+$name)
            . ([scriptblock]::Create($definition[0].Extent.Text))
        }
        $stopText=[IO.File]::ReadAllText((Join-Path $SourceDirectory 'Stop-WorkerSession.ps1'))
        Assert-True ($stopText.Contains('$childProcess=Get-WorkerStopChildProcess $session $child') -and $stopText.Contains('exited_roles')) 'Stop closes children through the exited-role boundary'
        $lease=[pscustomobject]@{mode='reused'}
        function Get-WorkerLeasedProcess($Expected){
            if($lease.mode -ceq 'reused'){throw 'Worker PID now names a different process; refusing ownership.'}
            if($lease.mode -ceq 'gone'){return $null}
            return [pscustomobject]@{fake=$true;pid=$Expected.pid}
        }
        $exitedChild=@{role='guest';pid=4242;creation_filetime='777';exe='C:\g\mgsvtpp.exe'}
        $liveChild=@{role='host';pid=4243;creation_filetime='778';exe='C:\h\mgsvtpp.exe'}
        $stopSession=@{roles=@{host=@{game='C:\h'};guest=@{game='C:\g';exited=@{pid=4242;creation_filetime='777';forced=$false}}}}
        Assert-True ((Test-WorkerRoleExited $stopSession $exitedChild) -and !(Test-WorkerRoleExited $stopSession $liveChild)) 'exit records name their exact lease'
        Assert-True ($null -eq (Get-WorkerStopChildProcess $stopSession $exitedChild)) 'an exited role tolerates its reused PID'
        Assert-Throws {Get-WorkerStopChildProcess $stopSession $liveChild} 'a live role still refuses a reused PID'
        $otherLease=$exitedChild.Clone();$otherLease.creation_filetime='999'
        Assert-Throws {Get-WorkerStopChildProcess $stopSession $otherLease} 'an exit record of another lease excuses nothing'
        $lease.mode='gone';Assert-True ($null -eq (Get-WorkerStopChildProcess $stopSession $liveChild)) 'a gone child needs no close'
        $lease.mode='live';Assert-True ((Get-WorkerStopChildProcess $stopSession $exitedChild).fake) 'a still-live leased child is closed as before'
    }
    # Start: -GuestSaveSeedRun reads only one exact, closed earlier run's guest copy; the actual seed chain copies it.
    & {
        $definition=@($startAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-WorkerSeedRunSaves'},$true))
        Assert-True ($definition.Count -eq 1) 'one actual seed-run boundary'
        . ([scriptblock]::Create($definition[0].Extent.Text))
        $fakeRepo=Join-Path $fixture 'fake-repo';$seedId='abcdefabcdefabcdefabcdefabcdef01'
        $seedRun=Join-Path $fakeRepo ('build\worker-runtime\'+$seedId)
        $seedSaves=Join-Path $seedRun 'guest-saves'
        $null=New-Item -ItemType Directory -Path (Join-Path $seedSaves '287700\local'),(Join-Path $seedSaves 'remote') -Force
        [IO.File]::WriteAllText((Join-Path $seedSaves '287700\local\TPP_GAME_DATA0'),"handed-off progress`n")
        [IO.File]::WriteAllText((Join-Path $seedSaves '287700\local\TPP_GRAPHICS_CONFIG'),"guest graphics`n")
        [IO.File]::WriteAllText((Join-Path $seedSaves 'remote\TPP_GAME_DATA'),"handed-off remote`n")
        [IO.File]::WriteAllText((Join-Path $seedSaves 'imported-progress.json'),"{}`n")
        $seedSessionPath=Join-Path $seedRun 'session.json'
        Write-WorkerSessionJson $seedSessionPath ([ordered]@{schema=1;run_id=$seedId;status='closed'})
        $before=@(Get-WorkerCopyInventory $seedSaves)
        Assert-True ((Get-WorkerSeedRunSaves $fakeRepo $seedId $id) -ieq $seedSaves) 'a closed earlier run seeds the guest'
        Assert-Throws {Get-WorkerSeedRunSaves $fakeRepo $seedId $seedId} 'the current run cannot seed itself'
        Assert-Throws {Get-WorkerSeedRunSaves $fakeRepo $seedId.ToUpperInvariant() $id} 'the seed run id is exact lower case'
        Assert-Throws {Get-WorkerSeedRunSaves $fakeRepo ('0'*32) $id} 'a missing seed run refuses'
        foreach($bad in @(@{schema=1;run_id=$seedId;status='ready'},@{schema=1;run_id=$seedId;status='cleanup_refused'},@{schema=1;run_id=('0'*32);status='closed'})){
            Write-WorkerSessionJson $seedSessionPath $bad
            Assert-Throws {Get-WorkerSeedRunSaves $fakeRepo $seedId $id} ('a seed run must be closed and own its id: '+$bad.status)
        }
        Write-WorkerSessionJson $seedSessionPath ([ordered]@{schema=1;run_id=$seedId;status='closed'})
        Rename-Item -LiteralPath (Join-Path $seedSaves 'remote') -NewName 'remote-moved'
        Assert-Throws {Get-WorkerSeedRunSaves $fakeRepo $seedId $id} 'a seed copy without remote refuses'
        Rename-Item -LiteralPath (Join-Path $seedSaves 'remote-moved') -NewName 'remote'
        Rename-Item -LiteralPath (Join-Path $seedSaves '287700\local\TPP_GAME_DATA0') -NewName 'TPP_CONFIG_DATA0'
        Assert-Throws {Get-WorkerSeedRunSaves $fakeRepo $seedId $id} 'a seed copy without TPP_GAME_DATA refuses'
        Rename-Item -LiteralPath (Join-Path $seedSaves '287700\local\TPP_CONFIG_DATA0') -NewName 'TPP_GAME_DATA0'
        $chain=@($startAst.FindAll({param($node)$node -is [Management.Automation.Language.IfStatementAst] -and $node.Clauses[0].Item1.Extent.Text -ceq "`$roleSeed -ceq 'lab'"},$true))
        Assert-True ($chain.Count -eq 1) 'one actual role seed chain'
        $roleSeed='run';$GuestSaveSeedRun=$seedId;$repo=$fakeRepo;$runId=$id;$freshBackup=Join-Path $fixture 'no-backup'
        $saveRoot=Join-Path $fixture 'run-seed\guest-saves';$null=New-Item -ItemType Directory -Path (Join-Path $fixture 'run-seed')
        . ([scriptblock]::Create($chain[0].Extent.Text))
        $copied=@(Get-WorkerCopyInventory $saveRoot)
        Assert-True ($seed.Seeded -and $seed.Files -eq 3 -and $copied.Count -eq 3) 'the run seed copies exactly 287700 and remote'
        foreach($row in $copied){
            $source=@($before|Where-Object{$_['path'] -ceq $row['path']})
            Assert-True ($source.Count -eq 1 -and $source[0]['sha256'] -ceq $row['sha256']) ('the run seed copies '+$row['path']+' unchanged')
        }
        $afterSeed=@(Get-WorkerCopyInventory $seedSaves)
        Assert-True ($afterSeed.Count -eq $before.Count -and @(for($i=0;$i -lt $before.Count;$i++){if($before[$i]['sha256'] -cne $afterSeed[$i]['sha256']){1}}).Count -eq 0) 'the seed run stays unchanged'
    }
    # Invoke-WorkerScenario: waitlog, expectstate, waitexit and applyhandoff helper bodies.
    & {
        foreach($name in @('Add-ScenarioExpectation','Get-ScenarioStepRoles','ConvertFrom-ScenarioPatternTokens','New-ScenarioLogCursor','Read-ScenarioLogLines',
            'Wait-ScenarioLog','Get-ScenarioStateValue','Get-ScenarioRoleLease','Wait-ScenarioRoleExit','Get-ScenarioHandoffSlots','Get-ScenarioHandoffSetSha256',
            'ConvertTo-ScenarioUtc','Test-ScenarioHandoffStage','Get-ScenarioSaveHashes','Test-ScenarioSameMap','Format-ScenarioMap','New-ScenarioHandoffApply','Invoke-ScenarioHandoffApply')){
            $definition=@($scenarioAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
            Assert-True ($definition.Count -eq 1) ('actual phase-2 scenario helper '+$name)
            . ([scriptblock]::Create($definition[0].Extent.Text))
        }
        $scenarioText=[IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\worker\Invoke-WorkerScenario.ps1'))
        Assert-True ($scenarioText.Contains('$gone=@(Get-ScenarioStepRoles $t|Where-Object{$exitedRoles.ContainsKey($_)})') -and
            $scenarioText.Contains("foreach(`$role in 'host','guest'){if(!`$exitedRoles.ContainsKey(`$role)){Release-RoleFinally `$role}}") -and
            $scenarioText.Contains('if($stop -and $stop.Closed){$pendingHandoff=Invoke-ScenarioHandoffApply $pendingHandoff}')) 'the runner fails fast on an exited role and applies only after a closed Stop'
        $expectations=[Collections.Generic.List[string]]::new();$expectFailures=[Collections.Generic.List[string]]::new()
        $runId=$id;$exitedRoles=@{};$clock=[pscustomobject]@{now=[int64]1000000};$repo=$RepoRoot
        function Get-ScenarioTick{return $clock.now}
        function Start-Sleep([int]$Milliseconds){$clock.now+=$Milliseconds}
        # Step roles: a role that left through waitexit may only be read (waitlog) or applied.
        foreach($case in @(@('wait 100',''),@('state',''),@('waitlog guest x 5',''),@('applyhandoff guest',''),@('title','host,guest'),@('title 200','host,guest'),
            @('title guest 200','guest'),@('titlemenu both 240','host,guest'),@('titlemenu host 240','host'),@('waitmission guest 30010 300','guest'),
            @('expectstate guest mission x','guest'),@('waitexit guest 90','guest'),@('placebehind host guest 6','host,guest'),
            @('heldcapture host guest w lbl 1','host,guest'),@('both press enter','host,guest'),@('guest press enter','guest'),@('host run status','host'))){
            Assert-True ((@(Get-ScenarioStepRoles ($case[0] -split '\s+')) -join ',') -ceq $case[1]) ('step roles of '+$case[0])
        }
        Assert-True ((ConvertFrom-ScenarioPatternTokens @("'save_handoff=exit;","how=wm_close'")) -ceq 'save_handoff=exit; how=wm_close') 'a quoted pattern keeps its space'
        Assert-True ((ConvertFrom-ScenarioPatternTokens @("'save_handoff=offer;","'")) -ceq 'save_handoff=offer; ') 'a quoted trailing space survives the split'
        Assert-True ((ConvertFrom-ScenarioPatternTokens @('^save_handoff=')) -ceq '^save_handoff=') 'an unquoted pattern'
        Assert-True ($null -eq (ConvertFrom-ScenarioPatternTokens @("''")) -and $null -eq (ConvertFrom-ScenarioPatternTokens @('(')) -and
            $null -eq (ConvertFrom-ScenarioPatternTokens @())) 'empty and invalid patterns refuse'
        # waitlog: this session's lines only (from the recorded offset), complete lines only.
        $run=Join-Path $fixture 'scenario-run';$null=New-Item -ItemType Directory -Path $run
        $hostGame=Join-Path $fixture 'scenario-games\host';$guestGame=Join-Path $fixture 'scenario-games\guest'
        $null=New-Item -ItemType Directory -Path (Join-Path $hostGame 'mgs-coop'),(Join-Path $guestGame 'mgs-coop') -Force
        $log=Join-Path $hostGame 'mgs-coop\script-observer.log'
        [IO.File]::WriteAllText($log,"save_handoff=staged; xfer=OLD; files=1`r`n")
        $offset=(Get-Item -LiteralPath $log).Length
        $sessionRecord=[ordered]@{schema=1;run_id=$id;status='ready';roles=[ordered]@{
            host=[ordered]@{game=$hostGame;log_offsets=[ordered]@{'script-observer.log'=[ordered]@{path=$log;offset=$offset}}}
            guest=[ordered]@{game=$guestGame;log_offsets=[ordered]@{'script-observer.log'=[ordered]@{path=(Join-Path $guestGame 'mgs-coop\script-observer.log');offset=0}}}}}
        Write-WorkerSessionJson (Join-Path $run 'session.json') $sessionRecord
        [IO.File]::AppendAllText($log,"save_handoff=configured; role=host; mode=stage; channel=4; dir=ok`r`nsave_handoff=offer; xfer=0A1B2C3D; files=3`r`npartial save_handoff=exit")
        Assert-True ((Wait-ScenarioLog 'host' '^save_handoff=offer; ' 5) -ceq 'save_handoff=offer; xfer=0A1B2C3D; files=3') 'waitlog finds a session line without CR'
        Assert-True ($null -eq (Wait-ScenarioLog 'host' 'xfer=OLD' 1)) 'waitlog ignores lines before the session offset'
        $before=$clock.now
        Assert-True ($null -eq (Wait-ScenarioLog 'host' 'save_handoff=exit' 2) -and $clock.now-$before -ge 2000) 'waitlog never takes an unterminated line and times out'
        $cursor=New-ScenarioLogCursor 'host'
        Assert-True (@(Read-ScenarioLogLines $cursor).Count -eq 2 -and @(Read-ScenarioLogLines $cursor).Count -eq 0) 'the cursor reads each complete line once'
        [IO.File]::AppendAllText($log,"; how=wm_close`r`n")
        Assert-True ((@(Read-ScenarioLogLines $cursor) -join '|') -ceq 'partial save_handoff=exit; how=wm_close') 'a completed partial line is read whole'
        Assert-True ((Wait-ScenarioLog 'host' 'save_handoff=exit; how=wm_close' 1) -ceq 'partial save_handoff=exit; how=wm_close') 'waitlog matches anywhere in the line'
        [IO.File]::WriteAllText($log,"short`r`n")
        Assert-Throws {Read-ScenarioLogLines $cursor} 'a log truncated below the cursor refuses'
        $sessionRecord.roles.host.log_offsets['script-observer.log'].path=Join-Path $fixture 'elsewhere.log'
        Write-WorkerSessionJson (Join-Path $run 'session.json') $sessionRecord
        Assert-Throws {New-ScenarioLogCursor 'host'} 'waitlog reads only the owned game folder log'
        $sessionRecord.roles.host.log_offsets['script-observer.log'].path=$log
        Assert-True ($null -eq (Wait-ScenarioLog 'guest' 'anything' 1)) 'a role without a log yet simply waits'
        # expectstate: fresh owned state only; booleans as true/false.
        $fixtureState=@{run_id=$id;role='guest';tick_ms=$clock.now;mission=40050;handoff='staged';handoff_pct=100;travel_unlocked=0;gameplay_ready=$false}
        function Read-RoleRaw([string]$Role){return $fixtureState.Clone()}
        foreach($case in @(@('mission','40050'),@('handoff','staged'),@('handoff_pct','100'),@('travel_unlocked','0'),@('gameplay_ready','false'))){
            $v=Get-ScenarioStateValue 'guest' $case[0]
            Assert-True ($v.Ok -and $v.Value -ceq $case[1]) ('expectstate reads '+$case[0])
        }
        Assert-True (!(Get-ScenarioStateValue 'guest' 'world_paired').Ok) 'a missing field fails'
        Assert-True (!(Get-ScenarioStateValue 'host' 'mission').Ok) 'another role''s state fails'
        $fixtureState.tick_ms=$clock.now-3001;Assert-True (!(Get-ScenarioStateValue 'guest' 'mission').Ok) 'stale state fails'
        $fixtureState.tick_ms=$clock.now+1;Assert-True (!(Get-ScenarioStateValue 'guest' 'mission').Ok) 'future state fails'
        $fixtureState.tick_ms=$clock.now;$fixtureState.run_id='f'*32;Assert-True (!(Get-ScenarioStateValue 'guest' 'mission').Ok) 'foreign state fails'
        Add-ScenarioExpectation 'guest' 'expectstate handoff' '^staged$' 'staged' $true
        Add-ScenarioExpectation 'guest' 'expectstate handoff' '^staged$' 'countdown' $false -WarningAction SilentlyContinue
        Assert-True ($expectations.Count -eq 2 -and $expectFailures.Count -eq 1 -and $expectFailures[0] -ceq 'guest expectstate handoff ^staged$ <- countdown') 'checks are recorded like expect'
        # waitexit: the owned lease must end while the supervisor keeps running.
        $creation=[DateTime]::new(2026,10,15,12,0,0,[DateTimeKind]::Utc).ToFileTimeUtc()
        $supervisor=[ordered]@{run_id=$id;stage='running';owned_processes=@(
            [ordered]@{role='host';pid=4243;creation_filetime=[string]($creation+10000000);exe=(Join-Path $hostGame 'mgsvtpp.exe');admitted=$true},
            [ordered]@{role='guest';pid=4242;creation_filetime=[string]$creation;exe=(Join-Path $guestGame 'mgsvtpp.exe');admitted=$true})}
        Write-WorkerSessionJson (Join-Path $run 'supervisor.json') $supervisor
        $fake=[pscustomobject]@{calls=0;exitAfter=3;ExitCode=0;disposed=$false;mode='live'}
        $fake|Add-Member -MemberType ScriptMethod -Name WaitForExit -Value {param($ms) $this.calls++;$clock.now+=$ms;return $this.calls -ge $this.exitAfter}
        $fake|Add-Member -MemberType ScriptMethod -Name Dispose -Value {$this.disposed=$true}
        function Get-WorkerLeasedProcess($Expected){
            if($fake.mode -ceq 'reused'){throw 'Worker PID now names a different process; refusing ownership.'}
            Assert-True ([long]$Expected.pid -in 4242,4243) 'waitexit leases the supervisor''s owned pid'
            return $fake
        }
        $fake.exitAfter=1000000
        Assert-Throws {Wait-ScenarioRoleExit 'host' 5} 'waitexit times out when the game stays'
        Assert-True ($fake.disposed -and !$exitedRoles.ContainsKey('host')) 'a timed-out waitexit records nothing'
        $supervisor.stage='closing';Write-WorkerSessionJson (Join-Path $run 'supervisor.json') $supervisor;$fake.mode='reused'
        Assert-Throws {Wait-ScenarioRoleExit 'guest' 5} 'an exit while the supervisor closes is not the game''s own'
        $supervisor.stage='running';Write-WorkerSessionJson (Join-Path $run 'supervisor.json') $supervisor
        $fake.mode='live';$fake.calls=0;$fake.exitAfter=3;$fake.disposed=$false
        Wait-ScenarioRoleExit 'guest' 90
        $recorded=Read-WorkerSessionJson (Join-Path $run 'session.json')
        Assert-True ($fake.disposed -and $exitedRoles.ContainsKey('guest') -and [long]$recorded['roles']['guest']['exited']['pid'] -eq 4242 -and
            [string]$recorded['roles']['guest']['exited']['creation_filetime'] -ceq [string]$creation -and $recorded['roles']['guest']['exited']['forced'] -eq $false -and
            [int]$recorded['roles']['guest']['exited']['exit_code'] -eq 0) 'waitexit records the exact lease, exit code and forced=false in session.json'
        $supervisor.owned_processes=@($supervisor.owned_processes[0]);Write-WorkerSessionJson (Join-Path $run 'supervisor.json') $supervisor
        Assert-Throws {Wait-ScenarioRoleExit 'guest' 5} 'waitexit needs the role''s owned lease'
        # The P2-6 set hash: a golden vector and an independent byte layout.
        $golden=@(@{entry='remote/TPP_GAME_DATA';bytes=0x01020304;sha256='B71199EBD070B36BEAB7317920C2C2F1D777DF8D05E5527D8458FDA57CB17A7A'},
            @{entry='287700/local/TPP_GAME_DATA0';bytes=1;sha256=('0'*64)})
        Assert-True ((Get-ScenarioHandoffSetSha256 $golden) -ceq '72096957C1A4E997965569CF00AD27F2C3BE607B98D7CFA8C1F36A64B261AA86') 'set hash golden vector (ascending slots, u32 LE sizes)'
        Assert-Throws {Get-ScenarioHandoffSetSha256 @(@{entry='287700/local/TPP_CONFIG_DATA0';bytes=1;sha256=('0'*64)})} 'settings files are never part of a set'
        function Get-FixtureSetSha([object[]]$Rows){
            $slotOf=@{'287700/local/TPP_GAME_DATA0'=0;'287700/local/TPP_GAME_DATA1'=1;'287700/local/PERSONAL_DATA0'=2;'287700/local/PERSONAL_DATA1'=3;'remote/TPP_GAME_DATA'=4;'remote/PERSONAL_DATA'=5}
            $bytes=[Collections.Generic.List[byte]]::new()
            foreach($r in @($Rows|Sort-Object {$slotOf[$_['entry']]})){
                $n=[long]$r['bytes'];$bytes.Add([byte]$slotOf[$r['entry']])
                foreach($shift in 0,8,16,24){$bytes.Add([byte](($n -shr $shift) -band 0xFF))}
                $bytes.AddRange([Convert]::FromHexString($r['sha256']))
            }
            return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes.ToArray()))
        }
        # A staged handoff in an owned fixture run (the native guest's P2-9 layout).
        function New-FixtureHandoffRun([string]$Name){
            $fixtureRun=Join-Path $fixture $Name
            $saves=Join-Path $fixtureRun 'guest-saves'
            $null=New-Item -ItemType Directory -Path (Join-Path $saves '287700\local'),(Join-Path $saves 'remote') -Force
            foreach($pair in @(@('287700\local\TPP_GAME_DATA0','own progress 0'),@('287700\local\TPP_GAME_DATA1','own progress 1'),@('287700\local\PERSONAL_DATA0','own personal'),
                @('remote\TPP_GAME_DATA','own remote'),@('287700\local\TPP_CONFIG_DATA0','own settings'),@('287700\local\TPP_GRAPHICS_CONFIG','own graphics'))){
                [IO.File]::WriteAllText((Join-Path $saves $pair[0]),$pair[1]+"`n")
            }
            $stage=Join-Path $fixtureRun 'guest-saves-handoff\0A1B2C3D'
            $null=New-Item -ItemType Directory -Path (Join-Path $stage '287700\local'),(Join-Path $stage 'remote') -Force
            $rows=[Collections.Generic.List[object]]::new()
            foreach($pair in @(@('287700/local/TPP_GAME_DATA0','host progress 0, a longer file'),@('287700/local/PERSONAL_DATA0','host personal'),@('remote/TPP_GAME_DATA','host remote'))){
                $path=Join-Path $stage $pair[0].Replace('/','\');[IO.File]::WriteAllText($path,$pair[1]+"`n")
                $rows.Add([ordered]@{entry=$pair[0];bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-WorkerHash $path)})
            }
            $set=Get-FixtureSetSha $rows.ToArray()
            Write-WorkerSessionJson (Join-Path $stage 'handoff.json') ([ordered]@{format='mgs-coop-save-handoff';v=1;xfer='0A1B2C3D';created_utc='2026-10-15T12:05:00Z';
                host_build='0.13.0+fixture';host_mission=30010;set_sha256=$set;files=$rows.ToArray()})
            $request=[ordered]@{format='mgs-coop-save-handoff-request';v=1;pid=4242;process_start_utc='2026-10-15T12:00:00Z';xfer='0A1B2C3D';set_sha256=$set;mode='scene';rejoin=0;created_utc='2026-10-15T12:05:01Z'}
            Write-WorkerSessionJson (Join-Path $fixtureRun 'guest-saves-handoff\request.json') $request
            return [pscustomobject]@{Run=$fixtureRun;Saves=$saves;Stage=$stage;Root=(Join-Path $fixtureRun 'guest-saves-handoff');Set=$set}
        }
        $h=New-FixtureHandoffRun 'handoff-run-1'
        $requestDoc=Read-WorkerSessionJson (Join-Path $h.Root 'request.json')
        $checked=Test-ScenarioHandoffStage $requestDoc $h.Root 4242 ([string]$creation)
        Assert-True ($checked.Xfer -ceq '0A1B2C3D' -and $checked.SetSha256 -ceq $h.Set -and @($checked.Files).Count -eq 3 -and $checked.Mode -ceq 'scene' -and
            $checked.CreatedUtc -ceq '2026-10-15T12:05:00Z') 'a valid staged handoff is admitted with its rows'
        Assert-Throws {Test-ScenarioHandoffStage $requestDoc $h.Root 4243 ([string]$creation)} 'the request must name the exited pid'
        Assert-Throws {Test-ScenarioHandoffStage $requestDoc $h.Root 4242 ([string]($creation+100000000))} 'the request must name the exited process start'
        foreach($change in @(@('mode','stage'),@('xfer','0a1b2c3d'),@('xfer','00000000'),@('set_sha256',('A'*64)),@('format','mgs-coop-save-handoff'),@('v',2))){
            $bad=$requestDoc.Clone();$bad[$change[0]]=$change[1]
            Assert-Throws {Test-ScenarioHandoffStage $bad $h.Root 4242 ([string]$creation)} ('request refuses '+$change[0]+'='+$change[1])
        }
        $stray=Join-Path $h.Stage 'remote\PERSONAL_DATA';[IO.File]::WriteAllText($stray,"unnamed`n")
        Assert-Throws {Test-ScenarioHandoffStage $requestDoc $h.Root 4242 ([string]$creation)} 'an unnamed staged file refuses'
        Remove-Item -LiteralPath $stray
        $staged0=Join-Path $h.Stage '287700\local\TPP_GAME_DATA0';$original=[IO.File]::ReadAllText($staged0)
        [IO.File]::WriteAllText($staged0,$original.Replace('host','HOST'))
        Assert-Throws {Test-ScenarioHandoffStage $requestDoc $h.Root 4242 ([string]$creation)} 'a staged byte change refuses'
        [IO.File]::WriteAllText($staged0,$original)
        $handoffPath=Join-Path $h.Stage 'handoff.json';$handoffText=[IO.File]::ReadAllText($handoffPath)
        $variants=@(
            @{name='unknown entry';edit={param($d) $d['files'][1]['entry']='287700/local/TPP_CONFIG_DATA0'}},
            @{name='repeated entry';edit={param($d) $d['files'][1]['entry']='287700/local/TPP_GAME_DATA0'}},
            @{name='size';edit={param($d) $d['files'][2]['bytes']=[long]$d['files'][2]['bytes']+1}},
            @{name='lower-case hash';edit={param($d) $d['files'][2]['sha256']=([string]$d['files'][2]['sha256']).ToLowerInvariant()}},
            @{name='no TPP_GAME_DATA0/1';edit={param($d) $d['files']=@($d['files'][1],$d['files'][2])}},
            @{name='xfer';edit={param($d) $d['xfer']='0A1B2C3E'}})
        foreach($variant in $variants){
            $doc=Read-WorkerSessionJson $handoffPath;& $variant.edit $doc;Write-WorkerSessionJson $handoffPath $doc
            Assert-Throws {Test-ScenarioHandoffStage $requestDoc $h.Root 4242 ([string]$creation)} ('handoff.json refuses '+$variant.name)
            [IO.File]::WriteAllText($handoffPath,$handoffText)
        }
        $null=Test-ScenarioHandoffStage $requestDoc $h.Root 4242 ([string]$creation)
        Assert-True $true 'the restored stage is admitted again'
        # applyhandoff: checked at step time, applied after Stop through Import-CoopProgressFiles (P2-18 contract).
        $applyLog=[Collections.Generic.List[hashtable]]::new();$importMode=[pscustomobject]@{mode='ok'}
        function Import-Module {param([Parameter(ValueFromRemainingArguments)]$Rest) $applyLog.Add(@{module=($Rest -join ' ')})}
        function Import-CoopProgressFiles {
            param([string]$StageRoot,[string]$SaveDir,[string]$ReplacedRoot,[string]$Suffix,[hashtable]$Marker,[object[]]$Manifest)
            $applyLog.Add(@{stage=$StageRoot;save=$SaveDir;replaced=$ReplacedRoot;suffix=$Suffix;source=$Marker.Source;files=@($Marker.Files).Count;exported=$Marker.ExportedUtc;manifest=@($Manifest).Count})
            if($importMode.mode -ceq 'throw'){throw 'fixture import refused'}
            $new=$SaveDir+'-new';Copy-Item -LiteralPath $SaveDir -Destination $new -Recurse
            foreach($sub in '287700\local','remote'){
                if($importMode.mode -cne 'keep-old'){foreach($old in @(Get-ChildItem -LiteralPath (Join-Path $new $sub) -File|Where-Object{$_.Name -cmatch '^(TPP_GAME_DATA[0-9]?|PERSONAL_DATA[0-9]?)$'})){Remove-Item -LiteralPath $old.FullName}}
                foreach($f in @(Get-ChildItem -LiteralPath (Join-Path $StageRoot $sub) -File)){Copy-Item -LiteralPath $f.FullName -Destination (Join-Path (Join-Path $new $sub) $f.Name) -Force}
            }
            [IO.File]::WriteAllText((Join-Path $new 'imported-progress.json'),(([ordered]@{format='mgs-coop-imported-progress';v=1;source=$Marker.Source}|ConvertTo-Json)+"`n"))
            $null=New-Item -ItemType Directory -Path $ReplacedRoot -Force
            $aside=Join-Path $ReplacedRoot ('20261015-120600'+$Suffix)
            Move-Item -LiteralPath $SaveDir -Destination $aside;Move-Item -LiteralPath $new -Destination $SaveDir
            return $aside
        }
        $exitedRoles=@{}
        $run=$h.Run
        Assert-Throws {New-ScenarioHandoffApply 'guest'} 'applyhandoff needs waitexit guest first'
        $exitedRoles['guest']=[ordered]@{pid=4242;creation_filetime=[string]$creation;forced=$false}
        Assert-Throws {New-ScenarioHandoffApply 'host'} 'applyhandoff is guest only'
        $pending=New-ScenarioHandoffApply 'guest'
        Assert-True ($pending.xfer -ceq '0A1B2C3D' -and @($pending.before.progress.Keys).Count -eq 4 -and @($pending.before.settings.Keys).Count -eq 2 -and !$pending.applied) 'the step records the copy before any change'
        Assert-True ((Test-WorkerDescendant $pending.replaced_root $run) -and (Test-WorkerDescendant $pending.save_dir $run) -and (Test-WorkerDescendant $pending.stage $run)) 'every apply path stays in the run'
        $expectations.Clear();$expectFailures.Clear()
        $result=Invoke-ScenarioHandoffApply $pending
        $call=@($applyLog|Where-Object{$_.ContainsKey('stage')})[-1]
        Assert-True ($call.stage -ceq $h.Stage -and $call.save -ceq $h.Saves -and $call.replaced -ceq (Join-Path $run 'guest-saves-replaced') -and $call.suffix -ceq '-before-handoff' -and
            $call.source -ceq 'handoff' -and $call.files -eq 3 -and $call.manifest -eq 3 -and $call.exported -ceq '2026-10-15T12:05:00Z') 'Import-CoopProgressFiles gets the run paths, -before-handoff, the handoff marker and the staged manifest'
        Assert-True ($result.applied -and !$result.failure -and $expectFailures.Count -eq 0 -and $expectations.Count -eq 6) ('the apply checks all pass: '+($expectFailures -join ' | '))
        Assert-True ($result.marker_source -ceq 'handoff' -and $result.aside.EndsWith('-before-handoff') -and @($result.after.progress.Keys).Count -eq 3 -and
            (Test-ScenarioSameMap $pending.before.progress $result.aside_progress)) 'scenario.json HandoffApply holds after, aside and its old progress'
        # Failures are recorded, never thrown past the run's cleanup.
        foreach($mode in 'throw','keep-old'){
            $h=New-FixtureHandoffRun ('handoff-run-'+$mode);$run=$h.Run;$importMode.mode=$mode
            $pending=New-ScenarioHandoffApply 'guest';$expectations.Clear();$expectFailures.Clear()
            $result=Invoke-ScenarioHandoffApply $pending -WarningAction SilentlyContinue
            if($mode -ceq 'throw'){Assert-True (!$result.applied -and $result.failure -ceq 'fixture import refused' -and $expectFailures.Count -eq 1) 'a refused import is one failed expectation'}
            else{Assert-True ($result.applied -and @($expectFailures|Where-Object{$_ -clike '*progress=staged*'}).Count -eq 1) 'old progress left behind fails progress=staged'}
        }
        $h=New-FixtureHandoffRun 'handoff-run-changed';$run=$h.Run;$importMode.mode='ok'
        $pending=New-ScenarioHandoffApply 'guest';$expectations.Clear();$expectFailures.Clear()
        [IO.File]::WriteAllText((Join-Path $h.Saves '287700\local\TPP_GAME_DATA0'),"written after the check`n")
        $result=Invoke-ScenarioHandoffApply $pending -WarningAction SilentlyContinue
        Assert-True (!$result.applied -and $result.failure -clike '*changed after applyhandoff*' -and !(Test-Path -LiteralPath (Join-Path $run 'guest-saves-replaced'))) 'a copy changed after the check is never replaced'
    }
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
# Worker-only graphics preset: pure retail text and recorded actual controller blocks.
& {
 function Graphics-Tag([string]$text){
  $tag=Get-GraphicsConfigTag ($text|ConvertFrom-Json)
  return [regex]::Replace($text,'("as"\s*:\s*")[A-Za-z0-9+/=]+(")',{param($m)$m.Groups[1].Value+$tag+$m.Groups[2].Value})
 }
 $tokens=$null;$errors=$null
 $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $RepoRoot 'tests\lab_scripts_tests.ps1'),[ref]$tokens,[ref]$errors)
 $literal=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$realConfig'},$true))
 Assert-True ($errors.Count -eq 0 -and $literal.Count -eq 1) 'graphics: exact existing retail config fixture'
 $inputText=& ([scriptblock]::Create($literal[0].Right.Extent.Text));$sourceText=$inputText
 Assert-True (Get-GraphicsConfigInfo $inputText).Valid 'graphics: actual retail tag'
 Assert-True ((Set-WorkerGraphicsQualityText -Text $inputText) -ceq $inputText) 'graphics: default preserve byte-identical'
 $map=[ordered]@{texture='Low';effect='Low';lighting='Low';model_detail='Low';postprocess='Low';shadow='Low';ssao='Off';texture_filtering='Medium'}
 $low=Set-WorkerGraphicsQualityText -Text $inputText -Quality low
 Assert-True ($inputText -ceq $sourceText -and (Get-GraphicsConfigInfo $low).Valid) 'graphics: source immutable and generated tag valid'
 $object=$low|ConvertFrom-Json
 foreach($key in $map.Keys){Assert-True ($object.graphics.quality_setting.$key -ceq $map[$key]) ('graphics: exact value '+$key)}
 Assert-True ((Set-WorkerGraphicsQualityText -Text $low -Quality low) -ceq $low) 'graphics: idempotent low'
 Assert-True ((Set-WorkerGraphicsQualityText -Text $inputText -Quality LOW) -ceq $low) 'graphics: case accepted consistently'
 function Graphics-Mask([string]$text){
  foreach($key in $map.Keys){$text=[regex]::Replace($text,'("'+[regex]::Escape($key)+'"\s*:\s*")[^"\\]*(")','$1QUALITY$2')}
  return [regex]::Replace($text,'("as"\s*:\s*")[A-Za-z0-9+/=]+(")','$1TAG$2')
 }
 Assert-True ((Graphics-Mask $low) -ceq (Graphics-Mask $inputText)) 'graphics: only quality/tag bytes change'
 $unknown=Graphics-Tag ($inputText.Replace('"project" : "tpp"','"future":{"array":[true,false,null,42],"escaped":"keep\"quoted","unknown":"ExtraHigh"}, "project" : "tpp"').Replace('"depth_of_field" : "Enable"','"future_quality" : "KeepExactly", "depth_of_field" : "Enable"'))
 $unknownLow=Set-WorkerGraphicsQualityText -Text $unknown -Quality low
 Assert-True ((Graphics-Mask $unknownLow) -ceq (Graphics-Mask $unknown) -and (Get-GraphicsConfigInfo $unknownLow).Valid) 'graphics: unknown settings preserved and retagged'
 $crlf=$unknown.Replace("`n","`r`n")
 Assert-True ((Graphics-Mask (Set-WorkerGraphicsQualityText -Text $crlf -Quality low)) -ceq (Graphics-Mask $crlf)) 'graphics: CRLF retained'
 Assert-Throws {Set-WorkerGraphicsQualityText -Text ($inputText.Replace('ExtraHigh','High')) -Quality low} 'graphics: corrupt tag refused'
 Assert-Throws {Set-WorkerGraphicsQualityText -Text '' -Quality low} 'graphics: empty refused'
 Assert-Throws {Set-WorkerGraphicsQualityText -Text (' '*65537) -Quality low} 'graphics: bounded'
 Assert-Throws {Set-WorkerGraphicsQualityText -Text $inputText -Quality medium} 'graphics: unsupported mode refused'
 foreach($schema in @($inputText.Replace('"project" : "tpp"','"project" : "other"'),$inputText.Replace('"version" : 1','"version" : 2'),
  $inputText.Replace('"version" : 1','"version" : "1"'),$inputText.Replace('"quality_setting"','"quality_future"'))){
  $bad=Graphics-Tag $schema;Assert-Throws {Set-WorkerGraphicsQualityText -Text $bad -Quality low} 'graphics: tag-valid wrong schema refused'
 }
 foreach($key in $map.Keys){
  $missing=Graphics-Tag ([regex]::Replace($inputText,'"'+[regex]::Escape($key)+'"\s*:\s*"ExtraHigh",?',''))
  Assert-Throws {Set-WorkerGraphicsQualityText -Text $missing -Quality low} ('graphics: missing '+$key)
  foreach($value in '1','false','null','[]','{"unexpected":1}'){
   $bad=Graphics-Tag ([regex]::Replace($inputText,'("'+[regex]::Escape($key)+'"\s*:\s*)"ExtraHigh"',{param($m)$m.Groups[1].Value+$value}))
   Assert-Throws {Set-WorkerGraphicsQualityText -Text $bad -Quality low} ('graphics: non-string '+$key+' '+$value)
  }
  $duplicate=Graphics-Tag ($inputText.Replace('"'+$key+'" : "ExtraHigh"','"'+$key+'" : "ExtraHigh", "'+$key+'" : "ExtraHigh"'))
  Assert-Throws {Set-WorkerGraphicsQualityText -Text $duplicate -Quality low} ('graphics: duplicate '+$key)
 }
 $duplicateAs=$inputText.Replace('"as" : "UzF+nvqVP+4="','"as" : "UzF+nvqVP+4=", "as" : "UzF+nvqVP+4="')
 Assert-Throws {Set-WorkerGraphicsQualityText -Text $duplicateAs -Quality low} 'graphics: duplicate tag'
 $duplicateUnknown=Graphics-Tag ($unknown.Replace('"unknown":"ExtraHigh"','"unknown":"ExtraHigh","unknown":"ExtraHigh"'))
 Assert-Throws {Set-WorkerGraphicsQualityText -Text $duplicateUnknown -Quality low} 'graphics: duplicate unknown'
 $escaped=Graphics-Tag ($inputText.Replace('"texture" : "ExtraHigh"','"texture" : "ExtraHigh", "\u0074exture" : "ExtraHigh"'))
 Assert-Throws {Set-WorkerGraphicsQualityText -Text $escaped -Quality low} 'graphics: decoded duplicate'
 $misplaced=Graphics-Tag ($inputText.Replace('"texture" : "ExtraHigh",','').Replace('"project" : "tpp"','"texture" : "ExtraHigh", "project" : "tpp"'))
 Assert-Throws {Set-WorkerGraphicsQualityText -Text $misplaced -Quality low} 'graphics: key must belong to quality object'
 $asts=@{}
 foreach($name in 'Start-WorkerSession.ps1','Invoke-WorkerScenario.ps1'){
  $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $SourceDirectory $name),[ref]$tokens,[ref]$errors)
  Assert-True ($errors.Count -eq 0) ('graphics: parses '+$name);$asts[$name]=$ast
  $p=@($ast.ParamBlock.Parameters|Where-Object{$_.Name.VariablePath.UserPath -ceq 'GraphicsQuality'})
  Assert-True ($p.Count -eq 1 -and $p[0].DefaultValue.Value -ceq 'preserve') ('graphics: default '+$name)
  $body=[scriptblock]::Create($ast.ParamBlock.Extent.Text+'; return $GraphicsQuality');$fixtureArgs=@{}
  if($name -ceq 'Start-WorkerSession.ps1'){$fixtureArgs=@{BuildDirectory='C:\fixture';CheckReceipt='C:\fixture';BaselineLoaderSha256=('A'*64)}}
  Assert-True ((& $body @fixtureArgs) -ceq 'preserve') ('graphics: bound default '+$name)
  Assert-True ((& $body @fixtureArgs -GraphicsQuality low) -ceq 'low') ('graphics: bound low '+$name)
  Assert-Throws {& $body @fixtureArgs -GraphicsQuality medium} ('graphics: rejected bound preset '+$name)
 }
 $start=$asts['Start-WorkerSession.ps1']
 $gate=@($start.FindAll({param($n)$n -is [Management.Automation.Language.IfStatementAst] -and $n.Clauses[0].Item1.Extent.Text -ceq '$GraphicsQuality -eq ''low'''},$true))
 Assert-True ($gate.Count -eq 1) 'graphics: exact low-copy block'
 $assign=@($start.FindAll({param($n)$n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$graphics'},$true))
 Assert-True ($assign.Count -eq 1 -and $assign[0].Right.Extent.Text -ceq "Join-Path `$saveRoot '287700\local\TPP_GRAPHICS_CONFIG'") 'graphics: only fresh role copy path'
 & {
  $record=[pscustomobject]@{source=$inputText;text=$inputText;writes=[Collections.Generic.List[hashtable]]::new()}
  $graphics='C:\fixture\run\host-saves\287700\local\TPP_GRAPHICS_CONFIG'
  function Read-WorkerBoundedText([string]$Path){Assert-True ($Path -ceq $graphics) 'graphics: copied read only';return $record.text}
  function Write-WorkerAtomicText([string]$Path,[string]$Text){$record.writes.Add(@{path=$Path;text=$Text});$record.text=$Text}
  $GraphicsQuality='preserve';$info=Get-GraphicsConfigInfo $record.text;. ([scriptblock]::Create($gate[0].Extent.Text))
  Assert-True ($record.writes.Count -eq 0 -and $record.text -ceq $inputText) 'graphics: preserve does not edit/write'
  $GraphicsQuality='low';. ([scriptblock]::Create($gate[0].Extent.Text))
  Assert-True ($record.writes.Count -eq 1 -and $record.writes[0].path -ceq $graphics -and $record.source -ceq $inputText -and $info.Valid) 'graphics: only copy written and verified'
  . ([scriptblock]::Create($gate[0].Extent.Text));Assert-True ($record.writes.Count -eq 1) 'graphics: no redundant write'
  $record.text=$inputText.Replace('ExtraHigh','High');Assert-Throws {. ([scriptblock]::Create($gate[0].Extent.Text))} 'graphics: bad copy refuses before write'
  Assert-True ($record.writes.Count -eq 1) 'graphics: bad copy never written'
 }
 $call=@($asts['Invoke-WorkerScenario.ps1'].FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and $n.Extent.Text.StartsWith("& (Join-Path `$worker 'Start-WorkerSession.ps1')",[StringComparison]::Ordinal)},$true))
 Assert-True ($call.Count -eq 1) 'graphics: one real Start invocation'
 & {
  $worker='C:\fixture';$build='C:\fixture';$CheckReceipt='C:\fixture';$BaselineLoaderSha256='A'*64
  $TimeoutSec=600;$ReadyTimeoutSec=180;$PartnerProfile='animated';$NativeStage='';$BuildProfile='normal';$BuddyCapacityArchive='';$MgoSleepRestArchive='';$MgoUniqueRestArchive='';$AmmoExperiment=$false;$MgoStandingCycleArchive='';$StandingLookupObserver=$false;$NativeActivationObserver=$false;$StandingFrameObserver=$false
  $SaveSeed='lab';$LabSetting=@();$Mode='lab';$WanSim='off'
  $HostSaveSeed='';$GuestSaveSeed='';$GuestSaveSeedRun=''
  function Join-Path([string]$Path,[string]$ChildPath){Assert-True ($ChildPath -ceq 'Start-WorkerSession.ps1') 'graphics: recorded Start only';return 'Graphics-RecordStart'}
  function Graphics-RecordStart {
   param($BuildDirectory,$CheckReceipt,$BaselineLoaderSha256,$TimeoutSec,$ReadyTimeoutSec,$PartnerProfile,$NativeStage,$BuildProfile,
    [switch]$AmmoExperiment,$SaveSeed,$HostSaveSeed,$GuestSaveSeed,$GuestSaveSeedRun,$GraphicsQuality,$LabSetting,$Mode,$WanSim,$BuddyCapacityArchive,$MgoSleepRestArchive,$MgoUniqueRestArchive,$MgoStandingCycleArchive,[switch]$StandingLookupObserver,[switch]$StandingFrameObserver,[switch]$NativeActivationObserver)
   if($script:recordSeeds){return "$SaveSeed|$HostSaveSeed|$GuestSaveSeed|$GuestSaveSeedRun"}
   return $GraphicsQuality
  }
  $script:recordSeeds=$false
  $GraphicsQuality='preserve';Assert-True ((& ([scriptblock]::Create($call[0].Extent.Text))) -ceq 'preserve') 'graphics: real invocation forwards default'
  $GraphicsQuality='low';Assert-True ((& ([scriptblock]::Create($call[0].Extent.Text))) -ceq 'low') 'graphics: real invocation forwards low'
  # The per-role seeds reach Start-WorkerSession unchanged (P2-19).
  $script:recordSeeds=$true
  Assert-True ((& ([scriptblock]::Create($call[0].Extent.Text))) -ceq 'lab|||') 'seeds: real invocation forwards empty role seeds'
  $HostSaveSeed='lab';$GuestSaveSeed='real'
  Assert-True ((& ([scriptblock]::Create($call[0].Extent.Text))) -ceq 'lab|lab|real|') 'seeds: real invocation forwards both role seeds'
  $GuestSaveSeed='';$GuestSaveSeedRun='0123456789abcdef0123456789abcdef'
  Assert-True ((& ([scriptblock]::Create($call[0].Extent.Text))) -ceq 'lab|lab||0123456789abcdef0123456789abcdef') 'seeds: real invocation forwards the seed run'
  $script:recordSeeds=$false
 }
}

Write-Output ("worker_session_tests: $script:checks detached checks passed; no worker/game/process/window operations")
