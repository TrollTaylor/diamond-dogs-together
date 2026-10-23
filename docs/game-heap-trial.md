# Paired game-heap trial

Loader 0.9.8 passed one live allocation/free trial in Mother Base on 2026-09-09, followed by a responsive return to title and normal quit. This verifies the selected heap/free pair at the early mission-thread boundary. Native host creation remains disabled; no co-op gameplay is established.

## Implementation and gates

The explicit eight-byte marker `heap-v1\n` enables a once-per-process experiment through the existing exact TppMain source receipt. The caller verifies the executable fingerprint, host function bytes, mission 30050, native host prerequisites and the selection thread. It observes later player readiness separately: the player object is still null at the early boundary. The heap adapter additionally compares SHA-256 of the three complete mapped allocator/release bodies before calling them.

| Function | RVA | Bytes | SHA-256 |
| --- | --- | --- | --- |
| Heap allocation | DBAB0 | 104 | 2A8E312DA8B88A39A24827E23ADC5A66D6659DB21D5E1E644BB19BEE9FB3F5B9 |
| Free dispatch | DBFD0 | 333 | 6E2D596197C811F405E34605E120E3FF4652D09527B51AC7BBE7A5E7392F8787 |
| Shared block/heap free wrapper | DCBC0 | 22 | EDEE2B02EE2E212432112FF50F7E4585699CAC6085099DF205DDAFEC55D1E189 |

The Win64 allocation call takes size, alignment and tag. The free dispatch takes a status-output pointer and the allocation pointer; it returns that same status pointer. This is the dispatch called by the shared free wrapper. Its verified heap branch recognizes the DEAD marker, subtracts the stored delta and invokes the native deallocator with the stored tag. See [allocation analysis](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/host-allocation-lifetime.md).

The original portable core requests 88 and 144 bytes with alignment 8 and tag 115, retaining both until their headers are inspected. It checks the 16-byte header for delta 16, tag 115, total size plus 16 and marker DEAD, then releases returned pointers exactly once in reverse order. Null allocation or an invalid/unreadable header stops further allocation. Any already returned allocation is still released through its paired allocator, even on a header-check failure. A zero free status and matching return pointer are required. The experiment writes no allocation payload or header, installs no allocator detour and constructs no game object. It does call the native allocation/free routines, which necessarily change their own allocator state. Native faults are not claimed to be recoverable.

Heap mode reuses the existing host receipt selection, so the trace also contains `mode=host_v1`; the additional `mode=heap_v1` line identifies the experiment. This branch always returns false to Lua, preventing it from setting S_IS_ONLINE. The separate [allocation bridge](allocation-bridge.md) is still unlinked from the probe.

## Observed result

PID 95056 started at 23:31:44 UTC with DLL SHA-256 `2FE44C9BE261B0B49D42FACA6D5F63A67999D8DB236D828877896D3CEF0A3712`. At tick 556426484 on thread 87868, both headers matched: totals 104 and 160, delta 16, tag 115, marker DEAD. Both releases returned their status pointers and status zero. The current-block observations before and after were both `0x1A3693F58`; this equality is not an atomic snapshot or complete allocator-accounting proof. Native host calls remained zero.

At initialization entry the current block and player object were null. At mission-start entry the current block was null and reserved player limit was four. Mother Base visibly loaded on foot. Return to title was requested at 23:38:44.247 UTC and the title menu was visible by 23:38:59.451 UTC. Up selected Quit Game, proving menu input. The normal quit completed by 23:39:50.850 UTC. No forced termination or debugger was used.

A bounded read-only check at the returned title matched all 38 mapped bodies and observed null session and bandwidth-manager members. After quit, all seven save/config hashes matched the pretrial backup, and the original executable, Steam DLL, owned probe and four-slot archive matched their expected hashes.

All 16 native suites passed in Release, including 91 portable heap checks. These cover the valid pair, allocation/read failures, each corrupted header field, free failure statuses, return-pointer mismatch and missing callbacks. The existing bridge fixture still passes 1,605 checks. Only documentation and evidence were changed after this build.

Evidence: [live trace](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/game-heap-live-trace-2026-09-09.log), [result and preservation](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/game-heap-result-2026-09-09.json), [post-title analysis](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/game-heap-post-title-analysis-2026-09-09.json), [build](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/game-heap-build-2026-09-09.log), [detailed tests](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/game-heap-ctest-2026-09-09.log), [installation](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/loader-0.9.8-game-heap-install-2026-09-09.json), [backup](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/save-before-game-heap-2026-09-09.json).

## Remaining work

The paired heap call is established for these sizes, alignment, tag and boundary. Complete the remaining host/client allocation inventory, especially the allocations reached through 3130C0 and 7DEBF0, and verify their deallocation/ownership before integrating exact-site routing. Then prepare a separately gated host lifecycle trial with observable creation and teardown. The static [job cancellation map](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/job-lifetime.md) does not yet demonstrate live job completion or bounded cancellation latency. Live peers, independent actors, Steam transport, save isolation and full campaign support remain unfinished.
