## Scope and evidence levels
All findings come from static analysis of D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe (1.0.15.4) using pefile/capstone scripts in my scratchpad. The repo, the Dynamite/tpp-mod source copies, and web sources were only read. Nothing was launched or modified. Labels used below: STATIC = decoded from the exe in this session; REPO = repo docs/code; UPSTREAM = Dynamite/IH source; WEB = issue trackers/articles. Nothing here is verified live.

## 1. Enums and vars (STATIC, cross-checked)
- `PlayerType` registration at RVA 0x9D45EA..0x9D46BC (AddEnum 0x1A156E0): SNAKE 0, DD_MALE 1, DD_FEMALE 2, AVATAR 3, LIQUID 4, OCELOT 5, QUIET 6. This matches IH InfFova.lua's table and the FOB `IsSnake` check (pType 0/3/4).
- `PlayerPartsType` 0..27 as listed in key_facts; OCELOT 26, QUIET 27. `PlayerCamoType` OCELOT 115, QUIET 116. `PlayerHandType` NONE 0 .. SILVER 7.
- The vars pointer global is RVA 0x2C16920; the var registration code (0x997F40.., 0x99BC80..) gives offsets that match tpp-mod `ScriptVars_tpp` (playerType at 251, partsType2 at 1680). The `*2` vars (partsType2/camoType2/faceEquipId2/weapons2/supportWeapons2/items2/handEquip2) exist only for the FOB defender's loadout and appearance; o50050 reads them in CheckEquipEspBonus_Deffense (o50050_sequence.lua L6178-6190). There is no `playerType2`, so a per-player type lives natively per instance, not in vars.

## 2. Native per-instance appearance (STATIC, new)
Player module functions get `P = *(&player2System + 8)` through getter 0xAEC8D0 (which returns RVA 0x2A82C28, the same root the repo already reads). `Q = *(P+0x60)` is a parts-info object with per-instance arrays: type +0x40, parts +0x48, camo +0x50, hand +0x58, faceId +0x60 (u16), +0x68, an avatar record of 0x4F bytes at +0xE8, and dirty masks at +0x180/+0x184.
- **`Player.SetPartsInfoAtInstanceIndex{instanceIndex, type, partsType, camoType, handType, faceId}`** (RVA 0x9DF530, registered in the Player luaL_Reg table at 0x9D749A/0x9D74A8) writes those arrays and sets `Q+0x180 |= 1<<idx`. handType defaults to 1. **It does no bounds check.** Vanilla Lua never calls it; IH only tried it once in a debug function.
- `Player.CopyPartsInfoFromLocalPlayer{instanceIndex}` (0x9D13F0) copies the local instance's entries (local index at P+0x214) to another instance.
- `Player.GetPlayerType{instanceIndex}` (0x9D30B0) reads Q+0x40[idx]. It returns 0..4, and Ocelot/Quiet come back as 0. That makes it a ready-made in-game readback for replication tests.
- `UpdatePartsStatus` (primary 0x9CD030, chunk 0x9CD0CD..0x9CE8BB) loops over every instance up to enclosing+0x228. It consumes the dirty bit (test 0x9CD1A8, clear 0x9CDA94) and steps the parts-load state at enclosing+0xB0[idx]. For the local instance it takes vars.playerType/camo/faceId/faceEquip from vars (0x9CD303). For other instances it only acts in mirror mode (bits 13 and 14 of enclosing+0x224), where it copies the local appearance (0x9CE786..0x9CE85D).
- **Native replication exists**: virtual 0x12A5810 (vtable slots 0x23D2E58/0x23D3088, close to Dynamite's SynchronizerImpl) takes the latest sync record and writes type/parts/camo/faceId/+0x68/+0x80/hand plus staff-like u32s into Q for that instance, setting the dirty bit only when a value changes (0x12A59BA..0x12A5B54). Variant 0x12CC790 also handles 13 avatar preset bytes. Implication: in a working native session the partner's DD-soldier or Snake look should replicate with no mod code. Dynamite issue #109 (WEB) says the remote AVATAR shows as Snake outside FOB, so avatar data, and possibly more, may be FOB-gated.

## 3. Quiet/Ocelot restriction (STATIC + UPSTREAM)
- In UpdatePartsStatus at 0x9CD353..0x9CD389, for the local instance: when type is 5 or 6 and neither `enclosing+0x224 bit18` nor `enclosing+0x284 bit2` is set, it writes vars.playerType = 0 (reverts to Snake). The `jne` at **0x9CD370** (`0F 85 9C 07 00 00`, the only occurrence in the file) is the upstream Dynamite patch "skip some check that resets player to Snake from Quiet/Ocelot" (old 0x1409CC6C0). The replacement is `48 E9 9C 07 00 00`. Once bypassed, native code forces parts 26/27 and camo 115/116 to match the type (0x9CDB12..0x9CDBCD).
- Bit 18 is set during allocation (0x9BEB3A, `or [P+0x204],0x40200`) when `Mission.SetMissionFlags` (0xA200F0 -> global 0x2C1FD40) has MISSION_FLAGS_FOB set. That explains why IH has to fake S_IS_SORTIE_PREPARATION and why Dynamite patches the jump instead.
- Lua side: vanilla TppMain.OnAllocate calls `TppPlayer.ForceChangePlayerFromOcelot()` for non-FOB missions; Dynamite excluded co-op (`and not e.IsCoop(...)`, standalone/TppMain.lua L381). TppMission.OnAbortMissionPreparation (L2274) also calls it.
- Sortie UI: Dynamite NOPs a `je` in SetupStaffList (old 0x14163FCDA). The 1.0.15.4 candidate is **0x163EA0A** (same rel32 0x1A2; follows vcall `[rax+0x4F0]` and test al). It is a candidate only.
- Ocelot's dual revolvers (old 0x149CFBA54, .xdata) remain unmapped.
- Known limitations (IH and BobDoleOwndU mod 518, WEB): pause menu disabled with the sortie-flag trick, possible infinite load after mission clear, sound problems, character forced back at every mission start. Quiet's FOB kit (WEB, PC Gamer): sniper focus, high jump, dash, invisibility while still; designed for FOB, untested in story missions. Dynamite's making-of shows "two players, Ocelot" working in its co-op (old exe).

## 4. Choosing each player's type before mission load (concrete)
The vanilla order in TppMain.OnAllocate is Mission.Start -> sequence.MissionPrepare() (missions call RegisterTemporaryPlayerType here, e.g. s10240 L665) -> TppPlayer.MissionStartPlayerTypeSetting() -> ForceChangePlayerFromOcelot guard. Insert this right after MissionPrepare in the repo's existing exact-hash TppMain transform:
```lua
local C = {
  SNAKE={PlayerType.SNAKE,PlayerPartsType.NORMAL_SCARF,PlayerCamoType.OLIVEDRAB},
  DD_MALE={PlayerType.DD_MALE,PlayerPartsType.NORMAL,PlayerCamoType.OLIVEDRAB},
  DD_FEMALE={PlayerType.DD_FEMALE,PlayerPartsType.NORMAL,PlayerCamoType.OLIVEDRAB},
  OCELOT={PlayerType.OCELOT,PlayerPartsType.OCELOT,PlayerCamoType.OCELOT},
  QUIET={PlayerType.QUIET,PlayerPartsType.QUIET,PlayerCamoType.QUIET} }
local name = Tpp.MgsCoop and Tpp.MgsCoop.character
local c = name and C[name]
if c and not (Tpp.MgsCoop.isHost and mvars.ply_isExistTempPlayerType) then -- host keeps story-forced types
  TppPlayer.RegisterTemporaryPlayerType{playerType=c[1],partsType=c[2],camoType=c[3],handEquip=TppEquip.EQP_HAND_NORMAL,faceEquipId=0}
end
```
Then guard the revert: `if not TppMission.IsFOBMission(vars.missionCode) and not MgsCoopAllowsFobChars then TppPlayer.ForceChangePlayerFromOcelot() end`, together with the native patch at 0x9CD370. For DD soldiers, prefer vanilla sortie-prep staff selection on the guest's own save, because faceId is not part of the temp type.

Remote apply fallback, run on the game Lua thread after the remote instance is realized:
```lua
local function ApplyRemote(idx,a)
  if idx<0 or idx>3 or type(Player.SetPartsInfoAtInstanceIndex)~="function" then return end
  Player.SetPartsInfoAtInstanceIndex{instanceIndex=idx,type=a.t,partsType=a.p,camoType=a.c,handType=a.h or PlayerHandType.NORMAL,faceId=a.f or 0}
end
-- local capture for the packet:
-- {t=vars.playerType,p=vars.playerPartsType,c=vars.playerCamoType,h=vars.playerHandType,f=vars.playerFaceId,fe=vars.playerFaceEquipId}
```
Fields to synchronize per player: type, parts, camo, hand (or the handEquip id), faceId, faceEquipId, staff header/seed, suit level; for AVATAR also the 13 preset bytes and the 79-byte record; emblem (Dynamite SendEmblem); and weapons, which render through the equipment ghost groups. Derive the remote index from the native local index (P+0x214), not a hardcoded 1.

## 5. D-Dog feasibility (STATIC + UPSTREAM)
There is no dog PlayerType and no dog player motion graph. D-Dog is `TppBuddyDog2` (GAME_OBJECT_TYPE_BUDDYDOG2) and is AI-driven. Dynamite (MAKING_OF L359) reports Quiet and D-Dog are local-only entities: each player's buddy is invisible to the other. The only lever is the `TppBuddy2BlockController` command API (RequestMoveToPosition 0xA48C80, RequestWalkToPosition 0xA49010, CallBuddy 0xA44560, ...) plus GameObject commands. A "P2 is D-Dog" mode would need a host-authoritative dog, packet-driven move/attack orders, dog transform replication, and redirecting the guest's camera and input away from their own TppPlayer2. That is new subsystem work with poor feel. Honest alternatives: P2 as a Diamond Dogs soldier (DD_MALE/DD_FEMALE, any DD camo, which reads as 'a Diamond Dog'), or later 'P2 commands a shared D-Dog'.

## 6. Single-PC test for this area
After the Sandboxie guest joins, have the host select Snake and the guest select a DD staff in sortie prep. On each side log `Player.GetPlayerType{instanceIndex=i}` for i=0..3 and the local index. Read Q arrays externally with an extended read-only inspect_player_system.py. Pass condition: each side shows the partner's type in the remote slot and the partner visibly renders as a DD soldier. If the remote slot stays 0/0/0 (Snake defaults), enable the mod appearance packet with SetPartsInfoAtInstanceIndex.

## Sources
- [Dynamite issue #109](https://github.com/unknown321/dynamite/issues/109) (avatar renders as Snake outside FOB)
- [Dynamite issue #43](https://github.com/unknown321/dynamite/issues/43) (buddy issues)
- Dynamite docs/MAKING_OF.md and hook/src/dynamitePatches.cpp L130-151 (local copy)
- IH InfFova.lua, InfFovaIvars.lua L46-110 and L1071, InfPlayerParts.lua (github.com/TinManTex/InfiniteHeaven, tpp/gamedir-ih/GameDir/mod/modules)
- [FOBCharacterEnabler (Nexus 518)](https://www.nexusmods.com/metalgearsolidvtpp/mods/518)
- [PC Gamer: Quiet playable in FOB](https://www.pcgamer.com/metal-gear-solid-5-update-makes-quiet-playable-in-fob-missions/)
- MockFox liveNotFoundInLuaRefs (github.com/TinManTex/MockFox)