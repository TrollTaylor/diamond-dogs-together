# Partner link v2 and two-PC play mode (native side)

This is the native contract for playing the offline co-op on two PCs: the
`mode=play` lab.ini, the save gate, the authenticated UDP link between the two
loaders, and the files and log lines the launchers read. The player tooling
(Play-Coop, the join code, the friend package) is described in
[two-pc-play.md](two-pc-play.md); the one-PC lab is in
[lab-protocol.md](lab-protocol.md).

What it is not: no Konami server is contacted (the offline-v1 marker and the
dead-port `coop.ini` `master_url` stay exactly as in the lab), and nothing
touches S_IS_ONLINE, the game's synchronizer, sessions, the master server,
fuse or Steam P2P (ISteamNetworking). The link is our own UDP socket.

Sources: `native/lab_config.cpp` (keys), `native/early_patches.cpp` (save
result, early.log), `native/game_script_observer.cpp` (gate, wiring),
`native/udp_transport.cpp` (socket, impairment), `native/link_crypto.cpp`,
`native/link_protocol.cpp`, `native/link_session.cpp` (pure protocol),
`native/partner_net.cpp` (threads, status file). Tests:
`tests/link_crypto_tests.cpp`, `tests/link_protocol_tests.cpp`,
`tests/link_session_tests.cpp`, `tests/link_udp_tests.cpp`,
`tests/lab_config_tests.cpp`, `tests/lab_ini_check.cpp`.

## 1. Play mode

### 1.1 lab.ini keys

Parsed by `ParseLabConfig`. An unknown value adds 1 to `invalid_values` and
keeps the default. A lab.ini without any of these keys parses exactly as
before (tested). "(play)" marks the defaults that differ in play.

| key | values | default | notes |
|---|---|---|---|
| `mode` | `lab` \| `play` | `lab` | Parsed even when `present` is false. |
| `net_link` | `v2` \| `legacy` | `v2` | `legacy` = the raw lab frames; refused in play. |
| `net_bind` | `loopback` \| `any` \| dotted IPv4 | `loopback`; (play) `any` | A 127/8 address counts as `loopback`. |
| `net_listen_port` | `0` \| 1024..65535 | role default 47820/47821; (play) host 47830, guest 0 | `0` (ephemeral) only for a guest off loopback; anywhere else it is invalid and not given, exactly as before. |
| `net_peer_addr` | dotted IPv4 | 127.0.0.1 | Four decimal octets, no leading zeros; not 0.0.0.0, 255.255.255.255 or 224.0.0.0/4; 127/8 only in lab. No names, no IPv6. With a loopback bind only 127/8. |
| `net_peer_port` | 1024..65535 | role default; (play) 47830 | Ignored by a host off loopback. |
| `net_jitter_ms` | 0..1000 | 0 | lab only |
| `net_loss_model` | `even` \| `burst` | `even` | `even` = `should_drop`; `burst` = Gilbert-Elliott |
| `net_burst` | 1..16 | 2 | mean loss-burst length |
| `net_dup_permille` | 0..1000 | 0 | lab only |
| `net_reorder_permille` | 0..1000 | 0 | lab only |
| `net_reorder_ms` | 1..500 | 40 | |
| `net_seed` | 1..4294967295 | 1 | |
| `partner_interp` | `fixed` \| `auto` | `fixed` | Play-Coop writes `auto`. |
| `partner_interp_min_ms` | 20..500 | 60 | |
| `partner_interp_max_ms` | min..1000 | 350 | below min: invalid, keeps 350 |
| `partner_lost` | `freeze` \| `hide` | `freeze` | `hide` is lab-tested only |

`net_latency_ms` (0..2000), `net_loss_permille` (0..1000) and
`partner_interp_ms` (0..500, default 100) keep their parsing.

### 1.2 Post-parse rules (in this order)

1. **Play defaults** for keys not given: `net_bind=any`, host
   `net_listen_port=47830`, guest `net_listen_port=0`, `net_peer_port=47830`.
2. **Play refusals.** Each is forced off, sets its bit in `play_refused` and
   adds 1 to `invalid_values`:

   | condition | forced to | bit |
   |---|---|---|
   | `loopback=1` | false | 0x01 |
   | `self_steamid` set | 0 | 0x02 |
   | `peer_steamid` set | 0 | 0x04 |
   | `self_name` set | "" | 0x08 |
   | `mutex_suffix` set | "" | 0x10 |
   | `xinput` not passthrough | passthrough | 0x20 |
   | `join_trigger=1` | false | 0x40 |
   | `auto_join_delay_s` not 0 | 0 | 0x80 |
   | `net_peer_addr` on the host | ignored | 0x100 |
   | `net_bind=loopback` | `any` | 0x200 |
   | `net_link=legacy` | `v2` | 0x400 |
   | invalid `net_peer_addr` (counted once, as the invalid value) | | 0x800 |
   | empty `save_dir` | | 0x1000 |
   | guest without a valid `net_peer_addr` | `net=0` | 0x2000 |
   | each nonzero of `net_latency_ms`, `net_loss_permille`, `net_jitter_ms`, `net_dup_permille`, `net_reorder_permille` | 0 | 0x4000 |
3. **Ports, when `net=1`.** Loopback bind: the lab rule, unchanged (role
   defaults when neither port is named; both nonzero, different, and not
   `listen_port`/`peer_port`). Host off loopback: `net_listen_port` (default
   47820 lab / 47830 play), the peer port is unused. Guest off loopback:
   `net_listen_port` 0 or >= 1024, `net_peer_port` >= 1024 and a valid
   `net_peer_addr`, otherwise `net=0` and +1. The guest port may equal the
   host port.
4. `present = enabled && role != none`, unchanged.

### 1.3 Save gate and lines

`InstallEarlyLabPatches` (DllMain) records whether the save redirect really
engaged; `lab::SavesRedirected()` returns it (false without lab.ini, on any
refusal, `save_dir_too_long`, a steam_api64 or slot mismatch).

**early.log**, only with `mode=play`, right after the `saves=` line:

```
play=1; loopback=0; mutex=unchanged; refused=0x0; saves=redirected
```

(`saves=unchanged` when the redirect did not engage; `refused` is
`play_refused` in upper-case hex.)

**Observer gate.** With `mode=play`, an `offline-v1` marker and
`!SavesRedirected()`, the marker is pass-through: no splice, archive,
partner, character receipts or link, and no hook is installed. The game is
plain offline play on the real saves. script-observer.log gets only:

```
play=refused; reason=saves_not_redirected; refused=0x0
mode=pass_through; gameplay=disabled
```

In lab mode the same condition only logs `offline_v1=warn;
reason=saves_not_redirected` after the `mode=offline_v1` line.

**script-observer.log**, right after `mode=offline_v1; ...`, only in play:

```
play=active; role=host|guest; partner=1; refused=0x0
```

A play guest whose lab.ini had no usable address logs `link=disabled;
reason=no_peer_addr` before `partner_net=disabled; reason=transport_bad_config`.

The partner still exists only in Mother Base (mission 30050); the
`link-status.json` `mission` field shows the current one as is.

## 2. Transport (`lab::UdpTransport`)

- Binds `bind_address:listen_port` (0.0.0.0 in play, 127.0.0.1 in the lab);
  `local_port()` is the bound port (the guest's ephemeral one in play).
- `any_source=false`: only `peer_address:peer_port` reaches the sink (the
  lab's exact filter). `any_source=true`: every datagram reaches the sink with
  its source; the session filters.
- `transmit_to(endpoint)` for any endpoint; `transmit()` goes to the peer.
- `SIO_UDP_CONNRESET` and `SIO_UDP_NETRESET` are off; `SO_EXCLUSIVEADDRUSE`.
- The receive thread drains at most 64 datagrams per pass, then runs the
  delay flush and `on_idle` when due (deadline-driven): a flood cannot starve
  the timers (tested with 100 000 junk datagrams).
- Impairment (lab only), outbound droppable datagrams: loss (`even` or
  Gilbert-Elliott: good->bad p = L/(B(1-L)), bad->good 1/B), latency, jitter
  (uniform 0..jitter_ms), reorder (+reorder_ms for that share), dup (a second
  copy at +1 ms). Delayed datagrams go through a 256-entry min-heap ordered by
  (due, insertion) with their endpoint; without jitter/reorder/dup the order is
  FIFO. Heap overflow counts as `dropped`; `duplicated` is counted. Only the
  impaired path copies a datagram; nothing else allocates per datagram.

## 3. Pairing and keys

**Secret.** 20 Crockford base32 characters (100 bits). Play reads
`mgs-coop\link-secret.txt` once at partner_net Start (never in DllMain):
exactly the 20 characters plus `\n` or `\r\n`. Status `ok`, `missing`,
`bad_length`, `bad_char`; play with anything but `ok` logs `link=disabled;
reason=secret_<status>` and starts no transport. The lab uses the lab secret
`C00PC00PC00PC00PC00P` only with `net_bind=loopback` and no file; off loopback
the file is required. Buffers that held the secret are cleared; the secret,
the code, keys, nonces and tags are never logged.

**Join code** (tooling; the DLL never parses it; `tests/link_join_code.hpp` is
the test-only native mirror): `MGS1-SSSSS-SSSSS-SSSSS-SSSSS-CC@<host>:<port>`,
check `SHA256("MGS1|" + S + "|" + host + "|" + port)`: `c1 =
alphabet[d[0] >> 3]`, `c2 = alphabet[((d[0] & 7) << 2) | (d[1] >> 6)]` over the
canonical forms. The key does not depend on host:port.

**Key schedule** (in-tree HMAC-SHA256, `link_crypto.cpp`, precomputed inner
and outer states, no handles):

```
Kdf(K, label, ctx) = HMAC-SHA256(K, label || 0x00 || ctx)
K_psk = HMAC-SHA256(key = "mgs-coop/link/v2", msg = S)
K_hs  = Kdf(K_psk, "hs", "")
K_g2h = Kdf(K_psk, "g2h", Ng || Nh)     K_h2g = Kdf(K_psk, "h2g", Ng || Nh)
sid   = u64 LE of Kdf(K_psk, "sid", Ng || Nh)[0..8), 1 if 0
tag   = HMAC-SHA256(K, datagram[0 .. size-16))[0..16)
```

Ng and Nh are 16 bytes from `BCryptGenRandom` on the transport thread.
`tests/link_crypto_tests.cpp` pins FIPS 180, RFC 4231 cases 1, 2 and 6, 1000
random pairs against BCrypt, the KDF vectors and every join-code vector.

## 4. Wire format

36-byte little-endian header, then the body, then the 16-byte tag:

```
0 'MGSL' | 4 version 2 | 5 type (1 HELLO, 2 WELCOME, 3 CONFIRM, 4 DATA, 5 REJECT)
6 flags (bit0 HAS_ACK, bit1 UNAUTH: REJECT only, zero tag) | 8 session_id
16 seq | 20 ack | 24 ack_bits | 28 send_ms | 32 ack_delay_ms | 34 body_len
```

Strict decode: size 52..1200 and `52 + body_len`, magic, version 2, known
type, reserved flags zero. Another version is answered (`proto_mismatch`,
UNAUTH, 88 bytes) only at >= 1200 bytes; everything else is `malformed`.

| packet | size | key | body |
|---|---|---|---|
| HELLO | 1200 (padded) | K_hs | proto, min_proto, mode (1 lab, 2 play), role 2, Ng, build[16] |
| WELCOME | 104 | K_hs, session_id = sid | proto, mode, role 1, Nh, echo Ng, build[16] |
| CONFIRM | 52 | K_g2h, seq 1 | empty |
| DATA | 52..1200 | K_g2h / K_h2g | messages |
| REJECT | 88 | K_hs (echo Ng), the session key, or UNAUTH (zero tag) | reason, proto, build[16], echo Ng |

REJECT reasons: 1 `proto_mismatch`, 2 `build_mismatch`, 3 `mode_mismatch`, 4
`bad_code`, 5 `busy`, 6 `role_mismatch`. Every answer to unauthenticated input
is smaller than that input (asserted for every path in the tests).

**DATA messages:** `u8 type | u8 0 | u16 len | bytes`: 0x01 FRAME (one MGSC
frame, unchanged: transform 56/64, input 76/68, look 40), 0x02 RELIABLE
(`channel | 0 | msg_id | payload <= 1024`), 0x03 CLOSE (accepted and logged,
never sent), 0x04 RELACK (`channel | free_slots | next_expected | bits`).
Unknown types are skipped by length; an overrun drops the packet. The packer
puts the FRAME first, then the RELACKs, then reliable data; the datagram never
exceeds 1200 bytes. An empty DATA is the keepalive / ack-only packet.

## 5. Handshake and sessions (`link_session.cpp`, pure)

**Guest:** `connecting` sends a HELLO with a fresh Ng every 500 ms for 10 s,
then every 2 s, after 10 minutes without success every 10 s, and accepts a
WELCOME echoing any of its last 4 nonces (deviation from "the pending Ng":
tolerates an RTT longer than one HELLO interval). The WELCOME must come from
the configured port (any address, counted in `src_changed`), carry a valid
K_hs tag and `session_id == sid(Ng, Nh)`; the guest then sends CONFIRM
(`handshake`, resent every 250 ms up to 10 times) and is `connected` on the
host's first DATA. Both sides send an empty DATA keepalive after 400 ms
without a send. `connected` with no authenticated packet for 2 s (five
keepalives) goes `reconnecting` (as connecting; the old session stays usable
until the new one completes, and its DATA brings it back). A K_hs REJECT
counts only with the echo of a pending Ng, a session REJECT only under the
current session key: `rejected`, retried every 5 s. Exception: a K_hs
REJECT(`busy`) within 3 s of entering `reconnecting` is our own NAT rebinding
(the host still holds the old endpoint until it is lost): ignored and counted,
the HELLO cadence goes on. An UNAUTH REJECT is a hint (`verified=0`) while
connecting and never changes state or cadence; everything else is ignored and
counted. The guest only ever sends to its configured endpoint.

**Host:** `listening`. A HELLO passes a per-address token bucket (any source
port; 8/s, burst 16, 64-entry table; a colliding address shares the entry
until it has been idle 2 s and a replacement starts with 2 tokens) and a
global MAC budget (500/s) before its tag is checked; HELLOs from the current
(or lost) guest's address skip both and use their own budget (16/s), so a
flood from elsewhere cannot block its reconnect. A bad tag gets at most one
UNAUTH REJECT(`bad_code`) per second in total. A
valid HELLO is checked for proto, mode, role and (play) build, then: an Ng of
a committed session is a replay; while `connected` a HELLO from another
endpoint gets REJECT(`busy`); the Ng of a pending slot gets the same WELCOME
again; otherwise a pending slot (2, oldest replaced) and a WELCOME. A CONFIRM
or DATA of a pending sid from that slot's endpoint with a valid K_g2h tag
commits (at most one commit per 2 s): the endpoint is locked, the Ng goes into
a 32-entry seen ring, the receive state is reset and an empty DATA is sent at
once (`connected` / `reconnected` same endpoint / `rebound` new endpoint). A
repeated CONFIRM of the current session is answered with an empty DATA (<= 4/s)
only in the guest's resend phase (no guest DATA yet, at most 2.75 s after the
commit, `connected`); otherwise it is a `replay` (CONFIRM always carries seq 1,
so the window cannot catch it), and it never counts as liveness.
DATA from any other endpoint is dropped (`wrong_endpoint`, never re-targeted).
No authenticated packet for 3 s: `lost`; the host then sends nothing at all
until the guest is back (its DATA, or a new handshake; a takeover by another
endpoint is possible only now).

Sequences are u32 per direction per session (CONFIRM 1, DATA from 2; host DATA
from 1) with a 1024-bit replay window and no wrap: at 0xFFFFFF00 the guest
reconnects and the host stops at `lost`. Receive checks, cheapest first:
size/header (`malformed`), type for the role (`unknown_msg`), HELLO budgets
(`rate`), session id (`wrong_session`), source (`wrong_endpoint`), window
(`replay`), then the tag (`bad_mac`).

Lab mode runs the same handshake on 127.0.0.1 with the transport's exact
filter (`net_bind=loopback`); `net_link=legacy` keeps the raw frames.

## 6. Reliable channels, timing

- Channels 1..3 (0 reserved for the link, 1 world, 2..3 game), 32-message
  windows both ways, 1024-byte messages, static storage. Per-channel RELACK
  (`next_expected`, a 32-bit bitmap, `free_slots`); a message is acknowledged
  only once stored; packet acks are for RTT statistics only. RTO =
  clamp(srtt + 4 rttvar, 100, 2000) (250 before the first sample), x2 per
  retry; at most 2400 bytes of retransmitted payload per 20 ms tick; while the
  peer advertises no free slot, one probe per second. A new session clears
  every channel on both sides. `partner_net::SendReliable` / `DrainReliable`;
  look stays a FRAME with its 2 s heartbeat (no v1 feature uses the channels).
- RTT: RFC 6298 from the packet ack and `ack_delay_ms`. Inbound loss: DATA
  sequence gaps over two 5-s buckets. Jitter: RFC 3550 on transform-carrying
  DATA. Offset: minimum transit (diagnostic).
- `partner_interp=auto`: target = clamp(frame + 2 jitter + loss pad + 10,
  min, max), loss pad one frame at >= 2% (off below 1%), two at >= 6% (one
  below 4%); the goal rises at once and falls only after 5 s of target + 15 <
  goal; the applied delay moves 1 ms per 20 ms tick. The driver reads it
  through the `interp_ms` hook (`partner_net::EffectiveInterpMs`; a null hook
  keeps `partner_interp_ms`). `AlignedDelayMs` is unchanged.
- `partner_lost=freeze` (default): while the link is not connected
  `SampleRemote` reports the held pose as `lost` (dm speed 0, stays
  realized). `hide` clears the realize bits after 10 s lost (`partner_lost=hide;
  after_ms=`), the next live sample re-realizes (`partner_lost=show`).

Threads and locks: Submit*/Sample* on the Lua thread inside the receipt lock;
the session and all send state under `g_tx_lock`; the receive state and the
reliable receivers under `g_lock`. Order: receipt lock, `g_tx_lock`, `g_lock`;
the receive thread never takes `g_tx_lock` while holding `g_lock`. No logging
or file I/O under either lock. At a new session the receive thread resets the
reliable senders, the reliable receivers and the receive state in the commit's
`g_tx_lock` section (taking `g_lock` inside it), so no packet of the new
session carries a RELACK of the old receivers; the game thread's send
sequences keep counting (the peer's reset receivers accept any first one).

## 7. Diagnostics

### 7.1 script-observer.log

```
link=config; mode=lab|play; role=host|guest; proto=2; bind=loopback|any|<ip>; port=%u; peer=<masked>:%u|learned; auth=code|lab; interp=fixed|auto
link=listening; port=%u
link=connecting; peer=<masked>:%u; attempt=%u             (attempt 1, then every 10th)
link=handshake; step=hello_rx|welcome_rx|confirm_tx|confirm_rx; peer=<masked>:%u   (per step change or 10 s)
link=connected; session=%u; peer=<masked>:%u; rtt_ms=%u; remote_build=%s
link=lost; after_ms=%u
link=reconnecting; attempt=%u
link=reconnected; session=%u; peer=<masked>:%u; gap_ms=%u
link=rebound; session=%u; peer=<masked>:%u
link=rejected; reason=%s; remote_proto=%u; remote_build=%s; verified=0|1
link=warn; reason=build_mismatch; remote_build=%s        (lab only)
link=disabled; reason=secret_missing|secret_bad_length|secret_bad_char|crypto|no_peer_addr|transport_<bind|winsock|socket|thread|bad_config>
link=drop; reason=malformed|wrong_session|wrong_endpoint|replay|bad_mac|rate|unknown_msg; count=%llu
link=stats; state=%s; session=%u; rtt_ms=%u; rttvar_ms=%u; jitter_ms=%u; loss_pct=%u.%u; resend=%u; tx_Bps=%u; rx_Bps=%u; interp_ms=%u; offset_ms=%d; rel_queued=%u; dup=%u; src_changed=%u; status_fail=%u; status_write_ms_max=%u
link=close; reason=peer
```

`link=drop` and `link=stats` come at most every 10 s and only when they
changed (`dup` counts replayed packets). Masking: 127/8, 10/8, 172.16/12,
192.168/16, 100.64/10 and 26/8 in full, any other address `a.b.x.x`.
`partner_net=listening; port=%u; peer_port=%u` (play host: the bound port and
`peer_port=0`; play guest: its ephemeral port), `partner_net=disabled;
reason=transport_*`, `partner_net=first_rx|look_rx|input_rx` and
`partner_net=stats` are byte-compatible with the lab.

### 7.2 `mgs-coop\link-status.json`

Written atomically (`.tmp` + `MoveFileExW REPLACE_EXISTING`) by the receive
thread within 1 s of a change and every 5 s; once by the starting thread when
the link is disabled. A failed replace (a reader without `FILE_SHARE_DELETE`)
is retried on the next check and counted in `status_fail`. Readers open it
with `FileShare ReadWrite, Delete` and check `pid`. One ASCII line, keys in
this order:

```
{"v":1,"pid":1234,"mode":"play","role":"host","state":"connected","reason":"","verified":1,"session":1,"peer":"203.0.x.x:51234","local_port":47830,"rtt_ms":42,"jitter_ms":3,"loss_permille":4,"resend":0,"tx_Bps":3900,"rx_Bps":3800,"interp_ms":80,"remote_build":"0.12.0","partner":"live","partner_shown":1,"look":1,"mission":30050,"saves":"redirected","time_utc":"2026-10-05T12:00:00Z"}
```

`state`: `off|disabled|listening|connecting|handshake|connected|reconnecting|lost|rejected`;
`reason`: a REJECT reason or a `link=disabled` reason; `verified` is 0 only for
an unauthenticated hint; `partner`: `none|live|stale|lost`; `partner_shown`,
`look` and `mission` come from `partner_net::PublishPartnerState` (game
thread); `saves` from `lab::SavesRedirected()`.

### 7.3 `lab_ini_check`

`lab_ini_check lab <file>` appends after its existing lines: `mode`,
`play_refused=0x%X`, `net`, `net_link`, `net_bind`, `net_listen_port`,
`net_peer_addr`, `net_peer_port`, `net_latency_ms`, `net_jitter_ms`,
`net_loss_permille`, `net_loss_model`, `net_burst`, `net_dup_permille`,
`net_reorder_permille`, `net_reorder_ms`, `net_seed`, `partner`,
`partner_interp`, `partner_interp_ms`, `partner_interp_min_ms`,
`partner_interp_max_ms`, `partner_lost`, `link_keys=1`. New modes:
`lab_ini_check secret <file>` prints `secret_status=ok|missing|bad_length|bad_char`;
`lab_ini_check code <text>` prints `code_status=<status>` and, when ok,
`host=` and `port=` (never the secret).

## 8. Constants

Play port 47830; lab ports 47810/47811 (emulator) and 47820/47821 (partner);
master dead port 47899; `kLinkMaxDatagram` 1200; lab secret
`C00PC00PC00PC00PC00P` (loopback only); code prefix `MGS1`; partner mission
30050.

## 9. Open risks for the live tests

- Windows Firewall: the first 0.0.0.0 bind (observer install) prompts for
  mgsvtpp.exe on both roles, possibly behind a fullscreen game; Block rules
  override Allow; a Public-profile network is not covered by the default Allow.
- NAT: no hole punching or relay; a host behind CGNAT needs a VPN (Tailscale's
  1280 MTU fits the 1228-byte HELLO on the wire, to be verified).
- Save gate timing: confirm that `SavesRedirected()` reflects DllMain at
  observer install and that the pass-through fallback engages before any save.
- Lab default change: v2 replaces the raw frames on 127.0.0.1 (handshake
  first, 20 ms idle cadence); re-run Test-PartnerSpike and Play-OfflineCoop;
  `net_link=legacy` exists for bisecting.
- CPU: the in-tree HMAC runs on the game thread per packet (measure
  `native_us`); status-file writes on the receive thread may stall under
  antivirus scanning (`status_write_ms_max`).
- `partner_lost=hide`, re-realize after a long loss and an epoch change while
  lost need a live pass.
