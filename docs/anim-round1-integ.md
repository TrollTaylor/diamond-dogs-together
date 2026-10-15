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
