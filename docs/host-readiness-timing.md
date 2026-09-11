# Host readiness timing

Loader 0.9.6 resolves the ambiguous 0.9.5 refusal: the player implementation pointer is null before `Mission.Start()` and at `OnInitialize` entry. At `OnMissionGameStart` entry, the same preflight passes with index limit four. Native host creation remains disabled.

## Live result

PID 14496 started at 22:43:04 UTC on 2026-09-09. Continue at 22:44:52 loaded Mother Base. TppMain selection and all three observations ran on thread 111904. The complete accessor bytes and host-function hash were checked before these bounded reads.

| Observation | Tick milliseconds | Current block | Player check |
| --- | --- | --- | --- |
| Before Mission.Start | 553520937 | `0x1a4a93f58` | `player_table_read` fails because the implementation pointer is null |
| OnInitialize entry | 553521015 | null | Same null implementation pointer |
| OnMissionGameStart entry | 553542562 | null | Known implementation type, index limit four; preflight passes |

All preceding mission, target-type and target-state checks passed at the two refusals. The printed `player_limit=0` was an unpopulated diagnostic variable, not a descriptor value. The TLS reads were consistent at each observation; this does not make the observations atomic or prove a resource lifetime.

An external read at 22:45:23 found the known implementation table, descriptor and executor limits four, bitset `0f`, and four distinct storage entries. This is reserved storage, not four initialized or independently playable characters.

Return to title was confirmed at 22:46:29; the menu appeared within 16 seconds and responded to Down and Up. All seven save/config hashes match the pre-test backup. The executable and Steam DLL are unchanged. At 22:47:56 a read-only check matched all 16 mapped native bodies and found the session and bandwidth-manager members null. MGSV remains at Continue. No debugger, native session call, allocator patch or TLS write was used.

## Implementation and checks

The host patch now also appends the existing bounded mission tracing fragment before TppMain's final return. Original script content and ordering are preserved. The two later observations only read memory; they do not consume or reset the one-attempt host-request latch. Preflight predicates are unchanged, but failures now identify their precise stage.

The prepared Release build passed all 14 native suites, 16 exact-source host-insertion assertions, 400 concurrent selections, and 72 actual-module Lua checks. The earlier mission-trace fixture coverage remains applicable to the unchanged tracing fragment; it was not rerun for this trial. Installed DLL SHA-256: `E7EDC9A04B544607510F547759D9F5008A2E494344DCCA6438AD44279A66507B`. The four-slot archive remains `74BFCD3656356EE4B08EEBCB9912D4806BA0B9F7F948D0C86E6E412D6E930666`.

## Next implementation decision

Follow-up: [exact-build player initialization analysis](player-session-initialization.md) verifies a constructor-recorded status bit consumed by allocation's network branches. Prepared 0.9.7 separates early session prerequisites from later player readiness; it remains uninstalled with host creation disabled. This advances the investigation described below.

The existing gate mixes two phases: early session setup and later player availability. Do not interpret its early refusal as a broken four-slot asset, or simply move the host call to mission start because the gate passes there.

Pinned Dynamite creates the host before `Mission.Start()` and sets `S_IS_ONLINE` before the rest of mission allocation. Its callback does not require a player implementation. Pinned tpp-mod also exposes a `session_create` command that directly calls the current-build host function; that source alone does not establish a safe phase for this adapter. These are source contracts, not successful execution evidence for this project.

Trace whether player allocation/initialization consumes session or online state. If it does, retain early session setup, separate that prerequisite from later actor readiness, and finish the allocation lifetime fix. If deferred creation is viable, verify its complete initialization and cleanup behavior before using the null-block observation to justify it. The remaining session allocations and asynchronous job release also require investigation. No active call is enabled by this report.

Evidence: [live trace](evidence/host-readiness-live-2026-09-09.log), [trial and save checks](evidence/host-readiness-result-2026-09-09.json), [player storage](evidence/host-readiness-player-storage-2026-09-09.json), [post-title network check](evidence/host-readiness-post-title-network-2026-09-09.json), [installation](evidence/loader-0.9.6-host-readiness-install-2026-09-09.json), [build](evidence/host-readiness-build-2026-09-09.log).

Source references: Dynamite `c90f252f8200765f66c005d6bf4357fda0c0d98e`, `moddata/standalone/TppMain.lua` and `hook/src/DynamiteLua.cpp`; tpp-mod `301bf797b7f8032941b21ed56a3570338de6ead4`, `src/client/component/session.cpp`. See [third-party notices](../THIRD_PARTY_NOTICES.md). No additional upstream code was incorporated in this diagnostic change.
