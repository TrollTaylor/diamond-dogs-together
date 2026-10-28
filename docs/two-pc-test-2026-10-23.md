# Two-PC test, 2026-10-23: problems to fix next session

**Setup.** Both PCs ran 0.13.6, over Steam. The host is the user; the joiner is the friend. Only the friend's logs were analysed (Collect-Logs zip `mgs-coop-logs-join-20261023-222242.zip`, extracted under the session scratchpad `online1023/friend/`). The host's own log for the same session is in `D:/Games/steamapps/common/MGS_TPP/mgs-coop/script-observer.log` (tail) and has not been read yet.

**Route.** Title, then the ACC (40050), Mother Base (30050, three visits), the ACC again, then Africa free roam (30020). The friend's guest session starts at script-observer.log line ~5704; Africa starts at line 10614.

## Works now (confirmed in these logs)
- **The 0.13.6 gun fix.** After the friend's relaunch, the host's manifest was accepted with `sender_epoch=2; link_epoch=1` (line 5874). In Mother Base the host's guns bound on the friend's copy (`equip_writer=handle_bound`, slots 1 and 2, lines 6562 and 6578), and `partner_weapon=match`.
- **Other weapon types in Africa.** The Hand of Jehuty arm (516) and grenades (354, 349) matched on the partner body (lines 12171, 14161).
- **Shared game over.** `downed_damage=stock_death` and `game_over_applied` each fired twice.

## Bug 1: the host's guns don't show on the friend's screen in Africa (30020)
User report: "my weapon animations not showing up in africa but they did in mother base".

**Evidence (friend's log, 30020):**
- The manifest and the preload are fine: `equip_preload=authorized ... manifest=1; want=0,46,9` (line 10574).
- The writer plans slots 1 and 2, then waits and backs off repeatedly:
  - `equip_writer=readiness_wait|backoff; reason=selected_bank_aux_not_empty` (14×/10×)
  - `remote_slot_not_empty` (9×/8×)
  - `other_slot_unexpected` (2×)
- It finally gives up for good: `equip_writer=refused; reason=selected_bank_aux_not_empty` for slot 1 (want 46) and slot 2 (want 9) (lines 14569 and 14572).
- Slot 0 was refused earlier, at line 11367 (`reason=stock_optional_dict`, `a1_refused no_base`).
- Result: body 1 has no primary or secondary gun. `partner_weapon=unsupported/absent` with `strip=0x58`, so aim, fire and reload of rifles and pistols are masked. The arm and the grenades still work.

**Likely cause (to verify).** In the Africa allocation, body 1's equipment bank or its "aux" slot is already occupied when the writer wants to bind. In Mother Base the same flow succeeds. Candidates:
- The f30020 pack preloads something into body 1's bank: a stock loadout, the guest's buddy, or the D-Walker or horse.
- The writer's readiness check is too strict for 30020.

**Next steps:**
- Read the writer's readiness rules: `selected_bank_aux_not_empty`, `remote_slot_not_empty` (native `equipment_stock_writer.cpp` / `partner_support_equip.cpp`).
- Log what occupies the bank in 30020.
- Reproduce with a hidden-worker run in 30020 (`-SaveSeed lab` reaches 30010; Africa may need `-SaveSeed real` or a lab goto to 30020) and compare with 30050.
- Also check the host-side log: the friend's guns showed fine on the host's screen.

## Bug 2: the host's vehicle doesn't sync on the friend's screen in Africa (30020)
User report: "vehicle not syncing in africa".

**Evidence (friend's log, 30020, host driving vehicle 25600):**

| Line | Log line | Meaning |
|---|---|---|
| 11703 | `partner_ride=rx; kind=2; seat=0; vehicle=25600` | The host's ride packet arrives |
| 11707 | `partner_ride_seat=byid; ... reason=local_read; remote_cat=0; local_cat=255` | The friend's copy cannot read vehicle 25600 by id |
| 11715 | `partner_lua=event=ride_seat_refused; id=25600; seat=0; reason=no_name` | The Lua seat table has no locator name for 25600 in 30020 |
| 11718 | `partner_ride_tilt=refused; verb=write; id=25600; reason=live` | Tilt writes refused too |
| 11722-11731 | `partner_ride_seat=refused ... reason=lua_refused`, then `unavailable ... block=lua_refused`, then `partner_ride=hide; reason=ride` | The seat chain fails, so the host is hidden while driving |

- The 0.13.6 "visible fallback" (keep the partner visible at the RIDE_FAST pose when the vehicle can't be read) is Mother Base by-id only, so it did not apply in Africa.
- Not checked yet: whether RIDE_FAST moved the friend's copy of the vehicle at all (`ride_fast` lines and the host log), i.e. whether the vehicle itself was standing still or only the driver was hidden.

**Next steps:**
- Add the 30020 vehicle locator names to the Lua seat table (`partner_sync.lua` SEAT / name map), or let 30020 use the by-id path the way Mother Base does.
- Find out why `local_read` fails for 25600 on the friend's copy. Possible causes: the vehicle is not realized there (too far, not spawned), it was Fultoned, or the id differs between the two copies.
- Extend the visible fallback to free roam, so a refused seat never hides the driver.
- Check whether VEHICLE_GONE (`world_vehicle_gone`, default off) is relevant.
- Hidden run: host drives a jeep in 30020; the friend watches.

**Status (2026-10-28, branch `wp/africa-vehicle`; fixture-tested only).** Vehicle 25600 is the host's relief vehicle (TppVehicle2 index 0, chosen in his sortie prep). It existed only on the host's copy: the guest's world follow carries no relief-vehicle key, so the guest's copy never spawned one. The guest's by-id read refused `live` (the pool bit for index 0 clear) for the whole ride. The Lua name map is not the problem: no mission has a locator for index 0.
- Fix 1 (native, `native/partner_driver_seat.inc`): the by-id gate now answers `absent` when the read refuses `live` or `index` (no such vehicle on this copy), before the addendum checks. `SeatFallbackHere` takes the 0.13.6 visible fallback for `absent` in free roam too (Mother Base keeps every fallback reason), at the start and at the id write. The driver stays visible, Warped to his streamed pose (`partner_ride_seat=fallback; ...; reason=absent; show=warp`), instead of `no_name` -> `lua_refused` -> `partner_ride=hide`. Any other free-roam refusal keeps the named path.
- Fix 2 (Lua, `lua/coop/world_ride_pose.lua`): each RIDE_FAST track asks `IsAlive` at most every 500 ms; while it is not true, no tilt write and no SetPosition run, and the frame counts as `absent=N` on the `ride_fast` line. The session's `applied=1157` counted calls into a missing id, not motion.
- Tests: `partner_driver_tests` `TestRideSeatAbsent` (failed on HEAD at its first check), the Mother Base Fulton block now logs `absent`, the gate table's `local_read` case is now a non-pool refusal; `world_ride_pose_lua_check` (failed on HEAD).
- Hidden repro: `tools/worker/scenarios/coop-vehicle-absent-30020.txt` (`-SaveSeed africa-jeep`: the guest's copy loses the Masa Village jeep, then the host drives it). `coop-vehicle-gone.txt` part B now expects `absent` and the fallback, and no ride hide. Not run yet.
- Still open: the vehicle itself does not appear on the guest's screen (the friend sees the host standing at seat height with no vehicle under him). That needs relief-vehicle parity at the sortie (carry the host's sortie-prep vehicle to the guest), a separate branch. Until then, in free roam, drive vehicles found in the world rather than one brought from the sortie prep.
- The horse (host-log item 5) is a different mechanism: the act-based `partner_native_mount=hide` rule (docs/mounts.md), not the seat gate. The partner's mount never exists on the viewer's copy, so showing the rider would mean drawing him floating at saddle height on every horse ride, which reverses the decision in docs/mounts.md section 7. Not changed; see docs/mounts.md "Risks".

## Other things to check
- `downed_control=refused` appears 74× in the friend's log, with `downed_pad=cap` 8×. Find out what is being refused (revive/carry input while downed?) and whether the friend noticed anything.
- `partner_recon=place; reason=far` appears once, a partner teleport.
- `save_handoff=applied_seen` and `disabled` appear: a handoff happened this session (`newest_replaced=20261023-221230-before-handoff`). It looked fine, but confirm with the host log.
- The user may remember more problems. Read the host's own log for the same session (Mother Base and Africa) and compare.

## Fix order for next session
1. Guns in Africa (bug 1).
2. The vehicle in Africa (bug 2), plus the visible fallback in free roam.
3. The host-log pass and the `downed_control` refusals.
4. Ship 0.14.x and run another two-PC test (both PCs must update; the friend is still on 0.13.6).
