# Native host-session preflight

**Current 0.9.6 behavior: native host creation is disabled in code.** The installed `host-v1` mode runs a bounded allocation-context observation and preflight, then returns false. It also observes readiness at initialization and mission-start entry through the existing mission trace. No host session has been created by this adapter. The alternative `players-v1` mode traces existing mission callbacks.

Prepared 0.9.7 (not installed) separates early host prerequisites from later player readiness based on the [constructor and allocation analysis](player-session-initialization.md). The lifetime gate still unconditionally refuses native host creation. The installed combined 0.9.6 preflight described below remains the latest live result.

The live request immediately before Mission.Start ran on the TppMain selection thread and observed a non-null current block. The precise refusal is now resolved: the player implementation pointer is null, also at OnInitialize entry. The unchanged four-slot preflight passes at OnMissionGameStart entry, when the current block is null. This does not establish that session creation can be deferred past initialization. Mother Base returned to title successfully, menu input worked and all seven saves/configs matched. [Current observation and timing caveat](host-readiness-timing.md).

The [current-build allocation investigation](host-allocation-lifetime.md) now maps both tagged allocations, the thread-local dispatch, heap headers and matching release branch. It passes 63 instruction predicates and two table checks; all 16 bodies match the live process. The session and bandwidth-manager fields are null in the recorded baseline. Allocation context, background-job completion and the complete native session lifetime still require verification before enabling the call.

Loader 0.9.0 introduces a separate `host-v1\n` development marker. This enables the verified archive/player package stages and an exact-source insertion immediately before the unique `Mission.Start()` call in TppMain.OnAllocate. Installed source: 21,634 bytes, SHA-256 `6542d997c3f29742ff7ea0587f7f307794586a9b7a8ee3114b58e79e7f0b6103`. The original script bytes and order are preserved around the insertion.

The original fragment requests one native host attempt only for mission 30050 with successful archive execution and package selection. The loader handles the exact request only on the thread that selected TppMain. The response has process lifetime. Only a Boolean true marks the mission online; refusal, an unhandled request or compilation failure leaves the original pre-start flow in place. No peer-acceptance operation is requested.

The native adapter uses pinned tpp-mod's current-build `CreateHostSession(FobTarget*)` signature and game-owned ServerManager target path. It verifies the complete 456-byte host function hash (`F2711162AE630725D8B8839A74D191184565BB5E5492A0001E197F7CA1C84941`), then requires mission 30050, the known target type with state/flag zero and the known player-system type with index limit four. One attempt is latched per process, including a refused or unsuccessful attempt. After these checks the adapter records `allocation_lifetime_unverified` and returns false without invoking the native function. No connection object is allocated by the mod.

Dynamite's pinned TppMain invokes host creation in OnAllocate before Mission.Start, then marks the mission online. Its native callback uses a constructor-captured target. The current-build tpp-mod source obtains the target from the game-owned ServerManager. The new implementation uses those source contracts, independently decoded current-build bodies and the prior live owner observations; it does not copy an upstream native implementation body. [Source integration](dynamite-integration.md), [current-build bodies](evidence/dynamite-current-build-candidates-2026-09-09.json), [owner observation](live-network-owner-observation.md).

## Original 0.9.0 validation and limits

Current 0.9.6 combined insertion and live validation are recorded in [host readiness timing](host-readiness-timing.md).

All 14 existing native suites pass. The new exact-source check passes 15 assertions and 400 concurrent selections. The Lua fixture loads the actual original and patched TppMain modules and executes their allocation prefixes through Mission.Start: 41 checks cover preserved order, mission/archive gates, false/unhandled responses, compile failure and true-only online status. It deliberately stops at Mission.Start; these are not mission gameplay tests. [Native build](evidence/native-tests-host-trial-0.9.0-2026-09-09.log), [new checks](evidence/host-trial-local-checks-2026-09-09.log).

## Allocation lifetime finding

The pinned Dynamite source at `c90f252f8200765f66c005d6bf4357fda0c0d98e` documents a host-only Hellbound reload failure in `docs/issue_7.md`. Network allocations created from a Lua mission-block context can outlive the block and prevent its recycling. Its allocator hook routes selected later network allocations to the heap. This means a game-owned FobTarget alone does not establish the lifetime of resources created during OnAllocate.

The actual CreateHostSession invocation was removed from this project's adapter. No upstream allocator hook, TLS change or old executable address was ported. A current-build allocation-domain and teardown strategy is required before enabling it. This source finding does not establish the cause of our earlier four-slot return-to-title stall, which occurred without host creation.

The diagnostic 0.9.0 DLL was installed at 21:10:30 UTC with SHA-256 `9AB37A8AF73C6AD37DE3D39F1584CD2BAD188EB4D8E6EF12F0E96FA2B7E53262`. It includes the selection reset and bounded original mission tracing. All 14 native suites pass; trace selection checks pass 15 assertions and 400 concurrent calls, and the Lua trace fixture passes 213 checks. [Trace build](evidence/mission-trace-build-2026-09-09.log), [trace checks](evidence/mission-trace-checks-2026-09-09.log).

The archived installation record's scope string was stale and mentioned a host trial. The executable code already refused host creation in that binary; future installer records now describe this correctly. Historical evidence is retained unchanged.
