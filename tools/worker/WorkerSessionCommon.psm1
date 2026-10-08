#Requires -Version 7
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-WorkerRuntimePins {
    [ordered]@{
        Exe='085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45'
        Steam='08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A'
        Archive='87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96'
    }
}
function Test-WorkerRunId([string]$Value) { return $Value -cmatch '\A[0-9a-f]{32}\z' }
function Test-WorkerDescendant([string]$Path,[string]$Root) {
    if(!$Path -or !$Root){return $false}
    $full=[IO.Path]::GetFullPath($Path).TrimEnd([char]92)
    $base=[IO.Path]::GetFullPath($Root).TrimEnd([char]92)
    return $full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)
}
function Assert-WorkerPlainPath([string]$Path,[switch]$MustExist) {
    $full=[IO.Path]::GetFullPath($Path).TrimEnd([char]92)
    if($full -cnotmatch '^[A-Za-z]:\\' -or $full.Length -ge 240 -or $full -match '[\r\n\x00]' -or $full -match '(?i)(^|\\)userdata(\\|$)') {throw 'Worker refuses a relative, device, long or userdata path.'}
    if($MustExist -and !(Test-Path -LiteralPath $full)){throw "Worker input is missing: $full"}
    $cursor=$full
    while($cursor) {
        if(Test-Path -LiteralPath $cursor) {
            if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Worker refuses filesystem links: $cursor"}
        }
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    return $full
}
function Get-WorkerHash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Assert-WorkerHash([string]$Path,[string]$Expected) {
    $null=Assert-WorkerPlainPath $Path -MustExist
    if($Expected -cnotmatch '^[A-Fa-f0-9]{64}$' -or (Get-WorkerHash $Path) -ine $Expected){throw "Worker file pin differs: $Path"}
}
function Write-WorkerSessionJson([string]$Path,$Value) {
    $null=Assert-WorkerPlainPath $Path
    $temporary=$Path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    try {
        [IO.File]::WriteAllText($temporary,($Value|ConvertTo-Json -Depth 20)+"`n",[Text.UTF8Encoding]::new($false))
        [IO.File]::Move($temporary,$Path,$true)
    } finally {if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}}
}
function Read-WorkerSessionJson([string]$Path,[int]$Maximum=1048576) {
    $null=Assert-WorkerPlainPath $Path -MustExist
    $stream=[IO.FileStream]::new($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        if($stream.Length -gt $Maximum){throw 'Worker receipt exceeds its bound.'}
        $reader=[IO.StreamReader]::new($stream,[Text.UTF8Encoding]::new($false,$true))
        try{return $reader.ReadToEnd()|ConvertFrom-Json -AsHashtable}finally{$reader.Dispose()}
    }finally{$stream.Dispose()}
}
function Get-WorkerCopyInventory([string]$Root) {
    $base=Assert-WorkerPlainPath $Root -MustExist
    $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($base)
    $rows=[Collections.Generic.List[object]]::new();$visited=0
    while($pending.Count) {
        $folder=$pending.Pop()
        foreach($item in @(Get-ChildItem -LiteralPath $folder -Force)) {
            if(++$visited -gt 4096){throw 'Worker copy inventory exceeds 4096 entries.'}
            $full=Assert-WorkerPlainPath $item.FullName -MustExist
            if(!(Test-WorkerDescendant $full $base)){throw 'Worker inventory escaped its root.'}
            if($item.PSIsContainer){$pending.Push($full);continue}
            if($item.Length -gt 268435456){throw 'Worker copy file exceeds 256MiB.'}
            $rows.Add([ordered]@{path=[IO.Path]::GetRelativePath($base,$full);size=[long]$item.Length;sha256=Get-WorkerHash $full})
        }
    }
    return @($rows|Sort-Object {$_['path']})
}
function Assert-WorkerRealSaveSource([string]$UserData) {
    # This helper only inspects directory entries. Unlike writable worker paths,
    # the two existing real-save source trees are allowed below userdata.
    $base=[IO.Path]::GetFullPath($UserData).TrimEnd([char]92)
    if($base -cnotmatch '\A[A-Za-z]:\\' -or $base.Length -ge 240 -or $base -match '[\r\n\x00]'){throw 'Invalid real-save source path.'}
    $cursor=$base
    while($cursor) {
        if(!(Test-Path -LiteralPath $cursor) -or ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Real-save source has a missing or linked ancestor.'}
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    $pending=[Collections.Generic.Stack[string]]::new();$entries=0
    foreach($folder in @('287700','311340')) {
        $path=Join-Path $base $folder
        if(!(Test-Path -LiteralPath $path -PathType Container)){throw 'Required real-save source folder is missing.'}
        if((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Real-save source folder is linked.'}
        $pending.Push($path)
    }
    while($pending.Count) {
        foreach($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)) {
            if(++$entries -gt 4096 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Real-save source is linked or exceeds its bound.'}
            if(!(Test-WorkerDescendant $item.FullName $base)){throw 'Real-save source escaped its root.'}
            if($item.PSIsContainer){$pending.Push($item.FullName)}elseif($item.Length -gt 268435456){throw 'Real-save source file exceeds 256MiB.'}
        }
    }
}
function Copy-WorkerSaveTree([string]$Source,[string]$Destination,[string]$RunDirectory) {
    $sourceRoot=Assert-WorkerPlainPath $Source -MustExist
    $destinationRoot=Assert-WorkerPlainPath $Destination
    if(!(Test-WorkerDescendant $destinationRoot $RunDirectory) -or (Test-Path -LiteralPath $destinationRoot)){throw 'Worker save destination must be a fresh run descendant.'}
    $before=@(Get-WorkerCopyInventory $sourceRoot)
    if(!$before.Count){throw 'Worker refuses an empty copied-save source.'}
    $null=New-Item -ItemType Directory -Path $destinationRoot
    foreach($row in $before) {
        $target=[IO.Path]::GetFullPath((Join-Path $destinationRoot $row.path))
        if(!(Test-WorkerDescendant $target $destinationRoot)){throw 'Worker copy target escaped.'}
        $folder=[IO.Path]::GetDirectoryName($target)
        if(!(Test-Path -LiteralPath $folder)){$null=New-Item -ItemType Directory -Path $folder -Force}
        $null=Assert-WorkerPlainPath $folder -MustExist
        [IO.File]::Copy((Join-Path $sourceRoot $row.path),$target,$false)
        Assert-WorkerHash $target $row.sha256
    }
    foreach($folder in @('287700\local','remote')) {
        $target=Join-Path $destinationRoot $folder
        if(!(Test-Path -LiteralPath $target)){$null=New-Item -ItemType Directory -Path $target -Force}
        $null=Assert-WorkerPlainPath $target -MustExist
    }
    $after=@(Get-WorkerCopyInventory $sourceRoot)
    if(!(Compare-HashManifest -Expected $before -Actual $after).Same){throw 'Copied-save source changed during copying.'}
    $copies=@(Get-WorkerCopyInventory $destinationRoot)
    if(!(Compare-HashManifest -Expected $before -Actual $copies).Same){throw 'Worker save tree hash mismatch.'}
    return $copies
}
function Test-WorkerProcessIdentity($Observed,$Expected) {
    return [long]$Observed.pid -eq [long]$Expected.pid -and
        [string]$Observed.creation_filetime -ceq [string]$Expected.creation_filetime -and
        [string]$Observed.exe -ieq [string]$Expected.exe
}
function Get-WorkerLeasedProcess($Expected) {
    $process=Get-Process -Id ([int]$Expected.pid) -ErrorAction SilentlyContinue
    if(!$process){return $null}
    try {
        $null=$process.SafeHandle
        $observed=@{pid=$process.Id;creation_filetime=[string]$process.StartTime.ToUniversalTime().ToFileTimeUtc();exe=$process.Path}
        if(!(Test-WorkerProcessIdentity $observed $Expected)){$process.Dispose();throw 'Worker PID now names a different process; refusing ownership.'}
        return $process
    } catch {$process.Dispose();throw}
}
function Read-WorkerFreshLog([string]$Path,[long]$Offset,[datetime]$NotBeforeUtc,[int]$Maximum=2097152) {
    if(!(Test-Path -LiteralPath $Path -PathType Leaf)){return ''}
    $null=Assert-WorkerPlainPath $Path -MustExist
    $stream=[IO.FileStream]::new($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        if((Get-Item -LiteralPath $Path).LastWriteTimeUtc -lt $NotBeforeUtc){return ''}
        if($Offset -lt 0 -or $stream.Length -lt $Offset){throw 'Worker append-only startup log was truncated; refusing stale evidence.'}
        # early_patches.cpp and steam_observer.cpp both use OPEN_ALWAYS and
        # FILE_APPEND_DATA. Do not treat a truncation as a fresh startup.
        $start=$Offset
        if($stream.Length-$start -gt $Maximum){throw 'Worker fresh startup log exceeds its bound.'}
        $null=$stream.Seek($start,[IO.SeekOrigin]::Begin)
        $reader=[IO.StreamReader]::new($stream,[Text.UTF8Encoding]::new($false,$true))
        try{return $reader.ReadToEnd()}finally{$reader.Dispose()}
    }finally{$stream.Dispose()}
}
function Set-WorkerIniValue([string]$Text,[string]$Key,[string]$Value) {
    if($Key -cnotmatch '^[a-z_]+$' -or $Value -match '[\r\n\x00]'){throw 'Invalid worker INI replacement.'}
    $pattern='(?m)^'+[regex]::Escape($Key)+'=[^\r\n]*\r?$'
    if([regex]::Matches($Text,$pattern).Count -ne 1){throw "Expected one generated INI key: $Key"}
    $replacement=$Key+'='+$Value
    return [regex]::Replace($Text,$pattern,[Text.RegularExpressions.MatchEvaluator]{param($match)$replacement})
}
function Test-WorkerSupervisorIdentity($Status,$Session) {
    return $Status.schema -eq 1 -and $Status.run_id -ceq $Session.run_id -and
        $Status.desktop -ceq ('MGSWorker_'+$Session.run_id) -and
        [long]$Status.supervisor_pid -eq [long]$Session.supervisor.pid -and
        [string]$Status.supervisor_start_filetime -ceq [string]$Session.supervisor.creation_filetime -and
        [string]$Status.supervisor_sha256 -ieq [string]$Session.supervisor.sha256
}
function Test-WorkerSupervisorReceipt($Status,$Session) {
    return (Test-WorkerSupervisorIdentity $Status $Session) -and
        !$Status.desktop_ownership_violation -and !$Status.input_desktop_private -and
        !$Status.switch_desktop_called -and !$Status.system_input_called -and !$Status.focus_called -and
        $Status.visible_desktop_unchanged_at_observations -and $Status.job_kill_on_close -and !$Status.handle_inheritance
}
function Test-WorkerCheckReceipt($Receipt,[string]$BuildDirectory,[string]$RepoRoot,[string]$LoaderSha256) {
    if($Receipt.schema -ne 1 -or $Receipt.status -cne 'passed' -or $Receipt.plan.profile -cne 'Normal' -or
       $Receipt.plan.project -ine $RepoRoot -or $Receipt.plan.build_directory -ine $BuildDirectory -or
       $Receipt.plan.flags.MGS_BACKGROUND_WORKER -cne 'ON' -or $Receipt.artifacts.loader_sha256 -ine $LoaderSha256){return $false}
    $experiments=@('MGS_LOCAL_GUN_PRELOAD_TEST','MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST',
        'MGS_LOCAL_STOCK46_WRITER_TEST','MGS_LOCAL_STOCK46_AMMO_TEST','MGS_LOCAL_ACTION_EVENTS','MGS_LOCAL_UPPER_END_OBSERVE','MGS_LOCAL_UPPER_PHASE_OBSERVE',
        'MGS_LOCAL_UPPER_MANUAL_IDLE','MGS_LOCAL_UPPER_PROBE','MGS_LOCAL_UPPER_CHANNEL2','MGS_LOCAL_UPPER_FIRE_ZERO_BLEND')
    if($Receipt.plan.flags.Count -ne $experiments.Count+1){return $false}
    foreach($name in $experiments){if($Receipt.plan.flags[$name] -cne 'OFF'){return $false}}
    return $true
}
function Test-WorkerContextText([string]$Text,[string]$RunId,[string]$Role,$Process) {
    if($Text -cnotmatch '\AMGSWORKERCTX1 ([0-9a-f]{32}) (host|guest) ([1-9][0-9]*) ([1-9][0-9]*) (MGSWorker_[0-9a-f]{32}) admitted\n\z'){return $false}
    return $Matches[1] -ceq $RunId -and $Matches[2] -ceq $Role -and [long]$Matches[3] -eq [long]$Process.pid -and
        $Matches[4] -ceq [string]$Process.creation_filetime -and $Matches[5] -ceq ('MGSWorker_'+$RunId)
}
function Read-WorkerBoundedText([string]$Path,[int]$Maximum=65536) {
    $null=Assert-WorkerPlainPath $Path -MustExist
    $stream=[IO.FileStream]::new($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        if($stream.Length -gt $Maximum){throw 'Worker text exceeds its bound.'}
        $reader=[IO.StreamReader]::new($stream,[Text.UTF8Encoding]::new($false,$true))
        try{return $reader.ReadToEnd()}finally{$reader.Dispose()}
    }finally{$stream.Dispose()}
}
function Write-WorkerAtomicText([string]$Path,[string]$Text) {
    $null=Assert-WorkerPlainPath $Path
    $temporary=$Path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    try{[IO.File]::WriteAllText($temporary,$Text,[Text.UTF8Encoding]::new($false))
        # Bounded retry: sync clients (OneDrive) and scanners briefly hold files.
        for($attempt=0;;$attempt++){try{[IO.File]::Move($temporary,$Path,$true);break}catch [UnauthorizedAccessException],[IO.IOException]{if($attempt -ge 49){throw};Start-Sleep -Milliseconds 10}}}
    finally{if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}}
}
function Get-WorkerConfigSnapshot([string]$Path,[string]$BackupDirectory,[string]$Name) {
    $null=Assert-WorkerPlainPath $Path
    $present=Test-Path -LiteralPath $Path -PathType Leaf
    $result=[ordered]@{path=$Path;present=$present;sha256=$null;backup=$null;worker_sha256=$null}
    if($present) {
        $result.sha256=Get-WorkerHash $Path;$result.backup=Join-Path $BackupDirectory $Name
        [IO.File]::Copy($Path,$result.backup,$false);Assert-WorkerHash $result.backup $result.sha256
    }
    return $result
}
function Restore-WorkerConfigSnapshot($Snapshot) {
    $path=Assert-WorkerPlainPath $Snapshot.path
    $present=Test-Path -LiteralPath $path -PathType Leaf
    $current=if($present){Get-WorkerHash $path}else{$null}
    if($Snapshot.present -and $current -ieq $Snapshot.sha256){return}
    if($present -and (!$Snapshot.worker_sha256 -or $current -ine $Snapshot.worker_sha256)){throw "Worker refuses to overwrite a changed configuration: $path"}
    if($Snapshot.present) {
        Assert-WorkerHash $Snapshot.backup $Snapshot.sha256
        [IO.File]::Copy($Snapshot.backup,$path,$true);Assert-WorkerHash $path $Snapshot.sha256
    }elseif($present){Remove-Item -LiteralPath $path}
}
function Test-WorkerPreparedConfigText([string]$Name,[string]$Text,[string]$Role,[string]$HostSteamId,[string]$MasterUrl,[string[]]$AllowedSaveDirs) {
    switch -CaseSensitive ($Name) {
        'script-observe.enabled' {return $Text -ceq "offline-v1`n"}
        'client-host.txt' {return $Role -ceq 'guest' -and $Text -ceq ($HostSteamId+"`n")}
        'lab.ini' {
            if(!(Test-LabGeneratedText $Text)){return $false}
            $ini=ConvertFrom-IniText $Text
            return $ini['enabled'] -ceq '1' -and $ini['role'] -ceq $Role -and $ini['loopback'] -ceq '1' -and
                $ini['xinput'] -ceq 'none' -and @($AllowedSaveDirs|Where-Object {$_ -and $_ -ieq $ini['save_dir']}).Count -eq 1
        }
        'coop.ini' {
            if(!(Test-LabGeneratedText $Text)){return $false}
            $ini=ConvertFrom-IniText $Text
            return $ini['master_url'] -ceq $MasterUrl
        }
        default {return $false}
    }
}
# The lab.ini partner settings of each admitted worker profile (pure; the
# native gate tools/worker/worker_game_gate.hpp admits exactly these).
# -NativeStage only with the native profile (default loco there).
function Get-WorkerPartnerSettings([string]$PartnerProfile,[string]$NativeStage='') {
    $animated=@('partner=1','net=1','partner_stage=3','partner_source=net','partner_look=net','partner_apply=xform','partner_anim=off',
        'partner_reset=n','partner_input=off','partner_dm=map','world=3')
    if($PartnerProfile -cne 'native' -and $NativeStage){throw '-NativeStage needs -PartnerProfile native.'}
    switch -CaseSensitive ($PartnerProfile) {
        'animated' { return $animated }
        'input' {
            return @('partner=1','net=1','partner_source=net','partner_apply=input','partner_input=net','partner_input_send=1',
                'partner_input_hold=own','partner_input_words=hpr','partner_recon=track','partner_stage=3','world=3')
        }
        'native' {
            # Fallback base identical to the animated profile, plus the native drive (docs/native-puppet.md 13.1).
            $native=$animated+@('partner_drive=native','partner_input_send=1','partner_input_hook=1','partner_input_hold=own',
                'partner_input_words=hpr','partner_input_wrap=window','partner_input_cam=sandbox','partner_input_local=main',
                'partner_recon=track','partner_input_slot=body')
            $stage=if($NativeStage){$NativeStage}else{'loco'}
            $extra=switch -CaseSensitive ($stage) {
                'census' {@('partner_input_defer=census','partner_input_mask=0x2200006','partner_native_aim=off')}
                'loco' {@('partner_input_defer=on','partner_input_mask=0x2200006','partner_native_aim=off')}
                'gun' {@('partner_input_defer=on','partner_input_mask=0x220004E','partner_native_gun=1','partner_native_fire=0',
                    'partner_native_aim=look','partner_input_triggers=1','partner_input_rstick=1')}
                default {throw "Unknown native stage: $stage"}
            }
            return @($native+$extra)
        }
        default { throw "Unknown partner profile: $PartnerProfile" }
    }
}
# The research build the native gun stage runs on: exactly the stock46
# preload+writer experiments (optionally the ammo experiment), worker ON, no
# upper_* probe/phase/idle experiment (they detour gun functions the native
# drive now runs on the main thread). Returns the sorted experiment list.
function Get-WorkerResearchExperiments([switch]$Ammo) {
    $list=@('MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST')
    if($Ammo){$list+='MGS_LOCAL_STOCK46_AMMO_TEST'}
    return @($list|Sort-Object)
}
function Test-WorkerResearchReceipt($Receipt,[string]$BuildDirectory,[string]$RepoRoot,[string]$LoaderSha256,[string[]]$Experiments) {
    if($Receipt.schema -ne 1 -or $Receipt.status -cne 'passed' -or $Receipt.plan.profile -cne 'Research' -or
       $Receipt.plan.project -ine $RepoRoot -or $Receipt.plan.build_directory -ine $BuildDirectory -or
       $Receipt.plan.flags.MGS_BACKGROUND_WORKER -cne 'ON' -or $Receipt.artifacts.loader_sha256 -ine $LoaderSha256){return $false}
    foreach($name in @($Receipt.plan.flags.Keys)){
        if($name -ceq 'MGS_BACKGROUND_WORKER'){continue}
        $expected=if($Experiments -ccontains $name){'ON'}else{'OFF'}
        if($Receipt.plan.flags[$name] -cne $expected){return $false}
    }
    foreach($name in $Experiments){if(!$Receipt.plan.flags.Contains($name) -or $name -clike 'MGS_LOCAL_UPPER_*'){return $false}}
    return $true
}
# The Invoke-WorkerChecks cache directory of a Research worker build with
# exactly these experiments (its Get-WorkerPlan profile key).
function Get-WorkerResearchBuildDirectory([string]$RepoRoot,[string[]]$Experiments) {
    $text=(@($Experiments|Sort-Object)) -join "`n"
    $hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($text))).Substring(0,12)
    return Join-Path $RepoRoot ('build\worker-checks\research-'+$hash+'-worker-on-release')
}
Export-ModuleMember -Function *
