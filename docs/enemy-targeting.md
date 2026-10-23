# Co-op enemy targeting

Workspace **0.10.8** connects Dynamite's nearest-player policy to the native offense-player selector. The code compiles, but has not been installed, tested or executed in MGSV. It does not establish playable co-op or shared combat.

The host scans after the game's normal `TppMain.OnUpdate` completes. A frame counter reduces receipt parsing, and a native clock limits scans to two per second. Native code accepts only this adapter's created host on its creation thread, active host state 3, matching network-owner/main-session pointers, expected session/member types, and exactly two present members with distinct player indices 0 and 1. Presence uses the same flag as the native member-count implementation. An allocated actor slot or nonzero coordinate alone is insufficient.

The snapshot reads the previously mapped published player records, excludes the DEAD status bit, checks finite positions and repeats reads around the roster/position sample. Lua enumerates `TppSoldier2` through the existing game interface, skips absent, non-normal and incapacitated soldiers, and queries their positions using `GameObject.SendCommand`. It selects the player with the smallest squared distance to any eligible soldier within 1000 game units. This adapts Dynamite's single global offense-player selection; it does not assign each soldier an independent target.

The result passes through one of two fixed commit receipts. Native code rechecks the owned roster, selected player's life status and snapshot age before accepting it. The new hook at RVA `0x1E02770` returns that player index while the co-op rules and one-second selection lifetime remain active. Missing data, no eligible soldier, termination or expiry restores the original host-index result. Guest clients retain the original offense selector. The normal mission update's returns, errors and coroutine yields are preserved; only the added scan is protected by a Lua error handler.

There are no new background threads or raw native soldier-interface calls. The roster reader makes bounded `ReadProcessMemory` calls, never calls the getter used to lazily allocate a session, and never writes the roster or host identity. Repeated reads detect some changes but are not an atomic snapshot and cannot exclude pointer reuse. Remote publication timing, Lua command behavior, cost of the soldier scan, target switching and gameplay consequences remain unverified.

## Exact-build evidence and provenance

The [static contract](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/targeting-contract-2026-09-10.json) records the inspected executable path and SHA-256, complete code bodies and interface slots. Member construction places its session interface at `member+0x18`; the index getter reads `interface+0x28`, hence **member+0x40**. Using an older guessed structure field at member+0x28 would select the wrong index. The original offense function's dataflow returns the native host-member index.

Original `TppEnemy.lua` and `TppPlayer.lua` were extracted locally, read-only, into `build/targeting-scripts-20260910`. Their hashes are recorded in the contract. They provide the existing indexed-object, life/status, position and vector-component query conventions; no complete game script is shipped in the source. TppMain insertion retains the existing complete source fingerprint.

Source: Dynamite `c90f252f8200765f66c005d6bf4357fda0c0d98e`, `GetNearestPlayer` and `GetFobOffensePlayerInstanceIndexHook`; interface cross-reference: tpp-mod `301bf797b7f8032941b21ed56a3570338de6ead4`. Changes include actual roster indices, game-thread queries, squared distance, dead-player exclusion and expiring selection. Complete licenses and modifications appear in [third-party notices](../THIRD_PARTY_NOTICES.md).

Compilation used `tools/build.ps1 -Configuration Release -BuildOnly`. The first compilation stopped on a deprecated formatting API treated as an error; the bounded replacement compiled successfully. No test suites or game launches were run. [Build record](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/targeting-prepared-2026-09-10.json). Installed 0.10.5 remains unchanged.

Connected guest control, remote position publication, enemy/world authority, shared damage outcomes, death/respawn and disconnect handling still need gameplay evidence and further implementation. Full campaign adaptations and four players remain deferred.
