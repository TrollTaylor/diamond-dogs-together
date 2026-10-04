# Characters in co-op

Each player picks the character they play on their own PC. The choice is made
when a mission loads, through the game's own "temporary player type" (the
same mechanism the story uses to force Snake in some missions). Your save
keeps your normal character next to the override (see [What your save keeps](#what-your-save-keeps)):
the next mission that does not apply an override restores it, as the game
does after those story missions.

Status: implemented and unit/fixture tested. **The character is not yet
verified in a live game.** The first live run (October 3, 0.11.0) showed the
override not taking effect on a title Continue into Mother Base; the cause is
found and fixed (see [The October 3 failure](#the-october-3-failure)), but the
fix itself still needs a live run. See [What has been verified](#what-has-been-verified).

## Who can be what

| Character | `character=` | Player 1 (host) | Player 2 (guest) | Notes |
|---|---|---|---|---|
| Game default | `default` (or no coop.ini) | yes | yes | Whatever you picked in sortie preparation. Nothing is changed. |
| Snake | `snake` | yes | yes | Uses your sortie Snake outfit. |
| Diamond Dogs soldier, male | `dd_male` | yes | yes (fallback default) | A staff member you deploy in sortie preparation is kept as-is (face, stats). Otherwise a default face is used. |
| Diamond Dogs soldier, female | `dd_female` | yes | yes | As above. |
| Avatar | `avatar` | yes | yes | Your partner may see Snake instead (see limits). |
| Ocelot | `ocelot` | yes | yes | Needs the Ocelot/Quiet bypass (automatic). One revolver only. |
| Quiet | `quiet` | yes | **yes, default for Player 2** | Needs the bypass. Replaced by the fallback where story Quiet is present. |
| D-Dog | n/a | no | no | Not possible: D-Dog is an AI buddy, not a player type, and has no player animations. |

The decision for Player 2 is: **Quiet by default, a Diamond Dogs soldier as
the fallback**. The lab scripts write the guest's `coop.ini` that way
(`lab.json`: host `default`, guest `quiet`). Player 1 keeps the game default
unless its own `coop.ini` says otherwise.

## How to configure

Create or edit `mgs-coop\coop.ini` in **your own** game folder (the folder
that contains `mgsvtpp.exe`). Each PC has its own file.

```ini
# Player 2 example
character=quiet
character_fallback=dd_male
```

- `character=` one of `default`, `snake`, `dd_male`, `dd_female`, `avatar`,
  `ocelot`, `quiet`. Anything else is ignored (treated as `default`).
- `character_fallback=` used where the chosen character cannot be used. One of
  `dd_male` (default), `dd_female`, `snake`, `avatar`. Ocelot and Quiet are not
  accepted as a fallback.
- The file is read once when the game starts. Restart the game after editing.
- The setting is only used in the co-op script modes (`host-v2` / `host-v3` /
  `client-v1`, and the offline lab mode `offline-v1`, in
  `mgs-coop\script-observe.enabled`). In any other mode, or without
  `coop.ini`, the game behaves exactly as before. offline-v1 answers only the
  character receipts (no session receipt); the offline lab makes the guest play
  Quiet (`tools/lab/lab.json` `offline.characters`). The partner instance that
  shows a remote Quiet needs no bypass: the reset at 0x9CD353 runs only for the
  local index (0x9CD2D5). Statically the partner's packages (parts, camo,
  facial motion) are chosen per instance from its own type (0x9B41B0); that a
  remote Quiet actually loads and renders that way is **unverified until a
  live run** ([partner-anim-appearance.md](partner-anim-appearance.md),
  experiments B1/B4). For a regression rerun of the proven offline E0-E8 with
  the default characters, prepare with `Prepare-LabInstance.ps1 -Offline
  -Character default`.

## Where the choice applies

| Situation | What happens |
|---|---|
| Story missions (10xxx/11xxx) and free roam (30xxx), started normally | Your character is applied when the mission loads. |
| Continue from the title screen, continue from a checkpoint, retry | Also applied (since this fix). These loads restore the checkpoint first and then apply your character the same way the game does for story missions that must keep a forced type. |
| ACC / helicopter (40xxx), FOB (50xxx), MGO, title screen | Not changed. The ACC shows your normal character. The title screen (title mode) is skipped explicitly. |
| Missions whose script forces the player: 10010 (prologue), 10280 (Truth), 10030 (Diamond Dogs), 10240 (Shining Lights), 10151 (Sahelanthropus), 11043 / 11044 (Subsistence) | The story's own choice wins. Any other mission that sets a temporary type in its preparation also wins. |
| Right after the prologue (story sequence `CLEARD_ESCAPE_THE_HOSPITAL`, e.g. 10020 Phantom Limbs) | The game forces Snake (scarf, tiger stripe) at mission start; this wins (`reason=story_sequence_snake`). |
| Quiet chosen in 10050 / 11050 (Cloaked in Silence) or 10260 (A Quiet Exit) | The fallback is used. |
| Quiet chosen while buddy Quiet is deployed | The fallback is used. |
| The Ocelot/Quiet bypass cannot be enabled (unexpected game version, mode) | The fallback is used. |
| Snake-only cutscenes | Expected: the game switches to Snake for the cutscene and back afterwards, as it does for DD soldiers. Not checked for Quiet/Ocelot. |

## The October 3 failure

Live evidence (guest, `character=quiet`, mission 30050 reached through the
title screen's Continue):

```
character=apply; mission=30050; requested=quiet; applied=quiet; type=6; reason=override; bypass=1; real=0
character=ocelot_reset_skipped; mission=30050
character_readback=start; local=0; vars=0; players=ready; limit=2; i0=0; i1=0; raw0=0; raw1=255; quiet_ocelot_read_as=0
```

The guest spawned as Snake. The decision, the native bypass and the revert
skip all worked; the registered type was simply never applied. The reason is
in the game's own scripts (all extracted read-only from this install, hashes
below):

1. Continue on the title screen (`title_sequence.lua`,
   `OnEndFadeOutSelectContinue`) calls `TppSave.VarRestoreOnContinueFromCheckPoint()`
   and loads the saved mission. A mission saves a checkpoint right after it
   starts (`TppSequence.lua`, leaving `Seq_Mission_Prepare` on a ground
   start, or `TppPlayer.UpdateCheckPointOnMissionStartDrop` when you land from
   the helicopter), and free roam saves more (`f30050_sequence.lua`:
   `CHK_MainGame`, `CHK_PazRoom`, `CHK_BattleHanger`). Every checkpoint save
   sets `gvars.sav_varRestoreForContinue = true`
   (`TppMission.VarSaveOnUpdateCheckPoint` -> `TppSave.ReserveVarRestoreForContinue`).
   So a save made inside a mission is normally continued through the
   checkpoint path.
2. `TppMain.OnAllocate` then sees `TppMission.IsMissionStart() == false` and
   calls `TppVarInit.InitializeForContinue(n)` instead of
   `InitializeForNewMission(n)` + `TppPlayer.MissionStartPlayerTypeSetting()`.
3. `InitializeForContinue` reloads every var from the checkpoint and calls
   `MissionStartPlayerTypeSetting()` only if
   `n.sequence.ALWAYS_APPLY_TEMPORATY_PLAYER_PARTS_SETTING` is set. f30050
   does not set it. `RegisterTemporaryPlayerType` only fills `mvars`; without
   `MissionStartPlayerTypeSetting` nothing copies them into `vars.playerType`.

The fixture tests reproduce this with the real `TppMain.lua`, `TppPlayer.lua`
and `TppVarInit.lua`: the vanilla continue ignores a registered Quiet, the
fixed chunk applies it.

It was not the player package: the mod's two-slot player object (same as the
FOB `two_players_game_obj`) has an empty `partsType` and no Snake-only parts
list, and parts are chosen natively from `vars` for the local instance.

## How it works (short)

- `lua/coop/character.lua` runs inside `TppMain.OnAllocate`, right after the
  mission's own `MissionPrepare` and before the `OnLoad`/`DeclareSVars` part.
  It asks the mod for `character=`/`character_fallback=`, decides, and calls
  `TppPlayer.RegisterTemporaryPlayerType{...}`.
- New mission start: `MissionStartPlayerTypeSetting` saves your own type in
  `gvars.ply_last*UsingTemp`, sets `gvars.ply_isUsingTempPlayerType`, and
  copies the registered type into `vars.playerType`, `vars.playerPartsType`,
  `vars.playerCamoType`, `vars.handEquip` and `vars.playerFaceEquipId`.
- Checkpoint continue: the chunk sets
  `n.sequence.ALWAYS_APPLY_TEMPORATY_PLAYER_PARTS_SETTING = true` for this one
  allocation, so the game's own `InitializeForContinue` runs the same
  `MissionStartPlayerTypeSetting` after it reloads the checkpoint. The chunk
  sets the flag only when the mission has not set it itself, and puts the
  previous value back at the end of the same `OnAllocate` (in the hooked
  `ForceChangePlayerFromOcelot`), or at the next allocation if that end call
  never ran, or right away if the chunk fails. Both paths apply the type at
  the same point, before `TppEquip.StartLoadingToEquipMissionBlock()`.
- Safety net: at the end of `OnAllocate`, if a type is still registered by
  the chunk but `vars.playerType` does not have it (a load path that ran no
  type setting at all), the chunk applies it there with the game's own
  `SaveCurrentPlayerType` + `ApplyTemporaryPlayerType`. The log then says
  `late=1`. With the fix above this is not expected to happen; if it does,
  it is a finding for the next run (it runs after the equipment block
  started loading, so the hand equipment may not match).
- A save made under an earlier override and continued with a character that
  keeps your own type (for example the checkpoint has a DD soldier, `character=snake`
  and your own type is Snake): the flag is set without a registered type, so
  the game's own restore runs on continue too.
- Outside FOB, the game turns Ocelot and Quiet back into Snake in two places.
  The script skips the Lua revert (`TppPlayer.ForceChangePlayerFromOcelot`)
  once, at the end of that same allocation. The revert still runs when you
  abort mission preparation. The native revert (inside `UpdatePartsStatus`)
  is bypassed by a two-byte patch that is switched on only for missions where
  Ocelot/Quiet is applied, switched off for every other mission, and refused
  unless the game code matches byte for byte.
- The lab readback (diagnostics only, `lua/coop/character_readback.lua`)
  starts after the mission's `OnMissionGameStart` returns and stops at
  `OnTerminate`. Before each `Player.GetPlayerType` call it asks the mod to
  sample the player object natively (read-only, never dereferencing a null
  pointer), because that game function has no null check and the player
  object does not exist while a mission loads or unloads.

Infinite Heaven, for comparison, writes `vars.playerType` (and parts, camo,
face, face equipment) directly from its menu, also inside a mission
(`InfFovaIvars.lua`, `this.playerType.OnChange`); the engine reloads the parts
when the vars change, the same way `s10151` swaps a DD soldier for Snake
mid-mission and waits for `PlayerStatus.PARTS_ACTIVE`. For Ocelot/Quiet it sets
the game status `S_IS_SORTIE_PREPARATION` to dodge the native revert, which IH
users report breaks the pause menu; this mod uses the byte patch instead. IH
does not use the temporary type, so its choice is written into the save
directly. This mod keeps the vanilla save/restore cycle instead.

## What your save keeps

The temporary type is the game's own mechanism, and it is saved:

- While an override is active, a checkpoint or mission-start save contains
  the override in `vars` and your own appearance in `gvars.ply_last*UsingTemp`
  with `gvars.ply_isUsingTempPlayerType = true`.
- The next mission that starts without an override (for example the ACC after
  you leave, or any mission with `character=default`) restores your own
  appearance from those gvars, as the game does after Shining Lights.
- If you continue such a save **without** the mod (or with `character=default`),
  the continued mission keeps the saved override until the next mission
  start, except Quiet and Ocelot: the game's own end-of-allocation revert
  turns them back into your sortie character immediately. Nothing is lost:
  the own appearance stays in the gvars until it is restored.
- The mod never writes the save itself and never changes the sortie
  selection (`vars.sortiePrep*`).

The lab redirects saves to copies; real play writes the user's save as
described above.

## Known limits

- **Your partner's view is unverified.** Each game applies its own player's
  character. Whether the other screen shows it depends on the game's own FOB
  synchronizer (native code at 0x12A5810 copies type/parts/camo/face for a
  remote player). This is untested. Avatar is reported to show as Snake to
  others outside FOB (Dynamite issue #109).
- Quiet's and Ocelot's abilities and animations were designed for FOB and
  have not been tested in story missions with this mod. Infinite Heaven users
  report problems such as missing sounds.
- Ocelot gets one revolver. Dynamite's dual-revolver patch is not ported.
- Facial animation is loaded per player type. Mixed types (for example Snake
  and Quiet) may show wrong facial animation on the remote player.
- A Diamond Dogs soldier forced from a non-DD sortie gets a default face
  (male 0, female 350, the same defaults Infinite Heaven uses) or your last
  sortie face of that gender. Deploy the staff member you want in sortie
  preparation to get their exact face and stats.
- On Mother Base you may meet a second Ocelot or Quiet (the base NPCs), and
  Mother Base demos that involve Quiet were not checked with Quiet as the player.
- Buddy Quiet is detected from the buddy you deployed (`vars.buddyType`) at
  mission load. This uses the same check as the game's own scripts, but its
  timing has not been tested.
- The in-game readback (`Player.GetPlayerType`) reports Ocelot and Quiet as
  Snake (0). The lab readback therefore also logs the raw per-instance bytes
  (`raw0`/`raw1`, 5 = Ocelot, 6 = Quiet, 255 = empty slot) and your own `vars`.

## Logs

`mgs-coop\script-observer.log` (co-op modes only). Values contain no spaces;
`; ` separates fields.

| Line | Meaning |
|---|---|
| `character_config=quiet; fallback=dd_male; coop_ini=1; reset_bypass_needed=1; applied_in=TppMain.OnAllocate` | Settings read at startup. |
| `character=<result>; mission=...; requested=...; applied=...; type=...; reason=...; bypass=0/1; real=...; start=<new\|continue\|unknown>; always=<0\|1>` | Decision for this mission. `result` is `skip`, `keep` or `apply`. `start` is the `OnAllocate` path (`continue` = checkpoint/title Continue). `always=1`: the continue flag was set for this allocation. |
| `character=allocated; mission=...; vars=6; parts=27; camo=116; temp=1; start=continue; late=0` | End of `OnAllocate` (every apply decision, including a kept sortie choice such as `reason=override_sortie_kept`, lab or not; never for `skip`/`keep`): what `vars` hold before the player is created. `temp` is `gvars.ply_isUsingTempPlayerType`. `late=1; before=<type>` means the safety net applied the type (see above). |
| `character=face; mission=...; face=...` | A Diamond Dogs face was set. |
| `character=ocelot_reset_skipped; mission=...` | The Lua Ocelot/Quiet revert was skipped once. |
| `player_type_patch=active; ...` / `player_type_patch=inactive; ...` | The native bypass changed state. |
| `player_type_patch=refused; reason=...` | The bypass was not enabled. The fallback is used. |
| `character_readback=<tag>; verdict=<v>; expect=<type\|na>; local=<n>; vars=<type>; parts=<n>; camo=<n>; temp=<0\|1>; players=<state>; limit=<n\|na>; i0=..; i1=..; raw0=..; raw1=..; quiet_ocelot_read_as=0` | Lab readback (only with `lab.ini` active and an apply/keep decision). `tag` is `start`, `change` or `manual`. See below. |

Readback fields:

- `verdict`: `applied` = `vars.playerType` and the local instance's native
  type both equal `expect`; `native_pending` = vars are right but the native
  slot is not (yet); `vars_applied` = vars are right, the native slot could
  not be read; `not_applied` = `vars.playerType` differs from `expect`;
  `keep` = story-forced decision, nothing expected.
- `expect`: the type the decision asked for (for a kept sortie choice, your
  own type).
- `local`: `PlayerInfo.GetLocalPlayerIndex()`; `raw<local>` is compared.
- `players`: `ready`, `stopped` (outside the mission's lifetime),
  `native_unavailable`, or the native reason the player object is not
  readable (for example `player_absent`); `i0`/`i1`/`raw*` are `na` unless
  `ready`. `i0`/`i1` come from `Player.GetPlayerType`, which reads 5/6 as 0.

What a good run looks like on the guest (Quiet, title Continue into 30050):

```
character=apply; mission=30050; requested=quiet; applied=quiet; type=6; reason=override; bypass=1; real=0; start=continue; always=1
character=allocated; mission=30050; vars=6; parts=27; camo=116; temp=1; start=continue; late=0
character=ocelot_reset_skipped; mission=30050
character_readback=start; verdict=applied; expect=6; local=0; vars=6; parts=27; camo=116; temp=1; players=ready; limit=2; i0=0; i1=0; raw0=6; raw1=255; quiet_ocelot_read_as=0
```

## What has been verified

| Item | Level |
|---|---|
| Root cause of the October 3 failure (continue path skips the type setting) | Static: the vanilla `TppMain`, `TppVarInit`, `TppSave`, `TppMission`, `title_sequence` and `f30050_sequence` scripts of this install; reproduced in the fixture with the real `TppMain`/`TppPlayer`/`TppVarInit` |
| Continue fix: flag set only when absent, previous value restored at the end of `OnAllocate`, next allocation or on failure; Quiet applied over a Snake checkpoint inside the real `InitializeForContinue` (`late=0`) | Lua fixture tests (`character_lua_check`, `character_game_lua_check`, and `character_composed_lua_check` on the complete DLL `TppMain` composition written by `host_trial_patch_check`, which also proves the check fails when `n` is shadowed or the end-of-`OnAllocate` call is lost) |
| Safety net (`late=1`), stale-override restore on continue, `start=`/`always=` reporting | Lua fixture tests |
| Native reset branch: bytes, complete function hash, jump target, uniqueness, FOB flag source; enums PlayerType QUIET 6 / OCELOT 5, PlayerPartsType 27 / 26, PlayerCamoType 116 / 115 | Static analysis of the installed 1.0.15.4 exe (`tools/generate_player_type_contract.py` re-run on October 3: regenerated output identical) |
| Bypass enable/disable/refusal logic, including on an in-memory copy of the real function | Unit tests (`player_type_patch_tests`) |
| Receipts, coop.ini query, log lines | Unit tests (`character_receipt_tests`) |
| Decision table, fallbacks, story-forced missions and story sequence, title mode, faces | Lua fixture tests |
| Readback lifecycle: no `Player.GetPlayerType` call during loading, after `OnTerminate`, in title mode, without the native guard, or above the instance limit; verdicts; lines never exceed the 192-byte payload | Lua fixture tests, including runs of the real composed `TppMain` |
| Readback guard: exact `GetPlayerType`/root-getter bytes, every pointer stage, null and unreadable pointers | Unit tests and static analysis |
| Decision, bypass, Lua revert skip and 2-player readback in a live game | **Live, October 3** (guest log); the type itself was not applied then |
| Character visible and playable in a mission after this fix, local or remote | **Not verified (needs a live game)** |

### Live check for the next run

1. Guest `coop.ini` `character=quiet` (lab default), host default. Reach 30050
   the same way as on October 3 (title Continue).
2. Guest log: `start=continue; always=1`, then
   `character=allocated; ...; vars=6; parts=27; camo=116; temp=1; ...; late=0`,
   then `character_readback=start; verdict=applied; ...; raw0=6`.
   Visually: Quiet. Stays Quiet for 2+ minutes.
3. If `late=1` appears, the flag did not reach `InitializeForContinue`; if
   `verdict=native_pending` persists, vars are right but the engine did not
   take them; if `verdict=not_applied` appears after `start`, something reset
   `vars.playerType` after allocation (record when).
4. Leave to the ACC: `character=skip; ...; reason=mission_class`; the ACC shows
   the guest's own character (vanilla restore).
5. Start a mission from the ACC (new mission path): `start=new`, same results.

## Re-extracting the game scripts

`character_game_lua_check` and `character_composed_lua_check` (the latter
also needs the `host_trial_patch_check` test) use the extracted vanilla
scripts when they are present with these SHA-256 values (they look in
`build/`, `MGS_GAME_LUA_DIR` and the main checkout's `build/`):

| Script | SHA-256 | Saved as |
|---|---|---|
| `/Assets/tpp/script/lib/TppVarInit.lua` | `cebb1fc45e992e479dcbf1c6b7f0fe391142fa07a4a696bd429ecab92dd9eecf` | `character-game-lua-20261003/package-00.lua` |
| `/Assets/tpp/script/lib/TppPlayer.lua` | `7407bd41dd82913edfe9ef7fbb344f443adc7399d42f86c7798ba6b02c94e5b9` | `.../package-01.lua` (or `startup-script-inspection-20260909/package-01.lua`) |
| `/Assets/tpp/script/lib/TppMain.lua` | `6542d997c3f29742ff7ea0587f7f307794586a9b7a8ee3114b58e79e7f0b6103` | `.../package-02.lua` (or `player-allocation-lifecycle-20260909/package-00.lua`) |
| `/Assets/tpp/script/lib/TppMission.lua` | `e99e6addd4c5135fb2d77ab58632d5e349a626eb7f10ea38301372d127f7404a` | `.../package-03.lua` (reference only) |
| `/Assets/tpp/script/lib/TppSave.lua` | `233ca87d04186563e644c972f49b43612525370a77386b1945bc1b5f73dba3cb` | `.../package-04.lua` (reference only) |
| `/Assets/tpp/script/lib/TppGVars.lua` | `9a878834d8f5992ff795d14f2577d8991f86102874d82545e6935721c499503b` | `.../package-05.lua` (reference only) |
| `title_sequence.fpkd` (0/00.dat) -> `title_sequence.lua` | fpkd `d7c399e229e0f1d383ac105acac70f2ab0dd00d529a572f651541f1f5365d430`, lua `1d9eb74704149be99b6a55fa9982be5c2785b4c7f64fad091857cfdc52571a0c` | `.../package-06.fpkd` (reference only) |
| `/Assets/tpp/script/lib/TppSequence.lua` | `db56372d8990652dcfacddb4f88950171f77b42ae9fcc2869ed22985ce4fca53` | `.../package-07.lua` (reference only) |
| `f30050.fpkd` (chunk3.dat) -> `f30050_sequence.lua` | fpkd `3b38bdb7cc8d1de21d76fea8064ed097d9164038af548a5d9dc090225fccdb49`, lua `08ce06a35b8f8561b7de9dd7e7dabba8292c118af1d2ae1ded1db2085201bc28` | `.../package-09.fpkd` (reference only) |

All from `master/0/00.dat` except `f30050.fpkd` (only in `chunk3.dat`); `chunk0.dat` also holds an older `title_sequence.fpkd` (`package-08.fpkd`) whose continue handler restores the checkpoint the same way. The archives are opened read-only; `build/datfpk.exe <file>.fpkd` unpacks an fpkd next to it. From the
repository root (Git Bash; `MSYS_NO_PATHCONV=1` keeps the archive paths intact):

```
MSYS_NO_PATHCONV=1 build/archive_probe.exe -out build/character-game-lua-20261003 -paths "/Assets/tpp/script/lib/TppVarInit.lua,/Assets/tpp/script/lib/TppPlayer.lua,/Assets/tpp/script/lib/TppMain.lua,/Assets/tpp/script/lib/TppMission.lua,/Assets/tpp/script/lib/TppSave.lua,/Assets/tpp/script/lib/TppGVars.lua,/Assets/tpp/pack/mission2/common/title_sequence.fpkd,/Assets/tpp/script/lib/TppSequence.lua,/Assets/tpp/pack/mission2/free/f30050/f30050.fpkd"
```

`archive_probe.exe` is built from `tools/archive_probe` (see its README).
Extracted game scripts stay in `build/` and are never committed or shipped.
