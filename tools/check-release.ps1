# Checks a GitHub release before (or after) it is published: it must carry both
# files tools/package.ps1 -Friend writes (AGENTS.md "Releasing"), the player zip
# DiamondDogsTogether-<version>.zip and the GPL source zip DDT-source-<version>.zip,
# with the sizes of the local files and, when GitHub reports one, their SHA-256.
# Launchers from 0.14.4 on only offer a release that has both; 0.14.3 and older
# take any DiamondDogsTogether-*.zip, so the release must never hold the player
# zip alone, nor a second asset that looks like one.
# Read only: it never uploads, edits or publishes anything.
#
#   pwsh -NoProfile -File tools/check-release.ps1 -Version 0.14.4 -PackageDir dist\DiamondDogsTogether-0.14.4-<stamp>
#
# -ReleaseJson reads a saved `gh release view v<version> --json tagName,isDraft,assets`
# instead of asking GitHub (tests/friend_package_tests.ps1 uses it).
param(
    [Parameter(Mandatory)][ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')][string]$Version,
    [string]$PackageDir,
    [string]$ReleaseJson,
    [string]$Repo = 'TrollTaylor/diamond-dogs-together'
)
$ErrorActionPreference = 'Stop'
$tag = "v$Version"
$player = "DiamondDogsTogether-$Version.zip"
$source = "DDT-source-$Version.zip"
if ($ReleaseJson) {
    $text = Get-Content -LiteralPath $ReleaseJson -Raw
} else {
    $text = (& gh release view $tag --repo $Repo --json tagName,isDraft,assets) -join "`n"
    if ($LASTEXITCODE) { throw "Could not read the release $tag of $Repo (gh release view failed)." }
}
$release = $text | ConvertFrom-Json
$problems = [Collections.Generic.List[string]]::new()
if ([string]$release.tagName -cne $tag) { $problems.Add("the release is tagged '$($release.tagName)', not $tag") }
$assets = @($release.assets)
foreach ($name in @($player, $source)) {
    $found = @($assets | Where-Object { [string]$_.name -ceq $name })
    if ($found.Count -ne 1) { $problems.Add("$name is not attached"); continue }
    if (!$PackageDir) { continue }
    $local = Join-Path $PackageDir $name
    if (!(Test-Path -LiteralPath $local -PathType Leaf)) { $problems.Add("$name is not in $PackageDir"); continue }
    $size = (Get-Item -LiteralPath $local).Length
    if ([int64]$found[0].size -ne $size) { $problems.Add("$name on GitHub is $($found[0].size) bytes, the local file $size bytes") }
    if ([string]$found[0].digest -match '^sha256:([0-9a-fA-F]{64})$') {
        $hash = (Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash
        if ($Matches[1].ToUpperInvariant() -cne $hash) { $problems.Add("$name on GitHub has SHA-256 $($Matches[1].ToUpperInvariant()), the local file $hash") }
    }
}
# The 0.14.3 updater takes the first DiamondDogsTogether-*.zip / mgs-coop-friend-*.zip it sees.
foreach ($other in @($assets | Where-Object { ([string]$_.name -like 'DiamondDogsTogether-*.zip' -or [string]$_.name -like 'mgs-coop-friend-*.zip') -and [string]$_.name -cne $player })) {
    $problems.Add("$($other.name) looks like a player zip too (launchers up to 0.14.3 may install it)")
}
if ($problems.Count) { throw "Release $tag is not ready: " + ($problems -join '; ') + '.' }
$state = if ($release.isDraft) { 'draft' } else { 'published' }
Write-Output "Release $tag ($state) carries $player and $source$(if ($PackageDir) { ', matching the local files' })."
