# Original Steam lifecycle observation

Evidence date: 2026-09-09. This note concerns passive observation of the installed game's own public Steam calls. It does not authorize additional SDK calls, game-function calls, or networking.

## Independently verified import boundaries

Inspected executable: `D:\Games\steamapps\common\MGS_TPP\mgsvtpp.exe`, SHA-256 `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`, preferred image base `0x140000000`. The import table was decoded directly from file bytes using this project's original PE reader. RVAs below identify IAT data slots, not function entry addresses or absolute runtime addresses.

| Public export | Ordinary import in this executable | Suitable observation |
| --- | --- | --- |
| `SteamAPI_Init` | IAT RVA `0x208e130` | Capture entry, forward once, record the original boolean return or exceptional exit. |
| `SteamAPI_RunCallbacks` | IAT RVA `0x208e0e8` | Forward once; record cadence, calling thread, and return-site RVA. |
| `SteamUser` | IAT RVA `0x208e128` | A potential future observation of a naturally returned opaque pointer; currently not patched. |
| `SteamNetworking` | IAT RVA `0x208e138` | Original 0.4.0 observes natural null/non-null/exception results; it retains and queries no interface pointer. |
| `SteamAPI_Shutdown` | Absent | No checked main-executable IAT slot exists for a shutdown wrapper. |
| `SteamAPI_GetHSteamUser` | Absent | Cannot observe this as a natural call through this executable's ordinary IAT. |
| `SteamAPI_GetHSteamPipe` | Absent | Cannot observe this as a natural call through this executable's ordinary IAT. |

The executable's delay-import directory is zero. This does not rule out dynamically resolved functions, another loaded module's imports, or process-exit cleanup. Export presence in the shipped Steam DLL is separate evidence and does not create an IAT slot in the game.

The observer resolves the named export from the already loaded, path-checked Steam DLL and changes a slot only when the existing pointer exactly matches that export. It checks the running executable fingerprint, unique import, mapped data page, expected target, pointer alignment, and page protection restoration. It pins its own module before publishing a replacement pointer. No absolute function address from another project is used.

## What the public lifecycle means

Valve documents `SteamAPI_Init` as returning true when required interfaces are acquired and accessible. An entry into the function is insufficient evidence. `SteamAPI_Shutdown` releases API pointers and memory, so a future observed shutdown entry must close any query permission before forwarding it. `RunCallbacks` can run concurrently on multiple threads; registered callbacks execute on those callers' threads. [Valve API reference](https://partner.steamgames.com/doc/api/steam_api)

The existing 0.2.0 game log records callbacks on thread IDs `84716` and `52332`, both returning to RVA `0x7ded63`. This establishes multiple callback callers, not one simulation or Lua owner. It is consistent with Valve's documented callback behavior. [Existing observation log](evidence/steam-observer-2026-09-09.log)

Valve's older public API declarations identify the no-argument handle accessors, and the official type reference defines both handles as signed 32-bit values. These handles are not Steam account IDs. The runtime inspector must continue respecting its externally established initialization gate before making those calls. [Valve's published historical API declarations](https://github.com/ValveSoftware/Proton/blob/proton_11.0/lsteamclient/steamworks_sdk_133/steam_api.h), [official handle types](https://partner.steamgames.com/doc/api/ISteamClient)

## Observer 0.3.0 behavior

The original observer now attempts checked IAT observation of both `SteamAPI_Init` and `SteamAPI_RunCallbacks`. Initialization logs contain start/completion, an observation ticket, the original true/false/exception outcome, thread ID, caller RVA, and overlap/loss counters. The forwarding helper restores the incoming Windows error value before calling the original and its outgoing value after diagnostic work. Original C++ exceptions are rethrown unchanged. Structured exceptions or process termination may leave an outstanding observation; they are not reported as success.

Installation still happens after the original DirectInput call returns. The first Steam initialization may already have happened. The install log explicitly starts with `init_observation=unobserved_at_install`, and subsequent callback samples retain `unobserved` if no initialization entry passed through the installed wrapper. No callback count, delay, loaded DLL, non-null symbol, or elapsed play time is treated as a substitute for an observed successful initialization.

The tracker holds its lock only during bounded bookkeeping. It releases it before the SDK call and before event sinks or logging. It tracks up to 16 simultaneous observations, records overflow as lost evidence, and rejects duplicate completion tickets. A completed call is associated with its own ticket even when concurrent calls return out of order. The tracker does not supply `SteamLifecycle::EngineInitialized` or any other authorization to call the SDK.

## Live 0.3.0 snapshot

The captured game log accepts the executable fingerprint and records successful replacements of both the Init slot at RVA `0x208e130` and the RunCallbacks slot at RVA `0x208e0e8`, with page protection restored for each. Callback samples through count 3,600 report two calling threads, `77056` and `107276`, with the second thread observed by count 2. Both return-site observations use RVA `0x7ded63`. [Captured 0.3.0 game log](evidence/steam-observer-0.3.0-2026-09-09.log)

Every captured callback sample retains `init_observation=unobserved`, `init_entries=0`, and `init_true_returns=0`. No natural Init entry or completion passed through the installed wrapper in this captured evidence. This does not establish whether initialization occurred before installation, used another path, or was absent; neither an already initialized SDK nor a failed initialization can be inferred. The explicit installation marker is `init_observation=unobserved_at_install`, and `context_queries=disabled` remains recorded throughout. [Captured 0.3.0 game log](evidence/steam-observer-0.3.0-2026-09-09.log)

This live result validates installation and callback forwarding observation. It does not validate complete lifecycle coverage, SDK context access, a Lua owner thread, networking, or playable co-op. The game log was read without modifying the running process or installed files to produce this documentation snapshot.

## Remaining lifecycle and concurrency gaps

A later, separate debugger startup trial at 16:28:34 UTC captured the game's natural Init call and matching return with `AL=1`, then detached successfully. This supersedes the lack of an Init result for that new launch only; it does not retroactively change the earlier 0.3.0 wrapper log or enable its context queries. See [live call evidence](steam-init-caller-investigation.md#live-natural-call-and-return-1628-utc).

1. **Shutdown coverage is absent.** A captured true Init return is a historical fact, not an indefinite lease on the SDK context. Before enabling queries outside the original call boundary, independently establish every relevant shutdown/unload path and serialization with those transitions. The ordinary main-executable IAT cannot provide this coverage by itself.
2. **Startup can be missed.** A pointer swap cannot capture a call already in progress or a function pointer copied earlier. An earlier installation point would require its own evidence and loader-lock review. Do not replay Init to manufacture an observation.
3. **Callback threads are not ownership evidence.** Queue any future game-state work until a verified game-thread boundary exists. Holding the tracker mutex across the original SDK call could deadlock if that call invokes a callback that re-enters observer code; the implementation avoids that pattern.
4. **Multi-slot installation is not one transaction.** Each Init/callback pointer replacement is independently checked and logged. If only one succeeds, the log describes partial observation. It must not claim complete lifecycle coverage.
5. **Removing a published pointer does not retire active wrappers.** The observer module remains pinned for the process lifetime. File installation/removal is performed only while the game is closed; there is no live unload operation.
6. **Snapshot checks cannot prevent a later shutdown.** A future query adapter needs a proven serialization mechanism across the entire call, not merely a boolean read before calling the SDK. The current observer intentionally makes no context-query calls.

The new tracker/forwarder passed original standalone tests for both boolean outcomes, exception identity, exact-once forwarding, error-state preservation despite noisy instrumentation, duplicate completions, capacity loss, and 1,000 observations across four test threads. The updated Windows observer compiled with warnings treated as errors. These are local implementation checks; the separate live snapshot above contains no captured Init entry or result.
