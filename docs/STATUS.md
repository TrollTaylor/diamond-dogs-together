## 0.13.5 (in progress)

Unreleased; the version stays 0.13.4 until the integration bump. Every new behaviour is a lab key, default off in lab
and play, until a hidden-worker FRAME proves it.

### F1

- **Vehicles: board block and passengers both ways** (`wp/board`; [native-puppet.md "Vehicle boarding"](native-puppet.md),
  research `re-vehicle.md`). New lab key `partner_ride_board=off|block|passenger` (default off, `_given`, play forces
  it off). `partner_board` retargets the ride action's two nearest-seat calls (site A `0x12458AC` in the pre-query,
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
- Scenarios (not run yet): `coop-vehicle-shared.txt` (block, both directions) and `coop-vehicle-passenger.txt`
  (back seat both directions, then a truck that must block); `debug goto_vehicle truck` added for the latter.
- Unproven until those FRAMEs: a back-door prompt on the light vehicle (else passenger degrades to block), the stock
  passenger camera offline, the 1/2 light and 3/4 truck type numbers, the copy's state after a passenger exit.

### F2

- **Cardboard box on body 1, native (`wp/cbox-native`; docs/native-puppet.md "Cardboard box").** New lab key
  `partner_native_cbox` (default 0 in lab and play, play forces it off, `_given` flag). With it, the viewer's
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
  mode=lab). Scenarios `partner-cbox-census.txt` and `partner-cbox.txt` (not run yet: no hidden-worker run in this
  phase). Tests: `partner_cbox_tests` (policy + the module on a fake world), driver, lab config, debug Lua checks.
- **Unproven, off:** no FRAME of body 1 boxed yet; the open items are listed in "Cardboard box".

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
- Key: still `partner_native_traverse` (default 0 in lab and play, play forces 0); no new key. The copy is built only
  with the key on and defer=on (`partner_icon=built|refused`).
- Contract: 691 code sites / 169 vtable slots (was 677 / 149): the Exec prologues, the priority body, the builder
  gate, the four poll call sites, the step-on force call, padop +0x18/+0x28/+0x50/+0x188 and vt[0]/vt[1] of the eight
  traversal ActionPlugin vtables; complete-body SHA-256 pins of `0xAFD870`, `0xB02480`, `0x12128C0`
  (`icon_bodies`, re-checked at Build) and reference assertions (only the ctor/dtor store the padop vtable; the
  forwarded functions are reached only through their slots).
- Tests: `partner_icon_tests` (new, in `Get-WorkerTests`), `partner_traverse_tests` rewritten for the icon level,
  `partner_driver_tests` `TestNativeTraverseIcon`, `partner_defer_tests` drives the copy through the real drain
  window. Scenario `tools/worker/scenarios/partner-traverse-icon.txt` (spot census in four headings plus captures).
- Unproven (hidden run): the census verdict, `res=ok` followed by `edge=body; remote=X; body=X`, frames of body 1 on
  the ladder / wall / ledge, and that the viewer never changes (`latched=0`, `viewer_changed=0`).

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
guest's own 80 arc previews forwarded). The review (`docs/evidence/rea-throws-2026-10-15/review-1ca3ce6.md`) found the
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

Hidden runs `9d08106b35be49ca877f7c761115db0c` and `dbb5b57eac50475db3e37ad708bb926e` (lab defaults, friendly dart): from 11 m the host's only stock prompt is Pet D-Dog, with D-Dog beside the host. Beside the teammate it shows only the stock carry icon and the mod's `HOLD / WAKE UP`. Carrying shows neither the Fulton offer nor the struggle guide. The host log records the suppressed hints 93 and 94 on queue `0x3C0`. The carried guest's view showed the carrier from behind, the follow camera stopped after put-down, and the wake restored 6600/6600 HP. An earlier attempt, run `1d3e80b4b4e14e12915d77d04ac9dbba`, never reached gameplay: the new vt+268 hook changed bytes the private pose module verifies (`pose_full_code`), so the worker refused. All three runs restored the baseline loaders, and the real saves were unchanged. Checks: all 264 parallel CTest tests and `lab_scripts_tests` passed; `friend_package_tests` was not rerun because of the known OneDrive lock. Not yet covered: a wounded carried teammate's camera, the reverse roles (the host carried), and a live two-PC session. [Evidence](evidence/carried-camera-partner-prompts-actual-2026-10-14.json).

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

Build: a world Lua chunk may now be up to 32000 bytes; CMake emits a chunk over 16000 bytes as two adjacent raw literals so each stays under the MSVC literal limit. Checks: full CTest suite passed (two PowerShell fixtures pass serially but can collide when run in parallel). D: had 0 bytes free and stopped a hidden run; 200 old, unreferenced worker install backups (3.15 GB) were moved to `C:\Users\taylo\mgs-coop-archive`, keeping every folder in the current rollback chains. [Evidence](evidence/vehicle-smooth-rescue-prompt-actual-2026-10-14.json).

**Installed 0.12.6.** Release receipt `20261014-205237-c617fc25f8c54985910f93f4c70ba47f` passed 263 audited tests with the worker off and the same experiment flags as 0.12.5 (the package's flag list now also names the five private observer options, all OFF). Package `mgs-coop-friend-0.12.6-20261014T205431Z-b88a5de1` is installed in both owned game copies: loader SHA-256 `1497f2ba...026b`; both executables, both Steam DLLs and all 13 real save files hash-identical before and after; no game launched. The source `Play-Coop.bat` selects this package. [Release evidence](evidence/release-0.12.6-2026-10-14.json).

## October 14, 2026: native rescue installed; riding and standing sleep under qualification

The playable installed rescue release remains **0.12.5**; source **0.12.6** contains private experiments whose activation stays off in friend builds. Completed research receipt `20261014-033939-50b1e531e0bb48eb9e6aa0ede9f762e3` passed **261 audited tests** and **124 worker-policy checks**. Subsequent improved entry/stack diagnostics are being checked separately. Source and fixture checks do not establish shared riding.

Actual run `4d4286bb7254490da1768c9ad30f97b7` passed 11 requirements: both private vehicle mirrors enabled, the host entered the authored jeep through normal Action input, and the host retained 1912 protected pose windows with fault 0. The private foot-only camera filter exercised the exact body-one mode-4 leaf. The guest stopped after its generic ride path disarmed the input owner before an authenticated native vehicle demand was available. Measured guest native-job drains remained below the 4000 microsecond limit. This is an ordering/refusal issue, not an accepted shared ride or a cost-limit failure. [Actual host entry and guest refusal](evidence/native-vehicle-host-entry-guest-refusal-actual-2026-10-14.json).

Actual run `b8721082fc5549d99d313ef740f3287e` passed six requirements covering a real friendly sleep dart, native rest, continuous three-second Action wake and health-preserving recovery. The candidate four-motion bank loaded, while original stock motions remained selected. The first native-frame observer recorded zero filtered calls; this did not distinguish an unused callback from rejected payloads. Improved diagnostics count every actual entry and capture the existing native setup stack independently. Upright selection and asynchronous resource retirement remain unqualified. [Actual sleep/wake and initial frame observation](evidence/mgo-standing-native-frame-zero-callback-actual-2026-10-14.json), [Earlier candidate bank loading](evidence/mgo-standing-cycle-bank-loaded-actual-2026-10-14.json).

Horse research found the original Player/Soldier carry-target distinction, the native horse-load finder rejection for Player cargo, and a transient handoff bit that must be observed at the actual finder call. The stock NPC comparison now avoids mandatory spectator pose windows. Run `3560409635c24376b2db4ce229c8b791` passed ten setup requirements but the game's original pickup predicate rejected a negative facing dot before and after Carry input. The same current guard was within range and asleep; no pickup, horse transfer or attachment was accepted. The next test corrects actual native facing before evaluating horse handoff. [Original pickup predicate refusal](evidence/native-horse-npc-current-facing-refusal-actual-2026-10-14.json), [Earlier lifetime refusal](evidence/native-horse-npc-settled-lifetime-refusal-actual-2026-10-14.json).

Read-only horse/NPC observations are admitted only by the compiled private worker; normal release play adds none of their diagnostic memory reads. All cited runs closed their owned processes, restored all 63 protected paths and preserved real saves. The previously verified two-copy pickup, carrying, put-down and wake/revival remain accepted and installed. Fresh two-PC Steam acceptance, native horse cargo, shared horse/vehicle passengers and upright sleep selection remain outstanding.

## October 13, 2026: native rescue verified; mounts still being qualified

Source candidate **0.12.6** continues private riding and animation research; the installed/playable package remains **0.12.5**. Research receipt `20261013-235656-b7cb924466684b3898e5a1e957d1dfb7` passed all **238 audited tests**. The distinct native vehicle-purpose core, physical mirror, camera/lifetime protection, authenticated reliable-message origins and original carry-hook ownership are compiled. Normal release activation remains off; these checks do not establish shared-riding acceptance.

The carry/camera composition fix passed actual hidden run `d13fd5d3981c43d1a4257566da3bcaa9`: both copies captured more than 600 protected pose windows with fault 0, and both private vehicle mirrors enabled. A later native cost fallback and long-range debug teleport invalidated telemetry before vehicle entry. The test is retained as partial evidence; reducing idle mirror work and pre-positioning both players are the next checks. No lifetime/generation checks are weakened. [Actual camera composition and later refusal](evidence/native-vehicle-carry-camera-composed-actual-2026-10-14.json).

Read-only run `d6228c5577264f60a47e18b98f350e73` captured the exact original initial bank append on both copies: capacity 32, all slots empty before the call, and stock player bank bound at slot 0 afterward. The unique bank was absent at that boundary and loaded later. This distinguishes a real constructor boundary from the already-bound common-bank callback; no append or selection authority is granted. A scoped original lookup observer and a separate complete four-motion standing-cycle profile are under qualification. [Actual initial-bank trace](evidence/mgo-raw-constructor-actual-2026-10-14.json).

Run `2d94f36436244667b475b3583cd6d01a` passed actual friendly sleep-dart and native shoulder-carry checks on both viewers. The host's current Horse/Player census also passed beside D-Horse. The guest's horse summon correctly refused while asleep, and holding Carry beside the horse left the teammate on the carrier: no horse attachment or put-down was accepted. The scenario's `native-putdown` capture label is not a successful outcome. Native horse loading still needs its own dispatch/attachment/retirement proof. [Actual carried source beside D-Horse](evidence/native-horse-cargo-carried-source-actual-2026-10-14.json).

All three hidden runs closed only their owned processes, restored all 63 current protected paths and preserved real saves. They used the private desktop and in-process input; no visible game windows or system input were used.

The corrected original animation getter was observed in hidden run `d267c618cc414ec68a2b1574964117bb`: both copies/bodies used the actual controller row and final engine vtable `24c1988`. Their gameplay creation loaded the exact unique MGO bank at common slot 17, but already had one engine bank bound. Run `b725bbce840941e39887ed7779ae8ecc` then tested a real friendly sleep dart and normal teammate wake, preserving 6600 HP. Both selected readers still proved original TPP origin 1 / rest index 1273. Exact native lookup returns the first matching engine bank, so later additional registration does not replace the resident animation. Both runs restored all 63 current protected paths and real saves; no append or upright-sleep feature was enabled. [Getter and bank evidence](evidence/mgo-unique-getter-bank-actual-2026-10-13.json), [Actual selected sleep source](evidence/mgo-unique-natural-selection-actual-2026-10-13.json).

The previous claim that player pickup cannot be added was incorrect. The game already contains a player-carries-player route. Scoped reuse of its natural search, action admission, request, carry locomotion and put-down now carries a wounded or sleeping teammate on both copies. No global online flag is changed. Two hidden runs passed normal input pickup, walking, put-down and a three-second Action wake/revive: sleeping guest `8d251aaa31d94b22a3b74bf18c751e66`, wounded guest `b52311eefe8d445bb0327eb0647b3570`. Both restored the current 63 protected files/absences and preserved real saves. These are actual local two-copy tests, not fresh two-PC acceptance. See [sleep evidence](evidence/native-rescue-sleep-wake-actual-2026-10-13.json) and [wounded evidence](evidence/native-wounded-rescue-actual-2026-10-13.json).

Reverse-role run `9903873dcf39457e8ccdd3df3da5bcdd` also passed guest pickup/carry/put-down of the sleeping host, actual native idle/life retirement, full pre-dart 7800 HP, and five native-accepted timely post-wake movement captures from both viewers. [Reverse rescue and movement](evidence/native-reverse-rescue-motion-actual-2026-10-13.json). Pickup now waits for both copies to reach actual native rest, so it cannot interrupt the falling sleep onset. Carry/readiness messages bind both current player generations, mission and link epoch; both players require this version. A stock wake-release episode prevents reseeding while actual masks/health finish retirement. Failed current-body reads retain cleanup duty without reporting readiness that could mask a replacement body.

The rescue HUD keeps the stock pickup icon and removes duplicate pickup instructions. It now uses concise semantic Action/Carry labels: Hold to wake/revive, Keep holding during actual progress, and Hold to put down. Completed holds stop asking for more input, and carried states suppress rescue actions. Its announcements are status-only and coalesced. Actual carried frames show the teammate on the carrier's shoulders. Controller-specific/remapped glyphs remain unfinished.

Friendly darts use the stock player sleep controller: onset, lying rest, native put-down and recovery, with no health loss or repeated sleep reseed. MGO/TPP controller comparison and decoded original animation channels show MGO's player rest is also lying; the guard upright sleep clips belong to a distinct soldier bank. A local-only MGO rest substitution fixture was loaded in hidden run `224489887f9544e19d3419ae6927a93a`, but the typed selected-motion reader proved that TPP's cached resident bank still won. Loading the archive is not accepted MGO animation reuse. A unique small bank and native first-binding route are under qualification. No proprietary animation assets are committed or bundled. Sustained guard-style upright sleep is unfinished.

The sleep timer now accumulates fresh paired gameplay without restarting during carry or temporary stale/pause gaps. Authenticated TEAM_V2 grants mirror wake and native put-down before canonical health/input release. The stock life updater revealed the remaining sleep loop: clearing life while stun remained depleted re-entered sleep. Wake now restores bound maximum stun through an owned compare/exchange, lets the original updater clear life, and waits for actual native retirement. Run `5df105434ea741f58ee2a19e882cb19a` passed automatic wake while carried, unchanged health, both native put-downs and fresh unlocked movement. A later unrelated vehicle positioning step stopped that combined scenario and is retained in its evidence; the wake checks passed. [Automatic wake evidence](evidence/native-auto-wake-stun-actual-2026-10-13.json). The older pending runs remain historical diagnoses.

Rescue-baseline Research receipt `20261013-205134-b84ec45ef2804d669e7ed73dcf9dd531` passed all 216 audited tests. Run `6feff4f358004518a633e0149d84ae0a` used no Action wake input and verified automatic wake from native carry, both native put-downs, sleep retirement, unchanged 6600 HP and unlocked movement in four timely native-accepted captures from both viewers. A fifth capture entered native action28/speed0 while position still advanced; the walking-only classifier rejected it. That later failure is preserved, and is not counted as a fifth passing motion capture. [Automatic carry wake](evidence/native-auto-carried-release-actual-2026-10-13.json).

Run `0d7366727db34110a35febda615dba58` passed actual rifle downing, wounded pickup/walk/drop, teammate Action revive to full 6600 HP, all eight scenario checks and three timely native-accepted post-revive movement captures. It needed owned forced process shutdown; all 63 protected paths and real saves still matched. [Current wounded rescue](evidence/native-wounded-prompt-release-actual-2026-10-13.json). Both new ready prompts were inspected in private captures. These results remain local two-copy evidence.

Horse run `cda340da5ea947a58087d5a0ec3b22c5` found the actual horse, passed its stock geometry, and exposed the player-target UI-only restriction. Clearing that restriction alone is insufficient: the player needs a physical attachment, horse cargo state and native unload/cleanup. The private physical reader initially refused an address-point mismatch; exact constructor review agrees with actual vtable `24c97f8`. No horse cargo writer has been accepted.

Jeep run `a39d407bc4c341eda71c81b16ba34a62` verified ordinary local entry/exit, native phase 3, driver 0, current vehicle vtable `22ed9f0`, getter `b2dd30`, four authored seats and unused passenger slots. This qualifies a read-only vehicle census, not a remote rider or shared passenger. The private passenger prototype remains off. [Jeep evidence](evidence/native-vehicle-jeep-census-actual-2026-10-13.json).

The offset-position horse/vehicle debug prototype is explicitly testing-only and off by default. It is not a native seat implementation. The installed rescue release is 0.12.5. Worker-disabled Release receipt `20261013-213206-d67f931d11b4499992aaad5c47a40007` passed all 216 audited tests; the friend-package fixtures passed 1108 checks with the documented optional modes excluded. Package `mgs-coop-friend-0.12.5-20261013T213512Z-3af534f0` is installed in both owned game copies. Its loader SHA256 is `6ac495d3314dfa48d8c959e6e31612cec5235d70d42e64424cf16b20e49ddf0e`. Only the two loaders, their two ownership receipts and host installer version metadata changed among 63 protected paths; all other 58 paths, including saves, activation and retail files, matched. No game was launched by the installer. The source Play-Coop selects this newest extracted package, including its local two-window option. Private mount and MGO candidates remain off. [Release and installation evidence](evidence/release-0.12.5-2026-10-13.json). Hidden workers always use a private desktop, bounded in-process input and lab saves, and restore the current installation after each run.

Read-only vehicle run `d26be38536db49cabd20bb1525a95083` verified the real lookup proxy: `23063f8` / `bf5450`, with primary `22ed930` and count getter `b2dbe0`. The proxy differs from the underlying direct vehicle query interface, so binder calls stay off until its inner owner/allocation is verified. [Actual query evidence](evidence/native-vehicle-query-actual-2026-10-13.json). A distinct read-only birth census preserves actual 64-bit creator receipts and current paired model identities; it never grants a native writer or manufactures a dual-parts ticket.

Actual ownership run `0e4fede1e0f248dfa2d181a0e23cef1c` verified the Horse service bridge to native primary/interface `2363c48/2363c80`, coherent empty cargo and finite native anchor matrix. It also verified the jeep proxy inner owner, live records/bitmap, final `24410e8/2441120` resource, and typed local camera owner through normal entry/exit. Post-exit controls passed two timely motion captures. The mounted callback census refused its event-key assumption; exact code review found it confused callback payload with the registered event, so native replica riding stays off pending that strict correction and actual test. The prologue providers were all null in Afghanistan; no VR controller is inferred from the global selector. [Actual horse/vehicle owners](evidence/native-horse-vehicle-owner-census-actual-2026-10-13.json), [source and fixture checks](evidence/native-owner-census-static-2026-10-13.json).

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

Final validation at source `7487c6355421b8ebb8bd41808ebd069323b3ca12`: Research and normal worker-disabled Release each passed 156/156 audited tests. A repeated actual hidden cycle `52f2d3663d0b45deb03cabca49a5d6d1` passed all 18 requirements after the nested-job callback fix: HP6600/6600, native local0 sleep5/Q84/life2, W+aim+fire magnitude1000â†’0, teammate E wake6/Q85/life0, then idle0/Q1/life0 and healthy1000â†’1000. All 63 protected installation/save files match afterward; no desktop switch, focus call or system input occurred. The 1,207 launcher/installer fixtures also passed; they do not establish gameplay. [Final checks](evidence/carry-sleep-final-checks-2026-10-11.json), [final actual cycle](evidence/native-sleep-composed-actual-2026-10-11.json).

Playable package update (October11): the checked0.12.4 friend package is now extracted at `dist/mgs-coop-friend-0.12.4-20261011T231346Z-c4e102df`, and its worker-disabled loader is installed in both owned game copies. The source `Play-Coop.bat` selects this newest package. Only the two loaders and two ownership records changed among63 protected files; retail executables, Steam DLLs, archives, activation and real/co-op/local-test saves are unchanged. The ZIP contains903 entries, SHA256 `d8d07269a353ff245b931524e9e4a6b8bba13ff9feb15b1fe30dea3d5e213052`. The upgrade follows the same156-check receipt above. Sleep remains the prone fallback; native sleep experiments and unfinished pickup/passengers are excluded from this playable build. [Package and installation evidence](evidence/release-0.12.4-2026-10-11.json). Hidden offline testing does not establish fresh two-PC Steam acceptance. The following dated paragraphs retain the investigation history; earlier refusals were superseded only where a newer successful receipt is identified.

Latest private animation result (October12): Research receipt `20261012-113434-03423ebde80643a18cf7563bfd9be1ed` passes185/185 audited checks. Hidden actual run `bde23e5f90ed4d579fead99b4bfe9b90` passes15/15 requirements: coherent cold carry route/profile readiness, actual body1 native sleep5/Q84/life2, unchanged6600HP, locked W+aim+fire1000 to0, normal local camera/cache, teammate wake, complete native retirement to active0/phase0/mode0/life0 and held0/fault0, then fresh local guest input1000 to1000. Both owned games closed normally without forced termination; all63 protected files and real saves match, and no system input, focus call or desktop switch occurred. No private pose refusal was recorded. Exact natural CLEAR/SET flag-map callbacks preserve their original native result and guarded topology; the preparation watchdog now compares its completed read time with a fresh clock. This supersedes the earlier core36/flag-map/preparation refusals for this short cycle only. Body1 rest maintenance is authored and source-checked but longer duration remains unaccepted. Guest local0 uses the prone fallback here; grass obscures the sleeping replica, so clear standing-pose acceptance is still absent. Pickup/drop, horse loading, shared passengers and fresh two-PC Steam remain unfinished. These private changes are excluded from installed normal0.12.4. [Latest hidden cycle](evidence/native-body1-core36-sleep-cycle-actual-2026-10-12.json).

Earlier private animation follow-up (October12): all175 audited Research checks passed at receipt `20261012-061031-f642ce76d02b4cbabd2f180ef09619dc`. The selected-body exporter and camera filter preserve local0 positions and third-person views, accepted in earlier camera run `cc38409742b049a2929466e7f7bb8719` (10/10). Actual short body1 sleep cycle `274ce091c4e640d39d079559bcb3ecd0` now passes all12 requirements: HP6600/6600, native onset/rest mode5/Q84/life2, actual W+aim+fire1000 to0, accepted teammate wake, two genuine native idle observations and Closed cleanup to active0/phase0/mode0/life0, held0/fault0 and native/cache/Lua gap0. No private refusal occurred. The owned worker closed using forced cleanup and restored all63 protected game/save files; no focus, system input or desktop switch occurred. Viewed final frames preserve normal cameras; guest local0 remains the existing prone fallback stance, and grass obscures the sleeping replica. Fresh postwake movement, clear guard-style standing sleep, longer/lifecycle-loss tests, actual pickup/shared passengers and two-PC Steam remain unaccepted. Native rest-duration maintenance and carry pickup/drop adapters are next. These private changes remain excluded from normal0.12.4. [Actual accepted short cycle](evidence/native-body1-sleep-cycle-actual-2026-10-12.json), [175 checks](evidence/sleep-recovery-holder-root-checks-2026-10-12.json).

Latest private wake diagnostic (October12): all174 Research checks passed at receipt `20261012-055206-127ba4474bfd43c193a1a1bacdcb85af`. Run `afcd52bc9d1a49408d82664844a96450` repeated the eight rest/control/camera requirements and then caught the exact first rejection: the body1 controller was mode0/request0/life0 with its node inactive, but PSaction19/Q85 and the current recovery graph `40D8FDA5CA06` remained. Current owner/max bindings passed; last strict owned observation was16ms old and accepted target50ms old, still canonically asleep without a wake grant. The current shared poll helper requires complete retirement too early; the later175-check holder and accepted short cycle above supersede this refusal; strict release remained unchanged. Actual full retirement, carrying and normal activation remain unaccepted. This run closed without forced termination and restored all63 protected files/saves. [First actual predicate](evidence/native-body1-sleep-wake-first-predicate-2026-10-12.json), [174 checks](evidence/sleep-wake-diagnostic-root-checks-2026-10-12.json), [native wake ordering](evidence/sleep-wake-ordering-proof-v2.json).

Native sleep cycle actual qualification (worker-only): run `c98eae6788a341f49591ad63b245c1d6` passed all scenario requirements after 153 audited Research checks. A WU dart kept guest HP6600/6600, entered mode5/Q84/life2 with the native rest graph, and held W+aim+fire produced actual movement1000 to0. Host E woke the teammate through mode6/Q85/life0; it then returned to idle mode0/Q1/life0, healthy cause0/lock0, and fresh W input1000 to1000. V10 separates a bound post-seed read-only zero-input observation from the original strict write admission; two fresh independently typed samples are still mandatory before release. Screenshots confirm native dark/red local sleep UI and restored gameplay after wake. Body1 standing sleep/carry/shared riders remain disabled and unqualified; there is no guard-style visible-pose or fresh online claim. Evidence: `docs/evidence/native-sleep-wake-cycle-actual-2026-10-11.json`. The worker closed invisibly and all63 protected .3 installation/save files match.

Configured-value correction: hidden run `ee66b57b580444ee83aaec2c57a0cb85` followed153 passed checks and observed actual local0 native sleep mode5/Qaction84/life2, current sleep graph and preserved6600HP. W+aim+fire produced fresh magnitude1000â†’0. Both request allocation readers passed. The strict exact-window cap receipt expired for read-only retention and the worker refused before wake; all63 protected files still match. Local stock sleep shows blackout/recovery UI; the replica's native sleep remains disabled. This is onset/rest/input evidence, not guard-style teammate animation or completed wake acceptance. [Actual native rest/input](evidence/native-sleep-rest-input-actual-2026-10-11.json).

The hidden-only native-sleep wrapper first compiled and passed151 audited checks, with complete transition/typed-owner checks, named control masks, per-frame zero-input receipts, conditional seeding, partial-write retention and current wake/retirement preflight. It is disabled in normal builds. Actual run `c1142101bffc402d9e54d464930aca96` passed both final carry-provider and carried-recipient reads but refused native sleep before its first scalar write. The follow-up diagnostic `01edf09f3e254d1ba9a89a7f6c7d88bd` measured the guest's actual configured stun3300/max3300, disproving the planner's initializer-only3000 assumption. Both copied runs closed and all63 protected files match the .3 baseline. No standing-sleep animation, attachment or shared passenger success is claimed. [First refusal](evidence/native-sleep-first-refusal-actual-2026-10-11.json), [configured value](evidence/native-sleep-configured-stun-actual-2026-10-11.json).

The read-only carried-recipient row16 and its reciprocal carrier tuple now qualify on both actual bodies. A current Q-owned request allocation reader and pure neutral post-retirement builder are integrated for the next build; they make no gameplay writes or native setter calls. Clearing a request after retirement cannot cancel active carry, and early EndRecipient alone does not stop the carrier. Native carrying stays disabled while paired exit/cancellation, attachment/drop/raycast ownership and mandatory body1 presentation windows are qualified. [Handshake](evidence/carry_owned_handshake_manifest.json), [retirement policy](evidence/carry_request_retirement_manifest.json).

Carry row32 live qualification: run `adaa9f8e9220497b8a5faef54490b574` passed all 22 requirements, including ten valid carry-controller observations on host/guest across healthy, dart-sleep, R pickup input and waking. Fifty controller query receipts are recorded in `docs/evidence/carry-row32-qualified-actual-2026-10-11.json`. All146 audited checks passed first; 63 protected files match the .3 installation afterward. Pickup remains absent: stock target op0 requires native action83/84 and the mod fallback is action1D. Pure initial-carrier/recipient request construction and exact stock put-down Lua proof are now retained for qualification; they issue no gameplay requests. Native sleeping, carrying, shared horse and road passengers remain unfinished.


Current carry follow-up (October 11): the read-only carry scheduler was incorrectly indexed at row33; complete builder/binder evidence and the actual node establish row32. Corrected offsets retain all type/owner checks. The offline direct-part-zero sleep notification crash shield now covers raw NPC/self/sentinel attacker IDs as well as teammate IDs, without granting them a mod sleep state. Actual sleep, loaded animation-state and on-foot vehicle observers passed on both bodies in hidden run `a2ce992362e349f9aceea4c7a3287677`; all 63 protected files match the installed baseline afterward. The requested-byte C0 preservation helper is staged as pure tested infrastructure; no wrapper activation or native carry/sleep/shared-rider functionality is enabled by that helper. Carry activation, guard-style standing sleep and shared riders remain under qualification. See the October 11 JSON evidence and row32 correction manifest.


The separate sleep behavior is implemented and live-tested. Cause 1 wounds keep positive protected health, cap natural input at 0.25 and stay downed until revival. Cause 2 friendly darts retain the exact pre-dart HP, block movement and combat, restore the crawl-rate lease, and use an immobile prone fallback. Host-authorized 30-second paired-gameplay sleep timers stop on stale/failed polls, clock reversal, scene changes and unpairing. Healthy teammates can wake sleepers earlier using the existing three-second E hold. Stock standing sleep and actual carrying remain unfinished.

Hidden run `80842d0969014d0ca6adbd449bfaa0b2` used actual WU shots both directions: Guest 6600/6600 and Host 7800/7800 HP were preserved, fresh native movement1000â†’0 was measured, the host woke the guest with E, and the host later woke by the timed path. Run `64aa22628536402daa1f3c1213506376` used actual rifle fire: Guest 330/6600 HP, magnitude1000â†’250, still wounded after 32 seconds, then healthy1000â†’1000 after host E revival. Fifteen required checks passed; both normal installations and all 63 protected files matched after cleanup. NativeTick64 and Lua world timestamps are different clocks and were not subtracted. [Actual sleep/injury/carry evidence](evidence/sleep-injury-carry-actual-2026-10-11.json).

Natural R near both sleepy and wounded teammates did not pick them up. The host moved while the sleeper stayed at the same position. The stock geometric carry query was eligible, but its gun target is not the shoulder-carry target. A new exact carry-controller reader refused its initial typed chain during live run `c5d2e756f9fc40c0b7804a8bcf073c4d`; no controller fields from that failure are accepted. Grouped first-failure diagnostics are now added without weakening guards. [Negative reader qualification](evidence/carry-controller-read-actual-2026-10-11.json).

Full native proof identifies stock sleep onset/rest/recovery callbacks and resident graph nodes, plus a typed read-only sleep-controller observer. Resident asset presence does not prove the live graph is loaded/playing. The carrier/recipient paths, target-body camera/control/attachment/drop ownership and transitive null-synchronizer safety must be qualified before activation. Current injuries and sleeping controls are never unlocked by a candidate carry flag or gun target. [Sleep chain](evidence/offline-native-sleep-chain.json), [reader stage](evidence/sleep-plugin-reader-stage-2026-10-11.json), [carry controller](evidence/carry_plugin_evidence.json).

The prologue uses a dedicated `TppPlayerHorse2forVr` controller and authored Ocelot route; it is not an established free-roam D-Horse passenger API. Ordinary road vehicles have native seat >= 2 passenger phases and body seat transforms. Their stock kind9 publisher requires the online flag; that flag remains untouched. A rider-array insertion alone does not attach the player. MGO's separately fingerprinted executable supplied study candidates only; no MGO offsets were applied to TPP. [Prologue/MGO study](evidence/riding-research-proof.json), [seat/ownership chain](evidence/vehicle-player-seat-proof.json), [motion producer and body transforms](evidence/vehicle-motion-producer-proof.json).

Fresh two-PC Steam acceptance, standing sleep, actual player carry/horse loading/shared passengers and mounted vehicle-seat observation remain open. All work and runtime tests stay on the qualified private desktop with copied saves and owned input. The last published and normally installed release below is 0.12.3 until the new candidate is packaged and installed.

Hidden read-only run `192b23ae2a3b49159ce13f5aab482eea` confirmed the precise refusals: sleep record callback adjustment incorrectly included unspecified padding; carry primary +38 did not match the expected scheduler row. All 20 controller query receipts and bounded native traces are retained. Both games closed without forced termination, all 63 protected files matched, and actual dart sleep / E wake passed again. Signed 32-bit sleep and vehicle callback padding checks are corrected; actual carry binding remains strict. [Actual controller identity evidence](evidence/controller-identity-read-actual-2026-10-11.json).

The carried-recipient kind-15 path and forced carrier target are identified offline, with full-range lifecycle proof. The unindexed P+C0 interface is a shared presentation-mode request, not a proved camera API. Its +F8 method only writes requested mode +6D; existing body-1 windows do not preserve that byte and can fall back to an unwrapped job on refusal. Native sleep recovery also touches local pad and camera state. Activation remains disabled until ownership, retirement and every relevant scope failure are qualified. [Carry lifecycle proof](evidence/carry-actual-lifecycle-proof.json), [window review](evidence/camera-sandbox-reuse-review.json), [sleep callback proof](evidence/sleep-callback-sync-proof.json).

The carry-provider reader now pins the complete boot registration and exact fixed singleton chain: locator `2C6C0C0`, final services `2C00D40` / vtable `2289588`, optional provider `2BD6160` / vtable `2191A90`. Its earlier base-vtable expectation refused the actual final service. Fifteen complete sites and two slots verify against the protected retail executable; 36 reader fixtures pass. Fresh actual runs `c1142101bffc402d9e54d464930aca96` and `01edf09f3e254d1ba9a89a7f6c7d88bd` now pass this read on both bodies; it makes no provider calls. [Final registration proof](evidence/carry_provider_final_registration_evidence.json).

The worker-only body-0 sleep lease now has 198 strict checks covering configured stun bounds, ordered conditional seeding, partial-write retention/repair, native onset/rest/wake observation, early host wake grants and fresh two-observation retirement. Complete seed/transition gates qualify the configured maximum actually read from the current owner. A bound retained observation can follow post-seed zero input, while all writes and final release retain stricter independent preflight. Actual runs `c98eae6788a341f49591ad63b245c1d6` and `52f2d3663d0b45deb03cabca49a5d6d1` passed mode5â†’6â†’0 and restored input. Native activation remains disabled in normal builds and on remote body1; these runs do not establish visible guard-style teammate sleep. [Initial planner manifest](evidence/sleep_body0_lease_manifest.json), [seed proof](evidence/sleep_body0_seed_proof.json), [final actual cycle](evidence/native-sleep-composed-actual-2026-10-11.json).

Additional complete native research proves the carry model method copies a matrix out rather than changing the model, and vehicle seat removal does not itself retire the player pose. Forced carry drop and shared riders still require runtime adapters and live validation. [Model getter](evidence/carry-model-proof.json), [vehicle activation review](evidence/vehicle-activation-proof.json).

## Previous release: 0.12.3 â€” October 11, 2026

Source and preserved development history are on the private [GitHub repository](https://github.com/TrollTaylor/diamond-dogs-together). The [0.12.3 prerelease](https://github.com/TrollTaylor/diamond-dogs-together/releases/tag/v0.12.3) includes the friend ZIP, with Play-Coop **T** for the manual two-window test and F9 downed/horse controls.

Package `dist/mgs-coop-friend-0.12.3-20261011T144511Z-bb927c13`, ZIP SHA-256 `dc72deae5c9a5e8fcb800121190bf756ce9d01df8acf01c62ed9b9310699c2ac`; Release loader `FA726C892B8E93C2CF728AE4748C858D897DC90508DCE77FA0A2DFAAD2A5F55B`. The package uses clean source commit `a924552724e07125e3453b485a40c2f0ed6f6f85`. Later documentation-only commits do not change those build inputs. Release receipt `build\worker-runs\20261011-144248-eaafafaf9e4e45848cccc7332e4b40dd\receipt.json` passed 138/138 audited checks; worker and experimental buddy capacity are OFF. The full friend launcher/installer fixture previously passed 1207 checks under PowerShell 5.1 and 7; its scripts are unchanged since that run. A separate GitHub checkout built the current loader with pinned dependencies and passed nine selected source-only fixtures without retail inputs.

Installed that loader in both owned game copies. Of 63 protected file hashes/absences, only the planned two loaders and two ownership records changed; retail executables, Steam DLLs, runtime archives, activation settings and real/co-op/lab/local-test saves stayed identical. The ZIP has no retail executable, Steam DLL, runtime archive or save. [Release and installation record](evidence/release-0.12.3-2026-10-11.json).

## October 11 revive, crawl and testing controls

The repair version is 0.12.3; fresh two-PC acceptance remains pending. The newest user-session logs showed the healthy teammate repeatedly refusing a changed native identity, so no held-revive packet reached the host. A healthy poll now coherently retires the stale lease and creates a fresh generation without restoring health through the old pointers. Active downed ownership and stale-grant rejection remain strict. Both actual Lua roles pass the generation-change/held-revive regression.

Private hidden run `f21ee5e3a27b4b61842eda78a47b092d` used actual host gunfire, host sleep darts and guest sleep darts; all three down/revive cycles passed, including reciprocal ACTION revives. It predates the slower crawl and horse helper. Both installed loaders were restored, real saves compared unchanged and no desktop switch, focus or system input occurred. [Exact evidence](evidence/revive-repair-live-2026-10-11.json). Natural healthy pointer replacement was diagnosed in user logs and exercised in fixtures, not reproduced naturally in this lab run.

The first native crawl-record candidate reported rate 500 but did not reduce physical travel. The subsequent exact native input hook runs the original update first, then caps only the freshly computed owned downed body's movement magnitude. In hidden run `39f6a904dc704ce799a1a1fb038788bb`, established local crawl measured Quiet 1.713 / 0.873 / 1.704 m/s and Snake 1.534 / 0.912 / 1.701 m/s before / while downed / after revival. Actual gun and reciprocal sleep-dart revives passed, as did cap and next-update reset readbacks; weapon, aim, reload, stance and dash inputs remain blocked while downed. Snake's remote crawl followed closely; Quiet's replica ran faster before corrective nudges, so precise remote slow-crawl parity remains unfinished. [Physical positions and timing](evidence/downed-input-actual-39f6-2026-10-11.json).

F9 Partner / Co-op provides downed, revive and movement-input readouts plus normal-release Summon / recall D-Horse, Horse status and D-Dog follow status. Run `87a5ce1` visibly established the guest horse while the host retained its D-Dog; the host's Dog status continued advancing after the guest's request. The real F9 movement-input action also returned its readout. No mount was performed. The helper requests the stock local horse four metres beside the player; its selection receipt explicitly leaves arrival unverified. Remote rider attachment and two-player mounting remain unfinished. The composed Dog/Horse fixture exercises releasing an owned held target before yielding the selector; the actual native-clear receipt was outside that run's bounded log budget, so the visible result does not independently establish that call's causality. [Horse and menu evidence](evidence/horse-recall-actual-87a-2026-10-11.json).

The later actual-enemy phase of `39f6a904` exposed a first-down transition failure: scoped lethal damage was intercepted but initial crawl controls stayed pending beyond the old 1500 ms deadline. Initial acquisition now has a bounded six-second grace; losing an already confirmed control lock retains the original 1500 ms deadline. In final hidden run `71cb477b8f394b8c8c4540d0dadfea4b`, an actual enemy hit (582 damage against 234 positive HP, attacker 1206) downed the host, prone/crawl controls locked, and the guest remained healthy. The guest then revived the host using actual E input; the host returned to standing with down/cause/lock all cleared. Diagnostic auto-heal kept the reviver alive during shooting and was switched off before revival; the host had no auto-heal. This run does not establish both-down checkpoint recovery or how much of the new acquisition grace was used: native damage and Lua world timestamps use different clocks. [Actual enemy and E-revive evidence](evidence/enemy-downed-revive-actual-71cb-2026-10-11.json).

The hospital opening's exact rail animation remains unavailable; the current implementation slows normal prone crawl and blocks combat inputs. Quiet's remote crawl drift remains open. All 63 pre-task protected file hashes/absences matched after final worker cleanup, including real saves and both normal installations. All actual runs used the private desktop and owned in-process input; no visible desktop switch, focus change or system input was used. Hidden two-copy testing does not establish fresh two-PC Steam acceptance.


## October 10, 2026: local two-window guest launch

The local test used package `mgs-coop-friend-0.12.1-20261010T005201Z-bdab9994`. That package is older than the 0.12.2 source in this tree. Choosing T printed Host on the left, Guest on the right, then failed with `[guest] The launched process did not match this game copy.`

Windows PowerShell 5.1 often leaves `Process.Path` empty on the first read after the GUI launch. The image path of the process the launcher started is still that copy's `mgsvtpp.exe`, including when the guest file is a hardlink of the host file. On this PC those two executables are one file (two links, one file index). The launcher still requires the process id, the start time, and an image path that is this copy's `mgsvtpp.exe`. A path from the other directory fails even when it names the same file. An empty path fails.

Current launcher package: `C:\Users\taylo\OneDrive\Documents\ChatGPT\mgs-coop\dist\mgs-coop-friend-0.12.2-20261010T195437Z-a0668e6a`. Zip SHA-256 `919E60F64EE665D51E5AC37B4739C63D85DBF32255FBCA9F053ED413CA8812AA`. Open that folder's `Play-Coop.bat` and press T. A checkout launcher that prints `Using package` selects this folder because its preparation time is newest. Release receipt `build\worker-runs\20261010-195230-d3c2cfdc8b0e48d6b0d89d4b9f0ce617\receipt.json` passed 137/137, including `local_coop_windows_tests`. That fixture passed 188 checks under Windows PowerShell 5.1 and PowerShell 7. The package is the dirty 0.12.2 tree at commit `2a0ee694d32e078203a365cedb589f50b5f15718`, loader SHA-256 `391AAF521C2950AD6BEC9FCE4672CC3361CF744C4A563A18E99C377B178E352C`. The live game folders were left unchanged. MGSV was not launched, so the two-window desktop session is still unchecked. Pressing T later updates both owned loaders to this package before the games start; that install step was not run here.

## October 10, 2026: downed, buddy follow, and ride-capacity continuation

The uncommitted work in this worktree already contained the downed/crawl/revive path, friendly-hit forwarding, D-Dog host follow, ally-targeting HUD, and the Play-Coop exact-release loader check. This session did not re-run a hidden game. Prior private-desktop evidence remains `docs/evidence/gameplay-downed-dog-live-2026-10-10.json` (run `7169753d2478483e9ec5b1ab93ad58c4`): friendly gunfire downed the guest, crawl stayed locked, E-hold revive worked both ways, sleep darts downed both directions without a crash, and the guest dog settled about 1.48 m from the host dog. That receipt explicitly did not exercise enemy lethal damage, both-down checkpoint recovery, or a second horse/walker. Mounting stays disabled (`rideMounted=false`).

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

ZIP: C:\Users\taylo\OneDrive\Documents\ChatGPT\mgs-coop\dist\mgs-coop-friend-0.12.1-20261010T005201Z-bdab9994.zip; SHA-256 AAC60045A0BFFEE757A1D469D361CC3C340358371E054A8F82F27E68CBC10B89. Both existing game copies were upgraded to its owned loader without launching them: host rollback bee11aaaf41a4022a07aa89162510f9d, guest 44339d1a81584625afa151507ec752d5. All **45 current real/co-op/lab save files** are unchanged, none added or removed; original EXE/Steam DLL/runtime archive and both activation baselines are unchanged. Evidence: D:\Games\steamapps\common\MGS_TPP\mgs-coop\local-launcher-update\before.json, after.json and entry-checks.json. Game-folder Play-Coop.bat and Play-Offline-Coop.bat shortcuts now point to the same current release flow. Existing Claude files are preserved; nothing was pushed.

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

Package: `C:\Users\taylo\OneDrive\Documents\ChatGPT\mgs-coop\dist\mgs-coop-friend-0.12.1-20261010T001133Z-1f94b83c.zip`, SHA-256 `F48280F02282F6B9AFB6093091CFE8D33F3C4AD716F82D0BD4E7CD070AFF2775`. Use that extracted folder's **Play-Coop.bat**; both PCs must install the exact same final ZIP. The earlier `20261009T234746Z-91a3114f` 0.12.1 bundle is superseded and has a different HELLO build identity.

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
continuation. See [build-specific evidence and remaining work](testing-2026-10-07-hidden-worker-animation.md).

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

The H8 build passed105 non-long suites and both long installer suites. Both games closed normally at18:52:22 UTC; all ten real save/config files match run-start and backup. Active lab settings and markers were disabled, and both installations were restored through the owned installer to the previously tested normal21 DLL `D4AF2A7F...CF5230` and baseline runtime archive. The newer accepted-source collector and gun diagnostics remain saved in source/research artifacts and require a fresh normal-profile release check. No new release or commit was made. [H8 evidence and next steps](testing-2026-10-07-animation-debug.md#october-7-h8-manual-idle-validation-and-user-requested-wrap-up).

## October 7, 2026: animation and debug continuation

Current source allows eight existing resident turn clips alongside the 13 locomotion/stance clips, while retaining the original runtime archive hash and bytes. A fresh normal-profile run with all research flags OFF verified all eight resources finishing naturally and actual aimed mouse pivots selecting crouched turn 18 and standing turn 16. The subsequent T7 research run verified accepted-source collection with a crouched pivot and two separately settled identical pivots: both queued clips played once for950 ms and returned to idle. Visual interpolation remains unchanged. Measured improvement over the previous collector and a fresh normal-profile check of the newer source remain pending.

F9 patrol placement visibly reused an armed living guard in a development build; its local combat killed the player and host Continue reloaded both instances. Fresh H2/H3/H5 environment checks separately verified host noon/rain with guest clock differences of 1â€“2 seconds and matching rainy weather. Guard placement remains host-local, with no new soldier allocation or AI replication claim.

Gun intent and the default-off bounded action-history companion have live transport receipts. Automatic upper gun playback remains disabled. H3 manually selected upper Fire child 116 but showed no gun/firing pose or finite finish. H5 installed the corrected observation hooks and captured one natural local End path through departure to idle. Its remote Fire request with zero blend still showed low arms and no gun. The writer refused before native dispatch; a later resource-mapping review requires qualification beyond a predicted bank. Remote update observation succeeded after a documented Warp advanced the body epoch from 0 to 1; source now accepts the legitimate initial epoch 0, with focused regression checks. Both H4 and H5 closed normally and all ten real save/config files remained unchanged. Later candidates require fresh gameplay checks. [Build-specific evidence](testing-2026-10-07-animation-debug.md).

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

At that stage, all 78 automated suites passed in aggregate after the lab installer test was rerun separately. Its arrival/title/recovery checks passed 11/11 ([check log](../build/codex-arrival-fix-tests-20261006.log)). The local [Play-World-Coop.bat](../Play-World-Coop.bat) launches the configured lab at world level 3.

On that previous DLL, the host checkpoint probe sends hash `0`, version `1`, pose `(1233009,21370,-123172)` mm / yaw `0`; the guest's actual VarSave callback confirms it 17 ms later. The injected guest PLAYER_DEAD opens Mission Failed in both games. Host-only native menu Continue then replays 47 records and returns both players on foot to the same checkpoint ground position `(1233009,20870,-123172)` mm. They pair at epoch `4`; guest alignment `id=2` accepts 151 unknowns, zero divergences and `w1=1`, followed by host readiness 34 ms later. Parts are `3,3` and reciprocal transform readback is 0 mm.

Host-only travel then follows Africa `30020` â†’ ACC `40020` â†’ Afghanistan `30010`, using 47 records on each leg. Both players visibly use the fresh helicopter entry. The guest reaches epoch `6`; alignment `id=3` accepts 271 soldiers / 77 CPs with 191 unknowns, zero divergences and `w1=1` at `368261214`, followed by host readiness 32 ms later. After both helicopter exits, the host ground view shows Snake and an opaque Quiet partner; native guest W for 1.5 s and host W for 6 s have reciprocal rested transform differences of 0 mm.

Setting `world=0` in both games logs UNPAIRED reason `5` and inactive level `0`; partner transform sync remains active. Both games then accept `WM_CLOSE` and exit normally at 12:08:41, with no fuse process. All ten real save/config files match both run-start and latest-backup hashes ([stop receipt](../build/lab/runs/20261006-115055/stop.json), [verification log](../build/codex-4666-live-stop-20261006.log)). Cleanup commit `9d5aac6` archives the active offline markers. Actual Disable leaves marker, `lab.ini`, `coop.ini` and link secret absent in both game folders, with the owned loader and game/archive hashes intact ([Disable log](../build/codex-final-disable-20261006.log), [file-state/hash receipt](../build/codex-final-install-verification-20261006.json)). The focused `lab_scripts_tests` suite passes 1/1. File-state inspection and the source gate establish inactive pass-through; a normal launch after cleanup was not run because it would use the real saves. Disk-write completion and physical damage death are not claimed.

The earlier diagnostic build was `59CD23348386815A84DA0CE9D18DB2B308E52BE21D1B5DDF9C254FC1910A9B13` from `5ab2c5a`, which distinguishes unreadable initial state from malformed alignment streams. Run `build/lab/runs/20261006-113833` found a startup mismatch: the host was on foot while the guest retained its older helicopter initial action despite matching seeded coordinates. Both remote bodies realized with parts `3,3` and zero transform readback error, but that startup did not establish an unobscured two-player on-foot view. Exact `TppPlayer.Init` source selects that action from `ply_initialPlayerState=RIDEON_HELICOPTER` and the guest's unsynchronized `svars.ply_isUsedPlayerInitialAction=false`; the host has `true` after landing. The 4666 world build added that mandatory seed and regression fixture; its separate run above verifies on-foot startup and fresh helicopter entry. Both games close normally afterward, and all ten real save/config files match run-start and latest-backup hashes.

The preceding live-tested integration DLL is `171041C2A598FD758FCC67A0B19B59BE4EC6C35FBA4CD98C2529BF946A9E97D6`. Run `build/lab/runs/20261006-111828` verifies native host title Continue following to Mother Base (`30050`) and host-only `goto=30010` following through ACC (`40050`) to Afghanistan (`30010`). The guest receives all 27 seed keys / 46 records; the host's title context is applied once, then ordinary allocation clears it. Both games reach CFG `30010`, epoch `5`, roster `2772775463`, 271 soldiers and 77 CPs. Guest alignment `id=1` is accepted with 191 unknown soldiers, zero divergences and `w1=1`; unknown states are held until their areas become readable, not treated as synchronized AI or positions.

The earlier diagnostic/title/recovery subset passed 11/11 ([check log](../build/codex-alignment-diagnostic-tests-20261006.log)).

The preceding test build's Afghanistan movement matches the local/remote coordinates in both directions with 0 mm difference at rest; screenshots show the separate Snake and Quiet bodies. Stage 0 â†’ 3 warp rearming places the partner before enabling its bits, with ready parts and zero warp error.

The host checkpoint probe publishes hash `0`, version `2`; the guest's actual VarSave callback confirms the exact host pose 19 ms later. An injected guest PLAYER_DEAD request opens Mission Failed on both games.

Only the host used native menu Continue; both copies reloaded and paired at epoch `6`. The guest became ready about 74 s after the host; all 271 host soldiers were initially unknown, so snapshots remained gated. When the host area activated, the unchanged DLL recovered: guest snapshot `id=36` was accepted about 1.9 s later with 206 unknown soldiers, zero divergences and `w1=1`, followed by host readiness. This verifies recovery with a loading delay.

The preceding test build also follows host-only travel to Africa: both copies reach `30020`, epoch `8`, roster `2220786415`, 265 soldiers and 68 CPs; guest alignment `id=37` is accepted with 157 unknown soldiers, zero divergences and `w1=1`. Native movement on each copy shows both player bodies and matching reciprocal transforms at rest. The Afghanistan range check hides the partner at 200.004 m and re-enters at 149.969 m with placement before realization. Both processes then close normally via `WM_CLOSE`; all ten real save/config files match both run-start and latest-backup hashes. The later diagnostic build exposed the initial-action mismatch described above. Physical damage death and the killed soldier's post-Continue readback remain unverified. The October 5 W0â€“W2 tests remain the recorded evidence for shared enemy outcomes, alerts, clock and weather. [Integrated run and historical results](testing-2026-10-05-world-sync.md).

Initial pairing still requires the copied-save games to reach the same supported scene. A real two-PC session, complete campaign gameplay/progression and four players remain unverified. The [friend launcher](two-pc-play.md) uses the same offline UDP route and copied saves; the earlier native Steam-join investigations are historical.

Source commits `7ec1e39` and `07520e0` add the local free-roam controllers, mission gates and distance safety. `a4a5aab` versions the source and package as **0.11.1**. The field test ran the partner changes in a DLL still labelled **0.11.0**, before W3/W4 integration; it does not establish live results for the final integrated 0.11.1 DLL.

Run `build/lab/runs/20261006-095606` verified the 13-entry archive installed in both copies. Afghanistan guest movement matched the host's partner body to about 20 mm, and both copies hid the partner at 200 m. Africa loaded the normal replacement pack with two IDs and parts states `3,3`; movement command/readback differences were 0â€“18 mm. Screenshots are retained under that run's `screens/`. Both games accepted `WM_CLOSE` and exited normally; all ten real save/config files were unchanged against both the run-start manifest and latest backup. [Field test details](testing-2026-10-05-world-sync.md).

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

The fully offline partner (offline-v1, our own UDP link; [test report](testing-2026-10-04.md)) gets three new pieces. All three are experimental and switchable, and none has run live yet. See [partner-anim-appearance.md](partner-anim-appearance.md).

- **Animation.** Each copy sends its slot-0 animation fields (act, stance, speed) in a v2 transform frame. Each copy can apply them to instance 1 in one of these ways: the Q arrays, `SetStance` 0xFF5B10 on a change, or the stance request component.
- **Movement.** A smooth native body transform (`xform`/`hybrid`, Warp's own T.vt+0x70/+0x68 calls without the resets) is available. It falls back to the proven Lua Warp whenever a call is refused.
- **Appearance.** Each copy sends its look, and the receiver applies it with `SetPartsInfoAtInstanceIndex` (`look=net`; `look=quiet|snake` for local tests). offline-v1 now answers the character receipts, and the lab guest plays Quiet.

The defaults keep the proven behaviour. `Play-OfflineCoop.ps1` turns on the appearance sync and the Quiet guest; it keeps the proven Warp movement and turns the unverified native paths on one at a time from its console ([M] smooth movement, [A] stance sync), or all at once with `-Experimental`. Review fixes: received anim/look values are bounded (stance 1..3 with the valid flag, locomotion act ids 1..6 only, the Snake face table index), a refused stance call is retried instead of sticking, the census keeps the stance prerequisites out of the transform verdict, the stance lines have separate caps plus a summary, the A1/A3 plans use hybrid movement so Warp's resets cannot mask the result, and `Prepare-LabInstance.ps1 -Offline -Character default` reruns the proven E-series without the Quiet guest. CTest passes 63 of 63 suites.

## October 3-4, 2026: 0.11.0 (branch `coop-lab`)

**At this native Steam-session stage, no guest join had been verified.** CTest passed 57 of 57 suites. The current offline partner route is recorded above. See [October 3 test report](testing-2026-10-03.md).

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

**Recorded installation: 0.10.33, Host mode.** DLL SHA-256 `FCB0E16A5AF1EE3E1884C7EB14D2EADFD33AC83280D7C0104EA2190EA564F9B6`. The user authorized testing on September 12. All 21 native suites passed, including session release and initialization/termination ownership regressions. Actual-source transformation, complete Lua parse, prestart execution and 91 lifecycle forwarding/cleanup checks passed. Installer upgrade/restore testing also found and fixed PowerShell's empty backup-path conversion in `File.Replace`. The owned live install succeeded. [Test results](testing-2026-09-12.md), [package workflow](local-package.md).

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
- 0.10.4 guest setup loaded Mother Base and returned to title. Read-only native client prerequisites passed; no native guest request or peer was involved. The game was subsequently quit normally and all seven save/config hashes matched the backup. [Result](evidence/client-loading-result-2026-09-09.json).
- Eighteen native suites passed for 0.10.5 before the user requested no further tests. The September 12 test run above supersedes that automated result; no test count establishes a connected player.

## September 12 remaining work

A real guest connection and independent actor control remain unproven. Guest/enemy damage, enemy targeting, world authority and synchronization, death/respawn and disconnect handling still need completion and eventual gameplay verification. Host targeting now has an integrated code path, but remote position publication and targeting behavior are unverified. Disconnect and owned-close code are now connected, but event delivery and asynchronous cleanup remain unverified; automatic reconnect is not enabled. Death/respawn now has a scoped code path, but actual recovery, placement, simultaneous deaths and post-respawn control remain unverified. Sight, damage-controller, hostage flag, trap, soldier action-task and CP status rules are now implemented but unverified. Online-result suppression and the full close-session branch adaptation are implemented but unverified. The concrete FOB service implementation remains unmapped; its existing game-owned dispatch is retained. These adaptations do not establish complete shared combat. Only one PC is available. A local simulated peer could exercise packet logic or actor loading, but would not verify a real remote connection or independent game client.

## Previous allocation and readiness investigations

The records in this section describe earlier versions and processes. The current installation is recorded above; these older entries are historical observations.

**Latest investigation: normal job ownership and cancellation mapped.** The dispatcher holds the same lock used by manager destruction across the bandwidth callback. Registration takes a scheduler reference; finished and pending cancellation branches clear scheduler ownership before releasing it. A new exact-build analyzer passes 103 additional instruction checks and three table checks on top of the allocation analysis; all 38 code bodies match live in PID 14496 at 23:22:33 UTC. Session and bandwidth-manager fields remain null. This resolves the static callback-lock/reference relationship, not live cancellation latency or transition behavior. [Evidence and next trial prerequisites](job-lifetime.md). Installed 0.9.6 and prepared 0.9.7 are unchanged; native host creation remains disabled.

**Latest development: isolated allocation bridge, not installed or linked into the probe.** The real fixture detour passes 1,605 checks, including exact-site routing, cross-thread scopes, exception recovery, concurrent forwarding and restored bytes; all 15 suites pass. The exact-build allocation analyzer now passes 102 instruction predicates, three table checks and 22 live body comparisons. Job shutdown locks, clears the manager pointer, unlocks and sets a cancellation-request bit; scheduler exclusion and completed cleanup remain unresolved. An earlier fixture result changed with diagnostic instrumentation; the final separate-translation-unit fixture passes five repeats, with that uncertainty recorded. [Implementation, evidence and remaining work](allocation-bridge.md).

**Prepared code: 0.9.7, not installed.** The player constructor records a status-derived bit that allocation consumes to select network setup and interface queries. The new exact-build analyzer checks 46 instructions, one method-table entry and four complete bodies. The preserved four-slot sample has this bit clear. Early host prerequisites and later player readiness are now separate functions; native creation still refuses unconditionally at the lifetime gate. All 14 native suites pass. [Initialization and decision](player-session-initialization.md). Built DLL SHA-256: `B5595BC2AF720E17F1089540109BF53CA44C3FC035BB9386173187AABDFDCCA6`.

**Current installed loader: 0.9.6, four-slot archive with precise host-readiness diagnostics.** The player implementation is null before Mission.Start and at OnInitialize entry. At OnMissionGameStart entry, the unchanged preflight passes with index limit four and a null current block. All three observations ran on the TppMain selection thread. Native host creation remains disabled. Mother Base returned to title within 16 seconds and menu input worked. [Current trial](evidence/host-readiness-result-2026-09-09.json), [timing and next investigation](host-readiness-timing.md).

The preceding 0.9.4 four-slot version completed a load/return cycle, and 0.9.3 completed two two-slot cycles and quit normally. Removing the overlapping original controller resolves the observed transition failure in these tests. It does not prove a safe native networking lifecycle or independent additional characters. [Four-slot result](evidence/four-controller-replacement-result-2026-09-09.json), [two-slot result](evidence/controller-replacement-result-2026-09-09.json).

Installed DLL SHA-256: `E7EDC9A04B544607510F547759D9F5008A2E494344DCCA6438AD44279A66507B`. Archive SHA-256: `74BFCD3656356EE4B08EEBCB9912D4806BA0B9F7F948D0C86E6E412D6E930666`, 3,720,192 bytes, seven entries. All five four-slot runtime entries remain byte-identical to the pinned count-one source; the renamed Mother Base package preserves 44 definitions and its complete FPK partner. All 14 native suites pass. The combined host/trace insertion passes 16 exact-source assertions, 400 concurrent selections and 72 actual-module Lua checks. Package selection code is unchanged from the 0.9.3 checks. [Assets](evidence/four-controller-replacement-assets-2026-09-09.json), [current installation](evidence/loader-0.9.6-host-readiness-install-2026-09-09.json).

PID 14496, started 22:43:04 UTC, is at the title menu with Continue selected. No debugger was used. All seven save/config hashes match the pre-readiness backup, and the executable and Steam DLL hashes remain unchanged. Native host creation remains disabled. The prior PID 103508 quit normally at 22:42:26 UTC.

The previous 0.9.1 four-slot/count-one and 0.9.2 two-slot controls both reproduced the identical state-11 teardown wait: 13,419,280 bytes against 12,641,280, excess 778,000. Both games closed normally, and all seven save/config files matched. Restoring parts count and allocation scaling was insufficient to fix the wait. [Two-slot result](evidence/two-player-control-result-2026-09-09.json). Native host creation remains disabled.

The installed mission trace preserves yields, errors, arguments and nil/multiple return values; its exact-source checks passed 15 assertions and 400 concurrent selections, and its Lua fixture passed 213 checks. The new block dispatch analysis passes 27 instruction checks plus three jump-table targets. These tests and reserved storage do not demonstrate remote actors or playable co-op.

The earlier 0.8.0 selection validation passed all 14 native suites, 14 additional exact-source assertions, 400 concurrent selections and 73 actual-module Lua checks. The 0.9.0 reset checks above supersede that Lua count. A read-only query mapper resolves eight GameObject bindings and checks four paths; indexed-ID and type-existence implementations remain unresolved and were not called. [Local checks](evidence/player-package-local-checks-2026-09-09.log), [query mapping](evidence/gameobject-query-analysis-2026-09-09.json).

**Next engine milestone:** verify the native host/client allocation lifetime and player initialization path before enabling networking, then establish independent control of a second actor and expand to four. The four-slot scene now unloads successfully, but the separate native networking allocation-domain concern remains unresolved. Live Steam transport, authenticated peers, world replication, save isolation and campaign adapters remain unfinished.

The [current-build allocation analysis](host-allocation-lifetime.md) passes 63 instruction predicates and two deletion-table checks across 16 bodies. Host and client creation share the identified 88-byte manager and 144-byte job allocations; the context-sensitive allocator and compatible heap-header release branch are mapped. All 16 bodies also match PID 14496 after the readiness trial; mapped session and bandwidth-manager members remain null. No allocator patch or session call was made. Player construction and allocation now show a status-derived network path, supporting early setup as the candidate. Next finish persistent session allocation routing and asynchronous cleanup; concrete service virtual targets and a complete live host lifecycle remain unverified.

## Historical observations

The records below retain their original process/version context; statements such as installed, current, latest or paused below describe the recorded experiment, not the current running-game state above.

Latest live retry: **18:49:53 UTC, PID 91880**, natural script-loader entry and EAX=0 return captured, debugger detached without timeout. Computer Use verified the startup notice, title prompt and keyboard transition to the main menu at 69%. The game was left at the main menu. All seven save/config hashes matched the previous post-motion check; executable, Steam DLL and installed probe hashes also matched. [Retry continuation](evidence/script-load-retry-2026-09-09/continuation.json). This supersedes the earlier unresolved visual continuation for the retry only; the bridge remains uninstalled and co-op is not playable.

The [script-loading bridge](script-load-bridge.md) passes 2,010 fixture calls, including concurrent use, callback reentry and restoration after removal. All 14 CTest suites pass in Release. A natural game script load and successful parser return were captured separately before the installed trial; this does not demonstrate script replacement or multiplayer.

Latest asset work: extracted the installed two-player actor package and staged seven allocation changes for four players. Both definitions compiled and passed XML/FPKD/FPK round trips. The [allocation experiment](four-player-allocation-assets.md) is uninstalled; memory sizing and live actors remain unverified.

Source direction: the user authorized Dynamite inspection and reuse. The [source port](dynamite-integration.md) records pinned licenses, 25 incompatible upstream patches, and 16 current-build function candidates from a second source whose supported executable checksum matches the installed game. No new hooks or co-op gameplay are enabled.

Latest: the [original player-record observer](live-player-record-writer.md) captured a natural bank copy and two writes through updater `0x12CE870`, immediately after store `0x12CF266`. Expanded registers matched bank 1 record zero and source index zero. A 24-instruction static check maps its caller and source-array dataflow. Fixture capture/refusal/deadline/copy-limit checks passed; the game continued and was left paused. All seven save/config hashes match the preceding post-motion check. Actor ownership, lifetime and playable co-op remain unresolved.

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

Raw evidence: [native test output](evidence/native-tests-2026-09-09.log), [actual game probe log](evidence/loader-probe-2026-09-09.log), [live callback trace](evidence/steam-observer-2026-09-09.log), [offline executable inventory](installed-build.json), [independent binding analysis](network-binding-analysis.md).

Historical installation: diagnostic proxy **0.5.0** was installed at `D:/Games/steamapps/common/MGS_TPP/dinput8.dll`, with its ownership record in `mgs-coop/probe-install.json`. Installed DLL SHA-256: `B2E65778F134693C46E5C78011FCE8D39ADFA403782A9532498390322F7BF3B2`. The previous 0.4.0 build and manifest are backed up under `build/backups/probe-0.4.0-20260909T185815Z`. The executable and shipped Steam DLL hashes were reverified unchanged. [Current installation evidence](evidence/loader-0.5.0-install-2026-09-09.json).

Earlier Steam capture launch: 16:28:34 UTC, PID 76012, in the desktop user's Steam context. After the user explicitly approved the restart, the prior game quit normally. The original CDB observer captured the game's natural Steam Init call and matching return (`AL=1`), removed its breakpoints and detached, with exit code zero and no timeout. Separate Computer Use verification reached the title prompt and Enter opened the main menu at 69% completion; all seven save/config hashes still matched the prelaunch baseline at 16:31:39 UTC. [Continuation evidence](evidence/game-steam-init-continuation-2026-09-09.json). See [Steam capture](evidence/game-steam-init-2026-09-09.json) and [current control observations](live-player-control-observation.md).

The preceding 15:06 launch, PID 42824, captured natural network registration and its matching return (`EAX=1`). Startup/title input, Continue and iDroid responded. Return to Mother Base triggered an automatic cutscene; later controls were left with the user after Computer Use detected input. On-foot movement correlation and controller support remain unverified. See [live registration evidence](live-registration-observation.md).

The original and pre-control seven-file save/config sets remain preserved under `build/backups/save-before-observation-20260909T145101Z` and `save-before-control-20260909T152909Z`. Ordinary game autosaves changed `TPP_GAME_DATA1` at 15:04:45, 15:43:06 and 16:20:47; their semantic changes are unknown. Before the latest restart, the closed game's seven current files were copied to `build/backups/save-before-steam-init-20260909T162821Z`, with matching source-before/source-after/backup hashes. [New baseline](evidence/save-check-before-steam-init-2026-09-09.json). The tools have not overwritten original saves. These copies are not save isolation or tested restoration.

The CDB observer is a separate development launch tool, not an installed game hook. [Fixture trials](registration-debugger-trials.md) document capture, wrong-byte rejection and no-hit recovery using explicit `.detach /h`. Earlier rejected fixture detach attempts produced a Windows single-step error dialog; later fixture launches suppress their own error boxes. The first game observation launched under the isolated tooling account and failed Steam initialization; its debugger detached at the deadline, and that known failed startup was closed. Retrying in the desktop account produced the successful game capture above.

Field evidence includes [title samples](evidence/player-status-title-2026-09-09.jsonl), [helicopter/iDroid samples](evidence/player-status-helicopter-2026-09-09.jsonl), [Mother Base arrival samples](evidence/player-status-mother-base-arrival-2026-09-09.jsonl), and [published vector samples](evidence/player-vector-helicopter-2026-09-09.jsonl). The read-only sampler makes no game calls or writes. Arrival read mission `30050` and standing/normal-action flags during a cutscene; these remain insufficient as playability conditions. The separate Steam call-site composition is now [fixture-validated](steam-init-caller-investigation.md) and independently reviewed in the launcher; the later live trial captured a true Init return and verified title/menu continuation. [Typed codec/receiver format and limits](typed-payload-format.md), [latest shared build](evidence/native-tests-with-input-receiver-2026-09-09.log), and [individual test results](evidence/native-test-details-with-input-receiver-2026-09-09.log) document the portable work.

The [earlier Steam dispatch investigation](steam-early-entry-investigation.md) corrected the classification of pointer `0x20A0940`: it is an object virtual-method slot, not a CRT constructor entry. Its caller and object/Boolean ABI are mapped, but the current proxy has no deterministic outside-DllMain installation callback before that dispatch. The separate debugger now captured one successful natural Steam initialization; no new installed early hook is claimed.

## Earlier engine investigations

The [live Mother Base motion trial](live-player-motion-observation.md) correlated one visible Space dive and C posture recovery with the published vector and named status bits in PID 106392. Horizontal dive displacement was approximately 4.78 game units; recovery changed the sampled height with posture. Short forward taps produced no measurable displacement. Five dive samples disagreed between the two position sources despite stable selector rereads, so an atomic replication snapshot is not established. The reader now skips a changed-selector observation while retaining its finite capture limit; all 13 existing suites pass. The game is paused at Mother Base. Six save/config hashes still match the preserved baseline; ordinary arrival autosaved `TPP_GAME_DATA1` at 17:13:15 UTC. Actor identity, sustained movement, controller behavior and all co-op gameplay remain unverified.

The [live session-owner reader](live-network-owner-observation.md) subsequently followed the statically derived three-pointer owner chain in PID 106392 at 17:01:34 UTC. All five final samples matched constructor table `0x2223508` and observed state `0`/flag `0`, the constructor's initial values. This is a raw baseline, not proof of no transport or connection. The original optional `world_probe --network` reader performs bounded reads only and suppresses values on pointer/type changes; no game restart or writes occurred. The updated build and all 13 existing suites pass. The [extended static report](evidence/packet-manager-lifecycle-with-owner-2026-09-09.json) verifies 44 checks across 25 ranges.

The [native packet-manager investigation](packet-manager-lifecycle.md) now reproduces 39 static checks across 22 bounded ranges. Manager setup lazily creates a transport at `inner+0x18`, its constructor registers two Steam callbacks, and its update skips the mapped Steam availability call when that pointer is null. Two upstream construction routes are traced, but their Boolean activation predicate and concrete virtual dispatch to setup remain unresolved. This was file inspection only: it did not sample the pointer live, call game code, alter the installed loader or restart MGSV. The next observation must establish natural setup/update ownership; absent getter receipts alone do not establish a null transport. [Static evidence](evidence/packet-manager-lifecycle-2026-09-09.json).

The 0.4.0 live check subsequently loaded the existing helicopter through Continue. All seven save/config hashes still matched the preserved baseline at the recorded checkpoint. No natural networking getter receipt appeared through this scene check; the installed getter observer has not yet demonstrated live forwarding. Next investigate the independently mapped native packet-manager callers and their lifecycle, rather than inferring availability from an installed hook. [Live result](evidence/steam-getter-live-2026-09-09.json).

Latest launch: **16:41:22 UTC, PID 106392**, with original loader 0.4.0 and no debugger. All three import replacements (Init, callbacks and networking getter) installed with page protection restored. The game accepted the startup notice and title Enter and reached the main menu at 69%. The seven save/config hashes matched the preserved 16:28 baseline at 16:41:25 UTC. [Original getter design and exact callers](steam-networking-getter-observation.md), [13-suite build](evidence/native-tests-with-steam-getter-2026-09-09.log), [current loader trace](evidence/loader-probe-0.4.0-2026-09-09.log).

The [original Steam packet adapter](steam-packet-adapter.md) is now implemented with explicit borrowed-access gates and fake-export coverage, including three guest inputs reaching the real portable coordinator. [Latest build](evidence/native-tests-with-steam-packets-2026-09-09.log) and [detailed results](evidence/native-test-details-with-steam-packets-2026-09-09.log) supersede the 11-suite build above. The running MGSV process remained PID 42824 during this work; no UI input, live SDK call or loader installation occurred in this packet-adapter step.

Independently map a read-only mission/actor observation path in the exact executable. Use the offline inventory's data anchors to guide investigation; those string addresses are not callable function pointers. Record a verified build-specific function map and instruction checks before installing a hook. The first bridge should observe title screen, mission start, local actor state and transition back out, without modifying campaign state.

The [independent decoded analysis](network-binding-analysis.md) establishes that the network registrar builds a temporary 23-entry table on its stack. Its two inspected queries push Lua values and return a result count, and their shared accessor can lazily initialize state. Do not actively call these wrappers as diagnostic queries. No-hit recovery, the live VM-dispatch caller, and the paired natural registrar return are now observed. Next correlate [published player-status records](player-update-investigation.md) and [named player fields](player-observation-candidates.md) with a real loaded actor and transitions. A [natural Steam initialization call site](steam-init-caller-investigation.md) is now independently mapped for an earlier observation. The Steam callback pump remains unsuitable as an assumed single Lua-owner thread.

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


Final carry/prologue ownership evidence: 69 complete carry sites, 8 slots and 2 constants establish recipient-first active retirement. Carrier-first cancellation can enable the physics-drop branch; the native End adapter still lacks mandatory pose/lifetime admission and remaining exit-interface qualification. CS carry callback owners are raw pointers, so readable callback entries/refcounts alone do not exclude model teardown. The prologue provider is descriptor0B/hash58FEFCCA07E1, with 17 complete sites and 8 slots: its shared manager registration is local-body only, natural admission requires statusword1E and selected actualID, and the global selector is manager+158. Corrected RIP targets are2C1BF20/2C1C078. This is static evidence, with no native calls, activation or retail-asset redistribution. [Carry active retirement](evidence/carry_active_end_ownership_manifest.json), [prologue resource comparison](evidence/prologue-normal-horse-comparison-v2.json), [selected-player provider](evidence/prologue-provider2-ownership-manifest-v1.json).
