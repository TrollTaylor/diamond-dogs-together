param(
    [ValidateRange(1,4294967295)][long]$TargetProcessId,
    [ValidateRange(2,30)][int]$DeadlineSeconds=10,
    [switch]$Fixture,
    [switch]$WatchPublished,
    [switch]$SkipBankCopy,
    [switch]$CaptureOwner,
    [ValidateSet('Local','Indexed','WrongBytes','NoHit','CopyLimit')][string]$Scenario='Local'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$runRoot=Join-Path $projectRoot ('build/vector-observation-'+[Guid]::NewGuid().ToString('N'))
$cdb='C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe'
if (!(Test-Path -LiteralPath $cdb) -or !(Test-Path -LiteralPath "$projectRoot/build/native/stop_debugger.exe")) { throw 'Build the observer helpers and install CDB first' }
if ($Fixture -and $TargetProcessId) { throw 'Fixture mode cannot attach to a supplied process' }
if ($SkipBankCopy -and !$WatchPublished) { throw 'Copy filtering requires the published write watch' }
if ($CaptureOwner -and ($Fixture -or !$SkipBankCopy)) { throw 'Owner capture requires the filtered game observer' }
if ($Scenario -eq 'CopyLimit' -and (!$Fixture -or !$SkipBankCopy)) { throw 'CopyLimit requires a filtered fixture' }
if (!$Fixture -and (!$TargetProcessId -or $Scenario -ne 'Local')) { throw 'Game mode requires a target PID and no fixture scenario' }
New-Item -ItemType Directory -Path $runRoot | Out-Null
$commands=Join-Path $runRoot 'vector.cdb'
$debugLog=Join-Path $runRoot 'debugger.log'
$gate=Join-Path $runRoot 'fixture-go'
$fixtureResult=Join-Path $runRoot 'fixture-result.json'
$fixtureProcess=$null
$debugger=$null
$pipeName='MgsVector'+[Guid]::NewGuid().ToString('N')
$pipePassword=[Guid]::NewGuid().ToString('N').Substring(0,12)
function Start-ObserverProcess([string]$File,[string[]]$Arguments) {
    $info=[Diagnostics.ProcessStartInfo]::new($File)
    $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.RedirectStandardInput=$true; $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process=[Diagnostics.Process]::new(); $process.StartInfo=$info
    if (!$process.Start()) { throw 'Process start failed' }
    return $process
}
try {
    if ($Fixture) {
        $executable=Join-Path $projectRoot 'build/native/player_vector_fixture.exe'
    } else {
        $target=Get-Process -Id $TargetProcessId
        $executable=[IO.Path]::GetFullPath('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
        if ($target.Path -ne $executable) { throw 'Target path does not match MGSV' }
        $targetStarted=$target.StartTime
        if (Get-Process cdb -ErrorAction SilentlyContinue) { throw 'An existing debugger must be resolved before attaching another observer' }
    }
    $arguments=@("$PSScriptRoot/generate_player_vector_trace.py",'--executable',$executable,'--output',$commands)
    if ($Fixture) { $arguments+='--fixture' }
    if ($WatchPublished) { $arguments+='--watch-published' }
    if ($SkipBankCopy) { $arguments+='--skip-bank-copy' }
    if ($CaptureOwner) { $arguments+='--capture-owner' }
    if ($Scenario -eq 'WrongBytes') { $arguments+='--wrong-bytes' }
    python @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Identity verification or command generation failed; no debugger attached' }
    if ($Fixture) {
        $fixtureProcess=Start-ObserverProcess $executable @($gate,$fixtureResult,$Scenario,$Scenario,$(if($SkipBankCopy){'SkipCopy'}else{'NoSkip'}))
        $TargetProcessId=$fixtureProcess.Id
        $targetStarted=$fixtureProcess.StartTime
    }
    $current=Get-Process -Id $TargetProcessId
    if ($current.Path -ne $executable -or $current.StartTime -ne $targetStarted) {
        throw "Target identity changed before attach: expected path=$executable start=$($targetStarted.ToUniversalTime().ToString('o')); observed path=$($current.Path) start=$($current.StartTime.ToUniversalTime().ToString('o'))"
    }
    $debugger=Start-ObserverProcess $cdb @('-server',"npipe:pipe=$pipeName,hidden,password=$pipePassword",'-pd','-hd','-sins','-y',"$projectRoot/build/native",'-noshell','-nosqm','-cf',$commands,'-logo',$debugLog,'-p',[string]$TargetProcessId)
    $stdout=$debugger.StandardOutput.ReadToEndAsync()
    $stderr=$debugger.StandardError.ReadToEndAsync()
    @{pipe_name=$pipeName;pipe_password=$pipePassword;debugger_pid=$debugger.Id;target_pid=$TargetProcessId;target_started_utc=$targetStarted.ToUniversalTime().ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $runRoot 'recovery.json')
    Write-Output "Bounded vector observation: $runRoot"
    $deadline=[DateTime]::UtcNow.AddSeconds($DeadlineSeconds)
    $released=$false; $deadlineDetached=$false
    while (!$debugger.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        if ($Fixture -and !$released -and (Test-Path -LiteralPath $debugLog)) {
            $text=[string](Get-Content -LiteralPath $debugLog -Raw)
            if ($text -match '(?m)^MGS_VECTOR_ARMED\r?$') { Set-Content -LiteralPath $gate -Value 'go'; $released=$true }
        }
        Start-Sleep -Milliseconds 50
    }
    if (!$debugger.HasExited) {
        & "$PSScriptRoot/stop_debugger.ps1" -PipeName $pipeName -PipePassword $pipePassword -ExpectedTargetProcessId $TargetProcessId
        $deadlineDetached=$true
        $debugger.StandardInput.WriteLine('q'); $debugger.StandardInput.Flush()
        if (!$debugger.WaitForExit(5000)) { throw 'Detachment succeeded but debugger exit is unconfirmed' }
    }
    if ($Fixture -and !$released) { Set-Content -LiteralPath $gate -Value 'go' }
    $text=[string](Get-Content -LiteralPath $debugLog -Raw)
    $entries=[regex]::Matches($text,'(?m)^MGS_VECTOR_ENTRY setter=(?<setter>[01]) tid=\S+ image=\S+ entry=\S+ caller=\S+ receiver_opaque=\S+ source_opaque=\S+ index_raw=\d+\r?$')
    if ($WatchPublished) {
        $entries=[regex]::Matches($text,'(?m)^MGS_VECTOR_WRITE bank=(?<setter>[01]) tid=\S+ image=\S+ instruction_raw=\S+ watched=\S+ rax_opaque=\S+ rbx_opaque=\S+ rcx_opaque=\S+ rdx_opaque=\S+ r8_opaque=\S+ r9_opaque=\S+ rsp_opaque=\S+ rdi_opaque=\S+ rsi_raw=\S+ r13_opaque=\S+ r14_opaque=\S+\r?$')
    }
    $record=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');target_pid=$TargetProcessId;target_started_utc=$targetStarted.ToUniversalTime().ToString('o');executable_sha256=(Get-FileHash $executable -Algorithm SHA256).Hash;fixture=[bool]$Fixture;scenario=$Scenario;armed=$text -match '(?m)^MGS_VECTOR_ARMED\r?$';entry_count=$entries.Count;aborted=$text -match '(?m)^MGS_ABORT ';deadline_detached=$deadlineDetached;debugger_exit=$debugger.ExitCode;debug_log=$debugLog;source_dereferenced=$false;game_calls=$false;actor_identity='unverified'}
    $record.observation_mode=$(if ($WatchPublished) {'published_write'} else {'setter_entry'})
    $record.copy_filter_enabled=[bool]$SkipBankCopy
    $record.owner_graph_requested=[bool]$CaptureOwner
    $record.source_dereferenced=[bool]$CaptureOwner
    $record.source_vector_read=$false
    $record.owner_graph_count=[regex]::Matches($text,'(?m)^MGS_VECTOR_OWNER primary=\S+ secondary=\S+ source_owner=\S+ source_table=\S+ source_accessor=\S+ source_array=\S+\r?$').Count
    $record.copy_skip_count=[regex]::Matches($text,'(?m)^MGS_VECTOR_COPY_SKIPPED\r?$').Count
    if ($Fixture) {
        if (!$fixtureProcess.WaitForExit(5000)) { throw 'Fixture did not finish after debugger exit' }
        $result=Get-Content -LiteralPath $fixtureResult -Raw | ConvertFrom-Json
        $record.fixture_result=$result
        $expectedCapture=$Scenario -in @('Local','Indexed')
        $record.passed=$result.passed -and $fixtureProcess.ExitCode -eq 0 -and $debugger.ExitCode -eq 0 -and ($entries.Count -eq [int]$expectedCapture)
        if ($expectedCapture) { $record.passed=$record.passed -and !$record.aborted -and !$deadlineDetached -and ($entries[0].Groups['setter'].Value -eq $(if($Scenario -eq 'Indexed'){'0'}else{'1'})) }
        if ($expectedCapture -and $SkipBankCopy) { $record.passed=$record.passed -and $record.copy_skip_count -eq 2 }
        if ($Scenario -eq 'NoHit') { $record.passed=$record.passed -and $deadlineDetached -and $record.armed -and !$record.aborted }
        if ($Scenario -eq 'CopyLimit') { $record.passed=$record.passed -and !$deadlineDetached -and $record.armed -and $record.aborted -and $record.copy_skip_count -eq 121 -and ($text -match '(?m)^MGS_ABORT copy_skip_limit\r?$') }
        if ($Scenario -eq 'WrongBytes') { $record.passed=$record.passed -and !$record.armed -and ($text -match '(?m)^MGS_ABORT setter_0_bytes_mismatch\r?$') }
    } else {
        $remaining=Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue
        $record.target_still_running=($null -ne $remaining -and $remaining.StartTime -eq $targetStarted)
        $record.capture_valid=($entries.Count -eq 1 -and !$record.aborted -and !$deadlineDetached -and $debugger.ExitCode -eq 0 -and $text -match '(?m)^MGS_VECTOR_DETACH\r?$')
        if ($CaptureOwner) { $record.capture_valid=$record.capture_valid -and $record.owner_graph_count -eq 1 }
    }
    $record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json')
    $record | ConvertTo-Json -Depth 5
    if ($Fixture -and !$record.passed) { throw 'Attach fixture failed; inspect its retained result and debugger log' }
} finally {
    # Never terminate MGSV or its debugger as a cleanup substitute. An error
    # leaves the dedicated debugger/recovery coordinates available for diagnosis.
    if ($fixtureProcess -and !$fixtureProcess.HasExited -and (!$debugger -or $debugger.HasExited)) { $fixtureProcess.Kill() }
    if ($debugger -and $debugger.HasExited) { $debugger.Dispose() }
    if ($fixtureProcess -and $fixtureProcess.HasExited) { $fixtureProcess.Dispose() }
}
