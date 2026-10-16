# Place family + gun mode table audit (REA / Ghidra 12.1.3, read-only)

Binary: D:\Games\steamapps\common\MGS_TPP\mgsvtpp.exe (sha256 085c2f82...bb45), image base 0x140000000.
Addresses below are RVAs unless written 0x14xxxxxxx. Tool: `npx rea-agents@latest function|xrefs|decompile`
with the snapshot in this folder. Raw outputs: fn-*.json (+ extracted fn-*.c / fn-*.asm), xrefs-2C556A0.json.
Instruction addresses for switch-case bodies come from REA's high-pcode `native_value_flow.operations`
(the Ghidra function bodies stop at the jump table because Switch Analysis is disabled, so `assembly`
only covers the prologue/epilogue). "observed" = seen in REA decompilation / pcode; "inferred" = my reading.

Naming used: plugin = param_1 (rcx) of the mode handlers (rec array at plugin+0x58, stride 0x610);
rec = own gun record; G = *(plugin+8); P = *(G+0x138); ctl = *(P+0xC8);
BS = body system *(*(G+0x48)+0x18), per-id 0x220 record BSrec = BS[1] + (id-BS+0x5C)*0x220;
ACT = *(*(G+0x78)+0x238) (body action controller, all calls take id);
ENT(id) = *(*(G+0x78)+0x280) + (id-*(*(G+0x78)+8))*0x20 (per-id 0x20-byte flag entry).

## 0. Gate semantics used for instance 1 (offline)
- P+0x204 bit 8 test + id==P+0x218: 0x108C8C0 at 0x108C8E5..0x108C8FA (`mov eax,[r9+0x204]; shr eax,8;
  test al,1; jz; cmp r8d,[r9+0x218]; jz`) guards the remote branch (observed). The doc identifies this as
  S_IS_ONLINE (not re-verified). 0x1052560 returns 1 iff (P+0x204 bit 8) && id != P+0x218 (observed, pure read).
- So every `((P+0x204>>8)&1)==0 || id==P+0x218` branch IS TAKEN for id 1 offline (inferred from the above).
- rec+0x3C0 bit 0 = (id == P+0x218), always clear for id 1: taken from the doc (0x10540A2..0x10540B8), not re-opened.

## 1. Place handlers, non-local id (bit 0 clear, offline, bit 29 set)

### Mode 46 0x107BAC0 (place execute)
Observed in fn-107BAC0.c; addresses from pcode.
- op 1 (enter), any id:
  - P+0x60 -> [+0x20][id] = 9 (store at 0x107BB89) - per-id byte in shared array.
  - 0x0FF51D0(ENT(id), 0x19, 200, 0xCE) at 0x107BBAF - bit-set inside ENT(id) only (0x0FF51D0 observed: `*(p+(b>>5)*4) |= 1<<(b&31)`).
  - ENT(id)+0x18 |= 0x1000; rec writes.
  - **Gate 0x107BC66..0x107BC79** (load [P+0x204], shr 8, and 1, cbranch; [P+0x218]==id) -> block 0x107BC9B, taken offline for any id:
    - call 0x104CF00(plugin, &req, rec, id, rec+0x100) at 0x107BD27 (placement probe; writes only its stack request and rec+0x100,
      reads plugin+0x3B0..0x3DA, plugin+0x350; raycasts through [plugin+0x70] vt+0x10/0x18/0xA8, gm+0x98->+0x208 vt+0x78; sub-helpers not opened).
    - **SHARED PLUGIN FIELD WRITE**: decompiled lines 309-316:
      `if (*(short*)(plugin+0x3d8) == -1) { plugin+0x350 &= ~8; *(u16*)(plugin+0x3e8) = 0xffff; } else { plugin+0x350 |= 8; *(u16*)(plugin+0x3e8) = *(u16*)(plugin+0x3d8); }`
      pcode: load u16 [plugin+0x3D8] 0x107BD64, cmp 0xFFFF 0x107BD6C; OR 8 store 0x107BD71; store 0x3E8 0x107BD79; AND ~8 store 0x107BD83; store 0xFFFF 0x107BD8B.
  - rec+0x3eb==3 || rec+0x3ec==1: 0x10593C0(BSrec) 0x107C002, 0x105A080 0x107C027, BS vt+0x38 0x107C045, ACT vt+0x388(id)->obj vt+0x60/0x70 (0x107C218/0x107C226).
    else: ENT(id)+0x18 |= 0x800; 0x105E500 0x107BDE8; ACT vt+0x190(id,2,0x88306e1d369e) 0x107BE1F.
  - tail: offline/local -> rec+0x2C3++ (gate 0x107C234/0x107C241); online-remote -> P+0x60 vt+0x48(id) 0x107C252.
- op 2 (exit): ENT(id) &= ~0x2000000; rec; 0x10593C0(BSrec) 0x107C2C9; plugin+0x350 &= ~8 / plugin+0x3E8 = 0xFFFF ONLY if rec bit 0 (0x107C2D7/0x107C2E4, line 481). => id 1 never undoes what op 1 latched.
- op 0 (state machine on rec+0x2C2), any id (pcode truncated at 0x107CC81, so no addresses for op 0/op 12):
  - state 0: rec+0x3B8/0x3BC from per-id [G+0x78]+0x290; reads P+0x60->+0x18[id]; ACT vt+0x388(id)->obj vt+0x70(quat), vt+0x88; 0x105A080(.., 8.0); 0x10593C0(BSrec); BS vt+0x38.
  - state 1: ACT vt+0x220(id,2,3) query; 0x1052560 (pure); 0x105A080(..,0).
  - states 2/3: 0x1052560; ACT vt+0x220; 0x1059EC0.
  - states 4/5/6: own bit in [plugin+0x38]+0x10 bitset; BS vt+0x38; func 0x1052530; 0x1052560; next-mode selector 0x105A190 (not opened).
- op 3: ACT vt+0x220; rec. op 4/5: 0x0FF51D0 on ENT(id) (0x107C3AE/0x107C465); P+0x60 vt+0x48(id) 0x107C3F5; 0x108AFC0 0x107C403.
- op 6: 0x108E7D0 0x107C482 (aim; writes rec and per-id aim obj [G+0x78]+0x260 vt+0(id); helpers 0x1043C80/0x1042F80/0x108ED30 not opened).
- op 9: placement block (0x107CA18 gm, 0x107CA33 placed request vt+0x48, 0x107CA4C P vt+0xB0 msg 0x182DB6BB, 0x107CA64 [P+0x48] vt+0x2D0,
  ctl vt+0xB0/0xC0/0x110 at 0x107CB18/0x107CB48/0x107CB89, P vt+0xC8 msg 0xAAE7433A 0x107CBFE) is inside
  `if ((rec+0x3c0 & 1) && rec+0x2c2 < 4)` (line 539) => local-only (observed; matches doc 0x107C6FA).
  Tail: offline/local gate 0x107CC2E/0x107CC3B -> 0x1021000 at 0x107CC68 for any id offline (observed).
- op 12 (messages): rec writes; 0x1044E10 (observed pure read: per-id dt from P+0x60 vt+8) at 0x107C5BF/0x107C6BD.

### 0x1021000 (called 0x107CC68, any id offline)
Observed: only direct non-stack store is rec+0x38C (0x1021062/0x102106B). Indirect calls (asm observed):
P+0x60 vt+8 (0x102104F, getter), ACT vt+0x388(id) (0x1021170), [BS+0x28] vt+0x28(id,quat) (0x1021D75),
ACT vt+0x348(id,0) (0x1022A6E) -> obj vt+0 (0x1022C4C), [BS+0x38] vt+0x20(id,..,0) (0x1023B55), vt+0x50(id,..) (0x1023B67),
vt+0x38(id,0xC,..) (0x1024113), vt+0x88(id,0xC,..) (0x10243EC). Direct callees 0x104C390 (observed: pure math, writes only out-params),
0x0E7DA60/0x0E7EE10 (not opened). No P+0xB8 / plugin+0x350 / 0x3D8 / 0x204 / 0x218 reference (grep of decompilation).
=> placement hand IK for the id's own body (inferred).

### Helpers
- 0x1059EC0 (observed): local stack builders 0x0AF1C00/0x1051670; ACT vt+0x178(id,2,0x185a6a24c032,&param) at 0x1059F42; rec+0x2E0 at 0x105A06A. Nothing else.
- 0x105A080 (observed): ACT vt+0x170(id,2,0x185a6a24c032,t,0) at 0x105A0BF; rec+0x2E0 at 0x105A150/0x105A168/0x105A17A from 0x1044B60/0x1044D20/0x104BCB0 (not opened, take a stack byte).
- 0x105E620 / 0x105E500 / 0x10593C0 / 0x1093990 (observed): write rec+0x3C4 and the per-id BSrec only.
- 0x103BC60 (observed): if rec bit 17 and !bit 1 -> ACT vt+0x178(id,...). Per-id.
- 0x108AFC0 (observed): class 2/3 records: rec writes only. ctl vt+0x158/0x160 and plugin+0x60 vt+0xE0/0xE8 (+0x1058CA0) are only in the class-1 paths
  (`(rec+0x27C & 0x1C0)==0x40` / `bVar2==1` after `class != remote class -> exit`). Early exit if P+0x60 vt+0x48(id) record +8 != 1 (inferred offline).
- 0x1099960 (observed): only if ENT(id)+0x10 bit 26 clear: gm+0x98->+0x208 vt+0xF0(owner id) query, then [G+0x120] vt+0x50(id,pos,flag) (target unresolved).
- 0x10586F0 (observed): local-only (rec bit 0) P vt+0xC0(id, 0x6B88E204, ...).
- 0x1053270 (observed): non-local -> only rec+0x3CC &= ~0x80000 ; local -> placed mgr vt+0x68 + G vt+8.
- 0x0FF51D0 / 0x0FFABA0 (observed): set/clear bits inside the given ENT(id) pointer (bit indices < 256).
- 0x1052560 (observed): pure "is remote" query. 0x1044E10 (observed): pure per-id dt read. 0x104C390: pure math.

### Mode 44 0x107D300 (place ready / C4 preview)
- op 1 any id: P+0x60->+0x20[id]=8 (0x107D3B2); 0x0FF51D0 0x107D3D0; rec; 0x105E500 0x107D411; ACT vt+0x190 0x107D448; 0x10586F0 0x107D5F8 (local-only inside).
- op 2: ENT(id) &= ~0x2000000; 0x10593C0 0x107D631; ALL preview work (plugin+0x3E0/0x3E4 stores 0x107D69C/0x107D6EC/0x107D7B0/0x107D8BE,
  plugin+0x60 vt+0/0x30/0x70/0x98/0x48, 0x104CF00 0x107D74B, [P+0x48] vt+0x180 line object) is inside `if (rec+0x3c0 & 1)` (line 197) => local-only.
- op 4/5: ENT(id)+0x18 |= 0x4100(/0x200); 0x1099960 0x107D93F; P+0x60 vt+0x48 0x107D959; 0x108AFC0 0x107D967.
- op 6: 0x108E7D0 0x107D9C5; 0x103BC60 0x107D9EB; BS vt+0x38 0x107DBAE; 0x1093990 0x107DBBA.
- op 10: 0x103D040 at 0x107DBD9 only if rec bit 0 (line 406) => local-only; else own bit in [plugin+0x38]+0x10 (0x107DC24).
- op 0: 0x105A190 at 0x107DC8B.

### Mode 43 0x107E480 (place hold)
- op 1: P+0x60->+0x20[id]=8 0x107E524; 0x0FF51D0 0x107E549; rec; 0x105E620 0x107E58C; ACT vt+0x170 0x107E5DF or vt+0x190 0x107E5CA; BS vt+0x38/0x80 0x107E7A2/0x107E7BD.
- op 2: ENT(id) &= ~0x2000000 (0x107E7E4); 0x10593C0 0x107E80F. op 4: 0x0FF51D0 0x107E855, 0x1099960 0x107E874. op 5: 0x108AFC0 0x107E89C, 0x0FF51D0 0x107E8CD.
- op 6: 0x108E7D0 0x107E8E9; 0x103BC60 0x107E90F; 0x105E620 0x107E92D; 0x1093990 0x107EADD; BSrec+0x20C; BS vt+0x38.
- op 12 msg 0xB6F952A5: G vt+8(G, id, 0x104AF80(rec+0x1B0), 2) at 0x107EB92 for ANY id (target unresolved).
- op 0: ACT vt+0x220, BS vt+0x38, 0x105A190 0x107EC8B.

### Mode 45 0x107DD00 (place draw)
- Only per-id writes: P+0x60->+0x20[id] (0x107DD96), ENT(id)+0/+4/+0xC/+0x18, BSrec fields (0x107DE9E..0x107DF40), own bit in [plugin+0x38]+0x10.
- Calls: 0x10593C0 0x107DE99/0x107E07D; ACT vt+0x190/0x170 0x107DF6D/0x107DF86, vt+0x220; BS vt+0x38/0x80; 0x0FFABA0 0x107E057; 0x108AFC0 0x107E18D;
  0x103BC60 0x107E1B8; 0x1093990 0x107E1C6; op 12 msg 0xB6F952A5: G vt+8(id, 0x104AEF0(rec+0x1B0), 2) at 0x107E26C (any id).

### Mode 47 0x107ECF0 (C4 detonation)
- op 1: per-id byte 0x107ED80; 0x0FF51D0 0x107EDA5; 0x105E500 0x107EDDA; ACT vt+0x190 0x107EE0F; BS vt+0x38; gate 0x107EFE7/0x107EFF4 -> rec+0x2C3++ offline.
- op 10: 0x1053270 at 0x107F2D9 (if rec bit 1; non-local effect = rec only); 0x103D040 at 0x107F2F3 only if rec bit 0 => detonation local-only (observed).
- No plugin/P field writes.

### Verdict (Q1)
- CONFIRMED: placing (mode 46 op 9) and detonating (mode 47 op 10, also mode 44 op 10) are local-only for instance 1.
- CONFIRMED (within what was opened): no reference to P+0xB8 anywhere in the place family or opened helpers; no ctl vt+0xC0 / stock path reachable for a support-class id-1 record.
- REFUTED for shared plugin fields: mode 46 op 1 latches plugin+0x350 bit 3 and u16 plugin+0x3E8 from plugin+0x3D8 for any id offline (0x107BD64..0x107BD8B), and only the local op 2 clears it.
- HUD: no direct HUD/P message call for id 1 found; open item = G vt+8 (op 12 0xB6F952A5, modes 43/45 and also 28/32) and [G+0x120] vt+0x50.

## 2. Mode table 0x2C556A0
REA xrefs on 0x142c556a0 -> single ref 0x142025322 (MOVAPS store) in initializer 0x1420252F0 (body 0x1420252F0..0x142025852).
57 16-byte member-pointer slots (index 0 = 0, 1..56 = handlers; second dword = EDX = 0 from `xor edx,edx` 0x1420252F9).
Index = mode (checked: 43 -> slot 0x2C55950 = 0x107E480, 37 -> 0x2C558F0 = 0x1087210, 53 -> 0x2C559F0 = 0x107FA30).

| mode | handler | store | role |
|---|---|---|---|
| 28 | 0x1089190 | 0x2C55860 @0x1420255C2 | support (throwable) HOLD hub; op1 rec+0x2C0=0x1C; op0 -> 29/30/32/33/37/38/40/41 |
| 29 | 0x1089190 | 0x2C55870 @0x1420255D4 | same handler; re-entry of hold (op0 returns 0x1D when rec+0x3CC bit 13) |
| 30 | 0x1087FC0 | 0x2C55880 | throw (doc: op10 0x108834D, preview 0x1061D90) |
| 31 | 0x1087FC0 | 0x2C55890 @0x1420255FF | same as 30 -> throw |
| 32 | 0x1088A20 | 0x2C558A0 @0x14202560E | support draw/holster transition (op0 -> 0x1C=28; op1 rec+0x3C0 |= 0x4000040 like mode 45) |
| 33 | 0x108A680 | 0x2C558B0 | throw (doc: op10 0x108AB10, op12) |
| 34 | 0x108A680 | 0x2C558C0 @0x142025643 | same as 33 -> throw |
| 35 | 0x10896B0 | 0x2C558D0 | throw (doc: op10 0x1089A11) |
| 36 | 0x10896B0 | 0x2C558E0 @0x142025675 | same as 35 -> throw |
| 37 | 0x1087210 | 0x2C558F0 | throw with stock spend (doc) |
| 38 | 0x1068C00 | 0x2C55900 | throw (doc) |
| 39 | 0x1088530 | 0x2C55910 | not opened (target of op0 0x27 from mode 41) |
| 40 | 0x108A130 | 0x2C55920 | throw (doc) |
| 41 | 0x1089BA0 | 0x2C55930 @0x1420256E4 | support hold with hand-side switch (op1 toggles rec+0x3C0 bit30, aim obj +0x6C, rec+0x2C0=0x29); op0 -> 30/32/33/35/37/38/39/42 |
| 42 | 0x1089BA0 | 0x2C55940 @0x1420256FD | same handler (op0 returns 0x2A on another switch) |
| 43 | 0x107E480 | 0x2C55950 | place hold |
| 44 | 0x107D300 | 0x2C55960 | place ready / C4 preview |
| 45 | 0x107DD00 | 0x2C55970 | place draw |
| 46 | 0x107BAC0 | 0x2C55980 | place execute |
| 47 | 0x107ECF0 | 0x2C55990 | C4 detonation |
Full list 1..56: 1 0x1065730, 2 0x1064AE0, 3 0x1065170, 4 0x1065E40, 5 0x0645940, 6 0x1075620, 7 0x1071D70, 8 0x1074070,
9/10 0x106F4E0, 11/12 0x1077840, 13 0x106D5E0, 14 0x1066310, 15 0x10696C0, 16 0x1072FD0, 17 0x1082D10, 18 0x1081A10, 19 0x1080E30,
20 0x1081340, 21 0x1076EF0, 22 0x1082190, 23 0x1084800, 24/25 0x1083B30, 26 0x1083450, 27 0x1084C30, (28..47 above),
48 0x1068370, 49 0x107F450, 50 0x10691B0, 51 0x1086BF0, 52 0x1085B50, 53 0x107FA30, 54 0x1080730, 55 0x107A780, 56 0x107B2F0.
Asm check for 28..42: value at slot T is the `[rbp-0x10]` written just before the MOVUPS that feeds the MOVAPS (e.g. 28: LEA RCX 0x1089190 @0x14202555E,
MOV [rbp-0x10],RCX @0x1420255A2, MOVUPS @0x1420255B0, MOVAPS [0x142c55860] @0x1420255C2).
None of 28/29, 32, 41/42 calls 0xBFF050 or 0x1061D90 (grep of decompilation): they do not issue requests themselves.

## Not opened / bounded
0x105A190 (REA: no procedure at that address), 0x104AF80/0x104AEF0/0x1052530 (func_ stubs), 0x104CF00 sub-helpers
(0x1051CC0, 0x1043C00, 0x1050C80, 0x1045F10, 0x0FC5370), aim helpers 0x1043C80/0x1042F80/0x108ED30, 0x1044B60/0x1044D20/0x104BCB0,
0x103D040 (local-only anyway), all virtual targets (G vt+8, [G+0x120] vt+0x50, ACT vt+*, BS vt+*, [plugin+0x70] vt+*, gm placed-manager vt+0x78/0xF0),
callees of 0x1089190/0x1088A20/0x1089BA0, handlers 0x1088530 (39), and the writers of plugin+0x3D8 / readers of plugin+0x3E8.
