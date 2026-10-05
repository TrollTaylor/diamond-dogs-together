# Live findings for partner animation and appearance (2026-10-04, offline lab)

The data comes from `docs/evidence/anim-survey-2026-10-04/`: the host ran with
`partner_anim_probe=1`, and a driven input sequence produced the slot-0 values
below. `Q = *(P+0x60)`. The fields `Q+0x18/0x28/0x30/0x38/0xD8` hold POINTERS to
per-instance arrays (see `native/partner_player.cpp` `ReadAnim`, which reads
element 0).

## Local-player animation state (slot 0), decoded

| Player action | `*(Q+0x18)[0]` (u16) | `*(Q+0x30)[0]` (u32) | `*(Q+0x38)[0]` |
|---|---|---|---|
| stand idle | 1 | 0x00101 | 0 |
| stand moving (running) | 2 | 0x30101 | 3 |
| crouch idle | 3 | 0x00102 | 0 |
| crouch moving (walking) | 4 | 0x20102 | 2 |
| prone idle | 5 | 0x00103 | 0 |
| prone moving (crawling) | 6 | 0x20103 | 2 |

How to read the fields:
- `q30` low byte is the stance: 1 stand, 2 crouch, 3 prone.
- `q30` byte 1 was always 1.
- `q30` byte 2 equals `q38`, the speed class: 0 idle, 2 walk, 3 run.
- `q18` is a combined locomotion id, 1..6.
- `q28` stayed 0 and `qd8` stayed 0xffff the whole time.
- The records at `Q+0x1D8/0x1E0/0x1F8` (hashed) did NOT change.

## Position driving of the realized instance 1 (live)

- `apply=warp` (TppPlayer2 `Warp` each tick) moves the body. The partner
  appears at the commanded position.
- `apply=slot` (writing only `(*(S+0x20))[1]`, the position slot) does NOT
  move the body. The slot value changes (`err_mm=0`), but the model stays where
  it last was (`docs/evidence/anim-survey-2026-10-04/slot-vs-warp.png`).
- With Warp every tick the partner holds one pose. Warp also calls what look
  like resets (`blk+0xC0 vt+0x90`, `blk+0xA0 vt+0x80`, at 0x9CEBD4..0x9CEC2C).
  The likely result is that animation never advances.

## Character in offline mode

- In the `offline-v1` marker mode the character receipt is not active. There
  are no `character_config`/`character=` lines, so `coop.ini character=quiet` on
  the guest had no effect. The Continue-path character fix (merged 8e69cab)
  therefore has still never run live.
- The partner currently uses `Player.CopyPartsInfoFromLocalPlayer`, so it
  always looks like the LOCAL player.

## Proven pieces (2026-10-04, docs/testing-2026-10-04.md)

`SetAllInstanceActiveMode(true)` realizes instance 1. `CopyPartsInfoFromLocalPlayer`
loads its parts (about 0.5 s). Warp moves it. Our UDP link carries poses both ways
at about 30 Hz, live.

## Follow-up (static, same day)

- `q30` read as a u32 is really four u8 array elements packed back to back:
  [stance0, stance1, speed0, speed1] (Q allocator 0xFDF010; Q+0x18 is u16[n],
  Q+0x28/0x30/0x38 are u8[n], n = *(u32*)(S+0x40) = 2). Byte 1 = 1 is
  instance 1's own stance (stand) and byte 2 = `q38` is speed0. `ReadAnim` now
  reads each slot separately.
- The implementation that acts on these findings (anim sync, the native body
  transform, appearance sync, the character in offline-v1) and its live
  experiments: [partner-anim-appearance.md](partner-anim-appearance.md).

## Live round 2 (build a63bd5b): results

| Item | Result |
|---|---|
| Guest plays Quiet (offline-v1 + Continue path) | **Works.** `character=allocated; vars=6; parts=27; camo=116`, `character_readback verdict=applied` |
| Host sees the guest as Quiet (appearance over our link) | **Works.** `partner_net=look_rx type=6`, then `partner_look=loaded q40=6 q48=27 q50=116`. Quiet is visible with her stealth shimmer (`evidence/anim-appearance-2026-10-04/look2-pair.png`) |
| Guest sees the host as Snake | **Fails.** `partner_look=timeout; state=1; q180=0x2`. A Quiet look on the guest's partner loads instantly, so with a local Quiet the Snake-type load for instance 1 never completes. Re-applying after realization does not help. To investigate: Snake-specific packs (arm/face/headband) that are only resident when the local player is Snake |
| Census (after accepting the PS subclass vtable 0x23B0C70) | **ok** for k=0/1. T vt 0x24C8EF0 (+0x68 0x1AD56D0, +0x70 0x1AD4450), A0 vt 0x23B59D0 (+0x80 0x10C7690), C0 vt 0x23D0CA0 (+0x90 0x128B3E0) |
| Smooth mover `apply=xform reset=n` | **Works.** `err_mm=0`, `yaw_err_mrad=0`, no faults, no Warp resets |
| Stance, native SetStance (aset=qs) | Set to crouch (`psw=0x22`), then **reverted** by the game 17 ms later (`psw=0x11`) |
| Stance, request path (aset=r) | `result=ok`, but **no visible change**; the slot-1 stance stays 0x11 |

Conclusion: offline, instance 1's action system is idle. It is not locally controlled (isLocal=0, so it reads no input), and network-follow is S_IS_ONLINE-gated (out of scope). Stance and locomotion animation need a different lever:
- (a) Drive instance 1 as a locally controlled body from the remote player's INPUT (a virtual pad per instance).
- (b) Play motion clips directly on instance 1 (the scripted-motion interfaces used by missions and demos).

## Live round 3 (merged wp/partner-anim2, coop-lab after f6056c2): results

| Step | Result |
|---|---|
| L1, look race fix | **PASS, both ways.** The guest (local Quiet) now loads the host's Snake: `partner_look=loaded q40=0 q48=8 q50=15 q58=3 q60=1 q68=2` in 568 ms. The host loads the Quiet partner. See `evidence/input-lever-2026-10-04/L1-pair.png`. |
| I0, census and pad decode | **PASS.** `partner_input_census ... verdict=ok`; rec0_2ac bit0=1, rec1_2ac bit0=0. Pad: A gives lx=-1000 (dir -1571), S gives ly=+1000, D gives lx=+1000, Shift+W gives held=0x200000 with ly=-1000 (dir 3142). A stance key press shows as **w54=0x2**: neither w4c nor w50 carries it, so the `words` default (0p0) is probably wrong for stance taps. |
| I1, pass-through hook at 0x9C2B51 | **PASS.** 59-60 windows/s, root1_calls 60/s, skips 0. **hook_thread == lua_thread** (no cross-thread race). |
| I2, neutral pad + isLocal | The partner's action system **comes alive**: slot 1 went to act=4 s=2 v=2 (crouch-walk) together with the host's crouch and walk. Host input or stance **leaks** into instance 1 even in neutral mode; the cause is still unknown (live pad path or the activation-time stance cache). cam_dirty=0x0, no faults. |
| I3, constant forward stick | **The partner walks on its own with real locomotion animation**: Quiet crouch-walked about 10 m away from the camera in about 6 s (`evidence/input-lever-2026-10-04/i3b-grid.png`). cam_dirty=0x0, no faults. |
| I6, network input (isend=1, input=net, both) | Input frames flow (`partner_net=input_rx`, `partner_input=live`, age_ms=0). The host partner's body runs at **3.1-3.2 m/s**, matching the guest's run. BUT `partner_input_place reason=err err_mm≈3100 via=xform` fires about every second, and the body sawtooths: it runs about 1.5 m, then is snapped back. Placement and correction fight the locomotion, so the partner never converges on the guest's position (pos1/rec1 = the guest position, about 50 m away). |

Next work (round 4):
1. Reconciliation for input mode: place the partner once at the remote's real position; then let locomotion move it and correct only drift, smoothly (blend), against the correct target, with no sawtooth.
2. Stance taps: map to w54 (or find the correct edge word) and verify that crouch and stand hold.
3. Prone (the hold classifier, `hold=own`).
4. Find and stop the host-input leak into instance 1.
5. Then promote the input lever to the default in Play-OfflineCoop.

### Correction after re-reading the round 3 logs (round 4)

- The I6 body did **not** follow the guest: the guest stood still while slot 1
  stayed in act=4 s=2 v=2 (crouch-move) since the I2 host-input leak, at the
  full-stick crouch-move speed of 3.19-3.20 m/s. The xform snaps fired about
  every 0.98 s (1.5 m threshold held 500 ms) against the remote pose 100 ms in
  the past, which is the sawtooth.
- pos1/rec1 were our own writes of the remote pose, not body readbacks, and
  `partner_body` had hit its 600-line mission cap; the body stayed within
  3.2 m of the remote.
- A stance press on keyboard shows held = w4c = w50 on the press frame and w54
  only on the release frame; "only w54" came from 2 Hz sampling.
- Round 4 (reconciliation, `words=hpr`, `hold=own`, the key-manager stubs and
  the wrap census) is in [partner-anim-appearance.md](partner-anim-appearance.md),
  section "Round 4".

## Live round 4 (merged wp/partner-anim4): results

| Step | Result |
|---|---|
| Startup placement | **PASS, both.** `partner_recon=place; reason=first; err_mm=0`, then `placed ok=1`. Armed `mode=net recon=track`; partner idle (act=1). No stuck state. |
| R1, idle | err_mm=0, snaps=0. **Stance desync:** `partner_recon=stance_fix; remote=2; body=1; via=tap` repeats every ~2 s (fixes=21..26). The injected stance taps do not change the partner's stance. |
| R2, tracking | err_mm ≤ 267, snaps=0, **but locomotion never starts**: act stays 1, v_free_mmps≈0, and `partner_recon=runaway; remote=moving ... action=pin` slides the body by position. The guest's input DID arrive (11 lines: held=0x200000, ly=-1000, mag=1000). |
| Fixed stick (R0) | **Regression vs round 3:** with ly=-1000 mag=1000 delivered, the partner stays act=1, speed 0. |
| wrap=census | Block-1 update calls outside the 0x9C2B51 window: `svc28` (UpdateTree service slot +0x28, sub-phases 0..6, mask 0x7F) **420 calls/s**, plus `walk7` (phase 7) 60/s. |
| wrap=window | Only walk7 gets wrapped (`walk7=60,60,0,60`). svc28 stays unwrapped (`420,0,0,0`), presumably because those calls run on another (job) thread where swapping the shared PP would race. The partner is still idle. |

Revised understanding: round 3's "locomotion" only happened because the partner had already entered a moving state through the host-input leak. The idle-to-move transition (and probably stance toggles) run in the svc28 sub-phases, which read the real pad, outside our window. Round 4 sealed the leak, so nothing starts the partner moving. The in-window consumer only continues an existing movement.

Next (round 5): give instance 1 its own input in the svc28 sub-phases without swapping the shared PP across threads. For example, redirect the PP getters per caller or per thread for block-1 contexts (thread-local "current id" set by wrapping the per-id dispatch of the block-1 nodes inside 0xBF6290/0xBF5F10), or hook the specific idle-to-move and stance-toggle readers. Until then, `Play-OfflineCoop` defaults back to the proven Warp movement (`-InputDrive` or [I] opt in).

Round 5 as planned above (per-thread or per-caller PP redirection for the
block-1 svc28 jobs) was **not pursued**. The direct-motion lever below was
tested instead.

## Live D series: the direct-motion lever (2026-10-05, run 20261005-050139)

Setup: offline lab, Mother Base, host Snake with partner Quiet (look=net), guest
plays Quiet. `stage=3 apply=xform reset=n`, input off. Evidence is in
[evidence/direct-motion-2026-10-05/](evidence/direct-motion-2026-10-05/).

| Step | Result |
|---|---|
| D0, census | **PASS.** `partner_dm_census; g=0x2c1bf20; g_ok=1; ... rec_idx=0/1; verdict=ok` |
| D1, patch + crouch idle (`dm=on dmplay=1`) | **PASS.** `partner_dm=patched; rva=0x1198e25; old=440f44c7; new=0f1f4000`, `partner_dm=play; gani=1`. The 2 Hz line shows `req0=1 rec10=1 rec248=0x02 cam_ok=1`; Quiet visibly crouches (`d1-crouch-idle.png`). So instance 1 **does** consume DM slot 0 offline once the index compare is patched (the open 55-65% question is answered: yes). |
| D2, sprint loop while orbiting (`source=orbit dmplay=5`) | **PASS.** The first attempt was `partner_dm=refused; reason=busy`: a looping gani stays active until `dmstop=1`. After stop then play, Quiet runs around Snake with a real stride cycle (legs in opposite phase across frames 1.3 s apart, `d2-sprint-pair.png`). This is the first genuine locomotion animation on the partner. |
| D2b, crouch-walk loop (`dmplay=3`, orbit) | **PASS.** Crouched stepping pose while orbiting (`d3-crouch-walk-grid.png`). |
| D2c, prone idle (`source=fixed dmplay=7`) | **PASS.** Quiet lies prone at the fixed spot (`d3-prone.png`; hidden behind Snake until the camera moved). |
| D3, revert (`dm=off`) | **PASS.** `partner_dm=reverted; reason=trigger`. Both games kept running, with no fault lines. Real saves UNCHANGED at Stop-Lab. |

Notes:

- `q18` (Q+0x18 act) toggled 0x3/0x4 during playback and `ps2d8` stayed 68,5,
  which is consistent with the Q arrays being bookkeeping, not drivers.
- Switching clips needs `dmstop=1` first (looping clips stay busy). A mapping
  mode should stop, then play, on every clip change, at most once per change.
- Orientation follows the xform yaw (orbit tangent), so the sprint faces the
  direction of travel; the clip's root motion does not fight the xform mover in
  these frames.

Follow-up (static, 2026-10-05): `dm=map` drives these clips from the other
player's stance and speed, with stand walk/run/jog loops added as GANI 10-12
(docs/partner-anim-appearance.md "D-map"). Live result pending (D4/D5).

Conclusion: **direct motion is a working animation route for the partner,
fully on the Lua and main-thread side**, with no worker-thread hooks. Next: a
DM mapping mode, which maps the remote's stance/speed (already in the net
frames) to a gani (idle/walk/run/dash per stance, prone idle and crawl), with
stop+play on change and hysteresis, while xform keeps the placement.
