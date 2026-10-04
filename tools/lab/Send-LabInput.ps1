<#
.SYNOPSIS
Send keyboard input to one lab game window and optionally capture it afterwards.

.DESCRIPTION
Focuses the host or guest window of the current lab run and sends scan-code key
presses (what DirectInput reads). Use it to click through title screens and
menus during automated lab runs. Synthetic input only reaches the foreground
window, so the script focuses the window first.

Key names: enter, esc, space, up, down, left, right, tab, e, f, q, r, c, x, z,
v, w, a, s, d, f8, f9, f10, f11, ctrl, shift (LabCommon Get-LabScanCodeTable).
A key spec is one name or several joined with '+' (pressed together in order,
released in reverse: ctrl+f8, shift+w), optionally followed by ':<ms>' to hold
it that long instead of -HoldMs (':<ms>' 1..60000; -HoldMs 0..60000, 0 = tap
as before): w:4000 walks forward for 4 s, w+a:6000 walks a circle for 6 s
(spike plan E0/E6/E10). Every key pressed is released in a finally block, so
Ctrl+C or an error during a hold never leaves a key down.

.EXAMPLE
pwsh -File tools/lab/Send-LabInput.ps1 -Role host -Keys enter -Shot title
pwsh -File tools/lab/Send-LabInput.ps1 -Role guest -Keys down,down,enter -GapMs 600
pwsh -File tools/lab/Send-LabInput.ps1 -Role host -Keys w+a:6000
pwsh -File tools/lab/Send-LabInput.ps1 -Role guest -Keys w:4000 -Shot walked
pwsh -File tools/lab/Send-LabInput.ps1 -Role host -Keys shift+w:3000,c
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

$config = Get-LabConfig -ConfigPath $ConfigPath
$current = Get-CurrentRun -Config $config -RunDir $RunDir
$dir = if ($Role -eq 'host') { $config.Paths.hostDir } else { $config.Paths.guestDir }
$process = Select-ProcessInDirectory -Processes @(Get-Process mgsvtpp -ErrorAction SilentlyContinue) -Directory $dir | Where-Object { !$_.Unknown } | Select-Object -First 1
if (!$process) { throw "No mgsvtpp.exe is running from $dir." }
$handle = Get-ProcessMainWindow -Id $process.Id
if ($handle -eq [IntPtr]::Zero) { throw "[$Role] pid $($process.Id) has no main window." }
Initialize-LabWin32

# pwsh -File passes "a,b" as one string; accept both forms. Every spec is
# validated before the first key is sent.
$Keys = @($Keys | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$specs = @($Keys | ForEach-Object { ConvertFrom-LabKeySpec -Spec $_ -DefaultHoldMs $HoldMs })
foreach ($spec in $specs) {
    $key = $spec.Spec
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
    # Press in order (modifiers first), hold, release in reverse order. The
    # release runs in finally: Ctrl+C (pipeline stop) or an error during a long
    # hold must not leave synthetic keys down for whatever window has focus.
    $pressed = [Collections.Generic.List[object]]::new()
    try {
        foreach ($p in @($spec.Keys)) {
            $pressed.Add($p)
            $null = [MgsCoopLab.User32]::Key($p.Scan, $p.Extended, $false)
            Start-Sleep -Milliseconds 30
        }
        Start-Sleep -Milliseconds $spec.HoldMs
    } finally {
        # .NET calls only: cmdlets may refuse to run while the pipeline is stopping.
        for ($i = $pressed.Count - 1; $i -ge 0; $i--) {
            $null = [MgsCoopLab.User32]::Key($pressed[$i].Scan, $pressed[$i].Extended, $true)
            [Threading.Thread]::Sleep(30)
        }
    }
    Write-Output "[$Role] sent $key (held $($spec.HoldMs) ms)"
    Start-Sleep -Milliseconds $GapMs
}

if ($Shot) {
    $screens = Join-Path $current.Dir 'screens'
    New-Item -ItemType Directory -Force $screens | Out-Null
    $path = Join-Path $screens ("{0}-{1}-{2}.png" -f $Shot, $Role, (Get-Date).ToString('HHmmss'))
    $capture = Save-WindowCapture -Handle $handle -Path $path
    Write-Output "[$Role] screenshot $($capture.Path) ($($capture.Method))"
}
