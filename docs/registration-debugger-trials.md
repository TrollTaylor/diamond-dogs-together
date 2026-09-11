# Registration debugger fixture trials

Evidence date: 2026-09-09. These trials exercise this project's original `registration_fixture.exe`. This document covers fixture validation; live MGSV observations are recorded separately. CDB version: `10.0.28000.2270 AMD64`.

## Accepted fixture result

The generated command captures a natural call to `FixtureRegistrar` on a newly created worker thread, then executes `.detach /h` and `q` on separate lines. This completed the fixture harness in about 0.6 seconds. No single stepping is used in the accepted command. Microsoft documents `/h` as continuing an outstanding debug event with handled status. [Microsoft detach reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/-detach--detach-from-process-)

The captured line was:

```text
MGS_REGISTRATION_ENTRY tid=0x9960 state_opaque=0000000012345678 caller=00007ff7656c100e entry=00007ff7656c1040
```

The fixture recorded `passed=true`, one original call, preserved argument, return value `37`, debugger detached, DR7 enable bits clear, and unchanged registrar entry bytes. The return-event wait completed with `wait_result=0`; the later inspection's `previous_suspend_count=0`. The worker's sampled RIP was `0x7ff956620404`. Addresses and thread IDs describe this run only.

The command still uses one temporary software breakpoint at the verified executable entry to install the later hardware breakpoint. CDB restores the bootstrap byte before continuing. This is an observation with temporary in-memory writes; it is not hardware-only or free of process changes. No executable file bytes are patched on disk.

## Optional paired entry and return capture

`generate_registration_trace.py --capture-return` adds a `.return.cmd` sibling to the existing initial, bootstrap, and entry command files. The default entry-only mode retains its existing command path. The fixture harness enables paired mode with `-CaptureReturn`.

At the first natural registrar entry, paired mode stores the entry RSP, opaque RCX argument, thread ID, and stack return address in debugger pseudo-registers. It then arms a persistent hardware execution breakpoint at that return address. Breakpoint IDs 0, 1, and 2 must be free during initial setup; ID 2 is checked again immediately before installation. No Lua state is dereferenced, and no game function is called.

An accepted return must have the captured thread ID and `RSP = entry RSP + 8`. The MGSV-specific branch additionally requires `RBX = entry RCX`, the independently identified return address `image base + 0x1A12F54`, and the expected `85 C0` instruction bytes. Those bytes are checked in the selected executable and again during observation. The fixture branch omits the RBX condition. These game-specific conditions describe the generated guard; the fixture cannot validate their live MGSV behavior.

The return breakpoint remains installed when another frame reaches the same address. Its command reports at most eight `MGS_RETURN_FRAME_SKIPPED` lines, then uses `gc` to continue the unmatched frame. A matched frame logs raw EAX, clears the owned return breakpoint, and executes `.detach /h` followed by `q` on separate lines. Microsoft documents conditional breakpoint commands using `.if` and `gc`; this implementation preserves the installed CDB's working, uncondensed `$$<` command-file form. [Conditional breakpoint reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/setting-a-conditional-breakpoint), [gc reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/gc--go-from-conditional-breakpoint-)

The normal paired fixture passed in about 0.70 seconds. Its [saved log](evidence/registration-fixture-return-2026-09-09.log) contains one entry followed by one accepted return with `eax=37`; the accepted return RSP is exactly eight bytes above the captured entry RSP. Its [saved result](evidence/registration-fixture-return-2026-09-09.json) separately confirms one original call, preserved argument, result `37`, successful continuation after detach, clear DR7 enables, and unchanged registrar bytes. This trial preceded the nested-fixture rebuild, so its generated fixture addresses differ from the later trial.

## Nested return-frame proof

The rebuilt fixture adds a non-inlined `InvokeRegistrar(state)` function with a volatile result, preserving a real call and return instead of allowing a tail call. In `--nested-return` mode, the first `FixtureRegistrar` invocation recursively calls that same wrapper once. The inner invocation therefore returns through the exact same call instruction's return address, on the same thread, with a lower RSP before the outer invocation returns. The fixture expects two original calls and independently checks that both results are `37`.

The harness command is:

```powershell
./tools/check_registration_fixture.ps1 -Scenario NestedReturn -CaptureReturn
```

`NestedReturn` refuses to run without `-CaptureReturn`. The accepted [nested log](evidence/registration-fixture-nested-return-2026-09-09.log) records:

```text
MGS_RETURN_FRAME_SKIPPED tid=0x15904 expected_tid=0x15904 rsp=000000e63f6ffa40 expected_rsp=000000e63f6ffaa0
MGS_REGISTRATION_RETURN tid=0x15904 state_opaque=0000000012345678 entry_rsp=000000e63f6ffa98 return_rsp=000000e63f6ffaa0 return=00007ff604951009 eax=37
```

This establishes that the same-thread inner return was skipped because its stack frame did not match, and the persistent breakpoint subsequently captured the outer return. The harness checks that the skipped event occurs before exactly one accepted EAX value of `37`. The [nested result](evidence/registration-fixture-nested-return-2026-09-09.json) records `passed=true`, `original_calls=2`, `nested_inner_result=37`, `nested_result_verified=true`, preserved arguments and registrar bytes, debugger detachment, and clear DR7 enables. Thread IDs and addresses are specific to the recorded run.

## Paired-mode failure checks and limits

The paired `WrongBytes` trial passed after the nested-fixture rebuild. The deliberately incorrect bootstrap entry-byte comparison produced `MGS_ABORT restored_entry_bytes_mismatch` before any registrar entry or return capture. Its [saved log](evidence/registration-fixture-return-wrong-bytes-2026-09-09.log) shows the refusal; its [saved result](evidence/registration-fixture-return-wrong-bytes-2026-09-09.json) confirms the original function still ran once with argument and result preserved after detachment.

The coordinator also reported a passing paired-mode `NoHit` run with the stricter timeout helper's `interrupt_verified=1` result. The preserved [NoHit log](evidence/registration-fixture-no-hit-2026-09-09.log) and [result](evidence/registration-fixture-no-hit-2026-09-09.json) are from the earlier entry-only run described below; they support the shared deadline-recovery path and do not independently record use of `--capture-return`. In this scenario the function is deliberately not entered before the timeout, so the return breakpoint is never armed.

### Timeout after entry with the return breakpoint armed

The later `ReturnTimeout` scenario exercises recovery while the persistent return breakpoint is installed. The original fixture registrar records that it is running under the debugger and waits inside its function body until detachment before returning `37`. The wait has a 45-second fixture safety limit. The harness requires `-CaptureReturn` and defaults this scenario to a two-second observation deadline.

The accepted invocation was:

```powershell
./tools/check_registration_fixture.ps1 -Scenario ReturnTimeout -CaptureReturn -DeadlineSeconds 2
```

The [saved debugger log](evidence/registration-fixture-return-timeout-2026-09-09.log) records one `MGS_REGISTRATION_ENTRY` and `MGS_HARDWARE_REGISTRAR_RETURN_ARMED`, with no accepted `MGS_REGISTRATION_RETURN`. At the two-second deadline, the harness reported strict-helper detachment for `target_pid=14124` with `interrupt_verified=1`. This helper report came from the harness output, rather than the saved CDB log.

The [saved result](evidence/registration-fixture-return-timeout-2026-09-09.json) records `passed=true`, `return_timeout=true`, `return_wait_entered=true`, `resumed_after_detach=true`, and `return_timeout_verified=true`. It separately confirms one original call, the preserved argument, result `37`, successful event completion, debugger detachment, clear DR7 enable bits, unchanged registrar bytes, and zero prior worker suspend count. The original function therefore completed after cleanup without requiring an observed return hit. This validates the tested timeout-after-entry path in the fixture, subject to the interrupt-ownership and exclusive-controller limits below.

The same Release rebuild passed all nine CTest suites. The nested-return regression also passed after the fixture change, preserving the inner-frame rejection and outer return capture; its [saved log](evidence/registration-fixture-nested-return-after-timeout-2026-09-09.log) and [result](evidence/registration-fixture-nested-return-after-timeout-2026-09-09.json) retain that regression evidence.

All nine CTest suites passed in the latest build; the debugger fixtures above were separate harness runs. The local [CTest log](../build/native/Testing/Temporary/LastTest.log) may be replaced by later builds. Passing these tests establishes the recorded observer and continuation behavior in the original fixture. It does not establish a usable Lua VM, owner-thread access rights, VM lifetime, actor identity, save isolation, synchronized gameplay, or campaign co-op. EAX is recorded as a raw return register; these trials do not assign it a game-specific meaning. The nested trial exercises the stack-frame rejection branch on the same thread; rejection of a competing thread and the MGSV RBX guard remain untested by this fixture.

## Rejected trials and diagnosis

| Exit sequence at the hardware hit | Observed result |
| --- | --- |
| Direct `qd` | The fixture timed out with zero original calls and no argument/result. The debugger was detached, DR7 was clear, and the worker had no previous suspend count. |
| Standalone `t`, then `qd` on the next script line | CDB reported an implicit-wait command-file failure and stopped at `FixtureRegistrar+0x7`. The following detach line did not run. |
| `t 1 "qd"` | A later fixture result contained the expected original call/result, but CDB exceeded the harness's 20-second exit deadline. This was not a harness pass. |
| `.detach /h`, then `q` | Natural call and full harness completion passed as recorded above. |

The direct-`qd` failure sampled worker RIP `0x7ff956623214`, with ntdll base `0x7ff9564c0000`. Independent parsing of the installed `ntdll.dll` export table places this at `NtRaiseHardError+0x14` (export RVA `0x163200`). This narrows the failure to an error-reporting path, rather than a leftover thread suspend count. An outstanding debug-exception handling interaction is a plausible explanation; the exact exception and internal CDB mechanism were not captured and remain unproven.

The added wait status, suspend count, and worker RIP distinguish this failure from an ordinary fixture timeout. Clear DR7 and `IsDebuggerPresent()==false` alone did not establish successful continuation. Microsoft's trace command supports a post-trace command, but that alternative did not satisfy this harness's exit deadline. [Microsoft trace reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/t--trace-)

## Scope and reproducibility

Sources: [original generator](../tools/generate_registration_trace.py), [fixture](../tests/registration_fixture.cpp), and [harness](../tools/check_registration_fixture.ps1). Local build outputs are [debugger log](../build/registration-fixture-debug.log) and [accepted result](../build/registration-fixture-result.json); later runs may replace them.

This validates the matched fixture hit and detach path. It does not establish live game registration timing, a valid Lua VM, owner-thread identity, VM lifetime, or networking. No live game debugger launch or attachment was performed for these trials. The machine also injected Windhawk DLLs into the fixture process; no causal role in the failure was established.

## No-hit deadline recovery

The added `NoHit` scenario deliberately waits for the debugger to detach before making its first registrar call. At the tested two-second deadline, the original stop client connected to the existing CDB server using its per-run named pipe/password, verified exactly one target with the expected PID, and requested `SetInterrupt(DEBUG_INTERRUPT_ACTIVE)`. It waited for a confirmed debugger break before executing `.detach /h` and verifying `DEBUG_STATUS_NO_DEBUGGEE`. The harness then sent `q` through the retained server input stream. No direct target breakpoint injection, debugger termination, or target termination was used in this successful path. [Microsoft interrupt contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dbgeng/nf-dbgeng-idebugcontrol-setinterrupt)

The complete trial finished in 2.57 seconds. Its [saved result](evidence/registration-fixture-no-hit-2026-09-09.json) records `resumed_after_detach=true`, one original call, preserved argument, return `37`, a completed event wait, zero prior suspend count, clear DR7 enables, and preserved registrar bytes. The [saved debugger log](evidence/registration-fixture-no-hit-2026-09-09.log) shows the hardware observer armed and no registration-entry capture. Capture and deliberate wrong-byte scenarios also passed after adding this recovery path.

The stop client uses the installed Windows SDK's declared DbgEng interfaces and the installed debugger's DLL. It connects using `server=.`; the `localhost` form failed with access denied in this environment. While the target runs, PID verification uses `GetProcessIdsByIndex`, since current execution-context queries failed. The wrapper bounds the helper process lifetime and never kills CDB or its target if detachment cannot be confirmed; the existing debugger must remain available for diagnosis in that case. [Stop helper](../tools/stop_debugger.cpp), [bounded wrapper](../tools/stop_debugger.ps1)

## Interrupt ownership check

The stop helper now refuses a session that is already stopped, including a legitimate loader breakpoint or a previous helper's interrupt. Those events have no ownership evidence for the current invocation and remain available to the existing controller. The helper requires an initially running target and its own successful interrupt request before considering a handled detach.

After the target stops, `GetLastEventInformation` must return a complete exception record for the verified debugger process ID and a known thread index. It must be a first-chance `0x80000003` breakpoint with zero exception flags, at the target's uniquely resolved `ntdll!DbgBreakPoint` address. The helper rechecks both execution status and event identity immediately before `.detach /h`. Access violations, single-step events, incomplete records, unknown addresses, and observable event changes cause a refusal; they are not marked handled by the helper. [Microsoft event-information contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dbgeng/nf-dbgeng-idebugcontrol-getlasteventinformation)

The stricter helper built with warnings treated as errors, and the two-second `NoHit` fixture passed with `MGS_STOP_DETACHED target_pid=63552 handled=1 interrupt_verified=1`. This is evidence for the accepted interrupt path, not a claim that every unexpected-exception path has been exercised.

DbgEng supplies neither a causal token tying an exception to this interrupt request nor an atomic event-check-and-detach operation. Recovery therefore requires a dedicated session with an exclusive controller: another client must not resume execution or replace the event concurrently. A natural game `DebugBreak` at the same Windows breakpoint address racing the request cannot be conclusively distinguished with these checks. Normal registration-hit detachment remains the generated observer's separate path.
