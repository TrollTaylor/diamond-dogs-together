param(
    [ValidateSet('True','False','Nested','OtherThread','Repeated','WrongBytes','WrongReturnBytes','WrongOwner','NoHit','ReturnTimeout')]
    [string]$Scenario = 'True',
    [ValidateRange(1, 30)][int]$DeadlineSeconds = 20
)
$ErrorActionPreference = 'Stop'
if ($Scenario -in @('NoHit','ReturnTimeout') -and !$PSBoundParameters.ContainsKey('DeadlineSeconds')) { $DeadlineSeconds = 2 }
$projectRoot = Split-Path -Parent $PSScriptRoot
$fixture = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build/native/steam_init_fixture.exe'))
$runRoot = Join-Path $projectRoot ('build/steam-init-fixture/' + $Scenario + '-' + [Guid]::NewGuid().ToString('N'))
$commands = Join-Path $runRoot 'observer.cdb'
$result = Join-Path $runRoot 'result.json'
$debugLog = Join-Path $runRoot 'debug.log'
$cdb = 'C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe'
$pipeName = 'MgsSteamFixture' + [Guid]::NewGuid().ToString('N')
$pipePassword = [Guid]::NewGuid().ToString('N').Substring(0, 12)
$targetMode = if ($Scenario -in @('WrongBytes','WrongReturnBytes')) { 'True' } else { $Scenario }
python (Join-Path $PSScriptRoot 'generate_steam_init_trace.py') --fixture --executable $fixture --output $commands
if ($LASTEXITCODE -ne 0) { throw 'Steam fixture command generation failed' }
if ($Scenario -in @('WrongBytes','WrongReturnBytes')) {
    # Change an observer comparison only. Neither fixture binary is modified.
    $guardFile = Join-Path $runRoot $(if ($Scenario -eq 'WrongBytes') { 'observer.bootstrap.cmd' } else { 'observer.return.cmd' })
    $guard = if ($Scenario -eq 'WrongBytes') { '(by(@$t3+0x0) == 0xff)' } else { '(by(@$t9+0x0) == 0x84)' }
    $replacement = if ($Scenario -eq 'WrongBytes') { '(by(@$t3+0x0) == 0xfe)' } else { '(by(@$t9+0x0) == 0x85)' }
    $script = Get-Content -LiteralPath $guardFile -Raw
    if (!$script.Contains($guard)) { throw 'Expected fixture comparison is absent; mismatch scenario cannot run' }
    $script.Replace($guard, $replacement) | Set-Content -LiteralPath $guardFile -Encoding ascii
}
$start = [Diagnostics.ProcessStartInfo]::new($cdb)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.WorkingDirectory = Split-Path -Parent $fixture
# The fixture raises its own caught SEH exception in Repeated mode. Pass that
# first-chance event to its handler; this is not a debugger-injected exception.
foreach ($argument in @('-server',"npipe:pipe=$pipeName,hidden,password=$pipePassword",'-pd','-hd','-sins','-y',(Split-Path -Parent $fixture),'-noshell','-nosqm','-xd','0xe0424242','-cf',$commands,'-logo',$debugLog,$fixture,$result,$targetMode)) {
    $start.ArgumentList.Add($argument)
}
$debugger = [Diagnostics.Process]::new()
$debugger.StartInfo = $start
$started = [DateTime]::Now
$launched = $false
$deadlineDetached = $false
$interruptVerified = $false
$outputReader = $null
$errorReader = $null
try {
    if (!$debugger.Start()) { throw 'CDB launch failed' }
    $launched = $true
    $outputReader = $debugger.StandardOutput
    $errorReader = $debugger.StandardError
    $stdout = $outputReader.ReadToEndAsync()
    $stderr = $errorReader.ReadToEndAsync()
    # Keep stdin open for the dedicated server, including deadline recovery.
    if (!$debugger.WaitForExit($DeadlineSeconds * 1000)) {
        $targets = @(Get-Process steam_init_fixture -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $fixture -and $_.StartTime -ge $started })
        if ($targets.Count -ne 1) { throw 'Cannot identify this inert fixture uniquely for verified detach' }
        $stopOutput = @(& (Join-Path $PSScriptRoot 'stop_debugger.ps1') -PipeName $pipeName -PipePassword $pipePassword -ExpectedTargetProcessId $targets[0].Id)
        $stopText = $stopOutput -join [Environment]::NewLine
        $stopText | Set-Content -LiteralPath (Join-Path $runRoot 'stop.log') -Encoding ascii
        $interruptVerified = $stopText -match '(?m)^MGS_STOP_DETACHED .* interrupt_verified=1(?:\s|$)'
        if (!$interruptVerified) { throw 'Deadline helper did not verify its interrupt event' }
        $deadlineDetached = $true
        $debugger.StandardInput.WriteLine('q')
        $debugger.StandardInput.Flush()
        if (!$debugger.WaitForExit(5000)) { throw 'Fixture detached, but debugger server did not exit' }
        if ($Scenario -notin @('NoHit','ReturnTimeout')) { throw "Unexpected observation deadline; inspect $debugLog" }
    }
    # A detached child can inherit pipe handles. Never await incomplete output
    # tasks after CDB exit; its own log is the authoritative observation record.
    $output = if (Test-Path -LiteralPath $debugLog) { Get-Content -LiteralPath $debugLog -Raw } else { '' }
    if (!$output -and $stdout.IsCompletedSuccessfully) { $output = $stdout.GetAwaiter().GetResult() }
    $errors = if ($stderr.IsCompletedSuccessfully) { $stderr.GetAwaiter().GetResult() } else { '' }
    $record = $null
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while (!$record -and [DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $result) {
            try { $record = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json } catch { $record = $null }
        }
        if (!$record) { Start-Sleep -Milliseconds 50 }
    }
    if (!$record) { throw "Fixture produced no complete result. Inspect $debugLog. $errors" }
    if (!$record.passed) { throw "Fixture preservation checks failed: $($record | ConvertTo-Json -Compress). Inspect $debugLog" }
    $calls = [regex]::Matches($output, '(?m)^MGS_STEAM_INIT_CALL tid=(?<tid>\S+) call=\S+ call_rsp=(?<rsp>\S+)')
    $returns = [regex]::Matches($output, '(?m)^MGS_STEAM_INIT_RETURN tid=(?<tid>\S+) call_rsp=(?<call_rsp>\S+) return_rsp=(?<return_rsp>\S+) al=(?<al>\d+)\r?$')
    $expectReturn = $Scenario -in @('True','False','Nested','OtherThread')
    if ($expectReturn) {
        $expectedAl = if ($Scenario -eq 'False') { '0' } else { '1' }
        if ($calls.Count -ne 1 -or $returns.Count -ne 1 -or $returns[0].Groups['al'].Value -ne $expectedAl -or
            $returns[0].Groups['tid'].Value -ne $calls[0].Groups['tid'].Value -or
            $returns[0].Groups['call_rsp'].Value -ne $calls[0].Groups['rsp'].Value -or
            $returns[0].Groups['return_rsp'].Value -ne $calls[0].Groups['rsp'].Value) {
            throw "Matching direct-call return/AL was not captured exactly once. Inspect $debugLog"
        }
    } elseif ($returns.Count -ne 0) { throw "Unexpected return capture in $Scenario. Inspect $debugLog" }
    if ($Scenario -in @('Nested','OtherThread')) {
        $skippedCalls = [regex]::Matches($output, '(?m)^MGS_STEAM_INIT_OTHER_CALL_SKIPPED tid=(?<tid>\S+) rsp=(?<rsp>\S+)')
        $skippedReturns = [regex]::Matches($output, '(?m)^MGS_STEAM_INIT_RETURN_FRAME_SKIPPED tid=(?<tid>\S+) rsp=(?<rsp>\S+) expected_rsp=(?<expected_rsp>\S+)')
        if ($skippedCalls.Count -ne 1 -or $skippedReturns.Count -ne 1 -or $skippedReturns[0].Index -ge $returns[0].Index -or $record.calls -ne 2 -or $record.inner_raw -ne 2779054080) {
            throw "Inner false return was not skipped before outer true return. Inspect $debugLog"
        }
        $sameThread = $skippedReturns[0].Groups['tid'].Value -eq $calls[0].Groups['tid'].Value
        if (($Scenario -eq 'Nested' -and (!$sameThread -or $skippedReturns[0].Groups['rsp'].Value -eq $calls[0].Groups['rsp'].Value)) -or
            ($Scenario -eq 'OtherThread' -and $sameThread)) { throw "Wrong inner frame identity. Inspect $debugLog" }
    }
    $abort = switch ($Scenario) {
        'Repeated' { 'repeated_steam_init_call_frame' }
        'WrongBytes' { 'steam_init_call_bytes_mismatch' }
        'WrongReturnBytes' { 'steam_init_return_hit_bytes_mismatch' }
        'WrongOwner' { 'steam_init_iat_not_original_export' }
        default { $null }
    }
    if ($abort -and $output -notmatch ('(?m)^MGS_ABORT ' + $abort + '\r?$')) { throw "Expected rejection $abort was absent. Inspect $debugLog" }
    $expectedCalls = if ($Scenario -in @('WrongBytes','WrongOwner','NoHit')) { 0 } else { 1 }
    if ($calls.Count -ne $expectedCalls) { throw "Unexpected call capture count in $Scenario. Inspect $debugLog" }
    if ($Scenario -eq 'Repeated' -and ($record.calls -ne 2 -or $record.caught_exceptions -ne 1)) { throw 'Repeated frame did not unwind and retry naturally' }
    if ($Scenario -eq 'WrongOwner' -and ($record.foreign_calls -ne 1 -or !$record.iat_restored)) { throw 'Fixture replacement call or self-restoration failed' }
    if ($Scenario -in @('NoHit','ReturnTimeout')) {
        if (!$deadlineDetached -or !$interruptVerified -or !$record.resumed_after_detach -or $output -notmatch '(?m)^MGS_STEAM_INIT_CALL_ARMED') {
            throw "Deadline did not verify clean detach and target continuation. Inspect $debugLog"
        }
        if ($Scenario -eq 'ReturnTimeout' -and ($record.wait_entered -ne 1 -or $output -notmatch '(?m)^MGS_STEAM_INIT_RETURN_ARMED')) { throw 'Return deadline did not exercise an armed return breakpoint' }
    }
    [ordered]@{ scenario = $Scenario; passed = $true; deadline_detached = $deadlineDetached; interrupt_verified = $interruptVerified;
        call_captures = $calls.Count; return_captures = $returns.Count; debugger_log = $debugLog; fixture_result = $result } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'summary.json') -Encoding utf8
    Write-Output "Steam Init observer fixture passed: $Scenario. Evidence: $runRoot"
} finally {
    # Only this inert executable started during this invocation is eligible for
    # failure cleanup. No game process or debugger process is ever killed here.
    if ($launched) {
        Get-Process steam_init_fixture -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $fixture -and $_.StartTime -ge $started } | Stop-Process
        if (!$debugger.HasExited) {
            try {
                $debugger.StandardInput.WriteLine('q')
                $debugger.StandardInput.Flush()
                if (!$debugger.WaitForExit(3000)) { Write-Warning "Fixture debugger $($debugger.Id) remains available for diagnosis" }
            } catch { Write-Warning 'Fixture debugger exit could not be confirmed' }
        }
    }
    if ($outputReader) { try { $outputReader.Dispose() } catch {} }
    if ($errorReader) { try { $errorReader.Dispose() } catch {} }
    $debugger.Dispose()
}
