# Natural player-record writer observation

2026-09-09. **The original observer captured position-record writes through updater RVA `0x12CE870`, bypassing the previously inspected vector setters.** Two unpaused Mother Base observations stopped after its `movaps [rdi+0x10],xmm3` store at `0x12CF266`. This supplies a concrete source path to investigate. Actor ownership, complete transforms, persistent hooks and playable co-op remain unverified.

All game trials used PID `106392`, started `16:41:22.7889335 UTC`, with original loader 0.4.0. Executable SHA-256: `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`. Addresses are RVAs relative to actual image base `B=0x140000000`. Only original tooling, public debugger APIs and independently inspected installed-game instructions were used. No Dynamite code is used.

## Live results

| Trial completion UTC | Scene / mode | Result |
| --- | --- | --- |
| 17:37:58.828 | Unpaused, prone; first write | Bank 1, raw RIP `0x53CB7D`: known bank-copy path |
| 17:41:44.141 | Paused; copy filter | 121 copy hits, bounded abort and clean detach; no other writer captured |
| 17:42:04.853 | Resumed, stationary prone; filter | Bank 0, raw RIP `0x12CF26A`; zero copy skips |
| 17:45:10.855 | Unpaused, stationary prone; expanded registers | Bank 1, same raw RIP; zero copy skips |

Successful captures reported thread `0x81B4` (33204), one receipt, debugger exit zero and detachment. The capped paused trial also detached and continued. Native UI verification showed the scene afterward; Escape opened the pause menu. These observations do not establish cadence or the only writer/thread.

The [trial index](evidence/vector-observer-2026-09-09/index.json) identifies archived results and matching debugger logs. Principal evidence: [first updater capture](evidence/vector-observer-2026-09-09/game-record-writer-first-result.json), [expanded registers](evidence/vector-observer-2026-09-09/game-record-writer-owner-registers-result.json). JSON `utc` is completion time, not precise hit time.

## Independently mapped source path

The [static report](evidence/player-record-writer-static-2026-09-09.json) checks 24 instructions across five spans. Reproduce with `python tools/inspect_player_record_writer.py --output docs/evidence/player-record-writer-static-2026-09-09.json` using the existing local Capstone dependency. It only reads the executable.

The PE exception range containing the updater is `[0x12CE870,0x12D0C24)`. The observed store is aligned when decoded from this entry. Full function semantics and embedded branch tables have not been reconstructed.

1. Caller `[0x12CC550,0x12CC5E1)` passes its receiver minus `0x20`, calling the updater at `0x12CC57C`. A bounded installed-file scan found this one aligned direct call; indirect references are not excluded.
2. The updater keeps its receiver in `R14`, loads `[receiver+0x50]`, then `[that+0x60]` into `R13`. Its natural virtual call at `0x12CE8FD`, slot `+0x10`, returns an object retained at `[rbp-0x80]`. The concrete method and lifetime remain unresolved.
3. Another interface at `[R14+0x58]` supplies the destination: slot `+0x8` is called at `0x12CEA09` with an index in `EDX`; `RDI` retains the result. This dataflow is not permission to invoke it.
4. Before the captured store, `RBX = [returned_source_object+0x20] + RSI*16`. Scalar loads from `RBX+0,+4,+8` supply the three components packed into `XMM3`, then stored at `[RDI+0x10]`. The next instruction at `0x12CF26A` starts reading `RBX+0xC` for later computation. That fourth source float is not established as the stored vector's fourth component.

| Expanded capture | Value | Limit |
| --- | --- | --- |
| RDI | `0x142BD4DF0` | Bank 1 record 0; plus `0x10` matches watched `0x142BD4E00` |
| RSI | `0` | Source-array index for this iteration, not actor or Steam identity |
| RBX | `0x160CBFFA8` | Address the game had loaded; observer did not dereference it |
| RAX | `0x160CC0020` | Static span retains source-accessor result; no type/lifetime proof |
| R13 | `0x160CC00D0` | Opaque source-side interface |
| R14 | `0x160CB9870` | Opaque updater receiver |

The bank-copy receipt maps to store `0x53CB79`, `movaps [rcx-0x70],xmm1`, followed by raw RIP `0x53CB7D`. Captured destination arithmetic matches bank 1's position address. The publisher flips the selector before copying to the other bank. Its activity while paused cannot establish interactive-actor readiness.

## Original bounded observer

Tools: `generate_player_vector_trace.py`, `observe_player_vector.ps1`, and `player_vector_fixture.cpp`. The runner verifies PID/path/start time, executable hash, image identity, relevant live instruction bytes and unused breakpoint IDs. Write mode requires local index zero and bank 0/1 before arming. It watches four aligned bytes at `B+0x2BD4000` and `B+0x2BD4E00`. Other indices and non-overlapping writes are outside this observation.

Microsoft documents `ba w4` as a four-byte hardware write watch, subject to processor overlap behavior: [Break on Access](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/ba--break-on-access-). This attach-time tool does not install startup software breakpoints, patch game instructions, call setters or open network sessions. Default capture records opaque source/receiver registers; the later optional owner mode reads bounded headers as described below. It briefly stops the process and changes debug-register state.

```powershell
# Verify the current PID; this number identifies the historical trial.
./tools/observe_player_vector.ps1 -TargetProcessId 106392 -WatchPublished -DeadlineSeconds 10
./tools/observe_player_vector.ps1 -TargetProcessId 106392 -WatchPublished -SkipBankCopy -DeadlineSeconds 10
```

Capture detaches immediately. The filter skips only the fingerprinted copy instruction with the matching destination, resumes through at most 120 matches and detaches on the 121st. The separate 2–30 second deadline uses the verified-interrupt detach helper. It never force-terminates MGSV. Recovery coordinates remain private in the build run folder if completion fails.

Fixture trials cover both setter entries, both write banks, both filtered banks, wrong bytes, deadline and copy-limit cleanup. They verify original arguments/data, continuation, disabled debug-register enables and unchanged inspected function bytes. Results identify different fixture revisions by hash; these are not game clients. Final Release build: all 13 CTest suites pass, in the [build log](evidence/native-tests-with-vector-copy-filter-2026-09-09.log).

## Failed and limited trials

- Setter watches recorded no calls during paused 10-second, stationary 10-second and third 30-second windows. The third detached at **17:29:42.097 UTC**; Space was sent at **17:29:48.796 UTC**. The dive missed the window: **no movement-time setter result**. The observed direct writer does not prove setters are never used.
- An early fixture timeout raced queued post-resume cleanup against the deadline helper (`wait_for_break`, HRESULT `0x8001000a`). The fixture continued with passing checks, but the runner failed. Removing queued commands after `g` fixed it; subsequent trials verified interrupt/detach. The [failure log](evidence/vector-observer-2026-09-09/earlier-timeout-race-debugger.log) is retained.
- The first game write attempt at 17:36:56 hit CDB's command-line length limit, never armed and detached. Its old `aborted=false` reflects missing error-marker parsing; `armed=false` and `capture_valid=false` exclude it. Setup now uses short, independently latched checks and explicit failure markers. This is invalid, not a no-hit observation.
- One fixture launch refused a path/start-time comparison before attachment. The original error lacked comparison values; diagnostics now include both. A fresh trial passed. The original cause remains unknown; the gate was not weakened.

All seven save/config hashes match the preceding post-motion check. Game executable, Steam DLL and installed loader also match; no debugger remained at 17:45:48 UTC. See [preservation check](evidence/save-check-after-vector-writer-2026-09-09.json). This does not undo the earlier ordinary arrival autosave or establish save isolation.

## Next integration gate

### Later owner-header capture

At 17:58:12 UTC the optional `-WatchPublished -SkipBankCopy -CaptureOwner` mode captured one recognized natural write and bounded owner headers, then detached successfully. The [result](evidence/player-owner-2026-09-09/result.json) and [debugger trace](evidence/player-owner-2026-09-09/debugger.log) retain the evidence. Private debugger recovery coordinates are excluded.

Primary/secondary updater tables matched RVAs `0x23D6228`/`0x23D6258`. The source interface table was `0x23AD970`, accessor `0xFDEEF0`, and the captured source array matched the writer's indexed pointer. Static inspection shows the accessor returns `[RCX+0x1E8]`; this is a descriptor accessor, not a complete actor snapshot. The source interface is a secondary subobject. The new mode validates recognized tables, source/store bytes, pointer readability and graph relationships before bounded reads. `source_dereferenced=true` records enabled header reading; `source_vector_read=false` distinguishes it from reading source vector contents.

This mode passed a live capture and syntax/generation checks. The earlier fixture cases cover capture and cleanup machinery, not independent exercise of this new owner graph. Construction, mission transitions and teardown still need runtime verification. No ownership lifetime is established by a single stopped-frame graph.

### Integration direction

Resolve the updater's construction/destruction, its `+0x50/+0x58` interfaces, concrete source accessor and array owner. Observe ownership across spawn, mission transition and teardown before retaining pointers or installing a persistent bridge. Then prove a second independently controlled actor with physics, equipment and damage. Local records or a 16-entry array do not establish allocatable players. Steam authentication, live transport, remote actors, save isolation and every campaign segment remain unfinished.

The user's subsequent authorization to reuse Dynamite changes the next investigation: use the [pinned source and current-build map](dynamite-integration.md) to identify these objects and accelerate actual engine integration. Earlier independent evidence remains useful validation.
