# Native host and mission admission

Two-player gameplay is the immediate target. Four-player expansion is deferred; the full-campaign objective remains. **No joined peer or playable co-op has been verified.**

## Host creation: live pass, loader 0.10.0

The adapter now creates a real native host before `Mission.Start` in the initial Mother Base experiment (mission 30050). It adapts Dynamite's `l_CreateHostSession` and persistent-allocation workaround at commit `c90f252f8200765f66c005d6bf4357fda0c0d98e`, with the current game's Boolean ABI and borrowed game-owned FobTarget. Two specific persistent allocations use the game's heap; unrelated allocator calls forward normally. The module and detour remain installed until process exit.

PID 79280 returned success on thread 58416 at tick 557886968. The session was nonnull in state 3, with acceptance disabled. The bandwidth manager and job had the expected heap headers. Mother Base loaded on foot with two indexed storage entries and the player constructor's online bit set. Storage entries are not proof of independent playable characters.

Return to title was requested at 00:04:17.483 UTC on September 10 (September 9 local). A title menu was visible by 00:04:32.701 UTC, keyboard navigation worked, and the game quit normally by 00:05:20.525 UTC. Session and manager owner fields were null at the title transition. All seven save/config files match the pretrial backup; the executable and Steam DLL remain unchanged. No forced termination occurred. Null fields and one normal exit do not prove complete asynchronous reclamation, leak freedom or repeat-session support.

[Result and hashes](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/dynamite-host-result-2026-09-09.json), [live trace](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/dynamite-host-live-trace-2026-09-09.log), [player storage](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/dynamite-host-player-storage-2026-09-09.json).

## Admission: loader 0.10.1

The explicit `host-v3` mode wraps the existing mission start/termination functions. After the original start returns, the wrapper invokes `TppNetworkUtil.SessionEnableAccept(true)` only for mission 30050 and this adapter's successfully created host. Before termination it invokes the same interface with false and invalidates its admission permission. The existing `host-v2` mode continues to leave admission disabled.

The native authorization checks the creation thread, original owned session, session table and state, main-session root, virtual acceptance slot, and three complete code bodies. The Lua interface dispatches slot `+0x70` to the native setter, which writes byte `+0xE2`. Requiring an existing main session avoids the accessor's lazy-creation branch. [Twelve instruction checks, table evidence and body contracts](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/session-accept-contract-2026-09-09.json).

The wrapper preserves original arguments, nil return values, errors and coroutine yields. Fixture results: 42 admission checks, 16 exact-source selection checks with 400 concurrent selections, 72 actual-module startup checks, and all 16 native suites. The admission fragment is a marked Dynamite adaptation under its GPLv3 with Commons Clause 1.0 terms; see [notices](../THIRD_PARTY_NOTICES.md).

**Live pass:** PID 36772 created the host on thread 79288 at tick 558912890. After the original mission start returned, the Lua call completed and the native acceptance flag read 1 at tick 558965734. On leaving Mother Base, the wrapper's close call completed and the same session flag read 0 at tick 559034218, before the original termination function returned. The current mission label had already changed to 40050 during termination; closing is intentionally tied to the owned host rather than the destination mission number.

The title menu was visible by 00:17:04.922 UTC on September 10 and keyboard navigation worked. Session and manager fields cleared. During the quit dialog, external input changed; the final confirmation action was not observed. Both the window and process were subsequently absent. No forced termination was performed by the agent. All seven save/config hashes match the fresh backup, with executable and Steam DLL unchanged. [Result](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/host-admission-result-2026-09-09.json), [trace](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/host-admission-live-trace-2026-09-09.log), [player observation](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/host-admission-player-storage-2026-09-09.json), [build checks](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/host-admission-build-2026-09-09.log).

Installed DLL SHA-256: `9B6B21F94508672C1270C048E8A2A5C77A371836FAC2033194FEA3165F9B2A94`. The unchanged two-player archive is `26CE7C2168D655C35BE39E4F17A295206AA0522DE8782CD57A36C4FEEA382F54`. The explicit `host-v3` marker remains installed. This version makes one host-creation attempt per process; another host trial requires a normal game restart. Admission state alone does not prove Steam connectivity or guest acceptance.

## Remaining gameplay work

Next is the native Steam client join path and two-player identity/ownership behavior, followed by shared enemies, interactions and objectives. Only one PC is available, so host development can proceed but real peer gameplay requires another tester or suitable second setup. Reconnects, repeat sessions, save isolation and campaign sequences remain unverified. The current experiment supports no claim of playable hospital, missions or endings.
