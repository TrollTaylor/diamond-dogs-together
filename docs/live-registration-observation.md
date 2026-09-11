# Natural game registration entry and return captures

On 2026-09-09 at 14:46:11 UTC, the original observer captured the installed MGSV executable entering its network Lua registrar naturally. This is live observation evidence, not a multiplayer implementation.

A second launch at 15:06:21 UTC captured the matching natural return as well. It recorded `EAX=1`, consistent with the independently decoded Lua result-count convention, then detached cleanly. This result is a completed registration call, not a connection or player count.

## Exact observation

| Item | Observed value |
| --- | --- |
| Executable SHA-256 | `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45` |
| Process | `31640` |
| Image base | `0x140000000` |
| Registrar RVA | `0xA1B2E0` |
| Thread ID | `0xA5E0` / `42464` |
| RCX, retained as opaque only | `0x0D810008` |
| Return address | `0x141A12F54`, image RVA `0x1A12F54` |
| Mission-variable backing pointer | `0x142C01360`, exactly image + `0x2C01360` |
| Callback import | `0x6EFC58A0`, exact `steam_api64!SteamAPI_RunCallbacks` export |
| Initialization import | `0x6EFC5420`, exact `steam_api64!SteamAPI_Init` export |

The two import slots still pointed to the shipped Steam exports at the observed entry. The later original 0.3.0 loader log records successful observer installation and callbacks. This establishes that this registration capture preceded those import replacements in this run. The import values by themselves do not prove the time or result of Steam initialization, nor whether an earlier DirectInput call occurred.

The observed registration thread matches the first subsequently logged callback thread. Another callback thread (`49208`) appears by callback 4. Matching one thread ID does not make the callback pump a valid or exclusive Lua execution boundary.

## Continuation and independent field reads

CDB executed `.detach /h`, reported `Detached`, then exited with code 0. The target remained alive. Computer Use subsequently observed the autosave information screen, clicked its ordinary OK button, and observed the FOX logo and animated title. Enter opened the main menu. Thus there is evidence of game execution and input after detachment, beyond process survival alone. No campaign was selected during this check.

At 14:46:37 UTC the separate original `world_probe.exe` verified the on-disk fingerprint, mapped image size, and selected live code bytes, then sampled three times:

- Mission code raw: `1`.
- Position fields: `[0, 0, 0]`.
- Local player index raw: `0`.

The game was at startup, so these values are **not proof of a playable mission, an allocated player actor, current world position, or a coherent world snapshot**. In particular, a positive mission code cannot alone serve as the gameplay-ready condition. The sampler uses read-only process access, bounds every image-relative read, rejects unexpected backing pointers, and makes no game calls or writes. Its numeric-push call fingerprints supplement the separate static field analysis; they do not independently establish the field offsets.

At 14:58:24 UTC, after Enter had opened the main menu, another read returned mission code `40050`, position `[0.04177, 1198.09912, -1.04203987]`, and index `0`. This shows that the fields change during the transition into the title/menu scene. Nonzero coordinates are also insufficient as a playable-actor condition. No campaign was selected; the two campaign save files still matched their backup hashes at this check. Correlation to actual player movement remains unverified.

During later menu navigation, a mouse click aimed at Quit activated the already selected Continue item before the hover selection changed. This opened the existing helicopter scene. No movement, combat, or mission selection was performed. The game was paused, returned to the title through its normal menu, and exited normally before the second launch. At 15:03:48 UTC, both campaign save files still matched the verified backup hashes, and the sampled fields were unchanged from the main menu. This unintended Continue action is not counted as a successful loaded-actor test. Subsequent menu actions use observed keyboard selection.

## Matched return on the second launch

| Item | Observed value |
| --- | --- |
| Launch UTC / process | `15:06:21` / `42824` |
| Thread | `0x16654` / `91732` |
| Opaque incoming RCX | `0x0DC10008` |
| Entry RSP | `0x15FA28` |
| Required and observed return RSP | `0x15FA30` |
| Matched return address | `0x141A12F54` |
| Returned EAX | `1` |
| Earlier unmatched return | Same thread, RSP `0x15F690`; skipped |

The return gate checked thread, original stack frame, dispatcher-retained `RBX` identity, and the loaded `test eax,eax` bytes. The inner return did not consume the persistent hardware breakpoint. The matching event removed that breakpoint and executed `.detach /h`; CDB exited with code 0. No target registers, return addresses, or VM data were assigned. The known vars pointer and two original Steam import targets matched the first capture.

Computer Use subsequently brought the second launch to the foreground, observed the autosave information screen, pressed Enter, and observed the NVIDIA startup logo followed by the animated title screen with its Press Enter prompt. This separately verifies continued execution and startup input after the paired capture; the launcher's `game_continuation_verified` field remains false because the launcher itself performs no UI check.

## Reproduction and failed launch context

Build with `tools/build.ps1 -Configuration Release`, then use `tools/observe_game_registration.ps1` while MGSV is closed. The script checks the exact executable and uses the fixture-validated temporary entry breakpoint followed by a hardware registrar breakpoint. On a no-hit deadline the original DbgEng client verifies the target, interrupts the existing debugger, detaches with handled status, and confirms no debuggee before CDB exits. No executable file is patched. This is not a permanent in-game hook.

Add `-CaptureReturn` for the paired observation. Its return breakpoint remains armed through unmatched nested frames and is bounded by the same launch deadline. The independent nested-return fixture verified this behavior before the game run.

Launch in the desktop user's context with the running Steam client. An earlier isolated tooling-account run reached a `SteamAPI_Init` failure and never initialized the vars backing pointer. Its 20-second deadline detached successfully. The detached failed startup had no targetable desktop window; only that known failed process was subsequently terminated. It is not counted as a registration capture or a working game launch.

MGSV inherited redirected output handles from CDB. Consequently, waiting unconditionally for pipe EOF could hang the launcher after CDB had already exited. The launcher now reads CDB's completed log and collects pipe output only when its read task has already completed. The successful run recorded both pipe tasks as incomplete while still returning its result promptly.

## Evidence and remaining work

- [CDB capture and detach log](evidence/game-registration-2026-09-09.log).
- [Launcher result](evidence/game-registration-2026-09-09.json). Its continuation field is false because the launcher cannot verify gameplay; the separate later UI observation is described above.
- [Paired entry/return log](evidence/game-registration-return-2026-09-09.log) and [launcher result](evidence/game-registration-return-2026-09-09.json).
- [Independent startup field samples](evidence/world-fields-startup-2026-09-09.jsonl).
- [Main menu field sample](evidence/world-fields-menu-2026-09-09.jsonl).
- [Later field sample after Continue](evidence/world-fields-after-continue-2026-09-09.jsonl).
- [Field derivation](player-observation-candidates.md).
- [Fixture capture, rejection and no-hit recovery](registration-debugger-trials.md).
- [Independently decoded live caller](live-registration-caller.md).

Before any loaded-mission check, seven existing local save/config files were copied into `build/backups/save-before-observation-20260909T145101Z`. Source hashes before and after each copy matched the backup hashes. This is a verified set of stable file copies, not campaign save isolation or a demonstrated restore procedure. No source save file was overwritten.

Still unverified: downstream VM result transfer, VM lifetime, repeated registration within one process, thread ownership across transitions, actual actor correspondence and field freshness, current sequence, compatible live Steam contexts, remote actor creation and synchronization. All campaign support remains unimplemented and unverified.
