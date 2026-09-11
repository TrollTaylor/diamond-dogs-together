# MGS Co-op

An in-development campaign co-op mod for **Metal Gear Solid V: The Phantom Pain**. The immediate target is fully playable **two-player co-op**, using direct, attributed Dynamite adaptations. Four-player expansion is deferred. The full-campaign target includes the hospital, both chapters, endings, side ops, travel and Mother Base.

**Current state: development prototype; playable two-player co-op is not verified.** The immediate target is two independently controlled players in a scene with shared gameplay/combat. Full campaign scripting, progression synchronization and four-player expansion are deferred.

**Installed build: 0.10.5.** It contains native host/client setup, Dynamite's second-player loading bypass and two teammate-role hooks. Earlier host creation/admission and guest setup loaded Mother Base successfully; a connected guest and independent control have not been demonstrated. The 0.10.5 live launch was interrupted by the user, so its new player rules remain unverified in-game. [Recorded guest setup](docs/evidence/client-loading-result-2026-09-09.json).

**New workspace build: 0.10.27, compiled only.** Fixes mission teardown depending on the 128-event diagnostic limit. A separate lifecycle signal now triggers native cleanup before the other mission wrappers; diagnostic errors cannot prevent original callbacks. Retains manual joining, session feedback and the equipment/combat/loading adaptations. Not installed or tested; independent guest control and shared gameplay remain unverified. [Teardown fix](docs/termination-boundary.md), [single-PC testing feasibility](docs/single-pc-testing.md), [remaining gameplay work](docs/dynamite-gameplay-gaps.md).

## What exists

- An original x64 DirectInput loader that forwards Windows input and observes existing Steam imports on the verified executable. Version 0.9.4 inserts an exact-source co-op bootstrap into Tpp.lua and mounts a fingerprinted custom archive. A live test verified execution of its custom script through both game loading methods. The controller package is selected for an initial Mother Base experiment, and that scene loads. The current two-player package supplies two indexed storage entries; independent guest control remains unverified. Native host creation is enabled only in explicit development modes. [Selection trial](docs/player-package-selection.md).
- A checked import-table parser and pointer patch, plus a lifecycle-gated Steam context reader. The live callback trace demonstrates multiple callback threads; it does not establish a Lua owner thread. The context reader is tested but not yet called against the live game.
- An original bounded debugger observation that captured the game's natural network registrar entry and matching return, skipped a nested return, and detached successfully. A read-only external sampler reads independently mapped mission/player fields; startup samples do not establish an active actor or playable mission.
- A separate live startup capture recorded the game's own Steam initialization returning true and detached cleanly. This establishes one successful call; runtime SDK access and multiplayer remain disabled.
- The sampler also reads bounded published player-status records and named posture/movement bits. A live Mother Base dive and posture recovery correlated with the readings; title-screen and cutscene measurements still prevent using these bits alone as readiness checks.
- A compiled C++ session coordinator with a maximum of four members, build compatibility checks, reusable slots protected by connection generations, host-only objective commits, mission epoch checks, load barriers, and host-loss freeze.
- Per-peer message ordering, bounded event reordering, duplicate/stale input rejection, held-input expiry, and bounded explicit packet framing.
- Versioned player-input and complete player-pose-roster codecs, with trusted slot/source checks and 362 passing validation checks. These formats are not connected to a live transport or game actors.
- A host input receiver with 783 passing checks: malformed or unauthorized messages cannot consume live input sequence history, and rejected inputs preserve queued events. It requires a trusted adapter and exclusive ownership of the portable coordinator/router.
- A gated Steam packet adapter with 124 passing fixture checks, bounded reads, current-recipient checks and separate replaceable/reliable send modes. No live Steam connection is enabled.
- An in-process four-member development lab and automated tests. These are policy tests, **not four running game clients**.
- A campaign design and an explicit support register for episodes 0–50, story quest candidates, and incomplete sequence inventories. All gameplay support remains unverified.

## Build and check on Windows

Requires CMake, Visual Studio C++ build tools with an x64 Windows SDK, and PowerShell. The build includes pinned MinHook source for the script bridge and its fixture; see [third-party notices](THIRD_PARTY_NOTICES.md). The loader includes explicit development modes for native host/client setup and scene rules. Use `-BuildOnly` with the build script to compile the loader without running tests. Playable co-op remains unverified.

```powershell
./tools/build.ps1 -Configuration Release
./build/native/coop_lab.exe
```

The script discovers the Visual Studio toolchain, builds with Ninja, and runs CTest. The static C++ runtime avoids requiring debug redistributables on game machines.

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

The future engine adapter must authenticate peers, bind session IDs, connect the host input receiver, and call the portable coordinator on the game thread. A claimed identity inside a packet is never trusted. The router's `ValidatedByEngine` flag is an internal adapter contract; it does not perform Steam authentication. Reordering storage does not implement retransmission, acknowledgement, timeout or encryption.

Replaceable movement and held buttons expire after 250 ms without an update. One-shot actions such as shots, interactions, revive completion and extraction need separate reliable, host-validated events; they cannot be inferred reliably from the latest held-button sample. The framing layer intentionally has no raw pointers or game-memory writes.

The coordinator's `Checkpoint` is a mission/epoch barrier record, **not an actual FOX world snapshot or save**. Late joining, rejoining during play, save transactions and host migration are not implemented. Starting a new load clears the temporary objective ledger; restoring persisted world/objective state is future adapter work.

Four-player support must be demonstrated with four independent playable actors and three remote clients. A lobby limit, packet test or NPC stand-in is insufficient. Converting originally playable sequences into spectator-only segments does not satisfy the start-to-finish goal.

