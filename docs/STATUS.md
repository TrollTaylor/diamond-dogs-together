## 2026-10-30: M6 seats and M7 passenger shooting

Both tracks are merged into `dev/oct7-continue` (local only: nothing pushed, tagged or released; not two-PC tested):
`c616d8b` merges `wp/seats` (`892620a`, review verdict `ship_behind_lab_key`, its fix round done) and `5a18d3b` merges
`wp/seat-shoot` (`e558854`, verdict `fix_first`, its fix round done). **No play default flipped**: every new key is
off in play, so a default session behaves as before for seats and shooting. Details:
`docs/vehicles-companions-design.md` 8.20 / 8.21 (M6), 8.22 / 8.23 (M7, renumbered from 8.20 / 8.21 at the merge, its
decision D16 now D18), 8.24 (the merge and the combined smoke), section 9 M6 / M7. Marks: [O] observed in a run or test
named here, [I] inferred, [U] unproven.

| Seat / behaviour | Built | Play default | Proven (runs) | Unproven |
|---|---|---|---|---|
| Jeep back seats (2, 3) as a passenger | (0.14.x, unchanged) | **On** | [O] combined smoke `a6830b7c` (play, `-WanSim bad`, shipped defaults): seat 2, frames on both copies while moving, the stock get-off and camera release | Two-PC |
| Jeep front seat (1) | Lab key `partner_ride_front`; offered only when the driver's copy reports seat 1 free (v4 seat addendum); the seat-1 remap skipped only while seat 1 may be in use | **Off** (D16) | [O] lab `f49c167d`, `3c748d46` (both roles, frames on both copies while moving); the handshake with the driver's dog in seat 1: lab `43473119`, `f115a2b3`, play `56322b36` (passenger in the back seat, frames on both copies) | The seat-1 path itself in play with the fixed code; a dog that jumps in after the passenger sat down [I]; two-PC |
| Truck cab (1) | Lab key `partner_ride_cab` (trucks: seat 1 only) | **Off** | [O] lab `0f145a4e` (both roles, moving frames on both copies, both stock get-offs); earlier `b66c7031`, `73af7206` | A play rehearsal (the test truck needs the lab-only sortie lever); two-PC |
| Seat race (driver's seat of an empty vehicle) | Lab key `partner_ride_race`; world op SEAT `0x36` (54; a 0.14.4 partner skips it by length); the guest holds until the host's grant; site C (the pre-query's final answer) | **Off** (D17) | [O] lab `43473119`, `f115a2b3`, play `56322b36`: the race inside one round trip, the host drives, no `two_drivers`, frames on both copies | The 1.5 s silence fallback in a run (Lua test only); Fulton prompt during a hold [I]; two-PC |
| Truck bed (E7), APC / tank crew | Not built (E7 static reads only) | - | - | Everything |
| Passenger aim and fire (stock) | Nothing to patch; the aim self-check is skipped while riding | **On** (stock) | [O] M7 lab runs (8.22); combined smoke `a6830b7c` in play: `aim_dir=refused; reason=ride; checks=0`, magazine 8 -> 5, the camera inside the jeep's body and pitched up after the shots (frames `pass-aim-01`, `pass-fire-1/2`, `drv-stopped`) | Whether a seated shot lands: none landed in six lab runs (8.22) [O], cause [U]; the smoke aimed at no target |
| Partner's raised gun on the driver's screen (design A) | Lab key `partner_seat_gun`: graph-2 GunHoldStart through the gesture hook, body 1's aim rates, the seated v6 direction (unchecked) | **Off** (D18) | [O] 30 holds ran on both copies (lab), but no frame shows a raised gun | The visible pose (design B, D18); the GunHold chain live; get-off during a hold; a mixed-build run [I] |
| Back-seat aim census | Lab key `partner_seat_aim` (read-only) | Lab only (play forces it off) | [O] `6fc3098b` | - |

**The merge:** conflicts in the two worker lab-key patterns (union), `lua/coop/debug_player.lua` `goto_vehicle` (both
levers kept; the M7 lever test now expects `goto_vehicle_args` for `jeep left`) and the docs (both sides kept, M7
renumbered). Op codes stay unique (SEAT `0x36` is the only new one); no CFG index added. The partner contract was
regenerated once after both merges (819 code sites, 206 vtable slots) and `generate_partner_contract.py`,
`generate_equip_writer_pins.py`, `generate_soldier_warp_contract.py`, `generate_nametag_contract.py` and
`generate_downed_damage_contract.py --check` all pass. Fast CTest 304/304 at `5a18d3b`; full CTest 321/321
(`tools/build.ps1`) at `5a18d3b`.

**Combined hidden smoke** (one run, design 8.24): `a6830b7cd9714aecb4c8ecef7ba0df52` (probe `848b1b832b3f40f5844dcca1d01ee73c`),
`tools/worker/scenarios/m6m7-smoke-play.txt` at `57d2cd7` (`wp/m6m7-smoke` in `D:/mgs-coop-wt/relief`, research build),
`-Mode play -WanSim bad`, seed `africa-jeep`, shipped defaults. Every expectation met; Closed, BaselineRestored,
RealSavesUnchanged, `Forced=False`; preflight receipt `20261030-120918-69670c53388b43c0b5e5bd9f964b4cf7` (313 tests
passed). Covered: the guest's SUPPLY DROP refusal (LOADOUT and VEHICLES, toasts drawn), a jeep drive with the host in
back seat 2 (`seat1=none`), the passenger aiming and firing while the jeep moved, both stock get-offs, and the jeep
parked at the same spot on both copies (1 mm apart). No `step=error`, `viewer_changed`, `board_uncertain`, non-zero
`faults=` or new-key line (`partner_board=race`, `board_remap`, `seat_ext_tx`, `partner_seat_gun`). Frames looked at on
both copies (`complete/frames/m6m7-integrate/a6830b7c-*`); archived under `%USERPROFILE%\mgs-coop-run-archive\m6m7\`.

**Docs:** README Known limits: the vehicle-shooting limit moved from Mounts to Seats and now says the camera can stay
tilted up after aiming (seen in the smoke); the roadmap lines for M6 / M7 name the merges. CAMPAIGN-ROADMAP keeps M6 at
[~] and M7 at [ ] (nothing new is on in play).

**Waiting for the user:** D16 (the jeep front seat in play), D17 (the seat race in play), D18 (how to continue M7).

## 2026-10-30: M6 seats, the review fixes (branch `wp/seats`, lab keys; merged in `c616d8b`, unreleased, not two-PC tested)

An independent review of `wp/seats` (`e2a7451`) raised three major and five minor findings; all eight were real and
are fixed or documented on `wp/seats` (local only: nothing pushed, tagged, merged or released). Results and evidence:
`docs/vehicles-companions-design.md` 8.21 (and the corrections to 8.20's summaries); the SEAT rules:
`docs/world-sync-protocol.md` section 21; the mechanism: `docs/native-puppet.md` "Seat 1 and seat races (M6)"; the v4
seat addendum: `docs/vehicle-ws3.md`. Marks: [O] observed in a run or test named here, [I] inferred, [U] unproven.

| Item | Built | Play default | Proven | Unproven |
|---|---|---|---|---|
| Seat race inside one round trip (R1) | The guest holds every empty vehicle's driver's seat until the host's SEAT grant (a 2 s native lease Lua refreshes; site C answers false, site B 4); a claim unanswered for 1.5 s drops the hold; the host yields at grant time and a live grant sticks | **Off** (`partner_ride_race`, D17) | [O] lab `43473119`, `f115a2b3`, play `56322b36` (`-WanSim bad`): the guest's icon refused before any answer (`action=hold; ...; deny=65535; grant=65535`), the host drives, the guest on foot, no `two_drivers`; frames on both copies | The silence fallback in a run (Lua test only); two-PC |
| The want after the door check (R2) | Site C `0x1246A4F` (CanExec's call of the pre-query `0x1245820`, pinned, only with the race key): the want is the pre-query's final answer; site A no longer races | **Off** | [O] lab `f115a2b3`, play `56322b36`: a host at the jeep's rear (inside the old 4.5 m gate) has no want, the guest is granted and drives; frames on both copies | A CanExec check after the pre-query (a tilted vehicle) can refuse an icon whose want was recorded [I] |
| Seat-1 handshake (R3, D16 (a)) | Ride addendum v4 from the driver's copy (Front / Cab accepted, `rider[1]` taken); the passenger's board offers seat 1 only with it | **Off** (`partner_ride_front`, `partner_ride_cab`) | [O] lab `43473119` (the host's dog jumped in: seat 1 withdrawn live), `f115a2b3` and play `56322b36` (the driver's dog in seat 1: the passenger in the back right seat, frames on both copies, the driver's own copy moving); truck cab `0f145a4e` (flags 0x03, seat 1 offered) | The seat-1 path itself in play with this code; a dog that jumps in after the passenger sat down [I]; two-PC |
| Remap scope (R4) | The seat-1 remap `jmp` only while seat 1 may be in use (the driver tick, its only writer) | (with seat 1) | [O] `board_remap; state=skip; why=policy` / `state=stock; why=idle` in `43473119`, `0f145a4e`, `56322b36`; unit tests | The other callers of `0xB7AF90` while the skip is live [I] |
| Phase-1 immediates / verify (R5), parser (R8) | Restored to 2 once site B is lost; `VerifyImage` strict; `ParseSeatRequest` tested; every rule run red first | - | [O] unit tests | Site B actually lost in a run (never happened) |
| Truck cab, both roles | (unchanged) | **Off** | [O] lab `0f145a4e`: every expectation met, moving frames on both copies in both roles, both stock get-offs with the camera release (the step `73af7206` missed) | Play rehearsal (lab-only levers); two-PC |
| Fulton prompt during a deny / hold (R6) | Documented (protocol 21, native-puppet, design 8.21) | - | - | Holding E there could Fulton the vehicle [I] |
| Summaries (R7) | Corrected: 8.20's play and truck "moving on both copies" claims, the lab "race" of `b66c7031` | - | - | - |

Hidden runs (research worker build, native gun stage, seed `afgh-ledge`; archived under
`%USERPROFILE%\mgs-coop-run-archive\m6m7\`): `43473119725643e9855a31b08e1da97a`, `f115a2b3cf874d9eb4cdb7470e03d46b`,
`0f145a4e19094fc4a0b0ecd2fcb0b755` (lab), `56322b362f96467194ad08b765e3dd9b` (`-Mode play -WanSim bad`). No play default
flipped (D16, D17 wait for the user). Fast CTest after each change; the full CTest passed 318 of 318 with these docs (`tools/build.ps1`).

## 2026-10-30: M6 more passenger seats (branch `wp/seats`, lab keys; merged in `c616d8b`, unreleased, not two-PC tested)

Branch `wp/seats` from `dev/oct7-continue` `60e0d13` in `D:/mgs-coop-wt/vehicle-state`; local only (nothing pushed,
tagged, merged or released). Results, the E6 / E7 static reads and the evidence: `docs/vehicles-companions-design.md`
8.20; the SEAT op: `docs/world-sync-protocol.md` section 21; the mechanism: `docs/native-puppet.md` "Seat 1 and seat
races (M6)". Marks: [O] observed in a run or test named here, [I] inferred, [U] unproven.

| Item | Built | Play default | Proven | Unproven |
|---|---|---|---|---|
| Jeep front seat (1) | Lab key `partner_ride_front`: site B sets both phase-1 `cmp al,imm8` immediates (`0x1251834`, `0x1251894`) to 1 only for the call it steers to seat 1; the seat matrix's seat-1 remap (`0xB79A43`) is a `jmp` for the process (scoped to seat-1 use since the review fixes, above); contract pins; the passenger report, the receiver gate and the passenger camera take seat 1 | **Off** (D16) | [O] lab `f49c167d`, `3c748d46`: both roles, logs on both copies, FRAMES on both copies while moving, the stock GetOffFrontRight; play `65277fb8` (`-WanSim bad`, keys named): both roles passed the seat-1 lines, seated frames on both copies, but moving frames on the passenger's own copy only (the driver's copy burst was stationary; review R7) | The driver's own D-Dog rides in that seat on the driver's copy and the passenger is drawn over it (D16; fixed by the handshake above); two-PC |
| Truck cab (1) | Lab key `partner_ride_cab`, the same mechanism for trucks (types 3 / 4: seat 1 only, never 2 / 3) | **Off** | [O] lab `b66c7031` (the guest as passenger), `73af7206` (both roles; moving frames on both copies for the guest-as-passenger role, on the host's own copy only for the host-as-passenger role; the guest's stock get-off) | The host's get-off from the cab (the worker lost its input desktop at that press, `73af7206`); a play rehearsal (the test truck needs the lab-only sortie lever); free roam's LRRP trucks carry soldiers; two-PC |
| Seat race | Lab key `partner_ride_race`; world op SEAT `0x36` (54, both directions; a 0.14.4 partner skips it by its length); the board's pre-query want (within 4.5 m) and a native deny (`MGS_COOP_BOARD_SEAT_V1`); NOTE 13 a 6 / 7 | **Off** (D17) | [O] lab `b66c7031`: the guest denied at the truck's cab doors, the host driving, the guest on foot, FRAMES on both copies; incidental denies in `3c748d46`; play `65277fb8`: the deny and the toast drawn, but the host did not board (no ride icon at its spot) | The want is taken from the pre-query's nearest search before its door check, so a host that cannot use a door still wins a tie (`65277fb8`); presses within one round trip of the guest's icon; two-PC |
| Truck bed (E7) | Not built (static reads only: the hide states live in the RideVehicle action; no bed anchor) | - | - | Everything |
| APC / tank crew | Not built; census: the wheeled armoured vehicle has one seat | - | [O] census `b66c7031` | The tank's seat count; everything else |

Hidden runs (research worker build, native gun stage, seed `afgh-ledge`; archived under
`%USERPROFILE%\mgs-coop-run-archive\m6m7\`): `f49c167d318744178e60aac5efbe3d58`, `3c748d4691964b7b90d7d35811e40d0b`,
`b66c703192e84214aa2d31f905489040`, `73af720692b5416a8cbbae651b4002f3` (lab), `65277fb83b1649ba85e9ef4c1e61f3de` (`-Mode play
-WanSim bad`; its first form `11fe8abd92924c1da1a2904f0d68d41c` stopped at a harness check before the seats).
An earlier start of the first run stopped inside the worker checks (the tree was edited while they ran; no game).
Fast CTest after each change; the full CTest passed 318 of 318 at `c0387d6` with these docs (`tools/build.ps1`).

## 2026-10-30: M7, shooting as a passenger (`wp/seat-shoot`; merged in `5a18d3b`, unreleased, not two-PC tested)

Branch `wp/seat-shoot` from `dev/oct7-continue` `60e0d13` in `D:/mgs-coop-wt/companion-host`. The 0.13.5 seat-aim census
(the uncommitted tree of `D:/mgs-coop-wt/seat-aim`) is committed on `wp/seat-aim` as `c87b270` and merged with a merge
commit (`5e2ce28`); the partner contract was regenerated and re-pinned (816, then 818 code sites / 206 vtable slots).
Details: docs/vehicles-companions-design.md 8.22, section 9 M7 and D18; docs/native-puppet.md "Seat gun". Marks: [O]
observed in a run or test named here, [I] inferred, [U] unproven.

**Verdict: M7 is not done.** Nothing flips: the new lab key `partner_seat_gun` stays off in lab and play.

| Part | Built | Result |
|---|---|---|
| E8, the local passenger | Nothing to patch | [O] A stock back-seat passenger aims (upper act 2, modes 6/7; the reticle on its screen) and fires (upper 3, mode 9; the magazine goes down) its own gun; no frame shows the raised gun itself (its camera sits over or behind its head). The seated body faces into the jeep and the +-90 deg aim range is measured from it (seat 2 the right side, seat 3 the left); the aim swings to the top of its range at each shot. [O] **No seated shot landed** in the six runs that aimed at the guard, in both role orders (in `a3f7c4e1` the reticle was red on him before the shots), while the same lever-aimed method killed that guard on foot in `6e0edd8b` (the guest never boarded there). The on-foot control of `d7cf5a4b` was not a valid control: its reticle was on empty ground (frame `d-d1-pre`). Why seated shots miss is [U] |
| The aim self-check | `partner_aim::GunDirSeated`; with the key on the passenger sends its seated v6 direction unchecked | [O] Not checked, so it cannot latch: `aim_dir=refused; reason=ride; checks=0` (key off), `aim_dir=valid; reason=seat; checks=0` (key on); no `reason=convention` in any run. The seated direction itself is not validated: roughly the lever's aim in `a3f7c4e1`, about 10 deg low in `d3409f57`, and it saturates near -75 deg at each shot |
| E9, the partner's gun (design A) | `partner_seat_gun` (pure), two local kinds in the gesture hook (`gun_start` GunHoldStart, `gun_hold` GunHold; held while the driver names it, released with stateNull), body 1's aim rates written and restored, `TickSeatGun`; contract pins `SEAT_GUN_SITES` | [O] 30 holds: on both copies in `30a6a21a`, `03ceff68`, `d3409f57`, `a3f7c4e1` (3+2, 3+2, 3+2, 6+4) and on the host only in `d7cf5a4b` (5), each `hold` -> `submit; kind=gun_start` -> `applied; ok=1` -> `rates; ok=1` -> `release` -> `ended; cause=release; null=1`, no uncertain or fault. [O] **The frames show no raised gun** on the seated partner. The raise never reached its clip end, so GunHold was never requested; no hold ended by the gate (getting off), the lease or the epoch, and the rate restore was never logged [U] |
| Test tooling | Debug commands `enemy_aim_nearest [height_m]` (the camera to the nearest living soldier) and `goto_vehicle jeep right` | [O] Both work (`a3f7c4e1`: the reticle red on a sleeping guard; `03ceff68`: seat 3 boarded from `goto_vehicle jeep right`) |

**Hidden runs** (research build, native gun stage, seed `africa-jeep`, lab; all Closed, BaselineRestored,
RealSavesUnchanged; `Forced=True` in `d40e4126`, `30a6a21a`, `6e0edd8b`, `03ceff68`, `a3f7c4e1`; CPU 5-30 % before each).
The budget of eight is spent; no play rehearsal ran. A first attempt stopped in its worker checks (a source file added
during the checks) and never launched the game.
- `6fc3098b28344e27afe917f36a600a72` (`coop-vehicle-seat-aim.txt`, the census): E8 above; the passenger camera kept the
  boarding direction and every shot missed the guard ahead; while firing, the view filled with a tan surface with a dark
  edge, probably the jeep's bodywork, which the passenger camera leaves out of its collision query [I].
- `d40e412636b04b58b5e4afeb40202dfa` (`coop-vehicle-seat-shoot.txt`): no hold started (the seat-owned branch of the driver
  tick returned before `TickGesture`; fixed in `8c0d482`); from seat 2 aiming across, the camera looked through the
  jeep's body.
- `30a6a21a4c784049b79c259dd920977c`: the hold path on both copies; no visible change; seat 2's body faces right.
- `6e0edd8b577f4df48f80b78cf3fd7541`: no boarding from the passenger side facing away (hence `goto_vehicle jeep right`;
  that facing is the cause is [I], one run); the guest, still on foot, killed the sleeping guard with the same aim lever
  (`enemy_aim_nearest 0.3`): `Dying`, `event=req; rid=3`, the host's `event=commit; ...; actor=guest; rid=3`.
- `03ceff689ef24091a4194b0ab1a6fbe7`: seat 3's body faces left; aiming out of its own side clamps.
- `d3409f579a724f5589113b0b69f1ce7f`: seat 3 across the jeep, the camera over the head; the lever's aim 7.5 deg low;
  bursts climbed to the sky.
- `a3f7c4e184d0490c833a4ee14a75beb1`: single shots with the reticle red on the sleeping guard; no hit recorded.
- `d7cf5a4b4e3e4c6c82b136fdd088e4e2` (`coop-vehicle-seat-shoot-diag.txt`): driving, standing and first-person shots, then
  an on-foot control; none hit, and the control's reticle was on empty ground (frame `d-d1-pre`), so it tested nothing.

Slices in the session scratch `m6m7/shoot/slice-<run8>`, frames `complete/frames/shoot/<run8>-<role>-<label>.jpg`; run
folders archived under `%USERPROFILE%\mgs-coop-run-archive\m6m7\<id>`.

**Wire:** no new op or CFG index; with the key on the v6 input frame carries a seated direction, which a 0.14.4 receiver
uses only in its native drive (disarmed while the sender rides) [I]. **Tests:** new `partner_seat_gun_tests`; additions
to `partner_gesture_hook_tests`, `lab_config_tests`, `lab_ini_check`, `worker_session_tests`, `friend_package_tests`,
`debug_menu_lua_check` (written with their code: that they failed first cannot be shown from the history). Fast CTest
304/304; full CTest 320/320 (`tools/build.ps1`, after the docs). The review's fix round (below) adds driver-level
tests shown failing against mutations.

**Open (D18 in the design doc):** a static study of the gun plugin on a seated body and of the pitch jump at the shot,
then design B with a new run budget; or park M7. The front seat and the truck bed depend on M6 (another track).

**The review's fix round (verdict fix first; design doc 8.23 and section 11).** Every finding was checked first.
- `fb71ab7`: `partner_seat_gun_tests` was in neither the Release receipt's audited list nor its excluded list, so
  `tools/package.ps1 -Friend` would have thrown; it is audited now. New slow CTest `worker_coverage_tests` compares
  every registered test (`ctest-test-names.txt`, written at configure) with the Release plan; it failed first naming
  `partner_seat_gun_tests`. The research worker build's inventory (320 tests) is fully classified too, and the
  hidden-run preflight passes with it: `Invoke-WorkerChecks -Profile Research` (no game, under the verify lock),
  receipt `20261030-110552-f8f0879750314da0a876b8eacab98aad`, `passed`, 313 audited tests, 0 failures.
- `e5c5739`: driver-level tests (`TestSeatGunDriver`, `TestSeatGunSender`): the seat-owned branch's gesture step and
  hold (fails on the parent of `8c0d482`), the command and `hold_seq`, the hook's end and the retry, the unanswered
  request, the aim's and the seat's release, the driver's seat refused, and the sender's seated direction only with
  the key and a passenger report. Nine one-line mutations of the rules each fail them. `PassengerSeat` (seats 1..3;
  0, 4, 255 refused) replaces the inline test, failing first.
- Docs and README: the README no longer says a passenger can raise the gun; the hold counts (30, d7cf5a4b host-only),
  the on-foot kill of `6e0edd8b` and the invalid control of `d7cf5a4b`, "not checked" for the seated direction, the
  rate restore's exceptions (epoch, uncertain, fault), the camera attribution [I]/[U] and the boarding cause [I].
- No runtime behaviour changed (the `PassengerSeat` refactor is the same test), so no hidden run was needed for it;
  nothing flips. Fast CTest 304/304 after each step; full CTest 321/321 after the docs.

## 2026-10-30: fixes after 0.14.4 (merged on `dev/oct7-continue`, unreleased, not two-PC tested)

The three tracks that followed the 0.14.4 release are merged into `dev/oct7-continue` (from `01fd055`): `f1e80be`
merges `wp/vehicle-fixes2` (`8e8e455`, review verdict `ship_behind_lab_key`, its fix round done), `101d925` merges
`wp/dog-heroism` (`e67982a`, verdict `ship_behind_lab_key`, no fix round) and `416b2fa` merges `wp/launcher-fixes2`
(`d76e7f6`, verdict `fix_first`, its fix round done); `8cb81f8` adds the combined smoke scenario. Local only: nothing
pushed, tagged or released. The track sections below keep their own details. Marks: [O] observed in a run or test
named here, [I] inferred, [U] unproven.

**Merge notes.** Conflicts only in docs. `docs/STATUS.md`: every top section kept. `docs/vehicles-companions-design.md`:
each track wrote a section 8.14; the vehicle fixes keep 8.14 / 8.15, the dog's section is now 8.16 and the launcher's
are 8.17 / 8.18 (every reference the branches added renumbered with them); section 10's table takes the dog's D7 row and
the vehicle fixes' D11 / D12 rows; section 11 keeps both review-log tables. Wire and configuration: the only new op is
VEHICLE_DAMAGE `0x3A` and the only new CFG bits are `r[15]` bits 64 / 128 (`world_vehicle_crew`,
`world_vehicle_damage`), both from the vehicle fixes; the dog and launcher tracks add no op, CFG index or lab key, so op
codes and indices stay unique (`world_wire_tests`, `lab_config_tests`). A 0.14.4 partner skips `0x3A` by its length
(`unknown_op`) and play pairing refuses mismatched builds anyway (protocol section 20); no mixed-build run [U].
`Get-WorkerTests` carries both tracks' additions: the Release receipt list is now 312 tests (311 +
`world_vehicle_damage_lua_check`), Normal and Research 310.

| Fix | Built | Play default | Proven | Unproven |
|---|---|---|---|---|
| D12 crew: a vehicle the guest destroys with only soldiers aboard is wrecked on the host's copy at once | `world_vehicle_rules.lua`, key `world_vehicle_crew` (`r[15]` bit 64); held while a player stands within the blast distance by type (15 m light vehicles, 50 m every other or unknown type, also for `break_safe`; `blast_near` holds an unnamed copy instead of a Despawn) | **On** (off in the lab unless named) | [O] lab FRAME `ac0f7656`, `c0a385a5` (the hold at 33 m, the wreck once clear); play `-WanSim bad` `8995e351`, `c6db67b8`; at the shipped defaults `34d5744d`, `98d84a21` (frames on both copies) | [U] the armoured blast's damage at 25-50 m (godmode in every run); [fixture] `blast_near` and `crew_near` with the host's own player; [U] a Despawn under seated soldiers; an alerted enemy vehicle that drives off stays split (M8, `247a4eed`); two-PC |
| The guest's vehicle damage short of destruction reported to the host | `world_vehicle_damage.lua`, op VEHICLE_DAMAGE `0x3A`, key `world_vehicle_damage` (bit 128); host bounds (half the copy's life per report, 3 per vehicle and 16 in all per second); D12's heavy rule counts own damage only | **Off** (lab key; named in play to use it) | [O] lab `ac0f7656`, the host bounds `c0a385a5`; with the key named in play `247a4eed`, `34d5744d` (before the review turned it off) | [U] live AI fire: each PC's guards hit the vehicle and their rounds add up (`846c1147`); no shot attribution; the frames cannot show a shared life at these values. So in a default session the guest's damage is still put back at the host's next state |
| D11 before PAIRED: the guest's LOADOUT and VEHICLES items inactive from its arrival after a host-led follow | `world_vehicle_rules.lua`, `world_relief_sync.lua`; rides on `world_loadout_block`; the mark lapses 300 s after the arrival without PAIRED | **On** (with `world_loadout_block`) | [O] lab FRAME `251adcb6` (both refusals with their toasts before PAIRED); play `-WanSim bad` `c6db67b8` (LOADOUT), `98d84a21` (both items, frames on both copies, the host held on its tips screen); the paired refusal again in `32c3893a` (below) | [fixture] the 300 s lapse; two-PC |
| The heroism and bond of a dog copy's death given back on the guest | `world_companion_guard.lua` (guest only); `world_sync.lua` hands on the attacker id; no key of its own | **Off** (under `companion_host_owned`, off in lab and play) | [O] lab `c7fdfdbf` (lab kill, mirrored down, guard fire: readbacks equal before and after, the stock notice on the guest's screen); control `2243bbc6`; a default play session logs no `dog_hero` line (`32c3893a`) | The first control `a7f00043` **failed** (the C4 missed the dog); no play rehearsal; the by-player branch (fixture only); persistence in the save [I]; the stock notice still shows. The review's four minor findings are open (no fix round ran): no lab key of its own, the scenario's expectations do not encode the give-back, the pre-death sample's age and the `unknown` attacker's caps, no co-op note after the give-back |
| Launcher and package findings of the 0.14.4 review, rounds 1 and 2 | `native/launcher/**` (setup lock, close during setup, the closing-gap wait), `tools/package.ps1` (untracked files refused, fail-closed), `Invoke-WorkerChecks.ps1` (coverage, Release-only launcher checks), the tag, source zip and network wording | No game behaviour | [O] failing-first unit tests; hidden end-to-end `r4`, `r5` (outside `tools/worker`, recorded as a deviation); Release receipts `20261030-014929-6188027e...` and `20261030-031742-378e13be...` (311 tests); local packages audited 54/54 and 26/26 (not uploaded); a Research preflight with the new list, this smoke's `20261030-044444-8a250766d8ad4dcb83e01318f42dc345`: 310 tests passed with the background worker on | [U] a Normal-profile preflight; the closing-gap wait and an abandoned setup lock end to end with the real exe (unit tests only); two-PC |

**Combined hidden test of the merged head** (`wp/next2-smoke` = `dev/oct7-continue` `8cb81f8`, built incrementally in
`D:/mgs-coop-wt/relief`, research worker build, native gun stage; `tools/worker/scenarios/next2-smoke-play.txt`,
`-Mode play -WanSim bad`, seed `africa-jeep`, **the shipped play defaults**: no labsetting line and no lab lever). Run
`32c3893a301e4d579d76894397047bde`: **every expectation met** (ExpectFailures empty), Closed, BaselineRestored,
RealSavesUnchanged, `Forced=False`; CPU 18 % before the run. Link RTT 174-189 ms, loss up to 13.3 %. No `step=error`,
`event=error`, `viewer_changed`, non-zero `faults=`, `via=wreck`, `event=vehicle_damage` or `event=dog_hero` line on
either copy. Slices `next2/integrate/slice-32c3893a` and frames `complete/frames/next2-integrate/32c3893a-*` in the
session scratch, looked at for every line below; run and probe folders archived under
`%USERPROFILE%\mgs-coop-run-archive\next2\` (`32c3893a...`, `df567698...`). One run of the two allowed.
- [O] Both reached gameplay (host 53.0 s, guest 51.0 s after launch) and paired (`remote_build=0.14.4+8cb81f8` on
  both; PAIRED epoch 3 in 30020 with 265 soldiers matched). `vehicle_rules; step=on; ...; break=1; credit=1;
  loadout=1; gone=1; crew=1` on both copies, and no `vehicle_damage` on-line (off in play).
  `dog_guard=off;reason=key_off;slots=0` and `dog_state ... installed=0` on both. Frame `smoke-start` on both screens:
  the jeep with the partner beside it.
- [O] D11: the guest's LOADOUT and VEHICLES items inactive and refused with both toasts (`step=refused` for
  `idroid_drop_loadout` and `idroid_drop_vehicle`, NOTE 13 a 5 and a 4; frames `d11-guest-loadout-refused`,
  `d11-guest-vehicles-refused`); the host's VEHICLES list opened (`d11-host-vehicles-open`).
- [O] A drive with a passenger: the guest drove 25608 with the host in a back seat (`board_passenger ... seat=2` on the
  host, `partner_ride_seat=confirmed ... seat=2` on the guest; frames `drv-seated` on both, `drv-pass-NN` from the
  host's seat, `drv-driver-NN`) and stopped in the grass under a tree (`drv-stopped` on both). Parked: the host's
  VEHICLE_STATE skipped 25608 while ridden, resumed (`was=partner_ride`) and sent it moved 23.7 m (`d_mm=23664`); the
  guest's copy already stood there (`step=same; d_mm=1`; no guest `step=applied` in the whole run); `drv-parked-both`
  shows the jeep under the same tree on both screens.
- [O] D14: the host's hand Fulton of 25610: host `credit; via=own; res=14`, guest `credit_seize`, then `credit;
  via=receiver; res=14` and `applied; via=seize; ok=1; ms=133`; one credit line per copy. Frames: the lift on the host's
  screen (`fult-lift-03`); the jeep beside the partner on the guest's screen before (`fult-before`) and gone on both
  after (`fult-after`).
- Not in this smoke: a vehicle damaged by the guest staying damaged (the report is off in play, so it does not; not
  tested), D12 (its rehearsals are in the table), the D11 window before PAIRED (here the guest paired 1.1 s after its
  arrival), a dog death, the launcher.

**Checks.** `pwsh -File tools/build.ps1 -Fast` 302/302 after each merge; full `pwsh -File tools/build.ps1` (Debug)
318/318 at `416b2fa` and again at the docs commit `6ac6a86`; `worker_session_tests` with the new scenario; the five contract checks
(`generate_partner_contract.py`, `generate_equip_writer_pins.py`, `generate_soldier_warp_contract.py`,
`generate_nametag_contract.py`, `generate_downed_damage_contract.py`, each with `--check`) pass.

**Open, for the user.** The dog track's two questions (should the guest share the stock penalty when the host's dog
really fell; is the control with the key on the guest only acceptable as the key-off control) and the launcher track's
one (may a launcher end-to-end use its own private-desktop harness outside `tools/worker`).

## 2026-10-30: the review of the vehicle fixes (`wp/vehicle-fixes2`, unreleased, not two-PC tested)

Branch `wp/vehicle-fixes2` in `D:/mgs-coop-wt/vehicle-state`, new commits after `116847e`; local, nothing merged,
pushed, tagged or released. The independent review of the vehicle fixes (verdict `ship_behind_lab_key`: three major,
four minor findings, four refuted claims) was checked finding by finding against the code, the slices and the frames.
Details: `docs/vehicles-companions-design.md` 8.15 (results) and section 11 (the finding table),
`docs/world-sync-protocol.md` section 20 (the review note; no wire change).

| Finding | Outcome | Proof |
|---|---|---|
| Major: `world_vehicle_damage` on in play without its fire gate meeting live AI fire (the guards' rounds on the two copies add up) | **Off in play again** (`795bdfc`), lab key; host bounds per report (half the life) and per second (3 per vehicle, 16 in all), and D12's heavy rule ignores reported drops (`8f20483`) | The bound live in the lab `c0a385a5` (2260 -> lever 600 -> host 1130 `cap=1`; -> 300 -> 565; both copies 565 past the refresh) [O]; a live-guard rehearsal or shot attribution stays open [U] |
| Major: the crew rule's 15 m (a jeep) vs. the armoured vehicle's blast that knocked the guest flat at 25 m | **Fixed** (`77be734`): 15 m for light vehicles, 50 m for every other type, an unknown type or a missing binding, for the hold and `break_safe`; an unnamed copy with a player inside it is held (`blast_near`) instead of a Despawn that cannot run | Lab FRAME `c0a385a5`: no knockdown at 33 m; the `crew_near` hold with the guest 33 m away (both screens), the wreck once it stood 63 m away, the host 60 m away on its feet; play `c6db67b8` and `98d84a21` (`-WanSim bad`, the guest's real C4 from 25 m: the same hold and release, frames on both copies) [O]. The blast's damage between 25 and 50 m is unmeasured [U]; `blast_near` live [fixture] |
| Major: the D11 pre-PAIRED fix in play by default without a play rehearsal of that window | **Rehearsed in play** with the new worker step `titlecontinue` (`5293dc2`); the pre-pair mark now lapses 300 s after the arrival without PAIRED (`017e777`) | `98d84a21` (play, `-WanSim bad`, the shipped defaults): both items refused before PAIRED with both toasts drawn, the host on its tips screen (frames), PAIRED 45.2 s after the arrival; `c6db67b8`: LOADOUT the same, VEHICLES not reached (the alerted Masa guards closed the iDroid) [O]. The lapse [fixture] |
| Minor: the host takes the guest's drop at face value; reported drops feed the heavy rule | **Fixed** (`8f20483`) | Fixtures (failing first); the bound live in `c0a385a5` |
| Minor: protocol section 20 says the guards' fire counts once | **Fixed** (`3d2fc8b`) | Rewritten: counted once only when the host copy's own hit came first |
| Minor: README states the crew hold as fact, the guest's gunfire as shared | **Fixed**: the hold is now proven live and the README names its distances; the damage report is off in play, so the README no longer claims it | `c0a385a5`, `c6db67b8`, `98d84a21` |
| Minor: stale "off in play" comments; 8.14 `Forced=True` for every run | **Fixed** (`3929a6c`, `3d2fc8b`) | |
| Refuted claims (D11 fix in play, "counts once", the damage FRAME, `Forced`) | **Corrected** in 8.14, section 20, this file (below) and the roadmap | |

**Play defaults now.** `world_vehicle_crew` **on** in play unless named (`3929a6c`; lab FRAME `c0a385a5`, the play
rehearsal `c6db67b8` with the key named, again at the shipped defaults `98d84a21`). `world_vehicle_damage` **off** in
play unless named (`795bdfc`). The D11 fix rides on `world_loadout_block` (on; play rehearsals `c6db67b8`, `98d84a21`).
In the lab both keys stay off unless named.

**Hidden runs** (3 of 3 budgeted; research build, native gun stage, copied saves, `africa-jeep` seed; each Closed,
BaselineRestored, RealSavesUnchanged, no `step=error`, `viewer_changed` or fault line; archived under
`%USERPROFILE%\mgs-coop-run-archive\next2\<id>`; slices `next2/vehicles/slice-<run8>` in the session scratch; frames
`complete/frames/next2-vehicles/<run8>-<role>-<label>.jpg`):
- `c0a385a55fd949a89f31505cb4ee19d5` (lab, `vehicle-review-lab.txt` at `2d47238`, every key named): **every expectation
  met** (`Forced=True` at shutdown): the host's half bound twice; the crew hold at 33 m and the wreck once clear.
- `c6db67b8999c4a79ab38ac6a57870fac` (play, `-WanSim bad`, `vehicle-review-play.txt` at `3d2fc8b`, crew named):
  `Forced=False`, link 176-188 ms, loss up to 3.8 %; D11 before PAIRED: LOADOUT refused with its toast; VEHICLES not
  reached (the alerted guards closed the iDroid; 2 expectations failed); the crew part met every expectation.
- `98d84a219dd4494ebd6d5578614ad189` (play, `-WanSim bad`, the same scenario at `b4cd471`, **no vehicle key named**):
  `Forced=False`, link 176-190 ms, loss up to 3.2 %; every behaviour met (D11 both items before PAIRED, the crew at its
  default, no VEHICLE_DAMAGE line on either copy); one expectation failed by the scenario's own timing: the VEHICLES
  toast is drawn in the frame, but its log line fell inside the HUD toast log's 10 s gap per note code (the scenario
  waited 3.5 s; it now waits 10.5 s again).

**Checks.** Tests first for every rule (logs `next2/vehicles/failfirst-*.log`: rules check 108, 63, 71; relief_sync 99;
damage 47, 69; lab_config 1274, 1275; worker_loading). `pwsh -File tools/build.ps1 -Fast` 302/302 after each change;
the full `pwsh -File tools/build.ps1` (Debug) 318/318 before the docs commit (code at `3929a6c`, scenario `b4cd471`).

**Wire.** No change since 8.14: VEHICLE_DAMAGE 0x3A (a 0.14.4 partner skips it by its length; play pairing refuses
mismatched builds anyway). The host bounds, the blast distances and the lapse are local behaviour.

**Integration note.** `wp/launcher-fixes2` (`bc56c0d`): `Assert-WorkerCoverage` must admit
`world_vehicle_damage_lua_check` (as before). The new worker step `titlecontinue` touches
`tools/worker/Invoke-WorkerScenario.ps1` (help, validation, roles, dispatch) and `tests/worker_loading_tests.ps1`.

## 2026-10-29: the vehicle fixes (D12 crew, the guest's vehicle damage report, D11 before PAIRED; `wp/vehicle-fixes2`, unreleased, not two-PC tested)

**Superseded in part by the review above** (2026-10-30): the damage report is off in play again, the crew rule's
blast distance depends on the vehicle type, and these refuted claims are corrected: the D11 fix was not exercised in
`247a4eed` / `8995e351` / `34d5744d` (the guest's first guard line has the same millisecond as PAIRED: the old paired
refusal); the damage FRAME shows the jeep, not the shared life; `Forced=False` held for four runs, not none.

Branch `wp/vehicle-fixes2` in `D:/mgs-coop-wt/vehicle-state`, from `dev/oct7-continue` `01fd055` (0.14.4); local,
nothing merged, pushed, tagged or released. The user approved the three fixes on 2026-10-29 after the 0.14.4 review
(D12's open question; findings `game-d11-prepair-gap` and `game-vstate-heals-guest-damage`). Details:
`docs/vehicles-companions-design.md` 8.14 (results), section 10 (D11, D12), `docs/world-sync-protocol.md` section 20
(the op and the keys).

| Fix | Built | Play default | Proven | Unproven |
|---|---|---|---|---|
| D12 crew: a soldier-crewed vehicle the guest destroys is wrecked on the host's PC at once | `world_vehicle_crew` (CFG `r[15]` bit 64, `world_vehicle_rules.lua`): a hold whose only reason is an NPC crew is judged at once and applied as the host's wreck (`break_crew`); held while a player is within 15 m of the host's copy; D-Dog, a buddy, the partner's ride and the host's own player keep the old hold or refusal | **On** where `world_vehicle_break` runs (30010, 30020) | FRAME lab `ac0f7656` (the guest's lever, the burning armoured vehicle on both screens), play `8995e351` (`-WanSim bad`, the guest's real C4, frames on both), again at the shipped defaults `34d5744d`; the crew's death counts once on each copy (one host commit, a `terminal_local` apply on the guest) [O] | An enemy vehicle that drives off in a fight moves separately on each PC (`247a4eed`: refused as far, the split stays; M8); `crew_near` and a Despawn under seated soldiers [U]; two-PC |
| The guest's vehicle damage report | `world_vehicle_damage` (bit 128), new chunk `world_vehicle_damage.lua`, new op VEHICLE_DAMAGE 0x3A (guest to host, 26 bytes): drops of the guest's copy within 3 s of its own fire (GUN_FIRE) and 300 m are reported; the host applies the lower life (at most the guest's drop, never 0) and VEHICLE_STATE carries it back | **On** where VEHICLE_STATE runs (30010, 30020) | FRAME lab `ac0f7656` (the jeep on both screens at the shared life; the engine draws no damage at these lives), play `247a4eed` (the guest's real round 2020 -> 1990 on both copies past the refresh, applied 117 ms after the report), again at the shipped defaults `34d5744d` (the host's round synced and never reported back) | The guest's explosives short of a destruction, its collisions, a guard's round in the guest's 3 s window [I]; two-PC |
| D11 before PAIRED | `world_follow.lua` records the follow arrival (`F.arrivedMission`); `world_vehicle_rules.lua` and `world_relief_sync.lua` mark that mission co-op at the guest's start, so LOADOUT and VEHICLES are inactive before PAIRED | (rides on `world_loadout_block`, **on**) | Lab `ac0f7656` (guard 29 s before PAIRED), `d3f5ca56` (104 s, on foot) with the host held on its tips screen (frames); FRAME `251adcb6` (both items refused with both toasts on the guest's screen before PAIRED, the host on its tips screen; the pair formed 96 s after the arrival); play `247a4eed`, `8995e351` (the fix in, refused with its toast; the play worker always pairs first) | The pre-pair window in play (the play worker always pairs first) [I]; two-PC |

**Hidden runs** (research build, native gun stage, copied saves, `africa-jeep` seed; each Closed, BaselineRestored,
RealSavesUnchanged, no `step=error`, `viewer_changed` or fault line; archived under
`%USERPROFILE%\mgs-coop-run-archive\next2\<id>`; slices in the session scratch `next2/vehicles/slice-<run8>`; frames in
`complete/frames/next2-vehicles/`):
- `846c1147` (lab, `20a7120`): the report path worked (the guest's lever 2140 -> 1500 applied on the host 17 ms later),
  but the guards' rounds on both copies were reported and added up until the host's jeep broke: the fire gate
  (`d8497c5`). Stopped at A2 (`goto_vehicle` went to a jeep 600 m away).
- `ac0f7656` (lab, `d8497c5`): the lab FRAME run (damage A/A2/A3, crew, D11's guard before PAIRED); the guest's iDroid
  did not open while it rode the helicopter in.
- `247a4eed` (play, `-WanSim bad`, `d8497c5`, the two keys named): D11 and the guest's damage passed; the host's rounds
  missed; the crew part failed outside the rule (the alerted armoured vehicle drove apart, refused as far).
- `d3f5ca56` (lab, `9538f86`): D11 on foot, the guard 104 s before PAIRED; the iDroid opened too late (arrival popups).
- `8995e351` (play, `-WanSim bad`, `da2043d`, crew named, the damage report at its play default): D11 and the crew
  passed; the Masa guards broke the jeep before the damage part. `Forced=False`.
- (none): `vehicle-fixes2-d11.txt` at `f6823b9` never launched: the worker's audited CTest snapshot held a
  failing-first test (`lab_config_tests` check 1275) written before its code.
- `251adcb6` (lab, `f6823b9`): **the D11 FRAME before PAIRED, every expectation met**, `Forced=False`.
- `34d5744d` (play, `-WanSim bad`, `f6823b9`, **no vehicle key named**: the shipped defaults): **every expectation
  met**, `Forced=False`, link 175-186 ms, loss up to 13.3 %: D11, the guest's and the host's rounds, the crew.

**Checks.** Tests first for every rule (logs `next2/vehicles/failfirst-*.log`). `pwsh -File tools/build.ps1 -Fast` 302/302 after each code change; the full `pwsh -File tools/build.ps1` (Debug) 318/318 (317 before plus `world_vehicle_damage_lua_check`) at the last code commit `f6823b9`. Hidden runs: 7 launched (`846c1147`, `ac0f7656`, `247a4eed`, `d3f5ca56`, `8995e351`, `251adcb6`, `34d5744d`) of the 8 budgeted; one more refused at its worker checks before launch.

**Wire.** VEHICLE_DAMAGE 0x3A is new; a 0.14.4 partner skips it by its length (`unknown_op`) and play pairing refuses
mismatched builds anyway. No other op or field changed. 0x35-0x37 stay reserved; 0x3B onward are free.

**Integration note.** `wp/launcher-fixes2` (`bc56c0d`) adds `Assert-WorkerCoverage` / `Assert-WorkerInventory` to
`tools/worker/Invoke-WorkerChecks.ps1`: the new `world_vehicle_damage_lua_check` (four Lua arguments) is in
`Get-WorkerTests` here and must stay admitted there at the merge.

## 2026-10-29: the heroism of a dog copy's death given back on the guest (`wp/dog-heroism`, lab only; merged on `dev/oct7-continue` 2026-10-30, see the top section)

The user approved it on 2026-10-29: when the guest's copy of the host's D-Dog dies on the guest's PC, the stock game
took 30 heroism and 2 bond points on the guest's PC ("D-Dog Near Death [Heroism -30]", run `9fa48400`) for
a death that is not real. Branch `wp/dog-heroism` (from `dev/oct7-continue` `01fd055`, in the worktree
`D:/mgs-coop-wt/companion-host`; commits `85ba7ec`, `159ae49`, `e905f99`, `2885155` and this one; nothing merged,
pushed, tagged or released). Details: `docs/vehicles-companions-design.md` 8.16.

- **Where the penalty is** [O, static, capstone]: native, not Lua. The dog's life update calls the buddy service's
  dying handler (`BuddyServiceImpl` slot `0x258`, `0x140A4F440`) just before its `Dead` message: unless the DOG bit of
  `deadBuddyType` is already set it takes 30 heroism and 20 raw bond (`vars.buddyFriendlyPoint`, 2 of
  `GetFriendlyPoint`'s 100), or 60 heroism, 100 raw bond and +150 demon points when a player (id 0..511) made the kill,
  then sets the bit. The notice comes from the same native code.
- **Built:** `lua/coop/world_companion_guard.lua` (guest only, under `companion_host_owned`'s peer agreement): each copy
  death gets back exactly what the stock penalty took, once, measured against the last 100 ms sample before it and
  never more than the stock amount (`AddHeroicPoint`, the raw bond written back, `SubOgrePoint`; log `event=dog_hero`).
  `world_sync.lua` hands the raw attacker id on. Lab levers `companion_lab hero` (both roles, play too: read only) and
  `kill_own` (guest: the native kill on the guest's own dog when the session is not host-owned). No wire or receipt
  change (protocol section 15). Fixture `world_companion_guard_lua_check` 115 -> 149 checks, red first.
- **Proven in the lab** [O]: run `c7fdfdbfb73f41d8860cef055d88aaa7` (key on, `afgh-ledge` / `afgh-horse`, every
  expectation met): the lab kill (`h=244995:244965:244995; b=1000:980:1000; give=30,20,0`), the host's dog down (the
  host's own -30 / -20 raw kept on the host; the guest's mirrored copy given back) and a real death by guard fire
  (`att=1206`, given back); the guest's readbacks equal before and after each death, the host's unchanged by the
  guest's copy deaths, no `dog_hero` line on the host; FRAME: the stock notice "D-Dog Near Death [Heroism -30]" on the
  guest's screen while the host's dog stands beside Snake on the host's (`c7fd-*-hr-dead-2`), and on both screens at the
  mirrored down (`c7fd-*-hr-down`). Control run `2243bbc624ad4f0bb141837b40c40ed0` (the key on the guest only, so the
  session is not host-owned and the guest's dog is its own; every expectation met): `kill_own` -> the guest lost 30
  heroism and 20 raw bond and kept the loss (`applies=0`, no `dog_hero`), the host unchanged, the notice on the guest's
  screen (`2243-guest-hc-dead-2`). A first control by the guest's own C4 with both keys off
  (`a7f00043c6a64466915a6ee99342a73b`) **failed**: the blast missed the dog and downed the host through the friendly hit.
  Runs archived under `%USERPROFILE%\mgs-coop-run-archive\next2\<run>` with their probe folders (`7a5e7417...`,
  `4c3b3c64...`, `a2d998d8...`); frames in the session scratch `complete/frames/dog/`.
- **Defaults: unchanged.** `companion_host_owned` stays **off in lab and play**, so nothing of this runs in a default
  session. Unproven: a play rehearsal (play refuses the levers that kill the copy), the by-player branch in a run
  (fixture only), the give-back reaching the guest's save at a checkpoint or mission end [I], both keys off with the
  guest's dog dying (fixture and code only [I]), Mother Base, two-PC.
- **Checks:** `pwsh -File tools/build.ps1 -Fast` 301/301 after each change; full `pwsh -File tools/build.ps1` (Debug)
  317/317 before the last commit.

## October 29, 2026: the review of those launcher and package fixes, round 2 (`wp/launcher-fixes2`, unreleased)

Same branch and worktree as the section below; still not merged, pushed, tagged or released. An independent review of that section (verdict: fix first) found one major and seven minor issues. Each was checked against the code before it was changed, and all eight are handled here. None changes play, the wire, a CFG index or a lab key, and no play default changed. Marks: [O] observed in a run or test named here, [I] inferred from code, [U] not run.

1. **The tag and source zip wording was still false** (major; `2dcd0e3`). At `c78ed3b` the tag held 16 tracked paths that the zip leaves out: `.gitignore`, `.gitmodules`, `AGENTS.md`, `Play-Offline-Coop.bat`, `native/launcher/assets/.gitattributes`, three files under `docs/evidence` and seven `docs/readme/*.jpg`. It also held MinHook only as a submodule link, while the zip carries its 21 files. A docs commit after packaging (`74291a3`) adds more differences [O: `git ls-tree -r c78ed3b` against the package's `source-files.json`].
   - The player README, `THIRD_PARTY_NOTICES.md` and `README.md` now say: the zip is made from the commit `build.source_commit` names (`build.source_dirty` false: the files did not differ from it), with MinHook's source in place of its link and without the files the programs are not built from (the notices list them by name). The tag can sit on a later commit, so the zip is compared with `build.source_commit`, not with the tag.
   - `friend_package_tests` checks that wording in the packaged README and notices, and that every tracked file the real tree's source zip leaves out is of a kind the notices name.
   - Failing first [O]: with the old texts it stopped at "README.txt and the notices name source_commit as the zip's commit". Item 3 of the section below is corrected.
2. **`friend_package_tests` depended on the developer's untracked files** (`f93dc74`). It packages the real tree, so since the untracked-file refusal any new, not yet committed file under a published folder (a scenario, a test) turned full CTest red. For that one run, the test now gives the child gits (`GIT_CONFIG_COUNT`, `core.excludesFile`) an excludes file that names exactly the tree's untracked files in the published folders. `tools/package.ps1` itself keeps no way around its refusal, which the git fixture still checks.
   - Failing first [O]: with an untracked `tools/zz-r2-untracked-probe.txt` in the worktree the test failed ("Untracked file would be published in the source zip"). With the fix it passed (45 s) with that file present; the file was then removed. The side effect noted in item 5 of the section below is gone.
3. **Every hidden run's preflight had gained the launcher's slow process test** (`8db0313`). `Get-WorkerTests` is the list of every profile, so the Normal and Research preflights of `Invoke-WorkerScenario` had gained `ddt_local_watch_process` (real helper processes, 8.5 s in the receipt, timing-sensitive) and `launcher_icon_tests`, though a hidden game run packages no launcher. Both moved to `Get-WorkerReleaseOnlyTests`, which only the Release plan adds.
   - [O] `-PlanOnly`: Normal and Normal with the background worker plan 309 tests, Release 311 (the receipt list is unchanged). `tests/worker_checks_tests.ps1` 154 checks; failing first, it stopped at "a normal-worker-off preflight skips the launcher's helper-process and icon checks".
   - [U] No Normal-profile worker check ran: its cache would be a new build folder on D: (`build/worker-checks/normal-worker-on-release`), which this round may not create. [I] The eleven fixtures that stay in every profile (0.02-0.16 s each in the Release receipt) are registered by CMake for every profile; the background-worker flag only adds two defines (`MGS_PLAY_REHEARSAL` to `mgs_coop_core`, `MGS_BACKGROUND_WORKER` to `mgs_coop_lab`). They were not run in that configuration.
4. **No test covered an abandoned setup lock** (`9b20ce7`). Two new sub-tests of `TestSetupElsewhere`: `RunSetup` is the first to wait on a lock whose owner ended without releasing it, and runs; a window that waited (`setup=waiting`) sets up once the owner ends that way. Both pass [O]. With `WAIT_ABANDONED` dropped from `SetupLock::Lock` and `SetupRunningElsewhere` (a mutation check, reverted), 5 checks fail [O].
   - In `r4` and `r5`, window C never tried a setup: the harness had given it a cache that forced `phase=error`. Its "never two setups" verdict was therefore trivially true. The runs show the folder-hold order (C opened after B's process ended), not a second setup that waited and then ran. That is now [O] by the unit tests only.
5. **The setup lock wording overclaimed** (docs). Only `RunSetup` in builds from `8c7a0da` on takes the lock. A 0.14.4 or older exe, `tools/package-install.ps1` and the worker's drop-in installs do not. Two windows of one folder were already kept apart by the folder's single-instance mutex, so the lock matters for the developer `--write-session`. Item 1 of the section below now says "a setup run by Diamond Dogs Together from this version on".
6. **A start during the closing gap exited without a window** (`bc9f305`). A window closed during setup is gone at once, but its process holds the folder for about 50 ms while it ends the setup. A start in that gap found the folder held and no window to bring forward, and exited (`r1`). The single-instance claim (now `ClaimFolderInstance` in `launcher_entry.cpp`, called by `ClaimSingleInstance`) waits up to 2 s for the folder when no window holds it, then looks for the window once more, since the next window may still be opening. An open window is still brought forward at once.
   - Failing first [O]: `launcher_ui_tests`' real-mutex cases (folder released after 150 ms, abandoned after 150 ms, held with no window, window found on the second look) failed 5 checks with the old claim. Now 386 checks, 0 failures, in four runs.
   - [U] Not run with the real exe end to end (see 8). [I] `ClaimSingleInstance` passes the old window search as the callback.
7. **The untracked-file refusal failed open** (`6240a0a`). Any failure to run git (git missing from PATH, a broken git) counted as "not a git checkout", which skipped the refusal. With a `.git` entry at the root, `tools/package.ps1` now refuses to package and says why. An unpacked source zip (no `.git`) packages as before.
   - Failing first [O]: `friend_package_tests`' git fixture, with git hidden from PATH and an untracked note, packaged it ("expected failure"). Now it is refused before anything is staged.
8. **The launcher end-to-end ran outside `tools/worker` and without the verify lock** (process; recorded, not sanctioned). Runs `r1`-`r5` of the section below used a scratch harness (`E2eHost.cs`: CreateDesktop, SW_HIDE, a kill-on-close job) on a private desktop in a fake folder. They did not use `tools/worker` and did not take the verify lock. They touched no game copy, save or Steam data [I: afterwards the fake folder held only the planted files and the log, and the setup's game check fails before any Steam lookup]. Their safety rested on the harness's own ordering, not on the worker's receipt, lock and backup/restore. This round ran no such harness, so item 6 is proven by unit tests only. Whether launcher end-to-end runs may use such a harness, or must move under `tools/worker`, is for the user to decide.

**Checks** [O]: `pwsh -File tools/build.ps1 -Fast` 301/301 at `8db0313`; full CTest (Debug) 317/317 at `8db0313`; `tests/worker_checks_tests.ps1` 154 checks; `friend_package_tests` passed with an untracked file in the tree.

**Release checks and a new local package** [O] (a check of the tooling, built and audited only; nothing uploaded; it still says 0.14.4 and must not be released as such). Under the verify lock (D: had 5.9 GB free): receipt `20261030-031742-378e13becd5542889d9257d250299f32` passed with 311 audited tests; its run folder is archived under `%USERPROFILE%\mgs-coop-run-archive\next2\20261030-031742-378e13becd5542889d9257d250299f32`. `tools/package.ps1 -Friend -CheckReceipt` with that receipt, from the clean tree at `205b13c`, into the session scratch `next2/launcher/r2/dist/DiamondDogsTogether-0.14.4-20261030T032122Z-38459f1b`:

| File | Bytes | SHA-256 |
|---|---|---|
| `DiamondDogsTogether-0.14.4.zip` | 3,081,001 | `8C08872381AEB5753E8CB0DDAB63C06970974BD30E924A3B724E60C7676622DE` |
| `DDT-source-0.14.4.zip` (1,410 source files plus `source-files.json`) | 8,344,719 | `ADB69998EFA2C0BF9D42CC5B1E97FB00840D7627D5A073B8134F1085746AC2A0` |
| `DiamondDogsTogether.exe` | 2,370,048 | `604BFC6EF745D4C9BA1662A10304403D8218EF7CEDA4B51C63B473CCE34043E0` |
| `dinput8.dll` | 7,060,992 | `51CBC61378F9872561BFB2BA32EDABC894A958F822B627D00EA3628E3C81A4EC` |

- Audit 26/26 [O] (`next2/launcher/r2/audit-r2.py` in the session scratch): the new wording in the packaged README and notices, with each left-out kind named; `build.audited_tests` 311, including both launcher checks; `source_commit` `205b13c`, `source_dirty` false; the exe and dll are the receipt's, build id `0.14.4+205b13c` in both.
- Against `205b13c` [O]: the 16 tracked files the zip leaves out are all of the kinds the notices name; the 21 zip files git does not track are MinHook's; the other 1,389 zip files equal their blobs at that commit (line endings aside: the checkout writes CRLF).
- This STATUS record is written after the package, so it is one of the later commits the texts describe.

## October 29, 2026: the 0.14.4 review's launcher and package findings fixed (`wp/launcher-fixes2`, unreleased)

Branch `wp/launcher-fixes2` from `dev/oct7-continue` `01fd055`, in the worktree `D:/mgs-coop-wt/launcher-setup`; not merged, pushed, tagged or released. It fixes the five non-blocking launcher and package findings of the 0.14.4 pre-release review. None changes play, the wire or a lab key. Marks: [O] observed in a run or test named here, [I] inferred from code, [U] not run.

1. **Two automatic setups could run at once after a window closed during setup** (`8c7a0da`). The closing window released the folder (its single-instance mutex) before its model was destroyed. The model then waited for a pending update check (WinHTTP waits up to about a minute on a stalled network) before it ended the setup, so a window opened again at once started a second automatic setup next to the first. Both ends are closed now:
   - The closing end. `~RealModel` ends the setup first: the real window calls ExitProcess before any join, also for a setup that has just finished but was not yet picked up. `HoldFolderWhileClosing` holds the folder while the phase is `setting_up`, so the process ends while it still owns the mutex.
   - The next window. `RunSetup` holds a per-folder setup lock (`Local\mgs-coop-setup-<16 hex>`, the play mutex's folder hash). A window whose automatic setup finds it taken waits (`setup=waiting; reason=other_setup`), then looks at the folder again, since the other setup may have finished it. A setup started anyway (Try again, `--write-session`) stops with a sentence (`setup=refused; reason=other_setup`) and changes nothing. This lock goes beyond the review's suggested fix: for a setup run by Diamond Dogs Together from this version on, the guarantee does not depend on timing. (Corrected in round 2, the section above: a 0.14.4 or older exe, `tools/package-install.ps1` and the worker's drop-in installs take no such lock, and two windows of one folder were already kept apart by its single-instance mutex.)
   - Failing first [O]: `launcher_ui_tests` 1 failure ("co-op getting ready holds the folder until the setup has ended"), `ddt_core_tests` 12 failures (`TestCloseDuringSetup`: the closed model's setup ran on to `setup=done` and wrote `friend-install.json` while the model waited for a stalled update check; `TestSetupElsewhere`: a second setup started). After the fix: 377 checks / 0 and 1284 / 0.
   - End to end [O], hidden: the Release receipt's own exe (`0.14.4+c78ed3b`) on a private, never-displayed desktop (CreateDesktop + `lpDesktop`, SW_HIDE, a kill-on-close job with UI limits; the input desktop stayed `Default`). It ran in a fake game folder in the session scratch: a 16 GiB sparse dummy `mgsvtpp.exe` (0 bytes allocated), a copy of `steam_api64.dll`, a dummy loader and `package.json`. The update feed was on 127.0.0.1 and accepted every connection without answering. The setup never passes its game check (the dummy is not the retail exe), so it never looks up Steam or reads a save; afterwards the folder held only those files and the log. Runs `r4` and `r5` (harness `next2/launcher/e2e/e2e-setup.ps1` in the session scratch; evidence `next2/launcher/e2e/runs/<run>`):
     - While the harness held the folder's setup lock, the window waited 5 s and started no setup (`setup=waiting`). Once the lock was released, it logged `setup=auto` and `setup=start`.
     - The window was closed 1.5 s into that setup, while it was still hashing and its update check still waited on the stalled feed. The window was gone after 5 ms and the process ended 52-54 ms after WM_CLOSE (`window=closed_during_setup`, no `update=check_failed`). When the window was gone, the process was still alive and still held the folder's single-instance mutex and its setup lock.
     - A window started 12-13 ms after the close opened only after that end (its `window=open` follows `window=closed_during_setup`). In `r1`, such a window started while the old process was still ending found the folder held and exited without a window. (Round 2: that order was a race, not a guarantee; such a start now waits for the folder, `bc9f305`. Window C never tried a setup, since its cache forced `phase=error`, so these runs do not show a second setup waiting.)
     - A window closed outside setup frees the folder at once (the mutex was gone 0.33 s after the close), while its process stays about 27 s until its update check times out (`update=check_failed ... 12002`), as before.
   - [U] The 0.14.4 exe was not run through the same harness: making its setup run long enough would let it read the real Steam saves. The failing-first tests show the old behaviour. No visible-desktop or two-PC run.
2. **The Release receipt left out shipped tests** (`bc56c0d`). `Get-WorkerTests` (the receipt's list) grows from 298 to 311 (round 2: the two launcher tests below now run only in the Release receipt, `Get-WorkerReleaseOnlyTests`; Normal and Research preflights run 309):
   - New: `dog_guard_tests`; the outcome attribution, companion, buddy companion, dog guard, relief and relief sync Lua checks; `launcher_icon_tests`; `ddt_local_watch_process` (the exe's real `--local-watch` helper, hidden, on temp folders).
   - Missing since 0.13.x / 0.14.3, not only since 0.14.4: `partner_act_log_tests`, `partner_ammo_leak_tests`, `partner_dm_overlay_tests`, `partner_ride_seat_tests`.
   - `Assert-WorkerInventory` admits the exact arguments of the three that take any.
   - The new `Assert-WorkerCoverage` refuses a Release inventory test that is neither audited nor excluded by name. The excluded list now also names `ddt_runtime_dat_integration` (reads the game folder), `ddt_launcher_screens` (mock pages in a DX11 window) and `launcher_assets_current` (Python asset check; `launcher_icon_tests` checks the icon the exe embeds).
   - Failing first [O]: `tests/worker_checks_tests.ps1` stopped at the shipped-module check; it now passes 147 checks.
   - Receipt `20261030-014929-6188027e7b644a37bf51164ae9fc7020` passed with 311 audited tests (`ctest-results.xml` 311 / 0 failures) [O]; its run folder (receipt, exe, dll, logs) is archived under `%USERPROFILE%\mgs-coop-run-archive\next2\20261030-014929-6188027e7b644a37bf51164ae9fc7020`.
3. **"The tag holds the same source" was inexact** (`d6da17f`, corrected in round 2 by `2dcd0e3`). `tools/package-readme-player.txt`, `THIRD_PARTY_NOTICES.md` and `README.md` said:
   - The source zip is the complete source of the two programs.
   - ~~The tag `v<version>` holds the same code and differs only by the package's record in `docs/STATUS.md`, written after the build.~~ Refuted by the round-2 review: the tag also holds repository-only files the zip leaves out, holds MinHook only as a submodule link, and can sit on a later commit. The texts now compare the zip with `build.source_commit` and list what it leaves out (section above, item 1).
   - `package.json` `build.source_commit` names the exact commit.
   - README.md: the source zip exists from 0.14.4 on; older packages carry `mgs-coop/source.zip`.
4. **The notices said the exe downloads nothing** (`d6da17f`). `THIRD_PARTY_NOTICES.md` now gives the exe's only network access: the update check against this repository's GitHub release feed and, on Update now, the download of that release's zip (WinHTTP). The setup downloads nothing. The player README has a short NETWORK section with the same facts, and README.md says the same [I: `core_update.cpp` is the only WinHTTP user; the local test's port probe only binds a local socket].
5. **Untracked files could reach the public source zip** (`c78ed3b`). `tools/package.ps1` lists what git neither tracks nor ignores in the published folders (`git ls-files --others --exclude-standard`, read as UTF-8; the MinHook dependency is left to the receipt; nothing is listed outside a git checkout).
   - It refuses such a file by name before it creates the dist folder: "Untracked file would be published in the source zip: ...".
   - `build.source_dirty` counts those files too.
   - Failing first [O]: `friend_package_tests` (a git fixture of its own) packaged an untracked `tools/notes.txt`. Now it is refused, and untracked code is refused even when the receipt hashed it. A clean tree, a tracked uncommitted file and an untracked `.log` record `source_dirty` false, true and true.
   - Side effect: `friend_package_tests` (full CTest) now fails while the checkout has an untracked file with a published extension under `native`, `lua`, `tools`, `tests`, `third_party`, `cmake` or `docs`. `git add` it or move it out; the message names it. (Gone in round 2, `f93dc74`: the test's real-tree run no longer depends on untracked files; `tools/package.ps1` still refuses them, and now also refuses to package a git checkout whose git cannot be run, `6240a0a`.)

**Local package** (a check of the tooling, built and audited only; nothing uploaded; it still says 0.14.4 and must not be released as such). `tools/package.ps1 -Friend -CheckReceipt` with the receipt above, from the clean tree at `c78ed3b`, into the session scratch `next2/launcher/dist/DiamondDogsTogether-0.14.4-20261030T015332Z-d51849d1`:

| File | Bytes | SHA-256 |
|---|---|---|
| `DiamondDogsTogether-0.14.4.zip` | 3,080,276 | `75068AEA56D47739D8ADC54604E803BBFFF9CF18A52A2816951EE62FC3EC6732` |
| `DDT-source-0.14.4.zip` (1,410 source files plus `source-files.json`) | 8,329,914 | `8FB527E3787A7396E073D801C69187A22D28BD07DB869421D6E5917F93776194` |
| `DiamondDogsTogether.exe` | 2,369,536 | `A0796E3ABE4FD34B10EEC730638D9883A3212B4A8F7F09B65101FCE8C07C9BE9` |
| `dinput8.dll` | 7,060,992 | `6390E3F31122D5A643C540B45928066A368EC065EC023129EB125607D8CDF32F` |

- **Package audit 54/54** [O]. It covers the 34 checks of the 0.14.4 audit plus:
  - the corrected README and notice texts;
  - `build.audited_tests` = 311, with each of the 13 added tests in the receipt;
  - `source_commit` `c78ed3b`, `source_dirty` false;
  - every source-zip file tracked by git or the MinHook submodule.
- **Extras** [O]: build id `0.14.4+c78ed3b` in both programs, and no personal path or e-mail in either zip.

**Checks** [O]: at `c78ed3b`, `pwsh -File tools/build.ps1` (Debug) 317/317, `tests/worker_checks_tests.ps1` 147 checks and `friend_package_tests` passed; `pwsh -File tools/build.ps1 -Fast` 301/301 after the launcher fix (`8c7a0da`).

## October 29, 2026: 0.14.4 (vehicles in Afghanistan and Africa, the vehicle decisions, launcher polish)

**Package (2026-10-29):** `dist/DiamondDogsTogether-0.14.4-20261030T010348Z-6cc0fbb5/`, built by `tools/package.ps1 -Friend -CheckReceipt` from the clean tree at `ecd3b60` (the binaries are from the version bump `89f8d93`; `ecd3b60` only rewords two design-doc lines so the source zip carries no persona name; the first package `20261030T004356Z-a5bd9d1b` is superseded) with Release receipt `20261030-003929-a54b029ae1e5453fb2853f94263a8b66` (passed, 298 audited tests). `DiamondDogsTogether.exe` and `dinput8.dll` both carry build id `0.14.4+89f8d93` and are the receipt's own files.

| File | Bytes | SHA-256 |
|---|---|---|
| `DiamondDogsTogether-0.14.4.zip` (the player zip: exe, dll, `mgs-coop/package.json`, README, notices and licences; no source) | 3,079,270 | `5C49FA7E49AE73787BDCC96625B8BD17DB6B67AC68C89661044967C51F5A374B` |
| `DDT-source-0.14.4.zip` (1,410 source files plus `source-files.json`) | 8,309,457 | `4B3BD8EE4666F8F3AF7F864980BD0E637C831C1A2394C0452EEC09063D6E1FF7` |
| `DiamondDogsTogether.exe` | 2,368,000 | `0D376486315C54AA3AC173656B6D06C31130674EFE608C5E1E1F01A676E2D2B8` |
| `dinput8.dll` | 7,060,992 | `8D9D1CA4751E39BED6D0E5F9280FD51A41C532FBEEEB582A10295272ACFABF3C` |

- **Package audit:** 34/34 (both zips present; the player zip holds exactly its 9 files and no source or script; exe and dll are the receipt's; `package.json` version 0.14.4, every `files[]` hash and `source_archive` match; every source file is in the source zip with its hash and matches the tree at `89f8d93`; `tools/check-release.ps1` accepts a simulated draft release carrying both zips). Build id strings `0.14.4+89f8d93` in both programs; no personal path or e-mail in either zip.
- **Windows Defender** (`MpCmdRun.exe -Scan -ScanType 3 -File <path> -DisableRemediation`, one scan per file; engine 1.1.26100.4, signatures 1.463.24.0): the exe, the dll and both zips "found no threats"; no detection recorded and the four hashes unchanged after the scans. (A Debug build of the merged launcher was flagged once during the launcher work, see the launcher polish section below; this Release build was not.)
- **To release:** upload **both** zips to the GitHub release `v0.14.4`, then run `pwsh -NoProfile -File tools/check-release.ps1 -Version 0.14.4 -PackageDir dist/DiamondDogsTogether-0.14.4-20261030T010348Z-6cc0fbb5` before publishing. Nothing was pushed, tagged or released by the packaging.

Branch `dev/oct7-continue`. Releases everything since 0.14.3 (`48ce7da`); none of it is two-PC tested yet. Proof runs are in the sections below.
- **Vehicles in Afghanistan and Africa free roam (on in play at 30010 / 30020):** M4 VEHICLE_GONE (a Fulton or a host destruction removes the vehicle on both copies) and VEHICLE_STATE (a parked vehicle keeps the host's pose and body life on both), D11 `world_loadout_block` (the guest's LOADOUT and VEHICLES drops refused), D12 `world_vehicle_break` (the host judges and wrecks a vehicle the guest destroys, or despawns it with a player within 15 m), D14 `world_vehicle_credit` (a Fultoned world vehicle credited on both co-op saves). Mother Base vehicle sync and the wreck parity stay lab only.
- **Off by default:** `companion_host_owned` (M1, M2 and the D7 dog guard) and `world_relief` (M3, the sortie vehicle on both copies).
- **Launcher** (merge `ffaea6b` of `wp/launcher-polish`): the logo at every icon size, a restart after an update (`--after-update` handshake, rollback), automatic setup with no Setup page, Settings > Debug mode with Play > Local test (two windows), and the source as its own `DDT-source-<version>.zip` release asset.
- **Version.** 0.14.4 in `CMakeLists.txt`, `README.md`, `tools/package.ps1` and `tools/probe.ps1`; `tools/set-session-mode.ps1`, `tools/package-install.ps1` and `tools/dev/Play-LocalCoop.ps1` accept 0.14.4 and still 0.14.3; `tests/ddt_core_tests.cpp` checks `IsVersionNewer("0.14.5", "0.14.4")` (0.14.4 > 0.14.3 was already checked).
- **Checks at the bump commit `89f8d93`:** `pwsh -File tools/build.ps1 -Fast` 301/301; `pwsh -File tools/build.ps1` (Debug) 317/317.

## 2026-10-29: the user's vehicle and companion decisions (D7, D11-D15) integrated (`dev/oct7-continue`, unreleased, not two-PC tested)

Both decision tracks are merged into `dev/oct7-continue` with merge commits only (local, nothing pushed, tagged or
released): `4e3d0e5` merges `wp/dog-guard` (D7, with its review fixes) and `92c4f9f` merges `wp/vehicle-decisions`
(D11, D12, D14, with their review fixes). Both reviews returned "fix first"; every finding was fixed on the track
branches before the merge, and the claims they refuted are corrected in the sections below. Details:
`docs/vehicles-companions-design.md` 8.9-8.12 (results), 8.13 (the combined smoke), section 9 M2 and M4, section 10
(the decision table), section 11 (review and integration log).

**Merge.** Conflicts were only in lists and docs: the world chunk order is now `... world_vehicle_state`,
`world_vehicle_rules`, `world_companion`, `world_companion_dog`, `world_companion_guard`, `world_relief`,
`world_relief_sync` (CMake, the bootstrap patch, its check and `world_sync_source.hpp.in` agree). No op code or CFG
index collides: D7 adds no op (the dog's state byte rides COMPANION 0x30 and COMPANION_POSE 0x39) and no CFG index
(it reads `companion_host_owned`, `r[16]`); the vehicle track adds REQ_VEHICLE_GONE cause 4 (U8 1..4) and REJECT 11
(protocol section 19) and the `r[15]` bits 8, 16 and 32. The vehicle track's design sections 8.9 / 8.10 became 8.11 /
8.12 (every reference renumbered). All four 2026-10-29 STATUS sections of the two tracks are kept below.

**Per decision** (FRAME = frames on both copies; play = `-Mode play -WanSim bad` rehearsal):
- **D7, the guest's copy of the host's D-Dog.** Built: the native guard (`native/dog_guard.cpp`: the dog's
  `ProcessDamages` and life-update slots) and `lua/coop/world_companion_guard.lua`: hits on the guest's copy are
  dropped while armed, the copy follows the host dog's state byte, and after a death the guard cannot see it is
  brought back from the host's live state (about 2.6 s). Proven in the lab [O]: E19 passes (`3922cc80`, `7fe2845e`,
  `cc2e15d2`), the restore (FRAME `606048af`, `cc2e15d2`), the follow (the down state on both screens, `3f69e3b7`, `606048af`) and the re-call (FRAME `606048af`, `cc2e15d2`),
  the hold under a simulated link loss (`9fa48400`). **Play default: off** (it follows `companion_host_owned`, off in
  lab and play; a default session installs nothing, `19e438fe`, and the combined smoke `a1d7aaa1`). Unproven: the guard's drops, the
  follow, the restore and the re-call in play (play refuses their levers; no guard fired at the dog in `1713b0a1` or
  `f7033e2e`); a real link loss [I]; the host-side pass-through [I]; the 30 heroism lost on the guest's PC at each
  copy death (open question); M1's own gaps; two-PC.
- **D11, the guest's LOADOUT drop.** Built: `world_loadout_block` (`r[15]` bit 32): the guest's iDroid LOADOUT and (the
  reviewer's pick) VEHICLES items are inactive in co-op and refused with a toast; the host's are untouched. **Play
  default: on.** Proven: FRAME `c0203fdd` (lab), play `70146170`, `47b95ae7`, and both items with both toasts at the
  shipped defaults `f729d7f4`, `33a7fa6e`; again in the combined smoke `a1d7aaa1`. Unproven: two-PC.
- **D12, a vehicle the guest destroys.** Built: `world_vehicle_break` (bit 8): the guest's unattributed break goes to
  the host (cause 4), which judges it (300 m, or its copy already badly damaged), refuses it when the guest was too far or while the
  host rides it,
  holds it while D-Dog or a soldier is seated (REJECT 9 after 120 s), and wrecks its copy, or despawns it while a
  player is within 15 m (the blast rule). **Play default: on** in Afghanistan and Africa. Proven: FRAME `c0203fdd`,
  `09333f05`, `7762536a` (lab), play at the shipped defaults `f729d7f4`, `33a7fa6e` (the Despawn with the host 8 m
  away, life unchanged; the wreck seen intact, burning and burnt on the host's screen); 120 m judged near
  (`0f3f00f1`, logs only). Not in the combined smoke. Unproven: a soldier-crewed vehicle stays split after the hold
  (open question for the user); a real launcher or rifle kill at range; the 15 m margin is not a measured radius
  [I]; two-PC.
- **D13, the M4 play defaults.** Nothing changed: VEHICLE_GONE and VEHICLE_STATE stay on in play in 30010 / 30020;
  Mother Base and the wreck parity stay lab only. In the combined smoke `a1d7aaa1` a jeep the guest drove with the
  host in a back seat stayed parked where the guest left it on both screens. Two-PC still to do.
- **D14, Fulton rewards.** Built: `world_vehicle_credit` (bit 16): the receiver removes its copy with
  `Seize {"Instant"}`, so both co-op saves are credited once each; the sortie vehicle keeps one credit (the host's).
  **Play default: on.** Proven: FRAME `c0203fdd` (lab: `4wdW` 1 -> 3 on both copies after the mission end, the GMP
  fee only on the player who Fultoned), play `70146170`, `47b95ae7`; the host's Fulton again in `a1d7aaa1`. Unproven: APCs, tanks and trucks; a
  far or unrealized receiver; the commit in play rests on the lab mission end [I]; two-PC.
- **D15, the guest's bail out of a rolling jeep.** Accepted as is; nothing built and not run. For the two-PC session.

**Combined smoke of the merged head** (one hidden run, worktree `D:/mgs-coop-wt/relief` on `wp/integ-smoke` =
`d9ce8d3`; `tools/worker/scenarios/integ-smoke-play.txt`, `-Mode play -WanSim bad`, seed `africa-jeep`, no labsetting
and no lab lever, so the shipped play defaults). Run `a1d7aaa1`: **every expectation met**, Closed, baseline and
real-save hashes verified, `Forced=False`; no error, `viewer_changed` or fault line on either copy; RTT 175-189 ms,
loss up to 14.2 %. FRAME on both copies for the pairing, D11, the drive and the Fulton (session scratch
`complete/frames/integrate/`):
- [O] Both games reached gameplay and paired; every module at its default (`vehicle_rules ... break=1; credit=1;
  loadout=1`, `dog_guard=off;reason=key_off;slots=0` and `installed=0` on both).
- [O] `companion_host_owned` is off in play, so there is no host dog on the guest's screen to check: each PC kept its
  own D-Dog (the host's beside Snake on the host's screen; the guest's own rode in the guest's jeep).
- [O] D11: the guest's LOADOUT and VEHICLES refused with both toasts drawn; the host's VEHICLES list opens.
- [O] A jeep the guest drove with the host in a back seat (the board hook's passenger seat and camera), then both got
  off: the host's VEHICLE_STATE resumed and sent the pose (`why=moved; d_mm=28138`), the guest's copy was already there
  (`step=same; d_mm=1`); the parked jeep at the same spot on both screens. [O, meaning U] its state word then read
  `sv=1` on the host's copy and `sv=3` on the guest's (logged, not written by VEHICLE_STATE).
- [O] D14: the host's hand Fulton of a jeep, credited on the host (`via=own`) and on the guest (`credit_seize`,
  `via=receiver`, `applied; via=seize; ms=250`); the GMP fee on the host only; the jeep gone on both screens.
- Not in the smoke: D12 (rehearsed in `f729d7f4` / `33a7fa6e`), the dog guard under its key, a mission end, D15. No
  defect found.

| Run | Scenario | Result |
|---|---|---|
| `a1d7aaa1` | `integ-smoke-play.txt` (`-Mode play -WanSim bad`, shipped defaults, merged head `d9ce8d3`) | Every expectation met; pair, D7 default off on both, D11 refusals, guest-driven jeep with the host as passenger parked on both, D14 Fulton credited on both |

**Checks.** Full CTest 314/314 on the merged head `92c4f9f` (`pwsh -File tools/build.ps1`, Debug, in the worktree
`D:/mgs-coop-wt/relief` on `wp/integ-smoke`); the contract checks `generate_partner_contract.py`,
`generate_equip_writer_pins.py`, `generate_soldier_warp_contract.py`, `generate_nametag_contract.py` and
`generate_downed_damage_contract.py` `--check` pass. Nothing pushed, tagged or released.

## 2026-10-29: D7 review fixes (`wp/dog-guard`, unreleased, not two-PC tested)

An independent review of `wp/dog-guard` at `8a294ce` (verdict: fix first) found one major and four minor problems
and refuted four claims. All five findings were verified against the code and fixed on `wp/dog-guard` (new commits
only), each with a test that failed first. Design: `docs/vehicles-companions-design.md` 8.10 (fixes, runs, results),
section 9 M2, D7 in section 10, the review log in section 11; protocol section 15 note.

**Fixed.**
- [O] **The guard is installed only under the key** (`e38f2f4`). Before, both PCs patched the dog's two vtable slots in
  every session whose partner gate passed, play included, whatever `companion_host_owned` said. Now with the key off
  (the lab and play default) neither slot is touched: a default play rehearsal (`19e438fe`, `-Mode play -WanSim bad`)
  logged `dog_guard=off;reason=key_off;slots=0` and `dog_state ... installed=0` on both copies, with the host's D-Dog
  fighting beside Snake on the stock code. The unarmed hook's pass-through is proven on the guest's PC (the copy died
  of guard fire with the guard off: `7fe2845e`, `cc2e15d2`, `9fa48400`); on the host's PC it is the same role-free
  branch (a host never arms) [I]: the host's dog took no real hit in any run.
- [O] **The guard holds through a link loss** (`e38f2f4`, `d413778`). An unpair, a link drop, a lower level or a new
  epoch no longer disarms it: an armed guard is held drop-only (no host state followed, no lab op) until the session or
  the mission ends, and a hold never starts a guard. A copy that dies while the host's state is unknown now waits until
  it is known (`host=unknown`, then `host_known`) instead of being treated as the host's dog being down. At runtime
  through a new guest lab lever, `companion_lab link_sim on|off` (`c256368`), which makes the guard module alone see the
  link as lost (`9fa48400`): the hold kept dropping guard hits for about 25 s with the copy standing over the prone
  guest (FRAME on the guest's screen), and a death by guard fire in that state waited 5.1 s, then restored once the
  state was known (`back; ms=6172`). [I] that a real link loss takes the same path: no worker lever cuts the world link.
- [O] **The restore no longer fights the game** (`d413778`). It clears only the dead DOG bit of `deadBuddyType`, once
  after the death (and once before a reload if set again); `buddyCommonFlag` is left to the game. One vars line per
  death in five restores and a mirrored death, every reload worked with the flag at 0; FRAME on both copies (`cc2e15d2`).
- [O] **Diagnostics** (`e38f2f4`, `5ef2470`, `19280d3`): the receipt reports the dog's record only when under a second
  old; the native log caps reset each mission; `companion_lab dog_state` on a disarmed guest reads the live record and
  names `installed=` and `fresh=`. The age gate missed the first second after a send-away (`9fa48400`: `life=0;
  state=2` 0.9 s after it), so an arm during a restore is now marked `restoring=1`.
- Fixture `world_companion_guard_lua_check` 65 -> 115 checks (the risky branches above, play refusals of `dog_stun`,
  `recall`, `kill_mirror`, `guard`, `link_sim`); `dog_guard_tests` adds the install gate, 30 admission cases,
  freshness and caps.
- Corrected claims (`3d0046c`): "Defaults: unchanged" (the guard was installed in play); the E19 host frames (no
  host dog in them); the restore's vars repair (it ran again after every send-away); `606048af`'s legs (two windows
  read `within=0`).

**Found.** [O] Each death of the guest's copy on the guest's PC shows the stock "D-Dog Near Death [Heroism -30]" there
(`9fa4-guest-hd-unk-dead`; not on the host's screen): every death the guard cannot see costs the guest's PC 30
heroism. Whether it reaches the guest's own save [U]; whether to give it back is an open question for the user (D7).

**Runs** (hidden worker; archived under `%USERPROFILE%\mgs-coop-run-archive\decisions\<run>`; frames as JPEG in the
session scratch folder `complete/frames/dog/`; every run closed with the baseline and the real-save hashes verified).

| Run | Scenario | Result |
|---|---|---|
| `7fe2845e` | `companion-dog-guard-review.txt` (lab, key on) | One expectation failed: a waitlog for a dog `Damage` message (the dog posts none that the Lua observer sees; none in the pre-D7 runs either). E19 through the new admission: 13 hits dropped, no death. **Guard off** in the line of fire: the copy died of guard fire 70.7 s later (`att=other`): the unarmed hook passes hits through. Restores with the one-shot repair: back in 2485 ms twice |
| `19e438fe` | `companion-dog-guard-play-off.txt`, `-Mode play -WanSim bad`, default keys | No expectation failed. Nothing installed on either PC (`dog_guard=off;reason=key_off;slots=0`, `installed=0`); the host's dog in the alert beside Snake on the host's screen |
| `cc2e15d2` | `companion-dog-guard-review-frames.txt` (lab, key on) | No expectation failed. FRAME on both copies: the kill restore (copy carried off, back beside the host; the host's dog beside Snake), the host's dog down ("D-Dog Near Death" on both) and re-called (beside Snake on both); the guard-off death (the copy lying dead beside the prone guest), restored in 2669 ms |
| `9fa48400` | `companion-dog-guard-hold.txt` (lab, key on) | No expectation failed. `link_sim on` in the E19 shield: `step=held; reason=hold`, 15 more hits dropped in about 25 s, `life=10000; state=0`, no death (FRAME of the copy over the prone guest, the guard aiming); guard off under `link_sim`: a real death 71.5 s later, `host=unknown`, nothing for 5.1 s, `link_sim off` -> `host_known; host=alive` -> send-away, reload, back in 6172 ms; `kdog` on both |

**Defaults: unchanged, and now true for the guard too.** `companion_host_owned` stays **off in lab and play**, and with
it off the guard installs nothing. Still missing before the play default can flip: the guard's drop, the follow, the
restore and the re-call have no play rehearsal (play refuses their levers, and guards in play have not fired at the
dog); a real link loss with the dog held was never run; the host's dog never took a real hit with the hooks
installed; the heroism question above; M1's own gaps (earlier sections); no two-PC session.

**Checks.** Fast CTest 298/298 after each step; full CTest 313/313 (`pwsh -File tools/build.ps1`, at `04a3a41`); the
contract checks `generate_partner_contract.py`, `generate_equip_writer_pins.py`, `generate_soldier_warp_contract.py`,
`generate_nametag_contract.py` and `generate_downed_damage_contract.py` `--check` pass. Nothing pushed, merged or
released.

## 2026-10-29: D7, the guest's copy of D-Dog cannot be hurt (`wp/dog-guard`, unreleased, not two-PC tested)

The user's decision D7 (2026-10-29): the guest's copy of the host's D-Dog must not be killable by anything on the
guest's PC; its health and state follow the host's dog; where a guard cannot cover a case, the copy comes back from the
host's live state (the restore fallback). Built on `wp/dog-guard` (from `dev/oct7-continue` `e2249e4`, worktree
`D:/mgs-coop-wt/companion-host`), behind M1's key `companion_host_owned`. Design: `docs/vehicles-companions-design.md`
8.9 (static reads, build, results), 6.12, section 9 M2, D7 in section 10; protocol section 15 (the state byte).

**Built** (commits `cb848ed`, `a2f84ee`; fixtures written first).
- [O static] The dog has no invincibility command (its three Lua dispatchers take no damage or life argument;
  `SetDisableDamage` has no dog handler) and the damage parameters are shared by every target, so the guard is native:
  `native/dog_guard.cpp` replaces two exact vtable slots of the D-Dog classes (bodies, call sites and slot values pinned
  by SHA-256 in `native/include/mgscoop/dog_guard.hpp`): the dog damage handler's `ProcessDamages` (`0x231AD38`) is
  skipped for instance 0 while the guest's lease is armed, and the dog life update (`0x231FD60`) drops pending damage,
  undoes a forced death the module did not issue and applies the host's dog-state byte to record 0. The lease counts the
  dog's own updates (a pause never expires it); disarmed at mission start, end and termination; the host can never arm.
  Receipt `MGS_COOP_DOG_GUARD_V1`. Unit tests `dog_guard_tests`.
- The host dog's **state byte** (valid, normal / asleep / down, life in 31ths) rides COMPANION_POSE and COMPANION
  `state`, both sent 0 before: **no wire change** (same ops, sizes and ranges; an older host sends 0 = not valid).
- New world chunk `lua/coop/world_companion_guard.lua` (after `world_companion_dog`): the host reads its byte; the guest
  arms while its copy of the host's dog is loaded and paired; the **restore fallback** (shipped behaviour under the key):
  a death while the host's dog lives -> the dead-buddy vars back, the copy sent away after 1.5 s, M1's reconcile primed
  to load the host's dog now (three per mission, 60 s each, logged `event=dog_restore`); a copy gone without a Dead
  message primes it too; a death mirrored from the host's waits for the host's re-call. Lab levers `companion_lab
  dog_life|dog_stun|recall` (host), `kill_mirror|guard` (guest), `dog_state` (both, also in play). Fixture
  `world_companion_guard_lua_check` (65 checks).

**Runs** (hidden worker; archived under `%USERPROFILE%\mgs-coop-run-archive\decisions\<run>`; frames as JPEG in the
session scratch folder `complete/frames/dog/`; every run closed with the baseline and the real-save hashes verified).

| Run | Scenario | Result |
|---|---|---|
| `3922cc80` | `companion-dog-guard.txt` (lab) | No expectation failed. E19 passes: about 85 s of guard fire through the guest's copy, 50 hits dropped, `life=10000; state=0`, no Dead; FRAME of the copy standing over the prone guest (`3922-guest-grd-shield-1`, `-3`). Restore after `kill_mirror`: back in 2585 ms (logs) |
| `3f69e3b7` | `companion-dog-follow.txt` (lab) | One expectation failed: the marked walk leg (the host's dog stayed where it was). Follow: hurt 30 % (guest life 3226), asleep (`STN` on both), awake, down (FRAME on both: "D-Dog Near Death" and the Fulton extraction), the host's re-call (`recalled; ms=36052`); the restore filmed from behind the host (the host's body hid the returned dog) |
| `1713b0a1` | `companion-dog-guard-play.txt`, `-Mode play -WanSim bad` | No expectation failed. M1 seeds in play, the guard armed with the host's byte, one dog following the host on both screens (walk 0.37-2.93 m, run 1.46-2.77 m), the host's alert applied on the guest's copy, the dog beside Snake on both screens; no guard fired at the dog (0 drops) |
| `606048af` | `companion-dog-follow-frames.txt` (lab) | No expectation failed. FRAME on both copies of the walk and run legs (0.35-1.09 m, 1.07-2.57 m; not every window within: a walk window of 3.73 m as the dog stopped after the run and an idle window of 3.65 m after the restore read `within=0`, review 2026-10-29), the restore (before, the copy carried off, the copy beside the host again; 2585 ms), hurt, down and the re-call (`recalled; ms=36069`) |
| `f7033e2e` | `companion-dog-guard-play-fight.txt`, `-Mode play -WanSim bad` | One expectation failed: in about 190 s of alert with the guest prone beside the dog no guard fired at it (0 drops; the guest's copy logged the alert `applied=0`); the dog stood over the prone guest beside Snake on both screens, `life=10000; state=0`, no Dead |

**Defaults:** `companion_host_owned` stays **off in lab and play**. (Correction, review 2026-10-29: "unchanged" did not
hold for the guard itself. Its two vtable slots were installed on both PCs in every session whose partner gate passed,
play included, whatever the key said (`1713b0a1`, `f7033e2e`: `mode=play` with `dog_guard=installed`), and the
unarmed pass-through had no runtime proof; since `e38f2f4` they are installed only under the key; see the section
above.) The key covers M1 and M2 together, and
not every path under it has a FRAME on both copies and a passing play rehearsal: the guard's drop, the follow, the
restore and the re-call are proven in the lab only (play refuses the levers that drive them, and the guards of both
play rehearsals never fired at the dog); M1's review fixes have no play rehearsal (the Quiet park with the host's Quiet out, no
horse / walker / Quiet shown on the guest's PC, the session end, the reconcile backoff); the guest's iDroid buddy
request and dismiss are not refused, and the host's sortie-prep and iDroid buddy lists were not reached; no two-PC
session. Not covered either: the dog's takedown result reaching the guest; what makes a held copy sit 2.2 m off an
unchanged target with repeated drift warps when the host's dog stands still (`3f69e3b7`) [U]; a visible limp for a hurt
dog [U].

**Checks.** Fast CTest 298/298 after each step; full CTest 313/313 (`pwsh -File tools/build.ps1`, Debug) and the
contract checks `generate_partner_contract.py`, `generate_equip_writer_pins.py`, `generate_soldier_warp_contract.py`,
`generate_nametag_contract.py` and `generate_downed_damage_contract.py` `--check` pass. Nothing pushed, merged or released.

## 2026-10-29: D11 / D12 review fixes and the D12 blast rule (branch `wp/vehicle-decisions`, unreleased, not two-PC tested)

The independent review of the vehicle decisions (`7d8c95e`) returned `fix_first`; the user took the reviewer's picks
for its two open questions. Fixed on `wp/vehicle-decisions` (normal commits on top of the decisions, not merged, not
pushed): `8b00f80` (the review fixes), `157d73d` (docs), `0e79337` (the blast rule), `3aac7be` and `f466d30`
(scenarios), `f60d6ce`, `290d987` and this section (docs). Details and every line quoted below: `docs/vehicles-companions-design.md` sections 8.12 (results),
10 (D11, D12) and 11 (the review log); `docs/world-sync-protocol.md` section 19.

- **D12 (a vehicle the guest destroys):** only the host's own player sitting in its copy refuses a guest's destruction
  at once; D-Dog or a soldier in a seat, the partner's ride, an unknown guest position and the rate limit now wait
  (re-checked every 250 ms, refused after 120 s) instead of refusing for good [O D-Dog hold, `7762536a`; fixture for the
  rest]. "Close enough" is 300 m (was 60 m), judged on the closer of where the guest stood when the request arrived and
  now [fixture; live: near at 1.6 to 21.5 m and at 120 m (`0f3f00f1`, the wreck), far refused at 1197 m (`7762536a`)]. An unattributed break goes out as REQ_VEHICLE_GONE
  cause 4 (a new value of an existing field, protocol section 19): a host without `world_vehicle_break` refuses it
  (REJECT 11), so a key mismatch never applies anything unjudged [fixture].
- **The blast rule (new, `0e79337`):** the host's wreck of its copy (`SetBodyLife 0`) explodes about 8 s later with the
  engine's own blast, 3500 of 7800 life on a player beside it (`7762536a`). The host now removes its copy with a Despawn
  instead (no explosion on the host's PC, the guest keeps its wreck) while the host's player or the partner stands
  within 15 m of it [O, `f729d7f4` in play: the host 8 m away with godmode off kept 7801 of 7801 life and saw the jeep
  vanish]; with nobody that close the wreck is as before [O, `f729d7f4`: the host 27 m away saw the jeep burn, then the
  burnt wreck, life unchanged].
- **D11:** the guest's VEHICLES supply drop is refused in co-op too, whatever `world_relief` says, with the toast "Only
  the host can bring or request a vehicle in co-op." [O, `f729d7f4`, frames on both screens; the host's VEHICLES list
  opens].
- **Play defaults:** `world_loadout_block`, `world_vehicle_break` and `world_vehicle_credit` stay on in play (off in the
  lab unless named): the shipped behaviour of each has its FRAME on both copies and a passing `-Mode play -WanSim bad`
  rehearsal with no key named (`f729d7f4` frames, `33a7fa6e` every expectation met, `Forced=False`; D14 unchanged since
  `47b95ae7`).
- **Review findings:** all eight accepted and fixed (two of them by the user's picks), the two refuted frame claims
  corrected in design 8.11 and in the section below (the jeep is never in view on the host's screen in `70146170` /
  `47b95ae7`; the occluder is the host's own body). Table: design section 11.
- **Runs:** `7762536a` (lab, review scenario), `f729d7f4` (play `-WanSim bad`, shipped defaults; every expectation but one toast log line, rate limited by the HUD), `33a7fa6e` (the same, every expectation met), `0f3f00f1` (lab: a soldier-crewed jeep, a break 120 m away). Each
  closed with the baseline restored and real saves unchanged; archived under
  `%USERPROFILE%\mgs-coop-run-archive\decisions\<id>`.
- **Seen in passing (D7 track):** in `f729d7f4` the guest's own C4 near the jeep put the guest's copy of D-Dog into
  near death ("D-Dog Near Death (Heroism -60)" on the guest's screen, `obs; m=Dead; id=9728; att=local`).
- **Not done / open:** a soldier-crewed vehicle the guest blows up stays on the host's PC: its crew dies unattributed
  on the guest's copy, so the host holds the request while the crew sits and refuses it after 120 s (`0f3f00f1`, held
  95 s until the run ended; a question for the user, design section 10 D12); a real launcher or rifle kill at range
  (the 120 m case used the lab lever; the frames of it show only a slope); a multi-hit kill spread over more than about
  10 s [I]; D-Dog or a buddy near the host's copy when it is wrecked (the blast rule checks the two players); a two-PC
  session.
- **Checks:** tests written first for each rule (the rules check failed at check 3 before the blast rule; the rules,
  vehicle, relief-sync and wire checks before `8b00f80`); fast CTest 297/297 after each step, full CTest 312/312 on
  `290d987`.

## 2026-10-29: the vehicle decisions D11, D12, D14 built and proven (branch `wp/vehicle-decisions`, unreleased, not two-PC tested)

Parts of this section were changed by the review fixes in the section above (300 m instead of 60 m, waits instead of
refusals for a filled seat, cause 4, the VEHICLES block, the D12 blast rule).

The user's decisions of 2026-10-29 on vehicles (`docs/vehicles-companions-design.md` section 10; results in 8.11;
`docs/world-sync-protocol.md` section 19), on `wp/vehicle-decisions` from `dev/oct7-continue` `e2249e4` (not merged,
not pushed). New world chunk `lua/coop/world_vehicle_rules.lua` (after `world_vehicle_state.lua`), three new lab.ini
keys in the CFG r[15] bitmask (now 0..63), **on by default in play, off in the lab unless named**. No wire change.

- **D11 `world_loadout_block` (bit 32):** the guest's iDroid LOADOUT supply drop is inactive while it is paired in co-op
  (any mission world sync runs; across a link drop inside that mission until its final stop); selecting it shows the
  toast "Only the host can order a loadout drop in co-op: it can carry a vehicle." The host's iDroid and every other
  item are untouched [O, frames in lab and play]. Not tied to `world_relief`: a loadout delivery can bring a vehicle
  that only the guest's copy would have [O static SetRelief call, I effect]. `world_relief_sync.lua` keeps only the VEHICLES item; the lab lever `relief_lab loadout on|off` is gone.
- **D12 `world_vehicle_break` (bit 8, with VEHICLE_GONE):** a vehicle that breaks on the guest's copy without
  attribution (no Damage message exists for vehicle hits) is requested as a destruction; the host applies it only when
  the guest's player stood within 60 m of it (its DOWNED_STATUS position, at most 3 s old) or the host's copy is down to
  a quarter of its peak life, never while someone rides the host's copy (unless broken there too), at most 4 per 20 s;
  applied, the host's copy becomes the same wreck (SetBodyLife 0) [O, lab lever break and real C4 in play]. Refused:
  REJECT 8, the guest keeps a wreck the host lacks (logged `break_refused`; no command is known to undo a wreck)
  [O for `ridden`, `09333f05`; the other refusals fixture only].
- **D14 `world_vehicle_credit` (bit 16, with VEHICLE_GONE):** the receiver of a Fultoned world vehicle removes its copy
  with the stock `Seize {"Instant"}`, so its own engine credits the vehicle (OnFulton -> OnFultonVehicle ->
  AddTempResource): each co-op save gets it once; no GMP on the receiver [O, resource readback after the mission end];
  never the sortie vehicle (M3's rule stays) [fixture].
- **D13** (keep the M4 play defaults) and **D15** (accept the guest's bail snap-back; check it in the two-PC session):
  recorded, nothing changed.

| Run | Scenario, mode | Result |
|---|---|---|
| `cf0aeeba` | `vehicle-decisions-lab.txt`, lab, `africa-jeep` | Both D14 Fultons met every expectation; stopped at D12's `placebehind` after the guest's 900 m warp alone (fixed `bbdc687`) |
| **`c0203fdd`** | the same at `bbdc687` | **Every expectation met.** D14: each copy credited each jeep once (`AddTempResource 14` twice per copy), `4wdW` 1 -> 1 before the commit -> **3 on both** after the mission end, GMP -10000 on each copy for its own hand Fulton only. D12: the guest's lever break -> host `break_judge; ok=1; why=near; d_mm=1899` -> host wreck (`via=wreck; src=req; ms=66`), fire on both screens. D11: the guest's LOADOUT refused (NOTE 13 a 5), the host's list opened |
| **`70146170`** | `vehicle-decisions-play.txt`, **play, `-WanSim bad`**, the new keys named | **Every expectation met** (link 175-185 ms). D14 both directions (receiver Seize 133 / 116 ms); D12 with the guest's real C4 (judge `near; d_mm=13842`, host wreck 216 ms, the guest's request done in 233 ms); D11 with the toast drawn (frame) |
| `243ae269` | `vehicle-decisions-refuse.txt`, lab | Stopped at a `placebehind` behind the seated host (fixed `73313b0`) |
| **`09333f05`** | the same at `73313b0`, lab | **D12's refusal live, every expectation met**: the host seated in the jeep the guest broke -> `break_judge; ok=0; why=ridden` -> REJECT 8 -> guest `break_refused; here=wreck`; the guest's wreck left its pool 25 s later while the host's jeep stayed intact (frames `09333f05-*-refuse-*`) |
| **`47b95ae7`** | the same at `f4ec5c1`, **play, `-WanSim bad`, no vehicle key named** (the shipped defaults) | **Every expectation met, `Forced=False`**: the three keys on from the defaults on both copies; the same D14, D12 and D11 lines and frames as `70146170` |

Frames (looked at, JPEG in the session scratch `complete/frames/vehicles/`): `c0203fdd-*-d14-*` and `70146170-*-d14p-*`
(the jeep on both screens, the balloon on the Fultoning copy, gone on both), `c0203fdd-*-d12-*` (fire and smoke at the
jeep on both screens), `70146170-*-d12p-*` (the guest's blast and burning wreck; the smoke column at that spot on the
host's, the jeep itself never in view on the host's screen: the figure in front is the host's own body, corrected by
the review, see the section above), `70146170-guest-d11p-guest-refused` (LOADOUT inactive, the toast),
`*-host-d11*-host-loadout-open` (the host's LOADOUTS list).

Tests: `world_vehicle_rules_lua_check` (67 checks, worker gate), `world_vehicle_lua_check` (r[15] 0..63),
`world_relief_sync_lua_check`, `lab_config_tests` (failed first at the new fields, then at the play default),
`world_receipt_tests`, `lab_ini_check`, `lab_scripts_tests`, `worker_session_tests`. Fast CTest 297/297, full CTest
312/312 (`tools/build.ps1`, Debug) on the final code.

Open: D12's far, unknown and rate refusals live (fixtures; `ridden` ran), the host's wreck blast with live guards and godmode off, D14 for heavier
vehicles and an unrealized receiver copy, the guest's VEHICLES drop is still active with `world_relief` off (its block
is M3's; the user's D11 named LOADOUT), a two-PC session.

## October 29, 2026: launcher polish, the three launcher tracks combined (branch `wp/launcher-polish`, unreleased)

The user's five launcher requests of 2026-10-29, combined on `wp/launcher-polish`: `wp/launcher-setup` (`7286911`) plus the merges `0a9f556` (`wp/launcher-icon` `146b377`) and `c976484` (`wp/launcher-update` `c0fcb89`), then `700dba7`, `9641d16` and `d70caff`. Not merged into `dev/oct7-continue`, not pushed, not released.

**What changed for players.**
- The exe icon is the project logo (`docs/readme/logo.png`) at every size from 16 to 256 px. At 48 px and below it shows the banner without the words, because the lettering turns to grey noise at those sizes.
- Update now downloads the update, installs it and starts the new version. The old window closes only once the new one runs. If the new one does not start, the old version is put back.
- There is no Setup page. Co-op gets ready by itself on the first start and after an update. Play shows "Getting co-op ready...", or a card with Try again and Collect logs if it fails. Update now waits until co-op is ready ("You can update once co-op is ready.").
- Settings > Debug mode (off by default) adds Play > Local test (two windows).
- The player zip no longer contains `mgs-coop/source.zip`. `DDT-source-<version>.zip` goes on the same release page, and the updater removes the old `source.zip`.

**Merge decisions.** `start_update` checks the setup track's rules first, without a message: co-op getting ready, a game or a local test. Then it checks the update track's rules, which give a sentence: MGSV running, or a co-op session on. `--update-now` waits for the automatic setup; before this, it was refused once and never tried again. That fix failed first in `TestUpdateNowAfterSetup`. After an update the new window opens on Play with "Updated to version X." and the line "This takes about a minute after an update." This replaces the update section's "opens on Setup ... Set up co-op once more" below. While the setup runs, Update now is switched off and nothing fails. It no longer shows an error, as the update section below still says. A window closed while the local test is still starting keeps the folder until that start ends (`ui::HoldFolderWhileClosing`). The update track's model tests now let the automatic setup finish first; before that, 36 `ddt_core_tests` checks failed. The setup track's test feed now lists the source zip.

**Proven, with no game and no visible window.**
- Fast CTest 292/292 after each step. Full CTest 308/308 at `d70caff`. `ddt_core_tests` 1256/0 and `launcher_ui_tests` 369/0.
- Release worker checks passed at `d70caff` (receipt `20261029-182631-7e97f89baa3f43a589580d7cbc067db5`, 296 tests).
- `tools/package.ps1 -Friend` was built locally and nothing was uploaded. `DiamondDogsTogether-0.14.3.zip` is 3,018,096 bytes, SHA-256 `329983E56F5B3DE3610B403F52213F206DA0D7898CCA50F61A66145463FC3D07`. It holds the exe, the dll, package.json, README.txt, the licences and the notices, and no source. `DDT-source-0.14.3.zip` is 7,891,990 bytes, SHA-256 `03253FF30D7417A5593B1CC39670D388E828CB23338C806ACA9E29DD9EFC2508`, with 1342 files that match the tree, including `docs/readme/logo.png`.
- The output audit passed 34/34. package.json and the receipt match the zip, and `tools/check-release.ps1` accepts a simulated draft release that carries both files.
- All 37 hidden screenshots of the Release exe were looked at: no Setup page; Play preparing, failed and ready; Settings with Debug off and on; Play's Local test; the update scenes.
- Windows drew the Release exe's icon at all 10 sizes. The embedded `.ico` is byte-identical to `native/launcher/resources/ddt_launcher.ico`.

**Not proven, and a warning.**
- Windows Defender quarantined one Debug build of the merged launcher as `Trojan:Win32/Bearfoos.A!ml`, a machine-learning false positive. It happened at 13:12, when `ddt_local_watch_process` started a copy of the exe hidden from `%TEMP%`. The next builds were not flagged; they have different hashes. Neither were the two Release builds. A player's Defender could flag a release the same way. Nothing was excluded or restored.
- No real two-window test, real GitHub update or two-PC run was done. The taskbar, title bar and Alt-Tab icon were not seen.
- `git merge-tree` onto `dev/oct7-continue` (`e2249e4`) conflicts only in this file: both sides add a section at the top, and keeping both resolves it.

**What to try.**
1. A real update from 0.14.3. Bump the version to 0.14.4 in `CMakeLists.txt`, `tools/package.ps1` and the `tools/package-install.ps1` allow-list. Publish a release with both zips, after `tools/check-release.ps1`. Then press Update now in 0.14.3: Diamond Dogs Together should come back as 0.14.4 by itself, with "Updated to version 0.14.4." and "Getting co-op ready...", and `mgs-coop\source.zip` should be gone.
2. The real two-window test. Turn on Settings > Debug mode, choose `MGS_TPP_guest`, then Play > Local test > Start local test. This copies this version's loader, launcher and package.json into that folder; the old files go to `mgs-coop\install-backups`.
3. The taskbar icon. Start the new exe and look at the taskbar, the title bar and Alt-Tab. Explorer may show the old icon until its icon cache refreshes.

## October 29, 2026: the update restarts the launcher; the source is its own release asset (branch `wp/launcher-update`, unreleased)

Two of the user's 2026-10-29 launcher requests: "when we update we should restart the exe automatically instead of just closing it" and "I don't like the idea of including the source.zip with every install". Commits on `wp/launcher-update` (from `48ce7da`): `4a929ad`, `b7d6a18`, `c607d7a`, then the review fixes `9db44e5`, `58f38c1`, `8cd6568`, `8b35dae`, `8b232b1`, `912415d`, `3756e80`.

**Why the launcher closed after an update.** `RelaunchSelf` (0.14.0-0.14.3) started the new exe and called `ExitProcess(0)` at once from the update thread. The new exe reaches its single-instance claim (`Local\DiamondDogsTogether-ui-<folder hash>`) in about 30 ms, while the old process was still exiting with the mutex held: the new exe brought the old window forward and closed, then the old one exited too. Shown on a private desktop with the 48ce7da build: a second launcher started while the first holds the mutex exits within 28 ms. (The user's own `launcher.log` fits this but cannot prove it: the 0.14.3 window opened 12 s after the 0.14.2 one, which a manual restart also explains.)

**The restart now (`native/launcher/core/core_update.hpp` "Restart after an update").**
- The old launcher creates `Local\DiamondDogsTogether-update-<pid>`, starts the new exe with `--after-update <pid>` and waits (up to 20 s) until the new exe sets that event or exits. The restart is the last step of the install: if the new exe cannot start or exits at once, the whole folder goes back to the running version (the exe moves back from `.old`, every other file and the old `mgs-coop/source.zip` too), the old window stays open and says "The new version would not start, so your current version was kept..."; the Windows error goes to `launcher.log` (`update=restart_failed; ...; restored=yes`). Otherwise the window closes normally (`UpdateStatus::ready_to_restart`).
- The new exe sets the event, waits up to 15 s for the old process to exit (ends it if it hangs; only a process that created the event), then claims the single instance and deletes `DiamondDogsTogether.exe.old`. An update always needs the one-time setup again (the install record names the version), so the new window opens on Setup with "Updated to version X. Set up co-op once more for this version."; Play and Tools show "Updated to version X." afterwards. The automatic, hidden setup of the setup track has to show this note wherever the window opens.
- An exe started by an updater before 0.14.4 (no flag) waits up to 10 s for the old window's mutex instead of closing, and retries the `.old` delete for up to 3 s, but only when `.old` comes from an update of the last 5 minutes (the exe written or `.old` renamed then; `OldExecutableFromRecentUpdate`). A `.old` that stays for good (an antivirus, read-only) never slows a normal start. So the first update from 0.14.3, which still runs the 0.14.3 updater, also ends with the new window open.
- The install is all or nothing: every file is written as `<target>.ddt-new`, then each target is moved aside and the new file moved in; a move another program blocks for a moment (sharing violation, access denied) is retried for about 2 s; any failure moves everything back, and a rollback that cannot finish says so ("could not be installed completely. Extract the latest zip...") instead of claiming the old version was kept. `*.ddt-new` / `*.ddt-old` left by a process that was ended half way are tidied when the window opens; a folder left half new, half old is then refused by the existing package gate ("...come from different Diamond Dogs Together versions... Extract the whole zip into the game folder again"). If the process ends exactly between moving the running exe to `.old` and moving the new one in, no `DiamondDogsTogether.exe` is left to start; extracting the zip again is the way back. Only a zip with our `mgs-coop/package.json` (product `mgs-coop-local-package`, `core_install.hpp` `kPackageProduct`) is installed.
- An update and the setup never overlap: Update now is refused while the setup runs; Set up co-op, Try again and Uninstall are refused while an update downloads, installs or restarts; and the update thread checks again just before it installs (setup, MGSV running from the folder, a live co-op session). Updates are refused while MGSV runs from the folder or a session is on ("Quit MGSV first..."). Host and Join are refused while an update downloads or installs ("Diamond Dogs Together is updating. Wait until it has restarted, then try again."); they do not wait.
- When Update now on the Play banner is refused or fails, the banner stays with the reason in red and Update now again. Cancel shows "Stopping the download..." until the download has stopped, then returns to "available"; Update now cannot restart a download that is still stopping. Errors are one plain sentence; the detail (zip error, file name, Windows error) goes to `launcher.log`.
- A closing window releases the folder's mutex at once, unless an update is downloading, installing or restarting (then its thread ends first): an update check that waits for a slow network never makes the next start close silently.
- Developer flags for the end-to-end test only: `--update-feed <http://127.0.0.1:port/... or localhost>` and `--update-now` (needs `--update-feed`); the relaunched window keeps the test feed. `ddt_core_tests` never uses the network (hook).

**The source zip.** `tools/package.ps1 -Friend` writes the player zip `DiamondDogsTogether-<version>.zip` (no source inside) and, next to it, `DDT-source-<version>.zip` (the complete corresponding source plus `source-files.json`; the completeness checks of `tests/friend_package_tests.ps1` moved to it). GPLv3 section 6(d) lets the source be offered "in the same way through the same place": **every release uploads both zips** to the GitHub release `v<version>` (AGENTS.md "Releasing"). Launchers from 0.14.4 on offer a release as an update only when it holds `DiamondDogsTogether-<version>.zip` with exactly the tag's version and `DDT-source-<version>.zip`, so they never install a release without its source; 0.14.3 and older take any `DiamondDogsTogether-*.zip`, so the release must never carry the player zip alone. The read-only `tools/check-release.ps1 -Version <v> -PackageDir <dist folder>` (`gh release view`) fails unless the release carries both zips with the local sizes and SHA-256 and no second `DiamondDogsTogether-*.zip`; run it before publishing (package.ps1 prints the command). `package.json` records `source_archive` (name, SHA-256, file count). The player README, `THIRD_PARTY_NOTICES.md`, README.md and the About box (new "Source code" button: the release page of the running version) say where the source is. A player zip passed on to a partner no longer carries the source; it says where the source is (whether that is enough for such resharing is the user's decision). The 0.14.4 updater removes files the previous `package.json` listed under `mgs-coop/` that the new package no longer ships, only while their hash still matches what was shipped (that is how `mgs-coop/source.zip` goes when 0.14.4 itself updates a 0.14.3-era folder). The 0.14.3 updater leaves it behind, so the window also removes a leftover `mgs-coop/source.zip` at start, only when the installed `package.json` does not list it and the zip verifies as our source bundle (its own `source-files.json` lists exactly its entries with matching SHA-256). Nothing else is ever deleted.

**Checks.** Failing first, each against the code before its fix: 52 `ddt_core_tests` and 8 `launcher_ui_tests` checks (first pass), then 4 (busy/cancel), 1 (`.old`), 9 (setup/update overlap), 34 (rollback, retries, plain errors, leftovers), 14 (cancel race, recent `.old`, feed rule), 15 UI (Play banner, Setup note, mutex release), and the package audit stopping at "no source.zip" and at the missing `tools/check-release.ps1`. At `3756e80`: `ddt_core_tests` 904 checks, `launcher_ui_tests` 220, friend package tests 1235, fast CTest 290/290; full CTest (`pwsh -File tools/build.ps1`, Debug) 305/305 at `bd8b32e`, the commit before this line was added. `--screenshot-all` scenes 25-30: Setup with the updated note, Tools updated, Tools update refused (also rendered with the longest current sentence: it wraps clear of its button), Play updated, Play update failed, Tools "Stopping the download...". Local package build (fixture receipt, nothing uploaded): `DiamondDogsTogether-0.14.3.zip` 2,844,464 bytes and `DDT-source-0.14.3.zip` 7,562,921 bytes (1,337 files) side by side; the 0.14.3 player zip was 10,222,077 bytes.

**End to end, on a private never-displayed desktop** (CreateDesktop + `lpDesktop`, kill-on-close job; the input desktop stayed `Default`), in a fake game folder made from the real 0.14.3 release zip (with its `mgs-coop/source.zip`), with a dummy `mgsvtpp.exe` or, with `-Retail`, hard links to read-only copies of the retail `mgsvtpp.exe` and `steam_api64.dll` (so the folder reaches `needs_setup`; the setup is never started). The feed is on 127.0.0.1 (the source zip and a decoy listed before the player zip). The update zip is this branch's `package.ps1 -Friend` output around a launcher built as 0.14.4 (`CMAKE_PROJECT_INCLUDE` version override), with `package.json` version set to 0.14.4. All binaries were built from the clean tree at `3756e80` (`0.14.3+3756e80` and `0.14.4+3756e80`; later commits change docs only):
- handoff (2 runs, plus 3 with `-Retail`, all passed): the old build with `--update-feed ... --update-now` downloads, installs (`files=9`) and hands over (`update=handed_over; removed=mgs-coop/source.zip`); a new process (new pid, `0.14.4+3756e80`, `--after-update <old pid>`) has its window, the old pid is gone, `.old` and `mgs-coop/source.zip` are gone, and `launcher.log` has `update=finished; from_pid=<old>; old_exe=removed`. With `-Retail` the new window opened in `needs_setup` (the Setup page with the note). Two earlier runs reported FAILED only because the harness also expected the legacy-only `update=source_zip_removed`; the harness now checks each mode's own line.
- legacy, with the unmodified 0.14.3 release exe (`0.14.3+a4c18fa`, SHA-256 `3A2A81FB...21A9`) as the old window, started with no arguments: files swapped as the 0.14.3 updater does, the new exe started with no arguments (as `CreateProcessW` with a NULL command line does), and the old window ended at once (1-4 ms, 3 runs), after 600 ms (2 runs, so the new exe had to wait for the mutex) or by a normal close (it exited 35 ms later, 1 run): all 6 passed (2 of them with `-Retail`), with `.old` removed and `update=source_zip_removed`. Both launchers read the public GitHub release list in these runs (no local feed); nothing was offered. The real `ExitProcess` path of 0.14.3 cannot be triggered without its GitHub feed, so its timing is bracketed by these cases. The earlier legacy runs used a 0.14.4 build as the old window and started the new exe with `--update-feed`; they are superseded.
- broken (2 runs, passed): the zip's exe is not a program: `update=restart_failed; ...; restored=yes`, the old window stays open, no other launcher runs, the exe, `dinput8.dll`, `package.json`, `README.txt` and `source.zip` hash exactly as installed, and no `.ddt-*`, `.old` or `update.zip` is left.
- slowclose (1 run, passed): the window is closed while its update check waits 25 s for the feed; a launcher started at once opened its window in 132 ms while the first process was still waiting.
Evidence (process lists, command lines, windows, file hashes, `launcher.log`, HTTP request logs) is in the session scratch folder `launcher2/update/e2e-runs/`; the harness is `launcher2/update/e2e-update.ps1` with `HiddenDesktop.psm1`, `serve.py` and `make-update-package.ps1`.

Not done here: the next release must bump the version (CMakeLists.txt, `tools/package.ps1`, `tools/package-install.ps1` allow-list), upload both zips and run `tools/check-release.ps1`. No two-PC, visible-desktop or real-GitHub update was run, so whether the new window comes to the front on a real desktop is not checked.

## 2026-10-29: vehicles and companions M1-M4 integrated (`dev/oct7-continue`, unreleased, not two-PC tested)

Milestones M1-M4 of `docs/vehicles-companions-design.md` are merged into `dev/oct7-continue` with merge commits only:
`8843d20` (`wp/companions`, the design and roadmap), `23c2cac` (`wp/companion-host` `b4c1558`: M1, M2 and their
review fixes), `c5d0de0` (`wp/relief` `6c6c67f`: M3 and its review fixes) and `bf24931` (`wp/vehicle-state` `c0d1b89`:
M4 and its review fixes). Nothing was pushed or released. The per-branch sections below keep their own detail; this
section is the summary that the reviews and fix passes leave standing.

**Integration.**
- World ops: no number was shared, so none changed. COMPANION 0x30 (48), COMPANION_ACK 0x31 (49), VEHICLE_STATE 0x33
  (51), VEHICLE_SPAWN 0x34 (52), CHARACTER 0x38 (56), COMPANION_POSE 0x39 (57): 59 ops; 0x32 and 0x35-0x37 stay
  reserved for the design (`docs/world-sync-protocol.md` section 14). Native, Lua constants, receipts, tests and the
  protocol doc agree (new protocol section 18 for VEHICLE_SPAWN).
- CFG: r[15] the vehicle modules bitmask (1 VEHICLE_GONE, 2 VEHICLE_STATE, 4 the wreck parity), r[16] the companion
  module, r[17] the relief module (17 values).
- World chunks, in order: `world_vehicle`, `world_vehicle_state`, `world_companion`, `world_companion_dog`,
  `world_relief`, `world_relief_sync` (bootstrap, source header, `host_trial_patch_check`, `lua_checks`).
- Worker `logmark`: one implementation (`wp/vehicle-state` `4bf9613`, which reuses the log reader's bounds); the
  `wp/relief` fixture checks (`c95998a`) run against it.
- `debug_vehicle.lua` holds both lever sets; two comment blocks were condensed so its CRLF form passes the
  16000-byte check (15838 bytes now; little headroom). Close to the 32000-byte world-chunk limit (LF):
  `world_relief_sync.lua` 29946, `world_companion.lua` 29397. M5 should put Quiet's hold in its own chunk.
- Design section 8 renumbered: companion 8.1-8.3, relief 8.4-8.7 (were 8.2-8.5), M4 8.8 (was 8.1); references in
  the design, this file, the scenarios and `world_relief_sync.lua` updated.
- Checks on `bf24931`: `pwsh -File tools/build.ps1` 311/311 (Debug, `ctest -j`), `-Fast` 296/296; the contract checks
  `generate_partner_contract.py`, `generate_equip_writer_pins.py`, `generate_soldier_warp_contract.py`,
  `generate_nametag_contract.py` and `generate_downed_damage_contract.py` `--check` all pass. **No hidden run of the
  integrated head**: no op number changed, so the integration task called for none; that the four modules compose in
  game is [I] (fixture- and composed-chunk-tested only). On the docs commit after it, full CTest 311/311 again; one
  full run before that failed `save_handoff_io_tests` check 259 ("the exiting token keeps the timer armed") under
  parallel load, then passed 5 of 5 alone and in the next full run (no file it covers changed in these merges).

**Final defaults** (lab.ini keys; a named value always wins):

| Key | Module | Lab | Play | Why |
|---|---|---|---|---|
| `companion_host_owned` | M1 + M2 (CFG r[16]) | off | off | E19 fails for the dog (D7 open); the key also covers Quiet (E15 not run); no play rehearsal of the review fixes |
| `world_relief` | M3 (CFG r[17]) | off | off | No play rehearsal of a drive, a mission end or a removal is possible in the worker (play refuses the levers); D11 open |
| `world_vehicle_gone` | M4 VEHICLE_GONE (r[15] bit 1) | off | **on** at 30010 / 30020 | FRAME on both copies and passing `-Mode play -WanSim bad` rehearsals with no key named (`e44516d7`, `b31a1b65`); Mother Base lab only |
| `world_vehicle_state` | M4 VEHICLE_STATE (r[15] bit 2) | off | **on** at 30010 / 30020 | FRAME on both copies (`3585869d` lab; `b31a1b65` play after-frame) and play rehearsals `4bbbfde4`, `b31a1b65` (`-WanSim bad`, no key named); Mother Base lab only |
| `world_vehicle_wreck` | M4 wreck parity (r[15] bit 4) | off | off | No play rehearsal of a destruction with it; the receiver's blast was never measured with live guards |

**M1, the host owns the companion** (lab key `companion_host_owned`, play default off).
- Proven [O] (logs on both copies, frames): the host's buddy loaded on the guest's copy with its look, the guest's
  call menu off and the refusal toasts (`489b3c6f`, `fa397547`; play rehearsal of the pre-fix build `b83c9f7a`); the
  Mother Base cell Quiet hidden (`b993af09`, `de43726a`); after the review fixes: no Quiet ever loaded on the host
  under the lock, the host's dog kept through a checkpoint Continue and a trip to 30020 (`74d5ba7e`, `9165baeb`), a
  host horse never shown on the guest's copy (frames on both, `9165baeb`, `b651b42c`), key agreement `step=peer;
  on=1` on both copies at pairing.
- Unproven: the stage hook's own effect in game [I]; the session end, the deploy wait for CHARACTER and the reconcile
  backoff in game (fixtures); the iDroid buddy request and dismiss refusals (a dismiss is NONE, not covered by the
  disable byte); the host's sortie-prep and iDroid buddy lists; an E20 control frame; whether a walker pilot sets
  `vars.playerVehicleGameObjectId` [U]; seeds `mb-quiet-cell` and `mb-walker` not boot-checked; a play rehearsal of
  the fixes.

**M2, D-Dog synced and following the host** (same key, play default off).
- Proven [O]: the guest's copy shows one dog, the host's, held at the host dog's pose and never released to the
  guest's AI (frames on both screens: `ffb2d7ab`, `d937191f` with the older hold, `994d252f` at Mother Base, and
  `b651b42c` with the shipped hold); E3 passes (84-176 s within 1.4-1.5 m of a guard in combat, no engagement:
  `4cd88ea2`, `d937191f`, `dc761c98`); the dog stays with the host with the guest 40 m away (2-23 cm) and with the
  host in the helicopter (`994d252f`, after `allactive_set`); D8 (b) keeps the guest's own dead/flag vars.
- Corrected by the review: not every window stays in band (`994d252f` had run windows of 626 and 703 cm and a walk
  window of 768 cm, cause [U]; walk windows of 3.11 m in `d937191f` and 3.21 m in `033193d3`); "the rest of the
  mission" after a guest-side death is [I].
- **E19 fails** [O]: guards on the guest's PC kill the guest's copy (`dc761c98`); `SetDisableDamage` does not
  protect it; the lab repair reloaded it once (`033193d3`, logs only).
- Unproven: the dog's takedown commit/apply (the call menu's "Get it" took nobody down twice); the walk/run/dash
  legs on the shipped hold; the Mother Base run leg; a play rehearsal.

**M3, the sortie vehicle on both copies** (lab key `world_relief`, default off since `4d18561`).
- Proven [O] (lab, logs and frames on both copies): relief types 1, 2 (`d18e8fa0`), 5, 9 (`1a5c1fa0`), 3
  (`5c35644d`) and the FOVA_1 paint (`1914e76c`) drawn at the same spot; the host seated in it on the guest's screen
  (`d18e8fa0`); the guest's VEHICLES request refused with the toast (`d18e8fa0`, `5c35644d`; in play `40c09484`,
  `98e4af59`); one mission-end credit, the host's (`5c35644d`, phase-scoped waitlogs); the host's Fulton removing it on
  both copies (VEHICLE_GONE on `d18e8fa0`; off, through `host_none` 15 s later, `5c35644d`); the guest's Fulton
  credited once, on the host (`5c35644d` VEHICLE_GONE off, `cbc30b91` on); continue (`1914e76c`, `40c09484`) and late
  join (`d3b23cca`); the arrival in play with `-WanSim bad` (`40c09484`, `98e4af59`).
- Refuted earlier and not counted: the `d18e8fa0` / `1a5c1fa0` expectations for the mission-end skip and the Fulton
  removals (they matched earlier phases' lines; `5c35644d` proved them again with logmarks).
- Unproven: a destruction of the relief vehicle [I]; types 4, 6, 7, 8 and 10; REJECT 8 while the host rides it, the
  reload after a refusal or timeout, the guard across a real link drop and the LOADOUT lever (fixtures); D8 (b) save
  content read from disk [U]; the heavy, continue and late scenarios re-run with logmarks; any play rehearsal of a
  drive, a mission end or a removal. Since the integration VEHICLE_GONE is on in play, so a play session that names
  `world_relief=1` takes the VEHICLE_GONE-on removal path (`cbc30b91`, lab) [I in play].

**M4, vehicle losses, damage and parked vehicles** (VEHICLE_GONE and VEHICLE_STATE on in play at 30010 / 30020).
- Proven [O]: each player's own Fulton by hand in play over `-WanSim bad` (`e44516d7`, frames on both); the removal
  held while the guest sits in the jeep (`92720b75`); a host C4 destruction becomes a disappearance on the guest's
  copy in play (`4bbbfde4`, `b31a1b65`, frames `play-c4-placed` to `play-destroyed-after` on both); VEHICLE_STATE's
  first pose applied in play after the host's bail (`b31a1b65`, `moved=1; d_mm=7970`, frame `play-bail-both`); body
  life host-authoritative both ways (`4bbbfde4`: the guest's own hits put back from 1602 to 2270); the push lever and
  the near-player hold framed on both screens (`3585869d`, lab); the wreck with its key named (`0edadbe2`,
  `3585869d`, lab lever). The integration pass looked at the `b31a1b65` pair frames `play-bail-both` and
  `play-destroyed-after`: the jeep at the same building on both screens, then the burning wreck on the host's screen
  and no jeep on the guest's.
- Unproven: the wreck parity in play and its blast with live guards; Mother Base (no 30050 run); a physical shove;
  the guest bailing out of a rolling jeep [I]; `player_near` and the seated-rider hold in play (lab only); change to
  apply latency against R7's 2 s (tx to apply 16..334 ms only); a far, unrealized receiver's wreck fallback; Fulton
  rewards against save resources; a jeep the guest destroys is not requested (no vehicle `Damage` message,
  `35ba7929`; the split shown in `72366eb5`).

**Open user decisions** (`docs/vehicles-companions-design.md` section 10): D7 for the dog (E19 fails; hide, show and
accept, repair, keep in the lab, or a native damage guard); D11 the guest's LOADOUT drop (block, or allow a loadout
without a vehicle); D13 the M4 play defaults (confirm on in Afghanistan and Africa, or off until a two-PC session);
D12 a vehicle the guest destroys (native hook, heuristic, or forward every unattributed break); D14 Fulton rewards for
world vehicles; D15 the guest's bail. To confirm: with the companion key on, the guest's PC shows no host D-Horse,
D-Walker or Quiet until M10, M9 and M5; a host who picks "none" while their Quiet is parked is saved as Quiet.

**Needs the two-PC Steam session** (M1-M3 count as done only after one; M4 has never run on two PCs):
- At the defaults (no key named): either player's Fulton of a world jeep, a host destruction (C4) and a drive-and-leave
  in Afghanistan or Africa, on both screens; the host's bail out of a rolling jeep.
- With `companion_host_owned=1` on both PCs: the host's dog on both screens, the guest's call, iDroid request and
  dismiss refused, the host's sortie-prep buddy list with a guest who plays Quiet, the dog's "Get it" takedown with
  real controls, and what the guest sees when their guards kill the dog (D7).
- With `world_relief=1` on both PCs: the host's sortie-prep vehicle on both screens, a drive, a mission end (one
  credit), a Fulton by each player, and the guest's LOADOUT drop (D11).

## October 29, 2026: M4 review fixes (branch `wp/vehicle-state`, unreleased; hidden-run proven in lab and play, not two-PC tested)

An independent review of the M4 branch (`243e1a8`) returned **fix_first**: four major findings (the index-0 refusal ran after the new wreck path; the wreck parity reached play with no play rehearsal of a destruction; the play-default flip covered Mother Base with no run there and was rehearsed only with the keys named; the guest applied body life downwards only) and six minor ones. Each was checked against the code and the run logs before it was fixed; all ten held. Design: `docs/vehicles-companions-design.md` section 8.8 ("M4 review") and section 9 M4; wire and Lua contract: `docs/world-sync-protocol.md` sections 16-17.

**What changed** (`c423b53`, `4bf9613`, `0dfac60`, `c98ad98`, `57fc2f7`, `7d82def`, `5b7522a`)
- **Index 0 first.** `V.remove` refuses index 0 (25600, the relief vehicle, M3 / E17) before any removal form. `2a3d97d` returned `wreck` before the refusal, so a destroyed 25600 was wrecked on the receiver on the rx and the req path (the reviewer's fixture probe: `step=applied; id=25600; via=wreck`). Now `refused; reason=index0` (REJECT 8 on the host).
- **Wreck parity behind its own key.** `world_vehicle_wreck=0|1` (CFG r[15] bit 4 through `world::VehicleModules`; LabCommon, the worker's `labsetting`), **off in lab and play unless named**. It used to ride on `world_vehicle_state`, which `482eb74` turned on in play, so play replayed every host-side destruction, blast included, on the guest's copy. In play a destroyed vehicle is now a Despawn on the receiver, as in 0.13.6. `vehicle_gone step=on` names it (`wreck=0|1`). A wreck that does not take (alive 2 s after `SetBodyLife 0`) falls back to the Despawn (`step=fallback`); with no Despawn form the id is not marked lost, so a re-sent announcement retries it.
- **Mother Base lab only.** Both modules run at 30010 and 30020 in play and at 30050 only in the lab (`step=off; mission=30050; reason=mb_lab_only` in play). No hidden run has covered 30050 (the cluster jeeps matched by name; the index Despawn of `coop-vehicle-gone-mb.txt` has never run). A named VEHICLE_STATE match must now lie within 40 m of the host's pose (`refused; reason=far`), like an unnamed one.
- **Body life host-authoritative both ways.** The guest writes the host's life whenever its copy's differs, so guest-only hits (its own fire, its own guards) are put back at the host's next state (the 10 s refresh at the latest) instead of adding up to a guest-only destruction that nothing repairs. A burst that destroys the guest's copy within those 10 s still stays on the guest's copy. A host life of 0 is written as 1: a destruction is VEHICLE_GONE's.
- **Holds and lines.** A needed move is held (`player_near`) while the local player is within 3.5 m of the target or of the vehicle's current spot (never pulled away from a player walking up to it). A failed placement is `refused; reason=place_failed` instead of `same`. A hold resets the logged outcome, and every host change (`f=1`) is answered by a line, so the line after an event can be waited for.
- **Worker `logmark <host|guest|both>`.** Later `waitlog` steps of that role read only lines completed after the mark (never before the session offset).
- **Scenarios.** `vehicle-state.txt` names `world_vehicle_wreck=1`, marks the logs before each event, adds the near-player hold and a guest-only life drop, and (`57fc2f7`) frames the push with both cameras on the jeep. `vehicle-state-play.txt` names no vehicle key (the shipped default) and adds the guest's own shots and a two-charge C4 destruction. New `vehicle-state-play-host.txt` (play, no key named): the host dives out of a rolling jeep (a real pose divergence in play, where no run had moved a vehicle) and a C4 destruction with the guest's camera on the jeep.
- **Docs.** README "What works" / "Known limits" (Afghanistan and Africa only; a destruction on the host's PC is a disappearance on the guest's; the guest's damage is undone), the roadmap (pushed vehicles back to `[~]`), the design (8.8, 9 M4) and the protocol (16, 17).

**Tests** (written first; each failed before its fix): `world_vehicle_lua_check` at the wreck-key check (index 0 on the rx and the req path with the wreck key on, the key, the fallback, Mother Base in play, r[15] bit 4), `world_vehicle_state_lua_check` at check 34 (the guest-only hit put back) and check 63 (a line per host change), plus the far match, the current-spot hold, place_failed and Mother Base in play; `lab_config_tests` and `world_receipt_tests` did not compile (`world_vehicle_wreck`, `VehicleModules`); `worker_session_tests` failed against the old runner (`-LabSetting admits world_vehicle_wreck=1`); the old LabCommon refused the key (`lab_scripts_tests`).

**Claims corrected** (the reviewer's refutations, each confirmed in the logs)
- `d9b48fcb` showed VEHICLE_STATE's **non-interference** with a drive and leave (`skip; partner_ride` -> `resume` -> `tx; moved` -> guest `hold; occupied` -> `same`), not a pose it applied: the guest's stats read `applied=1; moved=0` (the one apply was life). The shared left pose is RIDE_FAST's last placement. The first pose VEHICLE_STATE applied in play is `b31a1b65` (below).
- "Every expectation met" in `d9b48fcb`, `e235bda6` and `0edadbe2` was log-reviewed, not expectation-proven, for the post-drive lines: the guest's `(same|applied)` expectation matched the pre-drive sweep line (`t_ms=160308032` / `151330449`), and the post-drive host `tx; why=moved` of `0edadbe2` matched the earlier push tx (`seq=16`).
- A host-side destruction becoming a wreck on the guest's copy was shown only with the lab lever (`0edadbe2`, again `3585869d`); the play attempt (`e235bda6`) never destroyed the jeep. The M4 section's "The project rule is met for both" did not hold for the wreck path or Mother Base; it now holds for what ships in play (below).
- The roadmap's `[x]` for pushed vehicles had no host-screen frame of a push and only a lab-lever push: now `[~]` (`3585869d` adds the host-screen frame, still with the lever).

**Checks**
- `pwsh -File tools/build.ps1` (Debug, `ctest -j`): 306/306 passed, 106 s, and `pwsh -File tools/build.ps1 -Fast`: 291/291, on the tree of this section's commit (code as `c98ad98`; later commits are scenarios and docs).
- Each hidden run passed the worker gate (`Get-WorkerTests`, including `world_vehicle_state_lua_check` and `world_vehicle_lua_check`).

**Hidden runs** (copied saves, research build, native gun stage; each Closed, BaselineRestored and RealSavesUnchanged, no `step=error`, `viewer_changed` or fault line on either copy (the `crash_probe=av` at shutdown teardown is the known one, in every run); run folders in `%USERPROFILE%\mgs-coop-run-archive\companions\`, frames in the session scratch `complete/frames/vehicle-state/` with the prefix `rv-`)
- `72366eb5` (`vehicle-state.txt`, lab, `africa-jeep`, build `0dfac60`; 8 expectation failures, all after the drive). Both copies `vehicle_gone step=on; ...; wreck=1`. Passed: the push (`tx; d_mm=3000` -> guest `applied; moved=1; err_mm=0` 16 ms later), the guest push put back by the refresh (`applied; ...; f=5`), and **the near-player hold**: with the guest at the jeep's door the host's 2 m push gave guest `hold; id=25608; here=25608; reason=player_near` 32 ms after the host's tx and `applied; moved=1; d_mm=2000` once the guest was moved clear, 2.2 s later. That push had left the jeep against the watchtower: on the guest's drive its copy broke (`obs; m=VehicleBroken; att=none` on the guest's copy only; frame `m4-stopped`: the guest on foot, no jeep) while the host's stayed intact, the known split for a guest-side destruction (no request). Every later part found no jeep on the guest's copy. The scenario now pushes back towards the boot spot (`57fc2f7`).
- `4bbbfde4` (`vehicle-state-play.txt`, **play, `-WanSim bad`, no vehicle key named**, lab seed, build `c98ad98`; **every expectation met**, each post-event one after a `logmark`; shutdown `Forced=True` as in `e235bda6` and `e44516d7`). Both copies `vehicle_state step=on` and `vehicle_gone step=on; ...; wreck=0`; sweep `sent=13`. Drive and leave: `skip; partner_ride` -> `resume` -> `tx; why=moved; d_mm=13969` -> guest `hold; occupied` (D-Dog) -> `same; d_mm=0; f=1`; readbacks 9 mm apart; frame `play-left-both`. The host's shots: `tx; why=life; life=2270` -> guest `applied; life=2270; was=2300` 151 ms later. **The guest's own shots** took its copy alone to 1602; the host's refresh put it back (`applied; moved=0; life=2270; was=1602; f=5`). **A real destruction in play**: the host's C4 -> `watch; m=VehicleBroken; cause=2; att=none` -> `lost` -> `tx; cause=2` -> guest `applied; id=25608; via=locator; src=rx; ok=1` 132 ms after the tx; readbacks host `alive=0; life=0` (the wreck) and guest `alive=0; pos=0,0,0` (no instance); the guest's camera had turned away from the spot.
- `3585869d` (`vehicle-state.txt`, lab, `africa-jeep`, build `57fc2f7`; **every expectation met** (36), `Forced=False`). **The push framed on both screens** (`m4-before-push` -> `m4-pushed`: the jeep 3 m along on the host's screen and on the guest's; `tx; d_mm=3000` -> guest `applied; moved=1; err_mm=0` 17 ms later; readbacks 2 mm apart); the guest's own push (`m4-guest-pushed`, the guest's screen only) put back by the refresh 7.8 s later (`f=5`, `m4-corrected`); the near-player hold (`hold; player_near` 32 ms after the tx, `m4-near-held`: moved on the host's screen, still beside the guest on the guest's; `applied; moved=1; d_mm=2000` 2.0 s later once the guest stood clear, `m4-near-applied` on both); drive and leave (`skip; partner_ride` -> `resume` -> `tx; why=moved; d_mm=25750` -> guest `hold; occupied` -> `same; d_mm=1; f=1`, readbacks 1 mm apart); body life (`life=2270; was=2300` 33 ms after the tx; the guest-only lever drop to 1500 put back 8.5 s later, `was=1500; f=5`; the host's lever 200 -> guest `life=200`); the wreck with the key named (the host's lever life 0 -> `watch; m=VehicleBroken; cause=2` -> guest `applied; via=wreck; src=rx; ok=1` 149 ms after the tx; `m4-wreck-01`/`-03`: fire at the jeep on both screens, godmode on).
- `e20f76bc` (`vehicle-state-play-host.txt`, play, build `7d82def`): stopped at its first `placebehind` (`viewer landing starts without a valid partner pose`: only the host had made the 800 m warp to the jeep). Both players now go to the jeep first (`5b7522a`).
- `b31a1b65` (`vehicle-state-play-host.txt`, **play, `-WanSim bad`, no vehicle key named**, lab seed, build `5b7522a`; **every expectation met**, `Forced=False`). **The host drives and dives out of the rolling jeep:** host `skip; reason=local_rider` (the first in any host log) -> `skip; reason=occupied` (D-Dog stayed in) -> `resume` 3.9 s after the host's get-off -> `tx; why=moved; d_mm=18590` -> guest `applied; id=25608; from=25608; moved=1; d_mm=7970; err_mm=0; dyaw=258` 334 ms later: the guest's copy had stopped where RIDE_FAST last put it, 8 m short of where the host's rolled to, and VEHICLE_STATE moved it there. **The first pose VEHICLE_STATE applied in play.** Readbacks 46 mm apart afterwards; frame `play-bail-both` (the jeep at the same spot by the same building on both screens; the guest's 18 m spot faced a wall during the bail, so the divergence itself is in the logs only). **The C4 destruction framed on both screens:** `play-c4-placed` (the jeep behind sandbags on the guest's screen) -> host `watch; m=VehicleBroken; cause=2` -> `tx` -> guest `applied; via=locator; src=rx; ok=1` 151 ms after the tx -> `play-destroyed` / `play-destroyed-after` (the burning, overturned wreck on the host's screen; nothing at that spot on the guest's, no explosion there); readbacks host `alive=0; life=0`, guest `alive=0; pos=0,0,0`.

**Play defaults.** The project rule (a FRAME on both copies and a passing `-Mode play -WanSim bad` rehearsal) now holds for every path that acts in play at 30010 and 30020 with the keys unnamed: VEHICLE_GONE's Fultons (`e44516d7`, frames on both) and its destruction as a Despawn (`b31a1b65`, frames on both; `4bbbfde4`); VEHICLE_STATE's pose (frames on both `3585869d`, lab lever; play `b31a1b65`, `moved=1`, the after-frame on both) and body life both ways (frames `3585869d`; play `4bbbfde4`, `e235bda6`, `d9b48fcb`). Its holds acted in play as `occupied`, `local_rider` and `partner_ride`; `player_near` and VEHICLE_GONE's seated-rider hold are lab-proven only (`3585869d`, `92720b75`). The wreck parity and Mother Base are not in the play default.

**Still open**
- A two-PC session over Steam.
- The wreck parity in play: a play rehearsal of a real destruction with `world_vehicle_wreck=1`, and a lab run with live guards and godmode off that shows no extra guest-side kills or chain wrecks from the receiver's blast. Until then it stays a lab key. A wreck that does not take on a far, unrealized receiver copy (the fallback) has not run.
- Mother Base: a 30050 hidden run (the cluster jeeps by name, the index Despawn) before either module runs there in play.
- A jeep the guest destroys is not requested (no vehicle `Damage` message, `35ba7929`; `72366eb5` shows the split). The guest's damage short of destruction never reaches the host; it is now undone on the guest's copy instead.
- The guest diving out of a rolling jeep [I]: its own copy rolls on while the host's stops where RIDE_FAST last put it, so VEHICLE_STATE would move the guest's jeep back to that spot once the guest stands 3.5 m clear of it. Not run.
- A physical shove (a player or another vehicle) was never tested; every push so far is the lab lever. The host's bail (`b31a1b65`) is the only real divergence corrected in play.
- Change -> apply latency against R7's 2 s: only tx -> apply is measured (16..334 ms); the host's scan adds up to about 1.6 s, and a seated D-Dog holds the host's send while it stays in (`b31a1b65`: 4.2 s from the host's get-off to the guest's apply).
- A SetBodyLife raise on a copy that burns ([U]); Fulton rewards against save resources; why a teleport beside a jeep hides the Fulton prompt; `SelectVehicleLifeReceipt` has no unit test of its own.
- AI-driven, LRRP and reinforcement vehicles (M8); the sortie vehicle, index 0 (M3, E17).

## October 29, 2026: M4 vehicle losses, damage and parked vehicles (branch `wp/vehicle-state`, unreleased; hidden-run proven in lab and play, not two-PC tested)

Milestone M4 of `docs/vehicles-companions-design.md` (section 6.4 F4, section 9 M4; experiment E5 in section 8.8). Branch `wp/vehicle-state` from `wp/companions` `068852c`. Wire and Lua contract: `docs/world-sync-protocol.md` sections 16 (VEHICLE_GONE) and 17 (VEHICLE_STATE, new).

**What changed**
- **VEHICLE_STATE, world op 51 = 0x33 (host to guest, reliable, world queue, level 3; `b6dda1a`).** `lua/coop/world_vehicle_state.lua` (spliced after `world_vehicle.lua`). The host scans its TppVehicle2 pool, 4 ids every 250 ms (plus at once any id a `Damage` message names; `35ba7929` showed the game posts none for a vehicle, so a change arrives through the scan within about 1.6 s), and sends a live vehicle's pose (mm, mrad), body life, state word and zero-parts mask when it moved more than 0.5 m or 5 degrees or its life, state or parts changed (at most every 500 ms per vehicle), all of them once after each PAIRED (the sweep) and each again 10 s after its last send. Skipped: the host's own vehicle (`local_rider`), an occupied rider seat (`occupied`, an NPC), a vehicle the partner drives (`partner_ride`, the RIDE_FAST track latched 10 s, because the guest's seat chain never writes `rider[]`), the authored LRRP trucks (`lrrp`: `veh_trc_0000..0005` in 30010, `..0006` in 30020) and a vehicle VEHICLE_GONE lost or watches. The guest applies a state only to the vehicle the locator hash names (or, unnamed, the same id when it is alive, of that type and the nearest of its type within 40 m; never index 0), and only while nobody rides it on its copy and the local player is more than 3.5 m from where it would go (otherwise held up to 10 s): `SetPosition{position, rotY}` read back, and `SetBodyLife{life}` when the host's life is lower.
- **Native read `MGS_COOP_VEHICLE_LIFE_V1`** (read-only; `partner_vehicle::ReadLife`, `game_script_observer.cpp`). The TppVehicle2 instance dispatcher `0xB2B710` has no life getter (21 hashes read with capstone, design 3.6); the receipt reads the life block `*(object+0x150) + index*0x18` (12 u16, word 0 the body life `SetBodyLife` writes) behind the pinned vt+0x98 `0xB2DD00`, and the `ene_veh_state` word that `StoreToSVars` would save.
- **Wreck parity (`2a3d97d`).** With VEHICLE_STATE on, a destroyed vehicle (VEHICLE_GONE cause 2) becomes a wreck on the receiver through `SetBodyLife{life=0}` (the engine's own destruction, E5) instead of a Despawn; a Fulton stays a Despawn. **Since the M4 review (`c423b53`) it has its own key `world_vehicle_wreck`, off in lab and play unless named**, so in play a destroyed vehicle is a Despawn on the receiver.
- **Lab keys** `world_vehicle_gone` / `world_vehicle_state` (`lab.ini`, LabCommon, the worker's `labsetting`); CFG r[15] is now the vehicle modules bitmask (bit 1 VEHICLE_GONE, bit 2 VEHICLE_STATE; bit 4 `world_vehicle_wreck` since the M4 review). **Lab levers** (refused natively outside the one-PC lab and in Lua outside mode=lab): `debug vehicle_push <id|nearest> <dx> <dz>`, `debug vehicle_life <id|nearest> <life>`; `debug vehicle_state_status [id]` is read-only.
- **Play defaults (`482eb74`).** VEHICLE_GONE and VEHICLE_STATE are **on in play** unless named, and off in the lab unless named (scenarios opt in with `labsetting`); new `LabConfig::world_vehicle_gone_given` / `world_vehicle_state_given`, a named 0 keeps either off in play. The project rule was stated as met for both (FRAMEs on both copies and passing `-Mode play -WanSim bad` rehearsals, `e44516d7`, `d9b48fcb`); the M4 review found it did not hold for the wreck path (no play destruction) or Mother Base (no run), and the rehearsals had named the keys. See "M4 review fixes" above: the wreck parity got its own lab key, Mother Base is lab only, and `4bbbfde4` rehearsed the unnamed default. README "What works" / "Known limits" updated.
- **Scenarios:** `vehicle-state.txt` (lab), `vehicle-state-play.txt` (play), `coop-vehicle-gone.txt` (C first, a new part E), `vehicle-gone-player.txt` (lab), `vehicle-gone-play.txt` (play).
- **Tests** (written to fail before the branch, as `world_vehicle_state_lua_check` records in its header; the play-default checks failed before `482eb74`): `world_vehicle_state_lua_check` (in the worker gate), `world_vehicle_lua_check` (wreck parity), `world_wire_tests` (VehicleState), `world_receipt_tests` (r[15] bitmask), `partner_vehicle_tests` (TestLife), `lab_config_tests`, `lab_ini_check`, `debug_vehicle_lua_check`, `lab_scripts_tests`, `worker_session_tests`.

**Checks**
- `pwsh -File tools/build.ps1` (Debug, `ctest -j`): 306/306 passed, 96 s, on the tree of this section's commit (code as `02d1e95`). `pwsh -File tools/build.ps1 -Fast`: 291/291. `lab_config_tests` failed (check 1177) before the play-default change `482eb74`.
- Each hidden run passed the worker gate (`Get-WorkerTests`, including `world_vehicle_state_lua_check`).

**Hidden runs** (copied saves, research build, native gun stage, low graphics; each Closed, BaselineRestored and RealSavesUnchanged, no `step=error`, `viewer_changed` or fault on either copy; run folders in `%USERPROFILE%\mgs-coop-run-archive\companions\`, frames in the session scratch `complete/frames/vehicle-state/`):
- `92720b75` (`coop-vehicle-gone.txt`, lab, Wakh Sind, build `3b5115d`). **C passed**: the guest sat in J1 = 25608 while the host lost it (`step=tx; cause=3`); the guest held (`hold; reason=local_rider`, then `occupied` while D-Dog stayed seated 2.5 s) and applied after its get-off (`applied; via=locator; src=rx; ok=1`); frames `hold-guest-seated` (the guest seated in a jeep the host's copy no longer has) and `hold-applied` (gone on both). A (25615) and D (the engine's Fulton of 25617: `watch; m=Seize` -> `lost` 3.2 s later -> host `applied; src=req` -> `req_done`) passed again. B did not reproduce (the host did not board J3; B passed in `075c1ea4`). Part E (the guest's own Fulton beside a jeep under a roof) showed no Fulton prompt and did nothing. Shutdown `Forced=True`.
- `d407f115` (`vehicle-state.txt`, lab, `africa-jeep`, build `3b5115d`). Push lever: host `vehicle_push 25608 3 0` -> `tx; why=moved; d_mm=3000` -> guest `applied; moved=1; d_mm=3000; err_mm=0` 47 ms later; a guest-side push was put back by the host's 10 s refresh (`applied; ...; f=5`); frames `m4-before-push`, `m4-pushed`, `m4-guest-pushed`, `m4-corrected` on the guest's screen (the host's camera did not show the jeep). Drive and leave: `skip; reason=partner_ride` while the guest drove, `resume`, `tx; why=moved; d_mm=2190`, the guest held (`occupied`, D-Dog) and then matched within 1 mm; frames `m4-damaged` show the left jeep at the same spot on both screens. Damage: `tx; why=life` -> `applied; ...; life=2230; was=2255`, and the lever's life 200 -> `life=200` on the guest. E5: `SetBodyLife{life=0}` is the engine's own destruction (VehicleBroken, explosion and fire; frames `m4-guest-life0-*`, `m4-host-life0-*`); life 200 shows no fire on either copy.
- `0edadbe2` (`vehicle-state.txt`, lab, build `2a3d97d`, wreck parity on). The same push, correction, drive, damage and low-life lines; the host's `vehicle_life 25608 0` -> `watch; m=VehicleBroken; cause=2` -> `tx` -> guest `applied; id=25608; via=wreck; src=rx; ok=1; ms=116`; frames `m4-wreck-01..04`: explosion and fire at the same jeep on both screens. The guest's own part did not start: its two shots from a goto spot missed (life stayed 2300), so its lever break of 25610 had no local Damage and was not requested (by design); that wreck stayed on the guest's copy (the host's refresh was refused `absent`).
- `a50214f7` (`vehicle-gone-player.txt`, lab): stopped at `placebehind` right after the guest's 990 m warp (`viewer landing starts without a valid partner pose`); the scenario then teleported both copies (`2464f4e`).
- `e235bda6` (`vehicle-state-play.txt`, **play, `-WanSim bad`**, lab seed, build `d6f54f1`). Every VEHICLE_STATE line passed (the post-drive guest line only log-reviewed: its expectation matched the sweep's `same` line, M4 review): sweep `sent=13`, drive and leave (`skip` -> `resume` -> `tx; why=moved; d_mm=14235`), ten life changes applied on the guest 115..301 ms later (median 127 ms). Its last part, the host's C4, never destroyed the jeep (life 2300 -> 592, no VehicleBroken): three expectation failures in that part only. Shutdown `Forced=True`.
- `46b8fc12` (`vehicle-gone-player.txt`, lab, build `2464f4e`). The guest's teleport to the door-spot coordinates of veh_lv_0002 (25610) showed only "E: get in" (frame `pf-before`), so its E hold boarded the jeep (`partner_status=local; names=on_vehicle; vehicle=25610`; the host logged `vehicle_state step=skip; reason=partner_ride` at once); no FultonInfo, so no `watch`. The seated guest's next `teleport` and `goto_vehicle` answered `warp_readback_timeout` and the host's `placebehind` stopped the run (`viewer landing lost its scene or valid partner pose`; Closed, BaselineRestored, RealSavesUnchanged). A scenario input failure, not a mod defect.
- `e44516d7` (`vehicle-gone-play.txt`, **play, `-WanSim bad`**, `africa-jeep`, build `2464f4e`; every expectation met). The guest's Fulton by hand from its lab boot spot (frame `gp-guest-before`: "hold E: Fulton 100%, x24"): `obs; m=FultonInfo; id=25608; att=local` -> `watch` -> `lost` 3.0 s later -> `req; cause=1` -> host `applied; id=25608; via=locator; src=req; ok=1` -> guest `req_done; ms=250`; frames `gp-guest-hold`, `gp-guest-lift-01` (the balloon, "Fulton Recovery Expenses: [GMP -10000]" on the guest's copy), `gp-guest-after` on both screens (gone). The host's Fulton by hand of 25610 from its `goto_vehicle` door spot: `watch; m=Fulton` -> `skip; reason=own_watch` -> `lost` -> `tx; cause=1; actor=0` -> guest `applied; id=25610; via=locator; src=rx; ok=1` 116 ms later; frames `gp-host-lift-02` (the jeep in the air), `gp-host-after` on both screens. K27: four kills committed inside the pairing sweep applied on the guest 322..330 ms later (`src=resync`), eight outside it 299 ms (`src=outcome`); lab without WanSim 215..230 ms (`46b8fc12`). Shutdown `Forced=True`.
- `d9b48fcb` (`vehicle-state-play.txt` without the C4 part, **play, `-WanSim bad`**, lab seed, build `7a33ffa`; every expectation met, `Forced=False`). Sweep, drive and leave (`skip` -> `resume` -> `tx; why=moved; d_mm=13969` -> guest `hold; reason=occupied`), frame `play-left-both`: the jeep where the guest left it in Wialo Village on both screens; status readbacks 9 mm apart. That is VEHICLE_STATE's non-interference, not a pose it applied (the guest's stats `applied=1; moved=0`, the one apply a life; the shared pose is RIDE_FAST's last placement; M4 review); the post-drive guest expectation matched the sweep's line. The host's shots -> `tx; why=life; life=2270` -> guest `applied; ...; life=2270; was=2300` 125 ms later; frames `play-damaged`.
- `5179b50b` (`vehicle-gone-player.txt` revised, lab, `africa-jeep`, build `57d9195`; `Forced=False`). Part 2, **the guest's Fulton by hand of 25610 from its `goto_vehicle` door spot, passed**: `watch; m=FultonInfo; cause=1; att=local` -> `lost` 3.2 s later -> `req` -> host `applied; id=25610; via=locator; src=req; ok=1` -> guest `req_done; ms=66`; frames `pf-hold`, `pf-lift-*` (the balloon), `pf-after` on both screens (gone). Part 1, **the guest's own destruction, failed** (four expectation failures): the guest's five rounds from its boot spot hit veh_lv_0000 (life 2260 -> 2230), then its lever `vehicle_life 25608 0` broke it (`obs; m=VehicleBroken; att=none`), but no `watch` started because no local Damage on the vehicle had been recorded (`V.hit`), so no request went out; the guest's copy holds a wreck (frames `gw-*`: the guest knocked down by the blast) while the host's jeep stays intact (life 2260). See the next run.
- `35ba7929` (`vehicle-damage-census.txt`, lab, build `02d1e95`, the WS4 attacker census `partner_placed_census=1`): **the game posts no GameObject `Damage` message for a vehicle hit by gunfire.** The census logged the four soldiers' `Damage` from `clear_enemies` on both copies but nothing for 25608, although the guest's rounds lowered its life from 2260 to 2230 (the host's rounds missed). So VEHICLE_GONE's rule for the guest ("VehicleBroken within 10 s of a local Damage", 0.13.6) never attributes a destruction by gunfire, and a jeep the guest destroys stays a wreck on the guest's copy only, as before M4 (`7561ab63`). Behaviour unchanged; explosives untested.

**Still open**
- A two-PC session over Steam.
- A jeep the guest destroys is not requested (no vehicle `Damage` message, `35ba7929`); the attribution needs a native hook on the vehicle damage path or the local player's own fire near the vehicle. Damage the guest does short of destruction never reaches the host's copy (no guest-to-host damage op); since the M4 review it is put back on the guest's copy at the host's next state. Wheel and part lives and fire are not synced (no write found).
- A destruction in play was not reproduced here (the C4 of `e235bda6` did not destroy the jeep); it was after the M4 review (`4bbbfde4`, `b31a1b65`: the host's C4, a Despawn on the guest's copy); the wreck parity is proven in the lab only (`0edadbe2`, `3585869d`) and stays behind its lab key.
- Fulton rewards: the Fultoning copy pays and gets the vehicle, the other copy only Despawns [I]; not checked against save resources.
- Why a teleport beside a jeep hides the vehicle Fulton prompt is unknown; the scenarios use the boot spot or the `goto_vehicle` door spot.
- AI-driven, LRRP and reinforcement vehicles (M8); the sortie vehicle, index 0 (M3, E17).
- A physical shove (a player or another vehicle) is untested in lab and play (the push lever is lab only); a guest's push is put back by the host's 10 s refresh. Since the M4 review a real divergence in play is covered: the host's bail out of a rolling jeep (`b31a1b65`).
## October 29, 2026: M3 review fixes (branch `wp/relief`; `world_relief` off by default again)

An independent review of M3 (`wp/relief` at `c282538`) found the code sound but the default flip unproven: the play
rehearsals covered only the arrival and the refusal, a guest Fulton of the relief vehicle was credited twice with
VEHICLE_GONE off (its default), the guest's LOADOUT block (D11) shipped on before the user decided, and the proof
scenarios' waitlogs could be met by earlier phases' lines. Every point was checked first (the review log in
`docs/vehicles-companions-design.md` section 11). Not merged, not pushed. Two-PC: not tested.

**Changed.**
- `4d18561`: `world_relief` is **off by default again** in lab and play (a named 1 still works). Play refuses
  `worldtrigger` and the Fulton levers, so a hidden play rehearsal of a drive, a mission end and a removal is not
  possible yet; the flip waits for one.
- `c7c1ed3` (`lua/coop/world_relief_sync.lua`, tests first): a co-op guest's `OnFulton` never credits the host's sortie
  vehicle; the guest's own Fulton (or a destruction by its player) asks the host (REQ_VEHICLE_GONE for 25600, whatever
  `world_vehicle_gone` says); the host removes its own with its stock Fulton (credited once, on the host), refuses
  while it rides it, and the guest's copy comes back after a refusal or 45 s without an answer. A copy removed because
  the host had none is loaded again when the host's returns; placements have their own budget. The guest's iDroid
  VEHICLES drop stays inactive while the link drops inside the co-op mission. The LOADOUT block is the lab lever
  `relief_lab loadout on` (off by default, D11 open). Lines name their mission.
- `c95998a`: worker step `logmark <host|guest|both>`; `companion-relief.txt` rewritten with a mark per phase and
  VEHICLE_GONE at its default (off); new `companion-relief-guest-fulton.txt` (VEHICLE_GONE on).

**Refuted claims corrected** (section M3 below, design 8.6, roadmap): "no expectation failed" in `d18e8fa0` and
`1a5c1fa0` did not prove the mission-end seize skip or the host-Fulton removal (those expectations matched earlier
lines; the behaviour was confirmed by reading the slices); "the host's Fulton removes it on both copies with one
credit" held only with `world_vehicle_gone=1`.

**Hidden-worker runs** (loopback, research build, native gun stage, copied lab saves, low graphics; each closed with the
baseline restored and the real saves unchanged; archived under `%USERPROFILE%\mgs-coop-run-archive\companions`; frames
`complete/frames/relief/<run8>-*`).

| Run | Scenario | Result |
|---|---|---|
| `5c35644d` | `companion-relief.txt` (the M3 FRAME proof, rewritten), lab, `world_relief=1`, VEHICLE_GONE off | **All 38 expectations met, each after its phase's mark.** The east jeep, the west jeep and (first time) the west truck at the same spot on both screens; the host driving on the guest's screen; the refusal with the toast and LOADOUT left active; the mission end with the jeep alive on both: host credited (`4wdE` 1 -> 2), guest skipped; **the guest's Fulton**: no credit on the guest (no `AddTempResource` in its whole log), the host's own Fulton credited (`4wdW` 1 -> 2), both copies empty; **the host's Fulton**: the guest's copy removed 15 s later through the reconcile (`why=host_none`, no credit) |
| `cbc30b91` | `companion-relief-guest-fulton.txt`, lab, `world_relief=1`, `world_vehicle_gone=1` | **All 18 expectations met.** The guest's Fulton with VEHICLE_GONE on: one request, the host's credited Fulton, its VEHICLE_GONE announcement removed nothing more on the guest; host `4wdE` 1 -> 2, guest 1 |

**Still not shown.** A player's own Fulton or destruction of the vehicle (the worker's lever runs the same stock
Fulton); the guard across a real link drop, the host refusing while it rides it, and the guest's reload after a
refusal, a timeout or the host's vehicle coming back (fixtures); a play rehearsal of a drive, a mission end and a
removal (play refuses the levers that would drive them); types 4, 6..8 and 10; a two-PC session.

**Integration.** `wp/relief` contains M1 (the merge `d911f0c` of `wp/companion-host` `e08dc20`): integrate
`wp/companion-host` first, or accept M1 together with M3.

**Checks.** `pwsh -File tools/build.ps1 -Fast` 294/294 after each change (new tests failed first on the old code:
`lab_config_tests`, `world_relief_sync_lua_check`, `worker_session_tests`); `pwsh -File tools/build.ps1` (Debug, build id
`0.14.3+c95998a`, with only these docs changed on top): 309/309 passed, 98 s.

## October 29, 2026: M3, the sortie vehicle on both copies (branch `wp/relief`; on by default from `5a6bac8` until the review fixes above)

Branch `wp/relief` (from `wp/companions` `068852c`, with M1 merged at `e08dc20`). Not merged into the main checkout, not
pushed. Milestone M3 of `docs/vehicles-companions-design.md` (section 9; results, run ids and frames in section 8.6),
route A (section 8.4): each copy's own engine draws the host's sortie (relief) vehicle from the host's carry values.
Decisions D1 (a) and D8 (b) of 2026-10-28 apply. Lab key `world_relief=0|1` (CFG `r[17]`, lab and play); on by
default from `5a6bac8` after the lab FRAME proof and the play rehearsal passed, **off again since `4d18561`** (the
review fixes above: those runs did not prove what the flip cited).
Two-PC: not tested.

**What it does.**
- The follow seeds carry the host's relief carry vars and paint presets (31 values); the guest applies them before its
  `Load`, so its destination draws the same vehicle (index 0, 25600) at the same spot. The guest keeps its own values
  and every `TppSave.SaveVarsToSlot` writes those (D8 b); a solo load gives them back.
- The host sends VEHICLE_SPAWN (op 52) in free roam on change and every 5 s. A guest copy without the host's vehicle (a
  host continue, a late join) loads it mid mission with `Vehicle.LoadCarry` and sets it at the host's pose when it
  appears more than 3 m off; one parked 20 m off is placed; one the host lacks for 15 s, or of another type, is removed
  without a Mother Base credit.
- Only the host's choice spawns it: the guest never runs the sortie prep, and while paired in free roam its iDroid
  VEHICLES and LOADOUT supply drops are inactive (LOADOUT only with a lab lever since `c7c1ed3`, D11); selecting one
  shows "Only the host can bring or request a vehicle in co-op."
- One credit: the guest skips the mission-end seize; a removal on the guest's copy (VEHICLE_GONE, when that key is on)
  uses `Seize{"Instant"}` and skips that copy's `OnFulton` once. (Not so for the guest's own Fulton with VEHICLE_GONE
  off: credited twice until `c7c1ed3`, see above.)
- Lab levers: `relief_status`, `relief_lab nonactive vehicle|loadout`, `relief_lab forget`, `relief_arm ... [paint]`;
  worker key `3` (the iDroid MISSIONS tab).

**Hidden-worker runs** (loopback, research build, native gun stage, copied saves, low graphics; each closed with the
baseline restored and the real saves unchanged; archived under `%USERPROFILE%\mgs-coop-run-archive\companions`).

| Run | Scenario | Result |
|---|---|---|
| `d18e8fa0` | `companion-relief.txt`, lab (`lab`) | **The FRAME proof; no expectation failed.** The east jeep (Africa) and the west jeep (Afghanistan) at the same spot on both screens; the host driving it on the guest's screen; the guest's real iDroid vehicle request refused with the toast; one mission-end credit (host `4wdE` 1 -> 2, guest unchanged); the host's Fulton removed it on both copies with one credit (with `world_vehicle_gone=1` named). The guest's seize-skip and the three Fulton-removal expectations matched earlier phases' lines; the real lines are in the slice. Superseded by `5c35644d` |
| `40c09484` | `companion-relief-play.txt`, `-Mode play -WanSim bad` (`afgh-relief-lv`) | **The play rehearsal; no expectation failed.** Paired in the helicopter; the host's continue carried the jeep to the guest through the seeds; both at (-347.95, 763.58); the refusal and its toast |
| `98e4af59` | `companion-relief-play-default.txt`, `-Mode play -WanSim bad` (host: the launcher's co-op save copy `coop-relief-30020`, guest `africa-jeep`) | **The default in play while it was on; no expectation failed.** Arrival and refusal only. No key named; the real sortie-prep jeep of the 2026-10-23 two-PC save at (263.56, -228.88) on both copies; the refusal and its toast. The host's frames show the grass (camera pitched down), the guest's the jeep |
| `1a5c1fa0` | `companion-relief-heavy.txt`, lab (`lab`), keys at their defaults | No expectation failed. The west tank in Africa and the west wheeled APC in Afghanistan on both screens; the mission end with the tank alive on both and VEHICLE_GONE off: the host credited (`tnkW` 0 -> 1), the guest skipped (its `alive=1` line read in the slice; the harness matched its boot line) |
| `1914e76c` | `companion-relief-continue.txt`, lab (`afgh-relief-lv`) | No expectation failed. The continue through the seeds; a non-zero paint (FOVA_1) on both copies |
| `8a81f776`, `d3b23cca` | `companion-relief-late.txt`, lab (`afgh-relief-lv`) | A late load draws the vehicle at the drop point (`8a81f776`); after `c818a5a` it is placed at the host's pose, 0.25 m (`d3b23cca`) |
| `25b9c0ae` | `companion-relief.txt`, lab | One wrong expectation (fixed); superseded by `d18e8fa0` |
| `b7d65be9` | `companion-relief-play.txt`, play | Scenario error: the two saves were in different regions and never paired; superseded by `40c09484` |

**Commits** on `wp/relief` after the experiments (`2a5a623`): `d911f0c` (merge of M1 `e08dc20`), `b622340` (M3 behind the
key), `7f84a7a` (lab lever `relief_lab forget`, scenarios), `c818a5a` and `6358845` (a late-loaded vehicle placed at the
host's pose), `1f3acd8` (the lab proof walks the guest's real iDroid path), `5a6bac8` (on by default; an invalid value
refuses), `79d9b5f` (heavy-vehicle scenario), and the docs commit.

**Not done.** A destruction of the relief vehicle (no worker lever destroys one); relief types 3, 4, 6..8 and 10 on both
copies; the host's real sortie-prep selection and its GMP charge (the guest is never charged); the host's in-mission
vehicle drop (route C, later, D6); `world_vehicle_gone` is still off by default, so the guest's copy of a vehicle the
host lost mid mission goes after 15 s through the reconcile (fixture only); the guest's LOADOUT supply drop is blocked
too, even with no vehicle in the loadout (decision D11); a two-PC session (M3 is not done before it).

**Checks.** `pwsh -File tools/build.ps1 -Fast` 294/294 after each change; `pwsh -File tools/build.ps1` (Debug, build id `0.14.3+79d9b5f`, with only these docs changed on top): 309/309
passed, 101 s.

## October 29, 2026: M1/M2 review fixes (branch `wp/companion-host`, lab key, play default off)

Branch `wp/companion-host`, on top of M2 (`35c6f51`). Not merged, not pushed. An independent review of M1 and M2 found
two major and nine minor defects, all with the lab key `companion_host_owned` on, and refuted several claims; the
fixes, their fixture tests (each fails on the code before it) and the corrected claims are in
`docs/vehicles-companions-design.md` section 8.3 (the corrections in place in 8.1, 8.2, 9 and 10). No play default
changed. Two-PC: not tested.

**What changed (with the key on).**
- The host's Quiet lock no longer keeps a session-long copy of its buddy vars. A Quiet it removes is parked: the live
  var reads none and every save writes Quiet back for it, `StageBlockCurrentPosition(true)` parks her again after the
  mission-start slot restore, and a host who calls or picks another buddy keeps that buddy (saved and loaded).
- The guest's PC shows only a host companion it can hold still: the dog. A host horse, walker or Quiet is not shown
  there until its milestone (the seeds and the reconcile give none); a dog is called only at the host dog's pose.
- Both PCs must run the key: each shows it in its own traffic, and the module (the seeds, the mask, the refusals, the
  M2 hold, the mirror attribution) runs only when both have it. The host's deploy waits up to 10 s after pairing for
  the guest's character.
- The session ends 10 s after the link drops (120 s alone after a mission change): the host's lock lifts, the guest's
  copy of the host's companion is deleted and the guest's own buddy vars come back. The reconcile backs off and stops
  after six tries. `companion_cell goto` is lab only. Without the key an own horse or walker counts as the player's
  again (as before M1).
- `lua/coop/world_companion_dog.lua`: a new world chunk with the M2 hold and the companion lab levers (both old chunks
  were near the 32000-byte limit).

**Hidden-worker runs** (loopback, research build, native gun stage, copied lab saves, low graphics; each closed with
the baseline restored and the real saves unchanged; archived under `%USERPROFILE%\mgs-coop-run-archive\companions`).

| Run | Scenario | Result |
|---|---|---|
| `74d5ba7e` | `companion-host-quiet-dog.txt` (host `afgh-quiet`, guest `afgh-horse`, the guest plays Quiet) | No expectation failed; stopped by the worker at a `resetpose` after the Continue (a benchmark pose cannot cross a mission generation). Both keys seen at pairing; the helicopter Quiet sent away and parked, the sortie saves wrote her back, 30010 loaded none on the host and nothing was deleted there (de43726a's code loaded her and deleted her on the first frame); the host's dog call ended the park and the guest's copy loaded the dog at its pose; a host horse: the guest sent its dog away and never showed a horse; checkpoint and Continue: the dog on both copies. Frames: the host's body hid the companion |
| `9165baeb` | `companion-host-quiet-dog-frames.txt` (same seeds) | No expectation failed. The same lock and Continue lines, then host-led travel to 30020 through the helicopter: the host's dog on both copies, never a Quiet. Frames: the host's D-Dog beside Snake before and after the Continue; the host's D-Horse on the host's screen and none on the guest's; on the guest's screen the dog's head only (a rock rise in the way) |
| `b651b42c` | `companion-host-dog-guestview.txt` (same seeds, the guest 4 m to the other side) | No expectation failed. FRAME on both copies with the shipped M2 hold: the host's D-Dog beside Snake on the host's screen and the same dog beside Snake under its marker on the guest's (held 2-214 cm); then the host's D-Horse on the host's screen and no horse on the guest's |

**Not done / open:** the stage hook's own effect in game (no restore brought a Quiet back in these runs); the session
end in game (the worker's `world=0` unpairs only its own copy); the deploy wait for CHARACTER in game (the deploys came
long after pairing); the iDroid dismiss refusal; the host's sortie-prep and iDroid buddy lists; a play rehearsal of
the fixes; a two-PC session. E19 (the dog killed on the guest's PC) and D7 are unchanged.

## October 29, 2026: M2, D-Dog synced and following the host, stage 1 (branch `wp/companion-host`, lab key, play default off)

Branch `wp/companion-host`, on top of M1 (`e08dc20`). Not merged, not pushed. Milestone M2 of
`docs/vehicles-companions-design.md` (section 9) with E3's engagement part and E19's dog row (section 8.2 has every
result, the static reads and the frame names). It rides M1's lab key `companion_host_owned=0|1` (default off). Two-PC:
not tested. **E19 fails for the dog, so the play default stays off and decision D7 is the user's (design section 10).**

**What it does (with the key on).**
- The host streams its dog's pose in COMPANION_POSE (op 57, a datagram: read every 100 ms, sent at 10 Hz while the
  dog moves and 2 Hz idle); the guest answers COMPANION_ACK (op 49, 1 Hz). M1's COMPANION (op 48) now carries the dog's
  pose too, so a late join's reconcile calls the dog there.
- The guest holds its copy of the host's dog every 100 ms with the stop-AI form of `LuaAiStayAndSnarl` (`look`,
  `useMarker=false`: modes 1/2, whose Dog AI update is skipped) placed by the command's own `warp=1`, led by the sample's
  age (consecutive 10 Hz samples only), plus the stock `SetWarpPosition` beyond 15 m and for a dog that drifts over 1 m
  off an unchanged target. It is never released to the guest's AI (no release on a stale stream, a kind change, an
  unpair or a stop).
- The dog pose receipt no longer needs body 1, so both roles read their dog while the partner is hidden (heli, mount).
- D8 (b) also keeps the guest's own `deadBuddyType` and `buddyCommonFlag` in its saves.
- Lab levers (guest): `companion_lab hold <warp|marker|stop>`, `mark <leg>`, `near_guard <on|off|shield>`,
  `protect [off]`, `repair <on|off>` (the D7 candidate, default off). Worker key `3` (the call menu's "Get it").

**Hidden-worker runs** (loopback, research build, native gun stage, copied lab saves, low graphics; each closed with the
baseline restored and the real saves unchanged; archived under `%USERPROFILE%\mgs-coop-run-archive\companions`).

| Run | Scenario | Result |
|---|---|---|
| `4cd88ea2` | `companion-dog.txt` (earlier form) | The warp hold within R4 in every leg window (walk to 1.46 m, run to 4.4 m, dash to 2.8 m); the guest 40 m away: 2-23 cm; E3: 176 s within 1.4 m of a guard in combat, zero `mirror_engaged` |
| `994d252f` | `companion-dog-mb.txt` | Mother Base walk within 0.82 m, the dog on both screens; the run leg hit a container (scenario); host-led travel to 30010 with the host left in its helicopter: the guest read and held its dog for 42 s with body 1 unrealized |
| `ba6d6ff8` | `companion-dog-play.txt`, `-Mode play -WanSim bad` | 3 expectations failed: the dog stayed unrealized on both copies until each camera turned (136 s), so the legs ran with no dog. Scenario fixed (cameras turn first) |
| `d937191f` | `companion-dog.txt` | The legs (walk one window at 3.11 m, the rest within R4), away, E3 155 s, the dog on both screens in frames; the call menu's "Get it" took nobody down (3 expectations failed); **E19: a guard firing at the prone guest killed the guest's copy of the dog** (`Dead`, `deadBuddyType` 0 -> 4) |
| `dc761c98` | `companion-dog-e19.txt` | **E19 again with `SetDisableDamage` held: killed after 13 s**; the guest's buddy block unloaded and the dog did not come back; the guest's checkpoint save kept its own flags (`own_dead=0; live_dead=4`); "Get it" again took nobody down |
| `86421fa6` | `companion-dog-repair.txt` (first form) | Void for the repair: the shield never engaged (scenario; fixed) |
| `033193d3` | `companion-dog-repair.txt` | No expectation failed. The legs again (walk one window at 3.21 m); then the dog killed on the guest's copy with the D7 candidate `companion_lab repair` on: the death flags written back at once, the block unloaded, M1's reconcile loaded the dog again 18 s after the death and it was held beside the host's dog to the end (logs; no frame of the returned dog) |

Earlier runs of the same milestone, superseded: `8c0b425f` (the M1 hold never moved the dog), `ffb2d7ab` (marker
against warp), `52010d22` (void: seeds in different helicopters).

**Not done / open:** E19 (the dog can be killed on the guest's copy; D7 with the user); the dog's takedown for the
kill commit/apply check (not reached through the call menu in the worker); the play rehearsal (not re-run while the
default cannot flip); the Mother Base run leg; a two-PC session.

## October 29, 2026: M1, the host owns the companion (branch `wp/companion-host`, lab key, play default off)

Branch `wp/companion-host` from `wp/companions` `068852c` (the vehicles and companions design). Not merged, not pushed.
Milestone M1 of `docs/vehicles-companions-design.md` (section 9), with the experiments E1, E3 (save part), E4 and E20
(section 8.1 has every result, the static reads and the frame names). Decisions D5 (a), D1 (a) and D8 (b) of
2026-10-28 apply. Everything is behind the new lab key `companion_host_owned=0|1` (CFG `r[16]`, lab and play, default
off). Two-PC: not tested.

**What it does (with the key on).**
- The host's buddy is the session's one companion. The follow seeds carry the host's buddy vars (type, start status
  and position, every look var, and the four `*MissionStart` copies that a new mission loads); the guest applies them
  before its `Load`, so its copy loads the host's buddy. The guest keeps its own buddy vars and every
  `TppSave.SaveVarsToSlot` writes those (D8 b); a solo load gives them back.
- The host sends COMPANION (op 48, kind only for now) on change and every 5 s; a guest whose loaded buddy differs
  calls the host's kind with its own block after 8 s (15 s to send one away), 4 m in front of its player. (Review
  2026-10-29: a horse there was in the wrong place and mountable; now the guest's copy loads only the dog, at the host
  dog's pose: see the review-fix section above.)
- The guest cannot call or change a companion (a dismiss through the iDroid is not covered: `DeleteBuddy` requests
  NONE, which the disable byte list never holds; review 2026-10-29): the Lua entry points refuse another type, the F9 summon is
  host-only, `PlayerPad.CALL` is masked while the host has a companion out (the call menu and whistle; also Knock and
  Sonar; released in a CQC hold), and the disable byte covers every type that is neither the host's nor loaded. Each
  refusal shows a toast (NOTE 13).
- A guest who plays Quiet sends CHARACTER (op 56) before the sortie; the host disables buddy Quiet, sends a Quiet already
  out away, rewrites a Quiet sortie and its `buddyTypeMissionStart` to none (its own save keeps her; review 2026-10-29:
  the OnAllocate slot restore undid that rewrite and the first-tick delete was what protected, fixed above), and both copies
  refuse the Mother Base cell Quiet (`TppStory.CanArrivalQuietInMB`).
- Attribution is id-aware: a companion that stands for the partner's counts as `mirror`, is logged
  (`event=mirror_engaged`) and never forwarded.
- Lab levers: `debug seed_buddy <horse|walker|quiet|dog|none>`, `companion_status`, `companion_cell [goto]`,
  `companion_lab call_here [kind]` (host), `companion_lab quiet_out` (host).

**Hidden-worker runs** (loopback, research build, native gun stage, copied lab saves, low graphics; each closed with the
baseline restored and the real saves unchanged; archived under `%USERPROFILE%\mgs-coop-run-archive\companions`).

| Run | Scenario | Result |
|---|---|---|
| `489b3c6f` | run A, `companion-host-owned.txt` (`afgh-ledge` / `afgh-horse`) | Pass, no expectation failed. The guest loads the host's dog (`loaded; kind=dog; source=seed`) and keeps its horse; the dog on the host's screen; the call menu on the host's screen and none on the guest's; the F9 refusal toast; reconcile to D-Horse and back, a horse on both screens; every guest save writes its own horse while the dog is live |
| `fa397547` | run C, `companion-host-owned-mb.txt` (`africa-jeep` / run A's guest save) | Pass, no expectation failed. Mother Base: the dog on both screens; the guest's save read back with its own horse (E3); the cell quest refused on both copies (`mbquiet_hidden; stock=true`); D-Walker developed in the lab save |
| `b993af09` | run B, `companion-host-owned-quiet.txt` (`afgh-quiet` / `afgh-ledge`) | Pass with three scenario errors (fixed). The Quiet lock: Quiet sent away in the helicopter, the seeds carry none, the host's toast, no buddy Quiet on either screen, the empty cell on both screens. Found the host's sortie copy coming back as Quiet (fixed in `ae4c3e9`) |
| `de43726a` | `companion-host-owned-quiet-play.txt`, `-Mode play -WanSim bad` | Pass with three scenario errors (fixed). The late-join lock in play: `delete_buddy`, `sortie_rewrite`, `start_rewrite`; no buddy Quiet on either screen |
| `b83c9f7a` | `companion-host-owned-play.txt`, `-Mode play -WanSim bad` (`afgh-ledge` / `afgh-horse`) | Pass, no expectation failed. The guest loads the host's dog through the seeds in play (held 4 cm from the host dog's pose), the CALL mask (frames: the host's call menu, none on the guest's), the F9 refusal toast, the reconcile to the host's F9 horse. Night, both players on one spot: no frame shows the companion |

Earlier runs of the same branch, superseded: `dd4c61ff`, `a4296449` (seeds and E4 census), `8ad7a986` (the guest loaded
its own buddy from `buddyTypeMissionStart`), `6306df68` (logs passed, frames showed no companion), `9e8f22a7` (a host
warp made the guest send its dog away; the cell warp landed in the sea).

**Seeds** (`D:\mgs-coop-seeds`): `afgh-horse` (guest), `afgh-quiet` (host), `mb-quiet-cell` (both, beside the cell),
`mb-walker` (host, D-Walker deployed). The last two are not boot-checked yet.

**Commits** on `068852c`: `51bf9ec` (M1 behind the key), `61191a8`, `1d56c01`, `7ffdbad`, `c7b38d7`, `5b1cf10`,
`88307b9` (reconcile debounce, cell warp, framing levers, log lines), `b6628b1`, `ae4c3e9` (the sortie copy rewrite),
and the docs commit.

**The play default stays off.** The guest's copy now runs a real buddy AI for the host's companion. E3's engagement
part (M2), E15 and E19 have not run, so nothing yet shows that it never acts or gets hurt on its own on the guest's
copy; decision D7 waits for them. Two-PC: not tested.

**Open.** The iDroid buddy request and the host's sortie-prep and iDroid buddy lists were not reached in the worker
(E4); the locked-companion save case (E3); the guest's Quiet body cannot be filmed in the worker (its guest copy
refuses the Quiet reset bypass with `body_hash` at every load since before M1); with the key on the guest has no Knock
and no Sonar while the host's companion is out.

**Checks.** `pwsh -File tools/build.ps1 -Fast` 292/292 after each change; `pwsh -File tools/build.ps1` (Debug, build id `0.14.3+ae4c3e9`, with only these docs and the two play scenarios
changed on top): 307/307 passed, 104 s.

## 2026-10-29: M3 experiments round 2: mid-mission load, seed `afgh-relief-lv`, the ACC (`wp/relief`, lab levers only, not merged)

Follow-up on `wp/relief` to the section below. Results in `docs/vehicles-companions-design.md` section 8.5; M3 there
lists what the build needs. M3 itself is still not implemented.

- **Built (lab only).** `relief_load <type> <x> <y> <z> <rotY>` in `lua/coop/world_relief.lua` (routed by
  `lua/coop/debug_vehicle.lua`): it writes the carry vars and calls `Vehicle.LoadCarry()` mid mission, and refuses
  outside lab mode, outside free roam, over a live index 0 or while a sortie choice is armed. Tests
  `world_relief_lua_check` (75 checks; the new ones failed before the lever existed) and `debug_vehicle_lua_check`
  (the routing; failed on the old module). Scenarios `relief-midload.txt` and `relief-continue.txt`.
- **Seed `afgh-relief-lv`** in `D:\mgs-coop-seeds` (from run `612afff7`): 30010 enemyBase_S LZ; the host's save holds
  relief type 2 (east jeep) at the LZ, the guest's holds none. Boot it with `boot fast`.
- **Hidden runs** (both closed, baseline restored, real saves unchanged; archived in
  `%USERPROFILE%\mgs-coop-run-archive\companions\<id>`):

| Run | Scenario, seed | Result |
|---|---|---|
| `612afff7` | `relief-midload.txt`, lab | ACC: Tab opens the iDroid on its MAP tab; the MISSIONS tab needs key 3, which is not a worker key, so a scripted sortie-prep selection was not reached. Mid-mission load: the host's `relief_load 2` gave the jeep at (1227.24, 19.43, -127.05) in 8 s while the guest's copy had none; the guest's own load with the same values gave it at the identical pose; frames show the jeep on both screens (the east jeep in Africa, a body free roam there does not load). Mission end: the host credited it once (`AddTempResource 13`), the guest's seize was skipped. Seed `afgh-relief-lv` taken from this run. |
| `fc6f1f01` | `relief-continue.txt`, `afgh-relief-lv` | The host's Continue restored the jeep at (-347.97, 315.85, 763.60). The west tank (type 9, `GetVehicleType` 7) drew at the Africa LZ on the host's copy (frame). The guest's copy stopped on the post-load "Space: Resume Game" page after `boot fast`, so all its commands were refused until the travel: its load from the seed values and its tank were not tested. |

- **Route A stands**, now also for a continue or a late join (a mid-mission `Vehicle.LoadCarry()` with the host's
  values). Not covered: the guest's tank, relief types 3..8 and 10, a non-zero paint preset, a scripted sortie-prep
  selection (needs key 3 in the worker) and the host's real sortie-prep GMP charge.
- **Defaults.** Nothing flips: lab levers only.
- **Checks.** `pwsh -File tools/build.ps1 -Fast`: 291/291. `pwsh -File tools/build.ps1` (Debug): 306/306, 97 s.

## 2026-10-29: M3 experiments E2, E17, E18, the sortie (relief) vehicle (`wp/relief`, lab levers only, not merged)

Branch `wp/relief` from `wp/companions` `068852c`. Experiments only: M3 itself is not implemented (it waits for M1's
seeds and refusals). Results and the route decision are in `docs/vehicles-companions-design.md` section 8.4 and M3.

- **Built (lab only).** `lua/coop/world_relief.lua`, a new world chunk spliced last, reached through the debug
  commands `relief_census` (read only: carry vars, index 0, GMP and the nine Mother Base vehicle resources, paint
  presets, the sortie loadout slot), `relief_arm` (writes the carry vars at the ACC sortie, as the sortie prep's
  SetRelief does), `relief_seize stock|skip`, `relief_remove instant|fadeout|fulton [quiet]` and `relief_face`. Every
  lever but the census refuses outside lab mode; the hooks install with the first lever only. Tests:
  `world_relief_lua_check` (new, failed before the module existed), `debug_vehicle_lua_check` (routing),
  `host_trial_patch_check` and the composed world check list the chunk. Scenarios `relief-e2.txt`, `relief-e17.txt`,
  `relief-coop.txt`.
- **Seed (decision D10).** `coop-relief-30020` in `D:\mgs-coop-seeds`: a read-only copy of the host's launcher co-op
  save (`<game>\mgs-coop\coop-saves`, the path `native/launcher/core/core_paths.cpp` gives), both roles, source set hash
  `E0C1476C...8272`. It holds a real sortie-prep choice (relief type 2, east LV) saved in 30020. Never written back.
- **Hidden runs** (all closed, baseline restored, real saves unchanged; archived in
  `%USERPROFILE%\mgs-coop-run-archive\companions\<id>`):

| Run | Scenario, seed | Result |
|---|---|---|
| `698e67cf` | `relief-e2.txt`, lab | The lever wrote at the destination's mission start: no relief vehicle on either copy (the game's `Vehicle` table refuses writes; the mission start runs on the loading thread). The seize wrappers worked on both copies. |
| `5874afcd` | `relief-coop.txt`, `coop-relief-30020` | E2 real choice: the save continues with `reliefVehicleCarryType=514` (type 2) and 25600 alive, `GetVehicleType` 2. The lever at the ACC sortie gave the host type 2 at the 30010 LZ; the guest's commands were refused while it was still loading, so its copy had none. Unsuppressed seize: both copies credited `4wdEast`. |
| `fafd3fdd` | `relief-e2.txt`, lab | E2 route A emulated: type 1 at (1226.94, -127.08) in 30020 and type 2 at (-347.95, 763.58) in 30010, identical on both copies (frames `relief-arrival1`). E18: the host's seize credited once (`AddTempResource 14`, `4wdWest` 1 -> 2), the guest's `seize_skipped`, GMP unchanged. E17 `Instant` on the guest still credited (`AddTempResource 13`). Then the `relief_goto` warp dropped both players through the slope: shared game over, session forced closed (`Forced=true`). |
| `9f01d603` | `relief-e17.txt`, lab | Route A again at both LZs. E17: `FadeOut` (guest) and `Fulton` (host) remove index 0 on that copy only and credit (the guest's `4wdWest` 1 -> 2 at the next mission end); `Instant` with `quiet` (the receiver's `TppTerminal.OnFulton` for 25600 skipped) removed it with no credit (frames `relief-e17-quiet-before/after`). |

- **Route decision: route A** (each copy's own engine spawns the host's relief choice as index 0). What M3 needs is
  listed under M3 in the design. Not covered: relief types 3..10, a non-zero paint preset, a guest continue with its
  own 25600, the host's real sortie-prep GMP charge, and the seed path itself.
- **Defaults.** Nothing flips: the module is lab levers only, so no `-Mode play -WanSim bad` rehearsal was needed.
- **Checks.** `pwsh -File tools/build.ps1 -Fast`: 291/291. `pwsh -File tools/build.ps1` (Debug): 306/306, 102 s.

## October 28, 2026: 0.14.3 (Africa guns, free-roam vehicle fallback, host-log fixes)

**Package (2026-10-28):** `dist/DiamondDogsTogether-0.14.3-20261029T023814Z-d15f739d/DiamondDogsTogether-0.14.3.zip` (10,222,077 bytes), SHA-256 `E199D144DDDD444B3C313F322796CB89EE6C27BB8331D549853BC55AEFA7A965`, built by `tools/package.ps1 -Friend` from the clean tree at `a4c18fa` with Release receipt `20261029-023457-0c5320d4b721440382a548fdbf8679c6` (passed, 296 audited tests). `DiamondDogsTogether.exe` SHA-256 `3A2A81FB3E2645EE808EFE1E825A592B1F535E78819F95381BA4EC28A21921A9` and `dinput8.dll` `324AF4748F9DCCD4765BA801A5E3A071C9D71EB938793469D25FFFCA577E9F89`, both build id `0.14.3+a4c18fa`. Its `source.zip` holds 1,337 files, among them all 17 `native/*.inc` loader fragments.

It supersedes the first 0.14.3 package (`dist/DiamondDogsTogether-0.14.3-20261029T020207Z-b6469c40`, SHA-256 `E7015643...0B5E`, built at `a0653b5`). That package's `source.zip` lacked the 17 `native/*.inc` files that `native/*.cpp` `#include` into `dinput8.dll` (for example `native/partner_driver_seat.inc`), so it was not complete corresponding source, and the Release receipt did not hash them; 0.14.2 had the same gap. `a4c18fa` ships `.inc`, the `tests/fixtures/partner-lab` logs and the Go archive probe (`tools/archive_probe`), binds `.inc` in the receipt's source snapshot, and replaces the user's profile path in the docs with `%USERPROFILE%`. Checks at `a4c18fa`: `pwsh -File tools/build.ps1 -Fast` 290/290, `pwsh -File tools/build.ps1` (Debug) 305/305, `tests/worker_checks_tests.ps1` 131 checks.

Branch `dev/oct7-continue`. Releases the 2026-10-28 fixes for the two-PC test of 2026-10-23 (`docs/two-pc-test-2026-10-23.md`). Each is proven in the hidden worker or by fixtures, as the sections below record; none is two-PC tested yet.

- **Africa guns (bug 1, `beccaa7`, merge `952fc64`).** The generalized gun writer accepts the three natively empty states F1-F3 that made it refuse for good in 30020, so the host's guns bind on the friend's screen in Africa. See [Africa fixes and host-log fixes](#2026-10-28-africa-fixes-and-host-log-fixes-integrated-on-devoct7-continue-unreleased-not-two-pc-tested).
- **Free-roam vehicle fallback (bug 2, `a48a674`, `35d5519`, merge `30a6a0a`).** When the driver's vehicle does not exist on the viewer's copy, the by-id seat gate answers `absent` and free roam keeps the driver visible (the 0.13.6 fallback); Lua `R.present` stops placing a missing instance and still places a wreck. See [Africa vehicle, the driver stays visible](#october-28-2026-africa-vehicle-the-driver-stays-visible-branch-wpafrica-vehicle-unreleased-hidden-run-proven-not-two-pc-tested).
- **Host-log items 2, 3, 4 and 6 (merge `d727516`).** Item 2: a predicted manifest preload after a save handoff and the `late_manifest` line (`09ebf71`). Item 3: the `partner_ammo_leak` `vars_change` classifier, the fire/reload strip scoped to the mission, and an item-only job never latching the support lane (`f492d77`, `3fd064a`). Item 4: the box as a downed posture, and a timeout never latches `failed` (`2f3332b`). Item 6: the downed owner judged from body 0 (`LiveBodies`, `08f73d6`). See [Host fixes](#october-28-2026-host-fixes-two-pc-test-2026-10-23-items-2-3-4-6-of-the-host-log-pass) and [Host fixes proven in the hidden worker](#october-28-2026-host-fixes-proven-in-the-hidden-worker-items-2-3-4-6-one-more-fix).
- **Developer tooling, no player change.** The hidden worker accepts a Diamond Dogs Together drop-in install as an owned baseline (`2c9341c`, `916708f`); the downed-damage contract evidence is restored (`857a2b9`).
- **README Known limits.** A vehicle brought from sortie prep (the helicopter's vehicle choice) does not appear on the partner's screen; the partner sees the rider without it. World vehicles found on the map are the ones to ride together.
- **Version.** 0.14.3 in `CMakeLists.txt`, `README.md`, `tools/package.ps1` and `tools/probe.ps1`; `tools/set-session-mode.ps1`, `tools/package-install.ps1` and `tools/dev/Play-LocalCoop.ps1` accept 0.14.3 and still 0.14.2.
- **Checks before the bump commit** (build id `0.14.3+857a2b9`). `pwsh -File tools/build.ps1 -Fast`: 290/290 passed. `pwsh -File tools/build.ps1` (Debug): 305/305 passed, 103 s.

## 2026-10-28: Africa fixes and host-log fixes (integrated on `dev/oct7-continue`, unreleased, not two-PC tested)

Three fix branches for the two-PC test of 2026-10-23 are merged locally with `--no-ff`. Nothing is pushed or packaged, so the next two-PC test needs a 0.14.x package on both PCs. The friend is still on 0.13.6. `docs/two-pc-test-2026-10-23.md` has the per-item status table and the checklist for the next test.

**Merges**

| Merge | Branch at | Contents |
|---|---|---|
| `952fc64` | `wp/africa-guns` `c594e41` | Bug 1, Africa guns |
| `30a6a0a` | `wp/africa-vehicle` `03dabda` | Bug 2, the Africa vehicle; the section below |
| `d727516` | `wp/host-fixes` `6422519` | Host-log items 2, 3, 4 and 6; the two sections below |

The only conflict was in this file: both branches add a 2026-10-28 section at the top, and both are kept.

**Hidden-worker runs** (all loopback, copied lab saves, low graphics, research build, native gun stage). Each run closed with the baseline restored and the real saves unchanged.

- **Bug 1, the host's guns missing on the friend's screen in Africa.** `beccaa7` fixes it, after the receipts added in `e4f2415`. Three readiness rules of the generalized writer treated natively empty states as occupied, which made the writer refuse for good in 30020:
  - F1: the released target's D0 flag bits (`selected_bank_aux_not_empty`).
  - F2: the support writer's arm and grenades in slots 6 and 3 (`other_slot_unexpected`).
  - F3: the native cleared effect record 0x7FF after an unrealize (`remote_slot_not_empty`).

  The writer now accepts all three (docs/native-puppet.md "Generalized writer"). The new receipts are `equip_writer=aux` and `equip_writer=remote_slot`.
  - 30020, `partner-native-africa-guns.txt` with `-SaveSeed africa-jeep`, runs `96b5c909`, `8e97cb60` and `05f68a20`: both roles bind slot 2 (9) and slot 1 (46) with no backoff and log `partner_weapon=match; strip=0x0`. The frames of `05f68a20` show each partner aiming the rifle.
  - 30050 regression, `partner-native-mb-guns.txt` with `-SaveSeed mb-ladder`, run `04b2f3af`: the same binds.
  - Limit: no run reproduced the dirty head (every `aux` line read `f00`), so F1-F3 are proven by fixtures only.
- **Bug 2, the host hidden while driving in Africa.** `a48a674` and `35d5519`, with run `31adc898` (also `436a440c`, `011a03f2`, `7561ab63`); the details are in the next section.
  - When the vehicle is absent on the viewer's copy, the by-id gate answers `absent` and free roam keeps the driver visible.
  - RIDE_FAST skips a missing instance but still places a wreck.
  - Open: the host's sortie-prep (relief) vehicle itself still does not exist on the guest's copy.
- **Host-log item 2, late manifest** (`09ebf71`). Run `c07b8136` proves the staged half. The predicted preload is unit-tested only.
- **Host-log item 3, box-bump `vars_change`** (`f492d77`, `3fd064a`). Run `5ceb4985` found the support-lane latch, which `3fd064a` fixes. Runs `6041f5e5` and `0c8f1ab0` are clean.
- **Host-log item 4, box as the downed posture** (`2f3332b`). Runs `ce66a0bf`, `44e5a233` and `a5f30e31`.
- **Host-log item 6, the downed owner judged from body 0** (`08f73d6`). Run `20d41b16`.

**Checks at `d727516`**
- `pwsh -File tools/build.ps1`: Debug, configured afresh, `ctest -j 16`. Build id `0.14.2+d727516`. 305/305 passed, 101 s.
- `python tools/generate_partner_contract.py --check`: 800 code sites and 204 vtable slots match exe `085c2f82...bb45`. `pefile` 2024.8.26 was already installed.
- `generate_equip_writer_pins.py --check`: 118 gates and 10 fact groups match.
- `generate_soldier_warp_contract.py --check`: 19 sites and 10 slots match.
- `generate_nametag_contract.py --check`: 24 sites match.
- `generate_downed_damage_contract.py --check`: 57 sites match. Its evidence file `docs/evidence/downed-damage-2026-10-10.json` had been removed in the repo cleanup `a06a24e` and is restored from history.

**Still open for the next two-PC test**
- Every fix above over a real Steam link with latency.
- Bug 1's dirty states live.
- The predicted preload after a real handoff relaunch.
- A boxed-first friendly-fire down.
- A bump that moves a weapon or ammo field.
- Relief-vehicle parity.
- Host-log item 7 (ask the friend about the ledge fall) and the minor item 8 (the first-appearance freeze and warps).

## October 28, 2026: Africa vehicle, the driver stays visible (branch `wp/africa-vehicle`, unreleased; hidden-run proven, not two-PC tested)

Bug 2 of the two-PC test of 2026-10-23 (docs/two-pc-test-2026-10-23.md): in Africa (30020) the host drove his relief vehicle 25600 (TppVehicle2 index 0, from his sortie prep), which existed only on his copy. The guest's by-id read refused `live` (pool bit clear), free roam ran the named seat path (Lua `no_name` -> `lua_refused`, latched) and the guest saw no host for the whole 24.6 s ride.

- **Native (`native/partner_driver_seat.inc`).** `SeatByIdGate` reads this copy's vehicle before the addendum checks and answers `absent` when the read refuses `live` or `index` (`VehicleAbsentRefusal`): no vehicle with that id here, and the engine's id lookup `0xA272A0` used by the DM seat anchor tests the same pool bit. `SeatFallbackHere(mission, reason)`: Mother Base keeps every fallback reason (`absent`, `local_read`, `unrealized`), free roam takes the 0.13.6 visible fallback for `absent` only, at the episode start and at the id write (not latched). Expected guest lines: `partner_ride_seat=byid; ...; offer=0; reason=absent`, `partner_ride_seat=fallback; ...; reason=absent; show=warp; byid=0`, after the get-off `exit; ...; via=fallback`; never `lua_refused`, `unavailable; ...; block=` or `partner_ride=hide; reason=ride` for that vehicle. Mother Base's Fultoned-jeep case now logs `absent` instead of `local_read` (same behaviour). Any other free-roam refusal keeps the named path.
- **Lua (`lua/coop/world_ride_pose.lua`).** `R.present`: each RIDE_FAST track asks `IsAlive` at most every 500 ms; while the answer is not true and `GetPosition` does not answer either (no instance here), no tilt write and no SetPosition run and the frame counts on the `ride_fast` line as `absent=N` (`state=absent`). A wreck (`IsAlive` false, still in the pool, `GetPosition` answers) is placed as before, like the native gate's `absent` (the pool bit only). The session's `applied=1157` counted calls into a missing id.
- **Tests.** `partner_driver_tests` `TestRideSeatAbsent` (new; failed on HEAD at its first check), the gate table's `local_read` case is a non-pool refusal (`object`), the Mother Base Fulton block expects `absent` and keeps the fallback for `local_read`; `world_ride_pose_lua_check` (new block; failed on HEAD).
- **Hidden runs (`tools/worker/scenarios/coop-vehicle-absent-30020.txt`, `-SaveSeed africa-jeep`, research build, low graphics; each Closed / BaselineRestored / RealSavesUnchanged, no visible fallback; run folders in `%USERPROFILE%\mgs-coop-run-archive\africa\`).**
  - `31adc898` (HEAD `35d5519`, both parts, every expectation met). A, the control (both copies have jeep 25608): guest `byid; offer=1; reason=none`, the named seat `ride_seat; id=25608; seat=0; name=veh_lv_0000`, `confirmed`, `ride_fast; applied=9..282; state=interpolate; absent=0`, `exit; reason=foot; via=getoff; confirmed=1`; frame `control-drive-05`: the host seated in the moving jeep. B (the guest's copy alone loses 25608: `vehicle_remove;id=25608;via=locator;sync=0;alive=0`): `byid; offer=0; reason=absent; remote_cat=1; local_cat=255`, `fallback; reason=absent; show=warp; byid=0`, `ride_fast; state=absent; absent=102..702` with `applied` / `tilt_written` held at 283 (no SetPosition, no tilt write, no `partner_ride_tilt=refused`), `exit; reason=foot; via=fallback`; body 1 followed the drive on the guest's copy (probe `pos1_mm` -650.4,870.1 -> -659.0,843.8; the host ended at -659.7,844.4). Frames: `absent-before` (no jeep on the guest's screen), `absent-drive-06` / `absent-turn-01` (the host visible and moving with no jeep under him). Never in either part: `ride_seat_refused`, `refused`, `unavailable`, `seat_far`, `partner_ride=hide; reason=ride`, `viewer_changed`, a fault.
  - `436a440c`, `011a03f2` (HEAD `a48a674`, B only): the same B log chain; the watcher stayed at the boot spot (or `goto_partner` went to the stale boarding pose: the partner pose does not update during a ride), so the host mostly left the guest's camera.
  - `7561ab63` (HEAD `a48a674`, A only, a longer drive into the rock wall): the named seat confirmed in 30020, then the guest's copy of the jeep was wrecked there only (`Neutralize` for 25608 on the guest alone). `R.present` took the wreck's `IsAlive` false for absent, RIDE_FAST stopped placing it, the seated driver drifted (`err_mm` 3017), `seat_far`, `refused; reason=far`, `partner_ride=hide; reason=ride`. Fixed in `35d5519` (a wreck still answers `GetPosition` and is placed as before; unit-tested, not re-run live: B of `31adc898` shows `GetPosition` does not answer for a Despawned id, so `absent` still counts).
  - `coop-vehicle-gone.txt` part B now waits for `reason=(absent|local_read)` and the fallback instead of the ride hide (and its host status waitlog names `ride=2`); not re-run.
- **Still open.** The vehicle itself is still absent on the guest's screen (the 0.13.6 fallback look: the host standing at seat height); relief-vehicle parity at the sortie is a separate branch. The horse rider hide (host-log item 5) is the act-based `partner_native_mount=hide` rule, not this gate, and stays as decided (docs/mounts.md "Risks").

## October 28, 2026: Host fixes proven in the hidden worker (items 2, 3, 4, 6), one more fix

Hidden private-desktop worker only (no visible fallback), research build, native gun stage, copied lab saves, low
graphics, `MGS_GAME_LUA_DIR` unset, under the verify lock. Every run below closed with the baseline restored and the
real saves unchanged; run folders are in `%USERPROFILE%\mgs-coop-run-archive\africa\<id>`. Runs at `e7fb9ac` used
receipt `20261029-002820-43e3a1fd...`, runs at `3fd064a` receipt `20261029-005424-54e9c13c...`. The four scenarios
carry the run lines in their headers.

- **Item 4 (box as the downed posture), `downed-boxed-friendly-fire.txt`: proven for the session's order.** The
  guest's rifle downs the standing host, then the host boxes (the 2026-10-23 order: down=1 at h12188, cbox 0.5 s
  later). `ce66a0bf`, `44e5a233` (`e7fb9ac`) and `a5f30e31` (`3fd064a`): `controls_pending_prone` for about 3.5 s,
  then `partner_status=local; st=0x4` and `downed_control=observed;command=1;reason=downed;posture=box;generation=4`;
  `downed_status` down=1 cause=1 lock=1 at once and 8 s later (past the 6 s deadline that latched the session); no
  `controls_timeout*` line and no `step=refused` in any run. The guest revived the boxed host (`44e5a233`,
  `a5f30e31`), and in `a5f30e31` a third down re-armed under generation 5 with `posture=box` and held past 8 s.
  - The first version's revive held E for about 2.6 s, under the host's 3 s grant (`world_downed` HOLD); the scenario
    now renews the hold five times (the `coop-carry-wounded.txt` timing).
  - **Not staged: a host boxed before the shots.** The box stops most rifle rounds: a prone guest 6 m away landed 5
    hits (`44e5a233`, 3090/7800 HP) and a crouched guest 4 m away, reticle on the box, about 3 hits from two
    magazines (`a5f30e31`, 5274/7800). The boxed-from-the-first-lock case stays unit-test-only
    (`downed_player_tests`, `DownedPosture`).
- **Item 3 (vars_change, benign item changes, mission strip), `partner-box-bump-vars.txt` (play rehearsal): proven,
  plus one defect fixed.** A box dash (w+shift; the session's body 1 ran at 3.5 m/s in its box) reproduced the
  collision (`downed_defense ... attack=85` both ways). On the guest's copy its own box broke inside body 1's job:
  `partner_input=vars_change; verdict=benign; ...; fields=ammoStockCounts[15]:8>7` (`5ceb4985`, `6041f5e5`,
  `0c8f1ab0`), and no `ammo_leak` or `fire_reload_off` on either copy. In `0c8f1ab0` the guest's copy then showed
  the host's shots (body 1's magazine 3, 2, 1, 0) and its reload (8).
  - **Fixed, `3fd064a`:** in `5ceb4985` the support lane, which checks digests of the same block in the same window,
    still logged `partner_support=stock_check; reason=stock_leak` and `partner_support=refused;
    reason=stock_leak_mission_latched` (as the session did at h10254): no partner throws for the rest of the mission
    after a box bump. `partner_ammo_leak::SupportJobLeak` and `AfterJob`'s verdict now keep an item-only job from
    latching the lane; an ammo write still latches it (`partner_defer_tests`, failing first; `partner_ammo_leak_tests`).
    `6041f5e5` and `0c8f1ab0`: the same benign bump, no support stock line.
  - Not seen in the worker: a weapon or ammo field moved by a bump (no `verdict=leak`), so the mission-scoped strip
    (`scope=mission`, cleared at MissionStart) is unit-test-only (`partner_driver_tests`).
- **Item 2 (late manifest), `partner-equip-late-manifest.txt` (play rehearsal): the staged half proven, `c07b8136`.**
  The host continued while the guest was at its boot screens: `equip_preload=refused; reason=no_manifest; ...;
  source=none`; the guest's manifest came from its title, then `equip_preload=late_manifest; mission=30010;
  preload=refused; source=none; preloaded=0; remote_base=0,46,9; covered=0/2; guns=next_load; link_epoch=3`. The
  scenario gained `title guest 240` (titlemenu stops at the title). The writer still bound slot 1 (46) on the host,
  probably because the host's own loadout carries base 46; `guns=next_load` counts preloaded ids only. **The
  predicted preload (`source=predicted`) stays unit-test-only:** it needs a guest that exits after a handoff and
  relinks to the same host process, which the worker cannot stage.
- **Item 6 (downed owner from body 0), new `downed-partner-unrealized.txt`: proven, `20d41b16`.** The host rode
  D-Horse; the guest's copy hid and unrealized body 1 (`partner_mount=unrealize; ...; op=clear_allactive`, probe
  `ids=0000,ffff; parts=3,0`) for 12.9 s. Every guest `downed_status` during the ride read `ready=1;armed=1;down=0;
  ...;reason=alive`, and the guest logged no `downed_control` line (the session logged `reason=player` 15x in the same
  window). The old build was not re-run; the before side is the 2026-10-23 guest log.
- **Still unproven in a game:** the box revive and posture over a network with latency (the runs are loopback); a
  boxed-first down; a bump that moves a weapon or ammo field; the predicted preload after a real relaunch.
- CTest at `3fd064a`: fast 290/290, full 305/305.

## October 28, 2026: Host fixes (two-PC test 2026-10-23 items 2, 3, 4, 6 of the host-log pass)

Branch `wp/host-fixes` from `dev/oct7-continue` `916708f`. Code and fixtures only at `e7fb9ac` (no game run then; the
hidden-worker proof is the section above). One commit per item.

- **Item 6, `08f73d6`: the local player's downed protection while body 1 is unrealized.** `Live()` required both
  bodies realized and loaded, so while `partner_mount` / the ride hides had unrealized body 1 (the host's horse ride
  on the guest, heli rides, re-realizes) every poll was `downed_control=refused; reason=player` and lethal damage
  would have been a plain game over. `downed_player::detail::LiveBodies` judges ownership from body 0 (realized,
  id 0, loaded); a realized body 1 must still be id 1, an unrealized one is absent.
- **Item 4, `2f3332b`: friendly fire on a boxed host.** The prone request cannot take a boxed body
  (`controls_pending_prone` x6, `controls_timeout_prone`), and the timeout latched `failed` for the rest of the
  mission (every poll `controls_timeout`, darts ignored with `downed_unavailable`).
  - The Lua owner sends the local `PlayerStatus.CBOX` as an optional fifth field of the `MGS_COOP_DOWNED_V1` poll and
    lock receipts; `detail::DownedPosture` accepts the box as the downed posture (no prone request; masks and the
    input cap still apply; `detail::PostureHeld` keeps the cap without stance 3).
  - `detail::ControlsTimeout`: a timeout clears the cycle without latching; the abandoned generation retires
    (`NewScene`) and the next poll with a good `Live()` re-arms. Lua releases a downed mask the healthy owner no
    longer needs (`step=abandoned`) and `accept()` takes "up under a newer generation" as an abandoned cycle on both
    copies (no grant; a same-generation stand-up still needs one).
  - `downed_control=...` lines carry `posture=` and `generation=`; a `controls_timeout_*` and the first box lock of a
    generation are logged without the 1/s limiter.
  - Scenario `tools/worker/scenarios/downed-boxed-friendly-fire.txt` (the guest's rifle downs the boxed host; locked
    after 8 s; revive; a second boxed down arms again).
- **Item 3, `f492d77`: `fire_reload_off; reason=ammo_leak; scope=process` after a box-on-box bump.**
  - `partner_ammo_leak` names the 14 registrations of the weapon/ammo vars block `vars+0x548..+0x670` (Fox StrCode32
    of the exe's registration hashes: weapons, ammoInWeapons, ammoSubInWeapons, isInitialWeapon, supportWeapons,
    items, handEquip, ammoStockIds, ammoStockCounts, five unnamed byte arrays). The hook keeps the block bytes around
    body 1's job and logs `partner_input=vars_change; verdict=..; fields=<member>[<element>]:<before>><after>`
    (32 per mission).
  - A change confined to the viewer's own item fields (items, item stock ids/counts `0xE + sub`) is benign (counted
    as `ammo_benign`); any weapon or ammo element is a leak, and an item change never masks one.
  - The play strip reads this mission's counters (`shot_leaks_mission`, `ammo_leaks_mission`, reset at MissionStart
    with the mirror's leak stop) and is cleared at MissionStart: `partner_weapon=fire_reload_off; ...; scope=mission`.
  - The support lane's own `stock_leak_mission_latched` is unchanged (mission-scoped already; it also saw the bump).
  - Scenario `tools/worker/scenarios/partner-box-bump-vars.txt` (a census of the bump: which fields move).
- **Item 2, `09ebf71`: no partner guns for MB visit 1 after the save handoff.** The host continued before the
  relaunched guest linked; the allocation preload had no manifest and is a mission-block load at the allocation only
  (vanilla calls `RequestLoadToEquipMissionBlock` in `MissionPrepare`; a mid-mission load is an unverified lifecycle),
  so the late manifest could not bind (`no_resident` x6, refused).
  - `save_handoff::PeerOnHandedSave()` (host: the token `partner_restarting` was published once in this process) and
    `equipment_stock_probe::ChoosePreloadSource`: without the peer's manifest, after a handoff, the host's own last
    sent manifest (`partner_driver::OwnManifest`) predicts the peer's first loadout. `equip_preload=...; source=
    remote|predicted|none`. The writer still binds only from the real manifest (its `manifest_late` renewal).
  - A manifest after the allocation query logs `equip_preload=late_manifest; ...; covered=<n>/<m>;
    guns=this_load|next_load` once.
  - Scenario `tools/worker/scenarios/partner-equip-late-manifest.txt` stages the late manifest (the host continues
    while the guest is still at its boot screens). **Cannot be staged:** the predicted preload itself needs a guest
    that exits after a handoff and relinks to the same host process; the worker has no relaunch control and a second
    session is a new host process.
- **Tests (failing first):** `downed_player_tests` (LiveBodies, DownedPosture/PostureHeld, ControlsTimeout),
  `world_downed_lua_check` (posture field, abandoned cycle before and after the lock on both roles),
  `world_revive_input_lua_check` (the posture field), `partner_ammo_leak_tests` (new), `partner_driver_tests` (the strip
  ends with its mission; `TestEquipLateManifest`), `partner_defer_tests` (item-only job benign, ammoInWeapons a leak),
  `equipment_stock_probe_tests` (predicted source, cover), `save_handoff_io_tests` (`HandedSave`).
- **Unproven until a game run:** that the box posture holds the downed body (and the revive) in the box; which field
  the box-on-box bump really changes (the new `vars_change` line names it; if it is a weapon/ammo field the strip
  still latches, now for the mission); the predicted preload in a real relaunch.

## October 24, 2026: 0.14.2 (launcher: GitHub update checker, auto-download, background updates, in-app self-updater)

**Package (2026-10-24):** `dist/DiamondDogsTogether-0.14.2-20261024T143841Z-fcd27151/DiamondDogsTogether-0.14.2.zip` (10,063,881 bytes), SHA-256 `891A1CD48EA5AAE25F1F94AEE65E2B45FDC575B0A15ACBD50CFA2D5004DDBDFA`, built by `tools/package.ps1 -Friend` from the clean tree at `1764136` with Release receipt `20261024-143542-13ca3e7088cd4760866eae6ea6c9eacb` (passed, 296 audited tests). `DiamondDogsTogether.exe` SHA-256 `8AAC2C38A9074168DDD118594203491FBE2ED1E2D1B85614DA24E323C1C1A723` and `dinput8.dll` `937B13ABA2C585D416F06B2339153FBFDB44519B78A9FA816C74FD5151CA2119`, both build id `0.14.2+1764136`.

Branch `dev/oct7-continue`. Adds an integrated GitHub update checker and auto-downloader to `DiamondDogsTogether.exe` (`native/launcher/core/core_update.hpp`, `core_update.cpp`, `core_model.cpp`, `launcher_app.cpp`).

- **Update check & releases query (`native/launcher/core/core_update.cpp`, `core_model.cpp`).**
  - Connects to GitHub API (`/repos/TrollTaylor/diamond-dogs-together/releases?per_page=5`) via WinHttp over HTTPS with automatic redirect handling (AWS S3 `objects.githubusercontent.com`).
  - SemVer parsing (`ParseSemVer`) and comparison (`IsVersionNewer`) ignores leading `v` prefixes and build metadata (e.g. `+c327c26`).
  - Identifies latest non-draft release asset matching `DiamondDogsTogether-*.zip`.
- **Background asynchronous download & progress.**
  - `start_update()` launches a background worker thread reporting download progress (`bytes_received` / `total_bytes`) with cancellation support.
  - Downloads release ZIP to a temporary staging file in the game directory.
- **In-app self-updater & safe replacement (`ApplyUpdateZip`).**
  - Safely extracts the package zip into the game directory. Renames running `DiamondDogsTogether.exe` to `DiamondDogsTogether.exe.old` (Windows NTFS file rename allowed while executing), replaces binaries and license files, relaunches the updated executable via `CreateProcessW`, and cleans up `.old` artifacts on the next startup (`CleanOldExecutable`).
- **Launcher GUI integration (`native/launcher/launcher_app.cpp`, `launcher_theme.cpp`).**
  - **Tools page**: "Diamond Dogs Together Updates" card with current version, release channel, manual "Check for updates" button, release title, notes, and progress bar with Cancel / Update & Restart buttons.
  - **Sidebar footer**: Version badge with subtle "Update" badge when a new version is detected.
  - **Play page**: Compact banner notification when an update is available with one-click direct update or release notes view.
- **Verification.**
  - Full launcher updater unit tests in `tests/ddt_core_tests.cpp` (`TestUpdater`: SemVer parsing, comparison, GitHub JSON parsing, mock download, in-place extract, and rename).
  - All test suites passing (`pwsh -File tools/build.ps1 -Fast`: 289/289; `build/native/DiamondDogsTogether.exe --selftest`: pass; full suite: 304/304).

## October 24, 2026: 0.14.1 (enemy synchronization: patrol & combat position/yaw sync, combat discovery, wake-up `RecoveryLife`, `Holdup`, `VANISHED`; fast developer test loop & test boot)

**Package (2026-10-24):** `dist/DiamondDogsTogether-0.14.1-20261024T111637Z-d9c55bc7/DiamondDogsTogether-0.14.1.zip` (10,035,245 bytes), SHA-256 `D2D9B2BEB086861E6B5F9E69B471D4227D156E1143A0607E3CD2C507CFE63FC9`, built by `tools/package.ps1 -Friend` from the clean tree at `92597e2` with Release receipt `20261024-111304-add17cb82ad240ca9ee8cd4bae4f7b09` (passed, 296 audited tests). `DiamondDogsTogether.exe` SHA-256 `81A1192BF216CE7EB557A3646F0571C4D64D2618C82582B2B1A2F7AA0368366D` and `dinput8.dll` `6607917C716F471635217DBD6A512C9B1BC8E321165FA3D31DBF2708D6BD6AA8`, both build id `0.14.1+92597e2`.

Branch `dev/oct7-continue`. Extends world sync (`lua/coop/world_enemy.lua`, `lua/coop/world_outcome.lua`, `lua/coop/world_phase.lua`, `lua/coop/world_place.lua`, `lua/coop/world_place_guest.lua`) so authored enemies stay synchronized in life/action state, AI discovery/combat state, and continuous patrol/combat position and facing (`yaw`).

- **Enemy life and action state sync (`lua/coop/world_enemy.lua`, `lua/coop/world_outcome.lua`).**
  - **Wake-up (`NORMAL = 0`)**: When `E.applyOne` receives `st == 0` for a soldier currently in `SLEEP` (`2`) or `FAINT` (`3`), it sends `{id = "RecoveryLife"}` and tracks readback to `0` (`E.readbacks` retries once at `+1000 ms` until `+2000 ms`). Already-awake copies (`cur == 0`) continue to log `skip_backward`.
  - **Hold-up (`HOLDUP = 1`)**: `world_outcome.lua` binds `ACTION_STATUS.HOLD_UP_STAND` and `HOLD_UP_CROWL` (plus `TppEnemy.SetDisable`). `E.observe` records `"Holdup"` (`CAUSE.Holdup = 2`, `s.holdup[e.id] = true`, `s.attrib[h] = now`) and clears `s.holdup[e.id]` on `"Conscious"` (`CAUSE.Conscious = 9`). Applying `st == 1` sets `s.holdup[e.id] = true`, clears sneak and caution routes (`SetSneakRoute` / `SetCautionRoute` `""`), and completes `ok` (`readback=1`).
  - **Disable / vanish (`VANISHED = 7`)**: Applying `st == 7` calls `TppEnemy.SetDisable(e.id)` (or `{id = "SetEnabled", enabled = false}`) and completes `ok` (`readback=7`).
- **Enemy AI discovery & combat sync (`lua/coop/world_phase.lua`, `lua/coop/world_enemy.lua`, `lua/coop/world_place.lua`, `lua/coop/world_place_guest.lua`).**
  - `E.observe` records the most recently engaged soldier per CP in `E.s.lastByCp[e.cp] = h` on local/buddy interactions. When the guest escalates a CP (`OP.REQ_PHASE`), `P.onLocal` transmits `lastByCp[h]` in `v[5]` (`soldierHash`), and the host's `P.tick` sends `{id = "SetDiscoveryPlayer"}` to `W.ent[sh].id` when `req == 3` (`ALERT`).
  - On the host, `world_place.lua` records `"Damage"` timestamps per soldier (`P.patrol.combat[h]`) and sets `flags = 1` on `kind = 3` (`correct`) updates when the soldier's CP is in `ALERT` (`localPhase == 3`) or the soldier took damage within 5 s. On the guest (`world_place_guest.lua`), `P.sample` and `P.done` trigger `{id = "SetDiscoveryPlayer"}` (`event=place_combat`) when `W.w1` is active and `c.flags % 2 == 1`.
- **Continuous authored-soldier patrol & combat position/yaw sync (`lua/coop/world_place.lua`, `lua/coop/world_place_guest.lua`).**
  - While `W.w1`, `W.paired`, `W.eff >= 2`, and `W.rosterMatch` hold, `P.hostTick` scans up to 8 entries / 2 position reads per frame from `W.entOrder` for active-CP `NORMAL` soldiers within 55 m (`d2 <= 3025000000` mm², inside native `GuestWarpReceipt`'s 60 m `kMaxRangeLimitM` gate) of the host player, announcing `kind = 3` (`correct`) with ground-adjusted `y - 800` mm, movement heading `yaw`, and `flags` on initial sample, `>= 1.5 m` movement, `3000 ms` interval, or new combat alert.
  - On the guest (`P.sample`), when `W.w1` is active, divergence `> 3.5 m` (twice) or `> 8.0 m` (once) with a `2500 ms` per-soldier cooldown queues a `kind = 3` native `soldier_warp` correction, including during `ALERT` combat.
- **Verification.**
  - Fast suite (`pwsh -File tools/build.ps1 -Fast`): 289/289 passed (7.5 s), including new unit/contract checks in `tests/world_sync_lua_check.lua` and `tests/world_align_lua_check.lua`. Full suite (`pwsh -File tools/build.ps1`): 304/304 passed (98.0 s).
  - Live hidden-worker run `f6af1a3be7154dd28b95ab23467fc39d` (`boot 30010 1448.61 337.787 1466.4 0 time=12:00 weather=sunny`, `labsetting debug_soldier_warp=1`, `goto_camp afgh_village_cp`): fast boot reached gameplay in 27.26 s / 28.76 s game clock (44.3 s / 42.2 s wall time); the host patrol scanner automatically announced active-CP soldiers (`ent=2077341861` and `ent=1084844745` / `go=1142`, `sent=13;corrected=1;refused=0`); `cp_phase nearest alert` synced `phase=3` to the guest, triggered `event=place_combat; seq=5; ent=1084844745` (`SetDiscoveryPlayer`), warped the guest's combat soldier on `seq=7` (`div_m=13.7` -> `result=applied; err_m=0.07; frames=3`), and synced `enemy sleep nearest` (`state=2; ver=2; src=outcome; result=ok; readback=2`).

## October 23, 2026: developer test loop (parallel CTest, fast/slow labels, no global CTest lock)

Branch `wp/fast-tests` from `dev/oct7-continue` `ba7bb62`. Tests and tooling only; no product code changed, no game launched.

- **Parallel CTest.** `tools/build.ps1` and the track helper `bt.ps1` run `ctest -j N` (default: half the logical
  processors; `-Jobs N` overrides). ReleaseGun build, 302 tests, this 32-thread PC: serial 265 s; `-j 16` 89-92 s
  (5 runs) and `-j 32` 89 s, all 302/302. The wall time is now `lab_scripts_tests` alone (about 90 s).
- **Labels.** Every test is `fast` or `slow` (`cmake/test_labels.cmake`). Slow: the PowerShell suites, `link_udp_tests`
  (34 s of socket timers), `ddt_core_tests` (20 s), `ddt_runtime_dat_integration` (reads the game folder) and
  `ddt_launcher_screens` (DX11). `tools/build.ps1 -Fast` / `bt.ps1 -Fast` run only `ctest -L fast` (287 tests, 5.7 s at
  `-j 16`); without `-Fast` (or `bt.ps1 -CtestRegex .`) everything runs. A new slow test goes into `MGS_SLOW_TESTS`.
- **What -j serializes.** `RESOURCE_LOCK dx11_window` for the three tests that create a DX11 device and hidden window
  (`ddt_launcher_screens`, `debug_overlay_tests`, `worker_input_fixture_tests`); `clone_fixture_bin` for
  `standing_selection_off/on_tests` (both write `clone-fixture.bin` into the build folder). The fixtures on fixed
  127.0.0.1 UDP ports (`link_udp_tests` 48220-48261, `steam_loopback_tests` 48120-48161, `partner_player_tests`
  48190/48191) also hold a per-machine named mutex (`tests/test_port_lock.hpp`), because RESOURCE_LOCK cannot order the
  suites of two checkouts. Everything else already used PID/GUID-named folders, random free ports or fake transports.
- **Verify lock.** CTest no longer takes `%TEMP%\mgs-coop-verify.lock`. It waits while the lock is held by anything but
  a CTest (owner `ctest ...`, or the older `join-fix <checkout> <time>`) or while a real `mgsvtpp.exe` (over 32 MB; the
  fixtures run small stand-ins under that name) or a `worker_supervisor.exe` runs, then registers in
  `%TEMP%\mgs-coop-ctest-readers` (`tools/VerifyLock.psm1`; `bt.ps1` inlines the same rule). Suites of different
  worktrees run at the same time. Hidden-worker runs, worker checks, packaging and installs keep taking the lock; after
  taking it, `Wait-CtestReaders` waits for suites that were already running (`tools/worker/README.md`).
  `tools/build.ps1 -NoWorkerWait` skips the wait (the `build_profile_tests` fixture). `Invoke-WorkerChecks.ps1` is
  unchanged (`-j 1`).

## October 23, 2026: developer test boot (`lab_fast_boot`, `lab_boot`, save-seed library, low graphics preset)

Branch `wp/test-boot` from `dev/oct7-continue` `ba7bb62` (`docs/test-boot.md`). Developer-only hidden-worker test environment; never active in friend/Release packages (`worker_scenario::Compiled() && worker_scenario::Active()`, `mode=lab`, `offline-v1`, copied saves; `mode=play` forces both keys off and sets `play_refused` bit `0x1000000` `kPlayRefusedLabBoot`).

- **Fast boot and boot-to-spot (`lua/coop/debug_boot.lua`, `native/include/mgscoop/lab_boot.hpp`, `native/lab_ini.hpp`).** `lab_fast_boot=1` skips the Konami/KJP/Fox logos (`TppSave.IsNewGame()` true for `init_sequence.StartPreTitleSequence`), deletes the copyright splash, erases the autosave (`Seq_Demo_ConfirmAutoSave`), login (`Seq_Demo_LogInKonamiServer`) and offline DLC popups, bypasses Press Start, and selects `OnSelectContinue` (or `OnSelectRestartHeli` when a boot target is given). `lab_boot=<30010|30020|30050> [x y z yaw] [time=HH:MM] [weather=<name>]` (or `40010|40020|40050`) sorties from the ACC on foot (`SelectNextMissionHeliStartRoute(target, 0, false)` + `OnEndMissionPreparation(DEPLOY_TIME.CURRENT)` on the host while the guest follows via `world_follow` `kind=6`), clears any queued Mother Base arrival demo (`TppDemo.SetNextMBDemo()`), sets `TppPlayer.SetInitialPosition` during `MISSION_FINALIZE` (host at the spot, guest 4 m in front facing back) so the mission-start checkpoint save is written at that spot, and applies `TppClock.SetTime` and `TppWeather.ForceRequestWeather`.
- **Light test copies and save-seed library (`tools/worker/WorkerSessionCommon.psm1`, `Save-WorkerSeed.ps1`, `Start-WorkerSession.ps1`, `Invoke-WorkerScenario.ps1`).** Lab worker sessions default to `-GraphicsQuality low` (rewrites the run's copied `TPP_GRAPHICS_CONFIG` and its integrity tag; `-Mode play` keeps `preserve`). `Save-WorkerSeed.ps1` snapshots a closed worker run's `<role>-saves\{287700\local,remote}` into `D:\mgs-coop-seeds\<name>` with `manifest.json` (SHA-256 per file and set hash per role); `Start-WorkerSession.ps1` / `Invoke-WorkerScenario.ps1` accept `-SaveSeed <name>` / `-HostSaveSeed` / `-GuestSaveSeed`. Created seeds in `D:\mgs-coop-seeds`: `quiet-recruited` (`6c755769`), `mb-ladder` (`3e5e8243`, `30050 22.61 1.80 -15.55 0`), `africa-jeep` (`8d91d358`, `30020 -656.01 1.20 877.73 62`), and `afgh-ledge` (`c2ecccb9`, `30010 828.18 338.20 1630.51 95`).
- **Hidden-worker timing comparison (`%USERPROFILE%\mgs-coop-run-archive\test-boot\`).** Baseline `boot-baseline.txt` (`634e5f77`, `-GraphicsQuality preserve`, Continue into `30010`): host gameplay at **57.2 s**, guest at **54.3 s** from process creation. Fast boot `boot-30020.txt` (`0e20af9c`, `-GraphicsQuality low`, `boot 30020 -510.73 5.09 1183.02 0 time=12:00 weather=sunny` from `lab` seed through `40010` ACC + sortie into `30020` Masa Village): host gameplay in **28.88 s** of game clock (**45.4 s** since launch, `err_m=0.00`), guest in **29.43 s** of game clock (**45.3 s** since launch, `err_m=0.00`), zero input; `host-boot-spot.bmp` and `guest-boot-spot.bmp` show both players standing face-to-face at 12:00 sunny with the Steam persona nametag. Mother Base `boot 30050 22.61 1.80 -15.55 0` (`3e5e8243`, from `quiet-recruited`) reached the red container ladder in **31.30 s** / **31.58 s** game clock (**42.4 s** / **42.9 s** since launch) with the arrival demo skipped (eliminating the previous 60 s post-gameplay wait).

## October 23, 2026: 0.14.0 (Diamond Dogs Together launcher: one exe, drag-and-drop install, Steam only; repo cleanup; story character)

**Package (2026-10-23, not installed, not released):** `dist/DiamondDogsTogether-0.14.0-20261023T215953Z-dc28f400/DiamondDogsTogether-0.14.0.zip` (9,980,622 bytes), SHA-256 `F58E248804274173766CA49BD07233067C4A5ACB1813ADF5468446637F45C717`, built by `tools/package.ps1 -Friend -CheckReceipt` from the clean tree at `54f0b9d` with Release receipt `20261023-215536-f4f26658dfb34d72bf39e1876b9b5df5` (passed, 294 audited tests). `DiamondDogsTogether.exe` SHA-256 `5E1073BC...8F23` and `dinput8.dll` `CDE30622...5520`, both build id `0.14.0+54f0b9d`. Unzipped into a scratch folder and checked: the 10 root entries, `package.json` (drop-in, 9 `files[]` rows, every hash), both programs equal to the receipt, licences intact, `source.zip` (1,283 entries), and the packaged exe's `--selftest` (13/13). Not installed in a game folder and no game launched.

Branch `wp/launcher-integ` from `dev/oct7-continue` `cd25f22` (0.13.6): merge commits of `wp/repo-cleanup` `e4ad16a`
(about 855 research / stale files deleted in place; tag `archive/research-2026-10-23` holds them), `wp/character-story`
`a3f828b` and `wp/launcher-core` `cc97405` (which contains `wp/launcher-dat` `37eff7c` and `wp/launcher-gui` `705ef1d`),
in that order, with no conflicts, then the integration commit. No rebase. Full CTest after each merge: 296/296, 296/296,
302/302 (`-Configure`; `ddt_runtime_dat_integration` built the runtime archive from `D:/Games/steamapps/common/MGS_TPP`
and matched the pin). `generate_partner_contract.py --check` 800 code sites / 204 vtable slots (unchanged).

**What changed for players.**
- One program, `DiamondDogsTogether.exe` (Win32 + DX11 + Dear ImGui, no console): first-run setup (game check, real-save
  backup, co-op save copy, the runtime archive built from the player's own game files), Play (Host is Snake; Join picks
  Story / Ocelot / DD soldier m/f / avatar; a code or a Steam invite), In game status, Tools (Share save, Collect logs,
  Uninstall, About). Steam online only: no LAN/VPN codes, UPnP, firewall helpers or local test for players.
- Install is drag and drop: the zip's root goes into the game folder (`DiamondDogsTogether.exe`, `dinput8.dll`,
  `mgs-coop\` with the licences, `THIRD_PARTY_NOTICES.md`, `README.txt`, `package.json` and `source.zip`). The zip
  holds no `.bat` / `.ps1` and never the runtime `.dat`. `tools/package.ps1 -Friend` writes
  `dist/DiamondDogsTogether-<version>-<stamp>/DiamondDogsTogether-<version>.zip` plus the unpacked `package/`.
- The PowerShell player tooling is retired: `tools/friend/{Install-Coop,Play-Coop,Share-Save,Collect-Logs,Uninstall-Coop,
  Start-Coop}.ps1`, `FRIEND-GUIDE.txt`, `RUNTIME-ARCHIVE.txt` and the root `Play-Coop.bat` are removed (history and the
  0.13.6 release keep them). The C++ core (`native/launcher/core`, `tests/ddt_core_tests.cpp`) is their port.
- In-game texts (`native/coop_reason_text.cpp`, `tests/fixtures/link-reasons.json`, `tools/CoopPlay.psm1`) use the
  launcher's words ("Diamond Dogs Together", "Tools > Share save", "Tools > Collect logs"), with no Play-Coop.bat,
  Share-Save.bat or LAN/VPN code. `character=story` (Quiet while she is on the team, else Ocelot).

**Release and developer tooling.**
- `tools/worker/Invoke-WorkerChecks.ps1 -Profile Release` now also audits `ddt_archive_tests`, `ddt_core_tests`,
  `launcher_ui_tests` and `DiamondDogsTogether.exe --selftest`, and the receipt hashes and keeps
  `DiamondDogsTogether.exe` next to `dinput8.dll` (`artifacts.launcher_sha256`). The source snapshot covers the
  launcher's embedded assets (`.bin`, `.ico`, `.png`, `.manifest`). `tools/package.ps1 -Friend` refuses a receipt
  without the launcher, a changed launcher and a `-LauncherPath` that differs from the receipt.
- `package.json` (drop-in layout): product `mgs-coop-local-package`, version = the exe's X.Y.Z, the pinned game hashes,
  one `files[]` row per file of the zip (exactly one `dinput8.dll` row), `runtime_archive` target / SHA-256 / bytes.
  `tools/package-install.ps1` (lab, worker, local test) accepts it: it reads `mgs-coop/package.json` and installs only
  the loader and its licences.
- The developer two-window test moved to `tools/dev/` (`Play-LocalCoop.ps1`, `LocalCoopSession.psm1`,
  `LocalCoopWindows.psm1`, `FriendCommon.psm1`, new `Start-LocalCoop.ps1`); the root `Play-Offline-Coop.bat` starts it
  with the newest `dist/DiamondDogsTogether-*/package` (or `-PackageDirectory`). The host plays Snake, the guest Quiet;
  a drag-and-drop copy (the package's loader, no `probe-install.json`) is accepted, an owned older loader is upgraded.
- Developer flags (not for players): `DiamondDogsTogether.exe --write-session host|join [--code <code>]
  [--character <c>] [--game-dir <dir>]` runs the setup when needed and writes the session files exactly as Host / Join
  (same checks and order), but starts neither the helper nor Steam and leaves the clipboard alone; it prints
  `session=` / `role=` / `code=` (host). `--end-session [--game-dir <dir>]` switches co-op off (never under a running
  game). For the release gate's hidden-worker run.
- `tests/friend_package_tests.ps1` now covers the drop-in package, `tools/CoopPlay.psm1`, `tools/dev/FriendCommon.psm1`
  (cleanup, import core, save handoff) and the local test (1,154 checks, 38 s); the tests of the removed scripts are gone.
- Version 0.14.0 (CMakeLists, package.ps1, probe.ps1; the package-install / set-session-mode / Play-LocalCoop
  allow-lists keep 0.13.6 and add 0.14.0). The exe and the dll report `0.14.0+<commit>`.

**Known limits and what is untested (0.14.0).** No game launch in this integration. Not yet run: the hidden-worker gate
that starts both copies on exe-written session files (`--write-session`), a drag-and-drop install on a clean game
folder outside the fixtures, the GUI on a real install (setup against real Steam userdata, Host / Join through Steam,
the watchdog relaunch after a save handoff), the adoption of an existing 0.13.6 PowerShell install by the setup, and a
real two-PC session. The avatar may still appear as Snake to the partner (researched in `wp/character-story`, not
built). Both players need 0.14.0 (the build id changes).

## October 23, 2026: 0.13.6 (fixes from the first two-PC Steam session: guns and the box after a relaunch, long shared rides, a Fultoned vehicle, joining with different saves, sea / ledge / carry falls; lab levers for the HUD hint, truck, ride-hide and gesture proofs)

**Package and install (2026-10-23):** `mgs-coop-friend-0.13.6-20261023T181349Z-8a864a81.zip` (9,643,361 bytes), SHA-256 `ffd7f05ff1f62641ace423b3f3d797ec3ae0918e77674f78d21733e9e0452e71`, built by `tools/package.ps1 -Friend -CheckReceipt` from the clean tree at f9c22b0 with Release receipt `20261023-181036-183f3c8c3e8c463c986568161a5b77ed` (passed). Installed in both local copies (host Install-Coop -Role Host -Character snake; guest install.ps1 -NoInitialMode); both dinput8.dll SHA-256 00E2FA1E...99DA, probe-install.json version 0.13.6, 17 protected files unchanged, no script-observe.enabled left in the guest copy. Hidden-run proofs for this release: guns after a simulated relaunch b5dd1111 (play), long ride 197fc3c6 / 80a324d7 (play), join 247ae49a, c186abfb, e5f15c7e, 640abf2f, 5e44cb65 (play), sea death e9e5301b / b8ab039e (play), put-down 4078bbe7 (play). The 0.13.5 regression set was not re-run on this build.

Branch `wp/integ10` from `dev/oct7-continue` `0a2821d` (0.13.5 released at `cc973a8`, plus the README rewrite): merge
commits of the five online fixes from the first real two-PC Steam session (2026-10-22, 0.13.5; `diag-*.md` of that
session), in this order, `wp/online-net-epoch` `55b5aea`, `wp/online-ride-robust` `636d706`, `wp/online-vehicle-gone`
`ab64ccb`, `wp/online-handoff-join` `6437c6a`, `wp/online-body-safety` `de62dd2`, then the reviewed 0.13.6 feature
branches `wp/ladder-hint` `9906fee` (G1), `wp/truck-helper` `589b4a4` (G4), `wp/ride-unrealize` `1f2b90d` (G5) and
`wp/sign-knock` `c11bb79` (G6). Conflicts resolved by union (the three `-LabSetting` regex copies stay identical, key
lists, CMake test lists and `Get-WorkerTests` keep every member), the contract header regenerated inside each merge,
then the version commit. No rebase. Deferred, not included: `wp/seat-aim`, `wp/crack-discovery`.
**Not run in the game and not packaged:** no game launch and no hidden-worker run in this integration, so every runtime
check in the subsections below is still open. The build id changes and the equip manifest / caps now carry the
receiver's link epoch (a 0.13.5 receiver still drops a 0.13.6 sender's manifest when the counters differ), so both
players need 0.13.6. Full CTest 296/296 (`-Configure`, after the reboot with a working DX11 device, so
`debug_overlay_tests` counts too; 0.13.5 had 289, plus `world_vehicle_lua_check`, `partner_body_safety_tests`,
`log_budget_tests`, `partner_ui_filter_tests`, `vehicle_door_tests`, `partner_gesture_lever_tests` and
`partner_gesture_lever_fire_tests`; the five online merges alone: 292/292). Contract 800 code sites / 204 vtable slots
(0.13.5: 743 / 193; online-ride-robust +4 sites, G1 +23 / +7, G4 +7 sites, G6 +23 / +4; the other branches add no game
address); equip writer pins 118 SHA-256 gates (unchanged).

**Defaults.** The online fixes are bug fixes, **on by default with no key** (net-epoch's link-session binding and the
equip resend at every MissionStart, ride-robust's slot follow / visible fallback / temporary-refusal retry / net-quality
rebase after rides, handoff-join's title hold, messages and restart notice, body-safety's sea / ledge / put-down /
seated-aim guards and log caps). Every new key is **default 0 / off in lab and play** until hidden-worker frames prove
it (the runtime phase flips defaults):
- `world_vehicle_gone=0|1` (online-vehicle-gone: the host-authoritative VEHICLE_GONE world op; a named 1 runs in lab and
  play; docs/world-sync-protocol.md section 16; a `world_*` key like the other world-sync keys: no `_given` flag and not
  in the native-puppet.md table);
- `link_session_skew=0..999` (online-net-epoch: the lab / worker reproduction knob of a relaunched peer, a link
  impairment key; play refuses it);
- `partner_native_ui_filter=0|1` (G1), `debug_vehicle_door=0|1` (G4), `partner_native_unrealize_all=0|1` (G5) and
  `partner_gesture_lever=0|1` (G6): lab only with `_given`, play forces a named 1 off.

**Integration decisions.**
- **One net-quality reset path after rides.** online-ride-robust (rebase after a seat episode or a ride hide, default
  on) and G5 (a per-tick `QualityIn::excluded` feed, behind its key) fixed the same symptom; the merge keeps only the
  ride-robust rebase and G5's unrealize, and with the G5 key the rebase logs `partner_ride=net_rebased` (details under
  "partner_native_unrealize_all" below).
- **Lab vehicle levers keep distinct names** in `lua/coop/debug_vehicle.lua`: `vehicle_remove nearest [sync|fulton]` /
  `vehicle_gone_status` (online-vehicle-gone, refused outside `mode=lab`) and `goto_vehicle truck`,
  `goto_partner_vehicle`, `vehicle_door` (G4, the native receipt refuses outside the lab).
- Version 0.13.6 (CMakeLists, package.ps1, probe.ps1, the Play-Coop / Play-LocalCoop / package-install /
  set-session-mode allow-lists, which keep 0.13.5, FRIEND-GUIDE).

**Known limits (0.13.6):** every fix and lever is unit-tested only. Until a 0.13.6 pair is installed, the 0.13.5
workaround stands (after any guest relaunch both quit, the host starts, then the friend joins). Open user decisions from
the branch reports: the Fulton fallback look (the partner drawn standing at seat height while the viewer lacks the
vehicle) and the slot-follow parameters (1 Hz, 25 m); `world_vehicle_gone`'s Despawn (no balloon, the vehicle stays only
in the Fultoning save) versus a Seize with the Fulton option, the 5 m position match, a destroyed vehicle removed outright
on the other copy; the 10 s settle bind and the 60 s title hold of a pending handoff (F9 is the escape); the sea-death
rule for body parts 0..11 only and the narrower 2 m put-down guard. Pre-existing and unchanged: the Ctrl+W walk run
gait, the teardown AV `rva=0x21e72` at WM_CLOSE.

| Change | Branch | Key (default) | Scenarios to run (hidden-run phase) |
| --- | --- | --- | --- |
| Guns, box and caps survive a guest relaunch (link epoch) | `wp/online-net-epoch` | none (fix); `link_session_skew` reproduction knob (0) | `partner-equip-relaunch.txt` |
| Long shared rides, stuck DM flag, Fulton fallback, slot follow, rebase after rides | `wp/online-ride-robust` | none (fix) | `coop-vehicle-longride.txt` (lab and `-Mode play -WanSim bad`) |
| Host-authoritative VEHICLE_GONE | `wp/online-vehicle-gone` | `world_vehicle_gone` (0) | `coop-vehicle-gone.txt`, `coop-vehicle-gone-mb.txt` |
| Joining with different saves | `wp/online-handoff-join` | none (fix) | `handoff-double-press.txt`; rerun `handoff-stage.txt`, `handoff-restart.txt`, `handoff-after-restart.txt`, `travel-lock.txt` |
| Sea death, ledge / carry falls, seated aim latch, log caps | `wp/online-body-safety` | none (fix) | `body-safety-sea-death.txt`, `body-safety-sea-death-host.txt`, `body-safety-sea-death-downed.txt`, `body-safety-ledge.txt`, `body-safety-putdown-edge.txt`, `coop-vehicle-seated-aim.txt` |
| G1 body 1's HUD hints never reach the viewer's HUD | `wp/ladder-hint` | `partner_native_ui_filter` (0) | `partner-ui-filter-ladder-mb.txt` |
| G4 truck test levers for the passenger-mode truck fallback | `wp/truck-helper` | `debug_vehicle_door` (0) | `coop-vehicle-passenger.txt` (truck part) |
| G5 the other ride hides unrealize body 1 | `wp/ride-unrealize` | `partner_native_unrealize_all` (0) | `partner-ride-unrealize.txt`, `partner-ride-unrealize-seat.txt`, `partner-ride-unrealize-heli.txt` |
| G6 lab lever for the host's own knock and hand signs | `wp/sign-knock` | `partner_gesture_lever` (0) | `partner-upper-gestures.txt` |

### online-net-epoch

Branch `wp/online-net-epoch` from `dev/oct7-continue` `cc973a8`. Partner guns, the partner's cardboard box and the
capability exchange stopped working for the rest of the evening once the guest relaunched (quit, then the save
handoff's automatic relaunch) while the host kept running. Unit-tested and CTest green; no game launch and no hidden
run yet (the runtime checks below are for the hidden-run phase).

- **Root cause (P1/P2/P3):** the equipment manifest and the capability message carried the SENDER's process-local
  link-session counter, and the receiver dropped them unless it equalled its OWN counter (`stale_epoch`, counted
  silently). After a guest relaunch the counters differ for good (that evening: host session 3, guest 1), so every
  manifest and caps message was dropped in both directions: `equip_preload=refused; reason=no_manifest`,
  `equip_writer=refused; reason=no_manifest`, `remote_base=0,0,0`, FIRE/RELOAD stripped, `remote=unknown/none`, and
  no `partner_cbox=remote` (the box rides on the manifest). The lab never saw it (both copies start at session 1).
- **Fix 1 (default, no key):** the receiver binds what it stores to its own link session, the one the message was
  drained in (`partner_equip_wire::Store::link_epoch`, `partner_caps_wire::Store::link_epoch` / `Live`), and never
  compares the sender's number with its own. The duplicate and box-only checks are keyed on `link_epoch` (a guest
  relaunched on the same save sends the identical revision 1 again: accepted in the new session, not a duplicate);
  MissionStart seeds `link_epoch` from the session cache; the planning manifest (`partner_driver::RemoteManifest`, the
  observer's `EquipManifest` for the preload and the writer) is the current link session's only. Freshness stays
  with the transport (reliable windows reset per link session; the drain yields only the current session's
  messages).
- **Fix 2:** refused channel-2 messages log once per change of (link session, reason), at most 32 per process:
  `partner_equip=refused; reason=..; rev=..; sender_epoch=..; link_epoch=..; size=..; magic=..` and
  `partner_caps=refused; ...`. `partner_equip=manifest` and `partner_caps=rx` lines add `sender_epoch` /
  `link_epoch`. state.json `native.equip` adds `rx_sender_epoch`, `rx_link_epoch`, `rx_received`, `rx_invalid`,
  `rx_stale`, `rx_duplicates`, `rx_refused`; `native.net.caps` adds `stale`, `link_epoch`.
- **Fix 3:** `no_manifest` is no longer a permanent writer refusal: the slot still waits 300 observations and logs
  the refusal once, a body re-sync keeps it, and a manifest known later renews the slot in the same body gen
  (`equip_writer=manifest_late; reason=manifest`). The preload's `no_manifest` never consumed the attempt; with
  Fix 1 it is evaluated against the right session. Tested through its pure helpers only (`ManifestWaitReason`,
  `RenewManifestWait`): the `ObserveGeneral` renewal (plan-wait reset, order recompute, the `manifest_late` line)
  and `ResyncBody` keeping `no_manifest` read live game memory and are not run by a unit test, and the relaunch
  scenario delivers the manifest before body 1 appears, so it never reaches the late path. A `manifest_late` line
  in a hidden run or in play is its first runtime evidence.
- **Fix 4:** the sender resends its manifest once per MissionStart (the receiver's dedupe absorbs the repeat); a link
  (re)connect is a new session on both copies and resends as before.
- **Reproduction knob (lab / worker only):** lab.ini `link_session_skew=0..999` raises this copy's link-session
  counter at link start (first session = skew+1; `link=session_skew; skew=..; first_session=..` logged). A link
  impairment: the lab and the hidden worker's play rehearsal only; a friend's play refuses it
  (`kPlayRefusedImpairment`, forced 0). Allow-listed as `-LabSetting` / scenario `labsetting` key
  (`link_session_skew=[0-9]{1,3}`) and a LabCommon key; one range everywhere (the native parser and LabCommon
  refuse 1000 like the allow-lists). `lab_ini_check` prints it next to the `net_*` impairments.
- **Scenario:** `tools/worker/scenarios/partner-equip-relaunch.txt` (play mode, `labsetting host
  link_session_skew=2`: host session 3, guest 1): manifests on both sides with `sender_epoch`/`link_epoch`,
  `remote=native/ok`, `equip_writer=handle_bound` (or `handle_queued` then `observation_stopped;
  reason=model_wait_limit`, the same alternative partner-native-weapons.txt accepts), guest captures of the host's
  partner body with its gun, then a host box toggle with `partner_cbox=remote; ...; active=1; apply=native` on the
  guest.
- **Tests (R0; they fail on 0.13.5):** `partner_equip_wire_tests` (sender epoch 1 under link session 3 accepted and
  stamped; the same bytes a duplicate in session 3 but accepted again in session 4; the host's box toggle across the
  mismatch), `partner_native_tests` (caps epoch 1 live in session 3 only, a new session restarts the sequence),
  `partner_driver_tests` `TestEquipCrossProcessSession` (store, cache and MissionStart seeding with the receiver's
  session, the relaunch on the same save, caps, the refusal line once, the MissionStart resend, the box edge and
  `remote=native` on a guest at session 1 receiving epoch 3), `equipment_stock_writer_tests` (`no_manifest` not
  permanent, `RenewManifestWait`), `link_session_tests` (`SessionSkew`: host session 3, guest 1),
  `lab_config_tests` (lab keeps the skew, play refuses it, the rehearsal keeps it), `worker_debug_tests` (the new
  JSON fields), `worker_session_tests` (the allow-lists and LabCommon key).
- **Review fixes:** `TestEquipCrossProcessSession` also drains through the game's authenticated origin hook
  (`drain_reliable_origin`, as `PartnerDrainReliableOrigin`): `link_epoch` is the origin's session (3, then 5 while
  the `link_session` hook still says 3), never the sender's epoch 1, for the cache, `RemoteManifest` and caps
  `Live`; and pins `partner_caps=refused; reason=no_link|older; rev=..; sender_epoch=..; link_epoch=..; size=16` once
  per (link session, reason). `lab_scripts_tests` checks that a LabCommon-written `link_session_skew=2` parses
  natively as 2; `friend_package_tests` checks that Play-Coop never writes it and that play refuses a named one
  (`play_refused=0x4000`, forced 0).
- **Workaround for 0.13.5 until this ships:** after any guest relaunch, both players quit; the host starts, then the
  friend joins (restarting only the host is not enough).

### online-ride-robust

Branch `wp/online-ride-robust` from `dev/oct7-continue` `cc973a8`. Shared vehicle rides (diag-other #1, #2, #9; details
in docs/anim-round1-seat.md "0.13.6: long rides, a missing vehicle, temporary refusals"). Bug fixes, on by default, no
new key; unit-tested only (no game launch in this phase).
- **The partner no longer vanishes ~340 m into a shared ride.** Read-only RE: the realize policy `0x121B5E0` tests
  body 1's slot `(*(S+0x20))[1]` (`0x14121B8B7` `shl rdi,4` / `0x14121B8BE` `add rdi,[r9+0x20]`, `0x14121B8C2` /
  `0x14121B8CC` the z / x loads, `0x14121B92C` the area test), not the body or the record; the seated DM moved only the
  body. While seated the slot now follows the body (at most 1 Hz, past 25 m of drift: `partner_ride_seat=slot_follow`,
  the exit line's `follows=N`).
- **Recovery if it happens anyway:** no stop is sent to an unrealized body (`exit; ...; reason=unrealized; via=gone`),
  so `g+0x388` bit 8 (`RequestToStopDirectMotion` `0x9DB4C0`, cleared only by DM End) no longer sticks for minutes;
  after an `exit_timeout` or a gone body our own stale request bits are cleared (`partner_player::ClearDmBits`: request
  byte 0 == 1, Stop / GoNext only without the viewer's own DM; `partner_ride_seat=stale_cleared`).
- **Temporary refusals (`pending`, `dm_read`, `release`, `host_dm`) are retried instead of latching a hide** for the
  rest of the ride: the refused episode settles and the body re-seats through the re-show flow. A request of ours still
  pending while nothing plays on either body is cleared by the arm (after 1 s; `stale_cleared; ...; when=arm`). The
  same (vehicle, seat) refused 3 times in a row without a confirmed seat (`kSeatTransientTries`) latches as in 0.13.5
  (`partner_ride_seat=retries; ...; tries=3; latch=1`), so a refusal that lasts the whole ride (a `dm_read` failure)
  hides once instead of blinking body 1 every 3-4 s.
- **A vehicle the viewer's copy lacks (Fultoned there) or has unrealized no longer hides its driver** (Mother Base, seat
  by id `local_read` / `unrealized`, at the start or at the id write): the new `SeatPhase::fallback` keeps body 1
  realized and Warps it to the remote's streamed pose every tick (`partner_ride_seat=fallback; ...; show=warp`), re-checks
  every 2 s and, once the vehicle exists here, ends at once and seats it (`fallback_end; next=release`). Only for a
  missing vehicle: a vehicle this copy has (a `pending` / `host_dm` arm, perhaps the jeep the viewer drives) is never
  Warped into, and the Warp never runs while the viewer occupies that vehicle id here.
- **Known limit (tracked):** in that fallback the per-tick Warp holds body 1's last standing pose, so the partner is
  drawn standing, floating at the seat height, until the vehicle exists again or the ride ends. A seated or crouch idle
  needs a DM clip, which a per-tick Warp ends; a slot-placed variant with a clip is unproven and left for later. The
  vehicle itself is still not synced (the `VEHICLE_GONE` world op is a separate branch).
- **Known limit (tracked; lifted for a vehicle absent on the viewer's copy, `reason=absent`, by the Africa fix of
  2026-10-28, see that section): free roam is not covered by the fallback.** In 30010 / 30020 the seat plays on the vehicle's
  authored locator name (the proven named path), and the by-id gate's `local_read` / `unrealized` there is only logged
  (`byid; ...; offer=0; reason=local_read`): a vehicle Fultoned on the viewer's copy only (Lua's `vehicleName` still
  resolves its locator) most likely ends in `far` / `unconfirmed` / `lua_refused` and hides the driver as in 0.13.5.
  Not switched to the fallback because `local_read` also covers any by-id read-chain refusal, which would replace a
  working named seat by a standing Warp; no free-roam log of the case exists yet. Left to the `VEHICLE_GONE` branch.
- **Shorter clip fallback after every ride:** a seat settle or the end of a ride hide restarts the native link-quality
  window (`partner_native_net::Rebase`): the ride is no longer one arrival gap, and the good state is kept, so a link
  that was good before the ride needs no fresh 2 s quality hold. Native comes back after the 2 s settle alone (the
  driver test: 2.1 s after the settle, against 6.1 s in 0.13.5 and 4.1 s with a rebase that restarted the good state;
  the logs showed 6 s: `net_quality` ~2 s, then `good_ms=4004`). A late first frame after the ride still restarts it.
  A leave the link itself caused is not rebased (a seat exit for `lease` / `ride_stale`, a ride hide whose reason was
  `ride_stale`): that gap is real and the 2 s quality hold applies after it, as in 0.13.5.
- Review fixes (second round): the fallback is limited to the vehicle-absent reasons (it also ran after a `pending` /
  `host_dm` arm refusal and Warped body 1 into a vehicle that exists here every tick); `Rebase` keeps `good` /
  `good_since_ms`, and a link-caused leave is not rebased; the transient retry cap; `coop-vehicle-longride.txt` clears
  enemies with radius 100 (200 is refused as `radius_args`) and checks "still seated at the end" on the exit line
  (`confirmed=1; ...; follows=[1-9][0-9]`; the seated status waitlog matched from the boarding); the free-roam limit is
  stated; the realize-policy citation names `0x14121B8BE` for the `add`.
- Contract: 4 new read-only pins (747 code sites / 193 vtable slots; `--check` passes). Tests: `partner_driver_tests`
  `TestRideRobust` (vanish and re-seat; exit_timeout clear; pending / host_dm retried without a fallback or a Warp, the
  third refusal latching, ours cleared by the arm; dm_read for a whole ride; the Fultoned jeep's fallback and re-seat;
  the viewer-in Warp guard; a Mother Base target refusal not latched; slot follow; the rebase; a lease exit and its
  `ride_stale` hide keep the gap while a heli hide's end rebases; native back within the settle against a control),
  `partner_player_tests` ClearDmBits, `partner_native_tests` Rebase (good state kept, a late
  first frame). Each new block fails on 0.13.5, on the first round of this branch, or without the line it covers.
  Hidden repro for the next phase: `tools/worker/scenarios/coop-vehicle-longride.txt` (lab and `-Mode play -WanSim bad`).
- **Hidden runs (2026-10-23, `wp/integ10` `1691253`, research build, gun stage, lab seed; every run Closed,
  BaselineRestored, RealSavesUnchanged, no visible fallback):** the first `coop-vehicle-longride.txt` (`a198590d`)
  drove ~20 m into the plaza's north wall, below the 25 m slot-follow threshold. Route discovery (scratch probes,
  `939c0daf` .. `30775595`) found **no open-loop 400 m route from `veh_lv_0000`**: east of the plaza a convoy truck
  reaches the checkpoint with the jeep and blocks the road in 5 of 6 runs (the farthest drive around it, 134 m),
  boulders and walls close the other sides, and the real seed's Mother Base sortie jeep faces the sea (the drive
  fell in, `74d91896`). The scenario now turns right out of the alley and drives west along the street
  (~50-80 m, to the arcade). **Proven live on that route**, lab `197fc3c6` and play `-Mode play -WanSim bad`
  `80a324d7` (guest `rtt_ms=176`, `policy=online`), all expectations met: `partner_ride_seat=confirmed` on both
  copies (host seat 2, guest seat 0), `slot_follow` on both (`drift_mm` 28-34 m), status `phase=seated;
  confirmed=1` all along, the exit lines `confirmed=1; ...; follows=2|3`, and none of `range_rearm` (except the one
  at the 530 m `goto_vehicle` warp before the boarding), `reason=unrealized`, `pending` refusal, `unavailable`,
  `partner_ride=hide; reason=ride`, `g388=0x08`, `partner_realized=0`, `fallback`, `viewer_changed`. After the
  get-off `drive=clips; reason=seat` turns into `drive=native; reason=ok` 2.0 s later with `good_ms` carried over and
  no `reason=net_quality` in either run (diag #9 fixed; the HUD reads `native; gun matched` again on both copies in
  play). Frames (`long-01`..`long-05`, `long-end`, checked by eye): the host at the wheel on the guest's screen and
  the guest in back seat 2 on the host's screen, seated and visible along the drive and at the end.
  **Still unproven live:** the ~340 m unrealize prevention itself (no route reaches it; unit-tested only), the
  unrealized-body recovery, the Fulton fallback (`fallback; ...; show=warp`, needs Mother Base by id) and the
  temporary-refusal retry.

### online-vehicle-gone

Branch `wp/online-vehicle-gone` from `dev/oct7-continue` `cc973a8`. Vehicles were not world-synced: the guest
Fultoned the Mother Base sortie jeep 25603 (`obs; m=FultonInfo; ent=0; id=25603; att=local`), the host drove HIS copy
of it, the guest's seat-by-id read failed (`partner_ride_seat=byid; ...; reason=local_read`), the host was hidden for
~25 s and drove into the sea (diag-other #2; the visible-fallback half is online-ride-robust's). Unit-tested;
full CTest 289/290 (`-Configure`): the only failure is `debug_overlay_tests` (no hardware DX11 device on this machine,
unrelated; it fails the same way in other worktrees); `generate_partner_contract.py --check` 743 / 193. No game
launch and no hidden run yet.

- **New world ops** (docs/world-sync-protocol.md section 16): `VEHICLE_GONE` 0x2E (H->G, world queue, reliable,
  level 3, lab and play: seq, TppVehicle2 id 25600..26111, cause 1 fulton / 2 destroyed / 3 lab, actor, rid, type,
  xyz mm) and the guest's request `REQ_VEHICLE_GONE` 0x2F (answered natively with RECEIVED; REJECT 5 / 10 as every
  request). A new mechanism that removes vehicles from a copy, so it is **gated**: lab.ini `world_vehicle_gone=0|1`
  (lab and play, no play refusal, **default 0** until hidden-run frames), CFG r[15] for Lua. Both copies need it.
- **Lua** `lua/coop/world_vehicle.lua` (new chunk, spliced last): `W.observe` now hands ids no soldier map names to
  it (`pcall`, it can never fail world sync). The host announces a vehicle its copy lost (Fulton / FultonInfo / Dead /
  VehicleBroken, then read back gone within 30 s: the pool slot released, or `IsAlive` false: at once for a wreck,
  after 10 s for a Fulton; FultonFailed cancels); the guest requests only its own losses (its FultonInfo or Dead, or
  VehicleBroken within 10 s of its own Damage); the host removes its copy and announces it with the rid and the
  guest's id. The receiver removes a live vehicle within 40 m of the reported pose that is the nearest of its type
  there (or the one vehicle of that type within 5 m of it: pool indices differ between copies; a wreck or an id it
  lost itself is already gone) **only while linked and while nobody rides it**: never the local player's vehicle, an
  occupied rider[] seat (back-seat passenger, soldier) or a vehicle the partner drives (RIDE_FAST, latched 10 s past
  the last datagram until its exit RIDE_BODY); held up to 120 s otherwise (the host then answers REJECT 9). The
  viewer's own player is never moved or dismounted.
- **Removal (RE, read only):** the TppVehicle2 class `Despawn` (`0xB30AF0`) by the authored locator (the retail
  `TppEnemy.DespawnVehicle` form) or, at Mother Base only, for an unnamed sortie / supply vehicle by `index`
  (`ParseLuaSpawnIndex` `0xB304E0`; index 0, the relief vehicle, is an engine error: refused as `index0`). Despawn
  releases the pool instance itself (manager `+0x1C8` vtable+8, `VehicleCommunicationInfo::Clear`); for a named
  record it also calls `GameObjectImpl::Kill` `0xB2F850` on the old name, and only the locator form writes the name
  back, so the index form would erase a named vehicle's binding (elsewhere refused: `no_locator`). No balloon, no
  resource on the receiver. Readback `IsAlive` (class dispatcher `TppVehicle2_LuaCommands` `0xB305B0`: false when
  the pool bit is clear, nothing past the pool; instance dispatcher `0xB2B710`: `record+0x70 & 0x800004` clear; a
  wreck still answers `GetPosition`). The stock `Seize` (options Fulton / FadeOut / Instant) would account a
  resource: not used for removal (the lab lever's `fulton` form uses it on purpose). No new native game address
  (contract unchanged, 743 / 193).
- **Lab lever:** `debug vehicle_remove nearest [sync|fulton]` (nearest free vehicle within 60 m; without `sync` this
  copy only, the session's divergence; with `sync` a cause-3 loss through the world op, which skips the detection;
  with `fulton` the engine's own vehicle Fulton, `Seize {options={"Fulton"}}`, watched like the local player's
  FultonInfo so the real readback decides), refused natively outside the
  one-PC lab (`vehicle_remove=refused;reason=lab_only`) and in Lua outside mode=lab; read-only
  `debug vehicle_gone_status`. Worker allow-lists, LabCommon and `lab_ini_check` know `world_vehicle_gone`.
- **Tests (fail before, pass after):** `world_wire_tests` VehicleGone (neither code existed: 53 ops), `world_receipt_tests`
  (CFG r[15], 15 values), `lab_config_tests` (default off, lab and play), new `world_vehicle_lua_check` (111 checks;
  fails without the module and against the 0.13.5 `world_sync.lua`: no observer hook, no r[15]; also in the worker
  gate `Get-WorkerTests`),
  `debug_vehicle_lua_check` (the levers route, strict arguments), `worker_session_tests`, `lab_scripts_tests`
  (LabCommon writes it, the native parser reads it).
- **Scenarios** (hidden-run phase): `tools/worker/scenarios/coop-vehicle-gone.txt` (`-SaveSeed lab`, Wakh Sind:
  A the guest's synced loss of jeep J1 reaches the host, B the divergence: only the guest loses J2 and the host
  drives his copy (the guest's `reason=local_read` and hide lines are waited for), C the guest seated in J3 while the
  host loses it: held until the get-off, D the engine's own Fulton of J4 on the guest's copy through the detection
  path: `step=watch` -> `step=lost` -> `step=req`, never `watch_drop;reason=still_alive`) and
  `coop-vehicle-gone-mb.txt` (`-SaveSeed real`, the Mother Base sortie jeep: Despawn by index).
- **Gate before `world_vehicle_gone` may default on (or get a Play-Coop option):** the hidden runs' frames, part D
  (the engine's own Fulton is lost by the readback, not `watch_drop;reason=still_alive`) and a player's real vehicle
  Fulton in a manual check (`step=watch` -> `settle`/`lost` -> `req` on the guest). Until then it is a lab /
  hidden-run key: two-PC play keeps the 0.13.5 behaviour (docs/two-pc-play.md).
- **Hidden run `075c1ea4` (2026-10-23, `wp/integ10` `1691253`, research build, gun stage, lab seed; Closed,
  BaselineRestored, RealSavesUnchanged, no visible fallback; no `step=error`, fault, `viewer_changed` or AV):**
  A proven: the guest's synced loss of J1 25608 (`req` -> host `rx_req` -> `applied; via=locator; src=req; ok=1` ->
  `tx` -> guest `req_done; result=gone`); frames `gone-a-before` / `gone-a-after`: the jeep on both screens, then
  gone on both. D proven: the engine's own Fulton of J4 25617 on the guest's copy (`watch; m=Seize; att=lever`, the
  stock `obs; m=Fulton` and `m=FultonInfo; att=local`, `lost` 3.3 s later without a `settle`, never `watch_drop`) ->
  host `applied; src=req` -> `req_done`; frames `fulton-before` / balloon / `fulton-after`: gone on both screens. B
  reproduced the divergence as designed (J2 25615 only on the host's copy; the guest's `byid ... reason=local_read`,
  then `refused; reason=unconfirmed`, `unavailable; block=unconfirmed`, `partner_ride=hide; reason=ride`: the host is
  hidden on the guest's screen while he drives, the free-roam limit of online-ride-robust above). C did not run:
  the guest's `goto_vehicle jeep` left it on a slope away from J3 (`vehicle_not_riding`), so the receiver applied the
  removal at once; the never-yank-the-local-rider hold is unproven live. Scenario nits: the B waitlog
  `partner_status=local; st=0x400; names=on_vehicle; vehicle=` misses the `ride=2; ` field (the host did drive J2).
  **Default unchanged (0):** C and the manual real-Fulton check of the gate above are still open.
- **Limits:** a destroyed vehicle disappears on the other copy (no wreck); the relief vehicle (index 0) is never
  removed; outside Mother Base an unnamed vehicle (a supply drop) or one outside the `veh_lv` / `veh_trc` / `veh_wav`
  families is not removed (`no_locator`); two vehicles of a type within 5 m of the spot are left alone (`ambiguous`);
  no catch-up after a reconnect beyond the host's re-send of this mission's announcements at the next PAIRED.
- **Review fixes** (two reviewers on `e27dab2`; each Lua block fails against `e27dab2` and passes now):
  - The receiver's matching could remove an innocent vehicle: a wreck at the reported id (`IsAlive` false but still
    in the pool) fell back to the 5 m position match and removed a live neighbour (on the host path both copies lost
    it). Now only an id with no instance is matched; a wreck or an id this copy lost itself is `absent` (`reason=wreck`
    / `lost_here`); beyond 1 m the id must be the nearest of its type (`not_nearest`, then the 5 m match); `V.near`
    skips ids this copy is watching or lost; a request for a vehicle this copy is Fultoning itself waits for its own
    watch (`own_watch`).
  - A request the host matched to another pool index was announced with the host's id, and the guest removed its
    own vehicle at that index (another jeep). The answer now carries the guest's id (the host keeps its own in its
    gone list and answers a re-request again); the guest closes a request only when the rid's request names that id
    (a re-sent announcement with an old rid no longer closes another request).
  - A vehicle held for the partner's ride was removed at the first poll after an unpair or a 1.5 s datagram gap
    (world_ride_pose wipes its tracks). Nothing is removed while unlinked (`hold;reason=unpaired`), and the ride is
    latched 10 s past the last RIDE_FAST datagram (the clock stands while unlinked) until its exit RIDE_BODY.
  - A Fulton counted as lost as soon as `IsAlive` read false; a balloon that failed after that left a one-sided
    removal. Now, while the instance stays in the pool, a Fulton is lost after 10 s not alive (`step=settle`), so a
    FultonFailed or `IsAlive` true again cancels it first.
  - The index Despawn of a named vehicle erases its name binding (Ghidra `0xB30AF0`): the index form is now used only
    at Mother Base; `veh_wav_*` joins the scanned locator families.
  - The VEHICLE_GONE / REQ handlers, the RECEIVED / REJECT additions and the start / paired / unpaired hooks run in
    `pcall` (an error there failed all world sync for the mission); the error turns only this module off.
  - `world_vehicle_lua_check` added to `Get-WorkerTests`; scenario help names `world_vehicle_gone`; scenario comment
    corrected (the `sync` lever skips the detection) and part B / D log expectations added; two-pc-play no longer
    says to edit the play `lab.ini` (Play-Coop has no option for the key); the CFG receipt row lists 15 values;
    the IsAlive sentence and the Despawn / Kill description corrected; addresses as RVAs.
  - Rejected: none.

### online-handoff-join

Branch `wp/online-handoff-join` from `dev/oct7-continue` `cc973a8`. Joining without the same save was finicky: the
friend's first launch went alone into story mission 10020, the second waited at the title with wrong messages, MGSV
took 3 minutes to come back after the handoff and the host only saw "partner left". Bug fixes, on by default, no new
lab.ini key (details in docs/save-handoff.md "Messages while the guest waits", "Title Continue", "Exit" and the tooling
contract). Unit-tested; no game launch and no hidden run in this phase (the runtime checks are for the hidden-run
phase). Contract unchanged (743 code sites / 193 vtable slots; no new game address).
- **P1, a co-op copy with remote files only.** The friend's copy was seeded from a 2015 Steam save with only
  `remote\TPP_GAME_DATA` / `PERSONAL_DATA`; the set hash needed slot 0 or 1, so the guest never had a set, never
  decided its need (STATE `need=0 set=00000000`) and the title lock fell back to "story start: press again". The
  guest's own set may now be identified by slot 4 (`io::HashStep(..., remote_primary)` for the guest only; the host's
  snapshot, the OFFER decode and the manifest still need slot 0/1). New line `save_handoff=set; ok=1; set=..; files=N;
  local=0|1` / `ok=0; why=read|no_primary|hash` once per change (an invalid set was silent). F9 `idle` has its own
  sentence ("Comparing your save with the host's; nothing to share yet.") instead of the `same` one.
- **P2, the natural double press.** At the title, presses under 3 s apart are no escape and no decline; a pending
  handoff (CFG r[12] 1..4: NOTE 7 b=3/4, NOTE 9) has no Continue escape for its first 60 s at this title
  (`decision=held`); F9 > Shared save > Keep my own save stays the way out, and after 60 s a repeat 3-10 s apart still
  plays the own save. NOTE 7 b=1/2/3, NOTE 9, G2, the Play-Coop countdown line, docs/debug-menu.md and the FRIEND-GUIDE
  no longer say "press Continue twice".
- **P3, the settle wait.** `kSettleBindMs` 30 s -> 10 s: the window never guarded against a set saved for the next
  mission (every load reports loading at once and ends it; the set must stay unchanged with saving=0 and fresh
  reports); a deploy into Mother Base made the friend wait 16 s at the title. New toasts G11 (guest: "The host is in
  <M>: their save is checked there (about 10 s), then copied to you at the title screen...") and H8 (host: "Checking
  your save here (about 10 s; stay in this mission)...") replace G4/H3 while the host settles in 30010/30020/30050;
  NOTE 7 b=4 at the title; the host's F9 continue_unknown line is no longer "cannot be shared".
- **P4, the restart the host never heard of.** The guest posts its WM_CLOSE only once RESULT exiting left channel 4
  (`ReliableQueued(4)` 0), it was refused unsent, or 1 s passed (`save_handoff=exit_flush; ms=..; acked=0|1`); the
  host infers the restart when a scene/always guest with a verified stage leaves (`partner_restarting`, H2,
  `save_handoff=partner_restarting; how=link_closed`); a stage-mode guest leaving stays `partner_staged`.
- **P5, the 3-minute Steam relaunch.** The relaunch waits (read only, at most 30 s) until Steam no longer marks MGSV as
  running (`HKCU:\Software\Valve\Steam\Apps\287700` `Running`, `HKCU:\Software\Valve\Steam` `RunningAppID`; both read
  on this PC on 2026-10-22: 0 while MGSV is not running), then 2 s; the Play-Coop window retries every 20 s while no
  game runs and Steam does not mark it running, and asks for a manual start at 45 s (was once at 60 s and at 120 s);
  the helper retries the same way when the window is gone. Every step goes to `mgs-coop\play-coop.log`
  (`Write-CoopPlayLog`, no codes or SteamIDs, rotated at 1 MB), which Collect-Logs now collects. G2, the F9 exiting
  line and the FRIEND-GUIDE: "not back within a minute? Press Play in Steam". The window's restart wait
  (`Get-CoopRelaunchWait`) reads `play-session.json` again for up to 1 s before it concludes "co-op was switched off": a
  read that raced the helper's File.Replace stopped the wait and switched co-op off (found as a friend_package_tests
  flake in this branch's second full CTest).
- **P6, in game with need=1.** G12 once per link session ("The host's save is ready for you: go back to the title
  screen (or quit MGSV and start Play-Coop again) to get it...") and F9 `wait_host` why 1.
- **P7, contradictory host notes.** A sortie into the partner's own mission is a join: `deploy_alone;reason=join_partner`
  and NOTE 3 b=5 ("Joining your partner in <M>; co-op travel does not follow in story missions." / "Flying to your
  partner in <M>."), and no NOTE 6 when the partner is in that very mission.
- **P8, invisible toasts.** `hud=toast; kind=world_note|save_handoff; code=N; a=..; b=..[; partner=..; local=..];
  on=0|1[; skipped=N]` in script-observer.log, one (kind, code) per 10 s (`coop_hud::SetEventLog`).
- **Lab tooling.** `-GuestSaveSeed real-remote` (worker only, guest only): the real seed with 287700\local's progress
  files removed from the run's own copy (`Remove-WorkerLocalProgress`); scenario
  `tools/worker/scenarios/handoff-double-press.txt`.
- **Tests (each fails before the fix, passes after).** `save_handoff_core_tests` TestSettleWait, TestGuestInGame,
  TestExitFlush, TestHostInfersRestart (+ TestRemoteOnlyGuest, TestSettle for 10 s and a load ending the window);
  `save_handoff_io_tests` (a remote-only tree: the guest's set, `no_primary` for the host, the module's one
  `save_handoff=set` line); `steam_transport_tests` (every new or changed sentence, the toast log and its rate limit);
  `debug_overlay_tests` (F9 lines); `world_handoff_lua_check` (3 s gap, 60 s hold from the first tick, NOTE 7 b=4, no
  NOTE 6 into the partner's mission); `world_follow_lua_check` (NOTE 3 b=5); `friend_package_tests` (Steam's flag, the
  wait, play-coop.log in the apply, the launcher and Collect-Logs); `worker_session_tests` (`real-remote`).
- **Review fixes (two reviewers; on by default, no new key).**
  - The title hold for code 1 (wait_host) now also applies while the host loads: `X.decide` refused it only after the
    peer rules, which returned allow for peer mission 0 (the logged 17:46:14 host Continue), a story mission, no link
    or a queued follow, so the first press loaded the guest's own save with no note. It is now checked right after
    the lock bit (`world_handoff_lua_check`: host loading / in 10020 / unlinked / queued follow, refused then held;
    fails before). With the travel lock off code 1 stays allowed (a lock rule): the FRIEND-GUIDE and
    save-handoff.md "Title Continue" say "with the travel lock on (the default)".
  - The guest's start-up mission 1 (ingame=1, title=0) is no location and no "in game": the location rule waits for
    a real mission and G12 / F9 why 1 need a mission >= 10000 (`TestGuestStartup`: no need=1 why=location, G12, G11
    or H8 at the boot, need=0 at a title at the host's place; a story-opening guest decides need=1 why=story at
    mission 1 but gets no G12; fails before).
  - G11 names the seconds only for a host build >= 0.13.6 (`kSettle10Min*`; a 0.13.5 host takes 30 s); NOTE 7 b=4
    and F9 `wait_host` why 2 no longer say "(a few seconds)" (`TestSettleWait`, `steam_transport_tests`).
  - The manual-start hint no longer depends on a retry: when Steam still marks MGSV as running (no retry goes out) the
    window runs `Start-CoopRelaunch -CheckOnly` at 45 s and says "...Steam still shows MGS V as running: once Steam
    shows Play again, press it..." (before: no line until the 300 s limit). The applied save and "Waiting for Steam to
    let MGSV start again (up to 30 s)..." print before that wait (`-OnApplied`), not after it. Test knob
    `MGS_COOP_TEST_RELAUNCH_HINT_SEC`; friend_package_tests [handoff] covers both.
  - friend_package_tests SS-4 ([claimed failed/off]) flake: the claim is written only once the window recorded the
    game (`game_pid`); a claim written before Set-CoopPlayGame became `relaunched` and the exit found none.
  - handoff-double-press.txt: the host reaches its title first (the 60 s start at the guest's own title) and the held
    press is asserted (`waitlog ... decision=held`).
  - Rejected: NOTE 7 b=4 for a host whose save is `continue_unsupported` in 30010/30020/30050 (the set changes on the
    arrival save, so that state is transient there; the eligible and settling states fit b=4).
- **Runtime checks for the hidden-run phase:** handoff-double-press (real and real-remote guest seeds), R2
  handoff-stage with `-GuestSaveSeed real-remote`, R4 handoff-restart with `-Mode play -WanSim bad` three times
  (`result_rx; status=exiting` or `partner_restarting; how=link_closed`, `exit_flush`), R5, R6 travel-lock (now presses
  3 s apart), and the manual Steam relaunch check on one PC (diag-join.md P5).
- **Hidden runs (2026-10-23, `wp/integ10`, research build, gun stage, `-SaveSeed lab`, `save_handoff=stage`; every run
  Closed, BaselineRestored, RealSavesUnchanged, no visible fallback):**
  - **P1 proven live** (`handoff-stage.txt` steps, `-Mode play -GuestSaveSeed real-remote`): `247ae49a` (`de949e9`)
    and `c186abfb` (`188f55a`). The guest's copy holds no `287700\local` progress (`save_seed_removed`
    TPP_GAME_DATA0/1, PERSONAL_DATA0/1); it logs `save_handoff=set; ok=1; set=8DA6039D; files=2; local=0` at its
    boot and `need=1` (the host's `state_rx` shows the guest `need=1; set=8DA6039D`, not `set=00000000`), the host
    binds 30010 by `continue` and offers (6 files, 609,012 B), the guest stages at its 40050 title (3.2 s). The 0.13.5
    field failure (`need=0 set=00000000`, no offer) did not occur.
  - **Fixed from these runs (`188f55a`, on by default):** the boot reports the story sequence's default (the opening)
    until the save loads (`story=1`, then `2`, inside mission 1). In `247ae49a` / `1eff8016` a booting guest decided
    `need=1; why=story`, and the two copies booting together showed H4 on the host and G5 on the guest ("story
    opening", `hud=toast; ...; code=24` / `code=5; ...; on=1`) for saves that reach Mother Base. The core now counts
    the host's own story as unknown in its start-up mission (transient, no toast) and the guest's story rule waits for
    a real mission like the location rule (`save_handoff_core_tests` TestGuestStartup, fails before). After it
    (`c186abfb`, `e5f15c7e`, `640abf2f`, `5e44cb65`): host `elig=3` at its boot, no `need` line before the guest's
    title, then `need=1; why=location` there, and no H4 / G5 at all.
  - **P2 proven live** (`handoff-double-press.txt`): `e5f15c7e` (lab, `real-remote`), `640abf2f` (lab, `real`) and
    `5e44cb65` (`-Mode play -WanSim bad`, `real-remote`, guest `rtt_ms` 175-192, loss up to 13 %). Each: note 7 b=3
    and its toast, a press 2.4 s later `decision=refuse` (silent), the next ones 3-6 s apart `decision=held` (twice),
    zero `decision=repeat`, zero `save_handoff=declined`, zero note 10; the guest stays at 40050 and stages once the
    host continues into 30010 (`staged` in 3.2 s; 10.4 s over the bad WAN). Frames: the title menu after every
    press, the note 7 b=3 toast ("...Stay here; F9 > Partner / Co-op > Shared save > Keep my own save plays yours.")
    and "host save ready - MGSV restarts". The first run `1eff8016` failed its two expectations only on timing: the
    scenario pressed at +1 s and +4 s of the title, which still plays its logos (PRESS ENTER KEY was up at +25 s), so
    the presses never reached Continue; the guest still stayed at 40050 and staged. The scenario now waits 25 s
    after `titlemenu guest` (`188f55a`).
  - **P8 live:** `hud=toast; kind=save_handoff|world_note; code=..; a=..; b=..[; partner=..; local=..]; on=1` lines on
    both copies: guest G4 (4) at its title, G1 (1, a=595 KB), G3 (3); host H3 (23), H1 (21); world_note 7 (b=3) and
    16.
  - **Still open:** R4 handoff-restart (`-Mode play -WanSim bad`, three times: RESULT exiting / `partner_restarting`,
    `exit_flush`), R5 handoff-after-restart, R6 travel-lock and the one-PC Steam relaunch check (P5); the P2 escape
    after the 60 s hold and F9 Keep my own save; P3 (`kSettleBindMs` 10 s, G11 / H8) and P6 (G12) have no worker
    scenario; Steam transport cannot run in the worker.

### online-body-safety

Branch `wp/online-body-safety` from `dev/oct7-continue` `cc973a8` (diag-other #4, #5, #6, #7 and the #11 log caps).
Bug fixes for real play, on by default (no key); docs/native-puppet.md "Body safety". Hidden-worker results at the
end of this subsection (sea death proven both ways, put-down and seated aim passed, the ledge divergence not reproduced).

- Sea death (#5): `downed_damage::Evaluate` turns a lethal hit on the local player into "downed" only for a natural
  collision part (0..11). The sea / out-of-bounds kill (part 12, flags 0x2419, damage 10000) is the stock death again,
  so PLAYER_FALL_DEAD drives the shared game over at once (`downed_damage=stock_death; reason=environment`); the
  session downed both players in the sea (H:11467, F:12698). Read-only proof that no weapon uses part 12: the only
  player collision-hit builder `DefenseCallback::TargetCallbackExec` (the only caller of `GetPartsIdFromName`
  0xFF3480) writes packet+0x26 from that classifier (0..11, default 1) or 1 for shields; ~1400 logged samples show
  lethal part 12 only as the environment kill (9, all sea deaths; the other 4 part-12 samples are damage-0 0x401
  packets while carrying, never lethal). A lethal land fall's part is unproven (no sample): if it is 12, it is the
  stock death too. The replica stays protected from every lethal hit; a downed player keeps its shield except for a
  kill lethal at full health (review fix below). Tests: `downed_damage_tests` (fails on 0.13.5: part 12 on the local player returned cause 1).
- Ledge falls (#4): `partner_body_safety` drop guard (a cliff drop / fall of body 1 with no matching remote vertical act
  within 500 ms and an unchanged remote height: a vertical placement at once) and stuck guard (placed ok=0 while body 1
  falls or hangs at a ledge and the remote is on foot: the re-show flow at the target; bits cleared, held hidden with
  `clear_all_active` like the mount hide until unrealized, then the range placement). Tests: `partner_body_safety_tests`,
  `partner_driver_tests` `TestBodySafety` (drop, matched fall, free-roam re-show); none of these lines exist in 0.13.5.
- Carry put-down drop (#6): the put-down guard (remote carried/asleep, body 1 released 44/131/132/25/26, more than 2 m
  below the target: Warp at the target, xform pin every tick until it holds 500 ms) owns body 1 for the episode,
  overriding the stock carry hold for that tick only; in `partner_driver`/`partner_body_safety`, not in coop_carry.
  Tests: `partner_body_safety_tests`, `partner_driver_tests` (put-down Warp, pin, held end).
- Seated aim latch (#7): `partner_aim::RideGate`; no convention check and no v6 direction while the local player rides
  (ON_VEHICLE / ON_HELICOPTER / ON_HORSE, the native passenger report, the driven vehicle), `aim_dir=refused;
  reason=ride`; every ride edge restarts the miss streak. Tests: `partner_native_tests`, `partner_driver_tests` (the
  v6 sender while driving: no direction, no checks, the direction back on foot).
- Log caps (#11): the partner_recon event families rotate (64 per 2 min window, 1024 per family and mission,
  `resumed; dropped=` summaries) instead of 64 per mission; the world LOG receipts use a token bucket (600 burst,
  20/s) instead of 200 per fixed 10 s. Tests: `log_budget_tests`.
- Scenarios: `body-safety-sea-death.txt`, `body-safety-sea-death-host.txt`, `body-safety-sea-death-downed.txt` (MB,
  `-SaveSeed real -Mode play`),
  `body-safety-ledge.txt` (`-WanSim awful`), `body-safety-putdown-edge.txt`, `coop-vehicle-seated-aim.txt`.
- No new game address: contract 743 code sites / 193 vtable slots (`--check` unchanged).
- Review fixes (two reviewers; each new case fails on the previous branch head unless marked coverage):
  - Drop guard on the logged cliff drop (H:11285..11309): in play (native drive, `partner_native_traverse=1`) body 1's
    own 28 enters the native traversal hold on its edge tick, and the guard closed every cliff-drop episode a tick
    later (`!traverse_hold` in its enable); the hold now only blocks new episodes. And at +500 ms the body sat in 26
    (land) on the lip outside the railing at the deck's height, judged "landed", then fell again (26 -> 25 -> 54, no
    edge): a landing at the remote's level now keeps the episode open (at most 2.5 s) until body 1 is on its feet
    (landed) or falls again (place, here at +813 ms, before the hang). Tests: `partner_body_safety_tests` (the logged
    acts, times and heights), `partner_driver_tests` `TestBodySafetyNativeDrop` (native drive, traverse on).
  - A seat episode that takes body 1 drops a pending body-safety re-show (request or hold), as the range and ride
    hides do; the seat exit re-shows body 1 itself (reachable when the remote boards right after the stuck request,
    or during a hold whose bit clear was refused). Test: `TestBodySafetyRideGuard` (seat re-show).
  - The put-down guard never acts while the horse cargo holds body 1 (`PartnerCarryHold` without a stock carried
    body 1; Lua warps it onto the horse every tick; lab only): no episode, an active one ends `reason=cargo`. Test:
    `partner_body_safety_tests`.
  - A downed (protected) player who crawls into the sea: a non-natural-part hit on a protected player is calculated
    first; one lethal even at full health (damage >= maximum life: the sea kill, 10000 against 7200) is the stock
    death (`downed_damage=stock_death; reason=environment; protected=1; ...`); every natural part and a part-12 packet
    lethal only at the downed health keep the shield. Test: `downed_damage_tests` (`ProtectedEnvironmentKill`).
  - Coverage: the stuck re-show on Mother Base (30050, the ride guard's range gate; no bug found) and the rotating
    recon log caps in the driver (`capped` then `resumed; dropped=` after the 2 min window), both in
    `partner_driver_tests`. The world LOG bucket keeps its pure `log_budget_tests` (world_link runs on real time).

- **Hidden runs (2026-10-23, `wp/integ10` `32ac240`, research build, gun stage; every run Closed,
  BaselineRestored, RealSavesUnchanged, no visible fallback; scenario fixes and result notes in `a4c3a0f`):**
  - Sea death, guest (`b8ab039e`, `-Mode play -WanSim bad`, real seed): both copies on the walkway, the guest
    teleported over the sea: `downed_damage=sample;id=0;exact=1;scoped=1;...;part=12;life=6000;damage=10000` ->
    `downed_damage=stock_death;reason=environment` -> host `shared_game_over; type=2; cause=2`, guest
    `game_over_applied` 0.13 s later; no `lethal_capture`, `intercept;cause=lethal` or `downed; step=entered` on
    either copy. Frames: the guest falling past the deck into the sea, then MISSION FAILED on both copies.
  - Sea death, host (`e9e5301b`, same flags): the first deck spot `(205.0, 0.9, 403.0)` was open water and put the
    HOST in the sea: `exact=1;scoped=1;...;part=12;life=7200` -> `stock_death` -> `shared_game_over; type=2;
    cause=1`, guest `game_over_applied`; no `controls_timeout` (the session waited 12 s for it). Frames: MISSION FAILED
    on both copies.
  - The first scenario version (`de7b3e09`) teleported the guest alone 500 m from the host: its body 1 was unrealized
    (range rearm), so the downed shield was not live (`exact=0`), the stock death ran (game over 1.3 s later) and the
    fix was never exercised. The sea, ledge and put-down files now stage both copies on the walkway (x 206..212 there,
    the friend's own on-deck poses of the session; yaw 0 faces +z).
  - Ledge (`e768a6b7`, `d1c74cde`, stall copy `1bb6ad1d`; `-Mode play -WanSim awful`): the divergence did **not**
    reproduce (body 1 stopped within 0.2 m of the remote every time; the 400 ms worker input stall did not carry it
    further). The drop guard ran in every Mother Base run at the arrival: body 1 stepped 0.7 m off the arrival ramp
    while the remote stood on it, `partner_body_safety=drop; verdict=place; act=25; ms=500..515; remote_act=1;
    dy_mm=696..721` -> `partner_recon=place; reason=vertical` -> `placed; ok=1`; body 1 then fell the 0.7 m again
    94 ms later (26 -> 25 opens no new episode, by design) and walked on 0.7 m below the remote until the remote
    stepped down. The stuck guard and its re-show never ran.
  - Put-down (`4078bbe7`, `-Mode play -WanSim bad`): passed. `partner_body_safety=putdown; step=start; body_act=44;
    remote_act=44; dy_mm=2090; err_xz_mm=1187; carry_hold=1` -> `step=end; reason=held; ms=534; places=1; pins=31`,
    but body 1 never left the deck at this spot (the railing kept the stock put-down on it, `partner_body` y 0.80
    throughout): the episode was opened by the remote's own still-raised carried pose (its act 44 began 250 ms after
    body 1's) and the place / pins did not move body 1. Frames: the guest's body lying on the deck at the railing.
    The session's push off the deck did not reproduce, so the rescue itself is unproven.
  - Seated aim (`637b47ea`, lab): passed. `aim_dir=refused; reason=ride; checks=0; misses=0` on both copies while
    seated / driving, `aim_dir=valid; reason=none` on foot after the get-off (host then `checks=90; misses=5`, not
    latched), no `reason=convention` anywhere.
- **Unproven / open:** the ledge divergence, the stuck guard's re-show and the put-down rescue (no reproduction in
  the hidden worker); `body-safety-sea-death-downed.txt` (not run); seated aim in play; a lethal land fall's part.
  Finding (not changed): the put-down guard opens on an ordinary put-down whenever the remote's carried pose is still
  more than 2 m above body 1; harmless in `4078bbe7` (no pop), but the warp target is the raised pose.

### partner_native_ui_filter
- **G1, the viewer's HUD shows the partner's ladder hint** (`wp/ladder-hint`, from `dev/oct7-continue` `cc973a8`;
  docs/native-puppet.md "Partner HUD hints"). New lab key `partner_native_ui_filter=0|1` with `_given`: **default 0
  in lab and play** (also with the native gun profile); a named 1 runs in the lab only, play forces it off (not a
  play refusal). Version unchanged (0.13.5).
- Cause (read-only Ghidra and the `.text` dump): `P+0xD8` is one `UiControllerImpl` (vtable `0x1423AF700`) for both
  bodies; its hint queues vt+0x240..+0x288 are ten identical leaves over request records at ui+0x1E0+k*0x50 with no
  body check. Phase 1 of the ladder plugin (`0x1411EA220`) runs the state callback with the local event 4 for every
  idx while S_IS_ONLINE is clear (always in this mod); the ladder states' event 4 (`0x1411EC3A0` case 4 and three
  more) post hint 3 / priority 8 through vt+0x260 = `0x140FE8480` (record 0x320) with no local-index check, from
  inside instance 1's drain job. The 0.13.5 note's vtable `0x1423AF358` / `0x140FE6D10` was another UI class's
  table (stored at `0x140FE648A` / `0x140FE679B`; it holds the same leaves 0x38 bytes further in, so it is not
  `P+0xD8`'s interface); `0x140FE6D10` is UiControllerImpl's vt+0x228.
- Census: climb (0x61), pipe (0x60) and the ledge hang (2) post into the same queue (their IsNeedToSync guard is
  S_IS_ONLINE-gated, false offline); step-on, the pickup plugin and the box use no hint queue; interactive, trash
  box, the ride plugins and attack use vt+0x240 / +0x250 / +0x258. vt+0x268 / +0x270 / +0x278 / +0x288 / +0x280
  already have a body-1 filter (teammate_targeting, the pose window). Body 1's other UI calls found are guarded by
  `P+0x218` or read-only.
- Fix: `native/partner_ui_filter` MinHooks the five unfiltered leaves (`0xFE84F0`, `0xFE8970`, `0xFE8560`,
  `0xFE87E0`, `0xFE8480`), lazily with the defer install once the five slots and the SHA-256 body pins match. A
  request is swallowed only inside the scope `partner_ui_filter::Begin(P)` / `End` opens around block 1's drain job
  (the hook thread, P+0xD8's validated UI: vtable, primary, owner); everything else, the viewer's own update
  included, goes to the original leaf. No vtable copy: swapping the UI object's vptr for the window would break the
  identity checks that teammate_targeting, the pose window, nametag's HUD probe and the sleep / vehicle readers make
  during body 1's job. Counts from the hooks, lines from the driver tick (`partner_ui_filter=installed|configured|
  swallowed|key|patch_lost`).
- Contract 743 -> 765 code sites, 193 -> 199 vtable slots (`UI_FILTER_*`, with assertions: one leaf body modulo the
  record, slot-only reachability, the factory's vtable stores, the census return addresses).
- Tests: `partner_ui_filter_tests` (route / identity / key / log decisions, the contract pins, the detours on fake
  objects: scope, thread, receiver, ABI forwarding, refusals, fault latch, records digest, keys, Shutdown, Install
  refusals on a fake image), `partner_driver_tests` TestUiFilterLog, the key in `lab_config_tests`,
  `lab_ini_check`, `friend_package_tests`, `worker_session_tests` and the three `-LabSetting` regexes.
- Scenario `tools/worker/scenarios/partner-ui-filter-ladder-mb.txt` (not run): the Mother Base ladder with the key on
  both copies.
- Open: no hidden-worker run yet (none in this step); the default stays 0 until a FRAME shows the viewer's HUD
  without the hint while body 1 climbs. Why the climber's own HUD shows no hint mid-ladder in stock is not
  explained; if the viewer's hint survives with `q260` growing, it is not the vt+0x260 queue. A block-1 job that
  runs without a window (the legacy fallback after a refused drain) opens no scope, so its hints still pass.
- Second pass (same branch, no code change): re-checked against the image and Ghidra (read-only) that `0x1411EC3A0`
  case 4 posts vt+0x260(3, 8) unconditionally (the IsCompletedActionIcon call at `0x1411EC54A` belongs to case 1),
  that `0x1411EA220` picks event 4 for every idx while `P+0x204` bit 8 is clear, that only the window runs block 1's
  job (`block == a.pool1`), that no other contract pins or hooks the five leaves (the camera agent's native_ui /
  pose window cover `0xFE8690` / `0xFE8700` / `0xFE8770` / `0xFE89E0` / `0xFE8C70` only), and that the pose
  window's sleep-hint detour requires `[ui] == 0x23AF700` during body 1's job (a vptr swap would make it fail).
  The leaf ABI (rcx ui, dx id, r8d priority, r9b flag, three stack bytes at +0x28 / +0x30 / +0x38) matches the
  detours, and the call at `0x1411EC606` loads `[[[rsi+0x40]+0x138]+0xD8]` with id 3 / priority 8.
  `generate_partner_contract.py --check` 765 / 199; full CTest 290/290 green (`-Configure`).
- Review fixes (same branch): the contract now pins the UI init's owner store `0xFE7956` (primary+0x48 = ui+0x28 =
  P, Begin's owner check) and its slot (primary vtable `0x23AF6D0` vt+0x8 = `0xFE7950`): 766 code sites, 200 vtable
  slots. The driver reads the hook stats before the vehicle-job early return and runs the ui-filter log there too, so
  the ride hints (vt+0x240 / +0x258) are logged during the ride (`vehicle_driver_wiring_tests`). `partner_defer_tests`
  drives the scope through the real drain window: body 1's vt+0x260 request from block 1's job is swallowed; block 0's
  job, a refused block-1 job (no armed Arm, the legacy call), the thread after the window and after a job that unwound
  through GuardedCall all reach the original. native-puppet.md: priority 255 was read only on the vt+0x280 record
  (0x370); record 0x320's idle value is assumed from the shared leaf body. The scenario's two `swallowed` waitlogs
  now need `q260` > 0, so a line from another queue cannot pass them. `--check` 766 / 200; full CTest 290/290 green
  (`-Configure`). No game launch and no hidden-worker run.

### debug_vehicle_door

- G4 truck test setup for the passenger fallback proof (`wp/truck-helper`, docs/native-puppet.md "Truck test levers").
  Not run in the game yet; the version stays 0.13.5.
- Cause of the missing truck proof (runs `59cfc415`, `495e7c3a`, `de949f80`): RideVehicle CanExec gates trucks (types
  3/4) on the door (within 1.4 m of `seat + 0.4 m` outward, on its outer side; `0x1246B13..0x1246CA0`), and the old
  `goto_vehicle truck` spot was 1.9 m beside the record's centre, at the cargo bed. The two copies place their trucks
  independently, so the guest's nearest truck was another one.
- New lab key `debug_vehicle_door=0|1` (default 0 in lab and play, `_given` flag, play forces it off; the receipt also
  refuses outside mode=lab with offline and redirected saves). Added to lab_config, the three `-LabSetting` regex copies
  and help, LabCommon, worker_session_tests, lab_config_tests, lab_ini_check, friend_package_tests and the
  native-puppet.md configuration table.
- Native `MGS_COOP_VEHICLE_DOOR_V1` receipt (`native/vehicle_door.{hpp,cpp}`): this copy's record of a vehicle (or of
  the vehicle in the partner's live ride packet), GetVehicleType / the seat count / the seat matrix `0xB7AF90` for
  seats 0 and 1 under SEH with the record identity and the viewer's own act and ridden unit digested around them (a
  surprise latches the mission); answers the stock door point and a stand spot. Read only, no patch.
- Lua levers: `goto_vehicle truck [driver|passenger] [cm]` (the nearest truck with a free driver seat, its door's
  stand spot; key off without arguments: the old spot), `goto_partner_vehicle [driver|passenger] [cm]` (the guest to
  the host's current vehicle, passenger door by default) and the read-only `vehicle_door` report of the stock gate
  (`vehicle_door partner` without a door also reads the passenger door, matching `goto_partner_vehicle`).
- Review fixes: the `vehicle_door partner` default above; `debug_menu_lua_check` now asserts the key-off path (a nil
  `gotoTruck` answer keeps the old 1.9 m truck spot, a door argument still refuses `door_unavailable`).
- `coop-vehicle-passenger.txt` truck half rewritten: host boards a truck at its driver door and drives 1.5 s, the guest
  warps beside this copy of it and presses E; expected `board_block; ...; mode=passenger; reason=type; type=3|4`.
- Contract: `DOOR_SITES` (7 blocks), `DOOR_DATA` (0.4f x3, 1.4f), `door_assertions`; 750 code sites / 193 vtable slots
  (was 743/193). Tests: `vehicle_door_tests` (new, in Get-WorkerTests), `partner_driver_tests`
  (`RidePeerVehicle`), `lab_config_tests`, `debug_vehicle_lua_check`, `debug_menu_lua_check`.
- Runtime still to prove (hidden runs): the host gets the truck's ride prompt at the stand spot (`vehicle_door`
  `gate=pass`, frame `truck-door`, the E press boards), the guest's copy of the host's truck is where RIDE_FAST put it
  (`fast=1`, `rider0=65535`), and the guest's E press logs `board_block; reason=type; type=3|4` with
  `vehicle_not_riding`. Only then may a default change.

- **Hidden run `84e3db0e` (2026-10-23, `wp/integ10` `32ac240`, lab, research build, gun stage,
  `coop-vehicle-passenger.txt`; Closed, BaselineRestored, RealSavesUnchanged, no visible fallback):** every step
  passed, both passenger halves and the truck half. The host's `goto_vehicle truck` reached `veh_trc_0000` (id 25602,
  type 4, `rider0=65535`) at its driver door, `vehicle_door` `gate=pass; d3=1.16; dot=0.87`, frame `truck-door` shows
  the stock E + truck ride icon there, and the E press boarded (`riders=0,65535; localSeat=1`). The guest's
  `goto_partner_vehicle` answered `pseat=0; door=1; fast=1; rider0=65535`; its E press logged
  `partner_ride=board_block; vehicle=25602; site=prequery; mode=passenger; reason=type; type=4; seats=2` and
  `vehicle_not_riding`; frames `pass-truck-beside` / `pass-truck-blocked`: the guest at the passenger door with only
  the Fulton prompt, no ride icon, the host seated in the cab. A truck-only scratch copy first (`a42ddafe`, about 1 min
  after gameplay) found every truck AI-driven (`vehicle_driver_taken;id=25602;rider0=1194`, the truck 490 m away and
  driving); in the full file the truck stands near the jeep spot without a driver by the time the truck half runs.
  **Default unchanged (0):** a lab test lever (play forces it off, the receipt refuses outside the lab).

### partner_native_unrealize_all

- **G5: fully hide the partner on the other ride hides** (`wp/ride-unrealize`; [native-puppet.md "Ride hides"](native-puppet.md)).
  New key `partner_native_unrealize_all=0|1` (`_given`; **default 0 in lab and play**, a named 1 runs in lab only, play
  forces it off; added to lab_config, the three `-LabSetting` regexes and the help, LabCommon, worker_session_tests,
  lab_config_tests, lab_ini_check, friend_package_tests, lab_scripts_tests and the native-puppet.md config table).
- The heli hide (acts 97..108 or a heli ride), a vehicle drawn hidden (`partner_native_ride=hide`, or seat mode that
  refused / cannot use the seat: the seat fallback hide) and the horse kind-3 hide without `partner_native_mount` cleared
  only the realize bits, which left a frozen standing body at the get-on spot (filmed for the horse in `251abcef`).
  With the key each opens a ride episode (`partner_mount_policy::RideEpisode`, pure) and unrealizes body 1 exactly like
  the 0.13.5 mount hide: `CLEAR_ALLACTIVE` while Lua reports all-active (`partner_mount_policy::Unrealize`), the show's
  `SET_ALLACTIVE`, `partner_realized=1`. A mount episode keeps precedence (its own lines). Lines
  `partner_ride=unrealize_configured|unrealize_begin|unrealize|unrealize_end` (one cap of 64 with `net_rebased`; its
  capped line reads `partner_ride=capped; limit=64; kind=unrealize`).
- **Net quality (integration change):** the branch fed every tick a ride owns body 1 to the link-quality tracker as
  excluded (`QualityIn::excluded`). online-ride-robust fixes the same symptom (the ride as one inter-arrival gap:
  `reason=net_quality; max_gap=<ride length>` after a hidden ride, play `fbde3dcc` / `5a044a4d` after each mount,
  `fad26e25` after a 13 s seated ride) for every build with its rebase after a seat episode or a ride hide (default on,
  no key). The integration keeps that ONE reset path and drops G5's exclusion (`QualityIn::excluded`,
  `ExcludeFromNetQuality`, the per-tick feed); with the key the rebase logs one
  `partner_ride=net_rebased; skipped_ms=..; good=..; good_ms=..; max_gap=..; gaps=..` line (the configured line now
  ends `net_rebased_line=1`). The scenarios wait for `net_rebased` instead of `net_excluded`.
- No new game address: the op and its binding (`Player.SetAllInstanceActiveMode` 0x9DCD20, `P+0x204` bit 0x2000) are
  the mount hide's and already pinned (the branch alone: 743 code sites / 193 vtable slots, unchanged). No wire change.
- Tests: `partner_mount_tests` (ride kinds, rule, off-ride proof, episodes, against `partner_ride_wire::Observe`),
  `partner_native_tests` (G5's cases on the rebase: an unobserved ride with frames arriving or the sender paused),
  `partner_driver_tests` `TestRideUnrealize` (key off unchanged; vehicle hide, unavailable seat, GetOff latch, heli acts,
  horse kind 3 with and without the mount key, the show's `SET_ALLACTIVE`, `partner_realized=1` and one `net_rebased`
  line after the vehicle and horse episodes, and the online native drive after a 10 s hidden heli ride: no
  `net_quality` hold with or without the key, the same time back), `lab_config_tests`, `friend_package_tests`,
  `worker_session_tests`.
- Scenarios (not run yet): `partner-ride-unrealize.txt` (lab seed: horse kind 3, a vehicle drawn hidden, guest
  `partner_native_net=online`), `partner-ride-unrealize-seat.txt` (real seed, Mother Base: the refused-seat fallback;
  the reverse half is the key-off baseline), `partner-ride-unrealize-heli.txt` (lab seed: the heli arrival after the
  lab goto).
- **Hidden run `4df9cf88` (2026-10-23, `wp/integ10` `1691253`, `partner-ride-unrealize.txt`, research build, gun
  stage, lab seed; Closed, BaselineRestored, RealSavesUnchanged, no visible fallback):** all expectations met. Guest:
  horse `unrealize_begin; ride=horse; kind=3; episode=1` -> `op=clear_allactive` -> `allactive_cleared` ->
  `unrealize_end; ... ms=9727; cleared=1` -> `foot_stable` -> `net_rebased; skipped_ms=11479; good=1; max_gap=876`
  -> `allactive_set` -> `partner_realized=1`; the vehicle hide the same (`seat=ride_hide; vehicle=25608;
  episode=2`, `ms=12480`, `net_rebased; skipped_ms=13047; good=1`); `policy=online` back to `drive=native; reason=ok`
  about 2 s after each ride, no `reason=net_quality` at all; the guest's own status stayed `st=0x0`; no
  `viewer_changed`, `uncertain=1`, fault or AV. Frames: `horse-before` the host standing by the rock, `horse-mounting`
  / `horse-riding` / `horse-ride` no body at the get-on spot (0.13.5 left a frozen standing body there), `jeep-before`
  the host beside the jeep, `jeep-entering` / `jeep-drive` / `jeep-getin-spot` no body and no driver drawn,
  `jeep-exited` the body back beside the jeep. **Default unchanged (0):** the heli and the refused-seat (Mother Base)
  kinds have no frame yet, and play forces the key off by design, so the play gate cannot be met without a policy
  change.
- **Unproven / open:** the heli and refused-seat hides have no hidden-worker FRAME yet. The in-field heli boarding
  (act 97 after the body was shown) cannot be scripted (no debug heli call; the lab goto leaves free roam at once, and
  at the arrival the partner is hidden from the mission start, before Lua ever had all-active on), so it needs a
  manual run.

### partner_gesture_lever

- **G6: a lab lever that triggers the host's own knock and hand signs** (`wp/sign-knock`, docs/native-puppet.md
  "Gestures", "Lab lever: knock and hand signs"), so the unproven knock (0x17) and hand sign Go / Stop (0x18 / 0x19)
  kinds of `partner_native_gesture` can be filmed on the guest's body 1. Lab key `partner_gesture_lever` (a `_given`
  flag), **default 0 in lab and play; play forces a named 1 off**; the receiver (`partner_native_gesture`) is unchanged.
- **Research (read-only):** neither gesture is an action-icon press (padop key-type table `0x22E86A8`: key type 0..9
  -> pad bit 5, 6, 15, 3, 1, 11, 10, 2, 18, 7 = ACTION, RELOAD, CQC, HOLD, STANCE, UP, SUBJECT, DASH, EVADE, STOCK by
  the `PlayerPad` constants; `KnockActionPluginImpl`'s only icon query, key type 0 / result 0x70,
  is the high-grass crawl). Both are requests in the plugin's work row, made through vt+0x28 `RequestKnock`
  `0x11E5100`, vt+0x30 `RequestHandSignGo` `0x11E5060`, vt+0x38 `RequestHandSignStop` `0x11E50B0` of its vtable
  `0x23C5A20` (the block's `*(blk+0xF8)`), and taken by the polling `0x11E5160` / `0x11E3A20` within two frames.
  Stock callers: the **call menu** (hold CALL = Q for 12 frames; command set 0 = knock, enabled while (act 1..20 or
  31..37, or PS rec+8 bit 17), upper 0, `Q+0x28` 0, not running; no wall needed) for the knock; **Quiet's "Cover me" /
  "Cease cover", D-Horse's "Do it" and the child-soldier orders** for the signs. No D-Dog command signs, and a CALL
  tap is the whistle: the 0.13.5 "Q then E" was a whistle and a D-Dog pet. The menu's selection is UI-side and only
  partly mapped: `CallMenuImpl::UpdateSelectKeyboard` `0x179CE60` picks the enabled record numbered 1..8 through eight
  hashed key inputs, the mouse wheel or the mouse direction; the manager executes it (`HideCallMenu(1)`) only on a
  decide input (PP vt+0x40 bit 24 or the hashed input `0x7F4D42602D29`) while CALL is held. The hashes and bit 24 were
  not resolved to keyboard keys, hence the lever.
- **Lever:** the native debug command `gesture_lever knock|sign_go|sign_stop` (F9 console or a worker `run`; never
  Lua) calls this copy's own request slot with id 0, the same call the menu makes. Admission: key, `mode=lab`, lab
  active, copied saves, offline branch, partner code gate, gameplay, not latched. Identity, slot and polling slot
  re-checked per command; the stock precondition (knock: the menu's enable test; signs: speed <= 1, act not
  0x79/0x7A, PS rec+0xC bit 28 clear (`rec_c28`), `GetStance` `0x1002350` not 4 (a crawl with rec+0xC bit 20 set;
  the raw stance byte is only 1..3), upper 0); SEH (`fault` latches); instance 1's request fields and upper byte compared
  before / after (`other_changed` latches). Result `gesture_lever=requested;kind=..;id=0;...` or
  `gesture_lever=refused;reason=..;...`; log `partner_gesture=lever; result=..; reason=..; kind=..; ...; recc=0x..;
  other_same=..` (one line per command).
- New code: `native/include/mgscoop/partner_gesture_lever.hpp`, `native/partner_gesture_lever_policy.cpp` (pure:
  grammar, slots, admission, preconditions, formats), `native/partner_gesture_lever.cpp` (identity, the call, the
  guards), the handler `HandleGestureLever` in `native/game_script_observer.cpp`.
- Contract: 743 -> 766 code sites, 193 -> 197 vtable slots (`GESTURE_LEVER_SITES`: the three request bodies, the
  request test with its two sign branches, `GetStance` `0x1002350`, the polling's dispatch and kind -> state writes,
  the 0x17 / 0x18 / 0x19 upper writes, the five stock call sites, the knock entry's enable test
  (`UpdateCommandSetKnock`) and its command binding, the CALL hold classifier call; slots +0x28 / +0x30 / +0x38 /
  +0x58), plus
  `gesture_lever_assertions` (the vtable's only store is the constructor `0x11E37DC`, no direct caller of the request
  functions, the key-type table).
- Tests: `partner_gesture_lever_tests` (pure) and `partner_gesture_lever_fire_tests` (fake image: one call with this
  copy's plugin and id 0, every identity / slot / precondition refusal without a call, the `other_changed` and `fault`
  latches) in CMake and `Get-WorkerTests`; the key in `lab_config_tests`, `lab_ini_check`, `friend_package_tests`,
  `worker_session_tests`, the three `-LabSetting` regexes and the help, LabCommon, the native-puppet.md config table.
  Full CTest 291/291 (`-Configure`; 0.13.5 had 289, plus the two lever tests).
- Scenario `tools/worker/scenarios/partner-upper-gestures.txt` (key named on the host only): the old E / Q+E presses
  are replaced by `host run gesture_lever knock`, `sign_go`, `sign_stop` and a crouched `knock`, guest bursts
  `gest-knock`, `gest-sign`, `gest-sign-stop`, `gest-knock-squat`, waitlogs for the host's `lever; result=requested`
  and `local; kind=knock|sign_go|sign_stop`, and the guest's `rx`, `submit; ...; method=reserve`, `applied` and
  `ended`; the guest's own `gesture_lever knock` must refuse (`key_off`); a census of the stock route (no gate): the
  host holds CALL, the open menu is filmed (`callmenu-open`), then `1` (`callmenu-select1`) and `E`
  (`callmenu-decide-e`) are pressed while CALL is still down.
- **Unproven (needs the hidden-run phase):** the lever in the game, the 0x17 / 0x18 / 0x19 edges at tick rate, the
  knock / sign nodes on body 1 in FRAMES, the PS rec+0xC bit 28 sign gate in the lab, and the call menu's keyboard
  route (select keys and the decide key unresolved; the census only tries `1` + `E`). The default stays 0 until
  those frames exist.
- **Hidden run `260da3e5` (2026-10-23, `wp/integ10` `32ac240`, lab seed, gun stage, research build,
  `partner-upper-gestures.txt`; Closed, BaselineRestored, RealSavesUnchanged, no visible fallback):** every
  expectation passed. The host's lever answered `gesture_lever=requested` for knock, sign_go, sign_stop and the
  crouched knock (`other=same`), then `partner_gesture=local` with upper 0x17 / 0x18 / 0x19; the guest logged `rx`,
  `submit; graph=2; ...; method=reserve` (knock `0x7b2d4f836a92`, sign_go `0xe74e11baba4b`, sign_stop
  `0xb98c80a7ddae`, crouched knock `0xd08c8b213cab`), `applied; ...; ok=1` (current = node) and `ended;
  via=timeout; cause=deadline; ms=2000` (the pickups end `via=end; cause=clip`); the guest's own lever refused
  `key_off`; no `uncertain`, `fault` or `other_changed`. **FRAMES: no knock and no hand sign is drawn on body 1**
  (`gest-knock-NN`, `gest-sign-NN`, `gest-sign-stop-NN`: the arms never move; the pickup bursts of the same run show
  body 1 bending down): the knock / sign nodes become current on body 1 but are not drawn (the L8 case of the
  scenario header; the rate-record writes of re-traverse-gesture.md 5.3 "Optional" were not tried). Census: holding
  CALL (Q) opens the stock menu (1 Knock (lure enemy), 2 Wait, 3 Get it, 4 DD (call), 5 Sonar); pressing 1 while Q
  is held runs the knock (frame `callmenu-select1`: the host's Snake knocks; a host `partner_gesture=local;
  kind=knock` without a lever line); E did nothing. **Default unchanged (0)** (a lab lever); the knock / sign kinds
  of `partner_native_gesture` stay unproven visually.

## 2026-10-22: the back-seat aim census (`partner_seat_aim`; left uncommitted on `wp/seat-aim`, committed and merged on 2026-10-30)

Written on 0.13.5 (`cc973a8`) in `D:/mgs-coop-wt/seat-aim` and left uncommitted; committed there as `wip(seat-aim)` and
merged into `wp/seat-shoot` (M7) on 2026-10-30. The contract counts below are 0.13.5's; the merge regenerated the
contract (816 code sites / 206 vtable slots).

### partner_seat_aim

- **G2: aiming and shooting from the back seat, research first** (`wp/seat-aim`; [native-puppet.md "Seat aim"](native-puppet.md);
  research scratchpad `complete/re-seat-aim.md`). Verdict: **partial**.
- **The local passenger:** stock already allows the weapon in a back seat (static reading, not yet run).
  - The ride plugin's Exec stage 0 sets the "no weapon" flag 0xC3 every frame (`0x12473A2`).
  - The passenger phase clears it once its seat loop runs (`0x1250FF8`; the only clear in the ride plugin; the
    driver keeps it).
  - The gun plugin's EnableCheck (`0x103E010`) refuses only on 0xC3 / 0xC2.
  - Expect RMB / LMB to raise and fire the gun with the **standing** upper-body set (UpdateAttackType has no posture
    for act 0x6E, unlike the heli door's act 99 / posture 5 and the horse's 0x79 / 3), the act staying 0x6E, and no
    first person (the ride's PollingForSubjectiveCamera gives a passenger none).
- **The partner shown as a passenger:** not built. The DM seat chain owns body 1, the native puppet leaves (`seat`),
  and the aim mirror refuses ride. Three designs are in the note: (A) a graph-2 gun-hold pose on the seated body 1 in
  the `partner_gesture` style, (B) upper-only native windows while seated, (C) body 1 as a stock passenger. Each waits
  on run facts first.
- **Built (lab only, read-only):** new key `partner_seat_aim=0|1` (`_given`; default 0 in lab and play, play forces it
  off), the census `partner_seat_aim=on|sample|end|status|capped`.
  - The local player in a vehicle ride act and body 1 in the seat flow.
  - Fields: act, upper act, gun mode, posture, the weapon flags 0x18 / 0xC2 / 0xC3 / 0xCF / 0xD8, the local camera
    mode and handle.
  - Per watch: aims, fires, reloads and armed / aim time.
  - No write, no game call.
- **Contract** 759 code sites / 195 vtable slots (0.13.5: 743 / 193): `SEAT_AIM_SITES` (16 blocks), Exec at +0x80 of
  both ride vtables, `seat_aim_assertions`.
- **Tests:** new `partner_seat_aim_tests` (in `Get-WorkerTests`), plus `lab_config_tests`, `lab_ini_check`,
  `worker_session_tests`, `friend_package_tests`, the three `-LabSetting` allow-lists and the help.
- **Scenario** `coop-vehicle-seat-aim.txt` (written, not run): the first two halves of `coop-vehicle-passenger.txt`
  with `labsetting partner_seat_aim=1`. The passenger holds RMB, then RMB+LMB, in the back seat, in both directions.
- **No hidden run yet.** The checks are in native-puppet.md "Seat aim".

## October 22, 2026: 0.13.5 (back-seat passengers with their own camera, the partner's cardboard box, ladders and step-ons, the pickup gesture, hidden horse / D-Walker riders)

Branch `wp/integ9` from `wp/integ8` `228eecb` (0.13.4): merge commits of `wp/board` `7b504fd`, `wp/cbox-native`
`6d58e18`, `wp/traverse-icon` `e6b3b9c`, `wp/gesture` `9eea4a5` and `wp/mount` `900eefc` (in that order, conflicts
resolved by union, the contract header regenerated inside each merge), then the version commit, the per-feature
runtime commits and `wp/pass-cam` (merged `ca495d8`). No rebase. Hidden-worker runs: per feature below, the final
regression on `b847585` and the release regression on `766aaad` (the 0.13.4 scenarios plus the board block, default
lab settings: no regression; see "Release regression" at the end of this entry). Packaged and installed in both
copies (`mgs-coop-friend-0.13.5-20261022T173336Z-3944df14`, see "Package and install" at the end of this entry); the
build id changes, so both players need this package. Full CTest 289/289 (`-Configure`; 0.13.4 had 282, plus `partner_board_tests`,
`partner_cbox_tests`, `partner_icon_tests`, `partner_gesture_tests`, `partner_gesture_hook_tests`,
`partner_mount_tests` and `partner_pass_camera_tests`). Contract 743 code sites / 193 vtable slots, equip writer
pins 118 SHA-256 gates.

**Defaults.** All five 0.13.5 features are **on with the native gun profile in lab and play** (the play default of
Play-Coop and the worker gun stage); without the gun profile they stay off, and a named key (`_given`) always wins.
- `partner_ride_board=passenger` (F1): the viewer boards a free back seat (2/3) of a partner-driven light vehicle,
  with the passenger camera; trucks, armoured vehicles and a taken or absent back seat block (no second driver).
  Proof: `495e7c3a` lab, `de949f80` play bad WAN (block half `0348c4d8` / `fad26e25`).
- `partner_native_cbox=1` (F2): the remote's box model on body 1 (play needs link v2 connected). Proof: `05cffdd1`
  lab, `71e19d1e` play.
- `partner_native_traverse=1` (F3, the icon path; user decision 2026-10-22): step-on and the Mother Base ladder on
  body 1. Proof: `104e9a47` lab, `04d3511f` play; ladder `8f27806d` lab, `2596f6ae` play.
- `partner_native_gesture=1` (F4): the pickup on body 1 (graph 2). Proof: `0e343f72` lab, `75a7e7dc` play.
- `partner_native_mount=hide` (F5): no partner body while the remote rides D-Horse or D-Walker; RIDE_BODY no longer
  moves the viewer's own horse / walker (user decision 2026-10-22). Proof: `bfd6fcc4` / `2ab88411` lab, `5a044a4d`
  play.
- Unchanged from 0.13.4: play still forces off the C4 place / detonate mirror (`partner_native_place_mirror`, no
  play-mode C4 run) and the censuses.

**Known limits (0.13.5):**
- Vehicles: no aiming, shooting or first-person view from the back seat (not supported / not exercised); the truck
  fallback is unit-tested only (no truck both copies place at the same spot); the attach jitter against the 20 Hz
  RIDE_FAST placement was not measured; the 8 s self-clear of the passenger camera is unit-tested only; the guest's
  own head showed a blood decal after rides (godmode on; source not found, already before the camera code).
- Box: act 78 (moving box) never appears (the box still moves); the exit is cosmetic (77 -> 3, no exit motion); a
  viewer without the remote's box id (`viewer_box` / `viewer_no_box`) was not run.
- Traversal: wall climb (23) and pipe (22) unproven (no spot found); the viewer's HUD shows the stock "C + ladder"
  hint while body 1 is on a ladder (cosmetic, no input path).
- Gestures: only the standing pickup is in FRAMES; the squat and moving pickup, hand signs Go / Stop, the knock and
  the reverse direction are unproven.
- Mounts: the rider is hidden, no mount is drawn; the other ride hides (the horse kind-3 hide without the key, heli,
  vehicle fallback) still only clear the realize bits.
- Pre-existing, not fixed: the Ctrl+W walk makes body 1 play the run gait in place (also 0.13.4); the teardown AV
  `crash_probe=av; rva=0x21e72` at WM_CLOSE after `partner_shutdown=hooks_off; teardown=other_thread` (seen again
  in `0bb25613`). Not yet tested in a real two-PC Steam session.

| Feature | Branch | Lab key (default) | Status |
| --- | --- | --- | --- |
| F1 vehicles: board block and back-seat passengers | `wp/board`, `wp/pass-cam` | `partner_ride_board=off\|block\|passenger` (passenger with the gun profile, lab and play) | block proven (frames `0348c4d8` lab, `fad26e25` play bad WAN); passenger with the passenger camera proven both directions (frames `495e7c3a` lab, `de949f80` play bad WAN), default since; truck fallback unit-tested only |
| F2 cardboard box on body 1, native | `wp/cbox-native` | `partner_native_cbox=0\|1` (1 with the gun profile, lab and play) | proven both directions (frames `05cffdd1` lab, `71e19d1e` play bad WAN), default on; play needs link v2 connected |
| F3 traversal start through body 1's action icon | `wp/traverse-icon` | `partner_native_traverse=0\|1` (1 with the gun profile, lab and play; existing key, now the icon path) | step-on proven (frames `104e9a47` lab, `04d3511f` play bad WAN); ladder proven at Mother Base (frames `8f27806d` lab, `2596f6ae` play bad WAN, both directions); climb / pipe unproven (no spot found); default on by user decision 2026-10-22; open: the viewer's HUD shows the stock "C + ladder" hint while body 1 is on a ladder (cosmetic) |
| F4 gestures through SetCurrentNode / ReserveToResetNode | `wp/gesture` | `partner_native_gesture=0\|1` (1 with the gun profile, lab and play) | pickup proven (frames `0e343f72` lab, `75a7e7dc` play bad WAN), default on; hand signs / knock unproven (no lab spot) |
| F5 horse / D-Walker riders: research, hide by act | `wp/mount` | `partner_native_mount=off\|hide` (hide with the gun profile, lab and play) | hide proven after an unrealize fix (frames `bfd6fcc4` / `2ab88411` lab, `5a044a4d` play bad WAN, D-Walker both directions and D-Horse: no partner body while it rides), default hide in play since `5a044a4d`; no mount drawn; RIDE_BODY no longer moves the viewer's own horse / walker (user decision 2026-10-22; frames `2ab88411` lab, `5a044a4d` play: the viewer's idle horse stays put, the partner mounts its own horse on the first E) |

- Contract after the merges: 733 code sites / 185 vtable slots (0.13.4: 677 / 149; F1 +19 / +3, F2 +10 / +6, F3 +14 /
  +20, F4 +13 / +7, F5 none); equip writer pins 118 SHA-256 gates (0.13.4: 113; F2 adds five). The per-feature
  counts in the subsections below are each branch alone on top of 0.13.4.
- Wire: F4's gesture message is new (an older build drops it); F1's local passenger report rides the existing ride
  packet; F5 only changes how the viewer reads the remote's act (and sends its own Lua the existing
  `CLEAR_ALLACTIVE` op). No existing frame layout changes.
- Scenarios written: `coop-vehicle-shared.txt` and `coop-vehicle-passenger.txt` (run, see F1),
  `partner-cbox-census.txt`, `partner-cbox.txt`, `partner-traverse-icon.txt`, `partner-upper-gestures.txt`,
  `partner-mount.txt`.
- Version 0.13.5 (CMakeLists, package.ps1, probe.ps1, the Play-Coop / Play-LocalCoop / package-install /
  set-session-mode allow-lists, which keep 0.13.4, FRIEND-GUIDE).

### F1

- **Vehicles: board block and passengers both ways** (`wp/board`; [native-puppet.md "Vehicle boarding"](native-puppet.md),
  research `re-vehicle.md`). New key `partner_ride_board=off|block|passenger` (`_given`; default block with the
  native gun profile in lab and play after `0348c4d8` / `fad26e25`; **passenger** since `495e7c3a` / `de949f80`, below). `partner_board` retargets the ride action's two nearest-seat calls (site A `0x12458AC` in the pre-query,
  site B `0x1251479` in phase 1). **block**: while the partner drives a vehicle, the viewer gets no ride icon there
  and can no longer become a second driver of its copy. **passenger**: the viewer boards a free back seat (2/3) of a
  partner-driven light vehicle through the stock passenger phase; trucks and the rest block.
- A native local passenger report (act 0x6E, `P+0x120` / `Q+0xE0`, `rider[2|3]`) replaces Lua's foot report in the
  ride packet; the receiver seats the partner in a back seat of the viewer's own vehicle (by id too), refuses
  `shared_seat` natively and skips the far latch for the vehicle the viewer occupies; `rider[]` is never written.
- Logs `partner_ride=board_install|board_policy|board_block|board_passenger|board_seated|local_passenger|
  board_refused|board_uncertain` from the driver tick (the hook only counts); the mission-scoped `board_uncertain`
  latch turns passenger into block.
- Contract 696 code sites / 152 vtable slots (was 677/149): 19 board blocks and three vehicle service slots,
  `board_assertions`. Tests: `partner_board_tests` (also in `Get-WorkerTests`), driver board / seat-gate cases,
  lab_config, lab_ini_check, LabCommon, the -LabSetting allow-lists and the two Lua checks.
- Scenarios: `coop-vehicle-shared.txt` (block, both directions) and `coop-vehicle-passenger.txt` (back seat both
  directions, then a truck that must block); `debug goto_vehicle truck` added for the latter.
- **Hidden runs (wp/integ9, research build, lab seed; Closed, BaselineRestored, RealSavesUnchanged, no visible
  fallback):**
  - `0348c4d8` lab, `coop-vehicle-shared.txt`, `partner_ride_board=block`: **block proven both directions.** The
    viewer pressed E beside the jeep (25608) the partner drove: `board_block; site=prequery; reason=block`, run
    `vehicle_seats` -> `vehicle_not_riding`, no viewer `st=0x400` while the partner drove, no `board_uncertain`, no
    `reason=local_vehicle` / `partner_ride=hide`. FRAMES: the viewer stands beside the jeep with the partner still
    seated and driving (`shared-blocked`, `shared-host-view`), the jeep drives off with the partner while the viewer
    stays on foot (`shared-drive-away-NN`); reversed: the host stands beside the guest-driven jeep
    (`shared-rev-blocked`, no ride icon) and the ride icon is back once the partner got out (`shared-rev-exited`).
  - `fad26e25` play rehearsal (`-Mode play -WanSim bad`, rtt ~180 ms, 1-3 % loss), the same scenario **without**
    the key (scratch copy): `board_install; mode=block` by default on both, the same block lines and frames both
    directions. **Default flipped**: `partner_ride_board` resolves to block with the native gun profile in lab and
    play (`ResolveWeaponDefaults`); play admits block and turns a named passenger into block; anything without the
    gun profile stays off unless named. Tests: `lab_config_tests`, `friend_package_tests` (Play-Coop native: block|0;
    a named block or passenger in play: block|1), help text, native-puppet.md. Full CTest 288/288.
  - `59cfc415` lab, `coop-vehicle-passenger.txt`, `partner_ride_board=passenger`: the back-seat prompt appears (E +
    jeep icon), the viewer boards seat 2 (`board_passenger; seat=2; site=phase1; type=2; seats=4`,
    `local_passenger; seat=2; state=on; via=p120`, `board_seated; ms=0`, viewer act 0x6E), RIDE_FAST carries it
    (`applied` 21 -> 250 while the host drove), stock GetOffBackLeft (`local_passenger; state=off; reason=act`); the
    partner confirms it seated (`partner_ride_seat=confirmed; seat=2; state=0xfe95b58c6bb0`, then the BackLeftLoop
    `0xc40b38a1a985`), both directions, no `board_uncertain`, rider[] of the real jeep unchanged. FRAMES: the guest in
    the host's back seat while the host drives (`pass-host-view`, `pass-drive-host-NN`), the host in the guest's back
    seat (`pass-rev-seated`). **But the passenger's own camera is unusable** (`pass-guest-view`, `pass-drive-NN`,
    `pass-rev-drive-NN`: a close-up of the jeep's interior until the get-off). The truck half did not run: the host
    could not board `veh_trc_0000` from the `goto_vehicle truck` spot, and the guest's nearest truck was
    `veh_trc_0003` 817 m away (the copies' trucks differ); the guest's head showed a blood decal after the ride
    (godmode on, source not found). **Passenger stayed lab-only and named-only** until the passenger camera below.
- **User decisions:** (1) the passenger camera: the stock passenger phase connects no vehicle camera for a player
  offline (seat status `cam1f0=0x0` until the get-off's `0x8800000000006408`); on 2026-10-22 the user allowed new
  camera code that connects the vehicle camera for a local back-seat passenger at boarding and releases it at the
  get-off (protected camera files and `0x11F6520` untouched): done, see "Passenger camera" below, and passenger is
  now the default board mode. (2) A truck spot that both copies share (or a lab helper that places the guest at the
  host's truck) before the truck fallback can be shown in a FRAME (still open).
- Still unproven: the truck numbering (3/4) and its block in a FRAME (the truck half of `coop-vehicle-passenger.txt`
  never boarded the host in any run), the attach jitter against the 20 Hz RIDE_FAST placement, aiming or the
  first-person view from the back seat.
- **Passenger camera** (`wp/pass-cam` `27e5e19`, merged `ca495d8`, then `c5f272d`; user decision 2026-10-22;
  [native-puppet.md "Passenger camera"](native-puppet.md)). Root cause (read-only RE): the local camera takes the
  first of its six modules whose CanActivate is true; the stock VehicleCamera's CanActivate `0x12D0F60` accepts only
  act 0x6F (driver) and 0x70 (get-off), so a passenger (0x6E) keeps the fallback camera, whose collision ray hits the
  jeep (the interior close-up). `27e5e19` swapped the VehicleCamera's CanActivate slots to grant it to the passenger;
  **hidden run `4ae70de7` (lab) showed that does not work**: it connected (`mode=3; cam1f0=0x8800000000006408`), but
  the passenger's camera stayed where the guest boarded while the jeep drove away (its 0x6E branch keeps
  `CAM+0x20C = 0` with the light vehicle's driver parameter set). **Fix `c5f272d`:** no patch at all; while the native
  passenger report says seat 2/3, the driver tick writes the VehicleCamera's own encoding of the ridden vehicle on the
  local camera (`CAM+0x1F0 = 0x88..|0x64xx`, `CAM+0x200 = 1`; never while the VehicleCamera is the active module,
  never over another owner), so the fallback camera keeps orbiting the seated passenger and its collision query
  (PlayerCamera vt+0xA8 `0x1121D30`) skips the jeep; the stock get-off (VehicleCamera Deactivate) and FinishState
  clear it, and the driver clears only its own handle if still there 8 s after the report. Logs
  `partner_ride=pass_camera; state=install|connect|refused|release`. Also: the partner's name tag over a body seated
  by the seat flow uses a seated head height (`nametag=visible; ...; seated=1`). Contract 743 code sites / 193 vtable
  slots (0.13.5 before: 733 / 185; + the collision query's `CAM+0x1F0` test and PlayerCamera vt+0xA8); tests
  `partner_pass_camera_tests` (in `Get-WorkerTests`), `partner_driver_tests`, `debug_menu_tests` (seated height).
  - `4ae70de7` lab, `coop-vehicle-passenger.txt` (build `ca495d8`, the slot-swap grant): connect and release logged
    in the first half, but FRAMES `pass-guest-view`, `pass-drive-NN`, `pass-stopped`: the camera left behind at the
    boarding spot. The reverse half did not board: the jeep ended against a wall and the host's E got an item prompt
    (the scenario now backs the jeep away and re-warps the host first).
  - `495e7c3a` lab, the same scenario (build `c5f272d`), **both directions pass**: `pass_camera; state=connect;
    reason=handle; plan=connect; ms=0; cam1f0=0x8800000000006408; cam200=1; mode=5; vmode=3; writes=1`, then after
    the stock get-off `state=release; reason=stock; ok=1; ms=3059` (reverse `3083`). FRAMES: the passenger's own
    camera behind its seated body, outside the jeep, while the partner drives (`pass-guest-view`, `pass-drive-NN`;
    reverse `pass-rev-seated`, `pass-rev-drive-NN`), the stock get-off and the on-foot camera after it
    (`pass-exit-NN`, `pass-exited`, `pass-rev-exited`); the drivers' own cameras are the stock chase camera with the
    partner seated in the back (`pass-drive-host-NN`, `pass-rev-drive-driver-NN`); the name tag sits at the seated
    head. No `viewer_changed`, `uncertain=1`, `board_uncertain` or `crash_probe=av`; each viewer's `st=0x400` only
    while it drove itself.
  - `de949f80` play rehearsal (`-Mode play -WanSim bad`, rtt ~180 ms, 0-4 % loss), a scratch copy of the scenario
    **without** the key (build `8be9479`, the new default): `board_install; mode=passenger` on both copies, the same
    connect / release lines (release `ms=3087` / `3067`) and the same frames in both directions, with the play HUD.
  - **Default flipped** (`8be9479`): `partner_ride_board` resolves to **passenger** with the native gun profile in lab
    and play (`ResolveWeaponDefaults`; trucks and the rest still block, a named block / off wins); play no longer turns
    passenger into block (lab_config and the driver settings). Tests: `lab_config_tests`, `friend_package_tests`
    (Play-Coop native: `passenger|0`; a named block or passenger in play runs as named), the -LabSetting help.
    Full CTest 289/289 (`-Configure`). All three runs: Closed, BaselineRestored, RealSavesUnchanged, no visible
    fallback (`de949f80` reported Forced=true at shutdown, as `59cfc415` did). The guest's own head showed the blood
    decal after the ride again (godmode on; already in `59cfc415`, before any camera code).

### F2

- **Cardboard box on body 1, native (`wp/cbox-native`; docs/native-puppet.md "Cardboard box").** New lab key
  `partner_native_cbox` (`_given` flag; default on with the native gun profile in lab and play since the runs below,
  play admitted only with link v2 connected like the support lane, a named 0 opts out). With it, the viewer's
  equipment observer (`ObserveNativeCbox`, right after the support lane) calls `ChangeEquip(ctl, &r, 1, {box,
  flags 4})` for the remote's box edge and `SetCurrentItemIndex(ctl, 1, 0, 0)` for its off (only a box it put on);
  the engine's own CBox polling wakes the box on body 1. The box id is one the VIEWER owns with stock
  (`mapped=same|viewer_box`, else `viewer_no_box`): the box wake reads the viewer's item stock.
- Guards as the support lane: SRW lock, SEH, every chain re-read, the full SHA gate, a late re-capture, the strict
  viewer digest and body 1's other slots around each call, a mission-scoped uncertain latch; one attempt per remote
  edge (`Mission::cbox_seq`), never a second ChangeEquip while the box is current; busy while the gun or support
  writers are mid-flight. Watches: `entered` (77/78 within 1500 ms), `exited`, `viewer_changed`. A box this lane put
  on is carried into a restarted or new mission (`partner_cbox=carried`): taken off if body 1's row still shows it
  and the remote is not boxed, released with no call if the row was rebuilt.
- Driver: `partner_cbox=remote; ...; apply=native|off; seq=..`; the reconciliation holds its placements while body 1
  is boxed (`partner_cbox=hold`).
- Contract: 687 code sites / 155 vtable slots (was 677 / 149); writer pins 118 gates (was 113: SetCurrentItemIndex,
  OnRemoveItem, OnEquipItem, GetItemStockCount, GetEquipIdSlot).
- Lab tooling: `debug items` (read-only census) and the lab-only `debug give_item cbox` (refused natively outside
  mode=lab). Scenarios `partner-cbox-census.txt` and `partner-cbox.txt`. Tests: `partner_cbox_tests` (policy + the
  module on a fake world), driver, lab config, debug Lua checks.
- **Hidden runs (wp/integ9, research build, lab seed; Closed, BaselineRestored, RealSavesUnchanged, no visible
  fallback):**
  - `ea1abea5` lab, `partner-cbox-census.txt`: both seeds `items;ids=0,468,451,492,483,484,472,466;
    counts=0,8,10,54000,10,4,8,8;current=0` (boxes 468 / 472 / 466), so each viewer owns the other's 468.
  - `05cffdd1` lab, `partner-cbox.txt` (key named): **the box on the partner body proven both directions.** The
    viewer logs `partner_cbox=remote; id=468; active=1; apply=native`, `apply; id=468; want=468; mapped=same; sub=1;
    body_sub=1; result=0; reason=ok; h4=00000000>00000000; uncertain=0`, `entered; act=77; ms=115` (reversed 109),
    `partner_act; edge=body; remote=77; body=77; match_ms=793` (789), `hold; on=1`, then `off; reason=ok`, body 1
    77 -> 3, `hold; on=0`. The viewer's own `partner_status=local; st=0x4` / act 0x4D appear only in the half where
    it pressed `slot item 1`; no `viewer_changed`, `refused`, `exited`, `uncertain=1`, writer latch or AV; the
    post-run `run items` lines equal the census. FRAMES: the box model on the partner's spot on the viewer's
    screen (`cbox-on`, `rev-cbox-on`), moving away across the `cbox-move` bursts, gone with the partner crouched
    (`cbox-off`, `rev-cbox-off`), the viewer standing unboxed (`guest-self`, `host-self`); the viewer's act stays 5
    (prone) in every capture while it films the box (on, move, off).
  - `71e19d1e` play rehearsal (`-Mode play -WanSim bad`, rtt ~180 ms, 1-3 % loss), the same scenario **without**
    the key (scratch copy): `partner_cbox=mode; mode=play; enabled=1; reason=ok` by default on both, the same lines
    (`entered` ms 101 / 102) and frames both directions. **Default flipped**: `partner_native_cbox` resolves to 1
    with the native gun profile in lab and play (`ResolveWeaponDefaults`); the observer admits play through
    `SupportModeReason` (link v2 connected) and logs the reason on `partner_cbox=mode`. Tests: `lab_config_tests`,
    `friend_package_tests` (Play-Coop native: 1|0; a named 0 in play: 0|1), help text, native-puppet.md.
- Notes: the remote's act stays 77 while it walks boxed (no 78 on either side); body 1 leaves 77 -> 3 directly
  while the remote shows its exit act 61 for ~2.7 s (cosmetic). Still unproven: a viewer without the remote's box id
  (`viewer_box` / `viewer_no_box`; both seeds hold 468), body-1 box damage against the viewer's stock, the consumers
  of `Q+0x17C` / `*(P+0x130)` vt+0x48, and pad cross-effects while boxed.

### F3
- **Traversal start for body 1 through the action icon** (`wp/traverse-icon`, `native/partner_icon.*`,
  [native-puppet.md "Traversal icon"](native-puppet.md)). The stock ladder / pipe / wall-climb / step-on start goes
  through the shared padop icon controller (`P+0xD0`, vtable `0x22E8F00`), whose builder runs for the local id only
  and fills the slots with block 0's plugins, so body 1's polls (block 1's own plugin objects) never complete. A copy
  of the padop vtable (96 slots, +0x28 `IconCompletedBody1`, +0x50 `IconForceBody1`) is swapped into P's padop only
  inside instance 1's drain windows that carry a pending request, scoped to (thread, P, blk1) around block 1's job:
  body 1's query of the requested plugin runs that plugin's own priority and `ExecActionButtonFunction(plugin, 1, 0,
  res)` once per request id (ray cache `padop+0x7F0` saved/restored, the viewer's icon slots, input states and act
  words digested before/after: a change or an SEH fault latches the mission); every other body-1 query returns false;
  body 1's step-on force-complete is swallowed. No code byte and no image vtable slot is written.
- `partner_traverse`: replay acts 21..24 (23 wall climb added), `IconFor(act)`, per-kind source acts from the plugins'
  own gates (ladder 1..4/25/31/32, pipe and climb 1..20, step-on 1..20/31/32/54), `Out.icon` level with one `icon_seq`
  per episode instead of the ACTION tap (the `BumpTap(kAction)` step is gone; ACTION stays in the native mask and the
  sample strip with the key on). 25..28 and 54..56 are never requested.
- Driver: `Arm.icon_kind` / `icon_seq` (process-unique) / `icon_until_ms` (now + 250 ms), not compared by `SameArm`;
  logs `partner_traverse=icon; plugin=..; res=ok|none; seq=..; exec_ok=..; exec_fail=..; granted=..; ...` once per
  request, `partner_traverse=icon_act` for 2 s after a grant, and the read-only `partner_icon=census; distinct=..;
  blk_ok=..; ...; verdict=..` once per mission.
- Key: still `partner_native_traverse`; no new key. Since the user decision of 2026-10-22 (below) it defaults on with
  the native gun profile in lab and play, 0 without it, and a named 0 opts out. The copy is built only with the key
  on and defer=on (`partner_icon=built|refused`).
- Contract: 691 code sites / 169 vtable slots (was 677 / 149): the Exec prologues, the priority body, the builder
  gate, the four poll call sites, the step-on force call, padop +0x18/+0x28/+0x50/+0x188 and vt[0]/vt[1] of the eight
  traversal ActionPlugin vtables; complete-body SHA-256 pins of `0xAFD870`, `0xB02480`, `0x12128C0`
  (`icon_bodies`, re-checked at Build) and reference assertions (only the ctor/dtor store the padop vtable; the
  forwarded functions are reached only through their slots).
- Tests: `partner_icon_tests` (new, in `Get-WorkerTests`), `partner_traverse_tests` rewritten for the icon level,
  `partner_driver_tests` `TestNativeTraverseIcon`, `partner_defer_tests` drives the copy through the real drain
  window. Scenario `tools/worker/scenarios/partner-traverse-icon.txt` (spot census in four headings plus captures).
- **Hidden runs (wp/integ9 `19a5809`, research build, gun stage, lab seed, key named; Closed, BaselineRestored,
  RealSavesUnchanged, no visible fallback):**
  - Census, every run: `partner_icon=census; distinct=52/52; blk_ok=0x1e/0x1e; found=0x1e/0x1e; null=17/17; read=1;
    verdict=ok` on both copies, so block 1 owns its own traversal plugins as the research note says.
  - Spot census `27af27fa` and `d10ddf39` (scratch sweeps, W then W+E with a host capture per step): the lab start's
    four headings and Wialo Village around `veh_lv_0000` (the F1 jeep, 577.6,320.0,1157.4, not Wakh Sind) gave no
    traversal (E petted D-Dog, act 94, or boarded the jeep). The rock shelf east of the lab start did: from
    828.18,337.20,1630.51 at yaw 95 the host reached act 24. In `d10ddf39` body 1 replayed it from 20 m (`start;
    reason=placed` -> `icon; plugin=step_on; res=ok; exec_ok=1; granted=1; latched=0; viewer_changed=0; faults=0;
    restore_mismatch=0` -> `edge=body; remote=24; body=24; match_ms=43`, then 28 natively off the far side).
  - `104e9a47` lab, `partner-traverse-icon.txt` with Part 3 filled (two step-on blocks at that spot; the scenario
    ran with the committed steps, followed by scratch camp views): **step-on proven on body 1.** Both blocks: `start;
    reason=near` -> `entered; reason=request` -> `icon; plugin=step_on; res=ok; seq=1|2; exec_ok=1; exec_fail=0;
    granted=1; queries=14; denied=13; swaps=2; windows=2; passed=0; force_swallowed=1; req_queries=1; deny=none;
    latched=0; viewer_changed=0; faults=0; restore_mismatch=0` -> `edge=remote; remote=24; body=24; match_ms=0` ->
    `done; reason=left`; no `partner_recon=place; reason=vertical`; the viewer's own act stayed 1 from start to done
    (its only 25/26 are the scripted landing of `placebehind`). FRAMES (`stepon-climb-02..07`, `stepon2-climb-02..03`,
    checked by eye): from 5 m the viewer sees body 1 reach up, pull itself onto the shelf and stand on it, both
    attempts, while the viewer stands still.
  - Climb / ladder / pipe search `29052784`, `9b0d5500`, `c3c74e24` (the walls at Mission 1's horse start
    1458.9,356.3,1430.1, the slopedTown outpost 562.8,325.3,70.4, Wakh Sind Barracks -497.5,322.8,500.1 with its
    watchtower, barriers and cliff bases; the far camps reached by ~1 km hops, a direct 2.5 km warp to cliffTown fell
    through the world in `104e9a47`): the host's own act reached 24 once more (a Wakh Sind jersey barrier) and never
    21, 22 or 23. D-Dog took most E presses at Wakh Sind; after the hops the guest's native drive latched
    (`enter_timeout`), so nothing was replayed there.
- **Unproven after the first runtime step: wall climb (23), ladder (21) and pipe (22).** No spot was found. The climb
  Exec (`0x14114BEC0`) only accepts an authored crack-climb geo path whose end lies within 2.5 m and +-1.5 m of the
  player (`FUN_14114AA50`; the game's own record calls it `crack_climb`), so it needs a known crack. Mother Base has
  ladders (21 appears in older 30050 logs) but the lab runs in 30010. The runtime step left the defaults unchanged
  (the bar was step-on and wall climb in FRAMES, and only step-on was met). The ladder is proven since (below).
- **User decision (2026-10-22): on by default.** Step-on alone, with the viewer guard clean in both attempts, is
  enough: `partner_native_traverse` now resolves to 1 with the native gun profile in lab and play
  (`ResolveWeaponDefaults`; the play force-off is removed), 0 without it, and a named 0 opts out. `lab_config_tests`
  (the gun profile defaults it on in lab and play; a named 1 runs in both) and `friend_package_tests` (Play-Coop
  native `1|0`, a named 0 in play `0|1`) follow; native-puppet.md "Traversal" / "Traversal icon", its config table,
  the `-LabSetting` help and the scenario header say so.
- **Hidden runs after the default flip (wp/integ9 `b9b391f`, research build, gun stage, key NOT named, so the
  default ran; Closed, BaselineRestored, RealSavesUnchanged, no visible fallback, research worker checks receipt
  `20261022-152413-153d2411a9344c23a6b6fafaa2d1aac9`):**
  - `04d3511f` **step-on play rehearsal** (scratch copy of `partner-traverse-icon.txt` Part 3 plus one reversed
    block; `-Mode play -WanSim bad`, rtt ~185 ms, 0-13 % loss, lab seed). `partner_icon=built` and `census; ...;
    verdict=ok` on both copies. Both host step-ons on the guest: `start; reason=placed|near` -> `entered;
    reason=request` -> `icon; plugin=step_on; res=ok; exec_ok=1; exec_fail=0; granted=1; req_queries=1; deny=none;
    latched=0; viewer_changed=0; faults=0; restore_mismatch=0` -> `edge=body; remote=24; body=24; match_ms=50|16` ->
    `done; reason=left`; the reversed step-on on the host the same (`res=ok`, edge 24/24). The only `partner_recon=place;
    reason=vertical` came when `placebehind` teleported the partner (body act 1). FRAMES (checked by eye):
    `stepon-climb-02..07` on the guest and `rev-stepon-climb-02..07` on the host show body 1 reaching up, pulling
    itself onto the shelf and standing on it, with the play HUD; the viewer's local act was 1 in every capture.
  - `8f27806d` **Mother Base ladder, lab** (real seed, scratch scenario: three host attempts at the ladder on the red
    container, rungs at x=22.61, z=-14.86, deck y=0.80, the climber facing +z; the spot comes from the user's 0.13.2
    MB play, assess `host_0132` ~2876-2909). Attempts A (W then W+E from 22.61,-15.55 yaw 0) and C (E alone from
    22.61,-15.40) grabbed the ladder; B (from the west, yaw 95) walked past it. Both grabs on the guest: `start;
    reason=near` -> `entered; reason=request; body_act=21` -> `icon; plugin=ladder; res=ok; exec_ok=1; exec_fail=0;
    granted=1; queries=8; denied=7; req_queries=1; deny=none; latched=0; viewer_changed=0; faults=0;
    restore_mismatch=0` -> `edge=remote; remote=21; body=21; match_ms=0` -> `done; reason=left`; no vertical
    placement. FRAMES: `ladA-climb-01..08`, `ladA-up-01..04`, `ladA-top-01..03` and the `ladC` bursts show body 1 grab
    the ladder, climb it and go over the top onto the container roof while the viewer stands still (act 1).
  - `2596f6ae` **Mother Base ladder, play rehearsal, both directions** (`partner-traverse-ladder-mb.txt` as committed,
    `-Mode play -WanSim bad`, rtt ~185 ms, 0-15 % loss, real seed). `partner_icon=built` on both copies; the two host
    climbs on the guest and the reversed guest climb on the host each logged `icon; plugin=ladder; res=ok; exec_ok=1;
    granted=1; latched=0; viewer_changed=0; faults=0; restore_mismatch=0` and edge 21/21 `match_ms=0`; the same
    frames as the lab run in both directions, with the play HUD.
  - **Viewer safety:** no `viewer_changed`, `uncertain=1`, latch, fault or `crash_probe=av` line in any of the three
    runs (only `crash_probe=installed`); every viewer's own `partner_status=local` stayed `st=0x0` (the one `st=0x40`,
    on the host in `8f27806d`, came during the host's own climb); the viewer's local act was 1 in every capture.
  - **Found, open (cosmetic, not fixed):** while body 1 is on the ladder, the VIEWER's HUD shows the stock ladder
    hint "C + ladder" beside its own player (`8f27806d` `ladA-up-01..04`, `ladC-climb-07..up-04`; `2596f6ae`
    `lad-up-01..top-02` and the host's `rev-up-01..top-02`), although the viewer is on foot and the climber's own view
    mid-ladder (`lad-self-mid`, `rev-self-mid`) shows no hint. Likely source (Ghidra, read-only): body 1's
    LadderActionImpl state handler `0x1411EC3A0` case 4 calls `P+0xD8` (`UiControllerImpl`, vtable `0x1423AF358`)
    vt+0x260 = `0x140FE6D10` -> `[this+0x48]` vt+0xF8(3, 8, 0, 1, 0, 0) with no `P+0x218` check (its case 2 does check
    `P+0x218` before `P` vt+0x90). It has no input path: body 1's slide poll (`0x1411EC54A`) asks the padop's
    IsCompletedActionIcon with block 1's plugin, which never matches the viewer's slots. A fix would be a new scoped
    filter of that UI call for body 1 (a user decision; not attempted here).
- **Still open:** wall-climb (23) and pipe (22) proof. No crack-climb run was made this round: no candidate spot is
  known. Lead for finding one: the game's own Lua registers its crack points with the UI as a Vector3 list through
  `TppUiCommand.RegisterCrackPoints` (`0x1408D5010`, up to 64 points in the UI common data); none of the library
  scripts read with `tools/archive_probe` (TppMain, TppUI, TppMission, TppPlayer, TppQuest, TppTerminal, TppEnemy and
  others) calls it, so the caller is a location or mission script, and logging that call's argument would list the
  crack spots. D-Dog still takes the E press whenever it is the nearest action in 30010.

### F4

- **Gestures on the partner body** (`wp/gesture`, docs/native-puppet.md "Gestures"): lab key
  `partner_native_gesture` (a `_given` flag), **default on with the native gun profile in lab and play since the runs
  below** (it was 0 with a play force-off as merged). The sender samples its
  own upper act byte `Q+0x20` every tick and sends rising edges of 0x15 pickup, 0x17 knock, 0x18 / 0x19 hand sign Go /
  Stop as a new 12-byte message on reliable channel 2 (first byte 0x47; an older build drops it through its manifest
  decoder, `partner_action_events` v1 is untouched). The receiver gates the edge (native active, body 1 in act 1..6
  with `Q+0x28[1]` and `Q+0x20[1]` clear, no carry / seat / overlay / downed / vehicle, the remote live), picks the
  stock graph-2 node from the remote's stance and moving flag, and a MinHook on `0xB6C410` (only ActMotionStart's call,
  return `0xAF0381`, body 1's entry) submits it inside body 1's own motion update: `IsGraphExistNode(cs,1,2,node,{0,0})`,
  then `SetCurrentNode(cs,1,2,node,0.5f,0)` (pickup) or `ReserveToResetNode(cs,1,2,node,1.0f,0)` (signs, knock); the
  end (clip end, the remote's speed > 1 for signs, 2000 ms) returns graph 2 to stateNull only while it still shows our
  node. Nine SHA-256 pinned bodies, five cs slots checked before every call, SEH on every call, a viewer (body 0)
  digest around every write with a mission `uncertain` latch. Another owner's graph-2 request still waiting in body 1's
  request buffer is never overwritten (`refused; reason=pending`, `ended; cause=pending`, no stateNull); the window
  close (`PartnerShutdown`) turns the hook into a passthrough; the mission-boundary withdrawal logs the receipts it drains.
- New code: `native/partner_gesture_policy.{hpp,cpp}` (pure: kinds, nodes, wire, sender, store, gate, end),
  `native/partner_gesture.{hpp,cpp}` (the hook), `partner_player::ReadUpper`, driver `SendGesture` / `ReceiveGesture` /
  `TickGesture` and three Hooks (`read_upper`, `gesture_install`, `gesture_step`).
- Contract: 677 -> 690 code sites, 149 -> 156 vtable slots (`generate_partner_contract.py --check`), plus
  `gesture_assertions` (the kPins against the executable, the 0xAF037C call, every node an imm64 operand of its stock
  function, the 0.5 / 1.0 blend constants).
- Tests: `partner_gesture_tests` (nodes, wire, older peers, sender, store, gate, end) and `partner_gesture_hook_tests`
  (fake image: filter, submission ABI, end paths, refusals, the uncertain latch, the SEH fault) in CMake and
  `Get-WorkerTests`; `partner_driver_tests` TestGestures; the key in `lab_config_tests`, `lab_ini_check`,
  `friend_package_tests`, `worker_session_tests`, the three `-LabSetting` regexes, LabCommon and the help.
- Scenario `tools/worker/scenarios/partner-upper-gestures.txt`. A request no body-1 update took is logged as
  `refused; reason=unanswered; ...; body_calls=..; resolve=..`.
- **Hidden runs (wp/integ9, research build, gun stage, lab seed; Closed, BaselineRestored, RealSavesUnchanged, no
  visible fallback; the host picks up, the prone guest films body 1 from 4 m behind / 3 m right):**
  - `97ab39cc` lab (the scenario as written: E presses): no `partner_gesture=local` at all. Census `b9fa7639`: the
    swap panel over the dropped pistol shows the **R** glyph and E does nothing. The stock pickup polls the padop icon
    with key type 1 (`FUN_1412086f0` -> padop vt+0x28(plugin, 1, res)) = pad bit 0x40 (RELOAD, table `0x22E86A8`);
    over a dropped weapon a short R reloads and only the long press picks up. The scenario now holds R (scenario fix,
    no code change).
  - `ab16d947` lab (R press + R hold): the hold raised host `local; kind=pickup; upper=0x15; seq=1; stance=1` and
    `seq=2; stance=2`; guest `submit; graph=2; node=0x1e904603eb7d; exist=1; method=set; body_act=1` -> `applied;
    current=0x1e904603eb7d; ok=1` -> `ended; via=end; cause=clip; ms=625; null=1`, squat `0xff77b695370a` (`ms=609`).
    One frame inside the clip shows body 1 bent forward (`hold-01`).
  - `0e343f72` lab, the committed scenario (R held 1.2 s, guest burst during the hold): the same lines (`ms=656` both).
    **FRAMES (checked by eye): body 1 bends down at the waist reaching for the ground (`gest-pickup-03`) and comes back
    up (`-04`) while its legs stay planted, upright before (`-01`, `-02`) and after (`-05`..`-07`, `-end`); the
    viewer's own player stays prone (act 5) in every capture.** The squat clip is hidden by grass in its frames.
  - `75a7e7dc` play rehearsal (`-Mode play -WanSim bad`, rtt ~180 ms, 0-3 % loss), the same scenario **without** the
    key (scratch copy): `partner_gesture=configured; key=1` by default on both, the same lines (`ms=594` / `672`) and
    the same frames (`gest-pickup-03` reaching down, `-04` rising).
  - No `refused`, `uncertain`, `fault` or AV in any run; the viewer's `partner_status=local` never changed; the
    MotionVars rate-record writes (`re-traverse-gesture.md` 5.3 "Optional") were not needed: graph 2 renders on body 1.
- **Default flipped**: `partner_native_gesture` resolves to 1 with the native gun profile in lab and play
  (`ResolveWeaponDefaults`; the play force-off is gone), 0 without it, a named 0 opts out. Tests: `lab_config_tests`,
  `friend_package_tests` (Play-Coop native: 1|0; a named 0 in play: 0|1), help text, native-puppet.md.
- Still unproven: the squat clip in a FRAME, the moving pickup (W+R held over the dropped back weapon never raised 0x15
  on the host), the hand signs and the knock (no lab spot; E at the lab start pets D-Dog, act 94), the reverse
  direction (the guest picking up, the host filming), graph 2 taken back by body 1's own upper plugins (never seen).
- **User decisions:** a hand-sign / knock spot or trigger reachable from the lab seed (and a way past D-Dog) before
  those kinds can be shown in a FRAME.

### F5

Horse and D-Walker riders, branch `wp/mount`. Research first ([mounts.md](mounts.md)); no mount is drawn. Hidden
runs and the unrealize fix: see "Runtime" at the end of this section.
- **Still hidden, no mount drawn.**
  - The viewer's copy holds no GameObject for the partner's mount: one buddy block per copy, declared count 1.
  - The DM anchor `0x1198770` seats only GameObject types 50 and 4, not 13 (horse) or 44 (walker).
  - The horse rider states follow a live horse's state.
  - So any drawn rider would float. A real mount needs the private capacity-two buddy archive first (mounts.md
    section 7).
- **Finding: the D-Walker rider was never hidden.**
  - PlayerInfo `0x1412CE870` sets `ON_HORSE` only for act 121, and no Lua-bound `ON_*` status for the D-Walker
    drive act 125.
  - So `partner_ride.lua` reports a D-Walker rider (and the horse get-on act 122) as kind 0 (foot), and the viewer
    keeps it as an on-foot puppet.
  - The 0.13.4 line "horse and D-Walker riders stay hidden" holds only for the horse.
- **New key `partner_native_mount=off|hide`** (`_given` flag; since the runtime below: hide with the native gun
  profile in lab and play, off otherwise; a named off opts out).
  - With `hide`, the viewer also hides body 1 while the remote's act is 121..126.
  - Lines: `partner_mount=configured|hide|end`.
  - The key is wired into the three `-LabSetting` regex copies and help, `LabCommon`, `worker_session_tests`,
    `lab_config_tests`, `lab_ini_check`, `friend_package_tests`, and the native-puppet.md table and "Mounts"
    section.
- **Finding: RIDE_BODY (world op 42) moved the viewer's own idle D-Horse / D-Walker** onto the partner's mount
  path when both players deployed the same type. Turned off after the runtime by the user decision of 2026-10-22
  (see "RIDE_BODY fix" below; mounts.md section 2).
- **Lab probes and levers.**
  - Overlay probes 22..24 (`stateRideOnIdle`, `stateGallop`, `stateRideWalkerWalk`; `overlay_state`, debug
    only), to see whether a rider state plays on body 1 without a mount.
  - `walker_spawn` / `walker_status` / `goto_walker`. `walker_spawn` refuses with `player_mounted` while the local
    player rides (`ON_HORSE`, `ON_VEHICLE` or the `HORSE_IDLE`..`HORSE_GALLOP` gait statuses of the D-Walker drive).
  - Scenario `tools/worker/scenarios/partner-mount.txt` (since the 2026-10-22 reorder below: walker with hide,
    horse, both horses, walker baseline, then the lab-only probes; the runs here used the earlier order: probes,
    horse, both horses, walker with hide, walker baseline).
- **Act names:** 121 `horse_ride` (was `vehicle_passenger?`), 122 `horse_mount`, 123 `walker_mount`, 125
  `walker_drive`, 126 `walker_off`.
- **Tests:** new `partner_mount_tests` (CMake and `Get-WorkerTests`), `TestMountVisibility` in
  `partner_driver_tests`, and additions to the lab config, act log, overlay, `partner_sync` and `debug_buddy`
  checks. Contract unchanged at 677 code sites / 149 vtable slots.
- **Runtime (wp/integ9, research build, gun stage, lab seed; every run Closed, BaselineRestored,
  RealSavesUnchanged, no visible fallback, no AV, fault or uncertain line; the viewer's own act stayed 1 and its
  `partner_status=local` stayed `st=0x0` in every capture):**
  - `b264ad58` lab, `partner-mount.txt` as merged (key named on the guest): the lines as expected
    (`partner_mount=hide; mount=horse; act=121; kind=3`, `mount=walker; act=123; kind=0`, `end`, `foot_stable`), but
    a rock and the viewer's own model covered the host in every hide frame. The scenario now places the viewer beside
    / behind the rider and turns its camera (`mouse`), and films a second D-Walker ride from the other side.
  - `251abcef` lab (those vantage fixes): **the hide did not hide.** From the get-on to the dismount the partner
    body stood idle at the get-on spot, turned to the camera, without its name tag, for both D-Walker rides and the
    horse ride (the horse's kind-3 hide as well): `partner_bits=cleared; reason=ride` only clears the native realize
    bits, and Lua's all-instance-active mode keeps instance 1 realized (probe `rbits=0x3; parts=3,3`).
  - **Fix** (`partner_mount_policy::Unrealize`, `partner_driver.cpp` hidden reply): while a mount episode hides body 1
    and Lua reports all-active mode, the reply also carries the existing `CLEAR_ALLACTIVE` op (Lua
    `SetAllInstanceActiveMode(false)`, `partner_lua=event=allactive_cleared`); the show path's stage policy sends
    `SET_ALLACTIVE` again after the placement. One `partner_mount=unrealize; mount=..; act=..; episode=..;
    op=clear_allactive` line per episode. Tests: `partner_mount_tests` (`TestUnrealize`, the per-episode flag),
    `partner_driver_tests` `TestMountVisibility` (CLEAR while Lua reports all-active, none after the episode, SET on
    the show). Contract unchanged.
  - `bfd6fcc4` lab (the fix): **the hide proven.** Each episode: `partner_mount=unrealize`, `allactive_cleared`
    (probe `pool=00,ff; rbits=0x1`), then after the dismount `allactive_set`, `partner_realized=1` (look kept, no
    look timeout). FRAMES: the host standing (`walker-before`, `walker2-before`, `horse-before`), then no partner body
    anywhere on the guest's screen while the host mounts, rides, drives and turns the D-Walker (two vantages) or rides
    the D-Horse (`walker-mounting`, `walker-riding`, `walker-drive`, `walker-turn`, `walker2-*`, `horse-riding`,
    `horse-ride`), the body back at the dismount spot (`walker-off`, `walker2-off`, `horse-off`); the host's own view
    shows it in the walker and on the horse. **Lab default flipped**: `partner_native_mount` resolves to hide with
    the native gun profile in lab (`ResolveWeaponDefaults`), off otherwise; a named off opts out.
  - `fbde3dcc` play rehearsal (`-Mode play -WanSim bad`, rtt ~180 ms, 1-2 % loss; the scenario without the key and
    without the lab-only Part 1, against a build that defaulted hide in play too): the horse part passed as in
    `bfd6fcc4` (frames with the play HUD). Part 3's last E press mounted the host's horse (its first E had found
    none), so `host require localMounted=0` stopped the run before the D-Walker part. The D-Walker was unproven in
    play, so play kept forcing the key off until `5a044a4d` (below).
  - Part 1 probes (`251abcef`, `bfd6fcc4`): `stateRideOnIdle` / `stateGallop` drop body 1 into a seated / hunched
    rider pose at ground level with nothing under it (`code_match=0` although the pose changes);
    `stateRideWalkerWalk` shows the standing pose. Not a display; the rider stays hidden.
  - Part 5 baseline (the host with the key off watching the guest's D-Walker ride): body 1 walks on foot along the
    walker's path with no walker under it (`b264ad58`, `251abcef`, `bfd6fcc4`).
- **User decisions:** (1) **RIDE_BODY (world op 42) and the viewer's own horse.** When both copies deploy D-Horse,
  the viewer's own idle horse leaps and hangs in the air around the partner (`horse-viewer-own-*` in all four runs),
  and the partner's first E press found no horse to mount in all four runs (its own horse is apparently pulled the
  same way). **Decided 2026-10-22: turn it off** (the RIDE_BODY fix below). (2) The other ride hides (the horse
  kind-3 hide without the key; heli and vehicle fallbacks use the same bit clear, not filmed) still leave a standing
  body at the get-on spot; extending the unrealize to them needs its own frames.
- **RIDE_BODY fix (after the runtime, user decision 2026-10-22).** `lua/coop/world_ride_pose.lua` no
  longer applies a partner's horse or walker pose to any body that is not a registered mirror of the partner's mount
  (`R.mirrors`; none is registered in 0.13.5, and index 0, `6656` / `22528`, the copy's own declared buddy, can never
  be one). `R.decide` / `R.admit` refuse with `reason=not_mirror` (also for a vehicle-flagged horse / walker id), and
  `R.move` / `R.place` refuse the same bodies again; one `world_lua=event=ride_body;action=refuse;reason=not_mirror;
  id=..;kind=..` line per refused id. The sender, the wire, the vehicle path (RIDE_BODY's final pose, `RIDE_FAST`,
  `RIDE_TILT`, the seats) and the rider hide are unchanged. `world_ride_pose_lua_check` pins it (every horse / walker
  id refused through `decide`, `admit`, `move`, `place` and the op-42 handler; no `SetCallHorse` / `SetPosition` for
  15 partner poses; a registered index-1 mirror is the only horse moved; a vehicle's final pose still places it).
  **Confirmed in hidden runs `2ab88411` (lab) and `5a044a4d` (play)**, see "Runtime after the RIDE_BODY fix" below.
- **Scenario reorder for a play rehearsal (2026-10-22; run in `2ab88411` / `5a044a4d`).** `partner-mount.txt` now runs Part 1 D-Walker
  (host rides, guest hide; two vantages), Part 2 D-Horse (host rides, guest hide), Part 3 both D-Horse (RIDE_BODY),
  Part 4 the reverse D-Walker baseline, and the lab-only overlay probes last in a marked Part 5. A horse dismount E
  follows `host require localMounted=1` (Parts 2 and 3) and the second horse mount follows `host require
  localMounted=0`, so no E press can mount the host's horse by mistake (what stopped `fbde3dcc`), and the D-Walker
  evidence comes before any horse part. One buddy switch fewer (the host keeps its D-Horse for Part 4). The header
  says how to run it in play (drop the mount / overlay `labsetting` lines and Part 5, then `-Mode play -WanSim bad`;
  since `5a044a4d` both copies then run the play default, hide, and Part 4 is the reverse hide).
  `worker_session_tests` validates that play copy in play mode and pins the order, the dismount guard, the Part 5
  markers and the lab coverage (three probes).
- **Runtime after the RIDE_BODY fix (2026-10-22, `wp/integ9` `4f9396e` plus the play default change below; research
  build, gun stage, lab seed; both runs Closed, BaselineRestored, RealSavesUnchanged, `visible_fallback=false`, every
  require and expect passed; no `crash_probe=av`, `viewer_changed`, `uncertain=1`, latch or fault line; the guest's
  own `partner_status=local` stayed `st=0x0`, and the host's changed only for its own two horse rides):**
  - `2ab88411` lab, `partner-mount.txt` as shipped (guest named hide, host named off). Parts 1-2 repeat `bfd6fcc4`:
    two walker episodes (acts 123 and 125) and two horse episodes (121) on the guest, each `hide`, `unrealize`,
    `allactive_cleared`, then `end`, `allactive_set`, `partner_realized=1`. FRAMES: the host standing in
    `walker-before` / `walker2-before` / `horse-before`, no partner body in the mounting, riding, drive and turn
    frames (two D-Walker vantages and the D-Horse), back in `walker-off` / `walker2-off` / `horse-off`. **Part 3
    (RIDE_BODY off):** the guest's own idle D-Horse stands on the ground beside the guest in every frame from
    `horse-viewer-own-spawned` through the `horse-viewer-own` / `-ride` bursts to `horse-viewer-own-after`, and
    `horse-viewer-own-host` shows it standing at a distance, no horse in the air; **the host's first E mounted its
    own horse** (`host require localMounted=1`, `arrival=mounted`; host frame `horse-viewer-own-after` riding).
    Each copy logged one `world_lua=event=ride_body; action=refuse; reason=not_mirror` line per mount id (6656 kind 1,
    22528 kind 2). Part 4 baseline as before (body 1 on foot along the guest's walker path).
  - `5a044a4d` play rehearsal (`-Mode play -WanSim bad`, rtt 175-187 ms, loss 0-4 %; the header's play copy, no key
    named on either copy) on a build whose play default is hide: both copies logged `partner_mount=configured;
    mode=hide`. The guest's two walker and two horse episodes and the host's walker episode (Part 4, the guest
    rides) logged the same lines as the lab. FRAMES (play HUD): no partner body on the guest's screen while the host
    rides the D-Walker (two vantages) or the D-Horse, none on the host's screen while the guest rides its D-Walker
    (`base-walker-riding` / `-drive`), the body back after every dismount; Part 3 as in the lab. After each hidden
    episode the native drive left to clips for a moment (`partner_native_net; ... reason=net_quality`, the ride is a
    sample gap) and re-entered, as in `fbde3dcc`.
  - **Play default flipped** (`ResolveWeaponDefaults` no longer forces the key off in play): with the native gun
    profile `partner_native_mount` resolves to hide in lab and play, off without it; a named off opts out.
    `lab_config_tests` (hide in both modes, named hide runs in both) and `friend_package_tests` (Play-Coop native
    `hide|0`, a named off in play `off|1`) pin it. Run directories are archived under
    `%USERPROFILE%\mgs-coop-run-archive\integ9\` (D: is nearly full).
  - Still open: a drawn mount (mounts.md section 7); the other ride hides (the horse kind-3 hide without the key,
    heli, vehicle fallback) still only clear the bits.

### Final regression

On `b847585` (F5's commit, the last code change; the commit recording this changes STATUS.md only).
- **Static:** full CTest 288/288 (`-Configure`, build id `0.13.5+b847585`). `generate_partner_contract.py --check`
  gives 733 code sites / 185 vtable slots, and `generate_equip_writer_pins.py --check` gives 118 SHA-256 gates. The
  Research worker checks passed (receipt `20261022-113154-4d0a46756cbd432e850fd0d8b38b3a15`).
- **How the hidden runs ran:** research build, native gun stage, lab seed, the **default** lab settings (no 0.13.5 key
  named). The defaults were live:
  - `partner_ride=board_install; mode=block`;
  - `partner_cbox=mode; ...; enabled=1`;
  - `partner_gesture=configured; key=1`;
  - lab `partner_mount=configured; mode=hide`;
  - no `partner_icon=built` (traverse was still off on `b847585`; after the default flip of 2026-10-22 the F3 runs
    `04d3511f`, `8f27806d` and `2596f6ae` on `b9b391f` show `partner_icon=built` on both copies with the key not
    named; see F3).
- **What every run showed:** Closed, BaselineRestored, RealSavesUnchanged, `visible_fallback=false`. No
  `crash_probe=av`, `viewer_changed`, `uncertain=1`, `board_uncertain`, latch or fault line. The viewer's own
  `partner_status=local` changed only for its own presses or drives.
  - `2eb956ce` `partner-c4-v2.txt` (0.13.4: `646362fa`): passes.
    - Body 1 took 4/4 places (0.13.4: 3) and 2/2 detonations.
    - The guest's placed table stayed `owners=65535:0`, `owner1=-1` throughout.
    - No `placed_shared_write`, stock gap or leak, rec240 line, writer latch or `reverted=2`. Every support require
      passed.
    - FRAMES: body 1 kneels at P1..P4 (`c4-place-pN-03`) and raises the detonator (`c4-det-01`, `c4-det2-01`).
  - `b15c7bd4` `partner-support-play.txt`, `-Mode play -WanSim bad` (0.13.4: `bbee9f8e`): passes.
    - Four `throw_release` (three, then the last phase), and `viewer_own; changed=0; before=12; after=11`.
    - Every `stock_check` has `changed=0`; no gap, leak or latch; every require passed.
    - Play also showed `partner_cbox=mode; mode=play; enabled=1`, board block and gesture key 1.
    - FRAMES: body 1 winds up and throws (`grenade-throw-1-0N`, `-3-0N`).
  - `47e09b5f` vehicle seat, the scratch `vehicle-seat-rev2.txt` with `clear_enemies` (0.13.4: `4a9f053f`): passes.
    - Both directions: `seat_show`, then `confirmed; state=0xdfb7fc6173d0; ms=33|34`, then `exit; ...; confirmed=1`.
    - While the partner drove: `board_policy; vehicle=25608; seat=0; mode=block`. There was no `board_block`, since
      nobody pressed E beside it.
    - FRAMES: the seated driver in both directions.
  - `3abff2e6` `partner-overlay-states.txt` (compared with `873d7197`): passes.
    - All 14 guest states and both host states played and went back to native active.
    - The last one logged `stop; reason=debug` before its timeout.
    - The frames match `873d7197`: the stagger, ledge-hang arms, climb poses and die clip; `stateDead` standing; the
      CBox states crouched without a box.
  - `9dacaa70` `gameover-clock.txt` (0.13.4: `29fc8cab`): passes.
    - All five waitlogs matched (`held_ms=6573`, `host_continue` ok, the guest's kind-4 follow replay).
    - FRAMES: the pause menu, MISSION FAILED on both, both back in 30010.
  - `42a3c0da` `box-regress.txt` (scratch; 0.13.4: `28094e48`): passes for the viewer, the same as 0.13.4.
    - The guest is never `st=0x4`, and its `coop_carry` shows act 0x4D only on body 1.
    - The host's six `st=0x4` are its own boxes.
    - The 0.13.5 change: six `apply=native` (468, 472, 466, twice, `mapped=same`), six `entered; act=77` (100..122
      ms) and five `off; reason=ok` (the host ends boxed).
    - FRAMES: the three box models on body 1 beside the standing, unboxed guest.
  - `6e1a25d5` `partner-native.txt`: passes against its own 0.13.4 baseline.
    - Native mode was active in every capture: not latched, 0 snaps, the defer counters 0.
    - Stances and acts match. Run, sprint, crouch-move, crawl and g-run speeds are within about 10 %, and the leak
      captures read 0.
    - Aim `bit18=1` and leak-aim 0; the reload was entered and completed (adapt `k_milli` 1326).
- **Pre-existing, not a regression (not fixed here):** during the Ctrl+W walk, body 1 plays the run gait at 4.4..4.8
  m/s while the remote walks at 1.2..1.3 m/s.
  - The walk bit does arrive: `held=0x2200000`.
  - Recon (`runaway; action=pin`) keeps the body on the remote's track, so it runs in place.
  - The same scenario on the 0.13.4 build (`wp/integ8` `228eecb`, run `4f23f674`) shows the same: 3.1..4.8 vs
    0.95..1.25 m/s, with identical frames.
- **No regression, so no code change.**
- **Environment:** D: was full (0 bytes free) at the first `partner-native.txt` attempt (`c6b0712f`).
  - The worker's runtime.dat copy failed before the game launched. Closed, BaselineRestored and RealSavesUnchanged
    were still true.
  - This regression's own run directories were moved to the session scratchpad on C:, leaving D: with about 2 GB.
    That is not enough for many more hidden runs (one run takes 0.3..0.8 GB).

### Release regression

On `766aaad` (the last code commit before the package; F1 passenger camera, F3 traversal on, F5 RIDE_BODY off and
mount hide in play are all in it).
- **Static:** full CTest 289/289 (`-Configure`, build id `0.13.5+766aaad`). `generate_partner_contract.py --check`:
  743 code sites / 193 vtable slots; `generate_equip_writer_pins.py --check`: 118 SHA-256 gates. Worker checks
  passed: Research receipt `20261022-165122-6e46a2800ae3450c91b02700f485330a` (the runs below), Release receipt
  `20261022-165355-08fc9dab237a4cd3a3f3303a5f6f7526` (283 audited tests).
- **How the hidden runs ran:** research build, native gun stage, lab seed, the **default** settings (no 0.13.5 key
  passed on the command line; `coop-vehicle-shared.txt` pins `partner_ride_board=block` in its own file, as shipped).
  The defaults were live on both copies in every run: `partner_ride=board_install; mode=passenger` (block where the
  scenario names it), `partner_cbox=mode; ...; enabled=1`, `partner_gesture=configured; key=1`,
  `partner_mount=configured; mode=hide`, `partner_icon=built` with `census; ...; verdict=ok`.
- **What every run showed:** Closed, BaselineRestored, RealSavesUnchanged, `visible_fallback=false`, no expect
  failure. No `viewer_changed`, `uncertain=1`, `board_uncertain`, latch, fault, `placed_shared_write`, stock gap /
  leak or rec240 line. The viewer's own `partner_status=local` changed only for its own presses or drives.
  - `0bb25613` `partner-c4-v2.txt` (lab; `b847585`: `2eb956ce`): passes. The host's first spot did not take (three
    valid places this time, as in 0.13.4's `646362fa`); body 1 took 3/3 places and 2/2 detonations, the guest's
    placed table stayed `owners=65535:0`, `owner1=-1`. FRAMES: body 1 kneels at P2..P4 and raises the detonator
    (`c4-det-01` against `c4-ready-01`). The host logged the known teardown AV (`rva=0x21e72`, write 0x38, after
    `partner_shutdown=hooks_off; teardown=other_thread`) at WM_CLOSE.
  - `57eec21e` `partner-support-play.txt`, `-Mode play -WanSim bad` (rtt 173..188 ms; `b15c7bd4`): passes. Four
    `throw_release`, `viewer_own; changed=0; before=12; after=11`, every `stock_check` `changed=0`. FRAMES: body 1
    winds up and throws (`grenade-throw-1-0N`, `-3-0N`).
  - `c8b9046c` vehicle seat (scratch `vehicle-seat-rev2.txt` with `clear_enemies`; `47e09b5f`): passes. Both
    directions `seat_show`, `confirmed; state=0xdfb7fc6173d0; ms=34|38`, `exit; ...; confirmed=1`; while the partner
    drove `board_policy; vehicle=25608; seat=0; mode=passenger` (nobody pressed E beside it). FRAMES: the driver
    seated both ways.
  - `6a5280d4` `coop-vehicle-shared.txt` as shipped (block; `0348c4d8`): passes. Both directions `board_block;
    vehicle=25608; site=prequery; reason=block`, `vehicle_seats` -> `vehicle_not_riding`, the viewer's `st=0x400`
    only while it drove itself, no `reason=local_vehicle`. FRAMES: the viewer beside the partner-driven jeep, the
    jeep leaving with the partner, the reversed half the same.
  - `8cdfcfdb` `box-regress.txt` (scratch; `42a3c0da`): passes for the viewer. The guest is never `st=0x4`; six
    `apply` (468 / 472 / 466 twice), six `entered; act=77; ms=100`, five `off; via=native` (the host ends boxed).
    FRAMES: the three box models on body 1 beside the standing guest. Reported `Forced=true` at shutdown (as `59cfc415`
    and `de949f80`); Closed, BaselineRestored and RealSavesUnchanged true.
  - `e29c1043` `gameover-clock.txt` (`9dacaa70`): passes. All five waitlogs (`held_ms=6600`, `host_continue` ok, the
    guest's kind-4 follow replay). FRAMES: the pause menu, MISSION FAILED on both, both back in 30010.
  - `6ef9727c` `partner-native.txt` (`6e1a25d5`): passes. Native active in every capture, not latched, 0 snaps;
    stances and acts match; run / sprint / crouch-move / crawl speeds track the remote; leak captures 0; aim
    `bit18=1`; the reload entered and completed. The Ctrl+W walk shows the pre-existing run gait in place (4.1..4.7
    m/s against 1.2), the same as 0.13.4.
- **No regression, so no code change.** Proof frames (small JPEGs) are in the session scratchpad
  `complete/frames/release-0135/`; the run directories are archived under `%USERPROFILE%\mgs-coop-run-archive\integ9\`.

### Package and install

Package `mgs-coop-friend-0.13.5-20261022T173336Z-3944df14` (`dist/...zip`, 9,331,620 bytes, ZIP SHA-256
`2bcc902f82a12a4c59cf9238eed6ffc97f47fcac1d243e459aae5489e4b85611`), built by `tools/package.ps1 -Friend
-CheckReceipt` from `wp/integ9` `c929cda` (clean tree) with Release receipt
`20261022-173055-75ff966e3d8d470ca2865b7e55855b51` (283 audited tests, receipt SHA-256 `bf83240b...e342`); loader
SHA-256 `8bfeae657a497f34b3a25f258cd9782cb5f7fadf77fa6b9b03550c25274f8e08`, build id `0.13.5+c929cda`. The link
build id changed, so it will not connect to 0.13.4 or older packages; both players need this package.
- **Installed in both owned copies** (2026-10-22 17:34Z, no game launched): host through `Install-Coop.ps1 -Role Host
  -Character snake -NonInteractive -Yes` (kept the existing co-op save copy, real-save backup
  `real-save-backups\20261022-123418`, rollback `2a47b06b2492469a9b16d0dfaac18070`), guest through the owned
  `install.ps1 -NoInitialMode` (rollback `d9b3660a98974cf3a769cf7a7ff35064`; no `script-observe.enabled` left in
  `MGS_TPP_guest\mgs-coop`). Both `dinput8.dll` hash `8bfeae65...8e08` and carry `0.13.5+c929cda`; both
  `probe-install.json` say version 0.13.5 with that loader hash, the host's `friend-install.json` 0.13.5. Both
  executables, both Steam DLLs and all 17 save-folder files are hash-identical before and after.
- Replaces 0.13.3 (`bf29f67`, loader `1f3e6d12...`) in both copies; 0.13.4 was never packaged.

## October 21, 2026: 0.13.4 source (gameover clock + C4 v2 + the 0.13.3 rework, with the box apply removed)

Branch `wp/integ8` from `dev/oct7-continue` `bf29f67` (0.13.2): merge commits of `wp/gameover-clock` `8324dd2`,
`wp/c4-v2` `7e33abc` and `wip/agents-0133` `4914fa6` (the uncommitted tree the 0.13.3 package was built from), then
one fix-up commit. No rebase. Not packaged or installed yet; both copies still run 0.13.3, and input frame v7 (C4 v2)
plus the build id mean both copies need the same new package. Full CTest 282/282 (`-Configure`); contract
677 code sites / 149 vtable slots (`generate_partner_contract.py --check`, no regeneration).

What the fix-up changes against 0.13.3:
- **The cardboard box is no longer applied.** 0.13.3 asked Lua for `Player.ChangeEquip{playerIndex=1,...}` on every
  remote box toggle (op::cbox 1024). That binding (`FUN_1409D0D60`) never reads `playerIndex` and passes
  `R8D=[P+0x214]`, the local index (`0x1409D101A`), so it boxed the VIEWER's own player and the box ping-ponged
  between the copies (see the 0.13.3 entry). The Lua apply and its op::cbox consumer are gone; `NoteCbox` only logs
  `partner_cbox=remote; id=..; active=..; apply=off` and `StampCbox` is inert. The transport stays: manifest v2 bytes
  10..11 and byte 63 bit 0, the tick's ` item=`, the SendEquip box fields and `MgsCoopPartnerActiveItem`. A box toggle
  keeps the manifest revision and `SameSlots` no longer compares the box fields (`SameBox` does, so the store still
  takes the change), so a box toggle never re-runs the receiver's gun apply. A native apply for instance 1 comes later
  behind a lab key.
- **Traversal replay off.** `partner_native_traverse` defaults to 0 in lab and play (a `_given` flag like
  `partner_native_place_mirror`; play forces it off); its key is in the three `-LabSetting` regex copies,
  `LabCommon`, `worker_session_tests`, `lab_config_tests`, `lab_ini_check` and `friend_package_tests`. The ladder source
  gate is the game's own mask (acts 1..4, 25, 31, 32 from `FUN_1411E9A70`); see
  [native-puppet.md "Traversal"](native-puppet.md) for why the tap is unproven (the stock start goes through the
  local-only action icon, `0x141155B87`).
- **Act names** (`partner_act_log.cpp`, writer census): 22 pipe, 23 wall_climb, 24 step_on, 28 cliff_drop, 75/76 cure,
  79..83 trash_box (were heli?, ladder_climb?, fall_entry?, hop?, knockdown?, stun?); 21 is `ladder`.
- **Tests.** `partner_ride_wire_tests` is back for the 0.13.1 wire; `partner_traverse_tests` and it are in the audited
  `Get-WorkerTests` list; `TestNativeEnterAndLeave` asserts `sample_strip == 0` by default and `0x20` with traverse
  on; the Lua check's `ChangeEquip` stub now behaves like the binding (local index) and asserts no Lua apply.
- Version 0.13.4 (CMakeLists, package.ps1, probe.ps1, the Play-Coop / Play-LocalCoop / package-install /
  set-session-mode allow-lists, FRIEND-GUIDE).

Still open (no hidden-worker run on this merge yet): `partner-c4-v2.txt` and `gameover-clock.txt` on the merged build,
`partner-overlay-states.txt` (must not regress); the viewer boarding the partner-driven jeep still becomes a second
driver (the partner is then hidden); no passenger seat for the partner; horse and D-Walker riders stay hidden.

## October 18, 2026: 0.13.3 friend package, installed in both copies and played (box apply hit the viewer)

Package `mgs-coop-friend-0.13.3-20261018T213930Z-3204584b` (Release receipt
`20261018-213554-ab5af7b22f3f4bb4ac76fcff12745636`, 274 audited tests, ZIP SHA-256
`f06068efbe5baa9e493ee0cb8f7ec222e2df0698d7b4bca646866e81655d458d6`, loader SHA-256
`1f3e6d12073ad363c9bcef458a62dc7d48641edfb0f95d68538b7c852f4a4ae6`). Source commit `bf29f67` with a dirty
tree (saved later as `wip/agents-0133` `4914fa6`). The link build id is `0.13.3+bf29f67`, so it will not connect to
the 0.13.2 package. **Correction:** this entry first said "not installed"; it WAS installed in both owned copies and
played the same evening (early.log `early=start; version=0.13.3+bf29f67`, host line 3636, guest line 3546;
link-status.json `remote_build: 0.13.3+bf29f67` at 2026-10-18T21:44:23Z). No hidden-worker run. What that session
showed:
- Traversal: 0 `partner_traverse` lines and no remote act 21..24, so the replay never ran.
- Cardboard box: each remote box-on boxed the VIEWER's own player, never body 1. Guest
  `partner_cbox=request; id=468; active=1`, then its own `partner_status=local; st=0x4`, while the host's body 1 logged
  `edge=miss; remote=77(cbox?); body=1(stand_idle); held_ms=3003`. A re-request also knocked the local player out of
  its own box. Removed in 0.13.4.
- Vehicles: the 0.13.1 seat chain worked (partner drives, viewer sees it seated). A viewer boarding the partner-driven
  jeep took seat 0 as a second driver; the other copy then refused the partner seat (`reason=local_vehicle`) and hid
  the partner.

## October 18, 2026: 0.13.2 gameplay replaced (fixture-only; not a hidden-worker run)

Package `mgs-coop-friend-0.13.2-20261018T032136Z-bcd27cbc` (Release receipt
`20261018-031908-d344f7a10a714f57b029ed349de0dbeb`, 274 audited tests (the first version of this entry said 275 and
the commit 281/281 CTest), ZIP SHA-256 `8c53cf09fad9f312d5ced07178b279b2629ca17e3efd95dec6e1c629791c8d85`, loader
SHA-256 `f27f7d6aa9f9639ce4f24456d24f778722e121547affd02e3db9377c3a30e249`) shipped a climb whitelist, a box bit and
vehicle ACTION taps that were only fixture-tested. In play the box never called `Player.ChangeEquip`; the ladder tap
failed 3 of 3 (on the host a `partner_recon=place; reason=vertical` followed the tap by about 0.6 s, but on the guest
no placement followed and body 1 ran acts 1, 31, 32, 33 without reaching 21, so the placement does not explain it:
the stock start goes through the local-only action icon, see [native-puppet.md "Traversal"](native-puppet.md)); and
the mount/dismount taps never fired (0 `mount_action`/`dismount_action` lines: they sat after the vehicle early
return). The 0.13.1 seat chain (GetOn/Loop/GetOff) stays the vehicle path. Horse riders stay hidden: there is no
seated horse motion.

What 0.13.3 replaced it with (installed and played, see the 0.13.3 entry; corrected in 0.13.4):

- **Ladders, pipes and step-ons.** Acts 21, 22 and 24 (Ghidra writers LadderActionImpl, PipeActionPluginImpl,
  StepOnActionImpl on 1.0.15.4) place body 1, then fire up to 3 ACTION overlay taps 350 ms apart (not one) only after
  the window is armed again. ACTION is in the mask and in `sample_strip`, so the remote pad's E never arrives raw.
  Recon far/vertical/runaway placements are held for the climb. Acts 49/50/59 are not traversals. Never ran in play;
  off by default since 0.13.4.
- **Cardboard box.** Version 2 of the equip manifest carries `active_item_id` (bytes 10..11) and `cbox_active`
  (byte 63 bit 0). `EQP_IT_CBox` is 0x1C8; `IsVarsCurrentItemCBox` (`0x9D68E0`) accepts 0x1C8..0x1E2. The receiver
  asked Lua for `Player.ChangeEquip` with `playerIndex=1`, stock 0 and `dropPrevEquip` false. **Wrong:** the Lua
  binding (`0x9D0D60`) ignores `playerIndex` and passes the local index `[P+0x214]` in R8D, so it changed the
  viewer's own player. Removed in 0.13.4 (the transport stays).
- **Vehicle passengers.** Seats 0..3 already play that seat's GetOn/Loop/GetOff on the receiver's vehicle copy.
  Mount and dismount do not press ACTION. A passenger's aim while that direct motion owns the body is still the
  seat episode's early return: the aim tick does not run during it.

Vehicle GetOn/GetOff (0.13.1) is unchanged. The `owned_flag_map_tests` stack fix and the `FriendCommon.psm1` retry stay.

## October 17, 2026: 0.13.1 installed (vehicle entry `GetOn` and exit `GetOff` animations on by default)

Installed: package `mgs-coop-friend-0.13.1-20261017T114627Z-eea05a15` (Release receipt
`20261017-114346-7e1b71953874489b83fad76043815428`, 274 audited tests, ZIP SHA-256 `d53abae4...f2c7`) in both owned
copies, loader SHA-256 `7d2b8375...4bd1` (host through `Install-Coop.ps1 -Role Host -Character snake -NonInteractive -Yes`,
which kept the existing co-op save copy; guest through the owned `install.ps1`); both executables, both Steam DLLs and
all 17 save-folder files hash-identical before and after; no game launched. Source: `dev/oct7-continue` `5280e1d`
(280/280 CTest, contract 655 code sites / 146 vtable slots). The version is 0.13.1 and play refuses a build mismatch,
so both players need this same package.

**Vehicle boarding and dismount animations (`partner_ride_seat_enter=getin`, `partner_ride_seat_exit=getoff`).**
Ghidra decompilation of `RideVehicleActionImpl` (`0x1412471B0..0x14125C050`) and `DirectPlayMotionActionPluginImpl` (`0x141198770..0x141199860`) in `mgsvtpp_1_0_15_4_aug_4_patch.exe` (SHA-256 `085c2f82...bb45`) confirmed that both play the exact same graph-0 vehicle states (`stateVehicleGetOnFrontLeft`, `stateVehicleFrontLeftLoop`, `stateVehicleGetOffFrontLeft`, etc.) through `(*(PS+0x78)+0x238)->vtable+0x170(mgr, body_index, 0, state_code, blend, 0)` and anchor body 1's transform `*(PS+0x270)[1]` via `VehicleCommunicationInfo::FetchVehicleSeatLocalMatrix` (`0x140B7AF90`), whereas `RideVehicleActionImpl` also mutates the local vehicle's rider slots, turret ownership, disable-action flags, and camera. Keeping the 3-entry `DirectPlayMotionActionPluginImpl` chain (`GetOn` $\to$ `Loop` $\to$ `GetOff`) avoids all vehicle/camera side effects while playing the stock entry and exit motions. `partner_ride_seat_enter=getin` and `partner_ride_seat_exit=getoff` are now the defaults in lab and play (`kSeatGetInMs = 2500` ms, refreshed when a usable seat first arrives after an initial `seat=255` tick; mid-ride DM loss clears `get_on` so re-issued plays go straight to `Loop`; `SeatDmProbe` logs `when=loop` and `when=getoff`). Verified in hidden worker runs `6b4e1667bd994580818fe20d181b3b5d`, `bea50b8682754baa9bf94f7b482dac8e`, and `e10774b712824d12a12d0ace9d7b20d2` (both directions passing: `entry=7`, `entries=3; flags=0x0e,0x0f,0x0e`, `when=confirmed` on `0xdfb7fc6173d0` in 33-34 ms, `when=loop` on `0x489157d7d94f` with `err_mm=0..223`, `when=getoff` on `rec1a0=2` via `RequestToGoNextDirectMotion()`, finishing within 16 ms of the driver's own `act=112` dismount; frames `guest-seat-entering-01..05.bmp`, `guest-seat-exiting-04.bmp`, `host-seat-rev-entering-02..04.bmp`).

## October 16, 2026: the partner kneels to place C4 and presses the detonator (WS4 v2, lab)

**The teammate's body now kneels when the other player places C4 and plays the detonator press when they set it off.**
When the remote's own gun record enters the place (46) or detonate (47) mode, the viewer writes the matching request
bit into the partner body's own gun record just before its update (`partner_place_mirror`; the surface class rides on
a new input frame v7). The body's own game code then runs the kneel or the detonator motion. Nothing is placed or set
off in the viewer's world (the placement and the detonation run only for the local player), and the WS4 v1 digests
(placement latch, placed owner table, stock words) bracket every job. The first hidden run (`289badb6`) found one
shared write: a class bit plus a FIRE press in the same job made the decode register owner 1 in the viewer's placed
table. The digest caught it and turned support off. Fixed: the class is written only in a job without a FIRE press,
and is aligned again once the body kneels. Hidden runs `12320c2f`, `0d8190ea` and `96feab6d` (lab, research worker):
3/3 places and 2/2 detonations taken each time, the guest's placed table never moved (`owners=65535:0`), zero
`placed_shared_write`, unchanged counts. Frames show the kneel at every valid spot and the detonator raise right after
E, compared with a same-angle reference of the ready pose (it is brief: one frame at 150 ms). Lab key
`partner_native_place_mirror`: on with the native gun profile in lab (proven without a named key in run `98e99783`),
off in play (no play-mode C4 run yet). Contract
677 code sites / 149 vtable slots; full CTest 280/280. Merged into 0.13.4 (`wp/integ8`); not packaged yet
([the design](anim-round2-throws.md) "WS4 v2").

## October 16, 2026: world timers keep running under the MISSION FAILED menu

**After a shared game over the host's lab Continue now runs and both copies reload the checkpoint.** The game
holds its raw Lua clock (`Time.GetRawElapsedTimeSinceStartUp`) still under the MISSION FAILED menu and the ESC
pause menu while `OnUpdate` keeps running. The lab census run `65da90ce` showed the host's raw clock at 67517 ms
for 33 s of game over while `os.clock` and the update count advanced. Every world timer read that clock, so the
`host_continue` probe was scheduled but never started, and digests, stats, follow retries and holds froze until
a reload. `partner-native-checkpoint` had never passed (failing identically on 49af9ab, run `c5bd1ab1`).
`W.now()` is now the raw clock plus the wall time (`os.clock`) the game held it for, added once per update only
while the raw clock stands still (at most 250 ms per update, with any catch-up jump paid back), and logs
`event=raw_clock;step=held|resumed` ([protocol](world-sync-protocol.md) section 7, Clock). Hidden runs (lab,
normal worker): `partner-native-checkpoint` passes (`8a955580`: the probe runs 1 s after its trigger under the
menu, the guest replays the kind-4 follow, both back in 30010 at the checkpoint pose); the new
`gameover-clock.txt` passes (`9337b356`: a 6.6 s ESC pause held and resumed without a catch-up, both copies held
under the menu, Continue and follow); the new `gameover-manual-continue.txt` passes (`3412877e`: the real menu,
host Enter on CONTINUE FROM CHECKPOINT then NO on the lab save's Chicken Hat popup, the guest replays the kind-4
follow, both back in 30010). Real players were not blocked before the
fix: the menu Continue and the guest's first follow attempt need no Lua timer (pre-fix run `7ec64b80`), but a
refused guest attempt would never have retried and the 120 s follow timeout could not fire. Behaviour change:
the ESC pause also holds the raw clock, so world timers now keep wall time in a local pause too (digests and
downed status keep flowing; a partner's 3 s revive hold or 30 s sleep credit can complete meanwhile). Full
CTest 280/280. Branch `wp/gameover-clock` (from `wp/integ7`); merged into 0.13.4 (`wp/integ8`), not packaged yet.

## October 16, 2026: 0.13.0 installed (join fixes, save handoff, throws in play, C4/arm, vehicle seat and tilt)

Installed: package `mgs-coop-friend-0.13.0-20261016T163124Z-d00bc5c4` (Release receipt
`20261016-162824-bfaef91c021646e09afcf7dbe0dc5cc8`, 274 audited tests, ZIP SHA-256 `6e2192a7...b339`) in both owned
copies, loader SHA-256 `7452ec67...a8c6` (host through `Install-Coop.ps1 -Role Host -Character snake -NonInteractive`,
which kept the existing co-op save copy; guest through the owned `install.ps1`); both executables, both Steam DLLs and
all 17 save-folder files hash-identical before and after; no game launched. Source: `dev/oct7-continue` fast-forwarded
to `wp/integ7` `bebb316` (280/280 CTest, contract 655 code sites / 146 vtable slots). The version is 0.13.0 and play
refuses a build mismatch, so both players need this same package.

What 0.13.0 adds over 0.12.6 (details: [join-fix phase 1](join-fix-phase1-spec.md), [phase 2](join-fix-phase2-spec.md),
[save handoff](save-handoff.md), [throws and C4/arm](anim-round2-throws.md), [vehicles](vehicle-ws3.md)):
- The partner's mission is shown everywhere (HUD, F9, link-status, Play-Coop) with a 'different missions' note; the host
  helicopter hold has an on-screen message, a 'choose again to fly alone' escape and a 2-minute bound; a story-start
  co-op save is warned about at the title; Share-Save is documented and Install-Coop asks twice before replacing an
  imported partner save; Collect-Logs lists the co-op saves and keeps 3 sessions.
- Automatic save handoff: a joining guest whose save cannot meet the host gets the host's co-op save at the title and
  MGSV restarts once on it through Play-Coop (old copy kept aside, Share-Save [R]); travel lock: the guest follows the
  host's travel with soft refusals and escapes.
- Native partner grenade throws are on in two-PC play (stock guard, owner filter, viewer-own rebase, stall hold,
  receiver slot fix; WS1 rec+0x240 checks latch on a foreign row).
- The partner draws and holds C4 and holds Hand of Jehuty's aim pose instead of lowering the rifle (no kneel/detonate
  motion yet, no mirror of the charge in the viewer's world). The kneel and detonator motion came later in WS4 v2
  (lab only), merged in 0.13.4.
- Vehicles: the remote driver is seated by id in unnamed sortie vehicles (also at Mother Base), and the viewer's copy
  of the vehicle tilts with the driver's pitch and roll (RIDE_TILT).

Hidden runs on the integrated build: play-native `c74a4ef5`, throws default in play `67f13461`, C4/arm `ea2b96fa`, tilt
`05a6a6c7`, heli-alone `8c87caf7`, handoff transfer `db682ef9`. Known pre-existing issues: the shared game-over host
continue stalls under the MISSION FAILED menu (Lua clock; fixed by the held raw clock, `wp/gameover-clock`, merged in
0.13.4), and the lab seed's host shows infinite ammo.

## October 16, 2026: the partner holds C4 and Hand of Jehuty natively, on by default (WS4 v1)

**The teammate's body no longer lowers its rifle for C4 or Hand of Jehuty.** With the native partner (the gun
profile, lab and two-PC play) the loader now resolves `partner_native_placed=1` and `partner_native_arm=1`: when the
other player takes out C4 (391..395), their body draws it, holds it out in the place-hold / ready pose and puts it away;
with Hand of Jehuty (516) it holds the arm's aim pose (its floating hand and targeting beams) and never uses it. Nothing
is placed, fired or spent on the viewer's side: read-only digests of the viewer's placement latch, the placed owner
table and the arm energy, the stock guard and the support stock checks stayed unchanged in every run. Hidden runs (lab,
research worker): `7f8d643d` and `d05a1267` (frames of every phase from the side), the throws regression `6db902ce`
(3/3), and the earlier `4e5ab735` whose static watcher lost body 1 from the frame (scenario fixed). Not yet: the body
never kneels to place a charge or plays the detonation (the valid-spot and detonate requests are set only by the local
player's own probe; a v2 would mirror them), there is no mirror of the partner's charge in the viewer's world, and
mines, decoys and the other arms still lower the rifle. A named 0 (or `-Partner clips`) opts out
([the design](anim-round2-throws.md) "WS4 v1"). The default is proven without any named key: hidden run `197d73f2`
(lab, no labsetting) logged `partner_support=lanes; support=1; placed=1; arm=1` on both copies, the C4 and arm checks
unchanged, and its frames show the C4 hold and the arm's aim pose. Full CTest 279/279. Not packaged yet; play mode has
no C4/arm hidden run yet (it admits them by the throws' rule).

## October 16, 2026: native partner throws in two-PC play, on by default (Stage B)

**The teammate's throws now also animate in two-PC play.** With the native partner (Play-Coop's default
`-Partner native`) the loader now resolves `partner_native_support=1` in play as in the lab; play admits it only while
link v2 is connected, and a named `partner_native_support=0` (or `-Partner clips`) opts out. The play hidden rehearsals
with the named key passed before the flip (`117620b9` off, `e9244c56` bad, `733e023b` awful, the stall step
`33c11da9`, lab regression `8c7f3472`: 4/4 throws each, the viewer's counts unchanged, no false latch). The default
proof without the key passed: hidden run `a0d8edf6` (play, `-WanSim bad`, no `-LabSetting`) logged
`partner_support=mode; mode=play; reason=ok` on both copies and 4/4 partner throws on the guest with its counts unchanged
and no false latch; frames show each throw ([the throw design](anim-round2-throws.md) "Stage B"). Full CTest 277/277.
Not packaged yet; the installed 0.12.6 friend loader keeps throws lab-only.

## October 15, 2026: native partner grenade throws (lab / local test)

**Your teammate's body now throws grenades.** When the other player equips a thrown support item (EQP type 12:
grenades, smoke/stun/sleep grenades, empty magazines), aims and throws, their body on your screen holds the item and
plays its own native throw (wind-up, release, follow-through). Nothing is spawned on your side and your grenade counts
never change: a scoped stock guard puts the partner body on the game's own non-consuming remote-player branch, and an
owner filter at the thrown service's entry drops every request (real throw and arc preview) owned by the partner body.
Placed items (C4, mines) and the bionic arm still lower the gun (they write shared game state). This is on by default in
the lab/local two-window test (`partner_native_support`, the native gun profile in lab mode); two-PC play mode does not
admit it yet.

Hidden runs: `aaa2a0e0df66419e8d026f029417f3da` (GPT's v1: both throws visible, 229 partner requests dropped, none
passed, guest counts unchanged) and, after the review fixes, `ea93a2e38edf41d780f1ce275d1b0f40` (the guest's exact
counts required unchanged after every throw and while it aimed its own grenade; 317 partner requests dropped, the
guest's own 80 arc previews forwarded). The review (`archive/research-2026-10-23:docs/evidence/rea-throws-2026-10-15/review-1ca3ce6.md`) found the
guard only covered drain-window jobs; it now covers every body-1 job. Full ReleaseGun CTest 273/273. History of the
blocked attempts and the REA/Ghidra audit: [the throw design](anim-round2-throws.md).

Installed: package `mgs-coop-friend-0.12.6-20261015T233337Z-8ec568ed` (Release receipt
`20261015-233107-b5f9df5644f04116ac88dfccedbfeb00`, 263 audited tests, ZIP SHA-256 `c01d8cd9...5ae4`) in both owned
copies, loader SHA-256 `76cb0e89...65b2` (host through `Install-Coop.ps1 -Role Host -Character snake`, guest through the
owned `install.ps1`); both executables, both Steam DLLs and all 17 save-folder files (13 real saves) hash-identical
before and after; no game launched. The version stays 0.12.6, so both players need this same package.

GPT's original note (v1, before the review): `wp/anim-throws` implements `partner_native_support=1` for type-12
slot-3 equipment. A scoped stock guard, thrown-service entry owner filter,
non-local native equipment writer/follower and per-window input gate isolate body 1;
read-only support/inventory checks latch it off on a change. Placed equipment and
arms remain unsupported. The explicit p218 diagnostic and required private
whole-player pose window refuse support because they violate its non-local audit.
Static contract verification passed for executable 1.0.15.4 SHA-256
`085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45`.
ReleaseGun build and all **253/253 CTest tests** passed, including the friend-package
fixture. Results, patch bytes and the prepared scenario are recorded in
[the throw design](anim-round2-throws.md).
(Superseded: the hidden runs above verified animation, counts and item visibility.)

## October 15, 2026: the remote driver is drawn seated; partner weapon and stance fixes

**Driver in the seat.** A teammate driving a light vehicle or truck in Afghanistan or Africa free roam is now drawn in the driver's seat of the viewer's own copy of that vehicle (`partner_native_ride=seat`, the new driven default). The receiver keeps the partner body realized and plays a targeted direct motion on it, the engine's own seat mechanism that retail mission s10150 uses for Snake's jeep ride: `{{"coopRide",1},{"stateVehicleFrontLeftLoop",true,"veh_lv_0000","CNP_ppos_c","MTP_GLOBAL_C",true}}`. The engine re-anchors the body to the vehicle copy's seat matrix every frame (`0x1198770` -> `0xB7AF90` -> `PS+0x270`), so it follows the vehicle that `RIDE_FAST` moves. Four reversible code dwords are armed only for the seat episode: the anchor's seat index (`0x119883C`) and three skips over the direct-motion camera-connect stores (`0x1198D56`, `0x1198D81`, `0x11992E9`), so neither the seat's entry nor its end touches the viewer's own camera connect. The sender adds the vehicle id, native seat (from `GetRiderId`) and a PlayerStatus bitmask to its receipt; the ride wire is v2 (v1 still accepted). Heli, horse, D-Walker, APC/tank, unnamed vehicles and stale rides still hide. Hidden runs: `8b8e5bbc2acc4b2a967da44aa1aaab6f` (the guest's body seated in the jeep's driver seat on the host screen, hands on the wheel, while it drives) and `77d24c5e1ac74552a55450a7b0307966` (watched from the Wakh Sind plaza: Snake seated while the jeep drives in and parks, then climbing out); the body tracks the remote's seated pose to about 1 mm (`partner_ride_seat=status; ...; err_mm=1`). Rider `9728` in the census is D-Dog (`TppBuddyDog2`), who rides in his owner's passenger seat on the owner's copy. Not yet tested: both players driving at once, passenger seats, trucks, Africa vehicle names, the optional get-in/get-off chaining.

**Partner weapons.** When the remote holds a support weapon, item or the bionic arm (slot 3/4/6), the partner no longer raises its old rifle (`partner_weapon=unsupported; ...; strip=0x58`). Its aim follows the remote's aim state and aim act, not only the raw aim button, so toggle aim and the end of an aim show. Visual fire stops when the remote's magazine is empty and resumes at its reload. The partner's magazine base is rewritten on every weapon switch (the old `41/11` handgun reading is gone). The partner's reload length now adapts to the remote's (`partner_native_reload_rate=adapt`, the gun-profile default): 3018 -> 2285 -> 2082 -> 2051 -> 2035 ms against the remote's ~2020 ms, handgun 1242 vs 1223 ms. Hidden runs `204b5d547faf48da8ea89a7e485b1b59` (fire census: grenade 354, magazine 349 and Jehuty 516 gated; the host's grenade goes through the thrown service with owner 0), `6163920a2b104de1b2c560b998e03d8c` and `8ca5da4a50a14905bb61582999250149`.

**Stance and actions.** Crouch, prone and stand now follow within about half a second instead of 1.5-2.5 s, and an edge-triggered `partner_act` census and per-second `partner_pad_dropped` counts are logged for diagnosing what remains (`5b7221a6c7db4458b7364f25f82e75ca`: crouch 8/8, prone 6/6, dive 3/3 matched). A direct-motion overlay lever plays damage staggers, the ledge hang, ladder poses and a lying clip on the partner (`873d7197a16a44dc83dec7133f8b6446`); `stateDead` shows nothing and the box pose has no box model. The overlay stays **off** by default.

**Not done.** Native grenade throws, C4 placing, launcher fire and arm abilities for the partner were stopped twice by a safety classifier (`wp/anim-throws`, `docs/anim-round2-throws.md`): static analysis also showed the partner's native throws would spend the viewer's own shared grenade stock. Vault, climb and ladder still need a census at real spots. Details: `docs/anim-round1-integ.md`, `docs/anim-round1-seat.md`, `docs/anim-round1-weapons.md`, `docs/anim-round1-actions.md`. Full ReleaseGun CTest 269/269.

Installed: package `mgs-coop-friend-0.12.6-20261015T125018Z-402381a3` (Release receipt `20261015-124725-02a0ea189acf4d04893ba092438a4a3e`, 263 audited tests, ZIP SHA-256 `5985ea4c...aa51`) in both owned copies, loader SHA-256 `f3dd0621...0bbb` (host through `Install-Coop.ps1 -Role Host -Character snake`, guest through the owned `install.ps1`); both executables, both Steam DLLs and all 17 save-folder files (13 real saves) hash-identical before and after; no game launched. The version stays 0.12.6, so both players need this same package for two-PC play.

## October 14, 2026 (night): carried camera and the sleeping teammate's stray prompts

**Carried camera.** A carried player's view sat low beside the carrier, inside the bodies. The carried player now gets the stock non-animation camera (`Player.RequestToPlayCameraNonAnimation`, the layer the mission-clear and game-over shots use) aimed at the carrier (player instance 1) from 3.8 m behind and a little above. `Player.RequestToStopCameraAnimation` returns the normal camera after put-down, cleanup or link loss. A one-tick role dropout does not restart the shot, and a refused request is retried after 2 s. `followTime` is how long the shot keeps tracking its target in seconds (retail cutscenes use 3-7). With 0 the shot stayed where it started while the carrier walked away (run `9d08106b35be49ca877f7c761115db0c`). It is now 600 s and renewed while the carry lasts. In hidden run `dbb5b57eac50475db3e37ad708bb926e` the shot stayed behind the carrier through 2.5 s of walking and a turn, then returned to the normal camera after put-down.

**Sleeper's view in dark greyscale.** The sleeping player's light grey wash (an ImGui rectangle, which lightened the frame) is replaced by a full-screen pass. It copies the finished game frame and redraws it as greyscale at half brightness with a soft vignette. The pass runs from an ImGui draw callback queued first in the frame, so the backend restores its state, and the teammate's name tag and the `ASLEEP`/`CARRIED` status draw over it in colour. The game's back buffer is `R8G8B8A8_UNORM_SRGB` (`overlay=sleep_view; format=29; srgb=1; samples=1`), so the shader darkens in display space. Darkening the linear values (run `1be8bed7c825430e97e43fa618c3b6ee`) left the sky at about 160-175 of 255; the display-space version (run `dc2f33c97a7a41ca891d0ceb45ea9ca0`) brings it to about 85-110. A refused shader or frame copy falls back to a dark wash. The overlay fixture checks a green test frame turns grey, dark and darker at the edges, while a healthy player's frame keeps its colour.

**Stray prompts on a sleeping teammate.** The healthy player saw two prompts the wounded case does not show: the stock Fulton offer and the sleeper's own struggle guide (stick + E, `!`).

- *Struggle guide.* The player UI object has five prompt queues: vt+268/288/270/278/280 at `0xFE8770`/`0xFE89E0`/`0xFE8700`/`0xFE8C70`/`0xFE8690`. All five share one body over the field blocks `0x3C0`/`0x410`/`0x460`/`0x4B0`/`0x370`, and none checks which player instance owns the request. Instance 1's sleep update queues hint 93 (`0x5D`) on vt+268 from `0x12CAD45`; carried, it also queues hint 94 from `0x1127437`. Any prompt queued from inside the validated instance-1 job now stays off the local HUD (`teammate_partner_prompt`). The check uses the same player/UI ownership chain as the existing Quiet hint filter, but accepts any producer and any queue. The sleeper still sees its own guide. In worker builds the private native pose window already hooks vt+268 (`PoseSleepHint`), so that queue stays with the pose window, which forwards its other requests through the same filter. `tools/generate_native_ui_contract.py` pins the four added bodies and vtable slots (41 sites).
- *Fulton.* The stock Fulton offer covers any unconscious body. While carrying, E stays native for horse loading, so the stock Fulton could actually extract a carried sleeping teammate. The Fulton action is now withheld (`vars.playerDisableActionFlag` gains `PlayerDisableAction.FULTON`) while a downed teammate is within 3 m or carried. Only a bit the mod added is removed again, so a mission's own Fulton lock is kept.

Hidden runs `9d08106b35be49ca877f7c761115db0c` and `dbb5b57eac50475db3e37ad708bb926e` (lab defaults, friendly dart): from 11 m the host's only stock prompt is Pet D-Dog, with D-Dog beside the host. Beside the teammate it shows only the stock carry icon and the mod's `HOLD / WAKE UP`. Carrying shows neither the Fulton offer nor the struggle guide. The host log records the suppressed hints 93 and 94 on queue `0x3C0`. The carried guest's view showed the carrier from behind, the follow camera stopped after put-down, and the wake restored 6600/6600 HP. An earlier attempt, run `1d3e80b4b4e14e12915d77d04ac9dbba`, never reached gameplay: the new vt+268 hook changed bytes the private pose module verifies (`pose_full_code`), so the worker refused. All three runs restored the baseline loaders, and the real saves were unchanged. Checks: all 264 parallel CTest tests and `lab_scripts_tests` passed; `friend_package_tests` was not rerun because of the known OneDrive lock. Not yet covered: a wounded carried teammate's camera, the reverse roles (the host carried), and a live two-PC session. [Evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/carried-camera-partner-prompts-actual-2026-10-14.json).

Installed: package `mgs-coop-friend-0.12.6-20261014T235803Z-73f514f3` (Release receipt `20261014-235549-75c3eb274ab445b581fb5c2b211c626d`, ZIP SHA-256 `cc3a79bb...9d10`) in both owned copies, loader SHA-256 `d4a1807e...cbbb`; both executables, both Steam DLLs and all 13 real save files unchanged. It replaces the same evening's `20261014T232700Z-f62273b9`, which had the light sleeper wash. The version stays 0.12.6, so both players need this same package for two-PC play. Release builds have no native pose window, so they hook the vt+268 queue directly; no hidden run can exercise that path, because worker builds always leave that queue to the pose window.

## October 14, 2026 (evening): fixes from the first 0.12.6 local test

The user's local two-window test (both copies on loader `1497f2ba...`) found five problems. Their logs explain three:

- **No rescue prompt.** The local test runs the world in `lab` mode, where the co-op HUD defaults to off; the session log shows `nametag_drawn` but never `hud_drawn`. The hidden runs had forced `coop_hud=compact`. The rescue prompt is gameplay UI, so it now draws with any link v2 session (`coop_hud::RescueEnabled`), while the status line and toasts still follow the HUD mode. The overlay fixture renders it with the mode off.
- **D-Horse summon refused.** All four attempts returned `buddy_block_inactive` in Afghanistan (30010): with no buddy deployed, `TppBuddy2BlockController.IsBlockActive()` is false and `CallBuddy` has nothing to call. The summon now deploys D-Horse the way retail free-roam scripts do (`vars.buddyType = HORSE`, `ReserveCallBuddy(HORSE, NORMAL, position, yaw)`, `Load()`), then waits up to 900 frames for the block and the horse.
- **Sleeping player's black screen.** The stock unconscious view is a global player presentation mode requested through the P+C0 setter (`0x11F6520`, `mov [rcx+0x6d], dl`) that `coop_carry` already hooks; it kept an awake carried player's view but not a sleeper's. The hook now also drops nonzero presentation requests while the local player is asleep (`coop_carry::SetLocalAsleep`, cause 2), and the overlay draws a grey wash under the `ASLEEP` status. Hidden run `9536472b46f441ac852666ae75f94810` (lab defaults): friendly dart, the sleeper's own view stays visible and greyed on the ground and while carried, then a normal Action wake at 6600/6600 HP.

Verified with the lab defaults (no `coop_hud` override) in hidden run `f9b5857513a84d05b947a51ce38a40a9`: rifle down, `[E] HOLD / REVIVE` beside the host, `WOUNDED / Teammate 3 m` for the guest, carry, put-down and revive to 6600/6600 HP. The horse deploy path is not exercised by the lab saves, which already have D-Dog deployed.

Installed: package `mgs-coop-friend-0.12.6-20261014T215817Z-36b03dd6` (Release receipt `20261014-215556-3bb22a3f281d494baa5452e497219719`, ZIP SHA-256 `bfc290c8...5c9b`) in both owned copies, loader SHA-256 `1edbaad0...c6ff`; both executables, both Steam DLLs and all 13 real save files unchanged. The version stays 0.12.6, so the local launcher upgrades both copies' loaders by hash; both players need this same package for two-PC play. `friend_package_tests` failed twice on `File.Replace` ("Unable to remove the file to be replaced") inside its OneDrive-synced fixture folder; that is the known OneDrive lock, and the Release receipt's own checks passed.

Still open: the carried player's camera stays low beside the carrier. The stock manual around camera (`Player.SetAroundCameraManualMode` with an explicit target, run `1bd17c533e03406eb5627a4c398377d2`) returned success but had no visible effect inside the carried action, so it was removed; the carried camera needs a native fix. The remote driver is not drawn in the seat, and the vehicle copy stays level on slopes because `TppVehicle2 SetPosition` carries only yaw (pitch and roll need a native full-orientation write).

## October 14, 2026 (afternoon): smooth vehicles, new rescue prompt, MGO down rules

**Vehicles move smoothly on the other screen.** The driver's game now streams its vehicle pose 20 times a second over a new datagram op, `RIDE_FAST` (sender tick, vehicle id, position, yaw). The other game keeps a short sample buffer, renders the vehicle 100 ms behind the newest sample, interpolates position and yaw, extrapolates up to 200 ms, and places its copy with the stock `TppVehicle2 SetPosition` every frame. A reliable `RIDE_BODY` carries one final pose after the driver gets out. Before, the vehicle was hard-placed 4 times a second over the reliable queue, which looked like teleporting. Hidden run `57a660a530bd4a6fafac61cc84d9a1b5`: the host sent about 20 poses per second, and the guest placed its copy 50-58 times per second while the jeep moved; burst frames 150 ms apart show the jeep crossing the view in steady steps. The driver is still not drawn in the seat on the other screen: the partner body stays hidden while its player rides.

**Correction to the previous mirror diagnosis.** In run `4d4286bb7254490da1768c9ad30f97b7` both private vehicle mirrors reported `rx=0` and `tx=0` after the host was seated (phase 3). The host never sent a mount message, so the guest's ordering against the generic ride path was not the first failure. The next mirror step is the host's `TickLocal` admission (authored-name resolution of vehicle 25608 and its other preconditions).

**Rescue prompt redesigned.** The top-center text box, key-cap row and log sentences are gone. A healthy player now sees a small status pill under the teammate's name tag (`ASLEEP 11 m` / `WOUNDED 24 m`). Within 3 m, an Action button with `HOLD / WAKE UP` or `HOLD / REVIVE` appears beside the player's own character, clear of the stock Fulton/Carry icons, and a ring around the button fills during the hold. The downed player sees `ASLEEP` or `WOUNDED`, what the teammate can do, and the teammate's distance, above the stock sleep/crawl icons. The button glyph follows this game's last input: the `E` key cap after keyboard/mouse, the yellow `Y` face button after controller input. The HUD and name tag use Windows' Bahnschrift (DIN-style), falling back to Segoe UI. Runs `57a660a5...` and `7a972183b84b4f4fb0ebbd59d3ac779e` captured every state in game; the first run's overlapping placements were moved and the second confirmed them.

**MGO down rules reused.** MGO's ruleset exposes its down-state tuning by name in `mgsvmgo.exe` (SHA-256 `f9a1c190...47d4`): `CanBeKilled`, `EnableAbortWait`, `HealthPercent`, `MaxWait`, `MinWait`, `CallForHelpInterval`, `CallForHelpTimerate`, `CallForHelpRadius`, `MaxReviveTime`, `RateIncreasePer`, `RateDecrease`, `RateMax`, `InvincibleTime`. Two rules are now in co-op: an interrupted revive or wake drains at the fill rate instead of restarting (the same reviver on the same downed generation resumes from what is left; stale, paused, time-reversed and native-loss interruptions still reset), and a revived player gets 3 s of damage protection. MGO's player sleep uses the same native controller and lying rest as TPP's stock player sleep, which co-op already uses; `MGO_ZZZ` is MGO's kill-feed icon for a sleep takedown, not a world effect. No MGO code or addresses were applied to TPP. The guard-style upright doze is a soldier animation, not MGO's player sleep, and remains unselected.

Build: a world Lua chunk may now be up to 32000 bytes; CMake emits a chunk over 16000 bytes as two adjacent raw literals so each stays under the MSVC literal limit. Checks: full CTest suite passed (two PowerShell fixtures pass serially but can collide when run in parallel). D: had 0 bytes free and stopped a hidden run; 200 old, unreferenced worker install backups (3.15 GB) were moved to `%USERPROFILE%\mgs-coop-archive`, keeping every folder in the current rollback chains. [Evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/vehicle-smooth-rescue-prompt-actual-2026-10-14.json).

**Installed 0.12.6.** Release receipt `20261014-205237-c617fc25f8c54985910f93f4c70ba47f` passed 263 audited tests with the worker off and the same experiment flags as 0.12.5 (the package's flag list now also names the five private observer options, all OFF). Package `mgs-coop-friend-0.12.6-20261014T205431Z-b88a5de1` is installed in both owned game copies: loader SHA-256 `1497f2ba...026b`; both executables, both Steam DLLs and all 13 real save files hash-identical before and after; no game launched. The source `Play-Coop.bat` selects this package. [Release evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/release-0.12.6-2026-10-14.json).

## October 14, 2026: native rescue installed; riding and standing sleep under qualification

The playable installed rescue release remains **0.12.5**; source **0.12.6** contains private experiments whose activation stays off in friend builds. Completed research receipt `20261014-033939-50b1e531e0bb48eb9e6aa0ede9f762e3` passed **261 audited tests** and **124 worker-policy checks**. Subsequent improved entry/stack diagnostics are being checked separately. Source and fixture checks do not establish shared riding.

Actual run `4d4286bb7254490da1768c9ad30f97b7` passed 11 requirements: both private vehicle mirrors enabled, the host entered the authored jeep through normal Action input, and the host retained 1912 protected pose windows with fault 0. The private foot-only camera filter exercised the exact body-one mode-4 leaf. The guest stopped after its generic ride path disarmed the input owner before an authenticated native vehicle demand was available. Measured guest native-job drains remained below the 4000 microsecond limit. This is an ordering/refusal issue, not an accepted shared ride or a cost-limit failure. [Actual host entry and guest refusal](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-vehicle-host-entry-guest-refusal-actual-2026-10-14.json).

Actual run `b8721082fc5549d99d313ef740f3287e` passed six requirements covering a real friendly sleep dart, native rest, continuous three-second Action wake and health-preserving recovery. The candidate four-motion bank loaded, while original stock motions remained selected. The first native-frame observer recorded zero filtered calls; this did not distinguish an unused callback from rejected payloads. Improved diagnostics count every actual entry and capture the existing native setup stack independently. Upright selection and asynchronous resource retirement remain unqualified. [Actual sleep/wake and initial frame observation](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/mgo-standing-native-frame-zero-callback-actual-2026-10-14.json), [Earlier candidate bank loading](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/mgo-standing-cycle-bank-loaded-actual-2026-10-14.json).

Horse research found the original Player/Soldier carry-target distinction, the native horse-load finder rejection for Player cargo, and a transient handoff bit that must be observed at the actual finder call. The stock NPC comparison now avoids mandatory spectator pose windows. Run `3560409635c24376b2db4ce229c8b791` passed ten setup requirements but the game's original pickup predicate rejected a negative facing dot before and after Carry input. The same current guard was within range and asleep; no pickup, horse transfer or attachment was accepted. The next test corrects actual native facing before evaluating horse handoff. [Original pickup predicate refusal](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-horse-npc-current-facing-refusal-actual-2026-10-14.json), [Earlier lifetime refusal](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-horse-npc-settled-lifetime-refusal-actual-2026-10-14.json).

Read-only horse/NPC observations are admitted only by the compiled private worker; normal release play adds none of their diagnostic memory reads. All cited runs closed their owned processes, restored all 63 protected paths and preserved real saves. The previously verified two-copy pickup, carrying, put-down and wake/revival remain accepted and installed. Fresh two-PC Steam acceptance, native horse cargo, shared horse/vehicle passengers and upright sleep selection remain outstanding.

## October 13, 2026: native rescue verified; mounts still being qualified

Source candidate **0.12.6** continues private riding and animation research; the installed/playable package remains **0.12.5**. Research receipt `20261013-235656-b7cb924466684b3898e5a1e957d1dfb7` passed all **238 audited tests**. The distinct native vehicle-purpose core, physical mirror, camera/lifetime protection, authenticated reliable-message origins and original carry-hook ownership are compiled. Normal release activation remains off; these checks do not establish shared-riding acceptance.

The carry/camera composition fix passed actual hidden run `d13fd5d3981c43d1a4257566da3bcaa9`: both copies captured more than 600 protected pose windows with fault 0, and both private vehicle mirrors enabled. A later native cost fallback and long-range debug teleport invalidated telemetry before vehicle entry. The test is retained as partial evidence; reducing idle mirror work and pre-positioning both players are the next checks. No lifetime/generation checks are weakened. [Actual camera composition and later refusal](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-vehicle-carry-camera-composed-actual-2026-10-14.json).

Read-only run `d6228c5577264f60a47e18b98f350e73` captured the exact original initial bank append on both copies: capacity 32, all slots empty before the call, and stock player bank bound at slot 0 afterward. The unique bank was absent at that boundary and loaded later. This distinguishes a real constructor boundary from the already-bound common-bank callback; no append or selection authority is granted. A scoped original lookup observer and a separate complete four-motion standing-cycle profile are under qualification. [Actual initial-bank trace](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/mgo-raw-constructor-actual-2026-10-14.json).

Run `2d94f36436244667b475b3583cd6d01a` passed actual friendly sleep-dart and native shoulder-carry checks on both viewers. The host's current Horse/Player census also passed beside D-Horse. The guest's horse summon correctly refused while asleep, and holding Carry beside the horse left the teammate on the carrier: no horse attachment or put-down was accepted. The scenario's `native-putdown` capture label is not a successful outcome. Native horse loading still needs its own dispatch/attachment/retirement proof. [Actual carried source beside D-Horse](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-horse-cargo-carried-source-actual-2026-10-14.json).

All three hidden runs closed only their owned processes, restored all 63 current protected paths and preserved real saves. They used the private desktop and in-process input; no visible game windows or system input were used.

The corrected original animation getter was observed in hidden run `d267c618cc414ec68a2b1574964117bb`: both copies/bodies used the actual controller row and final engine vtable `24c1988`. Their gameplay creation loaded the exact unique MGO bank at common slot 17, but already had one engine bank bound. Run `b725bbce840941e39887ed7779ae8ecc` then tested a real friendly sleep dart and normal teammate wake, preserving 6600 HP. Both selected readers still proved original TPP origin 1 / rest index 1273. Exact native lookup returns the first matching engine bank, so later additional registration does not replace the resident animation. Both runs restored all 63 current protected paths and real saves; no append or upright-sleep feature was enabled. [Getter and bank evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/mgo-unique-getter-bank-actual-2026-10-13.json), [Actual selected sleep source](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/mgo-unique-natural-selection-actual-2026-10-13.json).

The previous claim that player pickup cannot be added was incorrect. The game already contains a player-carries-player route. Scoped reuse of its natural search, action admission, request, carry locomotion and put-down now carries a wounded or sleeping teammate on both copies. No global online flag is changed. Two hidden runs passed normal input pickup, walking, put-down and a three-second Action wake/revive: sleeping guest `8d251aaa31d94b22a3b74bf18c751e66`, wounded guest `b52311eefe8d445bb0327eb0647b3570`. Both restored the current 63 protected files/absences and preserved real saves. These are actual local two-copy tests, not fresh two-PC acceptance. See [sleep evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-rescue-sleep-wake-actual-2026-10-13.json) and [wounded evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-wounded-rescue-actual-2026-10-13.json).

Reverse-role run `9903873dcf39457e8ccdd3df3da5bcdd` also passed guest pickup/carry/put-down of the sleeping host, actual native idle/life retirement, full pre-dart 7800 HP, and five native-accepted timely post-wake movement captures from both viewers. [Reverse rescue and movement](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-reverse-rescue-motion-actual-2026-10-13.json). Pickup now waits for both copies to reach actual native rest, so it cannot interrupt the falling sleep onset. Carry/readiness messages bind both current player generations, mission and link epoch; both players require this version. A stock wake-release episode prevents reseeding while actual masks/health finish retirement. Failed current-body reads retain cleanup duty without reporting readiness that could mask a replacement body.

The rescue HUD keeps the stock pickup icon and removes duplicate pickup instructions. It now uses concise semantic Action/Carry labels: Hold to wake/revive, Keep holding during actual progress, and Hold to put down. Completed holds stop asking for more input, and carried states suppress rescue actions. Its announcements are status-only and coalesced. Actual carried frames show the teammate on the carrier's shoulders. Controller-specific/remapped glyphs remain unfinished.

Friendly darts use the stock player sleep controller: onset, lying rest, native put-down and recovery, with no health loss or repeated sleep reseed. MGO/TPP controller comparison and decoded original animation channels show MGO's player rest is also lying; the guard upright sleep clips belong to a distinct soldier bank. A local-only MGO rest substitution fixture was loaded in hidden run `224489887f9544e19d3419ae6927a93a`, but the typed selected-motion reader proved that TPP's cached resident bank still won. Loading the archive is not accepted MGO animation reuse. A unique small bank and native first-binding route are under qualification. No proprietary animation assets are committed or bundled. Sustained guard-style upright sleep is unfinished.

The sleep timer now accumulates fresh paired gameplay without restarting during carry or temporary stale/pause gaps. Authenticated TEAM_V2 grants mirror wake and native put-down before canonical health/input release. The stock life updater revealed the remaining sleep loop: clearing life while stun remained depleted re-entered sleep. Wake now restores bound maximum stun through an owned compare/exchange, lets the original updater clear life, and waits for actual native retirement. Run `5df105434ea741f58ee2a19e882cb19a` passed automatic wake while carried, unchanged health, both native put-downs and fresh unlocked movement. A later unrelated vehicle positioning step stopped that combined scenario and is retained in its evidence; the wake checks passed. [Automatic wake evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-auto-wake-stun-actual-2026-10-13.json). The older pending runs remain historical diagnoses.

Rescue-baseline Research receipt `20261013-205134-b84ec45ef2804d669e7ed73dcf9dd531` passed all 216 audited tests. Run `6feff4f358004518a633e0149d84ae0a` used no Action wake input and verified automatic wake from native carry, both native put-downs, sleep retirement, unchanged 6600 HP and unlocked movement in four timely native-accepted captures from both viewers. A fifth capture entered native action28/speed0 while position still advanced; the walking-only classifier rejected it. That later failure is preserved, and is not counted as a fifth passing motion capture. [Automatic carry wake](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-auto-carried-release-actual-2026-10-13.json).

Run `0d7366727db34110a35febda615dba58` passed actual rifle downing, wounded pickup/walk/drop, teammate Action revive to full 6600 HP, all eight scenario checks and three timely native-accepted post-revive movement captures. It needed owned forced process shutdown; all 63 protected paths and real saves still matched. [Current wounded rescue](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-wounded-prompt-release-actual-2026-10-13.json). Both new ready prompts were inspected in private captures. These results remain local two-copy evidence.

Horse run `cda340da5ea947a58087d5a0ec3b22c5` found the actual horse, passed its stock geometry, and exposed the player-target UI-only restriction. Clearing that restriction alone is insufficient: the player needs a physical attachment, horse cargo state and native unload/cleanup. The private physical reader initially refused an address-point mismatch; exact constructor review agrees with actual vtable `24c97f8`. No horse cargo writer has been accepted.

Jeep run `a39d407bc4c341eda71c81b16ba34a62` verified ordinary local entry/exit, native phase 3, driver 0, current vehicle vtable `22ed9f0`, getter `b2dd30`, four authored seats and unused passenger slots. This qualifies a read-only vehicle census, not a remote rider or shared passenger. The private passenger prototype remains off. [Jeep evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-vehicle-jeep-census-actual-2026-10-13.json).

The offset-position horse/vehicle debug prototype is explicitly testing-only and off by default. It is not a native seat implementation. The installed rescue release is 0.12.5. Worker-disabled Release receipt `20261013-213206-d67f931d11b4499992aaad5c47a40007` passed all 216 audited tests; the friend-package fixtures passed 1108 checks with the documented optional modes excluded. Package `mgs-coop-friend-0.12.5-20261013T213512Z-3af534f0` is installed in both owned game copies. Its loader SHA256 is `6ac495d3314dfa48d8c959e6e31612cec5235d70d42e64424cf16b20e49ddf0e`. Only the two loaders, their two ownership receipts and host installer version metadata changed among 63 protected paths; all other 58 paths, including saves, activation and retail files, matched. No game was launched by the installer. The source Play-Coop selects this newest extracted package, including its local two-window option. Private mount and MGO candidates remain off. [Release and installation evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/release-0.12.5-2026-10-13.json). Hidden workers always use a private desktop, bounded in-process input and lab saves, and restore the current installation after each run.

Read-only vehicle run `d26be38536db49cabd20bb1525a95083` verified the real lookup proxy: `23063f8` / `bf5450`, with primary `22ed930` and count getter `b2dbe0`. The proxy differs from the underlying direct vehicle query interface, so binder calls stay off until its inner owner/allocation is verified. [Actual query evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-vehicle-query-actual-2026-10-13.json). A distinct read-only birth census preserves actual 64-bit creator receipts and current paired model identities; it never grants a native writer or manufactures a dual-parts ticket.

Actual ownership run `0e4fede1e0f248dfa2d181a0e23cef1c` verified the Horse service bridge to native primary/interface `2363c48/2363c80`, coherent empty cargo and finite native anchor matrix. It also verified the jeep proxy inner owner, live records/bitmap, final `24410e8/2441120` resource, and typed local camera owner through normal entry/exit. Post-exit controls passed two timely motion captures. The mounted callback census refused its event-key assumption; exact code review found it confused callback payload with the registered event, so native replica riding stays off pending that strict correction and actual test. The prologue providers were all null in Afghanistan; no VR controller is inferred from the global selector. [Actual horse/vehicle owners](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-horse-vehicle-owner-census-actual-2026-10-13.json), [source and fixture checks](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-owner-census-static-2026-10-13.json).

## October 12, 2026: horse and walker body poses

A realized horse or walker body sends its position, the fourth float of the verified 16-byte census pose as heading, and whether it is occupied and by whom. The other game moves that body only when its own census realized that id. A walker uses `SetPosition`, including heading. A horse uses `SetCallHorse`. The horse dispatcher `0xE4D250`â€“`0xE4D95E` has no heading argument. Its call consumer at `0xE6AB89` takes the goal minus the start, calls `atan2f(direction.x, direction.z)` through thunk `0x1A0600A`, and stores that angle in the pose slot at `0xE6AC4E`. The goal is therefore the start plus one metre of `(sin yaw, 0, cos yaw)`. The vehicle the local player is driving is read with `TppVehicle2` `GetPosition` (`0xB2B7F8`, body `0xB2BA47`) and moved with `SetPosition` (`0xB2BA9F`, fields `position` and `rotY`). Parked vehicles have no verified id list, so they are left where they are. A remote occupied horse is still shown as a rider only by the existing ride-kind display. None of these calls mount, dismount, or arm a body.

A pose with no body id is dropped. A pose for a horse or walker this machine's census did not realize is refused. A vehicle pose is applied only when `Tpp.IsVehicle` accepts that same id. The local occupied body is left alone. The release loader reads horse and walker poses without the private-worker gate, and only after the same code pins match.

## October 12, 2026: what this build does in a mission

Two players still move, shoot, crawl when wounded, and revive with a three-second Action (E) hold. Friendly sleep darts still put a teammate to sleep; the same hold wakes them, or the host timer wakes them after 30 seconds. While you are the one down, the lower-center prompt says you are waiting. While a teammate is down:

- Far away: get closer. Pickup is unavailable.
- Inside about 3 metres, before the Action hold is observed: you are close enough, but Action (E) is not ready yet. Pickup is unavailable.
- Once that hold is observed: hold Action (E). A bar shows the three-second progress. Pickup is unavailable.

The same sentences are sent once through the game's own `TppUiCommand.AnnounceLogView` log when the state changes. The lower-center line is the existing co-op HUD, moved to where an action prompt is read. It is not a second overlay. The pinned native hint queue is a Quiet-camo site; a previous hidden run did not show that hint as a visible prompt, so this build does not call it.

Historical October 12 finding, superseded by the October 13 native runtime evidence below: no Lua pickup command was identified. That did not establish that native pickup was impossible. `TppPlayer2` controller `0x11566A0`â€“`0x1156A4E` contains `RequestCarryOff` only, and stock `s10070_sequence.lua` uses that command to end a carry. The player manager command set at `0x9C5F80` is `Warp`, `WarpAndWaitBlock`, and `WarpToStation`. The executable contains no `RequestToCarry`, `CarryPlayer`, `SetCarry`, or `StartCarry` string. `EndCarryAction` at `0x11322F7` and `StartCarryIdle` at `0x1132986` are internal action-node hashes, not a Lua pickup. The scratch `SubmitNativePoseLocalSleepingCarry` writes a force-A target and its header says that submission is not attachment. The carry-target, deferred-copy, owned-op1, shared-op2, queued-provider, and player-tree files remain contracts and fixtures.

A completed revive still restores health and the crawl cap. It then calls the stock local-player `Player.RequestToSetTargetStance(PlayerStance.STAND)`. That wrapper is `0x9DB191`: it takes the stance component and calls `vt+0x10` with the local player index. The hospital script uses the same call to leave a crawl. The downed pose stays the prone crawl until that revive request.

The other player's on-foot body still follows the partner transform stream. Horse, walker, and occupied-vehicle bodies use the pose sync above. There is still no mounted-animation write and no verified walker arm command.

The game was not launched for this change, and the installed game folders were not modified.

## October 12, 2026: carry-target service landed

`carry-target-service-v1` is in the main tree from `carry\carry-target-service-v1`. It has no earlier source base: the stage is the pinned contract only, copied byte-for-byte. Research Release `/W4 /WX` passed `carry_target_service_tests` (31 checks). Activation stays off. It does not install a hook, write a target, or start pickup, horse loading, or shared passengers. The named carry-passenger scratch stages through this service are merged. The guest pose-window refusal and the guest `0xC0000005` remain unverified. The game was not launched.

## October 12, 2026: player-tree closure landed

`player-tree-closure-v1` is in the main tree from `riding\player-tree-closure-v1`. The bundled op2-null, op2-tree, and row4 readers match their scratch dependencies, and the row4 header already in the tree was left unchanged. The fixture also publishes the existing row4 node at `block+0x210`. Research Release `/W4 /WX` passed `player_tree_closure_tests` (228 checks). It is a read-only path and effect reader. It does not bind a callback or activate carry, drop, or passengers. The carry-target service is recorded above. Pickup, horse loading, shared passengers, the guest pose-window refusal, and the guest `0xC0000005` remain unverified. The game was not launched.

## October 12, 2026: queued carry provider landed

`queued-carry-provider-v1` is in the main tree. Its scratch base is `deferred-carry-provider-v1`, and that base matched the current sleep-partner sources, so both stages were copied byte-for-byte. Two fixture fields were aligned with the current lease: `batch` is `batchBefore`, and the stale-poll case uses the current 750 ms window. Research Release `/W4 /WX` passed `sleep_deferred_carry_tests` (80 checks) and `sleep_queued_carry_tests` (103 checks). `mgs_partner_player` and the probe compiled. The queued proof reads a bound selected1 handshake and does not copy a queue or activate carrying. The carry-target service is recorded above. Player-tree closure is recorded above. Pickup, horse loading, shared passengers, the guest pose-window refusal, and the guest `0xC0000005` remain unverified. The game was not launched.

## October 12, 2026: shared op2 landed

`carry-op2-shared-v2` is now in the main tree on the owned-op1 base, byte-for-byte. It recognizes one natural shared op2 source and keeps the distinct sleep validator and current player-tree validator null. No new hook is installed and no carry message is generated. Research Release `/W4 /WX` passed `carry_op2_shared_tests` (15 checks), and `mgs_partner_player` compiled. The carry-target service is recorded above. Pickup, horse loading, shared passengers, the guest pose-window refusal, and the guest `0xC0000005` remain unverified. The game was not launched.

## October 12, 2026: owned op1 landed

`carry-op1-owned-v2` is now in the main tree on the deferred-copy base, byte-for-byte. It admits one natural owned op1 reply through the existing dispatch and keeps the queued sleep validator and player-tree handler null. Research Release `/W4 /WX` passed `carry_op1_owned_tests` (27 checks), and `mgs_partner_player` compiled. Activation stays off. The carry-target service is recorded above. Shared op2, the queued provider, and player-tree closure are recorded above. Pickup, horse loading, shared passengers, the guest pose-window refusal, and the guest `0xC0000005` remain unverified. The game was not launched.

## October 12, 2026: deferred carry copy merged

Scratch and the main tree contain the guest pose-window refusal and the guest `0xC0000005` from runs `35fdf39046304635b1b9432c8dc76163` and `b7baad6b1ff14cf792115954e160f1cb`. No diagnosis or partial fix for either failure was started. Owned op1 was authored against the deferred-copy output, so that earlier stage is the one that applies to the current tree.

`carry-deferred-copy-core47-v1` is now in the main tree: one default-null `0xfe00d0` setter hook, a single reserved queued-80 copy, and the authored fixture. Research Release `/W4 /WX` passed `carry_deferred_copy_tests` (31 checks) and `native_pose_hook_groups_tests` (6 checks), and `mgs_partner_player` compiled. Activation stays off. It does not bind a sleep provider, emit a carry request, or claim pickup, drop, horse loading, or shared passengers. The game was not launched.

## October 12, 2026: row4 storage fixture closed; native disposal still off

Codex had already landed the uncommitted carry-origin and queue-lifetime hooks, the cold P238 reader, and body0 rest maintenance. Research receipt `20261012-115408-443f7f3ef459439c847e165133a44f6c` passed 189/189 audited checks after the P238 optimized-reader correction. The same loader's hidden run `35fdf39046304635b1b9432c8dc76163` refused the guest pose window before cold P238 or sleep. The follow-up `b7baad6b1ff14cf792115954e160f1cb` enabled both pose windows, then the guest process exited `0xC0000005` before the asleep check; host and guest loaders were restored and real saves matched. Those two runtime results stay unaccepted.

The newest unfinished scratch stage was `row4-storage-lifetime-v2`: a read-only pair for the already qualified generic `0x5c4910` footprint, with 38 assertions and `fixtures_run` still false. Those assertions now pass under `/W4 /WX` both unoptimized and `/O2`. The reader, contract pins, and fixture are in the main tree and on the worker check list. They do not install a hook, call native disposal, or authorize carrying. The carry-target service is recorded above. Deferred copy, owned op1, shared op2, the queued provider, and player-tree closure are the exceptions. Native row4 capability, pickup/drop, horse loading, shared passengers, and a repeat hidden run of the crashed guest path remain unverified. No game was launched for this fixture close.

## October 11, 2026: carry, distinct sleep and shared-passenger development (0.12.4 installed)

Current result: the 0.12.4 source separates wounds from friendly dart sleep. Wounds allow slow crawling, block combat and require teammate revival; sleep preserves pre-dart health, immobilizes the player and allows early teammate wake or a fresh host-owned 30-second timer. Default sleep still uses the immobile prone fallback. Teammate pickup, putting down, horse loading, shared horse/vehicle passengers and visible guard-style remote sleep are not implemented. Native carry/riding activation and remote body1 sleep stay disabled.

Final validation at source `7487c6355421b8ebb8bd41808ebd069323b3ca12`: Research and normal worker-disabled Release each passed 156/156 audited tests. A repeated actual hidden cycle `52f2d3663d0b45deb03cabca49a5d6d1` passed all 18 requirements after the nested-job callback fix: HP6600/6600, native local0 sleep5/Q84/life2, W+aim+fire magnitude1000â†’0, teammate E wake6/Q85/life0, then idle0/Q1/life0 and healthy1000â†’1000. All 63 protected installation/save files match afterward; no desktop switch, focus call or system input occurred. The 1,207 launcher/installer fixtures also passed; they do not establish gameplay. [Final checks](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/carry-sleep-final-checks-2026-10-11.json), [final actual cycle](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-sleep-composed-actual-2026-10-11.json).

Playable package update (October11): the checked0.12.4 friend package is now extracted at `dist/mgs-coop-friend-0.12.4-20261011T231346Z-c4e102df`, and its worker-disabled loader is installed in both owned game copies. The source `Play-Coop.bat` selects this newest package. Only the two loaders and two ownership records changed among63 protected files; retail executables, Steam DLLs, archives, activation and real/co-op/local-test saves are unchanged. The ZIP contains903 entries, SHA256 `d8d07269a353ff245b931524e9e4a6b8bba13ff9feb15b1fe30dea3d5e213052`. The upgrade follows the same156-check receipt above. Sleep remains the prone fallback; native sleep experiments and unfinished pickup/passengers are excluded from this playable build. [Package and installation evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/release-0.12.4-2026-10-11.json). Hidden offline testing does not establish fresh two-PC Steam acceptance. The following dated paragraphs retain the investigation history; earlier refusals were superseded only where a newer successful receipt is identified.

Latest private animation result (October12): Research receipt `20261012-113434-03423ebde80643a18cf7563bfd9be1ed` passes185/185 audited checks. Hidden actual run `bde23e5f90ed4d579fead99b4bfe9b90` passes15/15 requirements: coherent cold carry route/profile readiness, actual body1 native sleep5/Q84/life2, unchanged6600HP, locked W+aim+fire1000 to0, normal local camera/cache, teammate wake, complete native retirement to active0/phase0/mode0/life0 and held0/fault0, then fresh local guest input1000 to1000. Both owned games closed normally without forced termination; all63 protected files and real saves match, and no system input, focus call or desktop switch occurred. No private pose refusal was recorded. Exact natural CLEAR/SET flag-map callbacks preserve their original native result and guarded topology; the preparation watchdog now compares its completed read time with a fresh clock. This supersedes the earlier core36/flag-map/preparation refusals for this short cycle only. Body1 rest maintenance is authored and source-checked but longer duration remains unaccepted. Guest local0 uses the prone fallback here; grass obscures the sleeping replica, so clear standing-pose acceptance is still absent. Pickup/drop, horse loading, shared passengers and fresh two-PC Steam remain unfinished. These private changes are excluded from installed normal0.12.4. [Latest hidden cycle](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-body1-core36-sleep-cycle-actual-2026-10-12.json).

Earlier private animation follow-up (October12): all175 audited Research checks passed at receipt `20261012-061031-f642ce76d02b4cbabd2f180ef09619dc`. The selected-body exporter and camera filter preserve local0 positions and third-person views, accepted in earlier camera run `cc38409742b049a2929466e7f7bb8719` (10/10). Actual short body1 sleep cycle `274ce091c4e640d39d079559bcb3ecd0` now passes all12 requirements: HP6600/6600, native onset/rest mode5/Q84/life2, actual W+aim+fire1000 to0, accepted teammate wake, two genuine native idle observations and Closed cleanup to active0/phase0/mode0/life0, held0/fault0 and native/cache/Lua gap0. No private refusal occurred. The owned worker closed using forced cleanup and restored all63 protected game/save files; no focus, system input or desktop switch occurred. Viewed final frames preserve normal cameras; guest local0 remains the existing prone fallback stance, and grass obscures the sleeping replica. Fresh postwake movement, clear guard-style standing sleep, longer/lifecycle-loss tests, actual pickup/shared passengers and two-PC Steam remain unaccepted. Native rest-duration maintenance and carry pickup/drop adapters are next. These private changes remain excluded from normal0.12.4. [Actual accepted short cycle](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-body1-sleep-cycle-actual-2026-10-12.json), [175 checks](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/sleep-recovery-holder-root-checks-2026-10-12.json).

Latest private wake diagnostic (October12): all174 Research checks passed at receipt `20261012-055206-127ba4474bfd43c193a1a1bacdcb85af`. Run `afcd52bc9d1a49408d82664844a96450` repeated the eight rest/control/camera requirements and then caught the exact first rejection: the body1 controller was mode0/request0/life0 with its node inactive, but PSaction19/Q85 and the current recovery graph `40D8FDA5CA06` remained. Current owner/max bindings passed; last strict owned observation was16ms old and accepted target50ms old, still canonically asleep without a wake grant. The current shared poll helper requires complete retirement too early; the later175-check holder and accepted short cycle above supersede this refusal; strict release remained unchanged. Actual full retirement, carrying and normal activation remain unaccepted. This run closed without forced termination and restored all63 protected files/saves. [First actual predicate](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-body1-sleep-wake-first-predicate-2026-10-12.json), [174 checks](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/sleep-wake-diagnostic-root-checks-2026-10-12.json), [native wake ordering](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/sleep-wake-ordering-proof-v2.json).

Native sleep cycle actual qualification (worker-only): run `c98eae6788a341f49591ad63b245c1d6` passed all scenario requirements after 153 audited Research checks. A WU dart kept guest HP6600/6600, entered mode5/Q84/life2 with the native rest graph, and held W+aim+fire produced actual movement1000 to0. Host E woke the teammate through mode6/Q85/life0; it then returned to idle mode0/Q1/life0, healthy cause0/lock0, and fresh W input1000 to1000. V10 separates a bound post-seed read-only zero-input observation from the original strict write admission; two fresh independently typed samples are still mandatory before release. Screenshots confirm native dark/red local sleep UI and restored gameplay after wake. Body1 standing sleep/carry/shared riders remain disabled and unqualified; there is no guard-style visible-pose or fresh online claim. Evidence: `archive/research-2026-10-23:docs/evidence/native-sleep-wake-cycle-actual-2026-10-11.json`. The worker closed invisibly and all63 protected .3 installation/save files match.

Configured-value correction: hidden run `ee66b57b580444ee83aaec2c57a0cb85` followed153 passed checks and observed actual local0 native sleep mode5/Qaction84/life2, current sleep graph and preserved6600HP. W+aim+fire produced fresh magnitude1000â†’0. Both request allocation readers passed. The strict exact-window cap receipt expired for read-only retention and the worker refused before wake; all63 protected files still match. Local stock sleep shows blackout/recovery UI; the replica's native sleep remains disabled. This is onset/rest/input evidence, not guard-style teammate animation or completed wake acceptance. [Actual native rest/input](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-sleep-rest-input-actual-2026-10-11.json).

The hidden-only native-sleep wrapper first compiled and passed151 audited checks, with complete transition/typed-owner checks, named control masks, per-frame zero-input receipts, conditional seeding, partial-write retention and current wake/retirement preflight. It is disabled in normal builds. Actual run `c1142101bffc402d9e54d464930aca96` passed both final carry-provider and carried-recipient reads but refused native sleep before its first scalar write. The follow-up diagnostic `01edf09f3e254d1ba9a89a7f6c7d88bd` measured the guest's actual configured stun3300/max3300, disproving the planner's initializer-only3000 assumption. Both copied runs closed and all63 protected files match the .3 baseline. No standing-sleep animation, attachment or shared passenger success is claimed. [First refusal](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-sleep-first-refusal-actual-2026-10-11.json), [configured value](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-sleep-configured-stun-actual-2026-10-11.json).

The read-only carried-recipient row16 and its reciprocal carrier tuple now qualify on both actual bodies. A current Q-owned request allocation reader and pure neutral post-retirement builder are integrated for the next build; they make no gameplay writes or native setter calls. Clearing a request after retirement cannot cancel active carry, and early EndRecipient alone does not stop the carrier. Native carrying stays disabled while paired exit/cancellation, attachment/drop/raycast ownership and mandatory body1 presentation windows are qualified. [Handshake](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/carry_owned_handshake_manifest.json), [retirement policy](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/carry_request_retirement_manifest.json).

Carry row32 live qualification: run `adaa9f8e9220497b8a5faef54490b574` passed all 22 requirements, including ten valid carry-controller observations on host/guest across healthy, dart-sleep, R pickup input and waking. Fifty controller query receipts are recorded in `archive/research-2026-10-23:docs/evidence/carry-row32-qualified-actual-2026-10-11.json`. All146 audited checks passed first; 63 protected files match the .3 installation afterward. Pickup remains absent: stock target op0 requires native action83/84 and the mod fallback is action1D. Pure initial-carrier/recipient request construction and exact stock put-down Lua proof are now retained for qualification; they issue no gameplay requests. Native sleeping, carrying, shared horse and road passengers remain unfinished.


Current carry follow-up (October 11): the read-only carry scheduler was incorrectly indexed at row33; complete builder/binder evidence and the actual node establish row32. Corrected offsets retain all type/owner checks. The offline direct-part-zero sleep notification crash shield now covers raw NPC/self/sentinel attacker IDs as well as teammate IDs, without granting them a mod sleep state. Actual sleep, loaded animation-state and on-foot vehicle observers passed on both bodies in hidden run `a2ce992362e349f9aceea4c7a3287677`; all 63 protected files match the installed baseline afterward. The requested-byte C0 preservation helper is staged as pure tested infrastructure; no wrapper activation or native carry/sleep/shared-rider functionality is enabled by that helper. Carry activation, guard-style standing sleep and shared riders remain under qualification. See the October 11 JSON evidence and row32 correction manifest.


The separate sleep behavior is implemented and live-tested. Cause 1 wounds keep positive protected health, cap natural input at 0.25 and stay downed until revival. Cause 2 friendly darts retain the exact pre-dart HP, block movement and combat, restore the crawl-rate lease, and use an immobile prone fallback. Host-authorized 30-second paired-gameplay sleep timers stop on stale/failed polls, clock reversal, scene changes and unpairing. Healthy teammates can wake sleepers earlier using the existing three-second E hold. Stock standing sleep and actual carrying remain unfinished.

Hidden run `80842d0969014d0ca6adbd449bfaa0b2` used actual WU shots both directions: Guest 6600/6600 and Host 7800/7800 HP were preserved, fresh native movement1000â†’0 was measured, the host woke the guest with E, and the host later woke by the timed path. Run `64aa22628536402daa1f3c1213506376` used actual rifle fire: Guest 330/6600 HP, magnitude1000â†’250, still wounded after 32 seconds, then healthy1000â†’1000 after host E revival. Fifteen required checks passed; both normal installations and all 63 protected files matched after cleanup. NativeTick64 and Lua world timestamps are different clocks and were not subtracted. [Actual sleep/injury/carry evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/sleep-injury-carry-actual-2026-10-11.json).

Natural R near both sleepy and wounded teammates did not pick them up. The host moved while the sleeper stayed at the same position. The stock geometric carry query was eligible, but its gun target is not the shoulder-carry target. A new exact carry-controller reader refused its initial typed chain during live run `c5d2e756f9fc40c0b7804a8bcf073c4d`; no controller fields from that failure are accepted. Grouped first-failure diagnostics are now added without weakening guards. [Negative reader qualification](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/carry-controller-read-actual-2026-10-11.json).

Full native proof identifies stock sleep onset/rest/recovery callbacks and resident graph nodes, plus a typed read-only sleep-controller observer. Resident asset presence does not prove the live graph is loaded/playing. The carrier/recipient paths, target-body camera/control/attachment/drop ownership and transitive null-synchronizer safety must be qualified before activation. Current injuries and sleeping controls are never unlocked by a candidate carry flag or gun target. [Sleep chain](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/offline-native-sleep-chain.json), [reader stage](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/sleep-plugin-reader-stage-2026-10-11.json), [carry controller](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/carry_plugin_evidence.json).

The prologue uses a dedicated `TppPlayerHorse2forVr` controller and authored Ocelot route; it is not an established free-roam D-Horse passenger API. Ordinary road vehicles have native seat >= 2 passenger phases and body seat transforms. Their stock kind9 publisher requires the online flag; that flag remains untouched. A rider-array insertion alone does not attach the player. MGO's separately fingerprinted executable supplied study candidates only; no MGO offsets were applied to TPP. [Prologue/MGO study](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/riding-research-proof.json), [seat/ownership chain](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/vehicle-player-seat-proof.json), [motion producer and body transforms](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/vehicle-motion-producer-proof.json).

Fresh two-PC Steam acceptance, standing sleep, actual player carry/horse loading/shared passengers and mounted vehicle-seat observation remain open. All work and runtime tests stay on the qualified private desktop with copied saves and owned input. The last published and normally installed release below is 0.12.3 until the new candidate is packaged and installed.

Hidden read-only run `192b23ae2a3b49159ce13f5aab482eea` confirmed the precise refusals: sleep record callback adjustment incorrectly included unspecified padding; carry primary +38 did not match the expected scheduler row. All 20 controller query receipts and bounded native traces are retained. Both games closed without forced termination, all 63 protected files matched, and actual dart sleep / E wake passed again. Signed 32-bit sleep and vehicle callback padding checks are corrected; actual carry binding remains strict. [Actual controller identity evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/controller-identity-read-actual-2026-10-11.json).

The carried-recipient kind-15 path and forced carrier target are identified offline, with full-range lifecycle proof. The unindexed P+C0 interface is a shared presentation-mode request, not a proved camera API. Its +F8 method only writes requested mode +6D; existing body-1 windows do not preserve that byte and can fall back to an unwrapped job on refusal. Native sleep recovery also touches local pad and camera state. Activation remains disabled until ownership, retirement and every relevant scope failure are qualified. [Carry lifecycle proof](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/carry-actual-lifecycle-proof.json), [window review](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/camera-sandbox-reuse-review.json), [sleep callback proof](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/sleep-callback-sync-proof.json).

The carry-provider reader now pins the complete boot registration and exact fixed singleton chain: locator `2C6C0C0`, final services `2C00D40` / vtable `2289588`, optional provider `2BD6160` / vtable `2191A90`. Its earlier base-vtable expectation refused the actual final service. Fifteen complete sites and two slots verify against the protected retail executable; 36 reader fixtures pass. Fresh actual runs `c1142101bffc402d9e54d464930aca96` and `01edf09f3e254d1ba9a89a7f6c7d88bd` now pass this read on both bodies; it makes no provider calls. [Final registration proof](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/carry_provider_final_registration_evidence.json).

The worker-only body-0 sleep lease now has 198 strict checks covering configured stun bounds, ordered conditional seeding, partial-write retention/repair, native onset/rest/wake observation, early host wake grants and fresh two-observation retirement. Complete seed/transition gates qualify the configured maximum actually read from the current owner. A bound retained observation can follow post-seed zero input, while all writes and final release retain stricter independent preflight. Actual runs `c98eae6788a341f49591ad63b245c1d6` and `52f2d3663d0b45deb03cabca49a5d6d1` passed mode5â†’6â†’0 and restored input. Native activation remains disabled in normal builds and on remote body1; these runs do not establish visible guard-style teammate sleep. [Initial planner manifest](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/sleep_body0_lease_manifest.json), [seed proof](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/sleep_body0_seed_proof.json), [final actual cycle](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-sleep-composed-actual-2026-10-11.json).

Additional complete native research proves the carry model method copies a matrix out rather than changing the model, and vehicle seat removal does not itself retire the player pose. Forced carry drop and shared riders still require runtime adapters and live validation. [Model getter](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/carry-model-proof.json), [vehicle activation review](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/vehicle-activation-proof.json).

## Previous release: 0.12.3 â€” October 11, 2026

Source and preserved development history are on the private [GitHub repository](https://github.com/TrollTaylor/diamond-dogs-together). The [0.12.3 prerelease](https://github.com/TrollTaylor/diamond-dogs-together/releases/tag/v0.12.3) includes the friend ZIP, with Play-Coop **T** for the manual two-window test and F9 downed/horse controls.

Package `dist/mgs-coop-friend-0.12.3-20261011T144511Z-bb927c13`, ZIP SHA-256 `dc72deae5c9a5e8fcb800121190bf756ce9d01df8acf01c62ed9b9310699c2ac`; Release loader `FA726C892B8E93C2CF728AE4748C858D897DC90508DCE77FA0A2DFAAD2A5F55B`. The package uses clean source commit `a924552724e07125e3453b485a40c2f0ed6f6f85`. Later documentation-only commits do not change those build inputs. Release receipt `build\worker-runs\20261011-144248-eaafafaf9e4e45848cccc7332e4b40dd\receipt.json` passed 138/138 audited checks; worker and experimental buddy capacity are OFF. The full friend launcher/installer fixture previously passed 1207 checks under PowerShell 5.1 and 7; its scripts are unchanged since that run. A separate GitHub checkout built the current loader with pinned dependencies and passed nine selected source-only fixtures without retail inputs.

Installed that loader in both owned game copies. Of 63 protected file hashes/absences, only the planned two loaders and two ownership records changed; retail executables, Steam DLLs, runtime archives, activation settings and real/co-op/lab/local-test saves stayed identical. The ZIP has no retail executable, Steam DLL, runtime archive or save. [Release and installation record](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/release-0.12.3-2026-10-11.json).

## October 11 revive, crawl and testing controls

The repair version is 0.12.3; fresh two-PC acceptance remains pending. The newest user-session logs showed the healthy teammate repeatedly refusing a changed native identity, so no held-revive packet reached the host. A healthy poll now coherently retires the stale lease and creates a fresh generation without restoring health through the old pointers. Active downed ownership and stale-grant rejection remain strict. Both actual Lua roles pass the generation-change/held-revive regression.

Private hidden run `f21ee5e3a27b4b61842eda78a47b092d` used actual host gunfire, host sleep darts and guest sleep darts; all three down/revive cycles passed, including reciprocal ACTION revives. It predates the slower crawl and horse helper. Both installed loaders were restored, real saves compared unchanged and no desktop switch, focus or system input occurred. [Exact evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/revive-repair-live-2026-10-11.json). Natural healthy pointer replacement was diagnosed in user logs and exercised in fixtures, not reproduced naturally in this lab run.

The first native crawl-record candidate reported rate 500 but did not reduce physical travel. The subsequent exact native input hook runs the original update first, then caps only the freshly computed owned downed body's movement magnitude. In hidden run `39f6a904dc704ce799a1a1fb038788bb`, established local crawl measured Quiet 1.713 / 0.873 / 1.704 m/s and Snake 1.534 / 0.912 / 1.701 m/s before / while downed / after revival. Actual gun and reciprocal sleep-dart revives passed, as did cap and next-update reset readbacks; weapon, aim, reload, stance and dash inputs remain blocked while downed. Snake's remote crawl followed closely; Quiet's replica ran faster before corrective nudges, so precise remote slow-crawl parity remains unfinished. [Physical positions and timing](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/downed-input-actual-39f6-2026-10-11.json).

F9 Partner / Co-op provides downed, revive and movement-input readouts plus normal-release Summon / recall D-Horse, Horse status and D-Dog follow status. Run `87a5ce1` visibly established the guest horse while the host retained its D-Dog; the host's Dog status continued advancing after the guest's request. The real F9 movement-input action also returned its readout. No mount was performed. The helper requests the stock local horse four metres beside the player; its selection receipt explicitly leaves arrival unverified. Remote rider attachment and two-player mounting remain unfinished. The composed Dog/Horse fixture exercises releasing an owned held target before yielding the selector; the actual native-clear receipt was outside that run's bounded log budget, so the visible result does not independently establish that call's causality. [Horse and menu evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/horse-recall-actual-87a-2026-10-11.json).

The later actual-enemy phase of `39f6a904` exposed a first-down transition failure: scoped lethal damage was intercepted but initial crawl controls stayed pending beyond the old 1500 ms deadline. Initial acquisition now has a bounded six-second grace; losing an already confirmed control lock retains the original 1500 ms deadline. In final hidden run `71cb477b8f394b8c8c4540d0dadfea4b`, an actual enemy hit (582 damage against 234 positive HP, attacker 1206) downed the host, prone/crawl controls locked, and the guest remained healthy. The guest then revived the host using actual E input; the host returned to standing with down/cause/lock all cleared. Diagnostic auto-heal kept the reviver alive during shooting and was switched off before revival; the host had no auto-heal. This run does not establish both-down checkpoint recovery or how much of the new acquisition grace was used: native damage and Lua world timestamps use different clocks. [Actual enemy and E-revive evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/enemy-downed-revive-actual-71cb-2026-10-11.json).

The hospital opening's exact rail animation remains unavailable; the current implementation slows normal prone crawl and blocks combat inputs. Quiet's remote crawl drift remains open. All 63 pre-task protected file hashes/absences matched after final worker cleanup, including real saves and both normal installations. All actual runs used the private desktop and owned in-process input; no visible desktop switch, focus change or system input was used. Hidden two-copy testing does not establish fresh two-PC Steam acceptance.


## October 10, 2026: local two-window guest launch

The local test used package `mgs-coop-friend-0.12.1-20261010T005201Z-bdab9994`. That package is older than the 0.12.2 source in this tree. Choosing T printed Host on the left, Guest on the right, then failed with `[guest] The launched process did not match this game copy.`

Windows PowerShell 5.1 often leaves `Process.Path` empty on the first read after the GUI launch. The image path of the process the launcher started is still that copy's `mgsvtpp.exe`, including when the guest file is a hardlink of the host file. On this PC those two executables are one file (two links, one file index). The launcher still requires the process id, the start time, and an image path that is this copy's `mgsvtpp.exe`. A path from the other directory fails even when it names the same file. An empty path fails.

Current launcher package: `<repo>\dist\mgs-coop-friend-0.12.2-20261010T195437Z-a0668e6a`. Zip SHA-256 `919E60F64EE665D51E5AC37B4739C63D85DBF32255FBCA9F053ED413CA8812AA`. Open that folder's `Play-Coop.bat` and press T. A checkout launcher that prints `Using package` selects this folder because its preparation time is newest. Release receipt `build\worker-runs\20261010-195230-d3c2cfdc8b0e48d6b0d89d4b9f0ce617\receipt.json` passed 137/137, including `local_coop_windows_tests`. That fixture passed 188 checks under Windows PowerShell 5.1 and PowerShell 7. The package is the dirty 0.12.2 tree at commit `2a0ee694d32e078203a365cedb589f50b5f15718`, loader SHA-256 `391AAF521C2950AD6BEC9FCE4672CC3361CF744C4A563A18E99C377B178E352C`. The live game folders were left unchanged. MGSV was not launched, so the two-window desktop session is still unchecked. Pressing T later updates both owned loaders to this package before the games start; that install step was not run here.

## October 10, 2026: downed, buddy follow, and ride-capacity continuation

The uncommitted work in this worktree already contained the downed/crawl/revive path, friendly-hit forwarding, D-Dog host follow, ally-targeting HUD, and the Play-Coop exact-release loader check. This session did not re-run a hidden game. Prior private-desktop evidence remains `archive/research-2026-10-23:docs/evidence/gameplay-downed-dog-live-2026-10-10.json` (run `7169753d2478483e9ec5b1ab93ad58c4`): friendly gunfire downed the guest, crawl stayed locked, E-hold revive worked both ways, sleep darts downed both directions without a crash, and the guest dog settled about 1.48 m from the host dog. That receipt explicitly did not exercise enemy lethal damage, both-down checkpoint recovery, or a second horse/walker. Mounting stays disabled (`rideMounted=false`).

Fixture fixes landed here, still not live proof:

- `worker_session_tests` had kept refusing `host worldtrigger world=0` after the lab runner intentionally admitted a host or guest `world=0` cleanup step. The fixture now admits that exact lab step for each role, still refuses `both`, a malformed value, and play mode, and checks the dispatch passes the real role.
- Walker census now requires the final factory types `0x2442920` / `0x2442960` from retail `mgsvtpp.exe` SHA-256 `085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45`. The construction-only pair `0x2442660` / `0x24426a0` is refused before any index-1 record read. The 88-byte factory at `0x17033b0` is an additional code pin.
- After a typed horse or walker census, controller+0x38 is read twice and must match one verified main-pack hash (`0x2a74960` horse table, `0x2a74a88` walker qword, copy site `0xa4428b`). The receipt names that pack. Zero, unknown, or a changing hash refuses. Naming the pack does not allocate a second body or enable riding.

Research Release cache `D:\mgs-coop-wt\integ5\build\worker-checks\research-7A96A2054190-worker-on-release` rebuilt with the existing worker/equipment flags and passed **140/140** CTest checks, including the previously failing `worker_session_tests`, both buddy-capacity on/off fixtures, `world_buddy_ride_lua_check`, and `friend_package_tests`. That is fixture evidence only.

Pinned capacity archive is now the scratch file `mgs-coop-buddy-capacity-variants-v2.dat` (17,501,184 bytes, SHA-256 `4b290cddc14f1146dce5a64a1008365d572b424b319874ef77e9496634d75ec9`). Its `catalog.json` is SHA-256 `4a7e7ea890adf8f261368cd546ccfd3dbfdc4407ef6af44aff308b98e53d0989`. The worker profile token stays `buddy_capacity_two_v1`. The file overrides these FPKDs, each authored to total/realized count 2 with a second locator 4 m away. For the six horse packs added beyond variant 00, only the FOX2 bytes differ from the retail pack:

- `buddy_horse2_00.fpkd`, `03`, `02_0`, `02_1`, `02_2`, `05`, `04`
- `buddy_walkergear/buddy_wg2_00.fpkd`

The walker selector at `0xa468c7` loads the qword at `0x2a74a88`, which is the Fox path hash of `/Assets/tpp/pack/buddy/walkergear/buddy_wg2_00.fpk` (extension id `0xa45`). It is not `buddy_walker2_00.fpk`. The package binder at `0xddd00` keeps that hash and writes the same path with the FPKD extension (`mask 0x7ffffffffffff`, then `or 0xed50000000000000`). That companion is `buddy_wg2_00.fpkd`, already overridden in the pinned archive. The seven horse selector hashes are the matching `.fpk` files, and the same rewrite names the overridden FPKDs. Two realized bodies and per-player mounting are not verified. `rideMounted` stays false. `tools/stage_buddy_runtime.py` still builds only horse 00 plus walker and now labels that result `buddy_capacity_variant00_rejected`. The native mount refuses a 16,659,456-byte file before any other archive profile is considered.

Enemy lethal hits and both-down checkpoint restart are implemented, not live-proven. A scoped local hit whose damage is at least current life calls `CaptureHit` with cause 1, zeroes that killing apply, and the next owner poll installs prone, the crawl pad mask, and protected health. Non-lethal enemy damage is left to the game. When both authenticated players are down, the host calls retail `ReserveGameOver(PLAYER_DEAD)` and sends that game-over to the guest, which calls the same retail function. That is the existing shared-death reload, not a new checkpoint write. No hidden run has shown an enemy bullet completing that down, or both players actually reloading the checkpoint. Prior live evidence above does not cover either case.

The same research Release cache then passed **140/140** CTest checks after the walker-pair note, the explicit refusal of the 16,659,456-byte archive, and the staging-script label change.

Guest horse selection no longer treats an active D-Dog follow as ownership of the horse slot. While the guest dog is still the active buddy, `buddy_select_horse` or `buddy_select_walker` yields that selector without calling dismiss, without recalling the dog, and without disabling host follow. A refused selection restores the borrowed dog. Dog and none selections still cannot replace that borrow. `world_buddy.lua` stays under the existing 16000-byte chunk limit. Research Release cache `research-7A96A2054190-worker-on-release` passed **140/140** again after that change.

Hidden census run `36b4a595c3af46cca78313b32a03220a` (fresh probe `5011cc54639d49008cbde1b0f33cfef0`; no desktop switch, focus, or system input) used the same pinned 17,501,184-byte archive. Before the guest selected a horse, the host buddy was still the dog and the guest was submitting that dog's target. The guest selection returned `selection_submitted_horse`. Fifteen seconds later the guest horse census was `k1 a2 r2` id 6657, unoccupied, pack `03`, local ride id 65535. The host horse census stayed `select_ride_first` because the host buddy was still the dog. The host status after that census was still `kdog` and follow was not disabled. No ride command was sent. `rideMounted` remains false in the capacity receipt. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D`. Real saves compared identical. The owned job was force-closed after the shutdown request; isolation did not fail. This is still a typed census, not a ridden-mount proof.

Ride admission is now a separate gate. `buddy_ride` calls the existing horse `SetCallHorse` or walker `SetPosition` only after a typed census is two allocated and two realized on a real pack token. One body, a missing pack, `pack_unknown`, a refused census, or both riders resolving to the same actor returns a refusal and does not call that command. The host is actor 0 and the guest actor 1 unless an existing ride id already takes one of those bodies; the other player then gets the remaining body. `rideMounted` stays false unless that census also shows the local ride id on an occupied body whose rider is not also the other body. A failed call clears it. The mount itself is still the game's ACTION key. Research Release cache `research-7A96A2054190-worker-on-release` passed **140/140** after the gate.

Hidden ride run `a5c5b73b3932489990cfa513e0275824` (no desktop switch, focus, or system input) used the same pinned archive. Both horse censuses before any ride input were `k1 a2 r2`, ids 6656 and 6657, both unoccupied, pack `03`, `mounted0`. The host was admitted to 6656 and the guest to 6657. Each then received one ACTION press. Eight seconds later both `buddy_census` commands returned `on_foot`: the debug guard only accepts stand, squat, or crawl, so it never read the occupied riders. Walker selection was not started. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D` and real saves compared identical. No second mount call was attempted.

`buddy_census` now also accepts retail `ON_HORSE` and `ON_VEHICLE` after the on-foot check fails. Other debug commands still require stand, squat, or crawl. A mounted census still returns the ride module's own refusal when the count, pack, or admission is missing. Research Release cache `research-7A96A2054190-worker-on-release` passed **140/140** after that guard.

Hidden ride run `ed3c1cd49a934c4b8327d085d29ccd01` read the horses after one ACTION each. The host census was body 6656 occupied by rider 0 and body 6657 empty, `localRide6656`, `mounted1`, pack `03`. The guest census was body 6656 empty and body 6657 occupied by rider 0, `localRide6657`, `mounted1`, pack `03`. Each player occupied only their own assigned body. The other body stayed empty, so one census did not show two riders. Walker selection was not started and no second mount call was made. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D` and real saves compared identical.

The later census display inferred a peer rider from channel 2's partner ride kind whenever exactly one local body was occupied. That heartbeat does not establish a native rider or remote attachment on the empty local body. The display inference was misleading and is removed by the October 11 corrective patch: native actor rider/occupancy fields are reported exactly, with the local native mount result labeled `localMounted` and channel 2's kind labeled separately as `peerWireRide`. The earlier **140/140** fixture result had asserted the inferred display and does not qualify remote mounting.

Hidden ride run `f6a2b54ceba14d25808dac369d0a6077` displayed two occupied horses on each copy after one ACTION each: host 6656 rider 0 / 6657 rider 1, guest 6656 rider 1 / 6657 rider 0, with `remote3`. The peer entries were synthesized by that Lua display inference, not read from native rider records. This run therefore does not prove two native riders or remote player attachment. The earlier `ed3c1cd49a934c4b8327d085d29ccd01` native census showed one occupied local horse and one empty horse on each copy. `buddy_select_walker` returned `on_foot`, so the walker pair was not selected. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D`. Real saves compared identical. The owned job was force-closed after shutdown; isolation did not fail.

One later ACTION each dismounted both horses. Hidden run `f145f79fbe2d4156a35a8a30bb317643` then selected walkers: both censuses were `k2 a2 r2`, ids 22528 and 22529, unoccupied, pack `00`, `remote0`. The host was admitted to 22528 and the guest to 22529. After one ACTION each, the host census occupied only 22529 (rider 0, `localRide22529`, `mounted1`) and left 22528 empty. The guest census occupied neither body (`localRide65535`, `mounted0`). Both lines stayed `remote0`, so the channel-2 ride kind never became vehicle or walker and the empty body was not filled. No second mount call was made. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D` and real saves compared identical.

The private capacity `buddy_ride` placement parks an unused Walker body 40 m away and places the admitted body 2 m beside the player with a 3-number `SetPosition`. The October 11 correction refuses an occupied, non-null-rider, or nonzero-state other body and compares the same qualified native fields twice before either placement; it reads them again after parking before the second command. These placement commands do not establish a mount or remote attachment. The normal release producer uses only the established public `ON_HORSE` / `ON_VEHICLE` / `ON_HELICOPTER` statuses; the unqualified no-argument ride-id getter was removed. The earlier **140/140** fixture result is retained as historical evidence and does not validate the correction or remote mounting.

Hidden run `6e789152c12e4745941383ff3b0e59e9` still admitted those two bodies, then one ACTION each left both `localRide65535` and both walkers unoccupied (`remote0`). The host's other walker was at `p842.2` and the guest's admitted walker at `p802.9`, so the placement ran, but the mount did not. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D` and real saves compared identical. The owned job was force-closed after shutdown; isolation did not fail.

The walker arming call is not in the verified dispatcher. Retail `mgsvtpp.exe` SHA-256 `085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45` compares exactly 16 Fox StrCode64 command ids in `TppWalkerGear2` Lua dispatch `0x1706190`â€“`0x1706cda`. They are `ForceStop`, `GetPosition`, `GetResourceId`, `IsBroken`, `IsEnemyRiding`, `IsFultonCaptured`, `IsPlayerRiding`, `NoRideOn`, `SetColoringType`, `SetCombatGrade`, `SetExtraPartsForSpecialEnemy`, `SetIgnoreDisableNpc`, `SetMainWeapon`, `SetMarkerEnabledCommand`, `SetMaxLife`, and `SetPosition`. `SetCallHorse` is not among them. `SetPosition` remains the only placement command and does not arm a chosen body for ACTION. No further hidden ride was run.

The pinned horse rider handler `0xe59c20`â€“`0xe59e19` (505 bytes, SHA-256 `9e35a6fc455a55bb3ffb7f393eca1822842978c7e713f8b1b2739bc83074bce1`, same executable) is the function that records a horse mount. It accepts only event `0x5167c60042b5`. Mode 0 stores the rider at the horse record `+0xe` and sets occupied `+0x2dd`. Mode 1 is the other horse path. It does not write the player ride id at PlayerInfo `+0xb6`, and it has no walker or vehicle branch and no compare against walker ids 22528 or 22529. Two Walker bodies were read as realized in private capacity tests, but per-player Walker mounting and remote attachment remain unverified. For horses, `ed3c1cd49a934c4b8327d085d29ccd01` read two realized bodies and one local mounted rider per copy; the peer body remained natively empty. The displayed peer riders in `f6a2b54ceba14d25808dac369d0a6077` were inferred from the wire kind and are not native attachment proof. These capacity and placement experiments remain default-off in normal releases.

Hidden census run `661d26d0e3d348ffa1b12f4724b764a0` (fresh private-desktop probe `f158702d7a2f4554bc6e3620b4985dff`; no desktop switch, focus, or system input) mounted the pinned archive on both copies (`horse_fpkd=00,03,02_0,02_1,02_2,05,04; walker_fpkd=wg2_00`). Host horse census was `k1 a2 r2` id 6657, unoccupied, pack `03` (the save's outfit, not variant 00). Host and guest walker censuses were both `k2 a2 r2` id 22529, unoccupied, pack `00`, at different positions. The guest could not select a horse (`dog_borrowed`) because its D-Dog follow still owned that slot, so there is no guest horse count. No ride command was sent and `rideMounted` stayed false. Stop restored both owned loaders and recorded real saves unchanged (no added, removed, or changed save files). This is a typed census, not a ridden-mount proof.

## October 9, 2026: manual local two-window test in every friend release

Every newly built friend package includes **T: Local two-window test** in Play-Coop.bat, plus Play-Offline-Coop.bat as an alias. The source checkout's launchers select the newest extracted friend package by its preparation time. Local play reuses the existing sibling guest folder, or asks for a separate second game folder, checks both owned loaders against the release, and starts both games without title/menu input, Continue, mission selection or teleports. Keyboard/mouse controls the clicked game; controller 0 belongs to the guest.

Each copy has persistent mgs-coop/local-test-saves, seeded initially from the main co-op copy. Only those copies receive windowed graphics settings. Monitor working-area and DPI-aware layout fits side by side when possible, or vertically on narrow displays, handles startup window recreation and bounded game resets, respects manual placement, and offers **R** to arrange again. Window mutation checks exact process identity and uses no activation or z-order changes. A per-folder ownership pointer, paired hash journal and hidden cleanup helper restore each folder's own activation baseline after both games exit. Launch failures close only exact owned processes; active local ownership blocks other play/install/uninstall operations.

Validation: **1,194 friend-package checks** passed using Windows PowerShell 5.1 and PowerShell 7, native INI parser and standalone no-window fake process. Local session fixtures passed **114** checks and layout/graphics fixtures **172** checks separately in each PowerShell version. The source forwarding wrapper passed inert newest-package, parameter and exit-code checks in both shells. Pure worker policy passed **107** checks. Future Release checks now include both local suites; fresh Release receipt D:\mgs-coop-wt\integ5\build\worker-runs\20261010-005034-f55c52571cb141e98184c5895d24aee2\receipt.json passed **120/120** audited tests. No retail game, visible game window, Computer Use or system input was used for this launcher task. Real window placement and manual gameplay remain to be checked by the user; local loopback does not establish Steam relay or fresh two-PC acceptance.

Current launcher bundle: **0.12.1+af78399**, source implementation commit af7839977e668e2398133e76ecc397641015229b; DLL SHA-256 E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D. No native or Lua gameplay source changed. This bundle supersedes the earlier 0.12.1+575392b package for the launcher feature; both PCs must use the same new bundle for online play.

ZIP: <repo>\dist\mgs-coop-friend-0.12.1-20261010T005201Z-bdab9994.zip; SHA-256 AAC60045A0BFFEE757A1D469D361CC3C340358371E054A8F82F27E68CBC10B89. Both existing game copies were upgraded to its owned loader without launching them: host rollback bee11aaaf41a4022a07aa89162510f9d, guest 44339d1a81584625afa151507ec752d5. All **45 current real/co-op/lab save files** are unchanged, none added or removed; original EXE/Steam DLL/runtime archive and both activation baselines are unchanged. Evidence: D:\Games\steamapps\common\MGS_TPP\mgs-coop\local-launcher-update\before.json, after.json and entry-checks.json. Game-folder Play-Coop.bat and Play-Offline-Coop.bat shortcuts now point to the same current release flow. Existing Claude files are preserved; nothing was pushed.

## October 9, 2026: 0.12.1 free-roam repairs (hidden checks completed)

The user reported a successful two-PC Steam session on 0.12.0, with invisible partner weapons, occasional movement corrections and Mother Base/Africa failures. The host-side logs and Claude's stopped `real-play-fixes` workflow were recovered. Guest-side logs from that session are unavailable. The repairs below passed the recorded private-desktop checks; fresh two-PC acceptance remains required.

Integrated changes: strictly admit the three verified retail Mother Base layout pairs while continuing to refuse player-controller variants; preserve compact pack-state receipts after verbose log caps; ignore only a paired nonlocal puppet's death request; use sender clocks and ring generations for reconciliation, one-shot jump placement, bounded dash relatching, effective-mask-aware HOLD speed parity and masked-stick neutralization; recover weapon loading across body generations, allow gated shared stock and closest filled-slot stand-ins, and update equipment independently of F9; append negotiated v6 gun-pose direction with stance-specific aim pivots, camera/sight refusals and a convention checker. Native thread warmup is retryable rather than a hard thread failure.

The ride/air/dead hide policy uses authenticated channel-2 ride heartbeats and stable on-foot placement before realization. After the Africa and shared-gun qualifications, the reviewed candidate defaults to hide for driven native loco/gun stages in lab/play and POINT for driven native gun aim, while preserving explicit off/look/hide settings. Play-mode default activation and movement passed the impaired-link rehearsal below; actual heartbeat-expiry/outage recovery remains unqualified. Measured optional-render acceptance is lab-only and off by default. No reload-speed patch or unqualified native raycast was introduced. The Release loader passed all 118 audited checks and is ready for friend packaging.

Latest checks: Release/worker-OFF receipt `20261010-000054-6719eefcde0d4f7fa4f756b827ecdcef` passed all 118 audited checks with loader SHA-256 `51CFF1038B53C885313DAADF4B9EAA2C388242513A4F41BB35323B24D1F4FBAD` and 528 exact source hashes. Normal/worker-ON receipt `20261009-232102-6945de7cb9e0480eadd85252009984ea` and Research/worker-ON receipt `20261009-232258-f0737948832947f3a49e6e0ba2b5b7c1` each passed all 118 audited checks. Friend-tooling fixtures passed 1,058 checks (including a real owned-junction source-bundle regression) under Windows PowerShell 5.1 and PowerShell 7; worker-session fixtures passed 668, worker policy 89, and detached loading/restart/disembark fixtures 144. The complete launcher/rollback suite passed 1,586 checks on the updated defaults.

Gun run `34fbb8de4e3846ed9a360a2730f7b954` verified both roles loading shared stock 39 through tier cs (refs 3 -> 4, fresh bank), all three slots bound, and another three fresh bindings on each role after Continue into generation 4. Both remote reloads completed in 3020 ms. All 84 captures had fresh snapshots, with zero recorded shot/ammo leaks, slot/magazine faults or ownership disarms. Actual firing selected slot 1 (873 -> 46), so this run does not qualify shared stock 39 in hand. Fresh active prone POINT aim differed by 14.88 degrees: its extra plugin rotation is unqualified. Sender and receiver now refuse prone POINT and use the existing LOOK fallback; exact prone gun-direction parity remains unfinished.

Shared hip run `77d0f9183dd14ccfb837d4c1b99f1c41` qualified actual selected slot0/hand0 with remote 871 -> shared substitute 39/class1 and HUD `gun substitute` in both directions. Tier Cs references 3 -> 4 used a fresh model bank. The receivers accepted/suppressed 12 and 13 shots, kept viewer ammo unchanged and completed/reconciled both reloads to 31/31; all 152 state samples recorded zero leaks, ownership disarms, slot/mag/store faults or refusals. Standing POINT error was 0.00/0.12 degrees; crouch 0.03/0.11-0.12 degrees. All six prone frames used fresh LOOK with no transported gun direction, and both source logs recorded `prone_frame`. Replacement timing remains approximate (sources 17/18 shots, source reload 985/1482 ms, partner 3019 ms); existing model-bank sharing is unqualified. Normal shutdown restored both installs and real-save hashes.

Travel run `ce44e7e13bf549afa5350b496d15f1f7` reached Africa but failed closed on Windows commit-limit error1455 before landing. Low-graphics retry `9de777cd06fc43efa3570b733fabe812` reached Africa without that resource failure, with both native bodies hidden during helicopter deployment and no fallback latch. It stopped before landing because the test omitted the normal E disembark action, visible in its private capture. The guarded wait now permits bounded E only for fresh owned helicopter state in a supported field mission. Fresh low-graphics run `e40e03fb49f5475f9bb626ac9b86ae6d` then completed Mother Base -> ACC -> Africa into generation 5, landed both players and captured walking in both directions: native active, zero recorded fallback/snap events, matching crouch-walk act 4 and at most 244 mm following error. Guest rain readback matched 17 ms after the host re-force and later checks matched, with clocks settling to 2-3 game seconds; rain was already active, so this is not dry-to-rain evidence. All three closed runs restored installs/configurations and verified real saves unchanged. The optional low-graphics setting affects only fresh worker save copies.

Mother Base run `751184586fcf49cf816c13df29f3a2d2` selected `f30050_ly003` on both roles and paired epoch 3 at effective world level 3. All six held walking captures had fresh in-hold movement, matching native acts, at most 255 mm reconciliation error and zero recorded snaps/fallbacks. Guest weather changed sunny -> cloudy -> rain on resync, then rain -> sunny on the host command. Evening time 64800 reached the guest in 34 ms; later clock checks differed by 2 game seconds. Cleanup required the owned job's forced close, with no isolation failure and both baselines/real saves verified. An earlier closed run `6d4704e3e80143a38f277a1d8c6a3243` stopped because the generic intro-skip sequence left a checkpoint confirmation menu open; its fresh idle reading was correctly rejected by the motion check. The passing repeat included a coordinator private-frame inspection pause, observed normal gameplay and required no menu input.

Play-mode run `3d5831c1ad374f2d8228bb102a37822f` used the final defaults without overrides, a copied lab save and private loopback with WAN impairment: RTT 176-185 ms, jitter 5-15 ms and observed loss 7-34 per mille. Both roles stayed native active/ok across all 43 captures, with no fallback latch, hard/network leave, deferred fault or shot/ammunition leak. Default HIDE produced initial ride_stale hiding and foot_stable placement; default POINT was selected, and admitted aiming used the owned point slot. Sprint rates matched at 10021/10022 mm/s with 282 mm error; the opposite-direction movement captured 3688/3768 mm/s and 92 mm error. Each role recorded one far_idle snap during setup/repositioning. The intended pause/outage steps were ineffective: pose/input ages stayed 0-45 ms and ride heartbeats continued, so no heartbeat expiry or outage recovery is established. Normal cleanup restored both installs and verified real saves unchanged.
Sprint/turn run `9e5e9e1e44a14fe5a885a09a78a78372` used the final native-loco defaults. All 26 captures were fresh; 643 unique movement/stop ring rows stayed native active. Adding Shift and separate left/right changes retained sprint class in both directions. Steady speed ratios were 0.9979/0.9809; after the turns, 1.0056/1.0064. The two movement windows recorded zero ring warps/snaps or fallback latches, five bounded nudges each and at most 409 mm error. Turning velocity parity is still approximate: the guest-driven right turn briefly reached body/source 14.134/5.201 m/s (ratio 2.718); ratios above 1.25 with source at least 5 m/s lasted 172 ms before recovery. The 800 ms stop diagnostics failed because both source and body were still decelerating (1.683/1.727 and 1.652/1.749 m/s) despite zero held axes/magnitude. No live dash retry was triggered, so the bounded retry path remains fixture-qualified. Settled source/body speeds were 11/12 and 11/15 mm/s with 194/72 mm error, satisfying the scenario's settled-stop bound. Cleanup required owned-job forced termination only; isolation checks passed and both baselines/real saves were unchanged.
Packaging now compares the fresh Release receipt's exact source snapshot and loader hash before and after staging; stale receipts and unchecked friend-loader overrides refuse. The shipping defaults and runtime loader are checked. Both owned game installs were upgraded with all 42 real/co-op/lab save files unchanged, no files added/removed, retail EXE/Steam/runtime-archive hashes preserved and the host left inactive. The first friend bundle omitted checked MinHook source behind the worktree junction; packaging now explicitly includes that already-hashed dependency, deduplicates source paths, and verifies source/manifest/ZIP bytes in the owned-junction fixture. That first `20261009T234746Z-91a3114f` bundle is superseded; the corrected bundle uses fresh Release receipt `20261010-000054-6719eefcde0d4f7fa4f756b827ecdcef` (118 checks). Its DLL differs from the first Release by 18 bytes: only the verified COFF/debug timestamps and two build identifiers (`0.12.1+e868757` -> `0.12.1+575392b`); every other DLL byte matches after normalizing exactly those fields. Both installed copies now match this final identity. The final artifact and upgrade verification are below.

Final handoff: friend build **0.12.1+575392b**, clean package source commit `575392b689f04585cee09149e3afee6e03f03822`, Release receipt `20261010-000054-6719eefcde0d4f7fa4f756b827ecdcef` (118/118), loader SHA-256 `51CFF1038B53C885313DAADF4B9EAA2C388242513A4F41BB35323B24D1F4FBAD`. Friend fixtures passed 1,058 checks under Windows PowerShell 5.1 and PowerShell 7. The final ZIP includes all 18 checked MinHook dependency files exactly once, with verified source/manifest/ZIP entries; no runtime archive or link secret is bundled.

Package: `<repo>\dist\mgs-coop-friend-0.12.1-20261010T001133Z-1f94b83c.zip`, SHA-256 `F48280F02282F6B9AFB6093091CFE8D33F3C4AD716F82D0BD4E7CD070AFF2775`. Use that extracted folder's **Play-Coop.bat**; both PCs must install the exact same final ZIP. The earlier `20261009T234746Z-91a3114f` 0.12.1 bundle is superseded and has a different HELLO build identity.

Final owned upgrades: host rollback `2d9be465e94846ba92122673335ae284`, guest `8a90fb7f6c274a88bafff1fe35c97e7c`. Both installed loaders match the final Release hash. All 42 saved files are unchanged, none added/removed; retail EXE, Steam DLL and local runtime archive hashes are unchanged. The host remains pass-through, existing co-op/guest-lab saves and the first real-save backup are preserved, and no game or worker remains running. Detailed final receipt: `D:\mgs-coop-scratch\codex-fix5\upgrade-after-final.json`; PE comparison: `release-runtime-diff.json` in the same folder. Work is saved locally on `dev/oct7-continue`; nothing was pushed.

Remaining acceptance: fresh two-PC Steam play using this exact ZIP, including both players' Collect-Logs results. Exact prone gun direction, replacement fire/reload cadence, turn-velocity/800 ms stopping parity and a real heartbeat-expiry/outage recovery run remain open. Mounted bodies and safe native climbing are not implemented; ride/travel is handled by hiding and verified return to foot. Native name-tag raycast occlusion remains unqualified. Story scripting, item/progression synchronization and four players remain deferred under AGENTS.md.
# Development status

## October 9, 2026: weapons3, reload exit, gun-type base ids, tier C writer, early manifest (fixture-tested, hidden runs pending)

`wp/weapons3` (off `integ/final` 0.12.0) fixes what hidden run 53e36944
showed ([native-puppet.md](native-puppet.md) "Weapons"); no new game code
patch, every new read pinned:

- **Reload (F1)**: body 1's reload was never stuck; it exits natively after
  3.02 s (its own playback rate). `partner_reload` now reports stuck only
  after 8 s and stays `entered` with the mirror frozen until the native exit;
  every exit counts (`complete`, `complete_late`), the mirror reconciles once
  after it, and a remote reload started meanwhile gets one queued tap (with
  the reconcile, so body 1's magazine is below capacity). Telemetry
  `reload.len_ms`, `remote_len_ms`, `refill_ms`. A lost exit (body 1 unread
  or wedged in 11/12) is released after 20 s (`partner_reload=release`, Arm
  `reload_release`): the mirror is never frozen forever. The reload keeps its
  native length (the rate work F2 is out of scope).
- **Sender base ids**: a slot's base id is never a non-gun id (343 was a
  type-11 id). Customized aliases get their own type from the game's alias
  path of `A2A3E0` (`0x2C934A0`, `0x2A711F0`; five new sites, the partner
  contract is at 495 code sites and 103 vtable slots) and the base is the
  first table index of that gun type with the slot's receiver, mapped to its
  id (ids 0x400../0x600..), else 0. The manifest is version 2 (64 bytes,
  `type[3]`); version 1 still decodes; one message type on channel 2.
- **Writer**: A1 needs the manifest's type; any A1 refusal goes to tier C
  (same type and receiver, same type, then the slot class; flags 3 and refs
  0 only, never the viewer's weapons or an already dispatched equip; a
  candidate line each; a tier d refusal names its last candidate).
  `OptionalReady` admits the exe's own constructor shapes n1..8 / m(n)
  (`kOptionalShapes`, asserted by `generate_equip_writer_pins.py --check`).
  The slot plan and the fire gate use the manifest's type and tier `sub`
  (a substitute in the remote's current slot is a match).
- **Early manifest**: the loadout sender and the channel 2 drain run from the
  observer's ~100 ms path in every mission (the title and the ACC too, no
  partner state read), the preload query drains once more and each
  MissionStart seeds the store from the session cache: the first mission
  after connecting preloads the remote's weapons.
- Scenario `partner-native-weapons.txt`: 18 x 200 ms reload bursts, a new
  `aim-hold-reload` section (instance 1's aiming exit), F1, writer (all three
  slots a1 or c), early manifest and switching evidence. Both PCs must run
  this build (a 0.12.0 peer refuses the version 2 manifest).
- **Review fixes**: a lost reload exit releases the mirror after 20 s (above);
  a same-revision manifest with new content replaces the seeded store (a
  0.12.0 sender restarts at revision 1 every mission and was dropped as a
  duplicate for the rest of the session); A1 never targets the viewer's own
  `vars.weapons` and a tier C candidate whose fresh Capture refs are not 0 is
  refused (the census is the allocation's first snapshot); the early manifest
  stops at window close in any mission (`partner_defer::ShuttingDown` is set
  in partner missions only); the sender has its own line budget (a spent
  mission weapon cap hid the title's `partner_equip=sent`); the slot plan's
  class is cross-checked against the writer's `TypeSlot`.

Evidence: fixture only (CTest, the Normal, Research and Release worker-check
profiles, every contract generator `--check`). Hidden runs: the coordinator.

## October 9, 2026: 0.12.0, the four October 9 tracks in one build (fixture-tested, hidden runs pending)

`integ/final` merges `wp/online-native`, `wp/steam-join`, `wp/aim-parity` and
`wp/weapons2` (at `cbecb6a`) and versions the source, the loader probe, the
script adapter line, the package/friend tooling (`SupportedLoaderVersions`,
`package-install.ps1`, `set-session-mode.ps1`) and the link build id as
**0.12.0**. Two copies must run the same package; 0.11.1 receivers are still
considered on the wire (the edge_rep saturation at 2, the v5 negotiation).
What the release brings (details in the track entries below):

- **Native partner online**: two-PC play shows the other player with the
  native partner by default on both PCs (Play-Coop writes
  `partner_input_send=1` and `partner_native_profile=gun`; `-Partner clips`
  opts out). Stalls, link drops, a paused remote and range hiding fall back
  to the clips and return by themselves; each PC reports how the other one
  shows it (`partner=... native; you on the other PC: native`).
- **Aim parity**: the aim-target mirror (`partner_native_aimpt=look`, part of
  the gun profile) feeds instance 1's network aim slot, so the partner's gun
  points where the other player aims (the fix for the act-12 aim wobble).
- **Reload**: body 1 runs its own native reload from the other player's
  reloads (input frame v5 `reload_seq`, `partner_native_reload=event`, the
  magazine mirror freezes during it and reconciles afterwards).
- **Weapon switching**: the generalized preload and writer put the other
  player's three slots (or resident same-type substitutes) on body 1 in every
  partner-body mission and the slot follower switches natively
  (`partner_native_equip=substitute`, `partner_native_slot=write`).
- **Steam code joins**: joins between players who are not Steam friends no
  longer depend on the 2015 callback dispatcher (`net_steam_dispatch=both`);
  the guest learns why a join failed, and a partner who quits shows as "left".
- **Co-op HUD**: an in-game `CO-OP [...]` status line with toasts (`coop_hud`,
  compact in play, `-Hud compact|full|off`).
- **Friend flow**: Play-Coop `[O] Host online` (MGS2 code or the F9 invite) /
  `[J] Join online` (start Play-Coop first, then paste the code or wait for
  the invite), `[L]` LAN/VPN as before; [two-pc-play.md](two-pc-play.md)
  "Quick start" and `tools/friend/FRIEND-GUIDE.txt` describe the same steps.

Known open points: run 53e36944 (one PC, before the merge) showed the writer
binding only slot 1 and the reload not leaving 11/12; the reload length/exit
and reload-speed work (F2, a receiver-side patch at `0x10778F0`) is not in
this build (that track stopped before any code). Play inherits both through
the gun profile; the gate strips FIRE/RELOAD for a slot that could not be
filled.

Merge semantics worth knowing:

- Input frames: every copy with `partner_input_send=1` sends v4 whatever its
  own drive (online), and v5 (the reload/switch gun group) once the peer sent
  v5, with a v5 probe every 2 s (weapons2). A copy that shows clips now also
  reads its slot and steps the reload/switch counters, so the other PC's
  native reload and slot follower work against it. The stall hold and the
  reload `sample_strip` compose.
- The weapon gate strips by `partner_slots` (mismatch, switching,
  projectile), then the online gun-absent strip and the play leak strip.
- lab.ini: `partner_native_profile=gun` (play and lab) yields
  `partner_native_reload=event`, `partner_native_slot=write`,
  `partner_native_equip=substitute` and `partner_native_aimpt=look`
  (`ResolveWeaponDefaults` stays the last step of `ParseLabConfig`); the
  play refusals are unchanged and play runs `nslot=call` as write.
- The generalized preload and writer admit play with a connected link v2
  and redirected saves; the stock46 pieces run for the native gun profile
  with `partner_native_equip=stock46` only.
- The worker gun stage (and Play-OfflineCoop) is aimpt look + reload event +
  slot write + equip substitute; one `-LabSetting` allow-list carries every
  track's A/B keys.
- Contract: `tools/generate_partner_contract.py --check` covers 490 code
  sites and 103 vtable slots (the union of the aim and weapons sites).

Evidence: fixture only (118/118 CTest, the Normal, Research and Release
worker-check profiles, every contract generator `--check` including
`--steamclient`). The hidden runs on the merged build are the coordinator's
next step: the weapons scenario, the native play rehearsal (`-Mode play`,
then `-WanSim bad`), the aim scenario and the Steam self-test (with
`coop-hud.txt`); then the two-PC acceptance with a friend.

## October 9, 2026: aim parity, the partner's gun aims where the remote aims (fixture-tested, hidden runs pending)

`wp/aim-parity` ([details](native-puppet.md#the-aim-target-mirror-aim-parity-partner_native_aimpt)).
The standing/crouched aim cycle (body act 12/15 swinging +-30 degrees every
~0.5 s while the remote holds act 11) is not the look (O7 refuted by runs
1bffe18a/14ab2e00/8919e533) but instance 1's network aim target `Q+0x10[1]`,
which only the online synchronizer feeds: offline the partner's gun aimed at
the world origin and the aim-turn predicate `0x10A0AB0` (> 80 degrees) kept
firing. The aim-target mirror (`partner_native_aimpt=look`, on in the worker
gun stage and Play-OfflineCoop; debug `aimpt off|look|point|lab|status`)
writes a point 100 m along the remote's look into that slot and its sample
record inside block 1's drain window, so the game's own gun pipeline computes
the remote's aim point, aim pose and turn decisions; refusals, restore,
forget and fault latch as the rates mirror. Contract checker: 449 code sites,
98 vtable slots (`tools/partner_aim_sites.py`: the predicate, the aim
parameters, the aim point builder, the Q methods and allocator, the handler
table, a census of `Q+0x10` writers). Telemetry `native.aim` plus the ring's
`r60_1`/`pred1` and the 1 Hz `partner_native_aim` line. New hidden scenario
`tools/worker/scenarios/partner-native-aim.txt`; the facing scenario gains
aim sections (baseline off, the qualified +-1500 aim turns, camera-rotated,
strafe, walk, crouched) and `aimturn` takes the proven +-1500 stimulus only
(dx 900 never turned a settled aimed body). Fixture evidence only.

Review fixes on the same branch: the scenarios' in-session A/B returns the
guest with `aimpt lab` (back to lab.ini) instead of forcing `look`, so
`-LabSetting partner_native_aimpt=off|point` really decides the rest of the
run; a restore through a changed chain is logged
(`partner_aim=restore_dropped`) instead of dropped silently; the mirror's
event lines share a budget of 64 per mission epoch (a chain flipping every
frame cannot flood the log); `native.aim` gains `written_mm` and the verdict
`slot` ours|other|na (the old criterion q10 == target could not hold while
the aim or the body moved: the target leads the hook's write by one tick) and
`aimpt_err_mm` is measured against the write; the mirror is bound before the
hook reports ready; Play-OfflineCoop gains `-NoAimMirror`; new fixture checks
cover the restore drop, the line budget and the window-close shutdown
(nothing written or restored after it).
## October 9, 2026: Steam code joins and the online UX (fixture-tested, hidden runs pending)

`wp/steam-join` ([steam-online.md](steam-online.md) section 20). Code joins
between players who are not Steam friends no longer depend on the 2015
steam_api's callback manager alone: the host sets the GNS function pointers
204/205 (`ISteamNetworkingUtils004::SetConfigValue`) and pumps
`ISteamNetworkingSockets012::RunCallbacks` on its own transport thread, with
the old 1251 callback and polling kept (`net_steam_dispatch=both|fnptr|
callback|poll`, Play-Coop `-SteamDispatch`, for the two-PC A/B). Every
steamclient property this relies on is asserted by `--steamclient` (RTTI,
strings and shapes; ten binary mutations fail it) and gated again at run
time; a refusal only falls back to callback + poll. The guest learns why a
join failed (1252, plain sentences shared by the native side and Play-Coop),
a partner who quits shows as "left" at once, and an in-game status line with
toasts (`coop_hud`, compact in play, off in the one-PC lab) shows the link,
route, round trip and reasons. The link build id is the version plus the
commit. New hidden scenario `coop-hud.txt`; the self-test gains stage F/D
(`fn=ok`). Evidence is fixture only: `steam_transport_tests` (573 checks),
`link_session_tests` (134), `lab_config_tests`, `steam_shim_patches_tests`,
`worker_debug_tests`, the friend-package and worker-session tests.
## October 9, 2026: native partner online (fixture-tested, hidden runs pending)

`wp/online-native` ([two-pc-play.md](two-pc-play.md) "Native partner",
[native-puppet.md](native-puppet.md) "Online"). Two-PC play now runs the
native partner by default on both PCs: Play-Coop writes `partner_input_send=1`
and `partner_native_profile=gun` (`-Partner clips` opts out), which the loader
expands into exactly the gun stage the hidden worker qualified; the raw native
keys stay play refusals. An online network policy (play default) keeps
stalls, link drops, a paused remote and range hiding from latching the native
partner off: they fall back as network reasons, re-entry waits 2 s of good
link quality, a paused remote is held neutral for 10 s, a flapping link keeps
the clips; the first catch-up snap after an input gap never counts as
diverging; a stall keeps the last stick 400 ms; edge sets repeat 5 times. The
sender sends v4 whatever its own drive, and a capability message (channel 2)
tells each PC how the other shows its body (link-status.json and the
Play-Coop status line: `partner=... native; you on the other PC: native` or
`clips (<reason>)`). The gun profile strips HOLD/RELOAD/FIRE while body 1 has
no gun (outside 30010); the stock46 gun pieces run only for a configured
native gun profile, and the friend loader comes from
`Invoke-WorkerChecks -Profile Release` (`build.ps1 -ReleaseGun`), packaged by
`package.ps1 -Friend` with provenance. The hidden worker gains `-WanSim` and
`-Mode play` (a play rehearsal on 127.0.0.1 with the worker-only
`play_rehearsal=1`, admitted by `PlayRehearsal`), scenarios `play-native.txt`
and `partner-native-online.txt`. Fixture evidence only (lab_config,
partner_native, partner_input, partner_recon, partner_driver, player_sync,
worker_protocol, steam_shim_patches, worker_session, worker_checks,
build_profile, lab_scripts and friend_package tests).

## October 9, 2026: weapons2 review fixes (fixture-tested, hidden runs pending)

`wp/weapons2` review. Input frame v5 is negotiated (v4 plus a v5 probe every
2 s until the peer's own v5 arrives), so a peer build without v5 keeps its
whole native input stream instead of refusing every 116-byte frame. Body 1's
never-filled slots hold handle 0: the plan, the follower (`reason=empty`) and
`slots.handles` now treat equip 0 as empty. The generalized writer digests the
viewer's ammo/weapon vars and shoot count around each dispatch
(`call_unverified; reason=local_ammo_changed` stops the allocation), reports
why it waits (`equip_writer=waiting; reason=..`), orders the slots only once a
manifest is known, and, like the preload and both stock46 pieces, stops when
the game window closes (`shutting_down`). Play runs `partner_native_slot=call`
as `write` from Configure. The preload line names the failing admission.
Residual (documented): the bullet allow-list rests on the weapon categories;
the scenario now checks every `probe1.shots` increase against
`fire.suppressed`.

## October 9, 2026: the generalized preload and writer (fixture-tested, hidden runs pending)

`wp/weapons2` ([details](native-puppet.md#generalized-preload-and-writer-w2w3-partner_native_equip)).
Body 1 can now carry the other player's three loadout slots or same-type
substitutes in every partner-body mission (30010, 30020, 30050), so the slot
follower switches natively and the gate keeps FIRE/RELOAD live after a switch.
The stock46 writer contract is parameterized (`kStock46` unchanged); `Derive`
builds any stock gun's target from the executable tables and runtime banks
(type -> slot, package row, `DB6AC0` dependency packages, optional or NULL
optional, cache entry). W2: one public `RequestLoadToEquipMissionBlock` of the
manifest's base ids per allocation, refused before the call unless nothing is
evicted, the queue has room and no viewer-referenced entry is touched; exact
readback. W3: per slot one dispatch per allocation (tier A1 the base id, tier C
a resident protected stock gun of the same type, tier D refuse), never a filled
slot, full preflight and readback per slot. FIRE: the bullet categories 1..6;
grenade launchers and missiles stay masked. `partner_native_equip=stock46|manifest|substitute`;
the weapons keys follow the native gun profile when not named (play gets
event/write/substitute). No new CMake option (`-ReleaseGun` ships it).
`tools/generate_equip_writer_pins.py --check`: 106 gates (stock46's 92
unchanged) and the static facts. Fixture evidence only.

## October 9, 2026: the partner's own reload and weapon switch (fixture-tested, hidden runs pending)

`wp/weapons2` ([details](native-puppet.md#the-partners-own-reload-r1r6-partner_native_reload)).
The hidden weapons runs showed body 1 already starting its own native reload
from the remote's R; the magazine mirror then wrote the refilled magazine in
the middle of it and the reload mode never left. New: input frame v5 (the
sender's gun mode, a reload counter, a switch counter and its current slot),
the event machine (`partner_native_reload=event`: overlay RELOAD taps until
body 1 enters 11/12, holstered and automatic reloads included, the raw R/CARRY
stripped from the remote's sample), the mirror's freeze and reconcile with
`rec+0x3E8` (`raw|event`), the post-phase-1 probe (bit 20/24 are only visible
inside the drain), the slot follower (`partner_native_slot=write|call`: instance
1's equipment row gets the remote's slot the way SetCurrentSlot writes it for a
non-local id; the game then switches natively), the slot plan and the
generalized weapon gate (mid-switch and unqualified-type strips). Defaults:
lab.ini `mirror`/`off`; the worker gun stage and Play-OfflineCoop run
`event`/`write`. Contract checker: 446 code sites, 101 vtable slots (the mode
table emulation, the reload op table, SetCurrentSlot's non-local data path).
Not done: the generalized writer/preload (other weapons on body 1 need their
own runtime qualification captures); until then switching to the hip or
secondary slot refuses (`empty`) and body 1 keeps stock46. Fixture evidence only.

## October 8, 2026: Steam online transport (fixture-tested, never run)

Two-PC play can now connect **online through Steam** with no port
forwarding, VPN or extra program ([design and record](steam-online.md)).
Link v2 runs unchanged on `ISteamNetworkingMessages002` (channel 0x4D47,
flags 0x21) from `ISteamClient017::GetISteamGenericInterface`, acquired once
in the post-RunCallbacks owner context behind hash, export-byte, module-path,
RTTI and forwarder-shape gates; Steam signals, traverses NAT and falls back to
Valve's relays. The game's own ISteamNetworking005, callbacks 1202/1203,
lobbies, sessions, S_IS_ONLINE, the FOB synchronizer and the Konami servers
are untouched. Players find each other with an `MGS2-...@<SteamID64>` code
(S1) or a Steam invite from the new F9 Partner / Co-op > Online panel (S2;
callback 337, registered by a SteamAPI_Init IAT wrapper in DllMain together
with the host's 1251, play + steam only), with a rich-presence beacon and a
4 Hz poll-accept as callback-free fallbacks. Play-Coop has a new menu
(`[O]` Host online, `[J]` Join online, `[L]` the unchanged LAN/VPN flow);
the lab stays UDP and refuses `net_transport=steam`. Evidence is fixture
only: `steam_transport_tests` (393 checks, two link v2 sessions end to end
over fake Steam interfaces with loss/reorder/duplication, a broken session
restarted by AutoRestartBrokenSession, busy REJECT, wrong-code hint, Release
exactly once, gates on synthetic PE images, fault latch, shutdown), the
extended config/status/shim/worker tests, the friend-package tests (online
host/join/wait flows) and the contract generator (`--check`, and
`--steamclient` for Valve's unpinned steamclient64). Next: the coordinator's
hidden self-test (`tools/worker/scenarios/steam-selftest.txt`), then the
two-PC acceptance list of [steam-online.md](steam-online.md) section 15 with
a friend. S3 (accepting an invite starts MGSV straight into co-op) awaits a
user decision. A review pass fixed three transport defects before any run: polls
of invited friends no longer use the 1251 rate tokens (one invited friend could
starve a code join), an established session no longer logs `steam=accept` at
4 Hz (steamclient's AcceptSessionWithUser answers true for it), and a fault while
copying a 337 payload can no longer leave the callback lock held.
## October 9, 2026: native facing and the partner's own fire/reload (fixture-tested, hidden runs pending)

`wp/native-polish`. Facing ([details](native-puppet.md#facing)): the facing
governor (`partner_native_face=idle` by default under the native drive: quiet
idle turns at 180 deg/s, one settle snap after a placement/snap/teleport or in
fall/land acts, a contest guard), native placements with a neutral window
instead of a disarm (a disarmed drain while active now leaves: `unwindowed`),
the placement yaw check, a windowed teleport (3 m in 200 ms) and the look
source hook (`partner_native_aim=looksrc`, opt-in, the O7 A/B run).
Weapons ([details](native-puppet.md#weapons)): drain windows isolate the pad's
sample rings (the partner's trigger read the viewer's), the magazine mirror
writes the per-instance store row (the record alone was overwritten every
frame), the shot noise filter (`partner_native_noise=filter`; FIRE needs it
live) and the weapon gate (input frame v4 xflags Equip 0x80 / Gamepad 0x40,
bytes 82..83). Contract checker: 405 code sites, 96 vtable slots. New hidden
scenario `tools/worker/scenarios/partner-native-facing.txt`; the weapons
scenario gains the switch-fire and trigger/noise/store proof lines. Play keeps
visual fire off by default (`-VisualFire`). Fixture evidence only.

Review fixes on the same branch: the weapon gate compares a customized
weapon's alias as its manifest base id (the lab save's 873 is base 46; the
raw compare would have masked FIRE/RELOAD for the whole weapons run), the
store row write is bounded by the store's row count `Y+0x10` (pinned: the
store init `0xEE92F0`), an idle facing turn never starts while the body is
still turning on its own (< 30 deg/s) and restarts after a tick gap instead
of snapping, the facing writes and the look source install stop at the
window-close shutdown (`partner_defer::ShuttingDown`), the drain consumers'
previous held words reset per mission, and the facing scenario's
`face-place-viewer-aims` is captured by the guest (the viewer holding RMB).
## October 8, 2026: partner name tag and friendly crosshair (fixture-tested, never run)

A name tag now floats above the co-op partner's head and a green ring appears at the
screen centre while you aim at him ([partner-nametag.md](partner-nametag.md)). Both
are drawn by our DX11 overlay (under the F9 menu) from a ~100-byte frame the Lua
owner thread publishes; Present never reads game memory. The game thread reads the
game's own render viewport and camera (`*(0x2ba1668)` -> +0x58 -> +0x18: view,
projection, VP, rect), the same unlocked reads the player update makes on that
thread, through ReadProcessMemory only; nothing is written and no game function is
called. `tools/generate_nametag_contract.py --check` pins 24 sites and re-runs the
projection builder 0x241b00 in an SSE emulator (clip.w = view z, x mirrored,
16:9 clamp); the loader compares them at startup and every tick checks vtables,
the projection shape and View x Proj == VP, hiding the tag on any refusal. Names
travel on reliable link channel 3 (lab.ini `nametag_name`, the lab `self_name`,
coop.ini `name` or the Steam persona; sanitized on both ends, fallback "Player 2" /
"Host"). Hidden during demos and the iDroid (STATE_V1 field 13), pause/loading
(stale frames), mission end and whenever the partner body is hidden. Settings:
lab.ini/coop.ini keys and the F9 Partner page (`nametag on|off|status`,
`nametag crosshair aim|always|off`, handled natively). Worker state.json gains a
`"nametag"` object; the hidden scenario `tools/worker/scenarios/partner-nametag.txt`
holds the runtime proof (the worker's `labsetting` now takes a `host`/`guest`
prefix). Fixture evidence only: `debug_menu_tests` (projection, refusals, capsule,
sanitizer, wire, commands), `debug_overlay_tests` (DX11 pixel readback of the label
and the ring), `worker_debug_tests`, `lab_config_tests`, `debug_menu_lua_check`.
Review fixes: the projection shape check accepted only P11 < 20 (a vertical FOV
above ~5.7 deg), so a binocular or scope zoom would have hidden the tag and the
friendly ring exactly while sniping; it now accepts 0.05..1000. The census waited
for nothing and could log a null-selector line at mission load and never the
vtables; it now logs the first tick that read the viewport vtable (and once more
when that one refused and a later one passes). A torn read no longer reuses a
camera from before another refusal or a hidden stretch. While off, terminating, in
a demo or the iDroid, or with the partner hidden the tick reads nothing (names
only). STATE_V1's HUD bits reach the tag also when the debug menu is off. The
overlay reads the clock after taking the frame (no false stale frame at a tick
boundary) and maps NDC over the backbuffer under an open menu too.

## October 8, 2026: native actions and weapons integrated (fixture-tested, never run together)

`integ/finish` carries both native work packages below. One input frame v4
(112 bytes, payload 84) carries both new groups: the sender's movement rates
(xflags Rates 0x10, bytes 72..77) and its magazine (xflags Mag 0x20, bytes
78..81); every native sender sends v4 (rates only unless
`partner_native_rates=off`, the magazine only with a readable gun). v1..v3
frames still decode; the unreleased single-feature v4 drafts (104 and 108
bytes) are refused. Masks are the union: loco/census 0x2240006 (EVADE), gun
stage 0x224005E (EVADE, HOLD, RELOAD and FIRE with `partner_native_fire=visual`);
the worker gate admits exactly the three stages Get-WorkerPartnerSettings
emits (a gun stage without fire needs a gate change). Contract checker: 365
code sites, 91 vtable slots (both packages' sites, slots and assertions).

## October 8, 2026: native movement actions (fixture-tested, hidden runs pending)

The dive was missing on the other player's screen because EVADE (0x40000,
Space) was outside every native mask. The native masks are now 0x2240006
(loco) and 0x224005E (gun, with visual fire since the weapons merge), in
lab_config, the worker gate and stages and Play-OfflineCoop; ACTION (ride,
knife kill, pickup) stays masked. The
contract pins the PlayerPad enum pairs, the three EVADE press consumers (act
27) and their network-remote gates. Sprint and crawl ran at 0.835 of the
remote's speed because the per-id movement rate record of instance 1 kept the
default 1.0: `partner_native_rates=mirror` (worker loco/gun stages and Play)
copies the remote's dash/crawl rates (input frame v4) into instance 1's own
record inside the drain window and restores them on leave. The native
reconciliation no longer pins a body that is only slower (split runaway,
`lag`, along-track nudges, a stop grace), and the arbiter times only the idle
runaway. New hidden scenario `tools/worker/scenarios/partner-native-actions.txt`
([details](native-puppet.md#actions)). Fixture evidence only.
Review fixes: the rates write re-derives instance 1's own record in the hook
(id 1, the rate service of instance 1's block PS; else `owner`), the restore
outside the drain re-checks its chain, a legacy Arm restores like a disarm,
the forgotten mission's Arm never re-applies after MissionEnd, `lag` lines
have their own log cap, and the contract census pins all nine EVADE pad
readers (six state-specific ones beyond act 27 are an open item).

## October 8, 2026: native partner weapons (fixture-tested, never run)

The partner body's shots are now visible on the other screen without real
bullets ([design](native-puppet.md#weapons)). The RequestBullet owner filter
swaps slot 0 of the bullet iface vtables (`0x2337DE8`, `0x2337D18`) and drops
only requests stamped with instance 1's id; muzzle flash, recoil, sound and
casings stay. The gun stage of Play-OfflineCoop and the worker now runs
`partner_native_fire=visual` (mask `0x224005E` with EVADE since the actions
merge); FIRE stays masked whenever
the filter is not installed, live and armed. Reload: instance 1's non-local
shots never spend its magazine, so input frame v4 carries the sender's
magazine and the drain window mirrors it into instance 1's gun record before
its update (FIRE/RELOAD edges and remote changes), which lets CanReload pass
exactly when the remote can reload. Loadout: each copy sends its equipment
manifest (weapons, customized aliases, receivers mapped to stock ids) on
channel 2 and logs the apply verdict; only stock46 can be loaded on body 1.
Contract checker (weapons package alone): 325 sites, 87 slots, a reachability proof that the
non-local shoot path skips every local side effect, and an owner-stamp
assertion (every other shot request builder stamps a typed owner). Open: the shot noise of
visual shots (census pins its interface), projectile weapons, a generalized
writer. Hidden scenario: `tools/worker/scenarios/partner-native-weapons.txt`.

## October 8, 2026: spawn sync (fixture-tested, runtime unverified)

Guards placed with the F9 menu and the host's per-soldier debug actions now reach
the guest's copy of the same soldier ([debug-menu.md](debug-menu.md) "Spawn sync").
The host announces `ENEMY_DEBUG` (0x22) on the world link at level 2+ with a
matching roster (not W1); the guest applies it and answers `ENEMY_DEBUG_ACK`
(0x23). A native placement is repeated on the guest through the same verified
`Warp()`: its target is a grant `world::Core` records when the guest's Lua pops the
message, never a Lua value, checked within 20 m (60 m for corrections) of the
guest's view of the host and behind the existing lab key, contract, per-grant
budget and latch (`generate_soldier_warp_contract.py --check` now also pins the two
callers). Route placements and come / ignore / notice / disable / enable replicate
as plain script calls, so they also work in play mode. Placed guards stay in an
active area on both PCs, a 60 s settle window corrects a guard that drifts more
than 6 m, and both menus get history rows plus the read-only `world_status` and
`spawn_sync` commands.

Kill / sleep / faint / Fulton / clear reported `sync=none` because outcome sync (W1)
never turned on when the host had not crossed a free-roam area trap: the
start-of-mission snapshot then had no readable soldier and the guest refused it
forever. The host now infers its area from the nearest readable soldier's command
post (bookkeeping only, no game call). Evidence is fixture-only (`world_wire_tests`,
`world_receipt_tests`, `soldier_warp_tests`, `debug_menu_tests`, the world-align and
debug-menu Lua checks); the hidden scenario `tools/worker/scenarios/spawn-sync.txt`
and the extended `debug-menu.txt` hold the guest-side proof lines.

## October 8, 2026: native puppet (fixture-tested, default off, never run)

The partner body can now be driven by the remote player's real pad through the
game's own locomotion, action and gun systems ([design](native-puppet.md)).
Block 1's per-block job is deferred out of the parallel batch (H1: job slot
`0x2295420`) and run on the main thread after the join (H2: call `0xBF4D55`),
inside the game's own TLS scratch scope and entry read lock, in a drain window
that swaps in instance 1's pad. Native windows also follow the remote HOLD
latch, never open while a hook-thread batch is in flight, and (aim stage) write
the remote look angles after phase 0. A pure arbiter switches between this
drive and the proven `dm=map` + xform partner, which stays the automatic
fallback. Input frame v3 carries the sender's look, camera and heading; the
equipment manifest codec and channel-2 drain are in place (sender pending).
Everything is behind `partner_drive=native` and `partner_input_defer` (default
legacy/off; play mode refuses it). The worker gains a `native` profile with
census/loco/gun stages and `tools/worker/scenarios/partner-native.txt`. Fixture
evidence only: the contract generator re-verifies every site against the exe,
`partner_defer_tests` runs the deferral end to end on a synthetic image with
real worker threads, and the driver tests walk the full entering/leaving
sequence with fakes. The coordinator's hidden runs (S0 census, then S1 loco)
decide whether any default changes.
## October 8, 2026: native "Spawn enemy here" (fixture-tested, runtime unverified)

**Spawn enemy here** now moves the nearest unused living, enabled, on-foot guard
(15-500 m) 5 m in front of the host, facing him. It sends the engine's own
set-transform message `0x287158b07327` to the soldier's GameObjectId through the
GameObject manager's SendCommand (`[0x2305e70+0x38]` = `0xbf3a80` on the static
object `0x2c38008`), the same call the player carry code makes at `0x1055b7a`.
The soldier handler `0x1360f90` then calls `T->SetPose`/`SetRot`, the slots the
partner transform already uses. The feature is default off behind lab.ini
`debug_soldier_warp=1`, lab only (play forces it off, bit `0x10000`).
`tools/generate_soldier_warp_contract.py` pins 19 sites, 10 vtable slots and the
unwind entries of the call path, and the loader refuses on any byte difference.
Without "He comes at me" the guard gets `SetEnableRestrictNotice` (not hostile);
with it he gets `SetDiscoveryPlayer` after landing. An unrealized guard is
force-realized and the message is retried for up to 150 updates. Any native
refusal, an older loader or a timeout falls back to the previous route snap
(`mode=route;why=..`). The 8-per-mission cap covers both modes; the guest is
unaffected beyond the existing world sync.

One dispatch hop cannot be pinned statically: the per-type handle object and its
vtable +0x10 are built by protected code. Each attempt resolves the chain
read-only, logs `soldier_warp_census` lines and reports `leaf=pinned` only for an
argument-preserving forwarder to a soldier subobject whose +0xa8 slot is the
verified handler. In that case it first mirrors the handler's own index, realized
and stance checks. Otherwise it uses the engine's dispatch (`leaf=engine`). The
fixture `soldier_warp_tests` executes the real SendCommand, GetHandle, registry
and dispatch bytes against recorders. The hidden scenario
`tools/worker/scenarios/debug-menu.txt` (new header `labsetting
debug_soldier_warp=1`) expects `mode=warp` with `d` of 10 m or less. Nothing has
run in the game yet. Open: realize latency of far guards, patrol AI or navmesh
correction moving the guard afterwards, and host-local positions.

Review fixes (same day, still fixture-only): a recognized forwarder hop that does
not reach the verified handler on a soldier subobject now refuses unsent (`leaf`)
instead of using the engine dispatch. Each `spawn_enemy here` command may move one
GameObjectId, at most 160 times and never after it was handled, and only while it is
the most recently popped command. Any answer other than `r=0;h=1` or `r=-1;h=0`, or a
caught exception, latches the feature off for the process
(`soldier_warp=disabled; reason=anomaly|fault`). Native `command`/`mission`
refusals end the command (`spawn_context`) instead of snapping a guard to a route
during travel or teardown. A pending placement dropped by the menu's context check
releases its force-realize only while its mission is still in gameplay. The route
fallback's final result keeps `why=`. The debug-menu scenario now requires 30010 or
30020.

## October 7, 2026: partner facing while moving (fixture-tested, runtime unverified)

The sender published `vars.playerRotY`, which is the control direction. Hidden run
442f60813e23409da970d49be0c21788 showed it stepping -59 to -14 degrees in one
tick on W -> W+A while the local body transform eased over about 250 ms. The
receiver lerped that step over one 33 ms packet gap, so the run loop snapped.
The sender now publishes the census-validated local body-transform yaw. It
falls back to `vars.playerRotY` when the read fails or the body is more than
2 m from the published position. A `partner_facing=source; yaw=body|vars`
line is logged on each change. With apply=xform/hybrid on the net source, the
receiver also limits the applied yaw to 900 deg/s with eased acceleration as
a safety net (`partner_facing_dps`, 0 restores the exact yaw). Position stays
exact, and teleport, relink, Warp, realization and stale poses snap.
Resident turn clips were mirrored: increasing yaw is a left turn in the
right-handed Fox frame, so the hidden run's mouse-right pivots (-75 degrees)
had selected `tn_l90`. The mapping now selects `tn_r90` for them. Worker
`state.json` carries a per-tick `facing` ring (source, source body,
target, applied and body yaw, clip, move, snap) for
`tools/worker/scenarios/partner-turns-moving.txt`. That run of 370f570
confirmed the mirror fix: the right pivot played clip 14, which finished
naturally after 834 ms. The body-yaw sender is not yet verified at runtime.
## October 7, 2026: debug menu overhaul (source)

The F9 menu now has a header with pending/telemetry state, a quick bar with recent commands, a searchable category rail (Player, Enemies, Partner / Co-op, World, Tools), per-button results and inline confirmation, and keyboard shortcuts ([guide](debug-menu.md)). **Spawn enemy here** heads the Enemies page: it moves an existing living guard onto the scored patrol route nearest the player (host-local; no new soldier allocation or coordinate placement). New host/local actions include heal/auto-heal, Fulton infinity, slow motion, go to partner, time +1 h, a nearby-soldier table with per-soldier kill/sleep/faint/Fulton/mark and command-post phase. Busy actions now queue instead of being refused. Worker builds draw the menu into captured frames and drive it with `menu ...` text (`tools/worker/scenarios/debug-menu.txt`). Source and fixture checks only; nothing here has run in the game yet, and the APIs marked unverified need live evidence.

## October 7, 2026: animation continuation after Claude's hidden-worker tests

Claude committed the prior work and private-desktop runtime fixes on
`dev/oct7-continue`, through 805a8ec. Its two-game Afghanistan locomotion run
904f772a2df64307a469db4ef05fface closed normally, restored both installations
and matched all real saves. Controlled continuation disproved the proposed
stand/crouch inversion: the host started crouching and the old runner captured
movement after input expired. Corrected hidden tests confirm standing run 11,
sprint 5, walk 10, crouch idle/move 1/4 and prone idle/crawl 7/6. Worker Raw
Input modifiers and a private cursor getter now supply actual source gaits and
gun input. Captures require native timing, held input and fresh pose readback.

Two production turn losses are fixed: raw pivots survive delayed visual
movement/stance, and new matching-stance turns survive old native-clip
confirmation. Existing lifecycle, gun/movement cancellation, freshness and
native stop/confirmation gates remain. Recording regressions and all 110
audited suites pass. Final hidden run `1a2de304f22243d2b8900a7f2301c742`
qualified both standing directions and a crouched pivot (about 75/75/82
degrees), confirmed finite turn playback and return to idle, and completed
16 timely frames including held strafe. Both instances closed normally, both
baselines were restored and all ten real saves matched. No system input,
focus or desktop switch was used.

Automatic upper-body aim/fire/reload playback remains disabled. Genuine
source aim/fire/reload reaches the guest; guarded read-only comparison finds
populated source weapon animation inputs but all ten partner inputs zero.
Static requested-allocation and default-allocator proofs are frozen; the
natural current-context scheduler caller/root/lifecycle is still unqualified.
The V3 timing reader refused without samples and was not expanded. The
separate unfinished `wp/debug-menu` worktree is not merged in this animation
continuation. See [build-specific evidence and remaining work](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-10-07-hidden-worker-animation.md).

Normal release `DC5F5FDF...B1B3755` passed all 110 audited suites with worker
and all research options OFF, and is installed in both local game copies and
staged at the usual launcher's `build/native/dinput8.dll`. Owned rollback
backups are retained. Post-install checks confirm unchanged retail/Steam/runtime
files and all ten real saves, with no game or worker remaining. This exact
normal DLL has automated checks; the fresh gameplay evidence above belongs to
the separate Worker-ON build. Nothing was pushed.

## October 7, 2026: initial background-worker implementation

The user requested implementation and continued development without Computer
Use or visible game windows. A default-off worker is being integrated with
separate build profiles, audited fixture selection, a private-desktop
supervisor/DX11 probe, bounded in-process input/backbuffer capture and the
existing Lua-owner debug action queue. The dedicated Normal/worker-ON build
has passed all109 audited CTests,198 input-only checks and72 runner-policy
checks. All11 native research flags remain OFF. Three bounded private DX11
probe attempts refused before qualification; the latest records an empty
private-window enumeration (FALSE, error0, no bad callback) at startup.
These were the initial implementation results, before Claude's subsequent
qualified private-desktop game runs described above. Receipts are retained under
`build/worker-runs` and `build/worker-runtime`.
No global input or visible fallback is authorized. Gun pose work retains its
existing exact-build, native-contract, owner and lifecycle gates.

The separate read-only clip-timing V3 correction is implemented in the
research workbench: metadata is bounded to16 layer pointers/128 bytes,
and only explicitly selected layers are followed. It accepts the saved H8
nine-layer allocation while retaining the exact native allocation/table
bounds. Root verification passed617 detached checks,24 complete disk-code
spans and21 exact instruction assertions. This has no process reads or game
writes in its detached/static modes; actual V3 game readback remains pending.

## October 7, 2026: paused at user request

H8 completed a fresh two-game Mother Base â†’ helicopter â†’ Afghanistan transition with host-only Continue and guest arrival acknowledgments. The explicit manual upper-idle diagnostic passed live: remote Fire116 returned to Idle89 at its natural update, with no queued state and an unchanged lower-channel request record. Automatic gun animation remains disabled: selecting Fire still produces no visible gun pose, and all ten remote Motion/Gun inputs remain zero. Guest weapon initialization succeeded; the host writer refused before dispatch after travel because its selected equipment bank was not empty. The pure animation reader's refusal was independently diagnosed as its five-layer limit against an actual nine-layer body; the proposed bounded reader correction has not been implemented.

The H8 build passed105 non-long suites and both long installer suites. Both games closed normally at18:52:22 UTC; all ten real save/config files match run-start and backup. Active lab settings and markers were disabled, and both installations were restored through the owned installer to the previously tested normal21 DLL `D4AF2A7F...CF5230` and baseline runtime archive. The newer accepted-source collector and gun diagnostics remain saved in source/research artifacts and require a fresh normal-profile release check. No new release or commit was made. [H8 evidence and next steps](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-10-07-animation-debug.md#october-7-h8-manual-idle-validation-and-user-requested-wrap-up).

## October 7, 2026: animation and debug continuation

Current source allows eight existing resident turn clips alongside the 13 locomotion/stance clips, while retaining the original runtime archive hash and bytes. A fresh normal-profile run with all research flags OFF verified all eight resources finishing naturally and actual aimed mouse pivots selecting crouched turn 18 and standing turn 16. The subsequent T7 research run verified accepted-source collection with a crouched pivot and two separately settled identical pivots: both queued clips played once for950 ms and returned to idle. Visual interpolation remains unchanged. Measured improvement over the previous collector and a fresh normal-profile check of the newer source remain pending.

F9 patrol placement visibly reused an armed living guard in a development build; its local combat killed the player and host Continue reloaded both instances. Fresh H2/H3/H5 environment checks separately verified host noon/rain with guest clock differences of 1â€“2 seconds and matching rainy weather. Guard placement remains host-local, with no new soldier allocation or AI replication claim.

Gun intent and the default-off bounded action-history companion have live transport receipts. Automatic upper gun playback remains disabled. H3 manually selected upper Fire child 116 but showed no gun/firing pose or finite finish. H5 installed the corrected observation hooks and captured one natural local End path through departure to idle. Its remote Fire request with zero blend still showed low arms and no gun. The writer refused before native dispatch; a later resource-mapping review requires qualification beyond a predicted bank. Remote update observation succeeded after a documented Warp advanced the body epoch from 0 to 1; source now accepts the legitimate initial epoch 0, with focused regression checks. Both H4 and H5 closed normally and all ten real save/config files remained unchanged. Later candidates require fresh gameplay checks. [Build-specific evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-10-07-animation-debug.md).

H6 verified remote update observation at initial epoch 0, but its writer refused because an old helper read logical45 rather than native46. The corrected H7 build subsequently dispatched the stock46 writer once in each game and bound handle605C4. A remote rifle is visible on the host partner's back. Pure owned readback in both games confirms active equipment46 and unchanged local slots/handles. A separately primed manual upper request selects Fire116, but its ten Gun/MotionVars inputs and caches remain zero, with low arms and no gun pose or finite finish. H7 also re-tested host noon/rain with guest clock differences of1â€“2 seconds and matching rain in both views. H6 and H7 closed normally with all ten real save/config files unchanged. Automatic gun playback remains disabled. The subsequent H8 run validates only the separately gated manual upper-idle diagnostic, as described above.

Normal packaging now explicitly disables all local research options before building, preventing cached experiment settings from entering a normal package. Supplied DLLs are packaged with their existing settings; the tool does not claim those settings are disabled.

## October 6, 2026: Enemy controls in source development

The F9 Enemy section now has camp and patrol-route selectors, host-only **Spawn guard on selected patrol** / **Spawn guard at closest camp**, and player teleports to the selected camp or last confirmed guard position. Placement reuses at most eight distinct normal living guards per mission, within 150 m of the player, and requests placement on an authored route at point 0. It refuses dead, recovered, unknown or unloaded guards; it does not allocate a new soldier. Guard placement remains a local host debug action, not replicated AI.

Camp travel prefers a readable living guard position, then falls back to the camp's actual declared player checkpoint through the retail `TppCheckPoint.GetCheckPointLocator` contract. Unloaded camps can use that validated pose for distance sorting. Missing/invalid locators refuse without guessed coordinates. Source fixtures pass **4,987 checks** and **224 exact-retail checks**, including strict vector method receivers and checkpoint fallback. **Live wilderness travel and visible guard placement are pending**; the dated installed-build evidence below does not validate these additions.

## October 6, 2026: debug menu development 0.11.1

The final debug-menu DLL is `0CB53F278436EA845D4240BC82EB2B09A4F51F08CA9C5C826460168067E898C6`, installed and hash-verified in both copies. Its only change from the B3 candidate is UI labels/help, including wording the current-gun action as **Refill**. Functional logic is unchanged. Final run `build/lab/runs/20261006-164030` separately verifies F9 in both Afghanistan games, corrected refill wording and inventory readback, automatic close on focus loss, and the menu's default partner preset on both roles. This manual preparation began with stage 0; the menu enabled stage 3 / net / xform / map / net look and received `partner_applied`. Both bodies then had parts `3,3`. After F9 closed, host W moved to `(797494,339428,1640439)` mm and guest remote/body readback matched at rest with 0 mm error ([movement views](../build/lab/runs/20261006-164030/screens/debug-final-movement-guest-164725.png), [preset view](../build/lab/runs/20261006-164030/screens/debug-final-partners-host-164712.png)). Both title menus used Continue in this smoke; it does not re-test host-only title following. The broader cheat results below remain B3 evidence.

Both final-run games closed normally through `WM_CLOSE`, with no fuse process and all ten real save/config files unchanged against run start and latest backup ([stop receipt](../build/lab/runs/20261006-164030/stop.json)). Actual Disable archived the offline markers and removed active lab/coop config and link secrets ([Disable log](../build/codex-debug-menu-release-disable-20261006.log)). Final [hash/file-state verification](../build/codex-debug-menu-release-verification-20261006.json) confirms the installed DLL, original executable/Steam DLL, runtime archive and ImGui license in both folders. Normal launches remain inactive; a normal real-save launch was not run. Defender's final DLL scan found no threats ([scan log](../build/codex-debug-menu-release-defender-20261006.log)).

B3 DLL `B3B8EE78C8222EE35CDEF08C608B59EED9F15D5010A9F48E2BE8249902428F02` was tested with two copied-save game instances on one PC in [run `20261006-163001`](../build/lab/runs/20261006-163001/run.json). Host-only Continue brought both players on foot to Afghanistan (`30010`). F9 opened the menu in both games, and scrolling exposed its header and footer. While the host menu captured a held W, position stayed `(800452,339300,1638765)` mm. With the menu closed, W moved the host to `(795493,339440,1641624)` mm; guest remote/body readback matched at rest with 0 mm error.

The B3 inventory receipt reports hip/back/secondary IDs `871/873/875`, active inventory slot `1`. Dropping the current hip weapon produced a visible **AM MRS-4** pickup ([host view](../build/lab/runs/20261006-163001/screens/debug-pickup-visible-host-163722.png)). Refilling the hip gun confirmed inventory ammo `30`, stock `210`, active slot `1`; its receipt remains `render_unverified`, so this does not prove a new gun was equipped or rendered. Bookmark `0`, a 2 m step and return confirmed position errors of 0.003 m and 0.000 m. Noon had both clock readback and visible daylight; the rainy-weather receipt coincided with visible rain ([host view](../build/lab/runs/20261006-163001/screens/debug-world-controls-host-163946.png)). Both games then closed normally through `WM_CLOSE`, with no fuse process; all ten real save/config files matched run-start and backup hashes ([stop receipt](../build/lab/runs/20261006-163001/stop.json)).

All **82 automated suites pass in aggregate after corrections**: the [combined run](../build/codex-debug-menu-final-ctest-20261006.log) passed 81/82 before the composition-fixture correction; [13 focused checks](../build/codex-debug-menu-layout-tests-20261006.log) then passed, including both world Lua checks. The [friend-package recheck](../build/codex-debug-menu-package-tests-20261006.log) also passed after source-inventory coverage was updated. The final wording DLL passes [13/13 focused checks in 3.10 s](../build/codex-debug-menu-release-tests-20261006.log) after all fixture targets were rebuilt. This is aggregate evidence, not a single clean 82-suite run on the final wording DLL.

The overlay uses Windows SDK D3D11/DXGI and Win32 input with pinned Dear ImGui core/backends. The render thread queues actions and reads copied telemetry; guarded actions are consumed on the game's natural Lua update, with mission-generation checks and bounded receipts. Complete ImGui MIT terms are packaged, and fixtures cover upgrade/restore of legacy seven-target packages to the new eight-target inventory. The default animation map has 13 locomotion/stance clips; partner gun aim/fire/reload and full turn actions are not transported. Arbitrary new guards and unloaded-gun spawning are not implemented. Pickups and guard enable/disable remain local debug operations. A real two-PC session, complete campaign/progression and physical damage death remain unverified.

## October 6, 2026: previous world build 0.11.1

The current route uses our own offline UDP link and separate co-op save copies. Development tests with two independently controlled game instances on one PC have shown both player bodies and synchronized movement in normal Afghanistan (`30010`) and Africa (`30020`) free roam. Mother Base (`30050`) remains supported. ACC, story missions and alternate mission packs such as `30011` do not receive a partner body.

The previous world-build **0.11.1** DLL was `4666A7DA7ED2711CED7D397ED0EFF92E41782C27127DFF9374C63B1DBBF99D65` from `046bb63`. It was installed and hash-verified in both copies for the following run. Run `build/lab/runs/20261006-115055` uses the offline link and redirected co-op saves. Host-only native title Continue sends all 47 records; the guest starts on foot at the host's same position `(1232994,20775,-125517)` mm, with player IDs `0,1` and both parts states `3,3`. Both copies pair in Africa `30020`, epoch `3`, roster `2220786415`, 265 soldiers and 68 CPs. Guest alignment `id=1` accepts 151 unknown soldiers, zero divergences and `w1=1`; host readiness follows 16 ms later. Native keyboard input independently moves each player; reciprocal commanded/body positions match at rest with 0 mm difference. That stream requires 28 keys / 47 records, including `svars.ply_isUsedPlayerInitialAction`; an exact-source `TppPlayer.Init` regression covers the earlier helicopter/foot mismatch.

At that stage, all 78 automated suites passed in aggregate after the lab installer test was rerun separately. Its arrival/title/recovery checks passed 11/11 ([check log](../build/codex-arrival-fix-tests-20261006.log)). The local [Play-World-Coop.bat](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/Play-World-Coop.bat) launches the configured lab at world level 3.

On that previous DLL, the host checkpoint probe sends hash `0`, version `1`, pose `(1233009,21370,-123172)` mm / yaw `0`; the guest's actual VarSave callback confirms it 17 ms later. The injected guest PLAYER_DEAD opens Mission Failed in both games. Host-only native menu Continue then replays 47 records and returns both players on foot to the same checkpoint ground position `(1233009,20870,-123172)` mm. They pair at epoch `4`; guest alignment `id=2` accepts 151 unknowns, zero divergences and `w1=1`, followed by host readiness 34 ms later. Parts are `3,3` and reciprocal transform readback is 0 mm.

Host-only travel then follows Africa `30020` â†’ ACC `40020` â†’ Afghanistan `30010`, using 47 records on each leg. Both players visibly use the fresh helicopter entry. The guest reaches epoch `6`; alignment `id=3` accepts 271 soldiers / 77 CPs with 191 unknowns, zero divergences and `w1=1` at `368261214`, followed by host readiness 32 ms later. After both helicopter exits, the host ground view shows Snake and an opaque Quiet partner; native guest W for 1.5 s and host W for 6 s have reciprocal rested transform differences of 0 mm.

Setting `world=0` in both games logs UNPAIRED reason `5` and inactive level `0`; partner transform sync remains active. Both games then accept `WM_CLOSE` and exit normally at 12:08:41, with no fuse process. All ten real save/config files match both run-start and latest-backup hashes ([stop receipt](../build/lab/runs/20261006-115055/stop.json), [verification log](../build/codex-4666-live-stop-20261006.log)). Cleanup commit `9d5aac6` archives the active offline markers. Actual Disable leaves marker, `lab.ini`, `coop.ini` and link secret absent in both game folders, with the owned loader and game/archive hashes intact ([Disable log](../build/codex-final-disable-20261006.log), [file-state/hash receipt](../build/codex-final-install-verification-20261006.json)). The focused `lab_scripts_tests` suite passes 1/1. File-state inspection and the source gate establish inactive pass-through; a normal launch after cleanup was not run because it would use the real saves. Disk-write completion and physical damage death are not claimed.

The earlier diagnostic build was `59CD23348386815A84DA0CE9D18DB2B308E52BE21D1B5DDF9C254FC1910A9B13` from `5ab2c5a`, which distinguishes unreadable initial state from malformed alignment streams. Run `build/lab/runs/20261006-113833` found a startup mismatch: the host was on foot while the guest retained its older helicopter initial action despite matching seeded coordinates. Both remote bodies realized with parts `3,3` and zero transform readback error, but that startup did not establish an unobscured two-player on-foot view. Exact `TppPlayer.Init` source selects that action from `ply_initialPlayerState=RIDEON_HELICOPTER` and the guest's unsynchronized `svars.ply_isUsedPlayerInitialAction=false`; the host has `true` after landing. The 4666 world build added that mandatory seed and regression fixture; its separate run above verifies on-foot startup and fresh helicopter entry. Both games close normally afterward, and all ten real save/config files match run-start and latest-backup hashes.

The preceding live-tested integration DLL is `171041C2A598FD758FCC67A0B19B59BE4EC6C35FBA4CD98C2529BF946A9E97D6`. Run `build/lab/runs/20261006-111828` verifies native host title Continue following to Mother Base (`30050`) and host-only `goto=30010` following through ACC (`40050`) to Afghanistan (`30010`). The guest receives all 27 seed keys / 46 records; the host's title context is applied once, then ordinary allocation clears it. Both games reach CFG `30010`, epoch `5`, roster `2772775463`, 271 soldiers and 77 CPs. Guest alignment `id=1` is accepted with 191 unknown soldiers, zero divergences and `w1=1`; unknown states are held until their areas become readable, not treated as synchronized AI or positions.

The earlier diagnostic/title/recovery subset passed 11/11 ([check log](../build/codex-alignment-diagnostic-tests-20261006.log)).

The preceding test build's Afghanistan movement matches the local/remote coordinates in both directions with 0 mm difference at rest; screenshots show the separate Snake and Quiet bodies. Stage 0 â†’ 3 warp rearming places the partner before enabling its bits, with ready parts and zero warp error.

The host checkpoint probe publishes hash `0`, version `2`; the guest's actual VarSave callback confirms the exact host pose 19 ms later. An injected guest PLAYER_DEAD request opens Mission Failed on both games.

Only the host used native menu Continue; both copies reloaded and paired at epoch `6`. The guest became ready about 74 s after the host; all 271 host soldiers were initially unknown, so snapshots remained gated. When the host area activated, the unchanged DLL recovered: guest snapshot `id=36` was accepted about 1.9 s later with 206 unknown soldiers, zero divergences and `w1=1`, followed by host readiness. This verifies recovery with a loading delay.

The preceding test build also follows host-only travel to Africa: both copies reach `30020`, epoch `8`, roster `2220786415`, 265 soldiers and 68 CPs; guest alignment `id=37` is accepted with 157 unknown soldiers, zero divergences and `w1=1`. Native movement on each copy shows both player bodies and matching reciprocal transforms at rest. The Afghanistan range check hides the partner at 200.004 m and re-enters at 149.969 m with placement before realization. Both processes then close normally via `WM_CLOSE`; all ten real save/config files match both run-start and latest-backup hashes. The later diagnostic build exposed the initial-action mismatch described above. Physical damage death and the killed soldier's post-Continue readback remain unverified. The October 5 W0â€“W2 tests remain the recorded evidence for shared enemy outcomes, alerts, clock and weather. [Integrated run and historical results](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-10-05-world-sync.md).

Initial pairing still requires the copied-save games to reach the same supported scene. A real two-PC session, complete campaign gameplay/progression and four players remain unverified. The [friend launcher](two-pc-play.md) uses the same offline UDP route and copied saves; the earlier native Steam-join investigations are historical.

Source commits `7ec1e39` and `07520e0` add the local free-roam controllers, mission gates and distance safety. `a4a5aab` versions the source and package as **0.11.1**. The field test ran the partner changes in a DLL still labelled **0.11.0**, before W3/W4 integration; it does not establish live results for the final integrated 0.11.1 DLL.

Run `build/lab/runs/20261006-095606` verified the 13-entry archive installed in both copies. Afghanistan guest movement matched the host's partner body to about 20 mm, and both copies hid the partner at 200 m. Africa loaded the normal replacement pack with two IDs and parts states `3,3`; movement command/readback differences were 0â€“18 mm. Screenshots are retained under that run's `screens/`. Both games accepted `WM_CLOSE` and exited normally; all ten real save/config files were unchanged against both the run-start manifest and latest backup. [Field test details](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-10-05-world-sync.md).

## Earlier development records

The dated sections below retain their original builds, test counts and limitations. They describe earlier native-session or offline development stages; their claims of current installation or missing guest control do not override the October 6 state above.

## October 4, 2026 (round 4): input-drive reconciliation, stance taps, prone, host-input leak (branch `wp/partner-anim4`, not run live)

Acts on live round 3 (see [partner-anim-appearance.md](partner-anim-appearance.md), "Round 4"):

- **Reconciliation** (`native/partner_recon`, pure, 101 unit checks): apply=input places the body once with a disarmed Warp at the time-aligned remote pose and verifies it before arming; drift is corrected by position-only nudges (T.vt+0x58); xform snaps only on a large sustained error; Warp only on teleport, relink, far, vertical or diverging; a body that keeps moving while the remote is idle (the round 3 stuck state) is pinned instead of sawtoothing. S+0x20[1] carries the body readback.
- **Stance and prone**: words default `hpr`, mask `0x2200006`, input frame v2 with edge sets and 12-frame hold/tap words, and `hold=own`, an exact replica of the classifier 0x434ED0 over instance 1's own history.
- **Host-input leak**: every window installs a PP vtable copy whose key-manager slots answer from the injected words, PP+0x14 carries the world heading, PP+0x278/0x4B0 are shadowed, the camera sandbox covers the request block, and `wrap=census|window` measures (and can close) the other walks of block 1's tree.
- `Play-OfflineCoop.ps1` now starts with the input drive; [W] falls back to the proven Warp. CTest passes 66 of 66 suites.
- **Review fixes (second pass)**: [I]/[P] and I6 send `correct=3000 runaway=pin` (a leftover `correct=0` had turned the reconciliation off silently); stale edge sets are dropped after a disarm and a stall keeps the held level without edges; a lasting stance mismatch gets one corrective tap (a hold for prone); the runaway pin also covers a stuck body with a moving remote; stale input with a live pose pins instead of a 10 m Warp cycle; wrap windows inject the level only; source/apply-only triggers re-place; the fallback Warp uses the 100 ms interpolated pose again; `speed_mmps` no longer sits on its span boundary. 153 recon checks including a lag model.


## October 4, 2026: partner animation and appearance (branch `wp/partner-anim`, not run live)

The fully offline partner (offline-v1, our own UDP link; [test report](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-10-04.md)) gets three new pieces. All three are experimental and switchable, and none has run live yet. See [partner-anim-appearance.md](partner-anim-appearance.md).

- **Animation.** Each copy sends its slot-0 animation fields (act, stance, speed) in a v2 transform frame. Each copy can apply them to instance 1 in one of these ways: the Q arrays, `SetStance` 0xFF5B10 on a change, or the stance request component.
- **Movement.** A smooth native body transform (`xform`/`hybrid`, Warp's own T.vt+0x70/+0x68 calls without the resets) is available. It falls back to the proven Lua Warp whenever a call is refused.
- **Appearance.** Each copy sends its look, and the receiver applies it with `SetPartsInfoAtInstanceIndex` (`look=net`; `look=quiet|snake` for local tests). offline-v1 now answers the character receipts, and the lab guest plays Quiet.

The defaults keep the proven behaviour. `Play-OfflineCoop.ps1` turns on the appearance sync and the Quiet guest; it keeps the proven Warp movement and turns the unverified native paths on one at a time from its console ([M] smooth movement, [A] stance sync), or all at once with `-Experimental`. Review fixes: received anim/look values are bounded (stance 1..3 with the valid flag, locomotion act ids 1..6 only, the Snake face table index), a refused stance call is retried instead of sticking, the census keeps the stance prerequisites out of the transform verdict, the stance lines have separate caps plus a summary, the A1/A3 plans use hybrid movement so Warp's resets cannot mask the result, and `Prepare-LabInstance.ps1 -Offline -Character default` reruns the proven E-series without the Quiet guest. CTest passes 63 of 63 suites.

## October 3-4, 2026: 0.11.0 (branch `coop-lab`)

**At this native Steam-session stage, no guest join had been verified.** CTest passed 57 of 57 suites. The current offline partner route is recorded above. See [October 3 test report](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-10-03.md).

- **Single-PC lab** ([how-to](single-pc-testing.md), [protocol](lab-protocol.md), `tools/lab/`):
  - Two instances run side by side. The guest has a fake SteamID and a renamed mutex, both instances use isolated save copies, and ISteamNetworking is emulated over loopback. The master URL is redirected to a local fuse.
  - Verified live: both instances boot together, the guest reaches Mother Base on its own save copy, the real saves are unchanged, and the join trigger, status file and connect diagnostics all work.
- **Blocker for the single-PC lab:** co-op sessions need a master-server login. fuse `f75b41d` (written for exe 1.0.15.3) makes the 1.0.15.4 client go offline. Offline, the host's `CreateHostSession` returns 0, and the guest's IsConnectReady sees login state 3 < 4. Deriving the version the client expects, and bypassing the login gate, were both stopped by the assistant's safety checks. Neither is pursued in this project.
- **Testing route at the time:** two PCs with real accounts on the normal online service. That earlier friend-test plan is retained in [friend test](friend-test.md); the current package uses the [offline two-PC workflow](two-pc-play.md). The runtime archive is game-derived and is never bundled.
- **Characters:** `coop.ini character=` (default guest = Quiet, fallback DD soldier). The Continue-path fix (`ALWAYS_APPLY_TEMPORATY_PLAYER_PARTS_SETTING` for one allocation) and the UpdatePartsStatus Quiet/Ocelot bypass are in place, but have not been confirmed live. D-Dog is not a playable type. See [characters](characters.md).
- **Other 0.11.0 changes:** Lua and patch checks run in CTest. Damage-table field 13 clearing is opt-in, which matches Dynamite. Client join reset/re-arm: Ctrl+F9 or `reset.trigger`.

## Earlier record

Updated 2026-09-12. Immediate scope: two independently controlled players together in a scene with shared gameplay/combat, using direct attributed Dynamite adaptations. Complete campaign scripting/progression and four players are deferred. **Playable co-op remains unfinished and unverified.**

## September 12 code and installation

**Recorded installation: 0.10.33, Host mode.** DLL SHA-256 `FCB0E16A5AF1EE3E1884C7EB14D2EADFD33AC83280D7C0104EA2190EA564F9B6`. The user authorized testing on September 12. All 21 native suites passed, including session release and initialization/termination ownership regressions. Actual-source transformation, complete Lua parse, prestart execution and 91 lifecycle forwarding/cleanup checks passed. Installer upgrade/restore testing also found and fixed PowerShell's empty backup-path conversion in `File.Replace`. The owned live install succeeded. [Test results](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-09-12.md), [package workflow](local-package.md).

**Loading and termination fixes, 0.10.31â€“33:** live runs exposed termination on a different worker and OnInitialize running independently of the eventual gameplay thread. 0.10.33 records matched initialization callbacks and establishes runtime ownership at OnMissionGameStart. Its live Diagnostic run (PID 20680) prepared Mother Base, polled the manual guest shortcut, restored rules on a different termination worker and accepted the next scene's setup. It returned to title and quit normally, with all seven save/config hashes and original EXE/Steam DLL hashes unchanged. Automated F8/Ctrl+F8 taps produced no input receipt; a physical shortcut check remains unverified.

**Final Host regression, 0.10.33:** PID 52824 created the Mother Base host on loading worker 83844 and transferred its ownership to gameplay thread 54152. The native session reached state 3 with acceptance enabled; a local dive visibly responded. Return to title restored rules, observed the game's session destruction, cleared ownership and accepted the next scene's allocation/initialization/runtime callbacks. The game quit normally. All seven save/config files and original EXE/Steam DLL hashes still match the baseline. The game is closed; Host mode remains selected. This run covers an owned host's loading-to-gameplay handoff without a guest. Owned-session termination on a foreign worker and connected-peer teardown remain unverified.

**First live run, 0.10.29:** PID 87784 loaded Mother Base, created a native host, set acceptance to 1 and responded to a local dive. Return to title exposed stale adapter ownership after the game had already destroyed the session. Native rule restoration succeeded; the title menu and normal quit worked. All seven save/config files, the executable and shipped Steam DLL matched their prelaunch hashes. 0.10.30 reconciles ownership only after repeatedly reading empty session/manager/main singleton slots at teardown; its live regression passed: ownership cleared and the next scene's allocation/runtime receipts were accepted. The title menu appeared. ACC loading also exercised a successful worker-to-runtime handoff. No guest joined.

**Earlier guest preparation, 0.10.30:** PID 59896 loaded Mother Base in Diagnostic mode, completed equipment preparation and passed the then-automatic native client prerequisite check without a join call. Return to title restored scoped rules and accepted the next scene's setup. This run and its host regression quit normally, with unchanged save/config and original game binary hashes. This diagnostic did not create a fake player or verify independent guest control.

The preceding 0.10.28/29 fixed the user's skipped worker-thread host preparation and outgoing-scene cleanup ownership. The user's standalone transform codec and Diagnostic role remain; neither creates or controls a remote actor. [Startup handoff](mission-thread-handoff.md), [outgoing-scene cleanup](outgoing-scene-cleanup.md).

Version 0.10.27 fixed operational teardown depending on the bounded diagnostic trace. Its dedicated termination signal remains in use before the other mission wrappers. [Teardown fix](termination-boundary.md).

Version 0.10.26 adds Dynamite's exact FOB-enemy-to-buddy icon conversion for an existing marker during the owned session. It does not create, extend or synchronize markers. Climbing and path-result fallback hooks remain unmapped. [Marker implementation](teammate-markers.md).

The preceding 0.10.25 adds host/guest status messages from owned-session and repeated roster observations. Submitted requests remain distinct from established sessions with two present members. It also decoded and retained the original connection-readiness gate. [Feedback and gate audit](session-feedback.md).

**Single-PC feasibility:** read-only inventory found an RTX 5080, approximately 64 GB RAM and VirtualBox 7.2.4 with an existing powered-off Windows 11 VM. A second isolated instance is plausible, not verified; guest graphics compatibility and a second licensed Steam identity remain prerequisites. No VM changes or launches occurred. [Investigation](single-pc-testing.md).

## Recorded live progress

- 0.10.0 created a native host, loaded Mother Base with two indexed storage entries and online initialization, returned to title and quit normally.
- 0.10.1 enabled native host acceptance after startup and disabled it before termination. No guest joined.
- 0.10.4 guest setup loaded Mother Base and returned to title. Read-only native client prerequisites passed; no native guest request or peer was involved. The game was subsequently quit normally and all seven save/config hashes matched the backup. [Result](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/client-loading-result-2026-09-09.json).
- Eighteen native suites passed for 0.10.5 before the user requested no further tests. The September 12 test run above supersedes that automated result; no test count establishes a connected player.

## September 12 remaining work

A real guest connection and independent actor control remain unproven. Guest/enemy damage, enemy targeting, world authority and synchronization, death/respawn and disconnect handling still need completion and eventual gameplay verification. Host targeting now has an integrated code path, but remote position publication and targeting behavior are unverified. Disconnect and owned-close code are now connected, but event delivery and asynchronous cleanup remain unverified; automatic reconnect is not enabled. Death/respawn now has a scoped code path, but actual recovery, placement, simultaneous deaths and post-respawn control remain unverified. Sight, damage-controller, hostage flag, trap, soldier action-task and CP status rules are now implemented but unverified. Online-result suppression and the full close-session branch adaptation are implemented but unverified. The concrete FOB service implementation remains unmapped; its existing game-owned dispatch is retained. These adaptations do not establish complete shared combat. Only one PC is available. A local simulated peer could exercise packet logic or actor loading, but would not verify a real remote connection or independent game client.

## Previous allocation and readiness investigations

The records in this section describe earlier versions and processes. The current installation is recorded above; these older entries are historical observations.

**Latest investigation: normal job ownership and cancellation mapped.** The dispatcher holds the same lock used by manager destruction across the bandwidth callback. Registration takes a scheduler reference; finished and pending cancellation branches clear scheduler ownership before releasing it. A new exact-build analyzer passes 103 additional instruction checks and three table checks on top of the allocation analysis; all 38 code bodies match live in PID 14496 at 23:22:33 UTC. Session and bandwidth-manager fields remain null. This resolves the static callback-lock/reference relationship, not live cancellation latency or transition behavior. [Evidence and next trial prerequisites](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/job-lifetime.md). Installed 0.9.6 and prepared 0.9.7 are unchanged; native host creation remains disabled.

**Latest development: isolated allocation bridge, not installed or linked into the probe.** The real fixture detour passes 1,605 checks, including exact-site routing, cross-thread scopes, exception recovery, concurrent forwarding and restored bytes; all 15 suites pass. The exact-build allocation analyzer now passes 102 instruction predicates, three table checks and 22 live body comparisons. Job shutdown locks, clears the manager pointer, unlocks and sets a cancellation-request bit; scheduler exclusion and completed cleanup remain unresolved. An earlier fixture result changed with diagnostic instrumentation; the final separate-translation-unit fixture passes five repeats, with that uncertainty recorded. [Implementation, evidence and remaining work](allocation-bridge.md).

**Prepared code: 0.9.7, not installed.** The player constructor records a status-derived bit that allocation consumes to select network setup and interface queries. The new exact-build analyzer checks 46 instructions, one method-table entry and four complete bodies. The preserved four-slot sample has this bit clear. Early host prerequisites and later player readiness are now separate functions; native creation still refuses unconditionally at the lifetime gate. All 14 native suites pass. [Initialization and decision](player-session-initialization.md). Built DLL SHA-256: `B5595BC2AF720E17F1089540109BF53CA44C3FC035BB9386173187AABDFDCCA6`.

**Current installed loader: 0.9.6, four-slot archive with precise host-readiness diagnostics.** The player implementation is null before Mission.Start and at OnInitialize entry. At OnMissionGameStart entry, the unchanged preflight passes with index limit four and a null current block. All three observations ran on the TppMain selection thread. Native host creation remains disabled. Mother Base returned to title within 16 seconds and menu input worked. [Current trial](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/host-readiness-result-2026-09-09.json), [timing and next investigation](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/host-readiness-timing.md).

The preceding 0.9.4 four-slot version completed a load/return cycle, and 0.9.3 completed two two-slot cycles and quit normally. Removing the overlapping original controller resolves the observed transition failure in these tests. It does not prove a safe native networking lifecycle or independent additional characters. [Four-slot result](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/four-controller-replacement-result-2026-09-09.json), [two-slot result](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/controller-replacement-result-2026-09-09.json).

Installed DLL SHA-256: `E7EDC9A04B544607510F547759D9F5008A2E494344DCCA6438AD44279A66507B`. Archive SHA-256: `74BFCD3656356EE4B08EEBCB9912D4806BA0B9F7F948D0C86E6E412D6E930666`, 3,720,192 bytes, seven entries. All five four-slot runtime entries remain byte-identical to the pinned count-one source; the renamed Mother Base package preserves 44 definitions and its complete FPK partner. All 14 native suites pass. The combined host/trace insertion passes 16 exact-source assertions, 400 concurrent selections and 72 actual-module Lua checks. Package selection code is unchanged from the 0.9.3 checks. [Assets](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/four-controller-replacement-assets-2026-09-09.json), [current installation](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/loader-0.9.6-host-readiness-install-2026-09-09.json).

PID 14496, started 22:43:04 UTC, is at the title menu with Continue selected. No debugger was used. All seven save/config hashes match the pre-readiness backup, and the executable and Steam DLL hashes remain unchanged. Native host creation remains disabled. The prior PID 103508 quit normally at 22:42:26 UTC.

The previous 0.9.1 four-slot/count-one and 0.9.2 two-slot controls both reproduced the identical state-11 teardown wait: 13,419,280 bytes against 12,641,280, excess 778,000. Both games closed normally, and all seven save/config files matched. Restoring parts count and allocation scaling was insufficient to fix the wait. [Two-slot result](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/two-player-control-result-2026-09-09.json). Native host creation remains disabled.

The installed mission trace preserves yields, errors, arguments and nil/multiple return values; its exact-source checks passed 15 assertions and 400 concurrent selections, and its Lua fixture passed 213 checks. The new block dispatch analysis passes 27 instruction checks plus three jump-table targets. These tests and reserved storage do not demonstrate remote actors or playable co-op.

The earlier 0.8.0 selection validation passed all 14 native suites, 14 additional exact-source assertions, 400 concurrent selections and 73 actual-module Lua checks. The 0.9.0 reset checks above supersede that Lua count. A read-only query mapper resolves eight GameObject bindings and checks four paths; indexed-ID and type-existence implementations remain unresolved and were not called. [Local checks](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/player-package-local-checks-2026-09-09.log), [query mapping](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/gameobject-query-analysis-2026-09-09.json).

**Next engine milestone:** verify the native host/client allocation lifetime and player initialization path before enabling networking, then establish independent control of a second actor and expand to four. The four-slot scene now unloads successfully, but the separate native networking allocation-domain concern remains unresolved. Live Steam transport, authenticated peers, world replication, save isolation and campaign adapters remain unfinished.

The [current-build allocation analysis](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/host-allocation-lifetime.md) passes 63 instruction predicates and two deletion-table checks across 16 bodies. Host and client creation share the identified 88-byte manager and 144-byte job allocations; the context-sensitive allocator and compatible heap-header release branch are mapped. All 16 bodies also match PID 14496 after the readiness trial; mapped session and bandwidth-manager members remain null. No allocator patch or session call was made. Player construction and allocation now show a status-derived network path, supporting early setup as the candidate. Next finish persistent session allocation routing and asynchronous cleanup; concrete service virtual targets and a complete live host lifecycle remain unverified.

## Historical observations

The records below retain their original process/version context; statements such as installed, current, latest or paused below describe the recorded experiment, not the current running-game state above.

Latest live retry: **18:49:53 UTC, PID 91880**, natural script-loader entry and EAX=0 return captured, debugger detached without timeout. Computer Use verified the startup notice, title prompt and keyboard transition to the main menu at 69%. The game was left at the main menu. All seven save/config hashes matched the previous post-motion check; executable, Steam DLL and installed probe hashes also matched. [Retry continuation](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/script-load-retry-2026-09-09/continuation.json). This supersedes the earlier unresolved visual continuation for the retry only; the bridge remains uninstalled and co-op is not playable.

The [script-loading bridge](script-load-bridge.md) passes 2,010 fixture calls, including concurrent use, callback reentry and restoration after removal. All 14 CTest suites pass in Release. A natural game script load and successful parser return were captured separately before the installed trial; this does not demonstrate script replacement or multiplayer.

Latest asset work: extracted the installed two-player actor package and staged seven allocation changes for four players. Both definitions compiled and passed XML/FPKD/FPK round trips. The [allocation experiment](four-player-allocation-assets.md) is uninstalled; memory sizing and live actors remain unverified.

Source direction: the user authorized Dynamite inspection and reuse. The [source port](dynamite-integration.md) records pinned licenses, 25 incompatible upstream patches, and 16 current-build function candidates from a second source whose supported executable checksum matches the installed game. No new hooks or co-op gameplay are enabled.

Latest: the [original player-record observer](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/live-player-record-writer.md) captured a natural bank copy and two writes through updater `0x12CE870`, immediately after store `0x12CF266`. Expanded registers matched bank 1 record zero and source index zero. A 24-instruction static check maps its caller and source-array dataflow. Fixture capture/refusal/deadline/copy-limit checks passed; the game continued and was left paused. All seven save/config hashes match the preceding post-motion check. Actor ownership, lifetime and playable co-op remain unresolved.

| Area | Evidence | Limit |
| --- | --- | --- |
| Source reuse | Existing project-authored core and probe; Dynamite reuse authorized 2026-09-09; pinned Dynamite/tpp-mod references and inactive porting tools added with licenses | No derived game hooks or remote actors installed yet |
| Native build | MSVC 19.50, x64 Windows, C++17, Release, static runtime | Compilation does not validate game structures |
| Automated suite | All 14 CTest suites pass in Release, including the script-loading bridge, typed payload, host input receiver, Steam packet and natural getter forwarding suites | Not a multiplayer gameplay test |
| Session/protocol tests | 2,891 checks passed, including header mutations, invalid inputs, slot recycling, independent sequence wrap, mission barriers, stale objective events and held-input expiry | In-process coordinator only |
| Peer router tests | Admission, bounded queueing, separate peers, duplicates, reordering, wrap and disconnection checks passed | No Steam authentication, ACK/retry implementation or socket connection |
| Typed player payloads | Original versioned input and complete pose-roster formats pass 362 checks, including malformed values, slot/source binding and roster authority | No transport or engine adapter. Post-delivery helpers alone cannot roll back the router cursor |
| Host input receiver | 783 admission/rollback checks pass; invalid high sequences, stale connections/slots and Session rejections preserve live routing/input state and unrelated event queues | Trusted adapter and exclusive ownership required; bounded router copying has no measured production throughput. Pose/event admission is separate unfinished work |
| Steam packet adapter | 124 fixture checks pass; three shipped flat-export wrappers independently matched to public signatures, bounded reads and no-delay/reliable sends implemented | Inactive in the loader; no real Steam traffic, live context, channel ownership, authentication handshake or replay boundary verified |
| Natural Steam networking getter | Original 0.4.0 adds exact-DLL/export-gated passive pointer forwarding, bounded caller receipts, and 15,238 fixture assertions including repeated concurrent/reentrant scenarios; its import replacement installed with protection restored on a live launch | No interface pointer retained or used; natural getter execution and complete ownership remain separate evidence |
| Four-member lab | Four members admitted; fifth rejected; input loss/duplication and host loss exercised | No game actors or remote machines |
| DirectInput forwarding | Six exports tested against System32; menus responded, and original 0.4.0 accepted disembarkation, a dive and posture recovery at Mother Base | Controller and sustained locomotion remain unverified; short forward taps produced no measurable motion |
| Diagnostic installer recovery | Five isolated scenarios passed: install/remove, foreign loader refusal, changed loader refusal, missing-executable removal and partial-manifest recovery | Inert fixtures; no live game files changed by these tests |
| Actual MGSV loader execution | `probe.log` at 2026-09-09 13:01:52 UTC recorded version 1.0.15.4, successful `DirectInput8Create` (`0x00000000`), System32 forwarding and loaded Steam DLL | No gameplay hooks, network sessions, saves, mission observations or remote player control |
| Live Steam import observer | Version 0.2.0 installed the callback pointer with page protection restored; observed 3,600 original callback calls and at least two callback threads | No custom traffic, game function calls or Lua ownership inference |
| Import patch checks | Real fixture DLL: 4,001 forwarded calls across threads, original error-state preservation and restored import/page protection; guard/no-access and executable pages rejected | Public-import observation only, not a validated game-function detour |
| Steam runtime reader | Five official opaque/context accessor signatures, explicit lifecycle gating, no cached pointers; fake-export tests pass | Not connected to a live verified initialization boundary |
| Passive Steam initialization observer | Original 0.3.0 forwards boolean/error/exception results; tests cover 1,000 concurrent observations and bounded overlap. Both live import patches installed with protection restored | No natural initialization call observed through 3,600 callbacks; successful initialization and shutdown coverage remain unproven |
| Natural-entry debugger fixture | Capture, deliberate wrong-byte gate and no-hit deadline passed. Paired mode captured result `37`; nested fixture skipped an inner return. New ReturnTimeout passed with the return breakpoint armed: strict interrupt/detach, original result and continuation preserved. Nested regression also passed | Separate debugger harness; fixture behavior does not prove game semantics. Saved no-hit artifact covers entry-only mode; earlier paired no-hit pass was not separately archived |
| Live natural network registrar | First entry captured on thread `42464`; second launch captured entry and original-frame return on thread `91732`, skipped an inner return and recorded `EAX=1`. Known vars backing pointer present; CDB detached, and startup input/title screen worked afterward | Result count for one registration call, not a connection/player count. No VM lifetime or ownership proof, active game function calls or persistent game hook |
| Mission/player sampler | Original external reader verified build and selected live code bytes; startup scene changes and one Mother Base dive/posture recovery were observed | Actor identity, lifetime, publication timing and complete sequence mapping remain unverified. Positive mission code or nonzero coordinates do not imply playable state |
| Published player-status reader | Independently decoded two fixed banks and named status bits; original bounded reader sampled the title screen with alternating bank selectors and `SQUAT`/`PARTS_ACTIVE` set. Release build and independent formula review passed | These flags also occur at the title screen. No active-actor readiness condition or publication cadence is established; selector rereads are not atomic snapshot proof |
| Published position candidate | Independently traced named near-player-position use of record `+0x10`; live dive moved the vector approximately 4.78 horizontal units, and recovery changed height with posture | One scene-level correlation. Producer actor, coordinate definition and freshness remain unresolved; two position sources disagreed in five dive samples |
| Natural Steam initialization | Ten original fixture cases passed; desktop-account live launch at 16:28:34 UTC captured the game's call at RVA `0x1428` and same-thread/same-stack return at `0x142E`, `AL=1`, then detached successfully | One successful natural call. No installed early boundary, shutdown serialization, live SDK access or multiplayer verified |
| Independent Lua analysis | 23 network bindings and registration/helper flows decoded; ten semantic checks pass under the executable fingerprint | Registration timing, live VM ABI/lifetime and thread ownership remain unverified |
| Campaign register | 51 main episode entries, 12 story quest candidates, six incomplete coverage inventories | Every segment remains unimplemented/unverified; sequence discovery is incomplete |

Raw evidence: [native test output](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-tests-2026-09-09.log), [actual game probe log](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/loader-probe-2026-09-09.log), [live callback trace](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/steam-observer-2026-09-09.log), [offline executable inventory](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/installed-build.json), [independent binding analysis](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/network-binding-analysis.md).

Historical installation: diagnostic proxy **0.5.0** was installed at `D:/Games/steamapps/common/MGS_TPP/dinput8.dll`, with its ownership record in `mgs-coop/probe-install.json`. Installed DLL SHA-256: `B2E65778F134693C46E5C78011FCE8D39ADFA403782A9532498390322F7BF3B2`. The previous 0.4.0 build and manifest are backed up under `build/backups/probe-0.4.0-20260909T185815Z`. The executable and shipped Steam DLL hashes were reverified unchanged. [Current installation evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/loader-0.5.0-install-2026-09-09.json).

Earlier Steam capture launch: 16:28:34 UTC, PID 76012, in the desktop user's Steam context. After the user explicitly approved the restart, the prior game quit normally. The original CDB observer captured the game's natural Steam Init call and matching return (`AL=1`), removed its breakpoints and detached, with exit code zero and no timeout. Separate Computer Use verification reached the title prompt and Enter opened the main menu at 69% completion; all seven save/config hashes still matched the prelaunch baseline at 16:31:39 UTC. [Continuation evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/game-steam-init-continuation-2026-09-09.json). See [Steam capture](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/game-steam-init-2026-09-09.json) and [current control observations](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/live-player-control-observation.md).

The preceding 15:06 launch, PID 42824, captured natural network registration and its matching return (`EAX=1`). Startup/title input, Continue and iDroid responded. Return to Mother Base triggered an automatic cutscene; later controls were left with the user after Computer Use detected input. On-foot movement correlation and controller support remain unverified. See [live registration evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/live-registration-observation.md).

The original and pre-control seven-file save/config sets remain preserved under `build/backups/save-before-observation-20260909T145101Z` and `save-before-control-20260909T152909Z`. Ordinary game autosaves changed `TPP_GAME_DATA1` at 15:04:45, 15:43:06 and 16:20:47; their semantic changes are unknown. Before the latest restart, the closed game's seven current files were copied to `build/backups/save-before-steam-init-20260909T162821Z`, with matching source-before/source-after/backup hashes. [New baseline](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/save-check-before-steam-init-2026-09-09.json). The tools have not overwritten original saves. These copies are not save isolation or tested restoration.

The CDB observer is a separate development launch tool, not an installed game hook. [Fixture trials](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/registration-debugger-trials.md) document capture, wrong-byte rejection and no-hit recovery using explicit `.detach /h`. Earlier rejected fixture detach attempts produced a Windows single-step error dialog; later fixture launches suppress their own error boxes. The first game observation launched under the isolated tooling account and failed Steam initialization; its debugger detached at the deadline, and that known failed startup was closed. Retrying in the desktop account produced the successful game capture above.

Field evidence includes [title samples](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/player-status-title-2026-09-09.jsonl), [helicopter/iDroid samples](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/player-status-helicopter-2026-09-09.jsonl), [Mother Base arrival samples](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/player-status-mother-base-arrival-2026-09-09.jsonl), and [published vector samples](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/player-vector-helicopter-2026-09-09.jsonl). The read-only sampler makes no game calls or writes. Arrival read mission `30050` and standing/normal-action flags during a cutscene; these remain insufficient as playability conditions. The separate Steam call-site composition is now [fixture-validated](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/steam-init-caller-investigation.md) and independently reviewed in the launcher; the later live trial captured a true Init return and verified title/menu continuation. [Typed codec/receiver format and limits](typed-payload-format.md), [latest shared build](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-tests-with-input-receiver-2026-09-09.log), and [individual test results](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-test-details-with-input-receiver-2026-09-09.log) document the portable work.

The [earlier Steam dispatch investigation](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/steam-early-entry-investigation.md) corrected the classification of pointer `0x20A0940`: it is an object virtual-method slot, not a CRT constructor entry. Its caller and object/Boolean ABI are mapped, but the current proxy has no deterministic outside-DllMain installation callback before that dispatch. The separate debugger now captured one successful natural Steam initialization; no new installed early hook is claimed.

## Earlier engine investigations

The [live Mother Base motion trial](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/live-player-motion-observation.md) correlated one visible Space dive and C posture recovery with the published vector and named status bits in PID 106392. Horizontal dive displacement was approximately 4.78 game units; recovery changed the sampled height with posture. Short forward taps produced no measurable displacement. Five dive samples disagreed between the two position sources despite stable selector rereads, so an atomic replication snapshot is not established. The reader now skips a changed-selector observation while retaining its finite capture limit; all 13 existing suites pass. The game is paused at Mother Base. Six save/config hashes still match the preserved baseline; ordinary arrival autosaved `TPP_GAME_DATA1` at 17:13:15 UTC. Actor identity, sustained movement, controller behavior and all co-op gameplay remain unverified.

The [live session-owner reader](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/live-network-owner-observation.md) subsequently followed the statically derived three-pointer owner chain in PID 106392 at 17:01:34 UTC. All five final samples matched constructor table `0x2223508` and observed state `0`/flag `0`, the constructor's initial values. This is a raw baseline, not proof of no transport or connection. The original optional `world_probe --network` reader performs bounded reads only and suppresses values on pointer/type changes; no game restart or writes occurred. The updated build and all 13 existing suites pass. The [extended static report](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/packet-manager-lifecycle-with-owner-2026-09-09.json) verifies 44 checks across 25 ranges.

The [native packet-manager investigation](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/packet-manager-lifecycle.md) now reproduces 39 static checks across 22 bounded ranges. Manager setup lazily creates a transport at `inner+0x18`, its constructor registers two Steam callbacks, and its update skips the mapped Steam availability call when that pointer is null. Two upstream construction routes are traced, but their Boolean activation predicate and concrete virtual dispatch to setup remain unresolved. This was file inspection only: it did not sample the pointer live, call game code, alter the installed loader or restart MGSV. The next observation must establish natural setup/update ownership; absent getter receipts alone do not establish a null transport. [Static evidence](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/packet-manager-lifecycle-2026-09-09.json).

The 0.4.0 live check subsequently loaded the existing helicopter through Continue. All seven save/config hashes still matched the preserved baseline at the recorded checkpoint. No natural networking getter receipt appeared through this scene check; the installed getter observer has not yet demonstrated live forwarding. Next investigate the independently mapped native packet-manager callers and their lifecycle, rather than inferring availability from an installed hook. [Live result](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/steam-getter-live-2026-09-09.json).

Latest launch: **16:41:22 UTC, PID 106392**, with original loader 0.4.0 and no debugger. All three import replacements (Init, callbacks and networking getter) installed with page protection restored. The game accepted the startup notice and title Enter and reached the main menu at 69%. The seven save/config hashes matched the preserved 16:28 baseline at 16:41:25 UTC. [Original getter design and exact callers](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/steam-networking-getter-observation.md), [13-suite build](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-tests-with-steam-getter-2026-09-09.log), [current loader trace](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/loader-probe-0.4.0-2026-09-09.log).

The [original Steam packet adapter](steam-packet-adapter.md) is now implemented with explicit borrowed-access gates and fake-export coverage, including three guest inputs reaching the real portable coordinator. [Latest build](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-tests-with-steam-packets-2026-09-09.log) and [detailed results](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-test-details-with-steam-packets-2026-09-09.log) supersede the 11-suite build above. The running MGSV process remained PID 42824 during this work; no UI input, live SDK call or loader installation occurred in this packet-adapter step.

Independently map a read-only mission/actor observation path in the exact executable. Use the offline inventory's data anchors to guide investigation; those string addresses are not callable function pointers. Record a verified build-specific function map and instruction checks before installing a hook. The first bridge should observe title screen, mission start, local actor state and transition back out, without modifying campaign state.

The [independent decoded analysis](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/network-binding-analysis.md) establishes that the network registrar builds a temporary 23-entry table on its stack. Its two inspected queries push Lua values and return a result count, and their shared accessor can lazily initialize state. Do not actively call these wrappers as diagnostic queries. No-hit recovery, the live VM-dispatch caller, and the paired natural registrar return are now observed. Next correlate [published player-status records](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/player-update-investigation.md) and [named player fields](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/player-observation-candidates.md) with a real loaded actor and transitions. A [natural Steam initialization call site](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/steam-init-caller-investigation.md) is now independently mapped for an earlier observation. The Steam callback pump remains unsuitable as an assumed single Lua-owner thread.

## Historical implementation order

1. Verify title-screen/input behavior with the installed original probe.
2. Port the authorized Dynamite engine paths using current-build source maps and existing observations. Verify safe update, Lua/actor ownership and build gates before enabling the bridge.
3. Prove Steam context ownership, compatible interfaces, authenticated peer identity, callbacks and a non-conflicting transport path using two real installations.
4. Prove a second fully interactive campaign actor with collision, equipment, damage and world synchronization.
5. Extend engine allocation, replication and ownership to three and four active actors. Test disconnect while remaining peers continue.
6. Build interaction reservations, enemy/AI authority, revive, shared extraction, cutscene/transition barriers and a complete checkpoint baseline.
7. Prove save isolation, host campaign ownership, reward transactions and interruption recovery before enabling campaign persistence.
8. Author the hospital opening adapter and remaining exceptional sequences. Verify each main mission and required story dependency using the campaign register.
9. Complete a fresh-to-ending four-player campaign with recorded evidence, then test replay, reconnect, difficult network conditions and recovery.

There is no release date estimate yet: the four-actor engine proof determines the scope of native work. Steam lobbies do not create actors or synchronize campaign scripts. Authorized Dynamite and current-build source reuse now accelerates the engine port; each integration still requires exact-build and runtime verification.

## Historical missing behavior

This describes the earlier probe/coordinator stage. The current offline transport, remote bodies and copied-save route are recorded at the top of this page.

No real networking transport or Steam lobby is connected. The installed script-load hook and bootstrap do not provide remote actors, runtime message dispatch, full snapshots, late join, campaign save redirection, mission adapters, voice chat, host migration or playable co-op UI. The portable host input receiver is groundwork for runtime integration.

The co-op release must not substitute spectators for playable hospital/ending segments or advertise passing development tests as campaign support.





Shared riding/carry static qualification: the exact ordinary Vehicle descriptor is ID23/hash61006B64A5FA; prior hash55624028B112 metadata describes a separate adjacent interaction controller. The existing live reader remains correct. Ordinary vehicle passenger entry accepts raw positions2/3 only through matched state/door geometry, occupancy admission and the same chosen seat across three native queries; bookkeeping entries do not attach a rider. The TPP/MGO comparison and same-seat/descriptor addenda are in `docs/evidence`. Pure queued-kind15 carry abort nomination requires neutral recipient readback before a separately qualified carrier End and cannot cancel an active attachment. Fresh cold resources alone do not prove that active drop physics and response lifetime are safe. All carry/shared-riding gameplay stays disabled; these source policies/evidence issue no native pickup, drop, mount or MGO calls.

Further source work: each drained job now owns its callback rather than using a process-global pointer that a nested refused job could replace before dispatch. A targeted nested HUD-before callback regression preserves outer/nested callback identities, arguments, input and cleanup. Pure cold carry rollback and an atomic reader/writer pose-exclusion proposal are integrated as fixtures only; the exclusion protocol has no installed native hook or gameplay activation. Exact prologue FOX2/resource comparison identifies `player2_cypr.mtar` as a player motion candidate and confirms the ordinary horse rig assets match. The prologue SequenceWork manager is global and the ordinary horse has a single rider slot, so shared horse entry, passenger aiming and carried-person loading remain unimplemented. Final normal worker-disabled Release and Research each passed156/156 after the callback and pure-policy deltas. Friend-package1207 checks passed; those final deltas do not change the launcher scripts. The subsequent hidden sleep/wake repeat passed all 18 requirements. No native carry/riding or body1 sleep activation is granted by these fixtures.


Final carry/prologue ownership evidence: 69 complete carry sites, 8 slots and 2 constants establish recipient-first active retirement. Carrier-first cancellation can enable the physics-drop branch; the native End adapter still lacks mandatory pose/lifetime admission and remaining exit-interface qualification. CS carry callback owners are raw pointers, so readable callback entries/refcounts alone do not exclude model teardown. The prologue provider is descriptor0B/hash58FEFCCA07E1, with 17 complete sites and 8 slots: its shared manager registration is local-body only, natural admission requires statusword1E and selected actualID, and the global selector is manager+158. Corrected RIP targets are2C1BF20/2C1C078. This is static evidence, with no native calls, activation or retail-asset redistribution. [Carry active retirement](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/carry_active_end_ownership_manifest.json), [prologue resource comparison](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/prologue-normal-horse-comparison-v2.json), [selected-player provider](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/prologue-provider2-ownership-manifest-v1.json).
