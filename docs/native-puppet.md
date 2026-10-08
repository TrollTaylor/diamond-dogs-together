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
  retargeted through a near stub to `WrapBatch`. On the hook (main) thread it
  marks the pass active, calls the batch, clears the flag (also on unwind), then
  drains a published deferred record.
- **H1**: the player GameObject's job slot (primary vtable `0x22953D0` + 0x50,
  qword `0x2295420` -> `0x9C1B90`) holds `W50`. W50 uses atomics only. It defers
  sub 0 of block 1 (the published identity: GO = P − 0x20, pool1) only when it
  was called by the job body (return address `0xBF4CEE`) inside an active
  hook-thread pass, claiming the per-pass record 0 -> 2, writing it and
  publishing 1. Everything else passes through and is counted.
- **Drain**: on the main thread after the join, the game's TLS scratch scope is
  pushed (`0x34470`/`0x34550`, exactly as `0xBF4F20` does around its batch), the
  entry read lock is taken as the job body takes it (spin while −1, +1), the gate
  `[[GO+8]+0x38] & 0x10` is checked, the param is rebuilt from the live entry,
  and the job runs inside the input hook's drain window
  (`partner_input_hook::RunJobWindow`). Unlock, then scope pop, also on unwind.
  A missing entry (`lost`), a closed gate, a torn record or an unbalanced scope
  faults the deferral for the mission.
- **Windows** (`native/partner_input_hook.cpp`), under a native Arm only:
  - the main window (phase 8, `0x9C2B51`) composes one frame injection (with
    steering); each consumer (drain0, drain1, walk7, walk11) gets the edges once
    and the level afterwards; older than 100 ms it gets a neutral pad;
  - every native window swaps the HOLD latch `PP+0x27C` bit 0 to the remote's
    (gun eligibility `0x1054210` reads it) and restores bit 0 only;
  - no window opens while a hook-thread batch is in flight (`wrap_in_batch`:
    a job run inline during the join would race block 0 on a worker; this closes
    a pre-existing round-4 `wrap=window` hazard);
  - svc28/30/40 never open windows (block 1's job runs only in drains);
  - after `svc28(root1, phase 0)` inside a drain the remote look angles are
    written into instance 1's own PS slot (`*(PS1+0x290)`, look angles are
    computed in phase 0 by `0xFF28D0` -> `0xFFAC00`), unless the game skipped
    its own computation (record bit 1); aim stage only (`partner_native_aim`).
- **Arbiter** (`native/partner_native.cpp`, pure): off / fallback / entering /
  active / leaving / latched. Fallback -> entering after every precondition held
  2 s (and 5 s after a leaving). Entering applies the native effective settings
  (apply=input, input=net, dm off, wrap=window, hold own, words hpr, cam sandbox,
  recon track with the native preset, slot body, p218 off, gun/fire-limited
  mask); the existing DM release/revert and the reconciliation's
  place/verify/arm sequence carry the switch; the deferral is armed last
  (identity -> Arm -> defer). Hard reasons (fault, hook loss, thread, unrealized,
  range, stage, lost, wrap_in_batch, terminating) leave at once; soft reasons
  after holds (input stale 1 s, link 0.5 s, health over 1 s windows, cost
  3 × 4 ms, diverging, runaway). Leaving: defer off -> window off -> dm=map
  rearm, body epoch ++. Three fallbacks or a failed DM revert latch the mission.

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

Play mode forces legacy/off and sets `kPlayRefusedNativeDrive` (0x10000).
partner.trigger: `drive= defer= local= naim= ngun= nfire= steer=`.

To enable (lab only), on both copies: the animated partner settings plus
`partner_drive=native partner_input_send=1 partner_input_hook=1 partner_input_hold=own
partner_input_words=hpr partner_input_wrap=window partner_input_cam=sandbox
partner_input_local=main partner_recon=track partner_input_slot=body
partner_input_defer=census|on partner_input_mask=0x2200006 partner_native_aim=off`
(gun stage: mask `0x220004E`, `partner_native_gun=1 partner_native_aim=look
partner_input_triggers=1 partner_input_rstick=1`). `Get-WorkerPartnerSettings`
(tools/worker/WorkerSessionCommon.psm1) is the source of truth;
`Play-OfflineCoop.ps1 -Native` opts into the loco stage.

## Wire

- Input frame v3 (100 bytes): v2 plus the sender's look angles, `CAM+0x270/+0x274`,
  the `PP+0x14` heading, xflags, inv slot and manifest revision. Sent only when
  the sender runs `partner_drive=native` (both copies must). A receiver without
  v3 look refuses the look write with `look_refused=no_v3`.
- Equipment manifest (`partner_equip_wire`, reliable channel 2): codec, receiver
  store and the always-on drain are implemented; the sender (a Lua
  `MGS_COOP_EQUIP_V1` receipt) is not yet (W2 research).

## Telemetry

Worker `state.json` gains `"native"` (mode, reason, deferral with per-second
rates, wrap census and look write, input, body parity, reconciliation, gun, a
48-entry ring). Log lines (`mgs-coop/script-observer.log`): `partner_defer=installed|refused|warn|fault`,
the 1 Hz `partner_defer; ...` and `partner_native; ...` lines, transitions
`partner_native=<mode>; reason=..`, `partner_native=enter; step=..`,
`partner_native_gun; ...`, `partner_equip=manifest; ...`.

## Runtime stages (coordinator, hidden worker only)

Scenario: `tools/worker/scenarios/partner-native.txt` (its header lists the
evidence per stage).

| Stage | Command | Pass criteria |
|---|---|---|
| S0 | `pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native.txt -PartnerProfile native -NativeStage census` | mode fallback (defer_census); h0/h1/h2/wrap true; per_s batches ≈ 8/frame, w50_b1_s0 ≈ w50_b0_s0 ≈ 2/frame, ret_other 0, deferred 0, w50_subs 0; batch_threads_seen 1 and thread_ok; svc28_b1 ≈ 7/frame; faults 0; the partner still animates via dm=map; both games close, saves match |
| S1 | same with `-NativeStage loco` (first run short) | mode active within ~6 s of gameplay; deferred == w50_b1_s0; windowed/drains ≥ 0.99; lost/torn/scope_unbalanced/wrap_in_batch 0; drain_us_max < 4000; body stance follows the remote; speed within 20 % while moving; recon snaps 0; fallbacks 0; leak captures clean |
| S2 | loco + `partner_input_local=all` (gate change) | only if S1 stance/actions fail |
| S3a | `-NativeStage gun -BuildProfile research` | gun bit18 and node active within 300 ms of aiming, aim_match; leak-aim clean; CamSub/dirty bits recorded; no host camera/zoom/HUD disturbance |
| S3a-r | as S3a with `-AmmoExperiment` (after its own qualification) | reload visible, no fallback |
| S3b | `partner_native_fire=1` (gate change) | fire-policy decision; aim error measured |

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
  qualified native call's arguments (stock 210 / ammo 30) and needs its own run.
- No test asserts mechanically that no CodeGate caller runs after a lazy
  install; today CodeGate runs once at configure and `partner_upper_probe::Install`
  calls it there too, before any partner install.
