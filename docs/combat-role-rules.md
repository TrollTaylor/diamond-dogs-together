# Co-op sight, damage and hostage role rules

Workspace **0.10.13** includes three mapped Dynamite adaptations: client sight processing after an alert, the fuller damage-controller path, and local application of synchronized hostage flags. Version 0.10.12 contained the first two. Compilation succeeded; these paths have not been executed in MGSV. Installed 0.10.5 remains unchanged.

The reference is Dynamite revision `c90f252f8200765f66c005d6bf4357fda0c0d98e`, `hook/src/dynamitePatches.cpp`, under GPLv3 with Commons Clause 1.0. Upstream bypasses three host-condition branches and describes the damage change as preventing a crash when shooting animals. That outcome is not verified for this port. [Attribution and complete license](../THIRD_PARTY_NOTICES.md).

## Exact executable mapping

The inspected executable is `D:/Games/steamapps/common/MGS_TPP/mgsvtpp.exe`, version 1.0.15.4, SHA-256 `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`. Addresses below are relative to its loaded image base.

| Path | Complete mapped body | Role-query return | Conditional branch |
| --- | --- | --- | --- |
| Sight controller | `0x133C900`, 3,104 bytes | `0x133C9A9` | `0x133C9AB`, `0F 84 DC 03 00 00` |
| Damage controller | `0xDA55F0`, 3,082 bytes | `0xDA578D` | `0xDA5795`, `0F 84 41 05 00 00` |
| Hostage flag request | `0xEBCD00`, 177 bytes | `0xEBCD42` | `0xEBCD44`, `74 2E` |

All three paths obtain the network session and call its secondary interface's role-query slot at offset `0x28`. The slot at RVA `0x2127068` points to the 64-byte native query at `0x313FE0`. The analyzer records full-body hashes, session dataflow, conditional consumers and interface identity. Function names describe inferred correspondence with upstream, not recovered retail symbols. [Current static mapping](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/combat-role-hostage-contract-2026-09-10.json); [earlier two-caller mapping](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/combat-role-contract-2026-09-10.json).

The hostage routine preserves the incoming object, index, bit mask and set/clear argument. Its selected path updates the indexed 16-byte flag record at object offset `0x28` and sets bit 0 in the indexed byte array at `0x78`. Its ordinary client path instead submits a three-byte request through object offset `0x68`. The interface wrapper at `0xEBCE50` separates the low flag byte from upper bits, calls the mapped routine for the low byte, and handles upper bits locally. This correspondence supports the Dynamite adaptation; it does not prove the later dirty-flag publication or every hostage interaction.

## Implementation and lifetime

`native/combat_role_patch.cpp` detours the role query. It calls the original first, then returns true only when co-op rules are active, the interface is exactly the adapter's owned session plus `0x20`, and the immediate return address is one of the three mapped sites. Other callers receive the original result. The three native branches are unchanged.

Installation checks the original role body and method-table slot, hashes all three complete caller bodies, and retains the exact installed detour bytes. The adapter binds the session after host ownership capture or successful client ownership checks. Existing co-op deactivation on failure, client loss or mission termination disables overrides; a returned owned close clears the session binding. The pinned detour remains in place and forwards outside its active scope.

Targeting initialization runs before this detour is installed, preserving its original-byte checks. Respawn recognizes this module's exact owned detour instead of requiring the original role-query bytes after installation. Its other supporting-body and action-function hashes remain required. The respawn caller is outside the three override sites and receives the original role result.

The first false-to-true override at each site records `combat_role=override_observed` with `site=sight`, `site=damage` or `site=hostage`. This receipt would demonstrate only a scoped Boolean override, not successful detection, damage, hostage behavior or synchronization.

## Limits

Session pointer identity and lifecycle flags are not an atomic lifetime proof; address reuse and in-flight calls during teardown remain unverified. No connected guest, enemy alert behavior, animal damage, world synchronization or compatibility with another runtime patch is established. Version 0.10.15 adds the [soldier action-task and CP status adaptations](soldier-rules.md), also unverified. Version 0.10.14 adds the separately scoped [trigger predicates](trap-rules.md), also unverified. Hostage behavior and actual flag publication to the peer remain unverified. [Remaining gameplay gaps](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/dynamite-gameplay-gaps.md).

The build used `tools/build.ps1 -Configuration Release -BuildOnly`. No test suites, Lua execution, game launches or installation were performed. [Prepared build record](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/hostage-rule-prepared-2026-09-10.json).
