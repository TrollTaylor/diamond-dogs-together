# Natural Steam networking interface observation

Evidence date: 2026-09-09. Original code and independent inspection of the installed game; no Dynamite code or dependency. The observer forwards a getter only when the game calls it. It never caches, dereferences or publishes the returned interface pointer.

## Verified static boundary

Executable SHA-256 `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`; shipped Steam DLL SHA-256 `08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A`.

Valve's public historical API declares `ISteamNetworking* SteamNetworking()` with the Steam calling convention. In this AMD64 DLL its named export is RVA `0x5E30`, bytes `48 8B 05 71 B0 02 00 C3`: load the pointer at DLL RVA `0x30EA8` into RAX and return. No arguments or extra initialization are performed by that eight-byte getter. [Valve declaration](https://github.com/ValveSoftware/Proton/blob/proton_11.0/lsteamclient/steamworks_sdk_133/steam_api.h).

The game import slot is RVA `0x208E138`. The original [analysis tool](../tools/inspect_steam_networking_calls.py) checks both complete file hashes, maps the named export, scans executable sections for direct FF15/FF25 IAT references, and verifies all nine discovered calls align with disassembly from declared PE unwind boundaries. [Full static evidence](evidence/steam-networking-callers-2026-09-09.json).

| Enclosing function RVA | Natural getter call RVAs | Subsequent interface use |
| --- | --- | --- |
| `0x1A5F740` | `0x1A5F749`, `0x1A5F758` | Null check then vtable `+0x18` tail call |
| `0x1A5F780` | `0x1A5F789`, `0x1A5F798` | Null check then vtable `+0x20` tail call |
| `0x1A5F8C0` | `0x1A5F8E0`, `0x1A5F8EB` | Null check then vtable `+0x30` call |
| `0x1A5FAC0` | `0x1A5FAD9` | Vtable `+0x10` read, channel argument zero |
| `0x1A5FB60` | `0x1A5FB96` | Vtable `+0` send, mode and channel arguments zero |
| `0x1A5FBF0` | `0x1A5FC0F` | Vtable `+0x08` availability loop, channel argument zero |

The last three slot interpretations agree with the independently checked [packet flat exports](steam-packet-adapter.md). Before the read at `0x1A5FB02`, `0x1A5FAE4` writes zero to `[rsp+0x28]`, the sixth Windows x64 argument (channel). Before the send at `0x1A5FBB5`, zero is written to both `[rsp+0x20]` and `[rsp+0x28]` (mode/channel). Availability calls at `0x1A5FC2F` and `0x1A5FCA0` receive `R8D=0`. These paths therefore use channel zero. This bounded inventory does not prove every game channel or exclude cached/dynamically resolved callers; it does not grant ownership of any other channel.

## Original loader 0.4.0

Installation remains after successful DirectInput creation, outside DllMain. The new getter import observation additionally requires the exact Steam file hash, expected loaded export RVA/bytes, the unique import at the expected slot, original pointer ownership and the existing checked data-pointer exchange. The observer module is pinned before publication. No new game-function detour is installed.

The original getter is called exactly once; the exact pointer and incoming/outgoing Windows error state are preserved. C++ exceptions are rethrown unchanged after recording an exception receipt. An SEH or process-termination exit is not reported as a normal completion. The receipt contains only null/non-null/exception, caller RVA, thread and tick; the pointer never enters shared observer state or a log. A non-null receipt does not validate interface lifetime, ownership, authentication or an SDK query lease.

Logging uses atomic outcome bits for the nine known return sites and one combined unknown-site bucket, plus a one-time multiple-thread discovery. This bounds getter log attempts to 31 per process. Different unknown callers can be folded together; this is an intentionally bounded discovery log, not a complete trace or transition history. Concurrent log order is not execution order, and timing overhead remains possible.

The compiled forwarding tests cover null/non-null results, original exception identity, deliberately noisy observation error state, nested forwarding, no sink and four concurrent callers. Their 15,238 assertions include 400 repetitions of the same scenario set. All 13 Release CTest suites pass; the probe itself now builds with `/W4 /WX`. [Build](evidence/native-tests-with-steam-getter-2026-09-09.log), [detailed tests](evidence/native-test-details-with-steam-getter-2026-09-09.log). These tests do not establish live getter execution or multiplayer support.

## Live installation and scene check

The preceding test session quit normally through the observed Quit Game confirmation. With the game closed, the owned 0.3.0 loader and manifest were copied and hash-verified under `build/backups/probe-0.3.0-20260909T164106Z`. The tested 0.4.0 loader was installed at 16:41:07 UTC, SHA-256 `58C0EE96991C905DEC4528E363E3F10A322223629EDD0677D36C446F4B590FF6`. The executable and shipped Steam DLL remained unchanged. [Installation record](evidence/loader-0.4.0-install-2026-09-09.json).

MGSV launched normally without a debugger at 16:41:22 UTC, PID `106392`. The trace records all three import replacements installed with page protection restored. Keyboard Enter dismissed the autosave notice, reached the main menu, and selected Continue to load the existing helicopter scene. The menu showed 69% completion. The saved trace reaches 3,600 callback calls and two callback threads (`33204`, `95196`), but **contains no natural networking getter receipt** at this checkpoint. Installation succeeded; execution of this new getter wrapper has not been demonstrated live. Absence of a receipt does not prove the networking interface is unavailable or that all networking paths were covered. [Trace](evidence/steam-observer-0.4.0-2026-09-09.log), [live result and save comparison](evidence/steam-getter-live-2026-09-09.json).

The getter's static caller chain can now guide the next independent investigation of when the native packet manager runs. No original online battle or external peer was invoked to manufacture activity, and no runtime SDK gate was enabled.
