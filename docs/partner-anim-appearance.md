# Partner animation, movement and appearance (offline-v1, experimental)

This page covers the work on branch `wp/partner-anim`, which builds on the offline
partner of [offline-partner-spike-plan.md](offline-partner-spike-plan.md) and
[testing-2026-10-04.md](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/testing-2026-10-04.md). It has three goals:

1. The partner animates like the real remote player (stance, locomotion,
   facing) and moves smoothly.
2. Each copy shows the other player's own character, with the test condition
   that the guest plays Quiet.
3. The character system works in offline-v1.

Everything here is fully offline. Nothing touches S_IS_ONLINE (P+0x204 bit
0x100), the synchronizer P+0xA8, a game session or the master server.

**Status: rounds 1-3 ran live (see the findings page); round 4 (the last section) is built and unit tested, not yet run live.**
Section 7 lists what still has to be proven in the game. A third round (the
input-drive lever, the Snake-load fix and the direct-motion lever) is in the
"Round 3" section at the end of this page; it is likewise built and unit tested,
not yet run live.

Evidence labels: **[V]** verified statically on 1.0.15.4 for this change (every
byte sequence and vtable slot is in the code gate,
`tools/generate_partner_contract.py`: 90 sites, 13 slots); **[R]** taken from the
research pass and not re-verified; **[S]** speculation.

## 1. Defaults keep the proven behaviour

The defaults are `partner_apply=warp`, `partner_anim=off` and
`partner_look=copy`. With them the native side behaves as before: a Lua Warp
every tick, `CopyPartsInfoFromLocalPlayer`, and no native game call. All the new
behaviour is opt-in through `lab.ini` or `partner.trigger`.

The one exception is the transform frame on the wire. It now always carries
the 8-byte animation extension, so it is a v2 frame (section 2). A receiver
from before this change drops v2 frames, so both copies must run this build.

`tools/lab/Play-OfflineCoop.ps1` opts in to the appearance sync and the Quiet
guest, keeps the proven Warp movement, and turns the native movement and
stance experiments on one at a time from its console, or all at once with
`-Experimental` (section 6).

## 2. Wire format

| Frame | Size | Contents |
|---|---|---|
| Transform v1 | 56 | MGSC header (kind snapshot, length 28) + `PlayerTransformPacket` (still accepted on receive) |
| Transform v2 | 64 | Same, with header length 36, plus 8 extension bytes: `0x41`, version 1, act u16, stance u8 (0..3), speed u8 (0..3), flags (bit 0 valid), 0 |
| Look | 40 | MGSC header (kind event, sequence = revision, length 12) + `0x4C`, version 1, type, parts, camo, hand, face u16, face_equip, flags (bit 0 parts loaded), revision u16 |
| Keepalive | 28 | Unchanged (kind event, length 0) |

Codec and strict decoding: `native/player_sync.cpp`, tested in
`tests/player_sync_tests.cpp`.

**Sender.** Every tick it reads the local slot-0 animation fields natively and
sends them with the pose. It sends the local look once its own parts state is 3:
at once when the look changes (the revision bumps), then every 2 s.

**Receiver.** Frames are handled in the order keepalive, look, transform. The
snapshot ring (`pose_interp`) carries the anim fields as discrete values, so a
sample takes them from the snapshot it holds or interpolates towards.

## 3. Static facts this change relies on

### Q arrays
**[V]** These come from the allocator at 0xFDF010, with n = `*(u32*)(S+0x40)`
(0xFDEFE5) = 2. The arrays are packed back to back with natural alignment:

| Array | Type | Meaning |
|---|---|---|
| Q+0x18 | u16[n] | action / locomotion id (0xFDF146, 0xFDF185) |
| Q+0x28 | u8[n] | |
| Q+0x30 | u8[n] | stance 1/2/3 (0xFDF22D) |
| Q+0x38 | u8[n] | speed class 0/2/3 (0xFDF265) |
| Q+0x40 | u8[n] | type |
| Q+0x48 | u8[n] | parts |
| Q+0x50 | u8[n] | camo |
| Q+0x58 | u8[n] | hand |
| Q+0x60 | u16[n] | face (0xFDF353, 0xFDF392) |
| Q+0x68 | u8[n] | face_equip (0xFDF3CD) |

The live survey read Q+0x30 as a u32. That u32 is really [stance0, stance1,
speed0, speed1], which is why `ReadAnim` now reads per slot.

### SetPartsInfoAtInstanceIndex
**[V]** 0x9DF530 takes `instanceIndex`, `type`, `partsType`, `camoType`,
`handType` and `faceId`. It stores u8 values to Q+0x40/48/50/58[i], a u16 to
Q+0x60[i], and sets dirty bit i in Q+0x180 (0x9DF6D2).

A missing key falls back to a default: instanceIndex 0 (the LOCAL player) and
handType 1. The function has no null check and no bounds check, so Lua always
passes all 6 keys for R = 1, using bounded integers.

### The Quiet/Ocelot reset
**[V]** The reset to Snake (0x9CD353..) runs only when the index is the local
index (0x9CD2D5). The receiver needs no bypass. The local Quiet on the guest
uses the existing bypass at 0x9CD370 through the character receipts.

### Warp and the body transform
**[V]** Warp (0x9CEAE0, P vtable +0x1B0) walks this chain:

- blk = `*(P+0x378) + pool*0x6A8`
- CS = `*(*(blk+0x78)+0x238)`
- T = CS.vt+0x388(id), which is 0xAF1130: `*(entry+0x60)+0x10`

It then calls T.vt+0x70(T, &quat) and T.vt+0x68(T, pos, &quat), followed by the
resets `*(blk+0xC0).vt+0x90(id)` and `*(blk+0xA0).vt+0x80(id)`
(0x9CEBD4..0x9CEC32). pos and quat are 16-byte aligned. **T+0x20** is the body
position (0xFFACEA) and **T+0x60** its rotation quaternion (0x10BEF4E).

### Blocks and PS
**[V]** The block ctor stores vtable 0x2295210 (0x9BAC04). The block layout:

- blk+0x138 = P, +8 = id count, +0xC = base id (0x9BAD39)
- `*(P+0x50)[k]` = &blocks[k] (0x9BEA93)
- P+0x58 = the local block (0x9BEAC0)

**[V]** PS = `*(blk+0x78)`, vtable 0x23B09F0 (ctor 0xFEDF58):

- vt+0 = SetStance 0xFF5B10. It sets the stance word `*(PS+0x298)[id-base]`,
  writes Q+0x30[id] = s, and calls `*(PS+0x260)`->vt[0]. PS+0x260 is set
  lazily by a hash lookup (0xFFF346), so the census keeps two verdicts:
  `verdict=` for the block/PS identities plus the transform chain (xform,
  hybrid, the body readback) and `stance=` for the identities plus the stance
  words and PS+0x260 (SetStance, `aset=r`). A missing stance prerequisite
  (`stance=ps_words|ps_sub`) never blocks xform.
- vt+0xC8 = 0xFF3220, the stance request component (lookup by hash
  0xDD151E7A76BA). Lua RequestToSetTargetStance (0x9DB191) calls its
  vt+0x10(X, index, stance).

### Not resolved statically
- **T's class.** The vtable at `*(entry+0x60)+0x10` and the classes of A0/C0 are
  unknown. Native calls through them need two things at runtime: instance 1's
  vtables equal to the local instance's, and slot targets inside .xtext. The
  contract has `t_vtable_rva = a0_vtable_rva = c0_vtable_rva = 0` (unpinned):
  pin them from a reviewed `partner_census` line (experiment A0). The calls
  copy Warp's own call sequence exactly, on the same thread and in the same
  frame phase as the proven Lua Warp. Whether skipping the resets is safe is
  part of experiment M1.
- **Whether these levers produce locomotion animation offline [S].** Every
  remote-follow branch of the action system is gated on S_IS_ONLINE [R], so a
  likely outcome is correct stance idles with a smooth glide.

## 4. What the native side does each tick (instance 1 only, R = 1)

All of it runs on the Lua runtime thread inside the partner tick receipt
(`native/partner_driver.cpp`, unit tested in `tests/partner_driver_tests.cpp`).
Every game write or call first re-reads the whole chain and passes all write
guards:

- P vtable, valid bit, online bit clear, synchronizer null
- count 2, local 0, R = 1, Q vtable, S
- not terminating
- for game calls also: realized, `ids[1] == 1`, parts state 3, and the census
  identities

Each call runs inside SEH. The first fault logs
`partner_call=fault; fn=..; native_calls=disabled` and switches native calls off
for the rest of the mission.

### Movement (`apply=` or `move=`)

| Value | What it does |
|---|---|
| `warp` / `slot` / `both` | As before. |
| `xform` | Writes the S+0x20[1] slot as Warp does, then calls T.vt+0x70 and T.vt+0x68 with (x, y, z, 0) and (0, sin(yaw/2), 0, cos(yaw/2)). No resets unless `reset=a\|c\|ac` (C0 before A0, Warp's order). If the call is refused (unrealized, parts, any identity, native_disabled), the tick sends the Lua WARP op instead, so the partner never stops moving. |
| `hybrid` | `xform`, plus a full Lua Warp on the first placement after a realize, on a body error above 1500 mm, and on a net relink (lost to live). |

A 1 Hz `partner_xform` line compares the previous command with T+0x20/T+0x60.

### Animation (`anim=off|mirror|net`, `aset=q|s|qs|r`)

The source is either the local slot 0 (`mirror`) or the received extension
(`net`). It is applied at stage 3 with the parts loaded:

| `aset` | What it does |
|---|---|
| `q` | Every tick writes u16 Q+0x18[1], u8 Q+0x30[1] and u8 Q+0x38[1]. Research expects this to change only the PlayerInfo record flags and the stance that plugin activation (0x10BED50) caches. Only the live-observed locomotion ids 1..6 are written (`partner::LocomotionAct`); any other act id (CQC, climbing, 0x79 which the PlayerInfo builder special-cases, garbage) is skipped and logged change-only as `partner_aset=q; state=refused; reason=act_domain; act=..`. |
| `s` | When the stance changes and has held for 150 ms, calls SetStance(PS1, 1, s), reads back the stance word (expected s\|s<<4) and Q+0x30[1], and logs `partner_stance=reverted` if the word changes on its own within 500 ms. The stance is committed only when the call succeeds: a refused call (`partner_stance=refused`, change-only per reason) is retried for the same target after 1 s. While instance 1 is unrealized or its parts reload (a look apply), the debounce resets, so the stance is applied again once the parts are loaded. |
| `qs` | Both of the above. |
| `r` | Uses the stance request component instead of SetStance (X1 must have the local X0's vtable). |

### Appearance (`look=copy|net|quiet|snake`)

| `look` | Behaviour |
|---|---|
| `copy` | `CopyPartsInfoFromLocalPlayer`, as before. `parts=explicit` sends the local look once through SET_PARTS. |
| `net` | The partner's look from its look frames. COPY stays the fallback until the first one arrives. A received look is normalized (section 4.1). After 5 s without a look: `partner_look=waiting`. |
| `quiet` / `snake` | Fixed local test looks: Quiet {6, 27, 116, 0, 0, 0}, Snake {0, 1, 0, 1, 0, 0}. |

**When SET_PARTS goes out (op 16).** It is sent once per (look revision,
`look=` / `parts=explicit` re-arm), only when all of these hold:

- stage 3
- Lua has latched all-active
- at least 2 s since the last apply

The reply carries `type, parts, camo, hand, face`. Before the reply, native
writes face_equip to Q+0x68[1]; that is the one field no binding sets. Lua calls
`Player.SetPartsInfoAtInstanceIndex` with all 6 keys, or logs
`event=parts_set_refused;reason=bad_look`.

**Verification.** After an apply the native side waits for the parts state to
cycle (or to stay matched for 3 s), with Q+0x180 bit 1 clear and the slot-1
arrays equal to the look. Then it logs `partner_look=loaded`; after 15 s without
that it logs `partner_look=timeout`. `mirror=on` is refused while look is not
`copy`, because mirror mode copies the local look over instance 1 (0x9CE786).

### 4.1 Look normalization
`partner::NormalizeLook` applies these rules:

- type > 6, or LIQUID (4): refused.
- parts > 27, camo > 116 or hand > 7: refused.
- SNAKE (0): refused (`face`) when face + 3 (face_equip 1 or 2) is 6 or more.
  **[V]** The Snake face package selector 0xAE95D0 (called at 0x9B4565 with
  type, parts, face, face_equip) indexes the 16-byte table at 0x2A827D0 with
  that value and no bounds check (0xAE967D..0xAE9687); the table has 6 path
  entries and index 6 onward are float constants.
- Quiet is forced to parts 27, camo 116, hand 0, face 0, face_equip 0; Ocelot
  to 26, 115, 0, 0, 0.
- AVATAR becomes SNAKE with face 0 and face_equip 0 (`avatar_as_snake=1`): its
  avatar record is neither sent nor settable, and its face id is not a Snake
  face. The record layout, its writers and a fix proposal are in
  [characters.md, "Avatar on the partner's screen"](characters.md#avatar-on-the-partners-screen).
- face_equip is otherwise passed through: **[V]** the package builder only
  compares it (==1/2 at 0xAE962D, ==3/4/5 at 0x9B1035, !=0 at
  0xAE9360/0xAE9220). A DD (1/2) face goes to the staff face database lookup
  (0x9B45B6) unchecked; that lookup was not traced [S].

The receive codec also refuses an animation extension whose flags say valid
but whose stance is not 1..3, and one that carries a stance or speed without
the valid flag (`sync::valid_anim`).

### Census and probes
- `census=1` logs `partner_census` for both blocks (with `stance=` and
  `verdict=`). It is also logged once per realize when a native feature needs
  it.

### Log caps
`partner_anim=source` and `partner_aset=q` share one 64-line cap
(`partner_anim=capped; limit=64`). `partner_stance` has three: set/req (ok),
refused, and reverted, each 64 lines
(`partner_stance=capped; limit=64; kind=set|refused|reverted`). The counters
keep going after the caps in `partner_stance=summary; last=<stance>; sets=..;
reqs=..; refused=..; reverts=..`, logged at the first stance event and then at
most every 10 s while they change. A trigger that changes `anim=` or `aset=`
re-arms the change-only lines.

### Threading
The native calls run inside the partner tick receipt, that is inside
`ObserveEntry` with `receipt_lock` held, on the same thread and frame phase as
the proven Lua Warp. A callee that reached luaL_loadbuffer on this thread
cannot re-enter that lock: the bridge forwards loads issued while its selector
runs without calling the selector (`script_load_bridge.cpp`, thread_local
`selecting`, `tests/script_load_bridge_tests.cpp` selector_reentry).
- `probe=ps` logs the per-id PS fields (+0x740 mask, +0x738 records, +0x280,
  +0x2D0/+0x2D8, stance word) at 4 Hz. It is read-only and its layouts are
  partly [S].
- `partner_anim_probe=1` logs the per-slot `partner_anim` line.

## 5. Character in offline-v1

`native/game_script_observer.cpp` now answers the character receipts in
offline-v1 too (`character::ReceiptsActive`): the query, BYPASS_ON/OFF,
REPORT/READBACK and PLAYERS. Every session receipt stays gated on
host_create/client.

`tools/lab/lab.json` `offline.characters` is `host=default, guest=quiet`, so
`Prepare-LabInstance.ps1 -Offline` writes the guest's `coop.ini` with
`character=quiet`.

Expected guest lines:

```
character_config=quiet; ...; reset_bypass_needed=1
player_type_patch=active
character=apply; ...; applied=quiet; type=6
character=allocated; ...; vars=6
```

## 6. Settings and the one-click test

**`lab.ini` keys:**

| Key | Values (default) |
|---|---|
| `partner_apply` | `warp\|slot\|both\|xform\|hybrid` (warp). Alias: `partner_move`. |
| `partner_anim` | `off\|mirror\|net` (off) |
| `partner_aset` | `q\|s\|qs\|r` (qs) |
| `partner_reset` | `n\|a\|c\|ac` (n) |
| `partner_look` | `copy\|net\|quiet\|snake` (copy) |

`lab.json` `partner` uses the short names (`apply`, `anim`, `aset`, `reset`,
`look`).

**`partner.trigger` grammar:** see [lab-protocol.md](lab-protocol.md). It now
allows up to 127 bytes and accepts `move=`, `anim=`, `aset=`, `reset=`,
`look=`, `probe=ps|off` and `census=1`.

**`Play-OfflineCoop.ps1`** prepares both copies with these settings:

- `partner_stage=3`
- `partner_source=net`
- `partner_look=net`
- `partner_apply=warp`, `partner_anim=off` (the proven movement, no native
  animation calls)

The guest plays Quiet. The unverified native paths stay off until they are
turned on one at a time, so a crash or hang can be attributed to the last
step (each one logs `partner_stage=changed`). `-Experimental` (also
`Play-Offline-Coop.bat -Experimental`) starts with both on (`partner_apply=hybrid`,
`partner_reset=n`, `partner_anim=net`, `partner_aset=qs`). Console keys:

| Key | Action |
|---|---|
| [M] | Smooth movement on/off (`apply=hybrid reset=n` / `apply=warp`) |
| [A] | Stance sync on/off (`anim=net aset=qs` / `anim=off`) |
| [W] | `apply=warp anim=off` (the proven movement, no animation calls) |
| [R] | Re-sends the current settings |
| [C] | `census=1` |

Every key sends the whole line, for example `stage=3 source=net apply=hybrid
reset=n anim=net aset=qs look=net`. The status line shows the character each
copy plays, the partner's loaded look, the current `apply/anim` settings,
movement smooth or Warp (and why), the partner's stance (also from
`partner_stance=summary`), and any native call fault.

## 7. Live experiments (in order) and what is unverified

`Test-PartnerSpike.ps1` gives a PASS / FAIL / INCOMPLETE verdict for these
experiments; `Invoke-PartnerExperiment.ps1` runs their plans.

1. **A0, census** (host). Trigger: `stage=3 source=fixed census=1` after
   `partner_parts=loaded`.
   - Expect: two `partner_census` lines with `verdict=ok` and `same_t=1`.
   - Then: pin `t_vt`/`t68`/`t70` (and a0/c0) in the generator after
     re-verifying them statically.
2. **A1, arrays only** (after M1 passes). Trigger: `stage=3 source=fixed
   apply=hybrid reset=n anim=mirror aset=q`, with `partner_anim_probe=1`. The
   host cycles through its stances. Hybrid places the partner with one Warp and
   then moves it without Warp's resets, which could otherwise mask a pose
   change; `Test-PartnerSpike` notes a per-tick Warp (apply=warp, or an xform
   fallback) in the segment.
   - Expect: `partner_aset=q; state=applied`, and `partner_anim` lines with
     slot 1 equal to slot 0.
   - Visual prediction: no pose change. Report it if the pose does change.
3. **A2, stance at activation.** With `aset=q` and the host crouched, send
   `stage=1`, wait for `partner_realized=0`, then send `stage=3`.
   - Does the partner re-appear crouched?
4. **A3, SetStance** (after M1 passes). Trigger: `stage=3 source=fixed
   apply=hybrid reset=n anim=mirror aset=qs`. The host toggles stance.
   - Expect: `partner_stance=set; from=..; to=..; psw=0x22`, with no `reverted`.
   - Watch the partner's idle stance. A `reverted` without a per-tick Warp
     points at the action plugin: try **A4** with `aset=r`. With a per-tick
     Warp it is not attributable (Warp's resets), and the verdict says so.
5. **M1, xform.** Trigger: `stage=3 source=orbit apply=xform reset=n`.
   - Expect: `partner_xform=applied`, `partner_xform` lines with
     `err_mm < 50` and `yaw_err_mrad < 50`, and no `partner_call=fault`.
   - **Key observation:** do the legs cycle while orbiting, or does the body
     glide? Take 3 screenshots 0.3 s apart.
6. **M2, reset bisection.** Repeat M1 with `reset=c`, then `reset=a`, then
   `reset=ac`.
   - Which reset freezes the pose, and which one stops a snap-back (err_mm
     growing)?
7. **M3, hybrid.** Trigger: `apply=hybrid`. Then force a 10 m jump with
   `partner_offset_cm` and re-trigger.
   - Expect: `partner_hybrid=warp; reason=first|err` only at the start and on
     the jump.
8. **M4, full local mirror.** Trigger: `stage=3 source=fixed apply=hybrid
   anim=mirror aset=qs`. The host walks, runs, crouch-walks, crawls and turns.
9. **P1, PS probe.** If the partner glides, run `probe=ps` while the host moves,
   to find per-id move inputs. The PS+0x738 setters 0xFF5220, 0xFF5710 and
   0xFF56E0 are verified writers of those records.
10. **N1, network animation** (both copies). Trigger: `stage=3 source=net
    apply=hybrid anim=net aset=qs`.
    - Expect: `partner_anim=source; anim=net; valid=1`, then `partner_stance=set`
      from the guest's crouch.
11. **B1, local Quiet** (host). Trigger: `stage=3 source=orbit look=quiet`.
    - Expect: `partner_lua=event=parts_set; R=1; type=6`, then
      `partner_look=loaded; q40=6; q48=27; q50=116`.
    - Then switch to `source=fixed`: does a still Quiet fade (stealth)?
12. **B2, reload cycle.** Trigger `look=snake`, then `look=quiet`.
    - Expect: one `partner_look=apply` per change, with no flicker loop.
13. **C1, the guest plays Quiet.** Title, Continue, Mother Base.
    - Expect: the section 5 lines. The pause menu opens. No infinite load.
14. **B4, network looks** (both copies). Trigger: `stage=3 source=net look=net`.
    - Expect on the host: `partner_net=look_rx; type=6`, `partner_look=apply;
      source=net; type=6`, and a Quiet on screen.
    - Expect on the guest: `look_rx; type=0`, and the host's Snake on screen.
15. **B5 and B6.** Leave the mission and come back (the look is re-applied after
    the reset). Set `net_loss_permille=1000` on the guest: expect
    `partner_look=waiting`, and the COPY fallback stays.

### Unverified until a live run
- Whether xform moves the realized body without Warp's resets, and whether that
  is stable (T's class is unknown statically). Whether locomotion animation
  plays at all for an offline non-local instance.
- Whether SetStance or the request component changes instance 1's visible
  stance, or the action plugin reverts it.
- Whether aset=q has any visible effect.
- Whether a remote Quiet loads in the 2 MB parts block of instance 1, and
  whether its weapons, stealth fade or facial motion look right.
- Whether a local Quiet on the guest is stable in offline-v1 (sounds, pause
  menu, buddy conflicts, equipment).
- Whether calling game code inside the luaL_loadbuffer receipt is always safe.
  It is the same thread and phase as the proven Lua Warp. A callee that loaded
  Lua on this thread would not deadlock (the bridge forwards nested loads
  without the selector, section 4 Threading), but a callee waiting on another
  thread that is itself blocked on the receipt lock would; none is expected.
- Whether a DD (1/2) face id outside the staff database is handled by the
  face lookup at 0x9B45B6 (passed through unchecked).
- The PS probe layouts other than +0x738/+0x740.

## Round 3: input drive, the Snake-load fix, and direct motion

Round 3 (branch `wp/partner-anim2`, implemented in the order L, I, D) acts on
the live round-2 findings: instance 1 is realized, visible and moved exactly by
the smooth mover, but its action system is idle (native `SetStance` is reverted
within 17 ms, `isLocal=0` so it reads no input), and a Snake look never finishes
loading on a guest playing Quiet. Every address below is an RVA, verified
statically against 1.0.15.4 (SHA-256 085c2f82...bb45) by
`tools/generate_partner_contract.py` (now 160 code sites, 38 vtable slots); the
native code gate re-checks all of them in the running image before any read,
patch or call. Nothing here touches S_IS_ONLINE (P+0x204 bit 0x100), the
synchronizer P+0xA8, P+0x214, sessions or the master-server path.

**Defaults keep the proven behaviour.** `input=off`, `isend=0`, `apply=warp`,
`anim=off`, `dm=off`. The one change on by default is the look fix
(`lookgate=1`, `lookrecover=1`), which only affects the `look=net`, fixed-look
and `parts=explicit` flows. Every experimental lever is switchable through
`lab.ini` and `partner.trigger`.

### L. The Snake-look load fix (on by default)

The guest-side stall is a race in our own appearance ops, not a missing pack
(topic 3 of the research). `UpdatePartsStatus` (0x9CD030) requests a slot's
packages once, in parts state 0, from the Q look of that moment, and state 1
then builds the body from the CURRENT Q look (0x9CB200 at 0x9CD49B). If
`COPY_PARTS` (local Quiet) ran first and the remote Snake `SET_PARTS` landed a
tick later while the slot was in state 1, 0x9CB200 keeps failing for a look
whose packages the block does not hold, and the instance stays in state 1
forever (`partner_look=timeout; state=1; q180=0x2`).

- **Gate (A, `lookgate=1`).** `SET_PARTS` goes out only when the game will read
  Q again before it builds the body: realized state 3/4, realized state 0 with
  no look yet (Q parts == 0xFF, 0x9CD6CE), or unrealized state 0/3/4. Otherwise
  it is deferred (`partner_look=deferred; state=N`), so the look lands on the
  first tick it is safe.
- **Withhold COPY (C, `lookgate=1`).** In `look=net`, `COPY_PARTS` is held back
  until the first remote look arrives, or 5 s after all-active
  (`partner_look=copy_withheld`), so the very first request already uses the
  remote look -- a single load.
- **Stall recovery (B, `lookrecover=1`).** A realized instance stuck in state 1
  (`q180&2`, `pb_flag[1]==3`) whose Q (type,parts) differs from the descriptor
  look `*(M+0x1100+8*slot)` (0x9B442D) for at least 1 s gets that descriptor
  look re-sent once (`partner_look=stall` -> `stall_recover` -> `apply;
  source=recover`), which makes 0x9CB200 match the loaded block; the state
  reaches 3 and A then applies the wanted look. `D` (the raw `P+0x90[1]=0`
  write) is rejected.

With `lookgate=0` the behaviour is exactly the old one (L4 uses it to reproduce
the stall and exercise the recovery path).

### I. The input lever (block-1 context window)

The root cause of the idle action system is that instance 1 has `isLocal=0`, so
it reads no input and its stance is reverted. There is one pad object and no
per-id getter, so the working equivalent is a block-scoped context swap: every
per-block phase-8 plugin update of block 1 runs inside the single synchronous
dispatch call at 0x9C2B51 (into 0xBF61D0), after the pad update and after
P+0x218 is recomputed. `native/partner_input_hook.cpp` replaces that call's
rel32 with a near stub to `InputDispatchHook` (`native/near_code.cpp`
`PatchDword`, one locked dword exchange inside a cache line). Around block 1's
call only, with instance 1 realized and every identity re-validated by direct
reads under SEH, the hook swaps in instance 1's own virtual pad (src words and
floats, PP derived mag/force/dir, PP keyboard overrides, PP mask tables), sets
the base action plugin's per-id `isLocal` bit `rec1+0x2AC bit0` (0x10BEDB0),
overrides the hold classifier through a per-window PP-vtable-pointer swap
(`hold=none`, the default: slot +0x90 returns 0, so host holds never reach
instance 1), sandboxes the camera POD fields, and restores everything in reverse
after the call. Movement is camera-relative by rotating the stick yaw by
`cam_cdeg - host_cam_cdeg`. The pure pad logic (`partner_input.cpp`: `Effective`
replicates the PP getters 0x1205F00..0x1206060, `Quantize`, `Compose`) is unit
tested; the hook body is validated statically and by the live matrix.

The driver (`partner_driver.cpp`) arms the window each tick when `input != off`,
`apply` is `input` or `xform`, `anim=off`, `dm != on`, stage 3, the targets
resolve (`ResolveInputTargets`), the hook is installed, and -- for
`apply=input` -- the body has been placed by one Lua Warp. It publishes an `Arm`
through a seqlock, corrects rare drift (`correct_mm`) with an `xform` snap, and
sends the local pad (`isend=1`) as a 68-byte input frame (`player_sync`
`encode_input_frame`, `partner_net` `SubmitInput`/`LatestInput`). A fault in the
window disables native input, the DM patch and the hook for the rest of the
mission. With `apply=input` and no armed window (refused, faulted, hook refused,
`input=off`, stage < 3) the partner falls back to the proven Lua Warp every tick
(`partner_input_place; reason=fallback`), so it never freezes.

Camera POD fields are compared (`cam=detect`) or restored (`cam=sandbox`) over
12 ranges; `CAM+0xB0` and `0x13A..0x150` (pointer-like) are never touched.
`p218=1` is a hazardous diagnostic that also sets P+0x218 inside the window
(gated by `lab.ini partner_input_p218_allow=1` and `cam=sandbox`; it ends at
once if the camera mode changes), auto-expiring after 20 s, to learn whether
locomotion start is P+0x218-gated. Game code pairs P+0x218 with P+0x214 and the
local block P+0x58 (0x11AD05D compares the id with P+0x218, then indexes with
P+0x214 at 0x11AD072; 0xFE8F85 calls the local block's vt+0x20 with id
P+0x218), so inside the window instance 1 can be routed into the host's index-0
data or block-0 per-id slots. Such writes are silent: SEH cannot catch them and
they outlive the expiry. Run I3x last, then quit without saving
(`partner_input=p218; on=1; hazard=index0_routing; restart_after=1`).

### D. The direct-motion lever (minimal, mutually exclusive with I)

One reversible 4-byte patch at 0x1198E25 (`44 0F 44 C7` -> `0F 1F 40 00`,
`native/partner_dm_patch.cpp`) makes the offline DM consumer compare the request
index with the body id (the online semantics), so Lua
`Player.RequestToPlayDirectMotion{{"coopLoco",1},{gani,true,"","","",true}}`
reaches instance 1. `dm=probe` logs the census and a 2 Hz `partner_dm` line;
`dm=on` also applies the patch (first tick at stage 3, parts loaded, input off).
`dmplay=N` plays `GANI[N]` (resident snap ganis in `player2_resident.mtar`),
`dmstop=1` stops it. Open: whether instance 1 actually polls DM slot 0 (about
55-65%; `partner_dm=unconsumed` means it does not). `dm=on` and the input window
are mutually exclusive (`partner_dm=refused; reason=input_active`, change-only,
also while `input != off`; and input arming refuses with `reason=dm`).

The DM entry playback without a locator (0x1198D9D) and DM End (0x11992E9)
write the host camera's connect handle `CAM+0x1F0 = 0` and its flag
`CAM+0x200 = 0` for ANY id, and End clears the pending `g+0x388` bits 0x3C
globally (0x11992FB). Those writes are no-ops only while both camera fields are
already 0, so `dmplay` is refused unless they are (`partner_dm=refused;
reason=cam_connect`). `partner_dm` logs `cam1f0`/`cam200`, and
`partner_dm=conflict` is logged once when the host enters its own DM, or a
camera connect appears, while the partner's DM is active.

### D-map. `dm=map`: the clip follows the other player

The live D series (2026-10-05) proved the lever: instance 1 plays any resident
gani through direct motion while `apply=xform` moves it. `dm=map` turns that
into an animated partner. It patches exactly like `dm=on` (same gates; refused
with `input_active`, `native_disabled` after a fault, or `patch_failed` after a
failed patch, which is not retried until the next `dm=` trigger), then a pure
state machine (`native/partner_dmmap.{hpp,cpp}`, unit tested in
`tests/partner_dmmap_tests.cpp`) picks the clip every tick and sends DM_STOP /
DM_PLAY through the existing op bits. No wire change; the manual `dmplay=` /
`dmstop=` are refused while the mapper owns the direct motion
(`partner_dm=refused; reason=map`, or `reason=map_release` while it releases).

**Clips.** `partner_sync.lua` GANI gains 10 `snapnon_s_wk_lp_vr1_l` (stand walk
loop), 11 `snapnon_s_rn_lp_vr1_l` (stand run loop) and 12 `snapnon_s_jg_lp_vr1`
(stand jog loop), all resident in `player2_resident.mtar` (path hashes
0xfc518eebbaea22fe, 0xfc529e31ab60265c, 0xfc5022ee48683466, each the straight
centre of its TurnSpeed blend in the motion graph). `c_stp_l` was rejected: it
is a one-shot stop/turn clip (siblings `c_135/180/l45/r45_stp_l`, no `_lp`).
`dmplay=` accepts 0..12 (`partner::max_gani` = `kPartnerDmGaniMax`).

**Table** (`dmmap=` / lab.ini `partner_dm_map`, 12 cells, row-major):

| stance \ move | none | walk | run | dash |
|---|---|---|---|---|
| 0 stand | 0 `s_idl_l` | 10 `s_wk_lp_vr1_l` | 11 `s_rn_lp_vr1_l` | 5 `s_dh_lp` (proven) |
| 1 squat | 1 `q_idl_l` (proven) | 3 `q_wk_lp_l` (proven) | 4 `q_rn_lp_l` | 4 (no squat dash exists) |
| 2 crawl | 7 `p_idl` (proven) | 6 `c_fst_lp_r` | 6 | 6 |

Default `dmmap=0,10,11,5,1,3,4,4,7,6,6,6`. Alternatives: stand walk 12 or 3,
stand run 12 or 5, squat run/dash 3 or 5, crawl idle 2.

**Classification.** Stance: the PlayerStatus flags (wire stance on `net`, the
local `tick.stance` on orbit/fixed); when they say stand, the anim stance
(squat/crawl) wins (`stance_via=anim`). Move, first match: Sprinting (wire flag,
or local `move == 3`) -> dash; on `source=fixed` the local move flag 0..2; the
horizontal speed of the commanded pose (newest against the oldest sample within
300 ms, at least 150 ms; a backwards time or a > 20 m/s step restarts it) in
the bands `dmspeed=<idle>:<run>:<dash>` (default 300:2600:5600 mm/s, 150 mm/s
hysteresis); on `net` only, the remote's anim act (odd idle, even moving; speed
3 = run). A lost remote keeps its stance and idles; a stale one freezes the
candidate. Full-stick crouch reads 3.19 m/s (run here) while the game's own
`q38` says walk (findings 2026-10-04): speed wins by design. The orbit source
moves at 2356 mm/s at `offset_cm = 300` (walk; `dmspeed=300:1500:5600` makes
it run, `300:1000:2000` dash).

**State machine** (`Step`, at most one action and one event per tick; one reply
never carries both 128 and 256):

- Reconcile against `q18[1]` every tick: play confirmed (`playing`), never
  confirmed within 2.5 s (`unconsumed`: a strike against the gani, a failure),
  stop confirmed (`q18[1]` left 0x8b and no request pending), stop not
  confirmed within 1 s (`stop_timeout`), the DM unreadable for those windows
  (`dm_read`), a playing clip gone (`lost` with a cause: `parts` reloading,
  `look` op or `warp` in the previous reply, `host` DM, else `ended`). A clip
  that ends on its own within 3 s is a strike; two strikes make a gani bad for
  the mission (`bad_gani`), and the stance's idle cell plays instead, then
  nothing. Failures back off 1, 2, 4, 8, 8 s; five in a row -> `failed`
  (a `dm=map`, `dmmap=` or `dmspeed=` trigger rearms it, [R] in
  Play-OfflineCoop). An unrealize or a teardown that forgets the clip keeps
  `failed` and a running backoff (no replay before the retry time); a clip
  still active when a backoff runs out is adopted only while patched.
- Decide: a new target holds 200 ms and the playing clip at least 300 ms
  before a switch; a switch is a stop, then the play on the tick the stop is
  confirmed. A play waits (`partner_dmmap=deferred`, change-only) for the patch,
  loaded parts, a readable DM, no host DM, no pending request (ours or foreign),
  no camera connect, and a reply without SET_PARTS, an executed COPY, a look
  apply still verifying, SET_ALLACTIVE or WARP. A stop waits for the patch, a
  readable DM, no host DM, no pending request (DM End clears `g+0x388` 0x3C
  globally) and no camera connect. A clip already active when the mapper starts
  (a manual `dmplay`, a late start) is adopted, then replaced after the hold.
- Blocked (`terminating`, `fault` = native calls disabled or an input fault,
  `stage` < 3, `apply` not xform/hybrid, `anim` != off, `input` armed): a
  playing clip is stopped (fault/terminating: nothing is sent), then the
  mapper waits. While a mapped clip is ours the anim arrays are not written
  (`partner_anim=deferred; reason=dm_map`), so its stop can be confirmed.
- Release (`dm` leaves map): one stop, then `released; result=stopped|ended`,
  or `result=timeout` after 1.5 s (at least 1 s after a stop the gate held
  back, so the patch is never reverted under our pending stop); with `dm=off`
  the patch is reverted after the release. `dm=map` again during a release
  keeps the clip in the phase it was in: a play still pending stays
  `play_sent` (confirmed or unconsumed as usual), a play that started is
  `playing` as that gani, a release stop already out is waited for. A teardown (mission start/end, STOP, tick reject, CFG refusal, fault)
  abandons the clip (`partner_dmmap=abandoned`) and reverts at once; as with
  `dm=on` the clip may keep looping into the next state (D3 precedent). The
  input lever stays refused (`reason=dm`) while `dm=on|map` or a release runs.

Constants: hold 200 ms, dwell 300 ms, confirm 2500 ms, stop wait 1000 ms,
release 1500 ms, loop minimum 3000 ms, backoff 1000..8000 ms, 5 failures, 2
strikes.

**Log lines** (CRLF; gani as a number or `none`, `speed_mmps=-1` when unknown):

- uncapped: `partner_dmmap=table; cells=..; speeds=..; reason=start|trigger`
  (first map tick of a mission, and every `dmmap=`/`dmspeed=` trigger in any dm
  mode), `partner_dmmap=failed; failures=5; last=..`,
  `partner_dmmap=released; result=stopped|ended|timeout; to=off|on|probe; ms=..`
- 600 shared, then `partner_dmmap=capped; limit=600; kind=switch`:
  `partner_dmmap=switch; step=stop|play; from=..; to=..; stance=..; move=..;
  speed_mmps=..; via=flags|speed|anim|none|lost|stale; stance_via=flags|anim;
  wait_ms=..` (play: since the stop; stop: how long the new candidate held) and
  `partner_dmmap=playing; gani=..; after_ms=..`
- 64 each, then `partner_dmmap=capped; limit=64; kind=<family>`:
  `deferred; reason=..; phase=..; want=..; cur=..`, `lost; gani=..;
  played_ms=..; cause=..; failures=..`, `stop_timeout; gani=..; ms=..;
  failures=..; backoff_ms=..; reason=stop_timeout|dm_read`, `unconsumed;
  gani=..; ms=..; failures=..; backoff_ms=..`, `adopted; reason=active_clip`,
  `abandoned; reason=..; gani=..`, `bad_gani; gani=..; reason=unconsumed|ended;
  strikes=2`
- every 5 s, 720 lines, then `...; limit=720; kind=summary`: `partner_dmmap;
  phase=..; cur=..; want=..; stance=..; move=..; via=..; speed_mmps=..;
  switches=..; plays=..; stops=..; deferrals=..; timeouts=..; unconsumed=..;
  lost=..; adopted=..; bad=0x..; failures=..; deferred=..`
- `partner_dm=refused; reason=map|map_release|patch_failed|native_disabled`

**Live plan.** D4 (host: Quiet partner, local Snake) and D5 (the same on the
guest: Snake partner, local Quiet, never tested with DM): `stage=3
source=orbit apply=xform reset=n anim=off input=off dm=map
dmspeed=300:2600:5600` (walk), `dmspeed=300:1500:5600` (run),
`dmspeed=300:1000:2000` (dash), back to `300:2600:5600`, then crouch 6 s, prone
6 s and stand on that window, then `dm=off`. FAIL on `partner_dm=unconsumed`,
`partner_dm=conflict`, `partner_dmmap=failed`, any `bad_gani` or more than 3
`stop_timeout`; it needs `partner_dm=patched`, `table`, `switch`, `playing`,
`released` and `partner_dm=reverted`. Visual: the strides match the travel,
squat walk and crawl appear, no camera jump, 0 AV. Live risks: clips 10, 11,
12, 4 and 6 have never played through DM (a T-pose is not detectable natively;
swap cells with `dmmap=`); stand speeds are unmeasured (the summary logs
`speed_mmps`); the stand pose between a stop and the next play is a short
hitch; whether a Warp or SET_PARTS ends a looping clip (`lost; cause=warp|look`);
root motion against xform (about 140 mm under dash); non-locomotion remote
states (vehicle, horse) may read as dash.

### Review fixes (second pass, static)

- `hold=none` and `holdlog` now really run. PP vtable slot -1 (0x23C8D38) is 0
  in the image, so the copy's slot -1 cannot mark "copy ready"; a separate flag
  does.
- A tap is never replayed. The hook keeps its tap, edge and last-sample state
  across Arm generation bumps, and `tap_seq` is process-lifetime in the driver.
  Only instance 1's private mask copy restarts with a new gen.
- `apply=input` relink now fires on lost -> live (the previous status was being
  overwritten before `TickInput` read it).
- `census=1` logs `partner_input_census` even with `input=off`, failing
  verdicts included. The DM census is logged once while instance 1 is not
  loaded, once when it is, and on `census=1`; it never repeats per tick.
- The 1 Hz `partner_input` line logs deltas: `per_s` (normalized to one second),
  `root1_calls`, `skips` and `stale` per interval, `windows` since the arm, and
  `us_max` over the interval. A window with no valid net sample counts as stale
  (`age_ms=none`).
- `TickInput` runs before `TickDm`, so `input=off dm=on dmplay=1` disarms,
  patches and plays in one tick.
- `holdlog=0|1` is applied (it needs `lab.ini partner_input_holdlog=1`; I5
  needs an armed input mode).
- Teardown: `MissionStart` disarms and reverts first, always publishes a
  disarmed Arm, and clears the hook's recorded fault (so a fault in mission 1
  never disables mission 2). A state-guard refusal, a tick parse reject or a
  refused CFG disarms and reverts the DM patch at once. The hook's `Validate`
  also checks count 2, local index 0 and the Q vtable.
- Exceptions. Our own validation/PRE/POST handle only access faults, and they
  restore exactly the fields PRE had saved. The holdlog path validates, honours
  the fault flag and never calls the original twice. An exception raised by the
  game's own update inside the window is recorded (`phase=call`) and passed on
  unchanged; if an outer handler unwinds through the window, a `__finally`
  restores the swapped fields first.
- The Lua-thread pad reads (`isend`, `ResolveInputTargets`) only count when no
  window was open across them (the hook's window sequence is even and
  unchanged; at most ~2 ms wait, then `busy`). They accept only the image PP
  vtable, never the hook's per-window copies. A busy tick keeps an armed
  window's published Arm.
- Input frames are stored only when their sequence is newer (wrap-safe) or after
  a 2 s restart gap; reordered older frames count as `in_drop`.
- The lab `press=` validator rejects leading-zero milliseconds, as the native
  parser does.

### Settings (lab.ini keys and trigger tokens)

`partner_apply` gains `input`. New keys, all inert unless offline-v1 and
`partner=1`: `partner_input` (off|pass|neutral|stick|net, default off),
`partner_input_cam` (off|detect|sandbox, default sandbox), `partner_input_hold`
(none|pass, default none), `partner_input_yaw` (rot|rotneg|none, default rot),
`partner_input_words` (3 of `0prhn`, default 0p0), `partner_input_mask`
(default 0x6 = STANCE|DASH), `partner_input_rstick`/`partner_input_triggers`
(default 0), `partner_input_stick` (deg:pct, default 180:100),
`partner_input_correct_mm` (0..10000, default 1500; 0 = never),
`partner_input_stale_ms` (50..2000, default 250), `partner_input_send`
(default 0), `partner_input_hook` (default 1; 0 forbids the hook),
`partner_input_p218_allow` (default 0), `partner_input_holdlog` (default 0; it
allows the `holdlog=1` trigger and turns it on when set),
`partner_pad_probe` (default 0), `partner_look_gate` (default 1),
`partner_look_recover` (default 1), `partner_dm` (off|probe|on|map, default
off), and for `dm=map` `partner_dm_map` (12 gani indices 0..12, default
`0,10,11,5,1,3,4,4,7,6,6,6`) and `partner_dm_speed` (`<idle>:<run>:<dash>` mm/s,
idle 200..2000, run idle+300..10000, dash run+300..20000, default
`300:2600:5600`); the trigger tokens are `dm=map`, `dmmap=` and `dmspeed=`.
The trigger grammar maximum is now 255 bytes; see
[lab-protocol.md](lab-protocol.md) for the full token list and canonical order.

`Play-OfflineCoop.ps1` keeps its proven defaults; a new console key [I]
(experimental) sends `stage=3 source=net apply=input input=net isend=1
cam=sandbox hold=none look=net` to both copies, [W] adds `input=off isend=0`,
and the status line shows `input=<mode>/<armed>`, windows/s, stale and the last
input fault.

### Live experiment matrix

The lab is offline-v1 at Mother Base 30050 with `partner_anim_probe=1` for every
I/D step. L1-L4 run in `Play-OfflineCoop`'s own flow (baseline
`stage=3 source=net look=net`, guest plays Quiet). I0-I7 and D0-D3 run on the
host (baseline `stage=3 source=fixed apply=xform reset=n` plus the step's own
tokens) except I6 (both copies). D4 (`dm=map` on the host) and D5 (the same
plan on the guest, a Snake partner with a local Quiet) follow D-map above.
`Test-PartnerSpike.ps1` /
`Invoke-PartnerExperiment.ps1` carry the plans and verdicts. A `crash_probe=av`
line with `ctx` bit0 = 1 attributes the crash to the input window; a
`partner_input=fault` means stop, collect the logs, fix and rerun the step. The
precise plan is the one returned alongside this change.

### Still open (settled live)

- Which thread runs 0x9C1F00 versus the Lua tick (I1 measures it; the PP mask
  swap window is about 1 ms).
- Whether `isLocal=1` with a non-local activation path is safe (I2).
- Whether locomotion start is P+0x218-gated (I3, and the I3x diagnostic).
- Camera vtable calls from instance-1 states are not sandboxed; `cam_dirty` and
  the visual checks detect any effect.
- The hold classifier `src->vt+0x10` codes are undecoded; `hold=none` returns 0,
  so the partner never sees host holds (prone needs a later `own` mode).
- DM: id-1 polling (about 55-65%) and the global Stop/End bits. The
  `CAM+0x1F0/+0x200` writes are located (0x1198D9D, 0x11992E9) and gated, but
  only for the play request: a host DM started during the partner's DM is
  logged, not prevented. Only Mother Base 30050 is tested.
- The window itself (validate/PRE/call/POST, the vtable swap, the unwind
  restore) has no synthetic-memory unit test yet; the live I1/I2 steps are its
  first exercise.

## Round 4: reconciliation, stance taps, prone, the host-input leak

Round 4 (branch `wp/partner-anim4`) acts on live round 3
([anim-appearance-findings-2026-10-04.md](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/anim-appearance-findings-2026-10-04.md),
"Live round 3"). Built and unit tested; not yet run live. Every new address is
in the code gate (`tools/generate_partner_contract.py`: now 208 code sites, 65
vtable slots, all re-verified against 1.0.15.4). Nothing touches S_IS_ONLINE
(P+0x204 bit 0x100), the synchronizer P+0xA8, P+0x214, sessions or the master
server.

### What round 3 actually showed (re-read of the live logs)

- **The sawtooth.** The old correction compared T+0x20 with the remote pose
  100 ms in the past (`partner_interp_ms`) and teleported the body with an
  xform (T.vt+0x70 + T.vt+0x68) after 1.5 m held 500 ms. The body ran on its
  own at 3.19-3.20 m/s, so the error reached 1.5 m after about 0.47 s and the
  snap fired at about 0.98 s: `err_mm` 3146 or 3200 (one tick apart), 322
  snaps in 320 s.
- **The body was not following the guest.** The guest stood still (pos0
  constant, 312 of 319 input lines with a zero stick) while slot 1 stayed in
  act=4 s=2 v=2 (crouch-move) from the I2 host-input leak on. The "3.1-3.2 m/s
  matching the guest" was the full-stick crouch-move speed, not the guest's.
  The stuck moving state survived disarming and Warp every tick.
- **pos1/rec1 were not readbacks**: they were our own writes of the remote
  pose into S+0x20[1]. `partner_body` had stopped at its 600-line mission cap.
- **No placement in I6**: `input_placed` was set by the earlier stick run and
  never reset, so switching to input=net did not place the body.
- **Stance press words.** On keyboard the press frame shows held = w4c = w50 =
  0x2, held frames held = w4c, and the release frame only w54. The round 3
  note "only w54" came from 2 Hz sampling.

### R. Reconciliation (`native/partner_recon.{hpp,cpp}`, pure, unit tested)

`apply=input` now runs a placement and correction state machine per Lua tick.
The driver feeds the body readback (T+0x20/T+0x60), the time-aligned target,
the input state and both instances' anim fields, and executes the returned
action:

| Action | Native effect |
|---|---|
| `place` | The window is disarmed for this tick and the Lua Warp (with its resets) goes to the aligned target and remote yaw. The next ticks verify that the body is within 500 mm of the target before arming (3 tries, 250 ms apart; then `placed ok=0`, the Warp fallback, and a retry after 5 s). |
| `nudge` | **T.vt+0x58 only** (0x1AD5660, verified): T+0x20 = pos, foot = pos - (0, T+0x160, 0); no rotation write and no rotation request (T.vt+0x70 is never called); y stays the body's own. At most every 66 ms. |
| `snap`, `pin`, `yaw` | An xform (T.vt+0x70 + T.vt+0x68, no resets) to the target (snap, pin) or to the body position with the remote yaw (yaw). |

**Target.** `SampleRemote(d)` with `d = clamp(in_age - 16 + align, 1, 150)` ms:
the newest snapshot extrapolated by at most about 33 ms, that is the remote
state that the input being consumed produced. The 100 ms interpolated pose is
no longer used by apply=input. `align` (trigger; lab.ini
`partner_recon_align_ms`) corrects a measured along-track bias.

**Decision order each tick:**

1. Placement when needed: the first one, re-arm after any arming refusal, a
   trigger that changes apply/input/source, `place=1`, relink (lost -> live),
   a remote teleport (a jump of more than 5 m), a body jump of more than 3 m
   that we did not cause, far (10 m or more), vertical (2 m or more held
   500 ms), diverging.
2. Runaway: the body moves on its own. Either the remote is idle (below
   150 mm/s and stick below 0.1 for 300 ms) while the body moves above
   800 mm/s for 300 ms, or the remote moves while the body's own velocity
   differs from the remote's by more than 800 mm/s for 300 ms and the filtered
   error is 1 m or more and growing (50 mm or more in 300 ms; a turn or a start
   lags without diverging). The body is pinned to the target every tick
   (`runaway=pin`, the default; `disarm` also disarms the window; `off` logs
   once per episode) until its own velocity matches the remote's again (below
   200 mm/s difference with an idle remote, 400 mm/s with a moving one, for
   500 ms). A moving remote alone never ends it.
3. Input pin: the net input has been stale for 500 ms while the pose is live
   (the window injects a neutral pad, so the body would stand while the remote
   runs). The body is pinned to the pose; far and vertical errors never Warp
   while the input is stale. When the input is live again, tracking resumes
   from the pinned pose with the 700 ms settle (no Warp).
4. Frozen (pose stale, input stale, a non-locomotion act on either side, a
   stance-change suspend, the 700 ms settle after a placement, no body): no
   corrections.
5. Hard snap: `correct` mm or more (default 3000) for 300 ms while moving, or
   2000 mm or more for 1 s and not shrinking while idle; at most one per
   1.5 s. Three in 10 s means diverging: Warp, then pin for 2 s (doubling up
   to 30 s). With the moving runaway rule, snaps are left for discontinuities
   (collision pushes, a body held back by an obstacle for a moment).
6. Idle facing: both still and a yaw error above 25 degrees for 800 ms (gap
   2 s).
7. Nudge: `e_f * (1 - exp(-dt/tau))`, capped at 600 mm/s while moving
   (`partner_recon_nudge_mmps`), 300 idle, 150 prone; dead zone 300/200/400 mm
   (doubled for 1 s after a stance change).

Prone never Warps for a far error (an xform snap instead). The body's own
motion is measured as the readback minus our last write (`own_mmps`); per-tick
motion above 20 m/s is a discontinuity and never counts as velocity.

**Stance.** The remote stance (anim extension) is debounced 150 ms. A change
suspends corrections for 700 ms (1 <-> 2) or 1500 ms (to or from prone);
`stance_mismatch` is logged when Q+0x30[1] still differs 1.5 s later.

**Stance level (net input).** The input carries stance only as edges, so one
lost or extra edge would invert crouch/stand until the remote toggles again.
When the body's stance Q+0x30[1] still differs from the remote's debounced
stance 2 s after the change (both in locomotion acts 1..6, input and pose
live), the reconciliation asks the driver for one corrective STANCE input
through the tap/press overlay: a 4-window tap toward the remote's stance, or a
400 ms hold (12+ windows: classify(1,12) == 3) when the remote is prone. It
then waits 2 s for the result before the next one (`partner_recon=stance_fix`,
`fixes=` in the 1 Hz line). Outside the mask it is refused and logged.

**Slot.** `slot=body` (the default with apply=input): S+0x20[1] carries the
body readback and its yaw (the remote pose until the body is readable), so
`pos1/rec1` in the probe are real readbacks. `slot=remote` is the round 3
behaviour; `slot=none` writes nothing once the body is readable.

**Modes.** `recon=track` (default); `recon=pin` (input-driven animation, body
pinned every tick: the proven smooth mover); `recon=off` or `correct=0`
(placement only, then pure locomotion: the stick diagnostics I3 and R0).

### S. Stance taps and held buttons

- **Words default `hpr`**: w4c = held, w50 = press edge, w54 = release edge,
  each edge for exactly one window. The stance toggle reads PP vt+0x40 (w50,
  masked) when rec+0x361 bit 0x08 is set, else PP vt+0x50 (w54, raw)
  (0x10CE317..0x10CE344, verified). With `0p0` the release-gated half never
  fired.
- **Mask default `0x2200006`**: STANCE 0x2, DASH 0x4, keyboard dash 0x200000
  (the host keyboard's Shift arrives as 0x200000) and keyboard walk 0x2000000
  (binding 0x19).
- **Input frame v2** (76 bytes; v1 68-byte frames are still decoded): an edge
  set with its own `edge_seq` (press, release, and `tap12` = released after a
  run of 1..11 frames), repeated twice for loss tolerance, plus the level
  `long12` (held for 12 frames or more). The sender feeds every tick's
  snapshot, sent or not. The receiver accumulates edge sets by sequence until
  the window consumes them (`partner_net::ConsumeInput`). A tap that was
  pressed and released between two samples is injected as one held window
  (`synth_taps`).
- **Resync after a disarm.** The first main window after a disarm (placement,
  refusal, [W] then [I], mission start) or after a gap of more than 250 ms
  (skips, pass mode) consumes and drops the edge sets that piled up meanwhile
  and injects the current level without edges (prev_held = held; the hold
  history restarts). Stale taps are never replayed as stance toggles.
- **Stalls.** Stale input (older than `stale_ms`, 250 ms) zeroes the stick but
  keeps the last live held level for up to 1 s without edges, and the hold
  history does not grow (a stall never produces a release edge or a prone).
  After 1 s the level is released silently (no release edge) and the history
  restarts.
- **Keyboard dash.** The host keyboard's Shift is binding 21 (held bit
  0x200000), forwarded by the mask and by the key-manager stubs. The `rt=255`
  that `partner_pad` shows with Shift+W comes from the W key: handle
  0x4872B2C95A55 writes both PP+0x484 (T1) = 1.0 and the forward axis PP+0x478
  = -1.0 (0x1206D03..0x1206D32, 0x1206D79..0x1206D8C), so `triggers` stays off.

### P. Prone: the hold classifier (`hold=own`, default)

PP vt+0x90 = 0x1205C30 returns 0 for a masked button (bt [PP+0x28]); with
PP+0x488 bit 0x10 it asks the key manager, else it tail-calls src vt+0x10 =
**0x434ED0** (verified): skip the leading zeros of the newest-first history
*(src+8) (n = src+0x40), count the newest run, stop at `frames` or at the
first gap; released && count < frames gives 2 (tap), newest gives
count < frames ? 1 : 3 (hold), else 0. Prone fires on classify(1, 12) == 3
(0x10CE030, 0x10CE45E).

`hold=own` replicates it exactly (`partner_input::ClassifyHold`) over a
128-entry history of what instance 1 was fed (one entry per main window). It
is installed in the window's vtable copy at +0x90 (with the mask exit) and at
+0x140. `hold=net` uses the sender's `long12`/`tap12` for frames == 12.
`hold=pass` stays diagnostic only (it reads the HOST's history); `hold=none`
returns 0 (no prone).

### C. The host-input leak

- **Key-manager slots.** PP vt+0xE8..+0x138 query the host keyboard through
  *(PP+0x388) with the handles PP+0x390[i] (verified; for example 0x10D50DA
  calls vt+0x100(0x19), the walk key). Every window now installs the vtable
  copy (not only with hold=none), and its slots answer from the injected
  words: pressed (+0xE8, +0x110, +0x118), released (+0xF0, +0x120, +0x128),
  held (+0x100, +0x108, +0x130, +0x138; the handle slots map a handle to its
  binding index with a 48-bit compare, unbound gives 0). +0xF8 (km vt+0x148,
  not decoded) answers 0.
- **PP+0x14** is written as the world heading wrap(host cam + atan2(lx, ly) -
  pi); it was a 0..1 value. PP+0x18 = atan2(lx, ly) of the injected stick.
- **Shadowed PP fields**: PP+0x278 (mouse-look gate, 0x120714E) and PP+0x4B0
  (rebind request, PP vt+0x18 = 0x1206540, consumed at 0x1206A8A). Instance 1
  sees and writes its own copy; the host keeps its value.
- **Camera sandbox** adds CAM+0x150 and the request block: CAM+0xD38,
  +0xD80..0xD97, +0xDB0..0xDBF, +0xDD0..0xDDF and +0xE9C..0xEEB (the setters
  vt+0xE0/+0xF8/+0x100/+0x128 write them for any id).
- **Other walks of block 1's tree (`wrap=census|window`, default off).** The
  base plugin, Move and PS do their per-id work in node vt+0x18 sub-phases,
  reached through the UpdateTree service (vtable 0x2306660: +0x28 active walk
  0xBF6420, +0x30 all-ids walk 0xBF61C0, +0x40 per-id walk 0xBF61E0) and the
  calls 0x9C1DA3 (phase 7) and 0x9C1AFF (phase 11). None of them runs inside
  the 0x9C2B51 window. `wrap=census` installs pass-through wrappers (the three
  service slots by a locked qword swap in read-only data, the two calls by
  rel32 stubs, every old value verified first) and logs a 1 Hz `partner_wrap`
  line: per entry the block-1 calls, the calls on the hook thread, the calls
  nested in a window, the wrap windows and the phases seen, plus the block-0
  calls. `wrap=window` runs the same window (this frame's injection, computed
  once by the main window) around block 1's calls that arrive on the hook
  thread outside a window. Decide it from the census: calls on other threads
  cannot be fixed by swapping. Wrap windows inject the frame's level only
  (`partner_input::LevelOnly`: held words and sticks; no press/release words,
  no net words, no key-manager press/release answers, no tap12), so an edge
  consumer in a wrapped sub-phase never sees an edge twice.
- **Not fixable by a window**: reads of host data by host id or block
  (0x121ED31, 0x12257CD, 0x12257E3, 0x10E14DD) and the look angles PS+0x290,
  which are computed from the host camera quaternion for every id offline
  (0xFFAC41..0xFFAD57).

### Readbacks

`partner_body` (2 Hz while apply=input or an input mode is set, 3600 lines per
mission, emitted outside TickInput's early returns):

- `speed_mmps` is horizontal, from the oldest same-epoch sample of a 32-tick
  ring (about 530 ms at 60 Hz) over the actual span, which must be at least
  150 ms (`na` otherwise; every placement, snap, pin, yaw, Warp fallback or
  disarm starts a new epoch). The round 4 first pass used a 16-tick ring with
  a 250 ms span, which sat on the boundary at 60 Hz and gave `na` on about
  half the lines.
- `own_mmps` is the reconciliation's estimate of the game's own motion.
- `vel_mmps` is the game's per-id velocity PS+0x268, stored at 0xFF23BC as
  T+0x40 / (S+0x38[id] * S+0x48); that it is m/s is inferred.
- `epoch=` and `snaps=`.

### Settings (lab.ini / trigger)

| lab.ini key | trigger | values (default) |
|---|---|---|
| `partner_input_hold` | `hold=` | none, pass, own, net (own) |
| `partner_input_words` | `words=` | 3 of 0prhn (hpr) |
| `partner_input_mask` | `bits=` | u32 (0x2200006) |
| `partner_input_correct_mm` | `correct=` | 0..10000 (3000; 0 = recon off) |
| `partner_recon` | `recon=` | off, track, pin (track) |
| `partner_recon_align_ms` | `align=` | -50..150 (0) |
| `partner_recon_nudge_mmps` | (lab.ini only) | 100..3000 (600) |
| `partner_input_slot` | `slot=` | remote, body, none (body) |
| `partner_recon_runaway` | `runaway=` | pin, disarm, off (pin) |
| `partner_input_wrap` | `wrap=` | off, census, window (off) |
| `partner_facing_dps` | (lab.ini only) | 0..1440 (900; 0 = exact remote yaw). xform/hybrid net facing safety-net rate limit, eased (full rate after 50 ms); the sender publishes the local body-transform yaw; teleport/relink/warp/realize/stale snap |
| (none) | `place=1` | one-shot: a new placement |

### Log lines

- `partner_recon=place; reason=first|rearm|relink|teleport|far|vertical|diverging|manual|retry; via=warp; target_mm=..; yaw_mrad=..; body_mm=..; err_mm=..; delay_ms=..; pose_age_ms=..; in_age_ms=..; tries=..`
- `partner_recon=placed; ok=1|0; err_mm=..; tries=..; ms=..`
- `partner_recon=snap; reason=far_moving|far_idle|far_prone; via=xform; err_mm=..; dy_mm=..; v_free_mmps=..; v_remote_mmps=..; snaps_10s=..`
- `partner_recon=diverging; ...` and `partner_recon=pin_end`
- `partner_recon=runaway; remote=idle|moving; v_free_mmps=..; v_remote_mmps=..; dv_mmps=..; err_mm=..; remote_idle_ms=..; act=..; s=..; v=..; action=pin|disarm|off` and `partner_recon=runaway_end; ms=..; reason=stopped|remote_moving` (both mean: the body's own motion matches the remote's again)
- `partner_recon=input_pin; in_age_ms=..; err_mm=..; reason=input_stale` and `partner_recon=input_pin_end; ms=..; reason=input_live|pose_stale`
- `partner_recon=stance_fix; remote=..; body=..; via=tap|hold; ms=..; fixes=..` or `partner_recon=stance_fix; refused=mask; remote=..; body=..`
- `partner_recon=off; reason=correct0; requested=track|pin; fix=correct=3000` (a trigger left `correct=0`: the reconciliation is off whatever `recon=` says)
- `partner_recon=frozen; reason=pose_stale|input_stale|act|remote_act|stance|settle|body` and `partner_recon=unfrozen`
- `partner_recon=stance; from=..; to=..; suspend_ms=..` and `partner_recon=stance_mismatch; remote=..; body=..; ms=..`
- `partner_recon=yaw; err_mrad=..` and `partner_recon=stop_check; ok=1|0; ms=..; v_free_mmps=..`
- `partner_recon=refused; action=..; reason=..; fallback=none|place` (change-only)
- 1 Hz: `partner_recon; mode=..; recon=off|track|pin; err_mm=..; dy_mm=..; along_mm=..; cross_mm=..; v_free_mmps=..; v_remote_mmps=..; dv_mmps=..; body_mm=..; target_mm=..; yaw_err_mrad=..; nudges=..; nudge_mm=..; snaps=..; places=..; pins=..; yaws=..; act=..; s=..; rs=..; frozen=..; delay_ms=..; pose_age_ms=..; in_age_ms=..; in_r=..; stop_ok=0|1|na; runaway=0|1; input_pin=0|1; fixes=..` (3600 per mission). `recon=` is the effective setting; `snaps` and `places` count since the reconciliation started (a diverging re-placement keeps the snaps that caused it), nudges/pins/yaws since the last placement.
- `partner_input` (1 Hz) adds `words=..; hold=..; press_n=..; release_n=..; synth_taps=..; h=c0,c1,c2,c3` (the hold classifier's answers for btn 1) `; km=..; shadow=..`
- `partner_wrap; mode=census|window; svc28=calls,same,nested,windows,0xphases; svc30=..; svc40=..; walk7=..; walk11=..; r0=..; other=..` and `partner_input_wrap=installed|refused; ...`
- Removed: `partner_input_place; reason=err|first|relink`. Kept: `partner_input_place; reason=fallback`.

Each event family has a 64-line cap; the counters continue in the 1 Hz line.

### Play-OfflineCoop

`tools/lab/Play-OfflineCoop.ps1` now starts with the input drive on both
copies: `stage=3 source=net apply=input anim=off input=net cam=sandbox
hold=own words=hpr bits=0x2200006 isend=1 recon=track correct=3000 runaway=pin
slot=body look=net`, also written into lab.ini through
`Prepare-LabInstance -Offline -PartnerSetting` (`partner_input_correct_mm=3000`,
`partner_recon_runaway=pin`). `correct=3000 runaway=pin` are explicit because
`correct=0` (R0, I3) turns the reconciliation off and a trigger only changes
the keys it names.

- [W] goes back to the proven Warp movement (`apply=warp input=off isend=0`).
- [I] re-sends the input drive; [P] sends it with `recon=pin`.
- [M] and [A] are the round 2 experiments.
- `-ProvenWarp` starts on the proven path; `-Experimental` starts with the
  round 2 experiments.

Since the live two-copy pass of 2026-10-05 (run 20261005-080408,
[anim-appearance-findings-2026-10-04.md](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/anim-appearance-findings-2026-10-04.md))
the default is `dm=map`; `-ProvenWarp` and [W] give the proven Warp movement
(every non-map line carries `dm=off`), `-InputDrive` starts the input drive.
The default, `-Animated` and [D] start `dm=map` (`stage=3 source=net apply=xform reset=n anim=off
input=off isend=0 dm=map look=net`; lab.ini `partner_apply=xform`,
`partner_reset=n`, `partner_dm=map` on both copies), and the status line then
shows `clip <name>` from the 5 s `partner_dmmap` summary (`FAILED` after
`partner_dmmap=failed` and while the summary says `phase=failed`; [R] re-sends
and rearms; cleared by `partner_stop`, `released`, `abandoned`, a stage below 3
and an idle summary).

The status line adds `input <mode>/<armed>`, `recon <mode> err <cm> snaps ..
places ..` (`recon OFF` when `correct=0` turned it off), `RUNAWAY(pinned)`
(only for runaway=pin|disarm), `INPUT-LOST(pinned)`, `stance-fixes ..`,
`frozen <reason>` and the body stance.

### Review fixes (round 4, second pass, static)

- [I]/[P] and the I6 plan set `correct=3000 runaway=pin` (finding: a
  `correct=0` from R0/I3 turned the reconciliation off silently; the 1 Hz line
  still said `mode=track`). The 1 Hz line has `recon=` (effective) and a
  trigger that leaves `correct=0` with `recon=track|pin` logs
  `partner_recon=off; reason=correct0`.
- Stale edge sets are dropped on the first window after a disarm or a gap
  (resync), and a stall keeps the held level without edges (no extra toggle).
- Stance level correction (`stance_fix`), the moving-remote runaway, the
  input pin (no far-Warp cycle while the input is stale), level-only wrap
  windows, cumulative snaps/places counters, a latched `runaway=off` event.
- `ApplyTrigger` compares apply/input/source before overwriting them, so a
  source-only or apply-only change re-places.
- The per-tick Warp fallback uses the proven 100 ms interpolated pose again
  (only the placement uses the aligned target).
- `partner_body speed_mmps` from a 32-tick ring with a 150 ms minimum span.
- Rejected: keyboard sprint through `rt` (see S: `rt=255` is the W key).
- Tests: a lag model (120 ms input latency, 10 vs 8 m/s^2), a stuck body with
  a moving remote, the input pin, non-sticking nudges, the stance fix, resync
  and stall rules, level-only wrap windows, source/apply re-placement, the
  fallback pose, `speed_mmps` at 60 Hz.

### Still open (settled live)

- Whether the stuck moving state recurs from a fresh mission (the R0 stop
  check), and whether its source is a sub-phase outside the window (the wrap
  census). Until then the runaway pin (idle or moving remote) keeps the
  partner smooth.
- What a STANCE tap does from prone (crouch or stand): the stance fix taps
  and retries every 2 s, so either converges; R5 records it.
- Whether position-only nudges (T.vt+0x58 zeroes T+0x1A4/T+0x1F0 and clears
  T+0x1A0 bit 2) disturb grounding at up to 15 Hz: watch for vertical hitches
  (`dy_mm`) while `nudges` grows.
- Whether instance-1 sub-phases arrive on the hook thread (the wrap census):
  only then can `wrap=window` close the gap.
- The meaning of km vt+0x148 (PP vt+0xF8 answers 0 inside the window).
- The along-track timing bias (`align=`), measured from the 1 Hz line.
- Steering (a stick bias towards the target) is not implemented: nudges carry
  the correction.
