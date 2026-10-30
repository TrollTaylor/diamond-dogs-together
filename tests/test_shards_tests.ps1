#Requires -Version 7
# tests/TestShards.psm1 (the parts of a sharded PowerShell suite, cmake/test_shards.cmake): every part runs in exactly
# one shard of N, the deal is deterministic and balanced longest first, and bad shards, unknown parts and parts never
# asked for fail the suite. Pure: no build, process launch, game or input APIs.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'TestShards.psm1') -Force -DisableNameChecking
$checks = 0
function Check([bool]$Value, [string]$Message) { $script:checks++; if (!$Value) { throw "Assertion failed: $Message" } }
function Refuses([scriptblock]$Call, [string]$Pattern) {
    $caught = $null
    try { & $Call | Out-Null } catch { $caught = $_ }
    Check ($null -ne $caught -and $caught.Exception.Message -match $Pattern) "refuses $Pattern"
}

$parts = [ordered]@{ alpha = 5000; beta = 20000; gamma = 7000; delta = 7000; epsilon = 1000; zeta = 0 }
foreach ($count in 1..8) {
    $assignment = Get-TestShardAssignment -Parts $parts -Count $count
    Check (@($assignment.Keys) -join ',' -ceq 'alpha,beta,gamma,delta,epsilon,zeta') "N=$count keeps every part, in table order"
    Check (@($assignment.Values | Where-Object { $_ -lt 1 -or $_ -gt $count }).Count -eq 0) "N=$count deals only shards 1..$count"
    $seen = @{}
    foreach ($k in 1..$count) {
        Initialize-TestShard -Parts $parts -Shard "$k/$count"
        foreach ($name in $parts.Keys) { if (Test-TestPart $name) { $seen[$name] = 1 + [int]$seen[$name] } }
        Assert-TestPartsAsked
    }
    Check (@($parts.Keys | Where-Object { $seen[$_] -ne 1 }).Count -eq 0) "N=$count runs every part in exactly one shard"
}
# Longest first to the least loaded shard, ties to the lower shard and in table order.
$two = Get-TestShardAssignment -Parts $parts -Count 2
Check ("$($two.beta)/$($two.gamma)/$($two.delta)/$($two.alpha)/$($two.epsilon)/$($two.zeta)" -ceq '1/2/2/2/2/1') 'two shards: beta alone, the rest together (a zero-cost tie goes to shard 1)'
$three = Get-TestShardAssignment -Parts $parts -Count 3
Check ("$($three.beta)/$($three.gamma)/$($three.delta)/$($three.alpha)/$($three.epsilon)" -ceq '1/2/3/2/3') 'three shards: longest first, equal costs in table order'
Check ((@((Get-TestShardAssignment -Parts $parts -Count 3).Values) -join ',') -ceq (@($three.Values) -join ',')) 'the deal is deterministic'

# The plain run runs everything; -Part runs only the named parts.
Initialize-TestShard -Parts $parts
Check (@(Get-TestPartsSelected).Count -eq $parts.Count) 'no -Shard: every part'
Initialize-TestShard -Parts $parts -Part 'gamma,alpha'
Check ((@(Get-TestPartsSelected) -join ',') -ceq 'alpha,gamma' -and (Test-TestPart 'gamma') -and !(Test-TestPart 'beta')) '-Part selects by name'
Refuses { Assert-TestPartsAsked } 'never asked for: alpha, delta, epsilon, zeta'

# Refusals: a bad shard, an unknown part (in the table or asked for), a bad table.
foreach ($bad in @('0/2', '3/2', '1/0', '1/65', '01/2', '1-2', '1/2/3', 'x')) {
    Refuses { Initialize-TestShard -Parts $parts -Shard $bad } 'Bad shard'
}
Refuses { Initialize-TestShard -Parts $parts -Part 'omega' } 'Unknown test part: omega'
Refuses { Initialize-TestShard -Parts $parts -Shard '1/2' -Part 'alpha' } 'not both'
Initialize-TestShard -Parts $parts -Shard '1/2'
Refuses { Test-TestPart 'omega' } 'Unknown test part: omega'
Refuses { Initialize-TestShard -Parts ([ordered]@{}) } 'at least one part'
Refuses { Initialize-TestShard -Parts ([ordered]@{ 'Bad Name' = 1 }) } 'Bad test part name'
Write-Host "test_shards_tests: $checks checks passed"
