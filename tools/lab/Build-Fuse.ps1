#Requires -Version 7
<#
.SYNOPSIS
Builds the local fuse master server (work/fuse/fuse.exe) offline, if missing.

.DESCRIPTION
Never clones or downloads anything: Go runs with GOPROXY=off, GOFLAGS=-mod=readonly
and GOTOOLCHAIN=local, so a build only succeeds from the existing source tree and
the local module cache. If the source is missing, the script says so and stops.
The lab always starts fuse with -standalone (see Start-Lab.ps1); without it fuse
backs up and patches mgsvtpp.exe on disk.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    [switch]$Force
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force

$config = Get-LabConfig -ConfigPath $ConfigPath
$source = $config.Paths.fuseSource
$exe = $config.Paths.fuseExe
$expectedCommit = [string](Get-ConfigValue $config.Hashes 'fuseCommit' '')

function Get-FuseCommit([string]$Directory) {
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (!$git -or !(Test-Path -LiteralPath (Join-Path $Directory '.git'))) { return $null }
    $commit = & $git.Source -C $Directory rev-parse HEAD 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return ([string]$commit).Trim()
}

if ((Test-Path -LiteralPath $exe -PathType Leaf) -and !$Force) {
    Write-Output "fuse is already built: $exe"
    Write-Output ("fuse.exe SHA-256: " + (Get-Sha256 $exe))
    $commit = if (Test-Path -LiteralPath $source) { Get-FuseCommit $source } else { $null }
    if ($commit -and $expectedCommit -and $commit -ne $expectedCommit) { Write-Warning "fuse source is at $commit, lab.json expects $expectedCommit." }
    return
}

if (!(Test-Path -LiteralPath (Join-Path $source 'go.mod') -PathType Leaf)) {
    throw ("fuse source not found at $source. This script never downloads code. Obtain fuse " +
        "(https://github.com/unknown321/fuse, commit $expectedCommit) yourself, then run this script again.")
}
$go = Get-Command go -ErrorAction SilentlyContinue
if (!$go) { throw 'Go is not installed or not on PATH. Install Go 1.24+ and run this script again.' }
$commit = Get-FuseCommit $source
if ($commit -and $expectedCommit -and $commit -ne $expectedCommit) { Write-Warning "fuse source is at $commit, lab.json expects $expectedCommit." }
if (!(Test-PathInside -Child $exe -Parent $source)) { throw 'lab.json fuseExe must be inside fuseSource (the build writes fuse.exe there).' }

if (!$PSCmdlet.ShouldProcess($exe, 'go build -trimpath (offline)')) { return }
$saved = @{}
foreach ($name in 'GOPROXY', 'GOFLAGS', 'GOTOOLCHAIN', 'GONOSUMDB', 'GOSUMDB') { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    $env:GOPROXY = 'off'
    $env:GOFLAGS = '-mod=readonly'
    $env:GOTOOLCHAIN = 'local'
    $env:GOSUMDB = 'off'
    Push-Location -LiteralPath $source
    try {
        & $go.Source build -trimpath -o $exe .
        if ($LASTEXITCODE -ne 0) {
            throw ('go build failed. With network access disabled, a missing module in the local cache ' +
                '(go env GOMODCACHE) fails the build; nothing was downloaded.')
        }
    } finally { Pop-Location }
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
Write-Output "Built $exe"
Write-Output ("fuse.exe SHA-256: " + (Get-Sha256 $exe))
Write-Output 'Start it only through Start-Lab.ps1 (always -standalone).'
