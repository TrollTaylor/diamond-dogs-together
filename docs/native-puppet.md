# Native puppet

Status (2026-10-09): the native partner ships in the 0.12.0 play profile.
The user reported an actual two-PC session; hidden runs separately established
locomotion and carried weapons, muzzle flash and native reload exit. The 0.12.1
repairs passed the recorded hidden checks; fresh two-PC acceptance is still
required. See [STATUS.md](STATUS.md) for exact
runs and remaining gaps. Historical sections below retain their original
fixture/live evidence boundaries. Byte, slot, ownership and lifecycle refusals
remain mandatory; only the coordinator runs the private-desktop worker.

The partner body (player instance 1) is driven by the remote player's real pad
through the game's own locomotion, action and gun systems. The proven
`dm=map` + xform partner stays the automatic fallback.

Native actions (2026-10-08, `wp/native-actions`, fixture-tested; hidden runs
pending): the dive, evade and prone rolls now reach the partner (EVADE in the
masks), the partner body runs at the remote's own movement rates (the rates
mirror) and the reconciliation no longer pins a body that is only slower. See
"Actions" and "Rates" below.

Native polish (2026-10-09, `wp/native-polish`, fixture-tested; hidden runs
pending): facing parity (the facing governor, neutral placements, the
placement yaw check, the windowed teleport and the look at its source) and the
partner's own trigger, reload and shot noise (the sample rings, the magazine
store row, the noise filter, the weapon gate). See "Facing" and "Weapons".

Aim parity (2026-10-09, `wp/aim-parity`, fixture-tested; hidden runs
pending): the partner's gun aims where the remote aims and the standing and
crouched aim no longer cycle act 12/15 (the aim-target mirror,
`partner_native_aimpt`). See "Facing".

## Mechanism

Executable: `mgsvtpp.exe` 1.0.15.4 (SHA-256 `085c2f82...bb45`). All addresses are
RVAs. `tools/generate_partner_contract.py --check` re-verifies every site, slot,
reference and containment fact below against the exe; `native/partner_player_contract.hpp`
is generated from it.

- **H2** (`native/partner_defer.cpp`): the only call of the batch function
  `0xBF4F20(group, mode)` at `0xBF4D55` (ticker `0xBF4D20`, modes 0 then 1) is
  retargeted through a near stub to `WrapBatch`. Batches in flight are counted
  (per pass and in total on the hook thread, and separately on any other
  thread), never flagged, so a nested or concurrent batch cannot clear another
  one's state. On the hook (main) thread the outermost batch drains a published
  deferred record after its join; a batch nested inside another hook-thread
  batch never drains, and a batch on another thread only counts. A batch that
  unwinds (an exception through the join) drops its record (`unwound`, also
  `torn`, the deferral faults), so a stale record is never drained in a later
  frame.
- **H1**: the player GameObject's job slot (primary vtable `0x22953D0` + 0x50,
  qword `0x2295420` -> `0x9C1B90`) holds `W50`. W50 uses atomics only. It defers
  sub 0 of block 1 (the published identity: GO = P − 0x20, pool1) only when it
  was called by the job body (return address `0xBF4CEE`) inside an active
  hook-thread pass with no nested hook-thread batch (`nested`) and no batch on
  another thread (`foreign`) in flight, claiming the per-pass record 0 -> 2,
  writing it and publishing 1. Everything else passes through and is counted.
- **Drain**: on the main thread after the join, the game's TLS scratch scope is
  pushed (`0x34470`/`0x34550`, exactly as `0xBF4F20` does around its batch), the
  entry read lock is taken as the job body takes it (spin while −1, +1), the gate
  `[[GO+8]+0x38] & 0x10` is checked, the param is rebuilt from the live entry,
  and the job runs inside the input hook's drain window
  (`partner_input_hook::RunJobWindow`). Unlock, then scope pop, also on unwind.
  A missing entry (`lost`), a closed gate, a torn record or an unbalanced scope
  faults the deferral for the mission. The drain window opens only for
  instance 1's block: the job's block must equal the Arm's `pool1` (whose live
  value Validate re-reads) and must not be instance 0's live pool; otherwise,
  or while any batch is in flight, the job runs once without a window
  (`drain_block`, `drain_in_batch`; the arbiter leaves on either), so instance
  0's job never sees the remote pad.
- **Windows** (`native/partner_input_hook.cpp`), under a native Arm only:
  - the main window (phase 8, `0x9C2B51`) composes one frame injection (with
    steering); each consumer (drain0, drain1, walk7, walk11) gets the edges once
    and the level afterwards; older than 100 ms it gets a neutral pad;
  - every native window swaps the HOLD latch `PP+0x27C` bit 0 to the remote's
    (gun eligibility `0x1054210` reads it) and restores bit 0 only;
  - no window (main, walk or drain) opens while any batch is in flight, on the
    hook thread or another thread (`wrap_in_batch`: a job run inline during the
    join, or a concurrent batch, would race block 0 on a worker; this closes a
    pre-existing round-4 `wrap=window` hazard);
  - the aim stage saves the sub-controller ranges of `*(CAM+0x360)`; if aim
    init replaces that pointer inside the window, the old object is neither
    compared nor restored (`camsub_dirty_or` bit 2);
  - svc28/30/40 never open windows (block 1's job runs only in drains);
  - after `svc28(root1, phase 0)` inside a drain the remote look angles are
    written into instance 1's own PS slot (`*(PS1+0x290)`, look angles are
    computed in phase 0 by `0xFF28D0` -> `0xFFAC00`), unless the game skipped
    its own computation (record bit 1); aim stage only (`partner_native_aim`).
- **Drain locality**: every drain window sets instance 1's isLocal bit
  (`rec+0x2AC` bit 0) for the deferred job, whatever `partner_input_local`
  says (that key now only affects the walk windows), and restores it after.
- **P1, the phase-1 local detour** (`native/partner_local_detour.cpp`): the base
  action plugin's update (`0x23B5090`+0x18 -> `0x109F0E0`, phases 0..6) runs
  phase 1 at `0x109E130`. There `0x109E17C` `cmp edx,[rax+0x218]; setne bpl`
  picks the local branch (event 4: the act handlers' move-intent sampler
  `0x10BCD90` reads PP+0x10/+0x14 into rec+0x2C0/+0x2C4, which starts
  locomotion) or the remote branch (event 5, network follow) by id == P+0x218,
  not by isLocal. Instance 1 therefore never sampled the injected pad: run
  27c64c8d had live input (held 0x200000, stick 1000) and the body stayed in
  act 1. P1 forces the local branch for exactly (P, id 1, the hook thread) while
  a drain window runs block 1's job, and nowhere else:
  - a near stub (`setne bpl` on the original flags; if the forced id, P and the
    TEB thread id all match, `bpl = 0` and `hits++`, else if only the id matched
    `refused++`; `jmp 0x109E186`; rax and rcx preserved, flags recomputed there);
  - `jmp rel32` to the stub in the int3 padding `0x109E123..0x109E12F` at
    `0x109E128` (outside every function, never a branch target);
  - the 4 bytes of `setne bpl` at `0x109E182` become `eb a4 cc cc` (jmp to the
    padding) in one locked dword exchange inside one cache line.
  The contract checker verifies the bytes, the single jump into `0x109E130`,
  the phase-1 jump-table entry, the padding and the cache line. It installs
  with `defer=on` only, once per process, never removed; the arbiter requires it
  (`not_installed`, `hook_lost`). P+0x218 itself is never written (91 compare
  sites in block 1's update and its pairing with P+0x214 / P+0x58 make that
  unsafe). Telemetry: `p1` (hits, refusals, windows per second) and the 1 Hz
  `partner_native_drain` line (injection, isLocal before, private PP masks,
  instance 1's record after its update: act, next act, sampled magnitude).
- **Arbiter** (`native/partner_native.cpp`, pure): off / fallback / entering /
  active / leaving / latched. Fallback -> entering after every precondition held
  2 s (and 5 s after a leaving). Entering applies the native effective settings
  (apply=input, input=net, dm off, wrap=window, hold own, words hpr, cam sandbox,
  recon track with the native preset, slot body, p218 off, gun/fire-limited
  mask); the existing DM release/revert and the reconciliation's
  place/verify/arm sequence carry the switch; the deferral is armed from the
  place step on, before the native Arm is published (identity -> defer -> Arm;
  `TickInput` refuses with `native_defer` until then). Run 27c64c8d showed why:
  with the Arm first, one frame ran block 1's job inline in a batch and left
  with `wrap_in_batch`; three such entries latched the mission. The cause of the
  last leaving is kept as `left_reason` through cooldown and settling. Hard reasons (fault, hook loss, thread, unrealized,
  range, stage, lost, wrap_in_batch, drain_block, terminating) leave at once;
  soft reasons after holds (input stale 1 s, link 0.5 s, health over 1 s
  windows, cost 3 × 4 ms, diverging, runaway). Leaving: defer off -> window off
  -> dm=map rearm, body epoch ++. Three fallbacks or a failed DM revert latch
  the mission. The GO identity and the census wrap roots name per-mission
  objects: they are withdrawn at MissionEnd/MissionStart (after the deferral is
  disarmed) and once `partner_drive` is no longer native.

## Actions

Pad bits (`native/include/mgscoop/partner_pad_bits.hpp`; values from the Lua
`PlayerPad` enum registration `0x9D3A78..0x9D3EE5`, each pair pinned by the
contract with its string; keyboard bit i is key binding i, `0x1206C21..0x1206CF2`):

| bit | name (aliases) | PC key | passed |
|---|---|---|---|
| 0x2 | STANCE | C (hold = prone) | always |
| 0x4 | DASH | Shift (while prone + A/D: roll) | always |
| 0x40000 | EVADE | Space: dive while running, evade, roll while prone | always (new) |
| 0x200000 | VEHICLE_DASH (keyboard binding 21) | W | always |
| 0x2000000 | keyboard binding 25 (walk) | Ctrl | always |
| 0x8 / 0x40 | HOLD / RELOAD = CARRY | RMB / R | gun stage |
| 0x10 | FIRE | LMB aimed | fire gate only |
| 0x20 | ACTION = MOVE_ACTION = JUMP = RIDE_ON/OFF = CQC_KNIFE_KILL | E | never from the remote's pad; with `partner_native_traverse=1` (default on with the gun profile, lab and play) it stays in the mask, but since 0.13.5 no replay step taps it (the icon request replaces the tap: "Traversal icon") |
| 0x200, 0x400, 0x8000, ... | CALL/INTERROGATE, SUBJECT, CQC, weapons, markers | | never |

The movement base is `kPartnerNativeBaseMask = 0x2240006`; the gun stage
mask is `0x224005E` (HOLD, RELOAD and FIRE with visual fire, "Weapons"). EVADE's press consumers (`0x1148C6C`, `0x1299821`,
`0x129B7DB`: PP vt+0x40 bit 18 -> `Q+0x18[id] = 0x1B`, act 27) first ask
`0x114DB20` / `0x12B60D0` whether the player is network-remote, which is true
only with S_IS_ONLINE (P+0x204 bit 8), so offline instance 1 takes them.
EVADE has nine pad readers in all (`EVADE_CONSUMERS` in
`tools/generate_partner_contract.py`: every PP vt+0x20/+0x30/+0x40/+0x58 read
through P+0xB0 followed by a bit-18 test; `--check` fails on a new one): the
three act-27 sites and six state-specific ones (`0x1097F74`, `0x11B56CF`,
`0x11B8347`, `0x11BA26E`, `0x11D8FDD`, `0x12213B5`) that write per-id player
state (`[PS+0x280]`/`[PS+0x298]` entries, the act record's +0x11/+0x292).
`0x11D8FDD` (EVADE press in a state checked through `[P+0x88]` vt+0x30 >= 4)
also calls a global service's vt+0x28 with the object id held at +0x29C of
its per-id record, i.e. it may message another GameObject in the receiver's
world. Which states reach these six, and whether instance 1 can enter them
with ACTION masked, is not verified (open item; the action census records
`remote_act`/`body.act`). The remote's own ACTION never reaches body 1: it
aliases ride, knife kill and pickup, which would act on the receiver's world.
RELOAD aliases CARRY (gun stage only; gating it on the remote's aim state is
an open item). Nothing presses ACTION on body 1: since 0.13.5 the traversal
replay ("Traversal", on with the gun profile) starts body 1's ladder, pipe, climb or
step-on through its action icon ("Traversal icon"), not through an overlay
tap.

Reconciliation (native preset, `partner_recon::NativeParams`): the moving
runaway compares the body's own velocity with the remote's along and across
the remote's motion and fires only on an overrun (`dv_along > thr`) or a
sideways drift (`|dv_cross| > thr`), `thr = max(1500, 20 % of |v_remote|)`
mm/s; a body that is only slower raises `partner_recon=lag` (never a pin) and
the along-track nudges go up to `clamp(15 % of |v_remote|, 300, 1500)` mm/s
(prone 150..400). After a stick release the idle runaway waits 1200 ms while
the slide decelerates (`stop_grace`). The arbiter's runaway timer counts only
the idle-remote runaway (`report.runaway_idle`). Runs a7823/63199 showed the
old rule: a sprint at 0.835 speed pinned, never exited, and the arbiter left
with `runaway`.

## Traversal

`partner_traverse` (`native/partner_traverse.*`, driven by `TickTraverse` in
`partner_driver.cpp`) replays a remote ladder (act 21), drain pipe (22), wall
climb (23) or step-on (24: low vault, onto a ledge, up out of a hang) on body
1. Key
`partner_native_traverse=0|1`: **default on with the native gun profile in
lab and play** (user decision 2026-10-22, after the step-on FRAMES of hidden
run `104e9a47`, "Traversal icon"), off without the profile; a named value
records `partner_native_traverse_given` and a named 0 opts out. The Mother
Base ladder (21) is proven since (hidden runs `8f27806d` lab, `2596f6ae`
play); pipe (22) and wall climb (23) are still unproven in a FRAME.

Act ids come from a static writer census of `Q+0x18[id]` on 1.0.15.4 (owners
named by Ghidra): 21 `LadderActionImpl`, 22 `PipeActionPluginImpl`, 23
`ClimbActionPluginImpl` (wall / cliff free-climb), 24 `StepOnActionImpl`, 25
fall and 26 land (`StepDownActionImpl`), 27 evade and 28 the cliff / edge drop
(`CliffActionImpl`), 54..56 the ledge hang (`EludeActionPluginImpl`). 25, 26,
28 and 54..56 start without ACTION and already match natively (falling and
EVADE reach body 1). There is no player door act: door states belong to the
toilet (84..89), trash box (79..83) and the 67..74 plugin. 49/50/59 are Fulton
acts.

One episode, per remote edge into 21..24 that body 1 does not show (25..28
and 54..56 are never requested: they arrive natively):

1. **Source gate.** Body 1's act must be a start the plugin's own gate
   accepts. A ladder admits only the game's own mask
   (`LadderActionImpl::CheckEnableStartLadder` `0x1411E9B70` ->
   `FUN_1411E9A70`, `0x18200001E`: acts 1..4, 25, 31, 32); a pipe
   (`FUN_141210510`) or a wall climb (`CheckActionState` `0x14114A900`)
   acts 1..20; a step-on (`FUN_141293790` and Exec's act 0x36 path) acts
   1..20, 31, 32 and the hang 54.
2. **Placement.** One reconciliation placement at the aligned remote pose
   (`PlaceReason::traverse`: the Lua Warp with a neutral pad), skipped when body
   1 is within 300 mm and 0.35 rad; no replay beyond 8 m (the reconciliation
   places normally). The placement must verify within 1200 ms.
3. **Icon request (0.13.5).** Only after the window is armed again with a
   live pad and while body 1's act is a source act of the plugin: the
   machine raises `Out::icon` (ladder, pipe, climb or step-on) as a level
   with one `icon_seq` per episode; the driver turns it into a
   process-unique request id and publishes `Arm.icon_kind` / `icon_seq` /
   `icon_until_ms` (now + 250 ms, not compared by `SameArm`). The drain
   window answers body 1's icon query once per id ("Traversal icon"). Body 1
   must enter a traversal act within 1500 ms of the request phase or the
   episode aborts (`no_entry`). The 0.13.4 ACTION overlay tap (`BumpTap`,
   up to 3 per episode) is gone.
4. **Hold.** While an episode runs and while body 1 runs any traversal act
   natively (21..24, 28, 54..56), the reconciliation's far, vertical and
   runaway placements are held (`traverse_hold`, `partner_recon.cpp`); a
   teleport still places. The hold ends 1500 ms after body 1 left the
   traversal, 3 s after the remote left every traversal act, or after 45 s.

ACTION is added to the Arm's `mask` and `sample_strip` only with
`native_active && traverse` (`BuildArm`): the remote's raw E (held, edges,
words, tap12/long12) never passes; nothing in the replay taps it any more
(the mask stays for a later in-traversal ACTION replay, see below).
`kNativeBase` and `kMovementOnlyBits` are unchanged. Carry, a seated ride, the
DM overlay and a downed remote block the replay (`block=` in the log). Every
edge logs `partner_traverse=<start|placed|request|entered|done|abort>;
reason=..; phase=idle|place|request|native; act=..; remote_act=..;
body_act=..; icon=..; seq=..; episodes=..; requests=..; entered=..;
aborted=..; block=..`.

**Status (0.13.5): on with the gun profile; step-on and ladder proven (lab and play), pipe / wall climb unproven.** The 0.13.4
tap path stayed off; the 0.13.3 build that shipped this machine on by default logged 0 `partner_traverse` lines in
play. A stock ladder, pipe,
wall-climb or step-on start goes through the HUD action icon:
`CorePluginImpl::ExecParallelStart` runs the icon builder `[[P+0xD0]]+0x18`
(`0x140AFCCE0`) only for `idx == [P+0x218]` (`0x141155B87`), and the plugin
polls read `IsCompletedActionIcon` (`0x140AFD870`, no player index) at
`0x1411EB00D` (ladder), `0x141213DBC` (pipe), `0x14114DEC8` (climb) and
`0x14129BF86` (step-on). An ACTION press on instance 1's pad has no consumer
there, so 0.13.5 replaces the tap step with an icon request for body 1 (the
plugin's own `ExecActionButtonFunction` for idx 1, "Traversal icon"). The
ACTION reads inside a running traversal (ladder `0x1411EFEEC`, `0x1411F02DD`;
elude `0x1411A7967`) do read instance 1's pad, which is why the mask and strip
wiring stays.

## Vehicle boarding

F1 (0.13.5, research `re-vehicle.md`): what the viewer's **own** ride action
does at a vehicle the partner drives, and the partner seated as a passenger in
either player's vehicle. Key `partner_ride_board=off|block|passenger`, a named
value records `partner_ride_board_given`:

- **passenger is the default with the native gun profile, in lab and play**
  (since hidden runs `495e7c3a` lab and `de949f80` play, see "Passenger
  camera"): the viewer boards a free back seat (2/3) of a light vehicle the
  partner drives; at trucks, armoured vehicles, a taken back seat or anything
  else it blocks exactly like block. Hidden run `59cfc415` (lab,
  `coop-vehicle-passenger.txt`): the back-door prompt appears on the light
  vehicle, the viewer boards seat 2 (`board_passenger; site=phase1`,
  `local_passenger; via=p120`, act 0x6E), rides with the partner-driven jeep
  (RIDE_FAST applied) and gets off with the stock GetOffBackLeft; the partner
  sees it in the back seat (`partner_ride_seat=confirmed; seat=2`), both
  directions. There the passenger's own camera looked at the jeep's interior
  (`cam1f0=0x0` until the get-off's own `0x8800000000006408`); the passenger
  camera fixes it. The truck half did not run in any of the passenger runs:
  the host could not board `veh_trc_0000` from the `goto_vehicle truck` spot
  and the guest's nearest truck was another one 817 m away (the copies' trucks
  differ), so the truck fallback (`reason=type`) is still only unit-tested. The
  light vehicle reads `type=2; seats=4`.
- **block** (named): hidden run `0348c4d8` (lab, `coop-vehicle-shared.txt`,
  both directions): the viewer pressed E beside the jeep the partner drove, got
  no ride icon (`board_block; site=prequery`), stayed on foot
  (`vehicle_not_riding`, no `st=0x400`), and the partner kept driving in the
  viewer's frames; the ride icon came back once the partner got out. Play
  rehearsal `fad26e25` (`-Mode play -WanSim bad`, rtt ~180 ms, 1-3 % loss; the
  same scenario without the key, when block was the default:
  `board_install; mode=block`): the same in both directions, no
  `board_uncertain`.

**Why.** The partner is drawn in the viewer's copy of its vehicle by the DM
seat chain (`partner_ride_seat`), never registered in the copy's `rider[]`.
So the copy's `rider[0]` is empty: RideVehicle `CanExec` (`0x1246650`) passes
its driver-seat test, the pre-query finds door 0/1 and phase 1 registers the
viewer as a **second driver** of the copy (seat 0, the player-driver flag
`rec+0x7C |= 0x4000`; on exit `TryToGetOff` makes the copy dormant).

**The hook** (`native/partner_board.cpp`, `native/include/mgscoop/partner_board.hpp`;
pure policy `partner_board_policy.hpp`). Two rel32 call sites of the stock ride
action are retargeted to wrappers (`RetargetCall`, the `partner_defer` H2
pattern; B failing reverts A), lazily from the driver tick after the code gate,
once per process; `PatchLive` is checked every tick:

| Site | Call | Stock range | 4 means |
|---|---|---|---|
| A `0x12458AC` (rel32 `0x00005AEF`) | pre-query `0x1245820` -> nearest free seat `0x124B3A0(unit, pos, first, last)` | 0..min(count-1, 1) | the pre-query fails, `CanExec` fails at `0x1246A56`: no ride icon |
| B `0x1251479` (rel32 `0xFFFF9EE2`) | phase 1 `0x1251200` -> `SearchNearestRideSeatForDriver 0x124B360(unit, pos)` | the same | the stock "no seat" exit (FinishRideSerial): the race backstop |

The driver publishes one policy word per tick (`partner_board::Pack`: vehicle,
the peer's seat, mode, lease end), from the peer's live ride (same link epoch,
within `kLeaseMs` 5000 ms of its last packet). Both wrappers decide the same:

- **stock** unless the peer holds **seat 0** of exactly this unit
  (`0x6400 | u16 unit+0x78`); a peer passenger leaves the driver's seat to the
  viewer (the copy is then driven by the viewer's own RIDE_FAST stream);
- **block**: 4 at both sites;
- **passenger**: light vehicles only (`GetVehicleType 0x55E7F0` in {1, 2},
  seat count `0xA27CC0` in 3..4): the original `0x124B3A0` over the free back
  seats 2..min(count-1, 3), never the peer's seat; trucks (3, 4), armoured
  vehicles and anything else block. Stock phase 1 passes a back seat
  unchanged to `TryToRide` (`0x1251832..0x1251840`), selects the passenger
  phase `0x1250CF0` (`0x1251894`), which writes act 0x6E, plays
  GetOn/Loop/GetOff of that seat and attaches the viewer to the copy RIDE_FAST
  moves. An exit from seat 2/3 never makes the copy dormant.

The wrappers read only u16 `unit+0x78` and call the two getters, under SEH: a
getter fault blocks, an index-read fault passes through to stock (the vehicle
is unknown, and the stock search reads the same unit first), and both count;
`rider[]` is never written by us.

**The local passenger report** (sender). `PlayerStatus.ON_VEHICLE` is set only
for the driver's act 0x6F (`UpdatePlayerInfo 0x12CE870`), so Lua reports a
passenger on foot and `MgsCoopPartnerRideSeat` returns nil. Native reads it
(`partner_board::ReadLocalPassenger`, passenger mode only): act
`(*(Q+0x18))[local] == 0x6E`, the vehicle `0x6400 | u16 unit+0x78` of
`(*(P+0x120))[local]` confirmed by the vehicle lookup (else
`(*(Q+0xE0))[local]`, which is `vars.playerVehicleGameObjectId`), the seat
s in {2, 3} with `rider[s] == local`. It replaces Lua's foot report in the
tick the ride sender sees (kind vehicle, seat, vehicle). `local_vehicle` keeps
meaning the vehicle the viewer **drives** (RIDE_TILT, the receiver's gate);
the report is `Driver::local_passenger`.

**Receiver** (`partner_driver_seat.inc`): the by-id gate lets the peer sit in a
back seat (2/3) of the vehicle the viewer drives (passenger mode; any other
seat keeps `local_vehicle`); the peer reporting the viewer's own back seat is
refused natively as `shared_seat` (by id and at `TickSeat` start; Lua's guard
needs ON_VEHICLE); passenger mode skips the 3 m far latch for the vehicle the
viewer drives or rides (the peer's stream lags the real vehicle). RIDE_FAST
needs no change: a passenger has no `R.localVehicle()`, so it never streams
and keeps applying the driver's track (`world_ride_pose_lua_check`).

**Logs** (from the driver tick; the hook only counts):
`partner_ride=board_install; mode=..; result=ok|refused; reason=..`,
`partner_ride=board_policy; vehicle=..; seat=..; mode=..; uncertain=..` (on a
change), `partner_ride=board_block; vehicle=..; site=prequery|phase1; n=..;
mode=..; reason=..; type=..; seats=..` and `partner_ride=board_passenger;
vehicle=..; seat=..; site=..; n=..; type=..; seats=..` (mission counts, at most
once a second per site), `partner_ride=board_seated` (a steered back seat seen
as the local passenger), `partner_ride=local_passenger; vehicle=..; seat=..;
state=on|off`, `partner_ride=board_refused; reason=patch_lost` and
`partner_ride=board_uncertain; reason=patch_lost|fault|two_drivers|steer_unconfirmed|seat_mismatch`
(mission latch, once; passenger then publishes block). Two caps of 200 lines
per mission: one for the periodic `board_block` / `board_passenger` counter
lines (`partner_ride=capped; kind=board_count`), one for every other board
line (`kind=board`), so a long stand at the peer's vehicle never silences the
latch and report lines.

**Pins.** `tools/generate_partner_contract.py` `BOARD_SITES` (19 blocks: both
call sites and their contexts, CanExec's pre-query test, `0x124B360`, the
nearest search's lookup / radius 100.0 / rider test, both getters and the two
service lookups, phase 1's seat pass-through and phase select, TryToRide, the
passenger act write, the `P+0x120` writer and the `Q+0xE0` read) and three
vehicle service slots (+0x168, +0x170, +0x220); `board_assertions` checks the
callers (`0x124B3A0`: site A and the `0x124B360` tail; `0x124B360`: site B;
`0x1245820`: CanExec only), the rel32 values, the cache lines and that no
branch lands inside either operand.

**Proven in `59cfc415`** (`re-vehicle.md` 9): the light vehicle's entry scan
offers the back door (the prompt appears; the "site B only" fallback is not
needed), the Wakh Sind jeep is type 2 with 4 seats, and after a passenger exit
the copy keeps following the driver's stream (`pass-after-exit-NN`). **Still
unproven:** the truck numbering (3/4) and its block (the passenger camera
is proven, see "Passenger camera"), the attach jitter against the
20 Hz RIDE_FAST placement, and what the copy's phase-6
`RequestVehicleToManualDrive` does to the next FAST placement. Research builds: `coop_passenger` and
`coop_vehicle_mirror` SHA-pin `0x1245820` and `0x1251200` whole and refuse
(`exact_code`) once the board sites are live. The startup partner code gate
and `partner_upper_probe`'s one-time gate both run at configure, before the
lazy board install, so they never see the patched rel32; nothing re-runs them.

### Passenger camera

User decision 2026-10-22: new camera code may connect the vehicle camera for a
local back-seat passenger at boarding and release it at the get-off; the camera
files listed as protected and the P+C0 setter `0x11F6520` stay untouched. No
new key: it is part of `partner_ride_board=passenger` (the default with the
native gun profile since hidden runs `495e7c3a` lab and `de949f80` play), the
only mode in which the viewer becomes a stock passenger. Research (read-only
Ghidra, raw dumps in the scratchpad `complete/re-passcam/`):

- **How the stock camera picks its view.** The local player's camera
  `CAM = *(P+0xB8)` (PlayerCamera, vtable `0x23B8250`) holds six modules at
  `CAM+0xBD0`. Every camera update (`0x1119410`, loop `0x11194B6..0x11194E5`)
  takes the first module whose `vt+0x18` (CanActivate) is true as the mode
  `CAM+0x340`; on a change the old module's `vt+0x08` runs, `CAM+0x33C` keeps
  the old mode and the new module's `vt+0x00` runs. The modules come from data
  (`CAM vt+0x50 0x1111C50`); the classes found are the cut-in camera, a camera
  for act 0x68, the subjective camera, one keyed to player status bits, the
  VehicleCamera (index 3 in the lab) and an always-true fallback (`0x23B29E0`,
  CanActivate `0x84B020 mov al,1`; index 5), which the on-foot player and a
  seated passenger use.
- **The VehicleCamera** (`tpp::gm::player::(anon)::VehicleCamera`, factory
  `0x12D0E20`, module vtable `0x23D67F0`, base `0x23D6720`). CanActivate
  `0x12D0F60` is `act = (*(Q+0x18))[P+0x218]; return act - 0x6F <= 1`, so it
  serves the driver (0x6F) and the get-off (0x70), never the passenger phase
  (0x6E). Its update (`vt+0x20 0x12D2050 -> 0x12D1E80`) sets the camera's
  ignore-collision owner `CAM+0x1F0 = 0x8800000000000000 | (0x6400 | idx)`
  with `CAM+0x200 = 1`. Every module's position solve ends in PlayerCamera
  `vt+0xA0 -> vt+0xA8` (`0x1121D30`), whose collision query ignores the owner
  in `CAM+0x1F0` when bit 63 is set (copied to `CAM+0xC20`, query flags
  `0x700 | 0x800000`; `PlayerCamera::GetIgnoreColliosionGeoOwnerId 0x1114450`
  returns `+0x1F0`). Its Deactivate (`0x12D0ED0`) and the ride's FinishState
  (`0x124C850`, `0x124CD38..0x124CD86`) clear `CAM+0x1F0 / +0x1F8 / +0x200`.
- **Why the passenger saw the interior** (run `59cfc415`). With act 0x6E
  nothing selects the VehicleCamera and the passenger phase `0x1250CF0` never
  touches the camera, so the fallback camera orbits the seated body and its
  collision ray hits the jeep (`cam1f0=0x0`). The get-off's
  `0x8800000000006408` came from the VehicleCamera once act 0x70 selected it.
- **The first fix did not work** (`wp/pass-cam` `27e5e19`, hidden run
  `4ae70de7`). Granting the VehicleCamera to act 0x6E (a swap of its two
  CanActivate slots) connected (`mode=3; cam1f0=0x8800000000006408`), but its
  passenger branch (`CAM+0x20C = 0`, with the light vehicle's driver parameter
  set 0x11 from `UpdateActionType 0x12D1D40`) left the passenger's camera where
  the guest boarded: the frames show the jeep driving away from a stationary
  camera (scratchpad `frames/passenger/grant-4ae70de7-...-DEFECT.jpg`). The
  slot swap is gone.

**The fix** (`c5f272d`; `native/partner_pass_camera.{hpp,cpp}`, pure
`partner_pass_camera_policy.hpp`). The passenger keeps the fallback camera, and
the driver tick (`TickPassCamera`, right after `TickBoard`) writes the
VehicleCamera's own encoding of the ridden vehicle to the local player's camera
(`CAM+0x200 = 1`, `CAM+0x1F0 = 0x8800000000000000 | (0x6400 | idx)`) while the
native local passenger report says seat 2/3 (act 0x6E): the fallback camera
then orbits the seated passenger outside the jeep, whose geometry its collision
query skips. `partner_pass_camera::Connect` reads the camera first (SEH;
`*(P+0xB8)` must carry the PlayerCamera vtable) and writes only when
`ConnectPlan` says so: never while the VehicleCamera is the active module (the
driver, the get-off: stock owns the field), never over another owner, nothing
when the handle is already there (`held`). The release is stock: at the get-off
act 0x70 selects the VehicleCamera, whose Deactivate (and FinishState) clear the
field; if our handle is still there 8 s after the report ended, the driver
clears it (`ReleasePlan`: only our own handle, never under the VehicleCamera).
No code or vtable is patched and no game function is called; the install
(`partner_pass_camera=installed; mode=connect; field=0x1f0; blocks=10`, once
per process after the code gate) only verifies the pinned blocks. A write
fault latches the mission (`state=refused; reason=fault; latch=mission`), as
does the board's `board_uncertain`; nothing is written while the mission
terminates.

**Logs** (driver tick, board cap): `partner_ride=pass_camera; state=install;
result=ok|refused; reason=..` once; per boarding `state=connect; vehicle=..;
seat=..; reason=handle; plan=connect|held; ms=..; cam1f0=0x88..; cam200=1;
mode=..; prev=..; vmode=..; writes=..` (the vehicle's handle in `CAM+0x1F0`),
or `state=refused; reason=other_owner|stock_module|not_connected|cam_read|
install|uncertain|..; ...` (no connect within 3 s, or not armed); after the
report ends `state=release; reason=stock|other|cleared|still_connected;
ok=1|0; ms=..`. `vmode` is the VehicleCamera's index among the six modules,
`mode` the current one (5, the fallback, while seated).

**Pins.** `PASS_CAMERA_SITES` (10 blocks: the whole CanActivate, the connect's
unit/act test, its 0x6E/0x70 branch and the store (the encoding we copy), the
update's call, the Deactivate clear, the module selection and switch,
FinishState's clear, and the collision query's `CAM+0x1F0` test at
`0x1121D8F`) and 8 slots (the VehicleCamera's Activate / Deactivate /
CanActivate / update / UpdateActionType, the base CanActivate, PlayerCamera
`vt+0x50` and `vt+0xA8`); `pass_camera_assertions`: the CanActivate is reached
only through the two slots, only the factory stores the two vtables, the
connect is called only by the update, the selection loops over six modules
through `call [rax+0x18]`, `0x1114450` returns `CAM+0x1F0`, and `vt+0xA0`
tail-jumps to `vt+0xA8`. Contract 743 code sites / 193 vtable slots.

**Proven.** Hidden run `495e7c3a` (lab, `coop-vehicle-passenger.txt`, build
`c5f272d`), both directions: `state=connect; reason=handle; plan=connect;
ms=0; cam1f0=0x8800000000006408; cam200=1; mode=5; vmode=3; writes=1` at the
boarding, `state=release; reason=stock; ok=1; ms=3059` (reverse `3083`) after
the stock get-off. FRAMES: the passenger's own camera behind its seated body,
outside the jeep, while the partner drives (`pass-guest-view`, `pass-drive-NN`,
reverse `pass-rev-seated`, `pass-rev-drive-NN`: the street changes behind the
jeep), the stock get-off, then the on-foot camera (`pass-exit-NN`,
`pass-exited`, `pass-rev-exited`); the drivers' cameras are the stock chase
camera with the partner body seated in the back (`pass-drive-host-NN`,
`pass-rev-drive-driver-NN`) and the name tag sits at the seated head. Neither
copy logged `viewer_changed`, `uncertain=1`, `board_uncertain` or
`crash_probe=av`; the viewers' `st=0x400` appears only while they drove
themselves. Play rehearsal `de949f80` (`-Mode play -WanSim bad`, rtt ~180 ms, 0-4 %
loss; a scratch copy of the scenario without the key, so the new default ran:
`board_install; mode=passenger` on both copies): the same connect / release
lines (release `ms=3087` / `3067`) and the same frames in both directions,
with the play HUD.

**Unproven.** Aiming or the first-person view from the back seat (other
modules take over; the handle stays set, so their collision skips the jeep
too), a passenger in a truck (trucks block), and the attach jitter against the
20 Hz RIDE_FAST placement. The guest's head showed the blood decal after the
ride in both lab runs (also in `59cfc415`, godmode on, source not found).

## Traversal icon

F3 (0.13.5, `native/partner_icon.*`, `tests/partner_icon_tests.cpp`; research
note `re-traverse-gesture.md` sections 1-4). Same key as "Traversal":
`partner_native_traverse`, **default on with the native gun profile in lab
and play** (user decision 2026-10-22; the play force-off is gone), off without
the profile, a named 0 opts out; no new key. **Step-on (24) is proven in
FRAMES** (hidden runs `104e9a47` lab and `04d3511f` play, below) **and so is
the ladder (21)** at Mother Base (`8f27806d` lab, `2596f6ae` play, both
directions; `partner-traverse-ladder-mb.txt`); wall climb (23) and pipe (22)
are unproven because no spot was found ("Hidden runs" below).

**Why body 1 never starts a traversal.** The icon controller "padop"
(`Player2PadOperatorControllerImpl`, vtable `0x1422E8F00`, 96 slots, one object
per P at `P+0xD0`, allocated by `0x140AFC770` -> ctor `0x140AFAE90`) is shared
by both bodies. Its builder (vt+0x18 `0x140AFCCE0`) runs only for
`idx == P+0x218` (`0x141155B87`) and fills the key-type slots
`padop+0x980+kt*0xE0` with block 0's plugin pointers (`[block+0x78]+0x10`, 0x45
`ActionPlugin*`). Action plugins are per block (`ActionPlugin+8` = its block),
so body 1's polls (ladder `0x1411EB00D`, pipe `0x141213DBC`, climb
`0x14114DEC8`, step-on `0x14129BF86`) pass block 1's own plugin objects to
`IsCompletedActionIcon` (vt+0x28 `0x140AFD870`), whose first compare
`[padop+0x980+kt*0xE0] == plugin` (`0x140AFD88D`) never holds for them. The
same static evidence says body 1 never reads the viewer's icon either.

**Mechanism.** No code byte and no image vtable slot is written (the carry
contract pins `0xAFD870` and the slot {`0x22E8F00`, +0x28}).

1. `partner_icon::Build` (once per process, lazily from `PartnerInstallDefer`
   with defer=on and the key on, after the code gate): the live image slots
   +0x28 / +0x50 must be the originals and the complete bodies of `0xAFD870`
   (0x1AE), `0xB02480` (0x135) and the traversal priority `0x12128C0` must
   match their SHA-256 pins (`icon_bodies` in `partner_player_contract.hpp`).
   It copies vt[-1] and the 96 slots and replaces +0x28 with
   `IconCompletedBody1` and +0x50 with `IconForceBody1`. Logs
   `partner_icon=built; ...` or `partner_icon=refused; reason=slots|pins|..`.
2. **Swap.** `ApplyPre` (drain windows only, `WindowOpts::icon`) installs the
   copy into `[P+0xD0]` when `SwapWanted` holds (native Arm, `icon_kind`
   1..4, `icon_seq != 0`, `NowMs() < icon_until_ms`) and the padop identity
   is exact (vtable `image+0x22E8F00`, `padop+0x10 == P`, as
   `carry_plugin_read.hpp` checks); `ApplyPost` (every exit path, SEH and
   `__finally` included) restores the image vtable first, under its own SEH
   (a fault in a later restore never leaves the copy installed), and only
   over the copy (`RestoreAction`): a vptr already back is left, a foreign
   vptr is never overwritten (`restore_mismatch`, latches the mission).
3. **Scope.** `partner_icon::Begin(P, blk1, kind, seq)` right before block
   1's job, `End` in `GuardedCall`'s `__finally` and the outer `__finally`.
   Outside the scope (another thread, outside the job) every call through the
   copy goes to the original.
4. **`IconCompletedBody1`.** A query is body 1's when the scope is active on
   this thread and `*(plugin+8) == blk1`; anything else goes to the original.
   Body 1's query is answered `true` only when (in this order, `GrantCheck`)
   kt == 0, a request is pending, the mission latch is clear, this request
   id was not granted yet, the plugin's vt[1] is the requested kind's Exec
   (`0x11EA140` ladder, `0x1211820` pipe, `0x114BEC0` climb, `0x1293F60`
   step-on), `padop+0x10 == P`, `P+0x204` bit 10 (the stock gate at
   `0x140AFD90C`) and `res` is 16-byte aligned; then priority
   `vt[0](plugin, 1, 0)` (`0x12128C0`: 6 for kt 0) and
   `Exec(plugin, 1, 0, res)` under SEH, with the shared ray-cache entry
   `padop+0x7F0..+0x840` (vt+0x188 type 0x19, used by the climb and pipe
   Exec) saved and restored. Exec fills body 1's own work and geo path; the
   stock poll then sets its work state and the plugin's Wake writes act
   21..24 in that frame. Every other body-1 query returns `false` (what stock
   returns for body 1).
5. **Viewer guard.** Around Exec the module digests the viewer's state Exec
   must never touch: the six key-type slots (`padop+0x980..+0xEC0`), the six
   icon input states (`padop+0x1240..+0x1330`) and the viewer's act words
   `Q+0x18[local]` / `Q+0x20[local]`. Any change, or an SEH fault, latches the
   mission-scoped `uncertain` flag: no further grants until MissionStart
   (`partner_input_hook::ResetFault` -> `partner_icon::ResetMission`).
6. **`IconForceBody1`** (+0x50): body 1's step-on poll calls it right after a
   completion (`0x14129C129`); stock would set the viewer's input-state bit 7
   (`padop+0x1260`) and copy the viewer's slot result into a local the poll
   discards. In scope it is swallowed; outside it goes to the original.

**Contract.** `tools/generate_partner_contract.py` pins the four Exec
prologues, the priority body, the two forwarded prologues, the builder gate
(`0x1155B79`, `0x1155BC3`), the four poll call sites and the step-on force
call (`ICON_SITES`), the padop slots +0x18/+0x28/+0x50/+0x188 and vt[0]/vt[1]
of every traversal `ActionPlugin` vtable (`0x23C6000`, `0x23C60C0`,
`0x23C9BE0`, `0x23C9CB0`, `0x23BB850`, `0x23BB930`, `0x23D13E0`,
`0x23D1490`), and asserts that only the ctor/dtor store the padop vtable and
that `0xAFD870` / `0xB02480` / the Exec functions are reached only through
those slots.

**Census (read-only).** Once per mission with the key on and the block-1
targets read: block 1's `PS+0x10` plugin list against block 0's, and
`*(p+8) == its block` for the four traversal plugins (found by vt[1]):
`partner_icon=census; distinct=<differ>/<both non-null>; blk_ok=0x<b0>/0x<b1>;
found=0x<b0>/0x<b1>; null=<n0>/<n1>; read=0|1; verdict=ok|unread|empty|shared|
missing0|missing1|blk0|blk1` (bits are 1 << kind: 0x1e = all four).

**Logs** (from the driver tick; the hook only counts):
- `partner_traverse=icon; plugin=ladder|pipe|climb|step_on; res=ok|none;
  seq=..; exec_ok=..; exec_fail=..; granted=..; queries=..; denied=..;
  swaps=..; swap_refused=..; windows=..; passed=..; force_swallowed=..;
  req_queries=..; req_denied=..; deny=<Deny>; latched=0|1;
  viewer_changed=..; faults=..; restore_mismatch=..` once per request. Every
  count is the delta since the request started; `latched` is the mission
  latch at the time of the line. `queries` / `denied` count every body-1
  query in scope (body 1's other traversal polls and its kt > 0 plugins
  query in the same job); `req_queries` / `req_denied` count only the
  requested plugin's own queries (its vt[1] is the requested Exec), and
  `deny` is that plugin's last refusal (`none` when it had none in this
  request). `windows` counts scoped jobs (`Begin`), `passed` calls through
  the copy that went to the original (another thread, another block, or
  outside the scope). `res=ok` as soon as the grant is counted, `res=none`
  when the machine left the request phase without one: entered natively or
  aborted; a tick inside the phase without the level, such as a disarmed
  window, neither ends the request nor restarts its deltas.
- `partner_traverse=icon_act; seq=..; act=..; ms=..` for every body-1 act
  change in the 2 s after a grant, then one line with `final=1`.
- The existing `partner_act` edges (`partner_act_log=1`): expect
  `edge=body; remote=X; body=X; match_ms<1500` after `res=ok`.

Reading a failed run: `queries=0` means body 1's polls never ran inside a
swapped drain window: no window swapped (`swaps=0` / `swap_refused>0`), the
swapped windows never scoped block 1's job (`windows=0`), or the polls went
through the copy outside the scope (`passed>0`: another thread or block).
`queries>0, req_queries=0` means body 1's other polls ran but the requested
plugin's poll never reached the icon (its own state gate refused first:
act, upper act `Q+0x20[1]`). `exec_fail>0, exec_ok=0` (`deny=exec`) means
the plugin's own gates refused body 1 at that pose (placement, facing, act,
upper act); `deny=p_flag` means `P+0x204` bit 10 was clear;
`deny=align|owner|fault`, `restore_mismatch>0` or `latched=1`: report.
`res=ok` without `edge=body` means the poll or the Wake did not follow the
completion.

**Risks** (for the hidden run).
- The ladder Exec reads `P+0xB0` pad +0x10/+0x14/+0x2C (instance 1's injected
  pad inside the drain) and body 1's facing; the climb and pipe Exec cast a
  ray whose default path passes `P+0x214` (the local index), so body 1's ray
  may exclude the viewer instead of itself (effect unknown).
- `P+0x204` bit 10's meaning and writer are unknown; it is set whenever the
  viewer's own icons work.
- Readers that check the padop vtable on other threads during a swapped
  window (carry, sleep, horse readers) see the copy and refuse for that read;
  the carry readers already avoid windows through `WindowSeq`.
- With `P+0x218 = 1` inside a window (`p218`, pose-owned windows) the
  builder would run for body 1 and fill the shared slots with block 1's
  plugins; while a request is pending the copy still answers only the
  requested plugin (no viewer E reaches body 1).
- Body 1 reloading (upper act 5) or picking up (0x15) cannot start a
  traversal, as in stock.

**Hidden runs** (research build, gun stage, lab seed, key named; STATUS.md
0.13.5 F3). The census reads `distinct=52/52; blk_ok=0x1e/0x1e;
found=0x1e/0x1e; verdict=ok` on both copies. Step-on spot: the rock shelf east
of the lab start, the host at 828.18,337.20,1630.51 facing yaw 95, W then W+E
(`partner-traverse-icon.txt` Part 3). In `104e9a47` both attempts logged
`icon; plugin=step_on; res=ok; exec_ok=1; granted=1; req_queries=1;
latched=0; viewer_changed=0; faults=0; restore_mismatch=0` and `edge=remote;
remote=24; body=24; match_ms=0`, with no vertical placement and the viewer's
act at 1 throughout; the frames show body 1 reaching up, pulling itself onto
the shelf and standing on it. In `d10ddf39` the same spot needed a placement
first (`start; reason=placed`, body edge `match_ms=43`). Spot search for the
rest (lab start, Wialo Village, Mission 1's horse start, the slopedTown
outpost, Wakh Sind Barracks) found no 21/22/23: the climb Exec only accepts an
authored crack-climb geo path whose end is within 2.5 m and +-1.5 m of the
player (`0x14114AA50`), and the lab seed's D-Dog takes the E press whenever it
is the nearest action (act 94, petting).

**Default (user decision 2026-10-22).** The key is on with the native gun
profile in lab and play on the strength of the step-on proof and the clean
viewer guard (`latched=0; viewer_changed=0; faults=0; restore_mismatch=0` in
every request).

**Runs after the flip** (key not named, so the default ran; `b9b391f`;
STATUS.md 0.13.5 F3). `04d3511f` is the step-on play rehearsal
(`-Mode play -WanSim bad`, rtt ~185 ms): `partner_icon=built` on both copies,
both host step-ons and one reversed step-on `res=ok` with edge 24/24, and the
same frames as the lab run with the play HUD. Ladder: Mother Base (real seed),
the ladder on the red container with its rungs at x=22.61, z=-14.86 (deck
y=0.80, the climber faces +z), from the user's 0.13.2 MB play. The host
teleports to 22.61,1.80,-15.55 yaw 0, W 300 ms, W+E 200 ms, then holds W; E
alone from 22.61,-15.40 also grabs it, an approach from the west (yaw 95)
walks past. `8f27806d` (lab) and `2596f6ae` (play, both directions) logged
`icon; plugin=ladder; res=ok; exec_ok=1; granted=1; req_queries=1;
deny=none; latched=0; viewer_changed=0; faults=0; restore_mismatch=0` and
`edge=remote; remote=21; body=21; match_ms=0` for every grab (body 1 often
enters 21 a frame before the act census sees the remote's 21), with no
vertical placement; the frames show body 1 grab the ladder, climb it and go
over the top onto the container roof while the viewer (act 1) stands still.

**Open (cosmetic): the viewer's HUD shows the ladder hint.** While body 1 is on
the ladder, the viewer's HUD shows the stock "C + ladder" hint beside its own
player (frames `8f27806d` `ladA-up-01..04`; `2596f6ae` `lad-up-01..top-02` and
the host's `rev-up-01..top-02`), although the viewer is on foot and the
climber's own view mid-ladder shows no hint. Likely source (read-only Ghidra):
body 1's LadderActionImpl state handler `0x1411EC3A0` case 4 calls `P+0xD8`
(`UiControllerImpl`, vtable `0x1423AF358`) vt+0x260 (`0x140FE6D10` ->
`[this+0x48]` vt+0xF8 with 3, 8, 0, 1, 0, 0) with no `P+0x218` check, while its
case 2 checks `P+0x218` before `P` vt+0x90. There is no input path: body 1's
slide poll (`0x1411EC54A`) asks IsCompletedActionIcon with block 1's plugin,
which never matches the viewer's icon slots. Suppressing it would need a new
scoped filter of that UI call for body 1 (not attempted).

**Wall climb (23) and pipe (22)** stay unproven: no spot is known. The game's
own Lua registers its crack points with the UI as a Vector3 list through
`TppUiCommand.RegisterCrackPoints` (`0x1408D5010`, up to 64 points); none of
the library scripts read with `tools/archive_probe` calls it, so the caller is
a location or mission script, and logging that call's argument would list the
crack-climb spots.

## Gestures

`partner_gesture` (`native/partner_gesture.*`, pure part
`native/partner_gesture_policy.*`, driven by `SendGesture` / `ReceiveGesture` /
`TickGesture` in `partner_driver.cpp`) plays the remote's pickup, hand sign Go
/ Stop and knock on body 1's upper graph. Key
`partner_native_gesture=0|1`: **default on with the native gun profile in lab
and play** (the pickup on body 1 in FRAMES: hidden runs `0e343f72` lab and
`75a7e7dc` play with bad WAN, see "Status" below), off without it, a named 0
opts out, a named value records `partner_native_gesture_given`. The same key
gates the sender (`tools/worker/scenarios/partner-upper-gestures.txt`).

**Why a new signal.** These are upper-only actions: the lower act `Q+0x18`
does not change, so neither the snapshot's anim extension (act, stance, speed)
nor the Lua tick says anything. The stock plugins write the upper act byte
`Q+0x20[id]`: 0x15 pickup (`PickUpActionPluginImpl` Wake `0x14120FD9A`,
cleared in its Sleep `0x14120EB37`), 0x17 knock (`StateKnock` `0x1411E7210`),
0x18 / 0x19 hand sign Go / Stop (`StateHandSignGo` `0x1411E6A40`,
`StateHandSignStop` `0x1411E6C30`). The byte persists from the state's enter to
its Sleep, so the driver tick samples it.

**Sender.** Every driver tick with the key on, `partner_player::ReadUpper` reads
both ids' byte; a rising edge of this copy's own byte (`P+0x214`, index 0) into
one of the four values sends one 12-byte message on reliable channel 2, append
mode (see "Wire"), with the sender's own stance `Q+0x30` and speed `Q+0x38`
(moving = speed != 0). The first sample after enabling, after an unread tick or
in a new link session is a baseline (a gesture already running is not sent). A
refused send stays pending for up to 500 ms; a newer edge supersedes it.

**Receiver.** `DrainEquip` hands a `'G'` message to `ReceiveGesture` (a
strictly newer seq of the session is stored; with the key off it is only
counted). `TickGesture` gates the newest edge, picks the stock node and
publishes a request to the body-1 hook:

| Gate (`partner_gesture_policy::Gate`, in order) | refusal |
|---|---|
| key on | `key_off` |
| the mission's `uncertain` latch clear | `uncertain` |
| native drive active, body 1 realized with its parts loaded | `not_native`, `unrealized` |
| no carry, seated ride, DM overlay, downed remote or vehicle job | `carry`, `seat`, `overlay`, `downed`, `vehicle` |
| the remote's pose sample live, the hook installed, no gesture running | `remote_stale`, `not_installed`, `busy` |
| body 1 in plain locomotion: act 1..6 (not 0x4D box), `Q+0x28[1] == 0`, `Q+0x20[1] == 0` | `box`, `body_act`, `body_q28`, `body_upper` |
| the edge is younger than 500 ms | `stale` |
| (after publication) a body-1 update takes the request within 500 ms | `unanswered` |

Nodes (the remote's stance from the message; the low-item / flag-0 rows of the
stock tables; `NodeFor`, every value an imm64 operand inside the stock
function, asserted by the contract generator):

| Gesture | Method (cs vtable) | Blend | Stand (1) | Squat (2) | Crawl (3) | Other |
|---|---|---|---|---|---|---|
| pickup | SetCurrentNode vt+0x170 | 0.5 | idle `0x1E904603EB7D`, moving `0x82E84B26E4CE` | `0xFF77B695370A` | refused (`stance`) | refused |
| hand sign Go | ReserveToResetNode vt+0x160 | 1.0 | `0xE74E11BABA4B` | `0x8881431FFF42` | `0x5FE632112A9A` | `0x2153458C564C` |
| hand sign Stop | ReserveToResetNode vt+0x160 | 1.0 | `0xB98C80A7DDAE` | `0x78CB18630AF1` | `0x825F953B5437` | `0x724E8FE0F814` |
| knock | ReserveToResetNode vt+0x160 | 1.0 | `0xD64283446A67` | `0x7BC06465D606` | `0xC1143CAC83B1` | `0x174A0F341288` |

The knock takes the second node of each stock pair (`0x7B2D4F836A92`,
`0xD08C8B213CAB`, `0x0430147D9D34`, `0x1EC34B25A344`) when body 1's hand look
`Q+0x58[1]` is non-zero. The item height and the per-id Left/Right flag
(`PS+0x2F0`) are not on the wire.

**The body-1 hook.** A MinHook on the per-entry motion update `0x140B6C410`,
installed lazily (the first native tick with the key on, after the startup
partner code gate) and never removed. Everything but `ActMotionStart`'s call
(return address `0x140AF0381`) for body 1's entry passes straight through (one
atomic compare). For body 1 it re-resolves the identity (P, Q, block 1 with id
1 only, PS, CS with base 1 and count 1, entry, graph and request-buffer
layout, and a different CS / entry / buffer for body 0), then, right before
the original update consumes it:

1. `IsGraphExistNode(cs, 1, 2, node, {0, 0})` (vt+0x210 `0x140AF1F10`; the tag
   list is walked until a zero entry, so it is never null);
2. pickup `SetCurrentNode(cs, 1, 2, node, 0.5f, 0)` (vt+0x170 `0x140AF2BA0`),
   signs and the knock `ReserveToResetNode(cs, 1, 2, node, 1.0f, 0)` (vt+0x160
   `0x140AF2710`): both write only body 1's request buffer (`SetRequest`
   `0x140B6E9F0` / `ReserveToReset` `0x140B6E6E0`);
3. after the original update, graph 2's current / queued node is read back
   (`applied; ok=`).

Neither write method looks at a request already waiting: both set the mask
bit and overwrite the graph's record, and the update clears the masks only
after consuming them (`0x140B6C5E2`). Stock action-plugin state code (the
pickup's `0x14120E520` through vt+0x170, `StateHandSignGo`'s call at
`0x1411E6B98` through vt+0x160) submits graph-2 requests from the plugin
update, not from the motion update, so a graph-2 mask bit in body 1's request
buffer at the update's entry is another owner's request (ours is always
consumed by the update it was made in). The hook then never writes: a new
request is refused (`pending`, checked right before the write) and a running
gesture ends (below).

Every later body-1 update tests the end (`partner_gesture_policy::End`):
another body or controller ends it without a write (`identity`); so do the
window closing (`shutdown`) and another owner's waiting graph-2 request
(`pending`, re-checked right before the stateNull write too: a late one ends
the gesture with `null=0; note=pending`); a lost lease
(the driver's command older than 250 ms, disabled, or a seat / carry / overlay
taking body 1) ends it with the stateNull return if graph 2 still shows our node;
another owner of graph 2 (after a 300 ms grace) ends it without a write
(`node`); the clip end after 150 ms (pickup `vt+0x228(cs, 1, 2)` `0x140AF1F70`,
signs and knock `vt+0x2B8(cs, 1, 3)` `0x140AF2110`), the remote's speed > 1 for
signs and the knock, or the 2000 ms deadline end it with
`SetCurrentNode(cs, 1, 2, stateNull 0xC1A2050D3802, 0.0f, 0)` (the validated
`partner_upper_manual_idle` return), only while graph 2 still shows our node. A
new mission epoch (MissionStart / MissionEnd / STOP) drops a running gesture
without any write; the withdrawal's step drains the hook's receipts, so it logs
them (and `receipts_dropped`) for the ending mission like a tick does. When the
game window closes (`PartnerShutdown`, any thread, next to the deferral and
input-hook shutdowns) `partner_gesture::Shutdown` makes every later update pass
straight through: no game call and no write, a running gesture included.

Guards: the nine bodies are SHA-256 pinned (`partner_gesture.hpp` `kPins`,
re-checked before the hook is created; the contract generator compares them
with the executable); the five cs vtable slots are checked before every call;
every call runs under SEH (a fault latches `fault` and the hook passes through
for the rest of the process). Around every write the viewer's own body 0
(request masks and graph-2 request record) is digested before and after, and
body 1 may change only its graph-2 mask bit and must keep its graph-0 record:
any surprise latches `uncertain` for the mission (no further writes, not even
the stateNull return). A diagnostic build whose phase observer already hooks
`0xB6C410` refuses the install (`phase_observer`).

Logs (`partner_gesture=`, 400 lines per mission; uncertain / fault / install /
configured / patch_lost uncapped): `configured; key=1; sender=..; receiver=..`,
`install; result=ok|<reason>` (the hook's own `installed; hook=0xb6c410;
caller=0xaf0381; pins=9; slots=5` or `install_refused; reason=..; rva=..`),
`local; kind=..; upper=0x..; seq=..; stance=..; speed=..; moving=..; sent=0|1`,
`rx; kind=..; seq=..; ...`, `refused; reason=..; where=driver|hook; ...`,
`submit; graph=2; node=0x..; exist=1; kind=..; method=set|reserve; seq=..;
stance=..; moving=..; body_act=..`, `applied; ...; current=0x..; queued=0x..;
ok=0|1`, `ended; via=end|timeout|override; cause=..; seq=..; kind=..; ms=..;
null=0|1; current=0x..`, `uncertain; reason=..`, `fault; reason=..`. The hook
answers every request it sees (submit or refused); a request no body-1 update
took within 500 ms is withdrawn with `refused; reason=unanswered; where=driver;
...; body_calls=<body-1 updates so far>; resolve=ok|<identity refusal>` (the
update filter never had body 1's entry, or the caller never matched).

**Status (0.13.5): the pickup is proven, on with the gun profile in lab and
play.** The stock pickup key is R, not E: `PickUpActionPluginImpl` polls the
padop icon with key type 1 (`FUN_1412086f0` -> padop vt+0x28(plugin, 1, res)),
and key type 1 is pad bit 0x40 (RELOAD, table `0x22E86A8`); over a dropped
weapon a short R is a reload and only the long press picks it up (the swap
panel shows the R glyph). Hidden runs (research build, gun stage, lab seed;
Closed, BaselineRestored, RealSavesUnchanged, no visible fallback; the host
picks up, the prone guest films body 1 from 4 m):
- `ab16d947` / `0e343f72` lab (key named): host `local; kind=pickup;
  upper=0x15; seq=1; stance=1` and `seq=2; stance=2`; guest `rx` -> `submit;
  graph=2; node=0x1e904603eb7d; exist=1; method=set; body_act=1` -> `applied;
  current=0x1e904603eb7d; queued=0x0; ok=1` -> `ended; via=end; cause=clip;
  ms=625..656; null=1`, and the same for the squat node `0xff77b695370a`
  (`body_act=3`). FRAMES: body 1 bends down at the waist reaching for the
  ground and comes back up while its legs stay planted, then stands as before;
  the viewer's own player stays prone (act 5) in every capture. `IsGraphExistNode`
  accepts the `{0, 0}` tag list and the vt+0x228 end fires after ~0.6 s.
- `75a7e7dc` play rehearsal (`-Mode play -WanSim bad`, rtt ~180 ms, 0-3 %
  loss), the same scenario without the key: `configured; key=1` by default on
  both, the same lines (`ms=594` / `672`) and the same frames.
- No `refused`, `uncertain`, `fault` or AV in any run; no viewer
  `partner_status=local` change.

Still unproven: the squat clip in a FRAME (grass hid body 1's arms), the
moving pickup (a held W+R over the dropped back weapon never raised 0x15 on
the host), the hand signs and the knock (no trigger in the 0.13.5 runs: E at
the lab start pets D-Dog, act 94; see the lab lever below), the reverse
direction (the guest picking up, the host filming), and whether body 1's own
upper plugins ever take graph 2 back (`override; cause=node|pending`, never
seen). The rate-record MotionVars writes of `re-traverse-gesture.md` 5.3
"Optional" were not needed.

### Lab lever: knock and hand signs

0.13.6 (`native/partner_gesture_lever.*`, pure part
`native/partner_gesture_lever_policy.cpp`, `tests/partner_gesture_lever_tests.cpp`
and `tests/partner_gesture_lever_fire_tests.cpp`). Lab key
`partner_gesture_lever=0|1`: **default 0 in lab and play**, play forces a
named 1 off (no refusal bit, like the WS4 v2 mirror), a named value records
`partner_gesture_lever_given`. It exists only to film the receiver's knock and
hand-sign nodes; the receiver itself is unchanged (`partner_native_gesture`).

**How the local player triggers them (read-only research, mgsvtpp.exe
1.0.15.4).** Neither is an action-icon press, so no key of the padop key-type
table `0x22E86A8` (key type 0..9 -> pad bit 5, 6, 15, 3, 1, 11, 10, 2, 18, 7:
ACTION, RELOAD, CQC, HOLD, STANCE, UP, SUBJECT, DASH, EVADE, STOCK; the
names are the `PlayerPad` Lua constants the PlayerPad binder `0x1409D3630`
registers (its STANCE registration is the pinned site `0x1409D3A90`),
bit 11 = 0x800 = `UP`)
starts them. `KnockActionPluginImpl`'s only icon query is its
`ExecActionButtonFunction` `0x1411E44B0` (key type 0 = ACTION, result type
0x70), and that is the high-grass crawl (state 8 `StateHighComoCrawl`: crawl,
idle, act 1..20, `IsHighComoCrawlPhase`), not the knock. The knock and the
signs are **requests** written into the plugin's per-id work row by three
methods of its primary vtable `0x1423C5A20` (the block's `*(blk+0xF8)`,
stored only by the constructor `0x1411E3750`; `*(plugin+8) == blk`):

| Slot | Function | Row write (`row = *(plugin+0x68) + (id - *(*(plugin+0x38)+0x24))*0xE0`) |
|---|---|---|
| vt+0x28 | `RequestKnock` `0x1411E5100` | `+0xD0` bit 0 set, `+0x24 = 1`, `+0x18` = the frame (`QST+0x40` -> `+0x38`), `+0x20 = 2` |
| vt+0x30 | `RequestHandSignGo` `0x1411E5060` | the same with `+0x24 = 2` |
| vt+0x38 | `RequestHandSignStop` `0x1411E50B0` | the same with `+0x24 = 3` |

The plugin's polling (vt+0x58 `0x1411E5160`) takes a request through
`FUN_1411E3A20` while the frame is at most `+0x18 + +0x20` (two frames):
kind 1 (knock) always; kinds 2 / 3 (signs) only while the speed `Q+0x38 <= 1`,
the act is not 0x79 / 0x7A and PS rec+0xC bit 28 is clear. It then starts
state 1 `StateKnock` `0x1411E7210` (`Q+0x20 = 0x17`), 2 `StateHandSignGo`
(0x18) or 3 `StateHandSignStop` (0x19), the signs only when `GetStance`
`0x141002350` is not 4. `GetStance` returns the raw `Q+0x30` byte (1 stand,
2 squat, 3 crawl; never 4), but 4 when it is 3 and PS rec+0xC bit 20
(0x00100000) is set.
No wall or cover is involved: the knock plays wherever it is requested.

The stock callers (a scan of `.text` for an `[reg+0xF8]` load feeding a
vt+0x28 / +0x30 / +0x38 call within ten instructions finds these five in the
player code; a caller that keeps the plugin pointer elsewhere would not show,
and the request functions have no direct caller):
- **Knock: the call menu.** Holding CALL (Q, pad bit 0x200) for 12 frames
  (`CallMenuManagerImpl::UpdatePadState` `0x14110FEF0`: PP vt+0x90(9, 12) == 3;
  a tap, 2, is the buddy call / whistle `CallBuzzer`) shows the call menu.
  Command set 0 is the knock (`UpdateCommandSetKnock` `0x14110F8D0`, enabled
  while (act 1..20 or 31..37, or PS rec+8 bit 17) and `Q+0x20 == 0`,
  `Q+0x28 == 0` and the speed not 2..3), whose callback `0x14110CCE0` calls
  vt+0x28 with the menu owner's id. The lever's precondition leaves out the
  rec+8 bit 17 alternative (stricter than stock; the log prints `rec8=`).
  The cursor and the decision live in the UI (`tpp::ui::hud::impl::CallMenuImpl`,
  `CallMenuManagerImpl::UpdateSelectState` `0x141110060`: select state 2 is set
  0, 4..7 the buddy set's slots, 8..11 the interrogation set). Partly mapped
  (read-only, unproven): `CallMenuImpl::UpdateSelectKeyboard` `0x14179CE60`
  selects the enabled record numbered 1..8 (the UI numbers the enabled records
  in order) through eight hashed key inputs (`0x91F6665C2DED`,
  `0x250FB37769C5`, ...; not resolved to keyboard keys), steps with the mouse
  wheel and picks by the mouse direction; `UpdateSelectCursor` `0x14179C430`
  picks by a stick direction. `CallMenuManagerImpl::Update` (the caller of
  `UpdateSelectState`) executes the selection through `HideCallMenu(1)` only
  when the manager's pad state has bit 0x10 (PP vt+0x40 bit 24, or the hashed
  input `0x7F4D42602D29`; `UpdatePadState`) while the UI reports state 1;
  releasing CALL without it hides the menu with 0 (no command). Bit 24 is not
  a `PlayerPad` constant (they end at 0x400000) and the decide key was not
  resolved, so the scenario films the open menu and tries `1` then `E` as a
  census only.
- **Hand signs: buddy commands and the child-soldier orders.**
  `ExecCommandSendCommandToBuddy` `0x14110CF10`: buddy command 4 (Quiet "Cover
  me") or 0x13 (D-Horse "Do it") -> vt+0x30 Go, 5 (Quiet "Cease cover") ->
  vt+0x38 Stop; the child-soldier order menu (`UpdateCommandSetRescueBoy`):
  `0x14110CD30` Go, `0x14110CE20` Stop. **No D-Dog command (8..0x10) signs**, so
  the lab seed's D-Dog cannot trigger one; the 0.13.5 scenario's "Q then E" was
  a CALL tap (the whistle) and then E (petting D-Dog, act 94).

**The lever.** The native debug command `gesture_lever knock|sign_go|sign_stop`
(the F9 console or a worker `run` step; it never reaches Lua) calls this copy's
own request slot with id 0, the very call the menu's command makes, on the
runtime thread. Admission, in order: the key (`key_off`), `mode=lab` (`mode`),
lab active, copied saves (`saves_not_redirected`), the offline branch, the
partner and its startup code gate (`code_gate`: the contract pins below),
gameplay, not shutting down, not latched. Then the identity (offline, two
instances, this copy at id 0, both blocks, both plugins with their back
pointers and distinct work rows, this copy's PS), the slot and the polling slot
(`slot`), this copy's arrays and the stock precondition: knock = the call
menu's enable test (`act`, `upper`, `q28`, `speed`), signs = speed <= 1, act
not 0x79/0x7A, PS rec+0xC bit 28 clear, `GetStance` not 4 (not a crawl with
rec+0xC bit 20 set) and upper 0 (`speed`, `act`, `rec_c28`, `stance`, `upper`).
The call runs under SEH (`fault` latches the lever for the process). Instance
1's request fields (row +0x18 / +0x20 / +0x24 / +0xD0 bit 0) and its upper
byte are compared before and after: any change latches (`other_changed`).
Body 1 and the peer are never touched; the request then runs the stock state
on this copy's own player, and the existing sender reports its edge.

Result (the `run` value, at most 160 characters):
`gesture_lever=requested;kind=..;id=0;act=..;stance=..;speed=..;row=K,0xD0,2;other=same`
or `gesture_lever=refused;reason=..;kind=..;...`. Log (one per command, uncapped):
`partner_gesture=lever; result=requested|refused; reason=..; kind=..; id=0;
act=..; upper=0x..; q28=..; stance=..; speed=..; called=0|1; row_kind=..;
row_d0=0x..; row_life=..; frame=..; rec4=0x..; rec8=0x..; recc=0x..;
other_same=0|1`. `recc` is PS rec+0xC: bit 28 (0x10000000) set refuses a sign
(`rec_c28`), and bit 20 (0x00100000) with `stance=3` refuses it as `stance`;
the lever checks both before the call, as the polling would.

Contract (`tools/generate_partner_contract.py`, `GESTURE_LEVER_SITES` and the
slots; 766 code sites / 197 vtable slots): the three request bodies, the
request test `0x1411E3A20` (frame window, kind dispatch and the two sign
branches: speed, act, rec+0xC bit 28), `GetStance` `0x141002350` (complete),
the polling's call and its kind -> state writes, the three upper writes (0x17 /
0x18 / 0x19), the five stock call sites, the knock entry's enable test
(`UpdateCommandSetKnock`, two sites) and its command binding, the CALL hold
classifier call; slots +0x28 / +0x30 / +0x38 / +0x58 of `0x23C5A20`; assertions: the
vtable's only store is the constructor, the request functions have no direct
caller and only data references, and the key-type table's ten values.

Scenario: `tools/worker/scenarios/partner-upper-gestures.txt` (key named on
the host only): after the pickups the standing host runs `gesture_lever
knock`, `sign_go`, `sign_stop` and a crouched `knock`; the prone guest bursts
`gest-knock`, `gest-sign`, `gest-sign-stop`, `gest-knock-squat`; waitlogs for
the host's `lever; result=requested` and `local; kind=knock; upper=0x17` /
`sign_go; upper=0x18` / `sign_stop; upper=0x19`, the guest's `rx`, `submit;
...; exist=1; kind=..; method=reserve`, `applied` and `ended`; the guest's own
`gesture_lever knock` must refuse with `key_off`; then a census of the stock
route (no gate): the host holds CALL (Q, renewed before every step so it
stays down without a new edge),
the menu is filmed (`callmenu-open`), `1` is pressed (`callmenu-select1`) and
then `E` (`callmenu-decide-e`) while CALL is still held. A host
`partner_gesture=local; kind=knock` there without a `partner_gesture=lever`
line before it would be the stock keyboard route; `E` may instead pet D-Dog
(act 94). Not run yet (no game launches in this phase): the lever, the knock /
sign nodes on body 1 and their frames are all unproven.

## Mounts

D-Horse and D-Walker riders are **hidden, never drawn** (docs/mounts.md has the
research). The viewer's copy holds no GameObject for the partner's mount (one
buddy block per copy, declared count 1), the DM anchor `0x1198770` seats only
GameObject types 50 and 4, and the rider states follow a live horse's state, so
any drawn rider would float.

The ride wire hides a horse rider: Lua reports `ON_HORSE` as kind 3, and
PlayerInfo `0x1412CE870` sets `ON_HORSE` only for act 121. It does **not** hide
a D-Walker rider: act 125 (`RideWalkerGearActionImpl::StateDrive`) sets no
`ON_*` status Lua binds, so the sender reports kind 0 (foot), and the viewer
keeps body 1 as an on-foot puppet whose remote position is the cockpit and
whose remote act (125) is no locomotion act: in the baseline frames (key off,
partner-mount.txt's baseline part) body 1 walks on foot along the walker's path with no
walker under it. The horse get-on / get-off act 122 is reported as foot too.

Key `partner_native_mount=off|hide` (**default hide with the native gun profile in lab and play**, a
named value records `partner_native_mount_given`, a named off opts out): with `hide` the viewer also hides body 1
while the remote's act is a mount act (121, 122 horse; 123..126 D-Walker), like the heli acts 97..108
(`partner_mount_policy.hpp`, `partner_ride_wire::Observe`). It acts only where `partner_native_ride` hides (not off).

Clearing the native realize bits (the ride hide's `partner_bits=cleared; reason=ride`) does **not** take body 1 off
the screen: Lua's all-instance-active mode (`Player.SetAllInstanceActiveMode`, P+0x204 bit 0x2000) keeps instance 1
realized, and in hidden runs `b264ad58` / `251abcef` the partner body stood idle at the get-on spot, turned to the
camera and without its name tag, for the whole horse or D-Walker ride. So while a mount episode hides body 1 and Lua
reports all-active, the hidden reply also carries `CLEAR_ALLACTIVE` (op 8; `partner_mount_policy::Unrealize`): Lua
sets the mode off (`partner_lua=event=allactive_cleared`), instance 1 is unrealized (probe `pool=00,ff`,
`rbits=0x1`; instance 0 untouched, it is the stock single-player state). The partner is shown again after the usual
500 ms of calm on-foot samples: placed, then the stage policy's `SET_ALLACTIVE` realizes it again
(`allactive_set`, `partner_realized=1`; the look is kept). Lines (driver tick, capped at 64):

- `partner_mount=configured; mode=hide; ride=<partner_native_ride>; acts=121..126` once per mission;
- `partner_mount=hide; mount=horse|walker; act=..; kind=<ride wire kind>; ride_live=..; episode=..` when the act rule starts hiding;
- `partner_mount=unrealize; mount=..; act=..; episode=..; op=clear_allactive` once per episode;
- `partner_mount=end; mount=..; act=<the first valid non-mount act>; ms=..; kind=..` when it stops (a missing act sample does not end it);
- the existing `partner_ride=hide; reason=ride; act=..` and `partner_ride=foot_stable; next=place`.

The horse kind-3 hide without the key still only clears the bits and leaves that standing body (filmed in
`251abcef`); the heli and vehicle-fallback ride hides use the same bit clear (not filmed).

Hidden runs (`tools/worker/scenarios/partner-mount.txt`, research build, gun stage, lab seed): `bfd6fcc4` (lab):
no partner body on the guest's screen while the host rode the D-Walker (two vantages) or the D-Horse, the body back
at the dismount spot, the guest's own act and status unchanged; the lab default follows it. `fbde3dcc` (play
rehearsal, `-WanSim bad`, key not named): the horse part passed the same way, but the run stopped before the
D-Walker part (Part 3 left the host mounted). Since 2026-10-22 the scenario runs the D-Walker part first and
presses a horse dismount E only after `host require localMounted=1`; its header says how to make the play copy
(docs/mounts.md section 8). The same day RIDE_BODY stopped moving the viewer's own D-Horse / D-Walker (docs/mounts.md
section 2): the "Part 3" stop above came from it pulling the host's horse away. On that build: `2ab88411` (lab, the
reordered scenario) repeats the `bfd6fcc4` hide frames, and in Part 3 the viewer's own idle D-Horse stays where it
stood while the host's first E mounts the host's own horse (each copy logs one `ride_body; action=refuse;
reason=not_mirror` line per mount id). `5a044a4d` (play rehearsal, `-WanSim bad`, rtt ~180 ms, key not named on
either copy): both copies log `partner_mount=configured; mode=hide`; no partner body while the host rides the D-Walker
(two vantages) or the D-Horse, nor on the host's screen while the guest rides its D-Walker (Part 4); the body back
after each dismount; Part 3 as in the lab. **Play now defaults hide too.**

## Rates

The locomotion speed is `base(0x109F7E0) * rate(0x109F690)` (callers
`0x10B9DC1`, `0x10BA6B7`); the rate is `rec+0x44` while crawling (stance 2),
`rec+0x50` while dashing (move class 3), 1.0 otherwise, from the body's own
record: `sp = *(PS+0x260)` (interface vtable `0x23AD380` at object+0x20,
`0x23AD328` while destroyed), `rec = *(sp+0x10) + (id - *(u32)(sp+8))*0x88`,
`id - base < *(u32)(sp+0xC)`, record vtable `0x23AD2B0` (`0x23AD260` while
constructed). The instance setup (`0x9CB7A5..0x9CB80C`) fills `+0x48` (dash)
and `+0x3C` (crawl) from the provider `*(P+0x130)` vt+0x28(id, 1|2) and derives
`+0x50 = +0x48 * +0x4C` and `+0x44 = +0x3C * +0x40`; `+0x4C` is a per-frame
dash factor (`0x10BD1C3`, reset `0x10A4C6B`), `+0x40` a crawl factor (provider
type 0x17 at `0x11403C9`, reset to 1.0 at `0x11313ED`). The provider answers
per id, so instance 1 kept 1.0 while the remote's local player had about 1.2
(runs a7823/63199: sprint 8.35 vs 10.0 m/s, crawl 1.38 vs 1.65; run and crouch
walk 1.00). Which provider input differs (hand mobility or staff skill) is
inferred; `native.rates` records the real values.

- `partner_player::ReadRates` reads instance 0's or 1's record read-only
  through the pinned chain (refusals: sp, sp_vtable, bounds, rec, rec_vtable,
  nonfinite, unpinned).
- Input frame v4 carries the sender's `+0x48`, `+0x3C`, `+0x40` x1000
  (500..2000, xflags `Rates` 0x10, bytes 72..77; see "Wire").
- `partner_rates` (pure) plans the writes: the three inputs, then `+0x50` and
  `+0x44` by the game's formulas (the dash factor stays the body's own); a
  field the game rewrote since the last write becomes the new original and is
  written again (`reapplied`); restore puts the originals back (a field the
  game rewrote keeps the game's value); out-of-range or non-finite values latch.
- The hook runs it in the drain window right before block 1's job (hook
  thread, no batch in flight, identity re-validated, SEH-contained; a changed
  record vtable latches without writing). The record must be instance 1's
  own, re-derived in the hook, never only the driver tick's pointers: id 1
  (Validate checked ids[1] == 1) and `sp == *(PS1+0x260)` with `PS1 =
  *(blk1+0x78)` (player PS vtable, `PS1+0x408 == blk1`), else it latches with
  `owner`. It restores at the next main dispatch once no enabled native Arm is
  published (disarmed, or a legacy Arm after leaving the native drive, e.g.
  Play's [I]); outside the drain the restore first re-checks the chain it was
  applied through (player slot P and vtable, ids[1] == 1, pool[1] unchanged
  and not instance 0's, its block, then the owner check) and drops without
  writing when it no longer holds. MissionStart/End forget the state without
  writing (the record dies with the mission) and pass the ending mission's
  epoch: an Arm still carrying it (published before the driver's disarm
  lands, a drain may run meanwhile) never applies the mirror again; each
  MissionStart is a new epoch.
- The driver reads both records every 100 ms and requests the mirror
  (`Arm.rates_mirror`) with `partner_native_rates=mirror` while the native drive
  is active and the remote's rates are under 2 s old.

Only instance 1's own record is written; instance 0, the provider and
S_IS_ONLINE are never touched.

## Facing

Static analysis of `mgsvtpp.exe` 1.0.15.4 plus the captures of hidden runs
a7823dbe, 63199c7a, bec857fb, d91fc20e and b9528e4d (the facing error is
wrap(body_yaw - remote_yaw) of the capturing role; `input.heading_err_mrad`
was not a facing metric: with a neutral stick it is the two cameras' yaw
difference, now `cam_diff_cdeg`, and `steer_err_mrad` is null below |stick|
0.1). Stops, pivots, crouch, prone and crawl already matched (|err| <= 0.3
degrees). The failures and their fixes:

- **The remote turned without locomotion** (debug placements, aim release):
  the round-4 idle rule (25 degrees, 800 ms, 2 s gap, acts 1..6 only) left
  2.5..7.9 degrees for whole sections. **The facing governor** (pure,
  `partner_recon` FaceMode, `partner_native_face=idle` by default, replaces
  `Action::yaw` under the native drive) turns a quiet body toward the
  time-aligned remote pose yaw with `partner_facing::Step` (180 deg/s, accel
  3600 deg/s^2): only while the remote stick r < 0.1, the remote's speed < 150
  mm/s and the body's own speed < 100 mm/s have each held 200 ms (any stick
  aborts a turn in the same tick), body act 1/3/5 with the remote in the same
  idle act, |err| > 5 degrees held 200 ms and the body's own yaw rate
  (readback minus our writes) below 30 deg/s (a pivot or turn animation the
  game still plays is waited out: reason `rate`), exit below 1 degree (the
  last write lands exactly; a driver tick gap over 250 ms restarts the eased
  turn from the readback, never a snap). Writes are the verified xform
  (T.vt+0x70, T.vt+0x68) at the body's own readback position on the driver
  tick (`Action::face`), refused while `partner_defer::InBatch()` or after
  the window-close shutdown (`partner_defer::ShuttingDown()`; never a
  placement on refusal); the start and end of a turn bump the body epoch,
  small steps do not.
- **Placed or teleported bodies** (fall/land acts 25/26, 77 degrees for 1.5 s
  after an 8 m teleport): the settle mode snaps once when |err| > 10 degrees
  within 1.5 s of a placement, snap or teleport, or while the body is in act
  25/26; a target that moved more than 3 m horizontally within 200 ms (no
  locomotion is that fast; the interpolated pose spreads a teleport below the
  per-tick 5 m test) is placed at once (`teleport_window_mm`).
- **Contest guard**: a readback more than 3 degrees from our last write (no
  write in between) means the game rotated the body: stop, back off 1 s;
  three contests within 5 s latch the governor off for 10 s
  (`face=contested`).
- **Placing while the viewer holds RMB** (57 degrees for 1.5 s): place and
  verify disarmed the window, so block 1's deferred job ran without one and
  read the viewer's live pad (act 11, the viewer's HOLD latch). Native
  placements now keep an enabled native Arm with `neutral` (every native
  window injects `Neutral()`: no held bit, no stick, HOLD latch bit 0
  cleared); verify also needs |yaw err| <= 10 degrees, else one xform at the
  placed yaw (`partner_recon=place_yaw_fix`) and verify again. A drain under a
  disabled Arm while the drive is active is now a hard arbiter reason
  (`unwindowed`; `defer.per_s.unwindowed_native`).
- **Standing aim oscillates about 30 degrees** (body act 12 the remote never
  shows; crouched aim converges toward the viewer's camera). Leading
  hypothesis O7: PS phase 0 (`0xFF28D0`) computes every player's look with its
  only call `0xFF29FC -> 0xFFAC00(PS, id)`, which offline rotates a constant by
  the VIEWER's camera quaternion `*(*(P+0x138)+0xB8)+0x90`; the aim states read
  it in the same phase-0 walk (`0x109F170`), before the post-phase-0 look write
  lands. **The look source hook** (`partner_native_aim=looksrc`,
  `native/partner_look_source.cpp`): a near stub (unwind data registered with
  `RtlAddFunctionTable`) runs `0xFFAC00` and then writes the remote's look into
  instance 1's slot, only while the drain window published it (the hook
  thread, instance 1's PS, ids[1] == 1, a live sample, the per-id record's
  skip bit clear); `jmp rel32` to it from the int3 padding `0xFFAD66..0xFFAD6F`
  (after `0xFFAC00`'s ret, outside every function, never a branch target);
  the call's rel32 low word changes from `ff 81` to `65 83` with one locked
  compare-exchange of the aligned qword `0xFF29F8` (the rel32 straddles the
  cache line at `0xFF2A00`; its high word stays `00 00`). Installed with
  defer=on and naim=looksrc only, never inside a batch, once per process,
  never removed; a refusal (`look_src=refused; reason=bytes|padding|caller|
  align|cache_line|...`) keeps the post-phase-0 write as the fallback. The
  facing A/B run (`partner-native-facing.txt` with and without
  `-LabSetting partner_native_aim=looksrc`) decides O7; if act 12 survives,
  `partner_native_face=aim` adds the governor's aim mode (body 11/12/14/15,
  the remote in an aim idle act, |err| > 10 degrees for 400 ms, the body's own
  yaw rate < 20 deg/s, 90 deg/s). **Outcome:** runs 1bffe18a (baseline),
  14ab2e00 (looksrc) and 8919e533 (face=aim) show the identical act-12 cycle
  with the look slot correct: O7 is refuted; the cause is instance 1's
  never-fed network aim target, fixed by the aim-target mirror below.

The contract checker pins the call and the instruction after it (rax unused),
`0xFFAC00`'s prologue, its S_IS_ONLINE gate, the camera chain and the store
tail, and asserts the single caller, no table entry for `0xFFAC00`, the padding
(ret at `0xFFAD65`, ten int3, no function, no rel32 or short branch and no table
entry into it), the qword's alignment and cache line and its exact bytes.

Telemetry: `native.facing` (setting, mode off|idle|settle|aim|contested,
err/target/body centidegrees, the body's own yaw rate, writes, snaps,
contests, turns, reason, place_yaw_err_mdeg, neutral_windows), `native.look`
(src off|pending|installed|refused, src_hits_s, src_hits/refused/skipped,
remote_cdeg and slot_cdeg {pitch, yaw}, err_mdeg = wrap(slot - remote) yaw,
also `gun.aim_err_mdeg`), the ring's `body_yaw_cdeg` and `face_err_cdeg`, the
1 Hz `partner_native_face; mode=..; setting=..; err_mdeg=..; act=..,..;
writes=..; snaps=..; contests=..; reason=..; look_err_mdeg=..; look_src=..;
src_hits_s=..; neutral=..` line and the `partner_recon=face_start|face_end|
face_snap|face_contest|face_latched|place_yaw_fix` events.

### The aim-target mirror (aim parity, `partner_native_aimpt`)

Status (2026-10-09, branch `wp/aim-parity`): implemented, fixture-tested,
**not yet run in the game**. Static analysis of `mgsvtpp.exe` 1.0.15.4 and the
captures of the facing runs 1bffe18a, 14ab2e00 (looksrc), 8919e533
(face=aim) and the weapons runs 24d3f9fb and f8ddb3f9.

**Why the standing aim wobbled.** O7 is refuted: the look slot matched the
remote in every capture (`look.err_mdeg` 0) and looksrc and face=aim produced
the identical trajectory. Act 12 is the standing aim turn (action states
11/12, handler `0x10B2720`), act 15 the crouched one (26/27, `0x10B0E60`).
The stand and crouch aim idles (`0x10B2699`, `0x10B0DE2`) start it when the
aim-turn predicate `0x10A0AB0` holds: `|(0.5 - r60) * pi| > 80 degrees`, with
`r60` the per-id aim yaw parameter (rate record `+0x60`, `r5c` `+0x5C` is the
pitch). Every gun state's update (`0x108E7D0`) recomputes both from the gun's
aim point `gunRec+0x150` (`0x1042F80`: the aim point's yaw/pitch relative to
the body, clamped to 90/75 degrees and smoothed), and gun phase 5
(`0x10407A0` -> `0x1096F90`) rebuilds that aim point every frame as a ray from
the viewer's camera `*(P+0xB8)+0xE0` through `Q+0x10[id]`, the per-player
network aim target (16-byte stride). Only the synchronizer writes it (Q
vt+0x18 `0xFE0080` push, vt+0x20 `0xFE02F0` interpolate, vt+0x28
`0xFDEDD0` snap, all under S_IS_ONLINE), so offline instance 1's gun aimed at
a fixed far point (the world origin in every capture). On the guest the
origin lay 94 degrees off the partner: the predicate fired, the 90-degree
pivot clip and the steering toward the (correct) look cancelled into a 30
degree swing, and two ticks after it ended the still-saturated predicate
fired again (period about 547 ms). On the host the origin lay 57..64 degrees
off: no turn, but the gun twisted that far.

**The fix.** The driver tick publishes a target (`partner_aim::Target`):
`look` = instance 1's `T+0x20` plus the stance pivot height (0.6 m standing,
0.2 m crouched, zero prone), then 100 m along
the remote's live v3 look `(sin yaw cos pitch, -sin pitch, cos yaw cos pitch)`
(the convention of `0xFFAC00` and `0x1042F80`); `point` uses the sender's own
standing/crouched aim direction from a negotiated input v6 frame. Prone refuses
the unqualified extra orientation (`prone_frame`). Without a fresh valid direction
it falls back to look and says `src=look`. The gun profile still defaults to
`look`; explicit `point` qualification remains separate. Inside block 1's drain window, right after the
magazine mirror and before the job, `partner_aim_point::Step` writes it where
the synchronizer would: `Q+0x10[1].xyz` (w kept) and record 1 of
`*(Q+0x1F0)` (stride 0x30: current and previous sample = the target, timers
`+0x24/+0x28/+0x2C` = 0, which is vt+0x18 twice plus vt+0x28; the
interpolation, if anything ran it, yields the target: duration 0 -> fraction
1.0 in `0xFDECB0`). The game's own pipeline then computes the remote's aim
point, aim pose (`r5c`/`r60`) and turn decisions. Instance 0's slot and
record are never touched; S_IS_ONLINE is never touched.

- Refusals (counted, logged once per change as `partner_aim=refused;
  reason=..`): `online` (P+0x204 bit 8 or the alternate dispatch), `owner`
  (P+0x214 or P+0x218 is 1), `q_vtable` (Q = `*(P+0x60)` not `0x23AD970`),
  `slots` (Q vt+0x18/+0x20/+0x28 not `0xFE0080`/`0xFE02F0`/`0xFDEDD0`, re-read
  live), `count` (id 1 not below `*(u32)(*(Q+0x1E8)+0x40)`), `array` /
  `record` (null or not 16-aligned), `range` (non-finite, a coordinate at or
  beyond 65536 m, or not 20..200 m from the body), `epoch` (a forgotten
  mission). `stale` (a publication older than 250 ms) and a wanted target
  without a body or look hold the slot (the last target stays). An access
  fault latches the mirror off until the next mission epoch
  (`partner_aim=fault`). The event lines (`installed`, `refused`, `restored`,
  `restore_dropped`) share a budget of 64 per mission epoch, the last one
  ending `; further=suppressed` (a chain flipping every frame cannot flood
  the log); the counters keep counting.
- `overwritten`: a drain that finds `Q+0x10[1]` changed since our last write
  (the census below says nothing should) counts it and logs it once per
  mission (`partner_aim=overwritten; q10_mm=..`).
- Restore: the originals (`Q+0x10[1]` and record 1) are saved at the first
  write of an epoch (`partner_aim=installed`) and written back when the
  publication no longer wants a target (the setting or the debug override
  off, the drive inactive, no gun) at the next drain, or at the next main
  dispatch once no enabled native Arm is published (the rates mirror's rule),
  after re-validating the chain (player slot, P vtable, Q, its array and
  record); a field the game rewrote since our last write keeps the game's
  value (`partner_aim=restored`); a chain that changed drops the state
  without writing (`partner_aim=restore_dropped; reason=chain;
  changed=player|object|<reason>; written=0`). MissionStart/End forget without writing
  (`PartnerForgetRates` passes the ending epoch to both mirrors). After the
  window-close shutdown nothing is written or restored.
- Setting: lab.ini `partner_native_aimpt=off|look|point` (default off; inert
  unless the native drive is active with `partner_native_gun=1`). The worker
  gun stage (`Get-WorkerPartnerSettings`) and Play-OfflineCoop set `look`
  (Play-OfflineCoop `-NoAimMirror`: off);
  the worker gate admits off/look/point in the gun stage only
  (`-LabSetting partner_native_aimpt=off|look|point`). The natively handled
  debug command `aimpt off|look|point|lab|status` overrides it live for a
  one-session A/B (`lab` returns to lab.ini).
- Not covered (open): a sighted remote weapon turns its owner at about 27
  degrees of camera swing while body 1's stock rifle needs 80 (the predicate
  on body 1's own gun), so a remote turn of 27..80 degrees twists the partner
  instead of turning it (`partner_native_face=aim` covers it; real parity
  needs the weapons track's body-1 weapon switch); the bone path (`gunRec+0x3D0`
  bit 9 with `+0x3D5 != 1`, `0x1097710`) ignores `Q+0x10` (`aim.path1` says
  `bone`); `look` carries the remote camera's parallax (about atan(0.6 m /
  distance of the remote's own aim point)); other readers of `Q+0x10[1]`
  (`0x108D270`, `0x1099AB0`, `0x10CBF50`, `0x10D8DC0`, `0x1221920`,
  `0x124BD30`, `0x125AEB0`, `0x126C570`, `0x126F0F0`) now see a real target
  instead of the origin: whether a guard on the viewer's machine reacts to the
  partner aiming at it is unverified.

The contract (`tools/partner_aim_sites.py`, appended by
`tools/generate_partner_contract.py`; `--check`: 449 code sites, 98 vtable
slots) pins the predicate and its two callers, the r5c/r60 update and its rate
record, the aim point builder (the `Q+0x10[id]` read, the S_IS_ONLINE/local
gate, the camera origin, both stores) and the path selector, the three Q
methods (reached only through the live Q vtable `0x23AD970`, stored once at
`0xFDEDB4`, and its sibling `0x23AD8A0`), the fraction helper, the Q
allocator (array n*16 and records n*0x30, 16-aligned, records initialized
cur = prev = 0 with duration -1.0), the act-12 entry, the local-only camera
aim writer, the range globals and their initializer order, the constants,
the static handler table (simulated: states 10/11/12/25/26/27), and a census
of every 16-byte indexed store through an `[object+0x10]` array: exactly four
write `Q+0x10` (vt+0x28 `0xFDEDFC`, vt+0x20 `0xFE03B6`, and the
state-specific `0x12755AA` and `0x1200FC2`, which the partner does not use
while aiming a gun; `overwritten` catches it if it does).

Telemetry: `native.aim` (`setting`, `override`, `state`
off|wait|ok|hold|refused|latched, `reason`, `src`, the hook's `writes`,
`refused`, `holds`, `overwritten`, `restores`, `drops`; `target_mm` (this
tick's target), `written_mm` (the hook's last write; the driver tick and the
drain share the game's main thread, so the target leads it by one tick while
the aim or the body moves), `slot` ours|other|na (`q10_1_mm` within 1 mm of
`written_mm` on every axis: the slot holds our write; other: restored or
written by something else), `q10_1_mm`/`q10_0_mm` (the slots),
`aimpt1_mm`/`aimpt0_mm` (the gun aim points), `aimpt_err_mm` = |aim point 1 -
`written_mm`| while the slot is ours (else against the target); `aim_yaw1_cdeg`/
`aim_pitch1_cdeg`/`aim_yaw0_cdeg`/`aim_pitch0_cdeg` = the world direction of
each gun's aim point from its body's eyes; `rel_yaw1_cdeg`/`rel_yaw0_cdeg` =
(0.5 - r60) * 18000, what the predicate tests (beyond +-8000 turns);
`r5c_1`, `r60_1`, `r5c_0`, `r60_0` x1000; `pred1`/`pred0`; `gun_state`
(rec+0x2C0), `b3d5`, `path1`/`path0` cam|bone|na), the ring's `r60_1` and
`pred1`, the 1 Hz `partner_native_aim; setting=..; override=..; state=..;
reason=..; src=..; writes=..; refused=..; holds=..; overwritten=..;
restores=..; q10=..; slot=..; target=..; err_mm=..; yaw1=..; yaw0=..; rel1=..;
r60_1=..; pred1=..; gun1=..; path1=..` line (own cap, while its key changes)
and the `partner_aim=installed|refused|overwritten|restored|fault` events.

Runs (coordinator, hidden worker): `tools/worker/scenarios/partner-native-aim.txt`
(a one-session A/B: `guest debug aimpt off` for aim-a, `guest debug aimpt lab` (back
to lab.ini, so `-LabSetting` keeps deciding) from aim-b on;
camera-rotated aims, pitch, crouch, prone, aim-walk, aim-strafe, a remote aim
turn, both directions) and the aim sections of `partner-native-facing.txt`
(face-aim-off, the qualified +-1500 aim turns, aim-cam, aim-strafe, aim-walk,
crouched aim and crouched strafe); the pass criteria are in each header. The
facing scenario's old `aimturn host guest 900` could not pass: dx 700 and 900
never turned a settled aimed body (runs fca0d6dc, 1bffe18a, 14ab2e00,
8919e533), dx 1500 did in four runs; `aimturn` now takes +-1500 only.

## Weapons

Status (2026-10-08, branch `wp/native-weapons`, merged into `integ/finish`
with the native actions): implemented, fixture-tested, **not yet run in the
game**. Static analysis of `mgsvtpp.exe` 1.0.15.4 only;
`tools/generate_partner_contract.py --check` pins every site below (365 code
sites and 91 vtable slots with the actions and rates sites, plus the
reachability, owner-stamp, EVADE census and reference assertions).

### Fire visuals (`partner_native_fire=visual`)

- **How the partner shoots.** Instance 1's gun update `0x103E2D0` runs inside
  block 1's drain; the shoot function `0x1041110` stamps the shooter's id into
  the shot request (`request+0x2A = id & 0x1FF`, `0x1041219`) and queues it
  (equip vt+0x138, `0x10414FC`). The model group's shot handler `0x1302F50`
  later spawns every bullet through **RequestBullet `0xD32C60`** (calls
  `0x13031C8`, `0x13031FA`, pellets `0x130343C`; `rdx` = the record, whose
  `+0x2A` it tests itself at `0x1302FF3`) and plays sound and the "Shoot"
  effect independently of that call.
- **Instance 1 is a non-local shooter.** `rec+0x3C0` bit 0 (`id == P+0x218`)
  is clear for it; `0x10415AB` then skips the whole local block. The checker
  proves by reachability (capstone, branches at `0x104122C`, `0x10415B4`,
  `0x1041E6E` taken) that the ammo call `0x1041650`, the camera kick
  `0x1041775`, CAM+0xB0 `0x1041BDB`, the shot origin `0x1041C6E`, the shoot
  count `0x1041CA5`, suppressor life `0x10417D6`, the synchronizer `0x1041946`
  and `0x1041BF4` are unreachable for it. No sandbox is needed for them.
- **The filter** (`native/partner_fire_filter.cpp`): RequestBullet has no
  direct caller; it is reached only through slot 0 of the bullet iface
  vtables `0x2337DE8` (live, stored by the ctor `0xD32D14`) and `0x2337D18`
  (while destroying, dtor `0xD32619`), both read-only data and 8-aligned. The
  install checks the prologue bytes and both slots, then swaps each slot with
  one locked qword exchange (protection restored; the first slot is swapped
  back when the second fails). The hook drops a request whose owner equals the
  armed id (instance 1's GameObjectId, type bits `0xFE00` clear; armed with
  `ids[1]`) and returns 0, the value RequestBullet itself returns when its slot
  allocator yields nothing (`0xD32CB4`); every other request goes to
  `0xD32C60` unchanged. Any thread (atomics only). Installed lazily by the
  driver tick once per process, never removed, and never disarmed once armed
  (a shot request of instance 1 queued while FIRE was allowed can reach
  RequestBullet after FIRE is masked again; instance 1 fires only under the
  native drive). The driver refuses an owner equal to the local instance's id
  (`ids[0]`), so the local player's shots are never dropped. Verified callers:
  RequestBullet uses only `rcx`/`rdx`, the instruction after each of the
  three call sites in `0x1302F50` does not read `rax`, and 0 is a value
  RequestBullet itself returns. The checker asserts that the shot queue
  (`call [rax+0x138]` with a request in `r8`) has exactly four owner stamps:
  the player shoot (`0x1041225`, `id & 0x1FF`) and three typed builders,
  the soldier ones `0x13A190E` and `0x1453385` (`0x400 | index`, type 2) and
  `0xEDAC19` (`type << 9 | index`, the type word of its system at
  `[this+0x70]+0x168`). Only a player-type shooter with index 1, that is
  instance 1, carries owner 1; no soldier shot is ever dropped.
- **Kept on the viewer's machine:** fire animation, recoil, muzzle and "Shoot"
  effects, gunshot audio, casings, the prone ground-dust effect. **Removed:**
  projectile, hit-scan, damage, impacts and tracers.
- **The fire gate.** FIRE (0x10) reaches instance 1 only while
  `partner_native_fire=visual`, `partner_native_gun=1`, the filter is
  installed, live (both slots still hold it) and armed for `ids[1]`, and
  instance 1's gun record has shown a gun class (`(rec+0x27C>>6)&7 == 1`) in
  this mission (latched: body 1 only carries stock46, and a holster/draw or
  an unreadable tick must not toggle the mask, whose change bumps the Arm gen
  and resets instance 1's private pad masks).
  Otherwise FIRE is masked and the reason is logged once per change
  (`partner_fire=masked; reason=off|gun_off|unavailable|not_ready|owner|gun_read|weapon|bytes|slot|changed|protect|alignment|disabled|hook_lost|disarmed`).
  The filter installs only after body 1 has shown a gun class (a body that
  cannot fire never gets the patch). With `partner_drive=native` FIRE is also
  stripped from the mask whenever the native drive is not active (the
  fallback, or a legacy input lever a trigger armed), so the gun stage's mask
  never reaches instance 1 outside this gate.
- **Not covered (open risk):** projectile weapons (row type 3 go through
  `gm+0x210`, not filtered; body 1 only ever carries stock46, a bullet weapon).
  The **unsuppressed shot noise** `{pos, 0x145, owner}` that `0x1302F50` sends
  through `group+0xA0` vt+0 at `0x1303D06` is now filtered (F3 below: AddNoise
  `0x515270`, slot 0 of `0x21876A8`, the census's `noise_vt`); the read-only
  census (`partner_fire_census; ...; noise_vt=...; noise_fn0=...`) stays.

### Why the partner never fired or reloaded (run b9528e4d) and the fixes

- **The trigger read the viewer's sample rings (F1).** The gun update's
  trigger `0x1095DF0` (its only call `0x103E485`) chooses by `rec+0x27C` bit
  24 (`0x1096137`); weapon 46 (the semi-auto RC_40204 shotgun, inferred from
  its capacity 9 and carry 42) takes the history path: the ring `[[PP+8]+8]`
  with the samples of this update `[[PP+8]+0x44]` (`0x1096171`), or the
  single held word when PP vt+0x98 (keyboard mode, `PP+0x488` bit 6) is set.
  Every window clears `PP+0x488 & 0x5F`, so instance 1's trigger read the
  viewer's gamepad history (no FIRE: the ring's gun3c0 never showed bit 24;
  with a gamepad on the viewer it would have fired on the viewer's trigger).
  Drain windows now point the src rings (held at `src+8`, sticks X/Y at
  `src+0x10+8i`/`+0x20+8i`, triggers at `src+0x30+8i`; the src update
  `0x435260` = src vt+0x38, called by the PP update) and the count `src+0x44`
  at instance 1's own rings: count 1, entry 0 the injected level, the older
  entries 0 for a keyboard remote (re-fire at the weapon's rate while held,
  exactly the remote's own behaviour) or this drain consumer's previous held
  word for a gamepad remote (input frame v4 xflags `Gamepad` 0x40, the
  sender's `PP+0x488` bit 6 clear); restored exactly after the job (also on
  unwind). Refusals: no src, capacity `src+0x40` outside 2..128 (the game's is
  120), a fault. The keyboard-mode bit itself is never flipped (eleven
  readers, including the move sampler `0x10BCD90`).
- **The mirrored magazine was overwritten every frame (F2).** Gun phase 0
  (`0x1040100 -> 0x103E010 -> 0x108C8C0`, call `0x103E08C`) copies the
  per-instance ammo store into `rec+0x25C/+0x25E` every frame
  (`Y = *(*(P+0xC8)+8)`, row `*(Y+0x50) + (id * u32[Y+0xC] + u8 rec+0x3DA) * 6`,
  word 0 / word 1). The mirror now writes that store row (word 1 with the sub
  magazine), then the record, never through the setter `0xEE96C0` (its
  notifier `[Y+0x58]` vt+0x100 takes no id). Refusals (`mag.store.last`): the
  store iface vtable not `0x23894B0`/`0x23895D0` or its +0x20 not the setter
  (`y_vt`), stride outside 1..16, the slot beyond it or id 1's row at or past
  the store's row count `Y+0x10` (the store init `0xEE92F0`, object vt+8 of
  both classes, sizes the types and rows by ids * stride: `slot`), the row type
  outside 1..9 or type 9 with a sub magazine (`type`), instance 1 as the local
  id (`owner`), S_IS_ONLINE or `P+0x204` bit 18 (`online`), `rec+0x3D0` bit 27
  (`tamper`: the obfuscated tamper counters at `*(P+0x48)+0x2F8`; bit 27 is set
  only by three `P+0x204` bit-18 gated sites, never offline; the checker's
  census fails on a fourth), a value above the capacity (`cap`). Completing a
  reload is safe for a non-local body (`0x1054A97`: the local branch runs the
  ammo setter; instance 1's writes `rec+0x3E8` and its own store row through
  `Y vt+0x20`); `leak.ammo_leaks` still watches the viewer's ammo vars.
- **The shot noise is dropped on this machine too (F3).** `0x1303CD4` sends
  `{pos, u8 0x45, u8 1, u16 owner = record+0x2A}` through `group+0xA0` vt+0,
  which is AddNoise `0x515270` (slot 0 of the noise iface vtable `0x21876A8`,
  the census's `noise_vt`; stored only by its ctor `0x51517F`, dtor `0x515246`
  and the static initializer `0x2074C5C`; no direct caller; the call site
  ignores rax; AddNoise keeps the record's `+0x12` owner, `0x5152F9`). The
  noise filter swaps that slot like the bullet slots and drops a record whose
  owner is the armed id (instance 1); everything else passes. FIRE needs it
  live (`partner_fire=masked; reason=noise_<why>`), unless the lab key
  `partner_native_noise=keep` (the worker's gun stage requires `filter`).
- **The weapon gate (F4).** Body 1 carries one gun (stock46). Input frame v4
  carries the sender's gun record equip (xflags `Equip` 0x80, bytes 82..83).
  A customized weapon's record holds its alias (the lab save's back slot:
  alias 873 of base 46, hidden run b9528e4d: `remote_equip` 873, `body_equip`
  46), so the receiver compares it as the stock id of the received loadout
  manifest slot whose alias it is (`weapon.remote_gun_base`; a stock id, an
  alias no slot names or no manifest yet stays itself). While neither the
  raw equip nor that base is body 1's equip every native window strips FIRE
  and RELOAD from its injection (`Arm.strip`, not compared by SameArm: no gen
  bump) and the magazine mirror freezes (`mag.last=weapon`); logged as
  `partner_weapon=mismatch|match; remote_equip=..; remote_base=..;
  body_equip=..; strip=..` and, with visual fire, `partner_fire=masked; reason=weapon_mismatch` /
  `partner_fire=live; reason=weapon_match`. A real weapon switch on body 1
  (the generalized writer, slot-select bits) stays open.
- **Trigger evidence.** After each windowed drain the hook reads instance 1's
  gun record: shots the trigger requested (`rec+0x3C0` bit 24 with the count
  `rec+0x3DF`, `0x10964F7`) and drains that left the gun in a fire mode
  (`rec+0x2C0` 9 or 10, `0x103E53A`): `native.trigger.requested`,
  `shot_drains`, `fire_modes`.

### Reload: the magazine mirror

- CanReload `0x1051F30` needs `mag < capacity` (and no `rec+0x280` bit 19,
  and spare stock); the reload request `0x1094770` (gate `0x103E3A4`: class 1,
  equip `rec+0x24C != 0`, `rec+0x3D5 != 4`) sets `rec+0x3C0` bit 20 from the
  R press while aiming (else the tap classifier). Instance 1's non-local shots
  never spend its magazine (`0x1041650` is local-only), so its magazine stayed
  at the change-equip ammo: 0 (fire impossible) or 30 with the ammo
  experiment (above the sniper's capacity: reload impossible, run bec857fb).
- **Input frame v4** (112 bytes, shared with the rates; see "Wire") adds the sender's magazine as its getter
  reads it (`0x10435D0`: capacity with `rec+0x3C0` bit 28, else `rec+0x25E`
  with `rec+0x27C` bit 9, else `rec+0x25C`) and its capacity (`rec+0x210/212`,
  doubled for equip `0x42B..0x42E` with bit 20, halved with bit 19), xflag
  `Mag` (0x20, bytes 78..81). Sent whenever the copy sends input frames
  (`partner_input_send=1`), whatever its own drive (since "Online").
- **The mirror** (`partner_input_hook.cpp` MirrorMag) runs in block 1's drain
  window before the job, on the thread of the gun update, under a native Arm
  with `partner_native_gun=1` (fire on or off). On a FIRE or RELOAD press
  edge of the drain's view, or when the remote's (mag, cap) changed, it
  re-validates the gun prim (vtable `0x23B3E80`, `prim+8 == blk1`,
  `prim+0x58 == rec`), refuses a full-magazine gun (bit 28), a non-gun class
  and a capacity outside 1..999, and writes `partner_native::MirrorMagazine`
  into the live magazine field (`0x25E` or `0x25C`, as the ammo setter
  `0x1058ABF` does) only when it differs: full stays full (no reload), empty
  stays empty, anything between maps into 1..cap-1 (rounded up), and a FIRE
  edge leaves 1 instead of 0 only when the remote fired a round (its
  previous magazine was loaded: the press spent the last round in the same
  sample); a FIRE press on an already empty remote is a dry fire and shows no
  shot. A fault disables it until MissionStart, and so does an ammo leak (a
  drain whose job changed the viewer's weapon/ammo vars, see `leak` below:
  `partner_input=ammo_leak; mag_mirror=disabled`, `mag.leak_off`). Known limit: after that last
  round the partner keeps 1 round until the remote's magazine changes, so an
  automatic partner weapon held down keeps firing visual rounds until the
  release (the scenario's firehold section records stock46's behaviour).
- The ammo experiment (`MGS_LOCAL_STOCK46_AMMO_TEST`) is superseded by the
  mirror; it stays research-only and is not used by the weapons scenario.

### The partner's own reload (R1..R6, `partner_native_reload`)

Status (2026-10-09, branch `wp/weapons2`): implemented, fixture-tested, not
yet run. Static analysis of `mgsvtpp.exe` 1.0.15.4; every site below is in
`tools/generate_partner_contract.py --check` (446 code sites, 101 vtable
slots) and in the runtime code gate.

- **What the hidden runs showed.** Body 1 already enters its own native
  reload when the remote's R reaches it: at the R tick the ring's gun3c0 goes
  `0x40060 -> 0x4040062` (aiming) or to `0x4000040` (holstered), exactly the
  reload enter `0x1077A36..0x1077AA6` (`rec+0x3C0 = ((aim | 0x200002) << 5) |
  (old & 0xFFBFFFDF)`) plus the transition bit 1 (`0x1040A78`). About 1 s
  later the magazine mirror wrote the remote's refilled magazine into body 1
  while it was still reloading; body 1's own completion `0x1054900` (the only
  caller is the reload event handler `0x1079B6E`; its non-local branch
  `0x1054A97` builds the new magazine from the shadow `rec+0x3E8`, written at
  the enter `0x107833C`) then wrote 8 of 9 and the reload mode (bit 26, cleared
  only by the exit `0x10790AA`) did not leave for 1..3 s.
- **Why the old counters stayed 0.** Gun phase 6 (`0x103EB70`, jump-table
  entry 6 of the gun iface dispatcher `0x1041000`) clears `rec+0x3DF` and
  `rec+0x3C0` bits 12/20/24/25 (`0x103F99C`); all seven phases run inside the
  drain, so a read after the job never sees bit 20 or 24. `mag.reload_edges`
  only counted R edges on which the mirror wrote.
- **R1 telemetry.** `gun.mode` (the current mode `rec+0x2C0`, written by the
  transition `0x1040A88` from the mode table `0x2C556A0`: 7 aim, 8 weapon
  change, 11/12 reload `0x1077840`; the checker emulates the table's static
  initializer `0x20252F0`), `gun.b26`, `gun.upper_req` (`*(Q+0x20)[1]`, 5 =
  reload, `0x1077A0A`), `gun.m3e4/m3e6/m3e8`, ring fields `gmode`/`rmode`. The
  **post-phase-1 probe** reads instance 1's record right after
  `svc28(root1, 1)` inside a drain (the same nesting the look write uses after
  phase 0): `reload.probe1.b20/b24/shots/mode_changes/mode`.
- **R2 wire.** Input frame v5 (116 bytes, payload 88): `[84]` gun flags
  (valid, in hand, aiming, reloading, changing), `[85]` the sender's
  `rec+0x2C0`, `[86]` `reload_seq` (+1 per entry into 11/12 its tick sees),
  `[87]` `switch_seq` (+1 per change of its current slot); the v3 `inv_slot`
  byte carries the sender's current slot. v1..v4 still decode; a v4-only
  build refuses 116-byte frames, so v5 goes out only after the peer's own v5
  (or as a probe every 2 s; "Wire").
- **R3 event machine** (`native/partner_reload.cpp`, pure, driver tick): a
  remote reload start (counter moved, mode 11/12) with the native gun window
  armed, the weapon gate passing, a gun class, the reload gate and CanReload
  taps RELOAD through the existing input overlay (`kTapFrames` windows: the
  press word while aiming, the hold classifier's tap when holstered), retried
  every 250 ms up to 3 taps until body 1 shows 11/12; `entered` ->
  `complete` when the mode leaves (every native exit counts, see F1 below),
  `stuck` after 8 s (a report only), `release` after 20 s (a lost exit).
  Fallbacks (the plain
  mirror, no animation): `inactive|weapon|body|cls|gate|no_reload|timeout|
  remote_done|released`. Holstered and empty-magazine automatic reloads arrive the
  same way (the sender's mode, not its pad). `partner.trigger rtap=1` (worker
  `run partner rtap=1`) forces one test tap.
- **R4 freeze and reconcile** (`MirrorMag`): with `raw|event` the mirror
  writes nothing while the remote reloads (v5 flag), while body 1 is in 11/12
  (never released at a stuck report; only the 20 s release of a lost exit
  ends it) or while the machine holds; the first write afterwards (the
  reconcile) also writes `rec+0x3E8` (the completion's base) so a later
  completion starts from the mirrored magazine. An R edge no longer
  re-asserts the remote's magazine (only with `mirror`). A change of body 1's
  slot `rec+0x3DA` rewrites the magazine of the new weapon.
- **R5.** With `event` and a live v5 group the remote's raw RELOAD (also
  CARRY, the same bit) is stripped from its sample (`Config.sample_strip`,
  not compared by SameArm); the overlay tap still delivers it. A v4 remote
  keeps the raw R.
- Settings: `mirror` (today's behaviour, the lab.ini default), `raw` (freeze
  and reconcile only; isolates "the mirror broke it"), `event` (R2..R6; the
  worker gun stage and Play-OfflineCoop).
- **F1 (weapons3; hidden run 53e36944, read-only diagnosis).** Body 1's
  reload was never stuck: it exits natively (`partner_reload=body_exit`,
  mode 11 -> 8, bit 26 cleared, `upper` 2) after 3.02 s, because the reload
  playback rate `rec+0x310` is per player (instance 1 about 1.0 offline; the
  host's own player about 1.5: its reload lasts 2.0 s). The old 2.5 s stuck
  rule dropped `entered` before that exit (so `completed` never counted) and
  unfroze the mirror mid-reload. Now (`native/partner_reload.cpp`, pure):
  `stuck_ms` 8000 and a report only (`partner_reload=stuck`, `reload.stuck`):
  the machine stays `entered` and holds the mirror until the native exit;
  every native exit of an entered reload counts (`complete`, or
  `complete_late` after 8 s, `reload.completed_late`); the mirror reconciles
  once after the exit (the remote's magazine and `rec+0x3E8`, only when they
  differ), never at a stuck report; an unread tick neither exits nor
  re-enters. **Lost exit (review):** an exit that never arrives (body 1
  unread for good, or wedged in 11/12) must not freeze the mirror forever:
  after `release_ms` 20000 (far above any native reload) the machine logs
  `partner_reload=release`, leaves `entered` without counting a completion
  (`reload.release`, `reload.releases`), and the Arm's `reload_release` lets
  the hook stop waiting for body 1's mode (the first write is the
  reconcile; the remote's own reload still freezes); the latch clears when a
  readable tick shows body 1 out of 11/12, and a remote reload while it is
  latched falls back (`released`). **Queued reload:** a remote reload start (counter moved, mode
  11/12) while body 1 is still entered is queued; if the remote is still
  reloading at body 1's exit the machine taps once more
  (`partner_reload=queued_tap; ...; reconcile=1`, `reload.queued_taps`) and,
  while that tap is pending, asks the mirror to reconcile although the remote
  reloads (Arm `reload_reconcile`; body 1's own completion just filled its
  magazine and its next reload request needs it below capacity), then holds
  again from `entered`. Telemetry: `reload.len_ms`/`len_max_ms` (body_exit -
  body_enter), `remote_len_ms` (the remote's mode-11/12 span), `refill_ms`
  (body_enter -> the first magazine change body 1 makes itself); the log line
  `partner_reload=complete|complete_late; setting=..; len_ms=..;
  remote_len_ms=..; refill_ms=..; mag=..; m3e8=..; completed=..; entered=..;
  late=..; queued_tap=..`. `gun.rate` (instance 1's `rec+0x310`) is not read:
  that read is not contract-pinned. The reload keeps body 1's own native
  length (no rate change, no patch: F2 is out of scope).

### Weapon switching (W1..W6, `partner_native_slot`)

- **How an instance switches** (exe, contract-pinned): each instance has an
  equipment row `*(Q+0xC0) + id*0x3A` (`0x11AD240`); the low nibble of
  `+0x36` is its current slot. Gun phase 0 (`0x103AED0`, its only caller
  `0x103E044`) compares it with the slot in hand `rec+0x3D9` (with
  `rec+0x3DE == 7`) and sets `rec+0x3C0` bit 7; with no gun mode active the
  same phase activates the slot's weapon at once (`0x103E049 -> 0x105FDB0`),
  while aiming the aim decider goes to mode 8 (`0x1072D12`, the native
  weapon-change sequence `0x1074070`). An empty handle (`>> 16 == 0xFFFF`)
  un-equips (`0x105FED2`). The pad's weapon bits only reach UI code: they stay
  masked.
- **W4 slot follower** (hook, drain window before block 1's job): instance
  1's nibble gets the remote's v5 slot (0..2) the way SetCurrentSlot
  `0x11AFA80` writes it for a non-local id (`Q+0x17C |= 2`, the nibble,
  `row+0 = (row+2+2*slot) & 0x7FF`; the checker proves the non-local exit
  `0x11AFE4B` is reached before any call for types 0/1/2), only to a slot the
  driver's plan admits and whose handle holds the planned equip. `call`
  (lab only; play runs `write`) calls `0x11AFA80(ctl, 1, slot, 0, 0, 0)`
  through the pinned controller slot. Verdicts (`partner_slot=write|refused;
  reason=..`): `same|wait|plan|chain|online|owner|mode|empty|mismatch|
  readback|fault|other|no_v5`; refusals count once per (slot, switch_seq).
- **Plan** (`native/partner_slots.cpp`): per slot `same` (body 1 holds the
  remote's base id), `sub` (another weapon of the same type, or of the same
  slot class when both types belong to this slot: the writer's tier C),
  `empty`, `other`, `unknown`; only `same|sub` are admitted. The remote's
  type of a slot is the manifest's own (version 2, also with base 0: the
  alias's type), else the type table's type of its base id (a version 1
  sender).
  Body 1 carries what the writers gave it: the stock46 writer only the back
  slot (plan `empty,same,empty`: the hip or the secondary refuses with
  `empty` and body 1 keeps its gun), the generalized writer below each slot's
  tier A1 (`same`) or tier C (`sub`) target.
- **W5 gate.** FIRE and RELOAD pass only while body 1's weapon is the
  remote's (raw equip or its manifest base) and body 1 is not mid-switch
  (`rec+0x3C0` bit 7 or mode 8: `weapon.gate=switching`); FIRE additionally
  needs a qualified bullet type (`weapon.gate=projectile` otherwise: shot
  class 3 at `0x13030B7` goes to `gm+0x210`, not filtered). The allow-list is
  the bullet categories 1 Assault, 2 Handgun, 3 Submachinegun, 4 Sniper,
  5 Shotgun, 6 Machinegun (the EquipIdTable `EQP_TYPE_*` names joined with the
  H2 capture of the runtime type table); 7 GrenadeLauncher, 8 Missile and
  anything else keep FIRE masked. A substitute (plan tier `sub`) in the remote's current slot
  (its live v5 slot, else the manifest slot whose alias or base is its
  in-hand equip) counts as the remote's weapon (`GateIn.substitute`); the
  old rule (the slot whose manifest base equals the remote's base) masked a
  base-0 slot as a mismatch.

### Generalized preload and writer (W2/W3, `partner_native_equip`)

Status (2026-10-09, branch `wp/weapons2`): implemented, fixture-tested, not
yet run. Static analysis of `mgsvtpp.exe` 1.0.15.4;
`python tools/generate_equip_writer_pins.py --check` asserts every static fact
below from the retail bytes and regenerates `native/equipment_stock_writer_pins.hpp`
(106 SHA-256 gates: the 92 stock46 gates unchanged plus 14 for the
generalized pieces), which both writers and the preload receipt check at
runtime before acting (`full_body_gate`).

- **The stock46 contract, parameterized.** The writer contract
  (`equipment_stock_writer_contract.hpp`) takes a `Target`; `kStock46` is
  the qualified stock46 contract constant for constant (the existing fixture
  tests run unchanged against it). `Derive` builds a target for any stock gun
  id 1..0x22F from the executable tables and the runtime banks, pure reads:
  - type/row: `packed = u16[types+8+id*2]` (`A2A3E0`/`A2A520` for ids below
    0x230: `packed & 0x3F`, `packed >> 6`); types 1..8 only (`DB6AC0` and
    `DC3700` serve no others; aliases 871..876 and blueprints 0x230+ refused);
  - slot: ChangeEquip `11AC4AA` and the lower writer `120A5D2` agree (the
    generator emulates both): types 1/5/7 hip (0), 4/6/8 back (1), 2/3
    secondary (2); with the qualified request flags 4 ChangeEquip also calls
    SetCurrentSlot for the new slot (`11AC4A0..11AC57B`);
  - GunBasic recipe (`0x2C25C50 + (row-1)*12`, row 1..514), the package row
    `0x2C20FD0 + id*24` (`A03C40`: parts hash, main FPK, flags 2), the parts
    class not `0x1094` (the ordinary `ADDEEB` branch);
  - the dependency packages exactly as `DB6AC0` selects them: recipe lanes
    0/1/2/3/10 through the LUT getters (`2349A90`, `2349B80`, `2349C00`,
    `2349CC0`, `2349A78`), the id tables `2349980` (two), `23499E0`,
    `2349A10` (two), `2349A08`, `2349A68`, hashes `2349348 + id*16`
    (`DB69E0`); stock46 derives `[36,55,64]`;
  - the optional asset: `[2A6B400+8+id*8]` for ids up to 0x7C (`1307EF2`);
    a larger id or a zero descriptor takes the NULL optional (family `plain`):
    `ADE358` still renders on the main bank with no asset, `1E1921F` skips
    every asset branch, size `512 + align16(8g+88)` (640 at g 5; at most 1824
    for g <= 153, within the 2048 stride). Family `optional` admits the
    constructor shapes of the exe's own per-parameter table (weapons3):
    header word0 = n parameters 1..8, word1 = m(n) and words 2..5 zero, the
    magic and parameters 3 kept; `1A95470(n, mode 0)` over the records
    `1A953D0(0)` returns (`0x2B4A078`) gives m(n) = 2,4,5,6,7,8,9,10 and the
    request size 224,400,480,544,624,688,768,832 (`kOptionalShapes`,
    asserted by `generate_equip_writer_pins.py --check`); render bytes
    `512 + size(n) + align16(8g+88)` <= 2048 (stock46 is n4/m6: 1184 bytes
    at g 5);
  - the cache entry: the first used entry of the id (`DC3755..DC376A`),
    identity type/receiver, flags 3 (protected) and refs 0 for A1/C (never an
    entry the viewer references).
  Per-slot layout (the generator asserts it): the binder record
  `binder30 + slot*12` (`B0C720`), the effect record `body628 + slot*0x50`
  (`9C5980`), the attachment items `13*slot .. 13*slot+12` (`B0CF80`).
- **W2 preload** (`SelectEquipPreloadReceipt`, the Lua chunk after the stock46
  one in `lua/coop/equipment_stock_preload.lua`): at the natural allocation
  owner of any partner-body mission (30010, 30020, 30050), after the ghost
  equipment groups returned, one public `TppEquip.RequestLoadToEquipMissionBlock`
  with the received manifest's base ids (this mission's store or the last one
  of the same link session; the query drains channel 2 once first). The
  sender sends from the title and the ACC too (early manifest, weapons3), so
  the first mission after connecting preloads; `no_manifest` (nothing spent)
  now means the peer sent none. Per id: a stock gun of its slot's type with
  a package row, of the manifest's own type of the slot (version 2:
  `dropped=<id>:type_mismatch` otherwise); base 0 is skipped; then the joint
  preflight (`equipment_preload_contract.hpp`)
  predicts `A03C40`/`A02290`/`DC3700` exactly: the hashes it appends, the
  queue room (no growth at `A03D2D`), and per id the cache path: the existing
  used entry gains only the protect bit, or the first unused entry is built;
  an id whose entry the viewer references (`cache_shared`) is dropped, and
  an eviction (`cache_evict`: no unused entry) drops ids from the end. The
  readback after the call requires the same owners and queue storage, the
  old prefix, exactly the new hashes, the built entries now used by their ids
  and every other entry byte-identical (nothing evicted).
- **W3 writer** (`equipment_stock_writer::ObserveGeneral`, the observer's
  `ObserveEquipWriter` on the natural mission owner, every ~100 ms): per
  slot, the two slots the remote does not hold first and its current slot
  last (flags 4 leaves body 1 on the last dispatched slot), one at a time:
  - plan: tier A1 the manifest's base id itself (its own protected,
    unreferenced cache entry, of the manifest's type of the slot:
    `type_mismatch` otherwise; never one of the viewer's own `vars.weapons`:
    `viewer_weapon`, `local_vars_unread` when they cannot be read); after ANY A1 refusal (`no_base` with a known
    type or alias, `type_mismatch`, `no_cache_entry`, `cache_shared`, ...;
    logged as `equip_writer=a1_refused; ...; next=c`) tier C (`substitute`
    only): the type is the manifest's type of the slot, else the base's table
    type, when it belongs to the slot, else only the slot class ({1,5,7},
    {4,6,8}, {2,3}); the search goes same type with the manifest's receiver
    (`recipe[0]`), then same type, then the rest of the slot class, the lowest
    id first (`NextCandidate`), and admits a candidate only with census flags
    3 and refs 0 (never an n3 entry such as the soldiers' rifle 39), as the
    id's first used entry, an id below 0x230 (never an alias such as the
    viewer's 871..876), not the want, not one of the viewer's own
    `vars.weapons`, not an equip this allocation already dispatched, and a
    full `Derive` + `Capture` pass (its packages loaded in this mission)
    whose fresh refs are still 0 (`stock_cache_in_use`: the census is the
    allocation's first snapshot; an entry referenced since is never picked).
    Each candidate logs `equip_writer=candidate; slot=..; want=..; equip=..;
    tier=c; pass=type_receiver|type|slot_class; type=..; receiver=..;
    flags=..; refs=..; reason=ok|<why>; shape=<w0..w5>|none` (at most 24 lines
    per allocation; at most 16 candidates per slot, then `candidate_limit`);
    tier D refuses with the LAST candidate's reason and names it
    (`candidates=n; candidate=<id>`; `no_resident` when none qualified); the
    slot stays empty;
  - readiness: the target's full preflight (the stock46 checks with its own
    data: packages ready in the mission entry, parts/optional assets, cache
    identity and refs 0, the new handle ring slot, the delayed resource map,
    all 64 banks, the render arena), the target slot's handle, metadata,
    binder record, effect record and attachments empty (a filled slot is
    never replaced), the other remote slots exactly what this allocation
    dispatched, at most 50 observations;
  - dispatch: the pins, a second preflight compared lease for lease, then
    the same SEH-contained ChangeEquip request as stock46 (instance 1,
    flags 4, stock/ammo 0) with the target's id;
  - readback: only the target slot's handle changed, to
    `(idx<<16)|(id<<5)|type`; the entry's refs +1; the new metadata row
    names the id and its cache entry; the slot's binder record has it current
    or pending; local equipment and every occupied cache entry unchanged; a
    bound handle maps an initially qualified resource with the target's
    render size. One attempt per slot per allocation; a re-realized body
    (epoch) ends the remaining slots. The viewer's weapon/ammo vars block
    (`vars+0x548..+0x670`) and shoot count (`vars+0x144`) are read directly
    before and after the call (`leak.ammo_leaks` only covers block 1's job):
    any change (or an unreadable read) is `call_unverified;
    reason=local_ammo_changed|local_vars_unread` and every remaining slot of
    the allocation refuses (`prior_local_change`);
  - nothing is spent while a gate is down: the pending slot's `reason` names
    the first failing gate (`gun_profile|setting_stock46|offline_branch|
    lab_inactive|saves_not_redirected|partner_off|code_gate|shutting_down|
    link_not_connected|mode|allocation_source|ghost_groups|terminating|
    dynamite_session|mission|mission_owner|body|engine_chain|census`), logged
    on change as `equip_writer=waiting; reason=..` (at most 8 lines per
    allocation). The dispatch order is fixed once a manifest is known (the
    remote's live v5 slot, else the manifest's `inv_slot`), never from a
    zeroed manifest.

### Release gating and play mode

- Runtime keys only (`partner_native_reload`, `partner_native_slot`,
  `partner_native_equip`). Not named in lab.ini, they follow the native gun
  profile (`ResolveWeaponDefaults`, the last step of `ParseLabConfig`, after
  any profile expansion): `event`/`write`/`substitute` with
  `partner_drive=native` and `partner_native_gun=1` (the worker gun stage,
  Play-OfflineCoop, play's `partner_native_profile=gun`), else
  `mirror`/`off`/`stock46`. Reload and slot are inert unless the native drive
  runs the gun stage (the arbiter active, an armed non-neutral window); `call`
  is downgraded to `write` in play (`Settings.play_mode`).
- No new CMake option: the generalized preload compiles with
  `MGS_LOCAL_STOCK46_PRELOAD_TEST` and the generalized writer with
  `MGS_LOCAL_STOCK46_WRITER_TEST`, so `tools/build.ps1 -ReleaseGun` (the three
  stock46 gun pieces ON) ships them unchanged. With `partner_native_equip`
  other than `stock46` the stock46 preload and writer stay off and the
  generalized pieces answer; with `stock46` it is the reverse.
- The follower and the mirror refuse with S_IS_ONLINE or `P+0x204` bit 18
  set, instance 1 as the local id, or any identity check failing; they never
  touch the viewer's own inventory (`leak.ammo_leaks` watches it). The
  follower reads the slot's live handle before the plan: a slot body 1 never
  had holds 0 (the stock46 preflight requires all seven zero), which, like
  `>> 16 == 0xFFFF`, is equip 0 and refuses `empty` (also the plan's tier).
- The game window closing (`PartnerShutdown`, `partner_defer::ShuttingDown`)
  ends the generalized preload and writer and the stock46 preload and writer
  (admission `shutting_down`), like the deferral and the drain windows.
- Play runs `partner_native_slot=call` as `write` from `Configure` and the
  trigger on (the telemetry shows what runs; `BuildArm` downgrades again).
  **One exception (WS2: the user asked for throws in play):** the guarded
  slot-3 type-12 support follower (`partner_support_equip::Follow`, native
  SetCurrentSlot `0x11AFA80` for slot 3, type 12) is the one native
  SetCurrentSlot allowed in play. It
  runs for any `partner_native_slot` except `off`, exactly as in the lab hidden
  runs, only with `partner_native_support` admitted (play: link v2 connected),
  the stock guard and the thrown owner filter armed, the full-body pins intact
  and its lease, row, handle and local-state checks passing
  (docs/anim-round2-throws.md "Throws in play"). Gun slots 0..2 stay data-only.
- The generalized pieces' admission (`EquipPiecesAdmitted`): the native gun
  profile with `manifest|substitute`, the offline-v1 branch on the redirected
  saves (`lab::SavesRedirected()`; play refuses to run without them), the
  partner and the code gate, and either the one-PC lab (`mode=lab`, lab.ini
  present) or two-PC play (`mode=play` with link v2 `connected`). Then the
  natural allocation/mission owner, any `SupportsPartnerBody` mission equal
  to the allocation and partner mission, this allocation's ghost-group
  receipt (authorized and returned), no termination or restore pending, no
  Dynamite session, the exact-build pins and every native preflight. The
  stock46 pieces keep their own gates (lab or play on the copied saves,
  30010 only). The preload's log line names the first failing admission
  (`equip_preload=refused; reason=disabled; admission=..`).

### Loadout mirroring

- **Sender** (pure reads, at most once per second): `vars.weapons[0..2]`
  (u16 at vars+0x548, registration `0x99B0BA`, `0x8070006` = u16[6]),
  `customizedWeapon[0..2]` (vars+0xB06) and `chimeraParts` (u8[36] at
  vars+0xB30), plus the type tables; the tables count only after the stock46
  sanity checks (type table vtable `0x22AD180`, packed 46 == `0x6244`, row
  393 == the stock46 recipe). Each slot carries its own gun type (1..8, 0
  unknown) and a base id that is never a non-gun id (weapons3; run 53e36944
  sent 343, a type-11 id whose row happened to hold receiver 69):
  - a stock id (below 0x230): itself when its packed type is a gun type
    (`scan=stock`, else `non_gun`, base 0);
  - a customized alias (0x367..0x36C): its type from the game's own alias
    path of the type getter `A2A3E0` (contract-pinned `0xA2A3E6..0xA2A432`,
    the getters `0x1E03790` and `0xA3C5B0`): the receiver r = u8 [alias table
    `0x2C934A0` + (id-0x367)*0xE + 0x12], then k = u8 [*(R+0x10) + r*6 - 2]
    and type = u8 [*(R+0x68) + k*3] & 0x1F of the equipment resource R =
    `0x2A711F0` (R+8 must be GunBasic); then the first type table index
    whose packed type is a gun type equal to the alias's and whose GunBasic
    row has the slot's receiver (`chimeraParts[i*12]`), mapped index -> id
    as `e < 0x230 ? e : e < 0x280 ? e + 0x1D0 : e + 0x380` (`A2A478`/`A2A48F`;
    `scan=gun_types`), else base 0 (`no_match`; the receiver's writer then
    substitutes by type). An id >= 0x400 is a valid base on the wire; the
    receiver's A1 refuses it (`equip_range`) and falls to tier C.
  The manifest (`partner_equip_wire`, channel 2, version 2: 64 bytes with
  `type[3]`; version 1 still decodes) gets a new revision on change and is
  resent after a new link session; one line per carried slot
  `partner_equip_base; rev=..; slot=..; alias=..; type=..; receiver=..;
  alias_receiver=..; base=..; scan=..; tables=..` and `partner_equip=sent;
  rev=..; epoch=..; base=..; alias=..; receiver=..; type=..; tables=..;
  mission=..`; the v4 frame names the revision (`equip_rev`).
- **Early manifest** (weapons3): the sender is driver-level
  (`partner_driver::TickEquipLink`): the driver tick in partner missions and
  the observer's ~100 ms path (`ObserveEquipLink`, from the debug snapshot,
  runtime thread, under the receipt lock) in every other mission, the title
  (40010) and the ACC included, whenever the link has a session and the copy
  drives natively or sends input frames (`partner_input_send=1`); a loadout
  without any weapon (vars not loaded yet) is not sent. Neither the sender
  nor the drain reads partner state (`ReadLoadout` has no partner-state
  gate). It stops when the game window closes, in any mission
  (`partner_window_closing`, set first thing by `PartnerShutdown`; the
  partner-mission flag `partner_defer::ShuttingDown` never gets set at the
  title): no observer link tick and no loadout read for the driver tick's
  sender. The same path drains channel 2 outside missions; the preload query
  drains it once more (`DrainEquipLink`), and every MissionStart seeds the
  mission's store from the session's cache (a manifest of another link
  session never), so the first mission after connecting preloads and plans
  from the remote's manifest without a resend. A 0.12.0 peer (manifest
  version 1 only) refuses the 64-byte version 2 (`size`, counted, nothing
  misread): both PCs must run the same package. A 0.12.0 sender's version 1
  still decodes; it restarts at revision 1 every mission, so the store
  counts only an exact repeat (revision, epoch and content) as a duplicate
  and the same revision with another loadout replaces the seeded manifest
  (review).
- **Receiver**: stores it (`partner_equip=manifest; ...; type=..; v=..;
  mission=..`) and logs the apply
  verdict for the back slot against instance 1's loaded equip
  (`partner_equip=apply; ...; result=already|unsupported|no_weapon|unknown;
  writer=stock46`). What body 1 can load is the generalized writer's business
  (above); replacing a filled slot is never done.

### Telemetry and logs

state.json `native` gains: `gun` (`fields_ok`, `cap`, `mag`, `stock`,
`class`, `b3d5`, `slot`, `noreload`, `b20`, `can_reload`, `gate`, and
`body_equip`), `fire` (`mode`, `installed`, `live`, `armed`, `effective`,
`refused`, `suppressed`, `passed`, `passed_player`, `owner_disarmed`,
`suppressed_per_s`, `census`), `mag` (`remote`, `remote_cap`, `local`,
`local_cap`, `writes`, `refused`, `fire_edges`, `reload_edges`, `cap`,
`written`, `last`, `faulted`, `leak_off`), `leak` (`shot_count` = the viewer's
`vars.playerShootCountInMission`, `shot_leaks` = drains whose job changed it,
`reload_drains`, `ammo_vars` = a digest of the viewer's weapon/ammo vars block
`vars+0x548..+0x670` (14 contiguous registrations, asserted by the checker),
`ammo_leaks` = drains whose job changed that block: instance 1's reload
completion runs the ammo setter `0x10589E0`, whose inventory component calls
are not statically attributed to an instance, so this is the runtime proof
that the partner's reload never touches the viewer's inventory) and `equip` (`tx_rev`, `tx_base`, `tx_alias`, `tables_ok`,
`rx_base`, `rx_alias`, `apply`); the ring gains `mag`. Log lines:
`partner_fire=installed|refused`, `partner_fire=live|masked; reason=..`, the
1 Hz `partner_fire; suppressed=..; ...` while counts change,
`partner_fire_census; ...`, `partner_native_gun; ...` with the record fields,
`partner_equip=sent|manifest|apply`.

Reload and switch (2026-10-09): `gun` also carries `mode` (rec+0x2C0), `b26`,
`upper_req`, `m3e4`/`m3e6`/`m3e8`, `hand_slot` (rec+0x3D9) and `slot_src`
(rec+0x3DE); `mag` adds `reload_seen`, `frozen`, `reconciles`, `m3e8_writes`;
`weapon` adds `gate` (match|mismatch|switching|projectile), `body_type` and
`switching`; new objects `reload` (`setting`, `state`, `last`, the remote's
v5 `remote_valid`/`remote_reloading`/`remote_mode`/`remote_seq`, `hold`,
`stuck`, `starts`, `taps`, `entered`, `completed`, `stuck_n`, `fallbacks`,
`body_entries`, `sample_strip`, `probe1` {`b20`, `b24`, `shots`,
`mode_changes`, `mode`}) and `slots` (`setting`, `remote_slot`, `remote_seq`,
`body_slot`, `hand_slot`, `target`, `read_ok`, `handles` [{equip, type} or
null] x3, `plan` x3, `ok_mask`, `writes`, `refused`, `last`, `faulted`,
`switching`); the ring adds `gmode`, `rmode`, `hslot` (null unread). Log
lines: `partner_reload=start|tap|retry|entered|complete|stuck|fallback|
tap_refused; setting=..; state=..; body_mode=..; remote_mode=..; seq=..;
mag=..; m3e8=..; taps=..; reason=..`, `partner_reload=rtap; queued=1`,
`partner_slot=plan; follow=..; body_slot=..; tiers=..; body=..; remote_base=..;
ok=..`, `partner_slot=write|refused; reason=..; target=..; ...`,
`partner_weapon=projectile|type_ok; ...`, `partner_input=fault; phase=slot`
(the follower off until MissionStart).

Weapons3: `reload` adds `completed_late`, `queued_taps`, `reconcile`,
`len_ms`, `len_max_ms`, `remote_len_ms`, `refill_ms`, `release`, `releases`; `equip` adds `tx_type`,
`rx_type`, `rx_version`; `slots.writer.slots[s]` adds `want_type`,
`candidates`, `candidate`. Log lines: `partner_reload=complete|complete_late;
... len_ms=..; remote_len_ms=..; refill_ms=..; ...; queued_tap=..`,
`partner_reload=queued_tap; ...; reconcile=1`, `partner_reload=release; ...`
(a lost exit after 20 s), `partner_equip_base; ...`,
`equip_writer=a1_refused; ...; next=c`, `equip_writer=candidate; ...`, and
every `equip_writer=` line carries `want_type`, `candidates`, `candidate`.

Generalized writer and preload: `slots.writer` (`active`, `dispatches`,
`bound`, `refused`, `slots` x3 {`want` the manifest's base id, `equip` the
target, `tier` a1|c|d|none, `family` optional|plain|none, `state`
idle|plan_wait|readiness|tracking|bound|queued|refused|unverified, `reason`,
`handle`}); `equip` adds `setting`, `resident_fields` and `resident` (the used
stock cache entries `[index, id, type, receiver, flags, refs]`, null before
the census) and `preload` {`ids`, `verdict`
authorized|refused|queued_verified|receipt_refused, `reason`, `new_hashes`}.
Log lines: `equip_preload=authorized|refused; reason=..; preflight=..;
mission=..; want=..; ids=..; dropped=<id>:<why>; actions=<id>:none|protect|build@<entry>;
new_hashes=..; queue=..; evict=0`, `equip_preload=queued_verified|receipt_refused;
readback=..`, `equip_resident; mission=..; used=..; entries=<index>:<id>/t<type>/r<receiver>/f<flags>/n<refs>,..`,
`equip_writer=plan|readiness_wait|handle_bound|handle_queued|observation_stopped|refused|call_unverified;
reason=..; mission=..; slot=..; want=..; equip=..; tier=..; family=..; type=..; cache=..;
expected_handle=..; result=..; mismatch_rva=..; dispatches=..; replace_filled=0; evict=0`,
`equip_writer_package=read_only; ...` and `equip_writer_optional=read_only; present=..; ...`
(once per slot's readiness).

### Proof (hidden worker; coordinator only)

`pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native-weapons.txt -PartnerProfile native -NativeStage gun -BuildProfile research`

The scenario header lists the evidence per section (aim, single shot, held
fire, reload aiming/holstered/automatic, the test tap, weapon switch 1/2/back
and while aiming, fire after each switch, crouched fire, both directions,
leak captures, loadout, census, noise record).

- **RA (reload A/B)**: the command above with `-LabSetting
  partner_native_reload=mirror`, then `=raw`, then the default (`event`).
  Pass (raw/event): the ring's `gmode` 11/12 near the shooter's reload, `mag.writes`
  flat while it is 11/12 (`mag.frozen` grows), `gmode` back to 8 (7/15/14
  when the shooter keeps aiming: `aim-hold-reload`) after body 1's own
  length (`partner_reload=complete; ...; len_ms=2950..3100`, `refill_ms`
  about 1560, `reload.completed == reload.entered`, `stuck_n` 0),
  `gun.mag == gun.cap` afterwards with at most one `mag.reconciles` (after
  the exit), `leak.ammo_leaks` 0; event
  also `reload.taps`/`reload.entered` per remote reload, `fallbacks` 0 and
  `auto-reload` entering 11/12 without an R press. mirror should reproduce the
  old fight (a write during 11/12, `partner_reload=stuck` or 8 of 9).
- **SB (generalized writer and native switch)**: the default run
  (`substitute`, `write`). Pass: per slot `equip_writer=plan` then
  `handle_bound` with its tier (a1 or c) and `slots.writer.slots[s].state`
  bound for ALL THREE slots (the `equip_writer=candidate` lines show the
  tier C choice; a tier d slot names its last candidate); `slots.handles`
  filled for 0, 1 and 2; `partner_equip=sent; ...; mission=40010` before
  the peer's `allocation_enter mission=30010` and
  `equip_preload=queued_verified` in the FIRST mission (the scenario grammar
  has no mission reload: a second-mission check needs another run); in
  `switch1`/`switch2` `partner_slot=write`, `slots.body_slot` ==
  `remote_slot` and `gun.hand_slot` following within one capture (ring `gmode`
  8 when it happens while aiming), `gun.body_equip` == the plan's equip,
  `weapon.gate=match`; `switch1-fire`/`switch2-fire`/`switch-back-fire`
  suppressed shots for bullet types; `switch2-reload` a reload with the
  secondary; the guest direction the same; `leak.ammo_leaks` 0 and the
  viewer's own slots unchanged. Variants: `-LabSetting partner_native_equip=manifest`
  (A1 only), `=stock46` (the SA control below).
- **SA (slot follower, stock46 only)**: `-LabSetting partner_native_equip=stock46`,
  then also `-LabSetting partner_native_slot=call`. Pass: `switch1`/`switch2` give
  `partner_slot=refused; reason=empty` (or `plan`), `slots.body_slot` stays 1,
  `gun.hand_slot` 1, no unarmed body, FIRE stripped (`weapon.gate=mismatch`);
  `switch3` -> `remote_slot` 1, no write (`same`), `weapon.gate=match`, and
  `switch-back-fire` shows suppressed shots.

## Configuration

lab.ini (`native/lab_config.cpp`, LabCommon key table):

| Key | Values (default first) |
|---|---|
| `partner_drive` | legacy, native |
| `partner_input_defer` | off, census, on (ignored unless drive=native) |
| `partner_input_local` | main, all |
| `partner_native_aim` | off, look, lookcam, looksrc (look plus the look source hook) |
| `partner_native_face` | idle, off, aim (the facing governor under the native drive) |
| `partner_native_noise` | filter, keep (the partner's shot noise on this machine; FIRE needs `filter` live) |
| `partner_native_gun` | 0, 1 (HOLD 0x8 + RELOAD 0x40) |
| `partner_native_fire` | 0 (off), visual (= 1: FIRE 0x10 only with gun and a live owner filter; never real partner bullets) |
| `partner_native_steer_deg` | 15 (0..30, 0 = no steering) |
| `partner_native_trim_pct` | 0 (0..10) |
| `partner_native_rates` | log, off, mirror (rates telemetry; mirror writes instance 1's rate record) |
| `partner_native_profile` | off, loco, gun (the qualified stage in one key; play's only way to the native drive) |
| `partner_native_net` | lab, online (play default; see "Online") |
| `partner_native_stall` | neutral, hold (play default; the last live stick up to `partner_native_stall_ms`) |
| `partner_native_stall_ms` | 400 (100..1000) |
| `partner_native_hold` | own, net (the sender's 12-frame words under the native drive) |
| `partner_input_edge_repeats` | 2 (2..8; play default 5; receivers accept up to 8) |
| `partner_native_aimpt` | off, look, point (the aim-target mirror: instance 1's network aim slot gets the remote's aim; gun stage and Play: point with qualified standing/crouched v6 direction and prone LOOK fallback; the debug command `aimpt` with off, look, point, lab or status) |
| `partner_native_reload` | mirror, raw, event (the partner's own reload: see "Weapons"; the gun stage and Play-OfflineCoop run event) |
| `partner_native_slot` | off, write, call (the slot follower; the gun stage and Play-OfflineCoop run write; play runs call as write) |
| `partner_native_equip` | stock46, manifest, substitute (what body 1 carries: see "Generalized preload and writer"; the gun stage and Play-OfflineCoop run substitute) |
| `partner_native_equip_shared` | off, on, prefer (gun profile: on; prefer is lab-only; fresh shape/bank/ref/ownership gates remain mandatory) |
| `partner_native_standin` | off, on (gun profile: on; closest already-filled class when the remote slot is unavailable) |
| `partner_native_equip_measured` | 0, 1 (lab-only optional-render experiment; always 0 outside lab) |
| `partner_native_cbox` | 0, 1 (F2: the remote's cardboard box on body 1 through ChangeEquip / SetCurrentItemIndex for instance 1, see "Cardboard box"; default 1 with the native gun profile in lab and play since hidden runs `05cffdd1` lab and `71e19d1e` play (-WanSim bad); play admits it only with link v2 connected; a named 0 opts out) |
| `partner_native_ride` | off, hide (driven native loco/gun stages default hide in lab/play; explicit off is preserved; mounted bodies are not rendered) |
| `partner_native_traverse` | 0, 1 (the ladder / pipe / wall-climb / step-on replay through body 1's action icon, see "Traversal" and "Traversal icon"; default 1 with the native gun profile in lab and play (user decision 2026-10-22; step-on proven in hidden runs 104e9a47 lab / 04d3511f play, the Mother Base ladder in 8f27806d lab / 2596f6ae play, pipe / wall climb unproven), otherwise 0; a named 0 opts out; a named value records `partner_native_traverse_given`) |
| `partner_ride_board` | off, block, passenger (the board hook at a vehicle the partner drives, see "Vehicle boarding"; default passenger with the native gun profile in lab and play (hidden runs 495e7c3a lab, de949f80 play; block: 0348c4d8, fad26e25), otherwise off; passenger includes the passenger camera, see "Passenger camera", and blocks at trucks and the rest; a named value runs as named and records `partner_ride_board_given`) |
| `partner_native_gesture` | 0, 1 (the remote's pickup / hand signs / knock on body 1's graph 2 and this copy's upper-act sender, see "Gestures"; default 1 with the native gun profile in lab and play since runs 0e343f72 lab / 75a7e7dc play, else 0; a named 0 opts out) |
| `partner_gesture_lever` | 0, 1 (lab only: the native debug command `gesture_lever <kind>` (kind `knock`, `sign_go` or `sign_stop`) makes this copy's OWN player knock or sign through the stock `KnockActionPluginImpl` request slot the call menu uses, for the gesture proof, see "Gestures", "Lab lever: knock and hand signs"; default 0 in lab and play, play forces a named 1 off; a named value records `partner_gesture_lever_given`) |
| `partner_native_mount` | off, hide (hide body 1 while the remote's act is a D-Horse / D-Walker act 121..126 and unrealize it through Lua's all-active mode, see "Mounts" and docs/mounts.md; acts only where `partner_native_ride` hides; default hide with the native gun profile in lab and play (hidden runs bfd6fcc4 / 2ab88411 lab, 5a044a4d play -WanSim bad), off otherwise; a named off opts out) |

`partner_native_reload`, `partner_native_slot` and `partner_native_equip` not
named in lab.ini follow the native gun profile: `event`, `write` and
`substitute` with `partner_drive=native` and `partner_native_gun=1` (so play's
`partner_native_profile=gun` gets them), otherwise the first value listed.

`partner_native_profile=loco|gun` expands, after the other keys, into exactly
the stage `Get-WorkerPartnerSettings` emits (lab_config `ApplyNativeProfile`;
lab_config_tests compares the fields with the raw stage text): drive native,
deferral on, send, hook, hold own, words hpr, wrap window, cam sandbox, local
main, recon track, slot body, rates mirror, noise filter, no P+0x218 window,
and mask 0x2240006 (loco) or 0x224005E with gun, visual fire, look, triggers
and right stick (gun). A key named in the same lab.ini keeps its value (the
`partner_native_aim=looksrc` A/B run). The weapons keys resolve last
(`ResolveWeaponDefaults`, after the expansion), so the gun profile, in play
too, runs `partner_native_reload=event`, `partner_native_slot=write` and
`partner_native_equip=substitute`. In play the raw native keys stay
refused (`kPlayRefusedNativeDrive` 0x20000: `partner_drive`,
`partner_input_defer`, `partner_native_aim=lookcam`, `partner_input_local=all`,
`partner_input_p218_allow`, `partner_native_noise=keep`; a refused key gets
the profile's value) and a profile without the dm=map + xform base is refused
(0x800000). In play a partner.trigger `drive=native|legacy` toggles the drive
inside an admitted profile (`partner_native=trigger_refused; reason=play_profile`
without one); `defer=` is always refused there
(`partner_native=trigger_refused; reason=play; key=defer`), and so are the
lab-only experiments lab.ini refuses there (`local=all`, `naim=lookcam`:
`reason=play; key=local|naim`); `ngun=1` and `nfire=visual` need the gun
profile (`reason=play_profile; key=ngun|nfire`), so the trigger only narrows
the profile; `nslot=call` runs as `write` in play. The trigger's other keys apply.
partner.trigger: `drive= defer= local= naim= ngun= nfire=0|off|1|visual steer=
nreload=mirror|raw|event nslot=off|write|call rtap=1` (the last also as the
worker debug command `partner rtap=1`).

To enable (lab only), on both copies: the animated partner settings plus
`partner_drive=native partner_input_send=1 partner_input_hook=1 partner_input_hold=own
partner_input_words=hpr partner_input_wrap=window partner_input_cam=sandbox
partner_input_local=main partner_recon=track partner_input_slot=body
partner_input_defer=census|on partner_input_mask=0x2240006 partner_native_aim=off`
(loco stage also `partner_native_rates=mirror`; gun stage: mask `0x224005E`,
`partner_native_gun=1 partner_native_fire=visual partner_native_aim=look
partner_input_triggers=1 partner_input_rstick=1 partner_native_rates=mirror
partner_native_reload=event partner_native_slot=write`;
the worker gate admits exactly these three stages, so a gun stage without fire
needs a gate change; the gun stage also admits `partner_native_aim=looksrc`,
which the coordinator selects with `-LabSetting partner_native_aim=looksrc`
(gun stage only; `-LabSetting partner_native_face=off|idle|aim` with any native
stage), and requires `partner_native_noise=filter`). `Get-WorkerPartnerSettings`
(tools/worker/WorkerSessionCommon.psm1) is the source of truth;
`Play-OfflineCoop.ps1` runs the same gun-stage settings on the animated
partner by default (`-NoNative` turns the native drive off).

## Cardboard box

F2 (0.13.5, unreleased): the remote's cardboard box on body 1 (instance 1),
native. Module `native/partner_cbox_equip.{hpp,cpp}`, pure policy
`native/include/mgscoop/partner_cbox_policy.hpp`, tests
`tests/partner_cbox_tests.cpp`, static contract in the research note
`re-cbox.md` (sections 3-6, 9, 12). Lab key `partner_native_cbox=0|1`:
**default 1 with the native gun profile in lab and play** (hidden runs
`05cffdd1` lab and `71e19d1e` play rehearsal, see Status below), off without
the gun profile; play admits it like the support lane, only with link v2
connected (`SupportModeReason`); a named value records
`partner_native_cbox_given` and a named 0 opts out.

**Why native.** The 0.13.3 Lua apply (`Player.ChangeEquip`, 0x9D0D60) ignores
`playerIndex` and passes `R8D = [P+0x214]`: it boxed the VIEWER's own player.
0.13.4 removed it and kept only the transport (manifest v2 bytes 10..11 and
byte 63 bit 0, `NoteCbox`).

**The two calls** (from `ObserveNativeCbox()`, the 10 Hz equipment tick right
after `ObserveNativeSupport()`, the same natural owner, gates and body lease):

- **on:** `ChangeEquip` 0x11AC2E0 `(ctl, &result, R8D = 1, {type 0x326C48DA30C0,
  equip = B, stock fields 0, flags = 4})`. For a non-local id and an item of
  type 0x11 it only writes body 1's item list (`row1+0x1C+2k` through
  SetEquipState, the per-(id, sub) record 8+k of the array `*(Q+0x160)`
  (`+ (sub + id*8)*0x10`; `Q+0x160` is a pointer), `Q+0x17C` bit 1) and
  calls `SetCurrentItemIndex(ctl, 1, k, 0)` (`row1+0x37` high nibble = k).
  Every viewer-side write in those functions is gated on `idx == [P+0x214]`.
  The engine's `CBoxActionPluginImpl::PollingImpl` then wakes the box on its
  own (act 77, `Q+0xD8[1]` = the box object): no detour, no input.
- **off:** `SetCurrentItemIndex` 0x11AF9A0 `(ctl, EDX = 1, R8D = 0, R9B = 0)`,
  only for a box this lane put on: sub 0 is always "no item", so state 2's
  can-wear check fails and the box exits (state 0xB). OnRemoveItem runs
  `UnsetSlot(ctl, 1, 4)` (body 1's item-slot handle may change; logged as
  `h4=before>after`).

**Choosing B.** The can-wear predicate (0x10DF430) takes body 1's OWN current
item but asks `GetItemStockCount` (ctl vt+0xC8, 0x11AD660), which scans the
VIEWER's item list (0x11AD67E `[P+0x214]`). So body 1 can only wear a box id
the viewer owns with stock > 0: `MapViewerBox` picks the remote's id W if the
viewer holds it in subs 1..7 with stock (`mapped=same`), else the viewer's
first box with stock (`mapped=viewer_box`: another camo), else refuses
`viewer_no_box` with no call. Body 1's sub: the one already holding B, else
the first free 1..7, else `body_items_full` (the pickup's replace-current path
is never used). B must pass `pp::EquipType(B) == 17`.

**Discipline** (`partner_support_equip`'s): SRW lock; every chain re-read
(player lease, offline, `[P+0x214] == 0`, body chain, the busy predicate
0x1156640, ctl vt+0x198/+0x68/+0x90/+0xC8/+0xB0/+0x20/+0x98, store
vt+0x68/+0x88/+0x10, the type table); the full SHA gate
(`equipment_stock_writer::PinsIntact`, which now covers SetCurrentItemIndex,
OnRemoveItem, OnEquipItem, GetItemStockCount and GetEquipIdSlot); a late
re-capture; the strict viewer digest around each call (row 0, its seven
handles, its item stocks `*(Y+0x98)+(0xE+k)*2`, `vars.items` +0x5A4,
`vars.currentItemIndex` +0x67C, the vars ammo block, its item records 0..7 at
`*(Q+0x160) + 0..0x7F` (the pointer itself is part of the lease), its act
and box word; never the `Q+0x17C` dirty mask, which both calls set for body 1
and the engine clears) and body 1's other slot handles (0..6 but 4); SEH around both
calls. Any surprise latches `uncertain` for the mission (no further call, not
even an off); `not_current` (ChangeEquip returned 0 but the row does not show
B) only ends the edge. The latches and the edge state are scoped to the
mission code and the driver's mission start (`Mission::start_ms`, so a
restarted mission with the same code starts clean); the box this lane put on
is carried across that reset (`partner_cbox=carried; id=..`): if body 1's row
survived the restart it is still ours, and it comes off when the remote is not
boxed (the engine would otherwise keep body 1 boxed while the viewer has
stock); a rebuilt row releases it with no call (`partner_cbox=released`). The
observer waits for the
driver's CFG (`step=partner_cfg`). One attempt per remote edge (`Mission::cbox_seq`); never
a second ChangeEquip while B is body 1's current item (it would reset the box
life record and run OnRemoveItem / OnEquipItem while boxed). Busy (transient)
while body 1 switches / reloads / throws, a gun writer slot is in readiness or
tracking, or the support lane latched uncertain: the box Wake allocates an
engine handle and OnRemoveItem touches body 1's slot-4 handle, which those
writers' digests compare.

**Watches** while our box is body 1's current item: `entered` (act 77/78
within 1500 ms, with the box word; on timeout the PS record bits `rec+8` bit 11
and `rec+0xC` bits 26..28, the viewer's stock and `rec_ok`), `exited` (the
engine took body 1 out of the box while it stays current), and the per-tick
viewer digest (`viewer_changed; fields=..`): a change in the viewer's item
rows, stocks, vars items or records while its act and its own selection stayed
the same latches `watch_latched` (no new on this mission; our box still comes
off). **Driver:** `NoteCbox` keeps the edge detection and logs `apply=native`
with the key; the reconciliation holds its far / vertical / runaway
placements while body 1 is in 77/78 for the remote's box and 3 s after the
remote's off (`partner_cbox=hold; on=1|0`): a placement would teleport the box.

Logs (one per decision):

- `partner_cbox=remote; id=..; active=0|1; apply=native|off; seq=..` (driver)
- `partner_cbox=mode; mode=lab|play|other; enabled=0|1; reason=ok|link_not_connected|mode`
- `partner_cbox=apply; id=B; want=W; mapped=same|viewer_box; via=native; sub=k; body_sub=k; result=..; reason=ok|..; act1=..; h4=..>..; uncertain=0|1; seq=..; mission=..`
- `partner_cbox=off; id=B; via=native; reason=ok|still_current|..; act1=..; h4=..>..; uncertain=..`
- `partner_cbox=refused; reason=viewer_no_box|type|body_items_full|full_body_gate|body_busy|gun_writer_active|support_uncertain|restriction|lease_changed|gates|uncertain_latched|viewer_changed_latched|<capture>; step=on|off|<gate>; ...`
- `partner_cbox=entered; act=77; ms=..; qd8=..` or `entered; ok=0; act=..; ms=..; rec8=0x..; recC=0x..; rec_ok=..; vstock=..; qd8=..`
- `partner_cbox=exited|viewer_changed|released|carried; ...`, `partner_cbox=hold; on=..; body_act=..; want=..; seq=..`

**Lab tooling.** `debug items` (read-only): `items;ids=..;counts=..;current=..`
(`vars.items`, `Player.GetAmmoStockBySlot(ITEM, i)`, `vars.currentItemIndex`).
`debug give_item cbox` (lab only: the native dispatcher refuses it outside
mode=lab with redirected saves): one stock `EQP_IT_CBox` into a FREE sub of the
LOCAL player's running item list (`Player.ChangeEquip toActive=false`), readback
on `vars.items`; a full list refuses `items_full`. Use it only if the census
shows a lab seed without a box. Risk: a checkpoint save writes the gift into
that run's copied save (never a real save), and the box model's assets may
not be resident for an item that was not in the sortie loadout.

**Scenarios.** `tools/worker/scenarios/partner-cbox-census.txt` (both
`run items`; both lab seeds' subs 1..7 hold 468, 451, 492, 483, 484, 472, 466
with stocks 8, 10, 54000, 10, 4, 8, 8, boxes 468 / 472 / 466: run `ea1abea5`),
then `partner-cbox.txt` (host boxes with `run slot
item 1`, the prone guest films `cbox-on`, a `cbox-move` burst, `cbox-off`,
`guest-self`; then the roles reversed). Hard requirement: no viewer
`partner_status=local; st=0x4` it did not press.

**Status (0.13.5): proven, on by default with the gun profile (lab and
play).** Hidden runs on `wp/integ9` (research build, lab seed, Closed,
BaselineRestored, RealSavesUnchanged, no visible fallback):

- `05cffdd1` lab, `partner-cbox.txt` (key named): both directions
  `partner_cbox=apply; id=468; want=468; mapped=same; ...; result=0;
  reason=ok; h4=00000000>00000000; uncertain=0`, `entered; act=77; ms=109|115;
  qd8=33806|33807`, `partner_act; edge=body; remote=77; body=77;
  match_ms=789|793`, `hold; on=1`, then `off; reason=ok` and body 1 77 -> 3,
  `hold; on=0`. The viewer's own `partner_status=local; st=0x4` and its act
  0x4D appear only in the half where that viewer pressed `slot item 1`; no
  `viewer_changed`, `refused`, `exited`, `uncertain=1`, writer latch or AV;
  both viewers' `run items` lines after the run equal the census. FRAMES: the
  box MODEL (the 468 camo) stands where the partner was on the viewer's screen
  (`cbox-on`, `rev-cbox-on`), slides away across the `cbox-move` bursts with
  the flaps of the moving box, is gone with the partner crouched beside it
  (`cbox-off`), and the viewer's own player stands unboxed (`guest-self`,
  `host-self`); the viewer's act stays 5 (prone) in every capture while it
  films the box (on, move, off).
- `71e19d1e` play rehearsal (`-Mode play -WanSim bad`, rtt ~180 ms, 1-3 %
  loss), the same scenario with the key NOT named: `partner_cbox=mode;
  mode=play; enabled=1; reason=ok` on both, then the same lines (ms=101|102)
  and frames both directions.

Notes: the remote's act stays 77 while it walks in the box (no 78 seen on
either side), and body 1 leaves the box 77 -> 3 directly while the remote
shows its exit act 61 for ~2.7 s (`edge=miss; remote=61`): cosmetic. The
prone viewer's "E + standing figure" prompt in some frames is the stock prone
prompt (also in 0.13.4 C4 runs without a box). Still open (not seen in these
runs): whether body 1's box damage changes the viewer's stock (watched by
`viewer_changed`), the consumers of `Q+0x17C` and `*(P+0x130)` vt+0x48, the PS
record bits' meaning, a viewer without the remote's box id (`viewer_box` /
`viewer_no_box`: both seeds hold 468), and pad cross-effects while boxed (the
remote's ACTION hold with an icon 0x31 reaches body 1's delivery-point
state).

## Online

Online play (2026-10-09, `wp/online-native`, fixture-tested; hidden runs
pending). Everything above runs unchanged in `mode=play`; what a real link
adds:

- **Sender**: input frames v4 (v5 once the peer sent v5, see "Wire") go out
  whenever `partner_input_send=1`, whatever this copy's own drive (TickOnline
  reads instance 0's gun record, rate record and slot and steps the reload/switch
  counters for a copy that shows clips; pure reads). Edge sets repeat
  `partner_input_edge_repeats` times (default 2, play 5); the wire `edge_rep`
  counter saturates at 2 (1, 2, 2, ...), so a 0.11.1 receiver, which refuses
  edge_rep > 2, still accepts every repeat (receivers dedupe by edge_seq).
- **Network policy** (`partner_native_net=online`, partner_native
  `OnlineParams`): input stale 1.5 s (`net_input`), link down 1.5 s
  (`net_link`), range hiding and unrealize leave as *network* reasons that
  never count toward the 3-per-mission latch; hard reasons keep it. A quiet
  remote (link up, its input and pose both stale: pause menu, loading) keeps
  the body active with a neutral pad (`reason=remote_quiet`) for up to 10 s,
  then `net_quiet`. Re-entry after a network leave: 2 s cooldown, then the
  link quality must hold 2 s (`partner_native_net::Quality`: a frame younger
  than 250 ms, no inter-arrival gap over 250 ms and a p95 under 120 ms in the
  last 2 s; `net_quality`), then the 2 s settle. More than 6 network leaves in
  5 min: `net_flap` until 30 s of good quality. A remote whose capabilities
  say it sends no input frames: `remote_caps`. The lab policy (default in the
  lab) is unchanged.
- **Stall hold** (`partner_native_stall=hold`): the native Arm's
  `config.stall_ms`; Compose keeps a late sample's stick and look (never
  analog triggers or edges; held buttons follow the 1 s grace) up to
  `partner_native_stall_ms`.
- **Gap snaps** (partner_recon, always on): the first hard snap during an
  input gap of 300 ms or more, or within 2 s after it, is catch-up
  (`gap_snaps`) and never counts toward `diverging`.
- **Hold variant** (`partner_native_hold=net`): the native drive's hold
  classifier answers from the sender's 12-frame words (NetHoldCode) instead of
  the receiver's frames, so a prone hold does not turn into a crouch tap at
  different frame rates. A/B in the two-PC test.
- **Capability exchange** (`partner_caps_wire`, 16 bytes, magic 0x43, reliable
  channel 2 in append mode; the manifest now appends too, so neither replaces
  the other's unsent message): features (input sender, native drive, gun,
  online, hold net, stall hold), profile, drive (off|clips|native), reason
  code (`partner_native_net::ReasonCode`, an append-only table), arbiter mode,
  policy; sent at every link session, on change at most 1/s and as a 5 s
  heartbeat. Logged `partner_caps=rx; drive=..; reason=..` on change.
- **Gun profile without a gun**: until body 1 shows a gun class this mission,
  HOLD, RELOAD and FIRE are stripped (`partner_weapon=absent; body_gun=absent`,
  `native.weapon.absent`, `native.weapon.strip`).
- **Play leak guard**: a shot or ammo leak of instance 1's job into the
  viewer's vars (`leak.shot_leaks`, `leak.ammo_leaks`, `mag.leak_off`;
  process totals) strips FIRE and RELOAD for the rest of the process in
  play (`partner_weapon=fire_reload_off; reason=shot_leak|ammo_leak`,
  `native.weapon.leak_strip` 80); the lab only counts.

Telemetry: state.json `native.net` (policy, stall, stall_ms, hold,
edge_repeats, quality_good, quality_good_ms, gaps_100/_250/_1000,
max_gap_ms, ia_p50_ms, ia_p95_ms, net_leaves, hard_leaves, net_reentries,
quiet_episodes, gap_snaps, remote_quiet, flap, last_net_reason, local_drive,
local_reason, caps {rx, sent, received, invalid, remote_drive,
remote_reason, remote_profile, remote_policy, remote_v4, refused});
link-status.json `partner_drive`, `partner_reason`, `remote_drive`,
`remote_reason`; log lines `partner_native_net; policy=..; drive=..; reason=..;
quality=..; ...; remote=<drive>/<reason>` (1 Hz while anything changed, play
or a configured native drive) and `partner_native_net=quiet|resume`.

No new executable address: the online policy, the capability message and the
stall hold are pure; the gun strip reads the already pinned gun record
(`ReadGun1`). `tools/generate_partner_contract.py --check` is unchanged.

## Wire

- Input frame v6 (120 bytes, payload 92): v5 plus signed pitch/yaw centidegrees
  at payload offsets 88..91 and gun_flags `AimDir` (0x20). These are the sender's
  checked standing/crouched gun direction, with class-specific inverse conversion.
  Camera-relative, sight and prone directions refuse; prone requires a separate
  unqualified plugin rotation, so it uses the camera-look fallback. The sender negotiates v6 per link session
  and sends a probe every two seconds; older input versions remain supported.
  No valid aim direction means zero appended fields and the existing look fallback.
- Input frame v5 (legacy 116 bytes, payload 88): v4 plus `[84]` gun flags
  (`GunFlag` valid 1, in hand 2, aiming 4, reloading 8, changing 0x10; without
  valid the next three bytes are 0), `[85]` the sender's gun mode `rec+0x2C0`,
  `[86]` `reload_seq`, `[87]` `switch_seq`; a v5 sender fills the v3 `inv_slot`
  byte with its current slot. A build without v5 refuses every 116-byte
  frame (all its input, not just the gun group), so the native sender
  negotiates: v4 frames until the peer was seen sending v5 in this link
  session (`partner_net::PeerSendsInputV5`, sticky until the receive reset of
  a new session or `ResetRemote`; like v4, whatever the sender's own drive, so
  a copy showing clips still feeds the peer's native reload and slot
  follower), plus one v5 probe every
  `kInputV5ProbeMs` (2 s) that lets a v5 peer learn ours (an older peer drops
  only the probe; the edge sets repeat in the next frames). Log
  `partner_input=v5_peer; probes=N` once per mission when the peer's v5
  arrived.
- Input frame v4 (112 bytes, payload 84; one version for both native work
  packages): v3 plus `[72..77]` the sender's dash, crawl and crawl factor
  rates (milli 500..2000 each, xflags `Rates` 0x10; zero without, always with
  `partner_native_rates=off`), `[78..81]` its magazine and capacity (xflags
  `Mag` 0x20; 1 <= cap <= 999, mag <= cap; zero without a readable gun) and
  `[82..83]` the sender's gun record equip (xflags `Equip` 0x80, 1..65535;
  zero without). xflags `Gamepad` 0x40: the sender's PlayerPad is in gamepad
  mode (`PP+0x488` bit 6 clear); absent means keyboard semantics. Sent whenever the sender sends input frames (any drive, since "Online"); v1..v3
  still decode, while the 104- and 108-byte single-feature drafts are refused
  (both copies must run the same build).
- Input frame v3 (100 bytes): v2 plus the sender's look angles, `CAM+0x270/+0x274`,
  the `PP+0x14` heading, xflags, inv slot and manifest revision. A receiver
  without v3 look refuses the look write with `look_refused=no_v3`.
- Equipment manifest (`partner_equip_wire`, reliable channel 2, one message
  type there besides the capability message): version 2 (weapons3, 64
  bytes) = version 1's 60 bytes plus `[60..62]` each slot's own gun type
  (0 unknown, else 1..8) and `[63]` 0; version 1 (60 bytes, types unknown)
  still decodes, any other size/version pairing is refused. The sender is
  native pure reads of vars and the type tables (see "Loadout mirroring");
  the receiver stores it, plans from it and logs the apply verdict.
- Gesture edge (`partner_gesture_policy`, reliable channel 2, append mode,
  only with `partner_native_gesture=1`; see "Gestures"): 12 bytes, `[0]` 0x47
  ('G'), `[1]` version 1, `[2]` kind (1 pickup, 2 hand sign Go, 3 hand sign
  Stop, 4 knock), `[3]` the sender's stance 0..3, `[4]` its speed 0..3, `[5]`
  flags (bit 0 moving), `[6..7]` seq (LE16, never 0), `[8..11]` sender_ms
  (LE32). Strict decode; the receiver keeps a strictly newer seq per link
  session. An older build has no classifier for 0x47: the message reaches its
  manifest decoder, which refuses it (`Result::magic`, the store's `invalid`
  counter only), and every other channel-2 message is unaffected. The v1
  action events (`partner_action_events`) are not touched: a new kind there
  would make an older `Valid` reject the whole frame.

## Telemetry

Worker `state.json` gains `"native"` (mode, reason, deferral with per-second
rates including `nested`, `foreign`, `unwound`, `drain_block` and
`drain_in_batch`, wrap census and look write, input, body parity,
reconciliation, gun, a 48-entry ring); `worker_debug_tests` checks the member
is valid JSON in every shape (no rates yet, a full ring, nulls, escaped text).
Log lines (`mgs-coop/script-observer.log`): `partner_defer=installed|refused|warn|fault`,
the 1 Hz `partner_defer; ...` and `partner_native; ...` lines, transitions
`partner_native=<mode>; reason=..`, `partner_native=enter; step=..`,
`partner_native_gun; ...`, `partner_equip=manifest; ...`.
Native actions add `native.recon.runaway_idle`, `lag`, `stop_grace`,
`dv_along_mmps`, `dv_cross_mmps` and `native.rates` (`setting`, `id0`/`id1`:
the records x1000 as crawl, crawl_factor, crawl_rate, dash, dash_factor,
dash_rate or null, `id1_read`, `remote` {valid, age_ms, dash, crawl,
crawl_factor}, `mirror` off|wait|ok|latched, `reason`, `writes`, `reapplied`,
`restores`, `drops`; latched reasons sp_vtable, rec_vtable, bounds, nonfinite,
range, fault, owner), and the log lines `partner_native_rates; ...` (on change),
`partner_recon=lag|lag_end` (their own 64-line cap, so a flapping lag never
starves the `runaway` lines); `partner_recon=runaway` carries dv_along/dv_cross.

## Runtime stages (coordinator, hidden worker only)

Scenario: `tools/worker/scenarios/partner-native.txt` (its header lists the
evidence per stage).

| Stage | Command | Pass criteria |
|---|---|---|
| S0 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native.txt -PartnerProfile native -NativeStage census` | mode fallback (defer_census); h0/h1/h2/wrap true; per_s batches ≈ 8/frame, w50_b1_s0 ≈ w50_b0_s0 ≈ 2/frame, ret_other 0, deferred 0, w50_subs 0; nested 0, foreign 0; batch_threads_seen 1 and thread_ok; svc28_b1 ≈ 7/frame; faults 0; the partner still animates via dm=map; both games close, saves match |
| S1 | same with `-NativeStage loco` (first run short) | mode active within ~6 s of gameplay; deferred == w50_b1_s0; windowed/drains ≥ 0.99; lost/torn/unwound/scope_unbalanced/wrap_in_batch/drain_block/drain_in_batch/nested/foreign 0; drain_us_max < 4000; body stance follows the remote; speed within 20 % while moving; recon snaps 0; fallbacks 0; leak captures clean |
| S2 | loco + `partner_input_local=all` (gate change) | only if S1 stance/actions fail |
| S3a | `-NativeStage gun -BuildProfile research` | gun bit18 and node active within 300 ms of aiming, aim_match; leak-aim clean; CamSub/dirty bits recorded; no host camera/zoom/HUD disturbance |
| S3a-r | as S3a with `-AmmoExperiment` (after its own qualification) | reload visible, no fallback |
| S3b | `partner-native-weapons.txt` with `-NativeStage gun -BuildProfile research` (gun stage = visual fire) | see "Weapons": suppressed shots, mirror writes, reload bit, no leaks, manifest/apply lines |
| F1 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native-facing.txt -PartnerProfile native -NativeStage gun -BuildProfile research`, then the same with `-LabSetting partner_native_aim=looksrc` | the scenario header: every face-* capture under 10 degrees; aim captures < 5 % of ring ticks over 10 degrees and no act 12 the remote does not show; look.err_mdeg < 1000 (looksrc: look.src installed, src_hits_s > 0); face-place-viewer-aims without act 11 and unwindowed_native 0; facing.contests <= 2; no fallback |
| O1 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native-online.txt -PartnerProfile native -NativeStage loco -SaveSeed lab` (A: lab policy), the same with `-LabSetting partner_native_net=online -LabSetting partner_native_stall=hold -LabSetting partner_input_edge_repeats=5` (B), then both with `-WanSim bad` and `-WanSim awful` | the scenario header: A latches after the third pause (today's defect); B never latches, `remote_quiet` during pauses, `net_quiet` after 12 s with `fallbacks` 0 and re-entry in ~6 s; under WanSim `hard_leaves` 0, sprint ratio 0.95..1.05, `err_mm` < 500 after stops |
| P1 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab -ScenarioPath tools/worker/scenarios/play-native.txt`, then with `-WanSim bad` | the scenario header: both early.log play lines with `refused=0x0; saves=redirected; rehearsal=1`, native active on both copies, `native.net.caps.remote_drive` native, pauses never latch, the game's own networking getter receipts unchanged, real saves unchanged |
| AIM1 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native-aim.txt -PartnerProfile native -NativeStage gun -BuildProfile research` (one-session A/B through `guest debug aimpt off`, then `lab`); then the same with `-LabSetting partner_native_aimpt=point` | the scenario header: aim-a reproduces the act-12 cycle with `aim.state` off, `pred1` 1 and `rel_yaw1_cdeg` beyond +-8000 at each act-12 start; every later aim capture after 300 ms: `aim.state` ok, `aim.slot` ours, refused 0, overwritten 0, `aimpt_err_mm` < 500, the wrapped difference of `aim_yaw1_cdeg` to the source role's `aim_yaw0_cdeg` within 500 and the pitch within 500 on >= 95 %, no act 12/15 the remote does not show, `face_err_cdeg` within 500 on >= 95 % of ring ticks after 600 ms |
| A1 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native-actions.txt -PartnerProfile native -NativeStage gun -BuildProfile research` (or `-NativeStage loco`) | the scenario header: sprint/crawl speed ratio 0.95..1.05 with `rates.mirror` ok and `rates.id1.dash == rates.remote.dash`; no runaway pin and no leaving through the sprint stop; the dive/evade/rolls: EVADE in the ring's held, body.act follows remote_act (27) within 2 captures, settles within 500 mm; leak-evade-host clean; ACTION never reaches the body |

## Known risks and open items

- Untested at runtime: deferral timing, main-thread cost (`drain_us_max`), a
  same-batch job waiting on block 1 (none seen statically), the Lua thread.
- Longer exposure of swapped PP/CAM fields (two drains per frame).
- Phase-0 consumers that run after PS still see the host-derived look for that
  frame (open item O7).
- Aim init side effects through `*(CAM+0x360)` vt+0x50 and the hash
  `0x5020E975DFD` sub-controller are not sandboxable (S3a decides).
- `aim_err_mdeg` and `body_equip` stay null: their readers are not qualified.
- Actions/rates (fixture-tested only): which act ids the dive, evade and rolls
  produce on the body (27 expected for EVADE presses); whether the provider
  difference is in the dash and crawl inputs only (`native.rates.id0` vs
  `id1` decides); a remote whose rates go stale (no v4 for 2 s) restores the
  body's own rates until they return; ACTION (vault, climb) is still masked;
  the six state-specific EVADE readers (see Actions) are census-pinned but
  their states are unverified, and `0x11D8FDD` may message another
  GameObject; the dive's collision effects on enemies (if any) happen in the
  receiver's world like any body contact.

- The ammo experiment (`MGS_LOCAL_STOCK46_AMMO_TEST`, research only) changes a
  qualified native call's arguments (stock 210 / ammo 30); the magazine mirror
  supersedes it (never combine them in a weapons run).
- Weapons (untested at runtime): projectile weapons are not filtered (body 1
  only carries stock46); the magazine mirror writes instance 1's own store row
  and record (a wrong rule shows as a refused or faulted mirror, never a write
  elsewhere); only stock46 can be loaded on body 1 (the manifest's other
  weapons log `result=unsupported`; the weapon gate masks FIRE/RELOAD then).
  Inferred, not proven: weapon 46 takes the trigger's history path (the ring
  fix is moot otherwise: `gun.f27c` bit 24 decides), the store row's type of
  instance 1's slot (a type outside 1..9 refuses the mirror), and that no
  thread other than the batch runs the src update while a drain holds the
  private rings (the existing window swaps src+0x48.. the same way). The noise
  filter drops every AddNoise record with owner 1 (also footsteps or other
  noise body 1 might make on this machine), which matches "each PC perceives
  for its own player". Instance 1's own completed reload runs the store
  setter with its notifier (game code, not ours): `leak.ammo_leaks` watches it.
- Generalized preload and writer (untested at runtime): the plain family
  (the NULL optional of ids above 0x7C) was never dispatched by us; its
  render size is derived from the bytes (`512 + align16(8g+88)`), not observed.
  The lower writer's slot-0 and slot-2 branches (`120A945` onwards) and the
  per-slot attachment/effect records never ran for instance 1 (pinned bytes,
  the same ChangeEquip ABI and request as stock46). Derived targets accept
  any parsed package count from 1 to the named count (stock46 keeps its
  measured [2,1,1,1]). The optional family admits the exe's constructor
  shapes n1..8 with m(n) (weapons3; only stock46's n4/m6 was observed: the
  other sizes are emulated from the table, never rendered by us). The
  preload may build a
  cache entry (`DC3470`, the path `TppRevenge` uses for mission weapons) but
  never evicts; a built entry is protected (flags 3) and stays for the
  mission. Tier C depends on what the mission's soldiers carry (per area,
  per mission); a substitute looks and handles like another gun of the same
  type. The bullet allow-list (types 1..6) comes from the EquipIdTable
  categories, not from the shot-class read (`0x13030B7` through the group's
  `+0x68` service, not pinned): an assault rifle's second mode (an underbarrel
  launcher, `[r14+0x7F]`) would be a projectile; its toggle is not in the
  native mask. Each allocation's dispatches take one ChangeEquip call per
  slot; a dispatch whose readback refuses (`call_unverified`) leaves whatever
  the game did (SEH-contained, no rollback), and the later slots then refuse
  (`other_slot_unexpected`).
- Weapons3 (fixture-tested only): the alias type path reads runtime-filled
  data (`0x2C934A0`, `*(0x2A711F0+0x10/+0x68)`); an unfilled table gives
  `scan=alias_type` (type 0, base 0: the receiver substitutes by slot class).
  A customized base id >= 0x400 is sent but never loaded (the receiver's A1
  refuses `equip_range`; tier C picks a resident gun of the type).
  Tier C can only use what the mission keeps resident with flags 3 and no
  reference (the soldiers' own rifle, n3, never qualifies): a mission
  without such a gun of the slot's class leaves the slot empty (tier d,
  the candidate named). The partner's reload keeps its own native length
  (about 3.02 s for stock46 at instance 1's rate 1.0, against the host
  shooter's 2.0 s): the reload-rate parity (F2) is not part of this build. A
  queued reload's reconcile writes the remote's magazine while the remote
  reloads (only while that tap is pending); a remote refill inside those
  250 ms can fill body 1 before its request (the retries then run out:
  `timeout`). The early manifest depends on the debug snapshot path
  (`debug_menu` enabled, as for the writer itself) for the title and the ACC.
- Facing (untested at runtime): whether the game keeps the governor's xform in
  idle acts (the old one-shot yaw rule did) and in aim acts (the contest guard
  latches if not); an idle turn starts only while the body's own yaw rate
  (readback minus our writes) is below 30 deg/s, so a pivot or turn animation
  the game still plays is waited out, never fought; a driver tick gap over
  250 ms restarts a turn from the readback instead of snapping onto the
  target; after the window-close shutdown (`partner_defer::ShuttingDown`) the
  governor's writes and the look source install are refused like in a batch; whether looksrc removes the act-12 cycle (O7 is a
  hypothesis: the A/B run decides); the look source patch is cross-modifying
  code installed only with no batch in flight (block 0's phase 0 runs on a
  worker inside a batch).
- Aim parity (untested at runtime): whether anything offline rewrites
  `Q+0x10[1]` inside the frame (`aim.overwritten` decides; the census says
  only two state-specific writers outside the Q methods); the sighted-remote
  turn threshold (27 vs 80 degrees) until body 1 can carry the remote's
  weapon; the bone path (`aim.path1`); `look` parallax for near aim points
  (`point` with an aim point on the wire would remove it); guards on the viewer's machine
  reacting to the partner aiming at them.
- No test asserts mechanically that no CodeGate caller runs after a lazy
  install; today CodeGate runs once at configure and `partner_upper_probe::Install`
  calls it there too, before any partner install.
- Fixture coverage of the windows themselves: `partner_defer_tests` installs
  the real input hook and the deferral on one synthetic image with a fake
  P/PP/src/CAM world and checks that block 0's job (on its worker) sees the
  host pad while block 1's deferred job sees the remote's (held word, HOLD
  latch, vtable copy) and everything is restored afterwards, plus the block,
  in-batch and replaced-sub-controller refusals. The game's real consumers
  of those fields are still only runtime-testable.
