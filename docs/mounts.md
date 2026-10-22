# Horse and D-Walker riders (F5, 0.13.5): research, decision, smallest viable design

Branch `wp/mount` from `wp/integ8` `228eecb`. Static research only (mgsvtpp.exe 1.0.15.4, SHA-256
`085c2f82...bb45`, Ghidra read-only and the `.text` dump); no game was launched. Addresses are absolute
(image base `0x140000000`).

**Decision.** There is no sound way to draw the partner on a horse or a D-Walker today, so the rider stays
hidden. The research found one gap in "hidden": the D-Walker rider was never hidden, because the sender reports it
as on foot. This branch adds a lab key that closes that gap (`partner_native_mount=hide`, default off), lab
probes for the open runtime questions, and the scenario `tools/worker/scenarios/partner-mount.txt`. It draws
no mount.

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

**RIDE_BODY moves the viewer's own buddy.** World op 42 (`lua/coop/world_ride_pose.lua`) is live at world level 3,
the Play-OfflineCoop default. It works like this:
1. Each copy sends the census pose of its own realized horse or walker every 250 ms (`R.send`, :249).
2. The other copy applies a pose only to an id its own census realized and that is not locally occupied (`R.move`,
   :231-234).
3. The horse is moved with `SetCallHorse` (start = pose, goal = 1 m along the heading); the walker with
   `SetPosition`.

So when **both** players deploy the same mount type, the viewer's own idle D-Horse or D-Walker is pulled along the
partner's mount path. It is the only "partner mount" on the viewer's copy today, but it is the viewer's buddy (the
spec rules it out as the partner's mount), and `SetCallHorse` is a call (the horse runs to a goal), not a
placement. The body 1 rider stays hidden. Scenario part 3 records what this looks like. This branch does not change
it (see Risks).

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

The check used the investigators' `foxhash.sc64` (validated: `stateGunHold` = `aa0ea22a5e0e`).

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

## 6. What this branch adds (lab only, default off)

- **`partner_native_mount=off|hide`.**
  - Defaults: off in lab and play. Play forces it off, and a named value records `partner_native_mount_given`.
  - Where the key is wired: `lab_config` (`ParseMountKey`, kept out of the main chain, which is at MSVC's nesting
    limit), the three `-LabSetting` regex copies and the help, `LabCommon`, `worker_session_tests`,
    `lab_config_tests`, `lab_ini_check`, `friend_package_tests`, and the docs/native-puppet.md table and "Mounts"
    section.
  - What it does: with `hide`, `partner_ride_wire::Observe` treats a valid remote act 121..126 like the heli acts
    (`partner_mount_policy::Hide`). Body 1 is hidden with `reason=ride`, a mount act is never a usable vehicle
    seat, and the partner returns after 500 ms of calm on-foot samples.
  - Scope: it acts only where `partner_native_ride` is not off. It changes nothing on the wire, so an old peer
    is unaffected.
  - Lines (driver tick, capped): `partner_mount=configured`, `partner_mount=hide; mount=..; act=..; kind=..;
    ride_live=..; episode=..` and `partner_mount=end; mount=..; act=..; ms=..; kind=..`. A missing act sample keeps
    the episode open (`partner_mount_policy::Holds`).
- **Overlay probes 22..24** (section 5).
- **Lab levers.**
  - `walker_spawn` / `walker_status` (`lua/coop/debug_buddy.lua`): the horse lever's stock `CallBuddy`, with
    `BuddyType.WALKER_GEAR`, 4 m to the right. Same guards and dog yield, an active buddy block only, never a mount
    (mount with E).
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
   RIDE_BODY must then map the remote's own index 0 to the local index 1 and **never** move the local index 0
   (this also fixes the Risk below).
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

Until step 0 ships, the rider stays hidden. `partner_native_mount=hide` makes that true for D-Walker too, once
a frame proves it.

## 8. Hidden-run checks for the runtime phase (`partner-mount.txt`)

1. **Part 1 (probes):** for each probe, `code_match` and `played_ms`, and the frame. Does a rider pose appear on
   body 1, and does it float?
2. **Part 2 (horse, guest hide):** `partner_ride=rx; kind=3` and `partner_ride=hide; reason=ride`. Does
   `partner_mount=hide; mount=horse; act=122` come before the kind-3 packet? No body 1 near the host in the riding
   frames. After the dismount: `partner_mount=end`, then `foot_stable`.
3. **Part 3 (both D-Horse):** does the guest's own horse follow the host's ridden horse (RIDE_BODY)? Record where
   it is in `horse-viewer-own`.
4. **Part 4 (walker, guest hide):** `walker_status ... actor0=22528`, and the host mounted with E. On the guest:
   ride kind stays 0, `partner_mount=hide; mount=walker; act=123|125; kind=0`, and **no body 1 in
   `walker-riding` / `walker-drive`**. This is the FRAME that may flip the default later.
5. **Part 5 (baseline, host off):** what the host draws for the guest's D-Walker ride today. Expected: body 1 on
   foot at the cockpit position, falling, or recon placements. Note any `partner_ride=hide; reason=air`,
   `partner_recon=place` or runaway lines.
6. **Every part:** no `partner_status=local` with `ON_HORSE` / `ON_VEHICLE` on the copy that did not mount, and no
   `crash_probe=av` other than the known teardown `rva=0x21e72`.

## Risks

- **RIDE_BODY moves the viewer's own buddy** (section 2) when both players deploy the same mount type. This is
  today's behaviour, unchanged here, and needs a decision. Options: keep it; restrict it to the capacity-two
  mirror index; or turn it off.
- **The walker hide depends on the remote act stream.** An act sample that never arrives (`anim_valid` false) keeps
  today's on-foot drawing until the first valid mount act.
- **The overlay probes are append-only catalogue entries 22..24.** A branch that also appends entries must keep the
  native catalogue, `partner_sync.lua` `OVERLAY` and `docs/anim-round1-actions.md` 4.3 in the same order. The
  index is the reply's low 5 bits; 25 of 32 are now used.
- **`walker_spawn` changes the local buddy** (lab saves: D-Dog to D-Walker) through the same stock call as
  `horse_spawn`. It is a lab lever; it never touches a save file beyond what the stock buddy selection does in the
  running lab copy.
