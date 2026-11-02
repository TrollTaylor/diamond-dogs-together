# The hidden-run queue (tools/worker/README.md "Test queue"): Submit-WorkerJob.ps1 writes jobs, Invoke-WorkerQueue.ps1
# (one runner at a time) runs them. Plain files under %TEMP%\mgs-coop-queue: jobs\ (waiting, oldest first), running\
# (the one job the runner works on), done\ (the job, its .result.json), logs\ (each job's run output), runner.lock and
# runner.log. Optional queue.json there tunes the runner (floors, retry times).
Set-StrictMode -Version Latest

function Get-WorkerQueueRoot([string]$QueueRoot) {
    if ($QueueRoot) { return [IO.Path]::GetFullPath($QueueRoot) }
    return (Join-Path ([IO.Path]::GetFullPath($env:TEMP)) 'mgs-coop-queue')
}

# A job id: the UTC submit time to the millisecond (so names sort oldest first) and six random hex digits.
function Test-WorkerQueueJobId([string]$Id) { return $Id -cmatch '^[0-9]{8}-[0-9]{6}-[0-9]{3}-[0-9a-f]{6}$' }
function New-WorkerQueueJobId {
    return ('{0}-{1}' -f [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff', [Globalization.CultureInfo]::InvariantCulture),
        [Guid]::NewGuid().ToString('N').Substring(0, 6))
}

function Get-WorkerQueueStartKey([int]$ProcessId) {
    try { return (Get-Process -Id $ProcessId -ErrorAction Stop).StartTime.ToUniversalTime().ToFileTimeUtc().ToString() }
    catch { return '' }
}
# True while the process that wrote "pid=<pid> start=<filetime> ..." still runs (same pid and start time).
function Test-WorkerQueueOwnerAlive([string]$Text) {
    if ($Text -cnotmatch '(?:^|\s)pid=([0-9]+) start=([0-9]+)(?:\s|$)') { return $false }
    $key = Get-WorkerQueueStartKey ([int]$Matches[1])
    return ($key -and $key -ceq $Matches[2])
}
function Read-WorkerQueueText([string]$Path) {
    try { return [IO.File]::ReadAllText($Path) } catch { return $null }
}
# The live runner's lock text, or $null when no runner is alive.
function Get-WorkerQueueRunner([string]$Root) {
    $text = Read-WorkerQueueText (Join-Path $Root 'runner.lock')
    if ($text -and (Test-WorkerQueueOwnerAlive $text)) { return $text.Trim() }
    return $null
}

function Write-WorkerQueueJson([string]$Path, $Value) {
    $temporary = $Path + '.' + [Guid]::NewGuid().ToString('N').Substring(0, 8) + '.tmp'
    [IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth 12) + "`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporary, $Path, $true)
}
function Read-WorkerQueueJson([string]$Path) {
    return [IO.File]::ReadAllText($Path) | ConvertFrom-Json -AsHashtable
}

# Scenario arguments -> absolute scenario files: entries split at commas (the pwsh -File form a.txt,b.txt) unless a file
# has that very name, relative paths from the repo; a list file holds one path per line (# and blank lines ignored).
function Resolve-WorkerQueueScenarios([string]$Repo, [string[]]$ScenarioPath, [string]$ScenarioList) {
    $entries = [Collections.Generic.List[string]]::new()
    foreach ($entry in @($ScenarioPath)) {
        if (!$entry) { continue }
        if ($entry.Contains(',') -and !(Test-Path -LiteralPath ([IO.Path]::GetFullPath($entry, $Repo)) -PathType Leaf)) {
            foreach ($piece in $entry.Split(',')) { if ($piece.Trim()) { $entries.Add($piece.Trim()) } }
        } else { $entries.Add($entry) }
    }
    if ($ScenarioList) {
        $list = [IO.Path]::GetFullPath($ScenarioList, $Repo)
        foreach ($line in [IO.File]::ReadAllLines($list)) {
            $text = $line.Trim()
            if ($text -and !$text.StartsWith('#')) { $entries.Add($text) }
        }
    }
    $files = @(foreach ($entry in $entries) {
        $file = [IO.Path]::GetFullPath($entry, $Repo)
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Scenario not found: $file" }
        $file
    })
    if (!$files.Count) { throw 'No scenario: give -ScenarioPath or -ScenarioList.' }
    return $files
}

Export-ModuleMember -Function Get-WorkerQueueRoot, Test-WorkerQueueJobId, New-WorkerQueueJobId, Get-WorkerQueueStartKey,
    Test-WorkerQueueOwnerAlive, Read-WorkerQueueText, Get-WorkerQueueRunner, Write-WorkerQueueJson, Read-WorkerQueueJson,
    Resolve-WorkerQueueScenarios
