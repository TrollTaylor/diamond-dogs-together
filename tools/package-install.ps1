param(
    [ValidateSet('Install','Restore')][string]$Action='Install',
    [string]$GameDirectory
)
$ErrorActionPreference='Stop'
$packageRoot=$PSScriptRoot
if(!$GameDirectory) { $GameDirectory=Read-Host 'MGSV folder containing mgsvtpp.exe' }
$gameItem=Get-Item -LiteralPath $GameDirectory
if(!$gameItem.PSIsContainer -or ($gameItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Use the direct MGSV folder.' }
$gameRoot=[IO.Path]::GetFullPath($gameItem.FullName).TrimEnd('\','/')
if($gameRoot -match '^[A-Za-z]:$') { throw 'Select the MGSV installation folder, not a drive root.' }
$modRoot=Join-Path $gameRoot 'mgs-coop'
$manifestPath=Join-Path $modRoot 'probe-install.json'
$allowed=@('mgs-coop/mgs-coop-player-runtime.dat','mgs-coop/DYNAMITE-LICENSE','mgs-coop/MinHook-LICENSE.txt',
    'mgs-coop/tpp-mod-LICENSE','mgs-coop/THIRD_PARTY_NOTICES.md','dinput8.dll','mgs-coop/probe-install.json')
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function RequireStopped {
    if(Get-Process mgsvtpp,mgsvmgo -ErrorAction SilentlyContinue) { throw 'Close MGSV normally before changing the mod.' }
}
function SafePath([string]$Root,[string]$Relative) {
    if([IO.Path]::IsPathRooted($Relative)) { throw 'Unexpected absolute package path.' }
    $full=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if(!$full.StartsWith($Root.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Path leaves the expected directory.' }
    $cursor=$full
    while($cursor.Length -gt $Root.Length) {
        if(Test-Path -LiteralPath $cursor) {
            if((Get-Item -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Filesystem links are not supported: $cursor" }
        }
        $cursor=Split-Path -Parent $cursor
    }
    $full
}
function RequireHash([string]$Path,[string]$Expected) {
    if($Expected -cnotmatch '^[A-Fa-f0-9]{64}$' -or !(Test-Path -LiteralPath $Path -PathType Leaf) -or (Hash $Path) -ne $Expected) {
        throw "File differs from its recorded hash: $Path"
    }
}
function ReplaceFile([string]$Source,[string]$Target,[string]$Expected) {
    RequireStopped
    RequireHash $Source $Expected
    $temp=$Target+'.mgs-coop-'+[Guid]::NewGuid().ToString('N')+'.tmp'
    $copied=$false
    try {
        [IO.File]::Copy($Source,$temp,$false)
        $copied=$true
        RequireHash $temp $Expected
        # PowerShell can coerce $null to an empty string for this .NET overload.
        # Pass a real null string: backups are already owned by rollback.json.
        if(Test-Path -LiteralPath $Target) { [IO.File]::Replace($temp,$Target,[NullString]::Value) }
        else { [IO.File]::Move($temp,$Target) }
    } finally {
        if($copied -and (Test-Path -LiteralPath $temp) -and (Hash $temp) -eq $Expected) { Remove-Item -LiteralPath $temp }
    }
}
function RestoreFiles($Rows,[string]$BackupRoot) {
    # Validate every current target and backup before changing any target.
    foreach($row in $Rows) {
        if($row.target -cnotin $allowed) { throw 'Unknown target in rollback record.' }
        RequireHash (SafePath $gameRoot $row.target) $row.after_sha256
        if($row.before_exists) { RequireHash (SafePath $BackupRoot $row.before_file) $row.before_sha256 }
    }
    foreach($row in $Rows) {
        $target=SafePath $gameRoot $row.target
        RequireStopped
        RequireHash $target $row.after_sha256
        if($row.before_exists) { ReplaceFile (SafePath $BackupRoot $row.before_file) $target $row.before_sha256 }
        else { Remove-Item -LiteralPath $target }
    }
}
RequireStopped
$null=SafePath $gameRoot 'mgs-coop/probe-install.json'
$null=SafePath $gameRoot 'mgs-coop/script-observe.enabled'
$mutex=[Threading.Mutex]::new($false,'Local\MgsCoopPackageInstall')
$locked=$false
try {
    $locked=$mutex.WaitOne(0)
    if(!$locked) { throw 'Another MGS Co-op installer is already running.' }
    if($Action -eq 'Restore') {
        $installed=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if($installed.product -ne 'mgs-coop-original-loader-probe' -or $installed.game_directory -ne $gameRoot -or
            $installed.rollback_id -cnotmatch '^[0-9a-f]{32}$') { throw 'No matching package rollback record.' }
        RequireHash (SafePath $gameRoot 'dinput8.dll') $installed.dll_sha256
        $backupRoot=SafePath $gameRoot ('mgs-coop/install-backups/'+$installed.rollback_id)
        $record=Get-Content -LiteralPath (Join-Path $backupRoot 'rollback.json') -Raw | ConvertFrom-Json
        if($record.product -ne 'mgs-coop-package-rollback' -or $record.game_directory -ne $gameRoot -or
            @($record.files).Count -ne $allowed.Count -or @($record.files.target | Select-Object -Unique).Count -ne $allowed.Count) { throw 'Invalid rollback file inventory.' }
        RestoreFiles $record.files $backupRoot
        Write-Output 'Previous loader and managed mod files restored. Role configuration and logs are retained.'
        return
    }
    $package=Get-Content -LiteralPath (Join-Path $packageRoot 'package.json') -Raw | ConvertFrom-Json
    if($package.product -ne 'mgs-coop-local-package' -or $package.version -ne '0.10.33' -or
        @($package.files).Count -ne ($allowed.Count-1) -or @($package.files.target | Select-Object -Unique).Count -ne ($allowed.Count-1)) { throw 'Unexpected package manifest.' }
    RequireHash (SafePath $gameRoot 'mgsvtpp.exe') $package.executable_sha256
    RequireHash (SafePath $gameRoot 'steam_api64.dll') $package.steam_dll_sha256
    foreach($file in $package.files) {
        if($file.target -cnotin $allowed -or $file.target -eq 'mgs-coop/probe-install.json') { throw 'Unknown package target.' }
        RequireHash (SafePath $packageRoot $file.source) $file.sha256
        $null=SafePath $gameRoot $file.target
    }
    $dllPath=SafePath $gameRoot 'dinput8.dll'
    $previous=$null; $ownershipRecordHash=$null
    if((Test-Path -LiteralPath $dllPath) -or (Test-Path -LiteralPath $manifestPath)) {
        if(!(Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'An unrelated loader exists; nothing was replaced.' }
        $previous=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if($previous.product -ne 'mgs-coop-original-loader-probe' -or $previous.game_directory -ne $gameRoot) { throw 'Loader ownership does not match this mod and folder.' }
        RequireHash $dllPath $previous.dll_sha256
        $ownershipRecordHash=Hash $manifestPath
    } else {
        foreach($file in $package.files) {
            if(Test-Path -LiteralPath (SafePath $gameRoot $file.target)) { throw 'Managed mod files already exist without a loader ownership record.' }
        }
    }
    $id=[Guid]::NewGuid().ToString('N')
    $backupRoot=SafePath $gameRoot ('mgs-coop/install-backups/'+$id)
    $null=New-Item -ItemType Directory -Path $backupRoot
    $newManifest=[ordered]@{
        product='mgs-coop-original-loader-probe'; version=$package.version; game_directory=$gameRoot
        dll_sha256=($package.files | Where-Object target -eq 'dinput8.dll').sha256
        executable_sha256=$package.executable_sha256; installed_utc=[DateTime]::UtcNow.ToString('o')
        rollback_id=$id; scope='Two-player development build; playable co-op unverified. Role is selected separately.'
    }
    $preparedManifest=Join-Path $backupRoot 'new-manifest.json'
    [IO.File]::WriteAllText($preparedManifest,($newManifest | ConvertTo-Json)+"`n",[Text.UTF8Encoding]::new($false))
    $writes=@($package.files | ForEach-Object {
        [pscustomobject]@{target=$_.target;source=(SafePath $packageRoot $_.source);sha256=$_.sha256}
    })+@([pscustomobject]@{target='mgs-coop/probe-install.json';source=$preparedManifest;sha256=(Hash $preparedManifest)})
    $rows=@(); $index=0
    foreach($file in $writes) {
        $target=SafePath $gameRoot $file.target
        $exists=Test-Path -LiteralPath $target -PathType Leaf
        $row=[pscustomobject]@{target=$file.target;before_exists=$exists;before_file="before-$index.bin";before_sha256=$null;after_sha256=$file.sha256}
        if($exists) {
            $row.before_sha256=Hash $target
            if(($file.target -eq 'dinput8.dll' -and (!$previous -or $row.before_sha256 -ne $previous.dll_sha256)) -or
                ($file.target -eq 'mgs-coop/probe-install.json' -and (!$previous -or $row.before_sha256 -ne $ownershipRecordHash))) {
                throw 'Loader ownership changed while preparing the backup.'
            }
            [IO.File]::Copy($target,(Join-Path $backupRoot $row.before_file),$false)
            RequireHash (Join-Path $backupRoot $row.before_file) $row.before_sha256
        }
        $rows+=@($row); $index++
    }
    $record=[ordered]@{product='mgs-coop-package-rollback';game_directory=$gameRoot;files=$rows}
    [IO.File]::WriteAllText((Join-Path $backupRoot 'rollback.json'),($record | ConvertTo-Json -Depth 5)+"`n",[Text.UTF8Encoding]::new($false))
    $written=@()
    try {
        for($i=0;$i -lt $writes.Count;$i++) {
            $target=SafePath $gameRoot $writes[$i].target
            if($rows[$i].before_exists) { RequireHash $target $rows[$i].before_sha256 }
            elseif(Test-Path -LiteralPath $target) { throw 'A target appeared during installation; refusing to overwrite it.' }
            ReplaceFile $writes[$i].source $target $writes[$i].sha256
            $written+=@($rows[$i])
        }
    } catch {
        $failure=$_
        if($written.Count) {
            try { RestoreFiles $written $backupRoot }
            catch { throw "Installation failed and rollback needs attention. Backups: $backupRoot. Rollback error: $_. Original error: $failure" }
        }
        throw $failure
    }
    # A first install starts in loading diagnostic mode. Upgrades retain the
    # existing role. Configuration is not part of the binary rollback set.
    try {
        $marker=SafePath $gameRoot 'mgs-coop/script-observe.enabled'
        if(!(Test-Path -LiteralPath $marker)) {
            $stagedMarker=Join-Path $backupRoot 'initial-mode.txt'
            [IO.File]::WriteAllText($stagedMarker,"client-check-v1`n",[Text.Encoding]::ASCII)
            [IO.File]::Copy($stagedMarker,$marker,$false)
        }
    } catch { Write-Warning 'Installation completed, but initial mode could not be set. Run configure-role.bat before launching MGSV.' }
    try {
        [IO.File]::WriteAllText((Join-Path $packageRoot 'installed-location.json'),(@{game_directory=$gameRoot} | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    } catch { Write-Warning 'Installation completed. Pass -GameDirectory explicitly when configuring the role.' }
    Write-Output "MGS Co-op $($package.version) installed in $gameRoot. Run configure-role.bat to select Host, Join or Diagnostic. Backups: $backupRoot"
} finally {
    if($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
