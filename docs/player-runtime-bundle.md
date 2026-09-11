# Player allocation, equipment and motion bundle

Current loader 0.9.4 uses four reserved player slots and additional-parts count one. A renamed normal-Mother-Base package removes the overlapping original single-player controller; its remaining 44 definitions and FPK partner are preserved. The seven-entry runtime loaded Mother Base and returned to a responsive title menu. The two-slot comparison passed two cycles. This is scene-transition evidence, not four playable actors. [Investigation](title-transition-investigation.md), [four-slot result](evidence/four-controller-replacement-result-2026-09-09.json).

The following records describe the earlier count-three bundle and its original validation; installation statements below are historical.

2026-09-09. A standalone local test archive now contains the staged four-player allocation files and the missing equipment/motion definitions. **The five-entry archive is installed and mounted by loader 0.8.0; its custom script executed, its controller was selected for mission 30050 and Mother Base loaded. Four playable actors remain unverified.** [Successful archive trial](custom-archive-loading.md), [subsequent selection trial](player-package-selection.md).

## Resolved dependencies

Dynamite's pinned `createEssentials()` copies `mtbs_mission_fob.fpk` as the data half of its co-op package, while `generate.go` builds the definition half from four FOX2 files. The earlier two-file allocation stage omitted three of those definitions and the animation data. The new project-authored [generator](../tools/stage_player_runtime_bundle.py) builds the following experiment with source hashes and the full Dynamite license alongside it:

| Component | Bundle treatment | Runtime limit |
| --- | --- | --- |
| Player GameObject package | Retains the staged total/realized count of four | Allocation has not been measured |
| Player block controller | Retains the staged four parts blocks/arrays and doubled instance budget | Memory scaling remains a hypothesis |
| Additional parts definition | Changes count from one to three, for the three proposed guests | Per-guest interpretation is an unverified hypothesis |
| Online motion definition | Preserves the upstream definition | Four-player animation behavior is unverified |
| Location motion loader | Preserves paths and shared motion/VRAM budgets | Sufficiency for four actors is unverified |
| Online and pipe MTAR data | Copies the exact installed game's two-entry FOB FPK | No FOB mission script or FOX2 is included in that FPK |

The installed `player2_location_motion.fpk` and `player2_location_vram_motion.fpk` were also found and hashed; each contains 16 animation entries. Their presence is established, but live archive precedence and loading remain unresolved. The initial TppMissionList lookup used the wrong `/lib/` directory. A corrected `/list/TppMissionList.lua` lookup found its sole copy among the inspected archives in `master/0/00.dat`. [Source inventories](evidence/player-runtime-bundle-2026-09-09/source-inventory.json), [corrected mission-list inventory](evidence/player-runtime-bundle-2026-09-09/mission-list-inventory.json).

## Compiled output and validation

Original four-entry output: `build/four-player-runtime-20260909c/`. `mgs-coop-player-runtime.dat` contains exactly four entries: the game-object FPK/FPKD and the controller FPK/FPKD. The controller definition package now includes four FOX2 files; its FPK supplies the two unchanged MTAR files. The archive contains no modified game scripts.

Four FOX2 definitions passed semantic XML round trips (58, 36, 32 and 46 nodes), and all four embedded FOX2 byte hashes matched after FPKD extraction. Both MTAR hashes matched their installed source. All four final package files matched byte-for-byte after standalone QAR extraction. The QAR version 1/flags 3150304 profile comes from the converter's plain-archive fixture; this round trip does not establish that a particular live mounting call accepts it. [Complete hashes and checks](evidence/player-runtime-bundle-2026-09-09/bundle.json).

The first archive verification failed because the converter's dictionary expects extensionless stems and appends each stored extension itself. Full filenames produced doubled extensions in the extracted inventory. The generator now writes unique stems and checks the complete final path set and file bytes. The failed `...20260909b` output is separate from the successful `...20260909c` stage.

```powershell
python tools/stage_player_runtime_bundle.py --stage build/four-player-assets-20260909b --extracted build/player-load-inspection-20260909 --converter build/datfpk.exe --output build/four-player-runtime-new
```

The tool requires a new directory beneath project `build/`, pins the converter and inspected input files, and never writes to the game directory. Extraction outputs and converted assets are local research data, not a distributable release. [Source and adaptation notice](../third_party/dynamite/player-runtime-source.json).

The installed stage is `build/four-player-runtime-20260909d/`, containing the same four package files plus the project-authored `/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua`. All five exact paths and bytes passed extraction checks. Archive SHA-256: `711def02f258f6ca42a63ca2374fdf342a306adb1ba29fe31f7e6df94b1830e5`. [Five-entry report](evidence/player-runtime-bundle-2026-09-09/bundle-with-probe.json).

## Loading contract

`TppPackList.MakeMissionPackList(missionCode, callback)` clears its current list, invokes the supplied mission callback, adds coloring packages and returns the list. Its installed source hash is recorded in the source inventory. The mission package to append is **only** `/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpk`; the controller's `instanceSettings_instancePackagePath` selects the game-object package. The bootstrap's current `GetPlayerPackages()` is an unused inventory of both paths, not a mission loading list.

The creation ABI and startup lifecycle have now been independently decoded and naturally observed. The original adapter calls the verified creation function once with flags `0x21` and retains the handle for process lifetime. The custom script resolves and executes through both loading methods. Loader 0.8.0 appends the controller only for the initial mission 30050 experiment; the live selection receipt and scene loading are observed. This does not establish FPK processing, four-actor allocation or memory sufficiency. Next observe allocation and resource use, then establish independent control before authenticated peers and campaign transitions. [Mount evidence and failed-trial history](custom-archive-loading.md), [selection evidence](player-package-selection.md).
