param(
    [ValidateSet('Install','Restore')][string]$Action='Install',
    [string]$GameDirectory,
    # Packages no longer contain the runtime archive (it holds game-derived bytes).
    # Pass the locally generated or copied archive; an installed archive with the
    # expected hash is reused when this is omitted.
    [string]$RuntimeArchive,
    # Accept an ownership record that was copied together with the folder from this
    # directory (for example a guest copy of the host installation). The installed
    # loader must still match that record exactly.
    [string]$AcceptRecordCopiedFrom,
    # Folder containing package.json and payload/ (default: this script's folder,
    # as in an extracted package). The lab scripts stage packages elsewhere.
    [string]$PackageDirectory,
    # Never ask for the runtime archive path (scripts and tests). Without it, an
    # interactive console session (install.bat) is asked when the archive is needed.
    [switch]$NoPrompt,
    # Do not select the loading diagnostic mode on a first install (the two-PC
    # play package's Install-Coop.ps1 leaves the game in pass-through).
    [switch]$NoInitialMode
)
$ErrorActionPreference='Stop'
$packageRoot=if($PackageDirectory) { [IO.Path]::GetFullPath((Get-Item -LiteralPath $PackageDirectory).FullName).TrimEnd('\','/') } else { $PSScriptRoot }
if(!$GameDirectory) { $GameDirectory=Read-Host 'MGSV folder containing mgsvtpp.exe' }
$gameItem=Get-Item -LiteralPath $GameDirectory
if(!$gameItem.PSIsContainer -or ($gameItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Use the direct MGSV folder.' }
$gameRoot=[IO.Path]::GetFullPath($gameItem.FullName).TrimEnd('\','/')
if($gameRoot -match '^[A-Za-z]:$') { throw 'Select the MGSV installation folder, not a drive root.' }
$modRoot=Join-Path $gameRoot 'mgs-coop'
$manifestPath=Join-Path $modRoot 'probe-install.json'
$archiveTarget='mgs-coop/mgs-coop-player-runtime.dat'
$allowed=@($archiveTarget,'mgs-coop/DYNAMITE-LICENSE','mgs-coop/MinHook-LICENSE.txt',
    'mgs-coop/ImGui-LICENSE.txt','mgs-coop/tpp-mod-LICENSE','mgs-coop/THIRD_PARTY_NOTICES.md','dinput8.dll','mgs-coop/probe-install.json')
# Retain owned rollback/install compatibility with packages from before the overlay.
$legacyAllowed=@($allowed | Where-Object { $_ -cne 'mgs-coop/ImGui-LICENSE.txt' })
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function RunningFromGame {
    # Only mgsvtpp/mgsvmgo processes whose executable is in this folder block the
    # installer, so a second lab instance can be updated while the other runs.
    foreach($process in @(Get-Process -Name mgsvtpp,mgsvmgo -ErrorAction SilentlyContinue)) {
        $path=$null
        try { $path=$process.Path } catch { $path=$null }
        if(!$path) { try { $path=(Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)" -ErrorAction Stop).ExecutablePath } catch { $path=$null } }
        if(!$path) {
            # A process that exited after the enumeration (another copy's game that just quit,
            # a test stand-in) has no folder to protect; any other unknown location still blocks.
            $exited=$false
            try { $exited=$process.HasExited } catch { $exited=$false }
            if($exited) { continue }
            return $true  # Unknown location: assume this folder.
        }
        $folder=(Split-Path -Parent ([IO.Path]::GetFullPath($path))).TrimEnd('\','/')
        if($folder.Equals($gameRoot,[StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}
function RequireStopped {
    if(Test-Path -LiteralPath (Join-Path $modRoot 'local-test-session.json')) {
        throw 'A local two-window session owns this folder. Quit both games and let its launcher finish cleanup before changing the mod.'
    }
    if(RunningFromGame) { throw 'Close MGSV running from this folder normally before changing the mod.' }
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
        $installed=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if($installed.product -ne 'mgs-coop-original-loader-probe' -or $installed.game_directory -ne $gameRoot -or
            $installed.rollback_id -cnotmatch '^[0-9a-f]{32}$') { throw 'No matching package rollback record.' }
        RequireHash (SafePath $gameRoot 'dinput8.dll') $installed.dll_sha256
        $backupRoot=SafePath $gameRoot ('mgs-coop/install-backups/'+$installed.rollback_id)
        $record=Get-Content -LiteralPath (Join-Path $backupRoot 'rollback.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        $recordAllowed=if(@($record.files.target) -ccontains 'mgs-coop/ImGui-LICENSE.txt') { $allowed } else { $legacyAllowed }
        if($record.product -ne 'mgs-coop-package-rollback' -or $record.game_directory -ne $gameRoot -or
            @($record.files).Count -ne $recordAllowed.Count -or @($record.files.target | Select-Object -Unique).Count -ne $recordAllowed.Count -or
            @($record.files.target | Where-Object { $_ -cnotin $recordAllowed }).Count) { throw 'Invalid rollback file inventory.' }
        RestoreFiles $record.files $backupRoot
        Write-Output 'Previous loader and managed mod files restored. Role configuration and logs are retained.'
        return
    }
    # A Diamond Dogs Together drop-in package (0.14.0+, tools/package.ps1 -Friend)
    # keeps package.json in mgs-coop\ and also lists the launcher, its README and
    # the source archive. This owned installer (developer and lab use) installs
    # only the loader and its licences from it; players copy the zip instead.
    $packageJson=Join-Path $packageRoot 'package.json'
    if(!(Test-Path -LiteralPath $packageJson -PathType Leaf)) { $packageJson=Join-Path $packageRoot 'mgs-coop/package.json' }
    $package=Get-Content -LiteralPath $packageJson -Raw -Encoding UTF8 | ConvertFrom-Json
    if(@($package.PSObject.Properties.Name) -contains 'layout' -and [string]$package.layout -ceq 'drop-in') {
        $package.files=@($package.files | Where-Object { $_.target -cnotin @('DiamondDogsTogether.exe','mgs-coop/README.txt','mgs-coop/source.zip') })
    }
    # Older packages bundled the archive; current packages record only its hash.
    $bundlesArchive=@($package.files | ForEach-Object { $_.target }) -contains $archiveTarget
    $packageAllowed=if(@($package.files.target) -ccontains 'mgs-coop/ImGui-LICENSE.txt') { $allowed } else { $legacyAllowed }
    $packageFileCount=if($bundlesArchive) { $packageAllowed.Count-1 } else { $packageAllowed.Count-2 }
    if($package.product -ne 'mgs-coop-local-package' -or $package.version -notin @('0.12.0','0.12.1','0.12.2','0.12.3','0.12.4','0.12.5','0.12.6','0.13.0','0.13.1','0.13.2','0.13.3','0.13.4','0.13.5','0.13.6','0.14.0','0.14.1') -or
        @($package.files).Count -ne $packageFileCount -or @($package.files.target | Select-Object -Unique).Count -ne $packageFileCount -or
        @($package.files.target | Where-Object { $_ -cnotin $packageAllowed -or $_ -ceq 'mgs-coop/probe-install.json' }).Count) { throw 'Unexpected package manifest.' }
    $archiveHash=$null
    if($bundlesArchive) {
        if($RuntimeArchive) { throw 'This package bundles the runtime archive; do not pass -RuntimeArchive.' }
    } else {
        if(!($package.PSObject.Properties.Name -contains 'runtime_archive') -or $package.runtime_archive.target -ne $archiveTarget -or
            [string]$package.runtime_archive.sha256 -cnotmatch '^[A-Fa-f0-9]{64}$') { throw 'Unexpected package manifest.' }
        $archiveHash=[string]$package.runtime_archive.sha256
    }
    RequireHash (SafePath $gameRoot 'mgsvtpp.exe') $package.executable_sha256
    RequireHash (SafePath $gameRoot 'steam_api64.dll') $package.steam_dll_sha256
    foreach($file in $package.files) {
        if($file.target -cnotin $allowed -or $file.target -eq 'mgs-coop/probe-install.json') { throw 'Unknown package target.' }
        RequireHash (SafePath $packageRoot $file.source) $file.sha256
        $null=SafePath $gameRoot $file.target
    }
    # Runtime archive source: an explicit local file, the already installed copy,
    # or (interactive console, e.g. a double-clicked install.bat) a path typed in.
    $archiveSource=$null
    if(!$bundlesArchive) {
        $archivePath=SafePath $gameRoot $archiveTarget
        $interactive=!$NoPrompt -and [Environment]::UserInteractive -and ![Console]::IsInputRedirected -and
            !@([Environment]::GetCommandLineArgs() | Where-Object { $_ -match '^-NonI' }).Count
        $archiveInput=$RuntimeArchive
        if(!$archiveInput -and !((Test-Path -LiteralPath $archivePath -PathType Leaf) -and (Hash $archivePath) -eq $archiveHash) -and $interactive) {
            Write-Host "This package contains no game-derived files. The two-player runtime archive (mgs-coop-player-runtime.dat, SHA-256 $archiveHash) is built on your own PC (DiamondDogsTogether.exe, or ddt_datbuild.exe build) or copied; see RUNTIME-ARCHIVE.txt."
            $archiveInput=([string](Read-Host 'Full path of mgs-coop-player-runtime.dat')).Trim().Trim('"')
            if(!$archiveInput) { throw 'No runtime archive path was entered; nothing was installed.' }
        }
        if($archiveInput) {
            $archiveItem=Get-Item -LiteralPath $archiveInput
            if($archiveItem.PSIsContainer) { throw 'RuntimeArchive must be a file.' }
            $archiveSource=[IO.Path]::GetFullPath($archiveItem.FullName)
            RequireHash $archiveSource $archiveHash
            if($archiveSource.Equals($archivePath,[StringComparison]::OrdinalIgnoreCase)) { $archiveSource='existing' }
        } elseif((Test-Path -LiteralPath $archivePath -PathType Leaf) -and (Hash $archivePath) -eq $archiveHash) {
            $archiveSource='existing'
        } else {
            throw "This package does not contain game-derived files. Pass -RuntimeArchive with your locally generated two-player runtime archive (SHA-256 $archiveHash); install.bat asks for it when run from a console."
        }
    }
    $dllPath=SafePath $gameRoot 'dinput8.dll'
    $previous=$null; $ownershipRecordHash=$null
    if((Test-Path -LiteralPath $dllPath) -or (Test-Path -LiteralPath $manifestPath)) {
        if(!(Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'An unrelated loader exists; nothing was replaced.' }
        $previous=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if($previous.product -ne 'mgs-coop-original-loader-probe') { throw 'Loader ownership does not match this mod and folder.' }
        if($previous.game_directory -ne $gameRoot) {
            $copiedFrom=if($AcceptRecordCopiedFrom) { [IO.Path]::GetFullPath($AcceptRecordCopiedFrom).TrimEnd('\','/') } else { $null }
            if(!$copiedFrom -or $copiedFrom -eq $gameRoot -or $previous.game_directory -ne $copiedFrom) {
                throw 'Loader ownership does not match this mod and folder. If this folder is a copy of another installation, pass -AcceptRecordCopiedFrom with the folder named in its probe-install.json.'
            }
            Write-Output "Accepting an ownership record copied from $copiedFrom; the installed loader must still match it."
        }
        RequireHash $dllPath $previous.dll_sha256
        $ownershipRecordHash=Hash $manifestPath
    } else {
        foreach($file in $package.files) {
            if(Test-Path -LiteralPath (SafePath $gameRoot $file.target)) { throw 'Managed mod files already exist without a loader ownership record.' }
        }
        if($archiveSource -and $archiveSource -ne 'existing' -and (Test-Path -LiteralPath (SafePath $gameRoot $archiveTarget))) {
            throw 'Managed mod files already exist without a loader ownership record.'
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
    })
    # 'existing' is resolved to the verified backup copy once it has been taken.
    if($archiveSource) { $writes+=@([pscustomobject]@{target=$archiveTarget;source=$archiveSource;sha256=$archiveHash}) }
    $writes+=@([pscustomobject]@{target='mgs-coop/probe-install.json';source=$preparedManifest;sha256=(Hash $preparedManifest)})
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
        if($file.source -eq 'existing') {
            if(!$exists -or $row.before_sha256 -ne $file.sha256) { throw 'Installed runtime archive changed while preparing the backup.' }
            $file.source=Join-Path $backupRoot $row.before_file
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
    # A first install starts in loading diagnostic mode (not with -NoInitialMode).
    # Upgrades retain the existing role. Configuration is not part of the binary
    # rollback set.
    try {
        $marker=SafePath $gameRoot 'mgs-coop/script-observe.enabled'
        if(!$NoInitialMode -and !(Test-Path -LiteralPath $marker)) {
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
