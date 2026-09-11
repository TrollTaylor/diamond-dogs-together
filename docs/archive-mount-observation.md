# Natural archive mount observation

2026-09-09. The game's archive-creation entry and common result path were observed during a normal startup mount of `master/chunk0.dat`. The debugger detached successfully, and startup continued with the existing 0.6.0 Lua bootstrap. This page records that natural observation; subsequent custom archive trials are in [custom archive loading](custom-archive-loading.md). Co-op remains unplayable.

## Static boundary

The pinned current-build tpp-mod source identifies `MountPoint::CreateWithPackFile` at RVA `0x48E20`, `Destroy` at `0x49270` and `SetIoHandleCount` at `0x4A200`. The independently decoded creation range is `0x48E20..0x48F8C`, with SHA-256 `dc556b49319c37495c86d14dbb6eb93f22251aa4eaa4fb7a54deae4bd0fffea7`. It preserves four arguments: name, file path, a 64-bit tag and 32-bit flags. Its successful path stores the tag at returned handle offset `0x28`; another path returns null after finding an existing name.

The common result point is `0x48F6B`, after the result has been placed in RAX and before security-cookie checking and stack restoration. Five pushes and a `0x70` stack allocation place RSP `0x98` below entry. The captured caller at `0x3BAE2` resides in the complete declared function beginning `0x3BA70`. It preserves the same four arguments, adds flag bit `0x20`, conditionally handles another path form and invokes creation. Flag semantics beyond these operations remain unresolved.

The [static verifier](../tools/inspect_mount_points.py) reproduces 21 aligned instruction checks across five complete functions, including the captured caller. [Static evidence](evidence/archive-mount-static-2026-09-09.json). The source mod mounts custom packs after its filesystem initializer at `0x3A960`; that initializer's full range has been decoded, but no installed initialization hook or custom call was added in this step.

## Live capture

The preceding PID 105412 quit normally from the main menu. Seven save/config files were copied and hash-checked before launch. The bounded observer then launched **PID 22440 at 19:31:20 UTC** in the desktop Steam session. [Save baseline](evidence/save-before-mount-2026-09-09.json).

| Observation | Value |
| --- | --- |
| Entry/return thread | `0xCD40` (52544) |
| Mount name | `fmanifest` |
| File path | `D:\Games\steamapps\common\MGS_TPP\master\chunk0.dat` |
| Tag / flags | `0` / `0x34` |
| Caller return address | `0x14003BAE7` |
| Entry/result RSP | `0x15F938` / `0x15F8A0` |
| Returned handle | `0x067A0490` |
| Raw handle offset `0x10` | `1` |
| Raw pointer at `0x18` | `0x05692CC0` |
| Stored tag at `0x28` | `0` |

The entry/result thread and stack frame matched. There was no abort or deadline detach, and CDB exited with code zero. The existing Lua bootstrap subsequently produced its normal replacement and execution receipt on the same thread. This demonstrates a natural successful archive mount and continued startup, not custom archive resolution or a general callable lifecycle guarantee. [Original debugger log](evidence/archive-mount-live-2026-09-09/debugger.log), [observer result](evidence/archive-mount-live-2026-09-09/result.json).

The observer reuses the tested startup bootstrap and deadline/detach harness. It places one temporary software breakpoint at the PE entry point, then observes creation and its result with hardware breakpoints. It does not call the mount function itself, alter arguments, rewrite game archives or retain a handle. The mount-specific script does not have a separate synthetic ABI fixture; its entry/result checks were exercised by this actual game capture.

```powershell
# Requires MGSV closed; the tool refuses to close or duplicate a live game.
./tools/observe_game_registration.ps1 -Observation ArchiveMount -DeadlineSeconds 20
```

Computer Use verified the title and main menu at 69%, including a fresh observation after the user resumed work. All seven save/config hashes matched the prelaunch baseline. [Continuation](evidence/archive-mount-live-2026-09-09/continuation.json), [save comparison](evidence/archive-mount-live-2026-09-09/saves.json), [binary comparison](evidence/archive-mount-live-2026-09-09/binaries.json). PID 22440 subsequently quit normally for the custom archive test.

The next integration must mount the fingerprinted standalone archive with a unique name, verify resolution of its exact contents, and retain ownership across mission load/unload. Mission selection must load only the controller package, which selects the four-player object package internally. A successful archive handle does not prove actor allocation, equipment/motion sufficiency, independent control or multiplayer synchronization.
