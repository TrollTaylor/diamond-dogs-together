# Outgoing scene cleanup

Build **0.10.29** extends the 0.10.28 handoff fix to cleanup ownership and the outgoing mission identity. It is compiled and prepared locally, with no installer or game execution. The original executable, Steam DLL and installed 0.10.27 loader are unchanged.

## Evidence and defects

The user's recorded September 11 launch allocated title mission 1, then logged `OnTerminate:40050` before allocating 40050. That scene later logged `OnTerminate:30050` before allocating 30050. Thus the traced live mission variable already identified the destination while the previous scene's termination callback ran. [Existing-log excerpt](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/user-launch-thread-excerpt-2026-09-12.log).

The old Lua cleanup predicate, native owned-close gate and full-close activation gate all required the live mission ID to equal 30050. On a transition away from Mother Base, that could skip the owned session close. The excerpt does not contain a Mother Base-to-title close with an active host, so that failure is a source-derived risk, not a recorded shutdown result.

The 0.10.28 thread handoff also covered only session/targeting ownership. Damage and soldier branches activated during allocation retained their worker-thread owner; cleanup on the runtime thread would refuse those restorations. Respawn and full-close metadata could similarly remain attached to the runtime thread when a later allocation needed to finish pending cleanup.

## Changes

- Damage, soldier, respawn and full-close modules now expose a serialized control-owner handoff. It accepts only the previous lifecycle owner or the current owner, changes no game bytes, and preserves active flags, original protection and pending restoration.
- The adapter transfers all rule ownership into an accepted allocation and again after allocation returns, before initialization. It records each module's result. Session ownership still transfers separately with repeated native host identity/type/state checks.
- Lua retains the mission associated with an accepted allocation until termination returns. Cleanup no longer uses the incoming mission variable as its scope.
- Both native close gates and full-close activation require the native adapter's captured outgoing mission to be 30050. That value comes from the accepted allocation boundary, not a Lua/network claim. The owned session, runtime thread, singleton/type/dispatch/body and native-context checks remain in place.
- Termination logs include outgoing and current mission IDs and cleanup status. No game mission variable is overwritten, native session close is still one-shot, and original callbacks retain their ordering, errors, yields and return values.

The inspected executable remains MGSV 1.0.15.4 AMD64, SHA-256 `085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45`. No new engine address or byte patch was introduced; existing mapped adaptations now use the corrected control lifetime.

## Validation limits

`tools/build.ps1 -Configuration Release -BuildOnly` compiled and linked the new loader. No tests, Lua/source-transform execution, installation, role change or game launch was performed. A refused handoff, changed native context or failed restoration still needs investigation; asynchronous close completion and guest gameplay are not established by a returned call. The next gameplay check remains Mother Base host startup, admission, return to title and normal shutdown, followed separately by a real guest connection/control check.

[Prepared build record](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/outgoing-scene-prepared-2026-09-12.json).
