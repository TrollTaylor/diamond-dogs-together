I found 30 problems in the design. The most serious ones would break it at runtime: guest death has no working offline path, the game-over menu skips the wrapped functions, and the TppMain.OnMessage observer would see resent messages twice. Below are the corrections, each with a citation, followed by the corrected design. Nothing was run in-game. Citations marked `lib/…` or `mission/…` refer to files under `scratchpad/world/scripts/`. Addresses come from the static exe read with the scratch `hash/` tools.

# CORRECTIONS

1. **`ChangeLifeState` has the wrong parameters and the wrong enum mapping.**
   - The handler reads only `"state"` (lea at 0x135BB15). It has no `stance` field.
   - `state=0` does nothing: the handler tests the value and jumps out on zero at 0x135BB62. Values 1 to 4 are dispatched (0x135BB9B–0x135BBA9).
   - The `TppEnemy.ChangeLifeState` wrapper accepts 1..3 (`lib/TppEnemy.lua:940-951`).
   - Game scripts send the raw command with `state=TppGameObject.NPC_LIFE_STATE_DEAD` (s10100_sequence.lua:1730, in Dynamite's moddata copy).
   - So the value the wrapper excludes, 4, is almost certainly DEAD, not FAINT. The design's "FAINT = raw 4" would kill soldiers that should only be stunned.
   - Fix: always use the `TppGameObject.NPC_LIFE_STATE_*` constants and log their values in P3. There is no wake-up through `ChangeLifeState`.
2. **`TppEnemy.SetRecovered` takes a gameObjectId, not a name** (`lib/TppEnemy.lua:2134-2139`).
3. **Guest death has no working offline path.**
   - `death_policy.lua` hands off to `MgsCoopRespawn`. Its `context()` requires mission 30050, `Tpp.MgsCoop.hostSessionCreated` or `clientPrepared`, and the native session context (`lua/coop/respawn.lua:39-44`, `docs/respawn.md`). Native respawn has never run.
   - Offline, the original handler therefore runs and calls `ReserveGameOver`. The design's guest wrapper drops that call, so a dead guest stays dead forever.
   - Fix: D3 defaults to a host-committed shared game over (`REQ_GAME_OVER`). Respawn becomes a later feature that needs its own offline design.
4. **The menu paths skip the wrapped functions, and the "returns true" test does not work.**
   - `GameOverContinue` calls `ExecuteContinueFromCheckPoint` directly. `GameOverAbortMission`, the `GameOverRestart*` closures and `PauseMenuAbortMission → AbortMissionByMenu` take their own routes (`lib/TppMission.lua:1131-1132`).
   - `AbortMission`, `RestartMission` and `ContinueFromCheckPoint` return nothing. Only `ReserveGameOver` and `ReserveMissionClear` return a boolean (`:653-730`).
   - Fix, host side: detect transitions at the single choke point `TppMain.ReservePlayerLoadingPosition(loadType, …)`. All four `MISSION_LOAD_TYPE` callers go through it (`lib/TppMission.lua`). Add `ExecuteReload` and `ReserveMissionClear` for the clear type.
   - Fix, guest side: wrap the `Execute*` and `GameOver*` functions too. Install the wrappers before `TppMission.Messages()` builds its table, because that table captures `e.X` at construction time.
5. **The OnMessage observer would count resent messages twice.** `TppMain.OnMessage` can return `Mission.ON_MESSAGE_RESULT_RESEND` (`lib/TppMain.lua:419-421`), and the engine then delivers the same message again. If the observer runs before the original, every resent message produces a duplicate `REQ_*`. Fix: observe after the original runs, skip when it returned RESEND, and pass its return values through.
6. **Outcomes can be applied twice on the receiving PC.**
   - A guest's optimistic local kill does not raise `localVer`. When the host's `ENEMY_OUTCOME` comes back, the guest would send `ChangeLifeState` DEAD again.
   - That causes a second local `Dead` message, so TppResult, TppRevenge and the mission handlers run again.
   - For fultons, a second `RequestForceFulton` runs `OnFulton` again, which means a second staff add (`lib/TppTerminal.lua:448-451`).
   - Fix: read the local state before applying (`GetLifeStatus`, `GetStatus`, `TppEnemy.IsRecovered`). Do nothing if it is already at or past the target state.
7. **The guest's own deaths of other kinds would kill host soldiers.** The guest's world diverges: its AI, animals, vehicles and falls differ from the host's. So the guest may send `REQ_OUTCOME` only when the guest caused the event (attacker is the guest's local player, its buddy, or a vehicle or explosive it owns) or when the guest is the proximity owner of that soldier.
8. **Alert phases would flap.**
   - The host decays the phase and forces the guest down every 2 s. Meanwhile the guest's soldiers are still fighting the guest, so the guest escalates again, then gets forced down again.
   - Fix: commit escalations only, and never force a phase down. Each PC decays on its own. The host does not decay below the phase the guest reports for that CP.
   - `SetPhase` reads `"phase"` (0xD5FB8A, 0xD60A84). `SetKeepCaution` and `SetKeepAlert` read `"enable"` (0xD6007C, 0xD60790).
9. **The clock threshold is far too tight.**
   - `vars.clock` is seconds of the day, and `TppClock.Start` sets the time scale to 20 (`lib/TppClock.lua:84`). A 5-game-second threshold is about 0.25 real seconds, so the guest would be corrected constantly.
   - Fix: use a threshold of at least 60 game seconds, compensate for latency, and suspend corrections during pause, demos, loading and slow motion.
   - Also, `TppWeather.CancelRequestWeather` turns random weather back on (`lib/TppWeather.lua:9`), so the pause must be re-applied every digest.
10. **Sequence detection misses changes.** `ReserveNextSequence` turns svars notification off around its write (`lib/TppSequence.lua:66`), so `OnChangeSVars` never fires for those changes. Fix: poll `svars.seq_sequence` every frame on the host.
11. **The guest's checkpoint replay can be refused.** `TppCheckPoint.Update` returns early when the player is in a vehicle or during an alert, after a `GetSyncMissionStatus` check (`lib/TppCheckPoint.lua:117-124`). Fix: replay with `{checkPoint=, ignoreAlert=true, permitHelicopter=true}`, and do not rely on the content of the guest's own checkpoint.
12. **The continue snapshot arrives too late.**
    - The design sends the snapshot after `MISSION_READY`, but applies it before `RestoreOnContinueFromCheckPoint2` in `OnInitialize` (`lib/TppMain.lua:220`). That point comes before `OnMissionGameStart`, so the snapshot cannot be there in time.
    - Fix: the host serialises svars right after its vanilla checkpoint save (`CHECKPOINT_SNAPSHOT`). The guest keeps it and writes it on continue.
    - This also takes P7 (a mid-game `StoreSVars`) off the critical path.
13. **The late-join route is unsafe.** `ReserveVarRestoreForContinue` loads the guest's own `CHECK_POINT` slot (`lib/TppSave.lua:409-410`), which may belong to another mission. Late join is not supported in v1.
14. **Events can arrive while the guest is still loading.** Messages from the new epoch arrive before the guest has built its soldier maps, and the design only logs them as "unknown". Fix: native keeps separate control and world queues. Lua drains the world queue only after its own `MISSION_READY`. The host holds guest requests while its own mission state is not in-game.
15. **Hashes overflow in Lua.** In Lua 5.1 on Windows, `%d` is 32-bit (`lua/coop/partner_sync.lua:37-39`), so u32 `StrCode32` hashes above 2^31 come out wrong. Send them as `%.0f` and validate the range in native code.
16. **`SVARS_DIFF` misses guest-local writes.**
    - The guest's mission handlers keep writing svars locally. The host only sends values that change on the host, so a guest-local write is never overwritten.
    - Fix: `DIGEST` carries an svars checksum, and a mismatch triggers a full resend.
    - The `sync=true` filter selects nothing in particular: TppSequence declares every `saveVarsList` key with `sync=true` (`lib/TppSequence.lua:94-96`).
17. **The partner body can fire traps.**
    - Trap `Enter` messages carry the object that entered, but most handlers ignore it (`mission/f30010/f30010_enemy.lua:1373-1416`).
    - With all instances active, instance 1 may fire area traps or mission traps on the wrong PC. An area trap would make `UpdateOutOfAreaSetting` switch to the partner's area.
    - Fix: add probe P14, and a gated filter that drops Trap messages entered by a non-local player.
18. **Forbidden mechanisms.**
    - D2 (the 0x1E02770 detour) is a hook inside AI/CP update code, which is probably on worker threads. The constraints forbid it, so it is removed as a user option.
    - P13 calls a virtual function (`vt+0x40`), which is a native call into the game, not a read. It is dropped.
19. **The fultoner-only option breaks mission targets.** If D1 is fultoner-only, the host's vanish path sends no `Fulton` message, so the host's mission never sees the target extracted. Mission targets always use `RequestForceFulton` on the host.
20. **Progress sync timing.**
    - A `PROGRESS_SYNC` sent between a clear and the guest's own clear processing can make the story advance twice or skip a step.
    - Fix: apply it only while the guest is in a heli space (40xxx), never between a clear transition and the guest's arrival there.
    - It is written only to a redirected co-op save. On a real second PC, it would otherwise overwrite the guest's real save.
21. **Several acceptance tests cannot run in the one-PC lab as written.**
    - "Both kill D within 100 ms" is impossible for one person driving two windows. It becomes a scripted `race_kill` probe.
    - The W2 "under 25 m for 2 minutes" test measures natural divergence, not our code. It becomes telemetry.
    - The W4 5-second clock test is replaced by the corrected threshold.
    - The W4 game-over test depended on the unwrapped `GameOverContinue`.
    - The W0 "hot zone" test does not apply: free roam uses the mission-area boundary (`AbortForOutOfMissionArea`, `lib/TppMission.lua:1712`).
    - The unfocused guest runs at 16–60 FPS, so all protocol timers use wall-clock time.
22. **The phase order is not the cheapest path.**
    - W0 puts the most expensive and least certain work first: the scarf controller pack, re-pinning the archive, the follow replay and progress sync.
    - Shared enemies need none of it. Soldier names are fixed in `f30010_enemy.lua`'s soldier table, so both players can deploy to 30010 by hand and outcomes can sync without a partner body.
    - Transform snapshots cost the most CPU (one `loadstring` per receipt) and add nothing visible to a shared world, so they move to the last phase.
23. **Wrong reinforcement citation.** `TppReinforceBlock.lua:178` respawns a vehicle (`type=9`). Reinforcement soldiers spawn separately in each world, so an unknown entity stays pending until it spawns and is dropped at the end of the epoch.
24. **The route-snap pattern is misquoted.** TppDebug does `SetEnabled` false, then `SetSneakRoute`, `SetCautionRoute` and `SetAlertRoute`, then `SetEnabled` true, with no `noAssignRoute` (`lib/TppDebug.lua:63-67`). `SetSneakRoute` reads `route` and `point` (0x135E590, 0x135E5E8).
25. **A guest on the support heli gets stuck.** The heli-door path (`HeliDoorClosed → MissionClearOrAbortOnHeliDoorClosed`) is refused on the guest, so the guest sits in the heli forever. It becomes a `REQ_MISSION heli_ride`, plus a probe.
26. **The guest's own game-over menu must be inert.** The host's choice drives what happens.
27. **The snapshot cannot fix enemy loadouts mid-mission.** It only applies at a load. A loadout mismatch is fixed only by a shared reload.
28. **Echo suppression deadlines are counted in frames.** They should be wall-clock time.
29. **Cutscenes are not handled.** Replaying a sequence can start a demo on the guest that warps the guest's player. The host holds requests while a demo is playing.
30. **Pause handling is missing, and ownership is mis-described.**
    - Pause must not stall the link. The native thread keeps the link alive, and request timeouts are measured from the host's receipt, not its commit.
    - Ownership was said to affect perception. Perception is always per PC.

Also corrected: `AddDamage` reads `attackId`, `ownerGameObjectId`, `isOneHitKill` and `isHeadShot` (0x135DAE1–0x135DC08). `ownerGameObjectId` is the attribution lever for P9.

#
