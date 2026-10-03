# Native session and join flow: how host and guest connect, how Dynamite differs, and why a guest join can fail

## 1. How the mode is chosen

1. `dinput8.dll` proxy, then `DirectInput8Create`, then `InstallSteamCallbackObserver`. The steam_observer install function (`native/steam_observer.cpp:173-271`) gates on:
   - the exe being named `mgsvtpp.exe` with SHA `085C2F82…`;
   - the proxy path being exactly `<exeDir>\dinput8.dll`;
   - `steam_api64.dll` being loaded from exeDir.
   
   It then calls `InstallGameScriptObserver(base, span, exeDir)` (line 271).
2. `game_script_observer.cpp:519-596` reads `<exeDir>\mgs-coop\script-observe.enabled`, at most 32 bytes. Matching is exact on byte length (lines 536-545):

| Marker bytes | Flags set | Meaning |
|---|---|---|
| `host-v3\n` (8) | host_admission + host_create + host + allocation + archive + bootstrap | Host: create session, then SessionEnableAccept after start |
| `host-v2\n` (8) | host_create … | create, no admission |
| `host-v1\n` / `heap-v1\n` | host only | trial, no creation |
| `client-v1\n` (10) | client + host + … | **Join** (needs client-host.txt) |
| `client-check-v1\n` (16) | client_check + client + … | **Diagnostic**: Ctrl+F8 runs the prerequisite check only, no join |
| `players-v1\n`, `archive-v1\n`, `bootstrap-v1\n`, `observe-v1\n` | lower levels | — |

   A CRLF, a BOM or any other variant disables everything silently; not even the log is opened.
3. For `client-v1` only, it reads `client-host.txt` (lines 557-573). The value is parsed by `ParseClientHostIdentity` (`client_session_request.hpp:12-22`): 17 digits, `(id>>32)==0x01100001`, low dword nonzero. If it is invalid, the observer logs a refusal and **returns without installing the script hook**.
4. `tools/set-session-mode.ps1` writes `host-v3`, `client-v1` (plus client-host.txt) or `client-check-v1`. Line 9 refuses if any mgsvtpp process is running. Line 17 refuses if `probe-install.json` game_directory differs from the target. Lines 18-20 check the version and hashes.

**Installed state (read-only check):**
- `MGS_TPP/mgs-coop/script-observe.enabled` = `host-v3\n` (0.10.33).
- `MGS_TPP_guest/mgs-coop/script-observe.enabled` = `client-v1\n`.
- `MGS_TPP_guest/mgs-coop/client-host.txt` = `76561198000000001\n`, a placeholder.
- The guest copy runs **dinput8 0.10.27** (sha 6B48…), and its manifest's game_directory points at `…\MGS_TPP`.
- No guest logs exist, neither in the guest folder nor in `C:\Sandbox\taylo\MGSV_Guest\drive\D\Games`, which is empty.

## 2. Host, step by step (verified live on 0.10.29/30/33)

1. The script bridge at RVA 0x1A178E0 patches TppMain (`bootstrap_patch.cpp:128-200`). It inserts host_session_trial before `Mission.Start`, the equipment fragment, the marker guard, and the appended wrappers. session_lifecycle is appended last, so it is the outermost wrapper.
2. `OnAllocate` sends the `MGS_COOP_ALLOCATION_ENTER_V1` receipt. `SelectMissionBoundary` (`game_script_observer.cpp:119-183`) records the allocation thread and mission.
3. The marker-loading BEGIN receipt runs on the allocation thread.
4. `host_session_trial.lua:15` compiles `MGS_COOP_HOST_CREATE_V1`, handled at `game_script_observer.cpp:266-282`:
   - `PrepareDynamitePlayerLoading` (`host_session_trial.cpp:143-157`): HostPrerequisites (lines 84-108) requires mission 30050 and a FobTarget at `*(*(base+0x2BEFC00)+0x30)+0x28` with vtable 0x2223508, `+0x10==0` and `+0x48==0`. It then installs the ghost, team (incl. targeting init), combat, trap, result, equipment, marker and soldier patches.
   - `TryCreateHostSession` (lines 159-178) leads to `CreateDynamiteHostSession` (`dynamite_session_port.cpp:325-367`):
     - verifies 38 body hashes;
     - pins the module;
     - installs the allocation bridge (0xDCB50 hooked; sites 0x7E131E and 0x7E1362 rerouted to 0xDBAB0);
     - calls `0x7EE590(target)`.
     
     Natively, `0x7EE590` calls IsConnectReady (0x7E4190), creates the session (0x3130C0) into root+0xCC8, calls 0x3144D0 on the host path (state 2, E2=1), calls EnableAccept(0), and sets target+0x10=2.
   - Lua sets `hostSessionCreated`, `S_IS_ONLINE` and the damage rules.
5. Equipment ghost groups are created (ghostCount=1), which sets `equipmentPreparationReturned`.
6. `OnInitialize` enter/return receipts. Then `OnMissionGameStart` sends `RUNTIME_ENTER`, which runs `TransferRuleThread` and `AdoptDynamiteSessionThread` (requires state 3).
7. `host_admission.lua:23-31` calls `TppNetworkUtil.SessionEnableAccept(true)` after `CanChangeDynamiteHostAdmission` (lines 207-225); accept becomes 1.
8. session_feedback shows 'host_ready'. Termination: SessionEnableAccept(false), then the close gate, then `ReconcileDynamiteSessionRelease` once the slots are empty.

Live evidence in the host `script-observer.log`:
- line 9198: `dynamite_host=returned; result=1`;
- line 9216: state=3;
- line 9251: `host_admission=authorized`;
- line 9255: accept=1.

## 3. Guest, step by step (compiled only; never executed natively)

1. Steps 1-3 are the same as the host's. On the `HOST_CREATE` receipt, `needs_player_patch` is true and the player patches install. `TryCreateHostSession` is **not** called (`!client_enabled`). The replacement returns `'client'` (line 278). Lua sets `clientPrepared` and `S_IS_ONLINE`. The guest loads **its own** Mother Base with no session.
2. At `OnMissionGameStart`, `client_join.lua:22-38` checks `eligible()` (30050, clientPrepared, equipmentPreparationReturned, not sessionLost). The `CLIENT_READY` receipt returns 'manual' only on the runtime thread with player_team active, no pending restore, not terminating and not consumed. The guest sees the announcement "press Ctrl+F8".
3. Every `OnUpdate`, the `CLIENT_POLL` receipt runs `game_script_observer.cpp:423-456`:
   - the foreground window PID must equal this process;
   - `GetAsyncKeyState(VK_F8)` must first be seen up, then F8 down with Ctrl down;
   - `client_join_consumed=true` is set **before** any native call.
4. `TryCreateClientSession` (`host_session_trial.cpp:180-194`):
   - the shared one-shot `attempted` flag;
   - CodeMatches (host body);
   - ObserveAllocationContext;
   - HostPrerequisites (target state 0, flag 0, mission 30050);
   - PlayerReadiness (player at 0x2A82C30, vtable 0x2295490, limit +0x208 == 2).
5. `CreateDynamiteClientSession` (`dynamite_session_port.cpp:402-457`):
   - `ClientPrerequisites` (lines 370-389): `client_join_bodies` hashes (0x7EE380, 0x3144D0, 0x7D6470, 0x315140, 0x3152D0, 0x315BE0, dispatch 0x31535C); `info = *(target+0x40)`; `info.state==0`; root+0xCC8 null; s_pSession (0x2B9DFB0) null; info is in RW committed memory.
   - `PrepareSessionAllocation`: the same bridge as the host.
   - `SubmitClientRequest` (`client_session_request.hpp:38-47`): `info.host(+0xB8)=host`, then `0x7EE380(target, &info)`. The previous host value is restored on a synchronous false.
   - Natively, `0x7EE380` runs IsConnectReady, checks `[info]==0`, creates the session, sets target+0x10=1, reads `[target+0x40]+0xB8` (0x7EE4FD), passes args to `main+0x20 vtbl+0x40` (0x3144D0, client branch: SppSocket vtbl+0x20, state 4, E0=1, E1 cleared), then sets target+0x48=1 and target+0x10=2.
   - On true, it re-reads the new session (vtable 0x2126F70, Start slot +0x68 = 0x315140, state 4-7), records client ownership, and calls Start, which sets +0xE1=1. State 6 needs E1 to advance to 7.
6. Lua: 'submitted' sets `clientJoinRequested`. session_feedback polls status every 60 frames:
   - `client_connecting` (state 4-7);
   - `client_established` (state 7, E1, `ReadRoster` with two present members and host≠local);
   - `session_error` (E4).
7. Loss: `Nt.SessionDisconnectFromHost` leads to `SESSION_LOST` and rule restore. There is no reconnect.

**Where the host SteamID comes from:** only the user-supplied `client-host.txt`, parsed once at startup into the process global `client_host`. It is written into the game-owned `SessionConnectInfo+0xB8`, the tpp-mod `session_connect` technique (`session.cpp:473-486`). There is no in-game discovery, no friend/lobby lookup and no tool that captures the host's ID.

## 4. What Dynamite does differently (pinned c90f252, old exe 1.0.15.3)

| Aspect | Dynamite | mgs-coop |
|---|---|---|
| Host SteamID | `SetSteamIdHook` (DynamiteHook.cpp:130-133) overwrites every SetSteamId call with cfg.HostSteamID; the connect info is zeroed | writes connect-info+0xB8; **no SetSteamId hook** (unmapped in 1.0.15.4) |
| Client target object | fresh BlockHeapAlloc'd FobTarget plus SessionConnectInfo, targetIPCString=&3, freed after the call (DynamiteLua.cpp:104-111) | borrows the game's FobTarget and its connect info; requires state 0 |
| IsConnectReady | hooked, always true (line 135) | original predicate kept (0x681CB90) |
| Connection filtering | OnP2PSessionRequest hook with whitelist/blacklist, then calls the original (85-128) | none (function unmapped) |
| Established hook | UpdateClientEstablishedHook announces and starts the sync channel (137-160) | polled status only |
| Reset | `Dynamite.ResetClientSessionState()` (mission end) and `...WithNotification` (manual key) reset Dynamite's flags and stop sync, with no native call (DynamiteLua.cpp:53-75) | **none**; one-shot per process |
| Join timing | client connects from the ACC **before** the mission, gets "Connection established", then both start the same mission (docs.md:194-205) | client joins **inside** an already-loaded Mother Base |
| Missions | 36 story missions (TppMain.lua:99-146); Mother Base not included | Mother Base 30050 only |
| Master server | redirected to local fuse at 127.0.0.1:6667 (dynamiteHooks.cpp:385-390, dynamitePatches.cpp:247-277) | Konami's official server (string RVA 0x2218708, lea at 0x7D346C) |
| Other | SetTimeOut(10); a 'start FOB P2P resolver always' patch; network heap workaround only on the host | none of these; bridge used for both roles |

Dynamite's MAKING_OF.md says the client provides the host SteamID "by either selecting a player target in UI or hardcoding it in the exe". The fuse master server is what "reliably connect[ed] two players together on FOB map".

## 5. Every reason a guest join can currently fail

**Install and configuration**
1. The guest copy runs 0.10.27. Its HOST_CREATE receipt is accepted only on the TppMain compile thread (`git show 8542970:native/game_script_observer.cpp:154`). Worker-thread Mother Base allocation therefore leaves `clientPrepared` unset, and no Ctrl+F8 prompt appears. This failure mode was observed in the user's 0.10.27 log on Sept 11.
2. `client-host.txt` holds the placeholder 76561198000000001, so the guest would target the wrong account.
3. The marker formatting must be exact; a CRLF or BOM silently disables the mod.
4. An invalid client-host.txt means no hooks are installed at all.
5. set-session-mode.ps1 refuses the guest directory (manifest mismatch), and its process check blocks it while the host instance is running.
6. Steam identity: P2P needs two different licensed accounts. The sandboxed Steam must use account B, and B needs a save that has unlocked Mother Base plus online login.

**Synchronous refusals (shown as "join could not start"; burns the one attempt)**

7. Any body-hash mismatch: client_join_bodies, the 38 session bodies, or the host-create body.
8. HostPrerequisites: mission ≠ 30050, wrong FobTarget type, or target +0x10/+0x48 nonzero.
9. PlayerReadiness: player limit ≠ 2.
10. ClientPrerequisites: connect-info state ≠ 0, an existing session, or bad memory.
11. Allocation-bridge install or scope failure.
12. **IsConnectReady** (inside 0x7EE380): byte[0x2BD7BC0] ≠ 0, or int[0x2BD7BB4] < target+0x14. Not sampled on the guest; Dynamite bypasses this check.
13. The native `[info]==0` check (tpp-mod forces it to 0; mgs-coop refuses instead).

**Gating before the request**

14. Lua eligibility: equipment preparation not returned, sessionLost set, wrong thread, or a pending restore.
15. Input: the key must be held while focused, and F8 must first be seen released. The poll checks current key state once per OnUpdate, so short or synthetic taps are missed (automated taps failed live). There is no controller binding.

**Asynchronous failures (state stuck at 4-6, error, or disconnect)**

16. The host is not in Mother Base with accept=1 at that moment. Pressing early wastes the only attempt.
17. Host acceptance depends on the unmapped original `OnP2PSessionRequest`.
18. The host ID may not propagate from +0xB8 to the socket's SetSteamId, for example if the game uses +0x70 or the P2P resolver result instead. Dynamite overrides SetSteamId globally.
19. The master-server or resolver dependency, and the risk of a ban from Konami.
20. Late join: neither scene was loaded with a session, so player-index or ghost mapping may break even at state 7. Each side is also on its own save's Mother Base.
21. Start is called manually at state 4, an untested ordering.
22. Client allocations made outside the two rerouted sites may land in the mission block.
23. There is no timeout and no SetTimeOut(10), so the connection can stay pending indefinitely.
24. Sandboxie BlockPorts/BlockNetworkFiles may interfere.
25. A same-thread Lua re-entry while holding receipt_lock would deadlock (low risk).

**Retry**

26. There is no reset. `attempted` and `client_join_consumed` last for the whole process, and the borrowed FobTarget ends at +0x10=2, so HostPrerequisites refuses a retry. A restart is mandatory.

## 6. Equivalents of the Dynamite pieces asked about

- **SetSteamId hook:** none. Replaced by the connect-info+0xB8 write; the function itself is unmapped in 1.0.15.4.
- **`Dynamite.ResetClientSessionStateWithNotification`:** none. The nearest analog is `ReconcileDynamiteSessionRelease`, which only clears adapter ownership after native teardown and never re-arms joining.
- **IsConnectReady override:** none. The predicate was decoded (docs/session-feedback.md, docs/evidence/connection-readiness-2026-09-10.json) and intentionally left in place.

## 7. Static checks made in this pass (read-only, capstone on the installed exe)

- 0x7D6470 is GetFobTarget (`[rcx+30h]`, then `[rax+28h]`).
- 0x315140 is `mov byte [rcx+0E1h],1; ret`.
- Secondary vtable 0x2127040: slot +0x40 = 0x3144D0, slot +0x60 = 0x312B80.
- Primary vtable 0x2126F70: slot +0x68 = 0x315140, slot +0x70 = 0x3134A0.
- 0x494110 is `mov rax,[base+0x2BA7B40]; ret`.
- The global 0x2BD7BB4 is written via setter 0x547390 from tpp::net code at 0x7DEF62..0x7DFF4E with constants 1-3; byte 0x2BD7BC0 is zeroed at 0x8E5B1C/0x8E5DED.
- The lea at 0x7D346C points to the Konami gate URL at RVA 0x2218708.
