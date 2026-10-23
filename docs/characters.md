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
| Avatar | `avatar` | yes | yes | Your partner sees Snake instead (see [Avatar on the partner's screen](#avatar-on-the-partners-screen)). |
| Ocelot | `ocelot` | yes | yes | Needs the Ocelot/Quiet bypass (automatic). One revolver only. |
| Quiet | `quiet` | yes | **yes, default for Player 2** | Needs the bypass. Replaced by the fallback where story Quiet is present. |
| Story: Quiet or Ocelot | `story` | yes | yes | Quiet while she is a usable buddy on Diamond Dogs' team in **your own save**, Ocelot before she joins and after she leaves. Chosen again at every mission load. See [Story: Quiet or Ocelot](#story-quiet-or-ocelot). |
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
  `ocelot`, `quiet`, `story`. Anything else is ignored (treated as `default`).
- `character_fallback=` used where the chosen character cannot be used. One of
  `dd_male` (default), `dd_female`, `snake`, `avatar`. Ocelot, Quiet and Story
  are not accepted as a fallback.
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
| `story` | Resolved to Quiet or Ocelot only where a character is applied at all (the rows above that apply); every row above then applies to the result unchanged. See [Story: Quiet or Ocelot](#story-quiet-or-ocelot). |
| Snake-only cutscenes | Expected: the game switches to Snake for the cutscene and back afterwards, as it does for DD soldiers. Not checked for Quiet/Ocelot. |

## Story: Quiet or Ocelot

`character=story` follows Quiet's story in **your own save**: you play Quiet
while she is a usable buddy on Diamond Dogs' team, and Ocelot before she joins
and after she leaves (the joiner's "Story: Quiet or Ocelot" choice in the
planned Diamond Dogs Together launcher).

**When it is decided.** At every mission load, in the same `TppMain.OnAllocate`
step as every other character, from the save that is loaded at that moment.
It is not stored anywhere: when Quiet leaves in your save, the next mission
load gives Ocelot.

**What is read.** The game's own buddy state, through the same functions the
game's scripts use (all inside a protected call):

| Read | Meaning |
|---|---|
| `TppBuddyService.DidObtainBuddyType(BuddyType.QUIET)` | Quiet was recruited (set when you spare her in Cloaked in Silence, `s10050_sequence`; unset by `TppStory.RequestLoseQuiet` at the end of A Quiet Exit). |
| `TppBuddyService.CheckBuddyCommonFlag(BuddyCommonFlag.BUDDY_QUIET_LOST)` | Quiet has left. Set when A Quiet Exit opens (`TppStory._UpdateS10260OpenFlag`), unset by a reunion (`TppStory.RequestReunionQuiet`). |
| `TppBuddyService.IsDeadBuddyType(BuddyType.QUIET)` | Quiet is dead. |
| `TppBuddyService.CanSortieBuddyType(BuddyType.QUIET)`, or `TppQuest.IsCleard("mtbs_q99011")` | Quiet can be deployed. After you recruit her she waits in a cell on the Medical platform until the `mtbs_q99011` visit; `TppMission.SetSortieBuddy` then makes her deployable, but it runs later in the same `OnAllocate`, so the chunk counts the cleared visit too (otherwise the first mission after the visit would still be Ocelot). |

The game's own test for "Quiet is available as a buddy" (the `UnlockBuddyQuiet`
radio in `TppStory.lua`, also used for `QuietReceivesPersecution`) is
`TppStory.CanArrivalQuietInMB(true) and TppBuddyService.CanSortieBuddyType(BuddyType.QUIET)`,
where `CanArrivalQuietInMB(true)` is "obtained, not lost, not dead" (the `true`
ignores a temporary hospital stay). The chunk applies exactly that test, read
part by part so the log can say why:

| Save state | Result | `reason=` |
|---|---|---|
| `BUDDY_QUIET_LOST` set (A Quiet Exit is open or done, before any reunion) | Ocelot | `departed` |
| Quiet dead | Ocelot | `dead` |
| Not recruited (before Cloaked in Silence, or she was killed there) | Ocelot | `not_recruited` |
| Recruited, still in her cell (visit `mtbs_q99011` not cleared) | Ocelot | `not_deployable` |
| Recruited and deployable (or the visit is cleared), not lost, not dead; also while hospitalized | **Quiet** | `on_team` |
| The buddy functions or enums are missing, or a call fails | Ocelot | `unreadable` |

**Gates and fallbacks.** `story` passes the same checks as every other
character first: title mode, mission class (only 10xxx/11xxx/30xxx),
story-forced missions, the hospital story sequence, Subsistence and any
temporary type set by the mission keep the story's choice and resolve nothing.
After the resolution, Quiet goes through the usual Quiet rules: in 10050 /
11050 / 10260, or with buddy Quiet deployed, `character_fallback` is used
(default `dd_male`); if the Ocelot/Quiet bypass cannot be enabled, the fallback
is used as well. In practice: the first Cloaked in Silence (10050) is Ocelot
(Quiet is not recruited yet), A Quiet Exit (10260) is Ocelot (she has already
left when it opens), and an Extreme replay (11050) with Quiet on the team is
the fallback.

**Log.** One extra line before the usual decision line:

```
character=story; resolved=quiet; reason=on_team; mission=30050; flags=os--
character=apply; mission=30050; requested=story; applied=quiet; type=6; reason=override; bypass=1; real=0; start=new; always=1
```

`flags` are the raw reads: `o` obtained, `s` deployable (including the cleared
visit), `l` lost, `d` dead, `-` false; `na` when unreadable. In a Quiet
mission the decision line shows the fallback
(`resolved=quiet` and `applied=dd_male; reason=quiet_conflict`).

**Native side.** `coop.ini` accepts `character=story` (never as a fallback);
the query receipt passes `'story'` to the chunk; the native Ocelot/Quiet
bypass is allowed for `story` (the chunk asks for it only when the result is
Quiet or Ocelot, and sends BYPASS_OFF for a fallback).

**Not verified live.** The reads and their timing are taken from the game's
own scripts and checked against the real `TppStory.lua` and `TppMission.lua`
(see [What has been verified](#what-has-been-verified)); no live run has used
`story` yet.

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

- **Your partner's view.** Each game applies its own player's character; the
  partner's screen shows it through the mod's own look frames
  (`partner_look=net`, the play default; see
  [partner-anim-appearance.md](partner-anim-appearance.md)), not through the
  game's FOB synchronizer. Quiet seen by the partner was observed in the
  October 4 lab; the other types are not verified on the partner's screen.
  An Avatar is shown as Snake on the partner's screen (see
  [Avatar on the partner's screen](#avatar-on-the-partners-screen)).
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
- `story` reads the buddy state at mission load, the same moment the buddy
  check above uses; that timing (in particular right after a title Continue)
  has not been checked live.

## Avatar on the partner's screen

**Symptom.** A player who uses their Avatar (`character=avatar`, or the Avatar
chosen in sortie preparation) sees their Avatar; the partner sees Snake with
the default Snake face. The partner's log says so on every look apply:
`partner_look=apply; ...; type=0; ...; face=0; fe=0; rev=..; avatar_as_snake=1`.

**Why (static, 1.0.15.4).**

1. The partner body (instance 1) gets its appearance from the mod's look
   frame: type, parts, camo, hand, face and face equipment, 12 bytes
   (`player_sync.hpp` `LookPacket`; sender `partner_net::SubmitLook` with the
   local slot of the parts-info object Q). The Avatar's own description is not
   in it.
2. That description is a per-instance record of 0x4F bytes:
   `[Q+0xE8] + index * 0x4F` (`Player.CopyPartsInfoFromLocalPlayer` 0x9D13F0
   copies it with exactly that stride). Bytes 0x00..0x4C are a copy of the
   Avatar's saved description: `GetAvatarInfoFromScriptVars` 0xAE8390
   (community name) copies 17 bytes from +0x18E and a 60-byte block from
   +0x19F of its source (the script vars, per the name) when source+0x1DB is
   1; `UpdateAvatarInfo` 0x9B7E90 compares bytes 0, 1 and that 60-byte block
   to decide whether a built Avatar is stale. +0x4D is set to 1 by
   `SetAvatarInfo`, `CopyPartsInfoFromLocalPlayer` and the local refresh.
3. For the local instance, `UpdatePartsStatus` refreshes that record from
   your own Avatar description (call at 0x9CE35A). For any other instance,
   a scan of every `imul ..., 0x4F` + `[..+0xE8]` access finds only these
   writers: `SetAvatarInfo` 0x9C9E70 (called only by the mirror-mode copy at
   0x9CE858), `CopyPartsInfoFromLocalPlayer` (Lua), and the FOB synchronizer
   (0x12CC9C4 copies the record out of a FOB sync packet and sets
   `Q+0x194 |= 1 << index`; 0x12CCCFF clears +0x4D). The other hits
   (0x9B27D4, 0x9CD47A, 0x9CD717, 0x12A849B) read it.
   `Player.SetPartsInfoAtInstanceIndex`, which the mod uses to apply a look,
   has no Avatar field. So outside FOB nothing gives instance 1 the partner's
   record. This is also what Dynamite issue #109 ("Avatar shows as Snake
   outside FOB") runs into.
4. Applying type AVATAR (3) without the record would build instance 1's
   Avatar from whatever its slot holds (zeros, or your own Avatar after a
   `look=copy`), and the Avatar package-name builders index their path tables
   without upper bounds (for example 0xAE8FB0 only checks for 0xFF; which
   record bytes reach which builder is not traced yet). The receiver
   therefore normalizes AVATAR to SNAKE with face 0 and face equipment 0
   (`partner::NormalizeLook`, `avatar_as_snake=1`), the safe choice.

The engine side does keep Avatar state per instance: the parts request reads
each instance's own record (0x9B27D4), `DoesNeedCreateAvatar` 0x9B1FE0 maps
the local index to slot 0 and other indices to slots 1.., and
`UpdateAvatarInfo` 0x9B7E90 rebuilds a slot when its record changes
(+0x1180/+0x1190 flags). Whether that rebuild actually runs for instance 1
outside FOB (memory budget, Avatar controller setup) is not known.

**Fix proposal (not implemented).** Send the record and write it the way the
FOB synchronizer does:

1. Wire: a new link event frame "avatar look" (magic `A`, version 1): the
   look revision it belongs to plus the 0x4D record bytes. Sent with the look
   frame (same gate: the sender's own parts loaded) only while the local type
   is AVATAR, and on change plus the look heartbeat. Both players run the same
   build (the link checks it), so no negotiation is needed; a missing, late or
   invalid record keeps today's Snake.
2. Sender: read the 0x4D bytes at `[Q+0xE8] + local * 0x4F` in
   `pp::ReadLooks` (the same Q and local index it reads today).
3. Receiver: when the look is AVATAR and a record with the same revision has
   arrived and passes validation, write it to `[Q+0xE8] + 1 * 0x4F` with
   +0x4D = 1 (what 0x9C9E70 and 0x12CC9C4 do), then send SET_PARTS with type 3
   and the look's face instead of Snake; log `avatar_as_snake=0`. Otherwise
   keep today's Snake normalization. The record must be written before the
   SET_PARTS reply that sets the dirty bit, inside the same tick, and cleared
   back (or the slot restored to Snake) at partner teardown.
4. Validation before writing: each of the 17 selection bytes within the
   Avatar editor's ranges and the 60 face-shape bytes within their slider
   range. These ranges are not pinned yet; they have to come from the
   package-name builders (0xAE8FB0, 0xAE9820, 0xAE6AC0, 0xAE7DA0, 0xAE8260)
   and the editor's limits before the receiver may trust a record.
5. Tests: codec round-trip and strict decode, validation tables, the
   writer on an in-memory Q, the NormalizeLook pass-through only with a
   validated record, and a hidden-worker scenario with an Avatar sender.

**Why it is not done here.** It is a new wire frame, a new native write into
game memory with an unpinned validation table, and an unknown engine
behaviour (point above); that is not small and not safe without a live run.

**The first live step costs no code.** In the offline lab, make the guest an
Avatar (`Prepare-LabInstance.ps1 -Offline -Character avatar`, in a save that
has an Avatar) and give the **guest's** `lab.ini` `partner_look=copy` (lab.json
partner `look`). `CopyPartsInfoFromLocalPlayer` then puts the guest's own
record (+0x4D = 1) and type 3 into the guest's instance 1, so the guest's
screen shows a partner that should look like the guest's own Avatar. If it
does, the engine builds a remote Avatar outside FOB and the proposal above
only needs the transport and the validation. If the partner shows as Snake,
bald, or with a broken head, the Avatar build is gated outside FOB and the fix
needs more native work first. Capture `partner_look=loaded` or
`partner_look=timeout` from the guest log with the frame.

## Logs

`mgs-coop\script-observer.log` (co-op modes only). Values contain no spaces;
`; ` separates fields.

| Line | Meaning |
|---|---|
| `character_config=quiet; fallback=dd_male; coop_ini=1; reset_bypass_needed=1; applied_in=TppMain.OnAllocate` | Settings read at startup. |
| `character=<result>; mission=...; requested=...; applied=...; type=...; reason=...; bypass=0/1; real=...; start=<new\|continue\|unknown>; always=<0\|1>` | Decision for this mission. `result` is `skip`, `keep` or `apply`. `start` is the `OnAllocate` path (`continue` = checkpoint/title Continue). `always=1`: the continue flag was set for this allocation. |
| `character=allocated; mission=...; vars=6; parts=27; camo=116; temp=1; start=continue; late=0` | End of `OnAllocate` (every apply decision, including a kept sortie choice such as `reason=override_sortie_kept`, lab or not; never for `skip`/`keep`): what `vars` hold before the player is created. `temp` is `gvars.ply_isUsingTempPlayerType`. `late=1; before=<type>` means the safety net applied the type (see above). |
| `character=story; resolved=<quiet\|ocelot>; reason=<on_team\|departed\|dead\|not_recruited\|not_deployable\|unreadable>; mission=...; flags=<o\|-><s\|-><l\|-><d\|-> or na` | `character=story` only, before the decision line, whenever a character is applied. See [Story: Quiet or Ocelot](#story-quiet-or-ocelot). |
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
| `character=story`: parse (never a fallback), query receipt, bypass allowed | Unit tests (`lab_config_tests`, `character_receipt_tests`) |
| `story` resolution: on the team / not recruited / in the cell / cell visit cleared / departed (both phases) / dead / hospitalized / unreadable; Quiet conflicts, buddy Quiet and refused bypass take the fallback; story gates, title mode and mission classes resolve nothing; continue; a later allocation follows the save | Lua fixture tests (`character_lua_check`, also with the real `TppPlayer`/`TppMain` in `character_game_lua_check`) |
| `story` against the game's own scripts: the real `TppStory` functions move the save through Quiet's story (recruit, cell, cell visit, A Quiet Exit opening, `RequestLoseQuiet`, `RequestReunionQuiet`) with the real `TppMission.SetSortieBuddy` after the chunk, and all 64 combinations of obtained / deployable / lost / dead / hospitalized / visit agree with the game's `UnlockBuddyQuiet` test | `character_story_lua_check` (needs the extracted `TppStory.lua` and `TppMission.lua`, below) |
| `story` in a live game | **Not verified** |
| Avatar shown as Snake to the partner: cause | Static (this section's addresses, read from the installed exe); not live |

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

### Live check for `character=story`

Use saves whose Quiet state you know (the lab seed, a copy of a real save; never
the real save itself). For each, the guest's `script-observer.log` must show
the `character=story` line before the decision line, and the screen must match:

1. A save **with Quiet deployable** (the Buddy menu offers her): 30050 or a
   free-roam mission gives `resolved=quiet; reason=on_team; flags=os--` and
   Quiet on screen (and a Quiet partner on the host's screen); also through a
   title Continue (`start=continue; always=1`).
2. A save **before Cloaked in Silence** (or with Quiet killed there):
   `resolved=ocelot; reason=not_recruited`, Ocelot on screen.
3. A save **after A Quiet Exit**: `resolved=ocelot; reason=departed; flags=--l-`.
4. If available, a save **between Cloaked in Silence and the cell visit**:
   `reason=not_deployable; flags=o---`; after the visit, the next mission load
   `reason=on_team`.
5. **Buddy Quiet deployed** in sortie preparation with a Quiet save:
   `resolved=quiet`, then `applied=dd_male; reason=buddy_quiet`.
6. `flags=na` (`reason=unreadable`) anywhere means the buddy functions could not
   be read at that moment; report the mission and how it was reached.

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

`character_story_lua_check` uses `TppMission.lua` (above, or
`startup-script-inspection-20260909/package-02.lua` /
`respawn-scripts-20260910/package-00.lua`, same SHA-256) and `TppStory.lua`:

| Script | SHA-256 | Saved as |
|---|---|---|
| `/Assets/tpp/script/lib/TppStory.lua` | `ac86be1ad896474229c9f7efcf4ffc6b597b760a503fc1882a6fc56b5e8b5728` | `character-story-lua-20261023/package-00.lua` |
| `/Assets/tpp/script/lib/TppDefine.lua` | `e00df50f2e44abb2b4190fa9a68cae3daf61a45cd35a74f76f5c602469de70a5` | `.../package-01.lua` (reference only) |
| `/Assets/tpp/script/lib/TppQuest.lua` | `6d3e08b1817b79d1022cc471616a85f9e653fc0286ad54ef5bf5f933e85c9e34` | `.../package-02.lua` (reference only) |
| `/Assets/tpp/script/lib/TppDemo.lua` | `7ba80b0576776863c7c18c471aeafffdaf7deb6b20081a5cf9b0f50f315e3b7e` | `.../package-03.lua` (reference only) |

```
MSYS_NO_PATHCONV=1 build/archive_probe.exe -out build/character-story-lua-20261023 -paths "/Assets/tpp/script/lib/TppStory.lua,/Assets/tpp/script/lib/TppDefine.lua,/Assets/tpp/script/lib/TppQuest.lua,/Assets/tpp/script/lib/TppDemo.lua"
```

`archive_probe.exe` is built from `tools/archive_probe` (see its README).
Extracted game scripts stay in `build/` and are never committed or shipped.
