# Natural Steam initialization caller

2026-09-09. File-only analysis of the installed AMD64 `mgsvtpp.exe` 1.0.15.4 identifies a concrete natural `SteamAPI_Init` call at **game RVA `0x1428`**, returning to **`0x142E`**. No game process was accessed, launched, or modified for this investigation. All code and analysis are original; no other mod is an implementation source.

Executable SHA-256: `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`. Preferred image base: `0x140000000`. Shipped `steam_api64.dll` SHA-256: `08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A`. Every address below is relative to its stated image and requires the actual loaded base.

## Exact game call and return

The ordinary import table binds `SteamAPI_Init` to game IAT RVA `0x208E130`. The containing semantic routine starts at `0x13A0`; its adjacent PE exception ranges are `[0x13A0,0x14C4)`, `[0x14C4,0x1595)`, and `[0x1595,0x15C8)`. Decoding from the first range's declared start confirms the call's instruction alignment.

| Game RVA | Exact bytes | Meaning |
| --- | --- | --- |
| `0x1423` | `E8 F8 2A 00 00` | Call internal helper `0x3F20`; its semantics are not established here. |
| `0x1428` | `FF 15 02 CD 08 02` | Call the pointer at IAT `0x208E130`, `SteamAPI_Init`. |
| `0x142E` | `84 C0` | Test returned `AL`. |
| `0x1430` | `75 0C` | Continue at `0x143E` when `AL` is nonzero. |
| `0x1432` | `B9 01 00 00 00` | Supply termination status `1` on the false path. |
| `0x1437` | `FF 15 3B C5 08 02` | Call imported `exit` through IAT `0x208D978`. |
| `0x143E` | `FF 15 E4 CC 08 02` | Call imported `SteamUser` through IAT `0x208E128` on the success path. |

The routine first constructs a security descriptor and named mutex, with failure/already-existing branches before the Init call. Consequently, starting the process alone does not guarantee arrival at `0x1428`. The pointer at game RVA `0x20A0940` was subsequently identified as virtual-method slot `+0x10` in table `0x20A0930`, with a concrete dispatcher call at `0x638B2`. It is not a CRT constructor-table entry. The method requires an object pointer in `RCX` and returns a Boolean-like result in `AL`; its result covers more work than the Steam call alone. See the [early-entry investigation](steam-early-entry-investigation.md) for the decoded caller chain and unresolved installation timing.

Searching executable sections for ordinary RIP-relative import calls/jumps found one Init reference, the decoded call above. No additional common RIP-relative MOV/LEA reference to its IAT slot was found. These bounded patterns do not establish exhaustive control flow through encoded code or dynamically obtained pointers.

## InitSafe and shutdown

The executable has no ordinary import or exact NUL-terminated ASCII name for `SteamAPI_InitSafe` or `SteamAPI_Shutdown`. Its delay-import directory is zero. Both names are exports of the shipped Steam DLL:

| Steam DLL RVA | Export / exact bytes | Static observation |
| --- | --- | --- |
| `0x5420` | `SteamAPI_Init`: `33 C9 E9 49 F5 FF FF` | Set `ECX=0`, tail-jump to internal routine `0x4970`. |
| `0x5430` | `SteamAPI_InitSafe`: `B1 01 E9 39 F5 FF FF` | Set `CL=1`, tail-jump to the same routine. |
| `0x59F0` | `SteamAPI_Shutdown`: `40 53 48 83 EC 20 48 8B 0D FB B4 02 00` | Shutdown implementation entry. |
| `0x4BB9` | `E8 32 0E 00 00` | Internal call to shutdown, followed by a false Init return at `0x4BC8`. |
| `0x4BD0` | `E8 1B 0E 00 00` | Another internal call to shutdown, followed by a false return at `0x4BDA`. |

Thus absence of a game shutdown import does not mean shutdown cannot execute: two cleanup paths in the DLL's shared initialization implementation call it directly. The successful tail sets `AL=1` at DLL RVA `0x4BA4`. This does not establish the game's later shutdown/unload paths or justify retaining interfaces indefinitely. Valve distinguishes successful initialization from shutdown, which releases API resources. [Valve lifecycle reference](https://partner.steamgames.com/doc/api/steam_api)

## Earlier observation boundary and capture design

The current DirectInput path calls thunk `0x328926` from game `0x34B1E` (`E8 03 3E 2F 00`). That thunk contains `FF 25 1C 47 D6 01`, jumping through DirectInput IAT `0x208D048`. The call returns to `0x34B23` (`85 C0`). The installed observer runs after the real DirectInput function returns. These addresses alone do not prove execution order; the [existing natural registration evidence](live-registration-observation.md) independently establishes registration before those Steam IAT replacements in its captured run.

The PE entry at game RVA `0x69B5F80` is the earliest **already fixture-tested debugger arming boundary**, with first 17 bytes `48 8D A4 24 68 FF FF FF 48 89 84 24 20 00 00 00 48`. It is not proof of observing all startup activity: the executable has a TLS callback array at RVA `0x9E78000`, listing callback RVAs `0x6CFC4CE`, `0x5AAB920`, and `0xE200`. Their earlier process-attach behavior has not been decoded here. No observer installation inside TLS or DllMain is proposed.

For one fresh, desktop-user-context trial, reuse the validated temporary PE-entry breakpoint, restore its byte, then arm a hardware execution breakpoint at `0x1428`. Verify the exact call/return bytes and that IAT `0x208E130` still equals the shipped DLL's exact `SteamAPI_Init` export. At the natural call instruction, save thread ID `T` and pre-call stack pointer `S` in debugger-local state. Observe return `0x142E` persistently until `T` and `RSP==S` match; this is a call-site pair, so there is **no `+8` stack adjustment**. Record only `AL` as the Init result before the game's own `test al,al`, then remove owned breakpoints and detach with the proven handled-event path.

Repeated/nested entries and unrelated return hits must not overwrite the first call's identity or prematurely consume the return breakpoint. Use the existing bounded no-hit recovery helper. Do not invoke Init, InitSafe, Shutdown, getters, or any game function to manufacture an observation. A captured true result establishes that call's completion only; the runtime query gate remains closed until shutdown coverage and serialization are established separately.

## Generated command and validation status

The original [standalone generator](../tools/generate_steam_init_trace.py) produces this observation without launching anything:

```text
python tools/generate_steam_init_trace.py --executable "D:\Games\steamapps\common\MGS_TPP\mgsvtpp.exe" --output build\steam-init-review\steam-init.cdb
```

File-only generation against both exact installed fingerprints and Python syntax compilation passed on 2026-09-09. The generated call/return and cleanup files were inspected. The independent fixture validation below subsequently passed all ten cases. The separate launcher accepts `-Observation SteamInit`; the default remains network registration. Its later live trial is recorded at the end of this document.

Five files must remain together: the initial `.cdb` and `.bootstrap.cmd`, `.call.cmd`, `.return.cmd`, and `.stop.cmd` siblings. The initial one-shot software breakpoint owns ID 0; persistent hardware call and return breakpoints own IDs 1 and 2. The first call stores its identity once. Nested/other-thread call and return hits retain that identity and continue through `gc`; a repeated call with the same thread and same stack pointer before its matched return aborts as ambiguous. Skip logging is capped at eight messages. Cleanup removes only the recorded owned IDs, then uses separate `.detach /h` and `q` lines. Windows paths use the existing uncondensed `$$<` command-file mechanism and escaping rules.

Markers are `MGS_STEAM_INIT_BOOTSTRAP_ARMED`, `MGS_STEAM_INIT_CALL_ARMED`, `MGS_STEAM_INIT_CALL`, `MGS_STEAM_INIT_RETURN_ARMED`, and `MGS_STEAM_INIT_RETURN`. The return marker contains `tid`, `call_rsp`, `return_rsp`, and `al`; only `al` is interpreted as the API result. Failures use `MGS_ABORT`. Pointer equality with the loaded `steam_api64!SteamAPI_Init` export and its exact entry bytes is rechecked at bootstrap, call, and return. This is distinct from a full runtime module fingerprint or complete lifecycle validation.

## Independent call-site fixtures

The original `steam_init_fixture.exe` imports a no-argument Boolean function from the separate original `steam_init_fixture_api.dll`. It has no Steam SDK dependency. A small AMD64 MASM wrapper with unwind metadata exports labels immediately before its real six-byte RIP-relative IAT call and immediately after it at `test al,al`. The generator's `--fixture` mode maps those exports and the unique import from bounded PE tables; it requires the labels to differ by exactly six bytes and the call's displacement to resolve to that IAT slot. The DLL export is mapped independently and checked again at runtime.

The API wrapper deliberately returns `EAX=0xA5A50000` for false and `EAX=0xA5A50001` for true. Both values are nonzero as full registers; only `AL` is the Boolean result. The caller fixture records the unmodified raw result and checks its bytes, API bytes, debugger state, worker debug registers, original call count, and absence of an extra suspend count. The fixture's repeated case raises its own handled SEH exception, unwinds through the MASM frames, then calls again from the same thread and stack location. The harness passes that first-chance exception to the fixture's handler with the documented `-xd` option. [Microsoft CDB command-line reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/cdb-command-line-options)

All ten cases passed using the MSVC Release build on 2026-09-09. The fixture and DLL compile with `/W4 /WX`; the shared build's ten CTest suites also passed. CDB fixtures are separate manual checks and are not counted as those CTest suites. Sanitized markers, original fixture results, binary hashes, CDB version, and source run paths are archived in [fixture evidence](evidence/steam-init-fixtures-2026-09-09.json); recovery credentials are excluded.

| Case | Observed result |
| --- | --- |
| True | One natural call and matched return, equal pre/post RSP, `AL=1`, raw result `0xA5A50001`. |
| False | One natural call and matched return, equal pre/post RSP, `AL=0`, raw result `0xA5A50000`. |
| Nested | Same-thread inner call and false return skipped at a different RSP; outer true return captured once. |
| OtherThread | Other thread's inner call and false return skipped; original thread's true return captured once. |
| Repeated | Same-frame retry after natural exception unwind rejected as ambiguous; the first API call unwound and the second returned true after detach. No return was misattributed. |
| WrongBytes | Deliberately wrong bootstrap comparison rejected before capturing a call; fixture code bytes were unchanged. |
| WrongReturnBytes | Deliberately wrong return comparison rejected after the original call; its result remained unchanged. |
| WrongOwner | Fixture-owned IAT replacement rejected at the call gate; the replacement naturally called the retained original once, then the fixture restored its own IAT. |
| NoHit | Two-second deadline interrupted the running fixture, verified the owned interrupt, detached, and allowed its first call to execute afterward. |
| ReturnTimeout | Two-second deadline while the original API was waiting and both hardware breakpoints were armed; verified detach let that same API return true once. |

Every fixture reported a detached debugger, clear worker DR7 enable bits, unchanged call/API code bytes, restored original IAT, successful worker completion, and previous suspend count zero. Both deadline cases recorded `MGS_STOP_DETACHED ... handled=1 interrupt_verified=1`. Wrong-byte tests alter observer comparisons only; WrongOwner changes only the inert fixture's own import slot. Neither test changes game files or processes.

To reproduce a case after a Release build:

```text
pwsh -NoProfile -ExecutionPolicy Bypass -File tools/check_steam_init_fixture.ps1 -Scenario Nested
```

Accepted scenarios are the ten names in the table. Each run writes a unique folder under `build/steam-init-fixture`; NoHit and ReturnTimeout default to two seconds, other cases to twenty. The dedicated debugger server keeps stdin open and uses its completed log without waiting for inherited output pipes. Timeout recovery uses the existing strict original stop helper; it never kills CDB or substitutes target termination for verified detach. Failure cleanup can end only the independently created inert fixture.

These tests validate this generated command composition on the installed debugger and this fixture build. They do not prove arrival at the game's Init call, absence of earlier TLS activity, later shutdown coverage, safe interface retention, callback serialization, or usable multiplayer. The separate live trial below now establishes arrival and the result for one game startup; the runtime SDK query gate remains closed.

## Live natural call and return, 16:28 UTC

After the user explicitly approved returning to title, quitting normally and restarting for this test, the previous game exited normally. A closed-game copy of all seven current save/config files was verified before launch: [save evidence](evidence/save-check-before-steam-init-2026-09-09.json). Earlier backups remain preserved.

The original launcher ran `-Observation SteamInit` in the desktop Steam account. At 2026-09-09 16:28:34 UTC, new game PID `76012` reached the natural Init call at `0x140001428`. The captured thread was `0xD200` (53760), and both pre-call and matching return RSP were `0x15FBD0`. The original export was `0x6EFC5420`; the original return had **AL=1**. Exactly one call and one matching return were logged, with no abort marker or deadline recovery.

The log records removal of the owned breakpoints, `.detach /h`, `Detached`, and debugger exit code zero. A separate process check found the game running and no CDB process. The harness leaves `game_continuation_verified=false` because it cannot validate UI behavior; subsequent UI evidence is recorded separately. [Sanitized capture](evidence/game-steam-init-2026-09-09.json), [markers and detach](evidence/game-steam-init-2026-09-09.log).

The executable, shipped Steam DLL and installed original loader hashes remained the expected values. The observer verified the loaded export pointer and entry bytes, not every loaded DLL byte. It did not add an SDK call, obtain a networking interface, register callbacks or send packets. This result proves successful completion of this one natural Init call. Shutdown/unload serialization, an installed early observation boundary and live transport ownership remain unresolved.

Separate Computer Use verification reached the title prompt, sent Enter and observed the main menu with Continue selected and 69% completion. At 16:31:39 UTC the game remained running, no CDB process was present, and all seven save/config hashes matched the new prelaunch baseline. This verifies startup/menu continuation only, not controller or on-foot behavior. [Continuation and save comparison](evidence/game-steam-init-continuation-2026-09-09.json).
