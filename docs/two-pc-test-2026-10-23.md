# Two-PC test, 2026-10-23: problems to fix next session

**Setup.** Both PCs ran 0.13.6, over Steam. The host is the user; the joiner is the friend. Only the friend's logs were analysed (Collect-Logs zip `mgs-coop-logs-join-20261023-222242.zip`, extracted under the session scratchpad `online1023/friend/`). The host's own log for the same session is in `D:/Games/steamapps/common/MGS_TPP/mgs-coop/script-observer.log` (tail) and has not been read yet.

**Route.** Title, then the ACC (40050), Mother Base (30050, three visits), the ACC again, then Africa free roam (30020). The friend's guest session starts at script-observer.log line ~5704; Africa starts at line 10614.

## Status after the 2026-10-28 integration
The three fix branches are merged locally on `dev/oct7-continue`: `wp/africa-guns` `c594e41`, `wp/africa-vehicle` `03dabda` and `wp/host-fixes` `6422519` (docs/STATUS.md, "2026-10-28: Africa fixes and host-log fixes"). They are not packaged yet, so neither PC has them.

"Proven" means that a hidden-worker run showed the fixed behaviour. Those runs are loopback (both copies on one PC, copied lab saves). Nothing below has been run over Steam between two PCs yet.

| Item | Status | Commits and runs |
|---|---|---|
| Bug 1 (host-log item 1): the host's guns missing on the friend's screen in Africa | **Fixed.** The 30020 bind is proven; the three dirty states behind it (F1-F3) are proven by fixtures only | `beccaa7`; runs `96b5c909`, `8e97cb60`, `05f68a20` (30020), `04b2f3af` (30050 regression) |
| Bug 2 (host-log item 5, jeep): the host hidden while driving in Africa | **Fixed and proven for the driver**, who now stays visible (`reason=absent` fallback). **Open:** the host's sortie-prep vehicle itself still does not exist on the guest's copy | `a48a674`, `35d5519`; run `31adc898` (also `436a440c`, `011a03f2`, `7561ab63`) |
| Host-log item 5 (horse): the rider hidden while on a horse | **Open by design.** The horse was debug-spawned on the host's copy only (docs/mounts.md "Risks") | none |
| Host-log item 2: no guest guns on the host for MB visit 1 after the save handoff | **Fixed.** The late-manifest receipt is proven; the predicted preload is unit-tested only | `09ebf71`; run `c07b8136` |
| Host-log item 3: the guest's shots and reloads stop on the host after a box bump | **Fixed and proven.** The bump changes only item fields (benign), and the strip now ends with the mission | `f492d77`, `3fd064a`; runs `5ceb4985`, `6041f5e5`, `0c8f1ab0` |
| Host-log item 4: friendly fire on a boxed host switched off his downed state | **Fixed and proven** for the session's order (downed, then boxed). Boxed before the shots is unit-tested only | `2f3332b`; runs `ce66a0bf`, `44e5a233`, `a5f30e31` |
| Host-log item 6: the guest's downed protection off while body 1 is unrealized | **Fixed and proven** | `08f73d6`; run `20d41b16` |
| Host-log item 7: two game overs in MB | **Not a bug as far as the logs show.** The friend fell into the sea from a ledge hang. Ask him whether a climb-up input was eaten | none |
| Host-log item 8: minor | **Open, not worked on:** a 1.1-1.5 s freeze when the partner first appears in MB; correction warps (`place; reason=far`, `snap far_idle`); in MB visit 2 the host's guns bound only 3 s before game over #2 | none |
| `downed_control=refused` 74× (friend) | **Explained.** Harmless, except the 15× during the horse ride (item 6, fixed) | see "Host-log pass" |
| `downed_pad=cap` 8× | **Works as designed** | see "Host-log pass" |
| `partner_recon=place; reason=far` | **Side effect of bug 1.** A gunless body cannot aim-walk, so it drifts, and recon warps it back | see "Host-log pass" |
| Save handoff | **Correct on both sides** | see "Host-log pass" |

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

**Status (2026-10-28, branch `wp/africa-guns`; fixture-tested, then hidden-worker runs in 30020 and 30050, see below).** The cause is three readiness rules in the generalized writer that treated natively empty states as occupied. None of them is specific to 30020. Africa hits them because the first bind comes late there.
- The released target's D0 flag bits (`selected_bank_aux_not_empty`): the release never clears the byte, and the load zeroes it first.
- The support writer's arm and grenades in slots 6 and 3 (`other_slot_unexpected`).
- The native cleared effect record (id 0x7FF) after the horse-ride unrealize (`remote_slot_not_empty`).

The host's own log of the same session shows the same aux wait in 30020. The friend's guns bound there only after one backoff.

The writer now accepts all three states (`docs/native-puppet.md`, "Generalized writer", F1-F3), and each wait names its field. New receipts: `equip_writer=aux` and `equip_writer=remote_slot`.

**Hidden-worker runs (2026-10-28, `wp/africa-guns` from `2344443` to `fa127d3`; only the scenarios changed in between; `-PartnerProfile native -NativeStage gun -BuildProfile research`).** These are the first hidden runs that exercise the gun writer in 30020.
- 30020, `partner-native-africa-guns.txt` with `-SaveSeed africa-jeep`: runs `96b5c909`, `8e97cb60` and `05f68a20`. In all three, both roles log `equip_writer=handle_bound` for slot 2 (9) and then slot 1 (46) at gen 2, epoch 0, with no backoff. Each slot first logs an `equip_writer=aux` line. Both roles log `partner_weapon=match` with `strip=0x0`, and there are no `selected_bank_aux_*`, `other_slot_unexpected`, `remote_slot_*` or `delayed_free_target_*` lines. In every capture on both roles, body 1 holds 46 (`mismatch=false`, `strip=0`). While the other role aims, the capture shows `bit18=1`, an active node and `aim_match=true`. Frames from `05f68a20`: on the guest's screen, the host's partner aims the rifle beside the jeep. On the host's screen, the guest's partner aims away from the camera, with the barrel visible.
- 30050, `partner-native-mb-guns.txt` with `-SaveSeed mb-ladder`: run `04b2f3af`, a regression check. Both roles bind slots 2 and 1, log `partner_weapon=match` with `strip=0x0`, and show the same capture state. Slot 0 refuses `stock_optional_header`, as before.
- Every run reported Closed, BaselineRestored and RealSavesUnchanged. `04b2f3af` and `05f68a20` also reported `Forced=true`: the supervisor terminated the job after WM_CLOSE. `8e97cb60` logged the known teardown `crash_probe=av` after `partner_shutdown=hooks_off`.
- Limit: the field's dirty head was not reproduced. Every `aux` line read `f00`, because the guns bind before any actor releases a resource. So F1, F2 (no support item was equipped before the bind) and F3 (no full unrealize) are still proven only by fixtures. The live runs show that the fix keeps the normal bind working in 30020 and 30050.

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

**Status (2026-10-28, branch `wp/africa-vehicle`; hidden-run proven, not two-PC tested).** Vehicle 25600 is the host's relief vehicle (TppVehicle2 index 0, chosen in his sortie prep). It existed only on the host's copy: the guest's world follow carries no relief-vehicle key, so the guest's copy never spawned one. The guest's by-id read refused `live` (the pool bit for index 0 clear) for the whole ride. The Lua name map is not the problem: no mission has a locator for index 0.
- Fix 1 (native, `native/partner_driver_seat.inc`): the by-id gate now answers `absent` when the read refuses `live` or `index` (no such vehicle on this copy), before the addendum checks. `SeatFallbackHere` takes the 0.13.6 visible fallback for `absent` in free roam too (Mother Base keeps every fallback reason), at the start and at the id write. The driver stays visible, Warped to his streamed pose (`partner_ride_seat=fallback; ...; reason=absent; show=warp`), instead of `no_name` -> `lua_refused` -> `partner_ride=hide`. Any other free-roam refusal keeps the named path.
- Fix 2 (Lua, `lua/coop/world_ride_pose.lua`): each RIDE_FAST track asks `IsAlive` at most every 500 ms; while it is not true and `GetPosition` does not answer either (no instance on this copy), no tilt write and no SetPosition run, and the frame counts as `absent=N` on the `ride_fast` line. A wreck (still in the pool: `GetPosition` answers) is placed as before. The session's `applied=1157` counted calls into a missing id, not motion.
- Tests: `partner_driver_tests` `TestRideSeatAbsent` (failed on HEAD at its first check), the Mother Base Fulton block now logs `absent`, the gate table's `local_read` case is now a non-pool refusal; `world_ride_pose_lua_check` (failed on HEAD).
- Hidden repro: `tools/worker/scenarios/coop-vehicle-absent-30020.txt` (`-SaveSeed africa-jeep`: the guest's copy loses the Masa Village jeep, then the host drives it). `coop-vehicle-gone.txt` part B now expects `absent` and the fallback, and no ride hide (not re-run). Hidden run `31adc898` (HEAD `35d5519`) proved both halves in 30020: with the jeep on both copies the guest seats the host by name (`veh_lv_0000`, `confirmed`, `absent=0`); with the jeep removed on the guest's copy only the guest logs `byid; reason=absent`, `fallback; reason=absent; show=warp`, `ride_fast; state=absent; absent=N` with no SetPosition, `exit; via=fallback`, never a ride hide, and its frames show the host visible and moving with no jeep under him (docs/STATUS.md 2026-10-28 has the run list, including `7561ab63`: a jeep wrecked on one copy only, which `35d5519` now keeps placing).
- Still open: the vehicle itself does not appear on the guest's screen (the friend sees the host standing at seat height with no vehicle under him). That needs relief-vehicle parity at the sortie (carry the host's sortie-prep vehicle to the guest), a separate branch. Until then, in free roam, drive vehicles found in the world rather than one brought from the sortie prep.
- The horse (host-log item 5) is a different mechanism: the act-based `partner_native_mount=hide` rule (docs/mounts.md), not the seat gate. The partner's mount never exists on the viewer's copy, so showing the rider would mean drawing him floating at saddle height on every horse ride, which reverses the decision in docs/mounts.md section 7. Not changed; see docs/mounts.md "Risks".

## Host-log pass (the host's own log of the same session)
Source: the host's `script-observer.log`, the 0.13.6 session from absolute line 1363950 to 1377207. In the item text, `hN` is a line of that slice (absolute line N + 1363949) and `gN` is a line of the friend's log. The link was good: relay route, rtt 134-147 ms, no send failures. It was lost only twice: once for 3 s while the guest was loading, and once for the planned handoff relaunch. The item numbers are the ones the fix branches, the scenarios and docs/STATUS.md use.

1. **The guest sees the host with no gun in Africa (bug 1).** The host's copy waited on the same `selected_bank_aux_not_empty` in 30020, then bound both of the guest's guns 15 s later (h7429, h7438). The guest's copy never cleared it. **Fixed** (bug 1 status above).
2. **The host sees the guest with no gun for all of MB visit 1 (about 46 s).** The host pressed Continue while the relaunched guest was still at its title. The allocation preload had no manifest (`equip_preload=refused; reason=no_manifest`, h365), and the manifest that arrived 20 s later could not load anything (`backoff no_resident` ×6, then `refused`). Visits 2 and 3 bound fine. **Fixed, `09ebf71`.** After a handoff the host predicts the peer's first loadout from its own manifest (`source=predicted`), and a late manifest logs `equip_preload=late_manifest; ...; guns=this_load|next_load` once.
3. **The host stops seeing the guest's shots and reloads for the rest of the process (from h10252).** A box-on-box bump inside body 1's job changed the host's ammo digest, which logged `ammo_leak` and `fire_reload_off; scope=process`. **Fixed, `f492d77` and `3fd064a`.** `partner_input=vars_change` now names the changed fields. A change confined to the viewer's own item fields is benign, and so is the support lane's item-only job. The play strip lasts only for its mission.
4. **Friendly fire on a boxed host switched off his downed state for the rest of the mission.** The prone request cannot take a boxed body. The 6 s timeout latched `failed` (34× `controls_timeout`), and three tranquilizer darts were ignored (`downed_unavailable`). **Fixed, `2f3332b`.** The box is accepted as a downed posture, and a timeout clears the cycle without latching.
5. **The host is invisible on the guest's screen while driving and while on the horse.** For the jeep, the vehicle was the host's relief vehicle, which did not exist on the guest's copy (bug 2). The horse was spawned from the F9 debug menu on the host's copy only, and the `partner_native_mount=hide` rule hid the rider. **The jeep is fixed (bug 2); the horse is unchanged by design.**
6. **The guest's own downed protection is off while body 1 is unrealized.** During the host's horse ride, `Live()` required both bodies realized (`downed_control=refused; reason=player` 15×, g16628-16758). **Fixed, `08f73d6`:** the local owner is judged from body 0.
7. **Two game overs in MB, probably not a bug.** Both were the guest's environment deaths: a ledge hang, then a fall into the sea.
8. **Minor.**
   - A one-time freeze when the partner first appears in MB: the partner tick took 1.1 s on the host and 1.48 s on the guest.
   - Visible correction warps of the guest's body.
   - In MB visit 2 the host's guns were planned only 3 s before game over #2.
   - Open.

## Other things to check
- **Done (explained, harmless).** `downed_control=refused` appears 74× in the friend's log, with `downed_pad=cap` 8×. Find out what is being refused (revive/carry input while downed?) and whether the friend noticed anything.
  - 72× `reason=player`: no live local player with both bodies realized. This covers the Africa helicopter insertion (47×), load and game-over screens, and the host's horse ride (15×, item 6, fixed).
  - 2× `context`: the ACC is not a supported mission, and shutdown.
  - `downed_pad=cap`: while the friend slept from the host's dart, his stick was capped, so a sleeping body cannot walk. 8 is the sample-log cap, not the number of caps.
- **Done (side effect of bug 1).** `partner_recon=place; reason=far` appears once, a partner teleport. The gunless body could not follow the host's aim-walk, and recon froze on `remote_act` until the error reached 10.1 m.
- **Done (correct).** `save_handoff=applied_seen` and `disabled` appear: a handoff happened this session (`newest_replaced=20261023-221230-before-handoff`). It looked fine, but confirm with the host log. The host's offer, `want_rx`, `staged`, `exiting` and reconnect match the guest's chain. The relaunch with handoff off is by design (one handoff per session).
- **Done.** The user may remember more problems. Read the host's own log for the same session (Mother Base and Africa) and compare. See "Host-log pass" above. Still to ask the user and the friend:
  - Was the friend trying to climb up when he fell (item 7)?
  - Anything else they noticed?

## Fix order for next session
1. **Done.** Guns in Africa (bug 1): merged 2026-10-28.
2. **Done for the driver.** The vehicle in Africa (bug 2), plus the visible fallback in free roam: merged 2026-10-28. Relief-vehicle parity at the sortie is still open.
3. **Done.** The host-log pass and the `downed_control` refusals: items 2, 3, 4 and 6 fixed and merged 2026-10-28.
4. **Next.** Ship 0.14.x and run another two-PC test. Both PCs must update; the friend is still on 0.13.6.

## What the next two-PC test must check
- **Africa guns (bug 1).** In 30020 each copy logs `equip_writer=handle_bound` for slots 1 and 2, then `partner_weapon=match` with `strip=0x0`. Watch the new `equip_writer=aux` / `remote_slot` lines for a dirty head (F1-F3 seen live), and `refused` never appears. Bind after a support item is equipped, and after a horse ride.
- **Africa vehicle (bug 2).**
  - With a sortie-prep vehicle, the guest logs `partner_ride_seat=fallback; ...; reason=absent` and sees the host at seat height (no vehicle, a known limit).
  - With a jeep found in the world, the named seat confirms (`ride_seat ... confirmed`).
  - Never `partner_ride=hide; reason=ride`.
- **Item 2.** Run a handoff, then the host continues before the guest links. Look for `equip_preload=...; source=predicted` and the guest's guns in the first MB visit. This is the first real test of the predicted preload.
- **Item 3.** A box-on-box bump logs `partner_input=vars_change; verdict=benign`, with no `fire_reload_off` and no `stock_leak_mission_latched`. Look for any `verdict=leak` fields.
- **Item 4.** Friendly fire on a boxed host, with real latency, both orders: down then box, and box then down. Expect `posture=box`, no `controls_timeout` latch, and a revive from the box.
- **Item 6.** During a partner horse ride, the viewer's `downed_status` stays `ready=1; armed=1`.
- **Item 8.** Note the first-appearance freeze, and any warps.
