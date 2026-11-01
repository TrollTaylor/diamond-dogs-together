# WS3 vehicles: the driver seated in unnamed vehicles (B1) and the ground tilt (A1)

Branch `wp/feat-vehicle` (from `wp/integ6`, 0.13.0). Spec: the WS3 section of the co-op gaps spec
(session scratchpad `gaps/coop-gaps-spec.md`), research "vehicle" and the CRITIC corrections #15-#17
(`gaps/research.md`, `gaps/spec.md`). The census is lab-only and off by default (play forces it
off); RIDE_TILT is on by default in lab and play since hidden run `0a2314f2` (the viewer's jeep
pitched with the seated driver) and the seat by id since hidden run `abb0b7cf` (the guest saw the
remote driver seated in the moving Mother Base sortie jeep). Hidden-run findings and the re-run
plan are at the end.

## Root causes (research, unchanged)

- (b) Missing driver: the user's vehicle was 25600, the deployed/sortie vehicle (TppVehicle2 slot 0).
  It has no authored locator, so `partner_sync.lua` refused the seat with `no_name`, native latched
  `lua_refused` and hid the partner for the whole ride.
- (a) No tilt: only x, y, z and yaw cross the wire (RIDE_FAST) and SetPosition carries only
  vec4(x, y, z, rotY); `0xBA9710`'s per-frame (small) move keeps the copy's own record angles.

## Settings (lab.ini)

| Key | Default | Play | Effect |
|---|---|---|---|
| `partner_ride_census=1` | 0 | forced 0 | Step 0: one read-only `partner_ride_census` line per ridden vehicle per second (own cap 400). |
| `partner_ride_seat_id=1` | **1** | admitted | B1: the seat-by-id offer, the ride addendum's category, the id write; Mother Base seat mode. `=0` turns it off. |
| `partner_ride_tilt=1` | **1** | admitted | A1: the ride addendum advertises RIDE_TILT; the driver sends, the viewer writes. `=0` turns it off. |

All three are in the three `-LabSetting` allow-lists (`Invoke-WorkerScenario.ps1` ValidatePattern and
labsetting line, `Start-WorkerSession.ps1`), `tools/lab/LabCommon.psm1` and `ParseVehicleKey`
(`native/lab_config.cpp`, a side helper: the main chain is at MSVC's nesting limit).
`ResolveWeaponDefaults` turns the census off in play (no `play_refused` bit, as before); no profile
turns it on. The seat by id and RIDE_TILT keep their value in play (an explicit `=0` wins in either
mode). `lab_ini_check` prints the three resolved values (`friend_package_tests`: the play package
resolves census|seat_id|tilt `0|1|1`).

**A1 default: ON (lab and play).** Hidden run `0a2314f2` showed the viewer's copy of the jeep
pitched nose-up with the driver seated; census runs `e54e0294`/`dd60eace` showed `same=1` (the
realized body is the unit record). It only writes the viewer's own copy record `+0x44/+0x48`,
behind the unchanged identity/sanity gates (`TiltRefusal`: key, contract blocks and thresholds,
runtime thread, peer, type 50, not the viewer's own vehicle, realized, `identity` body == record,
two equal reads, finite, |angle| <= 1570 mrad). Older peers: the ride addendum is dropped as an
invalid ride packet (one `partner_ride=refused; reason=packet_or_session` line), so the tilt
feature is never seen and no RIDE_TILT is sent to them; an unknown world op would be skipped anyway.

**B1 default: ON (lab and play).** The memory rule for the seat ("no default flip before a
hidden-run frame shows the driver seated") is met: hidden run `abb0b7cf` (Mother Base, real seed)
logged the full by-id episode on the guest (`byid; vehicle=25603; ...; offer=1`, `target; ...;
wrote=1`, `confirmed`, 13 seated status lines with `byid=1` and `err_mm` 0..218, no `seat_far`) and
its frame `guest-sortie-seat-drive-a-04` shows the remote driver seated in the moving unnamed
sortie jeep. Both open runtime facts held (the realized flag `rec+0x6E`, the DM confirmation of an
entry that starts untargeted). It still only acts in the previously refused `no_name` case (a named
vehicle keeps its locator; the record write runs only when every chained entry is untargeted). The
seat flow itself already runs in play whenever native drive is on (`partner_native_ride=seat`,
`kPartnerNativeRideDriven`). Capability: the offer needs the peer's live ride addendum with the
seat-id feature naming this ride's category (`RemoteCategory`); an older peer sends no addendum
(or a tilt-only one), so the offer refuses `no_ext`/`remote_category` and the ride keeps today's
behaviour (named locators seat, an unnamed vehicle hides), and an older receiver drops our
addendum as an invalid ride packet.

## Step 0: the read-only census (`native/partner_vehicle.{hpp,cpp}`)

`partner_vehicle::ReadVehicle(reader, image, id, out)` (pure, templated on the reader; the game
uses ReadProcessMemory, the tests a synthetic map) follows the exact chain the engine uses:

```
service = *(*(*(*(0x2C3A8A0)+8)+0x98)+0x2A8)   vt 0x22AC8E0, vt+0x18 == 0xA272A0 (the DM anchor's own lookup)
object  = *(service+0xB0)                      vt 0x22ED9F0: +0 == 0xB2DD30 (count u16 +0x1C0), +0x78 == 0xB36780
rec     = *(object+0x10) + (id & 0x1FF)*0x80   live bit *(object+0x28) (0xA272A0)
          +0x20 pos, +0x2C yaw, +0x44/+0x48 the other Euler angles (0x55DDA0), +0x6C type (category << 4 | variant),
          +0x6E realized (0xFF none), +0x6F slot, +0x78 index
V   = *(object+0x1A8)   vt in {0x2441010, 0x2441120}, vt+0x70 == 0x16FBEC0 (record SetPosition)
RB  = *(V+0x98)         vt in {0x24416B0, 0x2441780}, vt+0x78 == 0x16FDDC0
cat = (*(RB+0x70))[type >> 4]   vt in six category vtables, vt+0x1F0 == 0xBA9710
body = (*(cat+0x1C8))[slot]     0xBA9710 builds the physics pose from 0x55DDA0(body)
```

The census line (driver tick, `partner_ride_census=1`, `who=local` = the local player's own
vehicle, `who=remote` = this copy of the remote driver's vehicle):

```
partner_ride_census; who=remote; id=25600; ref=ok; type=0x14; cat=1; var=4; realized=3; slot=2; idx78=0; rec=0x...;
  pos_mm=..; yaw_mrad=..; a44_mrad=..; a48_mrad=..; body_ref=ok; v_vt=0x2441120; rb_vt=0x24416b0; cat_vt=0x22f8630;
  cat_fn=0xba9710; body=0x...; body_pos_mm=..; body_yaw_mrad=..; body_a44_mrad=..; body_a48_mrad=..; same=1
```

`same=1` means the realized body IS the unit record (critic #16). A1's write refuses
(`reason=identity`) unless that holds at write time.

## B1: seat by id

- **Ride wire v3 addendum** (`partner_ride_wire`, channel 2, magic 0x52, 16 bytes): features
  (bit 0 seat id, bit 1 tilt), the vehicle category (`unit+0x6C >> 4`), the v1/v2 revision it
  describes, vehicle id, seat, raw type, sender ms. Sent NEXT TO the v1/v2 packet (on change at
  once, else a 2 s heartbeat) only while a vehicle key is on. An older receiver drops it as an
  invalid ride packet (size/version) and keeps its stored v1/v2 state (it logs
  `partner_ride=refused; reason=packet_or_session` once). `RemoteCategory` returns a category only
  for a live addendum describing exactly the stored ride (revision, vehicle, seat).
- **Ride wire v4 seat addendum** (M6 review R3, docs/vehicles-companions-design.md 8.21; 12 bytes, the
  same magic, version 4): flags (bit 0 Front, bit 1 Cab: this copy draws that seat-1 passenger; bit 2
  OneTaken: `rider[1]` of this copy's driven vehicle is not empty), the v1/v2 revision it describes,
  the driven vehicle id, sender ms. Sent next to the v1/v2 packet only with `partner_ride_front` or
  `partner_ride_cab` in passenger mode; the peer's board offers seat 1 only for a live v4 describing
  that ride (`SeatFlagsFor`). The v3 decoder refuses unknown feature bits, so v4 is a separate packet:
  0.14.4 and v3 builds drop it like any unknown ride version (the one refused line above).
- **Offer** (`SeatByIdGate`, at the seat start and again at the write): both hooks, the ride still
  this (vehicle, seat), this copy has a vehicle with that id (`absent`: the read refused `live`, the
  index's pool bit clear, or `index`, past the pool; read first, so an absent id is `absent` with or
  without the addendum), a live addendum (`no_ext`), its category for this ride
  (`remote_category`), not the viewer's own vehicle (`local_vehicle`), this copy's same id readable
  (`local_read`: any other refusal of the read chain), realized (`unrealized`) and of the same
  category (`category`). The offer sets RIDE_SEAT entry bit 4 (`partner::seat_entry::by_id`); a
  refusal keeps today's behaviour (Lua refuses `no_name`, the ride hides) and is logged
  `partner_ride_seat=byid; ...; offer=0; reason=..`, except the visible fallback
  (docs/anim-round1-seat.md "Fallback"): Mother Base shows body 1 without a seat for `absent`,
  `local_read` and `unrealized`, free roam for `absent` only (the Africa fix, the two-PC test of
  2026-10-23 bug 2: the other player's relief vehicle 25600 existed only on his copy).
- **Mission scope**: seat mode runs in 30010/30020 (authored locators) and, with
  `partner_ride_seat_id=1` only (the default), in Mother Base 30050 (`SeatCapableMission`): MB has no
  `veh_lv_`/`veh_trc_` name, so there it seats only an offered id; a refused offer in 30050 is refused
  natively at the episode start (`partner_ride_seat=refused; ...; reason=<the byid reason>`, latched)
  before any release, arm or play (Lua could only answer `no_name`). The manual `seat_dm` lever stays
  30010/30020.
- **No silent hide**: a live vehicle ride under `partner_native_ride=seat` that seat mode cannot draw
  logs `partner_ride_seat=unavailable; vehicle=..; seat=..; reason=seat_unknown|mission|hooks|
  native_pose|blocked; mission=..; byid=0|1; block=<latched reason>|-` once per (vehicle, seat,
  reason) (`SeatUnavailable`, the seat_capable order, then the latch).
- **Lua** (`partner_sync.lua` rideSeat): with bit 4 and no authored name, the class comes from the
  retail `GetVehicleType` (EASTERN/WESTERN_LIGHT_VEHICLE -> lv, EASTERN/WESTERN_TRUCK -> trc, else
  `no_class`: lv/trc only); the same chain plays with locator "" and still `CNP_ppos_c` /
  `MTP_GLOBAL_C` on every entry. A named vehicle keeps its locator under the bit.
- **Order** (critic #17): the seat dwords are armed before the play (the untargeted entry's camera
  stores at 0x1198DAB/0x1198DB5 are skipped only while `cam_plain` 0x1198D81 is armed); once the DM
  is live on body 1 (`q18[1] == 0x8B`, `rec+0x18` a seated state of the seat) the driver tick (the
  player-update thread: `hook_thread == lua_thread` in every hidden run; `PartnerSeatTarget`
  refuses `thread` otherwise) calls `partner_player::WriteSeatTarget`: the ride seat contract
  blocks re-verified, the record re-resolved, `partner_ride_seat::PlanSeatTarget` (every chained
  entry carries CNP_ppos_c and a named seated state of the seat; all untargeted -> write, all
  targeted at the id -> nothing (`wrote=0`), anything else `foreign_target`), then
  `ApplySeatTarget`: every target id `rec+0xC0+2i` (interlocked 16-bit exchange) FIRST, then flag 2
  `rec+0xB8+i` (interlocked byte OR), then a read-back. The seat is confirmed only after that.
- The existing 3 m / 1 s sanity check (`seat_far`) is unchanged and decides a mis-seat.

Log lines: `partner_ride=ext_tx|ext_rx; rev=..; vehicle=..; seat=..; cat=..; type=..; features=..`,
`partner_ride_seat=byid; ...`, `partner_ride_seat=target; vehicle=..; seat=..; wrote=0|1; entries=..;
cur=..; ids=..; flags=..; reason=..; ms=..`, `partner_lua=event=ride_seat;id=..;seat=..;name=-;state=..;entry=..;byid=1;class=lv|trc`, the status line's new `; byid=1` suffix,
`partner_ride_seat=unavailable; vehicle=..; seat=..; reason=..; mission=..; byid=..; block=..` (every
seat-mode hide of a live vehicle ride, once per reason).

## A1: tilt

- New world op **RIDE_TILT 0x2D** (datagram, lab and play, 14 bytes: sender tick u32, id u16, two i32
  mrad in [-1570, 1570]); RIDE_FAST keeps its exact 7 values (critic #15); its own latest-value
  slot, so a RIDE_FAST and a RIDE_TILT of the same tick both reach Lua; an older peer skips the
  unknown code.
- **Capability**: the ride addendum's tilt feature (`pd::RideTiltPeer`). The caps wire v1 cannot
  carry a new bit: its decoder rejects unknown feature bits and non-zero reserved bytes, so an
  older peer would drop the whole caps message (deviation from the spec's "caps wire").
- **Sender** (`world_ride_pose.lua` R.fastSend): after RIDE_FAST, the native receipt
  `MGS_COOP_RIDE_TILT_V1 -- read <id>` returns the driver's own record +0x44/+0x48 in mrad only if
  the key is on, the id is the local vehicle (the tick's `veh=`) and the peer advertised tilt; then
  RIDE_TILT under the same tick. In play the ride addendum carries the tilt and the seat-id
  features as in lab (`features=0x03` by default; `partner_ride_seat_id=0` sends tilt only).
- **Receiver** (`R.fastApply`): the tilt samples ride on the FAST track (same sender clock),
  interpolated with `R.angleLerp` at the render time, held at most 300 ms; right before each
  `R.place` (SetPosition) the receipt `-- write <id> <a44> <a48>` runs
  `partner_vehicle::TiltRefusal` on two equal reads (key, contract blocks + thresholds re-verified,
  runtime thread, peer, type 50, not the viewer's own vehicle, realized, body chain ok,
  `identity` body == record, finite, |angle| <= 1570 mrad) and writes the two floats.
  Since the Africa fix (`R.present`) each track first asks the vehicle's `IsAlive` (at most every
  500 ms): while this copy has no vehicle with that id (the answer is not true and `GetPosition`
  does not answer either: the pool bit is clear) the frame writes no tilt and sends no SetPosition,
  and counts as `absent`. A wreck (`IsAlive` false, still in the pool, `GetPosition` answers) is
  placed as before (hidden run `7561ab63`: skipping a wreck on one copy left the seated driver
  behind, refused `far`, then hidden).
- Log lines: `partner_ride_tilt=read|write|refused; ...`, the world log's
  `event=ride_fast; ...; tilt_sent=..; tilt_written=..; absent=..` (`applied` counts SetPosition
  calls that did not answer false; `state=absent` while the newest track is absent).
- Known limit: a large move (>= 1 m or >= 0.1 rad, e.g. after a relay burst) still takes 0xBA9710's
  yaw-only branch for one frame.

## Driver pose (r1101 vanim, `partner_ride_steer`, off unless named)

User report (2026-11-01): "the driver seems to always be in the turning left animation? and the tires never move on
the other player screen". Branch `wp/vehicle-anim`.

**Root cause [I, static].** The viewer draws the remote driver with a targeted direct motion
(`stateVehicleFrontLeftLoop`, docs/anim-round1-seat.md). The real driver runs `RideVehicleAction`; its driver phase
callback `0x124E140` (the vehicle controller's phase 3) is a 13-way sub-phase switch (table `0x124FD4C`). Sub-phase 2
(`0x124F2D0`, every frame) smooths three blends in the ride work record (`*(plugin+0x78) + (id - base) * 0x240`):

- `+0x68` steer: the pad stick X (`*(*(P+0x138)+0xB0)` vt+0x68(0)) maps to a target `0.5 + 0.5|x|` (right),
  `0.5 (1 - |x|)` (left) or the neutral 0.5, approached at a fixed rate;
- `+0x60` accel and `+0x64` aux (from the throttle, brake and the vehicle's speed `+0x40`), both 0..1;

then (`0x124FC37`, pinned) writes them into the rider's rate record (`*(PS+0x260)` vt+0 (id), the record of
docs/native-puppet.md "Rates"): `+0x30` = accel, `+0x34` = steer (`1 - x` when work `+0x234` bit 0 is set; clamped
for seat kinds 3 and 4), `+0x08` = aux. Sub-phase 3 (enter, `0x124E9A5`, pinned) sets `+0x34` = 0.5. Body 1 never runs
`RideVehicleAction`, and the record constructor (`0xFDD69C`, already pinned) zeroes `+0x08..+0x38`: its steer blend
stays 0.0, the full-left end, for the whole ride. That the seated loop state reads `+0x34` for its turn blend is
inferred from this code and the clip names (`snapplv_s_dve_lp`, `_dve_tun_l`, `_dve_trn_r`); a frame is the proof.

**Fix.** With `partner_ride_steer=1` on both copies:

- the driver's copy (read only) reads its own body 0 blends while its player drives (seat 0 of the vehicle its ride
  packet names; `partner_player::ReadRideRates`) and sends them on the ride wire's **v5 steer addendum** (16 bytes,
  channel 2, the ride magic; `[2]` seat, `[4..5]` the ride revision, `[6..7]` vehicle, `[8]` steer, `[9]` accel,
  `[10]` aux in 1/250 units, `[12..15]` sender ms): a change of 2 units after 50 ms, else a 500 ms heartbeat. Line
  `partner_ride_steer=tx; vehicle=..; seat=0; rev=..; steer=..; accel=..; aux=..; raw=..`;
- the viewer's copy stores it (`partner_ride_steer=rx`, lease 1.5 s, the exact ride revision, vehicle and seat) and,
  while its seat episode has the peer seated and confirmed as the driver, writes the three blends into body 1's rate
  record every tick (`partner_player::WriteRideRates`: index 1, the set guards, the `ReadRates` chain and vtables,
  0..1, read back). Without a live packet it writes the neutral blends (steer 0.5, accel 0, aux 0): an older peer, a
  stale packet or a dropped heartbeat shows a straight driver, never the full left. The record's originals are saved
  before the first write (`partner_ride_steer=save`) and put back when the episode leaves the seated driver
  (`=restore; ...; ok=1`); a mission change drops them unwritten. Passenger seats are never written (their loops have
  no steer). Lines `=write; source=neutral|remote; steer=..` (on a source change or a 10-unit move, at most every
  250 ms) and `=refused; side=driver|viewer; reason=..` (once per reason).
- An older receiver drops the v5 packet as an invalid ride packet (version 5; one
  `partner_ride=refused; reason=packet_or_session` line) and keeps its state; an older sender sends none (neutral).

The truck uses the same driver phase (`stateTruckFrontLeftLoop`; seat kinds 3/4 are the clamped case), so the same
write applies [I]. APC and tank drivers are not drawn seated (hidden), so nothing changes for them.

**Not fixed: the wheels [U].** The viewer's copy of the partner's vehicle is placed every frame by `TppVehicle2`
`SetPosition` (`world_ride_pose.lua` RIDE_FAST: V vt+0x70 `0x16FBEC0` -> RB vt+0x78 `0x16FDDC0` -> category vt+0x1F0
`0xBA9710`, a teleport of the physics pose), so its physics wheels never roll or steer. The wheel visuals follow the
physics wheel constraints (`WheelAssociationAnimPlugin`, `PhxWheelConstraintParam`); no command or field that turns
or steers a wheel was found statically. The vehicle service has a per-index forced route speed (`0x84692F8D` `speed`
-> V vt+0x78 `0x16F9070`: move slot `+0x20` value, `+0x24` bit 0; cleared by V vt+0x80 `0x16F8240`), read by the AI
speed governor `0x16F6CE5`; whether it rolls the wheels of a copy that SetPosition also places is the next probe.

Second static pass (2026-11-01, capstone over the read-only RE copy) [I unless marked]:

- The per-frame small move is a set-transform, not a velocity. `0xBA9710` calls the category's physics set
  (`cat+0x288`, built by `0xBC9890`, vtable `0x2300150`) vt+0xB8 `0xBD4C00` with the
  slot, the new position and rotation: it stores them in the chassis body (`*(*(slot obj+0x98)+0x80)`) through
  `0x1CFFDA0` (`+0x10` pos, `+0x20` rot, `+0x244 |= 8`, a pending transform) and clears bits `0x180` of the
  slot's control block (`*(cat+0x288)+0x38` + slot * `0x210`, `+0x200`) [O, code]. A large move adds vt+0xA8
  `0xBD3B80` and vt+0xA0 `0xBD3A20`, which zero the block's `+0x190`/`+0x194`, clear `+0x20A` and reset per-wheel
  `+0xC0` floats to 1.0 [O, code]. The copy's chassis therefore jumps each frame with no velocity, and nothing
  makes its wheels roll.
- The wheel bones come from `WheelAssociationAnimPlugin` (constructor `0x1D66650`, vtable `0x2531418`; vt+0x18
  `0x1D666E0` forwards three update steps to the implementation object at `+0x68`) [O, code], fed by the physics
  wheel constraints (`PhxWheelConstraintParam`: `wheelFront`, `wheelUp`, `wheelRadius`, `wheelInertia`, ... at
  `0x1D5EBE8..0x1D5F08D`; `steeringMargin` at `0x1C91464`).
- The driver phase does not hand the throttle or the steer to the vehicle: its vt+0x170 calls (`0x124F830`,
  `0x124FBA3`, `0x124FC31`) are motion requests by name; the vehicle reads its driver's input on another path (not
  found). So sending the steer blend (above) cannot turn the copy's wheels by itself.
- Next step (needs a runtime read, not more static work): a lab-only read of the copy's slot control block and its
  wheel bodies on both copies while one player drives, to find the fields the driver's copy changes (wheel spin,
  steer angle); then a viewer-side write of those fields from RIDE_FAST's speed along the heading and yaw rate (or
  the v5 steer blend), behind its own key.

**Tests.** `partner_ride_wire_tests` (the v5 codec, store, lease, the send rule, `PlanSteer`), `partner_driver_tests`
`TestRideSteerDriver` (sender: key, seat 0 only, read only, refusals; viewer: neutral before a packet, the peer's
blends, the lease back to neutral, the refused write, the restore once; a passenger and the key off never write),
`lab_config_tests` (off unless named in lab and play; invalid values), `worker_session_tests` (the allow-lists).

**Proof scenario.** `tools/worker/scenarios/r1101-vanim-play.txt` (play, `-WanSim bad`, `-SaveSeed africa-jeep`):
the viewer stands 6 m out from the driver's door, 3 m toward the nose; the driver steers a, then d with the jeep
standing (both screens captured), then rolls straight, left and right; then the roles swap. Run `6cea2ec9` (an
earlier framing) met every log expectation but its frames show the jeep only far away or behind the viewer's own
body, so they prove nothing about the pose.

## Contract (`tools/generate_partner_contract.py`)

18 new code sites (607 total, was 589) and 13 new vtable slots (135, was 122):
the binding 0x9DAAE1 (B1), the lookup 0xA272B5/0xA272F7, 0xB2DD30, the SetPosition chain
0xB2B7AD/0xB2BA5D/0xB2BA9F/0xB2BB2D/0xB36780/0x16FBEDC/0x16FDDC0, 0xBA9710 (0xBA9747, 0xBA97CA,
0xBA9812), 0x55DBC8 and the three 0x55DDA0 reads; slots: service vt+0x18, object vt+0/+0x78,
V vt+0x70 (2), RB vt+0x78 (2), category vt+0x1F0 (6). `vehicle_assertions()` checks the call
targets (0xBF4110, 0xB7AF90, 0x55DB80, 0x55DDA0), the thresholds (rip targets, 1.0f/0.1f in
read-only .edata, `vehicle_data[]`), that the qword hits of each chain function are exactly the
pinned vtables, that 0x55DDA0 keeps rcx through its three reads, and the DM record entry offsets.
Header constants: `vehicle_*`, `vehicle_blocks[]`, `vehicle_data[]`, `dm_entry_*`,
`dm_cnp_ppos_c`, `seat_target_blocks[]`.

## Tests

`partner_vehicle_tests` (new, in Get-WorkerTests): the reader on a synthetic map (every link and
refusal, same/distinct body, unrealized), the census line, the tilt gates and write (incl. a
read-only page), `VerifyImage` on a fixture image, the ride addendum codec/store and an older
store's view, `PlanSeatTarget` and the id-then-flag order. `partner_driver_tests`
(`TestRideSeatById`, `TestRideExtAndCensus`), `world_wire_tests` (RideTilt, 51 ops),
`partner_sync_lua_check` (by-id rideSeat), `world_ride_pose_lua_check` (tilt push/interp/write
order/sender), `lab_config_tests`, `worker_session_tests` (allow-lists, the three scenarios).
After `bf0388c3`: `TestRideSeatById` Mother Base by id (offered, targeted, confirmed), MB refused at
the start without the offer (never armed/played, latched, `unavailable; reason=blocked`), MB with
the key off (`reason=mission`), an unknown seat (`reason=seat_unknown`); `TestRideSeat` MB line;
`TestRideExtAndCensus` play sends the tilt-only addendum and admits the peer's tilt;
`world_wire_tests` RIDE_TILT in play; `lab_config_tests`/`lab_ini_check`/`friend_package_tests`
the tilt default and play admission.
After `abb0b7cf` (seat id default on, lab and play): `lab_config_tests` the defaults matrix (census
off, seat id and tilt on, in every profile and in play; an explicit 0 keeps each off),
`TestRideExtAndCensus` the driver's defaults and the play addendum (`SeatId`, then `SeatId|Tilt`
with the category; `seat_id=0` sends tilt only), `TestRideSeatById`/MB key-off cases set
`seat_id=0` explicitly, `friend_package_tests` the play package resolves `0|1|1`.

## Hidden runs

- Census `e54e0294`/`dd60eace`: `same=1` on both sides, `cat=` equal, the viewer's copy level.
- Tilt `0a2314f2`: the viewer's copy of the jeep pitched nose-up with the driver seated: A1 default on.
- Seat by id, sortie `bf0388c3` (`-SaveSeed real`): the real save continued into **Mother Base
  30050**, not 30010. Host: `goto_vehicle; name=sortie_3; id=25603`, `vehicle;id=25603;seats=4;
  riders=0,65535,65535,65535;localSeat=1` (`localSeat` is the 1-based Lua index of GetRiderId's
  table, i.e. native seat 0, the driver, as `vehicle_names;...;local=25603;seat=0` says), `ext_tx;
  rev=2; vehicle=25603; seat=0; cat=1; type=0x10; features=0x01`. Guest: `partner_ride=rx; kind=2;
  rev=2; ...; seat=0; vehicle=25603`, the same `ext_rx`, census `who=remote; id=25603; ref=ok;
  type=0x10; cat=1; realized=3; slot=3; ...; same=1`, then only `partner_ride=hide; reason=ride`: no
  `byid` line, no `ride_seat` play. **Cause:** seat mode was mission-gated to 30010/30020
  (`SeatMission` inside `seat_capable`), so in 30050 `vi.seat_mode` was false, `Observe` hid the
  vehicle ride as `ride` and `TickSeat` never ran; nothing logged why. Not the class: the MB jeep is a
  light vehicle (frames: the open jeep; 4 seats; its category object has the same vtable
  `0x22f7f90` as the 30010 `veh_lv_0000` 25608, `type=0x20; cat=2`, seated by name earlier), so cat 1
  vs 2 is most likely the two sides' light vehicles, not another class (inferred; Lua's
  `GetVehicleType` still decides lv/trc and refuses anything else as `no_class`). **Fix:** Mother Base is seat-capable with `partner_ride_seat_id=1` only, seats by id only
  (a refused offer is refused natively at the start), and every seat-mode hide logs `unavailable`.
- Seat by id, sortie `abb0b7cf` (`-SaveSeed real`, Mother Base 30050, `coop-vehicle-seat-sortie.txt`):
  guest `ext_rx; rev=2; vehicle=25603; seat=0; cat=1; type=0x10; features=0x03`, `byid;
  vehicle=25603; seat=0; offer=1; reason=none; remote_cat=1; local_cat=1`, `start; ...; entry=4`,
  `partner_lua=event=ride_seat; id=25603; seat=0; name=-; state=stateVehicleFrontLeftLoop; entry=0;
  byid=1; class=lv`, `target; vehicle=25603; seat=0; wrote=1; entries=1; cur=0; ids=25603,0,0;
  flags=0x0f,0x00,0x00; reason=none; ms=33`, `confirmed; vehicle=25603; seat=0;
  state=0x489157d7d94f`, 13 status lines `phase=seated; ...; byid=1` with `err_mm` 0..218, no
  `seat_far`, then `exit; reason=foot; via=stop; confirmed=1`. Frame `guest-sortie-seat-drive-a-04`:
  the remote driver seated in the moving unnamed sortie jeep. **B1 default on, lab and play.**
  The reverse half (the host as the viewer) stopped on a guest `gameplay` refusal after the guest's
  goto (the Mother Base arrival demo), not on the seat; the scenario now waits 45 s after gameplay.

## Re-run plan (coordinator; one PC, both copies on this build)

1. **Seat by id** (first half done: `abb0b7cf`, B1 default on; the reverse half is still to re-run)
   `tools/worker/scenarios/coop-vehicle-seat-sortie.txt` with **`-SaveSeed real`**
   (`-PartnerProfile native -NativeStage gun`): Mother Base, `goto_vehicle sortie` -> the unnamed jeep
   (25603 in `bf0388c3`). The lab seed cannot reproduce it: it continues into 30010 without a
   deployed vehicle (`vehicle_none;alive_unnamed=none`); 30010's 25600/25601 exist only after a
   sortie with a vehicle. Guest log (first half; host log for the reverse half):
   `ext_rx; ...; vehicle=25603; seat=0; cat=1; type=0x10; features=0x03`,
   `byid; vehicle=25603; seat=0; offer=1; reason=none; remote_cat=1; local_cat=1`, `start`, `armed`,
   `play; ...; entry=4`, `partner_lua=event=ride_seat;id=25603;seat=0;name=-;state=stateVehicleFrontLeftLoop;entry=0;byid=1;class=lv`,
   `target; vehicle=25603; seat=0; wrote=1; entries=1; ...; ids=25603,0,0`, `confirmed`, status
   `byid=1` with `err_mm` well under 3000, no `seat_far`, and FRAMES with the driver seated in the
   moving jeep in both halves. Failure branches: `unavailable; reason=mission` (old build or the key
   off), `byid; offer=0; reason=..` then `refused; reason=..` (the gate),
   `partner_lua=event=ride_seat_refused;...;reason=no_class` (GetVehicleType is not a light vehicle
   or truck: the class map, not the seat states, refuses), `target; reason=..`, `seat_far`.
2. **30010 by id** (optional, after 1): needs a deployed vehicle in 30010. Smallest route: the real
   seed's host deploys from the ACC with a vehicle (or a save that did), then the same scenario; no
   code change. Until then 30010 sortie slots stay untested.
3. **Tilt** is default on: any vehicle scenario now shows `features=0x02` (or `0x03` with the seat
   id), `partner_ride_tilt=read` (driver) and `=write` (viewer); `coop-vehicle-tilt.txt` remains the
   targeted check.
