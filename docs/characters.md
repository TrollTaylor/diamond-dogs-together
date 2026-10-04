# Characters in co-op

Each player picks the character they play on their own PC. The choice is made
when a mission loads, through the game's own "temporary player type" (the
same mechanism the story uses to force Snake in some missions). Your save
keeps your normal character: the next mission that does not apply an override
restores it, as the game does after those story missions.

Status: implemented and unit/fixture tested; **not yet verified in a live
game**. See [What has been verified](#what-has-been-verified).

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
the fallback**. The guest's `coop.ini` must say so (the lab/installer scripts
are expected to write it; until they do, create it as shown below). Player 1
can be Snake or any character above.

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
- The setting is only used in the co-op script modes (`host-v3` / `client-v1`
  in `mgs-coop\script-observe.enabled`). In any other mode, or without
  `coop.ini`, the game behaves exactly as before.

## Where the choice applies

| Situation | What happens |
|---|---|
| Story missions (10xxx/11xxx) and free roam (30xxx) | Your character is applied when the mission loads. |
| ACC / helicopter (40xxx), FOB (50xxx), MGO, title | Not changed. The ACC shows your normal character. |
| Missions whose script forces the player: 10010 (prologue), 10280 (Truth), 10030 (Diamond Dogs), 10240 (Shining Lights), 10151 (Sahelanthropus), 11043 / 11044 (Subsistence) | The story's own choice wins. Any other mission that sets a temporary type in its preparation also wins. |
| Quiet chosen in 10050 / 11050 (Cloaked in Silence) or 10260 (A Quiet Exit) | The fallback is used. |
| Quiet chosen while buddy Quiet is deployed | The fallback is used. |
| The Ocelot/Quiet bypass cannot be enabled (unexpected game version, mode) | The fallback is used. |
| Snake-only cutscenes | The game switches to Snake for the cutscene and back afterwards, as it does for DD soldiers. |
| Continue from checkpoint | Keeps the character saved at that mission's start. |

## How it works (short)

- `lua/coop/character.lua` runs inside the game's mission allocation, after
  the mission's own preparation and before the game applies the player type.
  It asks the mod for `character=`/`character_fallback=`, decides, and calls
  `TppPlayer.RegisterTemporaryPlayerType{...}`.
- Outside FOB, the game turns Ocelot and Quiet back into Snake in two places.
  The script skips the Lua revert once, at the end of that same allocation.
  The native revert (inside `UpdatePartsStatus`) is bypassed by a two-byte
  patch that is switched on only for missions where Ocelot/Quiet is applied,
  switched off for every other mission, and refused unless the game code
  matches byte for byte.
- In each mission the mod logs one line describing what it did, for example
  `character=apply; mission=10036; requested=quiet; applied=quiet; type=6; reason=override; bypass=1; real=0`.

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
- On Mother Base you may meet a second Ocelot or Quiet (the base NPCs).
- Buddy Quiet is detected from the buddy you deployed (`vars.buddyType`) at
  mission load. This uses the same check as the game's own scripts, but its
  timing has not been tested.
- The in-game readback (`Player.GetPlayerType`) reports Ocelot and Quiet as
  Snake (0). The mod logs your own type separately (`vars=6`).

## Logs

`mgs-coop\script-observer.log` (co-op modes only):

| Line | Meaning |
|---|---|
| `character_config=quiet; fallback=dd_male; coop_ini=1; reset_bypass_needed=1; ...` | Settings read at startup. |
| `character=<result>; mission=...; requested=...; applied=...; type=...; reason=...; bypass=0/1; real=...` | Decision for this mission. `result` is `skip`, `keep` or `apply`. |
| `character=ocelot_reset_skipped; mission=...` | The Lua Ocelot/Quiet revert was skipped once. |
| `character=face; mission=...; face=...` | A Diamond Dogs face was set. |
| `player_type_patch=active; ...` / `player_type_patch=inactive; ...` | The native bypass changed state. |
| `player_type_patch=refused; reason=...` | The bypass was not enabled. The fallback is used. |
| `character_readback=<tag>; local=0; vars=6; i0=0; i1=1; ...` | Lab readback of both players' types (only with `lab.ini` or a configured character). |

## What has been verified

| Item | Level |
|---|---|
| Native reset branch: bytes, complete function hash, jump target, uniqueness, FOB flag source | Static analysis of the installed 1.0.15.4 exe (`tools/generate_player_type_contract.py`, `docs/evidence/player-type-contract-2026-10-03.json`) |
| Bypass enable/disable/refusal logic, including on an in-memory copy of the real function | Unit tests (`player_type_patch_tests`) |
| Receipts, coop.ini query, log lines | Unit tests (`character_receipt_tests`) |
| Decision table, fallbacks, story-forced missions, faces, readback | Lua fixture tests (`character_lua_check`) |
| The real game `TppPlayer.lua` functions with this script; the real `TppMain.OnAllocate` with the splice, run against stubs | Lua fixture tests (`character_game_lua_check`, needs the extracted scripts) |
| Character visible and playable in a mission, local or remote | **Not verified (needs a live game)** |
