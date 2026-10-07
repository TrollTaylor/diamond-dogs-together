<#
.SYNOPSIS
Send keyboard, mouse-button or relative mouse input to one lab game window and optionally capture it afterwards.

.DESCRIPTION
Focuses the host or guest window of the current lab run and sends scan-code key
presses (what DirectInput reads) or left/right mouse-button transitions. Use it to click through title screens and
menus during automated lab runs. Synthetic input only reaches the foreground
window, so the script focuses the window first.

Key names: enter, esc, space, up, down, left, right, tab, e, f, q, r, c, x, z,
v, w, a, s, d, f8, f9, f10, f11, ctrl, shift (LabCommon Get-LabScanCodeTable),
mouseleft, mouseright. Mouse buttons do not move the cursor.
A key spec is one name or several joined with '+' (pressed together in order,
released in reverse: ctrl+f8, shift+w), optionally followed by ':<ms>' to hold
it that long instead of -HoldMs (':<ms>' 1..60000; -HoldMs 0..60000, 0 = tap
as before): w:4000 walks forward for 4 s, w+a:6000 walks a circle for 6 s
(spike plan E0/E6/E10). Mouse buttons can combine with keyboard keys:
mouseright+mouseleft:1000 aims and fires; mouseright+r:300 requests reload while
aiming. Every button pressed is released in a finally block, so Ctrl+C or an
error during a hold never leaves a button down.

For camera/turn tests after the gameplay window is ready, -MouseDeltaX and
-MouseDeltaY send a signed relative mouse move (-2000..2000 each). -MouseRepeat
repeats the same move 1..8 times, with -MouseIntervalMs (1..2000, default 150)
between moves while the first combination remains held. -MouseStartDelayMs
(0..2000, default 0) waits once after that combination is pressed and before the
first move, allowing aim to settle. Foreground is checked after the wait and
before each move. Movement occurs only in the first combination, or alone when
-Keys is empty. A repeat above 1 requires a nonzero movement axis.
An initial delay above 0 also requires a nonzero movement axis.
The values are mouse counts; the resulting angle depends on game sensitivity.
Movement uses MOVE only, with no implicit button, wheel or absolute-position flag.

.EXAMPLE
pwsh -File tools/lab/Send-LabInput.ps1 -Role host -Keys enter -Shot title
pwsh -File tools/lab/Send-LabInput.ps1 -Role guest -Keys down,down,enter -GapMs 600
pwsh -File tools/lab/Send-LabInput.ps1 -Role host -Keys w+a:6000
pwsh -File tools/lab/Send-LabInput.ps1 -Role guest -Keys w:4000 -Shot walked
pwsh -File tools/lab/Send-LabInput.ps1 -Role host -Keys shift+w:3000,c
pwsh -File tools/lab/Send-LabInput.ps1 -Role host -Keys mouseright+mouseleft:1000
pwsh -File tools/lab/Send-LabInput.ps1 -Role host -MouseDeltaX 250 -Shot turned
pwsh -File tools/lab/Send-LabInput.ps1 -Role guest -Keys mouseright:500 -MouseDeltaX -250
pwsh -File tools/lab/Send-LabInput.ps1 -Role host -Keys mouseright:500 -MouseDeltaX 1500 -MouseRepeat 2
pwsh -File tools/lab/Send-LabInput.ps1 -Role host -Keys mouseright:500 -MouseDeltaX 1500 -MouseRepeat 2 -MouseStartDelayMs 300
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('host', 'guest')][string]$Role,
    [string[]]$Keys = @(),
    [ValidateRange(0, 60000)][int]$HoldMs = 150,
    [ValidateRange(-2000, 2000)][int]$MouseDeltaX = 0,
    [ValidateRange(-2000, 2000)][int]$MouseDeltaY = 0,
    [ValidateRange(1, 8)][int]$MouseRepeat = 1,
    [ValidateRange(1, 2000)][int]$MouseIntervalMs = 150,
    [ValidateRange(0, 2000)][int]$MouseStartDelayMs = 0,
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
$specs = @(Get-LabInputPlan -Keys $Keys -HoldMs $HoldMs -MouseDeltaX $MouseDeltaX -MouseDeltaY $MouseDeltaY -MouseRepeat $MouseRepeat -MouseIntervalMs $MouseIntervalMs -MouseStartDelayMs $MouseStartDelayMs)
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
    # hold must not leave synthetic keys/buttons down for whatever window has focus.
    $pressed = [Collections.Generic.List[object]]::new()
    try {
        foreach ($p in @($spec.Keys)) {
            $pressed.Add($p)
            if ($p.Kind -ceq 'Mouse') {
                $null = [MgsCoopLab.User32]::Mouse($p.DownFlags)
            } else {
                $null = [MgsCoopLab.User32]::Key($p.Scan, $p.Extended, $false)
            }
            Start-Sleep -Milliseconds 30
        }
        if ($null -ne $spec.MouseMove) {
            if ($spec.MouseMove.StartDelayMs -gt 0) { Start-Sleep -Milliseconds $spec.MouseMove.StartDelayMs }
            for ($moveIndex = 0; $moveIndex -lt $spec.MouseMove.Repeat; $moveIndex++) {
                if ($moveIndex -gt 0) { Start-Sleep -Milliseconds $spec.MouseMove.IntervalMs }
                if ([MgsCoopLab.User32]::GetForegroundWindow() -ne $handle) { throw "[$Role] foreground changed before the relative mouse move." }
                if ([MgsCoopLab.User32]::RelativeMouse($spec.MouseMove.DeltaX, $spec.MouseMove.DeltaY) -ne 1) { throw "[$Role] relative mouse move was not accepted by SendInput." }
            }
        }
        Start-Sleep -Milliseconds $spec.HoldMs
    } finally {
        # .NET calls only: cmdlets may refuse to run while the pipeline is stopping.
        for ($i = $pressed.Count - 1; $i -ge 0; $i--) {
            if ($pressed[$i].Kind -ceq 'Mouse') {
                $null = [MgsCoopLab.User32]::Mouse($pressed[$i].UpFlags)
            } else {
                $null = [MgsCoopLab.User32]::Key($pressed[$i].Scan, $pressed[$i].Extended, $true)
            }
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
