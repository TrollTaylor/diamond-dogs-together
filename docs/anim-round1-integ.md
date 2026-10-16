# Animation round 1 integration (Cursor finishing Claude's tracks)

Coordinator: the Cursor "camera agent", 2026-10-14 night. Claude Code quota was exhausted
(resets ~02:00 America/Chicago). This file is the durable record on `wp/anim-integ`.

## Do not undo

- Do **not** reset, rebase or re-create `wp/anim-integ` (`D:/mgs-coop-wt/anim`).
- Do **not** rewrite or force-push `wp/anim-seat` (50780ea), `wp/anim-weapons` (862ab8b),
  `wp/anim-actions` (ddb4f7d). Merging those branches again is a no-op.
- Do **not** touch the camera-agent files on `dev/oct7-continue`: `coop_carry*`,
  `teammate_targeting*`, `native_ui*`, `lua/coop/world_revive_input.lua`,
  `native/debug_overlay.cpp` sleeper greyscale, `RequestToPlayCameraNonAnimation`,
  P+C0 setter `0x11F6520`.
- Do **not** flip `kPartnerNativeRideDriven` from `hide` to `seat` until a hidden
  capture shows the remote body **seated in the jeep** (not just log confirmation).
- Do **not** merge `wp/anim-integ` into `dev/oct7-continue` or package/install until
  that visual proof exists. Seat remains `labsetting partner_native_ride=seat` only.

## What landed on wp/anim-integ

| Commit | What |
|---|---|
| 20f5ac3 | fast-forward to camera-agent `dev/oct7-continue` (carried camera, partner-prompt filter, dark sRGB greyscale sleep view) |
| 7a99335 | merge `wp/anim-seat` |
| 3b7186a | merge `wp/anim-weapons` |
| dc66bbf | merge `wp/anim-actions` |
| (integ: commits after this) | scenario camera fix; any later hidden-test fixes |

Conflict resolution was additive (allow-list regexes, LabCommon keys, tests, Settings).
`tools/generate_partner_contract.py` keeps both `ride_seat_assertions()` and
`anim_round1_assertions()`. Regenerated `partner_player_contract.hpp`: 568 sites, 113 slots.

Seat x overlay: `OverlayBlocked` returns `"seat"` while `m.ride_seated`; `TickOverlay` still
runs when `!vehicle_attached` so an active overlay winds down; `TickDm` stands down on
`body_attached = vehicle_attached \|\| ride_seated`; seat release waits while
`pdo::Active(m.overlay)`.

Full ReleaseGun CTest under the verify lock: **267/267**.

Worker note: `Invoke-WorkerChecks` refuses `MGS_GAME_LUA_DIR` pointing at the **main
checkout** (`Unaudited patch input: bootstrap_patch_check`). Keep extracted Lua inputs
under `D:/mgs-coop-wt/anim/build`. Copy from the main checkout's `build/` if a fresh
cache is needed; do not point the env at `C:/Users/taylo/OneDrive/Documents/ChatGPT/mgs-coop`.

## Hidden seat run b8102e6e (inconclusive visuals)

Command: `Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/coop-vehicle-seat.txt
-PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab` from this worktree.

Closed / BaselineRestored / RealSavesUnchanged. Frames:
`D:/mgs-coop-wt/anim/build/worker-runtime/b8102e6eced44c6c93e8778d84cb62ba`
and PNGs in `C:/Users/taylo/AppData/Local/Temp/mgs-caps/seat1/`.

**Logs (guest, first half) passed the pipeline:** seat patch applied, Lua
`event=ride_seat; name=veh_lv_0000; state=stateVehicleFrontLeftLoop`, confirmed
`0x489157d7d94f`, status lines with `err_mm` 58→2, exit foot, patches reverted.
Host `vehicle_seats`: `id=25608; riders=0,9728,65535,65535; localSeat=1` (host is
driver; 9728 occupied the passenger slot on the host copy). `vehicle_names`
`local=25608; seat=0`.

**Frames did not prove a seated body.** `placebehind guest host 14 5` *before* enter
put the guest at `(592, 323, 1160)` yaw -102 while the host jeep was at
`(577, 320, 1157)` yaw -83 — around a Wakh Sind alley corner looking at a shutter.
`godmode 0` left the watcher mortal; combat dropped the guest. Reverse half:
`guest vehicle_seats -> vehicle_not_riding`. Host reverse frames: prone, Guest nametag,
empty jeep.

So: **the seat flow ran; the camera was not on the jeep; do not treat this as a visual
pass or fail of the seated mesh.**

## Scenario fix

`coop-vehicle-seat.txt` and `coop-vehicle-seat-manual.txt`:

- `godmode 1`
- **both** `goto_vehicle` first (partner pose is invalid across the spawn-to-jeep
  gap; run after 2a9c264 threw `viewer landing starts without a valid partner pose`)
- `placebehind <watcher> <driver> 6 0` **before** board, while the driver is on foot
- do **not** `placebehind` after the driver is seated (run `76dea383`: seat mode owns
  body 1, same landing throw). A follow-cam would need a landing check that only
  uses the viewer's local position
- `require vehicle;id=` so a missed board stops the scenario
- reverse half starts with `heal` + `godmode 1`

Re-run the auto scenario with the same Invoke-WorkerScenario line. Judge from the
**guest** `seat-entered` / `seat-drive-*` frames: Snake seated in the jeep, nametag
above the seat, body following while it turns. Host `seat-rev-entered` must show Quiet
seated. If those frames pass, then flip `kPartnerNativeRideDriven` (one line in
`native/include/mgscoop/lab_config.hpp` plus the assertion in `tests/lab_config_tests.cpp`).

## Hidden seat run 8b8e5bbc (logs pass, visuals still not a default-flip)

Same Invoke-WorkerScenario line, scenario HEAD `5d50e45`. Closed / BaselineRestored /
RealSavesUnchanged. PNGs: `C:/Users/taylo/AppData/Local/Temp/mgs-caps/seat2/`.
Run folder: `D:/mgs-coop-wt/anim/build/worker-runtime/8b8e5bbc2acc4b2a967da44aa1aaab6f`.

Guest first half (and host reverse) again confirmed `veh_lv_0000` / 25608 / state
`0x489157d7d94f` / `err_mm` ~1..370 / `cam_ok=1` / exit foot + revert. Reverse guest
boarded (`vehicle;id=25608; localSeat=1`).

Guest `seat-entered` / drive / turn frames: watcher 6 m behind a driver whose yaw
(-83) faced a building, so the camera is a wall with Taylor's nametag ~9 m through
it. The jeep is on the other side. **Not a seated-mesh proof.**

Host `seat-rev-entered` / `seat-rev-drive-04`: host prone in the grass with a **clear
view of the jeep**. A bald Snake-looking body sits in the vehicle; the Guest nametag
floats beside it, not locked to the mesh. Guest is configured Quiet, so this may be
the partner with the wrong parts, or rider `9728` (passenger slot occupied on **both**
the host-driven and guest-driven census: `riders=0,9728,65535,65535`). **Do not treat
this as Quiet-seated proof. Do not flip the seat default.**

Next visual attempt should not use driver yaw for the watcher. Need a view from the
plaza side of `veh_lv_0000` (host jeep ~577, 320, 1157) that is not through a wall,
and a landing path that can follow after seat owns body 1. Identify 9728.

## Seat visual proof (Claude, 2026-10-15) and the default flip

**Run 8b8e5bbc, reconsidered.** The guest in these lab saves is not Quiet: `coop.ini
character=quiet` falls back to the bald DD soldier, and the guest's own frames show that body
(`guest-seat-entered`: the bald soldier from behind; `guest-seat-rev-entered`: the bald soldier
driving the jeep, D-Dog in the passenger seat). So the bald body in the host's
`seat-rev-entered` / `seat-rev-drive-04` frames **is the guest's partner body**, seated in the
jeep's driver seat with its hands on the wheel, and still seated after the jeep moved
(zoomed crops in `scratchpad/anim/host-seat-rev-*-zoom.png`). That is the reverse-direction
proof (the host sees the guest driving).

**Rider 9728 is D-Dog.** `0x2600` = `TppBuddyDog2` (`lua/coop/world_buddy.lua:84-86`,
`native/buddy_pose.cpp:15`). The lab saves have D-Dog deployed, and the stock buddy jumps into
the jeep's passenger seat (native seat 1) whenever its owner drives, on that owner's copy only.
`riders=0,9728,65535,65535; localSeat=1` = the driver plus D-Dog.

**Run 77d24c5e (plaza watcher).** `coop-vehicle-seat.txt` now teleports the guest to the Wakh
Sind plaza `(585.0, 323.1, 1176.9)` facing -90 (toward -x, the placebehind convention) *after*
the host boards, with a plain `debug teleport` (no `Wait-ViewerLanding`, which needs a partner
pose that seat mode owns). Closed / BaselineRestored / RealSavesUnchanged. Frames
(PNG copies in `C:/Users/taylo/AppData/Local/Temp/mgs-caps/seat3/`):
- `guest-seat-drive-a-08`: the jeep arriving from the alley with **Snake (the host, "Taylor")
  seated in the driver seat**, hands on the wheel, motion-blurred while moving;
- `guest-seat-plaza-parked`: the same, parked, seated pose sharp;
- `guest-seat-exiting-04` / `guest-seat-exited`: Snake climbs out of the driver seat, then
  stands beside the jeep.

The reverse half stopped at `guest require vehicle;id=` (`vehicle_not_riding`): guards went
into combat ("Soviet CP: Entering combat status") and knocked the guest watcher down despite
`godmode 1` (`lethal_unprotected`), so it could not board. That is a test-setup failure, not a
seat failure; 8b8e5bbc already covers that direction.

**Default flipped.** `kPartnerNativeRideDriven = PartnerNativeRide::seat`
(`native/include/mgscoop/lab_config.hpp`, assertion in `tests/lab_config_tests.cpp`). Still not
runtime-tested: T5 (the viewer driving its own vehicle while the partner's seat DM enters and
ends; the skips are designed for it), seat 1/2/3 (passengers), trucks, Africa (30020) names,
and get-in/get-off chaining (`partner_ride_seat_enter=getin`, `partner_ride_seat_exit=getoff`).

## Weapons and actions hidden tests (Claude, 2026-10-15)

All runs: `-PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab`, Closed /
BaselineRestored / RealSavesUnchanged, no ExpectFailures. Per-run log slices were cut with
`scratchpad/anim/runslice.py` (the game-folder `script-observer.log`, by the role pids in
`<run>/{host,guest}.state.json`).

- **Fire census `204b5d54`** (`partner-fire-census.txt`). Guest: `partner_weapon=unsupported;
  remote_slot=3; remote_equip=354; type=12; reason=slot; ...; strip=0x58` for the grenade, the same for
  the magazine 349 and for the Hand of Jehuty 516 (`remote_slot=6; type=14`), `unsupported_end` back on
  the rifle; FIRE live again (`partner_fire_census=bullet; owner=0x1; ...; dropped=1`). Host census: the
  grenade throw went through the **thrown** slot (vt+8 -> `0x15E0230`) 59 logged / 113 counted calls,
  `owner=0x0; id=354; aux=1`; no thrown/placed/projectile line with `owner=0x1` anywhere. Support sub 1
  was the magazine, so `placed` (C4) and the launcher slots were not exercised. The frames were useless
  (the standing watcher 5 m behind hid the host); the scenario now lays the watcher prone 7 m back.
- **Weapons `6163920a`** (`partner-native-weapons.txt`, defaults). The 3E8 shadow is rewritten on every
  switch and never exceeds the capacity (`partner_reload=shadow; reason=slot; ...; mag=10; cap=11;
  m3e8=10`; the handgun reload completes at 11/11, the old 41/11 is gone). Auto-fire stop:
  `partner_fire=dry_stop; remote_mag=0; ...; reason=remote_empty` then `dry_clear; ...; reason=reload`.
  Aim intent: `partner_aim_intent=on; source=act; remote_act=11; ...; pad_hold=0` with `added_act`
  growing. Reload length stayed 1.5x (3018 vs 2019 ms; handgun 1854 vs 1252) with the rate off.
- **Weapons + `partner_native_reload_rate=adapt` `8ca5da4a`**: body/remote reload ms 3018/2022 ->
  2285/2018 -> 2082/2034 -> 2051/2034 -> 2035/2018, handgun 1242/1223; `k_milli` 1000 -> 1500, writes
  only while mode 11/12, `refused=0; faulted=0`, no `latched`. **adapt is now the gun-profile default**
  (`ResolveWeaponDefaults`; `partner_native_reload_rate_given` keeps a named key authoritative).
- **Traversal census `5b7221a6`** (`partner-traversal-census.txt`). `partner_act` edges: crouch 8/8,
  prone 6/6, dive 3/3, stand-move 3/3, fall/land matched at the edge; one `stance_fix` (stand -> prone,
  `after_ms=1853`, after a `stance_wait; reason=state`), two misses (one crouch, one crawl).
  `partner_pad_dropped; action=1` after the E presses. No vault/ledge/ladder spot exists in the scenario,
  so those act ids are still unverified.
- **Overlay states `873d7197`** (`partner-overlay-states.txt`, `partner_native_overlay=debug`; the first
  run `cf395755` was occluded the same way and the watcher is now prone 7 m back). Every request played
  (body act 139) and resumed native without a fallback latch. Frames
  (`C:/Users/taylo/AppData/Local/Temp/mgs-caps/overlay2/zoom.png`): `stateDamageBigFront` a real
  stagger then recovery; `stateEludeIdle` the ledge-hang arms-up pose; `stateClimbIdle` /
  `stateClimbMoveFront` ladder poses; the clip `snapdam_p_die_idl` lying on the ground;
  `stateCBoxIdle`/`Move` a crouch with no box model (the item is not equipped on body 1);
  **`stateDead` shows nothing** (standing idle through its 2 s) - map death to the clip, not the state.
  `code_match=0` at the 16 ms `playing` sample is a sampling-time artifact, not a refusal. The overlay
  default stays **off** (user decision).

## Not run

- `coop-vehicle-seat-manual.txt` (T1/T4/T5 seat_dm)
- `partner-fire-census.txt` / weapons hidden tests (`docs/anim-round1-weapons.md`)
- overlay/traversal action tests (`docs/anim-round1-actions.md`)
- adversarial review of the three tracks was only the merge-time read (overlay/seat
  ownership looks consistent; no extra defect found). Claude's two review agents
  were cut off by quota with no findings.

## Camera-agent work already on 20f5ac3 (do not redo)

- Carried camera: `Player.RequestToPlayCameraNonAnimation` follow instance 1,
  `followTime=600`, renew 590 s (`lua/coop/world_revive_input.lua`).
- Slept-ally Fulton + struggle (hints 93/94) filtered by `OwnedPartnerPrompt`;
  `PoseSleepHint` forwards through `FilterPartnerPrompt`; worker builds leave vt+268.
- Sleep view: full-screen DX11 greyscale from ImGui callback, **sRGB** darken
  (linear pass washed the sky). Fallback `IM_COL32(8,8,10,170)`.
- Installed friend package on the camera branch: `mgs-coop-friend-0.12.6-20261014T235803Z-73f514f3`.
  PR: https://github.com/TrollTaylor/diamond-dogs-together/pull/1

## Next for Claude (after quota)

1. Read this file and `scratchpad/anim/HANDOFF-FROM-CURSOR.md`.
2. If a newer seat run exists under `build/worker-runtime/`, inspect those frames first.
3. If still no seated-body proof, run the fixed `coop-vehicle-seat.txt` (coordinator /
   hidden worker only).
4. Only then flip the seat default, merge to `dev/oct7-continue`, package.
5. Weapons/actions hidden tests remain; overlay default stays off.
6. Unassigned (not this round): vehicle slope tilt, horse loading, shared riding,
   upright/guard sleep, reverse-role carried camera.
