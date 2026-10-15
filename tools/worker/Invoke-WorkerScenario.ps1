#Requires -Version 7
<#
.SYNOPSIS
Runs one scripted test of both MGSV copies on a private, never-displayed desktop.
.DESCRIPTION
The single entry point for background game tests (AGENTS.md "Background agent
development"): audited worker checks, Start-WorkerSession (fresh probe, owned
install, copied saves, private desktop), the scenario steps through
Send-WorkerCommand only, then Stop-WorkerSession in a finally block, so the
baseline loader and configuration are restored and the real saves re-checked
even when a step fails or the session times out. Nothing is shown on, focused
on or sent to the user's desktop.

Scenario: one step per line (a file via -ScenarioPath or lines via -Step);
blank lines and lines starting with # are ignored. Roles: host, guest, both.

  title [timeoutSec]               press Enter on each role until it reports gameplay ready
  <role> press <keys> [ms]         keys joined by +: w a s d enter space escape e r shift ctrl 1 2 f
                                   c q x z tab lmb rmb (c = crouch, hold c ~0.3 s = prone)
  <role> hold <keys> <ms> [nowait] hold for 1..1500 ms (repeat the step for longer holds);
                                   nowait continues at once (e.g. to capture mid-movement)
  <role> mouse <dx> <dy>           relative mouse move, each -1500..1500
  <role> capture [label]           save the role's next rendered frame (BMP in the run folder);
                                   every capture also appends the role's partner telemetry to captures.jsonl
  <role> burst <label> <n> <ms>    n captures (2..30) every ms milliseconds (>= 100): label-01, label-02, ...
  <role> release                   clear the role's bounded simulated input
  stance <role> stand|crouch|prone set posture using C, requiring fresh local animation readback
  heldcapture <source> <viewer> <keys> <label> <n>
                                   renew a 1500 ms hold before each of 1..5 captures; always release
  aimturn <source> <viewer> <dx> <label>
                                   aim, move the mouse, require a body-heading change of at least 70 degrees,
                                   capture the pivot and its later idle state; dx is +-1500, the only
                                   qualified stimulus (dx 700 and 900 never turned a settled aimed body:
                                   runs fca0d6dc, 1bffe18a, 14ab2e00, 8919e533; dx 1500 turned 72..87 degrees
                                   in runs 4b91a2f5, fac92b9b, d3e07ce6, 1a2de304)
  resetpose <role>                 record the first fresh position/yaw, then return to that benchmark
                                   through the existing debug teleport and verify arrival/settling
  <role> debug <command>           one F9 debug-menu command through the game's own queue;
                                   waits for and prints its result
  <role> run <command>             like debug, but paired by command id (or overlay serial for
                                   "menu ..." requests) and stored as the role's last result;
                                   menu open [cat] | close | toggle | category <cat> | search [text]
                                   | press <action_id> | confirm | cancel | footer 0|1 | select <hash>
                                   drive the worker-drawn F9 overlay (worker builds only)
  <role> expect <regex>            the role's last run result must match; a mismatch is recorded in
                                   ExpectFailures and the scenario continues
  <role> require <regex>           same check, but a mismatch stops all following steps and cleans up
  <role> worldtrigger world=0      lab-only scoped world-disable cleanup test (host or guest);
                                   same owned private-run/plain-path/applied-log checks as travel
  waitmission <role> <mission> <timeoutSec> wait for a fresh loaded destination;
                                   guarded120ms Space resumes both owned loading roles, at most
                                   12 attempts per allocation, every5s; runtime/termination refuse
  wait <ms>                        sleep up to 60000 ms
  waitgameplay <role> <timeoutSec>   poll until the role is on foot in gameplay (debug guard passes);
                                     every 6 s presses E only for a verified helicopter state,
                                     otherwise escape then enter to try skipping a cutscene
  placebehind <viewer> <target> <back_m> [side_m]
                                     teleport viewer behind (and to the right of) target, facing it,
                                     with a 3 m upward margin; require fresh position to settle within 3 s
  state                            print both roles' mission / gameplay-ready state
  labsetting [host|guest] <key>=<value>
                                   before the session starts: add an allow-listed lab.ini key to both
                                   copies or to one (debug_soldier_warp=0|1, nametag=0|1,
                                   nametag_crosshair=aim|always|off, nametag_name=[A-Za-z0-9_]{1,16};
                                   partner_native_face=off|idle|aim; partner_native_aim=looksrc and
                                   partner_native_aimpt=off|look|point (the aim-target mirror; the gun stage
                                   emits look), partner_native_reload=mirror|raw|event,
                                   partner_native_slot=off|write|call, partner_native_equip=stock46|manifest|substitute
                                   (the reload/switch/writer A/B runs), native gun stage only;
                                   steam_selftest=0|1|2, lab only, never sends to a peer;
                                   online play A/B: partner_native_net=lab|online, partner_native_stall=neutral|hold,
                                   partner_native_hold=own|net, partner_input_edge_repeats=2..8;
                                   steam_selftest_fn=0|1, the self-test's function-pointer stage F/D;
                                   coop_hud=compact|full|off, the in-game co-op status line and toasts;
                                   the seated vehicle rider: partner_native_ride=off|hide|seat,
                                   partner_ride_seat_enter=direct|getin, partner_ride_seat_exit=stop|getoff;
                                   partner_native_overlay=off|debug|map, the DM overlay lever (native profile) and
                                   partner_act_log=0|1, the partner_act census lines, partner_native_stance_fast=0|1, the
                                   native stance window (0: the previous 2 s rule); docs/anim-round1-actions.md)

The session has a hard 600 s limit (supervisor). The result object lists the
run folder, the frames (renamed with labels) and whether everything closed.
.EXAMPLE
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -Step 'title','both capture start','host hold w 1500','host capture walked'
#>
[CmdletBinding()]
param(
    [string]$ScenarioPath,
    [string[]]$Step=@(),
    [string]$CheckReceipt,
    [ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$BaselineLoaderSha256,
    [ValidateRange(60,600)][int]$TimeoutSec=600,
    [ValidateRange(10,180)][int]$ReadyTimeoutSec=180,
    [ValidateSet('animated','input','native')][string]$PartnerProfile='animated',
    # Native puppet (docs/native-puppet.md 13): census|loco|gun (default loco).
    [ValidateSet('','census','loco','gun')][string]$NativeStage='',
    # research: the stock46 Research worker build (gun stage only).
    [ValidateSet('normal','research','buddy-capacity','mgo-sleep-rest','mgo-unique-rest','mgo-standing-cycle')][string]$BuildProfile='normal',
    [string]$BuddyCapacityArchive,
    [string]$MgoSleepRestArchive,
    [string]$MgoUniqueRestArchive,
    [string]$MgoStandingCycleArchive,
    [switch]$StandingLookupObserver,
    [switch]$StandingFrameObserver,
    [switch]$NativeActivationObserver,
    [switch]$AmmoExperiment,
    [ValidateSet('real','lab')][string]$SaveSeed='lab',
    # Optional quality preset, applied only to this run's fresh role save copies.
    [ValidateSet('preserve','low')][string]$GraphicsQuality='preserve',
    # key=value for both copies or host:key=value / guest:key=value for one (scenario "labsetting" lines).
    [ValidatePattern('^((host|guest):)?(debug_soldier_warp=[01]|nametag=[01]|nametag_crosshair=(aim|always|off)|nametag_name=[A-Za-z0-9_]{1,16}|partner_native_face=(off|idle|aim)|partner_native_aim=looksrc|partner_native_reload=(mirror|raw|event)|partner_native_slot=(off|write|call)|partner_native_equip=(stock46|manifest|substitute)|partner_native_equip_shared=(off|on|prefer)|partner_native_equip_measured=[01]|partner_native_standin=(off|on)|partner_native_ride=(off|hide|seat)|partner_ride_seat_enter=(direct|getin)|partner_ride_seat_exit=(stop|getoff)|partner_native_aimpt=(off|look|point)|steam_selftest=[012]|steam_selftest_fn=[01]|coop_hud=(compact|full|off)|partner_native_net=(lab|online)|partner_native_stall=(neutral|hold)|partner_native_hold=(own|net)|partner_input_edge_repeats=[2-8]|partner_native_unsupported=[01]|partner_native_dry_stop=[01]|partner_native_aim_intent=(pad|state)|partner_native_reload_rate=(off|adapt)|partner_fire_census=[01]|partner_native_overlay=(off|debug|map)|partner_act_log=[01]|partner_native_stance_fast=[01])$')][string[]]$LabSetting=@(),
    # lab (default) or play: the play rehearsal (docs/two-pc-play.md "Hidden play rehearsal"):
    # both copies in mode=play as Play-Coop writes it, on 127.0.0.1, real identity.
    [ValidateSet('lab','play')][string]$Mode='lab',
    # A lab.json wanSim preset on both copies (docs/native-puppet.md "Online").
    [ValidateSet('off','good','bad','awful')][string]$WanSim='off'
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$worker=$PSScriptRoot
$build=Join-Path $repo 'build\worker-checks\normal-worker-on-release'
Import-Module (Join-Path $worker 'WorkerSessionCommon.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $worker 'NativeActivationProfile.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $repo 'tools\lab\WorldSync.psm1') -Force -DisableNameChecking
if($PartnerProfile -cne 'native' -and $NativeStage){throw '-NativeStage needs -PartnerProfile native.'}
Assert-WorkerMgoSleepRestScope $BuildProfile $Mode $PartnerProfile $NativeStage ([bool]$AmmoExperiment) $MgoSleepRestArchive $BuddyCapacityArchive
Assert-WorkerMgoUniqueRestScope $BuildProfile $Mode $PartnerProfile $NativeStage ([bool]$AmmoExperiment) $MgoUniqueRestArchive $BuddyCapacityArchive $MgoSleepRestArchive
Assert-WorkerMgoStandingCycleScope $BuildProfile $Mode $PartnerProfile $NativeStage ([bool]$AmmoExperiment) $MgoStandingCycleArchive $BuddyCapacityArchive $MgoSleepRestArchive $MgoUniqueRestArchive
if($BuildProfile -ceq 'mgo-standing-cycle' -and $SaveSeed -cne 'lab'){throw 'Standing cycle tests require copied lab saves.'}
if($StandingLookupObserver -and $BuildProfile -cne 'mgo-standing-cycle'){throw '-StandingLookupObserver needs the exact standing-cycle profile.'}
if($StandingFrameObserver -and ($BuildProfile -cne 'mgo-standing-cycle' -or !$StandingLookupObserver)){throw '-StandingFrameObserver needs the exact standing-cycle profile and -StandingLookupObserver.'}
Assert-WorkerNativeActivationScope ([bool]$NativeActivationObserver) $BuildProfile $Mode $PartnerProfile $NativeStage $SaveSeed ([bool]$AmmoExperiment)
if($BuildProfile -cin @('research','mgo-sleep-rest','mgo-unique-rest','mgo-standing-cycle') -and !($PartnerProfile -ceq 'native' -and $NativeStage -ceq 'gun')){throw '-BuildProfile research is only for -NativeStage gun.'}
if($PartnerProfile -ceq 'native' -and $NativeStage -ceq 'gun' -and $BuildProfile -cnotin @('research','mgo-sleep-rest','mgo-unique-rest','mgo-standing-cycle')){throw '-NativeStage gun needs -BuildProfile research.'}
$researchExperiments=@()
if($BuildProfile -cin @('research','mgo-sleep-rest','mgo-unique-rest','mgo-standing-cycle')){
    $researchExperiments=if($BuildProfile -ceq 'mgo-standing-cycle'){@(Get-WorkerMgoStandingCycleExperiments -LookupObserver:$StandingLookupObserver -FrameObserver:$StandingFrameObserver)}elseif($BuildProfile -ceq 'mgo-unique-rest'){@(Get-WorkerMgoUniqueRestExperiments)}else{@(Get-WorkerResearchExperiments -Ammo:$AmmoExperiment)}
    $researchExperiments=@(Add-WorkerNativeActivationExperiment $researchExperiments ([bool]$NativeActivationObserver))
    $build=Get-WorkerResearchBuildDirectory $repo $researchExperiments
} elseif($BuildProfile -ceq 'buddy-capacity'){
    if($Mode -cne 'lab' -or $AmmoExperiment -or !($PartnerProfile -ceq 'native' -and $NativeStage -cin @('census','loco')) -or !$BuddyCapacityArchive){
        throw 'Buddy capacity needs explicit archive, copied-save lab mode and native census/loco; no ammo/gun experiment.'
    }
    $researchExperiments=@('MGS_LOCAL_BUDDY_CAPACITY_TEST')
    $researchExperiments=@(Add-WorkerNativeActivationExperiment $researchExperiments ([bool]$NativeActivationObserver))
    $build=Get-WorkerResearchBuildDirectory $repo $researchExperiments
} elseif($BuddyCapacityArchive){throw '-BuddyCapacityArchive needs -BuildProfile buddy-capacity.'}

$lines=@()
if($ScenarioPath){$lines+=[IO.File]::ReadAllLines([IO.Path]::GetFullPath($ScenarioPath))}
$lines+=$Step
$steps=@($lines|ForEach-Object{$_.Trim()}|Where-Object{$_ -and !$_.StartsWith('#')})
# labsetting lines configure the session; they are not steps.
foreach($line in @($steps|Where-Object{$_ -cmatch '^labsetting(\s|$)'})) {
    if($line -cnotmatch '^labsetting\s+(?:(host|guest)\s+)?(debug_soldier_warp=[01]|nametag=[01]|nametag_crosshair=(?:aim|always|off)|nametag_name=[A-Za-z0-9_]{1,16}|partner_native_face=(?:off|idle|aim)|partner_native_aim=looksrc|partner_native_reload=(?:mirror|raw|event)|partner_native_slot=(?:off|write|call)|partner_native_equip=(?:stock46|manifest|substitute)|partner_native_equip_shared=(?:off|on|prefer)|partner_native_equip_measured=[01]|partner_native_standin=(?:off|on)|partner_native_ride=(?:off|hide|seat)|partner_ride_seat_enter=(?:direct|getin)|partner_ride_seat_exit=(?:stop|getoff)|partner_native_aimpt=(?:off|look|point)|steam_selftest=[012]|steam_selftest_fn=[01]|coop_hud=(?:compact|full|off)|partner_native_net=(?:lab|online)|partner_native_stall=(?:neutral|hold)|partner_native_hold=(?:own|net)|partner_input_edge_repeats=[2-8]|partner_native_unsupported=[01]|partner_native_dry_stop=[01]|partner_native_aim_intent=(?:pad|state)|partner_native_reload_rate=(?:off|adapt)|partner_fire_census=[01]|partner_native_overlay=(?:off|debug|map)|partner_act_log=[01]|partner_native_stance_fast=[01])$'){
        throw "Invalid scenario step: '$line'"
    }
    $LabSetting=@($LabSetting)+@($(if($Matches[1]){$Matches[1]+':'+$Matches[2]}else{$Matches[2]}))
}
$steps=@($steps|Where-Object{$_ -cnotmatch '^labsetting(\s|$)'})
if(@($LabSetting|Where-Object{$_ -clike 'partner_native_*'}).Count -and $PartnerProfile -cne 'native'){throw '-LabSetting partner_native_* needs -PartnerProfile native.'}
if($LabSetting -ccontains 'partner_native_aim=looksrc' -and $NativeStage -cne 'gun'){throw '-LabSetting partner_native_aim=looksrc needs -NativeStage gun.'}
if(@($LabSetting|Where-Object{$_ -cmatch '^((host|guest):)?partner_native_aimpt='}).Count -and $NativeStage -cne 'gun'){throw '-LabSetting partner_native_aimpt needs -NativeStage gun.'}
# The partner's own reload and weapon switch A/B keys (docs/native-puppet.md "Weapons"): gun stage only.
if(@($LabSetting|Where-Object{($_ -creplace '^(host|guest):','') -cmatch '^partner_native_(reload|slot|equip|equip_shared|equip_measured|standin)='}).Count -and $NativeStage -cne 'gun'){
    throw '-LabSetting partner_native_reload / partner_native_slot / partner_native_equip need -NativeStage gun.'
}
if($Mode -ceq 'play' -and @($LabSetting|Where-Object{$_ -cmatch '(^|:)partner_native_equip_measured=1$'}).Count) {
    throw 'Measured optional equipment acceptance is lab-only.'
}
if(!$steps.Count){throw 'No scenario steps.'}
# Validate every step before anything starts.
$roleNames=@('host','guest','both')
$keyNames=@('w','a','s','d','enter','space','escape','e','r','shift','ctrl','1','2','f','c','q','x','z','tab','lmb','rmb')
function Test-ScenarioKeys([string]$Keys){
    $parts=$Keys.Split('+')
    return $parts.Count -gt 0 -and @($parts|Where-Object{$keyNames -cnotcontains $_}).Count -eq 0 -and
        @($parts|Sort-Object -Unique).Count -eq $parts.Count
}
function Test-ScenarioLabel([string]$Label){return $Label -cmatch '^[A-Za-z0-9_-]{1,64}$'}
foreach($line in $steps){
    $t=$line -split '\s+'
    $ok=switch -Regex ($t[0]) {
        '^title$' {$t.Count -eq 1 -or ($t.Count -eq 2 -and [int]$t[1] -ge 5 -and [int]$t[1] -le 400)}
        '^wait$' {$t.Count -eq 2 -and [int]$t[1] -ge 0 -and [int]$t[1] -le 60000}
        '^state$' {$t.Count -eq 1}
        '^waitmission$' {$t.Count -eq 4 -and $t[1] -cin 'host','guest' -and $t[2] -cin '30010','30020','30050','40010','40020','40050' -and [int]$t[3] -ge 5 -and [int]$t[3] -le 400}
        '^waitgameplay$' {$t.Count -eq 3 -and $t[1] -in 'host','guest' -and [int]$t[2] -ge 5 -and [int]$t[2] -le 400}
        '^placebehind$' {$t.Count -in 4,5 -and $t[1] -in 'host','guest' -and $t[2] -in 'host','guest' -and $t[1] -ne $t[2] -and [double]$t[3] -ge 1 -and [double]$t[3] -le 40}
        '^stance$' {$t.Count -eq 3 -and $t[1] -cin 'host','guest' -and $t[2] -cin 'stand','crouch','prone'}
        '^heldcapture$' {$t.Count -eq 6 -and $t[1] -cin 'host','guest' -and $t[2] -cin 'host','guest' -and
            (Test-ScenarioKeys $t[3]) -and (Test-ScenarioLabel $t[4]) -and [int]$t[5] -ge 1 -and [int]$t[5] -le 5}
        '^aimturn$' {$t.Count -eq 5 -and $t[1] -cin 'host','guest' -and $t[2] -cin 'host','guest' -and
            [Math]::Abs([int]$t[3]) -eq 1500 -and (Test-ScenarioLabel $t[4])}
        '^resetpose$' {$t.Count -eq 2 -and $t[1] -cin 'host','guest'}
        default {
            $roleNames -ccontains $t[0] -and $t.Count -ge 2 -and $(switch($t[1]){
                'press'{$t.Count -in 3,4 -and (Test-ScenarioKeys $t[2]) -and ($t.Count -eq 3 -or ([int]$t[3] -ge 1 -and [int]$t[3] -le 1500))}
                'hold'{($t.Count -eq 4 -or ($t.Count -eq 5 -and $t[4] -ceq 'nowait')) -and (Test-ScenarioKeys $t[2]) -and [int]$t[3] -ge 1 -and [int]$t[3] -le 1500}
                'mouse'{$t.Count -eq 4 -and [int]$t[2] -ge -1500 -and [int]$t[2] -le 1500 -and [int]$t[3] -ge -1500 -and [int]$t[3] -le 1500 -and ([int]$t[2] -ne 0 -or [int]$t[3] -ne 0)}
                'release'{$t.Count -eq 2}; 'capture'{$t.Count -eq 2 -or ($t.Count -eq 3 -and (Test-ScenarioLabel $t[2]))}; 'debug'{$t.Count -ge 3}
                'worldtrigger'{$Mode -ceq 'lab' -and (
                    ($t[0] -cin 'host','guest' -and $t.Count -eq 3 -and $t[2] -ceq 'world=0') -or
                    ($t[0] -ceq 'host' -and $t.Count -in 3,4 -and $t[2] -cmatch '^goto=(30010|30020)$' -and
                        ($t.Count -eq 3 -or $t[3] -cmatch '^goto_lz=[01]$')) -or
                    ($t.Count -eq 3 -and (($t[0] -ceq 'host' -and $t[2] -cmatch '^world_probe=(shared_checkpoint|host_continue)$') -or
                        ($t[0] -ceq 'guest' -and $t[2] -ceq 'world_probe=shared_death'))))}
                'run'{$t.Count -ge 3 -and $t[0] -cne 'both'}
                'expect'{$t.Count -ge 3 -and $t[0] -cne 'both' -and $(try{$null=[regex]::new((($t|Select-Object -Skip 2) -join ' '));$true}catch{$false})}
                'require'{$t.Count -ge 3 -and $t[0] -cne 'both' -and $(try{$null=[regex]::new((($t|Select-Object -Skip 2) -join ' '));$true}catch{$false})}
                'burst'{$t.Count -eq 5 -and (Test-ScenarioLabel $t[2]) -and [int]$t[3] -ge 2 -and [int]$t[3] -le 30 -and [int]$t[4] -ge 100 -and [int]$t[4] -le 5000}; default{$false}})
        }
    }
    if(!$ok){throw "Invalid scenario step: '$line'"}
}

if(!$BaselineLoaderSha256){
    $record=Get-Content -LiteralPath 'D:\Games\steamapps\common\MGS_TPP\mgs-coop\probe-install.json' -Raw|ConvertFrom-Json
    $BaselineLoaderSha256=[string]$record.dll_sha256
}
if(!$CheckReceipt){
    $out=if($BuildProfile -cin @('research','buddy-capacity','mgo-sleep-rest','mgo-unique-rest','mgo-standing-cycle')){@(& (Join-Path $worker 'Invoke-WorkerChecks.ps1') -Profile Research -EnableExperiment $researchExperiments -EnableBackgroundWorker 2>&1)}
        else{@(& (Join-Path $worker 'Invoke-WorkerChecks.ps1') -EnableBackgroundWorker 2>&1)}
    $CheckReceipt=@($out|ForEach-Object{[string]$_}|Select-String -Pattern 'Worker check receipt: (.+receipt\.json)'|ForEach-Object{$_.Matches[0].Groups[1].Value})[-1]
    if(!$CheckReceipt){throw "Worker checks produced no receipt:`n$($out -join "`n")"}
}
$receipt=Get-Content -LiteralPath $CheckReceipt -Raw|ConvertFrom-Json
if($receipt.status -cne 'passed'){throw "Worker checks did not pass: $($receipt.failure)"}

$session=& (Join-Path $worker 'Start-WorkerSession.ps1') -BuildDirectory $build -CheckReceipt $CheckReceipt -BaselineLoaderSha256 $BaselineLoaderSha256 -TimeoutSec $TimeoutSec -ReadyTimeoutSec $ReadyTimeoutSec -PartnerProfile $PartnerProfile -NativeStage $NativeStage -BuildProfile $BuildProfile -BuddyCapacityArchive $BuddyCapacityArchive -MgoSleepRestArchive $MgoSleepRestArchive -MgoUniqueRestArchive $MgoUniqueRestArchive -MgoStandingCycleArchive $MgoStandingCycleArchive -StandingLookupObserver:$StandingLookupObserver -StandingFrameObserver:$StandingFrameObserver -NativeActivationObserver:$NativeActivationObserver -AmmoExperiment:$AmmoExperiment -SaveSeed $SaveSeed -GraphicsQuality $GraphicsQuality -LabSetting $LabSetting -Mode $Mode -WanSim $WanSim
$session=@($session|Where-Object{$_ -isnot [string]})[-1]
$run=[string]$session.RunDirectory
$runId=[string]$session.RunId
$frames=[Collections.Generic.List[object]]::new();$failure=$null;$stop=$null
$releaseFailures=[Collections.Generic.List[string]]::new()
$lastFrameAt=@{host=[uint64]0;guest=[uint64]0}
$lastResult=@{host='';guest=''}
$expectations=[Collections.Generic.List[string]]::new();$expectFailures=[Collections.Generic.List[string]]::new()
$poseAnchors=@{}
$loadingAttempts=@{}
$continueRequirements=@{}
function Invoke-ScenarioWorldTravel([string]$Text,[string]$Role='host'){
    if($Mode -cne 'lab'){throw 'Worker world travel is lab-only.'}
    # Validate the live owned process/context lease and release virtual keys before transition.
    $null=Send-Step $Role @{Operation='release'}
    $canonical=ConvertTo-WorldTriggerText -Text $Text
    $travel=$Role -ceq 'host' -and $canonical -cmatch '^goto=(30010|30020)( goto_lz=[01])?$'
    $recovery=($Role -ceq 'host' -and $canonical -cmatch '^world_probe=(shared_checkpoint|host_continue)$') -or
        ($Role -ceq 'guest' -and $canonical -ceq 'world_probe=shared_death')
    $disable=$Role -cin 'host','guest' -and $canonical -ceq 'world=0'
    if(!$travel -and !$recovery -and !$disable){throw 'Worker world travel accepts only free-roam destinations, role-bound recovery probes or exact world=0.'}
    $owned=Read-WorkerSessionJson (Join-Path $run 'session.json')
    if($owned['run_id'] -cne $runId -or $owned['status'] -cne 'ready'){throw 'Worker travel session ownership differs.'}
    $mod=Join-Path $owned['roles'][$Role]['game'] 'mgs-coop'
    $null=Assert-WorkerPlainPath $mod -MustExist
    $log=Join-Path $mod 'script-observer.log'
    $null=Assert-WorkerPlainPath $log -MustExist
    $offset=(Get-Item -LiteralPath $log).Length
    $trigger=Join-Path $mod 'world.trigger'
    $null=Assert-WorkerPlainPath $trigger
    if(Test-Path -LiteralPath $trigger){throw 'Another world trigger is pending.'}
    # Capture both current allocations before submission; game-over need not
    # report gameplay_ready. A later same-mission wait must observe a new one.
    $captured=if($Role -ceq 'host' -and $canonical -ceq 'world_probe=host_continue'){
        Read-ContinueGenerations
    }else{$null}
    Write-WorkerAtomicText $trigger $canonical
    $limit=[Environment]::TickCount64+15000
    while([Environment]::TickCount64 -lt $limit){
        $stream=[IO.File]::Open($log,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        try{
            if($stream.Length -lt $offset){throw 'Worker travel log rotated before its receipt.'}
            $null=$stream.Seek($offset,[IO.SeekOrigin]::Begin)
            $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8,$true,4096,$true)
            try{$text=$reader.ReadToEnd()}finally{$reader.Dispose()}
        }finally{$stream.Dispose()}
        $result=Find-WorldTriggerResult -LogText $text -Canonical $canonical
        if($result.State -ceq 'applied'){
            if($captured){foreach($r in 'host','guest'){$continueRequirements[$r]=$captured[$r]}}
            Write-Host "  $Role world travel: $canonical applied";return
        }
        if($result.State -cin 'rejected','different'){throw "Worker world travel refused: $($result.Reason)"}
        Start-Sleep -Milliseconds 250
    }
    throw 'Worker world travel had no confirmed receipt.'
}
function Assert-ScenarioResult([string]$Role,[string]$Pattern,[string]$Actual,[bool]$Required){
    $verb=if($Required){'require'}else{'expect'}
    $line="$Role $verb $Pattern <- $Actual"
    $expectations.Add($line)
    if($Actual -cnotmatch $Pattern){
        $expectFailures.Add($line)
        if($Required){throw "Required scenario check failed: $line"}
        Write-Warning "  expectation failed: $line"
    }else{Write-Host "  expectation met: $Pattern"}
}
function Read-RoleState([string]$Role){
    # Early state files lack later fields; always return the three used here.
    $s=try{Get-Content -LiteralPath (Join-Path $run "$Role.state.json") -Raw|ConvertFrom-Json -AsHashtable}catch{$null}
    if(!$s){$s=@{}}
    [pscustomobject]@{mission=$s['mission'];gameplay_ready=[bool]$s['gameplay_ready'];generation=$s['generation']}
}
function Send-Step([string]$Role,[hashtable]$Arguments){
    $result=& (Join-Path $worker 'Send-WorkerCommand.ps1') -RunDirectory $run -Role $Role @Arguments
    return @($result|Where-Object{$_ -isnot [string]})[-1]
}
function Read-RoleRaw([string]$Role){
    try{Get-Content -LiteralPath (Join-Path $run "$Role.state.json") -Raw|ConvertFrom-Json -AsHashtable}catch{$null}
}
function Read-RoleLoading([string]$Role,[string]$Phase='initialized'){
    try{
        $path=Join-Path $run "$Role.loading.json"
        $file=Get-Item -LiteralPath $path -Force
        if($file.Length -gt 1024 -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)){return $null}
        $s=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json -AsHashtable
        if($s['run_id'] -cne $runId -or $s['role'] -cne $Role -or $s['phase'] -cne $Phase -or
           $s['mission'] -notin 30010,30020,30050,40010,40020,40050 -or ![uint32]$s['generation']){return $null}
        return $s
    }catch{return $null}
}
function Resume-TripLoading {
    # Each role validates its fresh supervisor/process lease in Send-Step.
    # This metadata is a candidate only: native input consumption must still
    # be in the same allocation before it can issue its fixed120ms Space.
    foreach($r in 'host','guest'){
        $now=Get-ScenarioTick;$live=Read-RoleRaw $r
        if($live -and $live['run_id'] -ceq $runId -and $live['role'] -ceq $r -and
           $live['gameplay_ready'] -and !$live['terminating'] -and $live.ContainsKey('tick_ms') -and
           [int64]$live['tick_ms'] -le $now -and $now-[int64]$live['tick_ms'] -le 1000){continue}
        $loading=Read-RoleLoading $r
        if(!$loading){continue}
        $key=$r+':'+[string]$loading['generation'];$now=Get-ScenarioTick
        if(!$loadingAttempts.ContainsKey($key)){$loadingAttempts[$key]=@{count=0;last=[int64]-5000}}
        $attempt=$loadingAttempts[$key]
        if($attempt.count -ge 12 -or $now-$attempt.last -lt 5000){continue}
        $attempt.count++;$attempt.last=$now
        try{
            $queued=Send-Step $r @{Operation='resume_loading'}
            Write-Host "  $r guarded loading resume queued sequence=$($queued.sequence) generation=$($loading['generation'])"
        }catch{Write-Host "  $r guarded loading resume refused: $($_.Exception.Message)"}
    }
}
function Get-ScenarioWaitAction([string]$Role,[string]$Result,[Collections.IDictionary]$State,[int64]$Now){
    # E is the observed helicopter disembark prompt, never a generic skip.
    # Exact debug guard plus fresh owned animation is required on each attempt.
    $tick=[int64]0;$act=[uint32]0
    if($Result -cne 'helicopter' -or !$State -or $Role -cnotin 'host','guest' -or
       $State['run_id'] -cne $runId -or $State['role'] -cne $Role -or
       !$State['gameplay_ready'] -or $State['terminating'] -or !$State['local_anim_valid'] -or
       $State['mission'] -notin 30010,30020,30050 -or
       !$State.Contains('local_act') -or [string]$State['local_act'] -cnotmatch '^[0-9]{1,3}$' -or
       ![uint32]::TryParse([string]$State['local_act'],[ref]$act) -or $act -lt 98 -or $act -gt 108 -or
       !$State.Contains('tick_ms') -or ![int64]::TryParse([string]$State['tick_ms'],[ref]$tick) -or
       $tick -lt 0 -or $tick -gt $Now -or $Now-$tick -gt 1000){return 'skip'}
    return 'e'
}
function Read-ContinueGenerations {
    $captured=@{}
    foreach($r in 'host','guest'){
        $s=Read-RoleRaw $r;$now=Get-ScenarioTick
        $generation=[uint32]0;$tick=[int64]0
        if(!$s -or $s['run_id'] -cne $runId -or $s['role'] -cne $r -or $s['terminating'] -or
           $s['mission'] -notin 30010,30020,30050,40010,40020,40050 -or
           !$s.ContainsKey('generation') -or [string]$s['generation'] -cnotmatch '^[1-9][0-9]{0,9}$' -or
           ![uint32]::TryParse([string]$s['generation'],[ref]$generation) -or
           !$s.ContainsKey('tick_ms') -or ![int64]::TryParse([string]$s['tick_ms'],[ref]$tick) -or
           $tick -lt 0 -or $tick -gt $now -or $now-$tick -gt 1000){
            throw "[$r] Continue requires a fresh owned mission generation."
        }
        $captured[$r]=@{mission=[int]$s['mission'];generation=$generation}
    }
    return $captured
}
function Wait-ScenarioMission([string]$Role,[int]$Mission,[int]$TimeoutSec){
    # Helicopter space (40010/40020/40050) never reports on-foot gameplay_ready, so
    # its arrival is the owned loading metadata entering the runtime phase for that
    # mission (runtime_enter), after the same guarded resume passed its tips screen.
    $heli=$Mission -ge 40000
    $limit=(Get-ScenarioTick)+1000*$TimeoutSec;$arrived=$false
    do{
        $s=if($heli){Read-RoleLoading $Role 'running'}else{Read-RoleRaw $Role};$now=Get-ScenarioTick;$generation=[uint32]0
        $validGeneration=$s -and $s.ContainsKey('generation') -and
            [string]$s['generation'] -cmatch '^[1-9][0-9]{0,9}$' -and
            [uint32]::TryParse([string]$s['generation'],[ref]$generation)
        $newAllocation=!$continueRequirements.ContainsKey($Role) -or
            $continueRequirements[$Role].mission -ne $Mission -or
            ($validGeneration -and $generation -gt $continueRequirements[$Role].generation)
        if($s -and $s['run_id'] -ceq $runId -and $s['role'] -ceq $Role -and
            [int]$s['mission'] -eq $Mission -and ($heli -or ($s['gameplay_ready'] -and !$s['terminating'])) -and
            $validGeneration -and $newAllocation -and
            $s.ContainsKey('tick_ms') -and [int64]$s['tick_ms'] -le $now -and ($heli -or $now-[int64]$s['tick_ms'] -le 1000)){
            $null=$continueRequirements.Remove($Role);$arrived=$true;break
        }
        Resume-TripLoading
        Start-Sleep -Milliseconds 250
    }while((Get-ScenarioTick) -lt $limit)
    if(!$arrived){throw "[$Role] destination $Mission did not load."}
    Write-Host "  $Role loaded mission $Mission"
}
function Read-FreshRole([string]$Role){
    $s=Read-RoleRaw $Role;$now=[Environment]::TickCount64
    if(!$s -or $s['run_id'] -cne $runId -or $s['role'] -cne $Role -or !$s['gameplay_ready'] -or $s['terminating'] -or
       !$s['mission'] -or !$s['generation'] -or !$s.ContainsKey('tick_ms') -or [uint64]$s['tick_ms'] -gt $now -or
       $now-[uint64]$s['tick_ms'] -gt 1000){throw "[$Role] requires fresh owned on-foot telemetry."}
    return $s
}
function Read-NativeReceipt([string]$Role,[uint64]$Sequence,[string[]]$Events){
    $path=Join-Path $run "$Role.worker.jsonl"
    $stream=$null
    try{
        # The native append handle remains open for the entire game session.
        # ReadAllLines denies sharing that existing writer; this read-only
        # handle must permit it. Read a bounded snapshot, ignoring a partial tail.
        $stream=[IO.FileStream]::new($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        $length=$stream.Length
        if($length -gt 8388608){throw 'Native receipt log exceeds its bounded size.'}
        $bytes=[byte[]]::new([int]$length);$read=0
        while($read -lt $bytes.Length){
            $count=$stream.Read($bytes,$read,$bytes.Length-$read)
            if(!$count){break};$read+=$count
        }
        $text=[Text.Encoding]::UTF8.GetString($bytes,0,$read)
        $lastNewline=$text.LastIndexOf([char]10)
        if($lastNewline -lt 0){return $null}
        [string[]]$lines=$text.Substring(0,$lastNewline).Split([char]10)
    }catch{return $null}finally{if($stream){$stream.Dispose()}}
    if($lines.Length -gt 16384){return $null}
    for($i=$lines.Length-1;$i -ge 0;$i--){
        $r=try{$lines[$i]|ConvertFrom-Json -AsHashtable}catch{continue}
        if($r -isnot [Collections.IDictionary]){continue}
        $receiptSequence=try{[uint64]$r['sequence']}catch{continue}
        if($r['run_id'] -ceq $runId -and $r['role'] -ceq $Role -and $receiptSequence -eq $Sequence -and
           $Events -ccontains [string]$r['event']){return $r}
    }
    return $null
}
function Get-CaptureSnapshot([string]$Role){
    $s=Read-RoleRaw $Role;$now=[Environment]::TickCount64
    $age=if($s -and $s.ContainsKey('tick_ms') -and [uint64]$s['tick_ms'] -le $now){$now-[uint64]$s['tick_ms']}else{$null}
    $row=[ordered]@{sampled_tick_ms=$now;state_age_ms=$age;stale=(!$s -or $null -eq $age -or $age -gt 1000 -or
        $s['run_id'] -cne $runId -or $s['role'] -cne $Role)}
    # Keep every copied telemetry field, including later diagnostic additions.
    if($s){foreach($k in $s.Keys){$row[$k]=$s[$k]}}
    return $row
}
function Release-RoleInput([string]$Role){$null=Send-Step $Role @{Operation='release'}}
function Release-RoleFinally([string]$Role){
    try{Release-RoleInput $Role}catch{
        $message="[$Role] input release refused: $($_.Exception.Message)"
        $releaseFailures.Add($message);Write-Warning $message
    }
}
function Invoke-DebugResult([string]$Role,[string]$Command,[int]$TimeoutMs=8000){
    $before=Read-RoleRaw $Role;$was=if($before){[uint64]$before['debug_sequence']}else{[uint64]0}
    $sent=try{Send-Step $Role @{Operation='debug';DebugCommand=$Command}}catch{return "SEND_REFUSED: $($_.Exception.Message)"}
    $deadline=[Environment]::TickCount64+$TimeoutMs
    if($before -and [int]$before['schema'] -ge 2 -and $sent -and $sent.sequence){
        # Schema 2: pair by the accepted sequence, then by command id or overlay serial.
        $seq=[uint64]$sent.sequence
        while([Environment]::TickCount64 -lt $deadline){
            Start-Sleep -Milliseconds 150
            $now=Read-RoleRaw $Role
            if(!$now -or [uint64]$now['debug_sequence'] -lt $seq){continue}
            if([uint64]$now['queued_sequence'] -lt $seq){
                $refusal=@($now['receipts']|Where-Object{$_ -and [int]$_['status'] -eq 3})|Select-Object -First 1
                return "QUEUE_REFUSED: $(if($refusal){$refusal['result']}else{$now['last_result']})"
            }
            $ref=[uint64]$now['queued_ref']
            if([string]$now['queued_kind'] -ceq 'menu'){
                if([uint64]$now['overlay_result_serial'] -eq $ref){return [string]$now['overlay_result']}
                continue
            }
            $final=@($now['receipts']|Where-Object{$_ -and [uint64]$_['id'] -eq $ref -and [int]$_['status'] -in 0,1})|Select-Object -First 1
            if($final){return [string]$final['result']}
        }
        throw "[$Role] '$Command' produced no final result."
    }
    while([Environment]::TickCount64 -lt $deadline){
        Start-Sleep -Milliseconds 200
        $now=Read-RoleRaw $Role
        # The first result can be the interim 'Queued for the natural game update.'
        if($now -and [uint64]$now['debug_sequence'] -gt $was -and [string]$now['last_command'] -ceq $Command -and
           [string]$now['last_result'] -cnotlike 'Queued*'){return [string]$now['last_result']}
    }
    throw "[$Role] debug '$Command' produced no result."
}
function Wait-Frame([string]$Role,[uint64]$Sequence,[string]$Label,[uint64]$RequestedAt=0,[hashtable]$Hold=$null){
    $path=Join-Path $run "$Role-frame-$Sequence.bmp"
    $deadline=[Environment]::TickCount64+5000
    $native=$null
    while([Environment]::TickCount64 -lt $deadline){
        $native=Read-NativeReceipt $Role $Sequence @('frame_written','frame_refused','command_refused','capture_failed')
        if($native -and ($native['event'] -cne 'frame_written' -or (Test-Path -LiteralPath $path -PathType Leaf))){break}
        Start-Sleep -Milliseconds 50
    }
    $hasFrame=$native -and $native['event'] -ceq 'frame_written' -and (Test-Path -LiteralPath $path -PathType Leaf)
    $writtenAt=if($native){[uint64]$native['tick_ms']}else{$null}
    $roles=[ordered]@{host=(Get-CaptureSnapshot 'host');guest=(Get-CaptureSnapshot 'guest')}
    $viewer=$roles[$Role]
    $viewerTick=if($viewer.Contains('tick_ms')){$viewer['tick_ms']}else{$null}
    $delta=if($null -ne $writtenAt -and $null -ne $viewerTick){[Math]::Abs([double]$writtenAt-[double]$viewerTick)}else{$null}
    $row=[ordered]@{label=$(if($Label){$Label}else{"frame-$Sequence"});role=$Role;sequence=$Sequence;tick_ms=$viewerTick;
        capture_requested_tick_ms=$RequestedAt;frame_written_tick_ms=$writtenAt;frame_written=[bool]$hasFrame;
        frame_result=$(if($native){$native['reason']}else{'missing_frame_receipt'});frame_state_delta_ms=$delta;
        frame_state_stale=($viewer['stale'] -or $null -eq $delta -or $delta -gt 250);
        frame_late=($null -eq $writtenAt -or ($RequestedAt -gt 0 -and $writtenAt-$RequestedAt -gt 1500));
        hold=$null;roles=$roles}
    foreach($k in 'mission','realized','body_valid','pose_valid','mapped_clip','remote_act','remote_action_flags','remote_age_ms',
        'position_error_m','heading_error_deg','player_pos','remote_pos','body_pos','queue_depth','local_anim_valid','local_act',
        'local_anim_stance','local_anim_speed','remote_anim_valid','remote_stance','remote_anim_stance','remote_anim_speed',
        'dm_phase','dm_move','dm_stance','dm_speed_valid','dm_speed_mmps','player_yaw','remote_yaw','body_yaw'){$row[$k]=$viewer[$k]}
    if($Hold){
        $accepted=Read-NativeReceipt $Hold.role ([uint64]$Hold.sequence) @('command_accepted')
        $start=if($accepted){[uint64]$accepted['tick_ms']}else{$null}
        $source=$roles[$Hold.role]
        $sourceDelta=if($null -ne $writtenAt -and $null -ne $source['tick_ms']){[Math]::Abs([double]$writtenAt-[double]$source['tick_ms'])}else{$null}
        $timely=$null -ne $start -and $null -ne $writtenAt -and $writtenAt -ge $start -and $writtenAt -lt $start+1500
        $motion=$source['local_anim_valid'] -and ([int]$source['local_act'] -in 2,4,6 -or [int]$source['local_anim_speed'] -in 2,3,4)
        $sameScene=$source['gameplay_ready'] -and !$source['terminating'] -and $source['mission'] -eq $Hold.mission -and
            $source['generation'] -eq $Hold.generation
        $row.hold=[ordered]@{role=$Hold.role;keys=$Hold.keys;sequence=$Hold.sequence;accepted_tick_ms=$start;
            duration_ms=1500;out_of_hold=(!$timely);source_frame_state_delta_ms=$sourceDelta;
            source_state_stale=($source['stale'] -or $null -eq $sourceDelta -or $sourceDelta -gt 250);
            local_motion_observed=[bool]$motion;motion_required=[bool]$Hold.motion_required;
            scene_unchanged=[bool]$sameScene;
            local_motion_during_hold=($timely -and $sameScene -and !$source['stale'] -and $null -ne $sourceDelta -and $sourceDelta -le 250 -and $motion)}
        if($Hold.ContainsKey('turn')){$row.hold.turn=$Hold.turn}
    }
    Add-Content -LiteralPath (Join-Path $run 'captures.jsonl') -Value ($row|ConvertTo-Json -Compress -Depth 8)
    if(!$hasFrame){throw "[$Role] frame $Sequence was not written: $($row.frame_result)"}
    if((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Captured frame must be a plain owned file.'}
    $lastFrameAt[$Role]=$writtenAt
    $name=if($Label){"$Role-$Label.bmp"}else{"$Role-frame-$Sequence.bmp"}
    if($Label){Copy-Item -LiteralPath $path -Destination (Join-Path $run $name)}
    $frames.Add([pscustomobject]@{Role=$Role;Label=$Label;Path=(Join-Path $run $name)})
    if($row.frame_late -or $row.frame_state_stale -or ($Hold -and $row.hold.out_of_hold)){
        Write-Warning "[$Role] frame '$Label' has late or stale timing; inspect captures.jsonl before interpreting animation."
    }
    if($Hold -and $Hold.motion_required -and !$row.hold.local_motion_during_hold){
        throw "[$($Hold.role)] '$Label' lacks timely local movement readback during its hold."
    }
    return $row
}
function Capture-Step([string]$Role,[string]$Label,[hashtable]$Hold=$null){
    # Native capture requests are rate-limited to 250 ms. A completed frame is
    # later than its request, so this margin also works with deferred readback.
    $remaining=[int64]$lastFrameAt[$Role]+300-[Environment]::TickCount64
    if($remaining -gt 0){Start-Sleep -Milliseconds $remaining}
    $c=Send-Step $Role @{Operation='capture'}
    return Wait-Frame $Role ([uint64]$c.sequence) $Label ([uint64]$c.expires_tick_ms-2000) $Hold
}
function Set-ScenarioStance([string]$Role,[string]$Stance){
    Release-RoleInput $Role
    $target=@{stand=1;crouch=2;prone=3}[$Stance]
    for($attempt=0;$attempt -lt 4;$attempt++){
        $before=Read-FreshRole $Role
        if(!$before['local_anim_valid'] -or [int]$before['local_anim_stance'] -notin 1,2,3){throw "[$Role] stance has no valid local animation readback."}
        if([int]$before['local_anim_stance'] -eq $target){Write-Host "  $Role stance=$Stance confirmed";return}
        $duration=if($target -eq 3){500}else{120}
        $operation=if($target -eq 3){'hold'}else{'press'}
        try{
            $null=Send-Step $Role @{Operation=$operation;Keys='c';DurationMs=$duration}
            Start-Sleep -Milliseconds ($duration+100)
        }finally{Release-RoleFinally $Role}
        $until=[Environment]::TickCount64+3000
        while([Environment]::TickCount64 -lt $until){
            Start-Sleep -Milliseconds 100
            $after=Read-FreshRole $Role
            if($after['local_anim_valid'] -and [int]$after['local_anim_stance'] -in 1,2,3 -and
               [uint64]$after['tick_ms'] -gt [uint64]$before['tick_ms'] -and
               [int]$after['local_anim_stance'] -ne [int]$before['local_anim_stance']){break}
        }
        if(!$after['local_anim_valid'] -or [int]$after['local_anim_stance'] -eq [int]$before['local_anim_stance']){
            throw "[$Role] C did not produce an observed stance transition."
        }
        if([int]$after['local_anim_stance'] -eq $target){Write-Host "  $Role stance=$Stance confirmed after C";return}
    }
    throw "[$Role] did not reach requested $Stance stance within four observed transitions."
}
function Invoke-HeldCapture([string]$Source,[string]$Viewer,[string]$Keys,[string]$Label,[int]$Count){
    Release-RoleInput $Source
    $motionRequired=@($Keys.Split('+')|Where-Object{$_ -cin 'w','a','s','d'}).Count -gt 0
    try{
        for($i=1;$i -le $Count;$i++){
            $before=Read-FreshRole $Source
            $held=Send-Step $Source @{Operation='hold';Keys=$Keys;DurationMs=1500}
            Start-Sleep -Milliseconds 350
            $null=Capture-Step $Viewer ('{0}-{1:00}' -f $Label,$i) @{role=$Source;keys=$Keys;sequence=$held.sequence;
                mission=$before['mission'];generation=$before['generation'];motion_required=$motionRequired}
        }
    }finally{Release-RoleFinally $Source}
}
function Get-ScenarioTick{return [Environment]::TickCount64}
function Send-AimTurnCommand([string]$Role,[hashtable]$Arguments,[int64]$Deadline){
    $sentAt=Get-ScenarioTick
    if($sentAt -ge $Deadline){throw "[$Role] pivot input exceeded its deadline."}
    $command=Send-Step $Role $Arguments
    $until=[Math]::Min($Deadline,$sentAt+500)
    for($poll=0;$poll -lt 11;$poll++){
        # Controller file I/O may return after the deadline even though the
        # native owner accepted on time. Always inspect that first receipt.
        if($poll -gt 0 -and (Get-ScenarioTick) -gt $until){break}
        $receipt=Read-NativeReceipt $Role ([uint64]$command.sequence) @('command_accepted','command_refused')
        if($receipt){
            if($receipt['event'] -cne 'command_accepted' -or [int64]$receipt['tick_ms'] -lt $sentAt -or
                [int64]$receipt['tick_ms'] -gt $until -or [int64]$receipt['tick_ms'] -gt (Get-ScenarioTick)){
                throw "[$Role] pivot input was refused or has invalid acceptance timing."
            }
            return @{command=$command;accepted_tick_ms=[int64]$receipt['tick_ms'];sent_tick_ms=$sentAt}
        }
        $remaining=$until-(Get-ScenarioTick)
        if($remaining -le 0){break}
        Start-Sleep -Milliseconds ([Math]::Min(50,$remaining))
    }
    throw "[$Role] pivot input lacks a timely native acceptance receipt."
}
function Read-AimTurnState([string]$Source,[string]$Observer,[hashtable]$SourceScene,[hashtable]$ObserverScene){
    $sourceState=Read-FreshRole $Source;$observerState=Read-FreshRole $Observer
    $sampledAt=Get-ScenarioTick
    foreach($state in $sourceState,$observerState){
        if(!$state.ContainsKey('tick_ms') -or $null -eq $state['tick_ms'] -or [int64]$state['tick_ms'] -gt $sampledAt -or
            $sampledAt-[int64]$state['tick_ms'] -gt 250){throw "[$Source/$Observer] pivot requires telemetry within 250 milliseconds."}
    }
    if($sourceState['mission'] -ne $SourceScene.mission -or $sourceState['generation'] -ne $SourceScene.generation -or
        $observerState['mission'] -ne $ObserverScene.mission -or $observerState['generation'] -ne $ObserverScene.generation){
        throw "[$Source/$Observer] pivot crossed a mission generation."
    }
    if(!$sourceState['local_yaw_valid'] -or !$sourceState.ContainsKey('player_yaw') -or
        $null -eq $sourceState['player_yaw'] -or ![double]::IsFinite([double]$sourceState['player_yaw'])){
        throw "[$Source] pivot needs finite valid absolute body-heading readback."
    }
    foreach($field in 'remote_action_flags','remote_equip','remote_age_ms'){
        if(!$observerState.ContainsKey($field) -or $null -eq $observerState[$field]){
            throw "[$Observer] pivot lacks received source action readback."
        }
    }
    $flags=[int]$observerState['remote_action_flags'];$age=[int]$observerState['remote_age_ms']
    $receivedFresh=$age -ge 0 -and $age -le 250
    return @{source=$sourceState;observer=$observerState;received_fresh=$receivedFresh;sampled_tick_ms=$sampledAt;
        source_state_age_ms=($sampledAt-[int64]$sourceState['tick_ms']);observer_state_age_ms=($sampledAt-[int64]$observerState['tick_ms']);
        clear=($receivedFresh -and ($flags -band 1) -eq 0);
        aiming=($receivedFresh -and [int]$observerState['remote_equip'] -gt 0 -and ($flags -band 1) -eq 1 -and ($flags -band 62) -eq 0)}
}
function Wait-AimTurnReady([string]$Source,[string]$Observer,[hashtable]$SourceScene,[hashtable]$ObserverScene,
    [int64]$ReleasedAt,[int64]$Deadline){
    $clear=$null;$clearUntil=[Math]::Min($Deadline,$ReleasedAt+1500)
    for($poll=0;$poll -lt 15 -and (Get-ScenarioTick) -lt $clearUntil;$poll++){
        Start-Sleep -Milliseconds 100
        if((Get-ScenarioTick) -gt $clearUntil){break}
        $sample=Read-AimTurnState $Source $Observer $SourceScene $ObserverScene
        if($sample.clear -and [int64]$sample.observer['tick_ms'] -gt $ReleasedAt){$clear=$sample;break}
    }
    if(!$clear){throw "[$Observer] pivot did not observe the source release aim."}
    $held=Send-AimTurnCommand $Source @{Operation='hold';Keys='rmb';DurationMs=1500} $Deadline
    $initialHeldAt=$held.accepted_tick_ms;$samples=[Collections.Generic.List[hashtable]]::new()
    $lastSourceTick=0;$lastObserverTick=[int64]$clear.observer['tick_ms']
    for($poll=0;$poll -lt 40 -and (Get-ScenarioTick) -lt $Deadline;$poll++){
        if((Get-ScenarioTick) -ge $held.accepted_tick_ms+400){
            $held=Send-AimTurnCommand $Source @{Operation='hold';Keys='rmb';DurationMs=1500} ([Math]::Min($Deadline,$held.accepted_tick_ms+750))
        }
        Start-Sleep -Milliseconds 100
        if((Get-ScenarioTick) -gt $Deadline){break}
        $sample=Read-AimTurnState $Source $Observer $SourceScene $ObserverScene
        $sourceTick=[int64]$sample.source['tick_ms'];$observerTick=[int64]$sample.observer['tick_ms']
        if(!$sample.aiming){$samples.Clear();continue}
        if($sourceTick -le $lastSourceTick -or $observerTick -le $lastObserverTick){continue}
        $lastSourceTick=$sourceTick;$lastObserverTick=$observerTick
        $yaw=[double]$sample.source['player_yaw']
        if($samples.Count -and [Math]::Abs([Math]::IEEERemainder($yaw-[double]$samples[0].yaw,360.0)) -gt 1.0){$samples.Clear()}
        $samples.Add(@{tick_ms=$sourceTick;yaw=$yaw})
        if($samples.Count -ge 3 -and $sourceTick-[int64]$samples[0].tick_ms -ge 250 -and
            (Get-ScenarioTick)-$initialHeldAt -ge 800){
            return @{state=$sample;hold=$held;release_observed_tick_ms=$clear.observer['tick_ms'];
                ready_tick_ms=(Get-ScenarioTick);initial_hold_accepted_tick_ms=$initialHeldAt;baseline_samples=$samples.ToArray()}
        }
    }
    throw "[$Source/$Observer] pivot did not receive stationary aim and settle body heading within 4 seconds."
}
function Invoke-AimTurn([string]$Source,[string]$Viewer,[int]$Dx,[string]$Label){
    $observer=if($Source -ceq 'host'){'guest'}else{'host'}
    try{
        $deadline=(Get-ScenarioTick)+4000
        $sourceScene=Read-FreshRole $Source;$observerScene=Read-FreshRole $observer
        $release=Send-AimTurnCommand $Source @{Operation='release'} $deadline
        $ready=Wait-AimTurnReady $Source $observer $sourceScene $observerScene $release.accepted_tick_ms $deadline
        # A native receipt prevents this renewal from being overwritten by the
        # mouse command in the worker's single-command input inbox.
        $held=Send-AimTurnCommand $Source @{Operation='hold';Keys='rmb';DurationMs=1500} $deadline
        $baseline=Read-AimTurnState $Source $observer $sourceScene $observerScene
        if((Get-ScenarioTick) -gt $deadline -or !$baseline.aiming -or [Math]::Abs([Math]::IEEERemainder(
            [double]$baseline.source['player_yaw']-[double]$ready.state.source['player_yaw'],360.0)) -gt 1.0){
            throw "[$Source] pivot heading/aim changed after readiness."
        }
        $mouseDispatchedAt=Get-ScenarioTick;$until=$mouseDispatchedAt+1000
        $mouse=Send-AimTurnCommand $Source @{Operation='mouse';Dx=$Dx;Dy=0} $until
        $excursion=0.0;$after=$null
        for($poll=0;$poll -lt 20 -and (Get-ScenarioTick) -lt $until;$poll++){
            if((Get-ScenarioTick) -ge $held.accepted_tick_ms+400){
                $held=Send-AimTurnCommand $Source @{Operation='hold';Keys='rmb';DurationMs=1500} ([Math]::Min($until,$held.accepted_tick_ms+750))
            }
            Start-Sleep -Milliseconds 50
            if((Get-ScenarioTick) -gt $until){break}
            $after=Read-AimTurnState $Source $observer $sourceScene $observerScene
            if(!$after.aiming){throw "[$Observer] pivot lost received stationary source aim after mouse input."}
            if([int64]$after.source['tick_ms'] -gt $mouse.accepted_tick_ms -and
                [int64]$after.source['tick_ms'] -gt [int64]$baseline.source['tick_ms']){
                $excursion=[Math]::IEEERemainder([double]$after.source['player_yaw']-[double]$baseline.source['player_yaw'],360.0)
                # Native ObserveTurn requires 1.20 radians (68.75 degrees).
                if([Math]::Abs($excursion) -ge 70.0){break}
            }
        }
        if([Math]::Abs($excursion) -lt 70.0){throw "[$Source] post-mouse pivot (dx $Dx) moved body heading only $excursion degrees; no qualifying turn stimulus."}
        Write-Host "  $Source body pivot=$excursion degrees after received aim/settled heading"
        Start-Sleep -Milliseconds 150
        if((Get-ScenarioTick) -ge $held.accepted_tick_ms+400){
            $held=Send-AimTurnCommand $Source @{Operation='hold';Keys='rmb';DurationMs=1500} ((Get-ScenarioTick)+150)
        }
        $null=Capture-Step $Viewer ($Label+'-pivot') @{role=$Source;keys='rmb';sequence=$held.command.sequence;
            mission=$sourceScene['mission'];generation=$sourceScene['generation'];motion_required=$false;
            turn=@{observer_role=$observer;release_observed_tick_ms=$ready.release_observed_tick_ms;
                readiness_tick_ms=$ready.ready_tick_ms;initial_hold_accepted_tick_ms=$ready.initial_hold_accepted_tick_ms;
                baseline_samples=$ready.baseline_samples;baseline_tick_ms=$baseline.source['tick_ms'];
                baseline_yaw=$baseline.source['player_yaw'];observer_baseline_tick_ms=$baseline.observer['tick_ms'];
                baseline_source_state_age_ms=$baseline.source_state_age_ms;baseline_observer_state_age_ms=$baseline.observer_state_age_ms;
                observer_baseline_flags=$baseline.observer['remote_action_flags'];observer_baseline_age_ms=$baseline.observer['remote_age_ms'];
                mouse_dispatched_tick_ms=$mouseDispatchedAt;mouse_accepted_tick_ms=$mouse.accepted_tick_ms;mouse_sequence=$mouse.command.sequence;
                latest_hold_sequence=$held.command.sequence;measured_tick_ms=$after.source['tick_ms'];
                measured_source_state_age_ms=$after.source_state_age_ms;measured_observer_state_age_ms=$after.observer_state_age_ms;
                measured_yaw=$after.source['player_yaw'];excursion_degrees=$excursion;threshold_degrees=70.0}}
    }finally{Release-RoleFinally $Source}
    Start-Sleep -Milliseconds 1800
    $null=Capture-Step $Viewer ($Label+'-settled')
}
function Wait-ViewerLanding([string]$Role,[double]$ExpectedX,[double]$ExpectedZ,
    [ValidateRange(0.01,2.0)][double]$MaximumHorizontalError=2.0){
    $initial=Read-FreshRole $Role
    if(!$initial['realized'] -or !$initial['body_valid'] -or !$initial['pose_valid']){
        throw "[$Role] viewer landing starts without a valid partner pose."
    }
    $deadline=[Environment]::TickCount64+3000
    $previousTick=[uint64]$initial['tick_ms'];$previousY=$null;$anchorY=$null;$stable=0;$lastPosition=$null
    for($sample=0;$sample -lt 12 -and [Environment]::TickCount64+250 -lt $deadline;$sample++){
        Start-Sleep -Milliseconds 250
        $current=Read-FreshRole $Role
        if([Environment]::TickCount64 -ge $deadline){break}
        if($current['mission'] -ne $initial['mission'] -or $current['generation'] -ne $initial['generation'] -or
           !$current['realized'] -or !$current['body_valid'] -or !$current['pose_valid']){
            throw "[$Role] viewer landing lost its scene or valid partner pose."
        }
        if([uint64]$current['tick_ms'] -le $previousTick){throw "[$Role] viewer landing has no new position sample."}
        $previousTick=[uint64]$current['tick_ms']
        $position=$current['player_pos']
        if($position -isnot [Array] -or $position.Count -ne 3){throw "[$Role] viewer landing has no local position readback."}
        foreach($coordinate in $position){
            if($null -eq $coordinate -or ![double]::IsFinite([double]$coordinate)){throw "[$Role] viewer landing has a nonfinite local position."}
        }
        $lastPosition=$position
        $dx=[double]$position[0]-$ExpectedX;$dz=[double]$position[2]-$ExpectedZ
        if([Math]::Sqrt($dx*$dx+$dz*$dz) -gt $MaximumHorizontalError){$previousY=$anchorY=$null;$stable=0;continue}
        $y=[double]$position[1]
        if($null -ne $previousY -and [Math]::Abs($y-$previousY) -le 0.25 -and [Math]::Abs($y-$anchorY) -le 0.25){
            $stable++
        }else{$anchorY=$y;$stable=0}
        $previousY=$y
        if($stable -ge 2){
            Write-Host "  $Role viewer position settled at y=$y from three fresh samples"
            return $current
        }
    }
    $observed=if($lastPosition){[string]::Format([Globalization.CultureInfo]::InvariantCulture,
        '{0:0.000},{1:0.000},{2:0.000}',$lastPosition[0],$lastPosition[1],$lastPosition[2])}else{'unavailable'}
    throw "[$Role] viewer position did not arrive and settle within 3 seconds; last_position=$observed; expected_xz=$ExpectedX,$ExpectedZ; horizontal_limit_m=$MaximumHorizontalError."
}
function Reset-ScenarioPose([string]$Role){
    Release-RoleInput $Role
    $current=Read-FreshRole $Role
    if(!$poseAnchors.ContainsKey($Role)){
        if(!$current['local_yaw_valid'] -or $null -eq $current['player_yaw'] -or
           ![double]::IsFinite([double]$current['player_yaw'])){throw "[$Role] benchmark needs valid local heading."}
        $position=$current['player_pos']
        if($position -isnot [Array] -or $position.Count -ne 3){throw "[$Role] benchmark needs local position."}
        foreach($coordinate in $position){
            if($null -eq $coordinate -or ![double]::IsFinite([double]$coordinate)){throw "[$Role] benchmark position is nonfinite."}
        }
        $poseAnchors[$Role]=@{position=$position.Clone();yaw=[double]$current['player_yaw'];
            mission=$current['mission'];generation=$current['generation']}
    }
    $anchor=$poseAnchors[$Role]
    if($current['mission'] -ne $anchor.mission -or $current['generation'] -ne $anchor.generation){
        throw "[$Role] benchmark cannot cross a mission generation."
    }
    $command=[string]::Format([Globalization.CultureInfo]::InvariantCulture,
        'teleport {0:0.000} {1:0.000} {2:0.000} {3:0.000}',$anchor.position[0],$anchor.position[1],$anchor.position[2],$anchor.yaw)
    $result=Invoke-DebugResult $Role $command 12000
    $landed=Wait-ViewerLanding $Role ([double]$anchor.position[0]) ([double]$anchor.position[2]) -MaximumHorizontalError 0.75
    if(!$landed['local_yaw_valid'] -or $null -eq $landed['player_yaw'] -or
       [Math]::Abs([Math]::IEEERemainder([double]$landed['player_yaw']-$anchor.yaw,360.0)) -gt 2.0 -or
       [Math]::Abs([double]$landed['player_pos'][1]-[double]$anchor.position[1]) -gt 0.75){
        throw "[$Role] benchmark arrival differs from its recorded position/heading."
    }
    Write-Host "  $Role benchmark $command confirmed: $result"
}
try {
    foreach($line in $steps){
        $t=$line -split '\s+'
        Write-Host "step: $line"
        # if/elseif, not switch: 'continue' inside a PowerShell switch only leaves the switch.
        if($t[0] -ceq 'wait'){Start-Sleep -Milliseconds ([int]$t[1]);continue}
        if($t[0] -ceq 'stance'){Set-ScenarioStance $t[1] $t[2];continue}
        if($t[0] -ceq 'heldcapture'){Invoke-HeldCapture $t[1] $t[2] $t[3] $t[4] ([int]$t[5]);continue}
        if($t[0] -ceq 'aimturn'){Invoke-AimTurn $t[1] $t[2] ([int]$t[3]) $t[4];continue}
        if($t[0] -ceq 'resetpose'){Reset-ScenarioPose $t[1];continue}
        if($t[0] -ceq 'state'){foreach($r in 'host','guest'){$s=Read-RoleState $r;Write-Host "  $r mission=$($s.mission) ready=$($s.gameplay_ready) generation=$($s.generation)"};continue}
        if($t[0] -ceq 'title'){
            $limit=[Environment]::TickCount64+1000*($(if($t.Count -eq 2){[int]$t[1]}else{240}))
            while([Environment]::TickCount64 -lt $limit){
                $pending=@('host','guest'|Where-Object{!((Read-RoleState $_).gameplay_ready)})
                if(!$pending.Count){break}
                foreach($r in $pending){$null=Send-Step $r @{Operation='press';Keys='enter';DurationMs=120}}
                Start-Sleep -Seconds 3
            }
            foreach($r in 'host','guest'){$s=Read-RoleState $r;Write-Host "  $r mission=$($s.mission) ready=$($s.gameplay_ready)"}
            continue
        }
        if($t[0] -ceq 'waitmission'){
            Wait-ScenarioMission $t[1] ([int]$t[2]) ([int]$t[3]);continue
        }
        if($t[0] -ceq 'waitgameplay'){
            $limit=[Environment]::TickCount64+1000*[int]$t[2];$last=0;$result=''
            while([Environment]::TickCount64 -lt $limit){
                try{$result=Invoke-DebugResult $t[1] 'position' 6000}catch{$result=$_.Exception.Message}
                if($result -like 'position=*'){break}
                if([Environment]::TickCount64-$last -ge 6000){$last=[Environment]::TickCount64
                    $action=Get-ScenarioWaitAction $t[1] $result (Read-RoleRaw $t[1]) ([Environment]::TickCount64)
                    if($action -ceq 'e'){$null=Send-Step $t[1] @{Operation='press';Keys='e';DurationMs=120}}
                    else{
                        $null=Send-Step $t[1] @{Operation='press';Keys='escape';DurationMs=120};Start-Sleep -Milliseconds 900
                        $null=Send-Step $t[1] @{Operation='press';Keys='enter';DurationMs=120}
                    }}
                Start-Sleep -Milliseconds 1500
            }
            if($result -notlike 'position=*'){throw "[$($t[1])] on-foot gameplay was not observed: $result"}
            Write-Host "  $($t[1]): $result"
            continue
        }
        if($t[0] -ceq 'placebehind'){
            Release-RoleInput $t[1]
            $result=Invoke-DebugResult $t[2] 'position'
            if($result -notmatch '^position=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+);yaw=(-?[\d.]+)'){throw "[$($t[2])] position result: $result"}
            $x=[double]$Matches[1];$y=[double]$Matches[2];$z=[double]$Matches[3];$yaw=[double]$Matches[4];$d=[double]$t[3]
            $side=if($t.Count -eq 5){[double]$t[4]}else{0.0}
            $rad=$yaw*[Math]::PI/180.0
            # forward = (sin, cos); right = (cos, -sin). Face the target.
            $tx=$x-$d*[Math]::Sin($rad)+$side*[Math]::Cos($rad);$tz=$z-$d*[Math]::Cos($rad)-$side*[Math]::Sin($rad)
            $face=[Math]::Atan2($x-$tx,$z-$tz)*180.0/[Math]::PI
            $inv=[Globalization.CultureInfo]::InvariantCulture
            $command=[string]::Format($inv,'teleport {0:0.000} {1:0.000} {2:0.000} {3:0.000}',$tx,($y+3.0),$tz,$face)
            $placed=Invoke-DebugResult $t[1] $command 12000
            Write-Host "  $($t[2]) at ($x,$y,$z) yaw $yaw -> $($t[1]) $command : $placed"
            $null=Wait-ViewerLanding $t[1] $tx $tz
            continue
        }
        $targets=if($t[0] -ceq 'both'){@('host','guest')}else{@($t[0])}
        foreach($r in $targets){
            switch($t[1]){
                'worldtrigger' {Invoke-ScenarioWorldTravel (($t|Select-Object -Skip 2) -join ' ') $r}
                'press' {$null=Send-Step $r @{Operation='press';Keys=$t[2];DurationMs=$(if($t.Count -eq 4){[int]$t[3]}else{120})}}
                'hold' {$null=Send-Step $r @{Operation='hold';Keys=$t[2];DurationMs=[int]$t[3]}}
                'mouse' {$null=Send-Step $r @{Operation='mouse';Dx=[int]$t[2];Dy=[int]$t[3]}}
                'release' {Release-RoleInput $r}
                'capture' {$null=Capture-Step $r $(if($t.Count -eq 3){$t[2]}else{''})}
                'burst' {
                    for($i=1;$i -le [int]$t[3];$i++){
                        $null=Capture-Step $r ('{0}-{1:00}' -f $t[2],$i)
                        Start-Sleep -Milliseconds ([int]$t[4])
                    }
                }
                'run' {
                    $text=(($t|Select-Object -Skip 2) -join ' ')
                    $timeout=if($text -like 'spawn_enemy*'){30000}else{15000}
                    try{$result=Invoke-DebugResult $r $text $timeout}catch{$result="NO_RESULT: $($_.Exception.Message)"}
                    $lastResult[$r]=$result
                    Write-Host "  $r $text -> $result"
                }
                'expect' {
                    Assert-ScenarioResult $r (($t|Select-Object -Skip 2) -join ' ') $lastResult[$r] $false
                }
                'require' {
                    Assert-ScenarioResult $r (($t|Select-Object -Skip 2) -join ' ') $lastResult[$r] $true
                }
                'debug' {
                    # The game's debug inbox holds one command: wait for each result.
                    $text=(($t|Select-Object -Skip 2) -join ' ')
                    try{$result=Invoke-DebugResult $r $text 12000}catch{$result="no result: $($_.Exception.Message)"}
                    Write-Host "  $r $text -> $result"
                }
            }
        }
        if($t[1] -in 'hold','press' -and $t[-1] -cne 'nowait'){Start-Sleep -Milliseconds ($(if($t[1] -eq 'hold'){[int]$t[3]}else{150})+100)}
    }
} catch {
    $failure=$_.Exception.Message
    Write-Warning "Scenario stopped: $failure"
} finally {
    # A failed or timed-out hold must also release before the owned shutdown.
    foreach($role in 'host','guest'){Release-RoleFinally $role}
    $stop=& (Join-Path $worker 'Stop-WorkerSession.ps1') -RunDirectory $run
    $stop=@($stop|Where-Object{$_ -isnot [string]})[-1]
}
$result=[ordered]@{
    RunDirectory=$run;Frames=$frames.ToArray();Failure=$failure;InputReleaseFailures=$releaseFailures.ToArray()
    Expectations=$expectations.ToArray();ExpectFailures=$expectFailures.ToArray()
    Closed=$stop.Closed;BaselineRestored=$stop.BaselineRestored;RealSavesUnchanged=$stop.RealSavesUnchanged;Forced=$stop.Forced
    IsolationFailed=$stop.IsolationFailed;SupervisorReason=$stop.SupervisorReason;SupervisorError=$stop.SupervisorError
}
Write-WorkerSessionJson (Join-Path $run 'scenario.json') $result
[pscustomobject]$result
