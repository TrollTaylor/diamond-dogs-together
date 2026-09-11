# Co-op FOB result suppression

Workspace **0.10.16** includes DYNAMITE-derived suppression of the game-over and FOB mission-result submission calls. Compilation succeeded; it is not installed or executed in MGSV. Neither result-screen behavior nor absence of network submissions is live-verified.

Source: Dynamite `c90f252f8200765f66c005d6bf4357fda0c0d98e`, game-over and FOB-result bypasses in `hook/src/dynamitePatches.cpp`, GPLv3 with Commons Clause 1.0. [Attribution and license](../THIRD_PARTY_NOTICES.md).

The inspected executable is `D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe`, version 1.0.15.4, SHA-256 `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`. Both mapped sites reach the same 3,712-byte routine at RVA `0x7F31F0`:

| Path | Call | Return address at submission routine |
| --- | --- | --- |
| Game over | Direct call at `0x88816E` | `0x888173` |
| Mission result | Interface slot `+0x368` at `0x160154E` | `0x1601554` |

The mission-result slot at `0x255A3E8` points to wrapper `0x1E0E950`. That wrapper obtains the service through getter `0x7D6830` and tail-jumps to the shared routine, preserving the UI return address. The shared routine consumes a service pointer, a Boolean and two 32-bit values, and returns a Boolean. Both selected callers ignore the result. It builds request data and invokes queue methods through the service at `+8`. [Static mapping](evidence/result-contract-2026-09-10.json).

`native/result_patch.cpp` hooks that routine and skips only the two mapped callers while the co-op result scope is active. Skipped calls return false; no request-state field is marked complete and no online success is fabricated. Other callers forward all arguments and the original result. Full native routine, game-over caller, wrapper and getter hashes gate installation, together with the complete mission-result code, its 23-entry jump table, and the interface slot. The game-over branch and mission-result call instruction are unchanged.

Successful co-op player preparation arms the scope before native creation and online initialization. It remains armed during setup failure, client loss, death, teardown and result screens. A source-owner-thread receipt inserted unconditionally before the next `Mission.Start` clears it, including when entering an ordinary mission; new co-op preparation then arms it again. This scope deliberately differs from gameplay rules that stop at termination. The hook remains pinned and forwards outside that scope. An incomplete hook installation never arms suppression.

First observed skips log `result_submission=suppressed` with `site=game_over` or `site=mission_result`. Such a receipt would establish only one skipped mapped call. It would not prove that all online requests are absent, result screens complete, or delayed callbacks belong to the intended mission. Lifecycle receipt delivery and concurrent transitions remain unverified. Suppression covers these two sites on this executable only.

Compilation used `tools/build.ps1 -Configuration Release -BuildOnly`; no test suites, Lua execution, game launch or installation occurred. [Prepared build](evidence/result-rules-prepared-2026-09-10.json). Installed 0.10.5 remains unchanged. Full native close-session behavior, independent guest control and shared gameplay still need work or verification.
