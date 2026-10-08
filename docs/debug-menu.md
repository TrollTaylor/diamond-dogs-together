# F9 debug menu

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
     world level and partner state.
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
(for example "Host only" or "Set a destination first").

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
| **Spawn enemy here** (H) | `spawn_enemy here` (`spawn_enemy here come` with "He comes at me") |
| Spawn at closest camp (H) | `spawn_enemy closest` |
| Go to spawned guard | `goto_spawn` |

Nearby table: radius 10..300 m, **List nearby** = `enemy_list r`. Click a row to
select a soldier (Name, distance, state, flags M marked / I ignoring / P placed). The
row of buttons below acts on the selection: `enemy <op> <hash>` with op kill (H, S),
sleep (H, S), faint (H, S, U), fulton (H, S, asks to confirm), come (H, S, U),
ignore / notice (H), disable / enable (H), mark / unmark (H, U), info.

Nearest (within 150 m): `enemy kill nearest`, `enemy sleep nearest`,
`enemy come nearest`, `enemy disable nearest`, `enemy mark nearest` (all H).

Area: **Clear nearby** `clear_enemies r` (H, S, asks to confirm, 1..100 m, at most 16),
**Unmark all** `enemy_unmark` (H), **Phase** `cp_phase nearest sneak|caution|alert`
(H, S; nearest command post).

**Camps & routes** (closed by default): Refresh camps `enemy_camps`, the camp list,
Load routes `enemy_routes <camp>`, Go to camp `goto_camp <camp>`, the route list and
Spawn on selected route `spawn_enemy <camp> <1..64>` (H).

The guest can list, read info and travel; everything that changes a soldier is host only.

### Partner / Co-op

Link, pose and animation readout (remote pose only once the partner has sent one).

| Button | Command |
|---|---|
| Go to partner | `goto_partner` (2 m from the partner, facing him; needs a paired link and a pose under 1 s old) |
| Show / Hide partner | `partner stage=3 source=net look=net`, `partner stage=0` |
| Default network movement / Warp movement | presets |
| Apply modes | `partner stage=3 source=net apply=.. input=.. anim=.. aset=.. dm=.. look=.. [reset=n|a|c|ac]` |
| Play clip / Stop clip | `partner dm=on dmplay=n`, `partner dmstop=1` |
| Slow motion (U) / off | `slowmo rate seconds` (0.05..1, 0.1..30 s), `slowmo off` |

The mode combos follow the running settings until you edit them ("Sync from game"
reverts). Slow motion is experimental: it slows this PC's whole world and the other
player sees your movement slowed.

### World (host only)

Clock, world level and handshake. Dawn / Noon / Dusk / Midnight (`time 21600`,
`time 43200`, `time 64800`, `time 0`), +1 hour (`time_add 3600`), HH:MM Set time
(`time s`), weather Sunny / Cloudy / Rainy / Sandstorm / Foggy / Auto
(`weather <name>`; Foggy is hidden in Africa). Time and weather follow to the guest
at world level 3 (results end in `sync=world`).

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
  `guard_here;ent=<id>;d=<m>;moved=<m>;far=0|1;mode=warp;come=0|1;sync=none;frames=<n>;leaf=pinned|engine`.
  `far=1` means more than 10 m away; `frames` is the number of updates until the
  engine handled the message.
- Fallback: `submitted_guard_here;ent=<id>;scope=local;mode=route;why=<reason>`, then
  `guard_here;ent=<id>;d=..;moved=..;far=0|1;mode=route;come=0|1;sync=none;route=<route>`
  (`far=1` above 60 m). `why` is `disabled` (key off), `native_unavailable` (no
  native answer), `realize_timeout`, or a native refusal: `contract`, `host`,
  `mission`, `command`, `pose`, `type`, `range` (target over 20 m from you), `input`,
  `manager`, `registry`, `chain`, `index`, `stance` (crouched/prone guards are not
  moved) or `fault`.

At most 8 guards per mission (native and route together). Placement is local to the
host: the guest's copy of that soldier does not move. Sleep, kill, Fulton and the
command-post phase do reach the guest when the world link is paired. The guard is
armed: turn on Auto-heal or keep Kill / Sleep ready. His patrol AI may walk him back
to his route afterwards.

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
from the census before relying on it. Every change of outcome (the menu retries each
update while the guard realizes) logs
`debug_menu=soldier_warp; id=; go=; r=; h=; leaf=; refused=`.

The hidden-worker scenario `tools/worker/scenarios/debug-menu.txt` turns the key on
with `labsetting debug_soldier_warp=1` and expects `mode=warp` with `d` of 10 m or
less.

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
- `sync=world` / `sync=none` says whether the result reaches the other player.

## Text grammar

Every button maps to one of these commands (also usable from the Tools console and
the worker's `debug` / `run` steps):

`position`, `bookmark n`, `return n`, `teleport x y z yaw`, `step dx dy dz`,
`forward m`, `goto_partner`, `heal`, `godmode 0|1`, `infinite_ammo 0|1`,
`fulton_infinite 0|1`, `slowmo rate seconds`, `slowmo off`, `inventory`,
`weapon hip|back|secondary current`, `drop_item current_hip|current_back|current_secondary n`,
`time s`, `time_add s`, `weather sunny|cloudy|rainy|sandstorm|foggy|auto`,
`partner k=v ...`, `enemy_camps`, `enemy_routes camp`, `goto_camp camp`, `goto_spawn`,
`spawn_enemy here [come]`, `spawn_enemy closest`, `spawn_enemy camp n`,
`enemy_list [r]`, `enemy <op> nearest|hash`, `enemy_unmark`,
`cp_phase camp|nearest sneak|caution|evasion|alert`, `enemy_nearest`,
`enemy_kill_nearest`, `enemy_enable_nearest`, `enemy_disable_nearest`,
`enemy_enable hash`, `enemy_disable hash`, `clear_enemies r`, `status`.

Worker builds only: `menu open [category] | close | toggle | category <name> | search [text] | press <action_id> | confirm | cancel | footer 0|1 | select <hash>`.

## Worker usage

In the hidden worker the menu is never opened with F9. Scenario steps:

- `<role> run <command>` waits for that command's final result (or the overlay
  result for `menu ...`) and prints it; `<role> expect <regex>` checks it.
- `<role> capture <label>` saves the next frame; with the menu open it contains the
  menu (the overlay draws before the capture in the same frame, without a mouse
  cursor or hover).
- `labsetting <key>=<value>` (scenario header, allow-listed: only
  `debug_soldier_warp=0|1`) adds a lab.ini key to both worker copies for that run.
- `tools/worker/scenarios/debug-menu.txt` opens every page, searches, presses,
  confirms and runs the spawn, partner and world commands. It enables the native
  placement and expects `spawn_enemy here` to report `mode=warp` with `d` of 10 m or
  less, then checks that the nearest soldier is still close a few seconds later.
  Mother Base has no authored camp soldiers, so spawn refusals are expected there.

## Not available

Mission travel, restart and checkpoint saves; freezing the clock; waking or holding
up soldiers; gamepad navigation; actions outside on-foot gameplay; creating new
soldiers or placing one at typed coordinates; replicating guard placement to the
guest; a free camera.

## Files

The menu writes only `mgs-coop/debug-overlay.log` (overlay lifecycle) next to the
game executable; action results are also logged in `mgs-coop/script-observer.log`.
Layout and settings are kept in memory only.
