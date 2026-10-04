# Single-PC testing: host and guest on one machine

This is how to run a host instance and a guest instance of MGSV on one PC and
try a co-op join with a log-only pass/fail result. Script reference:
[tools/lab/README.md](../tools/lab/README.md). The file and log contract
between the scripts and the loader: [lab-protocol.md](lab-protocol.md).

## What a single-PC run can and cannot show

- **It can show** that the mod's session logic works end to end: host
  creation, guest join request, session establishment on both sides, and the
  mod's own state machine and logs.
- **It cannot show** anything about Steam networking. Traffic between the two
  instances goes over 127.0.0.1 through the loader's loopback emulation of
  ISteamNetworking, and the guest uses a fake SteamID
  (`76561202255233008`, account `0xFFFFFFF0`) that never reaches Steam. Steam
  NAT traversal, relays, latency, loss and real-account behaviour are not
  exercised.
- **Real-network sign-off.** A run on two PCs or two Steam accounts is still
  required before anything is called working.

## How it works

| Problem on one PC | Lab answer (loader side, gated on `mgs-coop/lab.ini`) |
|---|---|
| The second instance exits on the single-instance mutex | The guest appends `mutex_suffix=-guest` to the mutex name |
| Both instances have the same SteamID | The guest reports a fake identity; P2P is emulated over UDP 47810/47811 (`loopback=1`) |
| Both instances share saves | Each instance's saves are redirected to a copy under `<game>\mgs-coop\lab\saves` |
| Konami login with a fake identity, and ban risk | Both use a local fuse master server: `coop.ini master_url=http://127.0.0.1:6667/tppstm/gate` |
| Fullscreen and borderless windows pause when unfocused | The lab save copies are set to Windowed 1280x720 |
| One gamepad drives both instances | `xinput=none` on the host and `xinput=0` on the guest. The host uses keyboard and mouse |
| The Ctrl+F8 join needs focus and fires once | The harness creates `mgs-coop/join.trigger` (`join_trigger=1`) |

With no `lab.ini` the loader behaves exactly as before. Without a `coop.ini`
`master_url`, the game keeps Konami's URL.

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

## Steps

The game must be closed for steps 1–2. Steam must be running and logged in.

1. **Build and test.** `pwsh -File tools/build.ps1 -Configuration Release`. All
   CTest suites must pass. The lab features need a loader that contains the lab
   modules: the observer, loopback, identity, mutex and save redirect.
2. **fuse.** `pwsh -File tools/lab/Build-Fuse.ps1`. This builds
   `work/fuse/fuse.exe` offline if it is missing.
3. **Prepare both folders.** First review with
   `pwsh -File tools/lab/Prepare-LabInstance.ps1 -WhatIf`, then run it without
   `-WhatIf`. It:
   - verifies the exe and Steam DLL hashes;
   - installs the loader as an owned install with rollback. The guest's ownership
     record then names `MGS_TPP_guest`;
   - sets the roles: `host-v3`, and `client-v1` with `client-host.txt` set to the
     host's real SteamID64 (from Steam's ActiveUser);
   - writes `lab.ini` and `coop.ini`;
   - backs up the real saves with a hash manifest into `build/lab/save-backups/`;
   - seeds both lab save copies;
   - sets the lab graphics copies to Windowed.

   If it reports that a lab graphics file does not exist yet, set Windowed
   1280x720 once in that instance's options. That changes only the lab copy.
4. **Start.** `pwsh -File tools/lab/Start-Lab.ps1`. This runs a preflight
   (processes by path, ports, hashes, configuration), then starts fuse
   `-standalone` with its database in `build/lab/fuse`, then the host. Once the
   host has a window it starts the guest, then places both windows side by side.
5. **Smoke.** `pwsh -File tools/lab/Invoke-LabSmoke.ps1 -Assist`.
   - In each window: press a key at the title screen, choose CONTINUE, and wait
     for Mother Base.
   - Do not press Ctrl+F8. The harness creates the guest's `join.trigger` when
     the host logs `session_status=host_ready` and the guest logs
     `client_join=armed`.
   - Result: PASS or FAIL. Details are in `build/lab/runs/<ts>/result.json`.
6. **Optional.** `pwsh -File tools/lab/Get-LabScreens.ps1 -Label after` saves
   screenshots. A remote actor visible on both sides is milestone M4.
7. **Stop.** `pwsh -File tools/lab/Stop-Lab.ps1`. This closes both games and
   fuse, then compares the real save hashes with the run-start manifest
   (exit code 2 if any real save changed).
8. **Before normal online play:** `pwsh -File tools/lab/Prepare-LabInstance.ps1 -Disable`.
   Without it, `coop.ini` keeps redirecting the host to a local fuse that is not
   running.

## Milestones

| Milestone | How to check | Evidence |
|---|---|---|
| M0 | `Start-Lab.ps1 -HostOnly`, then `Invoke-LabSmoke.ps1 -Assist` | Host creation still reaches `host_ready` under fuse. fuse logs `CMD_GET_URLLIST` and `CMD_AUTH_STEAMTICKET` (`fuse_messages` in `result.json`). `steam-observer.log` shows `master_server=redirected` |
| M1 | Full start | The guest reaches Mother Base next to the host. `early.log` shows `mutex=renamed`, `identity=fake` and `saves=redirected` |
| M2 | The smoke creates the trigger | Guest `dynamite_client=returned; request_started=1`. `loopback.log` shows guest send, host `session_request_posted`, then `accepted` |
| M3 | Smoke PASS | Guest `client_established` and host `host_peer_present` for two polls |
| M4 | Screenshots | The remote actor is visible on both sides |

## Troubleshooting

- **Preflight failed.** Each problem is listed. Usually the cause is that
  `Prepare-LabInstance.ps1` was not run, the game is still running from that
  folder, or a port is in use.
- **The guest exits immediately.** Check `MGS_TPP_guest\mgs-coop\early.log`. If
  it shows `mutex=unchanged`, the guest is not running the lab loader or
  `lab.ini` is not enabled.
- **A window stops rendering while unfocused.** It is not in Windowed mode. Set
  Windowed once in that instance's options.
- **The smoke times out at `waiting_host_ready`.** Check `host-script-observer.log`
  in the run folder for `dynamite_host=` and `host_admission=`.
- **fuse logged nothing.** The redirect did not run before the first gate
  request. Check `master_server=` in `steam-observer.log`.
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
