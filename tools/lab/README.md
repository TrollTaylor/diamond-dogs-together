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
| `lab.json` | Paths, hashes, ports (fuse 6667, loopback 47810/47811, partner net `netHost`/`netGuest` 47820/47821), the offline mode (`offline.masterPort` 47899, `offline.marker` `offline-v1`), the partner spike `lab.ini` defaults (`partner`), the guest's fake SteamID, characters (fuse lab: host `default`, guest `quiet`; `offline.characters`: host `default`, guest `quiet`, the test condition of docs/partner-anim-appearance.md), XInput routing, window layout and timeouts. Relative paths resolve against the repository root. |
| `Build-Fuse.ps1` | Builds `work/fuse/fuse.exe` offline (`GOPROXY=off`, `-mod=readonly`) if it is missing. Never clones; if the source is missing it tells you. |
| `Prepare-LabInstance.ps1` | Idempotent setup of both folders: hashes, owned loader install (with rollback), role markers, `lab.ini`, `coop.ini`, real-save backup and lab save copies, and windowed graphics for the lab copies. `-WhatIf` shows every change. `-Disable` switches the lab off, restores your own files and resets `offline-v1` markers to `host-v3` / `client-v1`. `-Offline` prepares the offline lab mode instead (below); `-PartnerSetting` overrides partner `lab.ini` keys. |
| `Start-Lab.ps1` | Preflight, then fuse (always `-standalone`), then the host and the guest, **each behind a safety gate** (below). Stops everything it started if a gate fails. Places the windows and writes `build/lab/runs/<ts>/run.json`. `-HostOnly` is milestone M0. `-LayoutOnly` re-applies window positions. `-Offline`: no fuse; requires a closed master port and free partner UDP ports (below). |
| `Invoke-LabSmoke.ps1` | Pass/fail from logs, `lab-status.json` and fuse's request log. Creates the guest's `join.trigger` when both sides are ready and writes `result.json` plus redacted log excerpts. `-Assist` prints the manual steps. `-AutoTitle` is best effort. |
| `Get-LabScreens.ps1` | Saves a PNG of each game window into the run folder. |
| `Send-LabInput.ps1` | Focuses one game window and sends scan-code keys: `enter`, `ctrl+f8`, held keys `w:4000`, chords `w+a:6000`; `shift`, `f10`, `f11` included. Pressed keys are always released (finally block), even on Ctrl+C. `-HoldMs 0` still taps. `-Shot` captures the window afterwards. Best effort (focus). |
| `Stop-Lab.ps1` | Closes **this run's** games (WM_CLOSE, then terminate after a timeout) and fuse, leaves any other MGSV process alone, deletes unconsumed `partner.trigger` files, records the log end offsets in `stop.json`, then checks the real save hashes. |
| `Set-PartnerTrigger.ps1` | Offline mode: validates a partner command (`stage=0..3 source=fixed\|orbit\|net apply\|move=warp\|slot\|both\|xform\|hybrid anim=off\|mirror\|net aset=q\|s\|qs\|r reset=n\|a\|c\|ac look=copy\|net\|quiet\|snake probe=ps\|off` and the one-shots `census=1` / `parts=explicit` / `mirror=on`, or `stop`; at most 127 bytes) and writes it atomically to an instance's `mgs-coop/partner.trigger`. `-Role both`, `-Force`; `-WaitSec` confirms the native result in `script-observer.log` (exit 0 applied, 2 not consumed, 3 rejected or different, 4 unconfirmed). |
| `Test-PartnerSpike.ps1` | Offline mode: PASS / FAIL / INCOMPLETE JSON verdict for experiment E0..E8 of the spike plan, or A0/A1/A3/M1/N1/B1/B4/C1 of docs/partner-anim-appearance.md, from this run's logs. Exit code 0 / 1 / 2. |
| `Invoke-PartnerExperiment.ps1` | Offline mode: runs one experiment's triggers, key input and screenshots, then polls `Test-PartnerSpike.ps1` until PASS or FAIL. |
| `LabCommon.psm1` | Shared helpers. The pure ones are covered by `tests/lab_scripts_tests.ps1` (CTest `lab_scripts_tests`). |
| `PartnerSpike.psm1` | Pure partner helpers: trigger grammar, log line parsing, the experiment rules. Covered by the same tests. |

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

Offline lab mode (the partner spike, no fuse; details below):

```powershell
pwsh -File tools/lab/Prepare-LabInstance.ps1 -Offline              # offline-v1 markers, closed master port, partner keys
pwsh -File tools/lab/Start-Lab.ps1 -Offline                        # host + guest, gated; no fuse
pwsh -File tools/lab/Test-PartnerSpike.ps1 -Experiment E0          # after both reached Mother Base
pwsh -File tools/lab/Set-PartnerTrigger.ps1 -Role host -Text 'stage=1 source=fixed apply=warp'
pwsh -File tools/lab/Test-PartnerSpike.ps1 -Experiment E1
pwsh -File tools/lab/Invoke-PartnerExperiment.ps1 -Experiment E3   # trigger, screenshots, poll the verdict
pwsh -File tools/lab/Stop-Lab.ps1
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
  and `character=` from `lab.json` `characters` (host `default`, guest
  `quiet`); the offline mode uses `offline.characters` (`default` for both
  while the partner spike runs).
  **The native redirect honours `coop.ini` with or without `lab.ini`**, so in the
  host's real `MGS_TPP` folder this file keeps sending a normal game start to the
  local fuse (or offline when fuse is stopped) until you run `-Disable`. Deleting
  `lab.ini` alone is not enough; both generated files say so. Stop-Lab warns
  while the lab `coop.ini` is present.
- **Your own files.** A `lab.ini`/`coop.ini` that the scripts did not write is
  kept as `*.user-<ts>`. `-Disable` renames the generated files to
  `*.disabled-<ts>` and moves the newest `*.user-<ts>` back. It also resets an
  `offline-v1` role marker (offline mode; it never creates a session) to
  `host-v3` / `client-v1`; other markers stay.
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

## Offline lab mode (partner spike)

[docs/offline-partner-spike-plan.md](../../docs/offline-partner-spike-plan.md)
sections 2.5 and 4; the how-to is in
[docs/single-pc-testing.md](../../docs/single-pc-testing.md). Both instances run
with no fuse and no session.

- **Prepare-LabInstance.ps1 -Offline** writes, per folder, the `offline-v1`
  marker (both roles; `lab.json` `offline.marker` also allows the `host-v1`
  fallback), a `coop.ini` with `master_url=http://127.0.0.1:47899/tppstm/gate`
  (`offline.masterPort`; a port where nothing listens, so the game reaches no
  server) and a `lab.ini` with the base lab keys plus the partner keys:
  `net=1`, `net_listen_port`/`net_peer_port` (host 47820/47821, guest
  47821/47820), `partner=1`, `partner_stage`, `partner_source`,
  `partner_apply`, `partner_anim`, `partner_aset`, `partner_reset`, `partner_look`,
  `partner_rate_hz`, `partner_interp_ms`, `partner_offset_cm`,
  `partner_teardown`, `partner_force_exists`, `partner_policy_relax`,
  `partner_anim_probe`, `crash_probe` (values from `lab.json` `"partner"`).
  `-PartnerSetting key=value,host:key=value,guest:key=value` overrides keys
  for one preparation (validated: stage 0..3, rate 1..60 Hz, interp 0..500 ms,
  offset 50..1000 cm, ...). The `coop.ini` character comes from `lab.json`
  `offline.characters` (host `default`, guest `quiet`: the animation /
  appearance test condition); `-Character default` (or `host:name` /
  `guest:name`) overrides it for one preparation, for example for a regression
  rerun of the proven E0-E8, and `Invoke-PartnerExperiment.ps1` notes a
  non-default character before an E-series experiment. The fuse is not
  involved. Running the script without `-Offline` switches both folders back.
- **Play-OfflineCoop.ps1** (`Play-Offline-Coop.bat`) is the one-click test: the
  guest plays Quiet, `look=net` shows each player's own character on the
  other copy, and (round 4) each partner is driven by the other player's pad
  (`apply=input input=net isend=1 hold=own words=hpr recon=track`, see
  docs/partner-anim-appearance.md "Round 4"). [W] switches both copies back to
  the proven Warp movement, [I] / [P] to the input drive (tracked / pinned),
  [M] / [A] to the round 2 experiments; `-ProvenWarp` starts on the proven
  path, `-Experimental` with the round 2 experiments.
- **Start-Lab.ps1 -Offline** skips the fuse checks and start. Its preflight
  requires no listener on TCP 47899, free UDP 47810/47811 and 47820/47821, the
  `offline-v1` markers, `lab.ini` `net=1`, `partner=1` and the role's partner
  ports, and the closed-port `coop.ini` URL. The safety gate is unchanged:
  `master_server=redirected` with that URL, `saves=redirected`, and for the
  guest `mutex=renamed` and `identity=fake`. `run.json` records `offline: true`
  plus the ports and each instance's partner values. A leftover
  `partner.trigger` is removed before the start.
- **Set-PartnerTrigger.ps1 -Role host|guest|both -Text '...'** sends a runtime
  command: `stage=N source=... apply=...` and the one-shots `parts=explicit`
  (op 16, variant F3b) and `mirror=on` (op 32, F3c), any subset, or `stop`.
  The text is validated and written in canonical order to a temporary file
  that is renamed over `mgs-coop/partner.trigger`; no window focus is needed.
  The native side polls the file every 30 Lua ticks during a 30050 mission and
  deletes it, accepted or rejected. `-WaitSec` waits for that and then reads
  the result in `script-observer.log`: exit 0 `partner_stage=changed` with the
  requested values, 2 not consumed, 3 `partner_trigger=rejected` or a different
  command applied, 4 no result line within 3 s. An unconsumed earlier file is
  only replaced with `-Force`. Commands are logged to
  `<run>/partner-triggers.log`.
- **Test-PartnerSpike.ps1 -Experiment E0|E1|E2|E3|E4|E5|E6|E8 [-Role host|guest|both]**
  reads this run's `script-observer.log`, `early.log` and `steam-observer.log`
  from the `run.json` offsets and applies the plan's section 3 rules with the
  section 2.7 line spellings (the matched lines are listed in
  [docs/lab-protocol.md](../../docs/lab-protocol.md)):

  | Experiment | PASS (log part) |
  |---|---|
  | E0 | both instances (a host-only run stays INCOMPLETE); redirect to the closed port; `mode=offline_v1`; `host_create=skipped`; `partner_net=listening`; `partner_cfg=ready` with `count=2 local=0 R=1 online_bit=0 sync=null`; first stage-0 probe `ids=xxxx,ffff pool=xx,ff`; `first_rx` and stats with tx, rx > 0 (both instances); `partner_tick=rate` (unfocused `hz>=10`); `partner_units` with `yaw_abs_max_cdeg>628` and lua/mem positions within 50 mm while they change; 5 minutes of probes; no `crash_probe=av` |
  | E1 | `stage=1 apply=warp`: `partner_warp_check err_mm<=5` within the first two checks; ids[1] stays ffff; no Lua error |
  | E2 | `stage=2`: `allactive_set`; a probe with `s0` bit 1 and `q170=0x3`; outcome A (ffff for 10 s) or B (realized), in `data.outcome` |
  | E3 | `stage=3`: `parts_copied`; `partner_bits=set`; `partner_realized=1` with `ms_after_bits<=1000`; `partner_parts=loaded` with `ms_after_realize<=15000`; 120 s of probes without `partner_realized=0` or `crash_probe=av`. Already realized before the bits (E2 outcome B): the 1 s rule is skipped and parts (15 s) and stability (120 s) count from the `stage=3` line on the probe clock |
  | E4 | `stage=3 source=orbit`: realized; at least 4 `partner_warp_check` lines, every `err_mm<=5`; `slot_mm` spread at least 500 mm |
  | E5 | under orbit, `apply=slot` and then `apply=both` both pass the E4 log checks |
  | E6 | `stage=3 source=net` on both: `first_rx`; rx increasing over two stats lines; `status=live` with `last_age_ms<200`; realized; while both stand still, the local `pos1_mm` within 200 mm of the peer's `pos0_mm` |
  | E8 | `stop` (`partner_bits=cleared; reason=trigger`), `allactive_cleared`, `partner_realized=0`, realized again, `partner_stop`, `partner_cfg=ready` after re-entry |

  FAIL when a line contradicts a rule or a deadline on the probe clock (`t_ms`)
  passes; INCOMPLETE while evidence is missing. A Lua `event=error`/`refused`,
  `crash_probe=av` or a dead process fails every experiment, and so does
  `partner_cfg=refused` for `online_bit`/`sync_present` or at a mission start
  (CFG). A per-tick refusal followed by `partner_cfg=ready` is a note
  (INCOMPLETE until it recovers). `mem_pos_mm=-2147483648,...` is reported as
  an unreadable vars block, not as a units failure. A stopped run is read up
  to `stop.json` `log_end_offsets`, so `-RunDir` on an older run ignores later
  runs' lines. Each experiment is judged on the log segment in which its
  configuration was active, so one boot can walk through E1..E5. Screenshot
  criteria are listed in `notes` as manual. Output: one JSON object
  (`experiment, role, verdict, matched, missing, failed, notes, per_role, run`);
  `-OutFile` saves a redacted copy; exit code 0 PASS, 1 FAIL, 2 INCOMPLETE.
- **Invoke-PartnerExperiment.ps1 -Experiment Ex** performs the experiment's
  steps (triggers, `Send-LabInput` keys such as `w+a:6000` and `w:4000`,
  screenshots) and polls the verdict until PASS, FAIL or its timeout; the final
  JSON goes to `<run>/partner-<Ex>-<time>.json`. `-NoInput` prints the keys to
  press instead of sending them. E0 walks both instances. Triggers are sent
  without `-Force`: a pending earlier trigger is waited for, and a rejected
  trigger stops the experiment.
- **Invoke-LabSmoke.ps1** refuses offline runs (there is no session to test).
- **Stop-Lab.ps1** is unchanged apart from deleting unconsumed `partner.trigger`
  files and recording the log end offsets in `stop.json`; with the offline `coop.ini` still in the host folder it warns that a
  normal start from there runs offline.

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
| `stop.json` | Shutdown result per process, the real-save comparison, whether the lab `coop.ini` is still present, which unconsumed partner triggers were deleted and each instance's log lengths after the close (`log_end_offsets`). |
| `partner-triggers.log`, `partner-<Ex>-<time>.json` | Offline mode: the partner commands sent, and Invoke-PartnerExperiment verdicts. |

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
- **Offline mode and partner spike:** offline settings (closed-port URL, net
  ports, partner keys in spec order, the online texts byte-identical to
  before), lab.json and `-PartnerSetting` validation, the offline preflight,
  Send-LabInput key specs, the trigger grammar, log line parsing, PASS and
  FAIL fixture logs for E0, E1, E3 and E6 (`tests/fixtures/partner-lab`) plus
  inline logs for E2, E4, E5 and E8, and `Test-PartnerSpike.ps1`,
  `Set-PartnerTrigger.ps1` and `Invoke-PartnerExperiment.ps1` on fixture runs.
- **Full dry run:** Prepare, Start, Smoke, Screens, Stop, a later unrecorded
  session that Stop-Lab must leave alone, Start-Lab refusals at the safety gate
  (host `saves=unchanged`, host `master_server=refused`, guest `identity=real`,
  host-id mismatch), and Disable with the user's `coop.ini` restored. The
  stand-in `tests/lab_fake_process.cpp` is copied as the fixture's
  `mgsvtpp.exe` and `fuse.exe`. It only runs where `mgs-coop\lab.ini` exists and
  plays the native side (log lines, fuse logins, loopback UDP).

The dry run then repeats the offline mode with the same stand-ins: Prepare
-Offline, Start-Lab -Offline (no fuse, gate ok on both), an INCOMPLETE E0
verdict (the stand-ins write no partner lines), triggers, Stop-Lab removing
them, Start-Lab refusals (a listener on the master port; offline folders
without `-Offline`) and the switch back to the fuse mode.

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
