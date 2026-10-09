<#
MGS Co-op save sharing: give a friend the same progress for co-op.

Double-click Share-Save.bat and choose:
  [E] Export my co-op save   - writes mgs-coop-save-<time>.zip to your Desktop.
                               Send that file to your friend.
  [I] Import a friend's save - puts the friend's progress into YOUR co-op save
                               copy (mgs-coop\coop-saves). Your previous copy is
                               kept aside and [R] brings it back.
  [R] Restore                - puts back the co-op save copy that the last
                               import replaced.

Only the co-op save COPY that Play-Coop uses is read or changed. Your real
Steam saves (normal game starts) are never read or written by this script.
Your own settings (TPP_CONFIG_DATA, TPP_GRAPHICS_CONFIG) are kept on import;
only the progress files (TPP_GAME_DATA*, PERSONAL_DATA*) come from the friend.

MGSV saves may be tied to the Steam account that made them. If the game does
not accept the imported progress (an error or a new game at the title), run
Share-Save.bat again and choose [R] to restore your own copy.
#>
[CmdletBinding()]
param(
    [ValidateSet('', 'Export', 'Import', 'Restore')][string]$Mode = '',
    [string]$GameDirectory,
    # Import: the zip from the friend (also accepted as a drag-and-drop argument).
    [string]$ZipPath,
    # Export: where the zip goes (default: the Desktop).
    [string]$OutputDirectory,
    [switch]$NonInteractive
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = $PSScriptRoot
trap {
    Write-Host ''
    Write-Host ('Stopped: ' + $_.Exception.Message) -ForegroundColor Red
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
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$PackageDirectory = Split-Path -Parent $scriptRoot
$interactive = !$NonInteractive -and [Environment]::UserInteractive

# The files that carry progress (taken from the friend) and the subfolders of
# the co-op save copy (Install-Coop's Copy-SaveSeed layout).
$progressPattern = '^(TPP_GAME_DATA[0-9]?|PERSONAL_DATA[0-9]?)$'
$subdirs = @('287700\local', 'remote')
$maxFileBytes = 4MB
$formatName = 'mgs-coop-save-share'

function Ask([string]$Prompt) {
    if (!$interactive) { return '' }
    return [string](Read-Host $Prompt)
}
function Section([string]$Text) { Write-Host ''; Write-Host "== $Text" -ForegroundColor Cyan }
function Get-FileSha([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant() }

# ---------------------------------------------------------------- game folder
if (!$GameDirectory) {
    $location = Read-FriendJson (Join-Path $PackageDirectory 'installed-location.json')
    $recorded = [string](Get-FriendProperty $location 'game_directory' '')
    if ($recorded -and (Test-Path -LiteralPath (Join-Path $recorded 'mgs-coop\friend-install.json') -PathType Leaf)) { $GameDirectory = $recorded }
    else {
        $registry = Get-RegistryMgsvCandidates
        $found = @(Find-MgsvGameFolders -SteamRoots @($registry.SteamRoots) -Candidates @($registry.Candidates) |
                Where-Object { Test-Path -LiteralPath (Join-Path $_ 'mgs-coop\friend-install.json') -PathType Leaf })
        if ($found.Count -eq 1) { $GameDirectory = $found[0] }
        else { throw 'No MGS Co-op install was found. Run Install-Coop.bat first (or pass -GameDirectory).' }
    }
}
$GameDirectory = $GameDirectory.Trim().Trim('"')
if (!(Test-Path -LiteralPath $GameDirectory -PathType Container)) { throw "Folder not found: $GameDirectory" }
$gameRoot = [IO.Path]::GetFullPath((Get-Item -LiteralPath $GameDirectory -Force).FullName).TrimEnd('\', '/')
$paths = Get-InstancePaths -GameDirectory $gameRoot
$saveDir = $paths.CoopSaves
$replacedRoot = Join-Path $paths.Mod 'coop-saves-replaced'

Write-Host 'MGS Co-op - share a co-op save' -ForegroundColor Cyan
Write-Host "Game folder: $gameRoot"
if (Test-Path -LiteralPath $paths.Mod) { Assert-NoReparsePoint -Path $paths.Mod -StopAt $gameRoot }
if (@(Get-GameProcessInDirectory -Directory $gameRoot).Count) { throw 'MGSV is running from this folder. Quit it first, then run Share-Save again. Nothing was changed.' }
if (Test-Path -LiteralPath $paths.PlaySession) {
    if (Test-CoopPlaySessionLive -GameRoot $gameRoot) { throw 'A Play-Coop window is active for this folder; close it first. Nothing was changed.' }
}
if (!(Test-Path -LiteralPath (Join-Path $saveDir '287700\local') -PathType Container)) {
    throw 'The co-op save copy (mgs-coop\coop-saves) is missing. Run Install-Coop.bat first.'
}

if (!$Mode) {
    if ($ZipPath) { $Mode = 'Import' }
    else {
        Section 'What do you want to do?'
        Write-Host '  [E] Export my co-op save (a zip on the Desktop to send to your friend)'
        Write-Host "  [I] Import a friend's save (your current co-op copy is kept aside)"
        Write-Host '  [R] Restore the co-op copy the last import replaced'
        Write-Host '  [Q] Quit'
        switch -Regex (Ask 'Choose E, I, R or Q') {
            '^[Ee]' { $Mode = 'Export' }
            '^[Ii]' { $Mode = 'Import' }
            '^[Rr]' { $Mode = 'Restore' }
            default { Write-Host 'Nothing was changed.'; exit 0 }
        }
    }
}

# ---------------------------------------------------------------- export
if ($Mode -eq 'Export') {
    $files = New-Object Collections.Generic.List[object]
    foreach ($sub in $subdirs) {
        $dir = Join-Path $saveDir $sub
        if (!(Test-Path -LiteralPath $dir -PathType Container)) { continue }
        foreach ($f in @(Get-ChildItem -LiteralPath $dir -File -Force)) {
            if ($f.Name -cnotmatch $progressPattern) { continue }
            if ($f.Length -le 0 -or $f.Length -gt $maxFileBytes) { throw "Unexpected save file size: $($f.FullName)" }
            $files.Add([pscustomobject]@{ Path = $f.FullName; Entry = ($sub.Replace('\', '/') + '/' + $f.Name); Bytes = $f.Length })
        }
    }
    if (!@($files | Where-Object { $_.Entry -like '287700/local/TPP_GAME_DATA*' }).Count) { throw 'The co-op save copy has no TPP_GAME_DATA file. Play once with Play-Coop (or run Install-Coop.bat), then export again.' }
    if (!$OutputDirectory) { $OutputDirectory = [Environment]::GetFolderPath('Desktop') }
    if (!(Test-Path -LiteralPath $OutputDirectory -PathType Container)) { throw "Folder not found: $OutputDirectory" }
    $zipOut = Join-Path $OutputDirectory ('mgs-coop-save-' + (Get-FriendStamp) + '.zip')
    $manifestFiles = @(foreach ($f in $files) { [ordered]@{ entry = $f.Entry; bytes = $f.Bytes; sha256 = (Get-FileSha $f.Path) } })
    $manifest = [ordered]@{ format = $formatName; v = 1; exported_utc = [DateTime]::UtcNow.ToString('o'); files = $manifestFiles }
    $stream = [IO.File]::Open($zipOut, [IO.FileMode]::CreateNew)
    try {
        $zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($f in $files) { [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.Path, $f.Entry) }
            $entry = $zip.CreateEntry('share-save.json')
            $w = New-Object IO.StreamWriter($entry.Open(), (New-Object Text.UTF8Encoding $false))
            try { $w.Write(($manifest | ConvertTo-Json -Depth 5)) } finally { $w.Dispose() }
        } finally { $zip.Dispose() }
    } finally { $stream.Dispose() }
    # Re-read what was written: every hash must match the source.
    $check = [IO.Compression.ZipFile]::OpenRead($zipOut)
    try { if (@($check.Entries).Count -ne $files.Count + 1) { throw 'The written zip is incomplete.' } } finally { $check.Dispose() }
    Section 'Done'
    Write-Host "Exported $($files.Count) save files to:"
    Write-Host "     $zipOut" -ForegroundColor Green
    Write-Host 'Send this file to your friend. They run Share-Save.bat, choose [I] and pick the file.'
    Write-Host 'Only your co-op save copy was read; your real saves and settings are not in the zip.'
    exit 0
}

# ---------------------------------------------------------------- restore
function Get-ImportBackups {
    if (!(Test-Path -LiteralPath $replacedRoot -PathType Container)) { return @() }
    return @(Get-ChildItem -LiteralPath $replacedRoot -Directory -Force | Where-Object { $_.Name -like '*-before-import' } | Sort-Object Name -Descending)
}
function Move-CurrentAside([string]$Suffix) {
    $null = New-Item -ItemType Directory -Path $replacedRoot -Force
    $dest = Join-Path $replacedRoot ((Get-FriendStamp) + $Suffix)
    $n = 1
    while (Test-Path -LiteralPath $dest) { $dest = Join-Path $replacedRoot ((Get-FriendStamp) + '-' + $n + $Suffix); $n++ }
    Move-Item -LiteralPath $saveDir -Destination $dest
    return $dest
}

if ($Mode -eq 'Restore') {
    $backups = @(Get-ImportBackups)
    if (!$backups.Count) { throw 'No co-op save copy from before an import was found. Nothing was changed.' }
    $pick = $backups[0]
    Write-Host "Restoring the co-op save copy kept at $($pick.FullName)"
    if ($interactive -and (Ask 'Restore it now? The current copy is kept aside too. [Y/n]') -match '^(n|no)$') { Write-Host 'Nothing was changed.'; exit 0 }
    $aside = Move-CurrentAside '-before-restore'
    try { Move-Item -LiteralPath $pick.FullName -Destination $saveDir }
    catch { Move-Item -LiteralPath $aside -Destination $saveDir; throw }
    Section 'Done'
    Write-Host 'Your previous co-op save copy is back. The replaced copy was kept at:'
    Write-Host "     $aside"
    exit 0
}

# ---------------------------------------------------------------- import
if (!$ZipPath) {
    Write-Host "Drag the friend's mgs-coop-save-....zip onto this window (or type its path), then press Enter."
    $ZipPath = Ask 'Zip file'
}
$ZipPath = ([string]$ZipPath).Trim().Trim('"')
if (!$ZipPath -or !(Test-Path -LiteralPath $ZipPath -PathType Leaf)) { throw "File not found: $ZipPath. Nothing was changed." }
if ((Get-Item -LiteralPath $ZipPath).Length -gt 32MB) { throw 'That file is too large to be a co-op save. Nothing was changed.' }

$stage = Join-Path $paths.Mod ('save-import-' + (Get-FriendStamp))
$zip = [IO.Compression.ZipFile]::OpenRead($ZipPath)
try {
    $manifestEntry = $zip.GetEntry('share-save.json')
    if (!$manifestEntry) { throw 'This zip was not made by Share-Save (share-save.json is missing). Nothing was changed.' }
    $r = New-Object IO.StreamReader($manifestEntry.Open())
    try { $manifest = $r.ReadToEnd() | ConvertFrom-Json } finally { $r.Dispose() }
    if ([string](Get-FriendProperty $manifest 'format' '') -ne $formatName) { throw 'This zip is not a Share-Save export. Nothing was changed.' }
    $want = @{}
    foreach ($m in @($manifest.files)) { $want[[string]$m.entry] = $m }
    $null = New-Item -ItemType Directory -Path $stage
    $taken = 0
    foreach ($e in @($zip.Entries)) {
        if ($e.FullName -eq 'share-save.json') { continue }
        $parts = $e.FullName.Split('/')
        $sub = ($parts[0..($parts.Count - 2)] -join '/')
        $name = $parts[-1]
        if (@('287700/local', 'remote') -notcontains $sub -or $name -cnotmatch $progressPattern) { throw "Unexpected file in the zip: $($e.FullName). Nothing was changed." }
        if ($e.Length -le 0 -or $e.Length -gt $maxFileBytes) { throw "Unexpected file size in the zip: $($e.FullName). Nothing was changed." }
        $m = $want[$e.FullName]
        if (!$m) { throw "The zip's file list does not name $($e.FullName). Nothing was changed." }
        $dest = Join-Path (Join-Path $stage $sub.Replace('/', '\')) $name
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force
        [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dest, $false)
        if ((Get-FileSha $dest) -ne ([string]$m.sha256).ToUpperInvariant()) { throw "$($e.FullName) is damaged (hash mismatch). Ask your friend to export again. Nothing was changed." }
        $taken++
    }
    if (!(Test-Path -LiteralPath (Join-Path $stage '287700\local\TPP_GAME_DATA0')) -and !(Test-Path -LiteralPath (Join-Path $stage '287700\local\TPP_GAME_DATA1'))) {
        throw 'The zip has no TPP_GAME_DATA file. Nothing was changed.'
    }
} catch {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    throw
} finally { $zip.Dispose() }

$exported = [string](Get-FriendProperty $manifest 'exported_utc' 'unknown')
Write-Host "Friend's save: $taken progress files, exported $exported (UTC)."
Write-Host 'Your current co-op copy is kept aside; your own settings stay; your real saves are not touched.'
if ($interactive -and (Ask 'Use this progress for co-op now? [Y/n]') -match '^(n|no)$') {
    Remove-Item -LiteralPath $stage -Recurse -Force
    Write-Host 'Nothing was changed.'; exit 0
}
# New copy = your current copy (settings included) with the friend's progress files on top.
$newDir = Join-Path $paths.Mod ('coop-saves-new-' + (Get-FriendStamp))
try {
    Copy-Item -LiteralPath $saveDir -Destination $newDir -Recurse
    foreach ($sub in $subdirs) {
        $target = Join-Path $newDir $sub
        $null = New-Item -ItemType Directory -Path $target -Force
        # Drop the old progress files so a slot the friend does not have cannot mix two players' progress.
        foreach ($old in @(Get-ChildItem -LiteralPath $target -File -Force | Where-Object { $_.Name -cmatch $progressPattern })) { Remove-Item -LiteralPath $old.FullName -Force }
        $from = Join-Path $stage $sub
        if (Test-Path -LiteralPath $from) { foreach ($f in @(Get-ChildItem -LiteralPath $from -File -Force)) { Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $target $f.Name) } }
    }
    $aside = Move-CurrentAside '-before-import'
    try { Move-Item -LiteralPath $newDir -Destination $saveDir }
    catch { Move-Item -LiteralPath $aside -Destination $saveDir; throw }
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    if (Test-Path -LiteralPath $newDir) { Remove-Item -LiteralPath $newDir -Recurse -Force }
}
Section 'Done'
Write-Host "Your co-op save copy now has your friend's progress. Your previous copy is kept at:"
Write-Host "     $aside"
Write-Host 'Start Play-Coop.bat as usual. If the game does not accept the save, run Share-Save.bat and choose [R].'
exit 0
