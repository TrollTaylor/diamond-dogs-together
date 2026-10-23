# Animation round 1, track V: the remote driver seated in the mirrored vehicle

Branch `wp/anim-seat` (based on `20f4afe`). Source plan: `critic.md` section 5 (5.1-5.6) of the
animation investigation. Nothing here was run in the game; everything below the "Hidden test"
heading is for the coordinator.

## What changed

| Area | Files | Summary |
|---|---|---|
| Seat patch module | `native/partner_ride_seat.{cpp}`, `native/include/mgscoop/partner_ride_seat.hpp` | The four reversible code dwords (anchor seat index, three camera-store skips), the seated state table (names + StrCode64) |
| Contract | `tools/generate_partner_contract.py`, `native/partner_player_contract.hpp` (regenerated) | 14 new pinned blocks, the patch dword constants, `ride_seat_assertions()` (callers, jump targets, function containment, no branch into a skipped span) |
| Wire | `native/partner_ride_wire.cpp`, `native/include/mgscoop/partner_ride_wire.hpp` | Ride wire v2 (seat + vehicle id), v1 still accepted; seated visibility; air rule needs 250 ms; act 97 is heli |
| Receipts | `native/partner_receipt.cpp`, `native/include/mgscoop/partner_receipt.hpp` | Tick suffix ` veh= seat=` and ` st=`, reply op `RIDE_SEAT` = 512 (+4 values), debug seat receipt, Lua refusal note parser, status names |
| Driver | `native/partner_driver.cpp`, `native/partner_driver_seat.inc`, `native/include/mgscoop/partner_driver.hpp` | The seat flow (release, arm, play, confirm, re-issue, sanity distance, exit, settle), v2 sender, rx/hide log caps, status census, `SeatManual` |
| Arbiter | `native/partner_native.cpp` | `seat` is a network/visibility leave reason (no fallback latch) |
| Observer | `native/game_script_observer.cpp` | Hooks `ride_seat_arm` / `ride_seat_revert`, the `MGS_COOP_PARTNER_SEAT_V1` receipt, sleep suppression also strips op 512 |
| Settings | `native/lab_config.cpp`, `native/include/mgscoop/lab_config.hpp`, `tools/lab/LabCommon.psm1` | `partner_native_ride=off|hide|seat`, `partner_ride_seat_enter=direct|getin`, `partner_ride_seat_exit=stop|getoff` |
| Lua | `lua/coop/partner_ride.lua`, `lua/coop/partner_sync.lua`, `lua/coop/debug_vehicle.lua`, `lua/coop/debug_menu.lua` | Sender seat/vehicle/status, receiver RIDE_SEAT handler with the locator name map, debug levers `vehicle_names` and `seat_dm` |
| Worker | `tools/worker/Invoke-WorkerScenario.ps1`, `tools/worker/Start-WorkerSession.ps1`, `tools/worker/scenarios/coop-vehicle-seat.txt`, `coop-vehicle-seat-manual.txt` | Allow-list keys, two scenarios |
| Tests | `tests/partner_ride_seat_tests.cpp` (new), `partner_driver_tests.cpp`, `partner_receipt_tests.cpp`, `partner_equip_wire_tests.cpp`, `partner_native_tests.cpp`, `lab_config_tests.cpp`, `partner_sync_lua_check.lua`, `debug_vehicle_lua_check.lua`, `debug_menu_lua_check.lua`, `lab_scripts_tests.ps1`, `worker_session_tests.ps1` | See "Tests" |

## Mechanism (re-verified by disassembly of mgsvtpp.exe 1.0.15.4, SHA-256 085c2f82...bb45)

Lua on the receiver plays, on body 1 (`R` = 1):

```lua
Player.RequestToPlayDirectMotion{{"coopRide", 1},
  [{<GetOn state>, false, <locator>, "CNP_ppos_c", "MTP_GLOBAL_C", true},]
  {<Loop state>, true, <locator>, "CNP_ppos_c", "MTP_GLOBAL_C", true},
  [{<GetOff state>, false, <locator>, "CNP_ppos_c", "MTP_GLOBAL_C", true}]}
```

`<locator>` is the receiver's own authored name of the remote's vehicle (`veh_lv_%04d`,
`veh_trc_%04d`). Every chained entry carries the locator (phase 6 applies the anchor only while
the current entry has flag 2, `0x1199508`). Never `stateDirectPlayRideRearLeftSeatOnVehicle`
(its phase-6 code drives the camera), never a horse/walker locator (the anchor returns true with
a stale matrix for any type but 50 and 4: `0x1198899 cmp r14w,4; jne 0x1198870 (mov al,1)`).

Verified facts the patches rely on:

- Anchor `0x1198770` is called only from phase 0 (`0x119980C`); type 50 -> vehicle service
  vt+0x18 -> CNP must be `CNP_ppos_c` (`0x2ccada28e251`, compare at `0x119882B`) ->
  `0xB7AF90(unit, rsp+0x30, r8b)` with `mov r8b, 2` at `0x119883D` -> rec+0x160..0x19F.
- Entry playback `0x1198CC0` is called only from phase 6 (`0x1199566`); the caller overwrites
  rax at once (`0x119956B movzx eax, byte [rbx+0x248]`), so the return value is unused. A named
  state with a locator passes `rec+0x78[i]` (`0x1198D15..0x1198D1D`).
- Seat-1 remap `0xB79A3F..0xB79A57`: seat 1 becomes seat 0 unless `word(unit+0x32) & 0xFE00`.
  On an empty receiver copy the front passenger is therefore probably drawn in the driver seat
  (hidden test T4). Seat 1 is implemented anyway.

### The four dwords (`native/partner_ride_seat.cpp`)

| Site | Original | Patched | Effect | Verified |
|---|---|---|---|---|
| `0x119883C` seat | `30 41 B0 02` | `30 41 B0 <seat>` | the anchor's seat index (byte 0 is the tail of `lea rdx,[rsp+0x30]`, unchanged) | 4 bytes inside one cache line (0x3C+4 = 64); no branch into `0x119883D..0x119883F` |
| `0x1198D56` cam_play | `C7 82 00 02` | `EB 64 00 02` | targeted entry: `jmp 0x1198DBC` over `mov [rdx+0x200],1` and `0x1198DB5 mov [rdx+0x1F0],rax` | `0x1198D58 + 0x64 = 0x1198DBC`; the epilogue `0x1198DBC..0x1198DCB` uses only rsp/rbx/rsi/rdi; no branch into `0x1198D57..0x1198D5F` |
| `0x1198D81` cam_plain | `48 8B 47 08` | `EB 39 47 08` | untargeted entry: `jmp 0x1198DBC` over both stores | `0x1198D83 + 0x39 = 0x1198DBC`; no branch into `0x1198D82..0x1198D84` |
| `0x11992E9` cam_end | `48 89 81 F0` | `EB 0C 81 F0` | DM End: `jmp 0x11992F7` over `[rcx+0x1F0]=rax` and `[rcx+0x200]=r14d` | `0x11992EB + 0x0C = 0x11992F7`; rdx (the P+0x138 object, loaded at `0x11992C7`) and r14 are untouched by the skipped stores and `0x11992F7` reads `[rdx+0x48]`; no branch into `0x11992EA..0x11992F6` |

**Deviation from critic 5.2 (cam_play).** The critic proposed a qword at `0x1198D22`
(`E9 95 00 00 00 ...`). `0x1198D22` is 8-byte misaligned (offset 2), so
`near_code::PatchQword` refuses it ("alignment"), and that helper also never flushes the
instruction cache (it was written for data pointers). The dword at `0x1198D56` (the first of
the two stores) skips exactly the same stores with the proven `PatchDword`; the three loads
before it still run, as in stock code. All four sites are locked dword compare-exchanges inside
one cache line each.

Arm writes seat first and the End skip last; a failure part way restores what it wrote. Every
write is preceded by `VerifyImage`: the 14 contract blocks `ride_seat_blocks` (both function
bodies, the jump targets, the End stores, the phase-6 apply) with the four dwords masked, so an
armed image re-validates and Revert is idempotent. A dword that is neither original nor ours is
refused (`site_<name>`), never overwritten.

The driver arms only when no DM runs on either body and nothing is pending
(`q18[0] != 0x8B && q18[1] != 0x8B && (g388 & 0x3C) == 0`), only in 30010/30020 (and Mother Base
30050 for an offered seat by id, docs/vehicle-ws3.md), only for the seat episode; it reverts after DM End ran (`q18[1] != 0x8B`), at MissionStart/MissionEnd/STOP
(MissionEnd also covers the window-close shutdown) and on every exit including link loss.

## Settings

| Key | Values | Default |
|---|---|---|
| `partner_native_ride` | `off`, `hide`, `seat` | driven native stages: `kPartnerNativeRideDriven` (= `seat`); else `off` |
| `partner_ride_seat_enter` | `direct` (the loop at once), `getin` (GetOn first, within 2.5 s of the first usable vehicle seat) | `getin` |
| `partner_ride_seat_exit` | `stop` (RequestToStopDirectMotion), `getoff` (the chain ends with GetOff; act 112 or foot advances to it) | `getoff` |

**Ghidra comparison (`RideVehicleActionImpl` vs `DirectPlayMotionActionPluginImpl`, October 16, 2026):**
Decompilation of `RideVehicleActionImpl` (`0x1412471B0..0x14125C050`) and `DirectPlayMotionActionPluginImpl` (`0x141198770..0x141199860`) in `mgsvtpp_1_0_15_4_aug_4_patch.exe` (`085c2f82...bb45`) confirmed that both invoke the exact same motion graph state player `(*(PS+0x78)+0x238)->vtable+0x170(mgr, body_index, 0, state_code, blend, 0)` with the same graph-0 vehicle states (`stateVehicleGetOnFrontLeft`, `stateVehicleFrontLeftLoop`, `stateVehicleGetOffFrontLeft`, etc.) and anchor body 1's transform `*(PS+0x270)[1]` via `VehicleCommunicationInfo::FetchVehicleSeatLocalMatrix` (`0x140B7AF90`). Because `RideVehicleActionImpl` also mutates the local vehicle's occupant slots (`RequestToUseRiderSeat`, `ClearRiderId`), turret ownership, `vars.playerDisableActionFlag`, and vehicle camera (`RequestToResetToNewRideVehicleCamera`), the 3-entry `DirectPlayMotionActionPluginImpl` chain (`GetOn` $\to$ `Loop` $\to$ `GetOff`) is strictly cleaner and safer for remote body 1. `partner_ride_seat_enter=getin` and `partner_ride_seat_exit=getoff` are now on by default (`kSeatGetInMs = 2500` ms, refreshed when a usable seat first arrives after an initial `seat=255` tick; mid-ride DM loss clears `get_on` so re-issued plays go straight to `Loop`; `SeatDmProbe` logs `when=loop` and `when=getoff`). Verified in hidden worker runs `6b4e1667bd994580818fe20d181b3b5d`, `bea50b8682754baa9bf94f7b482dac8e`, and `e10774b712824d12a12d0ace9d7b20d2`.

## Wire and receipts

- **Ride wire v2** (channel 2, 12 bytes): `52 02 kind seat rev_lo rev_hi veh_lo veh_hi ms0..3`.
  Sent only for kind=vehicle with a known seat or vehicle id; everything else stays v1
  (`52 01 kind 00 rev 00 00 ms`). Decode accepts both strictly: v1 reserved bytes zero; v2 seat
  0..3 or 0xFF; a v2 non-vehicle carries seat 0xFF and vehicle 0xFFFF. A same-revision seat or
  vehicle change is stale; the sender bumps the revision on any change.
  `partner_ride=rx; kind=..; rev=..; epoch=..; v=..; seat=..; vehicle=..` is logged only for a new
  state or revision (heartbeats no longer spend the 64-line cap).
- **Tick receipt suffix** (strict, in this order): ` ride=N` [` veh=<0..65535> seat=<0..3|255>`]
  [` st=0x<1..3 lowercase hex, <= 0xfff>`]. Lua sends `veh/seat` only on a vehicle and `st` only
  when a bit is set. RIDE_FAST is unchanged.
- **Status bits** (`st=`): bit 0 BINOCLE, 1 SUBJECT, 2 CBOX, 3 CBOX_EVADE, 4 TRASH_BOX,
  5 TRASH_BOX_HALF_OPEN, 6 BEHIND, 7 CARRY, 8 CQC_CONTINUOUS, 9 ON_HORSE, 10 ON_VEHICLE,
  11 ON_HELICOPTER (all twelve names exist in the exe; an absent binding leaves the bit unset).
  Logged on change: `partner_status=local; st=0x..; names=..; ride=..; vehicle=..; seat=..`
  (own 64-line cap). Not transmitted to the peer (census for the next manual test).
- **Reply op RIDE_SEAT = 512** (verified free): after the 5 look values (zeros) and the gani (0):
  `,<vehicle>,<seat>,<action>,<entry>`; action 1 play, 2 advance to GetOff; entry bit 1 GetOn,
  bit 2 GetOff. `reply_capacity` is now 160 (widest possible reply 133 characters).
- **Debug seat receipt**: `return 'MGS_COOP_PARTNER_SEAT_V1' -- arm <seat 0..3> <vehicle>` / `-- stop`.
- **Lua refusal note**: `event=ride_seat_refused;id=<vehicle>;seat=<seat>;reason=<..>` (LOG
  receipt) ends the native request at the next tick (parsed before the Lua log cap).

## Receiver Lua (`lua/coop/partner_sync.lua`)

- Name map: `GameObject.GetGameObjectId("TppVehicle2", name)` for `veh_lv_0000..0031` and
  `veh_trc_0000..0015`, cached; a miss rebuilds at most every 300 frames. Authored names
  (extracted read-only from the retail packs): 30010 `veh_lv_0000..0012`, `veh_trc_0000..0005`
  (f30010.fpkd); 30020 `veh_lv_0000..0010`, `veh_trc_0000..0009` (f30020_item.fox2), plus
  `reinforce_vehicle_0000` (mafr/afgh common reinforce, class unknown: not mapped, hides).
- State table: lv seat 0..3 = `stateVehicle{FrontLeft,FrontRight,BackLeft,BackRight}Loop` with
  their `GetOn*`/`GetOff*`; trc seat 0 = `stateTruckFrontLeftLoop` / `stateTruckGetOn/OffFrontLeft`;
  trc seat 1 = `stateVehicleFrontRightLoop` (guess: the MOG has no `stateTruckFrontRightLoop`) with
  `stateTruckGetOn/OffFrontRight`. All codes verified present in `TppPlayer2_layers.mog`.
- Refusals (the partner then hides): `no_name` (unnamed, e.g. sortie vehicles 25600..25607),
  `state` (truck seat 2/3; APC/tank never resolve), `shared_seat` (the viewer's own vehicle and
  seat), `bad_args`. Notes: `event=ride_seat;id=..;seat=..;name=..;state=..;entry=..`,
  `event=ride_seat_next;id=..;seat=..`, `event=ride_seat_refused;id=..;seat=..;reason=..`.

**Deviation from the task (get-off).** `RequestToInterruptDirectMotion` (`0x9D9190`) stores
its two numbers at g+0x154/+0x155 (`[rsp+0x3c]`, `[rsp+0x3d]`) and writes the qword
`[rsp+0x38]` to g+0x150; the consumer (`0x1199401..0x119940B`) compares the dword g+0x150,
four bytes of uninitialized stack, with the body id. A per-instance interrupt from Lua is
therefore unreliable, and the two arguments are (entry now, entry after), not (instance, entry).
The get-off uses `RequestToGoNextDirectMotion` (`0x9D8F20`, g+0x388 |= 0x10, consumed in phase 10
by the first body with an active DM; only body 1 has one while the dwords are armed) on a chain
that ends with the GetOff entry, with a stop fallback after 4 s. Known edge: a GoNext issued just
as the DM ends lingers until the next DM End clears 0x3C; the next arm then waits ("pending").

## Native seat flow (`native/partner_driver_seat.inc`)

Visibility (`partner_ride_wire::Observe`): with seat mode (nride=seat, 30010/30020, hooks, no
private pose lease, the (vehicle, seat) not latched) a live vehicle ride in a usable seat (0..3,
id >> 9 == 50) is `seated` and shown at once; heli (acts 97..108), horse, walker, stale, air and
dead still hide.

1. **Start** when seated and body 1 is realized with parts loaded (the existing range/re-show flow
   places and realizes it first). `partner_ride=seat_show`, `partner_ride_seat=start`.
2. **Own body 1** every tick: arbiter blocked with reason `seat` (no latch), deferral, window and
   native targets withdrawn. The DM comparator patch and the realize bits stay; the reply carries
   only the seat's ops; no xform, slot, Warp, recon, dm=map or animation write runs (the seat
   returns before them; `ride_seated` is also ORed into every `vehicle_attached` gate). The local
   sender, look and probe continue; `m.realized/parts_loaded` stay truthful (nametag).
3. **Release**: a DM already on body 1 (a dm=map clip) is stopped (never while the viewer's own DM
   runs), then forgotten.
4. **Arm**: no DM on either body, nothing pending (3 s, else refused); comparator patch; the four
   dwords. The camera-connect gate (`cam_idle`) does not apply: the skips are armed.
5. **Play**: RIDE_SEAT action 1 (entry bits from the settings).
6. **Confirm** when `q18[1] == 0x8B`, `rec248 & 0x10` and `rec+0x18` (low 48 bits) is a seated
   state of that seat. Unconfirmed after 2 s: our pending request is cleared and the play
   re-issued (3 plays, then refused). A foreign state on body 1 is refused; a DM that ends under
   a confirmed seat is re-issued (3 plays).
7. **Sanity**: body more than 3 m from the remote's streamed (seat-attached) pose for 1 s:
   `partner_ride=seat_far`, refused.
8. **Exit** on: kind not vehicle (`foot`/`ride`), stale ride lease (`ride_stale`), dead link
   (`lease`), seat or vehicle change (`vehicle_change`), heli act (`heli`), remote act 112
   (`getoff_act`, latched), unseated (air/dead), 200 m range (`range`), unrealized body,
   configuration change. exit=getoff with a confirmed loop entry and reason getoff_act/foot sends
   RIDE_SEAT action 2, else DM_STOP (re-sent after 1 s, at most 3); after 7 s it gives up.
9. **Settle** once `q18[1] != 0x8B`: dwords reverted, `partner_ride_seat=exit`, bits cleared and
   `range_needs_place` set: the existing (proven) place flow re-places the body at the remote's
   on-foot pose and realizes it, and native re-enters after its cooldown. Refusals and get-off
   latch the (vehicle, seat) so it hides until the remote's ride changes.

The `hide`-mode branch keeps `range_needs_place` only through bits cleared (the existing placement
needs `!bits_set`); critic 5.4 step 5's "range_needs_place" therefore includes the clear.

### 0.13.6: long rides, a missing vehicle, temporary refusals (online-ride-robust)

The first two-PC session (2026-10-22, diag-other #1, #2, #9) broke the flow above three ways; all
three are fixed by default (unit tests `TestRideRobust`, `partner_player_tests` ClearDmBits,
`partner_native_tests` Rebase; the hidden repro is `tools/worker/scenarios/coop-vehicle-longride.txt`).

- **The partner vanished ~340 m into a ride (both copies).** The engine's realize policy
  `0x121B5E0` tests body 1's **slot** `(*(S+0x20))[1]` against the loaded area, never the body or
  the record (`0x14121B8B7 add rdi,[r9+0x20]`, `0x14121B8C2` / `0x14121B8CC` the z / x loads,
  `0x14121B92C call [rax+0x20]` the area test; pinned in the contract). The seated DM moves only
  the body, so the slot stayed at the boarding spot and the engine unrealized body 1
  (`partner_bits=cleared; reason=range_rearm`). **Prevention:** while seated, at most once a second
  (`kSeatSlotFollowMs`), the slot is rewritten with the seated body's position and yaw when it has
  drifted more than 25 m (`kSeatSlotFollowMm`), so it stays well within 100 m of the vehicle. A slot
  write does not move the body (findings 2026-10-04, `apply=slot`); `apply=input` already publishes
  the body there on foot. `partner_ride_seat=slot_follow; vehicle=..; seat=..; drift_mm=..; body_mm=..`
  is logged once per episode; the exit line counts them (`; follows=N`).
- **Recovery when body 1 is unrealized anyway.** The exit no longer sends `DM_STOP` to a body that
  is gone (its DM ended with it; `q18[1]` only still reads 0x8B): `partner_ride_seat=dm;
  when=exit_gone`, then settle at once (`exit; ...; reason=unrealized; via=gone`). In 0.13.5 the
  three stops were never consumed: `RequestToStopDirectMotion` (`0x9DB4C0`) sets the global
  `g+0x388 |= 8`, which only DM End (`0x11992FB`) clears, so `g388=0x08` stayed ~3 min and every
  re-seat refused `pending`. After an `exit_timeout` or a gone body, settle clears the request bits
  this episode sent (Play 4 plus the Stop 8 / GoNext 0x10 it issued) through
  `partner_player::ClearDmBits`: only when request byte 0 == 1 (the last Play named body 1) and
  Stop / GoNext only while the viewer's own DM is not running
  (`partner_ride_seat=stale_cleared; ...; bits=0x08; when=settle; reason=exit_timeout`).
- **Temporary refusals never latch.** `pending`, `dm_read`, `release` and `host_dm` describe a
  moment, not the vehicle: no block, the next attempt may seat it. An arm refused for `pending` or
  `host_dm` with nothing armed and no DM on body 1 goes to the fallback below instead of an exit.
- **Fallback (`SeatPhase::fallback`): visible without a seat.** Where only the id can seat
  (Mother Base) and the by-id gate says this copy has no such vehicle (`local_read`: it was
  Fultoned here, the friend's jeep 25603 at 18:04:17) or it is not realized here (`unrealized`),
  and after a `pending` / `host_dm` arm refusal: body 1 stays realized and owned by the seat flow
  (native blocked as for a seat), and the reply Warps it to the remote's streamed (seated) pose every
  tick (the proven per-tick Warp of the input fallback; Warp also writes the slot). No DM is played,
  no dword armed; no Warp while a DM plays on body 1. Every `kSeatFallbackRetryMs` (2 s) it
  re-checks: the by-id gate (another refusal reason: latched as at the start), no DM on either body,
  nothing pending (a pending request of ours with no DM anywhere is cleared, `when=fallback`); then
  `fallback_end; ...; next=release` and the normal release, arm and play in the same tick. Leaving
  (foot, ride change, range...) settles without a stop (`exit; ...; via=fallback`). The smallest fix
  for the 0.13.5 symptom "the host vanished for 25 s while driving a jeep the friend had Fultoned";
  the vehicle itself is still not world-synced (the proper `VEHICLE_GONE` world op is a separate
  branch), so the partner floats at the seat height there, standing, until the vehicle exists again
  or the ride ends.
- **Clips after every ride (diag #9).** Settle (and a ride hide ending) sets
  `Mission::net_quality_rebase`; the next `TickNative` calls `partner_native_net::Rebase` before
  observing, so the first input frame after the ride only anchors the link-quality window instead
  of being one 10..85 s arrival gap (`drive=clips; reason=net_quality` for ~4 s).

## Ride visibility fixes (both modes)

- `partner_ride=rx` only on a new state or revision; hide/foot_stable/seat_show/seat_far lines on
  `ride_vis_cap`, `partner_ride_seat` events on `seat_cap` (64 each), the 1 Hz
  `partner_ride_seat=status` on its own 600-line cap.
- The `air` candidate (body act 25 within 1.5 s of our placement, remote standing, |dy| >= 2 m)
  must hold 250 ms (`kAirHoldMs`): a teleport Warp's few frames of act 25 no longer hide
  (action-anims 2.3; all 167 air hides of the snapshot were this). Chosen over the 500 ms
  post-teleport window because it needs no knowledge of the placement's source.
- Remote act 97 (support-heli boarding) hides like 98..108.

## Telemetry (log signatures)

```
partner_ride=rx; kind=2; rev=5; epoch=7; v=2; seat=0; vehicle=25608
partner_ride=seat_show; vehicle=25608; seat=0
partner_ride_seat=start; vehicle=25608; seat=0; entry=0; enter=direct; exit=stop; remote_act=111
partner_ride_seat_patch=applied; seat=0; sites=0x119883c,0x1198d56,0x1198d81,0x11992e9; seat_dword=0x00b04130
partner_ride_seat=armed; vehicle=25608; seat=0; name=-; state=-; reason=none; cam1f0=0x...; cam200=..
partner_ride_seat=play; vehicle=25608; seat=0; name=lua; state=lua; entry=0; attempt=1; reason=none
partner_lua=event=ride_seat; id=25608; seat=0; name=veh_lv_0000; state=stateVehicleFrontLeftLoop; entry=0
partner_ride_seat=confirmed; vehicle=25608; seat=0; state=0x489157d7d94f; entry=0; ms=..; plays=1; manual=0
partner_ride_seat=dm; when=confirmed; q18=0x..,0x8b; g388=0x00; ...; cam1f0=0x...; cam200=..; cam_ok=1
partner_ride_seat=status; phase=seated; vehicle=25608; seat=0; confirmed=1; q18=..; rec18=..; rec1a0=0; rec248=0x..; cam1f0=..; cam200=..; err_mm=..; remote_act=111; manual=0
partner_ride_seat=exit_request; vehicle=25608; seat=0; reason=getoff_act; via=stop
partner_ride_seat_patch=reverted; reason=getoff_act
partner_ride_seat=exit; vehicle=25608; seat=0; reason=getoff_act; via=stop; confirmed=1; plays=1; stops=1; ms=..; manual=0
partner_bits=cleared; reason=seat_exit
partner_ride_seat=refused; vehicle=25600; seat=0; name=-; state=-; reason=lua_refused; manual=0   (+ partner_lua=event=ride_seat_refused; ...; reason=no_name)
partner_ride=seat_far; vehicle=..; seat=..; err_mm=..; ms=..
partner_status=local; st=0x400; names=on_vehicle; ride=2; vehicle=25608; seat=0
partner_ride_seat=slot_follow; vehicle=25608; seat=0; drift_mm=40000; body_mm=..   (0.13.6, once per episode)
partner_ride_seat=dm; when=exit_gone; ...                                         (0.13.6: no stop to a gone body)
partner_ride_seat=exit; vehicle=..; seat=..; reason=unrealized; via=gone; ...; follows=N
partner_ride_seat=stale_cleared; vehicle=..; seat=..; bits=0x08; when=settle|fallback; reason=..
partner_ride_seat=fallback; vehicle=25603; seat=0; reason=local_read|unrealized|pending|host_dm; show=warp; byid=0
partner_ride_seat=fallback_end; vehicle=..; seat=..; reason=..; next=release; ms=..; warps=..; byid=1
partner_ride_seat=exit; vehicle=..; seat=..; reason=foot; via=fallback; ...
```

## Debug levers (`lua/coop/debug_vehicle.lua`, F9 menu / worker `run`)

- `vehicle_names`: `vehicle_names;lv=<runs>;trc=<runs>;local=<id>;seat=<s>` where a run is
  `<first>-<last>@<first id>` (ids consecutive) or `<index>@<id>`. Readable while driving.
- `seat_dm <state> <seat 0..3> <locator>`: refuses a non-`state*` name, the s10150 rear-seat state,
  a locator that is not a TppVehicle2 id (type 50), and any mission but 30010/30020; arms the
  dwords natively (`SeatManual`: body realized, no DM on either body, nothing pending), then plays
  `{{"coopRide",1},{state,true,locator,"CNP_ppos_c","MTP_GLOBAL_C",true}}`. Works with
  `partner_native_ride=off` (the body stays realized while the remote drives).
  `seat_dm stop` stops the DM and ends the manual seat (native backs the stop up after 1 s; the
  dwords revert after DM End).

## Hidden test (coordinator)

Build with `-ReleaseGun`; both copies on the same package.

**T1 (retail-exact seat, the critic's first experiment):**
`tools/worker/scenarios/coop-vehicle-seat-manual.txt` (partner_native_ride=off). Expect on the
guest after `seat_dm stateVehicleBackLeftLoop 2 veh_lv_0000`: `partner_ride_seat=armed; ...;
reason=manual`, `partner_ride_seat=confirmed; ...; manual=1`, status lines with `q18=..,0x8b`,
`rec248` with 0x10, and in the bursts the host's body in the jeep's rear-left seat **following the
moving jeep** (phase 0 re-anchors every frame). Check no jitter/push/vehicle-hit damage. Then seat 0
(`stateVehicleFrontLeftLoop`) and seat 1 (`stateVehicleFrontRightLoop`; **T4**: if it lands in the
driver seat, that is the `0xB79A3F` remap). If the locator differs, read `host run vehicle_seats`
(`vehicle;id=..`) and `guest run vehicle_names`.

**T2/T3 (automatic seat):** `tools/worker/scenarios/coop-vehicle-seat.txt`
(partner_native_ride=seat). Guest log as in "Telemetry", host log for the reversed half. In the
bursts: the driver seated while driving straight and turning, the nametag above the seat, the
standing body back beside the jeep after `exit`. `partner_lua=event=ride_seat_refused` with
`no_name` means the jeep was not an authored `veh_lv_*` (record `vehicle=` for the sortie-name
question).

**T5 (viewer's camera connect):** compare `cam1f0`/`cam200` in `armed`, `dm when=confirmed`, the
status lines and after `exit` while the viewer is on foot (all 0) and while the viewer drives its
own vehicle (`0x88000000000064xx`, `1`): they must not change across the partner DM entry and End.

**T6 (variants):** add `labsetting partner_ride_seat_exit=getoff` (expect `exit_request; ...;
via=getoff`, `partner_lua=event=ride_seat_next`, the GetOff animation in `seat-exiting` bursts) and
`labsetting partner_ride_seat_enter=getin` (`start; ...; entry=1`). Roles reversed covers Quiet
seated on the host screen.

**Failure signatures:** `partner_ride_seat_patch=refused; reason=block_0x...|site_...` (image
mismatch: nothing written); `partner_ride_seat=refused; ...; reason=unconfirmed|foreign_dm|lost|
far|host_dm|pending|dm_patch|seat_patch|release`; `exit; ...; reason=exit_timeout` (DM End never
observed, dwords reverted anyway). Every refusal hides the partner as `hide` would.

**If accepted:** flip `kPartnerNativeRideDriven` (see "Settings").

## Pins added (`tools/generate_partner_contract.py`, `--check` passes)

Blocks: `0x1198770` (69), `0x11987B5` (48), `0x11987E5` (79), `0x1198834` (60), `0x1198870` (48),
`0x1198CC0` (63), `0x1198CFF` (35), `0x1198D22` (64), `0x1198D62` (59), `0x1198DBC` (16),
`0x11992C3` (63), `0x1199508` (78), `0xB7AF90` (79), `0xB79A3F` (27); with the existing
`0x1198D9D`, `0x1198DB5`, `0x1198D56`, `0x11992E9`. Assertions: single callers of `0x1198770`,
`0x1198CC0`, `0x1199140`; the caller's rax overwrite; the seat dword bytes; cache lines; the three
displacements; RUNTIME_FUNCTION containment; no rel8/rel32 branch into any skipped or replaced
span. Constants: `ride_seat_rva`, `ride_cam_play_rva`, `ride_cam_plain_rva`, `ride_cam_end_rva`,
`*_old`/`*_new`, `ride_seat_blocks[]`. Runtime: `partner_ride_seat::VerifyImage` before each
write. Not called natively: nothing (the module only writes code bytes; Lua calls the retail
bindings `RequestToPlayDirectMotion`, `RequestToStopDirectMotion`, `RequestToGoNextDirectMotion`,
`GetGameObjectId`, `SendCommand{GetRiderId}`). 0.13.6 adds (read-only pins, nothing patched or
called): `0x121B8B7` (25, the realize policy's slot loads), `0x121B920` (15, its area call),
`0x9DB4C0` (10, `RequestToStopDirectMotion`: `g+0x388 |= 8`) and `0x9D8F20` (10,
`RequestToGoNextDirectMotion`: `g+0x388 |= 0x10`); 747 code sites.

## Camera agent coordination

The skips touch only the DM camera-connect stores (CAM+0x1F0/+0x200 at DM entry and End), never
`coop_carry`, the P+C0 setter `0x11F6520` or `RequestToPlayCameraNonAnimation`. While a seat DM is
armed, DirectMotion entry and End leave those fields alone for every body (the local player has
no DM in free roam; the dm=map `cam_connect` refusal is unchanged).

## Known limits

- Unnamed vehicles (ACC sortie/deployed ids 25600..25607, reinforcements) are seated by id through
  the WS3 seat-by-id path (`partner_ride_seat_id`, default on in lab and play since hidden run
  `abb0b7cf`, docs/vehicle-ws3.md "B1"), which also makes Mother Base (no authored locators)
  seat-capable by id only. They still hide (5.6 option a) when the offer refuses (an older peer
  without the ride addendum's seat-id feature, another category) or with
  `partner_ride_seat_id=0`; since 0.13.6 a Mother Base copy that lacks the vehicle (`local_read`)
  or has it unrealized is shown without a seat instead (the fallback above). Every seat-mode hide
  of a live vehicle ride logs `partner_ride_seat=unavailable; ...; reason=..`.
- Horse, D-Walker, heli, APC and tank still hide.
- The vehicle copy itself is still a SetPosition teleport (no wheels or steering); WS3 "A1"
  (`partner_ride_tilt`, default on in lab and play, docs/vehicle-ws3.md) adds the driver's
  pitch/roll (census `same=1`, hidden run `0a2314f2`).
- The 3 m / 1 s sanity distance compares two 100 ms-delayed streams; watch `err_mm` in the status
  lines at speed before tightening or loosening it.

## Tests

`partner_ride_seat_tests` (fixture image: arm/re-arm/revert, masked re-validation, each block's
refusal, jump targets, foreign dwords, read-only pages; the retail `.xtext` copy when installed),
`partner_driver_tests` `TestRideSeat` (enter/confirm/hold/exit/re-show, get-off advance and its
stop fallback, get-in, unconfirmed retries, Lua refusal, dm=map release and the viewer's DM,
sanity distance, patch refusal, Mother Base, nride=hide, seat change, the manual lever, the v2
sender and status census) and `TestRideRobust` (0.13.6: no stop to a gone body and the re-seat,
the stale stop cleared after exit_timeout and a foreign one kept, pending without a latch and the
fallback's Warp and re-seat, dm_read unlatched, the Fultoned jeep's local_read / unrealized fallback
and its re-seat by id, the slot follow cadence and threshold, the link-quality rebase after a
ride), `partner_player_tests` (ClearDmBits on synthetic memory), `partner_receipt_tests` (suffix grammar, status names, seat receipt,
refusal note, RIDE_SEAT replies), `partner_equip_wire_tests` (v2 codec and store, seated
visibility, air hold, act 97), `lab_config_tests`, `partner_native_tests`,
`partner_sync_lua_check` (RIDE_SEAT handler, name map cache, refusals, GoNext; sender suffix and
seat cache), `debug_vehicle_lua_check` (both levers), `debug_menu_lua_check` (mounted admission),
`lab_scripts_tests`, `worker_session_tests` (allow-lists, scenario validation).
