# Anim truth: the puppet draws what the other player really does (r1101 animtruth)

Lab key `partner_native_aim_truth=0|1` (**off unless named**, also in play and in the gun profile; worker
`labsetting partner_native_aim_truth=1`). Needs the native gun stage. Branch `wp/anim-truth`; proof scenario
`tools/worker/scenarios/r1101-animtruth-play.txt`.

**Hidden run `a8a69ae9`** (the scenario at `5340034f`+docs, mode=play, -WanSim bad, rtt ~175..182 ms, mb-ladder; the
guest presses, the host films; logs of both copies and frames looked at, archive
`mgs-coop-run-archive/r1101/animtruth/a8a69ae9...`, sheet `r1101/frames/animtruth/a8a69ae9-sheet.jpg`):
- Lethal rifle (slot 1, 873) at Mother Base, RMB and RMB+LMB: guest `partner_aim_local=no_aim; act=1` twice, its own
  frame standing with the rifle slung and the magazine unchanged (8/34); host `partner_aim_truth=loco; ... strip=0x18`
  and `hold_run; result=refused; refused_ms=1567` / `1549` (flags 0x25 / 0x5), no body-1 aim edge, and its frames
  show the puppet standing with the rifle slung, no muzzle flash.
- The WU S.Pistol (slot 2, 875, allowed at Mother Base): guest `partner_aim_local=aimed; act=11`, its frame aiming;
  host `partner_aim_truth=aim; remote_act=11`, body 1 in 11 `match_ms=17`, `hold_run; result=late; refused_ms=67;
  admitted_ms=1488`, and the puppet aims the pistol in its frame.
- RMB on the ladder: guest act 21, `no_aim; act=21`; host body 1 on the ladder through the traversal replay (21/21),
  `partner_aim_truth=act; remote_act=21; ... strip=0x18`, `hold_run; result=refused; first_act=21`; the puppet climbs
  with no gun raised in its frame.
- Not covered: the reverse direction (the host pressing), a gamepad, and a key-off A/B in the same run.

## The report

At Mother Base the stock f30050 rule disables lethal weapons (`vars.playerDisableActionFlag = CQC_KNIFE_KILL +
KILLING_WEAPON`). A player who right-clicks there does not aim on their own screen, but on the partner's screen their
puppet raised its gun and aimed, and with LMB it fired visual shots. Logs (fb1031 guest-local 1228, 3515, 9417, 10200,
13479, 14566; friend 30723, 30744, 45816, 47614, 72218): `partner_aim_intent=on; source=pad; remote_act=2;
pad_hold=1; flags=0x25`, then body 1 in act 13 (aim_stand_move) for 150..969 ms while the remote stays in act 2.

Why: the native drive passes the remote's raw HOLD (RMB) to body 1 (`AimFrame` returns `AimSource::pad` before any
gate), and the receiving copy's body 1 is not under the sender's disable flag. Neither the pad nor the v5 group can be
the authority: at Mother Base the v5 Aiming bit (the gun record's hold bit) follows the pad (`flags=0x25` with act 2),
and real aims often log no InHand. The remote's **act** is the authority: a real aim reaches an aim act 15..50 ms
after the press (friend log p50 31 ms, n=48); 38 fb1031 HOLD runs never did.

## The rule (receiver, `partner_slots::TruthStep`, `TickAimTruth`)

Each tick, on the remote's live act (the pose stream), its v5 slot and the weapon gate's lane:

| verdict | when | effect |
|---|---|---|
| `aim` | the act is an aim act (11..17, 19, 20) | pass |
| `keep` | locomotion 1..6 within 300 ms (`kTruthKeepMs`) of the last aim act | pass (a stance change while aiming keeps the gun up) |
| `draw` | locomotion with the v5 InHand bit | pass (a holstered gun coming out: all 55 such fb1031 lines were followed by an aim act; no Mother Base refusal ever showed InHand) |
| `loco` | the gun lane (v5 slot 0..2, not support) in locomotion 1..6, none of the above | HOLD and FIRE leave the remote's sample |
| `act` | a body-owned act: `AimBlockedAct` (dead/dying, carried, cure, trash box, rides, sleep, DM), hit 29, damage 45, ladder 21, pipe 22, wall climb 23, step_on 24, ledge hang 54..56 | HOLD and FIRE leave the sample in every lane |
| `lane` | support/arm lane, no v5 slot (an older peer) or an item slot | pass (those lanes keep their own gates) |
| `other` | an act the rule does not judge (cover, dive, CQC hold with its one-handed gun, the box) | pass |
| `nostate` / `off` | no live remote act / key off | pass (the old raw path) |

The strip joins the Arm's `sample_strip` (`BuildArm`), so Compose removes the raw level, edges, words, long12/tap12 and
the pad's LT/RT; the aim intent's synthesized HOLD (flag/act sources) goes through the same strip. It is state-based:
the first RMB sample of a refused run is already stripped, and when the real aim act arrives the level rise gives one
clean press. It is not a weapon strip: DASH and `hold_stripped` stay untouched (the real body may sprint).

With the key also:
- **Triggers (finder M8):** `Arm.truth_triggers`; the hook's `NativeView` zeroes LT for a stripped HOLD and RT for a
  stripped FIRE after every weapon strip and after the refused support request's 0x8078 strip
  (`partner_input::StripTriggers`), so a gamepad remote's held trigger follows the digital strip.
- **Stance (finder M6):** a refused aim's Aiming bits (v5 and Lua) and the Lua Reloading bit (the R input; R is also
  CARRY) no longer hold off the stance fix while a v5 group is live; the v5 Reloading (gun mode 11/12) still does.
- **Telemetry:** aim parity's `remote_aiming` follows the verdict (aim/keep), not the pad.

Sender half (telemetry only, `TickAimLocal`): `partner_aim_local=no_aim` once per HOLD run held 250 ms with no local
aim act (Mother Base), `partner_aim_local=aimed; after_ms=` for a real aim, with the local gun record's 0x3C0 word.

## Lines

- `partner_aim_truth=<verdict>; remote_act=..; slot=..; gate=..; pad_hold=..; fire=..; flags=0x..; strip=0x..;
  refusals=..; confirms=..` per verdict change (at most every 100 ms; own cap 1000 per mission).
- `partner_aim_truth=hold_run; result=refused|admitted|late; refused_ms; admitted_ms; len_ms; first_act; last_act;
  flags; runs; trig_zeroed` when a remote HOLD run ends.
- `partner_aim_local=no_aim|aimed; act; held_ms|after_ms; gun3c0; mode; slot; runs; aimed; no_aim` (the sender).

## Known limits

- A real aim starts on the puppet when the remote's act arrives through the pose stream (the interpolation delay
  plus ~30 ms after the press), not at the raw press. A LMB tap inside that window is not drawn (the strip lifts only
  with the act); a held LMB fires once the act confirms.
- Binoculars (SUBJECT) in an aim-family act still draw a gun aim: the v5 flags cannot tell it apart from a real aim
  (fb1031 friend 28540: act 11 with flags 0x1 was a real pistol aim, a reload tap followed). It needs the sender's
  BINOCLE status on the wire.

## Finder report (r1101): every mismatch and its state

Fixed behind the key (code and unit tests; not run in a game yet):
- Mother Base RMB aim (code-intent M1, state-gates M1, evidence M0) and RMB+LMB visual fire (M2): verdict `loco`.
- Weapon switch with RMB held (state-gates M7, log 857483 `remote_act=3; flags=0x11`): `loco` strips it. A remote that
  really aims a gun body 1 lacks (code-intent M3 mismatch): the puppet aims its own gun (act-true, the gun differs).
- RMB/LMB in body-owned acts (code-intent M4, state-gates M8): trash box, toilet, knockdown, carried, sleep, rides,
  hit, damage, and now ladder, pipe, wall climb, step_on and the ledge hang (`act`). CQC hold 91..96 passes: the real
  body can aim its one-handed gun there.
- Gamepad triggers past a strip (code-intent M8): `truth_triggers`.
- The Lua Aiming/Reloading input bits in the stance hold and aim parity (code-intent M6). The dm=map fallback's
  gun_busy still reads them (fallback only).

Deferred (why):
- Binoculars drawn as a gun aim (code-intent M7, evidence M2): needs BINOCLE on the wire (see Known limits).
- The viewer's own binoculars make body 1 aim (evidence M1, 7 of 23 SUBJECT edges): the cause (process-wide subject
  or camera state read inside body 1's window) needs a census run first.
- Aim lingering 5 s after release with a stand-in (evidence M3): suspected toggle aim on the viewer; needs a run.
- Non-locomotion pad passthrough (code-intent M5), iDroid / pause menu / demo skip (code-intent M12, state-gates
  M4/M5/M6), run disabled (state-gates M3), stance from crawl (state-gates M9): suspected; each needs a census of the
  sender's held word in that state. While the gun lane is in locomotion, `loco` already strips HOLD/FIRE in them.
- Reload length parity (code-intent M9): `partner_native_reload_rate=adapt` exists and needs its own hidden run.
- Holstered reload tap read as CARRY (code-intent M10): suspected, not observed.
- A stand-in gun of another type fires (code-intent M11): by design (weapons4).
- The box stays on the puppet after the remote leaves it (evidence M4, observed 4-10 s): the off reads ok (sub 0),
  so a retry cannot help; the exit needs a probe of what ends state 0xB on body 1.
- Box refused for the rest of the mission and dives in the open (evidence M5): the latch and the dive's cause (the
  remote's EVADE or the stance fix) need a run.
- dm=map clip stuck in 139 (evidence M6), stuck in act 32 (M7), ledge-hang desync (M8), remote aims while body 1 has
  no gun (M9: no resident weapon), NPC carry not drawn (M10), snap fall/land (M11), stance-fix dive (M12), empty
  vehicle seat (M13): other lanes (recon, traverse, equip writer, seats); none is an input-truth gate.

Not real / checked: the F9 menu open, heli rides and cassette tapes (state-gates OK-f9-heli-tapes).
