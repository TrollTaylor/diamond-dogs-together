# Co-op lab protocol (single-PC host + guest)

This is the contract between the native loader components and the `tools/lab/`
scripts. Native code writes these files and lines; the scripts only read them,
except for trigger files, which the scripts create and native code consumes.
Every path below is relative to `<exeDir>/mgs-coop/`. `<exeDir>` is the folder
of the running `mgsvtpp.exe`: `MGS_TPP` for the host, `MGS_TPP_guest` for the guest.

## Configuration inputs

| File | Owner | Purpose |
|---|---|---|
| `lab.ini` | lab scripts | Harness only. Parsed by `native/lab_config.cpp` (`mgscoop::GetLabConfig()`). Inert unless `enabled=1` and `role=` are both present. |
| `coop.ini` | installer / user | Player options: `master_url=`, `character=`, `character_fallback=` (`mgscoop::GetCoopConfig()`). |
| `script-observe.enabled` | existing | Mode marker: `host-v3`, `client-v1` or `client-check-v1`; `offline-v1` on both folders in the offline lab mode (below). |
| `client-host.txt` | existing | Guest only: the host's SteamID64 in decimal. |

`lab.ini` keys: `enabled`, `role` (`host`/`guest`), `loopback`, `self_steamid`
(`real` or a decimal SteamID64), `self_name`, `peer_steamid` (`learn` or decimal),
`listen_port`, `peer_port`, `mutex_suffix`, `save_dir` (absolute), `xinput`
(`passthrough`/`none`/`0`-`3`), `join_trigger`, `auto_join_delay_s`,
`net_latency_ms`, `net_loss_permille`, `trace_packets`. See
`native/include/mgscoop/lab_config.hpp` for types and validation.

Offline partner spike keys ([offline-partner-spike-plan.md](offline-partner-spike-plan.md)
2.1). The scripts write them, with exactly these names and in this order, only in
the offline lab mode (`Prepare-LabInstance.ps1 -Offline`); without `-Offline` the
generated `lab.ini` is byte-for-byte what it was before.

| Key | Values (lab default) | Validation in the scripts (`LabCommon.psm1` `ConvertTo-LabPartnerIni`) |
|---|---|---|
| `net` | `0`/`1` (`1`) | `net=1` needs both net ports |
| `net_listen_port`, `net_peer_port` | host 47820/47821, guest 47821/47820 (`lab.json` `ports.netHost`/`netGuest`) | 1024..65535; differ from each other and from `listen_port`/`peer_port` |
| `partner` | `0`/`1` (`1`) | |
| `partner_stage` | `0`..`3` (`0`) | |
| `partner_source` | `fixed`/`orbit`/`net` (`fixed`) | lower case |
| `partner_apply` | `warp`/`slot`/`both` (`warp`) | lower case |
| `partner_rate_hz` | `1`..`60` (`30`) | |
| `partner_interp_ms` | `0`..`500` (`100`) | |
| `partner_offset_cm` | `50`..`1000` (`300`) | |
| `partner_teardown` | `clear`/`keep` (`clear`) | |
| `partner_force_exists` | `0`/`1` (`0`) | |
| `partner_policy_relax` | `0`/`1` (`0`) | last resort |
| `partner_anim_probe` | `0`/`1` (`0`) | |
| `crash_probe` | `0`/`1` (`1`) | |
| `net_latency_ms`, `net_loss_permille` | existing keys, written only when set (`-PartnerSetting guest:net_latency_ms=100`) | 0..2000, 0..1000 |

The lab defaults come from `lab.json` `"partner"` (short names such as `stage`, or
the key names) and `Prepare-LabInstance.ps1 -Offline -PartnerSetting key=value`,
`host:key=value` or `guest:key=value` (the ports stay in `lab.json`).

Lab defaults used by the scripts:

| Setting | Host | Guest |
|---|---|---|
| Folder | `MGS_TPP` | `MGS_TPP_guest` |
| `self_steamid` | `real` | `76561202255233008` (account `0xFFFFFFF0`, not a real account) |
| `listen_port` / `peer_port` | 47810 / 47811 | 47811 / 47810 |
| `mutex_suffix` | (none) | `-guest` |
| `save_dir` | `<exeDir>\mgs-coop\lab\saves` | same pattern |
| fuse | `http://127.0.0.1:6667/tppstm/gate` | same |
| Offline mode: marker | `offline-v1` | `offline-v1` |
| Offline mode: `master_url` | `http://127.0.0.1:47899/tppstm/gate` (closed port, `lab.json` `offline.masterPort`) | same |
| Offline mode: `net_listen_port` / `net_peer_port` | 47820 / 47821 | 47821 / 47820 |

## Status outputs

### `lab-status.json` (atomic rewrite on every change)

The observer writes this file whenever any field changes. It writes a `.tmp`
file and then calls `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)`. A reader
must tolerate the file being absent or briefly stale.

```json
{
  "version": 1,
  "pid": 1234,
  "role": "host" | "guest" | "none",
  "mode": "host_v3" | "client_v1" | "client_check_v1" | "off",
  "mission": 30050,
  "session_status": "none" | "host_ready" | "host_peer_present" | "client_requested" | "client_established" | "session_error" | "closed",
  "members": 0,
  "local_index": -1,
  "join": "idle" | "armed" | "requested" | "refused" | "failed" | "established",
  "join_attempts": 0,
  "last_refusal": "",
  "updated_tick_ms": 0
}
```

### `script-observer.log` lines (append-only; existing log)

The scripts match these prefixes exactly. Values never contain spaces;
`;` separates fields.

| Line | Meaning |
|---|---|
| `session_status=<value>` | Logged only when the status changes (same vocabulary as the JSON). |
| `client_join=armed; source=<keyboard\|trigger_file\|auto>` | The guest can now be triggered. |
| `client_join=input_received; key=<ctrl_f8\|trigger_file\|auto>` | A trigger was consumed. |
| `dynamite_client=returned; request_started=<0\|1>` | The existing native result line. |
| `client_join=reset; reason=<...>` | The client session was released and re-armed (attempts may repeat). |
| `client_join=refused; reason=<...>` | For example `placeholder_host_id` or `not_ready`. |

### `host-steamid.txt`

The host writes its own SteamID64 (decimal, then a newline) once its
identity is known. The scripts copy it into the guest's `client-host.txt` and
the guest's `lab.ini` `peer_steamid=`.

### `early.log` (written during DllMain installation; append-only)

| Line | Meaning |
|---|---|
| `early=start; version=<v>; lab=<0\|1>; role=<...>` | |
| `mutex=renamed; suffix=<s>` or `mutex=unchanged` | |
| `identity=fake; steamid=<id>` or `identity=real` | |
| `saves=redirected; dir=<path>` or `saves=unchanged` | |
| `xinput=<passthrough\|none\|pad N>` | |

### `loopback.log` (Steam P2P emulation; append-only, bounded)

| Line | Meaning |
|---|---|
| `loopback=listening; port=<p>; peer_port=<p>; self=<id>` | |
| `loopback=hello; peer=<id>` | First datagram from the peer. |
| `loopback=session_request_posted; peer=<id>` | Synthetic `P2PSessionRequest_t` (1202) was queued. |
| `loopback=accepted; peer=<id>` | The game called `AcceptP2PSessionWithUser`. |
| `loopback=connect_fail; peer=<id>; error=<n>` | Synthetic 1203. |
| `loopback=stats; tx=<n>; rx=<n>; dropped=<n>` | Periodic, at most once every 10 s. |

## Offline lab mode (offline partner spike)

[offline-partner-spike-plan.md](offline-partner-spike-plan.md) sections 2.5 and 4.
There is no fuse and no session: `coop.ini` `master_url` points at a closed
127.0.0.1 port, so the game reaches no server, and the marker `offline-v1` never
calls the session APIs or sets S_IS_ONLINE. `Start-Lab.ps1 -Offline` requires
that nothing listens on that TCP port and that the partner UDP ports are free,
and keeps the safety gate unchanged (`master_server=redirected` with the
closed-port URL, `saves=redirected`, guest `mutex=renamed` and `identity=fake`).
`run.json` then records `offline: true`, `offline_master_url`,
`ports.offline_master`, `ports.net_host`, `ports.net_guest` and, per instance,
`partner` (the partner/net `lab.ini` values the instance booted with).
`Invoke-LabSmoke.ps1` refuses such runs; `Test-PartnerSpike.ps1` judges them
(E0 never passes on a host-only run: it needs both instances). The offline
`coop.ini` takes `character=` from `lab.json` `offline.characters` (`default`
on both); the fuse lab keeps the top-level `characters` (guest `quiet`).
`Prepare-LabInstance.ps1 -Disable` resets an `offline-v1` marker to the
folder's online lab role (`host-v3` / `client-v1`), because `offline-v1` skips
session creation and normal play could not host or join with it.

### `partner.trigger` (scripts create, native consumes and deletes)

One ASCII line, at most 63 bytes with its newline, written by
`Set-PartnerTrigger.ps1` as a temporary file renamed over
`mgs-coop/partner.trigger`:

```text
stop
stage=<0..3> source=<fixed|orbit|net> apply=<warp|slot|both> parts=explicit mirror=on   (any non-empty subset, each key once)
```

`parts=explicit` (one-shot op 16, `SetPartsInfoAtInstanceIndex`: variant F3b)
and `mirror=on` (one-shot op 32, the mirror diagnostic: F3c, at most 10 s) are
the native parser's one-shot tokens (`native/partner_receipt.cpp`
`ParseTrigger`). They take effect only at stage 1 or higher and are not part of
the `partner_stage=changed` line; Lua logs `partner_lua=event=parts_set_explicit`
and `event=mirror_on` when it runs them.

The scripts validate the grammar (lower case, blanks between tokens, `stop` only
alone) and write the canonical order `stage source apply parts mirror` (the
longest line is 61 bytes with its newline). Native code polls the file every 30
Lua ticks, only with `offline-v1` and `lab.ini` `partner=1`. It deletes the file
whether it accepts it or not, then logs `partner_stage=changed; stage=..;
source=..; apply=..; via=trigger` (the full configuration after the trigger;
`stop` sets stage 0) or `partner_trigger=rejected; reason=parse`.
`Set-PartnerTrigger.ps1 -WaitSec` therefore confirms the result in
`script-observer.log` (lines after the length recorded before the write): exit
0 applied, 2 not consumed, 3 rejected or a different command applied, 4 no
result line within 3 s. It never replaces a pending trigger without `-Force`,
and `Invoke-PartnerExperiment.ps1` never passes `-Force` (it waits for a
pending trigger instead). The file is not gated on `join_trigger`.
`Start-Lab.ps1 -Offline` removes a leftover file before the start and
`Stop-Lab.ps1` removes leftovers after closing the games (`stop.json`
`partner_triggers_removed`). Each script-written trigger is appended to
`<run>/partner-triggers.log` (UTC time, role, text).

### `script-observer.log` lines the partner checks match

`Test-PartnerSpike.ps1` (`tools/lab/PartnerSpike.psm1`) matches the section 2.7
spellings at the start of a line. The text before the first `;` is the line's
kind; every `key=value` token after it, separated by `;` or blanks, is a field.
Lists are comma separated (`ids=0000,ffff` and `pool=00,ff` are hex, `pos0_mm=x,y,z`
is signed decimal, `0x` values are hex). A vector component of `-2147483648`
(INT32_MIN, which native writes for a value it could not read, for example
`mem_pos_mm` while the vars block is unresolved) or any magnitude of 2^31-1 or
more marks the whole vector unavailable: it is excluded from distances and
spreads and reported as unreadable, never as a units or freshness failure.
Lines before the `run.json` offsets are ignored, and so are lines after the
`stop.json` `log_end_offsets` of a stopped run (`Stop-Lab.ps1` records each
instance's log lengths after closing the games, so judging an older run never
mixes in a later run's lines).

`partner_cfg=refused; reason=online_bit|sync_present` always fails. Otherwise a
refusal is classified by what precedes it in the same mission (since the last
`mission_boundary=runtime_enter`, `mission_boundary=termination_returned` or
`partner_stop`): after a `partner_cfg=ready` it comes from the per-tick state
read, which pauses the tick and logs `partner_cfg=ready` again once the state
is valid (a note when that line follows, INCOMPLETE until then); without one it
is the mission-start CFG receipt, after which `partner_sync.lua` stays inactive
for the mission (FAIL unless a later mission start logs `partner_cfg=ready`).

E3 accepts a realization that happened before `partner_bits=set` (E2 outcome B:
`partner_realized=1` is logged only on a change, so none follows the bits). The
last `partner_realized=1|0` line of the mission before the bits decides; the
1 s rule is then skipped, and parts and the 120 s window are measured on the
probe clock from the `stage=3` line (or with `ms_after_realize` when the
realization line is inside the stage 3 segment).

| Line | Used for |
|---|---|
| `mode=offline_v1; sessions=0; s_is_online=never; partner=<0\|1>` | E0 |
| `host_create=skipped; mode=offline_v1` | E0 |
| `partner=configured; mode=offline_v1; stage=..; source=..; apply=..; ...` | experiment segments (boot configuration) |
| `partner_stage=changed; stage=..; source=..; apply=..; via=trigger`, `partner_trigger=rejected; reason=parse` | experiment segments; a line may carry only the changed keys |
| `partner_cfg=ready; flags=..; count=2; realized_count=..; local=0; R=1; online_bit=0; sync=null; code_gate=ok` / `partner_cfg=refused; reason=..` | every experiment; E0 fails on `count`, `local`, `R`, `online_bit=1` or `sync=set` |
| `partner_net=listening; port=..; peer_port=..` / `partner_net=disabled; reason=..` | E0 |
| `partner_net=first_rx; seq=..; epoch=..` | E0, E6 |
| `partner_net=stats; tx=..; rx=..; drop_epoch=..; drop_seq=..; drop_bad=..; status=live\|stale\|lost; last_age_ms=..; ...` | E0 (tx, rx > 0), E6 (rx increasing, `status=live`, `last_age_ms<200`) |
| `partner_probe=state; t_ms=..; stage=..; ids=..; pool=..; ...; s0=..; q170=..; ...; pos0_mm=..; pos1_mm=..; rec1_mm=..; ...` | all; `t_ms` is the clock for every deadline; realized means `ids[1]!=ffff` and `pool[1]!=ff` |
| `partner_tick=rate; hz=..; focused=0\|1` | E0 (`focused=0` needs `hz>=10`) |
| `partner_units; lua_pos_mm=..; mem_pos_mm=..; yaw_cdeg=..; yaw_abs_max_cdeg=..; cam_cdeg=..` | E0 (`yaw_abs_max_cdeg>628`, lua/mem within 50 mm; `mem_pos_mm=-2147483648,..` in every line fails as unreadable) |
| `mission_boundary=runtime_enter; ...`, `mission_boundary=termination_returned; ...` | mission limits for the `partner_cfg=refused` and E3 realization rules |
| `partner_warp_check; R=1; apply=..; cmd_mm=..; slot_mm=..; err_mm=..` | E1, E4, E5 (`err_mm<=5`, `slot_mm` moving) |
| `partner_lua=event=<allactive_set\|allactive_cleared\|parts_copied\|error\|refused\|...>` | E2, E3, E8; `error` and `refused` fail any experiment |
| `partner_bits=set\|lost\|cleared; ...; reason=stop\|trigger` | E3, E8 (`cleared; reason=trigger` while stage 3 is the `stop` trigger) |
| `partner_realized=1; R=1; id=..; pool=..; ms_after_bits=..` / `partner_realized=0; ...` | E3 (`ms_after_bits<=1000` when it follows the bits), E4/E6 (no `=0`), E8 |
| `partner_parts=loaded; R=1; ms_after_realize=..` / `partner_parts=timeout; ...` | E3 (`ms_after_realize<=15000`) |
| `partner_stop; cleared=..; teardown=..` | E8; ends a segment |
| `crash_probe=installed`, `crash_probe=av; rva=..; ...` | `av` fails any experiment |

## Trigger inputs (scripts create, native consumes and deletes)

These files are honoured only when `lab.ini` has `join_trigger=1`
(`partner.trigger` is described above).

| File | Effect |
|---|---|
| `join.trigger` | Treated exactly like the guest's Ctrl+F8 join key. No foreground-window check. |
| `reset.trigger` | Releases a failed or pending client session and re-arms the join (the Ctrl+F9 equivalent). |

## Safety rules

- Lab behaviour activates only when `lab.ini` is present with `enabled=1`. With no
  `lab.ini`, the loader behaves exactly as 0.10.33 did.
- fuse must always run with `-standalone`. Without it, fuse patches the
  executable on disk.
- Offline lab mode: no fuse is started, and nothing may listen on the
  closed master port while the games run (Start-Lab checks it before the
  start); the master-server redirect is still required by the safety gate,
  because without it the game would contact Konami's default servers.
- The guest's fake SteamID never reaches real Steam networking. In loopback mode
  all ISteamNetworking traffic stays on 127.0.0.1.
- Saves are redirected to copies under `save_dir`. The real
  `Steam/userdata/<id>/{287700,311340}` files must keep their hashes after a run.
