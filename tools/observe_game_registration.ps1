param(
    [ValidateRange(2,30)][int]$DeadlineSeconds = 20,
    [switch]$CaptureReturn,
    [ValidateSet('NetworkRegistration','SteamInit','ScriptLoad','ArchiveMount')][string]$Observation = 'NetworkRegistration'
)
$ErrorActionPreference = 'Stop'
if ($Observation -eq 'SteamInit' -and $CaptureReturn) { throw 'SteamInit always captures its call and return; omit CaptureReturn' }
if ($Observation -eq 'ScriptLoad' -and $CaptureReturn) { throw 'ScriptLoad always captures its entry and epilogue; omit CaptureReturn' }
if ($Observation -eq 'ArchiveMount' -and $CaptureReturn) { throw 'ArchiveMount always captures its entry and result path; omit CaptureReturn' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$game = [IO.Path]::GetFullPath('D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe')
$cdb = 'C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe'
if (@(Get-Process mgsvtpp -ErrorAction SilentlyContinue).Count) { throw 'MGSV is already running; this observer requires a fresh startup and will not close it' }
if (!(Test-Path -LiteralPath "$projectRoot/build/native/stop_debugger.exe")) { throw 'Build the tested debugger stop helper first' }
# Steam IPC is account/session scoped. An isolated tooling account can create
# an invisible failed startup even when the user's Steam process is present.
try {
    $currentSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $currentSession = [Diagnostics.Process]::GetCurrentProcess().SessionId
    $steamCandidates = @(Get-CimInstance Win32_Process -Filter "Name='steam.exe'" | Where-Object SessionId -eq $currentSession)
    $matchingSteam = @($steamCandidates | Where-Object {
        $owner = Invoke-CimMethod -InputObject $_ -MethodName GetOwnerSid
        $owner.ReturnValue -eq 0 -and $owner.Sid -eq $currentSid
    })
    if ($matchingSteam.Count -ne 1) { throw 'No unique Steam client in this account/session' }
} catch {
    throw 'Steam account/session could not be verified. Run the observer in the desktop account with Steam running; no game was launched.'
}
$runRoot = Join-Path $projectRoot ('build/game-observation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$commandName = if ($Observation -eq 'SteamInit') { 'steam-init.cdb' } else { 'registration.cdb' }
$commands = Join-Path $runRoot $commandName
$debugLog = Join-Path $runRoot 'debugger.log'
$pipeName = 'MgsCoopGame' + [Guid]::NewGuid().ToString('N')
$pipePassword = [Guid]::NewGuid().ToString('N').Substring(0,12)
$generatorName = if ($Observation -eq 'SteamInit') { 'generate_steam_init_trace.py' } else { 'generate_registration_trace.py' }
if ($Observation -eq 'ScriptLoad') { $generatorName = 'generate_script_load_trace.py' }
if ($Observation -eq 'ArchiveMount') { $generatorName = 'generate_mount_trace.py' }
$generatorArguments = @((Join-Path $PSScriptRoot $generatorName), '--executable', $game, '--output', $commands)
if ($CaptureReturn) { $generatorArguments += '--capture-return' }
python @generatorArguments
if ($LASTEXITCODE -ne 0) { throw 'Game fingerprint or command generation failed' }
$start = [Diagnostics.ProcessStartInfo]::new($cdb)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.WorkingDirectory = Split-Path -Parent $game
foreach ($argument in @('-server',"npipe:pipe=$pipeName,hidden,password=$pipePassword",'-pd','-hd','-sins','-y',"$projectRoot/build/native",'-noshell','-nosqm','-cf',$commands,'-logo',$debugLog,$game)) {
    $start.ArgumentList.Add($argument)
}
$debugger = [Diagnostics.Process]::new()
$debugger.StartInfo = $start
$started = [DateTime]::Now
$deadlineDetached = $false
$stdoutReader = $null
$stderrReader = $null
try {
    if (!$debugger.Start()) { throw 'CDB game launch failed' }
    # Retain private recovery coordinates locally if interruption prevents the
    # normal bounded detach. They are never sent to a remote service.
    @{ pipe_name=$pipeName; pipe_password=$pipePassword; debugger_pid=$debugger.Id; started_utc=$started.ToUniversalTime().ToString('o'); executable=$game } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'recovery.json')
    $stdoutReader = $debugger.StandardOutput
    $stderrReader = $debugger.StandardError
    $stdout = $stdoutReader.ReadToEndAsync()
    $stderr = $stderrReader.ReadToEndAsync()
    Write-Output "Observation running; evidence directory: $runRoot"
    if (!$debugger.WaitForExit($DeadlineSeconds * 1000)) {
        $targets = @(Get-Process mgsvtpp -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $game -and $_.StartTime -ge $started })
        if ($targets.Count -ne 1) { throw 'Cannot uniquely identify the launched game for debugger detachment; debugger remains available' }
        & (Join-Path $PSScriptRoot 'stop_debugger.ps1') -PipeName $pipeName -PipePassword $pipePassword -ExpectedTargetProcessId $targets[0].Id
        $deadlineDetached = $true
        $debugger.StandardInput.WriteLine('q')
        $debugger.StandardInput.Flush()
        if (!$debugger.WaitForExit(5000)) { throw 'Game detached but debugger exit is unconfirmed' }
    }
    # CDB can exit while the continuing game retains inherited pipe handles.
    # EOF on those pipes is not a debugger-exit condition: never await it here.
    # The debugger's own completed log is the primary observation evidence.
    $stdoutComplete = $stdout.Status -eq [Threading.Tasks.TaskStatus]::RanToCompletion
    $stderrComplete = $stderr.Status -eq [Threading.Tasks.TaskStatus]::RanToCompletion
    $completedOutput = ''
    if ($stdoutComplete) {
        $completedOutput = $stdout.GetAwaiter().GetResult()
        $completedOutput | Set-Content -LiteralPath (Join-Path $runRoot 'stdout.log')
    }
    if ($stderrComplete) {
        $stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $runRoot 'stderr.log')
    }
    $evidenceSource = 'unavailable'
    $output = ''
    if (Test-Path -LiteralPath $debugLog -PathType Leaf) {
        $output = [string](Get-Content -LiteralPath $debugLog -Raw)
        $evidenceSource = 'debugger_log'
    } elseif ($stdoutComplete) {
        $output = $completedOutput
        $evidenceSource = 'completed_stdout'
    }
    $gameProcesses = @(Get-Process mgsvtpp -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $game -and $_.StartTime -ge $started })
    $entryMarker = if ($Observation -eq 'SteamInit') { '(?m)^MGS_STEAM_INIT_CALL ' } else { '(?m)^MGS_REGISTRATION_ENTRY ' }
    $returnMarker = if ($Observation -eq 'SteamInit') { '(?m)^MGS_STEAM_INIT_RETURN ' } else { '(?m)^MGS_REGISTRATION_RETURN ' }
    if ($Observation -eq 'ScriptLoad') {
        $entryMarker = '(?m)^MGS_SCRIPT_LOAD_ENTRY '
        $returnMarker = '(?m)^MGS_SCRIPT_LOAD_RETURN '
    }
    if ($Observation -eq 'ArchiveMount') {
        $entryMarker = '(?m)^MGS_MOUNT_ENTRY '
        $returnMarker = '(?m)^MGS_MOUNT_RETURN '
    }
    $initResult = $null
    if ($Observation -eq 'SteamInit') {
        $calls = [regex]::Matches($output, '(?m)^MGS_STEAM_INIT_CALL tid=(?<tid>\S+) call=\S+ call_rsp=(?<rsp>\S+)')
        $returns = [regex]::Matches($output, '(?m)^MGS_STEAM_INIT_RETURN tid=(?<tid>\S+) call_rsp=(?<call_rsp>\S+) return_rsp=(?<return_rsp>\S+) al=(?<al>[01])\r?$')
        if ($calls.Count -eq 1 -and $returns.Count -eq 1 -and
            $calls[0].Groups['tid'].Value -eq $returns[0].Groups['tid'].Value -and
            $calls[0].Groups['rsp'].Value -eq $returns[0].Groups['call_rsp'].Value -and
            $calls[0].Groups['rsp'].Value -eq $returns[0].Groups['return_rsp'].Value -and
            $output -notmatch '(?m)^MGS_ABORT ' -and $debugger.ExitCode -eq 0) {
            $initResult = [int]$returns[0].Groups['al'].Value
        }
    }
    $record = [ordered]@{
        utc = [DateTime]::UtcNow.ToString('o')
        observation = $Observation
        captured = [bool]($output -match $entryMarker)
        return_requested = [bool]($CaptureReturn -or $Observation -in @('SteamInit','ScriptLoad','ArchiveMount'))
        return_captured = [bool]($output -match $returnMarker)
        steam_init_result_al = $initResult
        deadline_detached = $deadlineDetached
        aborted = [bool]($output -match '(?m)^MGS_ABORT ')
        debugger_exit_code = $debugger.ExitCode
        live_game_pids = @($gameProcesses | ForEach-Object Id)
        game_continuation_verified = $false
        log = $debugLog
        evidence_source = $evidenceSource
        stdout_complete = $stdoutComplete
        stderr_complete = $stderrComplete
    }
    $record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'result.json')
    $record | ConvertTo-Json
    $output -split "`r?`n" | Where-Object { $_ -match '^MGS_' }
} finally {
    # Never terminate MGSV or CDB as a fallback. The recorded local pipe remains
    # usable by stop_debugger if the normal detach path reports a failure.
    # Closing only our readers releases inherited-pipe reads without waiting for
    # the game to close its write handles. Cleanup must not mask the run result.
    if ($null -ne $stdoutReader) { try { $stdoutReader.Dispose() } catch { } }
    if ($null -ne $stderrReader) { try { $stderrReader.Dispose() } catch { } }
    $debugger.Dispose()
}
