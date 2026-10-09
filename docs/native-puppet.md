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

Native polish (2026-10-09, `wp/native-polish`, fixture-tested; hidden runs
pending): facing parity (the facing governor, neutral placements, the
placement yaw check, the windowed teleport and the look at its source) and the
partner's own trigger, reload and shot noise (the sample rings, the magazine
store row, the noise filter, the weapon gate). See "Facing" and "Weapons".

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
mask is `0x224005E` (HOLD, RELOAD and FIRE with visual fire, "Weapons"). EVADE's press consumers (`0x1148C6C`, `0x1299821`,
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
  (500..2000, xflags `Rates` 0x10, bytes 72..77; see "Wire").
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

## Facing

Static analysis of `mgsvtpp.exe` 1.0.15.4 plus the captures of hidden runs
a7823dbe, 63199c7a, bec857fb, d91fc20e and b9528e4d (the facing error is
wrap(body_yaw - remote_yaw) of the capturing role; `input.heading_err_mrad`
was not a facing metric: with a neutral stick it is the two cameras' yaw
difference, now `cam_diff_cdeg`, and `steer_err_mrad` is null below |stick|
0.1). Stops, pivots, crouch, prone and crawl already matched (|err| <= 0.3
degrees). The failures and their fixes:

- **The remote turned without locomotion** (debug placements, aim release):
  the round-4 idle rule (25 degrees, 800 ms, 2 s gap, acts 1..6 only) left
  2.5..7.9 degrees for whole sections. **The facing governor** (pure,
  `partner_recon` FaceMode, `partner_native_face=idle` by default, replaces
  `Action::yaw` under the native drive) turns a quiet body toward the
  time-aligned remote pose yaw with `partner_facing::Step` (180 deg/s, accel
  3600 deg/s^2): only while the remote stick r < 0.1, the remote's speed < 150
  mm/s and the body's own speed < 100 mm/s have each held 200 ms (any stick
  aborts a turn in the same tick), body act 1/3/5 with the remote in the same
  idle act, |err| > 5 degrees held 200 ms and the body's own yaw rate
  (readback minus our writes) below 30 deg/s (a pivot or turn animation the
  game still plays is waited out: reason `rate`), exit below 1 degree (the
  last write lands exactly; a driver tick gap over 250 ms restarts the eased
  turn from the readback, never a snap). Writes are the verified xform
  (T.vt+0x70, T.vt+0x68) at the body's own readback position on the driver
  tick (`Action::face`), refused while `partner_defer::InBatch()` or after
  the window-close shutdown (`partner_defer::ShuttingDown()`; never a
  placement on refusal); the start and end of a turn bump the body epoch,
  small steps do not.
- **Placed or teleported bodies** (fall/land acts 25/26, 77 degrees for 1.5 s
  after an 8 m teleport): the settle mode snaps once when |err| > 10 degrees
  within 1.5 s of a placement, snap or teleport, or while the body is in act
  25/26; a target that moved more than 3 m horizontally within 200 ms (no
  locomotion is that fast; the interpolated pose spreads a teleport below the
  per-tick 5 m test) is placed at once (`teleport_window_mm`).
- **Contest guard**: a readback more than 3 degrees from our last write (no
  write in between) means the game rotated the body: stop, back off 1 s;
  three contests within 5 s latch the governor off for 10 s
  (`face=contested`).
- **Placing while the viewer holds RMB** (57 degrees for 1.5 s): place and
  verify disarmed the window, so block 1's deferred job ran without one and
  read the viewer's live pad (act 11, the viewer's HOLD latch). Native
  placements now keep an enabled native Arm with `neutral` (every native
  window injects `Neutral()`: no held bit, no stick, HOLD latch bit 0
  cleared); verify also needs |yaw err| <= 10 degrees, else one xform at the
  placed yaw (`partner_recon=place_yaw_fix`) and verify again. A drain under a
  disabled Arm while the drive is active is now a hard arbiter reason
  (`unwindowed`; `defer.per_s.unwindowed_native`).
- **Standing aim oscillates about 30 degrees** (body act 12 the remote never
  shows; crouched aim converges toward the viewer's camera). Leading
  hypothesis O7: PS phase 0 (`0xFF28D0`) computes every player's look with its
  only call `0xFF29FC -> 0xFFAC00(PS, id)`, which offline rotates a constant by
  the VIEWER's camera quaternion `*(*(P+0x138)+0xB8)+0x90`; the aim states read
  it in the same phase-0 walk (`0x109F170`), before the post-phase-0 look write
  lands. **The look source hook** (`partner_native_aim=looksrc`,
  `native/partner_look_source.cpp`): a near stub (unwind data registered with
  `RtlAddFunctionTable`) runs `0xFFAC00` and then writes the remote's look into
  instance 1's slot, only while the drain window published it (the hook
  thread, instance 1's PS, ids[1] == 1, a live sample, the per-id record's
  skip bit clear); `jmp rel32` to it from the int3 padding `0xFFAD66..0xFFAD6F`
  (after `0xFFAC00`'s ret, outside every function, never a branch target);
  the call's rel32 low word changes from `ff 81` to `65 83` with one locked
  compare-exchange of the aligned qword `0xFF29F8` (the rel32 straddles the
  cache line at `0xFF2A00`; its high word stays `00 00`). Installed with
  defer=on and naim=looksrc only, never inside a batch, once per process,
  never removed; a refusal (`look_src=refused; reason=bytes|padding|caller|
  align|cache_line|...`) keeps the post-phase-0 write as the fallback. The
  facing A/B run (`partner-native-facing.txt` with and without
  `-LabSetting partner_native_aim=looksrc`) decides O7; if act 12 survives,
  `partner_native_face=aim` adds the governor's aim mode (body 11/12/14/15,
  the remote in an aim idle act, |err| > 10 degrees for 400 ms, the body's own
  yaw rate < 20 deg/s, 90 deg/s).

The contract checker pins the call and the instruction after it (rax unused),
`0xFFAC00`'s prologue, its S_IS_ONLINE gate, the camera chain and the store
tail, and asserts the single caller, no table entry for `0xFFAC00`, the padding
(ret at `0xFFAD65`, ten int3, no function, no rel32 or short branch and no table
entry into it), the qword's alignment and cache line and its exact bytes.

Telemetry: `native.facing` (setting, mode off|idle|settle|aim|contested,
err/target/body centidegrees, the body's own yaw rate, writes, snaps,
contests, turns, reason, place_yaw_err_mdeg, neutral_windows), `native.look`
(src off|pending|installed|refused, src_hits_s, src_hits/refused/skipped,
remote_cdeg and slot_cdeg {pitch, yaw}, err_mdeg = wrap(slot - remote) yaw,
also `gun.aim_err_mdeg`), the ring's `body_yaw_cdeg` and `face_err_cdeg`, the
1 Hz `partner_native_face; mode=..; setting=..; err_mdeg=..; act=..,..;
writes=..; snaps=..; contests=..; reason=..; look_err_mdeg=..; look_src=..;
src_hits_s=..; neutral=..` line and the `partner_recon=face_start|face_end|
face_snap|face_contest|face_latched|place_yaw_fix` events.

## Weapons

Status (2026-10-08, branch `wp/native-weapons`, merged into `integ/finish`
with the native actions): implemented, fixture-tested, **not yet run in the
game**. Static analysis of `mgsvtpp.exe` 1.0.15.4 only;
`tools/generate_partner_contract.py --check` pins every site below (365 code
sites and 91 vtable slots with the actions and rates sites, plus the
reachability, owner-stamp, EVADE census and reference assertions).

### Fire visuals (`partner_native_fire=visual`)

- **How the partner shoots.** Instance 1's gun update `0x103E2D0` runs inside
  block 1's drain; the shoot function `0x1041110` stamps the shooter's id into
  the shot request (`request+0x2A = id & 0x1FF`, `0x1041219`) and queues it
  (equip vt+0x138, `0x10414FC`). The model group's shot handler `0x1302F50`
  later spawns every bullet through **RequestBullet `0xD32C60`** (calls
  `0x13031C8`, `0x13031FA`, pellets `0x130343C`; `rdx` = the record, whose
  `+0x2A` it tests itself at `0x1302FF3`) and plays sound and the "Shoot"
  effect independently of that call.
- **Instance 1 is a non-local shooter.** `rec+0x3C0` bit 0 (`id == P+0x218`)
  is clear for it; `0x10415AB` then skips the whole local block. The checker
  proves by reachability (capstone, branches at `0x104122C`, `0x10415B4`,
  `0x1041E6E` taken) that the ammo call `0x1041650`, the camera kick
  `0x1041775`, CAM+0xB0 `0x1041BDB`, the shot origin `0x1041C6E`, the shoot
  count `0x1041CA5`, suppressor life `0x10417D6`, the synchronizer `0x1041946`
  and `0x1041BF4` are unreachable for it. No sandbox is needed for them.
- **The filter** (`native/partner_fire_filter.cpp`): RequestBullet has no
  direct caller; it is reached only through slot 0 of the bullet iface
  vtables `0x2337DE8` (live, stored by the ctor `0xD32D14`) and `0x2337D18`
  (while destroying, dtor `0xD32619`), both read-only data and 8-aligned. The
  install checks the prologue bytes and both slots, then swaps each slot with
  one locked qword exchange (protection restored; the first slot is swapped
  back when the second fails). The hook drops a request whose owner equals the
  armed id (instance 1's GameObjectId, type bits `0xFE00` clear; armed with
  `ids[1]`) and returns 0, the value RequestBullet itself returns when its slot
  allocator yields nothing (`0xD32CB4`); every other request goes to
  `0xD32C60` unchanged. Any thread (atomics only). Installed lazily by the
  driver tick once per process, never removed, and never disarmed once armed
  (a shot request of instance 1 queued while FIRE was allowed can reach
  RequestBullet after FIRE is masked again; instance 1 fires only under the
  native drive). The driver refuses an owner equal to the local instance's id
  (`ids[0]`), so the local player's shots are never dropped. Verified callers:
  RequestBullet uses only `rcx`/`rdx`, the instruction after each of the
  three call sites in `0x1302F50` does not read `rax`, and 0 is a value
  RequestBullet itself returns. The checker asserts that the shot queue
  (`call [rax+0x138]` with a request in `r8`) has exactly four owner stamps:
  the player shoot (`0x1041225`, `id & 0x1FF`) and three typed builders,
  the soldier ones `0x13A190E` and `0x1453385` (`0x400 | index`, type 2) and
  `0xEDAC19` (`type << 9 | index`, the type word of its system at
  `[this+0x70]+0x168`). Only a player-type shooter with index 1, that is
  instance 1, carries owner 1; no soldier shot is ever dropped.
- **Kept on the viewer's machine:** fire animation, recoil, muzzle and "Shoot"
  effects, gunshot audio, casings, the prone ground-dust effect. **Removed:**
  projectile, hit-scan, damage, impacts and tracers.
- **The fire gate.** FIRE (0x10) reaches instance 1 only while
  `partner_native_fire=visual`, `partner_native_gun=1`, the filter is
  installed, live (both slots still hold it) and armed for `ids[1]`, and
  instance 1's gun record has shown a gun class (`(rec+0x27C>>6)&7 == 1`) in
  this mission (latched: body 1 only carries stock46, and a holster/draw or
  an unreadable tick must not toggle the mask, whose change bumps the Arm gen
  and resets instance 1's private pad masks).
  Otherwise FIRE is masked and the reason is logged once per change
  (`partner_fire=masked; reason=off|gun_off|unavailable|not_ready|owner|gun_read|weapon|bytes|slot|changed|protect|alignment|disabled|hook_lost|disarmed`).
  The filter installs only after body 1 has shown a gun class (a body that
  cannot fire never gets the patch). With `partner_drive=native` FIRE is also
  stripped from the mask whenever the native drive is not active (the
  fallback, or a legacy input lever a trigger armed), so the gun stage's mask
  never reaches instance 1 outside this gate.
- **Not covered (open risk):** projectile weapons (row type 3 go through
  `gm+0x210`, not filtered; body 1 only ever carries stock46, a bullet weapon).
  The **unsuppressed shot noise** `{pos, 0x145, owner}` that `0x1302F50` sends
  through `group+0xA0` vt+0 at `0x1303D06` is now filtered (F3 below: AddNoise
  `0x515270`, slot 0 of `0x21876A8`, the census's `noise_vt`); the read-only
  census (`partner_fire_census; ...; noise_vt=...; noise_fn0=...`) stays.

### Why the partner never fired or reloaded (run b9528e4d) and the fixes

- **The trigger read the viewer's sample rings (F1).** The gun update's
  trigger `0x1095DF0` (its only call `0x103E485`) chooses by `rec+0x27C` bit
  24 (`0x1096137`); weapon 46 (the semi-auto RC_40204 shotgun, inferred from
  its capacity 9 and carry 42) takes the history path: the ring `[[PP+8]+8]`
  with the samples of this update `[[PP+8]+0x44]` (`0x1096171`), or the
  single held word when PP vt+0x98 (keyboard mode, `PP+0x488` bit 6) is set.
  Every window clears `PP+0x488 & 0x5F`, so instance 1's trigger read the
  viewer's gamepad history (no FIRE: the ring's gun3c0 never showed bit 24;
  with a gamepad on the viewer it would have fired on the viewer's trigger).
  Drain windows now point the src rings (held at `src+8`, sticks X/Y at
  `src+0x10+8i`/`+0x20+8i`, triggers at `src+0x30+8i`; the src update
  `0x435260` = src vt+0x38, called by the PP update) and the count `src+0x44`
  at instance 1's own rings: count 1, entry 0 the injected level, the older
  entries 0 for a keyboard remote (re-fire at the weapon's rate while held,
  exactly the remote's own behaviour) or this drain consumer's previous held
  word for a gamepad remote (input frame v4 xflags `Gamepad` 0x40, the
  sender's `PP+0x488` bit 6 clear); restored exactly after the job (also on
  unwind). Refusals: no src, capacity `src+0x40` outside 2..128 (the game's is
  120), a fault. The keyboard-mode bit itself is never flipped (eleven
  readers, including the move sampler `0x10BCD90`).
- **The mirrored magazine was overwritten every frame (F2).** Gun phase 0
  (`0x1040100 -> 0x103E010 -> 0x108C8C0`, call `0x103E08C`) copies the
  per-instance ammo store into `rec+0x25C/+0x25E` every frame
  (`Y = *(*(P+0xC8)+8)`, row `*(Y+0x50) + (id * u32[Y+0xC] + u8 rec+0x3DA) * 6`,
  word 0 / word 1). The mirror now writes that store row (word 1 with the sub
  magazine), then the record, never through the setter `0xEE96C0` (its
  notifier `[Y+0x58]` vt+0x100 takes no id). Refusals (`mag.store.last`): the
  store iface vtable not `0x23894B0`/`0x23895D0` or its +0x20 not the setter
  (`y_vt`), stride outside 1..16, the slot beyond it or id 1's row at or past
  the store's row count `Y+0x10` (the store init `0xEE92F0`, object vt+8 of
  both classes, sizes the types and rows by ids * stride: `slot`), the row type
  outside 1..9 or type 9 with a sub magazine (`type`), instance 1 as the local
  id (`owner`), S_IS_ONLINE or `P+0x204` bit 18 (`online`), `rec+0x3D0` bit 27
  (`tamper`: the obfuscated tamper counters at `*(P+0x48)+0x2F8`; bit 27 is set
  only by three `P+0x204` bit-18 gated sites, never offline; the checker's
  census fails on a fourth), a value above the capacity (`cap`). Completing a
  reload is safe for a non-local body (`0x1054A97`: the local branch runs the
  ammo setter; instance 1's writes `rec+0x3E8` and its own store row through
  `Y vt+0x20`); `leak.ammo_leaks` still watches the viewer's ammo vars.
- **The shot noise is dropped on this machine too (F3).** `0x1303CD4` sends
  `{pos, u8 0x45, u8 1, u16 owner = record+0x2A}` through `group+0xA0` vt+0,
  which is AddNoise `0x515270` (slot 0 of the noise iface vtable `0x21876A8`,
  the census's `noise_vt`; stored only by its ctor `0x51517F`, dtor `0x515246`
  and the static initializer `0x2074C5C`; no direct caller; the call site
  ignores rax; AddNoise keeps the record's `+0x12` owner, `0x5152F9`). The
  noise filter swaps that slot like the bullet slots and drops a record whose
  owner is the armed id (instance 1); everything else passes. FIRE needs it
  live (`partner_fire=masked; reason=noise_<why>`), unless the lab key
  `partner_native_noise=keep` (the worker's gun stage requires `filter`).
- **The weapon gate (F4).** Body 1 carries one gun (stock46). Input frame v4
  carries the sender's gun record equip (xflags `Equip` 0x80, bytes 82..83).
  A customized weapon's record holds its alias (the lab save's back slot:
  alias 873 of base 46, hidden run b9528e4d: `remote_equip` 873, `body_equip`
  46), so the receiver compares it as the stock id of the received loadout
  manifest slot whose alias it is (`weapon.remote_gun_base`; a stock id, an
  alias no slot names or no manifest yet stays itself). While neither the
  raw equip nor that base is body 1's equip every native window strips FIRE
  and RELOAD from its injection (`Arm.strip`, not compared by SameArm: no gen
  bump) and the magazine mirror freezes (`mag.last=weapon`); logged as
  `partner_weapon=mismatch|match; remote_equip=..; remote_base=..;
  body_equip=..; strip=..` and, with visual fire, `partner_fire=masked; reason=weapon_mismatch` /
  `partner_fire=live; reason=weapon_match`. A real weapon switch on body 1
  (the generalized writer, slot-select bits) stays open.
- **Trigger evidence.** After each windowed drain the hook reads instance 1's
  gun record: shots the trigger requested (`rec+0x3C0` bit 24 with the count
  `rec+0x3DF`, `0x10964F7`) and drains that left the gun in a fire mode
  (`rec+0x2C0` 9 or 10, `0x103E53A`): `native.trigger.requested`,
  `shot_drains`, `fire_modes`.

### Reload: the magazine mirror

- CanReload `0x1051F30` needs `mag < capacity` (and no `rec+0x280` bit 19,
  and spare stock); the reload request `0x1094770` (gate `0x103E3A4`: class 1,
  equip `rec+0x24C != 0`, `rec+0x3D5 != 4`) sets `rec+0x3C0` bit 20 from the
  R press while aiming (else the tap classifier). Instance 1's non-local shots
  never spend its magazine (`0x1041650` is local-only), so its magazine stayed
  at the change-equip ammo: 0 (fire impossible) or 30 with the ammo
  experiment (above the sniper's capacity: reload impossible, run bec857fb).
- **Input frame v4** (112 bytes, shared with the rates; see "Wire") adds the sender's magazine as its getter
  reads it (`0x10435D0`: capacity with `rec+0x3C0` bit 28, else `rec+0x25E`
  with `rec+0x27C` bit 9, else `rec+0x25C`) and its capacity (`rec+0x210/212`,
  doubled for equip `0x42B..0x42E` with bit 20, halved with bit 19), xflag
  `Mag` (0x20, bytes 78..81). Sent whenever the copy sends input frames
  (`partner_input_send=1`), whatever its own drive (since "Online").
- **The mirror** (`partner_input_hook.cpp` MirrorMag) runs in block 1's drain
  window before the job, on the thread of the gun update, under a native Arm
  with `partner_native_gun=1` (fire on or off). On a FIRE or RELOAD press
  edge of the drain's view, or when the remote's (mag, cap) changed, it
  re-validates the gun prim (vtable `0x23B3E80`, `prim+8 == blk1`,
  `prim+0x58 == rec`), refuses a full-magazine gun (bit 28), a non-gun class
  and a capacity outside 1..999, and writes `partner_native::MirrorMagazine`
  into the live magazine field (`0x25E` or `0x25C`, as the ammo setter
  `0x1058ABF` does) only when it differs: full stays full (no reload), empty
  stays empty, anything between maps into 1..cap-1 (rounded up), and a FIRE
  edge leaves 1 instead of 0 only when the remote fired a round (its
  previous magazine was loaded: the press spent the last round in the same
  sample); a FIRE press on an already empty remote is a dry fire and shows no
  shot. A fault disables it until MissionStart, and so does an ammo leak (a
  drain whose job changed the viewer's weapon/ammo vars, see `leak` below:
  `partner_input=ammo_leak; mag_mirror=disabled`, `mag.leak_off`). Known limit: after that last
  round the partner keeps 1 round until the remote's magazine changes, so an
  automatic partner weapon held down keeps firing visual rounds until the
  release (the scenario's firehold section records stock46's behaviour).
- The ammo experiment (`MGS_LOCAL_STOCK46_AMMO_TEST`) is superseded by the
  mirror; it stays research-only and is not used by the weapons scenario.

### Loadout mirroring

- **Sender** (pure reads, at most once per second): `vars.weapons[0..2]`
  (u16 at vars+0x548, registration `0x99B0BA`, `0x8070006` = u16[6]),
  `customizedWeapon[0..2]` (vars+0xB06) and `chimeraParts` (u8[36] at
  vars+0xB30). A slot's base id is the equip id itself below 649, else the
  lowest stock id whose GunBasic recipe (`0x2C25C50 + (row-1)*12`, row =
  packed>>6 of the type table `0x2A70920+8+id*2`) has the slot's receiver
  (`chimeraParts[i*12]`); the tables count only after the stock46 sanity
  checks (type table vtable `0x22AD180`, packed 46 == `0x6244`, row 393 == the
  stock46 recipe). The manifest (`partner_equip_wire`, channel 2, latest
  mode) gets a new revision on change and is resent after a new link session
  (`partner_equip=sent; rev=..; base=..; alias=..; receiver=..; tables=..`);
  the v4 frame names the revision (`equip_rev`).
- **Receiver**: stores it (`partner_equip=manifest; ...`) and logs the apply
  verdict for the back slot against instance 1's loaded equip
  (`partner_equip=apply; ...; result=already|unsupported|no_weapon|unknown;
  writer=stock46`). Only stock46 is qualified to load on body 1; the
  generalized writer (any equip id) and a generalized preload remain open
  (each needs its own native qualification; replacing a filled slot was never
  qualified).

### Telemetry and logs

state.json `native` gains: `gun` (`fields_ok`, `cap`, `mag`, `stock`,
`class`, `b3d5`, `slot`, `noreload`, `b20`, `can_reload`, `gate`, and
`body_equip`), `fire` (`mode`, `installed`, `live`, `armed`, `effective`,
`refused`, `suppressed`, `passed`, `passed_player`, `owner_disarmed`,
`suppressed_per_s`, `census`), `mag` (`remote`, `remote_cap`, `local`,
`local_cap`, `writes`, `refused`, `fire_edges`, `reload_edges`, `cap`,
`written`, `last`, `faulted`, `leak_off`), `leak` (`shot_count` = the viewer's
`vars.playerShootCountInMission`, `shot_leaks` = drains whose job changed it,
`reload_drains`, `ammo_vars` = a digest of the viewer's weapon/ammo vars block
`vars+0x548..+0x670` (14 contiguous registrations, asserted by the checker),
`ammo_leaks` = drains whose job changed that block: instance 1's reload
completion runs the ammo setter `0x10589E0`, whose inventory component calls
are not statically attributed to an instance, so this is the runtime proof
that the partner's reload never touches the viewer's inventory) and `equip` (`tx_rev`, `tx_base`, `tx_alias`, `tables_ok`,
`rx_base`, `rx_alias`, `apply`); the ring gains `mag`. Log lines:
`partner_fire=installed|refused`, `partner_fire=live|masked; reason=..`, the
1 Hz `partner_fire; suppressed=..; ...` while counts change,
`partner_fire_census; ...`, `partner_native_gun; ...` with the record fields,
`partner_equip=sent|manifest|apply`.

### Proof (hidden worker; coordinator only)

`pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native-weapons.txt -PartnerProfile native -NativeStage gun -BuildProfile research`

The scenario header lists the evidence per section (aim, single shot, held
fire, reload aiming/holstered, weapon switch 1/2, crouched fire, both
directions, leak captures, loadout, census, noise record).

## Configuration

lab.ini (`native/lab_config.cpp`, LabCommon key table):

| Key | Values (default first) |
|---|---|
| `partner_drive` | legacy, native |
| `partner_input_defer` | off, census, on (ignored unless drive=native) |
| `partner_input_local` | main, all |
| `partner_native_aim` | off, look, lookcam, looksrc (look plus the look source hook) |
| `partner_native_face` | idle, off, aim (the facing governor under the native drive) |
| `partner_native_noise` | filter, keep (the partner's shot noise on this machine; FIRE needs `filter` live) |
| `partner_native_gun` | 0, 1 (HOLD 0x8 + RELOAD 0x40) |
| `partner_native_fire` | 0 (off), visual (= 1: FIRE 0x10 only with gun and a live owner filter; never real partner bullets) |
| `partner_native_steer_deg` | 15 (0..30, 0 = no steering) |
| `partner_native_trim_pct` | 0 (0..10) |
| `partner_native_rates` | log, off, mirror (rates telemetry; mirror writes instance 1's rate record) |
| `partner_native_profile` | off, loco, gun (the qualified stage in one key; play's only way to the native drive) |
| `partner_native_net` | lab, online (play default; see "Online") |
| `partner_native_stall` | neutral, hold (play default; the last live stick up to `partner_native_stall_ms`) |
| `partner_native_stall_ms` | 400 (100..1000) |
| `partner_native_hold` | own, net (the sender's 12-frame words under the native drive) |
| `partner_input_edge_repeats` | 2 (2..8; play default 5; receivers accept up to 8) |

`partner_native_profile=loco|gun` expands, after the other keys, into exactly
the stage `Get-WorkerPartnerSettings` emits (lab_config `ApplyNativeProfile`;
lab_config_tests compares the fields with the raw stage text): drive native,
deferral on, send, hook, hold own, words hpr, wrap window, cam sandbox, local
main, recon track, slot body, rates mirror, noise filter, no P+0x218 window,
and mask 0x2240006 (loco) or 0x224005E with gun, visual fire, look, triggers
and right stick (gun). A key named in the same lab.ini keeps its value (the
`partner_native_aim=looksrc` A/B run). In play the raw native keys stay
refused (`kPlayRefusedNativeDrive` 0x20000: `partner_drive`,
`partner_input_defer`, `partner_native_aim=lookcam`, `partner_input_local=all`,
`partner_input_p218_allow`, `partner_native_noise=keep`; a refused key gets
the profile's value) and a profile without the dm=map + xform base is refused
(0x800000). In play a partner.trigger `drive=native|legacy` toggles the drive
inside an admitted profile (`partner_native=trigger_refused; reason=play_profile`
without one); `defer=` is always refused there
(`partner_native=trigger_refused; reason=play; key=defer`); the trigger's other
keys apply.
partner.trigger: `drive= defer= local= naim= ngun= nfire=0|off|1|visual steer=`.

To enable (lab only), on both copies: the animated partner settings plus
`partner_drive=native partner_input_send=1 partner_input_hook=1 partner_input_hold=own
partner_input_words=hpr partner_input_wrap=window partner_input_cam=sandbox
partner_input_local=main partner_recon=track partner_input_slot=body
partner_input_defer=census|on partner_input_mask=0x2240006 partner_native_aim=off`
(loco stage also `partner_native_rates=mirror`; gun stage: mask `0x224005E`,
`partner_native_gun=1 partner_native_fire=visual partner_native_aim=look
partner_input_triggers=1 partner_input_rstick=1 partner_native_rates=mirror`;
the worker gate admits exactly these three stages, so a gun stage without fire
needs a gate change; the gun stage also admits `partner_native_aim=looksrc`,
which the coordinator selects with `-LabSetting partner_native_aim=looksrc`
(gun stage only; `-LabSetting partner_native_face=off|idle|aim` with any native
stage), and requires `partner_native_noise=filter`). `Get-WorkerPartnerSettings`
(tools/worker/WorkerSessionCommon.psm1) is the source of truth;
`Play-OfflineCoop.ps1` runs the same gun-stage settings on the animated
partner by default (`-NoNative` turns the native drive off).

## Online

Online play (2026-10-09, `wp/online-native`, fixture-tested; hidden runs
pending). Everything above runs unchanged in `mode=play`; what a real link
adds:

- **Sender**: input frames v4 go out whenever `partner_input_send=1`,
  whatever this copy's own drive (TickOnline reads instance 0's gun record and
  rate record for a copy that shows clips; pure reads). Edge sets repeat
  `partner_input_edge_repeats` times (default 2, play 5).
- **Network policy** (`partner_native_net=online`, partner_native
  `OnlineParams`): input stale 1.5 s (`net_input`), link down 1.5 s
  (`net_link`), range hiding and unrealize leave as *network* reasons that
  never count toward the 3-per-mission latch; hard reasons keep it. A quiet
  remote (link up, its input and pose both stale: pause menu, loading) keeps
  the body active with a neutral pad (`reason=remote_quiet`) for up to 10 s,
  then `net_quiet`. Re-entry after a network leave: 2 s cooldown, then the
  link quality must hold 2 s (`partner_native_net::Quality`: a frame younger
  than 250 ms, no inter-arrival gap over 250 ms and a p95 under 120 ms in the
  last 2 s; `net_quality`), then the 2 s settle. More than 6 network leaves in
  5 min: `net_flap` until 30 s of good quality. A remote whose capabilities
  say it sends no input frames: `remote_caps`. The lab policy (default in the
  lab) is unchanged.
- **Stall hold** (`partner_native_stall=hold`): the native Arm's
  `config.stall_ms`; Compose keeps a late sample's stick and look (never
  analog triggers or edges; held buttons follow the 1 s grace) up to
  `partner_native_stall_ms`.
- **Gap snaps** (partner_recon, always on): the first hard snap during an
  input gap of 300 ms or more, or within 2 s after it, is catch-up
  (`gap_snaps`) and never counts toward `diverging`.
- **Hold variant** (`partner_native_hold=net`): the native drive's hold
  classifier answers from the sender's 12-frame words (NetHoldCode) instead of
  the receiver's frames, so a prone hold does not turn into a crouch tap at
  different frame rates. A/B in the two-PC test.
- **Capability exchange** (`partner_caps_wire`, 16 bytes, magic 0x43, reliable
  channel 2 in append mode; the manifest now appends too, so neither replaces
  the other's unsent message): features (input sender, native drive, gun,
  online, hold net, stall hold), profile, drive (off|clips|native), reason
  code (`partner_native_net::ReasonCode`, an append-only table), arbiter mode,
  policy; sent at every link session, on change at most 1/s and as a 5 s
  heartbeat. Logged `partner_caps=rx; drive=..; reason=..` on change.
- **Gun profile without a gun**: until body 1 shows a gun class this mission,
  HOLD, RELOAD and FIRE are stripped (`partner_weapon=absent; body_gun=absent`,
  `native.weapon.strip`).

Telemetry: state.json `native.net` (policy, stall, stall_ms, hold,
edge_repeats, quality_good, quality_good_ms, gaps_100/_250/_1000,
max_gap_ms, ia_p50_ms, ia_p95_ms, net_leaves, hard_leaves, net_reentries,
quiet_episodes, gap_snaps, remote_quiet, flap, last_net_reason, local_drive,
local_reason, caps {rx, sent, received, invalid, remote_drive,
remote_reason, remote_profile, remote_policy, remote_v4, refused});
link-status.json `partner_drive`, `partner_reason`, `remote_drive`,
`remote_reason`; log lines `partner_native_net; policy=..; drive=..; reason=..;
quality=..; ...; remote=<drive>/<reason>` (1 Hz while anything changed, play
or a configured native drive) and `partner_native_net=quiet|resume`.

No new executable address: the online policy, the capability message and the
stall hold are pure; the gun strip reads the already pinned gun record
(`ReadGun1`). `tools/generate_partner_contract.py --check` is unchanged.

## Wire

- Input frame v4 (112 bytes, payload 84; one version for both native work
  packages): v3 plus `[72..77]` the sender's dash, crawl and crawl factor
  rates (milli 500..2000 each, xflags `Rates` 0x10; zero without, always with
  `partner_native_rates=off`), `[78..81]` its magazine and capacity (xflags
  `Mag` 0x20; 1 <= cap <= 999, mag <= cap; zero without a readable gun) and
  `[82..83]` the sender's gun record equip (xflags `Equip` 0x80, 1..65535;
  zero without). xflags `Gamepad` 0x40: the sender's PlayerPad is in gamepad
  mode (`PP+0x488` bit 6 clear); absent means keyboard semantics. Sent whenever the sender sends input frames (any drive, since "Online"); v1..v3
  still decode, while the 104- and 108-byte single-feature drafts are refused
  (both copies must run the same build).
- Input frame v3 (100 bytes): v2 plus the sender's look angles, `CAM+0x270/+0x274`,
  the `PP+0x14` heading, xflags, inv slot and manifest revision. A receiver
  without v3 look refuses the look write with `look_refused=no_v3`.
- Equipment manifest (`partner_equip_wire`, reliable channel 2): the sender
  is native pure reads of vars (see "Loadout mirroring"); the receiver stores
  it and logs the apply verdict.

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
| S3b | `partner-native-weapons.txt` with `-NativeStage gun -BuildProfile research` (gun stage = visual fire) | see "Weapons": suppressed shots, mirror writes, reload bit, no leaks, manifest/apply lines |
| F1 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native-facing.txt -PartnerProfile native -NativeStage gun -BuildProfile research`, then the same with `-LabSetting partner_native_aim=looksrc` | the scenario header: every face-* capture under 10 degrees; aim captures < 5 % of ring ticks over 10 degrees and no act 12 the remote does not show; look.err_mdeg < 1000 (looksrc: look.src installed, src_hits_s > 0); face-place-viewer-aims without act 11 and unwindowed_native 0; facing.contests <= 2; no fallback |
| O1 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native-online.txt -PartnerProfile native -NativeStage loco -SaveSeed lab` (A: lab policy), the same with `-LabSetting partner_native_net=online -LabSetting partner_native_stall=hold -LabSetting partner_input_edge_repeats=5` (B), then both with `-WanSim bad` and `-WanSim awful` | the scenario header: A latches after the third pause (today's defect); B never latches, `remote_quiet` during pauses, `net_quiet` after 12 s with `fallbacks` 0 and re-entry in ~6 s; under WanSim `hard_leaves` 0, sprint ratio 0.95..1.05, `err_mm` < 500 after stops |
| P1 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab -ScenarioPath tools/worker/scenarios/play-native.txt`, then with `-WanSim bad` | the scenario header: both early.log play lines with `refused=0x0; saves=redirected; rehearsal=1`, native active on both copies, `native.net.caps.remote_drive` native, pauses never latch, the game's own networking getter receipts unchanged, real saves unchanged |
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
  qualified native call's arguments (stock 210 / ammo 30); the magazine mirror
  supersedes it (never combine them in a weapons run).
- Weapons (untested at runtime): projectile weapons are not filtered (body 1
  only carries stock46); the magazine mirror writes instance 1's own store row
  and record (a wrong rule shows as a refused or faulted mirror, never a write
  elsewhere); only stock46 can be loaded on body 1 (the manifest's other
  weapons log `result=unsupported`; the weapon gate masks FIRE/RELOAD then).
  Inferred, not proven: weapon 46 takes the trigger's history path (the ring
  fix is moot otherwise: `gun.f27c` bit 24 decides), the store row's type of
  instance 1's slot (a type outside 1..9 refuses the mirror), and that no
  thread other than the batch runs the src update while a drain holds the
  private rings (the existing window swaps src+0x48.. the same way). The noise
  filter drops every AddNoise record with owner 1 (also footsteps or other
  noise body 1 might make on this machine), which matches "each PC perceives
  for its own player". Instance 1's own completed reload runs the store
  setter with its notifier (game code, not ours): `leak.ammo_leaks` watches it.
- Facing (untested at runtime): whether the game keeps the governor's xform in
  idle acts (the old one-shot yaw rule did) and in aim acts (the contest guard
  latches if not); an idle turn starts only while the body's own yaw rate
  (readback minus our writes) is below 30 deg/s, so a pivot or turn animation
  the game still plays is waited out, never fought; a driver tick gap over
  250 ms restarts a turn from the readback instead of snapping onto the
  target; after the window-close shutdown (`partner_defer::ShuttingDown`) the
  governor's writes and the look source install are refused like in a batch; whether looksrc removes the act-12 cycle (O7 is a
  hypothesis: the A/B run decides); the look source patch is cross-modifying
  code installed only with no batch in flight (block 0's phase 0 runs on a
  worker inside a batch).
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
