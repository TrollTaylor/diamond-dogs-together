# Single-PC co-op lab scripts

These scripts run a host and a guest MGSV instance side by side on one PC and
decide pass/fail from logs alone. The contract with the native loader is
[docs/lab-protocol.md](../../docs/lab-protocol.md). The how-to and its limits
are in [docs/single-pc-testing.md](../../docs/single-pc-testing.md).

Requirements: PowerShell 7 (`pwsh`), Steam running and logged in, MGSV
1.0.15.4 in `MGS_TPP` and a guest folder `MGS_TPP_guest` (the same executable
and the shipped `steam_api64.dll`), a built loader (`tools/build.ps1`) and the
local fuse build. None of the scripts download anything.

| Script | What it does |
|---|---|
| `lab.json` | Paths, hashes, ports (fuse 6667, loopback 47810/47811), the guest's fake SteamID, characters, XInput routing, window layout and smoke timeouts. Relative paths resolve against the repository root. |
| `Build-Fuse.ps1` | Builds `work/fuse/fuse.exe` offline (`GOPROXY=off`, `-mod=readonly`) if it is missing. Never clones; if the source is missing it tells you. |
| `Prepare-LabInstance.ps1` | Idempotent setup of both folders: hashes, owned loader install (with rollback), role markers, `lab.ini`, `coop.ini`, real-save backup and lab save copies, and windowed graphics for the lab copies. `-WhatIf` shows every change. `-Disable` switches the lab off. |
| `Start-Lab.ps1` | Preflight, then fuse (always `-standalone`), the host, and the guest once the host has a window. Places the windows and writes `build/lab/runs/<ts>/run.json`. `-HostOnly` is milestone M0. `-LayoutOnly` re-applies window positions. |
| `Invoke-LabSmoke.ps1` | Pass/fail from logs and `lab-status.json`. Creates the guest's `join.trigger` when both sides are ready and writes `result.json` plus log excerpts. `-Assist` prints the manual steps. `-AutoTitle` is best effort. |
| `Get-LabScreens.ps1` | Saves a PNG of each game window into the run folder. |
| `Stop-Lab.ps1` | Closes the games (WM_CLOSE, then terminate after a timeout) and fuse. Then it checks the real save hashes. |
| `LabCommon.psm1` | Shared helpers. The pure ones are covered by `tests/lab_scripts_tests.ps1` (CTest `lab_scripts_tests`). |

## End to end

```powershell
pwsh -File tools/build.ps1 -Configuration Release        # loader + all tests
pwsh -File tools/lab/Build-Fuse.ps1                      # no-op if fuse.exe exists
pwsh -File tools/lab/Prepare-LabInstance.ps1 -WhatIf     # review
pwsh -File tools/lab/Prepare-LabInstance.ps1             # apply (game closed)
pwsh -File tools/lab/Start-Lab.ps1                       # fuse + host + guest
pwsh -File tools/lab/Invoke-LabSmoke.ps1 -Assist         # click through, wait for PASS/FAIL
pwsh -File tools/lab/Get-LabScreens.ps1 -Label after     # optional extra screenshots
pwsh -File tools/lab/Stop-Lab.ps1                        # close all, verify real saves
pwsh -File tools/lab/Prepare-LabInstance.ps1 -Disable    # before normal online play
```

For a host-only check that host creation still works under fuse (M0), run
`Start-Lab.ps1 -HostOnly`, then `Invoke-LabSmoke.ps1 -Assist`. The smoke passes on
`dynamite_host=returned; result=1`, `host_admission=authorized` and
`session_status=host_ready` held for two polls.

## What Prepare-LabInstance changes

Per folder (host `MGS_TPP`, guest `MGS_TPP_guest`). The game must be closed in that
folder. The other instance may keep running, because processes are matched by
executable path.

- **Loader.** `build/native/dinput8.dll` is installed through
  `tools/package-install.ps1 -GameDirectory <folder> -PackageDirectory <stage>`.
  The installer keeps backups and an ownership record, and you can roll back with
  `tools/package-install.ps1 -Action Restore -GameDirectory <folder> -PackageDirectory <stage>`.
  The guest folder was made as a copy of the host install, so its old record names
  `MGS_TPP`. The script accepts that once through `-AcceptRecordCopiedFrom`.
  Afterwards the guest's `probe-install.json` names the guest folder. The runtime
  archive (SHA-256 `26CE7C21…`) is copied from the host's `mgs-coop` folder. It is
  game-derived and never leaves this PC.
- **Role.** The script writes the role marker with the same checks and exact
  bytes as `tools/set-session-mode.ps1`: an owned, unmodified loader and the
  archive hash from `lab.json`. The host gets `host-v3`. The guest gets
  `client-v1`, with `client-host.txt` set to the host's real SteamID64. To set
  a role by hand, run `tools/set-session-mode.ps1 -GameDirectory <folder>`.
- **`mgs-coop/lab.ini`** follows the lab-protocol defaults:

  | Setting | Host | Guest |
  |---|---|---|
  | `self_steamid` | real | `76561202255233008` (`self_name=Guest`) |
  | `peer_steamid` | the guest's fake id | the host's real id |
  | `listen_port` / `peer_port` | 47810 / 47811 | 47811 / 47810 |
  | `mutex_suffix` | — | `-guest` |
  | `xinput` | `none` | `0` |

  Both instances also get `loopback=1`, `save_dir=<folder>\mgs-coop\lab\saves` and
  `join_trigger=1`.
- **`mgs-coop/coop.ini`** contains `master_url=http://127.0.0.1:6667/tppstm/gate`
  and `character=` from `lab.json` (host `default`, guest `quiet`). A
  `lab.ini`/`coop.ini` that the scripts did not write is kept as `*.user-<ts>`.
- **Saves.**
  - The real `Steam\userdata\<id>\287700` and `311340` folders are read only once.
    They are copied, with a SHA-256 manifest, to `build/lab/save-backups/<ts>/`.
  - Each `save_dir` is seeded from that backup:
    - `287700/local/*` goes to `<save_dir>\287700\local\`.
    - `311340/remote/*` goes to `<save_dir>\remote\`.
  - `-Reseed` refreshes the lab copies. The old lab copies are moved to
    `build/lab/save-archive/`, not deleted.
- **Graphics.**
  - The lab copy of `TPP_GRAPHICS_CONFIG` is set to `Windowed` 1280x720, because
    only true windowed mode keeps rendering when the window is unfocused.
  - The file's `"as"` member is an integrity tag. Static analysis of the exe
    (0x52BE40, tag function 0x52D0E0) shows the tag is computed as follows:
    1. Take jsoncpp FastWriter output of the object without `"as"`, plus `"\n"`.
    2. Append `"-%x"` of that string's length.
    3. Hash with CryptoAPI MD5.
    4. Store the base64 of the first 8 bytes.
  - This was reproduced exactly on a real file. The script refuses to edit a file
    whose existing tag does not verify. In that case, or if the lab copy does not
    exist yet, set Windowed 1280x720 once in each instance's options. That changes
    only the lab copy.
- **Guest executable.** `MGS_TPP_guest\mgsvtpp.exe` is a hardlink to the host
  executable. Nothing in the lab writes it. `-CopyGuestExe` replaces the link
  with a verified independent copy.

## fuse (local master server)

The lab uses fuse commit `f75b41d`, source in `work/fuse`.

- **Always `-standalone`.** Without that flag, fuse backs up and patches
  `mgsvtpp.exe` on disk, at an offset for a different exe version.
  `Start-Lab.ps1` refuses any argument list without `-standalone`, with
  `-clientURL` or `-passThrough`, or listening anywhere except 127.0.0.1.
- **Database.** `./fuse.dat` (SQLite) lives in fuse's working directory,
  `build/lab/fuse/`. Delete it to reset the server-side players.
- **`-writeLog`.** This creates `log/<unix start time>/` in the working
  directory. Each decoded request from the game goes into a file named
  `<unix ms>_<MSGID>_request`, as plain JSON. Responses are written only in
  `-passThrough` mode.
  - These files include the Steam auth ticket from `CMD_AUTH_STEAMTICKET`.
    `Invoke-LabSmoke.ps1` therefore records only the message names and counts
    (`fuse_messages` in `result.json`).
  - fuse's console log (slog, stdout) goes to `fuse-stdout.log` in the run folder.
    It prints `CMD_AUTH_STEAMTICKET steamID=<id>`.
- **Authentication.** fuse authenticates nobody.
  - For `CMD_AUTH_STEAMTICKET`, it base64-decodes the ticket and takes the
    little-endian u64 at byte offset 12 as the SteamID. The ticket is not
    validated or range checked at this step, and the account id it returns is
    that number.
  - `CMD_REQAUTH_HTTPS` then uses the `user_name` the game sends (a SteamID,
    per fuse's source comment). fuse range checks that value
    (0x0110000100000000..0x01100001FFFFFFFF) and creates or reuses a user,
    player and session keyed by it.
  - Unverified: whether the game fills `user_name` from the AUTH response's
    `account_id` or from its own `GetSteamID`.
  - A real ticket from the guest process would carry the host's real SteamID.
    Both instances would then share one fuse user, and the second login would
    reuse the first one's session. So for distinct fuse identities, the guest
    must present the fake id: rewrite ticket bytes 12..19, and also
    `GetSteamID` if it feeds `user_name`. The fake id passes fuse's range check.

## Run folder

`build/lab/runs/<ts>/` contains:

| File | Contents |
|---|---|
| `run.json` | PIDs, hashes and log offsets. No SteamIDs. |
| `real-saves-before.json` | Real save hashes at run start. |
| `fuse-stdout.log`, `fuse-stderr.log` | fuse's console output. |
| `result.json` | The verdict, timings, evidence lines, final `lab-status.json` contents and fuse message counts. |
| `<role>-<log>` | Excerpts covering only this run. |
| `screens/*.png` | Window screenshots. |
| `stop.json` | Shutdown result and the real-save comparison. |

`build/lab/current-run.json` points to the newest run.

## Pass/fail (synthesis 3.5)

The smoke waits for readiness, bounded by `-ReadyTimeoutSec` (default 900 s,
because people click through the title screens):

- **Host:** `dynamite_host=returned; result=1`, `host_admission=authorized` and
  `session_status=host_ready`.
- **Guest:** `client_join=armed`.

It then creates `join.trigger`. **PASS** means guest
`session_status=client_established` and host `session_status=host_peer_present`
both hold for two consecutive polls within `-TimeoutSec` (default 180 s) of the
trigger. **FAIL** is any of:

- `dynamite_client=refused`
- `request_started=0`
- `client_join=refused`
- `refused=missing_or_invalid_client_host_txt`
- `session_status=session_error`
- `dynamite_host=returned; result=0`
- a process exit
- a timeout

Only log lines appended after `Start-Lab.ps1` recorded the offsets count.

## Testing the scripts without the game

CTest `lab_scripts_tests` (`tests/lab_scripts_tests.ps1`) runs in temporary
folders under `build/` and never touches the game folders, Steam or fuse. It
covers:

- **Pure helpers:** SteamID arithmetic, `lab.ini`/`coop.ini` generation, the
  graphics tag, verdict rules, the offset log reader, and manifests, backup and
  seeding.
- **Native cross-check:** the generated `lab.ini`/`coop.ini` and URL validation
  are checked against the native parsers (`tests/lab_ini_check.cpp`).
- **Owned installs:** a host install and a copied-guest install with
  `tools/package-install.ps1`.
- **Full dry run:** Prepare, Start, Smoke, Screens, Stop and Disable. The
  stand-in `tests/lab_fake_process.cpp` is copied as the fixture's `mgsvtpp.exe`
  and `fuse.exe`. It only runs where `mgs-coop\lab.ini` exists and plays the
  native side over loopback UDP.

For the dry run, `lab.json` takes an optional `paths.steamRoot`, which
replaces the registry Steam path.

## Limits

- A single-PC pass proves session and mod logic only. It does not cover Steam
  NAT, relay, latency, loss or real-account behaviour. A two-PC or two-account
  run is still required before calling anything working.
- `-AutoTitle` sends synthetic scan-code key presses to the focused window.
  Games often ignore them. Prefer `-Assist`.
- Screenshots of a window covered by another window can show the cover when
  the CopyFromScreen fallback is used.
