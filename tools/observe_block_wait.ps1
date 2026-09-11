param(
    [Parameter(Mandatory)][ValidateRange(1,4294967295)][long]$TargetProcessId,
    [ValidateRange(2,30)][int]$DeadlineSeconds=10,
    [switch]$ObserveEntry
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$executable=[IO.Path]::GetFullPath('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
$cdb='C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe'
if (Get-Process cdb -ErrorAction SilentlyContinue) { throw 'Resolve the existing debugger first' }
if (!(Test-Path -LiteralPath "$projectRoot/build/native/stop_debugger.exe")) { throw 'Build the tested detach helper first' }
$target=Get-Process -Id $TargetProcessId
if ($target.Path -ne $executable) { throw 'Target is not the expected game' }
$targetStarted=$target.StartTime
$runRoot=Join-Path $projectRoot ('build/block-observation-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$commands=Join-Path $runRoot 'block.cdb'
$debugLog=Join-Path $runRoot 'debugger.log'
$generatorArguments=@("$PSScriptRoot/generate_block_wait_trace.py",'--executable',$executable,'--output',$commands)
if ($ObserveEntry) { $generatorArguments+='--entry' }
& C:/Python314/python.exe @generatorArguments
if ($LASTEXITCODE -ne 0) { throw 'File/code verification failed before attach' }
$current=Get-Process -Id $TargetProcessId
if ($current.Path -ne $executable -or $current.StartTime -ne $targetStarted) { throw 'Target changed before attach' }
$pipeName='MgsBlock'+[Guid]::NewGuid().ToString('N')
$pipePassword=[Guid]::NewGuid().ToString('N').Substring(0,12)
$info=[Diagnostics.ProcessStartInfo]::new($cdb)
$info.UseShellExecute=$false; $info.CreateNoWindow=$true
$info.RedirectStandardInput=$true; $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
foreach ($argument in @('-server',"npipe:pipe=$pipeName,hidden,password=$pipePassword",'-pd','-hd','-sins','-y',"$projectRoot/build/native",'-noshell','-nosqm','-cf',$commands,'-logo',$debugLog,'-p',[string]$TargetProcessId)) { $info.ArgumentList.Add($argument) }
$debugger=[Diagnostics.Process]::new(); $debugger.StartInfo=$info
if (!$debugger.Start()) { throw 'Debugger did not start' }
$stdout=$debugger.StandardOutput.ReadToEndAsync(); $stderr=$debugger.StandardError.ReadToEndAsync()
@{pipe_name=$pipeName;pipe_password=$pipePassword;debugger_pid=$debugger.Id;target_pid=$TargetProcessId;target_started_utc=$targetStarted.ToUniversalTime().ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $runRoot 'recovery.json')
$deadline=[DateTime]::UtcNow.AddSeconds(20)
$armedAt=$null
$deadlineDetached=$false
while (!$debugger.HasExited -and [DateTime]::UtcNow -lt $deadline) {
    if (!$armedAt -and (Test-Path -LiteralPath $debugLog)) {
        $progress=[string](Get-Content -LiteralPath $debugLog -Raw)
        if ($progress -match '(?m)^MGS_BLOCK_ARMED\r?$') {
            $armedAt=[DateTime]::UtcNow; $deadline=$armedAt.AddSeconds($DeadlineSeconds)
        }
    }
    Start-Sleep -Milliseconds 50
}
if (!$debugger.HasExited) {
    & "$PSScriptRoot/stop_debugger.ps1" -PipeName $pipeName -PipePassword $pipePassword -ExpectedTargetProcessId $TargetProcessId
    $deadlineDetached=$true
    $debugger.StandardInput.WriteLine('q'); $debugger.StandardInput.Flush()
    if (!$debugger.WaitForExit(5000)) { throw "Detach succeeded but debugger exit unconfirmed: $runRoot" }
}
$log=[string](Get-Content -LiteralPath $debugLog -Raw)
$entries=[regex]::Matches($log,'(?m)^MGS_BLOCK_CAPTURE mode=(entry|comparison) tid=\S+ image=\S+ site=\S+ block=\S+ state=\d+ total=\d+ baseline=\d+ baseline_offset=\S+ c60=\d+ c18=\d+ c40=\d+ c10=\d+\r?$')
$remaining=Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue
$record=[ordered]@{
    utc=[DateTime]::UtcNow.ToString('o'); target_pid=$TargetProcessId
    target_started_utc=$targetStarted.ToUniversalTime().ToString('o')
    executable_sha256=(Get-FileHash $executable -Algorithm SHA256).Hash
    armed=$log -match '(?m)^MGS_BLOCK_ARMED\r?$'; entry_count=$entries.Count
    entries=@($entries | ForEach-Object Value); aborted=$log -match '(?m)^MGS_ABORT '
    deadline_detached=$deadlineDetached; debugger_exit=$debugger.ExitCode; debug_log=$debugLog
    target_still_running=($null -ne $remaining -and $remaining.StartTime -eq $targetStarted)
    capture_valid=($entries.Count -eq 1 -and $debugger.ExitCode -eq 0 -and !$deadlineDetached -and $log -notmatch '(?m)^MGS_ABORT ' -and $log -match '(?m)^MGS_BLOCK_DETACH\r?$')
    mode=$(if ($ObserveEntry) {'entry'} else {'comparison'}); armed_observed_utc=$(if ($armedAt) {$armedAt.ToString('o')} else {$null})
    game_calls=$false; data_writes=$false; hardware_breakpoints=$true
    limitation='A bounded natural block observation; block identity and allocation owner are not established. Entry capture alone does not prove a memory wait.'
}
$record | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $runRoot 'result.json')
$record | ConvertTo-Json -Depth 4
$debugger.Dispose()
