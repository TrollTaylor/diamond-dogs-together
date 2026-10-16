# Anim round 2: native partner throws, v1

Branch `wp/anim-throws`, based on `766746a5e950e69ad3afd9da945e3e273cd77be3`.
Status (2026-10-16): **on by default with the native gun profile in lab and play** (Stage B, at the end). Status
(2026-10-15): **implemented, default off.** Hidden run `aaa2a0e0` passed (the host body
natively threw grenade 354 and magazine 349 on the guest screen; all 229 owner-1 thrown requests
dropped, none passed; the guest's stock unchanged); the review findings in
`docs/evidence/rea-throws-2026-10-15/review-1ca3ce6.md` are fixed below ("Fixes after
review-1ca3ce6"). No release package or installation. Earlier blocked attempts below are historical.

## v1 design and admission

Only thrown support equipment (EQP type 12, slot 3) may receive native HOLD/FIRE.
Placed equipment (type 13), items and slot 6 remain unsupported. RELOAD, ACTION
(0x20) and CQC (0x8000) never pass through support admission. The original native
modes 28..42 perform the body animation; this implementation does not substitute
motions or generate a second real grenade.

- `partner_native_support=0|1` is the only new setting. Default: **on for the native gun
  profile in lab mode** (the local two-window test, after hidden runs `aaa2a0e0` and `ea93a2e3`),
  off everywhere else; a named key always wins (`partner_native_support_given`). Its independent parser helper avoids the main MSVC nesting limit.
  Admission additionally requires lab mode, or two-PC play with link v2 connected
  (WS2 Stage A, "Throws in play" below; play still needs the named key), redirected
  copied saves, the ordinary native gun/body admission, current owner/lifetime and a
  fresh v4 Equip plus v5 current slot/gun group (or, in play, the stall hold).
- `partner_support_guard` installs once. The seven-byte load at `0x108C8E5`
  becomes a short jump through the verified eight-byte int3 padding beginning
  **0x108C888**, then a near stub. Correct displacement is **EB A1**, not EB 9F.
  The stub repeats `mov eax,[r9+0x204]`, then takes `0x108C8FC` for id 1, the
  armed P (published by Arm), support class 2/3 and the armed key, in **every**
  body-1 job: drain window and thread are no longer conditions (review-1ca3ce6:
  refused, unwindowed and pass-through jobs were unguarded). Input admission
  stays window-bound in the hook. Every other call resumes at `0x108C8EC`. The remote branch sets
  counts to 0xFFFF and rec+0x3C0 bit 29, excluding mode 37's stock writes.
  One locked dword publishes the redirect within a cache line. Byte mismatch,
  fingerprint mismatch, allocation/protection/exchange failures refuse it.
- The thrown-service owner filter swaps both vt+8 slots before `0x15E0230`
  can execute. Full u16 owner +0x32 equal to armed player 1 returns 0. This drops
  both real requests and preview requests (+0x3E bit 0), before the copy into
  the shared service+0x100 record. Owner 0, soldiers and every other owner forward
  unchanged. Installed filters persist to cover already queued requests.
- `ObserveEquipmentTick` calls ChangeEquip `0x11AC2E0` only on its existing
  runtime owner. It verifies P+0x214 != 1, P+0x218 != 1, Y+0x30 != 1,
  P+0x70[1] == 1, a live pool/block, zero restriction predicate, equip < 0x800
  of type 12, a bounded sub-index and flags bit 2 set/bit 4 clear. It verifies
  all existing `equipment_stock_writer::PinsIntact` SHA-256 bodies before a call,
  rereads its lease, then verifies the returned row/handle and unchanged local
  row, handles, metadata and inventory vars. Uncertain outcomes latch the writer
  for the rest of the mission (not reset by a re-realize) and close the drain's
  admission through `partner_input_hook::LatchNativeSupportOff`. Neither the
  writer nor the follower runs while body 1 is busy (`partner_support_policy::BodyBusy`:
  unread mode, switch bit, modes 8/11/12 or the throw family 28..42).
  The drain's extended SlotFollow uses native SetCurrentSlot for support with
  the same checks; the old data-only gun path is never used for slot 3.
- Admission rechecks the current row/handle, gun record/equip, guard/filter and
  stock latch for every native consumer. A refused guard Enter strips support
  inputs for that very job. Stale frames or absent valid gun groups refuse.
  Support fire has its own current admission and does not depend on a previously
  observed class-1 gun. While support is enabled, the older unsupported-equipment
  diagnostic cannot weaken placed-item or arm refusals.
  The private required whole-player pose window implicitly sets P+0x218=1;
  **support refuses that mode**, as well as the explicit diagnostic p218 setting.
- Before/after body-1 jobs, read-only digests cover the support-count table
  vars+0x614 (46 u16s) and existing vars+0x548..0x670 inventory block. An episode
  baseline covers only the eight slot-3 stock words (k = 6..13, vars+0x620..0x62F)
  across the aim/throw episode, so the viewer's own reload or ammo pickup no
  longer latches support off; a change logs `partner_support=stock_index; k=..`.
  A changed digest latches support off until the next mission reset. The viewer
  throwing or picking up its own support item during the remote's episode no
  longer latches (WS2 C(b), "Viewer-own rebase" below). Unread stock cannot
  admit a new support input.
  `rec+0x240` is logged at episode entry and the observed native release edge.
- **Handle call (WS1, resolved; see "Handle call resolved" below).** Admission also requires
  `rec+0x240` == the Match-verified slot-3 handle (B), and every windowed body-1 job checks the
  row that handle names in the global handle manager (C; a foreign row latches support off for the mission since
  the hidden runs of 2026-10-16).

## Patch bytes and contract pins

Inspected executable (read only): `D:\Games\steamapps\common\MGS_TPP\mgsvtpp.exe`,
version 1.0.15.4, SHA-256
`085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45`.
The generator currently verifies **614 code sites and 128 vtable slots** (589/122 before the WS1
handle-call pins, 607/128 before its review fixes, 612/128 before the WS2 review fixes), including all previous
reference/containment assertions.

| RVA | Original bytes | Enabled in-memory bytes |
|---|---|---|
| 0x108C888 (padding) | CC CC CC CC CC CC CC CC | E9 d0 d1 d2 d3 CC CC CC; signed rel32 d = stub - (base+0x108C88D) |
| 0x108C8E5 (7-byte load) | 41 8B 81 04 02 00 00 | EB A1 CC CC 02 00 00; only the first dword changes atomically |
| 0x2429AD8 (live thrown vt+8) | 30 02 5E 41 01 00 00 00 | little-endian u64 address of SlotHook<2> |
| 0x24299A8 (base thrown vt+8) | 30 02 5E 41 01 00 00 00 | little-endian u64 address of SlotHook<2> |

Stub/ASLR addresses are process-specific; literal displacement and hook-pointer
bytes cannot be reported without an admitted runtime. No patch was applied to the
installed game or its on-disk EXE. The padding patch is prepared before the locked
load-site publication; neither patch is removed during process lifetime.

New pins cover the stock function/padding/load/continuations and its remote return,
thrown preview copy/null return, rec+0x240 callback sites, ChangeEquip/restriction,
SetCurrentSlot and every audited equip callee. The existing full-body SHA pins
remain authoritative for calls (see `equipment_stock_writer_pins.hpp`).

## Hidden scenario and expected receipts

`tools/worker/scenarios/partner-support-native.txt` enables support and fire census,
sets both local debug god modes, places the prone guest 7 m behind/2 m beside the
host, and captures aim/throw bursts for grenade 354 and magazine 349. It reads each
copy's support counts before and after each throw and returns the host to a gun.
The guest's counts must remain unchanged; the host spends only its own throws.

Coordinator only, under the shared `mgs-coop-verify.lock`, with no other worker:

```powershell
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-support-native.txt -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab
```

A run needs separate permission to make the worker's transient owned installation
changes: this handoff explicitly prohibited writes under `D:\Games` and installation.
No visible launch, Computer Use or real-save access is authorized.

Expected (independently capped families):

```text
partner_support=guard; slot=3; equip=0; type=12; reason=installed
partner_support=equip|follow|armed|refused|filtered; slot=3; equip=354|349; type=12; reason=...
partner_support=armed; ...; reason=throw_release; rec240=0x...
partner_support=stock_check; ...; changed=0
partner_support=refused; ...; reason=stock_leak_mission_latched
partner_fire_filter=summary; kind=thrown; ...; dropped=...; preview_dropped=...
partner_support=guard; ...; hits=...; refused=...; windows=...
```

## Verification and open items

Pure fixtures cover guard id/class/P/thread/window/key, full-owner matching with
preview and soldier/local owners, equip refusal preconditions, slot/type gate matrix,
freshness, support pad mask and the stock latch. A synthetic executable fixture runs
our generated stock stub on local test buffers, including another real OS thread;
it does not run retail game code. A synthetic vtable fixture proves partner previews
never reach the fake shared-record writer while other owners retain their return.
Final verification: `pwsh -NoProfile -File tools/build.ps1 -Configuration Release
-ReleaseGun -BuildDirectory build/native` passed: **253/253 CTest tests**, 334.86 s.
The friend-package fixture passed 1207 checks (no final lock flake). New fixtures:
765 support policy checks, 43 synthetic stock-stub checks, 39 equipment-precondition
checks and 21 thrown-filter/preview checks. The integrated driver suite passed
7603 checks, configuration 782 and debug Lua 18886. A driver regression covers
support without the historical gun-class latch, lost safety admission, the legacy
diagnostic, key off, placed equipment, arms and returning to an ordinary gun.

`python tools/generate_partner_contract.py --check` passed for all 589 sites and
122 slots. Preserved local outputs (ignored build artifacts):
`build/native/support-v1-20261015-final-build.log`,
`build/native/support-v1-20261015-LastTest.log`, and
`build/native/support-v1-20261015-contract.log`.
Built loader SHA-256:
`2571072e250f491d5f265640ffd77f992e5a6bf4b6ad3ebf00e727206b74b82b`.
This loader was not installed. These checks are source, policy and synthetic
execution evidence; they are not retail gameplay or two-PC acceptance.

Still unproved: native throw animation/item visibility, guest burst frames,
viewer arc-preview continuity/counts, rec+0x240 handle behavior, grenade-to-magazine
and return-to-gun visuals, both roles, and two-PC gameplay. (Superseded by the guard
fix below: every body-1 job is guarded now.) Unwindowed or refused
block-1 jobs do not receive the stock guard; persistent owner filtering and the
stock latch are backstops, not a proof that such jobs cannot spend stock. Existing
four unopened helpers remain the audit's bounded unknowns (the per-item vt+0x160 call is
resolved: "Handle call resolved" below). Placed types, arms, full campaign and progression remain outside v1.

## Historical blocked attempts

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
  - the call `[[prim+0x60]] vt+0x160(rec+0x240)` at `0x1087C05` (resolved in WS1: the global equipment-handle
    manager's `0xDC1740`, a per-handle pin bit plus model part; no stock, vars or HUD write; see "Handle call resolved");
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
- **Resolved in WS1 (2026-10-16, "Handle call resolved" below):** `[plugin+0x60] vt+0x160(rec+0x240, flag)` at
  `0x1087C05`, `0x10880D3`, `0x108AA91` and two more sites (`0x10892CB`, `0x1062696`) is the global equipment-handle
  manager's `0xDC1740`: the per-handle "pin pulled" bit and model part of the handle in rec+0x240. **Still
  unresolved, to be watched at runtime:**
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

## Fixes after review-1ca3ce6 (Claude, 2026-10-15)

Findings: `docs/evidence/rea-throws-2026-10-15/review-1ca3ce6.md` (two reviewer lenses, eight
gameplay findings and two native ones; the native lens confirmed the stub bytes, registers, flags,
publication, the thrown filter, the equip preconditions and the pins).

| Finding | Fix |
|---|---|
| **Major: guard only in windowed drain jobs** | The stub's window test (offset 0x19) and TEB-thread read/compare (0x2A, 0x32) are replaced by same-length NOPs, so every offset and jump stays as pinned; Arm publishes the armed P into the stub state before the key, Disarm withdraws the key first. `ShouldForce` has no window/thread inputs. The fixture now executes the stub with a wrong thread id, outside a window and on another OS thread (all forced), a cleared P and a class-1 gun (both original). |
| Episode digest too broad | The episode baseline/compare use the eight slot-3 stock words only (`ReadSupportSlotDigest`), with the changed index logged; the per-job full compares are unchanged. |
| Idle receipts fill the cap | Idle refusals (slot/off/admission/body_busy) log only on a reason change under a 64-line cap; equip/follow/other outcomes have their own 512-line cap; the mission latch line bypasses the hook's line cap. |
| Uncertain latch reset by re-realize | `g_uncertain` resets only on a mission change; the observer also latches the hook (`writer_uncertain_mission_latched`). |
| No busy gate for equip/follow | `partner_support_policy::BodyBusy` (unread mode, switching, 8/11/12, 28..42) refuses Observe (`body_busy`) and the drain's slot-3 follow (`SlotRefusal::mode`). A per-equip dispatch cap is not added: a failed dispatch already latches the writer for the mission, and a successful one makes the next Match pass. |
| Key off changed Round 1 | `partner_support_policy::Request(key, ...)` is false with the key off; NativeView and the drain use it, so `partner_native_support=0` returns the Round 1 mask (fixture: every slot/strip with the key off; `Strip(mask,false,false,false)==mask`). |
| Fixture type matrix not wired | `Ready()` reports the decoded descriptor type and the gate admits only that decoded 12 (`x.type` is no longer hard-coded). A synthetic Capture/Match image for type 13 is not added: `Capture` reads about thirty pinned native chains, and the type-13 refusal is covered by `Refusal()` (fixture) plus the decoded-type gate. |
| Scenario could not detect a viewer overwrite or a count change | After each throw the guest's exact lab-seed counts are `require`d; a new phase has the guest aim its own grenade (its arc on screen) while the host aims and throws, with `viewer-arc-*` captures, then the counts are required again. |

### Hidden run after the fixes: `ea93a2e38edf41d780f1ce275d1b0f40` (passed)

Same command as above, from `e8ebb94`. Closed / BaselineRestored / RealSavesUnchanged, no
ExpectFailures; every `guest require support;...;counts=12,16383,12,8,12,12,8,8` passed: after the
grenade throw, after the magazine throw and after the viewer-arc phase. Guest log:
`partner_support=equip` for 354 (`handle=00022c4c`) and 349 (`00022bac`), `armed; reason=throw` /
`throw_release`, every `stock_check; ...; changed=0` (three episodes), thrown filter
`dropped=317; preview_dropped=314; passed=80` (the 80 forwarded requests are the guest's own arc previews
while it aimed its own grenade), guard `hits=2040` across `windows=6920`. Idle refusals logged 6 lines
instead of the earlier ~70. Frames (contact sheet `C:/Users/taylo/AppData/Local/Temp/mgs-caps/support2/sheet.png`):
the host body's grenade and magazine throws (wind-up, release, follow-through) and the guest holding its
own HAND GRENADE (12) with its arc while the partner throws beside it.

## Handle call resolved (WS1, 2026-10-16)

Spec: `coop-gaps-spec.md` WS1 (research "handle-call", critic #13). Branch `wp/feat-throws` from `wp/integ6`.
Static evidence is from the exact executable (SHA-256 `085c2f82...bb45`), re-read by the contract generator.

**What the call is.** `[prim+0x60]` is X, the process-wide equipment-handle manager `[[gm]+0x98]+0x1E8` (the same
object as inventory Y+0x58 and controller ctl+0x28). The gun prim base constructor `0x101DE90` stores it at
`0x101E1D4..0x101E1EA`; the factory `0x103C910` is the constructor's only caller (`0x103C944`) and then installs
the prim vtables `0x23B3E80/0x23B3F68/0x23B3FA0/0x23B3FE0/0x23B4040`. X's two concrete vtables `0x234B840` and
`0x234B470` both hold vt+0x160 = `0xDC1740`, vt+0x38 = `0xDC64E0` and vt+0x40 = `0xDC0350`; those slots are the only
image pointers to the three methods.
- `0xDC1740(X, h, flag)` sets or clears bit 7 of byte 4 of the 6-byte row `X+0x140[(h>>16)*6]`. When the handle has a
  model instance (vt+0x40 `0xDC0350`: kinds 9+ read row byte 5 bit 0) it calls `0xDC1800`, which for kind 12 toggles
  model part `0xB2149CAFEFD` on the handle's instance slot through `[X+0x190] vt+0x100`, and for equips
  `0x17E..0x180` starts/stops an effect through `[X+0x198]`. It never allocates, frees or reuses a handle and writes
  no stock, vars, HUD, camera or id-indexed player state. **It checks nothing**: no owner, bounds or equip word.
- `0xDC64E0(X, h)` (vt+0x38) can release the handle's model instance through `0xDC6540`.
- rec+0x240 is written only by the activation `0x105FDB0`, at `0x10602E8`, next to rec+0x24C at `0x10602F0`. For a
  kind-12 handle (`h & 0x1F`) it stores the handle loaded at `0x105FEC0` from `Y+0x40[id*[Y+0xC] + rec+0x3D9]`
  (r15 = prim, r14 = rec, esi = id, ctl = `[[[prim+8]+0x138]+0xC8]`, Y = `[ctl+8]`: `0x105FDD8..0x105FE06`; slot
  index `0x105FEA5`; load `0x105FEB4`), and rec+0x24C = X vt+0x20 of that handle (`0x105FF1A..0x105FF3A`, which also
  saves the kind at `[rsp+0x40]`). The kind-12 route to the stores is `0x105FF8A` (not kind 9) -> `0x106019C` (kind
  outside 1..8) -> `0x106046D` (kind 12) -> block `0x10606C9` -> `0x1060767` -> `0x10602A8`. Other kinds rewrite
  ebx/r13d before the stores: kind 9 reloads both from slot 2 (`0x1060000`, `0x1060041`; its own handle goes to
  rec+0x244), and a kind outside 1..8 and 12..14 whose equip `[X+8]` vt+0x50 refuses clears both
  (`0x106066D`, `0x1060678`). The generator checks this by data flow over the activation's control flow graph
  (recursive descent from its entry, the kind-14 jump table `0x1061C70` resolved, calls clobbering the volatile
  registers): on every kind-12 path each pinned writer (prim, rec, id, Y, slot index, handle, kind, rsi = rec+0x1B0,
  r13d) lies on every path to its use and is the last write before it, and the kind-12 route is on every path; over
  all kinds the only other ebx/r13d writes are the two pairs above.

**Five vt+0x160 sites** pass rec+0x240 to X vt+0x160 (a generator census pins exactly these):

| Site | Where | Flag |
|---|---|---|
| `0x10892CB` | mode 28 hold hub (new) | 0 |
| `0x10880D3` | mode 30 op 1 | 0 |
| `0x108AA91` | mode 33 pin event | 1 |
| `0x1087C05` | mode 37 release | 0 |
| `0x1062696` | gun iface reset `0x1061F50` (vtable `0x23B3FA0+0x10`), classes 2/3 only (`0x1062629..0x106263B`), after X vt+0x38 at `0x1062683` (new) | 0 |

**Other X calls with rec+0x240 (unaudited).** The five sites are not every gun-plugin use of the handle. A second
generator census (`REC240_X_CALLS`: edx/r8d = `[r+0x240]` and rcx = `[r+0x60]` (prim) or `[r+0x30]` (iface) in the
48 bytes before an indirect call, neither rewritten nor crossed by a call) pins 26 sites: the five above, vt+0x38 at
`0x1062683`, and 20 more. Two more load rcx before that window and were found by hand (`REC240_X_CALLS_BY_HAND`: the
generator checks each is a call `[r+disp]` with edx/r8d = `[r+0x240]` in the 48 bytes before). Only vt+0x160 and
vt+0x38 were audited; the 17 other methods were not checked for an owner, bounds or equip test. The generator checks
that both concrete X vtables (`0x234B840`, `0x234B470`) hold each method below (`REC240_X_METHODS`):

| Site | X slot (method) | In |
|---|---|---|
| `0x103DF15` | vt+0x28 (`0xDBE590`) | `0x103D370` |
| `0x103F4D7` | vt+0x120 (`0xDC2E40`) | `0x103EB70` |
| `0x1043BB8` | vt+0x140 (`0xDBF180`) | `0x1043B40` |
| `0x1055ED3`, `0x1055F15` / `0x1055EF1`, `0x1055F33` | vt+0x1C0 (`0xDC1450`) / vt+0x1C8 (`0xDC2890`) | `0x1055EA0` (callers `0x106EA4C`, `0x1070CE4`, `0x1070E39`, `0x1079042`; checks only index != 0xFFFF; for kinds 9+ vt+0x1C0 calls `[X+0x190]` vt+0x1B8 on the handle's model instance slot) |
| `0x1056562`, `0x10565AE` (by hand), `0x10565DF` | vt+0x50 (`0xDBF930`), vt+0x60 (`0xDBF5C0`), vt+0x148 (`0xDBDC50`) | `0x1056390` |
| `0x1059664`, `0x105968F` | vt+0x1B8 (`0xDC26C0`), vt+0x1C8 | reload motion segment `0x1059620` (Anim round 1's "motion vt+0x1C8" is this X call) |
| `0x105DBAD`, `0x105DE8F` | vt+0x148 | `0x105DAD0` |
| `0x107905D` | vt+0x1D8 (`0xDC1540`) | reload handler `0x1077840` |
| `0x108B740`, `0x108B775` | vt+0xE0 (`0xDBFCF0`), vt+0xE8 (`0xDC12A0`) | `0x108AFC0` (class-1 guns) |
| `0x1093F10` (by hand) | vt+0xD8 (`0xDC5350`), rec+0x244 instead when rec+0x280 bit 0 | `0x1093A00` |
| `0x1098580` | vt+0x58 (`0xDC5540`) | `0x10982D0` |
| `0x1098939` | vt+0xA0 (`0xDBD910`) | `0x1098670` |
| `0x1099BFC`, `0x1099E63` | vt+0x30 (`0xDC0920`, classes 2/3 only), vt+0x128 (`0xDC2F70`) | gun iface vt+0x8 `0x1099AB0` (it first calls the activation `0x105FDB0` unconditionally) |

The call is safe for body 1 as v1 is built: at every throw call rec+0x240 is body 1's own Match-verified handle
(owner 1, kind 12, this equip), as runs `aaa2a0e0` (`0x12c4c`, `0x12bac`) and `ea93a2e3` (`0x22c4c`, `0x22bac`,
`0x22c4c`) show. The residual risk is any X call above outside B's four input-driven modes reaching a stale
rec+0x240 whose row an engine-side free handed to the viewer. Examples are the iface reset (vt+0x38/vt+0x160) and
`0x1055EA0` (vt+0x1C0/vt+0x1C8, the viewer's model instance). None is shown to be reachable. B guards only the
input-driven vt+0x160 modes. Only C sees a stale handle behind the other users: it checks the row whenever the record
has B's identity and class 2/3 and latches support off on a foreign row, but it cannot stop the call itself. WS4 (type 13 / class 3) must audit this table and cannot rely on the five vt+0x160 sites alone.
Three fail-closed pins close or detect the risk:

- **B (admission).** `partner_support_equip::Ready()` also returns the Match-verified slot-3 handle
  `Y+0x40[1*7+3]`; `SupportAdmitted` builds its record readiness with the pure
  `partner_support_policy::RecordReady`: the previous identity (prim vtable/block/record, rec+0x24C == the remote's
  equip, hand slot 3, no switch bit) **and** `rec+0x240 == that handle`. HOLD/FIRE therefore reach modes 28/30/33/37
  only when every vt+0x160 call hits a row with owner 1, kind 12, the right equip and an in-range index. The new term
  adds no refused frame to a normal equip/follow. Until the activation runs, rec+0x24C still names the previous equip
  (or rec+0x3D9 the gun slot), so the identity already refuses. For a kind-12 handle the activation then stores
  rec+0x240 (`0x10602E8`) before rec+0x24C (`0x10602F0`), and `ReadRecord` reads rec+0x24C first. A mismatch under the
  identity (`partner_support_policy::RecordStale`) is therefore the stale-handle hazard itself: rec+0x24C (X vt+0x20
  of rec+0x240 when the activation ran) names this equip, so rec+0x240 is in practice a same-equip handle on another
  row index. The phase-0 switch detector `0x103AED0` (`0x103AF7B..0x103AFB4`) compares only the slot and the equip
  bits `(h>>5)&0x7FF`, so the per-frame path does not re-run the activation and the state persists. (Only the gun
  iface vt+0x8 `0x1099AB0` calls `0x105FDB0` unconditionally, and when it runs is unknown.) B then refuses support
  until something re-runs the activation (a slot or equip change) and logs once per pair (the mission's first such
  line even past the shared cap). v1 does not force that re-activation; a run that shows the line fails (see "Hidden
  run"):
  `partner_support=refused; slot=3; equip=N; type=12; reason=rec240_stale; rec240=0x..; slot_handle=0x..`.
- **C (per-job row check; detect-only first, latching since the hidden runs).** After every windowed body-1 job, while the record has B's identity (so never
  during a gun<->support or support<->support switch before `0x105FDB0` refreshes rec+0x240; critic #13) and its class
  `(rec+0x27C>>6)&7` is 2 or 3 (the iface reset's own gate), `partner_support_policy::CheckRow` reads X from the calls'
  own receiver `[prim+0x60]` (vtable `0x234B840` required, as in the writer's Capture) and requires
  `idx = rec240>>16 < [X+0x68]`, row word +0 == `(rec240>>5)&0x7FF` and owner word +2 == 1. Lines (one per handle or
  verdict change):
  - `partner_support=rec240_check; slot=3; equip=N; type=12; reason=ok; rec240=0x..; why=ok; idx=..; capacity=..; row_equip=N; owner=1; class=2|3; checks=..; foreign=0; mode=latch; latched=0`
  - `partner_support=rec240_check; ...; reason=unread; why=unread` (manager chain or row unreadable; never latches)
  - `partner_support=refused; slot=3; equip=N; type=12; reason=rec240_foreign; rec240=0x..; why=owner|equip|range|empty; ...; mode=latch; latched=1`

  `partner_support_policy::kRec240Latch` is **true** (2026-10-16). It started false (`mode=log`: a foreign row only
  logged). The hidden runs at `5f11c7b` showed no false positive: lab `1b85d7f2` logged `reason=ok; owner=1` for 354
  (`0x22c4c`) and 349 (`0x22bac`), and the play rehearsals `1be14967` / `dc8cefc8` / `3a3a0913` logged only
  `reason=ok`; none had `rec240_foreign`, `rec240_stale` or `unread`. A foreign verdict now sets the stock latch
  (`g_support_leak`, the same mission latch as `LatchNativeSupportOff`; `latched=1`, reserved log slot): native support
  stays off for the mission and later presses get the old strip. C cannot stop an in-job native call; it catches an
  alias before more support presses are admitted.
- **E (contract).** New pins: `0x101E1D4` (prim+0x60 store), `0x103C944..0x103C980` (factory/vtables),
  `0x1089289..0x10892D1` (mode-28 site), `0x1062629..0x106263D` and `0x1062676..0x106269C` (iface reset class gate,
  vt+0x38, vt+0x160), `0x105FEA5`, `0x105FF6F`, `0x10602E8..0x10602F7` (the rec+0x240 writer), whole bodies
  `0xDC1740..0xDC17FF`, `0xDC1800..0xDC1901`, `0xDC0350..0xDC0396`, `0xDC64E0..0xDC653D` (in <= 80-byte parts), vtable
  slots `0x234B840`/`0x234B470` +0x160/+0x38/+0x40, and generator assertions (single base-ctor caller, the methods'
  only references are the two vtables, exact function ranges and rets, the class-2/3 branch target, and the census of
  exactly five rec+0x240 -> vt+0x160 call sites). The three existing call-site pins now name the resolved target.
  Counts: **607 code sites, 128 vtable slots** (was 589/122). The writer receipt field is now `rec_handle_risk=pinned`.
  Review fixes (WS1-2/WS1-3): pins `0x105FDD8..0x105FE06` (activation operands: prim, rec, id, ctl, Y),
  `0x105FF1A..0x105FF3A` (rec+0x24C = X vt+0x20(handle); the kind), `0x105FF60..0x105FF90` (eax = the kind,
  rsi = rec+0x1B0, the kind-9 branch; replaces `0x105FF6F`), `0x106018A..0x10601A2` (kinds outside 1..8),
  `0x1060465..0x1060473` (kind 12) and `0x1060767..0x106076C` (the kind-12 join); the `0x105FEB4` load was already
  pinned (35 bytes). New generator assertions: the activation data flow above (kind-12 last writers and route, the
  other kinds' ebx/r13d writes, the kind-14 jump table), the `REC240_X_CALLS` census of 26 X calls with rec+0x240,
  the two hand-found calls and the 19 X methods in both vtables. Counts: **612 code sites, 128 vtable slots**.

Tests: `partner_support_policy_tests` gained 73 checks (1378 -> 1451): record readiness with a mismatching rec+0x240
(other index, equip or kind; no Match-verified handle; switch; previous equip; equip range), the composed `Admit`, the
reader-based record wiring on a fake prim/record, and the row check on fake manager tables (class matrix 0..7; own row
ok with the pin bit set; viewer-owned row; other owner; reused row with another equip word; index == capacity; last
in-range index; empty handles; wrong/sibling manager vtable, no manager, zero or 17-bit capacity, no rows and an
unreadable row are `unread`; detect-only mode never latches; the latch mode, now the default, latches exactly the
foreign verdicts).
The hook static_asserts the pure policy's offsets against the generated contract. Review fix WS1-1 added 11 checks
(1451 -> 1462): `RecordStale` (another index, equip or kind and an empty rec+0x240 under the identity are stale; no
Match-verified handle or a switch is not judged; no state of a normal equip/follow, before or between the two
activation stores, is stale).

Still open (research "handle-call" Open): when exactly `0x1061F50` runs for body 1; `0xDC6540`, `[X+0x190]` vt+0x100 and
`[X+0x198]` were not opened; the LIFO free-list decode is inferred; the other plugin with six X vt+0x160 callers
(`0x11FB84F` .. `0x11FCAD2`, handles at +0x18 of per-id 0x60 records) stays outside v1 (slots 4/6 refused).

**Hidden run (done 2026-10-16; see the result below).** Re-run the throws scenario of `aaa2a0e0`/`ea93a2e3` in lab:
`pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-support-native.txt
-PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab` (MGS_GAME_LUA_DIR unset). Accept when:
scenario.json Closed / BaselineRestored / RealSavesUnchanged; the throws still pass exactly as in `ea93a2e3` (equip 354
and 349, `armed; reason=throw` and `throw_release`, every `stock_check; ...; changed=0`, every guest
`require support;...;counts=12,16383,12,8,12,12,8,8`, thrown filter drops, frames of both throws); at least one
`partner_support=rec240_check; ...; reason=ok; ...; owner=1` per thrown equip (354 and 349) on the guest; **no**
`reason=rec240_foreign`, **no** `reason=rec240_stale` and no `reason=unread` (any of them fails the run). Then flip
`kRec240Latch` (and its fixture assertion) and re-run once.

Result at `5f11c7b` (run `1b85d7f2cc794f90a57e665edd2d4189`, lab, research worker): passed like `ea93a2e3`. Every guest
`counts=12,16383,12,8,12,12,8,8` require passed, every `stock_check` had `changed=0`, and the frames show both throws.
On the guest: `rec240_check; reason=ok; owner=1` for 354 (`rec240=0x22c4c; idx=2; class=2; foreign=0`) and 349
(`0x22bac`); the host logged the same for the guest's 354. Neither role logged `rec240_foreign`, `rec240_stale` or
`unread`. The play rehearsals at the same head (below) logged only `reason=ok` too. `kRec240Latch` is therefore now
**true** (`82da4d0`).

Post-flip re-runs at `82da4d0` (lab, research worker, no build running):
- `8dbb4cfdf667467c9a2089427a6a8167` passed the gameplay criteria. It logged three `throw_release` (354, 349, 354),
  every `stock_check` with `changed=0`, and every guest require `counts=12,16383,12,8,12,12,8,8`. The guest logged
  `rec240_check; reason=ok; owner=1; mode=latch; latched=0` for 354 and 349, and the host logged it for 354. No role
  logged `rec240_foreign`, `rec240_stale` or `unread`. The frames show the grenade throw and the magazine throw
  (arm out at `-02`, follow-through at `-03`), and the viewer-arc throw while the guest's own HAND GRENADE stays at 12.
  Its stop.json has `forced_job_termination=true`. Both roles logged `partner_shutdown=hooks_off` and the host exited
  0, but the guest (exit 1223) did not exit within the close wait. That is the known WM_CLOSE slow exit
  (`join-fix-phase2-spec.md` item 3).
- `0c9269ced36f4322b356bee5fb79f72c` (not forced) has the same latch lines but lost the magazine throw. One host
  packet carried an unknown slot during the hold (`partner_weapon_ctx; remote_slot=255;
  remote_equip=-2147483648; remote_act=11`). The gate went `unsupported` (strip 0x58) for about 110 ms, the release
  fell inside that window, and 349 logged no `throw_release`. The frames show the hold, then idle. The same one-packet
  unknown slot appears before the flip (`1be14967`, `a7d164c1`) and is unrelated to the latch (`latched=0`
  throughout).

Open: a single unknown-slot remote packet during a support hold aborts the throw. Holding the last known slot through a
short gap, as the stall freeze does for input, would keep the throw. Fixed at both ends since (unit tests only, no hidden
run yet): see "Unknown-slot glitch" at the end of this document.

## Throws in play (WS2 Stage A + hardening, 2026-10-16)

Spec: `coop-gaps-spec.md` WS2 (research "throws-play", critic #4..#7). Branch `wp/feat-throws`. **Stage B (the default
flip to `gun` in both modes) is done** ("Stage B" at the end): `lab_config.cpp` now defaults `partner_native_support`
to the gun profile in lab and play; before it, play got throws only from a named `partner_native_support=1` (the
worker's `-LabSetting`). Stage A changed no contract site or slot (612 / 128). The WS2 review fixes add two
code sites (614 / 128, below): the viewer-own rebase now also reads instance 0's `gun_state` (rec+0x2C2) and `gun_sub`
(rec+0x3DC) besides the already pinned `gun_prim_vt_rva`, `prim_gun_rec`, `prim_blk`, `gun_mode_cur`, `gun_slot`,
`gun_stock`/`gun_stock_sub`, `gun_flags27c` and `gun_flags`, all on instance 0's own gun record.

**Admission (Stage A).** `partner_support_policy::ModeRefusal(lab, play, link_connected)` -> nullptr (lab; play with
link v2 connected) | `link_not_connected` | `mode`. The observer's `SupportModeReason()` (GetLabConfig().mode and
`partner_net::GetLinkInfo().state == connected`, the `EquipAdmissionReason` pattern) feeds both gates:
`PartnerSupportReady` (the driver's support gate) and `ObserveNativeSupport` (`context.enabled = key && !reason`).
One line per change, only with the key selected: `partner_support=mode; mode=play|lab|other; reason=ok|link_not_connected|mode`.
After a link drop the stock guard and the thrown owner filter stay installed and armed (queued or unwindowed body-1
work may still finish); `Ready` and `Follow` refuse through `ContextFor` because the writer context is disabled.
All other play refusals are unchanged (kPlayRefusedNativeDrive, kPlayRefusedNativeProfile, the forced-off fire
census, the gun-slot call->write downgrade, the process-wide play leak strip, which already covers an in-job stock
leak because the ammo digest vars+0x548..0x670 includes the support table).

**The play SetCurrentSlot exception.** The guarded slot-3 type-12 support follower (`partner_support_equip::Follow`,
native SetCurrentSlot `0x11AFA80`) is the one native SetCurrentSlot allowed in play. Play runs `nslot=call` as `write`
for gun slots, and SlotFollow's slot-3 branch calls Follow for any value except `off`, exactly the combination the lab
hidden runs used (`partner_driver.cpp` Configure, `docs/native-puppet.md` "Release gating").

**Viewer-own rebase (C(b)).** The episode baseline (the eight slot-3 words) spans viewer frames, so the viewer's own
throw or pickup used to latch support off for the mission. Each SupportCheck now also reads instance 0's gun record
(`ReadViewer`: the driver tick's `ReadGun(k=0)` prim/record in `Arm.viewer_gun_prim/rec`, re-validated in the hook:
prim vtable, prim+0x58 == record, never body 1's prim, record or block; read-only, mode 0xFF unread) and W50's count
of body-1 jobs (`partner_defer::Body1Jobs`). The change between the previous bracket's after-read and this bracket's
before-read happened outside body 1's job. `partner_support_policy::ClassifyGap` attributes it to the viewer, and the
episode baseline is rebased by exactly that delta, only when every body-1 job of the gap was bracketed (no other W50
count), no word moved to or from 0xFFFF, and it is either a pickup (increases only, each <= 64) or one throw/placement:
exactly one word down by exactly 1, nothing else, and both of these (WS2 review):
- the viewer **entered its spend step in this gap** (`ViewerSpendEntered`): read at both ends, not spent at the start,
  spent at the end. The spend step is mode 37 with rec+0x2C2 == 2 (the throw block, op 10 at 0x10879BF, sets 1 -> 2 and
  in the same block decrements the count and calls the stock setter ctl vt+0xC0) or mode 46 with rec+0x2C2 == 4/5 (the
  place block, op 9, decrements and then sets 3 -> 4/5). Holding an item (the hold hub 28/29, draw/holster 32, aim,
  C4 in hand 43..47) never attributes, and one throw or placement is credited once (a later gap of the same step,
  which still starts released, is foreign);
- the word is **the viewer's own held item** (`ViewerOwnWord`): rec+0x3DA == 3, word k = 6 + rec+0x3DC (the same
  (slot, sub) pair the spend passes to ctl vt+0xB0 and the classes 2/3 ammo copy passes to store vt+0x68, pinned at
  0x108CB57/0x108CB74), the record's count (rec+0x260, or rec+0x262 with rec+0x27C bit 9) equals the word's new value,
  and rec+0x3C0 bit 29 is clear (with it the throw writes no stock).

Line: `partner_support=stock_check; slot=3; equip=N; type=12; reason=viewer_own; changed=0; kind=throw|pickup; k=6..13;
before=..; after=..; words=..; viewer_mode=..; viewer_state=..; viewer_slot=..; viewer_k=..; viewer_count=..;
viewer_no_spend=..; viewer_prev=..; viewer_prev_state=..; other_jobs=0` (viewer_* at the gap's end, viewer_prev* its
start). Anything else logs `partner_support=stock_gap; ...; reason=unattributed; changed=1; ...` and the unchanged
episode compare latches as before (`stock_index`, `stock_leak_mission_latched`). The per-job compares (full support
table and ammo block around body 1's own job) are untouched, so a body-1 leak inside its job always latches; a
deferred decrement while the viewer only holds (or is not near) a slot-3 item, a decrement of a word the viewer does
not hold, a decrement in the gap after the viewer's own rebased throw, an unbracketed body-1 job in the gap, a double
decrement or a mixed gap all still latch. Residual: a deferred body-1 decrement is credited to the viewer only if it
is the viewer's own held word and lands in the one gap in which the viewer's record enters the spend step; the
viewer's own spend in that gap decrements the same word (bit 29 clear; a count >= 0x3FFF skips the decrement, but then
the record's count no longer equals the word), so both together drop it by 2 and latch. The remaining error is
fail-closed: if the viewer's record has already left the spend step, or holds another item, at the bracket after its
throw (a gap spanning two viewer frames), its own throw latches support off as before WS2. Block-1 sub-jobs
(`p->sub != 0`) are not counted.

**Stall freeze (critic #4).** Admit needs a fresh frame, and Compose marks every frame older than stale_ms stale, so a
250 ms..1 s gap used to strip HOLD|FIRE|RELOAD: body 1 released early, and when FIRE came back still held it could
throw again (the guard never runs it out). Now, inside the stall window (`partner_native_stall=hold`, the play
default, `partner_native_stall_ms` 400; Compose carries a stale sample's slot/equip only there) and for the same
slot-3 equip as the last admitted frame, the main window's `SupportFrame` (`partner_support_policy::StepFrame`) holds
that frame's HOLD|FIRE **level** with no HOLD/FIRE edge anywhere (`partner_input::Freeze`) and marks the frame
`support_frozen`; every consumer admits it with freshness waived only (every other gate rechecked). The driver holds
its support gate through the same window (`partner_support_policy::StallHold`: stale but <= stall_ms, v5 slot 3, the
equip of the last support verdict) and the observer keeps the writer context's slot/equip on the driver's last support
verdict (`partner_support_policy::ContextHold`: `Mission.support_equip` nonzero and its own newest v5 frame, fresh or
stale, <= stall_ms, slot 3, that equip), so `Ready` still answers and the writer dispatches nothing. It is never keyed
on `Mission.support_stall` (WS2 review): the driver sets that only on a tick whose own read was already stale, so a
frame that crossed stale_ms between the driver's read and the observer's used to clear the context and refuse the
whole stall (an early release). Fresh input
resumes through Compose's own edge state (no phantom press); beyond the window the old strip applies. Never against
the driver's own strip (a frame whose HOLD|FIRE `Arm.strip` removes is never admitted or frozen). Lab mode keeps
`partner_native_stall=neutral`, so nothing changes there. Lines (own cap of 128 per mission, so a bad link cannot use
up the shared stock_check cap): `partner_support=stall_freeze; slot=3; equip=N; type=12; reason=start|fresh|strip|off;
level=0x..; age_ms=..; frames=..; held_ms=0`.

**FIRE-edge buffer (C(c)).** A FIRE press while support waits only on body 1's equip (`WaitingOnEquip`: every gate but
Ready/record, decoded type 0 or 12; or the driver's gate one tick behind the hook's) is kept for 300 ms and delivered
on the first admitted frame of the same equip (`partner_input::AddEdges`: the press edge, plus the held bit and a
release edge on the next frame for a tap; AddEdges clears any release edge or release word of a bit it adds as held or
press, so the delivery frame never also carries the tap's own release, WS2 review). While the press is kept, FIRE
(level, edges, words, synthesized tap) leaves the published frame (`FrameOut.strip`, WS2 review): the main window
judges once per frame, but each drain consumer re-judges its full view against live body-1 state, so a consumer that
saw body 1 become ready later in the same frame used to inject the press the buffer then delivered again. Another
equip, leaving slot 3, a stale frame, any other refusal or 300 ms drop it; a fresh press at admission supersedes it. Lines: `partner_support=fire_buffer; ...; reason=buffered|delivered|dropped; held_ms=..`.

Tests: `partner_support_policy_tests` 1462 -> 1582 -> 1639 checks after the review fixes (ModeRefusal table; ClassifyGap verdicts and Rebase/
SlotDigest; episode sequences: the viewer's own throw or pickup during a partner episode does not latch, a body-1 leak
in its own job, a deferred decrement the viewer did not throw, an unbracketed job, a double decrement all still latch;
StallHold table; the Compose -> StepFrame -> Freeze -> NativeView pipeline: stalls of 251..1000 ms keep the admitted
level with no edge, fresh input resumes without a second press, beyond the window / lab neutral / body not ready /
the driver's strip / another equip strip; the FIRE buffer delivered within 300 ms with the tap's release, held press,
dropped after 300 ms, other equip, leaving slot 3, superseded, never latched/unguarded/stale/slot 2/type 13).
`worker_session_tests` validates the play scenario in play mode and the checkpoint companion.

**WS2 review fixes (2026-10-16).** All four findings were confirmed by reading the code; each has a fix and fixtures:
- WS2-1 (high): ClassifyGap credited any single -1 on any slot-3 word to the viewer while its gun mode was 28..47 at
  either end, so a deferred body-1 decrement while the viewer merely held a grenade or C4 (or in the gap right after
  its rebased throw) rebased instead of latching. Fixed as above (spend step entered in the gap + own word); two new
  pins (0x108CB57, 0x108CB74) and contract fields `gun_state`/`gun_sub`. Fixtures: holding in 28/29/30/32/33/42/43/45/47
  with a -1 on another word or on its own word, mode 37 state 1, mode 46 state 6, already released at the start, an
  unread start, another word, a kept count, bit 29, slot/sub, the lab-seed k8 case for 28/32/45, a -1 after a rebased
  throw (same and other word), two separate viewer throws, and the viewer's own placement.
- WS2-2 (medium): the observer's stall branch needed `support_stall`, which a race at stall onset left false. Fixed
  with `ContextHold` on the driver's last verdict; fixtures: driver live at 247 ms then observer at 252 ms (kept),
  fresh again, the window edge, no verdict, other equip/slot, no v5, neutral, stall_ms <= stale_ms, equip range.
- WS2-3 (medium): a buffered press stayed in the published frame, so a later drain consumer that saw body 1 become
  ready in the same frame injected it and the buffer delivered it again next frame. Fixed with `FrameOut.strip`;
  fixtures: two consumers with a mid-frame readiness flip (held press and tap): exactly one press frame per consumer,
  never with its release; still waiting, no FIRE level for a later consumer.
- WS2-4 (low): the delivery frame of a buffered tap carried held + press + Compose's release of the synthesized tap.
  Fixed in AddEdges; the buffer fixture asserts no release at k+1, and an AddEdges unit check.

**Hidden runs (not run here; another agent owns the runtime).** Coordinator only, verify lock, MGS_GAME_LUA_DIR unset:

```powershell
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-support-play.txt -Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab -LabSetting partner_native_support=1 -WanSim off
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-support-play.txt -Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab -LabSetting partner_native_support=1 -WanSim bad
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-support-play.txt -Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab -LabSetting partner_native_support=1 -WanSim awful
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-support-checkpoint.txt -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab
```

The evidence lines are in the scenario headers. Checkpoint save + reload (critic #6) cannot be scripted in play:
world triggers are lab-only by design (`world_link` Configure, `world_wire` ApplyTrigger `not_lab`,
kPlayRefusedWorldProbe, the runner's `worldtrigger` rule), so the persistence half runs on the same redirected
copied-save path in lab (`partner-support-checkpoint.txt`: three partner throws, shared checkpoint, shared death,
host Continue, the guest's counts read back from the reloaded save, one more throw). A play-mode checkpoint needs a
separate decision (a rehearsal-only world-probe admission). Stage B follows only after these pass: the default flip
(`lab_config.cpp` ResolveWeaponDefaults), lab_config_tests matrix, lab_ini_check, friend_package_tests, docs, then a
re-run of `partner-support-play.txt` without `-LabSetting partner_native_support=1`.

Results at `5f11c7b` (2026-10-16). The clean runs passed with frames: off `1be14967`, bad `dc8cefc8` and awful
`3a3a0913`. Each had 4/4 `throw_release`, `changed=0` everywhere, the exact `viewer_own` rebase, `hard_leaves=0` and no
latch. **Stage B was not flipped**, for three reasons:
- The header's network evidence never appeared. No `stall_freeze` start/fresh fired under bad or awful, because no
  input gap over 250 ms fell inside a support episode. The critic #4 path is still unproven at runtime and needs a
  stall-injection preset or step (added since: "Stall test step" below).
- The first off attempt `2e1beba1` lost one throw animation while another agent's build starved both games
  (gaps over 400 ms, so the old strip applied by design).
- The checkpoint run `a7d164c1` stops at the known `host_continue` freeze, so the post-reload half is unproven.

Also open: the host role shows infinite ammo, which was already there before this work, so the host's own spend
cannot be observed. A single unknown-slot remote packet can also abort a held throw (see the post-flip re-runs under
"Handle call resolved"; fixed at both ends since, below).

## Unknown-slot glitch and the stall test step (2026-10-16)

Unit tests only; no hidden run in this step (commands below).

### Why the host sent `remote_slot=255` mid-hold

The guest's lines in `0c9269ce` (the lost magazine throw): `partner_weapon_ctx; remote_slot=255;
remote_equip=-2147483648; type=-2147483648; remote_act=11; body_slot=3; body_equip=349; gate=unsupported; strip=0x58`,
then the observer's `partner_support=refused; slot=255; equip=0; type=0; reason=slot` (its writer context cleared),
`partner_weapon=unsupported; remote_slot=3; remote_equip=349; type=12; reason=slot` on the next tick (the support
verdict needs `Ready` again) and `unsupported_end` about 110 ms later. The same one-frame shape shows in a later run
right after a `throw_release`. `remote_act=11` comes from the pose stream, so only the input frame's weapon fields
were missing.

On the receiver, `remote_slot` is the frame's `inv_slot` only while `remote_v5` holds (version >= 5 with
`GunFlag::Valid`, fresh), else 255. `remote_equip` is `gun_equip` only with `XFlag::Equip`, else unknown. On the sender,
`XFlag::Equip` (`SenderV4`) and `GunFlag::Valid` (`SenderV5`) both come from ONE read of the local player's gun record
(`ReadGun(k=0)` with its fields); `inv_slot` comes from a separate read (`ReadSlots`). A frame that lacks both the equip
and the gun group therefore means `ReadGun(k=0)` failed (or returned without its fields) on that tick. Its `inv_slot`
may well have been 3, but without `GunFlag::Valid` the receiver reports 255. The same tick also cleared
`partner_reload::SenderStep`'s `have`. Both logged cases coincide with the host's own throw transition (the hold hub
to mode 37), while the host's gun record changes state; the host never left slot 3, so it was not a switch frame. The
sender did not log which `ReadGun` check failed. The new `partner_input=sender_hold` line names it, so the next hidden
run shows the exact refusal (`gun_node`, `gun_iface`, `gun_prim`, `gun_rec`, `fields`, ...).

**Corrected by the hidden runs (below, "Hidden runs at 6a89368 and b3d1ba5"):** the sender's reads never failed. The
glitch frames were complete; the receiver's driver read them as "no input" because `partner_net::LatestInput` returned
false while the receive thread held the lock. The sender and receiver holds below stay as defense in depth; neither
fired in any run.

### Fix 1: the sender keeps the last good read

`partner_input::HoldSender` (`partner_input.hpp`): the sender records the last good gun read (equip, magazine, the v5 gun
group with its counters) and the last good slot read. A failed read republishes them for up to `kSenderHoldMs` (250)
after the last good one. A good read always replaces them, an empty hand included, so a real change goes out at once.
Beyond the bound the unknown goes out as before. The driver's three read sites now share `ReadLocalGun`, which also
leaves `SenderStep` alone during a held failure, so a slot change across a failed read is still counted at the next
good one. `m.gun0`/`m.gun0_ok` (the viewer's own spend fields, `ReadViewer`) are untouched: only the wire fields are
held. Lines (own cap): `partner_input=sender_hold; field=gun|slot; reason=start; refusal=<the read's refusal>;
good_age_ms=..`, then `reason=resumed|expired; frames=..; held_ms=..`.

### Fix 2: the receiver debounces an unknown slot/equip during a support episode

`partner_support_policy::StepIdentity` plus `partner_input::HoldIdentity`, applied in `partner_net`'s receive path to
each accepted input frame before it is stored. Every consumer reads that one stored frame: the driver's gate, the
hook's `SupportFrame` and its drain consumers, and the observer's writer context. The driver publishes the equip of each
tick's support verdict (`Hooks::publish_support_identity` -> `partner_net::SetSupportIdentity`; 0 without a verdict, while
not native, during a vehicle job and at `ResetRemote`). While that equip is E and the last complete frame (v5 gun
group, slot 3, equip E) arrived at most `kIdentityHoldMs` (250) ago, a v5 frame whose slot and/or equip is UNKNOWN gets
slot 3, `XFlag::Equip` E and, when it lacked `GunFlag::Valid`, the last complete frame's gun group (counters kept, so
no phantom reload or switch; `GunFlag::AimDir` dropped, its angles are not carried). Its input (held bits, sticks,
edges) is untouched. A known part that names something else (another slot or equip) is a real change and applies at
once, an unknown run longer than 250 ms applies, and no support verdict (gun play, lab or play without the key) holds
nothing. A v1..v4 frame is never held. Lines (own cap of 128 per receive reset): `partner_support=slot_hold; slot=3;
equip=E; reason=held; missing=slot|equip|both; age_ms=..; v5=..; raw_slot=..`, then `reason=resumed|changed|expired|off;
packets=..; held_ms=..`.

### Stall test step (worker only)

The step `stall <host|guest> <ms>` (50..1000) in `Invoke-WorkerScenario.ps1` sends the native debug command
`input_stall <ms>`. `game_script_observer` admits it only with `worker_scenario::Compiled()` and `Active()` (the owned
private worker), `offline-v1`, copied saves, partner and net on. It then calls `partner_net::StallInput`, which is
compiled in only with `MGS_BACKGROUND_WORKER` (CMake defines it for `mgs_coop_lab` only in the worker build) and refuses
with `not_worker_build` in every other build. The Release profile never builds the background worker
(`Get-WorkerPlan`, `worker_checks_tests`), so a friend loader can never pause its input. During the stall `SubmitInput`
sends nothing (the driver's edge sets stay pending for the first frame after it). The peer's newest input frame ages
exactly as in a real gap. Lines: `partner_net=input_stall; reason=start; ms=..; stalls=..; last_tx_age_ms=..` and, with
the first frame after it, `reason=end; ms=..; actual_ms=..; skipped=..; stalls=..`. The runner records the command's
result as an expectation (`^input_stall=started;ms=<ms>;`).

`tools/worker/scenarios/partner-support-stall.txt` (play rehearsal) runs two host grenade throws with one 320 ms stall
each (past stale_ms 250, inside stall_ms 400): A inside the aim (HOLD), then FIRE; B inside the FIRE hold, then the
release. Expected per phase, in order: the host's `input_stall` start/end; on the guest
`partner_support=stall_freeze; ...; reason=start; level=0x8` (A) or `level=0x18` (B), then `reason=fresh`; then exactly
one `throw_release`. No early release, no second throw, every `stock_check` `changed=0`, and the guest's counts
unchanged.

Tests:
- `partner_input_tests` (HoldSender): start, held at exactly 250 ms, resumed, a good read wins, expiry, an empty hand,
  the magazine, independent reads, and the wire frame valid with `GunFlag::Valid`, Equip and slot 3.
- `partner_support_policy_tests`: the StepIdentity table (held, resumed, expired, changed, off, partial identities,
  the verdict's equip, range, a clock that goes backwards); HoldIdentity on frames (the 0c9269ce frame is stored with
  slot 3, the equip and the last gun group and stays valid, its input untouched; a v4 frame and a weapon switch are
  never held); and the receive -> Compose -> StepFrame -> NativeView pipeline (without the hold FIRE drops and comes
  back, two releases; with it one level run and one release; an unknown beyond 250 ms strips; a real slot change
  applies at once).
- `partner_driver_tests`: a failed local read 33 ms after a good one republishes the gun group, equip, magazine and
  slot, with the refusal in the start line, then resumed; beyond the hold the old unknown; the support verdict's equip
  is published, 0 without one.
- `link_udp_tests`, through a real partner_net: the glitch frame is stored held with the held/resumed lines, nothing
  is held without a verdict, and the stall is refused with `not_worker_build` in a non-worker build (or, in a worker
  build, started, silent for 320 ms and ended).
- `worker_session_tests`: the stall step's validation, the new scenario in play mode, the placement of its stalls,
  its waits, and the worker-only gating in the sources.

Hidden runs (not run here; coordinator only, verify lock, MGS_GAME_LUA_DIR unset, no build while they run):

```powershell
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-support-stall.txt -Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab -LabSetting partner_native_support=1 -WanSim off
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-support-native.txt -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab
```

The second re-runs the lab scenario of `0c9269ce`. Its throws must all log `throw_release`, and a
`partner_input=sender_hold` or `partner_support=slot_hold` line, if any, names the frame it kept.

## Hidden runs at 6a89368 and b3d1ba5 (2026-10-16)

Research worker checks passed at both heads (receipts `20261016-111821-6420d2b7...`, `20261016-114947-5bb4f891...`).
Every run: hidden worker, verify lock, `-SaveSeed lab`, `MGS_GAME_LUA_DIR` unset, no build process during the run
(CPU sampled every 2-3 s: averages 19..26 %, peaks 38..63 %), Closed, BaselineRestored and RealSavesUnchanged true,
no `ExpectFailures` except where noted. Frames were converted and inspected for every run: body 1 shows the throw family
in each throw burst, the explosion in the viewer-own phase, the grenade and the magazine in the lab run.

At `6a89368`:

| Run | Scenario | Result |
| --- | --- | --- |
| `6d9dc4f9` | play, WanSim off | 4/4 `throw_release`, `changed=0` everywhere, `viewer_own` rebase, no forbidden line. **One glitch line mid support episode** (between `armed; reason=throw` and its `throw_release`): `partner_weapon_ctx; remote_slot=255; remote_equip=-2147483648; ...; gate=unsupported; strip=0x58`. The throw still completed. No `sender_hold` and no `slot_hold` line anywhere. The same shape appeared twice per role in gun play. |
| `702613c1` | play, bad | 4/4, clean, no glitch; no `stall_freeze` (no gap over 250 ms in an episode). |
| `ea8791c2` | play, awful | 4/4, clean, no glitch; no `stall_freeze`. |
| `c9e3156f` | play, stall | **Failed.** The host's two `input_stall` ran (320 ms, `actual_ms=321/332`, `skipped=18/19`), but the guest never logged `stall_freeze`: at stale_ms its gate went `partner_weapon=unsupported; remote_slot=255; remote_equip=354; reason=type; strip=0x58` and body 1 dropped from aim to stand idle (`partner_act; edge=body; remote=11; body=1`). One `throw_release` per phase still followed after the stall. |
| `9f2d0bfb` | lab regression | 3/3 `throw_release` (the magazine throw lost in `0c9269ce` now completes), clean; one glitch line per role outside a hold. |

Defects and fixes (`b3d1ba5`):
- **The stall hold never engaged.** `TickNative`'s `StallHold` and the observer's `ContextHold` read `native_active`
  from `Driver::settings`, the configured copy, where it is always false (it is set only in `EffectiveSettings`).
  Both now use `partner_driver::NativeActive(d)` (the native drive configured and the arbiter's last step active).
  The policy unit tests passed because they never went through the driver; `partner_driver_tests` now does (a stale
  v5 slot-3 frame at 320 and 399 ms keeps the support gate with strip 0x40, 401 ms strips, `stall=neutral` does not
  hold; it fails without the fix).
- **The `remote_slot=255` glitch is on the receiver.** `partner_net::LatestInput` used `TryAcquireSRWLockShared` and
  returned false while the receive thread held `g_lock`. The driver's three read sites and the observer took that for
  "no input" for one tick (remote_slot 255, equip unknown, gate unsupported). The header promised that callers keep
  their previous sample, but only the input hook (`ConsumeInput`) did. `LatestInput` now waits for the short exclusive
  section, as `SampleRemote` already did on the same thread, and counts the waits (`partner_net=stats; ...;
  latest_waits=N`). `ConsumeInput` stays non-blocking.

Full CTest 277/277. At `b3d1ba5`:

| Run | Scenario | Result |
| --- | --- | --- |
| `33c11da9` | play, stall | **Passed, no ExpectFailures.** A: `stall_freeze; ...; reason=start; level=0x8; age_ms=267` then `reason=fresh; frames=4`, then one `throw_release`. B: `start; level=0x18; age_ms=251`, `fresh; frames=5`, one `throw_release`. During each stall the gate stayed `support` (strip 0x40) and body 1 kept its aim; frames `stall-*-hold` show it, `stall-*-throw-*` show one throw each. Counts unchanged, `changed=0` x4. `latest_waits` 2 (guest), 1 (host). |
| `117620b9` | play, off | 4/4, `changed=0` x10, `viewer_own`, no forbidden line, **no glitch and no unknown-slot ctx line on either role** while `latest_waits` reached 4 (guest) and 7 (host): each wait was a tick that used to read as no input. |
| `e9244c56` | play, bad | 4/4, clean, no glitch. |
| `733e023b` | play, awful | 4/4, clean, no glitch (`latest_waits` 1 on the host). |
| `8c7f3472` | lab regression | 3/3 (grenade, magazine, grenade), clean, no glitch, `latest_waits` 13/4. |

No `partner_support=slot_hold` or `partner_input=sender_hold` line appeared in any run. Under `-WanSim bad|awful` no
input gap over 250 ms fell inside a support episode, so the freeze shows only with the stall step. Still open: Stage B
(the default flip) and the checkpoint post-reload half (`a7d164c1`, the known `host_continue` freeze); the host's
infinite grenades (pre-existing). Some runs end with `Forced=true` (the supervisor terminated the job after WM_CLOSE),
which was also seen before this work; the saves and the baseline were unchanged.

## Stage B: on by default in play (2026-10-16)

After every play rehearsal and the stall proof passed at `b3d1ba5` (above), `ResolveWeaponDefaults` (`lab_config.cpp`)
resolves an unnamed `partner_native_support` to the native gun profile in **both** modes (`gun`, was
`gun && mode == lab`). Nothing else changed: play still admits support only with link v2 `connected`
(`partner_support_policy::ModeRefusal`, the `partner_support=mode` line), the guard, the owner filter, the per-job and
episode stock checks, the viewer-own rebase, the stall hold and the identity hold are as tested, and a named
`partner_native_support=0` (or `1`) still wins in either mode. `-Partner clips` resolves no native drive, so no support.

- **Play-Coop**: `New-CoopPlayLabIniText` writes no `partner_native_support` key (only `partner_native_profile=gun` with
  `-Partner native`), so friends get the loader's default; `friend_package_tests` asserts that the generated host and
  guest lab.ini resolve `partner_native_support=1` (not named), that `-Partner clips` resolves 0, and that an appended
  `partner_native_support=0` resolves 0 with `play_refused=0x0` and no invalid value.
- **Tests**: the `lab_config_tests` matrix (lab/play x off/loco/gun: on exactly for gun in both modes, clean in both;
  named 0/1 win in either key order); `lab_ini_check` prints `partner_native_support` and `partner_native_support_given`.
- **Scenarios**: the headers of `partner-support-play.txt` and `partner-support-stall.txt` drop
  `-LabSetting partner_native_support=1` from their commands (neither file names the key; `worker_session_tests` checks
  both). `partner-support-native.txt` (lab) keeps its explicit `labsetting partner_native_support=1`.

The proof of the default (no `-LabSetting`, no labsetting line):

```powershell
pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-support-play.txt -Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab -WanSim bad
```

Result at `f6bde02` (2026-10-16): **passed, the default is proven.** Research worker checks passed first (receipt
`20261016-122536-7c10d554...`, 9 checks, 271 audited tests; full CTest 277/277). Hidden run `a0d8edf6` (`-Mode play
-WanSim bad`, no `-LabSetting`: session.json `lab_settings: []`, no `native_support` in worker-lab.json, no labsetting
line in the scenario; hidden worker, no SwitchDesktop/ShowWindow/focus call, visible desktop unchanged; verify lock,
`-SaveSeed lab`, `MGS_GAME_LUA_DIR` unset, no build process during the run, CPU avg 20 % / max 40 %):
- Both roles logged `partner_support=mode; mode=play; reason=ok`, which the observer logs only with
  `partner_native_support` on, so the loader resolved the default in play on both copies.
- Guest: 4/4 `armed; reason=throw_release` (3 partner throws, then the 4th after the overlap), `stock_check changed=0`
  x10, `changed=1` x0, the exact `viewer_own` rebase (`kind=throw; k=6; before=12; after=11; viewer_mode=37;
  viewer_state=2; other_jobs=0`), `rec240_check ... foreign=0; latched=0`, thrown filter `dropped=403;
  preview_dropped=399; passed=90` (the guest's own throws pass). Host: its 1 `throw_release` (the guest's own throw
  mirrored), `changed=0` x2. Zero forbidden lines (stock_gap, stock_index, stock_leak_mission_latched, fire_reload_off,
  writer_uncertain_mission_latched, rec240_stale/foreign, unread, S_IS_ONLINE), zero `remote_slot=255` glitch or
  unknown-equip ctx lines, no `slot_hold`/`sender_hold`, `latest_waits` 3 (guest) / 1 (host), `hard_leaves=0`,
  `latched=0`.
- All 7 `require` lines met (`ExpectFailures` empty): the guest's counts `12,16383,12,8,12,12,8,8` after each partner
  throw and `11,...` after its own. Closed, BaselineRestored and RealSavesUnchanged true; `Forced=true` as before.
- Same shape as the named-key run `e9244c56` (same counts per line kind, the same three per-load `refused; slot=255`
  lines before gameplay). No `stall_freeze` (no input gap over 250 ms fell inside an episode; `max_gap` 923 / 892 ms
  outside them), as in every `-WanSim bad` run; the freeze is proven by `33c11da9`.
- Frames (converted to PNG and inspected): `grenade-throw-1..3` show body 1's wind-up and release/follow-through
  (throw-1 frames 02..04, throw-2 and throw-3 frames 03..04, then back to the stand with the rifle on its back); `viewer-own-throw-02` the guest's
  own grenade in hand; `viewer-own-partner-throw-02/03` body 1's wind-up and release behind the guest after the
  overlap, then the guest's own grenade exploding (04..08).

Open (unchanged): the checkpoint post-reload half (`a7d164c1`, the known `host_continue` freeze), the host's infinite
grenades (pre-existing), and a new friend package (0.12.6 keeps throws lab-only).
