#Requires -Version 7
# Pester-free tests for tools/lab (pure helpers) and the -GameDirectory /
# -PackageDirectory installer path. Everything runs inside an owned temporary
# folder under build/; no real game folder, Steam folder or process is touched.
[CmdletBinding()]
param([string]$IniCheck, [string]$FakeProcess, [switch]$InputOnly)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectRoot 'tools/lab/LabCommon.psm1') -Force
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
$nonce = [Guid]::NewGuid().ToString('N')
$fixture = Join-Path $buildRoot ('lab-scripts-tests-' + $nonce)
$ownerMarker = Join-Path $fixture '.fixture-owner'
$script:checks = 0

function Assert-True([bool]$Condition, [string]$Message) {
    $script:checks++
    if (!$Condition) { throw "Assertion failed: $Message" }
}
function Assert-Equal($Actual, $Expected, [string]$Message) {
    $script:checks++
    if ($Actual -cne $Expected) { throw "Assertion failed: $Message (expected '$Expected', got '$Actual')" }
}
function Assert-Throws([scriptblock]$Operation, [string]$Expected, [string]$Message) {
    $script:checks++
    $failure = $null
    try { & $Operation | Out-Null } catch { $failure = $_ }
    if (!$failure) { throw "Assertion failed: expected failure: $Message" }
    if ($Expected -and !$failure.Exception.Message.Contains($Expected)) { throw "Assertion failed: $Message (unexpected error: $($failure.Exception.Message))" }
}
function Test-LabInputSpecs {
    # Pure input planning and native INPUT construction; never calls SendInput.
    $scanTable = Get-LabScanCodeTable
    Assert-Equal "$($scanTable['shift'][0])/$($scanTable['f10'][0])/$($scanTable['f11'][0])/$($scanTable['enter'][0])/$($scanTable['up'][1])" '42/68/87/28/True' 'scan codes: shift 0x2A, f10 0x44, f11 0x57'
    function Get-KeySpecText($Spec) { "$(@($Spec.Keys | ForEach-Object { $_.Name }) -join '+')/$($Spec.HoldMs)/$(@($Spec.Keys | ForEach-Object { $_.Scan }) -join ',')" }
    Assert-Equal (Get-KeySpecText (ConvertFrom-LabKeySpec 'enter')) 'enter/150/28' 'plain key, default hold'
    Assert-Equal (Get-KeySpecText (ConvertFrom-LabKeySpec 'w:4000')) 'w/4000/17' 'hold W for 4 s'
    Assert-Equal (Get-KeySpecText (ConvertFrom-LabKeySpec 'W+A:6000')) 'w+a/6000/17,30' 'W and A together for 6 s'
    Assert-Equal (Get-KeySpecText (ConvertFrom-LabKeySpec 'ctrl+f8' -DefaultHoldMs 300)) 'ctrl+f8/300/29,66' 'combination with -HoldMs'
    Assert-Equal (Get-KeySpecText (ConvertFrom-LabKeySpec 'shift+w:3000')) 'shift+w/3000/42,17' 'shift+W (sprint) held'
    foreach ($bad in 'w:0', 'w:60001', 'w:abc', 'q+q', 'w+', ':100', 'jump', 'w:1:2', 'mousemiddle', 'mouseleft+mouseleft', 'MouseRight+mouseright:100', 'mouseleft:0', 'mouseright:60001', 'w+mouseleft+') {
        Assert-Throws { ConvertFrom-LabKeySpec $bad } '' "key spec '$bad' refused"
    }
    Assert-Equal (ConvertFrom-LabKeySpec 'enter' -DefaultHoldMs 0).HoldMs 0 '-HoldMs 0 (a tap) is still accepted, as before the key specs'
    Assert-Throws { ConvertFrom-LabKeySpec 'enter' -DefaultHoldMs -1 } 'HoldMs' 'negative -HoldMs refused'
    Assert-Throws { ConvertFrom-LabKeySpec 'enter' -DefaultHoldMs 60001 } 'HoldMs' '-HoldMs above 60 s refused'
    $left = (ConvertFrom-LabKeySpec 'mouseleft').Keys[0]
    $right = (ConvertFrom-LabKeySpec 'MOUSERIGHT:60000').Keys[0]
    Assert-Equal "$($left.Name)/$($left.Kind)/$($left.DownFlags)/$($left.UpFlags)" 'mouseleft/Mouse/2/4' 'left mouse button uses LEFTDOWN and LEFTUP'
    Assert-Equal "$($right.Name)/$($right.Kind)/$($right.DownFlags)/$($right.UpFlags)" 'mouseright/Mouse/8/16' 'right mouse button uses RIGHTDOWN and RIGHTUP'
    Assert-True (!$left.PSObject.Properties['Scan'] -and !$right.PSObject.Properties['Scan']) 'mouse buttons have no keyboard scan code'
    $combo = ConvertFrom-LabKeySpec 'ctrl+mouseright+mouseleft+w:1000'
    Assert-Equal "$(@($combo.Keys | ForEach-Object { $_.Name }) -join '+')/$($combo.HoldMs)" 'ctrl+mouseright+mouseleft+w/1000' 'mixed keyboard and mouse order and hold are preserved'
    Assert-Equal (@($combo.Keys | ForEach-Object { $_.Kind }) -join ',') 'Keyboard,Mouse,Mouse,Keyboard' 'mixed input uses explicit types'
    Assert-Equal "$($combo.Keys[0].Scan)/$($combo.Keys[3].Scan)" '29/17' 'mixed input retains keyboard scan codes'
    Assert-Equal (ConvertFrom-LabKeySpec 'mouseright+mouseleft' -DefaultHoldMs 0).HoldMs 0 'mouse combo permits default tap hold'
    Assert-Equal (ConvertFrom-LabKeySpec 'mouseright+r:300').Keys[1].Name 'r' 'aim and reload combination accepted'
    foreach ($name in $scanTable.Keys) {
        $keyboard = (ConvertFrom-LabKeySpec $name).Keys[0]
        Assert-Equal "$($keyboard.Kind)/$($keyboard.Scan)/$($keyboard.Extended)" "Keyboard/$($scanTable[$name][0])/$($scanTable[$name][1])" "keyboard '$name' remains unchanged"
    }
    Initialize-LabWin32
    Assert-Equal ([Runtime.InteropServices.Marshal]::SizeOf([MgsCoopLab.User32]::CreateMouseInput([uint32]2))) $(if ([IntPtr]::Size -eq 8) { 40 } else { 28 }) 'INPUT retains Win32 ABI alignment'
    foreach ($flags in [uint32]2, [uint32]4, [uint32]8, [uint32]16) {
        $input = [MgsCoopLab.User32]::CreateMouseInput($flags)
        Assert-Equal $input.type 0 "mouse transition $flags uses INPUT_MOUSE"
        Assert-Equal $input.u.mi.dwFlags $flags "mouse transition $flags retains its flag"
        Assert-True ($input.u.mi.dx -eq 0 -and $input.u.mi.dy -eq 0 -and $input.u.mi.mouseData -eq 0 -and $input.u.mi.time -eq 0 -and $input.u.mi.dwExtraInfo -eq [IntPtr]::Zero) "mouse transition $flags does not move the cursor or add metadata"
    }
    foreach ($flags in [uint32]0, [uint32]1, [uint32]6, [uint32]18, [uint32]::MaxValue) {
        Assert-Throws { [MgsCoopLab.User32]::CreateMouseInput($flags) } 'Expected a left or right mouse-button transition' "unsupported mouse flags $flags refused"
    }
    foreach ($pair in @(@(0,0), @(2000,-2000), @(-2000,2000), @(250,-130), @(-1,1))) {
        $move = [MgsCoopLab.User32]::CreateRelativeMouseInput($pair[0],$pair[1])
        Assert-Equal $move.type 0 'relative move uses INPUT_MOUSE'
        Assert-Equal "$($move.u.mi.dx)/$($move.u.mi.dy)" "$($pair[0])/$($pair[1])" 'relative move retains signed axes'
        Assert-Equal $move.u.mi.dwFlags ([uint32]1) 'relative move uses MOVE only (no button/wheel/absolute flag)'
        Assert-True ($move.u.mi.mouseData -eq 0 -and $move.u.mi.time -eq 0 -and $move.u.mi.dwExtraInfo -eq [IntPtr]::Zero) 'relative move has no wheel data or metadata'
    }
    foreach ($bad in -2001,2001,[int]::MinValue,[int]::MaxValue) {
        Assert-Throws { [MgsCoopLab.User32]::CreateRelativeMouseInput($bad,0) } 'Relative mouse dx' "native relative dx bound rejects $bad"
        Assert-Throws { [MgsCoopLab.User32]::CreateRelativeMouseInput(0,$bad) } 'Relative mouse dy' "native relative dy bound rejects $bad"
    }
    Assert-Equal @(Get-LabInputPlan).Count 0 'no keys/no movement leaves screenshot-only plan empty'
    $movementOnly = @(Get-LabInputPlan -MouseDeltaX -2000 -MouseDeltaY 2000 -HoldMs 0)
    Assert-Equal $movementOnly.Count 1 'relative-only request gets one foreground-gated step'
    Assert-Equal @($movementOnly[0].Keys).Count 0 'relative-only step presses no keys or buttons'
    Assert-Equal $movementOnly[0].HoldMs 0 'relative-only step preserves explicit tap hold'
    Assert-Equal "$($movementOnly[0].MouseMove.Kind)/$($movementOnly[0].MouseMove.DeltaX)/$($movementOnly[0].MouseMove.DeltaY)/$($movementOnly[0].MouseMove.Flags)" 'MouseMove/-2000/2000/1' 'relative step carries a typed bounded movement'
    Assert-Equal "$($movementOnly[0].MouseMove.Repeat)/$($movementOnly[0].MouseMove.IntervalMs)" '1/150' 'default movement remains a single move with no interval wait'
    Assert-Equal $movementOnly[0].MouseMove.StartDelayMs 0 'default movement adds no initial wait'
    $mixedPlan = @(Get-LabInputPlan -Keys 'ctrl+mouseright:500,w:250' -MouseDeltaX 120 -MouseDeltaY -20)
    Assert-Equal $mixedPlan.Count 2 'comma-separated keyboard/mouse specs still expand'
    Assert-Equal (@($mixedPlan[0].Keys | ForEach-Object { $_.Name }) -join '+') 'ctrl+mouseright' 'mouse movement retains original combination order'
    Assert-Equal "$($mixedPlan[0].HoldMs)/$($mixedPlan[1].HoldMs)" '500/250' 'mouse movement preserves per-spec holds'
    Assert-True ($null -ne $mixedPlan[0].MouseMove -and $null -eq $mixedPlan[1].MouseMove) 'relative movement belongs only to the first combination'
    $repeatPlan = @(Get-LabInputPlan -Keys 'mouseright:5,w:10' -MouseDeltaY 1 -MouseRepeat 8 -MouseIntervalMs 2000)
    Assert-Equal "$($repeatPlan[0].MouseMove.Repeat)/$($repeatPlan[0].MouseMove.IntervalMs)" '8/2000' 'planner retains upper repeat and interval boundaries'
    Assert-True ($null -eq $repeatPlan[1].MouseMove) 'repeated movement never belongs to a later combination'
    $repeatMinimum = @(Get-LabInputPlan -MouseDeltaX 1 -MouseRepeat 1 -MouseIntervalMs 1)
    Assert-Equal "$($repeatMinimum[0].MouseMove.Repeat)/$($repeatMinimum[0].MouseMove.IntervalMs)" '1/1' 'planner accepts lower repeat and interval boundaries'
    foreach ($delay in 0,300,2000) {
        $delayedPlan = @(Get-LabInputPlan -Keys 'mouseright:5,w:10' -MouseDeltaX 1 -MouseStartDelayMs $delay)
        Assert-Equal $delayedPlan[0].MouseMove.StartDelayMs $delay 'planner retains bounded initial delay'
        Assert-True ($null -eq $delayedPlan[1].MouseMove) 'initial delay belongs only to the first movement combination'
    }
    Assert-True ($null -eq @(Get-LabInputPlan -Keys 'w')[0].MouseMove) 'existing key-only plan remains unchanged'
    Assert-Throws { Get-LabInputPlan -MouseDeltaX -2001 } 'MouseDeltaX' 'PowerShell planner bounds dx'
    Assert-Throws { Get-LabInputPlan -MouseDeltaY 2001 } 'MouseDeltaY' 'PowerShell planner bounds dy'
    foreach ($bad in 0,9) { Assert-Throws { Get-LabInputPlan -MouseDeltaX 1 -MouseRepeat $bad } 'MouseRepeat' "planner rejects repeat $bad" }
    foreach ($bad in 0,2001) { Assert-Throws { Get-LabInputPlan -MouseDeltaX 1 -MouseIntervalMs $bad } 'MouseIntervalMs' "planner rejects interval $bad" }
    foreach ($bad in -1,2001,[int]::MaxValue) { Assert-Throws { Get-LabInputPlan -MouseDeltaX 1 -MouseStartDelayMs $bad } 'MouseStartDelayMs' "planner rejects initial delay $bad" }
    Assert-Throws { Get-LabInputPlan -MouseStartDelayMs 300 } 'requires a nonzero' 'initial-delay-only request is refused'
    Assert-Throws { Get-LabInputPlan -Keys mouseright -MouseStartDelayMs 300 } 'requires a nonzero' 'held buttons cannot enable a zero-axis initial delay'
    Assert-Throws { Get-LabInputPlan -MouseRepeat 2 } 'requires a nonzero' 'repeat-only request is refused before any dispatch'
    Assert-Throws { Get-LabInputPlan -Keys 'mouseright' -MouseRepeat 2 } 'requires a nonzero' 'held buttons cannot enable a zero-axis repeated move'
    Assert-Throws { Get-LabInputPlan -MouseDeltaX 10 -Keys 'mouseleft+mouseleft' } 'repeated' 'invalid combo cannot reach a movement dispatch'
    $sendInput = [IO.File]::ReadAllText((Join-Path $projectRoot 'tools/lab/Send-LabInput.ps1'))
    Assert-True ($sendInput -match '(?s)try \{.*Key\(\$p\.Scan, \$p\.Extended, \$false\).*\} finally \{.*Key\(\$pressed\[\$i\]\.Scan, \$pressed\[\$i\]\.Extended, \$true\)') 'Send-LabInput releases every pressed key in a finally block'
    Assert-True ($sendInput -match '(?s)try \{.*Mouse\(\$p\.DownFlags\).*\} finally \{.*Mouse\(\$pressed\[\$i\]\.UpFlags\)') 'Send-LabInput releases every pressed mouse button in a finally block'
    Assert-True ($sendInput -match '(?s)GetForegroundWindow\(\) -ne \$handle\) \{ Write-Warning.*continue \}.*try \{') 'foreground gate still precedes keyboard or mouse dispatch'
    # Execute the real dispatch try/finally against a recording native substitute.
    # No user32 method, actual window, process or SendInput call is executed.
    $tokens = $null; $parseErrors = $null
    $sendAst = [Management.Automation.Language.Parser]::ParseInput($sendInput,[ref]$tokens,[ref]$parseErrors)
    Assert-Equal @($parseErrors).Count 0 'input script parses with bounded relative parameters'
    $bindOnly = [scriptblock]::Create($sendAst.ParamBlock.Extent.Text + "`n[pscustomobject]@{ X = `$MouseDeltaX; Y = `$MouseDeltaY; Repeat = `$MouseRepeat; IntervalMs = `$MouseIntervalMs; StartDelayMs = `$MouseStartDelayMs }")
    $bound = & $bindOnly -Role host -ConfigPath fixture -MouseDeltaX -2000 -MouseDeltaY 2000
    Assert-Equal "$($bound.X)/$($bound.Y)" '-2000/2000' 'real script parameter binding accepts signed boundary deltas'
    Assert-Throws { & $bindOnly -Role host -ConfigPath fixture -MouseDeltaX 2001 } 'MouseDeltaX' 'real script binding refuses excessive dx before any side effects'
    Assert-Throws { & $bindOnly -Role host -ConfigPath fixture -MouseDeltaY -2001 } 'MouseDeltaY' 'real script binding refuses excessive dy before any side effects'
    Assert-Equal "$($bound.Repeat)/$($bound.IntervalMs)" '1/150' 'real script binding preserves repeat defaults'
    Assert-Equal $bound.StartDelayMs 0 'real script binding preserves zero initial delay'
    foreach ($delay in 0,300,2000) {
        $boundDelay = & $bindOnly -Role host -ConfigPath fixture -MouseDeltaX 1 -MouseStartDelayMs $delay
        Assert-Equal $boundDelay.StartDelayMs $delay 'real script binding accepts bounded initial delays'
    }
    foreach ($bad in -1,2001,[int]::MaxValue) { Assert-Throws { & $bindOnly -Role host -ConfigPath fixture -MouseStartDelayMs $bad } 'MouseStartDelayMs' "real script binding refuses initial delay $bad before side effects" }
    foreach ($pair in @(@(1,1),@(8,2000))) {
        $boundRepeat = & $bindOnly -Role host -ConfigPath fixture -MouseDeltaX 1 -MouseRepeat $pair[0] -MouseIntervalMs $pair[1]
        Assert-Equal "$($boundRepeat.Repeat)/$($boundRepeat.IntervalMs)" "$($pair[0])/$($pair[1])" 'real script binding accepts repeat and interval boundaries'
    }
    foreach ($bad in 0,9) { Assert-Throws { & $bindOnly -Role host -ConfigPath fixture -MouseRepeat $bad } 'MouseRepeat' "real script binding refuses repeat $bad" }
    foreach ($bad in 0,2001) { Assert-Throws { & $bindOnly -Role host -ConfigPath fixture -MouseIntervalMs $bad } 'MouseIntervalMs' "real script binding refuses interval $bad" }
    if (!('MgsCoopLab.InputFixture' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
namespace MgsCoopLab {
public static class InputFixture {
    public static List<string> Calls = new List<string>();
    public static List<int> Sleeps = new List<int>();
    public static List<string> Events = new List<string>();
    public static IntPtr Foreground = new IntPtr(123);
    public static uint MoveResult = 1;
    public static int Moves = 0;
    public static int ForegroundChecks = 0;
    public static int LoseFocusAfterMove = 0;
    public static int FailMoveAt = 0;
    public static int LoseFocusAtSleepMs = 0;
    public static int FailSleepAtMs = 0;
    private static void Record(string value) { Calls.Add(value); Events.Add(value); }
    public static void Wait(int milliseconds) {
        Sleeps.Add(milliseconds); Events.Add("sleep:" + milliseconds);
        if (LoseFocusAtSleepMs > 0 && milliseconds == LoseFocusAtSleepMs) Foreground = new IntPtr(456);
        if (FailSleepAtMs > 0 && milliseconds == FailSleepAtMs) throw new InvalidOperationException("fixture sleep interrupted");
    }
    public static IntPtr GetForegroundWindow() { ForegroundChecks++; return Foreground; }
    public static uint Key(ushort scan, bool extended, bool up) { Record("key:" + scan + ":" + (up ? "up" : "down")); return 1; }
    public static uint Mouse(uint flags) { Record("mouse:" + flags); return 1; }
    public static uint RelativeMouse(int dx, int dy) {
        Record("relative:" + dx + ":" + dy); Moves++;
        if (Moves == LoseFocusAfterMove) Foreground = new IntPtr(456);
        return Moves == FailMoveAt ? 0 : MoveResult;
    }
}
}
'@
    }
    $dispatchAst = @($sendAst.FindAll({ param($node) $node -is [Management.Automation.Language.TryStatementAst] },$true))
    Assert-Equal $dispatchAst.Count 1 'fixture exercises the actual dispatch try/finally'
    $sleepFixture = 'function Start-Sleep { param([int]$Milliseconds) [MgsCoopLab.InputFixture]::Wait($Milliseconds) }'
    $dispatchText = $dispatchAst[0].Extent.Text.Replace('[MgsCoopLab.User32]','[MgsCoopLab.InputFixture]')
    $dispatch = [scriptblock]::Create($sleepFixture + "`n" + $dispatchText)
    $spec = @(Get-LabInputPlan -Keys 'ctrl+mouseright:5' -MouseDeltaX 100 -MouseDeltaY -90)[0]
    $Role = 'fixture'; $handle = [IntPtr]123
    foreach ($scenario in 'accepted','rejected','foreground_lost') {
        [MgsCoopLab.InputFixture]::Calls.Clear()
        [MgsCoopLab.InputFixture]::Sleeps.Clear()
        [MgsCoopLab.InputFixture]::Moves = 0
        [MgsCoopLab.InputFixture]::ForegroundChecks = 0
        [MgsCoopLab.InputFixture]::LoseFocusAfterMove = 0
        [MgsCoopLab.InputFixture]::FailMoveAt = 0
        [MgsCoopLab.InputFixture]::MoveResult = $(if ($scenario -eq 'rejected') { 0 } else { 1 })
        [MgsCoopLab.InputFixture]::Foreground = $(if ($scenario -eq 'foreground_lost') { [IntPtr]456 } else { $handle })
        $pressed = [Collections.Generic.List[object]]::new()
        if ($scenario -eq 'accepted') { & $dispatch }
        elseif ($scenario -eq 'rejected') { Assert-Throws { & $dispatch } 'not accepted' 'failed relative move still runs original finally' }
        else { Assert-Throws { & $dispatch } 'foreground changed' 'lost foreground prevents relative submission and runs finally' }
        $expected = if ($scenario -eq 'foreground_lost') { 'key:29:down,mouse:8,mouse:16,key:29:up' } else { 'key:29:down,mouse:8,relative:100:-90,mouse:16,key:29:up' }
        Assert-Equal ([MgsCoopLab.InputFixture]::Calls -join ',') $expected "actual dispatch releases mixed inputs in reverse order after $scenario"
        if ($scenario -eq 'accepted') { Assert-Equal ([MgsCoopLab.InputFixture]::Sleeps -join ',') '30,30,5' 'default single move adds no interval sleep' }
    }
    $spec = @(Get-LabInputPlan -Keys 'ctrl+mouseright:5' -MouseDeltaX 100 -MouseDeltaY -90 -MouseRepeat 2 -MouseIntervalMs 150)[0]
    foreach ($scenario in 'accepted','second_focus_lost','second_rejected') {
        [MgsCoopLab.InputFixture]::Calls.Clear()
        [MgsCoopLab.InputFixture]::Sleeps.Clear()
        [MgsCoopLab.InputFixture]::MoveResult = 1
        [MgsCoopLab.InputFixture]::Foreground = $handle
        [MgsCoopLab.InputFixture]::Moves = 0
        [MgsCoopLab.InputFixture]::ForegroundChecks = 0
        [MgsCoopLab.InputFixture]::LoseFocusAfterMove = $(if ($scenario -eq 'second_focus_lost') { 1 } else { 0 })
        [MgsCoopLab.InputFixture]::FailMoveAt = $(if ($scenario -eq 'second_rejected') { 2 } else { 0 })
        $pressed = [Collections.Generic.List[object]]::new()
        if ($scenario -eq 'accepted') { & $dispatch }
        elseif ($scenario -eq 'second_rejected') { Assert-Throws { & $dispatch } 'not accepted' 'second move failure releases the original held combination' }
        else { Assert-Throws { & $dispatch } 'foreground changed' 'focus lost between moves blocks the second move and releases the combination' }
        $expected = if ($scenario -eq 'second_focus_lost') { 'key:29:down,mouse:8,relative:100:-90,mouse:16,key:29:up' } else { 'key:29:down,mouse:8,relative:100:-90,relative:100:-90,mouse:16,key:29:up' }
        Assert-Equal ([MgsCoopLab.InputFixture]::Calls -join ',') $expected "repeated $scenario dispatch keeps one mouse-down/up and releases in reverse"
        Assert-Equal ([MgsCoopLab.InputFixture]::ForegroundChecks) 2 "repeated $scenario dispatch checks focus before each move"
        $expectedSleeps = if ($scenario -eq 'accepted') { '30,30,150,5' } else { '30,30,150' }
        Assert-Equal ([MgsCoopLab.InputFixture]::Sleeps -join ',') $expectedSleeps "repeated $scenario dispatch waits only between moves before the final hold"
    }
    $spec = @(Get-LabInputPlan -Keys 'ctrl+mouseright:5' -MouseDeltaX 100 -MouseDeltaY -90 -MouseRepeat 2 -MouseIntervalMs 150 -MouseStartDelayMs 300)[0]
    foreach ($scenario in 'accepted','settle_focus_lost','settle_interrupted','first_rejected') {
        [MgsCoopLab.InputFixture]::Calls.Clear()
        [MgsCoopLab.InputFixture]::Sleeps.Clear()
        [MgsCoopLab.InputFixture]::Events.Clear()
        [MgsCoopLab.InputFixture]::Foreground = $handle
        [MgsCoopLab.InputFixture]::Moves = 0
        [MgsCoopLab.InputFixture]::ForegroundChecks = 0
        [MgsCoopLab.InputFixture]::LoseFocusAfterMove = 0
        [MgsCoopLab.InputFixture]::FailMoveAt = 0
        [MgsCoopLab.InputFixture]::MoveResult = $(if ($scenario -eq 'first_rejected') { 0 } else { 1 })
        [MgsCoopLab.InputFixture]::LoseFocusAtSleepMs = $(if ($scenario -eq 'settle_focus_lost') { 300 } else { 0 })
        [MgsCoopLab.InputFixture]::FailSleepAtMs = $(if ($scenario -eq 'settle_interrupted') { 300 } else { 0 })
        $pressed = [Collections.Generic.List[object]]::new()
        if ($scenario -eq 'accepted') { & $dispatch }
        elseif ($scenario -eq 'settle_focus_lost') { Assert-Throws { & $dispatch } 'foreground changed' 'focus lost during initial delay prevents the first move and releases buttons' }
        elseif ($scenario -eq 'settle_interrupted') { Assert-Throws { & $dispatch } 'sleep interrupted' 'interrupted initial delay runs the original finally release' }
        else { Assert-Throws { & $dispatch } 'not accepted' 'failed first move after initial delay releases buttons' }
        $middle = if ($scenario -eq 'accepted') { ',relative:100:-90,sleep:150,relative:100:-90,sleep:5' } elseif ($scenario -eq 'first_rejected') { ',relative:100:-90' } else { '' }
        Assert-Equal ([MgsCoopLab.InputFixture]::Events -join ',') ("key:29:down,sleep:30,mouse:8,sleep:30,sleep:300${middle},mouse:16,key:29:up") "delayed $scenario dispatch waits after buttons down, once before movement, and releases in reverse"
        $expectedChecks = if ($scenario -eq 'accepted') { 2 } elseif ($scenario -eq 'settle_interrupted') { 0 } else { 1 }
        Assert-Equal ([MgsCoopLab.InputFixture]::ForegroundChecks) $expectedChecks "delayed $scenario keeps foreground checks immediately before movement"
    }
    [MgsCoopLab.InputFixture]::LoseFocusAtSleepMs = 0
    [MgsCoopLab.InputFixture]::FailSleepAtMs = 0
    [MgsCoopLab.InputFixture]::MoveResult = 1
    # The real plan assignment and real dispatch body prove semantic validation
    # prevents even the first button-down, without calling window APIs.
    $planAst = @($sendAst.FindAll({ param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq '$specs' },$true))
    Assert-Equal $planAst.Count 1 'fixture uses the real complete-plan assignment'
    Assert-True ($planAst[0].Extent.EndOffset -lt $dispatchAst[0].Extent.StartOffset) 'complete input plan is validated before dispatch'
    $planAndDispatch = [scriptblock]::Create($sendAst.ParamBlock.Extent.Text + "`n" + $sleepFixture + "`n" + $planAst[0].Extent.Text + "`nforeach (`$spec in `$specs) { `$pressed = [Collections.Generic.List[object]]::new(); " + $dispatchText + ' }')
    [MgsCoopLab.InputFixture]::Calls.Clear()
    Assert-Throws { & $planAndDispatch -Role host -ConfigPath fixture -Keys mouseright -MouseRepeat 2 } 'requires a nonzero' 'real planning path refuses zero-axis repeats before a button-down'
    Assert-Equal ([MgsCoopLab.InputFixture]::Calls.Count) 0 'invalid repeated movement dispatches no input'
    Assert-Throws { & $planAndDispatch -Role host -ConfigPath fixture -Keys mouseright -MouseStartDelayMs 300 } 'requires a nonzero' 'real planning path refuses zero-axis initial delay before button-down'
    Assert-Equal ([MgsCoopLab.InputFixture]::Calls.Count) 0 'invalid initial delay dispatches no input'
    [MgsCoopLab.InputFixture]::Sleeps.Clear()
    [MgsCoopLab.InputFixture]::Foreground = $handle
    [MgsCoopLab.InputFixture]::Moves = 0
    [MgsCoopLab.InputFixture]::ForegroundChecks = 0
    [MgsCoopLab.InputFixture]::LoseFocusAfterMove = 0
    [MgsCoopLab.InputFixture]::FailMoveAt = 0
    & $planAndDispatch -Role host -ConfigPath fixture -Keys 'mouseright:5,w:10' -MouseDeltaX 100 -MouseRepeat 2 -MouseIntervalMs 1
    Assert-Equal ([MgsCoopLab.InputFixture]::Calls -join ',') 'mouse:8,relative:100:0,relative:100:0,mouse:16,key:17:down,key:17:up' 'real two-combination plan repeats only in the first held combination'
    Assert-Equal ([MgsCoopLab.InputFixture]::Sleeps -join ',') '30,1,5,30,10' 'later combination keeps its original hold without repeated movement waits'
    [MgsCoopLab.InputFixture]::Calls.Clear()
    [MgsCoopLab.InputFixture]::Sleeps.Clear()
    & $planAndDispatch -Role host -ConfigPath fixture -Keys 'mouseright:5,w:10' -MouseDeltaX 100 -MouseRepeat 2 -MouseIntervalMs 1 -MouseStartDelayMs 300
    Assert-Equal ([MgsCoopLab.InputFixture]::Calls -join ',') 'mouse:8,relative:100:0,relative:100:0,mouse:16,key:17:down,key:17:up' 'real script forwards initial delay without changing input order'
    Assert-Equal ([MgsCoopLab.InputFixture]::Sleeps -join ',') '30,300,1,5,30,10' 'real script delays only once before the first move, with no delay in the later combination'
}
function Assert-InFixture([string]$Path) {
    if (!(Test-PathInside -Child $Path -Parent $fixture)) { throw "Test path escaped its fixture: $Path" }
}
function Invoke-IniCheck([string]$Mode, [string]$Value) {
    $output = & $IniCheck $Mode $Value
    if ($LASTEXITCODE -ne 0) { throw "lab_ini_check $Mode failed ($LASTEXITCODE)" }
    $map = @{}
    foreach ($line in $output) { $i = $line.IndexOf('='); if ($i -gt 0) { $map[$line.Substring(0, $i)] = $line.Substring($i + 1) } }
    return $map
}
function Remove-OwnedFixture {
    if (!(Test-Path -LiteralPath $fixture)) { return }
    if ((Split-Path -Parent $fixture) -ne $buildRoot -or !(Test-Path -LiteralPath $ownerMarker -PathType Leaf) -or
        [IO.File]::ReadAllText($ownerMarker) -ne $nonce) { throw 'Refusing cleanup: fixture ownership check failed' }
    foreach ($item in @(Get-ChildItem -LiteralPath $fixture -Force -Recurse)) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing cleanup of a fixture containing links' }
    }
    Remove-Item -LiteralPath $fixture -Recurse -Force
}

if ($InputOnly) {
    Test-LabInputSpecs
    Write-Output "PASS: $script:checks pure lab input checks (no windows or input sent)"
    return
}

if (!(Test-Path -LiteralPath $buildRoot)) { $null = New-Item -ItemType Directory -Path $buildRoot }
$null = New-Item -ItemType Directory -Path $fixture
[IO.File]::WriteAllText($ownerMarker, $nonce)

try {
    # ------------------------------------------------------------ identity
    Assert-Equal (ConvertTo-SteamId64 -AccountId 1) '76561197960265729' 'account 1'
    Assert-Equal (ConvertTo-SteamId64 -AccountId 4294967280) '76561202255233008' 'lab guest account 0xFFFFFFF0'
    Assert-Equal (ConvertTo-SteamId64 -AccountId 97930372) '76561198058196100' 'ActiveUser arithmetic'
    Assert-Throws { ConvertTo-SteamId64 -AccountId 0 } 'Invalid Steam account id' 'account 0'
    Assert-Throws { ConvertTo-SteamId64 -AccountId 4294967296 } 'Invalid Steam account id' 'account > 32 bits'
    Assert-True (Test-IndividualSteamId64 '76561202255233008') 'guest id is individual'
    Assert-True (-not (Test-IndividualSteamId64 '76561197960265728')) 'account 0 is not individual'
    Assert-True (-not (Test-IndividualSteamId64 '7656119796026572')) '16 digits refused'
    Assert-True (-not (Test-IndividualSteamId64 '103582791429521408')) 'clan id refused'
    Assert-True (-not (Test-IndividualSteamId64 '')) 'empty refused'
    Assert-True (-not (Test-IndividualSteamId64 '7656119796026572x')) 'non-digit refused'
    $now = [DateTime]::UtcNow
    Assert-Equal (Resolve-SteamAccount -ActiveUser 42 -Candidates @([pscustomobject]@{ Account = 7; LastWriteUtc = $now })) ([UInt32]42) 'ActiveUser wins'
    Assert-Equal (Resolve-SteamAccount -ActiveUser 0 -Candidates @([pscustomobject]@{ Account = 7; LastWriteUtc = $now })) ([UInt32]7) 'single userdata account'
    Assert-Throws { Resolve-SteamAccount -ActiveUser 0 -Candidates @() } 'No Steam account' 'no account'
    Assert-Throws { Resolve-SteamAccount -ActiveUser 0 -Candidates @([pscustomobject]@{ Account = 7; LastWriteUtc = $now }, [pscustomobject]@{ Account = 8; LastWriteUtc = $now.AddDays(-1) }) } 'Ambiguous' 'two accounts refused'
    # REG_DWORD ActiveUser arrives as Int32; accounts from 2^31 up are negative there.
    Assert-Equal (ConvertFrom-RegistryDword ([int]97930372)) ([UInt32]97930372) 'registry dword: positive'
    Assert-Equal (ConvertFrom-RegistryDword ([int]-5)) ([UInt32]4294967291) 'registry dword: negative Int32 is an account >= 2^31'
    Assert-Equal (ConvertFrom-RegistryDword ([int]::MinValue)) ([UInt32]2147483648) 'registry dword: Int32 minimum'
    Assert-Equal (ConvertFrom-RegistryDword ([Int64]4294967295)) ([UInt32]4294967295) 'registry dword: Int64 in range'
    Assert-Equal (ConvertFrom-RegistryDword $null) ([UInt32]0) 'registry dword: absent'
    Assert-Throws { ConvertFrom-RegistryDword ([Int64]-1) } 'Not a 32-bit' 'registry dword: out of range'
    Assert-Equal (ConvertTo-SteamId64 -AccountId (ConvertFrom-RegistryDword ([int]-5))) '76561202255233019' 'large account id resolves'
    Assert-Equal (Resolve-SteamAccount -ActiveUser (ConvertFrom-RegistryDword ([int]-5))) ([UInt32]4294967291) 'large ActiveUser accepted'

    # ------------------------------------------------------------ lab config and ini text
    $labJson = Get-LabConfig  # the repository's tools/lab/lab.json (read only)
    Assert-True ([IO.Path]::IsPathRooted($labJson.Paths.hostDir) -and [IO.Path]::IsPathRooted($labJson.Paths.runDir)) 'lab.json paths absolute'
    Assert-Equal ([int]$labJson.Ports.fuse) 6667 'fuse port'
    Assert-Equal ([string]$labJson.Identity.guestSteamId) '76561202255233008' 'guest fake id'
    Assert-Equal ([string]$labJson.Hashes.exe) '085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45' 'exe hash'

    $configText = (Get-Content -LiteralPath (Join-Path $projectRoot 'tools/lab/lab.json') -Raw | ConvertFrom-Json)
    $configText.paths.hostDir = (Join-Path $fixture 'games/MGS_TPP')
    $configText.paths.guestDir = (Join-Path $fixture 'games/MGS_TPP_guest')
    $fixtureConfigPath = Join-Path $fixture 'lab.json'
    [IO.File]::WriteAllText($fixtureConfigPath, ($configText | ConvertTo-Json -Depth 8))
    $config = Get-LabConfig -ConfigPath $fixtureConfigPath -RepoRoot $fixture
    Assert-Equal $config.Paths.runDir (Join-Path $fixture 'build\lab') 'relative runDir resolves against the repo root'
    $hostId = '76561197960265729'
    $hostSettings = Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 $hostId
    $guestSettings = Get-LabInstanceSettings -Role guest -Config $config -GameDirectory $config.Paths.guestDir -HostSteamId64 $hostId
    Assert-Equal $hostSettings.Marker 'host-v3' 'host marker'
    Assert-Equal $guestSettings.Marker 'client-v1' 'guest marker'
    Assert-Equal $guestSettings.ClientHost $hostId 'client-host = host id'
    Assert-Equal $hostSettings.SaveDir (Join-Path $config.Paths.hostDir 'mgs-coop\lab\saves') 'host save_dir'
    Assert-Equal $guestSettings.MasterUrl 'http://127.0.0.1:6667/tppstm/gate' 'fuse url'
    Assert-Throws { Get-LabInstanceSettings -Role guest -Config $config -GameDirectory $config.Paths.guestDir -HostSteamId64 '76561202255233008' } 'must differ' 'guest id equal to host id refused'
    Assert-Throws { Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 '123' } 'not a valid' 'bad host id'

    $hostIni = New-LabIniFromSettings -Settings $hostSettings
    $guestIni = New-LabIniFromSettings -Settings $guestSettings
    Assert-True (Test-LabGeneratedText $hostIni) 'generated header'
    Assert-True (-not (Test-LabGeneratedText "enabled=1`n")) 'user file not generated'
    Assert-True ($hostIni.Contains('-Disable') -and $hostIni.Contains('even without this file') -and !$hostIni.Contains('Delete this file')) 'lab.ini header: deleting lab.ini alone keeps the coop.ini redirect; points to -Disable'
    Assert-Equal (Select-NewestUserBackup -FileName 'coop.ini' -Candidates @('coop.ini.user-20261001-120000', 'coop.ini.user-20261003-090000', 'coop.ini.user-20261002-235959', 'lab.ini.user-20261009-000000', 'coop.ini.user-x', 'coop.ini.disabled-20261010-000000')) 'coop.ini.user-20261003-090000' 'newest user backup'
    Assert-True ($null -eq (Select-NewestUserBackup -FileName 'lab.ini' -Candidates @('coop.ini.user-20261001-120000'))) 'no user backup'
    $h = ConvertFrom-IniText $hostIni
    $g = ConvertFrom-IniText $guestIni
    Assert-Equal $h['role'] 'host' 'host role'
    Assert-Equal $h['self_steamid'] 'real' 'host real identity'
    Assert-Equal $h['peer_steamid'] '76561202255233008' 'host peer = guest fake id'
    Assert-Equal $h['listen_port'] '47810' 'host listen'
    Assert-Equal $h['peer_port'] '47811' 'host peer port'
    Assert-Equal $h['xinput'] 'none' 'host xinput from lab.json'
    Assert-True (-not $h.Contains('mutex_suffix')) 'host keeps the original mutex'
    Assert-Equal $g['self_steamid'] '76561202255233008' 'guest fake id'
    Assert-Equal $g['self_name'] 'Guest' 'guest name'
    Assert-Equal $g['peer_steamid'] $hostId 'guest peer = host'
    Assert-Equal $g['mutex_suffix'] '-guest' 'guest mutex suffix'
    Assert-Equal $g['listen_port'] '47811' 'guest listen'
    Assert-Equal $g['join_trigger'] '1' 'guest join trigger'
    Assert-Equal $g['loopback'] '1' 'guest loopback'
    Assert-True ($guestIni.EndsWith("`n") -and !$guestIni.Contains("`r")) 'LF line endings'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId real -ListenPort 47811 -PeerPort 47811 -SaveDir 'C:\x' } 'must differ' 'equal ports'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId real -ListenPort 80 -PeerPort 47811 -SaveDir 'C:\x' } 'Invalid lab port' 'low port'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId real -ListenPort 47810 -PeerPort 47811 -SaveDir 'relative\x' } 'absolute' 'relative save_dir'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId real -ListenPort 47810 -PeerPort 47811 -SaveDir 'C:\x' -MutexSuffix 'a b' } 'mutex_suffix' 'bad suffix'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId real -ListenPort 47810 -PeerPort 47811 -SaveDir 'C:\x' -XInput '4' } 'xinput' 'bad xinput'
    Assert-Throws { New-LabIniText -Role guest -SelfSteamId '76561197960265728' -ListenPort 47810 -PeerPort 47811 -SaveDir 'C:\x' } 'self_steamid' 'non-individual id'
    $coopText = New-CoopIniText -MasterUrl $guestSettings.MasterUrl -Character quiet
    $coop = ConvertFrom-IniText $coopText
    Assert-Equal $coop['master_url'] 'http://127.0.0.1:6667/tppstm/gate' 'coop master_url'
    Assert-Equal $coop['character'] 'quiet' 'coop character'
    Assert-True ((Test-LabGeneratedText $coopText) -and $coopText.Contains('applies even without lab.ini') -and $coopText.Contains('-Disable')) 'coop.ini header explains the lingering redirect'
    Assert-Throws { New-CoopIniText -MasterUrl 'http://127.0.0.1:6667/tppstm/main' } 'Invalid master_url' 'bad suffix url'
    Assert-Throws { New-CoopIniText -MasterUrl 'http://127.0.0.1:6667/tppstm/gate' -Character dog } 'Invalid character' 'bad character'
    $parsed = ConvertFrom-IniText ([string][char]0xFEFF + "# c`r`n; c`r`n key = value `r`nnoequals`r`n=x`r`nkey=later`r`n")
    Assert-Equal $parsed['key'] 'later' 'ini: BOM, CRLF, comments, trimming, later wins'
    Assert-Equal $parsed.Count 1 'ini: lines without key ignored'

    $urls = @('http://127.0.0.1:6667/tppstm/gate', 'https://mgstpp-game.konamionline.com/tppstm/gate', 'http://h/tppstm/gate',
        'http:///tppstm/gate', 'http://tppstm/gate', 'ftp://h/tppstm/gate', 'HTTP://h/tppstm/gate', 'http://h/tppstm/gate/',
        'http://h st/tppstm/gate', 'http://h"/tppstm/gate', ('http://' + ('a' * 50) + '/tppstm/gate'), ('http://' + ('a' * 51) + '/tppstm/gate'), '')
    if ($IniCheck) {
        # Cross-check the scripts against the native parsers in the loader.
        foreach ($url in $urls) {
            if (!$url) { continue }
            $native = Invoke-IniCheck url $url
            Assert-Equal (Test-LabUrl $url) ($native['url_status'] -eq 'ok') "Test-LabUrl matches native ValidateUrl for $url"
        }
        $file = Join-Path $fixture 'host-lab.ini'; [IO.File]::WriteAllText($file, $hostIni)
        $n = Invoke-IniCheck lab $file
        Assert-Equal $n['present'] '1' 'native: host lab present'
        Assert-Equal $n['role'] 'host' 'native: host role'
        Assert-Equal $n['loopback'] '1' 'native: host loopback'
        Assert-Equal $n['self_steamid'] '0' 'native: host keeps real identity'
        Assert-Equal $n['peer_steamid'] '76561202255233008' 'native: host peer'
        Assert-Equal $n['listen_port'] '47810' 'native: host listen'
        Assert-Equal $n['xinput'] '-1' 'native: host xinput none'
        Assert-Equal $n['save_dir'] $hostSettings.SaveDir 'native: host save_dir'
        Assert-Equal "$($n['unknown_keys'])/$($n['invalid_values'])" '0/0' 'native: host ini clean'
        $file = Join-Path $fixture 'guest-lab.ini'; [IO.File]::WriteAllText($file, $guestIni)
        $n = Invoke-IniCheck lab $file
        Assert-Equal $n['role'] 'guest' 'native: guest role'
        Assert-Equal $n['self_steamid'] '76561202255233008' 'native: guest fake id'
        Assert-Equal $n['self_name'] 'Guest' 'native: guest name'
        Assert-Equal $n['peer_steamid'] $hostId 'native: guest peer'
        Assert-Equal $n['mutex_suffix'] '-guest' 'native: guest suffix'
        Assert-Equal "$($n['listen_port'])/$($n['peer_port'])" '47811/47810' 'native: guest ports'
        Assert-Equal $n['xinput'] '0' 'native: guest pad 0'
        Assert-Equal $n['join_trigger'] '1' 'native: guest join trigger'
        Assert-Equal "$($n['unknown_keys'])/$($n['invalid_values'])" '0/0' 'native: guest ini clean'
        $file = Join-Path $fixture 'coop.ini'; [IO.File]::WriteAllText($file, $coopText)
        $n = Invoke-IniCheck coop $file
        Assert-Equal $n['master_url'] 'http://127.0.0.1:6667/tppstm/gate' 'native: coop master_url'
        Assert-Equal $n['character'] 'quiet' 'native: coop character'
        Assert-Equal $n['character_fallback'] 'dd_male' 'native: coop fallback'
        Assert-Equal "$($n['unknown_keys'])/$($n['invalid_values'])" '0/0' 'native: coop ini clean'
    } else { Write-Warning 'lab_ini_check not supplied; native cross-checks skipped.' }

    # ------------------------------------------------------------ offline lab mode and partner settings
    # docs/offline-partner-spike-plan.md 2.1, 2.5 and 4. The online texts must stay byte-identical.
    $expectedHostIni = @(
        '# Generated by tools/lab/Prepare-LabInstance.ps1 - single-PC lab harness only (docs/lab-protocol.md).',
        '# Lab mode stays on while this file exists. The lab coop.ini next to it ALSO points the',
        '# master server at the local fuse, even without this file. Switch both off (and restore',
        '# your previous files) with: pwsh -File tools/lab/Prepare-LabInstance.ps1 -Disable',
        'enabled=1', 'role=host', 'loopback=1', 'self_steamid=real', 'peer_steamid=76561202255233008',
        'listen_port=47810', 'peer_port=47811', "save_dir=$($hostSettings.SaveDir)", 'xinput=none', 'join_trigger=1') -join "`n"
    Assert-Equal $hostIni ($expectedHostIni + "`n") 'online host lab.ini text unchanged (no partner keys without -Offline)'
    $expectedCoop = @(
        '# Generated by tools/lab/Prepare-LabInstance.ps1. Player options (docs/lab-protocol.md).',
        '# master_url sends this game to the lab''s local fuse and applies even without lab.ini.',
        '# Before normal online play run: pwsh -File tools/lab/Prepare-LabInstance.ps1 -Disable',
        '# (it renames this file and restores your previous coop.ini, if there was one).',
        'master_url=http://127.0.0.1:6667/tppstm/gate', 'character=quiet', 'character_fallback=dd_male') -join "`n"
    Assert-Equal $coopText ($expectedCoop + "`n") 'online coop.ini text unchanged'
    Assert-True (!$hostSettings.Offline -and $null -eq $hostSettings.Partner -and @($g.Keys | Where-Object { $_ -match '^(net|partner|crash_probe)' }).Count -eq 0) 'online settings carry no partner keys'
    Assert-Equal "$($labJson.Ports.netHost)/$($labJson.Ports.netGuest)" '47820/47821' 'lab.json partner net ports'
    Assert-Equal "$($labJson.Offline.masterPort)/$($labJson.Offline.marker)" '47899/offline-v1' 'lab.json offline block'
    Assert-Equal "$($labJson.Characters.host)/$($labJson.Characters.guest)" 'default/quiet' 'lab.json characters of the fuse lab unchanged (guest Quiet)'
    Assert-Equal "$($labJson.Offline.characters.host)/$($labJson.Offline.characters.guest)" 'default/quiet' 'lab.json offline.characters: the guest plays Quiet (test condition), the host keeps its character'
    Assert-Equal "$($hostSettings.Character)/$($guestSettings.Character)" 'default/quiet' 'online settings keep the top-level characters'
    $pj = $labJson.Partner
    Assert-Equal "$($pj.partner)/$($pj.stage)/$($pj.source)/$($pj.apply)/$($pj.rate_hz)/$($pj.interp_ms)/$($pj.offset_cm)/$($pj.teardown)/$($pj.force_exists)/$($pj.policy_relax)/$($pj.anim_probe)/$($pj.crash_probe)/$($pj.net)" '1/0/fixed/warp/30/100/300/clear/0/0/0/1/1' 'lab.json partner defaults'
    Assert-Equal "$($pj.anim)/$($pj.aset)/$($pj.reset)/$($pj.look)" 'off/qs/n/copy' 'lab.json partner anim/look defaults keep the proven behaviour'

    $deadUrl = 'http://127.0.0.1:47899/tppstm/gate'
    $hostOff = Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 $hostId -Offline
    $guestOff = Get-LabInstanceSettings -Role guest -Config $config -GameDirectory $config.Paths.guestDir -HostSteamId64 $hostId -Offline
    Assert-Equal "$($hostOff.Marker)/$($guestOff.Marker)" 'offline-v1/offline-v1' 'offline marker on both roles'
    Assert-Equal "$($hostOff.MasterUrl)|$($guestOff.MasterUrl)" "$deadUrl|$deadUrl" 'offline master URL: closed local port (no fuse)'
    Assert-True ((Test-LabUrl $deadUrl) -and $deadUrl.Length -eq 34) 'offline URL passes Test-LabUrl (34 characters)'
    Assert-True ($hostOff.Offline -and $guestOff.Offline) 'offline settings flagged'
    Assert-Equal "$($hostOff.Character)/$($guestOff.Character)" 'default/quiet' 'offline settings: offline.characters (guest Quiet)'
    Assert-Equal "$(Get-LabDisabledMarker -Role host -Marker "offline-v1`n")/$(Get-LabDisabledMarker -Role guest -Marker 'offline-v1')" 'archive/archive' '-Disable archives offline-v1 for both roles instead of enabling legacy sessions'
    Assert-True ($null -eq (Get-LabDisabledMarker -Role host -Marker "host-v3`n") -and $null -eq (Get-LabDisabledMarker -Role guest -Marker "client-v1`n") -and
        $null -eq (Get-LabDisabledMarker -Role host -Marker "host-v1`n") -and $null -eq (Get-LabDisabledMarker -Role host -Marker '') -and
        $null -eq (Get-LabDisabledMarker -Role host -Marker 'OFFLINE-V1')) '-Disable leaves every other marker alone'
    $hostOffIni = New-LabIniFromSettings -Settings $hostOff
    $guestOffIni = New-LabIniFromSettings -Settings $guestOff
    $ho = ConvertFrom-IniText $hostOffIni
    $go = ConvertFrom-IniText $guestOffIni
    $partnerOrder = @('net', 'net_listen_port', 'net_peer_port', 'partner', 'partner_stage', 'partner_source', 'partner_apply', 'partner_anim',
        'partner_aset', 'partner_reset', 'partner_look', 'partner_rate_hz', 'partner_interp_ms', 'partner_offset_cm', 'partner_teardown',
        'partner_force_exists', 'partner_policy_relax', 'partner_anim_probe', 'crash_probe', 'debug_soldier_warp')
    Assert-Equal ((@($ho.Keys) | Select-Object -Last 20) -join ',') ($partnerOrder -join ',') 'offline lab.ini: the spec 2.1 key names (plus anim/aset/reset/look and the lab spawn placement), in table order'
    Assert-Equal "$($ho['partner_anim'])/$($ho['partner_aset'])/$($ho['partner_reset'])/$($ho['partner_look'])" 'off/qs/n/copy' 'anim/look defaults written'
    Assert-Equal "$($ho['net'])/$($ho['net_listen_port'])/$($ho['net_peer_port'])/$($ho['partner'])" '1/47820/47821/1' 'host: net=1, 47820/47821, partner=1'
    Assert-Equal "$($go['net'])/$($go['net_listen_port'])/$($go['net_peer_port'])/$($go['partner'])" '1/47821/47820/1' 'guest: net ports swapped'
    Assert-Equal "$($ho['partner_stage'])/$($ho['partner_source'])/$($ho['partner_apply'])/$($ho['partner_rate_hz'])/$($ho['partner_interp_ms'])/$($ho['partner_offset_cm'])/$($ho['partner_teardown'])" '0/fixed/warp/30/100/300/clear' 'partner defaults written'
    Assert-Equal "$($ho['partner_force_exists'])/$($ho['partner_policy_relax'])/$($ho['partner_anim_probe'])/$($ho['crash_probe'])" '0/0/0/1' 'partner switches written'
    Assert-True (!$ho.Contains('net_latency_ms') -and !$ho.Contains('net_loss_permille')) 'impairment keys only when set'
    Assert-Equal "$($go['role'])/$($go['loopback'])/$($go['self_steamid'])/$($go['mutex_suffix'])/$($go['listen_port'])/$($go['save_dir'])" "guest/1/76561202255233008/-guest/47811/$($guestSettings.SaveDir)" 'offline guest keeps the safety keys (fake identity, mutex suffix, save redirect, loopback)'
    Assert-True ((Test-LabGeneratedText $hostOffIni) -and $hostOffIni.Contains('closed local port') -and $hostOffIni.Contains('-Disable') -and !$hostOffIni.Contains('local fuse')) 'offline lab.ini header'
    Assert-True ($guestOffIni.EndsWith("`n") -and !$guestOffIni.Contains("`r")) 'offline lab.ini: LF line endings'
    $coopOff = New-CoopIniText -MasterUrl $hostOff.MasterUrl -Character $hostOff.Character -Offline
    $co = ConvertFrom-IniText $coopOff
    Assert-Equal "$($co['master_url'])|$($co['character'])" "$deadUrl|default" 'offline coop.ini: closed-port URL, default character'
    Assert-True ((Test-LabGeneratedText $coopOff) -and $coopOff.Contains('closed local port') -and $coopOff.Contains('applies even without lab.ini') -and !$coopOff.Contains('fuse')) 'offline coop.ini wording'
    Assert-Throws { New-CoopIniText -MasterUrl 'https://mgstpp-game.konamionline.com/tppstm/gate' -Offline } 'Offline master_url' 'offline coop.ini refuses a real server'
    Assert-Throws { New-CoopIniText -MasterUrl 'http://localhost:47899/tppstm/gate' -Offline } 'Offline master_url' 'offline URL must name 127.0.0.1'
    $ex = Get-LabOfflineExpectation -Role guest -Config $config
    Assert-Equal "$($ex.Marker)/$($ex.MasterPort)/$($ex.NetListenPort)/$($ex.NetPeerPort)/$($ex.MasterUrl)" "offline-v1/47899/47821/47820/$deadUrl" 'offline expectation (guest)'
    function Get-AltConfig([scriptblock]$Mutate) {
        $copy = ($configText | ConvertTo-Json -Depth 8) | ConvertFrom-Json
        & $Mutate $copy
        $path = Join-Path $fixture ('lab-alt-' + [Guid]::NewGuid().ToString('N').Substring(0, 8) + '.json')
        [IO.File]::WriteAllText($path, ($copy | ConvertTo-Json -Depth 8))
        Get-LabConfig -ConfigPath $path -RepoRoot $fixture
    }
    $alt = Get-AltConfig { param($c) $c.offline.masterPort = 48999; $c.ports.netHost = 48820; $c.ports.netGuest = 48821 }
    $altHost = Get-LabInstanceSettings -Role host -Config $alt -GameDirectory $alt.Paths.hostDir -HostSteamId64 $hostId -Offline
    Assert-Equal "$($altHost.MasterUrl)|$($altHost.Partner['net_listen_port'])|$($altHost.Partner['net_peer_port'])" 'http://127.0.0.1:48999/tppstm/gate|48820|48821' 'lab.json offline port and net ports honoured'
    Assert-Throws { Get-AltConfig { param($c) $c.offline.masterPort = 6667 } } 'must differ from ports.fuse' 'master port must not be the fuse port'
    Assert-Throws { Get-AltConfig { param($c) $c.ports.netHost = 47810 } } 'collides' 'net port must not be a loopback port'
    Assert-Throws { Get-AltConfig { param($c) $c.ports.netGuest = 47820 } } 'collides' 'the two net ports must differ'
    Assert-Throws { Get-AltConfig { param($c) $c.offline.marker = 'host-v3' } } 'offline.marker' 'offline marker restricted'
    Assert-Throws { Get-AltConfig { param($c) $c.offline.characters.guest = 'dog' } } 'offline.characters.guest' 'offline characters validated'
    $altChar = Get-AltConfig { param($c) $c.offline.characters.guest = 'quiet' }
    Assert-Equal (Get-LabInstanceSettings -Role guest -Config $altChar -GameDirectory $altChar.Paths.guestDir -HostSteamId64 $hostId -Offline).Character 'quiet' 'offline.characters honoured'
    Assert-Equal (Get-LabInstanceSettings -Role guest -Config $altChar -GameDirectory $altChar.Paths.guestDir -HostSteamId64 $hostId -Offline -CharacterOverride default).Character 'default' '-Character override replaces offline.characters'
    Assert-Throws { Get-LabInstanceSettings -Role guest -Config $altChar -GameDirectory $altChar.Paths.guestDir -HostSteamId64 $hostId -CharacterOverride default } 'offline lab mode' 'character override only offline'
    Assert-Throws { Get-LabInstanceSettings -Role guest -Config $altChar -GameDirectory $altChar.Paths.guestDir -HostSteamId64 $hostId -Offline -CharacterOverride dog } 'Invalid character override' 'character override validated'
    Assert-Equal "$(ConvertFrom-LabCharacterArgs -Items @('default') -Role guest)|$(ConvertFrom-LabCharacterArgs -Items @('guest:quiet') -Role host)|$(ConvertFrom-LabCharacterArgs -Items @('default', 'guest:quiet') -Role guest)" 'default||quiet' '-Character items per role, later wins'
    Assert-True ($null -eq (ConvertFrom-LabCharacterArgs -Items @() -Role host)) '-Character absent: lab.json stays in force'
    Assert-Throws { ConvertFrom-LabCharacterArgs -Items @('guest:dog') -Role guest } 'Invalid -Character' '-Character name validated'
    Assert-Throws { ConvertFrom-LabCharacterArgs -Items @('both:quiet') -Role guest } 'Invalid -Character' '-Character role prefix validated'
    $noChar = Get-AltConfig { param($c) $c.offline.PSObject.Properties.Remove('characters') }
    Assert-Equal (Get-LabInstanceSettings -Role guest -Config $noChar -GameDirectory $noChar.Paths.guestDir -HostSteamId64 $hostId -Offline).Character 'default' 'offline characters default to default'
    Assert-Throws { Get-AltConfig { param($c) $c.partner.stage = 7 } } 'partner_stage' 'lab.json partner values validated'
    Assert-Throws { Get-AltConfig { param($c) $c.partner | Add-Member -NotePropertyName bogus -NotePropertyValue 1 } } 'Unknown lab.json partner setting' 'unknown lab.json partner key'

    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_stage -Value 3) '3' 'stage 3'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_stage -Value 4 } 'allowed: 0..3' 'stage 4 refused'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_source -Value ORBIT } 'fixed|orbit|net' 'source is case sensitive'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_apply -Value both) 'both' 'apply both'
    Assert-Equal "$(ConvertTo-LabPartnerValue -Key partner_apply -Value xform)/$(ConvertTo-LabPartnerValue -Key partner_apply -Value hybrid)" 'xform/hybrid' 'apply xform/hybrid'
    Assert-Equal "$(ConvertTo-LabPartnerValue -Key partner_anim -Value net)/$(ConvertTo-LabPartnerValue -Key partner_aset -Value r)/$(ConvertTo-LabPartnerValue -Key partner_reset -Value ac)/$(ConvertTo-LabPartnerValue -Key partner_look -Value quiet)" 'net/r/ac/quiet' 'anim/aset/reset/look values'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_anim -Value on } 'off|mirror|net' 'anim values'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_look -Value ocelot } 'copy|net|quiet|snake' 'look values'
    # Play-OfflineCoop: the animated partner (dm=map) by default, the proven
    # Warp movement on [W] / -ProvenWarp, the input drive on [I] / -InputDrive,
    # the round 2 experiments on [M]/[A] or -Experimental; every sent line is valid.
    $playPath = Join-Path $projectRoot 'tools/lab/Play-OfflineCoop.ps1'
    $playTokens = $null; $playErrors = $null
    $playAst = [System.Management.Automation.Language.Parser]::ParseFile($playPath, [ref]$playTokens, [ref]$playErrors)
    Assert-Equal @($playErrors).Count 0 'Play-OfflineCoop parses'
    $syncAst = $playAst.Find({ param($a) $a -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $a.Name -eq 'Get-SyncTrigger' }, $true)
    Assert-True ($null -ne $syncAst) 'Play-OfflineCoop defines Get-SyncTrigger'
    $syncBody = $syncAst.Body.GetScriptBlock()
    Import-Module (Join-Path $projectRoot 'tools/lab/PartnerSpike.psm1') -Force
    $playText = [IO.File]::ReadAllText($playPath)
    $inputMatch = [regex]::Match($playText, "\`$script:InputTrigger = '([^']+)'")
    Assert-True $inputMatch.Success 'Play-OfflineCoop defines the input-drive trigger'
    $script:InputTrigger = $inputMatch.Groups[1].Value
    $playLines = @{}
    foreach ($case in @(@('warp', 'off', 'track'), @('hybrid', 'off', 'track'), @('warp', 'net', 'track'), @('hybrid', 'net', 'track'),
            @('input', 'off', 'track'), @('input', 'off', 'pin'), @('map', 'off', 'track'))) {
        $experiment = [ordered]@{ Move = $case[0]; Anim = $case[1]; Recon = $case[2] }
        $playLines["$($case[0])/$($case[1])/$($case[2])"] = & $syncBody
    }
    Assert-Equal $playLines['input/off/track'] 'stage=3 source=net apply=input anim=off input=net cam=sandbox hold=own words=hpr bits=0x2240006 isend=1 recon=track correct=3000 runaway=pin slot=body dm=off look=net' 'Play-OfflineCoop [I] line: the input drive (correction explicit, dm off, EVADE passed)'
    Assert-Equal $playLines['input/off/pin'] 'stage=3 source=net apply=input anim=off input=net cam=sandbox hold=own words=hpr bits=0x2240006 isend=1 recon=pin correct=3000 runaway=pin slot=body dm=off look=net' 'Play-OfflineCoop [P] line'
    Assert-Equal $playLines['warp/off/track'] 'stage=3 source=net apply=warp anim=off input=off isend=0 dm=off look=net' 'Play-OfflineCoop [W] line: proven Warp, input off, dm off'
    Assert-Equal $playLines['hybrid/net/track'] 'stage=3 source=net apply=hybrid reset=n anim=net aset=qs input=off isend=0 dm=off look=net' 'Play-OfflineCoop experimental line'
    Assert-Equal $playLines['map/off/track'] 'stage=3 source=net apply=xform reset=n anim=off input=off isend=0 dm=map look=net' 'Play-OfflineCoop [D] line: dm=map with xform movement'
    foreach ($line in $playLines.Values) {
        Assert-True (Test-PartnerTriggerText $line) "Play-OfflineCoop line '$line' is a valid trigger"
        Assert-True ($line.Length + 1 -le 255) "Play-OfflineCoop line '$line' fits 255 bytes"
    }
    # The I6 plan sets the correction explicitly too (an earlier I3 leaves correct=0).
    $i6 = Get-PartnerExperimentPlan -Experiment I6
    Assert-True ((@($i6.Steps | Where-Object { $_.Kind -eq 'trigger' })[0].Value) -match 'correct=3000 runaway=pin') 'I6 trigger: correct=3000 runaway=pin'
    # The status line reads the effective recon= and the input pin / stance fixes. The
    # line parser is shared with the two-PC launcher (tools/CoopPlay.psm1).
    $coopPlayText = [IO.File]::ReadAllText((Join-Path $projectRoot 'tools/CoopPlay.psm1'))
    Assert-True ($playText.Contains('Update-CoopPartnerDetails -Details $s -Line $line') -and $playText.Contains('Format-CoopPartnerDetails -Details $state[$role] -Role $role')) 'Play-OfflineCoop uses the shared partner-details reader'
    Assert-True ($coopPlayText.Contains('recon=([a-z]+); err_mm=') -and $coopPlayText.Contains('input_pin=(\d); fixes=(\d+)')) 'partner-details reader parses recon=/input_pin=/fixes='
    Assert-True ($coopPlayText.Contains("'^partner_recon=runaway;.*action=(pin|disarm)'")) 'partner-details reader: RUNAWAY only for pin/disarm'
    Assert-True ($playText.Contains("`$startMove = if (`$Experimental) { 'hybrid' } elseif (`$InputDrive) { 'input' } elseif (`$ProvenWarp) { 'warp' } else { 'map' }")) 'Play-OfflineCoop starts with the animated partner (dm=map, live pass 2026-10-05) unless -InputDrive/-Experimental/-ProvenWarp'
    Assert-True ($playText.Contains("'I' {") -and $playText.Contains("'W' {") -and $playText.Contains("'P' {") -and $playText.Contains("'D' {")) 'Play-OfflineCoop defines [I], [W], [P] and [D]'
    Assert-True ($playText.Contains("if (`$experiment.Move -eq 'map') {") -and
        $playText.Contains("`$partnerSettings = @('partner_stage=3', 'partner_source=net', 'partner_look=net', 'partner_apply=xform', 'partner_anim=off', 'partner_reset=n', 'partner_dm=map')") -and
        !$playText.Contains('= Get-PlayPartnerDefaults')) 'Play-OfflineCoop passes only the 7 dm=map keys (lab.json "partner" supplies the rest, as before)'
    Assert-True ($playText.IndexOf("if (`$LanAddress -and !`$Lan) { throw '-LanAddress applies only with -Lan.' }") -ge 0 -and
        $playText.IndexOf("if (`$LanAddress -and !`$Lan)") -lt $playText.IndexOf("Prepare-LabInstance.ps1')")) 'Play-OfflineCoop refuses -LanAddress without -Lan before preparing anything'
    $pd = Get-PlayPartnerDefaults
    Assert-Equal "$($pd['partner_apply'])/$($pd['partner_reset'])/$($pd['partner_dm'])/$($pd['partner_look'])/$($pd['partner_stage'])/$($pd['partner_source'])" 'xform/n/map/net/3/net' 'Get-PlayPartnerDefaults: dm=map (apply xform, reset n, dm map, look net, stage 3, source net)'
    Assert-True ($coopPlayText.Contains("'^partner_dmmap; phase=([a-z_]+); cur=(\d+|none);'") -and $coopPlayText.Contains("'^partner_dmmap=failed'") -and
        $coopPlayText.Contains('function Get-CoopClipName')) 'partner-details reader shows the dm=map clip'
    Assert-True ($coopPlayText.Contains("'^partner_dmmap=(released|abandoned);' -or `$line -match '^partner_stop;'") -and
        $coopPlayText.Contains("'^partner_stage=changed; stage=[0-2];'") -and $coopPlayText.Contains("if (`$Matches[1] -eq 'failed') { 'FAILED' } elseif (`$Matches[1] -eq 'idle') { '' }")) 'partner-details reader clears the clip on stop/release/stage<3 and keeps FAILED through the summaries'
    # The reader behaves as Play-OfflineCoop's status line did (fed line by line).
    $pdState = New-CoopPartnerDetails
    foreach ($line in @('x allocation_enter; mission=30050', 'partner_cfg=ready; count=2', 'partner_realized=1', 'partner_parts=loaded',
            'partner_net=stats; tx=9; rx=12; drop=0; status=live; last_age_ms=33', 'partner_look=loaded; R=1; q40=6',
            "partner_stage=changed; stage=3; apply=xform; anim=off; input=off; dm=map; look=net`r", 'partner_dmmap; phase=playing; cur=10; t=1',
            'character=allocated; slot=0; vars=0')) { Update-CoopPartnerDetails -Details $pdState -Line $line }
    Assert-Equal (Format-CoopPartnerDetails -Details $pdState -Role host) 'host: partner visible, link live (33 ms) [plays Snake, partner looks like Quiet, settings xform/off, clip stand walk]' 'shared reader: status line of a dm=map partner'
    Update-CoopPartnerDetails -Details $pdState -Line 'partner_dmmap; phase=failed; cur=none; t=2'
    Assert-Equal (Get-CoopClipName $pdState.Dm) 'FAILED (dm=map gave up; [R] re-sends)' 'shared reader: failed clip'
    Update-CoopPartnerDetails -Details $pdState -Line 'partner_stop; reason=trigger'
    Assert-Equal $pdState.Dm '' 'shared reader: stop clears the clip'
    Update-CoopPartnerDetails -Details $pdState -Line 'mission_termination=entered; x=1'
    Assert-True (!$pdState.Mission -and !$pdState.Ready -and !$pdState.Shown) 'shared reader: mission end resets the partner state'
    Assert-True ($playText.Contains("'partner_input=net', 'partner_input_send=1'") -and $playText.Contains("'partner_recon=track'") -and
        $playText.Contains("'partner_input_correct_mm=3000'") -and $playText.Contains("'partner_recon_runaway=pin'")) 'Play-OfflineCoop prepares lab.ini for the input drive (correction explicit)'
    $ovInput = ConvertFrom-LabPartnerSettingArgs -Items @('partner_apply=input', 'partner_input=net', 'partner_input_send=1', 'partner_input_cam=sandbox',
        'partner_input_hold=own', 'partner_input_words=hpr', 'partner_input_mask=0x2240006', 'partner_recon=track', 'partner_input_correct_mm=3000', 'partner_recon_runaway=pin', 'partner_input_slot=body') -Role host
    Assert-Equal "$($ovInput['partner_apply'])/$($ovInput['partner_input_hold'])/$($ovInput['partner_recon'])/$($ovInput['partner_input_slot'])/$($ovInput['partner_input_mask'])/$($ovInput['partner_input_correct_mm'])/$($ovInput['partner_recon_runaway'])" 'input/own/track/body/0x2240006/3000/pin' '-PartnerSetting: the Play-OfflineCoop input-drive settings'
    Assert-True ($playText.Contains("'partner_input_words=hpr', 'partner_input_mask=0x2240006'") -and
        $playText.Contains("'partner_input_defer=on', 'partner_input_mask=0x224005E'") -and $playText.Contains("'partner_native_fire=visual'") -and
        $playText.Contains("'partner_native_rates=mirror'") -and !$playText.Contains('0x2200006') -and !$playText.Contains('0x220004E') -and
        !$playText.Contains('0x220005E') -and !$playText.Contains('0x224004E')) 'Play-OfflineCoop passes EVADE (0x40000) in every mask, visual fire in the native gun stage (0x224005E) and mirrors the movement rates (native)'
    # Round 4 lab.ini value kinds.
    Assert-Equal "$(ConvertTo-LabPartnerValue -Key partner_recon_align_ms -Value -16)/$(ConvertTo-LabPartnerValue -Key partner_recon_align_ms -Value 150)" '-16/150' 'partner_recon_align_ms (signed)'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_recon_align_ms -Value -51 } '-50..150' 'align range'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_recon_align_ms -Value '-0' } '-50..150' 'align -0 refused'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_recon -Value on } 'off|track|pin' 'recon values'
    Assert-Equal "$(ConvertTo-LabPartnerValue -Key partner_input_wrap -Value census)/$(ConvertTo-LabPartnerValue -Key partner_recon_runaway -Value disarm)/$(ConvertTo-LabPartnerValue -Key partner_recon_nudge_mmps -Value 900)" 'census/disarm/900' 'round 4 enums and ints'
    # Native puppet lab.ini keys (docs/native-puppet.md).
    Assert-Equal "$(ConvertTo-LabPartnerValue -Key partner_drive -Value native)/$(ConvertTo-LabPartnerValue -Key partner_input_defer -Value census)/$(ConvertTo-LabPartnerValue -Key partner_input_local -Value all)/$(ConvertTo-LabPartnerValue -Key partner_native_aim -Value lookcam)" 'native/census/all/lookcam' 'native enums'
    Assert-Equal "$(ConvertTo-LabPartnerValue -Key partner_native_gun -Value $true)/$(ConvertTo-LabPartnerValue -Key partner_native_fire -Value 0)/$(ConvertTo-LabPartnerValue -Key partner_native_steer_deg -Value 30)/$(ConvertTo-LabPartnerValue -Key partner_native_trim_pct -Value 0)" '1/0/30/0' 'native bools and ints'
    Assert-Equal "$(ConvertTo-LabPartnerValue -Key partner_native_fire -Value visual)/$(ConvertTo-LabPartnerValue -Key partner_native_fire -Value $true)/$(ConvertTo-LabPartnerValue -Key partner_native_fire -Value 1)/$(ConvertTo-LabPartnerValue -Key partner_native_fire -Value off)" 'visual/visual/visual/0' 'native fire: 0 or visual'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_native_fire -Value raw } '0|visual' 'native fire values'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_drive -Value puppet } 'legacy|native' 'drive values'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_input_defer -Value yes } 'off|census|on' 'defer values'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_native_steer_deg -Value 31 } '0..30' 'steer range'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_native_trim_pct -Value 11 } '0..10' 'trim range'
    $ovNative = ConvertFrom-LabPartnerSettingArgs -Items @('partner_drive=native', 'partner_input_defer=on', 'partner_native_aim=look',
        'partner_native_gun=1', 'partner_native_fire=visual', 'partner_input_mask=0x224005E', 'partner_native_rates=mirror') -Role guest
    Assert-Equal "$($ovNative['partner_drive'])/$($ovNative['partner_input_defer'])/$($ovNative['partner_native_aim'])/$($ovNative['partner_native_gun'])/$($ovNative['partner_native_fire'])/$($ovNative['partner_input_mask'])/$($ovNative['partner_native_rates'])" 'native/on/look/1/visual/0x224005e/mirror' '-PartnerSetting: the native gun-stage settings'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_native_rates -Value copy } 'off|log|mirror' 'rates values'

    # Round 3 lab.ini value kinds.
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_input -Value net) 'net' 'partner_input enum'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_input_words -Value rp0) 'rp0' 'partner_input_words'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_input_words -Value xyz } '3 of 0prhn' 'bad words refused'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_input_mask -Value '0x1F') '0x1f' 'mask hex lowercased'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_input_mask -Value 6) '6' 'mask decimal'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_input_mask -Value '0xZ' } 'decimal or 0x hex' 'bad mask refused'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_input_stick -Value '90:30') '90:30' 'stick deg:pct'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_input_stick -Value '360:0' } '<0..359>:<0..100>' 'bad stick refused'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_input_correct_mm -Value 10001 } '0..10000' 'correct over max refused'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_dm -Value probe) 'probe' 'partner_dm enum'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_dm -Value map) 'map' 'partner_dm=map'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_dm_map -Value '0,10,11,5,1,3,4,4,7,6,6,6') '0,10,11,5,1,3,4,4,7,6,6,6' 'partner_dm_map default table'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_dm_map -Value '26,27,28,29,30,0,1,2,3,4,5,6') '26,27,28,29,30,0,1,2,3,4,5,6' 'partner_dm_map receiver probes accepted'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_dm_map -Value '12,12,12,12,12,12,12,12,12,12,12,12') '12,12,12,12,12,12,12,12,12,12,12,12' 'partner_dm_map up to 12'
    foreach ($bad in @('0,10,11,5,1,3,4,4,7,6,6', '0,10,11,5,1,3,4,4,7,6,6,31', '0,10,11,5,1,3,4,4,7,6,6,01', '0,10,11,5,1,3,4,4,7,6,6,6,', '0, 10,11,5,1,3,4,4,7,6,6,6')) {
        Assert-Throws { ConvertTo-LabPartnerValue -Key partner_dm_map -Value $bad } '12 comma-separated' "partner_dm_map '$bad' refused"
    }
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_dm_speed -Value '300:2600:5600') '300:2600:5600' 'partner_dm_speed default'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_dm_speed -Value '2000:10000:20000') '2000:10000:20000' 'partner_dm_speed upper bounds'
    foreach ($bad in @('300:400:5600', '100:2600:5600', '0300:2600:5600', '300:2600', '300:2600:2800', '300:2600:20001', '2001:2600:5600')) {
        Assert-Throws { ConvertTo-LabPartnerValue -Key partner_dm_speed -Value $bad } 'idle 200..2000' "partner_dm_speed '$bad' refused"
    }
    $ovMap = ConvertFrom-LabPartnerSettingArgs -Items @('partner_dm=map,partner_dm_map=0,12,11,5,1,3,4,4,2,6,6,6,guest:partner_dm_speed=300:1500:5600') -Role guest
    Assert-Equal (@($ovMap.Keys | ForEach-Object { "$_=$($ovMap[$_])" }) -join ' ') 'partner_dm=map partner_dm_map=0,12,11,5,1,3,4,4,2,6,6,6 partner_dm_speed=300:1500:5600' '-PartnerSetting: a comma inside the dm=map table value'
    $pjMap = ConvertFrom-LabPartnerJson -Partner ([pscustomobject]@{ dm = 'map'; dm_map = '0,10,11,5,1,3,4,4,7,6,6,6'; dm_speed = '300:2600:5600' })
    Assert-Equal "$($pjMap['partner_dm'])/$($pjMap['partner_dm_map'])/$($pjMap['partner_dm_speed'])" 'map/0,10,11,5,1,3,4,4,7,6,6,6/300:2600:5600' 'lab.json dm=map short names'
    # GANI bound guard: resident movement, finite turns and manual graph probes agree.
    $syncLua = [IO.File]::ReadAllText((Join-Path $projectRoot 'lua/coop/partner_sync.lua'))
    $labHeader = [IO.File]::ReadAllText((Join-Path $projectRoot 'native/include/mgscoop/lab_config.hpp'))
    Assert-True ($syncLua.Contains('integer(dg, 0, 30)') -and $labHeader.Contains('kPartnerDmGaniMax = 30')) 'GANI range: Lua integer(dg, 0, 30) and kPartnerDmGaniMax = 30'
    $ganiTable = [regex]::Match($syncLua, 'local GANI = \{\[0\] = (?<body>[\s\S]*?)\}\r?\n')
    Assert-True $ganiTable.Success 'partner_sync.lua defines the GANI table'
    $ganiBody = $ganiTable.Groups['body'].Value
    Assert-Equal "$([regex]::Matches($ganiBody, '\.\. "snapnon_').Count)/$([regex]::Matches($ganiBody, '"state[A-Za-z]+"').Count)/$([regex]::Matches($ganiBody, '\.\. "snap(hg00|ar00|ar01|sr02)/').Count)" '19/7/5' 'GANI has 31 entries: 19 snapnon clips, 7 state names, 5 receiver clips'
    $ovInput = ConvertFrom-LabPartnerSettingArgs -Items @('partner_input=net', 'partner_input_holdlog=1', 'partner_dm=on') -Role host
    Assert-Equal (@($ovInput.Keys | ForEach-Object { "$_=$($ovInput[$_])" }) -join ' ') 'partner_input=net partner_input_holdlog=1 partner_dm=on' '-PartnerSetting: round 3 overrides'
    $pjInput = ConvertFrom-LabPartnerJson -Partner ([pscustomobject]@{ input = 'net'; dm = 'probe'; look_gate = 0 })
    Assert-Equal "$($pjInput['partner_input'])/$($pjInput['partner_dm'])/$($pjInput['partner_look_gate'])" 'net/probe/0' 'lab.json round 3 short names'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_reset -Value none } 'n|a|c|ac' 'reset values'
    $pjAnim = ConvertFrom-LabPartnerJson -Partner ([pscustomobject]@{ anim = 'net'; partner_look = 'net'; aset = 'qs' })
    Assert-Equal "$(@($pjAnim.Keys) -join ',')/$($pjAnim['partner_anim'])/$($pjAnim['partner_look'])" 'partner_anim,partner_look,partner_aset/net/net' 'lab.json short names for the anim/look keys'
    $ovAnim = ConvertFrom-LabPartnerSettingArgs -Items @('partner_apply=hybrid', 'partner_anim=net', 'partner_aset=qs', 'partner_look=net') -Role host
    Assert-Equal (@($ovAnim.Keys | ForEach-Object { "$_=$($ovAnim[$_])" }) -join ' ') 'partner_apply=hybrid partner_anim=net partner_aset=qs partner_look=net' '-PartnerSetting: the Play-OfflineCoop experiment settings'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_rate_hz -Value 0 } '1..60' 'rate 0 refused'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_rate_hz -Value 61 } '1..60' 'rate 61 refused'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_interp_ms -Value 501 } '0..500' 'interp 501 refused'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_interp_ms -Value 0) '0' 'interp 0 (latest snapshot)'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_offset_cm -Value 49 } '50..1000' 'offset 49 refused'
    Assert-Equal (ConvertTo-LabPartnerValue -Key partner_offset_cm -Value '0100') '100' 'leading zero normalized'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_teardown -Value none } 'clear|keep' 'teardown values'
    Assert-Equal "$(ConvertTo-LabPartnerValue -Key crash_probe -Value $true)/$(ConvertTo-LabPartnerValue -Key partner_force_exists -Value 'false')" '1/0' 'booleans written as 0/1'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_anim_probe -Value 2 } '0 or 1' 'boolean 2 refused'
    Assert-Throws { ConvertTo-LabPartnerValue -Key net_latency_ms -Value 2001 } '0..2000' 'latency capped like the native parser'
    Assert-Throws { ConvertTo-LabPartnerValue -Key net_loss_permille -Value -1 } '0..1000' 'negative loss refused'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_speed -Value 1 } 'Unknown partner' 'unknown key refused'
    Assert-Throws { ConvertTo-LabPartnerIni -Values @{ net = 1 } } 'needs net_listen_port' 'net=1 needs both ports'
    Assert-Throws { ConvertTo-LabPartnerIni -Values @{ net = 1; net_listen_port = 47820; net_peer_port = 47820 } } 'must differ' 'equal net ports refused'
    Assert-Throws { ConvertTo-LabPartnerIni -Values @{ net = 1; net_listen_port = 47810; net_peer_port = 47821 } -ListenPort 47810 -PeerPort 47811 } 'collides' 'net port equal to listen_port refused'
    Assert-Throws { ConvertTo-LabPartnerIni -Values @{ NET = 1 } } 'Unknown partner' 'key names are exact (lower case)'
    $ordered = ConvertTo-LabPartnerIni -Values @{ crash_probe = 0; partner_stage = 2; net = 0 }
    Assert-Equal (@($ordered.Keys) -join ',') 'net,partner_stage,crash_probe' 'canonical key order'
    $pjson = ConvertFrom-LabPartnerJson -Partner ([pscustomobject]@{ comment = 'x'; stage = 2; partner_apply = 'slot' })
    Assert-Equal "$(@($pjson.Keys) -join ',')/$($pjson['partner_stage'])/$($pjson['partner_apply'])" 'partner_stage,partner_apply/2/slot' 'lab.json short and full partner names'
    Assert-Throws { ConvertFrom-LabPartnerJson -Partner ([pscustomobject]@{ net_listen_port = 48000 }) } 'ports.netHost' 'ports stay in lab.json ports'
    $ov = ConvertFrom-LabPartnerSettingArgs -Items @('guest:net_latency_ms=100,guest:net_loss_permille=200', 'partner_stage=2') -Role guest
    Assert-Equal (@($ov.Keys | ForEach-Object { "$_=$($ov[$_])" }) -join ' ') 'net_latency_ms=100 net_loss_permille=200 partner_stage=2' '-PartnerSetting: guest items and shared items'
    $ov = ConvertFrom-LabPartnerSettingArgs -Items @('guest:net_latency_ms=100', 'partner_stage=2', 'host:partner_force_exists=true') -Role host
    Assert-Equal (@($ov.Keys | ForEach-Object { "$_=$($ov[$_])" }) -join ' ') 'partner_stage=2 partner_force_exists=1' '-PartnerSetting: host items'
    Assert-Throws { ConvertFrom-LabPartnerSettingArgs -Items 'net_listen_port=48000' -Role host } 'cannot change' '-PartnerSetting cannot move the ports'
    Assert-Throws { ConvertFrom-LabPartnerSettingArgs -Items 'other:partner_stage=1' -Role host } 'Invalid -PartnerSetting' '-PartnerSetting role prefix'
    Assert-Throws { ConvertFrom-LabPartnerSettingArgs -Items 'partner_stage=9' -Role host } 'allowed: 0..3' '-PartnerSetting values validated'
    Assert-Throws { Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 $hostId -PartnerOverrides @{ partner_stage = '1' } } 'only to the offline' 'partner overrides need -Offline'
    $e7 = Get-LabInstanceSettings -Role guest -Config $config -GameDirectory $config.Paths.guestDir -HostSteamId64 $hostId -Offline -PartnerOverrides (ConvertFrom-LabPartnerSettingArgs -Items 'guest:net_latency_ms=100,guest:net_loss_permille=200' -Role guest)
    $e7i = ConvertFrom-IniText (New-LabIniFromSettings -Settings $e7)
    Assert-Equal "$($e7i['net_latency_ms'])/$($e7i['net_loss_permille'])/$($e7i['partner_stage'])" '100/200/0' 'E7 impairment written for the guest'
    Assert-Throws { New-LabIniText -Role host -SelfSteamId real -ListenPort 47810 -PeerPort 47811 -SaveDir 'C:\x' -Partner @{ partner_stage = 5 } } 'partner_stage' 'New-LabIniText validates partner values'

    # Start-Lab -Offline preflight on lab.ini.
    $hostExpect = Get-LabOfflineExpectation -Role host -Config $config
    Assert-Equal @(Get-LabOfflineIniProblems -Role host -Lab $ho -Expectation $hostExpect).Count 0 'offline preflight accepts the generated host lab.ini'
    Assert-Equal @(Get-LabOfflineIniProblems -Role guest -Lab $go -Expectation (Get-LabOfflineExpectation -Role guest -Config $config)).Count 0 'offline preflight accepts the generated guest lab.ini'
    function Get-OfflineProblemsWith([string]$Key, [string]$Value) {
        $copy = [ordered]@{}
        foreach ($k in $ho.Keys) { $copy[$k] = $ho[$k] }
        $copy[$Key] = $Value
        @(Get-LabOfflineIniProblems -Role host -Lab $copy -Expectation $hostExpect) -join ' | '
    }
    Assert-True ((Get-OfflineProblemsWith 'net' '0').Contains('net is not 1')) 'offline preflight: net=0 refused'
    Assert-True ((Get-OfflineProblemsWith 'partner' '0').Contains('partner is not 1')) 'offline preflight: partner=0 refused'
    Assert-True ((Get-OfflineProblemsWith 'net_listen_port' '47821').Contains('47820/47821')) 'offline preflight: wrong net port refused'
    Assert-True ((Get-OfflineProblemsWith 'partner_stage' '7').Contains('Invalid lab.ini partner_stage=7')) 'offline preflight: invalid partner value refused'
    Assert-Equal @(Get-LabOfflineIniProblems -Role host -Lab $h -Expectation $hostExpect).Count 3 'online lab.ini fails the offline preflight (net, partner, ports)'
    Assert-Equal @(Get-LabOfflineIniProblems -Role host -Lab $null -Expectation $hostExpect).Count 0 'a missing lab.ini is reported by the shared checks'

    # ------------------------------------------------------------ world sync (docs/world-sync-protocol.md)
    Import-Module (Join-Path $projectRoot 'tools/lab/WorldSync.psm1') -Force
    Assert-True (@($ho.Keys | Where-Object { $_ -in @('world', 'world_probe') }).Count -eq 0) 'default offline lab.ini has no world keys'
    Assert-Equal (ConvertTo-LabPartnerValue -Key world -Value 2) '2' 'world 2'
    Assert-Equal (ConvertTo-LabPartnerValue -Key world -Value '0') '0' 'world 0'
    Assert-Throws { ConvertTo-LabPartnerValue -Key world -Value 4 } '0..3' 'world 4 refused'
    Assert-Throws { ConvertTo-LabPartnerValue -Key world -Value 'x' } '0..3' 'world x refused'
    Assert-Equal (ConvertTo-LabPartnerValue -Key world_probe -Value race_kill) 'race_kill' 'world_probe race_kill'
    Assert-Throws { ConvertTo-LabPartnerValue -Key world_probe -Value p2 } 'world_probe' 'unknown probe refused'
    Assert-Equal (@(Get-WorldProbeNames) -join ' ') 'p1 p3 p3b p4 p12 p17 race_kill remote_area_kill kill_near sleep_near fulton_near alert_near weather_rain clock_jump clock_fast shared_death host_continue shared_checkpoint' 'probe names and ids 1..18'
    $worldOv = ConvertFrom-LabPartnerSettingArgs -Items @('world=2', 'host:world_probe=race_kill') -Role host
    Assert-Equal "$($worldOv['world'])/$($worldOv['world_probe'])" '2/race_kill' '-PartnerSetting world=2 host:world_probe=race_kill'
    $worldHost = New-LabIniFromSettings -Settings (Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 $hostId -Offline -PartnerOverrides $worldOv)
    Assert-True ($worldHost.Contains("`nworld=2`n") -and $worldHost.Contains("`nworld_probe=race_kill`n") -and
        $worldHost.Replace("world=2`n", '').Replace("world_probe=race_kill`n", '') -ceq $hostOffIni) 'world keys are the only difference to the default offline lab.ini'
    if ($IniCheck) {
        $file = Join-Path $fixture 'world-host-lab.ini'; [IO.File]::WriteAllText($file, $worldHost)
        $n = Invoke-IniCheck lab $file
        if ($n.ContainsKey('world')) {
            Assert-Equal "$($n['world'])|$($n['world_probe'])|$($n['unknown_keys'])/$($n['invalid_values'])" '2|race_kill|0/0' 'native: lab.ini world=2 world_probe=race_kill'
            $file = Join-Path $fixture 'default-world-lab.ini'; [IO.File]::WriteAllText($file, $hostOffIni)
            $n = Invoke-IniCheck lab $file
            Assert-Equal "$($n['world'])|$($n['world_probe'])" '0|none' 'native: the default offline lab.ini is world 0'
        }
    }
    $worldCases = @(
        @('goto=30010', 'goto=30010'), @('goto_lz=1 goto=30020', 'goto=30020 goto_lz=1'), @('world=0', 'world=0'),
        @('  world_probe=race_kill   world=3 ', 'world=3 world_probe=race_kill'), @('world_probe=off', 'world_probe=off'),
        @('goto=30010 world=2 world_probe=p3b goto_lz=0', 'world=2 world_probe=p3b goto=30010 goto_lz=0'))
    foreach ($case in $worldCases) { Assert-Equal (ConvertTo-WorldTriggerText -Text $case[0]) $case[1] "world trigger '$($case[0])'" }
    foreach ($bad in @('', ' ', 'world=4', 'world=1 world=2', 'goto_lz=1', 'goto=30050', 'world_probe=none', 'world_probe=p2', 'stage=3', 'world', 'goto=30010 goto_lz=2')) {
        Assert-Throws { ConvertTo-WorldTriggerText -Text $bad } '' "world trigger refuses '$bad'"
        Assert-True (-not (Test-WorldTriggerText $bad)) "Test-WorldTriggerText is false for '$bad'"
    }
    Assert-Equal (Find-WorldTriggerResult -LogText "world=cfg; mission=30050`r`nworld_trigger=applied; text=goto=30010; t_ms=55`r`nworld_goto=armed; target=30010; lz=0; t_ms=55`r`n" -Canonical 'goto=30010').State 'applied' 'world trigger applied'
    Assert-Equal (Find-WorldTriggerResult -LogText "world_trigger=applied; text=world=0; t_ms=1`n" -Canonical 'goto=30010').State 'different' 'another world trigger applied'
    $wr = Find-WorldTriggerResult -LogText "world_trigger=applied; text=goto=30010; t_ms=1`nworld_trigger=rejected; reason=level0; t_ms=2`n" -Canonical 'goto=30010'
    Assert-Equal "$($wr.State)|$($wr.Reason)" 'rejected|level0' 'the newest world trigger line wins (rejected)'
    $wr = Find-WorldTriggerResult -LogText "world_trigger=applied; text=goto=30010; t_ms=1`r`nworld_goto=refused; reason=mission; t_ms=1`r`n" -Canonical 'goto=30010'
    Assert-Equal "$($wr.State)|$($wr.Reason)" 'rejected|mission' 'a refused goto is rejected, not applied'
    foreach ($why in 'level', 'target', 'busy', 'terminating') {
        $wr = Find-WorldTriggerResult -LogText "world_trigger=applied; text=world=3 goto=30010; t_ms=1`nworld_goto=refused; reason=$why; t_ms=1`n" -Canonical 'world=3 goto=30010'
        Assert-Equal "$($wr.State)|$($wr.Reason)" "rejected|$why" "goto refused: $why"
    }
    Assert-Equal (Find-WorldTriggerResult -LogText "world_trigger=applied; text=goto=30010; t_ms=1`n" -Canonical 'goto=30010').State 'none' 'a goto without armed/refused is not confirmed yet'
    $wr = Find-WorldTriggerResult -LogText "world_goto=refused; reason=busy; t_ms=1`nworld_trigger=applied; text=goto=30010; t_ms=2`nworld_goto=armed; target=30010; lz=0; t_ms=2`n" -Canonical 'goto=30010'
    Assert-Equal "$($wr.State)|$($wr.Line)" 'applied|world_goto=armed; target=30010; lz=0; t_ms=2' 'only goto lines after the newest trigger line count'
    Assert-Equal (Find-WorldTriggerResult -LogText "world_trigger=applied; text=world_probe=p1; t_ms=1`nworld_goto=refused; reason=busy; t_ms=1`n" -Canonical 'world_probe=p1').State 'applied' 'goto lines do not affect other triggers'
    Assert-Equal (Find-WorldTriggerResult -LogText 'partner_trigger=rejected; reason=parse' -Canonical 'world=1').State 'none' 'partner lines do not count'
    Assert-Equal (Find-WorldTriggerResult -LogText '' -Canonical 'world=1').State 'none' 'empty log'
    # Play-OfflineCoop: -World adds world=N only when > 0; -WorldProbe needs -World; [G] sends goto=30010.
    Assert-True ($playText.Contains("[ValidateRange(0, 3)][int]`$World = 3") -and $playText.Contains("[string]`$WorldProbe = 'none'")) 'Play-OfflineCoop -World 0..3 (shared world default 3), -WorldProbe (default none)'
    Assert-True ($playText.Contains("if (`$World -gt 0) { `$partnerSettings += `"world=`$World`" }") -and
        $playText.Contains("if (`$WorldProbe -ne 'none') { `$partnerSettings += `"world_probe=`$WorldProbe`" }")) 'Play-OfflineCoop adds the world keys only when set'
    Assert-True ($playText.IndexOf("if (`$WorldProbe -ne 'none' -and `$World -lt 1) { throw '-WorldProbe needs -World 1..3.' }") -ge 0 -and
        $playText.IndexOf("if (`$WorldProbe -ne 'none' -and `$World -lt 1)") -lt $playText.IndexOf("Prepare-LabInstance.ps1')")) 'Play-OfflineCoop refuses -WorldProbe without -World before preparing anything'
    Assert-True ($playText.Contains("'G' {") -and $playText.Contains("`$travelRole = if (`$World -ge 3) { 'host' } else { 'both' }") -and
        $playText.Contains("'Set-WorldTrigger.ps1') -Role `$travelRole -Text 'goto=30010' -Force")) 'Play-OfflineCoop [G]: host-only world3, both copies at lower levels'
    $playWorldErr = $null
    $playWorldAst = [System.Management.Automation.Language.Parser]::ParseFile($playPath, [ref]$null, [ref]$playWorldErr)
    $worldProbeParam = @($playWorldAst.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'WorldProbe' })[0]
    $worldProbeSet = @($worldProbeParam.Attributes | Where-Object { $_.TypeName.Name -eq 'ValidateSet' })[0]
    Assert-Equal (@($worldProbeSet.PositionalArguments | ForEach-Object { $_.Value }) -join ' ') ('none ' + (@(Get-WorldProbeNames) -join ' ')) 'Play-OfflineCoop -WorldProbe lists every probe'

    # ------------------------------------------------------------ two-PC play: lab rehearsal (-Lan, -WanSim) and shared defaults
    # docs/two-pc-play.md, spec section 8. Without the new switches the texts stay byte-identical.
    Assert-True (@($ho.Keys | Where-Object { $_ -in @('net_link', 'net_bind', 'net_peer_addr', 'partner_interp', 'net_jitter_ms', 'net_seed', 'mode') }).Count -eq 0) 'default offline lab.ini has no link v2 keys'
    Assert-True (!$hostOff.Lan -and $hostOff.WanSim -eq 'off') 'default offline settings: no -Lan, no -WanSim'
    # Get-PlayPartnerDefaults is the frozen literal of Play-OfflineCoop's dm=map keys:
    # the lab defaults + lab.json "partner" + the dm=map settings it passed before.
    $mapOverridesBefore = @('partner_stage=3', 'partner_source=net', 'partner_look=net', 'partner_apply=xform', 'partner_anim=off', 'partner_reset=n', 'partner_dm=map')
    $playDefaults = Get-PlayPartnerDefaults
    $playDefaultItems = @($playDefaults.Keys | ForEach-Object { "$_=$($playDefaults[$_])" })
    foreach ($r in 'host', 'guest') {
        $dir = if ($r -eq 'host') { $config.Paths.hostDir } else { $config.Paths.guestDir }
        $before = Get-LabInstanceSettings -Role $r -Config $config -GameDirectory $dir -HostSteamId64 $hostId -Offline -PartnerOverrides (ConvertFrom-LabPartnerSettingArgs -Items $mapOverridesBefore -Role $r)
        $now = Get-LabInstanceSettings -Role $r -Config $config -GameDirectory $dir -HostSteamId64 $hostId -Offline -PartnerOverrides (ConvertFrom-LabPartnerSettingArgs -Items $playDefaultItems -Role $r)
        Assert-Equal (New-LabIniFromSettings -Settings $now) (New-LabIniFromSettings -Settings $before) "[$r] Play-OfflineCoop dm=map lab.ini is byte-identical with Get-PlayPartnerDefaults"
        $effective = @($before.Partner.Keys | Where-Object { $_ -notin @('net', 'net_listen_port', 'net_peer_port', 'debug_soldier_warp') } | ForEach-Object { "$_=$($before.Partner[$_])" }) -join ' '
        Assert-Equal $effective ($playDefaultItems -join ' ') "[$r] Get-PlayPartnerDefaults equals Play-OfflineCoop's effective dm=map keys (order and values)"
    }
    # A tuned lab.json "partner" still reaches the default dm=map run (only the 7 keys override it).
    $tuned = Get-AltConfig { param($c) $c.partner.offset_cm = 150; $c.partner.crash_probe = 0 }
    $tunedSettings = Get-LabInstanceSettings -Role host -Config $tuned -GameDirectory $tuned.Paths.hostDir -HostSteamId64 $hostId -Offline -PartnerOverrides (ConvertFrom-LabPartnerSettingArgs -Items $mapOverridesBefore -Role host)
    Assert-Equal "$($tunedSettings.Partner['partner_offset_cm'])/$($tunedSettings.Partner['crash_probe'])/$($tunedSettings.Partner['partner_dm'])" '150/0/map' 'dm=map run keeps lab.json partner.offset_cm and crash_probe'
    Assert-Equal ([int]$labJson.Ports.play) 47830 'lab.json ports.play'
    $wanRows = @()
    foreach ($name in 'good', 'bad', 'awful') {
        $wp = Get-LabWanSimPreset -WanSim $labJson.WanSim -Name $name
        $wanRows += @("$name=" + (@($wp.Keys | ForEach-Object { $wp[$_] }) -join '/'))
    }
    Assert-Equal ($wanRows -join ' ') 'good=30/5/5/burst/1/0/0 bad=80/25/20/burst/2/5/10 awful=150/60/60/burst/3/10/30' 'lab.json wanSim presets (latency/jitter/loss/model/burst/dup/reorder)'
    Assert-Throws { Get-LabWanSimPreset -WanSim $labJson.WanSim -Name 'terrible' } 'Unknown -WanSim preset' 'unknown preset refused'
    Assert-Throws { Get-AltConfig { param($c) $c.wanSim.bad.net_jitter_ms = 1001 } } 'net_jitter_ms' 'wanSim values validated'
    Assert-Throws { Get-AltConfig { param($c) $c.wanSim.good.PSObject.Properties.Remove('net_burst') } } 'net_burst is missing' 'wanSim presets are complete'
    Assert-Throws { Get-AltConfig { param($c) $c.ports.play = 47820 } } 'ports.play' 'play port must not be a lab UDP port'
    $lanIp = '192.168.1.20'
    $lanHost = Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 $hostId -Offline -Lan -LanAddress $lanIp
    $lanGuest = Get-LabInstanceSettings -Role guest -Config $config -GameDirectory $config.Paths.guestDir -HostSteamId64 $hostId -Offline -Lan -LanAddress $lanIp
    $lanHostIni = New-LabIniFromSettings -Settings $lanHost
    $lanGuestIni = New-LabIniFromSettings -Settings $lanGuest
    Assert-True ($lanHostIni.Contains("`nnet=1`nnet_bind=any`nnet_listen_port=47820`nnet_peer_port=47821`npartner=1`n")) '-Lan host lab.ini: net_bind=any, 47820/47821'
    Assert-True ($lanGuestIni.Contains("`nnet=1`nnet_bind=any`nnet_listen_port=47821`nnet_peer_addr=$lanIp`nnet_peer_port=47820`npartner=1`n")) '-Lan guest lab.ini: net_bind=any, 47821, LAN peer address, 47820'
    $lg = ConvertFrom-IniText $lanGuestIni
    Assert-Equal "$($lg['loopback'])/$($lg['self_steamid'])/$($lg['mutex_suffix'])" '1/76561202255233008/-guest' '-Lan stays lab mode: fake guest id and mutex suffix kept'
    Assert-True (!$lanHostIni.Contains('net_peer_addr') -and !$lanHostIni.Contains('mode=')) '-Lan host: no peer address, no mode key'
    Assert-Throws { Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 $hostId -Lan -LanAddress $lanIp } 'only to the offline' '-Lan needs -Offline'
    Assert-Throws { Get-LabInstanceSettings -Role guest -Config $config -GameDirectory $config.Paths.guestDir -HostSteamId64 $hostId -Offline -Lan -LanAddress '127.0.0.1' } 'LAN IPv4' '-Lan refuses a loopback address'
    Assert-Throws { Get-LabInstanceSettings -Role guest -Config $config -GameDirectory $config.Paths.guestDir -HostSteamId64 $hostId -Offline -Lan -LanAddress 'my-pc' } 'LAN IPv4' '-Lan refuses a host name'
    $wanHost = Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 $hostId -Offline -WanSim bad
    $wanGuest = Get-LabInstanceSettings -Role guest -Config $config -GameDirectory $config.Paths.guestDir -HostSteamId64 $hostId -Offline -WanSim bad
    $wh = ConvertFrom-IniText (New-LabIniFromSettings -Settings $wanHost)
    $wg = ConvertFrom-IniText (New-LabIniFromSettings -Settings $wanGuest)
    Assert-Equal "$($wh['net_latency_ms'])/$($wh['net_jitter_ms'])/$($wh['net_loss_permille'])/$($wh['net_loss_model'])/$($wh['net_burst'])/$($wh['net_dup_permille'])/$($wh['net_reorder_permille'])/$($wh['net_seed'])/$($wh['partner_interp'])" '80/25/20/burst/2/5/10/1/auto' '-WanSim bad host: the preset, net_seed 1, partner_interp=auto'
    Assert-Equal "$($wg['net_seed'])/$($wg['partner_interp'])/$($wg['net_latency_ms'])" '2/auto/80' '-WanSim bad guest: net_seed 2'
    Assert-True (!$wh.Contains('net_bind')) '-WanSim alone keeps the loopback bind'
    $both = Get-LabInstanceSettings -Role guest -Config $config -GameDirectory $config.Paths.guestDir -HostSteamId64 $hostId -Offline -Lan -LanAddress $lanIp -WanSim good
    $bg = ConvertFrom-IniText (New-LabIniFromSettings -Settings $both)
    Assert-Equal "$($bg['net_bind'])/$($bg['net_peer_addr'])/$($bg['net_latency_ms'])/$($bg['net_seed'])" "any/$lanIp/30/2" '-Lan -WanSim together'
    Assert-Throws { Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 $hostId -Offline -WanSim nope } 'Unknown -WanSim preset' 'unknown -WanSim refused'
    # Start-Lab -Offline [-Lan] preflight on the -Lan files.
    $lanHostExpect = Get-LabOfflineExpectation -Role host -Config $config -Lan
    $lanGuestExpect = Get-LabOfflineExpectation -Role guest -Config $config -Lan
    Assert-Equal @(Get-LabOfflineIniProblems -Role host -Lab (ConvertFrom-IniText $lanHostIni) -Expectation $lanHostExpect).Count 0 '-Lan preflight accepts the -Lan host lab.ini'
    Assert-Equal @(Get-LabOfflineIniProblems -Role guest -Lab $lg -Expectation $lanGuestExpect).Count 0 '-Lan preflight accepts the -Lan guest lab.ini'
    Assert-True ((@(Get-LabOfflineIniProblems -Role guest -Lab $lg -Expectation (Get-LabOfflineExpectation -Role guest -Config $config)) -join ' ').Contains('prepared with -Lan')) 'a -Lan lab.ini needs Start-Lab -Lan'
    Assert-True ((@(Get-LabOfflineIniProblems -Role guest -Lab $go -Expectation $lanGuestExpect) -join ' ').Contains('net_bind is not any')) 'Start-Lab -Lan refuses a loopback lab.ini'
    $noPeer = [ordered]@{}; foreach ($k in $lg.Keys) { if ($k -ne 'net_peer_addr') { $noPeer[$k] = $lg[$k] } }
    Assert-True ((@(Get-LabOfflineIniProblems -Role guest -Lab $noPeer -Expectation $lanGuestExpect) -join ' ').Contains('no net_peer_addr')) 'Start-Lab -Lan: the guest needs net_peer_addr'
    # The link v2 keys: validators (spec 1.1) and -PartnerSetting rules.
    Assert-Equal "$(ConvertTo-LabPartnerValue -Key net_link -Value legacy)/$(ConvertTo-LabPartnerValue -Key net_bind -Value any)/$(ConvertTo-LabPartnerValue -Key net_bind -Value 10.0.0.5)" 'legacy/any/10.0.0.5' 'net_link and net_bind values'
    foreach ($bad in 'Loopback', 'localhost', '10.0.0.05', '1.2.3') { Assert-Throws { ConvertTo-LabPartnerValue -Key net_bind -Value $bad } 'loopback|any' "net_bind '$bad' refused" }
    Assert-Equal "$(ConvertTo-LabPartnerValue -Key net_peer_addr -Value 127.0.0.1)/$(ConvertTo-LabPartnerValue -Key net_peer_addr -Value 203.0.113.7)" '127.0.0.1/203.0.113.7' 'net_peer_addr: dotted IPv4 (127/8 allowed in the lab)'
    foreach ($bad in '0.0.0.0', '255.255.255.255', '224.0.0.1', '239.1.2.3', '203.000.113.7', 'my-pc', '[::1]', '::1', '1.2.3.4:5') {
        Assert-Throws { ConvertTo-LabPartnerValue -Key net_peer_addr -Value $bad } 'dotted IPv4' "net_peer_addr '$bad' refused"
    }
    Assert-Equal "$(ConvertTo-LabPartnerValue -Key net_seed -Value 4294967295)/$(ConvertTo-LabPartnerValue -Key net_burst -Value 16)/$(ConvertTo-LabPartnerValue -Key net_reorder_ms -Value 500)" '4294967295/16/500' 'net_seed (u32), net_burst, net_reorder_ms upper bounds'
    Assert-Throws { ConvertTo-LabPartnerValue -Key net_seed -Value 0 } '1..4294967295' 'net_seed 0 refused'
    Assert-Throws { ConvertTo-LabPartnerValue -Key net_seed -Value 4294967296 } '1..4294967295' 'net_seed above u32 refused'
    Assert-Throws { ConvertTo-LabPartnerValue -Key net_burst -Value 17 } '1..16' 'net_burst 17 refused'
    Assert-Throws { ConvertTo-LabPartnerValue -Key net_loss_model -Value gilbert } 'even|burst' 'net_loss_model values'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_interp_min_ms -Value 19 } '20..500' 'partner_interp_min_ms range'
    Assert-Throws { ConvertTo-LabPartnerValue -Key partner_lost -Value drop } 'freeze|hide' 'partner_lost values'
    Assert-Throws { ConvertTo-LabPartnerIni -Values @{ partner_interp_min_ms = 200; partner_interp_max_ms = 100 } } 'at least partner_interp_min_ms' 'interp max below min refused'
    Assert-Throws { ConvertTo-LabPartnerIni -Values @{ partner_interp_max_ms = 50 } } 'at least partner_interp_min_ms (60)' 'interp max below the default min refused'
    $ovLegacy = ConvertFrom-LabPartnerSettingArgs -Items @('net_link=legacy', 'partner_interp=auto', 'guest:net_jitter_ms=20') -Role guest
    Assert-Equal (@($ovLegacy.Keys | ForEach-Object { "$_=$($ovLegacy[$_])" }) -join ' ') 'net_link=legacy partner_interp=auto net_jitter_ms=20' '-PartnerSetting net_link=legacy (the bisect path) and the new keys'
    Assert-Throws { ConvertFrom-LabPartnerSettingArgs -Items 'mode=play' -Role host } 'allowed: lab' '-PartnerSetting mode=play refused (play is the two-PC launcher''s)'
    $modePlay = [ordered]@{}; foreach ($k in $ho.Keys) { $modePlay[$k] = $ho[$k] }; $modePlay['mode'] = 'play'
    Assert-True ((@(Get-LabOfflineIniProblems -Role host -Lab $modePlay -Expectation $hostExpect) -join ' ').Contains('Invalid lab.ini mode=play')) 'Start-Lab -Offline refuses a play-mode lab.ini'
    Assert-Throws { ConvertFrom-LabPartnerSettingArgs -Items 'net_bind=any' -Role host } 'use Prepare-LabInstance -Lan' '-PartnerSetting cannot change net_bind'
    Assert-Throws { ConvertFrom-LabPartnerSettingArgs -Items 'net_peer_addr=192.168.1.2' -Role guest } 'use Prepare-LabInstance -Lan' '-PartnerSetting cannot change net_peer_addr'
    Assert-Throws { ConvertFrom-LabPartnerJson -Partner ([pscustomobject]@{ net_bind = 'any' }) } 'Prepare-LabInstance -Lan' 'lab.json cannot set net_bind'
    $legacyIni = ConvertFrom-IniText (New-LabIniFromSettings -Settings (Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 $hostId -Offline -PartnerOverrides $ovLegacy))
    Assert-Equal @(Get-LabOfflineIniProblems -Role host -Lab $legacyIni -Expectation $hostExpect).Count 0 'Start-Lab -Offline accepts net_link=legacy and the new keys'
    $badNew = [ordered]@{}; foreach ($k in $ho.Keys) { $badNew[$k] = $ho[$k] }; $badNew['net_link'] = 'v3'
    Assert-True ((@(Get-LabOfflineIniProblems -Role host -Lab $badNew -Expectation $hostExpect) -join ' ').Contains('Invalid lab.ini net_link=v3')) 'Start-Lab -Offline refuses an invalid new key value'
    # LAN address detection (pure part) and the 0.0.0.0 port check of Start-Lab -Lan.
    $configs = @(
        [pscustomobject]@{ Status = 'Disconnected'; HasGateway = $true; Addresses = @('192.168.9.9') },
        [pscustomobject]@{ Status = 'Up'; HasGateway = $false; Addresses = @('10.1.1.1') },
        [pscustomobject]@{ Status = 'Up'; HasGateway = $true; Addresses = @('169.254.3.4', '127.0.0.1', '192.168.1.20', '192.168.1.21') })
    Assert-Equal (Select-CoopLanAddress -Configurations $configs) '192.168.1.20' 'LAN address: first IPv4 of an Up adapter with a gateway, no link-local/loopback'
    Assert-True ($null -eq (Select-CoopLanAddress -Configurations @($configs[0], $configs[1]))) 'no LAN address: $null (Prepare then asks for -LanAddress)'
    $prepText = [IO.File]::ReadAllText((Join-Path $projectRoot 'tools/lab/Prepare-LabInstance.ps1'))
    Assert-True ($prepText.Contains("'No LAN IPv4 address found; pass -LanAddress.'")) 'Prepare -Lan: the documented error without a LAN address'
    $udpProbe = $null
    for ($i = 0; $i -lt 20 -and !$udpProbe; $i++) {
        $candidate = Get-Random -Minimum 41000 -Maximum 49000
        try { $udpProbe = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Any, $candidate)) } catch { $udpProbe = $null }
    }
    try {
        $probePort = ([Net.IPEndPoint]$udpProbe.Client.LocalEndPoint).Port
        Assert-True (-not (Test-LocalPortFree -Port $probePort -Protocol Udp -AnyAddress)) 'Test-LocalPortFree -AnyAddress sees a 0.0.0.0 UDP listener'
    } finally { $udpProbe.Dispose() }
    $udpFree = 0
    for ($i = 0; $i -lt 20 -and !$udpFree; $i++) { $candidate = Get-Random -Minimum 41000 -Maximum 49000; if (Test-LocalPortFree -Port $candidate -Protocol Udp -AnyAddress) { $udpFree = $candidate } }
    Assert-True ($udpFree -ne 0) 'Test-LocalPortFree -AnyAddress finds a free UDP port'
    # Play-OfflineCoop: the rehearsal switches reach Prepare and Start-Lab; defaults unchanged.
    Assert-True ($playText.Contains("[ValidateSet('off', 'good', 'bad', 'awful')][string]`$WanSim = 'off'") -and $playText.Contains('[switch]$Lan') -and $playText.Contains('[string]$LanAddress')) 'Play-OfflineCoop: -Lan, -LanAddress, -WanSim (default off)'
    Assert-True ($playText.Contains("-Offline -PartnerSetting `$partnerSettings @rehearsal") -and $playText.Contains("'Start-Lab.ps1') -Offline -Lan:`$Lan")) 'Play-OfflineCoop passes -Lan/-WanSim to Prepare-LabInstance and -Lan to Start-Lab'
    Assert-True ($playText.Contains("if (`$Lan) { `$rehearsal['Lan'] = `$true") -and $playText.Contains("if (`$WanSim -ne 'off') { `$rehearsal['WanSim'] = `$WanSim")) 'Play-OfflineCoop: no rehearsal arguments without the switches'
    $playOk = $null; $playErr = $null
    $null = [System.Management.Automation.Language.Parser]::ParseFile($playPath, [ref]$playOk, [ref]$playErr)
    Assert-Equal @($playErr).Count 0 'Play-OfflineCoop still parses'
    # Native cross-checks of the rehearsal lab.ini files (link v2 parser, docs/two-pc-play.md).
    $linkKeys = $false
    if ($IniCheck) {
        $file = Join-Path $fixture 'link-probe-lab.ini'; [IO.File]::WriteAllText($file, $hostOffIni)
        $linkKeys = (Invoke-IniCheck lab $file)['link_keys'] -eq '1'
    }
    if ($linkKeys) {
        $wanBoth = New-LabIniFromSettings -Settings (Get-LabInstanceSettings -Role host -Config $config -GameDirectory $config.Paths.hostDir -HostSteamId64 $hostId -Offline -Lan -LanAddress $lanIp -WanSim awful)
        foreach ($case in @(@('lan-host', $lanHostIni, 'host'), @('lan-guest', $lanGuestIni, 'guest'), @('wan-host', $wanBoth, 'host'), @('offline-host', $hostOffIni, 'host'))) {
            $file = Join-Path $fixture "$($case[0])-lab.ini"; [IO.File]::WriteAllText($file, $case[1])
            $n = Invoke-IniCheck lab $file
            Assert-Equal "$($n['present'])|$($n['mode'])|$($n['role'])|$($n['play_refused'])|$($n['unknown_keys'])/$($n['invalid_values'])" "1|lab|$($case[2])|0x0|0/0" "native: $($case[0]) lab.ini clean"
        }
        $n = Invoke-IniCheck lab (Join-Path $fixture 'lan-guest-lab.ini')
        Assert-Equal "$($n['net'])|$($n['net_bind'])|$($n['net_listen_port'])|$($n['net_peer_addr'])|$($n['net_peer_port'])|$($n['net_link'])" "1|any|47821|$lanIp|47820|v2" 'native: -Lan guest net keys'
        $n = Invoke-IniCheck lab (Join-Path $fixture 'wan-host-lab.ini')
        Assert-Equal "$($n['net_latency_ms'])|$($n['net_jitter_ms'])|$($n['net_loss_permille'])|$($n['net_loss_model'])|$($n['net_burst'])|$($n['net_dup_permille'])|$($n['net_reorder_permille'])|$($n['net_seed'])|$($n['partner_interp'])" '150|60|60|burst|3|10|30|1|auto' 'native: -WanSim awful keys'
        $n = Invoke-IniCheck lab (Join-Path $fixture 'offline-host-lab.ini')
        Assert-Equal "$($n['net_bind'])|$($n['net_link'])|$($n['partner_interp'])|$($n['net_peer_addr'])" 'loopback|v2|fixed|127.0.0.1' 'native: the default offline lab.ini keeps the loopback defaults'
    } else { Write-Output 'link_keys missing: native link cross-checks skipped' }

    Test-LabInputSpecs

    if ($IniCheck) {
        $file = Join-Path $fixture 'host-offline-lab.ini'; [IO.File]::WriteAllText($file, $hostOffIni)
        $n = Invoke-IniCheck lab $file
        Assert-Equal "$($n['present'])/$($n['role'])/$($n['loopback'])/$($n['listen_port'])/$($n['peer_port'])/$($n['xinput'])" '1/host/1/47810/47811/-1' 'native: offline host lab.ini keeps the base settings'
        Assert-Equal $n['save_dir'] $hostOff.SaveDir 'native: offline save_dir'
        Assert-Equal $n['invalid_values'] '0' 'native: no invalid values in the offline lab.ini'
        Assert-Equal $n['unknown_keys'] '0' "native: every partner key (including partner_anim/aset/reset/look) is known to the parser"
        if ($n.ContainsKey('partner_stage')) {
            Assert-Equal "$($n['net'])/$($n['net_listen_port'])/$($n['net_peer_port'])/$($n['partner'])/$($n['partner_stage'])/$($n['partner_offset_cm'])/$($n['crash_probe'])" '1/47820/47821/1/0/300/1' 'native: partner keys parsed'
        }
        $file = Join-Path $fixture 'offline-coop.ini'; [IO.File]::WriteAllText($file, $coopOff)
        $n = Invoke-IniCheck coop $file
        Assert-Equal "$($n['master_url'])|$($n['unknown_keys'])|$($n['invalid_values'])" "$deadUrl|0|0" 'native: offline coop.ini'
        Assert-Equal (Invoke-IniCheck url $deadUrl)['url_status'] 'ok' 'native: the closed-port URL passes the redirect validator'
    }

    # ------------------------------------------------------------ TPP_GRAPHICS_CONFIG tag
    # A real (graphics settings only) config written by mgsvtpp 1.0.15.4 on this PC.
    $realConfig = @'
{
   "as" : "UzF+nvqVP+4=",
   "graphics" : {
      "quality_setting" : {
         "depth_of_field" : "Enable",
         "effect" : "ExtraHigh",
         "lighting" : "ExtraHigh",
         "model_detail" : "ExtraHigh",
         "motion_blur_amount" : "Large",
         "postprocess" : "ExtraHigh",
         "shadow" : "ExtraHigh",
         "ssao" : "ExtraHigh",
         "texture" : "ExtraHigh",
         "texture_filtering" : "ExtraHigh",
         "volumetric_clouds" : "On"
      },
      "version" : 1,
      "videoout_setting" : {
         "display_index" : 0,
         "framerate_control" : "Auto",
         "height" : 1440,
         "vsync" : "Enable",
         "width" : 2560,
         "window_mode" : "FullScreenWindowed"
      }
   },
   "project" : "tpp"
}
'@.Replace("`r`n", "`n")
    $info = Get-GraphicsConfigInfo -Text $realConfig
    Assert-True $info.Valid 'real config tag verifies (FastWriter + "-%x" length salt + MD5[0..8])'
    Assert-Equal $info.ComputedTag 'UzF+nvqVP+4=' 'computed tag'
    Assert-Equal "$($info.WindowMode) $($info.Width)x$($info.Height)" 'FullScreenWindowed 2560x1440' 'parsed video settings'
    $windowed = Set-GraphicsConfigWindowedText -Text $realConfig -Width 1280 -Height 720
    $after = Get-GraphicsConfigInfo -Text $windowed
    Assert-True $after.Valid 'edited config verifies'
    Assert-Equal $after.StoredTag 'a8ID1auvq+8=' 'edited tag (independently computed with Python hashlib)'
    Assert-Equal "$($after.WindowMode) $($after.Width)x$($after.Height)" 'Windowed 1280x720' 'edited video settings'
    Assert-Equal (($windowed -split "`n").Count) (($realConfig -split "`n").Count) 'formatting preserved'
    $tampered = $realConfig.Replace('"width" : 2560', '"width" : 2561')
    Assert-True (-not (Get-GraphicsConfigInfo -Text $tampered).Valid) 'tampered config does not verify'
    Assert-Throws { Set-GraphicsConfigWindowedText -Text $tampered } 'does not verify' 'refuse to edit an unverifiable config'
    Assert-Throws { Set-GraphicsConfigWindowedText -Text '{"as":"x"}' } 'does not verify' 'refuse unexpected layout'
    $synthetic = '{"b":1,"a":{"z":"q\"\\\n","y":-5},"c":true}' | ConvertFrom-Json
    Assert-Equal (ConvertTo-JsonCppFastText -Value $synthetic -Document) ("{`"a`":{`"y`":-5,`"z`":`"q\`"\\\n`"},`"b`":1,`"c`":true}" + "`n") 'FastWriter: sorted keys, escapes, ints, bools'
    Assert-Equal (Get-GraphicsConfigTag -Object $synthetic) 'vrSYXqLC+9k=' 'synthetic tag (independently computed with Python)'
    Assert-Throws { ConvertTo-JsonCppFastText -Value 1.5 } 'Floating-point' 'doubles refused'

    # ------------------------------------------------------------ log matching and verdicts
    $hostReady = "mode=host_v3; peer_acceptance=after_mission_start; max_players=2`r`n" +
        "install_result=0 installed=1 pid=4 thread=5 tick_ms=6 target_rva=0x1a178e0 lifetime=process`r`n" +
        "dynamite_host=returned; result=1 thread=83844 tick_ms=1 peer_acceptance_requested=0`r`n" +
        "host_admission=authorized; owned_host=1 same_thread=1 lua_interface=SessionEnableAccept`r`n" +
        "dynamite_session=observed; owner=0000000000000001 session=0000000000000002 manager=0000000000000003 job=0000000000000004 session_read=1 state=3 accept=1 manager_heap=1 job_heap=1 consistent=1 calls=0 writes=0 atomic=0`r`n" +
        "session_status=host_ready`r`n"
    $probeOk = "MGS Co-op loader probe 0.11.1`r`narchitecture=x64`r`ntime_utc=2026-10-03T10:00:00Z`r`nDirectInput8Create_hresult=0x00000000`r`n"
    $guestArmed = "mode=client_v1; configured_host=1; join=ctrl_f8_after_mission_start; max_players=2; connected=unverified`r`nclient_join=armed; source=trigger_file`r`n"
    $guestStarted = $guestArmed + "client_join=input_received; key=trigger_file`r`ndynamite_client=returned; request_started=1`r`nsession_status=client_requested`r`n"
    $labId = '76561202255233008'
    $fuseUrl = 'http://127.0.0.1:6667/tppstm/gate'
    $hostSave = 'D:\Lab\MGS_TPP\mgs-coop\lab\saves'
    $guestSave = 'D:\Lab\MGS_TPP_guest\mgs-coop\lab\saves'
    $hostEarly = "early=start; version=0.11.1; lab=1; role=host`r`nmutex=unchanged`r`nidentity=real`r`nsaves=redirected; dir=$hostSave`r`nxinput=none`r`n"
    $guestEarly = "early=start; version=0.11.1; lab=1; role=guest`r`nmutex=renamed; suffix=-guest`r`nidentity=fake; steamid=$labId`r`nsaves=redirected; dir=$guestSave`r`nxinput=pad 0`r`n"
    $steamOk = "`r`nMGS Co-op original Steam import observer 0.4.0; fingerprint=accepted`r`nmaster_server=redirected; method=in_place; url=$fuseUrl`r`n"
    function Get-HostSafetyFor([string]$Early = $hostEarly, [string]$Steam = $steamOk) {
        Get-LabSafetyVerdict -Role host -EarlyLog $Early -SteamLog $Steam -ExpectedSaveDir $hostSave -ExpectedUrl $fuseUrl
    }
    function Get-GuestSafetyFor([string]$Early = $guestEarly, [string]$Steam = $steamOk) {
        Get-LabSafetyVerdict -Role guest -EarlyLog $Early -SteamLog $Steam -ExpectedSaveDir $guestSave -ExpectedUrl $fuseUrl -ExpectedSteamId $labId -ExpectedMutexSuffix '-guest'
    }
    function Get-SafetyText($Verdict) { "$($Verdict.State)/$($Verdict.Reason)/$(@($Verdict.Missing) -join ',')" }

    # Safety evidence (docs/lab-protocol.md safety rules).
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor)) 'ok//' 'host safety ok'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor)) 'ok//' 'guest safety ok'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early '' -Steam '')) 'pending//saves,master_server' 'host: nothing written yet'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early '' -Steam '')) 'pending//saves,mutex,identity,master_server' 'guest: nothing written yet'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam '')) 'pending//master_server' 'redirect line not written yet'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace("saves=redirected; dir=$hostSave", 'saves=unchanged'))) 'unsafe/saves_unchanged/' 'saves=unchanged is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace($hostSave, 'C:\Program Files (x86)\Steam\userdata\7'))) 'unsafe/saves_wrong_dir/' 'redirect to another folder is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace("dir=$hostSave", 'dir='))) 'unsafe/saves_wrong_dir/' 'empty save dir is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace("; dir=$hostSave", ''))) 'unsafe/saves_wrong_dir/' 'redirect without a dir is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace($hostSave, 'd:/lab/mgs_tpp/MGS-COOP/lab/saves/'))) 'ok//' 'save dir compare ignores case, slash style and a trailing separator'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace($hostSave, '\\?\' + $hostSave))) 'ok//' 'save dir compare ignores a \\?\ prefix'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early ($hostEarly + "saves=redirected; dir=D:\elsewhere`r`n"))) 'unsafe/saves_wrong_dir/' 'any wrong redirect line is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace('lab=1', 'lab=0'))) 'unsafe/lab_disabled/' 'lab=0 is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace('role=host', 'role=guest'))) 'unsafe/role_mismatch/' 'role mismatch is unsafe'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace('mutex=renamed; suffix=-guest', 'mutex=unchanged'))) 'unsafe/mutex_unchanged/' 'guest mutex unchanged'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace('suffix=-guest', 'suffix=-other'))) 'unsafe/mutex_wrong_suffix/' 'guest mutex suffix differs'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace('mutex=renamed; suffix=-guest', 'mutex=renamed'))) 'unsafe/mutex_wrong_suffix/' 'guest mutex without suffix'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace("identity=fake; steamid=$labId", 'identity=real'))) 'unsafe/identity_real/' 'guest real identity is unsafe'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace($labId, '76561197960265729'))) 'unsafe/identity_wrong_id/' 'guest with another id is unsafe'
    Assert-Equal (Get-SafetyText (Get-GuestSafetyFor -Early $guestEarly.Replace("; steamid=$labId", ''))) 'unsafe/identity_wrong_id/' 'guest fake identity without an id'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Early $hostEarly.Replace('identity=real', "identity=fake; steamid=$labId"))) 'ok//' 'host identity line is evidence only'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam "master_server=refused; reason=instruction_bytes`r`n")) 'unsafe/master_server_refused/' 'redirect refused is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam "master_server=failed; reason=verify; method=in_place`r`n")) 'unsafe/master_server_failed/' 'redirect failure is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam "master_server=refused; reason=invalid_url; detail=bad_suffix`r`n")) 'unsafe/master_server_refused/' 'invalid url refusal is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam ($steamOk + "master_server=refused; reason=already_redirected`r`n"))) 'ok//' 'a later refusal of another URL keeps the verified redirect'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam $steamOk.Replace('6667', '6668'))) 'unsafe/master_server_wrong_url/' 'redirect to another URL is unsafe'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam "master_server=redirected; method=in_place`r`n")) 'unsafe/master_server_wrong_url/' 'redirect line without url'
    Assert-Equal (Get-SafetyText (Get-HostSafetyFor -Steam "xmaster_server=refused; reason=x`r`n")) 'pending//master_server' 'prefix anchored at line start'
    Assert-Throws { Get-LabSafetyVerdict -Role guest -ExpectedSaveDir $guestSave -ExpectedUrl $fuseUrl } 'ExpectedSteamId' 'guest checks need the expected id'
    Assert-Equal (Get-InstanceSafety -Role guest -Expectation ([pscustomobject]@{ SaveDir = $guestSave; Url = $fuseUrl; SteamId = ''; MutexSuffix = '-guest' }) -EarlyLog $guestEarly -SteamLog $steamOk).Reason 'no_expectation' 'missing expectation is unsafe'
    $hostSafety = Get-HostSafetyFor
    $guestSafety = Get-GuestSafetyFor

    Assert-True (Test-LogLine $hostReady 'dynamite_host=returned; result=1') 'prefix followed by space'
    Assert-True (-not (Test-LogLine "dynamite_host=returned; result=10`r`n" 'dynamite_host=returned; result=1')) 'result=10 is not result=1'
    Assert-True (-not (Test-LogLine "xsession_status=host_ready`n" 'session_status=host_ready')) 'prefix anchored at line start'
    Assert-True (-not (Test-LogLine "session_status=host_ready_extra`n" 'session_status=host_ready')) 'value must end'
    Assert-True (Test-LogLine 'session_status=host_ready' 'session_status=host_ready') 'end of text counts'
    Assert-Equal (Get-LastSessionStatus "session_status=host_ready`nsession_status=host_peer_present`n") 'host_peer_present' 'last session status'
    Assert-Equal (Get-FirstLogLine $hostReady 'host_admission=authorized') 'host_admission=authorized; owned_host=1 same_thread=1 lua_interface=SessionEnableAccept' 'first line evidence'

    $fuseBoth = @{ CMD_GET_URLLIST = 2; CMD_AUTH_STEAMTICKET = 2 }
    $fuseOne = @{ CMD_GET_URLLIST = 1; CMD_AUTH_STEAMTICKET = 1 }
    function Get-V([hashtable]$Override = @{}) {
        $arguments = @{ HostLog = $hostReady; GuestLog = ''; HostProbeLog = $probeOk; HostSafety = $hostSafety; GuestSafety = $guestSafety; FuseCounts = $fuseBoth }
        foreach ($key in $Override.Keys) { $arguments[$key] = $Override[$key] }
        Get-LabSmokeVerdict @arguments
    }
    function Get-VText($Verdict) { "$($Verdict.Verdict)/$($Verdict.Phase)/$($Verdict.Reason)" }

    $v = Get-LabSmokeVerdict
    Assert-Equal "$($v.Verdict)/$($v.Phase)" 'pending/waiting_safety_evidence' 'nothing yet: no safety evidence'
    Assert-Equal (Get-VText (Get-V @{ GuestSafety = $null })) 'pending/waiting_safety_evidence/' 'guest safety unknown blocks readiness'
    $v = Get-V @{ HostSafety = (Get-HostSafetyFor -Steam ''); GuestSafety = (Get-GuestSafetyFor -Early '') }
    Assert-Equal (@($v.Missing) -join ',') 'host:master_server,guest:saves,guest:mutex,guest:identity' 'missing safety evidence listed'
    $v = Get-V @{ HostSafety = (Get-HostSafetyFor -Early ''); ReadyElapsedSec = 900 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/timeout_waiting_safety_evidence' 'missing save evidence times out'
    $v = Get-V @{ HostSafety = (Get-HostSafetyFor -Steam ''); GuestSafety = (Get-GuestSafetyFor -Steam ''); ReadyElapsedSec = 900 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/not_under_fuse' 'missing master_server redirect at the timeout = not_under_fuse'
    $v = Get-V @{ HostLog = '' }
    Assert-Equal "$($v.Phase)/$($v.HostReady)" 'waiting_host_ready/False' 'host prerequisites needed'
    foreach ($line in @('mode=host_v3', 'install_result=0 installed=1', 'dynamite_host=returned; result=1', 'host_admission=authorized', 'session_status=host_ready')) {
        $without = (($hostReady -split "`r`n") | Where-Object { !$_.StartsWith($line) }) -join "`r`n"
        $v = Get-V @{ HostLog = $without }
        Assert-True ($v.Phase -eq 'waiting_host_ready' -and (@($v.Missing) -contains "host:$line")) "host prerequisite required: $line"
    }
    $v = Get-V @{ HostLog = $hostReady.Replace('state=3 accept=1', 'state=2 accept=1') }
    Assert-True (@($v.Missing) -contains 'host:dynamite_session=observed; state=3 accept=1 consistent=1') 'dynamite_session state=3 accept=1 required'
    $v = Get-V @{ HostLog = $hostReady.Replace('consistent=1', 'consistent=0') }
    Assert-True (@($v.Missing) -contains 'host:dynamite_session=observed; state=3 accept=1 consistent=1') 'dynamite_session consistent=1 required'
    $v = Get-V @{ HostProbeLog = $probeOk.Replace('0x00000000', '0x80004005') }
    Assert-True (@($v.Missing) -contains 'host:DirectInput8Create_hresult=0x00000000') 'DirectInput8Create success required'
    Assert-True (-not (Get-V @{ HostLog = $hostReady.Replace('host_admission=authorized', 'host_admission=refused') }).HostReady) 'admission required'
    $v = Get-V @{ FuseCounts = @{} }
    Assert-Equal "$($v.Phase)/$($v.HostReady)" 'waiting_host_fuse/False' 'host readiness needs fuse traffic'
    Assert-Equal (Get-V @{ FuseCounts = @{}; ReadyElapsedSec = 900 }).Reason 'not_under_fuse' 'no fuse traffic at the timeout = not_under_fuse'
    Assert-Equal (Get-V @{ FuseCounts = @{ CMD_GET_URLLIST = 1 } }).Phase 'waiting_host_fuse' 'auth through fuse required'
    $v = Get-V
    Assert-Equal "$($v.Verdict)/$($v.Phase)/$($v.HostReady)" 'pending/waiting_guest_armed/True' 'host ready'
    $v = Get-V @{ GuestLog = $guestArmed.Replace('mode=client_v1; configured_host=1', 'mode=client_v1; refused=x') }
    Assert-True ($v.Phase -eq 'waiting_guest_armed' -and (@($v.Missing) -contains 'guest:mode=client_v1; configured_host=1')) 'guest configured_host required'
    $v = Get-V @{ GuestLog = $guestArmed; FuseCounts = $fuseOne }
    Assert-Equal $v.Phase 'waiting_guest_fuse' 'full run needs fuse traffic from both instances'
    Assert-Equal (Get-V @{ GuestLog = $guestArmed; FuseCounts = $fuseOne; ReadyElapsedSec = 900 }).Reason 'not_under_fuse' 'guest never reached fuse'
    $v = Get-V @{ GuestLog = $guestArmed }
    Assert-Equal "$($v.Phase)/$($v.ShouldTrigger)" 'ready_to_trigger/True' 'trigger when both ready'
    $v = Get-V @{ GuestLog = $guestArmed; TriggerCreated = $true }
    Assert-Equal "$($v.Phase)/$($v.ShouldTrigger)" 'waiting_request_started/False' 'no second trigger'
    $v = Get-V @{ GuestLog = $guestStarted; TriggerCreated = $true }
    Assert-Equal "$($v.Verdict)/$($v.Phase)" 'pending/waiting_established' 'request started'
    $hostPeer = $hostReady + "session_status=host_peer_present`r`n"
    $guestEstablished = $guestStarted + "session_status=client_established`r`n"
    $v = Get-V @{ HostLog = $hostPeer; GuestLog = $guestEstablished; TriggerCreated = $true }
    Assert-Equal "$($v.Verdict)/$($v.PassPolls)" 'pending/1' 'first established poll'
    $v = Get-V @{ HostLog = $hostPeer; GuestLog = $guestEstablished; TriggerCreated = $true; PreviousPassPolls = 1 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'pass/established_stable' 'two consecutive polls pass'
    $v = Get-V @{ HostLog = $hostReady; GuestLog = $guestEstablished; TriggerCreated = $true; PreviousPassPolls = 1 }
    Assert-Equal "$($v.Verdict)/$($v.PassPolls)" 'pending/0' 'host peer missing resets the count'
    $guestStatus = ConvertFrom-LabStatusText -Text '{"version":1,"pid":77,"role":"guest","session_status":"client_established","join":"established"}' -ExpectedPid 77
    $hostStatus = ConvertFrom-LabStatusText -Text '{"version":1,"pid":55,"role":"host","session_status":"host_peer_present","join":"idle"}' -ExpectedPid 55
    $v = Get-V @{ GuestLog = $guestStarted; HostStatus = $hostStatus; GuestStatus = $guestStatus; TriggerCreated = $true; PreviousPassPolls = 1 }
    Assert-Equal $v.Verdict 'pass' 'lab-status.json drives the session state'
    Assert-True ($null -eq (ConvertFrom-LabStatusText -Text '{"pid":1,"session_status":"none"}' -ExpectedPid 2)) 'status from another process ignored'
    Assert-True ($null -eq (ConvertFrom-LabStatusText -Text '{"pid":1,' -ExpectedPid 1)) 'partial json ignored'
    Assert-True ($null -eq (ConvertFrom-LabStatusText -Text '{"pid":1}' -ExpectedPid 1)) 'json without session_status ignored'
    $failures = @(
        @{ Guest = $guestArmed + "dynamite_client=refused; reason=x`r`n"; Reason = 'dynamite_client_refused' },
        @{ Guest = $guestArmed + "dynamite_client=returned; request_started=0`r`n"; Reason = 'request_started_0' },
        @{ Guest = $guestArmed + "client_join=refused; reason=placeholder_host_id`r`n"; Reason = 'client_join_refused' },
        @{ Guest = "mode=client_v1; refused=missing_or_invalid_client_host_txt; hooks=0 game_calls=0`r`n"; Reason = 'missing_or_invalid_client_host_txt' },
        @{ Guest = $guestStarted + "session_status=session_error`r`n"; Reason = 'guest_session_error' }
    )
    foreach ($case in $failures) {
        $v = Get-V @{ GuestLog = $case.Guest; TriggerCreated = $true }
        Assert-Equal "$($v.Verdict)/$($v.Reason)" "fail/$($case.Reason)" "failure rule $($case.Reason)"
    }
    Assert-Equal (Get-V @{ HostLog = ($hostReady + "session_status=session_error`n") }).Reason 'host_session_error' 'host session error'
    Assert-Equal (Get-V @{ HostLog = "dynamite_host=returned; result=0 thread=1`n" }).Reason 'host_creation_failed' 'host creation failed'
    Assert-Equal (Get-V @{ HostAlive = $false }).Reason 'process_exit_host' 'host exit'
    Assert-Equal (Get-V @{ GuestAlive = $false }).Reason 'process_exit_guest' 'guest exit'
    Assert-Equal (Get-V @{ HostSafety = (Get-HostSafetyFor -Early $hostEarly.Replace("saves=redirected; dir=$hostSave", 'saves=unchanged')) }).Reason 'unsafe_host_saves_unchanged' 'unsafe host fails at once'
    Assert-Equal (Get-V @{ GuestSafety = (Get-GuestSafetyFor -Early $guestEarly.Replace("identity=fake; steamid=$labId", 'identity=real')) }).Reason 'unsafe_guest_identity_real' 'unsafe guest fails at once'
    Assert-Equal (Get-V @{ HostSafety = (Get-HostSafetyFor -Steam "master_server=failed; reason=verify; method=in_place`r`n"); HostOnly = $true }).Reason 'unsafe_host_master_server_failed' 'unsafe host fails host-only runs'
    Assert-Equal (Get-V @{ HostIdCheck = 'mismatch' }).Reason 'host_id_mismatch' 'host id mismatch fails at once'
    Assert-Equal (Get-V @{ HostIdCheck = 'ok' }).Verdict 'pending' 'matching host id'
    Assert-Equal (Get-V @{ GuestAlive = $false; GuestSafety = $null; HostIdCheck = 'mismatch'; HostOnly = $true; FuseCounts = $fuseOne }).Verdict 'pending' 'guest ignored in host-only runs'
    $v = Get-V @{ ReadyElapsedSec = 900; ReadyTimeoutSec = 900 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/timeout_waiting_guest_armed' 'readiness timeout'
    $v = Get-V @{ GuestLog = $guestStarted; TriggerCreated = $true; ReadyElapsedSec = 5000; TriggerElapsedSec = 179; TimeoutSec = 180 }
    Assert-Equal $v.Verdict 'pending' 'readiness timeout does not apply after the trigger'
    $v = Get-V @{ GuestLog = $guestStarted; TriggerCreated = $true; TriggerElapsedSec = 180; TimeoutSec = 180 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/timeout_waiting_established' 'session timeout after the trigger'
    # Host-only (M0): passes only with the host prerequisites AND evidence of fuse.
    $v = Get-V @{ HostOnly = $true; GuestSafety = $null; FuseCounts = $fuseOne }
    Assert-Equal "$($v.Verdict)/$($v.PassPolls)" 'pending/1' 'host-only first poll'
    $v = Get-V @{ HostOnly = $true; GuestSafety = $null; FuseCounts = $fuseOne; PreviousPassPolls = 1 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'pass/host_ready_stable' 'host-only pass (M0) under fuse'
    $v = Get-V @{ HostOnly = $true; GuestSafety = $null; FuseCounts = @{}; PreviousPassPolls = 1 }
    Assert-Equal "$($v.Verdict)/$($v.Phase)/$($v.PassPolls)" 'pending/waiting_host_fuse/0' 'host-only without fuse traffic never passes'
    $v = Get-V @{ HostOnly = $true; GuestSafety = $null; FuseCounts = @{}; ReadyElapsedSec = 900 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/not_under_fuse' 'host-only on Konami (no fuse traffic) reports not_under_fuse'
    $v = Get-V @{ HostOnly = $true; GuestSafety = $null; HostSafety = (Get-HostSafetyFor -Steam ''); ReadyElapsedSec = 900 }
    Assert-Equal "$($v.Verdict)/$($v.Reason)" 'fail/not_under_fuse' 'host-only without the master_server redirect reports not_under_fuse'

    # Host identity cross-check (host-steamid.txt vs client-host.txt / peer_steamid).
    $realHost = '76561197960265729'
    Assert-Equal (Get-HostIdCheck -HostIdText "$realHost`n" -ClientHostText "$realHost`n" -PeerSteamId $realHost) 'ok' 'host id ok'
    Assert-Equal (Get-HostIdCheck -HostIdText "$realHost`n" -ClientHostText "$realHost`n" -PeerSteamId 'learn') 'ok' 'peer learn ignored'
    Assert-Equal (Get-HostIdCheck -HostIdText '' -ClientHostText "$realHost`n" -PeerSteamId $realHost) 'not_written' 'host id not written yet'
    Assert-Equal (Get-HostIdCheck -HostIdText '76561197960265730' -ClientHostText $realHost -PeerSteamId $realHost) 'mismatch' 'client-host differs'
    Assert-Equal (Get-HostIdCheck -HostIdText $realHost -ClientHostText $realHost -PeerSteamId '76561197960265730') 'mismatch' 'peer_steamid differs'
    Assert-Equal (Get-HostIdCheck -HostIdText 'garbage' -ClientHostText $realHost -PeerSteamId $realHost) 'mismatch' 'invalid host id'
    $freshFile = Join-Path $fixture 'host-steamid.txt'
    [IO.File]::WriteAllText($freshFile, "$realHost`n")
    Assert-Equal (Read-FreshText -Path $freshFile -NotBeforeUtc ([DateTime]::UtcNow.AddMinutes(-5))) "$realHost`n" 'fresh host-steamid.txt read'
    Assert-Equal (Read-FreshText -Path $freshFile -NotBeforeUtc ([DateTime]::UtcNow.AddMinutes(5).ToString('o'))) '' 'file older than the host process ignored'
    Assert-Equal (Read-FreshText -Path (Join-Path $fixture 'absent.txt') -NotBeforeUtc $null) '' 'absent file'

    # Redaction: run folders are shareable.
    $sample = "time=1 msg=CMD_AUTH_STEAMTICKET steamID=$realHost`nloopback=listening; port=47810; peer_port=47811; self=$realHost`n" +
        "identity=fake; steamid=$labId`nsource_root=C:\Steam\userdata\97930372\287700 and C:/Steam/userdata/97930372`n" +
        "msg=CMD_CREATE_PLAYER player_name=`"Big Boss`"`nmsg=x player_name=Solo`nother=12345678901234567 clan=103582791429521408`n"
    $redacted = Hide-SteamIds -Text $sample -Keep @($labId)
    Assert-True (!$redacted.Contains($realHost) -and !$redacted.Contains('97930372') -and !$redacted.Contains('Big Boss') -and !$redacted.Contains('Solo')) 'real ids, account folders and player names removed'
    Assert-True ($redacted.Contains("steamid=$labId") -and $redacted.Contains('steamID=<steamid>') -and $redacted.Contains('userdata\<account>') -and $redacted.Contains('userdata/<account>')) 'lab id kept, placeholders written'
    Assert-True ($redacted.Contains('12345678901234567') -and $redacted.Contains('103582791429521408')) 'numbers that are not individual SteamIDs kept'
    Assert-True ((Hide-SteamIds -Text $sample).Contains('steamid=<steamid>')) 'without Keep the lab id is redacted too'

    # fuse console output and counts.
    $console = "time=1 level=INFO msg=`"client request handled`" type=CMD_GET_URLLIST ip=127.0.0.1:5000`n" +
        "time=2 level=INFO msg=CMD_AUTH_STEAMTICKET steamID=$realHost`n" +
        "time=3 level=INFO msg=`"client request handled`" type=CMD_AUTH_STEAMTICKET ip=127.0.0.1:5000`n" +
        "time=4 level=INFO msg=CMD_AUTH_STEAMTICKET steamID=$labId`n" +
        "time=5 level=INFO msg=`"client request handled`" type=CMD_AUTH_STEAMTICKET ip=127.0.0.1:5001`n" +
        "time=6 level=INFO msg=CMD_AUTH_STEAMTICKET steamID=$realHost`n"
    $fs = Get-FuseConsoleSummary -Text $console -LabSteamId $labId
    Assert-Equal "$($fs.Counts.CMD_GET_URLLIST)/$($fs.Counts.CMD_AUTH_STEAMTICKET)/$($fs.DistinctAuthIds)/$($fs.LabIdSeen)/$($fs.OtherAuthIds)" '1/2/2/True/1' 'fuse console summary'
    Assert-True (-not (Get-FuseConsoleSummary -Text $console.Replace($labId, $realHost) -LabSteamId $labId).LabIdSeen) 'lab id not seen when the guest presented the real id'
    $merged = Merge-FuseCounts @{ CMD_GET_URLLIST = 2; CMD_X = 1 } $fs.Counts
    Assert-Equal "$($merged.CMD_GET_URLLIST)/$($merged.CMD_AUTH_STEAMTICKET)/$($merged.CMD_X)" '2/2/1' 'fuse counts merged by maximum'
    Assert-Equal (Get-CountValue $null 'CMD_GET_URLLIST') 0 'count of nothing'

    # ------------------------------------------------------------ offset log reader
    $log = Join-Path $fixture 'observer.log'
    [IO.File]::WriteAllText($log, "a`nb`npartial")
    $r = Read-LogFromOffset -Path $log -Offset 0
    Assert-Equal "$($r.Text)|$($r.NewOffset)" "a`nb`n|4" 'complete lines only'
    $writer = [IO.FileStream]::new($log, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
    try {
        $bytes = [Text.Encoding]::ASCII.GetBytes("X`n")
        $writer.Write($bytes, 0, $bytes.Length); $writer.Flush()
        $r = Read-LogFromOffset -Path $log -Offset 4  # while the "game" still holds the file open for writing
        Assert-Equal "$($r.Text)|$($r.NewOffset)" "partialX`n|13" 'reads appended data while the writer is open'
    } finally { $writer.Dispose() }
    [IO.File]::WriteAllText($log, "new`n")
    $r = Read-LogFromOffset -Path $log -Offset 13
    Assert-True ($r.Truncated -and $r.Text -eq "new`n") 'replaced file restarts at 0'
    Assert-True (-not (Read-LogFromOffset -Path (Join-Path $fixture 'missing.log')).Exists) 'missing log'
    Assert-Equal (Get-FileLengthOrZero (Join-Path $fixture 'missing.log')) ([Int64]0) 'missing length'

    # ------------------------------------------------------------ offline partner spike: trigger grammar, log parsing, verdicts
    Import-Module (Join-Path $projectRoot 'tools/lab/PartnerSpike.psm1') -Force
    foreach ($case in @(
            @('stage=3 source=orbit apply=warp', 'stage=3 source=orbit apply=warp'), @(' apply=slot   stage=1 ', 'stage=1 apply=slot'),
            @('stop', 'stop'), @("  stop`t", 'stop'), @('source=net', 'source=net'), @("apply=both`tsource=fixed", 'source=fixed apply=both'), @('stage=0', 'stage=0'),
            @('parts=explicit', 'parts=explicit'), @('mirror=on', 'mirror=on'), @('mirror=on stage=3 parts=explicit', 'stage=3 parts=explicit mirror=on'),
            @('mirror=on apply=both parts=explicit source=orbit stage=3', 'stage=3 source=orbit apply=both parts=explicit mirror=on'),
            @('move=hybrid', 'apply=hybrid'), @('look=quiet census=1 anim=mirror stage=3', 'stage=3 anim=mirror look=quiet census=1'),
            @('probe=ps reset=ac aset=r apply=xform', 'apply=xform aset=r reset=ac probe=ps'), @('look=net', 'look=net'), @('probe=off', 'probe=off'),
            @('stage=3 source=net move=hybrid anim=net aset=qs look=net', 'stage=3 source=net apply=hybrid anim=net aset=qs look=net'),
            @('apply=input input=net stick=180:100 cam=sandbox hold=none yaw=rot words=rp0 bits=0x6 correct=1500 isend=1 dm=probe lookgate=1 lookrecover=1',
              'apply=input input=net stick=180:100 cam=sandbox hold=none yaw=rot words=rp0 bits=0x6 correct=1500 isend=1 dm=probe lookgate=1 lookrecover=1'),
            @('dmplay=3 dm=on tap=0x2', 'dm=on tap=0x2 dmplay=3'), @('press=0x2:1200', 'press=0x2:1200'),
            @('p218=1 padprobe=1 holdlog=0 dmstop=1', 'p218=1 padprobe=1 holdlog=0 dmstop=1'),
            @('dm=map', 'dm=map'), @('dmplay=12', 'dmplay=12'), @('dmplay=25', 'dmplay=25'), @('dmplay=26', 'dmplay=26'), @('dmplay=27', 'dmplay=27'), @('dmplay=28', 'dmplay=28'), @('dmplay=29', 'dmplay=29'), @('dmplay=30', 'dmplay=30'), @('dmmap=25,24,23,22,21,20,19,18,17,16,15,14', 'dmmap=25,24,23,22,21,20,19,18,17,16,15,14'), @('dmspeed=300:1500:5600 dm=map lookgate=1 dmmap=0,12,11,5,1,3,4,4,2,6,6,6', 'dm=map dmmap=0,12,11,5,1,3,4,4,2,6,6,6 dmspeed=300:1500:5600 lookgate=1'))) {
        Assert-Equal (ConvertTo-PartnerTriggerText -Text $case[0]) $case[1] "trigger grammar accepts '$($case[0])'"
    }
    Assert-True ((ConvertTo-PartnerTriggerText -Text 'stage=3 source=orbit apply=hybrid anim=mirror aset=qs reset=ac look=quiet probe=off census=1 parts=explicit mirror=on').Length + 1 -le 255) 'the longest classic canonical trigger fits the native 255 bytes'
    Assert-True ((ConvertTo-PartnerTriggerText -Text 'stage=3 source=net apply=input anim=off aset=qs reset=ac look=net input=net stick=359:100 cam=sandbox hold=none yaw=rotneg words=rhn bits=0xffffffff correct=10000 isend=1 padprobe=1 dm=probe lookgate=1 lookrecover=1').Length + 1 -le 255) 'a long round 3 trigger fits 255 bytes'
    Assert-True ((ConvertTo-PartnerTriggerText -Text 'stage=3 source=net apply=xform reset=n anim=off input=off isend=0 look=net dm=map dmmap=12,12,12,12,12,12,12,12,12,12,12,12 dmspeed=2000:10000:20000').Length + 1 -le 255) 'a long dm=map trigger fits 255 bytes'
    Assert-Equal (Get-PartnerTriggerExpectation -Canonical 'dm=map dmmap=0,10,11,5,1,3,4,4,7,6,6,6 dmspeed=300:2600:5600')['dm'] 'map' 'dm=map in the expected partner_stage line (dmmap/dmspeed are not part of it)'
    foreach ($bad in @('', '   ', 'STOP', 'Stage=1', 'stop stage=1', 'stage=4', 'stage=-1', 'stage=01', 'stage=1 stage=2', 'source=walk', 'apply=Warp',
            'speed=3', 'stage=1;source=net', "stage=1`nstop", 'stage=', 'stage=3 source=orbit apply=warp extra=1', "stage=1`u{e9}",
            'parts=implicit', 'parts=', 'mirror=off', 'Mirror=on', 'parts=explicit parts=explicit', 'mirror=on mirror=on', 'stop parts=explicit', 'mirror=on stop', 'stop stop',
            'move=warp apply=slot', 'apply=teleport', 'anim=on', 'aset=x', 'reset=b', 'look=ocelot', 'census=2', 'census', 'probe=on', 'stop census=1', 'look=net look=copy',
            'input=walk', 'cam=on', 'hold=mine', 'yaw=left', 'recon=on', 'align=151', 'align=-0', 'align=016', 'slot=both',
            'runaway=warp', 'wrap=on', 'place=0', 'place=1 place=1', 'words=xyz', 'words=rp', 'bits=0xZ', 'bits=0x123456789', 'correct=10001',
            'stick=360:0', 'stick=180:101', 'stick=180', 'tap=2', 'press=0x2:49', 'press=0x2:5001', 'press=0x2:0100', 'press=0x2:01200', 'dmplay=31', 'dmstop=0', 'isend=2',
            'dm=maybe', 'dmmap=0,10,11,5,1,3,4,4,7,6,6', 'dmmap=0,10,11,5,1,3,4,4,7,6,6,31', 'dmmap=0,10,11,5,1,3,4,4,7,6,6,06',
            'dmspeed=300:400:5600', 'dmspeed=0300:2600:5600', 'dmspeed=300:2600', 'dm=map dm=on', 'input=net input=off', 'tap=0x1 tap=0x2', 'stop input=net')) {
        $shown = $bad -replace "`n", '\n'
        Assert-Throws { ConvertTo-PartnerTriggerText -Text $bad } '' "trigger grammar refuses '$shown'"
        Assert-True (-not (Test-PartnerTriggerText $bad)) "Test-PartnerTriggerText is false for '$shown'"
    }
    # The native result of a consumed trigger (Set-PartnerTrigger -WaitSec).
    $changedLine = 'partner_stage=changed; stage=3; source=orbit; apply=warp; via=trigger'
    Assert-Equal (Find-PartnerTriggerResult -LogText "partner_probe=state; t_ms=1`r`n$changedLine`r`n" -Canonical 'stage=3 source=orbit').State 'applied' 'trigger result: applied'
    Assert-Equal (Find-PartnerTriggerResult -LogText "$changedLine`n" -Canonical 'apply=warp').State 'applied' 'trigger result: keys not named keep their values'
    Assert-Equal (Find-PartnerTriggerResult -LogText "$changedLine`n" -Canonical 'stage=3 parts=explicit').State 'applied' 'trigger result: one-shots are not part of the line'
    Assert-Equal (Find-PartnerTriggerResult -LogText "$changedLine`n" -Canonical 'stage=2').State 'different' 'trigger result: another command was applied'
    Assert-Equal (Find-PartnerTriggerResult -LogText "partner_stage=changed; stage=0; source=fixed; apply=warp; via=trigger`npartner_bits=cleared; reason=trigger`n" -Canonical 'stop').State 'applied' 'trigger result: stop means stage 0'
    $rejected = Find-PartnerTriggerResult -LogText "$changedLine`npartner_trigger=rejected; reason=parse`n" -Canonical 'stage=3'
    Assert-Equal "$($rejected.State)|$($rejected.Line)" 'rejected|partner_trigger=rejected; reason=parse' 'trigger result: the newest result line wins (rejected)'
    Assert-Equal (Find-PartnerTriggerResult -LogText "partner=configured; mode=offline_v1; stage=3; source=fixed; apply=warp`n" -Canonical 'stage=3').State 'none' 'trigger result: only via=trigger lines count'
    Assert-Equal (Find-PartnerTriggerResult -LogText '' -Canonical 'stage=3').State 'none' 'trigger result: empty log'
    $newLine = 'partner_stage=changed; stage=3; source=net; apply=hybrid; anim=net; aset=qs; reset=n; look=net; probe=off; via=trigger'
    Assert-Equal (Find-PartnerTriggerResult -LogText "$newLine`n" -Canonical 'stage=3 source=net apply=hybrid anim=net aset=qs look=net').State 'applied' 'trigger result: the new fields'
    Assert-Equal (Find-PartnerTriggerResult -LogText "$newLine`n" -Canonical 'look=quiet').State 'different' 'trigger result: another look was applied'
    Assert-Equal (Find-PartnerTriggerResult -LogText "$newLine`n" -Canonical 'census=1').State 'applied' 'trigger result: census is a one-shot (not in the line)'

    $line = ConvertFrom-PartnerLogLine 'partner_probe=state; t_ms=12; ids=0000,ffff; pool=00,ff; pos1_mm=-1,2,-3; s0=0x03; parts=3,x'
    Assert-Equal "$($line.Kind)|$($line.Prefix)|$($line.Value)|$($line.Fields.Count)" 'partner_probe=state|partner_probe|state|6' 'probe line: kind and fields'
    $probe = ConvertTo-PartnerProbe -Entry $line
    Assert-Equal "$($probe.TMs)/$($probe.Ids -join ',')/$($probe.Pool -join ',')/$($probe.Pos1 -join ',')/$($probe.S0)" '12/0,65535/0,255/-1,2,-3/3' 'typed probe fields (hex ids/pool, signed vectors, 0x values)'
    Assert-True ($null -eq $probe.Parts -and $null -eq $probe.Pos0 -and $null -eq $probe.Q194) 'malformed or missing probe fields are null'
    Assert-True (-not (Test-PartnerProbeRealized $probe)) 'ids[1]=ffff: not realized'
    Assert-True (Test-PartnerProbeRealized (ConvertTo-PartnerProbe (ConvertFrom-PartnerLogLine 'partner_probe=state; ids=0000,0001; pool=00,01'))) 'ids[1]!=ffff and pool[1]!=ff: realized'
    $warpLine = ConvertFrom-PartnerLogLine 'partner_warp_check; R=1; apply=warp; cmd_mm=1,2,3; slot_mm=1,2,4; err_mm=1'
    Assert-Equal "$($warpLine.Kind)|$($warpLine.Value)|$($warpLine.Fields['err_mm'])|$($warpLine.Fields['slot_mm'])" 'partner_warp_check||1|1,2,4' 'head without a value'
    $luaLine = ConvertFrom-PartnerLogLine 'partner_lua=event=refused;reason=bad_index;R=nil'
    Assert-Equal "$($luaLine.Kind)|$($luaLine.Fields['reason'])|$($luaLine.Fields['R'])" 'partner_lua=event=refused|bad_index|nil' 'Lua payload keeps its event; fields separated by ; alone'
    $loose = ConvertFrom-PartnerLogLine "partner_net=stats; tx=1 rx=2;status=live`r"
    Assert-Equal "$($loose.Fields['tx'])/$($loose.Fields['rx'])/$($loose.Fields['status'])" '1/2/live' 'blank or ; separated tokens, CR stripped'
    Assert-Equal (ConvertFrom-PartnerNumber '0x1f') ([Int64]31) 'hex number'
    Assert-Equal (ConvertFrom-PartnerNumber '-45') ([Int64]-45) 'signed number'
    Assert-True ($null -eq (ConvertFrom-PartnerNumber 'ffff') -and (ConvertFrom-PartnerNumber 'ffff' -Hex) -eq 65535) 'bare hex only with -Hex'
    Assert-True ($null -eq (ConvertFrom-PartnerNumber '1.5') -and $null -eq (ConvertFrom-PartnerNumber '') -and $null -eq (ConvertFrom-PartnerNumber '0x')) 'non-numbers are null'
    Assert-True ($null -eq (ConvertFrom-PartnerList '1,2' -Count 3) -and $null -eq (ConvertFrom-PartnerList '1,,3')) 'lists need every element'
    $sentinel = ConvertFrom-PartnerList '-2147483648,-2147483648,-2147483648' -Count 3
    Assert-True ((Test-PartnerVectorAvailable (ConvertFrom-PartnerList '-11000,500,2000' -Count 3)) -and !(Test-PartnerVectorAvailable $sentinel) -and
        !(Test-PartnerVectorAvailable (ConvertFrom-PartnerList '1,-2147483647,3' -Count 3)) -and !(Test-PartnerVectorAvailable $null)) 'INT32_MIN (and +-INT32_MAX) components mark a vector unavailable'
    Assert-True ($null -eq (Get-PartnerDistance $sentinel (ConvertFrom-PartnerList '0,0,0' -Count 3))) 'no distance to an unavailable vector'
    Assert-Equal (Get-PartnerSpread -Vectors @((ConvertFrom-PartnerList '0,0,0' -Count 3), $sentinel, (ConvertFrom-PartnerList '300,0,400' -Count 3))) ([double]500) 'spread ignores unavailable vectors'

    # Fixture logs (tests/fixtures/partner-lab) written with the section 2.7 spellings.
    $partnerFixtures = Join-Path $PSScriptRoot 'fixtures/partner-lab'
    $fx = @{}
    foreach ($name in 'e0-host', 'e0-guest', 'e1-host', 'e3-host', 'e6-host', 'e6-guest') { $fx[$name] = [IO.File]::ReadAllText((Join-Path $partnerFixtures "$name.log")) }
    $offSteam = "master_server=redirected; method=in_place; url=$deadUrl`r`n"
    $offSafe = [pscustomobject]@{ State = 'ok'; Reason = ''; Missing = @() }
    function Get-PV([string]$Experiment, [string]$Role, [string]$Log, [hashtable]$Extra = @{}) {
        $arguments = @{ Experiment = $Experiment; Role = $Role; ObserverLog = $Log; SteamLog = $offSteam; Safety = $offSafe; ExpectedMasterUrl = $deadUrl; Alive = $true }
        foreach ($key in $Extra.Keys) { $arguments[$key] = $Extra[$key] }
        Get-PartnerSpikeVerdict @arguments
    }
    function Assert-Verdict($Verdict, [string]$Expected, [string]$Contains, [string]$Message) {
        $text = "$($Verdict.verdict): failed=$(@($Verdict.failed) -join ' | '); missing=$(@($Verdict.missing) -join ' | ')"
        Assert-Equal $Verdict.verdict $Expected "$Message ($text)"
        if ($Contains) { Assert-True ((@($Verdict.failed) + @($Verdict.missing)) -join "`n").Contains($Contains) "$Message names '$Contains' ($text)" }
    }
    function Remove-Lines([string]$Text, [string]$Prefix) { (@($Text -split "\r?\n") | Where-Object { !$_.StartsWith($Prefix) }) -join "`n" }
    function Select-Lines([string]$Text, [int]$Count) { (@($Text -split "\r?\n") | Select-Object -First $Count) -join "`n" }
    function Set-LastLine([string]$Text, [string]$Old, [string]$New) {
        $lines = @($Text -split "\r?\n")
        for ($i = $lines.Count - 1; $i -ge 0; $i--) { if ($lines[$i].Contains($Old)) { $lines[$i] = $lines[$i].Replace($Old, $New); break } }
        $lines -join "`n"
    }

    # E0: offline boot and baseline.
    $v = Get-PV E0 host $fx['e0-host']
    Assert-Verdict $v 'PASS' '' 'E0 host PASS fixture'
    Assert-True ((@($v.matched) -contains 'host_create=skipped; mode=offline_v1') -and (@($v.matched) -contains "master_server=redirected; method=in_place; url=$deadUrl")) 'E0 evidence lines listed'
    Assert-Equal "$($v.data['yaw_abs_max_cdeg'])/$($v.data['unfocused_hz_min'])/$($v.data['probe_span_s'])" '17990/24/310' 'E0 data: units, unfocused rate, probe span'
    Assert-Verdict (Get-PV E0 guest $fx['e0-guest']) 'PASS' '' 'E0 guest PASS fixture'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'].Replace('online_bit=0', 'online_bit=1')) 'FAIL' 'S_IS_ONLINE got set' 'E0 online_bit=1 fails'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'].Replace('sync=null', 'sync=set')) 'FAIL' 'synchronizer' 'E0 sync=set fails'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'].Replace('count=2', 'count=1')) 'FAIL' 'two-slot package' 'E0 count=1 fails'
    Assert-Verdict (Get-PV E0 host ($fx['e0-host'] + "crash_probe=av; rva=0x9c1f00; access=read; target=0x0; thread=7; stage=0; realized=0`r`n")) 'FAIL' 'rva=0x9c1f00' 'E0 crash_probe=av fails'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'].Replace('partner_net=listening; port=47820; peer_port=47821', 'partner_net=disabled; reason=transport_bind')) 'FAIL' 'transport_bind' 'E0 partner_net=disabled fails'
    Assert-Verdict (Get-PV E0 host ($fx['e0-host'] -replace 'partner_cfg=ready;[^\r\n]*', 'partner_cfg=refused; reason=code_gate')) 'FAIL' 'reason=code_gate' 'E0 partner_cfg=refused fails'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'].Replace('hz=24; focused=0', 'hz=6; focused=0')) 'FAIL' 'below 10 Hz' 'E0 slow unfocused instance fails'
    $firstProbe = [regex]::new([regex]::Escape('ids=0000,ffff; pool=00,ff'))
    Assert-Verdict (Get-PV E0 host $firstProbe.Replace($fx['e0-host'], 'ids=0000,0001; pool=00,01', 1)) 'FAIL' 'first probe' 'E0 first probe must show ids=xxxx,ffff pool=xx,ff'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'].Replace('yaw_abs_max_cdeg=17990', 'yaw_abs_max_cdeg=314')) 'FAIL' 'radians' 'E0 rotY in radians fails after walking'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'].Replace('mem_pos_mm=-9490,500,3900', 'mem_pos_mm=-9300,500,3900')) 'FAIL' 'not fresh' 'E0 lua_pos vs mem_pos over 50 mm fails'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'] @{ Safety = [pscustomobject]@{ State = 'unsafe'; Reason = 'master_server_wrong_url'; Missing = @() } }) 'FAIL' 'unsafe_master_server_wrong_url' 'E0 unsafe instance fails'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'] @{ OfflineRun = $false }) 'FAIL' 'run_not_offline' 'E0 needs an offline run'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'] @{ ExpectedMasterUrl = 'https://mgstpp-game.konamionline.com/tppstm/gate' }) 'FAIL' 'closed local port' 'E0 refuses a non-local master URL'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'] @{ Alive = $false }) 'FAIL' 'process_exited' 'E0 process exit fails'
    Assert-Verdict (Get-PV E0 host (Remove-Lines $fx['e0-host'] 'partner_net=first_rx')) 'INCOMPLETE' 'partner_net=first_rx' 'E0 without first_rx is incomplete'
    Assert-Verdict (Get-PV E0 host (Select-Lines $fx['e0-host'] 20)) 'INCOMPLETE' '5 minutes' 'E0 under 5 minutes is incomplete'
    Assert-Verdict (Get-PV E0 host '') 'INCOMPLETE' 'mode=offline_v1' 'E0 without lines is incomplete'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'] @{ Safety = [pscustomobject]@{ State = 'pending'; Reason = ''; Missing = @('master_server') } }) 'INCOMPLETE' 'safety:master_server' 'E0 waits for the redirect line'
    $noRx = Remove-Lines (Remove-Lines $fx['e0-host'] 'partner_net=first_rx') 'partner_net=stats'
    Assert-Verdict (Get-PV E0 host $noRx @{ HostOnly = $true }) 'INCOMPLETE' 'the guest instance' 'E0 host-only run: never PASS (E0 needs both instances)'
    Assert-Verdict (Get-PV E0 host $fx['e0-host'] @{ HostOnly = $true }) 'INCOMPLETE' 'E0 PASS needs both instances' 'E0 host-only run with receive lines: still INCOMPLETE'
    Assert-Verdict (Get-PV E0 host $noRx) 'INCOMPLETE' 'tx>0 and rx>0' 'E0 with two instances: receive evidence required'
    $noTurn = $fx['e0-host'] -replace 'yaw_abs_max_cdeg=17990', 'yaw_abs_max_cdeg=0' -replace 'lua_pos_mm=-9500,500,3900; mem_pos_mm=-9490,500,3900', 'lua_pos_mm=-11000,500,2000; mem_pos_mm=-11000,500,2000' -replace 'lua_pos_mm=-9500,500,3900; mem_pos_mm=-9500,500,3900', 'lua_pos_mm=-11000,500,2000; mem_pos_mm=-11000,500,2000'
    Assert-Verdict (Get-PV E0 host $noTurn) 'INCOMPLETE' 'walk a circle' 'E0 units need a walk before deciding degrees or radians'
    # The guest must walk too (Invoke-PartnerExperiment E0 walks both); the hint names its role.
    $guestStill = $fx['e0-guest'] -replace 'lua_pos_mm=[-0-9,]+; mem_pos_mm=[-0-9,]+', 'lua_pos_mm=-9000,500,2500; mem_pos_mm=-9000,500,2500' -replace 'yaw_abs_max_cdeg=17990', 'yaw_abs_max_cdeg=0'
    Assert-Verdict (Get-PV E0 guest $guestStill) 'INCOMPLETE' 'Send-LabInput.ps1 -Role guest -Keys w+a:6000' 'E0 guest without a walk is INCOMPLETE and names the guest walk'
    # mem_pos_mm=INT32_MIN: the vars block was unreadable (not a units or freshness failure).
    $memGone = $fx['e0-host'] -replace 'mem_pos_mm=[-0-9,]+', 'mem_pos_mm=-2147483648,-2147483648,-2147483648'
    $v = Get-PV E0 host $memGone
    Assert-Verdict $v 'FAIL' 'mem_pos_mm unreadable' 'E0 mem_pos unreadable in every units line fails with its own reason'
    Assert-True (!(@($v.failed) -join ' ').Contains('not fresh')) 'the INT32_MIN sentinel is not reported as stale vars.playerPos*'
    $v = Get-PV E0 host $fx['e0-host'].Replace('mem_pos_mm=-9500,500,3900', 'mem_pos_mm=-2147483648,-2147483648,-2147483648')
    Assert-Verdict $v 'PASS' '' 'E0 one unreadable units line among readable (moving) ones: PASS'
    Assert-True ((@($v.notes) -join ' ').Contains('mem_pos_mm unavailable') -and $v.data['units_mem_unreadable'] -eq 1) 'the unreadable units line is noted'
    $oneUnits = Remove-Lines (Remove-Lines $memGone 'partner_units; lua_pos_mm=-9500') 'partner_units; lua_pos_mm=-9000'
    Assert-Verdict (Get-PV E0 host $oneUnits) 'INCOMPLETE' 'readable mem_pos_mm' 'E0 a single unreadable units line is INCOMPLETE'
    # partner_cfg=refused: transient per-tick refusals recover; mission-start refusals and online_bit/sync_present fail.
    $readyLine = 'partner_cfg=ready; flags=0x00000001; count=2; realized_count=2; local=0; R=1; online_bit=0; sync=null; code_gate=ok'
    $v = Get-PV E0 host ($fx['e0-host'] + "partner_cfg=refused; reason=player_absent`r`n$readyLine`r`n")
    Assert-Verdict $v 'PASS' '' 'E0 transient tick refusal followed by partner_cfg=ready: PASS'
    Assert-True ((@($v.notes) -join ' ').Contains('transient partner_cfg=refused; reason=player_absent')) 'the transient refusal is noted'
    Assert-Verdict (Get-PV E0 host ($fx['e0-host'] + "partner_cfg=refused; reason=s_null`r`n")) 'INCOMPLETE' 'partner_cfg=ready after partner_cfg=refused; reason=s_null' 'E0 unresolved tick refusal is INCOMPLETE'
    Assert-Verdict (Get-PV E0 host ($fx['e0-host'] + "partner_cfg=refused; reason=sync_present`r`n$readyLine`r`n")) 'FAIL' 'reason=sync_present' 'E0 sync_present refusal fails even when it recovers'
    Assert-Verdict (Get-PV E0 host ($fx['e0-host'] + "partner_cfg=refused; reason=online_bit`r`n")) 'FAIL' 'S_IS_ONLINE' 'E0 online_bit refusal fails'
    $cfgLater = $fx['e0-host'] + "partner_stop; cleared=0; teardown=clear`r`nmission_boundary=runtime_enter; accepted=1 thread=10 mission=30050 phase=1`r`npartner_cfg=refused; reason=player_absent`r`n"
    Assert-Verdict (Get-PV E0 host $cfgLater) 'FAIL' 'at the mission start (CFG receipt)' 'E0 mission-start refusal without recovery fails'
    $v = Get-PV E0 host ($cfgLater + "mission_boundary=runtime_enter; accepted=1 thread=10 mission=30050 phase=1`r`n$readyLine`r`n")
    Assert-Verdict $v 'PASS' '' 'E0 mission-start refusal followed by a later ready mission: PASS with a note'
    Assert-True ((@($v.notes) -join ' ').Contains('at a mission start (CFG)')) 'the recovered mission-start refusal is noted'

    # E1: Warp reaches the unrealized instance 1.
    $v = Get-PV E1 host $fx['e1-host']
    Assert-Verdict $v 'PASS' '' 'E1 PASS fixture'
    Assert-Equal "$($v.data['warp_checks'])/$($v.data['err_mm_max'])" '3/2' 'E1 data'
    Assert-Verdict (Get-PV E1 host ($fx['e1-host'] -replace 'err_mm=[0-9]+', 'err_mm=640')) 'FAIL' 'err_mm>5' 'E1 Warp not reaching the slot fails'
    Assert-Verdict (Get-PV E1 host ($fx['e1-host'] + "partner_lua=event=error;msg=bad_argument`r`n")) 'FAIL' 'partner_lua=event=error' 'E1 Lua error fails'
    $stage1 = [regex]::Replace($fx['e1-host'], '(?m)^(partner_probe=state; t_ms=[0-9]+; stage=1; )ids=0000,ffff; pool=00,ff', '${1}ids=0000,0001; pool=00,01')
    Assert-Verdict (Get-PV E1 host $stage1) 'FAIL' 'stay unrealized' 'E1 realization at stage 1 fails'
    Assert-Verdict (Get-PV E1 host (Remove-Lines $fx['e1-host'] 'partner_stage=changed')) 'INCOMPLETE' 'stage=1 apply=warp' 'E1 needs the stage=1 trigger'
    Assert-Verdict (Get-PV E1 host $fx['e1-host'].Replace('apply=warp; via=trigger', 'apply=slot; via=trigger')) 'INCOMPLETE' 'stage=1 apply=warp' 'E1 is the Lua Warp: apply=slot does not count'
    $noWarp = Remove-Lines $fx['e1-host'] 'partner_warp_check'
    Assert-Verdict (Get-PV E1 host $noWarp) 'FAIL' 'no partner_warp_check' 'E1 without warp checks for 10 s fails'
    Assert-Verdict (Get-PV E1 host (Remove-Lines $noWarp 'partner_probe=state; t_ms=2010450')) 'INCOMPLETE' 'partner_warp_check' 'E1 waits for the first warp check'
    $v = Get-PV E1 host ($fx['e1-host'] + "partner_stage=changed; stage=3; source=fixed; apply=warp; via=trigger`r`npartner_warp_check; R=1; apply=warp; cmd_mm=1,1,1; slot_mm=900,900,900; err_mm=1500`r`n")
    Assert-Verdict $v 'PASS' '' 'E1 is judged on its own segment (a later stage change closes it)'
    Assert-True ($null -ne $v.data['segment_end_line']) 'E1 segment end recorded'

    # E3: full realization.
    $v = Get-PV E3 host $fx['e3-host']
    Assert-Verdict $v 'PASS' '' 'E3 PASS fixture'
    Assert-Equal "$($v.data['ms_after_bits'])/$($v.data['parts_ms_after_realize'])/$($v.data['stable_s'])" '180/4200/130' 'E3 data'
    Assert-Verdict (Get-PV E3 host ($fx['e3-host'] + "partner_realized=0; R=1; ms_after_realize=60000`r`n")) 'FAIL' 'thrash' 'E3 unrealize fails'
    Assert-Verdict (Get-PV E3 host ($fx['e3-host'] + "crash_probe=av; rva=0x9c2000; access=write; target=0x0; thread=7; stage=3; realized=1`r`n")) 'FAIL' 'crash_probe=av' 'E3 access violation fails'
    Assert-Verdict (Get-PV E3 host $fx['e3-host'].Replace('ms_after_bits=180', 'ms_after_bits=1800')) 'FAIL' '1800 ms after the bits' 'E3 realization later than 1 s fails'
    Assert-Verdict (Get-PV E3 host $fx['e3-host'].Replace('partner_parts=loaded; R=1; ms_after_realize=4200', 'partner_parts=timeout; R=1; state=1; pb_state=0')) 'FAIL' 'F3b' 'E3 parts timeout fails'
    Assert-Verdict (Get-PV E3 host $fx['e3-host'].Replace('ms_after_realize=4200', 'ms_after_realize=16000')) 'FAIL' '16000 ms' 'E3 parts later than 15 s fails'
    $f3a = (Remove-Lines (Remove-Lines $fx['e3-host'] 'partner_realized=1') 'partner_parts=') -replace 'ids=0000,0001; pool=00,01', 'ids=0000,ffff; pool=00,ff'
    Assert-Verdict (Get-PV E3 host $f3a) 'FAIL' 'F3a' 'E3 without realization 10 s after the bits fails (F3a)'
    Assert-Verdict (Get-PV E3 host (Select-Lines $fx['e3-host'] 22)) 'INCOMPLETE' '120 s realized' 'E3 waits for 120 s of stability'
    Assert-Verdict (Get-PV E3 host (Remove-Lines $fx['e3-host'] 'partner_bits=set')) 'FAIL' 'no partner_bits=set' 'E3 without bits after 5 s fails'
    Assert-Verdict (Get-PV E3 host (Select-Lines $fx['e3-host'] 13)) 'INCOMPLETE' 'partner_bits=set' 'E3 just after the trigger is incomplete'
    # Instance 1 realized before the bits (E2 outcome B): native logs partner_realized=1
    # only on a change, so none follows the bits; this is not F3a.
    $e2Lines = @('partner_stage=changed; stage=2; source=fixed; apply=warp; via=trigger', 'partner_lua=event=allactive_set',
        'partner_realized=1; R=1; id=0001; pool=01; ms_after_bits=0',
        'partner_probe=state; t_ms=2995000; stage=2; ids=0000,0001; pool=00,01; parts=3,1; rbits=0x3; s0=0x03; s8=0x01; q170=0x3; q180=0x0; q194=0x0; q198=0x0; glob=0; pb_guard=1; pb_flag=0,0; pb_state=3,0; pos0_mm=-11000,500,2000; yaw0_mrad=0; pos1_mm=-11000,500,5000; yaw1_mrad=0; rec1_mm=0,0,0; rec1_status=0x00000000')
    $outcomeB = foreach ($l in @($fx['e3-host'] -split "\r?\n")) {
        if ($l.StartsWith('partner_stage=changed; stage=3')) { $e2Lines; $l }
        elseif ($l.StartsWith('partner_realized=1')) { }
        else { $l }
    }
    $outcomeB = $outcomeB -join "`n"
    $v = Get-PV E3 host $outcomeB
    Assert-Verdict $v 'PASS' '' 'E3 after E2 outcome B (realized before the bits): PASS, not F3a'
    Assert-True ($v.data['realized_before_bits'] -and $null -eq $v.data['ms_after_bits'] -and (@($v.notes) -join ' ').Contains('already realized before partner_bits=set')) 'E3 notes the earlier realization; the 1 s rule is skipped'
    Assert-Equal "$($v.data['parts_ms_after_stage3'])/$($v.data['stable_s'])" '400/130.4' 'E3 outcome B: parts and stability measured from the stage=3 line on the probe clock'
    $lateParts = $outcomeB.Replace("partner_parts=loaded; R=1; ms_after_realize=4200`n", '').Replace('partner_probe=state; t_ms=3030600', "partner_parts=loaded; R=1; ms_after_realize=40000`npartner_probe=state; t_ms=3030600")
    Assert-Verdict (Get-PV E3 host $lateParts) 'FAIL' 'after stage=3' 'E3 outcome B: parts loaded later than 15 s after stage=3 fails'
    Assert-Verdict (Get-PV E3 host (Remove-Lines $outcomeB 'partner_parts=')) 'FAIL' 'within 20 s of stage=3' 'E3 outcome B: no parts 20 s after stage=3 fails (F3b)'
    $earlyParts = $outcomeB.Replace('partner_parts=loaded; R=1; ms_after_realize=4200', 'partner_warp_check; R=1; apply=warp; cmd_mm=-11000,500,5000; slot_mm=-11000,500,5000; err_mm=0').Replace('ms_after_bits=0', "ms_after_bits=0`npartner_parts=loaded; R=1; ms_after_realize=900")
    $v = Get-PV E3 host $earlyParts
    Assert-Verdict $v 'PASS' '' 'E3 outcome B: parts loaded during stage 2 are accepted'
    Assert-True ((@($v.notes) -join ' ').Contains('already loaded before stage=3')) 'E3 notes the parts loaded before stage=3'
    $thrashBefore = $outcomeB.Replace('partner_stage=changed; stage=3', "partner_realized=0; R=1; ms_after_realize=500`npartner_stage=changed; stage=3") -replace 'ids=0000,0001; pool=00,01', 'ids=0000,ffff; pool=00,ff'
    Assert-Verdict (Get-PV E3 host $thrashBefore) 'FAIL' 'F3a' 'E3: a realization undone before the bits does not count (F3a when none follows)'
    $otherMission = "partner_realized=1; R=1; id=0001; pool=01; ms_after_bits=90`n" + $fx['e3-host'].Replace('partner_realized=1; R=1; id=0001; pool=01; ms_after_bits=180', '') -replace 'ids=0000,0001; pool=00,01', 'ids=0000,ffff; pool=00,ff'
    Assert-Verdict (Get-PV E3 host $otherMission) 'FAIL' 'F3a' 'E3: a realization from before the mission boundary does not count'
    $inSegment = $fx['e3-host'].Replace("partner_realized=1; R=1; id=0001; pool=01; ms_after_bits=180`r`n", '').Replace('partner_lua=event=allactive_set', "partner_lua=event=allactive_set`r`npartner_realized=1; R=1; id=0001; pool=01; ms_after_bits=0")
    $v = Get-PV E3 host $inSegment
    Assert-Verdict $v 'PASS' '' 'E3: realized at stage 3 before the bits: PASS with the native parts timing'
    Assert-Equal "$($v.data['parts_ms_after_realize'])/$($v.data['realized_before_bits'])" '4200/True' 'E3 realized in the segment before the bits: ms_after_realize is the measure'
    $probeOnly = Remove-Lines $fx['e3-host'] 'partner_realized=1'
    $v = Get-PV E3 host $probeOnly
    Assert-Verdict $v 'PASS' '' 'E3: probes showing realization without a partner_realized=1 line are not F3a'
    Assert-True ((@($v.notes) -join ' ').Contains('no partner_realized=1 line belongs to this mission')) 'E3 notes the missing realization line'

    # E6: network-driven partner (both logs).
    $v = Get-PV E6 host $fx['e6-host'] @{ PeerObserverLog = $fx['e6-guest'] }
    Assert-Verdict $v 'PASS' '' 'E6 host PASS fixture'
    Assert-Equal "$($v.data['last_status'])/$($v.data['last_age_ms'])/$($v.data['cross_check_mm'])" 'live/28/20' 'E6 data'
    $guestE6 = Get-PV E6 guest $fx['e6-guest'] @{ PeerObserverLog = $fx['e6-host'] }
    Assert-Verdict $guestE6 'PASS' '' 'E6 guest PASS fixture'
    Assert-Verdict (Get-PV E6 host $fx['e6-host'].Replace('rx=1700', 'rx=1400') @{ PeerObserverLog = $fx['e6-guest'] }) 'FAIL' 'rx not increasing' 'E6 rx not increasing fails'
    Assert-Verdict (Get-PV E6 host $fx['e6-host'].Replace('rx=1700', 'rx=1400').Replace('drop_epoch=0', 'drop_epoch=40') @{ PeerObserverLog = $fx['e6-guest'] }) 'FAIL' 'mission mismatch' 'E6 epoch drops name the mission mismatch'
    Assert-Verdict (Get-PV E6 host ($fx['e6-host'] -replace 'status=live; last_age_ms=[0-9]+', 'status=stale; last_age_ms=900') @{ PeerObserverLog = $fx['e6-guest'] }) 'FAIL' 'status=live' 'E6 stale remote fails'
    Assert-Verdict (Get-PV E6 host $fx['e6-host'].Replace('pos1_mm=-9000,500,6520', 'pos1_mm=-9000,500,8520') @{ PeerObserverLog = $fx['e6-guest'] }) 'FAIL' 'cross-check' 'E6 coordinate mismatch fails'
    Assert-Verdict (Get-PV E6 host (Set-LastLine $fx['e6-host'] 'pos1_mm=-9000,500,6520' 'pos1_mm=-9000,500,7520') @{ PeerObserverLog = $fx['e6-guest'] }) 'INCOMPLETE' 'stand still' 'E6 cross-check waits until both stand still'
    Assert-Verdict (Get-PV E6 host $fx['e6-host']) 'INCOMPLETE' 'peer probes' 'E6 needs the peer log'
    Assert-Verdict (Get-PV E6 host $fx['e6-host'] @{ PeerObserverLog = $fx['e6-guest']; HostOnly = $true }) 'FAIL' 'both instances' 'E6 host-only run fails'
    Assert-Verdict (Get-PV E6 host ($fx['e6-host'] + "partner_realized=0; R=1; ms_after_realize=30000`r`n") @{ PeerObserverLog = $fx['e6-guest'] }) 'FAIL' 'thrash' 'E6 unrealize fails'
    $merged = Merge-PartnerSpikeVerdict -Experiment E6 -Role both -PerRole ([ordered]@{ host = $v; guest = (Get-PV E6 guest $fx['e6-guest'].Replace('rx=1700', 'rx=1400') @{ PeerObserverLog = $fx['e6-host'] }) })
    Assert-Equal $merged.verdict 'FAIL' 'merged verdict: any FAIL fails'
    Assert-True (@($merged.failed | Where-Object { $_.StartsWith('[guest] ') }).Count -gt 0 -and @($merged.matched | Where-Object { $_.StartsWith('[host] ') }).Count -gt 0) 'merged lists carry the role'
    Assert-Equal (Merge-PartnerSpikeVerdict -Experiment E6 -Role both -PerRole ([ordered]@{ host = $v; guest = $guestE6 })).verdict 'PASS' 'merged verdict: both PASS'
    Assert-Equal (Merge-PartnerSpikeVerdict -Experiment E0 -Role both -PerRole ([ordered]@{ host = $v; guest = (Get-PV E0 guest '') })).verdict 'INCOMPLETE' 'merged verdict: INCOMPLETE without a FAIL'
    Assert-Equal "$(Get-PartnerExperimentDefaultRole E0)/$(Get-PartnerExperimentDefaultRole E6)/$(Get-PartnerExperimentDefaultRole E3)" 'both/both/host' 'default roles'

    # E2, E4, E5, E8 on small inline logs built from the same line formats.
    $stage0 = Select-Lines $fx['e1-host'] 10
    function New-ProbeText([Int64]$T, [int]$Stage, [string]$Ids = '0000,ffff', [string]$Pool = '00,ff', [string]$S0 = '0x01', [string]$Q170 = '0x1') {
        "partner_probe=state; t_ms=$T; stage=$Stage; ids=$Ids; pool=$Pool; parts=3,0; rbits=0x1; s0=$S0; s8=0x01; q170=$Q170; q180=0x0; q194=0x0; q198=0x0; glob=0; pb_guard=1; pb_flag=0,0; pb_state=3,0; pos0_mm=-11000,500,2000; yaw0_mrad=0; pos1_mm=-11000,500,5000; yaw1_mrad=0; rec1_mm=0,0,0; rec1_status=0x00000000"
    }
    $e2 = @($stage0, 'partner_stage=changed; stage=2; source=fixed; apply=warp; via=trigger', (New-ProbeText 4000300 2), 'partner_lua=event=allactive_set',
        (New-ProbeText 4001000 2 -S0 '0x03' -Q170 '0x3'), (New-ProbeText 4011000 2 -S0 '0x03' -Q170 '0x3')) -join "`n"
    $v = Get-PV E2 host $e2
    Assert-Verdict $v 'PASS' '' 'E2 outcome A (ids[1] stays ffff for 10 s)'
    Assert-Equal $v.data['outcome'] 'A' 'E2 outcome A recorded'
    $v = Get-PV E2 host ($e2 + "`n" + (New-ProbeText 4012000 2 -Ids '0000,0001' -Pool '00,01' -S0 '0x03' -Q170 '0x3'))
    Assert-Equal "$($v.verdict)/$($v.data['outcome'])" 'PASS/B' 'E2 outcome B (all-active alone realized)'
    Assert-Verdict (Get-PV E2 host ($e2 -replace 's0=0x03; s8=0x01; q170=0x3', 's0=0x01; s8=0x01; q170=0x1')) 'FAIL' 'partner_force_exists=1' 'E2 s0 staying 0x01 fails'
    Assert-Verdict (Get-PV E2 host (Remove-Lines $e2 'partner_lua=event=allactive_set')) 'FAIL' 'allactive_set' 'E2 without allactive_set after 5 s fails'

    $orbit = [Collections.Generic.List[string]]::new()
    $orbit.Add($stage0); $orbit.Add('partner_stage=changed; stage=3; source=orbit; apply=warp; via=trigger'); $orbit.Add((New-ProbeText 6000500 3 -Ids '0000,0001' -Pool '00,01'))
    foreach ($i in 0..4) {
        $x = [int](3000 * [Math]::Cos($i)); $z = [int](3000 * [Math]::Sin($i))
        $orbit.Add("partner_warp_check; R=1; apply=warp; cmd_mm=$x,500,$z; slot_mm=$x,500,$z; err_mm=0")
    }
    $orbit.Add((New-ProbeText 6005500 3 -Ids '0000,0001' -Pool '00,01'))
    $e4 = $orbit -join "`n"
    Assert-Verdict (Get-PV E4 host $e4) 'PASS' '' 'E4 orbit PASS'
    Assert-Verdict (Get-PV E4 host ($e4 -replace 'slot_mm=-?[0-9]+,500,-?[0-9]+', 'slot_mm=100,500,100')) 'FAIL' 'first spot' 'E4 partner stuck at its first spot fails'
    Assert-Verdict (Get-PV E4 host ($e4 -replace 'err_mm=0', 'err_mm=9')) 'FAIL' 'err_mm>5' 'E4 err_mm>5 fails'
    Assert-Verdict (Get-PV E4 host ($e4 -replace 'ids=0000,0001; pool=00,01', 'ids=0000,ffff; pool=00,ff')) 'FAIL' 'not realized' 'E4 needs a realized partner'
    Assert-Verdict (Get-PV E4 host (Select-Lines $e4 15)) 'INCOMPLETE' '4 partner_warp_check' 'E4 waits for 4 warp checks'
    $slot = $e4.Replace('stage=3; source=orbit; apply=warp; via=trigger', 'stage=3; source=orbit; apply=slot; via=trigger').Replace('apply=warp; cmd_mm', 'apply=slot; cmd_mm')
    $e5 = $slot + "`npartner_stage=changed; apply=both; via=trigger`n" + (@($orbit | Select-Object -Skip 2) -join "`n").Replace('apply=warp; cmd_mm', 'apply=both; cmd_mm')
    Assert-Verdict (Get-PV E5 host $e5) 'PASS' '' 'E5 slot then both PASS (a trigger may change apply alone)'
    Assert-Verdict (Get-PV E5 host $slot) 'INCOMPLETE' 'apply=both' 'E5 waits for apply=both'
    Assert-Verdict (Get-PV E5 host $e4) 'INCOMPLETE' 'apply=slot' 'E5 needs apply=slot'

    $e8 = @($fx['e3-host'].TrimEnd(), 'partner_bits=cleared; R=1; reason=trigger', 'partner_lua=event=allactive_cleared', 'partner_realized=0; R=1; ms_after_realize=131000',
        (New-ProbeText 3200000 3), 'partner_stage=changed; stage=3; source=fixed; apply=warp; via=trigger', 'partner_bits=set; R=1; q194=0x2; q198=0x2',
        'partner_realized=1; R=1; id=0001; pool=01; ms_after_bits=90', 'partner_stop; cleared=1; teardown=clear',
        'mission_termination=entered; accepted=1 thread=10 runtime_thread=10 outgoing_mission=30050 current_mission=30050 current_read=1',
        'partner_cfg=ready; flags=0x00000001; count=2; realized_count=2; local=0; R=1; online_bit=0; sync=null; code_gate=ok') -join "`n"
    $v = Get-PV E8 host $e8
    Assert-Verdict $v 'PASS' '' 'E8 teardown, re-realization and re-entry PASS'
    Assert-Equal $v.data['teardown'] 'clear' 'E8 teardown mode recorded'
    Assert-Verdict (Get-PV E8 host (Remove-Lines $e8 'partner_cfg=ready')) 'INCOMPLETE' 'step 4' 'E8 waits for the re-entry'
    Assert-Verdict (Get-PV E8 host (Remove-Lines $e8 'partner_stop')) 'INCOMPLETE' 'step 3' 'E8 waits for the mission exit'
    Assert-Verdict (Get-PV E8 host (Remove-Lines $e8 'partner_realized=0')) 'INCOMPLETE' 'partner_realized=0' 'E8 waits for the unrealize'
    Assert-Verdict (Get-PV E8 host (Remove-Lines $e8 'partner_bits=cleared')) 'INCOMPLETE' 'step 1' 'E8 needs the stop trigger'
    Assert-Verdict (Get-PV E8 host ($e8 + "`ncrash_probe=av; rva=0x9c85d0; access=read; target=0x10; thread=7; stage=3; realized=1")) 'FAIL' 'crash_probe=av' 'E8 crash fails'

    # Animation / appearance experiments (docs/partner-anim-appearance.md) on inline logs.
    $animHead = @(
        'partner=configured; mode=offline_v1; stage=0; source=fixed; apply=warp; rate_hz=30; interp_ms=100; offset_cm=300; teardown=clear; anim=off; aset=qs; reset=n; look=copy; t_pinned=0',
        'partner_cfg=ready; flags=0x00000001; count=2; realized_count=2; local=0; R=1; online_bit=0; sync=null; code_gate=ok')
    function New-AnimLog([string]$Changed, [string[]]$Lines) { (@($animHead) + @("partner_stage=changed; $Changed; via=trigger") + $Lines) -join "`r`n" }
    $census0 = 'partner_census; k=0; id=0; base=0; count=1; blk=ok; blk_vt=0x2295210; ps_vt=0x23b09f0; cs_vt=0x22e6070; t=set; t_vt=0x2400000; t68=0x1000000; t70=0x1000100; a0_vt=0x2410000; a080=0x1100000; c0_vt=0x2420000; c090=0x1200000; b70_vt=0x0; stance=ok; psw=0x11; equip_vt=0x23c14b0; same_t=1; same_a0=1; same_c0=1; verdict=ok'
    $census1 = $census0.Replace('k=0; id=0; base=0', 'k=1; id=1; base=1')
    $a0 = New-AnimLog 'stage=3; source=fixed; apply=warp; anim=off; aset=qs; reset=n; look=copy; probe=off' @($census0, $census1)
    $v = Get-PV A0 host $a0
    Assert-Verdict $v 'PASS' '' 'A0 census PASS'
    Assert-Equal "$($v.data['k1_t_vt'])/$($v.data['k1_t68'])/$($v.data['k1_stance'])" '0x2400000/0x1000000/ok' 'A0 records the T RVAs to pin and the stance verdict'
    $v = Get-PV A0 host (New-AnimLog 'stage=3; source=fixed; apply=warp' @($census0, $census1.Replace('stance=ok; psw=0x11', 'stance=ps_sub; psw=0x00')))
    Assert-True ($v.verdict -eq 'PASS' -and (@($v.notes) -join ' ').Contains('k=1 stance=ps_sub')) 'A0: a missing stance prerequisite is a note (xform unaffected)'
    Assert-Verdict (Get-PV A0 host $a0.Replace('k=1; id=1; base=1', 'k=1; id=1; base=1; x=0').Replace('same_c0=1; verdict=ok', 'same_c0=1; verdict=t_class')) 'FAIL' 'verdict=t_class' 'A0 identity failure fails'
    Assert-Verdict (Get-PV A0 host (New-AnimLog 'stage=3; source=fixed; apply=warp' @($census0.Replace('verdict=ok', 'verdict=unrealized')))) 'INCOMPLETE' 'realized and loaded' 'A0 before realization is incomplete'
    Assert-Verdict (Get-PV A0 host (New-AnimLog 'stage=3; source=fixed; apply=warp' @())) 'INCOMPLETE' 'census=1' 'A0 without census lines'
    $a1Lines = @('partner_aset=q; state=applied; act=1; s=1; v=0',
        'partner_anim; act=0001,0001; s=1,1; v=0,0; q28=0,0; qd8=ffff,ffff; psw=11,11; rec1=0x00000010',
        'partner_anim; act=0003,0003; s=2,2; v=0,0; q28=0,0; qd8=ffff,ffff; psw=11,11; rec1=0x00000020')
    $a1 = New-AnimLog 'stage=3; source=fixed; apply=hybrid; anim=mirror; aset=q; reset=n; look=copy; probe=off' $a1Lines
    $v = Get-PV A1 host $a1
    Assert-Verdict $v 'PASS' '' 'A1 arrays mirrored PASS'
    Assert-True (!$v.data['per_tick_warp']) 'A1 with hybrid: no per-tick Warp'
    $v = Get-PV A1 host ($a1 + "`r`npartner_aset=q; state=refused; reason=act_domain; act=20")
    Assert-True ($v.verdict -eq 'PASS' -and (@($v.notes) -join ' ').Contains('act_domain skips')) 'A1: act_domain skips are notes, not failures'
    $v = Get-PV A1 host (New-AnimLog 'stage=3; source=fixed; apply=warp; anim=mirror; aset=q; reset=n; look=copy; probe=off' $a1Lines)
    Assert-True ($v.verdict -eq 'PASS' -and $v.data['per_tick_warp'] -and (@($v.notes) -join ' ').Contains('Warps every tick')) 'A1 with apply=warp: the per-tick Warp is noted'
    Assert-Equal "$($v.data['slot1_equals_slot0'])/$($v.data['stances_seen'])" '2/1,2' 'A1 data'
    Assert-Verdict (Get-PV A1 host ($a1 + "`r`npartner_aset=q; state=refused; reason=count")) 'FAIL' 'reason=count' 'A1 refused write fails'
    Assert-Verdict (Get-PV A1 host (New-AnimLog 'stage=3; source=fixed; apply=warp; anim=mirror; aset=q' @($a1Lines[0], $a1Lines[1]))) 'INCOMPLETE' 'changes stance' 'A1 needs stance changes'
    Assert-Verdict (Get-PV A1 host (New-AnimLog 'stage=3; source=fixed; apply=warp; anim=off' $a1Lines)) 'INCOMPLETE' 'anim=mirror' 'A1 needs the anim=mirror segment'
    $a3Sets = @('partner_stance=set; R=1; id=1; from=0; to=2; psw=0x22; q30=2', 'partner_stance=set; R=1; id=1; from=2; to=1; psw=0x11; q30=1')
    $a3 = New-AnimLog 'stage=3; source=fixed; apply=hybrid; anim=mirror; aset=qs; reset=n; look=copy; probe=off' $a3Sets
    Assert-Verdict (Get-PV A3 host $a3) 'PASS' '' 'A3 SetStance PASS'
    $revert = "`r`npartner_stance=reverted; R=1; psw=0x11; expected=0x22; ms_after_set=120"
    $v = Get-PV A3 host ($a3 + $revert)
    Assert-True ($v.verdict -eq 'PASS' -and (@($v.notes) -join ' ').Contains('try aset=r') -and $v.data['stance_reverted'] -eq 1) 'A3 revert without per-tick Warp: the action plugin'
    $a3w = New-AnimLog 'stage=3; source=fixed; apply=warp; anim=mirror; aset=qs; reset=n; look=copy; probe=off' $a3Sets
    $v = Get-PV A3 host ($a3w + $revert)
    Assert-True ($v.verdict -eq 'PASS' -and (@($v.notes) -join ' ').Contains('not attributable') -and !(@($v.notes) -join ' ').Contains('try aset=r')) 'A3 revert with per-tick Warp is not attributed to the action plugin'
    $v = Get-PV A3 host ($a3 + "`r`npartner_xform=refused; reason=t_class; fallback=warp" + $revert)
    Assert-True ((@($v.notes) -join ' ').Contains('fell back to Warp') -and (@($v.notes) -join ' ').Contains('not attributable')) 'A3 revert after an xform fallback is not attributable'
    Assert-Verdict (Get-PV A3 host ($a3 + "`r`npartner_call=fault; fn=set_stance; native_calls=disabled")) 'FAIL' 'fn=set_stance' 'A3 fault fails'
    $a3Retry = New-AnimLog 'stage=3; source=fixed; apply=hybrid; anim=mirror; aset=qs; reset=n; look=copy; probe=off' @(
        'partner_stance=refused; R=1; to=2; reason=ps_sub', $a3Sets[0], $a3Sets[1])
    $v = Get-PV A3 host $a3Retry
    Assert-True ($v.verdict -eq 'PASS' -and (@($v.notes) -join ' ').Contains('transient partner_stance=refused')) 'A3: a refusal retried and applied is a note'
    Assert-Verdict (Get-PV A3 host ($a3 + "`r`npartner_stance=refused; R=1; to=3; reason=ps_sub")) 'FAIL' 'reason=ps_sub' 'A3: a refusal never applied fails'
    Assert-Verdict (Get-PV A3 host (New-AnimLog 'stage=3; source=fixed; apply=warp; anim=mirror; aset=qs' @('partner_stance=set; R=1; id=1; from=0; to=2; psw=0x22; q30=2'))) 'INCOMPLETE' '2 partner_stance=set' 'A3 needs two targets'
    $xrow = 'partner_xform; R=1; apply=xform; cmd_mm=1000,0,3000; body_mm=1000,0,3012; err_mm=12; yaw_err_mrad=3; reset=n'
    $m1 = New-AnimLog 'stage=3; source=orbit; apply=xform; anim=off; aset=qs; reset=n; look=copy; probe=off' @('partner_xform=applied; R=1; apply=xform; reset=n', $xrow, $xrow, $xrow, $xrow)
    $v = Get-PV M1 host $m1
    Assert-Verdict $v 'PASS' '' 'M1 xform PASS'
    Assert-Equal "$($v.data['err_mm_max'])/$($v.data['yaw_err_mrad_max'])/$($v.data['xform_checks'])" '12/3/4' 'M1 data'
    Assert-Verdict (Get-PV M1 host ($m1 + "`r`n" + $xrow.Replace('err_mm=12', 'err_mm=2400'))) 'FAIL' 'err_mm>1500' 'M1 body not following fails'
    Assert-Verdict (Get-PV M1 host (New-AnimLog 'stage=3; source=orbit; apply=xform' @('partner_xform=refused; reason=t_class; fallback=warp'))) 'FAIL' 'reason=t_class' 'M1 identity refusal fails'
    Assert-Verdict (Get-PV M1 host (New-AnimLog 'stage=3; source=orbit; apply=hybrid' @('partner_xform=refused; reason=unrealized; fallback=warp'))) 'INCOMPLETE' 'partner_xform=applied' 'M1 before realization is incomplete'
    $n1 = New-AnimLog 'stage=3; source=net; apply=hybrid; anim=net; aset=qs; reset=n; look=net; probe=off' @(
        'partner_anim=source; anim=net; valid=1; act=3; s=2; v=0', 'partner_stance=set; R=1; id=1; from=0; to=2; psw=0x22; q30=2')
    Assert-Verdict (Get-PV N1 host $n1) 'PASS' '' 'N1 network anim PASS'
    Assert-Verdict (Get-PV N1 host (New-AnimLog 'stage=3; source=net; apply=hybrid; anim=net' @('partner_anim=source; anim=net; valid=0; act=0; s=0; v=0'))) 'INCOMPLETE' 'anim extension' 'N1 without anim frames'
    $lookApply = 'partner_look=apply; R=1; source=quiet; type=6; parts=27; camo=116; hand=0; face=0; fe=0; rev=0; avatar_as_snake=0'
    $lookLua = 'partner_lua=event=parts_set; R=1; type=6; parts=27; camo=116; hand=0; face=0'
    $lookLoaded = 'partner_look=loaded; R=1; q40=6; q48=27; q50=116; q58=0; q60=0; q68=0; ms_after_apply=640; cycle=1'
    $b1 = New-AnimLog 'stage=3; source=orbit; apply=warp; anim=off; aset=qs; reset=n; look=quiet; probe=off' @($lookApply, $lookLua, $lookLoaded)
    $v = Get-PV B1 host $b1
    Assert-Verdict $v 'PASS' '' 'B1 local Quiet PASS'
    Assert-Equal $v.data['look_ms_after_apply'] ([Int64]640) 'B1 reload time recorded'
    Assert-Verdict (Get-PV B1 host ($b1.Replace($lookLoaded, 'partner_look=timeout; R=1; state=1; q40=6; q180=0x2; realized=1'))) 'FAIL' 'partner_look=timeout' 'B1 timeout fails'
    Assert-Verdict (Get-PV B1 host ($b1 + "`r`npartner_lua=event=parts_set_refused; reason=bad_look")) 'FAIL' 'parts_set_refused' 'B1 Lua refusal fails'
    Assert-Verdict (Get-PV B1 host (Remove-Lines $b1 'partner_look=loaded')) 'INCOMPLETE' 'partner_look=loaded' 'B1 waits for the reload'
    $b4 = New-AnimLog 'stage=3; source=net; apply=warp; anim=off; aset=qs; reset=n; look=net; probe=off' @(
        'partner_lua=event=parts_copied; R=1', 'partner_net=look_rx; type=6; parts=27; camo=116; hand=0; face=0; fe=0; rev=1; loaded=1',
        $lookApply.Replace('source=quiet', 'source=net').Replace('rev=0', 'rev=1'), $lookLua, $lookLoaded)
    $v = Get-PV B4 host $b4
    Assert-Verdict $v 'PASS' '' 'B4 network look PASS (the copy before the first look is allowed)'
    Assert-Equal $v.data['remote_type'] '6' 'B4 remote type recorded'
    Assert-Verdict (Get-PV B4 host ($b4 + "`r`npartner_lua=event=parts_copied; R=1")) 'FAIL' 'copy must stop' 'B4 copy after the network look fails'
    Assert-Verdict (Get-PV B4 host (Remove-Lines $b4 'partner_net=look_rx')) 'INCOMPLETE' 'look_rx' 'B4 needs the peer look'
    $c1 = @('character_config=quiet; fallback=dd_male; coop_ini=1; reset_bypass_needed=1; applied_in=TppMain.OnAllocate',
        'player_type_patch=active; branch_rva=0x9cd370',
        'character=apply; mission=30050; requested=quiet; applied=quiet; type=6; reason=override; bypass=1; real=0; start=continue; always=1',
        'character=allocated; mission=30050; vars=6; parts=27; camo=116; temp=1; start=continue; late=0') -join "`r`n"
    Assert-Verdict (Get-PV C1 guest $c1) 'PASS' '' 'C1 guest plays Quiet PASS'
    Assert-Verdict (Get-PV C1 guest $c1.Replace('character_config=quiet', 'character_config=default')) 'FAIL' 'character=quiet not in place' 'C1 default character fails'
    Assert-Verdict (Get-PV C1 guest $c1.Replace('applied=quiet', 'applied=dd_male').Replace('reason=override', 'reason=bypass_unavailable')) 'FAIL' 'reason=bypass_unavailable' 'C1 fallback fails'
    Assert-Verdict (Get-PV C1 guest $c1.Replace('vars=6', 'vars=0')) 'FAIL' 'expected 6' 'C1 reset to Snake fails'
    Assert-Verdict (Get-PV C1 guest ($c1 -split "`r`n" | Select-Object -First 1)) 'INCOMPLETE' 'character=apply' 'C1 waits for the allocation'
    Assert-Equal "$(Get-PartnerExperimentDefaultRole N1)/$(Get-PartnerExperimentDefaultRole B4)/$(Get-PartnerExperimentDefaultRole C1)/$(Get-PartnerExperimentDefaultRole M1)" 'both/both/guest/host' 'new default roles'

    foreach ($exp in 'E0', 'E1', 'E2', 'E3', 'E4', 'E5', 'E6', 'E8', 'A0', 'A1', 'A3', 'M1', 'N1', 'B1', 'B4', 'C1') {
        $plan = Get-PartnerExperimentPlan -Experiment $exp
        Assert-True ($plan.TimeoutSec -gt 0 -and $plan.Steps.Count -gt 0 -and $plan.Role -eq (Get-PartnerExperimentDefaultRole $exp)) "$exp plan"
        foreach ($s in @($plan.Steps | Where-Object { $_.Kind -eq 'trigger' })) { Assert-True (Test-PartnerTriggerText $s.Value) "$exp plan trigger '$($s.Value)' is valid" }
        foreach ($s in @($plan.Steps | Where-Object { $_.Kind -eq 'input' })) { Assert-True ($null -ne (ConvertFrom-LabKeySpec -Spec $s.Value)) "$exp plan input '$($s.Value)' is a valid key spec" }
    }
    Assert-Equal @((Get-PartnerExperimentPlan -Experiment E4).Steps | Where-Object { $_.Kind -eq 'screens' }).Count 4 'E4 plan takes 4 screenshots'
    foreach ($exp in 'A1', 'A3') {
        $trigger = @((Get-PartnerExperimentPlan -Experiment $exp).Steps | Where-Object { $_.Kind -eq 'trigger' })[0].Value
        Assert-True ($trigger.Contains('apply=hybrid') -and $trigger.Contains('reset=n')) "$exp plan uses hybrid movement without Warp's per-tick resets"
    }
    Assert-Equal (@((Get-PartnerExperimentPlan -Experiment E0).Steps | Where-Object { $_.Kind -eq 'input' } | ForEach-Object { "$($_.Role):$($_.Value)" }) -join ',') 'host:w+a:6000,guest:w+a:6000' 'E0 plan walks both instances (the units rule is judged on both)'
    # dm=map: D4 on the host (Quiet partner), D5 the same plan on the guest (Snake partner, local Quiet).
    foreach ($exp in 'D4', 'D5') {
        $plan = Get-PartnerExperimentPlan -Experiment $exp
        $role = if ($exp -eq 'D4') { 'host' } else { 'guest' }
        Assert-True ($plan.TimeoutSec -gt 0 -and $plan.Role -eq $role -and (Get-PartnerExperimentDefaultRole $exp) -eq $role) "$exp plan role $role"
        $triggers = @($plan.Steps | Where-Object { $_.Kind -eq 'trigger' })
        foreach ($s in $triggers) { Assert-True ((Test-PartnerTriggerText $s.Value) -and $s.Role -eq $role) "$exp plan trigger '$($s.Value)' is valid" }
        Assert-Equal (@($triggers | ForEach-Object { $_.Value }) -join ' | ') 'stage=3 source=orbit apply=xform reset=n anim=off input=off dm=map dmspeed=300:2600:5600 | dmspeed=300:1500:5600 | dmspeed=300:1000:2000 | dmspeed=300:2600:5600 | dm=off' "$exp plan: walk, run, dash, stances, off"
        Assert-Equal @($plan.Steps | Where-Object { $_.Kind -eq 'screens' }).Count 5 "$exp plan takes 5 screenshots"
    }
    $d4 = (@('partner_dm=patched; rva=0x1198e25; old=440f44c7; new=0f1f4000',
        'partner_dmmap=table; cells=0,10,11,5,1,3,4,4,7,6,6,6; speeds=300:2600:5600; reason=start',
        'partner_dmmap=switch; step=play; from=none; to=10; stance=0; move=1; speed_mmps=2351; via=speed; stance_via=flags; wait_ms=0',
        'partner_dmmap=playing; gani=10; after_ms=33',
        'partner_dmmap; phase=playing; cur=10; want=10; stance=0; move=1; via=speed; speed_mmps=2351; switches=1; plays=1; stops=0; deferrals=0; timeouts=0; unconsumed=0; lost=0; adopted=0; bad=0x0; failures=0; deferred=none',
        'partner_dmmap=released; result=stopped; to=off; ms=33',
        'partner_dm=reverted; reason=trigger') -join "`r`n")
    Assert-Verdict (Get-PV D4 host $d4) 'PASS' '' 'D4 dm=map PASS'
    Assert-Verdict (Get-PV D5 guest $d4) 'PASS' '' 'D5 dm=map PASS on the guest'
    Assert-Verdict (Get-PV D4 host ($d4 + "`r`npartner_dmmap=bad_gani; gani=11; reason=ended; strikes=2")) 'FAIL' 'bad_gani; gani=11' 'D4 bad gani fails'
    Assert-Verdict (Get-PV D4 host ($d4 + "`r`npartner_dmmap=failed; failures=5; last=unconsumed")) 'FAIL' 'partner_dmmap=failed' 'D4 failed mapper fails'
    Assert-Verdict (Get-PV D4 host ($d4 + "`r`npartner_dm=unconsumed; ms=2000")) 'FAIL' 'partner_dm=unconsumed' 'D4 unconsumed fails'
    $timeouts = (1..4 | ForEach-Object { 'partner_dmmap=stop_timeout; gani=10; ms=1023; failures=1; backoff_ms=1000; reason=stop_timeout' }) -join "`r`n"
    Assert-Verdict (Get-PV D4 host ($d4 + "`r`n" + $timeouts)) 'FAIL' 'stop_timeout x4' 'D4 repeated stop timeouts fail'
    Assert-Verdict (Get-PV D4 host (Remove-Lines $d4 'partner_dmmap=released')) 'INCOMPLETE' 'partner_dmmap=released' 'D4 waits for the release'

    # Scripts on a fixture run (no game): Test-PartnerSpike.ps1, Set-PartnerTrigger.ps1 and the smoke's offline refusal.
    $pwshExe = (Get-Process -Id $PID).Path
    $pr = Join-Path $fixture 'partner-run'
    $prPaths = @{ host = (Get-InstancePaths -GameDirectory (Join-Path $pr 'MGS_TPP')); guest = (Get-InstancePaths -GameDirectory (Join-Path $pr 'MGS_TPP_guest')) }
    foreach ($p in $prPaths.Values) { $null = New-Item -ItemType Directory -Path $p.Mod -Force }
    $prJson = Get-Content -LiteralPath (Join-Path $projectRoot 'tools/lab/lab.json') -Raw | ConvertFrom-Json
    $prJson.paths.hostDir = $prPaths.host.Game; $prJson.paths.guestDir = $prPaths.guest.Game; $prJson.paths.runDir = (Join-Path $pr 'lab')
    $prConfig = Join-Path $pr 'lab.json'
    [IO.File]::WriteAllText($prConfig, ($prJson | ConvertTo-Json -Depth 8))
    function New-PartnerFixtureRun([string]$Name, [hashtable]$Logs, [switch]$Online) {
        # Each script-observer.log starts with lines of an earlier run that the recorded offsets skip.
        $dir = Join-Path $pr "lab/runs/$Name"
        $null = New-Item -ItemType Directory -Path $dir -Force
        $instances = [ordered]@{}
        foreach ($role in 'host', 'guest') {
            $paths = $prPaths[$role]
            $previous = "crash_probe=av; rva=0x1; access=read; target=0x0; thread=1; stage=0; realized=0`r`npartner_cfg=ready; flags=0x00000101; count=2; realized_count=2; local=0; R=1; online_bit=1; sync=set; code_gate=ok`r`n"
            [IO.File]::WriteAllText((Join-Path $paths.Mod 'script-observer.log'), $previous + [string]$Logs[$role])
            $early = if ($role -eq 'host') { "early=start; version=0.11.1; lab=1; role=host`r`nmutex=unchanged`r`nidentity=real`r`nsaves=redirected; dir=$($paths.SaveDir)`r`n" }
            else { "early=start; version=0.11.1; lab=1; role=guest`r`nmutex=renamed; suffix=-guest`r`nidentity=fake; steamid=76561202255233008`r`nsaves=redirected; dir=$($paths.SaveDir)`r`n" }
            [IO.File]::WriteAllText((Join-Path $paths.Mod 'early.log'), $early)
            [IO.File]::WriteAllText((Join-Path $paths.Mod 'steam-observer.log'), "master_server=redirected; method=in_place; url=$deadUrl`r`n")
            $instances[$role] = [ordered]@{
                role = $role; dir = $paths.Game; exe = (Join-Path $paths.Game 'mgsvtpp.exe'); pid = 0
                log_offsets = [ordered]@{ 'script-observer.log' = [Text.Encoding]::UTF8.GetByteCount($previous); 'early.log' = 0; 'steam-observer.log' = 0 }
                expected = [ordered]@{
                    save_dir = $paths.SaveDir; master_url = $deadUrl
                    steamid = $(if ($role -eq 'guest') { '76561202255233008' } else { '' }); mutex_suffix = $(if ($role -eq 'guest') { '-guest' } else { '' })
                }
            }
        }
        $run = [ordered]@{ product = 'mgs-coop-lab-run'; schema = 2; stamp = $Name; run_dir = $dir; started_utc = [DateTime]::UtcNow.ToString('o'); host_only = $false; fuse = $null; instances = $instances }
        if (!$Online) { $run.offline = $true; $run.offline_master_url = $deadUrl }
        Write-JsonFile -Path (Join-Path $dir 'run.json') -Value $run
        return $dir
    }
    $testSpike = Join-Path $projectRoot 'tools/lab/Test-PartnerSpike.ps1'
    function Invoke-TestSpike([string[]]$Arguments) {
        $out = @(& $pwshExe -NoProfile -NonInteractive -File $testSpike -ConfigPath $prConfig @Arguments 2>&1)
        $code = $LASTEXITCODE
        $json = $null
        try { $json = (@($out | Where-Object { $_ -is [string] }) -join "`n") | ConvertFrom-Json } catch { $json = $null }
        [pscustomobject]@{ Code = $code; Text = ($out -join "`n"); Json = $json }
    }
    $runPass = New-PartnerFixtureRun 'e0-pass' @{ host = $fx['e0-host']; guest = $fx['e0-guest'] }
    $r = Invoke-TestSpike @('-Experiment', 'E0', '-RunDir', $runPass, '-NoProcessCheck', '-OutFile', (Join-Path $runPass 'e0.json'))
    Assert-Equal $r.Code 0 ('Test-PartnerSpike E0 PASS exit code: ' + $r.Text)
    Assert-Equal "$($r.Json.experiment)/$($r.Json.role)/$($r.Json.verdict)" 'E0/both/PASS' 'Test-PartnerSpike JSON verdict (E0 defaults to both roles)'
    Assert-True ($r.Json.per_role.host.verdict -eq 'PASS' -and $r.Json.per_role.guest.verdict -eq 'PASS' -and $r.Json.run.offline) 'per-role verdicts and run info'
    Assert-Equal @($r.Json.matched | Where-Object { $_.StartsWith('[host] partner_cfg=ready') }).Count 1 'matched lines carry the role'
    Assert-True (!(@($r.Json.matched) -join "`n").Contains('online_bit=1') -and !(@($r.Json.failed) -join "`n").Contains('rva=0x1')) 'lines before the run.json offsets are ignored'
    Assert-True ((Test-Path -LiteralPath (Join-Path $runPass 'e0.json')) -and ([IO.File]::ReadAllText((Join-Path $runPass 'e0.json')) | ConvertFrom-Json).verdict -eq 'PASS') '-OutFile written'
    $r = Invoke-TestSpike @('-Experiment', 'E0', '-RunDir', $runPass)
    Assert-True ($r.Code -eq 1 -and (@($r.Json.failed) -join ' ').Contains('process_exited')) 'liveness is part of the verdict while the run is up (pid 0 is not running)'
    Write-JsonFile -Path (Join-Path $runPass 'stop.json') -Value ([ordered]@{ product = 'mgs-coop-lab-stop' })
    $r = Invoke-TestSpike @('-Experiment', 'E0', '-RunDir', $runPass)
    Assert-True ($r.Code -eq 0 -and (@($r.Json.notes) -join ' ').Contains('stop.json')) 'after Stop-Lab liveness is not checked'
    # stop.json log_end_offsets (Stop-Lab) bound a stopped run: lines a later run appends are ignored.
    $ends = [ordered]@{}
    foreach ($role in 'host', 'guest') {
        $ends[$role] = [ordered]@{}
        foreach ($log in 'script-observer.log', 'early.log', 'steam-observer.log') { $ends[$role][$log] = Get-FileLengthOrZero (Join-Path $prPaths[$role].Mod $log) }
    }
    Write-JsonFile -Path (Join-Path $runPass 'stop.json') -Value ([ordered]@{ product = 'mgs-coop-lab-stop'; log_end_offsets = $ends })
    [IO.File]::AppendAllText((Join-Path $prPaths.host.Mod 'script-observer.log'), "crash_probe=av; rva=0x777; access=read; target=0x0; thread=7; stage=0; realized=0`r`n")
    $r = Invoke-TestSpike @('-Experiment', 'E0', '-RunDir', $runPass)
    Assert-True ($r.Code -eq 0 -and !(@($r.Json.failed) -join ' ').Contains('rva=0x777') -and $r.Json.run.bounded_by_stop) ('a stopped run is read up to stop.json log_end_offsets: ' + $r.Text)
    Write-JsonFile -Path (Join-Path $runPass 'stop.json') -Value ([ordered]@{ product = 'mgs-coop-lab-stop' })
    $r = Invoke-TestSpike @('-Experiment', 'E0', '-RunDir', $runPass)
    Assert-True ($r.Code -eq 1 -and (@($r.Json.failed) -join ' ').Contains('rva=0x777') -and (@($r.Json.notes) -join ' ').Contains('not the current one')) 'without end offsets an older run includes later lines, with a note'
    $runSolo = New-PartnerFixtureRun 'e0-solo' @{ host = $fx['e0-host']; guest = '' }
    $soloJson = Get-Content -LiteralPath (Join-Path $runSolo 'run.json') -Raw | ConvertFrom-Json
    $soloJson.instances.PSObject.Properties.Remove('guest')
    Write-JsonFile -Path (Join-Path $runSolo 'run.json') -Value $soloJson
    $r = Invoke-TestSpike @('-Experiment', 'E0', '-RunDir', $runSolo, '-NoProcessCheck')
    Assert-True ($r.Code -eq 2 -and (@($r.Json.missing) -join ' ').Contains('the guest instance')) ('E0 on a run without a guest instance is INCOMPLETE: ' + $r.Text)
    $runFail = New-PartnerFixtureRun 'e3-fail' @{ host = ($fx['e3-host'] + "partner_realized=0; R=1; ms_after_realize=60000`r`n"); guest = '' }
    $r = Invoke-TestSpike @('-Experiment', 'E3', '-RunDir', $runFail, '-NoProcessCheck')
    Assert-Equal "$($r.Code)/$($r.Json.role)/$($r.Json.verdict)" '1/host/FAIL' 'Test-PartnerSpike E3 FAIL (default role host), exit code 1'
    $r = Invoke-TestSpike @('-Experiment', 'E1', '-RunDir', $runFail, '-NoProcessCheck')
    Assert-Equal "$($r.Code)/$($r.Json.verdict)" '2/INCOMPLETE' 'Test-PartnerSpike INCOMPLETE exit code 2'
    $runOnline = New-PartnerFixtureRun 'online' @{ host = $fx['e0-host']; guest = $fx['e0-guest'] } -Online
    $r = Invoke-TestSpike @('-Experiment', 'E0', '-RunDir', $runOnline, '-NoProcessCheck', '-Role', 'host')
    Assert-True ($r.Code -eq 1 -and (@($r.Json.failed) -join ' ').Contains('run_not_offline')) 'E0 requires a Start-Lab -Offline run'
    $passThru = & $testSpike -Experiment E6 -RunDir (New-PartnerFixtureRun 'e6' @{ host = $fx['e6-host']; guest = $fx['e6-guest'] }) -ConfigPath $prConfig -NoProcessCheck -PassThru
    Assert-Equal "$($passThru.verdict)/$($passThru.per_role.host.data.cross_check_mm)/$($passThru.per_role.guest.data.cross_check_mm)" 'PASS/20/10' 'E6 through the script: both logs, cross-check, -PassThru'

    $setTrigger = Join-Path $projectRoot 'tools/lab/Set-PartnerTrigger.ps1'
    function Invoke-SetTrigger([string[]]$Arguments) {
        $out = @(& $pwshExe -NoProfile -NonInteractive -File $setTrigger -ConfigPath $prConfig @Arguments 2>&1)
        [pscustomobject]@{ Code = $LASTEXITCODE; Text = ($out -join "`n") }
    }
    $r = Invoke-SetTrigger @('-RunDir', $runPass, '-Role', 'host', '-Text', ' apply=slot  stage=3 ')
    Assert-Equal $r.Code 0 ('Set-PartnerTrigger succeeds: ' + $r.Text)
    $bytes = [IO.File]::ReadAllBytes($prPaths.host.PartnerTrigger)
    Assert-Equal ([Text.Encoding]::ASCII.GetString($bytes)) "stage=3 apply=slot`n" 'canonical trigger line, LF, ASCII'
    Assert-True ($bytes.Length -le 63 -and @(Get-ChildItem -LiteralPath $prPaths.host.Mod -Filter 'partner.trigger.*.tmp').Count -eq 0) 'at most 63 bytes; no temporary file left (temp + rename)'
    Assert-True ([IO.File]::ReadAllText((Join-Path $runPass 'partner-triggers.log')).Contains(' host stage=3 apply=slot')) 'trigger recorded in the run folder'
    $r = Invoke-SetTrigger @('-RunDir', $runPass, '-Role', 'host', '-Text', 'stop')
    Assert-True ($r.Code -ne 0 -and $r.Text.Contains('not consumed')) 'an unconsumed trigger is not replaced'
    $r = Invoke-SetTrigger @('-RunDir', $runPass, '-Role', 'host', '-Text', 'stop', '-Force')
    Assert-Equal ([IO.File]::ReadAllText($prPaths.host.PartnerTrigger)) "stop`n" '-Force replaces it'
    Remove-Item -LiteralPath $prPaths.host.PartnerTrigger
    $r = Invoke-SetTrigger @('-Role', 'guest', '-Text', 'stage=5')
    Assert-True ($r.Code -ne 0 -and !(Test-Path -LiteralPath $prPaths.guest.PartnerTrigger)) 'invalid trigger text: nothing written'
    $r = Invoke-SetTrigger @('-Role', 'both', '-Text', 'stage=3 source=net')
    Assert-True ($r.Code -eq 0 -and [IO.File]::ReadAllText($prPaths.host.PartnerTrigger) -eq "stage=3 source=net`n" -and [IO.File]::ReadAllText($prPaths.guest.PartnerTrigger) -eq "stage=3 source=net`n") '-Role both writes both instances'
    foreach ($p in $prPaths.Values) { Remove-Item -LiteralPath $p.PartnerTrigger }
    # -WaitSec: a stand-in for the native side consumes (deletes) the file and logs
    # its result to script-observer.log, as PollPartnerTrigger does.
    function Start-TriggerConsumer([string]$Role, [string]$Line) {
        $append = if ($Line) { "; [IO.File]::AppendAllText('{0}', '{1}' + [char]13 + [char]10)" -f (Join-Path $prPaths[$Role].Mod 'script-observer.log'), $Line } else { '' }
        Start-Process -FilePath $pwshExe -WindowStyle Hidden -PassThru -ArgumentList '-NoProfile', '-NonInteractive', '-Command',
        ("`$p = '{0}'; `$d = [DateTime]::UtcNow.AddSeconds(30); while (!(Test-Path -LiteralPath `$p) -and [DateTime]::UtcNow -lt `$d) {{ Start-Sleep -Milliseconds 50 }}; Start-Sleep -Milliseconds 200; Remove-Item -LiteralPath `$p{1}" -f $prPaths[$Role].PartnerTrigger, $append)
    }
    $consumer = Start-TriggerConsumer host 'partner_stage=changed; stage=1; source=fixed; apply=warp; via=trigger'
    $r = Invoke-SetTrigger @('-Role', 'host', '-Text', 'stage=1', '-WaitSec', '20')
    Assert-True ($r.Code -eq 0 -and $r.Text.Contains('applied by the native side: partner_stage=changed; stage=1')) ('-WaitSec confirms the applied trigger in script-observer.log: ' + $r.Text)
    $null = $consumer.WaitForExit(10000)
    $consumer = Start-TriggerConsumer host 'partner_trigger=rejected; reason=parse'
    $r = Invoke-SetTrigger @('-Role', 'host', '-Text', 'parts=explicit', '-WaitSec', '20')
    Assert-True ($r.Code -eq 3 -and $r.Text.Contains('REJECTED')) ('-WaitSec: a rejected trigger is not reported as consumed (exit 3): ' + $r.Text)
    $null = $consumer.WaitForExit(10000)
    $consumer = Start-TriggerConsumer host 'partner_stage=changed; stage=1; source=fixed; apply=warp; via=trigger'
    $r = Invoke-SetTrigger @('-Role', 'host', '-Text', 'stage=2', '-WaitSec', '20')
    Assert-True ($r.Code -eq 3 -and $r.Text.Contains('different command')) ('-WaitSec: another command applied (a raced replacement) exits 3: ' + $r.Text)
    $null = $consumer.WaitForExit(10000)
    $consumer = Start-TriggerConsumer host ''
    $r = Invoke-SetTrigger @('-Role', 'host', '-Text', 'mirror=on', '-WaitSec', '20')
    Assert-True ($r.Code -eq 4 -and $r.Text.Contains('unconfirmed')) ('-WaitSec: a consumed trigger without a result line exits 4: ' + $r.Text)
    $null = $consumer.WaitForExit(10000)
    $r = Invoke-SetTrigger @('-Role', 'guest', '-Text', 'stage=1', '-WaitSec', '1')
    Assert-Equal $r.Code 2 '-WaitSec exit code 2 when nothing consumed the trigger'
    Remove-Item -LiteralPath $prPaths.guest.PartnerTrigger
    # The fixture runs share the game folders: each run below rewrites their logs.
    $experiment = Join-Path $projectRoot 'tools/lab/Invoke-PartnerExperiment.ps1'
    $runPass2 = New-PartnerFixtureRun 'e0-pass-2' @{ host = $fx['e0-host']; guest = $fx['e0-guest'] }
    $out = @(& $pwshExe -NoProfile -NonInteractive -File $experiment -ConfigPath $prConfig -RunDir $runPass2 -Experiment E0 -NoInput -NoScreens -NoProcessCheck -TimeoutSec 1 2>&1)
    Assert-True ($LASTEXITCODE -eq 0 -and ($out -join ' ').Contains('MANUAL: in the host window press w+a:6000') -and ($out -join ' ').Contains('MANUAL: in the guest window press w+a:6000') -and
        @(Get-ChildItem -LiteralPath $runPass2 -Filter 'partner-E0-*.json').Count -eq 1) ('Invoke-PartnerExperiment E0 PASS on the fixture run (both instances walk): ' + ($out -join ' | '))
    # A trigger the native side rejects stops the experiment (no blind -Force, no carrying on).
    $runReject = New-PartnerFixtureRun 'e1-reject' @{ host = $fx['e1-host']; guest = $fx['e0-guest'] }
    $consumer = Start-TriggerConsumer host 'partner_trigger=rejected; reason=parse'
    $out = @(& $pwshExe -NoProfile -NonInteractive -File $experiment -ConfigPath $prConfig -RunDir $runReject -Experiment E1 -NoInput -NoScreens -NoProcessCheck -TimeoutSec 1 2>&1)
    Assert-True ($LASTEXITCODE -ne 0 -and ($out -join ' ').Contains('rejected') -and @(Get-ChildItem -LiteralPath $runReject -Filter 'partner-E1-*.json').Count -eq 0) ('Invoke-PartnerExperiment stops on a rejected trigger: ' + ($out -join ' | '))
    $null = $consumer.WaitForExit(10000)
    Assert-True (!(Test-Path -LiteralPath $prPaths.host.PartnerTrigger)) 'no trigger left behind after the rejection'
    # Set-WorldTrigger: canonical text, atomic write, both roles, -Force, refusal.
    $setWorld = Join-Path $projectRoot 'tools/lab/Set-WorldTrigger.ps1'
    function Invoke-SetWorld([string[]]$Arguments) {
        $out = @(& $pwshExe -NoProfile -NonInteractive -File $setWorld -ConfigPath $prConfig @Arguments 2>&1)
        [pscustomobject]@{ Code = $LASTEXITCODE; Text = ($out -join "`n") }
    }
    $r = Invoke-SetWorld @('-Role', 'both', '-Text', 'goto_lz=1  goto=30010')
    Assert-True ($r.Code -eq 0 -and [IO.File]::ReadAllText($prPaths.host.WorldTrigger) -eq "goto=30010 goto_lz=1`n" -and
        [IO.File]::ReadAllText($prPaths.guest.WorldTrigger) -eq "goto=30010 goto_lz=1`n") ('Set-WorldTrigger writes both in canonical form: ' + $r.Text)
    $r = Invoke-SetWorld @('-Role', 'host', '-Text', 'world=0')
    Assert-True ($r.Code -ne 0 -and [IO.File]::ReadAllText($prPaths.host.WorldTrigger) -eq "goto=30010 goto_lz=1`n") 'an unconsumed world.trigger is not replaced without -Force'
    $r = Invoke-SetWorld @('-Role', 'host', '-Text', 'world=0', '-Force')
    Assert-True ($r.Code -eq 0 -and [IO.File]::ReadAllText($prPaths.host.WorldTrigger) -eq "world=0`n") '-Force replaces it'
    foreach ($p in $prPaths.Values) { Remove-Item -LiteralPath $p.WorldTrigger }
    $r = Invoke-SetWorld @('-Role', 'guest', '-Text', 'goto=30050')
    Assert-True ($r.Code -ne 0 -and !(Test-Path -LiteralPath $prPaths.guest.WorldTrigger)) 'invalid world trigger: nothing written'
    $runShort = New-PartnerFixtureRun 'e0-short' @{ host = (Select-Lines $fx['e0-host'] 20); guest = $fx['e0-guest'] }
    $out = @(& $pwshExe -NoProfile -NonInteractive -File $experiment -ConfigPath $prConfig -RunDir $runShort -Experiment E0 -NoInput -NoScreens -NoProcessCheck -TimeoutSec 1 -PollSec 1 2>&1)
    Assert-True ($LASTEXITCODE -eq 2 -and ($out -join ' ').Contains('waiting: ')) ('Invoke-PartnerExperiment reports INCOMPLETE at the timeout: ' + ($out -join ' | '))
    $out = @(& $pwshExe -NoProfile -NonInteractive -File $experiment -ConfigPath $prConfig -RunDir $runOnline -Experiment E1 -NoInput -NoScreens 2>&1)
    Assert-True ($LASTEXITCODE -ne 0 -and ($out -join ' ').Contains('-Offline')) 'Invoke-PartnerExperiment refuses a run that is not offline'
    $out = @(& $pwshExe -NoProfile -NonInteractive -File (Join-Path $projectRoot 'tools/lab/Invoke-LabSmoke.ps1') -ConfigPath $prConfig -RunDir $runPass -NoScreens 2>&1)
    Assert-True ($LASTEXITCODE -ne 0 -and ($out -join ' ').Contains('Test-PartnerSpike')) 'the session smoke refuses offline runs'

    # ------------------------------------------------------------ manifests, backup and seeding
    # Windows PowerShell 5.1 (friend package) has no [IO.Path]::GetRelativePath.
    Assert-Equal (Get-RelativeChildPath -Root 'C:\a\b\' -Path 'C:\a\b\c\d.txt') 'c/d.txt' 'relative child path'
    Assert-Equal (Get-RelativeChildPath -Root 'c:/A/B' -Path 'C:\a\b\x') 'x' 'relative child path: separators and case'
    Assert-Throws { Get-RelativeChildPath -Root 'C:\a\b' -Path 'C:\a\bc\d.txt' } 'not below' 'a sibling with the same prefix is not a child'
    Assert-Throws { Get-RelativeChildPath -Root 'C:\a\b' -Path 'C:\a\b' } 'not below' 'the root itself is not a child'
    $userdata = Join-Path $fixture 'steam/userdata/7'
    foreach ($f in '287700/local/TPP_GAME_DATA0', '287700/local/TPP_GRAPHICS_CONFIG', '311340/remote/TPP_GAME_DATA', '287700/remotecache.vdf') {
        $path = Join-Path $userdata $f
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
        [IO.File]::WriteAllText($path, "fixture $f")
    }
    $backup = Join-Path $fixture 'build/lab/save-backups/1'
    $manifest = Backup-RealSaves -UserData $userdata -Destination $backup
    Assert-Equal @($manifest.verified).Count 3 'verified rows = 287700/local + 311340/remote'
    Assert-Equal @($manifest.all_files).Count 4 'informational backup includes the rest of 287700'
    Assert-True (Test-Path -LiteralPath (Join-Path $backup 'files/287700/remotecache.vdf')) 'backup copy written'
    Assert-Throws { Backup-RealSaves -UserData $userdata -Destination $backup } 'already exists' 'no backup overwrite'
    Assert-Throws { Backup-RealSaves -UserData $userdata -Destination (Join-Path $userdata 'backup') } 'outside Steam userdata' 'no backup inside userdata'
    Assert-True (Test-RealSavesAgainstBackup -BackupDirectory $backup).Comparison.Same 'unchanged saves'
    [IO.File]::WriteAllText((Join-Path $userdata '287700/remotecache.vdf'), 'Steam rewrites this; not verified')
    Assert-True (Test-RealSavesAgainstBackup -BackupDirectory $backup).Comparison.Same 'files outside the verified folders are ignored'
    [IO.File]::WriteAllText((Join-Path $userdata '287700/local/TPP_GAME_DATA0'), 'changed')
    [IO.File]::WriteAllText((Join-Path $userdata '311340/remote/NEW'), 'added')
    Remove-Item -LiteralPath (Join-Path $userdata '287700/local/TPP_GRAPHICS_CONFIG')
    $cmp = (Test-RealSavesAgainstBackup -BackupDirectory $backup).Comparison
    Assert-True (-not $cmp.Same) 'changes detected'
    Assert-Equal ($cmp.Changed -join ',') '287700/local/TPP_GAME_DATA0' 'changed file'
    Assert-Equal ($cmp.Missing -join ',') '287700/local/TPP_GRAPHICS_CONFIG' 'missing file'
    Assert-Equal ($cmp.Added -join ',') '311340/remote/NEW' 'added file'
    $pure = Compare-HashManifest -Expected @([pscustomobject]@{ path = 'a'; sha256 = '1' }) -Actual @([pscustomobject]@{ path = 'a'; sha256 = '1' })
    Assert-True $pure.Same 'pure compare same'
    Assert-True (-not (Compare-HashManifest -Expected @() -Actual @([pscustomobject]@{ path = 'a'; sha256 = '1' })).Same) 'pure compare added'
    $saveDir = Join-Path $fixture 'games/MGS_TPP_guest/mgs-coop/lab/saves'
    $seed = Copy-SaveSeed -BackupDirectory $backup -SaveDir $saveDir
    Assert-Equal "$($seed.Seeded)/$($seed.Files)" 'True/3' 'seeded from the backup copy'
    Assert-True (Test-Path -LiteralPath (Join-Path $saveDir '287700/local/TPP_GRAPHICS_CONFIG')) 'local -> <save_dir>/287700/local'
    Assert-True (Test-Path -LiteralPath (Join-Path $saveDir 'remote/TPP_GAME_DATA')) 'remote -> <save_dir>/remote'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $saveDir '287700/local/TPP_GAME_DATA0'))) 'fixture 287700/local/TPP_GAME_DATA0' 'seed comes from the backup, not the live folder'
    Assert-Equal (Copy-SaveSeed -BackupDirectory $backup -SaveDir $saveDir).Reason 'already_seeded' 'seeding is idempotent'
    [IO.File]::WriteAllText((Join-Path $saveDir '287700/local/LAB_PROGRESS'), 'lab play')
    $archive = Join-Path $fixture 'build/lab/save-archive/1/guest'
    $seed = Copy-SaveSeed -BackupDirectory $backup -SaveDir $saveDir -Reseed -ArchiveDirectory $archive
    Assert-True ($seed.Seeded -and !(Test-Path -LiteralPath (Join-Path $saveDir '287700/local/LAB_PROGRESS'))) 'reseed replaces lab copies'
    Assert-Equal @(Get-ChildItem -LiteralPath $archive -Recurse -File -Filter LAB_PROGRESS).Count 1 'reseed archives old lab copies'
    Assert-True (Write-TextFileIfChanged -Path (Join-Path $fixture 'w.txt') -Text "x`n") 'first write changes'
    Assert-True (-not (Write-TextFileIfChanged -Path (Join-Path $fixture 'w.txt') -Text "x`n")) 'same text is a no-op'

    # ------------------------------------------------------------ processes, fuse
    $dir = Join-Path $fixture 'games/MGS_TPP'
    $rows = Select-ProcessInDirectory -Directory $dir -Processes @(
        [pscustomobject]@{ Id = 1; Path = (Join-Path $dir 'mgsvtpp.exe') },
        [pscustomobject]@{ Id = 2; Path = (Join-Path $fixture 'games/MGS_TPP_guest/mgsvtpp.exe') },
        [pscustomobject]@{ Id = 3; Path = $null },
        [pscustomobject]@{ Id = 4; Path = (Join-Path $dir 'sub/mgsvtpp.exe') })
    Assert-Equal (@($rows | ForEach-Object { "$($_.Id):$($_.Unknown)" }) -join ',') '1:False,3:True' 'process matching by folder path'
    # Stop-Lab only terminates this run's own processes.
    $t0 = [DateTime]::UtcNow
    Assert-True (Test-RunProcessIdentity -ActualStartUtc $t0 -RecordedStartUtc $t0.AddMilliseconds(400).ToString('o')) 'recorded start time matches within 1 s'
    Assert-True (-not (Test-RunProcessIdentity -ActualStartUtc $t0 -RecordedStartUtc $t0.AddSeconds(5))) 'other start time: not this run''s process'
    Assert-True (Test-RunProcessIdentity -ActualStartUtc $t0 -NotBeforeUtc $t0.AddSeconds(-30) -NotAfterUtc $t0.AddSeconds(1)) 'older run.json: inside the launch window'
    Assert-True (-not (Test-RunProcessIdentity -ActualStartUtc $t0.AddHours(2) -NotBeforeUtc $t0.AddSeconds(-30) -NotAfterUtc $t0.AddSeconds(1))) 'older run.json: started hours later = a normal session'
    Assert-True (-not (Test-RunProcessIdentity -ActualStartUtc $null -RecordedStartUtc $t0)) 'unknown start time not verified'
    Assert-True (-not (Test-RunProcessIdentity -ActualStartUtc $t0)) 'nothing recorded: not verified'
    $recordedRows = @(
        [pscustomobject]@{ Role = 'host'; Id = 10; Alive = $true; PathMatches = $true; IdentityVerified = $true },
        [pscustomobject]@{ Role = 'guest'; Id = 11; Alive = $true; PathMatches = $true; IdentityVerified = $false },
        [pscustomobject]@{ Role = 'guest'; Id = 12; Alive = $false; PathMatches = $false; IdentityVerified = $false })
    $foundRows = @([pscustomobject]@{ Role = 'host'; Id = 10 }, [pscustomobject]@{ Role = 'guest'; Id = 11 }, [pscustomobject]@{ Role = 'host'; Id = 20 })
    $plan = @(Select-LabStopTargets -Recorded $recordedRows -Found $foundRows)
    Assert-Equal (@($plan | ForEach-Object { "$($_.Id):$($_.Action)" }) -join ',') '10:close_then_terminate,11:leave,20:leave' 'only the verified run process may be terminated'
    $plan = @(Select-LabStopTargets -Recorded $recordedRows -Found $foundRows -IncludeUnrecorded)
    Assert-Equal (@($plan | ForEach-Object { "$($_.Id):$($_.Action)" }) -join ',') '10:close_then_terminate,11:close_only,20:close_only' 'unrecorded processes: WM_CLOSE only'
    Assert-Equal @(Select-LabStopTargets).Count 0 'nothing to stop'
    $fuseArgs = New-FuseArguments -Port 6667
    Assert-Equal ($fuseArgs -join ' ') '-standalone -listenAddr 127.0.0.1:6667 -baseURL http://127.0.0.1:6667/ -writeLog' 'fuse arguments'
    Assert-Throws { Assert-FuseArgumentsSafe -Arguments @('-listenAddr', '127.0.0.1:6667') } 'standalone' 'fuse without -standalone refused'
    Assert-Throws { Assert-FuseArgumentsSafe -Arguments @('-standalone', '-listenAddr', '0.0.0.0:6667') } '127.0.0.1' 'fuse on all interfaces refused'
    Assert-Throws { Assert-FuseArgumentsSafe -Arguments @('-standalone', '-listenAddr', '127.0.0.1:6667', '-clientURL', 'x') } 'clientURL' 'fuse -clientURL refused'
    $fuseLog = Join-Path $fixture 'fuse/log/1700000000'
    $null = New-Item -ItemType Directory -Path $fuseLog -Force
    foreach ($n in '1700000000001_CMD_GET_URLLIST_request', '1700000000002_CMD_AUTH_STEAMTICKET_request', '1700000000003_CMD_GET_URLLIST_request', 'junk') {
        [IO.File]::WriteAllText((Join-Path $fuseLog $n), '{}')
    }
    $null = New-Item -ItemType Directory -Path (Join-Path $fixture 'fuse/log/1600000000') -Force
    [IO.File]::WriteAllText((Join-Path $fixture 'fuse/log/1600000000/1_CMD_OLD_request'), '{}')
    $summary = Get-FuseMessageSummary -FuseDirectory (Join-Path $fixture 'fuse') -SinceUnix 1699999999
    Assert-Equal "$($summary.CMD_GET_URLLIST)/$($summary.CMD_AUTH_STEAMTICKET)" '2/1' 'fuse message counts'
    Assert-True (-not ($summary.PSObject.Properties.Name -contains 'CMD_OLD')) 'older fuse runs excluded'
    $trigger = Join-Path $fixture 'join.trigger'
    New-TriggerFile -Path $trigger
    Assert-True ((Test-Path -LiteralPath $trigger) -and @(Get-ChildItem -LiteralPath $fixture -Filter 'join.trigger.*.tmp').Count -eq 0) 'trigger created atomically'

    # ------------------------------------------------------------ owned install into a host and a guest copy
    $games = Join-Path $fixture 'install'
    $hostGame = Join-Path $games 'MGS_TPP'
    $null = New-Item -ItemType Directory -Path (Join-Path $hostGame 'mgs-coop') -Force
    [IO.File]::WriteAllText((Join-Path $hostGame 'mgsvtpp.exe'), 'INERT executable fixture; never executed')
    [IO.File]::WriteAllText((Join-Path $hostGame 'steam_api64.dll'), 'INERT Steam DLL fixture')
    $archiveSource = Join-Path $fixture 'runtime-archive.dat'
    [IO.File]::WriteAllText($archiveSource, 'INERT runtime archive fixture')
    $loader1 = Join-Path $fixture 'loader1.dll'; [IO.File]::WriteAllText($loader1, 'INERT loader 1')
    $loader2 = Join-Path $fixture 'loader2.dll'; [IO.File]::WriteAllText($loader2, 'INERT loader 2')
    $exeHash = Get-Sha256 (Join-Path $hostGame 'mgsvtpp.exe')
    $steamHash = Get-Sha256 (Join-Path $hostGame 'steam_api64.dll')
    $archiveHash = Get-Sha256 $archiveSource
    $installer = Join-Path $projectRoot 'tools/package-install.ps1'
    $stage1 = Join-Path $fixture 'stage1'
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $stage1 -LoaderPath $loader1 -Version '0.11.1' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 $archiveHash
    $package = Get-Content -LiteralPath (Join-Path $stage1 'package.json') -Raw | ConvertFrom-Json
    Assert-Equal @($package.files).Count 6 'package has loader and licenses with no runtime archive payload'
    Assert-Equal ($package.files | Where-Object target -eq 'mgs-coop/ImGui-LICENSE.txt').sha256 (Get-Sha256 (Join-Path $projectRoot 'third_party/imgui/LICENSE.txt')) 'lab package preserves overlay MIT license'
    Assert-True (-not (@($package.files.target) -contains 'mgs-coop/mgs-coop-player-runtime.dat')) 'archive referenced by hash only'
    Assert-Equal $package.runtime_archive.sha256 $archiveHash 'archive hash recorded'
    Assert-InFixture $hostGame
    Assert-Throws { & $installer -Action Install -GameDirectory $hostGame -PackageDirectory $stage1 -NoPrompt } 'does not contain game-derived files' 'archive required on a fresh install'
    # A non-console caller (redirected input) is never prompted; it gets the explanation.
    $childOut = ('' | & (Get-Process -Id $PID).Path -NoProfile -File $installer -Action Install -GameDirectory $hostGame -PackageDirectory $stage1 2>&1 | Out-String)
    $childExit = $LASTEXITCODE
    $childOut = $childOut -replace '\s*\r?\n\s*\|?\s*', ' '  # undo the error view's line wrapping
    Assert-True ($childExit -ne 0 -and $childOut.Contains('install.bat asks for it') -and !$childOut.Contains('No runtime archive path')) 'redirected input: no prompt, clear error'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $hostGame 'dinput8.dll'))) 'nothing installed without the archive'
    $null = & $installer -Action Install -GameDirectory $hostGame -PackageDirectory $stage1 -RuntimeArchive $archiveSource -NoPrompt
    $hostRecord = Get-Content -LiteralPath (Join-Path $hostGame 'mgs-coop/probe-install.json') -Raw | ConvertFrom-Json
    Assert-Equal $hostRecord.game_directory $hostGame 'host record names the host folder'
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'dinput8.dll')) (Get-Sha256 $loader1) 'host loader installed'
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'mgs-coop/mgs-coop-player-runtime.dat')) $archiveHash 'host archive installed'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $hostGame 'mgs-coop/script-observe.enabled'))) "client-check-v1`n" 'first install defaults to diagnostics'
    # Upgrade reuses the installed archive.
    $stage2 = Join-Path $fixture 'stage2'
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $stage2 -LoaderPath $loader2 -Version '0.11.1' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 $archiveHash
    # A pre-overlay package and its seven-row rollback remain usable. Upgrading
    # it adds the MIT license; restoring removes only that newly owned file.
    $legacyStage = Join-Path $fixture 'legacy-stage'
    $null = New-LabPackage -RepoRoot $projectRoot -Destination $legacyStage -LoaderPath $loader1 -Version '0.11.1' -ExecutableSha256 $exeHash -SteamDllSha256 $steamHash -ArchiveSha256 $archiveHash
    $legacyManifest = Get-Content -LiteralPath (Join-Path $legacyStage 'package.json') -Raw | ConvertFrom-Json
    $legacyManifest.files = @($legacyManifest.files | Where-Object { $_.target -cne 'mgs-coop/ImGui-LICENSE.txt' })
    [IO.File]::WriteAllText((Join-Path $legacyStage 'package.json'), ($legacyManifest | ConvertTo-Json -Depth 5))
    $legacyGame = Join-Path $games 'legacy-game'
    Assert-InFixture $legacyGame
    $null = New-Item -ItemType Directory -Path $legacyGame
    foreach ($name in 'mgsvtpp.exe', 'steam_api64.dll') { Copy-Item -LiteralPath (Join-Path $hostGame $name) -Destination $legacyGame }
    $null = & $installer -Action Install -GameDirectory $legacyGame -PackageDirectory $legacyStage -RuntimeArchive $archiveSource -NoPrompt
    $legacyRecord = Get-Content -LiteralPath (Join-Path $legacyGame 'mgs-coop/probe-install.json') -Raw | ConvertFrom-Json
    $legacyRollback = Get-Content -LiteralPath (Join-Path $legacyGame ("mgs-coop/install-backups/$($legacyRecord.rollback_id)/rollback.json")) -Raw | ConvertFrom-Json
    Assert-Equal @($legacyRollback.files).Count 7 'legacy rollback inventory remains supported'
    $null = & $installer -Action Install -GameDirectory $legacyGame -PackageDirectory $stage2 -NoPrompt
    Assert-True (Test-Path -LiteralPath (Join-Path $legacyGame 'mgs-coop/ImGui-LICENSE.txt')) 'upgrade installs the new overlay license'
    $null = & $installer -Action Restore -GameDirectory $legacyGame
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $legacyGame 'mgs-coop/ImGui-LICENSE.txt'))) 'upgrade rollback removes only its added license'
    Assert-Equal (Get-Sha256 (Join-Path $legacyGame 'dinput8.dll')) (Get-Sha256 $loader1) 'upgrade rollback returns legacy loader'
    $null = & $installer -Action Restore -GameDirectory $legacyGame
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $legacyGame 'dinput8.dll'))) 'legacy seven-row rollback still restores the original installation'
    $null = & $installer -Action Install -GameDirectory $hostGame -PackageDirectory $stage2 -NoPrompt
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'dinput8.dll')) (Get-Sha256 $loader2) 'upgrade without -RuntimeArchive reuses the installed archive'
    # The guest folder starts as a copy of the host folder (record names the host).
    $guestGame = Join-Path $games 'MGS_TPP_guest'
    Assert-InFixture $guestGame
    $null = New-Item -ItemType Directory -Path $guestGame -Force
    Copy-Item -Path (Join-Path $hostGame '*') -Destination $guestGame -Recurse
    Assert-Throws { & $installer -Action Install -GameDirectory $guestGame -PackageDirectory $stage1 -NoPrompt } 'AcceptRecordCopiedFrom' 'copied record refused by default'
    Assert-Throws { & $installer -Action Install -GameDirectory $guestGame -PackageDirectory $stage1 -AcceptRecordCopiedFrom $guestGame -NoPrompt } 'ownership' 'copied-from must be the recorded folder'
    $null = & $installer -Action Install -GameDirectory $guestGame -PackageDirectory $stage1 -AcceptRecordCopiedFrom $hostGame -NoPrompt
    $guestRecord = Get-Content -LiteralPath (Join-Path $guestGame 'mgs-coop/probe-install.json') -Raw | ConvertFrom-Json
    Assert-Equal $guestRecord.game_directory $guestGame 'guest record names the guest folder'
    Assert-Equal (Get-Sha256 (Join-Path $guestGame 'dinput8.dll')) (Get-Sha256 $loader1) 'guest loader installed'
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'dinput8.dll')) (Get-Sha256 $loader2) 'host untouched by the guest install'
    $rollback = Get-Content -LiteralPath (Join-Path $guestGame ("mgs-coop/install-backups/$($guestRecord.rollback_id)/rollback.json")) -Raw | ConvertFrom-Json
    Assert-Equal @($rollback.files).Count 8 'guest rollback inventory complete'
    $null = & $installer -Action Restore -GameDirectory $guestGame
    Assert-Equal (Get-Sha256 (Join-Path $guestGame 'dinput8.dll')) (Get-Sha256 $loader2) 'guest restore returns the copied loader'
    Assert-Equal (Get-Content -LiteralPath (Join-Path $guestGame 'mgs-coop/probe-install.json') -Raw | ConvertFrom-Json).game_directory $hostGame 'guest restore returns the copied record'
    $null = & $installer -Action Restore -GameDirectory $hostGame
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'dinput8.dll')) (Get-Sha256 $loader1) 'host restore returns loader 1'
    Assert-Equal (Get-Sha256 (Join-Path $hostGame 'mgsvtpp.exe')) $exeHash 'executable fixture unchanged'
    # set-session-mode validates the host id before touching the folder; account 0 is refused.
    $sessionMode = Join-Path $projectRoot 'tools/set-session-mode.ps1'
    Assert-Throws { & $sessionMode -Role Join -HostSteamId '76561197960265728' -GameDirectory $hostGame } 'Supply the host player' 'set-session-mode: account 0 refused'
    Assert-Throws { & $sessionMode -Role Join -HostSteamId '7656119796026572' -GameDirectory $hostGame } 'Supply the host player' 'set-session-mode: 16 digits refused'
    Assert-Throws { & $sessionMode -Role Join -HostSteamId '76561197960265729' -GameDirectory (Join-Path $fixture 'no-such-folder') } 'Cannot find path' 'set-session-mode: a valid id passes the identity check'
    Assert-Throws { & $sessionMode -Role Host -HostSteamId '76561197960265729' -GameDirectory $hostGame } 'only with -Role Join' 'set-session-mode: id only with Join'

    # ------------------------------------------------------------ end to end on a simulated run (no game)
    # Two idle pwsh processes stand in for the game instances; a small simulator
    # plays the native side (consumes join.trigger, appends the protocol lines).
    function Get-PrivateLeaks([string]$Dir, [string[]]$Secrets) {
        # Files in a run folder that contain any of $Secrets (raw or JSON-escaped).
        $forms = @($Secrets | Where-Object { $_ } | ForEach-Object { $_; $_.Replace('\', '\\'); $_.Replace('\', '/') } | Select-Object -Unique)
        @(Get-ChildItem -LiteralPath $Dir -File -Recurse | Where-Object { $_.Extension -ne '.png' } | Where-Object {
                $text = [IO.File]::ReadAllText($_.FullName)
                @($forms | Where-Object { $text.Contains($_) }).Count -gt 0
            } | ForEach-Object { $_.Name })
    }
    $pwshPath = (Get-Process -Id $PID).Path
    $e2e = Join-Path $fixture 'e2e'
    $e2eHost = Join-Path $e2e 'MGS_TPP'
    $e2eGuest = Join-Path $e2e 'MGS_TPP_guest'
    foreach ($d in $e2eHost, $e2eGuest) { $null = New-Item -ItemType Directory -Path (Join-Path $d 'mgs-coop') -Force }
    $e2eHostPaths = Get-InstancePaths -GameDirectory $e2eHost
    $e2eGuestPaths = Get-InstancePaths -GameDirectory $e2eGuest
    $e2eJson = Get-Content -LiteralPath (Join-Path $projectRoot 'tools/lab/lab.json') -Raw | ConvertFrom-Json
    $e2eJson.paths.hostDir = $e2eHost
    $e2eJson.paths.guestDir = $e2eGuest
    $e2eJson.paths.runDir = (Join-Path $e2e 'lab')
    $e2eJson.paths.fuseSource = (Join-Path $e2e 'fuse')
    $e2eJson.paths.fuseExe = (Join-Path $e2e 'fuse/fuse.exe')
    $e2eConfigPath = Join-Path $e2e 'lab.json'
    [IO.File]::WriteAllText($e2eConfigPath, ($e2eJson | ConvertTo-Json -Depth 8))
    $hostLog = Join-Path $e2eHost 'mgs-coop/script-observer.log'
    $guestLog = Join-Path $e2eGuest 'mgs-coop/script-observer.log'
    [IO.File]::WriteAllText($hostLog, "previous run line`r`nsession_status=session_error`r`n")  # before the offset: ignored
    [IO.File]::WriteAllText($guestLog, "dynamite_client=refused; reason=previous_run`r`n")
    $offsets = @{ host = (Get-Item -LiteralPath $hostLog).Length; guest = (Get-Item -LiteralPath $guestLog).Length }
    [IO.File]::AppendAllText($hostLog, $hostReady)
    [IO.File]::AppendAllText($guestLog, $guestArmed)
    # This run's safety evidence, probe line and loopback lines (whole files belong to the run).
    $guestEarlyPath = Join-Path $e2eGuestPaths.Mod 'early.log'
    [IO.File]::WriteAllText((Join-Path $e2eHostPaths.Mod 'early.log'), $hostEarly.Replace($hostSave, $e2eHostPaths.SaveDir))
    [IO.File]::WriteAllText($guestEarlyPath, $guestEarly.Replace($guestSave, $e2eGuestPaths.SaveDir))
    foreach ($m in $e2eHostPaths.Mod, $e2eGuestPaths.Mod) { [IO.File]::WriteAllText((Join-Path $m 'steam-observer.log'), $steamOk) }
    [IO.File]::WriteAllText((Join-Path $e2eHostPaths.Mod 'probe.log'), $probeOk)
    [IO.File]::WriteAllText((Join-Path $e2eHostPaths.Mod 'loopback.log'), "loopback=listening; port=47810; peer_port=47811; self=$realHost`r`n")
    [IO.File]::WriteAllText((Join-Path $e2eGuestPaths.Mod 'loopback.log'), "loopback=listening; port=47811; peer_port=47810; self=$labId`r`nloopback=hello; peer=$realHost`r`n")
    [IO.File]::WriteAllText($e2eGuestPaths.ClientHost, "$realHost`n")
    [IO.File]::WriteAllText($e2eGuestPaths.LabIni, "peer_steamid=$realHost`n")
    # fuse: two logins in its -writeLog files and console log (kept outside the run folder).
    $e2eFuse = Join-Path $e2e 'lab/fuse'
    $since = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() - 5
    $fuseLogDir = Join-Path $e2eFuse ('log/' + ($since + 1))
    $null = New-Item -ItemType Directory -Path $fuseLogDir -Force
    foreach ($n in '1_CMD_GET_URLLIST_request', '2_CMD_AUTH_STEAMTICKET_request', '3_CMD_GET_URLLIST_request', '4_CMD_AUTH_STEAMTICKET_request') {
        [IO.File]::WriteAllText((Join-Path $fuseLogDir $n), "{`"ticket`":`"secret`"}")
    }
    $e2eConsole = Join-Path $e2eFuse 'console/e2e-stdout.log'
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $e2eConsole) -Force
    [IO.File]::WriteAllText($e2eConsole, $console + "time=7 level=INFO msg=CMD_CREATE_PLAYER player_name=`"Big Boss`"`n")
    $dummies = @()
    try {
        foreach ($i in 1, 2) {
            $dummies += Start-Process -FilePath $pwshPath -ArgumentList '-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 300' -WindowStyle Hidden -PassThru
        }
        $runStartedUtc = [DateTime]::UtcNow.AddSeconds(-30).ToString('o')
        [IO.File]::WriteAllText($e2eHostPaths.HostId, "$realHost`n")  # written by this run's host
        function New-FixtureRun([string]$Name) {
            $dir = Join-Path $e2e "lab/runs/$Name"
            $null = New-Item -ItemType Directory -Path $dir -Force
            $privateState = Join-Path $e2e "lab/private/run-$Name.json"
            Write-JsonFile -Path $privateState -Value ([ordered]@{ source_root = $userdata })
            Write-JsonFile -Path (Join-Path $dir 'real-saves-before.json') -Value ([ordered]@{ source_root = 'Steam/userdata/<account>'; files = (Get-RealSaveManifest -UserData $userdata) })
            $instances = [ordered]@{}
            foreach ($row in @(@('host', $e2eHostPaths, $dummies[0], $offsets.host, ''), @('guest', $e2eGuestPaths, $dummies[1], $offsets.guest, $labId))) {
                $instances[$row[0]] = [ordered]@{
                    role = $row[0]; dir = $row[1].Game; exe = $pwshPath; pid = $row[2].Id
                    process_start_utc = $row[2].StartTime.ToUniversalTime().ToString('o'); started_utc = [DateTime]::UtcNow.ToString('o')
                    log_offsets = [ordered]@{ 'script-observer.log' = $row[3] }
                    expected = [ordered]@{ save_dir = $row[1].SaveDir; master_url = $fuseUrl; steamid = $row[4]; mutex_suffix = $(if ($row[0] -eq 'guest') { '-guest' } else { '' }) }
                }
            }
            $run = [ordered]@{
                product = 'mgs-coop-lab-run'; schema = 2; stamp = $Name; run_dir = $dir; started_utc = $runStartedUtc; host_only = $false
                fuse = [ordered]@{ pid = 0; cwd = $e2eFuse; since_unix = $since; stdout = $e2eConsole; url = $fuseUrl }
                real_saves_before = 'real-saves-before.json'; private_state = $privateState; latest_save_backup = $backup
                instances = $instances
            }
            Write-JsonFile -Path (Join-Path $dir 'run.json') -Value $run
            return $dir
        }
        $simulator = Join-Path $e2e 'simulate-native.ps1'
        [IO.File]::WriteAllText($simulator, @'
param([string]$Trigger, [string]$HostLog, [string]$GuestLog)
$deadline = [DateTime]::UtcNow.AddSeconds(60)
while (!(Test-Path -LiteralPath $Trigger)) { if ([DateTime]::UtcNow -gt $deadline) { exit 3 }; Start-Sleep -Milliseconds 100 }
Remove-Item -LiteralPath $Trigger
[IO.File]::AppendAllText($GuestLog, "client_join=input_received; key=trigger_file`r`ndynamite_client=returned; request_started=1`r`nsession_status=client_requested`r`n")
Start-Sleep -Milliseconds 500
[IO.File]::AppendAllText($HostLog, "session_status=host_peer_present`r`n")
[IO.File]::AppendAllText($GuestLog, "session_status=client_established`r`n")
'@)
        $runDir = New-FixtureRun 'pass'
        $trigger = Join-Path $e2eGuest 'mgs-coop/join.trigger'
        $sim = Start-Process -FilePath $pwshPath -ArgumentList '-NoProfile', '-NonInteractive', '-File', $simulator, $trigger, $hostLog, $guestLog -WindowStyle Hidden -PassThru
        $smoke = Join-Path $projectRoot 'tools/lab/Invoke-LabSmoke.ps1'
        $output = & $pwshPath -NoProfile -NonInteractive -File $smoke -ConfigPath $e2eConfigPath -RunDir $runDir -NoScreens -PollMs 200 -TimeoutSec 20 -ReadyTimeoutSec 30 2>&1
        $exitCode = $LASTEXITCODE
        $null = $sim.WaitForExit(10000)
        Assert-Equal $exitCode 0 ("smoke passes on the simulated run: " + ($output -join ' | '))
        $result = Get-Content -LiteralPath (Join-Path $runDir 'result.json') -Raw | ConvertFrom-Json
        Assert-Equal "$($result.verdict)/$($result.reason)" 'pass/established_stable' 'result.json verdict'
        Assert-True $result.trigger.created 'trigger recorded'
        Assert-True ($null -ne $result.timings.trigger_sec -and $null -ne $result.timings.established_sec) 'timings recorded'
        Assert-True (-not (Test-Path -LiteralPath $trigger)) 'trigger consumed by the simulated native side'
        Assert-Equal $result.evidence.host.observer.'host_admission=authorized' 'host_admission=authorized; owned_host=1 same_thread=1 lua_interface=SessionEnableAccept' 'evidence line'
        Assert-Equal "$($result.evidence.host.safety.state)/$($result.evidence.guest.safety.state)" 'ok/ok' 'safety evidence recorded'
        Assert-Equal $result.host_id_check 'ok' 'host id cross-check'
        Assert-Equal "$($result.fuse_counts.CMD_GET_URLLIST)/$($result.fuse_counts.CMD_AUTH_STEAMTICKET)" '2/2' 'fuse traffic counted'
        Assert-Equal "$($result.fuse_identities.distinct_auth_ids)/$($result.fuse_identities.lab_guest_id_seen)" '2/True' 'fuse identities summarised without ids'
        $excerpt = [IO.File]::ReadAllText((Join-Path $runDir 'host-script-observer.log'))
        Assert-True (-not $excerpt.Contains('previous run line') -and $excerpt.Contains('host_peer_present')) 'excerpt covers only this run'
        Assert-True (@($result.files) -contains 'guest-script-observer.log') 'guest excerpt copied'
        Assert-True ([IO.File]::ReadAllText((Join-Path $runDir 'host-loopback.log')).Contains('self=<steamid>')) 'loopback excerpt redacted'
        $redactedConsole = [IO.File]::ReadAllText((Join-Path $runDir 'fuse-stdout.redacted.log'))
        Assert-True ($redactedConsole.Contains('steamID=<steamid>') -and $redactedConsole.Contains($labId) -and $redactedConsole.Contains('player_name=<redacted>')) 'fuse console copied redacted'
        Assert-True ([IO.File]::ReadAllText($e2eConsole).Contains($realHost)) 'raw fuse console (outside the run folder) untouched'

        # A refusal after the run's offsets fails the smoke.
        [IO.File]::AppendAllText($guestLog, "dynamite_client=refused; reason=not_ready`r`n")
        $runDir2 = New-FixtureRun 'fail'
        $null = & $pwshPath -NoProfile -NonInteractive -File $smoke -ConfigPath $e2eConfigPath -RunDir $runDir2 -NoScreens -PollMs 200 -TimeoutSec 5 -ReadyTimeoutSec 5 2>&1
        Assert-Equal $LASTEXITCODE 1 'smoke exit code on failure'
        $result = Get-Content -LiteralPath (Join-Path $runDir2 'result.json') -Raw | ConvertFrom-Json
        Assert-Equal "$($result.verdict)/$($result.reason)" 'fail/dynamite_client_refused' 'failure verdict'
        # The host's own id differs from what the guest targets.
        [IO.File]::WriteAllText($e2eHostPaths.HostId, "76561197960265730`n")
        $runDir3 = New-FixtureRun 'mismatch'
        $null = & $pwshPath -NoProfile -NonInteractive -File $smoke -ConfigPath $e2eConfigPath -RunDir $runDir3 -NoScreens -PollMs 200 -TimeoutSec 5 -ReadyTimeoutSec 5 2>&1
        $result = Get-Content -LiteralPath (Join-Path $runDir3 'result.json') -Raw | ConvertFrom-Json
        Assert-Equal "$($result.verdict)/$($result.reason)/$($result.host_id_check)" 'fail/host_id_mismatch/mismatch' 'host id mismatch fails the smoke'
        Assert-True (-not ([IO.File]::ReadAllText((Join-Path $runDir3 'result.json')).Contains('76561197960265730'))) 'mismatching id not written to result.json'
        [IO.File]::WriteAllText($e2eHostPaths.HostId, "$realHost`n")
        # A rule shown NOT in force mid-run fails the smoke at once.
        [IO.File]::AppendAllText($guestEarlyPath, "saves=unchanged`r`n")
        $runDir4 = New-FixtureRun 'unsafe'
        $null = & $pwshPath -NoProfile -NonInteractive -File $smoke -ConfigPath $e2eConfigPath -RunDir $runDir4 -NoScreens -PollMs 200 -TimeoutSec 5 -ReadyTimeoutSec 5 2>&1
        $result = Get-Content -LiteralPath (Join-Path $runDir4 'result.json') -Raw | ConvertFrom-Json
        Assert-Equal "$($result.verdict)/$($result.reason)" 'fail/unsafe_guest_saves_unchanged' 'unsafe guest fails the smoke'

        # Stop-Lab closes/terminates only the run's processes and checks the (fixture) real saves.
        $stopScript = Join-Path $projectRoot 'tools/lab/Stop-Lab.ps1'
        foreach ($p in $e2eHostPaths, $e2eGuestPaths) { New-TriggerFile -Path $p.PartnerTrigger -Text 'stage=3'; New-TriggerFile -Path $p.WorldTrigger -Text 'goto=30010' }
        $null = & $pwshPath -NoProfile -NonInteractive -File $stopScript -ConfigPath $e2eConfigPath -RunDir $runDir -TimeoutSec 3 2>&1
        Assert-Equal $LASTEXITCODE 0 'Stop-Lab exit code with unchanged saves'
        foreach ($d in $dummies) { Assert-True $d.WaitForExit(5000) "stand-in process $($d.Id) stopped" }
        $stop = Get-Content -LiteralPath (Join-Path $runDir 'stop.json') -Raw | ConvertFrom-Json
        Assert-True $stop.real_saves.unchanged_since_run_start 'saves unchanged since run start (source root from build/lab/private)'
        Assert-True ($stop.games.host.action -eq 'close_then_terminate' -and $stop.games.host.exit -in @('closed', 'terminated')) 'recorded process verified, closed or terminated'
        Assert-True (!(Test-Path -LiteralPath $e2eHostPaths.PartnerTrigger) -and !(Test-Path -LiteralPath $e2eGuestPaths.PartnerTrigger)) 'Stop-Lab deletes leftover partner.trigger files'
        Assert-Equal (@($stop.partner_triggers_removed) -join ',') 'host,guest' 'stop.json lists the removed partner triggers'
        Assert-True (!(Test-Path -LiteralPath $e2eHostPaths.WorldTrigger) -and !(Test-Path -LiteralPath $e2eGuestPaths.WorldTrigger)) 'Stop-Lab deletes leftover world.trigger files'
        Assert-Equal (@($stop.world_triggers_removed) -join ',') 'host,guest' 'stop.json lists the removed world triggers'
        Assert-Equal ([Int64]$stop.log_end_offsets.host.'early.log') (Get-FileLengthOrZero (Join-Path $e2eHostPaths.Mod 'early.log')) 'stop.json records the log end offsets per instance'
        Assert-True ($null -ne $stop.log_end_offsets.guest.'script-observer.log') 'stop.json records the guest log end offsets'
        foreach ($dir in $runDir, $runDir2, $runDir3, $runDir4) {
            $leaks = Get-PrivateLeaks -Dir $dir -Secrets @($realHost, 'userdata', '"secret"')
            Assert-Equal ($leaks -join ',') '' "run folder $(Split-Path -Leaf $dir) holds no SteamID, account folder or ticket"
        }
        [IO.File]::WriteAllText((Join-Path $userdata '311340/remote/TPP_GAME_DATA'), 'modified during the run')
        $null = & $pwshPath -NoProfile -NonInteractive -File $stopScript -ConfigPath $e2eConfigPath -RunDir $runDir -TimeoutSec 1 2>&1
        Assert-Equal $LASTEXITCODE 2 'Stop-Lab reports a changed real save'
    } finally {
        foreach ($d in $dummies) { if (!$d.HasExited) { Stop-Process -Id $d.Id -Force -ErrorAction SilentlyContinue } }
    }

    # ------------------------------------------------------------ full harness dry run with stand-in processes
    # Prepare -> Start -> Smoke -> Screens -> Stop -> (unrecorded process left alone)
    # -> Start refusals at the safety gate -> Disable against fixture folders.
    # lab_fake_process.exe is copied as the fixture's mgsvtpp.exe and fuse.exe; it
    # only runs inside a folder with mgs-coop\lab.ini and plays the native side.
    if ($FakeProcess) {
        $dry = Join-Path $fixture 'dry'
        $dryHost = Join-Path $dry 'MGS_TPP'
        $dryGuest = Join-Path $dry 'MGS_TPP_guest'
        $drySteam = Join-Path $dry 'Steam'
        $dryHostId = ConvertTo-SteamId64 -AccountId 12345
        $dryUser = Join-Path $drySteam 'userdata/12345'
        Assert-InFixture $dry
        foreach ($f in '287700/local/TPP_GAME_DATA0', '311340/remote/TPP_GAME_DATA') {
            $path = Join-Path $dryUser $f
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
            [IO.File]::WriteAllText($path, "dry $f")
        }
        $realGraphics = Join-Path $dryUser '287700/local/TPP_GRAPHICS_CONFIG'
        [IO.File]::WriteAllText($realGraphics, $realConfig)
        $realGraphicsHash = Get-Sha256 $realGraphics
        foreach ($d in $dryHost, $dryGuest) {
            $null = New-Item -ItemType Directory -Path $d -Force
            Copy-Item -LiteralPath $FakeProcess -Destination (Join-Path $d 'mgsvtpp.exe')
            [IO.File]::WriteAllText((Join-Path $d 'steam_api64.dll'), 'INERT Steam DLL fixture')
        }
        # The user's own coop.ini in the host folder: Prepare sets it aside, -Disable restores it.
        $userCoop = "character=snake`n"
        $null = New-Item -ItemType Directory -Path (Join-Path $dryHost 'mgs-coop') -Force
        [IO.File]::WriteAllText((Join-Path $dryHost 'mgs-coop/coop.ini'), $userCoop)
        $null = New-Item -ItemType Directory -Path (Join-Path $dry 'fuse') -Force
        Copy-Item -LiteralPath $FakeProcess -Destination (Join-Path $dry 'fuse/fuse.exe')
        $dryArchive = Join-Path $dry 'runtime-archive.dat'; [IO.File]::WriteAllText($dryArchive, 'INERT dry archive')
        $dryLoader = Join-Path $dry 'built-dinput8.dll'; [IO.File]::WriteAllText($dryLoader, 'INERT dry loader')
        $base = 0
        for ($attempt = 0; $attempt -lt 20 -and !$base; $attempt++) {
            $candidate = Get-Random -Minimum 20000 -Maximum 40000
            if ((Test-LocalPortFree -Port $candidate -Protocol Tcp) -and (Test-LocalPortFree -Port ($candidate + 1) -Protocol Udp) -and
                (Test-LocalPortFree -Port ($candidate + 2) -Protocol Udp) -and (Test-LocalPortFree -Port ($candidate + 3) -Protocol Udp) -and
                (Test-LocalPortFree -Port ($candidate + 4) -Protocol Udp) -and (Test-LocalPortFree -Port ($candidate + 5) -Protocol Tcp) -and
                (Test-LocalPortFree -Port ($candidate + 3) -Protocol Udp -AnyAddress) -and (Test-LocalPortFree -Port ($candidate + 4) -Protocol Udp -AnyAddress)) { $base = $candidate }
        }
        Assert-True ($base -ne 0) 'free fixture ports'
        $dryJson = Get-Content -LiteralPath (Join-Path $projectRoot 'tools/lab/lab.json') -Raw | ConvertFrom-Json
        $dryJson.paths.hostDir = $dryHost; $dryJson.paths.guestDir = $dryGuest
        $dryJson.paths.fuseSource = (Join-Path $dry 'fuse'); $dryJson.paths.fuseExe = (Join-Path $dry 'fuse/fuse.exe')
        $dryJson.paths.runDir = (Join-Path $dry 'lab'); $dryJson.paths.loader = $dryLoader; $dryJson.paths.runtimeArchive = $dryArchive
        $dryJson.paths | Add-Member -NotePropertyName steamRoot -NotePropertyValue $drySteam
        $dryJson.hashes.exe = Get-Sha256 (Join-Path $dryHost 'mgsvtpp.exe')
        $dryJson.hashes.steamApi = Get-Sha256 (Join-Path $dryHost 'steam_api64.dll')
        $dryJson.hashes.runtimeArchive = Get-Sha256 $dryArchive
        $dryJson.ports.fuse = $base; $dryJson.ports.host = $base + 1; $dryJson.ports.guest = $base + 2
        $dryJson.ports.netHost = $base + 3; $dryJson.ports.netGuest = $base + 4; $dryJson.offline.masterPort = $base + 5
        $dryJson.identity.hostSteamId = $dryHostId
        $dryJson.window.host.x = -6000; $dryJson.window.host.y = -6000
        $dryJson.window.guest.x = -5600; $dryJson.window.guest.y = -6000
        $dryJson.smoke.windowTimeoutSec = 30
        $dryJson.smoke.safetyTimeoutSec = 30
        $dryConfig = Join-Path $dry 'lab.json'
        [IO.File]::WriteAllText($dryConfig, ($dryJson | ConvertTo-Json -Depth 8))
        $labDir = Join-Path $projectRoot 'tools/lab'
        $dryUrl = "http://127.0.0.1:$base/tppstm/gate"
        function Invoke-DryStart([string[]]$Extra = @()) {
            $text = (& $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Start-Lab.ps1') -ConfigPath $dryConfig @Extra 2>&1 | Out-String)
            $code = $LASTEXITCODE
            $runPath = (Get-Content -LiteralPath (Join-Path $dry 'lab/current-run.json') -Raw | ConvertFrom-Json).run_dir
            [pscustomobject]@{ Code = $code; Text = $text; Run = (Get-Content -LiteralPath (Join-Path $runPath 'run.json') -Raw | ConvertFrom-Json); Dir = $runPath }
        }
        function Test-DryProcessesGone {
            @(Get-Process -Name mgsvtpp, fuse -ErrorAction SilentlyContinue | Where-Object { $p = Get-ProcessPathSafe $_; $p -and (Test-PathInside -Child $p -Parent $dry) }).Count -eq 0
        }
        $env:MGS_COOP_FAKE_HOST_STEAMID = $dryHostId
        $env:MGS_COOP_FAKE_UNSAFE = ''
        try {
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Prepare succeeds: ' + ($out -join ' | '))
            $hp = Get-InstancePaths -GameDirectory $dryHost
            $gp = Get-InstancePaths -GameDirectory $dryGuest
            Assert-Equal (Get-Content -LiteralPath $gp.Record -Raw | ConvertFrom-Json).game_directory $dryGuest 'guest ownership record names the guest folder'
            Assert-Equal (Get-Sha256 $gp.Loader) (Get-Sha256 $dryLoader) 'guest loader installed'
            Assert-Equal ([IO.File]::ReadAllText($hp.Marker)) "host-v3`n" 'host marker'
            Assert-Equal ([IO.File]::ReadAllText($gp.Marker)) "client-v1`n" 'guest marker'
            Assert-Equal ([IO.File]::ReadAllText($gp.ClientHost)) "$dryHostId`n" 'guest client-host.txt = host SteamID64'
            Assert-Equal (ConvertFrom-IniText ([IO.File]::ReadAllText($gp.LabIni)))['peer_steamid'] $dryHostId 'guest lab.ini peer'
            Assert-Equal (ConvertFrom-IniText ([IO.File]::ReadAllText($hp.CoopIni)))['master_url'] $dryUrl 'coop.ini fuse url'
            $userBackups = @(Get-ChildItem -LiteralPath $hp.Mod -File -Filter 'coop.ini.user-*')
            Assert-True ($userBackups.Count -eq 1 -and [IO.File]::ReadAllText($userBackups[0].FullName) -ceq $userCoop) 'user coop.ini set aside'
            $labGraphics = Join-Path $gp.SaveDir '287700/local/TPP_GRAPHICS_CONFIG'
            $g = Get-GraphicsConfigInfo -Text ([IO.File]::ReadAllText($labGraphics))
            Assert-True ($g.Valid -and $g.WindowMode -eq 'Windowed' -and $g.Width -eq 1280) 'lab graphics copy set to Windowed 1280x720 with a valid tag'
            Assert-Equal (Get-Sha256 $realGraphics) $realGraphicsHash 'the (fixture) real graphics config is untouched'
            Assert-True (Test-Path -LiteralPath (Join-Path $gp.SaveDir 'remote/TPP_GAME_DATA')) 'guest remote save copy seeded'
            $backups = @(Get-ChildItem -LiteralPath (Join-Path $hp.Mod 'install-backups') -Directory).Count
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig 2>&1
            Assert-Equal $LASTEXITCODE 0 'second Prepare succeeds'
            Assert-True (($out -join "`n").Contains('[host] loader already installed')) 'second Prepare is idempotent (loader)'
            Assert-Equal @(Get-ChildItem -LiteralPath (Join-Path $hp.Mod 'install-backups') -Directory).Count $backups 'no new install backup on a repeat'
            Assert-Equal @(Get-ChildItem -LiteralPath $hp.Mod -File -Filter 'coop.ini.user-*').Count 1 'the lab coop.ini is not mistaken for a user file'

            $start = Invoke-DryStart
            Assert-Equal $start.Code 0 ('Start-Lab succeeds: ' + $start.Text)
            $dryRun = $start.Dir
            $runJson = $start.Run
            Assert-True ($runJson.instances.host.pid -gt 0 -and $runJson.instances.guest.pid -gt 0 -and $runJson.fuse.pid -gt 0) 'run.json records the pids'
            Assert-Equal ($runJson.fuse.args -join ' ') "-standalone -listenAddr 127.0.0.1:$base -baseURL http://127.0.0.1:$base/ -writeLog" 'fuse started with -standalone'
            Assert-Equal "$($runJson.instances.host.safety.state)/$($runJson.instances.guest.safety.state)" 'ok/ok' 'both instances passed the safety gate'
            Assert-Equal $runJson.host_id_check_at_guest_start 'ok' 'host id checked before the guest started'
            Assert-True ($runJson.instances.host.process_start_utc -and $runJson.instances.guest.process_start_utc) 'process start times recorded'
            Assert-True (-not (Test-PathInside -Child $runJson.fuse.stdout -Parent $dryRun)) 'fuse console log kept outside the run folder'
            Assert-True (Test-Path -LiteralPath (Join-Path $dryRun 'real-saves-before.json')) 'real-save manifest at run start'
            Assert-Equal (Get-Content -LiteralPath (Join-Path $dryRun 'real-saves-before.json') -Raw | ConvertFrom-Json).source_root 'Steam/userdata/<account>' 'run folder does not name the account folder'

            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Invoke-LabSmoke.ps1') -ConfigPath $dryConfig -PollMs 200 -TimeoutSec 30 -ReadyTimeoutSec 60 2>&1
            Assert-Equal $LASTEXITCODE 0 ('smoke passes against the stand-ins: ' + ($out -join ' | '))
            $result = Get-Content -LiteralPath (Join-Path $dryRun 'result.json') -Raw | ConvertFrom-Json
            Assert-Equal $result.verdict 'pass' 'dry-run verdict'
            Assert-Equal $result.evidence.guest.final_status.session_status 'client_established' 'lab-status.json read with the expected pid'
            Assert-True ([int]$result.fuse_messages.CMD_GET_URLLIST -ge 2 -and [int]$result.fuse_counts.CMD_AUTH_STEAMTICKET -ge 2) 'fuse traffic from both instances counted'
            Assert-Equal "$($result.fuse_identities.distinct_auth_ids)/$($result.fuse_identities.lab_guest_id_seen)" '2/True' 'the guest reached fuse with the lab id'
            Assert-Equal $result.host_id_check 'ok' 'dry-run host id cross-check'
            Assert-Equal @($result.screenshots).Count 2 'one screenshot per window'

            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Stop-Lab.ps1') -ConfigPath $dryConfig -TimeoutSec 10 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Stop-Lab succeeds with unchanged saves: ' + ($out -join ' | '))
            $stop = Get-Content -LiteralPath (Join-Path $dryRun 'stop.json') -Raw | ConvertFrom-Json
            Assert-Equal "$($stop.games.host.exit)/$($stop.games.guest.exit)/$($stop.fuse)" 'closed/closed/stopped' 'games closed by WM_CLOSE, fuse stopped'
            Assert-True $stop.host_lab_coop_ini_present 'Stop-Lab reports the lingering lab coop.ini'
            foreach ($id in $runJson.instances.host.pid, $runJson.instances.guest.pid, $runJson.fuse.pid) {
                Assert-True ($null -eq (Get-Process -Id $id -ErrorAction SilentlyContinue)) "process $id gone"
            }
            Assert-True ([IO.File]::ReadAllText($runJson.fuse.stdout).Contains($dryHostId)) 'raw fuse console names the host'
            $leaks = Get-PrivateLeaks -Dir $dryRun -Secrets @($dryHostId, 'userdataS45')
            Assert-Equal ($leaks -join ',') '' 'dry run folder holds no SteamID or account folder'

            # A normal session started later from the host folder is not this run's: left alone.
            $later = Start-Process -FilePath $hp.Exe -WorkingDirectory $hp.Game -PassThru
            $deadline = [DateTime]::UtcNow.AddSeconds(15)
            while ((Get-ProcessMainWindow -Id $later.Id) -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline -and !$later.HasExited) { Start-Sleep -Milliseconds 200 }
            Assert-True (!$later.HasExited) 'later session running'
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Stop-Lab.ps1') -ConfigPath $dryConfig -TimeoutSec 3 2>&1
            Assert-Equal $LASTEXITCODE 0 'Stop-Lab with an unrecorded session'
            Start-Sleep -Milliseconds 500
            Assert-True (!$later.HasExited) 'Stop-Lab leaves a process it did not start running'
            $stop = Get-Content -LiteralPath (Join-Path $dryRun 'stop.json') -Raw | ConvertFrom-Json
            Assert-Equal $stop.games."unrecorded-host-$($later.Id)".action 'left_running' 'unrecorded session reported as left running'
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Stop-Lab.ps1') -ConfigPath $dryConfig -TimeoutSec 10 -IncludeUnrecordedProcesses 2>&1
            Assert-True $later.WaitForExit(10000) '-IncludeUnrecordedProcesses closes it with WM_CLOSE'
            Assert-Equal (Get-Content -LiteralPath (Join-Path $dryRun 'stop.json') -Raw | ConvertFrom-Json).games."unrecorded-host-$($later.Id)".exit 'closed' 'closed, not terminated'

            # Safety gate refusals: Start-Lab stops what it started and never starts the guest early.
            $env:MGS_COOP_FAKE_UNSAFE = 'host:saves'
            $start = Invoke-DryStart
            Assert-True ($start.Code -ne 0 -and $start.Text.Contains('unsafe_saves_unchanged')) ('host saves=unchanged refused: ' + $start.Text)
            Assert-True (!($start.Run.instances.PSObject.Properties.Name -contains 'guest') -or !($start.Run.instances.guest.PSObject.Properties.Name -contains 'pid')) 'guest never started after an unsafe host'
            Assert-True ((@($start.Run.aborted.stopped) -contains 'host:terminated_immediately') -and (@($start.Run.aborted.stopped) -contains 'fuse:stopped')) 'unsafe host terminated at once, fuse stopped'
            Assert-True (Test-DryProcessesGone) 'nothing left running after the host refusal'
            $env:MGS_COOP_FAKE_UNSAFE = 'host:master'
            $start = Invoke-DryStart
            Assert-True ($start.Code -ne 0 -and $start.Text.Contains('unsafe_master_server_refused')) 'master_server=refused stops the host'
            Assert-True (Test-DryProcessesGone) 'nothing left running after the redirect refusal'
            $env:MGS_COOP_FAKE_UNSAFE = 'guest:identity'
            $start = Invoke-DryStart
            Assert-True ($start.Code -ne 0 -and $start.Text.Contains('unsafe_identity_real')) 'guest identity=real refused'
            Assert-True ((@($start.Run.aborted.stopped) -contains 'guest:terminated_immediately') -and (@($start.Run.aborted.stopped) -contains 'host:closed')) 'unsafe guest terminated at once, host closed'
            Assert-True (Test-DryProcessesGone) 'nothing left running after the guest refusal'
            $env:MGS_COOP_FAKE_UNSAFE = ''
            $env:MGS_COOP_FAKE_HOST_STEAMID = ConvertTo-SteamId64 -AccountId 54321
            $start = Invoke-DryStart
            Assert-True ($start.Code -ne 0 -and $start.Text.Contains('host_id_mismatch')) 'host id mismatch stops the start before the guest'
            Assert-True (!($start.Run.instances.guest.PSObject.Properties.Name -contains 'pid')) 'guest not started on a host id mismatch'
            Assert-Equal $start.Run.host_id_check_at_guest_start 'mismatch' 'mismatch recorded'
            Assert-True (Test-DryProcessesGone) 'nothing left running after the id mismatch'
            $env:MGS_COOP_FAKE_HOST_STEAMID = $dryHostId

            # Offline lab mode (docs/offline-partner-spike-plan.md 2.5, 4): no fuse, closed master port,
            # offline-v1 on both folders; the safety gate is unchanged.
            $dryDead = "http://127.0.0.1:$($base + 5)/tppstm/gate"
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig -Offline -PartnerSetting 'guest:net_latency_ms=100' 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Prepare -Offline succeeds: ' + ($out -join ' | '))
            Assert-Equal "$([IO.File]::ReadAllText($hp.Marker))|$([IO.File]::ReadAllText($gp.Marker))" "offline-v1`n|offline-v1`n" 'offline-v1 marker on both folders'
            $hl = ConvertFrom-IniText ([IO.File]::ReadAllText($hp.LabIni))
            $gl = ConvertFrom-IniText ([IO.File]::ReadAllText($gp.LabIni))
            Assert-Equal "$($hl['net'])/$($hl['net_listen_port'])/$($hl['net_peer_port'])/$($hl['partner'])/$($hl['partner_stage'])" "1/$($base + 3)/$($base + 4)/1/0" 'host lab.ini partner keys'
            Assert-Equal "$($gl['net_listen_port'])/$($gl['net_peer_port'])/$($gl['net_latency_ms'])/$($gl['self_steamid'])/$($gl['mutex_suffix'])" "$($base + 4)/$($base + 3)/100/76561202255233008/-guest" 'guest lab.ini: swapped ports, override, fake identity and suffix kept'
            Assert-Equal "$((ConvertFrom-IniText ([IO.File]::ReadAllText($hp.CoopIni)))['master_url'])|$((ConvertFrom-IniText ([IO.File]::ReadAllText($gp.CoopIni)))['master_url'])" "$dryDead|$dryDead" 'coop.ini: closed-port master URL in both folders'
            Assert-Equal @(Get-ChildItem -LiteralPath $hp.Mod -File -Filter 'coop.ini.user-*').Count 1 'switching modes keeps the single user backup'
            Assert-Equal "$((ConvertFrom-IniText ([IO.File]::ReadAllText($hp.CoopIni)))['character'])|$((ConvertFrom-IniText ([IO.File]::ReadAllText($gp.CoopIni)))['character'])" 'default|quiet' 'offline coop.ini: host default, guest Quiet (lab.json offline.characters)'
            Assert-True (($out -join ' ').Contains('[guest] coop.ini character=quiet')) 'Prepare -Offline reports each coop.ini character'
            $out = (& $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Start-Lab.ps1') -ConfigPath $dryConfig 2>&1 | Out-String)
            Assert-True ($LASTEXITCODE -ne 0 -and $out.Contains('use -Offline') -and (Test-DryProcessesGone)) 'Start-Lab without -Offline refuses offline-prepared folders'
            $start = Invoke-DryStart @('-Offline')
            Assert-Equal $start.Code 0 ('Start-Lab -Offline succeeds: ' + $start.Text)
            $offRun = $start.Run
            Assert-True ($offRun.offline -and $null -eq $offRun.fuse -and $offRun.offline_master_url -eq $dryDead) 'run.json: offline=true, no fuse, closed-port URL'
            Assert-Equal "$($offRun.ports.net_host)/$($offRun.ports.net_guest)/$($offRun.ports.offline_master)" "$($base + 3)/$($base + 4)/$($base + 5)" 'run.json: partner and master ports'
            Assert-Equal "$($offRun.instances.host.safety.state)/$($offRun.instances.guest.safety.state)" 'ok/ok' 'offline: both instances passed the unchanged safety gate'
            Assert-Equal "$($offRun.instances.host.expected.master_url)|$($offRun.instances.guest.expected.mutex_suffix)" "$dryDead|-guest" 'the gate expected the closed-port URL and the guest suffix'
            Assert-Equal "$($offRun.instances.guest.partner.net_latency_ms)/$($offRun.instances.host.partner.partner)/$($offRun.instances.host.partner.net)" '100/1/1' 'run.json records the partner lab.ini values'
            Assert-True (!$start.Text.Contains('fuse listening') -and @(Get-Process -Name fuse -ErrorAction SilentlyContinue | Where-Object { $fp = Get-ProcessPathSafe $_; $fp -and (Test-PathInside -Child $fp -Parent $dry) }).Count -eq 0) 'no fuse started offline'
            $out = @(& $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Test-PartnerSpike.ps1') -ConfigPath $dryConfig -Experiment E0 2>&1)
            $spikeCode = $LASTEXITCODE
            $spike = (@($out | Where-Object { $_ -is [string] }) -join "`n") | ConvertFrom-Json
            Assert-True ($spikeCode -eq 2 -and $spike.verdict -eq 'INCOMPLETE' -and (@($spike.missing) -join ' ').Contains('mode=offline_v1')) ('E0 on the stand-ins is INCOMPLETE (they write no partner lines): ' + ($out -join ' '))
            Assert-True (!(@($spike.missing) -join ' ').Contains('safety:') -and @($spike.failed).Count -eq 0) 'E0 on the stand-ins: safety evidence complete, processes alive, nothing failed'
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Set-PartnerTrigger.ps1') -ConfigPath $dryConfig -Role both -Text 'stage=1 source=fixed apply=warp' 2>&1
            Assert-True ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $hp.PartnerTrigger) -and (Test-Path -LiteralPath $gp.PartnerTrigger)) ('partner triggers written during the run: ' + ($out -join ' | '))
            Assert-True ([IO.File]::ReadAllText((Join-Path $start.Dir 'partner-triggers.log')).Contains('guest stage=1 source=fixed apply=warp')) 'trigger log in the offline run folder'
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Stop-Lab.ps1') -ConfigPath $dryConfig -TimeoutSec 10 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Stop-Lab after the offline run: ' + ($out -join ' | '))
            $stop = Get-Content -LiteralPath (Join-Path $start.Dir 'stop.json') -Raw | ConvertFrom-Json
            Assert-Equal "$($stop.games.host.exit)/$($stop.games.guest.exit)/$($stop.fuse)" 'closed/closed/not_running' 'offline run closed; no fuse to stop'
            Assert-True (!(Test-Path -LiteralPath $hp.PartnerTrigger) -and !(Test-Path -LiteralPath $gp.PartnerTrigger) -and (@($stop.partner_triggers_removed) -join ',') -eq 'host,guest') 'Stop-Lab removed the unconsumed partner triggers'
            Assert-True (($out -join ' ').Contains('closed local port')) 'Stop-Lab names the lingering offline redirect'
            Assert-True ([Int64]$stop.log_end_offsets.host.'early.log' -ge [Int64]$offRun.instances.host.log_offsets.'early.log' -and [Int64]$stop.log_end_offsets.host.'early.log' -gt 0) 'offline stop.json: log end offsets after the run start offsets'
            # A listener on the closed master port blocks the offline start (the game would reach it).
            $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $base + 5)
            $listener.Start()
            try {
                $out = (& $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Start-Lab.ps1') -ConfigPath $dryConfig -Offline 2>&1 | Out-String)
                Assert-True ($LASTEXITCODE -ne 0 -and $out.Contains("TCP port $($base + 5)") -and (Test-DryProcessesGone)) 'Start-Lab -Offline refuses a listener on the master port; nothing started'
            } finally { $listener.Stop() }
            # -Lan rehearsal (docs/two-pc-play.md): net_bind=any, the guest targets the LAN
            # address, both copies share a random link secret; Start-Lab needs -Lan for it.
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig -Offline -Lan -LanAddress 192.0.2.10 -WanSim good 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Prepare -Offline -Lan -WanSim good succeeds: ' + ($out -join ' | '))
            $hostSecret = [IO.File]::ReadAllBytes($hp.LinkSecret)
            $guestSecret = [IO.File]::ReadAllBytes($gp.LinkSecret)
            Assert-True ((Get-CoopSecretFileStatus $hostSecret) -eq 'ok' -and [Convert]::ToBase64String($hostSecret) -ceq [Convert]::ToBase64String($guestSecret) -and $hostSecret.Length -eq 22) 'Prepare -Lan: the same random link secret (20 characters + CRLF) in both copies'
            Assert-True ([Text.Encoding]::ASCII.GetString($hostSecret, 0, 20) -cne (Get-CoopLabSecret)) 'Prepare -Lan: not the public lab secret'
            Assert-True (!($out -join ' ').Contains([Text.Encoding]::ASCII.GetString($hostSecret, 0, 20))) 'Prepare -Lan never prints the secret'
            $hl = ConvertFrom-IniText ([IO.File]::ReadAllText($hp.LabIni))
            $gl = ConvertFrom-IniText ([IO.File]::ReadAllText($gp.LabIni))
            Assert-Equal "$($hl['net_bind'])/$($gl['net_bind'])/$($gl['net_peer_addr'])/$($hl['net_listen_port'])/$($gl['net_peer_port'])" "any/any/192.0.2.10/$($base + 3)/$($base + 3)" 'Prepare -Lan lab.ini: bind any, guest targets the LAN address on the host port'
            Assert-Equal "$($hl['net_latency_ms'])/$($hl['net_seed'])/$($gl['net_seed'])/$($gl['partner_interp'])" '30/1/2/auto' 'Prepare -WanSim good: preset, seeds 1/2, adaptive interpolation'
            $out = (& $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Start-Lab.ps1') -ConfigPath $dryConfig -Offline 2>&1 | Out-String)
            Assert-True ($LASTEXITCODE -ne 0 -and $out.Contains('prepared with -Lan') -and (Test-DryProcessesGone)) 'Start-Lab -Offline without -Lan refuses -Lan folders'
            $start = Invoke-DryStart @('-Offline', '-Lan')
            Assert-Equal $start.Code 0 ('Start-Lab -Offline -Lan succeeds: ' + $start.Text)
            Assert-True ($start.Run.lan -and $start.Run.offline -and "$($start.Run.instances.guest.partner.net_peer_addr)" -eq '192.0.2.10') 'run.json: lan=true and the guest peer address'
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Stop-Lab.ps1') -ConfigPath $dryConfig -TimeoutSec 10 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Stop-Lab after the -Lan run: ' + ($out -join ' | '))
            [IO.File]::WriteAllText($gp.LinkSecret, "SHORT`r`n")
            $out = (& $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Start-Lab.ps1') -ConfigPath $dryConfig -Offline -Lan 2>&1 | Out-String)
            Assert-True ($LASTEXITCODE -ne 0 -and $out.Contains('link-secret.txt is bad_length') -and (Test-DryProcessesGone)) 'Start-Lab -Lan refuses a damaged link secret'
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig -Offline 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Prepare -Offline after -Lan: ' + ($out -join ' | '))
            Assert-True (!(Test-Path -LiteralPath $hp.LinkSecret) -and !(Test-Path -LiteralPath $gp.LinkSecret)) 'Prepare without -Lan deletes link-secret.txt in both copies'
            Assert-True (!(ConvertFrom-IniText ([IO.File]::ReadAllText($gp.LabIni))).Contains('net_bind')) 'Prepare without -Lan: the loopback lab.ini again'
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig -Offline -Lan -LanAddress 192.0.2.10 2>&1
            Assert-True ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $hp.LinkSecret) -and (Test-Path -LiteralPath $gp.LinkSecret)) ('Prepare -Lan again (for the -Disable check): exit ' + $LASTEXITCODE + ': ' + ($out -join ' | '))

            # -Disable after offline mode must remove the active marker, never enable legacy sessions.
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig -Disable 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Disable after the offline mode: ' + ($out -join ' | '))
            Assert-True (!(Test-Path -LiteralPath $hp.LinkSecret) -and !(Test-Path -LiteralPath $gp.LinkSecret)) '-Disable deletes the -Lan link secrets'
            Assert-True (!(Test-Path -LiteralPath $hp.Marker) -and !(Test-Path -LiteralPath $gp.Marker)) '-Disable removes both active offline markers for native pass-through'
            foreach ($instance in @($hp, $gp)) {
                $archives = @(Get-ChildItem -LiteralPath $instance.Mod -File -Filter 'script-observe.enabled.disabled-*')
                Assert-True ($archives.Count -eq 1 -and [IO.File]::ReadAllText($archives[0].FullName) -ceq "offline-v1`n") '-Disable archives exact offline marker bytes once'
            }
            Assert-True (($out -join ' ').Contains('normal launches use pass-through') -and !(Test-Path -LiteralPath $hp.LabIni) -and [IO.File]::ReadAllText($hp.CoopIni) -ceq $userCoop) '-Disable after offline: lab files renamed, user coop.ini restored, pass-through reported'
            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig 2>&1
            Assert-Equal $LASTEXITCODE 0 ('Prepare without -Offline switches back: ' + ($out -join ' | '))
            Assert-Equal "$([IO.File]::ReadAllText($hp.Marker))|$([IO.File]::ReadAllText($gp.Marker))|$((ConvertFrom-IniText ([IO.File]::ReadAllText($hp.CoopIni)))['master_url'])" "host-v3`n|client-v1`n|$dryUrl" 'back to host-v3 / client-v1 and the fuse URL'
            Assert-True (!(ConvertFrom-IniText ([IO.File]::ReadAllText($gp.LabIni))).Contains('partner')) 'the online lab.ini has no partner keys again'

            $out = & $pwshPath -NoProfile -NonInteractive -File (Join-Path $labDir 'Prepare-LabInstance.ps1') -ConfigPath $dryConfig -Disable 2>&1
            Assert-Equal $LASTEXITCODE 0 'Disable succeeds'
            Assert-Equal "$([IO.File]::ReadAllText($hp.Marker))|$([IO.File]::ReadAllText($gp.Marker))" "host-v3`n|client-v1`n" '-Disable preserves separately selected explicit online markers'
            Assert-True ((-not (Test-Path -LiteralPath $hp.LabIni)) -and (-not (Test-Path -LiteralPath $gp.CoopIni)) -and
                @(Get-ChildItem -LiteralPath $gp.Mod -Filter 'lab.ini.disabled-*').Count -eq 2) 'Disable renames lab.ini and coop.ini (one renamed copy per -Disable: offline, then fuse mode)'
            Assert-Equal ([IO.File]::ReadAllText($hp.CoopIni)) $userCoop 'Disable restores the user coop.ini'
            Assert-Equal @(Get-ChildItem -LiteralPath $hp.Mod -File -Filter 'coop.ini.user-*').Count 0 'user backup moved back'
        } finally {
            Remove-Item Env:MGS_COOP_FAKE_HOST_STEAMID -ErrorAction SilentlyContinue
            Remove-Item Env:MGS_COOP_FAKE_UNSAFE -ErrorAction SilentlyContinue
            foreach ($p in @(Get-Process -Name mgsvtpp, fuse -ErrorAction SilentlyContinue)) {
                $path = Get-ProcessPathSafe $p
                if ($path -and (Test-PathInside -Child $path -Parent $dry)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
            }
        }
    } else { Write-Warning 'lab_fake_process not supplied; full harness dry run skipped.' }

    Write-Output "$script:checks lab script checks passed; temporary fixtures only, no game, Steam or fuse started"
} finally {
    Remove-OwnedFixture
}
