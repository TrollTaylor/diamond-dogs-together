# Single-PC host+guest test harness: analysis

## 1. What was set up before

Labels: **[F]** = I read the file, **[S]** = static disassembly of mgsvtpp.exe 1.0.15.4, **[D]** = project docs, **[C]** = community or upstream docs. Nothing was launched.

| Item | State | Usable? |
|---|---|---|
| `D:/Games/steamapps/common/MGS_TPP_guest` | Real folder. `mgsvtpp.exe` is a **hardlink** to the main exe (fsutil lists both paths; hash 085C2F82). `steam_appid.txt` and `version_info.txt` are hardlinks. `master`, `mgo` and `CustomSoundtrack` are **junctions**. `steam_api64.dll` is a plain copy of the **shipped Valve DLL** (08BD0152), not Goldberg. `dinput8.dll` is **0.10.27** (6B484154). [F] | Partly. The loader is old, the manifest points at `MGS_TPP`, and it was never launched (no probe.log). |
| `MGS_TPP_guest/mgs-coop` | `script-observe.enabled`=`client-v1`. `client-host.txt`=`76561198000000001`, a placeholder. Runtime .dat hash is correct (26CE7C21). `probe-install.json` says game_directory=`MGS_TPP`, so `tools/set-session-mode.ps1:17` would refuse this folder. [F] | Needs a rebuild. |
| `MGS_TPP/saves_host/settings` | Legacy Goldberg files: account_name `Noob`, user_steam_id `76561199004369964`, listen_port `47584`, language `english`. No Goldberg DLL or `local_save.txt` exists, so these files are inert. The ID also disagrees with the guest's target ID. [F] | Not wired up. |
| `MGS_TPP/mgs-coop/steam_interfaces.txt`, `test_steam_*.obj` | Goldberg `generate_interfaces` output: SteamClient017, SteamUser018, SteamNetworking005, RemoteStorage012, and so on. [F] | Reusable as gbe `steam_settings/steam_interfaces.txt`. |
| `build/gbe_fork` (git-ignored) | gbe_fork checkout at commit 327815e. The deps build started 2026-09-11 and produced no .lib or DLL. [F] | Needs to be finished. |
| Sandboxie `MGSV_Guest` | Default template only. The sandboxed Steam ran on 09-11 at 10:04 and 13:34, logged in **only the main account** (76561198058196100), and ended with `LogonFailure No Connection`. MGSV was never launched in the box. [F] | Works only with a second licensed account. |
| Git 8542970 "Sandboxie, Goldberg, Local Dual-Actor testbeds" | Contains **no** testbed scripts. "Dual-Actor" refers to the Diagnostic role (`client-check-v1`, set-session-mode.ps1:34-40). No launcher exists in `tools/*.ps1`. [F] | — |
| `docs/single-pc-testing.md` | Inventory only. It recommends a VM or second account and says no fake player or multi-instance mode was implemented. [D] | — |

## 2. How two instances collide on one PC

1. **Single-instance mutex [S].** At RVA `0x13F9` the exe calls `CreateMutexW(sa, FALSE, L"METAL GEAR SOLID V: THE PHANTOM PAIN")`. The name string is at RVA `0x20A0980` and has no Global\ prefix, so it lives in the session-local namespace. Next it checks `GetLastError()==0xB7` at `0x1415` and, if so, jumps to the exit path at `0x1595`. All of this happens before `SteamAPI_Init` (`0x1428`) and before `DirectInput8Create`, which is where the mod currently installs its hooks. So the second instance quits.
   - Fix options: a DllMain IAT hook on slot `0x208D170`, a Sandboxie box, or closing the host's mutex handle externally.
   - Dynamite already applies all its patches from DllMain on this exe family (`hook/src/main.cpp:13-17` → `dynamite.cpp:82-110`), so DllMain patching is proven.
2. **Steam identity [S][F].** The game imports 13 Steam functions and no matchmaking. Its session transport is ISteamNetworking005 P2P, reached through the `SteamNetworking()` import (IAT slot `0x208E138`). It uses vtable slots 0x00 Send, 0x08 IsAvailable, 0x10 Read, 0x18 Accept, 0x20 Close and 0x30 GetSessionState, at call sites `0x1A5F749..0x1A5FC2F`.
   - With one Steam account, both processes get the same SteamID (`GetSteamID` is first called at `0x143E` and validated at `0x1B10`), so P2P between them can't work.
   - There is no `RestartAppIfNecessary` import, so launching the exe directly with steam_appid.txt=287700 works.
3. **Saves and config [S][F].** The save directory comes from `ISteamUser::GetUserDataFolder` (vtable idx 6, called at `0x170537`), which resolves to `userdata/97930372/287700/local`. That folder holds the 7 files the test runs hash. The cloud copies live in `userdata/97930372/311340/remote` (TPP_GAME_DATA, PERSONAL_DATA, TPP_CONFIG_DATA). Two instances on one account would share all of them, including `TPP_GRAPHICS_CONFIG`.
4. **Logs [F].** Already isolated per folder: `probe.cpp:133-150` and `steam_observer.cpp:173-200` require dinput8.dll to sit beside the exe and write to `<exe dir>/mgs-coop/`.
5. **Master server [S][C].** The mod still uses Konami. The URL string is at RVA `0x2218708`, and its only code reference is `lea rdx` at `0x7D346C`. The 1.0.15.3 address Dynamite used was `0x2218770`.
   - Same-account double login and co-op traffic to Konami are risky; Dynamite bundles **fuse** for that reason.
   - Goldberg or fake IDs cannot pass Konami login. fuse does no authentication ("Neither authorization nor authentication are implemented").
6. **Focus and input [S][C].**
   - **Keyboard and mouse:** raw input is registered at `0x1A45BC3` with `RIDEV_INPUTSINK` only when app byte `+0x40D` is set (`0x3FAE7C`). By default, only the foreground window gets keyboard and mouse input.
   - **Gamepad:** XInput is polled at `0x34F90` (thunk `0x32892C`) with no focus check, so **one pad drives both instances**.
   - **Focus loss:** WndProc `0x70430` chains to `0x249580`, where WM_KILLFOCUS sets bit 0 of `[*(0x2B93F10)+0x38]`. When not windowed, `0x24ABE4..0x24AC0D` leaves exclusive fullscreen and sends `WM_SYSCOMMAND SC_MINIMIZE`.
   - **Minimize:** the main loop (`0x1BC0`) polls `IsIconic` (`0x1C19`) and posts app event 0xA (inferred suspend), then 0xB on restore.
   - **Result:** use true **Windowed** mode. Community reports agree: windowed keeps running when unfocused, fullscreen and borderless pause. The current config is `FullScreenWindowed` at 2560x1440.
   - The game also has a force-foreground helper at `0x24B5B0` that runs when focus returns in fullscreen.

## 3. Options compared

| | Identity / transport | Mutex | Saves | Master server | Effort | Fidelity |
|---|---|---|---|---|---|---|
| **A** same account + our shim | Must emulate the 6 ISteamNetworking methods and the 1202/1203 callbacks, proxy `SteamUser` GetSteamID in DllMain (called at 0x143E before DI8), and handle auth tickets (unknown seam). | DllMain hook | Must redirect GetUserDataFolder plus remote storage (tpp-mod sites 0x16FBD0/0x1703C0/0x208D260/0x16FC2B) | fuse | High | Medium. Adds deterministic packet logs and fault injection. |
| **B** gbe_fork in 2 lab folders | Goldberg provides distinct IDs plus LAN P2P (binds port+i, broadcasts to 47584..+N, can add 127.0.0.1). | DllMain hook or Sandboxie | `local_save_path` per instance | fuse (required) | **Low-medium** | Good for mod and session logic; doesn't exercise real Steam relay or NAT. |
| **C** Sandboxie + 2nd Steam account | Real | Isolated by the box | Separate account; can't load MB without a save copy (portability unknown) | Konami (ban risk) or fuse | Needs a paid license; sandboxed Steam login has failed before | Highest |
| **D** VM | Real or gbe | Isolated | Isolated | Either | Very high; VBox DX11 unproven, Hyper-V GPU-P is heavy | High |

**Recommendation: B is the main harness. Sandboxie is a zero-code fallback for the mutex. Build A later only if gbe breaks the session. Use C or two PCs for final real-Steam sign-off.**

## 4. Harness design

**Folders** (outside steamapps, same D: volume):
`D:/Games/mgs-coop-lab/{host,guest}/` each containing:
- a copied `mgsvtpp.exe` (verify the hash; **don't hardlink**, so nothing can patch the real exe through the link)
- junctions to `master`, `mgo`, `CustomSoundtrack`
- `steam_appid.txt`, `version_info.txt`, the gbe `steam_api64.dll`, and the current `dinput8.dll`
- `steam_settings/configs.user.ini` with `[user::general] account_name, account_steamid, language=english` and `[user::saves] local_save_path=./mgs-coop/lab/saves`
- `steam_settings/configs.main.ini` with `[main::connectivity] listen_port=47584|47585`
- `steam_settings/custom_broadcasts.txt` containing `127.0.0.1`
- `steam_settings/steam_interfaces.txt`
- `mgs-coop/` with the runtime .dat, `script-observe.enabled` (`host-v3` / `client-v1`), `client-host.txt` (guest: host lab ID), `lab-instance.txt` (instance name, mutex suffix, master URL, xinput routing), a lab manifest, and `lab/saves/287700/{local,remote}` seeded from copies of the user's 287700/local and 311340/remote files.

Use deterministic lab IDs that never touch real Steam, for example host 76561198000000001 and guest 76561198000000002. Lab mode must refuse joins unless the gbe DLL is present.

**fuse:** run `server.Start("http://127.0.0.1:6667/","127.0.0.1:6667","tppstm",false,false,dsn,bonus)` as Dynamite's `masterserver.go` does, or run fuse with `-standalone`. Never run it in patching mode.

**dinput8 lab-mode changes** (all gated on `lab-instance.txt` plus exact code bytes):
- CreateMutexW IAT hook in DllMain
- master URL patch at 0x2218708 (≤62 chars) or rel32 retarget at 0x7D346C
- join trigger `mgs-coop/join.trigger` in the client poll (`game_script_observer.cpp:426-452`)
- new log lines `client_join=armed` and `session_status=<v>` on change, plus an atomic `status.json` (hook point `game_script_observer.cpp:283-289` / `dynamite_session_port.cpp:255-290`)
- XInput routing via IAT `0x208E070`

**Scripts (`tools/lab/`):**
- `lab.json`
- `Prepare-LabInstance.ps1`
- `Start-Lab.ps1`: preflight on processes, ports and hashes; start fuse; rotate logs; `Start-Process -WorkingDirectory`; position windows with `SetWindowPos` side by side at 1280x720.
- `Invoke-LabSmoke.ps1`: drive each title screen by foregrounding the window and sending SendInput with scan codes, holding keys about 150 ms. A manual-assist prompt is the fallback.
- `Get-LabScreens.ps1`: `GetWindowRect` + `CopyFromScreen`; two captures that differ show the window is still rendering.
- `Stop-Lab.ps1`
- Log tailing reads from saved offsets with FileShare.ReadWrite.

**Pass/fail rules:**
1. Host must log `installed=1`, `mode=host_v3`, `dynamite_host=returned; result=1`, `host_admission=authorized` and `dynamite_session=observed ... state=3 accept=1 ... consistent=1`.
2. Guest must log `mode=client_v1; configured_host=1` and `client_join=armed`.
3. The harness creates `join.trigger`; the guest must log `input_received; key=trigger_file` and then `dynamite_client=returned; request_started=1`.
4. **PASS** = guest `session_status=client_established` (state 7, started, two present members) **and** host `session_status=host_peer_present`, both stable across 2 polls within 180 s.
5. **FAIL** = `dynamite_client=refused`, `request_started=0`, `session_status=session_error`, a client-host refusal, a process exit, or a timeout.
6. Results go to `lab/runs/<ts>/result.json` with logs and screenshots.

**Milestones:**
- M0: host alone under gbe+fuse reaches state 3 with accept=1.
- M1: the guest runs alongside the host.
- M2: `request_started=1`.
- M3: established, the first-ever join.
- M4: the remote actor is visible in screenshots.

**One-PC couch mode:** host on keyboard and mouse in the foreground window; guest on an XInput pad with XInput disabled in the host via lab config. The guest window keeps rendering because it is in Windowed mode.

## 5. Key evidence paths
- `native/game_script_observer.cpp:410-452, 283-289, 519-596`
- `native/dynamite_session_port.cpp:255-290, 341-452`
- `native/probe.cpp:133-150, 237`
- `native/steam_observer.cpp:173-216`
- `tools/set-session-mode.ps1:7-33`, `tools/package-install.ps1:9,95`, `tools/package.ps1:31-32`, `tools/probe.ps1:9,26`
- `lua/coop/client_join.lua:39-58`
- `docs/testing-2026-09-12.md:147-162`
- `work/upstream-dynamite/hook/src/dynamitePatches.cpp:247-277`, `Config.cpp:72-81`, `masterserver/masterserver.go:11-95`, `webui/docs/docs.md:61-63,101-111,196-200,283-289`
- `work/upstream-tpp-mod/src/client/component/backend_server.cpp:294-315`
- `build/gbe_fork/dll/steam_user.cpp:507-518`, `dll/local_storage.cpp:602-607`
- gbe `network.cpp` (fetched to `scratchpad/singlepc/gbe_network.cpp`): 772-786, 897-901
- `C:/Windows/Sandboxie.ini`
- `C:/Sandbox/taylo/MGSV_Guest/drive/C/Program Files (x86)/Steam/{config/loginusers.vdf,logs/console_log.txt}`

Community sources: Steam discussion 527274088399724650 (windowed keeps running, fullscreen pauses on alt-tab); fuse `doc/USAGE.md` (-standalone, no auth); gbe_fork `post_build/README.release.md` and `steam_settings.EXAMPLE` configs.