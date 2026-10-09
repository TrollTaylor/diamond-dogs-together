# Player package selection

## Mother Base layouts

The prepared 0.12.1 selector restores the normal Mother Base list on real saves. Commit `7ec1e39` added a variant check that also matched `f30050_ly003.fpk`, leaving the original single-player mission pack in place. Diagnostic commit `e868757` records the cause. In hidden run `39845163ea6740ec81c36ec1a61d7ce8` on October 9, both copies listed `f30050.fpk` and `f30050_ly003.fpk` at positions 5 and 6, then reported `MGS_COOP_PACK_REFUSED_V1 -- mission=30050; reason=foreign_or_variant; path=/Assets/tpp/pack/mission2/free/f30050/f30050_ly003.fpk`. Host and guest remained without a realized partner. This run demonstrates the failure; it did not run the layout fix.

The selector now accepts exactly one of `f30050_ly001.fpk`, `f30050_ly002.fpk` or `f30050_ly003.fpk` immediately after the normal `f30050.fpk`. It replaces only the mission pack and appends one matching co-op controller. The layout stays in its original position. A list with no layout retains the earlier fixture behavior. Unknown, duplicated, misplaced or cross-mission layouts refuse without changing the list. Demo stage `f30051.fpk`, battle-gear hangars and Afghanistan's `f30011.fpk` still refuse because they have separate player controllers.

The exact retail `TppPackList.lua` SHA-256 is `85120c16a6cb27d8a732763b023f45125edd70201bef182695856756ee4cb00e`. Its `AddFOBLayoutPack` builds `_ly%03d.fpk` from `vars.mbLayoutCode`. `TppMissionList.lua`, SHA-256 `3c250e19fc5ba0ccc14b192daef2860555e6cadf0f44039876fc3e8687f60e1d`, adds this immediately after the normal Mother Base mission pack. The layout exception therefore precedes the general variant check and requires this order.

Claude's existing read-only `tools/archive_probe` audit requested layout codes 000–999 as both FPK and FPKD from `master/0/{00,01}.dat` and `chunk0..4.dat`. It found only layouts 001–003, all in `chunk3.dat`. Retained output is under the October 9 scratchpad `fix/probe/ly-*/inventory.json` and `fix/probe/lyd-*/inventory.json`; `fix/census.py` and `fix/census.txt` record the class census. The continuation independently checked the six retained package hashes and their decoded XML. Each FPKD has 33 entries, including 27 FOX2 files, 155 game-object locators and 126 soldier locator parameters. None defines `GameObject`, `TppPlayer2*`, `PlayerController*` or a player locator. Each outer FPK has nine route/navigation/model entries and references `mtbs_common.fpk`. This supports keeping those original layouts beside the controller-replaced mission package; it does not establish live actor allocation or resolve archive precedence beyond the scanned inputs.

| Original package in `chunk3.dat` | SHA-256 |
|---|---|
| `f30050_ly001.fpk` | `b89a5b41d82de52f03eecb69c3c15448adc93eb5c2dae63b8d95a58d8b13ca62` |
| `f30050_ly001.fpkd` | `244b10ee26843ca77408c25d0d8a25659f900106ad0c4f1ad84e5844da968f34` |
| `f30050_ly002.fpk` | `783227635642bb9be245c6a849fb9dd6768a98159db216ac33a8f727b281e395` |
| `f30050_ly002.fpkd` | `97cd892c77703649f15484f2061d07115bc71a8334c978094f7ac67a58397184` |
| `f30050_ly003.fpk` | `e66623861da3af6ab92d520d390d95225cfcf7f1fd8e1a516bcae6b3164a8f62` |
| `f30050_ly003.fpkd` | `267a620690de9af7b2695b1c3b32e1d7049947dcf1bccd9449e4bc550abb9d9c` |

## Pack state and feedback

Verbose selection, refusal and list logs remain bounded: 16 selections, 24 refusals and six lists of at most 96 entries per loaded module. They are diagnostics, not mission state. An independent compact `MGS_COOP_PACK_STATE_V1` receipt reports `mission`, `result=clear|selected|refused` and `scene` on every request. Clear runs before the original callback, so a thrown rebuild, unsupported mission or unavailable archive cannot inherit an earlier refusal. Native code accepts only the exact bounded schema and consumes the newest result once at mission start. Exhausting diagnostic caps cannot stop a later refusal or successful recovery from changing state.

A refused partner-body mission publishes `partner_reason="no_body_pack"` and an in-game warning naming the scene. Demo/hangar/story warnings explain that the body returns on a normal supported reload. Unknown layouts and unexpected lists request Collect-Logs. These changes report missing body capacity; a receipt does not prove a package loaded or an actor was allocated.

## Validation and remaining runtime work

The standalone Lua 5.1 fixture passes **729 checks**, including **18 actual retail `GetMissionPackagePath` requests** through the patched pack module. It covers layouts 001–003, the real-save list, invalid layout shapes, unchanged demo/hangar/Volgin refusals, Africa with coloring packs, cleared selection after unsupported requests, callback errors, missing archive and state transitions after both diagnostic caps are exhausted. Native parser, status and HUD regressions are prepared for the coordinator's build; this continuation did not build or run MGSV.

Normal Africa (`30020`) has no alternate branch in the pinned mission-list builder: it selects the ordinary `f30020.fpk`, alongside common and coloring packs. The fixture confirms selection, but the October 9 diagnostic run only booted Mother Base. Fresh hidden Mother Base realization and host-driven travel to Africa remain pending on the integrated fix. No new two-PC or full-campaign support is established. The earlier diagnostic run closed both owned processes normally, restored each copy's own loader/configuration and reported unchanged real saves.

## Historical 0.8.0 experiment

The record below describes the original four-slot experiment and its contemporary evidence, not the current two-player selector.

Loader 0.8.0 adds a separate `players-v1\n` development marker. The marker enables the already verified archive mount and an exact-source patch of `/Assets/tpp/script/lib/TppPackList.lua`: 4,500 bytes, SHA-256 `85120c16a6cb27d8a732763b023f45125edd70201bef182695856756ee4cb00e`. Every request checks its name, size, suffix and full source hash, including requests after the immutable cache has been populated. The original script bytes remain intact before its final return.

The project-authored Lua fragment wraps `MakeMissionPackList`. It calls the original function first and preserves its returned list, callback behavior, package order and errors. Only the initial Mother Base allocation experiment (mission 30050) can append `/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpk`, once. The controller owns loading the player-object package; adding both to this list would be incorrect. Archive direct-read success is required. A maximum of 16 selection receipts per module instance prevents unbounded diagnostic logging. These receipts establish selection, not loading or allocated actors.

The mission restriction is an experimental rollout boundary. The goal remains all released campaign content, including the first hospital gameplay; no mission is declared supported by this change. No guest control, world replication or Steam traffic is enabled.

## Validation

All 14 existing native suites pass. A separate exact-source check passes 14 assertions and 400 concurrent selections. The Lua 5.1 fixture executes the actual original and patched game modules and passes 73 checks: matching other mission lists, preserved order and table identity, callback errors, missing archive/bootstrap, duplicate prevention, reload cleanup and bounded receipts. Its first run identified a missing `gvars` fixture stub; the fixture was corrected without changing the game fragment. [Native build](evidence/native-tests-player-selection-0.8.0-2026-09-09.log), [package checks](evidence/player-package-local-checks-2026-09-09.log).

## Live trial

The last verified 0.7.0 game quit normally. Its loader, marker and ownership record were backed up; all seven save/config files were copied with matching before/after/backup hashes. [Save backup](evidence/save-before-player-selection-2026-09-09.json), [0.8.0 installation](evidence/loader-0.8.0-install-2026-09-09.json).

PID 41584 started at 20:21:28 UTC without a debugger. The archive opened and its script executed. Entry 70 selected the exact 4,500-byte TppPackList source for replacement. [Startup log](evidence/player-selection-startup-0.8.0-2026-09-09.log). Mission selection and allocation are not yet verified by this startup record.

Continuing the saved game produced entry 142, `MGS_COOP_PACK_SELECTED_V1 -- mission=30050`. Computer Use then verified Snake and D-Dog on foot at Mother Base Command Platform. The read-only sampler reported mission 30050, local index zero, crouching and matching position sources at the sampled instant. Escape opened the pause menu normally. The game remains paused there. [Selection log](evidence/player-selection-live-0.8.0-2026-09-09.log), [Mother Base samples](evidence/player-selection-mother-base-2026-09-09.jsonl), [continuation](evidence/player-selection-continuation-2026-09-09.json).

All seven save/config hashes still match the pretrial backup. The original executable and Steam DLL also match their accepted hashes, and the installed loader/archive match the installation record. PERSONAL_DATA1 has a newer write timestamp but identical contents. [File checks](evidence/player-selection-files-2026-09-09.json). These backups are not save isolation.

This establishes package selection and successful scene loading, not that the custom FPK supplied four live allocations. The mount-time `actor_packages_selected=0` log precedes the later mission selection receipt. It is not a current allocation count.

## Allocation query investigation

The original [read-only mapper](../tools/analyze_gameobject_queries.py) resolves eight GameObject registrations in the exact installed executable and checks four query-path facts. `GetGameObjectIdByIndex` is registered to RVA 0xBFA110; it converts its arguments, calls 0xBF4090 and pushes a 16-bit result. That helper jumps to 0x7B6F4A0, whose transformed control flow has not been resolved. The type-existence query also leads to an unresolved helper. No query was called in the game. [Reproducible static evidence](evidence/gameobject-query-analysis-2026-09-09.json).

The earlier network-specific registration tracer rejected this registrar because it searches for TppNetworkUtil; the dedicated mapper checks the actual GameObject registration sequence instead. A linear decode of the indexed helper's transformed target was incomplete and is not used as allocation evidence.

Next observe the controller's actual allocation and resource use, then establish independent control. A nonzero player ID or a four-entry asset definition alone is insufficient to prove four independently controlled actors.

Subsequent read-only work found four indexed player-system storage entries and an enabled limit of four. Extra inspected headers remain zeroed; complete character initialization is not established. [Live storage observation and derivation](live-four-slot-storage.md).
