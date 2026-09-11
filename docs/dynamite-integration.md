# Dynamite reuse and current-build port

## Current priority and live progress

Current update, 2026-09-10: installed 0.10.5 includes the mapped second-player loading bypass and teammate-role hooks. Workspace 0.10.27 adds manual joining, session feedback, equipment/loading and combat/lifecycle adaptations, plus existing teammate-marker presentation and the independent mission teardown signal. These paths are compiled only and remain unverified. No connected guest or shared combat is verified. Full campaign scripting is deferred. See [current status](STATUS.md), [remaining gameplay gaps](dynamite-gameplay-gaps.md) and [teammate markers](teammate-markers.md); older preparation/installation statements below are historical.


The user now prioritizes direct Dynamite reuse and fully playable **two-player** co-op. Four players are deferred. The 0.10.0 adapter has created a live native host and returned to title/quit normally; 0.10.1 adds mission admission through the existing Lua interface. [Current port and evidence](dynamite-host-port.md). The source review and older asset experiments below are historical records, not current installation claims.

Prepared 0.10.3 adds the current-build native guest request, Dynamite client startup order, and the session Start required by the final native handshake. Local tests pass; no native guest request or joined peer has been live-verified. [Guest implementation](client-join.md).

2026-09-09. The user authorized reading and reusing Dynamite to accelerate the four-player complete-campaign project. This supersedes the earlier prohibition. **Playable co-op remains unimplemented.**

## Source and compatibility

Pinned Dynamite revision: `c90f252f8200765f66c005d6bf4357fda0c0d98e`. See [license and attribution](../THIRD_PARTY_NOTICES.md). Its map targets the earlier English build. Against installed 1.0.15.4 SHA-256 `085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45`, the offline audit finds:

- 25 active entries in `GetPatches`: 24 expected-byte mismatches and one address outside the image. No expected-byte matches.
- 202 named addresses: 54 outside the image and 148 file-backed locations. File backing says nothing about matching function identity.
- Commented-out patches and the separate master-server URL patch are excluded.

Reproduce with `python tools/audit_dynamite_reference.py --output docs/evidence/dynamite-compatibility-2026-09-09.json`. The tool checks the pinned source hashes, reads the executable and emits an inventory. It never installs patches. [Recorded result](evidence/dynamite-compatibility-2026-09-09.json).

The current [tpp-mod source](https://github.com/alicealys/tpp-mod/tree/301bf797b7f8032941b21ed56a3570338de6ead4) lists SHA-1 `4F643576C6F6ECF6C67A9AEF4D4F02285ADA43E3`, which was recomputed from the installed executable and matches exactly. Its English TPP addresses occupy the first map column. The pinned source supplies **16 candidates**: 15 Dynamite names or explicit aliases plus the existing game-owned FobTarget accessor. The remaining 187 Dynamite names lack an unambiguous nonzero match in this join. These are candidates, not 16 approved hooks.

Reproduce with `python tools/map_dynamite_current_build.py --output docs/evidence/dynamite-current-build-candidates-2026-09-09.json`. This additionally requires the project's Capstone dependency and rejects a different executable. [Candidate bodies and call graphs](evidence/dynamite-current-build-candidates-2026-09-09.json).

| Function | Current RVA | Evidence and remaining work |
| --- | --- | --- |
| CreateHostSession | `0x7EE590` | Declared body fully decoded. Tests initial predicate, creates/reuses the session owner and returns success/failure in AL. Do not reuse the old int64 result declaration blindly. Runtime ownership and readiness remain unverified. |
| CreateClientSession | `0x7EE380` | Source candidate and decoded body; takes FobTarget and SessionConnectInfo. Runtime ownership, lifetime and result handling need verification. |
| GetFobTarget | `0x7D6470` | Current source obtains it through ServerManager for both host/client commands. This may avoid Dynamite's ad hoc temporary client allocations; inspect the object lifetime first. |
| GetMainSession | `0x313780` | Matches the accessor independently found through the game's network Lua queries. It can initialize state, so it is not a passive readiness query. |
| CloseSession | `0xA1A710` | Source candidate; close must be coordinated with actors, mission barriers and outstanding traffic. |
| GetQuarkSystemTable | `0xBFF050` | Matches the accessor already encountered in the original player-owner investigation. |

The remaining candidates cover hashing and Lua access. Same unqualified names can still identify different wrappers; source signatures are not ABI guarantees. In particular, current-source `lua_pushboolean` points to a wrapper rather than the previously decoded direct push helper. No candidate is consumed by the loader.

## What to reuse and what four players changes

1. **Engine connection and ownership.** Port Dynamite's host/client setup and game socket hooks using the current-build map. Reuse game-owned connection objects where verified; integrate with the existing four-slot coordinator. Preserve engine and Steam peer identity rather than inventing a second unsynchronized session.
2. **Actor allocation.** Dynamite's player controller asset selects `two_players_game_obj.fpk`, two parts blocks and two entries in both parts arrays. Its making-of notes report an unsuccessful attempt above two players. Inspect the referenced game package and player/parts/motion allocations together before staging a four-actor variant. Merely changing two array sizes cannot establish four playable actors.
3. **Multi-peer synchronization.** Dynamite has a single stored UDP address/socket context and a single `packetSeen` cursor. Its raw receive path verifies a fixed padded buffer. Adapt message framing to actual received lengths and per-peer generations/order, with host authorization before world changes. The project already has per-peer admission and ordering logic to reuse. Do not copy the single-peer routing state into a four-member session.
4. **Campaign behavior.** Reuse the mission loading, AI targeting, damage, hostage and transition fixes as starting points. Track host-authoritative objectives, interaction ownership, shared extraction, revive and transition barriers. Dynamite's source and documentation do not establish playable hospital, every mission or every ending. Keep all entries unsupported until exercised with real clients.
5. **Save ownership.** Inspect upstream save renaming/copying and build a separately tested co-op profile before enabling campaign persistence. Existing game saves and the running probe remain unchanged by this porting step.

Immediate implementation target is current-build host/client ownership and the player allocation package, followed by a second real actor and then three/four actors. Address discovery and old diagnostic observations are not substitutes for those tests. No Dynamite DLL, Lua replacement, archive patch, game-code patch or new network connection was installed during this source review.

Subsequent progress: the [installed two-player package was extracted and an experimental four-player variant compiled](four-player-allocation-assets.md). Both game-object counts and controller parts allocations now have staged four-player values; binary/container round trips passed. The files remain local and uninstalled, with runtime allocation and memory budgets unverified.
