# Exact-source Lua bootstrap

Loader 0.6.0 adds the first project-authored script replacement. It inserts `lua/coop/bootstrap.lua` before the installed Tpp.lua module's final `return e`, preserving the original source around that insertion. The compiled DLL contains the project fragment, not the game's source. Extracted and patched game scripts remain local build artifacts.

Selection requires the exact chunk name `@/Assets/tpp/script/lib/Tpp.lua`, length 15,742, suffix `return e`, and SHA-256 `ac2ca8bfaf622dcb1b5e11570829b4d1e9ce98b22e4a1624562a59ff2a90572b`. Every selection rechecks the digest, including after the fixed replacement cache is populated. The existing exact executable, function-byte and post-DirectInput lifecycle gates still apply.

The development marker `mgs-coop/script-observe.enabled` accepts exactly `bootstrap-v1` followed by LF (13 ASCII bytes) for this mode. `observe-v1` followed by LF retains pass-through observation. Other content leaves the hook inactive. Changes apply on the next launch; no live unload is provided.

The fragment assigns `e.MgsCoop`, with bootstrap version 1, a declared maximum of four, `gameplayEnabled=false` and the two staged player-package names. `GetPlayerPackages()` returns a fresh list. It does not mount packages, allocate actors, connect peers, alter missions or write saves. The declared maximum is not a demonstrated actor count.

After assigning the table, the fragment invokes standard Lua `loadstring` on a unique diagnostic string. The existing native load hook recognizes the exact string, length and chunk name as an execution receipt. A successful parse of Tpp.lua alone would not prove execution; observing this subsequent receipt establishes that the injected fragment ran past its table assignment. Native code retains no VM pointer and makes no extra VM calls.

## Local validation

All 14 existing CTest suites passed in the [0.6.0 build](evidence/native-tests-bootstrap-live-build-2026-09-09.log). The separate [bootstrap checks](evidence/bootstrap-local-checks-2026-09-09.log) passed 14 patch checks and 400 concurrent selections: mismatched or mutated inputs are rejected, original source boundaries are preserved, and cached output remains stable. The local Lua 5.1 fixture executed original and patched modules with explicit engine stubs, preserving original export types and dependency lists and observing exactly one receipt. These stubbed checks do not validate game engine behavior.

Reproduce against locally extracted Tpp.lua:

```powershell
./build/native/bootstrap_patch_check.exe build/startup-script-inspection-20260909/package-00.lua build/startup-script-inspection-20260909/Tpp-bootstrap.lua
./build/lua-fixture/lua51.exe tests/bootstrap_lua_check.lua build/startup-script-inspection-20260909/package-00.lua build/startup-script-inspection-20260909/Tpp-bootstrap.lua
```

## Live execution

On 2026-09-09 at 19:15:41 UTC, PID 105412 launched normally with loader 0.6.0. Entry 29 selected the exact Tpp.lua replacement; its parser returned zero with `replaced=1`. Entry 30 captured `execution_receipt=1`, also with parser result zero. Both ran on thread 10876. [Live trace](evidence/script-bootstrap-0.6.0-2026-09-09.log), [installation and previous-loader backup](evidence/loader-0.6.0-install-2026-09-09.json).

Computer Use verified the startup notice, title prompt and Return opening the main menu at 69%. PID 105412 was left with Continue selected. All seven save/config hashes matched the immediate prelaunch backup; the executable and shipped Steam DLL retained their accepted hashes, and the loader matched its installation record. [Continuation](evidence/script-bootstrap-0.6.0-continuation-2026-09-09.json), [save check](evidence/save-after-bootstrap-title-2026-09-09.json), [binary check](evidence/script-bootstrap-0.6.0-binary-check-2026-09-09.json).

Four-player allocation packages remain staged and uninstalled. The next engine step is to verify package mounting and allocation through the game's own startup/mission lifecycle, then establish a second independently controlled actor before extending to four. This bootstrap does not demonstrate playable co-op.
