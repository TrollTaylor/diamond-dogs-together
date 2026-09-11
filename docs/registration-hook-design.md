# Natural network registration observation

2026-09-09. **Use the installed Microsoft CDB debugger with a one-shot hardware execution breakpoint for the next observation. Do not introduce an inline detour yet.** This is a bounded way to learn which thread naturally enters the independently mapped network registrar, with no executable-byte replacement and no speculative Lua query calls.

This document is a proposal backed by current-build static evidence. The procedure has not been run against the game by this research task. The existing original Steam observer proved multiple callback threads; none can be designated the Lua owner from that callback evidence.

## Exact current-build target

| Check | Required value |
| --- | --- |
| Executable | Installed English `mgsvtpp.exe`, AMD64, 166517760 bytes |
| File SHA-256 | `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45` |
| File SHA-1 | `4F643576C6F6ECF6C67A9AEF4D4F02285ADA43E3` |
| Registrar RVA | `0xA1B2E0` |
| PE exception range | `[0xA1B2E0, 0xA1B54C)` |
| Expected first 17 loaded bytes | `40 55 48 8D AC 24 50 FF FF FF 48 81 EC B0 01 00 00` |
| Natural final return instruction | `RVA 0xA1B54B`, byte `C3` |

Resolve the process's actual image base and add the RVA; never assume the preferred image address survives ASLR. Verify the file fingerprint, executable identity and live prefix before arming. The prefix spans `push rbp`, `lea rbp,[rsp-0xB0]` and `sub rsp,0x1B0`, so the selected entry address is a complete instruction boundary. A mismatch means no breakpoint is armed at the proposed address.

The [original static binding analysis](network-binding-analysis.md) establishes the first argument in `RCX` as the value passed unchanged to the registration wrapper; it is consistent with a VM-state pointer. The runtime observation must label it an opaque state identity until its behavior is verified. Do not dereference it or call the mapped query functions.

## Independently identified caller

An additional scan of the installed executable found no literal 64-bit preferred-VA reference and no exact-target direct `E8`/`E9` reference to the registrar. It found one executable RIP-relative address materialization, confirmed by complete decoding of its containing function:

| Location | Observation |
| --- | --- |
| `[0xA1B140, 0xA1B19D)` | PE-declared caller range |
| `0xA1B164–0xA1B169` | Retrieves a value from the scoped object at `[rsp+0x28]`, then retrieves an apparent VM pointer through `[rax+8]` into `RBX` |
| `0xA1B170` | `48 8D 15 69 01 00 00`: materializes the registrar address in `RDX` |
| `0xA1B177–0xA1B17A` | Passes `RCX=RBX`, `RDX=registrar`, `R8D=0` to closure-construction helper `RVA 0x1A11770` |
| `0xA1B17F–0xA1B187` | Passes `RCX=RBX`, `EDX=0`, `R8D=0` to VM-call helper `RVA 0x1A10CC0` |

This path installs the registrar as a temporary Lua C closure and invokes it through the VM. Therefore, the return address observed on entry to the registrar may belong to a VM dispatcher, rather than the caller range above. A bounded stack trace is useful evidence; an unexpected direct caller alone should not be labeled a failure.

There is currently **no verified persistent data pointer that can be atomically exchanged** to observe this registrar. Its temporary Lua registration table is stack-owned, and the closure's ownership/lifetime has not been established. Changing the displacement of the `LEA` would be a code patch, not a data-pointer exchange. Existing import-patch safety tests do not validate that operation.

## First bounded CDB observation

Use `C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe` with a generated local command file and local log. Validate the exact command sequence on the project's own fixture before attaching to the game. Microsoft's [`ba` command](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/ba--break-on-access-) supports an execute breakpoint of size 1 and a one-shot option; the debugger manages processor breakpoint state. The observation should perform the following steps:

1. Attach to one explicitly identified game process, stop at the debugger's initial event, and verify its identity, image base and target bytes. Avoid blanket continuation of unrelated exceptions.
2. Arm a one-shot hardware execute breakpoint at `actual_image_base + 0xA1B2E0`, without limiting it to a presumed Steam callback thread.
3. On the natural hit, record process ID, actual thread ID, image base, `RIP`, opaque `RCX`, `RSP`, and the return address at `[RSP]`. Limit stack inspection to a short trace and the one return-address slot. Record the current loaded prefix again.
4. Clear this debugger's breakpoints and use `qd` to detach, allowing the original first instruction and return path to proceed. No target register assignment, target function evaluation, Lua execution, return-address replacement or stack write is needed.

[`qd` explicitly leaves a user-mode target running](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/qd--quit-and-detach-). A no-hit timeout must also have a tested interrupt-and-detach path; terminating the debugger process is not a substitute for clean detachment. Use a short deadline and record `no registration observed during this window`, rather than interpreting absence as proof the registrar is unused.

The first observation proves **natural entry on thread T with state identity S**. It does not prove exclusive ownership by T, successful return, lifetime of S, or permission to call Lua from future callbacks. The debugger pauses execution and changes timing, so record any behavior difference; do not bypass game/debugger incompatibility if the application refuses to proceed.

The fixture acceptance conditions are concrete: a known argument remains unchanged, the original function runs exactly once, its normal result reaches its caller, the fixture continues after detach, the bytes at the observed entry remain identical, and a no-hit deadline also leaves the fixture running. Include calls from more than one thread so the observer cannot accidentally validate only its initial debugger thread.

## Establishing order relative to DirectInput

Attaching at the current main menu may miss a one-time initialization. If no registration occurs naturally in the bounded window, observe a fresh ordinary startup from an earlier debugger stop after ensuring the current process can be closed safely. Do not manufacture a registration by invoking the initializer or loading a mission solely to make a missing event look successful.

There is a useful one-way ordering check in the current original observer 0.2.0. `native/probe.cpp` calls `InstallSteamCallbackObserver()` only after a successful forwarded `DirectInput8Create`. At a registrar hit, read the eight-byte callback import slot at `image_base + 0x208E0E8` and identify its target module:

- If it points to the verified original observer in this project's `dinput8.dll`, successful DirectInput creation already returned before the observed registration.
- If it points to the original Steam export, ordering remains unknown: observer installation could have failed or been disabled. That result does not prove registration preceded DirectInput.

This check reads one existing import slot; it does not change the slot or call Steam. Also preserve the process-specific existing probe/observer log as corroborating evidence. Do not compare an old launch's log to a new process without matching its identity and timestamps.

## Completion observation after entry works

A second bounded experiment can pair entry with the registrar's final `ret` at `RVA 0xA1B54B`, using a thread-restricted hardware breakpoint for that same invocation. Preserve the opaque state identity from entry in the debugger, not in game memory. At the final instruction, record `EAX` and the matching thread/frame, then detach without changing the return. The expected static result is `EAX=1`; it is a Lua result count, not a connection flag.

An exception or VM nonlocal exit may avoid this return. Report that outcome instead of forcing completion. This paired observation would establish that one natural registration completed on one thread. State destruction/reuse and steady-state Lua execution still need separate evidence before a persistent runtime bridge can retain or use the state.

## Why an inline detour is deferred

The registrar's unwind record at `RVA 0x2783BD8` begins:

`19 22 03 00 11 01 36 00 02 50 00 00 0C 5F A0 01 A0 01 00 00`

It declares version 1, handler flags 3, a 34-byte prologue, a 432-byte allocation and a saved `RBP`; it also references an exception/unwind handler. The proposed 17-byte relocation length therefore covers only part of the full declared prologue. Copying these unwind bytes to another allocation would also misinterpret image-relative handler information.

An eventual original detour needs a verified quiescent installation window, complete-instruction relocation, executable-page protection restoration, instruction-cache synchronization, a separately valid unwind description for the trampoline, and safe callback/trampoline lifetime on removal. Microsoft's [x64 exception rules](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170) and [`RtlAddFunctionTable`](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtladdfunctiontable) explain why dynamically generated nonleaf code requires proper unwind metadata. An aligned pointer exchange cannot make a 17-byte instruction rewrite atomic.

The hardware-breakpoint experiment obtains the missing owner-thread/lifecycle evidence first and avoids all of these trampoline requirements. No detour implementation or live game mutation was performed while preparing this design.
