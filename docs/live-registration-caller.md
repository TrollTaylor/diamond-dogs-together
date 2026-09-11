# Live registrar caller and matched return observation

2026-09-09. **The captured return address is the instruction immediately after the VM's native-function dispatch. A subsequent paired observation matched the original call's return and recorded result count `1`, without reading or modifying VM memory.**

Evidence: root's game capture, reported PID 31640 at 14:46:11 UTC, in [debugger.log](../build/game-observation-622ed83dceb44901bf37244f8487c4b1/debugger.log). This research independently decoded only the installed executable. SHA-256 remains `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`, version 1.0.15.4, AMD64. No additional live action was performed.

## What the capture establishes

| Captured item | Value |
| --- | --- |
| Image base | `0x140000000` |
| Natural registrar entry | `0x140A1B2E0`, RVA `0xA1B2E0` |
| Thread ID | `0xA5E0` |
| Opaque incoming `RCX` | `0x0D810008` |
| Return address read from entry stack | `0x141A12F54`, RVA `0x1A12F54` |
| Named variable backing pointer | `0x142C01360`, image plus `0x2C01360` |

The callback and initialization import slots still pointed to the original `steam_api64` exports. This identifies their ownership at that instant; it does **not** establish whether `SteamAPI_Init` had already executed. The trace establishes natural registrar entry on this thread, not its return or exclusive VM ownership.

## Exact caller contract

The semantic dispatch routine starts at RVA `0x1A12D50` and spans multiple PE exception records. At `0x1A12D67`, `mov rbx,rcx` retains the incoming VM argument. The relevant native-call block is:

| RVA | Bytes | Instruction / interpretation |
| --- | --- | --- |
| `0x1A12F43` | `48 8B 43 28` | Load active-call record through `[RBX+0x28]` |
| `0x1A12F47` | `48 8B 48 08` | Load its function-slot pointer |
| `0x1A12F4B` | `48 8B 01` | Load the callable object from that slot |
| `0x1A12F4E` | `48 8B CB` | Set `RCX=RBX`, the retained VM argument |
| `0x1A12F51` | `FF 50 20` | Indirectly call the function pointer at callable object `+0x20` |
| `0x1A12F54` | `85 C0` | Test the native function's returned `EAX` |
| `0x1A12F56` | `79 13` | Continue normal result handling when the count is nonnegative |

This matches the live registrar destination and stack return address exactly. The `+0x20` field is the function-pointer field on this path; do not describe it as an independently verified C++ vtable or infer a complete object layout.

On the nonnegative path, the dispatcher sign-extends `EAX`, multiplies it by 16, subtracts that byte count from the current VM top, and passes the resulting first-result pointer with `RCX=RBX` to helper `0x1A12CB0` at call `0x1A12F7B`. That helper moves value/tag slots to the caller's result area and restores call-frame state. A negative native return instead produces dispatcher status `2`. This corroborates the **Lua result-count ABI**: the registrar's expected `EAX=1` is neither a connection flag nor a member count.

The previously decoded path connects directly: network caller `0xA1B140` calls `0x1A10CC0`; that helper calls `0x1A129B0`, which calls dispatcher `0x1A12D50` at `0x1A12A0D`. This resolves the formerly unknown VM-dispatch caller without invoking any mapped function.

## Paired observation contract

At a fresh natural registrar entry, save thread ID `T`, opaque `RCX` identity `L`, and entry stack pointer `S` **in debugger-local state**. Verify the saved return address is image plus `0x1A12F54`. Arrange a hardware execution observation there that accepts only:

- Thread ID equals `T`.
- `RSP == S + 8`, matching the original native call's return frame.
- `RBX == L`, the dispatcher-retained VM argument.
- Loaded bytes at the observation address equal `85 C0 79 13`.

Record returned `EAX`, then remove the owned observation and detach cleanly. Expected count is `1`; preserve any different result as evidence. A thread-only check is insufficient because nested native callbacks can return through this same dispatch address. Do not consume a one-shot breakpoint on an unmatched nested frame; validate conditional behavior on the project's own fixture first.

The first entry log does not record `RSP`, so it cannot retrospectively supply this frame match. A new paired run at 15:06:21 UTC, PID `42824`, captured thread `0x16654`, opaque identity `0x0DC10008`, entry RSP `0x15FA28`, and matching return RSP `0x15FA30`. An earlier return on the same thread at RSP `0x15F690` was skipped. The accepted return at image + `0x1A12F54` passed the retained-RBX gate and returned `EAX=1`. The implemented runtime fingerprint checks `85 C0`; the following `79 13` branch was independently decoded from the hash-verified file, rather than separately gated at the hit. See the [paired log](evidence/game-registration-return-2026-09-09.log).

This establishes that one original registrar invocation returned normally. It does not prove downstream result transfer, later VM lifetime, or cross-thread ownership. The owned return breakpoint was removed before `.detach /h`; startup subsequently accepted input. No return-address replacement, target-register assignment, VM dereference or active query was used. A timeout/nonlocal exit remains a failed or incomplete observation, not a successful return.
