## World sync: script-level API map for MGSV 1.0.15.4

The game's own scripts provide enough to sync the discrete world state between the two PCs: who is dead, unconscious, fultoned or held up, alert phase, clock, weather, objectives, sequence and mission clear. They do not provide a way to push soldier positions every frame: the soldier command table has no `Warp` or `SetPosition`. A purely host-authoritative world will not work as-is (section 8 gives the evidence and a proposed hybrid).

Nothing here was run in-game. Code addresses come from a static read of the exe only.

### 0. What was extracted, and how

- **Paths.** Everything is under `W = C:/Users/taylo/AppData/Local/Temp/claude/D--Games-steamapps-common-MGS-TPP/e7a6e70e-f530-4f59-b4a3-60e868204a66/scratchpad/world`. Citations below are relative to `W/scripts/`.
- **Library scripts.** `scripts/lib/*.lua` holds all 53 modules listed in `Tpp.requires` (`lib/Tpp.lua:28`) plus `Tpp.lua`.
  - 26 of them came from the repo's `build/archive_probe.exe`. These are the patch copies in `master/0/00.dat`.
  - The other 27 (TppClock, TppMarker, TppRadio, TppRevenge, TppReinforceBlock, TppTrap and others) only exist in `master/data1.dat`. The repo tool does not scan that archive, and it also cannot pull `.lua` files out of `.fpkd` packages.
  - So I built a scratch copy of the probe (`W/probe2`, same datfpk dependency, adds `data1.dat` and dumps `.lua` entries). The repo was not modified.
- **Mission scripts.** `scripts/mission/{f30010,f30020,f30050,s10020,s10036,init}/`. f30010 is Afghanistan free roam, f30020 is Africa free roam, s10020 is mission 1 (Phantom Limbs), s10036 is mission 3, init is the init sequence.
- **Line numbers.** The library scripts are minified, so a line can hold several functions. Citations point at the line where the function starts. Mission scripts are not minified.
- **Exe evidence.** Fox `StrCode32` is the low 32 bits of `StrCode64` from the cached `unknown321/hashing` package. Command ids are compared as `cmp reg, imm32` in the `.xtext` section.
  - The method checks out: all 27 command ids that vanilla scripts use were found, and 3 made-up control strings were not.
  - Tools are in `W/hash/`: `hash.exe`, `disp.py`, `xdis.py`, `params.py`, `sites.py`.
- **Dynamite.** Its modified `Tpp*.lua` copies contain no script-level world sync.
  - The only changes are session lifecycle around the FOB path: `S_IS_ONLINE`, `CreateHostSession`, `CloseSession`, and the `IsCoop` list in `work/upstream-dynamite/moddata/standalone/TppMain.lua:99-183` and `TppMission.lua:715-737, 1089-1096`.
  - Its own channel (`hook/src/DynamiteSyncImpl/DynamiteSyncSchema.fbs`) carries only markers, `SyncVar`/`RequestVar` (svars by category/name), emblems and Boss Quiet. Its enemy world sync relied on the native FOB synchronizer, which we must not use.

### 1. Enumerating soldiers and command posts (CPs)

| API | Returns | Cite | When |
|---|---|---|---|
| Mission `enemy.soldierDefine` = `{cpName = {"sol_name", ...}}` | Static name lists per CP (f30010 has about 271 soldiers, `MAX_SOLDIER_STATE_COUNT=360`) | `mission/f30010/f30010_enemy.lua:19,122` | Any time once the script is loaded |
| `mvars.ene_cpList[cpId] = cpName`, `mvars.ene_soldierIDList[cpId][soldierId] = soldierName` (reverse id-to-name map), `mvars.ene_soldierDefine` | Filled by `TppEnemy.DefineSoldiers` | `lib/TppEnemy.lua:1422-1447`; called from `lib/TppMain.lua:184-185` | After `TppMain.OnInitialize` |
| `mvars.ene_baseCpList`, `ene_outerBaseCpList` (CP name suffix `_cp`, `_ob`, `lrrp`), `ene_soldierTypes`, `ene_soldierSubType` | CP classification | `lib/TppEnemy.lua:1448-1523` | After OnInitialize |
| `GameObject.GetGameObjectId([type,] name)`, `GameObject.GetGameObjectIdByIndex(type, i)`, `SendCommand({type=T}, {id="GetMaxInstanceCount"})`, `GameObject.GetTypeIndex(id)`, `GameObject.DoesGameObjectExistWithTypeName(T)`, `GameObject.NULL_ID` | Indexed enumeration pattern | `lib/TppEnemy.lua:840-865` (hostages), `866-881` (walker gear), `1413`; `lib/Tpp.lua:3-25` | Any frame |
| `Tpp.IsSoldier / IsCommandPost / IsHostage / IsVehicle / IsPlayer / IsLocalPlayer` | Type predicates | `lib/Tpp.lua:292-351` | Any frame |
| `SendCommand({type="TppSoldier2"}, {id="GetGameObjectIdUsingRoute", route=r})` | List of soldiers on a route | `mission/s10036/s10036_enemy.lua:618` | Any time |
| `TppEnemy.IsActiveSoldierInRange(pos, r)` | Any active soldier within range | `lib/TppEnemy.lua:771` | Any time |
| `TppEnemy.GetAllHostages()` | Living, enabled hostages | `lib/TppEnemy.lua:840` | Any time |

Sync soldiers by name (StrCode32 of the name), not by GameObject id. Reinforcements and quest soldiers are respawned by name (`lib/TppReinforceBlock.lua:178`, `lib/TppEnemy.lua:2925-2965`).

### 2. Reading soldier, hostage and CP state

All of these are `GameObject.SendCommand(id, {id=...})` calls that return a value.

| Command | Returns / constants | Cite |
|---|---|---|
| `GetLifeStatus` | `TppEnemy.LIFE_STATUS{NORMAL=0, DEAD=1, DYING=2, SLEEP=3, FAINT=4}` or `TppGameObject.NPC_LIFE_STATE_*` | `lib/TppEnemy.lua:25, 906-911`; `lib/TppPlayer.lua:1190-1196` |
| `GetStatus` | `TppGameObject.NPC_STATE_{NORMAL, DISABLE, CARRIED}`, `EnemyState.STAND_HOLDUP` | `lib/TppEnemy.lua:918-927`; `lib/TppPlayer.lua:1204`; `mission/s10020/s10020_sequence.lua:1291-1296` |
| `GetActionStatus` | `ACTION_STATUS{NORMAL, FULTON_RECOVERD, HOLD_UP_STAND, HOLD_UP_CROWL, NOW_CARRYING}` | `lib/TppEnemy.lua:25, 912-917` |
| `GetStateFlag` | Bitmask (`StateFlag.DYING_LIFE`, `StateFlag.ZOMBIE`) | `lib/TppPlayer.lua:1190`; `lib/TppHero.lua:202` |
| `GetPosition` | `Vector3` (supports `:GetLengthSqr()`, subtraction) | `lib/TppEnemy.lua:1919`; `mission/s10036/s10036_sequence.lua:317` |
| `GetRotationY` | Yaw | `lib/TppPaz.lua:333` (exe: hostage dispatcher only) |
| `IsFultonCaptured`, `IsBroken`, `IsAlive` | Walker gear, heli, vehicle state | `lib/TppEnemy.lua:877, 2100, 2197-2217` |
| `IsInRange{range, target={x,y,z}}` | Bool | `mission/s10020/s10020_sequence.lua:1279-1289` |
| `IsDoneHoldup`, `IsDoneTacticalTakedown`, `IsChild`, `IsDD`, `GetStaffId`, `GetFaceId`, `GetKeepFlagValue`, `IsZombieOrMsf` | Misc flags | `lib/TppHero.lua:165,175`; `lib/TppResult.lua:782` |
| CP: `GetPhase{cpName=}` | Phase 0..3 | `lib/TppEnemy.lua:904-905` |

Derived helpers:
- `TppEnemy.IsEliminated` / `IsNeutralized` (`lib/TppEnemy.lua:928-929, 3295-3305`).
- `TppEnemy.IsRecovered` reads `svars.ene_isRecovered` (`lib/TppEnemy.lua:930-939`).
- `vars.playerPhase`, the local player's overall phase (`lib/Tpp.lua:359`; `lib/TppMission.lua:1505`).
- `TppEnemy.GetSoldierType` (`lib/TppEnemy.lua:110`).

The exe adds commands no script uses: soldier `GetVehicleGameObjectId`, hostage `GetCurrentLife`, `GetMaxLife`, `GetNoticeState`, `GetRideVehicleState`. These are found only as names in the dispatcher.

**Bulk read.** `SendCommand({type="TppSoldier2"}, {id="StoreToSVars"})` fills Lua-readable arrays, about 25 per soldier. The useful ones:
- `svars.solName` (StrCode32), `solState` (UINT8), `solFlagAndStance`
- `solLocation[i*4 + 0..3]` (FLOAT: x, y, z, rotY)
- `solCp`, `solCpRoute`, `solScriptSneakRoute` / `CautionRoute` / `AlertRoute`, `solRouteNodeIndex`, `solTravelName`
- CP side: `cpNames`, `cpFlags`

Cites: key names `lib/TppEnemy.lua:1139`; declaration and types `:1188`; store `:1340-1360`.

**Exe dispatchers:**
- TppSoldier2: `0x141356170`
- TppCommandPost2: `0x140D5DF30`
- Hostage family: `0x140E86D3E`
- Vehicle: `0x140B2B79D`

**When safe:** reads are cheap native dispatches and safe on the Lua thread after `OnMissionGameStart` (`lib/TppMain.lua:288`), when `svars.seq_sequence > 1` (`lib/TppMission.lua:1356`). About 270 soldiers times 3 reads per frame is wasteful, so round-robin them. Bulk `StoreToSVars` is what checkpoints use; calling it mid-game is fine for reading but overwrites the checkpoint snapshot.

### 3. Writing or forcing soldier state

| Command | Params (per exe and scripts) | Cite | When |
|---|---|---|---|
| `ChangeLifeState` | `{state, stance}`. `TppEnemy.ChangeLifeState{targetName, lifeState}` accepts only 1..3 (DEAD, DYING, SLEEP), so FAINT is excluded | `lib/TppEnemy.lua:940-953`; exe `0x14135BA87` | Event-driven only |
| `RequestForceFulton` | `{}` (exe also reads `voiceType`, `enabled`); plays the full fulton | `lib/TppEnemy.lua:3052`; `mission/f30050/f30050_sequence.lua:2081-2085` | Event-driven; generates Fulton messages locally |
| `RequestVanish`; corpse `{type="TppCorpse"}` `RequestDisableWithFadeout{name}` | Remove a body or NPC | `lib/TppEnemy.lua:2864-2872` | Event-driven |
| `SetEnabled{enabled, noAssignRoute}` | Enable / disable | `lib/TppEnemy.lua:1099-1108` | Event-driven |
| `SetSneakRoute` / `SetCautionRoute` / `SetAlertRoute{route, point, isRelaxed/enabled}`, `SwitchRoute`, `SetRouteEnabled`, CP `ChangeRouteSets` and `ShiftChange{schedule}` | Routing | `lib/TppEnemy.lua:954-1008, 1025-1029, 1908-1918, 2286` | Event-driven, low rate |
| `SetCommandPost{cp}` | Reassign CP | `lib/TppEnemy.lua:1505`; `mission/s10036/s10036_enemy.lua:605` | Rare |
| `SetOutOfArea{soldiers, isOut}` sent to `{type="TppSoldier2"}` | Area deactivation | `lib/TppEnemy.lua:772-778` | On area change |
| `SetForceRealize`, `SetIgnoreSupportBlastInUnreal`, `SetVip` | Keep simulated far from the local player | `lib/TppEnemy.lua:889-891` | Once per target |
| `SetEverDown{enable}`, `SetDisableDamage{life, faint, sleep}`, `SetEnemyDamageRate{rate}`, `SetMaxLife`, `RecoveryStamina` | Damage tuning | `lib/TppEnemy.lua:2931`; `mission/f30050/f30050_sequence.lua:3520`; `mission/s10020/s10020_sequence.lua:1305-1308` | Once |
| Hostage `Warp{position, degRotationY}` | Hostages only (also Huey and Paz) | `lib/TppEnemy.lua:2381`; `mission/f30050/f30050_sequence.lua:3512`; `lib/TppPaz.lua:262` | Rare |
| Vehicle `SetPosition{position, rotY}` | Vehicles and heli | `mission/f30050/f30050_sequence.lua:3717`; `lib/TppDebug.lua:68` | Rare |
| Exe only (no vanilla script use) | Soldier `SetPuppet{enabled, actionName}`, `SetFriendly{enabled,...}`, `SetCommandAi{commandType, commandIndex}`, `ChangePhase{enabled}` (toggles bit 0 of a per-soldier byte, `0x14135AF92`; not a phase setter), `CallRadio`, `SpecialAction{stance}` | dispatcher `0x141356170` | Unverified |

Limits on writes:
- **No soldier warp.** The TppSoldier2 dispatcher contains no `Warp` or `SetPosition` (both absent from `0x141356170`). Vanilla's own "warp" of soldiers is disable, set route and point, re-enable (`lib/TppDebug.lua:63-68`).
- **Bulk restore exists only on the load path.** Write `svars.sol*` (and hostages / CP), then `{type="TppSoldier2"}` / `TppCommandPost2` `RestoreFromSVars` (`lib/TppEnemy.lua:1321-1337`). This runs only from `TppMain.OnInitialize` when continuing (`lib/TppMain.lua:218-220`), and continue first does `TppEnemy.StoreSVars(true)` (`lib/TppMission.lua:410`). Calling it mid-game is unverified.
- **Fulton side effects.** Fulton recovery records go through `TppTerminal.OnFulton`, which calls `TppEnemy.SetRecovered` and staff-add (`lib/TppTerminal.lua:448-453`). A forced fulton on the guest will also recruit into the guest's Mother Base.

### 4. Message system (host-side event hooks)

**Dispatch path.** The engine calls `TppMain.OnMessage(class, msgId, a0..a3)`, StrCode32-keyed (`lib/TppMain.lua:411-438`). It fans out to:
- every library module's `OnMessage` (`:396-403`),
- every mission script's `Messages()` table, compiled with `Tpp.MakeMessageExecTable` (`lib/Tpp.lua:110-158`; `lib/TppMain.lua:169-171, 361-366`),
- the location, order-box and animal scripts,
- the current sequence's `Messages` (`lib/TppSequence.lua:290-316`).

**Filtering.** Each handler is filtered by `TppMission.CheckMessageOption` / `CheckMissionState` (`lib/TppMission.lua:1341-1370`). Messages are dropped during clear, game over, demo or prepare unless flagged `isExec*`. A message can return `Mission.ON_MESSAGE_RESULT_RESEND` through the generation gate (`lib/TppMain.lua:418-423`; `lib/TppDefine.lua:47`).

**Recommendation.** Wrap `TppMain.OnMessage` the same way `partner_sync.lua` wraps `OnUpdate` (`lua/coop/partner_sync.lua:9, 206`). That sees everything before any filtering. The full catalogue of message names is in `lib/TppDbgStr32.lua:2`.

| Event | Message (class.msg) and args | Cite |
|---|---|---|
| Kill | `GameObject.Dead(gameObjectId, attackerId, attackId, deadFlags)` (`DeadMessageFlag.FIRE`, `NOT_DAMAGE_DEAD`, `FROM_PLAYER_ORDER`) | `lib/TppHero.lua:173-239`; `mission/f30010/f30010_sequence.lua:698`; `mission/s10020/s10020_sequence.lua:860` |
| Any neutralization (best single hook) | `GameObject.Neutralize(id, attackerId, NeutralizeType{DEAD, FAINT, SLEEP, DYING, FULTON, HOLDUP}, NeutralizeCause)` | `mission/s10036/s10036_sequence.lua:575, 797-808`; `lib/TppResult.lua:823-845` |
| Unconscious / dying | `Unconscious(id, ?, attackerId)`, `Dying(id, attackerId)`, `Conscious`, `ChangeLife` | `lib/TppRevenge.lua:946`; `lib/TppHero.lua:240` |
| Hold-up | `Holdup(id)` | `lib/TppHero.lua:164-166` |
| Damage | `Damage(id, attackId, attackerId)` | `lib/TppRevenge.lua:976`; `mission/f30010/f30010_sequence.lua:686` |
| Fulton | `Fulton(id, arg1, arg2, staffOrResourceId)`, then `FultonInfo(id, playerIndex, ...)`, which carries who fultoned; also `FultonFailed`, `FultonFailedEnd`; Player `PlayerFultoned` | `lib/TppTerminal.lua:360-382`; `mission/f30010/f30010_sequence.lua:710`; `lib/TppMission.lua:1109` |
| Carried, placed in heli | `Carried`, `PlacedIntoVehicle(id, vehicleId)`, Player `RideHelicopterWithHuman`, `HeliDoorClosed`, `Returned` | `lib/TppEnemy.lua:22`; `lib/TppTerminal.lua:364` |
| Phase | `ChangePhase(cpId, newPhase, oldPhase?)` (sender = cp name), `ChangePhaseForAnnounce(cpId, phase)`, `StartedDiscovery`, `StartedCombat`, `CommandPostAnnihilated(cpId, phase, count)` | `mission/f30010/f30010_sequence.lua:648-659`; `mission/s10020/s10020_sequence.lua:2362, 2886`; `lib/TppRevenge.lua:953-964` |
| Gimmicks, vehicles | `BreakGimmick(id, locatorHash, dataSetHash, attackerId)`, `SwitchGimmick`, `VehicleBroken(id, "Start"/"End")`, `WalkerGearBroken`, `LostControl` | `mission/f30010/f30010_sequence.lua:662`; `lib/TppGimmick.lua:299, 385`; `lib/TppHero.lua:248` |
| Pickups | Player `OnPickUpCollection(playerId, uniqueId, type, count)`, `OnPickUpWeapon(playerId, equipId, ...)`, `OnPickUpPlaced` | `lib/TppPlayer.lua:1025, 1235-1277` |
| Traps | `Trap.Enter/Exit(trapNameHash, gameObjectId)`; fires for any object, so filter with `Tpp.IsLocalPlayer` | `mission/f30050/f30050_sequence.lua:2081-2085` |
| Player | `Player.Dead(playerId, deathTypeHash)`. `OnPlayerDead` returns early unless `TppNetworkUtil.IsHost()` | `lib/TppMission.lua:1096, 1213-1227` |
| Weather, clock | `Weather.ChangeWeather(weatherId)`, `Weather.Clock(senderId)`, `WeatherForecast` | `mission/s10020/s10020_sequence.lua:3572, 3875`; `lib/TppEnemy.lua:24`; `lib/TppTerminal.lua:369` |
| Timers, UI, radio | `Timer.Finish(sender)` from `GkEventTimerManager.Start`; `UI.EndFadeOut/EndFadeIn`, `GameOver*`, `PauseMenu*`; `Radio.Finish` | `lib/TppMission.lua:1115-1154` |
| Script injection | `Mission.SendMessage("Mission", "OnAddTacticalActionPoint", ...)` posts a message from script; use for synthetic events on the guest is unverified | `lib/TppResult.lua:819` |
| Script-variable changes | `TppMain.OnChangeSVars(name, key)` goes to libs; svars declared `notify=true` (`seq_sequence`, `mis_canMissionClear`) trigger OnEnter/OnLeave and can-clear | `lib/TppMain.lua:391`; `lib/TppSequence.lua:298-308`; `lib/TppMission.lua:1788-1809` |

Subscribing:
- Mission script: `function this.Messages() return StrCode32Table{GameObject={{msg="Dead", sender=name, func=f, option={isExecMissionClear=true}}}} end`.
- Per sequence: `sequences.X.Messages`.
- `TppMission.RegisterMissionSystemCallback{OnRecovered, OnGameOver, OnEstablishMissionClear, ...}` (`lib/TppMission.lua:41-105`).

### 5. Alert phase

- **Constants:** `TppEnemy.PHASE{SNEAK=0, CAUTION=1, EVASION=2, ALERT=3}` (`lib/TppEnemy.lua:25`) and `TppGameObject.PHASE_*`.
- **Read:** per-CP `GetPhase` (`lib/TppEnemy.lua:904`); local summary `vars.playerPhase`.
- **Hook:** `ChangePhase` message. The vanilla reactions are the radio announce `TppEnemy._AnnouncePhaseChange` (`lib/TppEnemy.lua:3256-3275`), revenge points (`lib/TppRevenge.lua:964`), game over on discovery (`lib/TppMission.lua:1154-1157`) and the alarm lamp (`mission/f30010/f30010_sequence.lua:648-659`).
- **Writing a phase: no vanilla script does this.** The exe CP dispatcher has these commands:
  - `SetPhase{phase=N}`. Per instance (`0x140D60A81`), it writes the value to CP record `+0x4C`, and when phase is 3 and a flag at `+0x186` bit 0 is set, it also sets bit `0x10` at `+0x58`. Sent to `{type="TppCommandPost2"}` (`0x140D5FB81`), it writes all CPs.
  - `SetKeepAlert{enable}` and `SetKeepCaution{enable, ...}`.
  - `RequestNotice{type, targetId, sourceId}`.
  - `RequestRadio{label, memberId, isHqRadio}`.
  - `SetCpForceSiren`, `RequestForceReinforce`.
  - `SetIgnoreCoop` / `RemoveIgnoreCoop`, which looks FOB-related; avoid it.
  - Whether `SetPhase` takes effect immediately or is processed later is unverified. That is the first live test to run.
- **Radio:** CP radio and announce are local side effects of `ChangePhase`. `TppRadio.PlayCommonRadio` (`lib/TppRadio.lua:148`). Also `TppRadioCommand.*`.
- **When:** event-driven, on the host's `ChangePhase`. Never every frame.

### 6. Clock and weather

- **Read:** `vars.clock` in seconds; `TppClock.GetTime("number"|"string"|"time")`; `GetTimeOfDay`, which uses `WeatherManager.IsNight()` (`lib/TppClock.lua:28-39`); `vars.weather`, `vars.requestWeatherType[priority]`, `vars.weatherNextTime` (`lib/TppWeather.lua:61-97`).
- **Write the clock:** `vars.clock = s` (`TppClock.SetTime` / `AddTime`, `lib/TppClock.lua:40-48`). Speed: `TppCommand.Weather.SetClockTimeScale(20)` / `(0)` (`:84-85`; started in `TppMain.OnMissionGameStart`, `lib/TppMain.lua:288`).
- **Clock-dependent behaviour:** clock messages via `TppCommand.Weather.RegisterClockMessage{id, seconds, isRepeat}` (`:19-27`). These drive soldier shift changes (`lib/TppEnemy.lua:24, 1908`). Setting the clock jumps can fire them.
- **Write weather:** `WeatherManager.RequestWeather{priority=REQUEST_PRIORITY_NORMAL|EXTRA|FORCE, userId, weatherType, interpTime, fogDensity, fogType}`; wrappers `TppWeather.RequestWeather`, `ForceRequestWeather`, `CancelForceRequestWeather`; `WeatherManager.PauseNewWeatherChangeRandom(true)` (`lib/TppWeather.lua:5-24`). `TppDefine.WEATHER{SUNNY=0, CLOUDY=1, RAINY=2, SANDSTORM=3, FOGGY=4, POURING=5}` (`lib/TppDefine.lua:54`). Store/restore: `WeatherManager.StoreToSVars` / `RestoreFromSVars` (`lib/TppWeather.lua:85-97`).
- **When:**
  - Host-authoritative.
  - On the guest, pause random weather once.
  - Send weather on change (`ChangeWeather` message) with FORCE priority.
  - Correct the clock at about 1 Hz only when drift exceeds a few seconds.

### 7. Mission progress

- **Sequence:**
  - `TppSequence.SetNextSequence(name, opts)` writes `svars.seq_sequence`, gated by CheckMissionState (`lib/TppSequence.lua:45-65`). The change fires `OnLeave` / `OnEnter` (`:298-308`).
  - Read with `GetCurrentSequenceName` (`:81`).
  - `seq_sequence <= 1` means prepare (`lib/TppMission.lua:1356`).
  - Example sequence lists: `mission/s10020/s10020_sequence.lua:45-64`, `mission/f30010/f30010_sequence.lua:65-75, 751`.
- **Objectives:** `TppMission.UpdateObjective{objectives={...}, radio}` and `svars.mis_objectiveEnable[enum]` (`lib/TppMission.lua:106-160, 1961-2091`). `RestoreShowMissionObjective` rebuilds the UI from svars (`:2061`). Defined in mission `missionObjectiveDefine` / `Enum` (`mission/s10020/s10020_sequence.lua:232, 382`).
- **Mission-script svars:** `saveVarsList` becomes svars via `TppSequence.MakeSVarsTable` (`lib/TppSequence.lua:91-102`; `mission/f30010/f30010_sequence.lua:80`). They can be read and written as `svars[name]`; this is Dynamite's `SyncVar` idea done from Lua.
- **Clear, game over, abort:**
  - `TppMission.ReserveMissionClear{missionClearType, nextMissionId}` (`lib/TppMission.lua:714-745`).
  - `CanMissionClear` (`:675`).
  - `ReserveGameOver(type, radio)` (`:653-668`; types `lib/TppDefine.lua:67`).
  - `AbortMission` (`:453`).
  - Established in `TppMission.Update` through `GetSyncMissionStatus` (`:1464-1548`). That function consults `TppNetworkUtil.IsHost` / `SVarsIsSynchronized` and `sync=true` svars, which are the FOB synchronizer's flags. Do not depend on them; only check that the offline host path still passes.
- **Checkpoints and continue:**
  - `TppMission.UpdateCheckPoint(name)` goes to `TppCheckPoint.Update` (`lib/TppCheckPoint.lua:99-150`), then `VarSaveOnUpdateCheckPoint`, which stores enemy, weather and marker svars and saves (`lib/TppMission.lua:2155-2168`).
  - `ContinueFromCheckPoint` / `RestartMission` / `Reload` (`:303-443`).
  - The guest must not save its own checkpoints.
- **Loading a mission:** `TppMission.Load(code, cur, {force})` / `RequestLoad` (`:2227-2297`); from free roam, `AcceptMissionOnFreeMission` (`:198`).
- **Starting from identical randomness:** enemy loadout (`TppRevenge.DecideRevenge`, `lib/TppMain.lua:129`) and faces depend on each save's values:
  - `gvars.rev_revengeRandomValue` (`lib/TppRevenge.lua:5-12, 439`), `rev_revengeLv`, `rev_revengePoint`
  - `gvars.solface_groupNumber`, `gvars.hosface_groupNumber` (`lib/TppMission.lua:595-600`)
  - `str_storySequence`, `qst_*` quest flags, `missionStartClock`, `missionStartWeather`
  - The guest must adopt the host's values before its `TppMain.OnAllocate` runs.

### 8. Findings that challenge pure host authority

1. **Free roam deactivates soldiers outside the local player's area.** `trap_FreePlay_area01..05` call `UpdateOutOfAreaSetting`, which runs `SetOutOfArea(…, true)` on every other area's soldiers (`mission/f30010/f30010_enemy.lua:1374-1420, 1473-1486`). Both PCs must keep the union of both players' areas active.
2. **Each PC simulates only near its own player.** Script loading is centred on the local player's position: quest blocks follow `StageBlockCurrentSmallBlockIndexUpdated` (`lib/TppQuest.lua:523, 745`), and VIPs are kept simulated with `SetForceRealize` (`lib/TppEnemy.lua:891`). The host cannot properly simulate soldiers fighting a guest who is far from the host.
3. **No script path pushes soldier positions while playing** (section 3). Hits are also resolved on whichever PC fired the shot.

**Proposed model (hybrid):**
- **Host-authoritative facts:**
  - **What they are:** death, faint, sleep, dying, fulton, recovered, gimmick and vehicle broken, pickups, phase escalations, objectives, sequence, clear and game over, clock, weather.
  - **How they flow:** either PC detects an event on its own copy (`Neutralize`, `Dead`, `FultonInfo` messages) and sends it on the reliable channel. The host applies it and confirms.
  - **Conflict rule:** life states only move forward (alive, then faint/sleep, then dying, then dead; fulton is final). That avoids rollback.
- **Locally simulated:** soldier movement and AI on each PC, with low-rate route nudges, and a full resync through `svars.sol*` plus `RestoreFromSVars` on the load/continue path.
- **Guest-side suppression:** the guest must stop its own copies of mission triggers. Wrap `TppMission.ReserveMissionClear`, `ReserveGameOver` and `TppSequence.SetNextSequence`, plus save, on the guest so they only run when the host says so.

### 9. Summary of when each call is safe

| Call | When |
|---|---|
| Reads (section 2) | Any frame after game start; batch them |
| Event writes (`ChangeLifeState`, `RequestForceFulton`, `SetPhase`, `RequestWeather`) | Once per event |
| Route, CP, out-of-area changes | On change |
| `StoreToSVars` / `RestoreFromSVars`, objective restore, mission load / clear / game over | Specific sequence states only: OnInitialize, checkpoint, in-game with CheckMissionState true |
| Clock | About 1 Hz correction |

**Not yet verified live:** `SetPhase` semantics, mid-game `RestoreFromSVars`, `ChangeLifeState` producing a correct corpse, `SetPuppet`, and how `TppNetworkUtil.IsHost()` behaves offline.