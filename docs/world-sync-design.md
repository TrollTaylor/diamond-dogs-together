# World sync design (rev 2)

Research behind this design: [scripts](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/analysis-2026-10-05/world-sync-scripts-research.md), [native](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/analysis-2026-10-05/world-sync-native-research.md), [mission flow](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/analysis-2026-10-05/world-sync-flow-research.md), [critique of rev 1](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/analysis-2026-10-05/world-sync-design-critique.md). Script citations refer to scripts extracted locally from the installed game (not committed).


Status: W0-W2 implemented and exercised live in [October 5 testing](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-10-05-world-sync.md). W3 shared supported-scene travel and W4 forward-only reload alignment/recovery are implemented behind level 3; their automated fixtures are separate from live acceptance. W5-W6 remain design only. Levels 0..3 (lab.ini `world`, default 0 = inert). See [current protocol](world-sync-protocol.md), section 13, for the implemented subset and limitations. Target: exe 1.0.15.4.
Addresses are RVAs from a static read. Script citations use the extracted 1.0.15.4 scripts (`lib/…`, `mission/…`).
It assumes the parallel two-PC link, with a reliable ordered channel (R), an unreliable channel (U), and host and guest roles.

### 1. Goal and summary

The goal is one shared world: the same mission and location, the same enemies (alive, neutralised, dead, fultoned) in roughly the same places, the same alert, the same consequences whoever caused them, the same clock and weather, and the same mission progress.

1. **Each PC runs its own full single-player world.** There is no synchronizer and no session. Enemies exist only because both PCs run the same mission script.
2. **Discrete facts are host-committed with a single writer.** These are life outcomes, alert escalations, clock, weather, sequence and svars, checkpoints, game over and clear. Either PC may detect a fact; only the host commits it.
3. **Perception and hit resolution happen in the world of the player involved.** Offline, each world's soldiers perceive and shoot only that world's local player. The guest's world resolves the guest's fights, and the guest reports results it caused.
4. **Movement and AI run locally on both PCs.** Positions are exchanged for telemetry and markers only, in the last phase. Coarse correction is done with script commands. (Since 7.7 / 7.8 the guest's copies of the guards near either player are pulled to the host's positions by the engine's set-transform message.)
5. **Mission flow is a host-driven replay.** It is detected at the `TppMain.ReservePlayerLoadingPosition` choke point and replayed on the guest through the same high-level APIs.

### 2. Hard constraints

- Our own UDP link only. None of the following: the game's FOB/online synchronizer, `S_IS_ONLINE`, sessions, the game's Steam P2P, the master server, login or version checks, fuse.
- No worker-thread or AI-job hooks, and no per-thread redirection. Game calls are Lua script APIs on the Lua thread, inside the TppMain-chunk wrappers. Native code does small verified reads and patches on the main thread only, and calls no game functions for world sync.
- Receipts are `loadstring("return 'MGS_COOP_*_V1' -- args")`, with the source under 256 bytes (`native/game_script_observer.cpp:1251-1257`). Replies are short numeric lists.
- The guest's progress writes go only to a redirected co-op save (`native/save_redirect.cpp`). Co-op refuses to start on the guest without that redirect.

### 3. Lua–native bridge

| Receipt | Shape |
|---|---|
| `MGS_COOP_WORLD_CFG_V1` | Reply: `enabled, role (1 host, 2 guest), proto, linkUp, phaseLevel (lab.ini world=0..6)`. Inert (a literal string) when `world=0`. |
| `MGS_COOP_WORLD_TX_V1 -- <op> <n1> …` | Integers only. Hashes are formatted with `%.0f` (Lua 5.1 `%d` is 32-bit on Windows). Native validates ranges, frames the message and picks R or U by op. |
| `MGS_COOP_WORLD_RX_V1 -- <queue>` | `queue` is 0 for control or 1 for world. Pops one message. Reply: `op, n1..n15` (`op=0` means empty). At most 16 pops per frame. |
| `MGS_COOP_WORLD_LOG_V1 -- text` | Diagnostics, with the same sanitiser as the partner log. |

- **Native side.**
  - Native owns the link and two inbound queues.
    - The control queue holds `HELLO`, `MISSION_TRANSITION`, `MISSION_SEEDS`, `MISSION_READY`, `PROGRESS_SYNC`, `CHECKPOINT_SNAPSHOT`, `GAME_OVER` and `REJECT`.
    - The world queue holds everything else.
  - Each queue is bounded (4096 entries). On overflow, native sets a resync flag and Lua asks for a resync.
  - The receive thread is our own thread, not a hook, and touches only the queues (under an SRW lock, as `partner_net.cpp` does). The keepalive runs in native code, so a paused Lua thread never drops the link.
  - Large payloads are chunked and reassembled in native code.
- **Lua side.**
  - Lua drains the world queue only while `ready` is set, meaning after its own `MISSION_READY` for the current epoch. Until then, world messages wait in native.
  - All protocol timers use `Time.GetRawElapsedTimeSinceStartUp()`, never frame counts. The unfocused guest runs at 16–60 FPS. The game holds that clock still under the MISSION FAILED menu, so the wall clock carries it there (`W.now()`, docs/world-sync-protocol.md section 7, Clock).
- **Names.**
  - Names go on the wire as `Fox.StrCode32` hashes.
  - Each PC builds `hash ↔ name ↔ gameObjectId` maps at `OnMissionGameStart` from `mvars.ene_soldierDefine` and `mvars.ene_soldierIDList` (`lib/TppEnemy.lua:1422-1447`), plus hostages, vehicles and CPs. The maps are rebuilt on a quest or reinforcement spawn.
  - GameObject ids never go on the wire.

### 4. Authority model

#### 4.1 Why not pure host authority

- The host does not simulate soldiers near a distant guest. Free-roam area traps set other areas out of area (`mission/f30010/f30010_enemy.lua:1473-1486`), and streaming and realization centre on the local player.
- Host soldiers cannot perceive the guest without hooks in AI or CP code. Those hooks are forbidden.
- Soldiers have no warp or set-position command (TppSoldier2 dispatcher 0x1356170–0x1360701).
- Dynamite's experience: when each PC's AI decides outcomes, the worlds diverge, so shared outcomes need one writer (Dynamite `docs/MAKING_OF.md`).

#### 4.2 Domains

| Domain | Commit | Detected by | Guest |
|---|---|---|---|
| Mission, location, sequence, svars, objectives, checkpoints | Host | Host scripts | Replays host events; its own triggers are suppressed (7.3) |
| Enemy life outcome | Host | The host's world; the guest only for events it caused or soldiers it owns (rule 4.3) | Optimistic local result; host result is final |
| CP alert | Host commits **escalations only** | Either PC | Sends escalations; never forced down |
| Clock, weather | Host | Host | Corrected to the host's values; random weather paused |
| Perception | Each PC, for its own player | Own world | Nothing extra |
| Movement and AI | Local on both PCs | — | Telemetry, plus coarse correction in W6 |
| Partner body | Existing partner sync | — | — |
| Inventory, health, buddies, support | Each player | Own PC | Local only |
| Player death | Host (shared game over, D3) | Either PC | Sends `REQ_GAME_OVER` |

#### 4.3 Attribution rule (guest → host)

The guest sends `REQ_OUTCOME` only when at least one of these holds:
- the message's attacker or fultoner is the guest's local player (`Tpp.IsLocalPlayer`);
- the attacker is the guest's buddy, or a vehicle, explosive or support strike the guest owns;
- the guest is the proximity owner (section 8) and the event happened in the guest's world (falls, animals).

Any other death in the guest's world is logged as divergence and not sent.

#### 4.4 Life-state lattice

Wire enum: `NORMAL 0 < HOLDUP 1 < SLEEP 2 = FAINT 3 < DYING 4 < DEAD 5 < FULTONED 6 | VANISHED 7`.

It is mapped to the engine through the `TppGameObject.NPC_LIFE_STATE_*` constants at runtime, never through raw numbers.

- DEAD, FULTONED and VANISHED are terminal. The first commit wins; later requests get `REJECT` with the terminal state.
- A forward move is accepted whenever the current state is not terminal.
- A backward move (wake-up, hold-up release) is accepted only from the host's own world, or from the guest when the guest is the proximity owner and its `seenVer` equals the host's current `ver`.
- Each accepted move increments `ver` (u16 per entity per epoch).
- **Apply rule (both PCs).** Before applying, read the local `GetLifeStatus`, `GetStatus` and `TppEnemy.IsRecovered`. If the local state is already at or past the target, record the new `ver` and do nothing. This stops a second `Dead` or `OnFulton`, a second staff add and second result counts.
- **When terminal states disagree** (for example, the guest fultoned while the host had already killed): neither side undoes its local result. The host's state is what mission logic sees, and the visible difference is cosmetic.

### 5. Perception

- Offline, every soldier and CP decision reads offense index 0, which is the local player (static finding).
- The guest's world fights the guest, and the host's world fights the host.
- **Guest discovery** reaches the host as `REQ_PHASE` (escalation only). The host applies it with `SendCommand(cpId, {id="SetPhase", phase=N})`, with handlers 0xD60A81 and 0xD5FB81 reading `"phase"`. Host soldiers then search for the host's player, which is acceptable.
- **Optional (P5):** `RequestNotice{type, targetId, sourceId}` (0xD61105) sends the host CP to investigate the guest's last position.
- **Not used:** `SetDiscoveryPlayer`. The 0x1E02770 detour is not available under the constraints, so it is not offered as a decision.
- Applying a phase from the host on the guest runs the guest's discovery game-over check (`lib/TppMission.lua:1154-1155`). That check calls `ReserveGameOver`, which the guest drops. On the host, a guest-caused ALERT runs the host's vanilla discovery game over, which is the correct shared outcome.

### 6. Messages

#### 6.1 Rules

- **Envelope:** `{op u8, epoch u16, wseq u32 (R only), …}`, carried inside the link header.
- **Epoch.**
  - The host increments the epoch on every committed transition (clear, abort, restart, continue, reload, deploy).
  - Control ops carry the epoch they establish.
  - World ops from another epoch are dropped on receipt, except those already queued in native from the current epoch while not yet `ready` (3).
- **Requests.**
  - Guest requests carry a `rid`.
  - The host sends `RECEIVED{rid}` at once, then exactly one answer: the authoritative event carrying `rid`, or `REJECT`.
  - The guest's timeout runs from `RECEIVED`: 3 s if `RECEIVED` has not arrived, otherwise none, because a paused host answers late but still answers.
  - The host holds requests while its mission state is not in-game (`TppMission.CheckMissionState` false, during demos, game over or loading) and answers when it can.
- **Echo suppression.** Before applying an event, record `(entity, kind, state, wall deadline +3 s)`. Matching local messages are not forwarded. The host broadcasts its own applied result once, carrying the guest's `rid`.
- **The observer** wraps `TppMain.OnMessage` (`lib/TppMain.lua:411-438`, cached by `mission_main.lua` at load, so the wrap goes in the TppMain chunk).
  - It calls the original first.
  - If the original returned `Mission.ON_MESSAGE_RESULT_RESEND`, it does nothing. Otherwise it inspects the message and returns the original's values unchanged.
  - The only behaviour change is the gated Trap filter (P14).
- **U staleness.** Sender `tick u32` (milliseconds); keep the newest per entity. Nothing on U is needed for correctness.

#### 6.2 Reliable (R)

| Op | Dir | Payload | When |
|---|---|---|---|
| `HELLO` | both | `proto, exeBuild, role, missionCode, locationCode, epoch, rosterDigest, saveRedirected u8` | Link up and at every `OnMissionGameStart` |
| `RECEIVED` | H→G | `rid` | On every request |
| `REJECT` | H→G | `rid, reason, entHash, currentState, currentVer` | Refused request |
| `MISSION_TRANSITION` | H→G | `kind (finalize, abort, restart, continue, reload, deploy), srcMission, nextMission, clearType, heliRoute, layoutCode, clusterId, deployTime, isNoSave, newEpoch` | Host `ReservePlayerLoadingPosition(loadType)` wrapper, `ExecuteReload`, `OnEndMissionPreparation` |
| `MISSION_SEEDS` | H→G | `solface_groupNumber, hosface_groupNumber, rev_revengeRandomValue, rev_revengeLv[]` (chunked), `missionStartClock, missionStartWeather*, heli_missionStartRoute, ply_initialPlayerState, ply_missionStartPos/Rot, mbLayoutCode, mbClusterId, luaSeed` | Host `TppMission.Load` entry |
| `MISSION_READY` | both | `epoch, missionCode, rosterDigest, soldierCount` | `OnMissionGameStart`, after the maps are built |
| `PROGRESS_SYNC` | H→G | Chunked `(gvarHash, index, value)` for the progress subset (9.3) | Only while both PCs are in a heli space, before a follow |
| `REQ_MISSION` | G→H | `rid, kind (exit_area, abort, restart, continue, heli_ride, clear_request), arg` | Intercepted guest exit |
| `REQ_GAME_OVER` | G→H | `rid, type, cause` | Guest player death or guest mission game over |
| `GAME_OVER` | H→G | `type, radioHash` | Host `ReserveGameOver` returns true, or the host commits a guest request |
| `SEQUENCE` | H→G | `seqHash, ver` | Host per-frame poll sees `svars.seq_sequence` change |
| `REQ_SEQUENCE` | G→H | `rid, fromHash, toHash, trapHash` | Allow-listed guest trigger (9.5) |
| `SVARS_DIFF` | H→G | `count, (nameHash, index, value)…` | 1 s poll of `saveVarsList` keys and `mis_objectiveEnable`; full resend on a checksum mismatch |
| `OBJECTIVE` | H→G | `count, objectiveHash×n, radioHash, flags` | Host `TppMission.UpdateObjective` |
| `CHECKPOINT` | H→G | `cpNameHash, ver` | Host `TppMission.UpdateCheckPoint` succeeded |
| `CHECKPOINT_SNAPSHOT` | H→G | Chunked svars: `sol*`, `cp*`, `hos*`, `enemyHeli*`, `ene_wkrg*`, `uav*`, `securityCamera*`, `passenger*`, mission `saveVarsList`, `seq_sequence`, weather | After the host's vanilla checkpoint save completes (no extra `StoreSVars`) |
| `ENEMY_OUTCOME` | H→G | `entHash, entType, state, ver, cause, actor, rid, x, y, z` | Every committed life change |
| `REQ_OUTCOME` | G→H | `rid, entHash, entType, state, seenVer, cause, attackId, isHeadShot, x, y, z` | Rule 4.3 |
| `PHASE` | H→G | `cpHash, phase, ver, keep` | Host escalation (own or applied) |
| `REQ_PHASE` | G→H | `rid, cpHash, phase, cause, soldierHash, x, y, z` | Guest local `ChangePhase` above the host's phase for that CP |
| `GIMMICK` / `REQ_GIMMICK` | H→G / G→H | `locatorHash, dataSetHash, kind, ver` | `BreakGimmick` (W5) |
| `CLOCK` | H→G | `clock, timeScale` | A jump (deploy, sleep, `SetTime`) |
| `WEATHER` | H→G | `weatherType, priority, interpTime, fogDensity, fogType` | Host `vars.weather` change (polled) or `RequestWeather` |
| `PLAYER_AREA` | both | `areaIndex, blockIndex` | Free-roam area trap (local player only), `StageBlockCurrentSmallBlockIndexUpdated` |
| `OWNERSHIP` | H→G | `count, (entHash, owner)…` | Ownership change (1 Hz, with hysteresis) |
| `RESYNC` | G→H | `rid, what (outcomes, phases, svars, all)` | Digest mismatch or native overflow |

Budget: R under 1 KB/s in steady state; bursts under 64 KB.

#### 6.3 Unreliable (U)

| Op | Dir | Payload | Rate |
|---|---|---|---|
| `DIGEST` | H→G | `epoch, tick, clock, weather, (cpHash, phase)…, outcomeVerSum, svarsChecksum, seqHash` | 1 Hz |
| `GUEST_DIGEST` | G→H | `epoch, tick, (cpHash, localPhase)…, pendingCount` | 1 Hz |
| `SOLDIER_SNAP` | owner→other | `{entHash, x, y, z mm, yaw cdeg, lifeState, status, ver}` | W6 only; tiers 5/2/0.5 Hz within 50/150/300 m of the receiver; nothing beyond |

Digest rules:
- An `outcomeVerSum` or `svarsChecksum` mismatch for 3 consecutive digests → `RESYNC`.
- A CP below the host's phase on the guest → re-apply `PHASE` (upward only).
- A clock difference above 60 game seconds, after adding `timeScale × one-way latency`, corrects the guest's clock. The correction is suspended while either PC is paused, in a demo or loading.
- Random weather pause is re-applied each digest.

### 7. Applying outcomes

#### 7.1 Enemy outcomes (both PCs, script only, at most 8 per frame, after the apply rule in 4.4)

| State | Command | Notes |
|---|---|---|
| SLEEP / FAINT / DYING / DEAD | `SendCommand(id, {id="ChangeLifeState", state=TppGameObject.NPC_LIFE_STATE_X})` | The handler reads `state` only (0x135BB12); 0 is ignored |
| FULTONED (mission target, or D1 = both) | `SendCommand(id, {id="RequestForceFulton"})` (0x13601F3) when realized | Runs `TppTerminal.OnFulton`; skipped if `TppEnemy.IsRecovered(name)` |
| FULTONED (D1 = fultoner only, not a target) | `RequestVanish`, then `TppEnemy.SetRecovered(gameObjectId)` | Pattern at `lib/TppEnemy.lua:2872` |
| VANISHED / corpse | `RequestVanish`; corpse: `{type="TppCorpse"} RequestDisableWithFadeout{name}` | `lib/TppEnemy.lua:2864-2872` |
| HOLDUP | No setter known; record and mark | Probe P8 |
| NORMAL (woke) | `state=0` does nothing (verified); try `RecoveryLife` (0x135D4FF); otherwise wait for the local timer | Probe P3b |

- **Pending ledger.** Unrealized, out-of-area or not-yet-spawned entities are re-checked round-robin, 10 per frame. The ledger is dropped at the end of the epoch.
- **Readback** the next frame; one retry, then log.

#### 7.2 Alert

- Escalation only.
  - The guest's local escalation above the host's phase sends `REQ_PHASE`.
  - The host takes the maximum, applies `SetPhase` and broadcasts `PHASE`.
  - The guest applies `PHASE` only if its local phase is lower.
- Decay is local on each PC. The host does not let a CP decay below the guest's `localPhase` from `GUEST_DIGEST`: it re-applies `SetPhase` or uses `SetKeepCaution` / `SetKeepAlert{enable=true}` while the guest reports a higher phase.
- Revenge, heroism and announcements stay local to each save. `PROGRESS_SYNC` copies the host's revenge values.
- **Avoid:** `SetIgnoreCoop` / `RemoveIgnoreCoop`, and the soldier command `ChangePhase` (0x135AF88), which is not a CP phase setter.

#### 7.3 Guest suppression

These wrappers are active only for the guest in follow mode. A module token is set only while a host event is being replayed. Wrappers are installed in the TppMain chunk, before `TppMission.Messages()` is first built.

| Wrapped | Without the token |
|---|---|
| `ReserveMissionClear`, `ReserveMissionClearOnRideOnHelicopter`, `_ReserveMissionClearOnOutOfHotZone`, `AbortMission`, `AbortMissionByMenu`, `AbortForOutOfMissionArea`, `AbortForRideOnHelicopter`, `RestartMission`, `ExecuteRestartMission`, `ContinueFromCheckPoint`, `ExecuteContinueFromCheckPoint`, `GameOverAbortMission`, `GameOverReturnToTitle`, `ReturnToMission`, `Reload` | `REQ_MISSION` (or nothing for game-over menu items); return as the original does when it refuses |
| `ReserveGameOver` | `REQ_GAME_OVER{type}`; nothing local happens |
| `OnPlayerDead` (guest's own death) | `REQ_GAME_OVER{PLAYER_DEAD}`. The host commits a shared game over (D3) |
| `TppSequence.SetNextSequence` | Dropped, or `REQ_SEQUENCE` when the trap or sequence pair is allow-listed (9.5) |
| `UpdateCheckPoint`, `UpdateObjective` | Dropped; they run only from `CHECKPOINT` / `OBJECTIVE` |
| Random weather | `PauseNewWeatherChangeRandom(true)`, re-applied every digest |

- **Guest support-heli ride:** `REQ_MISSION heli_ride`. The host commits an abort or clear for both, or the guest gets a "partner must decide" notice. What happens to a heli that is refused takeoff is probe P15.
- **Trap filter (P14, gated):** the observer drops Trap messages whose entering object is a non-local player instance, so the partner body cannot fire mission or area traps.

#### 7.4 Guest actions

| Action | Guest detects | Host validation | Host applies |
|---|---|---|---|
| Kill | `Dead`, attacker = guest | Known, not terminal, epoch | `ChangeLifeState` DEAD, or `AddDamage{attackId, ownerGameObjectId, isOneHitKill=true, isHeadShot}` (0x135DAD7) when credit matters (P9) |
| Tranquilise, CQC, choke | `Neutralize`, `Unconscious`, `Dying` | Same | `ChangeLifeState` with the constant |
| Hold-up | `Holdup` | Same | Record and mark (P8) |
| Fulton | `Fulton` with fultoner = guest (not `FultonInfo`, not `FultonFailed`) | Not terminal | 7.1 fulton row |
| Damage | `Damage` | — | Nothing (no HP sync) |
| Gimmick break | `BreakGimmick`, attacker = guest | Not broken | P10, otherwise the mission's svars |
| Mission pickup | `OnPickUpCollection` / `OnPickUpPlaced` | Tracked uniqueId | Mark; svars follow |

#### 7.5 Checkpoints, continue and late join

- **Host checkpoint.** After the vanilla save, the host sends `CHECKPOINT` and then `CHECKPOINT_SNAPSHOT`, read from svars that the save path has just stored. The guest:
  - replays `UpdateCheckPoint{checkPoint=name, ignoreAlert=true, permitHelicopter=true}` with the token;
  - keeps the snapshot in memory as `pendingContinueSnapshot`.
- **Continue.**
  1. The host's `ReservePlayerLoadingPosition(CONTINUE_FROM_CHECK_POINT)` sends `MISSION_TRANSITION kind=continue`.
  2. The guest runs `ExecuteContinueFromCheckPoint` with the token.
  3. In a wrapper placed immediately before `TppEnemy.RestoreOnContinueFromCheckPoint2()` (`lib/TppMain.lua:220`), the guest writes `pendingContinueSnapshot`. The vanilla code then restores from svars (P6).
  - Fallback: replay the per-epoch outcome ledger after the load.
- **Late join:** not supported in v1. The guest joins at a heli space or free-roam start, or the host performs a shared continue or restart.

#### 7.6 Local by design

Inventory, ammo, health, suit, buddy, supply drops, support fire, decoys, interrogation, non-mission collectibles, plants, markers (until a later phase), free-roam quests (host only, guest quest blocks not reconciled), and results screens (each save keeps its own).

#### 7.7 Mother Base staff (W1 at 30050, fb1031)

- **Why it was off:** `W.MISSIONS[30050]` had no `enemies` since WP5, so W1 never ran at Mother Base (the 0.14.5 online session: staff knockouts and Fultons on the host, `commits=0` on both copies, debug enemy commands `sync=none`). Turning `enemies` on alone is not enough: Mother Base has no free-roam area trap, so `W.activeCp` stays nil and the W4 snapshot read every soldier as unknown; the guest refused it (`no_readable_state`) and W1 never came on.
- **Now (`allCp`):** the snapshot counts every CP as loaded (`W.inferredCp` = all CPs, `world_align.lua`); the apply rule already treats every CP as active when `W.activeCp` is nil. No area change ever re-checks the ledger there, so with `allCp` the whole ledger (an apply that failed while the staff member's platform was not loaded on that copy included) is re-checked every 10 s (`world_enemy.lua`).
- **Identity:** the roster digest hashes the Mother Base soldier slot names (the platform layout), not staff ids. A different layout keeps W1 off (PAIRED `match=0`) as before; the same layout with different staff is not detected (game Lua reads staff ids through `GetStaffId` / `TppMotherBaseManagement.GetStaffIdFromGameObject`, unused here), so a knockout acts on the same slot on both copies. The guest may continue its own save at 30050 (0.14.5 session: `save_handoff=need; need=0; why=same_scene`) and the rosters still matched (126 soldiers, 7 CPs, `match=1`): the slots match, the staff behind them may not, which is why no partner act removes or kills one.
- **Play: lab only (`enemiesLab`).** Runs `1bc18e9c` / `1d949f8c` showed W1 on at 30050 and the sleep / spare-Fulton commit and apply lines on both copies, but no frame showed the staff member, so in play `W.feature` drops `enemies` at 30050 (as in 0.14.5) until a run shows the outcomes on screen. fb1031-on runs `3781890d` / `8ce9d986` (play, `mb-staff-gone-play.txt`, W1 on in that build) showed it on the **host's** screen (staff member 1325835864 saluting 2.5 m ahead, asleep, lifted by the Fulton balloon, gone) and on both copies' logs (guest apply state=2 readback=2, then `event=spare; state=6; as=7` and apply state=7 readback=7; no `RequestForceFulton`, no game over), but never on the **guest's**: each PC moves its own staff, so in `3781890d` the guest's copy of him was elsewhere, and in `8ce9d986` the guest went to its own copy of him (`enemy goto 1325835864`, logged 3.3 m away, asleep) and no guest frame shows him before or after (the camera stayed level behind the guest's own body; the pitch request did not show him). So play stays lab only (red `5532ffa5`, green `1ce60874`). Missing: one guest frame of the staff member asleep and then gone (a camera that frames a body lying 3 m away, for example the guest standing 5 m off and level, or crouched). mb-staff-sync built the staff placement below (`staffPlace`, which `enemiesLab` also drops in play) and the play run for it (`tools/worker/scenarios/mb-staff-sync-play.txt`: the guest 5 m off with `enemy goto` distances, place-first, a guest Fulton of a second staff member, 30 s of a third), but no hidden run was admitted (four launches 19:12-20:22, the machine under the commit-free 15 GB / D: 3 GB floors), so play stays lab only (red `5e6ee098`, green `5968e9bb`) and nothing here has run in a game yet.
- **Staff placement (`staffPlace`, mb-staff-sync).** The cause of "soldiers in Mother Base don't seem synced": each PC moves its own staff, so the same staff member stands in different places on the two screens and an outcome happens to someone the partner does not see. Host-authoritative, as the free-roam patrol corrections (`world_place.lua`), which were lab-only everywhere because the guest's native placement (`GuestWarpReceipt`) needed lab mode and `debug_soldier_warp=1`:
  - **Native:** `soldier_warp::GuestAdmit` adds a second rule next to the lab key: play or lab, the current mission is 30050, and the grant is a correction (kind 3) only, never a warp, route or come. Everything else stays: the target comes only from the grant `world::Core` recorded when the guest's Lua popped the host's ENEMY_DEBUG (same epoch, at most 15 s), a live host pose at most 1 s old, 60 m range (for staff from the nearer of the host and the guest's own player), 160 sends per grant, the process-wide fault/anomaly latch, the startup `Verify` byte gate (now also for a play or lab guest: `soldier_warp=ready; key=mb_staff`). No new game address.
  - **Host:** the scanner samples NORMAL staff within 55 m of either player (the partner body, `TppPlayer2` index 1, every 500 ms), at most 2 position reads per tick over the 126 staff, nothing read at the origin, never a combat flag on staff (no `SetDiscoveryPlayer` at Mother Base); a sample goes out when they moved 1.5 m or every 3 s.
  - **Guest:** corrects its copy beyond 2 m on two samples or 5 m once (2.5 s apart; free roam 3.5 / 8 m), never force-realizes a staff member it has not loaded (`realize_timeout`), never pins `SetOutOfArea` there; `event=place_div` lines carry both positions (`host_xz`, `here_xz`).
  - **Place first, then the outcome:** right before the host's own outcome of a standing staff member, `W.staffAnnounce` sends where they are (x, z now, the ground y of their last standing sample, since a lying body reads lower); the guest's `W.staffHold` keeps that outcome until its copy stands there (a queued or running placement, or the newest sample over 1.5 m away), at most 4 s, then applies it anyway. The guest's own act needs no host-side move: its copy already stands where the host's is.
  - **Saves:** the warp changes only the live transform; the staff roster (`TppMotherBaseManagement`) is untouched and soldier state lives in mission svars that `TppEnemy.RestoreOnMissionStart2` zeroes at every Mother Base load [I].
- **Kept off:** Mother Base vehicles (VEHICLE_GONE / VEHICLE_STATE) stay lab-only.
- **A partner never kills or removes staff (`spareStaff`, review mb-1):** stock `f30050_sequence.lua` (`this.Messages()` GameObject `Dead` / `Dying`, any non-player) reserves `TARGET_DEAD` / `PLAYER_KILL_DD` (Mission Failed). A guest's kill mirrored as ChangeLifeState DEAD/DYING failed the host's mission for an act the host did not do. Now `E.applyOne` applies a partner's DYING or DEAD outcome to the local staff member as FAINT (`event=spare; state=N; as=3`); the ledger keeps the committed state, so nothing is re-requested. A **killed** staff member therefore shows **knocked out** on the partner's screen. A partner's **Fulton** (fb1031-on, coordinator decision) lands as VANISHED (`event=spare; state=6; as=7`): the existing state-7 apply, `TppEnemy.SetDisable` (stock `lib/TppEnemy.lua`: only `SendCommand{id="SetEnabled", enabled=false}`), so the staff member is gone on that screen too, without a balloon. The copy that did not act never runs the stock own-staff Fulton (`RequestForceFulton`; its roster effect is [U]). Why disabling is safe for the other player's save: no Fulton, Dead or Dying message follows a disable (f30050's `TARGET_DEAD` comes only from the `Dead` / `Dying` handlers), no extracted Lua path reacts to a disabled staff member, and the roster is native `TppMotherBaseManagement` data the command does not touch [I]; the soldier's own state lives in the mission svars (`svars.solState`, `CATEGORY_MISSION`) that `TppEnemy.RestoreOnMissionStart2` zeroes at every new Mother Base load, so the staff member is back on the next visit (a checkpoint Continue inside the same visit may keep them away [I], as it keeps a stock knockout). The host's own staff kill still fails the host's mission (stock), W1 goes off (`reason=game_over`) before any commit, and the guest's own kill is swallowed on the guest copy (`TARGET_DEAD` is not a death kind in `world_recovery.lua`).
- **Local by design:** morale and training effects of a hit count only on the copy whose player hit the staff member (the other copy gets ChangeLifeState, a knockout for kills; a Fulton disables the staff member there).

#### 7.8 Enemy follow (the enemy sync plan's round 1, M1; lab key `soldier_follow`, lab and play)

The 0.14.7 friend session (fb1101) showed why a shared kill "seemed to die on the other screen sometimes": each PC moved its own guards, the guest's game refused all 37 of the host's free roam position fixes (`event=place; kind=correct; result=refused; why=native_off; native=role`: 0.14.7 admitted them only for Mother Base staff), the two copies of a guard stood 8.7 m apart at the median and up to 95 m, and an outcome played wherever the other PC's copy happened to be. Round 1 keeps every guard's AI on each PC and pulls the guest's copies of the guards near either player to where the host's stand, with the levers 0.14.7 already proved (the native set-transform warp of 7.7, the host's patrol scanner, place first, the W1 outcome engine). Everything new is behind `soldier_follow` (lab.ini, CFG r[19] `W.followOn`; it follows the gun profile in lab and play since the round's hidden runs, the plan's decision 1; a named 0 is the off switch).

- **Native admission** (`soldier_warp::GuestAdmit`, game_script_observer `GuestWarpReceipt`): a third rule after the lab key, the follow rule: play or lab with `soldier_follow`, the mission 30010, 30020 or 30050, and a correction (kind 3) only. The follow and staff rules take the range from the nearer of the two players (`NearerSquared`), using this guest's own pose when the host's remote pose is stale (P1: every correction refused `pose` while the players were 130 m or more apart or right after a long teleport); a follow target more than 250 m from both refuses `far`. Without the key, play still refuses free roam corrections (`role`). The target is still only the grant `world::Core` recorded when the guest's Lua popped the host's ENEMY_DEBUG (now 64 slots: the follow scan samples many more guards). A fault latches the native warp for the process as before; an anomaly (an answer other than `out=0 h=1` / `out=-1 h=0`) now counts against its guard (the second latches that guard, `soldier_warp=soldier_latched`), the fifth across guards latches the feature (P1: one `r=0 h=0` in 23 warps latched 0.14.7's whole feature). Static reading of P1's anomaly: the play path's hop is the handle object's `0xbf5450` (`[handle+0x30]==2` answers a constant, else `[handle+0x20]->vt[0x70]`), which is the soldier type's dispatch `0x9e9ab0`: it looks the soldier implementation up by hash, calls its handler (`vt[0xa8]`, the handler 7.7 verified) and, when that answers not realized (-1), forwards the message to a fallback object (`[impl+0x58]->vt[0x10]`, then `0xbf6570`) whose answer can be 0 without the handled flag. So `r=0 h=0` is a soldier between realized states, not a mis-dispatch [I]. Run 3e01f334: 0 anomalies in 277 warps.
- **The partner's position** comes from a native receipt (`return 'MGS_COOP_WORLD_PEER_V1'`: the remote pose as this copy sees it, while paired and connected, at most 2 s old). GetPosition on the partner body (TppPlayer2 index 1) answers nothing (run f2d2db9b: the scan never saw the partner).
- **Host scan** (world_place.lua `P.scan`, every W1 mission): NORMAL guards of the active CPs within 55 m of the host or 60 m of the partner, 4 reads and 16 scans per update, read every 500 ms within 40 m of either player (1 s beyond, 2 s out of range), sent (ENEMY_DEBUG kind 3) when they moved 1 m or every 2 s. A guard at least 15 m nearer the partner than the host is the guest's copy's to move (two-way follow is M3). A guard whose correction the guest refused (`native_off`, `far`, `latched`) waits 10 s. Every pairing re-reads every guard (a rejoining guest is placed at once). A guard going up under a balloon (a Fulton message in the last 30 s, or rising over 3 m faster than 2 m/s) is not sampled. `event=place_scan` every 10 s: updates, reads, sent, the guest's side, the Lua cost (`avg_us`, `max_us`: run 3e01f334 21 us average per update). Play without follow scans only Mother Base staff (its guest refuses free roam corrections); the lab still scans them for the lab key.
- **Guest** (world_place_guest.lua `P.sample`): the host's sample moved on by the host copy's speed (from its previous sample) for min(age + 100 ms, 400 ms); off this player's screen (outside a 100-degree cone of the camera, or beyond 60 m) a guard is corrected after one sample over 2.5 m, 1 s apart; on screen after two samples over 5 m or one over 8 m, 1.5 s apart; never a copy within 3 m of this player, nor one 250 m from both players; the first sample of each guard after a pairing corrects beyond 2.5 m at once; FIFO 16. Mother Base staff keep 7.7's thresholds; the lab key without follow keeps 0.14.7's. `event=place_err` each second (bins under 1, 2, 3, 5, 8, 15 m and beyond, p50 / p95 / max, guards within 60 m of this player); `event=place_div` only for corrections, a place-first, a first sample or a guard 2 m off.
- **Combat** no longer pulls the copies apart: the 0.14.7 nudge (`SetDiscoveryPlayer` on the guest's copy while the host's fought: 11 times in the friend session) is gone (`event=place_combat; ...; nudge=0`), and a guest's REQ_PHASE ALERT makes the guard discover the host only within 40 m of the host. Flag 1 on a correction is the host copy's combat flag, never "come" (run f2d2db9b: the first combat correction of each guard marked him come and his samples were ignored after it).
- **Place first** (`W.placeAnnounce` / `W.placeHold`, 7.7 generalised): right before the host's own outcome of a standing guard, his position (flag 4 "first", the standing ground y; the last standing x, z for a body that rose); the guest corrects from 1 m at once, ahead of the FIFO, and holds the outcome until its copy stands there, at most 1.5 s in free roam (4 s at Mother Base). `event=place_first; step=tx | hold | done; ... err_m`; every apply with a wire position logs `d_m` (the two copies' distance when applied: run 3e01f334 0.2-0.9 m).
- **Crossfire** (the authority rules): the guest requests every forward change of its copy with its attribution (REQ_OUTCOME v[8]: 0 its player, 1 its buddy, 2 another attacker, 3 none). The host commits 0 / 1 as before; an unattributed DYING / DEAD / FULTONED wins (its copy cannot be undone) when the host's copy stands within 150 m of the partner (`event=terminal_win`); anything else unattributed is refused with REJECT 12 and the guest applies the host's state (a crossfire knockout wakes up there). Before, `event=divergence; why=not_attributed` left the guard dead on one screen and walking (later knocked out) on the other.
- **Mother Base wake-up** (fb1101 G6): a failed RecoveryLife is tried again every 5 s, at most 3 times, outside allCp too.
- **Proof tooling**: `tools/enemy_div_report.py --host --guest` computes the pass gates from both logs (refusals, the applied share, the place error p50 / p90, place-first, `d_m`, crossfire, commit / apply parity, the never-lines).
- **Probes** (hidden runs; `docs/STATUS.md` "Enemy follow"): P1 `79804732` (lab key, 30010, ALERT from the arrival): 22 corrections applied with 0.01-0.4 m landing error in 3 frames, then the guard stands still 2-7 s while the host's copy walks on (hop, stand, hop); P1b `bd58d327` (quiet for 14 s): the copies converge on their own routes (5.3, 3.4, 1.6, 0.1 m); P2: no stance trouble seen on the `leaf=engine` path in ALERT, the stance check still cannot run there (no leaf), see the anomaly above.
- **Round 1 runs** (play, `-WanSim bad`, research build): 30010 `3e01f334` (276/277 applied, 0 anomalies, place error p50 2.6 m / p90 8.8 m), `2b0b77ea` (320/320 applied, p50 1.74 m / p90 6.89 m over 1099 samples, place-first 3/3 within 2 m, `d_m` 0.0-0.1 m for standing outcomes, 0 `not_attributed`, 69 commits all applied, no latch or fault), 30050 `ee4cc176` (every gate: p50 1.14 m / p90 3.94 m). Every 30010 run had the outpost in ALERT from the arrival (`ChangePhase` 3 right after the players were placed by its guards, enemy ignore notwithstanding), so its p90 gate (5 m) failed on running guards (the frozen-guard limit below), mostly while the players stood apart; while both stood by one guard (100-200 s of `2b0b77ea`, still ALERT) 77 % of 416 samples were under 3 m and 91 % under 5 m.
- **Known limits** (until M3 / M4 / M5): a corrected guard in combat stands still until his next correction while the host's copy runs on, so a running guard hops every 1-1.5 s off screen and every 1.5 s or more on screen (glides are M4, matching routes M5); a guard 15 m nearer the guest than the host is not corrected (two-way follow is M3); the host's copy of a guard the guest fights alone stands wherever the host's AI has it; the guest's own kills land where the host's copy stands (place first on the host is M3).

### 8. Soldier transforms (W6)

- **Ownership.** The guest owns a soldier if it is at least 30 m nearer to the guest than to the host. Re-evaluated at 1 Hz. It affects only snapshot sending, correction and outcome attribution (4.3), never perception.
- **Correction** happens only when all of these hold:
  - the copies are more than 25 m apart for at least 5 s;
  - both are NORMAL;
  - the CP is in SNEAK;
  - the non-owner's player is more than 80 m away.
- **The correction** is `SetEnabled{enabled=false}`, then `SetSneakRoute{route, point}`, `SetCautionRoute`, `SetAlertRoute`, then `SetEnabled{enabled=true}` (`lib/TppDebug.lua:63-67`). The route and node come from the route sets, or from P7.
- **Never** native body moves, and never per-frame corrections. (Superseded in part: 7.7 and 7.8 move the guest's copy with the engine's own set-transform message, one warp per correction, never per frame. The route correction below is M5's.)
- **Out-of-area union.** Wrap `f30010_enemy.UpdateOutOfAreaSetting` and `f30020_enemy.UpdateOutOfAreaSetting` (`mission/f30010/f30010_enemy.lua:1473`, `mission/f30020/f30020_enemy.lua:1339`). The wrapper sets `SetOutOfArea(false)` for the local area plus the partner's area. Needed from W1.

### 9. Mission flow

#### 9.1 Partner beyond 30050 (W3)

- Replace the `vars.missionCode ~= 30050` gates (`lua/coop/partner_sync.lua:100,167`, `player_package_selection.lua:19`, `equipment_loading.lua:7`, `marker_loading.lua:18`) with `PARTNER_MISSIONS[code]`.
- Session-route modules (`client_join`, `host_admission`, `host_session_trial`, `session_feedback`, `session_lifecycle`, `enemy_targeting`, `respawn`) stay gated and inert.
- **Packs:**

  | Pack | Controller |
  |---|---|
  | f30010 | `single_pl2_instance_scarf.fox2` / `PLTypeNormalScarf` |
  | f30020 | `single_player2_instance.fox2` |

  Each needs `tools/stage_controller_replacement.py` generalised and a re-pin of `native/archive_mount.cpp` (digest `26ce7c…`).
- World sync does not depend on a partner body. A mission not in the table syncs without one.

#### 9.2 Following

- **Host detection:**
  - `TppMain.ReservePlayerLoadingPosition(loadType, …)` maps `MISSION_FINALIZE`, `MISSION_ABORT`, `MISSION_RESTART` and `CONTINUE_FROM_CHECK_POINT` to a `kind`.
  - `ReserveMissionClear` (returns true) supplies the clear type.
  - `ExecuteReload` is a reload.
  - `OnEndMissionPreparation(deployTime, …)` (`lib/TppMission.lua:1228`) is a deploy from the ACC.
  - `MISSION_SEEDS` is sent at the host's `TppMission.Load` entry.
- **Guest replay.** With the token set, the guest calls the matching high-level API (`ReserveMissionClear`, `AbortMission`, `RestartMission`, `ExecuteContinueFromCheckPoint`, `OnEndMissionPreparation`). Calling `OnEndMissionPreparation` outside the preparation UI is probe P16.
- **Lockstep.** If the guest is not in `srcMission`, it follows there first.
- **Seeds.** The guest's `TppMission.Load` wrapper overwrites the seed values at entry, before `TppEnemy.PreMissionLoad`. Both PCs call `math.randomseed(luaSeed)` in `OnAllocate`. This is best effort. A loadout mismatch can only be fixed by a shared restart; it is logged.
- **Roster check.** On a `rosterDigest` mismatch, enemy sync is off for that epoch. Mission flow, clock and weather continue.
- **Ready barrier.** The guest drains the world queue only after its own `MISSION_READY`. The host holds requests until the guest's `MISSION_READY` arrives, and its events for the epoch wait in the guest's native world queue.
- **Guest arrival at Mother Base (fb1031).** Online 0.14.5: a guest that followed the host's sortie into 30050, and later Continued into it twice, stayed in its arrival helicopter (remote act 102) and the host never drew it. The host itself had started on foot (f30050 `missionStartPosition.IsUseRoute()` is false with a queued MB opening demo), the guest rode the host's route. Now (`lua/coop/world_follow.lua`, `F.arrival`):
  - Way out (default on): an armed guest at 30050 riding a helicopter (`Tpp.IsHelicopter(vars.playerVehicleGameObjectId)` or `PlayerStatus.ON_HELICOPTER`), not in a game over, gets its pause-menu Restart locally instead of the `host_only` refusal; its Continue runs that restart (`step=local_restart;fn=;why=heli`). Within 30 s, that restart's `ReservePlayerLoadingPosition` (MISSION_RESTART only) gets the reset below (`step=mb_foot;at=restart`): the menu restart passes no return-to-mission flag, so vanilla `ExecuteRestartMission` would keep the helicopter start. The guest's queued MB demo is cleared there, before the 30050 pack list (which may queue one again with matching packs).
  - Off (`F.arrival.auto = false`; the review found it unproven and too broad: it also replaces the helicopter arrival that works and moves the guest to the Command cluster wherever the host landed): a linked co-op guest (CFG level >= 3, role 2, link up) entering 30050 with a helicopter start (`gvars.ply_initialPlayerState == RIDEON_HELICOPTER`; the route only when that constant is unreadable; for a Continue only while `svars.ply_isUsedPlayerInitialAction` is false, which vanilla `VarSaveOnUpdateCheckPoint` sets off the helicopter, so the W4 shared Continue keeps the host spawn) gets the vanilla `TppMission.ResetMBFreeStartPositionToCommand()` (the on-foot Command start vanilla uses for an MB restart), after `ReservePlayerLoadingPosition` and after every seed apply. Log `event=follow;step=mb_foot;at=reserve|seeds;route=;state=`.
  - With it on, the queued MB demo is also cleared at that point and at the 30050 allocation, the latter only when `gvars.f30050_missionPackIndex == 0` (1 = f30051 demo stage, 2 = battle hangar: the pack list already chose packs for that demo). Log `step=mb_demo;at=;cleared=<name>`.
  - The host, free roam and an on-foot start (the lab boot, a travel landing) are unchanged. Collect logs keeps up to the last 8 game launches of `script-observer.log` (2 MB each with the omission line: first quarter and end; 16 MB in all, from the log's last 24 MB) so a guest that relaunched still sends the stuck launch.
- **Arrival prevent (arrival-prevent, from the friend's full 0.14.5 log).** The stuck guest's helicopter never flew: it appeared at a stored position about 8 m over the Command platform 1 pad (9.116, 8.75, -42.43, yaw 0) and stayed there; every helicopter start that began at the route start flew in and landed. The 0.14.5 sortie seeds copied the host's start position (`initialPlayerFlag/Pos/RotY`, `ply_useMissionStartPos/ply_missionStartPos/Rot`), which the host's vanilla `ReservePlayerLoadingPosition` had written from the iDroid's stored helicopter start (`MbDvcActHeliLandStartPos` -> `mis_helicopterMissionStartPosition`), and re-applied them after the guest's own Reserve had reset them; then `world_recovery` saved the host's `CHK_MainGame` checkpoint while the guest still sat in that helicopter, so every Continue rebuilt it (reproduced in hidden run 15eda621). Defaults: the watchdog and the logs are on (run 15eda621 proved the frozen arrival and the watchdog's way out); the route-start seeds (`F.arrival.routeStart`), the checkpoint hold (`F.arrival.holdCheckpoint`) and the host hint (`W.arrivalWatch.hint`, note 19) are on since 0.14.6 (the user's release of every built feature) but have never run in a game (their fix-proof run never reached the verify lock: commit free under 15 GB); `false` turns each off:
  - Seeds (`lua/coop/world_follow_seed.lua` key[7] "pose", `S.ride`; `F.applySeeds`): when the seeds start the guest riding a helicopter in on a route (`ply_initialPlayerState == RIDEON_HELICOPTER`, `heli_missionStartRoute ~= 0`; a Continue only while `ply_isUsedPlayerInitialAction` is false) into free roam or Mother Base (30xxx), those eight keys get vanilla's ride-on reset (0 / false) at every apply (Reserve, VarRestore, StageBlock, RequestLoad, Load): the helicopter starts at the route start. Logged `step=seeds_apply;...;start=route`. An on-foot start, a used entry and the helicopter spaces keep the host's pose (W3/W4).
  - Checkpoint (`lua/coop/world_recovery.lua`): a guest riding a helicopter (`F.arrival.heli`) at 30010/30020/30050 holds a received shared checkpoint (`step=checkpoint_wait;reason=arrival_heli`) and applies it once off the helicopter, so a helicopter is never saved.
  - Watchdog (`lua/coop/world_arrival.lua`, a new chunk after recovery; `W.arrivalWatch.on`): at 30010/30020/30050, within 180 s of the start, in game and outside cutscenes, a player ON_HELICOPTER that moved less than 1 m for 20 s with no `LandedAtLandingZone`/`DescendToLandingZone`/`PlayerHeliGetOff`/`LandingFromHeli`: a linked guest runs the way out itself (`step=unstick;why=static_heli`; a local `RestartMission` under the follow token; Mother Base: on foot at Command via `A.foot`; free roam: `A.field` clears the stored start position at the restart Reserve so the helicopter starts at its route start); a linked host gets world note 19 (15 before the merge with fb1031 mbgo) "Stuck in the helicopter? Pause and choose Restart." (`step=host_hint`) and nothing else.
  - Logs: `event=arrival;step=start` / `step=start_pos` at every 30010/30020/30050 start (both roles: initial and mission start positions, start state, used flag, route, layout, cluster, MB demo, helicopter) and `step=heli_msg` (seven landing / get-off messages, 16 a mission).
  - Lab: `goto=30050 goto_lz=1` stores the friend's helicopter start position on the host before the sortie (repro); probe 20 `guest_frozen_heli` (guest) keeps the 0.14.5 pose seeds for the next sortie (forced frozen arrival for the watchdog).

#### 9.3 Progress alignment

- **When:** applied only while the guest is in a heli space (40xxx), before a follow. Never between a clear transition and the guest's arrival in the next heli space.
- **Copied:**
  - Story: `str_storySequence`, `str_missionOpenPermission`, `str_missionOpenFlag`, `str_missionClearedFlag`, `str_missionNewOpenFlag`, `str_elapsedMissionCount`, `str_didLostQuiet`, `str_isAllMissionCleared`, `str_isAllMissionSRankCleared`, `str_quietReunionMissionCount`.
  - Revenge: `rev_revengeLv`, `rev_revengePoint`, `rev_revengeBlockedCount`, `rev_revengeRandomValue`.
  - Quests: `qst_questOpenFlag`, `qst_questClearedFlag`, `qst_questActiveFlag`, `qst_questRepopFlag`.
  - All verified in the gvar declarations (`TppGVars.lua`).
- **Never copied:** MB management, staff, resources, the retry category.
- Written only to the redirected co-op save.

#### 9.4 Death, game over, clear

| Event | Host | Guest |
|---|---|---|
| Either player dies (D3 default) | Vanilla `OnPlayerDead` → `ReserveGameOver`; a guest `REQ_GAME_OVER` → host commits `TppMission.ReserveGameOver(PLAYER_DEAD…)` | `GAME_OVER` → `ReserveGameOver` with the token; its own menu choices are inert |
| Mission game over | Vanilla; `GAME_OVER` | Same |
| Game-over menu choice | Host only; the choice reaches `ReservePlayerLoadingPosition` → `MISSION_TRANSITION` | Replays it |
| Checkpoint | 7.5 | 7.5 |
| Clear | Vanilla conditions; `MISSION_TRANSITION kind=finalize` | Replays `ReserveMissionClear` with the token; local results; clear credit goes to the co-op save |
| Guest meets a clear condition | `REQ_MISSION clear_request` → notice, or per-mission allow-list | Waits |

**Until W4 lands:** any death, game over or continue disables world sync for that epoch. Both players redeploy by hand.

#### 9.5 Allow-lists and story missions

- A per-mission table lists the sequence pairs and trap names the host accepts from `REQ_SEQUENCE`. It starts empty.
- 10070 (`Reload`) needs re-arming in `OnReload`. 10010 and 10280 stay host-solo.
- Sequence replay can start demos on the guest that warp the guest's player. This is audited per mission.

### 10. Phased plan

**Lab:** one PC, `MGS_TPP` (host) and `MGS_TPP_guest`, windowed, loopback, `Play-OfflineCoop`. The host uses keyboard and mouse, the guest a gamepad. Current launchers and `lab.ini` accept `world=0..3`; `world=0` keeps partner-only behaviour, and level 3 includes the implemented W3/W4 subset. W0-W6 below are development phases, not available runtime levels. At `-World 3`, console `[G]` sends the goto to the host only; the guest follows host travel. Integrated W3/W4 live acceptance remains pending.

**Probes:** `world_probe=<name>` runs one Lua probe 10 s after `OnMissionGameStart` and logs through `WORLD_LOG`.

Confidence: H = direct static evidence, M = plausible, L = unknown.

#### W0: plumbing, no gameplay change

- **Work:**
  - The receipts and the two native queues.
  - `HELLO`, epoch, `RECEIVED` / `REJECT`.
  - Name maps and roster digest.
  - The observer, run after the original and logging only.
  - The probe runner.
- **Acceptance:**
  1. Both PCs in 30050 exchange `HELLO`.
  2. Over 10 minutes, each observed `Dead`, `Neutralize` or `ChangePhase` is logged exactly once, including messages that were resent.
  3. Both PCs deploy to 30010 by hand and the `rosterDigest` values are equal.
  4. With `world=0` the logs are byte-identical to today's apart from receipts.
- **Confidence:** H.

#### W1: shared enemies and alert in free roam (both PCs deployed to 30010 by hand; no partner body needed)

- **Work:** `ENEMY_OUTCOME` / `REQ_OUTCOME`, the apply rule, attribution, the pending ledger, `PHASE` / `REQ_PHASE` (escalation only), `GUEST_DIGEST` / `DIGEST`, the out-of-area union, and `RESYNC`.
- **Acceptance:**
  1. Host kills A. The guest's A reads back DEAD within 1 s, with exactly one guest-side `Dead` log.
  2. Guest tranquilises B. The host's B is asleep, `rid` is answered, and the guest's B gets no second `ChangeLifeState`.
  3. Guest fultons C. The host's `IsRecovered(C)` is true, and the guest's staff count goes up by exactly 1.
  4. `world_probe=race_kill`: at a native-scheduled tick, both PCs fire a guest-caused and a host-caused kill on D. Result: one commit, and the other side gets `REJECT` with DEAD.
  5. Guest is spotted: the host CP goes to ALERT. While the guest keeps fighting for 60 s, the host CP never drops below the guest's phase and there is no flapping (fewer than 2 `REQ_PHASE` per CP per minute).
  6. `world_probe=remote_area_kill`: a kill in an area that is out of area on the other PC waits in the ledger and is applied when that area activates.
- **Confidence:**
  - Message and command names: H.
  - `ChangeLifeState` with constants: H.
  - `SetPhase` takes effect: M (P1).
  - Applying to an unrealized soldier: L (P4).

#### W2: clock and weather

- **Work:** `CLOCK`, `WEATHER`, digest correction with the 60 game-second threshold, and the weather pause re-asserted every digest.
- **Acceptance:**
  1. Over 10 minutes, the clock difference stays at or below 60 game seconds, including across a host cigar time skip.
  2. Host forces rain (`TppWeather.ForceRequestWeather`); the guest has rain within the interpolation time and keeps it.
- **Confidence:** H for the setters (`vars.clock`, `WeatherManager.RequestWeather`).

#### W3: following and partner in any mission

- **Work:** the transition choke points, `MISSION_TRANSITION` / `SEEDS` / `READY`, guest suppression (7.3), `PROGRESS_SYNC` in the heli space, the partner capability table, and the f30010 and f30020 packs.
- **Acceptance:**
  1. Host goes 30050 → 40050 → 30010. The guest follows with no input, and the logs match on `missionCode`, route and deploy time.
  2. In 30010, both partner bodies appear when the players are near, with no latch when they are far apart.
  3. The guest crosses the free-roam boundary: nothing happens locally, and the host logs `REQ_MISSION exit_area`.
  4. The digests match.
  5. Five soldiers' `GetFaceId` values match (P2).
  6. P14 trap probe passes.
- **Confidence:**
  - Choke-point detection: H.
  - Guest `OnEndMissionPreparation` replay: M (P16).
  - Scarf pack: M.

#### W4: death, game over, checkpoint, continue

- **Work:** `REQ_GAME_OVER` / `GAME_OVER`, the guest's death and menu wrappers, `CHECKPOINT` and `CHECKPOINT_SNAPSHOT`, and continue and restart.
- **Acceptance:**
  1. Guest dies. Both PCs show game over. Host chooses Continue, which goes through `GameOverContinue → ExecuteContinueFromCheckPoint`. Both reload to the same checkpoint and epoch.
  2. After a host checkpoint, then a guest kill, then game over, then continue, the dead-soldier list equals the host's checkpoint list on both PCs.
  3. A guest menu choice does nothing.
- **Confidence:**
  - Detection: H.
  - Snapshot write: L (P6).
  - Ledger fallback: M.

#### W5: story missions

- **Work:** `SEQUENCE` (polled), `SVARS_DIFF` with checksum, `OBJECTIVE`, clear follow, allow-lists starting with 10033, `AddDamage` credit (P9), gimmicks (P10), hold-up (P8), heli ride (P15).
- **Acceptance:**
  1. In 10033, the guest's objectives and sequence match at each step.
  2. A target killed by the guest advances the host's sequence.
  3. Host clears: both see results, the guest's co-op save shows 10033 cleared, and both land at the ACC. `PROGRESS_SYNC` is applied there, not before.

#### W6: transform telemetry and correction

- **Work:** ownership, `SOLDIER_SNAP`, divergence telemetry, then correction (P7 or the route sets).
- **Acceptance:**
  1. Under 6 KB/s at a full outpost.
  2. Position reads cost under 0.5 ms per frame, measured against `world=5`.
  3. A soldier forced off its route on the host is re-routed on the guest while the guest is more than 80 m away.

Divergence over 2 minutes is reported, not gated.

### Probe index

| Id | Question | Probe | Phase |
|---|---|---|---|
| P1 | Does `SetPhase{phase}` apply at once and emit `ChangePhase`? | One CP; `GetPhase` the next frame and at +2 s | W1 |
| P2 | Do the seeds give identical faces? | 5 × `GetFaceId` on both PCs | W3 |
| P3 / P3b | Log the `NPC_LIFE_STATE_*` constants; `ChangeLifeState` FAINT readback; does `RecoveryLife` wake? | One soldier | W1 |
| P4 | Outcome on an out-of-area soldier | Apply, re-activate, read back | W1 |
| P5 | `RequestNotice{type, targetId, sourceId}` | One call | W5 |
| P6 | Snapshot written before `RestoreOnContinueFromCheckPoint2` survives | Change one `solState`, continue, read back | W4 |
| P7 | Mid-game `StoreToSVars` harmless? (off the critical path) | Call, continue, compare | W6 |
| P8 | A hold-up setter | Static scan; `SetPuppet` test | W5 |
| P9 | Do mission `Dead` handlers fire for applied deaths, with `ownerGameObjectId` set to the local player vs NULL? | 10033 target | W5 |
| P10 | Gimmick break from script | Static scan, then one dish | W5 |
| P11 | Cost of `SetForceRealize` | 10 soldiers | W6 |
| P12 | Offline `TppNetworkUtil.IsHost()` and `GetSyncMissionStatus()` (used by `OnPlayerDead` and the checkpoint check) | One log line per PC | W0 |
| P14 | Does the partner body fire Trap `Enter`? Does the filter stop it? | 30050: log Trap messages whose entering id is not the local player | W3 |
| P15 | Guest heli ride when the clear is refused | Board, refuse, observe | W5 |
| P16 | Guest `OnEndMissionPreparation` outside the preparation UI | Replay once | W3 |
| P17 | Does `TppMain.OnUpdate` tick during the pause menu? (sizes the queues) | Frame log while paused | W0 |

### 11. User decisions

- **D1.** Should a fulton recruit into both saves (default) or only the fultoner's? Mission targets always use `RequestForceFulton` on the host.
- **D3.** Should any player's death be a shared game over (default; the only working offline option today), or should there be offline respawn later (a new design is needed; `respawn.lua` is session-only)?

### 12. Will not do

- The synchronizer, `sync=true` propagation, or changing `SVarsIsSynchronized` / `TppNetworkUtil.IsHost`.
- `S_IS_ONLINE`, sessions, `SetIgnoreCoop`.
- The game's Steam P2P, the master server, login or version checks, fuse.
- Worker-thread or AI-job hooks, the 0x1E02770 detour, per-thread redirection, or native calls into game functions for world sync.
- Per-frame soldier transform driving (the enemy sync plan's M4 / M7 would need the user's go-ahead). Single corrections through the engine's own set-transform message are 7.7 / 7.8.
- Hooking `ActionControllerImpl` / `LifeControllerImpl`.
- Dynamite's FOB-route native patches.
- Deterministic lockstep.
- Copying MB management, staff or resources.
- Writing progress into a guest save that is not redirected.
