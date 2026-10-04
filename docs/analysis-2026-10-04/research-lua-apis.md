MGSV TPP 1.0.15.4 — Lua control of player instances and NPC puppets (read-only static analysis plus game and Infinite Heaven Lua)

Method
- **Command dispatchers.** I found that GameObject.SendCommand (0xBFA380) hashes the command id with Fox's 48-bit StrCode64: CityHash64WithSeeds(id..'\0', seed0 0x9AE16A3B2F90404F, seed1 (byte0<<16)+len), masked to 48 bits (0x1A09AC0/0x1A09770). I built a Go hasher from the repo's own vendored unknown321/cityhash, hashed 485 command ids from the game and Infinite Heaven (IH) Lua plus 89,828 identifier candidates, and mapped every immediate constant in .xtext. This locates every GameObject type's command dispatcher and shows exactly which ids each type supports.
- **Game Lua sources.** Game Lua comes from the repo's extracted package-00/01/02.lua files and a mirror of the game scripts in TinManTex/InfiniteHeaven (tpp/fpkd-combined-lua and tpp/data1_dat-lua, 673 files).

Scratch outputs, all under C:/Users/taylo/AppData/Local/Temp/claude/D--Games-steamapps-common-MGS-TPP/e7a6e70e-f530-4f59-b4a3-60e868204a66/scratchpad/:
- sc/sc.exe: the hasher (reads strings from stdin).
- ids_hash2.txt and guess_hash.txt: the hashed ids and candidates.
- allhits.json: command-id hits by address.
- idmap.py, dispnames.py, hstrs.py, xdis.py, vt.py, riprefs.py: the analysis scripts.
- gamelua/ and ih/: the Lua mirrors.

No repo or game files were modified.

(1) READING the local player each frame (cheap)
- **Position and rotation:** vars.playerPosX, vars.playerPosY, vars.playerPosZ and vars.playerRotY. playerRotY is in degrees: TppPlayer.GetRotation returns it and TppPlayer.Warp converts degrees to radians.
- **Other vars:** vars.playerCameraRotation[0]/[1] (pitch/yaw in degrees), vars.playerVehicleGameObjectId (NULL_ID on foot), vars.playerLife and vars.playerPhase.
- **Game wrappers:** TppPlayer.GetPosition() = {vars.playerPosX, vars.playerPosY, vars.playerPosZ} and TppPlayer.GetRotation() = vars.playerRotY.
- **Stance and motion class:** PlayerInfo.AndCheckStatus{PlayerStatus.X,...} and PlayerInfo.OrCheckStatus{...}. Enum: STAND SQUAT CRAWL NORMAL_ACTION PARALLEL_MOVE IDLE GUN_FIRE GUN_FIRE_SUPPRESSOR STOP WALK RUN DASH ON_HORSE ON_VEHICLE ON_HELICOPTER HORSE_* BINOCLE SUBJECT INTRUDE LFET_STOCK CUTIN DEAD DEAD_FRESH NEAR_DEATH NEAR_DEAD FALL CBOX CBOX_EVADE TRASH_BOX(_HALF_OPEN) INJURY_LOWER INJURY_UPPER CURE CQC_CONTINUOUS BEHIND ENABLE_TARGET_MARKER_CHECK UNCONSCIOUS PARTS_ACTIVE CARRY CURTAIN VOLGIN_CHASE. These take a table only, apply to the local player, and the module has no other functions.
- **Index and readiness:** PlayerInfo.GetLocalPlayerIndex(). Player.IsInstanceValid() (0x9D6360) is safe when P is null (it returns P and the P+0x204 bit0 flag).
- **Appearance:** Player.GetPlayerType{instanceIndex=i} has no null check, so call it only after start.
- **What does not exist:** there is no Lua getter for a non-local instance's transform. TppPlayer2 has no GetPosition command.
- **Cost:** about 10 var reads and 2-3 PlayerInfo calls per frame is negligible. The expensive part is the project's loadstring receipts, which compile a chunk each call. Have native code read vars memory (vars at 0x2C16920) for the outgoing pose, and return the incoming partner pose as a table from a single receipt.

(2) SETTING a player instance's transform by index — verified static path
- **Syntax from real usage:**
  - TppPlayer.Warp: `GameObject.SendCommand({type='TppPlayer2',index=0 or PlayerInfo.GetLocalPlayerIndex()},{id='WarpAndWaitBlock',pos={x,y,z},rotY=foxmath.NormalizeRadian(foxmath.DegreeToRadian(deg))})`
  - Prologue: `{type='TppPlayer2',index=0},{id='Warp',pos=position,rotY=TppMath.DegreeToRadian(rotationY)}` (s10010_sequence.lua:3715).
- **Binding:** {type,index} becomes the id (typeIdx<<9)|(index&0x1FF) at 0xBFA290. A missing index becomes 0x1FF. There is no bounds check.
- **Manager handler 0x9C5F80:**
  1. It lets a per-instance controller try the command first. Those controller handlers are 0x11566A0 and 0x11568B3, covering the wormhole commands, SetStandMoveSpeedLimit, SetBaseMotionSpeedRate, RequestCarryOff, SetHesitatingFire and SetSpecialAttackMode.
  2. It then handles only WarpToStation, Warp, WarpAndWaitBlock and two unnamed ids.
  3. Warp and WarpAndWaitBlock read 'pos' (a table of 3 numbers) and 'rotY' (radians), then call P->vtbl[0x1B0] = 0x9CEAE0(P, idx, pos, quat, isWaitBlock).
- **What 0x9CEAE0 does:**
  - It stores the per-index transform for any index.
  - If the instance is realized (P+0x78[idx] != 0xFF and P+0x70[idx] != 0xFFFF, the same guard as UpdatePartsStatus at 0x9CD160), it applies the transform to the character components.
  - Only for idx == P+0x214 (local) does it touch the camera and stage-block position, or the block-wait state (P+0x464=2) / warp message.
- **Answer:** non-local instances accept Warp. Use plain Warp for the partner. Never warp the local index per frame.
- **Unsupported for TppPlayer2:** Realize, Unrealize, SetEnabled, SetVisible, SetPosition, GetPosition, SpecialAction and any SetMotion-type command. Each is a silent no-op.
- **Commit 554cc4f:** its realizePlayer2 did nothing useful. 'Realize' is a no-op, and its Warp was gated on vars.playerPos, which does not exist.
- **Animation:** Player.RequestToPlayDirectMotion{name,{gani,hold,target,cnp,mtp,repeat},...} (IH InfMotion.lua:377-421), Player.RequestToMoveToPosition{name,position,direction,moveType,timeout} and Player.RequestToSetTargetStance(PlayerStance.X) are local-player only.
- **Instance-level bindings:**
  - Player.SetPartsInfoAtInstanceIndex (0x9DF530) and CopyPartsInfoFromLocalPlayer.
  - Player.SetAllInstanceActiveMode(bool) (0x9DCD20): sets P+0x204 bit 0x2000, with no null check.
  - Player.SetOtherInstanceIsForMirrorMode(bool) (0x9DF320): bit 0x4000.
  - The prologue mirror scene drives a realized instance 1. The mirror code at 0x9CCC13 hardcodes slot[1], which is a precedent for a second player instance in single-player.

(3) NPC puppet
- **Mother Base soldiers:**
  - Names: 'ly003_cl0C_npc0000|cl0Cpl0_uq_00C0_npc2|sol_plntP_NNNN', 18 per cluster (from ly003.lua). Get the id with `GameObject.GetGameObjectId('TppSoldier2', name)`.
  - Only the current cluster is loaded. The enabled subset is in mvars.mbSoldier_enableSoldierLocatorList[clusterId]. Reserve a soldier with mtbs_enemy.RegisterDisableSoldierForQuest(name).
  - The TppSoldier2 dispatcher (0x1356170-0x1360701) has **no Warp or SetPosition** (verified exhaustively against the candidate set).
  - Movement options:
    - SpecialAction{action='PlayMotion', path=gani, startPos=Vector3, startRot=radians, autoFinish=false, enableGravity=false, enableCollision=false, interpFrame, startFrame, stance, commandId, enableMessage}. Field names verified at 0x135BDCD; real soldier use at s10010_sequence.lua:584-591. Cancel with {id='SpecialAction', isDisable=true}.
    - Route snap: SetEnabled{enabled=false,noAssignRoute=true}, then SetSneakRoute{route,point,isRelaxed}, then SetEnabled{enabled=true,noAssignRoute=true}.
  - Disabling AI and reactions:
    - SetPuppet{enabled=true}: flag 0x8000000 (0x135FD6B); used in s10010, s10120 and s10150.
    - SetFriendly{} and SetSaluteDisable{enabled=true}.
    - SetSaluteMoraleDisable{enabled=true} and SetRestrictNotice{enabled=true}.
    - SetDisableDamage{life=true,faint=true,sleep=true} and SetIgnoreDamageAction{flag=...}.
    - SetDisableOccasionalChat{disable=true} and SetDisableWarpToNav{enabled=true}.
    - SetEverDown{enabled=true} and ChangeLifeState{state=n}.
  - Readback: GetPosition (returns a Vector3), GetLifeStatus and GetStatus.
- **Hostage-family NPCs (Huey, Ocelot, IH's own hostages) have a real Warp:**
  - Warp: {position=Vector3, degRotationY=deg or rotationY=rad, interpTime, demoEnd, control}.
  - SpecialAction: action PlayMotion, PlayState, PlayStance (stance='Stand') or ChildProne, with startPosition and startAngleY.
  - Also: ChangeFova{faceId,bodyId} and SetHostage2Flag{flag,on}.
  - IH InfNPCOcelot loads f30050_ocelot.fpk, then warps Ocelot and swaps his body to DD staff bodies on the Command cluster. IH InfNPC places custom hostages, but that needs IH's own FPKs.
- **Quiet buddy:** TppBuddyQuiet2 has MoveToPosition and WarpToPosition, but buddies are not normally present on MB.

(4) Per-frame hook in 30050
- TppMain.OnUpdate runs the module updates (TppMission, TppSequence, TppSave, TppDemo, TppPlayer.Update = UpdateDeliveryWarp, TppMission.UpdateForMissionLoad), then each sub-script's OnUpdate (f30050_sequence.OnUpdate only polls the buddy block), then ScriptBlock.UpdateScriptsInScriptBlocks. This happens once per frame on the Lua thread.
- It ticks during loading, before OnMissionGameStart, while P is still null.
- mission_main.lua caches TppMain.OnUpdate at load. Hook inside the TppMain chunk, as the repo already does (enemy_targeting, session_feedback, client_join, respawn, session_lifecycle, character_readback).
- Throttle on Time.GetRawElapsedTimeSinceStartUp(). Time.GetFrameIndex() is available to check cadence. The guest runs at 16-60 FPS when unfocused.
- Per-tick cost: one closure and pcall per wrapper, plus at most one receipt and one SendCommand. The id hash costs microseconds.

Recommendation for the first spike
1. Probe natively whether index 1 has a slot (P+0x78[1] and P+0x70[1]).
2. If it does, apply partner position and yaw with Lua Warp on index 1 (approach A) and watch whether the transform holds and what pose it shows. Plan native record-bank feeding for animation.
3. In parallel, get a visible fallback:
   - Ocelot or hostage Warp puppet with ChangeFova to a DD body (approach C): simplest smooth repositioning.
   - Or a soldier with SetPuppet plus repeated SpecialAction startPos (approach B), once a resident soldier gani path is known.