# World sync W0-W4: protocol and implementation

Implements phases W0 (plumbing), W1 (shared enemy outcomes and alert
escalation in free roam) and W2 (clock and weather) of
[world-sync-design.md](world-sync-design.md), plus the lab probes and a
lab-only mission jump. Level 3 also includes supported-scene host travel
(W3), forward-only reload alignment, shared death and checkpoint copying
(W4), described in section 13. Story missions (W5), full progression copying
and soldier transforms (W6) remain unimplemented.

W0-W2 ran live in October 5 testing. The earlier integrated October 6 run
`20261006-111828` verifies host title Continue, host-only Mother Base–ACC–
Afghanistan travel and accepted initial alignment on two processes on one
PC. That DLL's checkpoint copying and injected shared death also have live
receipts. Native host menu Continue reloads both copies and eventually
restores W1 once the host area becomes readable. The same run verifies
host-only Africa travel, alignment and independently moving field bodies.
See [recorded live results](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-10-05-world-sync.md). Automated
fixtures do not establish additional live acceptance. Script citations refer
to the extracted 1.0.15.4 scripts (`lib/...`, `mission/...`).

Code:

| Part | Files |
|---|---|
| Link DATAGRAM and hooks | `native/link_protocol.cpp`, `native/partner_net.cpp` |
| Core (pure, unit tested) | `native/world_wire.cpp` (op table, codec, queues, pairing, schedulers), `native/world_receipt.cpp` (receipts, trigger grammar) |
| Glue (Win32) | `native/world_link.cpp` (lock, hooks, pump, `world.trigger`), `native/game_script_observer.cpp` (receipt routing, install) |
| Lua (spliced into TppMain after `partner_sync.lua`) | `lua/coop/world_sync.lua`, `world_enemy.lua`, `world_outcome.lua`, `world_phase.lua`, `world_env.lua`, `world_probe.lua`, `world_goto.lua`, `world_follow_seed.lua`, `world_follow.lua`, `world_align.lua`, `world_recovery.lua`, ..., `world_vehicle.lua` (section 16), `world_vehicle_state.lua` (17), `world_vehicle_rules.lua` (19), ... |
| Config and tools | `lab.ini` `world` / `world_probe`, `tools/lab/Set-WorldTrigger.ps1`, `tools/lab/WorldSync.psm1`, `Play-OfflineCoop.ps1 -World`, `Play-Coop.ps1 -World` |
| Tests | `tests/world_wire_tests.cpp`, `tests/world_receipt_tests.cpp`, `tests/world_sync_lua_check.lua` (fragment and composed TppMain), additions to the link, lab config, host-trial patch and script suites |

## 1. Levels and gating

`lab.ini` `world=0..3`: the low-level lab/native config defaults to 0.
`Play-OfflineCoop` and `Play-Coop` default to 3; explicit `-World 0` opts out:

| Level | What runs |
|---|---|
| 0 | Inert: the native world module is not configured, no partner_net hook is installed, the world receipts are not classified (they stay literals), `world.trigger` is never read, no `world` log line. Lua makes exactly two loads per mission (the CFG query in OnInitialize and the CFG start in OnMissionGameStart), installs nothing and never ticks. |
| 1 | W0: HELLO, MISSION_READY, pairing and epoch, RECEIVED/REJECT, name maps and the roster digest, the observer (logging only), the probe runner, the lab goto. |
| 2 | Level 1 plus W1 in 30010/30020: enemy outcomes, the out-of-area union, alert escalation, digests and resync. |
| 3 | Level 2 plus W2 clock/weather, W3 supported-scene travel, and W4 checkpoint alignment/shared recovery. |

Native switch: `world_on = offline-v1 && partner=1 && net=1 && net_link=v2 &&
SavesRedirected() && world >= 1`. Otherwise, with `world >= 1`, the log says
`world=disabled; reason=partner_off|net_off|legacy_link|saves_not_redirected`.
The save gate holds in the lab too: world sync writes kills, staff (a fulton
recruits into both co-op saves, user decision D1) and mission clears, so it
never runs on unredirected saves.

Effective level: `eff = min(local, remote)`, computed natively at pairing,
pushed in PAIRED, used for native TX gating and by Lua. A remote HELLO with
`saves=0` is never paired.

Per mission (Lua `W.MISSIONS`): 30010 `{enemies, clock, weather=all,
areaEnemy=f30010_enemy}`, 30020 `{enemies, clock, weather=nofog,
areaEnemy=f30020_enemy}`, 30050 `{clock, weather=all}`; every other mission (the ACC 40050
included) runs the level-1 plumbing only for enemy/environment sync. W3/W4
travel and shared game over also support 40010/40020/40050.

W1 is off for the rest of the mission (W2 continues) after a continue or
checkpoint reload (`TppMission.IsMissionStart()` false at the CFG start), a
game over (`svars.mis_isDefiniteGameOver` or `mvars.mis_isReserveGameOver`
seen in the tick), a roster mismatch (PAIRED `match=0`, or duplicate hashes),
or missing or invalid W1 bindings. At negotiated level 3, W4 lifts the
continue/reload gate only after a validated host snapshot (section 13).
Lower levels retain the original continue gate. Shared death keeps W1 off
until the host chooses Continue/restart and the new epoch aligns.

## 2. Link use (docs/link-protocol.md)

- Reliable channel 1 (reserved for world until now) carries every R world
  bundle (at most 1024 bytes, one `SendReliable`).
- DATA message type 5 DATAGRAM (`u8 channel 1..3 | u8 0 | u16 0 | payload <=
  512`) carries the U bundles on channel 1; `partner_net::SendDatagram` sends
  one DATA packet with the datagram first, then due RELACKs and new reliable
  data. An older reader skips type 5 by its length. Channels 2..3 stay unused.
- `partner_net::WorldHooks` (all on the receive thread, no partner_net lock
  held): `on_datagram` (DATAGRAM channel 1), `on_idle` (end of every LinkIdle,
  about every 20 ms), `on_session` (after a new session was committed). With
  null hooks (world 0) nothing changes.
- Mixed levels: a level-0 peer never drains channel 1; its 32-message receive
  window fills and our sender reports `full`. HELLO and READY are sent only at
  a mission start and on LINK up, so this stays small; native logs
  `world=remote_silent` once when no HELLO arrives within 30 s of link up.

## 3. Bundle and ops

```
bundle: u8 0x57 'W' | u8 version 1 | u8 count | u8 flags 0 | op x count
op:     u8 op | u8 len | u16 epoch | body[len]       (little-endian)
```

Decoding: a wrong magic or version, nonzero flags, a count/len overrun or
trailing bytes drop the whole bundle (`bad_bundle`); an unknown op is skipped
by `len` (`unknown_op`); a `len` that is not the schema size, a field out of
range or a local op on the wire drops that op (`bad_op`); an op not allowed
for the receiver's role is dropped (`wrong_dir`); an op above the receiver's
own level is dropped (`level`), and a request above it gets an automatic
REJECT 10. Hashes are u32 `Fox.StrCode32` (normalised to unsigned), positions
i32 millimetres.

Control (queue 0):

| Code | Name | Dir | Lua fields | Native fields | Body |
|---|---|---|---|---|---|
| 0x01 | HELLO | both | missionCode, locationCode, rosterDigest, soldierCount, cpCount | level, role, proto 1, epoch, saves, build | 24 |
| 0x02 | MISSION_READY | both | missionCode, rosterDigest, soldierCount, cpCount | epoch (host current; guest adopted or 0) | 14 |
| 0x03 | RECEIVED | H->G | (native only) | rid, reqOp | 3 |
| 0x04 | REJECT | H->G | rid, reqOp, reason 1..15, entHash, state 0..7, ver | - | 11 |
| 0x05 | RESYNC | G->H | rid, what 1..7 (1 outcomes, 2 phases, 4 clock + weather) | - | 3 |
| 0x06 | RESYNC_END | H->G | rid, what, count, outcomeSeq (queued as a world op, see below) | - | 7 |
| 0x07 | MISSION_END | both | missionCode | epoch | 6 |
| 0x08 | PROBE_SYNC | both (lab) | probeId 1..18, step 1..4, entHash, arg | atMs (step 1) | 14 |

REJECT reasons: 1 unknown_entity, 2 terminal, 3 not_forward, 5 epoch, 6
not_paired, 7 roster_mismatch, 8 bad_state, 9 busy, 10 level, 11 inactive (4
reserved).

RESYNC_END has a control code but travels like a world op (queue 1,
epoch-checked, sent only while paired), so the guest handles it after every
ENEMY_OUTCOME the host sent before it and its `outcomeSeq` matches what the
guest counted.

World (queue 1):

| Code | Name | Dir | Wire | Lua fields | Rx | Body |
|---|---|---|---|---|---|---|
| 0x10 | ENEMY_OUTCOME | H->G | R | entHash, entType 1, state 0..7, ver 1.., cause 0..15, actor 0..2, rid, flags 0..3 (1 resync, 2 backward), x, y, z | - | 25 |
| 0x11 | REQ_OUTCOME | G->H | R | rid 1.., entHash, entType 1, state 1..7, seenVer, cause, attackId, isHeadShot, x, y, z | - | 28 |
| 0x12 | PHASE | H->G | R | cpHash, phase 0..3, ver 1.., keep 0, rid, flags 0..1 | - | 11 |
| 0x13 | REQ_PHASE | G->H | R | rid 1.., cpHash, phase 1..3, cause, soldierHash, x, y, z | - | 24 |
| 0x14 | PLAYER_AREA | both | R | area 0..15, missionCode | - | 5 |
| 0x15 | DIGEST | H->G | U | tick, clock 0..86399, rateX100, weather 0..5/255, outcomeSeq, outcomeVerSum, outcomeCount, cpCount, inGame | latencyMs | 22 |
| 0x16 | GUEST_DIGEST | G->H | U | tick, pendingCount, outcomeSeqSeen, outcomeVerSum, outcomeCount, cpCount, inGame, clock, weather | - | 22 |
| 0x17 | PHASE_VEC | both | U | cpCount 0..192, first 0, nwords 1..12, w1..w12 (host: local phases; guest: own phases) | - | 53 |
| 0x20 | CLOCK | H->G | R | clock 0..86399, rateX100, cause 1..4 (jump, fast, resync, fast_end), tick | latencyMs | 11 |
| 0x21 | WEATHER | H->G | R | weatherType 0..5/255 (release), interpTime 0..600, fogDensityMilli 65535, fogType 255, cause 1..3 (change, resync, digest) | - | 7 |
| 0x22 | ENEMY_DEBUG | H->G | R | seq 1.., entHash, kind 1..8 (warp, route, correct, come, ignore, notice, disable, enable), x, y, z mm (+-1e8), yaw mrad (+-8000), flags 0..3 (1 come, 2 restrict notice), routeHash, hostGameObjectId | - | 30 |
| 0x23 | ENEMY_DEBUG_ACK | G->H | R | seq 1.., entHash, result 0..1 (applied, refused), reason 0..31, errCm, frames | - | 12 |

`latencyMs = min(65535, rtt/2 + time queued on the receiver)`. Life-state
wire enum: NORMAL 0, HOLDUP 1, SLEEP 2, FAINT 3, DYING 4, DEAD 5, FULTONED 6,
VANISHED 7; ranks 0 1 2 2 3 4 5 5; DEAD, FULTONED and VANISHED are terminal.
`cause`: 0 unknown, 1 Dead, 2 Neutralize, 3 Unconscious, 4 Dying, 5 Fulton, 6
request, 7 probe, 8 resync, 9 sweep. `actor`: 0 host, 1 guest, 2 probe. Phase
wire values 0 SNEAK, 1 CAUTION, 2 EVASION, 3 ALERT.

Local ops (never on the wire): 0x70 LINK{state, session}, 0x71
PAIRED{epoch, mission, remoteRoster, remoteSoldiers, match, eff}, 0x72
UNPAIRED{reason: 1 session, 2 remote_mission, 3 epoch, 4 remote_end, 5 level0,
6 local_end}, 0x73 GOTO{step, target, lz}, 0x74 PROBE{id, arg}, 0x75
PROBE_FIRE{id, step, ent, arg}, 0x76 RESYNC_NEEDED{what}, 0x77 LEVEL{0}, and
0x78 GOTO_ACK{step, result} (Lua -> native: 0 refused, 1 reserved, 2 waiting).
W3/W4 use codes 0x09-0x0F and 0x18-0x1C (section 13). Spawn sync (F9 guard
placement and per-soldier debug actions, [debug-menu.md](debug-menu.md)
"Spawn sync") uses 0x22-0x23 at level 2 (paired, eff >= 2, roster match; not
W1). Codes 0x1D-0x1F and bundle flag bit 0 (chunking) remain reserved.

Pair-wait reason (no new local op; the local op table and queue sizes are
unchanged): the core reports why it is not paired as the first failing
pairing gate, in TryPair's order: 0 `paired`, 1 `off` (local level 0), 2
`no_link`, 3 `no_hello` (no HELLO on this link session), 4 `remote_off`
(remote level 0), 5 `remote_saves`, 6 `same_role`, 7 `local_loading` (no
valid local READY), 8 `remote_loading` (no valid remote READY/HELLO
mission), 9 `different_mission`, 10 `epoch` (the host's READY epoch or the
guest's adopted host mission does not match yet). Published views add 11
`settling`: `different_mission` is published as `settling` until the same
(local, remote) pair has lasted 15 s without interruption; the first
published `different_mission` of a pair posts one note 16 toast on that PC.
Any other value reads `unknown`. CFG r[11] carries the raw code;
`link-status.json`, the HUD, F9 and the worker `state.json` carry the
published one. The partner's scene comes from the same gates: its mission
(the host learns it from the guest READY, the guest from the host HELLO or
READY; cleared by a matching remote MISSION_END or a new session; 0 while
the link is down), its HELLO world level, and its HELLO location (both
roles). The core logs one line per change of (reason, local mission, remote
mission) while the local level is >= 1, the link is up and it is not
paired; pairing or a link loss resets it:
`world=pair_wait; reason=<name>; local=<mission>; remote=<mission>;
remote_level=<0..3>; t_ms=`.

## 4. Queues, pairing, epoch

Static queues (no allocation after configure): control 256 wire entries plus
32 reserved for local ops; world 4096; one latest slot each for DIGEST,
GUEST_DIGEST and PHASE_VEC (a newer tick replaces, an older one counts
`stale_u`; epoch-checked; delivered once after the world queue is empty);
outbound 2048 encoded R ops with a generation counter (a commit after a clear
removes nothing). World queue full: a host REQ_* gets REJECT 9 busy,
everything else is dropped; `resync_needed` is set and RESYNC_NEEDED{7}
pushed once until the guest sends RESYNC or the next PAIRED. Outbound full:
TX `0,6`. Drops are counted per queue and logged as `world=overflow;
queue=control|world|out; dropped=N`: the first at once, later ones at most
every 10 s (the next timer tick reports drops that were not logged at once,
such as a local op with the control queue and its reserve full).

Pairing: the host epoch is `epoch % 65535 + 1` at every CFG start (never 0).
The host pairs on a guest READY for its own mission carrying its epoch (both
levels >= 1, remote saves 1); a guest READY for the same mission with another
epoch makes the host re-send its READY (at most once per 2 s). The guest pairs
on a host READY or HELLO for its own mission, adopts that epoch and re-sends
its READY when it carried another one. Unpaired by a new session (all queues
cleared except pending local ops, generation bumped; LINK{1} then
UNPAIRED{1}), a remote READY for another mission (2, world ops of other
epochs purged), a remote MISSION_END (4), a local CFG start or MISSION_END
(6), or `world=0` (5, with LEVEL{0}). A CFG start also drops every queued
control and world op of the previous mission. World ops are kept only with
the latest host epoch (the guest learns it from host READY/HELLO; it survives
a new session); the host accepts guest world ops only while paired with the
matching epoch (else REQ_* get REJECT 5). Lua drains world ops only while
paired. TX gating: world ops `0,5` unless paired; W1 ops need eff >= 2, W2
ops eff >= 3 (`0,7`); role direction `0,4`; PROBE_SYNC/GOTO_ACK in play `0,8`.

Threads: `g_world_lock` (SRW) guards the core and is a leaf (no partner_net
or coop_hud call, no logging, no I/O under it); lines are formatted by the
core and written after unlock, and the world view
(`partner_net::PublishWorldState`, atomics only) is published after unlock at
the end of every idle tick, at configure and after each NOTE. The receive thread drains reliable channel 1 (32 per
idle), pumps, tracks the link state and runs the timers; the pump has one
owner at a time (`InterlockedCompareExchange`) and commits a bundle only after
`SendReliable` returned `queued`. Lock order: receipt lock -> (world lock,
released) -> `g_tx_lock` -> `g_lock`. The observer calls
`world_link::MissionRuntime` next to `PartnerMissionStart` and
`world_link::LocalMissionEnd` next to `PartnerMissionEnd` (the
`termination_returned` branch).

## 5. Receipts

| Receipt | Source | Reply | Threads |
|---|---|---|---|
| CFG | `return 'MGS_COOP_WORLD_CFG_V1' -- <mission> <phase>` (0 query, 1 start) | `return level,role,proto,link,epoch,probe,mode,session,remote_mission,remote_level,wait,handoff,lock,census,vehicle_gone,companion,relief` (17 values; the first eight are unchanged: mode 1 lab, 2 play; session is local link generation, zero disconnected; the start consumes a scheduled probe. r[9] the partner's mission, 0 unknown: no link, no HELLO yet, or the partner between its MISSION_END and its next READY/HELLO; r[10] its HELLO world level 0..3, 0 unknown; r[11] the raw pair-wait code 0..10, section 3, never `settling`. Join-fix phase 2: r[12] the save handoff code (0 none/off/host, 1 wait_host, 2 receiving, 3 staged or countdown, 4 exiting, 5 kept, 6 failed or refused; Lua `pending` = 1..4, `busy` = 2..4) and r[13] the travel lock (1 when it may apply: the effective `lab.ini` `travel_lock`, the F9 unlock not set, guest role; else 0), both filled by `world_link::HandleCfg` after the core from the save handoff's published state. r[14] the WS4 attacker census 0/1 (lab key `partner_placed_census`, lab only) and r[15] the vehicle modules bitmask 0..63 (bit 1 the 0.13.6 VEHICLE_GONE module, lab key `world_vehicle_gone`, section 16; bit 2 the M4 VEHICLE_STATE module, lab key `world_vehicle_state`, section 17; bit 4 VEHICLE_GONE's wreck parity, lab key `world_vehicle_wreck`; bits 8 / 16 / 32 the vehicle decisions of 2026-10-29, lab keys `world_vehicle_break`, `world_vehicle_credit`, `world_loadout_block`, section 19; lab and play), r[16] the M1 host-owned companion module 0/1 (lab key `companion_host_owned`, lab and play; section 15) and r[17] the M3 sortie vehicle module 0/1 (lab key `world_relief`, lab and play; section 18), also from `HandleCfg`. Both phases fill them; Lua treats a missing r[9]/r[11] as an older loader and a missing r[12]..r[17] (or an out-of-range r[15]) as 0) | query any; start runtime, not terminating |
| TX | `return 'MGS_COOP_WORLD_TX_V1' -- <op> <v1> ... <vn>` | `return 1` / `return 0,<reason>`: 1 syntax, 2 unknown_op, 3 bad_args, 4 wrong_role, 5 not_paired, 6 queue_full, 7 disabled, 8 lab_only | runtime; MISSION_END also termination |
| RX | `return 'MGS_COOP_WORLD_RX_V1' -- <mask>` (1 control; 3 control, world, U slots) | `return queue,op,v...` or empty `return 0,0,flags,qctrl,qworld,epoch` (flags 1 link, 2 paired, 4 resync_needed, 8 level0) | runtime |
| LOG | `return 'MGS_COOP_WORLD_LOG_V1' -- <payload>` (1..160 bytes) | `return true`; logs `world_lua=...` (partner sanitizer, `;` -> `; `; 0.13.6: a token bucket of 600 lines refilled at 20 lines/s, the old 200 per 10 s on average with room for a mission arrival's burst; the dropped count is logged as `world=log_cap; dropped=` before the next admitted line) | runtime or termination |
| WARP | `return 'MGS_COOP_WORLD_WARP_V1' -- <seq> <goId>` (guest spawn sync) | `return {r=,h=,leaf=}` or `return {refused="<reason>"}`: one native placement of the grant the core recorded when the guest's RX popped ENEMY_DEBUG `seq` (kind warp/correct, same epoch, at most 15 s old); the target never comes from Lua ([debug-menu.md](debug-menu.md) "Spawn sync") | runtime |
| NOTE | `return 'MGS_COOP_WORLD_NOTE_V1' -- <code> <a> <b>` (code 1..15; a, b 0..4294967295, no sign) | `return true` (handled, or skipped by the rate gate) or `return false` (a code with no meaning yet: 13..15). Codes: 1 PROGRESS{a title-mode ACC mission, b 1 story start, 0 not}, 2 HELI_WAIT{a target, 0}, 3 HELI_ALONE{a target, b reason 1 no_partner, 2 partner_elsewhere, 3 repeat, 4 timeout, 5 join_partner: the target is the partner's own mission (online 2026-10-22 P7)}, 4 HELI_REFUSED{a unsupported target or nextMissionId, 0}; join-fix phase 2: 5 TRAVEL_LOCKED{a host mission, b target}, 6 HOST_PARTNER_CANNOT_JOIN{a target, b partner mission}, 7 TITLE_CONTINUE_LOCKED{a host mission, b 1 location_differs, 2 story_start, 3 waiting_for_host_save, 4 host_save_checked (waiting, the host in game in 30010/30020/30050; online 2026-10-22 P3)}, 8 TRAVEL_ALONE{a target, b 1 repeat, 2 f9_unlock, 3 timeout}, 9 CONTINUE_DURING_HANDOFF{a pct 0..100, 0}, 10 HANDOFF_KEPT_OWN{0, 0}, 11 GUEST_LANDING_ELSEWHERE{a target, b host mission}, 12 SORTIE_HELD{a target, b host mission}; native alone posts 16 MISMATCH{a local, b partner mission} (section 3). Rate gate per code: the same (a, b) posts at most once per 1 s and logs `world=note; code=; a=; b=; partner=; t_ms=` at most once per 10 s (`world=note; code=; result=unknown; t_ms=` for 13..15). Code 1 records the title progress (b 1 story_start, else ok) and posts only for b 1, and not while a save handoff runs for this guest (`save_handoff::GuestHandoffActive`: guest, `save_handoff=scene` or `always`, a host STATE seen; the handoff toasts replace it, the log line stays); 2..12 post a toast with the partner's and the local mission (sentences in `coop_text::FormatWorldNote`). Lua ignores the result | runtime or termination |
| SAVE | `return 'MGS_COOP_WORLD_SAVE_V1' -- <op> <a> <b> <c> <d> <e>` (exactly 6 integers, no sign; join-fix phase 2, [save-handoff.md](save-handoff.md)): op 1 REPORT{a mission 0..99999, b title 0/1, c story 0 unknown, 1 opening, 2 open, d saving 0/1, e ingame 0/1}, sent on any change, otherwise every 1000 ms while active; op 2 CONTINUE{a target 1..99999 (vars.missionCode at ReservePlayerLoadingPosition k==4), b title, c story, 0, 0}; op 3 DECLINE{a 1 second_continue or 2 f9, 0, 0, 0, 0} | `return true` (accepted), `return false` (the module is off, or rate-gated: more than 4 per second; a DECLINE is never rate-gated), the literal when malformed. `save_handoff::HandleLua` only parses and stores (the report in a seqlock, CONTINUE and DECLINE in a small mailbox) for the link thread. Lua ignores the result | runtime |

Exact classification (name == source, NUL-free, at most 240 bytes). Integers
only: an optional `-` (i32 fields), no `+`, no leading zeros, no `-0`, no `.`,
single spaces; u32 up to 4294967295. Replies fit 224 bytes (17 values). TX,
RX, LOG, WARP, NOTE and SAVE are quiet (no entry/return lines); CFG logs like any receipt. A
refused receipt stays literal and Lua treats it as inactive or not sent.

## 6. Configuration and the trigger

`lab.ini`: `world=0..3` (lab and play; other values count as invalid and keep
the previous value) and `world_probe=none|p1|p3|p3b|p4|p12|p17|race_kill|
remote_area_kill|kill_near|sleep_near|fulton_near|alert_near|weather_rain|
clock_jump|clock_fast|shared_death|host_continue|shared_checkpoint` (ids
1..18 in this order after `none`; lab only: play forces none,
sets `play_refused |= 0x8000` and counts one invalid value). `lab_ini_check
lab` prints `world=` and `world_probe=` after `link_keys=1`.

Launchers: `Play-OfflineCoop.ps1 -World 0..3 [-WorldProbe <probe>]` adds
`world=N` (and `world_probe=`) to both lab.ini files when the level is above
0; default 3 enables shared time/weather and travel. Console key
`[G]` sends `goto=30010` to the host at `-World 3`, letting W3 bring the
guest along. At levels 1/2 it sends to both copies for independent manual
travel; at level 0 it sends nothing. `Play-Coop.ps1 -World 0..3` adds
`world=N` after the play partner keys (default 3, never `world_probe`).
`Prepare-LabInstance.ps1 -Offline -PartnerSetting world=3` works too.

`<game>\mgs-coop\world.trigger` (lab only: honoured with `world_on`, lab.ini
present and `mode=lab`; never read in play): one ASCII line of at most 255
bytes, blank-separated `world=0..3`, `world_probe=<probe>|off`,
`goto=30010|30020`, `goto_lz=0|1` (only with goto), each at most once. Native
reads it at most once per second from the CFG start and RX receipts (so only
while a mission with world sync runs), deletes it, then logs
`world_trigger=applied; text=<canonical>` (order world, world_probe, goto,
goto_lz) or `world_trigger=rejected; reason=token|repeat|empty|too_long|
not_lab|level0`. Effects: `world=0` at once (LEVEL{0} + UNPAIRED{5}; Lua runs
its stop cleanup and stays off), `world=1..3` at the next CFG start,
`world_probe=` now when a mission runs with world sync active, else at the
next CFG start (`off` cancels a pending one; a probe at level 0 is
`level0`), `goto=` see section 8. `tools/lab/Set-WorldTrigger.ps1 -Role
host|guest|both -Text <line> [-WaitSec] [-Force]` writes it atomically and
confirms the result line (exit 0 applied, for a goto `world_goto=armed`; 2
not consumed; 3 rejected, a goto `world_goto=refused`, or a different command;
4 unconfirmed); `<run>/world-triggers.log` records it. Start-Lab -Offline and
Stop-Lab delete leftovers (`stop.json world_triggers_removed`).

## 7. Lua contract

Eleven chunks, each one `do ... end` block of at most 16000 bytes (CMake fails
the configure otherwise), spliced in this order directly after
`partner_sync.lua` and before the marker lifecycle chunk, so world's wrappers
sit inside the session lifecycle's and outside partner's: `world_sync.lua`
(receipts, `W.n`, required bindings, maps and digest, observer, control
dispatch, tick, error policy, stop), `world_enemy.lua` (lattice, apply,
commits, sweep, ledger), `world_outcome.lua` (attribution, guest requests and
held requests, host request validation, outcome wire handlers),
`world_phase.lua` (W1 bindings, area union, phase cache, escalation, floor,
PHASE_VEC), `world_env.lua` (digests, resync stream, stats, clock, weather),
`world_probe.lua` (probes), `world_goto.lua` (lab goto),
`world_follow_seed.lua` (required seed schema and coverage), `world_follow.lua`
(mission travel), `world_align.lua` (checkpoint enemy alignment),
`world_recovery.lua` (shared death and accepted checkpoint saves). Everything lives on
`W = e.MgsCoopWorld`; hook lists `bindHooks`, `startHooks`, `stopHooks`,
`pairedHooks`, `unpairedHooks` and `observers` are the extension points.

Lifecycle: OnInitialize (after the original) resets the area state, sends the
CFG query and, at level >= 2 in 30010/30020, wraps `UpdateOutOfAreaSetting`
of `subScripts.enemy` and of the global `f300x0_enemy` (once per table).
OnMissionGameStart (after) resets the mission state, sends the CFG start,
resolves the bindings, builds the maps, turns W1 off after a continue, sends
HELLO and READY and schedules a probe at t0 + 10 s. OnUpdate (after) calls
`pcall(W.tick)` only while active. OnMessage observes after the original
(skipped when it returned `Mission.ON_MESSAGE_RESULT_RESEND`). OnTerminate
(before the original) runs the stop: MISSION_END, weather force cancelled,
random weather unpaused (even after an error), `event=stop`. Any Lua error in
the tick, the observer or a wrapper body logs one `event=error` and turns
world sync off for the mission; the original callbacks always run, and their
errors, returns and yields pass through.

Maps: CPs are the sorted string keys of `mvars.ene_soldierDefine`; soldiers
the sorted `ipairs` part of each (string keys such as `lrrpVehicle` and
`lrrpTravelPlan` are skipped). `W.h(name)` is the unsigned StrCode32; message
comparison uses the raw values. Roster digest `d = (d*31 + hash) % 2^32` over
CPs and their soldiers. `W.n(v)` formats an integer with `%.0f` (-0 -> 0) or
returns nil; then the op is not sent and `event=bad_value;op=;field=` is
logged (at most every 10 s).

Bindings: required `loadstring`, `vars`/`mvars`/`svars`, `Fox.StrCode32`,
`Time.GetRawElapsedTimeSinceStartUp`, `GameObject.SendCommand`/
`GetGameObjectId`/`NULL_ID`, `TppMission.CheckMissionState`/`IsMissionStart`
(and `Mission.ON_MESSAGE_RESULT_RESEND`, without which the observer is off).
W1: `TppEnemy.GetLifeStatus`/`IsRecovered`/`GetPhase`/`SetOutOfArea`,
`Tpp.IsLocalPlayer`, `TppGameObject.NPC_LIFE_STATE_{DEAD,DYING,SLEEP,FAINT}`
(four distinct integers in 1..4, else `event=inactive;reason=bad_const;...`)
and `PHASE_{SNEAK,CAUTION,EVASION,ALERT}`. W2: `TppWeather.ForceRequestWeather`/
`CancelForceRequestWeather`, `WeatherManager.PauseNewWeatherChangeRandom`,
`DemoDaemon.IsDemoPlaying`, numeric `vars.clock`/`vars.weather`.

Clock: every world timer reads `W.now()`, the raw clock
(`Time.GetRawElapsedTimeSinceStartUp`, ms) plus `W.held`. The game holds the
raw clock still under the MISSION FAILED menu and the ESC pause menu while
`OnUpdate` keeps running (hidden run `65da90ce`, a lab-only census: the host's
raw clock stayed at 67517 ms for 33 s of game over while `os.clock` and the
update count advanced; about 6 s of ESC pause held it at 52469 ms, and it
resumed from there without a catch-up jump; `Time.GetFrameTime` reads a fixed
16.683 ms even while paused). Under the menu that froze the lab
`host_continue` probe (scheduled, never started), the 1 Hz digests, the 60 s
stats, follow retries and the sortie holds until the mission reloaded.
`W.clockTick` runs once per update, before the world tick: when the raw clock
has not moved since the previous update it adds the wall time since then
(`os.clock`, at most 250 ms per update) to `W.held`; when the raw clock moves
again it pays back any catch-up beyond the wall time. `W.now()` stays
monotonic and is unchanged while the raw clock runs; without `os.clock` the
raw clock alone drives it. World timers therefore also keep wall time during a
local ESC pause, as the partner's game does (digests, downed status and their
freshness windows, holds); before, a paused copy stopped sending until it resumed. Logs: `event=raw_clock;step=held;raw_ms=` once a
hold passes 1 s, then `event=raw_clock;step=resumed;held_ms=;paid_back_ms=`.

Observer: `event=obs;m=<kind>;ent=<hash|0>;id=;att=local|buddy|other|none`
for Dead, Neutralize, Unconscious, Dying, Conscious (log only), Fulton,
FultonInfo, FultonFailed, Holdup and ChangePhase (Damage is recorded, not
logged; at most 4000 lines per mission, then `event=obs_cap`). Argument
orders: `Dead(id, attacker, attackId)`, `Damage(id, attackId, attacker)`,
`Neutralize(id, attacker, ...)`, `FultonInfo(id, fultoner)`,
`ChangePhase(cpId, phase)`. `local` = `Tpp.IsLocalPlayer`, the local horse or
walker gear; `buddy` = D-Dog or Quiet.

Attribution (guest requests): Dead and Neutralize count when the attacker is
local or a buddy; FultonInfo when the fultoner is; Unconscious, Dying and
attacker-less Dead/Neutralize count only within 10 s of a Damage/Dead/
Neutralize by the local player on that soldier; a lab probe override counts
for 10 s. Anything else is `event=divergence;why=not_attributed`.

Fulton: the `Fulton` message only marks the entity fulton-pending (until
FultonInfo, FultonFailed or 20 s); FULTONED is read only from
`TppEnemy.IsRecovered`; an applied FULTONED waits while a local fulton is
pending, then `RequestForceFulton` once, read back until +20 s, retried once
at +5 s only if no Fulton message came.

Apply rule (both PCs): `known` = the state of the highest ver seen (a
reject/resync value always replaces it). A non-number `GetLifeStatus` is
UNKNOWN (held as `unreadable`, never committed). The entity's CP must be
active (else held `out_of_area`); a local terminal state is never commanded
(`terminal_local`); NORMAL is `skip_backward`; a local rank at or above the
target is `noop`; SLEEP/FAINT/DYING/DEAD use `ChangeLifeState` with the
validated constant (read back from +200 ms, one retry at +1 s, failed at +2
s -> held `apply_failed`). An apply for a soldier whose readback is still in
flight waits (retried once per frame). Ledger: held entries are re-checked 10
per frame (all of them when the active CPs change) and dropped at stop, at an
epoch change, at UNPAIRED 2 and when W1 goes off.

Host commits (before the world drain each tick, C32): pending reads from
life messages and a sweep of 4 entities per frame whose committed state is
neither NORMAL nor terminal and whose local readback once reached it
(`reached`): a higher local rank commits forward, a lower one (only if
reached) commits backward with flags 2 (cause 9 from the sweep). A guest or
probe commit clears `reached` until a readback reaches the committed state,
so a late or failed apply never reverts the guest's outcome. A guest
REQ_OUTCOME is validated after that: W1 off 11, unknown 1, terminal 2, not
forward 3; otherwise committed with actor 1 and the rid, broadcast and
applied to the host world. Guest: a request whose answer cannot come is held
(at most 256) and re-checked after the next RESYNC_END (sent again only while
still forward of the known state): one made while unpaired or whose TX failed,
the outstanding ones at UNPAIRED (`event=req_hold;n=`), REJECT 5, 6 and 9, and
no RECEIVED within 3 s; the last two also send RESYNC 1 (at most once per 10
s). REJECT 2 and 3 apply the host's state. Re-pairing in the same mission:
the same host epoch keeps everything; another host epoch is another host
world (the host restarted the mission), so W1 goes off for the guest's mission
(`event=inactive;reason=epoch;scope=w1`, held requests dropped) while W2 goes
on.

Area union: the wrapper records the local area at any time (traps fire during
preparation). Once started, paired (`W.paired`) at eff >= 2 with W1 on and with a known
partner area that differs, the original runs once with `enemy.cpList[local]`
replaced by the union set (restored afterwards, errors re-raised), so nothing
is set out of area and back. Active CPs = local area (or union) plus
`quest_cp`. PLAYER_AREA goes out at PAIRED and on a local change (at most once
per 2 s; a pending change after the window); a received area re-runs the
original (at most once per 2 s). UNPAIRED forgets the partner area and
re-runs the original for the local area at once (W1 off or stop: at the next
tick), so vanilla deactivation resumes. Group shapes (keyed set or array) are
normalised and logged once (`event=area;shape=set|array`).

Alert (escalation only): the host broadcasts every local upward transition
(`event=phase;cp=;phase=;ver=;src=host|req|floor;rid=`); decays only show in
its PHASE_VEC. REQ_PHASE: `SetPhase` when above the current phase (the next
broadcast carries the rid; without one by +500 ms the readback is
broadcast), else the current phase with the rid. The guest applies a PHASE
op or a vector value above its local phase (vector: at most once per 5 s per
CP); a local upward transition above the host's phase raises `ownPhase` and
sends REQ_PHASE when higher than the last request (or 30 s later); a decay
lowers `ownPhase` and never requests; applying a host phase never raises it.
The host floors a CP to the guest's own phase from the guest vector or a
REQ_PHASE (at most once per 10 s; `event=floor` every 10 s while any floor is
set), only while paired at eff >= 2 and while the guest reported that phase in
the last 5 s; UNPAIRED (which also sets eff 0) and stop clear the guest's
phases. No mutual
ALERT latch. Never used: `SetIgnoreCoop`, the soldier `ChangePhase`,
`SetKeepAlert`/`SetKeepCaution`. A vector packs 2 bits per CP, 16 per u32
word (arithmetic only); more than 192 CPs disables it
(`event=phase_vec;disabled=1;cps=`).

Digests (1 Hz while paired at eff >= 2): DIGEST/GUEST_DIGEST plus the phase
vector. The guest counts a mismatch when its seen outcome sequence equals the
host's but the ver sum or count differs, or when the sequences differ for more
than 10 s; three in a row send RESYNC 1 (at most once per 10 s;
`event=digest_mismatch;what=outcomes;n=3`). The host's resync stream sends at
most 32 ops per frame (outcomes with flags 1 and cause 8, phases with flags 1,
CLOCK cause 3, WEATHER cause 2), then RESYNC_END. `event=stats;...` every 60 s.

Clock (eff >= 3, 30010/30020/30050): every clock is `floor(v) % 86400`. The
host compares each frame's clock delta with `20 x min(dtWall, 0.25)`: more
than 120 game s off is a jump (CLOCK cause 1; a long frame gap is not); a rate
above 40x over 250-ms windows is fast mode (CLOCK cause 2 at most 4 Hz) until
1 s at or below 30x (cause 4). The guest sets `vars.clock` to `clock +
rate/100 x latency` on a CLOCK op, and from digests only after two in a row
more than 60 s off; suspended in demos, outside the game
(`CheckMissionState()` not true) or while the host digest says not in game.
`event=clock;cause=;diff=;applied=1`, `event=clock_check;diff=` every 10 s.
A guest cigar is reverted (host authority).

The October 6 source update observes the host's public `TppClock.SetTime`
after the original call, sending explicit changes immediately even below
the jump threshold. Host-only level-3 wrappers preserve returns, errors and
yields, and restore only their own function identities on teardown. Guest
CLOCK/WEATHER commands received during a demo or unavailable gameplay keep
the latest value until gameplay resumes; fresh digests replace stale held
values. Link loss and level 0 discard held controls and release our forces.

Weather (eff >= 3; 30010, 30020, 30050): the host sends `vars.weather` on change
(30020: FOGGY becomes 255 = release; the mist is local to an area); the guest
calls `TppWeather.ForceRequestWeather(type, interp)` (nothing during demos), a 255
cancels its force and unpauses random weather, and it re-forces after three
digests that disagree (after interp + 5 s). Random weather is paused at PAIRED
and every digest while the host weather is 0..5. The same userId and FORCE
priority as the game's own forces: forces are cancelled and random weather
unpaused at stop, on LEVEL 0 and after an error.

The same source update observes host `ForceRequestWeather` and cancel calls.
Explicit force commands carry their requested interpolation (bounded to
0..600 s); ordinary weather changes use 20 s. A bounded pending intent covers
the engine's asynchronous weather readback, then yields to actual host
weather. Failed queue writes retry. `event=weather_readback;type=;matches=1`
confirms actual guest `vars.weather`, separately from API application.
`world_env_game_lua_check` exercises the pinned retail Clock/Weather modules;
these new paths have fixture evidence, with live verification pending.

## 8. Lab goto (supported free roam/ACC -> 30010/30020)

At World 3, use `Set-WorldTrigger.ps1 -Role host -Text 'goto=30010'`
(or target `30020`); the guest follows host travel through W3. Do not send
a separate goto to the guest. At levels 1/2, use `-Role both` so each copy
travels independently. The following native goto steps describe the copy
that receives the trigger; only the host receives it in the World 3 recipe.

Trigger `goto=30010|30020 [goto_lz=0|1]`, refused (`world_goto=refused;
reason=level|mission|target|busy|terminating`) unless world sync is on at
level >= 1, the copy stands in 30050, in free roam 30010/30020 or in an ACC
40010/40020/40050 (memory and the last CFG start), no goto runs and the
mission is not terminating. In free roam step 1 is
`TppMission.ReserveMissionClearOnRideOnHelicopter()` (`call=ride_heli`), so a
continued free roam (W1 off) starts fresh. Lua also refuses
unless the CFG mode is 1. Armed in the ACC (a lab save that continues into the
helicopter) it skips step 1: `world_goto=armed; target=; lz=; from=acc` and
GOTO{2} at once (60 s timeout).

1. Step 1 (30050): waits up to 30 s for `CheckMissionState() == true`
   (GOTO_ACK{1,2} once), then calls `f30050_sequence.ReserveMissionClear()`
   (the helicopter boarding: `TppDemo.SetNextMBDemo()` clears a queued MB
   demo, then `ReserveMissionClear{RIDE_ON_HELICOPTER, MTBS_HELI}`; fallback:
   the same two calls). The next frame reads back
   `svars.mis_isDefiniteMissionClear` and sends GOTO_ACK{1,1|0}. Native then
   expects the ACC within 120 s. The 30050 clear runs `MissionGameEnd`, whose
   reward announce can pause the game for input, and the ACC opens on an
   "Overall Completion" screen that waits for Enter (live 2026-10-05: Space
   does not dismiss it; Enter does).
2. Step 2 (40050): native pushes GOTO{2} at the ACC's CFG start. Lua waits for
   `Seq_Game_MainGame`, `CheckMissionState() == true` and 3 s (GOTO_ACK{2,2}
   once; native timeout 60 s), refuses unless the mission is a heli space and
   `gvars.mis_nextMissionCodeForEmergency == 0`, then calls
   `TppMission.SelectNextMissionHeliStartRoute(target, StrCode32("lz|rt"),
   false)`, logs the readback (`event=goto;step=2;next=;route=;want=`) and
   calls `TppMission.OnEndMissionPreparation(TppClock.DEPLOY_TIME.CURRENT,
   nil)`. Routes: 30010 `lz_drp_field_N0000|rt_drp_field_N_0000` /
   `lz_drp_enemyBase_S0000|rt_drp_enemyBase_S_0000`, 30020
   `lz_drp_savannahEast_N0000|rt_drp_savannahEast_N_0000` /
   `lz_drp_pfCampNorth_S0000|rt_drp_pfCampNorth_S_0000`. The next frame reads
   back `svars.mis_isDefiniteMissionClear` and sends GOTO_ACK{2,1|0}. The
   deploy adds 6 h of game time on the location change; W2 re-converges.
   Step 2 no longer waits for pairing itself: the W3 follow wrapper of
   `OnEndMissionPreparation` decides (section 13, the host deployment hold).
   While that hold keeps the deployment (`W.follow.hostDeploy`), the readback
   waits: GOTO_ACK{2,2} once with `event=goto;step=2;result=waiting;
   reason=held`, and Lua cancels the request after 150 s (60 s when nothing
   is held). The native step-2 timeout stays 60 s, so a long hold ends the
   native goto as `world_goto=failed; reason=acc_timeout` (harmless: the
   hold still releases, or flies alone after 120 s).
3. Arrival: the target's CFG start logs `world_goto=arrived; mission=;
   ms=`; any other mission `world_goto=failed; reason=unexpected_mission`.
   Timeouts: `step1_timeout` (45 s), `acc_timeout` (120 s to the ACC, 60 s
   for step 2), `step_timeout` (120 s to the target).

Manual fallback (not automated): `mvars.mis_selectedDeployTime =
TppClock.DEPLOY_TIME.CURRENT; TppMission.ReserveMissionClear{missionClearType
= FROM_HELISPACE, nextMissionId = T, nextHeliRoute = "<lz|rt>"}`.

## 9. Probes (lab only, each runs once)

A probe starts 10 s after the CFG start (lab.ini or a trigger before the
mission) or at `max(now + 1 s, t0 + 10 s)` (a trigger during the mission) and
logs `event=probe;id=<name>;step=...`, ending with `step=done;result=ok|
refused|fail` (refusals: `reason=role|level|mission|paired|mode|busy`).

| Probe | Where | Output |
|---|---|---|
| p1 | CPs, W1 bindings | `step=consts;sneak=;caution=;evasion=;alert=;enemy=0,1,2,3`; `SetPhase CAUTION` on the nearest SNEAK CP; `step=result;cp=;before=0;set=1;f1=;t2s=;msg=0|1` |
| p3 / p3b | W1 bindings | `step=consts;dead=;dying=;sleep=;faint=;ls_normal=0;...`; nearest soldier `ChangeLifeState FAINT`; `step=read;ms=;life=;status=` at +1 frame, +1 s, +5 s; `step=msg;m=;att=local|null|other` for its Dead/Neutralize/Unconscious within the run; p3b: `RecoveryLife` at +6 s, `step=wake;ms=;life=` at +7 s and +10 s |
| p4 | 30010/30020 | a soldier of a CP outside the local and partner areas: `step=start`, `ChangeLifeState DEAD`, reads `phase=out` at +1 frame and +1 s, then the union with its area simulated, reads `phase=active` at +2 s and +5 s, then the local area again |
| p12 | any | `step=result;is_host=;sync=` (read-only, never used) |
| p17 | any | 60 lines `step=sec;n=;frames=;dt_ms=` (pause the game during it; native `rx_gap_ms_max` corroborates) |
| race_kill | host, 30010/30020, paired at eff >= 2 | `PROBE_SYNC{7,1,D,3000}`; both copies log `step=fire;ent=D` at the same QPC time and kill D (the guest attributes its own kill) |
| remote_area_kill | host, paired at eff >= 2 | `step=start;area=X;ent=S`, a probe commit DEAD (actor 2, cause 7) plus a best-effort local `ChangeLifeState`; PROBE_SYNC step 2 (+5 s: the guest simulates area X) and step 3 (+10 s: back) |
| kill_near / sleep_near / fulton_near | paired at eff >= 2 | the nearest soldier (attribution override): `ChangeLifeState DEAD` / `SLEEP` / `RequestForceFulton` |
| alert_near | guest, paired at eff >= 2 | the nearest CP: `SetPhase ALERT`, re-asserted every 5 s for 60 s |
| weather_rain | host, eff >= 3, 30010/30020 | `TppWeather.ForceRequestWeather(RAINY, 5)` (cancelled at stop) |
| clock_jump / clock_fast | host, eff >= 3 | `TppClock.AddTime(3600)` / `vars.clock += 300` every 250 ms for 10 s |
| shared_death | either, supported W3/W4 scene at eff >= 3 | reserve PLAYER_DEAD shared game over through the wrapped `TppMission.ReserveGameOver` |
| host_continue | host, shared game over at eff >= 3 | `TppMission.ContinueFromCheckPoint({isNoFade = true})`; guest follows and aligns in the new epoch |
| shared_checkpoint | host, ready alignment at eff >= 3 | request a current-position checkpoint through `TppMission.UpdateCheckPoint`; require an accepted save/version change |

Probe ids 1..18 are assigned; ids 16..18 exercise W4 recovery and are
single-call lab probes. A probe's `result=ok` is an API/receipt result, not
proof that both games reached the expected state; record both readbacks.

## 10. Log lines

Native (`script-observer.log`; every `world` line ends with `; t_ms=<QPC ms>`):

```
world=configured; level=; role=host|guest; mode=lab|play; proto=1; probe=<name>|none
world=disabled; reason=partner_off|net_off|legacy_link|saves_not_redirected
world=cfg; mission=; level=; epoch=; link=0|1; read_mission=
world=hello_rx; mission=; location=; roster=; soldiers=; cps=; role=; level=; epoch=; saves=; proto=; build=
world=ready_rx; mission=; epoch=; roster=; soldiers=; cps=
world=paired; epoch=; mission=; roster_local=; roster_remote=; soldiers_local=; soldiers_remote=; match=0|1; level=; remote_level=; eff=
world=pair_refused; reason=remote_saves|role
world=pair_wait; reason=<pair-wait name, section 3>; local=; remote=; remote_level=
world=note; code=; a=; b=; partner= | world=note; code=; result=unknown
world=unpaired; reason=session|remote_mission|epoch|remote_end|level0|local_end
world=remote_silent; since_link_ms=
world=overflow; queue=control|world|out; dropped=
world=log_cap; dropped=
world=stats; level=; eff=; role=; paired=; epoch=; tx_r=; tx_u=; rx_r=; rx_u=; q_ctrl=; q_world=; q_world_max=; out_q=; out_full=; overflow=; purged=; stale_u=; bad_bundle=; bad_op=; wrong_dir=; level_drop=; unknown_op=; auto_received=; auto_reject=; rx_gap_ms_max=
world_trigger=applied; text=<canonical> | world_trigger=rejected; reason=token|repeat|empty|too_long|not_lab|level0
world_goto=armed; target=; lz= | refused; reason= | step; step=1|2; result=reserved|refused|waiting | arrived; mission=; ms= | failed; reason=; mission=
world_probe=scheduled; id=; when=start|trigger
world_lua=<payload, ';' expanded to '; '>
```

`world=stats` comes every 10 s, only when something changed.
`world=pair_wait` comes once per change of (reason, local, remote) while the
level is on, the link up and the core unpaired. `world=note` is at most one
line per note code and (a, b) per 10 s (section 5, NOTE).

Lua payloads (`world_lua=event=...`): `started`, `inactive` (reason
`missing_required`, `bad_const`, `continue`, `game_over`, `roster_mismatch`,
`level0`), `missing`, `error`, `stop`, `paired`, `unpaired`, `obs`,
`obs_cap`, `commit`, `req`, `req_rx` (host refusal), `req_done`,
`req_timeout`, `req_drop`, `req_hold`, `apply`, `ledger`, `divergence`, `phase`,
`req_phase`, `floor`, `phase_vec`, `area`, `digest_mismatch`, `resync`,
`resync_needed`, `clock`, `clock_tx`, `clock_check`, `weather`,
`weather_check`, `roster`, `stats`, `bad_value`, `probe`, `goto`,
`raw_clock` (section 7, Clock), `vehicle_gone` (section 16), `vehicle_state` (section 17).

## 11. Deviations from the corrected spec

| Change | Reason |
|---|---|
| Seven Lua chunks (`world_outcome.lua` and `world_goto.lua` added) and a different split: W1 bindings in `world_phase.lua`; attribution, requests, host request validation and the outcome wire handlers in `world_outcome.lua`; the resync stream and the stats line in `world_env.lua` | The enemy, phase and probe logic did not fit 16000 bytes per file |
| RX mask 3 needs paired, eff >= 2 and in-game, but not W1 on | W2 must drain CLOCK/WEATHER in 30050 and after a continue (W1 off) |
| A CFG start drops the queued control and world ops of the previous mission | Stale UNPAIRED/PROBE_FIRE/RESYNC_END of the old mission must not reach the new one |
| HELLO, READY and MISSION_END are consumed natively, never pushed to Lua | Lua ignored them anyway; one receipt less per message |
| A probe trigger at level 0 is `world_trigger=rejected; reason=level0`; a goto at level 0 is `world_goto=refused; reason=level` | The two refusal lines of the spec need a rule |
| REJECT 7 is never sent | A roster mismatch turns W1 off for the mission, so the host answers 11 |
| HOLDUP/VANISHED applies are `noop`; an entity with a readback in flight defers new applies | No setter in W1; no double command |
| Extra lines: `world=pair_refused; reason=role`, `event=clock_tx`, `event=req_rx`, `event=goto`, `event=resync_needed`, `event=phase;src=rx` | Diagnostics for the live tests |
| WEATHER fog fields accept only "none" | W2 never sends fog; later phases widen the range |
| RESYNC_END is queued and delivered as a world op | It must not overtake the ENEMY_OUTCOMEs sent before it (`seen` would be over-counted) |
| The host floor counts a guest phase only while reported in the last 5 s; a guest re-paired to another host epoch turns W1 off; lost requests are held for the next RESYNC_END | Review fixes: no ALERT latch after the guest left or stopped reporting, no lattice carried across host worlds, no guest outcome lost on a session change or REJECT 5/6/9 |

## 12. Live acceptance (lab, both copies on one PC)

The current live-tested **0.11.1** DLL is
`4666A7DA7ED2711CED7D397ED0EFF92E41782C27127DFF9374C63B1DBBF99D65`.
Run `20261006-115055` verifies host-only native title Continue to Africa
with all 28 seed keys / 47 records, both players starting on foot at matching
coordinates and independent native keyboard movement. Guest alignment
`id=1` accepts epoch `3`, roster `2220786415`, 265 soldiers, 68 CPs,
151 unknown soldiers, zero divergences and `w1=1`.

The host checkpoint probe has an actual guest VarSave receipt. An injected
guest PLAYER_DEAD opens Mission Failed on both windows. Host-only native
menu Continue returns both players on foot to the shared checkpoint and
accepts epoch `4` alignment (`id=2`, 151 unknowns, zero divergences, `w1=1`).
Host-only fresh deployment then follows 30020 → 40020 → 30010 with 47
records per leg and both players entering through their own helicopters.
Guest alignment `id=3` accepts epoch `6`, roster `2772775463`, 271 soldiers,
77 CPs, 191 unknowns, zero divergences and `w1=1`; host readiness follows
32 ms later. Independent ground movement shows two bodies with reciprocal
transforms matching at rest. Unknown states remain held until readable;
alignment does not establish identical AI or positions.

Live `world=0` sends UNPAIRED reason `5` and inactive level `0` while partner
sync continues. Both games close normally via `WM_CLOSE`; all ten real
save/config files match run-start and latest-backup hashes. Actual Disable
archives the offline markers and leaves marker, `lab.ini`, `coop.ini` and
link secret absent in both game folders. Cleanup commit `9d5aac6` passes
the focused lab suite; file-state checks and the source gate establish
inactive pass-through. No normal game launch after cleanup was exercised.
Arrival/title/recovery checks pass 11/11. Disk-write completion, physical
damage death and a camouflage cause remain unverified.

The preceding integrated DLL
`171041C2A598FD758FCC67A0B19B59BE4EC6C35FBA4CD98C2529BF946A9E97D6`
has the larger Mother Base → ACC → Afghanistan → Africa run
`20261006-111828`, including range hide/re-entry and Continue recovery after
an all-unknown host area became readable. The intervening diagnostic DLL
`59CD23348386815A84DA0CE9D18DB2B308E52BE21D1B5DDF9C254FC1910A9B13`
exposed a helicopter/foot initial-action mismatch in run `20261006-113833`.
The current build adds mandatory seed 28,
`svars.ply_isUsedPlayerInitialAction`, and passes both checkpoint-on-foot
and fresh-helicopter-entry cases. The previously killed soldier's
post-Continue readback remains unverified. These are separate build-specific
results on two copies on one PC; two-PC and full campaign play are untested.
[Results](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-10-05-world-sync.md).

Logs: `<host|guest dir>\mgs-coop\script-observer.log`; Lua lines appear as
`world_lua=event=...` with `;` expanded to `; ` (below written as in Lua).
Hashes are `%.0f` decimals. A tips or "Resume" screen can block a mission
start or follow the 30050 clear announce: `pwsh -File
tools/lab/Send-LabInput.ps1 -Role both -Keys space`. Commands and triggers are
in [lab-protocol.md](lab-protocol.md) "World sync".

- **W0-4 (world=0 inert, first):** `Play-OfflineCoop.ps1 -World 0 -ExitAfterReadySec
  180` explicitly disables shared world. Expect no active world configuration, pairing
  or enemy/environment sync; verify the partner still behaves normally.
  Also disable an active level-3 run with `Set-WorldTrigger.ps1 -Role both
  -Text 'world=0'`: each copy must receive UNPAIRED reason `5` then
  `event=inactive;reason=level0`, with no later old travel or alignment replay.
  Partner pose sync remains independent of the world level.
- **Main run:** `Play-OfflineCoop.ps1 -World 3 -KeepLabEnabled`.
- **W0-1 (30050):** both logs `world=configured; level=3; role=...;
  mode=lab`, `world=cfg; mission=30050`, `world=hello_rx; mission=30050; ...;
  level=3; ...; saves=1`, `world=paired; ...; mission=30050; ...; match=1; ...;
  eff=3`, `event=started;level=3;...`.
- **Goto (World 3):** `Set-WorldTrigger.ps1 -Role host -Text 'goto=30010' -WaitSec 15`
  -> host `world_goto=armed; target=30010; lz=0`, `world_goto=step; step=1;
  result=reserved` (Space if no `world=cfg; mission=40050` within 20 s), then
  `world=cfg; mission=40050`, `world_goto=step; step=2; result=waiting`, then
  `reserved`, `world=cfg; mission=30010`, `world_goto=arrived; mission=30010`.
  The guest must log `event=follow;step=queued|seeds_ready|replay|arrived`
  for the transitions. Record matching missions/epochs on both copies and
  complete alignment before accepting enemy sync. Both should land at LZ
  field_N; a clock jump of about +6 h is expected, with W2 convergence
  within 2 digests. At levels 1/2, use `-Role both` for independent travel.
- **W0-3 (30010):** `world=paired; ...; mission=30010; roster_local=X;
  roster_remote=X; soldiers_local=271; soldiers_remote=271; match=1`,
  `event=started;...;soldiers=271;cps=77;w1=1`, `event=area;shape=set|array`
  (record it; it appears once the partner area is known).
- **W0-2 (observer, 10 min):** each kill, neutralise or phase change gives one
  `event=obs;m=Dead|Neutralize|ChangePhase;ent=<hash>` per message on the PC
  where it happened, no duplicate (m, ent) within 1 s; `event=stats;...;resend=N`
  (N > 0 proves the RESEND path; 0 = not exercised).
- **W1-1 (host kills A; manual or `-Role host -Text world_probe=kill_near`):**
  host `event=commit;ent=A;state=5;ver=1;actor=host`; guest
  `event=apply;ent=A;state=5;ver=1;src=outcome;result=ok;readback=5` less than
  1000 ms (t_ms) after the commit; exactly one guest `event=obs;m=Dead;ent=A`.
- **W1-2 (guest tranquilises B; manual or `-Role guest -Text
  world_probe=sleep_near`):** guest `event=req;rid=r;ent=B;state=2`; host
  `event=commit;ent=B;state=2;ver=1;actor=guest;rid=r` and
  `event=apply;ent=B;...;src=commit;result=ok|noop;readback=2`; guest
  `event=req_done;rid=r;ms=..;result=outcome` then
  `event=apply;ent=B;state=2;ver=1;src=outcome;result=noop`, no second
  ChangeLifeState.
- **W1-3 (guest fultons C; manual or `world_probe=fulton_near` on the guest):**
  guest `event=obs;m=Fulton;ent=C`, `event=obs;m=FultonInfo;ent=C;att=local`
  (or the probe override), `event=req;...;state=6`; host
  `event=commit;ent=C;state=6;...;actor=guest` and
  `event=apply;ent=C;state=6;...;result=ok;readback=6` within 20 s; exactly one
  `m=FultonInfo;ent=C` per PC (one staff add per save, D1); guest
  `event=apply;ent=C;...;result=noop`.
- **W1-4 (`-Role host -Text world_probe=race_kill`):** both
  `event=probe;id=race_kill;step=fire;ent=D` with t_ms within 100 ms; host
  exactly one `event=commit;ent=D;state=5;ver=1`; guest either
  `event=req_done;rid=..;result=reject;reason=2|3;op=17;state=5;ver=1` then
  `event=apply;ent=D;...;src=reject;result=noop|terminal_local`, or
  `result=outcome`; never ver 2 for D.
- **W1-5 (alert; guest spotted or `-Role guest -Text world_probe=alert_near`):**
  host `event=phase;cp=X;phase=3;...;src=req;rid=..`; over 60 s host
  `event=floor;cp=X;host=3;guest=3` and never host < guest for more than 10 s;
  guest `event=req_phase;...;cp=X` at most once a minute; after the guest stops,
  both CPs decay normally (no `floor` line for X 20 s after the guest's own phase
  dropped). Latch check: the host alerts a CP itself -> the guest applies
  (`event=phase;...;src=rx;...;applied=1`) and no `src=floor` line for it.
  Floor end: with the host CP X floored, send `-Role guest -Text world=0`
  (the guest stops sending PHASE_VEC): no host `event=floor;cp=X` and no
  `event=phase;cp=X;...;src=floor` later than 5 s after the guest's
  `world_trigger=applied; text=world=0`, and X decays normally.
- **Review fixes (lab):** a goto from an unsupported scene is refused with
  `world_goto=refused; reason=mission`; normal 30010/30020 free roam is now
  an allowed source and rides the helicopter to its ACC. Use host-only
  triggers at World 3. After the guest leaves 30010
  (`world=unpaired` on the host), the soldiers of the guest's former area
  deactivate as in vanilla (the union is not logged; the host's original
  UpdateOutOfAreaSetting reruns for its own area at once). A guest request outstanding at a link drop shows
  `event=req_hold;n=` and is sent again after the next `event=resync;...;
  src=end`. At levels 1/2, a host mission restart while the guest stays in
  30010 gives the guest `event=inactive;reason=epoch;scope=w1` at the next
  `event=paired`. At level 3, test follow and alignment as in section 13.
- **W1-6 (`-Role host -Text world_probe=remote_area_kill`):** host
  `event=probe;id=remote_area_kill;step=start;area=X;ent=S`,
  `event=commit;ent=S;state=5;ver=1;actor=probe`; guest
  `event=ledger;ent=S;op=hold;reason=out_of_area`, `step=2`,
  `event=ledger;ent=S;op=applied;reason=out_of_area;waited_ms~5000`,
  `event=apply;ent=S;state=5;...;result=ok;readback=5`, `step=3`.
- **W2-1 (clock, 30010):** 10 min of play: every guest `event=clock_check;diff=`
  within +-60. `world_probe=clock_jump` on the host: host
  `event=clock_tx;cause=jump`, guest `event=clock;cause=jump;...;applied=1`
  within 1 s, then `clock_check` within 60. `world_probe=clock_fast`: guest
  `cause=fast` lines at most 4 per second; from 2 s after the host's
  `event=clock_tx;cause=fast_end`, `|diff| <= 60`.
- **W2-2 (weather, 30010; `-Role host -Text world_probe=weather_rain`):** host
  `event=weather;type=2;cause=1`; guest `event=weather;type=2;cause=1;applied=1`
  within 1 s, `event=weather_check;local=2;host=2` at +20 s and +120 s; at the
  mission end `event=stop`, the next mission's weather is not forced.
- **Probes (host, 30010; values recorded, no pass/fail):** P1
  `event=probe;id=p1;step=result;...;f1=;t2s=;msg=` (hoped f1=1, msg=1); P3/P3b
  `step=consts;dead=;dying=;sleep=;faint=` (four distinct values in 1..4), the
  FAINT `step=read;...;life=` values, the `step=msg;m=;att=` lines (who gets
  the kill credit of an applied death), P3b `step=wake`; P4 `step=read;
  phase=out|active;ms=;life=`; P12 `step=result;is_host=;sync=`; P17 60
  `step=sec;n=;frames=;dt_ms=` lines (pause during it) against `world=stats;
  ...; rx_gap_ms_max=`.

## 13. W3/W4 implemented subset

These operations are reliable and require both peers at level 3 with
redirected saves. H/G mean host/guest. The same bundle format and channel
are used; the exact body schemas are in `native/world_wire.cpp`.

| Code | Name | Dir | Lua fields | Queue | Body |
|---|---|---|---|---|---|
| 0x09 | MISSION_TRANSITION | H->G | id, kind, source, destination, clearType, route, layout, cluster, deployTime, noSave, nextEpoch | control | 27 |
| 0x0A | MISSION_SEED | H->G | id, destination, key, index, value | control | 15 |
| 0x0B | MISSION_SEEDS_END | H->G | id, destination, count, digest | control | 14 |
| 0x0C | REQ_MISSION | G->H | rid, kind, mission, arg | control | 11 |
| 0x0D | FOLLOW_ACK | G->H | id, mission, status (0 refused, 1 replay, 2 arrived) | control | 9 |
| 0x0E | ALIGN_BEGIN | H->G | id, mission, roster, soldierCount, cpCount (+ native epoch) | control | 18 |
| 0x0F | ALIGN_END | H->G | id, soldierCount, cpCount, digest, outcomeSeq (+ native epoch) | world | 16 |
| 0x18 | ALIGN_RECORD | H->G | id, type (1 soldier, 2 CP), index, hash, readable, state, version | world | 15 |
| 0x19 | ALIGN_ACK | G->H | id, mission, epoch, accepted | control | 11 |
| 0x1A | GAME_OVER | H->G | type, radio, cause | control | 6 |
| 0x1B | REQ_GAME_OVER | G->H | rid, type, radio, cause | control | 8 |
| 0x1C | CHECKPOINT | H->G | hash, version, x/y/z mm, yaw millidegrees | control | 22 |

**Travel.** Only 30010/30020/30050 and ACC 40010/40020/40050 are supported.
The wrappers install before `TppMission.Init` captures message callbacks.
The host captures `ReservePlayerLoadingPosition` and the prepared seed
values before vanilla `Load`. The guest waits for the complete checked seed
stream, then replays clear, abort, deployment, ordinary restart or Continue
through the corresponding vanilla Lua API. An outer `LoadWithChunkCheck`
barrier retains the game's pending request until seeds exist. Seed fields
include face/revenge choices, clock/weather preparation, initial player
flags/pose and helicopter route; hashes use unsigned `%.0f` formatting.

Startup Continue restores each copy's local checkpoint before the loading
position callback. The guest applies the host mission/location and saved
initial-player flag/pose at that callback, before vanilla stage-block
selection. RequestLoad and Load reassert the destination while preserving
the original current mission, so crossing from ACC loads the new assets
instead of reloading ACC. Seed keys 19-23 carry those saved vars and key
24 carries `mis_checkPoint`. A fixture executes the SHA-pinned extracted
Continue, stage-block, chunk-check and loading functions for all three
supported field missions. Run `20261006-111828` also verifies native host
title Continue following to Mother Base and onward through ACC to Afghanistan
with the integrated DLL.

All 28 seed keys are required, with every declared array index exactly
once; completion checks coverage as well as count and ordered digest.
The normal 16-entry revenge array produces 47 records. The host retains
its pending load if any required field is unreadable; it sends no partial
END. Duplicate, empty or self-consistent partial streams fail closed on
the guest. The schema lives in the separate bounded `world_follow_seed`
Lua chunk. Guest restore wrappers reapply the checked stream after later
mission-start/Continue restores, and the stage-block wrapper reasserts it
before vanilla placement reads, including helicopter preparation.
Keys 25-27 carry the host's BOOL, non-saved `ini_isTitleMode`,
`ini_isReturnToTitle` and `isContinueFromTitle` context. An authenticated
replay applies these once, before the high-level mission API, and logs
the observed before/after flags as `step=title_context`. Repeated seed
preparation skips these keys, so vanilla allocation's
`ClearIsContinueFromTitle` remains effective. This lets a guest that has
not pressed the native title Continue button enter ordinary mission/ACC
initialization when following the host's Continue, rather than retaining
title-mode sky and UI into the subsequent deployment.
Key 28 carries the saved BOOL `svars.ply_isUsedPlayerInitialAction`.
Player initialization uses that flag together with the initial player
state to choose whether to enter from the helicopter. Copying it after
restore prevents an old guest helicopter checkpoint from overriding the
host's resumed on-foot checkpoint pose. The exact player initialization
branch is covered by the SHA-pinned TppPlayer fixture input.
Already accepted pending preparation survives the inactive mission-loading
gap only while fresh CFG confirms level 3, the same role, an active link
and the unchanged nonzero local transport session. A changed session,
disconnect, explicit level zero or mixed negotiated level cannot apply
that stale preparation or reseed allocation. Idle save/stage hooks delegate
without extra CFG queries when no pending transaction exists.

Initial pairing still requires both copies in the same supported scene.
Joining with copied saves in different ACC/location scenes has no W3
bootstrap route; bring both copies to the same scene before following, or
let the automatic save handoff (join-fix phase 2, below) give the guest the
host's save.
Each copy sees why it is not paired (section 3, pair-wait reason): CFG r[9..11]
give Lua the partner's mission, its world level and the raw reason
(`W.peer()`), and a different-missions pair that lasts 15 s shows one
on-screen note on both PCs.

Travel controls survive native CFG mission resets, but are discarded on a
new link session. Lua transaction history resets even if the previous link
was already unpaired. A frozen outgoing stream retries refused TX items and
END without advancing its count. Guest queues are bounded and a transition
times out after 120 seconds.

**Host deployment hold.** The host's deployment (`OnEndMissionPreparation`
in the ACC) waits only while a partner can come: a linked level-3 partner
that is in this scene, still loading, or still replaying the host's last
transition (no FOLLOW_ACK 0 or 2 for it yet, the same link session, the
partner in the source mission or loading). It never waits with no link, a
partner that is not linked or below level 3, or a partner known to be in
another mission that is not following; then it flies at once
(`event=follow;step=deploy_alone;reason=no_partner|partner_elsewhere`,
NOTE 3; `reason=join_partner`, NOTE 3 b=5, when the target is the
partner's own mission: they load it and pair there, online 2026-10-22 P7).
A hold logs `step=deploy_wait;reason=ready;target=;partner=` and
shows NOTE 2 on screen, re-posted every 3 s. It ends when the copies pair
(the held deployment runs paired), when the deployment is chosen again
(after a 1 s debounce; `reason=repeat`, NOTE 3 b=3), or after 120 s
(`reason=timeout`, NOTE 3 b=4); a hold left from another mission is dropped
without deploying (`step=deploy_held;reason=stale`). With an older loader
(no r[9..11]) Lua cannot tell where the partner is and always holds, still
bounded at 120 s. A lower negotiated level releases the vanilla deployment
without enabling W3.

**Unsupported destinations.** A story or emergency target of the host's
deployment, or a ReserveMissionClear/AbortMission `nextMissionId` that W3
cannot follow, is refused the first time with NOTE 4 (the unchanged
`step=refused;reason=unsupported_mission` line). Choosing the same target
again 1-30 s later goes alone (NOTE 3 b=3); with no partner who could come
it is allowed at once (NOTE 3 b=1 or 2). Midmission Reload and
ReturnToMission are still refused before supported host paths change
vanilla state.

**Save handoff and travel lock (join-fix phase 2).** The design and the
native side are in [save-handoff.md](save-handoff.md); the Lua chunk is
`lua/coop/world_handoff.lua`, spliced right after `world_follow.lua`. It
reports the save state with the SAVE receipt (section 5) and reads CFG
r[12] (handoff code) and r[13] (travel lock). The travel lock wraps
`ReserveMissionClear`, `AbortMission`, `AbortMissionByMenu`,
`ContinueFromCheckPoint`, `ExecuteContinueFromCheckPoint`,
`OnEndMissionPreparation` and `e.ReservePlayerLoadingPosition` below
world_follow's armed-guest suppression (armed guests never reach it). It
applies only to an unarmed, linked guest (r[13] 1) whose host is at level 3
in a supported mission other than 1 and not loading (`W.peer()`); the host's
mission, any helicopter (4xxxx), restart, reload and return to title are
always allowed, and it is inert while a transition is replayed or queued.
A title Continue is refused softly (NOTE 7) when the guest's title location
differs from the host's, when its save is at the story start, or while a
handoff is pending (NOTE 9 while busy); pressing again 3 to 10 s later goes
alone (NOTE 8 b=1; while a handoff is pending this also sends SAVE op 3 and
NOTE 10). Online 2026-10-22 P2: at the title presses under 3 s apart never
escape, and a pending handoff has no Continue escape for its first 60 s
(`decision=held`; F9 Keep my own save is the way out); see save-handoff.md
"Title Continue". A sortie is held with its arguments (NOTE 12, re-posted every 3 s)
and released by choosing it again, by the F9 Travel lock checkbox (r[13]
drops to 0; NOTE 8 b=2), after 120 s (NOTE 8 b=3) or when it turns into a
follow. An iDroid ReserveMissionClear is refused (NOTE 5) and a repeat within
1-30 s goes alone. The host is warned (NOTE 6) when it loads into a mission
the partner cannot join, unless the partner is in that very mission (P7); the guest is warned (NOTE 11) when its Continue
lands elsewhere. The replay hold: while a handoff is pending at the title
(r[12] 1..4) the guest's follow replay waits (the 120 s queue timeout still
refuses the TRANSITION with FOLLOW_ACK 0). Lua log lines:
`event=handoff;step=report|lock|warn|replay_hold;...`.

**Guest arm.** Guest exits, hot-zone clear, abort and menu choices are
suppressed while W3 is armed; exit requests are logged and explicitly
refused, leaving the host to decide. An armed guest disarms
(`step=disarm;reason=unpaired_timeout|partner_mission`) after 120 s unpaired
with an empty transition queue and no replay running (the timer restarts at
each guest mission start), or at once when the partner's mission is one W3
cannot follow.

**Reload alignment.** Every supported enemy mission at level 3 starts W1
inactive. The host scans all mapped soldiers and CPs in bounded batches,
including initial checkpoint state whose version was previously zero.
The frozen snapshot names every soldier/CP by index and hash, with explicit
unknown markers for unreadable areas. The guest validates mission, roster,
epoch, unique indices, counts and ordered digest before applying through
the existing forward-only life ledger and phase floors. ALIGN_END is in the
world queue so it cannot overtake its records. Both sides resume W1 only
after complete acceptance; failed TX and ACK timeout retry safely.
A structurally complete snapshot with zero readable soldiers keeps W1 off
and logs `reason=no_readable_state`, with bounded readiness counts. It is
retried as host areas become active; a finished reload and matching epoch
do not by themselves establish enemy readiness. Malformed, missing or
digest-mismatched streams still log `reason=incomplete_snapshot`.

This does not revive a guest soldier that is already terminal when the host
reports NORMAL: the divergence is logged as `action=no_revive`. Unknown
host entities are discovered when their areas become readable. Unknown
does not mean VANISHED and does not establish fully identical enemy AI or
positions. Full story progress, arbitrary svars, hostages and vehicles are
not copied (0.13.6: with `world_vehicle_gone=1` a vehicle one copy loses to a
Fulton or destruction is removed on the other, section 16; positions and
damage of vehicles are still not copied).

**Recovery.** Either player's PLAYER_DEAD/PLAYER_FALL_DEAD reserves shared
game over. A lethal fall into the sea or out of bounds (damage part 12, flags 0x2419) is that
stock death since 0.13.6, never "downed" (native-puppet.md "Body safety"). The host retains menu authority; its Continue/restart travels
both copies and starts another alignment. Accepted host checkpoint saves
are versioned and copied via vanilla checkpoint APIs on the guest's
redirected save. Hash-zero/current-position checkpoints carry the host's
exact saved initial pose. Named checkpoints resolve the local mission's
declared name. Suppressed guest checkpoint updates do not independently
advance that copied checkpoint.

**Live acceptance (still required for a newly built DLL).** Start both
copies with `-World 3`; wait for pairing and alignment ready/accepted in
30010. From Mother Base, send `goto=30010` to the host only and record
30050 -> 40050 -> 30010 on both copies. Ordinary guest heli/boundary/menu
choices must leave both in the same mission. For recovery:

```powershell
pwsh -File tools/lab/Set-WorldTrigger.ps1 -Role host -Text 'world_probe=kill_near' -WaitSec 10
pwsh -File tools/lab/Set-WorldTrigger.ps1 -Role host -Text 'world_probe=shared_checkpoint' -WaitSec 10
pwsh -File tools/lab/Set-WorldTrigger.ps1 -Role guest -Text 'world_probe=shared_death' -WaitSec 10
pwsh -File tools/lab/Set-WorldTrigger.ps1 -Role host -Text 'world_probe=host_continue' -WaitSec 10
```

Record shared game-over state on both windows, the new matching epoch,
complete alignment, dead-soldier readback, phase floor and host checkpoint
pose copied into the guest save. The three new probes are lab-only,
single-call, role/readiness-gated exercises of public Lua APIs; actual input
death and host menu Continue remain a separate runtime check. Setting
`world=0` must remain inert. Automated fixtures cover malformed/incomplete
snapshots, versions, unknown discovery, no revive/double Fulton, queue
pressure, session replacement and mixed levels, not physical game input.

## 14. Remaining extension points

W5 full campaign/progression and W6 AI transforms remain design work.
World op codes 0x1D-0x1F, 0x32, 0x35-0x37 and 0x3A onward are available (0x2A-0x2D are the
ride ops, 0x2E/0x2F VEHICLE_GONE, section 16; 0x30/0x31/0x38/0x39 the companion ops, section 15; 0x33
VEHICLE_STATE, section 17; 0x34 VEHICLE_SPAWN, the M3 sortie vehicle, docs/vehicles-companions-design.md 6.3;
0x32 and 0x35-0x37 stay reserved for the companion / vehicle design, docs/vehicles-companions-design.md
section 5); control codes
0x09-0x0F are now used. HOLDUP, hostages/vehicles and dynamic quest roster
changes still require separate handling and live acceptance. Partner body
rendering and input drive are independent of these world modules.


## 15. Host-led companion positions and allocation research

`world_buddy.lua` adds an optional level-3 companion channel. The host keeps
its normal native buddy AI. For D-Dog, it publishes the actual instance-0
position at most four times per second; the guest holds its own native D-Dog
at that position with the dog command `LuaAiStayAndSnarl` (`position`, `look`,
`dash`; the retail `s10040` "stop AI" form), re-sent every 250 ms. (Earlier
revisions of this section named `RequestMoveToPosition` and a 0.75 m stop;
the code never used either: the controller's `RequestMoveToPosition` is listed
below as evidence only.) This synchronizes a host-led dog position target, not
dog combat, spotting, animations, health or a globally shared native actor.

**M2 (`docs/vehicles-companions-design.md` 6.2; lab key `companion_host_owned`,
with M1's host-owned companion).** With the key the host no longer sends
BUDDY_STATE: the dog pose rides COMPANION_POSE (0x39, a datagram with its own
latest-value slot: sender tick, kind, slot, present, x/y/z mm, yaw mrad, state,
plus the native receive latency), read every 100 ms and sent at 10 Hz while the
dog moves (5 cm or more since the last send) and at 2 Hz idle; the reliable
COMPANION (0x30, M1) carries the same pose on a kind change and every 5 s. The
guest answers COMPANION_ACK (0x31, reliable, 1 Hz: sequence, kind, result 0
holding at the host's pose / 1 loading or unreadable / 2 refused or another kind
/ 3 stale, held at the last pose / 5 held where it stands, error cm). The guest
holds its copy of the host's dog every 100 ms at the host dog's pose led by the
sample's age (at most 300 ms, and only from consecutive 10 Hz samples of a live
stream) with the stop-AI form of `LuaAiStayAndSnarl` (`look` given,
`useMarker=false`: modes 1/2, whose Dog AI update is skipped) placed by the
command's own `warp=1` (design 3.2 and 8.2; `SetWarpPosition` stays for errors
over 15 m in the other hold modes), and **never releases it to its own AI**: no release on a stale stream, `present=0`, a kind change, an
unpair or a stop (a host without a dog is the companion module's reconcile,
`DeleteBuddy`). The dog pose receipt `MGS_COOP_BUDDY_POSE_V1` no longer needs
body 1 realized (`buddy_pose::FieldReady`), so a partner hidden in the heli or
on a mount no longer stops either role's own-dog read. Without the key this
section's BUDDY_STATE path below is unchanged. Live acceptance: section 8 and 9
of the design.

**Review fixes (2026-10-29, design 8.3).** The M2 code lives in
`world_companion_dog.lua` (spliced after `world_companion.lua`). The key is local
to each copy, so each copy shows it in its own traffic: the guest's CHARACTER
(0x38) and the host's COMPANION (0x30) are sent whenever the local key is on, and
the companion path (COMPANION_POSE, the hold, the seeds' buddy keys, the guest's
refusals) runs only once the peer's has been seen; until then, and after the
session ends (10 s after a link-session or level-0 unpair, else 120 s alone in
game), BUDDY_STATE runs as without the key. The guest's copy loads only a host
companion it can hold (the dog): the seeds' `buddyType` and
`buddyTypeMissionStart` read none on the guest for any other kind, and the
reconcile sends a mirror away instead of calling one. No wire change.

**D7 dog guard (2026-10-29, `docs/vehicles-companions-design.md` 6.12, 8.9).** COMPANION_POSE `state` (its ninth Lua
field) and COMPANION `state` (its tenth), both sent 0 until now, carry the host dog's **state byte**: bit 7 valid, bits
5-6 state (0 normal, 1 stunned or asleep, 2 down), bits 0-4 life in 31ths (0 only when down). The host reads it from
its own dog's native life record with every COMPANION_POSE read and sends a changed byte at once. **No wire change**:
the ops, body sizes and field ranges (`U8()`) are unchanged, so no version bump; an older host sends 0, which a new
guest reads as "not valid" (its guard still drops hits but follows nothing), and an older guest ignores the field. The
guest's native guard (`native/dog_guard.cpp`, receipt `MGS_COOP_DOG_GUARD_V1`, armed by `world_companion_guard.lua`
while its copy of the host's dog is loaded and paired) drops every hit on that copy and applies the byte: life, asleep,
and once the host's dog is down, the copy's own stock death. Review 2026-10-29 (`docs/vehicles-companions-design.md`
8.10; local receipt only, still no wire change): the guard's slots are installed only on a PC running
`companion_host_owned`; the receipt's arm field takes 2 (a hold): through a link loss (UNPAIRED, link down, a lower
level or another epoch) an armed lease is held drop-only, with no byte followed and no lab op, until the session or the
mission ends, and a hold never starts the guard; the reply carries the record only when under a second old.

**Heroism at a copy's death (2026-10-29, `docs/vehicles-companions-design.md` 8.14).** Under `companion_host_owned` the
guest gives back the heroism, bond and demon points the stock dying penalty takes at each death of its copy of the
host's dog (`world_companion_guard.lua`, log `event=dog_hero`, lever `companion_lab hero`). **No wire change and no
receipt change:** it reads and writes only the guest's own vars and Mother Base points, so a 0.14.4 partner (host or
guest) is unaffected and needs nothing; with a 0.14.4 host the give-back still runs on a new guest whenever the key's
peer agreement holds (`C.applies`). The observer now hands the raw attacker id to `W.companionHurt` (local Lua only).

| World op | Name | Direction | Fields | Queue | Body bytes |
|---:|---|---|---|---|---:|
| 0x27 | BUDDY_STATE | H->G | sequence, kind, present, x/y/z mm, heading mrad, sender tick (+ native age ms) | world | 24 |
| 0x28 | BUDDY_ACK | G->H | sequence, kind, result, measured position error cm | world | 6 |

Both packets are reliable, level 3, non-request, and use the existing mission,
epoch, session and role guards. Kind values are NONE=0, DOG=1, HORSE=2,
WALKER=3, QUIET=4; these differ from native `BuddyType`. The sequence is a
nonzero 16-bit counter with half-range ordering. The native layer appends
packet age, so the guest does not compare clocks across PCs. Position range
is +/-1,000,000 metres, represented in millimetres; heading is -3142..3142
milliradians. Heading currently follows the host player's heading as a
navigation hint, not an observed dog facing angle.

The guest expires a dog target after one second. It re-sends the held target
(`LuaAiStayAndSnarl`) every 250 ms with no dead band (`dash=1` beyond 2 m),
and uses the stock dog `SetWarpPosition` only for errors over 15 metres.
Acknowledgements carry a measured pre-command error; they do not assert that
the dog has reached its target. `buddy_status` reports actual local readback,
last host position, sample ages, kind/version/result and module error state.
Unobserved positions are reported as unread, never substituted with player
positions or the requested destination. Its bounded debug result has the form
`buddy:G a1 p1 kdog v1 rfollowing e50 H=4.00,2.00,3.00@0 L=3.50,2.00,3.00@0 d0`.
H/G identifies the current role; a/p are active-context/paired flags; k/v/r/e
are kind, sequence, result and error in centimetres; H/L are actual last
host/local xyz in metres followed by age in milliseconds; d is module-disabled.
`?@-1` means unread and `99999+` means at least that old. Both positions survive
the native 160-byte note bound; the worst permitted result is 154 bytes.

Only known active field missions, ordinary in-game state, no running demo,
and an active native buddy block permit mutation. A guest already using
D-Dog borrows that actor; a guest with no buddy requests a dog once and waits
up to five seconds for actual allocation/readback. A horse, walker or Quiet
on the guest is left alone and reports `guest_buddy_busy`. A request returning
true means accepted, not spawned. The module does not change saved buddy
selection or force `Load`. Missing optional bindings or a companion API error
disable this module without disabling enemy/time/weather sync.

Unpairing, stale data, host buddy changes and stop release borrowed control.
An originally absent dog is dismissed; an original dog receives a stock
CallBuddy request at its actual current position. Native refusal retains
ownership for bounded retry. Cleanup waits through menus/demos/inactive
blocks, and a user-selected replacement or changed actor ID is never modified.
Epoch changes clear received targets; mission changes clear old allocation
ownership. Restoring native follow after release requires live verification.

**Qualified API evidence.** The installed executable SHA-256 is
`085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45`.
The registered `TppBuddy2BlockController` table at RVA 0xA45BE0..0xA45F27
identifies GetActiveBuddyType (0xA46030), IsBlockActive (0xA47060),
CallBuddy (0xA44560), RequestMoveToPosition (0xA48C80), and Load
(0xA47CF0). Complete wrappers, including their split cold code, validate
CallBuddy's numeric buddy type, Vector3 position and numeric heading. The
stock Load implementation multiplies saved rotation by pi/180 before the
same controller heading slot, establishing radians. Extracted retail
`s10020_sequence.lua` lines 1331-1336 calls dog instance 0 with
`{id="SetWarpPosition", pos=Vector3(...)}`; the native dog dispatcher matches
that command and reads `pos` at 0xC7D2FC. No guessed native address is called.

**Separate rides remain experimental.** The actual variant-00 D-Dog, D-Horse
and D-Walker FPKD assets each declare one GameObject, totalCount=1,
realizedCount=1, and one group-0 locator. Native CallBuddy selects one global
buddy type/request slot; repeating it does not establish a second ride.
The bounded staging tool `tools/stage_buddy_capacity.py` creates scratch-only
variant-00 horse/walker candidates with count 2 and a second closed locator
graph offset four metres. It keeps the original locator, parts and parameters,
round-trips FOX2/FPKD through the pinned existing local converter, and checks
all 17 horse / 38 walker other entries remain byte-identical. It never
installs an archive. Candidate hashes are recorded in its verification JSON.

Structural success does not prove game allocation, index-1 realization,
controller initialization, memory budget, per-player attachment or replicated
ride input. Only variant 00 is staged. The horse GetRidePlayer dispatcher
(0xE4D250, branch 0xE4D510) uses a count-bounded per-instance array of stride
0x2F0 and returns an occupied flag, not a player identity. Its out-of-range
path is unsafe; index-1 SendCommand probes require proof that this internal
array actually has two entries. The horse initializer passes declared
totalCount into the exact ride component (hash 0xDAD9FC9574D3) at 0xE4CE60,
then retains that component at primary manager+0x88. Its initializer
0xE7C750 and allocator 0xE7C4C0 reserve count times 0x2F0 bytes and construct
each record. This establishes a natural static capacity-two path; memory
budget, actual allocation and normal mounting still need live proof. The second analogous
dispatcher at 0x1807830 belongs to TppHorse2forVr, established through its
vtable type check at 0x1806D50; it must not be used for D-Walker. The actual
TppWalkerGear2 type check is 0x1704670 (hash 0xB4C3AC6CBE57), with Lua
command dispatch at 0x1706190..0x1706CDA. That handler refuses indices beyond
its instance count before dispatch. GetPosition reads the actual position
array at 0x170633A; IsPlayerRiding (hash 0x44367C424E38, branch 0x1706420)
returns a boolean from the rider slot and ride mode, not rider identity. The
registered no-argument Player.GetGameObjectIdIsRiddenToLocal wrapper
(0x9D2510) reads the selected local player's ride object ID; the related
indexed storage getter supports player indices 0..15 but establishes only
read storage, not a safe attach operation. These ride dispatchers are not called by the production module. Horse/walker host samples explicitly report
`second_ride_unavailable`; two independently usable rides are not claimed.

**Validation.** The isolated Lua fixture currently passes 183 checks for
actual-position publication, navigation cadence, stale/old data, native
refusal, user buddy replacement, allocation timeout, cleanup and status
readback. Eight synthetic XML tests cover the staging graph and rejection
paths. These are fixture/format results, not live game, network or ride proof.
Run host/guest lab saves with D-Dog selected in a supported field scene,
observe status/readback while the host walks and sprints, then check dog
release on unpair/buddy replacement and travel. Root-controlled private
worker testing owns any candidate archive installation and restoration.

`tools/stage_buddy_runtime.py` additionally builds a new scratch-only QAR
containing all 13 baseline21 entries byte-identical plus the two capacity-two
variant-00 FPKD overrides. The first verified candidate is 16,659,456 bytes,
SHA-256 `d328031c2478dea51439c95eb783248e01b9b8875e870ce9d4d9cd256593f3f8`.
Its `catalog.json` records all 15 paths/hashes, motion allowance 21, buddy
capacity 2, and the exact horse/walker/variant-00 allowlist. The default-off `MGS_LOCAL_BUDDY_CAPACITY_TEST` admits only this exact archive
with `MGS_BACKGROUND_WORKER=ON`, an actual never-displayed worker desktop,
and matching run/role/profile/hash/motion/capacity environment. The manifest
requires `runtime_profile=buddy_capacity_two_v1`, `runtime_buddy_capacity=2`
and the exact hash. This is lab-only and cannot combine the gun-preload archive.
The explicit worker `-BuildProfile buddy-capacity -BuddyCapacityArchive <path>`
requires native census/loco and a matching Research receipt with only the buddy
flag enabled. Its sidecar catalog is independently pinned. Both installed
baselines are verified first; only the owned run config selects the candidate.
Stop preflights every coherent baseline or worker loader/archive pair before
mutation, then lets the existing installation transaction restore archive,
loader and ownership records together. Never replace the candidate archive
before that transaction checks its installed hash. Production and release
pins remain unchanged; 703 detached worker checks cover policy and recorded
partial restoration, not live installation or allocation.


`Vector3` may be a callable table or userdata in retail, so the companion
module binds it through a protected constructor call plus actual GetX/Y/Z
readback instead of requiring Lua type `function`. Inactive `buddy_status`
reports the precise missing binding/context reason. `B.cleanupPending()` is
safe to invoke after world stop: it retries only previously owned release,
never follows, allocates, publishes or receives, and discards old mission
ownership without calling the old actor. The first hidden run showed the
host dog beside the player while both modules reported `a0 kunknown`; that
pre-fix observation is not evidence of mirrored positions. The binding fix
and inactive cleanup still need live acceptance.

Additional qualified allocation probes are read-only class
`GameObject.SendCommand({type="TppWalkerGear2"},{id="GetMaxInstanceCount"})`
(dispatcher 0x17061FD, class index 0x1FF) and the existing actual instance
`GetPosition`/`IsPlayerRiding`. Once an actual count of two is read, the stock
walker `SetPosition` accepts `pos` and numeric `rotY` and forwards radians
through helper 0x170D410; stock mission s10050_sequence uses this command.
The walker secondary vtable is 0x24426A0, whose +0xF8 getter 0x1704250 reads
`[this+0xB8]+index*0x580`; it is not the unrelated getter at 0x17046A0.
For horse, capture the naturally invoked 0xE4D250 receiver and read its
component count before any index-1 query. Stock `SetCallHorse` takes
`startPosition`/`goalPosition` and per-instance call state; neither repeated
CallBuddy nor that navigation request establishes player attachment.
The horse natural mount-event handler 0xE59C20 stores the actual rider ID
in that horse's record+0xE and occupied state+0x2DD, but its event caller
ownership/ABI is not qualified for synthetic force-mount calls. Normal local
mount input and actual Player.GetGameObjectIdIsRiddenToLocal readback are
required. Remote player attachment and ride replication remain unfinished.

## 16. Vehicles gone (0.13.6 online-vehicle-gone)

Vehicles are not world-synced (positions, damage and seats stay per copy; the
driven vehicle is only mirrored by RIDE_FAST). In the first real two-PC
session (2026-10-22, 0.13.5) the guest Fultoned the Mother Base sortie jeep
25603; the host then drove his copy of it, the guest's seat-by-id read
failed (`partner_ride_seat=byid; ...; reason=local_read`), the host was
hidden for ~25 s and drove into the sea. VEHICLE_GONE makes the loss of a
vehicle host-authoritative. It is a new mechanism that removes vehicles from
a copy, so it is gated: lab.ini `world_vehicle_gone=0|1` (lab and play, not
a play refusal). Default since M4: **0 in the lab** unless named (scenarios
opt in) and **1 in play** unless named (`LabConfig::world_vehicle_gone_given`;
lab FRAMEs `075c1ea4`, `92720b75`, `0edadbe2`; play rehearsals with
`-WanSim bad`: `e44516d7`, each player's own Fulton removed the jeep on both
copies, and `4bbbfde4` / `b31a1b65` with no vehicle key named, where the
host's C4 destruction was a Despawn on the guest's copy (framed on both in
`b31a1b65`); docs/STATUS.md M4 and M4 review fixes). Carried to Lua as CFG **r[15]** bit 1 (`world_link::HandleCfg`;
r[14] is the WS4 census flag; a missing value is 0). Both copies need it; a host
without it answers every request with REJECT 11, a guest without it logs
`event=vehicle_gone;step=ignored;reason=off` once and keeps its vehicles.
Since the M4 review the module runs at 30010 and 30020 in play and at Mother
Base (30050) only in the lab (`step=off;mission=30050;reason=mb_lab_only` in
play): no hidden run has covered 30050, its index Despawn included
(`coop-vehicle-gone-mb.txt` has never run). `step=on` names the wreck key
(`wreck=0|1`, below).

| Code | Name | Dir | Wire | Lua fields | Queue | Body |
|---|---|---|---|---|---|---|
| 0x2E | VEHICLE_GONE | H->G | R | seq 1.., id 25600..26111, cause 1 fulton / 2 destroyed / 3 lab, actor 0 host / 1 guest / 2 lab, rid (0 none), vehicle type (255 unknown), x, y, z mm | world | 21 |
| 0x2F | REQ_VEHICLE_GONE | G->H | R | rid 1.., id 25600..26111, cause 1..3 as above or 4 an unattributed destruction (since the D12 review, section 19), vehicle type, x, y, z mm (the guest's copy) | world (request) | 18 |

Both are level 3, lab and play, epoch-checked world ops (`native/world_wire.cpp`);
the id is a TppVehicle2 GameObjectId (class 50: 25600 + pool index). The host
answers a request natively with RECEIVED (REJECT 5 for another epoch, 10
below level 3), then from Lua (`lua/coop/world_vehicle.lua`, spliced last):

- **Detection.** `W.observe` hands every message whose id no soldier map names
  to the module (`pcall`: its errors never fail world sync; the module's own
  handlers, hooks and poll run in `pcall` too). The host watches a
  vehicle after Fulton, FultonInfo, Dead or VehicleBroken (registered as an
  observed message kind only while the key is on); the guest only after its
  own FultonInfo or Dead (attacker `local`) or a VehicleBroken within 10 s of a
  local Damage. M4 found that the game posts no GameObject Damage message for
  a vehicle hit by gunfire (hidden run `35ba7929`, the attacker census: none
  for the jeep whose life the guest's rounds lowered), so the last rule never
  fires for gunfire and a jeep the guest destroys stayed a wreck on its copy
  only (`5179b50b`); explosives are untested. Since D12 (section 19, key
  `world_vehicle_break`) an unattributed break is requested too (cause 4 since
  the D12 review) and the host judges it. The pose (mm) and
  `GetVehicleType` are read at that message.
  FultonFailed cancels; the loss counts when the instance stops answering
  `GetPosition` (its pool slot released), or when `IsAlive` reads back false:
  at once for a destroyed vehicle (its wreck stays in the pool), but for a
  Fulton only after 10 s of not alive with no FultonFailed and no `IsAlive`
  true again (`step=settle;dy_mm=`), so a balloon that fails after `IsAlive`
  dropped never leaves a one-sided removal. Within 30 s of the message (else
  `step=watch_drop;reason=still_alive;dy_mm=<rise since the message>`, the
  evidence that a Fultoned instance stays alive: then this detection does not
  work). **Unverified for a real Fulton:** nothing ties a Fulton to the
  `0x800004` bits; the lab lever's `fulton` form (the engine's own `Seize`)
  and a player's real Fulton are the checks (STATUS 0.13.6).
- **Host loss:** VEHICLE_GONE {seq, id, cause, actor 0, rid 0, type, pose};
  kept (64) and re-sent at the next PAIRED of the mission. **Guest loss:**
  REQ_VEHICLE_GONE; no RECEIVED in 5 s sends it once more; while unpaired it
  waits for the next PAIRED (dropped when the host epoch changed). The guest
  never announces.
- **Receiver** (the guest for VEHICLE_GONE, the host for a request): the id
  must be a live TppVehicle2 within 40 m of the reported position with the same
  type (when both are known) and, beyond 1 m, the nearest vehicle of its type
  there (a tie counts as nearer; `reason=not_nearest`). An id this copy lost
  itself (`step=absent;reason=lost_here`) or a wreck (still in the pool:
  `GetPosition` answers, `IsAlive` false; `reason=wreck`) is already gone here
  and is never matched. Pool indices can differ between copies, so an id with
  no instance (`reason=no_instance`), or a far, other-type or not-nearest one,
  is matched to the ONE live vehicle of that type within 5 m of the position
  (two: `reason=ambiguous`, nothing removed; never one this copy is watching or
  lost itself). Nothing is removed while unlinked (unpaired or below level 3:
  `step=hold;reason=unpaired`). It is removed only while free: never the local
  player's vehicle (`vars.playerVehicleGameObjectId` with ON_VEHICLE), any
  occupied `GetRiderId` seat (the local back-seat passenger, a soldier; body 1
  is never in rider[]), one this copy is watching itself (`own_watch`) or a
  vehicle the partner drives here: a RIDE_FAST datagram that keeps a track
  latches it for 10 s past the last one (a datagram gap; the clock stands while
  unlinked, when world_ride_pose wipes its tracks) until its exit RIDE_BODY
  (kind 3, occupied 0). Otherwise it is held and re-checked every 250 ms
  (`step=hold;reason=local_rider|occupied|own_watch|partner_ride|unpaired|riders_unreadable|pose_unreadable`)
  for 120 s (`step=hold_timeout`; a held request then gets REJECT 9). The
  viewer's own player is never moved or dismounted.
- **Index 0 first.** Index 0 (25600, the relief vehicle: M3 / E17) is refused
  `index0` before any removal form, the wreck below included (2a3d97d wrecked a
  destroyed 25600 on the receiver; M4 review).
- **Wreck parity** (M4, behind its own key since the M4 review): lab.ini
  `world_vehicle_wreck=0|1` (CFG r[15] bit 4, `world::VehicleModules`; lab and
  play, **0 in both** unless named). With it on, a destroyed vehicle (cause 2) is
  wrecked on the receiver by the instance command `SetBodyLife{life=0}` (the
  engine's own destruction: VehicleBroken, explosion and fire, IsAlive false, the
  wreck in the pool; E5, `d407f115`, `0edadbe2`) instead of a Despawn, read back
  like one (`via=wreck`). A wreck still alive 2 s later falls back to the Despawn
  (`step=fallback;id=;from=wreck;via=;ok=`); with no Despawn form the id is not
  marked lost, so a re-sent announcement retries it. It stays off in play: the
  only play destruction attempt (`e235bda6`) never destroyed the jeep, and the
  blast it replays on the receiver (it knocked the guest down in `5179b50b`) was
  never measured with live guards and godmode off, where it could kill guest-copy
  guards (not synced in play: unattributed local kills) or wreck a second vehicle
  on the guest's copy only. From 2a3d97d to the review it rode on VEHICLE_STATE's
  key, which 482eb74 turned on in play.
- **Removal:** the TppVehicle2 class command `Despawn` (`0xB30AF0`,
  StrCode32 `0xE22EC430`, dispatched by `TppVehicle2_LuaCommands` `0xB305B0`):
  an authored vehicle by its locator (`veh_lv_0000..0031`, `veh_trc_0000..0015`,
  `veh_wav_0000..0015` resolving to the id: the retail `TppEnemy.DespawnVehicle`
  form), an unnamed (sortie / supply) vehicle by `index = id % 512`
  (`ParseLuaSpawnIndex` `0xB304E0` reads the command's `index`; index 0, the
  relief vehicle, is an engine error and is refused here as `index0`) **only at
  Mother Base (30050)**. Despawn releases the pool instance itself (the
  manager's `+0x1C8` vtable+8 and `VehicleCommunicationInfo::Clear`). For a
  named record it then calls `GameObjectImpl::Kill` `0xB2F850` on the old
  name, which erases the name->index entry; the locator form writes the name
  back and re-inserts it, the index form does not (the record stays
  empty-named and `FastHashIndexMap::Insert` gets the empty hash), so retail
  scripts would no longer find that vehicle by name. Elsewhere a vehicle
  outside the scanned families is therefore refused (`no_locator`). There is
  no Fulton, no balloon and no resource on the receiver (the Fultoning copy
  keeps its stock vehicle). Read back with `IsAlive` (`0xED0E1BD6`: the class
  dispatcher answers false when the index's pool bit is clear, nothing for an
  index past the pool; the instance dispatcher `0xB2B710` answers record+0x70
  & 0x800004 clear): `step=applied;id=;via=locator|index;src=rx|req;ok=1|0`.
  On the host an applied (or already absent) request is announced with actor 1,
  the rid and the id **the guest sent** (when it was matched to another pool
  index here, the guest's index: the host keeps its own id in its gone list),
  which ends the guest's request (`step=req_done;result=gone`, closed only when
  the rid's request names that id; a re-request of it is answered again);
  refusals are REJECT 1 (not a vehicle) or 8 (far, type, not_nearest,
  ambiguous, index0, no_locator, send_error).
  The stock `Seize` instance command (`0x12E8F866`, options Fulton / FadeOut /
  Instant, CheckFultonType / CheckFarFromPlayer / DirectAccount) also removes a
  vehicle but accounts it as a resource; it is not used.
- **Lab lever** (`debug vehicle_remove nearest [sync|fulton]`, refused natively
  outside mode=lab with offline redirected saves and in Lua outside mode=lab):
  the nearest free vehicle within 60 m of this copy's player (never the local
  one or an occupied one). Without `sync` only this copy loses it (the session's
  divergence; `vehicle_gone_status` marks it `L`); with `sync` (key on) it is
  lost as cause 3 through the path above (it calls the loss path directly, so
  it skips the detection). With `fulton` (key on) the engine's own vehicle
  Fulton runs instead: the stock `Seize` instance command (`0x12E8F866`) with
  options `{"Fulton"}` (the retail `TppMission` relief-vehicle form without
  CheckFultonType / DirectAccount), watched as cause 1 like the local player's
  FultonInfo (`step=watch;m=Seize;att=lever`), so the real readback decides. `debug vehicle_gone_status` is
  read-only: `vgone;on=;role=;gone=;held=;watch=;req=`.
- Lua lines (`world_lua=event=vehicle_gone;step=...`): `on` (with `wreck=`),
  `off` (Mother Base in play), `watch`,
  `settle`, `watch_drop`, `lost`, `tx`, `req`, `rx`, `rx_req`, `matched`, `hold`,
  `hold_timeout`, `refused`, `absent`, `applied`, `fallback`, `req_done`, `req_timeout`,
  `resend`, `lever`, `ignored`, `error` (one error turns only this module off
  for the mission).

Tests: `world_wire_tests` (VehicleGone: table, bounds, world queue delivery,
RECEIVED, REJECT 5 and 10, stale purge), `world_receipt_tests` (r[15]),
`lab_config_tests`, `world_vehicle_lua_check` (the module against a fake pool,
then with `world_sync.lua`: the observer hook and r[15]), `debug_vehicle_lua_check`,
`worker_session_tests`, `lab_scripts_tests`; `world_vehicle_lua_check` is in
the worker gate (`Get-WorkerTests`). Hidden-worker scenarios:
`tools/worker/scenarios/coop-vehicle-gone.txt` (lab seed, Wakh Sind: the
guest's loss reaches the host, the divergence, the hold under a seated guest,
the engine's own Fulton through the detection path) and
`coop-vehicle-gone-mb.txt` (real seed, the Mother Base sortie jeep: the
index path), `vehicle-gone-player.txt` (lab, Africa: each player's own Fulton
by hand) and `vehicle-gone-play.txt` (the play rehearsal of the same, `-Mode
play -WanSim bad`). Hidden runs: docs/STATUS.md 0.13.6 (`075c1ea4`) and M4
(`92720b75`, `0edadbe2`, `46b8fc12`, `e44516d7`).

## 17. Vehicle state (M4, docs/vehicles-companions-design.md 6.4)

Vehicles nobody drives were per copy: a jeep pushed, left or shot on the host's
copy stayed as it was on the guest's (a wreck on one copy only in `7561ab63`).
VEHICLE_STATE makes the pose, body life and state word of a vehicle nobody
drives host-authoritative. Lab.ini `world_vehicle_state=0|1` (lab and play, not
a play refusal; since M4 **0 in the lab** unless named and **1 in play** unless
named, after the lab FRAMEs `d407f115` / `0edadbe2` and the play rehearsals
`e235bda6` / `d9b48fcb` with `-WanSim bad`; since the M4 review also `4bbbfde4`
and `b31a1b65`, the shipped default with no vehicle key named; `b31a1b65` is the
first pose applied in play, `moved=1` after the host's bail out of a rolling jeep),
carried to Lua as CFG **r[15] bit 2** (r[15] is the vehicle modules bitmask
since M4: bit 1 VEHICLE_GONE, bit 2 VEHICLE_STATE, bit 4 VEHICLE_GONE's wreck
parity since the M4 review; `world::VehicleModules`, `world_link::HandleCfg`; a
missing or out-of-range value is 0). Both copies need it; a guest without it
logs `event=vehicle_state;step=ignored;reason=off` once. Like VEHICLE_GONE it
runs at Mother Base (30050) only in the lab since the M4 review
(`step=off;mission=30050;reason=mb_lab_only` in play): no hidden run has covered
30050, where the guest matches the cluster jeeps `veh_clNN_clNN_0000` by name.

| Code | Name | Dir | Wire | Lua fields | Queue | Body |
|---|---|---|---|---|---|---|
| 0x33 | VEHICLE_STATE | H->G | R | seq 1.., id 25600..26111 (the host's pool slot), locator hash (StrCode32 of `veh_lv_/trc_/wav_NNNN` or the Mother Base `veh_clNN_clNN_0000`; 0 unnamed), vehicle type (255 unknown), flags (1 alive, 2 sweep, 4 refresh), x, y, z mm, yaw mrad (+-31416), body life (the engine's u16), state word (0..0x3FF), zero-parts mask (0..0xFFE) | world | 32 |

32 bytes is the largest body an outbound queue entry holds (`OutEntry`,
`kOpHeaderSize + 32`): a field more is a new op.

- **Native read** (read-only, `MGS_COOP_VEHICLE_LIFE_V1 -- <first id> <n 1..32>`,
  `game_script_observer.cpp` `SelectVehicleLifeReceipt`, key on, partner gate,
  runtime thread, 30010/30020/30050): `partner_vehicle::ReadVehicle` (the census
  chain) plus `ReadLife`: the object's vt+0x98 must be `0xB2DD00` with its 24
  pinned bytes; the life block is `*(object+0x150) + index*0x18`, 12 u16 (word 0
  the body life that the instance command `SetBodyLife` writes, `0xB2BC69`;
  `StoreToSVars` `0xB35BC0` copies all 12 into `ene_veh_life`). The state word is
  StoreToSVars' own `ene_veh_state` encoding of rec+0x70 / rec+0x7C
  (`partner_vehicle::SvState`). Reply `{c=<pool count>,[id]={life,zero,parts,state},...}`
  or `{refused='<reason>'}` (one `vehicle_life=refused; reason=` line per change).
  There is no Lua read of the life: the TppVehicle2 instance dispatcher
  (`0xB2B710..0xB2C1A7`) has `SetBodyLife`, `SetBodyDamageRate`,
  `SetWheelDamageRate`, `GetState` (two bits: `V+8[idx*32+0x1E] & 0x80`, rec+0x70
  & 0x18000) and no getter.
- **Host** (`lua/coop/world_vehicle_state.lua`, spliced after `world_vehicle.lua`,
  whose pool helpers it uses): scans 4 ids every 250 ms (a 24-vehicle pool in
  1.5 s) plus at once any id a `Damage` message names (the game posts none for a
  vehicle hit by gunfire, `35ba7929`, so in practice the scan); sends a live vehicle when
  it moved > 0.5 m or turned > 5 deg, or its life, state word or zero mask
  changed (at most once per 500 ms per vehicle; `step=tx;why=moved|life|state|parts`),
  every eligible vehicle after each PAIRED (`step=sweep`, flag 2, then
  `step=sweep_done;sent=`) and each again 10 s after its last send (flag 4,
  silent). Skipped (`step=skip;reason=`, once per change; `step=resume` after):
  the local player's vehicle (`local_rider`), an occupied rider[] seat
  (`occupied`: an NPC, the local passenger), a vehicle the partner drives here
  (`partner_ride`: a RIDE_FAST track, latched 10 s, world_vehicle.lua `V.busy`),
  the authored LRRP trucks (`lrrp`: `veh_trc_0000..0005` in 30010,
  `..0006` in 30020, each copy's own AI drives them until M8), one VEHICLE_GONE
  lost or watches (`gone_here`, `own_watch`). A wreck (IsAlive false) is never
  sent; its removal is VEHICLE_GONE's. An unreadable life is sent as 65535.
- **Guest:** applies only while linked, to the vehicle the locator hash names
  here (any pool index) when it lies within 40 m of the host's pose (`far`
  otherwise: a locator that resolves elsewhere here is never teleported; M4
  review), or for an unnamed one (never index 0, the relief vehicle: M3,
  `index0`) the same id when it is alive, of that type and the nearest of its
  type to the reported position within 40 m (`not_nearest`, `no_match`).
  Refused (`step=refused;reason=`): `no_locator`, `absent` (not alive here),
  `far`, `lrrp`, `gone_here`, and `place_failed` (a needed move whose
  `R.place` did not take: `step=refused;id=<host id>;reason=place_failed;here=;d_mm=;err_mm=;...`,
  never logged as `same`; the next state retries). Held (`step=hold;here=;reason=`,
  re-checked every 250 ms, `step=hold_timeout` after 10 s; the next refresh
  retries): `local_rider`, `occupied`, `partner_ride`, `own_watch`,
  `player_near` (for a move: the local player within 3.5 m of where it would go
  or of where it stands now, so a vehicle is never pulled away from a player
  walking up to it or in a GetOn before ON_VEHICLE; M4 review), `unpaired`.
  Applied: `SetPosition{position, rotY}` (world_ride_pose `R.place`) when it is
  > 0.5 m or 5 deg off, read back (`err_mm`); `SetBodyLife{life}` whenever this
  copy's body life differs from the host's, in both directions (M4 review: with
  the downward-only rule of the first build, guest-only hits added up between
  states, so the guest's copy reached 0 first and the split was permanent: no
  vehicle Damage message, so no request; e235bda6 shows the guest copy 102 below
  the host between two states), read back through the native read; a host life
  of 0 is written as 1 (a destruction is VEHICLE_GONE's, behind its own key).
  Lines: `step=applied;id=<here>;from=<host id>;moved=;d_mm=;err_mm=;dyaw=;life=;was=;sv=;here_sv=;zero=;here_zero=;f=`
  (every apply) and `step=same` (for every host change, `f=1`; for a sweep or a
  refresh once per change of outcome, where a hold counts as a change).
- **Lab levers** (refused natively outside the one-PC lab and in Lua outside
  mode=lab): `debug vehicle_push <id|nearest> <dx> <dz>` (this copy's vehicle
  moved by up to 50 m, y + 0.3 m: a stand-in for a shove), `debug vehicle_life
  <id|nearest> <life>` (this copy's body life through `SetBodyLife`, read back;
  0 is the E5 wreck attempt). `debug vehicle_state_status [id]` is read-only
  (play too).
- **Not covered:** a guest's own damage or push never reaches the host (the
  host's next state, the 10 s refresh at the latest, undoes a guest push and puts
  a guest-only hit back; a guest's destruction stays on its copy, section 16
  "Detection"); wheel lives are sent (zero mask) and logged but not written (no
  write API); a SetBodyLife raise on a copy that burns is [U] (fire is not a
  function of the life word, `d407f115`); AI-driven and reinforcement vehicles
  are M8.

Tests: `world_wire_tests` (VehicleState: table, bounds, world queue, stale
purge, level 3), `world_receipt_tests` (r[15] bitmask), `lab_config_tests`,
`partner_vehicle_tests` (TestLife: SvState, the life block, every refusal),
`world_vehicle_state_lua_check` (in the worker gate), `debug_vehicle_lua_check`,
`lab_scripts_tests`, `worker_session_tests` (the `logmark` step). Hidden-worker
scenarios `tools/worker/scenarios/vehicle-state.txt` (lab, `-SaveSeed
africa-jeep`) and `vehicle-state-play.txt` (the play rehearsal, `-Mode play
-WanSim bad`, lab seed, no vehicle key named: the shipped default); both mark
the logs (`logmark`) before each event, so a post-event expectation cannot be
met by an earlier line; `vehicle-state-play-host.txt` (play, no key named: the
host's bail and a C4 destruction). Hidden runs: docs/STATUS.md M4 (`d407f115`,
`0edadbe2`, `e235bda6`, `d9b48fcb`) and M4 review fixes (`3585869d`, `4bbbfde4`,
`b31a1b65`).

## 18. Sortie vehicle (M3, docs/vehicles-companions-design.md 6.3)

The host's sortie (relief) vehicle, TppVehicle2 index 0 (25600), is drawn by each
copy's own engine from the host's choice (route A). Lab.ini `world_relief=0|1`
(lab and play, not a play refusal; **0 in lab and play** unless named: it was 1
from `5a6bac8` until the M3 review reverted it in `4d18561`), carried to Lua as
CFG **r[17]** (`world_link::HandleCfg`; a missing value is 0); the module is
`lua/coop/world_relief_sync.lua`, after `world_relief.lua` (the E2/E17/E18 lab
levers) at the end of the world chunks.

- **Seeds.** The follow seeds always carry the host's relief carry vars
  (`reliefVehicleCarryType`, `...Attitude[4]`, `...Life[12]`, `...Ammo[3]`,
  `...State`) and the ten paint presets (`userPresetVehiclePaintType`), key class
  `relief` (`world_follow_seed.lua`); the guest applies them before its
  `TppMission.Load` only with the key on and keeps its own values for its saves
  (D8 b, a `TppSave.SaveVarsToSlot` wrapper; its live vars come back at its next
  solo load).
- **VEHICLE_SPAWN 0x34** (host to guest, world queue, reliable, level 3, lab and
  play; 11 fields, 31 bytes): seq, the host's mission, id 25600..26111 (M3 sends
  25600), cause 1..3 (1 relief; 2 drop and 3 reinforce reserved), the carry type
  (whole u32), the paint preset of that type, flags 0..3 (1 alive, 2 the pose is
  the carry attitude), xyz mm and yaw mrad (the carry attitude, or the live pose
  0.5 m up). Sent in free roam on change and every 5 s. A guest copy without the
  host's index 0 (a continue, a late join) writes the host's carry and calls
  `Vehicle.LoadCarry` mid mission; one more than 3 m from the host's pose, or
  parked more than 20 m off, is placed there; one the host lacks for 15 s, or of
  another type, is removed without a credit (`why=host_none`).
- **Losses.** With VEHICLE_GONE on (section 16), the receiver removes index 0
  with `Seize {"Instant"}` and a one-time skip of its `TppTerminal.OnFulton` for
  25600, never `Despawn` (VEHICLE_GONE's own `index0` refusal still applies with
  `world_relief` off). A guest-side loss of index 0 is REQ_VEHICLE_GONE for 25600
  whatever `world_vehicle_gone` says; the host removes its own with the stock
  `Seize {"Fulton"}` (credited once, on the host) or, for a destruction,
  quietly; REJECT 8 while the host rides it. The guest's mission-end
  `TppMission.SeizeReliefVehicleOn*` is skipped (one Mother Base credit, the
  host's).
- **Refusals.** The guest's iDroid VEHICLES drop is inactive in co-op free roam
  (NOTE 13 a 4, toast). The LOADOUT drop is D11's (decided 2026-10-29: blocked in
  co-op), section 19, key `world_loadout_block`, whatever `world_relief` says; the
  lab lever `relief_lab loadout on|off` is gone.
- **Since the integration (2026-10-29).** VEHICLE_GONE is on by default in play
  at 30010 and 30020 (section 16, M4), so a play session that names
  `world_relief=1` runs the VEHICLE_GONE-on removal path (lab run `cbc30b91`); the
  VEHICLE_GONE-off path (the reconcile's `host_none`, lab run `5c35644d`) applies
  in the lab and wherever `world_vehicle_gone=0` is named.

Tests: `world_wire_tests` (VehicleSpawnOp), `world_receipt_tests` (r[17]),
`world_relief_lua_check`, `world_relief_sync_lua_check`, `lab_config_tests`,
`lab_ini_check`. Hidden runs: docs/STATUS.md M3 and M3 review fixes.

## 19. The vehicle decisions of 2026-10-29 (D11, D12, D14; docs/vehicles-companions-design.md 8.11, 8.12)

Wire: no new op and no new field. One value range grew in the D12 review
(2026-10-29, `8b00f80`): REQ_VEHICLE_GONE's cause is **1..4** (was 1..3); 4 is a
destruction the guest's copy could not attribute (D12). VEHICLE_GONE keeps 1..3:
the host answers a cause 4 request as cause 2 (actor 1, the rid). A build before
`8b00f80` drops a cause 4 request as `bad_op`; play pairing refuses mismatched
builds, so only a lab pair of two builds could meet it (the guest's request then
ends in `req_timeout`). D14 changes only how a receiver removes its copy; D11 is
local to the guest. The module is `lua/coop/world_vehicle_rules.lua`, spliced
after `world_vehicle_state.lua` (before `world_companion.lua`); it wraps
`W.vehicleGone.decide`, `.remove`, `.request` and `.status`, the REQ_VEHICLE_GONE
and REJECT handlers, `W.vehicleState.readLife`, `W.observe` and `W.tick`, and
`world_relief_sync.lua`, spliced later, still takes index 0 first. Three lab.ini
keys, lab and play (no refusal bit), carried in the CFG **r[15]** vehicle bitmask,
which is now 0..63 (`world::VehicleModules`; Lua treats a missing or out-of-range
value as 0):

| Bit | Key | Decision | Needs |
|---|---|---|---|
| 8 | `world_vehicle_break` | D12 | VEHICLE_GONE on, at 30010 / 30020 (Mother Base lab only, as VEHICLE_GONE) |
| 16 | `world_vehicle_credit` | D14 | VEHICLE_GONE on, as above |
| 32 | `world_loadout_block` | D11 | world sync started (any mission) |

Each is **on in play unless named** and off in the lab unless named, like the
other vehicle modules (`f4ec5c1`; docs/STATUS.md for the runs behind each). A
named invalid value counts as named and off.
The keys are read per PC. Since the D12 review a mismatch no longer applies
anything unjudged: a guest with `world_vehicle_break` against a host without it
gets REJECT 11 for each unattributed break (host `step=refused;src=req;
reason=break_off`, guest `step=break_refused;reason=11`): that vehicle stays split,
as before D12. A guest without the key never asks. A receiver without
`world_vehicle_credit` despawns without a credit. `world_loadout_block` is the
guest's own: the host's value does not matter.

- **D11, the guest's supply drops that can bring a vehicle.** While the guest is
  paired (or unpaired inside the mission of its last pairing, until that mission's
  final stop), in game, the module sets the iDroid items `MSN_DROP_LOADOUT` and,
  since the user's pick of the D11 review (only the host picks vehicles),
  `MSN_DROP_VEHICLE` inactive with `TppUiCommand.SetMbTopMenuItemActive` every 2 s
  while the iDroid is closed (the first pass of a mission also while it is open;
  `TppTerminal.SetUp` leaves every item active). Selecting one posts the stock
  Terminal message `MbDvcActSelectNonActiveMenu` (arg StrCode32 of the item):
  `step=refused;path=idroid_drop_loadout` with NOTE 13 a 5 ("Only the host can
  order a loadout drop in co-op: it can carry a vehicle.") or
  `step=refused;path=idroid_drop_vehicle` with NOTE 13 a 4 ("Only the host can
  bring or request a vehicle in co-op."), each toast at most every 3 s. Guard
  lines: `step=guard;path=idroid_drop_loadout|idroid_drop_vehicle;via=menu_inactive;
  mission=` once per mission. The final stop sets them active again
  (`step=unguard`). The host is never touched, nor any other item. With the key on
  the module owns the VEHICLES item in every co-op mission, whatever `world_relief`
  says (`VR.ownsVehicleDrop`); `world_relief_sync.lua` then never sets, refuses or
  toasts it (its own `event=relief` guard and refusal lines appear only with this
  key off).
- **D12, a guest-side break.** The guest's VEHICLE_GONE observer asks
  `V.unattributed(id)` for a `VehicleBroken` with no local hit (the game posts no
  Damage message for a vehicle hit, `35ba7929`): with the key on, a vehicle other
  than index 0 that this copy did not itself remove or wreck in the last 5 s is
  watched as a destruction (`event=vehicle_rules;step=break_seen`), and its loss is
  requested as REQ_VEHICLE_GONE **cause 4**, at most 6 per 60 s; more wait in a
  queue of 32 (`step=break_queued;id=;n=`, then `step=break_ask;id=;queued_ms=`),
  beyond it a break is skipped (`step=break_skip;reason=queue_full`). The host:
  without its own key a cause 4 request gets REJECT 11 (above). With it, it judges
  every guest destruction request (cause 2 or 4) after VEHICLE_GONE's own match:
  a hold for `local_rider` (the host's own player in that vehicle) is refused at
  once (`step=break_judge;ok=0;why=ridden;by=local_rider`; a copy that is broken
  there too was already answered as gone); any other hold (`occupied`: D-Dog or a
  soldier in a seat; `partner_ride`; `pose_unreadable`) stays VEHICLE_GONE's hold,
  re-checked every 250 ms, REJECT 9 after 120 s. Then it applies only when the
  guest's player, from its DOWNED_STATUS position (`world_downed.lua`, 10 Hz, at
  most 3 s old), stood within **300 m** of the pose the guest reported, judged on
  the closer of its position when the request arrived and its current one
  (`why=near`), or the host's copy is at a quarter or less of the highest body life
  VEHICLE_STATE's native read gave for it this mission (`why=heavy`); else
  `why=guest_far`. With no known position it waits (`step=break_hold;why=
  guest_unknown`, REJECT 9 after 120 s). At most 4 are applied per 20 s; more wait
  (`step=break_hold;why=rate;n=`). One `step=break_judge;id=;rid=;ok=;why=;d_mm=;
  life=;peak=` line per request, at its outcome. Refusals are VEHICLE_GONE's
  `step=refused;src=req;reason=<why>` and REJECT 8; the guest logs
  `step=break_refused;id=;rid=;reason=<8|9|11>;here=wreck`. Applied, the host's copy
  becomes the same wreck: the instance command `SetBodyLife{life=0}` (the engine's
  own destruction and blast, E5), id marked gone first, read back like the wreck
  parity (a wreck still alive after 2 s is despawned, `step=fallback;from=wreck`),
  whatever `world_vehicle_wreck` says (that key is the guest's receiver wreck).
  **The blast rule** (`0e79337`, after run `7762536a` measured the wreck's later
  explosion, about 8 s after `SetBodyLife 0`, at 3500 of 7800 life on the host
  beside it): while the host's own player (`vars.playerPosX/Z`) or the partner (its
  fresh DOWNED_STATUS position) stands within **15 m** of the host's copy, or the
  copy's or the player's position cannot be read, the host removes its copy with the
  Despawn instead (`step=break_safe;id=;near=host|partner|unknown;d_mm=;form=despawn`,
  then VEHICLE_GONE's `applied;via=locator`): no explosion on the host's PC, the
  guest's copy keeps its wreck. No wire change. The
  announcement back (cause 2, actor 1, the rid) ends the guest's request. A refused
  guest copy keeps its wreck: no Lua command undoes one (`SetBodyLife` writes the
  life word, `IsAlive` reads rec+0x70 bits that no instance command is known to
  clear [I], section 3.6 of the design), and VEHICLE_STATE refuses that id there
  (`absent` for a named vehicle, `no_match` for an unnamed one).
- **D14, the receiver's credit.** A receiver (the guest for VEHICLE_GONE, the host
  for a request) removes a Fultoned vehicle (cause 1), never index 0, with the
  stock instance command `Seize {"Instant"}` (no DirectAccount, no CheckFultonType)
  instead of `Despawn`: its own engine then posts the Fulton messages and runs
  `TppTerminal.OnFulton` -> `OnFultonVehicle` -> `AddTempResource` (E17; no GMP is
  charged), so the vehicle is credited once to that copy's save as well; the
  temporary resource commits at the mission end. The id is marked gone before the
  command, so that copy's own `Fulton` / `FultonInfo` / `Neutralize` (att local) are
  never taken for a loss. Lines: `step=credit_seize;id=;role=;mission=`, VEHICLE_GONE's
  `step=applied;via=seize`, a Seize still alive after 2 s is despawned
  (`step=fallback;from=seize`), and every `OnFultonVehicle` call is logged
  `step=credit;fn=OnFultonVehicle;id=;res=<resource id>;via=receiver|own` (receiver:
  within 10 s of its own Seize; `step=credit_missing` when none came). Re-sent
  announcements and repeated requests find the id gone and credit nothing more.
  The sortie vehicle (index 0) keeps M3's host-only credit (section 18).
- `debug vehicle_gone_status` appends `vr=<break><credit><loadout><greyed>;j=;a=;r=;q=;s=;c=`
  (judged, applied, refused, asked, Seize credits, credits seen); the base part is
  cut to fit 160 characters, never these counters.

Tests: `world_vehicle_rules_lua_check` (in the worker gate), `world_vehicle_lua_check`
(r[15] 0..63, the observer hook, cause 4 and REJECT 11), `world_relief_sync_lua_check`
(LOADOUT is not the relief module's; VEHICLES is not while the rules module owns it),
`world_wire_tests` (REQ_VEHICLE_GONE cause 1..4), `world_receipt_tests`
(`VehicleModules` bits 8/16/32), `lab_config_tests`, `lab_ini_check`,
`lab_scripts_tests`, `worker_session_tests`. Scenarios
`tools/worker/scenarios/vehicle-decisions-lab.txt`, `vehicle-decisions-play.txt`,
`vehicle-decisions-refuse.txt`, `vehicle-decisions-review-lab.txt` and
`vehicle-decisions-review-play.txt`. Hidden runs: docs/STATUS.md "D11, D12, D14" and
"D11 / D12 review fixes".
