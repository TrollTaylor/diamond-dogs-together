# Mission flow, following the host, and Dynamite prior art: research report for world sync

This was read-only research. Nothing was launched, built or changed in the repo or the game folder.

**Sources and what they are:**
- **Formatted script copies (`standalone`).** `work/upstream-dynamite/moddata/standalone/*.lua` are Dynamite's stylua-formatted copies of the game scripts, made for exe 1.0.15.3, with Dynamite's small edits. Line numbers below refer to these copies.
- **Installed 1.0.15.4 scripts.** These are minified. I extracted them with `build/archive_probe.exe` into `scratchpad/world/lib2/named/`. I cite them by function name.
- **ACC script.** `heli_common_sequence.lua` was extracted with `build/datfpk.exe` to `scratchpad/world/heli/package-02_fpkd/Assets/tpp/level/mission2/heli/common/heli_common_sequence.lua`.
- **Mother Base script.** `f30050_sequence.lua` is the repo's game-derived copy at `build/controller-replacement-20260909/files/.../mgs_coop_f30050_fpkd/Assets/tpp/level/mission2/free/f30050/f30050_sequence.lua`.
- **Not extracted.** `TppClock.lua`, `TppMissionList.lua` and `TppRevenge.lua` are not in 00/01.dat or chunk0-4; they are probably in `master/data1.dat`, which archive_probe does not scan. I used Dynamite's formatted copies of TppMissionList and TppRevenge instead.

---

## Main conclusions

1. **Every mission change goes through one script pipeline.** It starts at `TppMission.ReserveMissionClear{...}` (or `AbortMission{...}`) and ends in `TppMission.Load` → `Mission.LoadLocation` / `Mission.LoadMission`. All inputs are plain Lua values:
   - next mission ID, clear type
   - helicopter route (a string hash), layout code, cluster ID
   - deploy time
   - the player start-position variables

   A guest can therefore follow the host with **Lua calls on the game's Lua thread**: replay the host's transition record. No native mission-load hooks are needed.
2. **The guest should follow in lockstep through each intermediate stop (Mother Base → ACC → mission) and make the same call the host made.** The start position, helicopter state and loadout handling in `ExecuteMissionFinalize` / `ReservePlayerLoadingPosition` depend on what kind of mission the player is leaving (ACC, free roam or story). Being in the same kind of place as the host makes the guest spawn the same way.
3. **The game already marks the mission progress state it treats as shared.** Every mission's `saveVarsList` becomes per-mission saved variables (svars) declared with `sync=true` (`TppSequence.MakeSVarsTable`, wired in at `TppMain.lua:265-266`). Those flags fed the online-mode (FOB) script-variable sync, which we cannot use. The *list* is still the designers' own definition of shared mission state, and we can replicate it over our reliable channel from Lua.
4. **The game has a Lua-level world snapshot and restore.** `TppEnemy.StoreSVars` sends `StoreToSVars`, and `RestoreOnContinueFromCheckPoint2` sends `RestoreFromSVars`, to command posts, soldiers, hostages, enemy helicopters, vehicles, walker gears, UAVs and cameras. Together with the "restore from checkpoint" flag (`gvars.sav_varRestoreForContinue`), this is the best candidate for late join and checkpoint alignment. It is not verified live.
5. **Dynamite's enemy and world sync is almost entirely the game's online-mode synchronizer**, which is off-limits for us. Its own additions (markers, script-variable copy, emblem, Boss Quiet commands and damage) travel over the game's Steam P2P UDP socket, which is also off-limits. **The message designs carry over; the transport and the session-dependent patches do not.**
6. **Host-authoritative holds.** Dynamite's own experience shows that letting each PC decide AI targets diverged badly. For mission flow there is one state machine (svars and sequence), so it needs one writer. Guest-side triggers must become *requests* to the host.

---

## 1. Getting from Mother Base (30050) into another mission

### 1a. The ways out of Mother Base free roam (f30050_sequence.lua)

| User action | Message → handler | Script call |
|---|---|---|
| iDroid mission list while on Mother Base | `MbDvcActAcceptMissionList` (l.1972) → `this.AcceptMission` (l.380-390) | `TppMission.AcceptMissionOnMBFreeMission` (TppMission.lua:269-290) → `ReserveMissionClear{nextMissionId, nextHeliRoute?, missionClearType=FREE_PLAY_NO_ORDER_BOX}` |
| Helicopter taxi to a platform, zoo or ward | `MbDvcActSelectLandPointTaxi` (l.1978) → `OnSelectLandPointTaxi` (l.2172-2178) | `this.ReserveMissionClear` (l.2446-2463): `TppMission.ReserveMissionClear{HELI_TAX_MB_FREE_CLEAR, nextMissionId, nextHeliRoute, nextLayoutCode, nextClusterId}` |
| Board the helicopter (to the ACC) | same wrapper with nil → `nextMissionId = SYS_MISSION_ID.MTBS_HELI (40050)`, clear type `RIDE_ON_HELICOPTER` (l.2446-2451) | `ReserveMissionClear` |
| Generic helicopter ride in any location | `ReserveMissionClearOnRideOnHelicopter` (TppMission.lua:3110-3132) → AFGH_HELI 40010 / MAFR_HELI 40020 / MTBS_HELI 40050 | |
| Emergency (FOB defence) | `MbDvcActAcceptMissionList` / `MbDvcActSelectLandPointEmergency` → `AcceptEmergencyMission` (291-309) → `AbortMission{emergencyMissionId, isInterrupt}` | Out of scope (FOB) |

Free roam (30010/30020) uses `AcceptMissionOnFreeMission` (232-268): it either loads an order-box script block or reserves `FREE_PLAY_NO_ORDER_BOX`. Walking out of the hot zone uses `_ReserveMissionClearOnOutOfHotZone` (3089-3108), which goes `ON_FOOT` to AFGH_FREE or MAFR_FREE.

Mission IDs (TppDefine.lua:346-360): `AFGH_FREE=30010, MAFR_FREE=30020, MTBS_FREE=30050, MTBS_ZOO=30150, MTBS_WARD=30250, AFGH_HELI=40010, MAFR_HELI=40020, MTBS_HELI=40050, HELI_SPACE=40060, FOB=50050`. Clear types are at TppDefine.lua:1339-1356.

### 1b. The ACC sortie (heli_common_sequence.lua)

1. **Pick a mission.** The mission list fires `MbDvcActAcceptMissionList` → `TppMission.AcceptMission` (l.1024; TppMission.lua:222-231). This calls `SetNextMissionCodeForMissionClear(n)` and opens sortie preparation through `TppUiCommand`.
2. **Pick a landing zone.** `MbDvcActSelectLandPoint` (l.1033) → `OnSelectLandPoint(missionCode, heliRoute, layoutCode, clusterCategory)` (l.909) fades out. `OnEndFadeOutSelectLandingPoint` then calls `TppMission.SelectNextMissionHeliStartRoute(mission, heliRoute)` (TppMission.lua:315-339). That sets `gvars.mis_nextMissionCodeForMissionClear` and `gvars.heli_missionStartRoute`.
3. **Sortie menu (loadout, buddy, deploy time).** `MissionPrep_End(selectedDeployTime)` (l.1279-1282) moves to `Seq_Game_MissionPreparationEnd` (l.2098+). After emblem creation, the timer `Timer_NextMission` calls `TppMission.OnEndMissionPreparation(deployTime, clusterCategory)` (TppMission.lua:2249-2270). It stores `mvars.mis_selectedDeployTime` and calls `ReserveMissionClear{FROM_HELISPACE, nextMissionId, nextHeliRoute, nextClusterId}`.
4. **Fade and finalize.** The ACC's `OnEstablishMissionClearFadeOut` (l.706-713) calls `TppMission.MissionFinalize{isNoFade=true}`.

### 1c. The common pipeline (TppMission.lua)

**Reserving the clear.** `ReserveMissionClear` (1029-1065) refuses after a definite game over or an earlier reservation. It writes:
- `svars.mis_missionClearType`
- `gvars.mis_nextMissionCodeForMissionClear`
- `mvars.heli_missionStartRoute`
- `vars.mbLayoutCode`, `vars.mbClusterId`
- `svars.mis_isDefiniteMissionClear = true`

**Establishing it.** `TppMission.Update` (2741-2810) reads `GetSyncMissionStatus` (2836-2864) and calls `EstablishedMissionClear` (3435+). In single player, `IsHost` and `SVarsIsSynchronized` presumably return true; we leave that untouched. `EstablishedMissionClear` then calls `systemCallbacks.OnEstablishMissionClear`, which differs by mission:
- ACC: fade, then `MissionFinalize`
- f30050 (l.713-714): `MissionGameEnd`
- story missions: their own result flow

**Mission end and results.** `MissionGameEnd` (1066-1099) fades out → `OnMissionGameEndFadeOutFinish` (3479) → `...FinishNd2` (3501-3592). That last step includes:
- `TppStory.UpdateStorySequence{updateTiming="OnMissionClear"}` (this is where **story progress** advances)
- `TppResult`, trophies, `TppSave.VarSave`

Results and rewards follow, then `MissionFinalize`.

**Finalize.** `ExecuteMissionFinalize` (1295-1504):
- sets `vars.locationCode` and `vars.missionCode = gvars.mis_nextMissionCodeForMissionClear`, plus layout and cluster
- when leaving the ACC: `TppClock.SetTimeFromHelicopterSpace(mvars.mis_selectedDeployTime, oldLoc, newLoc)` (deploy time)
- handles loadout
- `gvars.solface_groupNumber++` and a random `gvars.hosface_groupNumber` (1428-1433; these set enemy face randomization)
- `TppRevenge.OnMissionClearOrAbort`, `TppClock.SaveMissionStartClock`, `TppWeather.SaveMissionStartWeather`
- `TppMain.ReservePlayerLoadingPosition(MISSION_FINALIZE, fromHeli, fromFree, toHeli, toFree, ...)`
- `VarResetOnNewMission` (3929), `SaveGameData`, then `RequestLoad`

**Loading.** `RequestLoad` (3995) → `UpdateForMissionLoad` / `LoadWithChunkCheck` → `Load` (4043-4096). `Load` handles:
- the loading screen (`TppMain.EnableBlackLoading(showLoadingTips)`)
- the Mother Base layout
- `TppEneFova.InitializeUniqueSetting` and `TppEnemy.PreMissionLoad`
- `Mission.LoadLocation(force)` and `Mission.LoadMission(opts)`

Dynamite's issue_7 doc traces `Mission.LoadMission` to native `Mission_LoadMission`, which sets globals used by `SimpleMissionBlockController::Update` (1.0.15.3 addresses).

**Other transitions:**
- **Abort:** `AbortMission` (637-729) → `ReserveGameOver(ABORT)` → `EstablishedMissionAbort` (2970) → fade → `VarSaveForMissionAbort` (744-909; sets `vars.missionCode`, does not credit a clear) → `LoadForMissionAbort` (910).
- **Restart / Continue / Return to mission:**
  - `RestartMission` / `ExecuteRestartMission` (428-500)
  - `ContinueFromCheckPoint` / `ExecuteContinueFromCheckPoint` (501-615)
  - `ReturnToMission` (523)
  - Pause menu and game-over menu messages: 1830-1879
- **Mid-mission repack:** `TppMission.Reload{missionPackLabelName, OnEndFadeOut}` (391-427, `ExecuteReload` 4097). Dynamite issue_7 shows 10070 doing this after a cutscene.
- **Checkpoint:** `TppMission.UpdateCheckPoint` → `TppCheckPoint.Update` → `VarSaveOnUpdateCheckPoint` (3872-3901). That sets `ReserveVarRestoreForContinue` and calls `TppEnemy.StoreSVars`, `TppWeather.StoreToSVars`, marker/supply/decoy stores, `TppSave.VarSave` and `SaveGameData`.

### 1d. Start position, loading screens, deploy time and weather

- **`TppMain.ReservePlayerLoadingPosition` (TppMain.lua:666-805)** chooses the start position:
  - in the helicopter on `gvars.heli_missionStartRoute`
  - at `mvars.mis_helicopterMissionStartPosition`, which the iDroid sets via `MbDvcActHeliLandStartPos` → `SetHelicopterMissionStartPosition` (340-346)
  - on foot at a `NO_HELICOPTER_MISSION_START_POSITION` point or the order box
  - for aborts, it resets the route and position (774-797)
- **Streaming around the start point.** `StageBlockCurrentPosition` (805-822) uses `vars.initialPlayerFlag == USE_VARS_FOR_INITIAL_POS` with `vars.initialPlayerPosX/Z`. `TppPlayer.SetInitialPosition`, `SetMissionStartPosition` and `SetStartStatus` are the public setters.
- **Clock and weather.** `TppVarInit.InitializeForNewMission` calls `TppClock.RestoreMissionStartClock` and `TppWeather.RestoreMissionStartWeather`. Live setters exist: `TppClock.SetTime("20:00:00")` is used in TppMbFreeDemo, plus `TppWeather.RequestWeather` / `ForceRequestWeather` and `WeatherManager.StoreToSVars` / `RestoreFromSVars`.
- **Loading coroutine.** `TppMain.OnAllocate` runs as a coroutine. It yields in `WaitFinishMissionEndPresentation` (2279-2297) and in the svars-declare wait loop (TppMain.lua:273-277), so a co-op "wait for partner" yield loop is technically possible there.

---

## 2. Making the guest follow the host

### 2a. Design: replay the transition record on the guest

**Host side.** Wrap `TppMission.ReserveMissionClear`, `AbortMission`, `RestartMission`, `ContinueFromCheckPoint`, `Reload` and `OnEndMissionPreparation` with the existing `loadstring` receipt approach. After the original call returns true, send a **MISSION_TRANSITION** event on the reliable channel:

- `{epoch, kind (clear|abort|restart|continue|reload), srcMission, nextMissionId, missionClearType, nextHeliRoute (number), nextLayoutCode, nextClusterId, deployTime, missionPackLabelName, isNoSave}`

Send a second event, **MISSION_SEEDS**, at the host's `TppMission.Load` entry, *after* finalize has advanced its values:

- `vars.missionCode`, `vars.locationCode`, `vars.mbLayoutCode`, `vars.mbClusterId`
- `gvars.heli_missionStartRoute`, `gvars.mis_isStartFromHelispace`, `gvars.mis_isStartFromFreePlay`
- `gvars.solface_groupNumber`, `gvars.hosface_groupNumber`
- `gvars.rev_revengeRandomValue`, `gvars.rev_revengeLv[]`
- `gvars.missionStartClock`, `gvars.missionStartWeather*`
- `gvars.ply_initialPlayerState`, `gvars.ply_missionStartPos` / `Rot` (when used)

**Guest side**, on the Lua thread at its next update, with follow mode on:

| Guest's current place = host's source | Guest call |
|---|---|
| Mother Base free, free roam or ACC (non-story) | `TppMission.ReserveMissionClear{missionClearType, nextMissionId, nextHeliRoute, nextLayoutCode, nextClusterId}`, the same call the host made. From the ACC, also set `mvars.mis_selectedDeployTime = deployTime` (finalize reads it). The guest's own `OnEstablishMissionClear` then runs the same finalize and load. |
| Story mission, host **cleared** it | Same `ReserveMissionClear` replay. The guest's save copy then gets the clear credit, which matches "shared outcome". |
| Story mission, host **aborted** | `TppMission.AbortMission{nextMissionId, isNoSave}`. No clear credit (744-909). |
| Host restart, continue or `Reload{label}` | Same API (`RestartMission{isNoFade}`, `ContinueFromCheckPoint`, `Reload{missionPackLabelName}`) plus the checkpoint alignment in 2c. A `Reload` usually comes from the guest's *own* sequence once its sequences follow the host's (2b), so treat a mirrored Reload as a check rather than a command. |

On the guest's own `TppMission.Load` entry (a wrapper), **overwrite** the transmitted seed values just before `Mission.LoadLocation` / `LoadMission`. Its own finalize has already randomized `hosface_groupNumber`, so overwriting afterwards makes enemy faces, revenge loadout levels, clock and weather start the same.

`TppRevenge._ApplyRevengeToCp` also uses Lua `math.random()` (TppRevenge.lua:1190-1224), so identical variables are not enough on their own. One option is `math.randomseed(sharedSeed)` in a `TppMain.OnAllocate` wrapper on both PCs. It is unverified, and any extra `math.random` draw on one side breaks it. The safe fallback is the host snapshot in 2c, which carries per-soldier `solWeapon` and other state.

**Lockstep rule.** If the guest is not in the host's *source* mission when a transition arrives (for example it is still on Mother Base while the host is in the ACC), it first follows to the source and then applies the queued transition. Two reasons:
- `ExecuteMissionFinalize` and `ReservePlayerLoadingPosition` behave differently depending on the mission being left (1408-1500; TppMain 666-805).
- `ReserveMissionClear` from a story mission credits a clear, so it must only be used where the host also cleared.

**Guest's own exits must be blocked.** In follow mode, guest-originated `ReserveMissionClear`, `AbortMission`, `ReserveGameOver`, pause-menu restart and hot-zone exits must be intercepted. They are forwarded to the host as `REQUEST_*` events, and the guest acts only on the host's committed `MISSION_TRANSITION`. Otherwise a guest walking out of the hot zone (`Update` → `ReserveMissionClearOnOutOfHotZone`, 2770-2780) would leave the mission alone. A token set by the follow code distinguishes our replay calls from the guest's own.

**Ready barrier.** Each side sends `MISSION_READY{epoch, missionCode}` at `OnMissionGameStart`; the existing `mission_trace.lua` phase receipts already mark this point. Partner, world and enemy sync start only when both match.
- v1: the host waits at the landing zone, as Dynamite's users do (docs.md:181-205).
- v2: a coroutine yield loop in the host's `OnAllocate` path. That is feasible because it is already a coroutine, but untested.

**Partner body in other missions.** This is a hard prerequisite, independent of the mission scripts. The partner body currently exists only because `player_package_selection.lua` swaps `f30050.fpk` for `mgs_coop_f30050.fpk` and appends the controller package, and every Lua gate tests `vars.missionCode == 30050`. Every destination needs a matching package entry. Dynamite appends `/Assets/tpp/pack/coop/coop_essentials.fpk` in each mission's TppMissionList entry, e.g. TppMissionList.lua:132,158,173. Equipment block sizes also need checking: Dynamite raised `DEFAULT_EQUIP_MISSION_BLOCK_GROUP_SIZE=1777721` (TppDefine.lua:20-23) and the per-mission value by 100000 (TppPlayer.lua:2338-2350). Each mission pack needs an audit for a conflicting single-player controller, as 30050's had. The ACC (40xxx) needs no partner body.

### 2b. Mission progress inside a mission

**Sequence.** Mission progress is a named sequence plus svars:
- `TppSequence.SetNextSequence(name)` writes `svars.seq_sequence`.
- `seq_sequence` is declared `notify=true, sync=false`, so `TppSequence.OnChangeSVars` runs the old sequence's `OnLeave` and the new one's `OnEnter`.
- The host sends `SEQUENCE{name}` on every `seq_sequence` change. Hook `TppMain.OnChangeSVars` (TppMain.lua:870).
- The guest calls `TppSequence.SetNextSequence(name)` (or `ReserveNextSequence`).

**Mission svars.** The host sends value diffs for the mission's `saveVarsList` (sync-flagged, see conclusion 3), polled every N frames. It sends objective/radio events at the TppMission level as reliable events. The guest writes `svars[name] = v`.

**Not needed for mission flow:** the online-mode script-variable sync (Dynamite hook `TppGmImplScriptDeclVarsImplOnSessionNotify`, DynamiteHook.cpp:858-905, which counts table entries with flag `0x10`). It belongs to the game's online session and is not used.

### 2c. Checkpoint, continue, game over and late join

**Snapshot.** On request, the host runs the store half of `VarSaveOnUpdateCheckPoint` without writing to disk:
- `TppEnemy.StoreSVars()`
- `TppWeather.StoreToSVars()`
- player supply/support stores where relevant

It then serializes the mission-category svars:
- `cpNames`/`cpFlags` and `sol*`: per-soldier state, flags/stance, weapon, location ×4, fova (face/body variation), command post, routes, route node, travel
- `hos*`, `enemyHeli*`, `ene_wkrg*`, `uav*`, `securityCamera*`, `passenger*`
- the mission's `saveVarsList`
- `seq_sequence`

Declarations are in installed TppEnemy.lua. `DEFAULT_SOLDIER_STATE_COUNT=160` (TppDefine.lua:25), so this is a few KB to tens of KB, which suits the reliable channel.

**Guest apply.**
1. Load the same mission with `gvars.sav_varRestoreForContinue = true` (`TppSave.ReserveVarRestoreForContinue`). That makes `IsMissionStart()` false (TppMission.lua:1593-1599).
2. Write the host's svars from a wrapper placed *just before* `TppEnemy.RestoreOnContinueFromCheckPoint2()` in `TppMain.OnInitialize` (TppMain.lua:500-511, also `TppWeather.RestoreFromSVars`).
3. That function then sends `RestoreFromSVars` to every game-object type (TppEnemy.lua:3416-3445).

This is **unverified**. It needs a live test of when svars are reloaded from the save slot relative to our write.

**Policies:**
- Host continue or restart → the guest repeats the same API, then gets a fresh snapshot.
- Game over → a death policy (Dynamite disabled `OnPlayerDead` and respawns in 10 s, TppMission.lua:2230-2247; repo `respawn.lua`/`death_policy.lua` is gated to 30050), or both back to the ACC (reader-campaign.md:170).
- Disable the guest's pause-menu Restart and Checkpoint options in co-op.

### 2d. Guest save without the mission unlocked

`ReserveMissionClear` and `AbortMission` do not check unlocks; only the UI lists do. But the destination's scripts read the guest's own `gvars`:
- `str_storySequence` drives revenge caps (`GetRevengeLvLimitRank`, TppRevenge.lua:809-825) and demo, radio and buddy availability
- `str_missionClearedFlag` / `IsMissionCleard` drives first-time versus replay demos and finalize side effects (1452-1457)
- `rev_*` and `f30050_missionPackIndex` / Mother Base layout also matter

**Recommendation: a host-provided progress subset written into the guest's save copy at session start, and again before each follow.**
- **Copy (CATEGORY_GAME_GLOBAL and story):** `str_storySequence`, `str_missionOpenPermission`, `str_missionOpenFlag`, `str_missionClearedFlag`, `str_missionNewOpenFlag`, `str_elapsedMissionCount`, `str_didLostQuiet`, `str_isAllMission*`, `str_quietReunionMissionCount`
- **Copy (restartable):** `rev_revengeLv`, `rev_revengePoint`, `rev_revengeBlockedCount`, `rev_revengeRandomValue`
- **Copy (quests):** `qst_questOpenFlag`, `qst_questClearedFlag`, `qst_questActiveFlag`, `qst_questRepopFlag`, for free roam and side ops
- **Never copy:** CATEGORY_MB_MANAGEMENT, staff or resources, the retry category, or anything in `TppMotherBaseManagement`

Categories are from the installed TppGVars.lua (278 declarations). Writing gvars from Lua is ordinary game-script practice. This is only acceptable because the guest plays on an isolated copy. The memory notes the guest already does; Dynamite redirects saves to `DYNAMITE_*` (TppDefine.lua:187-192).

---

## 3. Dynamite: what it syncs, how, and what we can reuse

| Feature | How Dynamite does it | Depends on game session / FOB / Steam P2P? | Our reuse |
|---|---|---|---|
| Soldier AI, movement, vitals, hostages | Game online-mode synchronizer: host-run AI, movement commands sent to the client, "11 sync instances" (MAKING_OF.md:213, 314). Turned on by `S_IS_ONLINE` + `CreateHostSession` (TppMain.lua:166-186) and many role/host-check patches (dynamitePatches.cpp: sight, damage controller, hostage flag, traps, CP-member status, action task) | **Yes, entirely** | Unusable. The lesson that matters: host-run AI with replayed results works; per-PC AI drifts apart (MAKING_OF.md:163-167, 213) |
| Enemy targeting | Host worker thread every 500 ms, nearest player → FOB offense index hook (DynamiteCore.cpp:437-467; `GetFobOffensePlayerInstanceIndex`) | Yes (FOB offense index, worker thread) | Unusable as-is. In our model each PC only has its own local player as a target; separate problem |
| Script-variable copy | `RequestVar`/`SyncVar` flatbuffers: reads the var handle by name hash, writes gvars/svars natively (DynamiteSyncImpl.cpp:314-632). Only active list: `solFovaSeed`, `solFaceFova`, `solBodyFova`, `solRandomSeed` (DynamiteSyncImpl.h:97+), so enemies *look* the same. `WaitForSync` is a std::jthread and its caller is commented out (DynamiteHook.cpp:899) | Transport = game Steam UDP socket (`FoxNioImplSteamUdpSocketImplSend`, DynamiteSyncImpl.cpp:1152-1199) | **Idea reusable**, done in pure Lua (`svars[x]=...`) over our reliable channel. Also confirms enemy look depends on the fova/seed svars → send the `solface`/`hosface` group numbers or the snapshot |
| Markers (user fixed/follow, enemy sight marks) | Hooks `Marker2SystemImpl::PlacedUserMarkerFixed/Follow/Removed` and `SightManagerImpl::SetMarker`; replays on the peer by calling the same natives with a position and **game object ID** (DynamiteSyncImpl.cpp:168-260). Ignores marker requests during load to avoid a checkpoint restore hang (TppMain.lua:149-156, 638-640) | Transport only | Reusable over our link. Object IDs match because both PCs load the same mission data. Keep the load-time ignore window. Needs main-thread native calls or a Lua marker API (not identified in this pass) |
| Emblem | `SendEmblem`, then apply the opponent emblem texture (patch "always apply opponent emblem") | Transport and FOB emblem path | Cosmetic; low priority |
| Boss Quiet | Host-only hooks on `ActionControllerImpl::SetCommand / SetExtraActionCommand / SetNextActionTask / SetActionTask`. Client skips its own AI and receives the host's commands. Damage deltas (life/stamina) are mirrored in `LifeControllerImpl::UpdateLife` (DynamiteHook.cpp:1113-1372). Plus patches: always jump, process damage locally | Transport only (Boss Quiet is not covered by the 11 sync instances) | **The best pattern to port** for any unsynced AI entity: host decides, peer replays commands. Needs our transport and checks that these functions run on the main thread (no worker-thread hooks allowed). Not mapped for 1.0.15.4 except candidate addresses (synthesis.md S4) |
| Mission flow | **None.** Both players manually pick the same mission. The client connects from the ACC after the host has loaded, then starts the same mission (webui/docs/docs.md:166-205). Each runs its own script and save; one player's exfil ends it (docs.md:213-217) | Session close on clear, abort, finalize, terminate (TppMission.lua:715-738, 1089-1097, 1285-1292; TppMain.lua:940-946) | Only the manual flow and "no late join, host waits at the LZ" carry over. Our follow protocol (section 2) is new |
| Per-mission sequence edits | `TppCoder.SetWorldCenter`, `SessionEnableAccept(true)`, `StartNearestEnemyThread` in the gameplay `OnEnter`; re-arm on `OnReload` (10070); either-player objective check (s10040:887 Honey Bee); 10020 closes the session before its movie | Mostly session | The either-player objective idea becomes a guest REQUEST to the host |
| Revenge | Mine list off when online (TppRevenge.lua:529-532); MAKING_OF.md:189 says revenge was removed from loading | FOB rule | We need the opposite: the *same* revenge on both PCs via seeds or snapshot |
| Other items | Respawn after 10 s with OnPlayerDead disabled (TppMission.lua:2230-2247); Quiet/Ocelot in sortie and Ocelot revolver block sizes; cardboard-box delivery uses local index (TppPlayer.lua:2842-2848) | Mixed | Respawn policy and the player-type items are already partly ported in the repo |
| Documented limits | Buddies local; support heli partly shared; supply drops, interrogations and gimmicks local; weather change host-only (MAKING_OF.md:359-389) | | Shows these are separate sync jobs in any design |

---

## 4. Earlier decisions in this repo

- **`docs/campaign-design.md`** (proposed, not implemented):
  - The host owns story, unlocks and outcomes.
  - Guests get a participation record.
  - Every completion is a transaction with an ID, so a replay cannot pay out twice.
  - Shared briefing and a ready check before deployment.
  - Host-simulated enemies, alerts, weather, clock, objectives and boss phases; clients predict only their own movement.
  - Exclusive actions use host reservations.
  - Join at the ACC, a safe point or a checkpoint, applying a host snapshot first. This matches 2c.
  - Host loss means stop and recover.
- **`docs/analysis-2026-10-03/reader-campaign.md` §4 and `synthesis.md` (G13, stages S0-S6)**:
  - Mission-select sync v1 is manual; v2 is a host broadcast plus a guest prompt.
  - Turn checkpoint/continue into a close/reload/re-join, or send both players to the ACC.
  - Missions with `TppMission.Reload` need re-arming (10070 ×4).
  - Story Quiet missions fall back to a DD soldier.
  - 10010 and 10280 stay host-solo.
  - The mission class list (A/B/C) and the boss list are reusable.
  - The session/accept parts are obsolete under the offline route.
- **`docs/mission-thread-handoff.md`** is about the abandoned native session route (worker-thread allocation). Its one relevant point: mission loading runs on worker threads. That supports keeping all follow logic in Lua callbacks on the game's Lua thread.
- **`docs/dynamite-gameplay-gaps.md`, `soldier-rules.md`, `combat-role-rules.md`, `shared-damage.md`, `enemy-targeting.md`**: all of their native patches are host-role or session bypasses for the FOB route and **do not apply** to the offline design, where each PC is the "host" of its own world. The one exception is damage-table field 13 (parity by default).
- **Memory decisions:** offline only; no FOB synchronizer, `S_IS_ONLINE`, sessions, Steam P2P or worker-thread redirection. The guest's Quiet character falls back to a DD soldier in Quiet story missions.

---

## 5. Host-authoritative check, risks and live tests

**Host-authoritative is confirmed for mission flow.** Mission state is a single-writer state machine (`svars.seq_sequence` with OnEnter/OnLeave, `mis_*` svars), and replaying the host's sequence and svars is cheap. A fully deterministic lockstep across PCs is not viable: Lua `math.random` in revenge setup, engine AI timing, and Dynamite's observed drift. The guest still runs its full local mission script, because enemies only exist if `SetUpEnemy` runs. Its authoritative outputs are suppressed or forwarded, and presentation (UI, radio, local cutscene playback) stays local.

**Risks and unknowns to test live, in order:**

1. Guest `ReserveMissionClear` replay from 30050 to 40050 to a mission, the same way the host went. Pass if the guest lands in the same mission and landing zone, with the same deploy time.
2. A seed overwrite at the `TppMission.Load` entry, then compare enemy faces and revenge gear between the two PCs. Repeat with `math.randomseed` alignment.
3. The snapshot-restore hypothesis in 2c: whether svars written before `RestoreOnContinueFromCheckPoint2` survive and are applied.
4. Partner runtime packages in a non-30050 mission (start with 10033, as the earlier plans do), including equipment block size.
5. Guest REQUEST/suppression wrappers (hot zone, game over, pause menu).
6. `TppMission.Reload` missions (10070).
7. Whether `TppClock` / `vars` readback is available for periodic clock correction. `TppClock.lua` was not extracted; it is probably in `data1.dat`.

**Out of scope here and worth flagging:** enemy combat consequences (life, fulton and phase changes applied with Lua `GameObject.SendCommand`; installed TppEnemy uses `GetLifeStatus`, `SetEnabled`, `RequestVanish`, `ChangeFova`, `StoreToSVars`/`RestoreFromSVars`, among others), marker Lua APIs, and buddy handling.

**Scratch files**, all under `C:/Users/taylo/AppData/Local/Temp/claude/D--Games-steamapps-common-MGS-TPP/e7a6e70e-f530-4f59-b4a3-60e868204a66/scratchpad/world/`:
- `extract.sh`: wraps archive_probe with named copies
- `lib1/`, `lib2/named/*.lua`: installed 1.0.15.4 lib scripts, minified
- `heli/package-02_fpkd/Assets/tpp/level/mission2/heli/common/heli_common_sequence.lua`