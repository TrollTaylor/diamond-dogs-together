# Co-op trigger predicates

Workspace **0.10.14** adds DYNAMITE-derived local/host trigger predicates. It is compiled only, not installed or executed in MGSV. The intended upstream effect is to avoid guest trigger-entry crashes; this port has no gameplay evidence for that outcome.

The source is Dynamite `c90f252f8200765f66c005d6bf4357fda0c0d98e`, three return-value patches in `hook/src/dynamitePatches.cpp`, GPLv3 with Commons Clause 1.0. [Attribution and license](../THIRD_PARTY_NOTICES.md).

The exact inspected executable is `D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe`, version 1.0.15.4, SHA-256 `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`. Named data-element registrations and their constructors establish these callback tables and complete bodies:

| Predicate | Callback RVA | Complete size | Table slot RVA |
| --- | --- | --- | --- |
| IsPlayerAndLocal | `0x587E70` | 135 bytes | `0x219FFF8` |
| IsPlayerAndHost | `0x588850` | 126 bytes | `0x21A0488` |

Both callbacks take the candidate object in RDX and return a Boolean in AL. The object-ID accessor reads the word at object offset `0x68`. The local predicate compares the low nine ID bits with the published local-player index; the host predicate queries native session host status. Both include a non-player type fallback and false/null paths. The upstream local SETZ expected-byte sequence appears to contain a ModRM typo; this mapping uses named types, constructors, table slots and full dataflow. [Static evidence](evidence/trap-contract-2026-09-10.json).

`native/trap_patch.cpp` calls each original callback, then returns true only when both hooks installed successfully and the adapter reports an active owned co-op session. Otherwise it returns the original result. Installation validates both complete callback bodies and four table slots before enabling either policy. Partial installation retains any possibly executing trampolines and keeps the policy inactive. Existing session-loss, failure and termination deactivation disables the overrides; returned close clears the owned-session binding. The hooks remain pinned and forward outside this scope.

The true-return policy deliberately matches all three upstream changes, including original false/null and non-player outcomes. It broadens trigger eligibility beyond player objects. Trigger ordering, duplicate effects, mission-specific consequences and crash prevention remain unverified; this does not implement campaign trigger synchronization. Session flags also do not prove exclusion of in-flight teardown calls.

The first false-to-true override for each predicate records `trap_rule=override_observed`. That receipt would show only predicate execution, not correct interaction behavior. Compilation used `tools/build.ps1 -Configuration Release -BuildOnly`; no tests, Lua execution, game launch or installation occurred. [Prepared build](evidence/trap-rule-prepared-2026-09-10.json). Installed 0.10.5 remains unchanged.
