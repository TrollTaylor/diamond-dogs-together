# Horse and D-Walker riders (F5, 0.13.5): research, decision, smallest viable design

Branch `wp/mount` from `wp/integ8` `228eecb`. Static research only (mgsvtpp.exe 1.0.15.4, SHA-256
`085c2f82...bb45`, Ghidra read-only and the `.text` dump); no game was launched. Addresses are absolute
(image base `0x140000000`).

**Decision.** There is no sound way to draw the partner on a horse or a D-Walker today, so the rider stays
hidden. The research found one gap in "hidden": the D-Walker rider was never hidden, because the sender reports it
as on foot. This branch adds a lab key that closes that gap (`partner_native_mount=hide`), lab
probes for the open runtime questions, and the scenario `tools/worker/scenarios/partner-mount.txt`. It draws
no mount.

**Runtime (0.13.5, section 9).** The first hidden runs showed that "hidden" was not hidden for any ride: clearing
the native realize bits leaves instance 1 realized under Lua's all-active mode, so the partner body stood idle at
the get-on spot for the whole horse or D-Walker ride. The mount hide now also unrealizes body 1 through Lua
(`CLEAR_ALLACTIVE`) for the length of a mount episode; with that, hidden run `bfd6fcc4` shows no partner body while
the partner rides either mount, and the key defaults to hide with the native gun profile in lab. Since the play
rehearsal `5a044a4d` (`-WanSim bad`, key not named) showed the D-Walker and D-Horse hides as well, it defaults to
hide in play too (the earlier play rehearsal `fbde3dcc` had shown the horse only).

**RIDE_BODY (user decision 2026-10-22, section 2).** The partner's RIDE_BODY stream (world op 42) no longer moves
the viewer's own D-Horse or D-Walker: a horse or walker pose applies only to a registered mirror of the partner's
mount, and 0.13.5 registers none. Vehicles (RIDE_BODY's final pose, RIDE_FAST, seats) are unchanged. Hidden runs
`2ab88411` (lab) and `5a044a4d` (play) confirm it: the viewer's own idle D-Horse stays where it stood while the
partner rides, and the partner's first E mounts its own horse (section 9).

## 1. The four questions

| Question | Answer |
|---|---|
| What does the viewer's copy hold while the partner rides? | Body 1 (TppPlayer2 instance 1) and **no GameObject for the partner's mount**. One buddy block per copy, declared count 1: the viewer has a horse or walker only if its *own* buddy is one. A horse rider is hidden (ride wire kind 3). A D-Walker rider is reported on foot (kind 0) and is not hidden. Section 2. |
| What does `coop-ride-horse.txt` do, and what does `partner_ride.lua` send for kind 3? | The scenario is the old offset-cargo test (the guest's own player warped onto the host's horse), not a rider display. `MgsCoopPartnerRide` returns 3 when `ON_HORSE` is set; the native sender puts kind 3 on the ride wire (v1, no seat or id); the receiver hides body 1 with `partner_ride=hide; reason=ride`. Section 3. |
| Can body 1 be anchored to a horse or walker through the DM anchor `0x1198770`? | Not as shipped: the anchor writes the seat matrix only for GameObject types 50 (vehicle, `CNP_ppos_c`) and 4; for 13 (horse) and 44 (walker) it writes nothing, so a DM with a horse locator plays at a stale matrix. A type-13/44 branch is possible, but it needs a mount GameObject on the viewer's copy to read. Without one, the only option is a rider pose without a mount, which floats. Section 4. |
| Which DM nodes exist, and do they play on body 1 offline? | `stateRideOnIdle` `f241f3bcf60b`, `stateTrot` `165ce362e5dd`, `stateCanter` `c56aea50d394`, `stateGallop` `ce7e8409eb61` and `stateRideWalkerWalk` `1b8b642289d5` are each present once in the retail `TppPlayer2_layers.mog` node pool. The rider plays the horse states in graph 0, synchronised to the live horse's own state. Whether they play on body 1 is a runtime question: lab probes 22..24 and scenario part 1. Section 5. |

## 2. What the viewer's copy holds

**Body 1.** The partner is TppPlayer2 instance 1, driven by the native puppet. While the partner rides, the
viewer's visibility policy (`partner_ride_wire::Observe`) decides from two inputs:
- the ride wire kind (channel 2), which the partner's Lua fills in;
- the remote act.

The heli acts 97..108 hide by act. Every non-foot kind hides with `reason=ride`. Nothing draws a mount.

**No mount GameObject.** The D-Horse and D-Walker buddies come from the buddy block (`TppBuddy2BlockController`):
- One global buddy type and request slot per copy. The retail variant-00 FPKDs declare one `TppHorse2` /
  `TppWalkerGear2` with totalCount 1 (docs/world-sync-protocol.md section 15).
- The ids are per-copy instance ids. `6656` (`13 << 9 | 0`) and `22528` (`44 << 9 | 0`) name **each copy's own**
  buddy on both machines.
- The lab saves deploy D-Dog, so the viewer's copy normally holds no horse or walker at all while the partner rides.

**RIDE_BODY and the viewer's own buddy.** World op 42 (`lua/coop/world_ride_pose.lua`) is live at world level 3,
the Play-OfflineCoop default. Until the 0.13.5 fix below it worked like this:
1. Each copy sends the census pose of its own realized horse or walker every 250 ms (`R.send`).
2. The other copy applied a pose to any id its own census realized and that was not locally occupied (`R.move`).
3. The horse was moved with `SetCallHorse` (start = pose, goal = 1 m along the heading); the walker with
   `SetPosition`.

So when **both** players deployed the same mount type, the viewer's own idle D-Horse or D-Walker was pulled along
the partner's mount path. The ids are per-copy instance ids, so `6656` / `22528` name each copy's **own** buddy: the
pose of the partner's horse moved the viewer's horse. It was the only "partner mount" on the viewer's copy, but it is
the viewer's buddy (the spec rules it out as the partner's mount), and `SetCallHorse` is a call (the horse runs to a
goal), not a placement. The hidden runs filmed it (section 9): the viewer's idle horse leapt and hung in the air
around the partner, and the partner's own horse was pulled toward the viewer's idle horse the same way (its first E
press found no horse to mount in all four runs).

**The fix (user decision 2026-10-22: turn this sync off).** A horse or walker pose now applies only to a dedicated
mirror of the partner's mount, never to the viewer's own buddy:
- `R.decide` / `R.admit` refuse a horse or walker id (6656, 6657, 22528, 22529) with `reason=not_mirror` unless the
  id is registered in `R.mirrors`. **Nothing registers a mirror in 0.13.5**: the shipped buddy archives realize one
  buddy per copy, and that body is the viewer's own. Index 0 (`6656` / `22528`, the copy's declared buddy) is never a
  mirror, even if named (`R.mirror`), as section 7 step 0 requires. A vehicle flag on a horse or walker id changes
  nothing.
- `R.move` and `R.place` refuse the same bodies again (defence in depth), so no code path in the file can command
  the viewer's horse or walker from a partner pose.
- One `world_lua=event=ride_body;action=refuse;reason=not_mirror;id=..;kind=..` line per refused body id; the
  handler counts every refusal in `R.refused[id]`.
- Unchanged: the sender still streams its own census poses (the wire and an old peer's view are unchanged), the
  vehicle path (the reliable final pose after a ride, `RIDE_FAST` / `RIDE_TILT` placement, the seats) and the body 1
  rider hide.
- `tests/world_ride_pose_lua_check.lua` pins it: no mirror registered; every horse / walker id refused by `decide`,
  `admit` (also vehicle-flagged), `move` and `place`; the end-to-end handler sends no `SetCallHorse` / `SetPosition`
  for 15 partner poses of all four ids and logs one line per id; a registered index-1 mirror is the only horse moved
  (index 0 never); a vehicle's final pose still places the vehicle.

**Two mounts per copy exist only in the private capacity archive.**
- What it is: the lab-only worker profile `buddy_capacity_two_v1`, built by `tools/stage_buddy_runtime.py` and
  admitted only with `MGS_LOCAL_BUDDY_CAPACITY_TEST`. It overrides the FPKDs to totalCount 2.
- What the hidden census runs showed (docs/STATUS.md, October 10-11):
  - two realized horses (6656, 6657) and two walkers (22528, 22529) on each copy;
  - in `ed3c1cd4`, each player natively mounted only its own assigned horse, and the peer's horse stayed natively
    empty on each copy.
- So "the partner's own mount on the viewer's copy" exists only as that empty second body, and only in the
  private profile. Nothing attaches a rider to it.

## 3. The sender today

**What `partner_ride.lua` sends.** `MgsCoopPartnerRide` (:11-28) asks only `ON_HELICOPTER` (1), `ON_HORSE` (3) and
`ON_VEHICLE` (2), and never returns 4 (walker). `partner_sync.lua` adds the result to the tick as ` ride=<kind>`.
The native `SendRide` copies it onto the ride wire unchanged.

**Which acts set which statuses.** PlayerInfo `0x1412CE870` fills the status words: `+0xC0` holds statuses 0..31,
`+0xC4` holds 32..63. Its act switch:
- **0x79 (121), horse ride:** `ON_HORSE` (25, `+0xC0` bit 25). `HORSE_IDLE`..`HORSE_GALLOP` (44..47, `+0xC4` bits
  12..15) come from the gait byte. `HORSE_STAND` / `HIDE_R` / `HIDE_L` (41..43) come from the stance byte 5/6/7.
- **0x7D (125), D-Walker drive:** no `ON_*`. While the walker attachment exists, it sets the same gait bits
  (`HORSE_IDLE`..`HORSE_GALLOP`), the walker gun bits (`+0xC0` bits 11/12, `GUN_FIRE` / `GUN_FIRE_SUPPRESSOR` by
  the `BindLuaValues` decode), and `+0xC4` bit 6 (status 38) and bit 23 (status 55). Neither 38 nor 55 has a Lua
  name: `BindLuaValues` `0x140554650` binds 48 `PlayerStatus` names, none for the walker, and the executable has
  no `ON_WALKER*` string.
- **0x6F (111):** the only act that sets `ON_VEHICLE`.

So a D-Walker rider is reported as `ride=0` (foot). Hidden run `f145f79f` (October 11, capacity profile) agrees:
after the host mounted walker 22529, both census lines still showed the peer's channel-2 ride kind as `remote0`
(docs/STATUS.md). The horse get-on / get-off act 122 is reported as foot too.

**Act census.** These are the word stores to `Q+0x18[idx]` (`B8 imm32` + `66 89 04`), in the player action code:

| Act | Owner | Writers |
|---|---|---|
| 121 (0x79) | horse ride | `0x1412445F0` (store `0x1412446D5`) |
| 122 (0x7A) | horse get on / off and turns | ten `RideHorse` states, `0x141234561`..`0x141236798` |
| 123 (0x7B) | D-Walker get on | `0x141266200` (store `0x141266399`) |
| 124 (0x7C) | none found | (kept in the D-Walker range by `AimBlockedAct` and the act log) |
| 125 (0x7D) | D-Walker drive | `RideWalkerGearActionImpl::StateDrive` `0x141260CC0` (store `0x1412617E4`) |
| 126 (0x7E) | D-Walker leave / fall | `0x1412604D0` (store `0x141260617`), `0x141260A3B`, `0x1412624EB`, `0x1412629FA` |

`partner_act_log.cpp` called 121 `vehicle_passenger?` and 122 `horse?`. They are now `horse_ride`, `horse_mount`,
`walker_mount`, `walker_drive` and `walker_off`, and 124 stays `walker?`. The vehicle passenger act is 0x6E (110)
(F1's research), not 121.

**`coop-ride-horse.txt`** is the awake "shared-horse transport" test of `lua/coop/world_horse_cargo.lua`:
- It is enabled only with `cargo_test 1` and disabled in normal play.
- The host (carrier) mounts D-Horse with E and sends its horse pose (world op 43).
- The guest holds R near the horse and becomes "cargo": its **own** local player is warped onto an offset of that
  pose (0.55 m back, 1.55 m up; :12).
- Its results name `attachment=offset`. It does not involve body 1 or the ride wire, and it is not a rider display.

## 4. The DM anchor `0x1198770`

Decompiled (read-only). The anchor reads the current entry's target id `rec+0xC0[cur]` and its connect point
`rec+0xD0[cur]`. It handles two types:
- **Type 50 (`TppVehicle2`):** vehicle service `(QuarkSystemTable+0x98)[0x55]` vt+0x18 by index. The CNP must be
  `CNP_ppos_c` (`0x2CCADA28E251`). Then `0xB7AF90(unit, out, 2)` writes the seat matrix to `rec+0x160..0x19F`. The
  seated vehicle rider uses this branch (docs/anim-round1-seat.md).
- **Type 4:** the type-4 manager (`vt+0x10(4)`). The CNP must be `0x5212FA82AF39`. Then `vt+0x20(index, &pos,
  &quat)` gives the position and quaternion, and the anchor composes a matrix from them.

Any other type, including 13 (`TppHorse2`) and 44 (`TppWalkerGear2`), writes nothing. Phase 6 still applies
`rec+0x160` while the entry has flag 2, so a DM targeted at a horse anchors body 1 at a stale (or zero) matrix.

**Can it be extended?** Yes, in principle: a detour of `0x1198770` (one call site, phase 0 `0x119980C`) could add
type 13/44 branches. A branch could build the matrix from either source:
- the census pose (horse positions at `kind+0x20 + i*16`, walker position array);
- the mount's own connect point (unknown CNP, and the walker attachment is
  `GetAttachmentCommunicationInfoFromGameObjectId` `0x140A840D0` for `0x58xx` ids).

But the branch needs a mount GameObject on the viewer's copy to read. On today's copies there is none (section
2), so the only possible rider is a rider pose with no mount: it floats, which the spec rejects.

## 5. Rider states

**The nodes exist.** Each StrCode64 below is present exactly once in the shared node pool of the retail
`TppPlayer2_layers.mog`:

| State | StrCode64 |
|---|---|
| `stateRideOnIdle` | `f241f3bcf60b` |
| `stateTrot` | `165ce362e5dd` |
| `stateCanter` | `c56aea50d394` |
| `stateGallop` | `ce7e8409eb61` |
| `stateRideWalkerWalk` | `1b8b642289d5` |

The check used the investigators' `foxhash.sc64` (validated: `stateGunHold` = `aa0ea22a5e0e`) and
`tools/partner_overlay_mogcheck.py` (pool=1 each). Unlike the earlier overlay states, their absence from the SQ-4
graph 2/3 node list was **not** checked (`mog-nodes-2_3.json` was not available; docs/anim-round1-actions.md 4.3).

**The horse rider follows the live horse.** `RideHorseActionPluginImpl`'s phase handler `0x141236940` keeps a
`0x2B0` record per player. Its `+0x18` points at the horse's communication record: state at `+0x58`, rider id at
`+0x7C`, command at `+0x82`, transform at `+0xE0`. In phase 9 it:
- requests the rider node through `0x141230B30(ctl, idx, 0, node)`, which is controller vt+0x190 in **graph 0**,
  usually the horse's own current state (`+0x58`);
- sets `stateRideOnIdle` when the gait is idle;
- then runs `UpdateAnimationSynchronization`.

`stateTrot`, `stateCanter` and `stateGallop` are also the horse's own state names: they appear in the `TppHorse2`
code at `0x140E58F7F`..`0x140E674E1` and in the rider code at `0x14122F91A`..`0x14122FCC3`. Without the horse's
communication record the plugin cannot run on body 1.

**The D-Walker drive node has no name.** `StateDrive` enter (act 125) requests `vt+0x170(ctl, idx, 0,
0x23B9A991BFB)` (graph 0). Its damage branch uses `0xDC3F105036D5` / `0x138A349D0212`. None of the three matches
any name tried, and `stateRideWalkerWalk` has no code reference: it is in the pool only. The Lua DM binding takes a
name, so the unnamed drive node cannot be requested through it.

**Do they play on body 1 offline?** That needs a frame. This branch appends three looping overlay probes
(`partner_dm_overlay.cpp` 22..24 and `partner_sync.lua` `OVERLAY`): `stateRideOnIdle`, `stateGallop` and
`stateRideWalkerWalk`. They are debug only (`partner_native_overlay=debug`, `overlay_state <name> 1 <ms>`) and no
act maps to them. Scenario part 1 plays each on the guest's body 1 while the host stands. Expected, if a node
plays: `partner_overlay=playing; ...; code_match=1` and a seated rider pose with nothing under it. If it does not
play: `finished` at once, or the idle pose.

## 6. What this branch adds

- **`partner_native_mount=off|hide`.**
  - Defaults (0.13.5 runtime, section 9): hide with the native gun profile in lab and play, off otherwise; a named
    value records `partner_native_mount_given` (a named off opts out).
  - Where the key is wired: `lab_config` (`ParseMountKey`, kept out of the main chain, which is at MSVC's nesting
    limit), the three `-LabSetting` regex copies and the help, `LabCommon`, `worker_session_tests`,
    `lab_config_tests`, `lab_ini_check`, `friend_package_tests`, and the docs/native-puppet.md table and "Mounts"
    section.
  - What it does: with `hide`, `partner_ride_wire::Observe` treats a valid remote act 121..126 like the heli acts
    (`partner_mount_policy::Hide`). Body 1 is hidden with `reason=ride`, a mount act is never a usable vehicle
    seat, and the partner returns after 500 ms of calm on-foot samples.
  - Unrealize (added in the runtime phase, `partner_mount_policy::Unrealize`): while a mount episode hides body 1
    and Lua reports all-active mode, the hidden reply carries `CLEAR_ALLACTIVE` (op 8, the existing stage-0 op);
    Lua's `Player.SetAllInstanceActiveMode(false)` unrealizes instance 1. The show path's stage policy sends
    `SET_ALLACTIVE` again after the placement. Line `partner_mount=unrealize; mount=..; act=..; episode=..;
    op=clear_allactive`, once per episode.
  - Scope: it acts only where `partner_native_ride` is not off. It changes nothing on the wire, so an old peer
    is unaffected.
  - Lines (driver tick, capped): `partner_mount=configured`, `partner_mount=hide; mount=..; act=..; kind=..;
    ride_live=..; episode=..`, `partner_mount=unrealize; ...` and `partner_mount=end; mount=..; act=..; ms=..;
    kind=..`. A missing act sample keeps the episode open (`partner_mount_policy::Holds`).
- **Overlay probes 22..24** (section 5).
- **Lab levers.**
  - `walker_spawn` / `walker_status` (`lua/coop/debug_buddy.lua`): the horse lever's stock `CallBuddy`, with
    `BuddyType.WALKER_GEAR`, 4 m to the right. An active buddy block only, never a mount (mount with E). The horse
    lever's guards and dog yield:
    - it refuses with `player_mounted` while the local player rides. The check is one `PlayerInfo.OrCheckStatus`
      over `ON_HORSE`, `ON_VEHICLE` and `HORSE_IDLE`..`HORSE_GALLOP` (44..47). PlayerInfo sets the gait statuses
      for the horse ride 0x79 and for the D-Walker drive 0x7D (while the walker attachment exists), and the drive
      sets no `ON_*`. `walker_status` reports the same posture as `localMounted`;
    - it reads the vector back and re-samples the context and the walker right before the call.

    The get-on / leave acts 122, 123 and 126 set none of these statuses, and Lua has no local act read, so
    `partner-mount.txt` requires `localMounted=0` before each buddy switch that follows a ride. CallBuddyInit `0x140A44740` itself
    checks only its pending flags and the type mask, not whether the local player rides.
  - `goto_walker` (`lua/coop/debug_player.lua`): 2 m beside walker 22528, facing it (stock `GetPosition`).
- **Act names** (section 3).
- **Tests.**
  - `partner_mount_tests` (new, in CMake and `Get-WorkerTests`): the act census, the hide decision, the episode
    lines, and the visibility with the key (walker kind foot, horse kind, seat mode, heli).
  - `partner_driver_tests` `TestMountVisibility`: off draws the walker act as today; hide hides it with one
    `partner_mount=hide` line, holds across a missing sample, and ends on a foot act.
  - `lab_config_tests`, `partner_act_log_tests`, `partner_dm_overlay_tests`, `partner_sync_lua_check` and
    `debug_buddy_lua_check` cover the key, the names, the probes and the levers.
- **Contract.** No game code site, vtable slot or pinned body is involved: the hide is pure policy over existing
  reads, and the levers use stock Lua APIs. `generate_partner_contract.py --check` is unchanged at 677 / 149.

## 7. Smallest viable design for a drawn mount (not built)

Every step needs its own hidden-worker FRAME before the next step starts.

0. **A mount the viewer may use.** Ship the capacity-two buddy archive (or an equivalent second realized
   `TppHorse2` / `TppWalkerGear2` instance) outside the private worker profile. Census proof exists for allocation
   and realization only, and the memory budget is unmeasured. On each copy, index 0 is the local player's buddy
   and index 1 is the partner's mount mirror; `world_buddy_ride.lua` `R.assign` already pairs host 0 / guest 1.
   RIDE_BODY must then map the remote's own index 0 to the local index 1 and **never** move the local index 0.
   Since 0.13.5 RIDE_BODY refuses every horse / walker pose unless its id is registered in `R.mirrors`, and index 0
   can never be registered (section 2); this step registers index 1 and adds the id mapping (today a pose applies
   to the same id on the viewer's copy, which for a mirror would be the wrong body).
1. **Mount transform.** Stream the partner's mount pose like `RIDE_FAST` (20 Hz, 100 ms render delay; today
   `W.handlers[FAST]` drops horse and walker ids) and place the mirror every frame:
   - walker: `SetPosition{pos, rotY}`, which is in the verified `TppWalkerGear2` dispatcher;
   - horse: no verified per-frame placement exists. `SetCallHorse` is an AI call, so the horse would need a native
     placement (unknown, research first).
2. **Rider.**
   - Add a type-13/44 branch to the anchor `0x1198770`, as a pinned detour like the seat dwords, returning the
     mirror's saddle or cockpit matrix.
   - Play a DM on body 1 targeted at the mirror. Choose the state from the partner's gait: add `HORSE_IDLE`..
     `HORSE_GALLOP` (statuses 44..47, set for horse and walker) to the sender's report. Horse: `stateRideOnIdle` /
     `Trot` / `Canter` / `Gallop`.
   - The walker's drive node `0x23B9A991BFB` has no name for the Lua DM binding. It needs either a native request
     by code or proof that `stateRideWalkerWalk` plays.
3. **Mount animation.** A placed mirror slides with its own idle motion. Its gait needs the horse's state
   (`comm+0x58`), which has no known write API. This is the largest open item; without it the rider bobs on a
   sliding horse.
4. **Higher-fidelity alternative.** Body 1 natively mounts the mirror through the stock ride (`RideHorse` plugin
   for idx 1, the per-player ride component already sized for count 2), driven by the replayed pad. This needs the
   ACTION start for instance 1 (the F3 icon path), and the horse would then follow body 1's pad, corrected by recon.
   It is larger and unproven.

Until step 0 ships, the rider stays hidden. `partner_native_mount=hide` makes that true for D-Walker too (FRAMES:
`bfd6fcc4` / `2ab88411` lab, `5a044a4d` play).

## 8. Hidden-run checks for the runtime phase (`partner-mount.txt`)

The scenario order changed on 2026-10-22 to suit a play rehearsal (the runs in section 9 used the earlier order:
probes, horse, both D-Horse, D-Walker, baseline). The D-Walker part now runs first, so a horse problem can no longer
cost the D-Walker evidence; a horse dismount E is pressed only after `host require localMounted=1`, so no E press can
mount the host's horse by mistake (what stopped `fbde3dcc`); the lab-only overlay probes are last, in a marked Part
5. The header says how to make the play copy (drop the mount / overlay `labsetting` lines and Part 5, then run with
`-Mode play -WanSim bad`); `worker_session_tests` validates that copy in play mode and pins the order and the guard.

1. **Part 1 (walker, guest hide):** `walker_status ... actor0=22528`, and the host mounted with E. On the guest:
   ride kind stays 0, `partner_mount=hide; mount=walker; act=123|125; kind=0`, and **no body 1 in
   `walker-riding` / `walker-drive`**. In play this was the FRAME that lifted the play force-off (`5a044a4d`).
2. **Part 2 (horse, guest hide):** `walker_status ... localMounted=0` first (the Part 1 dismount finished), then
   `horse_spawn`. `partner_ride=rx; kind=3` and `partner_ride=hide; reason=ride`. Does
   `partner_mount=hide; mount=horse; act=122` come before the kind-3 packet? No body 1 near the host in the riding
   frames. After the dismount: `partner_mount=end`, then `foot_stable`.
3. **Part 3 (both D-Horse, RIDE_BODY):** since the section 2 fix, the guest's own horse must stay where CallBuddy
   put it (`horse-viewer-own-spawned`, the `horse-viewer-own` / `horse-viewer-own-ride` bursts and
   `horse-viewer-own-after` from the same spot; `horse-viewer-own-host` beside the host shows no guest horse), the
   host's first E must mount its own horse (`host require localMounted=1`), and each copy logs
   `world_lua=event=ride_body;action=refuse;reason=not_mirror;id=6656;kind=1` once (the observer prints it with `; `
   separators; a copy that saw the partner's D-Walker also logs `id=22528; kind=2` once). Proven in `2ab88411` lab
   and `5a044a4d` play.
4. **Part 4 (baseline, host off):** what the host draws for the guest's D-Walker ride today. The host keeps its
   D-Horse from Parts 2-3, so it has no walker of its own (and RIDE_BODY would no longer move one). Expected: body 1
   on foot at the cockpit position, falling, or recon placements, with no walker under it. Note any
   `partner_ride=hide; reason=air`, `partner_recon=place` or runaway lines. In the play copy no key is named, so
   the host runs the play default (hide) and Part 4 is the reverse hide instead (`5a044a4d`).
5. **Part 5 (probes, lab only):** for each probe, `code_match` and `played_ms`, and the frame. Does a rider pose
   appear on body 1, and does it float?
6. **Every part:** no `partner_status=local` with `ON_HORSE` / `ON_VEHICLE` on the copy that did not mount, and no
   `crash_probe=av` other than the known teardown `rva=0x21e72`.

## 9. Hidden runs (0.13.5 runtime, `wp/integ9`)

Research build, gun stage, lab seed; every run Closed, BaselineRestored, RealSavesUnchanged, no visible fallback,
no `crash_probe=av`, no fault or uncertain line; the guest's own act stayed 1 and its `partner_status=local` stayed
`st=0x0` in every capture. Part numbers for the first four runs are the earlier order (1 probes, 2 horse, 3 both
D-Horse, 4 D-Walker, 5 baseline); `2ab88411` and `5a044a4d` ran the current order (section 8).

| Run | Mode | What it showed |
| --- | --- | --- |
| `b264ad58` | lab, key named on the guest | Lines as expected (`partner_mount=hide; mount=horse; act=121; kind=3`, `mount=walker; act=123; kind=0`, `end`, `foot_stable`). Frames useless for the hide: a rock and the viewer's own model covered the host. Part 3: the guest's own idle horse leaping and hanging in the air around the host; the host could not mount its own horse. Part 5 baseline: body 1 walks on foot along the guest's D-Walker path, no walker under it. |
| `251abcef` | lab, vantage fixes (scratch copy) | **The hide did not hide.** Clear frames: from the get-on to the dismount the partner body stood idle at the get-on spot, turned to the camera, no name tag (`partner_bits=cleared; reason=ride`, probe still `rbits=0x3; parts=3,3`), for both D-Walker rides and the horse ride (the horse's kind-3 hide too). Part 1 probes: `stateRideOnIdle` / `stateGallop` drop body 1 into a seated / hunched rider pose at ground level with nothing under it (`code_match=0` although the pose changed); `stateRideWalkerWalk` shows the standing pose. |
| `bfd6fcc4` | lab, with the unrealize | **Proven.** `partner_mount=unrealize` and `allactive_cleared` at each episode (probe `pool=00,ff; rbits=0x1`), `allactive_set` and `partner_realized=1` after each dismount. Frames: the host standing (`walker-before`, `walker2-before`, `horse-before`), then no partner body anywhere while it mounts, rides, drives and turns (`walker-mounting`, `walker-riding`, `walker-drive`, `walker-turn`, `walker2-*`, `horse-riding`, `horse-ride`), the body back at the dismount spot with its look (`walker-off`, `walker2-off`, `horse-off`). The host's own view shows it in the walker / on the horse. |
| `fbde3dcc` | play rehearsal, `-WanSim bad` (rtt ~180 ms, 1-2 % loss), key not named (the then-tested play default) | Horse part as in `bfd6fcc4` (frames, play HUD visible). Part 3's last E press mounted the host's horse (its first E found none), so `host require localMounted=0` stopped the run before the D-Walker part: **the D-Walker is unproven in play**, so play stays off. |
| `2ab88411` | lab, the reordered `partner-mount.txt` as shipped, on the RIDE_BODY fix (head `4f9396e` plus the play-default change below) | **Passes, every require and expect.** Part 1/2: as `bfd6fcc4` (two walker and two horse episodes, each `hide`, `unrealize`, `allactive_cleared`, then `end`, `allactive_set`, `partner_realized=1`); FRAMES `walker-before` / `walker2-before` / `horse-before` show the host, the mounting, riding, drive and turn frames show no partner body, `walker-off` / `walker2-off` / `horse-off` show it back. **Part 3 (RIDE_BODY off): the guest's own idle D-Horse stands beside the guest, on the ground, in every frame from `horse-viewer-own-spawned` through the `horse-viewer-own` / `-ride` bursts to `horse-viewer-own-after`; `horse-viewer-own-host` shows it standing at a distance, no horse in the air around the host. The host's first E mounted its own horse** (`host require localMounted=1` passed, `arrival=mounted`; host frame `horse-viewer-own-after` shows it riding). Each copy logged `world_lua=event=ride_body; action=refuse; reason=not_mirror` once per id (guest 22528 and 6656, host 6656 and 22528). Part 4 baseline as before (body 1 on foot along the guest's walker path). |
| `5a044a4d` | play rehearsal, `-Mode play -WanSim bad` (rtt 175-187 ms, loss 0-4 %, one 13 % sample at the link start), the play copy the header describes (no key named on either copy), on a build whose play default is hide | **Passes, every require.** Both copies: `partner_mount=configured; mode=hide` with the key not named. Guest (host rides): two walker episodes (acts 125, 123) and two horse episodes (121), each `unrealize` / `allactive_cleared` then `end` / `allactive_set` / `partner_realized=1`. Host (Part 4, the guest rides its D-Walker): one walker episode, the same lines. FRAMES with the play HUD: the D-Walker (two vantages) and the D-Horse hidden on the guest's screen while the host rides, the body back after each dismount; Part 4 the same on the host's screen (`base-walker-riding` / `-drive`: no partner body; `base-walker-off`: back). Part 3 as in `2ab88411` (own horse stays, the host's first E mounts its own horse). The viewer's own status changed only for its own rides. After each episode the native drive left to clips for a moment (`partner_native_net; ... reason=net_quality; ... max_gap=..`, the hidden ride is a sample gap) and re-entered; `fbde3dcc` shows the same. |

Defaults after the runs: hide with the native gun profile in lab and play (the play force-off is removed after
`5a044a4d`); off without the gun profile; a named off opts out.

RIDE_BODY (user decision 2026-10-22, section 2) no longer moves the viewer's own horse or walker. `2ab88411` and
`5a044a4d` confirm it: the Part 3 behaviour of the first four runs (the viewer's horse in the air, the partner's first
E finding no horse) is gone.

## 10. Shared D-Horse (horse plan M1, lab key `world_horse_share`, on with the gun profile)

The viewer's copy now holds a body for the partner's horse: its own D-Horse (index 0, 6656) is moved under the rider.
- **Wire.** The rider's copy streams its census pose (`MGS_COOP_HORSE_DRIVE_V1 read`) in RIDE_FAST (op 44, kind 3, id
  6656, 20 Hz) and one final reliable RIDE_BODY (op 42, kind 1, rider 65534) at the dismount
  (`lua/coop/world_horse_share.lua`). A copy without the key drops 6656 from RIDE_FAST and refuses RIDE_BODY
  `not_mirror`, as before.
- **Placement.** The viewer renders the track 100 ms behind and places its 6656 every frame through
  `MGS_COOP_HORSE_DRIVE_V1 place` (`native/horse_drive.cpp`): only the pose part of the D-Horse call-enter
  (`0x140E6AA90`, state 2 enter): census pose[0] = x, y, z, yaw, and the CS transform write
  `(receiver+0x110[slot / receiver+0xF0])+0x10` (CS vtable `0x1422E6070`) `vt+0xA8` (`0x140AF35F0`) with the
  call-enter's own matrix (rows (cos,0,-sin,0) (0,1,0,0) (sin,0,cos,0) (x,y,z,1)). It never sets `rec+0x2E3/+0x2E4`
  (no call run), and refuses while this copy's player rides the horse (`rec+0x2DD`: the rider beats the owner) or a
  call is pending. Contract: `tools/generate_horse_drive_contract.py` (4 sites, 4 slots). The census reads back about
  0.3 m off in xz and 0.2-0.45 m above each write (the unridden census over the ridden one; P1), steadily.
- **The rider.** With the horse placed under 500 ms ago, the ride wire's live ON_HORSE (or act 121) shows body 1
  (`partner_ride_wire::HorseRider`, Visibility::horse) instead of the mount hide; the DM overlay's horse map
  (`partner_dm_overlay` In::horse, MapHorse) seats it in `stateRideOnIdle` for the whole ride, and the fallback xform
  keeps it on the rider's stream. Once body 1 is realized and loaded the horse is anchored under it
  (`horse_drive::HorseUnderRider`, the P2 seat 0.10 m forward / 0.49 m up; only within 1.5 m of the stream), so the rider
  sits in the saddle on this screen whatever the two streams' delays are.
- **Handoff.** Within 4 m of a horse the partner rides (2.5 m, free past 2.8 m, with section 12's `world_horse_pillion`, on
  with the gun profile since M3c, where E there claims the rear seat), an ACTION-only pad mask (`MgsCoopHorseHold`) stops a second
  driver (probe P5); it goes with the final pose, and the first E mounts the horse where it was left. Both players
  riding at once (`step=two_drivers`, a same-moment E race) places nothing and ends the partner's ride on this copy; no
  new track opens until this copy's player is off (Lua check only). A census generation change, a stale track (1.5 s)
  or a context change ends the ride. Travel / Bring partner here and the guest's checkpoint wait while anyone rides it
  (`checkpoint_wait; reason=horse`). On this copy a hit by the horse the partner rides counts as `mirror`.
- **Proof.** `tools/worker/scenarios/horse-shared.txt` (lab) and `horse-shared-play.txt` (play): runs `3b2f0955`
  (lab) and `9d98fa2c` (play, `-WanSim bad`), both copies, both role orders.
- **Not yet.** The guest loading the host's horse from the seeds (D-H1) runs only with `companion_host_owned` (the
  module's hold gains HORSE); without it each copy's own D-Horse stands in. No gait on the rider, no type-13 DM anchor,
  no horse damage protection on the viewer's copy (E19-horse not observed), no D-Walker.

## 11. Downed partner on the horse (horse plan M2, lab key `world_horse_cargo`, off unless named)

`lua/coop/world_horse_cargo.lua` (it replaces the offset prototype; op 43 is retired) puts a carried downed partner on
the shared D-Horse of section 10. It reaches the carry only through the existing `MGS_COOP_CARGO_V2` calls.
- **Load.** Carrying the downed partner (CarryRole 1), within 2.5 m of the horse, hold E 450 ms; beyond 1.2 m of the
  horse's root the carrier must face it (run `20aef564` refused a carrier 0.85 m away facing off). A guard on the horse
  (`IsCarriedHostage`) refuses `npc_cargo`; far / facing / busy / awake refuse too. A guest carrier asks the host with
  HORSE_SEAT (`world-sync-protocol.md` section 32, seat 2) against the victim's downed generation; the host grants while
  that generation is down and not being revived (else `awake`, `generation`, `mid_revive`, `busy`) and loads for itself
  without a claim. The carrier's copy plays the stock put-down (`RequestLocalPutDown`), then `SetCargoHold(1)` and
  LOADED with its horse pose.
- **Bodies.** Carrier's copy: body 1 warped every tick 0.44 m behind and 0.45 m over the unridden census root (kept 0.8-2.0
  m over the carrier's ground within 3 m, else 1.35 m over it; M2f, was 0.55 m and 1.0-2.0 / 1.45 m: run `5a92c73f` drew it
  0.38 m high after a dismount), or 0.54 m behind and 0.15 m over the rider while this copy rides (M2g, was 0.4), a
  quarter turn from the horse's yaw so it lies across the back (M2g: at the horse's yaw it lay along it over the saddle),
  ahead by the rider's last per-tick move (at most 0.6 m). Victim's copy: after
  the replica put-down, `SetCargoLocal(1)`; its own 6656 is placed on the carrier's pose when unridden (KEEP at 1 Hz,
  re-placed past 0.5 m; for 2 s after M1's track ends only on a KEEP pose sent after those 2 s, M2f); body 0 goes 0.9 m over its census, and while M1 places the horse from the rider's stream, right
  after each placement 1.3 m over the pose just written (`W.horseCargo.placed`, ahead by the horse's last move), a
  quarter turn from the yaw; its target never drops faster than 1 m/s. world_downed reports the held posture (as the
  cardboard box) while body 0 is cargo, so the native downed controls take no prone request (round 1's
  `controls_timeout_prone`; held in every round 2 run). Since the BODY0 round body 0 (victim) and body 1 (carrier)
  move by `hold0` / `hold1` with reset 14 (below); the lab lever `horse_probe cargo_pose <placed_mm> <census_mm>
  <back_mm> <down_mmps> <reset>` picks hold0's reset 0..15 or the Lua Warp (4).
- **hold0** (`hold0 <mission> <epoch> <x> <y> <z> <yaw> <reset>`, world_horse_share and world_horse_cargo): body 0's
  `T.vt+0x70` then `T.vt+0x68`, as the partner driver's xform mover moves body 1 (the partner contract's pinned T slots
  0x24c8ef0 +0x70 / +0x68), then Warp's resets on request (bit 1 `C0.vt+0x90`, bit 0 `A0.vt+0x80`, only for objects of
  body 1's class and slot). Static [O capstone]: the CS `vt+0xA8` write of horse_drive is the same `T.vt+0x68` call
  (0x140B6ECF0, Ghidra comment at 0x140B6F027); the A0 reset (0x1410C3010) ors 0x21 into the posture word
  `*(PS+0x298)[id]`, the C0 reset (0x14128B3E0) sets bit 0x20 of +0x2C0 in a 0x580-byte per-id record. Observed: the
  Lua Warp draws body 0 lying on the horse but it spends a second or more at a time on the ground under the moving horse
  (victim_pin lag window median 59-109 cm, up to 102-137 cm); hold0 with reset 0 or 1 keeps it closer (windows mostly
  6-9 cm, median 8, but 74-80 cm windows in every hold0 run) but in the airborne pose, on the victim's screen and on the
  carrier's (the partner stream carries the pose). Reset 2 (C0 alone): the BODY0 round below.
- **Body 0 / body 1 hold (BODY0 round).** world_horse_cargo now holds the victim's own body 0 by `hold0` and the
  carrier's body 1 by `hold1` (`hold1 <mission> <epoch> <x> <y> <z> <yaw> <reset>`, hold0 for body 1, also while that
  copy's player rides), both with reset 14 = the C0 reset (2) + a write watch (4) + a pin (8):
  - the C0 reset keeps the lying pose: `C0.vt+0x90` (0x14128B3E0) sets bit 0x20 of the 0x580-byte per-id record's +0x2C0,
    and its only reader `C0.vt+0xD0` (0x14128E010) clears it and answers "no fall" for that frame [O static]; without it
    (no reset, A0) the body plays the airborne pose; the A0 reset re-applies the stance and drops the body;
  - with the C0 reset alone (run `cfd2b604`) a later write of the same frame still dropped body 0 1.4-2.9 m under the
    moving horse. `native/horse_body_watch` (no code patch) sets 4-byte hardware write watches on body T's centre y
    (T+0x24) and foot y (T+0x14) on every thread of the process (a watcher thread arms them and disarms 500 ms after the
    last hold); a first vectored handler records each writer (`horse_drive=watch` / `watch_rip` every 2 s: rva, thread,
    order after the hold, stack return rvas) and, with the pin, puts T+0x20 / T+0x10 / T+0x174 back to the hold's target,
    only within 300 ms of a hold and only while T+0 still holds the body T vtable 0x24c8ef0. The writers [O run
    `911ce293`]: the hold's own T.vt+0x58 (0x1AD566C / 0x1AD5684, order 0 / 1), then the player's physics update after
    the script tick, T's update 0x141AD2900 writing the foot and the centre at 0x141AD1F5E / 0x141AD1F7E (order 2 / 3),
    called at 0x140B6C3CE (Ghidra comments there). The contract pins T.vt+0x58 (horse_drive 5 sites, 5 slots);
  - `free0 <mission> <epoch>` ends the watch and pin before every take-down or lay-down Warp; the Lua Warp is the
    fallback only when the native has no hold (off / request / pins); a transient refusal (body, at a mount) is left to
    the pin (one Warp's A0 reset left the victim prone for a whole ride in `144d99bb`). The M1 ride hold is never set
    for this copy's own cargo (it did not change the crouch below).
  - Lab levers: `horse_probe cargo_pose <placed> <census> <back> <down> <reset 0..15>` (4 the Lua Warp) and
    `pillion_pose <back> <rise> <mode 0|2|3|4|5|6> ...` (3 hold0 + C0, 6 hold0 + C0 + watch + pin).
- **Take-down.** On foot within reach, R (the carry key) or E held 0.3 s lays the body on the ground beside the horse at
  the carrier's side on both copies; the coop pickup shoulders them at about 0.5 s of R (run `a49c4d76` lost a 0.5 s
  take-down to it), and a pickup straight off the horse sends the ground point so the victim's copy lays body 0 there
  first. After a load the take-down waits until E and R have been up once (the loading E held on took them down again in
  `a49c4d76`). E at the flank is the stock mount.
- **Automatic lay-down** beside the horse: `horse_lost` (census generation, unreadable 1.5 s), `call_away` (a pending
  call, or the unridden horse 3 m from where it stood; for 2 s after a dismount the stand point follows the horse: run
  `883dc6ab` called away a horse still slowing from a gallop), `awake`, `victim_gone` (a new downed generation; the
  victim's copy lays body 0 down only while it is within 5 m of the horse), `stream_lost` (no KEEP or status 3.5 s,
  unpaired), `context` (mission, epoch or role). Travel / Bring partner here and the guest's checkpoint wait while loaded
  (world_horse_share's `busy()`).
- **Stance (M2f).** The victim's copy reads body 0's stance (PlayerStatus CRAWL / SQUAT / STAND; PlayerStance STAND 1 /
  SQUAT 2 / CRAWL 3 = the native stance byte, Ghidra 0x1409D3F25) every cargo tick and logs each change (`step=stance;
  s=..; was=..; src=placed|census; tracked=0|1; off_ms=..` = ms since M1's track ended); a SQUAT or STAND body held as cargo
  is asked back to CRAWL with the stock `Player.RequestToSetTargetStance` at most every 500 ms (`step=relie; from=..; n=..;
  ok=..`). `cargo_status` and `victim_pin` carry the stance; the lab lever `cargo_relie 0|1` (debug_rules.lua) turns the
  request off (0 only logs). world_horse_share hands the final (unanchored) placement of the partner's dismount to
  `W.horseCargo.placed` too.
- **Log lines** (`world_lua=event=horse_cargo; ...`): `step=claim / grant / deny / loading / loaded; seat=2 / cargo; seat=2 /
  take_down / unloaded; seat=2; why=..; side=carrier|cargo; placed=0|1 / refuse; why=.. / hold0_refused; reason=..`,
  `step=pin` (how, mounted, census / player / body heights) and `step=victim_pin` (want / got height, `lag_cm` / `drop_cm`
  the worst of the window against the last target, `src=placed|census`, `via=hold0|warp`) every 2 s; native
  `horse_drive=held; n=..; body=0` (M2g: `horse_drive=held_alone` when body 0 is held with body 1's cached class).
  world_horse_share's `step=final_sent` carries the ridden `path_cm`. Debug `horse_flank` (debug_rules.lua, lab and
  play, M2g) puts this player 1.6 m beside the local horse's census root on their side, facing it.
- **Hidden runs, round 1** (`7359e4d2`, `c667a209`, `6fd054c4`, `83ac3950` lab; `d26aaf70`, `547b431e` play): load, the
  first E mounting with the body on, the ride and the dismount keeping them on; the victim's controls timed out and the
  guest-carrier order, the take-down, the revive and the link drop were never reached.
- **Hidden runs, round 2** (`horse-cargo.txt` + `horse-cargo-drop.txt` lab, `horse-cargo-play.txt` play `-WanSim bad`,
  `-SaveSeed afgh-ledge`; slim archives `%USERPROFILE%\mgs-coop-run-archive\horse\<run>`), every claim on both logs
  and frames looked at:
  - `d2a5cafa` (lab) passed every step: host carrier and guest carrier (claim, host grant, load), the first E mounting,
    the ride, the dismount keeping them on, the 0.3 s R take-down at the same point on both copies, the revive, and the
    link drop (`why=stream_lost` on the host, `why=context` on the guest, both laid down). `c2927350` and `7f3155c8`
    (play `-WanSim bad`) passed every step in both orders; `7f3155c8` ran with world_horse_cargo not named (on through
    the gun profile for that session). Some waitlogs read from the session or logmark start and matched earlier lines
    (`7f3155c8` (m1): the guest's final_sent and the host's placed; `d2a5cafa` (b): the host's placed); the real lines
    are in the logs. `20aef564`, `883dc6ab` (play) and `a49c4d76`, `a242d867`, `8cfe8e97` (lab) each
    failed one step (facing, call_away after a gallop, the pickup race, a slope timing out the downed controls before
    the pickup, the guest carrier's E refused `horse_lost`); each but the last has its fix in.
  - Frames: with the Lua Warp the carrier's screen shows the body across the horse's back at the load, walk, trot,
    gallop and after the dismount in `a49c4d76`, but in `d2a5cafa` it shows the body on the ground beside or under the
    ridden horse (`host-h2-mounted`, `host-h2-ride-walk-01/02`, `guest-h2b-mounted`, `guest-h2b-ride-walk-01`); after the
    take-down it is on the ground with the revive prompt. On the victim's own screen the body is across the back at the
    load and the stop and part of each ride, and on the ground under or beside the moving horse for a second or more at a
    time (Lua Warp), or with the horse but in the airborne pose (hold0). In `7f3155c8` (hold0, A0 reset) the victim's copy
    also showed the horse lying on its side mid-gallop (`guest-h2-ride-gallop3-victim-01..03`).
  - **Gate not passed:** the downed player's own view of their body on the horse.
- **Hidden runs, BODY0 round** (the same scenarios, `-SaveSeed afgh-ledge`; slim archives
  `%USERPROFILE%\mgs-coop-run-archive\horse\<run>`), every claim on both logs and frames looked at:
  - `cfd2b604` (play `-WanSim bad`, hold0 + C0): the victim keeps the lying pose on their own screen, but lies on the
    ground under or hangs beside the horse at a gallop (victim_pin drop 136-288 cm); the carrier's screen low too.
  - `911ce293` (lab) / `dd0cd5d7` (play), hold0 + C0 + watch + pin: every scripted step in both orders (load, first E,
    ride, dismount kept, the R take-down at the same x,z on both copies, revive; link drop laid down on both, 12 cm
    apart); victim_pin lag 0 / drop 0 in 42 / 32 windows; the victim's own screen shows them lying across the horse at
    load, mount, walk, trot, gallop and the stop in both orders. Not yet: the carrier's screen (body 1 by the Lua Warp:
    low behind the horse at a trot or gallop), and after the dismount a 100 ms pin left a gap (drop 202-246 cm) and the
    victim's copy's horse fell Unconscious about 0.18 s later.
  - `144d99bb` (lab) / `7117d3ac` (play), hold1 and a 300 ms pin, world_horse_cargo not named (on through the gun
    profile for that session): every scripted step in both orders; no drop window; the carrier's screen keeps the body
    across the horse behind the rider at walk, trot and gallop and across it after the dismount, in both orders; the
    victim's own screen lying on it through every gait, but crouched (squatting) on the horse once the carrier has got
    off, in both orders. Unconscious only during the scenario's goto_horse / goto_carry teleports in `144d99bb`, none in
    `7117d3ac`. Ride paths lab 35 / 152 m, play 48 / 18 m.
  - `47101768` (lab) / `4585cc5a` (play), no ride hold for the cargo and the Warp fallback only without a hold: play
    passed every step in both orders with rides of 164 / 84 m, the take-downs at the same x,z on both copies, the
    revives, no Unconscious line and no hold refusal; lying on both screens at walk, trot and gallop in both orders;
    after the dismount the carrier's screen keeps the body across the horse, the victim's own still crouches on it.
    The lab session's (a) passed (38 m) but a placebehind for (b) dropped the guest through the ground (y 211), so (b)
    and the link drop did not run in this session (they passed in `144d99bb`).
  - M3 check `7c0b9e71` (lab, world_horse_pillion named, `pillion_pose` mode 6): every seat step; seat_pin lag 0 / dy 0
    (was dy 99-176 cm), no Unconscious line; the passenger squats on the horse behind the driver on their own screen at
    walk, trot, gallop and the stop, both orders; the host-driven stall remains (`step=drive; moved_cm=0`).
  - **Gate not passed:** on the downed player's own screen the body crouches on the horse after the dismount (with
    or without the ride hold), so world_horse_cargo stays off unless named. Next: log body 0's action and posture words
    through the dismount on the victim's copy (what changes there: M1's final placement, body 1 getting off, the target
    switching from the placed pose + 1.3 m to the census + 0.9 m) and hold the placed height while unridden; or the
    D-Horse cargo slot (route i, which the user now allows) for the engine's own carried pose. Done in the M2f round below.
- **Hidden runs, M2f round** (`horse-cargo.txt` + `horse-cargo-drop.txt` lab, `horse-cargo-play.txt` play `-WanSim bad`,
  `-SaveSeed afgh-ledge`, world_horse_cargo on with the gun profile and not named; slim archives
  `%USERPROFILE%\mgs-coop-run-archive\horse\<run>`), claims on both logs, frames looked at:
  - **Cause** [O logs `5a92c73f`, `131f88c6`, and the BODY0 run `4585cc5a`'s full guest log]: on the victim's copy body 0
    lies in prone_idle (coop_carry=watch `a=5`) from the replica put-down through the whole ride; at M1's final placement
    (the carrier's dismount) it goes into fall / land (`a=19` / `1a`, every frame), and at `partner_mount=show_end` (body 1
    shown on foot) it ends in crouch_idle (`a=3`) = the squat of every BODY0 run; `step=stance s=SQUAT` 17 / 33 ms after
    the final. In the same tick the victim's copy re-placed its horse on the last KEEP pose, sent up to 1 s before the
    final: 10-11 m back along the ride (`step=horse_placed` 1 ms after the final), then three more 1 Hz jumps while the
    carrier's horse slowed; in `131f88c6` that horse fell Unconscious 0.25 s after the first jump (`m=Unconscious
    id=6656`) [O]. That the jump starts the fall is [I] (handing the final pose to the cargo, `8e33e2d5`, did not stop
    it); the 2 s settle that removes the jump (`cad4da2f`) never reached a dismount in a run [U].
  - **The fix on one body** [O logs + frames, both drop parts, host carrying the guest]: with `cargo_relie 0` the victim's
    own screen shows them squatting on the horse 4 s after the dismount (`guest-h2c-stays`), `cargo_status` stance=SQUAT;
    `cargo_relie 1` -> one `step=relie from=SQUAT n=1 ok=1` -> prone_idle (`a=5`), stance CRAWL, and 2.5 s later the frame
    shows them lying prone across the horse (`guest-h2c-relie`). Rides 86 / 72 m (`final_sent path_cm`); the link drop laid
    them down on both copies (host `why=stream_lost`, guest `why=context`, placed=1). Review correction: the frames show
    it on the horse in `5a92c73f` only; `131f88c6`'s two guest frames face a rock wall with no horse in view (that copy's
    horse had gone Unconscious 4 s earlier), so `131f88c6` backs the squat-then-CRAWL pair by its logs only. The
    dismount itself ran with the lever at 0; the lever went to 1 after the squat.
  - Attempts: 1 `5a92c73f` lab (part 1: the host's R found the downed guest, `coop_carry=search ids=1`, and never
    shouldered them) / `d4aec0a2` play (stopped at (a)'s gallop6: a host capture refused, `sequence_expiry_or_busy`);
    2 `131f88c6` lab (part 1: a placebehind's viewer landing saw no new position sample) / `05758736` play (a guest capture
    refused at the walk); 3 `770368d0` lab (part 1: a host capture refused at gallop6; part 2 then found the host still
    mounted, `position result: on_foot`) / `016ec854` play (a guest placebehind's landing, before the load). Until they
    stopped [O]: the load, the first E mounting, the victim's stance CRAWL in every window through walk / trot / gallop
    (play `d4aec0a2` 17, lab `770368d0` 15), the victim's own screen prone across the horse at load / walk / trot / gallop,
    the carrier's screen across the rump behind the rider at walk / trot / gallop. Review correction: at the LOAD the
    carrier's screen (`d4aec0a2` / `770368d0` `host-h2-loaded`) shows body 1 lying along the horse's length, above the
    saddle with the legs in the air, not across it. How many of the queue's refusal results are this round's was not
    checked [U]; cause [U] (no native change in this round).
  - **Gate not passed** (three attempts used): never completed in a session this round: the guest-carrier order, the
    take-down and revive, the play rehearsal with the cargo, a dismount with `cargo_relie 1` from the start (the request
    during the fall / land loop), the settle guard live. world_horse_cargo is off unless named again (`06ba9917`).
  - **Next:** the same three scenarios on this tree once the queue's capture / landing refusals are understood (look at
    `sequence_expiry_or_busy` in `native/worker_scenario.cpp` Poll: a capture sent while a `hold ... nowait` runs, or
    a frame stall of the game; and the 250 ms landing sample in `Invoke-WorkerScenario.ps1` Wait-ViewerLanding); first
    check on the victim's log that the 2 s settle removes the jump and the fall (`coop_carry=watch` stays `a=5` through
    the dismount, no `step=relie`); if the fall stays, keep body 0 held with its own pose through M1's final, or the cargo
    slot. Done in the M2g round below.
- **Hidden runs, M2g round** (lab `horse-cargo.txt` (a) + `horse-cargo-b.txt` (b) + `horse-cargo-drop.txt` (c), play
  `horse-cargo-play.txt` (a) + `horse-cargo-play-b.txt` (b); attempt 2 sent parts 2+ back to the boot spot, attempt 3
  starts them at the horse; `-WanSim bad`, `-SaveSeed afgh-ledge`, world_horse_cargo named; slim
  archives `%USERPROFILE%\mgs-coop-run-archive\horse\<run>`), claims on both logs, frames looked at:
  - **Carrier's screen** (df9c16ce / 965fe3d5): hold1 held body 1 at the horse's yaw and hold0 body 0 a quarter turn from
    it, through the same quaternion (0, sin(yaw/2), 0, cos(yaw/2)) [O code]; so on the carrier's screen body 1 lay ALONG
    the horse over the saddle at the load and the walk (`770368d0` / `d4aec0a2` host-h2-loaded, `4585cc5a`
    host-h2-ride-walk-01) [O frames]. Body 1 is now held a quarter turn from the horse's yaw too, and 0.15 m (was 0.4)
    over the rider while riding (its back had been at the rider's mid-back). The older note that body 1 lies across at
    the horse's yaw (`c667a209`) dates from the Lua Warp [I].
  - **The fall at the final** (cfdfe961 / 4faf6d81): the per-id fall check `0x14128E010` (C0.vt+0xD0) skips one frame for
    each C0 reset (bit 0x20 of the 0x580 record's +0x2C0) and otherwise runs its ground probe after the +0x3B8 grace [O
    static, Ghidra comment]. While tracked, hold0 runs from M1's per-frame placement; after the final the cargo tick held
    body 0 again only TRACK_PIN_MS (200 ms) later [O code], and `5a92c73f` (c) / `4585cc5a` (a) show fall / land from the
    final on [O logs]; that the gap starts it is [I]. Body 0 is now held every tick once the track has ended.
  - **The mount** (3b2236a7 / e59c6840): in `f7e09c55` the victim's copy unrealized body 1 at the partner's mount
    (`partner_mount=unrealize; op=clear_allactive`), the census refused (body 1's block is checked first), hold0 refused
    `body` twice, and body 0 fell / landed and squatted; five CRAWL requests over 2.6 s laid it down again [O guest log].
    With world_horse_cargo, a hold0 of body 0 alone now uses body 1's class from the last full census while body 0's own
    T class equals it (`horse_drive=held_alone`); the pillion's hold0 at the defaults is unchanged.
  - Attempt 1 (da74e9e1 + the two fixes above): lab `f7e09c55` stopped in (a)'s ride on a refused guest capture
    (`sequence_expiry_or_busy`, frame 27); parts (b) and (c) then found the host still mounted. Until then [O both logs +
    frames]: the load, the first E mounting, and the carrier's screen with body 1 ACROSS the rump behind the rider at the
    load, mounted, walk, trot and gallop (host-h2-loaded, -mounted, -ride-walk/-trot/-gallop/-gallop2/-gallop4); the
    victim's own screen lying across the horse at the load and the walk..gallop2, hunched at the mount (the squat above).
    Play `bb7dde83`: before the load the carrier's rifle put the host's own horse Unconscious (aim ray on 6656,
    `horse_hit life=5400/12000`), so the first E at the loaded horse did not mount it [O host log]; host-h2-loaded shows
    body 1 across the rump in play [O frame]. Neither stop was cargo code: the shot now goes away from the horse (the
    victim 8 m behind the carrier at the horse, the carrier 4 m in front of them) and the parts take single frames.
  - Attempt 2 (3d2b9d39: + the mount hold, single frames, the shot away from the horse; merged dev adbad887): two queue
    jobs stopped before any game on the worker's audited CTest (`partner_defer_tests` / `..._nested_foreign_tests` check
    185, a timing check that passes alone: 3/3 and 383 checks on the worker's build) and were re-queued. Lab `39fff762`:
    **(a) passed every step** [O both logs + frames]: the load, the first E mounting, a ride of 199 m (`final_sent
    path_cm=19944`), the victim's stance CRAWL in all 25 `victim_pin` windows (worst window lag 152 / drop 21 cm) with no
    `step=stance`, `step=relie` or hold refusal from the load to the take-down, only prone_idle (`a=5`) in the 6 s after
    M1's final (before: fall / land and a squat, `5a92c73f`, `4585cc5a`, and W's queue run `e759bc02` on the dev tree:
    SQUAT 33 ms after the final, four CRAWL requests, lying again at 1.6 s), one KEEP re-place 2.3 s after the final (the
    settle, 3.2 m), the R take-down at the same x,z on both copies (86610,148424) and the revive; no Unconscious and no
    NEVER line. Frames: on the carrier's screen the body lies ACROSS the rump behind the rider at the load, walk, trot,
    gallop, gallop4, the stop and both frames after the dismount (`host-h2-*`); on the victim's own screen it lies on its
    back at every gait and after the dismount (`guest-h2-*`), with the horse itself in view only at the load (a camera
    close over the body; that the horse is under it at the other gaits is [I] from the logs: `got_cm` = `want_cm`). (b)
    stopped before its load: the guest's call after the teleport left the horse where (a) ended, by a rock wall, and
    the guest's R there never shouldered the downed host (`coop_carry=search ids=1` for 9 s; the stock carry, not this
    code); (c) then found the guest mounted. Play `624148f9`: after the load the carrier stood
    at the horse's head (`host-h2-loaded`: the body across the rump) and the first E did not mount [O].
  - Attempt 3 (4f312de6: + `horse_flank`, parts 2+ at the horse, shorter rides, the victim's camera a little down; merged
    dev 872e3519 and e8f0c0f9 with W's capture retry): lab `a53a243a` (a): `horse_flank` put the carrier at the flank and
    the first E mounted [O host log]; ride 60 m (`path_cm=5966`); the victim's stance CRAWL in all 19 windows and no stance
    change from the load to the take-down, one window dropped 250 cm across M1's final (lag 46) [O guest log]; the R
    take-down at the same x,z on both copies (84844,162005) [O]. Frames: the victim's own screen lies on its back at every
    gait with the rider's head beside the body and the horse-and-rider shadow under it, and after the dismount; the
    carrier's screen across the rump at the load, walk, gallop, gallop4, the stop and after the dismount, BUT on the
    ground behind the trotting horse in `host-h2-ride-trot` [O]: the partner recon warped body 1 to the victim's streamed
    position several times during the ride (`partner_recon=place; reason=teleport|retry; via=warp`, err 0.3-5.4 m, three
    tries each, beside `horse_drive=held; body=1`) [O host log], so hold1 and the recon both move body 1. Then the 3 s
    revive did not start (no `event=downed; step=grant`; the victim at hp 330 / 6600 after the bleed-out) [O], so (b) and
    (c) started with the guest still down and failed. Before the load the guest's put-down was moved six times by the
    body-safety placement (`step=putdown_place`, slide 4.23-4.43 m, drop 2.3 m) [O]. Play `72e6269a` (a): the carrier held
    the body at the horse (`carrybodies` required and passed) but the 900 ms E there gave no `step=loading` and no
    refusal line [O host log]; cause [U] (offTick returns silently when the carry role or the raw E read is not as
    expected); (b) then stopped on a stance check.
  - **Gate not passed** (three attempts used). Proven this round [O both logs + frames]: order (a) end to end in the lab
    (`39fff762`): the load, the first E, a 199 m ride with the victim lying on their back on their own screen (the horse
    in view only at the load; the HUD read `Teammate 4 m` there and `5 m` at gallop4, so that the horse stays under them
    is [I] from `got_cm` = `want_cm`), the dismount with the stance request on from the start and no fall / land, the
    take-down at one spot and the revive; the carrier's screen across the rump at the load, walk, trot and gallop
    (`f7e09c55`, `39fff762`; at the load also in play, floating high in `624148f9`). Not in `a53a243a` [O frames, the
    review's correction]: the carrier's screen shows body 1 sitting up on the rump, leaning back with the knees up, at
    the load (`host-h2-loaded`) and the walk (`host-h2-ride-walk`), and on the ground behind the trotting horse
    (`host-h2-ride-trot`). Two drivers move body 1 on the carrier's copy during the hold: `partner_recon=place ...
    via=warp` ran beside `horse_drive=held; body=1` in `39fff762` too (21 lines, err up to 5.1 m; 32 lines, up to
    5.4 m, in `a53a243a`) [O the review's count of the full logs, which the slim archive does not keep]; no frame of
    `39fff762` caught it. Never seen live: the guest-carrier order (b) with this code, the link drop with it,
    a play run past the load (`bb7dde83`, `624148f9`: the first E; `72e6269a`: the load), the `held_alone` path (no
    `body` refusal came in attempts 2 / 3), a horse falling (none seen). world_horse_cargo stays off unless named.
  - **Next:** keep the partner recon off body 1 while hold1 holds it (it warps body 1 to the streamed pose: the trot frame
    of `a53a243a`, and 21 warps unseen in `39fff762`), and find why body 1 sat up on the rump in `a53a243a`; find why the revive did not start after the 60 m ride (`a53a243a`: no grant, hp 330); run (b) where
    the stock pickup works (open ground; `39fff762`'s rock wall stopped it), then (c) and the play pair, both orders.

## 12. Ride behind your partner (horse plan M3, lab key `world_horse_pillion`, on with the gun profile since M3c)

`lua/coop/world_horse_pillion.lua` (a world chunk after `world_horse_share.lua`) seats a second player behind the
partner who rides the shared D-Horse of section 10. Round 1 (four attempts) did not pass its gate; the M3c round passed it
(lab `51f6a2cc`, play `4f52fa2a`, both `-WanSim bad`, the key not named) and turned the key on with the gun profile in
lab and play (`native/lab_config.cpp`; a named 0 is the off switch), with M3's 2.5 m / 2.8 m ride hold.
- **Claim.** On foot within 2.5 m of the horse the partner rides on this copy (world_horse_share's track), an E edge claims
  the rear seat; since the skeptic's round also anywhere the ride hold masks E (it releases past 2.8 m: before, E was
  masked and claimed nothing between 2.5 and 2.8 m, host `e_free; hold=1; masked=1` at 259 / 276 cm in `93b59ae4` /
  `5687aa1c`; unit-tested, [U] live). The ride hold masks ACTION there (probe P5), so E is read before the pad masks:
  `MGS_COOP_HORSE_DRIVE_V1 pad <mission> <epoch>` returns the PlayerPad held word's E without `& ~PP+0x28`
  (`(src+0x48 ?: PP+0x460) & 0x20`, `horse_drive::RawAction`; partner_player `ReadPad`, no new site), and since M3c the
  getter's word with the mask (`held`), the button mask (`bm`) and the move stick's magnitude (`mag`). HORSE_SEAT (0x50,
  `world-sync-protocol.md` section 32) seat 1: a guest's CLAIM is granted while the host rides the shared horse (else
  DENY `not_riding` / `busy` / `locked`); the host's own press beside the guest's ride commits at once (GRANT with cause
  passenger) and holds a 3 s press lock against a guest claim. KEEP at 1 Hz both ways; 3.5 s without it ends the seat.
- **Passenger's copy.** Pad mask `MgsCoopPillion` (buttons 0xFFFFFFA7: everything but aim, fire and reload; the move
  stick; the camera stays free), SQUAT, and body 0 every frame 0.35 m behind and 0.8 m over the placed census root
  (world_horse_share calls `W.horsePillion.placed` right after each placement) by BODY0's hold (section 11): `hold0` with
  reset 14 (the C0 reset, the write watch and the pin after the player's physics update), the default since M3c (round
  1's hold0 + A0 left body 0 1-2.3 m under its target). A tick without a placement holds body 0 at its last target again
  (`step=hold_keep`). Lab lever (debug_rules.lua, lab only): `pillion_pose <back> <rise> <mode 0|2|3|4|5|6> <dback> <dup>
  [seat 0|1]` (mode 4 the Lua Warp, 5 both, 6 the default; seat 0 asks the driver's copy for no overlay seat).
- **Driver's copy.** Every tick `MGS_COOP_HORSE_DRIVE_V1 seat1 <mission> <epoch> <back_mm> <up_mm> <pose 0|1>` (only
  while this copy's player rides the shared horse) leases 300 ms on the partner driver's clock; the partner driver then
  replaces body 1's pose with `horse_drive::RearSeat` of this copy's own body 0 (0.45 m behind, 0.3 m over its root, its
  yaw) and, with pose 1, the DM overlay seats it (`partner_dm_overlay` In::pillion: stateRideOnIdle). Lines
  `partner_pillion=seat / end`, `horse_drive=seat1`, and since M3c `partner_pillion=stats; xz_cm; dy_cm; n` every second
  (body 1's readback against this tick's rear seat, `horse_drive::NoteSeatErr`).
- **Endings.** E held 0.3 s again (after a release) hops off; the driver getting off (`driver_off`), the ride's end on the
  passenger's copy (`track_end`), a census change, no KEEP (`stream_lost`), a downed passenger, unpairing or a context
  change lay the passenger down at the claim-time ground offset. Until the skeptic's round that point was 1.3 m beside the
  horse, which the frames show is the horse's RIGHT (not its left, as this section said): in `51f6a2cc` (a) it put the
  passenger inside the rock wall the horse had ridden along (1.5 s later only the rifle and a forearm show on both
  screens). Now the passenger's copy keeps the placed root every 0.25 m from the seating (24 points) and lays them at the
  newest one 2-4 m from the root now, ground the horse stood on (`step=unseated; ...; at=trail`); without one (a horse
  that has not moved 2 m since the seating) the spot the passenger stood on at the claim, 0.3 m up, when it is within 5 m
  of the root (`at=foot`; play `16f3f282` / `023f9167`: the side point put the passenger inside the rock on the driver's
  dismount right after a seating); the side point only past that (`at=side`: a jump of the placed horse). Travel /
  Bring partner here wait while seated.
- **Ride hold.** With the pillion key the M1 hold is 2.5 m / 2.8 m (4 m / 4.5 m without it). M3c: an E 3.16-4.04 m from the
  ridden horse reached the game unmasked (`step=e_free; hold=0; masked=0`, read from the pad verb; guest lines in runs
  `93b59ae4`, `85a49831` (316 cm), `51f6a2cc` (404 cm), `5687aa1c`, `e284fb1c`, `4f52fa2a`); the host's e_free lines in
  `93b59ae4` / `5687aa1c` were masked at 2.59 / 2.76 m (the band the claim now covers). No pickup or revive was done there (nothing to pick up or revive in the
  scenario); in this binding a pickup is R, which the hold never masks.
- **The tie** (both press E at an unridden horse at once): each player mounts their own copy's horse and the partner's
  RIDE_FAST then arrives while this copy rides (world_horse_share opens no track): `step=tie; role=host; verdict=drive` /
  `role=guest; verdict=yield; getoff=none` on both copies (runs `763f071d`, `93b59ae4`, `51f6a2cc`), and M1's two-driver
  fallback stays (each sees themself riding alone until one gets off). The rule's second half, the guest getting off to
  sit behind, is not built: there is no Lua get-off (the stock scripts only send `HorseForceStop`, rec+0x2DF) and
  `0x141230B30` was first read as a generic ride-state request; M4 found it is a motion-node set (section 13), so there is
  no get-off id to pass there [O static].
- **Log lines** (`world_lua=event=horse_pillion; ...`): `step=claim / grant / deny / denied / seated; how=grant|host_press;
  mode=.. / driver / seat1; ok=..;pose=.. / seat_pin` (lag and height against body 0's target, 1 s, with this copy's horse
  census `occ`, `life`, `max`) `/ drive; moved_cm=..; rider=..; occ=..; held=..; bm=..; mag=..` (the driver's ridden path,
  2 s, the census rider index and the pad words) `/ first_move; ms=..` (the first ride key to 0.5 m of path) `/ stall`
  (ride keys 2 s with under 0.5 m) `/ horse_hit; m=..` (W.observe: a 6656 Damage / Unconscious / Conscious / Dead with the
  seat state, the life and body 0's height) `/ tie / hold_keep / pad / e_free; dist_cm; hold; masked / unseated; seat=1;
  why=..; side=passenger|driver` (the passenger's with `x; z; at=trail|foot|side`). The read verb carries record 0's `life` / `maxlife` from the receiver's life table
  (`horse_drive::ReadLife`: `*(receiver+0x30)+8`, 0x28 bytes per index, +0 life, +2 max; the reader is the D-Horse Lua
  dispatcher's SetMaxLife loop at 0x140E4D340, pinned in `tools/horse_drive_sites.json`, contract 6 sites 5 slots).
- **Hidden runs, round 1** (`ba6b4bb5` / `b48f897b`, `306525db` / `c4428efa`, `bdd49a57` / `a295d660`, `718b6411`): the
  claim, grant, seat1 lease, overlay seat, hop-off and both-off on both logs; the driver's screen showed two riders; the
  passenger's own body was never seated, the passenger's copy's horse fell Unconscious every run, and the host-driven
  horse 'stood still for about 16 s'.
- **M3c findings** [O unless marked]:
  - The host-driven 'stall' was the scenario. Run `7c0b9e71` re-read against the worker's input ticks (Lua t_ms = worker
    tick + ~3.4 s): the e_free press, placed 1.1-1.9 m from the horse, claimed the seat; the scenario then waited 4 s on
    e_free, 5.3 s on goto_horse and 3.5 s on captures, so the host's first ride key came 17 s after the grant (the 8 empty
    drive windows); the horse moved in the first window after it. The later empty windows were the horse against a cliff
    (frames). With the keys right after the claim the horse moves 0.5 m within 266-1034 ms of the first key in the M3c runs
    (`step=first_move`; the skeptic: 1034 ms for the host in `5687aa1c`, the horse facing the cliff, and 600 ms for the
    host's second ride in `85a49831`; this section said 266-583 ms). `step=stall` lines: 2 in `93b59ae4`, 1 in `85a49831`,
    3 in `e284fb1c`, each with the horse against rock or a cliff in the frames [I: terrain, not code], none in `51f6a2cc`
    or `4f52fa2a`.
  - The passenger's copy's horse fell when body 0 dropped onto it: in every round-1 run body 0 hung 1-2.3 m under its
    target (mode 2), and in BODY0 it fell 0.17-0.19 s after a 100 ms pin gap and at goto teleports [I: the stock fall onto a
    character]. With mode 6 no M3c run logged an Unconscious 6656 or a horse_hit, and the horse's life stayed 12000 / 12000
    on the passenger's copy through every ride.
  - The passenger's own body: seat_pin lag 0 / dy 0 in every window but the first after each seating (40-59 cm while the
    body comes up from the ground); frames show them squatting behind the driver on their own screen.
- **Hidden runs, M3c** (`horse-pillion.txt` + `horse-pillion-tie.txt` + `horse-pillion-drop.txt` lab, `horse-pillion-play.txt`
  play, all `-WanSim bad -SaveSeed afgh-ledge`, world_horse_pillion not named; slim archives
  `%USERPROFILE%\mgs-coop-run-archive\horse\<run>`):
  - `763f071d` / `f3d04eca` (attempt 1): the guest-driven order passed; the host-driven order stopped at a scenario step
    (placebehind needs the host's position, refused while it rides). The lab tie logged `step=tie` on both copies.
  - `93b59ae4` / `5687aa1c` (2), `85a49831` / `e284fb1c` (3): every seat step in both orders, the tie, the link drop (the
    guest's copy laid them off with `why=context`, the host's seat ended `why=stream_lost`); the second ride of each session
    started where the first had coasted into rock (or a recalled horse arrived facing it) and stopped there (`step=stall`,
    frames of the rock face; one session's last first E then met a horse on a rock slope).
  - `51f6a2cc` (lab, attempt 4: the host's ride first from the boot spot): every step of both orders, the tie and the link
    drop; first_move 316 / 367 ms, gallops of 12-16 m/s in both orders, no stall line, no Unconscious line, life 12000
    throughout; passenger seat_pin 0 / 0 after the first window; driver stats xz <= 50 cm and |dy| <= 35 cm after the first
    second of each seat (that second: body 1 still 1.4-2.2 m under the seat, coming up from the ground). Frames [O]: two
    riders on the driver's screen at walk, trot and gallop in both orders (the passenger squatting at the rear). Corrected
    as the skeptic checked them (this list said both orders' side views show both bodies): in order (b) the driver's side
    views show no second rider (`guest-h3b-idle-side-guest` is foliage and rock, `guest-h3b-stopped-side-guest` a leg
    close-up); on the passenger's own screen the passenger is mostly hidden behind the driver, and (a)'s stop side views
    show only the driver's face.
  - `4f52fa2a` (play, attempt 4): every step of both orders on both logs (host_press and grant seats, hop-off, both off on the
    driver's dismount, the first E at the unridden horse); first_move 316 / 350 ms, the host's ride at a canter (6 m/s), the
    guest's at a gallop (15 m/s), no stall, no Unconscious line, life 12000; passenger seat_pin 0 / 0 after the first
    window; driver stats xz <= 29 cm, |dy| <= 36 cm after the first second. Frames [O]: two riders on the driver's
    screen at trot and gallop in both orders, the passenger on the horse behind the driver on their own screen through the
    ride, mostly hidden behind the driver (the skeptic: (b)'s `guest-h3b-idle-side-guest` is a rear view with one body).
  - Every hop-off and drop frame seen from behind the horse (`4f52fa2a`, `51f6a2cc`, both copies) shows the passenger on
    the horse's right; `51f6a2cc` (a) inside the rock wall (the Endings item above has the fix).
- **Hidden runs, M3d** (the skeptic's corrections, same scenarios, the key not named, `-WanSim bad`): lab `d795ab11` (at
  54e96e67: the ridden-path lay-off) and `da67ef81`, play `975a0641` (at 7564a60a: plus the claim-time spot): every hop-off
  in both orders `at=trail`, the passenger on the ground behind the horse on their own screen and beside its rear on the
  driver's; the lab drops `at=trail`; the driver's dismount right after the second seating `at=foot` (standing beside the
  horse). Play `16f3f282` / `023f9167` (at 54e96e67): that dismount used the side point and put the passenger inside the
  rock on their own screen, and the guest's next first E at the unridden horse did not mount. `975a0641` passed every
  step (one `step=stall` in (b), rock ahead in the frames); the lab runs every step but the drop's `world_inactive` expect.
- **Not yet:** the tie's get-off (above); on the driver's screen the first second of each seat shows the passenger beside
  the horse before they come up; the passenger squats (no seated rider pose: `player2_cypr` waits for M4, D-H6); the stock
  E / horse prompt still shows on the passenger's screen (E is masked; held 0.3 s it hops off); no aiming or shooting from
  the rear seat (M4); a downed passenger falls off (M5 makes them cargo); Africa untried; not tried by two people. The tie
  rule (plan item 5, 'M3 adds the tie rule') is only logged (`getoff=none`), not done. Never seen live: the driver's
  dismount with the guest driving (every driver_off run had the host driving), the `hold_keep` keepalive (it never fired),
  the link drop in play mode (lab only).

## 13. Shoot from the back of the horse (horse plan M4, lab key `world_horse_shot`, off unless named)

Built on section 12's rear seat (M4 round, `wp/horse`). The key `world_horse_shot` (lab and play, needs
`world_horse_pillion`; a named 1 runs in play too) is **off unless named**: the M4 gate was not reached in three attempts.
Markers [O]/[I]/[U].
- **The passenger's own copy.** The pillion pad mask already leaves aim, fire and reload free, and body 0 is on foot
  natively, so the stock squat-aim runs on the seat; the passenger aims and fires on its own screen [O frames, runs
  `d935beae`, `28015a91`, `48d31500`, `1a7c5d0e`: the rifle raised behind the driver, the magazine counting down].
  With the key, `world_horse_pillion.lua` leases `MGS_COOP_HORSE_DRIVE_V1 pbox <mission> <epoch> <x> <y> <z> <yaw>` (the
  placed census root, mm / mrad) with every placement (`step=shot_box` once per seating; `pillion_status ... shot=`); the
  partner driver publishes it to the RequestBullet hook as `partner_seat_shot` class `horse` (vehicle 6656, seat 1;
  `TickHorseShot`): `kHorseBox` about the root, 1.3 m behind to 1.7 m ahead, 0.6 m a side, 0.6 m under to 2.2 m over.
  A bullet starting inside it moves along its own line to the exit (+5 cm, never past the aim point - 0.3 m), as the
  jeep's S1 (design 8.37). No new hook, site or wire. Lines `partner_seat_shot=published; vehicle=6656; seat=1;
  class=horse` and one `partner_seat_shot=shot; result=shift` per shot.
- **Cross-neck shots** [O logs, both copies, lab and play]: every straight-ahead shot (`d_milli=1000,..`) started 1.57 m
  ahead of its muzzle (`exit_cm=152`), past the driver and the horse's head; the passenger's copy's horse kept 12000 /
  12000 in every `seat_pin` window (40-86 windows a run); the driver's life read before and during the ride stayed the
  same (status `life=7801,7801` host, `6600,6600` guest, godmode off while driving, `28015a91`). Not discriminating: no
  stock (unshifted) shot on the same line was fired, so whether a stock shot hits the horse or the driver is [U].
- **The driver's copy.** `partner_seat_gun`'s gate takes the pillion seat (the seat1 lease with the overlay seat) as a
  seated passenger seat and its DM overlay as no block (`psg::Block`); it runs with `world_horse_shot` even with
  `partner_seat_gun` off. The hold runs (`partner_seat_gun=hold; ...; pillion=1`, the gesture hook's `gun_start`
  applied on graph 2 with body 1 at act 139) [O], but **no frame shows the gun raised**: body 1 sits behind the driver
  with its arms forward and the rifle still on its back (`28015a91`, `48d31500`) [O looked]. This is the M7 seat gun's
  known limit (docs/native-puppet.md "Seat gun": "the hold runs but the partner's gun does not show"). Since M4b (below)
  the overlay leaves body 1 to its native aim while the remote aims, and the raised gun shows.
- **Hits.** No shot from the rear seat put a guard down in five runs (both orders): sleeping guards 4-7 m away (attempts
  1-2) and an awake one (attempt 3), aimed with `enemy_aim_nearest`. From the horse the lever's 1.5 m eye is too low: 0.3 m up put the reticle 1.4 m short of
  the guard (`28015a91`: aim_nearest d=6.5 m, the shots' `dist_cm` 503-506); the 1.2 / 1.6 / 2.0 ladder (`48d31500`) hit
  nothing either, and in (a) the enemy follow had moved the guest's copy of the warped guard back to the host's (39 m,
  `place_div`). Whether a rear-seat bullet reaches a guard at all is [U] (design 8.22 saw the same with seated jeep
  shots before 8.30's aim-camera fix).
- **Seat fixes found by the M4 runs (only with `world_horse_shot` named since the review: 1397fe22 red / 706edae8
  green).** Both play `-WanSim bad` runs with them in (`1a7c5d0e`, `53af6597`) stopped because the driver's E did not
  get them off after the passenger hopped off; the pre-fix play run `28015a91` and both post-fix lab runs got off
  1.3-1.8 s after the hop-off. In `1a7c5d0e` the host's E was consumed (worker `input_consumed` seq 62, 1.6 s after the
  overlay stop) but the action stayed `a=79` (no `a=7a`), `localMounted=1` [O host log]; the host's lines from the
  hop-off to the E match `28015a91`'s, and none of the holds acts on the driver's own E [I code]; cause [U]. In
  `53af6597` the driver was in a downed cycle from the awake guards (`event=downed step=abandoned` 2 s after the
  hop-off) [I]. With no play pass of the get-off, the defaults keep 0.15.2's seat rules
  (`SeatHold.grace`, `In.pillion_reseat`, the Lua track wait all follow the shot key; since step S they follow
  `world_horse_pillion`, section 14).
  - `d935beae`: a frame capture stalled the driver's game past the 300 ms seat1 lease; the overlay seat stopped, and
    because the lease was back before the stop completed, `finish()` kept stateRideOnIdle as the remote episode's done
    entry and never seated body 1 again (`partner_pillion=stats; dy_cm=-190` for the rest of the ride: body 1 on the
    ground behind the horse) [O]. Fixed: `horse_drive::HoldSeat` keeps a lease live on the previous tick 2 more ticks, and
    the overlay re-enters a pillion seat once idle. `48d31500`: no lease end inside a seating (each `partner_pillion=end`
    is a real hop-off or driver_off) [O].
  - `28015a91` (play): a 0.9 s gap in the guest's RIDE_FAST ended the host's track (`stream_lost`) and the host's seat
    behind the guest (`track_end`) while the ride went on [O]. Now a track ended by `stream_lost` is held 2 s
    (`step=track_wait`, body 0 at its last target); a final or other end still lays off at once. Unit-tested; no run
    logged a `track_wait` yet [U live].
- **Coverage asked of M4.** The driver's dismount with the GUEST driving: `48d31500` (b), `why=driver_off; side=driver`
  on the guest, `why=driver_off; side=passenger; placed=1; at=foot` on the host [O both logs]. The 2.5-2.8 m E band:
  not reached (S held 350 ms moved the guest nowhere: claim at 1.56 m) [U]. The play-mode link drop: the worker's only
  drop (`worldtrigger world=0`) is lab only, so it cannot be run in play [U].
- **The tie's get-off** [O static, Ghidra]: `0x141230B30` is not a ride-state request. It takes (`ctrl =
  *(*(blk+0x78)+0x238)`, id, layer 0, `FUN_14122f880(node)`, blend) and calls ctrl vt+0x190: a motion-node set with a
  48-bit node hash (`0xf241f3bcf60b` in `FUN_141236940` case 9); its 19 call sites (in 14 functions, plus one data
  reference) are the ride states' event handlers
  (a switch on the event code). There is no get-off id to pass there; a native get-off is the ride state machine's own
  transition [U]. The tie rule (host drives, guest behind) is not built; comment at `0x141230B30`.
- **D-H6 (`player2_cypr`)**: not started (it waits on this gate). Local evidence only: `player2_cypr.mtar` (184 clips, 7
  referenced by the resident graph, names unknown; design 3.4); the rear-seat clip's name hash is [U].
- **Hidden runs** (`horse-pillion-shot.txt` lab / `horse-pillion-shot-play.txt` play, 50 m south of afgh_field_cp, `-WanSim
  bad -SaveSeed afgh-ledge`; slim archives `%USERPROFILE%\mgs-coop-run-archive\horse\<run>`): attempt 1 `d935beae` (lab,
  stopped in (b) at a capture refusal, `sequence_expiry_or_busy`) / `28015a91` (play, every step ran); attempt 2
  `48d31500` (lab, every step ran) / `1a7c5d0e` (play, stopped after (a): the host's E did not dismount, cause [U]);
  attempt 3 `88b7b621` (lab, the key named, the guards left awake: the warped guard did not ignore the
  players, shot the passenger (228 a hit; the passenger's copy's horse 12000 -> 10163 at the lowest; that the guards'
  rounds did it is [I]: the first two drops, 12000 -> 11627 -> 10885, came in the windows right after the passenger's
  own cross-neck shots) and the outpost's guards took the driver to a lethal hit three times (downed_damage), the
  host's horse fell Unconscious (life 4828) while the passenger's copy read 10163 or more (horse damage is not shared),
  and both copies reloaded 30010 (a 30010 -> 30010 reload, not a mission change); the guest's debug commands were
  refused after) / `53af6597` (play, the same: the guards' fire downed the driver and the passenger's copy's horse
  went to 6489 / 12000 [I the guards' rounds]; stopped after (a)). The 'commit actor=guest' check (a guard hit from
  the rear seat) failed in all 6 runs. Process: `world_horse_shot` was on by default from fe81384a to d9f36d26, before
  any proof run, and went back off before the head (never released). Under the shot key the seat gun's gesture runs on
  body 1 while the DM overlay seats it (`psg::Block` exempts the overlay): two systems on body 1, lab key only.
- **M4b round** (`wp/horse`; scenarios as parts from the boot spot: `horse-pillion-shot.txt` / `-play.txt` (a),
  `horse-pillion-shot-b.txt` (b, both sessions), `horse-pillion-shot-c.txt` (lab: an A/B and driver_off); markers
  [O]/[I]/[U]).
  - **The driver's get-off after a hop-off** (play `1a7c5d0e`). Cause [I]: the harness lost the E, not the horse code.
    The host's `press e 150` (worker seq 62) was accepted at 352892765 and the game's next keyboard read came 266 ms
    later (352893031; every other keyboard command of that run 0 ms) [O host.worker.jsonl]; the worker releases a press
    at accept + duration and never delivers a late press (native/worker_scenario.cpp `Scenario::Tick` and its prune),
    and the release stamps the same command, so that `input_consumed` read already saw E up [I code]. The scenarios hold
    the driver's E 400 ms, and the driver's copy logs its raw E and the ride's end for 8 s after a driver seat ends
    (`step=driver_e; e=1; after_ms`, `step=driver_e_off; after_ms`). Lab `11645acc` and play `22926dbf` (`-WanSim bad`),
    both orders: the E read 1.70 / 1.82 s (lab a / b) and 2.26 / 1.82 s (play a / b) after the hop-off and the ride
    ended 16-25 ms later; every `localMounted=0` require passed; driver_off with the host driving too (lab c) [O both
    logs]. The three seat holds still follow `world_horse_shot`: moving them to the pillion default needs a run with
    them at the defaults (the shot key not named), not run yet.
  - **The driver sees the gun.** With `world_horse_shot`, while the remote aims (its live pose's Aiming flag;
    `horse_drive::AimSeat`, 1.5 s past the last aiming tick) the partner driver does not let the DM overlay seat body 1
    (`m.pillion_pose` false; the seat gun's pillion gate follows it), keeps body 1's pose at the rear seat and notes it
    (`NoteSeatAim`); the `seat1` reply carries it (`aim=1, sx, sy, sz` mm, `syaw` mrad; `SeatReply`), and the driver's
    copy's Lua pins body 1 there every tick with `hold1` reset 14 (C0 + the write watch + pin; `HoldScope` admits body 1
    under the pillion key only with the shot key), `free0` at the aim's or the seat's end. Lines `partner_pillion=aim;
    on=0|1`, `step=aim_hold; on=1; n; ok` / `on=0; n; ticks`, `step=aim_hold_refused`. No new hook, site, slot or wire.
    [O both logs, lab and play, both orders] every remote aim gave `aim; on=1`, `aim_hold; ok=1`, `horse_drive=held;
    body=1`, the watch pinning nearly every trap (4 writers; some reports fewer, e.g. 100 of 108, 160 of 167, 184 of
    186) and `partner_pillion=stats` xz 0 / dy 0 while held (exceptions: the
    walk shot up to 195 cm, one partner_recon teleport warp of 0.94 m). [O frames looked at] the driver's screen shows
    body 1 on the horse's rump behind the seated driver with the rifle raised, aimed ahead or down at the guard: lab
    `host-h4-pillion-aim-host`, `host-h4-guard-aim-host`, `guest-h4b-pillion-aim-guest`; play `host-h4-pillion-aim-host`,
    `host-h4-guard-shot-host`, `guest-h4b-pillion-aim-guest` (from behind: hunched behind the driver, the barrel out to
    the left). Not the squat: body 1 stands or crouches on the rump; why the native drive shows a standing aim is [U].
    The seat gun's gesture still starts for 15-62 ms on the input frame's aim flag before the pose flag turns the
    overlay off (`partner_seat_gun=release; cause=gate; refusal=key_off`).
  - **Hits.** `enemy_aim_nearest <up> <eye up> <eye back>` puts the lever's eye at the rear seat's camera (28015a91's
    miss fits h - 0.80 b = 0.385 m: eye 1.2 m up, 1.0 m back). A camera read is not built: the aim ray starts at the
    camera eye `*(*(*(gun+8)+0x138)+0xB8)+0xE0` in GunAimPointSolve `0x141096F90` (Ghidra comment) [O static]. Attempt 1
    (target 0.3 m up): play (a) the guest's first shot at the sleeping guard 6.1 m away put him Dying (guest `event=req;
    state=4`, host `commit; state=4; actor=guest; rid=2`; frame `guest-h4-guard-shot`: the guard sitting up, hit) and the
    host's apply of it failed (`apply; state=4; src=commit; result=fail; readback=2` and a ledger hold `apply_failed`,
    as in design 8.30 / 8.31), so the guard stayed asleep on the driver's screen while dying on the passenger's: the hit
    landed on the shooter's copy only; play (b) the host's second shot killed that dying guard
    (host `commit; state=5; actor=host`, guest `apply; result=ok`) - the guard warped 5.6 m ahead was missed. Lab: no hit
    in either order: (a) the reticle at the lying guard's head (frame `guest-h4-guard-aim`) and the line's aim point
    about 0.8 m short (664-682 cm against 6.8 m), (b) 0.7-0.9 m short (the reticle on the road).
  - **Cross-neck.** Every straight-ahead shot started past the box (exit 152-153 cm); the driver's life 7801 / 6600
    and the passenger's copy's horse 12000 in every window (lab, play (a): 30 seat_pin windows) [O]; in play
    `2b9552f6` (b) the passenger's (host's) copy's horse read 10163 / 11965 of 12000 in 6 of 34 seat_pin windows while
    awake guards fired (cause [U]). A/B in lab (c):
    with `seatshot off` the unshifted straight-ahead line (`result=census; apply=0`) left the horse at 12000 and the
    driver at 7801 [O] (which body the line crossed is [U]); in attempt 1 the `on` shot 0.8 s into the aim did not fire [O no line] (attempt 2: below).
  - **Harm seen in play (b)** (`22926dbf`): after the passenger's kill awake guards shot both players
    (`downed_damage` attackers 1039 / 1243; the guest driver 6600 -> 3450) and the guest's copy's horse went Unconscious
    3 s after the dismount (life 4652 / 12000) [O]; that the guards' rounds hurt the horse is [I].
  - **Attempt 2** (`5043fb4d`, scenarios only: the guard 0.3 / 0.7 / 1.1 m up from the 1.2 / 1.0 m eye; the A/B shots
    1.5 s into the aim). Play `2b9552f6`: (a) passed every expect: the guest's first shot put the sleeping guard 4.3 m
    away Dying, the second killed him (guest `req; state=4` then `state=5`; host `commit; state=4` and `state=5; actor=guest`,
    the host's state 4 apply failed again, `result=fail; readback=2`, its state 5 apply ok: the Dying state showed on
    the shooter's copy only); frames `guest-h4-guard-shot` (the guard hit, the reticle red) and `host-h4-guard-aim-host`
    (body 1 leaning over the driver, the rifle aimed down at him) [O]. (b) failed: awake guards (`downed_damage` attackers
    1041 / 1044 / 1243 / 1270) took the guest driver to lethal hits three times (`downed_damage=intercept; cause=lethal`,
    life 330, `event=downed; step=abandoned`), so after the host's hop-off the guest's E was read (`driver_e; e=1; masked=1`)
    but did not get a downed rider off (`localMounted=1`, `downed_control=refused; reason=controls_timeout_prone` three
    times, downed generations 4 to 9) [O]: a defect, a rider downed on the horse stays on it (README known limits); the host's three shots at the guard 4.2-5.4 m ahead hit
    nothing. Lab `375df6ee`: (a) the guest's copy of the warped guard ended 36.4 m away (the sleep's apply moved it 39.7 m;
    the d check failed), so the ladder shot at a far guard; (b) the host's shots at its own guard 4.3-5.1 m ahead (aim
    points 3.9-4.5 m out) hit nothing; (c) passed: the A/B's two straight-ahead shots (`seatshot off`: `census; apply=0`;
    on: `shift`, exit 152 cm) both left the driver at 7801 and the passenger's copy's horse at 12000 [O]; the driver's E
    after a hop-off 1.80-1.85 s and the ride's end 16-17 ms later in (a), (b), and driver_off in (c) [O].
  - **Gate: not reached in two attempts** (lab: no hit from the seat in either order in four parts; the (b) order's hit
    only on (a)'s dying guard in play `22926dbf`; play `2b9552f6` (b) the driver downed by awake guards; in both plays
    the first hit's Dying state on the shooter's copy only, which the scenario's waitlog on the host's `commit` did not
    catch). `world_horse_shot` stays off unless named; README: no aiming or shooting from the seat at the defaults
    (unchanged), and the stuck downed rider as a known limit. All four sessions ended with the harness's
    `forced_job_termination=true` (one process exit 1223); no crash_probe line.
- **Next:** the gate's lab half: a hit from the rear seat in lab in both orders (a camera-eye read instead of the eye
  model, or a target that stands still and level), the squat (why the native drive shows a standing aim), keep the
  guards quiet after a kill in play (b), the Dying state of a seat hit on the driver's copy (and a scenario check on
  the host's apply, not only its commit), a downed rider getting off the horse, then the flip with a run not named; the
  seat holds to the pillion default with a run not naming the shot key.
## 14. Step S: the rear seat's holds at the defaults; the downed rider (not fixed)

Round `wp/horse` step S (code head `9ac40a64`; markers [O] / [I] / [U]). Since this step the three seat holds that M4
found (section 13) come with `world_horse_pillion` (on with the gun profile), no longer with `world_horse_shot`:
`partner_driver` `Settings.horse_seat_holds = world_horse_pillion && world_horse_share` sets `horse_drive::SeatHold.grace`
(a lease live on the previous tick kept 2 more ticks) and the DM overlay's `In.pillion_reseat` (a stopped pillion seat
re-entered once idle); `world_horse_pillion.lua` holds a track ended by `stream_lost` 2 s (`step=track_wait`) whatever
the shot key. A named `world_horse_pillion=0` (or `world_horse_share=0`) turns them off with the seat. Red / green:
`43362ee7` / `9b808c5f` (partner_driver_tests Configure, pillion Lua check). Their condition (the driver's E after a
hop-off in play `-WanSim bad`, both orders, with them in) was met by `22926dbf` (shot key named).
- **Hidden runs, no horse key named** (`pillion_status` `key=1; mode=6; shot=0` on both copies; scenarios
  `horse-seat-a.txt` / `horse-seat-play-a.txt` (a), `horse-seat-b.txt` (b), `horse-down.txt` (lab); `-WanSim bad`, the gun
  profile, afgh-ledge). Lab `1c2a6b0a` (its first job refused before the game, `Worker qualification ownership receipt
  mismatch`, not counted) and play `79287a27`: parts (a) and (b) passed every require in both [O both logs]:
  - The first E at the unridden horse mounted in every part (the driver's in (a) and (b), the guest's at the end of (a)).
  - The driver's E (held 400 ms) after the passenger's hop-off got the driver off in all four orders: `step=driver_e`
    2536 / 2553 ms (lab a / b), 2769 / 2895 ms (play a / b) after the seat ended, `driver_e_off` 16-25 ms later,
    `localMounted=0` [O both logs]; frame `host-s-both-off-host` (play (a)): both on the ground beside the horse [O].
  - The passenger's copy (`step=seat_pin`, 25-26 windows per order): every window 0/0 except the second (the seating:
    lag 32-52 cm, dy 20-23 cm, as in `22926dbf`) and one lab (a) window dy 65 cm [O].
  - The driver's copy (`partner_pillion=stats`): after the seating second (body 1 beside the horse, the known limit)
    under 0.55 m, except 9 windows of 0.87-5.26 m, 7 of them with 5-29 ticks in the second (game hitches): lab (a)
    165 / 233 / 499 / 87 cm at n = 29 / 27 / 13 / 13, play (a) 526 / 182 / 91 cm at n = 7 / 13 / 38, play (b) 142 / 102 cm
    at n = 5 / 36; each back under 0.4 m within 2 s; one
    `partner_pillion=seat` and one `end` (the hop-off) per order, no seat end inside a ride [O]. That a hold acted in
    those windows is [I] (the grace and the re-entry log nothing); no `track_wait` or `stream_lost` line in any run [U
    live]. So the holds are on at the defaults because their condition was met (`22926dbf`), not because a run saw them
    keep the seat; the `d935beae` drop has not been seen again with them in or out.
  - Frames looked at: the driver's screen shows the passenger on the horse behind the driver at walk / trot / gallop
    (play `host-h3-walk-host-02`, `-trot-host-02`, `-gallop-host-02`; two riders side on in `host-h3-stopped-side-host`
    and in (b) `guest-h3b-idle-side-guest`, `guest-h3b-gallop2-guest-02`; lab sheet `l1-h3-host`) [O]. NEW: in play (b),
    after the horse ran into the rock and stalled (`step=stall` 2077 ms), the driver's screen shows body 1 STANDING on the
    horse's back and the driver leaning on its neck (`guest-h3b-gallopfree-guest-02`, `guest-h3b-stopped-side-guest`)
    while the overlay kept playing stateRideOnIdle (one `partner_overlay=play`, the stop only at the hop-off) [O]; cause
    [U] (none of the holds acted there by the logs [I]).
  - Not met: order (b) did not start on open ground. Both players were teleported to the boot spot and both horses were
    called (`horse_spawn`), but the horse stayed where (a) ended (play seat at 692.9 / 1656.5 m against (a)'s final 692.6 /
    1656.7; lab 864.1 / 1740.5 against 864.6 / 1740.7), by a rock wall: (b)'s rides were 28 m (play) and 6 m (lab), each
    with a stall [O].
- **The downed rider** (`2b9552f6` (b): a driver downed by guards kept `localMounted=1` after E). Static cause [I from
  code]: a wounded cycle needs the prone request (stance 3, `downed_player.cpp`); a mounted body never takes it, so the
  cycle waits `controls_pending_prone` and after `kControlsAcquireMs` (6 s) ends `controls_timeout_prone` (a new
  generation: `event=downed; step=abandoned`). While it waits, world_downed's `MgsCoopDowned` mask (buttons
  `0xFDDFFFFF`) holds ACTION, so E is masked (`driver_e; e=1; masked=1`); the guards' fire restarted cycles (generations
  3 to 9). A passenger who is downed is laid off by `world_horse_pillion` (`stop("passenger_downed")`) [I code; never seen
  live]. The repro at the defaults (`horse-down.txt`, lab `1c2a6b0a` part 3) did not down the rider: the host's three
  rifle bursts from beside the horse hit the horse (`horse_hit; m=Unconscious; life=5400`, Conscious at 6020 3.5 s later)
  and the rider on the host's copy: 23 `downed_damage=sample; id=1` lines (body 1) at host log lines 6226-6500, all
  `damage=0.0` (on foot the same friendly fire gives `damage=942` and `friendly_hit=queued` in the cargo runs), so
  friendly fire does not hurt a mounted partner and cannot be the repro; the guest kept 6600 / 6600, `down=0`, and the
  guest's E got off [O]. The horse went down on the host's copy only: frame `host-s-down-shot` (looked at) shows the horse
  lying, the shooter at its flank and body 1 in the riding pose about 1.5 m in the air above it; `guest-s-down-shot`
  (looked at) shows the guest still riding the standing horse on their own screen [O].
  Ghidra (comment at `0x14122F3F0`, static [I]): the RideHorse get-off check
  returns true for ACTION (bit 5 of the pad word) or a byte `*(rec+0x18)+0x16E` (a forced get-off flag; writer unknown),
  and its callers then set the ride record's next state (+0x11 = 3).
- **Code after the runs** (the review): `6719e257` / `a0a45c4a` changed `partner_driver.cpp` (`body_owned`, only with
  `Settings.horse_cargo`) and `world_downed.lua` (`revive_wait` / `revive_near`, logging at the defaults); `31477fee`
  merged dev's `lrrp_follow` / `drive_vert` (on). No seat run at `31477fee` or later [U]; the seat path at the defaults
  unchanged is [I from the diff].
- **Next:** (b) from open ground (a way to move the horse after (a): it does not answer the call after M1's final, or
  end (a) away from rock); the standing pose after a stall; the horse down on one copy only (the rider hangs above it on
  that copy); the downed rider: a repro that downs the rider (a guard: friendly fire does 0 damage to a mounted partner),
  then on the rider's copy a downed and mounted rider
  reported as a held posture (no prone timeout) and the native get-off: the `+0x16E` flag's writer, or ACTION left
  unmasked for the get-off and pressed by the mod (a new pinned site either way).

## 15. M2h round: one driver on body 1, the revive after the take-down (not passed)

Round `wp/horse` M2h (world_horse_cargo named, off unless named; markers [O] / [I] / [U]; slim archives with the full
session observer logs `%USERPROFILE%\mgs-coop-run-archive\horse\<run>`).
- **What the old runs showed** (re-sliced from the game folders' observer logs): in `a53a243a` the 32
  `partner_recon=place` lines beside `horse_drive=held; body=1` were logged but not applied (the apply is skipped under
  `carry_hold`; each verify ended `placed ok=0` with `body_mm` still on the horse); the second driver was the native
  input window, armed with the victim's pad (`partner_native mode=active mag_pct=100`, err 0.3-6.5 m). The review could
  not check either [U]: no observer logs of `a53a243a` are archived (only `slices.txt`, no recon lines), and the same
  `partner_native mode=active mag_pct=100` signature shows in every neutral hold of this round, so it alone does not
  mark a non-neutral pad.
- **One driver on body 1** (`55269368` red / `6719e257` green): with `world_horse_cargo` the partner driver sets
  `pr::In.body_owned` while coop_carry's cargo hold holds body 1 on this copy and no stock carried body 1
  (`Settings.horse_cargo = world_horse_cargo && world_horse_share`): the reconciliation places, verifies, nudges, pins,
  snaps, turns and stance-fixes nothing, drops pending placements and keeps the window armed with a neutral pad; edge
  line `partner_recon=cargo_hold; on=0|1; mode=..`. Live [O both orders, lab `990d2530` and play `c62e2ce6`]: 0
  `partner_recon=place` lines inside the cargo hold, `partner_native_face reason=owned; neutral=1`.
- **Load / ride lines**: `step=loaded` and `step=pin` carry `hyaw` / `byaw` (the hold's yaw, a quarter turn) and since
  `a0a45c4a` `bx_cm` / `bz_cm` (the hold's target) and `px_cm` / `pz_cm` (the rider); `step=near` (carrying within 2.5 m
  of the horse, 1 Hz: `d_cm`, the raw E, mounted). Body 1's readback yaw equals `byaw` (`c62e2ce6`: 729 / 729) [O].
- **Rider not yet in the saddle** (`e61febc7` / `a0a45c4a`): lab `990d2530` (a) logged the rider's own y 0.97 m under the
  census root right after the mount (`step=pin` `py_cm` 33751 / `hy_cm` 33848) with body 1 held 0.15 m over it, by the
  ground [O]; riding now uses the rider anchor only while the rider is at or over the census root, else the census
  cargo point (`how=unseated`).
- **Revive lines** (world_downed): `step=revive_wait` (the raw E held near a downed teammate while the revive does not
  take it: carry role, the record's age, generation, xz mm; on the host `host=1` with the actor's target generation and
  the 3 m reach) and since `a0a45c4a` `step=revive_near` (on foot within 5 m of a downed teammate, 1 Hz, 120 a mission:
  the raw E, the revive's held, its mask, xz, age, generation).
- **Scenarios**: the load E from `horse_flank` in all five parts (play `72e6269a`: `goto_horse` put the carrier on the
  census root, a fall / land, and E 900 ms gave no load); (b) lab and play from the boot spot (both teleported, the
  guest's horse called first, 12 s, a waitlog that it stands within about 25 m of the boot spot: it came in 3 of 6 runs, `ad7bbe2c`, `c62e2ce6`,
  `49a1c525`);
  the carry check takes the carry acts (acts 1 / 3 / 39, b1 0x2a..0x2c: `ad7bbe2c` / `c62e2ce6` (a) stopped there while the
  host was carrying); the victim's camera turned before the load frame (`mouse 400 300` looked straight down,
  `600 -200` level with the body; attempt 3 `600 80`); two more gallop holds in lab (a) (`990d2530` rode 37 m).
- **Hidden runs** (lab `horse-cargo.txt` (a) + `horse-cargo-b.txt` (b) + `horse-cargo-drop.txt` (c), play
  `horse-cargo-play.txt` (a) + `horse-cargo-play-b.txt` (b); `-WanSim bad`, the gun profile, research build, afgh-ledge;
  claims on both logs and frames looked at):
  - Attempt 1 (`225f213a`): lab `ad7bbe2c` and play `c62e2ce6` (a) stopped at the carry check while the host carried
    (above). Play `c62e2ce6` (b) **passed every require**: from the boot spot the guest carried the downed host, `step=near`
    then claim (77 cm) / grant / loaded, the first E mounted, a 178 m ride (`final_sent path_cm=17815`), the host's stance
    CRAWL in 20 `victim_pin` windows (worst lag 59 / drop 22 cm), the dismount, the take-down at the same x,z on both copies
    (63233,169665), the revive (`down=0`); no Unconscious or horse_hit line [O both logs]. Frames: on the carrier's screen
    across behind the rider at trot / gallop / gallop4 and across the rump at the stop and after the dismount, but at the
    LOAD the upper body raised upright on the rump (back to the camera) and at the walk hunched behind the rider; on the
    victim's own screen prone on the horse, the horse in view at the trot, gallop and after the dismount, not at walk /
    gallop4 / stop (camera straight down) [O].
  - Attempt 2 (`1a072dfd`): lab `990d2530` (a) and (b) ran to the revive: loads, first E, rides of 37 / 159 m, the
    victim CRAWL in 19 / 19 windows (worst drop 52 / 24 cm), the take-down at the same x,z (82979,163190 / 83833,178115),
    0 recon placements inside the cargo hold; then the 3 s revive did not start in either order (no grant, no
    `revive_wait`) [O]; (c) found the host still down. Carrier frames across behind the rider except (a)'s gallop frame:
    body 1 on the ground beside the galloping horse (no recon line then; the rider-under-the-root case above is the
    candidate [I]). Play `49a1c525`: (a) the stock pickup did not shoulder the guest; (b) the guest (still down from (a))
    never hit the host.
  - Attempt 3 (`6ab24575` + dev `60bc4a6a` merged, `31477fee`): lab `107977cb` (a): load, a 43 m ride (path_cm 4280, the
    horse stopped at a rock wall), the victim CRAWL in 22 windows (worst drop 12 cm), the take-down at the same x,z
    (83185,163333); every carrier frame of the ride and the stop shows body 1 across the horse behind the rider [O]; the
    revive did not start: `revive_near` on the carrier shows the revive mask set and `held=1` for about 3 s, then the
    victim's record 517 ms old (`age_ms=517`, over the revive's 500 ms), the mask dropped and the hold restarted as the E
    holds ran out [O host log] - under `-WanSim bad` one late status reset the 3 s hold. (b): the guest's horse did not
    come to the boot spot and the stock pickup did not shoulder the host. Play `2a41c416` (a) **passed every require**: load
    from the flank (`step=near` 84 / 76 cm), first E, a 44 m ride, the guest CRAWL in 20 windows (one window drop 203 cm
    across M1's final, as `a53a243a`: body 0 2 m under its target on the victim's own screen right after the dismount, 0
    in the next window [O log]), the take-down (the host's `step=unloaded` line lost: the host log has 1468
    `world=log_cap` lines dropping 1788 Lua lines through the whole session, the cargo hold included, so host-side
    absences in this log are weak evidence; the guest laid body 0 at the host's point, `from=partner`),
    the revive (`step=grant`, `down=0`,
    frame h2-revived both standing); on the victim's own screen lying on the horse with the horse in view at trot,
    gallop, gallop4, the stop and both frames after the dismount [O]. (b) the horse did not come and the host was not hit.
  - **Gate not passed** (three attempts used): never in one session both orders through the revive; the lab revive
    did not get through in 3 of 3 tries in the two lab sessions that reached it (`990d2530` (a) and (b): it did not
    start, [U] why, no near line then; `107977cb` (a): it started and was reset by world_downed's generic 500 ms
    freshness gate, not horse-specific); guest carrying passed only on pre-`a0a45c4a` code (`c62e2ce6`); rides in
    (a) 37-44 m (under 50 m); the load pose on the carrier's screen (upper body raised: the downed pose, act 5, its yaw =
    the hold's [O]); the victim's frames without the horse at walk and the load; the link drop (c) not reached this round.
    world_horse_cargo stays off unless named.
  - **Next:** hold E 6 s or more in the scenarios' revives (the 3 s hold restarts on one status over 500 ms old under
    `-WanSim bad`; the players' revive itself is world_downed / world_revive_input, unchanged); a longer straight (a)
    ride on open ground (turn away from the rock walls); run (b) before (a) or call the horse with more time (it came in 3
    of 6 boot-spot recalls); a scenario guard that revives / waits out a down left by a failed part (the cascade into the
    next part's downing and pickup); the pose at the load (try the A0 reset once at the first hold, or hold until the stock
    put-down's last act ends); then the play pair, (c), the flip and one run with the key not named.

## 16. Step S2: riding behind your partner at the defaults - (b) from the boot spot, a rock stop, the standing pose (not fixed), the downed rider (not reproduced)

Round `wp/horse` step S2 (no horse key named in any run: `pillion_status` `key=1; mode=6; shot=0` on both copies; markers
[O] / [I] / [U]; slim archives with the full session observer logs `%USERPROFILE%\mgs-coop-run-archive\horse\<run>`).
- **Order (b) from the boot spot** (`866cce5c` red / `c8209630` green, `9b7afee6` red / `79d467da` green): the lab /
  play debug command `horse_here` (`debug_buddy.lua` through `world_horse_share` `S.here`) puts this copy's unridden horse
  4 m right of the player with its census root 0.6 m over the player's feet (the `place` verb, not anchored; refused while
  either player rides it or with the key off; line `event=horse_share; step=here; placed=..`). Why: in `79287a27` /
  `1c2a6b0a` both copies answered `horse_spawn` with `submitted_horse_recall` + `horse_selected`, yet the horse stayed
  where (a) ended (0.3 m) [O]. Placed at the feet (`c8209630`), the root read 0.67 m higher a moment later (placed y
  339.09, census 339.76) [O]; the 0.6 m (`2a41c416`: the root 0.54 m over a player beside it) matches what the census
  reads. Corrected by the review (fixland3): that offset is not shown to be why the host's E did not mount in lab
  `41ae2b4c` part 3. Part 2 of the same run had the same 0.66-0.80 m census offsets and the same `a=0x19` fall after
  `goto_horse`'s warp, and both mounts worked (the guest reached `a=0x79`); what differed in part 3 was a 2.4 s `a=0x1a`
  state when E was pressed 2.5 s after the warp [O]. Why E did not mount there is [U]; the 0.6 m root was never tried in
  that part (attempt 2's part 3 ended in the cliff fall). `horse-seat-b.txt` now teleports both players to the
  boot spot facing -60 and runs `horse_here` on both copies: (b) began on open ground in all four runs (the driver's
  `goto_horse` census 803.4 / 1642.1 against the boot spot 805.0 / 1636.5; `step=here; placed=1` on both copies) and rode
  129-140 m (`final_sent path_cm` 13998 / 12908 / 13232 / 13168) [O].
- **The rock stop** (`horse-seat-a.txt`, `-play-a.txt`, `-b.txt`): after the gallops the driver gallops on about 9 s
  (W+Shift, then W) toward the rock walls; frames of the push and the stop, side on. `step=stall` in lab `41ae2b4c` (a)
  (ms 2035) and play `186c02d8` (b) (ms 2185); in the other orders the horse stood pressed on rock in the frames with
  `step=drive; moved_cm=0` and the ride keys held, but no `stall` line inside the 3 s waitlog [O].
- **The standing pose: reproduced, cause not found.** `partner_pillion=stats` (driver's copy, 1 Hz while body 1 rides
  behind) now also logs body 1's act / stance / speed / q28 / qd8 and upper act, the overlay phase, body 1's DM record as
  the overlay last read it (q18, ps2d8, rec10 / 11 / 18 / 1a0 / 248), the seat grace ticks used and the pose flag
  (`c8209630`, `228d598c`). Frames looked at, the driver's screen: body 1 STANDING on the rump behind the seated driver
  - order (b) (the guest drives) from the seating on, on open ground before any ride, in all four (b) runs:
    lab `41ae2b4c` / `c1982534` and play `b228d163` `guest-h3b-idle-side-guest` (79287a27's (b) idle frame had it seated);
  - order (a) (the host drives) seated at idle and after the gallops (`host-h3-idle-side-host`,
    `host-h3-stopped-side-host`), standing from the rock push on (lab `41ae2b4c` `host-s2a-rock1-host-01`, 2.7 s after the
    seated stopped frame, `host-s2a-rock-side-host`; play `b228d163` `host-s2a-rock-side-host`: a boot planted on the
    rump) [O].
  Body 1's anim and DM fields do not change with it: the stats windows of those seats read `act=139; st=1; sp=0; q28=0;
  qd8=65535; up=0; ov=playing; dm=1; q18=0x79,0x8b; ps2d8=5; rec10=1; rec11=0; rec18=0xf241f3bcf60b; rec1a0=0;
  rec248=0x02; pose=1` [O logs lab `41ae2b4c` host part 1 lines 2416-2887 and guest part 2]. Corrected by the review
  (fixland3): the windows are not dy 0. The stats line is written only for a window with seat error, and on the driver's
  copy ride windows at the final head reach xz 635 cm (`186c02d8` guest part 2 line 1292), 564 cm (`186c02d8` host part 1
  line 4586), 535 cm with dy 161 (`c1982534` guest part 2 line 924) and 391 cm with dy -109 (`c1982534` host part 1 line 3036); the first seated windows read
  dy -167 / -173; one window, at the (b) seating in `186c02d8` (line 860), has `rec248=0x12` with `grace=2`; there are
  also `ov=idle` and `stop_sent` windows [O logs]. In the frame from the 635 cm second (`186c02d8`
  `guest-h3b-gallop2-guest-02`, t 388077296) and in `c1982534` `guest-h3b-gallop2-guest-03`, body 1 stands in the air
  beside and behind the rearing horse at the rock wall, legs straight, not on the horse [O, looked at in fixland3]. So
  the standing on the rump is none of the DM record, the act, the stance, the upper act or the pose flag; that the DM
  ride pose stops showing on a
  body whose DM act stays 139 is [I]; why is [U] (0xf241f3bcf60b is also a RideHorse motion code: FUN_141236940 state 9).
  The holds stay on. Corrected by the review (fixland3): in `186c02d8` (b) the first seated window, where the standing
  began, logged `grace=2` (the 2-tick lease grace kept a lapsed lease), so a hold did act there - the first logged sign
  of one acting; but `41ae2b4c`, `b228d163` and `c1982534` stood from the same window with `grace=0`, so the standing is
  not the holds' doing [O logs]; what the hold changed on screen in `186c02d8` is [U].
- **The downed rider: not reproduced** (two attempts; `horse-down-guard.txt`, (c2) passenger then (c1) driver, guards
  not friendly fire). Attempt 1 (lab `41ae2b4c`, play `b228d163`): seat-a's `clear_enemies 100` left the nearest guard
  354 m away, so `spawn_enemy here come` routed him (`mode=route; why=far`), and the host's E did not mount (above). Attempt 2
  (lab `c1982534`, play `186c02d8`): `spawn_enemy here` warped another guard beside the host (d 5.1 m), and `enemy goto
  2164161801 10` took both players to a cliff where both fell (`downed_damage=lethal_capture; damage=10000`, parts 8 /
  10; `step=both_downed`; `horse_here` y 150.9 / 214.7 m lab, -48.7 / -79.1 m play) before any mount [O]. Static (Ghidra comment at `0x14122EB30`, [I]): the RideHorse state table
  getter; on the get-off check the callers set the player's ride record (stride 0x2B0 at `*(this+0x68)`) `+0x11 = 3`,
  `+0x235 = 0` and `*(rec+0x18)+0x82 = 6`; `*(rec+0x18)` carries the rider id at `+0x7C`; a byte scan of `.xtext` found no
  immediate write to `+0x16E` and five register writes in other structures, so the `+0x16E` writer is still unknown.
- **Horse log lines** (`0b4263d2` red / `38c3c705` green): `log_budget::HorseLine` (`world_lua=event=horse_...` /
  `world_lua=horse_...`) takes its own bucket (300 burst, 10 / s) in world_link's LOG receipt; play `b228d163`'s host had
  441 `world=log_cap` lines under the default lrrp_follow flood and lost the horse's `first_move` / `stall` lines.
  Play `186c02d8` (the final head): the host's log again had 441 `world=log_cap` lines, yet its pillion `drive` lines ran
  without a gap over 2.6 s and `first_move` was there (`b228d163`: a 12 s gap, no `first_move`) [O both host logs].
- **Gate not passed** (the standing pose, the downed rider). Seen at the final head `38c3c705` with no key named, both
  orders, lab `c1982534` and play `186c02d8`: every first E at an unridden horse mounted (the driver's in (a) and (b), the
  guest's at the end of (a)); rides of 135 / 132 m (lab) and 180 / 132 m (play); the driver's E after the hop-off got
  them off 2.6-3.4 s after the seat ended (`driver_e` 3103 / 3445 / 3253 / 2619 ms, `driver_e_off` 17-25 ms later); the
  passenger's seat_pin windows 0/0 after the seating second except one dy 52 cm window in lab (a) and in play (a) [O both
  logs]. In lab `c1982534` (a) the guest's E during the walk-away step already claimed the seat (2.71 m: the 900 ms walk
  did not pass the 2.8 m release), so that part's `e_free` / claim waitlogs failed while the seat itself was granted
  [O]. On the driver's screen body 1 stood in (b) from the seating and in (a) at the rock (above), so the gate's "seated,
  not standing" is not met.
- **Next:** the standing pose - find what shows a DM-seated body 1 standing while its DM act and record stay put: diff
  body 1's motion graph state (the instance-1 counterparts of Q+0x1D8 / 0x1E0 / 0x1F8, which `ReadAnim` hashes for
  instance 0 only) between a seated and a standing frame, and try once a DM replay of stateRideOnIdle while standing (the
  overlay's own `lost` path); compare (b)'s seat entry with 79287a27's (that one sat: the guest faced 119 there, and the
  horse stood where (a) ended); the downed rider - where (b) ends `spawn_enemy here come` already warps a guard 5 m from
  the host (attempt 2: d 5.1 / 5.4 m), so drop the `enemy goto` (it sent both players off a cliff; done in fixland3: every
  guard op names `nearest`, not run yet) and run `horse_here`
  and the mounts there, the rider's godmode off just before the guard turns hostile; then the forced get-off by the ride
  record (`+0x11 = 3`, the state table getter's vtable to find the instance).

## 17. M2i round: the revive after the take-down (late-status drain), the boot-spot route, the load pose (A0 once)

Round `wp/horse` M2i (world_horse_cargo named, off unless named; markers [O] / [I] / [U]; slim archives with the full
session observer logs `%USERPROFILE%\mgs-coop-run-archive\horse\<run>`).
- **Why the lab revive failed** (`107977cb` host log, the carrier reviving): `step=revive_near` read `held=1; mask=1`
  at t 381682646 / 381683646 / 381684996, then `age_ms=517; mask=0; held=0` at 381685766 and no grant [O: the lines].
  Corrected by the review (fixland3): the cause is [I], not [O] - the end of the 3.4 s E and the 517 ms-late sample fall
  in the same 1 Hz sample, so the samples cannot tell the player's release from the stale reset. Two candidate causes:
  the scenarios' E (four `hold e 1500 nowait` 850 ms apart, released 3.4 s after the first) barely covered the 3 s
  hold, and world_downed's host gate (`not fresh -> resetHold()`) zeroed a hold under way when one status came over
  500 ms late. Also [I from code]: the MGO drain (`releaseHold`) survived one host tick only, and world_revive_input's
  own 500 ms gate (not editable) drops the reviver's held while the target's record is late, so a guest reviver's late
  view (its status naming no target) restarted the hold too. `990d2530` logged nothing then (no `revive_near` yet): the
  same E length; that run's cause stays [U].
- **Late-status drain** (`2bb390c3` red / `390b1984` green, world_downed only): a record past FRESH (still ready, time
  not reversed) or the actor's status naming no target marks `D.lateAt`; the host's stale branch now releases the hold
  as a brief release does (the MGO drain at the fill rate) and `releaseHold` keeps that drain within 1 s of a late
  mark, so the same reviver resumes from what is left. Unchanged: not-ready records, a time reversal and a loop gap
  still reset; a multi-tick release with fresh statuses still restarts (guard check). Lines `step=revive_late`
  (progress, age) and `step=revive_resume` (the resumed progress). Lua checks: downed 174662, revive input 728.
  fixland3 (the review: it changed every revive at the defaults and no game ran it): `8b3147b0` red / `611a1195` green -
  the drain is on only while `W.horseCargo.keyOn()` is true (world_horse_pillion's share key with world_horse_cargo
  named); at the defaults a late status resets every revive exactly as before `390b1984` (downed checks 176304).
- **Scenarios** (`69141e5c`): all five cargo parts teleport both players to the boot spot facing -60 and run
  `horse_here` on both copies (the recall left the horse elsewhere in 3 of 6 (b) runs; (a) rode 37-44 m into rock
  walls); the rides take step S2's (b) route (walk, a left trot, four straight gallops, two left gallops; 129-140 m
  there) without the right turn; before the revive the reviver's copy moves its horse 4 m away (`horse_here`, no mount
  on E after the revive) and E is held about 5.5 s without a gap (overlapping `nowait` holds); the victim's camera
  `mouse 600 150` before the load frame; (c) also waits for the victim's own `why=stream_lost` unload and frames the
  drop from the side.
- **The load pose** (`13f76dec` red / `d602df54` green): `2a41c416` host log: body 1 `act=5; s=3` with the readback
  yaw equal to the hold's (`byaw` 1354) from the load through the ride and after the dismount [O], yet frame
  `host-h2-loaded` shows it along the horse with its upper body raised and `host-h2-stays` across the rump [O]. With
  world_horse_cargo the hold1 receipt now adds Warp's A0 reset (`A0.vt+0x80`, which re-applies the stance) once per
  hold1 run, on its first hold1 1 s or more in, only under the A0 class guard (else skipped); line
  `horse_drive=a0_once; body=1; applied=..`.
- **The 2 m drop** [I]: one `victim_pin` window in six cargo sessions (`2a41c416` guest, t 381970693, `drop_cm=203`, 63
  samples), never in a frame; that window holds M1's last anchored track placements while body 1 (the carrier) still
  played its get-off act on the victim's copy (act 122 until about 1 s after the final), and the anchor may move the
  horse up to 1.5 m (`kAnchorMaxM`). M1's tracking is on by default; not changed this round.
- **Hidden runs: none finished this round.** From 21:36 to 22:20 C: fell to 0 bytes free (an Unreal Engine build was
  writing to it), then the queue's floors (C: 6 GB, commit free 15 GB) held the enemy jobs ahead of this round's; the
  attempt-1 jobs were queued at 22:21 at the code head `d602df54` (lab `20261106-042115-789-d89b21`: `horse-cargo.txt`,
  `horse-cargo-b.txt`, `horse-cargo-drop.txt`; play `20261106-042116-245-e164ac`: `horse-cargo-play.txt`,
  `horse-cargo-play-b.txt`; `-WanSim bad`, gun profile, research build, afgh-ledge) and neither had started by 23:07 (C: 4.2 GB free again and falling, three enemy jobs ahead); the queue runs them when the floors allow, on the worktree as it is then.
- **Gate not passed** (not run): nothing above is seen live yet - the late-status drain, the boot-spot route in (a)
  and (b), the 5.5 s revive, the A0 reset at the load and the victim's stream_lost unload in (c) are [I] until a run
  shows them on both copies. world_horse_cargo stays off unless named; no default change.
- **Next:** read those two queued jobs' results if they ran (the queue runs a job's repo at run time: it tests the
  worktree as it is then), else run them: per part both observer logs (`step=revive_late` / `revive_resume` /
  `event=downed; step=grant`, `final_sent path_cm` over 5000 in (a) and (b), `horse_drive=a0_once; applied=1`, the
  victim's `why=stream_lost; side=cargo`, 0 `partner_recon=place` inside the cargo hold, no `Unconscious` / fall line)
  and the frames (load, walk, trot, gallop, gallop4, stop, stays, stays2 on both screens with the horse in view; the
  A0 load frame on the carrier's screen; `h2c-dropped` / `-side`); then the play pair; on a pass the flip
  (Settings default with the gun profile, red / green) and one lab + play run with the key not named.

## 18. fixland3: the review's corrections before the land

Round `wp/horse` fixland3 (no hidden run; markers [O] / [I] / [U]).
- **Turned back off unless named:** the late-status revive drain (`390b1984`) reached every revive at the defaults with
  no game run; it now needs world_horse_cargo (section 17). The A0 reset at the load (`d602df54`) was already
  world_horse_cargo only. At the defaults the horse code that runs is that of `38c3c705`, where lab `c1982534` / play
  `186c02d8` ran with no horse key named [I from the diffs: every later horse code change is behind world_horse_cargo];
  there is no run at the landed head itself.
- **Corrected** (sections 16 and 17): the 0.6 m root is not the shown fix for `41ae2b4c` part 3 (an `a=0x1a` state at
  the press, cause [U]); the pillion stats windows are not dy 0 (driver-copy windows to xz 6.35 m, dy -1.73..+1.61 m;
  body 1 in the air beside the rearing horse at a rock wall in (b), README limit added); a seat hold logged acting once
  (`186c02d8` (b), `grace=2`) where the standing began, while three other runs stood with `grace=0`; `107977cb`'s revive
  cause is [I].
- **Scenario:** `horse-down-guard.txt` names the nearest guard (`enemy <op> nearest`) instead of the hardcoded
  2164161801 that `spawn_enemy here` no longer picks, and drops the first `enemy goto` (both validate ok; not run).
- **Left as they are** (log or lab only): the stats line calls `pp::ReadUpper` directly, not `h.read_upper` (a guarded
  read that only logs, so test fakes cannot drive it); `pillion_grace_n` carries over into the next seat when a seat ends
  without an error window (the stats line only); the static `HoldRun` in `SelectHorseDriveReceipt` is not reset per
  mission or load (world_horse_cargo only).
- **Never seen live:** a rider downed while riding (`stop(passenger_downed)`, the downed rider ending on the ground, a
  revive on both copies), the late-status drain, the A0 reset at the load, the cargo boot-spot route in (a). A rider
  downed while riding can still stay on the horse (not fixed; README).

## 19. Step S3: the standing rear rider (order test, body 1's records), the downed rider's get-off (world_horse_down_off)

Round `wp/horse` step S3 (markers [O] / [I] / [U]; slim archives with the full session observer logs
`%USERPROFILE%\mgs-coop-run-archive\horse\<run>`).
- **Seat entries compared (old runs):** the overlay's request / play lines, the native mode, body 1's act at the play
  and the remote act at the (b) seating are the same in the seated round-6 run `79287a27` and the standing S2 runs (for
  example `c1982534` (b): `native=active`, `body_act=25` (a fall), `remote_act=3`, `rec18=0x0; code_match=0` at the
  confirm, as in `79287a27`); so are the native gun / aim / support lines after it [O logs]. A candidate that fits every
  run so far [I]: the standing begins at the first rock push on a copy and stays. S2's (b) ran after (a)'s deliberate
  rock push (added in S2), and round 6's (b) ran after an (a) without one and sat until its own stall.
- **Order test** (`horse-s3-b-first.txt` / `horse-s3-play-b-first.txt`, then `horse-s3-a-after.txt`): (b) first in the
  session from the boot spot (horse_here on both copies), frames at idle / walk / trot / gallop, the rock push, then a
  DM replay of stateRideOnIdle while body 1 may stand (`overlay_stop` on the driver's copy, then the seat holds'
  re-entry plays it again), the hop-off and the driver's E; then (a) from the boot spot, whose host copy's body 1 was the
  drawn driver through (b)'s rock push, with a replay at idle and after its own rock push.
- **RideHorse ride records** (static [I], Ghidra comments at `0x14122B610` and `0x14122DA20`): the action's
  constructor `0x14122B610` sets the vtables `0x1423CB600` (obj), `0x1423CB680` (obj+0x10, what the factory
  `0x14122CF80` returns) and `0x1423CB6B0` (obj+0x30, the per-player event dispatcher `0x14122DEF0`); obj+8 is the
  player block (block+0x138 = P), obj+0x38 an id node (+0x24 the base id), obj+0x68 the records (0x2B0 each). The block
  constructor (`0x1409BAC80`) clears two 0x45-slot arrays at block+0x190 / +0x3B8 (its actions). The per-player update
  `0x14122DA20` runs `0x14122DDA0`, which enters the record's next state (+0x11) when it is set and differs from +0x10
  (or +0x235 is set); state 3 (`0x141236100`) is the get-off (act 0x7A, a side pick, its motion through vt+0x190). The
  stock get-off in ride state 5 (`0x141236B8D`) writes sub+0x82 = 6 (when sub+0x7C names the rider), +0x11 = 3 and
  +0x235 = 0. Fifteen ride states call the get-off check `0x14122F3F0` (5, 6/7, 8, 9, 10..19, 29).
- **Code** (`e838d134` red: horse_drive_tests did not compile, world_horse_share_lua_check failed check 67 /
  `4f55fe8d` green; contract `tools/generate_horse_ride_contract.py`, `tools/horse_ride_sites.json`, 4 sites and 3 slots,
  its own refusal `ride_pins` so a miss never touches the shared horse's pins):
  - `MGS_COOP_HORSE_DRIVE_V1 rrec` (world_horse_share's Lua, 4 Hz on both copies): both bodies' ride records (the action
    found in each body's block by its vtable), logged change-only as `horse_drive=ride_rec; body; ok; why; st; nx; again;
    sub; rider; sel; forced; id` (reads only, at the defaults).
  - `getoff` (lab key `world_horse_down_off`, off unless named, with world_horse_share): when this copy's player rides
    the shared horse (census rider 0) and world_downed has it down, world_horse_share asks up to 6 times 500 ms apart;
    native re-reads body 0's record, refuses unread / idle / a state without the stock get-off check / a next state
    already set, and writes the stock get-off's three bytes; lines `horse_drive=getoff; ok; why; st; nx; rider; sel; id`
    and `event=horse_share; step=getoff; n; ok; reason`. The rear-seat passenger downed while seated is laid off by
    world_horse_pillion's `stop("passenger_downed")` as before (no native mount).
  - `partner_pillion=mg` (logs only): on the driver's copy, next to the stats window, body 1's part of the motion
    records ReadAnim hashes for instance 0 (`*(Q+0x1D8)` + 0x40 (0x40 bytes), `*(Q+0x1E0)` and `*(Q+0x1F8)` 0x40 bytes each,
    their instance-1 stride not known) as raw hex whenever their hash changes (120 lines a process).
- **Hidden runs** (attempt 1, code head `4f55fe8d`, no horse key named except `world_horse_down_off` in part 3's
  labsetting, which applies to the whole session but acts only on a rider downed while riding, so parts 1 and 2 ran as
  with no key [I]; `-WanSim bad`, the gun profile, research build, afgh-ledge). The first submission (lab `6e2677`, play
  `911a74`) ended in both preflights on `worker_session_tests` (the new key was missing from Start-WorkerSession's
  allow-list; no game ran, not counted). Lab `a37b0a4a` and play `cc2e94c6`:
  - **The standing is not carried over from a rock push**: with (b) first in the session, before any rock push, the
    driver's screen shows body 1 STANDING on the rump from the seating (lab and play `guest-h3b-idle-side-guest`), and in
    (a) after it the host's screen shows the same (lab and play `host-h3-idle-side-host`) [O frames looked at]. What every
    standing-from-the-seating run shares (S2's four (b) runs and these four): the horse was put beside the players by
    the lab command `horse_here` (the `place` verb). Where the horse came by the stock call (S2's (a): `c1982534`
    `host-h3-idle-side-host`, looked at again: seated) or stood where it was left (`79287a27` (b)), body 1 sat at idle and
    stood only from a rock push or stall on [O frames]. So the standing from the seating comes with a horse placed by
    `horse_here` [I], a lab command players do not have; players get a horse by calling it, so on their screens the rear
    rider [I] sits until the horse is pushed into a rock wall (S2: `41ae2b4c` / `b228d163` (a), `79287a27` (b)). No run
    used 0.15.3's code (no seat holds): the holds were on in every one of these runs, and they logged acting once
    (`186c02d8`, `grace=2`) where the others stood with `grace=0` (section 16), so that the holds play no part is [I].
  - **Body 1's ride record stays idle**: `horse_drive=ride_rec; body=1` reads `st=0; sub=0` once and never changes on
    either copy in either run (one line per process), while body 0's follows the ride (2 get on, 5 idle, 9 / 10 / 11 /
    16 / 18 / 19 the gaits and turns, 3 the get-off with `sel=6`, 0 after) [O logs, lab `a37b0a4a` guest part 1 lines
    1960-3864 and host part 2 lines 506-2784]. So the RideHorse action never runs on body 1, and the standing is not its doing [I].
  - **Body 1's Q records carry no motion state**: one `partner_pillion=mg` line per process, the same bytes in all four
    (`h=66a3906b,9868c3e1,f0631ba4`: a pointer and zeros in each record), never changing through the seat, the gaits and
    the rock push [O logs]. Where the motion graph keeps the displayed node is still [U].
  - **The DM replay was not tried**: `overlay_stop` is refused while the driver rides (`on_foot`, debug_player) on both
    copies, so no frame shows a replay [O].
  - Every first E at an unridden horse mounted (the guest's in (b), the host's in (a)); the driver's E after the hop-off
    got the driver off in (b) lab / play (`driver_e_off` 2469 / 3470 ms) and in (a) play (2502 ms); in lab (a) the
    passenger's hop-off E did not take and the host's E got both off (`why=driver_off`) [O logs].
  - **The downed rider: not reproduced** (part 3, where S2's (b) ended): `spawn_enemy here come` warped a guard 5.2 / 5.1 m
    from the host (lab / play; no cliff this time), `horse_here` placed both horses, and then the host's E after
    `goto_horse` did not mount in either run (`host require localMounted=1`), as in S2's `41ae2b4c` part 3 (cause [U];
    both are horse_here horses with a guard coming). So `getoff` never ran live; `world_horse_down_off` stays off unless
    named.
- **Attempt 2** (lab `e3f8297d`, same code `4f55fe8d`, scenarios `horse-s3-b-called.txt` / `horse-s3-a-called.txt`: horses
  called by `horse_spawn` only, no `horse_here`, no teleport; world_horse_down_off named for the session): order (b)
  with a called horse - the first (b) with one in these tests - shows body 1 SEATED behind the driver at idle on the
  driver's screen (`guest-h3b-idle-side-guest`) and STANDING on the horse's back after the gallops, when the horse had run
  into a tree (`guest-h3b-gallop2-guest-02` the horse rearing at the trunk, `-gallopfree-guest-01` / `-03`,
  `guest-h3b-stopped-side-guest`; contact sheet `sheets/s3-a2-b-guest.jpg`) [O frames looked at]. The stats windows of
  that seat read the same fields seated and standing (`act=139; st=1; ov=playing; dm=1; rec18=0xf241f3bcf60b;
  rec248=0x02; pose=1`), with two windows of 5.6 / 5.7 m seat error at n = 25 / 12 ticks during the gallops (guest part 1
  lines 2254 / 2365) and `grace=1` in three windows; body 1's ride record stayed `st=0` [O logs]. That the standing starts
  at such a spike (body 1 moved metres for a tick while the horse rears) is a candidate [I]. The first E at the called
  horse mounted; the hop-off and the driver's E got both off (`driver_e_off` 3036 ms). (c1) did not down the driver:
  `guest debug godmode 0` did not take while the guest rode (`status ... god=1`), and the guard was 321 m away
  (`clear_enemies 100` at the start: `mode=route; why=far`), so `getoff` never ran. Part 2 stopped at the host's first E
  (`host require localMounted=1`) on the horse where part 1 left it, by the trees [O].
- **Attempt 3** (lab `57a6c1ad`, `horse-s3-down2.txt`, world_horse_down_off named): both players where S2's (b) ended,
  the horses called there, the guest's godmode off on foot (`god=0` this time), then `goto_horse` warped the guest to the
  horse's census root (878.37 / 1722.13) and the guest's E 2 s later did not mount (`guest require localMounted=1`),
  before any guard or frame [O log]. Every failed first E of these parts (S2 `41ae2b4c` part 3, S3 lab / play part 3,
  `e3f8297d` part 2, `57a6c1ad`) was at a horse standing among the rocks and trees there, after `goto_horse`; at the boot
  spot it mounted every time [O]; why is [U].
- **Gate not passed:** the rear rider stands on the driver's screen from the seating with a horse_here horse in both
  orders and, with a called horse, from the moment the horse runs into a tree or a rock; nothing logged changes with it
  yet; the downed rider was not reproduced (five tries over S2 and S3; this step's three attempts are used).
- **Next:** start both orders with a horse the players called (the stock recall; for (b) after (a) move the players, not
  the horse: M1's final keeps the horse where (a) left it), so body 1 sits at idle, then a rock push: a seated and a
  standing window in one seat. Diff body 1's motion state there from a source other than the Q records (the motion
  controller that `0x141230B30(ctl, idx, 0, node)` plays nodes on, per index), and trigger the DM replay by a lab key
  (the overlay's stop + re-entry after a `step=stall`) instead of `overlay_stop`, which is refused while mounted. For the
  horse_here horse, compare what `place` leaves different from a recalled horse (census record, its CS slot) with
  `horse_drive=ride_rec` of body 0. The downed rider: mount first (a recalled horse, away from the guard), then
  `spawn_enemy here come` from the saddle is refused (debug commands while mounted), so let the player on foot make the
  guard hostile with no `clear_enemies` before it, mount on open ground (the boot spot: every first E there mounted;
  `spawn_enemy here` warps the nearest guard only within 250 m, so check `enemy info nearest` there first), and turn the
  rider's godmode off BEFORE the rider mounts (attempt 2:
  `debug godmode 0` did not take while riding); then `getoff` live with world_horse_down_off named. For the standing:
  log body 1's position each tick around the 5-6 m seat-error windows and the overlay's DM state then, and try the DM
  replay after such a window.

## 20. M2j round: a downed partner on the horse at the code head - both orders through the revive, the link drop; the victim's own walk / trot frames (not passed)

Round `wp/horse` M2j (world_horse_cargo named, off unless named; markers [O] / [I] / [U]; slim archives with the full
session observer logs `%USERPROFILE%\mgs-coop-run-archive\horse\<run>`).
- **Round 7's leftover jobs** (ran on the tree of `2044e31d` + the S3 scenarios, no S3 code): lab `3a86c82f` passed
  `horse-cargo.txt` (ride 134 m, take-down at 89210,172784 on both copies, revive grant) and `horse-cargo-b.txt` (128
  m, 88148,173392, the host up again); its (c) failed only expectations: with the guest's (the victim's) world off its
  cargo ended by the context hook (`why=context; from=partner; placed=1`, 3 cm from the host's point) and world_downed
  reset it (`reason=restored`), so a `stream_lost` on the victim's copy cannot happen that way. Play `7c1de731`: (a)
  passed (145 m); (b) never downed the host: the guest's hits on body 1 did 0 native damage (`downed_damage
  damage=0.0`, lab 942.0), as in `2a41c416` and `49a1c525` (b), each after a teammate revive of the guest in (a);
  `c62e2ce6` (b), with no revive in (a), did 942 [O logs]. Cause [U].
- **New defect, fixed:** `3a86c82f` (a) frames `host-h2-stays` / `-stays2` (the carrier's screen after the dismount by a
  rock wall) show body 1 lying flat in the air over the horse [O]; `step=pin how=ground`: the census root stood only
  0.15 m over the carrier's ground there, so the 0.8 m low edge of the on-foot window moved body 1 to the carrier's
  ground + 1.35 m, 0.75 m over the census cargo point [O log, cause I]. `cb33c317` red / `3d0e9549` green: on foot
  within 3 m the census cargo point is corrected only when it is more than 2.0 m over the carrier's ground (run
  `7359e4d2`); no low edge (`5a92c73f` had drawn it 0.38 m high the same way). Since then every carrier stays frame
  shows body 1 lying across the horse's back (`01d9924e`, `00c966c1`, `619d982c`, `f7871604`) [O], `how=census` [O].
- **The anchor during the get-off** (`3d0e9549`, `horse_drive::AnchorNote`): with Settings.horse_cargo the rider body
  anchors no horse place while body 1 plays act 122 (the get-off, which holds a shown ride), so the stream pose places
  the horse while the carrier gets off; line `horse_drive=anchor_hold; on=0|1`. Live [O]: `on=1` at every act-122 edge
  on the victim's copy (2-3 per session); after the dismount no victim_pin window over 50 cm in the 5 sessions with it. One window
  with `drop_cm=185` came at a ride START (lab `01d9924e` (b), host's copy, `src=placed`), not after the dismount [O];
  cause [U] (one sample of body 0 under a target that had just moved up, or a real drop: no frame then).
- **The revive** (`5272714c` red / `6b5518fa` green): lab `01d9924e` (a): `revive_near` `held=1` in the 1 Hz samples
  371.5-373.5 s, then `age_ms=534; mask=0; held=0` and no `revive_late` line, so the hold had been zeroed before a late
  tick could drain it; candidate [I]: a host loop gap over 500 ms (D.tick's `loop_gap` reset). With world_horse_cargo a
  loop gap now drains the hold as a late status does; any reset of a hold under way there logs
  `step=revive_reset;why=..;progress=..;gap_ms=..;drain=0|1` (at the defaults the reset is unchanged, the line is new).
  Never seen live: no `revive_reset` in the later runs. The scenarios hold E about 7.5 s.
- **The load E** (`6b5518fa`): an E begun while carrying that ends under 450 ms logs `step=load_e`, one whose carry
  ended by then `step=refuse;why=carry` (lab `01d9924e` (c) and play `403e46a2` (b) logged nothing at the failed
  loads). Play `403e46a2` (b): the flank warp during the pickup act 27 ended the guest's carry (`coop_carry=tx;
  event=2` right after the warp) [O]; the scenarios now wait 2 s after the pickup. Neither line seen live yet.
- **Scenarios:** (c) `host worldtrigger world=0` (the carrier's world off: carrier `why=context`, the victim's copy,
  world on, `why=stream_lost`); play session `horse-cargo-play-b1.txt` ((b) first, with the boot: the guest's rifle
  downed the host in both play runs) + `horse-cargo-play-a2.txt`; `horse-cargo-drop1.txt`: (c) alone from the boot (as
  the third part it failed twice before the link drop: the load E in `01d9924e`, the pickup R in `00c966c1`).
- **Hidden runs** (`-WanSim bad`, gun profile, research build, afgh-ledge; logs both copies, frames looked at):
  - Attempt 1 (code `3d0e9549`): lab `01d9924e` (a) through the take-down, the revive reset (above); (b) passed; (c) no
    load. Play `403e46a2`: (b) no load (the carry ended at the flank warp), (a) passed (162 m, revive grant).
  - Attempt 2 (code `6b5518fa`, the code head): lab `00c966c1` (a) **passed** (load, a0_once, ride `path_cm=14109`,
    take-down at 90211,172475 on both copies, revive `step=grant`), (b) **passed** (130 m, 89572,172069 both, the host
    up again); victim CRAWL in 17 / 17 windows, worst drop 12 / 36 cm; 0 `partner_recon=place` inside the hold; (c) the
    pickup R started nothing. Play `619d982c` (b) **passed** (124 m, 88934,171763 both, the host up again) and (a)
    **passed** (135 m, 89220 / 89221,172778, revive grant); one recon place (`reason=teleport`) 13 lines after
    partner_recon's own `cargo_hold; on=0`, the take-down's lay-down. 0 `Unconscious` / `horse_hit` lines on any copy.
    The play host logs dropped Lua lines (`world=log_cap`: 954 lines, 1270 Lua lines in (b)): the host's downed
    `step=grant` line of (b) is lost; its `require down=0` passed.
  - Attempt 3 (code `6b5518fa`): lab `f7871604` (c) alone **passed**: load, a walk / trot ride, the dismount (body 1 on
    the horse on both screens), then the carrier's world off: carrier `why=context; placed=1` at 81118,165080, victim
    `why=stream_lost; placed=1` at 81147,165089 (0.3 m), the victim still down (`state=wounded`); frames
    `host-h2c-dropped-side` and `guest-h2c-dropped-side` both show them lying on the ground beside the horse [O].
  - Frames looked at (attempt 2, both orders, lab and play): the carrier's screen shows the body across the horse at
    the load, walk, trot, gallop, gallop4, the stop, both stays frames, and the revived partner standing [O]; on the
    victim's own screen the horse is in view at the gallop, the stop and both stays frames; at the trot only at the
    frame's edge in 2 of 5 (`619d982c` `guest-h2-ride-trot-victim`: the mane and saddle under the body; `00c966c1`
    `host-h2b-ride-trot-victim`: the mane's tip), at the walk in none of 5 (`guest-h2-ride-walk-victim` /
    `host-h2b-ride-walk-victim` in `00c966c1` and `619d982c`, `guest-h2p-walk` in `f7871604`: the body lying in the
    air with the rider's head below it), nor at the load [O]; logs at those moments: body 0 held at the cargo point,
    CRAWL [O].
- **Gate not passed, by its frame rule only:** everything else holds at the code head `6b5518fa`: both carrier orders
  in lab and play through the ride (124-141 m), the dismount, the take-down at the same spot on both copies and the
  revive; the link drop lays them down at the same spot on both screens, still down; no horse fell; 0 recon places
  inside the hold. Missing: frames from the victim's own copy that show the horse at the walk (0 of 5) and plainly at
  the trot (2 of 5, at the frame's edge). Three
  attempts used; world_horse_cargo stays off unless named; no default change.
- **Never seen live:** the loop-gap drain (`revive_reset`), `load_e` / `refuse;why=carry`, a late-status drain
  (`revive_late`), a 2 m drop after the dismount with the anchor held (none in 5 sessions; one 185 cm sample at a ride
  start).
- **Next:** on the victim's copy turn the camera along the horse before the walk and the trot captures (as the load
  frame's `mouse 600 150` turn, then a little down) or add a side capture per gait, so the horse is in the frame; or
  the user accepts the carrier-screen frames plus the victim's gallop / stop / stays frames and the logs for the walk
  and the trot. Then the flip (lab_config `world_horse_cargo = gun`, red / green) and one lab + play run with the key
  not named that includes an ordinary revive: with the key on by default the late-status and loop-gap drains reach
  every revive.

## Risks

- **RIDE_BODY moved the viewer's own buddy** (section 2) when both players deployed the same mount type. Observed
  (section 9, all four runs): the viewer's own idle D-Horse leapt and hung in the air around the partner, and the
  partner's first E press found no horse to mount (its horse was pulled the same way). **Resolved by the user
  decision of 2026-10-22 (turn it off):** a horse or walker pose now applies only to a registered mirror, and none
  is registered (section 2). Confirmed in `2ab88411` (lab) and `5a044a4d` (play): in `partner-mount.txt` Part 3
  (both D-Horse) the guest's own horse stays where CallBuddy put it and the host's first E press mounts its own
  horse, with one `ride_body; action=refuse; reason=not_mirror` line per mount id on each copy. A two-mount world
  (both players riding at once, each seeing the other's mount) still needs section 7.
- **The other ride hides still only clear the bits.** Without the key, a horse rider's kind-3 hide leaves the partner
  drawn as a standing body at the spot where it got on (section 9, `251abcef`); the heli and vehicle-fallback ride
  hides use the same bit clear and presumably look the same (not filmed). 0.13.6 (G5) adds
  `partner_native_unrealize_all` (lab only, default 0; docs/native-puppet.md "Ride hides"): the same unrealize for
  those hides (the hidden ride no longer counts as a net-quality gap with or without the key since the 0.13.6
  online-ride-robust rebase); unit-tested, not yet filmed.
- **The horse rider is still hidden in free roam (two-PC test of 2026-10-23, host-log item 5).** The host rode an
  F9-spawned horse (`scope=local`, on his copy only) and the guest's copy hid and unrealized him for 14 s
  (`partner_mount=hide/unrealize`, acts 121/122). This is the act rule of this branch (`partner_native_mount=hide`,
  the play default), not the vehicle seat gate, so the Africa vehicle fix (the visible fallback for a vehicle absent
  on the viewer's copy, docs/anim-round1-seat.md "Fallback") does not cover it, and it was not extended to mounts:
  the partner's mount never exists on the viewer's copy (section 2), so a visible rider would be drawn floating at
  saddle height on every horse or D-Walker ride (`251abcef` filmed the standing body at the get-on spot), which
  reverses the decision of section 7 ("until step 0 ships, the rider stays hidden"). A drawn rider needs section 7,
  or a user decision to prefer the floating rider over the hide. Side effect to fix separately: while body 1 is
  unrealized by the mount hide, the viewer's own downed protection refuses (`downed_control=refused; reason=player`,
  host-log item 6).
- **The walker hide depends on the remote act stream.** An act sample that never arrives (`anim_valid` false) keeps
  today's on-foot drawing until the first valid mount act.
- **The overlay probes are append-only catalogue entries 22..24.** A branch that also appends entries must keep the
  native catalogue, `partner_sync.lua` `OVERLAY` and `docs/anim-round1-actions.md` 4.3 in the same order. The
  index is the reply's low 5 bits; 25 of 32 are now used.
- **`walker_spawn` changes the local buddy** (lab saves: D-Dog to D-Walker) through the same stock call as
  `horse_spawn`. It is a lab lever; it never touches a save file beyond what the stock buddy selection does in the
  running lab copy. Its `player_mounted` refusal misses the get-on / leave acts 122, 123 and 126 (no status), so
  callers wait for and require `localMounted=0`.
