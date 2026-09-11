# Title transition investigation

2026-09-09. Loader 0.8.0 with `players-v1` reached Mother Base and exposed four reserved indexed player-storage entries. Returning to title through the pause menu then remained on a black screen for more than eight minutes. The raw mission code changed to 40050 and the player-system implementation pointer cleared. Neither observation proves completed teardown or successful title rendering.

The process remained responsive and accepted a normal Alt+F4 window-close request. Process absence was verified before a comparison launch. All seven save/config hashes matched the pre-selection baseline; matching copies and the 0.8.0 loader, marker and logs were preserved. [Close and backup record](evidence/player-return-title-close-2026-09-09.json).

A noninvasive debugger snapshot was detached after capture. The main thread was sampled in SleepEx through game RVAs 0x80AF9 and 0x1A46DBB. Subsequent static inspection shows timing calculations and a frame callback around these addresses. The earlier suggestion that this demonstrated a task-system deadlock was unsupported. The cause of the incomplete transition is unresolved. [Stack snapshot](evidence/player-return-title-stacks-2026-09-09.log), [world state](evidence/player-return-title-stall-world-2026-09-09.jsonl), [cleared player root](evidence/player-return-title-stall-storage-2026-09-09.json).

## Comparison experiment

PID 46964 launched at 20:58:42 UTC using the same installed 0.8.0 DLL and archive, with the marker changed to `archive-v1`. This leaves the custom archive mount/bootstrap enabled but disables the TppPackList replacement and extra-player allocation package selection. The title prompt and main menu responded, and Continue selected the existing save. A loading-stage read at 21:01:32 UTC found mission 30050, player index limit one, one storage entry and executor bitset 01. This contrasts with four entries/0F in the earlier package-enabled experiment; it is not a controlled remote-actor test. [Single-slot observation](evidence/title-control-player-storage-2026-09-09.json).

Mother Base was visually verified on foot with Snake and D-Dog. Return to Title was confirmed at 21:03:34.544 UTC; the main menu was visually verified by 21:04:08.420 UTC, within 34 seconds. That comparison ended at the main menu and later quit normally. All seven save/config hashes still match the original baseline. [Comparison result](evidence/title-control-result-2026-09-09.json), [retained observer log](evidence/title-control-observer-0.8.0-2026-09-09.log).

This single comparison makes the player-package/selection path the leading suspect. It does not distinguish the extra allocation count, nested controller dependencies, unload lifetime or another difference in the two runs. Next compare the package's load/unload lifecycle with the pinned upstream controller and add bounded transition receipts before enabling host creation. A coordinate click opened the checkpoint dialog during menu navigation; it was cancelled on No, and only the subsequent keyboard-selected Return to Title was confirmed.

## Independent selection-state fix

The 0.9.0 build clears `playerPackageSelected` and `playerPackageMission` before every original mission-list request. A request for another mission, a callback error or an unavailable archive therefore cannot leave a previous successful selection as current. No engine resources are released by this bookkeeping change, and it is not claimed to fix the observed black screen.

All 14 native suites pass. Exact-source checks pass 14 assertions and 400 concurrent selections. The actual-module Lua fixture passes 79 checks, including selection clearing on transition, callback failure and missing archive. [Build](evidence/title-transition-fix-build-2026-09-09.log), [focused checks](evidence/title-selection-reset-tests-2026-09-09.log). This build was installed for the trace trial below; native host creation has not run.

## Installed 0.9.0 trace and native wait capture

PID 32080 launched at 21:10:42 UTC with the selection reset and original bounded TppMain lifecycle tracing. The wrappers preserve arguments, nil/multiple returns, coroutine yields and original errors. They add no game API calls; a bounded loadstring receipt records callback entry/return. The exact-source selector passed 15 checks and 400 concurrent selections; the Lua fixture passed 213 checks. All 14 native suites passed.

Continue loaded Mother Base. A new read found four reserved indexed storage entries, then on-foot Snake and D-Dog were visually verified. Return to Title was confirmed at 21:17:21.035 UTC. The trace recorded EnableBlackLoading and OnTerminate entering and returning, but no subsequent OnAllocate. The player-system implementation cleared. Current global mission code is already 40050 during shutdown; it does not identify the departing callback's scene. The screen remained black through 21:30:01 UTC. All seven save/config hashes matched the pretrace baseline. [Trace](evidence/mission-trace-live-0.9.0-2026-09-09.log), [result](evidence/mission-trace-result-2026-09-09.json), [storage](evidence/mission-trace-player-storage-2026-09-09.json).

An independent executable search found block dispatch at RVA 0x6e4e0, called by the task wrapper at 0x6d660. Its current-build state jump table and counter predicates pass 27 instruction checks plus three state-target checks. Whole dispatch SHA-256 is 05ea4e586413f543de990f5803ef203dd839487a0024c822f6b27f7e5e371b9b. The earlier Dynamite addresses fall inside different functions in this build. [Reproducible analysis](../tools/analyze_block_lifecycle.py), [static report](evidence/block-lifecycle-analysis-2026-09-09.json).

The first bounded debugger observation at state-12/18 comparisons had no hit and detached at its deadline. A broader natural-entry capture then found block 0x1a3293f58 in state 11. A subsequent capture at its state-11 comparison (0x6ed89) confirmed the unsigned memory expression c60 - c18 + c40 + c10 equals 13,419,280, above the 12,641,280 threshold at block+0x14c: **778,000 bytes prevent that state from advancing**. This is an earlier threshold than the block+0x148 predicate in the first experiment. Every debugger detached; no engine calls or counter writes occurred. [Entry](evidence/block-wait-entry-2026-09-09.json), [comparison](evidence/block-wait-state11-2026-09-09.json).

This identifies a live wait condition, not the allocation owner or complete root cause. Native host creation was absent throughout. Dynamite's documented networking-allocation problem prompted inspection but is not established as this failure. The new host adapter now refuses execution until allocation lifetime is verified. [Host preflight](host-session-trial.md).

## Count-only comparison: stall reproduced

The next 0.9.1 experiment retains four player instances and changes only TppPlayer2AdditionalPartsBlockData.count from the hypothesized three back to the pinned upstream value one. Internal paths and all other definitions remain unchanged; the x3 filename is deliberately retained for this comparison. All four FOX2 definitions and the five-entry archive pass round trips. Four of five archive entries and three of four FOX2 definitions are byte-identical to the prior bundle. [Control bundle](evidence/player-runtime-parts1-control-2026-09-09.json).

Archive SHA-256: 28308ddb53dacb4e76a3980fdc73023f95ad646c15e0ed3dabf4229cf9cd6742; 2,420,736 bytes. The loader's exact archive gate was updated for this single control profile. This is not yet a verified fix or playable four-player support.


PID 33280 loaded Mother Base with four reserved entries. Return to Title was confirmed at 21:38:37.016 UTC and remained on black loading through 21:40:01.990 UTC. OnTerminate returned, no subsequent OnAllocate appeared, and the player-system implementation cleared. A bounded natural state-11 comparison captured the same total 13,419,280 and threshold 12,641,280: exactly 778,000 bytes excess. The additional-parts count change is therefore insufficient. The debugger detached automatically; normal Alt+F4 was requested at 21:40:10.095 UTC and process absence was verified. All seven save/config hashes matched. [Result](evidence/parts1-control-result-2026-09-09.json), [capture](evidence/parts1-control-block-wait-2026-09-09.json), [trace](evidence/parts1-control-live-2026-09-09.log).

## Two-slot allocation control

Loader 0.9.2 and archive c6603a58d5a6037cacb4e98bd6ece8dd9e50fc0a325e4aec400f28bdcc26be8f restore total/realized count, parts block count, both parts arrays and instance memory budget to the original two-slot values. This is a diagnostic profile, not a change to the project goal. All internal paths, the script probe, motion data and three supplemental definitions remain unchanged. The original staging tool verifies every intended XML difference, two FOX2 round trips, embedded definitions and five archive entries. All 14 native suites pass. The control was installed and tested; it reproduced the stall, as recorded below. [Asset report](evidence/two-player-control-assets-2026-09-09.json).


PID 52404 started at 21:43:43.527 UTC. Mother Base loaded and the bounded player-system reader observed exactly two reserved indexed entries. Return to Title was confirmed at 21:47:37.068 UTC and remained black through 21:49:02.007 UTC. The native state-11 comparison at 21:48:19.101 UTC was identical to both four-slot runs: total 13,419,280, baseline 12,641,280, excess 778,000. OnTerminate returned and no subsequent OnAllocate appeared. The debugger detached; normal Alt+F4 was requested at 21:49:13.318 UTC, process absence was verified, and all seven save/config hashes still matched. [Result](evidence/two-player-control-result-2026-09-09.json), [native capture](evidence/two-player-control-block-wait-2026-09-09.json), [storage](evidence/two-player-control-player-storage-2026-09-09.json).

These comparisons show that neither restoring additional-parts count one nor reverting all four-slot allocation scaling is sufficient to remove the wait. They do not prove the retained allocation owner.

## Duplicate controller definition found in the integration path

Pinned Dynamite removes single-player controller definitions from supported mission FPKDs before adding its co-op essentials package. See `work/upstream-dynamite/moddata/moddata.go` (for example lines 174-178) and the actual deletion loop in `installer/installer.go:341-358`. Its Mother Base 30050 list is not in its co-op mission support path; do not infer a ready-made Mother Base fix.

Our package-selection fragment only appends a controller package after the original list. A new read-only extraction found:

- `f30050.fpkd`, SHA-256 `3b38bdb7cc8d1de21d76fea8064ed097d9164038af548a5d9dc090225fccdb49`, contains `single_player2_instance.fox2`, SHA-256 `dc38ce424736be69765660d077c05cd7329e00bab50bdb58e1817298f5ab4f1d`.
- `f30051.fpkd`, SHA-256 `d209e49e21ae16efc31a07872ec2f6c0a0c26628742aad650701413eec004afd`, also contains a single-player controller, with different content.
- `mtbs.fpkd`, SHA-256 `ae9ba99d61d87346b8900c54651693ab77c9fd96b1e2ae378513f5b174d687ed`, already contains an additional-parts-count-one definition.

Both mission FOX2 files declare `TppPlayer2BlockControllerData`; the custom package adds another definition of this type. This establishes overlapping definitions in the selected asset path, not a live proof that these own the retained 778,000 bytes. [Inventory](evidence/mtbs-controller-package-inventory-2026-09-09.json). Extracted data remains local under `build/mtbs-controller-ownership-20260909`.

Next prepare a renamed local Mother Base mission package with only the original controller definition removed, replace its exact mission-list entry for the bounded experiment, preserve the FPK partner and all unrelated entries byte-for-byte, and repeat the two-slot lifecycle comparison. Keep original installed archives intact. Consider the duplicate supplemental parts definition separately so that one experiment does not conflate both changes. Equipment ghost-block setup differs too, but is not yet a demonstrated cause and should not be changed in the same control.


## Controller replacement control installed (0.9.3)

A renamed normal Mother Base package removes only single_player2_instance.fox2. All 44 retained definitions pass embedded-byte checks; the paired FPK is an exact copy, including its resources and references. All five existing two-slot archive entries remain byte-identical. The seven-entry archive passes extraction validation. The exact-source selection fragment replaces one unambiguous f30050.fpk entry; it declines demo/hangar and absent/ambiguous normal lists. All 14 native suites, 14 exact-source assertions, 400 concurrent selections and 98 actual-module Lua checks pass. The first Lua fixture run assumed that coloring packages preceded the callback's entries; that fixture assumption was corrected to match the actual original module. The game fragment was unchanged by that correction. [Assets](evidence/controller-replacement-assets-2026-09-09.json), [checks](evidence/controller-replacement-source-checks-2026-09-09.log).

The installed archive SHA-256 is 26ce7c2168d655c35be39e4f17a295206aa0522de8782cd57a36c4feea382f54, 3,720,192 bytes. Two-slot allocation and additional-parts definitions remain unchanged. Prior owned files and all seven matching saves were backed up. PID 59748 completed two Mother Base load/return-to-title cycles. Title was visible within 26 seconds and 16 seconds of confirmation respectively, and Continue reloaded the scene between them. It quit normally at 22:06:34 UTC. All seven save/config hashes matched. No debugger or native host call was used. See [result](evidence/controller-replacement-result-2026-09-09.json) and [live trace](evidence/controller-replacement-live-2026-09-09.log). This fixes the observed two-slot scene-transition failure; it does not identify the exact retained allocation or prove multiplayer lifecycle safety.


## Four-slot restoration (0.9.4)

The same packaging tool now accepts exactly pinned two- or four-slot source archives. The four-slot/count-one source is preserved byte-for-byte in all five runtime entries; the two renamed Mother Base files use the same controller removal. The seven-entry archive is 3,720,192 bytes, SHA-256 `74bfcd3656356ee4b08eebcb9912d4806ba0b9f7f948d0c86e6e412d6e930666`. All round-trip checks and all 14 native suites pass. Loader 0.9.4 changes only version strings and the archive fingerprint. Native host creation remains disabled. Installed after normal quit and matching backups; PID 94384 loaded Mother Base with four reserved storage entries and returned successfully. The title scene was visible within 13 seconds, the full menu at 22:10:37 UTC, and arrow-key input responded afterward. All seven save/config hashes match; shipped executable and Steam DLL hashes remain unchanged. No debugger or native host call was used. [Live result](evidence/four-controller-replacement-result-2026-09-09.json). [Assets](evidence/four-controller-replacement-assets-2026-09-09.json), [installation](evidence/loader-0.9.4-four-controller-replacement-install-2026-09-09.json).
