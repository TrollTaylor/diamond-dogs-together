# Native puppet

Status (2026-10-08): implemented on `wp/native-puppet`, **fixture-tested only**.
Nothing here has run in the game. Every new path is default OFF behind lab.ini
keys and refuses cleanly on any byte or slot mismatch. The coordinator alone
qualifies it with the hidden worker (stages below); defaults change only after
that.

The partner body (player instance 1) is driven by the remote player's real pad
through the game's own locomotion, action and gun systems. The proven
`dm=map` + xform partner stays the automatic fallback.

Native actions (2026-10-08, `wp/native-actions`, fixture-tested; hidden runs
pending): the dive, evade and prone rolls now reach the partner (EVADE in the
masks), the partner body runs at the remote's own movement rates (the rates
mirror) and the reconciliation no longer pins a body that is only slower. See
"Actions" and "Rates" below.

## Mechanism

Executable: `mgsvtpp.exe` 1.0.15.4 (SHA-256 `085c2f82...bb45`). All addresses are
RVAs. `tools/generate_partner_contract.py --check` re-verifies every site, slot,
reference and containment fact below against the exe; `native/partner_player_contract.hpp`
is generated from it.

- **H2** (`native/partner_defer.cpp`): the only call of the batch function
  `0xBF4F20(group, mode)` at `0xBF4D55` (ticker `0xBF4D20`, modes 0 then 1) is
  retargeted through a near stub to `WrapBatch`. Batches in flight are counted
  (per pass and in total on the hook thread, and separately on any other
  thread), never flagged, so a nested or concurrent batch cannot clear another
  one's state. On the hook (main) thread the outermost batch drains a published
  deferred record after its join; a batch nested inside another hook-thread
  batch never drains, and a batch on another thread only counts. A batch that
  unwinds (an exception through the join) drops its record (`unwound`, also
  `torn`, the deferral faults), so a stale record is never drained in a later
  frame.
- **H1**: the player GameObject's job slot (primary vtable `0x22953D0` + 0x50,
  qword `0x2295420` -> `0x9C1B90`) holds `W50`. W50 uses atomics only. It defers
  sub 0 of block 1 (the published identity: GO = P − 0x20, pool1) only when it
  was called by the job body (return address `0xBF4CEE`) inside an active
  hook-thread pass with no nested hook-thread batch (`nested`) and no batch on
  another thread (`foreign`) in flight, claiming the per-pass record 0 -> 2,
  writing it and publishing 1. Everything else passes through and is counted.
- **Drain**: on the main thread after the join, the game's TLS scratch scope is
  pushed (`0x34470`/`0x34550`, exactly as `0xBF4F20` does around its batch), the
  entry read lock is taken as the job body takes it (spin while −1, +1), the gate
  `[[GO+8]+0x38] & 0x10` is checked, the param is rebuilt from the live entry,
  and the job runs inside the input hook's drain window
  (`partner_input_hook::RunJobWindow`). Unlock, then scope pop, also on unwind.
  A missing entry (`lost`), a closed gate, a torn record or an unbalanced scope
  faults the deferral for the mission. The drain window opens only for
  instance 1's block: the job's block must equal the Arm's `pool1` (whose live
  value Validate re-reads) and must not be instance 0's live pool; otherwise,
  or while any batch is in flight, the job runs once without a window
  (`drain_block`, `drain_in_batch`; the arbiter leaves on either), so instance
  0's job never sees the remote pad.
- **Windows** (`native/partner_input_hook.cpp`), under a native Arm only:
  - the main window (phase 8, `0x9C2B51`) composes one frame injection (with
    steering); each consumer (drain0, drain1, walk7, walk11) gets the edges once
    and the level afterwards; older than 100 ms it gets a neutral pad;
  - every native window swaps the HOLD latch `PP+0x27C` bit 0 to the remote's
    (gun eligibility `0x1054210` reads it) and restores bit 0 only;
  - no window (main, walk or drain) opens while any batch is in flight, on the
    hook thread or another thread (`wrap_in_batch`: a job run inline during the
    join, or a concurrent batch, would race block 0 on a worker; this closes a
    pre-existing round-4 `wrap=window` hazard);
  - the aim stage saves the sub-controller ranges of `*(CAM+0x360)`; if aim
    init replaces that pointer inside the window, the old object is neither
    compared nor restored (`camsub_dirty_or` bit 2);
  - svc28/30/40 never open windows (block 1's job runs only in drains);
  - after `svc28(root1, phase 0)` inside a drain the remote look angles are
    written into instance 1's own PS slot (`*(PS1+0x290)`, look angles are
    computed in phase 0 by `0xFF28D0` -> `0xFFAC00`), unless the game skipped
    its own computation (record bit 1); aim stage only (`partner_native_aim`).
- **Drain locality**: every drain window sets instance 1's isLocal bit
  (`rec+0x2AC` bit 0) for the deferred job, whatever `partner_input_local`
  says (that key now only affects the walk windows), and restores it after.
- **P1, the phase-1 local detour** (`native/partner_local_detour.cpp`): the base
  action plugin's update (`0x23B5090`+0x18 -> `0x109F0E0`, phases 0..6) runs
  phase 1 at `0x109E130`. There `0x109E17C` `cmp edx,[rax+0x218]; setne bpl`
  picks the local branch (event 4: the act handlers' move-intent sampler
  `0x10BCD90` reads PP+0x10/+0x14 into rec+0x2C0/+0x2C4, which starts
  locomotion) or the remote branch (event 5, network follow) by id == P+0x218,
  not by isLocal. Instance 1 therefore never sampled the injected pad: run
  27c64c8d had live input (held 0x200000, stick 1000) and the body stayed in
  act 1. P1 forces the local branch for exactly (P, id 1, the hook thread) while
  a drain window runs block 1's job, and nowhere else:
  - a near stub (`setne bpl` on the original flags; if the forced id, P and the
    TEB thread id all match, `bpl = 0` and `hits++`, else if only the id matched
    `refused++`; `jmp 0x109E186`; rax and rcx preserved, flags recomputed there);
  - `jmp rel32` to the stub in the int3 padding `0x109E123..0x109E12F` at
    `0x109E128` (outside every function, never a branch target);
  - the 4 bytes of `setne bpl` at `0x109E182` become `eb a4 cc cc` (jmp to the
    padding) in one locked dword exchange inside one cache line.
  The contract checker verifies the bytes, the single jump into `0x109E130`,
  the phase-1 jump-table entry, the padding and the cache line. It installs
  with `defer=on` only, once per process, never removed; the arbiter requires it
  (`not_installed`, `hook_lost`). P+0x218 itself is never written (91 compare
  sites in block 1's update and its pairing with P+0x214 / P+0x58 make that
  unsafe). Telemetry: `p1` (hits, refusals, windows per second) and the 1 Hz
  `partner_native_drain` line (injection, isLocal before, private PP masks,
  instance 1's record after its update: act, next act, sampled magnitude).
- **Arbiter** (`native/partner_native.cpp`, pure): off / fallback / entering /
  active / leaving / latched. Fallback -> entering after every precondition held
  2 s (and 5 s after a leaving). Entering applies the native effective settings
  (apply=input, input=net, dm off, wrap=window, hold own, words hpr, cam sandbox,
  recon track with the native preset, slot body, p218 off, gun/fire-limited
  mask); the existing DM release/revert and the reconciliation's
  place/verify/arm sequence carry the switch; the deferral is armed from the
  place step on, before the native Arm is published (identity -> defer -> Arm;
  `TickInput` refuses with `native_defer` until then). Run 27c64c8d showed why:
  with the Arm first, one frame ran block 1's job inline in a batch and left
  with `wrap_in_batch`; three such entries latched the mission. The cause of the
  last leaving is kept as `left_reason` through cooldown and settling. Hard reasons (fault, hook loss, thread, unrealized,
  range, stage, lost, wrap_in_batch, drain_block, terminating) leave at once;
  soft reasons after holds (input stale 1 s, link 0.5 s, health over 1 s
  windows, cost 3 × 4 ms, diverging, runaway). Leaving: defer off -> window off
  -> dm=map rearm, body epoch ++. Three fallbacks or a failed DM revert latch
  the mission. The GO identity and the census wrap roots name per-mission
  objects: they are withdrawn at MissionEnd/MissionStart (after the deferral is
  disarmed) and once `partner_drive` is no longer native.

## Actions

Pad bits (`native/include/mgscoop/partner_pad_bits.hpp`; values from the Lua
`PlayerPad` enum registration `0x9D3A78..0x9D3EE5`, each pair pinned by the
contract with its string; keyboard bit i is key binding i, `0x1206C21..0x1206CF2`):

| bit | name (aliases) | PC key | passed |
|---|---|---|---|
| 0x2 | STANCE | C (hold = prone) | always |
| 0x4 | DASH | Shift (while prone + A/D: roll) | always |
| 0x40000 | EVADE | Space: dive while running, evade, roll while prone | always (new) |
| 0x200000 | VEHICLE_DASH (keyboard binding 21) | W | always |
| 0x2000000 | keyboard binding 25 (walk) | Ctrl | always |
| 0x8 / 0x40 | HOLD / RELOAD = CARRY | RMB / R | gun stage |
| 0x10 | FIRE | LMB aimed | fire gate only |
| 0x20 | ACTION = MOVE_ACTION = JUMP = RIDE_ON/OFF = CQC_KNIFE_KILL | E | never |
| 0x200, 0x400, 0x8000, ... | CALL/INTERROGATE, SUBJECT, CQC, weapons, markers | | never |

The movement base is `kPartnerNativeBaseMask = 0x2240006`; the gun stage
mask is `0x224004E`. EVADE's press consumers (`0x1148C6C`, `0x1299821`,
`0x129B7DB`: PP vt+0x40 bit 18 -> `Q+0x18[id] = 0x1B`, act 27) first ask
`0x114DB20` / `0x12B60D0` whether the player is network-remote, which is true
only with S_IS_ONLINE (P+0x204 bit 8), so offline instance 1 takes them.
EVADE has nine pad readers in all (`EVADE_CONSUMERS` in
`tools/generate_partner_contract.py`: every PP vt+0x20/+0x30/+0x40/+0x58 read
through P+0xB0 followed by a bit-18 test; `--check` fails on a new one): the
three act-27 sites and six state-specific ones (`0x1097F74`, `0x11B56CF`,
`0x11B8347`, `0x11BA26E`, `0x11D8FDD`, `0x12213B5`) that write per-id player
state (`[PS+0x280]`/`[PS+0x298]` entries, the act record's +0x11/+0x292).
`0x11D8FDD` (EVADE press in a state checked through `[P+0x88]` vt+0x30 >= 4)
also calls a global service's vt+0x28 with the object id held at +0x29C of
its per-id record, i.e. it may message another GameObject in the receiver's
world. Which states reach these six, and whether instance 1 can enter them
with ACTION masked, is not verified (open item; the action census records
`remote_act`/`body.act`). ACTION
stays masked: it aliases ride, knife kill and pickup, which would act on the
receiver's world. RELOAD aliases CARRY (gun stage only; gating it on the
remote's aim state is an open item). Phase 2 (vault/climb) would replay a
one-frame ACTION press only when the remote's act enters a vault/climb census
whitelist near the body; not implemented.

Reconciliation (native preset, `partner_recon::NativeParams`): the moving
runaway compares the body's own velocity with the remote's along and across
the remote's motion and fires only on an overrun (`dv_along > thr`) or a
sideways drift (`|dv_cross| > thr`), `thr = max(1500, 20 % of |v_remote|)`
mm/s; a body that is only slower raises `partner_recon=lag` (never a pin) and
the along-track nudges go up to `clamp(15 % of |v_remote|, 300, 1500)` mm/s
(prone 150..400). After a stick release the idle runaway waits 1200 ms while
the slide decelerates (`stop_grace`). The arbiter's runaway timer counts only
the idle-remote runaway (`report.runaway_idle`). Runs a7823/63199 showed the
old rule: a sprint at 0.835 speed pinned, never exited, and the arbiter left
with `runaway`.

## Rates

The locomotion speed is `base(0x109F7E0) * rate(0x109F690)` (callers
`0x10B9DC1`, `0x10BA6B7`); the rate is `rec+0x44` while crawling (stance 2),
`rec+0x50` while dashing (move class 3), 1.0 otherwise, from the body's own
record: `sp = *(PS+0x260)` (interface vtable `0x23AD380` at object+0x20,
`0x23AD328` while destroyed), `rec = *(sp+0x10) + (id - *(u32)(sp+8))*0x88`,
`id - base < *(u32)(sp+0xC)`, record vtable `0x23AD2B0` (`0x23AD260` while
constructed). The instance setup (`0x9CB7A5..0x9CB80C`) fills `+0x48` (dash)
and `+0x3C` (crawl) from the provider `*(P+0x130)` vt+0x28(id, 1|2) and derives
`+0x50 = +0x48 * +0x4C` and `+0x44 = +0x3C * +0x40`; `+0x4C` is a per-frame
dash factor (`0x10BD1C3`, reset `0x10A4C6B`), `+0x40` a crawl factor (provider
type 0x17 at `0x11403C9`, reset to 1.0 at `0x11313ED`). The provider answers
per id, so instance 1 kept 1.0 while the remote's local player had about 1.2
(runs a7823/63199: sprint 8.35 vs 10.0 m/s, crawl 1.38 vs 1.65; run and crouch
walk 1.00). Which provider input differs (hand mobility or staff skill) is
inferred; `native.rates` records the real values.

- `partner_player::ReadRates` reads instance 0's or 1's record read-only
  through the pinned chain (refusals: sp, sp_vtable, bounds, rec, rec_vtable,
  nonfinite, unpinned).
- Input frame v4 carries the sender's `+0x48`, `+0x3C`, `+0x40` x1000
  (500..2000, xflags bit 4).
- `partner_rates` (pure) plans the writes: the three inputs, then `+0x50` and
  `+0x44` by the game's formulas (the dash factor stays the body's own); a
  field the game rewrote since the last write becomes the new original and is
  written again (`reapplied`); restore puts the originals back (a field the
  game rewrote keeps the game's value); out-of-range or non-finite values latch.
- The hook runs it in the drain window right before block 1's job (hook
  thread, no batch in flight, identity re-validated, SEH-contained; a changed
  record vtable latches without writing). The record must be instance 1's
  own, re-derived in the hook, never only the driver tick's pointers: id 1
  (Validate checked ids[1] == 1) and `sp == *(PS1+0x260)` with `PS1 =
  *(blk1+0x78)` (player PS vtable, `PS1+0x408 == blk1`), else it latches with
  `owner`. It restores at the next main dispatch once no enabled native Arm is
  published (disarmed, or a legacy Arm after leaving the native drive, e.g.
  Play's [I]); outside the drain the restore first re-checks the chain it was
  applied through (player slot P and vtable, ids[1] == 1, pool[1] unchanged
  and not instance 0's, its block, then the owner check) and drops without
  writing when it no longer holds. MissionStart/End forget the state without
  writing (the record dies with the mission) and pass the ending mission's
  epoch: an Arm still carrying it (published before the driver's disarm
  lands, a drain may run meanwhile) never applies the mirror again; each
  MissionStart is a new epoch.
- The driver reads both records every 100 ms and requests the mirror
  (`Arm.rates_mirror`) with `partner_native_rates=mirror` while the native drive
  is active and the remote's rates are under 2 s old.

Only instance 1's own record is written; instance 0, the provider and
S_IS_ONLINE are never touched.

## Configuration

lab.ini (`native/lab_config.cpp`, LabCommon key table):

| Key | Values (default first) |
|---|---|
| `partner_drive` | legacy, native |
| `partner_input_defer` | off, census, on (ignored unless drive=native) |
| `partner_input_local` | main, all |
| `partner_native_aim` | off, look, lookcam |
| `partner_native_gun` | 0, 1 (HOLD 0x8 + RELOAD 0x40) |
| `partner_native_fire` | 0, 1 (FIRE 0x10, only with gun) |
| `partner_native_steer_deg` | 15 (0..30, 0 = no steering) |
| `partner_native_trim_pct` | 0 (0..10) |
| `partner_native_rates` | log, off, mirror (rates telemetry; mirror writes instance 1's rate record) |

Play mode forces legacy/off and sets `kPlayRefusedNativeDrive` (0x10000); in
play mode a partner.trigger `drive=native` or `defer=census|on` is refused too
(`partner_native=trigger_refused; reason=play`), the trigger's other keys apply.
partner.trigger: `drive= defer= local= naim= ngun= nfire= steer=`.

To enable (lab only), on both copies: the animated partner settings plus
`partner_drive=native partner_input_send=1 partner_input_hook=1 partner_input_hold=own
partner_input_words=hpr partner_input_wrap=window partner_input_cam=sandbox
partner_input_local=main partner_recon=track partner_input_slot=body
partner_input_defer=census|on partner_input_mask=0x2240006 partner_native_aim=off`
(loco stage also `partner_native_rates=mirror`; gun stage: mask `0x224004E`,
`partner_native_rates=mirror partner_native_gun=1 partner_native_aim=look
partner_input_triggers=1 partner_input_rstick=1`). `Get-WorkerPartnerSettings`
(tools/worker/WorkerSessionCommon.psm1) is the source of truth;
`Play-OfflineCoop.ps1 -Native` opts into the loco stage.

## Wire

- Input frame v4 (108 bytes): v3 plus the sender's dash, crawl and crawl
  factor rates (milli, xflags bit 4); sent with `partner_drive=native` unless
  `partner_native_rates=off`. Older receivers drop it (both copies must run it).
- Input frame v3 (100 bytes): v2 plus the sender's look angles, `CAM+0x270/+0x274`,
  the `PP+0x14` heading, xflags, inv slot and manifest revision. Sent only when
  the sender runs `partner_drive=native` (both copies must). A receiver without
  v3 look refuses the look write with `look_refused=no_v3`.
- Equipment manifest (`partner_equip_wire`, reliable channel 2): codec, receiver
  store and the always-on drain are implemented; the sender (a Lua
  `MGS_COOP_EQUIP_V1` receipt) is not yet (W2 research).

## Telemetry

Worker `state.json` gains `"native"` (mode, reason, deferral with per-second
rates including `nested`, `foreign`, `unwound`, `drain_block` and
`drain_in_batch`, wrap census and look write, input, body parity,
reconciliation, gun, a 48-entry ring); `worker_debug_tests` checks the member
is valid JSON in every shape (no rates yet, a full ring, nulls, escaped text).
Log lines (`mgs-coop/script-observer.log`): `partner_defer=installed|refused|warn|fault`,
the 1 Hz `partner_defer; ...` and `partner_native; ...` lines, transitions
`partner_native=<mode>; reason=..`, `partner_native=enter; step=..`,
`partner_native_gun; ...`, `partner_equip=manifest; ...`.
Native actions add `native.recon.runaway_idle`, `lag`, `stop_grace`,
`dv_along_mmps`, `dv_cross_mmps` and `native.rates` (`setting`, `id0`/`id1`:
the records x1000 as crawl, crawl_factor, crawl_rate, dash, dash_factor,
dash_rate or null, `id1_read`, `remote` {valid, age_ms, dash, crawl,
crawl_factor}, `mirror` off|wait|ok|latched, `reason`, `writes`, `reapplied`,
`restores`, `drops`; latched reasons sp_vtable, rec_vtable, bounds, nonfinite,
range, fault, owner), and the log lines `partner_native_rates; ...` (on change),
`partner_recon=lag|lag_end` (their own 64-line cap, so a flapping lag never
starves the `runaway` lines); `partner_recon=runaway` carries dv_along/dv_cross.

## Runtime stages (coordinator, hidden worker only)

Scenario: `tools/worker/scenarios/partner-native.txt` (its header lists the
evidence per stage).

| Stage | Command | Pass criteria |
|---|---|---|
| S0 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native.txt -PartnerProfile native -NativeStage census` | mode fallback (defer_census); h0/h1/h2/wrap true; per_s batches ≈ 8/frame, w50_b1_s0 ≈ w50_b0_s0 ≈ 2/frame, ret_other 0, deferred 0, w50_subs 0; nested 0, foreign 0; batch_threads_seen 1 and thread_ok; svc28_b1 ≈ 7/frame; faults 0; the partner still animates via dm=map; both games close, saves match |
| S1 | same with `-NativeStage loco` (first run short) | mode active within ~6 s of gameplay; deferred == w50_b1_s0; windowed/drains ≥ 0.99; lost/torn/unwound/scope_unbalanced/wrap_in_batch/drain_block/drain_in_batch/nested/foreign 0; drain_us_max < 4000; body stance follows the remote; speed within 20 % while moving; recon snaps 0; fallbacks 0; leak captures clean |
| S2 | loco + `partner_input_local=all` (gate change) | only if S1 stance/actions fail |
| S3a | `-NativeStage gun -BuildProfile research` | gun bit18 and node active within 300 ms of aiming, aim_match; leak-aim clean; CamSub/dirty bits recorded; no host camera/zoom/HUD disturbance |
| S3a-r | as S3a with `-AmmoExperiment` (after its own qualification) | reload visible, no fallback |
| S3b | `partner_native_fire=1` (gate change) | fire-policy decision; aim error measured |
| A1 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native-actions.txt -PartnerProfile native -NativeStage gun -BuildProfile research` (or `-NativeStage loco`) | the scenario header: sprint/crawl speed ratio 0.95..1.05 with `rates.mirror` ok and `rates.id1.dash == rates.remote.dash`; no runaway pin and no leaving through the sprint stop; the dive/evade/rolls: EVADE in the ring's held, body.act follows remote_act (27) within 2 captures, settles within 500 mm; leak-evade-host clean; ACTION never reaches the body |

## Known risks and open items

- Untested at runtime: deferral timing, main-thread cost (`drain_us_max`), a
  same-batch job waiting on block 1 (none seen statically), the Lua thread.
- Longer exposure of swapped PP/CAM fields (two drains per frame).
- Phase-0 consumers that run after PS still see the host-derived look for that
  frame (open item O7).
- Aim init side effects through `*(CAM+0x360)` vt+0x50 and the hash
  `0x5020E975DFD` sub-controller are not sandboxable (S3a decides).
- `aim_err_mdeg` and `body_equip` stay null: their readers are not qualified.
- Actions/rates (fixture-tested only): which act ids the dive, evade and rolls
  produce on the body (27 expected for EVADE presses); whether the provider
  difference is in the dash and crawl inputs only (`native.rates.id0` vs
  `id1` decides); a remote whose rates go stale (no v4 for 2 s) restores the
  body's own rates until they return; ACTION (vault, climb) is still masked;
  the six state-specific EVADE readers (see Actions) are census-pinned but
  their states are unverified, and `0x11D8FDD` may message another
  GameObject; the dive's collision effects on enemies (if any) happen in the
  receiver's world like any body contact.

- The ammo experiment (`MGS_LOCAL_STOCK46_AMMO_TEST`, research only) changes a
  qualified native call's arguments (stock 210 / ammo 30) and needs its own run.
- No test asserts mechanically that no CodeGate caller runs after a lazy
  install; today CodeGate runs once at configure and `partner_upper_probe::Install`
  calls it there too, before any partner install.
- Fixture coverage of the windows themselves: `partner_defer_tests` installs
  the real input hook and the deferral on one synthetic image with a fake
  P/PP/src/CAM world and checks that block 0's job (on its worker) sees the
  host pad while block 1's deferred job sees the remote's (held word, HOLD
  latch, vtable copy) and everything is restored afterwards, plus the block,
  in-batch and replaced-sub-controller refusals. The game's real consumers
  of those fields are still only runtime-testable.
