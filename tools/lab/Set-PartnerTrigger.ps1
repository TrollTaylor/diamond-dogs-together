#Requires -Version 7
<#
.SYNOPSIS
Sends a runtime command to the offline partner spike of one lab instance by
writing <game>/mgs-coop/partner.trigger (docs/offline-partner-spike-plan.md 2.1).

.DESCRIPTION
The text is validated against the trigger grammar first: 'stop' alone, or any
of stage=0..3 source=fixed|orbit|net apply=warp|slot|both, each at most once,
separated by blanks. It is written in canonical form (stage, source, apply; one
ASCII line, at most 63 bytes with the newline) to a temporary file in the
instance's mgs-coop folder, which is then renamed over partner.trigger, so the
native side never reads a partial line. No window focus is needed.

The native side honours the file only with the offline-v1 marker and lab.ini
partner=1, polls it every 30 Lua ticks during a 30050 mission, deletes it and
logs partner_stage=changed (or partner_trigger=rejected; 'stop' clears the
realize bits: partner_bits=cleared; reason=trigger). The script warns when the
instance is not prepared that way. An earlier partner.trigger that was not
consumed yet is not replaced without -Force. -WaitSec waits for the native side
to consume the file (exit code 2 if it did not). When a current lab run exists,
the command is appended to <run>/partner-triggers.log (UTC time, role, text).
Nothing else is written; the game is not touched.

.EXAMPLE
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text 'stage=1 source=fixed apply=warp'
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text 'stage=3 source=orbit' -WaitSec 10
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role both -Text 'stage=3 source=net'
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
    [pscustomobject]@{ Role = $name; Path = $p.PartnerTrigger }
}

$written = @()
foreach ($t in @($targets)) {
    if (!$PSCmdlet.ShouldProcess($t.Path, "Write partner trigger '$canonical'")) { continue }
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
    $pending = @($written | Where-Object { Test-Path -LiteralPath $_.Path })
    foreach ($t in $written) {
        if ($pending -contains $t) { Write-Warning "[$($t.Role)] partner.trigger was not consumed within $WaitSec s (is a 30050 mission running in offline-v1 with partner=1?)." }
        else { Write-Output "[$($t.Role)] consumed by the native side." }
    }
    if ($pending.Count) { exit 2 }
}
