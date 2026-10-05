**World sync: native feasibility (static analysis of mgsvtpp.exe 1.0.15.4)**

Everything below comes from reading the exe, the repo and Dynamite's sources. Nothing was launched or changed. All addresses are RVAs. Scratch scripts and extracted files are in `C:/Users/taylo/AppData/Local/Temp/claude/D--Games-steamapps-common-MGS-TPP/e7a6e70e-f530-4f59-b4a3-60e868204a66/scratchpad/world/` (`w.py`, `scan_team2.py`, `hnd.py`, `hnd2.py`, `packs1/`, `scripts1/`).

## Summary

1. **Perception:** host soldiers cannot notice the partner body without a patch. Every enemy decision I found uses one player index, and offline it is always 0 (the local player). The only lever is the FOB offense-index function at 0x1E02770. Detouring it would run inside soldier/command-post (CP) update code, likely on worker threads, which breaks the hard constraint. Recommendation: each PC detects its own player and reports it; the host applies the result with script commands.
2. **Damage:** use script commands that set the outcome (`ChangeLifeState`, `RequestForceFulton`, `RequestVanish`). They are main-thread and patch-free. Replaying a hit (`AddDamage`) is secondary. The native damage hooks the repo already mapped are not needed.
3. **Soldier positions:** the read layout is known, but there is no static pointer to the soldier manager. Use Lua `GetPosition` to start. Guest soldiers should stay AI-driven and be corrected with route commands, not moved natively.
4. **Partner outside 30050:** blocked mainly by assets. Every mission pack carries its own single-player controller, which must be swapped per mission, plus Lua gates and an area check on where the partner can appear.

**Challenge to "host-authoritative":** the evidence supports a split model. Discrete world state is host-authoritative. Perception is owner-authoritative: each PC detects its own player. Soldier simulation should belong to whichever PC has a player nearby, because in free roam the host never fully simulates soldiers near a guest who is far away.

## (1) Do soldiers perceive player instance 1 offline?

**Answer: no, functionally. Confidence: high for the gate, medium for "no patch-free lever exists".**

**How the gate works**
- Every enemy decision site I found reads one "offense player instance index" from a FOB team interface. It is a static singleton at 0x2B56D90, vtable 0x25564F0, built at 0x206CC90 and 0x208C550.
- It is reached through `GetQuarkSystemTable` (0xBFF050), then +0x98 (the registry at 0x2C00D40, built by 0x993A10 and stored at 0x992FF2), then +0x1A0.
- The soldier manager keeps its own copy of the registry pointer at +0x650 (seen at 0x135BACE).

| Interface slot | Function | What it does |
|---|---|---|
| +0x210 | 0x1E030B0, IsOffenseTeamByOnlineFobLocal | session vt+0x28 |
| +0x218 | 0x1E03080, IsDefenseTeam | the negation of the above |
| +0x220 | 0x1E02770, GetFobOffensePlayerInstanceIndex | `QST+0x78 → vt+0x40` (session); if non-null, returns the host member index, otherwise 0 |
| +0x228 | 0x1E026B0, GetFobDefensePlayerInstanceIndex | |
| +0 | 0x1E02D70 → 0x53CA40 | returns the PlayerInfo record for an index (bank 0x2BD3FF0, buffer selector 0x2BD3FD0, stride 0xE0) |

**What it returns offline:** 0 either way. There is no session member, and the host index of a single-player session is 0. Note that 0x1E030B0 has no null check, which suggests a session-like object exists even offline. Changing the result without a code patch would mean creating a session, which is forbidden.

**Who uses it (14 call sites of slot +0x220).** The scan is in `scan_team2.py`. Two representative sites:
- **Soldier target search, 0x1325530 (chunk at 0x13255A5).**
  - It walks the soldier's candidate entries of type 5.
  - Each entry's game-object id goes through registry +0x228 / vt+0x50.
  - A player entry whose index is not the offense index is skipped (0x1325619..0x132562B, `cmp ecx,eax; je`). Only non-player objects and the offense player survive.
- **Command post, 0xD6B7E0.**
  - It takes the offense index at 0xD6B879, then the PlayerInfo record for that index (`call [r8]`) and its position (vt+0x10).
  - It tests the offense player's bit in a per-player bitmask (`bt ecx,ebx` at 0xD6B8A4).
  - So perception data is laid out per player, but decisions read only one player.
- The other sites are 0xD2F790, 0xD6C8C0, 0xD6FD90, 0x5ECED0 and 0x1949xxx–0x1955xxx; I did not classify these.
- This matches Dynamite's own account in `docs/MAKING_OF.md`: enemies attack a single player id, and the host flips it to the nearest player.

**What is not the gate.** Instance count and realization are not: realizedCount = 2, and the PlayerInfo updater 0x12CE870 fills records for every instance. The sight controller's host check (0x133C9AB, taking the role query at 0x313FE0) and DoesFobGhostPlayer are side branches, not the selection.

**Options**
- **(a) Patch-free:** none found.
- **(b) Tiny patch:** detour 0x1E02770. The full body is already contract-verified in `native/enemy_targeting.cpp`, but it is gated on the FOB session. The problems:
  - The hook body runs inside soldier/CP update code, whose thread is unverified (probably job threads). That conflicts with "no worker-thread hooks".
  - The model is a single target. While the index is 1, host soldiers ignore the host player entirely — Dynamite's desync complaint.
  - Hits on the puppet body would land on host-side instance 1. The player damage callback 0xFF9D30 / 0xFFA25F has an online/index-1 early return, so its offline behaviour is unknown.
  - **Do not adopt this without the user's explicit decision.**
- **(c) Recommended: simulate.** In each PC's own world, soldiers target that PC's local player (index 0). So the guest's world already perceives the guest correctly. The guest reports what happened to the host over the reliable channel. The host applies it with script commands:

| Effect | Command | Handler / evidence |
|---|---|---|
| Alert phase | command post `SetPhase{phase}` | handler 0xD5FB81 (CP dispatcher 0xD5DF30) |
| A notice | command post `RequestNotice{type,targetId,sourceId}` | handler 0xD61105; game use in s10010 `RequestNoticeGimmick` |
| A specific soldier discovers "the player" | soldier `SetDiscoveryPlayer{}` | 0x135B32A, which calls `[M+0x68]`.vt+0x108 with the local player id at M+0x28C |

The host should own the phase and broadcast it back.

## (2) Damage and neutralization on behalf of the remote player

**Answer: use outcome commands from the Lua thread. Confidence: high that the handlers exist and how they route; medium on side effects.**

The TppSoldier2 dispatcher is 0x1356170 (vtable slot 0x23E79C0; `this` = soldier manager M; edi = soldier index). Its handlers queue work through soldier vt+0x1F8 rather than running damage inline.

| Command | Handler | Fields / effect |
|---|---|---|
| `ChangeLifeState` | 0x135BB12 | `{state, stance}`; states 1..4 map to action requests 0x4F/0x4D/0x4C/0x4B; stance is written to `*(M+0x98)`, records stride 0x70, +0x34. Used by game scripts (s10030_enemy.lua:957 and :977, s10040_enemy.lua:1079, s10100 and others). **Preferred "apply outcome" path.** |
| `AddDamage` | 0x135DAD7 | `{attackId, ownerGameObjectId, isOneHitKill, isHeadShot}`, also via vt+0x1F8; game use at s10010_sequence.lua:630. A replay; the result depends on the target's state. Use only for kill credit or headshots. |
| `RequestForceFulton` | 0x13601F3 | `{}`; game use exists |
| `SetToHeliRecoveredComplete` | calls 0x13522B0 | |
| `RequestVanish` | 0x135FF83 | `{enabled, alwaysDisable, label}` |
| Also present | | `RequestDisableWithFadeout`, `SetEverDown`, `SetDisableDamage`, `RecoveryLife`, `SetEnabled` |
| Readback | | `GetLifeStatus` (calls 0x1369240), `GetStatus` |

- The native paths in `docs/shared-damage.md` (DefenseCallback 0xFF9D30 / 0xFFA25F) handle damage dealt to players. The damage controller at 0xDA55F0 handles a host-role check. Neither is needed for applying soldier outcomes.
- Recommended flow: the guest hits a soldier in its own world and sends "soldier i → state s, cause c". The host validates and applies `ChangeLifeState` or `RequestForceFulton` / `RequestVanish`, then broadcasts the authoritative state. The guest re-applies only if its own state differs.

## (3) Soldier transforms

**Reading positions (high confidence on the layout).** The `GetPosition` handler at 0x135A2B4 shows the layout:
- position of soldier `idx` = `*(*(M+0x18)+0x20) + idx*0x10`, as `{x,y,z}` floats;
- per-index records: `*(M+0x98)`, count at +0x10, base at +8, stride 0x70.

**Finding M natively is not solved.**
- The player has a static root (0xAEC8D0 → 0x2A82C28); a pattern scan found no soldier equivalent.
- The generic id→object lookup 0xBF3FC0 jumps into protected code at 0x147B5C9D0.
- M's vtable base is 0x23E7710 (constructor around 0x134C9xx). A one-time, read-only, main-thread memory scan for that vtable pointer is possible but crude (medium confidence).

**Recommendation for the host snapshot:** start with Lua `GameObject.SendCommand(id,{id="GetPosition"})`, round-robin about 20 soldiers per tick at 5–10 Hz, and add life status on change. Move to native reads only if profiling demands it.

**Moving soldiers on the guest**
- TppSoldier2 has no Warp or SetPosition (from the earlier research).
- Soldiers do use the same character transform chain as the player: `CS.vt+0x388` returns T, and T+0x160 is read at 0x1361684..0x1361690. That chain is generic (vtable 0x24C8D80/0x24C8EF0, built through 0x1AD2940). So `T.vt+0x70`/`+0x68` would likely move a soldier's body (medium confidence).
- But the soldier's AI, route and navigation would fight it, and calling it from the main thread while AI jobs run is a race. Do not use it.
- Guest soldiers should stay AI-driven, with the same mission and routes. The host corrects coarsely:
  - `SetSneakRoute`, `SwitchRoute` or `SetAlertRoute`;
  - for large divergence, `SetEnabled{false,noAssignRoute}` → route snap → `SetEnabled{true}`;
  - life and removal through the commands in (2).
- Positions are for detecting divergence and for markers, not for driving bodies.
- `SetForceRealize` exists and can force realization of soldiers near the guest (cost unmeasured).

## (4) Player instance 1 outside 30050

**Answer: mainly blocked by assets, then Lua gates and an area check. Confidence: high.**

**Pack swap is 30050-only.** `lua/coop/player_package_selection.lua:19` swaps `f30050.fpk` → `mgs_coop_f30050.fpk` and appends the two-slot controller only when `missionCode == 30050`. The swap exists because each mission's pack ships its own single-instance controller. Extracted fox2 files:

| Pack | Controller file | Parts type |
|---|---|---|
| `f30010.fpkd` (chunk2) | `single_pl2_instance_scarf.fox2` | `PLTypeNormalScarf` |
| `f30020.fpkd` (chunk4) | `single_player2_instance.fox2` | `PLTypeNormal` |
| `f30050.fpkd` (chunk3) | `single_player2_instance.fox2` | `PLTypeNormal` |
| `s10036_area.fpkd` | scarf variant | |

- All of them use `single_player_game_obj.fpk`, `partsBlockCount` 1 and `instanceBlockSize` 563200.
- Two controllers at once produced the state-11 teardown wait (`docs/STATUS.md`: 13,419,280 vs 12,641,280 bytes).

**What each new mission needs**
- A renamed pack with the controller removed (generalise `tools/stage_controller_replacement.py`).
- A two-slot controller that matches the location's parts type (scarf in Afghanistan).
- 30010 has a second variant, `f30011.fpk` (label `recoverVolginDemo`). Story missions have several area packs (Dynamite deletes the controller from each).
- `native/archive_mount.cpp` pins one archive (3,720,192 bytes, sha256 `26ce7c…`). Adding packs means rebuilding and re-pinning it.
- Dynamite also raised `DEFAULT_EQUIP_MISSION_BLOCK_GROUP_SIZE` and added `coop_essentials.fpk` for memory headroom.

**Lua gates:** `lua/coop/partner_sync.lua:100` and `:167` (`vars.missionCode ~= 30050`).

**`SetAllInstanceActiveMode` prerequisites** (`docs/offline-partner-spike-plan.md`, realize policy 0x121B5E0):
- realizedCount must be 2, which only the two-slot package gives;
- instance 1's position must be inside the loaded area (0x121B8FF / 0x121B933);
- the guard `P.vt+0x1A0` (0x9B3D60) must pass.

In the large free-roam maps, a guest outside the host's streamed blocks will not realize. The code must cope with an unrealized partner (Warp then only writes the slot). The same streaming limit means host-side soldiers near a distant guest are unrealized. That is the main reason for proximity-based ownership instead of pure host authority.

## Open items to check live

- Does 0x1E02770 return 0 offline? Read QST+0x78 → vt+0x40 once.
- Thread affinity of the 14 offense-index callers.
- Whether `SetDiscoveryPlayer`, `SetPhase` and `RequestNotice` give acceptable host alerts.
- Whether `ChangeLifeState` on a soldier the guest neutralized leaves the host's mission bookkeeping (svars, kill counters) consistent; it may need the scripts researcher's TppEnemy helpers.
- `GetPosition` cost at 5 Hz across a full free-roam roster.