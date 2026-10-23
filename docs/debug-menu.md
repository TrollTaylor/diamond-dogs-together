# F9 debug menu


October 11 carry/sleep candidate: **Wounded and sleeping teammates** provides **Incapacitation status**, **Revive input status**, **Downed movement input**, **Native sleep controller**, **Carry eligibility**, **Carry target selection**, **Carried-player state** and **Pickup request state**. Injury stays downed until a teammate revives; sleep preserves health, blocks movement/combat and uses a 30-second paired-gameplay wake fallback. Both can be recovered by a healthy teammate holding ACTION within three metres for three seconds with F9 closed. The stock standing sleep pose, actual player pickup/carry/put-down, horse loading and shared passengers remain unfinished.

The carry eligibility query checks native geometry/health; its gun candidate is explicitly distinct from the shoulder-carry controller's target. The target-selection, carried-player, queued-request and sleep rows are read-only diagnostics: a ready receipt never starts a carry, attaches a body, unlocks input or proves a playing animation. Chain refusals identify the first typed component stage. **Pickup policy details** observes the native pickup provider without invoking it. **Sleep animation state details** reads the current native graph state; a candidate sleep hash does not prove animation playback. **Vehicle controller details** reads raw native player vehicle phase/target and entry-or-seat values, with no occupancy or attachment claim. **Current vehicle seats** reads two matching stock rider arrays on the actual current vehicle; on foot it reports `vehicle_not_riding`. It never creates a vehicle, guesses seat capacity or inserts a rider.

Horse summon uses the stock local loader four metres beside the player, with bounded context checks and cooldown. Selection/class availability do not prove arrival; close F9, wait, and repeat Summon / recall if missing. It does not create a second horse or attach a remote rider. Native mount census never substitutes a network riding bit for occupancy.
The debug menu exists only in offline copied-save co-op sessions. Every action is a
short text command that waits for the game's own Lua update; nothing calls the game
from the menu thread.

## Opening and closing

- **F9** opens and closes the menu. **Esc** also closes it (when the search box is
  not being edited) and the title-bar X does too.
- While the menu is open it captures the keyboard and mouse: the game does not see
  movement keys, mouse look or clicks, the menu draws its own mouse cursor, and the
  game cannot recentre or clip the cursor. After closing, input returns once F9, Esc
  and the mouse buttons are released and 120 ms have passed.
- Losing focus (Alt-Tab) closes the menu.
- The window can be moved and resized; its size is not saved between game launches.
  **Tools > Reset layout** puts it back.

## Layout

1. **Header** (always visible):
   - `HOST` or `GUEST`, mission (`30010 Afghanistan`, `30020 Africa`, `30050 Mother Base`),
     mission generation, then `READY` (green) or `WAITING: <reason>` (amber), link,
     world sync and partner state.
   - World sync: `| World L3 paired` (green), `| World L3: partner in <short name>`
     (amber: the partner is in another mission, for example `story mission 10020` or
     `Mother Base heli`; shown after 15 s in different missions), `| World L3 waiting
     (<reason>)` (grey; `no_link`, `no_hello`, `remote_off`, `remote_saves`,
     `same_role`, `local_loading`, `remote_loading`, `epoch`, `settling`, ...) or
     `| World off`.
   - Second line: `Pending: warp #12 (frame 34)` or `Idle`, the queue length, the active
     toggles (Auto-heal, Infinite ammo, Fulton, Slowmo) and the telemetry age. The age
     turns amber after 1 s: the game thread is paused, loading or in a menu.
2. **Quick bar**: Read position, **Spawn enemy here**, Kill nearest, Sleep nearest,
   Heal, Save/Go bookmark, Go to partner, Noon. Below it, **Recent** repeats your last
   six distinct commands.
3. **Left rail**: the search box and the five pages: Player, Enemies (with the nearby
   count), Partner / Co-op (with a link dot), World, Tools.
4. **Page**: the selected page, scrolling on its own. A search replaces the page with
   matching actions from every page.
5. **Footer**: the newest result as `[OK|REFUSED|PENDING|QUEUE] #id command -> result`.
   The `^` button expands the last six results.

Every button shows its result next to it (green ok, amber pending, red refused) for
20 seconds. Hovering a button shows what it does, its exact text command, whether it
is **Host only**, **Local only** or **Syncs to guest**, and "unverified in this build"
when the game API has no recorded live check. A greyed button says why when hovered
(for example "Host only" or "Set a destination first"). Two reasons name the scene:
`mission` ("F9 commands work only in Mother Base and Afghanistan/Africa free roam, not
in story missions or the helicopter.") while a story mission or the helicopter has no
supported gameplay, and `partner_elsewhere` on Go to partner ("Your partner is in
another mission. Meet in the same Mother Base or free roam first.").

Commands sent while another action is still running (a teleport waiting for its
position readback, a guard placement) wait in a 16-slot queue and run afterwards;
they are no longer refused as busy.

## Keyboard

| Key | Effect |
|---|---|
| F9 | Open or close |
| Esc | Clear the search while typing in it, otherwise close |
| Ctrl+F | Focus the search box |
| Ctrl+1 .. Ctrl+5 | Player, Enemies, Partner, World, Tools |
| Ctrl+R | Repeat the last command (confirm actions ask again) |
| Up / Down in the rail | Previous / next page |
| Tab, arrows, Enter / Space | Move between and press buttons |

Gamepads are not supported in the menu (the pad is muted while it is open).

## Pages and commands

H = host only, S = syncs to the guest, U = unverified in this build. Everything else
is local to the PC that sends it.

### Player

| Button | Command |
|---|---|
| Read position | `position` |
| Heal (U) | `heal` (sets life to its maximum; refused at 0 life) |
| Auto-heal (U) | `godmode 1` / `godmode 0` (refills life every update while alive; a lethal hit still kills) |
| Infinite ammo | `infinite_ammo 1` / `0` |
| Infinite Fulton (U) | `fulton_infinite 1` / `0` |
| Forward 5 m / Up 3 m | `forward 5`, `step 0 3 0` |
| Use current / From partner | fill the teleport form (no command) |
| Teleport | `teleport x y z yaw` (disabled until the form is filled) |
| Move by offset | `step dx dy dz` (each -100..100 m) |
| Save / Go to bookmark, slot grid | `bookmark n`, `return n` (0..7, this mission only) |
| Read inventory, Refill hip/back/secondary | `inventory`, `weapon hip current`, ... |
| Drop hip/back/secondary gun | `drop_item current_hip n` (payload 1..10) |

### Enemies

**Spawn near you** box at the top of the page:

| Button | Command |
|---|---|
| **Spawn enemy here** (H, S) | `spawn_enemy here` (`spawn_enemy here come` with "He comes at me") |
| Spawn at closest camp (H, S) | `spawn_enemy closest` |
| Go to spawned guard | `goto_spawn` |

Nearby table: radius 10..300 m, **List nearby** = `enemy_list r`. Click a row to
select a soldier (Name, distance, state, flags M marked / I ignoring / P placed). The
row of buttons below acts on the selection: `enemy <op> <hash>` with op kill (H, S),
sleep (H, S), faint (H, S, U), fulton (H, S, asks to confirm), come (H, S, U),
ignore / notice (H, S), disable / enable (H, S), mark / unmark (H, U), info.

Nearest (within 150 m): `enemy kill nearest`, `enemy sleep nearest`,
`enemy come nearest`, `enemy disable nearest` (H, S), `enemy mark nearest` (H).

Area: **Clear nearby** `clear_enemies r` (H, S, asks to confirm, 1..100 m, at most 16),
**Unmark all** `enemy_unmark` (H), **Phase** `cp_phase nearest sneak|caution|alert`
(H, S; nearest command post).

**Camps & routes** (closed by default): Refresh camps `enemy_camps`, the camp list,
Load routes `enemy_routes <camp>`, Go to camp `goto_camp <camp>`, the route list and
Spawn on selected route `spawn_enemy <camp> <1..64>` (H, S).

The guest can list, read info and travel; everything that changes a soldier is host only,
and the host's changes reach the guest's copy of that soldier (see **Spawn sync**).

### Partner / Co-op

Link, pose and animation readout (remote pose only once the partner has sent one).
While the partner is in another mission (not paired, pair-wait `different_mission`),
an amber sentence comes before the pose lines: "Your partner is in <their mission>; you
are in <yours>. Co-op only pairs when you are both in the same Mother Base, free roam or
helicopter." The `partner ...` commands (Show / Hide, the presets, Apply modes) answer
`partner_applied;partner=not_paired` while world sync is on but not paired (the setting
is kept, but no partner body shows until you pair), otherwise `partner_applied`.

| Button | Command |
|---|---|
| Go to partner | `goto_partner` (2 m from the partner, facing him; needs a paired link and a pose under 1 s old) |
| Show / Hide partner | `partner stage=3 source=net look=net`, `partner stage=0` |
| Default network movement / Warp movement | presets |
| Apply modes | `partner stage=3 source=net apply=.. input=.. anim=.. aset=.. dm=.. look=.. [reset=n|a|c|ac]` |
| Play clip / Stop clip | `partner dm=on dmplay=n`, `partner dmstop=1` |
| Slow motion (U) / off | `slowmo rate seconds` (0.05..1, 0.1..30 s), `slowmo off` |
| Name tag on / off, Name tag status | `nametag on`, `nametag off`, `nametag status` |

**Shared save** (automatic save handoff, [save-handoff.md](save-handoff.md)): shown
right after **Online** while save sharing is configured in this session (lab.ini
`save_handoff=stage|scene|always`, written by Play-Coop `-SaveHandoff`, in a world-sync
link v2 session with redirected saves). With save sharing off it is still shown to a
joiner whose travel lock may apply (world sync on), with the `off` line and the Travel
lock checkbox, and in the session Play-Coop restarts after a handoff (`applied`);
hidden otherwise. It reads copied state only
(`save_handoff_state`) and sends no command, so it works at the title screen and in
the helicopter as well. One status line:

| State (link-status `handoff`) | Line |
|---|---|
| `off` | Automatic save sharing is off (Play-Coop -SaveHandoff off). |
| `idle`, `same` | Your save matches the host's place; nothing to share. (host: Your save matches your partner's place; nothing to share.) |
| `wait_host` | Waiting for the host to reach Mother Base or free roam; their save is then copied to you. |
| `receiving` | Receiving the host's save: 42% of 594 KB. |
| `staged` | Host's save received and checked (594 KB). |
| `countdown` | Host's save ready: MGSV restarts in 7 s. Your current co-op save is kept aside (Share-Save.bat [R]). |
| `exiting` | MGSV is closing to load the host's save; Play-Coop starts it again. |
| `kept` | You kept your own save for this session. (host: Your partner kept their own save.) |
| `refused` | The host's save cannot be shared: `<reason>`. (host: Your save cannot be shared: `<reason>`.) |
| `failed` | Getting the host's save failed: `<reason>`. Nothing was changed. (host: Sending your save failed: `<reason>`.) |
| `applied` | You now play on the host's save. Share-Save.bat [R] brings your previous co-op save back. |
| host `offered`, `sending`, `partner_staged`, `partner_restarting` | Offering your save to your partner. / Sending your save: 42% / Your partner received your save. / Your partner is restarting on your save. |

The countdown seconds come from the newest "MGSV restarts in N s" toast (re-posted
every second), else 10 s from when the menu first saw the countdown. The reasons
are short phrases such as "it is still in the story opening", "it continues into a
mission co-op cannot join", "the connection dropped" or "not enough disk space".

For the joiner (the host has neither row):

- **Keep my own save** (enabled while the host's save is awaited, on its way, ready
  or counting down) asks the save handoff to stop for this session: MGSV does not
  restart and your co-op save stays. It records `last_ui_action=handoff_keep`. Pressing
  Continue twice at the title does the same.
- **Travel lock (you follow the host's travel)**, checked by default: unchecking it
  turns the joiner's travel lock off for the rest of this game session
  (`last_ui_action=travel_unlock`; checking it again records `travel_lock`). Play-Coop
  `-TravelLock off` turns the lock off before starting.

The worker cannot press these two rows (they are not registered actions); its
`state.json` reports `handoff`, `handoff_pct` and `travel_unlocked` instead.

**Name tag** ([partner-nametag.md](partner-nametag.md)): a
readout line (on/off, shown or the hidden reason, the partner's
name, distance, "on target") and the rows above. The `nametag ...` commands are
handled by the loader itself, never by Lua or the game, and are local to this PC
and kept in memory only. Their result is the status line
`nametag;on=..;xh=..;st=visible|hidden;why=..;name=..;src=remote|fallback;d=..;stance=..;px=..;hov=..;aim=..;stale_frames=..`.

The mode combos follow the running settings until you edit them ("Sync from game"
reverts). Slow motion is experimental: it slows this PC's whole world and the other
player sees your movement slowed.

### World (host only)

Clock, world level and handshake (`handshake paired` or `handshake waiting
(<reason>)`, the pair-wait names of the header), then, once the partner's mission is
known, `Partner mission <n> (<name>)   partner world level <n>`. Dawn / Noon / Dusk /
Midnight (`time 21600`, `time 43200`, `time 64800`, `time 0`), +1 hour
(`time_add 3600`), HH:MM Set time (`time s`), weather Sunny / Cloudy / Rainy /
Sandstorm / Foggy / Auto (`weather <name>`; Foggy is hidden in Africa). Time and weather
follow to the guest at world level 3. The time readback and weather results end in
`sync=world` only when the clock is synced (paired in a Mother Base or free-roam
scene), otherwise `sync=none`.

### Tools

Session status (`status`), a console that sends any text command (Up/Down recalls the
last 16; invalid characters are refused before sending), a clickable command
reference, the 16-entry receipt log with an All / OK / Refused / Pending filter and
**Clear log**, raw telemetry and **Reset layout**.

## Spawning near you: what it really does

The game cannot create new soldiers at runtime, so **Spawn enemy here** reuses an
existing one:

1. It takes the nearest unused guard that is alive (NORMAL), enabled, on foot (no
   vehicle) and 15 to 500 m away, among the guards of the command posts with patrol
   routes.
2. Without "He comes at me" he gets `TppEnemy.SetEnableRestrictNotice` (he ignores
   you; the Nearby table shows I). With it he gets `SetDiscoveryPlayer` once he has
   landed (he attacks).
3. **Native placement** (lab.ini `debug_soldier_warp=1`, lab only; play forces it
   off): the menu sends the engine's own set-transform message `0x287158b07327` to
   his GameObjectId through the GameObject manager's SendCommand, the same call the
   player carry code makes. The target is 5 m along your heading at your height, and
   he faces you. A guard that is not realized yet answers `r=-1`: he is
   force-realized and the message is resent every update for up to 150 updates, and
   force-realize is released after the readback.
4. **Route snap fallback** when the key is off, the native side refuses, the loader is
   older or he never lands: he is disabled, put on the best-scored patrol route of the
   four nearest command posts at point 0, re-enabled and force-realized (the retail
   TppDebug sequence). Expect him 5-60 m away.

Results:

- Native: `submitted_guard_here;ent=<id>;scope=local;mode=warp`, then
  `guard_here;ent=<id>;d=<m>;moved=<m>;far=0|1;mode=warp;come=0|1;<sync>;frames=<n>;leaf=pinned|engine`.
  `far=1` means more than 10 m away; `frames` is the number of updates until the
  engine handled the message.
- Fallback: `submitted_guard_here;ent=<id>;scope=local;mode=route;why=<reason>`, then
  `guard_here;ent=<id>;d=..;moved=..;far=0|1;mode=route;why=<reason>;come=0|1;<sync>;route=<route>`
  (`far=1` above 60 m). `why` is `disabled` (key off), `native_unavailable` (no
  native answer), `realize_timeout`, or a native refusal: `contract`, `host`, `pose`,
  `type`, `range` (target over 20 m from you), `input`, `manager`, `registry`, `chain`,
  `leaf` (the hop is a known forwarder that does not reach the verified handler),
  `index`, `stance` (stance 1-4 guards are not moved), `budget`, `fault`, `anomaly`
  or `latched` (see below).
- `command` or `mission` from the native side (the spawn command is no longer the
  current one, or the mission is ending) end the command with
  `spawn_context;why=command|mission` and no route snap: no guard commands are sent
  during travel or teardown. A pending native placement that the menu drops
  (`action_context_changed`) releases its force-realize only while the same mission is
  still in gameplay.

`<sync>` is `sync=world;seq=<n>` when the placement was announced to the guest
(see **Spawn sync**) or `sync=none;why=<reason>` (`not_paired`, `roster`,
`world_off` without a world link, `tx_<n>` a refused send).

At most 8 guards per mission (native and route together). The guard is armed: turn
on Auto-heal or keep Kill / Sleep ready. His patrol AI may walk him back to his route
afterwards.

### Native placement: what is verified

`tools/generate_soldier_warp_contract.py` checks the exact executable
(`085c2f82...bb45`) instruction by instruction and writes
`native/soldier_warp_contract.hpp`: the manager getter and its static object, the
SendCommand / GetHandle / registry / dispatch bytes, the soldier handler `0x1360f90`
(its hash branch, realized test, stance branches and `T->SetPose`/`SetRot` tail),
the vtable slots and the carry-code precedent. With the key on, the loader compares
every site at startup (`soldier_warp=ready` or `soldier_warp=refused; reason=...;
rva=...`) and nothing is called before a host `spawn_enemy here`.

One hop cannot be pinned statically: the per-type handle object and its vtable +0x10
are built by protected code. Each attempt resolves the chain read-only; outcome changes log
`soldier_warp_census; ...` lines, at most 24 per process, with the vtable and hop
RVAs and the hop's first bytes.
`leaf=pinned` means the hop is an argument-preserving forwarder to a soldier
subobject whose vtable +0xa8 is the verified handler; the loader then mirrors the
handler's own checks first (index, realized bit, stance 1-4) without sending.
`leaf=engine` means the engine's own dispatch was used without that proof; pin it
from the census before relying on it. A recognized forwarder that lands anywhere else
refuses (`leaf`) without sending. Every change of outcome (the menu retries each
update while the guard realizes) logs
`debug_menu=soldier_warp; id=; go=; r=; h=; leaf=; refused=`.

Native bounds per command: only the most recently popped `spawn_enemy here` command
authorizes a send, it moves one GameObjectId, at most 160 sends (the menu needs 151 at
most: one, then one per update for 150 updates) and nothing after the engine handled
it (`target`, `budget`, `landed`). The handler and SendCommand only answer `r=0;h=1`
(moved) or `r=-1;h=0` (not yet). Any other answer (`anomaly`) or a caught exception
(`fault`) logs `soldier_warp=disabled; reason=fault|anomaly; id=..; go=..; r=..; h=..;
until=process_exit` and every later attempt in that process refuses with `latched`,
so an unexpected unpinned dispatch is used at most once.

The hidden-worker scenario `tools/worker/scenarios/debug-menu.txt` turns the key on
with `labsetting debug_soldier_warp=1` and expects `mode=warp` with `d` of 10 m or
less.

## Spawn sync (the guest's copy)

Each PC runs its own copy of every authored soldier, so a placement or a debug action
on the host has to be repeated on the guest's copy of the same soldier. The host
announces it on the world link (`ENEMY_DEBUG`, [world-sync-protocol.md](world-sync-protocol.md));
the guest applies it and answers (`ENEMY_DEBUG_ACK`). Code:
`lua/coop/world_place.lua` (host, reports) and `lua/coop/world_place_guest.lua` (guest).

It needs the world link paired at an effective level of 2 or more and matching
rosters (`world_status` shows `roster=match`). It does not need outcome sync (W1),
so a placement works while the start-of-mission enemy snapshot is still pending.
Soldiers are named by `ent=`, the unsigned StrCode32 of the soldier name, which is
the same on both PCs (the guest's own GameObjectId is used locally; the host's is
only compared, `goid_match`).

| Host action | Kind | The guest's copy |
|---|---|---|
| Spawn enemy here (native) | 1 warp | The same engine set-transform message to the host's exact target and yaw (gates below). Restrict-notice unless "He comes at me"; with it, `SetDiscoveryPlayer` once landed. |
| Route snap, closest camp, camp route | 2 route | The retail route sequence on the same route (the guest resolves the hash from its own route sets). |
| Settle window (automatic) | 3 correct | Re-warp only after two samples more than 6 m off. |
| `enemy come` | 4 | `SetDiscoveryPlayer`: he hunts the guest's own player. |
| `enemy ignore` / `notice` | 5 / 6 | `SetEnable/DisableRestrictNotice` (the I flag). |
| `enemy disable` / `enable` | 7 / 8 | `TppEnemy.SetDisable` / `SetEnable`. |

Kill, sleep, faint, Fulton, Clear nearby and the command-post phase are not spawn
sync: they use the world outcome ledger and report `sync=world` once outcome sync is
on. A placed guard is kept in an active area on both PCs (`SetOutOfArea` false, again
after every area change), so a later kill is never held as out of area. Mark stays
local.

**Guest native placement.** The guest sends `return 'MGS_COOP_WORLD_WARP_V1' -- <seq> <goId>`
every update until the guard lands (force-realize once on `r=-1`, at most 150 updates
or 10 s, force-realize always released). The native handler
(`game_script_observer.cpp` `GuestWarpReceipt`) uses the already verified `Warp()` and
adds no game address. It refuses unless all of these hold: guest role in lab mode,
lab.ini `debug_soldier_warp=1`, `soldier_warp=ready` at startup, the mission gates,
the grant `world::Core` recorded when the guest's Lua popped that `ENEMY_DEBUG`
(kind warp or correct, same epoch, at most 15 s old, 16 slots; cleared at unpair, link
session, host epoch and local mission changes), and a live remote pose of the host at
most 1 s old. The target always comes from the grant. The range is checked against the
guest's view of the host: 20 m for kind 1, 60 m for corrections. One GameObjectId per
grant, at most 160 sends, nothing after landing, and the process-wide `fault` /
`anomaly` latch of the host path. The per-grant gate id is the grant's process-unique
serial (bit 31 set), so a restarted host that reuses an epoch and seq is a new grant,
never a spent one. `tools/generate_soldier_warp_contract.py --check`
asserts that `Warp()` has exactly these two callers and their gates. Play mode
forces the key off, so there only route placements (kind 2) move the guest's copy.

**Settle window.** For 60 s after a native landing the host samples its guard once a
second while he is NORMAL and sends a correction when he moved 1 m or every 5 s. The
guest logs every sample (`event=place_div`) and re-warps only when he is more than
6 m off on two samples in a row, NORMAL, below ALERT and not a "come" guard; at most
one correction per 5 s and 10 per guard. A guard whose placement is still pending
on the guest is not sampled, and a queued correction is refreshed in place by a newer
one (newest target and grant). "Come" guards chase each PC's own player,
so they are never corrected. A re-pair in the same epoch re-sends the tracked
guards at once.

**Lifecycle.** Unpairing drops the guest's queue. A world-link stop (mission end,
level 0) also forgets the pins and tracked guards. A pending force-realize is released
only while the mission is still in gameplay (as the menu's own cancel), never during
teardown. Outcome sync (W1) turning off changes nothing here.

**Results.**

- Host spawn and action results end in `sync=world;seq=<n>` or `sync=none;why=<reason>`.
- When the guest answers, the host's receipt log gets a row `remote spawn_enemy ->
  guest_spawn;seq=<n>;ent=<id>;kind=<kind>;result=applied|refused;why=<reason>;err_m=<m>;frames=<n>`
  (corrections only when refused).
- The guest's receipt log gets `remote spawn_enemy -> remote_spawn;seq=<n>;ent=<id>;state=landed;mode=<kind>;come=0|1;err_m=<m>`
  or `...;state=refused;...;why=<reason>;native=<native refusal>`.
- `world_status` (both roles): `world;paired=;eff=;w1=;roster=match|mismatch|unknown;level=;area=<n>|inferred<n>|none;sent=;acked=;corrected=;refused=;applied=;failed=`.
- `spawn_sync` on the guest: the last row plus `partner_m` (guard to the host's body on
  this screen), `player_m` (guard to this player) and `goid_match`; on the host: the
  last guest answer.

| Guest `why` | Cause |
|---|---|
| `not_ready`, `roster` | not paired or not in gameplay, roster mismatch |
| `unknown_entity`, `unmapped` | hash not in this roster, GameObjectId NULL |
| `terminal_local`, `unreadable_local`, `vehicle_local`, `disabled_local` | this copy is dead/Fultoned (never revived), unreadable, riding or disabled (the last two for native placements only: a route placement re-enables him, as on the host) |
| `native_off` | key off, play mode, contract refused, latched, or an older loader |
| `grant`, `pose`, `range`, `stance` | native gates above |
| `realize_timeout`, `position_timeout` | no landing within 150 updates / 10 s, no readback |
| `route_unknown`, `context`, `busy` | route not in the guest's route sets, entry older than 15 s, FIFO of 8 full |
| `native_other` | type, chain, leaf, index, fault, anomaly or a failed call (exact text in `native=`) |

**Outcome sync before an area trap.** At level 3 outcome sync waits for the host's
start-of-mission enemy snapshot, which counts only soldiers of the active free-roam
area. When the host has not crossed a `trap_FreePlay_areaNN` yet, the host now takes
the area that holds the command post of the nearest soldier with a readable
position (`event=area;inferred=<n>`) and reads the snapshot for that area. This is
bookkeeping only: no game call, no `SetOutOfArea`, and the area used to apply
outcomes is unchanged; a real trap replaces it. Without any such soldier nothing is
inferred and the snapshot stays refused as before.

**Logs.** Host: `world_lua=event=place_tx; ...`, `event=place_ack; ...`,
`event=area; inferred=..`. Guest: `event=place; seq=..; kind=..; result=..; why=..;
native=..; err_m=..; frames=..`, `event=place_div; ...` and
`world_place=warp; seq=..; kind=..; ent=..; go=..; age_ms=..; r=..; h=..; leaf=..; refused=..`
(on every change). Both: `debug_menu=remote; message=..`.

## Results

- Interim results (amber, `PENDING`) mean the command was accepted and a readback
  follows, for example `submitted_warp` then `warp_position_readback;error_m=0.012`.
- Final results are `OK` (green) or `REFUSED` (red). `QUEUE` means the queue refused
  it before the game saw it (not in gameplay, queue full, invalid text).
- Common refusals: `host_only`, `gameplay`, `demo`, `on_foot`, `helicopter`,
  `position_args`, `empty_bookmark`, `spawn_no_route`, `spawn_no_guard` (no eligible
  guard: dead, disabled, riding, closer than 15 m or farther than 500 m),
  `spawn_pool_limit_8`, `no_readable_soldier`, `enemy_not_authored`,
  `partner_pose_unavailable`, `life_vars_unreadable`, `missing_<api>` (the game
  function is absent in this build).
- `sync=world` / `sync=none` says whether the result reaches the other player:
  kill, sleep, faint, Fulton, Clear nearby and the phase use the world outcome
  ledger (`sync=world` once outcome sync is on); placements and come / ignore /
  notice / disable / enable add `;seq=<n>` (spawn sync) or `;why=<reason>`.
  Time and weather end in `sync=world` (`time_readback;sync=world`) only when the
  clock is synced (paired in a Mother Base or free-roam scene), else `sync=none`.

## Text grammar

Every button maps to one of these commands (also usable from the Tools console and
the worker's `debug` / `run` steps):

`position`, `bookmark n`, `return n`, `teleport x y z yaw`, `step dx dy dz`,
`forward m`, `goto_partner`, `heal`, `godmode 0|1`, `infinite_ammo 0|1`,
`fulton_infinite 0|1`, `slowmo rate seconds`, `slowmo off`, `inventory`,
`weapon hip|back|secondary current`, `drop_item current_hip|current_back|current_secondary n`,
`time s`, `time_add s`, `weather sunny|cloudy|rainy|sandstorm|foggy|auto`,
`nametag on|off|status`, `nametag crosshair aim|always|off`,
`partner k=v ...`, `enemy_camps`, `enemy_routes camp`, `goto_camp camp`, `goto_spawn`,
`spawn_enemy here [come]`, `spawn_enemy closest`, `spawn_enemy camp n`,
`enemy_list [r]`, `enemy <op> nearest|hash`, `enemy_unmark`,
`cp_phase camp|nearest sneak|caution|evasion|alert`, `enemy_nearest`,
`enemy_kill_nearest`, `enemy_enable_nearest`, `enemy_disable_nearest`,
`enemy_enable hash`, `enemy_disable hash`, `clear_enemies r`, `status`,
`world_status`, `spawn_sync` (both read-only, both roles).

Worker builds only: `menu open [category] | close | toggle | category <name> | search [text] | press <action_id> | confirm | cancel | footer 0|1 | select <hash>`.

## Worker usage

In the hidden worker the menu is never opened with F9. Scenario steps:

- `<role> run <command>` waits for that command's final result (or the overlay
  result for `menu ...`) and prints it; `<role> expect <regex>` checks it.
- `<role> capture <label>` saves the next frame; with the menu open it contains the
  menu (the overlay draws before the capture in the same frame, without a mouse
  cursor or hover).
- `labsetting [host|guest] <key>=<value>` (scenario header, allow-listed:
  `debug_soldier_warp=0|1`, `nametag=0|1`, `nametag_crosshair=aim|always|off`,
  `nametag_name=[A-Za-z0-9_]{1,16}`) adds a lab.ini key to both worker copies, or to
  one, for that run.
- `tools/worker/scenarios/debug-menu.txt` opens every page, searches, presses,
  confirms and runs the spawn, partner and world commands. It enables the native
  placement and expects `spawn_enemy here` to report `mode=warp` with `d` of 10 m or
  less and `sync=world;seq=<n>`, then checks that the nearest soldier is still close a
  few seconds later and that the guest's copy landed (`guest run spawn_sync`).
  Mother Base has no authored camp soldiers, so the scenario needs 30010 or 30020.
- `tools/worker/scenarios/partner-nametag.txt` names the copies `HostTag` and
  `GuestTag` and proves the name tag and the friendly crosshair on both screens
  ([partner-nametag.md](partner-nametag.md)).
- `tools/worker/scenarios/spawn-sync.txt` is the focused guest-side proof: outcome
  sync on (`w1=1` on both), the guest 3 m behind the host, the guard landed within
  10 m of the host's body on the guest's screen, then sleep, come and Clear nearby
  readbacks on the guest's copy.
- 0.13.6 vehicle levers (typed commands only, no menu row; lua/coop/debug_vehicle.lua ->
  lua/coop/world_vehicle.lua, [world-sync-protocol.md](world-sync-protocol.md) section 16):
  `vehicle_remove nearest [sync]` removes this copy's nearest free vehicle (within
  60 m; never the one the player rides or an occupied one) with the stock TppVehicle2
  Despawn: without `sync` on this copy only (the divergence of the 2026-10-22 session),
  with `sync` (lab.ini `world_vehicle_gone=1`) as a lab loss through VEHICLE_GONE.
  Result `vehicle_remove;id=;via=locator|index;sync=;alive=;dist=`. Lab only: native
  refuses it outside mode=lab with offline redirected saves
  (`vehicle_remove=refused;reason=lab_only`), Lua outside mode=lab.
  `vehicle_gone_status` (read-only) answers `vgone;on=;role=;gone=<ids, L = this copy
  only>;held=;watch=;req=`. Scenarios `coop-vehicle-gone.txt` (lab seed) and
  `coop-vehicle-gone-mb.txt` (real seed).

## Not available

Mission travel, restart and checkpoint saves; freezing the clock; waking or holding
up soldiers; gamepad navigation; actions outside on-foot gameplay (only the UI-only
rows, such as Online and Shared save, work at the title and in the helicopter); creating new
soldiers or placing one at typed coordinates; replicating each guard's AI movement
frame by frame (each PC runs its own copy; see the settle window below); marker
sync; a free camera.

## Files

The menu writes only `mgs-coop/debug-overlay.log` (overlay lifecycle) next to the
game executable; action results are also logged in `mgs-coop/script-observer.log`.
Layout and settings are kept in memory only.
