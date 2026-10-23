<#
.SYNOPSIS
Developer only: the local two-window test (two MGSV copies on one PC).
.DESCRIPTION
Not shipped: the Diamond Dogs Together player package has no scripts, and
players use DiamondDogsTogether.exe (Steam online only). This starts
tools/dev/Play-LocalCoop.ps1 with the newest Diamond Dogs Together package
that tools/package.ps1 -Friend staged under dist\ (its package\ folder, the
zip's contents), or with -PackageDirectory. The main game folder is
-GameDirectory, else the one MGSV folder with a co-op install
(mgs-coop\friend-install.json) that Steam knows. The guest copy is
-GuestDirectory, else <game folder>_guest. Repo root: Play-Offline-Coop.bat.
#>
[CmdletBinding()]
param(
    [string]$GameDirectory,
    [string]$GuestDirectory,
    [string]$PackageDirectory,
    [ValidateRange(0,3)][int]$World = 3,
    [ValidateSet('native','clips')][string]$Partner = 'native',
    [ValidateSet('','compact','full','off')][string]$Hud = '',
    [switch]$NonInteractive
)
$ErrorActionPreference = 'Stop'
trap { Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red; exit 1 }
Import-Module (Join-Path $PSScriptRoot 'FriendCommon.psm1') -Force -DisableNameChecking
$project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (!$PackageDirectory) {
    $dist = Join-Path $project 'dist'
    $choices = @()
    if (Test-Path -LiteralPath $dist -PathType Container) {
        foreach ($folder in @(Get-ChildItem -LiteralPath $dist -Directory -Filter 'DiamondDogsTogether-*')) {
            $root = Join-Path $folder.FullName 'package'
            $package = Read-FriendJson (Join-Path $root 'mgs-coop\package.json')
            if (!$package -or [string]$package.product -cne 'mgs-coop-local-package' -or [string](Get-FriendProperty $package 'layout' '') -cne 'drop-in') { continue }
            if (!(Test-Path -LiteralPath (Join-Path $root 'dinput8.dll') -PathType Leaf)) { continue }
            try {
                $prepared = if ($package.prepared_utc -is [DateTime]) { $package.prepared_utc.ToUniversalTime() }
                    else { [DateTime]::Parse([string]$package.prepared_utc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime() }
            } catch { continue }
            $choices += [pscustomobject]@{ Root = $root; Name = $folder.Name; PreparedUtc = $prepared }
        }
    }
    $selected = @($choices | Sort-Object PreparedUtc, Name -Descending | Select-Object -First 1)
    if (!$selected.Count) { throw 'Build a package first (pwsh -File tools\package.ps1 -Friend), or pass -PackageDirectory.' }
    Write-Host ('Using package: ' + $selected[0].Name)
    $PackageDirectory = $selected[0].Root
}
if (!$GameDirectory) {
    $registry = Get-RegistryMgsvCandidates
    $found = @(Find-MgsvGameFolders -SteamRoots @($registry.SteamRoots) -Candidates @($registry.Candidates) |
            Where-Object { Test-Path -LiteralPath (Join-Path $_ 'mgs-coop\friend-install.json') -PathType Leaf })
    if ($found.Count -ne 1) { throw 'Pass -GameDirectory: the main MGSV folder (set up with DiamondDogsTogether.exe once).' }
    $GameDirectory = $found[0]
}
$settings = @{ GameDirectory = $GameDirectory; PackageDirectory = $PackageDirectory; World = $World; Partner = $Partner; Hud = $Hud; NonInteractive = $NonInteractive }
if ($GuestDirectory) { $settings['GuestDirectory'] = $GuestDirectory }
& (Join-Path $PSScriptRoot 'Play-LocalCoop.ps1') @settings
exit $LASTEXITCODE
