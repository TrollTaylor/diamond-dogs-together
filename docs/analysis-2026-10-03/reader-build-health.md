## Build and test health: mgs-coop working tree (checked 2026-10-03)

### 1. Running tools/build.ps1 -Configuration Release
- **Result:** EXIT 0. The build was incremental: CMake reconfigured in 0.4 s, then ninja printed `no work to do`, so build/native was already up to date with the sources. Total time was 4 s.
- **Warnings:** the only one is a CMake deprecation from `third_party/minhook/CMakeLists.txt:27` (`cmake_minimum_required` below 3.10).
- **CTest:** 21/21 passed, total test time 3.50 s.

| # | Suite | Result |
|---|---|---|
| 1 | session_release_tests | PASS (147 checks) |
| 2 | mission_termination_tests | PASS (342 checks) |
| 3 | client_session_request_tests | PASS |
| 4 | heap_trial_tests | PASS |
| 5 | session_tests | PASS |
| 6 | import_table_tests | PASS |
| 7 | steam_runtime_tests | PASS |
| 8 | steam_lifecycle_tests | PASS |
| 9 | steam_getter_tests | PASS |
| 10 | typed_payload_tests | PASS |
| 11 | player_sync_tests | PASS (23 checks) |
| 12 | input_receiver_tests | PASS |
| 13 | steam_packet_tests | PASS |
| 14 | four_player_lab | PASS (prints "IN-GAME FOUR-PLAYER SUPPORT: UNVERIFIED") |
| 15 | peer_router_tests | PASS |
| 16 | allocation_bridge_tests | PASS |
| 17 | script_load_bridge_tests | PASS |
| 18 | player_ghost_tests | PASS |
| 19 | win_import_patch_tests | PASS |
| 20 | probe_forwarding | PASS |
| 21 | probe_install_recovery | PASS (uses build/probe-install-tests-<guid>, cleaned up afterwards) |

### 2. Clean rebuild in a scratch directory
I re-ran the same toolchain setup into my own temp folder, not the repo, to get compile warnings and a fresh binary.
- **Toolchain:** VS 18 BuildTools, MSVC 14.50.35717 (cl 19.50.35727), SDK 10.0.28000.0, CMake 4.3.2.
- **Result:** 110/110 ninja steps in 4.07 s, then CTest 21/21 passed in 2.98 s. The whole run (configure, build, test) took 11 s.
- **Warnings:** 0 compiler or linker warnings. 53 translation units compile with `/W4 /WX`, including the probe DLL. The minhook C sources do not.
- **DLL identity:** the clean `dinput8.dll` is 367616 bytes, the same size as the installed file. Its SHA-256 is 22f079a5..., but `cmp -l` against the installed DLL shows only **6 differing bytes**: offsets 0x110-0x112 (PE TimeDateStamp) and 0x50754-0x50756 (debug-directory timestamp).
- **Conclusion:** the installed 0.10.33 is functionally identical to the current uncommitted source. Lua is embedded via `file(READ)` at configure time, so this covers the Lua modules too.

### 3. Hash comparison
| File | SHA-256 | Notes |
|---|---|---|
| build/native/dinput8.dll (Sep 12 12:58) | FCB0E16A5AF1EE3E1884C7EB14D2EADFD33AC83280D7C0104EA2190EA564F9B6 | |
| D:/Games/steamapps/common/MGS_TPP/dinput8.dll | FCB0E16A5AF1EE3E1884C7EB14D2EADFD33AC83280D7C0104EA2190EA564F9B6 | **match**; manifest version 0.10.33, rollback_id 425ee3c5... |
| D:/Games/steamapps/common/MGS_TPP_guest/dinput8.dll | 6B48415471028AF6CE9B3D20BE8F75A1FC7AA2494DD1BB9AB0E37173157FEDDC | **stale 0.10.27** (Sep 11); its manifest game_directory points at the HOST folder |
| mgs-coop-player-runtime.dat (host and guest) | 26CE7C21...2F54 | identical |
| steam_api64.dll (host and guest) | 08BD0152...C60A | original Steam DLL in both; guest has no Goldberg and no steam_settings folder |

- The guest mgsvtpp.exe is a hardlink (link count 2).
- The guest's master, mgo and CustomSoundtrack folders are symlinks to the host folder.

### 4. Lua fixture and manual checks
`build/lua-fixture/lua51.exe` exists (Lua 5.1.5). All outputs went to the scratchpad; nothing in the repo or game folders was overwritten.

| Check | Result |
|---|---|
| `host_trial_patch_check.exe` (player-allocation package-00.lua → scratch output) | PASS, 20 checks + 400 concurrent selections; output **byte-identical to build/host-trial-0.10.33.lua** |
| `lua51 tests/host_trial_lua_check.lua` | PASS, 77 checks |
| `lua51 tests/session_lifecycle_lua_check.lua lua/coop/session_lifecycle.lua` | PASS, 91 checks |
| `lua51 tests/client_join_lua_check.lua lua/coop/client_join.lua` | PASS, 589 checks |
| `lua51 tests/mission_trace_lua_check.lua lua/coop/mission_trace.lua <TppMain>` | PASS, 213 checks |
| `mission_trace_patch_check.exe` | PASS, 15 checks |
| `bootstrap_patch_check.exe` (3-arg form) | PASS, 23 checks; outputs equal Tpp-bootstrap-08.lua and Tpp-archive-08.lua |
| `bootstrap_lua_check.lua` (documented 2-arg form) | PASS |
| `bootstrap_lua_check.lua` (4-arg form with lua/coop/archive_probe.lua) | PASS |
| `player_package_patch_check.exe` (TppPackList package-00.lua) | PASS, 14 checks; output equals build/controller-replacement-packlist.lua |
| `player_package_lua_check.lua` | PASS, 98 checks |
| `host_admission_lua_check.lua lua/coop/host_admission.lua` | **FAIL**, assert #3 at line 46 |

About the bootstrap check: my first run of `bootstrap_lua_check` failed only because I passed 3 arguments without the 4th archive_probe path. That was my mistake, not a code failure.

About the admission failure: the fixture (line 9) lacks `equipmentPreparationReturned=true`, which `lua/coop/host_admission.lua:27` requires. With that field added in a scratch copy, it passes 42 checks. This is a stale fixture, unchanged since the baseline commit. None of these checks is in CTest.

### 5. Git state
- **Branch history:** HEAD is ae88752. Before it are 554cc4f and 8542970 (the 0.10.27 baseline), all committed 2026-09-11 by Taylor.
- **Working tree:** 30 modified files (+656/−156), 43 untracked paths, and one gitlink with untracked content: third_party/minhook has **no .gitmodules** and contains an untracked provenance.json.
- **Untracked paths that the build depends on:**
  - native/include/mgscoop/session_release.hpp: `SessionSlotsReleased` does two passes over root 0x2bef840 +0xcc8/+0xd00 and main 0x2b9dfb0.
  - native/include/mgscoop/mission_termination.hpp: the `MissionTermination` scope.
  - native/include/mgscoop/mission_initialization.hpp
  - tests/session_release_tests.cpp, tests/mission_termination_tests.cpp, tests/session_lifecycle_lua_check.lua
- **Other untracked paths:** tools/package.ps1, package-install.ps1, package-configure-role.ps1 and package-readme.txt; docs (testing-2026-09-12.md, local-package.md, mission-thread-handoff.md, outgoing-scene-cleanup.md); 28 docs/evidence files.

**What each changed file does:**
- **native/game_script_observer.cpp:** removes the "source compilation thread = owner" assumption (`host_source_thread`). In its place:
  - an SRWLOCK serializes `ObserveEntry`;
  - a `MissionPhase` state machine (idle → allocating → allocated → running) is driven by new Lua receipts (ALLOCATION_ENTER/RETURNED, INITIALIZATION_ENTER/RETURNED, RUNTIME_ENTER, TERMINATION_RETURNED);
  - gates are now per-thread: `OnAllocationThread`, `OnRuntimeThread`, `OnTerminationThread`;
  - `TransferRuleThread` hands rule-module ownership between threads;
  - the termination scope may run on a worker only if no session is owned;
  - the captured `allocation_mission` is passed to the close gates;
  - Diagnostic mode now goes through the Ctrl+F8 poll and returns diagnostic_passed or diagnostic_failed without a native join;
  - the old automatic CLIENT_JOIN receipt is removed.
- **native/dynamite_session_port.cpp/.hpp:**
  - `AdoptDynamiteSessionThread` revalidates the root/session/main singletons, the vtables (base+0x2126f70 and +0x2127040) and state==3 twice before moving the owner thread.
  - `ReconcileDynamiteSessionRelease` clears adapter ownership only once the singleton slots are empty, with no native calls.
  - The close path takes `outgoing_mission`, because live `vars.missionCode` may already be the destination.
  - A returned close no longer clears ownership immediately.
- **damage_patch, soldier_patch, respawn_patch, full_close_patch, enemy_targeting (.cpp/.hpp):** each adds `AdoptMissionThread(previous_thread)`. full_close `ContextReady` and `Activate` take `outgoing_mission` (must be 30050).
- **native/host_session_trial.cpp:** the diagnostic check now returns `ready` instead of always `false`.
- **native/player_sync.cpp/.hpp:** explicit little-endian 28-byte serialization (replacing memcpy of the struct), with stricter validation. Still a development format with no transport.
- **native/probe.cpp, tools/probe.ps1:** version strings bumped to 0.10.33.
- **tools/set-session-mode.ps1:** refuses filesystem links, requires a matching product and game_directory, accepts loaders 0.10.27-0.10.33, and adds a Diagnostic-mode message.
- **lua/coop/client_join.lua:** adds the diagnostic_manual flow.
- **lua/coop/session_feedback.lua:** removes the 554cc4f `realizePlayer2` (TppPlayer2 index 1 Realize plus Warp). The code comment says index 1 is the guest's own local player.
- **lua/coop/session_lifecycle.lua:** `mgsCoopSceneMission` keeps the scene mission captured at allocation; a new outer wrapper sends the boundary receipts around OnAllocate, OnInitialize, OnMissionGameStart and OnTerminate.
- **tests/client_join_lua_check.lua, host_trial_lua_check.lua, host_trial_patch_check.cpp:** fixtures updated for the new receipts and for the equipment and marker insertion sites.
- **README.md and docs:** describe the 0.10.33 status and the 2026-09-12 test runs.

### 6. Verified vs. not verified
- **Verified in this session:** compilation, the 21 synthetic CTest suites, the Lua fixtures, and DLL identity with the installed build.
- **Live-verified, per repo docs only (not re-run here):** host creation (state 3, accept=1) in Mother Base, plus loading/termination worker handoffs.
- **Not verified anywhere:** a guest join, a second actor, shared combat, connected-peer teardown, and any campaign mission.
- **Single-PC readiness:** the guest instance runs the stale 0.10.27 loader. Its install record also blocks the current installer and role configurator, which compare `game_directory` against the target folder.