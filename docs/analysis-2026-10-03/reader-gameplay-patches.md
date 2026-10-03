# Gameplay patch parity: Dynamite c90f252 to mgs-coop 0.10.33

Evidence levels used below:
- **LIVE-install**: an install or activation receipt appears in a real game run. Source: `build/diagnostic-live-0.10.33.log`, a host in Mother Base with no peer.
- **compiled**: built only, never seen running.
- **cand**: a candidate address from my read-only scan of `D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe`. Not verified.

Gameplay behaviour is unverified for every item, because no guest has ever joined.

## 1. How addresses are mapped to 1.0.15.4

- **Name join.** `tools/audit_dynamite_reference.py` and `tools/map_dynamite_current_build.py` join Dynamite's 202 names with tpp-mod 301bf797 `symbols.hpp`. Only 16 match; 187 stay unresolved (`docs/evidence/dynamite-current-build-candidates-2026-09-09.json`).
- **Per-feature contracts.** Every gameplay site was found by hand. A `tools/generate_*_contract.py` script freezes it by hard-coding the RVAs and asserting the instructions with Capstone. The output is a `native/*_contract.hpp` file with full-body sha256 or byte arrays, plus vtable slot checks. MinHook or atomic byte swaps run only if those checks pass.
- **Layout rule (new finding).** Old VAs below about 0x142000000 still sit within 0x2000 bytes of the same RVA in the new exe. The FOB-result and game-over sites confirm this: they land exactly on mgs-coop's independently mapped call sites. Old VAs in the high region (about 0x143000000 and up) were relocated arbitrarily, so they need semantic mapping.

## 2. GetPatches: all 25 active entries, plus the URL patch and 2 commented ones

| # | Upstream VA / function | Change | mgs-coop status | 1.0.15.4 location |
|---|---|---|---|---|
|1|0x140ffa9ff DefenseCallback::TargetCallbackExec|JZ->JMP|ported+gated, LIVE-install (`coop_damage=active`)|0xFFA25F (body 0xFF9D30) damage_patch.cpp|
|2|0x141378209 ActionCoreImpl::ExecGrazeCallback ("not needed?")|NOP|intentionally skipped (gaps.md:21)|cand 0x1378ABF or 0x1376FF9 (ambiguous)|
|3|0x149f57ac7 ActionControllerImpl::UpdateActionTask|JZ->JMP|ported+gated, LIVE-install|0x1373827 soldier_patch.cpp|
|4|0x1417a200e RespawnActionPluginImpl::UpdatePlaySubState|NOP|ported, armed per respawn; restore seen live|0x17A16BE respawn_patch.cpp|
|5|0x14133d9cb SightControllerImpl::Update|NOP the IsHost JZ|ported via role-query detour, LIVE-install|caller return 0x133C9A9; detour 0x313FE0|
|6|0x140ebcfc4 Hostage2FlagInfoImpl::RequestSetAndSyncImpl|NOP|same role detour|return 0xEBCD42|
|7|0x140887905 GameOverEvCall::UpdateGameOver|JZ->JMP|ported (submit hook + caller filter), LIVE-install|submit 0x7F31F0, return 0x888173 (JZ cand 0x888155)|
|8|0x1416026ce TppUIFobResultImpl::UpdateFobResult|NOP the call|same|return 0x1601554 (call 0x160154E)|
|9-11|0x144e42a62 / 0x144e42a8e / 0x144e46f45 trap callbacks|force true|ported as 2 detours, LIVE-install|0x587E70 (local), 0x588850 (host)|
|12|0x14625fd1a DoesFobGhostPlayer|NOP|ported+gated, LIVE-install|0x9C176A|
|13|0x1459b60b5 Daemon::StarFobP2pNameResolver ("not needed?")|NOPs|intentionally skipped|cand 0x7DEBF6|
|14|0x140da58e5 DamageControllerImpl::Update (animal crash)|NOP|role detour, LIVE-install|return 0xDA578D|
|15|0x146457b68 CloseSession mission check|NOP|ported, applied only around the owned close|0xA1A710+0x4A|
|16|0x14163fcda CharacterSelectorCallbackImpl::SetupStaffList (Quiet/Ocelot in sortie)|NOP NOP|**missing**|cand 0x163EA0A|
|17|0x1409cc6c0 Player2GameObjectImpl::UpdatePartsStatus (reset to Snake)|JNZ->JMP|**missing**|cand 0x9CD370|
|18|0x149cfba54 UnrealUpdaterImpl::PreUpdate (Ocelot dual revolvers)|NOP|**missing**, high region|unmapped|
|19|0x149f4f17b Soldier2Impl::UpdateCpMemberStatus|JZ->JMP|ported+gated, LIVE-install|0x136BD2B|
|20|0x1409c9a13 Player2Impl::SetEmblemTexture|CMOVNZ->MOV|**missing**|cand 0x9CA6B3 (loads hash 0x90bb6285c23c, "OpponentEmblem")|
|21|0x149c1758d RideWalkerGearActionImpl::SyncMotionWithWalkerGearMotion|NOP|**missing**|unmapped|
|22|0x14a62cf4e walkergear ActionCoreImpl::ReceiveEquipInfo|JZ->JMP|**missing**|unmapped|
|23-24|0x1417cce2a / 0x1417ccd72 BossQuiet CombatAiImpl::StepMove|JNZ->JMP; JNZ retarget|**missing** (deferred)|cand 0x17CC24A / 0x17CC192 (new target 0x17CC30C)|
|25|0x1417d54f7 BossQuiet DamageHandlerImpl::ProcessDamages|NOP NOP|**missing**|cand 0x17D4A17, after `call [rax+0x28]`; return 0x17D4A12 fits combat_role|
|URL|0x142218770 master server string|replace in place|not applied|string RVA 0x2218708|
|commented|0x1409b9d3b climbing IS_ONLINE; 0x1413536b6 Soldier2Impl::Initialize|not applied upstream|n/a; mgs-coop decoded the constructor prefix at 0x1352470|—|

`patch/patch.go` is inert: `PatchAll` and `UpdateURL` are commented out in `Run`.

## 3. CreateHooks: all 44 non-debug hooks

The roughly 50 debug hooks in `CreateDebugHooks` are logging only and are intentionally skipped.

| Hook (old VA) | What it does upstream | mgs-coop |
|---|---|---|
|luaL_openlibs (0x14c1fd0c0)|registers the `Dynamite` Lua lib|replaced by script_load_bridge + bootstrap source transforms|
|IsDefenseTeamByOnlineFobLocal / IsOffenseTeamByOnlineFobLocal|false / true|ported: 0x1E03080 / 0x1E030B0 (player_team_patch.cpp), LIVE-install|
|GetFobOffensePlayerInstanceIndex|nearest player from a background thread|ported, redesigned: enemy_targeting.cpp 0x1E02770 + Lua scan|
|GetFobDefensePlayerInstanceIndex|returns 15|ported: 0x1E026B0|
|SetSteamId (0x14c404640)|forces the host SteamID|equivalent: SessionConnectInfo+0xB8 (client_session_request.hpp)|
|IsConnectReady (0x1459d0cc0)|forces true|intentionally not ported; gate decoded at 0x681CB90 (session-feedback.md:13-15)|
|UpdateClientEstablished (0x140316610)|announcement + sync init + Ping + SendEmblem|announcement only, in session_feedback.lua; no sync|
|GetUiMarkerTypeFromSystemType2|0x200005 -> 0x1C|ported: 0x935020, LIVE-install|
|SightManagerImplSetMarker (0x149c687d0)|duration 9999999 for objectID<=15, plus sync|**missing**, unmapped|
|SteamUdpSocketImplOnP2PSessionRequest (0x14c4040c0)|whitelist/blacklist + packetSeen reset|**missing**|
|AddLocalDamage, DamageControllerImplInitialize, EquipHudSystemImplInitData|passthrough / pointer capture|not needed|
|Marker2SystemImpl ctor; SightManagerImplInitialize; EquipControllerImplInitialize|pointer captures for sync and GetEquipIDInSlot|**missing** (only needed with sync)|
|Marker2System PlacedFixed / PlacedFollow / RemovedUserMarker|drop requests during loading, plus sync|guard ported (0x5C53A0 / 0x5C5530 / 0x5C6250); **sync missing**|
|UiControllerImplSetNoUseEquipId / EquipCrossEvCallIsItemNoUse|9999 / false|ported: 0xFE8320 / 0x8AE340; override observed live|
|CreateHostSession, FobTargetCtor, BlockHeapAlloc (issue_7)|log / capture FobTarget / force network allocations onto the heap|equivalent: direct call to 0x7EE590, GetFobTarget 0x7D6470, 2 exact heap sites|
|CloseSession hook|reset state, stop sync|equivalent in Lua/native lifecycle|
|FoxNioImplSteamUdpSocketImpl Send / Recv|capture socket; filter `DYNAMITE` packets|**missing**|
|Emblem CreateEmblemParameters / CreateEmblem|logging wrappers used by CreateEmblem|**missing**|
|SessionImpl2DeleteMember; MessageBox2 SendMessageToSubscribers (0x190fd3bf)|stop sync on disconnect|n/a (no sync); Nt handling in session_lifecycle.lua|
|ClimbActionPluginImpl::CheckActionState (0x149876920)|always true|**missing, unmapped** (gaps.md:33,37)|
|FoxGeoPathResultGetNumPoint (0x14ce84090)|returns 1 when the inner pointer is null|**missing, unmapped**|
|8 Boss Quiet hooks|host-authoritative AI forwarding; SetNextActionTask rewrite; close-combat move off; recovery step skip; life/damage sync|**missing** (deferred)|
|ScriptDeclVarsImplOnSessionNotify + BitStreamWriterPrimitiveWrite|measures var sync; SendEmblem / Ping|**missing**|

## 4. The `Dynamite` Lua library (DynamiteLua.cpp:405-438)

| Function | mgs-coop |
|---|---|
|CreateHostSession / CreateClientSession / ResetClientSessionState(+WithNotification)|ported natively|
|Start/StopNearestEnemyThread, IsNearestEnemyThreadRunning, GetOffense/DefensePlayerIndex, GetSoldierLifeStatus|ported via the targeting design|
|IgnoreMarkerRequests / AcceptMarkerRequests|ported|
|IsHost / IsClient|mode files|
|Log|probe log|
|AddFixedMarker, AddObjectMarker, RemoveMarker, RemoveAllUserMarkers|missing|
|GetPlayerPosition, WarpToPartner (a default key bind)|missing|
|IsSynchronized, RequestVar, Ping|missing; no shipped upstream script calls them|
|GetEquipment, GetEquipIDInSlot (used by s10040 Honey Bee)|missing|
|Donated / MissionComplete / GetMissionsCompleted|skip|

## 5. Lua and asset changes Dynamite makes

| Script | Upstream change | mgs-coop |
|---|---|---|
|TppMain.lua|IsCoop list (99-147). OnAllocate (148-190): IgnoreMarkerRequests; `SetTimeOut(10)`; host CreateHostSession; S_IS_ONLINE; non-coop branch resets state. Equip ghost groups (364-367). Skip ForceChangePlayerFromOcelot (381-383). Skip RestoreMarkerLocator (510-512). AcceptMarkerRequests at end of OnMissionCanStart (638-640). OnTerminate cleanup (940-946).|Ported for 30050: online flag, host create, ghost groups, marker guard, termination. **Missing**: SetTimeOut(10), the Ocelot skip.|
|TppMission.lua|Cleanup in mission abort/clear/finalize paths (715-739, 1089-1097, 1285-1293). **OnPlayerDead body commented out** (2231-2247). Donate popup.|Death policy wrapper with fallback; lifecycle cleanup via TppMain wrappers.|
|TppPlayer.lua|Warp `fobRespawn` uses the local index (60-64). Equip block +100000 "dual revolver fix" (2338-2352). Cbox delivery client index (2842-2850).|**missing**|
|TppRevenge.lua|`RegisterMissionMineList` returns early if S_IS_ONLINE (529-532). DecideRevenge stays.|**missing** (marker-loading.md:26 only looked for a blanket bypass)|
|TppDefine.lua|DEFAULT_EQUIP_MISSION_BLOCK_GROUP_SIZE=1777721 (20-23); DYNAMITE_* save names (187-192)|**missing** (save isolation is handled elsewhere)|
|TppMissionList.lua|AddMissionPack coop_essentials.fpk for about 36 missions|30050 replacement pack only|
|DamageParameterTables.lua|**Data identical to vanilla** (475 rows checked; 146 rows have field13==1). The trailing loop is a no-op.|mgs-coop really clears the 146 rows: **divergence**|
|TppEnemy.lua|Replaced in commit ad092b69 (issue #40, Red Brass). Diff unknown.|not ported|
|Tpp.lua / script_loader.lua|key-bind Lua loader|replaced by own bootstrap|
|36 `s10xxx_sequence.lua`|DeadInFOB 10 s timer + RequestToRespawn; Nt/Network announcements; OnEnter `TppCoder.SetWorldCenter` + SessionEnableAccept(true) + StartNearestEnemyThread; clear-movie close; s10070 OnReload accept; s10040 Honey Bee check|respawn/accept only, generic and limited to 30050; **no SetWorldCenter**|
|moddata.go|deletes the single-player instance fox2 per area pack; init_sequence skips logos; o50050 disables the FOB timer; adds lang lng2|30050 pack copy minus the controller|

## 6. DynamiteSyncImpl, and whether mgs-coop has any of it

**Transport.** Messages are an 8-byte `DYNAMITE` header followed by a FlatBuffers MessageWrapper with a global `packet_num`. They are sent with the game's own SteamUdpSocket Send, reusing the socket and address captured from the game's sends (`DynamiteHook.cpp:983-997`). The Recv hook passes header-matching packets to `RecvRaw` but does not strip them from the game (`DynamiteHook.cpp:999-1023`). `RecvRaw` verifies the buffer, drops any packet whose number is at or below `packetSeen`, and dispatches the message (`DynamiteSyncImpl.cpp:1206-1282`).

**Messages:**
- **Markers.** Local placement and removal hooks forward user markers; the receiver applies them through the original functions. Sight marks are forwarded with their duration.
- **Script vars.** SyncVar/RequestVar by gvars/svars name and type, applied via GetVarIndexWithName. The enemy fova/seed list is defined, but WaitForSync is commented out, so this is dormant in production.
- **Emblem.** Sent at connection and on host OnSessionNotify; the receiver builds OpponentEmblem textures. Works together with the SetEmblemTexture patch.
- **Boss Quiet.** The host forwards SetCommand / ExtraCommand / NextActionTask / ActionTask; the client's local AI calls return early. Life and stamina damage are exchanged both ways.

**mgs-coop has none of this.** Its `steam_packets.cpp` (ISteamNetworking P2P on its own channel, 1200-byte cap), `protocol.cpp` (kinds input/snapshot/event/loaded, 28-byte header) and `player_sync.cpp` are linked into dinput8 through mgs_coop_core. No runtime code calls them: no references in probe.cpp, game_script_observer.cpp, host_session_trial.cpp or dynamite_session_port.cpp. That layer is the natural carrier for a safer DynamiteSync port.

## 7. Divergences that matter most

1. **Scope.** Everything is tied to mission 30050. Dynamite never supported free roam.
2. **Damage table.** Upstream's field-13 change never took effect; mgs-coop's does.
3. **Companion character.** None of the Quiet/Ocelot enablement is ported.
4. **No world center.** SetWorldCenter is never called.
5. **No permanent partner marker** and no marker sync.
6. **Climbing and path hooks** are still unmapped.
7. **Join timing.** Dynamite's guest connects before loading the mission; mgs-coop's guest joins after its own mission start.