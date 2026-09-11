# Saved-marker loading guard

Workspace 0.10.24 adapts DYNAMITE's temporary marker-loading guard. Upstream TppMain describes hangs when native saved user-marker restoration runs during co-op loading, including initial loading and checkpoint recovery. The guard starts before the original `OnAllocate`, drops native fixed/follow/remove requests, and ends after the original `OnMissionCanStart` returns. It also ends before termination, during native gameplay-rule cleanup and before each new allocation. Dropped follow-marker requests return true, matching upstream. Requests are not queued or replayed, and shared marker networking is not implemented.

The appended Lua lifecycle uses exact source/name receipts on the captured script owner thread. Activation requires a configured host/client mode, mounted archive, allocation mode, native mission code 30050 and completed pending rule restoration. The Lua caller also requires the selected player package and archive-read result. The existing player preparation cannot proceed unless the native marker guard is active. A declined host/client setup releases the guard and clears the Lua flag. Ordinary missions retain original marker behavior.

The exact original TppMain source transform additionally wraps its unique `TppMarker.RestoreMarkerLocator()` call with a condition on the active guard flag, preserving that call outside guarded loading. This implements the separate Lua checkpoint path skipped by DYNAMITE. The original source remains fingerprinted at 21,634 bytes, SHA-256 `6542D997C3F29742FF7EA0587F7F307794586A9B7A8EE3114B58E79E7F0B6103`. All insertion anchors must be unique and ordered. The transform was compiled but not executed this turn.

Native mapping on the pinned executable:

| Method | RVA | Complete bytes | Interface entry |
| --- | --- | --- | --- |
| Fixed user marker | 0x5C53A0 | 325 | 0x21ADE40 |
| Follow user marker | 0x5C5530 | 448 | 0x21ADE50 |
| Remove user marker | 0x5C6250 | 60 | 0x21ADE58 |
| Remove all, mapping support only | 0x5C6190 | 99 | 0x21ADE60 |

The named `RemovedAllUserMarker` binding reaches wrapper 0x8D6090, which dispatches interface slot +0x70 through context+0x98->+0x80. Constructor/destructor code initializes table 0x21ADDF0. The same table's placement/removal methods share the marker record ring, count and capacity. Placement copies xyz from RDX; follow additionally takes object ID in R8W and returns Boolean AL. Its 448-byte logical function spans three contiguous unwind ranges, all recorded; using only its first 99-byte range would miss the actual placement paths. Remove consumes EDX index. All three complete method bodies and four interface entries are checked before hook installation.

All hooks are created before enabling, with policy inactive until all three enable successfully. Partial enabling preserves original forwarding and retains trampolines. The atomic guard is process-wide during loading and affects naturally invoked marker methods on any thread, as upstream's loading policy does. It does not retain marker-system object pointers. Disabling the policy does not remove potentially executing trampolines.

Limitations: the original Lua lifecycle functions retain yields/errors and are not wrapped in protected execution. If original allocation/startup throws before readiness, the guard can remain active until a subsequent cleanup/termination/allocation boundary. No runtime lifetime, marker behavior, loading-hang recovery, connected guest or shared gameplay is verified. No tests, Lua execution, installation, game launch or VM launch occurred.

Provenance: DYNAMITE `c90f252f8200765f66c005d6bf4357fda0c0d98e`, `TppMain.lua` and the three `DynamiteHook.cpp` marker guards; GPLv3 + Commons Clause 1.0, retained in `third_party/dynamite/LICENSE`. [Static contract](evidence/marker-loading-contract-2026-09-10.json), [prepared build](evidence/marker-loading-prepared-2026-09-10.json).

The same source audit found that current pinned DYNAMITE still calls `TppRevenge.DecideRevenge` and `SetUpEnemy`. Its older development narrative is therefore not sufficient evidence for deleting these calls. Exact standalone TppRevenge/TppMarker extraction requests returned no entries in the inspected archives; that is a limitation of this extraction path, not proof those modules are absent. No blanket revenge-system bypass was added.
