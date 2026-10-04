<#
.SYNOPSIS
Send keyboard input to one lab game window and optionally capture it afterwards.

.DESCRIPTION
Focuses the host or guest window of the current lab run and sends scan-code key
presses (what DirectInput reads). Use it to click through title screens and
menus during automated lab runs. Synthetic input only reaches the foreground
window, so the script focuses the window first.

Key names: enter, esc, space, up, down, left, right, tab, e, f, q, r, c, x, z,
v, w, a, s, d, f8, f9, ctrl (combine: ctrl+f8). Or pass -ScanCode.

.EXAMPLE
pwsh -File tools/lab/Send-LabInput.ps1 -Role host -Keys enter -Shot title
pwsh -File tools/lab/Send-LabInput.ps1 -Role guest -Keys down,down,enter -GapMs 600
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('host', 'guest')][string]$Role,
    [string[]]$Keys = @(),
    [int]$HoldMs = 150,
    [int]$GapMs = 500,
    [string]$Shot,
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    [string]$RunDir
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force

$scan = @{
    enter = @(0x1C, $false); esc = @(0x01, $false); space = @(0x39, $false); tab = @(0x0F, $false)
    up = @(0x48, $true); down = @(0x50, $true); left = @(0x4B, $true); right = @(0x4D, $true)
    e = @(0x12, $false); f = @(0x21, $false); q = @(0x10, $false); r = @(0x13, $false); c = @(0x2E, $false)
    x = @(0x2D, $false); z = @(0x2C, $false); v = @(0x2F, $false); w = @(0x11, $false); a = @(0x1E, $false)
    s = @(0x1F, $false); d = @(0x20, $false); f8 = @(0x42, $false); f9 = @(0x43, $false); ctrl = @(0x1D, $false)
}

$config = Get-LabConfig -ConfigPath $ConfigPath
$current = Get-CurrentRun -Config $config -RunDir $RunDir
$dir = if ($Role -eq 'host') { $config.Paths.hostDir } else { $config.Paths.guestDir }
$process = Select-ProcessInDirectory -Processes @(Get-Process mgsvtpp -ErrorAction SilentlyContinue) -Directory $dir | Where-Object { !$_.Unknown } | Select-Object -First 1
if (!$process) { throw "No mgsvtpp.exe is running from $dir." }
$handle = Get-ProcessMainWindow -Id $process.Id
if ($handle -eq [IntPtr]::Zero) { throw "[$Role] pid $($process.Id) has no main window." }
Initialize-LabWin32

# pwsh -File passes "a,b" as one string; accept both forms.
$Keys = @($Keys | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
foreach ($key in $Keys) {
    $parts = $key.ToLowerInvariant().Split('+')
    foreach ($p in $parts) { if (!$scan.ContainsKey($p)) { throw "Unknown key name: $p" } }
    if ([MgsCoopLab.User32]::IsIconic($handle)) { $null = [MgsCoopLab.User32]::ShowWindow($handle, 9) }
    # Windows refuses SetForegroundWindow from a background process unless the
    # caller just produced input; an Alt tap (scan 0x38) satisfies that rule.
    for ($attempt = 0; $attempt -lt 4 -and [MgsCoopLab.User32]::GetForegroundWindow() -ne $handle; $attempt++) {
        $null = [MgsCoopLab.User32]::Key([uint16]0x38, $false, $false)
        $null = [MgsCoopLab.User32]::Key([uint16]0x38, $false, $true)
        $null = [MgsCoopLab.User32]::SetForegroundWindow($handle)
        Start-Sleep -Milliseconds 300
    }
    if ([MgsCoopLab.User32]::GetForegroundWindow() -ne $handle) { Write-Warning "[$Role] could not focus the window for '$key'."; continue }
    # Press modifiers first, release in reverse order.
    foreach ($p in $parts) { $null = [MgsCoopLab.User32]::Key([uint16]$scan[$p][0], [bool]$scan[$p][1], $false); Start-Sleep -Milliseconds 30 }
    Start-Sleep -Milliseconds $HoldMs
    [array]::Reverse($parts)
    foreach ($p in $parts) { $null = [MgsCoopLab.User32]::Key([uint16]$scan[$p][0], [bool]$scan[$p][1], $true); Start-Sleep -Milliseconds 30 }
    Write-Output "[$Role] sent $key"
    Start-Sleep -Milliseconds $GapMs
}

if ($Shot) {
    $screens = Join-Path $current.Dir 'screens'
    New-Item -ItemType Directory -Force $screens | Out-Null
    $path = Join-Path $screens ("{0}-{1}-{2}.png" -f $Shot, $Role, (Get-Date).ToString('HHmmss'))
    $capture = Save-WindowCapture -Handle $handle -Path $path
    Write-Output "[$Role] screenshot $($capture.Path) ($($capture.Method))"
}
