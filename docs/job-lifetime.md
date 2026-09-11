# Bandwidth job ownership and cancellation

The normal dispatch and cancellation paths are now mapped on the installed executable. The callback and manager destructor use the same job lock. The scheduler keeps a separate job reference from registration until cancellation processing retires the job. This resolves the missing static ownership connection; it does **not** prove that a live session starts, cancels promptly or survives a mission transition.

At 23:22:33 UTC on 2026-09-09, the new analyzer passed 103 job instruction checks and three additional method-table checks, plus the existing 102 allocation checks and three table checks. All **38 bounded code bodies** matched live in PID 14496. The session and bandwidth-manager fields were still null. No game functions, allocator hooks or input actions were invoked. [Recorded analysis](evidence/job-lifetime-analysis-2026-09-09.json).

## Ownership chain

| Phase | Exact-build evidence | Consequence |
| --- | --- | --- |
| Construction | Job allocation calls derived constructor `0x1a3bd20`, then base constructor `0x32070`, which initializes reference count, lock and scheduler pointer to zero. | Field interpretation is connected to this actual job type. |
| Registration | Manager constructor calls `0x3af70` then `0x2c530`. Success sets state 1 and scheduler pointer `+0x30`, increments reference count `+8`, then inserts job node `+0x38` into scheduler queue `+0x200`. | Scheduler holds a reference independently of the manager. |
| Execution | Worker `0x32bb0` obtains a job and calls `0x32370`. That dispatcher acquires lock `+0x2c`, invokes slot 2, then releases the lock. Slot 2 is bandwidth wrapper `0x7e13c0`. | Normal callback execution is serialized with manager-pointer clearing. |
| Manager destruction | `0x7e0dd0` acquires the same lock, clears job `+0x88`, unlocks, requests cancellation and drops its references. | An executing callback finishes before pointer clearing. Later normal dispatch reads null and skips manager update. |
| Execution finished | Worker calls `0x2e1f0` after the callback returns. This marks state 5 and enqueues the node in scheduler queue `+0x140`. | Finished processing is separate from the callback. |
| Cancellation consumed | Scheduler `0x2e660` checks bit 0 of job `+0x28`. Finished jobs without termination flags return to state 2; cancelled jobs take removal. Pending queue `+0x200` also checks cancellation before promotion. | Cancellation is asynchronously consumed, not a join. |
| Retirement | Removal clears request bit, state and scheduler pointer, invokes slot 4, then decrements the scheduler reference. Prior count at most 1 dispatches slot 1 to slot 0 with deletion flag 1. | The job can be deleted after the manager has gone, using its own lifetime and mapped free wrapper. |

Concrete registration slot 3 (`0x4ff9e0`) returns success zero. Retirement slot 4 (`0x645940`) immediately returns. Release slot 1 (`0x96ead0`) checks null and dispatches slot 0 with deletion flag 1; its separate null-return branch is included in the analyzer. Slot 0 is the mapped job deleting destructor at `0x5918c0`.

Completed-job cancellation branches at `0x2e6fd`/`0x2e701`; its scheduler-pointer clear is at `0x2e764`, then reference decrement at `0x2e775`. Pending-job cancellation branches at `0x2e872`/`0x2e877`; its clear and decrement are at `0x2e8d8` and `0x2e8e9`. These are distinct paths, not inferred from matching offsets alone.

## Integration decision

Retain the candidate that creates the session before player construction and routes the two verified persistent allocations to the heap. The callback-lock and scheduler-reference relationship is now statically explained. The [0.9.8 paired game heap/free exercise](game-heap-trial.md) subsequently passed, including a responsive return to title and normal quit. The next practical prerequisites are the allocation inventory outside those sites and connection of the tested bridge with exact-build checks. Native host creation remains disabled.

Keep manager and job on their normal game-owned lifecycle. Do not manually delete a job on cancellation or clear its scheduler pointer from the mod. A controlled session trial must inspect actual heap headers and session state, then verify mission reload, return to title and normal quit. A successful creation return or static cancellation branch is insufficient evidence of complete cleanup.

Queue helper internals, dependency teardown, process shutdown and exceptional control flow are not exhaustively verified. This establishes normal-path ordering; there is no measured cancellation latency or live non-null manager/job observation yet. It does not justify enabling peer acceptance, remote actors or campaign support.

## Reproduction and attribution

```powershell
C:/Python314/python.exe tools/analyze_job_lifetime.py --output build/job-lifetime-analysis.json
# Optional: use a freshly verified running game PID for bounded read-only comparison.
C:/Python314/python.exe tools/analyze_job_lifetime.py --pid <pid> --output build/job-lifetime-live.json
```

The tool requires executable SHA-256 `085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45`, records body hashes/disassembly and checks explicit instructions and table entries. Its sampler compares every mapped body before following known NetworkInfo fields, using query/read rights and bounded reads. Equal rereads are not an atomic snapshot.

Project-authored analysis extends the investigation informed by pinned Dynamite `c90f252f8200765f66c005d6bf4357fda0c0d98e` and tpp-mod `301bf797b7f8032941b21ed56a3570338de6ead4`. No upstream native function body was incorporated. See [attribution](../THIRD_PARTY_NOTICES.md), [bridge](allocation-bridge.md) and [allocation lifetime](host-allocation-lifetime.md).
