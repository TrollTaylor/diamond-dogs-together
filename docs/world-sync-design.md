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
- **Round 1 runs** (play, `-WanSim bad`, research build, native gun profile): 30010 `3e01f334` (276/277 applied, 0 anomalies, place error p50 2.6 m / p90 8.8 m), `2b0b77ea` (320/320 applied, p50 1.74 m / p90 6.89 m over 1099 samples, place-first 3/3 within 2 m, `d_m` 0.0-0.1 m for standing outcomes, 0 `not_attributed`, 69 commits all applied, no latch or fault), `51f8e90f` (the first crossfire in a game: the guest's copy of 1740692850 died `att=other`, the host logged `event=terminal_win; ...; partner_m=17.3` and committed it `actor=guest`; the guest's shot of the sleeping 2961503 committed `actor=guest`, applied on the host 2.9 m from the guest's copy), `e6011c22` (253/253 applied, p50 1.99 m / p90 7.65 m, place-first 4/4, `d_m` <= 0.4 m), `c2084e88` (with the lift below: 151/151 applied, 0 copies under the ground, p50 2.91 m / p90 9.91 m over 347 samples, place-first 4/4, `d_m` 0.0 m); 30050 `ee4cc176` (the key named; every gate: p50 1.14 m / p90 3.94 m), `942ff515` (the key not named: `soldier_warp=ready; ...; follow=1` from the gun profile; every gate: p50 1.0 m / p90 3.74 m, 129 commits applied), `25098847` (with the lift: p50 1.03 m / p90 4.0 m, 76/76 applied, one staff copy 3.1 m under its request 2 s after landing). Every 30010 run had the outpost in ALERT from the arrival (`ChangePhase` 3 right after the players were placed by its guards, enemy ignore notwithstanding), so its p90 gate (5 m) failed on running guards (the frozen-guard limit below), mostly while the players stood apart; while both stood by one guard (100-200 s of `2b0b77ea`, still ALERT) 77 % of 416 samples were under 3 m and 91 % under 5 m. The guest's hold-E Fulton stimulus (30050 B, 30010 D) never showed a Fulton prompt on the worker in three runs; 30010 D now shoots.
- **Copies under the ground** (runs `51f8e90f`, `6e22bed0`, `e6011c22`): the place error compared x, z only, and some corrected copies were falling through the world: placed with their feet on the host copy's (GetPosition y - 800 mm), 200 of 253 corrections of `e6011c22` landed with GetPosition 0.80 m over the request (standing) but 31 at 0.0 m or lower, and 12 of those were about 20 m lower 2 s later (free fall); a falling copy re-corrected kept its fall speed (75, 117, 164 m), a sleeping copy was read at y -80 m (the guest's goto followed it there: 41 `far` refusals for the rest of `51f8e90f`). A correction now puts the feet `P.LIFT` (400 mm) over the host copy's: `c2084e88` 0 of 151 under the ground (every landing 0.3-0.4 m over the request), `25098847` 1 of 76 (a Mother Base staff copy 3.1 m lower 2 s later, a deck below). Every applied correction logs `dy_m`; `event=place_sink` (1.5 m lower 2 s after landing), `why=fall` (a sample 10 m under the host copy: corrected at once) and `event=body_fall` (a body this copy downed 10 m lower 3 s after) are never-lines of `enemy_div_report.py`.
- **Known limits** (M3 to M7 below take them on): a corrected guard in combat stands still until his next correction while the host's copy runs on, so a running guard hops every 1-1.5 s off screen and every 1.5 s or more on screen (glides are M4, matching routes M5); a guard 15 m nearer the guest than the host is not corrected (two-way follow is M3); the host's copy of a guard the guest fights alone stands wherever the host's AI has it; the guest's own kills land where the host's copy stands (place first on the host is M3).

#### 7.9 Enemy sync M3: two-way follow (lab key `soldier_owner`, CFG r[19] bit 2)

The guards the guest fights far from the host stand in the same place on the host's screen too.

- **Ownership** (host, `world_place.lua` `P.ownerOf`, at each follow-scan read): the guest owns a guard that is at least 15 m nearer the guest's player than the host's. He goes back to the host when he is less than 5 m nearer the guest, or within 20 m of the host's player (a hysteresis band). A decision holds 10 s. OWNER (0x49) tells the guest each change (`event=owner; owner; why=near|host_near|none; dh_m; dq_m`). Engagement is not used: in run `4ad309a3` the Damage messages named the local player as the attacker of guards neither player shot (81 ENGAGE, 102 owner flips within 10 s). ENGAGE (0x4A) stays on the wire, and the host only logs it.
- **The guest's stream** (`world_place_guest.lua` `P.ownScan`): it reads its owned NORMAL guards within 60 m of its player (4 per update, every 500 ms within 40 m, 1 s beyond) and sends ENEMY_PLACE (0x47, ENEMY_DEBUG's shape, kind 3, the feet y plus the lift) when one moved 1 m or every 2 s. A place the host refused (`far`, `native_off`, `latched`) backs that guard off for 10 s.
- **The host's placements**: the host's Lua pops ENEMY_PLACE, and native records the host's warp grant (`Core::RecordGrant`; each role records only its own role's grants). The sample goes through the guest's own sampler and FIFO (`P.sample` with `c.host`: the host's view, never within 3 m of the host's player). Native: `HostWarpReceipt` (`WORLD_WARP_HOST_V1 "<seq> <goId>"`), the third `soldier_warp::Warp` caller. Its target comes only from the grant. `HostAdmit` admits play or lab with soldier_follow + soldier_owner, in 30010 / 30020 / 30050, corrections only. The range is measured from the nearer of the two players, with `far` beyond 250 m, and the same gate, budget and latches apply. The result is logged as `event=hplace` and answered with ENEMY_PLACE_ACK (0x48).
- **The guest's place-first** (`W.guestPlaceAnnounce`): right before its own REQ_OUTCOME of a guard it owns, the guest sends ENEMY_PLACE with flag 4 (the standing sample). The host places its copy from 1 m, but only while the host's player is more than 40 m away or the copies are under 3 m apart (`why=visible` otherwise). The host's apply of that outcome (src commit) waits for it for at most 1.5 s (`W.placeHoldHost`).
- **Calm-down**: a host phase decrease goes out as PHASE flag 2 (`src=down`). The guest lowers its CP unless its own fight raised it in the last 5 s (`applied=0`).
- **Late join**: M1's pairing burst (`why=pair`), within 2 s.
- **Runs** (play, `-WanSim bad`): `4ad309a3` (owner flips, range refusals: fixed in `bcf16f2c`); `d4460ea5` and `8ee31261`: 8-9 owner changes, 0 fast flips, 42-55 host placements all applied, the calm-down applied on the guest; host place error for guest-owned guards p50 1.5-1.6 m / p90 6.1-6.4 m (M1's frozen-guard limit, before M4). The guest's own shot never registered: the outpost alerts on arrival and its guards down both players (the guest place-first is unit-tested only).

#### 7.10 Enemy sync M4: glides, not hops (lab key `soldier_snap`, CFG r[19] bit 4)

- **SOLDIER_SNAP** (0x4B, both directions, a 25-byte datagram): the owner's sample of one guard. It carries the sender tick, the roster slot (W.entOrder index), x/y/z in mm (ENEMY_DEBUG kind 3's y), yaw in mrad, flags (1 alert, 2 moving) and speed in mm/s. The producer (`world_snap.lua` `S.produce`) reads at most 6 per update of the NORMAL guards this side owns within 60 m of the other player: 10 Hz within 30 m or in an ALERT CP, 5 Hz to 60 m. `world::Core` keeps the newest two per slot by sender tick (`StoreSnap`, `SnapPair`; a late older one counts as `stale_u`). They never enter Lua's queues and are cleared with the grants.
- **The soft follow** (`S.drive`): one DRIVE_V1 (`"<slot> <goId> <x_mm> <y_mm> <z_mm>"`, this copy's own GetPosition) for each of up to 8 remote-owned NORMAL guards within 60 m per update. Native (`DriveReceipt`, the fourth `Warp` caller) takes the target from the slot's two snaps only: 100 ms behind, extrapolated at most 250 ms, stale after 1 s (`DriveTarget`). It steps from this copy (`DriveStep`): at rest under 1.5 m, then at most 0.4 m plus the owner's speed for 100 ms every 100 ms until under 0.3 m, and the whole way beyond 8 m. The y is this copy's own feet + 0.1 m unless the target is more than 1 m off them. The step stays within 60 m of the nearer player. `DriveAdmit` admits play or lab with soldier_follow + soldier_snap (a host only with soldier_owner), in 30010 / 30020 / 30050. `Gate::AdmitDrive` has no command budget, but the per-guard and process anomaly latches apply. `world_drive=stats` is logged every 10 s and `event=snap_stats` from Lua. While a guard is driven, M1 / M3 corrections leave him alone (`why=driven`).
- **Runs**: `2d967b8a` (with M5 and M6): place error p50 0.92 m / p90 2.5 m over 659 samples (M1 alone in the same scenario: 1.74 / 6.89), host side for the guest's guards p50 0.95 / p90 2.84. 1307 steps, 2 hard snaps, 0 anomalies; M1 corrections fell to 29 (the drive does the rest). `441b490c` was cut short by a frame capture error. Mother Base: see STATUS.
- **Limits**: a guard glides back with its own animation (a guard the AI keeps still slides). Runs `2d967b8a` and `ef7e9088` saw 1-2 copies 3-4.5 m lower 2 s after a placement (`place_sink`: a ledge or platform drop, not through the world); the cause and the fix are in 7.14.

#### 7.11 Enemy sync M5: the same route (lab key `soldier_route`, CFG r[19] bit 8)

- The host reads which route each of its guards walks: `GetGameObjectIdUsingRoute` on every route of the mission's route sets (`P.route`'s names), 6 per update. When a guard near the guest (60 m) that the host owns changes route, the host sends ENEMY_DEBUG kind 2 with flag 16 (route only, the route hash, no placement, not kept for an ACK; `event=route_tx`).
- The guest's copy takes it (`P.routeSync`) with `SetSneakRoute` without a point. When the table has no point, the soldier dispatcher's handler (0x135e58d) passes the local that 0x1356238 initialises to 0xFFFF; this is static reading only, and the in-game meaning of that point is probe P8. The guest skips it when `IsOnRoute` says the copy already walks it, and takes it again right after each landed correction (`why=landed`; `event=route_apply`).
- **Run** `2d967b8a`: 3 routes sent and taken (`on=false; ok=1`). Whether the copies then walk in step is not measured (no route-churn or walk-cycle frames yet).

#### 7.12 Enemy sync M6: bodies and hold-ups (lab key `soldier_body`, CFG r[19] bit 16)

- **Bodies**: each side reads the lying bodies (sleep .. dead) within 60 m of its own player (2 per update). A body at rest (under 0.2 m in 1.5 s) more than 1.5 m from where it last lay was moved there. The other copy then places its own body (host: ENEMY_DEBUG kind 3 flag 8; guest: ENEMY_PLACE flag 8; 0.2 m above it; `event=body_tx`). The receiver queues its lying body when it is more than 1 m off (`event=place_body`), through the same FIFO and native placement; a dead body may be placed (`c.body`).
- **Hold-ups**: a hold-up applied here (W1 state 1) also sends `SetForceHoldup` to this copy once (`event=holdup_show`). The release is not shown (no command known).
- **The ping-pong** (enemy latch track): a dead guard reads NPC_STATE_DISABLE on both copies, so each refused the other's body placement (`disabled_local`), while `P.body` had already moved its resting spot to the sender's: its own body then looked carried and was sent back, about once a second all session (`e583ee62` 384 / 383, `5526d92a` 289). `P.body` now runs `P.check` first; refused, nothing is queued (`place_body ...; fix=0; why=...`) and the resting spot stays where this copy's body lies. Run `41f2b07f`: 2 body_tx, 1 refused (dead 1179373153, 6.8 m apart), no echo. The two copies of a dead guard stay apart (whether set-transform moves a corpse is a probe for later).
- **Probe P9** (run `2d967b8a`): the native set-transform moves a lying body (`host_warp r=0 h=1`, landing 0.02 m). The guest's dying guards had crawled 9-10 m, and the host placed its bodies there. A carried body is not hidden while carried; it moves once it is dropped and at rest.

#### 7.13 Enemy sync M7: the puppet bubble (lab key `soldier_puppet`, CFG r[19] bit 32; probes only)

The plan's own section 9 recommendation is to run M7's probes behind a switch for now.

- **The probe**: on the guest, the driven copies of the host's guards become puppets: `SetPuppet {enabled=true}` and `SetRestrictNotice {enabled=true}`, applied once per change (`event=puppet`). They are driven every update (DRIVE_V1 mode 1: every 30 ms from 5 cm, at most 0.2 m plus the owner's speed for 50 ms per step; `DriveAdmit` refuses mode 1 without soldier_puppet or on a host). Outside 60 m, off NORMAL, after 3 s without a drive result, or at unpairing, they are set back.
- **Run `ef7e9088`**: place (puppet) error p50 0.63 m / p90 1.93 m over 739 samples, 8524 steps, 3 hard, 0 anomalies. The puppets stand in their idle poses on the guest's screen (no T-pose). Placements and corrections of a puppet are refused `disabled_local`: GetStatus answers DISABLE.
- **P11 at runtime (wp/enemy-m7, STATUS 2026-11-02)**: the scorer 0x141325530 behind the patched filter is never called in free roam (census run af17b899: 0 calls while soldiers fire); the live readers of the offense player index 0x141E02770 are 0xD5AF51, 0xD65320, 0xD7BECC, 0x1350035, 0xD782E3, 0xD6638E, 0xD6D9CE, 0xD6D82F, 0xD6B87F (notice gauge 0x140D6B7E0), 0xD59430, 0x14DCA77 (run 32a93d27). Forcing the getter to 1 on the host (P11b, run 65e2440a) stopped the host's soldiers hitting the host but gave no hit on body 1. Perception of both players needs the index chosen per asking soldier; the scorer patch is a no-op.
- **Step 1, per-soldier perception (lab key `soldier_perceive`, CFG r[19] bit 256 since the wp/enemy-gaps merge, 128 before; wp/enemy-m7, STATUS 2026-11-04)**: `soldier_perceive.hpp` `InstallPick` hooks the getter through a register-saving stub (`Choose`: a forced index, per-soldier targets by the caller's soldier register, the leaf snapshot mode) and the notice system's per-slot update 0x140D65850; the host's Lua sends each sampled guard's nearer player (3 m band, 4 s hold; `MGS_COOP_SOLDIER_PERCEIVE_V1`). Runs 96216259, 37c9604c, 183e2cf7 and 7e658437 show why it cannot deliver the plan's goal as it stands: the notice system is per CP (77 slots; slot 45 = afgh_field_cp, its index list the CP's guards), so the six notice-system "per-soldier" getter callers (0xD6638E, 0xD6B87F, 0xD782E3, 0xD6D82F, 0xD6D9CE, 0xD56834) are CP-level; the soldier leaf reads its own once-a-frame snapshot of the index ([leaf+0x28c], +0x1d0, +0x294) in its per-soldier code; and with every caller and the leaf forced to 1 and body 1's player record state equal to the host's, the guards stop seeing the host but never notice body 1 standing among them. Seeing body 1 needs something outside the index ([I] its registration as a soldier sensor target: probe P12f), then a per-soldier swap of the leaf snapshot. Two live callers do carry per-guard values: 0xD59430 (a CP knowledge copy, r12 = 176..272 in all four runs) and 0x14DCA77 (rdi = 13..246, afgh_field_cp's own guards, on the guest in 96216259; 18 in 7e658437), so "no per-soldier caller in free roam" does not hold as stated [U: whether either decides a guard's target]; the rest is census and research commands.
- **Step 1, P12f/P13 and the told sight (lab key `soldier_perceive`, CFG r[19] bit 256; wp/enemy-m7, STATUS 2026-11-04 "told sight")**:
  - **What the guards' senses do not take** [O]: four hidden sessions on afgh_field_cp (`29e389d7`, `0608ad9f`, `dbba96de`, `d7cb1b1d`). With the offense index forced to 1, guards ignore the host 11-29 m away in the open and never notice body 1 among them. This holds when the soldier and notice code also read body 1 through the player-info service's LOCAL accessors (0x14053C9D0 record, 0x14053C990 index, `perceive_local 1`), when every caller of those two reads body 1 (`perceive_local all`), and when body 1's only extra status bit, 138 (+0xD1 bit 2), is cleared before each leaf snapshot. The game sets 138 again every frame, so that clear never really held [U]. All CP notice masks stayed 0. Setting the soldier manager's online bit (`perceive_online`, L4) stalled the host. [I] Body 1 never becomes a candidate for the soldiers' own sight; the sight check itself is still not located.
  - **What does work** [O]: a told discovery. `SetDiscoveryPlayer` (debug `enemy come`) makes the guard discover the offense player. In `dbba96de`, with the index forced to 1, the told guard 1740692850 (index 15) and guard 246 fired 45 times at body 1, the CP went to ALERT (`src=host`), and the host 15 m away was not hit. With the index stock, a told guard went for the host.
  - **The told sight** (host Lua, `world_snap.lua`): there is one offense player for every guard. It is the nearer player of the nearest guard sampled in the last 2 s, held at least 4 s (`S.nearest`, `S.offense`). Native answers it as the getter's forced index (`MGS_COOP_SOLDIER_PERCEIVE_V1 -- offense <0|1|off>`); clear and mission end drop it. A calm guard (CP not in ALERT) whose pick is body 1 and that stays within 10 m of the guest for 0.8 s is told `SetDiscoveryPlayer`: the offense switches to 1 first, and each guard is told at most once per 30 s.
  - **Run `d7cb1b1d`** (the proof session) [O]: About 2 s after the guest's warp put it 7.5 m from guard 1740692850, that guard was told (`event=perceive_tell`). The CP went to ALERT 3.8 s later. The guest's copy received the ALERT 118 ms after that and released its puppets. 124 hits on body 1 by seven guards followed (13, 14, 15, 18, 219, 231, 246). The host was also hit 40 times in the same 25 s, because the offense player flapped every 4.2-8.6 s: the first version took a 1 s rolling minimum, and a host-side sample reset it. The 2 s per-guard window replaces it (Lua test), but no run has shown it yet [U]. With the guest 55 m away, only the host was hit (37, then 91). After the guest came back, the guards kept hitting the host (23, then 68) and never body 1 again: in ALERT nobody is told again, and an offense switch alone does not bring body 1 back [I].
  - **Why the gate is not reached**: every guard follows the same offense player. The getter callers that run in combat are frame-level (0xD5AF51, 0xD65320, 0xD7BECC, the leaf snapshot 0x1350035) or carry the CP slot (0xD6B87F, 0xD6D82F, 0xD6D9CE, 0xD6638E, 0xD782E3). None carries the guard (0xD59430 r12 is rare). So "different guards pick different players" needs the leaf snapshot swapped per guard inside each soldier subsystem that reads it. The leaf calls about 20 of them in turn, and that swap is not built. The sight is distance-only, with no view cone or line of sight: a guard 10 m away notices the guest through a wall.
- **Not done**: the host's perception of the guest's body (P11; step 1 above: built, the gate not reached). The soldier target filter at 0x1325629 compares the target's player index with the local player's and is not patched (unverified at runtime). Also not done: damage rerouting (P12), soldier bullets on the guest's copy (P13) and the act mirror. So inside the bubble no guard notices or shoots the guest, while on the host's screen the same guards shoot the host. The probe can never become a default as it stands.

#### 7.14 Enemy gaps: the placement height and the slot drift (lab keys `place_lift`, CFG r[19] bit 128; `partner_native_slot_rearm`)

- **Warp's y is GetPosition space** [O]: every correction's readback 2 frames after landing (`dy_m`) was either +0.40 (the copy stood at the owner's own GetPosition y, the request being 0.4 m under it: LIFT 400) or -0.01 (left at the request, falling: about 1 in 100). Three of the four archived `place_sink` events started with a falling readback. The `y - 800 + 400` rule put the copy's own GetPosition 0.4 m under the owner's, so the engine had to lift it onto the floor; on a thin floor it sometimes did not and the copy dropped a storey (2.3-4.5 m). The soft follow asked for the copy's own GetPosition y - 0.7 m on every level step.
- **`place_lift`** (follows the gun profile again since the enemy latch track, STATUS 2026-11-04; it was off unless named after `e583ee62` latched the warp off, which the A/B below shows it did not cause): `P.LIFT` 800, so corrections, place-firsts, the guest's ENEMY_PLACE and the SOLDIER_SNAP stream carry the owner's own GetPosition y; `DriveStep`'s level y is the copy's own GetPosition y + 0.02 m (`kLiftFeetM` 0, `kLiftLevelM`) unless the target is more than 1 m off it.
- **Measurement** (no key): both roles read a placed copy every tick 2 s after landing (the host's check ran only while a placement was queued: 3.3 s and 8 s in `d4460ea5` / `2d967b8a`). More than 1.5 m under its landing is `event=place_sink` with `owner_dy_m` (vs the owner's newest sample, at most 3 s old) and `moved_m`; level with the owner (`owner_dy_m` over -1.5) it is `event=place_drop`. The drive reply carries `v` (the copy's y minus the target's, cm); a driven copy 1.5 m under its owner for 2 s is `event=drive_sink` (`snap_stats ... sink=`). `enemy_div_report.py` counts `guard_sank`, `drive_sank` and `place_drop` on both roles (its landing histogram was dropped: the pattern never matched the logged `dy_m`, so landings are counted by hand).
- **Runs** (play, `-WanSim bad`, the gun profile, `place_lift=1`): `7c174346`, `0b4a1509`, `3e2611fc`: 124 of 127 applied corrections and host placements read back within 0.1 m (three 0.2-0.8 m over, one of them a body placement; counted by hand), 0 `place_sink`, 0 `drive_sink`, 1 `place_drop` (a guest-owned guard in ALERT whose owner copy went 1.7 m down too); place error p50 0.95 / 1.01 / 0.73 m, p90 2.29 / 2.82 / 1.87 m (the default run `9a44af58`: 0.71 / 1.88); drive 31-132 steps, 0 anomalies, hard snaps host 1 in `7c174346` and guest 5 / 4 / 5; `7c174346` fails the M3 host-side p90 gate (5.93 m).
- **The arrival alert** [O] run `7c174346`: the host alone at (421.16, 2084.42), 50 m south of afgh_field_cp, stayed quiet; at (415.8, 2106.9) patroller 1926520947 came within 10 m and the CP's ALERT followed (ChangePhase att=none) at the moment the host's clock left its fast mode (`clock_tx; cause=fast_end`, 2 ms later). [I] The clock anomaly is reflex mode after a guard spotted a player, and the ALERT comes when it ends: the scout's "arrival alert at fast_end" in 21 archived runs is a guard seeing a player next to him after a warp. Restrict notice (`enemy ignore`, applied on both copies) did not stop it at 3-8 m (`0b4a1509`, `3e2611fc`: the guest's CP alerted, `src=req`, on its goto next to a guard).
- **The firefight latch** (enemy latch track, STATUS 2026-11-04) [O static, Ghidra + capstone]: every soldier warp goes SendCommand -> the soldier handle vtable 0x23063f8 +0x10 = 0xbf5450 -> (handle+0x20)->vt[0x70] = the soldier type dispatcher 0x9e9ab0 (every census line: `hop=0xbf5450`). 0x9e9ab0 calls the handler 0x1360f90 (answers out 0 with handled 1, or out -1 when the guard is not realized or has no character) and, on -1 for a guard whose slot is allocated, relays the message through 0xbf6570 -> 0xbf6490, which writes *out = 0 first and offers it to each component; the set-transform handler is not reached again, so handled stays 0 and nothing moves. The relay starts from the guard's unrealized form (`[leaf+0x58]->vt[0x10]`, realize bit clear, 0x9e9bc9) OR, when the realize bit is set, from `leaf->vt[0xf0](idx)->vt[0x60]` (0x9e9b9e / 0x9e9bb4; a null vt[0xf0] answers -1). Correction at the landing review (decompile and capstone, read-only): the first text here said out=0 handled=0 means "not realized on this copy" [I]; both branches end in 0xbf6490, so it means "the handler refused a guard whose slot exists on this copy" [O static], and whether that guard was realized is [U] (no realize read before or after the send). The relayed guards of the proof runs were M3 copies 40-140 m from the copy's own player, so "not loaded there" stays the likely case [I]. The answer is independent of y. The relay contract now pins the realized branch too (rows 0x9e9b9e, 0x9e9bb4). A/B at dev d836a1c7, the same long ALERT (`enemy-latch-alert.txt`: the guest from guard to guard inside afgh_field_cp, the host 50 m south): arm A shipped defaults (`6348f361`) and arm B `place_lift=1` (`5526d92a`) both latched the host's process after 5 such answers on M3 host copies of guest-owned guards (A 127.8 s, B 229 s into the ALERT); A also latched one guest guard. `tools/generate_soldier_relay_contract.py` pins the chain (4 sites, 3 slots; `soldier_warp::Verify` checks them); `Chain::typed` marks it (census `leaf=typed`), and on it out=0 handled=0 (or the handle's mode-2 0x8ce64801) is refusal `relayed`, never an anomaly. The gate counts no relay toward a latch: only that guard backs off (3 s, doubled per relay in a row up to 30 s, a move clears it; `Admit` / `AdmitDrive` refuse `backoff` unsent; `soldier_warp=relayed; ...; streak; back_ms`). Unexplained answers keep the guard latch (2) and the process latch (5), a fault still latches at once. Lua maps both to WHY 23 `unrealized`; the sender backs that guard off 3 s. Not done: a pre-send realize read (the leaf comes from a component lookup call, `vt[0x18](0x3fef1d0298aa)`), and force-realizing far copies.
- **The proof** (play, `-WanSim bad`, gun profile, research build, afgh-ledge, the defaults incl. `place_lift`): `41f2b07f` (join, a quiet part with a sleep and a kill, then the 4.5-minute ALERT): 0 latches, census `leaf=typed` on both copies, but 0 relays; place error p50 0.63 / p90 1.61 m over 88 samples (before the ALERT 0.53 / 0.95, the quiet part 0.50 / 0.90); the sleep and the kill applied 0.2 / 0.1 m from the guard, the ALERT's outcomes 0.0-1.8 m; 260 of 263 landings within 0.1 m; one `place_sink` (2929558510, landed 0.49 m over, then 5.05 m under its owner) and one `drive_sink`. `31a799e3` (the A/B stimulus on the fix build): 17 relays on the host (all M3 grants, streaks up to 4), 15 `backoff` refusals, 32 host placements refused `unrealized` (the guest's ACKs decode WHY 23), 0 anomalies, 0 latches; most relayed guards were moved again after their backoff (go 1043: 6 moves), but go 1264 (7 relays) was not moved again after its last relay; 190 of 191 landings within 0.1 m; guest place error p50 0.65 / p90 17.2 m (copies of host-owned guards 46-75 m off for about 25 s: far refusals, then the copy back near the guest between corrections) and M3 host p50 1.88 / p90 18.5 m (relayed host copies stay put until they realize).
- **A1, the slot drift** (`partner_native_slot_rearm`, off unless named): the fb1101 friend session's 19.8 s mismatch had a complete plan (`map=0,1,2`); after a prone/dive body 1 dropped back to slot 0 while the follower had spent its two writes of that (slot, switch) episode. `partner_slots::EpisodeStep` adds one write 750 ms after body 1 left a slot it had reached, at most 3 per episode and 2 s apart (`partner_slot=drift`). `partner_weapon=mismatch` names `remote_slot`, `body_slot` and `why=plan|slot|equip` (plan: no body slot or stand-in for that remote slot). In the runs above the late joiner's secondary (875) had no body weapon, the plan gave slot 0's 36 as its stand-in (`map=0,1,0`, `partner_weapon=standin`, the host's HUD "gun stand-in"), and every `why=slot` mismatch cleared within about 0.4 s (the switch lag; the join-time `why=plan` mismatch of 873, body equip 2047, lasted about 1-3 s); the drift case did not come up.

#### 7.15 Enemy sync M7 FM1: the far mirror (lab key `soldier_act`, CFG r[19] bit 512; research key `act_census`)

- **The engine's own act sync** [O static, Ghidra decompile, mgsvtpp 1.0.15.4]: each guard's body runs one 0x1e0-byte
  request record R (`*(M+0x10) + idx*0x1e0`) that his brain writes. The request manager M (the +0x20 sub-object of the
  object 0x14136e8d0 builds: vtable 0x1423e8a48, `M+0x50` = the soldier impl S, `M+0x58` = the action codec, vtable
  0x1423e9638) runs 0x1413735c0 every frame. In FOB (`S+0x2a4` bit 2) 0x1413738b0 has the owner pack the request
  (0x1413704a0, 0x70 bytes) and order (0x141370900, 0x18) and the other peer unpack them (0x14136f810 / 0x1413701f0, then
  `R+0x19e |= 0x80`, `R+0x1d2 |= 1`). The codec is one symmetric serializer 0x141386640 with a direction word. The unpack
  reads the network-position entry `*(*(S+0xe0)+8) + idx*0x18` only when idx < `[*(S+0xe0)+0x10]` and dereferences it
  with no null check (U1), warps the copy beyond 15 m of it (0x14136ece0, 225.0 at 0x142329c00), and runs a moving copy
  toward the packed position (gait + 1 at 2..5 m, gait 3 and that destination beyond 5 m). SetPuppet (bit 27 of dword +8
  of `*(*(S+0x110)+8) + idx*0x28`) gates only the brain tick (0x1414dc8b0). Offline a new order is marked done at once
  (0x141373c20), so a mirror re-posts it every update.
- **The far rule**: a guard is a mirror puppet on the guest only while the guest's player is beyond R of him. R per phase
  is 1.5 x the farthest first notice or engagement distance measured at the defaults (floors 80 m calm / caution, 120 m
  ALERT), capped at the realize radius minus 10 m. He is taken beyond R + 15 m and released within R, so nobody is made
  unseen who could be seen without the key. Any guest shot releases every puppet for 15 s. Superseded by 7.16 (R(ALERT) 261 m since run 94a7bf2a). At the landing of this section: 80 m calm /
  caution (the floor; no first-notice distance measured yet) and 184 m evasion / ALERT (1.5 x the farthest ALERT hit,
  122.4 m, one hit on the host in `3bc7280b`). No realize cap applies: the same 12 copies stayed realized out to 212 m
  from the host and 400 m from the guest, so the realize radius was not measured (an earlier 138 m cap rested on it).
  [I] In `0a271c10` the guest took 424 hits from guards 13 / 18 / 231 whose nearest logged approach to its spot was
  110-150 m, so ALERT engagement may reach about 150 m (R 225 m); one 333.8 m hit on the guest in `3bc7280b` by guard
  176 of another CP is [U]. The gate run must log those distances again.
- **The path** (native/soldier_act.cpp, lua/coop/world_mirror.lua, SOLDIER_ACT 0x58 in protocol section 33):
  - the host registers its own NORMAL guards within 250 m of the guest and beyond R - 10 m (`ACT_V1 t`); its hook packs
    them with the engine's pack each frame, stages a changed record (a destination 5 cm or a facing 1 degree off is
    jitter) at most every 100 ms and every 1 s, and `ACT_V1 s` sends the staged parts (at most 24 per 100 ms);
  - the guest's `ACT_V1 m` takes the newest complete request and order of the slot into the hook's mailbox; the hook
    unpacks a new request into the puppet's R with his network-position entry pre-filled with the host copy's position
    (a table that does not cover him is lent one for the call), swaps players 0 / 1 in facing ids and order target ids,
    and re-posts the order every update;
  - the puppets' own bullets and noise are dropped (`partner_fire_filter`, the `soldier_authority` mirror mask): the host
    copy's shots are the real ones; the guest's own shots are counted to release the puppets. The mask is a lease: the
    hook clears a bit 1.5 s after the last fresh `ACT_V1 m` (the apply's own lease), and `r` is always taken, so a
    puppet Lua forgets cannot keep his bullets dropped;
  - every update Lua checks each puppet for the guest within R and a guest-local phase rise in his CP, and sends one `m`
    (to the puppet asked longest ago) whose reply carries the guest's shot count, so those three releases do not wait
    for the 12-per-update round robin (~0.8 s with ~280 roster entries); a puppet whose object is gone, and every puppet
    at a mission start, is released with `r`;
  - DRIVE_V1 mode 2 (the mirror drive) never steps a puppet under 5 m and moves him the whole way beyond; a far puppet
    is outside the 60 m stream, so only the engine's 15 m rule and the catch-up keep him in place there.
- **Probe P1** (`act_census`): every 0.5 s `soldier_act_census` per realized guard within 250 m (the U1 table, S's mode,
  peer and owner bytes, R's flags, the packed request and order, gait / posture / kinds, the aim point, the weapon
  bytes), `soldier_act_payload` (U3), `soldier_engage_bins` (Defend hits on the local player per shooter with its
  distance), `soldier_act_bands` every 10 s (realized copies per 25 m band); `act_probe` (U2) puppets one guard and
  posts another's request to him on one copy.
- **Runs** (play, `-WanSim bad`, the gun profile, research build, afgh-ledge): `125d916b` (the census at the defaults:
  the hook resolved nothing, M is the +0x20 sub-object, vtable 0x1423e8a48, not 0x1423e8a18), `0a271c10` and `963ab276`
  (`soldier_act=1`, enemy-act-1-quiet + enemy-act-2-alert: 0 puppets; one clock bug, then the reply key `or`, a Lua
  reserved word, kept Lua from seeing `f=1`; both fixed after their runs, unproven), `3bc7280b` (the latch regression
  with `soldier_act=1`: 0 latches, faults or game overs, place error p50 0.76 / p90 6.8 m; but `m` sent for mere
  candidates armed the fire drop of 9 guards whose own brain ran: 582 of their bullets and 343 noises dropped on the
  guest's copy; fixed after the run: candidates are only queried, `q`). [O] U1: `*(S+0xe0)` is NULL offline; the lent
  table carried 125 unpacks in two runs with 0 faults, all on copies whose own brain ran (the candidate bug), none on a
  puppet. [O] ALERT hits came from up to 122.4 m (host, one hit; the next 58.4 m) and 82-87 m (guest, `3bc7280b`;
  `0a271c10` logged no distances); the watcher's walk south stops about 105 m from the outpost, inside that range.
  [U] U3: payloads of kinds 1 and 0x12 often hold the u16 0x0400 (the 0x400|i soldier-id form) at +0x34, kind 25 0x0564
  at +0x14 and kind 20 hash-like words; whether these are ids, and so the remap or refuse rule, is open. [U] U2, the
  frames and counts of the gate.

#### 7.16 Enemy sync M7 FM1 retry: the first puppets, R(ALERT) 261 m, the apart release (lab key `soldier_act`, off)

- **R from the gate runs** (plan rule, R = 1.5 x the farthest first notice or engagement distance per phase): in ALERT
  the guest's own copies hit it from up to 173.9 m (`94a7bf2a`; 152.0 / 152.6 m in `adecad01` / `80cc0176`), the host
  was hit from up to 121.5 m, so R(evasion / ALERT) is 261 m; R(calm / caution) stays the 80 m floor (no first notice in
  calm was seen). The far rule's range is 300 m (was 250 m: the host registers its guards within 300 m of the guest), so an ALERT
  puppet exists only 276-300 m from the guest; the census logs out to 320 m.
- **U3** (0x1413816e0, the action B kind 1 serializer): payload B (wire +0x20) of kind 1 (wire +0x64) is the aim; its
  target mode is wire +0x35 (R+0x4c & 7): 1 a packed position at B+0, 2 a big-endian u16 GameObjectId and u16 part, 3 a
  u32, 4 a u16 id. The host's aims are mode 4 at id 0 (its own player); `soldier_act::RemapAimTarget` swaps players 0 and
  1 there before the unpack, as facing ids and order target ids already were. Action A kind 1 carries 0xffff at A+4 (no
  id); kinds 0x14 / 0x16 hold 0x0402 / 0x0401 at A+0x19 [U, rare].
- **Apart**: a climb is not replayed (`94a7bf2a`: guard 19's puppet stood at a tower's foot 7 m below his host copy for
  ~100 s, gait 3), and nothing warps a copy under 15 m (the engine's FOB rule) or beyond the 60 m stream. The m reply
  now carries the owner copy's position from the newest order part (`ox/oy/oz` mm, `soldier_act::MailOwner`); a puppet
  more than 5 m (3D) from it for 2 s, after his first 4 s as a puppet, is released (`why=apart`, `event=mirror_apart`)
  and not taken again for 30 s: his own brain and the follow corrections take him back, as without the key. In the runs
  it also took walkers that lagged 5.5-12.7 m (the request is re-applied only on a change or every 5 s, so the engine's
  catch-up runs only then), copies 10-15 m off right after `mirror on`, and two vehicle riders 283-285 m off.
- **Watch tools** (research): `aim_ent <hash> [up]` turns this copy's camera to a roster guard (both copies on the same
  guard), `aim_partner [up]` to the partner's character (world sync's peer position when the command has no pose: native
  attaches the pose only to goto_partner / spawn_sync). The quiet watch point is the end of the walk south from
  (423, 269.5, 2081) (z ~2018-2026, four runs); the host's spot 50 m south of the outpost sits at a rock that hides
  bearings -30..-1 degrees.
- **Runs** (`94a7bf2a`, `adecad01`, `80cc0176` the gate scenarios; `46c0da9e` the latch regression; STATUS has the
  numbers): puppets form and run the posted requests (standing, walking at the host copy's speed: ratio p50 0.98 in
  `94a7bf2a`; worse in the final build, p50 0.20 / 0.55 by the skeptic's pairing); 0 unpack faults; releases at the ALERT
  rise, `mirror off`, the guest's shot within one update; matched frames of a mirrored guard standing at the same spot
  on both screens (`80cc0176` q-0, q-30), none walking. In ALERT: with R(ALERT) 184 m (`94a7bf2a`) three puppets of the
  alerted CP existed 184-212 m out (two taken in ALERT, one held from calm until a `stale` release ~29 s in), none
  fired, none in a frame; with 261 m (`adecad01`, `80cc0176`) every take was at R 80 m (calm) and none existed in ALERT.
  The gate is not reached; `soldier_act` stays off.
- **Next**: decision D-FM1 (calm / caution only, or a watch point beyond 276 m for the ALERT half); a host spot with a
  clear view of a walking puppet; moving puppets re-applied every 1 s; apart only for a still owner copy; skip riders.

#### 7.17 Enemy gaps 2: the stream's reach, the realize hold, dead bodies (lab keys `place_reach`, CFG r[19] bit 2048; `body_dead`, bit 1024)

- **Causes** (archived logs of `798b51dd`, `41f2b07f`): every M3 host place sample of the ALERT part lay 60-160 m from
  the host's player, beyond the M4 stream (the owner streamed its guards within 60 m of the other player, the receiver
  drove within 60 m of its own), so the host's copies of the guards the guest fought moved only by 1 s corrections: 157
  of the 214 samples over 5 m (p90 6.97 m); 57 of the 58 refused host placements were `unrealized` (relays 120-160 m
  from the host). The LRRP riders (3970963356, 2305481811: in the roster from the start, 435.9 m apart at the join):
  11 s `far` (the guest's copy in its truck more than 250 m from both players), 17 s `vehicle_local` (riding on the
  guest's screen, on foot on the host's), then 48 s `unrealized` after it got out. A dead guard 6.8 m apart: the
  guest's corpse lay 7 m from where it died 63 s later; the host refused the body placement (`disabled_local`: a dead
  guard is NPC_STATE_DISABLE).
- **place_reach** (off unless named; no matched frame yet):
  - The M4 stream reaches 160 m (`world_place.lua` `P.reach`): the owner streams a guard within 160 m of the other
    player when he stands within 60 m of its own (4 Hz beyond 60 m); the receiver drives the other side's guards within
    160 m of its player. The native step still needs the target within 60 m of the nearer player, as before.
  - The realize hold (`P.hold`): a kind-3 placement (an M1 correction, an M3 host placement) or a drive step the game
    relayed (`refused=relayed`: not realized on this copy) sends `SetForceRealize` to that guard (`event=realize_hold;
    on=1`; at most 4 at once, never Mother Base staff); released (`forceRealize=false`) when a placement of him lands
    (`why=landed`), after 8 s (`timeout`) or at stop. The native per-guard backoff after a relay (3 s, doubling) is
    unchanged, so the next try after it finds him realized.
  - No wire change: a partner without the key streams and drives within 60 m as before.
- **body_dead** (off unless named): `P.check` lets the other copy's dead body placement (M6 flag 8) through; after any
  body placement (`P.doneHook`) the body's resting spot is where this copy's body lies, moved or not, and the body scan
  skips a body while its placement is pending (at most 15 s), so a corpse the game does not move is never reported back
  (no ping-pong). Whether the native set-transform moves a corpse is still [U].
- **Runs** (play, `-WanSim bad`, the gun profile, afgh-ledge): `9023e5c6`, `8281b868`, `3c9d1250` with both keys named,
  `b77e2104` with no key named (place_reach on by default in that build, so it is not a run of the shipped defaults;
  no run exercised the gaps-2 code with both keys off, which rests on code reading and the unit tests). The host's place
  error for the guest-owned guards p90 2.78 / 2.63 / 1.95 / 2.63 m (`798b51dd` without it 6.97 m); 0 latches (the
  divergence report's composite gate still fails on `drive_sank=1` in `9023e5c6` / `b77e2104`, as at the defaults). The
  realize hold ended in a landed placement in only 8 of 26 holds (all 14 host holds of `3c9d1250` timed out); a backoff
  refusal never gets a hold and a hold ends at the first landing, so the riders went unplaced again for ~40 s in
  `9023e5c6`: the hold does not yet wait out the backoff. No frame shows the watched guard on the host's screen (a
  rock, grass or a rise of ground in every host view), so both keys stay off unless named. STATUS has the numbers.
- **Not done**: the riders' in-vehicle part (the LRRP truck is each copy's own: VEHICLE_STATE skips LRRP trucks and
  occupied vehicles), so their copies stay `far` / `vehicle_local` while the truck drives; the next step is the vehicle
  owner path (the host streams an LRRP truck with riders, the guest's copy follows) or a soldier dismount command [U].
  body_dead's stimulus: a guard killed by the debug outcome offers no pick-up prompt (`8281b868` d-a), a dropped steel
  shield takes the pick-up key (`9023e5c6`), and two rifle shots at 2.5 m left a sleeping guard asleep (`3c9d1250`);
  next: a firefight kill the guest then carries, or a CQC kill.

#### 7.18 Enemy gaps 3: the hold waits out the backoff, a copy a storey off its owner (lab keys `place_reach`, CFG r[19] bit 2048; `drive_vert`, native only)

- **The hold's logs, read again** [O] (run `3c9d1250`, design 7.17): all 14 host holds were drive relays, every relay
  `streak=1`, so each relayed guard had a landed move before his next relay (a move ends the streak; the relay table has
  64 rows, none evicted). The hold was released only by a landed placement (`P.doneHook`), never by a landed drive step,
  so a drive hold always "timed out" at 8 s; and a refusal in the native backoff (3 s, doubled per relay in a row up to
  30 s) never got a hold.
- **The hold now** (`place_reach`; `world_place.lua` `P.hold` / `P.hdr` / `P.rzTick`): a correction, M3 host placement or
  drive step refused `relayed` or `backoff` holds the guard (`event=realize_hold; on=1; ...; why; b`). The receipts'
  refusal names the ms left of that guard's backoff (`{refused="backoff",b=<ms>}`, `sw::FormatRefusal`,
  `RefuseWarp` in the guest / host warp receipts and `DriveReceipt`); a held drive step is not tried again before the
  backoff ends; a held placement is queued again then (the refused entry, or a newer queued sample of him; not while
  the stream drives him). A landed drive step (`s >= 1`, `h=1`) releases the hold like a landed placement. The release
  line names `ms`, `src` (place / drive), `st` / `rs` (his landed / resting drive steps while held), `rf` (refusals),
  `rq` (re-queued placements) and the running `landed` / `timeout` totals. Still 8 s and at most 4 guards.
- **drive_sink at the defaults** [O]: one in `6348f361` (the shipped defaults, arm A), `41f2b07f` and `31a799e3` (the
  defaults), 2929558510 each time, 3.28-3.32 m under its target at 1.02-1.05 m planar; also `3bc7280b` (1.14 m planar) /
  `46c0da9e` (`soldier_act`), `9023e5c6` (1799112697, 3.45 m, 0.15 m planar), `b77e2104` (2.15 m, 0.14 m) and on the guest 667680985
  (`125d916b` / `0a271c10`, 3.6-5.5 m). Cause [O code]: `sw::DriveStep`'s rest test is planar (a copy at rest moves
  beyond 1.5 m), and the y is only set on a moving step, so a copy within 1.5 m in x / z but a storey off its target is
  never moved; M1 / M3 corrections skip a driven guard (`why=driven`). [O frame] `b77e2104` guest al-2 (goto 2929558510
  8): the guest's camera looks up a high wall under a red threat arc and the wall top is empty, so no guard is visible;
  that the guest's copy stood about 5 m over the guest comes only from the aim readout (`aim_nearest` d 9.4 m at rotX
  -31.5) [I]. Every such `drive_sink` had 0.14-1.14 m planar.
- **drive_vert** (lab key, lab and play, native only, no CFG bit; off unless named, see Defaults): the soft
  follow's step (`DriveReceipt`, mode 0 only) also moves a copy at rest in x / z that stood more than 1.5 m
  (`kDriveVertM`) above or below the target for 1 s (`kDriveVertMs`, `sw::DriveVertDue`) onto the target's spot
  (`DriveStep` `vert_m`, reply `s=3`, `world_drive=vert; go; slot; gap_cm; e_cm; r; h; n` for the first 64, stats
  `vert=`). Run `617311eb` (named): 0 `drive_sink` on both copies, one snap (the guest's copy of 1799112697, 5.64 m under
  its target at 0.86 m planar, `r=0 h=1`).
- **The watch** (`enemy-reach-watch.txt`, after `enemy-integ-carry.txt`): the host on the ground point (488.53, 268.24,
  2206.17), where `b77e2104`'s host v-3 had shown 1740692850's body about 110 m away; the guest 6 m (`617311eb`), then 10 m
  crouched (`025fbb28`) from him, so he is guest-owned (`event=owner; owner=guest; why=near; dh_m=109`); both cameras on
  him (`aim_ent`) through binoculars, three captures 2-3 s apart. Each copy's aim readout and position give where its
  copy stood (scratch `aimpos.py`).
- **Frames** [O, looked at]: `617311eb` w-b: on the host's screen a guard's body at the crosshair beside the guest's
  character 107.6 m away; on the guest's his legs at the crosshair 5.1 m away (`SCANNING`); copies 0.11 m apart (w-a
  1.00 m: the host's two bodies just left of the crosshair; w-c 0.57 m: the body right of it, the guest's view the
  flashlight only). `025fbb28` w-b: two bodies at the host's crosshair beside the guest's character, 109.2 m; on the
  guest's the analyzer's "Soviet Soldier" at the crosshair, 10.1 m, his body dark behind grass; copies 1.40 m apart
  (w-a 1.38 m, his marker by the nametag with no clear body; w-c 0.68 m). All in calm. Both times he discovered the
  guest after w-c (`phase_discovery`) and the CP's ALERT began before the ALERT part; in `617311eb` the guest was downed
  6 s into it and the host 93 s later, so both were down: the mod's shared game over (MISSION FAILED on the guest), which
  `enemy_div_report.py` did not count (it now counts `step=shared_game_over` / `game_over_applied`).
- **Runs** (play, `-WanSim bad`, the gun profile, research build, afgh-ledge; join, quiet, integ-carry, watch, alert):
  `617311eb` (`place_reach=1`, `drive_vert=1`): host M3 place error p50 0.83 / p90 2.5 m (200 samples: the guest was
  down for most of the ALERT), 0 latches / faults / `drive_sink`, one `drive_vert` snap, one hold (guest, the rider
  2305481811, re-queued at the backoff end and landed 3.1 s after it began), the game over above. `025fbb28`
  (`place_reach=1`, `drive_vert` at its new default): host M3 p50 0.91 / p90 1.99 m (863), guest p50 0.61 / p90 2.0 m
  (150), 0 latches, faults, game overs, `drive_sink`; five `drive_vert` snaps on the host (2929558510 2.47 m over and 1.80
  m under its target, 2942862796 2.61 / 4.06 / 5.80 m under; 0.11-1.17 m planar; all `r=0 h=1`, `h=1` only: no position read-back); 0 relays and 0 holds on
  either copy. Failing rows in both: corrections applied 6/20 and 4/13 (`far` 5 / 6 and `vehicle_local` 8 / 3: the
  riders, 7.17 (a)), M3 host placements applied 3/11 and 9/12 (native `range` 8 / 3: a target more than 60 m from the
  nearer player, `near=host` [I: the guest's pose more than 1 s old]).
- **The no-key run after the flips** `35d4074f` (both on by default then, d4d4cb65; join, quiet, integ-carry, alert): 0
  latches, faults, game overs; guest place error p50 0.61 / p90 1.47 m (126); host M3 p50 1.09 / p90 2.67 m (940); the hold
  [O]: host 39 holds (37 relayed, 2 backoff), 35 landed (by `st`: 10 by a landed drive step, 25 by a placement, mostly re-queued `rq=1`; `src` names what started
  the hold, not what landed it; 1.2-3.1 s, most 3.09 s,
  i.e. at the 3 s backoff's end) and 4 timed out (within 2 s of each other mid-ALERT); guest 20, all landed (round 5: 8 of
  26); two `drive_vert` snaps (2929558510, 2.41 m over and 1.84 m under). FAIL rows: `drive_sink` 1 and `place_sink` 1, the
  host's copy of guest-owned 1179373153 about 150 m off: an M3 placement landed (`dy_m -0.01`, 329556708), a drive step
  relayed 19 ms later and the hold force-realized him, 2 s later he stood 3.73 m lower (`place_sink`, moved 0.4 m), the
  next landed drive step (329559830, 2.63 m planar, so a moving step at his target's y) logged him 5.10 m under, and 2 s
  later the guest's sample still had him 5.4 m over the host's copy; at 329630983 a host `place_div why=fall` had him
  82.5 m off and 10.5 m under (`enemy_div_report.py` counts `guard_fell` on guest lines only, so it missed this row).
  [I] a copy realized far from the host's player drops through a floor whose collision that PC has not loaded (the same
  `place_sink` came at the defaults in `41f2b07f`, without the hold); whether the force-realize causes it is [U]. Also
  corrections applied 15/33 (`unrealized/backoff` 12: samples arriving inside the backoff) and M3 host placements 43/70
  (unrealized 27). Frames looked at: c-b (the sleeping guard by the guest's character on both screens), al-9 (both alive).
- **Defaults**: both off unless named. `place_reach` is off again (6eb410a7 red, then green): its no-key run failed the
  `drive_sink` row. `drive_vert` was on by default from 61470582 / cdfc3651 and is off again (dec7c687 red, 7736b898
  green): a skeptic's check of the runs [O] found that its 8 snaps were of 4 copies (1799112697 once in `617311eb`,
  2929558510 twice each in `025fbb28` and `35d4074f`, 2942862796 three times in `025fbb28`), every one 63-121 m from the
  receiving player, so inside `place_reach`'s 60-160 m reach only (the 60 m band live at the defaults never ran, and no
  run had drive_vert without place_reach); a second snap 1.4-1.7 s later with the gap back over 1.5 m (-2.47 then +1.80,
  -2.41 then +1.84, 2.61 / 4.06 / 5.80 m) means at most 4 of 8 moves held (`h=1` is no read-back); its 1 s snap masks the
  2 s `drive_sink` detector (2942862796 more than 1.5 m under for at least 3 of about 4 s in `025fbb28`, 0 `drive_sink`);
  no frame shows it; `DriveAdmit` also admits Mother Base and Africa, untried. Next for it: a y read-back after the snap,
  a test within 60 m with place_reach off, a frame, then the no-key run. No wire change: an older partner streams and
  drives within 60 m and ignores `b`.
- **What blocks place_reach and what to try next**: (1) the far-copy drop: after a landed placement of a copy 100-160 m
  from this PC's player, read its y back at 2 s (as `P.sinkTick`) and, when it dropped more than 1.5 m under the owner,
  place it again at the owner's y (or skip the force-realize beyond ~120 m); (2) a drive refusal cleared a held
  placement's pending retry (`P.hold` set `r.c = c`, nil for a drive step): fixed (`r.c = c or r.c`, unit-tested, not
  yet in a run); (3) then one run with place_reach named, then the no-key run. Also not done: `body_dead`'s stimulus; the riders'
  in-vehicle part (7.17); the guest is discovered at 10 m crouched in calm (restrict notice does not stop it, 7.14).

#### 7.19 Enemy sync M7 FM1 round 6: the calm half, the walkers' legs, the version gate (lab key `soldier_act`)

- **The user's decision D-FM1 (a)** (2026-11-05): the far mirror mirrors only in calm and caution and is judged on calm
  frames; in ALERT guards work as before (no puppets). `soldier_act=1` (and `calm`) is on with that phase limit;
  `soldier_act=all` keeps every phase (7.16; a test value, but play and release builds accept it too). The limit rides in soldier_act's own value and the `ACT_V1 p`
  reply (`pl=1`), no CFG bit (r[19] has none free); Lua releases and skips a guard of a CP in evasion / ALERT
  (`why=phase_limit`, before the R check).
- **Version gate** (protocol section 36): a 0.15.0 or 0.15.1 partner connects (the handshake checks only `kLinkVersion`
  2) but has no ACT_CAPS (0.15.0 has no SOLDIER_ACT either; 0.15.1 has round 5's 0x58). Each PC with `soldier_act` sends ACT_CAPS (0x5C: caps bit 1 on, bit 2 its calm limit;
  revision 6) once per pairing and mission start; the far mirror (registration, every SOLDIER_ACT part, every puppet)
  runs only after the partner's ACT_CAPS with bit 1, so nothing of it runs against an old partner or one with the key
  off (`why=peer`). The defaults of a PC without the key send nothing new. Unit-tested with an old partner
  (world_align_lua_check, world_wire_tests; 0.15.1 skips 0x5C by its length, checked by reading); in game both copies
  logged `event=mirror_peer; caps=1|3; rev=6`.
- **Walkers, three causes** [O, runs `fbf3522a`, `f11fe8a1`; Ghidra decompiles read-only]:
  - the 5 s re-apply (7.16): an unchanged request is now re-applied from the mailbox every 1 s while the owner copy
    moves (its order-part position >= 0.3 m from the last apply; `ReapplyDue`, `OwnerMoved`), else every 5 s. The
    speed ratio while the owner copy moves is now p50 0.99 / 0.95 taken together (was 0.19 / 0.78), but not per
    guard: guard 246 stood about 4.5 s and then jumped 16-20 m, again and again (ratio 0.0 in both runs). The apart
    release waits for a still owner copy (moved under 0.5 m for 2 s); apart releases were still 4 / 10 / 1 a run
    (`fbf3522a` / `f11fe8a1` / `0d801772`; round 5 12-13).
  - a stale order target: the order unpack 0x1413701f0 writes the target (R+0x1b0..+0x1bb) and its mode (the low 3 bits
    of +0x1bc) only for packed modes 1..4, so a host order without a target left the puppet's own brain's target
    (guard 15 in `fbf3522a`: 413.5, 267.6, 2133.3). Now cleared (`OrderHasTarget`, `ClearOrderTarget`, `order_clear`).
  - his own leg: with the request record byte-identical to the host copy's (the census packs of both copies differ in
    one facing-yaw bit) and the destination R+0x80 the host copy's, a puppet keeps walking the leg his own brain had
    before the take, at the host copy's speed (guard 15: the host copy toward 363.6, 2159.8, the puppet toward
    414.4, 2133.6, 15.8 m apart before the engine's 15 m warp). The leg is not the whole cause: by the 3 m
    test below, the same-leg takes of `fbf3522a` had p50 1.81 m (per-take medians 0.18-9.77 m), `f11fe8a1`'s one 8.05 m. The request unpack 0x14136f810 runs its
    catch-up (gait + 1 at 2..5 m, gait 3 beyond) only for dest mode 0; 99 % of the moving samples are kind 1, dest mode
    1. A walker (action A kind 1) is now taken only when his own destination is within 3 m (flat) of the host copy's
    (`LegMatch`; the hook packs each realized non-puppet guard every 0.5 s; frame line `leg_wait`). That it helps is
    not shown [U]: in `0d801772` every take passed it, p90 was still 9.4 m, guard 19 with the host copy's destination
    ran the opposite way, and on the same guards (15, 17, 18, 246) the error went 6.43 to 4.38 m. [U] what in the
    body keeps the old leg (the path follower's own target is not in the 0x70 request).
- **What a player sees with the key named** [O]: puppets jump 15-22 m between two 0.5 s census samples 7 / 13 / 13
  times a run (the engine's 15 m warp of a copy left too far behind [I]); Lua's near release took puppets at 73.9 m
  (after a 16 m warp toward the guest), 79.8 m and 79.9 m, which the 0.5 s census (minimum 80.4-99.5 m) missed;
  ALERT came at the end of `fbf3522a` and `f11fe8a1` with no puppet, so `why=phase_limit` never fired in a game.
  Unexplained [U]: in `0d801772` the guest's screen alone showed a CP radio report of a possible intruder about 1 s
  after the run's only `order_clear` and guard 17's take, in calm, before any shot.
- **Riders**: the far rule skips a guard whose `GetVehicleGameObjectId` is a game object (never taken, released
  `why=vehicle`, not registered by the host). [U] whether a soldier at a gun emplacement answers that command.
- **Logs**: `soldier_act_frame` adds `payload_ids` (the aim-target swaps), `due`, `reapply`, `expired`, `order_clear`,
  `leg_wait` (`due` counts just before the unpack, so applied / due restates only 0 unpack faults; delivery is the
  host's `staged` against the guest's `rx`, 55-66 % under `-WanSim bad`); `event=mirror_caps`, `event=mirror_peer`, `soldier_act_mirror=...; phase=calm|all`.
- **The watch spots** (known ground, both measured standing points of `fbf3522a`): A (424.627, 271.559, 2025.110), the
  end of the walk south (four runs) and B (414.897, 274.034, 2017.557). From A a bush hides guard 17's later route;
  from B a bush hides 17's whole route; guard 15's patrol by the fence is visible from both.

#### 7.20 Enemy round 7 track A: guards at the same height, the far fights (lab keys `drive_vert`, native only; `place_reach`, CFG r[19] bit 2048)

- **What round 6 left** (7.18): `drive_vert` snapped copies only 63-121 m out (place_reach's band), at most 4 of 8 snaps
  held, "landed" was `h=1` with no position read-back, and its 1 s snap reset the 2 s `drive_sink` window. At the old
  defaults the host's copy of 2929558510 (a guard on a wall top about 49 m from the host's quiet spot) was left 2.2-3.3 m
  under its target in 4 of 6 runs, every time within 60 m. place_reach's no-key run `35d4074f` had a far copy (1179373153,
  ~150 m) drop 3.7-5.4 m after a landed placement.
- **The y read-back** (`lua/coop/world_readback.lua`, a new chunk after `world_mirror.lua`; protocol section 37): 0.5 s
  and 2 s after every vert snap, every landed drive step that ends a place_reach hold, every landed kind-3 placement of a
  held copy or of a copy beyond 60 m, and every put-back, the copy's GetPosition y against where it was put
  (`event=readback; src; at; dy_m; held` within 1 m). It runs in the placement tick (`P.sinkTick`), so it does not
  depend on the guard still being driven.
- **drive_sink apart from snaps**: moved from `world_snap.lua` into the new chunk (P.hdr). The time the reply's `v` puts
  the copy more than 1.5 m under the target is summed across readings; a reading level for less than 2 s (a snap that
  did not hold) does not reset it, level for 2 s does. Each gap between two low readings counts up to 2 s, so sparse
  readings count as in 0.15.1 (the round's first cap, 500 ms, made readings 1 s apart need 4 s; fixed after the
  skeptic, 835bcfae). Not yet seen on a sinking copy in a run (below).
- **No thrash** (native `DriveReceipt`): at most 2 vert snaps of one copy in 10 s from the first; the third due snap is
  not taken (`world_drive=vert_stop`, once per window).
- **Far copies** (place_reach only): a read-back that finds a copy beyond 60 m more than 1.5 m under where it was put
  (and under the owner's newest sample of him, when one is at most 3 s old: the owner did not go down too) puts him back
  once (his placement queued again, or that newest sample), ends his force-realize when that put-back is done (or at once
  when nothing can put him back) and starts no new realize hold for him for 10 s (`event=readback_fix`,
  `event=readback_nohold`). Why this fix: in `35d4074f` the drop came while the hold force-realized him ~150 m from the
  host [I: on a floor the host has not loaded]; one put-back gives the copy its place again, and ending the force-realize
  lets the engine stop simulating him there instead of letting him fall again; the 10 s without a hold stops the hold /
  fall / hold loop.
- **Report**: `enemy_div_report.py` counts `guard_fell` on both copies (it missed `35d4074f`'s host row) and prints the
  read-backs per copy and source (held, under or above where the copy was put: a copy driven on after it was put
  measures the ground he ran to, not a fall), the guards under / above, the 2 s read-backs got, cut by a newer arm of
  the same copy (one slot per copy) or open at the log end, the put-backs, the vert snaps and stops
  (`tests/enemy_div_report_check.py`).
- **Scenario** `enemy-vert-alert.txt`: `enemy-latch-alert.txt` with a height watch of 2929558510 in its second block
  (the guest 8 m from him, so he is guest-owned and the host's copy is driven 49 m from the host as in `41f2b07f`; this round he was 67-73 m off; both cameras on him
  with `aim_ent`, the host through binoculars, frames v-a / v-b 3 s apart, before the guest's shot).
- **Runs** (play, `-WanSim bad`, the gun profile, research build, afgh-ledge, at f945663d) [O]:
  - `90d9b4d1` (`drive_vert=1`; join, vert-alert): 0 vert snaps on both copies. The host stood at the quiet spot 67-73
    m from 2929558510 (49 m in `41f2b07f`: he moves along the wall), so its copy of him was placed by M3, not driven
    (his place read-backs 0.00 m). Read-backs: host 293 / guest 48, all held (place); 2 s read-backs host 102 of 191
    (87 cut by a newer arm). 0 `drive_sink` (no copy driven in the 60 m band: the counter not exercised [U]). The guest
    was downed in the ALERT (t 336621486); host M3 p90 15.83 m, 4.32 m before that (161 samples, the first ~80 s of the
    alert part); during the down the per-second median place error was 14-29 m (max 48.9 m) for about 12 s.
  - `ddcdec94` (`place_reach=1, drive_vert=1`; join, quiet, integ-carry, vert-alert): 2 vert snaps, both of the host's
    copy of 2929558510 at 70-71 m: gap -2.40 m, then +1.88 m 1.7 s later; read-backs -1.27 m (0.5 s), -1.26 m (0.5 s),
    -1.88 m (2 s; the fix skipped `why=owner`). Far placement read-backs 37 of 41 held (+1.02..+1.55 m over where put
    for three guards within 3.5 s; one `place_drop` with its owner). 0 put-backs needed. 9 holds, 9 landed. 0
    `drive_sink` / `place_sink` / `guard_fell`, latches, faults, game overs. Host M3 p90 11.63 m; 2.27 m (p50 0.94 m,
    983 samples) before the guest was downed (t 337325821); after it 110 M3 placements were refused `range` with
    `near=host` and the per-second median place error rose to 52 m (max 65.2 m) by 25 s after the down, when the log
    ended. The three not-held place lines above where put were driven copies (`place_div why=driven`), not pops.
  - `1870d1bf` (no key named, at e0bd0352; join, quiet, integ-carry, latch-alert): 0 latches, faults, game overs,
    `drive_sink`, `place_sink`, `guard_fell`; guest place error p90 1.72 m; host M3 p50 1.93 / p90 6.89 m (no guest
    down); read-backs host 486 / guest 26, all `src=place`, all held; 0 vert snaps, 0 put-backs, 0 holds.
- **Frames** [O, looked at]: no matched pair. `90d9b4d1` v-a: host binoculars, a guard by his marker at 67 m; guest 8 m
  from him, a high wall and its empty top under a red threat arc. `ddcdec94` v-a / v-b: host binoculars, only his
  marker at 72 / 70 m, no body; the guest's v-b figure stands 3-4 m from the camera where the third-person character
  stands: most likely the guest's own character, not the guard [I] (corrected after the skeptic).
- **Defaults**: both keys stay off unless named. drive_vert: no snap within 60 m, 0 of 3 read-backs held, no frame.
  place_reach: the run's host M3 p90 failed (11.63 m; the pre-down 2.27 m passes, though not like-for-like with
  `90d9b4d1`'s 4.32 m; the drift after the guest's down happens without the key too) and its frame row failed (the host
  saw only the marker with the key, the guard's body by his marker without it).
- **Next**: a host point within 60 m of 2929558510 and a guest point that sees the wall top; a vert snap only while
  the owner copy is at rest in y (the two snaps chased an owner going down and up); the host's M3 placements while the
  guest is downed (`near=host` range refusals), then place_reach's gate again and a no-key run after any flip.

#### 7.21 Enemy round 8 track B: reinforcement trucks in the same place on both screens (lab key `lrrp_follow`, native only)

- **Cause** (archived run `1870d1bf`, no key) [O]: the guest's mission starts 71.6 s after the host's (`vehicle_state;
  step=on` 337953769 / 338025355). Each copy's own AI drives its LRRP trucks (`veh_trc_0000..0005` = 25600..25605 in
  30010) along its own travel plan from its own start [I: TppEnemy `lrrpTravelPlan` / `lrrpVehicle`], so the guest's
  truck runs ~72 s behind: at the join the riders 3970963356 / 2305481811 were 432.9 m apart (`apply d_m`); the guest's
  truck was still driving in at ~11 m/s from (764, 1940) while the host's copies stood at the outpost (413, 2138). Nothing
  shared the truck: VEHICLE_STATE skips LRRP trucks on both sides (`S.busy` `lrrp`) and a correction skips a rider
  (`vehicle_local`), so the riders were `far`, then `vehicle_local` every ~1 s until they got out.
- **lrrp_follow** (native-only lab key; no CFG bit: Lua asks the receipt `MGS_COOP_LRRP_CFG_V1`; off unless named: the
  gun profile turned it on at f91dc7ff, back off at 34b9f024 after the review below): the host owns every LRRP truck (the trucks start on its clock). It streams
  each live one no player rides (not the local player's vehicle, not the partner's RIDE_FAST ride) as LRRP_FAST (0x5D,
  protocol section 38): 10 Hz while it moves (> 0.5 m/s over 250 ms), 1 Hz at rest, one datagram per 50 ms (one receive
  slot for all trucks: a newer datagram for one truck replaces an undelivered one for another). The guest's
  copy gets `ForceStop{enabled=true}` (the E10 stop of its own AI driver) while the host's track lives and is put on the
  newest sample advanced by its velocity (the sample's age on the receiver's min-offset clock + 100 ms, at most 400 ms
  ahead) every frame while the host's truck moves or carries riders (no local-player hold; untested near the guest); its
  riders ride along. At rest it is pinned (put back
  at once when 1 m+ off; `step=settle` over 3 m). A crew still seated on the guest's copy while the host's truck rests
  empty gets `UnsetRelativeVehicle` after 2 s and `StartTravel{travelPlan=""}` after 5 s (s10090's get-off; `step=unseat`);
  once off, the M1..M6 corrections may take them (the runs show they mostly did not: see the review). A track 3 s stale, the local player's ride, unpaired, start and
  stop release it. In `world_vehicle_state.lua` (`S.lf`; since round 9 B its own chunk `world_lrrp.lua`, 7.25, which drops the `StartTravel ""`); an error there turns only lrrp_follow off. Logs `event=lrrp;
  step=host|follow|riders|stop|release|settle|jump|unseat|stale|refused|error`.
- **Session 1** `c90ab046` (key named; join, the truck watch, integ-carry, latch-alert) [O]: truck 25605 carries
  3970963356 / 2305481811. At the pairing the guest's copy jumped 460 m onto the host's, then followed it for 166 s: guest
  vs host at the same t_ms p50 0.14 / p90 0.42 / max 3.79 m (1760 samples while it moved), |dy| p90 0.04 m; rider
  3970963356 seated on both p50 0.21 / p90 0.71 m. At its stop the host's crew got out, the guest's stayed seated; the
  first form released ForceStop at rest, so the guest's own AI drove the truck and its crew 300 m back east and the 30 s
  settle put it back six times (305-311 m): fixed by the pin and the unseat. 76 rider corrections refused
  `vehicle_local`. integ-carry failed: 2961503 was awake (the watch replaced latch-quiet's sleep).
- **Session 2** `c784a5a7` (key named, the fixed form) [O]: truck error while it moved p50 0.16 / p90 0.74 / max 5.52 m
  (1525 samples), |dy| p90 0.06 m; riders seated on both p50 0.10-0.18 / p90 0.51-0.95 m (both crews, four riders; each
  rider's one sample at the pairing, before the first placement, was 368-403 m; the review's interpolated p90 0.75-1.09 m,
  25605's riders 10.7-49 m in 14 samples each, 25606's 8.1-9.8 m); one jump per truck at the pairing (11.6-402 m; 25605's
  target 364 m from the guest, still at the seed spot), and truck 25605 caught up 6.0 m 14.8 s after its first jump and 5.1 m later (a sample
  gap ended at the 400 ms extrapolation cap; 610 / 350 m from the players [I: not in view]). The guest's crews of the four
  parked trucks (empty on the host's copy) and of 25605 at its stop got out after the unseat (25605's 6-8 s after the
  host's); 0 `vehicle_local` refusals, but 25605's unseated crew did not follow the host's copies: 3970963356 stood at
  (407.4, 2123.7) for ~100 s, 43-70.7 m off; 2305481811 26-31 m off for ~80 s (42 of the 87 guest-side `unrealized`
  refusals; the guest's kill of 2305481811 applied on the host 27.8 / 29.9 m away, `1870d1bf` 0.4 / 1.5 m). Frames t-10
  (both, binoculars SE): the lit truck at the same spot by the crosshair on both screens (`aim_ent` 191.5 m host /
  186.8 m guest). 0 latches / faults / game overs / drive_sank.
  integ-carry failed at its placebehind (`viewer landing has no new position sample`; the truck had parked 10 m from the
  host's spot) and latch-alert was cut at 606.8 s by the timeout. Frame proxy (partner_tick hz per 10 s): 46 Hz from the
  first window on both copies, before the pairing too, so the PC was loaded [I]; session 1 dropped to 43-51 Hz while the
  truck drove and the cameras looked 300-600 m east (59 Hz otherwise); `1870d1bf` 59 Hz: not judged.
- **Session 3** `0da27fc6` (no key named, the final code head f91dc7ff: lrrp_follow on with the gun profile; join, the short
  truck part, integ-carry, latch-alert; all four parts passed) [O]: `event=lrrp; step=on` on both roles; truck error while
  it moved p50 0.14 / p90 0.52 / max 5.65 m (1224 samples; interpolated 0.14 / 0.49 m, 1210), |dy| p90 0.04 m; riders
  seated on both p50 0.10-0.18 / p90 0.36-0.96 m (interpolated 0.92-1.13 m; 11 of 510 samples of 25605's two riders were
  10-41 m apart for ~3 s: the guest's rider reads frozen at (956.8, 1968.2) while the truck was within 0.2 m); one jump
  per truck at the pairing (49.5-402.6 m; 25605's target 364 m from the guest at the seed spot), later three catch-ups
  of 5.4-6.7 m at least 43.9 s apart; every truck ends with `rl=0`, but 25605's guest crew was out before the host's and
  got back in and out twice (rl 0-1-0-1-0, ~125 m from the players), and no correction was tried for the unseated
  riders; 0 `vehicle_local`; report: 0 latches /
  faults / game overs / drive_sank, guest place error p50 0.51 / p90 0.91 m, corrections 25/26; failing rows as at the old
  defaults: M3 host p90 7.26 m (`1870d1bf` 6.89), the guest's state-5 apply at 39.5 m (1179373153 in the ALERT). Frames s-0 /
  s-1: the truck out of view (far east); c-b: the sleeping guard by the guest's character on both screens. Frame proxy 46
  Hz from the first window on both (before the pairing too): not judged.
- **Review** (skeptic, all three runs): the truck rows hold; refuted were the riders' p90 (0.75-1.13 m interpolated, with
  10-49 m outliers in both key-on runs), the pairing jump being out of view, the crews getting out "a few seconds later"
  (0da27fc6 flapped), the unseated crews being kept with the host's copies (above), the "did not drive off" clause (below)
  and the gate as a whole (frame time not judged, Africa not run). lrrp_follow is off unless named again (1aa30d55 red /
  34b9f024 green); turning it on is the user's call (a possible player unseen by reinforcements, below).
- **Not done**: a crew that gets back in on the host's screen stays out on the guest's, and the guest's truck drives off
  empty (25606 in `c784a5a7` and `0da27fc6`: `rh=2 rl=0`, ~12.5 m/s, still moving at the end, at (-965, 1570) and
  (-1433, 1159)); the unseat ends the guest's crew's LRRP plan (StartTravel ""), so it no longer boards [I] and those
  reinforcements may never reach the guest on the guest's PC; corrections for unseated riders; the frame-time row needs
  an unloaded PC and a key-off run of the same parts; Africa (30020) not run; a mixed-version run.
- **Old partners**: a partner without 0x5D skips it by its length (`unknown_op`); a guest without the key ignores it and
  its own AI keeps its truck, as in 0.15.1 (unit tests).

#### 7.22 Enemy round 8 track A: the quiet place correction, the host's copies while the guest is down (lab keys `quiet_place`, `range_flat`, native only)

Markers: [O] seen in a log line, a frame or the code (Ghidra decompile, read only); [I] inferred; [U] unknown.

- **Bug 1, a false radio report** (enemy7 track C's plan (4), runs fbf3522a / f11fe8a1 / 0d801772 [O 3/3, cause I]): at
  the afgh-ledge seed guards 15 and 246 hold swapped routes on the two copies; about 5 s after align the guest's copy of
  246, 56 m off and off the guest's screen, gets the default-on place correction, the set-transform message
  0x287158b07327 through SendCommand. Within 0.2 s he is in action kind 3, then 15 -> 12 -> 13: the guest's screen shows
  the radio report of a possible intruder, and about 20 s later the outpost goes to CAUTION on the guest, which the host
  then applies.
  - **How often** [O, census runs of rounds 5-7: 963ab276, 3bc7280b, 94a7bf2a, 46c0da9e, adecad01, 80cc0176, fbf3522a,
    f11fe8a1, 0d801772; recount of the review, the earlier 282 / 54 did not reproduce]: of 539 applied place warps (the
    guest's corrections and the host's M3 placements, both the set-transform message; 57 guard-runs) 499 had the guard in
    action kind 3 within 0.5 s (55 guard-runs), but 260 found him in kind 3 already before the warp (263 changed into
    kind 3, 52 guard-runs), so kind 3 after a warp does not by itself show the warp caused it [I]; kind 13 within 30 s:
    1 (0d801772's 246; in fbf3522a his kind 13 came 52 s after). The report is rare; only these census runs log the
    kinds, so the rate at the shipped defaults is inferred [I].
  - **The quiet path** [O code]: `0x14136ece0(M-0x20, idx, entry)`, the request unpack's network-position warp (its only
    engine callers are the three in the FOB unpack). It reads S = (M-0x20)+0x70's slot table only: if the slot position
    `*(*(S+0x18)+0x20)+idx*0x10` is more than 15 m (225.0 squared, 0x142329c00) from the entry it writes the slot
    position and yaw, looks idx up in the realized list `*(S+0x18)+0x28` (count +0x44) and, when found, moves the body
    through the character controller (S vt[0x100](S, idx), the component's vt[0x388], then rotation vt+0x70 and
    position vt+0x68 with y + controller +0x160). It tests no puppet bit, no S+0x2a4 mode, no FOB state, sends no
    message and touches no brain, so a non-puppet guard offline goes through the same lines as the puppets the far
    mirror already moved with it (round 6: take-time warps of guards 15 / 17 followed by no kind 3) [O code, I effect].
  - **Built**: `quiet_place` (native only, no CFG bit): the guest's Lua names a correction off its player's screen
    (` 1` on WORLD_WARP_V1); the native stages it for the request manager hook (installed for a guest with the key),
    which calls 0x136ece0 in its next update with the target and the grant's yaw; a slot within 15 m is biased 20 m
    along x first (the function overwrites it). `world_place=quiet` prints the slot read back; Lua reads the copy back
    0.5 s and 2 s after (`event=readback; src=quiet`, x/z and y). Not realized here or no hook: the message as before.
    Three contract sites pin the function's body (soldier_act, 20 sites).
- **Bug 2, the host's copies drift while the guest is down** (round 7 [O ddcdec94, 90d9b4d1]): the cause is not the
  down itself. In both runs the guest's `enemy goto <guard> 8` put them on the outpost's cliff edge and they fell
  through the world (ddcdec94: y 269.9 -> 92.1 m at x/z 436.1, 2181.7 was a sample mid-fall, 5 s later y -79.65 at the
  same x/z; 90d9b4d1: y 270.7 -> -79.7 m) and the fall downed them (damage 10000, attacker 0). The M3 owner rule is x/z
  (`P.ownerOf`, the PEER pose's x and z), so the guest stayed the owner of the ledge guards 5-30 m away horizontally.
  The host's range rule (`NearerSquared`) is 3D, and every placement beyond 60 m of the host was refused `range`
  (ddcdec94: 0 of 110 applied while down, host place error p90 27 m from the bins; 90d9b4d1: 63 of 135, p90 21.7 m).
  [Corrected by session 2 and the review below: the refusals came from the guest's pose not being used at all while the
  host hides the guest's body for range (ddcdec94: all 111 `near=host` refusals after `partner_range=hide` at
  202573 mm; before it 3 `range` refusals, all `near=guest`). The 3D rule against the x/z owner rule is a code fact
  never seen refusing a placement [I].]
  - **Built**: `range_flat` (native only): the host's M3 placement range and its soft-follow range from the nearer
    player on the ground plane (x/z), as the owner rule; `world_place=host_warp` prints `flat`, the guest pose age and
    the guest's height over the target (`dgy_m`).
- **Gates** (design): quiet_place: in calm at the seed the 246 correction is `leaf=quiet`, moved, read back within 1 m
  (x/z and y) at 0.5 s and 2 s, no kind 3 / 13 of him after it, no guest-only phase change (`event=req_phase`) in calm,
  0 latches / faults / game overs. range_flat: with the guest down in an ALERT, the host M3 place error p90 while down
  <= 5 m and those placements applied, 0 latches / faults / game overs.
- **Session 1, `bcd95b14`** (`quiet_place=1, range_flat=1, act_census=1`; enemy8-quiet-calm + enemy8-down-alert, gun
  profile, research build, afgh-ledge, `-WanSim bad`) [O]:
  - quiet_place gate PASS: the guest's 246 correction 8.7 s after align was `leaf=quiet`, `world_place=quiet moved=1
    bias=0`, `event=place err_m=0.01 dy_m=0.00`; read back at 0.5 s 0.08 m x/z, 0.00 m y and at 2 s 0.15 m x/z, 0.00 m
    y. The census shows him in action kind 1 from 0.1 s after the move until 117.7 s (then kind 8 in the ALERT part): no
    kind 3, no 13. 0 guest `req_phase` in calm (one in the ALERT part, phase 3, beside the host's own ALERT). 0 latches,
    faults, game overs. Frames q246-a..d (both copies, looked at): no radio subtitle and no caution on the guest's screen;
    guard 246 cannot be picked out on either (the host's view is half behind a tree) [U].
  - Two more quiet moves in the ALERT part (`bias=1`, under 15 m): moved, read back 0.00 m at 0.5 s; at 2 s 5.3 / 8.9 m
    x/z off the target with y held (they ran on: ALERT).
  - range_flat gate NOT REACHED: the guest stood on ddcdec94's goto point (433.3, 271.4, 2181.7, 1.5 m up on the
    ledge) and was not downed. With the key every host placement was `near=guest flat=1` (103; 94 applied, 9
    `unrealized`); host M3 p90 7.46 m over the run (its own 5 m row fails, as at the 0.15.1 defaults: 1870d1bf 6.89 m).
  - quiet_place then follows the gun profile (64ab8909 red / cde0cbb4 green); the down part drops the guest from the
    air on ddcdec94's fall line (436.105, 240.0, 2181.736).
- **Session 2, `8b581a26`** (`range_flat=1, act_census=1`; quiet_place at its new default, not named; same two parts)
  [O]:
  - range_flat gate FAIL: the guest was downed (fell from the air point; the fall went on through the cliff foot to y
    -79.65 at x/z 436.137, 2184.572, under the world; frame dd-1: "WOUNDED" over a pale void on the guest's screen,
    "TEAMMATE WOUNDED" on the host's). While down (48 s) the host refused 142 of 142 placements `range`, all
    `near=host flat=1`, `dgy_m` -268..-271 (the guest pose's y read as about 0); host M3 error while down p50 17.1 /
    p90 27.4 m (bins; ddcdec94 without the key 27.1 m). 0 latches, faults, game overs.
  - **The second cause** [O code + logs, corrects the bug 2 finding above]: the receipts take the guest's pose from the
    partner body driver's `mission.cmd`, valid only while `have_cmd`. When the host's copy of the guest's body is hidden
    for range (`partner_range=hide`, beyond 200 m in 3D: `distance_mm=200264` here, 202573 in ddcdec94 before its
    refusals) or unrealized, `have_cmd` is false (partner_driver.cpp), `snapshot.remote_valid` is false and the remote
    position is not copied (left 0), so `NearerSquared(Flat)` keeps only the host's leg: near=host whatever the plane.
    The same invalid pose answers the Lua PEER receipt (the M3 owner rule's `dq`), so the owner falls back to the host
    after 10 s. range_flat fixes the plane only; it cannot help while the body is hidden, and it is untested for a guest
    whose pose is known (every down so far fell through the world). It stays off unless named. Before the down the host
    M3 error was p50 1.8 / p90 7.75 m (its own 5 m row fails); the guest's drive was refused 148 of 149.
  - quiet_place: no quiet move in this run [O]: this time the seed's 246 was 36.5 m off and the host sent no sample of
    him in calm (no `place_div` of 246 on the guest), and the guest's 5 corrections in the down part were refused `far`
    (the guest under the world, more than 250 m from them in 3D). 0 guest `req_phase` in calm.
  - **Next for bug 2**: give the M3 / drive nearer rule (and PEER) the guest's network pose sample
    (`partner_net::SampleRemote` x / y / z, age at most 1 s) when the body is range-hidden or unrealized, keep the x/z
    plane (range_flat), then the same down part (the fall through the world reproduces it: 90d9b4d1, 8b581a26).
- **Session 3, `1befcef4`** (no key named, at the code head cde0cbb4 with quiet_place on by default; enemy-prove-1-join,
  enemy-latch-quiet, enemy-integ-carry, enemy-latch-alert; build 0.15.1+aaf852f, docs only after cde0cbb4) [O]:
  - 115 quiet moves of 7 guards (72 biased: under 15 m), all `moved=1`; 106 within 1 m of the target at the 3-frame
    read-back, 8 more than 2 m off (4 guards: 1926520947 4 of 5 at 34 m, 667680985 once 91.9 m, 2305481811 twice,
    1740692850 once) while the slot table read the target: the warp wrote the slot but did not move the body [O], most
    likely a guard not in the realized list `*(S+0x18)+0x28` although his realized bit was set [I]. In 5 of the 8
    (1926520947 4 times at 33.9-34.8 m, 667680985 once at 91.9 m) the landing error equals the error before the move: the
    body was not moved at all [O]. `world_place=quiet moved=1 err_m=0.00` reads the slot table only: 0x136ece0 returns 1
    after writing the slot whenever the delta is over 15 m, also when idx is not in the realized list, so the line does
    not prove a body move (off at the defaults; only the read-backs do). 4 corrections went by the message
    (`leaf=typed`, among them 246's, 0.01 m). Quiet read-backs within 1 m: 55 of 207; 80 of the 115 0.5 s read-backs
    over 1 m x/z; 1 2 s read-back open; `terminal_win` 1 on the host.
  - FAIL (the no-key row 0 drive_sink / place_sink / guard_fell): 22 `place_sink` of one guard, 1904171355: after 22 of
    his 43 quiet moves, each landed (`land_dy_m` -0.03..0.02), he walked 9.4-14.1 m on (`moved_m`) and stood 2.15 m under
    his owner's sample (round 7's `1870d1bf` at the old defaults, same parts: 0). The 2 s read-backs show the same
    walk-off for others (maxima 14.3-17.2 m x/z, y held): the quiet warp leaves the brain's path, so a guard walks on along it, where the
    message's kind 3 had stopped him [I, plan (3) of enemy7 track C]. Also 1 `guard_fell` on the HOST (its copy of
    3268415846 10.6 m under the guest's sample, `place_div why=fall`; the host has no quiet move) [U cause].
  - PASS: 0 latches, faults, game overs; guest place error p50 0.69 / p90 1.88 m; corrections 119 of 126 (7
    `vehicle_local`); 0 `drive_sink`; host M3 p50 1.7 / p90 6.64 m (reported; its 5 m row fails as at the defaults).
    0 guest `req_phase` in calm (1 in the ALERT part, another CP, phase 3).
  - Frames looked at: c-b (both: the sleeping guard's ZZZ by the barriers beside the guest's character).
  - **Decision**: quiet_place back off unless named (f8441a11 red / green): a row failed by the flipped key. range_flat
    off unless named (gate failed). Nothing changes at the defaults; the receipt's quiet token and the log fields stay.
    No session ran f8441a11 at the defaults: with quiet_place off the native ignores the Lua's ` 1` and takes the old
    `sw::Warp` (unit tests and code [I]; the token's parse ran live in all three sessions). Process note: quiet_place was
    turned on although session 1's gate rested on one move (n=1) and its other two quiet moves read back 5.29 / 8.87 m
    off at 2 s (4 of 6 read-backs within 1 m), with the down gate not reached.
  - **Next for bug 1**: stop the walk-off: after a quiet move, give the guard a stand at the target (the request start
    0x14136f090 mode 0 issues a stand command offline, plan (1)) or end his path (the planner cancel), and move only
    guards in the realized list (read `*(S+0x18)+0x28`, not the realized bit), then the calm gate and a no-key run.

#### 7.23 Enemy round 8 track C: the far mirror's walkers plan their own path (lab key `act_plan`, native only; with `soldier_act`)

- **Why the walkers went wrong** (enemy7 C-fm1 research, read-only): offline the walk-request start 0x1413725f0 plans
  only for a FOB remote copy (S+0x2a4 bit 2 and the slot's owner byte != S+0x2a8); offline (census `mode=0x782`) it sets
  R+0x19e bit 1 only, and the path step 0x141372b70 drives the body along the path the soldier's follower already holds:
  his own brain's last one. The unpack's 15 m warp 0x14136ece0 then snaps him toward the host copy again and again.
  Session 1 (`fcfb3acc`) shows it in the calm window only [O]: puppets in a walk had their path end p50 55.69 m from
  R+0x80 (121 samples, 74 % over 10 m; guard 18's frozen at one point 55.7 m away in all 72), the guest's own guards
  80 % within 1 m. Over the whole run the two look alike (puppets p50 1.86 / p90 65.45 m, own guards p50 0.00 / p90
  80.92 m), so the pooled figures do not support it.
- **The planner** (session 1's `soldier_act_planner` line, decompiled read-only): `*(S+0x30)` has vtable 0x23a1610; the
  follower pair is {the planner, idx}. vt[0] 0x140fa2430 (planner, idx, move) is the soldier route planner's request: it
  takes the move's type hash (+0x70, low 48 bits), refuses (0xffff) when the guard's slot *(planner+0x48)+idx*0x10
  {u48 hash, u16 handle +8, counter +0xa, done +0xc} holds another non-zero hash, fills the guard's record
  (+0x40 stride 0xf0) and submits a path query (planner+0x30 vt[0]); it reads no FOB state. vt[0x10] cancels a handle
  vt[0x18] accepts (the slot's +8): the query and the slot. vt[0x90] (has a path) = the slot's handle is live and done;
  vt[0xa0] the path; vt[0xd0] (idx, float) sets record +0xd0. The brains use the same planner with several type hashes:
  the guest's own guards' census (`afeddbdc`) shows a brain's hash in the slot in 61 % of samples, free in 39 % [O].
- **Built** (`act_plan`, native only, no CFG bit, no wire; needs `soldier_act`; off unless named):
  - after the manager update's original (the engine's offline start of each puppet's new request has run), for each
    puppet whose dest-mode-1 request the hook unpacked in that update, when R+0x19c holds no handle and
    (R+0x19e & 3) == 2: 0x1413725f0 again with a shadow gate object (+0x70 -> S' with +0x2a4 = 4, +0x2a8 = 0, +0x18 ->
    +0x18 owner bytes all 1). It plans from his slot position (through the request's waypoint) to R+0x80, stores the
    handle in R+0x19c and calls vt[0xd0](idx, 10.0f): the FOB remote copy's own code. The next start cancels the handle.
  - a slot of another type with a live handle (his brain's walk) is cancelled first with vt[0x10] and that handle
    (`plan_cancel`); another type with no handle is left alone (`plan_busy`); a refused plan counts `plan_none`; when he
    is released, our handle still in his slot is cancelled (`plan_release`), so his brain's next walk is not refused.
    The plan call and the cancels run only for the planner class the contract pins (`PlannerKnown`: vtable 0x23a1610).
    The follower getter vt[0x20] (also before that check) and the follower's vt[0x90] / vt[0xa0] (census, the 300 ms
    check, the planner line) are called through the object's own vtable with only an in-image check, as the engine
    calls them (0x14136f0f2 / 0x141372bae); their code (0xfa1cc0, 0xfa1ed0) is pinned only as vtable slot targets.
  - the gait catch-up of the unpack's own mode 0 rule for a walk (mode 1 copies the gait as sent): the owner copy ahead
    toward R+0x80 by 2..5 m flat: gait + 1 (cap 3); beyond 5 m: 3; never lowered (`gait_up`).
  - a warped puppet faces the request's facing (face mode 1: wire +0x50 int16 * pi/32767) or keeps his own yaw (the lent
    entry had yaw 0).
  - a walker (action A kind 1) is taken only within 10 m (flat) of the owner copy (`near_wait`).
  - logs (protocol section 40): census `r19c`, `r198`, the follower's path, `r80` and the slot; `soldier_act_planner`
    once; `soldier_act_warp15` per unpack whose 15 m warp moved the copy (read back, `first=1` a take);
    `soldier_act_plan` 300 ms after each plan (the path end against R+0x80); frame counters.
  - contract (`tools/generate_soldier_act_contract.py`, 31 sites, 13 slots; 34 sites with track A's 3): 0x13725f0 (prologue through the gate, the
    move hash, the planner call / R+0x19c / vt[0xd0]), 0x136f0e4, 0x136f156, 0x136f203, 0x136f759, 0x1372ba1, 0x1354a00,
    the planner's vt[0] head, vt[0x10], vt[0x18], vt[0x90], vt[0xd0]; vtable 0x23a1610 slots +0, +0x10, +0x18, +0x20,
    +0x90, +0xa0, +0xd0; data 0x20a8078 = 10.0f.
- **Gate** (the plan's, per guard, calm frames): walkers' place error while the owner copy moves p50 <= 1.0 / p90 <= 2.5
  m; 15 m warps while a puppet <= 1 a run, take warps <= 2; speed ratio >= 0.8 for every walker with >= 20 moving
  samples; `plan` > 0 with the path end within 1 m of R+0x80 for >= 90 % of plans; matched host / guest frames of one
  mirrored guard walking at one spot; no guest-only ChangePhase in calm that a puppet started; 0 faults / latches / game
  overs; frame p95 within + 0.5 ms of mirror off.
- **Not reached**: the `act_plan` session `afeddbdc` was void: after about 80 s the host's game sat under the modal "You
  have signed out" (every captured host frame), its request manager update stopped before the guest paired, so no
  SOLDIER_ACT part was sent and no guard was a puppet [O]. Reference without the plan (`fcfb3acc`, build 946864a, before
  the slot reads of 49770205): place error p50 4.75 / p90 13.30 m, calm warps 3 takes / 3 held, frame p95 + 0.45 ms with
  calm-only puppet frames (+ 0.19 ms over the whole run), no matched frame pair (STATUS).
- **Next**: one rerun of the `act_plan` session; if the plans land (path end within 1 m) but the error stays, the
  re-apply cadence (1 s) restarts each plan from where he stands: plan only on a changed request or a destination moved
  over 1 m.

#### 7.24 Enemy round 9 track A: the quiet move only for a guard at rest, the partner's network pose while range-hidden (lab keys `quiet_place` and `range_flat`, native only)

Markers: [O] seen in a log line, a frame or the code (Ghidra decompile, read only); [I] inferred; [U] unknown.

- **Why round 8's quiet move failed** [O 1befcef4, re-counted with the round's gate script]: 114 of its 115 quiet
  moves came in the ALERT part (enemy-latch-alert), 1 in calm (enemy-integ-carry). Per move, the 0.5 s and 2 s
  read-backs were both within 1 m for 17 of 92 (18 %; enemy_div_report `moves_read` 92). Guard 1904171355 (43 moves) ran with his host copy at 2-4 m/s;
  after each warp his own brain kept its path and he walked on (22 `place_sink`). In 5 of the 8 off-target moves the
  body did not move: 0x14136ece0 writes the slot first, then moves the body only for an idx it finds in the u16 list
  `*(S+0x18)+0x28` (count u32 `+0x44`), and returns 1 either way [O decompile].
- **Built** (`quiet_place`, native only, no CFG bit; still off unless named):
  - the request manager hook keeps a rest view of every realized guard, refreshed every 250 ms before the engine's
    update: in the realized list or not, his slot speed on the ground plane over about 0.5 s, and his follower's path
    (planner vt[0x20], follower vt[0x90] has / vt[0xa0] last point; track C's pins, read only);
  - `QuietCheck` (the guest's warp receipt) takes the quiet move only when that view is at most 1.5 s old, the guard is
    listed, his speed is at most 0.5 m/s and no path of his ends more than 2 m (x/z) from both him and the target; any
    other guard gets the set-transform message as in 0.15.2 (`world_place=warp ... quiet=<why>`). This is the task's
    "send the move only when the guard is at rest in his own brain"; stopping a walker (planner cancel, a new route
    node) was not built: its effect on a brain-driven guard is [U];
  - `QuietOne` checks the list again just before the warp (not listed: no warp, `moved=0`);
  - a third quiet read-back at 5 s (`at=5000`; the walk-on row); report rows `quiet.why`, `moves_both_within_1m`,
    `walk_on_within_2m_5s`. Contract: `slots_list` / `slots_list_count` (pinned by the existing site 0x136ed8c).
- **Bug 2's guest pose** (on at the defaults at 554b2fa9; off unless `range_flat` is named since the land step, see
  below): `soldier_warp::RemoteRangeSource`. While the host hides the
  guest's body for range (`partner_range=hide`, beyond 200 m) the body driver has no command, and the receipts and PEER
  read the guest's pose as unknown (8b581a26: position 0, 142 of 142 refused). Now the guest's network pose sample (at
  most 1 s old) stands in for the host's M3 placement range, the soft-follow range and PEER (the owner rule), on both
  roles; `partner_range_pose=` when the source changes, `host_warp ... g_src=`.
- **Gate** (named, `quiet_place=1`): per quiet move, the body read-backs (GetPosition, x/z and y) within 1 m at 0.5 s
  and 2 s for >= 90 %, the 5 s read-back within 2 m (x/z) for >= 90 %, 0 kind 3 / 13 caused by a quiet move, no
  guest-only ChangePhase in calm, 0 place_sink / drive_sink / guard_fell, 0 latches / faults / game overs.
- **Session 1, `0f020398`** (`quiet_place=1, act_census=1`; enemy-prove-1-join, enemy-latch-quiet, enemy-integ-carry,
  enemy-latch-alert, gun profile, research build, afgh-ledge, `-WanSim bad`; all four parts passed) [O]. The first try
  (`406324b6`) was void: the worker lost the input desktop (`input_desktop_unavailable`, error 5) after the host reached
  gameplay, every step was refused and the guest never started.
  - GATE FAIL (n = 2): the guest asked 18 times for 6 guards (3970963356 x7, 2305481811 x6, 1904171355 x2, three
    with one each): `moving` 14, `path` 2, `ok` 2; 17 asks in the ALERT part, 1 (`moving`, 2929558510) in part 3
    (calm); 4 of the message asks were refused by the engine (`range`) anyway. 1904171355: 0.5 s 0.03 m, 2 s 7.74 m,
    5 s 20.39 m (5 s held=0, 1.79 m above); 2961503 (the guard put to sleep in the quiet part): 0.10 / 0.02 / 6.14 m.
    Both 0.5 s and 2 s within 1 m: 1 of 2; the 5 s row 0 of 2. The 5 s row measures from the original target, so it
    does not test the rest rule: 1904171355 was soft-follow driven after his running host copy (`place_div
    why=driven`, 4.6-6.2 m; the host copy moved about 24 m in those 5 s), and the scenario's own `enemy goto 2961503
    8` put the guest 8 m beside 2961503 1.3 s after the move (he went ka 8 to 18 and ran at about 3 m/s). The FAIL
    stands, but it is no evidence that the guards walked on along their own paths.
  - Kind 3 entered within 0.5 s after a quiet move 0 of 2 (the 16 message corrections with the census: 13); hollow:
    1904171355 was already at ka 3 from a message correction (`quiet=path`) 2.2 s earlier and 2961503 went ka 15 to
    27, so n is at most 1. PASS:
    0 `place_sink` (1befcef4: 22), 0 `drive_sink`, 0 `guard_fell`, 0 latches / faults / game overs; one guest
    `req_phase` (phase 3, in the ALERT the host's own script set). Guest place error p50 0.59 / p90 1.49 m.
  - Reported: host M3 p50 1.74 / p90 10.74 m; before the guest's fall p90 5.74 m. The guest fell through the world in
    the ALERT part again and was down 27 s: host error while down p50 16.2 / p90 27.2 m, 2 applied / 57 refused.
  - The pose fix ran [O]: the host hid the guest at 201.7 m; 114 host placements read `g_src=net`: 58 refused
    `near=host range` (57 inside the down window [356258764, 356286107], 1 just before; the 3D range, `range_flat`
    off) and 56 applied `near=guest` (54 after the window, 2 inside it), the host still hiding the guest's body until
    `partner_range=near` at 119 m. Without the fix all 114 would have read the guest's pose as unknown [I from the
    code; 8b581a26].
  - Frames looked at: q-0 / q1 (both in gameplay, no modal), c-b (guest: the sleeping guard lying by the barriers
    beside the guest; host: only his ZZZ marker beside the guest's crouched character between the barriers, the body
    itself hidden), al-1 (both in the ALERT: the radio subtitles).
  - Not in the round's notes before the review: truck 25605 broke on the guest (VehicleBroken, att=none) 3.8 s after
    message corrections of its two crew guards and was destroyed on the host through vehicle_gone; a terminal_win for
    2929558510.
- **Decision**: quiet_place stays off unless named (its gate failed). The rest rule ends the walk-off seen in round 8
  (0 place_sink) only by refusing almost every quiet move: corrections are needed because guards walk, and a walking
  guard now gets the message, its kind 3 and the radio risk of bug 1 as in 0.15.2.
- **Session 2, `dee3baf6`** (no key named, code head 554b2fa9, the same four parts) [O]: parts 1, 2 and 4 passed; part 3
  (enemy-integ-carry) stopped before the carry: the harness threw ("[host] viewer landing has no new position sample")
  because the host's state tick did not advance within 250 ms after the scenario put the host behind the guest; the
  host's return_count cadence has 454-547 ms gaps right after that teleport in both no-key runs and none in `0f020398`
  or `c784a5a7`, so a host-side stall is as likely as a harness fault (cause [U]; 2 of 2 no-key runs at 554b2fa9, 1
  earlier case in the archive). No carry row. First frames q-0 on both copies in gameplay (not void). No quiet move (key off). Host `place_sink` 1: the host's
  copy of 3268415846 4.89 m under after a landed placement that read the driver's command (`g_src=cmd`; round 8's
  `guard_fell` was this guard too) [O; cause U]. 0 `drive_sink`, 0 `guard_fell`, 0 latches / faults / game overs.
  Guest place error p50 0.76 / p90 4.29 m (the p90 <= 5 m row passes; a later goto took the guest 727 m off to the
  reinforcement guards' copies in their truck, track B's open row); 17 guest `req_phase`, all in the ALERT part (2
  phase 3; the 15-outpost phase 1 burst at t 357536189 came right after `enemy goto 1179373153 8`, about 30 s before
  the 727 m goto, and the same burst is in 68491f7f (0.15.2) and c784a5a7 (0.15.1): pre-existing). Host M3 p50 1.15 /
  p90 7.82 m, host placements applied 60 / 90: this FAIL came from the pose. cmd placements 59 / 59 applied; all 30
  refusals read `g_src=net` `unrealized` (12 engine relays, 18 backoff) for three guards 729-754 m from the host that
  became guest-owned only through the net PEER; host M3 error inside the hidden window p90 23.9 m (65 samples) against
  5.5 m outside. Of the 31 `g_src=net` lines the one applied was a place_body echo: the host moved its copy of the
  guest-neutralized 2305481811 766.7 m to the guest's spot.
- **Session 3, `d3c4e4f6`** (no key named, the same code head; the rerun for the carry row) [O]: part 3 stopped the same
  way before the carry (the host's viewer landing had no new position sample); parts 1, 2 and 4 passed; q-0 on both
  copies in gameplay. 0 `place_sink` / `drive_sink` / `guard_fell`, 0 latches / faults / game overs. Guest place error
  p50 0.82 / p90 3.91 m (21 corrections refused `far`: the far reinforcement guards again); host M3 p50 1.03 / p90
  4.39 m (its 5 m row passes), placements applied 89 / 99 (10 unrealized); 4 guest `req_phase`, all phase 3 of the one
  outpost inside the scripted ALERT. No host placement read `g_src=net` (no placement while the guest was hidden).
  A place_drop for 2929558510 (3.56 m under, owner -0.06 m; excluded from the sinks by design). The carry row is not
  reached in either no-key run, so no complete no-key pass exists at this head; the named run `0f020398` at the same
  code head passed that part (cause [U], above).
- **Land step (the review)** [O code, tests; not run]: the range pose is off unless `range_flat` is named
  (`RemoteRangeSource(..., net_allowed)`; red a063b20c, green 22931f5d): (1) `DriveReceipt` still set its range point
  from the command pose (0,0,0 while hidden) when its nearer test, on the net pose, chose the partner, so every such
  soft-follow drive was refused `range`; this also hit the pairing window, since `range_hidden` starts true at every
  30010/30020 mission start (`partner_range_pose=net` at mission start in all runs, both roles), where a guard within
  60 m of the local player but nearer the partner was driven before [I code]; both receipts now take the point from the
  pose the nearer test used (`soldier_warp::RangePlayer`). (2) It made the `dee3baf6` host placement FAIL (above).
  (3) No complete no-key run backs it. `QuietOne`'s realized-list read is now under SEH (`soldier_act::ListFind`; it was
  outside the `__try`, reachable only with `quiet_place`).
- **Next for bug 1**: the quiet move must stop or re-route a walking guard: read the brain's own walk (the planner slot
  of idx: hash, handle, done; track C's session 1), then try, one at a time in a census run, (a) planner vt[0x10]
  cancel of his live handle right after the quiet warp and log whether his brain plans again from the new spot within
  1 s (follower path end, slot speed), (b) the request start 0x14136f090 mode 0 stand command offline. Measure at the
  seed's calm correction (enemy8-quiet-calm: the 246 case) and with more calm corrections (no calm correction was asked
  in the 4-part set). Log the speed in `quiet=moving`.
- **Next for bug 2**: the fall through the world still refuses the host's placements while the guest is under the
  world (3D range); `range_flat=1` named (it now brings the pose and the drive fix) is the next run (enemy8-down-alert
  reproduces the fall; it happened in 0f020398's ALERT part too), then one no-key run before any default change. The
  quiet gate's 5 s row should measure from the owner's copy, not the original target.

#### 7.25 Enemy round 9 track B: the patrol trucks' crews (lab key `lrrp_follow`, native only; own chunk `world_lrrp.lua`)

- **Chunk**: lrrp_follow moved out of `world_vehicle_state.lua` (30887 of 32000 LF bytes) into `lua/coop/world_lrrp.lua`,
  spliced right after it (CMake part `LRRP`, `world_lrrp_source`, the bootstrap splice, the host trial and composed checks).
  The wire is unchanged (LRRP_FAST 0x5D, protocol 38 / 42). Off unless named; no CFG bit.
- **What changed on the guest** (red first: 99cf1044, aed13563, 43f25a8c; green 4f22843d, a0d3f1dd):
  - Never `StartTravel{travelPlan=""}` (round 8's unseat ended the crew's LRRP plan: 25606 drove off empty). At rest, a
    rider this copy has and the host's truck no longer has gets `UnsetRelativeVehicle` 2 s after the host's crew got out,
    again at most every 5 s (`step=unseat; id; ent; k; rh; rl`).
  - First form (4f22843d): `SetRelativeVehicle{targetId, rideFromBeginning=true}` (TppEnemy.AssignSoldiersToCP's LRRP seat)
    3 s after the host's crew boarded. Session 1 showed it seats nobody mid-mission (below), so a0d3f1dd removed it: while
    the host's truck moves with riders and this copy's known crew is out, ForceStop is lifted and the truck is not placed
    (`step=crew_wait`): this copy's own AI keeps it and its crew boards it on its own clock; it is taken again once manned.
  - Jumps: within 400 m of this player (either end) a move over 30 m waits (`step=defer`; the truck stays this copy's own,
    as 0.15.2); 5..30 m slides 1 m a frame (`step=catch`); farther out it jumps (`step=jump; ...; near_m`).
  - Lines: `step=crew; id; ent; x; z; in; rl` (each crew member this copy has seen seated, 1 Hz for 60 s after its
    rider count or, on the guest, the host's changed; both roles), `step=cost; role; n; avg_ms; max_ms; tracks` (os.clock
    per tick, every 10 s), `step=lever; on; role`.
  - Lever (both roles, any mode): `debug vehicle_state_status lrrp 0|1` pauses / resumes lrrp_follow on that copy (the
    frame-time window of a hidden run).
- **Session 1** `68491f7f` (`lrrp_follow=1`, 4f22843d; join, `enemy-lrrp-crew.txt`, `enemy-lrrp-alert3.txt`; all parts
  passed; first frames of both copies in gameplay, no modal) [O]:
  - Truck error while followed p50 0.16 / p90 0.57 / max 5.33 m (1039 samples; 25605 p90 0.66, 25606 p90 0.53), |dy|
    p90 0.05 m. Jumps within 400 m of the guest: 0, by construction (the code jumps only at 400 m+). 25605 waited (18
    `defer` lines before its first jump, near_m 150-400) and jumped 366.9 m at near_m 400.0, again 99 m (590 m away)
    after the lever window.
  - The board did not work: 50 `SetRelativeVehicle` (k=1..50, 25 per guard) to 25606's two guards, `rl` stayed 0; the
    guest's 25606 drove off empty for 123.2 s while the host's carried its crew (the row the gate needs at 0).
  - `UnsetRelativeVehicle`: cause unknown [U]: the parked trucks' guest crews got out 1.1 / 3.6 s after their 4th / 3rd
    unseat round, 25606's 64 s after the host's and within 9 s of its last unseat burst.
  - 25605 near the players: its guest copy was its own (deferred) on the approach, so its riders were p90 426 m apart and
    its crew got out 53-73 s after the host's; the guest's place error p90 15.03 m comes from these two guards (29
    `place_div` over 8 m each, 16-48 m), 11 corrections refused `vehicle_local` (they still sat in the guest's truck).
  - Frame proxy (partner_tick hz, 4 windows of 10 s each): on 24.4 / off (lever 0 on both) 22.7 / on 23.3 ms p95 (+1.7 /
    +0.6 ms); `step=cost` avg 0.05 (host) / 0.15 (guest) ms per tick, max 2-3 ms (os.clock's 1 ms step).
  - Report: 0 latches / faults / game overs / drive_sank; M3 host p50 1.67 / p90 4.96 m; corrections 16/33.
  - Frames k-5 / k-6 (both binoculars west at the stop): rock and terrain, no truck in view on either screen.
- **Session 2** `801cd90b` (`lrrp_follow=1`, a0d3f1dd; the same parts; all passed; first frames in gameplay) [O]:
  - Truck error while followed p50 0.17 / p90 0.61 / max 4.62 m (708); 25606's seated crew p90 0.53 m; jumps within
    400 m of the guest: 0 logged (25605's first jump, ~368 m at ~356721333 with the guest ~397-400 m away, was dropped by
    the guest's `log_cap`; its logged jumps 510 / 645 m); 0 latches / faults / game overs / drive_sank.
  - Empty truck (the review): 54 `crew_wait` lines (398 follow lines in mode=wait) held 25606 while the host's drove
    manned, but at 356989579, the host's truck paused (v=0), the guest's EMPTY copy (rl=0, rh=2) was taken again and
    jumped 100.5 m to (-1689.36, 969.78), pinned 9 s, released (`crew_out` at 356998672); its crew walked NE on foot (to
    -1564, 958). Its guest crew had got out at 356953086, 6.4 s after the mod sent `UnsetRelativeVehicle` during the
    host's boarding (k=25, rh=1, rl=2 at 356946676), and did not board again (~110 s). Both fixed at the land (below).
  - Crews 20-40 s after the host's get-out (step=crew on both copies, interpolated): 25605's 2305481811 p90 25.3 m and
    3970963356 p90 42.3 m (the guest's copies still in the guest's own, deferred truck: they got out 51-70 s after the
    host's); 25606's two 9.3 / 10.9 m (the guest's copies still seated in the pinned truck, the host's on foot).
  - 25605's riders p90 427 m (deferred approach, as session 1); guest place error p90 13.32 m (these two guards),
    corrections 13/23 (10 `vehicle_local`), M3 host p50 1.97 / p90 7.54 m.
  - Frame proxy p95 on / off / on: host 23.8 / 23.3 / 22.2, guest 24.4 / 25.6 / 22.7 ms (session 1 24.4 / 22.7 / 23.3 on
    both): within 2 ms of the off window (4 samples of 10 s each per window); `step=cost` avg <= 0.13 ms per tick.
  - Frames k-8 (both binoculars aimed at 3970963356): the host's view SW with guard markers 38-95 m, the guest's W at a
    rock: the rider is not in the same direction on both screens [I], no truck in view.
- **Session 3** `d7b3a81c` (no key named, the final code head dc6dabd3; the same parts, the key off; all passed) [O]: no
  `event=lrrp; step=on` on either copy, VEHICLE_STATE on (`lrrp=6`), 0 latches / faults / game overs / drive_sank, guest
  place error p50 0.67 / p90 1.85 m (25605's late guest crew stays beyond the row's 60 m), corrections 16/43 (26
  `vehicle_local`), M3 host p90 6.97 m; frame p95 over the session 25.6 / 25.0 ms (host / guest), the named runs
  25.0-25.6 ms. Frames al-3: both copies in play (the host crouched south with guard markers, the guest in the outpost).
- **Gate** (Afghanistan; Africa 30020 not run: seed `africa-jeep` exists and `enemy-lrrp-africa.txt` is ready) [O]: PASS
  truck error while moving, no jump within 400 m (by construction), 0 driving empty on one screen (session 2; but one empty truck moved, below), 0 latches / faults / game
  overs, frame time (weak); FAIL riders within 8 m while seated (25605, deferred near the guest), crews within 5 m 20 s
  after the get-out (9-42 m), the guest's crew boarding when the host's does (it boards on its own clock or not at all).
  `lrrp_follow` stays off unless named. Corrected at the land (the review): the no-jump row passes by construction (the
  jumps sit at the 400 m boundary), and session 2 had one empty truck moved (the 100.5 m jump at a host pause), so that
  row FAILs too.
- **Land fixes** (red ee9f0367, green e5cbe4c6; off unless named, not run): `crewless` (no place, ForceStop lifted) also
  holds at rest when the host's manned truck is 5 m+ off this copy's empty one; once the host's rider count rises at a
  stop (`tr.hn`, `tr.boarding`), no unseat until the host's truck drives. Not fixed (debug only): `LF.paused` survives a
  mission start; the scratch gate tool printed only 40 lines per command list and skipped jumps of empty trucks.
- **Log volume** [O]: named, lrrp lines are 81-83% of the guest's world log (4947 / 6074, 5202 / 6236; 41-47 lines/s
  against the 20/s budget in `native/world_link.cpp`); `world=log_cap` dropped 224 / 340 guest lines (all modules).
- **What blocks it** [O unless marked]: the guest's crews run their own LRRP clock and no command tried moves it:
  `SetRelativeVehicle{rideFromBeginning=true}` seats nobody mid-mission (50 tries), whether `UnsetRelativeVehicle` causes
  the get-outs is unknown [U], `StartTravel ""` (round 8) gets them out but ends the plan. Near the guest the truck
  waits (no visible jump), so the truck the players see is still the guest's own, ~72 s late, as in 0.15.2.
- **Next**: (1) find the soldier's LRRP step in the exe (the travel plan cursor of SetLrrp / StartTravel) and set the
  guest copy's to the host's at the pairing and at each get-out / boarding, instead of commanding seats; or (2) put the
  guest's crew under the M1..M6 corrections as host-owned on foot for 60 s after the host's get-out (P.ownerOf from this
  chunk) and seat them with the vehicle's own get-in action; (3) a jump hidden by the camera (outside the 100-degree view,
  60 m+) instead of waiting within 400 m; then run `enemy-lrrp-africa.txt`.

#### 7.26 Enemy round 9 track C: the far mirror's walkers plan once per leg (lab key `act_plan`, native only; with `soldier_act`)

- **Re-run of 7.23's void session** (`226fc789`, the round 8 build): the plans land (100 of 104 path ends within 1 m of
  R+0x80, 4 with no path yet at 300 ms) and the walker's place error fell to p50 1.27 / p90 2.91 m (guard 18, the one
  calm walker taken; 8.88 / 14.36 m without the plan; walkers 15, 17 and 246 were walking at take time and never taken,
  17-45 m off). What is left is a trail of about 1 s: every moving owner's 1 s re-apply restarted the
  request (the engine's start cancels R+0x19c) and planned again (one plan per 1.01 s), each restart cost a little speed
  (0.93 in the first 250 ms), and the catch-up raised the gait only beyond 2 m [O logs, I cause].
- **Plan once per leg** (`act_plan`; red 45670276, green 8447ee9b): in `MirrorRx`, a due re-apply of a walk (wire +0x61 ==
  1 on both requests) is held (no unpack, no restart, no new plan; `plan_hold`; the moving / still re-apply timer restarts
  and the planned request stays the one compared) when `PlanHold`: not a first apply; `RequestChangedFar` is false (the
  request equals the planned one but for the destination wire +0x40 moved within `kPlanMoveM` = 1 m; any other change is
  a change as before); `PlanLiveFor` (our last plan's handle is in R+0x19c, his planner slot holds `kMoveHash` with that
  handle, and its 300 ms check did not find the path end far or missing); the unpack's 15 m warp would not move him; and
  the catch-up gait now (from the last unpack's gait) equals the gait R holds (each command copies the gait from R when it
  is issued, so a gait change needs a restart). An unpack forgets our plan (the engine's start cancels it); `PlanStep`
  remembers each new one. The catch-up starts beyond `kPlanCatchM` = 1 m (was 2 m, the engine's own mode 0 onset).
- **Seen with it** (`77785f31`): 172 held re-applies; a caution runner kept one handle 7.3 s [O]. Long caution walks
  (64-140 m) got a partial path first (end 1.2-94.2 m short at 300 ms; a later plan reached R+0x80) [O]; a partial path is
  not held, and runners 246 and 17 stood while re-planned every 1 s, then jumped 16 m [O]. The release freeze [O]:
  `PlanStep`'s release cancels our slot and writes R+0x19c = 0xffff but leaves the walk request in R, so five released
  walkers stood 34-48 s (ka 1 / dm 1, empty slot) while their host copies walked on; open. Calm walkers unmeasured in that run (calm ended after 42.5 s by the guest's own guard 246's startle).
- **Calm walker with the hold** (`c0c3d088`, `quiet_place=1` named; the seed's place correction was not applied, so calm
  lasted the whole watch): guard 18 p50 1.38 / p90 1.98 m (121 samples), ratio 0.95, 9 plans for him (98 without the
  hold), 63 held, 2 catch-ups; he leads the host copy (along-track p50 +1.18 m, 89 of 119 samples nearer the destination,
  error smallest at about -1 s): the hold removed the 1 s trail; 11 of 13 plans within 1 m (2 had no path yet at 300 ms) [O]. The p90 row passes, p50 does not.
- **Gate** (7.23's, per guard, calm frames): not reached; see docs/STATUS.md (2026-11-05, enemy round 9 track C).
- **Next**: not a lead (he already leads with the hold); fix the release freeze (hand the walk request back at release),
  take walkers that are walking at take time, hold a partial path while it grows, and a spot where a calm walker is in
  clear view from both players for the frame pair.

#### 7.27 Enemy round 10 track A: a guard on a wall at the same height on both screens (lab key `drive_vert`, native only)

- **What round 7 left** (7.20): no vert snap of a copy within 60 m (the host stood 67-73 m from the wall guard
  2929558510), 0 of 3 read-backs held, and both snaps chased an owner copy going down 2.40 m, then up 1.88 m 1.7 s
  later. At the shipped defaults the copy of a guard on the wall top was left 2.2-3.3 m under its owner copy in 4 of 6
  old tests (6348f361, 41f2b07f, 31a799e3, b77e2104; `drive_sink`, all in the alert, ~30 s after the guest's first goto).
- **The owner at rest** (native `DriveReceipt`, `sw::OwnerYAtRest`): a due vert snap (the copy at rest in x/z more
  than 1.5 m above or below the rendered target for 1 s) is taken only once the rendered target's y (the owner copy,
  rendered 100 ms behind) has stayed within 0.2 m (max - min) for 1 s; a wider span, more than 600 ms between drive
  calls of that copy or a non-finite y starts the window over. A due snap that waits logs `world_drive=vert_wait` once per
  due spell; the vert line adds `rest_ms`. The rule tests y only: a guard walking on level ground counts as at rest.
  The snap is a visible jump of the copy (4.2-5.8 m in the runs below, once 3 m in front of the guest). The no-thrash
  cap and the read-backs (7.20) are unchanged. 629f0a42 red /
  fbd3def4 green (`soldier_warp_tests`).
- **Scenario** `enemy-vert-near.txt`: `enemy-vert-alert.txt` with the host at 415.80 269.0 2106.92 (a logged teleport
  landing, 7c174346; 31 m from the guard by the aim readout) before the alert and through block 2; in block 2 the guest
  teleports onto the wall top at its logged spot there (373.86 277.2 2120.18, 3e2611fc; the first run's `enemy goto
  2929558510 3` left the guest at the wall's foot, 0463d36d), both cameras on him (aim_ent, the host through binoculars),
  v-a / v-b / v-c; then the host back to the quiet spot for blocks 3-9.
- **Runs** (play, `-WanSim bad`, the gun profile, research build, afgh-ledge; join, latch-quiet, integ-carry, vert-near):
  `c59ae699` (`drive_vert=1`, 0.15.2+fbd3def), `948c978a` (`drive_vert=1`, 0.15.2+0463d36), `ef9584e4` (no key, at the final code head 0463d36d).
  Two earlier jobs stopped in the worker's own audited CTest before any game (`partner_defer_tests` check 185;
  `ddt_core_tests_1` CloseDuringSetup: timing tests, both passed in this tree's build).
  - [O] 3 vert snaps of 2929558510, all within 60 m, all held: `c59ae699` the guest's copy (far 3 m, gap +4.24 m,
    `rest_ms` 1085; read-backs 0.00 m at 0.5 s and 2.01 s) and the host's copy (far 31 m, gap -5.79 m: the guest's
    copy, then the owner, had gone down to the wall's foot; `rest_ms` 1058; -0.02 m at 0.5 s and 2.07 s); `948c978a` the
    guest's copy (far 49 m, gap +4.44 m, `rest_ms` 1178; 0.01 m at 0.52 s and 2.24 s). 0 `vert_stop`.
  - [O] FAIL `drive_sink` 0: the guest logged one in each named run (2929558510 -3.30 m / -3.48 m, low 2.1 / 2.3 s), in
    block 1 while the host owned him: each time a `vert_wait` came first on the guest (gap 2.50 / 3.48 m, `rest_ms` 0:
    the owner still climbing; `c59ae699` logged a second one, on the host before its down move: gap -2.42 m, `rest_ms`
    197, span 17 cm), the host's copy then rose 1.0-1.7 m more and came to rest at the top about when `drive_sink`
    fired, and the snap followed 1.1 / 1.2 s later. The planar error stayed 1.06-1.07 m throughout [I: a ladder: the owner climbed
    with x / z still]. So the copy was under for about 3.2-3.4 s and then on him. With the rest rule no snap can come
    sooner: the climb alone kept the gap over 1.5 m for more than 2 s, which is `drive_sink`'s own threshold.
  - [O] PASS 0 `place_sink`, `guard_fell`, latches, faults, game overs, roster mismatches, overflow (both runs).
  - [O] frames: `948c978a` v-a..v-c: the guest on the wall top sees him standing on the wall top about 11 m ahead (aim
    10.9 m, 6.1 deg down); the host's binoculars (31 m, 12.9 deg up, about 6.9 m over the host's camera) show only his
    X-ray outline through the wall, at its top under his marker. The snap seen is the guest's copy, 4 s before the guest
    took him over (the host's copy was the climbing owner, not snapped). Both at the wall top [I: from the two aim
    readouts, his aim point about 277 m on both]. `c59ae699`: the guest at the wall's foot saw only the wall (aim 2.8 m, 57 deg up);
    the host's binoculars showed the silhouette at the top.
  - reported: guest place error p90 1.81 / 1.66 m; host M3 p90 15.09 m (6.55 m before the guest's down) / 7.68 m.
  - [O] no key (`ef9584e4`): the guest's copy under the host's (owner) copy in block 1 again (`drive_sink` -4.54 m, low
    3.5 s) and nothing moved it until the guest became his owner 1.0 s later; then both copies were on the ground (the
    host's binoculars, the guest's aim). PASS 0 latches / faults / game overs, guest p90 1.17 m; FAIL `place_sink` 1
    (3268415846 with his owner 4.89 m lower too: down a ladder on both [I]); host M3 p90 6.94 m.
- **Defaults** (round 10; flipped on below): `drive_vert` stayed off unless named: the `drive_sink` row failed in both named runs (above), and a flip
  needs its named run to pass. User decision D-A10 (docs/STATUS.md): turn it on with that row read as "left under after
  his height has stayed level for 1 s" (the copy waits below about 3 s while he climbs, then jumps to his height; at the
  defaults nothing moves it until the other PC takes him over), or keep it off.
- **Next**: if the user says on, the flip is one line (`if (!c.drive_vert_given) c.drive_vert = gun;`, the lab_config
  test red first) and the no-key run of this batch. Otherwise a `drive_sink` that counts only while the owner copy is at
  rest in y would measure the bug itself, not the climb (a detector change: needs the user's or the reviewer's yes).
- **Flip (enemy flip A-vert, user decision D-A10, 2026-11-05)**: `drive_vert` follows the gun profile in lab and play
  (`if (!c.drive_vert_given) c.drive_vert = gun;`; a named 0 opts out). fba2a7b8 red / 8937f6eb green (`lab_config_tests`).
  The `drive_sink` gate row is read as D-A10 says: `tools/enemy_div_report.py` gates **vert left under 0** = a copy more
  than 1.5 m under its owner copy for more than 2 s (drive_sink's own window) counted from the moment the owner copy had
  been level in y for 1 s, per guard with the time, from the native lines (a vert snap up: `rest_ms` - 1000, capped by
  the time he was known under; a `vert_stop` up: until his next snap or the log end; n/a when copies sank but no
  `world_drive=vert` line gives the owner's rest); `drive_sink` stays in the report as information. ddabed29 red /
  59c1b48d green (`tests/enemy_div_report_check.py`). On round 10's runs: `c59ae699` / `948c978a` PASS (the snaps came
  58-178 ms after the owner had been level 1 s), `ef9584e4` n/a (drive_vert off).
- **No-key run at the flipped head** `11b73082` (59c1b48d, 0.15.2+59c1b48; join, latch-quiet, integ-carry, vert-near,
  latch-alert; lab settings `nametag=1` only) [O]: `drive_vert` on without being named (the guest's `world_drive=vert`;
  stats `vert=1`; on the host only [I]: same loader and profile, but no host `vert` / `vert_wait` / `vert_stop` /
  `drive_sink` line, as it drove no copy a storey off within 60 m of the host). One snap of 2929558510, the guest's copy 2.41 m under the host's (owner) copy, far 33 m, `rest_ms`
  1352; read-backs 0.00 m at 0.52 s and 2.00 s; 0 `vert_wait`, 0 `vert_stop`, 0 `drive_sink`; vert left under 0. 7 s
  later the guest teleported onto the wall top and took him over: the guest sees him standing on it about 12 m ahead
  (aim 12.2 m, 3.3 deg), the host's binoculars at 31 m show his X-ray outline at the wall's top under his marker (v-a).
  0 latches / faults / game overs / roster mismatches / overflow / `place_sink`; one `guard_fell` on the host
  (1179373153 at 144 m, 11.9 m under, put back at once and held; the host logged no vert line); guest place error p90
  2.89 m (FAIL against the rounds' 2 m p90 gate; the tool's own p90 <= 5 m row passes: reinforcements 3970963356 /
  2305481811 arriving 374-397 m off, 1740692850 2.0-135.6 m apart (`why=far` rising 32.5 to 135.6 m over 28 s; his
  135.6 m sample is in the tail) and one 12.7 m sample of 3268415846, none a vert move: the place error is x/z); host
  M3 p90 6.57 m. The drive band is 60 m from the player whose screen shows the copy (`S.drive`,
  `lua/coop/world_snap.lua`), so a guard within 60 m of only one player is not moved up on the other's screen.
  `drive_vert` stays on.

#### 7.28 Enemy round 10 track B: the LRRP crews on the host's travel step (lab key `lrrp_follow`, native only; op LRRP_STEP 0x5E)

- **RE** (read-only Ghidra, exe 085c2f82...bb45) [O unless marked]: the TppSoldier2 command dispatcher 0x141356170 (this =
  S, the soldier subobject the soldier_warp chain reaches; S-0x20 = P). `StartTravel` (0x385d1071) reads `travelPlan`
  (string -> plan index, 0x141352130), `step` (number; the key string at 0x1423e7bd8) and `keepInAlert`, then calls
  T.vt[0x88](T, idx, plan, step), T.vt[0x98](T, idx, keep), 0x14136de40 (applies the step's CP and route group) and
  0x14136bc80, with T = *(S+0x48), the soldier travel / route component (`SetRelativeVehicle` is T.vt[0x110],
  `UnsetRelativeVehicle` T.vt[0x118]). `SetLrrp` also takes `travelPlanStep` (0x1423e7ca8) into the same T.vt[0x88].
  `StoreToSVars` (0x1413695a0; site 0x14136a3d9) writes plan = T.vt[0x78](idx) (0xff none) as its name hash
  *(P+0x6f8)[plan] into `svars.solTravelName[idx]`, step = T.vt[0x80](idx) into `svars.solTravelStepIndex[idx]`, and
  T.vt[0x90] as `solFlagAndStance` bit 18 (keep travel in alert); `RestoreFromSVars` (0x141367770) puts them back with
  the same T.vt[0x88] + 0x14136de40. So a checkpoint restore is "set this soldier's travel cursor", and
  `StartTravel{travelPlan, step, keepInAlert}` is the same call from Lua. 0x14136de40 keeps the last applied plan / step
  per soldier at *(P+0x6d8) / *(P+0x6e0) (written only there, through 0x141368620), so those bytes go stale during
  ordinary travel: read T (svars), not them. [I] svars row = GameObjectId & 0x1ff. [U] the hold timer at a stop lives in
  T; StartTravel restarts the step, so its timer starts at the call.
- **Route** (no native write, no new contract site): both copies call `StoreToSVars` (TppEnemy.StoreSVars' own call)
  once a second while a crew is known and read the crew's rows; the host sends LRRP_STEP on a change (5 s refresh); the
  guest sets a differing step with `StartTravel` while its truck follows the host's (30 m or nearer) or waits crewless, one
  per guard per 5 s and at most 3 per host step; while steps arrive it no longer sends `UnsetRelativeVehicle`.
  Protocol section 45. `step=follow` 1 Hz (was 4 Hz) for the guest's log budget.
- **Built** (red first): 5e2b0f3a / 20ac2166 (Lua), 3dce7916 / f71305ca (LRRP_STEP), d21096be (host lines 4 Hz moving),
  4cfc2002 / 2f365581 (3 tries per host step); scenario `enemy-lrrp-crew10.txt` (round 9's crew part without the 40 s
  lever window, which let each copy's own AI run off the host's step).
- **Sessions**: two jobs refused before the game by the worker's audited CTest (`ddt_core_tests_1` CloseDuringSetup, a
  launcher timing check; not this change), counted as one session; `ffd0248d` (`lrrp_follow=1`, a8c615a4; join,
  `enemy-lrrp-crew10.txt`, `enemy-lrrp-alert3.txt`; all parts passed; first frames of both copies in gameplay); the run
  with no key named at the same code head (below). [O]:
  - The clock: 24 `StartTravel{travelPlan, step, keepInAlert}`, 23 read back on their target step at the next 1 s read
    (25606's crew 0 -> 1 -> 2 -> 3 ...; plans `travelArea1_01` / `travelArea1_02`); the guest on the host's step after a
    host change p50 1.5 / p90 3.6 / max 12.7 s (21 changes). The svars rows matched (0 `tstep_miss`).
  - Only the 2 trucks the host saw manned (25605, 25606: 4 guards) were on the host's step. The other 4 LRRP trucks
    (25602/3/4/7) had 0 riders on the host's copy and 2 each on the guest's: the host sends no LRRP_STEP for a crew it
    has not seen seated, and the guest unseated those 8 guards at the pairing (24 `UnsetRelativeVehicle`); their place
    error and a boarding after the unseat were not measured (none of the 4 moved) [U]. (Corrected at the landing review.)
  - Get-outs and boardings within 0.035-0.87 s of the host's (25605 2 -> 1 -> 0; 25606 2 -> 0 -> 1 -> 2; one boarding
    event), 0 trucks moving with the crews differing, 0 `crew_wait`; crews 20-40 s after the host's get-out p90
    0.00-1.75 m (round 9: 9-42 m, with a 40 s lever-off window crew10 removes; 25606's two guards stood still).
  - Truck error while moving p50 0.15 / p90 0.55 m (314). Riders within 8 m while seated: FAIL for 25605 in the first 54 s
    after the pairing (p90 499 m, 46 of 46 samples): its guest copy was 363 m from the guest's player and 410 m from the
    host's truck, so the 400 m rule deferred it (27 `defer` lines) until the jump at near_m 400.0; after that every rider
    p90 0.42-1.00 / max 5.3 m, 0 over 8 m (25606's riders: one 385-386 m sample each at its pairing jump). Allowing
    host line gaps up to 1.6 s (108 more moving lines), the truck error is p90 1.26 / max 6.13 m (n=423).
  - 7 of the 24 sets set a guard back: 4 one step back while the guest's copy led the host's by less than the step's
    latency (WanSim ~180 ms plus the 1 s reads; e.g. k=10 5 -> 4 57 ms after the host reached 5), then kept off the
    host's step 5.2-6.6 s by the 5 s spacing; 3 were 25605's driver 6 -> 0 while the host's driver stayed on step 0 (two
    reverted to 6 within 1.4-2.2 s, the third went to 7 by the guest's AI; then `why=tries`). So 3 of the 23 read-backs
    never held, and on a stays-on-the-step basis the lag p90 is about 6.3 s, not 3.6 s. The get-outs still lined up.
  - 0 `log_cap` on either copy (guest world log 7.1 lines/s, lrrp 5.4/s); `step=cost` avg <= 0.15 ms per tick with the
    1 s `StoreToSVars`; 0 latches / faults / game overs / drive_sank; one host `place_sink` of an outpost guard (not a crew).
  - Frames: no matched crew pair (the host's binoculars on rock in k-5..k-7); the guest's k-6 shows a soldier on foot
    beside a truck at the stop.
- **Gate** (Afghanistan): every row passes but "crews within 8 m while seated", which fails for a truck within 400 m of the
  guest at the pairing (that copy stays the guest's own, as 0.15.2, until the guest is 400 m from it); the rows cover only
  the 2 trucks the host saw manned, and crew10's frame-time row has no off window (cross-run only). Known key-only
  defects [I]: the unseat skip holds while any step arrives (the host refreshes every 5 s), even when `StartTravel` is
  held; no phase / notice gate on `StartTravel`; `StoreToSVars` 1 Hz rewrites svars between checkpoints. `lrrp_follow` stays
  off unless named; turning it on in 30010 with that limit is the user's decision (docs/STATUS.md, D-B10).
- **Next**: hold a copy that is one step ahead for up to 20 s instead of setting it back; leave a guard whose host step
  never changes (the driver) alone; a pairing jump hidden from the guest's camera if the no-jump row allows it; Africa.

#### 7.29 Enemy round 10 track C: every calm walker taken, the released walk, the plan checked again (lab key `act_plan`, native only; with `soldier_act`)

- **Why** (round 9, 7.26): 3 of 4 calm walkers were never taken (`leg_wait` 775, `near_wait` 53 in `226fc789`): walking at
  take time, their copies were on another leg or more than 10 m off, and stayed 17-45 m off; 77785f31's five released
  walkers stood 34-48 s: the release cancelled our plan and left the host copy's walk in R (guard 18: R+0x80 the host
  copy's new caution walk 190 m away, R+0x19c 0xffff, slot empty, fh 0); 2-4 plans a run had no path yet at 300 ms.
  The path step 0x141372b70 (decompiled, read only) acts only with (R+0x19e & 3) in {2, 3} and a follower path
  (vt[0x90], vt[0xa0]); with no path it issues nothing, so a walk in R with no path never ends; at the path's end it
  sets R+0x19e bit 2.
- **Built** (native only, no CFG bit, no wire, no new contract site; under `act_plan`):
  - `TakeWait(plan, leg, near)`: with `act_plan` a walker is taken whatever his leg or distance (the plan walks him onto
    the host copy's path; beyond 15 m the take opens with the unpack's warp, `soldier_act_warp15 first=1`); without it
    `LegMatch` as round 6. `TakeNear` stays for the record.
  - `PlanCheckVerdict(path, end_near, tries)`: 300 ms after a plan and then every 300 ms up to 4 more times while the path
    is missing or ends beyond 1 m of R+0x80; the last verdict is counted (`plan_ok` / `plan_far` / `plan_nopath`,
    `recheck`; the `soldier_act_plan` line prints `tries`, `ms` from the plan). Only no path at all stops the hold.
  - `PartialHolds(partial, self, end)`: a path whose last check ended beyond 1 m is held while he is more than 3 m (flat)
    from its end; nearer, the next due re-apply plans again from where he stands.
  - `ReleaseChoose(dm, r19e, slot, r19c)` at a release: a walk (dest mode 1) with our live plan is kept (`keep`); with no
    handle, (R+0x19e & 3) == 2 and a free or own slot it is planned again (`replan`, the same shadow plan); not a walk:
    our live handle cancelled as before (`cancel`); his brain's type in the slot: left alone. `ReleaseWalkStep` ends a
    kept walk: `over` once R+0x19c or his slot no longer holds our handle (the engine's start of his brain's next request
    cancelled it; an orphan of ours in his slot is cancelled), `finish` (our handle cancelled, R+0x19c 0xffff) after 2 s
    stood within 1.5 m of R+0x80 or 180 s. Each step logs `soldier_act_release; step=keep|replan|replan_none|cancel|none|
    finish|over|over_cancel`.
  - The frame line adds `rel_keep`, `rel_replan`, `rel_end`, `recheck`; its buffer is 1024 bytes.
- **Gate** (scaled to what a player can see at 95-300 m: 7.23's p50 1.0 / p90 2.5 m became p50 1.5 / p90 3.0 m, the frame
  p95 + 0.5 ms became + 1 ms on capture-free seconds against the mirror-off part of the same run; per mirrored calm
  walker with >= 20 moving samples): place error p50 <= 1.5 / p90 <= 3.0 m; >= 3 of the calm walkers in range mirrored;
  15 m warps while a puppet <= 1 a run; 0 released guards frozen > 5 s; plans with the path end within 1 m >= 90 %
  (rechecked); a matched host / guest frame pair of one mirrored guard walking; no ChangePhase started by a puppet; 0
  faults / latches / game overs.
- **Seen** (`15aa58cb`; docs/STATUS.md 2026-11-05 enemy round 10 track C): 4 of 4 calm walkers mirrored; 17 and 18 within
  the gate (p50 0.55 / 1.18 m); plans 97.6 % within 1 m; 28 released walks kept (0 `finish`, 20 taken again within
  about 1.5 s), 0 with round 9's freeze signature. Not reached: 5 released stands > 5 s (guard 246 29.9 s straight
  after a keep release and 11.0 s after the mirror off, in calm); guard 15 leads by p50 3 m (the host copy walks the
  route's line, the plan the straight line to the leg's end), guard 246's guest copy stood at the spot his 27.1 m take
  warp put him (he walked 15 m and 28 m once released; cause [U]) and drew 2 held warps, frame p95 + 1.52 ms with 12
  puppets, no matched frame pair. 5 take warps of 18-49 m (round 9: 1): `TakeWait` takes walkers at any distance. `689e654c` (spot A): 17 and 18 within the gate again, 246 walked
  (1.57 / 3.13 m), 15 1.99 / 6.56 m; held warps 1; frame p95 + 0.20 ms on 20 s off / 20 s on with no capture; a matched
  frame pair of guard 17 walking (`w17-2`); but 6 released stands of 10.5-26.2 s, 5 with no path: at the end of the
  kept walk his brain set a new R+0x80 without a new request and planned nothing (guard 15, R+0x1a0 unchanged, R+0x19c
  and slot cleared); guard 18 stood 20.1 s straight from a release with no walk (`step=none`, dm 0); 5 take warps of
  18-49 m in calm and 5 at once (15.8-45.2 m) at the caution retake; 27 of 36 kept releases taken again within about
  1.5 s. Mirrored 42-89 % of the host-moving samples per walker in both runs.
- **Nits** (not fixed; `act_plan` only): `g_rel[]` / `g_planned[]` are not cleared on a mission or level change (a
  reused index within 180 s could have a stale handle cancelled); `TakeWait`'s `take_near` is unused.
- **Next**: plan a released copy's brain re-target that has no path (until his brain's own type shows in his slot); the
  host copy's path on the wire (or a plan through the owner positions); no take of a copy that cannot walk; no far
  take that opens with a warp in calm and no retake within 1.5 s of a release; clear the per-guard state on a mission
  change; then one
  named run, the flip per D-FM1 (a) and the no-key run.

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
