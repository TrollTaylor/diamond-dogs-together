# Mission and local-player observation candidates

2026-09-09. **The strongest minimal observation path is the game's named `vars` fields plus `PlayerInfo.GetLocalPlayerIndex`. Their static associations and scalar types are established below; no runtime read, call or hook was performed.** A current campaign sequence and the local actor's game-object identity remain unverified.

Input: installed `D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe`, AMD64, version 1.0.15.4, 166517760 bytes. SHA-256: `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`. SHA-1: `4F643576C6F6ECF6C67A9AEF4D4F02285ADA43E3`.

Method: this project's original PE reader, exact NUL-terminated string scans, executable RIP-relative references, and Capstone 5.0.7 instruction decoding from independently found boundaries. No external address map, co-op source, game archive or save was used. All addresses below are **RVAs**, requiring the actual loaded image base. They are evidence, not enabled callable targets.

## Smallest useful field set

Let `B` be the loaded executable base and `P` the pointer stored at `B + 0x2C16920`. The initialization path assigns `P = B + 0x2C01360` at instruction RVA `0x996C9B`. This is an observed static assignment, not proof that the pointer can never change.

| Named field | Address derived from registration | Decoded scalar type | Name / property registration instruction RVA |
| --- | --- | --- | --- |
| `vars.missionCode` | `P + 0x12` | Unsigned 16-bit integer | Name `0x996FCA`, call `0x996FD4` |
| `vars.playerPosX` | `P + 0xEC` | IEEE 754 32-bit float | Name `0x998521`, call `0x99852B` |
| `vars.playerPosY` | `P + 0xF0` | IEEE 754 32-bit float | Name `0x99858C`, call `0x998596` |
| `vars.playerPosZ` | `P + 0xF4` | IEEE 754 32-bit float | Name `0x9985F7`, call `0x998601` |

The containing initializer has PE range `[0x996B70, 0x9AA55B)`. It opens the `vars` namespace using its string at RVA `0x228B0BC`, through the call at `0x996DC7`. For each field it passes the namespace object in `RCX`, name in `RDX`, field address in `R8`, and value adapter in `R9` to helper `0x1A15A90`. This helper reaches `0x1A14BC0`, which pushes the field pointer, creates a one-upvalue closure with the adapter, and associates the closure with the supplied name. Separate 24-byte descriptors also record each field pointer/name pair. These two paths corroborate the association.

The mission adapter is `0x9AAA90`; its read branch at `0x9AAAE3` loads an unsigned word, converts it to a double and pushes one Lua numeric result at `0x9AAAF1`. Its other branch writes a word at `0x9AAAD0` and returns zero results. The position adapter is `0x9AABF0`; its read branch loads a float at `0x9AAC49`, widens it to double and pushes one result at `0x9AAC50`. Its other branch writes a float at `0x9AAC32` and returns zero results. **Both are combined read/write adapters; invoking either as a guessed getter is inappropriate.** Their mode and pointer are supplied through the VM's property-call machinery.

Initialization writes `0xFFFF` to the mission field at `0x996CEA`. That supports distinguishing an initialization value from a confirmed mission, but its complete meaning is not established. Coordinate names/types do not establish update frequency, units, coordinate space, freshness during cutscenes, or correspondence with the currently controlled actor. A finite value alone does not establish actor presence.

## Local-player index and actor identity

`PlayerInfo` has a complete three-entry stack registration at `[0x554B80, 0x554C16)`: `GetLocalPlayerIndex -> 0x554C20`, `AndCheckStatus -> 0x554520`, and `OrCheckStatus -> 0x554C60`, followed by a null pair. The library string is at `0x21961B0`, and the original registration wrapper is called at `0x554BF1` with the incoming VM argument unchanged.

`GetLocalPlayerIndex` spans `[0x554C20, 0x554C52)`. It preserves incoming `RCX`, initializes a stack DWORD, passes its address to leaf helper `0x53C990`, then zero-extends the DWORD into `RDX`, restores the VM argument in `RCX`, and calls numeric-push helper `0x1A11880`. It returns `EAX=1`, **the Lua result count**.

The entire 15-byte leaf is:

`8B 05 3E 76 69 02 83 F8 10 89 01 0F 92 C0 C3`

It reads the DWORD at `B + 0x2BD3FD4`, writes that value to its caller-provided address, and returns a Boolean indicating unsigned value `< 16`. The Lua wrapper ignores this Boolean. The comparison is not proof of 16 allocated actors, four-player support, or a valid actor for every index. The index is also not a game-object ID or a Steam identity.

The actual `GameObject` namespace is independently registered in `[0xBF9E30, 0xBF9FD8)`. Named associations include:

| Binding | Function RVA | Registration call RVA | Statically supported contract |
| --- | --- | --- | --- |
| `GameObject.GetGameObjectId` | `0xBF9FE0` | `0xBF9EA5` | Examines one/two VM arguments, uses conversion/resolution helpers, pushes a zero-extended 16-bit ID, returns one Lua result; failure path pushes `0xFFFF` |
| `GameObject.GetGameObjectIdByIndex` | `0xBFA110` | `0xBF9EC8` | Converts argument 1 and checks/converts numeric argument 2; passes the converted first value and low 32-bit index to resolver `0xBF4090`; pushes its 16-bit output and returns one result |

The same registrar publishes `NULL_ID = 0xFFFF` at `0xBF9FB3`. Resolver semantics, accepted type names, index correspondence with `PlayerInfo`, object lifetime, and callback side effects are not yet established. Do not construct an active `GetGameObjectIdByIndex` query from these candidates. Another `GetGameObjectId` occurrence belongs to the unrelated `Gimmick` namespace at registrar `0x65CC80`; string name alone would have selected the wrong function.

## Sequence remains unresolved

Exact NUL-terminated `GetCurrentMission`, `GetCurrentSequence`, `GetCurrentSequenceName`, `GetMissionCode`, `GetPlayerGameObjectId`, and `TppSequence` strings were absent in this executable. This does not establish absence from game scripts or other registration forms.

The `TppGameSequence` registrar at `0x53A040` exposes platform/version, pause-menu and resident-block facilities; its inspected registration does not expose current campaign sequence. `TppSimpleGameSequenceSystem` initialization at `0x994510` pushes a closure at `0x993AE0`, which immediately jumps to RVA `0x7659870`; that path was not resolved in this bounded investigation. Neither name should be relabeled a current-sequence getter. Mission code alone cannot identify the playable hospital, cutscene or objective sequence.

## Next bounded observation

Prefer natural execution observation using the already fixture-tested debugger approach, after checking the exact executable and live bytes. The narrow points immediately before existing value-push calls expose the values without evaluating any game function or editing VM state:

| Candidate stop | Exact instruction bytes | Values present before the original instruction executes |
| --- | --- | --- |
| `0x554C42` | `E8 39 CC 4B 01` | `RCX` opaque VM identity; `RDX` local-player index |
| `0x9AAAF1` | `E8 5A 6E 06 01` | `RCX` opaque VM identity; `RAX` field address; low `XMM1` double value |
| `0x9AAC50` | `E8 FB 6C 06 01` | `RCX` opaque VM identity; `RAX` field address; low `XMM1` double value |

The latter two adapters are shared by other variables: a hit only identifies the intended field when `RAX` equals the current backing pointer plus the registered offset. A natural hit can correlate thread, state, pointer and value. No hit is not a request to manufacture a VM call. Preserve registers, instructions, return path and clean debugger detachment.

After that correlation, a future original reader can copy only the pointer, two mission bytes, twelve coordinate bytes and four index bytes using guarded reads at a verified observation boundary. Reject unexpected pointer changes, unreadable ranges and non-finite coordinates. Report unknown validity rather than inventing actor or mission readiness. Repeated pointer checks alone cannot establish an atomic mission/coordinate snapshot; ownership and transition behavior must be observed.

First runtime acceptance is agreement between named natural reads and a bounded snapshot across an ordinary loaded mission and return to title, including movement and a transition. Actor identity and current sequence stay explicitly unknown until separately verified. No polling of Lua, mission changes, save access or remote actor creation is authorized by this static report.

**Decoder limitation:** PE exception records may describe fragments. For example, the initial record for `0x9AAA90` ends at `0x9AAAAF`, while its followed read/write branches continue through `ret` at `0x9AAB00`; the float adapter continues through `0x9AAC5F`. Full decoding of one `.pdata` range is not proof that all function paths were inspected. The branches used for the contracts above were followed explicitly.
