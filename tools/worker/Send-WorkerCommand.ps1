#Requires -Version 7
<#
.SYNOPSIS
Writes one bounded command for an admitted private MGSV worker.
.DESCRIPTION
Uses owned files only. No focus, Windows messages or system input. A queued
command is not proof of gameplay; inspect worker receipts and captured frames.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunDirectory,
    [Parameter(Mandatory)][ValidateSet('host','guest')][string]$Role,
    [Parameter(Mandatory)][ValidateSet('press','hold','release','mouse','capture','debug','resume_loading')][string]$Operation,
    [string]$Keys='enter',
    [ValidateRange(1,1500)][int]$DurationMs=100,
    [ValidateRange(-1500,1500)][int]$Dx=0,
    [ValidateRange(-1500,1500)][int]$Dy=0,
    [string]$DebugCommand
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$run=[IO.Path]::GetFullPath($RunDirectory,$repo)
$root=Join-Path $repo 'build\worker-runtime'
if(!$run.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Worker run directory must be below build\worker-runtime.'}
$cursor=$run
while($cursor) {
    if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Worker command refuses filesystem links.'}
    $cursor=[IO.Path]::GetDirectoryName($cursor)
}
$statusPath=Join-Path $run 'supervisor.json'
$status=Get-Content -LiteralPath $statusPath -Raw|ConvertFrom-Json
if($status.mode -cne 'mgsv' -or $status.run_id -cnotmatch '^[0-9a-f]{32}$' -or !$status.ready -or $status.stage -cne 'running' -or
    $status.desktop_ownership_violation -or !$status.visible_desktop_unchanged_at_observations -or
    ([DateTime]::UtcNow-(Get-Item -LiteralPath $statusPath).LastWriteTimeUtc).TotalSeconds -gt 3){throw 'Worker has no fresh, admitted private-desktop supervisor.'}
$supervisor=Get-Process -Id ([int]$status.supervisor_pid) -ErrorAction Stop
if([string]$supervisor.StartTime.ToUniversalTime().ToFileTimeUtc() -cne [string]$status.supervisor_start_filetime -or
   (Get-FileHash -LiteralPath $supervisor.Path -Algorithm SHA256).Hash -cne [string]$status.supervisor_sha256){throw 'Supervisor process lease changed.'}
$owned=@($status.owned_processes|Where-Object{$_.role -ceq $Role})
if($owned.Count -ne 1 -or !$owned[0].admitted){throw 'Role has no owned admitted game.'}
$game=Get-Process -Id ([int]$owned[0].pid) -ErrorAction Stop
if([string]$game.StartTime.ToUniversalTime().ToFileTimeUtc() -cne [string]$owned[0].creation_filetime -or
   ![IO.Path]::GetFullPath($game.Path).Equals([string]$owned[0].exe,[StringComparison]::OrdinalIgnoreCase)){throw 'Game process lease changed.'}
$context=([IO.File]::ReadAllText((Join-Path $run ($Role+'.context')))).TrimEnd([char]10).Split(' ')
if($context.Count -ne 7 -or $context[0] -cne 'MGSWORKERCTX1' -or $context[1] -cne $status.run_id -or $context[2] -cne $Role -or
   $context[3] -cne [string]$game.Id -or $context[4] -cne [string]$owned[0].creation_filetime -or $context[5] -cne $status.desktop -or
   $context[6] -cne 'admitted'){throw 'Native role context does not match its supervisor lease.'}
if($Operation -in @('press','hold')) {
    $known=@('w','a','s','d','enter','space','escape','e','r','shift','ctrl','1','2','f','c','q','x','z','tab','lmb','rmb')
    $tokens=$Keys.Split('+')
    if(!$tokens.Count -or @($tokens|Where-Object{$known -cnotcontains $_}).Count -or @($tokens|Sort-Object -Unique).Count -ne $tokens.Count){throw 'Unknown or duplicated typed worker key.'}
}
if($Operation -eq 'mouse' -and !$Dx -and !$Dy){throw 'Mouse command needs a bounded nonzero delta.'}
if($Operation -eq 'debug' -and (!$DebugCommand -or $DebugCommand.Length -gt 200 -or $DebugCommand -cmatch '[^\x20-\x7e]' -or $DebugCommand -cne $DebugCommand.Trim())){throw 'Debug command must be a bounded single ASCII line; the existing gameplay queue validates its grammar.'}
$state=$null
if($Operation -eq 'debug') {
    $statePath=Join-Path $run ($Role+'.state.json')
    $state=Get-Content -LiteralPath $statePath -Raw|ConvertFrom-Json
    if($state.run_id -cne $status.run_id -or $state.role -cne $Role -or $state.pid -ne $game.Id -or
       [string]$state.process_start_filetime -cne [string]$owned[0].creation_filetime -or !$state.gameplay_ready -or
       $state.terminating -or !$state.mission -or !$state.generation -or [uint64]$state.tick_ms -gt [Environment]::TickCount64 -or
       [Environment]::TickCount64-[uint64]$state.tick_ms -gt 2000){throw 'Debug actions require a fresh owned gameplay mission generation.'}
}
$loading=$null
if($Operation -eq 'resume_loading') {
    $loadingPath=Join-Path $run ($Role+'.loading.json')
    if((Get-Item -LiteralPath $loadingPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Loading context must be an owned plain file.'}
    $loading=Get-Content -LiteralPath $loadingPath -Raw|ConvertFrom-Json
    if($loading.run_id -cne $status.run_id -or $loading.role -cne $Role -or $loading.pid -ne $game.Id -or
       [string]$loading.process_start_filetime -cne [string]$owned[0].creation_filetime -or $loading.phase -cne 'initialized' -or
       $loading.mission -notin 30010,30020,30050,40010,40020,40050 -or !$loading.generation -or
       [uint64]$loading.tick_ms -gt [Environment]::TickCount64 -or
       [Environment]::TickCount64-[uint64]$loading.tick_ms -gt 400000){throw 'Resume requires the current owned initialized loading token.'}
}
$mutex=[Threading.Mutex]::new($false,('Local\MGSWorkerCommand_'+$status.run_id+'_'+$Role));$locked=$false
try {
    $locked=$mutex.WaitOne(5000);if(!$locked){throw 'Worker role command writer is busy.'}
    $counterPath=Join-Path $run ($Role+'.controller-sequence')
    if((Test-Path -LiteralPath $counterPath) -and ((Get-Item -LiteralPath $counterPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Worker sequence file must be an owned plain file.'}
    $previous=if(Test-Path -LiteralPath $counterPath){[uint64]([IO.File]::ReadAllText($counterPath))}else{[uint64]0}
    if($previous -ge 4096){throw 'Worker command budget exhausted; start a new bounded run.'}
    $sequence=$previous+1;$expires=[Environment]::TickCount64+$(if($Operation -eq 'debug'){15000}else{2000})
    $suffix=if($Operation -eq 'debug'){'debug'}else{'input'}
    $text=if($Operation -eq 'debug'){"MGSWORKERDEBUG1 $($status.run_id) $Role $sequence $expires $($state.mission) $($state.generation) $DebugCommand`n"}
        elseif($Operation -in @('press','hold')){"MGSWORKER1 $($status.run_id) $Role $sequence $expires $Operation $Keys $DurationMs 0`n"}
        elseif($Operation -eq 'resume_loading'){"MGSWORKER1 $($status.run_id) $Role $sequence $expires resume_loading $($loading.mission) $($loading.generation) 0`n"}
        elseif($Operation -eq 'mouse'){"MGSWORKER1 $($status.run_id) $Role $sequence $expires mouse $Dx $Dy 0`n"}
        else{"MGSWORKER1 $($status.run_id) $Role $sequence $expires $Operation 0 0 0`n"}
    $path=Join-Path $run ($Role+'.'+$suffix);$temp=$path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    [IO.File]::WriteAllText($temp,$text,[Text.UTF8Encoding]::new($false))
    # Bounded retry: sync clients (OneDrive) and scanners briefly hold the command file.
    for($attempt=0;;$attempt++){try{[IO.File]::Move($temp,$path,$true);break}catch [UnauthorizedAccessException],[IO.IOException]{if($attempt -ge 49){Remove-Item -LiteralPath $temp -ErrorAction SilentlyContinue;throw};Start-Sleep -Milliseconds 10}}
    [IO.File]::WriteAllText($counterPath,[string]$sequence,[Text.UTF8Encoding]::new($false))
    [pscustomobject]@{run_id=$status.run_id;role=$Role;sequence=$sequence;operation=$Operation;expires_tick_ms=$expires;path=$path;gameplay_validated=$false}
} finally {if($locked){$mutex.ReleaseMutex()};$mutex.Dispose()}
