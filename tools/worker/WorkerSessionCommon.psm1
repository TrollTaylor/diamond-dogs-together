#Requires -Version 7
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Set-WorkerGraphicsQualityText {
    # Pure. Only low edits the eight existing quality strings and integrity tag.
    # Per-key retail options: NVIDIA's MGSV graphics/performance guide confirms
    # six Low, SSAO Off and filtering Medium; pinned retail serialization agrees.
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Text,
        [ValidateSet('preserve','low')][string]$Quality='preserve'
    )
    if($Quality -eq 'preserve'){return $Text}
    if([Text.Encoding]::UTF8.GetByteCount($Text) -gt 65536){throw 'Worker graphics config exceeds its bound.'}
    $document=$null
    try {
        $document=[System.Text.Json.JsonDocument]::Parse($Text,[System.Text.Json.JsonDocumentOptions]::new())
        $nodes=[Collections.Generic.Stack[System.Text.Json.JsonElement]]::new();$nodes.Push($document.RootElement)
        while($nodes.Count){
            $node=$nodes.Pop()
            if($node.ValueKind -eq [System.Text.Json.JsonValueKind]::Object){
                $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
                foreach($property in $node.EnumerateObject()){
                    if(!$seen.Add($property.Name)){throw 'Worker graphics config has a duplicate JSON key.'}
                    $nodes.Push($property.Value)
                }
            } elseif($node.ValueKind -eq [System.Text.Json.JsonValueKind]::Array){
                foreach($item in $node.EnumerateArray()){$nodes.Push($item)}
            }
        }
        $root=$document.RootElement
        if($root.ValueKind -ne [System.Text.Json.JsonValueKind]::Object){throw 'Worker graphics root must be an object.'}
        $project=$root.GetProperty('project');$graphics=$root.GetProperty('graphics');$storedTag=$root.GetProperty('as')
        if($project.ValueKind -ne [System.Text.Json.JsonValueKind]::String -or $project.GetString() -cne 'tpp' -or
           $graphics.ValueKind -ne [System.Text.Json.JsonValueKind]::Object -or $storedTag.ValueKind -ne [System.Text.Json.JsonValueKind]::String){
            throw 'Worker graphics config has an unsupported schema.'
        }
        $version=$graphics.GetProperty('version');$versionNumber=0
        if($version.ValueKind -ne [System.Text.Json.JsonValueKind]::Number -or !$version.TryGetInt32([ref]$versionNumber) -or $versionNumber -ne 1){
            throw 'Worker graphics config has an unsupported version.'
        }
        $settings=$graphics.GetProperty('quality_setting')
        if($settings.ValueKind -ne [System.Text.Json.JsonValueKind]::Object){throw 'Worker graphics quality settings must be an object.'}
        $values=[ordered]@{texture='Low';effect='Low';lighting='Low';model_detail='Low';postprocess='Low';shadow='Low';ssao='Off';texture_filtering='Medium'}
        foreach($key in $values.Keys){
            if($settings.GetProperty($key).ValueKind -ne [System.Text.Json.JsonValueKind]::String){throw "Worker graphics key must be an existing string: $key"}
        }
        $before=Get-GraphicsConfigInfo -Text $Text
        if(!$before.Valid){throw 'Worker graphics config integrity tag does not verify.'}
        $result=$Text
        foreach($key in $values.Keys){
            $pattern='("'+[regex]::Escape($key)+'"\s*:\s*")[^"\\]*(")'
            if([regex]::Matches($result,$pattern).Count -ne 1){throw "Worker graphics key has an unexpected layout: $key"}
            $value=$values[$key]
            $result=[regex]::Replace($result,$pattern,{param($m)$m.Groups[1].Value+$value+$m.Groups[2].Value})
        }
        $tag=Get-GraphicsConfigTag -Object ($result|ConvertFrom-Json)
        $pattern='("as"\s*:\s*")[A-Za-z0-9+/=]+(")'
        if([regex]::Matches($result,$pattern).Count -ne 1){throw 'Worker graphics tag has an unexpected layout.'}
        $result=[regex]::Replace($result,$pattern,{param($m)$m.Groups[1].Value+$tag+$m.Groups[2].Value})
        $after=Get-GraphicsConfigInfo -Text $result
        if(!$after.Valid -or $after.WindowMode -cne $before.WindowMode -or $after.Width -ne $before.Width -or $after.Height -ne $before.Height){
            throw 'Edited worker graphics config failed validation.'
        }
        $readback=($result|ConvertFrom-Json).graphics.quality_setting
        foreach($key in $values.Keys){if($readback.$key -cne $values[$key]){throw 'Edited worker quality value failed readback.'}}
        return $result
    } finally {if($null -ne $document){$document.Dispose()}}
}

function Get-WorkerRuntimePins {
    [ordered]@{
        Exe='085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45'
        Steam='08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A'
        Archive='87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96'
    }
}
function Get-WorkerBuddyCapacityPins {
    # Local research catalog only. Production pins above are immutable.
    [ordered]@{Profile='buddy_capacity_two_v1';Archive='4B290CDDC14F1146DCE5A64A1008365D572B424B319874EF77E9496634D75EC9';Bytes=17501184;
        Catalog='4A7E7EA890ADF8F261368CD546CCFD3DBFDC4407EF6AF44AFF308B98E53D0989';Motions=21;Capacity=2}
}
function Get-WorkerMgoSleepRestPins {
    [ordered]@{Profile='mgo_sleep_rest_v1';Archive='0A8A13467B6782EE3E32A5B99F8C950CEA9A6D8CB486FAAC10C0A417200BAE54';Bytes=26949632;
        Catalog='D70047206D28561D2CE8831BA0AB0311C8CB2CDB864A01373907DD4318E3F755';Motions=21;Capacity=0}
}
function Get-WorkerMgoUniqueRestPins {
    [ordered]@{Profile='mgo_unique_rest_v1';Archive='92E935BCED30DCD7A1EDA498462A375A8594241A1CFA7228026E3E346E886C25';Bytes=16360448;
        Catalog='CD23AA3146823AF8577079009ED54CFA6F1508F2AE2E7D55E8A0B55DAB46AD51';Motions=21;Capacity=0}
}
function Get-WorkerMgoStandingCyclePins {
    [ordered]@{Profile='mgo_standing_cycle_v1';Archive='882E683B296E97F355B2D2E888194D9EE2BEA3C64EE1EA93BA1654601E1C34C2';Bytes=16362496;
        Catalog='88A1F5C70E2A0812429E2B75BB6ED55718E0AAFD9B53CBE16AA0464B4F83BAC4';Motions=21;Capacity=0}
}
function Assert-WorkerMgoStandingCycleScope([string]$BuildProfile,[string]$Mode,[string]$PartnerProfile,[string]$NativeStage,[bool]$Ammo,[string]$Archive,[string]$BuddyArchive,[string]$OldMgoArchive,[string]$TwoRestArchive) {
    if($BuildProfile -ceq 'mgo-standing-cycle'){
        if($Mode -cne 'lab' -or $PartnerProfile -cne 'native' -or $NativeStage -cne 'gun' -or $Ammo -or !$Archive -or $BuddyArchive -or $OldMgoArchive -or $TwoRestArchive){
            throw 'Standing cycle requires its exact explicit archive and copied-save native gun lab; no mixed archive, ammo or visible play.'
        }
    } elseif($Archive){throw '-MgoStandingCycleArchive needs -BuildProfile mgo-standing-cycle.'}
}
function Get-WorkerMgoStandingCycleExperiments {
    param([switch]$LookupObserver,[switch]$FrameObserver)
    if($FrameObserver -and !$LookupObserver){throw 'Frame observer requires the original-forward standing lookup observer.'}
    $flags=@('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST','MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER')
    if($LookupObserver){$flags+='MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER'}
    if($FrameObserver){$flags+='MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER'}
    return $flags
}
function Assert-WorkerMgoUniqueRestScope([string]$BuildProfile,[string]$Mode,[string]$PartnerProfile,[string]$NativeStage,[bool]$Ammo,[string]$Archive,[string]$BuddyArchive,[string]$OldMgoArchive) {
    if($BuildProfile -ceq 'mgo-unique-rest'){
        if($Mode -cne 'lab' -or $PartnerProfile -cne 'native' -or $NativeStage -cne 'gun' -or $Ammo -or !$Archive -or $BuddyArchive -or $OldMgoArchive){
            throw 'Unique MGO bank requires its exact explicit archive, native gun and copied-save private lab; no mixed archive or ammo.'
        }
    } elseif($Archive){throw '-MgoUniqueRestArchive needs -BuildProfile mgo-unique-rest.'}
}
function Get-WorkerMgoUniqueRestExperiments {
    return @('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST','MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER')
}
function Assert-WorkerMgoSleepRestScope([string]$BuildProfile,[string]$Mode,[string]$PartnerProfile,[string]$NativeStage,[bool]$Ammo,[string]$Archive,[string]$BuddyArchive) {
    # Pure: no preparation/launch is allowed until this explicit scope passes.
    if($BuildProfile -ceq 'mgo-sleep-rest'){
        if($Mode -cne 'lab' -or $PartnerProfile -cne 'native' -or $NativeStage -cne 'gun' -or $Ammo -or !$Archive -or $BuddyArchive){
            throw 'MGO rest requires its explicit archive, native gun stage and copied-save lab; no ammo or buddy archive.'
        }
    } elseif($Archive){throw '-MgoSleepRestArchive needs -BuildProfile mgo-sleep-rest.'}
}
function Get-WorkerSessionRuntimeCatalog($Session,$Pins) {
    # Pure. Older baseline sessions omit every experiment field. Partial or
    # unknown declarations never expand the archive restoration allowance.
    $keys=@('runtime_profile','runtime_archive_sha256','runtime_archive_bytes','runtime_motion_count','runtime_buddy_capacity','runtime_catalog_sha256')
    $present=@($keys|Where-Object{$Session.Contains($_)})
    if(!$present.Count){
        if($Session.Contains('runtime_build_profile') -or $Session.Contains('runtime_native_stage')){throw 'Orphan runtime experiment scope.'}
        return [ordered]@{Profile='';Archive=$Pins.Archive;Bytes=16333824;Motions=21;Capacity=0;Experimental=$false}
    }
    $standing=$Session.Contains('runtime_profile') -and $Session.runtime_profile -ceq 'mgo_standing_cycle_v1'
    $unique=$Session.Contains('runtime_profile') -and $Session.runtime_profile -ceq 'mgo_unique_rest_v1'
    $mgo=$standing -or $unique -or ($Session.Contains('runtime_profile') -and $Session.runtime_profile -ceq 'mgo_sleep_rest_v1')
    $candidate=if($standing){Get-WorkerMgoStandingCyclePins}elseif($unique){Get-WorkerMgoUniqueRestPins}elseif($mgo){Get-WorkerMgoSleepRestPins}else{Get-WorkerBuddyCapacityPins}
    $requiredBuild=if($standing){'mgo-standing-cycle'}elseif($unique){'mgo-unique-rest'}else{'research'}
    $scopeKeys=@('runtime_build_profile','runtime_native_stage')
    if($mgo){
        if(@($scopeKeys|Where-Object{$Session.Contains($_)}).Count -ne 2 -or
           $Session.runtime_build_profile -cne $requiredBuild -or $Session.runtime_native_stage -cne 'gun' -or
           [string]$Session.runtime_archive_bytes -cne [string]$candidate.Bytes -or
           [string]$Session.runtime_motion_count -cne '21' -or [string]$Session.runtime_buddy_capacity -cne '0'){
            throw 'MGO rest session must retain its Research/native gun scope.'
        }
    } elseif(@($scopeKeys|Where-Object{$Session.Contains($_)}).Count){throw 'Runtime scope keys are only valid for the exact MGO rest profile.'}
    if($present.Count -ne $keys.Count -or $Session.runtime_profile -cne $candidate.Profile -or
       $Session.runtime_archive_sha256 -ine $candidate.Archive -or [long]$Session.runtime_archive_bytes -ne $candidate.Bytes -or
       [int]$Session.runtime_motion_count -ne $candidate.Motions -or [int]$Session.runtime_buddy_capacity -ne $candidate.Capacity -or
       $Session.runtime_catalog_sha256 -ine $candidate.Catalog -or $Session.mode -cne 'lab'){
        throw 'Worker refuses an incomplete or changed experimental runtime catalog.'
    }
    return [ordered]@{Profile=$candidate.Profile;Archive=$candidate.Archive;Bytes=$candidate.Bytes;Motions=$candidate.Motions;Capacity=$candidate.Capacity;Experimental=$true}
}
function Get-WorkerRestorePair($Session,$Pins,[string]$BaselineLoader,[string]$CurrentLoader,[string]$CurrentArchive) {
    # Pure ownership policy. Coherent original or this run's installed pair,
    # including partial preparation where only one role has been upgraded.
    $catalog=Get-WorkerSessionRuntimeCatalog $Session $Pins
    if($CurrentLoader -ieq $BaselineLoader -and $CurrentArchive -ieq $Pins.Archive){return 'baseline'}
    if($CurrentLoader -ieq $Session.candidate_sha256 -and $CurrentArchive -ieq $catalog.Archive){return 'worker'}
    throw 'Installed loader/archive are not a coherent owned worker or baseline pair.'
}
# A Diamond Dogs Together drag-and-drop install (tools/package.ps1 -Friend; the
# launcher core's ReadPackage/LoaderIsOurs in native/launcher/core/core_install.*)
# keeps mgs-coop\package.json beside the loader it shipped and never writes
# probe-install.json. Returns that package's dinput8.dll hash (upper case) when
# the folder holds one exact drop-in manifest for the pinned game files: product
# mgs-coop-local-package, layout drop-in, an X.Y.Z version, exactly one files[]
# row with target dinput8.dll and a full SHA-256, and the pinned executable,
# Steam DLL and runtime archive hashes. Anything else returns $null. Read only.
function Get-WorkerDropInLoaderSha256([string]$GameDirectory,$Pins) {
    $path=Join-Path ([IO.Path]::GetFullPath($GameDirectory).TrimEnd([char]92)) 'mgs-coop\package.json'
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){return $null}
    $package=$null
    try{$package=Read-WorkerSessionJson $path}catch{return $null}
    if($package -isnot [Collections.IDictionary]){return $null}
    if([string]$package['product'] -cne 'mgs-coop-local-package' -or [string]$package['layout'] -cne 'drop-in' -or
        [string]$package['version'] -cnotmatch '\A[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,6}\z'){return $null}
    if([string]$package['executable_sha256'] -ine $Pins.Exe -or [string]$package['steam_dll_sha256'] -ine $Pins.Steam){return $null}
    $archive=$package['runtime_archive']
    if($archive -isnot [Collections.IDictionary] -or [string]$archive['sha256'] -ine $Pins.Archive){return $null}
    $rows=@(@($package['files'])|Where-Object{$_ -is [Collections.IDictionary] -and [string]$_['target'] -ceq 'dinput8.dll'})
    if($rows.Count -ne 1 -or [string]$rows[0]['sha256'] -cnotmatch '\A[A-Fa-f0-9]{64}\z'){return $null}
    return ([string]$rows[0]['sha256']).ToUpperInvariant()
}
# The normal baseline loader of one worker game folder and what owns it. An
# owned package-install names it in the folder's own probe-install.json (product
# mgs-coop-original-loader-probe, this exact folder, dll_sha256 = the installed
# loader): Owner 'record'. A Diamond Dogs Together drag-and-drop install owns it
# through its drop-in package.json (Get-WorkerDropInLoaderSha256) and leaves any
# earlier probe-install.json stale or absent: Owner 'drop-in'; that record is
# then only backed up and restored byte for byte (or kept absent). A foreign or
# unknown loader refuses. Read only. Returns Sha256 (upper case), Owner,
# RecordPresent and, for drop-in, the package.json hash.
function Get-WorkerOwnedBaseline([string]$GameDirectory,$Pins) {
    $game=Assert-WorkerPlainPath ([IO.Path]::GetFullPath($GameDirectory)) -MustExist
    $loader=Assert-WorkerPlainPath (Join-Path $game 'dinput8.dll') -MustExist
    if(!(Test-Path -LiteralPath $loader -PathType Leaf)){throw 'Baseline loader is not a file.'}
    $record=Assert-WorkerPlainPath (Join-Path $game 'mgs-coop\probe-install.json')
    $recordPresent=Test-Path -LiteralPath $record -PathType Leaf
    if(!$recordPresent -and (Test-Path -LiteralPath $record)){throw 'The ownership record is not a file.'}
    $current=(Get-WorkerHash $loader).ToUpperInvariant()
    if($recordPresent){
        $owner=$null;try{$owner=Read-WorkerSessionJson $record}catch{$owner=$null}
        if($owner -is [Collections.IDictionary] -and [string]$owner['product'] -ceq 'mgs-coop-original-loader-probe' -and [string]$owner['game_directory'] -ieq $game -and
            [string]$owner['dll_sha256'] -cmatch '\A[A-Fa-f0-9]{64}\z' -and [string]$owner['dll_sha256'] -ieq $current){
            return [pscustomobject]@{Sha256=$current;Owner='record';RecordPresent=$true;PackageSha256=$null}
        }
    }
    $dropIn=Get-WorkerDropInLoaderSha256 $game $Pins
    if($dropIn -and $dropIn -ceq $current){
        return [pscustomobject]@{Sha256=$current;Owner='drop-in';RecordPresent=$recordPresent;PackageSha256=Get-WorkerHash (Join-Path $game 'mgs-coop\package.json')}
    }
    throw 'Baseline loader is not owned by this exact game folder.'
}
# Stop's check of a role's baseline ownership record: the exact bytes Start
# backed up, or, for a drop-in owned copy that had none
# (baseline_record_present false), its absence. Sessions written before the
# drop-in gate always had one.
function Assert-WorkerBaselineRecord([string]$Path,$Row) {
    $null=Assert-WorkerPlainPath $Path
    if($Row.Contains('baseline_record_present') -and $Row['baseline_record_present'] -eq $false){
        if(Test-Path -LiteralPath $Path){throw 'The baseline had no ownership record, but one exists now.'}
        return
    }
    Assert-WorkerHash $Path ([string]$Row['baseline_record_sha256'])
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
    # Every experiment Invoke-WorkerChecks (Get-WorkerExperiments) writes into a receipt; all must be OFF.
    $experiments=@('MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER','MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER','MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER',
        'MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER','MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER',
        'MGS_LOCAL_BUDDY_CAPACITY_TEST','MGS_LOCAL_GUN_PRELOAD_TEST','MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST',
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
# Deletes a leftover world.trigger and its atomic-write temporaries
# (world.trigger.<32 hex>.tmp) in one game's mgs-coop folder, so a trigger an
# aborted "host worldtrigger" step left behind is never applied by a later
# session (Start-WorkerSession before launch, Stop-WorkerSession after the
# games closed). Nothing else is touched; a linked entry refuses. Returns the
# number of files removed.
function Remove-WorkerWorldTrigger([string]$GameDirectory) {
    $mod=Join-Path ([IO.Path]::GetFullPath($GameDirectory)) 'mgs-coop'
    if(!(Test-Path -LiteralPath $mod -PathType Container)){return 0}
    $null=Assert-WorkerPlainPath $mod -MustExist
    $removed=0
    foreach($item in @(Get-ChildItem -LiteralPath $mod -Force|Where-Object{$_.Name -ceq 'world.trigger' -or $_.Name -cmatch '\Aworld\.trigger\.[0-9a-f]{32}\.tmp\z'})) {
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Worker refuses a linked world trigger: $($item.FullName)"}
        if($item.PSIsContainer){throw "Worker world trigger is not a file: $($item.FullName)"}
        Remove-Item -LiteralPath $item.FullName -Force
        $removed++
    }
    return $removed
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
function Test-WorkerPreparedConfigText([string]$Name,[string]$Text,[string]$Role,[string]$HostSteamId,[string]$MasterUrl,[string[]]$AllowedSaveDirs,[string]$Mode='lab') {
    switch -CaseSensitive ($Name) {
        'script-observe.enabled' {return $Text -ceq "offline-v1`n"}
        'client-host.txt' {return $Mode -ceq 'lab' -and $Role -ceq 'guest' -and $Text -ceq ($HostSteamId+"`n")}
        'lab.ini' {
            if($Mode -ceq 'play') {
                # The play rehearsal's lab.ini (ConvertTo-WorkerPlayLabIniText).
                if(!(Test-CoopPlayGeneratedText $Text)){return $false}
                $ini=ConvertFrom-IniText $Text
                return $ini['enabled'] -ceq '1' -and $ini['mode'] -ceq 'play' -and $ini['play_rehearsal'] -ceq '1' -and $ini['role'] -ceq $Role -and
                    $ini['xinput'] -ceq 'none' -and $ini['net_bind'] -ceq 'loopback' -and !$ini.Contains('loopback') -and
                    @($AllowedSaveDirs|Where-Object {$_ -and $_ -ieq $ini['save_dir']}).Count -eq 1
            }
            if(!(Test-LabGeneratedText $Text)){return $false}
            $ini=ConvertFrom-IniText $Text
            return $ini['enabled'] -ceq '1' -and $ini['role'] -ceq $Role -and $ini['loopback'] -ceq '1' -and
                $ini['xinput'] -ceq 'none' -and @($AllowedSaveDirs|Where-Object {$_ -and $_ -ieq $ini['save_dir']}).Count -eq 1
        }
        # The play rehearsal's fresh random link secret (20 Crockford characters + CRLF).
        'link-secret.txt' {return $Mode -ceq 'play' -and $Text -cmatch '\A[0-9A-HJKMNP-TV-Z]{20}\r\n\z'}
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
                'census' {@('partner_input_defer=census','partner_input_mask=0x2240006','partner_native_aim=off')}
                'loco' {@('partner_input_defer=on','partner_input_mask=0x2240006','partner_native_aim=off','partner_native_rates=mirror')}
                # Visual fire (docs/native-puppet.md "Weapons"): FIRE reaches body 1 only
                # while the RequestBullet owner filter is live; its shots never hit.
                # The aim-target mirror (docs/native-puppet.md "Facing"): the partner's gun aims where
                # the remote aims (-LabSetting partner_native_aimpt=off|look for the A/B).
                # The partner's own reload (the event machine, docs/native-puppet.md "Weapons" R1..R6) and the
                # weapon switch's slot follower (W4) and the generalized writer (W2/W3: the remote's three
                # slots or same-type substitutes); -LabSetting partner_native_reload=mirror|raw|event /
                # partner_native_slot=off|write|call / partner_native_equip=stock46|manifest|substitute select the A/B variants.
                'gun' {@('partner_input_defer=on','partner_input_mask=0x224005E','partner_native_gun=1','partner_native_fire=visual',
                    'partner_native_aim=look','partner_input_triggers=1','partner_input_rstick=1','partner_native_rates=mirror',
                    'partner_native_aimpt=point','partner_native_reload=event','partner_native_slot=write','partner_native_equip=substitute')}
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
    # The stock46 preload requires the offline equipment allocation (CMakeLists.txt).
    $list=@('MGS_LOCAL_OFFLINE_EQUIPMENT_TEST','MGS_LOCAL_STOCK46_PRELOAD_TEST','MGS_LOCAL_STOCK46_WRITER_TEST')
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
# --- The one-PC play rehearsal (docs/two-pc-play.md "Hidden play rehearsal") ---
# The partner lines a play lab.ini gets for a worker profile: nothing for the
# clip partner (Play-Coop -Partner clips), partner_native_profile for the native
# loco or gun stage (Play-Coop -Partner native writes gun). Pure.
function Get-WorkerPlayPartnerSettings([string]$PartnerProfile,[string]$NativeStage='') {
    if($PartnerProfile -cne 'native' -and $NativeStage){throw '-NativeStage needs -PartnerProfile native.'}
    switch -CaseSensitive ($PartnerProfile) {
        'animated' {return @()}
        'native' {
            $stage=if($NativeStage){$NativeStage}else{'loco'}
            if($stage -cnotin @('loco','gun')){throw "-Mode play runs the native profile loco or gun (got $stage)."}
            return @('partner_native_profile='+$stage)
        }
        default {throw "-Mode play runs the animated (clips) or native profile (got $PartnerProfile)."}
    }
}
# The rehearsal's lab.ini from Play-Coop's own UDP play text
# (New-CoopPlayLabIniText; the guest made with a placeholder host address):
# moved onto 127.0.0.1 (host listens on 47830 and targets 47831, the guest
# listens on 47831 and targets 47830), plus play_rehearsal=1, xinput=none and
# the extra key=value lines (partner profile, -WanSim preset, -LabSetting). A
# key already in the text is replaced, never duplicated. The worker's
# save_dir and mutex_suffix are set afterwards like in the lab. Pure.
function ConvertTo-WorkerPlayLabIniText([string]$BaseText,[string]$Role,[string[]]$Extra=@()) {
    if($Role -cnotin @('host','guest')){throw "Invalid role: $Role"}
    if(!(Test-CoopPlayGeneratedText $BaseText) -or $BaseText -cnotmatch '(?m)^mode=play$' -or $BaseText -cmatch '(?m)^net_transport=steam$'){
        throw 'The play rehearsal starts from Play-Coop''s UDP play lab.ini.'
    }
    $text=Set-WorkerIniValue $BaseText 'net_bind' 'loopback'
    $lines=[Collections.Generic.List[string]]::new()
    if($Role -ceq 'host'){$lines.Add('net_peer_port=47831')}
    else{
        $text=Set-WorkerIniValue $text 'net_listen_port' '47831'
        $text=Set-WorkerIniValue $text 'net_peer_addr' '127.0.0.1'
    }
    foreach($line in @('play_rehearsal=1','xinput=none')+@($Extra)){$lines.Add($line)}
    foreach($line in $lines){
        $kv=$line.Split('=',2)
        if($kv.Count -ne 2 -or $kv[0] -cnotmatch '^[a-z_0-9]+$' -or $kv[1] -match '[\r\n\x00]'){throw "Invalid play rehearsal key: $line"}
        if($kv[0] -cin @('mode','role','enabled','loopback','self_steamid','peer_steamid','self_name','join_trigger','auto_join_delay_s','net_transport')){
            throw "The play rehearsal never sets $($kv[0])."
        }
        if([regex]::Matches($text,'(?m)^'+[regex]::Escape($kv[0])+'=').Count){$text=Set-WorkerIniValue $text $kv[0] $kv[1]}
        else{$text=$text.TrimEnd([char]10)+"`n"+$line+"`n"}
    }
    return $text
}
# Pure: the play rehearsal's startup safety from this run's early.log and
# steam-observer.log (the lab verdict expects the fake guest identity, which a
# play copy never has). ok: saves=redirected to the copy, the play line with
# refused=0x0, saves=redirected and rehearsal=1, the renamed mutex, the real
# identity and the closed-port master server; unsafe on any contrary line.
function Get-WorkerPlaySafetyVerdict {
    param([Parameter(Mandatory)][ValidateSet('host','guest')][string]$Role,[AllowNull()][AllowEmptyString()][string]$EarlyLog='',
        [AllowNull()][AllowEmptyString()][string]$SteamLog='',[Parameter(Mandatory)][string]$ExpectedSaveDir,
        [Parameter(Mandatory)][string]$ExpectedUrl,[Parameter(Mandatory)][string]$ExpectedMutexSuffix)
    if(!$EarlyLog){$EarlyLog=''};if(!$SteamLog){$SteamLog=''}
    $missing=[Collections.Generic.List[string]]::new()
    $unsafe={param($reason)[pscustomobject]@{State='unsafe';Reason=$reason;Missing=@()}}
    foreach($m in [regex]::Matches($EarlyLog,'(?m)^early=start(?=[ ;\r\n]|$)[^\r\n]*')){
        if($m.Value -match '[ ;]lab=0(?=[ ;]|$)'){return & $unsafe 'lab_disabled'}
        if($m.Value -match '[ ;]role=([A-Za-z_]+)' -and $Matches[1] -cne $Role){return & $unsafe 'role_mismatch'}
    }
    if([regex]::IsMatch($EarlyLog,'(?m)^saves=unchanged(?=[ ;\r\n]|$)')){return & $unsafe 'saves_unchanged'}
    $saveLines=[regex]::Matches($EarlyLog,'(?m)^saves=redirected(?=[ ;\r\n]|$)[^\r\n]*')
    if(!$saveLines.Count){$missing.Add('saves')}
    $wantDir=ConvertTo-LabComparablePath $ExpectedSaveDir
    foreach($line in $saveLines){
        $dir=[regex]::Match($line.Value,'; dir=(.*)$')
        if(!$dir.Success -or (ConvertTo-LabComparablePath $dir.Groups[1].Value.TrimEnd([char]13)) -cne $wantDir){return & $unsafe 'saves_wrong_dir'}
    }
    $playLines=[regex]::Matches($EarlyLog,'(?m)^play=1;[^\r\n]*')
    if(!$playLines.Count){$missing.Add('play')}
    foreach($line in $playLines){
        if($line.Value -cnotmatch '; refused=0x0;'){return & $unsafe 'play_refused'}
        if($line.Value -cnotmatch '; saves=redirected(?=[ ;\r]|$)'){return & $unsafe 'play_saves_unchanged'}
        if($line.Value -cnotmatch '; rehearsal=1(?=[ ;\r]|$)'){return & $unsafe 'not_a_rehearsal_build'}
        if($line.Value -cnotmatch '; loopback=0;'){return & $unsafe 'loopback_emulator'}
    }
    if([regex]::IsMatch($EarlyLog,'(?m)^mutex=unchanged(?=[ ;\r\n]|$)')){return & $unsafe 'mutex_unchanged'}
    $mutexLines=[regex]::Matches($EarlyLog,'(?m)^mutex=renamed(?=[ ;\r\n]|$)[^\r\n]*')
    if(!$mutexLines.Count){$missing.Add('mutex')}
    foreach($line in $mutexLines){
        $suffix=[regex]::Match($line.Value,'; suffix=([^ ;\r]*)')
        if(!$suffix.Success -or $suffix.Groups[1].Value -cne $ExpectedMutexSuffix){return & $unsafe 'mutex_wrong_suffix'}
    }
    if([regex]::IsMatch($EarlyLog,'(?m)^identity=fake(?=[ ;\r\n]|$)')){return & $unsafe 'identity_fake'}
    foreach($line in [regex]::Matches($SteamLog,'(?m)^master_server=(refused|failed)(?=[ ;\r\n]|$)[^\r\n]*')){
        if($line.Value -cmatch '; reason=already_redirected(?=[ ;]|$)'){continue}
        return & $unsafe ('master_server_'+$line.Groups[1].Value)
    }
    $masterLines=[regex]::Matches($SteamLog,'(?m)^master_server=redirected(?=[ ;\r\n]|$)[^\r\n]*')
    if(!$masterLines.Count){$missing.Add('master_server')}
    foreach($line in $masterLines){
        $url=[regex]::Match($line.Value,'[ ;]url=(\S+)')
        if(!$url.Success -or $url.Groups[1].Value -cne $ExpectedUrl){return & $unsafe 'master_server_wrong_url'}
    }
    $state=if($missing.Count){'pending'}else{'ok'}
    return [pscustomobject]@{State=$state;Reason='';Missing=$missing.ToArray()}
}
# The Invoke-WorkerChecks cache directory of a Research worker build with
# exactly these experiments (its Get-WorkerPlan profile key).
function Get-WorkerResearchBuildDirectory([string]$RepoRoot,[string[]]$Experiments) {
    $text=(@($Experiments|Sort-Object)) -join "`n"
    $hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($text))).Substring(0,12)
    return Join-Path $RepoRoot ('build\worker-checks\research-'+$hash+'-worker-on-release')
}
# Named save seeds (docs/test-boot.md "Save-seed library"): <SeedRoot>\<name>\{host,guest}\{287700,remote}
# plus <SeedRoot>\<name>\manifest.json, captured by tools/worker/Save-WorkerSeed.ps1 from the copied saves of a
# closed worker run (never from Steam userdata) and verified file by file before every use. The root is
# -SeedRoot, else $env:MGS_COOP_SEED_ROOT, else D:\mgs-coop-seeds (outside every checkout).
function Get-WorkerSeedRoot([string]$SeedRoot='') {
    $root=if($SeedRoot){$SeedRoot}elseif($env:MGS_COOP_SEED_ROOT){$env:MGS_COOP_SEED_ROOT}else{'D:\mgs-coop-seeds'}
    return Assert-WorkerPlainPath ([IO.Path]::GetFullPath($root))
}
# A seed name: 2..32 of [a-z0-9-], starting with a letter; never a built-in seed word.
function Test-WorkerSeedName([string]$Name) {
    return $Name -cmatch '\A[a-z][a-z0-9-]{1,31}\z' -and $Name -cnotin @('real','lab','run','both','real-remote')
}
# The role copy's files exactly as Get-WorkerCopyInventory lists them, as one SHA-256:
# "<path>|<size>|<sha256>\n" per file in path order (upper-case hex).
function Get-WorkerSeedSetSha256([object[]]$Rows) {
    $text=(@($Rows|Sort-Object {[string]$_['path']}|ForEach-Object{[string]$_['path']+'|'+[string]$_['size']+'|'+([string]$_['sha256']).ToUpperInvariant()}) -join "`n")+"`n"
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($text)))
}
# A seed role copy fit for a game: 287700\local and remote only, a TPP_GAME_DATA file, at most 64 files of
# at most 16 MiB each (the copied saves are a few MB).
function Assert-WorkerSeedRows([object[]]$Rows,[string]$What) {
    if(!$Rows.Count -or $Rows.Count -gt 64){throw "$What must hold 1..64 files."}
    foreach($row in $Rows){
        if([string]$row['path'] -cnotmatch '\A(287700\\local|remote)\\[A-Za-z0-9_.-]{1,64}\z'){throw "$What holds an unexpected file: $($row['path'])"}
        if([long]$row['size'] -gt 16777216){throw "$What holds an oversized file: $($row['path'])"}
    }
    if(!@($Rows|Where-Object{[string]$_['path'] -cmatch '\A287700\\local\\TPP_GAME_DATA[0-9]?\z'}).Count){throw "$What has no 287700\local\TPP_GAME_DATA."}
}
# Reads and verifies one role of a named seed: the manifest's format, name and role entry, then every
# file's size and SHA-256 and the set hash; nothing more and nothing less on disk. Returns the folder and rows.
function Read-WorkerNamedSeed([string]$SeedRoot,[string]$Name,[ValidateSet('host','guest')][string]$Role) {
    if(!(Test-WorkerSeedName $Name)){throw "Invalid save seed name: $Name"}
    $root=Get-WorkerSeedRoot $SeedRoot
    $seed=Assert-WorkerPlainPath (Join-Path $root $Name)
    if(!(Test-Path -LiteralPath $seed -PathType Container)){throw "No save seed '$Name' in $root (tools/worker/Save-WorkerSeed.ps1 creates it)."}
    $manifestPath=Join-Path $seed 'manifest.json'
    $manifest=Read-WorkerSessionJson $manifestPath 1048576
    if([string]$manifest['format'] -cne 'mgs-coop-worker-seed' -or [string]$manifest['v'] -cne '1' -or [string]$manifest['name'] -cne $Name -or
       $manifest['roles'] -isnot [Collections.IDictionary]){throw "Save seed '$Name' has no valid manifest."}
    $entry=$manifest['roles'][$Role]
    if($entry -isnot [Collections.IDictionary]){throw "Save seed '$Name' has no $Role copy."}
    $folder=Assert-WorkerPlainPath (Join-Path $seed $Role) -MustExist
    $rows=@(Get-WorkerCopyInventory $folder)
    Assert-WorkerSeedRows $rows "Save seed '$Name' ($Role)"
    $want=@($entry['files'])
    if($want.Count -ne $rows.Count){throw "Save seed '$Name' ($Role) differs from its manifest (file count)."}
    for($i=0;$i -lt $rows.Count;$i++){
        $w=@($want|Where-Object{$_ -is [Collections.IDictionary] -and [string]$_['path'] -ceq [string]$rows[$i]['path']})
        if($w.Count -ne 1 -or [long]$w[0]['size'] -ne [long]$rows[$i]['size'] -or [string]$w[0]['sha256'] -ine [string]$rows[$i]['sha256']){
            throw "Save seed '$Name' ($Role) differs from its manifest: $($rows[$i]['path'])"
        }
    }
    $set=Get-WorkerSeedSetSha256 $rows
    if([string]$entry['set_sha256'] -cne $set){throw "Save seed '$Name' ($Role) set hash differs from its manifest."}
    return [pscustomobject]@{Name=$Name;Role=$Role;Folder=$folder;Files=$rows;SetSha256=$set;ManifestSha256=(Get-WorkerHash $manifestPath)}
}
# Copies a verified named seed role (Read-WorkerNamedSeed) into a fresh owned run save folder, file by file,
# each re-hashed; the 287700\local and remote folders always exist afterwards. Returns the copy's inventory.
function Copy-WorkerNamedSeed($Seed,[string]$Destination,[string]$RunDirectory) {
    $target=Assert-WorkerPlainPath $Destination
    if(!(Test-WorkerDescendant $target $RunDirectory) -or (Test-Path -LiteralPath $target)){throw 'A named seed copy must be a fresh run descendant.'}
    foreach($folder in @('287700\local','remote')){$null=New-Item -ItemType Directory -Path (Join-Path $target $folder) -Force}
    foreach($row in @($Seed.Files)){
        $to=[IO.Path]::GetFullPath((Join-Path $target ([string]$row['path'])))
        if(!(Test-WorkerDescendant $to $target)){throw 'A named seed copy escaped its folder.'}
        [IO.File]::Copy((Join-Path $Seed.Folder ([string]$row['path'])),$to,$false)
        Assert-WorkerHash $to ([string]$row['sha256'])
    }
    $copies=@(Get-WorkerCopyInventory $target)
    if((Get-WorkerSeedSetSha256 $copies) -cne $Seed.SetSha256){throw 'The named seed copy differs from the verified seed.'}
    return $copies
}
Export-ModuleMember -Function *
