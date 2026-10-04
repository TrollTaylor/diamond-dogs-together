#Requires -Version 7
<#
.SYNOPSIS
Starts fuse (always -standalone), the host instance and then the guest instance.

.DESCRIPTION
Preflight: no mgsvtpp.exe running from either lab folder, fuse/loopback ports
free, executable/Steam DLL/loader/archive hashes, lab.ini/coop.ini/markers
present and consistent. Then records log offsets (logs are append-only; the
smoke test reads only what this run appends) and a hash manifest of the real
saves, starts fuse with its working directory (database fuse.dat and log/)
in build/lab/fuse, starts the host, waits for its main window, starts the guest
and places the windows side by side. Run metadata: build/lab/runs/<ts>/run.json.

.EXAMPLE
pwsh -File tools/lab/Start-Lab.ps1 -WhatIf
pwsh -File tools/lab/Start-Lab.ps1
pwsh -File tools/lab/Start-Lab.ps1 -HostOnly      # milestone M0: host + fuse only
pwsh -File tools/lab/Start-Lab.ps1 -LayoutOnly    # re-apply window positions
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    [switch]$HostOnly,
    [switch]$LayoutOnly,
    [string]$RunDir
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force

$config = Get-LabConfig -ConfigPath $ConfigPath
$labRoot = $config.Paths.runDir
$dirs = [ordered]@{ host = $config.Paths.hostDir; guest = $config.Paths.guestDir }

function Set-LabLayout($Instances) {
    $resize = [bool](Get-ConfigValue $config.Window 'resize' $false)
    $width = if ($resize) { [int](Get-ConfigValue $config.Window 'width' 0) } else { 0 }
    $height = if ($resize) { [int](Get-ConfigValue $config.Window 'height' 0) } else { 0 }
    foreach ($name in $Instances.Keys) {
        $pid_ = [int]$Instances[$name].pid
        $handle = Get-ProcessMainWindow -Id $pid_
        if ($handle -eq [IntPtr]::Zero) { Write-Warning "[$name] no main window to place."; continue }
        $pos = Get-ConfigValue $config.Window $name $null
        if (!$pos) { continue }
        $ok = Move-GameWindow -Handle $handle -X ([int]$pos.x) -Y ([int]$pos.y) -Width $width -Height $height
        Write-Output "[$name] window placed at $($pos.x),$($pos.y): $ok"
    }
}

if ($LayoutOnly) {
    $current = Get-CurrentRun -Config $config -RunDir $RunDir
    $live = [ordered]@{}
    foreach ($name in $current.Run.instances.PSObject.Properties.Name) { $live[$name] = $current.Run.instances.$name }
    Set-LabLayout $live
    return
}

$roles = if ($HostOnly) { @('host') } else { @('host', 'guest') }
$expectedUrl = 'http://127.0.0.1:' + [int]$config.Ports.fuse + '/tppstm/gate'
$problems = [Collections.Generic.List[string]]::new()

# ---------------------------------------------------------------- preflight
foreach ($name in $dirs.Keys) {
    foreach ($row in @(Get-GameProcessInDirectory -Directory $dirs[$name])) {
        $problems.Add("[$name] mgsvtpp is already running from $($dirs[$name]) (pid $($row.Id)).")
    }
}
if (!(Test-LocalPortFree -Port ([int]$config.Ports.fuse) -Protocol Tcp)) { $problems.Add("TCP port $($config.Ports.fuse) (fuse) is in use.") }
foreach ($port in [int]$config.Ports.host, [int]$config.Ports.guest) {
    if (!(Test-LocalPortFree -Port $port -Protocol Udp)) { $problems.Add("UDP port $port (loopback) is in use.") }
}
if (!(Test-Path -LiteralPath $config.Paths.fuseExe -PathType Leaf)) { $problems.Add("fuse.exe not found: $($config.Paths.fuseExe) (run tools/lab/Build-Fuse.ps1).") }
if (!@(Get-Process -Name steam -ErrorAction SilentlyContinue).Count) { Write-Warning 'Steam is not running. Start Steam and log in first: the game calls SteamAPI_Init and exits without it.' }

$instances = [ordered]@{}
foreach ($name in $roles) {
    $p = Get-InstancePaths -GameDirectory $dirs[$name]
    $info = [ordered]@{ role = $name; dir = $p.Game; exe = $p.Exe }
    if (!(Test-FileHash -Path $p.Exe -Expected ([string]$config.Hashes.exe))) { $problems.Add("[$name] mgsvtpp.exe missing or hash differs.") }
    if (!(Test-FileHash -Path $p.SteamApi -Expected ([string]$config.Hashes.steamApi))) { $problems.Add("[$name] steam_api64.dll missing or hash differs.") }
    if (!(Test-FileHash -Path $p.Archive -Expected ([string]$config.Hashes.runtimeArchive))) { $problems.Add("[$name] runtime archive missing or hash differs.") }
    $record = if (Test-Path -LiteralPath $p.Record -PathType Leaf) { Get-Content -LiteralPath $p.Record -Raw | ConvertFrom-Json } else { $null }
    if (!$record -or [string](Get-ConfigValue $record 'game_directory' '') -ne $p.Game -or !(Test-FileHash -Path $p.Loader -Expected ([string](Get-ConfigValue $record 'dll_sha256' '0')))) {
        $problems.Add("[$name] loader/ownership record does not match this folder (run Prepare-LabInstance.ps1).")
    }
    $lab = if (Test-Path -LiteralPath $p.LabIni -PathType Leaf) { ConvertFrom-IniText ([IO.File]::ReadAllText($p.LabIni)) } else { $null }
    if (!$lab -or (Get-ConfigValue $lab 'enabled' '') -ne '1' -or (Get-ConfigValue $lab 'role' '') -ne $name) { $problems.Add("[$name] lab.ini missing or not enabled for role $name.") }
    $coop = if (Test-Path -LiteralPath $p.CoopIni -PathType Leaf) { ConvertFrom-IniText ([IO.File]::ReadAllText($p.CoopIni)) } else { $null }
    if (!$coop -or (Get-ConfigValue $coop 'master_url' '') -ne $expectedUrl) { $problems.Add("[$name] coop.ini master_url is not $expectedUrl.") }
    $marker = if (Test-Path -LiteralPath $p.Marker -PathType Leaf) { [IO.File]::ReadAllText($p.Marker) } else { '' }
    $wantMarker = if ($name -eq 'host') { "host-v3`n" } else { "client-v1`n" }
    if ($marker -cne $wantMarker) { $problems.Add("[$name] script-observe.enabled is not $($wantMarker.Trim()).") }
    if ($name -eq 'guest') {
        $clientHost = if (Test-Path -LiteralPath $p.ClientHost -PathType Leaf) { [IO.File]::ReadAllText($p.ClientHost).Trim() } else { '' }
        if (!(Test-IndividualSteamId64 $clientHost) -or ($lab -and (Get-ConfigValue $lab 'peer_steamid' '') -ne $clientHost)) {
            $problems.Add('[guest] client-host.txt is not a valid SteamID64 or differs from lab.ini peer_steamid.')
        }
    }
    $instances[$name] = [pscustomobject]@{ Paths = $p; Info = $info }
}
if ($problems.Count) {
    $problems | ForEach-Object { Write-Warning $_ }
    throw "Preflight failed ($($problems.Count) problem(s)); nothing was started."
}
Write-Output 'Preflight passed.'
foreach ($name in $roles) {
    $p = $instances[$name].Paths
    foreach ($trigger in $p.JoinTrigger, $p.ResetTrigger) {
        if ((Test-Path -LiteralPath $trigger) -and $PSCmdlet.ShouldProcess($trigger, 'Remove leftover trigger')) { Remove-Item -LiteralPath $trigger -Force }
    }
}
if (!$PSCmdlet.ShouldProcess(($roles -join ' + '), 'Start fuse -standalone and the game instance(s)')) { return }

# ---------------------------------------------------------------- run folder and baselines
$stamp = New-RunStamp
$runPath = Join-Path $labRoot ('runs/' + $stamp)
$null = New-Item -ItemType Directory -Path $runPath -Force
$run = [ordered]@{
    product = 'mgs-coop-lab-run'; schema = 1; stamp = $stamp; run_dir = $runPath
    started_utc = [DateTime]::UtcNow.ToString('o'); host_only = [bool]$HostOnly
    config_path = $config.Path; repo = $config.Paths.repo; version = (Get-ProjectVersion -RepoRoot $config.Paths.repo)
    ports = [ordered]@{ fuse = [int]$config.Ports.fuse; host = [int]$config.Ports.host; guest = [int]$config.Ports.guest }
    fuse = $null; instances = [ordered]@{}; real_saves_before = $null; latest_save_backup = $null
    notes = 'SteamIDs are intentionally not recorded here.'
}
foreach ($name in $roles) {
    $p = $instances[$name].Paths
    $offsets = [ordered]@{}
    foreach ($log in Get-LabLogNames) { $offsets[$log] = Get-FileLengthOrZero (Join-Path $p.Mod $log) }
    $info = $instances[$name].Info
    $info.exe_sha256 = Get-Sha256 $p.Exe
    $info.steam_api_sha256 = Get-Sha256 $p.SteamApi
    $info.loader_sha256 = Get-Sha256 $p.Loader
    $info.archive_sha256 = Get-Sha256 $p.Archive
    $info.lab_ini_sha256 = Get-Sha256 $p.LabIni
    $info.log_offsets = $offsets
    $run.instances[$name] = $info
}
try {
    $identity = Get-HostIdentity -OverrideSteamId ([string](Get-ConfigValue $config.Identity 'hostSteamId' 'auto')) -SteamPath $config.Paths.steamRoot
    Write-JsonFile -Path (Join-Path $runPath 'real-saves-before.json') -Value ([ordered]@{ source_root = $identity.UserData; files = (Get-RealSaveManifest -UserData $identity.UserData) })
    $run.real_saves_before = 'real-saves-before.json'
} catch { Write-Warning "Could not hash the real saves before the run: $($_.Exception.Message)" }
$pointer = Join-Path $labRoot 'latest-save-backup.json'
if (Test-Path -LiteralPath $pointer -PathType Leaf) { $run.latest_save_backup = (Get-Content -LiteralPath $pointer -Raw | ConvertFrom-Json).backup_dir }
Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
Write-JsonFile -Path (Join-Path $labRoot 'current-run.json') -Value ([ordered]@{ run_dir = $runPath; stamp = $stamp })

# ---------------------------------------------------------------- fuse
$fuseDir = Join-Path $labRoot 'fuse'
$null = New-Item -ItemType Directory -Path $fuseDir -Force
$fuseArgs = New-FuseArguments -Port ([int]$config.Ports.fuse) -WriteLog ([bool](Get-ConfigValue $config.Fuse 'writeLog' $true))
Assert-FuseArgumentsSafe -Arguments $fuseArgs
$sinceUnix = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() - 1
$fuse = Invoke-WithoutStdHandleInheritance {
    Start-Process -FilePath $config.Paths.fuseExe -ArgumentList $fuseArgs -WorkingDirectory $fuseDir -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $runPath 'fuse-stdout.log') -RedirectStandardError (Join-Path $runPath 'fuse-stderr.log')
}
$run.fuse = [ordered]@{
    exe = $config.Paths.fuseExe; sha256 = (Get-Sha256 $config.Paths.fuseExe); pid = $fuse.Id; cwd = $fuseDir; args = $fuseArgs
    database = (Join-Path $fuseDir 'fuse.dat'); log_root = (Join-Path $fuseDir 'log'); since_unix = $sinceUnix
    stdout = 'fuse-stdout.log'; stderr = 'fuse-stderr.log'; url = $expectedUrl
}
Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
$deadline = [DateTime]::UtcNow.AddSeconds([int](Get-ConfigValue $config.Fuse 'startupTimeoutSec' 20))
while (!(Test-TcpListening -Port ([int]$config.Ports.fuse))) {
    if ($fuse.HasExited) { throw "fuse exited during startup (code $($fuse.ExitCode)); see $runPath\fuse-stdout.log" }
    if ([DateTime]::UtcNow -gt $deadline) { Stop-Process -Id $fuse.Id -Force -ErrorAction SilentlyContinue; throw 'fuse did not start listening in time; stopped it.' }
    Start-Sleep -Milliseconds 250
}
Write-Output "fuse listening on 127.0.0.1:$($config.Ports.fuse) (pid $($fuse.Id), -standalone, cwd $fuseDir)."

# ---------------------------------------------------------------- games
$windowTimeout = [int](Get-ConfigValue $config.Smoke 'windowTimeoutSec' 180)
foreach ($name in $roles) {
    $p = $instances[$name].Paths
    $process = Invoke-WithoutStdHandleInheritance { Start-Process -FilePath $p.Exe -WorkingDirectory $p.Game -PassThru }
    $run.instances[$name].pid = $process.Id
    $run.instances[$name].started_utc = [DateTime]::UtcNow.ToString('o')
    Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
    Write-Output "[$name] started pid $($process.Id); waiting for its main window..."
    $deadline = [DateTime]::UtcNow.AddSeconds($windowTimeout)
    $handle = [IntPtr]::Zero
    while ($handle -eq [IntPtr]::Zero) {
        if ($process.HasExited) { throw "[$name] exited before showing a window (code $($process.ExitCode)). Check $($p.Mod)\early.log and probe.log; the run folder is $runPath." }
        if ([DateTime]::UtcNow -gt $deadline) { throw "[$name] no main window after $windowTimeout s; the process is left running (Stop-Lab.ps1 closes it)." }
        Start-Sleep -Milliseconds 500
        $handle = Get-ProcessMainWindow -Id $process.Id
    }
    $run.instances[$name].window_seen_utc = [DateTime]::UtcNow.ToString('o')
    Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
    Write-Output "[$name] main window is up."
}
Set-LabLayout $run.instances
Write-JsonFile -Path (Join-Path $runPath 'run.json') -Value $run
Write-Output "Run folder: $runPath"
Write-Output 'Next: pwsh -File tools/lab/Invoke-LabSmoke.ps1 -Assist   (Stop-Lab.ps1 closes everything)'
