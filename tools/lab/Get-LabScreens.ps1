#Requires -Version 7
<#
.SYNOPSIS
Captures each running lab game window to a PNG in the run folder.

.DESCRIPTION
Uses PrintWindow with PW_RENDERFULLCONTENT and falls back to a screen copy of
the window rectangle (CopyFromScreen) when PrintWindow fails or returns a black
image. Windows that are covered by other windows can appear covered in the
fallback; two captures a few seconds apart that differ show the window is
still rendering. Files: <run>/screens/<label>-<role>-<HHmmss>.png
#>
[CmdletBinding()]
param(
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    [string]$RunDir,
    [string]$Label = 'manual',
    [switch]$PassThru
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force

if ($Label -cnotmatch '^[A-Za-z0-9_-]{1,40}$') { throw 'Label may contain letters, digits, - and _ only.' }
$config = Get-LabConfig -ConfigPath $ConfigPath
$current = Get-CurrentRun -Config $config -RunDir $RunDir
$screens = Join-Path $current.Dir 'screens'
$time = (Get-Date).ToString('HHmmss')
$saved = @()
foreach ($name in $current.Run.instances.PSObject.Properties.Name) {
    $instance = $current.Run.instances.$name
    $id = [int](Get-ConfigValue $instance 'pid' 0)
    $process = Get-LiveProcess -Id $id -ExpectedPath ([string]$instance.exe)
    if (!$process) { Write-Warning "[$name] process $id is not running; no capture."; continue }
    $handle = Get-ProcessMainWindow -Id $id
    if ($handle -eq [IntPtr]::Zero) { Write-Warning "[$name] has no main window; no capture."; continue }
    $path = Join-Path $screens ("$Label-$name-$time.png")
    $capture = Save-WindowCapture -Handle $handle -Path $path
    if ($capture.Path) {
        Write-Host "[$name] saved $($capture.Path) ($($capture.Method))"
        $saved += $capture.Path
    } else { Write-Warning "[$name] capture failed: $($capture.Reason)" }
}
if ($PassThru) { return $saved }
