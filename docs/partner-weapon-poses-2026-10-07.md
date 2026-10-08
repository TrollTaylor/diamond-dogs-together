# Partner weapon poses (aim, fire, reload) on the remote body

October 7, 2026 spike, based on `dev/oct7-continue` d678460. Static research
and fixture tests only: no game was launched, no loader installed and nothing
under the game folder was changed. Every runtime statement below cites an
earlier recorded run; the new code is **not runtime-qualified** and is
default OFF.

## Question

Make instance 1 (the partner body) visibly aim, fire and reload when the
remote player does. Inputs already reach the guest: genuine source
Aiming/Firing/Reloading flags and the current gun alias (`equip_id`) arrive
with every pose sample (`native/partner_driver.cpp`, `dmobs.gun_actions`
in the net branch of the tick, base line 1826). Automatic upper-body playback
is disabled ([STATUS](STATUS.md), [hidden-worker evidence](testing-2026-10-07-hidden-worker-animation.md#read-only-comparison-of-source-and-partner-gun-inputs)).

## (a) Direct-motion weapon clips

Method: a detached Go scan using the same PathCode64 the tools use
(`github.com/unknown321/hashing` `HashFileNameWithExtension`, module from
`work/go-mod-cache`). It reproduces the five `full_mtar_hash` values in
`build/partner-gun-preload-candidate-v2-20261006/integration-plan.json`
and the eight turn hashes in
`build/partner-turn-resident-audit-20261007/turn-paths-hashed.txt`. MTAR
entry tables were read directly (count at +4, 32-byte entries from 0x20,
64-bit hash at +0). About 50 million candidate names were tried
(`snap<weapon>_[aw0_|aw1_]<stance>_<tokens>` with s/c/p/q and alternative
stance prefixes, one to three action tokens).

| MTAR (source) | Entries | Named weapon clips |
| --- | --- | --- |
| `player2_resident.mtar` (`/Assets/tpp/pack/player/motion/player2_resident_motion.fpk`; copy in `build/partner-turn-resident-audit-20261007`) | 1285 | none: only `snapnon_*` locomotion/turn clips matched |
| `pl_rcvr_hg00.mtar` (`chunk0.dat` `/Assets/tpp/pack/player/motion/equip/receiver/pl_rcvr_hg00.fpk`) | 7 | `snaphg00_s_fre`, `snaphg00_s_fre_emp_rld`; 5 unnamed |
| `pl_rcvr_ar00.mtar` (`.../receiver/pl_rcvr_ar00.fpk`) | 7 | `snapar00_aw1_s_fre`, `snapar00_aw0_s_fre_rld`; 5 unnamed |
| `pl_rcvr_ar01.mtar` (`.../receiver/pl_rcvr_ar01.fpk`) | 7 | `snapar01_aw0_s_fre`, `snapar01_aw0_s_fre_rld`; 5 unnamed |
| `pl_rcvr_sr02.mtar` (`.../receiver/pl_rcvr_sr02.fpk`) | 4 | all four: `snapsr02_s_fre1`, `_s_fre2`, `_s_fre_rld`, `_s_fre_emp_rld` |

Receiver copies: `build/partner-gun-motion-audit-20261006` and
`build/partner-gun-motion-ar01-audit-20261006`. One unnamed hash
(`fc53763f430f5be1`) is shared by ar00 and ar01.

Findings:

- **No aim-idle or aim-walk loop is resident by name, and no crouch or prone
  weapon clip was found.** Every named weapon clip is a *standing* one-shot
  (fire, or fire-stance reload). Retail aim is the upper graph's
  `stateGunHold` family (graph 2 of `TppPlayer2_layers.mog`), which the
  public direct-motion request cannot select: the generic direct-motion state
  exists only in lower graph 0
  ([H2 graph audit](testing-2026-10-07-animation-debug.md#october-7-equipment-preparation-event-history-and-channel-test)).
- The receiver clips are only playable on instance 1 with the **31-motion
  research runtime archive** (`2E314419...C9A`, which adds these MTARs to the
  additional-motion list). The normal archive's catalogue allowance is 0..20
  ([archive allowance](testing-2026-10-07-animation-debug.md#subsequent-archive-and-observation-source-changes)).
  The current catalogue (`lua/coop/partner_sync.lua` GANI 26..30) holds
  `snaphg00_s_fre` 26, `snapar00_aw1_s_fre` 27, `snapar01_aw0_s_fre` 28,
  `snapsr02_s_fre2` 29 and `snapsr02_s_fre_rld` 30. The other reloads would
  need new indices beyond `kPartnerDmGaniMax` 30, whose 32-bit bad mask
  allows only one more.
- Playability and placement: manual clip 29 entered the direct-motion act,
  raised the arms and **ended naturally** (run `20261006-175724`). The
  dm=map lifecycle already places the body by xform while finite turns play,
  and fire clips are in-place. No new placement path is involved.
- Clip choice: the wire carries the per-instance **slot alias**
  (871 hip / 873 back / 875 secondary,
  `lua/coop/equipment_observation.lua` header), not a weapon type. The
  retail slot convention gives hip = rifle, back = sniper, secondary =
  handgun. This is a heuristic: a shotgun or SMG in those slots gets the
  rifle/sniper/handgun clip.

## (b) Weapon model on the partner

- The only qualified remote weapon display is the default-off stock46 writer
  (`native/equipment_stock_writer.cpp`). H7 showed the remote rifle **on
  Quiet's back**, with body 1 active/back slot 46
  ([H7](testing-2026-10-07-animation-debug.md#october-7-h7-corrected-stock46-binding-and-remaining-gun-inputs)).
- No retail Lua or verified native call moves it to the hands. The in-hand
  attachment follows the native gun activation (`1099AB0` → `105FDB0`,
  `build/upper-direct-motion-graph-audit-20261007/GUN-INITIALIZATION-CLOSURE.md`),
  the same scheduler lifecycle that blocks (c).
- Consequence: a fire/reload direct-motion clip shows raised arms with
  **empty hands**, as clip 29 already did in run `20261006-175724`. With the
  stock46 writer on, the rifle stays on the back. That is what "looks
  wrong" in the task, and it is not solved here.

## (c) Native upper-body path

It stopped at the natural current-context **scheduler caller, root and
lifecycle** for body 1. The partner's ten Gun/MotionVars inputs read zero.
A channel-2 redirect reached upper Fire child 116 but produced no visible
pose. The required link is the gun service at context+68/this+1B0 to its
owned phase root and update invocation
([remaining gun work](testing-2026-10-07-hidden-worker-animation.md#normal-release-and-handoff)).
The direct-motion consumers already ran on the natural animation worker
thread (run `20261007-064430`). Finishing this path means qualifying, and
probably driving, that worker-side scheduler for body 1, which conflicts with
the no-worker-thread-hook rule. It is **not shorter** than (a), and it is
outside these rules.

## Chosen route and implementation

The route is (a) as direct-motion **one-shots only**, behind a default-OFF
key. It is a visible fire/reload body cue for runtime evaluation, not a full
weapon pose.

- `native/include/mgscoop/partner_gun.hpp` (new, pure, header-only):
  - A rising Reloading edge, or else a rising Firing edge, from a fresh
    remote that is standing and not moving issues a new token for the
    alias's clip: secondary 26, hip 27, back 29, and a back reload 30. The
    catalogue has no handgun or rifle reload, so those edges request nothing.
  - A flag already held on the first fresh sample, or after loss, disable or
    an equip change, is not an edge.
  - The request stays presented for 250 ms, and longer while its flag is
    held. Moving or leaving stance 0 withdraws it.
  - A same-clip edge while that clip is queued or playing is coalesced.
  - Refusals carry a reason: `weapon`, `stance`, `moving`, `no_clip`,
    `motion_not_in_archive` or `queued`.
- `native/partner_driver.cpp` `TickGun` (called in `TickDmMap` after the
  turn selection):
  - It feeds the request into dm=map's existing finite-event queue. dm=map
    already provides stop then play on change, waits after the stop, and
    finishes one-shots naturally without strikes.
  - The token is presented when issued, and afterwards only while dm=map
    still holds its observation. A lifecycle cancellation (Warp/teleport,
    range hide, source change, unrealize) therefore never replays the clip.
  - With the key on, the source-turn `cancel_queue` keeps gun events
    (`partner_gun::CancelTurns`). Otherwise a held Firing flag would cancel
    and re-queue the playing clip every tick.
  - Logs: `partner_gun=request; kind=...; gani=...; token=...; equip=...; available=...`
    and `partner_gun=skip; kind=...; reason=...` (capped at 64).
- `native/partner_dmmap.cpp`/`.hpp`: `OneShot` now also covers 26..30, so
  these clips finish once instead of being struck as short loops. The new
  `Turn` keeps `IncompatibleTurnStance` limited to 13..20.
- `native/lab_config.*`: the `partner_gun=off|dm` key, default `off`.
- Aim alone requests nothing, because no aim loop exists. Aim, aim-walk,
  crouch and prone keep the existing locomotion and turn clips.

### Enabling

The feature needs all of the following:

- `partner_dm=map` (as in the existing animation runs)
- `partner_gun=dm`
- the 31-motion research runtime archive (motion count 31)

With the normal archive, every edge logs
`partner_gun=skip; reason=motion_not_in_archive` and nothing else changes.
Add this line to `lab.ini` for **both** roles:

```
partner_gun=dm
```

`tools/lab/LabCommon.psm1` does not yet map a lab.json key for it (that is
outside this change). Add the line to the generated `lab.ini`, or extend that
table.

### Tests

- `partner_dmmap_tests`: tables, edges, refusals, re-baselining, token wrap,
  queue helpers, and a queue run through `pdm::Step`. In that run, an urgent
  request stops the idle loop, plays once, finishes unstruck and idle
  returns. A request still held after completion freezes without replaying.
- `partner_driver_tests` `TestDmMapGunOneShots`: the production raw
  30 Hz / 60 Hz source path with source turns.
  - Default OFF plays no receiver clip and logs nothing.
  - ON plays 29 and 30 exactly once each, with no strike, no unconsumed play
    and no same-reply play+stop, and idle returns after each.
  - The 21-motion archive refuses the request.
  - A crouched edge is refused, and handgun fire plays 26.
- `lab_config_tests`: default off, `dm` parsed, other values refused.

### Scenario

`tools/worker/scenarios/partner-gun.txt` covers aim, a short shot, held
fire, reload, aim-walk and crouched aim/fire, with guest bursts. Proof in the
guest's `captures.jsonl`:

- `mapped_clip` 29 (or 26/27 for other aliases) with `dm_phase` `playing`
  while `remote_action_flags` includes 2;
- `mapped_clip` 30 after the reload edge (8);
- `mapped_clip` 0 again afterwards;
- locomotion clips for the aim, aim-walk and crouch rows.

The guest log must show the matching `partner_gun=request` and
`partner_dmmap=finished; gani=29` lines. The `fire-burst-*` and `reload-*`
frames show the visual result, which includes the missing hand weapon.

## Risks and what is left

- **Visual:** the hands are empty, as described in (b). A rifle on the back
  combined with a firing pose may read as wrong. Decide with the frames
  whether a cue without a weapon is acceptable at all.
- **Full-body clips:** a one-shot replaces the lower-body loop and briefly
  freezes locomotion. It is only requested when the remote is standing still,
  and it is withdrawn as soon as the remote moves.
- **Class heuristic:** the alias-to-class mapping can pick the wrong family
  for non-default loadouts. A correct mapping needs the remote's base
  equipment ID on the wire (`customizedWeapon`), which is a protocol change.
- **Coverage:** only standing fire for three classes and a sniper reload are
  covered. Crouch, prone, aim loops and other reloads have no resident or
  catalogued clip.
- **Research archive:** shipping this would mean shipping the 31-motion
  archive, which changes package and installer hashes.
- **Layering:** receiver clips also carry weapon receiver tracks, and their
  effect without an attached weapon is unverified.
- **Not run in game:** none of this has been verified at runtime. Measured
  complexity and coverage for the new functions were not taken, because the
  repository has no analyzer.
