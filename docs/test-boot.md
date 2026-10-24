# Developer test boot

Faster hidden-worker game runs: the startup screens skipped, the title taken by
the mod, both copies landed at a chosen spot, named save seeds and light test
copies. Developer only: it never runs in a friend/play package.

## Safety gates

- lab.ini keys `lab_fast_boot` and `lab_boot` (below). `mode=play` forces both
  off and sets `play_refused` bit `0x1000000` (`kPlayRefusedLabBoot`), so the
  supervisor's play-rehearsal gate refuses such a session; the worker scripts
  refuse them with `-Mode play` before anything starts.
- The Lua (`lua/coop/debug_boot.lua`, spliced into TppMain after the debug
  chunks) does nothing until the loader answers its receipt
  `return 'MGS_COOP_BOOT_CFG_V1'`. The loader answers (`mgscoop/lab_boot.hpp`,
  `game_script_observer.cpp` `SelectLabBootReceipt`) only when all of these
  hold: a background-worker build (`worker_scenario::Compiled()`), its owned
  private worker admitted this process (`worker_scenario::Active()`), lab.ini
  present in `mode=lab`, the offline-v1 marker and copied saves
  (`lab::SavesRedirected()`). Everywhere else it answers `return 0`: a
  friend/Release loader (no background worker) never skips a screen or moves a
  player, whatever its lab.ini says.

## lab.ini keys

```
lab_fast_boot=0|1
lab_boot=off
lab_boot=<30010|30020|30050> <x> <y> <z> <yaw> [time=HH:MM] [weather=<name>]
lab_boot=<30010|30020|30050|40010|40020|40050> [time=HH:MM] [weather=<name>]
```

Coordinates in metres (at most 3 decimals, |v| <= 100000), yaw in degrees
(|yaw| <= 360, forward = (sin yaw, cos yaw)), weather
`sunny|cloudy|rainy|sandstorm|foggy` (no fog in 30020: the game has none there).
Without a position the target's default is used: the game's own free-roam
fail-safe starts (30010 `1448.61 337.787 1466.4`, 30020 `-510.73 5.09 1183.02`)
and the Mother Base command deck where MB continues start (`9.0 0.8 -41.1`).
`lab_ini_check lab` prints `lab_fast_boot`, `lab_boot_mission`, `lab_boot_pos`,
`lab_boot_clock_s`, `lab_boot_weather`.

## What happens

Every step is the game's own code, found in its init and title scripts
(extracted read-only, see "Game inputs"):

| Step | How | Log (`script-observer.log`) |
| --- | --- | --- |
| Logos | `init_sequence.StartPreTitleSequence` runs with `TppSave.IsNewGame()` answering true for that one call: the game's own new-game branch deletes the Konami/KJP/Fox splash screens and starts the pre-title at once | `lab_boot=fast; step=logos` |
| Copyright | the engine's `cesa` splash, which `Seq_Demo_WaitCopyRightLogo` waits on, is deleted | `step=copyright` |
| Offline / autosave notice | popups the init (`Seq_Demo_ConfirmAutoSave`, `Seq_Demo_LogInKonamiServer`, `Seq_Demo_ShowDlcErrorNetwork`) and title (`Seq_Game_PushStart`) sequences only wait on are erased (`TppUiCommand.ErasePopup`); a stuck DLC announce moves on as its own message would | `step=popup; seq=...` |
| Press start | `title_sequence.StartPressStartMenu` (called by `OnEndShowSplashScreen`) is replaced: the menu stays hidden (`mvars.title_hideTitleMenuOnInitialize`) and the title goes to `Seq_Game_PushStart` as its PressStart message does | `step=press_start` |
| Title menu | the menu's own `OnSelectContinue` (fast boot) or `OnSelectRestartHeli` (a boot target: the ACC without a load), every 3 s while the menu stays (world_handoff's wrappers still decide for the guest) | `step=continue` / `step=restart_heli` |
| Loading tips | boot loads pass `waitOnLoadingTipsEnd=false` to `TppMission.Load` (no key at the end of a load) | `step=load` |
| Sortie | in the ACC (in game, 2 s settled) the host (or a copy without world follow) calls the lab goto's `SelectNextMissionHeliStartRoute(target, 0, false)` + `OnEndMissionPreparation(DEPLOY_TIME.CURRENT)`; route 0 and no LZ left from the ACC make `ReservePlayerLoadingPosition` start the player ON_FOOT | `lab_boot=place; step=deploy` |
| Guest | with world follow (W3) the guest never sorties itself (`step=follow_host`): it is in its ACC and replays the host's sortie (world_follow kind 6); the world follow hold keeps the host's sortie until both are paired | |
| Spot | after the mission's own `ReservePlayerLoadingPosition` (mission finalize, target mission) `TppPlayer.SetInitialPosition`: the host on the spot, the guest 4 m in front of it facing back, 0.5 m up | `step=initial` |
| Mother Base | at the 30050 allocation `TppDemo.SetNextMBDemo()` clears the queued arrival demo (f30050 MissionPrepare reads it) | `step=mb_demo; cleared=<name>` |
| Arrival | on foot in the target: more than 3 m off the spot is corrected with `TppPlayer.Warp` (at most 3); the host then sets the time (`TppClock.SetTime`) and weather (`TppWeather.ForceRequestWeather`); world sync carries both to the guest | `step=warp`, `step=gameplay; ...; via=spot; err_m=..; x=..; clock=..; weather=..` |

`ms=` is the game clock (`Time.GetRawElapsedTimeSinceStartUp`) in ms, so the
gameplay line's `ms` is the time from the game's start to the boot's end.
`lab_boot=trace; step=seq` lines record every sequence change while the boot
runs. A fast boot without a target ends at the first on-foot gameplay
(`via=continue`) or in the ACC (`via=acc`).

Limits: an ACC target is reached by the title's RestartHeli, so it must be the
save's own ACC (a lab seed saved in Afghanistan titles in 40010; use the real
seed or an MB seed for 40050); another ACC logs `step=refused;
reason=acc_differs` and stays. Audio: the copied saves have no audio setting
(volume lives in the binary TPP_CONFIG_DATA), so nothing is muted.

## Hidden worker

Scenario line (Invoke-WorkerScenario):

```
boot 30020 -510.73 5.09 1183.02 0 time=12:00 weather=sunny
boot fast
```

`boot` adds `labsetting lab_fast_boot=1` and `labsetting lab_boot=<spec>` for
both copies and, at its place in the steps, waits (no input at all, 300 s)
until each copy logs `lab_boot=...; step=gameplay`, then reads each position.
The keys alone work too: `labsetting lab_fast_boot=1`, `labsetting guest
lab_boot=30020`, or `-LabSetting 'lab_boot=30020 1 2 3 90'` (a labsetting line
without a role is both copies; `both` is not a role there).

`scenario.json` now has `Gameplay` (per role: seconds from its own process
creation to the first observed gameplay and how it was seen), `Boot` (per role:
the gameplay line, every boot step with its game-clock ms, the position) and
`Timings` (each step's start and end in seconds after the host's launch).

Scenarios: `scenarios/boot-baseline.txt` (plain title + waitgameplay) and
`scenarios/boot-30020.txt` (the boot to a fixed spot, captures of both).

### Timed hidden-worker proof

| Run | Scenario | Seed | Target | Game clock (`ms=`) | Wall time from launch | Outcome |
| --- | --- | --- | --- | --- | --- | --- |
| `634e5f77` | `boot-baseline.txt` (`-GraphicsQuality preserve`) | `lab` | `30010` (Continue) | — | host `57.2 s`, guest `54.3 s` | Normal Enter-driven title + Continue into Afghanistan (`1448.61, 337.79, 1466.40`). |
| `0e20af9c` | `boot-30020.txt` (`-GraphicsQuality low`) | `lab` | `30020 -510.73 5.09 1183.02 0 time=12:00 weather=sunny` | host `28.88 s`, guest `29.43 s` | host `45.4 s`, guest `45.3 s` | Zero input; skipped logos, autosave/offline popups and Press Start, took `RestartHeli` (`40010`), sortied to Africa (`30020`) via `world_follow` (`kind=6`) and landed at Masa Village (`err_m=0.00` on both copies, 12:00 sunny; `host-boot-spot.bmp` / `guest-boot-spot.bmp` show both players face-to-face with the `Taylor` nametag). |
| `3e5e8243` | `boot 30050 22.61 1.80 -15.55 0 time=12:00 weather=sunny` | `quiet-recruited` | `30050` command deck ladder | host `31.30 s`, guest `31.58 s` | host `42.4 s`, guest `42.9 s` | Skipped the Mother Base arrival demo (`step=mb_demo`) and landed at the red container ladder (`22.61, 0.80, -15.55`, `err_m=0.00`) without the previous 60 s post-gameplay wait. |
| `8d91d358` | `boot 30020 -656.01 1.20 877.73 62 time=12:00 weather=sunny` | `lab` | `30020` Masa Village jeep | host `28.91 s`, guest `29.51 s` | host `47.9 s`, guest `47.6 s` | Landed beside the authored jeep (`-656.01, 1.20, 877.73`, `err_m=0.00`). |
| `c2ecccb9` | `boot 30010 828.18 338.20 1630.51 95 time=12:00 weather=sunny` | `lab` | `30010` step-on rock shelf | host `28.70 s`, guest `28.88 s` | host `44.8 s`, guest `42.0 s` | Landed at the Afghanistan rock shelf (`828.18, 337.20, 1630.51`, `err_m=0.00`). |

### Light test copies

In lab mode `-GraphicsQuality` now defaults to `low` (six Low, SSAO Off,
filtering Medium, only in the run's own copied `TPP_GRAPHICS_CONFIG`, its
integrity tag rewritten); the play rehearsal keeps `preserve`, and
`-GraphicsQuality preserve` restores the old behaviour. Depth of field, motion
blur, volumetric clouds and vsync stay as copied: their off spellings are not
pinned.

## Save-seed library

Named snapshots of a finished run's copied saves, outside every checkout:
`<SeedRoot>\<name>\{host,guest}\{287700\local,remote}` plus `manifest.json`
(format `mgs-coop-worker-seed`, each file's size and SHA-256, a set hash per
role, the source run). SeedRoot is `-SeedRoot`, else `$env:MGS_COOP_SEED_ROOT`,
else `D:\mgs-coop-seeds`.

```
pwsh -File tools/worker/Save-WorkerSeed.ps1 -RunId <run id> -Name africa-jeep
pwsh -File tools/worker/Save-WorkerSeed.ps1 -RunDirectory C:\Users\taylo\mgs-coop-run-archive\test-boot\<id> -Name mb-ladder -Note '...'
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath ... -SaveSeed africa-jeep
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath ... -HostSaveSeed mb-ladder -GuestSaveSeed lab
```

Save-WorkerSeed reads only a closed run's own `<role>-saves` (session.json
`status=closed`), takes the files directly in `287700\local` and `remote`, and
refuses anything under a `userdata` folder or a seed root inside the checkout.
An existing seed is replaced only with `-Force` (the old one moves to
`<SeedRoot>\.replaced\`). Start-WorkerSession verifies the seed against its
manifest (before the probe and again at the copy) and records
`save_seed_kind=named`, `save_seed_name` and the set and manifest hashes.
Seed names: 2..32 of `[a-z0-9-]` starting with a letter, never `real`, `lab`,
`run`, `both` or `real-remote`.

Seeds created in `D:\mgs-coop-seeds` (each captured with `Save-WorkerSeed.ps1` from a closed worker run whose mission-start checkpoint save was written at the target spot):

| Name | Content | Source run |
| --- | --- | --- |
| `mb-ladder` | Mother Base `30050`, command deck by the red container ladder (`22.61, 0.80, -15.55 yaw 0`) | `3e5e82436ab34cf18e60a4b9a0ed20f0` (`boot 30050 22.61 1.80 -15.55 0` from `quiet-recruited`) |
| `africa-jeep` | Africa `30020` next to the Masa Village jeep (`-656.01, 1.20, 877.73 yaw 62`) | `8d91d358a91f4fcf92a1032e2a27e418` (`boot 30020 -656.01 1.20 877.73 62`) |
| `afgh-ledge` | Afghanistan `30010` at the rock shelf / step-on ledge (`828.18, 337.20, 1630.51 yaw 95`) | `c2ecccb9756644bebaa3fe843694fc95` (`boot 30010 828.18 338.20 1630.51 95`) |
| `quiet-recruited` | Real save seed with Quiet on the team (`30050`, story character) | `6c755769e4894693b757abf210fed1ef` (`-SaveSeed real`) |

## Game inputs

`debug_boot_game_lua_check` runs the module against the game's own
`init_sequence.lua` and `title_sequence.lua`, pinned by SHA-256 in
`cmake/lua_checks.cmake` (`78e0781d...` and `1d9eb747...`, both from
`master/0/00.dat`). Extract them read-only (no game file changes) with the
project's tools, then unpack the two fpkd files:

```
MSYS_NO_PATHCONV=1 build/archive_probe.exe -out build/boot-script-inspection-20261023 -paths "/Assets/tpp/pack/mission2/init/init.fpkd,/Assets/tpp/pack/mission2/init/title.fpkd,/Assets/tpp/pack/mission2/common/title_sequence.fpkd"
build/datfpk.exe build/boot-script-inspection-20261023/package-00.fpkd
build/datfpk.exe build/boot-script-inspection-20261023/package-02.fpkd
```

Without them the check is skipped (the CMake status line names it).
