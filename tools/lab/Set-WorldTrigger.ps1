#Requires -Version 7
<#
.SYNOPSIS
Sends a runtime command to the world sync of one or both lab instances by
writing <game>/mgs-coop/world.trigger (docs/world-sync-protocol.md
"world.trigger", docs/lab-protocol.md).

.DESCRIPTION
The text is validated against the trigger grammar first: any of world=0..3,
world_probe=<probe>|off (p1 p3 p3b p4 p12 p17 race_kill remote_area_kill
kill_near sleep_near fulton_near alert_near weather_rain clock_jump
clock_fast), goto=30010|30020 and goto_lz=0|1 (only with goto), each at most
once, separated by blanks. It is written in canonical form (one ASCII line,
at most 255 bytes with the newline) to a temporary file in the instance's
mgs-coop folder, which is then renamed over world.trigger, so the native side
never reads a partial line. No window focus is needed.

Effects (native, lab mode only; never in play): world=0 turns world sync off at
once (LEVEL 0, its weather forces are released); world=1..3 applies at the next
mission start; world_probe=<probe> runs that probe now when a mission runs
with world sync active, else 10 s after the next mission start (off cancels a
pending one); goto=30010|30020 sends a copy that stands in Mother Base (30050)
to free roam through the game's own calls: the helicopter boarding, then the
ACC sortie (goto_lz=1 picks the second landing zone). Send goto to both copies
(-Role both). The native side polls the file at most once per second while a
mission with world sync runs (world>=1), deletes it and logs
world_trigger=applied; text=<canonical> or world_trigger=rejected; reason=...
A goto is then either armed (world_goto=armed) or refused
(world_goto=refused; reason=level|mission|target|busy|terminating).

-WaitSec waits for the native side to consume the file and then confirms the
result in that instance's script-observer.log (the lines appended since just
before the write). When a current lab run exists, the command is appended to
<run>/world-triggers.log (UTC time, role, text). Nothing else is written; the
game is not touched.

Exit code: 0 written (with -WaitSec: applied, and for a goto armed); 2 not
consumed within -WaitSec; 3 rejected (a goto: refused), or a different command
was applied (a -Force replacement raced the native poll); 4 consumed but no
result line (for a goto: no world_goto=armed or refused line) appeared in
script-observer.log within 3 s.

.EXAMPLE
pwsh -File tools/lab/Set-WorldTrigger.ps1 -Role both -Text 'goto=30010' -WaitSec 15
pwsh -File tools/lab/Set-WorldTrigger.ps1 -Role host -Text 'world_probe=race_kill' -WaitSec 10
pwsh -File tools/lab/Set-WorldTrigger.ps1 -Role guest -Text 'world_probe=sleep_near'
pwsh -File tools/lab/Set-WorldTrigger.ps1 -Role both -Text 'world=0'
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][ValidateSet('host', 'guest', 'both')][string]$Role,
    [Parameter(Mandatory)][AllowEmptyString()][string]$Text,
    [string]$ConfigPath = (Join-Path $PSScriptRoot 'lab.json'),
    [string]$RunDir,
    [ValidateRange(0, 600)][int]$WaitSec = 0,
    [switch]$Force
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'LabCommon.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'WorldSync.psm1') -Force

$canonical = ConvertTo-WorldTriggerText -Text $Text
$config = Get-LabConfig -ConfigPath $ConfigPath
$roles = if ($Role -eq 'both') { @('host', 'guest') } else { @($Role) }
$dirs = @{ host = $config.Paths.hostDir; guest = $config.Paths.guestDir }

$targets = foreach ($name in $roles) {
    $p = Get-InstancePaths -GameDirectory $dirs[$name]
    if (!(Test-Path -LiteralPath $p.Mod -PathType Container)) { throw "[$name] $($p.Mod) does not exist (run Prepare-LabInstance.ps1 -Offline)." }
    Assert-NoReparsePoint -Path $p.Mod -StopAt $p.Game
    if (Test-Path -LiteralPath $p.WorldTrigger) {
        Assert-NoReparsePoint -Path $p.WorldTrigger -StopAt $p.Game
        if (!$Force) { throw "[$name] an earlier world.trigger was not consumed yet (the native side polls it only while a mission with world sync runs). Pass -Force to replace it." }
    }
    $marker = if (Test-Path -LiteralPath $p.Marker -PathType Leaf) { [IO.File]::ReadAllText($p.Marker).Trim() } else { '' }
    $lab = ConvertFrom-IniText (Read-SharedText -Path $p.LabIni)
    $world = [string](Get-ConfigValue $lab 'world' '')
    if ($marker -cne 'offline-v1' -or !$world -or $world -ceq '0') {
        Write-Warning "[$name] the instance is not prepared for world sync (marker '$marker', lab.ini world=$world); the native side will ignore the trigger. Run Play-OfflineCoop.ps1 -World 1..3 (or Prepare-LabInstance.ps1 -Offline -PartnerSetting world=N)."
    }
    if ([string](Get-ConfigValue $lab 'mode' 'lab') -ceq 'play') {
        Write-Warning "[$name] lab.ini mode=play: world.trigger is never honoured in play."
    }
    [pscustomobject]@{ Role = $name; Path = $p.WorldTrigger; Observer = (Join-Path $p.Mod 'script-observer.log'); Offset = [Int64]0 }
}

$written = @()
foreach ($t in @($targets)) {
    if (!$PSCmdlet.ShouldProcess($t.Path, "Write world trigger '$canonical'")) { continue }
    $t.Offset = Get-FileLengthOrZero $t.Observer
    New-TriggerFile -Path $t.Path -Text $canonical
    $written += $t
    Write-Output "[$($t.Role)] world.trigger <- '$canonical'"
}
if (!$written.Count) { return }

$current = $null
try { $current = Get-CurrentRun -Config $config -RunDir $RunDir } catch { $current = $null }
if ($current) {
    $log = Join-Path $current.Dir 'world-triggers.log'
    foreach ($t in $written) { [IO.File]::AppendAllText($log, ('{0} {1} {2}' -f [DateTime]::UtcNow.ToString('o'), $t.Role, $canonical) + "`n", [Text.Encoding]::ASCII) }
}

if ($WaitSec -gt 0) {
    $deadline = [DateTime]::UtcNow.AddSeconds($WaitSec)
    while ([DateTime]::UtcNow -lt $deadline -and @($written | Where-Object { Test-Path -LiteralPath $_.Path }).Count) { Start-Sleep -Milliseconds 250 }
    # Worst result wins: 3 (rejected / different) > 2 (not consumed) > 4 (unconfirmed) > 0.
    $codes = [Collections.Generic.List[int]]::new()
    foreach ($t in $written) {
        if (Test-Path -LiteralPath $t.Path) {
            Write-Warning "[$($t.Role)] world.trigger was not consumed within $WaitSec s (is a mission with world sync running?)."
            $codes.Add(2)
            continue
        }
        $result = $null
        $confirmBy = [DateTime]::UtcNow.AddSeconds(3)
        do {
            $logText = (Read-LogFromOffset -Path $t.Observer -Offset $t.Offset -MaxBytes 4MB).Text
            $result = Find-WorldTriggerResult -LogText $logText -Canonical $canonical
            if ($result.State -ne 'none') { break }
            Start-Sleep -Milliseconds 200
        } while ([DateTime]::UtcNow -lt $confirmBy)
        switch ($result.State) {
            'applied' { Write-Output "[$($t.Role)] applied by the native side: $($result.Line)"; $codes.Add(0) }
            'rejected' { Write-Warning "[$($t.Role)] REJECTED by the native side, reason $($result.Reason) ($($result.Line))."; $codes.Add(3) }
            'different' {
                Write-Warning "[$($t.Role)] the native side applied a different command ($($result.Line)), not '$canonical'. Send it again."
                $codes.Add(3)
            }
            default {
                Write-Warning "[$($t.Role)] world.trigger was consumed, but script-observer.log shows no result for it (world_trigger=, and for a goto world_goto=armed|refused) (unconfirmed)."
                $codes.Add(4)
            }
        }
    }
    foreach ($c in 3, 2, 4) { if ($codes.Contains($c)) { exit $c } }
}
