#Requires -Version 7
<#
.SYNOPSIS
Sends a runtime command to the offline partner spike of one lab instance by
writing <game>/mgs-coop/partner.trigger (docs/offline-partner-spike-plan.md 2.1).

.DESCRIPTION
The text is validated against the trigger grammar first: 'stop' alone, or any
of stage=0..3 source=fixed|orbit|net apply=warp|slot|both|xform|hybrid|input
(move= is the same key) anim=off|mirror|net aset=q|s|qs|r reset=n|a|c|ac
look=copy|net|quiet|snake probe=ps|off; the round 3 keys (docs/partner-anim-
appearance.md "Round 3") input=off|pass|neutral|stick|net stick=<0..359>:<0..100>
cam=off|detect|sandbox hold=none|pass yaw=rot|rotneg|none words=<3 of 0prhn>
bits=0x<hex> correct=<0..10000> isend=0|1 p218=0|1 padprobe=0|1 holdlog=0|1
dm=off|probe|on lookgate=0|1 lookrecover=0|1; and the one-shots census=1
(component census), parts=explicit (op 16: re-apply the look; with look=copy
the local look, F3b), mirror=on (op 32, mirror diagnostic), tap=0x<hex>,
press=0x<hex>:<50..5000>, dmplay=<0..9> and dmstop=1, each at most once,
separated by blanks. parts/mirror/dmplay act only at stage >= 1; tap/press
only while the input window is armed and within the mask; p218 needs lab.ini
partner_input_p218_allow=1. It is written in canonical form (one ASCII line,
at most 255 bytes with the newline) to a temporary file in the instance's
mgs-coop folder, which is then renamed over partner.trigger, so the native
side never reads a partial line. No window focus is needed.
docs/partner-anim-appearance.md describes the animation, movement and look
experiments and their log lines.

The native side honours the file only with the offline-v1 marker and lab.ini
partner=1, polls it every 30 Lua ticks during a 30050 mission, deletes it and
logs partner_stage=changed (or partner_trigger=rejected; 'stop' clears the
realize bits: partner_bits=cleared; reason=trigger). The script warns when the
instance is not prepared that way. An earlier partner.trigger that was not
consumed yet is not replaced without -Force.

-WaitSec waits for the native side to consume the file and then confirms the
result in that instance's script-observer.log (the lines appended since just
before the write): the native side also deletes a trigger it rejects, so a
disappearing file alone proves nothing. When a current lab run exists, the
command is appended to <run>/partner-triggers.log (UTC time, role, text).
Nothing else is written; the game is not touched.

Exit code: 0 written (with -WaitSec: applied, partner_stage=changed shows the
requested values); 2 not consumed within -WaitSec; 3 rejected
(partner_trigger=rejected) or a different command was applied (a -Force
replacement raced the native poll); 4 consumed but no result line appeared in
script-observer.log within 3 s.

.EXAMPLE
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text 'stage=1 source=fixed apply=warp'
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text 'stage=3 source=orbit' -WaitSec 10
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role both -Text 'stage=3 source=net'
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text parts=explicit -WaitSec 10
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text 'stage=3 source=orbit move=xform reset=n census=1'
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text 'stage=3 source=fixed anim=mirror aset=qs look=quiet'
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role both -Text 'stage=3 source=net apply=hybrid anim=net aset=qs look=net'
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text 'stage=3 source=fixed apply=input input=stick stick=180:100 cam=sandbox'
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role both -Text 'stage=3 source=net apply=input input=net isend=1 cam=sandbox hold=own words=hpr bits=0x2200006 recon=track look=net'
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text 'wrap=census'
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text 'dm=on dmplay=1'
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text stop
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
Import-Module (Join-Path $PSScriptRoot 'PartnerSpike.psm1') -Force

$canonical = ConvertTo-PartnerTriggerText -Text $Text
$config = Get-LabConfig -ConfigPath $ConfigPath
$roles = if ($Role -eq 'both') { @('host', 'guest') } else { @($Role) }
$dirs = @{ host = $config.Paths.hostDir; guest = $config.Paths.guestDir }

$targets = foreach ($name in $roles) {
    $p = Get-InstancePaths -GameDirectory $dirs[$name]
    if (!(Test-Path -LiteralPath $p.Mod -PathType Container)) { throw "[$name] $($p.Mod) does not exist (run Prepare-LabInstance.ps1 -Offline)." }
    Assert-NoReparsePoint -Path $p.Mod -StopAt $p.Game
    if (Test-Path -LiteralPath $p.PartnerTrigger) {
        Assert-NoReparsePoint -Path $p.PartnerTrigger -StopAt $p.Game
        if (!$Force) { throw "[$name] an earlier partner.trigger was not consumed yet (the native side polls it only during an offline-v1 mission with partner=1). Pass -Force to replace it." }
    }
    $marker = if (Test-Path -LiteralPath $p.Marker -PathType Leaf) { [IO.File]::ReadAllText($p.Marker).Trim() } else { '' }
    $lab = ConvertFrom-IniText (Read-SharedText -Path $p.LabIni)
    if ($marker -cne 'offline-v1' -or [string](Get-ConfigValue $lab 'partner' '') -cne '1') {
        Write-Warning "[$name] the instance is not prepared for the partner spike (marker '$marker', lab.ini partner=$([string](Get-ConfigValue $lab 'partner' ''))); the native side will ignore the trigger. Run Prepare-LabInstance.ps1 -Offline."
    }
    [pscustomobject]@{ Role = $name; Path = $p.PartnerTrigger; Observer = (Join-Path $p.Mod 'script-observer.log'); Offset = [Int64]0 }
}

$written = @()
foreach ($t in @($targets)) {
    if (!$PSCmdlet.ShouldProcess($t.Path, "Write partner trigger '$canonical'")) { continue }
    # The native result line is appended after this offset (checked with -WaitSec).
    $t.Offset = Get-FileLengthOrZero $t.Observer
    New-TriggerFile -Path $t.Path -Text $canonical
    $written += $t
    Write-Output "[$($t.Role)] partner.trigger <- '$canonical'"
}
if (!$written.Count) { return }

$current = $null
try { $current = Get-CurrentRun -Config $config -RunDir $RunDir } catch { $current = $null }
if ($current) {
    $log = Join-Path $current.Dir 'partner-triggers.log'
    foreach ($t in $written) { [IO.File]::AppendAllText($log, ('{0} {1} {2}' -f [DateTime]::UtcNow.ToString('o'), $t.Role, $canonical) + "`n", [Text.Encoding]::ASCII) }
}

if ($WaitSec -gt 0) {
    $deadline = [DateTime]::UtcNow.AddSeconds($WaitSec)
    while ([DateTime]::UtcNow -lt $deadline -and @($written | Where-Object { Test-Path -LiteralPath $_.Path }).Count) { Start-Sleep -Milliseconds 250 }
    # Worst result wins: 3 (rejected / different) > 2 (not consumed) > 4 (unconfirmed) > 0.
    $codes = [Collections.Generic.List[int]]::new()
    foreach ($t in $written) {
        if (Test-Path -LiteralPath $t.Path) {
            Write-Warning "[$($t.Role)] partner.trigger was not consumed within $WaitSec s (is a 30050 mission running in offline-v1 with partner=1?)."
            $codes.Add(2)
            continue
        }
        # Native logs the result right after deleting the file; allow 3 s for it.
        $result = $null
        $confirmBy = [DateTime]::UtcNow.AddSeconds(3)
        do {
            $text = (Read-LogFromOffset -Path $t.Observer -Offset $t.Offset -MaxBytes 4MB).Text
            $result = Find-PartnerTriggerResult -LogText $text -Canonical $canonical
            if ($result.State -ne 'none') { break }
            Start-Sleep -Milliseconds 200
        } while ([DateTime]::UtcNow -lt $confirmBy)
        switch ($result.State) {
            'applied' { Write-Output "[$($t.Role)] applied by the native side: $($result.Line)"; $codes.Add(0) }
            'rejected' {
                Write-Warning "[$($t.Role)] REJECTED by the native side ($($result.Line)): the native trigger parser refused '$canonical'."
                $codes.Add(3)
            }
            'different' {
                Write-Warning "[$($t.Role)] the native side applied a different command ($($result.Line)), not '$canonical': a replaced (-Force) trigger raced the native poll. Send it again."
                $codes.Add(3)
            }
            default {
                Write-Warning "[$($t.Role)] partner.trigger was consumed, but script-observer.log shows no partner_stage=changed or partner_trigger=rejected line after it (unconfirmed)."
                $codes.Add(4)
            }
        }
    }
    foreach ($c in 3, 2, 4) { if ($codes.Contains($c)) { exit $c } }
}
