# Anim round 2, track B (native throws, places, arm): BLOCKED

Branch `wp/anim-throws` (base `ea0ea33`). Status: **blocked, nothing to merge, do not schedule its hidden test**
(`partner-support-native.txt` does not exist and the `partner_native_support` key was not added).
Static analysis of `mgsvtpp.exe` 1.0.15.4 only; all addresses are RVAs. No code changed in this round.

## Why it is blocked

1. **Stock guard step stopped.** The design needs instance 1's support classes to take the game's own non-consuming
   (remote) state before slot 3 can ever receive HOLD/FIRE (review finding 1/5, verified below). The step that would add
   that as a new code patch was stopped by a safety classifier during implementation. Per the hard rules it was not
   retried or worked around. It needs a decision by the user or coordinator.
2. **Body 1 cannot hold support equipment.** No ChangeEquip writer exists for slot 3/6. ChangeEquip `0x11AC2E0` maps
   types 12/13 to slot 3 (`0x11AC4B7..0x11AC4C4`) and 14 to slot 6. For slot 3 it searches the sub index through ctl
   vt+0x90, then calls SetCurrentSlot (vt+0x10) and ctl vt+0xA0. Its stock handling for a non-local id is unaudited, and
   the support stock is shared (no instance id), so a writer is its own native-call qualification task. Without it the
   follower refuses `reason=empty` and any slot-3 gate could never open.

Until both are resolved, Round 1 behaviour stays: slot 3/4/6 equipment means `Gate::unsupported`, strip `0x58`.

## Review findings: verdicts

| # | Finding | Verdict |
|---|---|---|
| 1 | Owner filters do not make body-1 throws/places side-effect-free | **Real for throws, wrong for places** (details below). Fix: slot 3 stays unsupported; no gate change. |
| 2 | Placed caller 0xC0938D missing (owner 0) | **Real.** Recorded here; no caller list was ever committed, so there is nothing to correct in code yet. |
| 3 | Wrong rax-flow justification | **Real.** The correct proof is recorded below for the future filters. |
| 4 | Track B not implemented | **Real.** Resolved as the reviewer's alternative: this doc commit records the track as blocked. |
| 5 | Plan omits the shared support stock spend and the ammo-leak latch | **Real for throws** (same mechanism as 1). |

### Throws spend the viewer's stock (findings 1, 5)
- Mode 37 (`0x1087210`) op 10 (`0x10879BF`), offline and for any id:
  - the thrown request at `0x1087BF2`;
  - the call `[[prim+0x60]] vt+0x160(rec+0x240)` at `0x1087C05`, which is not yet identified;
  - then, unless rec+0x3C0 bit 29 (`0x1087C0B`) or a count >= 0x3FFF (`0x1087C39`): the decrement into
    rec+0x25C..0x262, ctl vt+0xB0, the stock setter ctl vt+0xC0 = `0x11AF490` (`0x1087CF0`), message 0xAAE7433A at
    count 0 (`0x1087D99`) and ctl vt+0x110.
- The setter writes store vt+0x78 and u16 `[ctl+0x20]+0x614+idx*2`. Neither takes an instance id.
- For classes 2/3, the ammo copy `0x108C8C0` (its only caller is `0x103E08C`) refreshes body 1's counts every frame
  from the shared stock: store vt+0x68 = `0xEE90A0` maps slot 3 to sub+6, then vt+0x88 reads the count
  (`0x108CB57..0x108CC38`).
- Offline, bit 29 comes from the global P+0x264 bit 3 (`0x108CC5F`). The remote branch (`0x108C8FC`/`0x108C907`:
  counts 0xFFFF and bit 29) is taken only with S_IS_ONLINE.
- The op-4 throw gate (`0x1087586`) needs bit 29 or rec+0x25C != 0.
- Within the throw-family handlers (modes 28..42), ctl vt+0xC0 is called from exactly three functions in
  `0x1000000..0x1100000`: mode 37 (gated as above), mode 46 (local-only, below) and the class-1 ammo setter `0x10589E0`.
  A direct-call closure (depth 5) of modes 28..42 never reaches `0x10589E0`.

### Places are local-only for instance 1 (finding 1, mode-46 part rejected)
- Mode 46 (`0x107BAC0`) op 9 (`0x107C6FA`) starts with `test byte [rec+0x3C0], 1; je 0x107CC23`, which skips the whole
  placement block: the placed request `0x107CA33`, message 0x182DB6BB (`0x107CA4C`), vt+0x2D0, the decrement and the
  stock setter (`0x107CA6A..0x107CBFE`).
- rec+0x3C0 bit 0 is rewritten every gun phase as `id == P+0x218` (`0x10540A2..0x10540B8`), so it is always clear for
  instance 1.
- Mode 47 (C4 detonation) op 10 calls `0x103D040` only behind the same bit (`0x107F2DE`).
- The review's mode-46 decrement and 0x182DB6BB claims therefore do not apply to body 1. The round-1 doc's "no locality
  test on this path" was wrong as well.
- What does run for instance 1 offline: `0x1021000` (`0x107CC68`), which is unaudited.

### Placed callers (finding 2)
A gm-rooted dataflow scan finds exactly these `*(*(*(0xBFF050())+0x98)+0x208)` vt+0x48 call sites (`.xtext`):
- `0x107CA33` (mode 46, local-only);
- `0x15D9352` (fn `0x15D9230`, owner from its object record +0x16);
- `0xC0938D` (fn `0xC09180`, vtables `0x2309A10`/`0x2309AB0`): r14 = gm+0x208 at `0xC09252`, and `0xC09364` stores
  dword 0x1AD at request+0x30. That is equip 0x1AD with owner word +0x32 = 0, so it is not a player placement.

Any future drop rule must compare the full 16-bit owner with the armed id.

### Thrown drop value (finding 3)
- `0x1041000` is the gun iface phase switch, not a consumer.
- The current mode handler is a member pointer at rec+0x2B0. Op 10 is dispatched only at `0x103EBF4` (phase 6
  `0x103EB70`), and rax is overwritten at `0x103EBF6`. Op 12 goes through `0x105295C` (`0x10528F0`).
- The op-10 entries:

  | mode | handler | op-10 entry |
  |---|---|---|
  | 37 | `0x1087210` | `0x10879BF` |
  | 38 | `0x1068C00` | `0x106900D` |
  | 30 | `0x1087FC0` | `0x108834D` |
  | 35 | `0x10896B0` | `0x1089A11` |
  | 40 | `0x108A130` | `0x108A459` |
  | 33 | `0x108A680` | `0x108AB10` |

- The preview helper `0x1061D90` (thrown request with +0x3E bit 0) is called only from op 10 of these six handlers.
- Only modes 37 and 33 have an op 12, and neither op 12 reaches a `0xBFF050` call or the helper. Modes 30/35/38/40 have
  11-entry op tables.
- Independently, the thrown request `0x15E0230` itself returns 0 when its pool yields no object (`0x15E02B5`), so 0 is
  inside its own return domain.

## Related observation (outside this track)
`0x108BD40` (gun phase 0 `0x103E070`, flags27c bit 19 on a pending switch) calls the class-1 ammo setter `0x10589E0`.
That setter has no locality test offline and writes the shared stock through ctl vt+0xC0. The ammo-leak digest would
catch it (`partner_weapon=fire_reload_off; reason=ammo_leak`). Worth a look by the weapons track.

## Next steps (need a decision)
1. Decide how instance 1's support classes may get the non-consuming state. The patch step was blocked; it needs user
   approval or another approach chosen by the user.
2. Qualify a slot-3/6 ChangeEquip writer for instance 1 that does not touch the shared support stock.
3. Then add the owner filters (`partner_native_support`, default off), gates and telemetry, with these pins.

## Attempt 2 (2026-10-15, after the user approved the native route): blocked again

The user approved option (a) (the native route with the stock-guard patch). The implementer was stopped by a safety
classifier while writing a read-only static capstone scan (`scratchpad/anim/throws3/writes.py`, listing memory writes
and camera/P loads in gun modes 28..47 reachable for instance 1). Per the rules it stopped; no code, contract, key,
scenario or build changed. Static results from before the stop (not yet pinned):

- **Stock guard design.** `0x108C8C0` (only caller: gun phase 0 at `0x103E08C`; rcx plugin, rdx rec, r8d id; r9 = P
  from `[[rcx+8]+0x138]` at `0x108C8D7`). Its remote branch only writes qword `[rec+0x25C] = -1` (counts 0xFFFF) and
  `[rec+0x3C0] |= 0x20000000` (bit 29), then returns. Proposed site: the dword at `0x108C8E5` of
  `mov eax,[r9+0x204]` (`41 8b 81 04 02 00 00`) -> `eb 9f cc cc` (jmp rel8 to the int3 padding `0x108C888..0x108C88F`,
  then a near stub). The stub must re-execute the load and force the branch only for (id 1, P, the hook thread,
  support class `(rec+0x27C>>6)&7` in {2,3}); for a class-1 gun the remote branch would overwrite the magazine mirror.
- **Closure.** A whole-image direct-call graph plus ctl-tagged vtable calls gives 47 stock-reaching functions (ctl
  slots vt+0xB8 `0x11AF530`, vt+0xC0 `0x11AF490`, vt+0x100 `0x11AF0B0`). With the local bit clear, the online bit
  clear and bit 29 set, no handler of modes 28..47 or 52..56 reaches a stock write (without the guard the method finds
  mode 37's own setter call at `0x1087CF0`). Stock layout: store vt+0x88 `0xEE9050` reads `Y+0x98[k]`, vt+0x78
  `0xEE9730` writes it, the setter also writes `[ctl+0x20]+0x614+2k`; vt+0x68 `0xEE90A0` maps slot 3 to k = 6+sub,
  slot 4 to 0xE+sub, slot 6 to 5.
- **0x10589E0** (class-1 ammo setter) cannot be reached by a support-class record of id 1 (bit 19 is set only by
  `0x1059B3F` for the bit-20 0x42B..0x42E guns; `0x108BD40` clears it before activation).
- **Drop values.** `0x15E0230` returns 0 when its pool is empty (`0x15E02B5`), so 0 is safe. `0x1337760` always
  returns the pool object (through `0x1336110`), so 0 is outside its return domain, but the six known callers
  (`0x130369F`, `0xBD6B19`, `0xBDF09E`, `0x1942D17`, `0xBDDF02`->`0xBDCE4E`, `0x1726248`) never dereference it.
- **Equip path, slot 3, non-local id.** SetCurrentSlot `0x11AFA80` writes Q+0x17C, row+0x36/0x37 and
  row+0 = row[0xC+2*sub], then ctl vt+0xA0 `0x11B0700` -> vt+0x18 `0x11B0900` -> Y vt+8 `0xEE9880` (allocates the
  handle into `Y+0x40[id*stride+3]`, light path for non-gun types) plus the block notify (blk vt+0x180 -> vt+0x20);
  these callees are not yet qualified. ctl vt+0x98 `0x11AFF30` writes row+0xC+2*sub per id (vars+0x584 only for the
  local id); vt+0x90 `0x11AD420` is a pure read; vt+0x1F0 `0x11B1B60` (non-local) calls vt+0xA0(6) and vt+0x98, and
  maps ids outside 515..521 to 519.
- **Rocket arms are not visual-only (correction).** Mode 53 (`0x107FA30`) op 3 sets rec+0x2C2 = 3 and the shared plugin
  field `[r14+0x350]` bit 7 for any id; op 4 in state 3 queries the rocket service (vt+0x68 `0x1339700`) and can set
  `[r14+0x350] |= 0x200`, write the viewer's camera P+0xB8 (+0x234, +0x2DC, vt+0x128 twice) or reach projectile vt+0x70
  `0x1339CF0` through `0x10951A0`. **All of slot 6 stays Gate::unsupported** (the camera part is the camera agent's area).
- **Still unchecked (the blocked step):** shared plugin fields / camera writes in modes 28..47 for instance 1, the slot-3
  equip callees above, and modes 54..56.

## REA/Ghidra audit (2026-10-15, user-suggested route): the scope that is safe to build

At the user's suggestion the remaining static analysis was done with the REA CLI over Ghidra 12.1.3 (read-only,
per-function decompilation; no custom scanners). Full notes: `docs/evidence/rea-throws-2026-10-15/{throw,place,equip}-audit.md`.
No safety classifier stopped these runs.

- **Throw family (modes 28..42; table initializer `0x1420252F0`: 28/29 = `0x1089190` hold hub, 30/31 = `0x1087FC0`,
  32 = `0x1088A20` draw/holster, 33/34 = `0x108A680`, 35/36 = `0x10896B0`, 37 = `0x1087210`, 38 = `0x1068C00`,
  39 = `0x1088530`, 40 = `0x108A130`, 41/42 = `0x1089BA0`).** Under the guard (rec+0x3C0 bit 29) mode 37 op 10 skips
  every stock/ctl/message write. No opened throw code writes the camera P+0xB8, plugin fields, globals or the HUD; all
  other stores go to rec, stack requests or id-indexed slots. Body 1's throw aim *reads* the viewer's camera offline
  (`0x1042F80`, `0x1093620`): a direction coupling, not a write.
- **The preview slot.** `0x1061D90` posts an arc-preview request (+0x3E bit 0, owner +0x32 = id) every op-10 tick for
  any id offline; `0x15E0230` copies previews into ONE shared record at thrown-service+0x100 (`0x15E023A..0x15E0257`).
  **The owner filter must run at the `0x15E0230` entry**, before the preview branch, so body 1 neither creates a real
  object nor overwrites the viewer's preview.
- **Unresolved, to be watched at runtime:** `[plugin+0x60] vt+0x160(rec+0x240, flag)` at `0x1087C05`, `0x10880D3`,
  `0x108AA91` (likely a per-item "pin pulled" toggle keyed by body 1's own equipment handle in rec+0x240), and
  `G vt+8` on message `0xB6F952A5` in modes 28/32/43/45. Four helpers have no Ghidra function
  (`0x105F8A0`, `0x103AE60`, `0x103A460`, `0x108FAD0`).
- **Place family (43..47).** Placing and detonating are local-only for id 1 (confirmed), but mode 46 op 1 writes the
  shared `plugin+0x350` bit 3 and `plugin+0x3E8` for any id offline (`0x107BD64..0x107BD8B`). **Placed types (13) stay
  Gate::unsupported.**
- **Arms (slot 6).** Mode 53 writes shared plugin and camera state. **Slot 6 stays Gate::unsupported.**
- **Equip (slot 3, type 12/13).** ChangeEquip `0x11AC2E0` for id 1 writes only instance 1's row, its inventory
  entries `Y+0x40/0x50[1*7+3]` and one entry of the shared non-gun handle ring; vars, the shared stock, HUD and
  instance 0 are untouched provided P+0x214 != 1, **P+0x218 != 1** (the lower writer `0x1209EC0` tests P+0x218, so the
  lab p218 window must stay off), Y+0x30 != 1, P+0x70[1] == 1, P+0x78[1] != 0xFF, the restriction predicate
  `0x1156640` is 0, flag bit 2 set and bit 4 clear. The model binder `0xB0C720` does nothing for slots 3/6.

**Build scope (v1):** thrown support weapons only (type 12: grenades, smoke, stun, sleep gas, magazines): stock guard
+ thrown owner filter at the entry + slot-3 type-12 equip/follow + gate; placed types and arms keep Round 1's
`Gate::unsupported`.
