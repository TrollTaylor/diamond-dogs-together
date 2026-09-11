# Live native session-owner observation

On 2026-09-09 at **17:01:34 UTC**, the original external sampler read the independently traced native session service in MGSV PID **106392**. All five samples passed its code and type checks and returned:

| Field | Observed value |
| --- | --- |
| Service vtable RVA | `0x2223508` |
| State DWORD at service `+0x10` | `0` |
| Flag byte at service `+0x48` | `0` |
| Concurrent mission field | `40050` |

[Final live samples](evidence/network-service-typed-live-2026-09-09.jsonl). An [initial discovery sample](evidence/network-service-live-2026-09-09.jsonl) observed the same values using a broader in-image table check; the final reader requires the specific constructor-derived table.

These are the constructor's initial field values. They do not establish whether setup ran earlier and state subsequently reset, whether a transport exists elsewhere, whether the game is online, or whether a connection is active. Mission `40050` and stationary player coordinates were read concurrently; no fresh UI observation was made in this step.

## Independent derivation

The complete wrapper at `0x1E0AE40` loads the pointer stored at image RVA `0x2BEFC00`, calls accessor `0x7D6470`, and tail-calls the previously mapped setup routine `0x7EE380`. The accessor is a nine-byte leaf that returns `[[RCX+0x30]+0x28]`. This establishes the bounded chain used by the reader:

```text
[image base + 0x2BEFC00] -> [root + 0x30] -> [branch + 0x28] -> service
```

The live table value led to a static reference in constructor `0x7ED6F0`. At `0x7ED711` it computes table `0x2223508`, then stores that pointer into the service. Its base constructor `0x7E4070` initializes state `+0x10` to zero, and its own code zeros flag `+0x48`. Its destructor also writes that table, so matching the table is not an object-lifetime guarantee.

The extended original verifier passed **44 checks across 25 bounded ranges**, including those constructors and the exact access instructions. [Extended static evidence](evidence/packet-manager-lifecycle-with-owner-2026-09-09.json). The earlier 39-check packet-manager report remains preserved separately.

## Sampler behavior

```powershell
./build/native/world_probe.exe <current-game-PID> 5 100 --network
```

`--network` is optional and must be last. Existing world samples remain available without it. The additional record is tagged `kind: network_service_candidate`; it is a separate JSON line for each sample, not a new player record.

The executable requires the known game hash and mapped image size, verifies the full wrapper/accessor and selected field-write bytes, and reads with `PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`. It never evaluates an address as a callable function. Each pointer read is length-bounded, checked for user-address overflow, and passed to `ReadProcessMemory`; null, inaccessible and different-type paths suppress raw field output. Only the expected service table is accepted before reading fields. Each sample resolves the chain twice and suppresses fields if pointers, table or values differ.

Equal repeated reads do not provide an atomic snapshot, exclude ABA reuse, or establish a lifetime lease. The reader does not traverse from this service into the packet manager or expose remote IDs, heap addresses, Steam interfaces or packet content. `transport_presence` and `connection_status` remain explicitly unverified.

The updated native build and all **13 existing test suites passed**. Those tests cover existing portable components and loader behavior; they do not provide synthetic coverage of the new heap-reader path. Its positive check is the bounded live observation above. [Build evidence](evidence/native-tests-with-network-reader-2026-09-09.log).

No restart, game input, game write or save operation was performed. The installed 0.4.0 loader hash remains `58C0EE96991C905DEC4528E363E3F10A322223629EDD0677D36C446F4B590FF6`.

## Consequence for implementation

This supplies a concrete session-owner baseline for future natural setup and teardown observations. It does not justify invoking native setup during ordinary campaign play. The next native networking proof still needs the concrete dispatch into manager setup/update and live ownership; the separate four-actor proof remains essential before any campaign support claim.
