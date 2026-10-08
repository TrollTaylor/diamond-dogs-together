# Native puppet

Status (2026-10-08): implemented on `wp/native-puppet`, **fixture-tested only**.
Nothing here has run in the game. Every new path is default OFF behind lab.ini
keys and refuses cleanly on any byte or slot mismatch. The coordinator alone
qualifies it with the hidden worker (stages below); defaults change only after
that.

The partner body (player instance 1) is driven by the remote player's real pad
through the game's own locomotion, action and gun systems. The proven
`dm=map` + xform partner stays the automatic fallback.

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

## Weapons

Status (2026-10-08, branch `wp/native-weapons`): implemented, fixture-tested,
**not yet run in the game**. Static analysis of `mgsvtpp.exe` 1.0.15.4 only;
`tools/generate_partner_contract.py --check` pins every site below (323 code
sites, 87 vtable slots, plus the reachability, owner-stamp and reference
assertions).

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
- **Not covered (open risks):** projectile weapons (row type 3 go through
  `gm+0x210`, not filtered; body 1 only ever carries stock46, a bullet weapon)
  and the **unsuppressed shot noise** `{pos, 0x145, owner}` that `0x1302F50`
  sends through `group+0xA0` vt+0 at `0x1303D06` (skipped only for suppressed
  weapons). Its interface `*(*(svc+0xA8)+8)` is not statically classifiable,
  so the filter logs a read-only census once per mission
  (`partner_fire_census; ...; noise_vt=...; noise_fn0=...`) to pin it; the
  scenario's noise section records whether the receiver's guards react.

### Reload: the magazine mirror

- CanReload `0x1051F30` needs `mag < capacity` (and no `rec+0x280` bit 19,
  and spare stock); the reload request `0x1094770` (gate `0x103E3A4`: class 1,
  equip `rec+0x24C != 0`, `rec+0x3D5 != 4`) sets `rec+0x3C0` bit 20 from the
  R press while aiming (else the tap classifier). Instance 1's non-local shots
  never spend its magazine (`0x1041650` is local-only), so its magazine stayed
  at the change-equip ammo: 0 (fire impossible) or 30 with the ammo
  experiment (above the sniper's capacity: reload impossible, run bec857fb).
- **Input frame v4** (104 bytes) adds the sender's magazine as its getter
  reads it (`0x10435D0`: capacity with `rec+0x3C0` bit 28, else `rec+0x25E`
  with `rec+0x27C` bit 9, else `rec+0x25C`) and its capacity (`rec+0x210/212`,
  doubled for equip `0x42B..0x42E` with bit 20, halved with bit 19), xflag
  `Mag` (bit 4). Sent whenever the copy runs `partner_drive=native`.
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
| `partner_native_aim` | off, look, lookcam |
| `partner_native_gun` | 0, 1 (HOLD 0x8 + RELOAD 0x40) |
| `partner_native_fire` | 0 (off), visual (= 1: FIRE 0x10 only with gun and a live owner filter; never real partner bullets) |
| `partner_native_steer_deg` | 15 (0..30, 0 = no steering) |
| `partner_native_trim_pct` | 0 (0..10) |

Play mode forces legacy/off and sets `kPlayRefusedNativeDrive` (0x10000); in
play mode a partner.trigger `drive=native` or `defer=census|on` is refused too
(`partner_native=trigger_refused; reason=play`), the trigger's other keys apply.
partner.trigger: `drive= defer= local= naim= ngun= nfire=0|off|1|visual steer=`.

To enable (lab only), on both copies: the animated partner settings plus
`partner_drive=native partner_input_send=1 partner_input_hook=1 partner_input_hold=own
partner_input_words=hpr partner_input_wrap=window partner_input_cam=sandbox
partner_input_local=main partner_recon=track partner_input_slot=body
partner_input_defer=census|on partner_input_mask=0x2200006 partner_native_aim=off`
(gun stage: mask `0x220005E`, `partner_native_gun=1 partner_native_fire=visual
partner_native_aim=look partner_input_triggers=1 partner_input_rstick=1`; the
worker gate admits the gun stage with mask `0x220004E` and fire off, or mask
`0x220005E` with fire visual). `Get-WorkerPartnerSettings`
(tools/worker/WorkerSessionCommon.psm1) is the source of truth;
`Play-OfflineCoop.ps1 -Native` opts into the loco stage.

## Wire

- Input frame v3 (100 bytes): v2 plus the sender's look angles, `CAM+0x270/+0x274`,
  the `PP+0x14` heading, xflags, inv slot and manifest revision. A receiver
  without v3 look refuses the look write with `look_refused=no_v3`.
- Input frame v4 (104 bytes, "Weapons"): v3 plus the sender's magazine and
  capacity (xflag `Mag`). Sent whenever the sender runs
  `partner_drive=native` (both copies must run the same build).
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

## Known risks and open items

- Untested at runtime: deferral timing, main-thread cost (`drain_us_max`), a
  same-batch job waiting on block 1 (none seen statically), the Lua thread.
- Longer exposure of swapped PP/CAM fields (two drains per frame).
- Phase-0 consumers that run after PS still see the host-derived look for that
  frame (open item O7).
- Aim init side effects through `*(CAM+0x360)` vt+0x50 and the hash
  `0x5020E975DFD` sub-controller are not sandboxable (S3a decides).
- `aim_err_mdeg` and `body_equip` stay null: their readers are not qualified.
- The ammo experiment (`MGS_LOCAL_STOCK46_AMMO_TEST`, research only) changes a
  qualified native call's arguments (stock 210 / ammo 30); the magazine mirror
  supersedes it (never combine them in a weapons run).
- Weapons (untested at runtime): the shot noise of body 1's visual shots is
  not filtered (census only); projectile weapons are not filtered (body 1 only
  carries stock46); the magazine mirror writes instance 1's own record (a
  wrong rule shows as a refused or faulted mirror, never a write elsewhere);
  only stock46 can be loaded on body 1 (the manifest's other weapons log
  `result=unsupported`).
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
