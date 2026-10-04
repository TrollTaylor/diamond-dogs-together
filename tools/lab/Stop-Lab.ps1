#Requires -Version 7
<#
.SYNOPSIS
Closes both lab games and fuse, then verifies the real save files' hashes.

.DESCRIPTION
Posts WM_CLOSE to each game's main window, waits up to -TimeoutSec, then
terminates what is still running (only processes whose executable is in the
lab folders, or the run's fuse.exe). Afterwards it hashes the real
Steam\userdata\<id>\287700\local and 311340\remote files and compares them with
the manifest taken by Start-Lab.ps1 at run start and with the latest backup
taken by Prepare-LabInstance.ps1. Writes stop.json into the run folder.
Exit code 0 = real saves unchanged, 2 = a real save file changed (restore it
from build/lab/save-backups/<ts>/files/), 1 = error.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    [string]$RunDir,
    [int]$TimeoutSec = 60,
    [switch]$KeepFuse
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force

$config = Get-LabConfig -ConfigPath $ConfigPath
$current = $null
try { $current = Get-CurrentRun -Config $config -RunDir $RunDir } catch { Write-Warning "$($_.Exception.Message) Falling back to processes found in the lab folders." }

# ---------------------------------------------------------------- targets
$targets = [Collections.Generic.List[object]]::new()
if ($current) {
    foreach ($name in $current.Run.instances.PSObject.Properties.Name) {
        $i = $current.Run.instances.$name
        $p = Get-LiveProcess -Id ([int](Get-ConfigValue $i 'pid' 0)) -ExpectedPath ([string]$i.exe)
        if ($p) { $targets.Add([pscustomobject]@{ Role = $name; Process = $p }) }
    }
}
foreach ($pair in @(@('host', $config.Paths.hostDir), @('guest', $config.Paths.guestDir))) {
    foreach ($row in @(Get-GameProcessInDirectory -Directory $pair[1])) {
        if ($row.Unknown) { continue }
        if (@($targets | Where-Object { $_.Process.Id -eq $row.Id }).Count) { continue }
        $p = Get-Process -Id $row.Id -ErrorAction SilentlyContinue
        if ($p) { $targets.Add([pscustomobject]@{ Role = $pair[0]; Process = $p }) }
    }
}

# ---------------------------------------------------------------- close games
$outcome = [ordered]@{}
foreach ($t in $targets) {
    $handle = Get-ProcessMainWindow -Id $t.Process.Id
    $posted = $false
    if ($PSCmdlet.ShouldProcess("$($t.Role) pid $($t.Process.Id)", 'Send WM_CLOSE')) { $posted = Close-GameWindow -Handle $handle }
    $outcome[$t.Role] = [ordered]@{ pid = $t.Process.Id; wm_close_posted = [bool]$posted; exit = 'running' }
    Write-Output "[$($t.Role)] WM_CLOSE posted: $posted"
}
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
while ($targets.Count -and [DateTime]::UtcNow -lt $deadline -and @($targets | Where-Object { !$_.Process.HasExited }).Count) { Start-Sleep -Milliseconds 500 }
foreach ($t in $targets) {
    if ($t.Process.HasExited) { $outcome[$t.Role].exit = 'closed'; continue }
    if ($PSCmdlet.ShouldProcess("$($t.Role) pid $($t.Process.Id)", "Terminate after $TimeoutSec s")) {
        Stop-Process -Id $t.Process.Id -Force -ErrorAction SilentlyContinue
        $outcome[$t.Role].exit = 'terminated'
        Write-Warning "[$($t.Role)] did not close within $TimeoutSec s; terminated. Its lab save copy may be incomplete (the real saves are separate)."
    }
}

# ---------------------------------------------------------------- fuse
$fuseState = 'not_running'
if (!$KeepFuse) {
    $fuseIds = @()
    $fuseRun = if ($current) { Get-ConfigValue $current.Run 'fuse' $null } else { $null }
    if ($fuseRun) { $fuseIds += [int](Get-ConfigValue $fuseRun 'pid' 0) }
    foreach ($p in @(Get-Process -Name fuse -ErrorAction SilentlyContinue)) { $fuseIds += $p.Id }
    foreach ($id in ($fuseIds | Select-Object -Unique)) {
        $p = Get-LiveProcess -Id $id -ExpectedPath $config.Paths.fuseExe
        if (!$p) { continue }
        if ($PSCmdlet.ShouldProcess("fuse pid $id", 'Stop')) { Stop-Process -Id $id -Force; $fuseState = 'stopped'; Write-Output "fuse (pid $id) stopped." }
    }
} else { $fuseState = 'kept' }

# ---------------------------------------------------------------- real saves
$saves = [ordered]@{ checked = $false; unchanged_since_run_start = $null; unchanged_since_backup = $null; details = @() }
$beforeFile = if ($current) { [string](Get-ConfigValue $current.Run 'real_saves_before' '') } else { '' }
if ($beforeFile) {
    $before = Get-Content -LiteralPath (Join-Path $current.Dir $beforeFile) -Raw | ConvertFrom-Json
    $comparison = Compare-HashManifest -Expected @($before.files) -Actual (Get-RealSaveManifest -UserData ([string]$before.source_root))
    $saves.checked = $true
    $saves.unchanged_since_run_start = $comparison.Same
    $saves.details += [ordered]@{ against = 'run_start'; changed = $comparison.Changed; missing = $comparison.Missing; added = $comparison.Added }
}
$backupDir = if ($current -and (Get-ConfigValue $current.Run 'latest_save_backup' $null)) { [string]$current.Run.latest_save_backup } else {
    $pointer = Join-Path $config.Paths.runDir 'latest-save-backup.json'
    if (Test-Path -LiteralPath $pointer -PathType Leaf) { [string](Get-Content -LiteralPath $pointer -Raw | ConvertFrom-Json).backup_dir } else { $null }
}
if ($backupDir -and (Test-Path -LiteralPath (Join-Path $backupDir 'manifest.json'))) {
    $check = Test-RealSavesAgainstBackup -BackupDirectory $backupDir
    $saves.checked = $true
    $saves.unchanged_since_backup = $check.Comparison.Same
    $saves.details += [ordered]@{ against = $backupDir; changed = $check.Comparison.Changed; missing = $check.Comparison.Missing; added = $check.Comparison.Added }
}
foreach ($d in $saves.details) {
    $same = (@($d.changed).Count + @($d.missing).Count + @($d.added).Count) -eq 0
    Write-Output ("Real saves vs {0}: {1}" -f $d.against, $(if ($same) { 'UNCHANGED' } else { 'CHANGED' }))
    foreach ($kind in 'changed', 'missing', 'added') { foreach ($f in @($d[$kind])) { Write-Warning "  $kind`: $f" } }
}
if (!$saves.checked) { Write-Warning 'No save manifest to compare against (run Prepare-LabInstance.ps1 / Start-Lab.ps1 first).' }

$stop = [ordered]@{
    product = 'mgs-coop-lab-stop'; finished_utc = [DateTime]::UtcNow.ToString('o')
    games = $outcome; fuse = $fuseState; real_saves = $saves
}
if ($current) { Write-JsonFile -Path (Join-Path $current.Dir 'stop.json') -Value $stop }
# The run-start manifest is authoritative (normal play between Prepare and Start
# legitimately changes the real saves); the backup comparison is the fallback.
$changed = if ($null -ne $saves.unchanged_since_run_start) { !$saves.unchanged_since_run_start }
elseif ($null -ne $saves.unchanged_since_backup) { !$saves.unchanged_since_backup } else { $false }
if ($changed) {
    Write-Warning 'A real save file changed. The lab never writes there; check whether the save redirect was active (early.log saves=redirected) and restore from the backup if needed.'
    exit 2
}
exit 0
