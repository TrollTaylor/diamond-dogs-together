# Remaining native gameplay adaptations

Source audit, updated 2026-09-12, against pinned Dynamite `c90f252f8200765f66c005d6bf4357fda0c0d98e`. Workspace/installed 0.10.30 has passed native fixtures; September 12 live testing verified host creation/admission and local input, but no guest control or shared combat. See [test results](testing-2026-09-12.md). This list concerns the immediate two-player scene/combat target; campaign progression and four players remain deferred.

The review of `hook/src/dynamitePatches.cpp` identified that the 0.10.10 Lua respawn request lacked a companion native branch bypass. Version 0.10.11 now includes that mapped adaptation. The command merely sets request bit 8; a returned Lua call alone was insufficient evidence of respawn. [Respawn implementation](respawn.md).

Version 0.10.12 also includes the sight and damage-controller host-condition adaptations, scoped to two mapped callers and the owned co-op session. This addresses code gaps beyond nearest-player selection and the player damage callback; it does not establish detection, alert or world damage behavior. [Implementation and limits](combat-role-rules.md).

Version 0.10.13 adds the hostage flag-request role adaptation to the same owned-session hook. It preserves the game's flag writes and dirty-bit updates, but remote hostage behavior remains unverified.

Version 0.10.14 adds the named local/host trap callback adaptations with original execution and active owned-session scope. Trigger eligibility broadens as in upstream; event ordering, duplicate effects and crash prevention remain unverified. [Trigger implementation](trap-rules.md).

Version 0.10.15 adds the soldier action-task branch and normal CP-member status dispatch, with complete body gates and co-op scene activation/restoration. Enemy behavior and replication remain unverified. [Soldier implementation](soldier-rules.md).

Version 0.10.16 added the game-over and FOB mission-result submission bypasses through a shared native hook restricted to two exact callers. The co-op result scope survives teardown until the next mission starts. No network behavior is verified. [Result implementation](result-rules.md).

Version 0.10.18 adds the separately identified `SessionDisconnectPreparingMembers` call from upstream TppMain termination, before close. It requests native cleanup of preparing members and an optional auxiliary task, then rechecks ownership. Requests and returned calls do not establish that members have disconnected. [Implementation](preparing-disconnect.md).

Version 0.10.19 adds the last branch adaptation identified by this immediate native patch-list audit: full `CloseSession` mission-condition bypass. It changes the verified wrapper's branch during the authorized close call and restores it on the returned/failed receipt. Native service dispatch and the null fallback remain in the original game function; the concrete service implementation remains unmapped. [Implementation and limitations](full-close.md).

This closes the listed code-port gap, **not the gameplay acceptance gap**. It does not prove that every DYNAMITE dependency or required gameplay path is implemented. The source audit is not an exhaustive engine or campaign audit. Several upstream entries are explicitly uncertain (graze damage and resolver restart), so they are not assumed necessary merely because they appear in the patch list.

A broader startup review found a missing step beyond the native branch list: ordinary Mother Base allocation does not call `CreateEquipGhostBlockGroups`. Version 0.10.20 now adds DYNAMITE's one-remote-player equipment group before the original equipment load begins, and gates admission/join on returned preparation calls. [Equipment loading](equipment-loading.md).

Version 0.10.24 adds upstream's native fixed/follow/remove marker-loading guards and skips the Lua checkpoint-marker restoration call during guarded loading. Actual hang prevention is unverified. [Marker loading](marker-loading.md). Version 0.10.21 now follows upstream manual connection timing with a guest Ctrl+F8 control after mission startup. It does not automatically detect host readiness. [Manual join implementation](manual-join.md). Version 0.10.22 adds the mapped equipment restriction setter hook with co-op scope. Version 0.10.23 adds the companion item-selection predicate bypass, identified by its restriction flags and selection callers. Both equipment adaptations remain unverified in-game. Remaining non-patch hook dependencies still require review. [Equipment use](equipment-use.md).

Dynamite's supplementary sync schema carries markers, script variables, emblems and Boss Quiet commands/damage. This project has not connected that protocol. Boss-specific and campaign-variable adaptations remain deferred; the existing native player/world paths must first establish the immediate shared scene and combat target.

All prepared gameplay changes still need eventual live verification: joined peer identity, remote actor allocation/control, published guest state, combat outcomes, respawn, disconnect and shutdown. No such evidence was created by this source audit, and no tests or game launches were run.

Version 0.10.25 adds owned-session feedback instead of DYNAMITE's hook-driven one-time connection announcement. Its supplementary marker/emblem protocol is still absent. The original connection-readiness predicate remains in place; read-only decoding shows the idle-target prerequisite avoids its first refusal, while a busy flag and state threshold still apply. No actual guest refusal has been observed. [Session feedback and readiness audit](session-feedback.md).

Version 0.10.26 adds the existing-marker FOB enemy icon to buddy icon conversion, scoped to the owned session. Marker creation, duration extension and supplementary synchronization are not implemented. The later upstream hook audit also found climbing-state and path-result fallbacks whose current-build mappings remain unresolved; they were not added from candidate addresses. [Teammate marker implementation](teammate-markers.md).

Version 0.10.27 fixes teardown being triggered only by the diagnostic trace: an independent lifecycle receipt now runs before other termination wrappers, and tracing errors no longer prevent original callbacks. Native cleanup and close still require gameplay verification. [Termination boundary](termination-boundary.md).

Version 0.10.28 addresses the worker-thread host-preparation refusal seen in the user's September 11 log. Its [explicit handoff](mission-thread-handoff.md) still needs runtime verification. A further source review confirmed that DYNAMITE's custom message schema carries no general player transform/input messages and its player synchronizer initialization hook forwards the game initializer. The standalone transform codec should not be treated as a replacement for this native path. The climbing-state and path-result hooks remain unmapped; no candidate-only calls or patches were added during this review. The corrected [local package](local-package.md) prepares a concrete next installation, but no installer or game was executed.

Version 0.10.29 extends that handoff to all native rule controllers and retains the outgoing mission for cleanup when the live mission ID already names the destination. This prevents the prepared code from rejecting damage/soldier restoration solely because allocation used a worker, or skipping owned close solely because the destination is outside Mother Base. Neither runtime restoration nor session close completion is verified. [Outgoing-scene fix](outgoing-scene-cleanup.md).
