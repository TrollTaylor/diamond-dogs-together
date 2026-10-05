# World sync W0-W2: protocol and implementation

Implements phases W0 (plumbing), W1 (shared enemy outcomes and alert
escalation in free roam) and W2 (clock and weather) of
[world-sync-design.md](world-sync-design.md), plus the lab probes and a
lab-only mission jump. Mission following (W3), the partner body outside
30050, game over and checkpoints (W4), story missions (W5) and soldier
transforms (W6) are not implemented (section 13 lists the extension points).

Nothing here has run in-game yet. Script citations refer to the extracted
1.0.15.4 scripts (`lib/...`, `mission/...`).

Code:

| Part | Files |
|---|---|
| Link DATAGRAM and hooks | `native/link_protocol.cpp`, `native/partner_net.cpp` |
| Core (pure, unit tested) | `native/world_wire.cpp` (op table, codec, queues, pairing, schedulers), `native/world_receipt.cpp` (receipts, trigger grammar) |
| Glue (Win32) | `native/world_link.cpp` (lock, hooks, pump, `world.trigger`), `native/game_script_observer.cpp` (receipt routing, install) |
| Lua (spliced into TppMain after `partner_sync.lua`) | `lua/coop/world_sync.lua`, `world_enemy.lua`, `world_outcome.lua`, `world_phase.lua`, `world_env.lua`, `world_probe.lua`, `world_goto.lua` |
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
| 3 | Level 2 plus W2: clock (30010, 30020, 30050) and weather (30010; 30020 without FOGGY). |

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
included) runs the level-1 plumbing only.

W1 is off for the rest of the mission (W2 continues) after a continue or
checkpoint reload (`TppMission.IsMissionStart()` false at the CFG start), a
game over (`svars.mis_isDefiniteGameOver` or `mvars.mis_isReserveGameOver`
seen in the tick), a roster mismatch (PAIRED `match=0`, or duplicate hashes),
or missing or invalid W1 bindings. Until W4, both players redeploy by hand
after a death.

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
| 0x08 | PROBE_SYNC | both (lab) | probeId 1..15, step 1..4, entHash, arg | atMs (step 1) | 14 |

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
Reserved for later phases: control 0x09-0x0F, world 0x18-0x1F, bundle flag
bit 0 (chunking).

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
| CFG | `return 'MGS_COOP_WORLD_CFG_V1' -- <mission> <phase>` (0 query, 1 start) | `return level,role,proto,link,epoch,probe,mode` (mode 1 lab, 2 play; the start consumes a scheduled probe) | query any; start runtime, not terminating |
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
clock_jump|clock_fast` (ids 1..15 in this order; lab only: play forces none,
sets `play_refused |= 0x8000` and counts one invalid value). `lab_ini_check
lab` prints `world=` and `world_probe=` after `link_keys=1`.

Launchers: `Play-OfflineCoop.ps1 -World 0..3 [-WorldProbe <probe>]` adds
`world=N` (and `world_probe=`) to both lab.ini files only when set; console key
`[G]` sends `goto=30010` to both copies. `Play-Coop.ps1 -World 0..3` adds
`world=N` after the play partner keys (never `world_probe`). `Prepare-
LabInstance.ps1 -Offline -PartnerSetting world=2` works too.

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

Seven chunks, each one `do ... end` block of at most 16000 bytes (CMake fails
the configure otherwise), spliced in this order directly after
`partner_sync.lua` and before the marker lifecycle chunk, so world's wrappers
sit inside the session lifecycle's and outside partner's: `world_sync.lua`
(receipts, `W.n`, required bindings, maps and digest, observer, control
dispatch, tick, error policy, stop), `world_enemy.lua` (lattice, apply,
commits, sweep, ledger), `world_outcome.lua` (attribution, guest requests and
held requests, host request validation, outcome wire handlers),
`world_phase.lua` (W1 bindings, area union, phase cache, escalation, floor,
PHASE_VEC), `world_env.lua` (digests, resync stream, stats, clock, weather),
`world_probe.lua` (probes), `world_goto.lua` (lab goto). Everything lives on
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

## 8. Lab goto (30050 -> 30010/30020, both copies, no menus)

Trigger `goto=30010|30020 [goto_lz=0|1]`, refused (`world_goto=refused;
reason=level|mission|target|busy|terminating`) unless world sync is on at
level >= 1, the copy stands in 30050 or in the ACC 40050 (memory and the last
CFG start), no goto runs and the mission is not terminating. Lua also refuses
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

Probe ids 1..15 are all used; W3 widens `probeId`.

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
- **Goto:** `Set-WorldTrigger.ps1 -Role both -Text 'goto=30010' -WaitSec 15`
  -> `world_goto=armed; target=30010; lz=0`, `world_goto=step; step=1;
  result=reserved` (Space if no `world=cfg; mission=40050` within 20 s), then
  `world=cfg; mission=40050`, `world_goto=step; step=2; result=waiting`, then
  `reserved`, `world=cfg; mission=30010`, `world_goto=arrived; mission=30010`.
  Both land at LZ field_N; a clock jump of about +6 h, W2 converges within 2
  digests.
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
- **Review fixes (lab):** the goto refusal: `Set-WorldTrigger.ps1 -Role both
  -Text 'goto=30010' -WaitSec 15` while in 30010 exits 3 with
  `world_goto=refused; reason=mission`. After the guest leaves 30010
  (`world=unpaired` on the host), the soldiers of the guest's former area
  deactivate as in vanilla (the union is not logged; the host's original
  UpdateOutOfAreaSetting reruns for its own area at once). A guest request outstanding at a link drop shows
  `event=req_hold;n=` and is sent again after the next `event=resync;...;
  src=end`. A host mission restart while the guest stays in 30010 gives the
  guest `event=inactive;reason=epoch;scope=w1` at the next `event=paired`.
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

## 13. Extension points (out of scope)

- Op ranges for W3-W6 (control 0x09-0x0F, world 0x18-0x1F), bundle flag bit 0
  (chunking); `probeId` widens to 1..63 in W3.
- `W.MISSIONS`, `W.handlers[op]`, `W.observers`, `W.bindHooks`,
  `W.startHooks`/`stopHooks`/`pairedHooks`: guest suppression wrappers (W3)
  plug into the TppMain-chunk section of `world_sync.lua`; the Trap filter
  (P14) into `W.observe`; proximity-owner attribution (W6) into
  `W.attClass`/`E.attributed`; HOLDUP, VANISHED, hostages and vehicles use
  `entType` 2/3 (refused today); W4 replaces the continue/game-over "W1 off"
  rule.
- `world_link::MissionRuntime` is the native hook for mission following.
- Untouched: the partner gates (30050 only), death and respawn, game over,
  checkpoints.
- Risks to watch live: P1 (`SetPhase` emits ChangePhase), P4 (out-of-area
  apply), P3 (FAINT = 4; the attacker of applied deaths), step 2 of the goto
  outside the preparation UI, the LZ route hash readback, the cpList shape,
  weather FORCE against the game's own forces, receipt cost during a resync
  stream.
