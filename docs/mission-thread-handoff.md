# Mission loading thread handoff

This document records the original 0.10.28/29 implementation and its then-current
validation limits. September 12 testing supersedes its fixed-runtime-thread
assumption: 0.10.32 supports matching unowned termination on a worker, and
0.10.33 separates loading-worker initialization from gameplay start. Current
installation and test evidence are in [status](STATUS.md) and
[test results](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-09-12.md). References below to an untested build or
installed 0.10.27 describe the earlier investigation.

Build **0.10.28**, prepared on 2026-09-12, fixes a host-startup refusal found while reviewing the user's changes. It is compiled, not installed or tested. [Build and fingerprint record](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/mission-thread-handoff-prepared-2026-09-12.json).

The current **0.10.29** also transfers native rule cleanup ownership in both lifecycle directions and retains the outgoing mission ID for shutdown. Those gaps were not covered by 0.10.28. [Follow-up fix](outgoing-scene-cleanup.md).

## Observed failure

The user's installed 0.10.27 launch (PID 63008, September 11) compiled TppMain on thread 63456. Title allocation used that thread, but helicopter allocation used 84508 and Mother Base allocation used 24012. Initialization and gameplay start returned to 63456. The final observed native session pointer was null, although the player system reported two storage slots. [Existing-log excerpt](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/user-launch-thread-excerpt-2026-09-12.log).

The selector accepted host preparation, marker-loading and mission-boundary receipts only on the compilation thread. It therefore skipped them during Mother Base allocation. The missing replacements are inferred from the source and the observed execution threads: the old logger did not record every refused receipt. Two storage slots do not establish two player actors.

## Implementation

- The outer mission wrapper now reports allocation entry, allocation return, initialization entry and termination return using operational receipts independent of the diagnostic quota.
- Allocation entry records its actual worker thread. Host creation remains before the original `Mission.Start`, with the existing executable, code-body, mission, archive, target and allocator gates.
- Initialization requires a completed allocation and matching mission. It records the first runtime thread and refuses later runtime thread changes. Host ownership transfers only if the adapter still owns the same native host and repeated root, main-slot, vtable and state reads agree. Clients join later on the runtime thread.
- Targeting transfers to that thread and clears cached selections. Admission, status and owned close continue to require the resulting thread and their existing native checks.
- Native selectors are serialized; their lock is released before the original Lua compiler/callback execution. Original mission callbacks remain outside `pcall`, preserving errors, coroutine yields and nil-containing return values. Optional feedback cannot prevent the original callback.
- Overlapping allocations, missing return boundaries and an unclosed owned session refuse new preparation. Allocation migration between workers within a callback is not enabled by this patch. A refused handoff requires investigation and a restart, not an automatic retry.

No new game address, native detour or Steam API was added for the handoff. The exact inspected executable is MGSV 1.0.15.4 AMD64, SHA-256 `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`.

## User changes retained and adjusted

Reviewed commits `554cc4fdba8327912f470962ef7f64e46c4a9349` and `ae88752e3148746544540c0baa7354bb9a19125c` added Diagnostic role selection, an attempted player-two setup, a transform codec and fixture updates. The fixture edits are retained; no tests were run.

The transform codec retains its 28-byte format with explicit little-endian fields. It now rejects zero epochs/session IDs, reserved action bits and mismatched envelope/payload sequences. Its version constant is not transmitted; a future transport must select this schema explicitly and supply peer authorization, freshness and actor mapping. It is not wired into native gameplay replication.

Automatic `Realize`/`Warp` calls were removed from session feedback: `Realize` was not verified for TppPlayer2, and slot 1 is the guest's own local player. Repeated status changes could also repeat the warp. The existing loading and native session paths remain responsible for actor creation. Diagnostic mode prepares loading without requesting a connection; it does not spawn a fake player.

The user's ignored `dist/mgs-coop-0.10.27` package remains unchanged. Its installer can overwrite an unrelated loader and its batch configurator writes to the extracted package with incorrect marker formatting and host-ID expansion. Do not treat it as the updated deliverable; a corrected, ownership-aware package is still required.

## Validation limit

`./tools/build.ps1 -Configuration Release -BuildOnly` compiled and linked the updated loader. No tests, Lua/source-transform execution, installation, game launch or multiplayer verification were performed. Installed 0.10.27 remains hash `6B48415471028AF6CE9B3D20BE8F75A1FC7AA2494DD1BB9AB0E37173157FEDDC`; the shipped executable and Steam DLL hashes matched the recorded originals.

The next gameplay check is whether Mother Base now reports allocation acceptance, host creation, a successful thread handoff and open admission, followed by normal shutdown. A connected guest and independent shared gameplay remain separate outstanding checks.
