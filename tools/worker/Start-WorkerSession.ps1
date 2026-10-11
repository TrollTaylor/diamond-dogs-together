#Requires -Version 7
<#
.SYNOPSIS
Starts two owned MGSV workers only after a fresh private-desktop DX11 probe.
.DESCRIPTION
Baseline21 by default; explicit pinned buddy capacity research is lab-only. No visible fallback, desktop switching, foreground
input, Steam launch or Computer Use. A returned ready receipt proves worker
context and actual save/master/identity redirection; it does not prove gameplay.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$CheckReceipt,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$BaselineLoaderSha256,
    [string]$ConfigPath,
    [ValidateRange(30,600)][int]$TimeoutSec=600,
    [ValidateRange(10,180)][int]$ReadyTimeoutSec=120,
    [ValidateSet('animated','input','native')][string]$PartnerProfile='animated',
    # Native puppet stage (docs/native-puppet.md 13): census|loco|gun, only with
    # -PartnerProfile native (default loco there). gun needs -BuildProfile research.
    [ValidateSet('','census','loco','gun')][string]$NativeStage='',
    # research: the Research worker build (stock46 preload+writer, optionally
    # ammo; no upper_* experiment), only with -NativeStage gun.
    [ValidateSet('normal','research','buddy-capacity')][string]$BuildProfile='normal',
    [string]$BuddyCapacityArchive,
    [switch]$AmmoExperiment,
    # real: fresh copies of the real saves (a pending story demo can block the
    # partner's two-player pack in Mother Base). lab: copies of each folder's
    # existing Play-OfflineCoop lab saves (read only), the state earlier live
    # runs used. The real saves are backed up and re-checked either way.
    [ValidateSet('real','lab')][string]$SaveSeed='real',
    # Optional quality preset, applied only to this run's fresh role save copies.
    [ValidateSet('preserve','low')][string]$GraphicsQuality='preserve',
    # Extra lab.ini keys for this run: allow-listed lab experiments, the facing
    # A/B keys, the name tag keys, the Steam self-test, the online-play A/B
    # keys and the reload/switch/writer A/B keys (Invoke-WorkerScenario
    # "labsetting" lines); key=value for both copies, host:key=value /
    # guest:key=value for one.
    [ValidatePattern('^((host|guest):)?(debug_soldier_warp=[01]|nametag=[01]|nametag_crosshair=(aim|always|off)|nametag_name=[A-Za-z0-9_]{1,16}|partner_native_face=(off|idle|aim)|partner_native_aim=looksrc|partner_native_reload=(mirror|raw|event)|partner_native_slot=(off|write|call)|partner_native_equip=(stock46|manifest|substitute)|partner_native_equip_shared=(off|on|prefer)|partner_native_equip_measured=[01]|partner_native_standin=(off|on)|partner_native_ride=(off|hide)|partner_native_aimpt=(off|look|point)|steam_selftest=[012]|steam_selftest_fn=[01]|coop_hud=(compact|full|off)|partner_native_net=(lab|online)|partner_native_stall=(neutral|hold)|partner_native_hold=(own|net)|partner_input_edge_repeats=[2-8])$')][string[]]$LabSetting=@(),
    # lab: the one-PC lab (OfflineLab). play: the play rehearsal: both copies
    # run mode=play exactly as Play-Coop writes it, moved onto 127.0.0.1 with
    # the worker-only play_rehearsal=1, the real identity and a fresh link
    # secret (the supervisor admits it with PlayRehearsal; docs/two-pc-play.md
    # "Hidden play rehearsal"). Profiles: animated (clips) or native loco|gun.
    [ValidateSet('lab','play')][string]$Mode='lab',
    # A lab.json wanSim preset on both copies (one-way impairment each,
    # net_seed 1/2, partner_interp=auto), in either mode.
    [ValidateSet('off','good','bad','awful')][string]$WanSim='off'
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
Import-Module (Join-Path $repo 'tools\lab\LabCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'WorkerSessionCommon.psm1') -Force -DisableNameChecking
$pins=Get-WorkerRuntimePins
$build=Assert-WorkerPlainPath ([IO.Path]::GetFullPath($BuildDirectory,$repo)) -MustExist
if(!(Test-WorkerDescendant $build (Join-Path $repo 'build')) -or $build -ieq (Join-Path $repo 'build\native')){throw 'Use a separate worker build under build.'}
if($ReadyTimeoutSec -ge $TimeoutSec){throw 'Ready timeout must leave time for the bounded session.'}
if(!$ConfigPath){$ConfigPath=Join-Path $repo 'tools\lab\lab.json'}
$configFile=Assert-WorkerPlainPath ([IO.Path]::GetFullPath($ConfigPath,$repo)) -MustExist
$config=Get-LabConfig -ConfigPath $configFile -RepoRoot $repo
$loader=Assert-WorkerPlainPath (Join-Path $build 'dinput8.dll') -MustExist
$loaderHash=Get-WorkerHash $loader
if($loaderHash -ieq $BaselineLoaderSha256){throw 'Worker loader must differ from the saved normal baseline.'}
if($PartnerProfile -cne 'native' -and $NativeStage){throw '-NativeStage needs -PartnerProfile native.'}
if($BuildProfile -ceq 'research' -and !($PartnerProfile -ceq 'native' -and $NativeStage -ceq 'gun')){throw '-BuildProfile research is only for -NativeStage gun.'}
if($PartnerProfile -ceq 'native' -and $NativeStage -ceq 'gun' -and $BuildProfile -cne 'research'){throw '-NativeStage gun needs -BuildProfile research (a weapon on body 1).'}
# Native polish lab settings: the facing governor only with the native profile, the look source A/B run only in the gun stage.
if($Mode -ceq 'play' -and @($LabSetting|Where-Object{$_ -cmatch '(^|:)partner_native_equip_measured=1$'}).Count) {
    throw 'Measured optional equipment acceptance is lab-only.'
}
$labKeys=@($LabSetting|ForEach-Object{$_ -creplace '^(host|guest):',''})
if(@($labKeys|Where-Object{$_ -clike 'partner_native_*'}).Count -and $PartnerProfile -cne 'native'){throw '-LabSetting partner_native_* needs -PartnerProfile native.'}
if($labKeys -ccontains 'partner_native_aim=looksrc' -and $NativeStage -cne 'gun'){throw '-LabSetting partner_native_aim=looksrc needs -NativeStage gun.'}
if(@($labKeys|Where-Object{$_ -clike 'partner_native_aimpt=*'}).Count -and $NativeStage -cne 'gun'){throw '-LabSetting partner_native_aimpt needs -NativeStage gun.'}
if(@($labKeys|Where-Object{$_ -cmatch '^partner_native_(reload|slot|equip|equip_shared|equip_measured|standin)='}).Count -and $NativeStage -cne 'gun'){
    throw '-LabSetting partner_native_reload / partner_native_slot / partner_native_equip need -NativeStage gun.'
}
if($AmmoExperiment -and $BuildProfile -cne 'research'){throw '-AmmoExperiment needs -BuildProfile research.'}
$capacityPins=$null;$capacitySource=$null;$capacityCatalogSource=$null
if($BuildProfile -ceq 'buddy-capacity'){
    if($Mode -cne 'lab' -or $AmmoExperiment -or !($PartnerProfile -ceq 'native' -and $NativeStage -cin @('census','loco')) -or !$BuddyCapacityArchive){
        throw 'Buddy capacity needs explicit archive, copied-save lab mode and native census/loco; no ammo/gun experiment.'
    }
    $capacityPins=Get-WorkerBuddyCapacityPins
    $capacitySource=Assert-WorkerPlainPath ([IO.Path]::GetFullPath($BuddyCapacityArchive,$repo)) -MustExist
    $capacityCatalogSource=Assert-WorkerPlainPath (Join-Path ([IO.Path]::GetDirectoryName($capacitySource)) 'catalog.json') -MustExist
    Assert-WorkerHash $capacitySource $capacityPins.Archive;Assert-WorkerHash $capacityCatalogSource $capacityPins.Catalog
    if((Get-Item -LiteralPath $capacitySource).Length -ne $capacityPins.Bytes){throw 'Buddy capacity archive length differs.'}
} elseif($BuddyCapacityArchive){throw '-BuddyCapacityArchive needs -BuildProfile buddy-capacity.'}
# The play rehearsal runs what Play-Coop can write: the clip partner or the native loco/gun profile.
if($Mode -ceq 'play'){$null=Get-WorkerPlayPartnerSettings $PartnerProfile $NativeStage}
$checks=Read-WorkerSessionJson ([IO.Path]::GetFullPath($CheckReceipt,$repo))
if($BuildProfile -ceq 'research'){
    if(!(Test-WorkerResearchReceipt $checks $build $repo $loaderHash (Get-WorkerResearchExperiments -Ammo:$AmmoExperiment))){
        throw 'Require passed Research/background ON checks with exactly the stock46 experiments (no upper_*) for this exact build and loader.'
    }
} elseif($BuildProfile -ceq 'buddy-capacity'){
    if(!(Test-WorkerResearchReceipt $checks $build $repo $loaderHash @('MGS_LOCAL_BUDDY_CAPACITY_TEST'))){
        throw 'Buddy capacity requires passed Research/background ON checks with exactly MGS_LOCAL_BUDDY_CAPACITY_TEST for this build.'
    }
} elseif(!(Test-WorkerCheckReceipt $checks $build $repo $loaderHash)){throw 'Require passed Normal/background ON/all experiments OFF checks for this exact build and loader.'}
foreach($name in @('Start-WorkerSession.ps1','Stop-WorkerSession.ps1','WorkerSessionCommon.psm1')) {
    if(!$checks.source_hashes.Contains('tools\worker\'+$name)){throw 'Re-run worker checks after adding the runtime controllers.'}
}
$minhookLogical=Join-Path $repo 'third_party\minhook'
$dependency=$checks.source_dependencies['third_party\minhook']
$minhook=Get-Item -LiteralPath $minhookLogical -Force
$minhookLinked=[bool]($minhook.Attributes -band [IO.FileAttributes]::ReparsePoint)
$minhookTarget=if($minhookLinked){$minhook.ResolveLinkTarget($true).FullName}else{$minhook.FullName}
if($dependency.logical_path -ine $minhookLogical -or $dependency.resolved_path -ine $minhookTarget -or [bool]$dependency.linked -ne $minhookLinked){throw 'Reviewed MinHook source dependency changed.'}
$null=Assert-WorkerPlainPath $minhookTarget -MustExist
foreach($entry in $checks.source_hashes.GetEnumerator()) {
    $relative=[string]$entry.Key;$prefix='third_party\minhook\'
    $source=if($relative.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){[IO.Path]::GetFullPath($relative.Substring($prefix.Length),$minhookTarget)}else{[IO.Path]::GetFullPath($relative,$repo)}
    $sourceRoot=if($relative.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){$minhookTarget}else{$repo}
    if(!(Test-WorkerDescendant $source $sourceRoot)){throw 'A checked source path escaped its declared root.'}
    Assert-WorkerHash $source ([string]$entry.Value)
}
$cache=Read-WorkerBoundedText (Join-Path $build 'CMakeCache.txt') 1048576
foreach($entry in $checks.plan.flags.GetEnumerator()) {
    $pattern='(?m)^'+[regex]::Escape([string]$entry.Key)+':BOOL='+[regex]::Escape([string]$entry.Value)+'\r?$'
    if([regex]::Matches($cache,$pattern).Count -ne 1){throw 'The checked worker cache flags changed.'}
}
foreach($name in @('worker_supervisor.exe','worker_probe.exe')){$null=Assert-WorkerPlainPath (Join-Path $build $name) -MustExist}
$mutex=[Threading.Mutex]::new($false,'Local\MGSCoopBackgroundSession')
$lockOwned=$false;$session=$null;$process=$null;$runDirectory=$null
try {
    try{$lockOwned=$mutex.WaitOne([TimeSpan]::FromSeconds(30))}catch [Threading.AbandonedMutexException]{$mutex.ReleaseMutex();throw 'Previous worker coordinator abandoned its lock; inspect its session before restarting.'}
    if(!$lockOwned){throw 'Another background session controller is active.'}
    $instances=[ordered]@{};$roleBaselines=@{}
    foreach($role in @('host','guest')) {
        $directory=if($role -ceq 'host'){$config.Paths.hostDir}else{$config.Paths.guestDir}
        $p=Get-InstancePaths -GameDirectory (Assert-WorkerPlainPath $directory -MustExist)
        if(@(Get-GameProcessInDirectory -Directory $p.Game).Count){throw "MGSV is already running in $($p.Game); refusing adoption."}
        foreach($path in @($p.Exe,$p.SteamApi,$p.Loader,$p.Record,$p.Archive)){$null=Assert-WorkerPlainPath $path -MustExist}
        Assert-WorkerHash $p.Exe $pins.Exe;Assert-WorkerHash $p.SteamApi $pins.Steam
        # Each copy's normal baseline is the loader its own ownership record names
        # (the host may hold the friend-package install, the guest the one-PC lab
        # install); the host's must equal -BaselineLoaderSha256.
        $record=Read-WorkerSessionJson $p.Record
        $roleBaseline=[string]$record.dll_sha256
        if($roleBaseline -cnotmatch '^[A-Fa-f0-9]{64}$' -or ($role -ceq 'host' -and $roleBaseline -ine $BaselineLoaderSha256)){throw 'Baseline loader is not owned by this exact game folder.'}
        if($roleBaseline -ieq $loaderHash){throw 'Worker loader must differ from the saved normal baseline.'}
        Assert-WorkerHash $p.Loader $roleBaseline;Assert-WorkerHash $p.Archive $pins.Archive
        if((Get-Item -LiteralPath $p.Archive).Length -ne 16333824){throw 'Baseline21 archive size differs.'}
        if($record.product -cne 'mgs-coop-original-loader-probe' -or $record.game_directory -ine $p.Game){throw 'Baseline loader is not owned by this exact game folder.'}
        $roleBaselines[$role]=$roleBaseline.ToUpperInvariant()
        if(Test-Path -LiteralPath $p.Marker){throw 'Disable the existing lab/session marker before starting a private worker.'}
        if((Test-Path -LiteralPath $p.LabIni) -and (ConvertFrom-IniText (Read-WorkerBoundedText $p.LabIni))['enabled'] -eq '1'){throw 'Disable the current lab before starting a private worker.'}
        $instances[$role]=$p
    }
    if($instances.host.Game -ieq $instances.guest.Game -or (Test-WorkerDescendant $instances.host.Game $instances.guest.Game) -or (Test-WorkerDescendant $instances.guest.Game $instances.host.Game)){throw 'Worker game roots overlap.'}
    # Each start obtains fresh actual hardware/pixel/desktop/job qualification.
    $qualifications=@(& (Join-Path $repo 'tools\worker\Invoke-WorkerProbe.ps1') -BuildDirectory $build -PassThru | Where-Object {$_ -isnot [string]})
    if($qualifications.Count -ne 1){throw 'Fresh probe did not return one typed qualification.'}
    $qualification=$qualifications[0]
    $supervisor=Join-Path $build 'worker_supervisor.exe'
    Assert-WorkerHash $supervisor $qualification.SupervisorSha256
    Assert-WorkerHash (Join-Path $build 'worker_probe.exe') $qualification.ProbeSha256
    $runId=[Guid]::NewGuid().ToString('N')
    $runDirectory=Assert-WorkerPlainPath (Join-Path $repo ('build\worker-runtime\'+$runId))
    if(Test-Path -LiteralPath $runDirectory){throw 'Worker run directory is not fresh.'}
    $null=New-Item -ItemType Directory -Path $runDirectory
    $statePath=Join-Path $runDirectory 'session.json'
    $identity=Get-HostIdentity -OverrideSteamId ([string]$config.Identity.hostSteamId) -SteamPath $config.Paths.steamRoot
    Assert-WorkerRealSaveSource $identity.UserData
    # The lab manifest helper deliberately emits its row array as one pipeline
    # value. Direct typed assignment preserves rows; @(...command...) nests it.
    [object[]]$beforeSaves=Get-RealSaveManifest -UserData $identity.UserData
    if(!$beforeSaves.Count){throw 'No real save manifest; refusing worker preparation.'}
    $session=[ordered]@{schema=1;run_id=$runId;run_dir=$runDirectory;repo=$repo;build_directory=$build;status='staging';
        started_utc=[DateTime]::UtcNow.ToString('o');failure=$null;supervisor=$null;owned_processes=@();
        candidate_sha256=$loaderHash;baseline_sha256=$BaselineLoaderSha256;archive_sha256=$pins.Archive;lab_settings=@($LabSetting);mode=$Mode;wan_sim=$WanSim;graphics_quality=$GraphicsQuality.ToLowerInvariant();
        check_receipt=[IO.Path]::GetFullPath($CheckReceipt,$repo);check_receipt_sha256=Get-WorkerHash ([IO.Path]::GetFullPath($CheckReceipt,$repo));
        qualification=$qualification.QualificationPath;qualification_sha256=Get-WorkerHash $qualification.QualificationPath;
        prepare_started=$false;prepare_completed=$false;configuration=$null;real_saves=[ordered]@{root=$identity.UserData;before=$beforeSaves};
        roles=[ordered]@{};shutdown_sequence=0;gameplay_validated=$false;visible_fallback=$false}
    Write-WorkerSessionJson $statePath $session
    $archive=Join-Path $runDirectory 'baseline21-runtime.dat'
    [IO.File]::Copy($instances.host.Archive,$archive,$false);Assert-WorkerHash $archive $pins.Archive
    if($capacityPins){
        $session.runtime_profile=$capacityPins.Profile;$session.runtime_archive_sha256=$capacityPins.Archive
        $session.runtime_archive_bytes=$capacityPins.Bytes;$session.runtime_motion_count=$capacityPins.Motions
        $session.runtime_buddy_capacity=$capacityPins.Capacity;$session.runtime_catalog_sha256=$capacityPins.Catalog
        $archive=Join-Path $runDirectory 'buddy-capacity-two-runtime.dat'
        [IO.File]::Copy($capacitySource,$archive,$false);Assert-WorkerHash $archive $capacityPins.Archive
        if((Get-Item -LiteralPath $archive).Length -ne $capacityPins.Bytes){throw 'Staged candidate archive length differs.'}
        $session.runtime_archive_path=$archive
        $session.runtime_catalog_path=Join-Path $runDirectory 'buddy-capacity-catalog.json'
        [IO.File]::Copy($capacityCatalogSource,$session.runtime_catalog_path,$false)
        Assert-WorkerHash $session.runtime_catalog_path $capacityPins.Catalog
        Write-WorkerSessionJson $statePath $session
    }
    $runtime=Get-WorkerSessionRuntimeCatalog $session $pins
    $licenseFiles=[ordered]@{dynamite='DYNAMITE-LICENSE';minhook='MinHook-LICENSE.txt';imgui='ImGui-LICENSE.txt';tppmod='tpp-mod-LICENSE';notices='THIRD_PARTY_NOTICES.md'}
    foreach($role in @('host','guest')) {
        $p=$instances[$role];$backup=Join-Path $runDirectory ($role+'-baseline')
        $null=New-Item -ItemType Directory -Path $backup
        $licenses=@{}
        foreach($entry in $licenseFiles.GetEnumerator()){$licenses[$entry.Key]=Assert-WorkerPlainPath (Join-Path $p.Mod $entry.Value) -MustExist}
        $package=Join-Path $backup 'package'
        $null=New-LabPackage -RepoRoot $repo -Destination $package -LoaderPath $p.Loader -Version (Get-ProjectVersion $repo) -ExecutableSha256 $pins.Exe -SteamDllSha256 $pins.Steam -ArchiveSha256 $pins.Archive -LicenseOverrides $licenses
        $snapshots=@()
        foreach($key in @('LabIni','CoopIni','Marker','ClientHost','LinkSecret')){$snapshots+=@(Get-WorkerConfigSnapshot $p.$key $backup $key)}
        $recordCopy=Join-Path $backup 'probe-install.json';[IO.File]::Copy($p.Record,$recordCopy,$false)
        $session.roles[$role]=[ordered]@{game=$p.Game;baseline_package=$package;baseline_package_sha256=Get-WorkerHash (Join-Path $package 'package.json');
            baseline_sha256=$roleBaselines[$role];baseline_record_sha256=Get-WorkerHash $p.Record;baseline_record=$recordCopy;configs=$snapshots;
            worker_save_dir=$null;mutex_suffix=$null;lab_sha256=$null;coop_sha256=$null;marker_sha256=$null;record_sha256=$null;log_offsets=@{};safety=$null}
    }
    Write-WorkerSessionJson $statePath $session
    # Read-only real-save backup is unique to this run. Preserve previous
    # Play-OfflineCoop lab copies and their progress/graphics without touching
    # them: Prepare will skip its default save and graphics operations below.
    $freshBackup=Join-Path $runDirectory 'real-save-backup'
    Assert-WorkerRealSaveSource $identity.UserData
    $manifestBackup=Backup-RealSaves -UserData $identity.UserData -Destination $freshBackup
    $backupReceipt=Join-Path $runDirectory 'save-backup.json'
    Write-WorkerSessionJson $backupReceipt ([ordered]@{schema=1;backup_dir=$freshBackup;created_utc=$manifestBackup.created_utc;verified=@($manifestBackup.verified)})
    $session.real_saves.backup_receipt=$backupReceipt;$session.real_saves.backup_receipt_sha256=Get-WorkerHash $backupReceipt
    $session.real_saves.backup_manifest_sha256=Get-WorkerHash (Join-Path $freshBackup 'manifest.json')
    Write-WorkerSessionJson $statePath $session
    $raw=Read-WorkerSessionJson $configFile
    $raw.paths.runDir=Join-Path $runDirectory 'prepare';$raw.paths.loader=$loader;$raw.paths.runtimeArchive=$archive
    if($runtime.Experimental){$raw.hashes.runtimeArchive=$runtime.Archive}
    $raw.xinput.host='none';$raw.xinput.guest='none';$raw.offline.marker='offline-v1'
    $raw.graphics.windowMode='Windowed';$raw.saves.localSubdir='287700\local';$raw.saves.remoteSubdir='remote'
    $copyConfig=Join-Path $runDirectory 'worker-lab.json';Write-WorkerSessionJson $copyConfig $raw
    $session.configuration=$copyConfig;$session.prepare_started=$true;$session.status='preparing';Write-WorkerSessionJson $statePath $session
    # Default: the normal animated partner of Play-OfflineCoop (xform + dm=map).
    # -PartnerProfile input selects the round-4 input drive; native the native
    # puppet stage (docs/native-puppet.md 13.1) on the animated fallback base.
    $settings=Get-WorkerPartnerSettings $PartnerProfile $NativeStage
    $settings=@($settings)+@($LabSetting)
    # -Mode play: Prepare installs the loader, archive, marker and coop.ini as
    # for the lab (the clip base); the play lab.ini replaces its lab.ini below.
    if($Mode -ceq 'play'){$settings=@(Get-WorkerPartnerSettings 'animated')}
    $prepareWanSim=if($Mode -ceq 'lab'){$WanSim}else{'off'}
    & (Join-Path $repo 'tools\lab\Prepare-LabInstance.ps1') -ConfigPath $copyConfig -Role both -Offline -Loader $loader -RuntimeArchive $archive -PartnerSetting $settings -WanSim $prepareWanSim -SkipSaves -SkipGraphics | ForEach-Object {Write-Host $_}
    if($Mode -ceq 'play') {
        # The play rehearsal (docs/two-pc-play.md "Hidden play rehearsal"):
        # Play-Coop's own play lab.ini moved onto 127.0.0.1, a fresh random
        # link secret in both copies, no client-host.txt (play has none).
        $wanLines=@()
        if($WanSim -cne 'off'){
            $preset=Get-LabWanSimPreset -WanSim $config.WanSim -Name $WanSim
            foreach($key in $preset.Keys){$wanLines+=($key+'='+$preset[$key])}
        }
        $secret=New-CoopSecret
        foreach($role in @('host','guest')) {
            $p=$instances[$role]
            $base=if($role -ceq 'host'){New-CoopPlayLabIniText -Role host -SaveDir $p.SaveDir -Partner clips}
                else{New-CoopPlayLabIniText -Role guest -SaveDir $p.SaveDir -PeerAddress '192.0.2.1' -Partner clips}
            $roleSettings=@($LabSetting|Where-Object{$_ -cnotmatch '^(host|guest):' -or $_.StartsWith($role+':')}|ForEach-Object{$_ -creplace '^(host|guest):',''})
            $extra=@(Get-WorkerPlayPartnerSettings $PartnerProfile $NativeStage)+$wanLines
            if($WanSim -cne 'off'){$extra+=@('net_seed='+$(if($role -ceq 'host'){'1'}else{'2'}))}
            $extra+=$roleSettings
            Write-WorkerAtomicText $p.LabIni (ConvertTo-WorkerPlayLabIniText $base $role $extra)
            Write-WorkerAtomicText $p.LinkSecret ($secret+"`r`n")
            if(Test-Path -LiteralPath $p.ClientHost -PathType Leaf){Remove-Item -LiteralPath $p.ClientHost}
        }
        $secret=$null
    }
    $session.prepare_completed=$true
    foreach($role in @('host','guest')) {
        $p=$instances[$role];$row=$session.roles[$role]
        Assert-WorkerHash $p.Loader $loaderHash;Assert-WorkerHash $p.Archive $runtime.Archive
        $ini=ConvertFrom-IniText (Read-WorkerBoundedText $p.LabIni)
        if($ini['role'] -cne $role -or $ini['enabled'] -cne '1' -or $ini['xinput'] -cne 'none'){throw 'Generated worker lab role/xinput gate differs.'}
        # A world.trigger an aborted scenario left behind (Invoke-WorkerScenario
        # "host worldtrigger") must never be applied by this session.
        $row.world_trigger_removed=Remove-WorkerWorldTrigger $p.Game
        if($row.world_trigger_removed){Write-Host "[$role] removed $($row.world_trigger_removed) leftover world.trigger file(s)."}
        $saveRoot=Join-Path $runDirectory ($role+'-saves')
        if(!(Test-WorkerDescendant $saveRoot $runDirectory) -or (Test-Path -LiteralPath $saveRoot)){throw 'Worker save copy must be a fresh owned run descendant.'}
        $null=Assert-WorkerPlainPath $saveRoot
        $row.worker_save_dir=$saveRoot
        Assert-WorkerHash (Join-Path $freshBackup 'manifest.json') $session.real_saves.backup_manifest_sha256
        if($SaveSeed -ceq 'lab'){
            $labSaves=Assert-WorkerPlainPath $p.SaveDir -MustExist
            foreach($sub in @('287700\local','remote')){if(!(Test-Path -LiteralPath (Join-Path $labSaves $sub) -PathType Container)){throw "Lab save copy is missing $sub in $labSaves."}}
            $null=New-Item -ItemType Directory -Path $saveRoot
            foreach($sub in @('287700','remote')){Copy-Item -LiteralPath (Join-Path $labSaves $sub) -Destination $saveRoot -Recurse}
            $seed=[pscustomobject]@{Seeded=$true;Files=@(Get-ChildItem -LiteralPath $saveRoot -Recurse -File).Count}
        } else {
            $seed=Copy-SaveSeed -BackupDirectory $freshBackup -SaveDir $saveRoot -LocalSubdir '287700\local' -RemoteSubdir 'remote'
        }
        if(!$seed.Seeded -or !$seed.Files){throw 'Fresh worker save copy was not seeded from the verified backup.'}
        $row.save_seed=@(Get-WorkerCopyInventory $saveRoot)
        $row.mutex_suffix='worker_'+$role+'_'+$runId.Substring(0,8)
        $text=Set-WorkerIniValue (Read-WorkerBoundedText $p.LabIni) 'save_dir' $saveRoot
        if([regex]::Matches($text,'(?m)^mutex_suffix=').Count){$text=Set-WorkerIniValue $text 'mutex_suffix' $row.mutex_suffix}
        else{$text=$text.TrimEnd([char]13,[char]10)+"`nmutex_suffix="+$row.mutex_suffix+"`n"}
        Write-WorkerAtomicText $p.LabIni $text
        $ini=ConvertFrom-IniText $text
        # The play rehearsal keeps the real identity in both copies.
        $row.expected_steamid=if($role -ceq 'guest' -and $Mode -ceq 'lab'){[string]$ini['self_steamid']}else{[string]$identity.SteamId64}
        $row.expected_peer=[string]$ini['peer_steamid'];$row.expected_url='http://127.0.0.1:'+([string]$raw.offline.masterPort)+'/tppstm/gate'
        $graphics=Join-Path $saveRoot '287700\local\TPP_GRAPHICS_CONFIG'
        $info=Get-GraphicsConfigInfo (Read-WorkerBoundedText $graphics)
        if(!$info.Valid){throw 'Fresh copied graphics config has an invalid integrity tag.'}
        if($info.WindowMode -cne 'Windowed' -or $info.Width -ne [int]$raw.graphics.width -or $info.Height -ne [int]$raw.graphics.height) {
            $edited=Set-GraphicsConfigWindowedText -Text (Read-WorkerBoundedText $graphics) -Width ([int]$raw.graphics.width) -Height ([int]$raw.graphics.height) -WindowMode Windowed
            Write-WorkerAtomicText $graphics $edited
            $info=Get-GraphicsConfigInfo $edited
        }
        if($GraphicsQuality -eq 'low') {
            # This graphics path is only the freshly seeded, owned role save copy.
            $copiedText=Read-WorkerBoundedText $graphics
            $edited=Set-WorkerGraphicsQualityText -Text $copiedText -Quality low
            if($edited -cne $copiedText){Write-WorkerAtomicText $graphics $edited}
            $info=Get-GraphicsConfigInfo $edited
        }
        if(!$info.Valid -or $info.WindowMode -cne 'Windowed'){throw 'Copied graphics config must have a valid integrity tag and be windowed.'}
        $row.graphics_sha256=Get-WorkerHash $graphics;$row.lab_sha256=Get-WorkerHash $p.LabIni;$row.coop_sha256=Get-WorkerHash $p.CoopIni
        $row.marker_sha256=Get-WorkerHash $p.Marker;$row.record_sha256=Get-WorkerHash $p.Record
        foreach($snapshot in $row.configs){if(Test-Path -LiteralPath $snapshot.path -PathType Leaf){$snapshot.worker_sha256=Get-WorkerHash $snapshot.path}}
        foreach($name in @('early.log','steam-observer.log','script-observer.log')) {
            $path=Join-Path $p.Mod $name;$row.log_offsets[$name]=[ordered]@{path=$path;offset=0;before_sha256=$null}
            if(Test-Path -LiteralPath $path){$null=Assert-WorkerPlainPath $path -MustExist;$row.log_offsets[$name].offset=[long](Get-Item -LiteralPath $path).Length;$row.log_offsets[$name].before_sha256=Get-WorkerHash $path}
        }
    }
    [object[]]$afterSaves=Get-RealSaveManifest -UserData $identity.UserData
    if(!(Compare-HashManifest -Expected $beforeSaves -Actual $afterSaves).Same){throw 'Real saves changed during preparation; refusing launch.'}
    $manifest=[ordered]@{schema='1';mode='mgsv';run_id=$runId;run_dir=$runDirectory;desktop='MGSWorker_'+$runId;
        timeout_ms=[string]($TimeoutSec*1000);ready_timeout_ms=[string]($ReadyTimeoutSec*1000);
        exe_sha256=$pins.Exe;steam_sha256=$pins.Steam;loader_sha256=$loaderHash;runtime_archive_sha256=$runtime.Archive;runtime_motion_count='21';
        probe_receipt=$qualification.QualificationPath;probe_receipt_sha256=$session.qualification_sha256;offline_master_port=[string]$raw.offline.masterPort}
    # The supervisor's gate: OfflineLab, or PlayRehearsal for the play rehearsal.
    if($Mode -ceq 'play'){$manifest['lab_mode']='play'}
    if($runtime.Experimental){$manifest['runtime_profile']=$runtime.Profile;$manifest['runtime_buddy_capacity']='2'}
    foreach($role in @('host','guest')){
        $manifest[$role+'_exe']=$instances[$role].Exe;$manifest[$role+'_save_dir']=$session.roles[$role].worker_save_dir
        foreach($kind in @('lab','coop','graphics')){$manifest[$role+'_'+$kind+'_sha256']=$session.roles[$role][$kind+'_sha256']}
    }
    $launch=Join-Path $runDirectory 'launch.manifest'
    Write-WorkerAtomicText $launch ((($manifest.Keys|ForEach-Object {$_+'='+$manifest[$_]}) -join "`n")+"`n")
    $session.launch_manifest_sha256=Get-WorkerHash $launch;$session.status='launching';Write-WorkerSessionJson $statePath $session
    Assert-WorkerHash $supervisor $qualification.SupervisorSha256
    $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$supervisor;$psi.WorkingDirectory=$build
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $psi.ArgumentList.Add('--launch');$psi.ArgumentList.Add($launch)
    $process=[Diagnostics.Process]::new();$process.StartInfo=$psi
    # The supervisor outlives this script: keep it from inheriting our stdout/
    # stderr, or a caller capturing our output would wait for the whole session.
    $startedSupervisor=Invoke-WithoutStdHandleInheritance {$process.Start()}
    if(!$startedSupervisor){throw 'Owned worker supervisor did not start.'}
    $null=$process.SafeHandle
    $session.supervisor=[ordered]@{pid=$process.Id;creation_filetime=[string]$process.StartTime.ToUniversalTime().ToFileTimeUtc();exe=$supervisor;sha256=$qualification.SupervisorSha256}
    $session.process_started_utc=$process.StartTime.ToUniversalTime().ToString('o');Write-WorkerSessionJson $statePath $session
    $deadline=[Environment]::TickCount64+([long]$ReadyTimeoutSec*1000)
    while([Environment]::TickCount64 -lt $deadline) {
        if($process.HasExited){throw "Worker supervisor exited before admission (code $($process.ExitCode))."}
        $statusPath=Join-Path $runDirectory 'supervisor.json'
        if(!(Test-Path -LiteralPath $statusPath)){Start-Sleep -Milliseconds 100;continue}
        $status=Read-WorkerSessionJson $statusPath
        if(!(Test-WorkerSupervisorReceipt $status $session)){throw 'Worker supervisor isolation/ownership receipt differs.'}
        $session.owned_processes=@($status.owned_processes);Write-WorkerSessionJson $statePath $session
        if($status.stage -in @('refused','closed','closing','terminated')){throw "Worker refused: $($status.reason)"}
        if(!$status.ready){Start-Sleep -Milliseconds 100;continue}
        if(!$status.job_policy_verified -or !$status.desktop_created -or @($status.owned_processes).Count -ne 2){throw 'Worker ready receipt lacks owned private job.'}
        $allSafe=$true
        foreach($role in @('host','guest')) {
            $owned=@($status.owned_processes|Where-Object {$_.role -ceq $role})
            if($owned.Count -ne 1 -or !$owned[0].admitted -or $owned[0].exe -ine $instances[$role].Exe -or $owned[0].sha256 -ine $pins.Exe){throw 'Worker role process lease differs.'}
            $leased=Get-WorkerLeasedProcess $owned[0];if(!$leased){throw 'An admitted game process exited.'};$leased.Dispose()
            if(!(Test-WorkerContextText (Read-WorkerBoundedText (Join-Path $runDirectory ($role+'.context')) 1024) $runId $role $owned[0])){throw 'Actual native context ownership differs.'}
            $row=$session.roles[$role];$early=$row.log_offsets['early.log'];$steam=$row.log_offsets['steam-observer.log']
            $limit=[datetime]::Parse($session.process_started_utc).ToUniversalTime()
            $earlyText=Read-WorkerFreshLog $early.path $early.offset $limit;$steamText=Read-WorkerFreshLog $steam.path $steam.offset $limit
            $safety=if($Mode -ceq 'play'){Get-WorkerPlaySafetyVerdict -Role $role -EarlyLog $earlyText -SteamLog $steamText -ExpectedSaveDir $row.worker_save_dir -ExpectedUrl $row.expected_url -ExpectedMutexSuffix $row.mutex_suffix}
                else{Get-LabSafetyVerdict -Role $role -EarlyLog $earlyText -SteamLog $steamText -ExpectedSaveDir $row.worker_save_dir -ExpectedUrl $row.expected_url -ExpectedSteamId $row.expected_steamid -ExpectedMutexSuffix $row.mutex_suffix}
            $row.safety=@{state=$safety.State;reason=$safety.Reason;missing=@($safety.Missing)}
            if($safety.State -ceq 'unsafe'){throw "Actual $role startup safety refused: $($safety.Reason)"}
            if($safety.State -cne 'ok'){$allSafe=$false}
        }
        # The play rehearsal has no client-host.txt (both copies keep the real identity).
        $idCheck='not_applicable'
        if($Mode -ceq 'lab') {
            $hostId=Read-WorkerFreshLog $instances.host.HostId 0 ([datetime]::Parse($session.process_started_utc).ToUniversalTime()) 1024
            $idCheck=Get-HostIdCheck -HostIdText $hostId -ClientHostText (Read-WorkerBoundedText $instances.guest.ClientHost 1024) -PeerSteamId $session.roles.guest.expected_peer
            if($idCheck -ceq 'mismatch'){throw 'Actual fresh host identity differs from guest configuration.'}
            # Offline, host-steamid.txt is only written once an online host session
            # exists, so 'not_written' is the normal offline state (as in Start-Lab);
            # only an actual mismatch refuses.
            if($idCheck -cne 'ok' -and $idCheck -cne 'not_written'){$allSafe=$false}
        }
        $session.host_identity_check=$idCheck;Write-WorkerSessionJson $statePath $session
        if($allSafe){$session.status='ready';$session.ready_utc=[DateTime]::UtcNow.ToString('o');Write-WorkerSessionJson $statePath $session
            Write-Host "Private worker context and startup safety admitted: $runDirectory"
            [pscustomobject]@{RunDirectory=$runDirectory;RunId=$runId;SessionPath=$statePath;Ready=$true;GameplayValidated=$false};return}
        Start-Sleep -Milliseconds 100
    }
    throw 'Worker actual context/startup safety admission timed out.'
} catch {
    $failure=$_.Exception.Message
    if($session) {
        if($process -and !$session.supervisor -and !$process.HasExited) {
            # No lease metadata could be published yet. The Process object was
            # returned directly by our own Start call and owns its OS handle.
            $process.Kill();if(!$process.WaitForExit(5000)){throw 'New supervisor could not be closed; do not restore while it owns children.'}
        }
        # A preparation may fail after writing one role. Record only outputs
        # attributable to the exact requested Prepare configuration, so Stop
        # can remove them without adopting unrelated edits as owned content.
        if($session.prepare_started) {
            foreach($role in $session.roles.Keys) {
                $row=$session.roles[$role];$p=Get-InstancePaths $row.game
                foreach($snapshot in $row.configs) {
                    if(!$snapshot.worker_sha256 -and (Test-Path -LiteralPath $snapshot.path -PathType Leaf)) {
                        try {
                            $text=Read-WorkerBoundedText $snapshot.path
                            $url='http://127.0.0.1:'+([string]$raw.offline.masterPort)+'/tppstm/gate'
                            if(Test-WorkerPreparedConfigText ([IO.Path]::GetFileName($snapshot.path)) $text $role ([string]$identity.SteamId64) $url @($p.SaveDir,$row.worker_save_dir) $Mode){$snapshot.worker_sha256=Get-WorkerHash $snapshot.path}
                        }catch{Write-Warning "Could not qualify a partial generated configuration for cleanup: $($snapshot.path)"}
                    }
                }
            }
        }
        $session.status='failed';$session.failure=$failure;Write-WorkerSessionJson (Join-Path $runDirectory 'session.json') $session
        try{& (Join-Path $PSScriptRoot 'Stop-WorkerSession.ps1') -RunDirectory $runDirectory | ForEach-Object {Write-Host $_}}
        catch{$failure+=' Cleanup also refused: '+$_.Exception.Message}
    }
    throw $failure
} finally {
    if($process){$process.Dispose()}
    if($lockOwned){$mutex.ReleaseMutex()};$mutex.Dispose()
}
