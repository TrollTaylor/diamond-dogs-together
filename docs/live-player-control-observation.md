# Player-state control observation

2026-09-09. Development observation of the exact installed executable SHA-256 `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`, PID `42824`. This is ordinary single-player UI control plus the project's external read-only sampler. It is not remote actor control or a campaign co-op test.

## Save evidence before control

At 15:28:14 UTC, before Continue in this check, `TPP_GAME_DATA0` still matched the 14:51 backup. `TPP_GAME_DATA1` differed; its last-write timestamp was 15:04:45.117271 UTC, during the preceding menu/exit session and before the current 15:06 launch. Its semantic changes have not been decoded. The later menu still displayed 69% overall completion.

All seven current local save/config files were copied into `build/backups/save-before-control-20260909T152909Z`; source hashes before and after each copy matched the copied file. The earlier `save-before-observation-20260909T145101Z` backup remains intact. No original save was overwritten by the tools. These copies do not implement save isolation or prove restoration.

The two archived versions of `TPP_GAME_DATA1` have equal length, 199,916 bytes, and differ at 10,063 byte positions. This comparison establishes a change, not its gameplay meaning or cause. Ordinary game navigation can result in save writes even though the sampler makes no calls or writes. The earlier 15:03 hash check was accurate at that time; it is not a current unchanged-save claim.

Evidence: [pre-control check](evidence/save-check-before-control-2026-09-09.json), [archived version comparison](evidence/save-version-comparison-2026-09-09.json).

## Helicopter and iDroid

Keyboard Enter selected the visibly highlighted Continue item. The existing helicopter scene loaded. Tab initiated the iDroid view; the game's daily-bonus and completed PF-grade informational notices were displayed. No reward screen, online battle, or mission was selected in these steps.

At 15:31:05 UTC, three samples during this transition read mission code `40050`, position `[0.04177,1198.09912,-1.04203987]`, local index `0`, and raw status words `[1048614,16777248,0,0,131584]`. Bank selection alternated. Selected named bits remained `SQUAT=true`, `PARTS_ACTIVE=true`; other currently decoded posture/movement bits were false. Compared with the title sample, raw status bits changed while the coordinates did not. These observations are not an on-foot movement correlation.

Evidence: [helicopter/iDroid samples](evidence/player-status-helicopter-2026-09-09.jsonl), [title samples](evidence/player-status-title-2026-09-09.jsonl), [static field derivation](player-update-investigation.md).

## Mother Base arrival

The iDroid notices were dismissed with their visible OK actions. The Missions tab's Return to Mother Base option was selected using keyboard navigation, then the highlighted command-platform landing zone and ordinary drop-in confirmation. The loading screen resumed normally. Arrival automatically began a cutscene whose visible dialogue concerned nuclear disarmament; no FOB mission or online battle was selected. This observation does not establish why the game triggered that cutscene.

At 15:43:52 UTC, five 250 ms samples read mission `30050`, index `0`, position starting at `[-2.28130865,34.7998848,2.53391171]` with small drift, and words `[1048734,0,0,0,131072]`. `STAND`, `NORMAL_ACTION` and `PARTS_ACTIVE` were already true before any movement input in the scene. Thus this flag combination and minor coordinate changes are also insufficient to establish human control. The screenshot taken immediately after this sample showed the automatic cutscene.

Evidence: [arrival samples](evidence/player-status-mother-base-arrival-2026-09-09.jsonl), [static producer investigation](player-producer-investigation.md).

The 15:48:57 UTC [save check](evidence/save-check-mother-base-arrival-2026-09-09.json) found another ordinary-game write to `TPP_GAME_DATA1`, last-write time 15:43:06.793714 UTC and SHA-256 `DD22827FAABEE83F3962A996FC6A178DB98BBAE7674A6EE2B115B67122ECA833`. `TPP_GAME_DATA0` still matched its preserved original. No save was restored or overwritten by the tools; both earlier backup sets remain available.

After the cutscene and credits, the game showed a loading hint with Space / Resume Game. The attempted Space action was refused because Computer Use detected user input. No further game input was sent; the controls were left with the user. The hint mentioned FOB abilities, but this was not evidence of selecting a FOB mission.

A later read-only window view showed Snake seated in the helicopter and 69% overall completion. It did not establish who initiated the intervening transition. At 16:00:20 UTC, the extended sampler passed the full local-selector and vector-getter byte checks, then took five bounded samples. Every sample read mission `40050`, index `0`, raw status `[1048614,32,0,0,131072]`, and `player_reference_position_candidate=[0.04177,1198.09912,-1.04203987]`, equal to the existing named coordinate fields. The bank selector changed during this interval. This establishes readability and agreement in that stationary scene, not movement freshness, active actor identity, atomicity or a root/feet coordinate definition.

Evidence: [published vector samples](evidence/player-vector-helicopter-2026-09-09.jsonl), [named position consumer and getter derivation](player-vector-producer-investigation.md). The sampler performs no game calls or writes. On-foot movement correlation, active-actor readiness, game-object identity and current campaign sequence remain unverified.

## Approved restart for Steam startup observation

The first Return to Title action was rejected by automatic approval review after controls had been left with the user. The user then explicitly answered “Yes, restart for the test” to the request to return to title, quit normally and restart. The game returned through its ordinary confirmation and title menu, then Quit Game was selected. On continuation at 16:28 UTC, both process and window checks confirmed the prior game had exited. No forced termination was used.

While closed, all seven save/config files were copied into `build/backups/save-before-steam-init-20260909T162821Z`; each source-before/source-after/copy hash matched. `TPP_GAME_DATA1` now has last-write time 16:20:47.1709406 UTC and SHA-256 `CC4E1FA85CED331216B985303B3E5A5E3B7D5538240E0C25A0006BB50CF90BFC`. Its semantic changes are unknown. No source save was restored or overwritten by the tools. [New closed-game baseline](evidence/save-check-before-steam-init-2026-09-09.json).

The new game PID `76012` started at 16:28:34 UTC. Its natural Steam Init call returned true and the debugger detached without deadline recovery. Computer Use selected the new returned window and brought it forward. The autosave startup notice appeared; Enter dismissed it and the Konami startup sequence followed. Further title/menu verification is recorded with the separate continuation evidence, rather than changing the launch harness's own UI-unverified result.

The title prompt subsequently appeared; Enter reached the main menu with Continue selected and 69% completion. The 16:31:39 UTC check confirmed the game remained running, no CDB process was present, and all seven save/config hashes matched the prelaunch baseline. The game was left at that menu. This trial did not select Continue or exercise on-foot/controller input. [Continuation evidence](evidence/game-steam-init-continuation-2026-09-09.json).

## Original loader 0.4.0 scene check

Subsequent on-foot work is recorded in [live player motion](live-player-motion-observation.md). That later trial reached Mother Base without the earlier automatic cutscene, observed a dive and posture recovery, and left the game paused. Earlier statements below describe their individual historical trials.

After the next original loader passed all 13 suites, PID 76012 quit normally from the title menu, including its visible confirmation. The owned old loader was preserved before the new one was installed. MGSV then launched without a debugger at 16:41:22 UTC, PID 106392. Enter dismissed the startup autosave notice and opened the main menu; another observed Continue selection loaded the helicopter. No mission, online battle or reward action was selected. All seven files matched the preserved 16:28 save/config baseline at the recorded checkpoint. The new networking getter observer installed successfully, but no natural getter result was logged during this scene check. [Live result](evidence/steam-getter-live-2026-09-09.json), [getter investigation](steam-networking-getter-observation.md).
