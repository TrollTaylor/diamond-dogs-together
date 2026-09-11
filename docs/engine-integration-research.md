# Independent MGSV engine integration research

Inspected 2026-09-09. This project is an original implementation. It has no dependency on an existing co-op mod and does not incorporate another co-op mod's code, address maps, protocol or asset edits. Findings below come from this installed game, the installed Windows library, and independent public API or mod-tooling documentation.

## Verified local target

These values were read directly from the installed files, including complete SHA-1 hashing:

| File in `D:\Games\steamapps\common\MGS_TPP` | Version | Bytes | SHA-1 |
| --- | --- | ---: | --- |
| `mgsvtpp.exe` | 1.0.15.4 | 166517760 | `4F643576C6F6ECF6C67A9AEF4D4F02285ADA43E3` |
| `mgsvmgo.exe` | 1.1.2.9 | 155560448 | `1582B87C25CC989FE5457B9F1FD90ACACA9BCD89` |

`steam_appid.txt` contains `287700`; `version_info.txt` contains `Tpp_steam_mst_en_day3900Mgo_patch_0707_1632`. The initial top-level listing contains both executables and `steam_api64.dll`, with no `dinput8.dll` present. This observation does not establish that every game archive is unmodified.

The hashes also match the English builds identified in the independent [tpp-mod version table](https://github.com/alicealys/tpp-mod#installation). That is corroborating version evidence only. This project does not import that project's native code or address map.

The separate `mgsvmgo.exe` is observable evidence of a separate executable. Its existence does not establish that MGO actor allocation, replication or game rules can be used in the TPP campaign. No four-player game session was observed in this inspection.

## Original loader diagnostic

`native/probe.cpp` is an original Windows SDK implementation with no game offsets, hooks, gameplay changes, Steam API calls, networking or save access. It inventories an already-loaded module; it does not load a second Steam runtime or initialize a Steam user context.

The export table of this machine's `C:\Windows\System32\dinput8.dll` was read directly from the PE file. The original proxy preserves all observed names and ordinals:

| Ordinal | Export |
| ---: | --- |
| 1 | `DirectInput8Create` |
| 2 | `DllCanUnloadNow` |
| 3 | `DllGetClassObject` |
| 4 | `DllRegisterServer` |
| 5 | `DllUnregisterServer` |
| 6 | `GetdfDIJoystick` |

Windows documents [DirectInput8Create](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416756(v=vs.85)) as creating the DirectInput object from `dinput8.dll`. The proxy obtains the absolute System32 path, loads that module with the system-directory search flag, verifies the actual loaded path, and resolves exports individually. Microsoft's [LoadLibraryExW documentation](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw) describes that search behavior.

Only `DisableThreadLibraryCalls` runs in `DllMain`. Initialization occurs lazily when a forwarded export is called. No global C++ object needs destruction during unload; the real DirectInput module remains loaded for the process lifetime.

After the first forwarded `DirectInput8Create` returns, the probe can record file-version metadata and observed Steam export presence in `mgs-coop/probe.log`. It derives the location from the loaded proxy's absolute path, checks that the proxy is beside `mgsvtpp.exe`, and never uses the current working directory to choose its log location. It preserves the DirectInput HRESULT and last-error result even when diagnostic output cannot be written. The diagnostic is disabled in other process names; non-x64 builds are rejected at compilation.

The log must say when Steam was not loaded at observation time. A missing modern export in an older `steam_api64.dll` is an observation about that module, not a reason to substitute a newer DLL. Export presence alone does not prove interface ABI compatibility, authentication, a session or a remote player.

This is a loader proof only after a controlled launch produces a log and input still works. Compilation and export inspection are useful prerequisites but are not an in-game test. No completed four-player mod is represented by this probe.

## Independent native and Lua integration route

The game's native entry points and data layouts must be researched independently for the exact installed executable. Record each discovered function with the binary hash, architecture, relative address, validation bytes, calling convention, ownership/lifetime expectations and the actual observation supporting its interpretation. A guessed name or older-version address cannot count as a working hook.

The first gameplay integration should be read-only: identify a stable in-game update boundary and a way to observe the active mission, transition state, local actor and session status. Refuse unknown builds before installing hooks. Read only allocated actors; an integer session limit is not sufficient evidence that corresponding character objects exist.

Public [IHHook documentation](https://github.com/TinManTex/IHHook/blob/master/README.md) describes extending MGSV's modified embedded Lua runtime through its C API. This is independent evidence that a native/Lua boundary has been used by mod tooling, not a dependency or an address source for this project. Its stated executable target is older than the locally verified build. Do not attach a separate ordinary Lua interpreter to the game's internal state. Independently verify the current runtime and register only the project's original interface.

The project's engine adapter should keep these capabilities separate:

| Capability | Evidence needed |
| --- | --- |
| Session membership | Real joining/leaving identities, unique stable member mapping and disconnection behavior. |
| Controllable remote characters | Distinct allocated actors, separate inputs, visible equipment, animation and collision on every endpoint. |
| World replication | Agreed positions, AI decisions, damage, deaths, interactables and objective outcomes. |
| Story authority | One committed outcome for objectives, cutscenes, checkpoint transitions and mission completion. |
| Persistent campaign | Verified isolated save ownership, progression mapping and recovery after reload. |

Existing game networking is a candidate to be independently identified and tested before reuse. If it exposes a safe verified transport or replication component, reuse that component through an original adapter. If a component cannot support four peers, explicitly identify the missing behavior and implement it instead of assuming a Steam lobby provides it.

## Four-player and full-campaign acceptance gates

The user's objective means up to four active players from the opening through the ending. A spectator guest, a forced solo opening or a host-only ending would be a scope reduction and must not silently count as completion.

These are proposed engineering gates, not completed tests:

1. **Loader and input:** compile and inspect the original proxy; stage a reversible install without replacing another loader; launch to title screen and verify normal keyboard/controller input and the diagnostic.
2. **Independent observations:** validate current-build symbols and read-only mission/session/actor reporting, including title screen, a loaded mission and a transition back out.
3. **Two real endpoints:** independently establish the transport and two engine-backed actors in a minimal controlled scene. Verify movement, equipment, damage and disconnect at both endpoints.
4. **Three and four actors:** prove allocation and synchronization for each actor with separate input sources. Verify that the fourth actor is not a duplicate host, an NPC proxy without full interactions, or a spectator. Test a guest leaving while the others continue.
5. **Shared combat:** test separated engagements, two players interacting with the same enemy/object, simultaneous damage and revive requests, different frame rates and delayed packets. Host authority must prevent double extraction and duplicate objective completion.
6. **Opening:** create dedicated co-op staging for heavily scripted hospital sequences and verify the first shared checkpoint transition. Preserve the story while giving all active players a supported role.
7. **Campaign adapters:** verify tutorials, free roam, main missions, bosses, cutscenes, vehicles, helicopters, prisoner transport, Mother Base and story unlock transitions individually.
8. **Completion:** record an uninterrupted fresh campaign with all four participants, including ending, reload, guest reconnection and checkpoint recovery. A start-to-finish claim requires this evidence.

The next immediate runnable artifact is the original loader probe. The next gameplay milestone is an independently mapped read-only mission/actor bridge. Neither milestone should be described as working co-op until real remote control and world synchronization are observed.
