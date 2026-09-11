# Player-state update investigation

2026-09-09. **A concrete new route is the game's published `PlayerInfo` status records, read by named predicates and switched between two banks by an identified callback. This is stronger evidence of changing player state than the `vars.playerPos*` fields, whose movement freshness remains unresolved.** The publication routine's cadence, execution thread and relationship to an interactive actor have not been observed.

This investigation read only the installed AMD64 `mgsvtpp.exe` 1.0.15.4 with the project's original PE tools and Capstone. SHA-256: `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`. Every address below is an RVA added to the actual loaded executable base `B`. No process reads, calls, hooks, saves, archives or external mod code were used by this task.

## Existing coordinate-writer evidence

The pure eight-byte accessor at `0x9AA930` is `48 8B 05 E9 BF 26 02 C3`: it returns `P`, the pointer stored at `B+0x2C16920`. Prior name registration identifies `P+0xEC`, `P+0xF0`, and `P+0xF4` as `vars.playerPosX/Y/Z` floats.

The confirmed coordinate-capable write is the shared property setter at `0x9AAC32`, bytes `F3 0F 11 0B` (`movss [rbx],xmm1`). It is an intended coordinate write only when `RBX` matches one of those three validated field addresses. The next instruction is at `0x9AAC36`. Because this adapter serves other float properties, its unfiltered call count is not movement evidence.

A bounded static search found 1,456 REX-form references to the backing pointer and 242 direct calls to its pure accessor. Most pointer references are descriptors inside variable initialization. Looking for the coordinate displacements in the direct callers' declared ranges did not identify a native movement updater. This search can miss cached pointers, adjusted base registers, bulk copies, indirect calls and other instruction forms. It does not prove the fields are exclusively checkpoint values or exclusively updated through Lua.

## Named player-status record route

The previously verified `PlayerInfo` registration maps `AndCheckStatus` to `0x554520` and `OrCheckStatus` to `0x554C60`. Their followed branches resolve a player index, reject unsigned indices at least 16, obtain a record through `0x53CA40`, iterate numeric status IDs from the supplied table, and inspect the record's bitset. The predicates push a Boolean and return one Lua result. Neither is a zero-argument native readiness getter.

The exact address derivation is:

```text
bank   = int32  at B + 0x2BD3FD0
index  = uint32 at B + 0x2BD3FD4
record = B + 0x2BD3FF0 + (bank * 16 + index) * 0xE0
```

A reader must require `bank` to be 0 or 1 and `index < 16`. Do not emulate the native accessor's fallback to record zero on an invalid index. Record storage is not evidence that each index has an allocated or playable actor.

For a status ID `n < 0x93`, the predicates use:

```text
word = uint32 at record + 0xC0 + 4 * (n >> 5)
set  = (word & (1u << (n & 31))) != 0
```

Five DWORDs, 20 bytes at record offsets `0xC0..0xD3`, cover the inspected status IDs. The implementation uses `shr 5`, `and 0x1F`, a DWORD load and `bt`; the bit numbering is therefore directly supported by instructions.

`PlayerStatus` is independently named at registrar `0x554650`, with namespace creation at `0x55468F`. The following small subset is enough to correlate future observation with ordinary play:

| Registered name | Decimal bit ID | Constant-registration call RVA |
| --- | ---: | --- |
| `STAND` | 4 | `0x5546B4` |
| `SQUAT` | 5 | `0x5546CD` |
| `CRAWL` | 6 | `0x5546E6` |
| `NORMAL_ACTION` | 7 | `0x5546FF` |
| `IDLE` | 9 | `0x554731` |
| `WALK` | 21 | `0x554795` |
| `RUN` | 22 | `0x5547AE` |
| `DASH` | 23 | `0x5547C7` |
| `DEAD` | 62 | `0x554957` |
| `PARTS_ACTIVE` | 145 | `0x554B04` |

Names and bit IDs are statically verified. Their complete gameplay semantics are not: for example, `PARTS_ACTIVE` alone does not establish that a human can control the actor, and absence of `DEAD` does not establish actor presence. Report the raw named bits rather than inventing an `is_playable` Boolean.

## Concrete publication callback

The read accessor `0x53CA40` selects `bank`. The local producer accessor `0x53C9A0` selects `1-bank`, using the current local index; indexed producer accessor `0x53CA10` uses the same opposite-bank convention. This supports an inferred producer/published-record design.

Publisher `0x53CB10` then:

1. Computes `new_bank = 1 - bank`.
2. Stores `new_bank` at `0x53CB2C`.
3. Copies all `0xE00` bytes of the newly selected bank into the opposite bank, preserving a starting baseline for later updates.
4. Returns through `rep ret` at `0x53CBB2` on the aligned-copy path; the fallback path tail-calls copy helper `0x1A05B60` with size `0xE00`.

The aligned path copies 28 blocks of 128 bytes. Its bank-selection store precedes that copy, so the store itself is not the end of the routine. No lock, atomic publication protocol or universal producer exclusion was established by these bytes.

A five-byte callback thunk at `0x53C910`, `E9 FB 01 00 00`, jumps to the publisher. Its pointer appears at data RVA `0x2190B40`, slot `+0x10` of the table starting at `0x2190B30`. Initializer `0x53CA80` assigns that table to an allocated object at `0x53CACC`, retains the object through global `0x2BD3FE0`, and passes it to service registration helper `0x2C7C0` at `0x53CAF1` when the service exists. This supplies a concrete naturally registered callback to investigate; it does not yet establish a once-per-frame schedule or an appropriate hook installation method.

## Minimal byte gates

The executable hash, image identity and bounds must match before relying on these fields. Useful additional live instruction checks are:

| RVA / length | Expected bytes | Evidence checked |
| --- | --- | --- |
| `0x53CA40` / 12 | `48 63 05 89 75 69 02 83 F9 10 72 12` | Active bank load and index bound |
| `0x53CA5E` / 27 | `48 C1 E0 04 8B C9 48 03 C1 48 8D 0D 82 75 69 02 48 69 C0 E0 00 00 00 48 03 C1 C3` | Exact valid-index record formula |
| `0x5545D7` / 24 | `8B C8 8B C0 48 C1 E8 05 83 E1 1F 8B 84 86 C0 00 00 00 0F A3 C8 48 8B CB` | Status word/bit extraction |
| `0x53CB10` / 12 | `B8 01 00 00 00 4C 8D 05 D4 74 69 02` | Publisher entry |
| `0x53CB23` / 15 | `2B 05 A7 74 69 02 48 63 D0 89 05 9E 74 69 02` | Bank toggle and publication store |
| `0x53CBB2` / 2 | `F3 C3` | Aligned-copy completion return |

These checks bind a reader to the inspected dataflow; they do not certify an inline detour. PE exception records split some predicate/accessor branches, so those branches were decoded explicitly rather than treating one record as the entire function.

## Next smallest useful observation and limits

An original guarded reader can sample only the validated bank, local index and 20-byte status bitset. Re-read bank/index after the copy and mark a changed selection unusable. An unchanged pair still does not prove an atomic snapshot because two bank flips can return to the same value, and a producer may update another field concurrently.

The next debugger observation should target one natural callback completion at `0x53CBB2`, verify the full publisher identity, record its thread/caller and a bounded status sample, then detach cleanly. Subsequent bounded observations can establish cadence and correlate `STAND/SQUAT/CRAWL` and `WALK/RUN` changes with actual visible control. Until that correlation, call this **published player-status data**, not a verified live actor or per-frame engine bridge.

Current mission sequence, actor game-object ID, actor transform, input readiness, record invalidation/lifetime and coordinate freshness remain unresolved. The successful paired network-registrar return reported by root establishes a separate initialization event; it does not settle these player-update questions. No function calls or active queries are required for the proposed observation.

## Root's first live status sample

Root performed the bounded external reads in PID 42824 at `2026-09-09T15:19:08Z`; this static-analysis task did not perform them. The original evidence is [player-status-title-2026-09-09.jsonl](evidence/player-status-title-2026-09-09.jsonl). Root reported the game was still at its title screen. The file records the accepted executable fingerprint, read-only scope, five valid-selector samples, and explicit unverified freshness/actor presence and non-atomic snapshot labels.

With a requested interval of 250 ms, bank values were `1, 0, 0, 1, 1` over 1,031 ms; local index remained zero. Every sample read the same five status DWORDs: `[1048614, 536870944, 0, 0, 131072]`. Among the ten named bits above, `SQUAT` and `PARTS_ACTIVE` were true and the other eight were false. Raw mission code was `40050`; the three named coordinate fields remained `[0.04177, 1198.09912, -1.04203987]`.

This establishes that the guarded record formula is readable in this running build and that the bank selector changes even at the title screen. It does not measure publisher cadence or identify its caller/thread. The unchanged bitsets do not establish active movement. In particular, `SQUAT` and `PARTS_ACTIVE` cannot by themselves serve as an interactive-gameplay readiness test, because they were already set in this title-screen observation. The nonzero coordinate fields also remain unverified as the current actor transform.

The corresponding `native/world_probe.cpp` additions were reviewed against the static derivation: signed bank validation, unsigned index bound, record stride, 20-byte bitset extent, ten name/ID mappings, and extraction all match. The code rejects selection changes during a sample while preserving the stated non-atomic limitation. This review did not add calls, hooks, process writes or game controls.
