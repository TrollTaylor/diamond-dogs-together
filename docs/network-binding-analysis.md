# Independent network binding analysis

2026-09-09. Static analysis of the installed English `mgsvtpp.exe` 1.0.15.4 independently identifies the `TppNetworkUtil` module initializer and its 23 Lua-facing bindings. No function was called, hooked or observed in a running game. The evidence is sufficient for a build-specific static map; lifecycle and runtime contracts remain unverified.

Executable SHA-256: `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`.

Reproduce with `python tools/analyze_network_bindings.py`. The analyzer reads only the executable, this project's hash-bound candidate report, and the project's Capstone disassembler. It rejects a candidate report for a different executable. Detailed instruction evidence and checks are in [network-binding-analysis.json](network-binding-analysis.json).

## Registration is constructed on the stack

The independently discovered range `RVA 0xA1B2E0–0xA1B54C` creates 23 adjacent `{name pointer, function pointer}` pairs on its stack, followed by two zero pointers. This explains why the earlier literal-pointer search found no permanent registration table.

At `RVA 0xA1B52A`, it calls helper `RVA 0x6B8C0` with these Windows x64 arguments:

| Register | Statically established value |
| --- | --- |
| `RCX` | Unchanged first argument received by the initializer; consistent with the VM state |
| `RDX` | Address of the `TppNetworkUtil` string |
| `R8` | Address of the temporary 23-entry registration table |

The helper preserves an additional VM flag, invokes a three-argument registration thunk, records library metadata, then restores the flag. The thunk at `RVA 0x1A18080` clears `R9D` and branches to `RVA 0x1A17C30`. That routine iterates the table in 16-byte increments, uses the pointer at `+8` for closure construction and the pointer at `+0` for naming, and stops at a null name. This supports the Lua C registration interpretation. The generic public contracts are documented in [Lua's `luaL_Reg` and C-function API](https://www.lua.org/manual/5.1/manual.html#luaL_Reg); exact game behavior comes from its own decoded instructions.

The table belongs to this initializer's stack and cannot be retained after the initializer returns. Calling only a standard registration primitive would also omit the additional behavior of the game's wrapper.

## Two observation bindings

| Binding | Entry RVA | Declared function end, exclusive | Statically established behavior |
| --- | --- | --- | --- |
| `IsSessionConnect` | `0xA1B280` | `0xA1B2D4` | Preserves incoming `RCX`, obtains a shared object, defaults false when null, otherwise calls its virtual slot `+0x10`; the boolean is `(returned_EAX & 0xFFFFFFF6) != 0`. It pushes one result and returns `EAX = 1`. |
| `GetSessionMemberCount` | `0xA1B0A0` | `0xA1B0E1` | Preserves incoming `RCX`, defaults the count to zero, obtains the shared object and calls virtual slot `+0x18` on its subobject at `+0x20`. It sign-extends the 32-bit result, pushes one result and returns `EAX = 1`. |

Their native return values are **Lua result counts**. They do not directly return the connection boolean or member count to a C++ caller.

The boolean-shaped push helper at `RVA 0x1A11750` normalizes `EDX`, writes tag 1 and the boolean into the current VM slot, then advances the top by 16 bytes. The integer-shaped push helper at `RVA 0x1A11880` converts signed `RDX` to a double, writes tag 3 and the number, and makes the same advancement. Both read the top slot through `RCX + 0x10`. These are static observations, not permission to manipulate the VM layout directly.

Both wrappers call the accessor at `RVA 0x313780`. Its body has conditional initialization, helper calls and shared-state storage paths. Actively polling either wrapper is therefore **not established as side-effect-free**, particularly before the game has initialized the relevant service. Observing naturally occurring calls is a smaller next step.

## Next runtime evidence

The first candidate observation boundary is the naturally called module initializer at `RVA 0xA1B2E0`. Its first 17 bytes cover three complete instructions and contain no RIP-relative operand:

`40 55 48 8D AC 24 50 FF FF FF 48 81 EC B0 01 00 00`

That is an instruction-boundary observation, not a ready-to-enable detour. Before any hook, verify the exact loaded build and bytes, observe natural arrival and thread ownership, determine whether registration occurs before or after DirectInput initialization, and establish safe installation/removal and trampoline behavior. A controlled debugger observation can resolve those lifecycle questions without making speculative query calls.

After the original initializer completes, the first bridge should record only that registration completed and the identity/lifetime of the state. It should preserve all original arguments and results. A later observation stage may wrap existing naturally invoked queries, with stack invariants checked and no extra calls. Mission and actor observation still need independent mapping; this network module alone does not establish either one.

Validation completed: 23 pairs and a null terminator recovered, full declared ranges decoded, all ten semantic checks passed, the zero-upvalue registration thunk identified, and a mismatched candidate fingerprint rejected. No remote actors, network sessions, campaign progress or live VM access were tested.
