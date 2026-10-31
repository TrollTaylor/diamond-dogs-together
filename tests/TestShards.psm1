#Requires -Version 5.1
# The parts of one long PowerShell suite, so that CTest can run it as several parallel shards
# (cmake/test_shards.cmake, docs/STATUS.md "Developer test loop"). The suite names its parts with their measured
# cost and asks for each part before running it:
#   Initialize-TestShard -Shard $Shard -Parts ([ordered]@{ pure = 3000; fixtures = 10000 })
#   if (Test-TestPart 'fixtures') { ... }
#   Assert-TestPartsAsked      # at the end: every part of the table was asked for
# -Shard '' runs every part (the developer's plain run); -Shard K/N runs the parts dealt to shard K: longest first,
# each to the shard with the least cost so far (the lowest number on a tie), so every part runs in exactly one shard of
# the N that CMake registers. -Part <name>[,<name>] runs only those parts (the developer loop).
Set-StrictMode -Version Latest

$script:testParts = $null
$script:testSelected = $null
$script:testAsked = $null

# Part name -> shard number (1-based) for N shards.
function Get-TestShardAssignment {
    param([Parameter(Mandatory)][Collections.Specialized.OrderedDictionary]$Parts, [Parameter(Mandatory)][ValidateRange(1, 64)][int]$Count)
    $names = @($Parts.Keys)
    # Longest first; equal costs keep their table order (Windows PowerShell 5.1 has no Sort-Object -Stable).
    $order = @(0..($names.Count - 1) | Sort-Object -Property @{ Expression = { [int64]$Parts[$names[$_]] }; Descending = $true },
        @{ Expression = { $_ }; Descending = $false })
    $load = [int64[]]::new($Count)
    $assigned = [ordered]@{}
    foreach ($name in $names) { $assigned[$name] = 0 }
    foreach ($index in $order) {
        $best = 0
        for ($s = 1; $s -lt $Count; $s++) { if ($load[$s] -lt $load[$best]) { $best = $s } }
        $assigned[$names[$index]] = $best + 1
        $load[$best] += [int64]$Parts[$names[$index]]
    }
    return $assigned
}

function Initialize-TestShard {
    param([Parameter(Mandatory)][Collections.Specialized.OrderedDictionary]$Parts, [AllowEmptyString()][string]$Shard = '',
        [AllowEmptyString()][string]$Part = '')
    if (!$Parts.Count) { throw 'A sharded suite names at least one part.' }
    foreach ($name in $Parts.Keys) {
        if ($name -cnotmatch '^[a-z][a-z0-9-]*$') { throw "Bad test part name: $name" }
        if ([int64]$Parts[$name] -lt 0) { throw "Bad test part cost: $name" }
    }
    $selected = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    if ($Shard -and $Part) { throw 'Use -Shard or -Part, not both.' }
    if ($Shard) {
        if ($Shard -cnotmatch '^([1-9][0-9]?)/([1-9][0-9]?)$' -or [int]$Matches[1] -gt [int]$Matches[2] -or [int]$Matches[2] -gt 64) {
            throw "Bad shard: $Shard (want K/N with 1 <= K <= N <= 64)"
        }
        $index = [int]$Matches[1]
        $assignment = Get-TestShardAssignment -Parts $Parts -Count ([int]$Matches[2])
        foreach ($name in $assignment.Keys) { if ($assignment[$name] -eq $index) { $null = $selected.Add($name) } }
    } elseif ($Part) {
        foreach ($name in $Part.Split(',')) {
            if (!$Parts.Contains($name)) { throw "Unknown test part: $name (parts: $(@($Parts.Keys) -join ', '))" }
            $null = $selected.Add($name)
        }
    } else {
        foreach ($name in $Parts.Keys) { $null = $selected.Add($name) }
    }
    $script:testParts = $Parts
    $script:testSelected = $selected
    $script:testAsked = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
}

# True when this run runs the part. An unknown name fails the suite (a typo never skips a part silently).
function Test-TestPart([Parameter(Mandatory)][string]$Name) {
    if ($null -eq $script:testParts) { throw 'Initialize-TestShard first.' }
    if (!$script:testParts.Contains($Name)) { throw "Unknown test part: $Name" }
    $null = $script:testAsked.Add($Name)
    return $script:testSelected.Contains($Name)
}

# The names of the parts this run runs (for the suite's summary line).
function Get-TestPartsSelected {
    if ($null -eq $script:testParts) { throw 'Initialize-TestShard first.' }
    return @($script:testParts.Keys | Where-Object { $script:testSelected.Contains($_) })
}

# Every part of the table was asked for: a part without its Test-TestPart block would otherwise never run.
function Assert-TestPartsAsked {
    if ($null -eq $script:testParts) { throw 'Initialize-TestShard first.' }
    $missing = @($script:testParts.Keys | Where-Object { !$script:testAsked.Contains($_) })
    if ($missing.Count) { throw "Test parts never asked for: $($missing -join ', ')" }
}

Export-ModuleMember -Function Get-TestShardAssignment, Initialize-TestShard, Test-TestPart, Get-TestPartsSelected, Assert-TestPartsAsked
