# Single-PC testing: host and guest on one machine

This is how to run a host instance and a guest instance of MGSV on one PC and
try a co-op join with a log-only pass/fail result. The offline lab mode for
the partner spike (no fuse, no session, own netcode) is described in
[Offline lab mode](#offline-lab-mode-the-partner-spike-no-server-at-all).
Script reference: [tools/lab/README.md](../tools/lab/README.md). The file and
log contract between the scripts and the loader: [lab-protocol.md](lab-protocol.md).

## What a single-PC run can and cannot show

- **It can show** that the mod's session logic works end to end: host
  creation, guest join request, session establishment on both sides, and the
  mod's own state machine and logs.
- **It cannot show** anything about Steam networking. Traffic between the two
  instances goes over 127.0.0.1 through the loader's loopback emulation of
  ISteamNetworking, and the guest uses a fake SteamID
  (`76561202255233008`, account `0xFFFFFFF0`) that must never reach Steam or
  Konami (enforced by the safety gate below and the fail-closed rule for
  identity forging). Steam NAT traversal, relays, latency, loss and
  real-account behaviour are not exercised.
- **Real-network sign-off.** A run on two PCs or two Steam accounts is still
  required before anything is called working.

## How it works

| Problem on one PC | Lab answer (loader side, gated on `mgs-coop/lab.ini`) |
|---|---|
| The second instance exits on the single-instance mutex | The guest appends `mutex_suffix=-guest` to the mutex name |
| Both instances have the same SteamID | The guest reports a fake identity; P2P is emulated over UDP 47810/47811 (`loopback=1`) |
| Both instances share saves | Each instance's saves are redirected to a copy under `<game>\mgs-coop\lab\saves` |
| Konami login with a fake identity, and ban risk | Both use a local fuse master server: `coop.ini master_url=http://127.0.0.1:6667/tppstm/gate`. Start-Lab refuses to keep an instance running unless its log shows the redirect, and identity forging must check `master_server::RedirectedToLoopback()` (fail closed). fuse still hands out Konami heatmap/device URLs; see "Residual Konami endpoints" in tools/lab/README.md |
| Fullscreen and borderless windows pause when unfocused | The lab save copies are set to Windowed 1280x720 |
| One gamepad drives both instances | `xinput=none` on the host and `xinput=0` on the guest. The host uses keyboard and mouse |
| The Ctrl+F8 join needs focus and fires once | The harness creates `mgs-coop/join.trigger` (`join_trigger=1`) |

With no `lab.ini` the loader behaves exactly as before. Without a `coop.ini`
`master_url`, the game keeps Konami's URL. A `coop.ini` `master_url` is honoured
**with or without** `lab.ini`, so the lab `coop.ini` in the host's real folder
keeps redirecting normal play until `Prepare-LabInstance.ps1 -Disable` (which also
restores your own `coop.ini`).

### Safety gate

The safety rules in [lab-protocol.md](lab-protocol.md) are enforced at run time,
not assumed from the configuration files. Start-Lab starts one instance at a
time and, before starting the next, requires this run's `early.log` and
`steam-observer.log` to show `saves=redirected` with the folder's lab save
directory and `master_server=redirected` with the lab fuse URL; the guest must
also show `mutex=renamed` (`-guest`) and `identity=fake` with the lab SteamID. A
line showing a rule is not in force (`saves=unchanged`,
`master_server=refused`/`failed`, `identity=real`, a different dir, URL or id)
terminates that instance immediately; missing evidence after
`smoke.safetyTimeoutSec` (120 s) stops it as well. On any failure Start-Lab
stops every process it started, including fuse. The smoke test applies the same
rules on every poll and fails at once on an unsafe line. Details: the "Safety
gate" section of [tools/lab/README.md](../tools/lab/README.md).

### The master-server redirect

`native/master_server_patch.cpp` changes the game's gate URL in memory only.

**The site.** In 1.0.15.4 the URL `https://mgstpp-game.konamionline.com/tppstm/gate`
sits at RVA 0x2218708. It is 48 characters plus a NUL, followed by 7 zero
alignment bytes; a constant starts at 0x2218740. Its only reference in any
section is the `lea rdx,[rip+disp32]` at 0x7D346C (disp32 at 0x7D346F) in
case 1 of the gate state machine. No absolute or RVA pointer refers to it. This
was checked by `tools/generate_master_server_contract.py`, which writes
`native/master_server_contract.hpp`.

**Before writing.** The loader verifies all 27 context bytes, the displacement
target, the string, its terminator and the padding. It refuses and logs a
reason on any mismatch.

**How it writes.** The method depends on the URL length:
- Up to 48 characters: it overwrites the string in place, NUL-padded to 49
  bytes, and restores the page protection.
- 49 to 69 characters: it retargets the lea's disp32 to a read-only buffer
  within ±2 GB.

The lab URL (33 characters) uses the in-place method.

**What it logs.** `master_server=redirected; method=<in_place|lea_retarget>; url=<url>`.
The redirect is unit tested on synthetic images only. Whether it reaches the
game before the first gate request is verified live by fuse logging
`CMD_GET_URLLIST`.

**What other code may rely on.** `master_server::RedirectedToLoopback()` is
true only after a verified redirect whose host is exactly 127.0.0.1, localhost
or [::1]. Any lab identity substitution that could reach a master server (a
forged auth ticket SteamID, for example) must refuse unless it is true.

**Not wired yet.** Nothing in this branch calls `master_server::Install`; the
proposed call site is in the work-package report. Until it is merged, the
safety gate stops every lab start at the host (no `master_server=` line).

## Steps

The game must be closed for steps 1–2. Steam must be running and logged in.

1. **Build and test.** `pwsh -File tools/build.ps1 -Configuration Release`. All
   CTest suites must pass. The lab features need a loader that contains the lab
   modules: the observer, loopback, identity, mutex, save redirect and the
   master-server redirect call. Without them the safety gate stops the start.
2. **fuse.** `pwsh -File tools/lab/Build-Fuse.ps1`. This builds
   `work/fuse/fuse.exe` offline if it is missing.
3. **Prepare both folders.** First review with
   `pwsh -File tools/lab/Prepare-LabInstance.ps1 -WhatIf`, then run it without
   `-WhatIf`. It:
   - verifies the exe and Steam DLL hashes;
   - installs the loader as an owned install with rollback. The guest's ownership
     record then names `MGS_TPP_guest`;
   - sets the roles: `host-v3`, and `client-v1` with `client-host.txt` set to the
     host's real SteamID64 (from Steam's ActiveUser; the host's own
     `host-steamid.txt` is cross-checked at run time);
   - writes `lab.ini` and `coop.ini` (your own copies are kept as `*.user-<ts>`);
   - backs up the real saves with a hash manifest into `build/lab/save-backups/`;
   - seeds both lab save copies;
   - sets the lab graphics copies to Windowed.

   If it reports that a lab graphics file does not exist yet, set Windowed
   1280x720 once in that instance's options. That changes only the lab copy.
4. **Start.** `pwsh -File tools/lab/Start-Lab.ps1`. This runs a preflight
   (processes by path, ports, hashes, configuration including each `save_dir`
   and the guest identity), then starts fuse `-standalone` with its database in
   `build/lab/fuse`, then the host. Once the host has a window **and** has passed
   the safety gate (and its `host-steamid.txt`, if written, matches the guest's
   target), it starts the guest behind the same gate, then places both windows
   side by side. Any gate failure stops everything Start-Lab started.
5. **Smoke.** `pwsh -File tools/lab/Invoke-LabSmoke.ps1 -Assist`.
   - In each window: press a key at the title screen, choose CONTINUE, and wait
     for Mother Base.
   - Do not press Ctrl+F8. The harness creates the guest's `join.trigger` when
     the host's prerequisites (synthesis §3.5, including
     `dynamite_session=observed … state=3 accept=1 … consistent=1`), both
     instances' fuse traffic and the guest's `mode=client_v1; configured_host=1`
     and `client_join=armed` are all present.
   - Result: PASS or FAIL. Details are in `build/lab/runs/<ts>/result.json`,
     including what was still missing at a timeout. `not_under_fuse` means fuse
     never saw the logins or the redirect line was missing.
6. **Optional.** `pwsh -File tools/lab/Get-LabScreens.ps1 -Label after` saves
   screenshots. A remote actor visible on both sides is milestone M4.
7. **Stop.** `pwsh -File tools/lab/Stop-Lab.ps1`. This closes this run's games
   and fuse (never another MGSV session; see `-IncludeUnrecordedProcesses`),
   then compares the real save hashes with the run-start manifest (exit code 2
   if any real save changed).
8. **Before normal online play:** `pwsh -File tools/lab/Prepare-LabInstance.ps1 -Disable`.
   Without it, the lab `coop.ini` keeps sending the host's normal game starts to
   the local fuse (or offline when fuse is stopped), even if `lab.ini` was
   deleted. `-Disable` renames both generated files and restores your own.

## Offline lab mode: the partner spike (no server at all)

The co-op direction is now fully offline with the mod's own netcode
([offline-partner-spike-plan.md](offline-partner-spike-plan.md)). This mode
runs both instances with no fuse and no session: each `coop.ini` points the
master server at `http://127.0.0.1:47899/tppstm/gate`, a local port where
nothing listens, so the game fails its login ("Failed to log in to server") and
continues offline. The marker `offline-v1` on both folders never calls the
session APIs and never sets S_IS_ONLINE. The two instances exchange poses over
their own UDP ports 47820/47821 (`net=1`); the loopback P2P ports 47810/47811
stay configured but are not what the spike uses.

What stays the same: the safety gate (`master_server=redirected` with the
closed-port URL, `saves=redirected`, guest `mutex=renamed` and `identity=fake`),
the save backup and redirect, the guest's fake identity and mutex suffix, and
Stop-Lab's real-save check. Without the redirect line the gate still stops the
instance, because the game would otherwise contact Konami's default servers.

### Steps

1. **Build and test** as above. The loader must contain the offline partner
   code (`offline-v1` marker, partner receipts, `partner_net`); without it the
   experiments stay INCOMPLETE (no `mode=offline_v1` line).
2. **Prepare.** `pwsh -File tools/lab/Prepare-LabInstance.ps1 -Offline` (review
   with `-WhatIf` first). It writes `offline-v1` to both markers, the
   closed-port `coop.ini` (character `default` on both) and a `lab.ini` with
   the partner keys of [lab-protocol.md](lab-protocol.md) (`net=1`,
   `partner=1`, `partner_stage=0`, ...). One-off settings for a later
   experiment go through `-PartnerSetting`, for example
   `-PartnerSetting host:partner_force_exists=1,partner_offset_cm=100` (F3a) or
   `-PartnerSetting guest:net_latency_ms=100,guest:net_loss_permille=200` (E7).
   `lab.ini` is read once at process start: prepare, then start.
3. **Start.** `pwsh -File tools/lab/Start-Lab.ps1 -Offline`. The preflight
   requires no listener on TCP 47899 and free UDP 47810/47811/47820/47821,
   the `offline-v1` markers, `net=1`, `partner=1` and the role's partner ports.
   fuse is not started. `run.json` records `offline: true`.
4. **Title.** In each window press Enter through the title
   (`pwsh -File tools/lab/Send-LabInput.ps1 -Role host -Keys enter`), dismiss
   the server-failure notice, choose CONTINUE and wait for Mother Base (30050).
   To verify live: the notice appears, there is no long hang or retry loop,
   and Mother Base loads.
5. **E0 baseline.** `pwsh -File tools/lab/Test-PartnerSpike.ps1 -Experiment E0`
   (both instances). Walk a circle on each instance for the units check,
   `Send-LabInput.ps1 -Role host -Keys w+a:6000` (W and A held together for
   6 s), then wait for the next 30 s `partner_units` line. E0 needs 5 minutes
   of probes; run it again until PASS or FAIL.
6. **Experiments**, one boot, in the plan's order (E0, E1, E2, E3, E4, E5, E6, E8):

   | Experiment | Command | Judged on |
   |---|---|---|
   | E1 Warp, unrealized | `Set-PartnerTrigger.ps1 -Role host -Text 'stage=1 source=fixed apply=warp'` | host: `partner_warp_check err_mm<=5` in the first 2 s, ids[1] stays ffff, no Lua error |
   | E2 all-active alone | `Set-PartnerTrigger.ps1 -Role host -Text 'stage=2'` | host: `allactive_set`, `s0=0x03`/`q170=0x3`; outcome A (ffff for 10 s) or B (realized) |
   | E3 realization | `Set-PartnerTrigger.ps1 -Role host -Text 'stage=3'` | host: `parts_copied`, `partner_bits=set`, `partner_realized=1` within 1 s, `partner_parts=loaded` within 15 s, 120 s without `crash_probe=av` or `partner_realized=0`; screenshot by hand |
   | E4 orbit | `Set-PartnerTrigger.ps1 -Role host -Text 'stage=3 source=orbit'` | host: realized, every `err_mm<=5`, `slot_mm` moving; 4 screenshots 2 s apart |
   | E5 apply modes | `-Text 'stage=3 source=orbit apply=slot'`, later `-Text 'apply=both'` | host: both modes ran realized with `err_mm<=5`; the visual comparison is by hand |
   | E6 network | `Set-PartnerTrigger.ps1 -Role both -Text 'stage=3 source=net'`, then `Send-LabInput.ps1 -Role guest -Keys w:4000` | both: `first_rx`, rx increasing, `status=live`, `last_age_ms<200`, realized; while both stand still, the peer's `pos0_mm` within 200 mm of the local `pos1_mm` |
   | E8 teardown | `-Text stop`, then `-Text 'stage=3'`, then leave and re-enter Mother Base | host: `partner_bits=cleared; reason=trigger`, `allactive_cleared`, `partner_realized=0`, realized again, `partner_stop`, `partner_cfg=ready` again |

   After each one: `pwsh -File tools/lab/Test-PartnerSpike.ps1 -Experiment Ex`
   (add `-Role guest` to judge the guest). `Invoke-PartnerExperiment.ps1
   -Experiment Ex` does the trigger, input and screenshots of the table and polls
   the verdict until PASS or FAIL. `Get-LabScreens.ps1 -Label <name>` saves
   screenshots at any time.
7. **Stop.** `pwsh -File tools/lab/Stop-Lab.ps1` closes the games, deletes any
   unconsumed `partner.trigger` and checks the real saves. Afterwards either
   `Prepare-LabInstance.ps1` (back to the fuse mode) or
   `Prepare-LabInstance.ps1 -Disable` (before normal online play).

### Verdicts

`Test-PartnerSpike.ps1` prints one JSON object: `experiment`, `role`,
`verdict`, `matched` (the evidence lines), `missing`, `failed`, `notes` and
`per_role`. Exit code 0 is PASS, 1 FAIL, 2 INCOMPLETE.

- **PASS**: every log criterion of the plan's section 3 holds. Criteria that
  need a screenshot are listed in `notes` as manual.
- **FAIL**: a line shows a criterion is not met (for example `online_bit=1`,
  `count=1`, `crash_probe=av`, `err_mm>5`, `partner_realized=0`,
  `partner_parts=timeout`), or a deadline passed on the probe clock (`t_ms`)
  without the evidence. The message names the plan's next step (F3a, F3b, ...).
- **INCOMPLETE**: evidence is still missing; `missing` says what. Run it again.

Each experiment is judged on the part of the log in which its configuration was
active, from the `partner=configured` or `partner_stage=changed` line that
started it until the next change that ends it, so E1 to E5 can follow each other
in one boot. Lines from before `Start-Lab.ps1` recorded the log offsets never
count. Liveness of the instances is part of the verdict while the run is up and
is not checked after Stop-Lab.

### What this mode cannot show

Everything above is log evidence from one PC. Whether the partner is visible,
animated and in the right place is judged from screenshots by hand. The native
partner code, the Lua splice and the offline title flow need live verification;
the scripts' parsing and rules are unit tested against fixture logs written
with the plan's section 2.7 spellings (`tests/fixtures/partner-lab`).

## Milestones

| Milestone | How to check | Evidence |
|---|---|---|
| M0 | `Start-Lab.ps1 -HostOnly`, then `Invoke-LabSmoke.ps1 -Assist` | Smoke PASS `host_ready_stable`, which requires the §3.5 host prerequisites (`state=3 accept=1 … consistent=1`), `master_server=redirected` to the lab URL and fuse traffic (`CMD_GET_URLLIST` and `CMD_AUTH_STEAMTICKET`, `fuse_counts` in `result.json`). Without fuse evidence it fails `not_under_fuse` |
| M1 | Full start | The guest reaches Mother Base next to the host. `early.log` shows `mutex=renamed`, `identity=fake` and `saves=redirected` |
| M2 | The smoke creates the trigger | Guest `dynamite_client=returned; request_started=1`. `loopback.log` shows guest send, host `session_request_posted`, then `accepted` |
| M3 | Smoke PASS | Guest `client_established` and host `host_peer_present` for two polls |
| M4 | Screenshots | The remote actor is visible on both sides |

## Troubleshooting

- **Preflight failed.** Each problem is listed. Usually the cause is that
  `Prepare-LabInstance.ps1` was not run, the game is still running from that
  folder, or a port is in use.
- **Safety gate failed.** The message names the reason. `unsafe_<reason>`
  means a log line showed a rule not in force (the instance was terminated at
  once); `safety_evidence_missing_after_…(…)` lists the lines that never
  appeared, usually because the loader lacks the sibling modules or the
  `master_server::Install` call. `host_id_mismatch` means Steam is logged into a
  different account than the one Prepare resolved: log into the host's account
  and re-run Prepare.
- **The guest exits immediately.** Check `MGS_TPP_guest\mgs-coop\early.log`. If
  it shows `mutex=unchanged`, the guest is not running the lab loader or
  `lab.ini` is not enabled.
- **A window stops rendering while unfocused.** It is not in Windowed mode. Set
  Windowed once in that instance's options.
- **The smoke times out at `waiting_host_ready`.** Check `host-script-observer.log`
  in the run folder for `dynamite_host=` and `host_admission=`.
- **fuse logged nothing (`not_under_fuse`).** The redirect did not run before
  the first gate request. Check `master_server=` in `steam-observer.log`.
- **A real save changed (Stop-Lab exit 2).** Restore it from
  `build/lab/save-backups/<ts>/files/`. Then check `early.log` for
  `saves=redirected`.

## Earlier options (background)

The alternatives were analysed in
[analysis-2026-10-03/synthesis.md](analysis-2026-10-03/synthesis.md) §3:

- Goldberg/gbe_fork in both folders, kept as a differential cross-check.
- Sandboxie with a second licensed Steam account.
- A VM. The 2026-09-10 inventory found VirtualBox 7.2.4, but MGSV under its
  virtual DX11 GPU is unproven.

The in-mod loopback harness above keeps the shipped `steam_api64.dll` and the
loader's hash gates. Real-account testing remains the final step.
