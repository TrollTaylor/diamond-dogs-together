# Hidden worker: first animation evaluation (2026-10-07)

**Follow-up audit:** the original action labels below assumed the host started
standing. `host-start.bmp` actually shows crouched Snake, so this run does not
demonstrate a stand/crouch inversion. A single C press subsequently stands him
up; the guest's upright partner is consistent with that transition.

Run `build/worker-runtime/904f772a2df64307a469db4ef05fface`: hidden private-desktop
session (tools/worker/Invoke-WorkerScenario.ps1, scenario
tools/worker/scenarios/partner-locomotion.txt, lab saves, animated partner profile,
Afghanistan 30010, noon). The guest filmed the host's partner body; per-capture
telemetry is in that run's `captures.jsonl`. Real saves were unchanged.

Telemetry (guest's view of the host's partner):

| Host action | mapped_clip / remote_act | Problem |
|---|---|---|
| Run (W, standing) | 1 / 3 the whole time (squat idle) | Partner slides along in a crouched idle pose: no run clip |
| Sprint (Shift+W) | 4 / 4 (squat run) | Crouched run while the host sprints upright |
| Walk (Ctrl+W) | 4 / 4 (squat run) | Same |
| Crouch (C) | 0 / 1 (stand idle) | Inverted: the partner stands when the host crouches |
| Crouch-move | 11 / 2 (stand run) | Inverted |
| Prone / crawl | 7 / 5, then 6 / 6 | Correct |
| Stationary mouse turns | 0 / 1, no turn clip | No turn clips selected |
| Strafe (A) | 11 / 2 (stand run) | Run clip while strafing sideways |

The original conclusion of swapped stance classes and a stuck run was not
supported. The first W request was consumed at tick 476051750 for 1400 ms.
Guest capture 21 was refused at 476051781 by the native 250 ms capture limit,
only 156 ms after the preceding frame. The runner then waited five seconds.
The first surviving `run-02` telemetry is 476057000, after input ended, and all
run samples have identical positions and no movement flags. The actual game
log records crouched clips 4 → 3 → 1 and later standing clips 11 → 10 → 0, with
successful playback. No gait mapping was inverted.

The camera-only stationary mouse steps did not establish a body pivot. The
existing turn collector requires actual body-heading change; aimed RMB pivots
are the appropriate stimulus.

The revised scenario confirms each starting posture from local engine arrays,
renews bounded input before each motion capture, respects the capture cooldown,
fails on a refused/missing frame, and records native frame time separately from
both roles' telemetry time. It also records absolute headings, local and remote
animation fields, the selected movement class and measured speed. Runtime
comparison is described below. Remote aim/fire/reload intent still has no
enabled production upper-body animation backend.

## Controlled continuation

The first corrected run, `c646217606fd4b2aae1d4860ef235bcc`, confirmed both
postures from local engine readback but stopped at its first capture. The BMP
and exact native receipt existed; PowerShell's default file sharing prevented
the reader from opening the live append log. The reader now allows the existing
writer, reads a bounded frozen file length and ignores incomplete final rows.
A persistent detached fixture reproduces that actual held-writer failure and
checks exact receipt identity, partial appends, refusals and size/row limits.

Run `a549329711bd46cba88908abd9ed2ce5` collected 39 actual frames with source
input active and both roles' telemetry. Upright running selects clip 11 and
shows different leg positions in `guest-run-02.bmp` and `guest-run-04.bmp`.
Crouch idle selects clip 1; crouch movement selects clip 4. This independently
confirms the existing stance mapping. Normal W, Shift+W and Ctrl+W all produced
the same native act/speed (2/2) and running flag (32), so those modifier inputs
did not yet create distinct source gaits. Synthetic Raw Input modifiers now
use legacy generic VKeys while preserving scan code, side and release flags;
actual gait comparison after this correction remains pending.

The later prone/crawl frames from this run are invalid visual evidence: moving
downhill and placing the viewer at the source's height behind it embedded the
viewer below uphill terrain, after which it fell. The run stopped when fresh
source motion ceased at `crawl-05`. Viewer placement now starts 3 m higher and
requires three fresh settled position samples within 3 seconds. Each movement
phase returns the source to its recorded initial position and heading through
the existing validated debug teleport. No terrain height is inferred.

Both runs closed normally without forced termination, restored both saved
baseline loaders/configurations and matched all ten real save files. Neither
called system input/focus APIs or switched the user's desktop. The full audited
Normal/background-ON check before the second run passed all 110 CTests; the
latest modifier/placement changes require a fresh check and runtime comparison.

Run `6695dde8e5404a0cbfe7ed5f045985c3` stopped before capturing: the viewer
landed 1.44 m away horizontally from its airborne teleport point, exceeding
the test's 0.75 m tolerance. It was standing on terrain with valid body/pose
readback. Viewer landing now allows 2 m of natural drift; source benchmark
resets retain the stricter 0.75 m limit. New detached cases cover that measured
slide and its refusal for a benchmark. This run also closed normally, restored
both baselines and matched all real saves.

## Turn collection correction

The production raw-source collector was gated by the delayed visual movement
class and stance. While the display still showed the previous movement, that
gate cleared the turn state and advanced its accepted-source cursor, discarding
the beginning of a real pivot after stopping. Collection now follows the raw
source's actual movement, stance and gun activity under the existing lifecycle
gates. Playback waits for visual idle in the same stance; bounded pending events
keep their original identity and receive age. Existing stale, gap, scene,
moving, firing/reloading and body-transition cancellation remains in force.

Pure and driver recording regressions exercise an immediate W release/pivot
and a stance change at 30 Hz source/60 Hz owner with 100 ms interpolation. They
check that collection survives visual lag, no turn plays early, and the correct
finite turn is delivered once. They also check expiry and gun interruption.
Compilation and a fresh private-desktop gameplay comparison are pending.

The updated Normal/background-ON candidate passed all 110 audited CTests,
including 4,303 raw-source turn checks, the driver recording regressions and
181 detached worker-session checks. Loader: `98F4861B...B5C5504`; receipt:
`build/worker-runs/20261008-001216-3fcf92bb08744803be646fc3037c6bf5`.

Run `3aa91d5d8fce47f48f06dde7323d0f32` recorded 39 timely frames and source
motion during every held movement capture. Corrected modifier packets now
produce distinct native gaits: normal W is speed class 2/running flag 32 and
clip 11, Shift+W is class 3/sprint flag 4 and clip 5, Ctrl+W is class 1/walking
flag 16 and clip 10. Approximate settled speeds are 4.6, 10 and 1.2 m/s.
Crouch idle/move select 1/4; prone idle/crawl select 7/6, with crawl reaching
about 1.65 m/s. The viewer remains on terrain with valid partner readback;
grass and the viewer can occlude the prone partner in these frames.

The run stopped at the aimed pivot because the source body heading changed
0 degrees. Run `e016fae8e36c4a758b2a6bce1460562e` then collected 26 combat
frames: actual host RMB/fire frames show a lowered handgun and no aim/fire
flags, so they do not test genuine source gun poses. Keyboard R briefly
produces the reload flag. Both runs closed normally, restored both baselines
and matched all real saves.

Exact retail mouse decoding exposed a necessary worker condition. At
`1A45908` the handler calls GetCursorPos, then ScreenToClient. Failure skips
raw axes, and an out-of-client position also rejects button-down events.
The worker intercepts SetCursorPos but had no process-local GetCursorPos
answer. A private client-centre getter and focused API-contract fixtures were
added. The resulting candidate `C2185000...94A1CFE` passed all 110 audited
CTests. Run `aab679e799464a22b8ce715733a913ab` repeated 39 valid movement
frames, but the first aimed source pivot changed body heading only 0.00565
degrees. This run also closed normally, restored both baselines and matched
all real saves. The getter correction alone has not established source mouse
input. Separate worker-only counters are being added for packet posting,
payload reads, cursor refusals and DirectInput consumption; no gameplay
acceptance will be inferred from API counts alone.

## Qualified source gun input and retained turns

The next Normal/worker-ON candidate passed all 110 audited CTests (receipt
`20261008-005747-501b47817c5c4c0db7911d5bea90efe7`, loader
`1712C83A...238BDE8`). All eleven research options were OFF. Its worker-only
input counters distinguish posted packets, successful payload reads, unique
tokens and cursor/window qualification; they do not assert gameplay acceptance.

Run `b03d48b327c14012a13a02bc58513dda` supplied actual source gun input:
the host's screenshot shows raised aiming arms and a reticle, its handgun
magazine falls from 8 to 5 during firing, and fresh guest readback receives
Aim (1), Aim+Fire (3) and Reload (8). Thirteen timely frames were captured.
Eleven mouse packets produced eleven unique payload reads (33 successful
copies), with matching Present/cursor windows and no cursor refusals. No
DirectInput mouse consumption was recorded. The source body did not pivot
with the earlier 200 ms RMB stimulus: a camera-only displacement subsequently
changed body heading when aiming began. A turn test must therefore observe
fresh genuine aim and settled body heading *before* sending the tested mouse
displacement. Command acceptance or old camera alignment cannot prove a turn.
Run `377e2444b6594c4d8900376806e30416` separately completed all 26 combat frames.
The partner still has lowered arms and no automatic gun pose.

A second production turn cancellation was exposed by a recording regression:
when a delayed new-stance pivot coincided with native confirmation/completion
of an old clip, the mapper returned before updating its candidate stance.
Repeated stance urgency then erased the newly retained pivot. Stance-only
urgency now preserves queued turns belonging to the current stance and cancels
incompatible turns; movement, gun activity and lifecycle cancellation retain
their existing rules. An incompatible active standing/crouching resident turn
can stop through the unchanged native stop gate before the matching new turn
plays. The regression requires native stop confirmation, no same-tick replay
and natural finite completion. A raw animation-only stance fallback also
clears stale pending turns. The first audited run failed this new regression;
the corrected candidate passed the full suite above. Live pivot comparison is
reported separately below when a qualifying source stimulus is available.

## Read-only comparison of source and partner gun inputs

Run `f0d28ff964fd48a9bbf3ad14253546b9` captured the frozen Gun19 reader on
host body 0 and guest partner body 1. Both acquisitions passed the private
session, exact process/module, native-code, copied-save and before/after
ownership guards while the guest continuously received fresh source Aim (1).
No native calls or process writes were made. The source's ten animation inputs
include Motion+58=1 and nonzero Gun+2D0/+2D8 values; its bitmap is `[1,0,0]`.
All ten corresponding partner inputs and its bitmap remain zero, with channel
2 in Idle (node 89) and no queued state. This confirms that delivered aim
intent is insufficient: the partner's native weapon/motion initialization
still needs a qualified natural owner/lifecycle path. Copying source pointers
or forcing the upper state is not an implemented fix.

The frozen V3 timing reader refused both bodies with
`timing_workspace_layer_count`; it produced no timing samples. Its bounds
were not expanded. Static exact-retail analysis additionally established the
model-record allocation's requested byte span, two-pass initialization and
carve interval. This is requested capacity, not an OS allocation size or proof
of successful equipment binding. Concrete current allocator ownership and the
natural gun scheduler activation remain separate gates. Frozen audit artifacts
are under `build/combat-owner-closure-20261007`; qualified read-only receipts
and the compact comparison are under the run's `readonly-captures` and
`gun-comparison.json`.

Root independently passed both standalone disk audits. A separate proof pins
the default-context allocator singleton, its virtual slot and forwarded
size/alignment/flags. Equality with the current context and a live owner lease
remain unproved; neither frozen reader nor the production backend was expanded.

All four runs in this section closed normally, restored both saved loaders
and configurations, and matched all ten real save files. Neither system input,
focus nor desktop switching was called. Their restored loader baseline was
`166E0220...8C8ED5`; the older normal21 restoration in the previous report is
historical, not the current baseline.

## Live body-pivot qualification

The revised runner requires a fresh observed release, genuine received
stationary Aim, at least three distinct finite body-heading samples spanning
250 ms within one degree, and at least 800 ms of initial aiming before its
baseline. It renews bounded holds, waits for exact native command acceptance,
uses the opposite role as intent observer even when source and viewer match,
and records baseline, mouse-dispatch, native-acceptance and measured heading
times. A qualifying post-mouse excursion must exceed 70 degrees within one
second; refusal cannot create a successful pivot frame.

Run `fca0d6fdf0ae4a1296f6962b21be74b3` correctly refused dx700: its settled
source body heading did not change. Run `4b91a2f527d04da68ec2ffd973a20a4b`
then captured ten frames using three separated dx1500 inputs while genuinely
aiming. The source's body turns, the partner plays standing clip 13, and the
native mapper records confirmation and three natural finite finishes of
about 833–835 ms before returning to idle. The decoder passes both mouse
amounts unchanged; its downstream aiming/camera listener has not been audited.
This is a measured test stimulus correction, not a native input scaling patch.

Run `fac92b9b7de248cbb3bd337243a1dc67` qualified a single dx1500 pivot:
body heading changed from -60 to -147.12022 degrees after the settled baseline.
The fresh guest pivot frame has Aim (1), mapped clip 13 and phase `playing`;
its settled frame returns to clip 0. The reverse test stopped on a controller
receipt race: native renewal was accepted after 735 ms, inside its 750 ms
deadline, but the controller returned after that deadline and skipped reading
the existing receipt. The helper now always checks the first exact receipt
after controller return, accepting only a native timestamp inside the original
deadline. A missing/late receipt does not add a wait. Detached delayed-return
cases cover all three outcomes. Both refused runs and the successful axis run
closed normally, restored both baselines and matched all ten real saves.

The next run, `d3e07ce6397046bf8d3b27ac65dba474`, qualified both standing
directions (-87.12 and +72.22 degrees), but another renewal left only a narrow
receipt-observation margin before the crouched test. Renewal now starts at
400 ms, preserving the 1500 ms hold and 750 ms native acceptance bound. A
detached timeline with 150 ms of controller I/O per command verifies that
readiness and the pivot still pass with all accepted renewal gaps in bounds.
The final runner has 285 detached checks; the full Normal/worker-ON check
passed all 110 CTests (37.53 seconds), 198 input checks and 84 policy checks.
Receipt: `20261008-014027-f0dd5679a209404796d85fecfec111a3`.

Final live run `1a2de304f22243d2b8900a7f2301c742` completed all 16 frames.
Fresh settled-baseline post-mouse excursions were -75.19699, +75.19696 and
-82.24367 degrees for the two standing directions and crouched pivot. The
standing and crouched pivot frames show clips 13 and 17 playing respectively;
the reverse frame catches the stop handoff, and native records confirm clip
14 playing and finishing naturally. Standing turns finish at about 834 ms,
crouched turns at about 950 ms. Each settles back to its stance's idle clip
(0 or 1). The five held strafe captures show source Aim+Run (33), changing
position and clips 10/11, followed by three idle frames. This confirms
locomotion while aiming, not an upper-body aim pose.

The final run closed without forced termination, restored both loader/config
baselines and matched all ten real save files. Its receipt records no system
input, focus or desktop switch. No game or worker process remained afterward.
This is live private-desktop evidence for this Worker-ON build; a normal
Worker-OFF DLL has separate automated/release validation below.

## Normal release and handoff

The Normal/Release/worker-OFF build passed all 110 audited CTests in 40.57
seconds, plus 198 input and 84 policy checks. All eleven research flags and
the background-worker flag are OFF; every recorded build input and the DLL
hash were rechecked before packaging. Receipt:
`20261008-014422-3e498a65374749e8b8cecab9aad471c8`. Loader SHA-256:
`DC5F5FDF69DE33EBF1D0A17D4F678A5261D07B1B04F33384897A74FA3B1B3755`.
Defender's custom scan completed successfully before installation.

The exact checked DLL was staged at `build/native/dinput8.dll`, the path
used by `Play-Offline-Coop.bat`, and installed through the owned installer in
both local MGSV folders. The previous launcher DLL is preserved at
`build/native/dinput8.pre-animation-20261008T014748Z.dll`; both installations
also retain their owned rollback backups. The runtime archive, retail EXEs,
Steam DLLs and all ten real save files were rechecked unchanged after the
install. No game or worker remained. The delivery receipt is
`build/animation-release-20261007.json`. The live gameplay evidence above is
for the separately checked Worker-ON build; the exact normal DLL has not been
launched visibly or claimed to have fresh normal-profile gameplay validation.

Remaining gun work: prove a complete natural player caller linking the
current gun service (cached context+68/this+1B0) to its owned phase root and
actual update invocation, with lifecycle and ABI qualification. The inspected
complete virtual+18 callers instead use context+80/+8. A cache store or matching
virtual slot does not qualify activation. Keep the frozen readers and default
OFF backend until that relation is established. The debug-menu worktree,
weather changes and AI replication are outside this animation release.
