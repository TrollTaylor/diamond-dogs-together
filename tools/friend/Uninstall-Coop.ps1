<#
MGS Co-op uninstaller (two-PC play package). Double-click Uninstall-Coop.bat, or:

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Uninstall-Coop.ps1 -GameDirectory "<MGSV folder>" -NonInteractive -Yes

  0. Switches off a Play-Coop session that did not end cleanly (only the
     files whose SHA-256 still matches play-session.json are deleted), and
     removes what an unfinished automatic save handoff left: link-rejoin.txt
     (a join code) and, in mgs-coop\coop-saves-handoff, request.json and the
     staged copies of the partner's save (by their known file names only;
     provenance.json and applied.json stay, coop-saves-replaced is never touched).
  1. Restores the loader state from before each friend install, through the
     package's owned rollback (scripts\install.ps1 -Action Restore). Only the
     install states that Install-Coop recorded in friend-install.json are
     undone (including one from a run that stopped halfway, status pending);
     an MGS Co-op loader that was there before is left in place.
  2. Renames lab.ini and coop.ini to *.disabled-<time>. When no loader remains,
     the role files are renamed the same way. When a loader remains, the
     copies of lab.ini, coop.ini and the role files from before the friend
     test are put back, and it says plainly that this loader stays active.
     Files of the earlier online friend test (*.online-old-<time>) are never
     restored.
  3. Checks the real save files against the backup made before the friend test
     (the first backup of each Steam account) and reports. It never overwrites
     real saves; it prints how to restore them if they differ.
  4. Offers (default No) to remove the Windows Firewall rule that Play-Coop
     added (administrator prompt).
Nothing else is deleted: backups, the co-op save copy, the copies kept aside
(coop-saves-replaced) and logs stay in <game>\mgs-coop.
#>
[CmdletBinding()]
param(
    [string]$GameDirectory,
    [switch]$NonInteractive,
    [switch]$Yes,
    # Folder with package.json (default: the package folder); used for its
    # installed-location.json only.
    [string]$PackageDirectory,
    # Remove the Windows Firewall rule Play-Coop added without asking (administrator prompt).
    [switch]$RemoveFirewallRule
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
    $packaged = Join-Path $scriptRoot $PackagedName
    if (Test-Path -LiteralPath $packaged -PathType Leaf) { return $packaged }
    $repo = Join-Path $scriptRoot $RepoRelative
    if (Test-Path -LiteralPath $repo -PathType Leaf) { return (Resolve-Path -LiteralPath $repo).Path }
    throw "This package is incomplete: $PackagedName is missing. Extract the whole ZIP again."
}
Import-Module (Resolve-Piece 'FriendCommon.psm1' 'FriendCommon.psm1') -Force -DisableNameChecking
$installer = Resolve-Piece 'install.ps1' '..\package-install.ps1'
$interactive = Test-FriendInteractive -NonInteractive:$NonInteractive
function Ask([string]$Prompt) {
    if (!$interactive) { throw "Missing input: $Prompt (pass the matching parameter)." }
    return ([string](Read-Host $Prompt)).Trim()
}
function Section([string]$Text) { Write-Host ''; Write-Host $Text -ForegroundColor Cyan }

Write-Host 'MGS Co-op - uninstaller' -ForegroundColor Cyan
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

# ---------------------------------------------------------------- 0: play session
# A session whose launcher window or helper still runs is in use, not a leftover.
Import-Module (Join-Path $PSScriptRoot 'LocalCoopSession.psm1') -DisableNameChecking
if (Test-Path -LiteralPath $paths.Mod -PathType Container) { $null = Invoke-LocalCoopRecovery -HostRoot $gameRoot }
if (Test-CoopPlaySessionLive -GameRoot $gameRoot) { throw 'Another Play-Coop window is active for this folder; close it first (or quit MGSV). Nothing was changed.' }
$leftover = Invoke-CoopPlayCleanup -GameRoot $gameRoot
if ($leftover.Ran) {
    Section 'Step 0: switching off the last Play-Coop session'
    Write-Host ('Removed: ' + (@($leftover.Removed) -join ', '))
    foreach ($m in @($leftover.Messages)) { Write-Host $m -ForegroundColor Yellow }
}
$staleHandoff = Remove-CoopStaleHandoff -GameRoot $gameRoot
if (@($staleHandoff.Removed).Count) {
    Write-Host ('Removed what an unfinished automatic save handoff left: ' + (@($staleHandoff.Removed) -join ', '))
}

# ---------------------------------------------------------------- 1: loader
Section 'Step 1: restoring the previous loader'
$friendIds = @(Get-FriendProperty $state 'rollback_ids' @() | ForEach-Object { [string]$_ })
if ([string](Get-FriendProperty $state 'status' '') -eq 'pending') {
    # Install-Coop stopped between the loader install and its final record: a
    # newer ownership record than the one before that run belongs to it.
    $pendingBefore = [string](Get-FriendProperty $state 'pending_record_before' '')
    $current = [string](Get-FriendProperty (Read-FriendJson $paths.Record) 'rollback_id' '')
    if ($current -cmatch '^[0-9a-f]{32}$' -and $current -cne $pendingBefore -and $friendIds -notcontains $current) {
        $friendIds += @($current)
        Write-Host 'Note: the last install run did not finish; its loader is undone as well.'
    }
}
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
    Write-Host 'The configuration and the install record were left in place; the loader stays in pass-through.' -ForegroundColor Red
    Write-Host 'Moving dinput8.dll out of the game folder removes the mod; the files it replaced are backed up' -ForegroundColor Red
    Write-Host "in $($paths.Mod)\install-backups." -ForegroundColor Red
    Write-Host 'Send the output above to the person who gave you this package.' -ForegroundColor Red
    $exitCode = 2
} elseif (!$loaderLeft) {
    Write-Host "Loader removed (restored $restored install state(s)). dinput8.dll is no longer in the game folder."
} else {
    $record = Read-FriendJson $paths.Record
    $version = [string](Get-FriendProperty $record 'version' 'unknown')
    $id = [string](Get-FriendProperty $record 'rollback_id' '')
    $earlierId = [string](Get-FriendProperty $state 'record_before_friend' '')
    if ($record -and (Get-FriendProperty $record 'product') -eq 'mgs-coop-original-loader-probe' -and $id -and $id -ceq $earlierId) {
        Write-Host "The MGS Co-op loader ($version) from BEFORE the friend test is back in place (restored $restored friend install state(s))."
    } elseif ($record -and (Get-FriendProperty $record 'product') -eq 'mgs-coop-original-loader-probe') {
        Write-Host "An MGS Co-op loader ($version) that the friend test did NOT install is in the game folder, for" -ForegroundColor Yellow
        Write-Host "example from the developer package or the single-PC lab. It was not changed (restored $restored friend install state(s))." -ForegroundColor Yellow
    } else {
        Write-Host "A dinput8.dll that the friend test did not install remains in the game folder. It was not changed (restored $restored friend install state(s))." -ForegroundColor Yellow
    }
}

# ---------------------------------------------------------------- 2: configuration
if (!$restoreFailed) {
    Section 'Step 2: disabling the co-op configuration'
    foreach ($name in @('lab.ini', 'coop.ini', 'link-secret.txt')) {
        $renamed = Rename-FriendFile -Path (Join-Path $paths.Mod $name) -Suffix 'disabled' -Stamp $stamp
        if ($renamed) { Write-Host "$name -> $(Split-Path -Leaf $renamed)" }
    }
    $previous = @(Get-FriendProperty $state 'previous_files' @())
    foreach ($name in @('lab.ini', 'coop.ini', 'script-observe.enabled', 'client-host.txt')) {
        $path = Join-Path $paths.Mod $name
        $before = $previous | Where-Object { (Get-FriendProperty $_ 'name') -eq $name } | Select-Object -First 1
        $copy = [string](Get-FriendProperty $before 'copy' '')
        $haveCopy = $before -and (Get-FriendProperty $before 'existed' $false) -and $copy -and (Test-Path -LiteralPath $copy -PathType Leaf)
        if ($loaderLeft -and $haveCopy) {
            # A loader remains: its configuration from before the friend test
            # comes back with it (for the lab, lab.ini keeps it on its save copy).
            [IO.File]::Copy($copy, $path, $true)
            Write-Host "$name restored from the copy made before the friend test."
        } elseif ($name -in @('script-observe.enabled', 'client-host.txt')) {
            $renamed = Rename-FriendFile -Path $path -Suffix 'disabled' -Stamp $stamp
            if ($renamed) { Write-Host "$name -> $(Split-Path -Leaf $renamed)" }
        } elseif ($haveCopy) {
            Write-Host "Your $name from before the friend test is kept as $(Split-Path -Leaf $copy) (no loader remains to use it)."
        }
    }
    $archived = @(Get-FriendProperty $state 'online_archived' @())
    if ($archived.Count) { Write-Host "Files of the earlier online friend test stay archived (*.online-old-*, $($archived.Count) file(s)); they are not restored." }
    $null = Rename-FriendFile -Path $statePath -Suffix 'uninstalled' -Stamp $stamp
    if ($loaderLeft) {
        $labText = Read-SharedText -Path $paths.LabIni
        $lab = ConvertFrom-IniText $labText
        $labSaveDir = ''
        if ($lab.Contains('save_dir') -and $lab.Contains('enabled') -and [string]$lab['enabled'] -match '^(1|true|yes|on)$') { $labSaveDir = [string]$lab['save_dir'] }
        Write-Host ''
        Write-Host 'IMPORTANT: the remaining loader stays ACTIVE whenever MGSV starts from this folder.' -ForegroundColor Yellow
        if ($labSaveDir) {
            Write-Host "Its lab.ini from before the friend test redirects the saves to: $labSaveDir" -ForegroundColor Yellow
        } else {
            Write-Host 'Without a lab.ini save_dir it uses your REAL save files, and the game connects to the' -ForegroundColor Yellow
            Write-Host 'normal online service with it loaded.' -ForegroundColor Yellow
        }
        Write-Host 'To remove it as well, use the installer that put it there (developer package:' -ForegroundColor Yellow
        Write-Host 'install.ps1 -Action Restore), or close MGSV and move dinput8.dll out of the game folder.' -ForegroundColor Yellow
    }
}

# ---------------------------------------------------------------- 3: saves
Section 'Step 3: checking your real save files'
# Compared with the FIRST backup of each Steam account: a reinstall takes a new
# backup, but the reference is the state before the friend test.
$baseline = Get-FriendSaveBaselines @(Get-FriendProperty $state 'backups' @())
if (!@($baseline.Baselines).Count) {
    $latest = [string](Get-FriendProperty $state 'latest_backup' '')
    $baseline = Get-FriendSaveBaselines @($latest)
}
foreach ($m in @($baseline.Missing)) { Write-Host "Note: the save backup $m is missing." -ForegroundColor Yellow }
if (!@($baseline.Baselines).Count) {
    Write-Host 'The save backup recorded by the installer was not found; the real saves could not be checked.' -ForegroundColor Yellow
    if ($exitCode -eq 0) { $exitCode = 3 }
} else {
    foreach ($b in @($baseline.Baselines)) {
        $check = Test-RealSavesAgainstBackup -BackupDirectory $b.Backup
        $cmp = $check.Comparison
        if ($cmp.Same) {
            Write-Host 'Real save files: UNCHANGED since the backup made before the friend test (SHA-256 checked).' -ForegroundColor Green
            continue
        }
        Write-Host 'Real save files: DIFFERENT from the backup made before the friend test.' -ForegroundColor Yellow
        foreach ($p in @($cmp.Changed)) { Write-Host "  changed: $p" }
        foreach ($p in @($cmp.Missing)) { Write-Host "  missing: $p" }
        foreach ($p in @($cmp.Added)) { Write-Host "  new:     $p" }
        Write-Host 'Nothing was overwritten. This is expected if you played without the mod in between.'
        Write-Host "The backup from before the friend test: $($b.Backup)"
        Write-Host 'To go back to it: close MGSV, quit Steam, then copy the CONTENTS of these two folders'
        Write-Host '(replace the files when asked):'
        Write-Host "  $($b.Backup)\files\287700\local  ->  $($check.SourceRoot)\287700\local"
        Write-Host "  $($b.Backup)\files\311340\remote  ->  $($check.SourceRoot)\311340\remote"
        Write-Host 'Copy nothing else: leave remotecache.vdf (Steam Cloud bookkeeping) as it is. Files listed'
        Write-Host 'as "new" are not in the backup; move them out of the folder for an exact return. If Steam'
        Write-Host 'then reports a Steam Cloud conflict, keep the local files. Keep the backup until you are sure.'
        if ($exitCode -eq 0) { $exitCode = 4 }
    }
}
# ---------------------------------------------------------------- 4: firewall rule
$rule = Get-FriendProperty $state 'firewall_rule' $null
$ruleName = [string](Get-FriendProperty $rule 'display_name' '')
if ($ruleName) {
    Section 'Step 4: the Windows Firewall rule added by Play-Coop'
    $remove = [bool]$RemoveFirewallRule
    if (!$remove -and $interactive) { $remove = (Ask "Remove the firewall rule '$ruleName' (administrator prompt)? [y/N]") -match '^(y|yes)$' }
    if ($remove) {
        if (Invoke-CoopElevated -Script (New-CoopRemoveFirewallScript -DisplayName $ruleName)) { Write-Host "Firewall rule '$ruleName' removed." }
        else { Write-Host 'The firewall rule was not removed (cancelled or failed).' -ForegroundColor Yellow }
    } else {
        Write-Host "The firewall rule '$ruleName' stays. Remove it later in Windows Defender Firewall > Advanced settings > Inbound Rules."
    }
}

Section 'Done.'
Write-Host "Kept (delete them yourself when no longer needed): save backups in $($paths.Mod)\real-save-backups,"
Write-Host "the co-op save copy in $($paths.Mod)\coop-saves, and the logs in $($paths.Mod)."
if ($exitCode -eq 0 -or $exitCode -eq 4) {
    if (!$loaderLeft) { Write-Host 'The game is back to normal; you can play online as usual.' }
}
exit $exitCode
