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
| `script-observe.enabled` | existing | Mode marker: `host-v3`, `client-v1` or `client-check-v1`. |
| `client-host.txt` | existing | Guest only: the host's SteamID64 in decimal. |

`lab.ini` keys: `enabled`, `role` (`host`/`guest`), `loopback`, `self_steamid`
(`real` or a decimal SteamID64), `self_name`, `peer_steamid` (`learn` or decimal),
`listen_port`, `peer_port`, `mutex_suffix`, `save_dir` (absolute), `xinput`
(`passthrough`/`none`/`0`-`3`), `join_trigger`, `auto_join_delay_s`,
`net_latency_ms`, `net_loss_permille`, `trace_packets`. See
`native/include/mgscoop/lab_config.hpp` for types and validation.

Lab defaults used by the scripts:

| Setting | Host | Guest |
|---|---|---|
| Folder | `MGS_TPP` | `MGS_TPP_guest` |
| `self_steamid` | `real` | `76561202255233008` (account `0xFFFFFFF0`, not a real account) |
| `listen_port` / `peer_port` | 47810 / 47811 | 47811 / 47810 |
| `mutex_suffix` | (none) | `-guest` |
| `save_dir` | `<exeDir>\mgs-coop\lab\saves` | same pattern |
| fuse | `http://127.0.0.1:6667/tppstm/gate` | same |

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

## Trigger inputs (scripts create, native consumes and deletes)

These files are honoured only when `lab.ini` has `join_trigger=1`.

| File | Effect |
|---|---|
| `join.trigger` | Treated exactly like the guest's Ctrl+F8 join key. No foreground-window check. |
| `reset.trigger` | Releases a failed or pending client session and re-arms the join (the Ctrl+F9 equivalent). |

## Safety rules

- Lab behaviour activates only when `lab.ini` is present with `enabled=1`. With no
  `lab.ini`, the loader behaves exactly as 0.10.33 did.
- fuse must always run with `-standalone`. Without it, fuse patches the
  executable on disk.
- The guest's fake SteamID never reaches real Steam networking. In loopback mode
  all ISteamNetworking traffic stays on 127.0.0.1.
- Saves are redirected to copies under `save_dir`. The real
  `Steam/userdata/<id>/{287700,311340}` files must keep their hashes after a run.
