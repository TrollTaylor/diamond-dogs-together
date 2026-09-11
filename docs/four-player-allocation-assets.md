# Experimental four-player allocation files

2026-09-09. Two allocation definitions were staged and compiled into paired FPK/FPKD containers. **This initial standalone stage was not installed; four-player runtime allocation has not been demonstrated.**

The subsequent [runtime asset bundle](player-runtime-bundle.md) combines these allocations with supplemental parts/motion definitions and local animation data. That bundle is now installed and mounted. Loader 0.8.0 selected its controller package for the Mother Base experiment and the scene loaded. Actual four-player allocation remains unverified. [Current selection trial](player-package-selection.md).

## What the installed game contains

The read-only [archive inspector](../tools/archive_probe/README.md) found the exact `two_players_game_obj.fpk` and `.fpkd` paths in `master/chunk0.dat`. The FPK contains only a reference to `resident00.fpk`; its companion FPKD contains one FOX2 with a `GameObject` of type `TppPlayer2`. Both `totalCount` and `realizedCount` are **2**. [Extracted-package inventory](evidence/four-player-assets-2026-09-09/source-inventory.json).

This resolves a concrete missing layer in the earlier plan: increasing Dynamite's controller parts arrays alone would still select a game-object package configured for two instances. Other archives/mounted overrides and actual runtime package selection remain unverified.

## Staged changes

The generator requires the inspected package hash, decoded XML hash and pinned Dynamite controller hash before writing a new experiment beneath `build/`. It changes seven properties:

- Player GameObject total and realized counts: 2 to 4.
- Controller parts block count: 2 to 4.
- Both controller parts-type arrays: two to four `PLTypeNormal` entries.
- Controller package path: a new project-specific four-player package path.
- Instance block budget: 1,126,400 to 2,252,800 bytes. This proportional increase is an **unverified memory-budget hypothesis**, not an engine requirement established by measurement.

The successful stage is `build/four-player-assets-20260909b`; binaries are under its `compiled/` directory. It contains `mgs_coop_four_players_game_obj.{fpk,fpkd}` and `mgs_coop_four_player_controller.{fpk,fpkd}`. The controller package is only an allocation definition; it does not include Dynamite's supplemental motion/parts loading or mission modifications. It is not a complete playable mod bundle.

Derived controller source is attributed to Dynamite commit `c90f252f8200765f66c005d6bf4357fda0c0d98e`. Its full GPLv3/Commons Clause license accompanies the local stage. The other definition is generated from the user's installed game. Extracted and generated game assets stay in the local build directory; the evidence folder contains metadata and verification results, not copies of the game's assets.

## Verification and reproduction

The pinned local datfpk converter compiled both FOX2 files. Re-reading them preserved every parsed XML node, attribute and value: 65 nodes for the game object and 58 for the controller. Each compiled FOX2 was packed into FPKD, extracted, and matched byte-for-byte. The paired FPK reference lists matched the intended definitions. [Verification and hashes](evidence/four-player-assets-2026-09-09/verification.json), [change manifest](evidence/four-player-assets-2026-09-09/stage.json).

```powershell
# Use a fresh output directory for each experiment.
python tools/stage_four_player_assets.py --extracted build/player-package-inspection-20260909 --output build/four-player-assets-new
python tools/compile_player_asset_stage.py --stage build/four-player-assets-new --converter build/datfpk.exe
```

The first packaging attempt exposed a datfpk CLI behavior: v0.3.0 ignores a positional output directory when extracting FPK(D). The verifier correctly failed on the missing expected output. The wrapper now uses and checks the converter's default sibling directory. The failed stage remains separate; only the successful second stage has a complete verification report.

Reversible archive mounting and an initial mission package selection are now observed in the subsequent runtime-bundle experiment. Next measure actual allocation and player/parts/motion limits, then prove independently controlled actors. Session admission, three remote clients, save isolation, hospital support and the full campaign remain outstanding.
