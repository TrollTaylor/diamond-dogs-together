# Co-op player roles

Version 0.10.7 introduced the defender-index change described below. Workspace 0.10.8 also connects the offense-index hook to [host nearest-player targeting](enemy-targeting.md).

Version 0.10.7 extends the two existing role predicates with the native defender-index override from Dynamite. It is compiled only, not installed or verified in-game. The immediate target remains two independently controlled players with shared combat.

The original function at RVA `0x1E026B0` obtains the native session, reads its host member index, walks the roster, and returns the first member index different from the host. It returns `UINT_MAX` when it finds none. That dataflow explains why changing only the two local role predicates leaves a separate guest-as-defender rule intact.

The new hook returns Dynamite's value **15** during the existing co-op scene lifetime. This is an upstream policy value, distinct from the original failure value; it does not create or allocate a player. Outside that lifetime, the hook forwards the original argument and result. The native session's host identity and roster are unchanged.

All three hooks require matching complete current-build bodies before creation. They are installed before `Mission.Start` for the existing Mother Base experiment and activated together only after successful installation. The existing source-owner-thread termination receipt deactivates them. If enabling fails partway, callable trampolines remain alive and inactive hooks forward the original behavior. No new thread or periodic game call is introduced.

The 181-byte defender function spans three exception/unwind ranges. The static generator decodes the complete contiguous body, rather than mistaking the first range for the whole function. [Mapping and exact inspected executable](evidence/player-index-contract-2026-09-10.json). Source references: Dynamite `c90f252f8200765f66c005d6bf4357fda0c0d98e`, with interface semantics cross-referenced against tpp-mod `301bf797b7f8032941b21ed56a3570338de6ead4`. [Licenses and modifications](../THIRD_PARTY_NOTICES.md).

Compilation used `tools/build.ps1 -Configuration Release -BuildOnly`. No tests or game launches were run. [Build record](evidence/player-index-prepared-2026-09-10.json).

This change removes one defender-role assignment. Version 0.10.8 subsequently adds the dynamic offense-player selection code using present members and game-script soldier queries; its gameplay behavior remains unverified. Enemy targeting, connected guest control, shared combat, death/respawn and disconnect behavior still need completion and eventual gameplay verification. Installed 0.10.5 is unchanged.
