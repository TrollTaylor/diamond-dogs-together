# MGS Co-op

An in-development campaign co-op mod for **Metal Gear Solid V: The Phantom Pain**. The immediate target is fully playable **two-player co-op**, using direct, attributed Dynamite adaptations. Four-player expansion is deferred. The full-campaign target includes the hospital, both chapters, endings, side ops, travel and Mother Base.

**Current development version: 0.11.1.** This prototype uses our own offline UDP link and separate co-op save copies. Normal Afghanistan, Africa and Mother Base packs support a second player body.

The current build verifies both players on foot in Africa after host-only title Continue, independent movement, initial enemy alignment and checkpoint copying. An injected guest death opens Mission Failed on both games; host-only native Continue restores both to the shared checkpoint and resumes enemy sync. Host-only travel from Africa follows through ACC into Afghanistan, with both players using the fresh helicopter entry and moving independently on the ground. Both games close normally and all ten real save/config files remain unchanged. The lab configuration and activation markers are disabled after the test. [Build-specific live evidence](docs/testing-2026-10-05-world-sync.md), [current status](docs/STATUS.md).

For the configured single-PC lab, launch both copies with world sync enabled:

```powershell
pwsh -File tools/lab/Play-OfflineCoop.ps1 -World 3
```

Double-click [Play-World-Coop.bat](Play-World-Coop.bat) for the same world-level-3 launch.

All 78 automated suites pass in aggregate after the lab installer test was rerun separately; the latest arrival/title/recovery checks pass 11/11. The focused lab cleanup suite also passes.

Both copied-save games must first reach the same supported scene. Unknown enemy states stay held until their areas load; alignment does not establish identical AI or positions. A real two-PC session, full campaign/progression and physical damage death remain unverified. ACC has no partner body, and story missions and alternate packs such as `30011` are excluded. [World-sync design](docs/world-sync-design.md).

## What exists

- An original x64 DirectInput loader with exact-build script and actor guards. The local 13-entry runtime archive replaces each supported mission's single-player controller with one two-player controller. Afghanistan retains its scarf controller type; Africa and Mother Base use the normal type. Live field tests verified both player bodies and movement on two independently controlled game instances. Game-derived archive files stay local and are never committed or bundled.
- Our own offline UDP link sends player pose, appearance and movement data between the two games. The single-PC lab uses separate save copies; the friend launcher also activates co-op only for a copied-save session. The older native Steam-session experiments below are historical groundwork, separate from this active transport.
- Experimental shared enemy outcomes, alerts, clock and weather, with live W0–W2 evidence from fresh Afghanistan free roam. The current build follows host title Continue and Africa–ACC–Afghanistan travel, then accepts an initial host snapshot. A preceding build also verified Mother Base–ACC–Afghanistan travel. Unreadable enemy states stay unknown until their areas load. Checkpoint copying and injected shared death have current-build receipts; host-only native menu Continue reloads both copies at the shared checkpoint and resumes enemy sync. Hospital, story sequences, side-op progression, rewards, vehicles and full campaign support remain unverified.
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

