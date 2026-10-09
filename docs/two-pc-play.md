# Two-PC play

Two people on two PCs play the offline co-op of the single-PC lab: each game
runs offline (the `offline-v1` marker and the dead-port `master_url`), plays
from a save COPY, and shows the other player as the partner in Mother Base
or Afghanistan/Africa free roam (30050/30010/30020). Shared world level 3 is
the default, including host time/weather and supported travel. Use
`Play-Coop.bat -World 0` for partner movement only. The PCs talk over link v2
(PART N; see `docs/link-protocol.md`), either **online through Steam**
(`ISteamNetworkingMessages002` over Valve's relays: no port forwarding, VPN or
extra program; [steam-online.md](steam-online.md), section "Online (Steam)"
below) or over our own UDP socket for **LAN/VPN** play. Never over Konami's
servers or the game's own Steam P2P.

The player-facing guide is `tools/friend/FRIEND-GUIDE.txt` (shipped in the
friend ZIP). This page describes the tooling (PART T) and the files it relies
on.

F9 opens the in-game debug menu during an offline copied-save session. The
new source Enemy controls select an authored camp and patrol route, teleport
to a readable guard or validated player checkpoint, and let the host reuse
up to eight nearby normal living guards per mission. Placement is local;
new soldier allocation and replicated AI are not provided. Unloaded or
unreadable guards refuse placement. Follow the [menu instructions](../README.md)
for the controls and their pending live validation.

## Package

`pwsh -File tools/package.ps1 -Friend` stages `dist/mgs-coop-friend-<version>-<stamp>/`
and its ZIP (`package.json` flavor `friend-play`):

| file | purpose |
|---|---|
| `Install-Coop.bat` | install once: loader, save backup, save copy; leaves the game in pass-through |
| `Play-Coop.bat` | every game: Host online / Join online (Steam) or LAN/VPN Host/Join, then MGSV starts through Steam with co-op on |
| `Uninstall-Coop.bat` | switch a leftover session off, restore the previous loader, check the real saves |
| `Collect-Logs.bat` | logs ZIP on the Desktop (redacted) |
| `scripts/` | the `.ps1` scripts, `FriendCommon.psm1`, `CoopPlay.psm1`, `install.ps1` |
| `FRIEND-GUIDE.txt`, `RUNTIME-ARCHIVE.txt` | player documentation |

The ZIP never contains the runtime archive (`mgs-coop-player-runtime.dat`, see
`RUNTIME-ARCHIVE.txt`), a `link-secret.txt`, `LabCommon.psm1` or
`set-session-mode.ps1` as scripts (`source/` keeps the full source tree).
Every script runs under Windows PowerShell 5.1, ASCII with CRLF.

## Install: pass-through

`Install-Coop.ps1` keeps the old installer's machinery (discovery, hash gates,
save backup with manifest, `Copy-SaveSeed` into `mgs-coop\coop-saves`, the
owned install through `install.ps1 -NoInitialMode`, the `friend-install.json`
state machine with rollback, `previous_files`) and drops the SteamID question,
`set-session-mode.ps1`, `client-host.txt` and the online markers. Its end
state is **pass-through**: `dinput8.dll` and `mgs-coop\` are installed, but
there is no marker, `lab.ini`, `coop.ini` or `link-secret.txt`, so a normal
start from Steam is plain MGSV on the real saves.

- `-Role Host|Join` only picks the default character (Host `default`, Join
  `quiet`), stored as `role_hint` and `character`.
- Upgrade from the online friend-test package: a `lab.ini`/`coop.ini` with the
  old friend header, `client-host.txt` and a `host-v3`/`client-v1` marker are
  renamed `*.online-old-<time>`, recorded in `online_archived` and **never
  restored**. Any other configuration file is renamed `*.disabled-<time>`
  (`set_aside`); the `.before-friend-*` copies (`previous_files`) keep their
  restore semantics in Uninstall.
- A read-only firewall and network-profile report (`Get-NetFirewallApplicationFilter`,
  `Get-NetConnectionProfile`) explains Block rules and Public networks;
  nothing is changed.

## Play-Coop

`Play-Coop.ps1` (no code parameter: the join code is read with `Read-Host`):

1. **Preflight**: install status `installed`; loader hash and ownership
   record (version in `$script:SupportedLoaderVersions`); exe, `steam_api64.dll`
   and runtime archive hashes from `package.json`; `coop-saves` present and
   short enough; no reparse points; MGSV not running from the folder; an
   active session whose launcher (pid and start time) or watchdog still runs
   stops it ("Another Play-Coop window is active"; Install and Uninstall
   check the same), a leftover one is cleaned up first; a `lab.ini`/`coop.ini`, marker
   or `link-secret.txt` that this session does not own stops it (nothing is
   overwritten); a warning when Steam's MGSV folder is another one.
2. **Host**: a fresh 100-bit secret; one join code per address (LAN,
   Tailscale 100.64/10, ZeroTier adapters, Radmin 26/8, or `-HostAddress`).
   Optional, each default No: `[P]` public IP lookup (contacts api.ipify.org),
   `[U]` UPnP mapping (background job, 15 s timeout, recorded before the
   router is asked, removed at the end), `[F]` firewall rule (elevated,
   `-EncodedCommand`, `-Profile Any`, the exe's Block rules listed first and
   removed on request; one rule named `mgs-coop play`, replaced by a later
   `[F]` on another port; recorded in `friend-install.json.firewall_rule`). The
   chosen code is copied to the clipboard; the clipboard is cleared at the end
   if it still holds the code.
3. **Join**: paste the code; status-specific sentences for typos; a host name
   is resolved before the launch (first IPv4); the dotted IPv4 goes to
   `lab.ini`.
4. **Activate** (per-folder mutex `Local\mgs-coop-play-<SHA-256 prefix>`), each
   file recorded with its SHA-256 in `play-session.json` *before* it is
   written: `lab.ini` (UTF-8, LF), `link-secret.txt` (ASCII, S + CRLF),
   `coop.ini` (UTF-8, LF), the marker `offline-v1\n` (after the ownership,
   loader-hash and archive-hash checks of `set-session-mode.ps1`). Then a
   hidden watchdog starts (`Play-Coop.ps1 -Watch <session>`, created through
   `Win32_Process.Create`, so it survives closing the console window).
5. **Launch**: the lengths of `early.log`, `steam-observer.log` and
   `script-observer.log` are recorded, then `steam://rungameid/287700`. The
   launcher waits for MGSV from this folder without a timeout (`[Q]` aborts and
   cleans up), then requires within 90 s, in the new bytes only:
   `saves=redirected; dir=<coop-saves>`, `play=1; ... refused=0x0; saves=redirected`,
   `master_server=redirected; ... url=http://127.0.0.1:47899/tppstm/gate` and
   `play=active`. `play=refused; reason=saves_not_redirected` or no
   `saves=redirected` shows a red banner (normal offline play on the REAL
   saves); a missing `play=` line with redirected saves means the loader is too
   old. Then one status line per second from `link-status.json` (pid checked,
   older than 15 s = "game not responding", outside 30050/30010/30020 =
   "Partner body unavailable in this mission").
6. **Deactivate** (game exit, `[Q]` before the launch, Ctrl+C, the watchdog,
   the next Play-Coop, Install, Uninstall, `Play-Coop.ps1 -Cleanup`): marker,
   `coop.ini`, `lab.ini`, `link-secret.txt`, each deleted only while its hash
   matches (the marker only when it is exactly `offline-v1\n`); then the UPnP
   mapping and the clipboard; `state=off`. A changed file is kept and named.
   The launcher never deletes files under a running game: closing it while
   MGSV runs leaves the cleanup to the watchdog at game exit. `[Q]` in the
   status loop is refused while no live watchdog exists; a launcher loop that
   ends early with the game running restarts the watchdog once, else waits for
   the game to exit and cleans up itself.

Play `lab.ini` (host; the guest has `role=guest`, `net_listen_port=0`,
`net_peer_addr=<IPv4>`, `net_peer_port=<port>`):

```
# Generated by mgs-coop Play-Coop.ps1 (two-PC play); removed when the game exits.
enabled=1
mode=play
role=host
save_dir=<game>\mgs-coop\coop-saves
net=1
net_bind=any
net_listen_port=47830
partner_interp=auto
<Get-PlayPartnerDefaults: Play-OfflineCoop's effective dm=map partner keys under the shipped lab.json>
```

Never written in play: `loopback`, `self_steamid`, `peer_steamid`,
`self_name`, `mutex_suffix`, `xinput`, `join_trigger`, `auto_join_delay_s`,
or an impairment key.

### World sync (experimental, enabled by default)

On **both PCs**, open Command Prompt in the extracted friend-package folder
and run:

```bat
Play-Coop.bat -World 3
```

Choose Host on one PC and Join on the other. Both copies must first enter
the same supported scene (Mother Base, Afghanistan/Africa free roam, or its
corresponding ACC) so they can pair; different saved scenes do not bootstrap
automatically. After pairing, the host controls supported travel and recovery.
The launcher keeps the existing co-op save copy between sessions.

`Play-Coop.ps1 -World 1..3` adds `world=N` to the play `lab.ini` (default 3;
explicit `-World 0` writes no world key and shares partner movement): 1 plumbing only, 2 shared enemy
outcomes and alert in free roam (30010/30020), 3 also clock/weather, host-driven
supported-scene travel, reload alignment, shared death and checkpoint copying
([world-sync-protocol.md](world-sync-protocol.md)). The effective level is
the lower of the two (a player at 0 never pairs).
It needs the redirected co-op save (it writes kills, staff and clears there)
and never runs on the real saves. At negotiated level 3, the host selects
travel and Continue/restart/abort; the guest follows through vanilla Lua APIs.
Only 30010/30020/30050 and their ACC scenes are supported. Enemy sync resumes
after a complete matching host alignment snapshot; unreadable areas remain
unknown until discovered, and terminal guest soldiers are never revived.
Lower levels retain manual travel and the Continue gate. Story missions and
full progression copying remain unsupported. Lab recovery probes and the
goto trigger are refused in play. See protocol section13 for current limits
and separate live acceptance requirements. Recorded live verification uses
two game instances on one PC; actual two-PC play and full campaign progression
have not been validated.

## Online (Steam)

The recommended way since the Steam transport ([steam-online.md](steam-online.md)).
Play-Coop's menu: `[O] Host online (Steam, recommended)`, `[J] Join online`,
`[L] LAN/VPN (UDP code)` (the MGS1 flow above, byte-identical) and `[Q] Quit`;
`-Role Host|Join -Transport steam [-Code <MGS2>]` skips it (`-Role` alone keeps
LAN/VPN).

- **Preflight**: Steam must run and be logged in: the registry `ActiveUser`
  gives this PC's SteamID64 (76561197960265728 + ActiveUser), else "Start
  Steam and log in" and nothing is written.
- **Host online**: a fresh secret, the code
  `MGS2-SSSSS-SSSSS-SSSSS-SSSSS-CC@<SteamID64>` shown once and copied to the
  clipboard (cleared at exit as before). No UPnP, public-IP or firewall step.
  The text points to the in-game invite: F9 > Partner / Co-op > Online >
  Invite a friend.
- **Join online**: paste an MGS2 code (an MGS1 code falls through to the
  LAN/VPN join), or press Enter to wait for the host's Steam invite. A code
  whose SteamID is this PC's own is refused ("That is your own code"). A
  code writes `net_peer_steam` and `link-secret.txt`; waiting writes
  `net_steam_wait=1` and no secret (the 337 invite or an F9 paste arms the
  guest in the game). Start Play-Coop *before* accepting an invite: an
  invite accepted while MGSV is not running from Play-Coop starts plain MGSV
  on the real saves (the S3 command-line launch needs a user decision).
- **Launch evidence**: the UDP lines plus `link=config; mode=play; role=...;
  proto=2; bind=steam; port=0;`; a `link=disabled; reason=steam_*` line is
  shown with its sentence (`Get-CoopLinkReasonText`).
- **Status line**: `[HOST] link=connected via Steam (relay) rtt=61ms ...`;
  a waiting guest shows `link=waiting via Steam - waiting for an invite or a
  code (F9 -> Partner / Co-op -> Online)`.

Online `lab.ini` (host; the guest adds `net_peer_steam=<host SteamID64>` or
`net_steam_wait=1`):

```
# Generated by mgs-coop Play-Coop.ps1 (two-PC play); removed when the game exits.
enabled=1
mode=play
role=host
save_dir=<game>\mgs-coop\coop-saves
net=1
net_transport=steam
net_steam_self=<this PC's SteamID64>
partner_interp=auto
<Get-PlayPartnerDefaults>
world=3
```

In the game, F9 > Partner / Co-op > **Online** shows the Steam state, link,
route (relay/direct) and RTT and the partner's persona name. The host has
"Accept joins", "Copy join code" (masked unless "Reveal" is held) and the
"Invite a friend" list (friends playing MGSV first; `InviteUserToGame` at most
once per friend per 10 s); the guest has a code box and "Join". The UI only
posts requests; the loader's owner context makes the Steam calls.

## Join code

`MGS1-SSSSS-SSSSS-SSSSS-SSSSS-CC@<host>:<port>`: S is 20 Crockford base32
characters (100 bits), CC the check over `"MGS1|" + S + "|" + host + "|" + port`
(SHA-256, first 10 bits), host a canonical dotted IPv4 or lower-case DNS name,
port 1024..65535 (default 47830). Parsing tolerates case, blanks and dashes
and reads O as 0 and I/L as 1; statuses `ok, bad_prefix, bad_char,
bad_length, bad_check, bad_host, bad_port, ipv6_unsupported, empty`
(`ConvertFrom-CoopJoinCode` in `tools/CoopPlay.psm1`; vectors in
`tests/friend_package_tests.ps1`). The key does not depend on the address:
the host shows one code per address, all with the same secret.

## Logs

`Collect-Logs.ps1` adds `link-status.json` and `play-session.json` to its
allow-list and never collects `link-secret.txt` or `steam-join.ini`. Every
collected text is redacted: join codes
(`(?i)MGS[12][\s-]*(?:[0-9A-Z][\s-]*){22}(?:@\S+)?`), invite strings
(`\+mgscoop_join\s+\S+`), public
IPv4 addresses after `peer=`, `net_peer_addr=`, `"peer":"` and `host=`
(private, CGNAT/Tailscale and Radmin ranges stay readable; version numbers
such as 1.0.15.4 are untouched), SteamIDs, account folders and the Windows
user name.

## One-PC rehearsal (lab)

`Play-OfflineCoop.ps1 -Lan [-LanAddress <ipv4>] [-WanSim good|bad|awful]`
(also on `Prepare-LabInstance.ps1 -Offline` and `Start-Lab.ps1 -Offline -Lan`):

- `-Lan`: both copies `net_bind=any` (ports 47820/47821 unchanged), the guest
  `net_peer_addr=<LAN IPv4>` (default: the first IPv4 of an Up adapter with a
  default gateway, else "No LAN IPv4 address found; pass -LanAddress."), a
  random `link-secret.txt` in both copies (the lab secret only works on
  127.0.0.1). Prepare without `-Lan` and `-Disable` delete `link-secret.txt`.
  Start-Lab checks the partner ports with an exclusive 0.0.0.0 bind and needs
  the secret in both copies. The scripts only mention the firewall.
- `-WanSim`: the `lab.json` `wanSim` preset (one-way per copy), `net_seed`
  1/2 and `partner_interp=auto`.
- Without these switches every generated file is byte-identical to before.
  `-PartnerSetting net_link=legacy` selects the raw-frame link for bisecting.

## Open points for the live tests

Online (Steam): every item of [steam-online.md](steam-online.md) section 15
(code join between non-friends through the 2015 steam_api dispatcher, the
invite with a waiting guest, the beacon path, relay and direct routes, RTT
and `interp=auto`, Wi-Fi drop and recovery, Steam offline, a wrong code, the
firewall prompt, a 30-minute world-sync session, a clean exit on both PCs).

LAN/VPN: see the open-risks list of the link spec: Windows Firewall prompts and Block
rules, CGNAT without a VPN, the save gate timing, Steam dialogs delaying the
launch, the watchdog surviving a closed console, clipboard history. After the
install, normal launches use the real saves with `dinput8.dll` loaded in
pass-through, and the co-op save copy drifts from the real saves; the guide
recommends Uninstall when the players stop playing co-op.
