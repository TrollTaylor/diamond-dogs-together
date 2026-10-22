# Round 1, track A: non-weapon action animations (stance lag, act census, DM overlay lever)

Branch `wp/anim-actions` (based on `20f4afe`). Static work only: nothing was launched, no worker ran,
nothing under `D:/Games` was written. Every result below is fixture evidence; the hidden tests at the
end are for the coordinator.

Inputs: the investigation reports `critic.md` (P0.3, P0.6, P2.10, 3.6, section 6), `action-anims.md`
(sections 3, 7.2) and `log-forensics.md` (4.3, section 6 items 2 and 3).

## 1. What changed

| Part | Default | Key | Files |
|---|---|---|---|
| Stance lag: the native stance window is 500 ms instead of 2 s, behind guards | **on** (native drive) | `partner_native_stance_fast=0\|1` (0 = the old 2 s rule) | `partner_recon.*`, `partner_driver.cpp` (StepRecon, LogReconEvents) |
| Act census telemetry (`partner_act`, `partner_pad_dropped`) | **on** (log only) | `partner_act_log=0\|1` | `partner_act_log.*`, `partner_driver.cpp` (TickActLog) |
| DM overlay lever for graph-0 action states | **off** | `partner_native_overlay=off\|debug\|map` | `partner_dm_overlay.*`, `partner_native.*` (suspend sub-mode), `partner_driver.*`, `partner_receipt.*`, `lua/coop/partner_sync.lua`, `lua/coop/debug_player.lua` |

The stance window and the census are safe without a runtime test (the window uses the existing,
proven corrective STANCE tap/hold; the census only logs), so they are on. The overlay plays
DirectMotion states on body 1 that no hidden test has played yet (critic T12), so it is off until
the coordinator has run H3 and H4 below.

No new native address is called or patched. The overlay reuses the shipped comparator patch
`0x1198E25` (`partner_dm_patch`, pinned in `native/partner_player_contract.hpp`) and the Lua binding
`Player.RequestToPlayDirectMotion` (`0x9DA7D0`) that dm=map already calls with state names
(`stateStandIdleLeft`/`stateSquatIdleLeft`, GANI 8/9). The named-state branch (`0x1199140` ->
controller vt+0x170) is reached by the engine itself. There is no network wire change.

## 2. Stance lag (critic P0.3, log-forensics 4.3)

**Problem.** The native drive passes STANCE (0x2) from the remote's pad, but a lost or mis-classified
edge leaves body 1 in the wrong stance. The level fix waited `stance_fix_ms = 2000` before one
corrective tap (prone: a 400 ms hold), so crouch/prone/stand showed 1.5-2.5 s late and the body
lingered in crouch idle after the remote stood up. The live log's `stance_mismatch; remote=3; body=1;
ms=1502` (H:731619) was the guest being downed (act 5 is prone, critic 3.6): the downed remote lies
down without a STANCE press, so only the level fix ever puts body 1 down.

**Rule now (partner_recon `NativeParams`).** The remote's stance level is the anim extension's
Q+0x30 (1 stand, 2 squat, 3 crawl), debounced 150 ms as before. A mismatch held **500 ms** asks for the
corrective input (tap, or a hold toward prone), at most every **1000 ms**, unless a guard holds it.
The guards never let the fix fight a stance change already in flight:

| Guard (`Report::fix_wait`) | Holds the fix while |
|---|---|
| `pad` | the remote's live input shows STANCE (held, its "now" word, the edge set, tap12, long12), and 400 ms after |
| `body` | body 1's own stance changed within 300 ms |
| `settle` | either act was outside 1..6 within 800 ms (dive 57, evade 27, Quiet's evade 58, hop 28, fall/land 25/26, aim 11..20) |
| `state` | either act was a sleep, carry, carried, dead or DM act (39, 40, 41, 42..44, 131..133, 139) within 2000 ms (their native exits, e.g. the wake 133 -> 5 -> 3 -> 1, run first) |
| `gun`, `reload`, `switch` (driver, gun stage) | the remote aims/fires/reloads/changes weapon (v5 gun flags or the sample's ActionFlag), body 1's reload event machine holds, or body 1 is mid-switch |
| `carry` (driver) | body 1 is carried by the local player (coop_carry) |
| `downed` | the remote is downed (coop_hud teammate view, fresh within 1.5 s) and its level is not prone: a downed remote is mirrored lying, nothing pulls it up |

Non-locomotion acts on either side never get a fix (unchanged). The legacy (non-native) rule is
unchanged; `partner_native_stance_fast=0` restores the 2 s rule for the native drive too.

**Expected timing.** A lost crouch edge: `stance_fix` about 650 ms after the remote's change (150 ms
debounce + 500 ms). A prone hold in progress: the fix waits until 400 ms after the remote released C.
Prone -> stand needs two taps (prone -> crouch -> stand), about 1.6 s.

**Log lines** (own `partner_recon` caps, unchanged families):

```
partner_recon=stance; from=1; to=2; suspend_ms=700
partner_recon=stance_wait; reason=pad|body|settle|state|gun|reload|switch|carry|downed; remote=2; body=1; ms=<mismatch ms>
partner_recon=stance_fix; remote=2; body=1; via=tap; ms=67; fixes=<n>; after_ms=<mismatch ms, ~500>
partner_recon=stance_mismatch; remote=3; body=1; ms=1500     (only when the fix did not resolve it within 1.5 s)
```

`stance_wait` is logged once per guard and mismatch episode. `after_ms` is new on `stance_fix`.

Fixtures: `tests/partner_recon_tests.cpp` `TestNativeStanceFix` (window, lost crouch/stand/prone,
a late native transition never fought, pad/body/settle/state/gate/downed guards, stale input) and
`tests/partner_driver_tests.cpp` `TestRound1Actions` (the gun gate through the driver, the 2 s rule with
`stance_fast=0`).

## 3. Act census telemetry (critic P0.6, log-forensics section 6 items 2 and 3)

`native/partner_act_log.{hpp,cpp}` (pure) and `TickActLog` in the driver, called once per tick
**before** the ride/range guards, so the acts of a hidden rider are logged as well.

**Act edges.** One line on every change of the remote's act (the interpolated pose sample's anim
extension: what the viewer is shown, `interp_ms` behind) and of body 1's act (Q+0x18[1], only while
realized):

```
partner_act; t=<GetTickCount64>; edge=remote; remote=57(dive); body=1(stand_idle); match_ms=pending
partner_act; t=<..>; edge=body; remote=57(dive); body=57(dive); match_ms=120
partner_act; t=<..>; edge=miss; remote=6(prone_crawl); body=57(dive); match_ms=miss; held_ms=400
```

- `match_ms` on a remote edge: 0 when body 1 already shows the act (it led), else `pending`.
- On a body edge that reaches the pending remote act: the latency in ms; otherwise `-`.
- A `miss` line when the remote left the act unmatched, or held it 3000 ms unmatched (once).
- `na` for an invalid side (remote sample not live / no anim; body unrealized).
- Own cap 3000 lines per mission, then `partner_act=capped; limit=3000` and, at most once per
  second while something happened, `partner_act=summary; remote_edges=..; body_edges=..; matched=..;
  missed=..; dropped=..`.

Family names (action-anims.md section 3 with critic 3.6; `?` marks likely/speculative ones):

| act | name | act | name |
|---|---|---|---|
| 1 | stand_idle | 39 | carry_body |
| 2 | stand_move | 40 / 41 | dead / dying? |
| 3 | crouch_idle | 42 / 43 / 44 | carried_start / carried / carried_putdown |
| 4 | crouch_move | 45 | damage? |
| 5 | prone_idle | 49, 50 | interact? |
| 6 | prone_crawl | 51..53 | fulton_catch? |
| 8 | stand_run_step? | 54..56 | ledge_hang? |
| 9 | supine_idle? | 57 / 58 | dive / evade_quiet |
| 11 / 12 / 13 | aim_stand_idle / aim_stand_turn / aim_stand_move | 59 | interact_held? |
| 14 / 15 / 16 | aim_crouch_idle / aim_crouch_turn / aim_crouch_move? | 67..74 | cover? |
| 17, 19, 20 | aim_prone? | 75, 76 | cure |
| 21 / 23 | ladder / wall_climb | 77, 78 | cbox? |
| 22 | pipe | 79..83 | trash_box |
| 24 | step_on | 91..96 | cqc_hold? |
| 25 / 26 | fall / land | 97 / 98..108 | heli_board? / heli |
| 27 | evade | 109 / 110 / 111 / 112 | vehicle_in / vehicle_dismount? / vehicle_seated / vehicle_out |
| 28 | cliff_drop | 121 / 122 / 123 / 124 / 125 / 126 | horse_ride / horse_mount / walker_mount / walker? / walker_drive / walker_off |
| 29 | hit? | 131 / 132 / 133 | sleep_onset / sleep / sleep_wake |
| 0 | none | 139 / 142 | dm / dive_variant? |

Everything else prints `?`. 0.13.4 renamed 21-24, 28, 75/76 and 79..83 from the static writer census of
`Q+0x18[id]` on 1.0.15.4 (owners named by Ghidra): 21 `LadderActionImpl`, 22 `PipeActionPluginImpl`, 23
`ClimbActionPluginImpl` (wall / cliff free-climb), 24 `StepOnActionImpl` (low vault, onto a ledge, up out of a hang),
28 `CliffActionImpl` (edge drop), 75/76 Cure, 79..83 `TrashBoxActionPluginImpl`. They were heli?, ladder_climb?,
fall_entry?, hop?, knockdown? and stun?. 0.13.5 (F5, docs/mounts.md) renamed the mount acts the same way: 121 the
RideHorse ride state (PlayerInfo sets ON_HORSE only for it; it was vehicle_passenger?), 122 the RideHorse get-on /
get-off / turn states (it was horse?), 123 / 125 / 126 the D-Walker get-on, `StateDrive` and leave states; 124 has
no writer and keeps walker?.

**Dropped pad presses.** Per second, the remote's presses (its input frames' edge sets, i.e. the
sender's pad before this copy's mask; v1 frames: rising held bits) that body 1's window did not pass,
logged only when something was dropped (own cap 3600 lines, then `partner_pad_dropped=capped`):

```
partner_pad_dropped; action=1; stock=0; call=0; subject=0; weapon=0; cqc=1; light=0; vfire=0; marker=0; fire=0; other=0; other_bits=0x0; mask=0x224005e; native=1
```

Groups: ACTION 0x20, STOCK 0x80, CALL 0x200, SUBJECT 0x400, weapon select 0x7800, CQC 0x8000,
LIGHT_SWITCH 0x20000, VEHICLE_FIRE 0x80000, PLACE_MARKER 0x400000, FIRE 0x10 (dropped by the mask or
the weapon gate), other (DECIDE, MB_DEVICE, SIDE_ROLL, VEHICLE_CALL; `other_bits` names them).
`mask` is the window's effective mask minus the weapon gate's strip; `native=0` means the native
window was not armed (fallback): every group press then counts. Movement and gun bits never count
as `other`. Log only: no behaviour change.

Fixtures: `tests/partner_act_log_tests.cpp`; driver flow in `TestRound1Actions`.

## 4. DM overlay lever (critic P2.10, action-anims 7.2)

`native/partner_dm_overlay.{hpp,cpp}` (pure machine), `TickOverlay`/`OverlayBlocked` in the driver,
the arbiter's suspend sub-mode in `partner_native.cpp`, the reply branch in `partner_sync.lua`.

### 4.1 Flow

1. **Request.** The debug command (`overlay_state`, any mode but off) or, with `map`, a remote act
   from the map held 150 ms that body 1 does not show natively.
2. **Suspend.** The overlay asks the arbiter to suspend (`In::suspend_reason = kSuspendOverlay`,
   "overlay"). The arbiter leaves like a visibility leave (deferral, window, dm=map rearm, epoch) but
   counts **no fallback, no latch, no network leave**. Body 1 stays realized; the effective settings
   are the configured ones (apply=xform), so the fallback xform places it at the remote pose every
   tick. The overlay refuses unless `partner_apply` is a transform driver.
3. **Own the DM.** TickDm (and dm=map) stays out while the overlay runs. If dm=map still owns a clip
   (the drive was already in fallback), TickDm runs once with dm=off to release it first.
4. **Patch.** Once native is clear (not active, window and deferral disarmed) and dm=map is idle,
   the comparator patch is applied (`partner_dm=patched; rva=0x1198e25; ...`).
5. **Play.** DM_PLAY with the reply value `64 + 32 * loop + index`; Lua plays
   `{{"coopLoco", 1}, {OVERLAY[index], loop, "", "", "", true}}`. Only when no DM is active on either
   body (q18[0], q18[1] != 139), no request is pending (g+0x388 & 0x3C), this reply carries no
   Warp/SET_PARTS/SET_ALLACTIVE/COPY, and **the local camera connect is idle** (CAM+0x1F0 and
   CAM+0x200 zero, because DM entry and End write both to 0 for any id). Otherwise
   `partner_overlay=deferred; reason=cam_connect|host_dm|busy|pending|reply|dm_read`, and an abort at
   the request's deadline.
6. **Confirm.** q18[1] == 139 within 2500 ms (`playing`, with `rec18` and `code_match`: rec+0x18
   equal to the state's StrCode64, or stateDirectPlay for a clip path), else `unconsumed` (our
   pending request is cleared).
7. **Run.** Debug: until the request's ms (default 3000, at most 60000) or `overlay_stop`. Map:
   loops while the remote stays in the mapped act (exit after 250 ms unmapped; a switch to another
   entry after a 500 ms dwell stops and plays the next one **without** returning to native); a
   one-shot runs out on its own (max 5 s). A loop the game ends is replayed once. The local camera
   connect appearing mid-play (the viewer enters a vehicle) logs `conflict` once.
8. **Stop and release.** DM_STOP (never while the local body is in DM: Stop is global; one retry
   after 1000 ms), wait for q18[1] != 139, `released`. If body 1 went away (ride/range hide,
   unloaded) the overlay aborts **without** a Stop (a stale global Stop would end the next DM).
9. **Resume.** The suspend reason clears; the arbiter's fallback re-enters **at once** (reason
   `resume`: no cooldown, no 2 s settling hold) unless another blocker showed up meanwhile, in which
   case the normal rules apply again. Entering reverts the patch (dm=off), places and arms as usual.
   A map overlay is not restarted for the same remote episode (no suspend/resume thrash).

Eligibility (`OverlayBlocked`, refused or aborted with this reason): `terminating`, `stage`, `parts`,
`range`, `ride` (visibility hidden or restoring), `carry` (coop_carry hold), `vehicle`,
`pose_lease` (the private native pose window), `fault`, `patch_failed`, `dm_revert_failed`,
`apply` (not a transform driver), `body_state` (body 1 in a sleep/carry/carried/dead act another
mirror owns).

### 4.2 Camera: the hook point for track V

The overlay keeps the existing `cam_connect` refusal. `Hooks::dm_camera_skip_armed`
(`partner_driver.hpp`, default null) is the hook point for the camera-skip module (critic 5.2: the
DM entry/End store skips at `0x1198D22`, `0x1198D81`, `0x11992E9`): while it returns true the overlay
plays with a non-zero camera connect. This track does not implement or arm those patches. Track V:
point this hook at its "skips armed" query (or call the overlay's In::cam_skip from its own path).

### 4.3 Catalogue (append only; the index is the reply value's low 5 bits and `OVERLAY[index]`)

Each state's StrCode64 was computed with the investigators' `foxhash.sc64` (validated:
`stateGunHold` = `aa0ea22a5e0e`), found exactly once in the shared node pool of the retail
`TppPlayer2_layers.mog` (copy `build/partner-turn-resident-audit-20261007/package-00_fpk/...`), and
**absent** from the SQ-4 graph 2/3 node list (`mog-nodes-2_3.json`); the controls `stateThrowHoldEnd`,
`statePlaceSwitchOn`, `stateBinocleLoop` and `stateGunHold` were correctly found in graph 2, and the
non-existent `stateDamageBig` absent (`tools/partner_overlay_mogcheck.py`; it takes the directory of a
StrCode64 module `foxhash.sc64`, the MOG and the SQ-4 node list). The clip paths
are `player2_resident.mtar` PathCode64 entries (motion-levers `verified-clips.tsv`).

| index | entry | code | loop | map |
|---|---|---|---|---|
| 0 | stateDamageBigFront | 3db7bdedaea7 | 0 | 29, 75, 76 |
| 1 | stateDamageBigBack | ad926744b0e3 | 0 | - |
| 2 | stateDamageBigLeft | 1888402f2d17 | 0 | - |
| 3 | stateDamageBigRight | 074254c360e9 | 0 | - |
| 4 | stateDead | dad5759b0203 | 0 | 40, 41 |
| 5 | stateCBoxIdle | 039c85aa4598 | 1 | 77 |
| 6 | stateCBoxMove | 6e06f122e3b3 | 1 | 78 |
| 7 | stateEludeIdle | 1cf4b93bd4fc | 1 | 54, 55, 56 |
| 8 | stateClimbIdle | 2928469590b4 | 1 | 21, 23 (\|vy\| <= 250 mm/s) |
| 9 | stateClimbMoveFront | 489a8f233d63 | 1 | 21, 23 (vy > 250) |
| 10 | stateClimbMoveBack | 66d0af83390e | 1 | 21, 23 (vy < -250) |
| 11 | stateClimbIdleR | 04cf99dc27d5 | 1 | - |
| 12 | stateStandIdleLeft | 904c34eb558a | 1 | - (DM-proven control, GANI 9) |
| 13 | stateSquatIdleLeft | f0cde06b01db | 1 | - (DM-proven control, GANI 8) |
| 14 | stateCqcDeath | e7e552499013 | 0 | - |
| 15 | statePutDown | 20b90885c4c1 | 0 | - |
| 16 | stateFultonStart | 75d780810388 | 0 | - |
| 17 | stateLedgeDrop | 6f4869f627ce | 0 | - |
| 18 | stateCrawlSupineIdle | 61ffe4f8b02b | 1 | - |
| 19 | snapdam_s_fre (clip) | fc53062c44565522 | 0 | - |
| 20 | snapdam_p_die_idl (clip) | fc5046c89dad210c | 1 | - |
| 21 | snapnon_s_fal_lp_l (clip) | fc50fb7461e1772c | 1 | - |
| 22 | stateRideOnIdle | f241f3bcf60b | 1 | - (mount research probe, docs/mounts.md) |
| 23 | stateGallop | ce7e8409eb61 | 1 | - (mount research probe) |
| 24 | stateRideWalkerWalk | 1b8b642289d5 | 1 | - (mount research probe; no code reference, graph unverified) |

The damage direction is not on the wire, so the map plays Front for every hit; the other directions
are for the debug command. `vy` is the remote's vertical speed (EMA of the sampled y). The
`partner_dm_overlay_tests` fixture reads `lua/coop/partner_sync.lua` and checks that `OVERLAY` has the
same strings in the same order.

### 4.4 Commands and settings

- lab.ini `partner_native_overlay=off|debug|map` (default off), worker `labsetting
  partner_native_overlay=...` (native profile only).
- F9 / worker debug (`lua/coop/debug_player.lua`): `overlay_state <entry> [[loop] 0|1] [ms]`
  (ms 100..60000) and `overlay_stop`; results `overlay_queued;state=<entry>`, `overlay_stop_queued`,
  `overlay_args`, `overlay_refused`. They reach the driver as partner one-shots `overlay=<entry>|stop`,
  `oloop=0|1`, `oms=<ms>` (also valid in `partner.trigger`). An unknown entry is refused by the
  native parser.

### 4.5 Log lines (own cap 400 per mission, then `partner_overlay=capped; limit=400`)

```
partner_actions=configured; overlay=debug; act_log=1                       (MissionStart, only when the lever is on)
partner_overlay=queued; state=<entry>|stop; loop=<0|1>; ms=<ms>; setting=<off|debug|map>
partner_overlay=<event>; state=<entry>; loop=<0|1>; source=debug|map; reason=<r>; ms=<ms>; phase=<phase>; native=<arbiter mode>; remote_act=<a>; body_act=<b>; q18=0x<q0>,0x<q1>[; extra]
   events: request, refused, deferred (+cam1f0, cam200, g388), play (+gani), playing (+rec18, code_match, rec248),
           conflict (+cam1f0...), stop, stop_retry, stop_timeout, finished, lost, unconsumed, released, abort
partner_overlay=native; mode=leaving|fallback|entering|active; reason=overlay|resume|ok; suspends=<n>   (the arbiter's suspend cycle)
partner_overlay=native; step=dm_revert|place|armed; ms=<ms>
partner_lua=event=dm_overlay; index=<i>; loop=<0|1>      (Lua; bad index: event=dm_refused; reason=bad_overlay)
```

The arbiter's own `partner_native=...` transition lines (64 cap) are **not** spent on suspend cycles.

### 4.6 Arbiter suspend sub-mode (`partner_native`)

`In::suspend_reason` (pointer compared, then text): EnterBlocker and HardLeave return it right after
the visibility guard; `leave()` on it sets `State::suspended`, counts nothing and marks
`Out::suspend_cycle`; fallback stays blocked with that reason; when it clears and nothing else
blocks, fallback goes straight to entering with reason `resume` (`State::resumed` until active).
Another blocker during the suspend clears `suspended`, restoring the cooldown and the settling hold.
Fixture: `tests/partner_native_tests.cpp` `TestSuspend` (six suspends never latch, immediate
resume, another blocker restores the hold, suspend while entering, suspend while already in
fallback keeps the normal hold).

## 5. Hidden tests for the coordinator

Build: `pwsh -NoProfile -File tools/build.ps1 -Configuration Release -ReleaseGun -BuildDirectory build/native`
(passed here, see section 7). Worker runs need the worker build as usual.

### H1. Stance lag (stance window on, default)

Scenario `tools/worker/scenarios/partner-traversal-census.txt`, section "Stance cycles", or the user's
two-window test: crouch (C), stand (C), prone (hold C), crouch, stand, then a fast C-C.

Expected on the viewer (the guest for the host's body):
- frames `census-crouch`, `census-stand`, `census-prone`, ... show body 1 in the remote's stance;
- any `partner_recon=stance_fix` has `after_ms` about 500..700 (never ~2000);
- `stance_wait; reason=pad` around the prone hold and the double tap, not a fix in the middle;
- `partner_recon=stance_mismatch` (1.5 s) rare or absent; no stance fix while either act is 27/57/58.
A/B: `-LabSetting partner_native_stance_fast=0` must show the old `after_ms` >= 2000.

### H2. Act census (traversal)

`pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-traversal-census.txt -PartnerProfile native -NativeStage loco`

Expected in both logs: `partner_act; t=...; edge=remote|body|miss; ...` for every section (dive
`57(dive)` then `6(prone_crawl)`, evade `27(evade)`, the 3 m fall `25(fall)`/`26(land)`, rolls),
`match_ms` values (dive about 100..500 ms; act 6 previously "never shown": a `miss` or a long
`match_ms` names it), `partner_pad_dropped; action=1` after the E presses and the vault probe, and
which groups Q, F, 1, 2, X and Z land in. Record every remote act id with no name (`?`) or a `?` name
and the frame where it happened: that is the census the act table and the overlay map need.
No ladder/ledge/window spot is known in the repo; the scenario has a commented template.

### H3. Overlay states on body 1 (T12)

`pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-overlay-states.txt -PartnerProfile native -NativeStage loco`

Expected per state (guest log): `queued` -> `request` -> `native; mode=leaving; reason=overlay` ->
`partner_dm=patched` -> `play; ...; gani=<64+32*loop+index>` -> `partner_lua=event=dm_overlay` ->
`playing; ...; q18=0x1,0x8b; rec18=0x<code>; code_match=1` -> `finished` (one-shot) or
`stop; reason=timeout` + `released` -> `native; mode=entering; reason=resume` -> `native; step=...` ->
`native; mode=active; reason=ok`. No `partner_native=leaving|fallback|latched` lines and the
arbiter's `fallbacks` unchanged across all cycles. The frame per state shows the pose (or not).
Report per state: shown (yes/no/idle pose), `played_ms` of `finished`, `code_match`. A state that
`finished` in < 300 ms or shows the idle pose transitions out at once (record it; it must not be
mapped). `overlay_stop` must give `stop; reason=debug` then `released; reason=debug`.

### H4. Overlay map mode

Run any scenario with real mapped remote acts and `-LabSetting partner_native_overlay=map`, e.g. the
friendly-fire rescue scenario (`coop-rescue-lab-default.txt`; whether a friendly hit shows remote act
29/75/76 before the mod's downed state is itself unverified: the census in H2/H4 tells) or a
ladder/ledge spot from H2. Expected: `partner_overlay=request; ...; source=map; ...; remote_act=29`
-> play `stateDamageBigFront` -> `finished` -> resume; no repeat while the remote stays in the act;
ladder: `stateClimbIdle`/`MoveFront`/`MoveBack` switching (`released; reason=switch`) without
`native; mode=entering` in between. Watch for any `partner_native=latched` (must not appear) and
for a body left in DM (q18 1 = 0x8b after `released`).

### H5. Camera safety

With the viewer seated in a vehicle (the `coop-ride-vehicle.txt` pattern) and the overlay on, run
`<viewer> run overlay_state stateDead 0 3000`. Expected: `deferred; reason=cam_connect; cam1f0=0x88...;
cam200=1`, then `abort; reason=cam_connect` at the deadline, **no** `play`, and the viewer's camera
unaffected. (With track V's camera-skip module armed, the same command plays.)

### H6. Default off

With lab defaults (no `partner_native_overlay`), `overlay_state stateDead` must log
`partner_overlay=queued; ...; setting=off` and `partner_overlay=refused; ...; reason=setting_off`, and
nothing else changes (native stays active).

## 6. Integration notes

- `CMakeLists.txt`: the two pure modules join `mgs_coop_core` through a separate `target_sources`
  line; `partner_dm_overlay_tests` takes `lua/coop/partner_sync.lua` as its argument.
- `native/lab_config.cpp`: the main key chain is at MSVC's block nesting limit (C1061 with three more
  `else if`); this track's keys are parsed first by `ParseRound1ActionsKey`. Other tracks adding keys
  should do the same.
- `partner_native.{hpp,cpp}`: edits are in `EnterBlocker`, `HardLeave`, `leave()`, the fallback,
  entering and leaving cases, plus `SameReason`; track W's NativeMask/hold code is untouched. The
  reason "overlay" is not appended to `partner_native_net` `kReasons` (append-only codes; the
  capability message reports it as "other"); the integrator may append it after track V's reasons.
- `partner_driver.cpp`: StanceBlock/RemoteDowned and the StepRecon inputs, the round-1 block in front
  of `TickNative`, three lines in `TickNative`, Configure/MissionStart/MissionEnd/Stop/ApplyTrigger,
  `TickActLog` just before the vehicle/ride section, `TickOverlay` around the `TickDm` call. The ride
  section (~4330-4490 originally) and the weapon gate are untouched.
- `partner_sync.lua`: reply values 64..127 belong to `OVERLAY`; GANI stays 0..30.
- Track V (vehicle seat DM): the seat DM and the overlay share the DM. The overlay already refuses
  while the ride guard hides the body; when the seat path exists, add its "seated" flag to
  `OverlayBlocked` and have the seat path wait for `!partner_dm_overlay::Active(m.overlay)`.
- Camera agent: the overlay never touches carried-camera code; it refuses while the camera connect is
  non-zero (that includes the stock carry/ride connects).

## 7. Checks run

`tools/build.ps1 -Configuration Release -ReleaseGun -BuildDirectory build/native` in this worktree:
build passed, CTest 248/248 passed (friend_package_tests and lab_scripts_tests included). The
composed/game-input Lua checks (`partner_sync_composed_lua_check` and the other "SKIPPED (missing
interpreter or game input)" ones) are not registered in a worktree without the extracted game files;
they run in the main checkout. The fixtures added or extended:
`partner_recon_tests`, `partner_act_log_tests` (new), `partner_dm_overlay_tests` (new),
`partner_native_tests`, `partner_receipt_tests`, `lab_config_tests`, `partner_driver_tests`,
`partner_sync_lua_check`, `debug_menu_lua_check`, `lab_scripts_tests`, `worker_session_tests`.

## 8. Open items

- No hidden run yet: whether the graph-0 states play standalone on body 1 (T12), whether their
  root motion fights the xform, and whether `rec+0x18` holds the requested state code (`code_match`).
- The damage direction is not on the wire (Front only in the map).
- A ladder/ledge overlay is drawn at the remote's position by xform; the remote's on-ladder position is
  on the ladder, so the pose should line up, but body 1's capsule against the ladder is untested.
- Sender-side PlayerStatus (`BINOCLE`, `SUBJECT`, `CBOX`, `TRASH_BOX`, `BEHIND`) and the remote's
  in-hand equip class are still not on the wire (critic P0.6 remainder); the census logs only what the
  receiver already has.
- `partner_lua` notes (`event=dm_overlay`) share the 64-line Lua note cap.
