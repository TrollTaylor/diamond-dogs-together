## October 11 revive, crawl and testing controls

The candidate is 0.12.3. The newest user-session logs showed the healthy teammate repeatedly refusing a changed native identity, so no held-revive packet reached the host. A healthy poll now coherently retires the stale lease and creates a fresh generation without restoring health through the old pointers. Active downed ownership and stale-grant rejection remain strict. Both actual Lua roles pass the generation-change/held-revive regression.

Private hidden run `f21ee5e3a27b4b61842eda78a47b092d` used actual host gunfire, host sleep darts and guest sleep darts; all three down/revive cycles passed, including reciprocal ACTION revives. It predates the slower crawl and horse helper. Both installed loaders were restored, real saves compared unchanged and no desktop switch, focus or system input occurred. [Exact evidence](evidence/revive-repair-live-2026-10-11.json). Natural healthy pointer replacement was diagnosed in user logs and exercised in fixtures, not reproduced naturally in this lab run.

The first native crawl-record candidate reported rate 500 but did not reduce physical travel. The subsequent exact native input hook runs the original update first, then caps only the freshly computed owned downed body's movement magnitude. In hidden run `39f6a904dc704ce799a1a1fb038788bb`, established local crawl measured Quiet 1.713 / 0.873 / 1.704 m/s and Snake 1.534 / 0.912 / 1.701 m/s before / while downed / after revival. Actual gun and reciprocal sleep-dart revives passed, as did cap and next-update reset readbacks; weapon, aim, reload, stance and dash inputs remain blocked while downed. Snake's remote crawl followed closely; Quiet's replica ran faster before corrective nudges, so precise remote slow-crawl parity remains unfinished. [Physical positions and timing](evidence/downed-input-actual-39f6-2026-10-11.json).

F9 Partner / Co-op provides downed, revive and movement-input readouts plus normal-release Summon / recall D-Horse, Horse status and D-Dog follow status. Run `39f6a904` visibly established a nearby horse on both copies, including the stock E mount prompt; the guest horse was already present before recall. The host had released its Dog before that request, so this does not qualify switching the guest's borrowed Dog while the host retains Dog. No mount was performed and remote rider attachment/two-player mounting remains unfinished. The helper uses the stock local horse four metres beside the player; selection and available capacity are not claimed as visible arrival.

The later actual-enemy phase of `39f6a904` failed: the host's scoped lethal damage was intercepted, but initial crawl controls remained pending beyond the old 1500 ms deadline, then the game took its normal shared-failure path. The guest stayed healthy with the diagnostic auto-heal aid; this was not both players down. A bounded six-second first-acquisition grace and distinct stance/mask/crawl diagnostics are now staged for actual validation; loss of an already confirmed control lock retains the original 1500 ms deadline. The hospital opening's exact rail animation remains unavailable. All 63 pre-task protected file hashes/absences matched after cleanup, including real saves and both normal installations.


## October 10, 2026: local two-window guest launch

The local test used package `mgs-coop-friend-0.12.1-20261010T005201Z-bdab9994`. That package is older than the 0.12.2 source in this tree. Choosing T printed Host on the left, Guest on the right, then failed with `[guest] The launched process did not match this game copy.`

Windows PowerShell 5.1 often leaves `Process.Path` empty on the first read after the GUI launch. The image path of the process the launcher started is still that copy's `mgsvtpp.exe`, including when the guest file is a hardlink of the host file. On this PC those two executables are one file (two links, one file index). The launcher still requires the process id, the start time, and an image path that is this copy's `mgsvtpp.exe`. A path from the other directory fails even when it names the same file. An empty path fails.

Current launcher package: `C:\Users\taylo\OneDrive\Documents\ChatGPT\mgs-coop\dist\mgs-coop-friend-0.12.2-20261010T195437Z-a0668e6a`. Zip SHA-256 `919E60F64EE665D51E5AC37B4739C63D85DBF32255FBCA9F053ED413CA8812AA`. Open that folder's `Play-Coop.bat` and press T. A checkout launcher that prints `Using package` selects this folder because its preparation time is newest. Release receipt `build\worker-runs\20261010-195230-d3c2cfdc8b0e48d6b0d89d4b9f0ce617\receipt.json` passed 137/137, including `local_coop_windows_tests`. That fixture passed 188 checks under Windows PowerShell 5.1 and PowerShell 7. The package is the dirty 0.12.2 tree at commit `2a0ee694d32e078203a365cedb589f50b5f15718`, loader SHA-256 `391AAF521C2950AD6BEC9FCE4672CC3361CF744C4A563A18E99C377B178E352C`. The live game folders were left unchanged. MGSV was not launched, so the two-window desktop session is still unchecked. Pressing T later updates both owned loaders to this package before the games start; that install step was not run here.

## October 10, 2026: downed, buddy follow, and ride-capacity continuation

The uncommitted work in this worktree already contained the downed/crawl/revive path, friendly-hit forwarding, D-Dog host follow, ally-targeting HUD, and the Play-Coop exact-release loader check. This session did not re-run a hidden game. Prior private-desktop evidence remains `docs/evidence/gameplay-downed-dog-live-2026-10-10.json` (run `7169753d2478483e9ec5b1ab93ad58c4`): friendly gunfire downed the guest, crawl stayed locked, E-hold revive worked both ways, sleep darts downed both directions without a crash, and the guest dog settled about 1.48 m from the host dog. That receipt explicitly did not exercise enemy lethal damage, both-down checkpoint recovery, or a second horse/walker. Mounting stays disabled (`rideMounted=false`).

Fixture fixes landed here, still not live proof:

- `worker_session_tests` had kept refusing `host worldtrigger world=0` after the lab runner intentionally admitted a host or guest `world=0` cleanup step. The fixture now admits that exact lab step for each role, still refuses `both`, a malformed value, and play mode, and checks the dispatch passes the real role.
- Walker census now requires the final factory types `0x2442920` / `0x2442960` from retail `mgsvtpp.exe` SHA-256 `085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45`. The construction-only pair `0x2442660` / `0x24426a0` is refused before any index-1 record read. The 88-byte factory at `0x17033b0` is an additional code pin.
- After a typed horse or walker census, controller+0x38 is read twice and must match one verified main-pack hash (`0x2a74960` horse table, `0x2a74a88` walker qword, copy site `0xa4428b`). The receipt names that pack. Zero, unknown, or a changing hash refuses. Naming the pack does not allocate a second body or enable riding.

Research Release cache `D:\mgs-coop-wt\integ5\build\worker-checks\research-7A96A2054190-worker-on-release` rebuilt with the existing worker/equipment flags and passed **140/140** CTest checks, including the previously failing `worker_session_tests`, both buddy-capacity on/off fixtures, `world_buddy_ride_lua_check`, and `friend_package_tests`. That is fixture evidence only.

Pinned capacity archive is now the scratch file `mgs-coop-buddy-capacity-variants-v2.dat` (17,501,184 bytes, SHA-256 `4b290cddc14f1146dce5a64a1008365d572b424b319874ef77e9496634d75ec9`). Its `catalog.json` is SHA-256 `4a7e7ea890adf8f261368cd546ccfd3dbfdc4407ef6af44aff308b98e53d0989`. The worker profile token stays `buddy_capacity_two_v1`. The file overrides these FPKDs, each authored to total/realized count 2 with a second locator 4 m away. For the six horse packs added beyond variant 00, only the FOX2 bytes differ from the retail pack:

- `buddy_horse2_00.fpkd`, `03`, `02_0`, `02_1`, `02_2`, `05`, `04`
- `buddy_walkergear/buddy_wg2_00.fpkd`

The walker selector at `0xa468c7` loads the qword at `0x2a74a88`, which is the Fox path hash of `/Assets/tpp/pack/buddy/walkergear/buddy_wg2_00.fpk` (extension id `0xa45`). It is not `buddy_walker2_00.fpk`. The package binder at `0xddd00` keeps that hash and writes the same path with the FPKD extension (`mask 0x7ffffffffffff`, then `or 0xed50000000000000`). That companion is `buddy_wg2_00.fpkd`, already overridden in the pinned archive. The seven horse selector hashes are the matching `.fpk` files, and the same rewrite names the overridden FPKDs. Two realized bodies and per-player mounting are not verified. `rideMounted` stays false. `tools/stage_buddy_runtime.py` still builds only horse 00 plus walker and now labels that result `buddy_capacity_variant00_rejected`. The native mount refuses a 16,659,456-byte file before any other archive profile is considered.

Enemy lethal hits and both-down checkpoint restart are implemented, not live-proven. A scoped local hit whose damage is at least current life calls `CaptureHit` with cause 1, zeroes that killing apply, and the next owner poll installs prone, the crawl pad mask, and protected health. Non-lethal enemy damage is left to the game. When both authenticated players are down, the host calls retail `ReserveGameOver(PLAYER_DEAD)` and sends that game-over to the guest, which calls the same retail function. That is the existing shared-death reload, not a new checkpoint write. No hidden run has shown an enemy bullet completing that down, or both players actually reloading the checkpoint. Prior live evidence above does not cover either case.

The same research Release cache then passed **140/140** CTest checks after the walker-pair note, the explicit refusal of the 16,659,456-byte archive, and the staging-script label change.

Guest horse selection no longer treats an active D-Dog follow as ownership of the horse slot. While the guest dog is still the active buddy, `buddy_select_horse` or `buddy_select_walker` yields that selector without calling dismiss, without recalling the dog, and without disabling host follow. A refused selection restores the borrowed dog. Dog and none selections still cannot replace that borrow. `world_buddy.lua` stays under the existing 16000-byte chunk limit. Research Release cache `research-7A96A2054190-worker-on-release` passed **140/140** again after that change.

Hidden census run `36b4a595c3af46cca78313b32a03220a` (fresh probe `5011cc54639d49008cbde1b0f33cfef0`; no desktop switch, focus, or system input) used the same pinned 17,501,184-byte archive. Before the guest selected a horse, the host buddy was still the dog and the guest was submitting that dog's target. The guest selection returned `selection_submitted_horse`. Fifteen seconds later the guest horse census was `k1 a2 r2` id 6657, unoccupied, pack `03`, local ride id 65535. The host horse census stayed `select_ride_first` because the host buddy was still the dog. The host status after that census was still `kdog` and follow was not disabled. No ride command was sent. `rideMounted` remains false in the capacity receipt. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D`. Real saves compared identical. The owned job was force-closed after the shutdown request; isolation did not fail. This is still a typed census, not a ridden-mount proof.

Ride admission is now a separate gate. `buddy_ride` calls the existing horse `SetCallHorse` or walker `SetPosition` only after a typed census is two allocated and two realized on a real pack token. One body, a missing pack, `pack_unknown`, a refused census, or both riders resolving to the same actor returns a refusal and does not call that command. The host is actor 0 and the guest actor 1 unless an existing ride id already takes one of those bodies; the other player then gets the remaining body. `rideMounted` stays false unless that census also shows the local ride id on an occupied body whose rider is not also the other body. A failed call clears it. The mount itself is still the game's ACTION key. Research Release cache `research-7A96A2054190-worker-on-release` passed **140/140** after the gate.

Hidden ride run `a5c5b73b3932489990cfa513e0275824` (no desktop switch, focus, or system input) used the same pinned archive. Both horse censuses before any ride input were `k1 a2 r2`, ids 6656 and 6657, both unoccupied, pack `03`, `mounted0`. The host was admitted to 6656 and the guest to 6657. Each then received one ACTION press. Eight seconds later both `buddy_census` commands returned `on_foot`: the debug guard only accepts stand, squat, or crawl, so it never read the occupied riders. Walker selection was not started. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D` and real saves compared identical. No second mount call was attempted.

`buddy_census` now also accepts retail `ON_HORSE` and `ON_VEHICLE` after the on-foot check fails. Other debug commands still require stand, squat, or crawl. A mounted census still returns the ride module's own refusal when the count, pack, or admission is missing. Research Release cache `research-7A96A2054190-worker-on-release` passed **140/140** after that guard.

Hidden ride run `ed3c1cd49a934c4b8327d085d29ccd01` read the horses after one ACTION each. The host census was body 6656 occupied by rider 0 and body 6657 empty, `localRide6656`, `mounted1`, pack `03`. The guest census was body 6656 empty and body 6657 occupied by rider 0, `localRide6657`, `mounted1`, pack `03`. Each player occupied only their own assigned body. The other body stayed empty, so one census did not show two riders. Walker selection was not started and no second mount call was made. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D` and real saves compared identical.

The later census display inferred a peer rider from channel 2's partner ride kind whenever exactly one local body was occupied. That heartbeat does not establish a native rider or remote attachment on the empty local body. The display inference was misleading and is removed by the October 11 corrective patch: native actor rider/occupancy fields are reported exactly, with the local native mount result labeled `localMounted` and channel 2's kind labeled separately as `peerWireRide`. The earlier **140/140** fixture result had asserted the inferred display and does not qualify remote mounting.

Hidden ride run `f6a2b54ceba14d25808dac369d0a6077` displayed two occupied horses on each copy after one ACTION each: host 6656 rider 0 / 6657 rider 1, guest 6656 rider 1 / 6657 rider 0, with `remote3`. The peer entries were synthesized by that Lua display inference, not read from native rider records. This run therefore does not prove two native riders or remote player attachment. The earlier `ed3c1cd49a934c4b8327d085d29ccd01` native census showed one occupied local horse and one empty horse on each copy. `buddy_select_walker` returned `on_foot`, so the walker pair was not selected. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D`. Real saves compared identical. The owned job was force-closed after shutdown; isolation did not fail.

One later ACTION each dismounted both horses. Hidden run `f145f79fbe2d4156a35a8a30bb317643` then selected walkers: both censuses were `k2 a2 r2`, ids 22528 and 22529, unoccupied, pack `00`, `remote0`. The host was admitted to 22528 and the guest to 22529. After one ACTION each, the host census occupied only 22529 (rider 0, `localRide22529`, `mounted1`) and left 22528 empty. The guest census occupied neither body (`localRide65535`, `mounted0`). Both lines stayed `remote0`, so the channel-2 ride kind never became vehicle or walker and the empty body was not filled. No second mount call was made. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D` and real saves compared identical.

The private capacity `buddy_ride` placement parks an unused Walker body 40 m away and places the admitted body 2 m beside the player with a 3-number `SetPosition`. The October 11 correction refuses an occupied, non-null-rider, or nonzero-state other body and compares the same qualified native fields twice before either placement; it reads them again after parking before the second command. These placement commands do not establish a mount or remote attachment. The normal release producer uses only the established public `ON_HORSE` / `ON_VEHICLE` / `ON_HELICOPTER` statuses; the unqualified no-argument ride-id getter was removed. The earlier **140/140** fixture result is retained as historical evidence and does not validate the correction or remote mounting.

Hidden run `6e789152c12e4745941383ff3b0e59e9` still admitted those two bodies, then one ACTION each left both `localRide65535` and both walkers unoccupied (`remote0`). The host's other walker was at `p842.2` and the guest's admitted walker at `p802.9`, so the placement ran, but the mount did not. Stop restored both loaders to `E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D` and real saves compared identical. The owned job was force-closed after shutdown; isolation did not fail.

The walker arming call is not in the verified dispatcher. Retail `mgsvtpp.exe` SHA-256 `085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45` compares exactly 16 Fox StrCode64 command ids in `TppWalkerGear2` Lua dispatch `0x1706190`–`0x1706cda`. They are `ForceStop`, `GetPosition`, `GetResourceId`, `IsBroken`, `IsEnemyRiding`, `IsFultonCaptured`, `IsPlayerRiding`, `NoRideOn`, `SetColoringType`, `SetCombatGrade`, `SetExtraPartsForSpecialEnemy`, `SetIgnoreDisableNpc`, `SetMainWeapon`, `SetMarkerEnabledCommand`, `SetMaxLife`, and `SetPosition`. `SetCallHorse` is not among them. `SetPosition` remains the only placement command and does not arm a chosen body for ACTION. No further hidden ride was run.

The pinned horse rider handler `0xe59c20`–`0xe59e19` (505 bytes, SHA-256 `9e35a6fc455a55bb3ffb7f393eca1822842978c7e713f8b1b2739bc83074bce1`, same executable) is the function that records a horse mount. It accepts only event `0x5167c60042b5`. Mode 0 stores the rider at the horse record `+0xe` and sets occupied `+0x2dd`. Mode 1 is the other horse path. It does not write the player ride id at PlayerInfo `+0xb6`, and it has no walker or vehicle branch and no compare against walker ids 22528 or 22529. Two Walker bodies were read as realized in private capacity tests, but per-player Walker mounting and remote attachment remain unverified. For horses, `ed3c1cd49a934c4b8327d085d29ccd01` read two realized bodies and one local mounted rider per copy; the peer body remained natively empty. The displayed peer riders in `f6a2b54ceba14d25808dac369d0a6077` were inferred from the wire kind and are not native attachment proof. These capacity and placement experiments remain default-off in normal releases.

Hidden census run `661d26d0e3d348ffa1b12f4724b764a0` (fresh private-desktop probe `f158702d7a2f4554bc6e3620b4985dff`; no desktop switch, focus, or system input) mounted the pinned archive on both copies (`horse_fpkd=00,03,02_0,02_1,02_2,05,04; walker_fpkd=wg2_00`). Host horse census was `k1 a2 r2` id 6657, unoccupied, pack `03` (the save's outfit, not variant 00). Host and guest walker censuses were both `k2 a2 r2` id 22529, unoccupied, pack `00`, at different positions. The guest could not select a horse (`dog_borrowed`) because its D-Dog follow still owned that slot, so there is no guest horse count. No ride command was sent and `rideMounted` stayed false. Stop restored both owned loaders and recorded real saves unchanged (no added, removed, or changed save files). This is a typed census, not a ridden-mount proof.

## October 9, 2026: manual local two-window test in every friend release

Every newly built friend package includes **T: Local two-window test** in Play-Coop.bat, plus Play-Offline-Coop.bat as an alias. The source checkout's launchers select the newest extracted friend package by its preparation time. Local play reuses the existing sibling guest folder, or asks for a separate second game folder, checks both owned loaders against the release, and starts both games without title/menu input, Continue, mission selection or teleports. Keyboard/mouse controls the clicked game; controller 0 belongs to the guest.

Each copy has persistent mgs-coop/local-test-saves, seeded initially from the main co-op copy. Only those copies receive windowed graphics settings. Monitor working-area and DPI-aware layout fits side by side when possible, or vertically on narrow displays, handles startup window recreation and bounded game resets, respects manual placement, and offers **R** to arrange again. Window mutation checks exact process identity and uses no activation or z-order changes. A per-folder ownership pointer, paired hash journal and hidden cleanup helper restore each folder's own activation baseline after both games exit. Launch failures close only exact owned processes; active local ownership blocks other play/install/uninstall operations.

Validation: **1,194 friend-package checks** passed using Windows PowerShell 5.1 and PowerShell 7, native INI parser and standalone no-window fake process. Local session fixtures passed **114** checks and layout/graphics fixtures **172** checks separately in each PowerShell version. The source forwarding wrapper passed inert newest-package, parameter and exit-code checks in both shells. Pure worker policy passed **107** checks. Future Release checks now include both local suites; fresh Release receipt D:\mgs-coop-wt\integ5\build\worker-runs\20261010-005034-f55c52571cb141e98184c5895d24aee2\receipt.json passed **120/120** audited tests. No retail game, visible game window, Computer Use or system input was used for this launcher task. Real window placement and manual gameplay remain to be checked by the user; local loopback does not establish Steam relay or fresh two-PC acceptance.

Current launcher bundle: **0.12.1+af78399**, source implementation commit af7839977e668e2398133e76ecc397641015229b; DLL SHA-256 E4C62745EB63667C513D1A60C8646B4672C8A749DDB9E09F5E1148919BA09E1D. No native or Lua gameplay source changed. This bundle supersedes the earlier 0.12.1+575392b package for the launcher feature; both PCs must use the same new bundle for online play.

ZIP: C:\Users\taylo\OneDrive\Documents\ChatGPT\mgs-coop\dist\mgs-coop-friend-0.12.1-20261010T005201Z-bdab9994.zip; SHA-256 AAC60045A0BFFEE757A1D469D361CC3C340358371E054A8F82F27E68CBC10B89. Both existing game copies were upgraded to its owned loader without launching them: host rollback bee11aaaf41a4022a07aa89162510f9d, guest 44339d1a81584625afa151507ec752d5. All **45 current real/co-op/lab save files** are unchanged, none added or removed; original EXE/Steam DLL/runtime archive and both activation baselines are unchanged. Evidence: D:\Games\steamapps\common\MGS_TPP\mgs-coop\local-launcher-update\before.json, after.json and entry-checks.json. Game-folder Play-Coop.bat and Play-Offline-Coop.bat shortcuts now point to the same current release flow. Existing Claude files are preserved; nothing was pushed.

## October 9, 2026: 0.12.1 free-roam repairs (hidden checks completed)

The user reported a successful two-PC Steam session on 0.12.0, with invisible partner weapons, occasional movement corrections and Mother Base/Africa failures. The host-side logs and Claude's stopped `real-play-fixes` workflow were recovered. Guest-side logs from that session are unavailable. The repairs below passed the recorded private-desktop checks; fresh two-PC acceptance remains required.

Integrated changes: strictly admit the three verified retail Mother Base layout pairs while continuing to refuse player-controller variants; preserve compact pack-state receipts after verbose log caps; ignore only a paired nonlocal puppet's death request; use sender clocks and ring generations for reconciliation, one-shot jump placement, bounded dash relatching, effective-mask-aware HOLD speed parity and masked-stick neutralization; recover weapon loading across body generations, allow gated shared stock and closest filled-slot stand-ins, and update equipment independently of F9; append negotiated v6 gun-pose direction with stance-specific aim pivots, camera/sight refusals and a convention checker. Native thread warmup is retryable rather than a hard thread failure.

The ride/air/dead hide policy uses authenticated channel-2 ride heartbeats and stable on-foot placement before realization. After the Africa and shared-gun qualifications, the reviewed candidate defaults to hide for driven native loco/gun stages in lab/play and POINT for driven native gun aim, while preserving explicit off/look/hide settings. Play-mode default activation and movement passed the impaired-link rehearsal below; actual heartbeat-expiry/outage recovery remains unqualified. Measured optional-render acceptance is lab-only and off by default. No reload-speed patch or unqualified native raycast was introduced. The Release loader passed all 118 audited checks and is ready for friend packaging.

Latest checks: Release/worker-OFF receipt `20261010-000054-6719eefcde0d4f7fa4f756b827ecdcef` passed all 118 audited checks with loader SHA-256 `51CFF1038B53C885313DAADF4B9EAA2C388242513A4F41BB35323B24D1F4FBAD` and 528 exact source hashes. Normal/worker-ON receipt `20261009-232102-6945de7cb9e0480eadd85252009984ea` and Research/worker-ON receipt `20261009-232258-f0737948832947f3a49e6e0ba2b5b7c1` each passed all 118 audited checks. Friend-tooling fixtures passed 1,058 checks (including a real owned-junction source-bundle regression) under Windows PowerShell 5.1 and PowerShell 7; worker-session fixtures passed 668, worker policy 89, and detached loading/restart/disembark fixtures 144. The complete launcher/rollback suite passed 1,586 checks on the updated defaults.

Gun run `34fbb8de4e3846ed9a360a2730f7b954` verified both roles loading shared stock 39 through tier cs (refs 3 -> 4, fresh bank), all three slots bound, and another three fresh bindings on each role after Continue into generation 4. Both remote reloads completed in 3020 ms. All 84 captures had fresh snapshots, with zero recorded shot/ammo leaks, slot/magazine faults or ownership disarms. Actual firing selected slot 1 (873 -> 46), so this run does not qualify shared stock 39 in hand. Fresh active prone POINT aim differed by 14.88 degrees: its extra plugin rotation is unqualified. Sender and receiver now refuse prone POINT and use the existing LOOK fallback; exact prone gun-direction parity remains unfinished.

Shared hip run `77d0f9183dd14ccfb837d4c1b99f1c41` qualified actual selected slot0/hand0 with remote 871 -> shared substitute 39/class1 and HUD `gun substitute` in both directions. Tier Cs references 3 -> 4 used a fresh model bank. The receivers accepted/suppressed 12 and 13 shots, kept viewer ammo unchanged and completed/reconciled both reloads to 31/31; all 152 state samples recorded zero leaks, ownership disarms, slot/mag/store faults or refusals. Standing POINT error was 0.00/0.12 degrees; crouch 0.03/0.11-0.12 degrees. All six prone frames used fresh LOOK with no transported gun direction, and both source logs recorded `prone_frame`. Replacement timing remains approximate (sources 17/18 shots, source reload 985/1482 ms, partner 3019 ms); existing model-bank sharing is unqualified. Normal shutdown restored both installs and real-save hashes.

Travel run `ce44e7e13bf549afa5350b496d15f1f7` reached Africa but failed closed on Windows commit-limit error1455 before landing. Low-graphics retry `9de777cd06fc43efa3570b733fabe812` reached Africa without that resource failure, with both native bodies hidden during helicopter deployment and no fallback latch. It stopped before landing because the test omitted the normal E disembark action, visible in its private capture. The guarded wait now permits bounded E only for fresh owned helicopter state in a supported field mission. Fresh low-graphics run `e40e03fb49f5475f9bb626ac9b86ae6d` then completed Mother Base -> ACC -> Africa into generation 5, landed both players and captured walking in both directions: native active, zero recorded fallback/snap events, matching crouch-walk act 4 and at most 244 mm following error. Guest rain readback matched 17 ms after the host re-force and later checks matched, with clocks settling to 2-3 game seconds; rain was already active, so this is not dry-to-rain evidence. All three closed runs restored installs/configurations and verified real saves unchanged. The optional low-graphics setting affects only fresh worker save copies.

Mother Base run `751184586fcf49cf816c13df29f3a2d2` selected `f30050_ly003` on both roles and paired epoch 3 at effective world level 3. All six held walking captures had fresh in-hold movement, matching native acts, at most 255 mm reconciliation error and zero recorded snaps/fallbacks. Guest weather changed sunny -> cloudy -> rain on resync, then rain -> sunny on the host command. Evening time 64800 reached the guest in 34 ms; later clock checks differed by 2 game seconds. Cleanup required the owned job's forced close, with no isolation failure and both baselines/real saves verified. An earlier closed run `6d4704e3e80143a38f277a1d8c6a3243` stopped because the generic intro-skip sequence left a checkpoint confirmation menu open; its fresh idle reading was correctly rejected by the motion check. The passing repeat included a coordinator private-frame inspection pause, observed normal gameplay and required no menu input.

Play-mode run `3d5831c1ad374f2d8228bb102a37822f` used the final defaults without overrides, a copied lab save and private loopback with WAN impairment: RTT 176-185 ms, jitter 5-15 ms and observed loss 7-34 per mille. Both roles stayed native active/ok across all 43 captures, with no fallback latch, hard/network leave, deferred fault or shot/ammunition leak. Default HIDE produced initial ride_stale hiding and foot_stable placement; default POINT was selected, and admitted aiming used the owned point slot. Sprint rates matched at 10021/10022 mm/s with 282 mm error; the opposite-direction movement captured 3688/3768 mm/s and 92 mm error. Each role recorded one far_idle snap during setup/repositioning. The intended pause/outage steps were ineffective: pose/input ages stayed 0-45 ms and ride heartbeats continued, so no heartbeat expiry or outage recovery is established. Normal cleanup restored both installs and verified real saves unchanged.
Sprint/turn run `9e5e9e1e44a14fe5a885a09a78a78372` used the final native-loco defaults. All 26 captures were fresh; 643 unique movement/stop ring rows stayed native active. Adding Shift and separate left/right changes retained sprint class in both directions. Steady speed ratios were 0.9979/0.9809; after the turns, 1.0056/1.0064. The two movement windows recorded zero ring warps/snaps or fallback latches, five bounded nudges each and at most 409 mm error. Turning velocity parity is still approximate: the guest-driven right turn briefly reached body/source 14.134/5.201 m/s (ratio 2.718); ratios above 1.25 with source at least 5 m/s lasted 172 ms before recovery. The 800 ms stop diagnostics failed because both source and body were still decelerating (1.683/1.727 and 1.652/1.749 m/s) despite zero held axes/magnitude. No live dash retry was triggered, so the bounded retry path remains fixture-qualified. Settled source/body speeds were 11/12 and 11/15 mm/s with 194/72 mm error, satisfying the scenario's settled-stop bound. Cleanup required owned-job forced termination only; isolation checks passed and both baselines/real saves were unchanged.
Packaging now compares the fresh Release receipt's exact source snapshot and loader hash before and after staging; stale receipts and unchecked friend-loader overrides refuse. The shipping defaults and runtime loader are checked. Both owned game installs were upgraded with all 42 real/co-op/lab save files unchanged, no files added/removed, retail EXE/Steam/runtime-archive hashes preserved and the host left inactive. The first friend bundle omitted checked MinHook source behind the worktree junction; packaging now explicitly includes that already-hashed dependency, deduplicates source paths, and verifies source/manifest/ZIP bytes in the owned-junction fixture. That first `20261009T234746Z-91a3114f` bundle is superseded; the corrected bundle uses fresh Release receipt `20261010-000054-6719eefcde0d4f7fa4f756b827ecdcef` (118 checks). Its DLL differs from the first Release by 18 bytes: only the verified COFF/debug timestamps and two build identifiers (`0.12.1+e868757` -> `0.12.1+575392b`); every other DLL byte matches after normalizing exactly those fields. Both installed copies now match this final identity. The final artifact and upgrade verification are below.

Final handoff: friend build **0.12.1+575392b**, clean package source commit `575392b689f04585cee09149e3afee6e03f03822`, Release receipt `20261010-000054-6719eefcde0d4f7fa4f756b827ecdcef` (118/118), loader SHA-256 `51CFF1038B53C885313DAADF4B9EAA2C388242513A4F41BB35323B24D1F4FBAD`. Friend fixtures passed 1,058 checks under Windows PowerShell 5.1 and PowerShell 7. The final ZIP includes all 18 checked MinHook dependency files exactly once, with verified source/manifest/ZIP entries; no runtime archive or link secret is bundled.

Package: `C:\Users\taylo\OneDrive\Documents\ChatGPT\mgs-coop\dist\mgs-coop-friend-0.12.1-20261010T001133Z-1f94b83c.zip`, SHA-256 `F48280F02282F6B9AFB6093091CFE8D33F3C4AD716F82D0BD4E7CD070AFF2775`. Use that extracted folder's **Play-Coop.bat**; both PCs must install the exact same final ZIP. The earlier `20261009T234746Z-91a3114f` 0.12.1 bundle is superseded and has a different HELLO build identity.

Final owned upgrades: host rollback `2d9be465e94846ba92122673335ae284`, guest `8a90fb7f6c274a88bafff1fe35c97e7c`. Both installed loaders match the final Release hash. All 42 saved files are unchanged, none added/removed; retail EXE, Steam DLL and local runtime archive hashes are unchanged. The host remains pass-through, existing co-op/guest-lab saves and the first real-save backup are preserved, and no game or worker remains running. Detailed final receipt: `D:\mgs-coop-scratch\codex-fix5\upgrade-after-final.json`; PE comparison: `release-runtime-diff.json` in the same folder. Work is saved locally on `dev/oct7-continue`; nothing was pushed.

Remaining acceptance: fresh two-PC Steam play using this exact ZIP, including both players' Collect-Logs results. Exact prone gun direction, replacement fire/reload cadence, turn-velocity/800 ms stopping parity and a real heartbeat-expiry/outage recovery run remain open. Mounted bodies and safe native climbing are not implemented; ride/travel is handled by hiding and verified return to foot. Native name-tag raycast occlusion remains unqualified. Story scripting, item/progression synchronization and four players remain deferred under AGENTS.md.
# Development status

## October 9, 2026: weapons3, reload exit, gun-type base ids, tier C writer, early manifest (fixture-tested, hidden runs pending)

`wp/weapons3` (off `integ/final` 0.12.0) fixes what hidden run 53e36944
showed ([native-puppet.md](native-puppet.md) "Weapons"); no new game code
patch, every new read pinned:

- **Reload (F1)**: body 1's reload was never stuck; it exits natively after
  3.02 s (its own playback rate). `partner_reload` now reports stuck only
  after 8 s and stays `entered` with the mirror frozen until the native exit;
  every exit counts (`complete`, `complete_late`), the mirror reconciles once
  after it, and a remote reload started meanwhile gets one queued tap (with
  the reconcile, so body 1's magazine is below capacity). Telemetry
  `reload.len_ms`, `remote_len_ms`, `refill_ms`. A lost exit (body 1 unread
  or wedged in 11/12) is released after 20 s (`partner_reload=release`, Arm
  `reload_release`): the mirror is never frozen forever. The reload keeps its
  native length (the rate work F2 is out of scope).
- **Sender base ids**: a slot's base id is never a non-gun id (343 was a
  type-11 id). Customized aliases get their own type from the game's alias
  path of `A2A3E0` (`0x2C934A0`, `0x2A711F0`; five new sites, the partner
  contract is at 495 code sites and 103 vtable slots) and the base is the
  first table index of that gun type with the slot's receiver, mapped to its
  id (ids 0x400../0x600..), else 0. The manifest is version 2 (64 bytes,
  `type[3]`); version 1 still decodes; one message type on channel 2.
- **Writer**: A1 needs the manifest's type; any A1 refusal goes to tier C
  (same type and receiver, same type, then the slot class; flags 3 and refs
  0 only, never the viewer's weapons or an already dispatched equip; a
  candidate line each; a tier d refusal names its last candidate).
  `OptionalReady` admits the exe's own constructor shapes n1..8 / m(n)
  (`kOptionalShapes`, asserted by `generate_equip_writer_pins.py --check`).
  The slot plan and the fire gate use the manifest's type and tier `sub`
  (a substitute in the remote's current slot is a match).
- **Early manifest**: the loadout sender and the channel 2 drain run from the
  observer's ~100 ms path in every mission (the title and the ACC too, no
  partner state read), the preload query drains once more and each
  MissionStart seeds the store from the session cache: the first mission
  after connecting preloads the remote's weapons.
- Scenario `partner-native-weapons.txt`: 18 x 200 ms reload bursts, a new
  `aim-hold-reload` section (instance 1's aiming exit), F1, writer (all three
  slots a1 or c), early manifest and switching evidence. Both PCs must run
  this build (a 0.12.0 peer refuses the version 2 manifest).
- **Review fixes**: a lost reload exit releases the mirror after 20 s (above);
  a same-revision manifest with new content replaces the seeded store (a
  0.12.0 sender restarts at revision 1 every mission and was dropped as a
  duplicate for the rest of the session); A1 never targets the viewer's own
  `vars.weapons` and a tier C candidate whose fresh Capture refs are not 0 is
  refused (the census is the allocation's first snapshot); the early manifest
  stops at window close in any mission (`partner_defer::ShuttingDown` is set
  in partner missions only); the sender has its own line budget (a spent
  mission weapon cap hid the title's `partner_equip=sent`); the slot plan's
  class is cross-checked against the writer's `TypeSlot`.

Evidence: fixture only (CTest, the Normal, Research and Release worker-check
profiles, every contract generator `--check`). Hidden runs: the coordinator.

## October 9, 2026: 0.12.0, the four October 9 tracks in one build (fixture-tested, hidden runs pending)

`integ/final` merges `wp/online-native`, `wp/steam-join`, `wp/aim-parity` and
`wp/weapons2` (at `cbecb6a`) and versions the source, the loader probe, the
script adapter line, the package/friend tooling (`SupportedLoaderVersions`,
`package-install.ps1`, `set-session-mode.ps1`) and the link build id as
**0.12.0**. Two copies must run the same package; 0.11.1 receivers are still
considered on the wire (the edge_rep saturation at 2, the v5 negotiation).
What the release brings (details in the track entries below):

- **Native partner online**: two-PC play shows the other player with the
  native partner by default on both PCs (Play-Coop writes
  `partner_input_send=1` and `partner_native_profile=gun`; `-Partner clips`
  opts out). Stalls, link drops, a paused remote and range hiding fall back
  to the clips and return by themselves; each PC reports how the other one
  shows it (`partner=... native; you on the other PC: native`).
- **Aim parity**: the aim-target mirror (`partner_native_aimpt=look`, part of
  the gun profile) feeds instance 1's network aim slot, so the partner's gun
  points where the other player aims (the fix for the act-12 aim wobble).
- **Reload**: body 1 runs its own native reload from the other player's
  reloads (input frame v5 `reload_seq`, `partner_native_reload=event`, the
  magazine mirror freezes during it and reconciles afterwards).
- **Weapon switching**: the generalized preload and writer put the other
  player's three slots (or resident same-type substitutes) on body 1 in every
  partner-body mission and the slot follower switches natively
  (`partner_native_equip=substitute`, `partner_native_slot=write`).
- **Steam code joins**: joins between players who are not Steam friends no
  longer depend on the 2015 callback dispatcher (`net_steam_dispatch=both`);
  the guest learns why a join failed, and a partner who quits shows as "left".
- **Co-op HUD**: an in-game `CO-OP [...]` status line with toasts (`coop_hud`,
  compact in play, `-Hud compact|full|off`).
- **Friend flow**: Play-Coop `[O] Host online` (MGS2 code or the F9 invite) /
  `[J] Join online` (start Play-Coop first, then paste the code or wait for
  the invite), `[L]` LAN/VPN as before; [two-pc-play.md](two-pc-play.md)
  "Quick start" and `tools/friend/FRIEND-GUIDE.txt` describe the same steps.

Known open points: run 53e36944 (one PC, before the merge) showed the writer
binding only slot 1 and the reload not leaving 11/12; the reload length/exit
and reload-speed work (F2, a receiver-side patch at `0x10778F0`) is not in
this build (that track stopped before any code). Play inherits both through
the gun profile; the gate strips FIRE/RELOAD for a slot that could not be
filled.

Merge semantics worth knowing:

- Input frames: every copy with `partner_input_send=1` sends v4 whatever its
  own drive (online), and v5 (the reload/switch gun group) once the peer sent
  v5, with a v5 probe every 2 s (weapons2). A copy that shows clips now also
  reads its slot and steps the reload/switch counters, so the other PC's
  native reload and slot follower work against it. The stall hold and the
  reload `sample_strip` compose.
- The weapon gate strips by `partner_slots` (mismatch, switching,
  projectile), then the online gun-absent strip and the play leak strip.
- lab.ini: `partner_native_profile=gun` (play and lab) yields
  `partner_native_reload=event`, `partner_native_slot=write`,
  `partner_native_equip=substitute` and `partner_native_aimpt=look`
  (`ResolveWeaponDefaults` stays the last step of `ParseLabConfig`); the
  play refusals are unchanged and play runs `nslot=call` as write.
- The generalized preload and writer admit play with a connected link v2
  and redirected saves; the stock46 pieces run for the native gun profile
  with `partner_native_equip=stock46` only.
- The worker gun stage (and Play-OfflineCoop) is aimpt look + reload event +
  slot write + equip substitute; one `-LabSetting` allow-list carries every
  track's A/B keys.
- Contract: `tools/generate_partner_contract.py --check` covers 490 code
  sites and 103 vtable slots (the union of the aim and weapons sites).

Evidence: fixture only (118/118 CTest, the Normal, Research and Release
worker-check profiles, every contract generator `--check` including
`--steamclient`). The hidden runs on the merged build are the coordinator's
next step: the weapons scenario, the native play rehearsal (`-Mode play`,
then `-WanSim bad`), the aim scenario and the Steam self-test (with
`coop-hud.txt`); then the two-PC acceptance with a friend.

## October 9, 2026: aim parity, the partner's gun aims where the remote aims (fixture-tested, hidden runs pending)

`wp/aim-parity` ([details](native-puppet.md#the-aim-target-mirror-aim-parity-partner_native_aimpt)).
The standing/crouched aim cycle (body act 12/15 swinging +-30 degrees every
~0.5 s while the remote holds act 11) is not the look (O7 refuted by runs
1bffe18a/14ab2e00/8919e533) but instance 1's network aim target `Q+0x10[1]`,
which only the online synchronizer feeds: offline the partner's gun aimed at
the world origin and the aim-turn predicate `0x10A0AB0` (> 80 degrees) kept
firing. The aim-target mirror (`partner_native_aimpt=look`, on in the worker
gun stage and Play-OfflineCoop; debug `aimpt off|look|point|lab|status`)
writes a point 100 m along the remote's look into that slot and its sample
record inside block 1's drain window, so the game's own gun pipeline computes
the remote's aim point, aim pose and turn decisions; refusals, restore,
forget and fault latch as the rates mirror. Contract checker: 449 code sites,
98 vtable slots (`tools/partner_aim_sites.py`: the predicate, the aim
parameters, the aim point builder, the Q methods and allocator, the handler
table, a census of `Q+0x10` writers). Telemetry `native.aim` plus the ring's
`r60_1`/`pred1` and the 1 Hz `partner_native_aim` line. New hidden scenario
`tools/worker/scenarios/partner-native-aim.txt`; the facing scenario gains
aim sections (baseline off, the qualified +-1500 aim turns, camera-rotated,
strafe, walk, crouched) and `aimturn` takes the proven +-1500 stimulus only
(dx 900 never turned a settled aimed body). Fixture evidence only.

Review fixes on the same branch: the scenarios' in-session A/B returns the
guest with `aimpt lab` (back to lab.ini) instead of forcing `look`, so
`-LabSetting partner_native_aimpt=off|point` really decides the rest of the
run; a restore through a changed chain is logged
(`partner_aim=restore_dropped`) instead of dropped silently; the mirror's
event lines share a budget of 64 per mission epoch (a chain flipping every
frame cannot flood the log); `native.aim` gains `written_mm` and the verdict
`slot` ours|other|na (the old criterion q10 == target could not hold while
the aim or the body moved: the target leads the hook's write by one tick) and
`aimpt_err_mm` is measured against the write; the mirror is bound before the
hook reports ready; Play-OfflineCoop gains `-NoAimMirror`; new fixture checks
cover the restore drop, the line budget and the window-close shutdown
(nothing written or restored after it).
## October 9, 2026: Steam code joins and the online UX (fixture-tested, hidden runs pending)

`wp/steam-join` ([steam-online.md](steam-online.md) section 20). Code joins
between players who are not Steam friends no longer depend on the 2015
steam_api's callback manager alone: the host sets the GNS function pointers
204/205 (`ISteamNetworkingUtils004::SetConfigValue`) and pumps
`ISteamNetworkingSockets012::RunCallbacks` on its own transport thread, with
the old 1251 callback and polling kept (`net_steam_dispatch=both|fnptr|
callback|poll`, Play-Coop `-SteamDispatch`, for the two-PC A/B). Every
steamclient property this relies on is asserted by `--steamclient` (RTTI,
strings and shapes; ten binary mutations fail it) and gated again at run
time; a refusal only falls back to callback + poll. The guest learns why a
join failed (1252, plain sentences shared by the native side and Play-Coop),
a partner who quits shows as "left" at once, and an in-game status line with
toasts (`coop_hud`, compact in play, off in the one-PC lab) shows the link,
route, round trip and reasons. The link build id is the version plus the
commit. New hidden scenario `coop-hud.txt`; the self-test gains stage F/D
(`fn=ok`). Evidence is fixture only: `steam_transport_tests` (573 checks),
`link_session_tests` (134), `lab_config_tests`, `steam_shim_patches_tests`,
`worker_debug_tests`, the friend-package and worker-session tests.
## October 9, 2026: native partner online (fixture-tested, hidden runs pending)

`wp/online-native` ([two-pc-play.md](two-pc-play.md) "Native partner",
[native-puppet.md](native-puppet.md) "Online"). Two-PC play now runs the
native partner by default on both PCs: Play-Coop writes `partner_input_send=1`
and `partner_native_profile=gun` (`-Partner clips` opts out), which the loader
expands into exactly the gun stage the hidden worker qualified; the raw native
keys stay play refusals. An online network policy (play default) keeps
stalls, link drops, a paused remote and range hiding from latching the native
partner off: they fall back as network reasons, re-entry waits 2 s of good
link quality, a paused remote is held neutral for 10 s, a flapping link keeps
the clips; the first catch-up snap after an input gap never counts as
diverging; a stall keeps the last stick 400 ms; edge sets repeat 5 times. The
sender sends v4 whatever its own drive, and a capability message (channel 2)
tells each PC how the other shows its body (link-status.json and the
Play-Coop status line: `partner=... native; you on the other PC: native` or
`clips (<reason>)`). The gun profile strips HOLD/RELOAD/FIRE while body 1 has
no gun (outside 30010); the stock46 gun pieces run only for a configured
native gun profile, and the friend loader comes from
`Invoke-WorkerChecks -Profile Release` (`build.ps1 -ReleaseGun`), packaged by
`package.ps1 -Friend` with provenance. The hidden worker gains `-WanSim` and
`-Mode play` (a play rehearsal on 127.0.0.1 with the worker-only
`play_rehearsal=1`, admitted by `PlayRehearsal`), scenarios `play-native.txt`
and `partner-native-online.txt`. Fixture evidence only (lab_config,
partner_native, partner_input, partner_recon, partner_driver, player_sync,
worker_protocol, steam_shim_patches, worker_session, worker_checks,
build_profile, lab_scripts and friend_package tests).

## October 9, 2026: weapons2 review fixes (fixture-tested, hidden runs pending)

`wp/weapons2` review. Input frame v5 is negotiated (v4 plus a v5 probe every
2 s until the peer's own v5 arrives), so a peer build without v5 keeps its
whole native input stream instead of refusing every 116-byte frame. Body 1's
never-filled slots hold handle 0: the plan, the follower (`reason=empty`) and
`slots.handles` now treat equip 0 as empty. The generalized writer digests the
viewer's ammo/weapon vars and shoot count around each dispatch
(`call_unverified; reason=local_ammo_changed` stops the allocation), reports
why it waits (`equip_writer=waiting; reason=..`), orders the slots only once a
manifest is known, and, like the preload and both stock46 pieces, stops when
the game window closes (`shutting_down`). Play runs `partner_native_slot=call`
as `write` from Configure. The preload line names the failing admission.
Residual (documented): the bullet allow-list rests on the weapon categories;
the scenario now checks every `probe1.shots` increase against
`fire.suppressed`.

## October 9, 2026: the generalized preload and writer (fixture-tested, hidden runs pending)

`wp/weapons2` ([details](native-puppet.md#generalized-preload-and-writer-w2w3-partner_native_equip)).
Body 1 can now carry the other player's three loadout slots or same-type
substitutes in every partner-body mission (30010, 30020, 30050), so the slot
follower switches natively and the gate keeps FIRE/RELOAD live after a switch.
The stock46 writer contract is parameterized (`kStock46` unchanged); `Derive`
builds any stock gun's target from the executable tables and runtime banks
(type -> slot, package row, `DB6AC0` dependency packages, optional or NULL
optional, cache entry). W2: one public `RequestLoadToEquipMissionBlock` of the
manifest's base ids per allocation, refused before the call unless nothing is
evicted, the queue has room and no viewer-referenced entry is touched; exact
readback. W3: per slot one dispatch per allocation (tier A1 the base id, tier C
a resident protected stock gun of the same type, tier D refuse), never a filled
slot, full preflight and readback per slot. FIRE: the bullet categories 1..6;
grenade launchers and missiles stay masked. `partner_native_equip=stock46|manifest|substitute`;
the weapons keys follow the native gun profile when not named (play gets
event/write/substitute). No new CMake option (`-ReleaseGun` ships it).
`tools/generate_equip_writer_pins.py --check`: 106 gates (stock46's 92
unchanged) and the static facts. Fixture evidence only.

## October 9, 2026: the partner's own reload and weapon switch (fixture-tested, hidden runs pending)

`wp/weapons2` ([details](native-puppet.md#the-partners-own-reload-r1r6-partner_native_reload)).
The hidden weapons runs showed body 1 already starting its own native reload
from the remote's R; the magazine mirror then wrote the refilled magazine in
the middle of it and the reload mode never left. New: input frame v5 (the
sender's gun mode, a reload counter, a switch counter and its current slot),
the event machine (`partner_native_reload=event`: overlay RELOAD taps until
body 1 enters 11/12, holstered and automatic reloads included, the raw R/CARRY
stripped from the remote's sample), the mirror's freeze and reconcile with
`rec+0x3E8` (`raw|event`), the post-phase-1 probe (bit 20/24 are only visible
inside the drain), the slot follower (`partner_native_slot=write|call`: instance
1's equipment row gets the remote's slot the way SetCurrentSlot writes it for a
non-local id; the game then switches natively), the slot plan and the
generalized weapon gate (mid-switch and unqualified-type strips). Defaults:
lab.ini `mirror`/`off`; the worker gun stage and Play-OfflineCoop run
`event`/`write`. Contract checker: 446 code sites, 101 vtable slots (the mode
table emulation, the reload op table, SetCurrentSlot's non-local data path).
Not done: the generalized writer/preload (other weapons on body 1 need their
own runtime qualification captures); until then switching to the hip or
secondary slot refuses (`empty`) and body 1 keeps stock46. Fixture evidence only.

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
only: `steam_transport_tests` (393 checks, two link v2 sessions end to end
over fake Steam interfaces with loss/reorder/duplication, a broken session
restarted by AutoRestartBrokenSession, busy REJECT, wrong-code hint, Release
exactly once, gates on synthetic PE images, fault latch, shutdown), the
extended config/status/shim/worker tests, the friend-package tests (online
host/join/wait flows) and the contract generator (`--check`, and
`--steamclient` for Valve's unpinned steamclient64). Next: the coordinator's
hidden self-test (`tools/worker/scenarios/steam-selftest.txt`), then the
two-PC acceptance list of [steam-online.md](steam-online.md) section 15 with
a friend. S3 (accepting an invite starts MGSV straight into co-op) awaits a
user decision. A review pass fixed three transport defects before any run: polls
of invited friends no longer use the 1251 rate tokens (one invited friend could
starve a code join), an established session no longer logs `steam=accept` at
4 Hz (steamclient's AcceptSessionWithUser answers true for it), and a fault while
copying a 337 payload can no longer leave the callback lock held.
## October 9, 2026: native facing and the partner's own fire/reload (fixture-tested, hidden runs pending)

`wp/native-polish`. Facing ([details](native-puppet.md#facing)): the facing
governor (`partner_native_face=idle` by default under the native drive: quiet
idle turns at 180 deg/s, one settle snap after a placement/snap/teleport or in
fall/land acts, a contest guard), native placements with a neutral window
instead of a disarm (a disarmed drain while active now leaves: `unwindowed`),
the placement yaw check, a windowed teleport (3 m in 200 ms) and the look
source hook (`partner_native_aim=looksrc`, opt-in, the O7 A/B run).
Weapons ([details](native-puppet.md#weapons)): drain windows isolate the pad's
sample rings (the partner's trigger read the viewer's), the magazine mirror
writes the per-instance store row (the record alone was overwritten every
frame), the shot noise filter (`partner_native_noise=filter`; FIRE needs it
live) and the weapon gate (input frame v4 xflags Equip 0x80 / Gamepad 0x40,
bytes 82..83). Contract checker: 405 code sites, 96 vtable slots. New hidden
scenario `tools/worker/scenarios/partner-native-facing.txt`; the weapons
scenario gains the switch-fire and trigger/noise/store proof lines. Play keeps
visual fire off by default (`-VisualFire`). Fixture evidence only.

Review fixes on the same branch: the weapon gate compares a customized
weapon's alias as its manifest base id (the lab save's 873 is base 46; the
raw compare would have masked FIRE/RELOAD for the whole weapons run), the
store row write is bounded by the store's row count `Y+0x10` (pinned: the
store init `0xEE92F0`), an idle facing turn never starts while the body is
still turning on its own (< 30 deg/s) and restarts after a tick gap instead
of snapping, the facing writes and the look source install stop at the
window-close shutdown (`partner_defer::ShuttingDown`), the drain consumers'
previous held words reset per mission, and the facing scenario's
`face-place-viewer-aims` is captured by the guest (the viewer holding RMB).
## October 8, 2026: partner name tag and friendly crosshair (fixture-tested, never run)

A name tag now floats above the co-op partner's head and a green ring appears at the
screen centre while you aim at him ([partner-nametag.md](partner-nametag.md)). Both
are drawn by our DX11 overlay (under the F9 menu) from a ~100-byte frame the Lua
owner thread publishes; Present never reads game memory. The game thread reads the
game's own render viewport and camera (`*(0x2ba1668)` -> +0x58 -> +0x18: view,
projection, VP, rect), the same unlocked reads the player update makes on that
thread, through ReadProcessMemory only; nothing is written and no game function is
called. `tools/generate_nametag_contract.py --check` pins 24 sites and re-runs the
projection builder 0x241b00 in an SSE emulator (clip.w = view z, x mirrored,
16:9 clamp); the loader compares them at startup and every tick checks vtables,
the projection shape and View x Proj == VP, hiding the tag on any refusal. Names
travel on reliable link channel 3 (lab.ini `nametag_name`, the lab `self_name`,
coop.ini `name` or the Steam persona; sanitized on both ends, fallback "Player 2" /
"Host"). Hidden during demos and the iDroid (STATE_V1 field 13), pause/loading
(stale frames), mission end and whenever the partner body is hidden. Settings:
lab.ini/coop.ini keys and the F9 Partner page (`nametag on|off|status`,
`nametag crosshair aim|always|off`, handled natively). Worker state.json gains a
`"nametag"` object; the hidden scenario `tools/worker/scenarios/partner-nametag.txt`
holds the runtime proof (the worker's `labsetting` now takes a `host`/`guest`
prefix). Fixture evidence only: `debug_menu_tests` (projection, refusals, capsule,
sanitizer, wire, commands), `debug_overlay_tests` (DX11 pixel readback of the label
and the ring), `worker_debug_tests`, `lab_config_tests`, `debug_menu_lua_check`.
Review fixes: the projection shape check accepted only P11 < 20 (a vertical FOV
above ~5.7 deg), so a binocular or scope zoom would have hidden the tag and the
friendly ring exactly while sniping; it now accepts 0.05..1000. The census waited
for nothing and could log a null-selector line at mission load and never the
vtables; it now logs the first tick that read the viewport vtable (and once more
when that one refused and a later one passes). A torn read no longer reuses a
camera from before another refusal or a hidden stretch. While off, terminating, in
a demo or the iDroid, or with the partner hidden the tick reads nothing (names
only). STATE_V1's HUD bits reach the tag also when the debug menu is off. The
overlay reads the clock after taking the frame (no false stale frame at a tick
boundary) and maps NDC over the backbuffer under an open menu too.

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




