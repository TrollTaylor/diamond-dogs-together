#Requires -Version 7
# The Release receipt's coverage against this build's real test list (M7 review, wp/seat-shoot): every test CMake
# registers must be audited (Get-WorkerTests, Get-WorkerReleaseOnlyTests) or excluded by name (Get-WorkerPlan's
# excluded list), or tools/package.ps1 -Friend (Invoke-WorkerChecks -Profile Release, Assert-WorkerCoverage) throws.
# partner_seat_gun_tests was registered without either and only the friend package would have found it.
# Reads ctest-test-names.txt (cmake/test_labels.cmake writes it at configure time) from the working directory CTest
# gives this test (the build directory). Pure: no build, process launch, game or input APIs.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scriptPath = Join-Path $repo 'tools\worker\Invoke-WorkerChecks.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Worker runner parse failed' }
$pure = @('Get-WorkerExperiments', 'Get-WorkerReleaseExperiments', 'Get-WorkerTests', 'Get-WorkerShardedTests', 'Get-WorkerExcludedShardedTests', 'Get-WorkerShardNames', 'Get-WorkerReleaseOnlyTests', 'Get-WorkerHash',
    'Test-WorkerUnder', 'Assert-WorkerPath', 'Get-WorkerPlan', 'Assert-WorkerCoverage')
foreach ($name in $pure) {
    $definitions = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name }, $true))
    if ($definitions.Count -ne 1) { throw "Missing or duplicate pure boundary: $name" }
    . ([scriptblock]::Create($definitions[0].Extent.Text))
}
$checks = 0
function Check([bool]$Value, [string]$Message) { $script:checks++; if (!$Value) { throw "Assertion failed: $Message" } }

$listPath = Join-Path (Get-Location).Path 'ctest-test-names.txt'
Check (Test-Path -LiteralPath $listPath -PathType Leaf) "the configure-time test list exists ($listPath; reconfigure the build)"
$names = @([IO.File]::ReadAllLines($listPath) | ForEach-Object { $_.Trim() } | Where-Object { $_ })
# Not vacuous: the list is this build's (this test and the partner fixtures are in it), with no duplicate names.
Check ($names -ccontains 'worker_coverage_tests' -and $names -ccontains 'partner_driver_tests' -and $names.Count -ge 100) 'the list names this build''s tests'
Check (@($names | Sort-Object -Unique -CaseSensitive).Count -eq $names.Count) 'no duplicate test names'

$release = Get-WorkerPlan $repo 'Release' 'Release' @() $false ''
Check ($release.profile -ceq 'Release' -and @($release.excluded).Count -gt 0) 'the Release plan with its excluded list'
$inventory = [pscustomobject]@{ tests = @($names | ForEach-Object { [pscustomobject]@{ name = $_; command = @('x.exe'); properties = @() } }) }
# The check itself refuses an unclassified name (so a pass below means something).
$probe = [pscustomobject]@{ tests = @(@($release.tests) + @($release.excluded) + @('unclassified_probe_tests') |
    ForEach-Object { [pscustomobject]@{ name = $_; command = @('x.exe'); properties = @() } }) }
$caught = $null
try { Assert-WorkerCoverage $probe $release } catch { $caught = $_ }
Check ($null -ne $caught -and $caught.Exception.Message -match 'neither audits nor excludes: unclassified_probe_tests') 'an unclassified test is refused'

# Every name at once (Assert-WorkerCoverage stops at the first), then the receipt's own check.
$missing = @($names | Where-Object { @($release.tests) -cnotcontains $_ -and @($release.excluded) -cnotcontains $_ })
Check ($missing.Count -eq 0) ("the Release receipt neither audits nor excludes: " + ($missing -join ', ') +
    ' (add each to Get-WorkerTests in tools/worker/Invoke-WorkerChecks.ps1, or to the plan''s excluded list with its reason)')
Assert-WorkerCoverage $inventory $release
Check $true 'the Release receipt covers every registered test'
Write-Host "worker_coverage_tests: $checks checks passed ($($names.Count) registered tests)"
