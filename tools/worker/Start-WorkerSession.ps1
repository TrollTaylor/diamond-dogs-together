#Requires -Version 7
<#
.SYNOPSIS
Starts two owned MGSV workers only after a fresh private-desktop DX11 probe.
.DESCRIPTION
Normal baseline21 only. No visible fallback, desktop switching, foreground
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
    [ValidateSet('animated','input')][string]$PartnerProfile='animated',
    # real: fresh copies of the real saves (a pending story demo can block the
    # partner's two-player pack in Mother Base). lab: copies of each folder's
    # existing Play-OfflineCoop lab saves (read only), the state earlier live
    # runs used. The real saves are backed up and re-checked either way.
    [ValidateSet('real','lab')][string]$SaveSeed='real',
    # Extra lab.ini keys for both copies of this run: allow-listed lab
    # experiments only (Invoke-WorkerScenario "labsetting" lines).
    [ValidatePattern('^debug_soldier_warp=[01]$')][string[]]$LabSetting=@()
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
$checks=Read-WorkerSessionJson ([IO.Path]::GetFullPath($CheckReceipt,$repo))
if(!(Test-WorkerCheckReceipt $checks $build $repo $loaderHash)){throw 'Require passed Normal/background ON/all experiments OFF checks for this exact build and loader.'}
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
    $instances=[ordered]@{}
    foreach($role in @('host','guest')) {
        $directory=if($role -ceq 'host'){$config.Paths.hostDir}else{$config.Paths.guestDir}
        $p=Get-InstancePaths -GameDirectory (Assert-WorkerPlainPath $directory -MustExist)
        if(@(Get-GameProcessInDirectory -Directory $p.Game).Count){throw "MGSV is already running in $($p.Game); refusing adoption."}
        foreach($path in @($p.Exe,$p.SteamApi,$p.Loader,$p.Record,$p.Archive)){$null=Assert-WorkerPlainPath $path -MustExist}
        Assert-WorkerHash $p.Exe $pins.Exe;Assert-WorkerHash $p.SteamApi $pins.Steam
        Assert-WorkerHash $p.Loader $BaselineLoaderSha256;Assert-WorkerHash $p.Archive $pins.Archive
        if((Get-Item -LiteralPath $p.Archive).Length -ne 16333824){throw 'Baseline21 archive size differs.'}
        $record=Read-WorkerSessionJson $p.Record
        if($record.product -cne 'mgs-coop-original-loader-probe' -or $record.game_directory -ine $p.Game -or $record.dll_sha256 -ine $BaselineLoaderSha256){throw 'Baseline loader is not owned by this exact game folder.'}
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
        candidate_sha256=$loaderHash;baseline_sha256=$BaselineLoaderSha256;archive_sha256=$pins.Archive;lab_settings=@($LabSetting);
        check_receipt=[IO.Path]::GetFullPath($CheckReceipt,$repo);check_receipt_sha256=Get-WorkerHash ([IO.Path]::GetFullPath($CheckReceipt,$repo));
        qualification=$qualification.QualificationPath;qualification_sha256=Get-WorkerHash $qualification.QualificationPath;
        prepare_started=$false;prepare_completed=$false;configuration=$null;real_saves=[ordered]@{root=$identity.UserData;before=$beforeSaves};
        roles=[ordered]@{};shutdown_sequence=0;gameplay_validated=$false;visible_fallback=$false}
    Write-WorkerSessionJson $statePath $session
    $archive=Join-Path $runDirectory 'baseline21-runtime.dat'
    [IO.File]::Copy($instances.host.Archive,$archive,$false);Assert-WorkerHash $archive $pins.Archive
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
            baseline_record_sha256=Get-WorkerHash $p.Record;baseline_record=$recordCopy;configs=$snapshots;
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
    $raw.xinput.host='none';$raw.xinput.guest='none';$raw.offline.marker='offline-v1'
    $raw.graphics.windowMode='Windowed';$raw.saves.localSubdir='287700\local';$raw.saves.remoteSubdir='remote'
    $copyConfig=Join-Path $runDirectory 'worker-lab.json';Write-WorkerSessionJson $copyConfig $raw
    $session.configuration=$copyConfig;$session.prepare_started=$true;$session.status='preparing';Write-WorkerSessionJson $statePath $session
    # Default: the normal animated partner of Play-OfflineCoop (xform + dm=map).
    # -PartnerProfile input selects the round-4 input drive.
    $settings=if($PartnerProfile -ceq 'input'){@('partner=1','net=1','partner_source=net','partner_apply=input','partner_input=net','partner_input_send=1',
        'partner_input_hold=own','partner_input_words=hpr','partner_recon=track','partner_stage=3','world=3')}
        else{@('partner=1','net=1','partner_stage=3','partner_source=net','partner_look=net','partner_apply=xform','partner_anim=off',
        'partner_reset=n','partner_input=off','partner_dm=map','world=3')}
    $settings=@($settings)+@($LabSetting)
    & (Join-Path $repo 'tools\lab\Prepare-LabInstance.ps1') -ConfigPath $copyConfig -Role both -Offline -Loader $loader -RuntimeArchive $archive -PartnerSetting $settings -SkipSaves -SkipGraphics | ForEach-Object {Write-Host $_}
    $session.prepare_completed=$true
    foreach($role in @('host','guest')) {
        $p=$instances[$role];$row=$session.roles[$role]
        Assert-WorkerHash $p.Loader $loaderHash;Assert-WorkerHash $p.Archive $pins.Archive
        $ini=ConvertFrom-IniText (Read-WorkerBoundedText $p.LabIni)
        if($ini['role'] -cne $role -or $ini['enabled'] -cne '1' -or $ini['xinput'] -cne 'none'){throw 'Generated worker lab role/xinput gate differs.'}
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
        $row.expected_steamid=if($role -ceq 'guest'){[string]$ini['self_steamid']}else{[string]$identity.SteamId64}
        $row.expected_peer=[string]$ini['peer_steamid'];$row.expected_url='http://127.0.0.1:'+([string]$raw.offline.masterPort)+'/tppstm/gate'
        $graphics=Join-Path $saveRoot '287700\local\TPP_GRAPHICS_CONFIG'
        $info=Get-GraphicsConfigInfo (Read-WorkerBoundedText $graphics)
        if(!$info.Valid){throw 'Fresh copied graphics config has an invalid integrity tag.'}
        if($info.WindowMode -cne 'Windowed' -or $info.Width -ne [int]$raw.graphics.width -or $info.Height -ne [int]$raw.graphics.height) {
            $edited=Set-GraphicsConfigWindowedText -Text (Read-WorkerBoundedText $graphics) -Width ([int]$raw.graphics.width) -Height ([int]$raw.graphics.height) -WindowMode Windowed
            Write-WorkerAtomicText $graphics $edited
            $info=Get-GraphicsConfigInfo $edited
        }
        if(!$info.Valid -or $info.WindowMode -cne 'Windowed'){throw 'Copied graphics config must have a valid integrity tag and be windowed.'}
        $row.graphics_sha256=Get-WorkerHash $graphics;$row.lab_sha256=Get-WorkerHash $p.LabIni;$row.coop_sha256=Get-WorkerHash $p.CoopIni
        $row.marker_sha256=Get-WorkerHash $p.Marker;$row.record_sha256=Get-WorkerHash $p.Record
        foreach($snapshot in $row.configs){if(Test-Path -LiteralPath $snapshot.path -PathType Leaf){$snapshot.worker_sha256=Get-WorkerHash $snapshot.path}}
        foreach($name in @('early.log','steam-observer.log')) {
            $path=Join-Path $p.Mod $name;$row.log_offsets[$name]=[ordered]@{path=$path;offset=0;before_sha256=$null}
            if(Test-Path -LiteralPath $path){$null=Assert-WorkerPlainPath $path -MustExist;$row.log_offsets[$name].offset=[long](Get-Item -LiteralPath $path).Length;$row.log_offsets[$name].before_sha256=Get-WorkerHash $path}
        }
    }
    [object[]]$afterSaves=Get-RealSaveManifest -UserData $identity.UserData
    if(!(Compare-HashManifest -Expected $beforeSaves -Actual $afterSaves).Same){throw 'Real saves changed during preparation; refusing launch.'}
    $manifest=[ordered]@{schema='1';mode='mgsv';run_id=$runId;run_dir=$runDirectory;desktop='MGSWorker_'+$runId;
        timeout_ms=[string]($TimeoutSec*1000);ready_timeout_ms=[string]($ReadyTimeoutSec*1000);
        exe_sha256=$pins.Exe;steam_sha256=$pins.Steam;loader_sha256=$loaderHash;runtime_archive_sha256=$pins.Archive;runtime_motion_count='21';
        probe_receipt=$qualification.QualificationPath;probe_receipt_sha256=$session.qualification_sha256;offline_master_port=[string]$raw.offline.masterPort}
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
            $safety=Get-LabSafetyVerdict -Role $role -EarlyLog (Read-WorkerFreshLog $early.path $early.offset $limit) -SteamLog (Read-WorkerFreshLog $steam.path $steam.offset $limit) -ExpectedSaveDir $row.worker_save_dir -ExpectedUrl $row.expected_url -ExpectedSteamId $row.expected_steamid -ExpectedMutexSuffix $row.mutex_suffix
            $row.safety=@{state=$safety.State;reason=$safety.Reason;missing=@($safety.Missing)}
            if($safety.State -ceq 'unsafe'){throw "Actual $role startup safety refused: $($safety.Reason)"}
            if($safety.State -cne 'ok'){$allSafe=$false}
        }
        $hostId=Read-WorkerFreshLog $instances.host.HostId 0 ([datetime]::Parse($session.process_started_utc).ToUniversalTime()) 1024
        $idCheck=Get-HostIdCheck -HostIdText $hostId -ClientHostText (Read-WorkerBoundedText $instances.guest.ClientHost 1024) -PeerSteamId $session.roles.guest.expected_peer
        if($idCheck -ceq 'mismatch'){throw 'Actual fresh host identity differs from guest configuration.'}
        # Offline, host-steamid.txt is only written once an online host session
        # exists, so 'not_written' is the normal offline state (as in Start-Lab);
        # only an actual mismatch refuses.
        if($idCheck -cne 'ok' -and $idCheck -cne 'not_written'){$allSafe=$false}
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
                            if(Test-WorkerPreparedConfigText ([IO.Path]::GetFileName($snapshot.path)) $text $role ([string]$identity.SteamId64) $url @($p.SaveDir,$row.worker_save_dir)){$snapshot.worker_sha256=Get-WorkerHash $snapshot.path}
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
