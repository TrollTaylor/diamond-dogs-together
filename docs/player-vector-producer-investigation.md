# Published player vector: setter boundary and named position use

Later live evidence: [natural player-record writer](live-player-record-writer.md) captures a direct update at `0x12CF266` inside `0x12CE870`, bypassing these setters. Its source array and updater receiver now have observed opaque addresses; ownership and lifetime remain unresolved. Earlier setter no-hit trials did not overlap the intended dive.

2026-09-09. **The vector at published player record `+0x10` has a verified position-related use: `TppBuddy2BlockController.SetVarsPositionFromNearPlayer` copies its first three floats into `vars.initialBuddyPos`.** The indexed/local setters are decoded completely, but this bounded static investigation did not resolve a natural caller of either setter to its producing actor. The vector remains a candidate player reference position, not a validated current actor transform.

Only the installed AMD64 `mgsvtpp.exe` 1.0.15.4 was read, with the project's original PE tools and Capstone. SHA-256: `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`. Every address below is an RVA added to actual image base `B`. No process memory, game calls, UI actions, archives or external mod code were used. This task changed only this document.

## Exact setter boundary

The interface object and table are described in [player-producer-investigation.md](player-producer-investigation.md). Table `B+0x25564F0` contains the indexed vector setter in slot `+0x118` and the local setter in slot `+0x120`.

| Property | Indexed setter | Local setter |
| --- | --- | --- |
| Complete function range | `[0x1E03410,0x1E0342D)` | `[0x1E03450,0x1E0346B)` |
| Input index | `EDX`, moved to `ECX` | Global local index through accessor |
| Aligned 16-byte source address | `R8`, retained in `RBX` | `RDX`, retained in `RBX` |
| Opposite-bank accessor call | `0x1E0341B` -> `0x53CA10` | `0x1E03459` -> `0x53C9A0` |
| Source load | `0x1E03420`: `movaps xmm0,[rbx]` | `0x1E0345E`: same |
| Record write | `0x1E03423`: `movaps [rax+0x10],xmm0` | `0x1E03461`: same |

The receiver in `RCX` is not used by either body. The bodies copy all 16 bytes unchanged. They establish no producer-source ownership, coordinate space, finite-value validation or lifetime contract. A native return type is not assigned here merely because `RAX` still contains the record address at return.

For valid selectors, the store destination is:

```text
B + 0x2BD3FF0 + ((1-bank)*16 + index)*0xE0 + 0x10
```

Use only bank values 0/1 and indices below 16 for a future observer. The native accessor's fallback on an invalid index does not authorize silently reading or writing index zero. This investigation found no accepted direct caller of either setter and did not prove that the vector is exclusively written through these functions. Virtual-call candidates with the same slot numbers were rejected when their argument/receiver dataflow did not establish this player interface.

## One concrete named position path

The independently decoded registrar `[0xA45BE0,0xA45F27)` sets the module name `TppBuddy2BlockController` at `0xA45DD9` and calls the registration helper at `0xA45EF5`. Within its temporary table:

```text
A45E59  lea rax,[B+0x22B9A38]       // "SetVarsPositionFromNearPlayer"
A45E60  mov [rbp+0xB0],rax
A45E67  lea rax,[B+0xA49590]
A45E6E  mov [rbp+0xB8],rax
```

The wrapper `[0xA49590,0xA495AB)` naturally calls helper `0xA49510` at `0xA4959F` and returns zero VM results. It accesses the controller global and is a setter path; it must not be actively called to diagnose position.

Helper `[0xA49510,0xA49589)` performs this concrete dataflow:

1. Calls the previously verified pure `vars` pointer accessor `0x9AA930` at `0xA49525`; retains `P` in `RBX`.
2. Calls `0xBFF050` at `0xA4952D`, follows context `+0x98` and service collection `+0x1A0`.
3. Calls service table slot `+0x18` at `0xA49548`, with `RDX` pointing at a 16-byte aligned stack output buffer.
4. Copies output floats 0, 1 and 2 to `P+0x2D70`, `P+0x2D74` and `P+0x2D78` at `0xA49552`, `0xA49560` and `0xA4956E`. It applies no offset or transform in this helper.

Under the actual-interface identity check, slot `+0x18` resolves to `[0x1E02E00,0x1E02E1F)`. This getter calls `0x53C9D0` at `0x1E02E09`, loads the published local record's 16 bytes at `+0x10` at `0x1E02E0E`, copies them to the caller's output at `0x1E02E15`, and returns that output pointer in `RAX`. `0x53C9D0` explicitly loads the active bank and local index at RVAs `0x2BD3FD0` and `0x2BD3FD4`.

The destination name is not inferred from nearby strings. In the `vars` registrar, `0x99ECE6` adds `0x2D70` to the exact `vars` allocation, `0x99ED0A` names that descriptor `initialBuddyPos`, and `0x99ED2D..0x99ED71` registers the same base as an array of three elements with the same name. The complete chain therefore ties record `+0x10` to a named near-player position use. It does not establish that the stored point is the player root, feet, head, collision center, camera or a continuously refreshed transform. The fourth vector component is unused by this consumer.

There is also a native call to `0xA49510` at `0xA448B6`; subsequent instructions read those three `vars` floats and pack them into the controller's vector at `+0xA0`. Both caller relationships are static evidence. Neither was observed executing in this task.

## Complete small-function fingerprints

The whole executable hash is required. These full function-byte checks additionally bind the reader/writer mechanisms, without certifying a detour or making them callable:

| RVA / bytes | Exact expected hex |
| --- | --- |
| `0x1E03410` / 29 | `40 53 48 83 EC 20 8B CA 49 8B D8 E8 F0 95 73 FE 0F 28 03 0F 29 40 10 48 83 C4 20 5B C3` |
| `0x1E03450` / 27 | `40 53 48 83 EC 20 48 8B DA E8 42 95 73 FE 0F 28 03 0F 29 40 10 48 83 C4 20 5B C3` |
| `0x1E02E00` / 31 | `40 53 48 83 EC 20 48 8B DA E8 C2 9B 73 FE 0F 28 40 10 48 8B C3 66 0F 7F 03 48 83 C4 20 5B C3` |

The 39-byte consumer span `[0xA49525,0xA4954C)` has SHA-256 `DC061797F97B748804636F31183E895C827EBA44C6AC660D6AE0A3B8963256CF`. Its virtual call bytes at `0xA49548` are `41 FF 50 18`; return boundary is `0xA4954C`. The three-float copy span `[0xA4954C,0xA49576)` has SHA-256 `210C544FD68F0A6B01F6477FAD845E6CB5086412ABE23A2962D6EE5E17B82DE1`.

## Specific next observation

The lower-cost [live motion correlation](live-player-motion-observation.md) has now observed a dive and posture recovery at Mother Base. Position changed with the dive; its Y value also changed with posture. Two separately read position sources briefly disagreed during motion. This closes one scene-level correspondence check, while the natural producer and actor identity work below remains required.

To close the producer gap, observe a natural entry to one of the two fully fingerprinted setters and capture its original thread, caller, index selection and opaque source address. At the source-load boundary, `RBX` is the source and `RAX` is the computed opposite-bank record. Accept a bounded 16-byte source sample only after proving the caller's actual ownership and readable lifetime; then match the post-store bytes and identify the caller's supplying object. A missing hit is meaningful only for that tested scene and interval: bulk copies or other producer routes may write the record directly.

A lower-cost next correlation can read only the fixed published-bank vector `+0x10..+0x1F`, under the existing executable/selector guards, while root performs ordinary controlled movement. Label it `published_player_vector_candidate`, retain the non-atomic snapshot limitation, and compare it with the existing named coordinates. This does not require invoking `SetVarsPositionFromNearPlayer`, dereferencing an actor heap chain or changing the current sampler. The appropriate next implementation remains root's decision after completing the current motion observation.

Unresolved: a verified natural setter caller, source actor and generation, world/relative coordinate space, publication cadence, transition invalidation, and whether the same point exists throughout hospital/cutscene/vehicle scenes. No co-op actor capability follows from this vector mapping.
