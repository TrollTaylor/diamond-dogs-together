param([ValidateRange(1, 30)][int]$DeadlineSeconds = 20, [ValidateSet('Capture','WrongBytes','NoHit','NestedReturn','ReturnTimeout')][string]$Scenario = 'Capture', [switch]$CaptureReturn)
$ErrorActionPreference = 'Stop'
if ($Scenario -in @('NestedReturn','ReturnTimeout') -and !$CaptureReturn) { throw "$Scenario requires -CaptureReturn" }
if ($Scenario -eq 'ReturnTimeout' -and !$PSBoundParameters.ContainsKey('DeadlineSeconds')) { $DeadlineSeconds = 2 }
$expectCapture = $Scenario -in @('Capture','NestedReturn')
$projectRoot = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $projectRoot 'build/native/registration_fixture.exe'
$commands = Join-Path $projectRoot 'build/registration-fixture.cdb'
$result = Join-Path $projectRoot ('build/registration-fixture-' + [Guid]::NewGuid().ToString('N') + '.json')
$debugLog = Join-Path $projectRoot 'build/registration-fixture-debug.log'
$cdb = 'C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe'
$pipeName = 'MgsCoopFixture' + [Guid]::NewGuid().ToString('N')
$pipePassword = [Guid]::NewGuid().ToString('N').Substring(0, 12)
$generatorArguments = @((Join-Path $PSScriptRoot 'generate_registration_trace.py'), '--fixture', '--executable', $fixture, '--output', $commands)
if ($CaptureReturn) { $generatorArguments += '--capture-return' }
python @generatorArguments
if ($LASTEXITCODE -ne 0) { throw 'Command generation failed' }
if ($Scenario -eq 'WrongBytes') {
    $bootstrap = Join-Path $projectRoot 'build/registration-fixture.bootstrap.cmd'
    $script = Get-Content -LiteralPath $bootstrap -Raw
    $entryGuard = '(by(@$t2+0x0) == 0x48)'
    if (!$script.Contains($entryGuard)) { throw 'Fixture entry changed; update the deliberate mismatch scenario' }
    $script.Replace($entryGuard, '(by(@$t2+0x0) == 0x49)') | Set-Content -LiteralPath $bootstrap -Encoding ascii
}
$start = [Diagnostics.ProcessStartInfo]::new($cdb)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.WorkingDirectory = Split-Path -Parent $fixture
foreach ($argument in @('-server',"npipe:pipe=$pipeName,hidden,password=$pipePassword",'-pd','-hd','-sins','-y',(Split-Path -Parent $fixture),'-noshell','-nosqm','-cf',$commands,'-logo',$debugLog,$fixture,$result)) {
    $start.ArgumentList.Add($argument)
}
if ($Scenario -eq 'NoHit') { $start.ArgumentList.Add('--no-hit') }
if ($Scenario -eq 'NestedReturn') { $start.ArgumentList.Add('--nested-return') }
if ($Scenario -eq 'ReturnTimeout') { $start.ArgumentList.Add('--return-timeout') }
$debugger = [Diagnostics.Process]::new()
$debugger.StartInfo = $start
$started = [DateTime]::Now
$deadlineDetached = $false
$interruptVerified = $false
try {
    if (!$debugger.Start()) { throw 'CDB launch failed' }
    $stdout = $debugger.StandardOutput.ReadToEndAsync()
    $stderr = $debugger.StandardError.ReadToEndAsync()
    # Keep stdin open while the debugger executes its command file.
    if (!$debugger.WaitForExit($DeadlineSeconds * 1000)) {
        $targets = @(Get-Process registration_fixture -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $fixture -and $_.StartTime -ge $started })
        if ($targets.Count -ne 1) { throw 'Cannot identify this fixture target uniquely for clean detach' }
        $stopOutput = @(& (Join-Path $PSScriptRoot 'stop_debugger.ps1') -PipeName $pipeName -PipePassword $pipePassword -ExpectedTargetProcessId $targets[0].Id)
        $stopOutput | Write-Output
        $interruptVerified = ($stopOutput -join [Environment]::NewLine) -match '(?m)^MGS_STOP_DETACHED .* interrupt_verified=1(?:\s|$)'
        $deadlineDetached = $true
        $debugger.StandardInput.WriteLine('q')
        $debugger.StandardInput.Flush()
        if (!$debugger.WaitForExit(5000)) { throw 'Target detached, but debugger server did not exit' }
        if ($Scenario -notin @('NoHit','ReturnTimeout')) { throw 'Unexpected fixture deadline; target detached cleanly but observation failed' }
    }
    $output = $stdout.GetAwaiter().GetResult()
    $errors = $stderr.GetAwaiter().GetResult()
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while (!(Test-Path -LiteralPath $result) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    if (!(Test-Path -LiteralPath $result)) { throw "No fixture result. Inspect $debugLog. $errors" }
    $record = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json
    $record | ConvertTo-Json
    if (!$record.passed) { throw "Fixture failed. Inspect $debugLog" }
    if ($expectCapture -and $output -notmatch '(?m)^MGS_REGISTRATION_ENTRY ') { throw "No capture. Inspect $debugLog" }
    if ($CaptureReturn -and $expectCapture) {
        $returnCaptures = [regex]::Matches($output, '(?m)^MGS_REGISTRATION_RETURN .* eax=37\r?$')
        $allReturnCaptures = [regex]::Matches($output, '(?m)^MGS_REGISTRATION_RETURN ')
        if ($returnCaptures.Count -ne 1 -or $allReturnCaptures.Count -ne 1 -or $output -notmatch '(?m)^MGS_HARDWARE_REGISTRAR_RETURN_ARMED') { throw "Matching return value 37 was not captured exactly once. Inspect $debugLog" }
    }
    if ($Scenario -eq 'NestedReturn') {
        $skippedFrames = [regex]::Matches($output, '(?m)^MGS_RETURN_FRAME_SKIPPED tid=(?<tid>\S+) expected_tid=(?<expected_tid>\S+) rsp=(?<rsp>\S+) expected_rsp=(?<expected_rsp>\S+)')
        $nestedFrames = @($skippedFrames | Where-Object { $_.Groups['tid'].Value -eq $_.Groups['expected_tid'].Value -and $_.Groups['rsp'].Value -ne $_.Groups['expected_rsp'].Value -and $_.Index -lt $returnCaptures[0].Index })
        if ($skippedFrames.Count -lt 1 -or $skippedFrames.Count -gt 8 -or $nestedFrames.Count -lt 1 -or $record.original_calls -ne 2 -or !$record.nested_return -or $record.nested_inner_result -ne 37 -or !$record.nested_result_verified) { throw "Nested return frame was not skipped before the outer return capture. Inspect $debugLog" }
    }
    if ($Scenario -eq 'WrongBytes' -and ($output -notmatch '(?m)^MGS_ABORT restored_entry_bytes_mismatch' -or $output -match '(?m)^MGS_REGISTRATION_ENTRY ')) { throw "Byte mismatch did not fail closed. Inspect $debugLog" }
    if ($Scenario -eq 'NoHit' -and (!$deadlineDetached -or !$record.resumed_after_detach -or $output -notmatch '(?m)^MGS_HARDWARE_REGISTRAR_ARMED' -or $output -match '(?m)^MGS_REGISTRATION_ENTRY ')) { throw "No-hit timeout did not detach and resume cleanly. Inspect $debugLog" }
    if ($Scenario -eq 'ReturnTimeout') {
        $entryCaptures = [regex]::Matches($output, '(?m)^MGS_REGISTRATION_ENTRY ')
        if (!$deadlineDetached -or !$interruptVerified -or $entryCaptures.Count -ne 1 -or $output -notmatch '(?m)^MGS_HARDWARE_REGISTRAR_RETURN_ARMED' -or !$record.return_timeout -or !$record.return_wait_entered -or !$record.return_timeout_verified -or !$record.resumed_after_detach -or $record.original_calls -ne 1 -or $record.result -ne 37) { throw "Timeout after entry did not detach and preserve the original return. Inspect $debugLog" }
    }
    if (!$expectCapture -and $output -match '(?m)^MGS_REGISTRATION_RETURN ') { throw "Unexpected return capture in $Scenario scenario. Inspect $debugLog" }
    Copy-Item -LiteralPath $result -Destination (Join-Path $projectRoot 'build/registration-fixture-result.json') -Force
    Write-Output "Registration observer fixture passed: $Scenario. Target continued after detach."
} finally {
    # This cleanup is strictly for this original test executable and this run.
    Get-Process registration_fixture -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $fixture -and $_.StartTime -ge $started } | Stop-Process
    if ($debugger.Id -and !$debugger.HasExited) {
        # Fixture-only failure cleanup may end the inert target above, but never
        # kills the debugger as a substitute for confirming normal detachment.
        try {
            $debugger.StandardInput.WriteLine('q')
            $debugger.StandardInput.Flush()
            if (!$debugger.WaitForExit(3000)) { Write-Warning "Fixture debugger $($debugger.Id) remains available for diagnosis" }
        } catch { Write-Warning 'Fixture debugger exit could not be confirmed' }
    }
    $debugger.Dispose()
}
