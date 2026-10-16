# Slot 3 / slot 6 equip for a non-local player instance (id 1): static audit

Target: `D:\Games\steamapps\common\MGS_TPP\mgsvtpp.exe`, sha256 085c2f82...e9bb45, Ghidra 12.1.3 through REA
(`rea-agents` 6.5.0, snapshot `mgsvtpp-ghidra.snapshot`). All addresses below are RVAs (Ghidra = 0x140000000 + RVA).
Read-only: REA `function`, `instructions`, `xrefs`, `search`, `search-bytes`, `read-bytes` only. Nothing was edited
or launched. Raw REA JSON for every call is next to this file (`fn-*.json`, `rb-*.json`, `sb-*.json`, `xr-*.json`).

Tags: **[O]** = observed in REA decompilation/instructions; **[B]** = observed in REA `read-bytes` output and decoded by
hand (short leaf functions that Ghidra has no procedure for); **[I]** = inferred; **[R]** = taken from the repo's own
earlier evidence (docs/evidence/remote-equipment-contract-2026-10-07.md, equipment_stock_writer_contract.hpp), used to
find vtables and then re-checked where marked.

Naming: P = player system (`*(image+0x2A82C30)`), ctl = `[P+0xC8]` (equip iface, vtable 0x23C1720), Y = `[ctl+8]`
(inventory/store, vtable 0x23895D0 or 0x23894B0), X = engine `[Y+0x58]` (vtable 0x234B840), row(id) =
`[[P+0x60]+0xC0] + id*0x3A`, Q = `[P+0x60]`, vars = `[ctl+0x20]`, blk = `[[P+0x50] + P+0x78[id]*8]`.

## Vtables (anchors)

- ctl vtable 0x23C1720 [B] (`rb-ctlvt.json`): +0x10 0x11AFA80 SetCurrentSlot, +0x18 0x11B0900, +0x38 0x11AD240,
  +0x50 0x11AD320, +0x90 0x11AD420, +0x98 0x11AFF30, +0xA0 0x11B0700, +0xB8 0x11AF530, +0xC0 0x11AF490 (stock setter),
  +0x100 0x11AF0B0, +0x198 0x11AC2E0 ChangeEquip, +0x1F0 0x11B1B60.
- Y vtables 0x23894B0 and 0x23895D0 [B] (`rb-Yvt.json`, found by `search-bytes` for the pointer 0xEE9880): both have
  +8 0xEE9880, +0x10 0xEE9D70, +0x20 0xEE96C0, +0x68 0xEE90A0, +0x70 0xEE9760, +0x78 0xEE9730, +0x88 0xEE9050.
  0xEE8CA0 [O] is a destructor that writes 0x2389480 at +0 and 0x23894B0 at +0x20, so Y is the +0x20 sub-object.
- X vtable 0x234B840 [B] (`rb-Xvt.json`): +0 0xDC3250 alloc, +8 0xDC63A0 free, +0x10 0xDC2A50 owner,
  +0x20 0xDBED90, +0x28 0xDBE590, +0x30 0xDC0920, +0x150 0xDBEAE0, +0x238 0x083A30.
- blk vtable 0x2295210 [B] (`rb-blkvt.json`): +0x128 0x9C5980, +0x130 0x9CC6F0, +0x180 0x9C3B40.
- body writer `[blk+0xF0]` vtable 0x23C95D8 [B]: +0x28 = 0x1209EC0 (the lower writer).
- model binder `[blk+0x618]` vtable 0x22EA550 [B]: +0x18 0xB0CF80, +0x20 0xB0C720, +0x28 0xB0C800.
- equip types table vtable 0x22AD180 [B]: +0 0xA2A3E0 (type), +0x48 0xA2A710 (support category).
- Jump tables [B] (`rb-jt.json`): SetCurrentSlot case 3 -> 0x11AFC2C, case 6 -> 0x11AFC23; vt+0x98 case 3 ->
  0x11B0241, case 6 -> 0x11B020F.

## Call path

ChangeEquip(ctl, out, id, req20) for types 12/13 with request flag byte req+0x13 bit 2:

1. `0x11AC312..0x11AC327` [O]: u16 `P+0x70[id]` and byte `P+0x78[id]`; pool 0xFF -> out = -1, nothing written.
2. `0x11AC337` [O]: req+0x13 bit 4 set -> a different branch (vt+0x1A8, slot 5); a writer must keep bit 4 clear.
3. `0x11AC375` [O]: type = `[[ctl+0x28]+8]` vt+0 (types 0xA2A3E0 [O], a pure table lookup).
4. type != 14 (`0x11AC37C`): restriction predicate `[blk+0x70]` vt+0x30 = 0x1156640 [B] (`mov rax,[rcx+0x168];
   mov rcx,[rcx+0x1A8]; sub edx,[rax+0xC]; imul rdx,0xB0; cmp [rdx+rcx],0; setne al`) - a pure read. Nonzero ->
   out = -1 (`0x11AC3B1..0x11AC3B3`).
5. `0x11AC49D` [O]: the lower writer `[blk+0xF0]` vt+0x28 = 0x1209EC0 with id' = `P+0x70[id]`, a 16-byte request
   (equip & 0x7FF, 0xFFFF, stock/sub/ammo/sub, suppression, 1, flags & 3), param_4 = 0, param_7 = 7, param_8 = 0,
   param_9 = 1, param_10 = NULL, param_12 = 1 (`0x11AC3C6..0x11AC492`).
6. type 14 (`0x11AC379..0x11AC396`) [O]: ctl vt+0x1F0(id, equip, 1, 1, 1) instead of steps 4-5.
7. req+0x13 bit 2 (`0x11AC4A0`) [O]: type -> slot (`0x11AC4B7..0x11AC4CE`: 12/13 -> 3, 14 -> 6, 0x11 -> 4); for 3/4
   find the sub with ctl vt+0x90(id, slot, sub) == full u16 req+8 (`0x11AC520..0x11AC542`, sub 0 if none);
   SetCurrentSlot(id, slot, sub, 0, 1) (`0x11AC57B`); for slot 3 also ctl vt+0xA0(out, id, 3, equip, 0, 1, 0)
   (`0x11AC5AF`).

## Per function

### Lower writer 0x1209EC0 (types 12/13) [O]
- Local flag: `cmp r15d,[[writer+8]+0x138]+0x218; setz [rsp+0x40]` (`0x1209F6C/0x1209F73`), i.e. id == **P+0x218**.
- Engine: X = `[[BFF050()+0x98]+0x1E8]`, types = X+8; ctl = `[P+0xC8]`, Y = ctl+8 (decompiled lines 155-173).
- Non-zero equip, types 12/13 (decomp lines 238-315): slot = 3; category = types vt+0x48(equip) (0xA2A710 [O], pure);
  sub = first sub whose ctl vt+0x90 entry has the same category, else the first empty sub, else ctl vt+0x50(id)
  (current sub). Reads only.
- Everything between decomp lines 436 and 800 is inside `if (local)`.
- Support branch (`0x120B48E`): `test dl,dl; jz 0x120B632` (`0x120B49A`) skips the whole stock block for a non-local id:
  Y vt+0x68/vt+0x88 (`0x120B4AF`, `0x120B4C1`), ctl vt+0xC0 stock setter (`0x120B540`), ctl vt+0x100 (`0x120B59E`),
  ctl vt+0xB8 (`0x120B5D0`), UI `[P+0xD8]` vt+0x2D8, vt+0x118 (`0x120B62A`).
- Non-local id runs only: ctl vt+0x98(id, 3, sub, equip & 0x7FF, equip >> 11, 0, 0) (`0x120B65C`); then
  ctl vt+0x38(id) == 3 (`0x120B66B`) and ctl vt+0x50(id) == sub (`0x120B680`) -> ctl vt+0xA0(&tmp, id, 3, equip,
  hi, 0, 0) (`0x120B6B6`); then `cmp [rsp+0x40],0; jz 0x120C095` (`0x120BD75/0x120BD7A`) returns before the tail
  (UI list `[P+0xD8]` vt+0x190, Y vt+0xC8, the model message, globals `+0x1BC2..+0x1C41`, ctl vt+0x118, `[P+0xB8]`).
- Type 14 returns at once (decomp line 317 `if (type == 0xe) goto exit`).

### ctl vt+0x90 = 0x11AD420 [B] (`rb-11AD420.json`)
- `mov rax,[rcx+0x18]; ... imul r10,id,0x3A; add r10,[[rax+0x60]+0xC0]; cmp r8d,6; ja ret0; jmp [table 0x11AD4A0]`.
- Slot 3: `movzx eax,r9b; movzx eax,word [r10+rax*2+0xC]; ret` (`0x11AD484`); slot 4 uses +0x1C (`0x11AD48F`).
- **Pure read** of row(id)+0xC+2*sub. No memory store anywhere in 0x11AD420..0x11AD49C.

### ctl vt+0x98 = 0x11AFF30 [O]
- Always: `Q+0x17C |= 1 << id` (`0x11AFF64`); calls `[ctl+0x28]` vt+0x238 (`0x11AFF88`) = 0x083A30
  [B] `mov rax,[rcx+0xF8]; ret` (pure getter; the object identity is not established, see unknowns).
- Local flag: id == `[[ctl+0x18]+0x214]` = P+0x214 (`0x11AFF92`).
- Case 3 (`0x11B0241`): `row[6+sub] = equip`; only if local `vars+0x584+2*sub = equip`; if row+0x36 & 0xF == 3 and
  row+0x37 & 0xF == sub, `row+0 = equip`. No Y, X or stock call.
- Case 6 (`0x11B020F`): `row+8 = equip`; only if local `vars+0x5D4 = equip`; if row slot == 6, `row+0 = equip`.

### SetCurrentSlot ctl vt+0x10 = 0x11AFA80 [O]
- Always: `Q+0x17C |= 1 << id` (`0x11AFAC5`); local flag = id == P+0x214 (`0x11AFADE`).
- slot != 4: only if local `vars+0x674 = slot` (`0x11AFAF8`); `row+0x36` low nibble = slot (`0x11AFAFF..0x11AFB09`).
- Case 3 (`0x11AFC2C`): `row+0 = row[6+sub]`, `row+0x37` low nibble = sub, only if local `vars+0x678 = sub`, then
  ctl vt+0xA0(out, id, 3, row[6+sub], 0, 1, 0).
- Case 6 (`0x11AFC23`): `row+0 = row+8` (no vt+0xA0).
- Everything after `0x11AFB3A` (`test r13b; jz 0x11AFE4B`, `0x11AFB3D`) is local-only: vt+0x118, camera/HUD params
  `[P+0xE0]` vt+0x30, message 0xFDB181F7 through P vt+0xC8, UI `[P+0xD8]` vt+0x190.

### ctl vt+0xA0 = 0x11B0700 [O]
- Calls ctl vt+0x18 with all arguments (`0x11B075B`).
- `id == P+0x214` (`0x11B0767`) and vt+0x38(id) == slot: camera/HUD params `[P+0xE0]` vt+0x30 (local only).
- slot == 2 only: `[ctl+0xF8]+1+id*2` bit 1 (`0x11B08B1..0x11B08D9`). Not reached for slots 3/6.

### ctl vt+0x18 = 0x11B0900 [O]
- Y vt+8(out, id, slot, equip, p6, p7, p8) (`0x11B094F`).
- Block notify, only if P+0x204 bit 0 (`0x11B0957`), P+0x70[id] != 0xFFFF (`0x11B096A`), P+0x78[id] != 0xFF
  (`0x11B097A`): `blk vt+0x180` (`0x11B098E`) then result vt+0x20(id, slot, *out, ...) (`0x11B09A2`).
- blk vt+0x180 = 0x9C3B40 [B]: `mov rax,[rcx+0x618]; ret` -> the model binder.
- binder vt+0x20 = 0xB0C720 [O]: slot 0/1/2 -> record 0/1/2, slot 4 -> record 3; any other slot returns at once
  (`cmp r8d,2; jnz 0xB0C7E7`, `0xB0C751/0xB0C755`). **For slots 3 and 6 the block notify writes nothing.**

### Y vt+8 = 0xEE9880 (inventory allocate) [O]
- idx = Y+0xC * id + slot; H = `Y+0x40[idx]`.
- H valid and p8 == 0 and X vt+0x20(H) == equip and X vt+0x28(H) == p6: reuse, `*out = H` (`0xEE98CB..0xEE98F8`).
- Otherwise H valid -> Y vt+0x10(id, slot) (`0xEE990C`); X vt+0(&new, equip, p6) (`0xEE9925`); new kind 0 ->
  `*out = 0`, return (`0xEE992E..0xEE9933`); X vt+0x10(new, id & 0x1FF) (`0xEE994B`); `Y+0x40[idx] = new`
  (`0xEE9956`); `Y+0x50[idx*6]` zeroed (`0xEE9964/0xEE9968`).
- Kinds 1..8 only (`0xEE997D`): metadata from X vt+0x150 and, only if id == Y+0x30 (`0xEE9A16`) and p7 == 0, Y vt+0x70.
  Kind 9: `Y+0x50[idx*6] = [X+0x10] vt+0x68(equip)`. Support (12/13) and arm (14) kinds take neither.
- No access to Y+0x98 (shared stock) in this function.

### Y vt+0x10 = 0xEE9D70 (release) [O]
- Returns if H is invalid. Only if id == Y+0x30 (`0xEE9DBD`) and kind 1..8 and slot 0..2: Y vt+0xA8 calls.
- Always: X vt+8(H) (`0xEE9E1E`), `Y+0x40[idx] = 0` (`0xEE9E2F`), `Y+0x50[idx*6] = 0` (`0xEE9E3E/0xEE9E41`).

### X vt+0 = 0xDC3250 (allocate) [O]
- kind = `[X+8]` vt+0(equip) (`0xDC3278`); 0 -> handle 0.
- Kinds 1..8: regular ring X+0x48 plus stock-cache work (DC3700, refs, X+0x120 metadata). Not our path.
- Other kinds (`0xDC33EC`): ring X+0x60 full (`[X+0x6C] == [X+0x68]`, `0xDC33F6`) -> handle 0; else idx = DBDA10();
  `X+0x140[idx*6] = {u16 equip, u16 0xFFFF, byte p6 & 0x7F, byte 0}` (`0xDC3426..0xDC3441`); handle =
  idx<<16 | (equip & 0x7FF)<<5 | kind.
- No instance id, no stock, no vars.

### X vt+0x10 = 0xDC2A50 (owner) [O]
- Kinds 1..8: owner flag bit 5 of X+0x120 metadata via the controller chain. Other kinds: `X+0x140[idx*6+2] = owner`
  (`0xDC2AE8`), where owner = id & 0x1FF.

### X vt+8 = 0xDC63A0 (free) [O]
- Other kinds: if `X+0x140[idx].byte5` bit 0, call 0xDC6540 (not opened); then `byte4 &= 0x80`, `word0 = 0`, and
  push idx back to the ring X+0x60 (0xDC1640).

### X vt+0x20 = 0xDBED90 and vt+0x28 = 0xDBE590 [B]
- vt+0x28: other kinds return `X+0x140[idx*6+4] & 0x7F` (p6). Pure read.
- vt+0x20: same prologue; the read ended mid-instruction at `0f b7 04`, so "returns word0 (equip)" is [I].

### ctl vt+0x1F0 = 0x11B1B60 (arm) [O]
- equip outside 0x203..0x209 -> 0x207 (`0x11B1B7F..0x11B1B9D`).
- current = `(Y+0x40[id*stride+6] >> 5) & 0x7FF` if the handle is valid (`0x11B1BA0..0x11B1BD2`).
- `id == P+0x214` (`0x11B1BE5..0x11B1BF3`): `[[ctl+0x28]+0x10]` vt+0x60/0x58, ctl vt+0x100(5), Y vt+0x70(5, ...),
  UI `[P+0xD8]` vt+0x1B0, ctl vt+0xE0, vt+0x118. All local-only.
- current != equip (`0x11B1CCC`): ctl vt+0xA0(tmp, id, 6, equip, 0, 1, 1) (`0x11B1CFA`; p8 = 1 so Y vt+8 never reuses
  and always releases and re-allocates); ctl vt+0x98(id, 6, 0, equip, 0, 0, 0) (`0x11B1D23`).

### Getters [O]
- ctl vt+0x38 0x11AD240 = row+0x36 & 0xF (current slot); vt+0x50 0x11AD320 = row+0x37 & 0xF (current sub);
  vt+0x30 0x11AD390 reads `[ctl+8]+0x40` at row's slot. All pure.
- types vt+0 0xA2A3E0: table lookup (calls getters 0x1E03790 and 0xA3C5B0, not opened).
- types vt+0x48 0xA2A710: support category by id ranges 0x15D..0x1AD and 0x600..0x608 (calls 0xA2ABB0, not opened).

## What a non-local id 1 writes (types 12/13, flag bit 2)
- row(1)+0xC+2*sub = equip (vt+0x98, from the lower writer)
- row(1)+0x36 = 3, row(1)+0x37 = sub, row(1)+0 = equip (SetCurrentSlot, and vt+0x98 when the slot is already active)
- Q+0x17C bit 1 (a shared dword, but only bit `id`)
- Y+0x40[1*stride+3] = new handle, Y+0x50[(1*stride+3)*6] = 0 (Y vt+8); an earlier handle there is released first
- engine X: one entry of the non-gun ring X+0x60 and X+0x140[idx] = {equip, owner = 1, p6 = 0, 0}

Type 14 writes the same, with slot 6 and row+8.

It never writes, for id 1:
- vars (+0x584/+0x5D4/+0x674/+0x678/+0x548.. are all behind id == P+0x214)
- the stock Y+0x98[k] or `[ctl+0x20]+0x614` (ctl vt+0xC0/0xB8/0x100 and Y vt+0x68/0x88 are only in the lower writer's
  local block, `0x120B49A`)
- HUD/UI/camera (`[P+0xD8]`, `[P+0xE0]`, `[P+0xB8]`, message 0xFDB181F7, all local-gated)
- the model binder (no-op for slots 3/6)
- instance 0's row or handles (every index is id*0x3A or id*stride+slot)

## Conclusion
Equipping the remote's support weapon (types 12/13) or arm (14) into instance 1 through ChangeEquip 0x11AC2E0 is
side-effect free for the viewer under the preconditions below. Writes are confined to instance 1's row, Y entries and
dirty bit, plus one entry of the engine's shared non-gun handle pool. [O] for each step listed; the whole-path claim is
[I] for the parts not opened (see unknowns).

It does not make throws safe. Once row(1) slot 3 is active, gun phase 0 / mode 37 for instance 1 read and spend the
shared stock. This is documented in anim-round2-throws.md and was not re-verified here. Slot 6 activation enables mode
53 (rocket arm: shared plugin field and camera writes, per the same doc).

## Preconditions / refusals for a writer
1. Offline, `P+0x204` bit 0 set, and bit 8 (online) clear.
2. Id 1 must be non-local everywhere:
   - P+0x214 != 1 (SetCurrentSlot, vt+0x98, vt+0xA0, vt+0x1F0)
   - **P+0x218 != 1** (lower writer `0x1209F6C`). The lab `partner_input_p218_allow` window sets P+0x218 = 1; inside it,
     the call would run the stock setters and UI for id 1.
   - Y+0x30 != 1 (Y vt+8/vt+0x10).
3. `P+0x78[1] != 0xFF` and `P+0x70[1] == 1`. ChangeEquip hands `P+0x70[id]` to the lower writer as its id, so another
   value would make it write another instance's row.
4. Restriction byte `[[blk+0x70]+0x1A8][(1 - [[blk+0x70]+0x168]+0xC)*0xB0]` == 0, else out = -1.
5. Request: equip < 0x800 and of type 12/13 (or 14); req+0x13 bit 4 clear; bit 2 set to activate. Check out == 0 and
   read back, because ChangeEquip does not check the lower writer's result: if the equip is not found it activates
   sub 0.
6. Keep row(1)+0x37 & 0xF < 8 [I]: vt+0x50 can return up to 15, and vt+0x98 indexes row[6+sub].
7. Free entries in the non-gun ring (`[X+0x6C] != [X+0x68]`) with margin [I]. The pool is shared with the viewer's own
   support and arm handles, so instance 1 consumes capacity.
8. Arm: ids 0x203..0x209. Each change of arm releases and re-allocates a handle.
9. Run on the game thread that owns these structures [I] (not established here).
