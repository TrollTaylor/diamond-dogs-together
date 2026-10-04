# Partner animation, movement and appearance (offline-v1, experimental)

This page covers the work on branch `wp/partner-anim`, which builds on the offline
partner of [offline-partner-spike-plan.md](offline-partner-spike-plan.md) and
[testing-2026-10-04.md](testing-2026-10-04.md). It has three goals:

1. The partner animates like the real remote player (stance, locomotion,
   facing) and moves smoothly.
2. Each copy shows the other player's own character, with the test condition
   that the guest plays Quiet.
3. The character system works in offline-v1.

Everything here is fully offline. Nothing touches S_IS_ONLINE (P+0x204 bit
0x100), the synchronizer P+0xA8, a game session or the master server.

**Status: built and unit tested; nothing in this page has run live yet.**
Section 7 lists what still has to be proven in the game.

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

`tools/lab/Play-OfflineCoop.ps1` opts in to the experiment settings (section 6).

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
  writes Q+0x30[id] = s, and calls `*(PS+0x260)`->vt[0].
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
| `q` | Every tick writes u16 Q+0x18[1], u8 Q+0x30[1] and u8 Q+0x38[1]. Research expects this to change only the PlayerInfo record flags and the stance that plugin activation (0x10BED50) caches. |
| `s` | When the stance changes and has held for 150 ms, calls SetStance(PS1, 1, s), reads back the stance word (expected s\|s<<4) and Q+0x30[1], and logs `partner_stance=reverted` if the word changes on its own within 500 ms. |
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
- Quiet is forced to parts 27, camo 116, hand 0; Ocelot to 26, 115, 0.
- AVATAR becomes SNAKE (`avatar_as_snake=1`): its avatar record is neither sent
  nor settable.

### Census and probes
- `census=1` logs `partner_census` for both blocks. It is also logged once per
  realize when a native feature needs it.
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
- `partner_apply=hybrid`
- `partner_anim=net`
- `partner_aset=qs`
- `partner_look=net`

The guest plays Quiet. Console keys:

| Key | Action |
|---|---|
| [R] | Re-sends the experiment settings |
| [W] | `apply=warp anim=off` (the proven movement, no animation calls) |
| [C] | `census=1` |

The status line shows the character each copy plays, the partner's loaded look,
movement smooth or Warp (and why), the partner's stance, and any native call
fault.

## 7. Live experiments (in order) and what is unverified

`Test-PartnerSpike.ps1` gives a PASS / FAIL / INCOMPLETE verdict for these
experiments; `Invoke-PartnerExperiment.ps1` runs their plans.

1. **A0, census** (host). Trigger: `stage=3 source=fixed census=1` after
   `partner_parts=loaded`.
   - Expect: two `partner_census` lines with `verdict=ok` and `same_t=1`.
   - Then: pin `t_vt`/`t68`/`t70` (and a0/c0) in the generator after
     re-verifying them statically.
2. **A1, arrays only.** Trigger: `stage=3 source=fixed apply=warp anim=mirror
   aset=q`, with `partner_anim_probe=1`. The host cycles through its stances.
   - Expect: `partner_aset=q; state=applied`, and `partner_anim` lines with
     slot 1 equal to slot 0.
   - Visual prediction: no pose change. Report it if the pose does change.
3. **A2, stance at activation.** With `aset=q` and the host crouched, send
   `stage=1`, wait for `partner_realized=0`, then send `stage=3`.
   - Does the partner re-appear crouched?
4. **A3, SetStance.** Trigger: `... anim=mirror aset=qs`. The host toggles
   stance.
   - Expect: `partner_stance=set; from=..; to=..; psw=0x22`, with no `reverted`.
   - Watch the partner's idle stance. On revert or pop, try **A4** with `aset=r`.
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
  It is the same thread and phase as the proven Lua Warp. No callee is expected
  to compile Lua, which would deadlock the receipt lock.
- The PS probe layouts other than +0x738/+0x740.
