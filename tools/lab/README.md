# Single-PC co-op lab scripts

These scripts run a host and a guest MGSV instance side by side on one PC and
decide pass/fail from logs alone. The contract with the native loader is
[docs/lab-protocol.md](../../docs/lab-protocol.md). The how-to and its limits
are in [docs/single-pc-testing.md](../../docs/single-pc-testing.md).

Requirements: PowerShell 7 (`pwsh`), Steam running and logged in, MGSV
1.0.15.4 in `MGS_TPP` and a guest folder `MGS_TPP_guest` (the same executable
and the shipped `steam_api64.dll`), a built loader (`tools/build.ps1`) that
contains the lab modules (observer, loopback, identity, mutex, save redirect,
master-server redirect call), and the local fuse build. None of the scripts
download anything.

| Script | What it does |
|---|---|
| `lab.json` | Paths, hashes, ports (fuse 6667, loopback 47810/47811), the guest's fake SteamID, characters, XInput routing, window layout and timeouts. Relative paths resolve against the repository root. |
| `Build-Fuse.ps1` | Builds `work/fuse/fuse.exe` offline (`GOPROXY=off`, `-mod=readonly`) if it is missing. Never clones; if the source is missing it tells you. |
| `Prepare-LabInstance.ps1` | Idempotent setup of both folders: hashes, owned loader install (with rollback), role markers, `lab.ini`, `coop.ini`, real-save backup and lab save copies, and windowed graphics for the lab copies. `-WhatIf` shows every change. `-Disable` switches the lab off and restores your own files. |
| `Start-Lab.ps1` | Preflight, then fuse (always `-standalone`), then the host and the guest, **each behind a safety gate** (below). Stops everything it started if a gate fails. Places the windows and writes `build/lab/runs/<ts>/run.json`. `-HostOnly` is milestone M0. `-LayoutOnly` re-applies window positions. |
| `Invoke-LabSmoke.ps1` | Pass/fail from logs, `lab-status.json` and fuse's request log. Creates the guest's `join.trigger` when both sides are ready and writes `result.json` plus redacted log excerpts. `-Assist` prints the manual steps. `-AutoTitle` is best effort. |
| `Get-LabScreens.ps1` | Saves a PNG of each game window into the run folder. |
| `Stop-Lab.ps1` | Closes **this run's** games (WM_CLOSE, then terminate after a timeout) and fuse, leaves any other MGSV process alone, then checks the real save hashes. |
| `LabCommon.psm1` | Shared helpers. The pure ones are covered by `tests/lab_scripts_tests.ps1` (CTest `lab_scripts_tests`). |

## End to end

```powershell
pwsh -File tools/build.ps1 -Configuration Release        # loader + all tests
pwsh -File tools/lab/Build-Fuse.ps1                      # no-op if fuse.exe exists
pwsh -File tools/lab/Prepare-LabInstance.ps1 -WhatIf     # review
pwsh -File tools/lab/Prepare-LabInstance.ps1             # apply (game closed)
pwsh -File tools/lab/Start-Lab.ps1                       # fuse + host + guest, gated
pwsh -File tools/lab/Invoke-LabSmoke.ps1 -Assist         # click through, wait for PASS/FAIL
pwsh -File tools/lab/Get-LabScreens.ps1 -Label after     # optional extra screenshots
pwsh -File tools/lab/Stop-Lab.ps1                        # close the run, verify real saves
pwsh -File tools/lab/Prepare-LabInstance.ps1 -Disable    # before normal online play
```

For the host-only check (M0, "host creation still works without Konami"), run
`Start-Lab.ps1 -HostOnly`, then `Invoke-LabSmoke.ps1 -Assist`. M0 passes only
with the host prerequisites below **and** proof that the run went through the
local fuse; otherwise it fails with `not_under_fuse`.

## Safety gate (Start-Lab)

The protocol's safety rules (saves redirected, fake identity kept off real
servers) depend on native code from several packages. Start-Lab therefore does
not trust the configuration files alone. After starting each instance, and
before starting the next, it reads that instance's `early.log` and
`steam-observer.log` from this run's offsets and requires, within
`smoke.safetyTimeoutSec` (default 120 s):

| Instance | Required lines |
|---|---|
| both | `saves=redirected; dir=<this folder's mgs-coop\lab\saves>` |
| both | `master_server=redirected; method=...; url=http://127.0.0.1:<fuse port>/tppstm/gate` |
| guest | `mutex=renamed; suffix=-guest` |
| guest | `identity=fake; steamid=76561202255233008` |

A line that shows a rule is **not** in force terminates that instance at once:
`saves=unchanged`, a different `dir=`, `master_server=refused` or `=failed`, a
different `url=`, `identity=real` or another id on the guest, `mutex=unchanged`
on the guest, or `early=start ... lab=0`. Missing evidence at the deadline, no
window, or an early exit also stop the start. Before the guest starts, a
`host-steamid.txt` written by this run's host must equal the guest's
`client-host.txt` and `lab.ini` `peer_steamid` (`host_id_mismatch` otherwise).
On any failure Start-Lab stops every process it started (games and fuse) and
records the reason under `aborted` in `run.json`.

Consequence: with a loader built from this branch alone (no early.log writer,
no `master_server::Install` call yet), Start-Lab always stops at the host's gate.
That is intended.

The preflight also checks that each `lab.ini` `save_dir` is the folder's own lab
copy, that `loopback=1`, and that the guest uses the fake SteamID and the
`-guest` mutex suffix.

## What Prepare-LabInstance changes

Per folder (host `MGS_TPP`, guest `MGS_TPP_guest`). The game must be closed in that
folder. The other instance may keep running, because processes are matched by
executable path.

- **Loader.** `build/native/dinput8.dll` is installed through
  `tools/package-install.ps1 -GameDirectory <folder> -PackageDirectory <stage> -NoPrompt`.
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
- **Host identity.** The host's SteamID64 is 76561197960265728 + Steam's
  `ActiveUser` (registry; accounts from 2^31 up are handled), else the single
  userdata account with MGSV saves, else `-HostSteamId`. docs/lab-protocol.md
  describes copying the host's `host-steamid.txt` instead; that file only exists
  after the host has run, so the scripts derive the id at Prepare time and
  cross-check `host-steamid.txt` at run time (Prepare warns when an earlier
  run's file names another account; Start-Lab and the smoke stop with
  `host_id_mismatch`). The id is written only to `lab.ini` and `client-host.txt`.
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
  and `character=` from `lab.json` (host `default`, guest `quiet`).
  **The native redirect honours `coop.ini` with or without `lab.ini`**, so in the
  host's real `MGS_TPP` folder this file keeps sending a normal game start to the
  local fuse (or offline when fuse is stopped) until you run `-Disable`. Deleting
  `lab.ini` alone is not enough; both generated files say so. Stop-Lab warns
  while the lab `coop.ini` is present.
- **Your own files.** A `lab.ini`/`coop.ini` that the scripts did not write is
  kept as `*.user-<ts>`. `-Disable` renames the generated files to
  `*.disabled-<ts>` and moves the newest `*.user-<ts>` back.
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
  (`-passThrough` is the only mode in which fuse itself forwards requests to
  Konami.)
- **Database.** `./fuse.dat` (SQLite) lives in fuse's working directory,
  `build/lab/fuse/`. Delete it to reset the server-side players.
- **`-writeLog`.** This creates `log/<unix start time>/` in the working
  directory. Each decoded request from the game goes into a file named
  `<unix ms>_<MSGID>_request`, as plain JSON. Responses are written only in
  `-passThrough` mode. These files include the Steam auth ticket from
  `CMD_AUTH_STEAMTICKET`, so the smoke test only counts message names.
- **Console log.** fuse's slog output goes to
  `build/lab/fuse/console/<run>-stdout.log`, outside the run folder, because it
  prints `CMD_AUTH_STEAMTICKET steamID=<id>` and player names. Run folders get a
  redacted copy (`fuse-stdout.redacted.log`).
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
  - **Fail closed.** A forged ticket sent to Konami's real gate would put the
    fake id (inside the real account's tampered ticket) in front of Steam's
    ticket validation. The loader therefore exposes
    `mgscoop::master_server::RedirectedToLoopback()`, true only after a verified
    redirect to 127.0.0.1, localhost or [::1]; any ticket or identity
    substitution must refuse unless it returns true.
  - The smoke records, without ids, how many distinct SteamIDs fuse accepted and
    whether the lab id was among them (`fuse_identities` in `result.json`), and
    warns if the guest never reached fuse with the lab id.
- **Residual Konami endpoints.** fuse's URL list (`handlers/gate_init.go`,
  `initUrlList`) still hands the game `HEATMAP=http://mgstpp-app.konamionline.com/tppstmweb/heatmap`
  and `DEVICE=http://mgstpp-app.konamionline.com/tppstm/main`, plus document
  links on `legal.konami.com` and `www.konami.com`. `CMD_GET_URLLIST` rewriting
  only replaces `mgstpp-game.konamionline.com`. If the game uses the heatmap or
  device URLs, an instance under fuse (including the guest with its fake
  identity) can still contact Konami's app server directly; fuse never sees that
  traffic, so the smoke cannot detect it. Whether the game calls them is
  unverified. To rule it out, block outbound traffic for the guest executable
  yourself while lab runs are active, for example (elevated PowerShell; remove
  the rule afterwards):

  ```powershell
  New-NetFirewallRule -DisplayName 'mgs-coop lab guest: no outbound' -Direction Outbound -Action Block `
      -Program 'D:\Games\steamapps\common\MGS_TPP_guest\mgsvtpp.exe'
  Remove-NetFirewallRule -DisplayName 'mgs-coop lab guest: no outbound'
  ```

  Windows Defender Firewall does not filter loopback traffic, so fuse and the
  loopback P2P emulation should keep working; that is untested here, and the
  rule matches only processes started from the guest folder's path.

## Run folder

`build/lab/runs/<ts>/` is meant to be shareable. Everything copied into it goes
through a redaction step: individual SteamID64s other than the lab's fake id,
Steam userdata account folder numbers and fuse player names are replaced.

| File | Contents |
|---|---|
| `run.json` | PIDs, process start times, hashes, log offsets, the expected safety values, gate results and any `aborted` reason. No SteamIDs. |
| `real-saves-before.json` | Real save hashes at run start (the account folder is recorded only in `build/lab/private/run-<ts>.json`). |
| `result.json` | The verdict, missing evidence, timings, evidence lines, safety state per instance, host-id check, final `lab-status.json` contents, fuse message counts and the id-free fuse identity summary. |
| `<role>-<log>` | Redacted excerpts covering only this run. |
| `fuse-stdout.redacted.log` | Redacted copy of fuse's console log. |
| `screens/*.png` | Window screenshots. |
| `stop.json` | Shutdown result per process, the real-save comparison and whether the lab `coop.ini` is still present. |

`build/lab/current-run.json` points to the newest run.

## Pass/fail (synthesis 3.5)

The smoke waits for readiness, bounded by `-ReadyTimeoutSec` (default 900 s,
because people click through the title screens):

- **Safety, every instance:** the gate lines above.
- **Host:** `DirectInput8Create_hresult=0x00000000` (probe.log),
  `install_result=0 installed=1`, `mode=host_v3`, `dynamite_host=returned; result=1`,
  `host_admission=authorized`, `dynamite_session=observed; … state=3 accept=1 … consistent=1`
  and `session_status=host_ready`.
- **Under fuse:** at least one `CMD_GET_URLLIST` and one `CMD_AUTH_STEAMTICKET`
  per instance since fuse started (fuse `-writeLog` files and console lines;
  the larger count wins). This is a count, so it cannot tell which instance
  sent what.
- **Guest:** `mode=client_v1; configured_host=1` and `client_join=armed`.

It then creates `join.trigger`. **PASS** means guest
`session_status=client_established` and host `session_status=host_peer_present`
both hold for two consecutive polls within `-TimeoutSec` (default 180 s) of the
trigger. **FAIL** at once on:

- an unsafe instance (`unsafe_<role>_<reason>`, see the gate)
- `host_id_mismatch`
- `dynamite_client=refused`
- `request_started=0`
- `client_join=refused`
- `refused=missing_or_invalid_client_host_txt`
- `session_status=session_error`
- `dynamite_host=returned; result=0`
- a process exit

A timeout reports the first missing step (`timeout_<phase>`, with `missing` in
`result.json`); when the missing evidence is the fuse traffic or the
master-server redirect, the reason is `not_under_fuse`. Only log lines appended
after `Start-Lab.ps1` recorded the offsets count.

## Stopping

Stop-Lab terminates only processes it can prove this run started: the recorded
pid, still running from the recorded executable path, with the recorded start
time. Any other `mgsvtpp`/`mgsvmgo` running from a lab folder (for example a
normal session started from Steam after the run) is listed and left running.
`-IncludeUnrecordedProcesses` sends those WM_CLOSE only, never a forced
termination, and also stops other fuse processes started from the lab
`fuse.exe`. `-KeepFuse` leaves this run's fuse running (with a warning).

## Testing the scripts without the game

CTest `lab_scripts_tests` (`tests/lab_scripts_tests.ps1`) runs in temporary
folders under `build/` and never touches the game folders, Steam or fuse. If
PowerShell 7 is missing, CMake warns and CTest reports the suite as skipped. It
covers:

- **Pure helpers:** SteamID arithmetic and registry DWORD conversion,
  `lab.ini`/`coop.ini` generation, the graphics tag, safety evidence rules,
  verdict rules (including `not_under_fuse` and every failure rule), host-id
  cross-check, redaction, fuse console parsing, stop-target selection, the
  offset log reader, and manifests, backup and seeding.
- **Native cross-check:** the generated `lab.ini`/`coop.ini` and URL validation
  are checked against the native parsers (`tests/lab_ini_check.cpp`).
- **Owned installs:** a host install and a copied-guest install with
  `tools/package-install.ps1`, the no-prompt path, and `set-session-mode.ps1`
  identity validation.
- **Simulated smoke runs:** pass, failure, host-id mismatch and an unsafe line,
  plus a check that no run folder contains a SteamID, account folder or ticket.
- **Full dry run:** Prepare, Start, Smoke, Screens, Stop, a later unrecorded
  session that Stop-Lab must leave alone, Start-Lab refusals at the safety gate
  (host `saves=unchanged`, host `master_server=refused`, guest `identity=real`,
  host-id mismatch), and Disable with the user's `coop.ini` restored. The
  stand-in `tests/lab_fake_process.cpp` is copied as the fixture's
  `mgsvtpp.exe` and `fuse.exe`. It only runs where `mgs-coop\lab.ini` exists and
  plays the native side (log lines, fuse logins, loopback UDP).

For the dry run, `lab.json` takes an optional `paths.steamRoot`, which
replaces the registry Steam path.

## Limits

- A single-PC pass proves session and mod logic only. It does not cover Steam
  NAT, relay, latency, loss or real-account behaviour. A two-PC or two-account
  run is still required before calling anything working.
- The gate and the smoke trust what native code logs. They prove the lines were
  written, not that, for example, every save write really went to the lab copy;
  Stop-Lab's real-save hash comparison is the after-the-fact check.
- `-AutoTitle` sends synthetic scan-code key presses to the focused window.
  Games often ignore them. Prefer `-Assist`.
- Screenshots of a window covered by another window can show the cover when
  the CopyFromScreen fallback is used.
