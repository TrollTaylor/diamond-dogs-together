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
| `debug_soldier_warp` | `0`/`1` (`0`; written only when set) | lab only: play forces it off (`play_refused` bit `0x10000`). F9 **Spawn enemy here** native placement ([debug-menu.md](debug-menu.md)) |
| `net_latency_ms`, `net_loss_permille` | existing keys, written only when set (`-PartnerSetting guest:net_latency_ms=100`) | 0..2000, 0..1000 |

The lab defaults come from `lab.json` `"partner"` (short names such as `stage`, or
the key names) and `Prepare-LabInstance.ps1 -Offline -PartnerSetting key=value`,
`host:key=value` or `guest:key=value` (the ports stay in `lab.json`).

### Partner link v2 keys (native; [link-protocol.md](link-protocol.md))

Two-PC play: see docs/two-pc-play.md.

Since the link v2 change the partner transport authenticates every datagram
and runs a handshake first; in the lab this happens on 127.0.0.1 with today's
ports and roles and the built-in lab secret, so no lab.ini change is needed
(the guest instance initiates, the host responds). New keys, all optional, with
their native parsing in [link-protocol.md](link-protocol.md) 1.1:
`mode=lab|play` (`lab`), `net_link=v2|legacy` (`v2`; `legacy` restores the raw
frames for bisecting), `net_bind=loopback|any|<ipv4>` (`loopback`),
`net_peer_addr=<ipv4>` (127.0.0.1), `net_jitter_ms`, `net_loss_model=even|burst`,
`net_burst`, `net_dup_permille`, `net_reorder_permille`, `net_reorder_ms`,
`net_seed`, `partner_interp=fixed|auto`, `partner_interp_min_ms`,
`partner_interp_max_ms`, `partner_lost=freeze|hide`. `net_listen_port=0` is
valid only for a guest off loopback; on loopback it still counts as invalid,
exactly as before. Off loopback (the one-PC LAN rehearsal) the lab secret is
refused: both folders need the same `mgs-coop\link-secret.txt`, otherwise the
log shows `link=disabled; reason=secret_missing`. `mode=play` refuses every lab
identity hack and impairment key (`play_refused` bits) and is never used by the
lab scripts.

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
| `play=1; loopback=0; mutex=unchanged; refused=0x<hex>; saves=redirected\|unchanged` | Only with lab.ini `mode=play`, right after the `saves=` line ([link-protocol.md](link-protocol.md) 1.3). |
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
`coop.ini` takes `character=` from `lab.json` `offline.characters` (host
`default`, guest `quiet`: the test condition of
[partner-anim-appearance.md](partner-anim-appearance.md)); the fuse lab keeps
the top-level `characters` (guest `quiet`). offline-v1 answers the character
receipts (query, Ocelot/Quiet reset bypass, readback), so the guest's
`character=quiet` takes effect; every session receipt stays off.
`Prepare-LabInstance.ps1 -Disable` resets an `offline-v1` marker to the
folder's online lab role (`host-v3` / `client-v1`), because `offline-v1` skips
session creation and normal play could not host or join with it.

### `partner.trigger` (scripts create, native consumes and deletes)

One ASCII line, at most 255 bytes with its newline, written by
`Set-PartnerTrigger.ps1` as a temporary file renamed over
`mgs-coop/partner.trigger`:

```text
stop
stage=<0..3> source=<fixed|orbit|net> apply=<warp|slot|both|xform|hybrid|input> anim=<off|mirror|net>
  aset=<q|s|qs|r> reset=<n|a|c|ac> look=<copy|net|quiet|snake> probe=<ps|off>
  census=1 parts=explicit mirror=on   (any non-empty subset, each key once; move= is apply=)
  # round 3 (docs/partner-anim-appearance.md "Round 3"):
  input=<off|pass|neutral|stick|net> stick=<0..359>:<0..100> cam=<off|detect|sandbox>
  hold=<none|pass> yaw=<rot|rotneg|none> words=<3 of 0prhn> bits=0x<hex> correct=<0..10000>
  isend=<0|1> p218=<0|1> padprobe=<0|1> holdlog=<0|1> dm=<off|probe|on|map> lookgate=<0|1> lookrecover=<0|1>
  tap=0x<hex> press=0x<hex>:<50..5000> dmplay=<0..12> dmstop=1   (one-shots)
  # dm=map (docs/partner-anim-appearance.md "D-map"):
  dmmap=<12 comma-separated gani 0..12> dmspeed=<idle>:<run>:<dash>   (written after dm=)
  # round 4 (docs/partner-anim-appearance.md "Round 4"):
  hold=<none|pass|own|net> recon=<off|track|pin> align=<-50..150> slot=<remote|body|none>
  runaway=<pin|disarm|off> wrap=<off|census|window> place=1   (place is a one-shot)
  # correct=0 turns the reconciliation off whatever recon= says (logged as
  # partner_recon=off; reason=correct0); a trigger changes only the keys it
  # names, so the Play-OfflineCoop [I]/[P] lines and I6 send correct=3000 runaway=pin.
```

`census=1` (log `partner_census` for both blocks once, plus `partner_input_census`
even with `input=off` and `partner_dm_census` when `dm` is not off; a trigger that changes
`anim=` or `aset=` also re-arms the change-only anim/stance lines), `parts=explicit`
(one-shot op 16: re-apply the desired look; with `look=copy` the local look,
variant F3b) and `mirror=on` (one-shot op 32, the mirror diagnostic: F3c, at
most 10 s; refused with `partner_trigger=ignored; token=mirror=on; reason=look`
unless `look=copy`, because mirror mode copies the local look over instance 1)
are the native parser's one-shot tokens (`native/partner_receipt.cpp`
`ParseTrigger`). The round 3 one-shots are `tap`/`press` (held bits, only while
the input window is armed and within `bits=`; refused with
`partner_input=tap_refused; reason=mask|not_armed`), `dmplay`/`dmstop` (direct
motion, `dmplay` only at stage 1+; refused with `partner_dm=refused;
reason=not_patched|parts|busy|cam_connect`, and with `reason=map` while
`dm=map` owns the direct motion or `reason=map_release` while it releases its
clip) and `p218=1` (the hazardous
diagnostic P+0x218 window: needs `lab.ini partner_input_p218_allow=1` and
`cam=sandbox`, auto-expires after 20 s; quit without saving afterwards).
`holdlog=0|1` is a setting, not a one-shot: it needs `lab.ini
partner_input_holdlog=1` (else `partner_input=holdlog_refused;
reason=not_allowed`) and acts only while the input window is armed. `press=`
milliseconds have no leading zeros (`press=0x2:0100` is refused by both the
scripts and the native parser).
`parts`/`mirror`/`dmplay` take effect only at stage 1 or higher. The one-shots
are not part of the `partner_stage=changed` line; Lua logs
`partner_lua=event=parts_set; R=1; type=..; ...`, `event=mirror_on`,
`event=dm_play;gani=N` / `event=dm_stop` when it runs them. The other keys are
described in [partner-anim-appearance.md](partner-anim-appearance.md).

The scripts validate the grammar (lower case, blanks between tokens, `stop` only
alone, `move=` written back as `apply=`) and write the canonical order (the
classic keys first, then the round 3 keys, with the one-shots last). Native code
polls the file every 30 Lua ticks, only with `offline-v1` and `lab.ini`
`partner=1`. It deletes the file whether it accepts it or not, then logs
`partner_stage=changed; stage=..; source=..; apply=..; anim=..; aset=..;
reset=..; look=..; probe=..; lookgate=..; lookrecover=..; input=..; cam=..;
hold=..; yaw=..; words=..; mask=0x..; correct=..; isend=..; dm=..; recon=..;
align=..; slot=..; runaway=..; wrap=..; via=trigger`
(the full configuration after the trigger; `stop` sets stage 0) or
`partner_trigger=rejected; reason=parse`.
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
| `soldier_warp=ready; ...` / `soldier_warp=refused; reason=body_contract\|slot_contract\|image; rva=..`, `soldier_warp=disabled; reason=fault\|anomaly; id=..; go=..; r=..; h=..; until=process_exit` (latched off), `soldier_warp_census; go=..; type_vt=..; count_fn=..; handle_vt=..; hop=..; hop_bytes=..; leaf_vt=..; leaf=pinned\|engine; ...`, `debug_menu=soldier_warp; id=..; go=..; r=..; h=..; leaf=..; refused=..` | `debug_soldier_warp=1` only: the contract gate at startup, the read-only dispatch census (at most 24 lines) and each native placement attempt ([debug-menu.md](debug-menu.md)) |
| `world_place=warp; seq=..; kind=1\|3; ent=..; go=..; age_ms=..; r=..; h=..; leaf=pinned\|engine; refused=..`, `debug_menu=remote; message=..` | Guest spawn sync ([debug-menu.md](debug-menu.md) "Spawn sync"): each change of a native placement of a host-announced grant (same key, contract, budget and latch as `soldier_warp`), and each debug-menu history row for the other PC's result (both roles) |
| `partner_census; k=0\|1; id=..; base=..; count=..; blk=ok\|<reason>; blk_vt=..; ps_vt=..; cs_vt=..; t=set\|null; t_vt=..; t68=..; t70=..; a0_vt=..; a080=..; c0_vt=..; c090=..; b70_vt=..; stance=ok\|<reason>; psw=0x..; equip_vt=..; same_t=..; same_a0=..; same_c0=..; verdict=ok\|<reason>` | A0 (RVAs to pin); logged on `census=1` and once per realize when a native feature needs it. `verdict` = identities + transform chain (xform, hybrid, body readback); `stance` = identities + the SetStance prerequisites (`ps_words`: stance words, `ps_sub`: PS+0x260), a note in A0, never blocks xform |
| `partner_anim; act=a0,a1; s=s0,s1; v=v0,v1; q28=..; qd8=..; psw=..,..; rec1=0x..` | A1 (`lab.ini` `partner_anim_probe=1`; per slot: act u16 hex, stance and speed u8) |
| `partner_anim=source; anim=mirror\|net; valid=0\|1; act=..; s=..; v=..` | A1/N1 (change-only; with `partner_aset=q` capped at 64 lines, then `partner_anim=capped; limit=64`) |
| `partner_aset=q; state=applied; act=..; s=..; v=..` / `partner_aset=q; state=refused; reason=..; act=..` | A1 (change-only; `reason=act_domain` = an act id outside the locomotion ids 1..6, deliberately not written: a note, not a failure) |
| `partner_stance=set; R=1; id=1; from=..; to=..; psw=0x..; q30=..`, `partner_stance=refused; R=1; to=..; reason=..`, `partner_stance=reverted; R=1; psw=..; expected=..; ms_after_set=..`, `partner_stance=req; R=1; from=..; to=..; result=..` | A3, N1 (a `refused` target never set afterwards fails A3; one retried and set is a note). Set/req-ok, refused (change-only per reason; the target is retried after 1 s) and reverted are capped at 64 lines each: `partner_stance=capped; limit=64; kind=set\|refused\|reverted`. A `reverted` while the partner was Warped every tick is reported as not attributable |
| `partner_stance=summary; last=0..3; sets=..; reqs=..; refused=..; reverts=..` | counters that keep going after the caps: at the first stance event, then at most every 10 s while they change (Play-OfflineCoop status line) |
| `partner_xform=applied; R=1; apply=..; reset=..` / `partner_xform=refused; reason=..; fallback=warp` | M1 (identity refusals fail; `unrealized`/`parts` are normal before the load) |
| `partner_xform; R=1; apply=..; cmd_mm=..; body_mm=..; err_mm=..; yaw_err_mrad=..; reset=..` | M1 (1 Hz readback of T+0x20/T+0x60; `err_mm>1500` fails) |
| `partner_hybrid=warp; reason=first\|err\|relink` | M1 data |
| `partner_call=fault; fn=xform\|set_stance\|stance_request; native_calls=disabled` | fails A3/M1/N1/B1/B4; native calls stay off for the mission |
| `partner_net=look_rx; type=..; parts=..; camo=..; hand=..; face=..; fe=..; rev=..; loaded=..` | B4 (change-only, receive thread) |
| `partner_look=apply; R=1; source=net\|quiet\|snake\|copy; type=..; parts=..; camo=..; hand=..; face=..; fe=..; rev=..; avatar_as_snake=..` | B1, B4 |
| `partner_look=loaded; R=1; q40=..; q48=..; q50=..; q58=..; q60=..; q68=..; ms_after_apply=..; cycle=0\|1`, `partner_look=timeout; ...`, `partner_look=refused; reason=..; ...`, `partner_look=waiting; R=1; ms_after_allactive=..; fallback=copy` | B1, B4 (`timeout`/`refused` fail) |
| `partner_looks; look0=t,p,c,h,f,fe; look1=...; parts=..,..` | diagnostics (change-only, explicit looks) |
| `partner_lua=event=parts_set; R=1; type=..; ...`, `partner_lua=event=parts_set_refused; reason=bad_look` | B1, B4 (`parts_set_refused` fails); `parts_copied` after a network look fails B4 |
| `partner_ps; k=..; id=..; mask740=..; r738_milli=..; r280=..; b2d0=..; b2d8=..; psw=..` | `probe=ps` (4 Hz, read-only, layouts partly unverified) |
| `character_config=..`, `player_type_patch=active\|refused`, `character=apply; ...; applied=..`, `character=allocated; ...; vars=..` | C1 (guest: `quiet`, `applied=quiet`, `vars=6`) |
| `partner_recon=place; reason=..; via=warp; target_mm=..; ...`, `partner_recon=placed; ok=1\|0; err_mm=..; tries=..; ms=..` | round 4 R1/R4 (apply=input placement and its verification) |
| `partner_recon=snap; reason=far_moving\|far_idle\|far_prone; via=xform; err_mm=..; ...`, `partner_recon=diverging; ...` | R2/R3 (a snap only on a large sustained error; no 1 Hz sawtooth) |
| `partner_recon=runaway; remote=idle\|moving; ...; dv_mmps=..; err_mm=..; ...; action=pin\|disarm\|off`, `partner_recon=runaway_end; ms=..; reason=..` | R0/R1/R2 (the body moves on its own: the remote stands still, or its own velocity differs from the moving remote's while the error grows; ends when both motions match) |
| `partner_recon=input_pin; in_age_ms=..; err_mm=..; reason=input_stale`, `partner_recon=input_pin_end; ms=..; reason=input_live\|pose_stale` | R4 (net input stale 500 ms with a live pose: pinned, no far Warp; then tracking resumes) |
| `partner_recon=stance_fix; remote=..; body=..; via=tap\|hold; ms=..; fixes=..`, `partner_recon=stance_fix; refused=mask; ...` | R5 (a stance mismatch held 2 s: one corrective STANCE tap, or a hold for prone, every 2 s at most) |
| `partner_recon=off; reason=correct0; requested=track\|pin; fix=correct=3000` | a trigger left `correct=0`: no corrections whatever `recon=` says |
| `partner_recon=frozen; reason=..`, `partner_recon=unfrozen`, `partner_recon=stance; ...`, `partner_recon=stance_mismatch; ...`, `partner_recon=yaw; ...`, `partner_recon=stop_check; ok=..; ...`, `partner_recon=refused; ...` | R0-R6 diagnostics (each family capped at 64 lines) |
| `partner_recon; mode=..; recon=off\|track\|pin; err_mm=..; dy_mm=..; along_mm=..; cross_mm=..; v_free_mmps=..; v_remote_mmps=..; dv_mmps=..; body_mm=..; target_mm=..; ...; snaps=..; places=..; ...; stop_ok=..; runaway=..; input_pin=..; fixes=..` | the 1 Hz reconciliation line (3600 per mission; `recon=` is the effective setting; snaps/places are cumulative since the reconciliation started) |
| `partner_body; R=1; body_mm=..; yaw_mrad=..; speed_mmps=<n\|na>; own_mmps=<n\|na>; vel_mmps=<n\|na>; ...; epoch=..; snaps=..` | 2 Hz body readback (3600 per mission; speed from the oldest same-epoch sample of a 32-tick ring over the actual span, at least 150 ms) |
| `partner_wrap; mode=..; svc28=c,s,n,w,0xph; svc30=..; svc40=..; walk7=..; walk11=..; r0=..; other=..`, `partner_input_wrap=installed\|refused; ...` | W0 wrap census (wrap=census\|window) |
| `partner_dm_census; ...`, `partner_dm; patched=..; g388=0x..; ...` (2 Hz, change-only) | D0 (dm=probe\|on\|map) |
| `partner_dm=patched; rva=0x1198e25; ...`, `partner_dm=reverted; reason=trigger\|stop\|mission_start\|mission_end\|cfg_refused\|tick_rejected\|fault` | D1-D5 (the comparator patch and its revert) |
| `partner_dm=play; gani=..`, `partner_dm=stop` | D1-D3 manual one-shots (not logged for mapped steps) |
| `partner_dm=refused; reason=not_patched\|parts\|busy\|cam_connect\|not_in_dm\|map\|map_release`, `partner_dm=refused; reason=input_active\|native_disabled\|patch_failed\|comparator_bytes\|patch_..` | a refused one-shot; a refused patch (change-only; a failed patch is not retried until the next `dm=` trigger) |
| `partner_dm=unconsumed; ms=..`, `partner_dm=cleared_pending`, `partner_dm=conflict; q18_0=..; cam1f0=..; cam200=..` | a play never consumed (FAIL in D1-D5); our DM met the host's DM or a camera connect (FAIL) |
| `partner_dmmap=table; cells=..; speeds=..; reason=start\|trigger` | D4/D5 the dm=map table (first map tick; every `dmmap=`/`dmspeed=` trigger) |
| `partner_dmmap=switch; step=stop\|play; from=..; to=..; stance=..; move=..; speed_mmps=..; via=..; stance_via=..; wait_ms=..`, `partner_dmmap=playing; gani=..; after_ms=..` | D4/D5 clip switches (600 per mission, then `partner_dmmap=capped; limit=600; kind=switch`) |
| `partner_dmmap=deferred; reason=..; phase=..; want=..; cur=..`, `partner_dmmap=lost; gani=..; played_ms=..; cause=ended\|warp\|look\|parts\|host; ...`, `partner_dmmap=adopted; reason=active_clip`, `partner_dmmap=abandoned; reason=..; gani=..` | D4/D5 diagnostics (each family 64, then `partner_dmmap=capped; limit=64; kind=<family>`) |
| `partner_dmmap=unconsumed; gani=..; ...`, `partner_dmmap=stop_timeout; ...; reason=stop_timeout\|dm_read`, `partner_dmmap=bad_gani; gani=..; reason=unconsumed\|ended; strikes=2`, `partner_dmmap=failed; failures=5; last=..` | D4/D5 failures (bad_gani, failed or more than 3 stop_timeout FAIL) |
| `partner_dmmap=released; result=stopped\|ended\|timeout; to=off\|on\|probe; ms=..` | D4/D5 the mapped clip released when dm leaves map (then reverted with dm=off) |
| `partner_dmmap; phase=..; cur=..; want=..; stance=..; move=..; via=..; speed_mmps=..; switches=..; ...; bad=0x..; failures=..; deferred=..` | the 5 s dm=map summary (720 per mission; Play-OfflineCoop shows the clip) |

## World sync (opt-in; [world-sync-protocol.md](world-sync-protocol.md))

`lab.ini` keys (written only when set; the default offline `lab.ini` is
unchanged): `world=0..3` (0 inert, 1 plumbing, 2 enemies + alert, 3 also clock/
weather, host travel and reload recovery) and `world_probe=<probe>` (lab only; `p1 p3 p3b p4 p12 p17 race_kill
remote_area_kill kill_near sleep_near fulton_near alert_near weather_rain
clock_jump clock_fast shared_death host_continue shared_checkpoint`). `Play-OfflineCoop.ps1 -World N [-WorldProbe <probe>]`
sets them on both copies (or `Prepare-LabInstance.ps1 -Offline -PartnerSetting
world=N`). World sync needs offline-v1, `partner=1`, `net=1`, `net_link=v2` and
redirected saves; it logs `world=configured` or `world=disabled; reason=...`.

### `world.trigger` (scripts create, native consumes and deletes)

One line: `world=0..3`, `world_probe=<probe>|off`, `goto=30010|30020`,
`goto_lz=0|1` (only with goto), each at most once. Written with
`tools/lab/Set-WorldTrigger.ps1 -Role host|guest|both -Text '<line>' [-WaitSec
N] [-Force]` (canonical form, atomic rename; `<run>/world-triggers.log`).
Native polls it at most once per second while a mission with world sync runs
(lab mode only), deletes it and logs `world_trigger=applied; text=<canonical>`
or `world_trigger=rejected; reason=token|repeat|empty|too_long|not_lab|level0`;
a goto is then `world_goto=armed; ...` or `world_goto=refused;
reason=level|mission|target|busy|terminating`. With `-WaitSec` the script exits
0 only when applied (a goto: armed), 3 when rejected or refused (or another
command was applied), 2 when not consumed, 4 when unconfirmed after 3 s.
`world=0` switches world sync off at once; `world=1..3` applies at the next
mission start; `world_probe` runs now (or 10 s after the next start); `goto`
flies a copy standing in Mother Base, normal Afghanistan/Africa free roam
or the ACC to the target free roam. With World 3, send it to the host only;
`[G]` also selects host only and the guest follows the host's travel. At
levels 1/2 use `-Role both` for independent travel. Start-Lab -Offline and Stop-Lab delete leftovers
(`stop.json world_triggers_removed`).

### Live test recipe (both copies on this PC)

These are expected results for acceptance; W3/W4 live acceptance remains
pending for the integrated DLL. Automated fixture passes do not replace
the two running games and their readbacks.

1. Build and test: `pwsh -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1`.
2. W0-4 (world=0 is inert): `pwsh -File tools/lab/Play-OfflineCoop.ps1
   -ExitAfterReadySec 180`. Expect no `world` key in either `lab.ini`, no log
   line starting with `world`, unchanged partner behaviour. Compare each
   `script-observer.log` with a coop-lab 794d24f baseline after dropping every
   `entry_count=` and `return_count=` line, the two `MGS_COOP_WORLD_CFG_V1`
   receipt lines and the `tick_ms=`/`t_ms=` values.
3. Main run: `pwsh -File tools/lab/Play-OfflineCoop.ps1 -World 3
   -KeepLabEnabled`. In 30050 both logs show `world=configured; level=3;
   role=...; mode=lab`, `world=cfg; mission=30050`, `world=hello_rx;
   mission=30050; ...; level=3; ...; saves=1`, `world=paired; ...;
   mission=30050; ...; match=1; ...; eff=3` and `world_lua=event=started;
   level=3; ...`.
4. Goto: `pwsh -File tools/lab/Set-WorldTrigger.ps1 -Role host -Text
   'goto=30010' -WaitSec 15` (or `[G]`). Host logs: `world_goto=armed;
   target=30010; lz=0`, `world_goto=step; step=1; result=reserved`. If neither
   shows `world=cfg; mission=40050` within 20 s, press Space in both windows
   (`pwsh -File tools/lab/Send-LabInput.ps1 -Role both -Keys space`); the
   ACC's "Overall Completion" screen needs Enter (`-Keys enter`). Then
   `world=cfg; mission=40050`, `world_goto=step; step=2; result=waiting` and
   `reserved`, `world=cfg; mission=30010`, `world_goto=arrived;
   mission=30010`. Guest logs `event=follow;step=queued|seeds_ready|replay|arrived`
   for each transition and both logs reach matching mission epochs. A clock
   jump of about +6 h is expected. Complete `event=align;step=accepted` on the
   guest and `step=ready` on the host must precede enemy-sync acceptance.
5. Probes on one copy: `pwsh -File tools/lab/Set-WorldTrigger.ps1 -Role host
   -Text 'world_probe=kill_near'` (likewise `sleep_near`, `fulton_near` and
   `alert_near` on the guest; `race_kill`, `remote_area_kill`, `p1`, `p3`,
   `p3b`, `p4`, `p12`, `p17`, `weather_rain`, `clock_jump`, `clock_fast` on the
   host). Every probe line is `world_lua=event=probe; id=<name>; step=...`.

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
