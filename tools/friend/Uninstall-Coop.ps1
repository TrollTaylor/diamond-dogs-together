<#
MGS Co-op friend-test uninstaller. Double-click Uninstall-Coop.bat, or:

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Uninstall-Coop.ps1 -GameDirectory "<MGSV folder>" -NonInteractive -Yes

  1. Restores the loader state from before each friend install, through the
     package's owned rollback (scripts\install.ps1 -Action Restore). Only the
     install states that Install-Coop recorded in friend-install.json are
     undone; an MGS Co-op loader that was there before is left in place.
  2. Renames lab.ini and coop.ini to *.disabled-<time> (and the role files
     when no loader remains, or puts back their previous copies when an earlier
     loader remains).
  3. Checks the real save files against the backup manifest and reports. It
     never overwrites real saves; it prints how to restore them if they differ.
Nothing is deleted: backups, the co-op save copy and logs stay in <game>\mgs-coop.
#>
[CmdletBinding()]
param(
    [string]$GameDirectory,
    [switch]$NonInteractive,
    [switch]$Yes,
    # Folder with package.json (default: the package folder); used for its
    # installed-location.json only.
    [string]$PackageDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = $PSScriptRoot

function Resolve-Piece([string]$PackagedName, [string]$RepoRelative) {
    $packaged = Join-Path $scriptRoot $PackagedName
    if (Test-Path -LiteralPath $packaged -PathType Leaf) { return $packaged }
    $repo = Join-Path $scriptRoot $RepoRelative
    if (Test-Path -LiteralPath $repo -PathType Leaf) { return (Resolve-Path -LiteralPath $repo).Path }
    throw "This package is incomplete: $PackagedName is missing. Extract the whole ZIP again."
}
Import-Module (Resolve-Piece 'LabCommon.psm1' '..\lab\LabCommon.psm1') -Force -DisableNameChecking
Import-Module (Resolve-Piece 'FriendCommon.psm1' 'FriendCommon.psm1') -Force -DisableNameChecking
$installer = Resolve-Piece 'install.ps1' '..\package-install.ps1'
$interactive = Test-FriendInteractive -NonInteractive:$NonInteractive
function Ask([string]$Prompt) {
    if (!$interactive) { throw "Missing input: $Prompt (pass the matching parameter)." }
    return ([string](Read-Host $Prompt)).Trim()
}
function Section([string]$Text) { Write-Host ''; Write-Host $Text -ForegroundColor Cyan }

Write-Host 'MGS Co-op - friend test uninstaller' -ForegroundColor Cyan
if (!$PackageDirectory) { $PackageDirectory = Split-Path -Parent $scriptRoot }

# ---------------------------------------------------------------- game folder
if (!$GameDirectory) {
    $location = Read-FriendJson (Join-Path $PackageDirectory 'installed-location.json')
    $recorded = [string](Get-FriendProperty $location 'game_directory' '')
    $candidates = @()
    if ($recorded -and (Test-Path -LiteralPath (Join-Path $recorded 'mgs-coop\friend-install.json') -PathType Leaf)) { $candidates = @($recorded) }
    else {
        $registry = Get-RegistryMgsvCandidates
        $candidates = @(Find-MgsvGameFolders -SteamRoots @($registry.SteamRoots) -Candidates @($registry.Candidates) |
                Where-Object { Test-Path -LiteralPath (Join-Path $_ 'mgs-coop\friend-install.json') -PathType Leaf })
    }
    if ($candidates.Count -eq 1) {
        Write-Host "Friend-test install found in: $($candidates[0])"
        $GameDirectory = $candidates[0]
    } else {
        $GameDirectory = Ask 'Full path of the MGSV folder where the friend test is installed'
    }
}
$GameDirectory = $GameDirectory.Trim().Trim('"')
if (!(Test-Path -LiteralPath $GameDirectory -PathType Container)) { throw "Folder not found: $GameDirectory" }
$gameRoot = [IO.Path]::GetFullPath((Get-Item -LiteralPath $GameDirectory -Force).FullName).TrimEnd('\', '/')
$paths = Get-InstancePaths -GameDirectory $gameRoot
$statePath = Join-Path $paths.Mod 'friend-install.json'
$state = Read-FriendJson $statePath
if (!$state -or (Get-FriendProperty $state 'product') -ne 'mgs-coop-friend-install') {
    throw "No friend-test install record (mgs-coop\friend-install.json) in $gameRoot. Nothing was changed."
}
if (@(Get-GameProcessInDirectory -Directory $gameRoot).Count) { throw 'MGSV is running from this folder. Quit the game normally, then run the uninstaller again. Nothing was changed.' }
Write-Host "Game folder: $gameRoot"
if (!$Yes) {
    $answer = Ask 'Remove the friend-test install from this folder? [Y/n]'
    if ($answer -match '^(n|no)$') { Write-Host 'Cancelled. Nothing was changed.'; exit 1 }
}
$stamp = Get-FriendStamp
$exitCode = 0

# ---------------------------------------------------------------- 1: loader
Section 'Step 1: restoring the previous loader'
$friendIds = @(Get-FriendProperty $state 'rollback_ids' @() | ForEach-Object { [string]$_ })
$restored = 0
$restoreFailed = $null
for ($i = 0; $i -lt 32; $i++) {
    $record = Read-FriendJson $paths.Record
    $id = [string](Get-FriendProperty $record 'rollback_id' '')
    if (!$record -or !$id -or $friendIds -notcontains $id) { break }
    try {
        $null = & $installer -Action Restore -GameDirectory $gameRoot
        $restored++
    } catch { $restoreFailed = $_.Exception.Message; break }
}
$loaderLeft = Test-Path -LiteralPath $paths.Loader -PathType Leaf
if ($restoreFailed) {
    Write-Host "The loader could not be restored: $restoreFailed" -ForegroundColor Red
    Write-Host 'lab.ini was left in place so that the game keeps using the save copy, not your real save.' -ForegroundColor Red
    Write-Host "Backups of the replaced files are in $($paths.Mod)\install-backups. Send the output above to the person who gave you this package." -ForegroundColor Red
    $exitCode = 2
} elseif (!$loaderLeft) {
    Write-Host "Loader removed (restored $restored install state(s)). dinput8.dll is no longer in the game folder."
} else {
    $record = Read-FriendJson $paths.Record
    $version = [string](Get-FriendProperty $record 'version' 'unknown')
    if ($record -and (Get-FriendProperty $record 'product') -eq 'mgs-coop-original-loader-probe') {
        Write-Host "An MGS Co-op loader ($version) that was installed BEFORE the friend test was put back (restored $restored state(s))."
    } else {
        Write-Host 'A dinput8.dll that was there before the friend test remains in the game folder.'
    }
}

# ---------------------------------------------------------------- 2: configuration
if (!$restoreFailed) {
    Section 'Step 2: disabling the co-op configuration'
    foreach ($name in @('lab.ini', 'coop.ini')) {
        $renamed = Rename-FriendFile -Path (Join-Path $paths.Mod $name) -Suffix 'disabled' -Stamp $stamp
        if ($renamed) { Write-Host "$name -> $(Split-Path -Leaf $renamed)" }
    }
    $previous = @(Get-FriendProperty $state 'previous_files' @())
    foreach ($name in @('script-observe.enabled', 'client-host.txt')) {
        $path = Join-Path $paths.Mod $name
        $before = $previous | Where-Object { (Get-FriendProperty $_ 'name') -eq $name } | Select-Object -First 1
        $copy = [string](Get-FriendProperty $before 'copy' '')
        if ($loaderLeft -and $before -and (Get-FriendProperty $before 'existed' $false) -and $copy -and (Test-Path -LiteralPath $copy -PathType Leaf)) {
            [IO.File]::Copy($copy, $path, $true)
            Write-Host "$name restored from the copy made before the friend test."
        } else {
            $renamed = Rename-FriendFile -Path $path -Suffix 'disabled' -Stamp $stamp
            if ($renamed) { Write-Host "$name -> $(Split-Path -Leaf $renamed)" }
        }
    }
    foreach ($row in $previous) {
        $n = [string](Get-FriendProperty $row 'name' '')
        $copy = [string](Get-FriendProperty $row 'copy' '')
        if ($n -in @('lab.ini', 'coop.ini') -and $copy -and (Test-Path -LiteralPath $copy -PathType Leaf)) {
            Write-Host "Your $n from before the friend test is kept as $(Split-Path -Leaf $copy) (not restored automatically)."
        }
    }
    $null = Rename-FriendFile -Path $statePath -Suffix 'uninstalled' -Stamp $stamp
}

# ---------------------------------------------------------------- 3: saves
Section 'Step 3: checking your real save files'
$backup = [string](Get-FriendProperty $state 'latest_backup' '')
if (!$backup -or !(Test-Path -LiteralPath (Join-Path $backup 'manifest.json') -PathType Leaf)) {
    Write-Host 'The save backup recorded by the installer was not found; the real saves could not be checked.' -ForegroundColor Yellow
    if ($exitCode -eq 0) { $exitCode = 3 }
} else {
    $check = Test-RealSavesAgainstBackup -BackupDirectory $backup
    $cmp = $check.Comparison
    if ($cmp.Same) {
        Write-Host 'Real save files: UNCHANGED since the backup made at install time (SHA-256 checked).' -ForegroundColor Green
    } else {
        Write-Host 'Real save files: DIFFERENT from the backup made at install time.' -ForegroundColor Yellow
        foreach ($p in @($cmp.Changed)) { Write-Host "  changed: $p" }
        foreach ($p in @($cmp.Missing)) { Write-Host "  missing: $p" }
        foreach ($p in @($cmp.Added)) { Write-Host "  new:     $p" }
        Write-Host 'Nothing was overwritten. This is expected if you played without the mod in between.'
        Write-Host 'To go back to the backup: close MGSV and Steam, then copy'
        Write-Host "  $backup\files\287700\*  ->  $($check.SourceRoot)\287700\"
        Write-Host "  $backup\files\311340\*  ->  $($check.SourceRoot)\311340\"
        Write-Host '(replace the files when asked). Keep the backup folder until you are sure.'
        if ($exitCode -eq 0) { $exitCode = 4 }
    }
}
Section 'Done.'
Write-Host "Kept (delete them yourself when no longer needed): save backups in $($paths.Mod)\real-save-backups,"
Write-Host "the co-op save copy in $($paths.Mod)\coop-saves, and the logs in $($paths.Mod)."
if ($exitCode -eq 0 -or $exitCode -eq 4) {
    if (!$loaderLeft) { Write-Host 'The game is back to normal; you can play online as usual.' }
}
exit $exitCode
