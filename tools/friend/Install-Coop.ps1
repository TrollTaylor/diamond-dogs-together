<#
MGS Co-op friend-test installer (two PCs, each player with their own Steam account).

Double-click Install-Coop.bat and answer the questions, or run it with
parameters (every question has one; -NonInteractive never asks):

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Install-Coop.ps1 `
      -GameDirectory "D:\SteamLibrary\steamapps\common\MGS_TPP" -Role Join `
      -HostSteamId 7656119XXXXXXXXXX -Character quiet `
      -RuntimeArchive "C:\path\mgs-coop-player-runtime.dat" -NonInteractive -Yes

What it changes (all reversible with Uninstall-Coop.bat):
  - <game>\dinput8.dll and the managed mgs-coop files, through the package's
    owned installer (scripts\install.ps1: hash-checked backups and rollback);
  - <game>\mgs-coop\script-observe.enabled (+ client-host.txt for Join), through
    the shared role command (scripts\set-session-mode.ps1);
  - <game>\mgs-coop\lab.ini (diagnostic mode: saves redirected to a copy, real
    Steam identity and networking, status logging) and coop.ini (character only,
    never a master server URL);
  - a backup of the real save files with a SHA-256 manifest
    (<game>\mgs-coop\real-save-backups\<time>) and the save copy the game uses
    while the mod is installed (<game>\mgs-coop\coop-saves).
It never writes below Steam\userdata, never changes mgsvtpp.exe or
steam_api64.dll, and never starts the game or Steam.
#>
[CmdletBinding()]
param(
    # MGSV folder (contains mgsvtpp.exe). Default: found through Steam, then asked.
    [string]$GameDirectory,
    [ValidateSet('Host', 'Join')][string]$Role,
    # Join only: the host player's 17-digit SteamID64.
    [string]$HostSteamId,
    # default, quiet, dd_male, dd_female, snake, avatar, ocelot (default: Host=default, Join=quiet).
    [string]$Character,
    # mgs-coop-player-runtime.dat with the expected SHA-256 (never bundled).
    [string]$RuntimeArchive,
    # Steam folder (default: registry). Tests use a fixture folder.
    [string]$SteamPath,
    # Steam account id whose saves are backed up (default: Steam's logged-in user,
    # else the only account with MGSV saves).
    [UInt32]$SteamAccountId = 0,
    # Replace an existing co-op save copy with a fresh copy of the real save
    # (the previous copy is moved aside, never deleted). A first install (no
    # active friend install, also after Uninstall-Coop) always does this.
    [switch]$ReseedSaves,
    # Reinstall although the real save files changed since the backup made
    # before the friend test (interactive runs ask instead).
    [switch]$AcceptChangedSaves,
    [switch]$NonInteractive,
    # Do not ask for the final confirmation.
    [switch]$Yes,
    # Folder with package.json and payload\ (default: the package folder).
    [string]$PackageDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = $PSScriptRoot
# A failure ends with one plain line (and exit code 1) instead of a raw
# Windows PowerShell error record.
trap {
    Write-Host ''
    Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}

function Resolve-Piece([string]$PackagedName, [string]$RepoRelative) {
    # Packaged layout: scripts\<name>. Repository layout: tools\friend\..\<path>.
    $packaged = Join-Path $scriptRoot $PackagedName
    if (Test-Path -LiteralPath $packaged -PathType Leaf) { return $packaged }
    $repo = Join-Path $scriptRoot $RepoRelative
    if (Test-Path -LiteralPath $repo -PathType Leaf) { return (Resolve-Path -LiteralPath $repo).Path }
    throw "This package is incomplete: $PackagedName is missing. Extract the whole ZIP again."
}
Import-Module (Resolve-Piece 'LabCommon.psm1' '..\lab\LabCommon.psm1') -Force -DisableNameChecking
Import-Module (Resolve-Piece 'FriendCommon.psm1' 'FriendCommon.psm1') -Force -DisableNameChecking
$installer = Resolve-Piece 'install.ps1' '..\package-install.ps1'
$roleCommand = Resolve-Piece 'set-session-mode.ps1' '..\set-session-mode.ps1'

$interactive = Test-FriendInteractive -NonInteractive:$NonInteractive
function Ask([string]$Prompt) {
    if (!$interactive) { throw "Missing input: $Prompt (run without -NonInteractive, or pass the matching parameter)." }
    return ([string](Read-Host $Prompt)).Trim()
}
function Section([string]$Text) { Write-Host ''; Write-Host $Text -ForegroundColor Cyan }

if (!$PackageDirectory) { $PackageDirectory = Split-Path -Parent $scriptRoot }
$packageRoot = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd('\', '/')
$packageJson = Join-Path $packageRoot 'package.json'
$package = Read-FriendJson $packageJson
if (!$package -or (Get-FriendProperty $package 'product') -ne 'mgs-coop-local-package') { throw "package.json not found or invalid in $packageRoot. Extract the whole ZIP and run Install-Coop.bat from it." }
$version = [string]$package.version
$archiveSha256 = ([string]$package.runtime_archive.sha256).ToUpperInvariant()

Write-Host "MGS Co-op $version - friend test installer" -ForegroundColor Cyan
Write-Host 'Development build. Two-player co-op is NOT verified yet; this install is for testing it.'

# ---------------------------------------------------------------- game folder
Section 'Step 1: game folder'
if (!$GameDirectory) {
    $registry = Get-RegistryMgsvCandidates
    $roots = @($registry.SteamRoots)
    if ($SteamPath) { $roots = @($SteamPath) + $roots }
    $found = @(Find-MgsvGameFolders -SteamRoots $roots -Candidates @($registry.Candidates))
    if ($found.Count -eq 1) {
        Write-Host "Found MGSV in: $($found[0])"
        if (!$interactive) { throw 'Pass -GameDirectory in non-interactive mode.' }
        $answer = Ask 'Use this folder? [Y/n]'
        if ($answer -match '^(n|no)$') { $GameDirectory = Ask 'Full path of the MGSV folder (contains mgsvtpp.exe)' } else { $GameDirectory = $found[0] }
    } elseif ($found.Count -gt 1) {
        for ($i = 0; $i -lt $found.Count; $i++) { Write-Host ("  {0}: {1}" -f ($i + 1), $found[$i]) }
        $answer = Ask 'Several MGSV folders were found. Enter the number to use'
        $n = 0
        if (![int]::TryParse($answer, [ref]$n) -or $n -lt 1 -or $n -gt $found.Count) { throw 'No valid folder number was entered. Nothing was changed.' }
        $GameDirectory = $found[$n - 1]
    } else {
        Write-Host 'MGSV was not found automatically.'
        Write-Host 'In Steam: right-click METAL GEAR SOLID V: THE PHANTOM PAIN > Manage > Browse local files, and copy that folder path.'
        $GameDirectory = Ask 'Full path of the MGSV folder (contains mgsvtpp.exe)'
    }
}
$GameDirectory = $GameDirectory.Trim().Trim('"')
if (!(Test-Path -LiteralPath $GameDirectory -PathType Container)) { throw "Folder not found: $GameDirectory" }
$gameItem = Get-Item -LiteralPath $GameDirectory -Force
if ($gameItem.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Use the real MGSV folder, not a link to it.' }
$gameRoot = [IO.Path]::GetFullPath($gameItem.FullName).TrimEnd('\', '/')
if ($gameRoot -match '^[A-Za-z]:$') { throw 'Select the MGSV folder, not a drive.' }
$paths = Get-InstancePaths -GameDirectory $gameRoot
if (!(Test-Path -LiteralPath $paths.Exe -PathType Leaf)) { throw "mgsvtpp.exe is not in $gameRoot. Select the folder that contains mgsvtpp.exe." }
Write-Host "Game folder: $gameRoot"
Write-Host 'Checking the game version (this takes a few seconds)...'
if ((Get-Sha256 $paths.Exe) -ne ([string]$package.executable_sha256).ToUpperInvariant()) {
    throw 'mgsvtpp.exe is not the expected unmodified Steam version 1.0.15.4. This mod only supports that exact executable. Nothing was changed.'
}
if (!(Test-Path -LiteralPath $paths.SteamApi -PathType Leaf) -or (Get-Sha256 $paths.SteamApi) -ne ([string]$package.steam_dll_sha256).ToUpperInvariant()) {
    throw 'steam_api64.dll is not the original file shipped with the game. Verify the game files in Steam (Properties > Installed Files) and try again. Nothing was changed.'
}
Write-Host 'Game version OK (Steam 1.0.15.4, original files).'
if (@(Get-GameProcessInDirectory -Directory $gameRoot).Count) { throw 'MGSV is running from this folder. Quit the game normally, then run the installer again. Nothing was changed.' }
foreach ($p in @($paths.Mod)) { if (Test-Path -LiteralPath $p) { Assert-NoReparsePoint -Path $p -StopAt $gameRoot } }
# The owned installer refuses these cases too, but only after the save backup;
# say it early and plainly.
if ((Test-Path -LiteralPath $paths.Loader) -or (Test-Path -LiteralPath $paths.Record)) {
    $existing = Read-FriendJson $paths.Record
    if (!$existing -or (Get-FriendProperty $existing 'product') -ne 'mgs-coop-original-loader-probe' -or !(Test-Path -LiteralPath $paths.Loader -PathType Leaf)) {
        throw 'Another mod''s dinput8.dll (or an incomplete install) is in the game folder. This test needs a game without other mods: remove the other mod first. Nothing was changed.'
    }
    if ([string](Get-FriendProperty $existing 'game_directory' '') -ne $gameRoot) {
        throw 'The MGS Co-op files in this folder were copied from another game folder. Remove dinput8.dll and the mgs-coop folder (or ask for help), then try again. Nothing was changed.'
    }
    if ((Get-Sha256 $paths.Loader) -ne ([string](Get-FriendProperty $existing 'dll_sha256' '')).ToUpperInvariant()) {
        throw 'dinput8.dll was changed after MGS Co-op installed it. Remove it (or ask for help), then try again. Nothing was changed.'
    }
}
$saveDir = Join-Path $paths.Mod 'coop-saves'
$saveProblem = Get-FriendSaveDirProblem $saveDir
if ($saveProblem) { throw "$saveProblem Move the game to a shorter folder path and try again." }

# ---------------------------------------------------------------- Steam account
$steamRoot = $null
if ($SteamPath) { $steamRoot = [IO.Path]::GetFullPath($SteamPath).TrimEnd('\', '/') }
else {
    try { $steamRoot = Get-SteamInstallPath }
    catch { throw 'Steam was not found for this Windows user. Install and start Steam, log in, then run the installer again. Nothing was changed.' }
}
$active = [UInt32]0
if ($SteamAccountId -ne 0) { $active = $SteamAccountId } elseif (!$SteamPath) { $active = Get-SteamActiveUser }
try {
    $account = Resolve-SteamAccount -ActiveUser $active -Candidates (Get-SteamSaveCandidates -SteamPath $steamRoot)
} catch {
    throw ('Could not tell which Steam account plays MGSV on this PC: ' + $_.Exception.Message + ' Start Steam, log in with your own account, then run the installer again.')
}
$ownSteamId = ConvertTo-FriendSteamId64 -AccountId $account
$userData = Join-Path (Join-Path $steamRoot 'userdata') ([string]$account)
if ($active -eq 0) { Write-Host 'Note: Steam does not report a logged-in user (is Steam running?). Using the only Steam account with MGSV saves on this PC.' -ForegroundColor Yellow }
# Every path the installer creates must fit Windows' classic 260-character
# limit (Windows PowerShell 5.1 cannot exceed it on a default Windows setup).
$saveFiles = @()
foreach ($app in @('287700', '311340')) {
    $appDir = Join-Path $userData $app
    if (Test-Path -LiteralPath $appDir -PathType Container) {
        foreach ($f in @(Get-ChildItem -LiteralPath $appDir -File -Recurse -Force)) { $saveFiles += @(Get-RelativeChildPath -Root $userData -Path $f.FullName) }
    }
}
$lengthProblem = Get-FriendPathLengthProblem -GameRoot $gameRoot -SaveFiles $saveFiles
if ($lengthProblem) { throw "$lengthProblem Move the game to a shorter folder path (Steam > Settings > Storage) and try again. Nothing was changed." }

# ---------------------------------------------------------------- earlier friend install
$statePath = Join-Path $paths.Mod 'friend-install.json'
$state = Read-FriendJson $statePath
if ($state -and (Get-FriendProperty $state 'product') -ne 'mgs-coop-friend-install') { $state = $null }
if ($state) {
    # A reinstall (role or character change) takes a new backup, but the backup
    # from BEFORE the friend test stays the reference: the real saves must not
    # change while the mod is installed.
    Write-Host 'An active friend-test install was found; checking your real save files against the backup made before the friend test...'
    $baseline = Get-FriendSaveBaselines @(Get-FriendProperty $state 'backups' @())
    $changes = @()
    foreach ($b in @($baseline.Baselines)) {
        $cmp = (Test-RealSavesAgainstBackup -BackupDirectory $b.Backup).Comparison
        if (!$cmp.Same) { $changes += @([pscustomobject]@{ Backup = $b.Backup; Comparison = $cmp }) }
    }
    foreach ($m in @($baseline.Missing)) { Write-Host "Note: the save backup $m is missing; it cannot be used for the check." -ForegroundColor Yellow }
    if ($changes.Count) {
        Write-Host ''
        Write-Host 'WARNING: your REAL save files changed while the mod was installed.' -ForegroundColor Yellow
        foreach ($c in $changes) {
            foreach ($p in @($c.Comparison.Changed)) { Write-Host "  changed: $p" }
            foreach ($p in @($c.Comparison.Missing)) { Write-Host "  missing: $p" }
            foreach ($p in @($c.Comparison.Added)) { Write-Host "  new:     $p" }
            Write-Host "  The backup from before the friend test is kept in: $($c.Backup)"
        }
        Write-Host 'This should not happen while the mod is installed (the game plays from the save copy).'
        Write-Host 'Steam Cloud may have brought saves from another PC, or the save redirection did not'
        Write-Host 'work. Please run Collect-Logs.bat and send the logs. Uninstall-Coop.bat shows how to'
        Write-Host 'put the backup back. A new install keeps comparing with the backup above.'
        if ($AcceptChangedSaves) {
            Write-Host 'Continuing (-AcceptChangedSaves).'
        } elseif ($interactive) {
            if ((Ask 'Continue the install anyway? [y/N]') -notmatch '^(y|yes)$') { Write-Host 'Cancelled. Nothing was changed.'; exit 1 }
        } else {
            throw 'Your real save files changed since the backup made before the friend test (see above). Nothing was changed. Pass -AcceptChangedSaves to install anyway.'
        }
    } elseif (@($baseline.Baselines).Count) {
        Write-Host 'Real save files: unchanged since the backup made before the friend test.'
    }
}

# ---------------------------------------------------------------- role
Section 'Step 2: role'
if (!$Role) {
    Write-Host '  1: Host - your friend joins YOUR game'
    Write-Host '  2: Join - you join your FRIEND''s game (you need the host''s SteamID64)'
    switch (Ask 'Choose 1 or 2') {
        '1' { $Role = 'Host' }
        '2' { $Role = 'Join' }
        default { throw 'Choose 1 (Host) or 2 (Join). Nothing was changed.' }
    }
}
Write-Host "Role: $Role"
if ($Role -eq 'Host' -and $HostSteamId) { throw '-HostSteamId is only used with -Role Join.' }
if ($Role -eq 'Join') {
    if (!$HostSteamId) {
        Write-Host 'Enter the HOST player''s SteamID64: 17 digits, usually starting with 7656119.'
        Write-Host ' - The host''s installer shows it at the end of its install.'
        Write-Host ' - Or the host opens Steam, clicks their profile name (top right) > Account details;'
        Write-Host '   the "Steam ID" line shows the number.'
    }
    $attempt = 0
    while ($true) {
        if (!$HostSteamId) { $HostSteamId = Ask 'Host SteamID64' }
        $problem = Get-FriendSteamIdProblem -Value $HostSteamId -OwnSteamId $ownSteamId
        if (!$problem) { $HostSteamId = $HostSteamId.Trim(); break }
        $attempt++
        if (!$interactive -or $attempt -ge 5) { throw "Host SteamID64 not accepted: $problem Nothing was changed." }
        Write-Host $problem -ForegroundColor Yellow
        $HostSteamId = ''
    }
    Write-Host "Host SteamID64: $HostSteamId"
}

# ---------------------------------------------------------------- character
Section 'Step 3: character'
$defaultCharacter = Get-FriendDefaultCharacter -Role $Role
if (!$Character -and !$interactive) { $Character = $defaultCharacter }
if (!$Character) {
    Write-Host 'Which character do you play? Each player chooses on their own PC.'
    Write-Host '(The character choice is not yet verified in a live game; this test checks it too.)'
    $descriptions = [ordered]@{
        default = 'your normal choice from sortie preparation'; quiet = 'Quiet'
        dd_male = 'Diamond Dogs soldier (male)'; dd_female = 'Diamond Dogs soldier (female)'
        snake = 'Snake'; avatar = 'your Avatar'; ocelot = 'Ocelot (one revolver)'
    }
    $list = @(Get-FriendCharacters)
    for ($i = 0; $i -lt $list.Count; $i++) { Write-Host ("  {0}: {1,-10} {2}" -f ($i + 1), $list[$i], $descriptions[$list[$i]]) }
    $answer = Ask "Number or name, Enter = $defaultCharacter"
    if (!$answer) { $Character = $defaultCharacter }
    else {
        $n = 0
        if ([int]::TryParse($answer, [ref]$n) -and $n -ge 1 -and $n -le $list.Count) { $Character = $list[$n - 1] } else { $Character = $answer.ToLowerInvariant() }
    }
}
if (!(Test-FriendCharacter $Character)) { throw "Unknown character '$Character'. Use one of: $((Get-FriendCharacters) -join ', '). Nothing was changed." }
Write-Host "Character: $Character"

# ---------------------------------------------------------------- runtime archive
Section 'Step 4: two-player runtime archive'
$archiveSource = $null   # $null = reuse the installed copy
$installedArchiveOk = (Test-Path -LiteralPath $paths.Archive -PathType Leaf) -and (Get-Sha256 $paths.Archive) -eq $archiveSha256
if (!$RuntimeArchive -and !$installedArchiveOk) {
    $nextToPackage = Join-Path $packageRoot 'mgs-coop-player-runtime.dat'
    if ((Test-Path -LiteralPath $nextToPackage -PathType Leaf) -and (Get-Sha256 $nextToPackage) -eq $archiveSha256) { $RuntimeArchive = $nextToPackage }
}
if (!$RuntimeArchive -and !$installedArchiveOk) {
    Write-Host 'The mod needs mgs-coop-player-runtime.dat. It is NOT part of this package because it'
    Write-Host 'contains data generated from the game''s own files. See RUNTIME-ARCHIVE.txt.'
    Write-Host "Expected SHA-256: $archiveSha256"
    $RuntimeArchive = (Ask 'Full path of mgs-coop-player-runtime.dat (you can drag the file into this window)').Trim('"')
    if (!$RuntimeArchive) { throw 'No runtime archive was given. Nothing was changed.' }
}
if ($RuntimeArchive) {
    $RuntimeArchive = $RuntimeArchive.Trim().Trim('"')
    if (!(Test-Path -LiteralPath $RuntimeArchive -PathType Leaf)) { throw "Runtime archive not found: $RuntimeArchive. Nothing was changed." }
    $archiveSource = [IO.Path]::GetFullPath((Get-Item -LiteralPath $RuntimeArchive).FullName)
    if ((Get-Sha256 $archiveSource) -ne $archiveSha256) { throw "This file is not the expected runtime archive (its SHA-256 differs from $archiveSha256). Nothing was changed." }
    if ($archiveSource.Equals($paths.Archive, [StringComparison]::OrdinalIgnoreCase)) { $archiveSource = $null }
    Write-Host 'Runtime archive OK (hash verified).'
} else { Write-Host 'Runtime archive: the installed copy has the expected hash and is reused.' }

# ---------------------------------------------------------------- confirm
Section 'Summary'
Write-Host "  Game folder:  $gameRoot"
Write-Host "  Role:         $Role"
if ($Role -eq 'Join') { Write-Host "  Host ID:      $HostSteamId" }
Write-Host "  Character:    $Character"
Write-Host '  Changes:      installs dinput8.dll + mod files (previous files are backed up),'
Write-Host '                backs up your real MGSV saves, and makes the game use a COPY of'
Write-Host '                your save while the mod is installed (your real save is not written).'
if (!$Yes) {
    $answer = Ask 'Install now? [Y/n]'
    if ($answer -match '^(n|no)$') { Write-Host 'Cancelled. Nothing was changed.'; exit 1 }
}

# ---------------------------------------------------------------- 5: save backup and copy
Section 'Step 5: backing up your real save files'
$stamp = Get-FriendStamp
$null = New-Item -ItemType Directory -Path $paths.Mod -Force
Assert-NoReparsePoint -Path $paths.Mod -StopAt $gameRoot
$backupRoot = Join-Path $paths.Mod 'real-save-backups'
$backupDir = Join-Path $backupRoot $stamp
$n = 1
while (Test-Path -LiteralPath $backupDir) { $backupDir = Join-Path $backupRoot ($stamp + '-' + $n); $n++ }
try { $manifest = Backup-RealSaves -UserData $userData -Destination $backupDir }
catch { throw ('Could not back up your saves: ' + $_.Exception.Message + ' Play MGSV once normally (so a save exists), then run the installer again. Nothing else was changed.') }
Write-Host ("Backed up {0} save files ({1} checked by hash) to {2}" -f @($manifest.all_files).Count, @($manifest.verified).Count, $backupDir)
$seedArchive = Join-Path (Join-Path $paths.Mod 'coop-saves-replaced') $stamp
$reseed = [bool]$ReseedSaves
$seedLocal = Join-Path $saveDir '287700\local'
$alreadySeeded = (Test-Path -LiteralPath $seedLocal) -and @(Get-ChildItem -LiteralPath $seedLocal -File -Force).Count -gt 0
if ($alreadySeeded -and !$reseed) {
    $copyFiles = @(Get-ChildItem -LiteralPath $saveDir -File -Recurse -Force | Sort-Object LastWriteTime -Descending)
    $copyAge = 'unknown'
    if ($copyFiles.Count) { $copyAge = $copyFiles[0].LastWriteTime.ToString('yyyy-MM-dd HH:mm') }
    if (!$state) {
        # No active friend install (first install, or after Uninstall-Coop): the
        # copy is left over from an earlier test and may be far older than the
        # real save, while the game uses the normal online service.
        Write-Host "A co-op save copy from an earlier test exists (last changed $copyAge). It is moved aside and"
        Write-Host 'replaced with a fresh copy of your current real save.'
        $reseed = $true
    } elseif ($interactive) {
        Write-Host "A co-op save copy from this friend test exists (last changed $copyAge); it holds the progress made during co-op tests."
        $reseed = (Ask 'Replace it with a fresh copy of your current real save? The old copy is kept aside. [y/N]') -match '^(y|yes)$'
    }
}
$seed = Copy-SaveSeed -BackupDirectory $backupDir -SaveDir $saveDir -Reseed:$reseed -ArchiveDirectory $seedArchive
if ($seed.Seeded) {
    Write-Host "Co-op save copy created: $saveDir ($($seed.Files) files)"
    if ($alreadySeeded) { Write-Host "The previous copy was moved to $seedArchive" }
} else { Write-Host "Co-op save copy kept from the earlier install: $saveDir" }

# ---------------------------------------------------------------- 6: install
Section 'Step 6: installing the mod'
$managed = @('lab.ini', 'coop.ini', 'script-observe.enabled', 'client-host.txt')
# Previous configuration files: copied aside once, so Uninstall-Coop can report
# (and, for a previous loader, put back) what was there before the first friend install.
$previousFiles = @()
$snapshots = @{}
foreach ($name in $managed) {
    $path = Join-Path $paths.Mod $name
    $snapshots[$name] = $null
    if (Test-Path -LiteralPath $path -PathType Leaf) { $snapshots[$name] = [IO.File]::ReadAllBytes($path) }
    if (!$state) {
        $copy = $null
        if ($null -ne $snapshots[$name]) {
            $copy = $path + '.before-friend-' + $stamp
            [IO.File]::Copy($path, $copy, $false)
        }
        $previousFiles += @([ordered]@{ name = $name; existed = ($null -ne $snapshots[$name]); copy = $copy })
    }
}
$stateSnapshot = $null
if (Test-Path -LiteralPath $statePath -PathType Leaf) { $stateSnapshot = [IO.File]::ReadAllBytes($statePath) }
# The loader state before this run. A failure later in this run restores the
# loader only when this run replaced the ownership record (a new rollback id),
# and puts the configuration back only when the loader is exactly as before.
$recordBefore = [string](Get-FriendProperty (Read-FriendJson $paths.Record) 'rollback_id' '')
$loaderHashBefore = Get-FriendFileSha256 $paths.Loader
$rollbackIds = @()
$backups = @()
$installs = @()
$recordBeforeFriend = $recordBefore
if ($state) {
    $rollbackIds = @(Get-FriendProperty $state 'rollback_ids' @() | ForEach-Object { [string]$_ })
    $backups = @(Get-FriendProperty $state 'backups' @())
    $installs = @(Get-FriendProperty $state 'installs' @())
    $previousFiles = @(Get-FriendProperty $state 'previous_files' @())
    $recordBeforeFriend = [string](Get-FriendProperty $state 'record_before_friend' '')
    if ([string](Get-FriendProperty $state 'status' '') -eq 'pending') {
        # An earlier run stopped between the loader install and its final record
        # (window closed, crash, failed restore). A loader it installed belongs
        # to the friend test too, so Uninstall-Coop undoes it.
        $pendingBefore = [string](Get-FriendProperty $state 'pending_record_before' '')
        if ($recordBefore -cmatch '^[0-9a-f]{32}$' -and $recordBefore -cne $pendingBefore -and $rollbackIds -notcontains $recordBefore) {
            $rollbackIds += @($recordBefore)
            Write-Host 'Note: an earlier install run did not finish; its loader is included in the uninstall.'
        }
    }
}
$backups += @($backupDir)
function New-StateText([string]$Status, [string[]]$Ids, [object[]]$Installs) {
    $s = [ordered]@{
        product = 'mgs-coop-friend-install'; version = $version; game_directory = $gameRoot
        role = $Role; character = $Character; save_dir = $saveDir
        status = $Status; pending_record_before = $(if ($Status -eq 'pending') { $recordBefore } else { '' })
        record_before_friend = $recordBeforeFriend
        first_backup = [string]$backups[0]; latest_backup = $backupDir; backups = $backups; rollback_ids = @($Ids)
        previous_files = $previousFiles; installs = @($Installs)
        note = 'Written by Install-Coop.ps1. Uninstall-Coop.ps1 restores the loader states listed in rollback_ids (status pending: also a newer ownership record than pending_record_before) and checks the real saves against the first backup of each Steam account.'
    }
    return (($s | ConvertTo-Json -Depth 6) + "`n")
}
$loaderChanged = $false
try {
    # Recorded BEFORE the loader is touched, so that Uninstall-Coop can undo an
    # install that stops halfway (status pending).
    Write-FriendTextFile -Path $statePath -Text (New-StateText -Status 'pending' -Ids $rollbackIds -Installs $installs)
    # lab.ini first: without the loader nothing reads it, and with the loader
    # the game never runs without the save redirection.
    Write-FriendTextFile -Path $paths.LabIni -Text (New-FriendLabIniText -Role $Role -SaveDir $saveDir)
    Write-FriendTextFile -Path $paths.CoopIni -Text (New-FriendCoopIniText -Character $Character)

    $arguments = @{ Action = 'Install'; GameDirectory = $gameRoot; PackageDirectory = $packageRoot; NoPrompt = $true }
    if ($archiveSource) { $arguments['RuntimeArchive'] = $archiveSource }
    $loaderChanged = $true
    # The shared commands print developer-package hints (configure-role.bat,
    # one attempt per restart); the friend sees this script's own lines instead.
    $output = @(& $installer @arguments)
    $record = Read-FriendJson $paths.Record
    $rollbackId = [string](Get-FriendProperty $record 'rollback_id' '')
    if ($rollbackId -cnotmatch '^[0-9a-f]{32}$') { throw 'The installer did not leave a valid ownership record.' }
    Write-Host "Loader and mod files installed. Files it replaced are backed up in $($paths.Mod)\install-backups\$rollbackId"

    $roleArguments = @{ Role = $Role; GameDirectory = $gameRoot }
    if ($Role -eq 'Join') { $roleArguments['HostSteamId'] = $HostSteamId }
    $null = & $roleCommand @roleArguments
    if ($Role -eq 'Join') { Write-Host 'Role set: Join (script-observe.enabled = client-v1; the host''s SteamID64 is in client-host.txt).' }
    else { Write-Host 'Role set: Host (script-observe.enabled = host-v3).' }

    $rollbackIds += @($rollbackId)
    $installs += @([ordered]@{ utc = [DateTime]::UtcNow.ToString('o'); role = $Role; character = $Character; rollback_id = $rollbackId; backup = $backupDir; package = $packageRoot })
    Write-FriendTextFile -Path $statePath -Text (New-StateText -Status 'installed' -Ids $rollbackIds -Installs $installs)
} catch {
    $failure = $_
    Write-Host 'Installation failed; undoing the changes...' -ForegroundColor Yellow
    if ($loaderChanged) {
        $recordedId = [string](Get-FriendProperty (Read-FriendJson $paths.Record) 'rollback_id' '')
        if ($recordedId -and $recordedId -cne $recordBefore) {
            # This run's install completed (new ownership record): return to the state before it.
            try { $null = & $installer -Action Restore -GameDirectory $gameRoot; Write-Host 'The previous loader state was restored.' }
            catch { Write-Host "Automatic restore of the loader failed: $($_.Exception.Message)" -ForegroundColor Yellow }
        }
    }
    $loaderAsBefore = ([string](Get-FriendProperty (Read-FriendJson $paths.Record) 'rollback_id' '') -ceq $recordBefore) -and
        ((Get-FriendFileSha256 $paths.Loader) -ceq $loaderHashBefore)
    if ($loaderAsBefore) {
        # The loader is exactly as before this run: put the configuration and the
        # install record back as well.
        foreach ($name in $managed) {
            $path = Join-Path $paths.Mod $name
            try {
                if ($null -ne $snapshots[$name]) { [IO.File]::WriteAllBytes($path, [byte[]]$snapshots[$name]) }
                elseif (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path }
            } catch { Write-Host "Could not restore $name : $($_.Exception.Message)" -ForegroundColor Yellow }
        }
        try {
            if ($null -ne $stateSnapshot) { [IO.File]::WriteAllBytes($statePath, [byte[]]$stateSnapshot) }
            elseif (Test-Path -LiteralPath $statePath -PathType Leaf) { Remove-Item -LiteralPath $statePath }
        } catch { Write-Host "Could not restore friend-install.json : $($_.Exception.Message)" -ForegroundColor Yellow }
        Write-Host 'The game folder is back to its state before this run (the new save backup is kept).'
    } else {
        # Keep lab.ini (the game keeps using the save copy) and the pending
        # install record (Uninstall-Coop can undo this run).
        Write-Host ''
        Write-Host 'The mod loader could NOT be removed automatically.' -ForegroundColor Red
        Write-Host 'lab.ini was kept, so the game still uses the save copy, not your real save files.' -ForegroundColor Red
        Write-Host 'Close this window and run Uninstall-Coop.bat. If that fails too, do not play MGSV' -ForegroundColor Red
        Write-Host 'from this folder and send the output to the person who gave you this package.' -ForegroundColor Red
        Write-Host "(Moving dinput8.dll out of $gameRoot also turns the mod off; the files it replaced" -ForegroundColor Red
        Write-Host " are backed up in $($paths.Mod)\install-backups.)" -ForegroundColor Red
    }
    throw $failure
}

# ---------------------------------------------------------------- done
Section 'Installed.'
Write-Host "Your real saves are backed up in: $backupDir"
Write-Host "While the mod is installed, the game plays from the copy in: $saveDir"
if ($Role -eq 'Host') {
    Write-Host ''
    Write-Host '=================================================================' -ForegroundColor Green
    Write-Host "  YOUR SteamID64: $ownSteamId" -ForegroundColor Green
    Write-Host '  Send this number to your friend; their installer asks for it.' -ForegroundColor Green
    Write-Host '=================================================================' -ForegroundColor Green
    if ($interactive) { try { Set-Clipboard -Value $ownSteamId; Write-Host '(It has also been copied to the clipboard.)' } catch { } }
    Write-Host ''
    Write-Host 'Next: start MGSV from Steam, Continue into Mother Base, and wait for the message'
    Write-Host '"Co-op: host is accepting connections". Then tell your friend to press Ctrl+F8.'
} else {
    Write-Host ''
    Write-Host 'Next: when the host says they are ready, start MGSV from Steam, Continue into'
    Write-Host 'Mother Base, wait for "Co-op: wait for the host to be ready, then press Ctrl+F8",'
    Write-Host 'and press Ctrl+F8.'
}
Write-Host 'See FRIEND-GUIDE.txt for the full test steps. Run Uninstall-Coop.bat before normal play.'
exit 0
