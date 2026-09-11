# Player package selection experiment

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
