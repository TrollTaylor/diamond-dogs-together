# World sync W0-W4: protocol and implementation

Implements phases W0 (plumbing), W1 (shared enemy outcomes and alert
escalation in free roam) and W2 (clock and weather) of
[world-sync-design.md](world-sync-design.md), plus the lab probes and a
lab-only mission jump. Level 3 also includes supported-scene host travel
(W3), forward-only reload alignment, shared death and checkpoint copying
(W4), described in section 13. Story missions (W5), full progression copying
and soldier transforms (W6) remain unimplemented.

W0-W2 ran live in [October 5 testing](testing-2026-10-05-world-sync.md).
W3/W4 automated fixtures do not establish live acceptance. Script citations
refer to the extracted 1.0.15.4 scripts (`lib/...`, `mission/...`).

Code:

| Part | Files |
|---|---|
| Link DATAGRAM and hooks | `native/link_protocol.cpp`, `native/partner_net.cpp` |
| Core (pure, unit tested) | `native/world_wire.cpp` (op table, codec, queues, pairing, schedulers), `native/world_receipt.cpp` (receipts, trigger grammar) |
| Glue (Win32) | `native/world_link.cpp` (lock, hooks, pump, `world.trigger`), `native/game_script_observer.cpp` (receipt routing, install) |
| Lua (spliced into TppMain after `partner_sync.lua`) | `lua/coop/world_sync.lua`, `world_enemy.lua`, `world_outcome.lua`, `world_phase.lua`, `world_env.lua`, `world_probe.lua`, `world_goto.lua`, `world_follow_seed.lua`, `world_follow.lua`, `world_align.lua`, `world_recovery.lua` |
| Config and tools | `lab.ini` `world` / `world_probe`, `tools/lab/Set-WorldTrigger.ps1`, `tools/lab/WorldSync.psm1`, `Play-OfflineCoop.ps1 -World`, `Play-Coop.ps1 -World` |
| Tests | `tests/world_wire_tests.cpp`, `tests/world_receipt_tests.cpp`, `tests/world_sync_lua_check.lua` (fragment and composed TppMain), additions to the link, lab config, host-trial patch and script suites |

## 1. Levels and gating

`lab.ini` `world=0..3` (default 0 in the lab, in play, in Play-OfflineCoop and
in Play-Coop):

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
areaEnemy=f30020_enemy}`, 30050 `{clock}`; every other mission (the ACC 40050
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
W3/W4 use codes 0x09-0x0F and 0x18-0x1C (section 13). Codes 0x1D-0x1F
and bundle flag bit 0 (chunking) remain reserved.

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
call, no logging, no I/O under it); lines are formatted by the core and
written after unlock. The receive thread drains reliable channel 1 (32 per
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
| CFG | `return 'MGS_COOP_WORLD_CFG_V1' -- <mission> <phase>` (0 query, 1 start) | `return level,role,proto,link,epoch,probe,mode,session` (first seven fields preserved; mode 1 lab, 2 play; session is local link generation, zero disconnected; the start consumes a scheduled probe) | query any; start runtime, not terminating |
| TX | `return 'MGS_COOP_WORLD_TX_V1' -- <op> <v1> ... <vn>` | `return 1` / `return 0,<reason>`: 1 syntax, 2 unknown_op, 3 bad_args, 4 wrong_role, 5 not_paired, 6 queue_full, 7 disabled, 8 lab_only | runtime; MISSION_END also termination |
| RX | `return 'MGS_COOP_WORLD_RX_V1' -- <mask>` (1 control; 3 control, world, U slots) | `return queue,op,v...` or empty `return 0,0,flags,qctrl,qworld,epoch` (flags 1 link, 2 paired, 4 resync_needed, 8 level0) | runtime |
| LOG | `return 'MGS_COOP_WORLD_LOG_V1' -- <payload>` (1..160 bytes) | `return true`; logs `world_lua=...` (partner sanitizer, `;` -> `; `; at most 200 lines per 10 s, then `world=log_cap`) | runtime or termination |

Exact classification (name == source, NUL-free, at most 240 bytes). Integers
only: an optional `-` (i32 fields), no `+`, no leading zeros, no `-0`, no `.`,
single spaces; u32 up to 4294967295. Replies fit 224 bytes (17 values). TX,
RX and LOG are quiet (no entry/return lines); CFG logs like any receipt. A
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
`world=N` (and `world_probe=`) to both lab.ini files only when set; console key
`[G]` sends `goto=30010` to the host at `-World 3`, letting W3 bring the
guest along. At levels 1/2 it sends to both copies for independent manual
travel; at level 0 it sends nothing. `Play-Coop.ps1 -World 0..3` adds
`world=N` after the play partner keys (never `world_probe`).
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

Weather (eff >= 3; 30010, 30020): the host sends `vars.weather` on change
(30020: FOGGY becomes 255 = release; the mist is local to an area); the guest
calls `TppWeather.ForceRequestWeather(type, 20)` (nothing during demos), a 255
cancels its force and unpauses random weather, and it re-forces after three
digests that disagree (after interp + 5 s). Random weather is paused at PAIRED
and every digest while the host weather is 0..5. The same userId and FORCE
priority as the game's own forces: forces are cancelled and random weather
unpaused at stop, on LEVEL 0 and after an error.

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

Lua payloads (`world_lua=event=...`): `started`, `inactive` (reason
`missing_required`, `bad_const`, `continue`, `game_over`, `roster_mismatch`,
`level0`), `missing`, `error`, `stop`, `paired`, `unpaired`, `obs`,
`obs_cap`, `commit`, `req`, `req_rx` (host refusal), `req_done`,
`req_timeout`, `req_drop`, `req_hold`, `apply`, `ledger`, `divergence`, `phase`,
`req_phase`, `floor`, `phase_vec`, `area`, `digest_mismatch`, `resync`,
`resync_needed`, `clock`, `clock_tx`, `clock_check`, `weather`,
`weather_check`, `roster`, `stats`, `bad_value`, `probe`, `goto`.

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

This is the test recipe and expected evidence, not a live pass report.
W3/W4 acceptance remains pending for the integrated DLL.

Logs: `<host|guest dir>\mgs-coop\script-observer.log`; Lua lines appear as
`world_lua=event=...` with `;` expanded to `; ` (below written as in Lua).
Hashes are `%.0f` decimals. A tips or "Resume" screen can block a mission
start or follow the 30050 clear announce: `pwsh -File
tools/lab/Send-LabInput.ps1 -Role both -Keys space`. Commands and triggers are
in [lab-protocol.md](lab-protocol.md) "World sync".

- **W0-4 (world=0 inert, first):** `Play-OfflineCoop.ps1 -ExitAfterReadySec
  180`: no `world` lab.ini key, no line starting with `world`; the log equals a
  coop-lab 794d24f baseline after dropping every `entry_count=` and
  `return_count=` line, the two world CFG receipt lines and the `tick_ms`/`t_ms`
  values.
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
supported field missions; title-menu automatic following still needs live
validation.

All 24 seed keys are required, with every declared array index exactly
once; completion checks coverage as well as count and ordered digest.
The normal 16-entry revenge array produces 43 records. The host retains
its pending load if any required field is unreadable; it sends no partial
END. Duplicate, empty or self-consistent partial streams fail closed on
the guest. The schema lives in the separate bounded `world_follow_seed`
Lua chunk. Guest restore wrappers reapply the checked stream after later
mission-start/Continue restores, and the stage-block wrapper reasserts it
before vanilla placement reads, including helicopter preparation.
Already accepted pending preparation survives the inactive mission-loading
gap only while fresh CFG confirms level 3, the same role, an active link
and the unchanged nonzero local transport session. A changed session,
disconnect, explicit level zero or mixed negotiated level cannot apply
that stale preparation or reseed allocation. Idle save/stage hooks delegate
without extra CFG queries when no pending transaction exists.

Initial pairing still requires both copies in the same supported scene.
Joining with copied saves in different ACC/location scenes has no W3
bootstrap route yet; bring both copies to the same scene before following.

Travel controls survive native CFG mission resets, but are discarded on a
new link session. Lua transaction history resets even if the previous link
was already unpaired. A frozen outgoing stream retries refused TX items and
END without advancing its count. Guest queues are bounded and a transition
times out after 120 seconds. Host deployment waits for readiness; a lower
negotiated level releases the vanilla deployment without enabling W3.
Guest exits, hot-zone clear, abort and menu choices are suppressed while
W3 is armed; exit requests are logged and explicitly refused, leaving the
host to decide. Story destinations, midmission Reload and ReturnToMission
are refused before supported host paths change vanilla state.

**Reload alignment.** Every supported enemy mission at level 3 starts W1
inactive. The host scans all mapped soldiers and CPs in bounded batches,
including initial checkpoint state whose version was previously zero.
The frozen snapshot names every soldier/CP by index and hash, with explicit
unknown markers for unreadable areas. The guest validates mission, roster,
epoch, unique indices, counts and ordered digest before applying through
the existing forward-only life ledger and phase floors. ALIGN_END is in the
world queue so it cannot overtake its records. Both sides resume W1 only
after complete acceptance; failed TX and ACK timeout retry safely.

This does not revive a guest soldier that is already terminal when the host
reports NORMAL: the divergence is logged as `action=no_revive`. Unknown
host entities are discovered when their areas become readable. Unknown
does not mean VANISHED and does not establish fully identical enemy AI or
positions. Full story progress, arbitrary svars, hostages and vehicles are
not copied.

**Recovery.** Either player's PLAYER_DEAD/PLAYER_FALL_DEAD reserves shared
game over. The host retains menu authority; its Continue/restart travels
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
World op codes 0x1D-0x1F and 0x22 onward are available; control codes
0x09-0x0F are now used. HOLDUP, hostages/vehicles and dynamic quest roster
changes still require separate handling and live acceptance. Partner body
rendering and input drive are independent of these world modules.
