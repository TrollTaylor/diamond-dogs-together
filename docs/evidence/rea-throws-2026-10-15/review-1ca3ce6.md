# Review of 1ca3ce6 (native partner throws v1) and hidden run aaa2a0e0

Coordinator: Claude, 2026-10-15. Read-only reviews (two lenses) of `766746a..1ca3ce6`, plus one hidden worker run.

## Hidden run aaa2a0e0df66419e8d026f029417f3da (passed)

`partner-support-native.txt`, `-PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab`; Closed / BaselineRestored / RealSavesUnchanged, no ExpectFailures. Guest log (the viewer of the host body):
- `partner_support=equip; ... equip=354; ... handle=00012c4c; ... result=0` and `equip=349 ... handle=00012bac`;
- `partner_support=armed; ... reason=throw` then `throw_release; rec240=0x12c4c` (rec+0x240 is body 1's own new handle; same for 349);
- `partner_fire_filter=summary; kind=thrown; ... owner=1; dropped=229; preview_dropped=227; passed=0`; census `thrown; owner=0x1` 110 previews (aux=1) + 2 real (aux=0), all `dropped=1`;
- `partner_support=stock_check; ... changed=0; before=0xaff8c4fb; after=0xaff8c4fb` after each throw and episode;
- guest `support` counts `12,16383,12,8,12,12,8,8` before and after both throws.
Frames: `guest-grenade-throw-01..03` show the host body's wind-up, forward release and follow-through (contact sheet `C:/Users/taylo/AppData/Local/Temp/mgs-caps/support1/grenade.png`).
Host side: one real host grenade (`id=33122` = 0x8162 = 354 + flag, owner 0, forwarded); the host count read stayed 12 (not investigated).

## Native-correctness review

Verdict: The native core is correct. I found no defect in the stock-guard stub, the thrown-service owner filter or the contract pins. There are two minor issues: the support writer re-equips body 1 without checking whether it is busy, and the new slot>2 input strip ignores the support key. Neither is blocking.

Checked against the pins and the raw REA output for 0x108C8C0, 0x11AC2E0, 0x11AFA80 and 0x1061D90:

- **Patch bytes.** EB A1 at 0x108C8E5 lands on 0x108C888 (0x108C8E7 - 0x5F). The pad jump is E9 rel32 measured from 0x108C88D. Patch orig/new dwords 0x04818B41/0xCCCCA1EB and the 0x5A-byte BytesOk span match contract sites 0x108C8C0 and 0x108C8EC.
- **Stub registers and flags.** Every jcc displacement resolves to the right label. The stub replays mov eax,[r9+0x204] and pushes/pops both rax and rcx. It never touches rdx, rbp, rsi, r8, r9 or r13. Flags are dead at both 0x108C8EC (shr) and 0x108C8FC. 0x108C8C0 returns void, so leaving eax unshifted on the forced path is harmless.
- **Stub conditions.** The order is r8d==1, window flag, key, r9==armed P, TEB thread id (gs:[0x48]), then class (rec+0x27C>>6)&7 in {2,3}. The state layout is static-asserted.
- **Publication and refusals.** The dword is published with one locked cmpxchg inside a single cache line, after the stub and pad are prepared. Byte, fingerprint, allocation, protection and exchange failures all refuse. The stub is never freed or removed.
- **Thrown filter.**
  - Both vt+8 slots are swapped with locked qword exchanges.
  - The full u16 owner at +0x32 is compared before 0x15E0230 runs. 0x1061D90 sets that field to id&0x1FF and the preview bit at +0x3E.
  - The generator confirms 0x15E0230 has no direct callers.
  - The return value is ignored at 0x1087BF5 and in the void preview helper.
  - Every other owner is forwarded unchanged. The owner is only ever latched to 1, so arming has no race.
- **ChangeEquip and SetCurrentSlot.**
  - The 0x11AC2E0 decompile shows vt+0xA0 writes a local and *out is set to 0 on success, so the result==0 check is valid.
  - SetCurrentSlot's p5/p6 are only read on the local path.
  - Capture enforces every equip-audit precondition: offline, P+0x214/0x218 != 1, Y+0x30 != 1, P+0x70[1]==1, live pool and block, restriction byte with node==blk and base==1, type 12, equip < 0x800, sub < 8, flags 4, and ring margin.
  - PinsIntact runs before each call and covers every called body, including 0x11AFA80. Each call is followed by a fresh snapshot, an instance-0 and other-slot comparison, a readback, and the uncertainty latch.
- **Input gate.** The drain gate is consistent: Enter before injection, Leave on every exit including the __finally, and the a.strip==0x40 clause covers a stale Arm. A refusal after SafePre still runs inside the open guard window, which only adds protection.
- **Lifetime.** The guard key and the owner filter stay armed for the whole process (MissionEnd, disconnect, shutdown). That only adds protection. Termination and disconnect disarm the drain, and the equip writer resets per mission.

The documented residual risks (unwindowed or refused jobs, the rec+0x240 vt+0x160 call, the four unopened helpers) still apply. I did not build, test or run anything.

### [minor] The type-12 support writer re-equips body 1 with no busy check, even mid-throw, mid-reload or mid-switch
- File: `native/partner_support_equip.cpp`
- Detail: Observe (lines 161-205) calls ChangeEquip 0x11AC2E0 for id 1 at line 193. Before the call it checks only that the snapshot is unchanged, the handle, and that the pins are intact. It never looks at body 1's gun state. ObserveNativeSupport (native/game_script_observer.cpp:1722) builds the context without the body_busy gate that the existing gun writer applies at game_script_observer.cpp:1677 (gun mode 0xFF, 8, 11 or 12, or body_switching; comment: 'never equip through an unknown switch/reload'). It also has no check for throw modes 28..42. Follow (SetCurrentSlot 0x11AFA80) has no such check either.

When the remote's slot-3 item changes, Match(before,false) fails with handle_equip. Observe then dispatches ChangeEquip with flags 4, which activates the slot. Per equip-audit.md, Y vt+8 cannot reuse the old slot-3 handle because the equip differs. It calls Y vt+0x10 / X vt+8, which frees the old handle's index back into the non-gun ring X+0x60 that the viewer's support and arm handles also use. It then allocates a new handle, and SetCurrentSlot rewrites row(1)+0/+0x36/+0x37.

Verified from the code: the gate is missing. Not verified: the harm. It depends on the still-unresolved [plugin+0x60] vt+0x160(rec+0x240) call at 0x1087C05 and on whether rec+0x240 holds this Y handle (the contract names it support_item_handle).
- Failure scenario: The remote throws grenade 354 and, right after release, selects magazine 349. Body 1 replays the throw 100-200 ms late and is still in mode 37 with rec+0x2C2==1. The next 100 ms observer tick sees remote equip 349 != handle 354, passes every check, and runs ChangeEquip(1, 349). The 354 handle is released into the shared ring and 349 is allocated. Row(1) changes under the running throw. Body 1's op-10 release then calls vt+0x160(rec+0x240, 0) with a handle that may be stale, and its ring index may already have been reused by an allocation for the viewer's own support item. The same unguarded activation also happens while body 1 is reloading its gun or in mode 8, which the gun writer deliberately avoids.
- Fix: Add a busy gate to the support context, mirroring ObserveEquipWriter: set context.body_busy when mission.body_gun_mode is 0xFF, 8, 11, 12 or 28..42, or when mission.body_switching is set, and have Observe refuse with reason=body_busy. Apply the same check in Follow from a.gun_rec (mode_cur in 8/11/12/28..42, or rec+0x3C0 bit 7) before SetCurrentSlot. Never release a slot-3 handle while ThrowMode(body mode) is true.

### [minor] The new slot>2 input strip ignores the support key, so it changes behaviour with partner_native_support=0
- File: `native/partner_input_hook.cpp`
- Detail: In NativeView (line 497), support_request = (j.slot_valid && j.inv_slot>2) || .... Its first clause does not check a.native_support. Strip() then ORs 0x58|0x8020 for any request that is not admitted. In RunJobWindowOutcome (line 2690), the !support_window strip also has an unconditional first clause. With the default key 0, the guard is never armed, so support_window is always false and nothing is ever admitted. Every native window (drain, main RunWindow, wrap windows) therefore strips HOLD/FIRE/RELOAD whenever the remote's frame shows slot 3, 4 or 6.

Before this diff the hook applied only the driver's a.strip. The driver still honours the round-1 diagnostic partner_native_unsupported=0 when nsupport is off (m.unsupported = ngun && (nunsupported || nsupport) ? ... : no), and the doc says the diagnostic is overridden only 'while support is enabled'. The driver therefore reports a non-unsupported gate while the hook strips anyway. This fails closed and is not a safety issue (ACTION/CQC are outside the native mask in any case). It is still a default-key behaviour change, contrary to 'everything behind partner_native_support (default 0)'.
- Failure scenario: A lab run with partner_native_support=0 (default) and partner_native_unsupported=0, used to observe body 1 aiming while the remote holds a grenade. Before the diff, HOLD/FIRE reached body 1. Now NativeView and the drain strip 0x58 on every frame with inv_slot>2, while partner_weapon/gate telemetry still shows the driver did not mark the gate unsupported. The diagnostic silently does nothing.
- Fix: Choose one: (a) gate both clauses on the key, e.g. support_request = a.native_support && ((j.slot_valid && j.inv_slot>2) || a.strip==kPadReload), and likewise for the !support_window strip, which restores the pre-diff behaviour with the key off; or (b) keep the strip unconditional on purpose, and update the doc and the driver so they agree, with a hook-level test for key 0 + nunsupported 0.

## Gameplay/policy review

Verdict: The review found 8 verified defects in 766746a..1ca3ce6: 1 major and 7 minor. I made no edits, builds, game launches or REA runs. The major one is that the stock guard only covers windowed drain jobs. A partner throw that releases during an unwindowed body-1 job can still spend the viewer's grenades, so v1 does not meet its guarantee of "no change to the viewer's counts". The doc lists this as residual risk, but a cheap fix exists.

These parts hold up against the pins and audit notes:
- **Stub bytes and offsets:** EB A1 lands on 0x108C888, the state offsets match, and the remote/back targets are correct.
- **Thrown filter:** it drops owner 1 at the vt+8 entry, before the preview copy at 0x15E023A.
- **Placed types (13) and arms (slot 6):** neither can be equipped or admitted.
- **Play mode:** it fails closed, because the guard and filter are only armed and Ready only holds in lab mode.
- **Leaving the support gate:** HOLD and FIRE are stripped in the same tick, and stale or non-live input strips them too, so there is no stuck HOLD/FIRE.
- **Lua `support` command:** no path raises an error, and the result stays well under the 160/161-character limits.
- **Scenario file:** every line of partner-support-native.txt passes the Invoke-WorkerScenario.ps1 validator.

Not proved, and no hidden run would detect either one:
- **Soldier grenades:** nothing shows that soldier thrown requests carry a full typed owner at +0x32. If they carried `id & 0x1FF`, soldier index 1's grenades would be dropped. The scenario has no soldier throws.
- **Count oracle:** nothing checks what the retail `Player.GetAmmoStockBySlot(SUPPORT, i)` actually returns. Only test fakes assume it reads support entry i.

### [major] Stock guard only covers windowed drain jobs, so a throw that releases in any other body-1 job spends the viewer's grenades
- File: `D:/mgs-coop-wt/anim-throws/native/partner_support_guard.cpp`
- Detail: The stub only forces the non-consuming branch when g_state.id==1 (the window is open between Enter and Leave) and the TEB thread equals the drain thread (BuildStub offsets 0x19 and 0x2A). Enter is only called in RunJobWindowOutcome (partner_input_hook.cpp ~2686). These body-1 jobs therefore run gun phase 0 and modes 28..42 unguarded:
- jobs refused as !armed (gen change), which go to refusal() and then GuardedJobCall;
- jobs that fail BeginWindow (Leave is called before refusal);
- W50 pass-through jobs, nested or foreign (S1 acceptance only needs windowed/drains >= 0.99);
- every job after the arbiter leaves native mode (defer off, then window off).
In those frames 0x108C8C0 takes the offline branch: it copies counts from the shared stock, and bit 29 comes from P+0x264 bit 3, which is clear with infinite ammo off. Mode 37 op 10 then runs the decrement and the ctl vt+0xC0 setter (0x1087C0B..0x1087CF0). The throw state machine moves from pull to release without further pad input, so a throw started in a window can release in an unguarded job. The latch only notices on a later windowed job of the same episode, after the stock has already changed.
- Failure scenario: Lab, partner_native_support=1, infinite ammo off. The remote throws grenade 354. Body 1 is in mode 33/35 when the guest's arbiter hard-leaves native mode (drain_block, wrap_in_batch or lost), or that frame's block-1 job is a W50 pass-through. Body 1 reaches mode 37 op 10 in an unguarded job and the guest's vars+0x614 count for its slot-3 entry drops by one. The owner filter drops the object, so the viewer loses a grenade with nothing visible. partner-support-native.txt injects no stall or refusal, so the hidden run cannot exercise this.
- Fix: Make the forced remote branch independent of the drain window and thread. Keep only key==1, r8d==1, r9==armed P and class 2/3 (drop the `cmp [rcx],1` and the gs:[0x48] compare), and keep Enter/Leave as telemetry only. The remote branch is the game's own non-local online path, so instance 0 and the viewer cannot be affected. At minimum, hold Enter around refusal()'s legacy GuardedJobCall and keep the guard on while body 1's mode is in 28..42 after the arbiter leaves.

### [minor] Episode stock digest covers all 46 stock words, so the viewer reloading or picking up ammo latches support off for the mission
- File: `D:/mgs-coop-wt/anim-throws/native/partner_input_hook.cpp`
- Detail: SupportCheck compares g_support_episode_stock against ReadSupportDigest() across every windowed job of an episode. ReadSupportDigest hashes all 46 u16 at vars+0x614. Per the doc, the class-1 ammo setter 0x10589E0 writes the same table through ctl vt+0xC0 (`[ctl+0x20]+0x614+2k`), and store vt+0x68 maps only slot 3 to k=6+sub. So the table also holds gun reserve stock and other slots' stock. The viewer's own reload or an ammo pickup between two body-1 jobs changes the digest, and the episode check then sets g_support_leak. The doc only admits a false positive for the viewer spending *support* stock.
- Failure scenario: The remote aims a grenade (episode active through HOLD in slot 3, or body 1 in modes 28..42). The viewer reloads its rifle, so reserve stock at k<6 drops. The next body-1 job logs `partner_support=stock_check; reason=stock_leak; changed=1` and `refused; reason=stock_leak_mission_latched`, and native throws are off until MissionStart. This is routine in co-op.
- Fix: Restrict the episode baseline to the slot-3 stock words k=6..13 (vars+0x620..0x62F), ideally only k=6+row(1) sub. Keep the full 0x548..0x670 compare per job only, where no viewer job runs concurrently. Log which index changed.

### [minor] Routine 1 Hz refusal receipts use up the 256-line receipt cap and hide later equip, follow and uncertain receipts
- File: `D:/mgs-coop-wt/anim-throws/native/partner_support_equip.cpp`
- Detail: Observe runs every ObserveEquipmentTick. With the key on in lab, whenever the remote is not in slot 3 (the normal case), or its input does not match, it sets why="slot" and calls Receipt("refused","slot"). The repeat limiter only suppresses the same reason within 1000 ms, so this logs at 1 Hz. g_lines is capped at 256 and only resets on mission or realize_gen change. The hook's SupportLine cap (512) is likewise shared between per-episode lines (armed/throw, throw_release, stock_check x2) and the one `stock_leak_mission_latched` line.
- Failure scenario: In a lab mission the remote carries a rifle for about 4.5 minutes, which fills 256 `partner_support=refused; ...; reason=slot` lines. When the remote then selects grenade 354, the `partner_support=equip` receipt and any `dispatch_result` or `local_or_other_slot_changed` uncertain-latch receipt are dropped. The evidence the coordinator relies on vanishes without a trace.
- Fix: Do not emit receipts for the idle reasons (slot, off, admission), or log them only when the reason changes. Give equip, follow and uncertain outcomes their own cap. Give the stock-latch line a reserved slot outside the per-episode cap.

### [minor] Writer's uncertain latch clears on every unrealize/realize, so a ChangeEquip that changed viewer state can be retried in the same mission
- File: `D:/mgs-coop-wt/anim-throws/native/partner_support_equip.cpp`
- Detail: Observe clears g_uncertain whenever g_owned_gen != x.realize_gen (or player/block changes). mission.realize_gen increments on every realize and unrealize transition (partner_driver.cpp ~5724/5733). Observe, the drain and the viewer's jobs share the main thread, and ChangeEquip is synchronous. So `local_or_other_slot_changed` or `dispatch_result` means the call itself touched the viewer's row, handles or vars, or failed its readback. That evidence is discarded at the next body re-realize, while the hook's stock latch is mission-scoped.
- Failure scenario: ChangeEquip(id 1, 354) leaves the viewer's row or vars+0x584 changed, so the writer latches "local_or_other_slot_changed". The partner body then unrealizes and realizes (distance, fallback and re-entry), realize_gen goes up, and g_uncertain becomes false. The next tick dispatches ChangeEquip again and changes the viewer's loadout a second time in the same mission.
- Fix: Scope g_uncertain to the mission (reset only when x.mission changes). On a local-snapshot change, also set the hook's g_support_leak so drain admission is latched by the same mission-scoped owner.

### [minor] Support equip and follow have no body-1 busy gate, unlike the gun follower, so a quick item switch re-equips mid-throw
- File: `D:/mgs-coop-wt/anim-throws/native/partner_support_equip.cpp`
- Detail: Observe dispatches ChangeEquip (activate flag 4) whenever body 1's slot-3 handle does not match the remote's equip. It never checks body 1's gun record mode (28..42 throw family, mode 8), rec+0x3C0 bit 7 (switch), or a per-episode write cap. SlotFollow's gun path refuses on mode, wait and kSlotEpisodeWrites. Per equip-audit, ChangeEquip releases the old slot-3 handle (Y vt+0x10, then X vt+8 pushes the index back into the non-gun ring shared with the viewer) and allocates a new one. This can happen while mode 37 has still to run the unresolved `[plugin+0x60] vt+0x160(rec+0x240,0)` at 0x1087C05, whose handle source is unknown (throw-audit).
- Failure scenario: The remote throws 354 and selects 349 straight away. Body 1 lags by network latency and is still in mode 33/37. Observe calls ChangeEquip(349), which frees the 354 handle and its ring index, possibly reallocated to the viewer's next support equip, before body 1's op-10 vt+0x160 call on rec+0x240. The scenario waits 4 s between items and never exercises this.
- Fix: Pass body 1's gun mode and switching state (mission.body_gun_mode, body_switching) into Context. Refuse Observe and Follow with a new `body_busy` reason while the mode is in 28..42, mode 8 or switch bit 7 is set. Cap dispatches per (remote equip, switch_seq) like kSlotEpisodeWrites.

### [minor] Support key off still changes Round 1 behaviour and overrides partner_native_unsupported=0
- File: `D:/mgs-coop-wt/anim-throws/native/partner_input_hook.cpp`
- Detail: NativeView now sets support_request=(j.slot_valid && j.inv_slot>2) regardless of a.native_support, and partner_support_policy::Strip then adds 0x58|0x8020. RunJobWindowOutcome also strips 0x8078 for slot>2 whenever no guard window opened, which is always the case with the key off. At 766746a, only a.strip (from Gate::unsupported, present only with nunsupported) stripped these bits. The Round 1 doc says named keys always win.
- Failure scenario: Lab A/B run with partner_native_unsupported=0 and partner_native_support=0 (default). The remote aims a grenade: HOLD, FIRE and RELOAD are now stripped from body 1, where 766746a passed HOLD. The diagnostic no longer reproduces the old behaviour, although the brief requires everything to sit behind partner_native_support.
- Fix: Compute support_request, and apply the !support_window strip, only when a.native_support is set. With the key off NativeView then returns exactly the 766746a result.

### [minor] Policy fixture's slot/type matrix tests a type field that production always sets to 12
- File: `D:/mgs-coop-wt/anim-throws/tests/partner_support_policy_tests.cpp`
- Detail: The `slot/type matrix incl placed and arms` check iterates GateIn.type 0..31. SupportAdmitted always assigns x.type=12 before Admit, so the matrix proves nothing about production refusal of type 13. In production that refusal depends on partner_support_equip Capture's packed-descriptor decode (index biasing for 0x400..0x44F and >=0x600, `packed&0x3F`) and Match's handle-kind check. No fixture covers either: partner_support_equip_tests only exercises the pure Refusal().
- Failure scenario: A regression in Capture's type decode (wrong bias, or the wrong mask) makes a type-13 claymore read as 12. All 765 support-policy checks and 39 equip checks still pass, while the hook-side type refusal no longer holds. Only Observe's other checks and the driver's remote_equip_type==12 check stay as separate barriers.
- Fix: Have Ready()/Capture return the decoded type and feed it into GateIn.type. Add a fixture that runs Capture/Match over a synthetic memory image with a type-13 descriptor and a kind-13 handle, plus a type-12 control.

### [minor] Hidden scenario cannot detect a viewer arc-preview overwrite and does not assert unchanged guest counts
- File: `D:/mgs-coop-wt/anim-throws/tools/worker/scenarios/partner-support-native.txt`
- Detail: The header claims the captures show body 1's preview "never replaces the local arc". The guest (viewer) stays prone with its gun and never selects support or aims, so there is no local arc on the viewer that could be overwritten. The "guest counts must remain unchanged" claim is also never asserted: the before/after `guest run support` results are only recorded, and stock_check and filtered lines are comments. No refusal or stall is injected, so the unguarded-job path (finding 1) is never exercised. The doc lists viewer arc-preview continuity as unproved, and this run cannot change that.
- Failure scenario: The owner filter misses a preview path (for example a different service slot that writes service+0x100). The run still passes: the guest has no preview to corrupt, the frames look normal, and a stock change on the guest appears only in a raw result nobody is required to diff.
- Fix: Add a phase where the guest runs `debug slot support 0` and holds RMB, showing its own arc, while the host aims and throws. Capture guest frames and compare the arc. After the first read, `require` the guest's exact `counts=` string after each throw, using known lab-seed values or a runner-side equality check. Optionally add a short host network-pause step during a throw to exercise the refusal path.

