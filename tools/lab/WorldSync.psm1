# World sync lab helpers (docs/world-sync-protocol.md "world.trigger").
# Pure functions only (no file, process or window access):
#  - the grammar of <game>/mgs-coop/world.trigger, canonicalized exactly as
#    the native parser does (native/world_receipt.cpp ParseTrigger);
#  - the native result line in script-observer.log
#    (world_trigger=applied; text=<canonical> / world_trigger=rejected; reason=...,
#    for a goto also world_goto=armed / world_goto=refused; reason=...).
# Used by Set-WorldTrigger.ps1 and Play-OfflineCoop.ps1; unit tested by
# tests/lab_scripts_tests.ps1. ASCII only.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:WorldTriggerMaxBytes = 255  # native reads at most 255 bytes (world_receipt.hpp max_trigger)
# Probe ids 1..15 in this order (lab_config.hpp WorldProbe).
$script:WorldProbeNames = @('p1', 'p3', 'p3b', 'p4', 'p12', 'p17', 'race_kill', 'remote_area_kill', 'kill_near', 'sleep_near',
    'fulton_near', 'alert_near', 'weather_rain', 'clock_jump', 'clock_fast')
$script:WorldTriggerOrder = @('world', 'world_probe', 'goto', 'goto_lz')
$script:WorldTriggerUsage = 'world=0..3 world_probe=<probe>|off goto=30010|30020 goto_lz=0|1 (goto_lz only with goto)'

function Get-WorldProbeNames { return $script:WorldProbeNames }

function ConvertTo-WorldTriggerText {
    # Pure: validates a world.trigger line and returns its canonical form (keys
    # in the order world, world_probe, goto, goto_lz; single spaces).
    param([AllowNull()][AllowEmptyString()][string]$Text)
    if ($null -eq $Text -or !$Text.Trim()) { throw "World trigger text is empty ($script:WorldTriggerUsage)." }
    $values = [ordered]@{}
    foreach ($token in @($Text.Trim() -split '\s+')) {
        if ($token -cnotmatch '^(world|world_probe|goto|goto_lz)=(.*)$') { throw "Unknown world trigger token '$token' ($script:WorldTriggerUsage)." }
        $key = $Matches[1]
        $value = $Matches[2]
        if ($values.Contains($key)) { throw "Duplicate world trigger key '$key'." }
        $ok = switch ($key) {
            'world' { $value -cmatch '^[0-3]$' }
            'world_probe' { $value -ceq 'off' -or $value -cin $script:WorldProbeNames }
            'goto' { $value -cin @('30010', '30020') }
            'goto_lz' { $value -cin @('0', '1') }
        }
        if (!$ok) { throw "Invalid world trigger value '$token' ($script:WorldTriggerUsage)." }
        $values[$key] = $value
    }
    if ($values.Contains('goto_lz') -and !$values.Contains('goto')) { throw 'goto_lz applies only with goto.' }
    $canonical = (@($script:WorldTriggerOrder) | Where-Object { $values.Contains($_) } | ForEach-Object { $_ + '=' + $values[$_] }) -join ' '
    if ($canonical.Length + 1 -gt $script:WorldTriggerMaxBytes) { throw "World trigger text is longer than $script:WorldTriggerMaxBytes bytes." }
    return $canonical
}

function Test-WorldTriggerText {
    param([AllowNull()][AllowEmptyString()][string]$Text)
    try { $null = ConvertTo-WorldTriggerText -Text $Text; return $true } catch { return $false }
}

function Find-WorldTriggerResult {
    # Pure: the native result of the newest consumed world trigger in a log text
    # that starts at the offset recorded just before the trigger was written:
    #   State 'applied'   world_trigger=applied; text=<-Canonical> (with goto=:
    #                     and world_goto=armed after it)
    #   State 'different' world_trigger=applied for another text (a replaced
    #                     trigger raced the native poll)
    #   State 'rejected'  world_trigger=rejected; reason=<Reason>, or a goto
    #                     refused after it (world_goto=refused; reason=<Reason>)
    #   State 'none'      neither line yet (a goto: neither armed nor refused yet)
    param([AllowNull()][AllowEmptyString()][string]$LogText, [Parameter(Mandatory)][string]$Canonical)
    $last = $null
    $after = [Collections.Generic.List[string]]::new()
    foreach ($line in @(([string]$LogText) -split "\r?\n")) {
        if ($line.StartsWith('world_trigger=', [StringComparison]::Ordinal)) { $last = $line; $after.Clear() }
        elseif ($last) { $after.Add($line) }
    }
    if (!$last) { return [pscustomobject]@{ State = 'none'; Line = $null; Reason = '' } }
    if ($last -cmatch '^world_trigger=rejected; reason=([a-z0-9_]+)') {
        return [pscustomobject]@{ State = 'rejected'; Line = $last; Reason = $Matches[1] }
    }
    if ($last -cmatch '^world_trigger=applied; text=(.*?)(; t_ms=[0-9]+)?$') {
        if ($Matches[1] -cne $Canonical) { return [pscustomobject]@{ State = 'different'; Line = $last; Reason = '' } }
        # ApplyTrigger logs applied first; a goto is then armed or refused.
        if ($Canonical -cmatch '(^| )goto=') {
            foreach ($line in $after) {
                if ($line -cmatch '^world_goto=refused; reason=([a-z0-9_]+)') {
                    return [pscustomobject]@{ State = 'rejected'; Line = $line; Reason = $Matches[1] }
                }
                if ($line.StartsWith('world_goto=armed;', [StringComparison]::Ordinal)) {
                    return [pscustomobject]@{ State = 'applied'; Line = $line; Reason = '' }
                }
            }
            return [pscustomobject]@{ State = 'none'; Line = $null; Reason = '' }
        }
        return [pscustomobject]@{ State = 'applied'; Line = $last; Reason = '' }
    }
    return [pscustomobject]@{ State = 'none'; Line = $null; Reason = '' }
}

Export-ModuleMember -Function *-*
