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
