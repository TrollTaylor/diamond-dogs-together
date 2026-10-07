#Requires -Version 7
<#
.SYNOPSIS
Qualifies the pinned private-desktop DX11 worker without launching MGSV.
.DESCRIPTION
Runs only the original GUI supervisor and its pinned DX11 probe. No desktop
switch, focus, system input or game installation. Every attempt has a new run
directory and a bounded lifetime. Qualification is separate from gameplay.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [ValidateRange(10,120)][int]$TimeoutSec=45,
    [switch]$PassThru,
    [switch]$PlanOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$build=[IO.Path]::GetFullPath($BuildDirectory,$repo)
$root=Join-Path $repo 'build'
if(!$build.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Worker binaries must be in the repository build directory.'}
foreach($path in @($build,(Join-Path $build 'worker_supervisor.exe'),(Join-Path $build 'worker_probe.exe'))) {
    $cursor=$path
    while($cursor) {
        if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Worker refuses filesystem links: $cursor"}
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
}
$supervisor=Join-Path $build 'worker_supervisor.exe'
$probe=Join-Path $build 'worker_probe.exe'
foreach($exe in @($supervisor,$probe)) {
    if(!(Test-Path -LiteralPath $exe -PathType Leaf)){throw "Build the worker first: $exe"}
    $bytes=[IO.File]::ReadAllBytes($exe)
    if($bytes.Length -lt 256 -or [BitConverter]::ToUInt16($bytes,0) -ne 0x5A4D){throw 'Invalid worker PE.'}
    $offset=[BitConverter]::ToInt32($bytes,60)
    if($offset -lt 64 -or $offset+94 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes,$offset) -ne 0x4550 -or
        [BitConverter]::ToUInt16($bytes,$offset+4) -ne 0x8664 -or [BitConverter]::ToUInt16($bytes,$offset+92) -ne 2){throw 'Worker must be x64 with the GUI subsystem.'}
}
$runId=[Guid]::NewGuid().ToString('N')
$runDir=Join-Path $root ('worker-runtime\'+$runId)
$cursor=$runDir
while($cursor) {
    if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Worker probe output must remain in plain repository directories.'}
    $cursor=[IO.Path]::GetDirectoryName($cursor)
}
$plan=[ordered]@{schema='1';mode='probe';run_id=$runId;run_dir=$runDir;desktop='MGSWorker_'+$runId;
    timeout_ms=[string]($TimeoutSec*1000);ready_timeout_ms=[string](($TimeoutSec-5)*1000);
    probe_exe=$probe;probe_sha256=(Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash}
if($PlanOnly){$plan|ConvertTo-Json;return}
$null=New-Item -ItemType Directory -Path $runDir
$manifest=Join-Path $runDir 'launch.manifest'
[IO.File]::WriteAllText($manifest,(($plan.Keys|ForEach-Object{$_+'='+$plan[$_]})-join "`n")+"`n",[Text.UTF8Encoding]::new($false))
$receipt=[ordered]@{schema=1;run_id=$runId;run_dir=$runDir;supervisor=$supervisor;
    supervisor_sha256=(Get-FileHash -LiteralPath $supervisor -Algorithm SHA256).Hash;
    probe_sha256=$plan.probe_sha256;started_utc=[DateTime]::UtcNow.ToString('o');pid=$null;status='starting';failure=$null}
$psi=[Diagnostics.ProcessStartInfo]::new()
$psi.FileName=$supervisor;$psi.WorkingDirectory=$build;$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true
$psi.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
$psi.ArgumentList.Add('--launch');$psi.ArgumentList.Add($manifest)
$process=[Diagnostics.Process]::new();$process.StartInfo=$psi
try {
    if(!$process.Start()){throw 'Worker supervisor did not start.'}
    $receipt.pid=$process.Id;$receipt.process_started_utc=$process.StartTime.ToUniversalTime().ToString('o')
    if(!$process.WaitForExit(($TimeoutSec+5)*1000)){
        # Closing this exact supervisor also closes its KILL_ON_JOB_CLOSE handle.
        $process.Kill();$null=$process.WaitForExit(5000);throw 'Worker qualification timed out.'
    }
    $receipt.exit_code=$process.ExitCode
    $statusPath=Join-Path $runDir 'supervisor.json'
    $probePath=Join-Path $runDir 'probe.json'
    if(!(Test-Path -LiteralPath $statusPath) -or !(Test-Path -LiteralPath $probePath)){throw "Worker refused before qualification. Inspect $runDir"}
    $status=Get-Content -LiteralPath $statusPath -Raw|ConvertFrom-Json
    $observed=Get-Content -LiteralPath $probePath -Raw|ConvertFrom-Json
    $owned=@($status.owned_processes)
    $imagePath=Join-Path $runDir 'probe-backbuffer.bmp'
    $qualificationPath=Join-Path $runDir 'qualification.txt'
    if($status.run_id -cne $runId -or $status.desktop -cne $plan.desktop -or $status.supervisor_pid -ne $process.Id -or
       [string]$status.supervisor_start_filetime -cne [string]$process.StartTime.ToUniversalTime().ToFileTimeUtc() -or
       $status.supervisor_sha256 -cne $receipt.supervisor_sha256 -or $owned.Count -ne 1 -or $owned[0].role -cne 'probe' -or
       $owned[0].sha256 -cne $plan.probe_sha256 -or $owned[0].exit_code -ne 0 -or !$owned[0].admitted -or
       $observed.run_id -cne $runId -or $observed.desktop -cne $plan.desktop -or $observed.pid -ne $owned[0].pid -or
       [string]$observed.process_start_filetime -cne [string]$owned[0].creation_filetime -or
       !(Test-Path -LiteralPath $imagePath) -or !(Test-Path -LiteralPath $qualificationPath)){throw 'Worker qualification ownership receipt mismatch.'}
    $imageHash=(Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash
    $seal=([IO.File]::ReadAllText($qualificationPath)).TrimEnd([char]10).Split(' ')
    if($imageHash -cne $observed.bitmap_sha256 -or $seal.Count -ne 7 -or $seal[0] -cne 'MGSWORKERQUAL1' -or
       $seal[1] -cne $runId -or $seal[2] -cne $receipt.supervisor_sha256 -or $seal[3] -cne $plan.probe_sha256 -or
       $seal[4] -cne $imageHash -or $seal[6] -cne 'qualified' -or [uint64]$seal[5] -gt [Environment]::TickCount64 -or
       [Environment]::TickCount64-[uint64]$seal[5] -gt 120000){throw 'Worker qualification content/hash receipt mismatch.'}
    if($process.ExitCode -ne 0 -or $status.stage -cne 'completed' -or !$status.probe_ready -or $status.desktop_ownership_violation -or
       !$status.visible_desktop_unchanged_at_observations -or !$observed.ready -or !$observed.hardware_device -or !$observed.pixel_readback_verified -or
       !$observed.desktop_creation_denied -or !$observed.input_desktop_unchanged_at_observations -or !(Test-Path -LiteralPath (Join-Path $runDir 'qualification.txt'))){
        throw "Private worker qualification failed: $($status.reason); probe=$($observed.reason)"
    }
    $receipt.status='qualified';$receipt.gameplay_validated=$false
    Write-Host "Private desktop hardware probe qualified: $runDir"
    if($PassThru){[pscustomobject]@{RunDirectory=$runDir;QualificationPath=$qualificationPath;SupervisorSha256=$receipt.supervisor_sha256;ProbeSha256=$plan.probe_sha256}}
} catch {$receipt.status='failed';$receipt.failure=$_.Exception.Message;throw}
finally {
    if($receipt.pid -and !$process.HasExited){$process.Kill();$null=$process.WaitForExit(5000)}
    $receipt.finished_utc=[DateTime]::UtcNow.ToString('o')
    [IO.File]::WriteAllText((Join-Path $runDir 'controller.json'),($receipt|ConvertTo-Json -Depth 5)+"`n",[Text.UTF8Encoding]::new($false))
    $process.Dispose()
}
