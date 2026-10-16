# Anim round 2: native partner throws, v1

Branch `wp/anim-throws`, based on `766746a5e950e69ad3afd9da945e3e273cd77be3`.
Status (2026-10-15): **implemented, default off.** Hidden run `aaa2a0e0` passed (the host body
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
  Admission additionally requires lab mode, redirected copied saves, the ordinary
  native gun/body admission, current owner/lifetime and a fresh v4 Equip plus v5
  current slot/gun group. Friend/play mode does not admit it.
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
  throwing its own support item during the remote's episode still latches (the
  stock table is shared, so the two cannot be told apart). Unread stock cannot
  admit a new support input.
  `rec+0x240` is logged at episode entry and the observed native release edge.
- **Handle call (WS1, resolved; see "Handle call resolved" below).** Admission also requires
  `rec+0x240` == the Match-verified slot-3 handle (B), and every windowed body-1 job checks the
  row that handle names in the global handle manager (C, detect-only until its hidden run).

## Patch bytes and contract pins

Inspected executable (read only): `D:\Games\steamapps\common\MGS_TPP\mgsvtpp.exe`,
version 1.0.15.4, SHA-256
`085c2f82d1c963c40b3d2d55786661dfee2b18cbbf388a710c00fa76c5e9bb45`.
The generator currently verifies **607 code sites and 128 vtable slots** (589/122 before the WS1
handle-call pins), including all previous reference/containment assertions.

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
- rec+0x240 is written only by the activation `0x105FDB0`: `0x10602E8` stores the handle loaded from
  `Y+0x40[id*stride + rec+0x3D9]` (`0x105FEA5`, `0x105FEB4`), and `0x10602F0` stores rec+0x24C from the same handle.

**Five gun-plugin sites** pass rec+0x240 (a generator census pins exactly these):

| Site | Where | Flag |
|---|---|---|
| `0x10892CB` | mode 28 hold hub (new) | 0 |
| `0x10880D3` | mode 30 op 1 | 0 |
| `0x108AA91` | mode 33 pin event | 1 |
| `0x1087C05` | mode 37 release | 0 |
| `0x1062696` | gun iface reset `0x1061F50` (vtable `0x23B3FA0+0x10`), classes 2/3 only (`0x1062629..0x106263B`), after X vt+0x38 at `0x1062683` (new) | 0 |

The call is safe for body 1 as v1 is built: at every throw call rec+0x240 is body 1's own Match-verified handle
(owner 1, kind 12, this equip), as runs `aaa2a0e0` (`0x12c4c`, `0x12bac`) and `ea93a2e3` (`0x22c4c`, `0x22bac`,
`0x22c4c`) show. The residual risk is the non-input iface reset firing on a stale rec+0x240 whose row an engine-side
free handed to the viewer; it is not shown to be reachable. Three fail-closed pins close or detect it:

- **B (admission).** `partner_support_equip::Ready()` also returns the Match-verified slot-3 handle
  `Y+0x40[1*7+3]`; `SupportAdmitted` builds its record readiness with the pure
  `partner_support_policy::RecordReady`: the previous identity (prim vtable/block/record, rec+0x24C == the remote's
  equip, hand slot 3, no switch bit) **and** `rec+0x240 == that handle`. HOLD/FIRE therefore reach modes 28/30/33/37
  only when every vt+0x160 call hits a row with owner 1, kind 12, the right equip and an in-range index. It may refuse
  one more frame after an equip, until the activation refreshes rec+0x240; that transient logs once per pair:
  `partner_support=rec240_check; ...; reason=pending; rec240=0x..; slot_handle=0x..`.
- **C (per-job row check, detect-only).** After every windowed body-1 job, while the record has B's identity (so never
  during a gun<->support or support<->support switch before `0x105FDB0` refreshes rec+0x240; critic #13) and its class
  `(rec+0x27C>>6)&7` is 2 or 3 (the iface reset's own gate), `partner_support_policy::CheckRow` reads X from the calls'
  own receiver `[prim+0x60]` (vtable `0x234B840` required, as in the writer's Capture) and requires
  `idx = rec240>>16 < [X+0x68]`, row word +0 == `(rec240>>5)&0x7FF` and owner word +2 == 1. Lines (one per handle or
  verdict change):
  - `partner_support=rec240_check; slot=3; equip=N; type=12; reason=ok; rec240=0x..; why=ok; idx=..; capacity=..; row_equip=N; owner=1; class=2|3; checks=..; foreign=0; mode=log; latched=0`
  - `partner_support=rec240_check; ...; reason=unread; why=unread` (manager chain or row unreadable; never latches)
  - `partner_support=refused; slot=3; equip=N; type=12; reason=rec240_foreign; rec240=0x..; why=owner|equip|range|empty; ...; mode=log; latched=0`

  `partner_support_policy::kRec240Latch` is **false**: a foreign row only logs. After a hidden run shows no
  `rec240_foreign`, flipping it to true makes the same verdict call `LatchNativeSupportOff` for the mission
  (`latched=1`, reserved log slot). C cannot stop an in-job native call; it detects an alias before more support
  presses are admitted.
- **E (contract).** New pins: `0x101E1D4` (prim+0x60 store), `0x103C944..0x103C980` (factory/vtables),
  `0x1089289..0x10892D1` (mode-28 site), `0x1062629..0x106263D` and `0x1062676..0x106269C` (iface reset class gate,
  vt+0x38, vt+0x160), `0x105FEA5`, `0x105FF6F`, `0x10602E8..0x10602F7` (the rec+0x240 writer), whole bodies
  `0xDC1740..0xDC17FF`, `0xDC1800..0xDC1901`, `0xDC0350..0xDC0396`, `0xDC64E0..0xDC653D` (in <= 80-byte parts), vtable
  slots `0x234B840`/`0x234B470` +0x160/+0x38/+0x40, and generator assertions (single base-ctor caller, the methods'
  only references are the two vtables, exact function ranges and rets, the class-2/3 branch target, and the census of
  exactly five rec+0x240 -> vt+0x160 call sites). The three existing call-site pins now name the resolved target.
  Counts: **607 code sites, 128 vtable slots** (was 589/122). The writer receipt field is now `rec_handle_risk=pinned`.

Tests: `partner_support_policy_tests` gained 73 checks (1378 -> 1451): record readiness with a mismatching rec+0x240
(other index, equip or kind; no Match-verified handle; switch; previous equip; equip range), the composed `Admit`, the
reader-based record wiring on a fake prim/record, and the row check on fake manager tables (class matrix 0..7; own row
ok with the pin bit set; viewer-owned row; other owner; reused row with another equip word; index == capacity; last
in-range index; empty handles; wrong/sibling manager vtable, no manager, zero or 17-bit capacity, no rows and an
unreadable row are `unread`; the detect-only default never latches; the latch mode latches exactly the foreign verdicts).
The hook static_asserts the pure policy's offsets against the generated contract.

Still open (research "handle-call" Open): when exactly `0x1061F50` runs for body 1; `0xDC6540`, `[X+0x190]` vt+0x100 and
`[X+0x198]` were not opened; the LIFO free-list decode is inferred; the other plugin with six X vt+0x160 callers
(`0x11FB84F` .. `0x11FCAD2`, handles at +0x18 of per-id 0x60 records) stays outside v1 (slots 4/6 refused).

**Hidden run (runtime owner; not run here).** Re-run the throws scenario of `aaa2a0e0`/`ea93a2e3` in lab:
`pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-support-native.txt
-PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab` (MGS_GAME_LUA_DIR unset). Accept when:
scenario.json Closed / BaselineRestored / RealSavesUnchanged; the throws still pass exactly as in `ea93a2e3` (equip 354
and 349, `armed; reason=throw` and `throw_release`, every `stock_check; ...; changed=0`, every guest
`require support;...;counts=12,16383,12,8,12,12,8,8`, thrown filter drops, frames of both throws); at least one
`partner_support=rec240_check; ...; reason=ok; ...; owner=1` per thrown equip (354 and 349) on the guest; **no**
`reason=rec240_foreign` and no `reason=unread`; `reason=pending` lines, if any, only right after an equip/follow
line. Then flip `kRec240Latch` (and its fixture assertion) and re-run once.
