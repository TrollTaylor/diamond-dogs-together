# Scoped allocation bridge

The original allocation bridge is implemented as a separate Windows static library and tested with a real MinHook detour of a compiled fixture allocator. It is **not linked into the game probe, installed, or exercised against a game allocator**. Prepared probe 0.9.7 and installed probe 0.9.6 are unchanged by this library.

## Selection and ownership

Within an acquired, thread-affine scope, routing requires an exact match of return address, size, alignment and tag against one of two configured sites. Unscoped calls, other threads without their own scope, other origins, mismatched arguments and allocator reentry use the original trampoline. A nested scope cannot acquire ownership or change its outer scope. Null results are returned without retry. The bridge preserves incoming Windows last-error state at allocator entry and leaves the selected allocator's result intact.

Installation checks bounded expected target bytes before creating or enabling the hook. Executable identity, full function validation, the heap target, site identities, lifecycle and external caller quiescence remain the adapter's responsibility. No game address is embedded in this library. Its plan remains immutable while installed.

Scopes are counted across threads; removal refuses while any scope or dispatch is active. Scope acquisition shares the installation lock so it cannot race removal's ownership update. This does **not** make an idle dispatch counter sufficient to unload: external callers must be stopped before installation/removal, and the bridge module must remain loaded until scopes and callers have stopped and removal succeeds. Failed removal retains owned trampoline state; an already-disabled hook can be removed on a retry. Failure injection for MinHook enable/remove errors is not covered by this fixture.

## Validation, 2026-09-09

All 15 native suites pass. The allocation fixture reports **1,605 checks, zero failures**. It verifies exact routing, wrong bytes, duplicate installation, argument forwarding, alignment, null propagation, reentry, nested scopes, cross-thread scope retention, C++ exception unwinding, 1,000 concurrent allocations split equally between selected and original routes, removal and restored target bytes. Five additional runs of the final fixture all passed.

During development, the expanded single-translation-unit fixture reported four failures; adding failure-line output changed that result. The exact cause was not established. The final target lives in a separate translation unit with whole-program optimization disabled, preventing compiler assumptions across that ABI boundary. Passing the revised fixture does not retroactively explain the earlier result. Retain this as an integration uncertainty rather than treating fixture success as live allocator verification.

The fixture allocates and frees with the CRT aligned allocator. It does not verify FOX allocation headers, FOX freeing, asynchronous job completion, session creation or gameplay. No structured-exception recovery or arbitrary DLL unload is promised.

Evidence: [build](evidence/allocation-bridge-build-2026-09-09.log), [full CTest output](evidence/allocation-bridge-ctest-2026-09-09.log), [five final repeats](evidence/allocation-bridge-repeat-2026-09-09.json). Run `tools/build.ps1 -Configuration Release` to reproduce.

## Game job shutdown mapping

**Follow-up:** the [job lifetime analysis](job-lifetime.md) now connects the shared callback lock, scheduler-owned reference and normal cancellation cleanup. Its 38 mapped bodies match live. The narrower investigation below is preserved as the preceding evidence; live lifecycle behavior remains untested.

The updated [allocation analyzer](../tools/analyze_host_allocations.py) checks 102 instruction predicates, three method-table entries and 22 bounded code bodies on executable SHA-256 `085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45`. All 22 bodies matched the running PID 14496 at 23:09:34 UTC. The mapped session and bandwidth-manager fields remained null; no game calls or writes were made. [Exact-build and live evidence](evidence/host-job-shutdown-analysis-2026-09-09.json).

The manager destructor obtains its job reference, acquires the job lock at offset `0x2c` with compare-exchange from zero to one, clears the manager backpointer at job offset `0x88`, then releases the lock. Its next call sets bit zero at job offset `0x28` when job offset `0x30` is non-null, returning status zero; otherwise it returns status `0x94970407`. Cancellation is the interpretation supported by this destructor context; the scheduler's handling is not yet mapped. The destructor then releases its references. There is no explicit join in this body.

Job method-table slot two points to the wrapper at RVA `0x7e13c0`: a null manager backpointer returns immediately; a non-null pointer tail-dispatches to manager update at `0x7e1430`. The wrapper itself does not acquire the job lock. Verify the scheduler dispatch lock and final reference-release ordering before concluding that clearing the pointer excludes an in-flight callback.

The two proposed allocation sites remain a partial inventory. Subsequent work mapped normal scheduler callback exclusion and passed a [paired FOX heap allocation/free trial](game-heap-trial.md). Remaining session allocation lifetimes and live job cleanup still require verification. Native session creation remains disabled, and this bridge remains unlinked from the probe. Source hints and attribution are recorded in [allocation lifetime](host-allocation-lifetime.md) and [third-party notices](../THIRD_PARTY_NOTICES.md).
