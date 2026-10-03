## 1. Build-to-game pipeline

**Build (compiled, reproducible)**
- `tools/build.ps1` runs CMake/Ninja.
- `CMakeLists.txt:100-143` runs `file(READ lua/coop/<m>.lua)` and then `configure_file(native/<m>_source.hpp.in -> build/native/generated/<m>_source.hpp)`.
- Each template wraps the whole file in a C++ raw string. For example, `bootstrap_source.hpp.in` produces `inline constexpr char bootstrap_source[] = R"MGSBOOT(...)MGSBOOT"`.
- `CMAKE_CONFIGURE_DEPENDS` forces a reconfigure whenever a Lua file changes.
- `native/bootstrap_patch.cpp` includes all 14 generated headers and builds `mgs_bootstrap_patch`.
- That library is linked into `mgs_coop_probe`, which is output as `dinput8.dll` (`CMakeLists.txt:180-183`).
- `archive_probe.lua` is the only Lua that is not embedded; it goes into the archive.

**Startup (verified live)**
1. `dinput8.dll` proxies `DirectInput8Create` (`probe.cpp:186`). After a successful create, `InstallSteamCallbackObserver` runs, and `steam_observer.cpp:271` calls `InstallGameScriptObserver(base, span, exeDir)`.
2. The observer reads `mgs-coop/script-observe.enabled`. Exact byte strings select hierarchical modes (`game_script_observer.cpp:536-545`). Host/Join use `host-v3` / `client-v1`.
3. If the mode is archive-v1 or higher, `MountPlayerArchive` runs (`archive_mount.cpp:175`). It checks, in order:
   - the SHA-256 of 0x16C code bytes at RVA 0x48E20;
   - the file size, 3,720,192 bytes;
   - the file SHA-256, 26ce7c….
   It then calls `CreateWithPackFile("mgs_coop_player_runtime_v1", path, 0, 0x21)`.
4. A MinHook detour is installed on `luaL_loadbuffer` (RVA 0x1A178E0) after a 36-byte body check. The policy is `ObserveEntry` / `ObserveReturn`.
5. For every Lua compile, `ObserveEntry` (serialized by an SRW lock) tries the exact-source selectors. If one matches, the bridge passes a replacement buffer of at most 4 MiB to the original function.
   - Tpp.lua: insert `bootstrap.lua`, plus `archive_bootstrap.lua` when the archive is mounted.
   - DamageParameterTables.lua: replace the prefix and append the damage module.
   - TppMission.lua: append `death_policy`.
   - TppPackList.lua: append `player_package_selection`.
   - TppMain.lua: perform the host composition described next.

**TppMain host composition** (`bootstrap_patch.cpp:128-200`)
- At three unique anchors, in order:
  - `host_session_trial` before `Mission.Start()` (inside OnAllocate);
  - `equipment_loading` before `TppEquip.StartLoadingToEquipMissionBlock()`;
  - a guard around `TppMarker.RestoreMarkerLocator()`.
- Then these are appended before `return e`: host_admission, client_join, damage_lifecycle, enemy_targeting, respawn, session_feedback, marker_loading, mission_trace, session_lifecycle.
- Each appended module captures `e.OnX` and re-wraps it, so `session_lifecycle` is the outermost wrapper.

**Lua-to-native bridge**
- Lua calls `loadstring("return 'MGS_COOP_..._V1'")`.
- The hook recognizes `name == buffer` and returns substitute source: `return true`, `return false`, `return 'client'`, or `return {localIndex=..}`.
- No `lua_State` is kept, and native code makes no VM calls.

**Live proof (0.10.33, PID 52824, script-observer.log)**
- replacement=1 for DamageParameterTables (entry 22), Tpp (29), TppMission (59), TppMain (66) and TppPackList (70).
- `PACK_SELECTED` for mission 30050.
- `HOST_CREATE` on loading worker 83844.
- `EQUIPMENT_GROUPS_RETURNED`.
- Native session state=3.
- Allocation → initialization → runtime → termination boundaries all accepted, followed by the 40050 transition.
- No guest has ever executed this layer.

## 2. What the custom archive contains (verified from the build stage)

**Source and format**
- Built from `C:/Users/taylo/OneDrive/Documents/ChatGPT/mgs-coop/build/controller-replacement-20260909`; its hash matches the installed file.
- QAR version 1, flags 0x3011E0, 7 entries.

| Entry | Size | Contents |
| --- | --- | --- |
| `mgs_coop_four_players_game_obj.fpk` | 112 B | FPK half of the player game-object pair |
| `mgs_coop_four_players_game_obj.fpkd` | 2,416 B | FOX2 `GameObject` of type TppPlayer2, totalCount/realizedCount **2** |
| `mgs_coop_four_player_controller.fpk` | 2,407,088 B | Byte copy of `mtbs_mission_fob.fpk`: player2_online.mtar and player2_pipe.mtar |
| `mgs_coop_four_player_controller.fpkd` | 6,624 B | Controller (partsBlockCount 2, instanceBlockSize 1,126,400, PLTypeNormal x2), additional_parts (count 1), online motion, mtbs motion loader |
| `MgsCoopArchiveProbe.lua` | 215 B | Receipt-only probe script |
| `mgs_coop_f30050.fpk` | 989,168 B | Exact copy of `f30050.fpk` |
| `mgs_coop_f30050.fpkd` | 308,880 B | 44 of 45 `f30050.fpkd` entries; `single_player2_instance.fox2` removed; still contains f30050_sequence.lua etc. |

**What it amounts to**
- This equals Dynamite's `coop_essentials` plus Dynamite's controller-deletion technique, applied to Mother Base.
- The 'four' names are historical. The four-slot build (74bfcd…) is staged under `build/four-controller-replacement-20260909` but is not accepted by the loader.

**How it is used**
- `player_package_selection.lua` substitutes the f30050 path and appends the controller FPK.
- This only happens when `missionCode == 30050` and `archiveDirectReadSucceeded` is set.

## 3. How missions and TppMain are modified today

**Scope**
- Only the global libraries listed above are modified, in memory.
- No mission sequence script is modified.
- Mother Base data is changed only through the renamed FPKD.

**Mission-30050 gating**
- Lua: 11 sites check `vars.missionCode == 30050`.
- Native: `dynamite_session_port.cpp:133`, `full_close_patch.cpp:57`, `host_session_trial.cpp:92`, `game_script_observer.cpp:301` and `:492-493`.

**What Dynamite has that mgs-coop lacks**
- Dynamite calls `TppCoder.SetWorldCenter(Vector3(...))` before `SessionEnableAccept(true)` in 36 sequence scripts; mgs-coop never calls it.
- Dynamite's OnAllocate skips `ForceChangePlayerFromOcelot` for co-op missions; mgs-coop keeps the vanilla call.

## 4. Requirements to (a) ship more Lua and (b) change game data for more missions

### (a) More Lua (co-op menu, character select, mission patches)

**Option 1: add another embedded module (current method)**
Steps:
- Create `lua/coop/x.lua` and `native/x_source.hpp.in`.
- Add the CMake `file(READ)` / `configure_file` lines.
- Splice the module into an existing selector's `result_size` / `memcpy` chain, or add a new `Select*Patch` for another exact source.
- Update the `*_patch_check` tools and the Lua fixtures.

Constraints:
- MSVC error C2026 caps a single literal at 16,380 bytes; `session_lifecycle.lua` is already 10,378 B. Split the literal or generate a byte array.
- Exact-hash brittleness.

**Option 2: ship Lua inside the archive**
- Load it with `Script.LoadLibrary` or `dofile` from `archive_bootstrap.lua`. This was proven live in trial 6, and archive scripts also pass through the hook.
- Cost: re-stage the archive with datfpk and update three hash pins.

**Option 3: native `MgsCoop` library via the engine binder**
- Hook `lua_func_register_functions` at 0x17A0A0 and use `lua_bind_start` 0x1A158E0, `lua_bind_closure` 0x1A15690, `lua_bind_end` 0x1A15760 and `LuaAutoPtr` 0x6A590 / 0x6A960. The registrar decode at 0xBF9E30 corroborates these addresses.
- Must be byte- and ABI-verified per AGENTS.md before use.
- Removes the per-call loadstring overhead and allows real tables.

**Option 4: dev-only loose-file override**
- Follow tpp-mod's `lual_load_buffer_stub` pattern.
- Allows iterating on Lua without rebuilding the DLL.

**What a menu and character select would need**
- Input: `PlayerVars.scannedButtonsDirect` with `PlayerPad` masks, as in Dynamite's `script_loader.lua`.
- Text: `AnnounceLogView`. Popups need lng2 langIds; datfpk supports lng2.
- Guest appearance: set `vars.playerType` to DD_MALE, DD_FEMALE, AVATAR, OCELOT or QUIET, plus parts, camo, face and staff header/seed.
- A TppMain splice to skip `ForceChangePlayerFromOcelot` in co-op.
- Remote appearance depends on the game's FOB synchronizer and the additional-parts block (count 1); this is unverified.
- D-Dog is not a PlayerType, so it is infeasible.

### (b) Game data for more missions

For each mission, mirror Dynamite's `moddata.go`:
1. **Remove the single-player controller FOX2.** Delete `single_player2_instance.fox2` or `single_pl2_instance_scarf.fox2` from every area FPKD of the mission (area, area01, area02…). Without this, a duplicate controller caused the 778,000-byte block wait seen earlier.
2. **Add the controller pack.** Append the controller FPK to the mission pack list through the TppPackList wrapper.
3. **Adapt the sequence script.** Add `SetWorldCenter` and `SessionEnableAccept` at the first gameplay OnEnter, a session close at clear sequences, and per-mission fixes. These can be exact-source splices keyed by chunk name; package scripts are confirmed to pass through the hook.
4. **Generalize the 30050 gates.**

Packaging route depends on unresolved archive precedence:
- If a same-path override in our archive wins, ship only modified FPKDs.
- If not, ship renamed FPKD/FPK pairs, which costs FPK copies.

Tooling: `stage_controller_replacement.py` generalizes to this pattern. `archive_probe` must also scan data1.dat. Generate assets on the user machine.

## 5. Dynamite comparison

| | Dynamite | mgs-coop |
| --- | --- | --- |
| Native Lua access | Registers a `Dynamite` C library from a `luaL_openlibs` hook (old exe); no 1.0.15.4 mapping exists | loadstring receipt RPC through the `luaL_loadbuffer` hook |
| Game Lua changes | Replaces 10 whole Lua libraries in 00.dat | In-memory splices into 5 libraries, each pinned by exact hash |
| Mission data | Replaces about 50 sequence scripts inside repacked FPKDs; deletes controller FOX2s | One renamed Mother Base package |
| Archive handling | Backs up and rewrites master/0/00.dat (Steam verify reverts it) | Never touches game archives; mounts a standalone archive (reversible) |
| Other installed files | Loose `lua/*.lua` plus `dynamite.ini` | None |
| Strings and UI | Adds lng2 strings to lang FPKs | AnnounceLogView raw strings only |

- `hook/src/lua` holds only Lua headers.
- Dynamite's 36-mission list excludes Mother Base and the hospital.

## 6. Single-PC notes for this area

- The guest copy's DLL is 0.10.27 while the host's is 0.10.33, so the embedded Lua differs between them.
- The guest archive hash matches.
- `probe-install.json` in the guest folder points to MGS_TPP, so `set-session-mode.ps1` refuses the guest folder.
- That script also refuses while any mgsvtpp process is running.
- The DLL derives its marker and archive paths from its own directory, so each instance reads its own `mgs-coop/` folder.

## Verification levels

- **Verified live:** the mount, every replacement listed, host receipts, and package script names seen by the hook.
- **Compiled or fixture-only:** client_join, respawn, targeting, damage effects with a peer, and all guest paths.
- **Speculation:** SetWorldCenter necessity, Quiet/Ocelot animation completeness, archive precedence outcome, and remote appearance replication.