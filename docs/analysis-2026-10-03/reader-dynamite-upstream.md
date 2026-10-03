# Upstream Dynamite inventory (unknown321/dynamite @ c90f252)

Source: `C:/Users/taylo/OneDrive/Documents/ChatGPT/mgs-coop/work/upstream-dynamite`. It is a shallow clone with a single commit, "calc distance fix" (2026-07-31). Upstream targets exe **1.0.15.3**. Unless marked VERIFIED, every address below is a 1.0.15.3 VA.

Evidence levels used in this report:
- **UPSTREAM**: code shipped and released by upstream; I did not run it.
- **VERIFIED**: I checked it statically, read-only, on the installed 1.0.15.4 files.
- **SPECULATION**: marked inline where it appears.

## 1. Architecture

| Layer | Files | Role |
|---|---|---|
| Go installer and launcher | `main.go`, `cli/cli.go`, `installer/installer.go`, `config/config.go`, `savebackup/`, `masterserver/`, `webui/`, `updatecheck/` | Web UI config on 127.0.0.1:8070; QAR repack of `master/0/00.dat`; save isolation; writes `dinput8.dll` and `lua/*.lua`; runs embedded fuse on 127.0.0.1:6667 |
| Native hook | `hook/src/*` (MinHook, spdlog, mINI and flatbuffers via FetchContent; mingw GCC 15, C++23; `hook/Makefile` docker build; output `hook/cmake-build-release-docker-mingw15/dinput8.dll`, embedded through `hook/embed.go`) | dinput8 proxy, about 45 detours, 27 byte patches, master-URL patch, `Dynamite` Lua library, custom sync protocol |
| Game data | `moddata/` (embedded through `moddata/moddata.go`) | Replaced system Lua (`standalone/*.lua`), 35 mission `*_sequence.lua`, coop_essentials fpk/fpkd, lng2 strings |
| Legacy | `patch/patch.go`, `patch/caves.txt` | Old file-based exe patcher. The installer never calls it, and its `UpdateURL` and `PatchAll` calls are commented out. It documents dead and unused patch attempts. |

## 2. Feature list (UPSTREAM)

1. **2-player session reuse of the FOB netcode.**
   - Host: `tpp::net::FobTarget::CreateHostSession`.
   - Client: `FobTarget::CreateClientSession`, with the target SteamID injected through the `SetSteamId` hook. Transport is the game's Steam P2P `SteamUdpSocketImpl`.
2. **Connection filtering**: whitelist and blacklist (`DynamiteHook.cpp:85-128`).
3. **Enemy targeting**: a host-only thread recomputes every 500 ms which player is nearest to any live soldier (`DynamiteCore::GetNearestPlayer`, `StartNearestEnemyThread`). FOB team hooks force both players to count as the "offense" team.
4. **Damage fixes**: patches 0x140ffa9ff and 0x141378209, plus the ignore-defender flag removed in `DamageParameterTables.lua`.
5. **Markers**:
   - The partner gets a permanent marker with the D-Dog icon.
   - User markers (fixed and follow) and enemy sight markers are shared.
   - Markers are ignored during load to avoid a checkpoint-restore hang.
6. **Emblem sync**: SendEmblem plus the "always apply opponent emblem" patch.
7. **Boss Quiet sync** for mission 11 (host-authoritative commands and damage).
8. **Respawn instead of game over**: 0x1417a200e plus the commented-out `TppMission.OnPlayerDead`.
9. **No uploads of sneak results** on game over or the FOB result screen.
10. **Playable Quiet and Ocelot in sortie** (0x14163fcda, 0x1409cc6c0), plus the Ocelot dual-revolver fix (0x149cfba54 and Lua equip block sizes).
11. **Walker gear crash fixes, item no-use removal, climb-check override, trap crash fixes, host-path forcing** for hostages, the damage controller, the sight controller and cp member status.
12. **WarpToPartner** keybind.
13. **Keybind script loader** with hot reload, and custom Lua modules from `<game>/lua`.
14. **Save isolation** under `DYNAMITE_*` names, with a zip backup.
15. **Bundled local master server** (fuse) with stub resources.
16. **Extras**:
    - The logo and autosave prompt are skipped (`init_sequence.lua`).
    - The FOB timer is disabled (`o50050_sequence.lua`).
    - In-game strings come in 8 languages.
    - A donation popup appears every 3 missions.

## 3. How a session starts

### Host
1. Set `[Coop] Host=true`.
2. Start a coop mission normally.
3. `TppMain.OnAllocate` (`standalone/TppMain.lua:147-185`) runs:
   - `TppNetworkUtil.SetTimeOut(10)`
   - `Dynamite.CreateHostSession()`, which uses the FobTarget captured by the `FobTargetCtor` hook
   - `TppGameStatus.Set("Mission","S_IS_ONLINE")`
4. The `BlockHeapAlloc` hook moves the network allocations (tag 0x73) onto the heap. This fixes issue 7 (`docs/issue_7.md`).
5. The first gameplay sequence's `OnEnter`, guarded by S_IS_ONLINE, runs (example: `s10033_sequence.lua:1272-1278`):
   - `TppNetworkUtil.SessionEnableAccept(true)`
   - `Dynamite.StartNearestEnemyThread()`
   - `TppCoder.SetWorldCenter(Vector3(..))`
6. The log shows "Created host session". The host tells the partner to connect and waits at the landing zone.

### Client
1. Set `[Coop] Host=false` and HostSteamID to the host's SteamID64.
2. Wait until the host has loaded the mission.
3. Hold **V** (Master Key) for at least 1 s and press **UP**. This runs `Dynamite.CreateClientSession`, which:
   - allocates FobTarget and SessionConnectInfo (targetIPCString points at the char 3)
   - calls `CreateClientSession`
4. `SetSteamIdHook` injects the host SteamID, and `IsConnectReady` is forced true.
5. On `UpdateClientEstablished` the game shows "Co-op connection established". The hook also initialises sync, sends a Ping and sends the emblem.
6. The client then starts the **same** mission. The sign of success is a "broken" intro camera that orbits the host.
7. If the client sees "You have been disconnected", it presses V+LEFT (reset) and retries.
8. This must be repeated for **every** mission.

### Teardown
Abort, clear, finalize and terminate all run the same sequence (`TppMission.lua:714-737, 1089-1097, 1285-1293`; `TppMain.lua:938-946`):
- `StopNearestEnemyThread`
- reset S_IS_ONLINE
- `SessionDisconnectPreparingMembers`
- `CloseSession`
- `ResetClientSessionState`

## 4. Lua API (`DynamiteLua.cpp CreateLibs`; global `_Dynamite=1`)

| Function | What it does |
|---|---|
| `CreateHostSession` | Host only; announces `dynamite_created_host_session` |
| `CreateClientSession` | Client only; announces `dynamite_establishing_connection` or `dynamite_connection_failed` |
| `ResetClientSessionState` | Resets state and stops sync |
| `ResetClientSessionStateWithNotification` | Same as above, with an announcement |
| `StartNearestEnemyThread`, `StopNearestEnemyThread`, `IsNearestEnemyThreadRunning` | Control the host targeting thread |
| `GetOffensePlayerIndex`, `GetDefensePlayerIndex` | Current offense and defense indices |
| `GetSoldierLifeStatus(id)` | Life state of a soldier |
| `AddFixedMarker(x,y,z)`, `AddObjectMarker(x,y,z,id)`, `RemoveMarker(i)`, `RemoveAllUserMarkers` | Marker control |
| `IgnoreMarkerRequests`, `AcceptMarkerRequests` | Marker gating during load |
| `IsHost`, `IsClient` | Config role |
| `GetPlayerPosition(i)` | Returns a Vector3 |
| `WarpToPartner` | Warps to the partner |
| `IsSynchronized`, `RequestVar(cat,name)`, `Ping` | Sync helpers |
| `Log(str)` | Writes to the spdlog file |
| `Donated`, `MissionComplete`, `GetMissionsCompleted` | Donation popup and mission counter |
| `GetEquipment` | Bound to `l_GetActiveEquipmentID`; returns nothing |
| `GetEquipIDInSlot(p,slot,i)` | Equipment ID in a slot |

Callers in the shipped Lua:
- `IsCoop`, `IsHost`, `CreateHostSession`, `ResetClientSessionState`, `Stop/StartNearestEnemyThread`, `Ignore/AcceptMarkerRequests`, `MissionComplete`, `Donated`, `GetMissionsCompleted`, `Log`
- `IsClient` in `TppPlayer.WarpByCboxDelivery`
- `GetEquipIDInSlot` in s10040 (honey bee check)

## 5. Sync protocol (DynamiteSyncImpl)
- Wire format: an 8-byte header "DYNAMITE" followed by a FlatBuffer `MessageWrapper{packet_num, Message}`, max 1024 bytes.
- Sending: through the game's own `SteamUdpSocketImpl::Send`, reusing the socket, info and address captured from the last game send.
- Receiving: the `SteamUdpSocketImpl::Recv` hook filters by header and dispatches.
- Ordering: stale packets are dropped. There is no retransmit.
- The `GameSocket` path is deprecated.

## 6. Master server (fuse)

### Why Dynamite bundles it
- The FOB session machinery needs a logged-in online state.
- Mission results and resources are uploaded after missions. Co-op data is abnormal, and upstream warns it "may result in online ban".

### How it is bundled
- `masterserver.Run()` calls `server.Start("http://127.0.0.1:6667/","127.0.0.1:6667","tppstm",false,false,"./dynamite/dynamite.dat",bonus)` when `UseLocalMasterServer=true`.
- The hook patches the exe URL string at 0x142218770 to `cfg.MasterServerURL` (`dynamitePatches.cpp:247-277`).

### Local availability (VERIFIED)
- fuse is not in `work/go-mod-cache`, not in `C:/Users/taylo/go/pkg/mod` and not in `third_party/`.

### Pinned version
- `f75b41d1aded4d2e0684af2f03fab064186b225b` (2025-08-01), pseudo-version `v0.0.0-20250801084223-f75b41d1aded`.
- License: GPLv3 + Commons Clause.
- Dependencies: pure Go (modernc sqlite).

### Build
```
git clone https://github.com/unknown321/fuse
cd fuse
git checkout f75b41d1aded
go build -trimpath -o fuse.exe .
```
Local Go 1.26.3 satisfies go 1.24.5.

### Run
```
fuse.exe -standalone [-writeLog] [-listenAddr 127.0.0.1:6667] [-baseURL http://127.0.0.1:6667/]
```
- **Use `-standalone` on 1.0.15.4.** Without it, fuse backs up and patches the exe, checking for the 1.0.15.3 md5 and writing at file offset 35746160.
- The database is `fuse.dat` in the cwd.
- There is no authentication.

### Redirecting 1.0.15.4 (VERIFIED)
- The URL string is at VA **0x142218708** (file offset 0x2217D08, 48 chars + NUL).
- It is referenced by the LEA at **0x1407D346C** (`48 8D 15 95 52 A4 01`).
- The exe has no ASLR (DllCharacteristics 0x8120).

## 7. Saves (UPSTREAM)
- **Backup**: `<steam>/userdata/<acc>/287700.dynamite.<ts>.zip` and `311340.dynamite.<ts>.zip`.
- **Conversion** in `311340/remote`, re-encrypted with squib:
  - TPP_GAME_DATA → DYNAMITE_GAME_DATA
  - TPP_CONFIG_DATA → DYNAMITE_CONFIG_DATA
  - PERSONAL_DATA → DYNAMITE_PERSONAL_DATA
- **Local copies**: `DYNAMITE_*0` and `DYNAMITE_*1` in `287700/local`.
- **Game side**: `TppDefine.lua:187-192` renames the save files, so mod saves never touch real saves.
- **Restore procedure** (docs): disable cloud sync for TPP and GZ, delete the `DYNAMITE_*` files, reinstall.

## 8. Respawn, known broken, mission list

### Respawn and mission rules
- A player respawns about 10 s after death.
- At least one player must exfiltrate to finish the mission.
- Fultoning a player fails the mission.
- Restarting from the pause menu loads forever.
- The host may wait up to 30 s for the results screen.

### Known broken (docs and open issues)
- Crashes on triggers.
- Invisible players: SetWorldCenter missing in a loaded sequence (#59).
- Hostages and enemies not in sync (positions #112, fovas #113).
- Vehicles and D-Horse desync.
- Buddies are local.
- Walker gear invisible or not fultonable.
- Interrogation is local.
- Objectives and markers are not shared.
- Host aborts out-of-area if the client connects late (#3).
- FOB items do not work (#4).
- Packet loss (#119).
- Avatar shown as Snake (#109).
- Missions 1 and 2 must be cleared solo first (#85).
- The June 2026 game update broke Dynamite (#136). Upstream suggests downgrading to depot 287701 build 6239679 manifest 2518831260221836800.

### Mission list
The full table is in `key_facts`: 28 finishable, 8 marked '?', and the prologue, 2, 22, 46 and all variants disabled.

## 9. Player character types
- Both player instances load as `PLTypeNormal` (`two_player2_instances.fox2.xml`).
- Each player chooses their character in their own sortie: Snake, avatar, DD staff, or Quiet or Ocelot via patch 0x14163fcda. The 0x1409cc6c0 patch stops the game resetting them to Snake.
- The partner sees that choice through FOB player sync. In regular missions a custom avatar is drawn as Snake (#109).
- The partner's map marker and icon show as **D-Dog**.
- Buddy AI is local. Playing as D-Dog is not supported.

## 10. Gaps against the user's 1.0.15.4 port
- The master-server URL is not redirected.
- fuse is not vendored.
- 00.dat md5 mismatch: b28bedbc… installed vs 2779dcc7… expected. The Lua must be rebased onto 1.0.15.4.
- Not yet ported: coop_essentials and the per-mission instance deletion, SessionEnableAccept and SetWorldCenter inserts, and the client hook chain.
- One-PC testing collides with Steam P2P identity, because both instances on one account share a SteamID.
- Upstream's repack of 00.dat (remove then create) would break the hardlink to `MGS_TPP_guest`.