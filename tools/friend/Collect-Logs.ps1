<#
Collects the MGS Co-op logs of one PC into a ZIP on the Desktop
(mgs-coop-logs-<role>-<time>.zip) for sending to the developer.

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Collect-Logs.ps1 [-GameDirectory "<MGSV folder>"] [-OutputDirectory <folder>]

Included (bounded, read-only, the game may still be running):
  probe.log (tail), script-observer.log (the last 3 sessions: from the
  3rd-last "script adapter" start line), steam-observer.log (tail), early.log,
  lab-status.json, link-status.json, play-session.json, lab.ini, coop.ini,
  script-observe.enabled, probe-install.json, client-host.txt,
  host-steamid.txt and friend-install.json, coop-saves.txt (the co-op save
  folders: names, sizes and times only, never save contents), plus
  summary.txt with versions, file hashes, the co-op save state and the real
  save check. Never included: link-secret.txt (the link key), steam-join.ini
  and any save file. SteamIDs are masked except their last 4
  digits; Steam account folder numbers and the Windows user name in paths are
  removed; join codes (MGS1-... and MGS2-...) and invite strings
  (+mgscoop_join ...) are replaced and public IPv4 addresses after peer=,
  net_peer_addr=, "peer": and host= are shown as a.b.x.x.
#>
[CmdletBinding()]
param(
    [string]$GameDirectory,
    # Default: the Desktop.
    [string]$OutputDirectory,
    [switch]$NonInteractive,
    [string]$PackageDirectory,
    # Per-log limit in bytes (the ZIP stays well below a few MB).
    [ValidateRange(4096, 16777216)][int]$MaxLogBytes = 1048576
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
$interactive = Test-FriendInteractive -NonInteractive:$NonInteractive
if (!$PackageDirectory) { $PackageDirectory = Split-Path -Parent $scriptRoot }

Write-Host 'MGS Co-op - collecting logs' -ForegroundColor Cyan
if (!$GameDirectory) {
    $location = Read-FriendJson (Join-Path $PackageDirectory 'installed-location.json')
    $recorded = [string](Get-FriendProperty $location 'game_directory' '')
    if ($recorded -and (Test-Path -LiteralPath (Join-Path $recorded 'mgs-coop') -PathType Container)) { $GameDirectory = $recorded }
    else {
        $registry = Get-RegistryMgsvCandidates
        $found = @(Find-MgsvGameFolders -SteamRoots @($registry.SteamRoots) -Candidates @($registry.Candidates) |
                Where-Object { Test-Path -LiteralPath (Join-Path $_ 'mgs-coop') -PathType Container })
        if ($found.Count -eq 1) { $GameDirectory = $found[0] }
        elseif ($interactive) { $GameDirectory = ([string](Read-Host 'Full path of the MGSV folder (contains mgsvtpp.exe)')).Trim().Trim('"') }
        else { throw 'Pass -GameDirectory.' }
    }
}
if (!(Test-Path -LiteralPath $GameDirectory -PathType Container)) { throw "Folder not found: $GameDirectory" }
$gameRoot = [IO.Path]::GetFullPath((Get-Item -LiteralPath $GameDirectory -Force).FullName).TrimEnd('\', '/')
$paths = Get-InstancePaths -GameDirectory $gameRoot
if (!(Test-Path -LiteralPath $paths.Mod -PathType Container)) { throw "No mgs-coop folder in $gameRoot; nothing to collect." }
Write-Host "Game folder: $gameRoot"

$marker = Read-SharedText -Path $paths.Marker
# The ZIP name: the last Play-Coop role, else the install's usual role, else the marker.
$role = ConvertTo-FriendRoleName $marker
$playSession = Read-FriendJson $paths.PlaySession
$installState = Read-FriendJson (Join-Path $paths.Mod 'friend-install.json')
$playRole = [string](Get-FriendProperty $playSession 'role' '')
$hint = [string](Get-FriendProperty $installState 'role_hint' (Get-FriendProperty $installState 'role' ''))
if ($playRole -eq 'host') { $role = 'host' } elseif ($playRole -eq 'guest') { $role = 'join' }
elseif ($hint -eq 'Host') { $role = 'host' } elseif ($hint -eq 'Join') { $role = 'join' }
$stamp = Get-FriendStamp
if (!$OutputDirectory) { $OutputDirectory = [Environment]::GetFolderPath('Desktop') }
if (!$OutputDirectory -or !(Test-Path -LiteralPath $OutputDirectory -PathType Container)) { throw "Output folder not found: $OutputDirectory" }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\', '/')
$zipPath = Join-Path $OutputDirectory ("mgs-coop-logs-$role-$stamp.zip")
$staging = Join-Path ([IO.Path]::GetTempPath()) ('mgs-coop-logs-' + [Guid]::NewGuid().ToString('N'))
$content = Join-Path $staging ("mgs-coop-logs-$role-$stamp")
$null = New-Item -ItemType Directory -Path $content -Force
$utf8 = New-Object Text.UTF8Encoding $false
function Save([string]$Name, [AllowEmptyString()][string]$Text) {
    [IO.File]::WriteAllText((Join-Path $content $Name), [string](Hide-CoopNetworkText (Hide-FriendSensitiveText $Text)), $utf8)
}
try {
    $included = New-Object Collections.Generic.List[string]
    $missing = New-Object Collections.Generic.List[string]
    # script-observer.log: the last 3 loader sessions (a problem session is often
    # followed by a short check), bounded to 3 logs' worth.
    $observerLimitChars = [int][Math]::Min(16777216, 3 * [Int64]$MaxLogBytes)
    $observer = Read-FriendFileTail -Path (Join-Path $paths.Mod 'script-observer.log') -MaxBytes ([Math]::Min(16777216, $MaxLogBytes * 8))
    if ($observer) {
        Save 'script-observer.log' (Limit-TextTail -Text (Get-TextFromLastMarkers -Text $observer -Marker 'MGS Co-op script adapter' -Count 3) -MaxChars $observerLimitChars)
        $included.Add('script-observer.log')
    } else { $missing.Add('script-observer.log') }
    foreach ($row in @(@('probe.log', 262144), @('steam-observer.log', 262144), @('early.log', 65536))) {
        $text = Read-FriendFileTail -Path (Join-Path $paths.Mod $row[0]) -MaxBytes ([Math]::Min($MaxLogBytes, [int]$row[1]))
        if ($text) { Save $row[0] $text; $included.Add($row[0]) } else { $missing.Add($row[0]) }
    }
    # Allow-list. link-secret.txt (the link key) and steam-join.ini are never collected.
    foreach ($name in @('lab-status.json', 'link-status.json', 'play-session.json', 'lab.ini', 'coop.ini', 'script-observe.enabled', 'probe-install.json', 'client-host.txt', 'host-steamid.txt', 'friend-install.json')) {
        $text = Read-FriendFileTail -Path (Join-Path $paths.Mod $name) -MaxBytes 65536
        if ($text) { Save $name $text; $included.Add($name) } else { $missing.Add($name) }
    }
    # The co-op save folders (which copy is active, what was replaced or imported):
    # relative names, sizes and times only; no save file is read or collected.
    Save 'coop-saves.txt' (((Get-FriendSaveListing -ModDirectory $paths.Mod) -join "`r`n") + "`r`n")
    $included.Add('coop-saves.txt')

    # ------------------------------------------------------------ summary
    $package = Read-FriendJson (Join-Path $PackageDirectory 'package.json')
    $record = Read-FriendJson $paths.Record
    $state = Read-FriendJson (Join-Path $paths.Mod 'friend-install.json')
    $exe = Get-FriendFileSha256 $paths.Exe
    $steamApi = Get-FriendFileSha256 $paths.SteamApi
    $loader = Get-FriendFileSha256 $paths.Loader
    $archive = Get-FriendFileSha256 $paths.Archive
    $expectExe = [string](Get-FriendProperty $package 'executable_sha256' '085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45')
    $expectSteam = [string](Get-FriendProperty $package 'steam_dll_sha256' '08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A')
    $expectArchive = '87BBE82BB1E907CF405200155DA2794BF7E1F5A3D45BF66426A38CDD31A91F96'
    if ($package) { $expectArchive = [string]$package.runtime_archive.sha256 }
    $saveCheck = 'not checked (no install record)'
    # Against the first backup of each Steam account (the state before the
    # friend test), like Uninstall-Coop.
    $baseline = Get-FriendSaveBaselines @(Get-FriendProperty $state 'backups' @())
    $results = @()
    foreach ($b in @($baseline.Baselines)) {
        try {
            $cmp = (Test-RealSavesAgainstBackup -BackupDirectory $b.Backup).Comparison
            if ($cmp.Same) { $results += 'UNCHANGED since the backup made before the friend test' }
            else { $results += "DIFFERENT from the backup made before the friend test (changed=$(@($cmp.Changed).Count) missing=$(@($cmp.Missing).Count) new=$(@($cmp.Added).Count))" }
        } catch { $results += ('check failed: ' + $_.Exception.Message) }
    }
    if (@($baseline.Missing).Count) { $results += ('missing backups=' + @($baseline.Missing).Count) }
    if ($results.Count) { $saveCheck = $results -join '; ' }
    $installStatus = [string](Get-FriendProperty $state 'status' 'none')
    $running = @(Get-GameProcessInDirectory -Directory $gameRoot).Count -gt 0
    $os = [Environment]::OSVersion.VersionString
    # The co-op save copy: its file count, the Share-Save import marker and the
    # newest folder set aside (an import, a restore or an Install-Coop reseed).
    $coopSaveFiles = 0
    if (Test-Path -LiteralPath $paths.CoopSaves -PathType Container) { $coopSaveFiles = @(Get-ChildItem -LiteralPath $paths.CoopSaves -File -Recurse -Force -ErrorAction SilentlyContinue).Count }
    $importMarkerState = 'absent'
    if (Test-Path -LiteralPath (Join-Path $paths.CoopSaves 'imported-progress.json') -PathType Leaf) { $importMarkerState = 'present' }
    $newestReplaced = 'none'
    $replacedRoot = Join-Path $paths.Mod 'coop-saves-replaced'
    if (Test-Path -LiteralPath $replacedRoot -PathType Container) {
        foreach ($d in @(Get-ChildItem -LiteralPath $replacedRoot -Directory -Force)) {
            if ($d.Name -cmatch '^[0-9]{8}-[0-9]{6}' -and ($newestReplaced -eq 'none' -or [string]::CompareOrdinal($d.Name, $newestReplaced) -gt 0)) { $newestReplaced = $d.Name }
        }
    }
    $lines = @(
        'MGS Co-op log collection',
        ('collected_local=' + [DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz')),
        ('collected_utc=' + [DateTime]::UtcNow.ToString('o')),
        ('role=' + $role + '; marker=' + ([string]$marker).Trim()),
        ('package_version=' + [string](Get-FriendProperty $package 'version' 'unknown') + '; installed_loader_version=' + [string](Get-FriendProperty $record 'version' 'none')),
        ('friend_install_role=' + [string](Get-FriendProperty $state 'role_hint' (Get-FriendProperty $state 'role' 'none')) + '; character=' + [string](Get-FriendProperty $state 'character' 'none') + '; status=' + $installStatus),
        ('play_session=' + [string](Get-FriendProperty $playSession 'state' 'none') + '; role=' + $playRole + '; port=' + [string](Get-FriendProperty $playSession 'port' '')),
        ('link_secret_present=' + (Test-Path -LiteralPath $paths.LinkSecret -PathType Leaf) + ' (never collected)'),
        ('game_directory=' + $gameRoot),
        ('game_running=' + $running),
        ('mgsvtpp.exe sha256=' + $exe + '; expected=' + ($exe -eq $expectExe.ToUpperInvariant())),
        ('steam_api64.dll sha256=' + $steamApi + '; expected=' + ($steamApi -eq $expectSteam.ToUpperInvariant())),
        ('dinput8.dll sha256=' + $loader + '; matches_install_record=' + ([bool]$loader -and $loader -eq [string](Get-FriendProperty $record 'dll_sha256' ''))),
        ('mgs-coop-player-runtime.dat sha256=' + $archive + '; expected=' + ($archive -eq $expectArchive.ToUpperInvariant())),
        ('real_saves=' + $saveCheck),
        ('coop_saves=files=' + $coopSaveFiles + '; import_marker=' + $importMarkerState + '; newest_replaced=' + $newestReplaced),
        ('windows=' + $os + '; powershell=' + $PSVersionTable.PSVersion.ToString()),
        ('included=' + ($included -join ',')),
        ('missing=' + ($missing -join ',')),
        ('limits=per log ' + $MaxLogBytes + ' bytes; SteamIDs masked except the last 4 digits; join codes and public peer addresses masked')
    )
    Save 'summary.txt' (($lines -join "`r`n") + "`r`n")
    New-FriendZip -SourceDirectory $content -Destination $zipPath -FolderName ("mgs-coop-logs-$role-$stamp")
} finally {
    if ((Test-Path -LiteralPath $staging) -and $staging.StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $staging -Recurse -Force }
}
$size = (Get-Item -LiteralPath $zipPath).Length
Write-Host ''
Write-Host "Saved: $zipPath ($([Math]::Round($size / 1024)) KB)" -ForegroundColor Green
Write-Host 'Send this ZIP file to the person who gave you the package. SteamIDs inside are masked'
Write-Host 'except their last 4 digits, join codes and public addresses are masked, and the link key is'
Write-Host 'not included. Co-op save folders are listed by name, size and time only; no save file is'
Write-Host 'included. You can open the ZIP to check what it contains.'
Write-Output $zipPath
exit 0
