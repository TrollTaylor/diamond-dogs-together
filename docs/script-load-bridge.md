# Script-loading bridge

2026-09-09. The native bridge compiles and passes fixture tests. Loader **0.5.0** now installs it in opt-in pass-through mode, with live startup loads and successful parser returns recorded. **No scripts have been replaced in MGSV.** The earlier separate debugger observations remain below as boundary evidence.

## Implementation

`native/script_load_bridge.cpp` wraps a verified `luaL_loadbuffer`-shaped function. The four arguments are forwarded unchanged unless a caller-provided selector supplies a non-null replacement of at most 4 MiB. No Lua state is retained, and the bridge does not call Lua stack operations. Selection and result observation preserve the original call's Windows error state. Callback reentry forwards without recursively invoking callbacks; original exceptions propagate with active-call accounting unwound.

The boundary is informed by tpp-mod's current `scripting.cpp` and `symbols.hpp`; this bridge is project-authored. Actual detours use pinned MinHook v1.3.4, commit `c3fcafdc10146beb5919319d0683e44e3c30d537`. [Attribution and licenses](../THIRD_PARTY_NOTICES.md).

Installation requires caller-supplied expected bytes at an executable address. This check alone is not a build registry or ABI/lifecycle proof: the production adapter still needs an exact executable fingerprint, verified target and safe installation boundary. Policy/context/replacement storage must remain valid and callbacks must be thread safe. Removal is only for a controlled fixture or after **all possible callers have stopped**; an idle counter alone does not authorize hot-unloading.

## Fixture result

The executable fixture exercises a real MinHook trampoline, wrong-byte refusal, unchanged arguments/results, replacement, invalid replacement fallback, both callback reentry paths, original exception forwarding, four concurrent workers, and byte restoration/removal after workers join. It then checks a pass-through installation without callbacks. **2,010 fixture calls, zero failures**. All 14 project CTest suites pass in Release. [Build/test output](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-tests-script-bridge-2026-09-09.log). These calls are test code, not the game's parser or connected clients.

## Exact game boundary and live receipt

Current source maps `luaL_loadbuffer` to RVA `0x1A178E0`. The installed executable SHA-256 is `085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45`. Its complete 36-byte declared body stores RDX/R8 as a buffer/length pair, supplies a reader callback, calls the parser at `0x1A11500`, and returns its EAX. The epilogue at `0x1A178FF` restores 0x38 stack bytes and returns. [Decoded body](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/script-load-boundary-2026-09-09.json).

The bounded startup observer captured this natural call on PID **104372**, thread **0x4FA4**, at **18:34:33 UTC**:

- Name: `@Fox/Scripts/Classes/Entity.lua`; buffer length: 122 bytes.
- Entry stack: `0x15F578`; parser-return stack: `0x15F540`, exactly 0x38 lower.
- Matching thread/frame reached the fingerprinted epilogue with **EAX=0**.
- Debugger detached with exit code 0, no abort and no deadline recovery.

[Result](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/script-load-2026-09-09/result.json), [trace](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/script-load-2026-09-09/debugger.log). Private debugger pipe recovery coordinates are excluded. The observer uses the existing temporary software entry bootstrap and hardware entry/epilogue breakpoints. It reads a bounded script name/prefix, makes no game calls, and never modifies scripts or archives.

The original parser's success is established for that call. **Visual continuation is not established.** The first captured game window was blank; after the user's interruption/retry, the process remained running with no window returned by Computer Use. A non-invasive main-thread stack inspection showed execution in a SleepEx path, not a remaining debugger stop. This does not identify why the window disappeared. A later request for the Steam window returned a different application image, so UI inputs were stopped pending clarification. The process was not force-terminated or duplicated.

## Next integration

The user-requested retry at **18:49:53 UTC** captured the same natural `Entity.lua` load and EAX=0 return in PID **91880**, thread **0x1BEBC**, then detached without abort or timeout. Computer Use subsequently verified the startup notice, title prompt and main menu at 69% completion. Return advanced the notice and opened the main menu. The game was left at that menu. This establishes visible startup continuation for the retry; it does not explain the earlier missing window or validate an installed bridge. All seven save/config hashes matched the 17:45 check, and executable, Steam DLL and installed probe hashes matched their recorded values. [Retry trace](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/script-load-retry-2026-09-09/debugger.log), [continuation](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/script-load-retry-2026-09-09/continuation.json), [preservation check](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/script-load-retry-2026-09-09/preservation.json).

The 0.5.0 pass-through trial establishes that installation after DirectInput catches `init.lua`, `Tpp/start.lua` and TPP libraries. See [installed bridge trial](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/game-script-hook.md). Next add an explicit immutable script-replacement policy and verified archive mounting/selection for the staged actor files. Session creation, four live actors, motion/parts coverage, save isolation and every campaign segment remain unfinished.
