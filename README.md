# MGS Co-op

An in-development campaign co-op mod for **Metal Gear Solid V: The Phantom Pain**. The immediate target is fully playable **two-player co-op**, using direct, attributed Dynamite adaptations. Four-player expansion is deferred. The full-campaign target includes the hospital, both chapters, endings, side ops, travel and Mother Base.

**Current development version: 0.11.1.** This prototype uses our own offline link (UDP for LAN/VPN, or online through Steam's networking with no port forwarding: [Steam online](docs/steam-online.md), fixture-tested, not yet run between two PCs) and separate co-op save copies. Normal Afghanistan, Africa and Mother Base packs support a second player body.

The debug-menu build was tested with both players on foot in Afghanistan after host-only title Continue. Live checks cover F9 input capture and restored movement, teleports and bookmark return, a visible equipped-weapon pickup, inventory refill feedback, daylight and rain. The earlier world-sync build also verified Africa, checkpoint copying, injected shared death, host-only Continue recovery and Africa–ACC–Afghanistan travel. Each build's receipts and limitations are recorded separately. [Build-specific live evidence](docs/testing-2026-10-05-world-sync.md), [current status](docs/STATUS.md).

For the configured single-PC lab, launch both copies with world sync enabled:

```powershell
pwsh -File tools/lab/Play-OfflineCoop.ps1 -World 3
```

Double-click [Play-World-Coop.bat](Play-World-Coop.bat) for the same world-level-3 launch.

In an offline copied-save session, **F9** opens the Dear ImGui debug menu. Mouse and keyboard navigation operate the menu; F9 closes it and restores game control. Actions run on the game's natural Lua update, with feedback in the menu and `mgs-coop/script-observer.log`. They are refused outside supported on-foot gameplay, including demos, loading and game-over. The menu provides:

- Position/yaw readouts, eight teleport bookmarks, return-to-bookmark and coordinate/relative teleports.
- Infinite ammo, refill the currently loaded hip/back/secondary gun, and local pickups of equipped weapons/items. Select guns with normal game controls; arbitrary unloaded guns are refused.
- Nearest authored guard inspection, enable/disable, bounded nearby clearing, camp/route selectors and patrol placement using the existing living guard pool.
- Host time/weather controls and partner movement/animation presets, manual locomotion clips, transform/heading errors and network telemetry.

The normal profile permits 13 locomotion/stance clips and eight resident turn clips without changing the runtime archive. A fresh normal-profile run verified all eight turn resources finishing naturally and actual aimed mouse pivots selecting crouched and standing turns. The newer turn detector uses accepted source heading history while retaining position smoothing; a research run verified two separated identical pivots playing once each and returning to idle. That newer source still needs a fresh normal-profile check. Gun intent is transported, but automatic upper-body aiming, firing and reload playback remains unverified. The separate action-history and native gun trials are default off. Changing a debug preset does not implement gun playback. Pickups and guard placement are local debug operations; they do not establish replication of new objects.

For the Enemy controls, **Refresh camps**, select a **Camp**, then **Load patrol routes** and choose a **Patrol route**. **Teleport to camp** uses a readable living guard position, or a validated authored player checkpoint when guards are unloaded. On the host, **Spawn guard on selected patrol** reuses a normal living guard within 150 m and requests placement at patrol point 0; **Teleport near spawned guard** goes to its confirmed position. The limit is eight distinct reused guards per mission. Dead, recovered, unreadable or unloaded guards are refused. Placement does not allocate a new soldier or establish replicated AI; with a paired world link the guest's copy of the same guard is moved the same way (spawn sync, [debug menu](docs/debug-menu.md)). Missing checkpoint locators also refuse cleanly. A development run visibly placed an armed living guard that subsequently killed the local player; the dated receipt identifies that DLL and the placement limitations. [Animation, world and enemy development evidence](docs/testing-2026-10-07-animation-debug.md). **Spawn enemy here** can instead move the nearest living guard 5 m in front of the host, facing him, through the engine's own set-transform message; this native placement is lab only, default off (`lab.ini debug_soldier_warp=1`), falls back to the route snap and is not yet verified in the game ([debug menu](docs/debug-menu.md)).

All 82 automated suites pass in aggregate, including native graphics/input fixtures, actual retail Lua composition, guarded debug commands and new/legacy installer rollback. The final wording-only build also passes 13 focused debug/bootstrap/composition checks.

Both copied-save games must first reach the same supported scene. Unknown enemy states stay held until their areas load; alignment does not establish identical AI or positions. A real two-PC session, full campaign/progression and physical damage death remain unverified. ACC has no partner body, and story missions and alternate packs such as `30011` are excluded. [World-sync design](docs/world-sync-design.md).

An experimental, default-off **native puppet** drive (lab only, never run in the game yet) can drive the partner through the game's own locomotion, action and gun systems from the other player's pad, with the animated partner as its automatic fallback. [Native puppet design and status](docs/native-puppet.md).

## What exists

- An original x64 DirectInput loader with exact-build script and actor guards. The local 13-entry runtime archive replaces each supported mission's single-player controller with one two-player controller. Afghanistan retains its scarf controller type; Africa and Mother Base use the normal type. Live field tests verified both player bodies and movement on two independently controlled game instances. Game-derived archive files stay local and are never committed or bundled.
- Our own offline UDP link sends player pose, appearance and movement data between the two games. The single-PC lab uses separate save copies; the friend launcher also activates co-op only for a copied-save session. The older native Steam-session experiments below are historical groundwork, separate from this active transport.
- Experimental shared enemy outcomes, alerts, clock and weather, with live W0–W2 evidence from fresh Afghanistan free roam. Earlier world builds verified host title Continue, Africa–ACC–Afghanistan and Mother Base–ACC–Afghanistan travel, initial enemy alignment, checkpoint copying and injected shared death/Continue recovery. The debug build retains that logic, with separate Afghanistan menu evidence. Unreadable enemy states stay unknown until their areas load. Hospital, story sequences, side-op progression, rewards, vehicles and full campaign support remain unverified.
- A checked import-table parser and pointer patch, plus a lifecycle-gated Steam context reader. The live callback trace demonstrates multiple callback threads; it does not establish a Lua owner thread. The context reader is tested but not yet called against the live game.
- An original bounded debugger observation that captured the game's natural network registrar entry and matching return, skipped a nested return, and detached successfully. A read-only external sampler reads independently mapped mission/player fields; startup samples do not establish an active actor or playable mission.
- A separate live startup capture recorded the game's own Steam initialization returning true and detached cleanly. This established one successful call; that experiment did not enable SDK access or multiplayer.
- The sampler also reads bounded published player-status records and named posture/movement bits. A live Mother Base dive and posture recovery correlated with the readings; title-screen and cutscene measurements still prevent using these bits alone as readiness checks.
- A compiled C++ session coordinator with a maximum of four members, build compatibility checks, reusable slots protected by connection generations, host-only objective commits, mission epoch checks, load barriers, and host-loss freeze.
- Per-peer message ordering, bounded event reordering, duplicate/stale input rejection, held-input expiry, and bounded explicit packet framing.
- Versioned player-input and complete player-pose-roster codecs, with trusted slot/source checks and 362 passing validation checks. These formats are not connected to a live transport or game actors.
- A host input receiver with 783 passing checks: malformed or unauthorized messages cannot consume live input sequence history, and rejected inputs preserve queued events. It requires a trusted adapter and exclusive ownership of the portable coordinator/router.
- A gated Steam packet adapter with 124 passing fixture checks, bounded reads, current-recipient checks and separate replaceable/reliable send modes. No live Steam connection is enabled.
- An in-process four-member development lab and automated tests. These are policy tests, **not four running game clients**.
- A campaign design and an explicit support register for episodes 0–50, story quest candidates, and incomplete sequence inventories. Complete campaign entries remain unsupported until their playable sequences have recorded verification.

## Build and check on Windows

Requires CMake, Visual Studio C++ build tools with an x64 Windows SDK, and PowerShell. The build includes pinned MinHook source for the script bridge and its fixture; see [third-party notices](THIRD_PARTY_NOTICES.md). Use `-BuildOnly` with the build script to compile the loader without running tests. Automated checks support the recorded live results; they do not establish full campaign support.

```powershell
./tools/build.ps1 -Configuration Release
./build/native/coop_lab.exe
```

The script discovers the Visual Studio toolchain, builds with Ninja, and runs CTest. The static C++ runtime avoids requiring debug redistributables on game machines.

For a local package, `tools/package.ps1 -RuntimeArchive <path>` compiles with `-BuildOnly`, checks the archive hash and bundles the loader, current source and licenses. The archive is generated or copied locally and passed to the owned installer; it is never included in the package. Owned upgrades keep hash-checked backups and a restore command. [Package workflow](docs/local-package.md).

For a two-PC test with a friend, `tools/package.ps1 -Friend` stages `dist/mgs-coop-friend-<version>-<stamp>` and a ZIP. It includes `Install-Coop.bat`, `Play-Coop.bat`, `Uninstall-Coop.bat`, `Collect-Logs.bat` and `FRIEND-GUIDE.txt`, with scripts compatible with Windows PowerShell 5.1. Installation backs up the real saves and creates a co-op save copy. `Play-Coop.bat` selects Host or Join and uses a fresh private join code for our own UDP link; both games play offline, without Konami sessions or Steam P2P. Steam still launches each player's licensed game. The installer leaves normal launches inactive on the real saves. The game-derived runtime archive is supplied locally. A real two-PC friend session remains unverified. [Two-PC workflow](docs/two-pc-play.md).

## Loader probe

```powershell
./tools/probe.ps1 -Action Status
./tools/probe.ps1 -Action Install
# Launch MGSV normally, then inspect D:/Games/steamapps/common/MGS_TPP/mgs-coop/probe.log
./tools/probe.ps1 -Action Remove
```

Close the game before installation or removal. The installer refuses to overwrite any existing `dinput8.dll`. Removal checks the recorded DLL hash before deleting only the owned loader. The actual game executable, Steam DLL, archives and saves are not replaced. Logs remain after removal. A diagnostic log establishes execution of the probe; normal input and scene loading still require gameplay verification.

## Design and evidence

- [Campaign design](docs/campaign-design.md): character roles, hospital staging, cutscenes, stealth, revives, shared progression, bosses, vehicles and failure recovery.
- [Campaign support register](data/campaign-support.json): explicit segment work and acceptance records.
- [Independent engine investigation](docs/engine-integration-research.md): current-build evidence and native integration gates.
- [Independent network binding analysis](docs/network-binding-analysis.md): 23 game Lua bindings decoded from this executable; no runtime calls made to these candidates.
- [Steam integration](docs/steam-integration-research.md): compatible interfaces, invitation/authentication boundaries and native transport investigation.
- [Steam packet adapter](docs/steam-packet-adapter.md): independently checked flat-export boundary, bounded packet I/O and remaining live prerequisites.
- [Steam lifecycle evidence](docs/steam-lifecycle-boundaries.md): passive initialization tracking and the limits of the current live observation point.
- [Natural networking getter](docs/steam-networking-getter-observation.md): original pointer forwarding, bounded caller receipts and independently mapped channel-zero paths.
- [Native packet-manager lifecycle](docs/packet-manager-lifecycle.md): verified lazy transport setup and conditional polling; upstream activation and virtual dispatch remain unresolved.
- [Live session owner](docs/live-network-owner-observation.md): original bounded external reader, constructor-derived type check and five live baseline samples.
- [Steam startup observation](docs/steam-init-caller-investigation.md): independently mapped natural initialization call and ten original debugger fixture cases.
- [Earlier Steam entry](docs/steam-early-entry-investigation.md): the actual virtual-method dispatcher and unresolved early installation timing.
- [Typed player messages](docs/typed-payload-format.md): byte formats, admission boundaries and coordinator handoff.
- [Published player position candidate](docs/player-vector-producer-investigation.md): independently traced position-related use; producer actor and coordinate definition remain unresolved.
- [Live player motion](docs/live-player-motion-observation.md): observed dive and posture recovery, failed short forward taps, and evidence that the two position sources are not a synchronized snapshot.
- [Natural player-record writer](docs/live-player-record-writer.md): original hardware watch captured bank copying and a direct position updater; source ownership remains unresolved.
- [Natural registration observation](docs/registration-hook-design.md): independent caller/unwind evidence and the next debugger experiment.
- [Live registration captures](docs/live-registration-observation.md): observed entry and return, continued startup and the limits of raw mission/player fields.
- [Progress and next gates](docs/STATUS.md): implemented work, validation and remaining requirements.

## Architecture boundaries

The active offline link and guarded partner driver are distinct from the earlier portable coordinator and Steam-adapter experiments described below. W3/W4 extend the offline world path; their tested subset and remaining live checks are recorded above.

The future engine adapter must authenticate peers, bind session IDs, connect the host input receiver, and call the portable coordinator on the game thread. A claimed identity inside a packet is never trusted. The router's `ValidatedByEngine` flag is an internal adapter contract; it does not perform Steam authentication. Reordering storage does not implement retransmission, acknowledgement, timeout or encryption.

Replaceable movement and held buttons expire after 250 ms without an update. One-shot actions such as shots, interactions, revive completion and extraction need separate reliable, host-validated events; they cannot be inferred reliably from the latest held-button sample. The framing layer intentionally has no raw pointers or game-memory writes.

The coordinator's `Checkpoint` is a mission/epoch barrier record, **not an actual FOX world snapshot or save**. Late joining, rejoining during play, save transactions and host migration are not implemented. Starting a new load clears the temporary objective ledger; restoring persisted world/objective state is future adapter work.

Four-player support must be demonstrated with four independent playable actors and three remote clients. A lobby limit, packet test or NPC stand-in is insufficient. Converting originally playable sequences into spectator-only segments does not satisfy the start-to-finish goal.

