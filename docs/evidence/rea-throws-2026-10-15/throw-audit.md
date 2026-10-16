# Throw-family side-effect audit (body 1, offline, non-local, rec+0x3C0 bit 29 set)

mgsvtpp.exe 1.0.15.4 (sha256 085c2f82...). All addresses are RVAs (Ghidra = 0x140000000 + RVA).
Method: REA CLI 6.5.0 only, `function` with the Ghidra snapshot, one call at a time (~3-4 min each).
Every `fn-XXXXXXX.json` / `fn-XXXXXXX.c` in this folder is the raw REA output / extracted pseudocode.
Call-site instruction addresses come from REA's `native_value_flow.operations` (raw pcode, CALL/CALLIND
with the INT_ADD vtable offset at the same address).

Tags: **[O]** = observed in the REA decompilation / pcode; **[I]** = inferred.

Conditions assumed throughout: id = 1, P = [[plugin+8]+0x138], offline (P+0x204 bit 8 clear),
rec+0x3C0 bit 0 clear (non-local), rec+0x3C0 bit 29 set. rec = plugin+0x58 + (id - [[plugin+0x38]+0x24])*0x610
(every handler computes it from the id, it is not passed in) [O].

## Tool limits hit (not blocked by a classifier; REA limitations)

- `read-bytes`, `search-bytes`, `resolve-native-call-targets`, `inspect-native-instruction` do NOT accept
  `--snapshot` ("Unknown flag: --snapshot" for read-bytes/search-bytes). Without a snapshot they would re-run the
  full Ghidra analysis, so they were not used. Consequence: vtable slots cannot be read, so no virtual call target
  could be resolved (`xrefs 0x11AF490` returns only 0x9F5598C, i.e. no vtable data refs exist in this snapshot).
- Four direct callees have **no Ghidra function** at their address (decompiler prints `func_0x...`;
  `function`/`instructions` on 0x105F8A0 return "No procedure exists at the requested address"):
  `0x105F8A0`, `0x103AE60`, `0x103A460`, `0x108FAD0`. They could not be opened.

## Handlers (decompiled, all ops)

| mode | handler | op-10 behaviour (offline, any id) |
|---|---|---|
| 37 | 0x1087210 | preview 0x1061D90 if rec+0x2C2==0 (call 0x10879ED); throw block if rec+0x2C2==1 |
| 39 | 0x1088530 | no op 10 (switch has cases 0-6 only) |
| 30 | 0x1087FC0 | preview only (0x1088376) |
| 33 | 0x108A680 | preview only (0x108AB39) |
| 35 | 0x10896B0 | preview only (0x1089A3A) |
| 38 | 0x1068C00 | preview (0x1069032); if rec+0x3CC bit 19: 0x1053270 (0x1069061) |
| 40 | 0x108A130 | if rec+0x3C0 bit 1: 0x1053270 (0x108A46B); preview (0x108A49D) |

Preview gate in every handler: `((P+0x204>>8)&1)==0 || id==P+0x218` [O] -> always true offline, for any id.

### Mode 37 op 10 (0x10879BF), decompiled lines 270-408 of fn-1087210.c
1. `0x10879ED` CALL 0x1061D90 when rec+0x2C2==0 [O].
2. rec+0x2C2==1: `if (rec+0x3C0 & 1) [[P+0x48]] vt+0x2D0()` at 0x1087A1A -> **skipped for body 1** [O].
3. rec+0x2C2 = 2 (0x1087A20, rec) [O].
4. Offline -> builds the request on the stack; reads per-id `[[plugin+8]+0x78]+0x280[id]` bit 22 and
   `[[P+0x60]+0xD8/0xE0][id]` (reads) [O]; owner word +0x32 = id & 0x1FF (`uStack_a6`, base `uStack_d8`) [O].
5. 0x1087B11 CALL 0x1037BA0 and 0x1087B23 CALL 0x1039020 (stack outputs only, see below) [O].
6. 0x1087B3C M vt+0x388(id) where M = [[plugin+8]+0x78]+0x238: result only read (+0x194,+0x1A0,+0x20..0x2C) [O]; getter [I].
7. 0x1087BD8 CALL 0xBFF050 (returns [0xBFEFA0()+8]) [O]; 0x1087BF2 `[[gm+0x98]+0x200] vt+8(&req)` = thrown request [O].
8. **0x1087C05 `[plugin+0x60] vt+0x160(dword rec+0x240, 0)`** - runs before and independent of bit 29 and of
   locality [O] (pcode CALLIND inputs: target, this, 4-byte value, constant 0).
9. bit-29 test (0x1087C0B, decompiled `if ((rec+0x3c0 & 0x20000000) == 0)`) encloses: count decrement into
   rec+0x25C..0x262 (0x1087C70..C90), ctl=P+0xC8 vt+0xB0 (0x1087CC0), ctl vt+0xC0 stock setter (0x1087CF0),
   the P+0x204 bit 18 shadow writes rec+0x3D0 |= 0x8000000 / rec+0x5FE (0x1087D0E/0x1087D24),
   `P vt+0xC8(id, 0xAAE7433A, ...)` at count 0 (0x1087D99), ctl vt+0x110 (0x1087DA6) -> **all skipped** [O].
10. rec+0x3CC |= 0x60, rec+0x3C0 &= ~0x4000000 (0x1087DB4/DBB, rec) [O].

## Writes / calls OUTSIDE rec, classified

### A. Shared (non-indexed) state that body 1 still reaches under the guard
1. **Shared preview slot in the thrown service.** 0x1061D90 (`fn-1061D90.c` line 35): proceeds when
   `(rec+0x3C0 & 0x20000000) != 0 || rec+0x25C != 0` (0x1061DAF) [O]; builds a request with +0x3E bit 0
   (`uStack_5a | 1`) and owner +0x32 = id & 0x1FF, then `[[gm+0x98]+0x200] vt+8` at 0x1061F20 [O].
   0x15E0230 (the thrown service entry): `test byte [rdx+0x3E],1` at 0x15E023A; preview -> `add rcx,0x100;
   jmp 0x15DE020` (0x15E0246..0x15E0257) [O]. 0x15DE020 is a pure 0x40-byte request copy into its first
   argument [O]. So **every op-10 tick of modes 30/33/35/37(rec+0x2C2==0)/38/40, body 1 overwrites the single
   preview record at service+0x100** (no per-owner slot) [O]. The consumer of service+0x100 was not opened;
   that it drives the local throw-arc/landing preview is [I]. An owner filter at 0x15E0230 drops it only if it
   runs at the entry, before 0x15E023A; a filter placed in the pool path (0x15E025C..0x15E02B5) would not.
   Without the guard, body 1's preview is suppressed only while rec+0x25C == 0 (bit 29 clear).
2. **0x1087C05 `[plugin+0x60] vt+0x160(rec+0x240, 0)`** - target unresolved (see "Identification").
   Same receiver/shape also at mode 30 op 1 (0x10880D3, flag 0) and mode 33 op 12 (0x108AA91, flag 1, on message
   0xA3B3E67F, which also sets rec+0x3CC bit 9) [O]. None of the three is gated by locality or bit 29 [O].
   Receiver is the plugin-wide object (not per id); the per-player selector is the dword rec+0x240 [O].
   Whether the callee writes only the instance named by that handle is [I]/unknown.
3. **The real thrown request** (0x1087BF2): pool path of 0x15E0230 calls `[gm+0x18] vt+0x20(service+0x84)`
   (0x15E027C), then either the pool object copy (0x15E02C0) or `[gm+0x18] vt+0x30(service+0x84)` (0x15E0293) [O].
   This is the request the owner filter is meant to drop.
4. **Physics query** in 0x1037BA0 (called for both preview and throw): `[[gm+0x80]+8] vt+0x10(&q, 0x4000700,
   &start)` at 0x1038F12 with stack result, then `result vt+8(1)` release at 0x1038FB5 [O]. Transient, no
   persistent shared write [I].

### B. Per-id state (outside rec but indexed by id = body 1's own slots) [O unless noted]
- `[[P+0x60]+0x20][id]` byte = 7 (mode 37 op 1) / 6 (modes 30/33/35/38/39/40 op 1).
- `[[plugin+8]+0x78]+0x280 + (id-base)*0x20` flag dwords (ops 1/2/3/4/5/12) and 0xFF51D0 (set-bits on that
  per-id 0x20-byte bitset, `fn-0FF51D0.c`). 0xFFABA0 (mode 37 op 2, same slot) not opened [I: clear-bits twin].
- B = [[[plugin+8]+0x48]+0x18]; per-id record B[id] (stride 0x220): 0x10593C0, 0x105F6C0, 0x105F960, 0x1093990,
  mode 37 `lVar16+0x210`, mode 38 op 1 B[id]+0x140..0x21A; `B vt+0x38/0x40/0x48/0x80/0x88(id, ...)`;
  `[B+0x38]+0x18/0x20` per-id float/word table (mode 37 op 12, lines 441-450); `[B+0x28]/[B+0x30] vt+0x28(id, quat)`
  in 0x101EE30 (op 9, rec+0x3D4==4).
- Per-id aim struct `[[[plugin+8]+0x78]+0x260] vt+0(id)` +0x5C/+0x60/+0x6C/+0x80 (0x103AA20, 0x105AF90, 0x108E7D0,
  0x108ED30, 0x108ACE0).
- Motion requests `M vt+0x190(id,...)` (0x105AF90, 0x103BDE0, 0x105F960, modes 38/39 op 1), `M vt+0x170(id,...)`
  (mode 39 op 6, 0x1088933) - id-keyed; per-id effect [I].
- Action requests `[plugin+8] vt+8(id, 0x106E1200 / 0x3E3E07F8 / 0x40BABB27 / 0x5519439C, flags)` (op 0 of
  30/35/37/38/39, op 12 of 33/37) - id-keyed; per-id effect [I].

### C. Gated OFF for body 1 [O]
- Local bit (rec+0x3C0 bit 0): `[[P+0x48]] vt+0x2D0` (0x1087A1A); 0x1053270 local branch (placed service
  `[[gm+0x98]+0x208] vt+0x68` and `[plugin+8] vt+8(0x61FE32CC / 0x2A805080)`) - non-local only clears rec+0x3CC
  bit 19; 0x10586F0 (`P vt+0xC0(id, 0x6B88E204, ...)`); 0x1058D50 (mode 39 op 3, not opened).
- Bit 29: the whole stock/decrement/message block of mode 37 (list above).
- Class 1 only (rec+0x27C class bits == 0x40): in 0x108AFC0 `ctl vt+0x158/0x160(id, slot, bit)`,
  `[plugin+0x60] vt+0xE0/0xE8(rec+0x240, ...)`, 0x1058CA0; 0x103BB20's class-1 block. Support classes 2/3 take
  the rec-only branches (0x108AFC0 lines 119-166).

### D. Reads of the viewer's state (no writes, but coupling) [O]
- 0x1093620 (op 0/1 of every handler): offline, rec+0x3D4 not in {1,2,3}: rec+0x3C0 bit 31 =
  `[P+0xB8]+0x32C bit 2` (viewer camera side).
- 0x1042F80 (via 0x108E7D0 / 0x108F9D0, op 6): offline, support class with rec+0x3C0 bit 18|19: aim yaw/pitch
  computed from `rec+0x150..0x158 - [P+0xB8]+0xE0..0xE8` (viewer camera position), lines 49-57/134-137.
  Body 1's throw aim (rec+0x344/0x348, per-id aim struct) therefore follows the viewer's camera offline.
- 0x1037BA0 class 5: reads `[P+0xB8]+0xA0..0xAC`.
- 0x108C8C0 (ammo copy, gun phase 0): offline bit 29 = P+0x264 bit 3 (line 111-114).
- No write to [P+0xB8] (camera), to any `plugin+X` field (grep of all opened functions: no plugin-field store,
  no +0x350), to globals (no `DAT_14...` store), or HUD call was found in any opened function.

### E. Note outside the throw handlers (0x108C8C0)
For classes 2/3 with rec+0x3D0 bit 27 set, the ammo copy bumps obfuscated counters in the shared object [P+0x48]
(+0x318/+0x34C, +0x31C/+0x350; xor 0xB49DEA2F / 0x94FBD5AC) when the shadow rec+0x5FE^0x3FC8 or the count
mismatch (lines 84-103) [O]. Mode 37 sets that bit only inside the bit-29-gated block (0x1087D0E) [O]. With the
proposed guard (forced remote branch, lines 19-23: rec+0x25C = -1, bit 29, return) the counters are not reached
for id 1 [O]. Without the guard, offline, this path is live for any id that ever had bit 27 set [I].

## Identification of 0x1087C05 (best effort, not resolved)
- Receiver: object at plugin+0x60 (shared by all ids). Its +8 sub-object answers equip-id queries
  vt+0x48(equipId) (returns a type, compared with 5 in mode 33 op 12) and vt+0x50(equipId) (bool) [O].
- Methods seen on it, all keyed by the dword rec+0x240 plus one flag: vt+0xE0 / vt+0xE8 (class-1 guns,
  0x108B740 / 0x108B775), vt+0x160 (throwables) [O].
- vt+0x160 flag sequence for one throw: 0 on mode 30 enter (0x10880D3), 1 on the mode-33 animation event
  0xA3B3E67F that also sets rec+0x3CC bit 9 (0x108AA91), 0 right after the release request in mode 37
  (0x1087C05) [O]. Op 0 of modes 30/37/38/39/40 uses rec+0x3CC bit 9 with rec+0x280 bit 20 to pick mode 33
  (0x21, "pull") vs mode 35 (0x23, plus the 0x106E1200 action request) [O]. Reading: vt+0x160 = per-held-item "pin pulled / armed" state toggle on the equipment
  instance identified by rec+0x240 [I].
- It is not the ctl (P+0xC8) vt+0x160: ctl vt+0x160 is called as (ctl, id, slot, bit) in 0x108AFC0
  (0x108B59A, 4 inputs) while this site passes (obj, handle, 0) (3 inputs) [O]; different class [I].
- Who writes rec+0x240 was not found (not in 0x108C8C0, 0x108BD40 or 0x103E010). What body 1's rec+0x240
  holds (its own instance, 0, or a stale/viewer handle) is unknown.

## Functions opened (REA `function`)
Handlers: 0x1087210, 0x1088530, 0x1087FC0, 0x108A680, 0x10896B0, 0x1068C00, 0x108A130.
Level 1: 0x1061D90, 0x1037BA0, 0x1039020, 0x1053270, 0x1093620, 0x103AA20, 0x105F6C0, 0x105F960, 0x105AF90
(contains 0x105AFA8), 0x105FB70, 0x108AFC0, 0x108E7D0, 0x103BDE0, 0x1093990, 0x103BB20, 0x10586F0, 0xFF51D0,
0x10593C0, 0x101EE30, 0x108F9D0, 0xBFF050, 0x104BD60.
Level 2: 0x1043C80, 0x1042F80, 0x108ED30, 0x108ACE0, 0x15E0230, 0x15DE020.
Context: 0x108C8C0, 0x108BD40, 0x103E010 (contains 0x103E070).

## Not opened (bounded)
- No Ghidra function: 0x105F8A0 (op 1 of 30/33/35/37/39/40), 0x103AE60 (op 6 of 30/33/35/37, op 0 of 40),
  0x103A460 (op 0 of 30/35), 0x108FAD0 (mode 37 op 4).
- Skipped: 0xFFABA0, 0x1043AA0, 0x10448D0, 0x1058D50 (local only), 0x1058CA0 (class 1 only), 0x104A620,
  0x10499D0, 0x108F300, 0x103BF80, 0x104B7C0, 0x1043470, 0x10497C0, 0x1051740, 0x1400D93F0, 0x45BEB0,
  0x7A9470, 0xBFEFA0; the consumer of thrown-service+0x100; every virtual target (M, B, [plugin+8], [P+0x60],
  [plugin+0x60], [gm+0x18], [[gm+0x80]+8]).

## Verdict
Not proven side-effect free. Observed under the guard: no stock, ctl, message, camera, plugin-field, global or
HUD write in the opened code; everything else is rec or id-indexed. Remaining viewer-world effects: (1) the
preview copy into the single slot thrown-service+0x100 (dropped only if the owner filter sits at 0x15E0230
entry); (2) the unresolved `[plugin+0x60] vt+0x160(rec+0x240, flag)` at 0x1087C05 / 0x10880D3 / 0x108AA91;
(3) four unopenable helpers. Body 1's throw aim also reads the viewer's camera offline (correctness, not a write).
