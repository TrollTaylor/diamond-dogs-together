#Requires -Version 7
<#
.SYNOPSIS
Closes one exact owned private worker job and restores its normal baseline.
.DESCRIPTION
No name-based process kills, window activation, input injection or real-save
restoration. Cleanup refuses unknown process ownership or changed package files.
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RunDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
Import-Module (Join-Path $repo 'tools\lab\LabCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'WorkerSessionCommon.psm1') -Force -DisableNameChecking
$run=Assert-WorkerPlainPath ([IO.Path]::GetFullPath($RunDirectory,$repo)) -MustExist
$root=Join-Path $repo 'build\worker-runtime'
$runId=[IO.Path]::GetFileName($run)
if(!(Test-WorkerRunId $runId) -or [IO.Path]::GetDirectoryName($run) -ine $root){throw 'Stop requires one exact worker-runtime run directory.'}
$statePath=Join-Path $run 'session.json';$session=Read-WorkerSessionJson $statePath
if($session.schema -ne 1 -or $session.run_id -cne $runId -or $session.run_dir -ine $run -or $session.repo -ine $repo){throw 'Worker session identity differs.'}
$pins=Get-WorkerRuntimePins

function Assert-WorkerConfigSnapshotPreflight($Snapshot,[string]$Game,[string]$Run) {
    $path=Assert-WorkerPlainPath $Snapshot.path
    if(!(Test-WorkerDescendant $path (Join-Path $Game 'mgs-coop'))){throw 'Baseline configuration target escaped its game mod directory.'}
    if($Snapshot.present) {
        if(!(Test-WorkerDescendant $Snapshot.backup $Run)){throw 'Configuration backup escaped the owned run.'}
        Assert-WorkerHash $Snapshot.backup $Snapshot.sha256
    }
    $exists=Test-Path -LiteralPath $path
    $present=Test-Path -LiteralPath $path -PathType Leaf
    if($exists -and !$present){throw "Worker configuration target is not a file: $path"}
    if(!$present){return}
    $current=Get-WorkerHash $path
    if($Snapshot.present -and $current -ieq $Snapshot.sha256){return}
    if(!$Snapshot.worker_sha256 -or $current -ine $Snapshot.worker_sha256){throw "Worker refuses to overwrite a changed configuration: $path"}
}
function Invoke-WorkerPackageRestore([string]$Repo,[string]$Game,[string]$Package,[string]$Role) {
    & (Join-Path $Repo 'tools\package-install.ps1') -Action Restore -GameDirectory $Game -PackageDirectory $Package -NoPrompt | ForEach-Object {Write-Host "[$Role] $_"}
}
function Get-WorkerRealSaveComparison($RealSaves) {
    # The manifest reader returns one wrapped row-array. Direct assignment
    # preserves its per-file rows; another @(...call...) would nest it again.
    [object[]]$actual=Get-RealSaveManifest -UserData $RealSaves.root
    return Compare-HashManifest -Expected @($RealSaves.before) -Actual $actual
}
function Invoke-WorkerRestoreOwnedSession($Session,[string]$Run,[string]$Repo,$Pins,$Report) {
    # Qualify EVERY exact config/backup before any restoration mutation. Generic
    # Prepare -Disable can archive/delete unrelated current configuration and
    # select historical *.user-* backups; this transaction uses its own copies.
    foreach($role in $Session.roles.Keys) {
        $row=$Session.roles[$role]
        $game=Assert-WorkerPlainPath $row.game -MustExist
        if(@(Get-GameProcessInDirectory -Directory $game).Count){throw 'An MGSV process remains; restoration is blocked.'}
        foreach($snapshot in @($row.configs)){Assert-WorkerConfigSnapshotPreflight $snapshot $game $Run}
    }
    foreach($role in $Session.roles.Keys) {
        $row=$Session.roles[$role];$p=Get-InstancePaths -GameDirectory $row.game
        Assert-WorkerHash $p.Exe $Pins.Exe;Assert-WorkerHash $p.SteamApi $Pins.Steam;Assert-WorkerHash $p.Archive $Pins.Archive
        $package=Assert-WorkerPlainPath $row.baseline_package -MustExist
        if(!(Test-WorkerDescendant $package $Run)){throw 'Baseline package escaped the run.'}
        Assert-WorkerHash (Join-Path $package 'package.json') $row.baseline_package_sha256
        # Per-role baseline (newer sessions); older sessions carry one for both.
        $roleBaseline=if($row.Contains('baseline_sha256') -and [string]$row.baseline_sha256 -cmatch '^[0-9A-Fa-f]{64}$'){[string]$row.baseline_sha256}else{[string]$Session.baseline_sha256}
        Assert-WorkerHash (Join-Path $package 'payload\dinput8.dll') $roleBaseline
        $current=Get-WorkerHash $p.Loader
        if($current -ine $roleBaseline) {
            if($current -ine $Session.candidate_sha256){throw "[$role] installed DLL is neither this worker nor its normal baseline."}
            $record=Read-WorkerSessionJson $p.Record
            if($record.product -cne 'mgs-coop-original-loader-probe' -or $record.game_directory -ine $p.Game -or $record.dll_sha256 -ine $Session.candidate_sha256){throw 'Worker install ownership changed; restoration is blocked.'}
            Invoke-WorkerPackageRestore $Repo $p.Game $package $role
        }
        Assert-WorkerHash $p.Loader $roleBaseline;Assert-WorkerHash $p.Record $row.baseline_record_sha256;Assert-WorkerHash $p.Archive $Pins.Archive
        $Report.roles[$role]=[ordered]@{loader_restored=$true;configuration_restored=$false;baseline_sha256=$roleBaseline}
    }
    foreach($role in $Session.roles.Keys) {
        foreach($snapshot in @($Session.roles[$role].configs)){Restore-WorkerConfigSnapshot $snapshot}
        $Report.roles[$role].configuration_restored=$true
    }
}
if($session.archive_sha256 -ine $pins.Archive -or $session.baseline_sha256 -cnotmatch '^[0-9A-Fa-f]{64}$' -or $session.candidate_sha256 -cnotmatch '^[0-9A-Fa-f]{64}$'){throw 'Worker session package pins differ.'}
$mutex=[Threading.Mutex]::new($false,'Local\MGSCoopBackgroundSession');$lockOwned=$false;$leased=$null
$report=[ordered]@{schema=1;run_id=$runId;run_dir=$run;started_utc=[DateTime]::UtcNow.ToString('o');status='closing';
    supervisor_closed=$false;forced_supervisor_termination=$false;owned_children_closed=$false;roles=[ordered]@{};real_saves_unchanged=$false;
    failure=$null;worker_isolation_failed=$false;system_input_called=$false;focus_called=$false;desktop_switched=$false;gameplay_validated=$false}
try {
    try{$lockOwned=$mutex.WaitOne([TimeSpan]::FromSeconds(30))}catch [Threading.AbandonedMutexException]{$mutex.ReleaseMutex();throw 'Background coordinator lock was abandoned; refusing unreviewed cleanup.'}
    if(!$lockOwned){throw 'Another background session controller is active.'}
    $statusPath=Join-Path $run 'supervisor.json'
    $status=$null
    if($session.supervisor) {
        $supervisorPath=Assert-WorkerPlainPath $session.supervisor.exe -MustExist
        if([IO.Path]::GetFileName($supervisorPath) -cne 'worker_supervisor.exe' -or !(Test-WorkerDescendant $supervisorPath (Join-Path $repo 'build'))){throw 'Supervisor executable is not a worker build.'}
        Assert-WorkerHash $supervisorPath $session.supervisor.sha256
        $leased=Get-WorkerLeasedProcess $session.supervisor
        if(Test-Path -LiteralPath $statusPath) {
            $status=Read-WorkerSessionJson $statusPath
            if(!(Test-WorkerSupervisorIdentity $status $session)){throw 'Supervisor shutdown receipt ownership differs.'}
            if(!(Test-WorkerSupervisorReceipt $status $session)){$report.worker_isolation_failed=$true}
        }
        if($leased) {
            $sequence=[long]$session.shutdown_sequence+1
            if($status){$sequence=[Math]::Max($sequence,[long]$status.control_sequence+1)}
            $session.shutdown_sequence=$sequence;$session.status='closing';Write-WorkerSessionJson $statePath $session
            Write-WorkerAtomicText (Join-Path $run 'supervisor.control') ("MGSWORKERCTL1 $runId $sequence shutdown`n")
            if(!$leased.WaitForExit(10000)) {
                # This held Process handle was creation-time/path leased above.
                # KILL_ON_JOB_CLOSE closes only its job; never kill by image name.
                $leased.Kill();$report.forced_supervisor_termination=$true
                if(!$leased.WaitForExit(5000)){throw 'Exact supervisor process did not terminate; restoration is blocked.'}
            }
        }
        if(Test-Path -LiteralPath $statusPath) {
            $status=Read-WorkerSessionJson $statusPath
            if(!(Test-WorkerSupervisorIdentity $status $session)){throw 'Final supervisor ownership receipt differs.'}
            if(!(Test-WorkerSupervisorReceipt $status $session)){$report.worker_isolation_failed=$true}
            $session.owned_processes=@($status.owned_processes)
        }
    }
    $report.supervisor_closed=$true
    foreach($child in @($session.owned_processes)) {
        if($child.role -cnotin @('host','guest') -or !$session.roles.Contains($child.role)){throw 'Unknown worker child in session receipt.'}
        $expectedExe=Join-Path $session.roles[$child.role].game 'mgsvtpp.exe'
        if($child.exe -ine $expectedExe -or $child.sha256 -ine $pins.Exe){throw 'Owned child image lease differs.'}
        $childProcess=Get-WorkerLeasedProcess $child
        if($childProcess) {try{if(!$childProcess.WaitForExit(5000)){throw 'Owned game survived the supervisor job closure; restoration is blocked.'}}finally{$childProcess.Dispose()}}
    }
    foreach($role in $session.roles.Keys) {
        $directory=Assert-WorkerPlainPath $session.roles[$role].game -MustExist
        if(@(Get-GameProcessInDirectory -Directory $directory).Count){throw 'An MGSV process remains or cannot be attributed; restoration is blocked.'}
    }
    $report.owned_children_closed=$true
    Invoke-WorkerRestoreOwnedSession $session $run $repo $pins $report
    $comparison=Get-WorkerRealSaveComparison $session.real_saves
    $report.real_saves_unchanged=$comparison.Same;$report.real_save_comparison=$comparison
    if(!$comparison.Same){throw 'Real saves differ from the pre-worker manifest; no real-save restoration was attempted.'}
    $report.status='closed';$session.status='closed';$session.closed_utc=[DateTime]::UtcNow.ToString('o')
    Write-WorkerSessionJson $statePath $session
    Write-Host "Owned private worker closed; normal baseline and real-save hashes verified: $run"
    [pscustomobject]@{RunDirectory=$run;Closed=$true;BaselineRestored=$true;RealSavesUnchanged=$true;Forced=$report.forced_supervisor_termination}
} catch {
    $report.status='cleanup_refused';$report.failure=$_.Exception.Message
    $session.status='cleanup_refused';$session.cleanup_failure=$report.failure
    try{$comparison=Get-WorkerRealSaveComparison $session.real_saves;$report.real_saves_unchanged=$comparison.Same;$report.real_save_comparison=$comparison}catch{$report.real_save_check_error=$_.Exception.Message}
    Write-WorkerSessionJson $statePath $session
    throw $report.failure
} finally {
    $report.finished_utc=[DateTime]::UtcNow.ToString('o');Write-WorkerSessionJson (Join-Path $run 'stop.json') $report
    if($leased){$leased.Dispose()};if($lockOwned){$mutex.ReleaseMutex()};$mutex.Dispose()
}
