# WS3 vehicles: the driver seated in unnamed vehicles (B1) and the ground tilt (A1)

Branch `wp/feat-vehicle` (from `wp/integ6`, 0.13.0). Spec: the WS3 section of the co-op gaps spec
(session scratchpad `gaps/coop-gaps-spec.md`), research "vehicle" and the CRITIC corrections #15-#17
(`gaps/research.md`, `gaps/spec.md`). The census and the seat by id are lab-only and off by default
(play forces them off); RIDE_TILT is on by default in lab and play since hidden run `0a2314f2`
(the viewer's jeep pitched with the seated driver). Hidden-run findings and the re-run plan are at
the end.

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
| `partner_ride_seat_id=1` | 0 | forced 0 | B1: the seat-by-id offer, the ride addendum's category, the id write; Mother Base seat mode. |
| `partner_ride_tilt=1` | **1** | admitted | A1: the ride addendum advertises RIDE_TILT; the driver sends, the viewer writes. `=0` turns it off. |

All three are in the three `-LabSetting` allow-lists (`Invoke-WorkerScenario.ps1` ValidatePattern and
labsetting line, `Start-WorkerSession.ps1`), `tools/lab/LabCommon.psm1` and `ParseVehicleKey`
(`native/lab_config.cpp`, a side helper: the main chain is at MSVC's nesting limit).
`ResolveWeaponDefaults` turns the census and the seat by id off in play (no `play_refused` bit, as
before); no profile turns them on. `lab_ini_check` prints the three resolved values
(`friend_package_tests`: the play package resolves `1|0|0`).

**A1 default: ON (lab and play).** Hidden run `0a2314f2` showed the viewer's copy of the jeep
pitched nose-up with the driver seated; census runs `e54e0294`/`dd60eace` showed `same=1` (the
realized body is the unit record). It only writes the viewer's own copy record `+0x44/+0x48`,
behind the unchanged identity/sanity gates (`TiltRefusal`: key, contract blocks and thresholds,
runtime thread, peer, type 50, not the viewer's own vehicle, realized, `identity` body == record,
two equal reads, finite, |angle| <= 1570 mrad). Older peers: the ride addendum is dropped as an
invalid ride packet (one `partner_ride=refused; reason=packet_or_session` line), so the tilt
feature is never seen and no RIDE_TILT is sent to them; an unknown world op would be skipped anyway.

**B1 default: OFF.** It only acts in the previously refused `no_name` case (a named vehicle keeps
its locator; the record write runs only when every chained entry is untargeted), but two runtime
facts are unproven: the realized flag `rec+0x6E` and the DM confirmation of an entry that starts
untargeted (research "Open"). The memory rule for the seat ("no default flip before a hidden-run
frame shows the driver seated") applies here too: no seat frame exists yet (`bf0388c3` below).

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
- **Offer** (`SeatByIdGate`, at the seat start and again at the write): both hooks, the ride still
  this (vehicle, seat), a live addendum (`no_ext`), its category for this ride
  (`remote_category`), not the viewer's own vehicle (`local_vehicle`), this copy's same id readable
  (`local_read`), realized (`unrealized`) and of the same category (`category`). The offer sets
  RIDE_SEAT entry bit 4 (`partner::seat_entry::by_id`); a refusal keeps today's behaviour (Lua
  refuses `no_name`, the ride hides) and is logged `partner_ride_seat=byid; ...; offer=0; reason=..`.
- **Mission scope**: seat mode runs in 30010/30020 (authored locators) and, with
  `partner_ride_seat_id=1` only, in Mother Base 30050 (`SeatCapableMission`): MB has no
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
  RIDE_TILT under the same tick. In play the ride addendum carries only the tilt feature (the seat
  id feature stays lab-only).
- **Receiver** (`R.fastApply`): the tilt samples ride on the FAST track (same sender clock),
  interpolated with `R.angleLerp` at the render time, held at most 300 ms; right before each
  `R.place` (SetPosition) the receipt `-- write <id> <a44> <a48>` runs
  `partner_vehicle::TiltRefusal` on two equal reads (key, contract blocks + thresholds re-verified,
  runtime thread, peer, type 50, not the viewer's own vehicle, realized, body chain ok,
  `identity` body == record, finite, |angle| <= 1570 mrad) and writes the two floats.
- Log lines: `partner_ride_tilt=read|write|refused; ...`, the world log's
  `event=ride_fast; ...; tilt_sent=..; tilt_written=..`.
- Known limit: a large move (>= 1 m or >= 0.1 rad, e.g. after a relay burst) still takes 0xBA9710's
  yaw-only branch for one frame.

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

## Re-run plan (coordinator; one PC, both copies on this build)

1. **Seat by id** `tools/worker/scenarios/coop-vehicle-seat-sortie.txt` with **`-SaveSeed real`**
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
