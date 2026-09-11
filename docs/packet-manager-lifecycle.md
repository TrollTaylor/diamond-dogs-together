# Native packet-manager lifecycle

Independent installed-file inspection, 2026-09-09. The new verifier passed **39 static checks across 22 bounded disassembly ranges**. This establishes a lazy transport allocation and a conditional polling path. It does **not** demonstrate their execution, identify the activation predicate, or provide playable co-op.

Executable SHA-256: `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`, version 1.0.15.4. All addresses below are RVAs in that executable, relative to its loaded image base. No game code, DLL export, network operation or save operation was invoked for this investigation.

## Reproduce

```powershell
python tools/inspect_packet_manager.py --output docs/evidence/packet-manager-lifecycle-2026-09-09.json
```

The tool uses the project's existing Capstone disassembler. It verifies the whole executable hash, disassembles bounded ranges, checks aligned branch targets and instructions, checks table pointers, and resolves callback-registration import identities from the PE import table. [Machine-readable evidence](evidence/packet-manager-lifecycle-2026-09-09.json) includes disassembly and explicit unresolved edges. It does not scan or open a running process.

Unwind ranges may be pieces of a larger function. Leaf and tail thunks stop at their first terminator; bytes after a tail jump are not interpreted as that routine's body. A complete decoded range is not a claim of complete control-flow analysis.

## Verified paths

| Component | Static evidence | Implication |
| --- | --- | --- |
| Outer manager | Constructor `0x1CEDAC0` installs vtable `0x251FDA0` and stores an allocated inner object at `outer+0x10` | The outer and inner pointers are distinct |
| Setup | Vtable `+0x10` points to `0x1CEE6D0`, which loads the inner pointer and calls `0x1CEE6F0` | Setup is exposed through a virtual method; its natural caller is unresolved |
| Lazy transport | Setup compares `inner+0x18` with zero at `0x1CEE70F`; if absent, calls factory `0x1A5F7C0` and assigns the resulting reference to that field | Setup creates a transport only when that field is absent |
| Transport factory | Allocates `0x58` bytes and calls constructor `0x1A5F470` | Factory output is a reference container, not a directly returned raw object |
| Steam callbacks | Constructor sets concrete vtable `0x24BF430` and calls the verified `SteamAPI_RegisterCallback` import with IDs 1202 and 1203 | Construction changes Steam callback state; this is not a read-only query |
| Update | Vtable `+0x78` points to leaf `0x1CEE890`, which loads the inner pointer and jumps to `0x1CEE8A0` | Update has a separate entry from setup |
| Conditional polling | At `0x1CEE8AC`, update loads `inner+0x18`, tests it and skips the call when null; otherwise `0x1CEE8B5` calls `0x1A5FBF0` | The mapped Steam packet-availability loop requires a present transport on this path |
| Send/read | Transport vtable `+0x10` points to `0x1A5FB60`, and `+0x20` to `0x1A5FAC0` | These connect to the independently mapped channel-zero Steam paths |
| Cleanup | Manager cleanup wrapper `0x1CEE7B0` reaches `0x1CEE7C0`, which closes active peer records and releases the transport reference | Cleanup may affect the game's existing peer connections |

See the [earlier getter map](steam-networking-getter-observation.md) for the nine direct getter calls and channel-zero packet paths. The current loader's lack of a getter receipt is consistent with an inactive path, but it **does not prove** the internal transport pointer was null. No pointer from this manager was sampled live in this step.

## Upstream construction and remaining gap

Two routines, `0x7EE380` and `0x7EE590`, call manager factory `0x1CEE1E0`. Both first call the Boolean predicate at `0x7E4190` and return false when it is false. That entry is a tail jump to `0x681CB90`; its semantics remain unresolved. Do not label this as an online toggle or override its result.

Both routes construct a descriptor containing the newly created manager, then dispatch through virtual slot `+0x40` on an interface at `singleton+0x20`. The concrete implementation of that dispatch has not been established. In particular, this report does not assert a verified call edge from that dispatch to manager setup. The singleton constructor at `0x312080` is another unresolved tail jump, to `0x4A72F10`.

The upstream descriptors provide capacity 16. This is a native record capacity, **not evidence of 16 playable actors or four-player campaign support**. The original co-op coordinator still limits membership to one host and three guests.

Upstream wrappers `0x1E0AE40` and `0x1E0AFB0` tail-call those two routines and occur in pointer slots `0x255A530` and `0x255A528`. They obtain the service through accessor `0x7D6470`, whose complete nine-byte leaf reads `[[RCX+0x30]+0x28]`. Their names and natural callers are unresolved; they are not established Lua bindings.

## Integration decision

Keep the native factory, predicate and manager virtual methods inactive in the mod until their ownership and natural lifecycle are observed. The next useful boundary is natural setup/update observation, or a bounded external read of a fully traced owner path; receipt of Steam callbacks alone does not establish either.

A different packet channel alone does not isolate connection lifetime. Valve documents that channels to a user share an underlying P2P connection, and `CloseP2PSessionWithUser` frees that peer's connection resources. The original adapter therefore needs explicit coexistence and teardown ownership before it can share this native transport. [Valve ISteamNetworking reference](https://partner.steamgames.com/doc/api/ISteamNetworking).

The installed 0.4.0 loader and existing portable networking adapter were not changed by this step. No new restart was needed. Remote actors, authentication, transport ownership and campaign adapters remain unfinished.

## Subsequent owner observation

The [external session-owner observation](live-network-owner-observation.md) subsequently traced the wrapper's owner pointer chain, independently identified its constructor table, and sampled state `0`/flag `0` five times in the running game. The extended verifier now checks 44 items across 25 ranges; its report is preserved separately from this initial 39-check investigation. The transport pointer and concrete setup/update dispatch remain unverified.
