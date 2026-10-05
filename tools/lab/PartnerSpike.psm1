# Offline partner spike helpers (docs/offline-partner-spike-plan.md 2.1, 2.7, 3).
# Pure functions only (no file, process or window access):
#  - the runtime trigger grammar of <game>/mgs-coop/partner.trigger (2.1);
#  - robust parsing of the native script-observer.log partner lines (2.7);
#  - the experiment PASS/FAIL rules of section 3 (E0, E1, E2, E3, E4, E5, E6, E8)
#    and of the animation / appearance experiments (docs/partner-anim-appearance.md:
#    A0 census, A1 anim arrays, A3 SetStance, M1 xform, N1 network anim, B1 local
#    Quiet look, B4 network looks, C1 guest plays Quiet).
# Used by Set-PartnerTrigger.ps1, Test-PartnerSpike.ps1 and
# Invoke-PartnerExperiment.ps1; unit tested by tests/lab_scripts_tests.ps1.
# A verdict is PASS (every log criterion met), FAIL (a criterion is shown NOT
# met, or a deadline measured on the probe clock t_ms passed) or INCOMPLETE
# (evidence still missing). Screenshot criteria are listed in notes as manual.
# ASCII only.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:PartnerTriggerMaxBytes = 255  # native reads at most 255 bytes (partner_receipt.hpp max_trigger)
$script:PartnerMaxEvidence = 60
# A1/A3 run with hybrid movement (one Warp to place the partner, then the
# native transform without Warp's resets every tick), so Warp's per-tick resets
# (0x9CEC0E..0x9CEC2C) cannot mask or undo the stance; run them after M1.
$script:PartnerA1Trigger = 'stage=3 source=fixed apply=hybrid reset=n anim=mirror aset=q'
$script:PartnerA3Trigger = 'stage=3 source=fixed apply=hybrid reset=n anim=mirror aset=qs'
$script:PartnerExperiments = @('E0', 'E1', 'E2', 'E3', 'E4', 'E5', 'E6', 'E8', 'A0', 'A1', 'A3', 'M1', 'N1', 'B1', 'B4', 'C1', 'L1', 'L2', 'L3', 'L4', 'I0', 'I1', 'I2', 'I3', 'I3x', 'I4', 'I5', 'I6', 'I7', 'D0', 'D1', 'D2', 'D3')

# ---------------------------------------------------------------------------
# Trigger grammar (2.1)
# ---------------------------------------------------------------------------

$script:PartnerTriggerKeys = [ordered]@{
    # key -> allowed values, in canonical order. move= is accepted as the same
    # key as apply= (written back as apply=). census=1 (component census),
    # parts=explicit (one-shot op 16: re-apply the look; with look=copy the local
    # look, variant F3b) and mirror=on (one-shot op 32, mirror diagnostic: F3c;
    # refused natively unless look=copy) are the native parser's one-shots
    # (native/partner_receipt.cpp ParseTrigger); parts/mirror act only at stage >= 1.
    # Round 3 (docs/partner-anim-appearance.md "Round 3"): apply gains input;
    # input/cam/hold/yaw/dm are enums; stick/tap/press/words/bits/correct/isend/
    # p218/padprobe/holdlog/dmplay/dmstop/lookgate/lookrecover are value keys.
    stage = @('0', '1', '2', '3'); source = @('fixed', 'orbit', 'net'); apply = @('warp', 'slot', 'both', 'xform', 'hybrid', 'input')
    anim = @('off', 'mirror', 'net'); aset = @('q', 's', 'qs', 'r'); reset = @('n', 'a', 'c', 'ac'); look = @('copy', 'net', 'quiet', 'snake')
    probe = @('ps', 'off'); census = @('1'); parts = @('explicit'); mirror = @('on')
    input = @('off', 'pass', 'neutral', 'stick', 'net'); cam = @('off', 'detect', 'sandbox'); hold = @('none', 'pass', 'own', 'net')
    yaw = @('rot', 'rotneg', 'none'); dm = @('off', 'probe', 'on'); dmstop = @('1')
    # Round 4 (docs/partner-anim-appearance.md "Round 4"): reconciliation and the
    # wrap census; place=1 is a one-shot (a new placement).
    recon = @('off', 'track', 'pin'); slot = @('remote', 'body', 'none'); runaway = @('pin', 'disarm', 'off')
    wrap = @('off', 'census', 'window'); place = @('1')
}
# Value keys: key -> a scriptblock that returns $true for a valid value.
$script:PartnerTriggerValueKeys = [ordered]@{
    stick = { param($v) if ($v -cnotmatch '^(0|[1-9][0-9]{0,2}):(0|[1-9][0-9]?|100)$') { return $false } $p = $v.Split(':'); [int]$p[0] -le 359 -and [int]$p[1] -le 100 }
    tap = { param($v) $v -cmatch '^0x[0-9a-fA-F]{1,8}$' }
    press = { param($v) if ($v -cnotmatch '^0x[0-9a-fA-F]{1,8}:([1-9][0-9]{1,3})$') { return $false } $ms = [int]$v.Split(':')[1]; $ms -ge 50 -and $ms -le 5000 }
    words = { param($v) $v -cmatch '^[0prhn]{3}$' }
    bits = { param($v) $v -cmatch '^0x[0-9a-fA-F]{1,8}$' }
    correct = { param($v) $v -cmatch '^(0|[1-9][0-9]{0,4})$' -and [int]$v -le 10000 }
    isend = { param($v) $v -ceq '0' -or $v -ceq '1' }
    p218 = { param($v) $v -ceq '0' -or $v -ceq '1' }
    padprobe = { param($v) $v -ceq '0' -or $v -ceq '1' }
    holdlog = { param($v) $v -ceq '0' -or $v -ceq '1' }
    dmplay = { param($v) $v -cmatch '^[0-9]$' }
    lookgate = { param($v) $v -ceq '0' -or $v -ceq '1' }
    lookrecover = { param($v) $v -ceq '0' -or $v -ceq '1' }
    align = { param($v) if ($v -cnotmatch '^(0|-?[1-9][0-9]{0,2})$') { return $false } [int]$v -ge -50 -and [int]$v -le 150 }
}
# Canonical write order (the existing keys, then the round 3 keys).
$script:PartnerTriggerOrder = @('stage', 'source', 'apply', 'anim', 'aset', 'reset', 'look', 'probe', 'input', 'stick', 'cam',
    'hold', 'yaw', 'words', 'bits', 'correct', 'isend', 'p218', 'padprobe', 'holdlog', 'dm', 'lookgate', 'lookrecover',
    'recon', 'align', 'slot', 'runaway', 'wrap',
    'census', 'parts', 'mirror', 'tap', 'press', 'dmplay', 'dmstop', 'place')
$script:PartnerTriggerUsage = 'stage=0..3 source=fixed|orbit|net apply|move=warp|slot|both|xform|hybrid|input anim=off|mirror|net aset=q|s|qs|r reset=n|a|c|ac look=copy|net|quiet|snake probe=ps|off input=off|pass|neutral|stick|net stick=<0..359>:<0..100> cam=off|detect|sandbox hold=none|pass|own|net yaw=rot|rotneg|none words=<3 of 0prhn> bits=0x<hex> correct=<0..10000> isend=0|1 p218=0|1 padprobe=0|1 holdlog=0|1 dm=off|probe|on dmplay=<0..9> dmstop=1 lookgate=0|1 lookrecover=0|1 recon=off|track|pin align=<-50..150> slot=remote|body|none runaway=pin|disarm|off wrap=off|census|window census=1 parts=explicit mirror=on tap=0x<hex> press=0x<hex>:<50..5000> place=1, or stop alone'
$script:PartnerTriggerTokenPattern = '^(stage|source|apply|move|anim|aset|reset|look|probe|census|parts|mirror|input|stick|cam|hold|yaw|words|bits|correct|isend|p218|padprobe|holdlog|dm|dmplay|dmstop|tap|press|lookgate|lookrecover|recon|align|slot|runaway|wrap|place)=(.*)$'

function ConvertTo-PartnerTriggerText {
    # Canonical trigger line (without the newline): 'stop', or any of the keys
    # above (move= is apply=), each at most once, in any order, written back in
    # canonical order. Lower case and case sensitive, like the native byte-wise
    # parser. Throws with the reason for anything else.
    param([AllowNull()][AllowEmptyString()][string]$Text)
    if ($null -eq $Text -or !$Text.Trim()) { throw "Partner trigger text is empty ($script:PartnerTriggerUsage)." }
    $trimmed = $Text.Trim()
    if ($trimmed -match '[^\x20-\x7e\t]') { throw 'Partner trigger text must be printable ASCII on one line.' }
    if ($trimmed -ceq 'stop') { return 'stop' }
    $values = [ordered]@{}
    foreach ($token in ($trimmed -split '[ \t]+')) {
        if ($token -ceq 'stop') { throw "'stop' must be the only token of a partner trigger." }
        if ($token -cnotmatch $script:PartnerTriggerTokenPattern) {
            throw "Unknown partner trigger token '$token' ($script:PartnerTriggerUsage)."
        }
        $key = $Matches[1]
        if ($key -ceq 'move') { $key = 'apply' }
        $value = $Matches[2]
        if ($values.Contains($key)) { throw "Duplicate partner trigger key '$key' (move= and apply= are one key)." }
        if ($script:PartnerTriggerKeys.Contains($key)) {
            if ($value -cnotin $script:PartnerTriggerKeys[$key]) {
                throw "Invalid partner trigger value '$token' (allowed: $key=$($script:PartnerTriggerKeys[$key] -join '|'))."
            }
        } elseif ($script:PartnerTriggerValueKeys.Contains($key)) {
            if (-not (& $script:PartnerTriggerValueKeys[$key] $value)) { throw "Invalid partner trigger value '$token'." }
        } else {
            throw "Unknown partner trigger token '$token' ($script:PartnerTriggerUsage)."
        }
        $values[$key] = $value
    }
    $canonical = (@($script:PartnerTriggerOrder) | Where-Object { $values.Contains($_) } | ForEach-Object { $_ + '=' + $values[$_] }) -join ' '
    if ($canonical.Length + 1 -gt $script:PartnerTriggerMaxBytes) { throw "Partner trigger text is longer than $script:PartnerTriggerMaxBytes bytes." }
    return $canonical
}

function Test-PartnerTriggerText {
    param([AllowNull()][AllowEmptyString()][string]$Text)
    try { $null = ConvertTo-PartnerTriggerText -Text $Text; return $true } catch { return $false }
}

function Get-PartnerTriggerExpectation {
    # What the native 'partner_stage=changed; stage=..; source=..; apply=..;
    # anim=..; aset=..; reset=..; look=..; probe=..; via=trigger' line must show
    # after a canonical trigger: 'stop' -> stage 0; keys the trigger does not
    # name keep their previous values (not checked). The one-shots
    # census/parts/mirror are not part of that line.
    param([Parameter(Mandatory)][string]$Canonical)
    $expect = [ordered]@{}
    if ($Canonical -ceq 'stop') { $expect['stage'] = '0'; return $expect }
    foreach ($token in ($Canonical -split ' ')) {
        $pair = $token.Split('=', 2)
        if ($pair[0] -cin @('stage', 'source', 'apply', 'anim', 'aset', 'reset', 'look', 'probe', 'input', 'cam', 'hold',
                'yaw', 'dm', 'lookgate', 'lookrecover', 'recon', 'align', 'slot', 'runaway', 'wrap')) { $expect[$pair[0]] = $pair[1] }
    }
    return $expect
}

function Find-PartnerTriggerResult {
    # Pure: the native result of the newest consumed trigger in a log text that
    # starts at the offset recorded just before the trigger was written:
    #   State 'applied'   partner_stage=changed (via=trigger) matching -Canonical
    #   State 'different' partner_stage=changed for another command (a replaced
    #                     trigger raced the native poll: the older one was applied)
    #   State 'rejected'  partner_trigger=rejected (the native parser refused it)
    #   State 'none'      neither line yet
    param([AllowNull()][AllowEmptyString()][string]$LogText, [Parameter(Mandatory)][string]$Canonical)
    $last = $null
    foreach ($e in (Get-PartnerLogEntries -Text $LogText)) {
        if ($e.Kind -ceq 'partner_trigger=rejected' -or ($e.Kind -ceq 'partner_stage=changed' -and (Get-PartnerField $e 'via') -ceq 'trigger')) { $last = $e }
    }
    if (!$last) { return [pscustomobject]@{ State = 'none'; Line = $null } }
    if ($last.Kind -ceq 'partner_trigger=rejected') { return [pscustomobject]@{ State = 'rejected'; Line = $last.Line } }
    $expect = Get-PartnerTriggerExpectation -Canonical $Canonical
    foreach ($key in $expect.Keys) {
        if ((Get-PartnerField $last $key) -cne $expect[$key]) { return [pscustomobject]@{ State = 'different'; Line = $last.Line } }
    }
    return [pscustomobject]@{ State = 'applied'; Line = $last.Line }
}

# ---------------------------------------------------------------------------
# Log line parsing (2.7)
# ---------------------------------------------------------------------------

function ConvertFrom-PartnerNumber {
    # '123', '-45', '0x1f' -> Int64; with -Hex also bare hex digits ('ffff').
    # $null when the text is not a number (never throws).
    param([AllowNull()][AllowEmptyString()][string]$Text, [switch]$Hex)
    if (!$Text) { return $null }
    $t = $Text.Trim()
    $digits = $null
    if ($t -cmatch '^0[xX]([0-9A-Fa-f]{1,16})$') { $digits = $Matches[1] }
    elseif ($Hex -and $t -cmatch '^([0-9A-Fa-f]{1,16})$') { $digits = $Matches[1] }
    if ($null -ne $digits) {
        [UInt64]$u = 0
        if (![UInt64]::TryParse($digits, [Globalization.NumberStyles]::AllowHexSpecifier, [Globalization.CultureInfo]::InvariantCulture, [ref]$u)) { return $null }
        if ($u -gt [UInt64][Int64]::MaxValue) { return $null }
        return [Int64]$u
    }
    [Int64]$value = 0
    if ($t -cmatch '^[+-]?[0-9]{1,18}$' -and
        [Int64]::TryParse($t, [Globalization.NumberStyles]::AllowLeadingSign, [Globalization.CultureInfo]::InvariantCulture, [ref]$value)) { return $value }
    return $null
}

function ConvertFrom-PartnerList {
    # 'a,b[,c]' -> Int64[] (each element as ConvertFrom-PartnerNumber); $null when
    # any element is not a number or the count differs from -Count.
    param([AllowNull()][AllowEmptyString()][string]$Text, [switch]$Hex, [int]$Count = 0)
    if (!$Text) { return $null }
    $parts = $Text.Split(',')
    if ($Count -gt 0 -and $parts.Count -ne $Count) { return $null }
    $out = [Int64[]]::new($parts.Count)
    for ($i = 0; $i -lt $parts.Count; $i++) {
        $n = ConvertFrom-PartnerNumber -Text $parts[$i] -Hex:$Hex
        if ($null -eq $n) { return $null }
        $out[$i] = $n
    }
    return , $out
}

function ConvertFrom-PartnerLogLine {
    # One log line -> Kind (text before the first ';'), Prefix/Value (Kind split
    # at its first '='), Fields (every key=value token after the first ';';
    # tokens end at ';' or a blank; the first occurrence of a key wins).
    #   'partner_probe=state; t_ms=5; ids=0000,ffff' -> Kind partner_probe=state, Fields t_ms, ids
    #   'partner_warp_check; R=1; err_mm=3'           -> Kind partner_warp_check
    #   'partner_lua=event=refused;reason=bad_index'  -> Kind partner_lua=event=refused, Fields reason
    param([Parameter(Mandatory)][AllowEmptyString()][string]$Line, [int]$Index = 0)
    $text = $Line.TrimEnd([char]13, [char]10)
    $semi = $text.IndexOf(';')
    $kind = if ($semi -ge 0) { $text.Substring(0, $semi).Trim() } else { $text.Trim() }
    $rest = if ($semi -ge 0) { $text.Substring($semi + 1) } else { '' }
    $eq = $kind.IndexOf('=')
    $prefix = if ($eq -ge 0) { $kind.Substring(0, $eq) } else { $kind }
    $value = if ($eq -ge 0) { $kind.Substring($eq + 1) } else { '' }
    $fields = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
    foreach ($m in [regex]::Matches($rest, '(?:^|[;\s])([A-Za-z][A-Za-z0-9_]*)=([^;\s]*)')) {
        $key = $m.Groups[1].Value
        if (!$fields.ContainsKey($key)) { $fields[$key] = $m.Groups[2].Value }
    }
    [pscustomobject]@{ Index = $Index; Line = $text; Kind = $kind; Prefix = $prefix; Value = $value; Fields = $fields }
}

function Get-PartnerLogEntries {
    # Every non-empty line of a log text, parsed, with its 0-based line index.
    param([AllowNull()][AllowEmptyString()][string]$Text)
    $list = [Collections.Generic.List[object]]::new()
    if (!$Text) { return , $list.ToArray() }
    $lines = $Text -split "`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i].TrimEnd([char]13)
        if (!$line.Trim()) { continue }
        $list.Add((ConvertFrom-PartnerLogLine -Line $line -Index $i))
    }
    return , $list.ToArray()
}

function Get-PartnerField {
    param([Parameter(Mandatory)]$Entry, [Parameter(Mandatory)][string]$Name)
    if ($Entry.Fields.ContainsKey($Name)) { return $Entry.Fields[$Name] }
    return $null
}

function Get-PartnerFieldNumber {
    param([Parameter(Mandatory)]$Entry, [Parameter(Mandatory)][string]$Name, [switch]$Hex)
    return ConvertFrom-PartnerNumber -Text (Get-PartnerField -Entry $Entry -Name $Name) -Hex:$Hex
}

function Get-PartnerFieldList {
    param([Parameter(Mandatory)]$Entry, [Parameter(Mandatory)][string]$Name, [switch]$Hex, [int]$Count = 0)
    return ConvertFrom-PartnerList -Text (Get-PartnerField -Entry $Entry -Name $Name) -Hex:$Hex -Count $Count
}

# Typed probe fields: property, log key, bare hex, list length (0 = single number).
$script:PartnerProbeFields = @(
    @('Ids', 'ids', $true, 2), @('Pool', 'pool', $true, 2), @('Parts', 'parts', $false, 2), @('RBits', 'rbits', $false, 0),
    @('S0', 's0', $false, 0), @('S8', 's8', $false, 0), @('Q170', 'q170', $false, 0), @('Q180', 'q180', $false, 0),
    @('Q194', 'q194', $false, 0), @('Q198', 'q198', $false, 0), @('Glob', 'glob', $false, 0), @('PbGuard', 'pb_guard', $false, 0),
    @('PbFlag', 'pb_flag', $false, 2), @('PbState', 'pb_state', $false, 2), @('Pos0', 'pos0_mm', $false, 3), @('Yaw0', 'yaw0_mrad', $false, 0),
    @('Pos1', 'pos1_mm', $false, 3), @('Yaw1', 'yaw1_mrad', $false, 0), @('Rec1', 'rec1_mm', $false, 3), @('Rec1Status', 'rec1_status', $false, 0)
)

function New-PartnerProbe {
    # partner_probe=state line -> probe with Index, Line, TMs and Stage; the other
    # typed fields stay $null until Expand-PartnerProbe (parsing every field of
    # every probe of a long run is the slow part of a verdict).
    param([Parameter(Mandatory)]$Entry)
    $probe = [ordered]@{ Index = $Entry.Index; Line = $Entry.Line; Entry = $Entry; Expanded = $false
        TMs = (Get-PartnerFieldNumber $Entry 't_ms'); Stage = (Get-PartnerFieldNumber $Entry 'stage') }
    foreach ($spec in $script:PartnerProbeFields) { $probe[$spec[0]] = $null }
    return [pscustomobject]$probe
}

function Expand-PartnerProbe {
    # Fills a probe's typed fields once ($null for a missing or malformed field).
    param([Parameter(Mandatory)]$Probe)
    if ($Probe.Expanded) { return $Probe }
    $fields = $Probe.Entry.Fields
    $text = $null
    foreach ($spec in $script:PartnerProbeFields) {
        if (!$fields.TryGetValue($spec[1], [ref]$text)) { continue }
        if ($spec[3]) { $Probe.($spec[0]) = ConvertFrom-PartnerList -Text $text -Hex:$spec[2] -Count $spec[3] }
        else { $Probe.($spec[0]) = ConvertFrom-PartnerNumber -Text $text -Hex:$spec[2] }
    }
    $Probe.Expanded = $true
    return $Probe
}

function ConvertTo-PartnerProbe {
    # partner_probe=state line -> typed fields ($null for a missing or malformed field).
    param([Parameter(Mandatory)]$Entry)
    return Expand-PartnerProbe -Probe (New-PartnerProbe -Entry $Entry)
}

function Test-PartnerProbeRealized {
    # Realized iff ids[1] != 0xFFFF and pool[1] != 0xFF (the 0x9CD160 guard).
    param($Probe)
    if ($null -eq $Probe -or $null -eq $Probe.Ids -or $null -eq $Probe.Pool) { return $false }
    return $Probe.Ids[1] -ne 0xffff -and $Probe.Pool[1] -ne 0xff
}

function Test-PartnerVectorAvailable {
    # A readable 3-vector in mm. Native writes INT32_MIN (-2147483648) for each
    # component it could not read (partner_units mem_pos_mm while the vars block
    # is unresolved; any non-finite float converted by ToMm); a magnitude of
    # 2^31-1 or more is treated the same (no real position is 2000 km away).
    param($Vector)
    if ($null -eq $Vector -or $Vector.Count -ne 3) { return $false }
    foreach ($c in $Vector) { if ([Math]::Abs([double]$c) -ge 2147483647) { return $false } }
    return $true
}

function Get-PartnerDistance {
    # Euclidean distance of two 3-vectors (mm); $null when either is missing or
    # unavailable (sentinel components, see Test-PartnerVectorAvailable).
    param($A, $B)
    if (!(Test-PartnerVectorAvailable $A) -or !(Test-PartnerVectorAvailable $B)) { return $null }
    $dx = [double]$A[0] - [double]$B[0]; $dy = [double]$A[1] - [double]$B[1]; $dz = [double]$A[2] - [double]$B[2]
    return [Math]::Sqrt($dx * $dx + $dy * $dy + $dz * $dz)
}

function Get-PartnerSpread {
    # Largest pairwise distance among 3-vectors (mm); 0 for fewer than two.
    param([object[]]$Vectors = @())
    $list = @($Vectors | Where-Object { $null -ne $_ })
    $best = 0.0
    for ($i = 0; $i -lt $list.Count; $i++) {
        for ($j = $i + 1; $j -lt $list.Count; $j++) {
            $d = Get-PartnerDistance $list[$i] $list[$j]
            if ($null -ne $d -and $d -gt $best) { $best = $d }
        }
    }
    return $best
}

function Format-PartnerVector {
    param($Vector)
    if ($null -eq $Vector) { return 'none' }
    return ($Vector -join ',')
}

function New-PartnerContext {
    # Parsed view of one instance's script-observer.log text (this run only).
    param([AllowNull()][AllowEmptyString()][string]$ObserverLog)
    $entries = Get-PartnerLogEntries -Text $ObserverLog
    $probes = [Collections.Generic.List[object]]::new()
    foreach ($e in $entries) { if ($e.Kind -ceq 'partner_probe=state') { $probes.Add((New-PartnerProbe -Entry $e)) } }
    $last = if ($entries.Count) { $entries[$entries.Count - 1].Index } else { -1 }
    [pscustomobject]@{ Entries = $entries; Probes = $probes.ToArray(); Timeline = (Get-PartnerTimeline -Entries $entries); LastIndex = $last }
}

function Select-PartnerEntry {
    # Entries whose Kind equals -Kind (or whose Prefix equals -Prefix) with
    # From < Index < To.
    param([Parameter(Mandatory)]$Context, [string]$Kind, [string]$Prefix, [int]$From = -1, [int]$To = [int]::MaxValue)
    $rows = foreach ($e in $Context.Entries) {
        if ($e.Index -le $From -or $e.Index -ge $To) { continue }
        if ($Kind -and $e.Kind -cne $Kind) { continue }
        if ($Prefix -and $e.Prefix -cne $Prefix) { continue }
        $e
    }
    return @($rows)
}

function Select-PartnerProbe {
    # Probes with From < Index < To, expanded unless -NoExpand (TMs and Stage only).
    param([Parameter(Mandatory)]$Context, [int]$From = -1, [int]$To = [int]::MaxValue, [switch]$NoExpand)
    $rows = foreach ($p in $Context.Probes) {
        if ($p.Index -le $From -or $p.Index -ge $To) { continue }
        if ($NoExpand) { $p } else { Expand-PartnerProbe -Probe $p }
    }
    return @($rows)
}

function Get-PartnerProbeSpanMs {
    # Probe clock (t_ms) covered by probes: last minus first; 0 with fewer than two.
    # An empty selection arrives as $null (one null item in the pipeline): skipped.
    param([object[]]$Probes = @())
    $times = @($Probes | Where-Object { $null -ne $_ -and $null -ne $_.TMs } | ForEach-Object { [Int64]$_.TMs })
    if ($times.Count -lt 2) { return [Int64]0 }
    return [Int64]($times[$times.Count - 1] - $times[0])
}

# ---------------------------------------------------------------------------
# Configuration timeline and experiment segments
# ---------------------------------------------------------------------------

function Get-PartnerTimeline {
    # The partner configuration after each line that changes it:
    #   partner=configured; stage=..; source=..; apply=..   (boot)
    #   partner_stage=changed; stage=..; source=..; apply=.. (trigger)
    #   partner_bits=cleared; reason=trigger while stage>=3  ('stop' trigger)
    #   partner_stop (mission STOP receipt) / partner_cfg=ready (re-entry)
    param([object[]]$Entries = @())
    $stage = $null; $source = $null; $apply = $null; $stopped = $false
    # Animation / appearance settings (older logs without them: off/qs/n/copy).
    $anim = 'off'; $aset = 'qs'; $reset = 'n'; $look = 'copy'
    $rows = [Collections.Generic.List[object]]::new()
    foreach ($e in $Entries) {
        $changed = $false
        if ($e.Kind -ceq 'partner=configured' -or $e.Kind -ceq 'partner_stage=changed') {
            $s = Get-PartnerFieldNumber $e 'stage'
            if ($null -ne $s) { $stage = $s }
            $v = Get-PartnerField $e 'source'; if ($v) { $source = $v }
            $v = Get-PartnerField $e 'apply'; if ($v) { $apply = $v }
            $v = Get-PartnerField $e 'anim'; if ($v) { $anim = $v }
            $v = Get-PartnerField $e 'aset'; if ($v) { $aset = $v }
            $v = Get-PartnerField $e 'reset'; if ($v) { $reset = $v }
            $v = Get-PartnerField $e 'look'; if ($v) { $look = $v }
            $stopped = $false
            $changed = $true
        } elseif ($e.Kind -ceq 'partner_bits=cleared' -and (Get-PartnerField $e 'reason') -ceq 'trigger' -and $null -ne $stage -and $stage -ge 3) {
            $stopped = $true; $changed = $true
        } elseif ($e.Kind -ceq 'partner_stop') {
            $stopped = $true; $changed = $true
        } elseif ($e.Kind -ceq 'partner_cfg=ready' -and $stopped) {
            $stopped = $false; $changed = $true
        }
        if ($changed) {
            $rows.Add([pscustomobject]@{ Index = $e.Index; Stage = $stage; Source = $source; Apply = $apply; Anim = $anim; Aset = $aset
                    Reset = $reset; Look = $look; Stopped = $stopped; Line = $e.Line })
        }
    }
    return , $rows.ToArray()
}

function Get-PartnerSegment {
    # The LAST contiguous range of lines in which $Predicate (called with a
    # timeline row) holds and the partner is not stopped. Start is the index of
    # the line that made it hold; End is the line that ended it (exclusive) or
    # LastIndex + 1. $null when the predicate never held.
    param([Parameter(Mandatory)]$Context, [Parameter(Mandatory)][scriptblock]$Predicate)
    $segment = $null
    $open = $null
    foreach ($row in $Context.Timeline) {
        $holds = !$row.Stopped -and [bool](& $Predicate $row)
        if ($holds -and $null -eq $open) { $open = $row }
        elseif (!$holds -and $null -ne $open) {
            $segment = [pscustomobject]@{ Start = $open.Index; End = $row.Index; Line = $open.Line; State = $open; EndLine = $row.Line }
            $open = $null
        }
    }
    if ($null -ne $open) { $segment = [pscustomobject]@{ Start = $open.Index; End = $Context.LastIndex + 1; Line = $open.Line; State = $open; EndLine = $null } }
    return $segment
}

# ---------------------------------------------------------------------------
# Verdict bookkeeping
# ---------------------------------------------------------------------------

function New-PartnerCheck {
    param([string]$Experiment, [string]$Role)
    [pscustomobject]@{
        Experiment = $Experiment; Role = $Role
        Matched = [Collections.Generic.List[string]]::new(); Missing = [Collections.Generic.List[string]]::new()
        Failed = [Collections.Generic.List[string]]::new(); Notes = [Collections.Generic.List[string]]::new()
        Data = [ordered]@{}
    }
}

function Add-PartnerMatch {
    param([Parameter(Mandatory)]$Check, [AllowNull()][AllowEmptyString()][string]$Line)
    if ($Line -and !$Check.Matched.Contains($Line) -and $Check.Matched.Count -lt $script:PartnerMaxEvidence) { $Check.Matched.Add($Line) }
}

function Add-PartnerItem {
    param([Parameter(Mandatory)]$List, [Parameter(Mandatory)][string]$Text)
    if (!$List.Contains($Text)) { $List.Add($Text) }
}

function Get-PartnerVerdictName {
    param([Parameter(Mandatory)]$Check)
    if ($Check.Failed.Count) { return 'FAIL' }
    if ($Check.Missing.Count) { return 'INCOMPLETE' }
    return 'PASS'
}

function Complete-PartnerCheck {
    param([Parameter(Mandatory)]$Check)
    [pscustomobject][ordered]@{
        experiment = $Check.Experiment; role = $Check.Role; verdict = (Get-PartnerVerdictName -Check $Check)
        matched = $Check.Matched.ToArray(); missing = $Check.Missing.ToArray(); failed = $Check.Failed.ToArray()
        notes = $Check.Notes.ToArray(); data = $Check.Data
    }
}

function Test-PartnerMissionBoundary {
    # Lines after which native has no partner_cfg=ready state for the mission:
    # mission_boundary=runtime_enter (PartnerMissionStart resets it),
    # mission_boundary=termination_returned and partner_stop.
    param([Parameter(Mandatory)]$Entry)
    return $Entry.Kind -ceq 'mission_boundary=runtime_enter' -or $Entry.Kind -ceq 'mission_boundary=termination_returned' -or $Entry.Kind -ceq 'partner_stop'
}

function Get-PartnerCfgRefusal {
    # Classifies one partner_cfg=refused line (native/game_script_observer.cpp):
    #   Kind 'tick': logged by the per-tick ReadState after this mission's
    #     partner_cfg=ready (change-only); the tick pauses and logs
    #     partner_cfg=ready again once the state is valid.
    #   Kind 'cfg': the mission-start CFG receipt refused; partner_sync.lua
    #     stays inactive until the next mission start.
    # Recovered: the first later partner_cfg=ready line, or $null.
    param([Parameter(Mandatory)]$Context, [Parameter(Mandatory)]$Entry)
    $ready = $false
    foreach ($e in $Context.Entries) {
        if ($e.Index -ge $Entry.Index) { break }
        if ($e.Kind -ceq 'partner_cfg=ready') { $ready = $true }
        elseif (Test-PartnerMissionBoundary -Entry $e) { $ready = $false }
    }
    $later = @(Select-PartnerEntry -Context $Context -Kind 'partner_cfg=ready' -From $Entry.Index | Select-Object -First 1)
    [pscustomobject]@{ Kind = $(if ($ready) { 'tick' } else { 'cfg' }); Recovered = $(if ($later.Count) { $later[0] } else { $null }) }
}

function Add-PartnerCommonChecks {
    # Rules every experiment shares, within (From, To): no crash_probe=av, no Lua
    # error or index refusal, the process still alive, and partner_cfg=refused:
    # reason=online_bit/sync_present always FAIL; a mission-start (CFG) refusal
    # FAILs until a later mission start logs partner_cfg=ready; a per-tick
    # refusal is a note once partner_cfg=ready follows, INCOMPLETE until then.
    param([Parameter(Mandatory)]$Check, [Parameter(Mandatory)]$Context, [int]$From = -1, [int]$To = [int]::MaxValue, $Alive = $null)
    foreach ($e in (Select-PartnerEntry -Context $Context -Kind 'crash_probe=av' -From $From -To $To)) {
        Add-PartnerMatch $Check $e.Line
        Add-PartnerItem $Check.Failed ('crash_probe=av: access violation in the game image at rva=' + [string](Get-PartnerField $e 'rva') + ' (collect the RVAs; F3d)')
    }
    $before = @(Select-PartnerEntry -Context $Context -Kind 'crash_probe=av' -To ([Math]::Max($From + 1, 0))).Count
    if ($before) { Add-PartnerItem $Check.Notes "$before crash_probe=av line(s) before this experiment's segment (each RVA is logged once per process)" }
    foreach ($e in (Select-PartnerEntry -Context $Context -Kind 'partner_lua=event=error' -From $From -To $To)) {
        Add-PartnerMatch $Check $e.Line
        Add-PartnerItem $Check.Failed 'partner_lua=event=error: the Lua side stopped ticking (fix the call shape; pos must be a plain table)'
    }
    foreach ($e in (Select-PartnerEntry -Context $Context -Kind 'partner_lua=event=refused' -From $From -To $To)) {
        Add-PartnerMatch $Check $e.Line
        Add-PartnerItem $Check.Failed 'partner_lua=event=refused: Lua refused the instance index'
    }
    foreach ($e in (Select-PartnerEntry -Context $Context -Kind 'partner_cfg=refused' -From $From -To $To)) {
        Add-PartnerMatch $Check $e.Line
        $reason = [string](Get-PartnerField $e 'reason')
        if ($reason -cin @('online_bit', 'sync_present')) {
            Add-PartnerItem $Check.Failed "partner_cfg=refused; reason=$reason`: S_IS_ONLINE or the synchronizer appeared: STOP and audit the host_session_trial path"
            continue
        }
        $refusal = Get-PartnerCfgRefusal -Context $Context -Entry $e
        if ($refusal.Kind -ceq 'tick') {
            if ($refusal.Recovered) { Add-PartnerItem $Check.Notes "transient partner_cfg=refused; reason=$reason (the partner tick paused until: $($refusal.Recovered.Line))" }
            else { Add-PartnerItem $Check.Missing "partner_cfg=ready after partner_cfg=refused; reason=$reason (the partner tick pauses while the player state is refused)" }
        } elseif ($refusal.Recovered) {
            Add-PartnerItem $Check.Notes "partner_cfg=refused; reason=$reason at a mission start (CFG); a later mission start logged: $($refusal.Recovered.Line)"
        } else {
            Add-PartnerItem $Check.Failed "partner_cfg=refused; reason=$reason at the mission start (CFG receipt): partner_sync.lua stays inactive for this mission"
        }
    }
    if ($Alive -is [bool]) {
        if (!$Alive) { Add-PartnerItem $Check.Failed 'process_exited: the instance is no longer running' }
    } else { Add-PartnerItem $Check.Notes 'process liveness not checked' }
}

function Add-PartnerCfgReady {
    # partner_cfg=ready before $Before (required by every segment experiment).
    param([Parameter(Mandatory)]$Check, [Parameter(Mandatory)]$Context, [int]$Before = [int]::MaxValue)
    $ready = @(Select-PartnerEntry -Context $Context -Kind 'partner_cfg=ready' -To $Before)
    if ($ready.Count) { Add-PartnerMatch $Check $ready[$ready.Count - 1].Line } else { Add-PartnerItem $Check.Missing 'partner_cfg=ready' }
}

function Get-PartnerSegmentOrMissing {
    param([Parameter(Mandatory)]$Check, [Parameter(Mandatory)]$Context, [Parameter(Mandatory)][scriptblock]$Predicate, [Parameter(Mandatory)][string]$How)
    $segment = Get-PartnerSegment -Context $Context -Predicate $Predicate
    if (!$segment) { Add-PartnerItem $Check.Missing $How; return $null }
    Add-PartnerMatch $Check $segment.Line
    $Check.Data['segment_start_line'] = $segment.Start
    $Check.Data['segment_end_line'] = $(if ($null -ne $segment.EndLine) { $segment.End } else { $null })
    if ($segment.EndLine) { Add-PartnerItem $Check.Notes ('segment ended by: ' + $segment.EndLine) }
    return $segment
}

function Get-PartnerMsAfterEvent {
    # Probe clock elapsed after an event line: newest probe t_ms minus the
    # t_ms of the first probe logged after the event (0 when unknown).
    param([Parameter(Mandatory)]$Context, [int]$EventIndex, [int]$To = [int]::MaxValue)
    return Get-PartnerProbeSpanMs -Probes (Select-PartnerProbe -Context $Context -From $EventIndex -To $To -NoExpand)
}

# ---------------------------------------------------------------------------
# Experiments (section 3)
# ---------------------------------------------------------------------------

function Invoke-PartnerE0 {
    # E0: offline boot and baseline at stage 0 (both instances).
    param($Check, $Context, [string]$EarlyLog, [string]$SteamLog, $Safety, [string]$ExpectedMasterUrl, [bool]$OfflineRun, [bool]$HostOnly)
    if (!$OfflineRun) { Add-PartnerItem $Check.Failed 'run_not_offline: this run was not started with Start-Lab.ps1 -Offline (run.json offline=true)' }
    if ($ExpectedMasterUrl -and $ExpectedMasterUrl -cnotmatch '^http://127\.0\.0\.1:[0-9]{4,5}/tppstm/gate$') {
        Add-PartnerItem $Check.Failed "expected master URL is not a closed local port: $ExpectedMasterUrl"
    }
    $master = [regex]::Match(([string]$SteamLog + "`n" + [string]$EarlyLog), '(?m)^master_server=redirected(?=[ ;\r\n]|$)[^\r\n]*')
    if ($master.Success) { Add-PartnerMatch $Check $master.Value }
    if ($null -eq $Safety) { Add-PartnerItem $Check.Missing 'safety evidence (early.log / steam-observer.log)' }
    elseif ($Safety.State -eq 'unsafe') { Add-PartnerItem $Check.Failed ('unsafe_' + $Safety.Reason + ': the safety gate rules are not in force') }
    elseif ($Safety.State -ne 'ok') { foreach ($m in @($Safety.Missing)) { if ($m) { Add-PartnerItem $Check.Missing "safety:$m" } } }

    $mode = @(Select-PartnerEntry -Context $Context -Kind 'mode=offline_v1')
    if ($mode.Count) {
        Add-PartnerMatch $Check $mode[0].Line
        if ((Get-PartnerField $mode[0] 'partner') -ceq '0') { Add-PartnerItem $Check.Failed 'mode=offline_v1 reports partner=0 (lab.ini partner=1 not applied)' }
        $online = Get-PartnerField $mode[0] 's_is_online'
        if ($online -and $online -cne 'never') { Add-PartnerItem $Check.Failed "mode=offline_v1 reports s_is_online=$online" }
    } else {
        Add-PartnerItem $Check.Missing 'mode=offline_v1'
        $other = @(Select-PartnerEntry -Context $Context -Prefix 'mode')
        if ($other.Count) { Add-PartnerItem $Check.Notes ('observer mode line: ' + $other[0].Line) }
    }
    $create = @(Select-PartnerEntry -Context $Context -Kind 'host_create=skipped')
    if ($create.Count) { Add-PartnerMatch $Check $create[0].Line } else { Add-PartnerItem $Check.Missing 'host_create=skipped' }

    foreach ($e in (Select-PartnerEntry -Context $Context -Kind 'partner_net=disabled')) {
        Add-PartnerMatch $Check $e.Line
        Add-PartnerItem $Check.Failed ('partner_net=disabled; reason=' + [string](Get-PartnerField $e 'reason') + ' (check the bind reason and port collisions)')
    }
    $listening = @(Select-PartnerEntry -Context $Context -Kind 'partner_net=listening')
    if ($listening.Count) { Add-PartnerMatch $Check $listening[0].Line } else { Add-PartnerItem $Check.Missing 'partner_net=listening' }

    $ready = @(Select-PartnerEntry -Context $Context -Kind 'partner_cfg=ready')
    if ($ready.Count) {
        Add-PartnerMatch $Check $ready[0].Line
        $first = $ready[0]
        $expect = [ordered]@{ count = '2'; local = '0'; R = '1'; online_bit = '0'; sync = 'null' }
        foreach ($key in $expect.Keys) {
            $actual = Get-PartnerField $first $key
            if ($actual -cne $expect[$key]) {
                $why = switch ($key) {
                    'count' { 'the two-slot package was not selected in offline-v1 (check mode=players_v1 and the PACK receipts; try host-v1)' }
                    'online_bit' { 'S_IS_ONLINE got set: STOP and audit the host_session_trial path' }
                    'sync' { 'the synchronizer P+0xA8 exists: STOP and audit the host_session_trial path' }
                    default { 'unexpected player layout' }
                }
                Add-PartnerItem $Check.Failed "partner_cfg=ready $key=$actual (expected $($expect[$key])): $why"
            }
        }
        foreach ($later in @($ready | Select-Object -Skip 1)) {
            if ((Get-PartnerField $later 'online_bit') -cne '0' -or (Get-PartnerField $later 'sync') -cne 'null') {
                Add-PartnerMatch $Check $later.Line
                Add-PartnerItem $Check.Failed 'a later partner_cfg=ready shows online_bit=1 or sync=set: STOP'
            }
        }
        $probe = @(Select-PartnerProbe -Context $Context -From $first.Index -NoExpand | Where-Object { $_.Stage -eq 0 } | Select-Object -First 1 | ForEach-Object { Expand-PartnerProbe -Probe $_ })
        if ($probe.Count) {
            $p = $probe[0]
            Add-PartnerMatch $Check $p.Line
            if ($null -eq $p.Ids -or $null -eq $p.Pool) { Add-PartnerItem $Check.Failed 'first probe: ids/pool fields malformed' }
            elseif ($p.Ids[0] -eq 0xffff -or $p.Pool[0] -eq 0xff -or $p.Ids[1] -ne 0xffff -or $p.Pool[1] -ne 0xff) {
                $raw = ConvertFrom-PartnerLogLine -Line $p.Line
                Add-PartnerItem $Check.Failed ('first probe at stage 0: ids=' + (Get-PartnerField $raw 'ids') + ' pool=' + (Get-PartnerField $raw 'pool') + ' (expected ids=xxxx,ffff pool=xx,ff: instance 0 realized, instance 1 not)')
            }
            if ($null -ne $p.S0) { Add-PartnerItem $Check.Notes ('first probe s0=0x{0:x2} (0x01 expected [S])' -f $p.S0) }
        } else { Add-PartnerItem $Check.Missing 'partner_probe=state at stage 0 after partner_cfg=ready' }
    } else { Add-PartnerItem $Check.Missing 'partner_cfg=ready' }

    if ($HostOnly) {
        # E0 passes only on both instances (first_rx and tx/rx > 0 on both).
        Add-PartnerItem $Check.Missing 'the guest instance: E0 PASS needs both instances and partner_net receive evidence (this run is host-only; start it without -HostOnly)'
    } else {
        $rx = @(Select-PartnerEntry -Context $Context -Kind 'partner_net=first_rx')
        if ($rx.Count) { Add-PartnerMatch $Check $rx[0].Line } else { Add-PartnerItem $Check.Missing 'partner_net=first_rx' }
        $stats = @(Select-PartnerEntry -Context $Context -Kind 'partner_net=stats' | Where-Object {
                $tx = Get-PartnerFieldNumber $_ 'tx'; $r = Get-PartnerFieldNumber $_ 'rx'
                $null -ne $tx -and $null -ne $r -and $tx -gt 0 -and $r -gt 0 })
        if ($stats.Count) { Add-PartnerMatch $Check $stats[0].Line } else { Add-PartnerItem $Check.Missing 'partner_net=stats with tx>0 and rx>0' }
    }

    $rates = @(Select-PartnerEntry -Context $Context -Kind 'partner_tick=rate')
    if ($rates.Count) {
        Add-PartnerMatch $Check $rates[0].Line
        $unfocused = @($rates | Where-Object { (Get-PartnerField $_ 'focused') -ceq '0' })
        if ($unfocused.Count) {
            $slow = @($unfocused | Where-Object { $hz = Get-PartnerFieldNumber $_ 'hz'; $null -eq $hz -or $hz -lt 10 })
            foreach ($s in $slow) { Add-PartnerMatch $Check $s.Line }
            if ($slow.Count) { Add-PartnerItem $Check.Failed ('unfocused partner_tick=rate below 10 Hz: ' + $slow[0].Line) }
            else { Add-PartnerMatch $Check $unfocused[0].Line }
            $min = ($unfocused | ForEach-Object { Get-PartnerFieldNumber $_ 'hz' } | Where-Object { $null -ne $_ } | Measure-Object -Minimum).Minimum
            $Check.Data['unfocused_hz_min'] = $(if ($null -ne $min) { [Int64]$min } else { $null })
        } else { Add-PartnerItem $Check.Notes 'no unfocused (focused=0) partner_tick=rate line on this instance yet' }
    } else { Add-PartnerItem $Check.Missing 'partner_tick=rate' }

    $units = @(Select-PartnerEntry -Context $Context -Kind 'partner_units')
    $walk = "Send-LabInput.ps1 -Role $($Check.Role) -Keys w+a:6000"
    if ($units.Count) {
        $lua = @($units | ForEach-Object { Get-PartnerFieldList $_ 'lua_pos_mm' -Count 3 })
        $mem = @($units | ForEach-Object { Get-PartnerFieldList $_ 'mem_pos_mm' -Count 3 })
        # mem_pos_mm=INT32_MIN,.. means the vars block was not readable (not a units or freshness problem).
        $memReadable = @($mem | Where-Object { Test-PartnerVectorAvailable $_ }).Count
        $memUnreadable = $units.Count - $memReadable
        $Check.Data['units_mem_unreadable'] = $memUnreadable
        if ($memUnreadable -and $memReadable) { Add-PartnerItem $Check.Notes "$memUnreadable partner_units line(s) with mem_pos_mm unavailable (INT32_MIN: the vars block was not readable at that moment)" }
        if (!$memReadable) {
            $first = @($units | Select-Object -First 1)
            Add-PartnerMatch $Check $first[0].Line
            if ($units.Count -ge 2) { Add-PartnerItem $Check.Failed 'partner_units: mem_pos_mm unreadable in every line (INT32_MIN sentinel: the vars block at *(B+0x2C16920) did not resolve), so lua_pos cannot be compared with vars.playerPos*' }
            else { Add-PartnerItem $Check.Missing 'partner_units with a readable mem_pos_mm (the first line had the INT32_MIN sentinel)' }
        }
        $yaw = @($units | ForEach-Object { Get-PartnerFieldNumber $_ 'yaw_abs_max_cdeg' } | Where-Object { $null -ne $_ })
        $yawMax = if ($yaw.Count) { [Int64]($yaw | Measure-Object -Maximum).Maximum } else { $null }
        $moved = Get-PartnerSpread -Vectors $lua
        $Check.Data['yaw_abs_max_cdeg'] = $yawMax
        $Check.Data['units_lua_spread_mm'] = [Math]::Round($moved)
        $worst = 0.0
        $bad = $null
        foreach ($u in $units) {
            $d = Get-PartnerDistance (Get-PartnerFieldList $u 'lua_pos_mm' -Count 3) (Get-PartnerFieldList $u 'mem_pos_mm' -Count 3)
            if ($null -eq $d) { continue }
            if ($d -gt $worst) { $worst = $d }
            if ($d -gt 50 -and $null -eq $bad) { $bad = $u }
        }
        $Check.Data['units_lua_mem_max_mm'] = [Math]::Round($worst)
        if ($bad) { Add-PartnerMatch $Check $bad.Line; Add-PartnerItem $Check.Failed ('partner_units: lua_pos and mem_pos differ by more than 50 mm (vars.playerPos* not fresh): ' + $bad.Line) }
        $memSpread = Get-PartnerSpread -Vectors $mem
        if ($null -ne $yawMax -and $yawMax -gt 628) {
            $line = @($units | Where-Object { (Get-PartnerFieldNumber $_ 'yaw_abs_max_cdeg') -eq $yawMax } | Select-Object -First 1)
            Add-PartnerMatch $Check $line[0].Line
        } elseif ($moved -ge 2000) {
            Add-PartnerItem $Check.Failed "partner_units: yaw_abs_max_cdeg=$yawMax <= 628 after walking (rotY is radians: change the Lua multiplier and the native conversion)"
        } else {
            Add-PartnerItem $Check.Missing "partner_units yaw_abs_max_cdeg>628 (walk a circle: $walk, then wait for the next 30 s units line)"
        }
        if ($moved -lt 500 -or ($memReadable -and $memSpread -lt 500)) { Add-PartnerItem $Check.Missing "partner_units with changing lua_pos_mm and mem_pos_mm (walk: $walk, then wait for the next 30 s units line)" }
    } else { Add-PartnerItem $Check.Missing 'partner_units' }

    $installed = @(Select-PartnerEntry -Context $Context -Kind 'crash_probe=installed')
    if ($installed.Count) { Add-PartnerMatch $Check $installed[0].Line } else { Add-PartnerItem $Check.Notes 'no crash_probe=installed line (crash_probe=0, or the probe did not install)' }
    $span = Get-PartnerProbeSpanMs -Probes $Context.Probes
    $Check.Data['probe_span_s'] = [Math]::Round($span / 1000.0, 1)
    if ($span -lt 300000) { Add-PartnerItem $Check.Missing ('5 minutes of probes (t_ms span {0:F0} s of 300 s)' -f ($span / 1000.0)) }
}

function Add-PartnerWarpChecks {
    # partner_warp_check lines of a segment: count, err_mm statistics, slot spread.
    param($Check, $Context, $Segment, [string]$Tag)
    $rows = @(Select-PartnerEntry -Context $Context -Kind 'partner_warp_check' -From $Segment.Start -To $Segment.End)
    $errs = @($rows | ForEach-Object { Get-PartnerFieldNumber $_ 'err_mm' })
    $slots = @($rows | ForEach-Object { Get-PartnerFieldList $_ 'slot_mm' -Count 3 })
    $result = [pscustomobject]@{
        Rows = $rows; Count = $rows.Count
        ErrMax = $(if ($errs.Count -and @($errs | Where-Object { $null -eq $_ }).Count -eq 0) { [Int64]($errs | Measure-Object -Maximum).Maximum } else { $null })
        Malformed = @($errs | Where-Object { $null -eq $_ }).Count
        Spread = (Get-PartnerSpread -Vectors $slots)
    }
    if ($Tag) {
        $Check.Data["${Tag}warp_checks"] = $result.Count
        $Check.Data["${Tag}err_mm_max"] = $result.ErrMax
        $Check.Data["${Tag}slot_spread_mm"] = [Math]::Round($result.Spread)
    }
    return $result
}

function Invoke-PartnerE1 {
    # E1: does a Lua Warp write instance 1's position slot while it is unrealized?
    param($Check, $Context, $Alive)
    $segment = Get-PartnerSegmentOrMissing $Check $Context { param($s) $s.Stage -eq 1 -and $s.Apply -ceq 'warp' -and $s.Source -cin @('fixed', 'orbit') } `
        "stage=1 apply=warp (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text 'stage=1 source=fixed apply=warp')"
    Add-PartnerCfgReady $Check $Context $(if ($segment) { $segment.End } else { [int]::MaxValue })
    if (!$segment) { Add-PartnerCommonChecks $Check $Context -Alive $Alive; return }
    Add-PartnerCommonChecks $Check $Context -From $segment.Start -To $segment.End -Alive $Alive
    $probes = @(Select-PartnerProbe -Context $Context -From $segment.Start -To $segment.End)
    $elapsed = Get-PartnerProbeSpanMs -Probes $probes
    $warp = Add-PartnerWarpChecks $Check $Context $segment ''
    $Check.Data['warp_checks'] = $warp.Count
    if ($warp.Count -eq 0) {
        if ($elapsed -ge 5000) { Add-PartnerItem $Check.Failed 'no partner_warp_check within 5 s of stage=1 (WARP not applied)' }
        else { Add-PartnerItem $Check.Missing 'partner_warp_check' }
    } else {
        $early = @($warp.Rows | Select-Object -First 2)
        $good = @($early | Where-Object { $e = Get-PartnerFieldNumber $_ 'err_mm'; $null -ne $e -and $e -le 5 })
        if ($good.Count) { Add-PartnerMatch $Check $good[0].Line }
        else {
            Add-PartnerMatch $Check $early[0].Line
            Add-PartnerItem $Check.Failed ('partner_warp_check err_mm>5 in the first 2 s (' + $early[0].Line + '): the Warp did not reach instance 1''s slot; continue with apply=slot for E3 and schedule variant 2b (native Warp)')
        }
        $late = @($warp.Rows | Select-Object -Skip 2 | Where-Object { $e = Get-PartnerFieldNumber $_ 'err_mm'; $null -eq $e -or $e -gt 5 })
        if ($late.Count) { Add-PartnerItem $Check.Notes "$($late.Count) later partner_warp_check line(s) with err_mm>5 (max $($warp.ErrMax))" }
        $Check.Data['err_mm_max'] = $warp.ErrMax
    }
    if ($probes.Count) {
        $realized = @($probes | Where-Object { Test-PartnerProbeRealized $_ })
        if ($realized.Count) {
            Add-PartnerMatch $Check $realized[0].Line
            Add-PartnerItem $Check.Failed 'ids[1] left ffff during stage 1 (E1 expects instance 1 to stay unrealized)'
        } else { Add-PartnerMatch $Check $probes[$probes.Count - 1].Line }
        $gap = @($probes | ForEach-Object { Get-PartnerDistance $_.Pos0 $_.Pos1 } | Where-Object { $null -ne $_ })
        if ($gap.Count) { Add-PartnerItem $Check.Notes ('pos0-pos1 distance in probes: {0:F0}..{1:F0} mm (fixed source: about the offset)' -f ($gap | Measure-Object -Minimum).Minimum, ($gap | Measure-Object -Maximum).Maximum) }
        $rec = @($probes | ForEach-Object { Get-PartnerDistance $_.Rec1 $_.Pos1 } | Where-Object { $null -ne $_ })
        if ($rec.Count) { Add-PartnerItem $Check.Notes ('informational: rec1_mm vs pos1_mm in the last probe: {0:F0} mm (does the record updater cover non-local instances?)' -f $rec[$rec.Count - 1]) }
    } else { Add-PartnerItem $Check.Missing 'partner_probe=state after stage=1' }
    Add-PartnerItem $Check.Notes 'manual: pos0 keeps tracking the local player and the screenshot shows no camera jump'
}

function Invoke-PartnerE2 {
    # E2: does all-active mode alone realize instance 1?
    param($Check, $Context, $Alive)
    $segment = Get-PartnerSegmentOrMissing $Check $Context { param($s) $s.Stage -eq 2 } "stage=2 (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text 'stage=2')"
    Add-PartnerCfgReady $Check $Context $(if ($segment) { $segment.End } else { [int]::MaxValue })
    if (!$segment) { Add-PartnerCommonChecks $Check $Context -Alive $Alive; return }
    Add-PartnerCommonChecks $Check $Context -From $segment.Start -To $segment.End -Alive $Alive
    $probes = @(Select-PartnerProbe -Context $Context -From $segment.Start -To $segment.End)
    $elapsed = Get-PartnerProbeSpanMs -Probes $probes
    $set = @(Select-PartnerEntry -Context $Context -Kind 'partner_lua=event=allactive_set' -From $segment.Start -To $segment.End)
    if (!$set.Count) {
        if ($elapsed -ge 5000) { Add-PartnerItem $Check.Failed 'no partner_lua=event=allactive_set within 5 s of stage=2' }
        else { Add-PartnerItem $Check.Missing 'partner_lua=event=allactive_set' }
        return
    }
    Add-PartnerMatch $Check $set[0].Line
    $after = @($probes | Where-Object { $_.Index -gt $set[0].Index })
    $afterMs = Get-PartnerProbeSpanMs -Probes $after
    $mechanism = @($after | Where-Object { $null -ne $_.S0 -and ($_.S0 -band 2) -ne 0 -and $_.Q170 -eq 3 })
    if ($mechanism.Count) { Add-PartnerMatch $Check $mechanism[0].Line }
    elseif ($afterMs -ge 3000) { Add-PartnerItem $Check.Failed 's0 bit 1 / q170=0x3 never appeared after allactive_set: the all-active branch is not running (set partner_force_exists=1 for E3)' }
    else { Add-PartnerItem $Check.Missing 'partner_probe=state with s0=0x03 and q170=0x3' }
    $flags = @(Select-PartnerEntry -Context $Context -From $set[0].Index -To $segment.End | Where-Object { $_.Fields.ContainsKey('flags') } | Select-Object -First 1)
    if ($flags.Count) {
        $f = Get-PartnerFieldNumber $flags[0] 'flags'
        Add-PartnerItem $Check.Notes ('flags after allactive_set: ' + [string](Get-PartnerField $flags[0] 'flags') + $(if ($null -ne $f -and ($f -band 0x2000)) { ' (bit 0x2000 set)' } else { ' (bit 0x2000 NOT set)' }))
    } else { Add-PartnerItem $Check.Notes 'no flags= line after allactive_set (s0/q170 used as the mechanism evidence)' }
    $realized = @($after | Where-Object { Test-PartnerProbeRealized $_ })
    if ($realized.Count) {
        Add-PartnerMatch $Check $realized[0].Line
        $Check.Data['outcome'] = 'B'
        Add-PartnerItem $Check.Notes 'outcome B: all-active alone realized instance 1 (pure-Lua realization works, Rank 3); still run E3 for the appearance'
    } elseif ($afterMs -ge 10000) {
        Add-PartnerMatch $Check $after[$after.Count - 1].Line
        $Check.Data['outcome'] = 'A'
        Add-PartnerItem $Check.Notes 'outcome A (expected): ids[1] stayed ffff for 10 s'
    } else { Add-PartnerItem $Check.Missing 'outcome: 10 s of probes after allactive_set' }
    if ($after.Count) {
        $p = $after[$after.Count - 1]
        Add-PartnerItem $Check.Notes ('record: glob={0} pb_guard={1} pb_flag={2} pb_state={3}' -f $p.Glob, $p.PbGuard, (Format-PartnerVector $p.PbFlag), (Format-PartnerVector $p.PbState))
    }
}

function Invoke-PartnerE3 {
    # E3: full realization (bits + copied parts): realized, loaded, stable.
    param($Check, $Context, $Alive)
    $segment = Get-PartnerSegmentOrMissing $Check $Context { param($s) $s.Stage -eq 3 } "stage=3 (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text 'stage=3')"
    Add-PartnerCfgReady $Check $Context $(if ($segment) { $segment.End } else { [int]::MaxValue })
    if (!$segment) { Add-PartnerCommonChecks $Check $Context -Alive $Alive; return }
    Add-PartnerCommonChecks $Check $Context -From $segment.Start -To $segment.End -Alive $Alive
    $from = $segment.Start; $to = $segment.End
    $elapsed = Get-PartnerProbeSpanMs -Probes (Select-PartnerProbe -Context $Context -From $from -To $to -NoExpand)
    $copied = @(Select-PartnerEntry -Context $Context -Kind 'partner_lua=event=parts_copied' -From $from -To $to)
    if ($copied.Count) { Add-PartnerMatch $Check $copied[0].Line }
    elseif ($elapsed -ge 5000) { Add-PartnerItem $Check.Failed 'no partner_lua=event=parts_copied within 5 s of stage=3' }
    else { Add-PartnerItem $Check.Missing 'partner_lua=event=parts_copied' }
    $bits = @(Select-PartnerEntry -Context $Context -Kind 'partner_bits=set' -From $from -To $to)
    foreach ($lost in (Select-PartnerEntry -Context $Context -Kind 'partner_bits=lost' -From $from -To $to | Select-Object -First 1)) {
        Add-PartnerMatch $Check $lost.Line
        Add-PartnerItem $Check.Notes 'partner_bits=lost: an unknown writer clears Q+0x194/0x198 (keep re-ORing; static follow-up)'
    }
    if (!$bits.Count) {
        if ($elapsed -ge 5000) { Add-PartnerItem $Check.Failed 'no partner_bits=set within 5 s of stage=3 (was a pose applied?)' }
        else { Add-PartnerItem $Check.Missing 'partner_bits=set' }
        return
    }
    Add-PartnerMatch $Check $bits[0].Line
    $b = $bits[0].Index
    # Realization is logged only when it changes. When instance 1 is already
    # realized as the bits are ORed (E2 outcome B: all-active alone realized it,
    # possibly in an earlier segment of this mission), no partner_realized=1
    # follows the bits: the realization reference is then that earlier line.
    $boundary = @(Select-PartnerEntry -Context $Context -Kind 'mission_boundary=runtime_enter' -To $b)
    $missionFrom = if ($boundary.Count) { $boundary[-1].Index } else { -1 }
    $prior = @(Select-PartnerEntry -Context $Context -Prefix 'partner_realized' -From $missionFrom -To $b)
    $r = $null
    $afterBits = $false
    $rIsLine = $true
    if ($prior.Count -and $prior[-1].Kind -ceq 'partner_realized=1') {
        $r = $prior[-1]
        Add-PartnerItem $Check.Notes 'instance 1 was already realized before partner_bits=set (E2 outcome B: all-active alone realizes it), so the 1 s rule after the bits does not apply'
    } else {
        $realized = @(Select-PartnerEntry -Context $Context -Kind 'partner_realized=1' -From $b -To $to)
        if ($realized.Count) { $r = $realized[0]; $afterBits = $true }
    }
    if (!$r) {
        $seen = $null
        foreach ($p in (Select-PartnerProbe -Context $Context -From $b -To $to -NoExpand)) {
            if (Test-PartnerProbeRealized (Expand-PartnerProbe -Probe $p)) { $seen = $p; break }
        }
        if ($seen) {
            $r = $seen
            $rIsLine = $false
            Add-PartnerItem $Check.Notes 'probes show instance 1 realized after the bits, but no partner_realized=1 line belongs to this mission (timing unknown: the 1 s rule is not checked)'
        }
    }
    $Check.Data['realized_before_bits'] = [bool]($r -and !$afterBits -and $r.Index -lt $b)
    if (!$r) {
        if ((Get-PartnerMsAfterEvent -Context $Context -EventIndex $b -To $to) -ge 10000) {
            $last = @(Select-PartnerProbe -Context $Context -From $b -To $to -NoExpand | Select-Object -Last 1 | ForEach-Object { Expand-PartnerProbe -Probe $_ })
            $hint = 'F3a: ids[1] stayed ffff after the bits'
            if ($last.Count -and $null -ne $last[-1].S0 -and ($last[-1].S0 -band 2) -eq 0) { $hint += '; s0 bit 1 is clear: rerun with partner_force_exists=1' }
            $hint += '; then partner_offset_cm=100, then partner_policy_relax=1 (last resort), then Rank 5'
            Add-PartnerItem $Check.Failed $hint
        } else { Add-PartnerItem $Check.Missing 'partner_realized=1' }
        return
    }
    Add-PartnerMatch $Check $r.Line
    if ($afterBits) {
        $msBits = Get-PartnerFieldNumber $r 'ms_after_bits'
        $Check.Data['ms_after_bits'] = $msBits
        if ($null -eq $msBits) { Add-PartnerItem $Check.Failed 'partner_realized=1 has no ms_after_bits field' }
        elseif ($msBits -gt 1000) { Add-PartnerItem $Check.Failed "partner_realized=1 came $msBits ms after the bits (PASS needs <= 1000 ms)" }
    } else { $Check.Data['ms_after_bits'] = $null }
    # A partner_realized=1 line inside this segment carries the native timing
    # (ms_after_realize on partner_parts). An earlier realization is measured on
    # the probe clock from the stage=3 line instead (parts are copied there).
    $native = $rIsLine -and $r.Index -gt $from
    $ref = [Math]::Max([int]$r.Index, [int]$from)
    $loaded = @(Select-PartnerEntry -Context $Context -Kind 'partner_parts=loaded' -From $r.Index -To $to)
    $timeout = @(Select-PartnerEntry -Context $Context -Kind 'partner_parts=timeout' -From $ref -To $to)
    if ($loaded.Count) {
        $l = $loaded[0]
        Add-PartnerMatch $Check $l.Line
        $msParts = Get-PartnerFieldNumber $l 'ms_after_realize'
        $Check.Data['parts_ms_after_realize'] = $msParts
        if ($native) {
            if ($null -eq $msParts -or $msParts -gt 15000) { Add-PartnerItem $Check.Failed "partner_parts=loaded after $msParts ms (PASS needs <= 15000 ms)" }
        } elseif ($l.Index -lt $from) {
            Add-PartnerItem $Check.Notes "parts were already loaded before stage=3 ($($l.Line)); check the appearance in the screenshot"
        } else {
            $span = Get-PartnerProbeSpanMs -Probes (Select-PartnerProbe -Context $Context -From $ref -To $l.Index -NoExpand)
            $Check.Data['parts_ms_after_stage3'] = $span
            if ($span -gt 15000) { Add-PartnerItem $Check.Failed "partner_parts=loaded at least $span ms (probe clock) after stage=3 (PASS needs <= 15000 ms)" }
        }
    } elseif ($timeout.Count) {
        Add-PartnerMatch $Check $timeout[0].Line
        Add-PartnerItem $Check.Failed 'F3b: partner_parts=timeout (check q180 bit 1 and pb_state[1]; try the op16 variant: Set-PartnerTrigger.ps1 -Text parts=explicit; check partsBlockCount=2)'
    } elseif ((Get-PartnerMsAfterEvent -Context $Context -EventIndex $ref -To $to) -ge 20000) {
        Add-PartnerItem $Check.Failed $(if ($native) { 'F3b: no partner_parts=loaded within 20 s of realization' } else { 'F3b: no partner_parts=loaded within 20 s of stage=3 (instance 1 was realized earlier)' })
    } else { Add-PartnerItem $Check.Missing 'partner_parts=loaded' }
    foreach ($u in (Select-PartnerEntry -Context $Context -Kind 'partner_realized=0' -From $r.Index -To $to | Select-Object -First 1)) {
        Add-PartnerMatch $Check $u.Line
        Add-PartnerItem $Check.Failed 'partner_realized=0 after realization (thrash, failure mode e)'
    }
    $stable = Get-PartnerMsAfterEvent -Context $Context -EventIndex $ref -To $to
    $Check.Data['stable_s'] = [Math]::Round($stable / 1000.0, 1)
    if ($stable -lt 120000) { Add-PartnerItem $Check.Missing ('120 s realized without crash_probe=av or partner_realized=0 (probe clock {0:F0} s)' -f ($stable / 1000.0)) }
    Add-PartnerItem $Check.Notes 'manual: the host screenshot shows a second player model about 3 m from Snake; then repeat on the guest'
}

function Invoke-PartnerE4 {
    # E4: movement without networking (orbit).
    param($Check, $Context, $Alive)
    $segment = Get-PartnerSegmentOrMissing $Check $Context { param($s) $s.Stage -eq 3 -and $s.Source -ceq 'orbit' } "stage=3 source=orbit (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text 'stage=3 source=orbit')"
    Add-PartnerCfgReady $Check $Context $(if ($segment) { $segment.End } else { [int]::MaxValue })
    if (!$segment) { Add-PartnerCommonChecks $Check $Context -Alive $Alive; return }
    Add-PartnerCommonChecks $Check $Context -From $segment.Start -To $segment.End -Alive $Alive
    Add-PartnerOrbitChecks $Check $Context $segment ''
    Add-PartnerItem $Check.Notes 'manual: 4 screenshots 2 s apart show the partner at different points around Snake; note the pose (idle, T-pose or frozen)'
}

function Add-PartnerOrbitChecks {
    # Shared by E4 and E5: realized throughout, err_mm<=5 every second, slot moving.
    param($Check, $Context, $Segment, [string]$Tag)
    $all = @(Select-PartnerProbe -Context $Context -From $Segment.Start -To $Segment.End -NoExpand)
    $probes = @($all | Select-Object -Last 30 | ForEach-Object { Expand-PartnerProbe -Probe $_ })
    $warp = Add-PartnerWarpChecks $Check $Context $Segment $Tag
    if ($warp.Count -lt 4) { Add-PartnerItem $Check.Missing "${Tag}4 partner_warp_check lines (have $($warp.Count))" }
    else {
        $bad = @($warp.Rows | Where-Object { $e = Get-PartnerFieldNumber $_ 'err_mm'; $null -eq $e -or $e -gt 5 })
        if ($bad.Count) { Add-PartnerMatch $Check $bad[0].Line; Add-PartnerItem $Check.Failed ("${Tag}partner_warp_check err_mm>5: " + $bad[0].Line) }
        else { Add-PartnerMatch $Check $warp.Rows[$warp.Count - 1].Line }
        if ($warp.Spread -lt 500) { Add-PartnerItem $Check.Failed ("${Tag}slot_mm stays at its first spot (spread {0:F0} mm): try apply=both" -f $warp.Spread) }
    }
    if ($probes.Count) {
        $last = $probes[$probes.Count - 1]
        if (Test-PartnerProbeRealized $last) { Add-PartnerMatch $Check $last.Line }
        elseif (@($probes | Where-Object { Test-PartnerProbeRealized $_ }).Count -eq 0 -and (Get-PartnerProbeSpanMs -Probes $all) -ge 5000) {
            Add-PartnerItem $Check.Failed "${Tag}instance 1 is not realized (ids[1]=ffff): pass E3 first"
        } else { Add-PartnerItem $Check.Missing "${Tag}partner_probe=state showing instance 1 realized" }
    } else { Add-PartnerItem $Check.Missing "${Tag}partner_probe=state" }
    foreach ($u in (Select-PartnerEntry -Context $Context -Kind 'partner_realized=0' -From $Segment.Start -To $Segment.End | Select-Object -First 1)) {
        Add-PartnerMatch $Check $u.Line
        Add-PartnerItem $Check.Failed "${Tag}partner_realized=0 while moving (the pose left the loaded area?)"
    }
}

function Invoke-PartnerE5 {
    # E5: apply-mode comparison under orbit (apply=slot, then apply=both).
    param($Check, $Context, $Alive)
    $segment = Get-PartnerSegmentOrMissing $Check $Context { param($s) $s.Stage -eq 3 -and $s.Source -ceq 'orbit' -and $s.Apply -cin @('slot', 'both') } `
        "stage=3 source=orbit apply=slot (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text 'stage=3 source=orbit apply=slot')"
    Add-PartnerCfgReady $Check $Context $(if ($segment) { $segment.End } else { [int]::MaxValue })
    if (!$segment) { Add-PartnerCommonChecks $Check $Context -Alive $Alive; return }
    Add-PartnerCommonChecks $Check $Context -From $segment.Start -To $segment.End -Alive $Alive
    foreach ($mode in 'slot', 'both') {
        $rows = @($Context.Timeline | Where-Object { $_.Index -ge $segment.Start -and $_.Index -lt $segment.End })
        $sub = $null
        for ($i = 0; $i -lt $rows.Count; $i++) {
            if ($rows[$i].Apply -cne $mode) { continue }
            $end = if ($i + 1 -lt $rows.Count) { $rows[$i + 1].Index } else { $segment.End }
            $sub = [pscustomobject]@{ Start = $rows[$i].Index; End = $end; Line = $rows[$i].Line }
        }
        if (!$sub) { Add-PartnerItem $Check.Missing "apply=$mode under orbit (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text 'apply=$mode')"; continue }
        Add-PartnerMatch $Check $sub.Line
        Add-PartnerOrbitChecks $Check $Context $sub "$mode`:"
    }
    Add-PartnerItem $Check.Notes 'manual: does the partner move with apply=slot only (remote body reads S+0x20)? Record jitter or animation resets under warp compared with slot'
}

function Invoke-PartnerE6 {
    # E6: network-driven partner (both instances at stage=3 source=net).
    param($Check, $Context, $PeerContext, $Alive, [bool]$HostOnly)
    if ($HostOnly) { Add-PartnerItem $Check.Failed 'E6 needs both instances (this run is host-only)' }
    $segment = Get-PartnerSegmentOrMissing $Check $Context { param($s) $s.Stage -eq 3 -and $s.Source -ceq 'net' } "stage=3 source=net (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text 'stage=3 source=net')"
    Add-PartnerCfgReady $Check $Context $(if ($segment) { $segment.End } else { [int]::MaxValue })
    $rx = @(Select-PartnerEntry -Context $Context -Kind 'partner_net=first_rx')
    if ($rx.Count) { Add-PartnerMatch $Check $rx[0].Line } else { Add-PartnerItem $Check.Missing 'partner_net=first_rx' }
    if (!$segment) { Add-PartnerCommonChecks $Check $Context -Alive $Alive; return }
    Add-PartnerCommonChecks $Check $Context -From $segment.Start -To $segment.End -Alive $Alive
    $all = @(Select-PartnerProbe -Context $Context -From $segment.Start -To $segment.End -NoExpand)
    $probes = @($all | Select-Object -Last 30 | ForEach-Object { Expand-PartnerProbe -Probe $_ })
    $elapsed = Get-PartnerProbeSpanMs -Probes $all
    $stats = @(Select-PartnerEntry -Context $Context -Kind 'partner_net=stats' -From $segment.Start -To $segment.End)
    $rxValues = @($stats | ForEach-Object { Get-PartnerFieldNumber $_ 'rx' })
    if ($stats.Count -ge 2 -and $null -ne $rxValues[0] -and $null -ne $rxValues[-1] -and $rxValues[-1] -gt $rxValues[0]) {
        Add-PartnerMatch $Check $stats[0].Line
        Add-PartnerMatch $Check $stats[-1].Line
    } elseif ($stats.Count -ge 2) {
        $epoch = Get-PartnerFieldNumber $stats[-1] 'drop_epoch'
        if ($null -ne $epoch -and $epoch -gt 0) { Add-PartnerItem $Check.Failed ('partner_net=stats rx not increasing with drop_epoch=' + $epoch + ': mission mismatch') }
        else { Add-PartnerItem $Check.Failed 'partner_net=stats rx not increasing: check the ports' }
    } else { Add-PartnerItem $Check.Missing "2 partner_net=stats lines with increasing rx (have $($stats.Count); stats come every 10 s)" }
    $live = @($stats | Where-Object {
            $age = Get-PartnerFieldNumber $_ 'last_age_ms'
            (Get-PartnerField $_ 'status') -ceq 'live' -and $null -ne $age -and $age -lt 200 })
    if ($live.Count) { Add-PartnerMatch $Check $live[-1].Line }
    elseif ($stats.Count -ge 2) { Add-PartnerItem $Check.Failed ('no partner_net=stats with status=live and last_age_ms<200 (last: ' + $stats[-1].Line + ')') }
    else { Add-PartnerItem $Check.Missing 'partner_net=stats status=live last_age_ms<200' }
    if ($stats.Count) {
        $Check.Data['last_status'] = Get-PartnerField $stats[-1] 'status'
        $Check.Data['last_age_ms'] = Get-PartnerFieldNumber $stats[-1] 'last_age_ms'
    }
    $realizedProbes = @($probes | Where-Object { Test-PartnerProbeRealized $_ })
    if ($realizedProbes.Count) { Add-PartnerMatch $Check $realizedProbes[-1].Line }
    elseif ($elapsed -ge 15000) { Add-PartnerItem $Check.Failed 'instance 1 never realized under source=net (no WARP before the first packet; check rx)' }
    else { Add-PartnerItem $Check.Missing 'partner_probe=state showing instance 1 realized' }
    foreach ($u in (Select-PartnerEntry -Context $Context -Kind 'partner_realized=0' -From $segment.Start -To $segment.End | Select-Object -First 1)) {
        Add-PartnerMatch $Check $u.Line
        Add-PartnerItem $Check.Failed 'partner_realized=0 under source=net (thrash)'
    }
    # Cross-check: the peer's own pos0 against this instance's pos1, both from the
    # newest probes. Valid only while both players stand still.
    if ($null -eq $PeerContext -or !$PeerContext.Probes.Count) { Add-PartnerItem $Check.Missing 'peer probes for the pos0/pos1 cross-check (run with -Role both)'; return }
    if (!$realizedProbes.Count) { return }
    $mine = @($realizedProbes | Select-Object -Last 2)
    $theirs = @($PeerContext.Probes | Select-Object -Last 2 | ForEach-Object { Expand-PartnerProbe -Probe $_ })
    $stillMine = $mine.Count -eq 2 -and $null -ne (Get-PartnerDistance $mine[0].Pos1 $mine[1].Pos1) -and (Get-PartnerDistance $mine[0].Pos1 $mine[1].Pos1) -le 50
    $stillTheirs = $theirs.Count -eq 2 -and $null -ne (Get-PartnerDistance $theirs[0].Pos0 $theirs[1].Pos0) -and (Get-PartnerDistance $theirs[0].Pos0 $theirs[1].Pos0) -le 50
    $error_ = Get-PartnerDistance $mine[-1].Pos1 $theirs[-1].Pos0
    $Check.Data['cross_check_mm'] = $(if ($null -ne $error_) { [Math]::Round($error_) } else { $null })
    if ($null -eq $error_) { Add-PartnerItem $Check.Missing 'pos1_mm / peer pos0_mm for the cross-check' }
    elseif (!$stillMine -or !$stillTheirs) { Add-PartnerItem $Check.Missing ('cross-check while both players stand still for 15 s (newest difference {0:F0} mm)' -f $error_) }
    elseif ($error_ -le 200) { Add-PartnerItem $Check.Notes ('cross-check: local pos1_mm={0} vs peer pos0_mm={1} ({2:F0} mm)' -f (Format-PartnerVector $mine[-1].Pos1), (Format-PartnerVector $theirs[-1].Pos0), $error_) }
    else { Add-PartnerItem $Check.Failed ('cross-check: local pos1_mm={0} vs peer pos0_mm={1} differ by {2:F0} mm > 200 (coordinate mismatch? compare the numbers)' -f (Format-PartnerVector $mine[-1].Pos1), (Format-PartnerVector $theirs[-1].Pos0), $error_) }
    Add-PartnerItem $Check.Notes 'manual: the partner appears at the other instance''s position and follows it; pause test (Esc on the guest): record stale/lost or live'
}

function Invoke-PartnerE8 {
    # E8: teardown (stop trigger), re-realization, mission exit and re-entry.
    param($Check, $Context, $Alive)
    $first = @(Select-PartnerEntry -Context $Context -Kind 'partner_realized=1')
    if (!$first.Count) { Add-PartnerItem $Check.Missing 'partner_realized=1 before the stop (pass E3 first)'; Add-PartnerCommonChecks $Check $Context -Alive $Alive; return }
    Add-PartnerMatch $Check $first[0].Line
    Add-PartnerCommonChecks $Check $Context -From $first[0].Index -Alive $Alive
    $stop = @(Select-PartnerEntry -Context $Context -Kind 'partner_bits=cleared' -From $first[0].Index | Where-Object { (Get-PartnerField $_ 'reason') -ceq 'trigger' })
    if (!$stop.Count) { Add-PartnerItem $Check.Missing "step 1: partner_bits=cleared; reason=trigger (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text stop)"; return }
    Add-PartnerMatch $Check $stop[0].Line
    $s = $stop[0].Index
    $cleared = @(Select-PartnerEntry -Context $Context -Kind 'partner_lua=event=allactive_cleared' -From $s)
    if ($cleared.Count) { Add-PartnerMatch $Check $cleared[0].Line } else { Add-PartnerItem $Check.Missing 'step 1: partner_lua=event=allactive_cleared' }
    $gone = @(Select-PartnerEntry -Context $Context -Kind 'partner_realized=0' -From $s)
    if (!$gone.Count) {
        if ((Get-PartnerMsAfterEvent -Context $Context -EventIndex $s) -ge 5000) { Add-PartnerItem $Check.Failed 'step 1: no partner_realized=0 within 5 s of the stop (teardown did not unrealize)' }
        else { Add-PartnerItem $Check.Missing 'step 1: partner_realized=0' }
        return
    }
    Add-PartnerMatch $Check $gone[0].Line
    $again = @(Select-PartnerEntry -Context $Context -Kind 'partner_realized=1' -From $gone[0].Index)
    if (!$again.Count) { Add-PartnerItem $Check.Missing "step 2: partner_realized=1 again (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text 'stage=3')"; return }
    Add-PartnerMatch $Check $again[0].Line
    $stopLine = @(Select-PartnerEntry -Context $Context -Kind 'partner_stop' -From $again[0].Index)
    if (!$stopLine.Count) { Add-PartnerItem $Check.Missing 'step 3: partner_stop (leave the mission)'; return }
    Add-PartnerMatch $Check $stopLine[0].Line
    $Check.Data['teardown'] = Get-PartnerField $stopLine[0] 'teardown'
    $termination = @(Select-PartnerEntry -Context $Context -Prefix 'mission_termination' -From $stopLine[0].Index | Select-Object -First 1)
    if ($termination.Count) { Add-PartnerMatch $Check $termination[0].Line } else { Add-PartnerItem $Check.Notes 'no mission_termination= line after partner_stop' }
    $reentry = @(Select-PartnerEntry -Context $Context -Kind 'partner_cfg=ready' -From $stopLine[0].Index)
    if ($reentry.Count) { Add-PartnerMatch $Check $reentry[0].Line } else { Add-PartnerItem $Check.Missing 'step 4: partner_cfg=ready after re-entering 30050' }
    Add-PartnerItem $Check.Notes 'manual: the model is gone from the screenshot after the stop; on a crash at stop rerun with partner_teardown=keep'
}

# ---------------------------------------------------------------------------
# Animation / appearance experiments (docs/partner-anim-appearance.md)
# ---------------------------------------------------------------------------

function Add-PartnerFaultChecks {
    # partner_call=fault: a native call raised inside SEH (calls are disabled for
    # the rest of the mission).
    param($Check, $Context, [int]$From = 0, [int]$To = [int]::MaxValue)
    foreach ($e in (Select-PartnerEntry -Context $Context -Kind 'partner_call=fault' -From $From -To $To)) {
        Add-PartnerMatch $Check $e.Line
        Add-PartnerItem $Check.Failed ('partner_call=fault; fn=' + [string](Get-PartnerField $e 'fn') + ': a native call raised (native calls disabled for the mission; collect the crash_probe RVAs)')
    }
}

function Get-PartnerAnimSegment {
    # Shared prologue: the segment for $Predicate, partner_cfg=ready, common and fault checks.
    param($Check, $Context, $Alive, [scriptblock]$Predicate, [string]$How)
    $segment = Get-PartnerSegmentOrMissing $Check $Context $Predicate $How
    Add-PartnerCfgReady $Check $Context $(if ($segment) { $segment.End } else { [int]::MaxValue })
    if (!$segment) { Add-PartnerCommonChecks $Check $Context -Alive $Alive; return $null }
    Add-PartnerCommonChecks $Check $Context -From $segment.Start -To $segment.End -Alive $Alive
    Add-PartnerFaultChecks $Check $Context -From $segment.Start -To $segment.End
    return $segment
}

function Invoke-PartnerA0 {
    # A0: component census of both blocks (pure reads; no game call).
    param($Check, $Context, $Alive)
    Add-PartnerCfgReady $Check $Context ([int]::MaxValue)
    Add-PartnerCommonChecks $Check $Context -Alive $Alive
    $lines = @(Select-PartnerEntry -Context $Context -Prefix 'partner_census')
    foreach ($e in @($lines | Where-Object { $_.Kind -ceq 'partner_census=refused' })) {
        Add-PartnerMatch $Check $e.Line
        Add-PartnerItem $Check.Failed ('partner_census=refused; reason=' + [string](Get-PartnerField $e 'reason'))
    }
    foreach ($k in 0, 1) {
        $mine = @($lines | Where-Object { $_.Kind -ceq 'partner_census' -and (Get-PartnerField $_ 'k') -ceq [string]$k })
        if (!$mine.Count) { Add-PartnerItem $Check.Missing "partner_census; k=$k (after partner_parts=loaded: Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text 'stage=3 census=1')"; continue }
        $last = $mine[-1]
        Add-PartnerMatch $Check $last.Line
        $verdict = [string](Get-PartnerField $last 'verdict')
        if ($verdict -ceq 'unrealized' -or $verdict -ceq 'parts') { Add-PartnerItem $Check.Missing "partner_census with instance 1 realized and loaded (k=$k verdict=$verdict)" }
        elseif ($verdict -cne 'ok') { Add-PartnerItem $Check.Failed "partner_census k=$k verdict=$verdict (an identity check failed: no native call will be made)" }
        foreach ($f in 't_vt', 't68', 't70', 'a0_vt', 'a080', 'c0_vt', 'c090', 'psw', 'stance') { $Check.Data["k${k}_$f"] = Get-PartnerField $last $f }
        $stanceVerdict = [string](Get-PartnerField $last 'stance')
        if ($verdict -ceq 'ok' -and $stanceVerdict -and $stanceVerdict -cne 'ok') {
            Add-PartnerItem $Check.Notes "k=$k stance=$stanceVerdict`: SetStance / aset=r are refused for this instance (xform is not affected)"
        }
        if ($k -eq 1 -and $verdict -ceq 'ok') {
            foreach ($same in 'same_t', 'same_a0', 'same_c0') {
                if ((Get-PartnerField $last $same) -cne '1') { Add-PartnerItem $Check.Notes "$same=0: instance 1 differs from the local instance (calls needing it are refused)" }
            }
        }
    }
    Add-PartnerItem $Check.Notes 'review: pin t_vt/t68/t70 (and a0/c0) in tools/generate_partner_contract.py after re-verifying those RVAs statically'
}

function Invoke-PartnerA1 {
    # A1: anim=mirror aset=q writes the slot-1 arrays (expected: PlayerInfo flags only, no visible pose change).
    param($Check, $Context, $Alive)
    $segment = Get-PartnerAnimSegment $Check $Context $Alive { param($s) $s.Stage -eq 3 -and $s.Anim -ceq 'mirror' -and $s.Aset -cin @('q', 'qs') } `
        "stage=3 anim=mirror aset=q (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text '$script:PartnerA1Trigger')"
    if (!$segment) { return }
    $aset = @(Select-PartnerEntry -Context $Context -Kind 'partner_aset=q' -From $segment.Start -To $segment.End)
    # act_domain: the host did something other than stand/crouch/prone x idle/moving
    # (act outside 1..6); those ids are deliberately not copied.
    $skipped = @($aset | Where-Object { (Get-PartnerField $_ 'state') -ceq 'refused' -and (Get-PartnerField $_ 'reason') -ceq 'act_domain' })
    if ($skipped.Count) { Add-PartnerItem $Check.Notes "$($skipped.Count) partner_aset=q act_domain skips (non-locomotion act ids are not copied)" }
    $refused = @($aset | Where-Object { (Get-PartnerField $_ 'state') -ceq 'refused' -and (Get-PartnerField $_ 'reason') -cne 'act_domain' })
    $applied = @($aset | Where-Object { (Get-PartnerField $_ 'state') -ceq 'applied' })
    foreach ($e in $refused) { Add-PartnerMatch $Check $e.Line; Add-PartnerItem $Check.Failed ('partner_aset=q refused: reason=' + [string](Get-PartnerField $e 'reason')) }
    if ($applied.Count) { Add-PartnerMatch $Check $applied[0].Line } elseif (!$refused.Count) { Add-PartnerItem $Check.Missing 'partner_aset=q; state=applied (needs partner_parts=loaded)' }
    $anim = @(Select-PartnerEntry -Context $Context -Kind 'partner_anim' -From $segment.Start -To $segment.End)
    if (!$anim.Count) { Add-PartnerItem $Check.Missing 'partner_anim lines (lab.ini partner_anim_probe=1)'; return }
    $same = 0; $stances = @{}
    foreach ($e in $anim) {
        $act = Get-PartnerFieldList $e 'act' -Hex -Count 2
        $s = Get-PartnerFieldList $e 's' -Count 2
        $v = Get-PartnerFieldList $e 'v' -Count 2
        if ($null -eq $act -or $null -eq $s -or $null -eq $v) { continue }
        $stances[[string]$s[0]] = $true
        if ($act[0] -eq $act[1] -and $s[0] -eq $s[1] -and $v[0] -eq $v[1]) { $same++ }
    }
    $Check.Data['anim_lines'] = $anim.Count
    $Check.Data['slot1_equals_slot0'] = $same
    $Check.Data['stances_seen'] = (@($stances.Keys) | Sort-Object) -join ','
    if ($stances.Count -lt 2) { Add-PartnerItem $Check.Missing 'the host changes stance while A1 runs (stand, crouch, prone: partner_anim s= changes)' }
    if (!$same) { Add-PartnerItem $Check.Missing 'a partner_anim line with slot 1 equal to slot 0 (act, s and v)' } else { Add-PartnerMatch $Check $anim[-1].Line }
    Add-PartnerItem $Check.Notes 'manual: expected NO visible change of the partner pose (the arrays feed PlayerInfo); report any visible change'
    $null = Add-PartnerWarpNote $Check $Context $segment
}

function Add-PartnerWarpNote {
    # Warp (0x9CEAE0) ends with the resets *(blk+0xC0).vt+0x90 and *(blk+0xA0).vt+0x80
    # (0x9CEC0E..0x9CEC2C), suspected to reset the body's animation state. When
    # the segment moved the partner by Warp every tick (apply=warp/slot/both, or
    # xform refused and falling back to Warp), a missing visible change or a
    # partner_stance=reverted cannot be told apart from that reset.
    param($Check, $Context, $Segment)
    $applies = @(@($Segment.State) + @($Context.Timeline | Where-Object { $_.Index -ge $Segment.Start -and $_.Index -lt $Segment.End }) |
        ForEach-Object { [string]$_.Apply } | Where-Object { $_ } | Select-Object -Unique)
    $warpApply = @($applies | Where-Object { $_ -cin @('warp', 'both') })
    $perTickWarp = $warpApply.Count -gt 0
    $fallback = @(Select-PartnerEntry -Context $Context -Kind 'partner_xform=refused' -From $Segment.Start -To $Segment.End |
        Where-Object { (Get-PartnerField $_ 'fallback') -ceq 'warp' -and (Get-PartnerField $_ 'reason') -cnotin @('unrealized', 'parts') })
    $Check.Data['per_tick_warp'] = $perTickWarp -or $fallback.Count -gt 0
    if ($perTickWarp) { Add-PartnerItem $Check.Notes "apply=$($warpApply[0]) Warps every tick (with Warp's resets): rerun with apply=hybrid reset=n after M1 passes to attribute the result" }
    elseif ($fallback.Count) { Add-PartnerItem $Check.Notes ('xform fell back to Warp (reason=' + [string](Get-PartnerField $fallback[0] 'reason') + '): the per-tick Warp resets may mask the result') }
    return $Check.Data['per_tick_warp']
}

function Invoke-PartnerA3 {
    # A3: SetStance (PS.vt+0 = 0xFF5B10) on stance changes.
    param($Check, $Context, $Alive)
    $segment = Get-PartnerAnimSegment $Check $Context $Alive { param($s) $s.Stage -eq 3 -and $s.Anim -cne 'off' -and $s.Aset -cin @('s', 'qs') } `
        "stage=3 anim=mirror aset=qs (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text '$script:PartnerA3Trigger')"
    if (!$segment) { return }
    $warped = Add-PartnerWarpNote $Check $Context $segment
    $set = @(Select-PartnerEntry -Context $Context -Kind 'partner_stance=set' -From $segment.Start -To $segment.End)
    # A refused target is retried after 1 s: a refusal followed by a set of the
    # same target was transient (a note); one never followed by it fails.
    foreach ($e in (Select-PartnerEntry -Context $Context -Kind 'partner_stance=refused' -From $segment.Start -To $segment.End | Select-Object -First 3)) {
        Add-PartnerMatch $Check $e.Line
        $to = [string](Get-PartnerField $e 'to')
        $later = @($set | Where-Object { $_.Index -gt $e.Index -and (Get-PartnerField $_ 'to') -ceq $to })
        if ($later.Count) { Add-PartnerItem $Check.Notes ('transient partner_stance=refused (reason=' + [string](Get-PartnerField $e 'reason') + "; to=$to): retried and applied") }
        else { Add-PartnerItem $Check.Failed ('partner_stance=refused: reason=' + [string](Get-PartnerField $e 'reason')) }
    }
    $targets = @($set | ForEach-Object { Get-PartnerField $_ 'to' } | Sort-Object -Unique)
    $Check.Data['stance_sets'] = $set.Count
    $Check.Data['stance_targets'] = $targets -join ','
    $reverted = @(Select-PartnerEntry -Context $Context -Kind 'partner_stance=reverted' -From $segment.Start -To $segment.End)
    $Check.Data['stance_reverted'] = $reverted.Count
    if ($reverted.Count) {
        Add-PartnerMatch $Check $reverted[0].Line
        if ($warped) { Add-PartnerItem $Check.Notes "$($reverted.Count) partner_stance=reverted with a per-tick Warp: Warp's resets may have undone SetStance (not attributable; rerun with apply=hybrid reset=n)" }
        else { Add-PartnerItem $Check.Notes "$($reverted.Count) partner_stance=reverted without a per-tick Warp: the action plugin overrides the stance word (try aset=r)" }
    }
    if ($set.Count -ge 2 -and $targets.Count -ge 2) { foreach ($e in @($set | Select-Object -First 2)) { Add-PartnerMatch $Check $e.Line } }
    else { Add-PartnerItem $Check.Missing "2 partner_stance=set lines with different targets (have $($set.Count); the host toggles stance every 3 s)" }
    Add-PartnerItem $Check.Notes 'manual: the partner switches to the crouch / prone idle within 0.5 s of each set'
}

function Invoke-PartnerM1 {
    # M1: native body transform (apply=xform or hybrid) without Warp's resets.
    param($Check, $Context, $Alive)
    $segment = Get-PartnerAnimSegment $Check $Context $Alive { param($s) $s.Stage -eq 3 -and $s.Apply -cin @('xform', 'hybrid') } `
        "stage=3 apply=xform (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text 'stage=3 source=orbit apply=xform reset=n')"
    if (!$segment) { return }
    $states = @(Select-PartnerEntry -Context $Context -Prefix 'partner_xform' -From $segment.Start -To $segment.End | Where-Object { $_.Kind -cin @('partner_xform=applied', 'partner_xform=refused') })
    $applied = @($states | Where-Object { $_.Kind -ceq 'partner_xform=applied' })
    $bad = @($states | Where-Object { $_.Kind -ceq 'partner_xform=refused' -and (Get-PartnerField $_ 'reason') -cnotin @('unrealized', 'parts', 'fault', 'native_disabled') })
    foreach ($e in @($bad | Select-Object -First 2)) { Add-PartnerMatch $Check $e.Line; Add-PartnerItem $Check.Failed ('partner_xform=refused; reason=' + [string](Get-PartnerField $e 'reason') + ' (census identity: run A0)') }
    if ($applied.Count) { Add-PartnerMatch $Check $applied[0].Line } elseif (!$bad.Count) { Add-PartnerItem $Check.Missing 'partner_xform=applied (needs partner_parts=loaded)' }
    $rows = @(Select-PartnerEntry -Context $Context -Kind 'partner_xform' -From $segment.Start -To $segment.End)
    $errs = @($rows | ForEach-Object { Get-PartnerFieldNumber $_ 'err_mm' } | Where-Object { $null -ne $_ })
    $yaws = @($rows | ForEach-Object { Get-PartnerFieldNumber $_ 'yaw_err_mrad' } | Where-Object { $null -ne $_ })
    $Check.Data['xform_checks'] = $rows.Count
    $Check.Data['err_mm_max'] = $(if ($errs.Count) { [Int64]($errs | Measure-Object -Maximum).Maximum } else { $null })
    $Check.Data['yaw_err_mrad_max'] = $(if ($yaws.Count) { [Int64]($yaws | Measure-Object -Maximum).Maximum } else { $null })
    if ($rows.Count -lt 4) { Add-PartnerItem $Check.Missing "4 partner_xform readback lines (have $($rows.Count))" }
    else {
        $far = @($rows | Where-Object { $e = Get-PartnerFieldNumber $_ 'err_mm'; $null -ne $e -and $e -gt $script:PartnerXformFarMm })
        if ($far.Count) { Add-PartnerMatch $Check $far[0].Line; Add-PartnerItem $Check.Failed ("partner_xform err_mm>$script:PartnerXformFarMm (the body does not follow the transform): " + $far[0].Line) }
        else { Add-PartnerMatch $Check $rows[-1].Line }
        if ($null -ne $Check.Data['err_mm_max'] -and $Check.Data['err_mm_max'] -gt 50) { Add-PartnerItem $Check.Notes "err_mm up to $($Check.Data['err_mm_max']) (> 50 expected by the research: snap-back? try reset=c/a/ac)" }
        if ($null -ne $Check.Data['yaw_err_mrad_max'] -and $Check.Data['yaw_err_mrad_max'] -gt 50) { Add-PartnerItem $Check.Notes "yaw_err_mrad up to $($Check.Data['yaw_err_mrad_max'])" }
    }
    $hybrid = @(Select-PartnerEntry -Context $Context -Kind 'partner_hybrid=warp' -From $segment.Start -To $segment.End)
    $Check.Data['hybrid_warps'] = $hybrid.Count
    Add-PartnerItem $Check.Notes 'manual (KEY): do the legs cycle while orbiting, or does the body glide in one pose? Take 3 screenshots 0.3 s apart'
}

function Invoke-PartnerN1 {
    # N1: the partner's animation fields over the network (anim=net).
    param($Check, $Context, $Alive)
    $segment = Get-PartnerAnimSegment $Check $Context $Alive { param($s) $s.Stage -eq 3 -and $s.Source -ceq 'net' -and $s.Anim -ceq 'net' } `
        "stage=3 source=net anim=net (Set-PartnerTrigger.ps1 -Role both -Text 'stage=3 source=net apply=hybrid anim=net aset=qs')"
    if (!$segment) { return }
    $sources = @(Select-PartnerEntry -Context $Context -Kind 'partner_anim=source' -From $segment.Start -To $segment.End)
    $valid = @($sources | Where-Object { (Get-PartnerField $_ 'valid') -ceq '1' })
    if ($valid.Count) { Add-PartnerMatch $Check $valid[0].Line }
    elseif ($sources.Count) { Add-PartnerMatch $Check $sources[-1].Line; Add-PartnerItem $Check.Missing 'received frames with the anim extension (valid=1): is the peer on this build, with its parts loaded?' }
    else { Add-PartnerItem $Check.Missing 'partner_anim=source; anim=net' }
    $applied = @(Select-PartnerEntry -Context $Context -Kind 'partner_stance=set' -From $segment.Start -To $segment.End) +
        @(Select-PartnerEntry -Context $Context -Kind 'partner_aset=q' -From $segment.Start -To $segment.End | Where-Object { (Get-PartnerField $_ 'state') -ceq 'applied' })
    if ($applied.Count) { Add-PartnerMatch $Check $applied[0].Line } else { Add-PartnerItem $Check.Missing 'partner_stance=set or partner_aset=q; state=applied from the received anim' }
    Add-PartnerItem $Check.Notes 'manual: the partner shows the other player''s stance and facing on both PCs; record whether locomotion animates'
}

function Get-PartnerLookChecks {
    # Shared by B1/B4: apply, Lua SetPartsInfo, loaded; timeout/refusals fail.
    param($Check, $Context, $Segment, [string]$Source, [string]$Type)
    $applies = @(Select-PartnerEntry -Context $Context -Kind 'partner_look=apply' -From $Segment.Start -To $Segment.End | Where-Object { (Get-PartnerField $_ 'source') -ceq $Source })
    if ($Type) { $applies = @($applies | Where-Object { (Get-PartnerField $_ 'type') -ceq $Type }) }
    if ($applies.Count) { Add-PartnerMatch $Check $applies[0].Line } else { Add-PartnerItem $Check.Missing "partner_look=apply; source=$Source$(if ($Type) { "; type=$Type" })" }
    $lua = @(Select-PartnerEntry -Context $Context -Kind 'partner_lua=event=parts_set' -From $Segment.Start -To $Segment.End)
    if ($lua.Count) { Add-PartnerMatch $Check $lua[0].Line } else { Add-PartnerItem $Check.Missing 'partner_lua=event=parts_set (Player.SetPartsInfoAtInstanceIndex called)' }
    foreach ($e in (Select-PartnerEntry -Context $Context -Kind 'partner_lua=event=parts_set_refused' -From $Segment.Start -To $Segment.End | Select-Object -First 1)) {
        Add-PartnerMatch $Check $e.Line; Add-PartnerItem $Check.Failed 'partner_lua=event=parts_set_refused: the reply look values were refused by Lua'
    }
    foreach ($e in (Select-PartnerEntry -Context $Context -Kind 'partner_look=refused' -From $Segment.Start -To $Segment.End | Select-Object -First 1)) {
        Add-PartnerMatch $Check $e.Line; Add-PartnerItem $Check.Failed ('partner_look=refused; reason=' + [string](Get-PartnerField $e 'reason'))
    }
    foreach ($e in (Select-PartnerEntry -Context $Context -Kind 'partner_look=timeout' -From $Segment.Start -To $Segment.End | Select-Object -First 1)) {
        Add-PartnerMatch $Check $e.Line; Add-PartnerItem $Check.Failed ('partner_look=timeout (the parts never reloaded with the look): ' + $e.Line)
    }
    $loaded = @(Select-PartnerEntry -Context $Context -Kind 'partner_look=loaded' -From $Segment.Start -To $Segment.End)
    if ($Type) { $loaded = @($loaded | Where-Object { (Get-PartnerField $_ 'q40') -ceq $Type }) }
    if ($loaded.Count) {
        Add-PartnerMatch $Check $loaded[0].Line
        $Check.Data['look_ms_after_apply'] = Get-PartnerFieldNumber $loaded[0] 'ms_after_apply'
    } else { Add-PartnerItem $Check.Missing "partner_look=loaded$(if ($Type) { "; q40=$Type" })" }
    return $applies
}

function Invoke-PartnerB1 {
    # B1: local test look=quiet (no network): instance 1 reloads as Quiet.
    param($Check, $Context, $Alive)
    $segment = Get-PartnerAnimSegment $Check $Context $Alive { param($s) $s.Stage -eq 3 -and $s.Look -ceq 'quiet' } `
        "stage=3 look=quiet (Set-PartnerTrigger.ps1 -Role $($Check.Role) -Text 'stage=3 source=orbit look=quiet')"
    if (!$segment) { return }
    $null = Get-PartnerLookChecks $Check $Context $segment 'quiet' '6'
    Add-PartnerItem $Check.Notes 'manual: the screenshot shows Quiet orbiting; with source=fixed, note whether a still Quiet fades (stealth)'
}

function Invoke-PartnerB4 {
    # B4: look=net on both: each instance shows the other player's own look.
    param($Check, $Context, $Alive)
    $segment = Get-PartnerAnimSegment $Check $Context $Alive { param($s) $s.Stage -eq 3 -and $s.Look -ceq 'net' } `
        "stage=3 look=net (Set-PartnerTrigger.ps1 -Role both -Text 'stage=3 source=net look=net')"
    if (!$segment) { return }
    $rx = @(Select-PartnerEntry -Context $Context -Kind 'partner_net=look_rx')
    if ($rx.Count) {
        Add-PartnerMatch $Check $rx[-1].Line
        $Check.Data['remote_type'] = Get-PartnerField $rx[-1] 'type'
    } else { Add-PartnerItem $Check.Missing 'partner_net=look_rx (the peer sends its look once its parts are loaded)' }
    $applies = Get-PartnerLookChecks $Check $Context $segment 'net' ''
    if ($applies.Count) {
        $copied = @(Select-PartnerEntry -Context $Context -Kind 'partner_lua=event=parts_copied' -From $applies[0].Index -To $segment.End)
        if ($copied.Count) { Add-PartnerMatch $Check $copied[0].Line; Add-PartnerItem $Check.Failed 'partner_lua=event=parts_copied after the network look was applied (the copy must stop)' }
    }
    Add-PartnerItem $Check.Notes 'manual: the host screenshot shows the guest''s character (Quiet), the guest screenshot shows the host''s (Snake)'
}

function Invoke-PartnerC1 {
    # C1: the guest plays Quiet in offline-v1 (coop.ini character=quiet; lab.json offline.characters.guest).
    param($Check, $Context, $Alive)
    Add-PartnerCommonChecks $Check $Context -Alive $Alive
    $config = @(Select-PartnerEntry -Context $Context -Prefix 'character_config')
    if (!$config.Count) { Add-PartnerItem $Check.Missing 'character_config= (offline-v1 answers the character receipts in this build)' }
    elseif ($config[-1].Value -cne 'quiet') { Add-PartnerMatch $Check $config[-1].Line; Add-PartnerItem $Check.Failed ('character_config=' + $config[-1].Value + ': coop.ini character=quiet not in place (Prepare-LabInstance.ps1 -Offline with lab.json offline.characters.guest=quiet)') }
    else { Add-PartnerMatch $Check $config[-1].Line }
    foreach ($e in (Select-PartnerEntry -Context $Context -Kind 'player_type_patch=refused' | Select-Object -First 1)) { Add-PartnerMatch $Check $e.Line; Add-PartnerItem $Check.Failed 'player_type_patch=refused: the Quiet reset bypass was not installed' }
    $patch = @(Select-PartnerEntry -Context $Context -Kind 'player_type_patch=active')
    if ($patch.Count) { Add-PartnerMatch $Check $patch[0].Line } else { Add-PartnerItem $Check.Missing 'player_type_patch=active' }
    $apply = @(Select-PartnerEntry -Context $Context -Kind 'character=apply')
    if (!$apply.Count) { Add-PartnerItem $Check.Missing 'character=apply' }
    else {
        $last = $apply[-1]
        Add-PartnerMatch $Check $last.Line
        if ((Get-PartnerField $last 'applied') -cne 'quiet') { Add-PartnerItem $Check.Failed ('character=apply applied=' + [string](Get-PartnerField $last 'applied') + '; reason=' + [string](Get-PartnerField $last 'reason')) }
    }
    $allocated = @(Select-PartnerEntry -Context $Context -Kind 'character=allocated')
    if (!$allocated.Count) { Add-PartnerItem $Check.Missing 'character=allocated' }
    elseif ((Get-PartnerField $allocated[-1] 'vars') -cne '6') { Add-PartnerMatch $Check $allocated[-1].Line; Add-PartnerItem $Check.Failed ('character=allocated vars=' + [string](Get-PartnerField $allocated[-1] 'vars') + ' (expected 6: Quiet)') }
    else { Add-PartnerMatch $Check $allocated[-1].Line }
    $readback = @(Select-PartnerEntry -Context $Context -Prefix 'character_readback')
    if ($readback.Count) { Add-PartnerMatch $Check $readback[-1].Line }
    Add-PartnerItem $Check.Notes 'manual: the guest plays Quiet for 2 minutes; the pause menu opens; the host sees a Quiet partner with look=net (B4)'
}

$script:PartnerXformFarMm = 1500

function Get-PartnerSpikeVerdict {
    # One instance's verdict for one experiment. Inputs are this run's log texts
    # (from the run.json offsets); $Safety is Get-InstanceSafety's result (E0);
    # $Alive is $true/$false or $null when not checked.
    param(
        [Parameter(Mandatory)][ValidateSet('E0', 'E1', 'E2', 'E3', 'E4', 'E5', 'E6', 'E8', 'A0', 'A1', 'A3', 'M1', 'N1', 'B1', 'B4', 'C1', 'L1', 'L2', 'L3', 'L4', 'I0', 'I1', 'I2', 'I3', 'I3x', 'I4', 'I5', 'I6', 'I7', 'D0', 'D1', 'D2', 'D3')][string]$Experiment,
        [Parameter(Mandatory)][ValidateSet('host', 'guest')][string]$Role,
        [AllowNull()][AllowEmptyString()][string]$ObserverLog = '',
        [AllowNull()][AllowEmptyString()][string]$EarlyLog = '',
        [AllowNull()][AllowEmptyString()][string]$SteamLog = '',
        [AllowNull()][AllowEmptyString()][string]$PeerObserverLog = '',
        $Safety = $null,
        [string]$ExpectedMasterUrl = '',
        [bool]$OfflineRun = $true,
        [bool]$HostOnly = $false,
        $Alive = $null
    )
    $check = New-PartnerCheck -Experiment $Experiment -Role $Role
    $context = New-PartnerContext -ObserverLog $ObserverLog
    switch ($Experiment) {
        'E0' {
            Invoke-PartnerE0 $check $context ([string]$EarlyLog) ([string]$SteamLog) $Safety $ExpectedMasterUrl $OfflineRun $HostOnly
            Add-PartnerCommonChecks $check $context -Alive $Alive
        }
        'E1' { Invoke-PartnerE1 $check $context $Alive }
        'E2' { Invoke-PartnerE2 $check $context $Alive }
        'E3' { Invoke-PartnerE3 $check $context $Alive }
        'E4' { Invoke-PartnerE4 $check $context $Alive }
        'E5' { Invoke-PartnerE5 $check $context $Alive }
        'E6' {
            $peer = if ($PeerObserverLog) { New-PartnerContext -ObserverLog $PeerObserverLog } else { $null }
            Invoke-PartnerE6 $check $context $peer $Alive $HostOnly
        }
        'E8' { Invoke-PartnerE8 $check $context $Alive }
        'A0' { Invoke-PartnerA0 $check $context $Alive }
        'A1' { Invoke-PartnerA1 $check $context $Alive }
        'A3' { Invoke-PartnerA3 $check $context $Alive }
        'M1' { Invoke-PartnerM1 $check $context $Alive }
        'N1' { Invoke-PartnerN1 $check $context $Alive }
        'B1' { Invoke-PartnerB1 $check $context $Alive }
        'B4' { Invoke-PartnerB4 $check $context $Alive }
        'C1' { Invoke-PartnerC1 $check $context $Alive }
        default { Invoke-PartnerRound3 $check $context $Experiment $Alive }
    }
    if (!$context.Entries.Count) { Add-PartnerItem $check.Notes 'script-observer.log has no lines from this run yet' }
    return Complete-PartnerCheck -Check $check
}

function Merge-PartnerSpikeVerdict {
    # Combines per-role verdicts: FAIL if any role fails, else INCOMPLETE if any
    # is incomplete, else PASS. Top-level lists carry a "[role] " prefix.
    param([Parameter(Mandatory)][string]$Experiment, [Parameter(Mandatory)][string]$Role, [Parameter(Mandatory)][Collections.IDictionary]$PerRole)
    $verdicts = @($PerRole.Values | ForEach-Object { $_.verdict })
    $verdict = if ($verdicts -contains 'FAIL') { 'FAIL' } elseif ($verdicts -contains 'INCOMPLETE' -or !$verdicts.Count) { 'INCOMPLETE' } else { 'PASS' }
    $lists = [ordered]@{ matched = @(); missing = @(); failed = @(); notes = @() }
    foreach ($name in $PerRole.Keys) {
        foreach ($key in @($lists.Keys)) { $lists[$key] += @($PerRole[$name].$key | ForEach-Object { "[$name] $_" }) }
    }
    [pscustomobject][ordered]@{
        experiment = $Experiment; role = $Role; verdict = $verdict
        matched = $lists.matched; missing = $lists.missing; failed = $lists.failed; notes = $lists.notes
        per_role = $PerRole
    }
}

function Get-PartnerExperimentDefaultRole {
    # E0, E6, N1 and B4 are judged on both instances, C1 on the guest (it plays
    # Quiet), the rest on the host.
    param([Parameter(Mandatory)][string]$Experiment)
    if ($Experiment -in @('E0', 'E6', 'N1', 'B4', 'L1', 'L2', 'L4', 'I6')) { return 'both' }
    if ($Experiment -eq 'C1') { return 'guest' }
    return 'host'
}

function Get-PartnerExperimentPlan {
    # Operator steps per experiment for Invoke-PartnerExperiment.ps1: triggers
    # per role, keyboard input (Send-LabInput key specs), screenshots and the
    # default verdict timeout. Section 3 of the spike plan.
    param([Parameter(Mandatory)][ValidateSet('E0', 'E1', 'E2', 'E3', 'E4', 'E5', 'E6', 'E8', 'A0', 'A1', 'A3', 'M1', 'N1', 'B1', 'B4', 'C1', 'L1', 'L2', 'L3', 'L4', 'I0', 'I1', 'I2', 'I3', 'I3x', 'I4', 'I5', 'I6', 'I7', 'D0', 'D1', 'D2', 'D3')][string]$Experiment)
    $step = { param($Kind, $Role, $Value, $WaitSec) [pscustomobject]@{ Kind = $Kind; Role = $Role; Value = $Value; WaitSec = $WaitSec } }
    $plan = switch ($Experiment) {
        'E0' { @{ Timeout = 420; Steps = @((& $step 'input' 'host' 'w+a:6000' 2), (& $step 'input' 'guest' 'w+a:6000' 2), (& $step 'screens' 'both' 'e0' 0)) } }
        'E1' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'host' 'stage=1 source=fixed apply=warp' 4), (& $step 'screens' 'both' 'e1' 0)) } }
        'E2' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'host' 'stage=2' 12), (& $step 'screens' 'both' 'e2' 0)) } }
        'E3' { @{ Timeout = 240; Steps = @((& $step 'trigger' 'host' 'stage=3' 20), (& $step 'screens' 'both' 'e3' 0)) } }
        'E4' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'host' 'stage=3 source=orbit' 2), (& $step 'screens' 'host' 'e4a' 2), (& $step 'screens' 'host' 'e4b' 2), (& $step 'screens' 'host' 'e4c' 2), (& $step 'screens' 'host' 'e4d' 0)) } }
        'E5' { @{ Timeout = 90; Steps = @((& $step 'trigger' 'host' 'stage=3 source=orbit apply=slot' 8), (& $step 'screens' 'host' 'e5-slot' 2), (& $step 'trigger' 'host' 'apply=both' 8), (& $step 'screens' 'host' 'e5-both' 0)) } }
        'E6' { @{ Timeout = 120; Steps = @((& $step 'trigger' 'both' 'stage=3 source=net' 5), (& $step 'input' 'guest' 'w:4000' 1), (& $step 'screens' 'host' 'e6' 16)) } }
        'E8' { @{ Timeout = 600; Steps = @((& $step 'trigger' 'host' 'stop' 3), (& $step 'screens' 'host' 'e8-stopped' 1), (& $step 'trigger' 'host' 'stage=3' 15), (& $step 'screens' 'host' 'e8-again' 0), (& $step 'manual' 'host' 'Leave the mission, then re-enter Mother Base (30050).' 0)) } }
        'A0' { @{ Timeout = 90; Steps = @((& $step 'trigger' 'host' 'stage=3 source=fixed census=1' 25), (& $step 'trigger' 'host' 'census=1' 3)) } }
        'A1' { @{ Timeout = 120; Steps = @((& $step 'trigger' 'host' $script:PartnerA1Trigger 3), (& $step 'manual' 'host' 'On the host: stand, walk, run, crouch, crouch-walk, prone, crawl (a few seconds each); lab.ini partner_anim_probe=1 logs partner_anim.' 0), (& $step 'screens' 'host' 'a1' 0)) } }
        'A3' { @{ Timeout = 120; Steps = @((& $step 'trigger' 'host' $script:PartnerA3Trigger 3), (& $step 'input' 'host' 'c' 3), (& $step 'screens' 'host' 'a3-crouch' 1), (& $step 'input' 'host' 'c' 3), (& $step 'screens' 'host' 'a3-stand' 0)) } }
        'M1' { @{ Timeout = 90; Steps = @((& $step 'trigger' 'host' 'stage=3 source=orbit apply=xform reset=n' 4), (& $step 'screens' 'host' 'm1a' 0), (& $step 'screens' 'host' 'm1b' 0), (& $step 'screens' 'host' 'm1c' 6)) } }
        'N1' { @{ Timeout = 150; Steps = @((& $step 'trigger' 'both' 'stage=3 source=net apply=hybrid anim=net aset=qs' 5), (& $step 'input' 'guest' 'w:3000' 1), (& $step 'input' 'guest' 'c' 3), (& $step 'screens' 'both' 'n1' 0)) } }
        'B1' { @{ Timeout = 120; Steps = @((& $step 'trigger' 'host' 'stage=3 source=orbit look=quiet' 20), (& $step 'screens' 'host' 'b1' 0)) } }
        'B4' { @{ Timeout = 150; Steps = @((& $step 'trigger' 'both' 'stage=3 source=net look=net' 25), (& $step 'screens' 'both' 'b4' 0)) } }
        'C1' { @{ Timeout = 60; Steps = @((& $step 'manual' 'guest' 'Prepare with lab.json offline.characters.guest=quiet (Prepare-LabInstance.ps1 -Offline), Continue into Mother Base and play as Quiet for 2 minutes.' 0), (& $step 'screens' 'guest' 'c1' 0)) } }
        'L1' { @{ Timeout = 120; Steps = @((& $step 'manual' 'both' 'Start host, then guest (guest reaches Mother Base ~20 s later), both look=net; the look fix is automatic.' 0), (& $step 'screens' 'both' 'l1' 0)) } }
        'L2' { @{ Timeout = 120; Steps = @((& $step 'manual' 'both' 'Reverse order: start guest first, then host; both must load (no stall on either).' 0), (& $step 'screens' 'both' 'l2' 0)) } }
        'L3' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'host' 'look=snake' 1), (& $step 'trigger' 'host' 'look=quiet' 15), (& $step 'screens' 'host' 'l3' 0)) } }
        'L4' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'guest' 'lookgate=0 lookrecover=1' 2), (& $step 'manual' 'guest' 'Join second as in L1 to reproduce the old stall, then the recovery path.' 10), (& $step 'trigger' 'guest' 'lookgate=1' 2), (& $step 'screens' 'guest' 'l4' 0)) } }
        'I0' { @{ Timeout = 90; Steps = @((& $step 'trigger' 'host' 'census=1 padprobe=1' 2), (& $step 'input' 'host' 'w:1500' 1), (& $step 'input' 'host' 's:1500' 1), (& $step 'input' 'host' 'a:1500' 1), (& $step 'input' 'host' 'd:1500' 1), (& $step 'input' 'host' 'c' 2), (& $step 'input' 'host' 'c:1500' 2), (& $step 'screens' 'host' 'i0' 0)) } }
        'I1' { @{ Timeout = 150; Steps = @((& $step 'trigger' 'host' 'stage=3 source=fixed apply=xform reset=n input=pass' 120), (& $step 'screens' 'host' 'i1' 0)) } }
        'I2' { @{ Timeout = 330; Steps = @((& $step 'trigger' 'host' 'stage=3 source=fixed apply=xform reset=n input=neutral cam=detect hold=none' 5), (& $step 'manual' 'host' 'Walk, crouch and open the menu for 5 minutes.' 300), (& $step 'screens' 'host' 'i2' 0)) } }
        'I3' { @{ Timeout = 120; Steps = @((& $step 'trigger' 'host' 'stage=3 source=fixed apply=input correct=0 input=stick stick=180:100 cam=sandbox' 5), (& $step 'screens' 'host' 'i3-180-100' 0), (& $step 'trigger' 'host' 'stick=180:30' 5), (& $step 'trigger' 'host' 'stick=90:100' 5), (& $step 'trigger' 'host' 'stick=0:0' 5), (& $step 'trigger' 'host' 'stick=180:100 cam=detect' 5), (& $step 'screens' 'host' 'i3c' 0)) } }
        'I3x' { @{ Timeout = 40; Steps = @((& $step 'trigger' 'host' 'p218=1 input=stick stick=180:100 cam=sandbox' 20), (& $step 'screens' 'host' 'i3x' 0), (& $step 'manual' 'host' 'Hazardous diagnostic (restart_after=1): quit the game WITHOUT saving after I3x; no other experiment in the same process.' 0)) } }
        'I4' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'host' 'input=stick stick=0:0' 2), (& $step 'trigger' 'host' 'tap=0x2' 2), (& $step 'trigger' 'host' 'tap=0x2' 2), (& $step 'trigger' 'host' 'tap=0x2' 1), (& $step 'trigger' 'host' 'stick=180:100' 3), (& $step 'screens' 'host' 'i4' 0)) } }
        'I5' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'host' 'stage=3 source=fixed apply=xform reset=n input=neutral holdlog=1' 2), (& $step 'manual' 'host' 'Hold the stance key ~1.5 s.' 2), (& $step 'trigger' 'host' 'press=0x2:1200 hold=none holdlog=0' 2), (& $step 'screens' 'host' 'i5' 0)) } }
        'I6' { @{ Timeout = 150; Steps = @((& $step 'trigger' 'both' 'stage=3 source=net apply=input input=net isend=1 cam=sandbox hold=own words=hpr bits=0x2200006 recon=track slot=body look=net' 5), (& $step 'input' 'guest' 'w:3000' 1), (& $step 'input' 'guest' 'c' 3), (& $step 'input' 'host' 'w:3000' 1), (& $step 'screens' 'both' 'i6' 0)) } }
        'I7' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'host' 'input=off isend=0' 3), (& $step 'screens' 'host' 'i7' 0), (& $step 'manual' 'host' 'Re-arm, then leave Mother Base while armed; then quit while armed.' 0)) } }
        'D0' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'host' 'dm=probe' 10), (& $step 'screens' 'host' 'd0' 0)) } }
        'D1' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'host' 'input=off apply=xform reset=n dm=on dmplay=1' 5), (& $step 'screens' 'host' 'd1' 0)) } }
        'D2' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'host' 'source=orbit dmplay=3' 5), (& $step 'trigger' 'host' 'dmstop=1' 2), (& $step 'trigger' 'host' 'dmplay=2' 3), (& $step 'screens' 'host' 'd2' 0)) } }
        'D3' { @{ Timeout = 60; Steps = @((& $step 'trigger' 'host' 'dm=off' 3), (& $step 'screens' 'host' 'd3' 0)) } }
    }
    [pscustomobject]@{ Experiment = $Experiment; Role = (Get-PartnerExperimentDefaultRole -Experiment $Experiment); TimeoutSec = $plan.Timeout; Steps = $plan.Steps }
}


function Invoke-PartnerRound3 {
    # Round 3 (docs/partner-anim-appearance.md "Round 3") experiments L/I/D.
    # Conservative: FAIL on an input fault or a crash attributed to the input
    # window (crash_probe ctx bit0); PASS/INCOMPLETE from the key log lines of
    # section 10; the visual criteria stay manual notes.
    param($Check, $Context, [string]$Experiment, $Alive)
    $has = { param($kind) @(Select-PartnerEntry -Context $Context -Kind $kind).Count -gt 0 }
    # Safety: an AV with ctx bit0 blames the input window.
    foreach ($av in @(Select-PartnerEntry -Context $Context -Kind 'crash_probe=av')) {
        $ctx = Get-PartnerFieldNumber $av 'ctx' -Hex
        if ($null -ne $ctx -and ($ctx -band 1)) { Add-PartnerItem $Check.Failed "crash_probe=av with ctx bit0 (input window): rva=$(Get-PartnerField $av 'rva')" }
    }
    if (& $has 'partner_input=fault') { Add-PartnerItem $Check.Failed 'partner_input=fault (native input disabled)' }
    switch -regex ($Experiment) {
        '^L[1-4]$' {
            if (& $has 'partner_look=timeout') { Add-PartnerItem $Check.Failed 'partner_look=timeout (a look never finished loading)' }
            if (& $has 'partner_look=loaded') { Add-PartnerMatch $Check 'partner_look=loaded' } else { Add-PartnerItem $Check.Missing 'partner_look=loaded on both copies' }
            if ($Experiment -eq 'L4') {
                if (& $has 'partner_look=stall_recover') { Add-PartnerMatch $Check 'partner_look=stall_recover (B path)' } else { Add-PartnerItem $Check.Missing 'partner_look=stall then stall_recover (lookgate=0)' }
            }
            Add-PartnerItem $Check.Notes 'manual: the Snake / Quiet partner is visible on both copies within a few seconds (no timeout, no stall left)'
        }
        '^I0$' {
            if (& $has 'partner_input_census') { Add-PartnerMatch $Check 'partner_input_census' } else { Add-PartnerItem $Check.Missing 'partner_input_census (census=1)' }
            if (& $has 'partner_pad') { Add-PartnerMatch $Check 'partner_pad (padprobe=1)' } else { Add-PartnerItem $Check.Missing 'partner_pad lines (padprobe=1)' }
            Add-PartnerItem $Check.Notes 'manual: verdict=ok, rec1_2ac bit0=0 and rec0_2ac bit0=1; W gives ly~=-1000 with dir_check=ok; set the words default from the stance tap'
        }
        '^I1$' {
            if (& $has 'partner_input=armed') { Add-PartnerMatch $Check 'partner_input=armed; mode=pass' } else { Add-PartnerItem $Check.Missing 'partner_input=armed' }
            if (& $has 'partner_input_hook') { Add-PartnerMatch $Check 'partner_input_hook=installed' } else { Add-PartnerItem $Check.Missing 'partner_input_hook=installed' }
            Add-PartnerItem $Check.Notes 'manual: per_s~=60, root1_calls~=60, skips=0, no AV for 120 s; record hook_thread vs lua_thread'
        }
        '^I[2-5]$' {
            if (& $has 'partner_input=armed') { Add-PartnerMatch $Check 'partner_input=armed' } else { Add-PartnerItem $Check.Missing 'partner_input=armed' }
            if (& $has 'partner_body') { Add-PartnerMatch $Check 'partner_body' }
            Add-PartnerItem $Check.Notes 'manual: see section 10 (I2 cam_dirty/no camera jump; I3 act=2 speed>500 at 180:100; I4 stance holds; I5 holdlog codes)'
        }
        '^I3x$' {
            if (& $has 'partner_input=p218') { Add-PartnerMatch $Check 'partner_input=p218; on=1' } else { Add-PartnerItem $Check.Missing 'partner_input=p218 (needs partner_input_p218_allow=1)' }
            Add-PartnerItem $Check.Notes 'manual: locomotion appears -> start is P+0x218-gated; nothing -> the pad does not reach the Move plugin (D series). HUD/camera glitches are expected; P+0x218 is paired with P+0x214 / the local block (0x11AD05D, 0xFE8F85), so state may be silently wrong afterwards: quit without saving'
        }
        '^I6$' {
            if (& $has 'partner_net=input_rx') { Add-PartnerMatch $Check 'partner_net=input_rx' } else { Add-PartnerItem $Check.Missing 'partner_net=input_rx on the receiver' }
            # Round 4: the body is placed and verified once; a 1 Hz snap cycle is the round 3 sawtooth.
            $placed = @(Select-PartnerEntry -Context $Context -Kind 'partner_recon=placed' | Where-Object { (Get-PartnerField $_ 'ok') -ceq '1' })
            if ($placed.Count) { Add-PartnerMatch $Check 'partner_recon=placed; ok=1' } else { Add-PartnerItem $Check.Missing 'partner_recon=placed; ok=1 (apply=input placement)' }
            $snaps = @(Select-PartnerEntry -Context $Context -Kind 'partner_recon=snap').Count
            if ($snaps -gt 10) { Add-PartnerItem $Check.Failed "partner_recon=snap x$snaps (a snap cycle: see partner_recon=runaway / the 1 Hz partner_recon line)" }
            Add-PartnerItem $Check.Notes 'manual: partner animates in step, stale=0, age_ms<100; heading matches the sender yaw within 300 mrad (else retry yaw=rotneg)'
        }
        '^I7$' {
            if (& $has 'partner_input=disarmed') { Add-PartnerMatch $Check 'partner_input=disarmed' } else { Add-PartnerItem $Check.Missing 'partner_input=disarmed on input=off / leave / quit' }
            Add-PartnerItem $Check.Notes 'manual: an idle partner_pad equals the pre-arm values; a clean quit with 0 crash_probe=av'
        }
        '^D0$' {
            if (& $has 'partner_dm_census') { Add-PartnerMatch $Check 'partner_dm_census; g_ok=1' } else { Add-PartnerItem $Check.Missing 'partner_dm_census (dm=probe)' }
            if (& $has 'partner_dm') { Add-PartnerMatch $Check 'partner_dm (idle probe)' }
        }
        '^D[1-3]$' {
            if (& $has 'partner_dm=unconsumed') { Add-PartnerItem $Check.Failed 'partner_dm=unconsumed (id 1 never polls DM slot 0; the DM lever is dead)' }
            if (& $has 'partner_dm=conflict') { Add-PartnerItem $Check.Failed 'partner_dm=conflict (host DM or camera connect while the partner DM was active)' }
            if (& $has 'partner_dm=refused; reason=cam_connect') { Add-PartnerItem $Check.Notes 'partner_dm=refused; reason=cam_connect (host camera connect set: run D1 in free roam)' }
            if (& $has 'partner_dm=patched') { Add-PartnerMatch $Check 'partner_dm=patched' } else { Add-PartnerItem $Check.Missing 'partner_dm=patched (dm=on)' }
            Add-PartnerItem $Check.Notes 'manual: the partner plays the requested gani (D1 crouch idle; D2 crouch-walk loop then prone); partner_dm keeps cam1f0=0x0 cam200=0 and the host camera does not jump; D3 reverts cleanly with 0 AV'
        }
    }
}

Export-ModuleMember -Function *-*
