# mgs-coop synthesis: status, join blockers, single-PC harness, characters, gaps, campaign

Scope: MGSV:TPP exe 1.0.15.4 (SHA-256 085C2F82…BB45), mod 0.10.33. Sources are the 9 reader reports. I checked five points myself, read-only, and mark them [synth-checked].

Evidence labels used throughout:

| Label | Meaning |
|---|---|
| LIVE | Seen in a real game run's logs |
| COMPILED | Builds and unit/fixture tests pass, but never run in game |
| STATIC | Decoded from the exe; not run |
| SPEC | Inference or speculation |
| MISSING | Does not exist |

---

## 1. Where we are

**Verified live.** All live runs were host-only, in Mother Base 30050, on builds 0.10.29 to 0.10.33.

1. **Loader chain (LIVE).**
   - The `dinput8.dll` proxy hands off to the steam_observer gates (`native/steam_observer.cpp:173-271`). These check the exe SHA, that the proxy sits at `<exeDir>\dinput8.dll`, and that `steam_api64.dll` loads from exeDir.
   - Then `InstallGameScriptObserver` runs (`native/game_script_observer.cpp:519-596`). It reads the byte-exact marker `<gameDir>/mgs-coop/script-observe.enabled`; the host's marker is `host-v3\n`.
2. **Custom archive mount (LIVE).**
   - File: `mgs-coop/mgs-coop-player-runtime.dat`, SHA 26CE7C21…, 3,720,192 B.
   - Mounted with `CreateWithPackFile` at RVA 0x48E20, flags 0x21.
   - Contents: TWO-slot TppPlayer2 data (totalCount=2, partsBlockCount=2), the mtbs_mission_fob MTAR, and a renamed f30050 package with `single_player2_instance.fox2` removed.
3. **Lua splices (LIVE).** A MinHook detour on `luaL_loadbuffer` (RVA 0x1A178E0) splices embedded Lua into Tpp.lua, TppMain.lua, TppMission.lua, TppPackList.lua and DamageParameterTables.lua. Each target is pinned by exact size and hash. All five showed `replacement=1` in run PID 52824. Lua talks to native code through `loadstring("return 'MGS_COOP_*_V1'")` receipts.
4. **Host session (LIVE, no peer).**
   - `CreateHostSession` RVA 0x7EE590 is called on the game-owned FobTarget and reaches state 3.
   - Ownership moves from the loading worker (thread 83844) to the gameplay thread (54152) via `AdoptDynamiteSessionThread`.
   - `SessionEnableAccept(true)` sets accept=1.
   - Release is reconciled on exit.
   - Log evidence: `MGS_TPP/mgs-coop/script-observer.log` lines 9184-9256.
5. **Gameplay rules: installed, effects unverified.** Install receipts are LIVE for:
   - damage branch 0xFFA25F
   - soldier patches 0x136BD2B and 0x1373827
   - combat-role detour 0x313FE0 (3 exact callers)
   - result-submit filter 0x7F31F0
   - trap detours 0x587E70 and 0x588850
   - ghost check 0x9C176A
   - team/index patches 0x1E03080, 0x1E030B0 and 0x1E026B0 (index forced to 15)
   - marker icon remap 0x935020
   - marker-loading guard
   - equipment no-use patch (this override was actually observed firing)

   None of these has produced a gameplay effect yet, because there has never been a peer.
6. **Steam IAT wrappers (LIVE).** The only networking activity ever logged is the availability-poll getter at caller 0x1A5FC15, on two threads. Send, Read and Accept have never run.
7. **Diagnostic mode (`client-check-v1`).**
   - On 0.10.30 the automatic prerequisite check passed LIVE.
   - On 0.10.33 the check is routed through Ctrl+F8. The poll armed, but automated key taps never registered, so the physical key path is unverified.
8. **Build health (verified by a reader this session).**
   - `tools/build.ps1 -Configuration Release` passes 21/21 CTest suites.
   - A clean rebuild gives 0 warnings under /W4 /WX.
   - The installed `dinput8.dll` (FCB0E16A…) equals the working tree except for 6 timestamp bytes.
   - All manual Lua/patch checks pass except the stale `tests/host_admission_lua_check.lua`.

**Compiled only (never executed in game)**

9. **Guest join path:** `client-v1` marker plus `client-host.txt`, then the `CLIENT_READY`='manual' receipt, then Ctrl+F8. That calls `CreateClientSession` RVA 0x7EE380 with the host SteamID written to SessionConnectInfo+0xB8, then Start at 0x315140 (`native/dynamite_session_port.cpp:402-457`, `native/include/mgscoop/client_session_request.hpp:38-47`).
10. **Session lifecycle:** status feedback (`ReadDynamiteSessionStatus`, `dynamite_session_port.cpp:255-289`), loss handling (`lua/coop/session_lifecycle.lua:39-74`), and the explicit close sequence with a live session.
11. **Gameplay pieces:** respawn (branch 0x17A16BE, armed per respawn), death policy, and enemy-targeting commit (needs 2 members).
12. **The mod's own P2P layer** (`native/steam_packets.cpp`, `protocol.cpp`, `player_sync.cpp`): linked into dinput8 but never called.

**Missing**

13. **A guest has never launched.** There are no logs in `MGS_TPP_guest/mgs-coop` and none under `C:/Sandbox/taylo/MGSV_Guest/drive/D/Games`.
14. **The guest copy is stale.**
    - Loader is 0.10.27 (6B484154…).
    - Its `probe-install.json` game_directory points at `…\MGS_TPP`.
    - `client-host.txt` holds the placeholder `76561198000000001` [synth-checked].
15. **No second identity and no way to run a second instance.** Nothing gets past the single-instance mutex, there is no loopback/identity shim, and gbe_fork was never built. The sandboxed Steam only ever logged in the main account and ended in `LogonFailure No Connection`.
16. **No master-server redirect.** The game still uses Konami: URL string at RVA 0x2218708, referenced by the `lea` at 0x7D346C. fuse is not vendored or built.
17. **No save isolation.**
18. **Join robustness pieces missing:**
    - no retry/reset (joining is one-shot per process)
    - no pre-mission (ACC) join path
    - no IsConnectReady, SetSteamId or OnP2PSessionRequest parity with Dynamite
    - no `SetTimeOut(10)`
19. **Everything is hard-gated to mission 30050:** 11 Lua sites plus native sites at `host_session_trial.cpp:92`, `dynamite_session_port.cpp:133`, `full_close_patch.cpp:57` and `game_script_observer.cpp:301,492-493`. There is no story-mission support, no `TppCoder.SetWorldCenter`, and no per-mission packs or sequence edits.
20. **No character selection.** Dynamite's Quiet/Ocelot patches are not ported.
21. **No DynamiteSync equivalent** (markers, sight marks, emblem, Boss Quiet), no permanent partner marker, and no WarpToPartner.
22. **Repo hygiene.**
    - Versions 0.10.28 through 0.10.33 are uncommitted; HEAD is ae88752, which is 0.10.27 plus two commits.
    - The build depends on 43 untracked paths.
    - `third_party/minhook` is a gitlink with no `.gitmodules`, so a fresh clone will not build.
    - The Lua/patch checks are not in CTest.
23. **Damage divergence.** `lua/coop/damage_parameters.lua:12-24` clears field 13 on 146 rows. Dynamite's equivalent loop is a no-op, so the Dynamite build that was actually tested never cleared it.

---

## 2. Why no guest has joined yet (ranked)

**R1. The guest code path has never run, because a second instance has never run. (Certain, environmental.)**
- **Mutex.** At RVA 0x13F9 the exe calls `CreateMutexW(L"METAL GEAR SOLID V: THE PHANTOM PAIN")` (string at 0x20A0980, local namespace). If `GetLastError()==0xB7` (check at 0x1415) it exits via 0x1595. This happens before `SteamAPI_Init` (0x1428) and before `DirectInput8Create`, which is where the mod currently installs everything (STATIC).
- **No second identity.** The transport is ISteamNetworking005 P2P keyed by SteamID. Two processes on one account get the same SteamID, so they cannot connect to each other.
- **Stale guest.** Loader 0.10.27 only accepts the HOST_CREATE receipt on the TppMain compile thread (`git show 8542970:native/game_script_observer.cpp:154`). Mother Base allocation runs on a worker thread, so `clientPrepared` is never set and the Ctrl+F8 prompt never appears. This failure was observed in a 0.10.27 log on Sept 11.
- **Placeholder host ID** in `client-host.txt`.
- **Tooling refuses the guest folder.**
  - `tools/set-session-mode.ps1:17` checks game_directory against the folder [synth-checked].
  - Line 9 refuses if any `mgsvtpp` process is running, so you cannot configure one instance while the other runs [synth-checked].
  - `tools/package-install.ps1` rejects the guest's ownership record.

**R2. The join trigger cannot be automated and only fires once. (Certain.)**
- It is a `GetAsyncKeyState` current-state poll, once per OnUpdate, gated on the MGSV window being in the foreground (`game_script_observer.cpp:423-456`). Synthetic taps were missed live (`docs/testing-2026-09-12.md:147-162`).
- `client_join_consumed` and `attempted` are set for the whole process lifetime, and are set before the native call (`host_session_trial.cpp:24,160,182`; `game_script_observer.cpp:40,439`).
- After 0x7EE380 runs, the borrowed FobTarget is left at +0x10=2 and +0x48=1, so `HostPrerequisites` refuses any retry. Any failure, including pressing too early, means restarting the game.

**R3. No observability. (Certain.)** `ReadDynamiteSessionStatus` computes `host_peer_present` and `client_established`, but returns them only to Lua and never logs them (`game_script_observer.cpp:283-289`). A failed attempt cannot be localized from logs.

**R4. The join model differs from Dynamite's. (Likely problem; unverified.)**
- Dynamite's client connects from the ACC before loading, then starts the same mission (`webui/docs/docs.md:194-205, 239-246`).
- mgs-coop joins in the middle of an already-loaded 30050, which Dynamite never supported: 30050 is not in `IsCoop`, `moddata/standalone/TppMain.lua:99-147`.
- Each side has also loaded its own save's Mother Base.
- So even if the session reaches state 7, the remote actor may never be realized and player indices may be wrong (SPEC, high impact).

**R5. Missing Dynamite parity that may be needed. (Possible.)**
- **IsConnectReady** (0x7E4190, which jumps to 0x681CB90) is not bypassed. It requires byte[base+0x2BD7BC0]==0 and int[base+0x2BD7BB4] >= FobTarget+0x14. These values were never sampled on a guest, and a synchronous refusal burns the single attempt.
- **No SetSteamId override.** The host ID travels only through ConnectInfo+0xB8 (0x7EE4FD, then 0x3144D0, then SppSocket vtbl+0x20). That path is untested; if the game later uses +0x70 or the resolver output, the guest connects to the wrong peer.
- **targetIPCString at +0xC8** must be non-null on the old exe; whether 1.0.15.4 needs it is unknown.
- **No `SetTimeOut(10)`**, so a pending connection can sit forever.

**R6. Online / master-server state. (Possible.)**
- A fake or second identity cannot log into Konami.
- The global at 0x2BD7BB4 is written only by tpp::net code (setter 0x547390, constants 1-3), which suggests login state (SPEC).
- Dynamite relied on fuse to "reliably connect two players".
- Sending co-op traffic to Konami carries ban risk.

**R7. Lower-probability risks. (SPEC.)**
- Client allocations outside the two rerouted sites (0x7E131E, 0x7E1362) may land in the mission block.
- Start (0x315140) is called at state 4; that ordering is untested.
- `CreateClientSession` runs under the non-recursive `receipt_lock`, so Lua re-entry on that thread would deadlock. The host path does the same and was fine live.

---

## 3. Single-PC test harness

### 3.1 Hard constraints (STATIC, from the steam-transport and single-pc readers)

**Steam surface the game uses**
- 13 steam_api64 imports and no Matchmaking import.
- ISteamNetworking005 slots used: Send[0], IsP2PPacketAvailable[1], Read[2], Accept[3], Close[4], GetP2PSessionState[6]. Sends are mode 0 on channel 0.
- Callbacks registered: 1202 `P2PSessionRequest_t` and 1203 `P2PSessionConnectFail_t`.
- No interface pointer is cached in a global, so wrapping the IAT getters intercepts all Steam use.

**When the game touches identity and the mutex**
- The mutex check (0x13F9) and `GetSteamID` (getter at 0x143E, vtable call at 0x1452, validity check 0x1B10) run before DI8.
- The local-account cache 0x4CE0A0 stores SteamID at +0, persona name at +8, logged-on flag at +0xB0. Its timing relative to the current install point is unknown.

**Host accept path.** The host's packet manager accepts a peer only in response to a 1202 event (0x1CEE420 calls Accept 0x1A5F740 and sets the record to state 2). Any emulation must inject 1202 for the first packet from a new peer.

**Hang hazard.** The poll loop (0x1A5FC50-0x1A5FCA5) spins forever if a packet is reported available but never consumed. Never report packets from an unaccepted session.

**Saves.** `GetUserDataFolder[6]` (0x170537, 0x54CBD5) plus RemoteStorage012 slots [0], [1], [3], [10] and [12] mean two same-account instances would share `userdata/97930372/287700/local` and the cloud files in `311340/remote`.

**Focus and input**
- Keyboard and mouse raw input go to the foreground window only.
- XInput is polled at 0x34F90 with no focus check, so one pad drives both instances.
- Fullscreen and borderless minimize on focus loss, and minimizing pauses (IsIconic poll at 0x1C19). Only true `Windowed` mode keeps running unfocused. Current config is `FullScreenWindowed` 2560x1440.

**Logs** are already isolated per folder: everything goes to `<exeDir>/mgs-coop/` (`probe.cpp:133-150`).

### 3.2 Options and the recommendation

| Option | Identity/transport | Pros | Cons |
|---|---|---|---|
| A: in-mod loopback shim ("lab mode") | Emulate ISteamNetworking005 over 127.0.0.1 UDP; fake guest SteamID via SteamUser/SteamFriends proxies; synthetic 1202/1203 | Keeps the shipped steam_api64.dll and its hash gates (`steam_observer.cpp:198-227`); reuses live-proven IAT infrastructure (`import_table.cpp:395-448`, `win_import_patch.cpp:337-365`, the RunCallbacks wrapper); gives packet traces and fault injection; no third-party binary | Must match the hidden-return CSteamID ABI; identity-cache timing; real `SteamAPI_Init` in a second same-account process is untested |
| B: gbe_fork in two lab folders | Goldberg IDs plus LAN P2P (binds port+i) | Mature emulation; no vtable code | Build never completed (`build/gbe_fork` @327815e); replaces the shipped DLL (needs a hash-gate exemption; conflicts with the AGENTS.md rule); both instances must be gbe; unknown behaviour of the old interface strings in `.xdata` |
| C: Sandboxie plus a second licensed Steam account | Real Steam | Highest fidelity | Needs a second license; the previous sandboxed Steam login failed (BlockPorts template suspected, SPEC) |
| D: VM | — | — | Not recommended (DX11 under VBox unproven) |

**Recommendation**
- Build the components shared by every option first (3.3: C1-C3, C5-C8).
- Then implement **Option A as the primary harness**.
- Keep **Option B as a differential cross-check**: if a join fails under A, re-running under B separates shim bugs from mod bugs.
- Use **Option C, or two PCs, as the final real-Steam sign-off**.

The readers disagree on this choice; see §7, item 1.

### 3.3 Components

| ID | Component | Where / how | Notes |
|---|---|---|---|
| C1 | Lab gate config | `<exeDir>/mgs-coop/lab-instance.txt` (or `loopback.ini`): role, self_id (real\|fake), self_name, listen port, peer id/endpoint/name, mutex suffix, master URL, xinput routing, save dir | Ignored unless present and the exe code bytes match. Suggested ports 47810/47811, outside Steam's 27015-27050 and gbe's 47584+ |
| C2 | Folder parity | Guest folder = `D:/Games/steamapps/common/MGS_TPP_guest` with a COPY of `mgsvtpp.exe` instead of the hardlink (avoids on-disk patch bleed), 0.10.33+ loader, own `probe-install.json` with game_directory = guest path | Extend `set-session-mode.ps1` / `package-install.ps1` with `-GameDirectory`/`-GuestOf`, and check processes by exe path instead of `Get-Process mgsvtpp` |
| C3 | DllMain-minimal patches (guest) | IAT pointer writes only: `CreateMutexW` slot 0x208D170 appends a suffix when lpName exactly equals the title string, then restores the slot. If the identity cache runs before the DI8 install point, also wrap `SteamUser` 0x208E128 and `SteamFriends` 0x208E110 here | First verify `lea r8` at 0x13D6 resolves to 0x20A0980 and `cmp eax,0B7h` at 0x1415. Arxan risk: §7 item 10. Fallback: Sandboxie for the mutex (§7 item 11) |
| C4 | Loopback shim (Option A) | New native module replacing the networking branch at `steam_observer.cpp:260-269`. Wrap `SteamNetworking` 0x208E138 in both roles with a 22-slot vtable (slots 8-21 return failure). Wrap `RegisterCallback` 0x208E0F8 / `UnregisterCallback` 0x208E0F0 to track ids 1202/1203. After the original `RunCallbacks` (0x208E0E8), drain the synthetic queue, checking vptr 0x24BF400/0x24BF418, flags bit 0 and +0xC==id before calling slot 0. Guest overrides: `GetSteamID[2]` (hidden return pointer), `GetPersonaName[0]`, `GetFriendPersonaName[7]`, `RequestUserInformation[37]` returns false, swallow `ActivateGameOverlayToUser[29]` for loopback IDs, `GetAuthSessionTicket[13]` rewrites ticket bytes 12-19 to the fake ID (fuse only) | Steam semantics: Send implicitly accepts; the first inbound packet from a peer queues the packet and posts 1202 once; unaccepted packets are never visible; Read truncates; unreliable cap 1200 B; `GetP2PSessionState` returns active, IP 0x7F000001 and the peer port; Close sends CLOSE; remote loss or a 10 s timeout produces 1203 with error 4. Single lock (the poll runs on 2 threads). Callback registry shares that lock with the Unregister wrapper (destructor 0x1A5F5F0) |
| C5 | UDP transport | One Winsock socket on 127.0.0.1, `SIO_UDP_CONNRESET` off, 1 MB buffers; header `MGLB`, u16 version, type (DATA/HELLO/PING/CLOSE), mode, channel, src, dst, length; receive thread never calls game code; optional latency/loss/reorder knobs; bounded packet trace | Reject datagrams from unconfigured endpoints. Supports channels other than 0 for the mod's own channel |
| C6 | Master-server redirect plus fuse (both instances) | Retarget the disp32 at RVA 0x7D346F (the `lea rdx` at 0x7D346C) to a DLL-owned `http://127.0.0.1:6667/tppstm/gate`; this is tpp-mod's site. Run fuse f75b41d1aded as `fuse.exe -standalone -listenAddr 127.0.0.1:6667 -baseURL http://127.0.0.1:6667/ -writeLog` | NEVER run fuse without `-standalone`: it patches the exe on disk (expects the 1.0.15.3 md5, writes at the wrong offset). Cloning or building fuse needs network access and user consent. Worth keeping for real two-PC play to avoid Konami |
| C7 | Save isolation (both) | Redirect `GetUserDataFolder[6]` and RemoteStorage012 FileWrite/FileRead/FileDelete/FileExists/GetFileSize to `<dir>/mgs-coop/lab/saves/287700/{local,remote}`. Alternative or addition: a Dynamite-style save-name redirect (`TppDefine.lua:187-192`). Suppress SteamUserStats writes on the guest | Seed from COPIES of `userdata/97930372/287700/local/*` and `311340/remote/*`. The guest needs Mother Base unlocked, so give it a copy of the host's save |
| C8 | Automation hooks in the observer | Lab-gated `mgs-coop/join.trigger` treated as Ctrl+F8 (consumed on use; no foreground check) in `game_script_observer.cpp:426-452`; log `client_join=armed` when CLIENT_READY returns 'manual'; log `session_status=<value>` on change plus an atomic `status.json` (hook at `game_script_observer.cpp:283-289`); host writes its own SteamID64 to `mgs-coop/host-steamid.txt` (kept out of logs); strengthened `client-check-v1` logs IsConnectReady inputs (target+0x10/+0x14, byte 0x2BD7BC0, int 0x2BD7BB4) and connect-info flags (+0x00 state; +0x70/+0xB8 nonzero; +0xC8 non-null) | Refuse a client-v1 join outside lab mode when the host ID is a known lab placeholder |
| C9 | Window and input | Windowed 1280x720, set once per lab save through the in-game options (the `as` field in TPP_GRAPHICS_CONFIG may be an integrity tag, so do not hand-edit). XInput routing via IAT 0x208E070 (`xinput=0\|1\|none`) | Couch mode: host on keyboard/mouse, guest on a pad |
| C10 | Scripts `tools/lab/` | `lab.json`, `Prepare-LabInstance.ps1` (idempotent: hashes, loader, archive 26CE7C21, markers, lab config, save seed), `Start-Lab.ps1` (preflight on processes, ports 6667/47810/47811, hashes; start fuse; rotate logs; start each exe with `-WorkingDirectory`; place windows with `SetWindowPos`), `Invoke-LabSmoke.ps1`, `Get-LabScreens.ps1`, `Stop-Lab.ps1` | Tail logs with FileShare.ReadWrite from saved offsets. Title-screen automation via SendInput (scan codes, about 150 ms holds) is brittle; keep a manual-assist step |
| C11 | Transport self-test | Two-process CTest executable driving the shim through the same vtable surface (Send/Avail/Read/Accept/Close/SessionState plus 1202 dispatch), next to `tests/steam_packet_tests.cpp` | Runs without the game |

### 3.4 Ordered build list

1. Commit the 0.10.33 tree and fix the minhook gitlink (G1).
2. Guest parity: C2. Install the same loader into `MGS_TPP_guest`.
3. Lab gate plus automation: C1 and C8, including the strengthened diagnostic and `host-steamid.txt`.
4. Master-server redirect plus fuse (C6). **Then M0:** a host-only lab run still reaches `state=3 accept=1` under fuse. This re-verifies host creation without Konami.
5. Save isolation (C7) and window/input (C9).
6. Mutex rename (C3). Log the first return-address RVA seen by the SteamUser/SteamFriends getter wrappers: 0x4CE0D3 means the cache path, 0x1444 the startup check. This decides whether the identity proxies must move into DllMain.
7. Shim (C4, C5) and its self-test (C11).
8. Scripts (C10).
9. **M1:** the guest runs alongside the host with a fake identity and an isolated save, and reaches Mother Base.
10. **M2:** the trigger produces `request_started=1`, and the packet trace shows guest Send, host 1202, then Accept at 0x1A5F740.
11. **M3:** first-ever join, `client_established` plus `host_peer_present`.
12. **M4:** the remote actor is visible in screenshots, and the `Player.GetPlayerType{instanceIndex}` readback matches.
13. In parallel, optional: finish the gbe_fork build for the differential re-run (Option B).

### 3.5 Pass/fail rules (log-only)

1. **Host prerequisites:**
   - `DirectInput8Create_hresult=0x00000000`
   - `installed=1`
   - `mode=host_v3`
   - `dynamite_host=returned; result=1`
   - `host_admission=authorized`
   - `dynamite_session=observed; … state=3 accept=1 … consistent=1`
2. **Guest prerequisites:** `mode=client_v1; configured_host=1` and `client_join=armed`.
3. **Trigger:** the harness creates `join.trigger`. Expect `client_join=input_received; key=trigger_file`, then `dynamite_client=returned; request_started=1`.
4. **PASS:** guest `session_status=client_established` (state 7, E1, two present members, host ≠ local) AND host `session_status=host_peer_present`. Both must appear within 180 s and hold for 2 consecutive polls.
5. **FAIL:**
   - `dynamite_client=refused`
   - `request_started=0`
   - `session_status=session_error` (E4)
   - `refused=missing_or_invalid_client_host_txt`
   - a process exit
   - a timeout
6. **Output:** write `lab/runs/<ts>/result.json` with timings, PIDs, hashes, logs and screenshots.

### 3.6 Fallbacks and limits

- **Option B (gbe_fork):** per-folder `steam_settings/configs.user.ini` (account_name, account_steamid, `local_save_path`), `configs.main.ini` (listen_port 47584/47585), `custom_broadcasts.txt` containing 127.0.0.1, and `steam_interfaces.txt` copied from `MGS_TPP/mgs-coop/`. Still needs fuse, the mutex fix and a hash-gate exemption.
- **Option C:** a second licensed account in sandboxed Steam (MGSV_Guest), launching the guest exe inside the box. The box would need its network/port template reviewed. Family Sharing does not allow simultaneous play.
- **What a single-PC pass proves:** session and mod logic only. It says nothing about Steam NAT, relay, latency or loss. A real two-account run is mandatory before calling anything "working".

---

## 4. Characters

All of this is STATIC or design; none has been verified live.

**Enums (STATIC, registration at 0x9D45EA)**
- PlayerType: SNAKE 0, DD_MALE 1, DD_FEMALE 2, AVATAR 3, LIQUID 4 (broken), OCELOT 5, QUIET 6.
- PlayerPartsType: OCELOT 26, QUIET 27.
- PlayerCamoType: OCELOT 115, QUIET 116.
- vars: base pointer at 0x2C16920; playerType at +0xFB, parts +0xF8, camo +0xF9, hand +0xFA, faceId +0xFC.
- There is no per-player `playerType2` var. Per-instance appearance lives natively in Q = *(P+0x60), where P = *(base+0x2A82C30):
  - arrays: type +0x40, parts +0x48, camo +0x50, hand +0x58, faceId +0x60
  - avatar record of 0x4F bytes at +0xE8
  - dirty mask at +0x180

**Native replication.** A synchronizer apply function (0x12A5810, plus a variant at 0x12CC790) writes a remote player's type, parts, camo, face and hand into Q. So once a session's player sync runs, appearance may replicate with no mod code. Dynamite issue #109 (a remote Avatar renders as Snake outside FOB) suggests this path is partly FOB-gated (SPEC).

| Character | P1 | P2 | Approach | Honest limits |
|---|---|---|---|---|
| Snake | yes | yes | Vanilla sortie selection; nothing to build | Partner rendering unverified until M4 |
| DD soldier (DD_MALE/DD_FEMALE), "a Diamond Dog" | yes | **recommended default** | Vanilla sortie-prep staff selection on the guest's own (copied) save. Matches `docs/campaign-design.md:11` | `RegisterTemporaryPlayerType` does not carry faceId, so prefer sortie selection over config forcing. Replication unverified |
| Avatar | yes | yes | Local selection works | Partner may see Snake (#109) until the 13 preset bytes (Q+0x78) and the 79-byte record (Q+0xE8) are replicated; no Lua setter exists |
| Ocelot | possible | possible | (1) Native patch at **0x9CD370** in UpdatePartsStatus: `0F 85 9C 07 00 00` becomes `48 E9 9C 07 00 00` (unique in the file; old Dynamite site 0x1409CC6C0), session-scoped and contract-checked. (2) Skip `TppPlayer.ForceChangePlayerFromOcelot()` in TppMain.OnAllocate and in `TppMission.OnAbortMissionPreparation` while co-op is active. (3) Equip-block size changes (TppPlayer +100000; `DEFAULT_EQUIP_MISSION_BLOCK_GROUP_SIZE=1777721`). (4) Optional sortie-UI patch at SetupStaffList candidate **0x163EA0A** (one of 35 pattern hits; unconfirmed) | The dual-revolver patch (old 0x149CFBA54) is unmapped, so Ocelot gets one revolver. `GetPlayerType` maps 5 and 6 to 0, so readback cannot confirm Ocelot or Quiet |
| Quiet | possible | possible, with fallback | Same as Ocelot | FOB-designed kit; facial motion is loaded per type (mixed types may break remote facial animation); sound issues (lead: `SoundPlayerAnimEvent.SetupPlayerType` 0x986360); IH reports pause-menu loss and infinite load with the sortie-flag trick. Conflicts with story/buddy Quiet (episodes 11, 40, 45, buddy deployed): fall back to DD soldier there |
| D-Dog | no | **not feasible as a playable character** | No PlayerType, no player motion graph. D-Dog is `TppBuddyDog2` AI, and buddies are local-only per Dynamite (MAKING_OF:359) | Stretch goal only: "P2 commands a host-authoritative D-Dog" via `TppBuddy2BlockController` (RequestMoveToPosition 0xA48C80, RequestWalkToPosition 0xA49010, CallBuddy 0xA44560; argument signatures undecoded), plus dog-transform replication and a guest camera/input redirect. A new subsystem |

**Mechanism**
- A per-machine config value `character=` is exposed as `Tpp.MgsCoop.character`.
- In the existing TppMain transform, insert `TppPlayer.RegisterTemporaryPlayerType{…}` after `sequence.MissionPrepare()` and before `TppPlayer.MissionStartPlayerTypeSetting()`. This needs a new unique anchor.
- The host keeps story-forced types in 10240, 10151, 10010, 10280 and the Subsistence variants.

**Verification first**
- On both sides, log `Player.GetPlayerType{instanceIndex=i}` for i=0..1, plus the local index at P+0x214.
- Extend `tools/inspect_player_system.py` (read-only) to dump the Q arrays.

**Fallback if native sync does not deliver appearance**
- Send an appearance packet on the mod's own P2P channel: type, parts, camo, hand, faceId, faceEquipId, staff header and seed.
- Apply it with `Player.SetPartsInfoAtInstanceIndex` (0x9DF530; it does **no bounds check**, so clamp idx < P+0x208, which is 2).
- Only apply after the remote instance is realized (guard at 0x9CD160).
- Use one authority, native or mod, to avoid parts-reload thrash.

**Index and mirror-mode cautions**
- Derive the remote index from the local index (P+0x214), never hardcode 1. Commit 554cc4f's `realizePlayer2` used index 1, which is the guest's own local player, and was removed.
- Mirror mode (P+0x204 bits 0x2000|0x4000) copies the local appearance onto all instances in the ACC/heli.

---

## 5. Gap list to a playable 2-player mod (ordered)

Format per item: what · where · size · depends on · how to verify.

**Foundation**

- **G1. Commit the 0.10.33 tree.** All 30 modified and 43 untracked paths, including `native/include/mgscoop/{session_release,mission_termination,mission_initialization}.hpp` and `tests/{session_release,mission_termination}_tests.cpp`. Add `.gitmodules` for minhook or vendor it.
  - Size S · Depends: none.
  - Verify: a fresh clone builds and passes 21/21; the DLL matches FCB0E16A modulo timestamps (or add `/Brepro`).
- **G2. Wire the Lua/patch checks into CTest and fix the stale fixture.** Add `equipmentPreparationReturned=true` to `tests/host_admission_lua_check.lua:9`, plus a negative case.
  - Where: `CMakeLists.txt`, `tests/*_lua_check.lua`, `*_patch_check` tools (guard with `if(EXISTS)` for the extracted game inputs).
  - Size S · Depends: G1.
  - Verify: about 30 CTest suites green.
- **G3. Guest-instance install path.**
  - Where: `tools/package-install.ps1`, `tools/set-session-mode.ps1` (-GameDirectory/-GuestOf; per-path process check), guest `probe-install.json`; convert the guest exe hardlink into a copy.
  - Size S-M · Depends: G1.
  - Verify: both folders have DLL hash parity, and set-session-mode accepts the guest while the host is running.

**Harness (see §3)**

- **G4. Observability.** `client_join=armed`, `session_status=` lines, `status.json`, `host-steamid.txt`, and the strengthened `client-check-v1` (IsConnectReady inputs, connect-info flags).
  - Where: `game_script_observer.cpp:283-289,410-458`; `dynamite_session_port.cpp:255-289`; `steam_runtime.cpp`.
  - Size S · Depends: G1.
  - Verify: a host-only run logs `session_status=host_ready`.
- **G5. Lab gate plus `join.trigger`.**
  - Where: `game_script_observer.cpp:426-452`, new lab-config reader.
  - Size S · Depends: G4.
  - Verify: the trigger is consumed and `key=trigger_file` is logged.
- **G6. Master-server redirect plus local fuse.**
  - Where: new contract-checked patch at 0x7D346C/0x7D346F; `tools/lab/` fuse launcher.
  - Size M · Depends: G1 and user consent to fetch fuse.
  - Verify: fuse `-writeLog` shows CMD_GET_URLLIST and CMD_AUTH_STEAMTICKET; **M0** passes.
- **G7. Save isolation.**
  - Where: SteamUser[6] and RemoteStorage wrappers; optional TppDefine save-name splice.
  - Size M · Depends: G5.
  - Verify: hashes of the real `userdata/97930372/{287700/local,311340/remote}` are unchanged after a run.
- **G8. Mutex rename in DllMain.**
  - Where: `probe.cpp` DllMain (currently `probe.cpp:237-240`), IAT 0x208D170.
  - Size S-M · Depends: G5.
  - Verify: the second instance reaches the title screen.
- **G9. Loopback shim plus two-process self-test** (C4, C5, C11).
  - Where: new `native/steam_loopback.*`, `tests/`.
  - Size L · Depends: G5, G7, G8.
  - Verify: self-test green, then **M1/M2**.
- **G10. Lab scripts.**
  - Where: `tools/lab/*.ps1`.
  - Size M · Depends: G3-G9.
  - Verify: `result.json` reaches PASS (**M3**).

**Join robustness**

- **G11. Reset and re-arm.**
  - On E4, state 8, `Nt.SessionDisconnectFromHost` or a 30-60 s pending timeout: close the client-owned session, wait for root+0xCC8 and s_pSession to empty, clear `attempted` and `client_join_consumed`, re-arm on a key such as Ctrl+F9.
  - Switch the client to Dynamite's fresh-object model: a zeroed FobTarget (0x80 bytes) plus SessionConnectInfo (at least 0xD8 bytes, +0xB8 = host, +0xC8 non-null), allocated from heap 0xDBAB0 and kept alive until close.
  - Add `TppNetworkUtil.SetTimeOut(10)`.
  - Where: `host_session_trial.cpp:24,160-194`; `game_script_observer.cpp:40,439`; `dynamite_session_port.cpp:402-457`; `client_join.lua`.
  - Size M · Depends: G4.
  - Verify: a forced failure followed by a successful second attempt without restarting.
- **G12. Parity fallbacks behind marker options.**
  - (a) An IsConnectReady bypass scoped to the 0x7EE380 call.
  - (b) Map SetSteamId and `SteamUdpSocketImpl::OnP2PSessionRequest` in 1.0.15.4, starting from constructor 0x1A5F470, the 1202 handler 0x1A5FA10 and the accept call sites. Use them for host logging, a whitelist (only `client-host.txt`'s peer), and an ID-override fallback.
  - Size M-L · Depends: G4 diagnostics.
  - Verify: enable only if the M2/M3 logs show refusals.
- **G13. Pre-mission (ACC) join path, Dynamite order.**
  - Guest connects from 40010/40020/40050 while the host is in-mission with accept=1, then loads the same mission.
  - Requires: HostPrerequisites accepting ACC codes, the allocation bridge active for out-of-mission allocations, the owned client session surviving the load, and player patches applied when an owned client session already exists.
  - Size L · Depends: G11.
  - Verify: client_established before load; after load both actors are present (Dynamite's success sign is the intro camera orbiting the host).

**In-scene playability (30050 smoke scene)**

- **G14. Remote actor visibility.**
  - Index from P+0x214; `SetWorldCenter` experiment for 30050 (coordinate from a logged host position; SPEC); WarpToPartner as pure Lua via `GameObject.SendCommand({type='TppPlayer2',index=local},{id='WarpAndWaitBlock',…})` with the position from the player bank at image+0x2BD3FF0.
  - Size M · Depends: M3.
  - Verify: M4 screenshots plus `GetPlayerType` readback.
- **G15. Make damage field-13 clearing opt-in** (default to Dynamite's effective no-op).
  - Where: `lua/coop/damage_parameters.lua:12-24`.
  - Size S · Depends: none.
  - Verify: both players can damage enemies normally.
- **G16. Character baseline** (DD-soldier P2; appearance verification tooling).
  - Size S-M · Depends: M4.
  - Verify: the remote slot shows type 1 or 2.

**First real combat mission (10033 "Over the Fence")**

- **G17. Mission allowlist.** Replace the 11 Lua `==30050` sites and the native sites with one shared set, seeded {30050, 10033}. Re-verify the CloseSession route for 10xxx (wrapper 0xA1A710, branch 0xA1A759/0xA1A75A; only the 30050 path +0x60 → 0x312B80 is mapped).
  - Size M · Depends: M3.
  - Verify: fixtures plus receipts for 10033.
- **G18. Lua delivery restructure.** MSVC caps a string literal at 16,380 B (C2026). Move large modules into the archive (`Script.LoadLibrary`/`dofile` from `archive_bootstrap.lua`, proven in trial 6). Add a dev-only loose-file override. A native `MgsCoop` library via the engine binder (0x17A0A0, 0x1A158E0, 0x1A15690, 0x1A15760) comes later.
  - Size M · Depends: G1.
  - Verify: builds; modules load live.
- **G19. Archive-precedence experiment** (same-path override vs renamed copies), then a mission-asset generator driven by Dynamite's `moddata.go` table, generated on the user's machine (no game bytes shipped).
  - Size S, then M · Depends: G18.
  - Verify: the engine loads the archive copy, or the renamed-pair route is confirmed.
- **G20. 10033 port.**
  - Area FPKD minus `single_player2_instance.fox2`/`single_pl2_instance_scarf.fox2`, plus pack-list substitution.
  - Sequence splice at `Seq_Game_MainGame.OnEnter`: `SetWorldCenter(-397.83188,344.12,556.29047)`, `SessionEnableAccept(true)`, start targeting.
  - Session close on clear/abort/finalize/terminate (Dynamite `TppMission.lua:715-738,1089-1097,1285-1292`).
  - TppRevenge `RegisterMissionMineList` early return under S_IS_ONLINE.
  - TppPlayer Warp/Cbox local-index fixes.
  - Equip block sizes.
  - Size L · Depends: G13, G17-G19, G7.
  - Verify: full mission clear by one exfiltration; both players visible; enemies react to both; respawn; close receipts; real saves untouched.
- **G21. Extend result suppression to all co-op missions** (0x7F31F0; callers 0x88816E and 0x160154E; scope arming).
  - Size S-M · Depends: G17.
  - Verify: fuse log shows no co-op result uploads.
- **G22. Partner marker permanence** (map SightManagerImplSetMarker, old 0x149C687D0; objectID ≤ 15 gets duration 9999999), then **marker/sight-mark sync on the mod's own P2P channel**. Do not hook the game socket. Queue handlers onto the mission thread.
  - Size M, then L · Depends: M3.
  - Verify: the partner mark persists beyond about 15 s; markers mirror.
- **G23. Quiet/Ocelot** (0x9CD370 contract, ForceChangePlayerFromOcelot skip, optional 0x163EA0A) with a per-mission fallback policy.
  - Size M · Depends: G16.
  - Verify: local type persists; remote renders as Quiet/Ocelot.
- **G24. Real two-account validation** (two PCs, or Sandboxie plus a second license).
  - Size external · Depends: G20.
  - Verify: the same pass rules over the internet.

---

## 6. Campaign roadmap

**Definition the user must sign off on**
- Two players, not the 4-player rules in `docs/campaign-design.md`. Rewrite `data/campaign-support.json` to match.
- The host's isolated co-op save is the progression authority. The guest plays on a copy of it, refreshed each session and never written back.
- Same-mission join, with the host waiting at the landing zone.
- The mission completes when either player exfiltrates.
- Death means a 10 s respawn.
- Prologue (10010) and Truth (10280) are host-solo.
- Cutscenes play locally; do not skip them (ep43 crash, #16).
- No pause-menu restart in co-op.
- Total Stealth variants deferred.

**Stages**

| Stage | Content | Exit criteria |
|---|---|---|
| S0 | First guest join; 30050 as a connection/visibility smoke scene only (it has no hostile AI) | M3 plus M4 on single-PC, then the ACC-join variant (G13) |
| S1 | 10033 end-to-end (G17-G21) | Join, mutual visibility, AI reacts to both, respawn, exfiltration, close, results suppressed, saves isolated |
| S2 | Class A: 10036, 10043, 10041, 10054, 10052, 10091, 10085, 10200, 10211, 10081, 10045, 10082 (about 13 including 10033; 2-4 sequences, no Reload) | Per-mission evidence: join, world-center receipt, respawn, clear-close, result suppression, save hashes |
| S3 | Class B fixes: 10040 (either-player Honey Bee check via `GetEquipIDInSlot`), 10044 (host-only tank #19), 10086 (interrogation #83), 10100, 10195, 10121 (walker gear #116/#21), 10156 (canister), 10093, 10171, 10110, 10120; then story transitions 10020 (pre-movie close), 10070 (4 Reloads, re-arm accept in OnReload), 10080 (late-connect abort #3), 10240 (forced Snake), 10260 | Mission-specific adapters; accept re-armed after every `TppMission.Reload` |
| S4 | Boss sync: BossQuiet message family for 10050/11050 and the Skulls missions (10020/10040/10090/10130/10140). Boss Quiet patch candidates 0x17CC24A, 0x17CC192 and 0x17D4A17 (ProcessDamages can reuse the combat_role detour via return 0x17D4A12). Sahelanthropus 10151/11151, Eli and Man on Fire are labelled "parallel local boss" | Shared boss state across both clients |
| S5 | Variants (Subsistence 11043/11044, Extreme 11054/11090/11050/11140/11130/11151), then bespoke MB missions 10030 and 10115 | — |
| S6 | Free roam 30010/30020 and the 167 side ops as a separate project (quest-block consistency, animals, fast travel, starting a mission from free roam). Total Stealth only with a host-authoritative detection rule | — |

**Class C (not faithfully co-op):** 10010, 10280, the shared Sahelanthropus fight, and Total Stealth under local perception.

**Status priors:** Dynamite's per-mission statuses are community reports on exe 1.0.15.3. Treat them as priors, not evidence. Mark a mission "verified" only after a two-PC or two-account full run.

---

## 7. Contradictions and uncertainties between readers

1. **Primary single-PC harness.**
   - steam-transport: in-mod shim primary, gbe fallback.
   - single-pc: gbe primary, shim fallback, rating the shim "High" effort.
   - session-join: Sandboxie plus a second Steam account.
   - dynamite-upstream: second account highest fidelity; shim speculative.
   - **Synthesis decision:** shared components first, shim primary, gbe as a differential cross-check, real accounts for sign-off. Rationale: hash-gate compatibility, live-proven IAT infrastructure, a small statically mapped surface, and the gbe build has stalled.
2. **Shim effort:** "feasible and small" (steam-transport) vs "High" (single-pc). Unresolved. The game-side surface is small (6 networking slots, 2 callbacks, about 4 identity slots), but exact Steam semantics and threading are where bugs will be.
3. **`client-host.txt` contents.** steam-transport says it already holds the host's real ID. Wrong: it holds the placeholder `76561198000000001` [synth-checked]. That is a valid individual-format ID that may belong to a real person, so a real-Steam join would target a stranger.
4. **Installed slot count.** The characters reader describes a four-slot build (count 4, clamp 0..3). The installed archive is TWO-slot (26CE7C21):
   - `host_session_trial.cpp:122` requires limit==2 [synth-checked];
   - `set-session-mode.ps1:20` says "Expected two-player archive" [synth-checked].

   The four-slot archive 74bfcd36… is staged but not gated. Clamp to 0..1.
5. **URL string file offset.** dynamite-upstream gives 0x2217D08; single-pc gives 0x2217B08. Decimal 35748616 = 0x2217B08 [synth-checked arithmetic: RVA 0x2218708 − 0x208D000 + raw 0x208C400]. Irrelevant for in-memory patching; use the RVA.
6. **In-place URL capacity:** 48 chars + NUL (dynamite-upstream, session-join) vs 62 counting 14 trailing zero bytes (single-pc). The 33-char local URL fits either way. Prefer retargeting the `lea` to a DLL-owned buffer.
7. **Dynamite finishable count.** dynamite-upstream's summary says 28, but its own list and the campaign reader total 27. Resolution: 27 finishable plus 8 '?' = 35 sequence-edited missions; the 36th IsCoop code (10280) has no edits and no pack.
8. **DamageParameterTables.** dynamite-upstream lists "removes the ignore-defender flag" as a Dynamite feature. gameplay-patches diffed all 475 rows: the data equals vanilla and the loop iterates the outer table, so it is a no-op. Trust the diff; Dynamite never actually cleared field 13.
9. **Player root naming.** session-join uses `*(base+0x2A82C30)+0x208`; characters uses `P=*(base+0x2A82C28+8)` with `P+0x208 = enclosing+0x228`. Same object [synth-checked `host_session_trial.cpp:119-122`]. Repo docs' "enclosing" is P−0x20; use P-relative offsets.
10. **DllMain/Arxan risk.** steam-transport calls pre-entry patching unproven; single-pc calls it precedented (Dynamite patches from DllMain, but on 1.0.15.3). Treat as low-to-medium risk and verify at M1. Keep the patches to IAT pointer writes.
11. **Sandboxie as the mutex fallback.** single-pc proposes it. steam-transport notes Sandboxie blocks Steam IPC unless explicitly opened. Under the shim the guest still calls real `SteamAPI_Init`, so it needs IPC to a Steam client. My inference: Sandboxie-for-mutex pairs cleanly only with gbe, or with a sandboxed Steam.
12. **Sandboxie box config:** "Template=BlockPorts, BlockNetworkFiles=y" (session-join, steam-transport) vs a default template list without BlockPorts (single-pc). The cause of the earlier `LogonFailure No Connection` is unknown (BlockPorts suspected, SPEC).
13. **Guest links:** junctions (single-pc) vs symlinks (build-health, steam-transport). Irrelevant to set-session-mode, which checks only the game dir, `mgs-coop` dir, marker and `client-host.txt` for reparse points [synth-checked lines 7-15].
14. **Same-account `SteamAPI_Init` in a second process:** expected to work, untested (shim prerequisite). The identity-cache (0x4CE0A0) timing is unknown and decides whether the identity proxies must go into DllMain.
15. **Mid-mission join viability:** every reader flags it as risky; there is no evidence either way.
16. **Master-server dependency.**
    - Host creation is LIVE while logged into Konami.
    - Whether fuse login works on 1.0.15.4 is unverified.
    - Whether IsConnectReady's global (0x2BD7BB4) is login state is inference.
    - Switching to fuse could break host creation, hence M0.
17. **Native appearance replication outside FOB:** a synchronizer exists (STATIC), but #109 implies partial FOB gating. Unknown until M4.
18. **Save portability across SteamIDs.** Unknown per single-pc. Dynamite's cipher is keyed by file name (`savebackup.go:180-213`), which suggests saves are not SteamID-bound, but that is unverified.
19. **SetWorldCenter necessity.** dynamite-upstream cites upstream issue #59 (missing → invisible players). lua-archive and campaign call it inference. It is upstream-reported, not verified on 1.0.15.4, and no 30050 coordinate exists.
20. **Unvalidated candidate RVAs:** SetupStaffList 0x163EA0A (1 of 35 hits), UpdatePartsStatus 0x9CD370 (unique), emblem 0x9CA6B3, Boss Quiet 0x17CC24A, 0x17CC192 and 0x17D4A17, resolver 0x7DEBF6. Each needs a full-body contract before use.
21. **Archive same-path override precedence** is unresolved (`inventory.json archive_precedence_resolved=false`).
22. **`00.dat` md5 b28bedbc…** is assumed to be stock 1.0.15.4 (mtime evidence only). Dynamite's installer would refuse it because it expects 2779dcc7….
23. **30050 Lua gate count:** "10 sites" (campaign summary) vs the 11 sites it actually lists. Use 11.
24. **Licensing:** Dynamite and fuse are GPLv3 plus Commons Clause. The runtime archive contains game-derived bytes, so it must be generated per machine and never shipped in dist ZIPs (`tools/package.ps1` currently bundles it).

---

### Appendix: key 1.0.15.4 addresses

**Session**

| Item | Address / layout |
|---|---|
| CreateHostSession | 0x7EE590 |
| CreateClientSession | 0x7EE380 |
| IsConnectReady | thunk 0x7E4190, body 0x681CB90 |
| GetFobTarget | 0x7D6470 |
| Start | 0x315140 |
| Open/Connect | 0x3144D0 |
| Close | 0x312B80 |
| CloseSession wrapper | 0xA1A710 |
| s_pSession | base+0x2B9DFB0 |
| Session root | base+0x2BEF840 (+0xCC8 session, +0xD00 manager) |
| ServerManager | base+0x2BEFC00 |
| FobTarget layout | +0x10 state, +0x14 threshold, +0x40 ConnectInfo*, +0x48 flag |
| ConnectInfo layout | +0x00 state, +0xB8 host SteamID64 |
| Session flags | state +0x90; flags E1/E2/E4 at +0xE1/+0xE2/+0xE4 |

**Steam IAT slots and transport**

| Item | Address |
|---|---|
| SteamNetworking | 0x208E138 |
| SteamUser | 0x208E128 |
| SteamFriends | 0x208E110 |
| SteamRemoteStorage | 0x208E118 |
| RunCallbacks | 0x208E0E8 |
| Register / UnregisterCallback | 0x208E0F8 / 0x208E0F0 |
| CreateMutexW | 0x208D170 |
| XInputGetState | 0x208E070 |
| SteamUdpSocketImpl ctor | 0x1A5F470 |
| Send / Recv / Poll | 0x1A5FB60 / 0x1A5FAC0 / 0x1A5FBF0 |
| CCallback vptrs (1202 / 1203) | 0x24BF400 / 0x24BF418 |
| Packet-manager accept | 0x1CEE420 |

**Process, Lua and assets**

| Item | Address |
|---|---|
| Mutex call / name string | 0x13F9 / 0x20A0980 |
| Master URL string / lea | 0x2218708 / lea at 0x7D346C (disp32 at 0x7D346F) |
| luaL_loadbuffer | 0x1A178E0 |
| CreateWithPackFile | 0x48E20 |
| vars pointer | 0x2C16920 |
| Mission code (vars+0x12) | RVA 0x2C01372 |
| Player system | P = *(base+0x2A82C30): +0x204 flags, +0x208 count, +0x214 local index, +0x60 parts-info Q |
| UpdatePartsStatus | 0x9CD030 (Quiet/Ocelot reset jne at 0x9CD370) |
| SetPartsInfoAtInstanceIndex | 0x9DF530 |
| Synchronizer apply | 0x12A5810 |