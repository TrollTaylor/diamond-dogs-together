# Soldier action and patrol-status updates

Workspace **0.10.15** includes Dynamite's action-task suppression bypass and normal CP-member status dispatch. It compiles but is not installed or executed in MGSV. Shared enemy behavior remains unverified.

Source: Dynamite `c90f252f8200765f66c005d6bf4357fda0c0d98e`, `ActionControllerImpl::UpdateActionTask` and `Soldier2Impl::UpdateCpMemberStatus` entries in `hook/src/dynamitePatches.cpp`, GPLv3 with Commons Clause 1.0. [Attribution and complete license](../THIRD_PARTY_NOTICES.md).

The exact inspected executable is `D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe`, version 1.0.15.4, SHA-256 `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`. Current mapping:

| Update | Full dispatch body | Branch | Adaptation |
| --- | --- | --- | --- |
| CP-member status | RVA `0x136BC80`, 209 bytes | `0x136BD2B`, `74 07` | `EB 07` selects the fuller normal status writer |
| Action task | RVA `0x1373800`, 167 bytes | `0x1373827`, `74 1A` | `EB 1A` skips the record-flag suppression branch |

The CP dispatcher reads object flag bit 2 at `+0x2C4`, gathers indexed state and position, and selects between routines at `0x136C0F0` and `0x136BD60`. Both update indexed status/position records through object `+0xF0`, array `+0x18`; the normal routine updates a broader set of flags. The constructor prefix copies a setup Boolean into bit 2 and derives bit 1 using the arithmetic referenced in Dynamite's commented constructor patch. This supports the inferred online/non-FOB correspondence. The complete constructor was not decoded; only the relevant 265-byte prefix is claimed.

The action routine checks bit 2 at incoming record `+0xC2`. The bypass retains the existing task query, conditional update at `0x1372B70`, and shared tail at `0x1373E00`. It does not remove every task condition. The enclosing 572-byte update loop handles both online and ordinary paths, passes the action records, and advances arrays with strides `0xD0` and `0x1E0`. Its online-mode selector is distinct from the action record flag. Retail function names remain inferred, not recovered symbols. [Full static evidence](evidence/soldier-contract-2026-09-10.json).

`native/soldier_patch.cpp` validates both complete dispatch bodies, both status writers and the enclosing action loop before activation. It changes only each branch's first byte with an atomic exchange; instruction lengths, targets, arguments and state storage remain unchanged. Instruction-cache flushing and original page protection restoration are required. State records track actual changed bytes even after a flush/protection failure, enabling later cleanup attempts to recover them.

Activation is part of the existing Mother Base co-op setup before `Mission.Start`, on the source-owner thread. Existing player-rule deactivation on setup failure, client loss or mission termination attempts both restorations. A partial activation failure attempts to roll back both sites and reports any incomplete restoration. This is scene-lifetime scope, including startup before native session ownership; it is not a per-actor or per-call ownership check. Concurrent work already in flight can observe either branch selection during transitions, and no quiescence is established.

These rules do not establish synchronized enemy movement, patrol decisions, combat outcomes or shutdown. Compilation used `tools/build.ps1 -Configuration Release -BuildOnly`; no tests, Lua execution, game launches or installation occurred. [Prepared build record](evidence/soldier-rules-prepared-2026-09-10.json). Installed 0.10.5 remains unchanged.
