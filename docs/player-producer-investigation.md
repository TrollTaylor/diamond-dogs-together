# Player-record producer investigation

2026-09-09. **A traced game routine obtains a producer record, resolves a managed slot through an identifier and a component pool, and updates `PARTS_ACTIVE` from the component's state.** This identifies a concrete source of a published status bit. A current actor transform, a verified `GameObjectId`, input ownership and a safe heap-object lifetime have not been established.

Source: only the installed AMD64 `mgsvtpp.exe` 1.0.15.4, SHA-256 `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`, read with the project's original PE tools and Capstone. Addresses are RVAs relative to the actual executable base `B`. This task made no process reads, game calls, UI actions, patches or source/build edits. Root performed the separately attributed gameplay observations below.

## Route from the producer accessor to its interface

The indexed producer accessor `0x53CA10` selects the opposite bank described in [player-update-investigation.md](player-update-investigation.md). Its caller thunk at `0x1E02D90` is exactly:

```text
1E02D90  8B CA                 mov ecx,edx
1E02D92  E9 79 9C 73 FE        jmp 53CA10
```

The thunk is the pointer at `0x25564F8`, slot `+8` of the table at `0x25564F0`. Startup constructor `0x206CC90` writes this table address into the fixed object at `B+0x2B56D90` (`0x206CCA4`). The pure accessor at `0x1E02D80`, bytes `48 8D 05 09 40 D5 00 C3`, returns that object's address. Main initialization calls the accessor at `0x99316C` and stores the result at service collection offset `+0x1A0` at `0x993171`; that collection is initialized by `0x993A10` at `B+0x2C00D40`.

The same table contains small vector-copy setters. For example, its slot `+0x118` is `0x1E03410`, which receives an index in `EDX` and an aligned 16-byte source in `R8`, calls `0x53CA10`, and writes the source to record `+0x10` at `0x1E03423`. Local-index variant `0x1E03450` calls `0x53C9A0` and writes the same offset at `0x1E03461`. This verifies a vector-sized field and write mechanism, not that the field is position. Searching only direct calls to these setters found none; the useful next path was the indirect record accessor, followed by direct field writes.

Runtime interface ownership must still be checked. In particular, `0xBFF050` obtains a context through `0xBFEFA0` and reads its `+8` member. `0xBFEFA0` can allocate and initialize a singleton when its pointer at `B+0x2C3A8A0` is null. Neither accessor is suitable for an active diagnostic call. A future natural observation should verify the actual service object and table against `B+0x2B56D90` and `B+0x25564F0` before accepting the indirect dispatch as this implementation; the context singleton's assignment and all alternative service implementations have not been audited.

## One traced producer path

The declared routine `[0x9C1F00, 0x9C2E8B)` saves its incoming `RCX` owner as `RDI` at `0x9C1F48`; call this opaque owner `M`. Its function pointer occurs at `0x2295418`. No class name or engine object lifetime is inferred from that table occurrence.

The relevant branch is conditional. Entry requires bit zero of `M+0x224`. At `0x9C2525..0x9C252F`, bit one of that field being clear routes execution to `0x9C2D1D`. The record loop additionally requires the routine's computed `R15B` flag to be true and the unsigned count at `M+0x228` to be positive. The meanings of these mode flags are unresolved; this is not an unconditional frame update.

On this path, `R13D` starts at zero and `R14D` is `0xFFFF`. At `0x9C2D3C`, the routine naturally calls `0xBFF050`, follows context `+0x98` then service collection `+0x1A0`, and retains the service in `RSI`. At `0x9C2D6C` it calls table slot `+8` with `RCX=service` and `EDX=R13D`. Under the table-identity condition above, the return is the indexed producer record. It retains that pointer in `R8`.

For each index `i`, the subsequent instructions perform the following derivation. This is a description of the game's code, **not a safe external heap-reader recipe**:

```text
producer = service.slot8(i)
producer.status_word4 &= ~(1 << 17)       // PARTS_ACTIVE, ID 145

id       = uint16[ pointer[M+0x90] + 2*i ]
pool     = uint8 [ pointer[M+0x98] + i ]
reject this slot if id == 0xFFFF or pool == 0xFF

entry    = pointer[M+0x398] + pool*0x6A8
component = pointer[entry+0x78]
relative = uint32(id - uint32[component+8])
part     = pointer[component+0x280] + relative*0x20
active   = (uint32[part+0x10] >> 14) & 1

if active:
    producer.status_word4 |= 1 << 17
```

The record bit is cleared at `0x9C2D72` and conditionally set at `0x9C2DDF`. The identifier load is `0x9C2D86`; pool load and sentinel test are `0x9C2D9B..0x9C2DA5`; the `0x6A8` stride is `0x9C2DAB`; component resolution begins at `0x9C2DBF`. Source state is loaded at `0x9C2DD3` and bit 14 is tested at `0x9C2DD7..0x9C2DDD`.

There is also a zero-result test after calculating `entry`. It does not supply heap bounds, pool capacity, allocation lifetime or generation validation. No bound for `relative` is established by this block. Keep `id` as an opaque 16-bit managed identifier; its relation to the script `GameObjectId` namespace is unverified. The loop count at `M+0x228` is not proof of player capacity or populated actor slots.

On a slot that passes those checks, the same block copies five more values from the owner-associated data object `D=pointer[M+0x80]`:

| Producer destination | Source | Store RVA |
| --- | --- | --- |
| `+0x88`, uint16 | uint8 at `pointer[D+0x40]+i` | `0x9C2DF3` |
| `+0x8A`, uint16 | uint8 at `pointer[D+0x48]+i` | `0x9C2E04` |
| `+0x8C`, uint16 | uint16 at `pointer[D+0x60]+2*i` | `0x9C2E15` |
| `+0x90`, uint32 | uint32 at `pointer[D+0x88]+4*i` | `0x9C2E33` |
| `+0x94`, uint32 | uint32 at `pointer[D+0x90]+4*i` | `0x9C2E3A` |

These fields have no established names here. Invalid slots skip these copies after clearing `PARTS_ACTIVE`; therefore those other values can remain from an earlier record state. An observer must not treat every nonzero record field as current merely because a bank was published.

## Reproducible instruction checks

Use the full executable fingerprint and the already documented producer-accessor formula. These short checks locate the additional dataflow; they do not authorize a hook or an internal function call.

| RVA / length | Exact bytes | Meaning |
| --- | --- | --- |
| `0x1E02D90` / 7 | `8B CA E9 79 9C 73 FE` | Interface index argument to producer accessor |
| `0x9C2D3C` / 19 | `E8 0F C3 23 00 48 8B 88 98 00 00 00 48 8B B1 A0 01 00 00` | Context and service collection traversal |
| `0x9C2D63` / 12 | `48 8B 06 41 8B D5 48 8B CE FF 50 08` | Natural producer-record dispatch |
| `0x9C2D72` / 10 | `81 A0 D0 00 00 00 FF FF FD FF` | Clear status ID 145 |
| `0x9C2DBF` / 20 | `48 8B 40 78 2B 50 08 8B CA 48 C1 E1 05 48 8B 80 80 02 00 00` | Component and 32-byte part-record derivation |
| `0x9C2DD3` / 12 | `8B 54 01 10 C1 EA 0E F6 C2 01 74 0B` | Source bit-14 test |
| `0x9C2DDF` / 11 | `41 81 88 D0 00 00 00 00 00 02 00` | Set status ID 145 |

## Observation boundary and limits

A useful next bounded debugger capture is the natural call at `0x9C2D6C` and its matching return at `0x9C2D6F`, preserving the original thread, stack and call. Record the opaque owner, index, actual service/table/slot destination and returned record address. Accept the address only if it matches the opposite-bank formula for a bounded index. A later natural point at `0x9C2DEA` would show that the source-bit decision has completed for a valid slot. Its thread/caller and a bounded status sample can be observed without actively calling the getters or setters. The existing debugger fixture still needs any new call-site composition validated before use.

This is a conditional managed-component update route, not yet a safe persistent hook or a per-frame player adapter. It supplies no actor transform. The preceding helper `0x9CD030` contains float stores to offsets `+0xF0/+0xF4` at `0x9CE254/0x9CE25C`, but their base is `pointer[M+0x100]`, an object used through a virtual table earlier in that block. It was not resolved to the named `vars` allocation, so these matching displacements are not accepted as player-coordinate writes.

Remaining evidence needed: natural service identity, the owner's actual type and lifetime, mapping from managed identifiers to game objects, a transform write tied to the same actor, input ownership, and transition invalidation. No heap pointer chain from this report is ready for an unguarded sampler.

## Root's title-to-helicopter status delta

Root reported ordinary UI navigation into the existing helicopter scene and pressing Tab. Root's external read evidence at `2026-09-09T15:31:05Z` is [player-status-helicopter-2026-09-09.jsonl](evidence/player-status-helicopter-2026-09-09.jsonl). Compared with the earlier title sample, its words changed from `[1048614,536870944,0,0,131072]` to `[1048614,16777248,0,0,131584]`; the raw mission code, named coordinates and index were unchanged.

The existing 48-constant `PlayerStatus` registrar gives exact names for two changed bits: ID 56 is `SUBJECT` (`0x5548E5` computes `R8D=1+0x37`, name at `0x5548E9`, registration call `0x5548F3`); ID 61 is `CUTIN` (`0x554930` computes `R8D=1+0x3C`, name at `0x554934`, call `0x55493E`). Thus `SUBJECT` became set and `CUTIN` became clear. The registrar has no constant for changed ID 137; its name remains unresolved. This comparison does not isolate the effect of Tab from entering the helicopter scene, and does not justify labeling ID 137 as iDroid or any of these bits as a general readiness gate.

Root subsequently reported an automatic Mother Base arrival cutscene with no movement input yet. The [15:43:52 UTC arrival samples](evidence/player-status-mother-base-arrival-2026-09-09.jsonl) record mission `30050`, small changes in the named coordinate fields, and status words `[1048734,0,0,0,131072]`. `STAND`, `NORMAL_ACTION` and `PARTS_ACTIVE` are already set during that cutscene. Consequently even that combination does not establish human control; slight coordinate drift also does not establish that user movement is being sampled.
