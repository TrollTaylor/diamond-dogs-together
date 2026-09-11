param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9]{16,96}$')][string]$PipeName,
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9]{12}$')][string]$PipePassword,
    [Parameter(Mandatory)][ValidateRange(1, 4294967295)][long]$ExpectedTargetProcessId,
    [ValidateRange(1, 30)][int]$TimeoutSeconds = 10
)
$ErrorActionPreference = 'Stop'
$helper = Join-Path (Split-Path -Parent $PSScriptRoot) 'build/native/stop_debugger.exe'
if (!(Test-Path -LiteralPath $helper -PathType Leaf)) { throw 'Build the original stop_debugger helper first' }
$start = [Diagnostics.ProcessStartInfo]::new($helper)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in @($PipeName, $PipePassword, [string]$ExpectedTargetProcessId)) { $start.ArgumentList.Add($argument) }
$client = [Diagnostics.Process]::new()
$client.StartInfo = $start
try {
    if (!$client.Start()) { throw 'Could not start the debugger stop client' }
    $stdout = $client.StandardOutput.ReadToEndAsync()
    $stderr = $client.StandardError.ReadToEndAsync()
    if (!$client.WaitForExit($TimeoutSeconds * 1000)) {
        # This is only our remote client. Never kill CDB or its target here.
        $client.Kill()
        throw 'Stop client timed out; debugger detachment is unconfirmed. Keep the debugger server available.'
    }
    $output = $stdout.GetAwaiter().GetResult()
    $errors = $stderr.GetAwaiter().GetResult()
    if ($client.ExitCode -ne 0 -or $output -notmatch '(?m)^MGS_STOP_DETACHED ') {
        throw "Debugger detachment unconfirmed: $errors"
    }
    $output.Trim()
} finally {
    $client.Dispose()
}
