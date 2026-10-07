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
foreach($name in @('MGS_LOCAL_GUN_PRELOAD_TEST','MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST','MGS_LOCAL_ACTION_EVENTS','MGS_LOCAL_UPPER_END_OBSERVE','MGS_LOCAL_UPPER_PHASE_OBSERVE','MGS_LOCAL_UPPER_MANUAL_IDLE','MGS_LOCAL_UPPER_PROBE','MGS_LOCAL_UPPER_CHANNEL2','MGS_LOCAL_UPPER_FIRE_ZERO_BLEND')){$flags[$name]='OFF'}
$flags.MGS_BACKGROUND_WORKER='ON'
$receipt=@{schema=1;status='passed';plan=@{profile='Normal';project='C:\repo';build_directory='C:\repo\build\worker';flags=$flags};artifacts=@{loader_sha256=$hash}}
Assert-True (Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' $hash) 'normal21 worker build checks'
$receipt.plan.profile='Research';Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' $hash)) 'reject research profile';$receipt.plan.profile='Normal'
foreach($name in @($flags.Keys|Where-Object {$_ -cne 'MGS_BACKGROUND_WORKER'})){$flags[$name]='ON';Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' $hash)) 'reject every experiment';$flags[$name]='OFF'}
$flags.MGS_BACKGROUND_WORKER='OFF';Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' $hash)) 'worker disabled';$flags.MGS_BACKGROUND_WORKER='ON'
Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\other' 'C:\repo' $hash)) 'different build'
Assert-True (!(Test-WorkerCheckReceipt $receipt 'C:\repo\build\worker' 'C:\repo' ('B'*64))) 'different loader'
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
Assert-True (@($startAst.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Copy-WorkerSaveTree'},$true)).Count -eq 0) 'Start never copies previous lab progress'
Assert-True ($start.Contains('Assert-WorkerRealSaveSource $identity.UserData') -and $start.Contains('backup_receipt_sha256=Get-WorkerHash $backupReceipt') -and $start.Contains("backup_manifest_sha256=Get-WorkerHash (Join-Path `$freshBackup 'manifest.json')")) 'fresh save source and backup receipts are qualified'
$startAssignments=@{}
foreach($name in @('freshBackup','manifestBackup','saveRoot','seed','graphics')) {
    $definition=@($startAst.FindAll({param($node)$node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and $node.Left.VariablePath.UserPath -ceq $name},$true))
    Assert-True ($definition.Count -eq 1) ('actual fresh-save assignment '+$name)
    $startAssignments[$name]=$definition[0].Extent.Text
}
Assert-True ($startAssignments.seed -ceq "`$seed=Copy-SaveSeed -BackupDirectory `$freshBackup -SaveDir `$saveRoot -LocalSubdir '287700\local' -RemoteSubdir 'remote'") 'role seed reads only this run verified backup'
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
        . ([scriptblock]::Create($startAssignments.seed))
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
