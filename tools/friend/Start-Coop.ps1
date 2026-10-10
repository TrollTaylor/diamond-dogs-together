<#
.SYNOPSIS
Opens the newest extracted friend package from this source checkout.
The package handles normal online play and the local two-window test.
#>
[CmdletBinding()]
param(
    [switch]$LocalTest,
    [string]$GameDirectory,
    [string]$GuestDirectory,
    [ValidateSet('Host','Join')][string]$Role,
    [ValidateSet('steam','udp')][string]$Transport,
    [string]$Code,
    [ValidateRange(1024,65535)][int]$Port = 47830,
    [string]$HostAddress,
    [ValidateRange(0,3)][int]$World = 3,
    [ValidateSet('native','clips')][string]$Partner = 'native',
    [ValidateSet('','both','fnptr','callback','poll')][string]$SteamDispatch = '',
    [ValidateSet('','compact','full','off')][string]$Hud = '',
    [switch]$Cleanup,
    [switch]$NonInteractive
)
$ErrorActionPreference = 'Stop'
trap { Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red; exit 1 }
$project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dist = Join-Path $project 'dist'
$choices = @()
if (Test-Path -LiteralPath $dist -PathType Container) {
    foreach ($folder in @(Get-ChildItem -LiteralPath $dist -Directory -Filter 'mgs-coop-friend-*')) {
        $manifest = Join-Path $folder.FullName 'package.json'
        $launcher = Join-Path $folder.FullName 'scripts\Play-Coop.ps1'
        $local = Join-Path $folder.FullName 'scripts\Play-LocalCoop.ps1'
        if (!(Test-Path -LiteralPath $manifest -PathType Leaf) -or !(Test-Path -LiteralPath $launcher -PathType Leaf) -or !(Test-Path -LiteralPath $local -PathType Leaf)) { continue }
        try { $package = Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json } catch { continue }
        if ($package.product -cne 'mgs-coop-local-package' -or $package.flavor -cne 'friend-play') { continue }
        try {
            $prepared = if ($package.prepared_utc -is [DateTime]) {
                $package.prepared_utc.ToUniversalTime()
            } else {
                [DateTime]::Parse([string]$package.prepared_utc, [Globalization.CultureInfo]::InvariantCulture,
                    [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
            }
        } catch { continue }
        $choices += [pscustomobject]@{FullName=$folder.FullName;Name=$folder.Name;PreparedUtc=$prepared}
    }
}
$selected = @($choices | Sort-Object PreparedUtc,Name -Descending | Select-Object -First 1)
if (!$selected.Count) { throw 'Extract the current friend package into this project''s dist folder first, then use its Play-Coop.bat.' }
Write-Host ('Using package: ' + $selected[0].Name)
$options = @{PackageDirectory=$selected[0].FullName}
foreach ($key in $PSBoundParameters.Keys) { $options[$key] = $PSBoundParameters[$key] }
& (Join-Path $selected[0].FullName 'scripts\Play-Coop.ps1') @options
exit $LASTEXITCODE
