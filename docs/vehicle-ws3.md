# WS3 vehicles: the driver seated in unnamed vehicles (B1) and the ground tilt (A1)

Branch `wp/feat-vehicle` (from `wp/integ6`, 0.13.0). Spec: the WS3 section of the co-op gaps spec
(session scratchpad `gaps/coop-gaps-spec.md`), research "vehicle" and the CRITIC corrections #15-#17
(`gaps/research.md`, `gaps/spec.md`). Nothing here was run in the game; the hidden-run plan is at
the end. Everything new is lab-only and off by default (play forces the three keys off).

## Root causes (research, unchanged)

- (b) Missing driver: the user's vehicle was 25600, the deployed/sortie vehicle (TppVehicle2 slot 0).
  It has no authored locator, so `partner_sync.lua` refused the seat with `no_name`, native latched
  `lua_refused` and hid the partner for the whole ride.
- (a) No tilt: only x, y, z and yaw cross the wire (RIDE_FAST) and SetPosition carries only
  vec4(x, y, z, rotY); `0xBA9710`'s per-frame (small) move keeps the copy's own record angles.

## Settings (lab.ini, lab mode only, default 0)

| Key | Effect |
|---|---|
| `partner_ride_census=1` | Step 0: one read-only `partner_ride_census` line per ridden vehicle per second (own cap 400). |
| `partner_ride_seat_id=1` | B1: the seat-by-id offer, the ride addendum's category, the id write. |
| `partner_ride_tilt=1` | A1: the ride addendum advertises RIDE_TILT; the driver sends, the viewer writes. |

All three are in the three `-LabSetting` allow-lists (`Invoke-WorkerScenario.ps1` ValidatePattern and
labsetting line, `Start-WorkerSession.ps1`), `tools/lab/LabCommon.psm1` and `ParseVehicleKey`
(`native/lab_config.cpp`, a side helper: the main chain is at MSVC's nesting limit).
`ResolveWeaponDefaults` turns them off in play; no profile turns them on.

**B1 default: OFF.** It only acts in the previously refused `no_name` case (a named vehicle keeps
its locator; the record write runs only when every chained entry is untargeted), but two runtime
facts are unproven: the realized flag `rec+0x6E` and the DM confirmation of an entry that starts
untargeted (research "Open"). The memory rule for the seat ("no default flip before a hidden-run
frame shows the driver seated") applies here too.

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
cur=..; ids=..; flags=..; reason=..; ms=..`, `partner_lua=event=ride_seat;id=..;seat=..;name=-;state=..;entry=..;byid=1;class=lv|trc`, the status line's new `; byid=1` suffix.

## A1: tilt

- New world op **RIDE_TILT 0x2D** (lab-only datagram, 14 bytes: sender tick u32, id u16, two i32
  mrad in [-1570, 1570]); RIDE_FAST keeps its exact 7 values (critic #15); its own latest-value
  slot, so a RIDE_FAST and a RIDE_TILT of the same tick both reach Lua; an older peer skips the
  unknown code.
- **Capability**: the ride addendum's tilt feature (`pd::RideTiltPeer`). The caps wire v1 cannot
  carry a new bit: its decoder rejects unknown feature bits and non-zero reserved bytes, so an
  older peer would drop the whole caps message (deviation from the spec's "caps wire").
- **Sender** (`world_ride_pose.lua` R.fastSend): after RIDE_FAST, the native receipt
  `MGS_COOP_RIDE_TILT_V1 -- read <id>` returns the driver's own record +0x44/+0x48 in mrad only if
  the key is on, the id is the local vehicle (the tick's `veh=`) and the peer advertised tilt; then
  RIDE_TILT under the same tick.
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

## Hidden-run plan (coordinator; one PC, both copies on this build, `-SaveSeed lab`)

1. **Census** `tools/worker/scenarios/vehicle-tilt-census.txt` (`-PartnerProfile native
   -NativeStage gun`). Look for, on the host (`who=local`) and the guest (`who=remote`):
   `ref=ok; body_ref=ok; ...; same=1` on both, `cat=` equal on both sides, the driver's
   `a44_mrad`/`a48_mrad` changing on slopes and small on flat ground, the guest's copy level.
   `same=0` (or a refusal) means A1 stays off and the write target needs a new decision.
2. **Seat by id** `tools/worker/scenarios/coop-vehicle-seat-sortie.txt`: the sortie vehicle 25600
   (or 25601..25607; `goto_vehicle sortie`), both directions. Needs a save with a deployed vehicle
   (if `goto_vehicle sortie` answers `vehicle_none`, the run proves nothing). Look for `byid; offer=1`,
   `partner_lua=event=ride_seat;...;byid=1;class=lv`, `target; ...; wrote=1`, `confirmed`, status
   `err_mm` well under 3000 and `byid=1`, no `seat_far`, and FRAMES with the driver seated in the
   moving vehicle in both halves. Then decide the B1 default.
3. **Tilt** `tools/worker/scenarios/coop-vehicle-tilt.txt` (only after 1 showed `same=1`):
   `ext_rx ... features=0x02` both sides, `partner_ride_tilt=read` (host), `=write` (guest), no
   `refused; reason=identity`, the guest census `who=remote` angles tracking the host's
   `who=local`, and frames of the jeep and its seated driver pitched/rolled on slopes.
