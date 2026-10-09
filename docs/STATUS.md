# Development status

## October 8, 2026: Steam online transport (fixture-tested, never run)

Two-PC play can now connect **online through Steam** with no port
forwarding, VPN or extra program ([design and record](steam-online.md)).
Link v2 runs unchanged on `ISteamNetworkingMessages002` (channel 0x4D47,
flags 0x21) from `ISteamClient017::GetISteamGenericInterface`, acquired once
in the post-RunCallbacks owner context behind hash, export-byte, module-path,
RTTI and forwarder-shape gates; Steam signals, traverses NAT and falls back to
Valve's relays. The game's own ISteamNetworking005, callbacks 1202/1203,
lobbies, sessions, S_IS_ONLINE, the FOB synchronizer and the Konami servers
are untouched. Players find each other with an `MGS2-...@<SteamID64>` code
(S1) or a Steam invite from the new F9 Partner / Co-op > Online panel (S2;
callback 337, registered by a SteamAPI_Init IAT wrapper in DllMain together
with the host's 1251, play + steam only), with a rich-presence beacon and a
4 Hz poll-accept as callback-free fallbacks. Play-Coop has a new menu
(`[O]` Host online, `[J]` Join online, `[L]` the unchanged LAN/VPN flow);
the lab stays UDP and refuses `net_transport=steam`. Evidence is fixture
only: `steam_transport_tests` (365 checks, two link v2 sessions end to end
over fake Steam interfaces with loss/reorder/duplication, a broken session
restarted by AutoRestartBrokenSession, busy REJECT, wrong-code hint, Release
exactly once, gates on synthetic PE images, fault latch, shutdown), the
extended config/status/shim/worker tests, the friend-package tests (online
host/join/wait flows) and the contract generator (`--check`, and
`--steamclient` for Valve's unpinned steamclient64). Next: the coordinator's
hidden self-test (`tools/worker/scenarios/steam-selftest.txt`), then the
two-PC acceptance list of [steam-online.md](steam-online.md) section 15 with
a friend. S3 (accepting an invite starts MGSV straight into co-op) awaits a
user decision.

## October 8, 2026: native actions and weapons integrated (fixture-tested, never run together)

`integ/finish` carries both native work packages below. One input frame v4
(112 bytes, payload 84) carries both new groups: the sender's movement rates
(xflags Rates 0x10, bytes 72..77) and its magazine (xflags Mag 0x20, bytes
78..81); every native sender sends v4 (rates only unless
`partner_native_rates=off`, the magazine only with a readable gun). v1..v3
frames still decode; the unreleased single-feature v4 drafts (104 and 108
bytes) are refused. Masks are the union: loco/census 0x2240006 (EVADE), gun
stage 0x224005E (EVADE, HOLD, RELOAD and FIRE with `partner_native_fire=visual`);
the worker gate admits exactly the three stages Get-WorkerPartnerSettings
emits (a gun stage without fire needs a gate change). Contract checker: 365
code sites, 91 vtable slots (both packages' sites, slots and assertions).

## October 8, 2026: native movement actions (fixture-tested, hidden runs pending)

The dive was missing on the other player's screen because EVADE (0x40000,
Space) was outside every native mask. The native masks are now 0x2240006
(loco) and 0x224005E (gun, with visual fire since the weapons merge), in
lab_config, the worker gate and stages and Play-OfflineCoop; ACTION (ride,
knife kill, pickup) stays masked. The
contract pins the PlayerPad enum pairs, the three EVADE press consumers (act
27) and their network-remote gates. Sprint and crawl ran at 0.835 of the
remote's speed because the per-id movement rate record of instance 1 kept the
default 1.0: `partner_native_rates=mirror` (worker loco/gun stages and Play)
copies the remote's dash/crawl rates (input frame v4) into instance 1's own
record inside the drain window and restores them on leave. The native
reconciliation no longer pins a body that is only slower (split runaway,
`lag`, along-track nudges, a stop grace), and the arbiter times only the idle
runaway. New hidden scenario `tools/worker/scenarios/partner-native-actions.txt`
([details](native-puppet.md#actions)). Fixture evidence only.
Review fixes: the rates write re-derives instance 1's own record in the hook
(id 1, the rate service of instance 1's block PS; else `owner`), the restore
outside the drain re-checks its chain, a legacy Arm restores like a disarm,
the forgotten mission's Arm never re-applies after MissionEnd, `lag` lines
have their own log cap, and the contract census pins all nine EVADE pad
readers (six state-specific ones beyond act 27 are an open item).

## October 8, 2026: native partner weapons (fixture-tested, never run)

The partner body's shots are now visible on the other screen without real
bullets ([design](native-puppet.md#weapons)). The RequestBullet owner filter
swaps slot 0 of the bullet iface vtables (`0x2337DE8`, `0x2337D18`) and drops
only requests stamped with instance 1's id; muzzle flash, recoil, sound and
casings stay. The gun stage of Play-OfflineCoop and the worker now runs
`partner_native_fire=visual` (mask `0x224005E` with EVADE since the actions
merge); FIRE stays masked whenever
the filter is not installed, live and armed. Reload: instance 1's non-local
shots never spend its magazine, so input frame v4 carries the sender's
magazine and the drain window mirrors it into instance 1's gun record before
its update (FIRE/RELOAD edges and remote changes), which lets CanReload pass
exactly when the remote can reload. Loadout: each copy sends its equipment
manifest (weapons, customized aliases, receivers mapped to stock ids) on
channel 2 and logs the apply verdict; only stock46 can be loaded on body 1.
Contract checker (weapons package alone): 325 sites, 87 slots, a reachability proof that the
non-local shoot path skips every local side effect, and an owner-stamp
assertion (every other shot request builder stamps a typed owner). Open: the shot noise of
visual shots (census pins its interface), projectile weapons, a generalized
writer. Hidden scenario: `tools/worker/scenarios/partner-native-weapons.txt`.

## October 8, 2026: spawn sync (fixture-tested, runtime unverified)

Guards placed with the F9 menu and the host's per-soldier debug actions now reach
the guest's copy of the same soldier ([debug-menu.md](debug-menu.md) "Spawn sync").
The host announces `ENEMY_DEBUG` (0x22) on the world link at level 2+ with a
matching roster (not W1); the guest applies it and answers `ENEMY_DEBUG_ACK`
(0x23). A native placement is repeated on the guest through the same verified
`Warp()`: its target is a grant `world::Core` records when the guest's Lua pops the
message, never a Lua value, checked within 20 m (60 m for corrections) of the
guest's view of the host and behind the existing lab key, contract, per-grant
budget and latch (`generate_soldier_warp_contract.py --check` now also pins the two
callers). Route placements and come / ignore / notice / disable / enable replicate
as plain script calls, so they also work in play mode. Placed guards stay in an
active area on both PCs, a 60 s settle window corrects a guard that drifts more
than 6 m, and both menus get history rows plus the read-only `world_status` and
`spawn_sync` commands.

Kill / sleep / faint / Fulton / clear reported `sync=none` because outcome sync (W1)
never turned on when the host had not crossed a free-roam area trap: the
start-of-mission snapshot then had no readable soldier and the guest refused it
forever. The host now infers its area from the nearest readable soldier's command
post (bookkeeping only, no game call). Evidence is fixture-only (`world_wire_tests`,
`world_receipt_tests`, `soldier_warp_tests`, `debug_menu_tests`, the world-align and
debug-menu Lua checks); the hidden scenario `tools/worker/scenarios/spawn-sync.txt`
and the extended `debug-menu.txt` hold the guest-side proof lines.

## October 8, 2026: native puppet (fixture-tested, default off, never run)

The partner body can now be driven by the remote player's real pad through the
game's own locomotion, action and gun systems ([design](native-puppet.md)).
Block 1's per-block job is deferred out of the parallel batch (H1: job slot
`0x2295420`) and run on the main thread after the join (H2: call `0xBF4D55`),
inside the game's own TLS scratch scope and entry read lock, in a drain window
that swaps in instance 1's pad. Native windows also follow the remote HOLD
latch, never open while a hook-thread batch is in flight, and (aim stage) write
the remote look angles after phase 0. A pure arbiter switches between this
drive and the proven `dm=map` + xform partner, which stays the automatic
fallback. Input frame v3 carries the sender's look, camera and heading; the
equipment manifest codec and channel-2 drain are in place (sender pending).
Everything is behind `partner_drive=native` and `partner_input_defer` (default
legacy/off; play mode refuses it). The worker gains a `native` profile with
census/loco/gun stages and `tools/worker/scenarios/partner-native.txt`. Fixture
evidence only: the contract generator re-verifies every site against the exe,
`partner_defer_tests` runs the deferral end to end on a synthetic image with
real worker threads, and the driver tests walk the full entering/leaving
sequence with fakes. The coordinator's hidden runs (S0 census, then S1 loco)
decide whether any default changes.
## October 8, 2026: native "Spawn enemy here" (fixture-tested, runtime unverified)

**Spawn enemy here** now moves the nearest unused living, enabled, on-foot guard
(15-500 m) 5 m in front of the host, facing him. It sends the engine's own
set-transform message `0x287158b07327` to the soldier's GameObjectId through the
GameObject manager's SendCommand (`[0x2305e70+0x38]` = `0xbf3a80` on the static
object `0x2c38008`), the same call the player carry code makes at `0x1055b7a`.
The soldier handler `0x1360f90` then calls `T->SetPose`/`SetRot`, the slots the
partner transform already uses. The feature is default off behind lab.ini
`debug_soldier_warp=1`, lab only (play forces it off, bit `0x10000`).
`tools/generate_soldier_warp_contract.py` pins 19 sites, 10 vtable slots and the
unwind entries of the call path, and the loader refuses on any byte difference.
Without "He comes at me" the guard gets `SetEnableRestrictNotice` (not hostile);
with it he gets `SetDiscoveryPlayer` after landing. An unrealized guard is
force-realized and the message is retried for up to 150 updates. Any native
refusal, an older loader or a timeout falls back to the previous route snap
(`mode=route;why=..`). The 8-per-mission cap covers both modes; the guest is
unaffected beyond the existing world sync.

One dispatch hop cannot be pinned statically: the per-type handle object and its
vtable +0x10 are built by protected code. Each attempt resolves the chain
read-only, logs `soldier_warp_census` lines and reports `leaf=pinned` only for an
argument-preserving forwarder to a soldier subobject whose +0xa8 slot is the
verified handler. In that case it first mirrors the handler's own index, realized
and stance checks. Otherwise it uses the engine's dispatch (`leaf=engine`). The
fixture `soldier_warp_tests` executes the real SendCommand, GetHandle, registry
and dispatch bytes against recorders. The hidden scenario
`tools/worker/scenarios/debug-menu.txt` (new header `labsetting
debug_soldier_warp=1`) expects `mode=warp` with `d` of 10 m or less. Nothing has
run in the game yet. Open: realize latency of far guards, patrol AI or navmesh
correction moving the guard afterwards, and host-local positions.

Review fixes (same day, still fixture-only): a recognized forwarder hop that does
not reach the verified handler on a soldier subobject now refuses unsent (`leaf`)
instead of using the engine dispatch. Each `spawn_enemy here` command may move one
GameObjectId, at most 160 times and never after it was handled, and only while it is
the most recently popped command. Any answer other than `r=0;h=1` or `r=-1;h=0`, or a
caught exception, latches the feature off for the process
(`soldier_warp=disabled; reason=anomaly|fault`). Native `command`/`mission`
refusals end the command (`spawn_context`) instead of snapping a guard to a route
during travel or teardown. A pending placement dropped by the menu's context check
releases its force-realize only while its mission is still in gameplay. The route
fallback's final result keeps `why=`. The debug-menu scenario now requires 30010 or
30020.

## October 7, 2026: partner facing while moving (fixture-tested, runtime unverified)

The sender published `vars.playerRotY`, which is the control direction. Hidden run
442f60813e23409da970d49be0c21788 showed it stepping -59 to -14 degrees in one
tick on W -> W+A while the local body transform eased over about 250 ms. The
receiver lerped that step over one 33 ms packet gap, so the run loop snapped.
The sender now publishes the census-validated local body-transform yaw. It
falls back to `vars.playerRotY` when the read fails or the body is more than
2 m from the published position. A `partner_facing=source; yaw=body|vars`
line is logged on each change. With apply=xform/hybrid on the net source, the
receiver also limits the applied yaw to 900 deg/s with eased acceleration as
a safety net (`partner_facing_dps`, 0 restores the exact yaw). Position stays
exact, and teleport, relink, Warp, realization and stale poses snap.
Resident turn clips were mirrored: increasing yaw is a left turn in the
right-handed Fox frame, so the hidden run's mouse-right pivots (-75 degrees)
had selected `tn_l90`. The mapping now selects `tn_r90` for them. Worker
`state.json` carries a per-tick `facing` ring (source, source body,
target, applied and body yaw, clip, move, snap) for
`tools/worker/scenarios/partner-turns-moving.txt`. That run of 370f570
confirmed the mirror fix: the right pivot played clip 14, which finished
naturally after 834 ms. The body-yaw sender is not yet verified at runtime.
## October 7, 2026: debug menu overhaul (source)

The F9 menu now has a header with pending/telemetry state, a quick bar with recent commands, a searchable category rail (Player, Enemies, Partner / Co-op, World, Tools), per-button results and inline confirmation, and keyboard shortcuts ([guide](debug-menu.md)). **Spawn enemy here** heads the Enemies page: it moves an existing living guard onto the scored patrol route nearest the player (host-local; no new soldier allocation or coordinate placement). New host/local actions include heal/auto-heal, Fulton infinity, slow motion, go to partner, time +1 h, a nearby-soldier table with per-soldier kill/sleep/faint/Fulton/mark and command-post phase. Busy actions now queue instead of being refused. Worker builds draw the menu into captured frames and drive it with `menu ...` text (`tools/worker/scenarios/debug-menu.txt`). Source and fixture checks only; nothing here has run in the game yet, and the APIs marked unverified need live evidence.

## October 7, 2026: animation continuation after Claude's hidden-worker tests

Claude committed the prior work and private-desktop runtime fixes on
`dev/oct7-continue`, through 805a8ec. Its two-game Afghanistan locomotion run
904f772a2df64307a469db4ef05fface closed normally, restored both installations
and matched all real saves. Controlled continuation disproved the proposed
stand/crouch inversion: the host started crouching and the old runner captured
movement after input expired. Corrected hidden tests confirm standing run 11,
sprint 5, walk 10, crouch idle/move 1/4 and prone idle/crawl 7/6. Worker Raw
Input modifiers and a private cursor getter now supply actual source gaits and
gun input. Captures require native timing, held input and fresh pose readback.

Two production turn losses are fixed: raw pivots survive delayed visual
movement/stance, and new matching-stance turns survive old native-clip
confirmation. Existing lifecycle, gun/movement cancellation, freshness and
native stop/confirmation gates remain. Recording regressions and all 110
audited suites pass. Final hidden run `1a2de304f22243d2b8900a7f2301c742`
qualified both standing directions and a crouched pivot (about 75/75/82
degrees), confirmed finite turn playback and return to idle, and completed
16 timely frames including held strafe. Both instances closed normally, both
baselines were restored and all ten real saves matched. No system input,
focus or desktop switch was used.

Automatic upper-body aim/fire/reload playback remains disabled. Genuine
source aim/fire/reload reaches the guest; guarded read-only comparison finds
populated source weapon animation inputs but all ten partner inputs zero.
Static requested-allocation and default-allocator proofs are frozen; the
natural current-context scheduler caller/root/lifecycle is still unqualified.
The V3 timing reader refused without samples and was not expanded. The
separate unfinished `wp/debug-menu` worktree is not merged in this animation
continuation. See [build-specific evidence and remaining work](testing-2026-10-07-hidden-worker-animation.md).

Normal release `DC5F5FDF...B1B3755` passed all 110 audited suites with worker
and all research options OFF, and is installed in both local game copies and
staged at the usual launcher's `build/native/dinput8.dll`. Owned rollback
backups are retained. Post-install checks confirm unchanged retail/Steam/runtime
files and all ten real saves, with no game or worker remaining. This exact
normal DLL has automated checks; the fresh gameplay evidence above belongs to
the separate Worker-ON build. Nothing was pushed.

## October 7, 2026: initial background-worker implementation

The user requested implementation and continued development without Computer
Use or visible game windows. A default-off worker is being integrated with
separate build profiles, audited fixture selection, a private-desktop
supervisor/DX11 probe, bounded in-process input/backbuffer capture and the
existing Lua-owner debug action queue. The dedicated Normal/worker-ON build
has passed all109 audited CTests,198 input-only checks and72 runner-policy
checks. All11 native research flags remain OFF. Three bounded private DX11
probe attempts refused before qualification; the latest records an empty
private-window enumeration (FALSE, error0, no bad callback) at startup.
These were the initial implementation results, before Claude's subsequent
qualified private-desktop game runs described above. Receipts are retained under
`build/worker-runs` and `build/worker-runtime`.
No global input or visible fallback is authorized. Gun pose work retains its
existing exact-build, native-contract, owner and lifecycle gates.

The separate read-only clip-timing V3 correction is implemented in the
research workbench: metadata is bounded to16 layer pointers/128 bytes,
and only explicitly selected layers are followed. It accepts the saved H8
nine-layer allocation while retaining the exact native allocation/table
bounds. Root verification passed617 detached checks,24 complete disk-code
spans and21 exact instruction assertions. This has no process reads or game
writes in its detached/static modes; actual V3 game readback remains pending.

## October 7, 2026: paused at user request

H8 completed a fresh two-game Mother Base → helicopter → Afghanistan transition with host-only Continue and guest arrival acknowledgments. The explicit manual upper-idle diagnostic passed live: remote Fire116 returned to Idle89 at its natural update, with no queued state and an unchanged lower-channel request record. Automatic gun animation remains disabled: selecting Fire still produces no visible gun pose, and all ten remote Motion/Gun inputs remain zero. Guest weapon initialization succeeded; the host writer refused before dispatch after travel because its selected equipment bank was not empty. The pure animation reader's refusal was independently diagnosed as its five-layer limit against an actual nine-layer body; the proposed bounded reader correction has not been implemented.

The H8 build passed105 non-long suites and both long installer suites. Both games closed normally at18:52:22 UTC; all ten real save/config files match run-start and backup. Active lab settings and markers were disabled, and both installations were restored through the owned installer to the previously tested normal21 DLL `D4AF2A7F...CF5230` and baseline runtime archive. The newer accepted-source collector and gun diagnostics remain saved in source/research artifacts and require a fresh normal-profile release check. No new release or commit was made. [H8 evidence and next steps](testing-2026-10-07-animation-debug.md#october-7-h8-manual-idle-validation-and-user-requested-wrap-up).

## October 7, 2026: animation and debug continuation

Current source allows eight existing resident turn clips alongside the 13 locomotion/stance clips, while retaining the original runtime archive hash and bytes. A fresh normal-profile run with all research flags OFF verified all eight resources finishing naturally and actual aimed mouse pivots selecting crouched turn 18 and standing turn 16. The subsequent T7 research run verified accepted-source collection with a crouched pivot and two separately settled identical pivots: both queued clips played once for950 ms and returned to idle. Visual interpolation remains unchanged. Measured improvement over the previous collector and a fresh normal-profile check of the newer source remain pending.

F9 patrol placement visibly reused an armed living guard in a development build; its local combat killed the player and host Continue reloaded both instances. Fresh H2/H3/H5 environment checks separately verified host noon/rain with guest clock differences of 1–2 seconds and matching rainy weather. Guard placement remains host-local, with no new soldier allocation or AI replication claim.

Gun intent and the default-off bounded action-history companion have live transport receipts. Automatic upper gun playback remains disabled. H3 manually selected upper Fire child 116 but showed no gun/firing pose or finite finish. H5 installed the corrected observation hooks and captured one natural local End path through departure to idle. Its remote Fire request with zero blend still showed low arms and no gun. The writer refused before native dispatch; a later resource-mapping review requires qualification beyond a predicted bank. Remote update observation succeeded after a documented Warp advanced the body epoch from 0 to 1; source now accepts the legitimate initial epoch 0, with focused regression checks. Both H4 and H5 closed normally and all ten real save/config files remained unchanged. Later candidates require fresh gameplay checks. [Build-specific evidence](testing-2026-10-07-animation-debug.md).

H6 verified remote update observation at initial epoch 0, but its writer refused because an old helper read logical45 rather than native46. The corrected H7 build subsequently dispatched the stock46 writer once in each game and bound handle605C4. A remote rifle is visible on the host partner's back. Pure owned readback in both games confirms active equipment46 and unchanged local slots/handles. A separately primed manual upper request selects Fire116, but its ten Gun/MotionVars inputs and caches remain zero, with low arms and no gun pose or finite finish. H7 also re-tested host noon/rain with guest clock differences of1–2 seconds and matching rain in both views. H6 and H7 closed normally with all ten real save/config files unchanged. Automatic gun playback remains disabled. The subsequent H8 run validates only the separately gated manual upper-idle diagnostic, as described above.

Normal packaging now explicitly disables all local research options before building, preventing cached experiment settings from entering a normal package. Supplied DLLs are packaged with their existing settings; the tool does not claim those settings are disabled.

## October 6, 2026: Enemy controls in source development

The F9 Enemy section now has camp and patrol-route selectors, host-only **Spawn guard on selected patrol** / **Spawn guard at closest camp**, and player teleports to the selected camp or last confirmed guard position. Placement reuses at most eight distinct normal living guards per mission, within 150 m of the player, and requests placement on an authored route at point 0. It refuses dead, recovered, unknown or unloaded guards; it does not allocate a new soldier. Guard placement remains a local host debug action, not replicated AI.

Camp travel prefers a readable living guard position, then falls back to the camp's actual declared player checkpoint through the retail `TppCheckPoint.GetCheckPointLocator` contract. Unloaded camps can use that validated pose for distance sorting. Missing/invalid locators refuse without guessed coordinates. Source fixtures pass **4,987 checks** and **224 exact-retail checks**, including strict vector method receivers and checkpoint fallback. **Live wilderness travel and visible guard placement are pending**; the dated installed-build evidence below does not validate these additions.

## October 6, 2026: debug menu development 0.11.1

The final debug-menu DLL is `0CB53F278436EA845D4240BC82EB2B09A4F51F08CA9C5C826460168067E898C6`, installed and hash-verified in both copies. Its only change from the B3 candidate is UI labels/help, including wording the current-gun action as **Refill**. Functional logic is unchanged. Final run `build/lab/runs/20261006-164030` separately verifies F9 in both Afghanistan games, corrected refill wording and inventory readback, automatic close on focus loss, and the menu's default partner preset on both roles. This manual preparation began with stage 0; the menu enabled stage 3 / net / xform / map / net look and received `partner_applied`. Both bodies then had parts `3,3`. After F9 closed, host W moved to `(797494,339428,1640439)` mm and guest remote/body readback matched at rest with 0 mm error ([movement views](../build/lab/runs/20261006-164030/screens/debug-final-movement-guest-164725.png), [preset view](../build/lab/runs/20261006-164030/screens/debug-final-partners-host-164712.png)). Both title menus used Continue in this smoke; it does not re-test host-only title following. The broader cheat results below remain B3 evidence.

Both final-run games closed normally through `WM_CLOSE`, with no fuse process and all ten real save/config files unchanged against run start and latest backup ([stop receipt](../build/lab/runs/20261006-164030/stop.json)). Actual Disable archived the offline markers and removed active lab/coop config and link secrets ([Disable log](../build/codex-debug-menu-release-disable-20261006.log)). Final [hash/file-state verification](../build/codex-debug-menu-release-verification-20261006.json) confirms the installed DLL, original executable/Steam DLL, runtime archive and ImGui license in both folders. Normal launches remain inactive; a normal real-save launch was not run. Defender's final DLL scan found no threats ([scan log](../build/codex-debug-menu-release-defender-20261006.log)).

B3 DLL `B3B8EE78C8222EE35CDEF08C608B59EED9F15D5010A9F48E2BE8249902428F02` was tested with two copied-save game instances on one PC in [run `20261006-163001`](../build/lab/runs/20261006-163001/run.json). Host-only Continue brought both players on foot to Afghanistan (`30010`). F9 opened the menu in both games, and scrolling exposed its header and footer. While the host menu captured a held W, position stayed `(800452,339300,1638765)` mm. With the menu closed, W moved the host to `(795493,339440,1641624)` mm; guest remote/body readback matched at rest with 0 mm error.

The B3 inventory receipt reports hip/back/secondary IDs `871/873/875`, active inventory slot `1`. Dropping the current hip weapon produced a visible **AM MRS-4** pickup ([host view](../build/lab/runs/20261006-163001/screens/debug-pickup-visible-host-163722.png)). Refilling the hip gun confirmed inventory ammo `30`, stock `210`, active slot `1`; its receipt remains `render_unverified`, so this does not prove a new gun was equipped or rendered. Bookmark `0`, a 2 m step and return confirmed position errors of 0.003 m and 0.000 m. Noon had both clock readback and visible daylight; the rainy-weather receipt coincided with visible rain ([host view](../build/lab/runs/20261006-163001/screens/debug-world-controls-host-163946.png)). Both games then closed normally through `WM_CLOSE`, with no fuse process; all ten real save/config files matched run-start and backup hashes ([stop receipt](../build/lab/runs/20261006-163001/stop.json)).

All **82 automated suites pass in aggregate after corrections**: the [combined run](../build/codex-debug-menu-final-ctest-20261006.log) passed 81/82 before the composition-fixture correction; [13 focused checks](../build/codex-debug-menu-layout-tests-20261006.log) then passed, including both world Lua checks. The [friend-package recheck](../build/codex-debug-menu-package-tests-20261006.log) also passed after source-inventory coverage was updated. The final wording DLL passes [13/13 focused checks in 3.10 s](../build/codex-debug-menu-release-tests-20261006.log) after all fixture targets were rebuilt. This is aggregate evidence, not a single clean 82-suite run on the final wording DLL.

The overlay uses Windows SDK D3D11/DXGI and Win32 input with pinned Dear ImGui core/backends. The render thread queues actions and reads copied telemetry; guarded actions are consumed on the game's natural Lua update, with mission-generation checks and bounded receipts. Complete ImGui MIT terms are packaged, and fixtures cover upgrade/restore of legacy seven-target packages to the new eight-target inventory. The default animation map has 13 locomotion/stance clips; partner gun aim/fire/reload and full turn actions are not transported. Arbitrary new guards and unloaded-gun spawning are not implemented. Pickups and guard enable/disable remain local debug operations. A real two-PC session, complete campaign/progression and physical damage death remain unverified.

## October 6, 2026: previous world build 0.11.1

The current route uses our own offline UDP link and separate co-op save copies. Development tests with two independently controlled game instances on one PC have shown both player bodies and synchronized movement in normal Afghanistan (`30010`) and Africa (`30020`) free roam. Mother Base (`30050`) remains supported. ACC, story missions and alternate mission packs such as `30011` do not receive a partner body.

The previous world-build **0.11.1** DLL was `4666A7DA7ED2711CED7D397ED0EFF92E41782C27127DFF9374C63B1DBBF99D65` from `046bb63`. It was installed and hash-verified in both copies for the following run. Run `build/lab/runs/20261006-115055` uses the offline link and redirected co-op saves. Host-only native title Continue sends all 47 records; the guest starts on foot at the host's same position `(1232994,20775,-125517)` mm, with player IDs `0,1` and both parts states `3,3`. Both copies pair in Africa `30020`, epoch `3`, roster `2220786415`, 265 soldiers and 68 CPs. Guest alignment `id=1` accepts 151 unknown soldiers, zero divergences and `w1=1`; host readiness follows 16 ms later. Native keyboard input independently moves each player; reciprocal commanded/body positions match at rest with 0 mm difference. That stream requires 28 keys / 47 records, including `svars.ply_isUsedPlayerInitialAction`; an exact-source `TppPlayer.Init` regression covers the earlier helicopter/foot mismatch.

At that stage, all 78 automated suites passed in aggregate after the lab installer test was rerun separately. Its arrival/title/recovery checks passed 11/11 ([check log](../build/codex-arrival-fix-tests-20261006.log)). The local [Play-World-Coop.bat](../Play-World-Coop.bat) launches the configured lab at world level 3.

On that previous DLL, the host checkpoint probe sends hash `0`, version `1`, pose `(1233009,21370,-123172)` mm / yaw `0`; the guest's actual VarSave callback confirms it 17 ms later. The injected guest PLAYER_DEAD opens Mission Failed in both games. Host-only native menu Continue then replays 47 records and returns both players on foot to the same checkpoint ground position `(1233009,20870,-123172)` mm. They pair at epoch `4`; guest alignment `id=2` accepts 151 unknowns, zero divergences and `w1=1`, followed by host readiness 34 ms later. Parts are `3,3` and reciprocal transform readback is 0 mm.

Host-only travel then follows Africa `30020` → ACC `40020` → Afghanistan `30010`, using 47 records on each leg. Both players visibly use the fresh helicopter entry. The guest reaches epoch `6`; alignment `id=3` accepts 271 soldiers / 77 CPs with 191 unknowns, zero divergences and `w1=1` at `368261214`, followed by host readiness 32 ms later. After both helicopter exits, the host ground view shows Snake and an opaque Quiet partner; native guest W for 1.5 s and host W for 6 s have reciprocal rested transform differences of 0 mm.

Setting `world=0` in both games logs UNPAIRED reason `5` and inactive level `0`; partner transform sync remains active. Both games then accept `WM_CLOSE` and exit normally at 12:08:41, with no fuse process. All ten real save/config files match both run-start and latest-backup hashes ([stop receipt](../build/lab/runs/20261006-115055/stop.json), [verification log](../build/codex-4666-live-stop-20261006.log)). Cleanup commit `9d5aac6` archives the active offline markers. Actual Disable leaves marker, `lab.ini`, `coop.ini` and link secret absent in both game folders, with the owned loader and game/archive hashes intact ([Disable log](../build/codex-final-disable-20261006.log), [file-state/hash receipt](../build/codex-final-install-verification-20261006.json)). The focused `lab_scripts_tests` suite passes 1/1. File-state inspection and the source gate establish inactive pass-through; a normal launch after cleanup was not run because it would use the real saves. Disk-write completion and physical damage death are not claimed.

The earlier diagnostic build was `59CD23348386815A84DA0CE9D18DB2B308E52BE21D1B5DDF9C254FC1910A9B13` from `5ab2c5a`, which distinguishes unreadable initial state from malformed alignment streams. Run `build/lab/runs/20261006-113833` found a startup mismatch: the host was on foot while the guest retained its older helicopter initial action despite matching seeded coordinates. Both remote bodies realized with parts `3,3` and zero transform readback error, but that startup did not establish an unobscured two-player on-foot view. Exact `TppPlayer.Init` source selects that action from `ply_initialPlayerState=RIDEON_HELICOPTER` and the guest's unsynchronized `svars.ply_isUsedPlayerInitialAction=false`; the host has `true` after landing. The 4666 world build added that mandatory seed and regression fixture; its separate run above verifies on-foot startup and fresh helicopter entry. Both games close normally afterward, and all ten real save/config files match run-start and latest-backup hashes.

The preceding live-tested integration DLL is `171041C2A598FD758FCC67A0B19B59BE4EC6C35FBA4CD98C2529BF946A9E97D6`. Run `build/lab/runs/20261006-111828` verifies native host title Continue following to Mother Base (`30050`) and host-only `goto=30010` following through ACC (`40050`) to Afghanistan (`30010`). The guest receives all 27 seed keys / 46 records; the host's title context is applied once, then ordinary allocation clears it. Both games reach CFG `30010`, epoch `5`, roster `2772775463`, 271 soldiers and 77 CPs. Guest alignment `id=1` is accepted with 191 unknown soldiers, zero divergences and `w1=1`; unknown states are held until their areas become readable, not treated as synchronized AI or positions.

The earlier diagnostic/title/recovery subset passed 11/11 ([check log](../build/codex-alignment-diagnostic-tests-20261006.log)).

The preceding test build's Afghanistan movement matches the local/remote coordinates in both directions with 0 mm difference at rest; screenshots show the separate Snake and Quiet bodies. Stage 0 → 3 warp rearming places the partner before enabling its bits, with ready parts and zero warp error.

The host checkpoint probe publishes hash `0`, version `2`; the guest's actual VarSave callback confirms the exact host pose 19 ms later. An injected guest PLAYER_DEAD request opens Mission Failed on both games.

Only the host used native menu Continue; both copies reloaded and paired at epoch `6`. The guest became ready about 74 s after the host; all 271 host soldiers were initially unknown, so snapshots remained gated. When the host area activated, the unchanged DLL recovered: guest snapshot `id=36` was accepted about 1.9 s later with 206 unknown soldiers, zero divergences and `w1=1`, followed by host readiness. This verifies recovery with a loading delay.

The preceding test build also follows host-only travel to Africa: both copies reach `30020`, epoch `8`, roster `2220786415`, 265 soldiers and 68 CPs; guest alignment `id=37` is accepted with 157 unknown soldiers, zero divergences and `w1=1`. Native movement on each copy shows both player bodies and matching reciprocal transforms at rest. The Afghanistan range check hides the partner at 200.004 m and re-enters at 149.969 m with placement before realization. Both processes then close normally via `WM_CLOSE`; all ten real save/config files match both run-start and latest-backup hashes. The later diagnostic build exposed the initial-action mismatch described above. Physical damage death and the killed soldier's post-Continue readback remain unverified. The October 5 W0–W2 tests remain the recorded evidence for shared enemy outcomes, alerts, clock and weather. [Integrated run and historical results](testing-2026-10-05-world-sync.md).

Initial pairing still requires the copied-save games to reach the same supported scene. A real two-PC session, complete campaign gameplay/progression and four players remain unverified. The [friend launcher](two-pc-play.md) uses the same offline UDP route and copied saves; the earlier native Steam-join investigations are historical.

Source commits `7ec1e39` and `07520e0` add the local free-roam controllers, mission gates and distance safety. `a4a5aab` versions the source and package as **0.11.1**. The field test ran the partner changes in a DLL still labelled **0.11.0**, before W3/W4 integration; it does not establish live results for the final integrated 0.11.1 DLL.

Run `build/lab/runs/20261006-095606` verified the 13-entry archive installed in both copies. Afghanistan guest movement matched the host's partner body to about 20 mm, and both copies hid the partner at 200 m. Africa loaded the normal replacement pack with two IDs and parts states `3,3`; movement command/readback differences were 0–18 mm. Screenshots are retained under that run's `screens/`. Both games accepted `WM_CLOSE` and exited normally; all ten real save/config files were unchanged against both the run-start manifest and latest backup. [Field test details](testing-2026-10-05-world-sync.md).

## Earlier development records

The dated sections below retain their original builds, test counts and limitations. They describe earlier native-session or offline development stages; their claims of current installation or missing guest control do not override the October 6 state above.

## October 4, 2026 (round 4): input-drive reconciliation, stance taps, prone, host-input leak (branch `wp/partner-anim4`, not run live)

Acts on live round 3 (see [partner-anim-appearance.md](partner-anim-appearance.md), "Round 4"):

- **Reconciliation** (`native/partner_recon`, pure, 101 unit checks): apply=input places the body once with a disarmed Warp at the time-aligned remote pose and verifies it before arming; drift is corrected by position-only nudges (T.vt+0x58); xform snaps only on a large sustained error; Warp only on teleport, relink, far, vertical or diverging; a body that keeps moving while the remote is idle (the round 3 stuck state) is pinned instead of sawtoothing. S+0x20[1] carries the body readback.
- **Stance and prone**: words default `hpr`, mask `0x2200006`, input frame v2 with edge sets and 12-frame hold/tap words, and `hold=own`, an exact replica of the classifier 0x434ED0 over instance 1's own history.
- **Host-input leak**: every window installs a PP vtable copy whose key-manager slots answer from the injected words, PP+0x14 carries the world heading, PP+0x278/0x4B0 are shadowed, the camera sandbox covers the request block, and `wrap=census|window` measures (and can close) the other walks of block 1's tree.
- `Play-OfflineCoop.ps1` now starts with the input drive; [W] falls back to the proven Warp. CTest passes 66 of 66 suites.
- **Review fixes (second pass)**: [I]/[P] and I6 send `correct=3000 runaway=pin` (a leftover `correct=0` had turned the reconciliation off silently); stale edge sets are dropped after a disarm and a stall keeps the held level without edges; a lasting stance mismatch gets one corrective tap (a hold for prone); the runaway pin also covers a stuck body with a moving remote; stale input with a live pose pins instead of a 10 m Warp cycle; wrap windows inject the level only; source/apply-only triggers re-place; the fallback Warp uses the 100 ms interpolated pose again; `speed_mmps` no longer sits on its span boundary. 153 recon checks including a lag model.


## October 4, 2026: partner animation and appearance (branch `wp/partner-anim`, not run live)

The fully offline partner (offline-v1, our own UDP link; [test report](testing-2026-10-04.md)) gets three new pieces. All three are experimental and switchable, and none has run live yet. See [partner-anim-appearance.md](partner-anim-appearance.md).

- **Animation.** Each copy sends its slot-0 animation fields (act, stance, speed) in a v2 transform frame. Each copy can apply them to instance 1 in one of these ways: the Q arrays, `SetStance` 0xFF5B10 on a change, or the stance request component.
- **Movement.** A smooth native body transform (`xform`/`hybrid`, Warp's own T.vt+0x70/+0x68 calls without the resets) is available. It falls back to the proven Lua Warp whenever a call is refused.
- **Appearance.** Each copy sends its look, and the receiver applies it with `SetPartsInfoAtInstanceIndex` (`look=net`; `look=quiet|snake` for local tests). offline-v1 now answers the character receipts, and the lab guest plays Quiet.

The defaults keep the proven behaviour. `Play-OfflineCoop.ps1` turns on the appearance sync and the Quiet guest; it keeps the proven Warp movement and turns the unverified native paths on one at a time from its console ([M] smooth movement, [A] stance sync), or all at once with `-Experimental`. Review fixes: received anim/look values are bounded (stance 1..3 with the valid flag, locomotion act ids 1..6 only, the Snake face table index), a refused stance call is retried instead of sticking, the census keeps the stance prerequisites out of the transform verdict, the stance lines have separate caps plus a summary, the A1/A3 plans use hybrid movement so Warp's resets cannot mask the result, and `Prepare-LabInstance.ps1 -Offline -Character default` reruns the proven E-series without the Quiet guest. CTest passes 63 of 63 suites.

## October 3-4, 2026: 0.11.0 (branch `coop-lab`)

**At this native Steam-session stage, no guest join had been verified.** CTest passed 57 of 57 suites. The current offline partner route is recorded above. See [October 3 test report](testing-2026-10-03.md).

- **Single-PC lab** ([how-to](single-pc-testing.md), [protocol](lab-protocol.md), `tools/lab/`):
  - Two instances run side by side. The guest has a fake SteamID and a renamed mutex, both instances use isolated save copies, and ISteamNetworking is emulated over loopback. The master URL is redirected to a local fuse.
  - Verified live: both instances boot together, the guest reaches Mother Base on its own save copy, the real saves are unchanged, and the join trigger, status file and connect diagnostics all work.
- **Blocker for the single-PC lab:** co-op sessions need a master-server login. fuse `f75b41d` (written for exe 1.0.15.3) makes the 1.0.15.4 client go offline. Offline, the host's `CreateHostSession` returns 0, and the guest's IsConnectReady sees login state 3 < 4. Deriving the version the client expects, and bypassing the login gate, were both stopped by the assistant's safety checks. Neither is pursued in this project.
- **Testing route at the time:** two PCs with real accounts on the normal online service. That earlier friend-test plan is retained in [friend test](friend-test.md); the current package uses the [offline two-PC workflow](two-pc-play.md). The runtime archive is game-derived and is never bundled.
- **Characters:** `coop.ini character=` (default guest = Quiet, fallback DD soldier). The Continue-path fix (`ALWAYS_APPLY_TEMPORATY_PLAYER_PARTS_SETTING` for one allocation) and the UpdatePartsStatus Quiet/Ocelot bypass are in place, but have not been confirmed live. D-Dog is not a playable type. See [characters](characters.md).
- **Other 0.11.0 changes:** Lua and patch checks run in CTest. Damage-table field 13 clearing is opt-in, which matches Dynamite. Client join reset/re-arm: Ctrl+F9 or `reset.trigger`.

## Earlier record

Updated 2026-09-12. Immediate scope: two independently controlled players together in a scene with shared gameplay/combat, using direct attributed Dynamite adaptations. Complete campaign scripting/progression and four players are deferred. **Playable co-op remains unfinished and unverified.**

## September 12 code and installation

**Recorded installation: 0.10.33, Host mode.** DLL SHA-256 `FCB0E16A5AF1EE3E1884C7EB14D2EADFD33AC83280D7C0104EA2190EA564F9B6`. The user authorized testing on September 12. All 21 native suites passed, including session release and initialization/termination ownership regressions. Actual-source transformation, complete Lua parse, prestart execution and 91 lifecycle forwarding/cleanup checks passed. Installer upgrade/restore testing also found and fixed PowerShell's empty backup-path conversion in `File.Replace`. The owned live install succeeded. [Test results](testing-2026-09-12.md), [package workflow](local-package.md).

**Loading and termination fixes, 0.10.31–33:** live runs exposed termination on a different worker and OnInitialize running independently of the eventual gameplay thread. 0.10.33 records matched initialization callbacks and establishes runtime ownership at OnMissionGameStart. Its live Diagnostic run (PID 20680) prepared Mother Base, polled the manual guest shortcut, restored rules on a different termination worker and accepted the next scene's setup. It returned to title and quit normally, with all seven save/config hashes and original EXE/Steam DLL hashes unchanged. Automated F8/Ctrl+F8 taps produced no input receipt; a physical shortcut check remains unverified.

**Final Host regression, 0.10.33:** PID 52824 created the Mother Base host on loading worker 83844 and transferred its ownership to gameplay thread 54152. The native session reached state 3 with acceptance enabled; a local dive visibly responded. Return to title restored rules, observed the game's session destruction, cleared ownership and accepted the next scene's allocation/initialization/runtime callbacks. The game quit normally. All seven save/config files and original EXE/Steam DLL hashes still match the baseline. The game is closed; Host mode remains selected. This run covers an owned host's loading-to-gameplay handoff without a guest. Owned-session termination on a foreign worker and connected-peer teardown remain unverified.

**First live run, 0.10.29:** PID 87784 loaded Mother Base, created a native host, set acceptance to 1 and responded to a local dive. Return to title exposed stale adapter ownership after the game had already destroyed the session. Native rule restoration succeeded; the title menu and normal quit worked. All seven save/config files, the executable and shipped Steam DLL matched their prelaunch hashes. 0.10.30 reconciles ownership only after repeatedly reading empty session/manager/main singleton slots at teardown; its live regression passed: ownership cleared and the next scene's allocation/runtime receipts were accepted. The title menu appeared. ACC loading also exercised a successful worker-to-runtime handoff. No guest joined.

**Earlier guest preparation, 0.10.30:** PID 59896 loaded Mother Base in Diagnostic mode, completed equipment preparation and passed the then-automatic native client prerequisite check without a join call. Return to title restored scoped rules and accepted the next scene's setup. This run and its host regression quit normally, with unchanged save/config and original game binary hashes. This diagnostic did not create a fake player or verify independent guest control.

The preceding 0.10.28/29 fixed the user's skipped worker-thread host preparation and outgoing-scene cleanup ownership. The user's standalone transform codec and Diagnostic role remain; neither creates or controls a remote actor. [Startup handoff](mission-thread-handoff.md), [outgoing-scene cleanup](outgoing-scene-cleanup.md).

Version 0.10.27 fixed operational teardown depending on the bounded diagnostic trace. Its dedicated termination signal remains in use before the other mission wrappers. [Teardown fix](termination-boundary.md).

Version 0.10.26 adds Dynamite's exact FOB-enemy-to-buddy icon conversion for an existing marker during the owned session. It does not create, extend or synchronize markers. Climbing and path-result fallback hooks remain unmapped. [Marker implementation](teammate-markers.md).

The preceding 0.10.25 adds host/guest status messages from owned-session and repeated roster observations. Submitted requests remain distinct from established sessions with two present members. It also decoded and retained the original connection-readiness gate. [Feedback and gate audit](session-feedback.md).

**Single-PC feasibility:** read-only inventory found an RTX 5080, approximately 64 GB RAM and VirtualBox 7.2.4 with an existing powered-off Windows 11 VM. A second isolated instance is plausible, not verified; guest graphics compatibility and a second licensed Steam identity remain prerequisites. No VM changes or launches occurred. [Investigation](single-pc-testing.md).

## Recorded live progress

- 0.10.0 created a native host, loaded Mother Base with two indexed storage entries and online initialization, returned to title and quit normally.
- 0.10.1 enabled native host acceptance after startup and disabled it before termination. No guest joined.
- 0.10.4 guest setup loaded Mother Base and returned to title. Read-only native client prerequisites passed; no native guest request or peer was involved. The game was subsequently quit normally and all seven save/config hashes matched the backup. [Result](evidence/client-loading-result-2026-09-09.json).
- Eighteen native suites passed for 0.10.5 before the user requested no further tests. The September 12 test run above supersedes that automated result; no test count establishes a connected player.

## September 12 remaining work

A real guest connection and independent actor control remain unproven. Guest/enemy damage, enemy targeting, world authority and synchronization, death/respawn and disconnect handling still need completion and eventual gameplay verification. Host targeting now has an integrated code path, but remote position publication and targeting behavior are unverified. Disconnect and owned-close code are now connected, but event delivery and asynchronous cleanup remain unverified; automatic reconnect is not enabled. Death/respawn now has a scoped code path, but actual recovery, placement, simultaneous deaths and post-respawn control remain unverified. Sight, damage-controller, hostage flag, trap, soldier action-task and CP status rules are now implemented but unverified. Online-result suppression and the full close-session branch adaptation are implemented but unverified. The concrete FOB service implementation remains unmapped; its existing game-owned dispatch is retained. These adaptations do not establish complete shared combat. Only one PC is available. A local simulated peer could exercise packet logic or actor loading, but would not verify a real remote connection or independent game client.

## Previous allocation and readiness investigations

The records in this section describe earlier versions and processes. The current installation is recorded above; these older entries are historical observations.

**Latest investigation: normal job ownership and cancellation mapped.** The dispatcher holds the same lock used by manager destruction across the bandwidth callback. Registration takes a scheduler reference; finished and pending cancellation branches clear scheduler ownership before releasing it. A new exact-build analyzer passes 103 additional instruction checks and three table checks on top of the allocation analysis; all 38 code bodies match live in PID 14496 at 23:22:33 UTC. Session and bandwidth-manager fields remain null. This resolves the static callback-lock/reference relationship, not live cancellation latency or transition behavior. [Evidence and next trial prerequisites](job-lifetime.md). Installed 0.9.6 and prepared 0.9.7 are unchanged; native host creation remains disabled.

**Latest development: isolated allocation bridge, not installed or linked into the probe.** The real fixture detour passes 1,605 checks, including exact-site routing, cross-thread scopes, exception recovery, concurrent forwarding and restored bytes; all 15 suites pass. The exact-build allocation analyzer now passes 102 instruction predicates, three table checks and 22 live body comparisons. Job shutdown locks, clears the manager pointer, unlocks and sets a cancellation-request bit; scheduler exclusion and completed cleanup remain unresolved. An earlier fixture result changed with diagnostic instrumentation; the final separate-translation-unit fixture passes five repeats, with that uncertainty recorded. [Implementation, evidence and remaining work](allocation-bridge.md).

**Prepared code: 0.9.7, not installed.** The player constructor records a status-derived bit that allocation consumes to select network setup and interface queries. The new exact-build analyzer checks 46 instructions, one method-table entry and four complete bodies. The preserved four-slot sample has this bit clear. Early host prerequisites and later player readiness are now separate functions; native creation still refuses unconditionally at the lifetime gate. All 14 native suites pass. [Initialization and decision](player-session-initialization.md). Built DLL SHA-256: `B5595BC2AF720E17F1089540109BF53CA44C3FC035BB9386173187AABDFDCCA6`.

**Current installed loader: 0.9.6, four-slot archive with precise host-readiness diagnostics.** The player implementation is null before Mission.Start and at OnInitialize entry. At OnMissionGameStart entry, the unchanged preflight passes with index limit four and a null current block. All three observations ran on the TppMain selection thread. Native host creation remains disabled. Mother Base returned to title within 16 seconds and menu input worked. [Current trial](evidence/host-readiness-result-2026-09-09.json), [timing and next investigation](host-readiness-timing.md).

The preceding 0.9.4 four-slot version completed a load/return cycle, and 0.9.3 completed two two-slot cycles and quit normally. Removing the overlapping original controller resolves the observed transition failure in these tests. It does not prove a safe native networking lifecycle or independent additional characters. [Four-slot result](evidence/four-controller-replacement-result-2026-09-09.json), [two-slot result](evidence/controller-replacement-result-2026-09-09.json).

Installed DLL SHA-256: `E7EDC9A04B544607510F547759D9F5008A2E494344DCCA6438AD44279A66507B`. Archive SHA-256: `74BFCD3656356EE4B08EEBCB9912D4806BA0B9F7F948D0C86E6E412D6E930666`, 3,720,192 bytes, seven entries. All five four-slot runtime entries remain byte-identical to the pinned count-one source; the renamed Mother Base package preserves 44 definitions and its complete FPK partner. All 14 native suites pass. The combined host/trace insertion passes 16 exact-source assertions, 400 concurrent selections and 72 actual-module Lua checks. Package selection code is unchanged from the 0.9.3 checks. [Assets](evidence/four-controller-replacement-assets-2026-09-09.json), [current installation](evidence/loader-0.9.6-host-readiness-install-2026-09-09.json).

PID 14496, started 22:43:04 UTC, is at the title menu with Continue selected. No debugger was used. All seven save/config hashes match the pre-readiness backup, and the executable and Steam DLL hashes remain unchanged. Native host creation remains disabled. The prior PID 103508 quit normally at 22:42:26 UTC.

The previous 0.9.1 four-slot/count-one and 0.9.2 two-slot controls both reproduced the identical state-11 teardown wait: 13,419,280 bytes against 12,641,280, excess 778,000. Both games closed normally, and all seven save/config files matched. Restoring parts count and allocation scaling was insufficient to fix the wait. [Two-slot result](evidence/two-player-control-result-2026-09-09.json). Native host creation remains disabled.

The installed mission trace preserves yields, errors, arguments and nil/multiple return values; its exact-source checks passed 15 assertions and 400 concurrent selections, and its Lua fixture passed 213 checks. The new block dispatch analysis passes 27 instruction checks plus three jump-table targets. These tests and reserved storage do not demonstrate remote actors or playable co-op.

The earlier 0.8.0 selection validation passed all 14 native suites, 14 additional exact-source assertions, 400 concurrent selections and 73 actual-module Lua checks. The 0.9.0 reset checks above supersede that Lua count. A read-only query mapper resolves eight GameObject bindings and checks four paths; indexed-ID and type-existence implementations remain unresolved and were not called. [Local checks](evidence/player-package-local-checks-2026-09-09.log), [query mapping](evidence/gameobject-query-analysis-2026-09-09.json).

**Next engine milestone:** verify the native host/client allocation lifetime and player initialization path before enabling networking, then establish independent control of a second actor and expand to four. The four-slot scene now unloads successfully, but the separate native networking allocation-domain concern remains unresolved. Live Steam transport, authenticated peers, world replication, save isolation and campaign adapters remain unfinished.

The [current-build allocation analysis](host-allocation-lifetime.md) passes 63 instruction predicates and two deletion-table checks across 16 bodies. Host and client creation share the identified 88-byte manager and 144-byte job allocations; the context-sensitive allocator and compatible heap-header release branch are mapped. All 16 bodies also match PID 14496 after the readiness trial; mapped session and bandwidth-manager members remain null. No allocator patch or session call was made. Player construction and allocation now show a status-derived network path, supporting early setup as the candidate. Next finish persistent session allocation routing and asynchronous cleanup; concrete service virtual targets and a complete live host lifecycle remain unverified.

## Historical observations

The records below retain their original process/version context; statements such as installed, current, latest or paused below describe the recorded experiment, not the current running-game state above.

Latest live retry: **18:49:53 UTC, PID 91880**, natural script-loader entry and EAX=0 return captured, debugger detached without timeout. Computer Use verified the startup notice, title prompt and keyboard transition to the main menu at 69%. The game was left at the main menu. All seven save/config hashes matched the previous post-motion check; executable, Steam DLL and installed probe hashes also matched. [Retry continuation](evidence/script-load-retry-2026-09-09/continuation.json). This supersedes the earlier unresolved visual continuation for the retry only; the bridge remains uninstalled and co-op is not playable.

The [script-loading bridge](script-load-bridge.md) passes 2,010 fixture calls, including concurrent use, callback reentry and restoration after removal. All 14 CTest suites pass in Release. A natural game script load and successful parser return were captured separately before the installed trial; this does not demonstrate script replacement or multiplayer.

Latest asset work: extracted the installed two-player actor package and staged seven allocation changes for four players. Both definitions compiled and passed XML/FPKD/FPK round trips. The [allocation experiment](four-player-allocation-assets.md) is uninstalled; memory sizing and live actors remain unverified.

Source direction: the user authorized Dynamite inspection and reuse. The [source port](dynamite-integration.md) records pinned licenses, 25 incompatible upstream patches, and 16 current-build function candidates from a second source whose supported executable checksum matches the installed game. No new hooks or co-op gameplay are enabled.

Latest: the [original player-record observer](live-player-record-writer.md) captured a natural bank copy and two writes through updater `0x12CE870`, immediately after store `0x12CF266`. Expanded registers matched bank 1 record zero and source index zero. A 24-instruction static check maps its caller and source-array dataflow. Fixture capture/refusal/deadline/copy-limit checks passed; the game continued and was left paused. All seven save/config hashes match the preceding post-motion check. Actor ownership, lifetime and playable co-op remain unresolved.

| Area | Evidence | Limit |
| --- | --- | --- |
| Source reuse | Existing project-authored core and probe; Dynamite reuse authorized 2026-09-09; pinned Dynamite/tpp-mod references and inactive porting tools added with licenses | No derived game hooks or remote actors installed yet |
| Native build | MSVC 19.50, x64 Windows, C++17, Release, static runtime | Compilation does not validate game structures |
| Automated suite | All 14 CTest suites pass in Release, including the script-loading bridge, typed payload, host input receiver, Steam packet and natural getter forwarding suites | Not a multiplayer gameplay test |
| Session/protocol tests | 2,891 checks passed, including header mutations, invalid inputs, slot recycling, independent sequence wrap, mission barriers, stale objective events and held-input expiry | In-process coordinator only |
| Peer router tests | Admission, bounded queueing, separate peers, duplicates, reordering, wrap and disconnection checks passed | No Steam authentication, ACK/retry implementation or socket connection |
| Typed player payloads | Original versioned input and complete pose-roster formats pass 362 checks, including malformed values, slot/source binding and roster authority | No transport or engine adapter. Post-delivery helpers alone cannot roll back the router cursor |
| Host input receiver | 783 admission/rollback checks pass; invalid high sequences, stale connections/slots and Session rejections preserve live routing/input state and unrelated event queues | Trusted adapter and exclusive ownership required; bounded router copying has no measured production throughput. Pose/event admission is separate unfinished work |
| Steam packet adapter | 124 fixture checks pass; three shipped flat-export wrappers independently matched to public signatures, bounded reads and no-delay/reliable sends implemented | Inactive in the loader; no real Steam traffic, live context, channel ownership, authentication handshake or replay boundary verified |
| Natural Steam networking getter | Original 0.4.0 adds exact-DLL/export-gated passive pointer forwarding, bounded caller receipts, and 15,238 fixture assertions including repeated concurrent/reentrant scenarios; its import replacement installed with protection restored on a live launch | No interface pointer retained or used; natural getter execution and complete ownership remain separate evidence |
| Four-member lab | Four members admitted; fifth rejected; input loss/duplication and host loss exercised | No game actors or remote machines |
| DirectInput forwarding | Six exports tested against System32; menus responded, and original 0.4.0 accepted disembarkation, a dive and posture recovery at Mother Base | Controller and sustained locomotion remain unverified; short forward taps produced no measurable motion |
| Diagnostic installer recovery | Five isolated scenarios passed: install/remove, foreign loader refusal, changed loader refusal, missing-executable removal and partial-manifest recovery | Inert fixtures; no live game files changed by these tests |
| Actual MGSV loader execution | `probe.log` at 2026-09-09 13:01:52 UTC recorded version 1.0.15.4, successful `DirectInput8Create` (`0x00000000`), System32 forwarding and loaded Steam DLL | No gameplay hooks, network sessions, saves, mission observations or remote player control |
| Live Steam import observer | Version 0.2.0 installed the callback pointer with page protection restored; observed 3,600 original callback calls and at least two callback threads | No custom traffic, game function calls or Lua ownership inference |
| Import patch checks | Real fixture DLL: 4,001 forwarded calls across threads, original error-state preservation and restored import/page protection; guard/no-access and executable pages rejected | Public-import observation only, not a validated game-function detour |
| Steam runtime reader | Five official opaque/context accessor signatures, explicit lifecycle gating, no cached pointers; fake-export tests pass | Not connected to a live verified initialization boundary |
| Passive Steam initialization observer | Original 0.3.0 forwards boolean/error/exception results; tests cover 1,000 concurrent observations and bounded overlap. Both live import patches installed with protection restored | No natural initialization call observed through 3,600 callbacks; successful initialization and shutdown coverage remain unproven |
| Natural-entry debugger fixture | Capture, deliberate wrong-byte gate and no-hit deadline passed. Paired mode captured result `37`; nested fixture skipped an inner return. New ReturnTimeout passed with the return breakpoint armed: strict interrupt/detach, original result and continuation preserved. Nested regression also passed | Separate debugger harness; fixture behavior does not prove game semantics. Saved no-hit artifact covers entry-only mode; earlier paired no-hit pass was not separately archived |
| Live natural network registrar | First entry captured on thread `42464`; second launch captured entry and original-frame return on thread `91732`, skipped an inner return and recorded `EAX=1`. Known vars backing pointer present; CDB detached, and startup input/title screen worked afterward | Result count for one registration call, not a connection/player count. No VM lifetime or ownership proof, active game function calls or persistent game hook |
| Mission/player sampler | Original external reader verified build and selected live code bytes; startup scene changes and one Mother Base dive/posture recovery were observed | Actor identity, lifetime, publication timing and complete sequence mapping remain unverified. Positive mission code or nonzero coordinates do not imply playable state |
| Published player-status reader | Independently decoded two fixed banks and named status bits; original bounded reader sampled the title screen with alternating bank selectors and `SQUAT`/`PARTS_ACTIVE` set. Release build and independent formula review passed | These flags also occur at the title screen. No active-actor readiness condition or publication cadence is established; selector rereads are not atomic snapshot proof |
| Published position candidate | Independently traced named near-player-position use of record `+0x10`; live dive moved the vector approximately 4.78 horizontal units, and recovery changed height with posture | One scene-level correlation. Producer actor, coordinate definition and freshness remain unresolved; two position sources disagreed in five dive samples |
| Natural Steam initialization | Ten original fixture cases passed; desktop-account live launch at 16:28:34 UTC captured the game's call at RVA `0x1428` and same-thread/same-stack return at `0x142E`, `AL=1`, then detached successfully | One successful natural call. No installed early boundary, shutdown serialization, live SDK access or multiplayer verified |
| Independent Lua analysis | 23 network bindings and registration/helper flows decoded; ten semantic checks pass under the executable fingerprint | Registration timing, live VM ABI/lifetime and thread ownership remain unverified |
| Campaign register | 51 main episode entries, 12 story quest candidates, six incomplete coverage inventories | Every segment remains unimplemented/unverified; sequence discovery is incomplete |

Raw evidence: [native test output](evidence/native-tests-2026-09-09.log), [actual game probe log](evidence/loader-probe-2026-09-09.log), [live callback trace](evidence/steam-observer-2026-09-09.log), [offline executable inventory](installed-build.json), [independent binding analysis](network-binding-analysis.md).

Historical installation: diagnostic proxy **0.5.0** was installed at `D:/Games/steamapps/common/MGS_TPP/dinput8.dll`, with its ownership record in `mgs-coop/probe-install.json`. Installed DLL SHA-256: `B2E65778F134693C46E5C78011FCE8D39ADFA403782A9532498390322F7BF3B2`. The previous 0.4.0 build and manifest are backed up under `build/backups/probe-0.4.0-20260909T185815Z`. The executable and shipped Steam DLL hashes were reverified unchanged. [Current installation evidence](evidence/loader-0.5.0-install-2026-09-09.json).

Earlier Steam capture launch: 16:28:34 UTC, PID 76012, in the desktop user's Steam context. After the user explicitly approved the restart, the prior game quit normally. The original CDB observer captured the game's natural Steam Init call and matching return (`AL=1`), removed its breakpoints and detached, with exit code zero and no timeout. Separate Computer Use verification reached the title prompt and Enter opened the main menu at 69% completion; all seven save/config hashes still matched the prelaunch baseline at 16:31:39 UTC. [Continuation evidence](evidence/game-steam-init-continuation-2026-09-09.json). See [Steam capture](evidence/game-steam-init-2026-09-09.json) and [current control observations](live-player-control-observation.md).

The preceding 15:06 launch, PID 42824, captured natural network registration and its matching return (`EAX=1`). Startup/title input, Continue and iDroid responded. Return to Mother Base triggered an automatic cutscene; later controls were left with the user after Computer Use detected input. On-foot movement correlation and controller support remain unverified. See [live registration evidence](live-registration-observation.md).

The original and pre-control seven-file save/config sets remain preserved under `build/backups/save-before-observation-20260909T145101Z` and `save-before-control-20260909T152909Z`. Ordinary game autosaves changed `TPP_GAME_DATA1` at 15:04:45, 15:43:06 and 16:20:47; their semantic changes are unknown. Before the latest restart, the closed game's seven current files were copied to `build/backups/save-before-steam-init-20260909T162821Z`, with matching source-before/source-after/backup hashes. [New baseline](evidence/save-check-before-steam-init-2026-09-09.json). The tools have not overwritten original saves. These copies are not save isolation or tested restoration.

The CDB observer is a separate development launch tool, not an installed game hook. [Fixture trials](registration-debugger-trials.md) document capture, wrong-byte rejection and no-hit recovery using explicit `.detach /h`. Earlier rejected fixture detach attempts produced a Windows single-step error dialog; later fixture launches suppress their own error boxes. The first game observation launched under the isolated tooling account and failed Steam initialization; its debugger detached at the deadline, and that known failed startup was closed. Retrying in the desktop account produced the successful game capture above.

Field evidence includes [title samples](evidence/player-status-title-2026-09-09.jsonl), [helicopter/iDroid samples](evidence/player-status-helicopter-2026-09-09.jsonl), [Mother Base arrival samples](evidence/player-status-mother-base-arrival-2026-09-09.jsonl), and [published vector samples](evidence/player-vector-helicopter-2026-09-09.jsonl). The read-only sampler makes no game calls or writes. Arrival read mission `30050` and standing/normal-action flags during a cutscene; these remain insufficient as playability conditions. The separate Steam call-site composition is now [fixture-validated](steam-init-caller-investigation.md) and independently reviewed in the launcher; the later live trial captured a true Init return and verified title/menu continuation. [Typed codec/receiver format and limits](typed-payload-format.md), [latest shared build](evidence/native-tests-with-input-receiver-2026-09-09.log), and [individual test results](evidence/native-test-details-with-input-receiver-2026-09-09.log) document the portable work.

The [earlier Steam dispatch investigation](steam-early-entry-investigation.md) corrected the classification of pointer `0x20A0940`: it is an object virtual-method slot, not a CRT constructor entry. Its caller and object/Boolean ABI are mapped, but the current proxy has no deterministic outside-DllMain installation callback before that dispatch. The separate debugger now captured one successful natural Steam initialization; no new installed early hook is claimed.

## Earlier engine investigations

The [live Mother Base motion trial](live-player-motion-observation.md) correlated one visible Space dive and C posture recovery with the published vector and named status bits in PID 106392. Horizontal dive displacement was approximately 4.78 game units; recovery changed the sampled height with posture. Short forward taps produced no measurable displacement. Five dive samples disagreed between the two position sources despite stable selector rereads, so an atomic replication snapshot is not established. The reader now skips a changed-selector observation while retaining its finite capture limit; all 13 existing suites pass. The game is paused at Mother Base. Six save/config hashes still match the preserved baseline; ordinary arrival autosaved `TPP_GAME_DATA1` at 17:13:15 UTC. Actor identity, sustained movement, controller behavior and all co-op gameplay remain unverified.

The [live session-owner reader](live-network-owner-observation.md) subsequently followed the statically derived three-pointer owner chain in PID 106392 at 17:01:34 UTC. All five final samples matched constructor table `0x2223508` and observed state `0`/flag `0`, the constructor's initial values. This is a raw baseline, not proof of no transport or connection. The original optional `world_probe --network` reader performs bounded reads only and suppresses values on pointer/type changes; no game restart or writes occurred. The updated build and all 13 existing suites pass. The [extended static report](evidence/packet-manager-lifecycle-with-owner-2026-09-09.json) verifies 44 checks across 25 ranges.

The [native packet-manager investigation](packet-manager-lifecycle.md) now reproduces 39 static checks across 22 bounded ranges. Manager setup lazily creates a transport at `inner+0x18`, its constructor registers two Steam callbacks, and its update skips the mapped Steam availability call when that pointer is null. Two upstream construction routes are traced, but their Boolean activation predicate and concrete virtual dispatch to setup remain unresolved. This was file inspection only: it did not sample the pointer live, call game code, alter the installed loader or restart MGSV. The next observation must establish natural setup/update ownership; absent getter receipts alone do not establish a null transport. [Static evidence](evidence/packet-manager-lifecycle-2026-09-09.json).

The 0.4.0 live check subsequently loaded the existing helicopter through Continue. All seven save/config hashes still matched the preserved baseline at the recorded checkpoint. No natural networking getter receipt appeared through this scene check; the installed getter observer has not yet demonstrated live forwarding. Next investigate the independently mapped native packet-manager callers and their lifecycle, rather than inferring availability from an installed hook. [Live result](evidence/steam-getter-live-2026-09-09.json).

Latest launch: **16:41:22 UTC, PID 106392**, with original loader 0.4.0 and no debugger. All three import replacements (Init, callbacks and networking getter) installed with page protection restored. The game accepted the startup notice and title Enter and reached the main menu at 69%. The seven save/config hashes matched the preserved 16:28 baseline at 16:41:25 UTC. [Original getter design and exact callers](steam-networking-getter-observation.md), [13-suite build](evidence/native-tests-with-steam-getter-2026-09-09.log), [current loader trace](evidence/loader-probe-0.4.0-2026-09-09.log).

The [original Steam packet adapter](steam-packet-adapter.md) is now implemented with explicit borrowed-access gates and fake-export coverage, including three guest inputs reaching the real portable coordinator. [Latest build](evidence/native-tests-with-steam-packets-2026-09-09.log) and [detailed results](evidence/native-test-details-with-steam-packets-2026-09-09.log) supersede the 11-suite build above. The running MGSV process remained PID 42824 during this work; no UI input, live SDK call or loader installation occurred in this packet-adapter step.

Independently map a read-only mission/actor observation path in the exact executable. Use the offline inventory's data anchors to guide investigation; those string addresses are not callable function pointers. Record a verified build-specific function map and instruction checks before installing a hook. The first bridge should observe title screen, mission start, local actor state and transition back out, without modifying campaign state.

The [independent decoded analysis](network-binding-analysis.md) establishes that the network registrar builds a temporary 23-entry table on its stack. Its two inspected queries push Lua values and return a result count, and their shared accessor can lazily initialize state. Do not actively call these wrappers as diagnostic queries. No-hit recovery, the live VM-dispatch caller, and the paired natural registrar return are now observed. Next correlate [published player-status records](player-update-investigation.md) and [named player fields](player-observation-candidates.md) with a real loaded actor and transitions. A [natural Steam initialization call site](steam-init-caller-investigation.md) is now independently mapped for an earlier observation. The Steam callback pump remains unsuitable as an assumed single Lua-owner thread.

## Historical implementation order

1. Verify title-screen/input behavior with the installed original probe.
2. Port the authorized Dynamite engine paths using current-build source maps and existing observations. Verify safe update, Lua/actor ownership and build gates before enabling the bridge.
3. Prove Steam context ownership, compatible interfaces, authenticated peer identity, callbacks and a non-conflicting transport path using two real installations.
4. Prove a second fully interactive campaign actor with collision, equipment, damage and world synchronization.
5. Extend engine allocation, replication and ownership to three and four active actors. Test disconnect while remaining peers continue.
6. Build interaction reservations, enemy/AI authority, revive, shared extraction, cutscene/transition barriers and a complete checkpoint baseline.
7. Prove save isolation, host campaign ownership, reward transactions and interruption recovery before enabling campaign persistence.
8. Author the hospital opening adapter and remaining exceptional sequences. Verify each main mission and required story dependency using the campaign register.
9. Complete a fresh-to-ending four-player campaign with recorded evidence, then test replay, reconnect, difficult network conditions and recovery.

There is no release date estimate yet: the four-actor engine proof determines the scope of native work. Steam lobbies do not create actors or synchronize campaign scripts. Authorized Dynamite and current-build source reuse now accelerates the engine port; each integration still requires exact-build and runtime verification.

## Historical missing behavior

This describes the earlier probe/coordinator stage. The current offline transport, remote bodies and copied-save route are recorded at the top of this page.

No real networking transport or Steam lobby is connected. The installed script-load hook and bootstrap do not provide remote actors, runtime message dispatch, full snapshots, late join, campaign save redirection, mission adapters, voice chat, host migration or playable co-op UI. The portable host input receiver is groundwork for runtime integration.

The co-op release must not substitute spectators for playable hospital/ending segments or advertise passing development tests as campaign support.




