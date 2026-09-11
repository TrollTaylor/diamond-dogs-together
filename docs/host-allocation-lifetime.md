# Native session allocation lifetime

The installed 0.9.8 loader still refuses native host creation. It observes thread-local allocation context at the exact pre-Mission.Start request point and two later phases, and its explicit heap mode has passed a bounded paired allocation/free test. The controller-overlap fix and the networking allocation issue are separate. This investigation maps the latter on executable SHA-256 `085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45` without changing the executable file.

**Latest result:** player initialization consumes an early status-derived network bit, supporting early setup with a lifetime fix. A separate allocation bridge passes a real detour fixture, and the expanded analyzer maps normal callback exclusion and scheduler cancellation ownership. The live heap/free pair passed for sizes 88 and 144, followed by return to title and normal quit with all seven save/config hashes preserved. The bridge is not linked into the probe; native host creation remains disabled pending remaining allocation ownership and live lifecycle verification. [Heap trial](game-heap-trial.md), [job lifetime](job-lifetime.md), [bridge](allocation-bridge.md), [player initialization](player-session-initialization.md). The 0.9.5 observations below are historical.

## Live request-point observation

Loader 0.9.5 was installed at 22:26:47 UTC after 0.9.4 quit normally. DLL SHA-256 is `5FA06F24CDE4C4D3C7510A5CCF753523A26393183E64C62936D24D1CED537EBF`; the four-slot archive remains `74BFCD3656356EE4B08EEBCB9912D4806BA0B9F7F948D0C86E6E412D6E930666`. The `host-v1` mode invokes only the still-disabled preflight, with no allocator hook or native session invocation.

In PID 103508 (started 22:26:59 UTC), Continue at 22:28:40 reached the exact inserted request immediately before Mission.Start. TppMain selection and the request both ran on thread 69432. The complete accessor bytes matched, and bounded reads found TLS index 0, TLS storage `0x5949e0`, and current block `0x1a3693f58`, with consistent rereads. This establishes a non-null current block at our proposed creation point. Given the verified allocator branch, an unmodified synchronous allocation through that path would use the block. It does not establish the lifetime of any newly created session resource.

The existing preflight refused with `mission=30050 state=0 flag=0 player_limit=0`. **This does not prove the player descriptor contained zero:** the compound preflight short-circuits, and this output does not identify whether the pointer read, type check, limit read, or value check failed. Improve this diagnostic before changing readiness requirements or moving the creation point. A later external observation at 22:29:27 found descriptor/index limits four, bitset `0f`, and four distinct storage entries. Neither observation demonstrates extra interactive actors.

Mother Base loaded visibly on foot. Return to title was confirmed at 22:30:27; the menu was visible within 16 seconds and responded to Down/Up afterward. All seven save/config files match the pre-test backup; the original executable and Steam DLL hashes are unchanged. A post-title read again found the mapped session and bandwidth-manager members null, with all 16 code bodies matching. The game remains at Continue. No debugger was used.

All 14 native suites pass, and the unchanged exact-source host insertion passes 15 assertions plus 400 concurrent selections. These checks do not validate a heap redirection or host session. [Trial result](evidence/host-context-result-2026-09-09.json), [live trace](evidence/host-context-live-2026-09-09.log), [later storage](evidence/host-context-player-storage-2026-09-09.json), [post-title network fields](evidence/host-context-post-title-network-2026-09-09.json), [build](evidence/host-context-build-2026-09-09.log).

## Earlier static mapping and baseline

At 22:21:03 UTC on 2026-09-09, the new analyzer passed 63 instruction predicates and two deletion-table checks. All 16 mapped code bodies also matched live in PID 94384. The NetworkInfo root was `0x6e72810`; its session member at `+0xcc8` and bandwidth-manager member at `+0xd00` were both null. Both fields and the root matched rereads. These are bounded raw observations, not atomic snapshots or proof of a session lifecycle. No game function, allocator, Steam operation, or UI action was invoked during this inspection.

## What the executable establishes

Both host creation (`0x7ee590`) and client creation (`0x7ee380`) call the same lazy bandwidth initialization routine (`0x7cfa70`) when taking their new-session construction branch. The lazy initializer skips construction if `NetworkInfo+0xd00` is already non-null. Otherwise it constructs an 88-byte bandwidth manager, whose constructor creates a 144-byte job. Both allocation sites use alignment 8 and tag `0x73`.

| Operation | Current-build RVA | Verified behavior |
| --- | --- | --- |
| Manager allocation call | `0x7e1319` | Size 88, alignment 8, tag 115; return at `0x7e131e` |
| Job allocation call | `0x7e135d` | Size 144, alignment 8, tag 115; return at `0x7e1362` |
| Context-sensitive allocation | `0xdcb50` | Uses current block when non-null; otherwise tail-dispatches to heap allocation |
| Current block accessor | `0xe63e0` | Reads TEB `+0x58`, game TLS index at RVA `0x2c6ba70`, then TLS storage `+0x48` |
| Block tail allocation | `0xdbc20` | Receives block pointer, size, alignment and tag |
| Heap allocation | `0xdbab0` | Returns a pointer with a 16-byte header and marker `0xdead` at pointer minus 4 |
| Shared free wrapper | `0xdcbc0` | Dispatches through `0xdbfd0` |
| Heap free branch | `0xdc0ba` | Recognizes `0xdead`, recovers the original allocation base and tag, then calls the underlying deallocator |

The manager's deleting destructor and the job's deleting destructor both call that shared free wrapper. The manager destructor clears the job's manager back-reference and releases its job reference. Its intervening job operations are not yet behaviorally verified, so this is not evidence that asynchronous teardown has completed safely.

The heap and context-sensitive allocators share the Windows x64 argument layout: RCX size, RDX alignment, R8D tag, with the pointer returned in RAX. The block-tail function has a different layout because it also receives the block pointer. The complete fingerprint and body checks are required; old Dynamite addresses must never be substituted.

## Integration decision

The [job lifetime follow-up](job-lifetime.md) now establishes the normal callback lock and scheduler reference-retirement paths. This resolves the corresponding static uncertainty below. Remaining work is the other allocation inventory, live heap/free validation and a controlled session lifecycle trial.

Dynamite's pinned issue_7 accurately identifies this class of allocation risk, and its current hook sends later network-tagged allocations to the heap after skipping the first one. The two independently mapped call sites offer a more precise candidate for this project: restrict any future redirection to these allocation origins and verified parameters during an explicitly owned session-creation scope. Do not depend on the first network-tag allocation being a particular constructor.

The generic routing library is implemented and fixture-tested, but the game adapter is **not connected or installed**. A restricted route would not establish that all session allocations are safe. Before enabling host creation:

1. The request-point context and failed stage are observed: a null player implementation before Mission.Start. The [player constructor and allocation analysis](player-session-initialization.md) now shows status-derived selection of network setup. Prepared 0.9.7 separates session prerequisites from actor readiness and retains early setup as the candidate; no new host call is enabled.
2. Verify the remaining session construction and background-job paths, including allocations outside these two sites. Decide whether a scoped route or an existing engine memory scope provides the complete required lifetime.
3. Generic allocator forwarding and cleanup now pass an isolated real detour fixture. Game heap headers and game freeing still need verification; the CRT fixture does not establish them. Preserve unselected allocations, null results, alignment, tag and original freeing behavior.
4. Run a controlled host session trial with peer acceptance disabled, inspect the actual allocation headers and session state, then verify mission reload, return to title and normal quit. A successful creation return alone is insufficient.
5. Only then enable authenticated peer joining and prove a second independently interactive actor before expanding to four.

## Reproduction and attribution

```powershell
C:/Python314/python.exe tools/analyze_host_allocations.py --output build/host-allocation-analysis.json
# Optional read-only baseline against a freshly verified running MGSV PID:
C:/Python314/python.exe tools/analyze_host_allocations.py --pid <pid> --output build/host-allocation-live.json
```

The tool records complete disassembly and hashes for each bounded body, checks specific dataflow predicates, and optionally compares every body to live memory before following the singleton. It uses only query/read process rights and reads at most 256 bytes per request. Non-null manager/job header observation has not been exercised by this baseline. A null member must not be interpreted as a working or available transport.

The source hints were Dynamite `c90f252f8200765f66c005d6bf4357fda0c0d98e`, `docs/issue_7.md` and `hook/src/DynamiteHook.cpp` (GPLv3 plus Commons Clause 1.0), and tpp-mod `301bf797b7f8032941b21ed56a3570338de6ead4`, `NetworkInfo_::m_instance` (GPLv3). This is project-authored executable analysis; no upstream native hook body was incorporated. See [third-party notices](../THIRD_PARTY_NOTICES.md).

Evidence: [static analysis and live baseline](evidence/host-allocation-analysis-2026-09-09.json). This advances the current-build networking port; no additional actor, remote peer, Steam traffic, save isolation, or campaign support is demonstrated.
