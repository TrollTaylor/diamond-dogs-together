# The shared verify lock, %TEMP%\mgs-coop-verify.lock (mkdir + owner.txt): hidden-worker game
# sessions, worker checks, packaging and installs hold it exclusively so that nothing else on
# the machine starves the game. CTest suites no longer take it. Enter-CtestReader instead waits
# while that lock is held by anything other than a CTest, or while a real mgsvtpp.exe or a
# worker_supervisor.exe runs, then registers the suite in %TEMP%\mgs-coop-ctest-readers.
# Suites of different checkouts run at the same time; a lock holder that must not overlap a
# running suite calls Wait-CtestReaders after taking the lock (docs/STATUS.md "Developer test loop").
Set-StrictMode -Version Latest

function Get-VerifyLockPath { return (Join-Path $env:TEMP 'mgs-coop-verify.lock') }
function Get-CtestReaderDirectory { return (Join-Path $env:TEMP 'mgs-coop-ctest-readers') }

# CTest-only holders: an owner starting with 'ctest ' or the older bt.ps1 form
# 'join-fix <checkout> <ISO time>'. Every other owner (worker, checks, package,
# install, unknown, or no owner.txt yet) blocks CTest.
function Test-VerifyLockOwnerIsCtest([AllowEmptyString()][string]$Owner) {
    $text = $Owner.Trim()
    return ($text -match '^ctest\s' -or $text -match '^join-fix [A-Za-z]:\\\S+ \d{4}-\d\d-\d\dT\S+$')
}

# Why a CTest run must wait now, or $null when it may start.
function Get-VerifyLockBlocker {
    $lock = Get-VerifyLockPath
    if (Test-Path -LiteralPath $lock) {
        $owner = ''
        try { $owner = [IO.File]::ReadAllText((Join-Path $lock 'owner.txt')).Trim() } catch { $owner = '' }
        if (!(Test-VerifyLockOwnerIsCtest $owner)) {
            return "verify lock held: $(if ($owner) { $owner } else { '(no owner.txt yet)' })"
        }
    }
    foreach ($process in @(Get-Process -Name mgsvtpp, worker_supervisor -ErrorAction SilentlyContinue)) {
        $path = $null
        try { $path = $process.Path } catch { $path = $null }
        # Test fixtures run small stand-ins named mgsvtpp.exe (lab_fake_process.exe,
        # a copied powershell.exe); MGSV's own executable is about 166 MB.
        if ($process.ProcessName -eq 'mgsvtpp' -and $path -and (Test-Path -LiteralPath $path -PathType Leaf) -and
            (Get-Item -LiteralPath $path).Length -lt 32MB) { continue }
        return "$($process.ProcessName).exe running (pid $($process.Id)$(if ($path) { ", $path" }))"
    }
    return $null
}

function Get-ProcessStartKey([int]$ProcessId) {
    try { return (Get-Process -Id $ProcessId -ErrorAction Stop).StartTime.ToUniversalTime().ToFileTimeUtc().ToString() }
    catch { return '' }
}

# Waits until no worker session or worker checks run, then registers this
# process as a running suite. Returns the marker for Exit-CtestReader.
function Enter-CtestReader {
    param([string]$Label = 'ctest', [ValidateRange(1, 1440)][int]$TimeoutMinutes = 60, [switch]$NoWait)
    $directory = Get-CtestReaderDirectory
    $null = New-Item -ItemType Directory -Path $directory -Force
    $marker = Join-Path $directory ('{0}-{1}.txt' -f $PID, [Guid]::NewGuid().ToString('N').Substring(0, 8))
    $text = "pid=$PID start=$(Get-ProcessStartKey $PID) label=$Label time=$([DateTime]::UtcNow.ToString('o'))"
    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    $reported = $null
    while ($true) {
        # Register first, then look: a holder that takes the lock after this
        # check finds the marker in Wait-CtestReaders.
        [IO.File]::WriteAllText($marker, $text)
        $blocker = if ($NoWait) { $null } else { Get-VerifyLockBlocker }
        if (!$blocker) { return $marker }
        Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
        if ($blocker -ne $reported) { Write-Host "CTest waits: $blocker"; $reported = $blocker }
        if ((Get-Date) -gt $deadline) { throw "CTest gave up after $TimeoutMinutes min: $blocker" }
        Start-Sleep -Seconds 10
    }
}

function Exit-CtestReader([string]$Marker) {
    if ($Marker) { Remove-Item -LiteralPath $Marker -Force -ErrorAction SilentlyContinue }
}

# Live running suites (markers of exited processes are removed).
function Get-CtestReaders {
    $directory = Get-CtestReaderDirectory
    if (!(Test-Path -LiteralPath $directory)) { return @() }
    $live = [Collections.Generic.List[string]]::new()
    foreach ($file in @(Get-ChildItem -LiteralPath $directory -Filter '*.txt' -File -ErrorAction SilentlyContinue)) {
        $text = ''
        try { $text = [IO.File]::ReadAllText($file.FullName) } catch { continue }
        $alive = $false
        if ($text -match '^pid=(\d+) start=(\d+) ') { $alive = (Get-ProcessStartKey ([int]$Matches[1])) -ceq $Matches[2] }
        if ($alive) { $live.Add($text.Trim()) }
        elseif ($file.LastWriteTimeUtc -lt [DateTime]::UtcNow.AddSeconds(-30)) {
            Remove-Item -LiteralPath $file.FullName -Force -ErrorAction SilentlyContinue
        }
    }
    return $live.ToArray()
}

# For a verify-lock holder: after taking the lock, wait for running suites to finish.
function Wait-CtestReaders([ValidateRange(1, 1440)][int]$TimeoutMinutes = 30) {
    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    while ($true) {
        $readers = @(Get-CtestReaders)
        if (!$readers.Count) { return }
        if ((Get-Date) -gt $deadline) { throw "CTest suites still running after $TimeoutMinutes min: $($readers -join '; ')" }
        Write-Host "Waiting for $($readers.Count) CTest suite(s): $($readers[0])"
        Start-Sleep -Seconds 5
    }
}

Export-ModuleMember -Function Get-VerifyLockPath, Get-CtestReaderDirectory, Test-VerifyLockOwnerIsCtest,
    Get-VerifyLockBlocker, Enter-CtestReader, Exit-CtestReader, Get-CtestReaders, Wait-CtestReaders
