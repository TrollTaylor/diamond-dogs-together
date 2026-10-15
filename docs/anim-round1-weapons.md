# Anim round 1, track W: partner weapon animations

Branch `wp/anim-weapons` (base `20f4afe`). Static analysis of `mgsvtpp.exe` 1.0.15.4 (SHA-256 `085c2f82...bb45`)
only; nothing here has run in the game yet. All addresses are RVAs. Every new code site and vtable slot is pinned
in `tools/generate_partner_contract.py` (`--check`: 554 code sites, 113 vtable slots, plus the new
`anim_round1_assertions`) and in the generated `native/partner_player_contract.hpp`; the runtime code gate checks
the same bytes before any install.

Sources: `critic.md` section 6 (P0.1, P0.2, P0.4, P0.5, P2.7), `weapon-anims.md` 4.1 / A3 / A5 / A6,
`action-anims.md` (act table), the live logs it quotes (G:810328, G:812166, G:816270, the 10:35 run).

Nothing in this track touches camera code (`coop_carry*`, `teammate_targeting*`, `native_ui*`, the P+C0 setter,
`RequestToPlayCameraNonAnimation`) or the ride/visibility section of `partner_driver.cpp` (it only reads
`m.visibility.hidden` and the ride wire store).

## 1. What changed

| # | Change | Default (gun profile) | Key |
|---|---|---|---|
| 1 | Unsupported-equipment gate: `Gate::unsupported`, strip HOLD\|FIRE\|RELOAD (0x58) | on | `partner_native_unsupported=0\|1` |
| 1b | Holster (map slot > 2 to an empty body slot) | not implemented (section 3) | none |
| 2 | State-driven aim intent (HOLD from v5 Aiming / the remote's aim act) | `state` | `partner_native_aim_intent=state\|pad` |
| 3a | `rec+0x3E8` rewritten on every slot change and whenever it exceeds the capacity | always (mirror) | none (part of the mirror) |
| 3b | Reload start during a brief mismatch/switch: queued, replayed once matched | always (`reload=event`) | none (part of the event machine) |
| 3c | Reload duration parity: adaptive `rec+0x310` write | **off** | `partner_native_reload_rate=off\|adapt` |
| 4 | Auto-fire stop when the remote's magazine is dry | on | `partner_native_dry_stop=0\|1` |
| 5 | Fire/projectile/thrown/placed census (read-only) | **off** (lab only; play forces 0) | `partner_fire_census=0\|1` |
| 6 | Static RE: throw/place/arm creation sites and the Round-2 plan | report | section 7 |
| 7 | `partner_weapon_ctx` per-change line; `partner_native_gun` no longer blind after 64 changes | always | none |

"Gun profile" = `partner_drive=native` with `partner_native_gun=1` (`ResolveWeaponDefaults`); outside it the
gate/stop keys are off and the aim intent is `pad`. Named keys always win. The keys are in `lab_config`,
`tools/lab/LabCommon.psm1` (lab.ini writer) and the worker `-LabSetting` / `labsetting` allow-lists
(`Invoke-WorkerScenario.ps1`, `Start-WorkerSession.ps1`).

Why some defaults are off: 3c writes game memory in a new place (instance 1's own gun record) and its effect on the
reload motion is inferred statically (section 4); 5 swaps four extra read-only vtable slots (forwarders). Both wait
for a hidden test. 1, 2, 4 only remove or synthesize pad bits on body 1's existing native paths.

## 2. Unsupported-equipment gate (critic P0.1, weapon-anims 4.1)

- `partner_slots::JudgeUnsupported` (pure): the remote's live v5 slot (input frame `inv_slot`, the sender's equip-row
  nibble) above 2 (3 support, 4 item, 6 hand; ChangeEquip `0x11AC4AA..0x11AC5AF`: types 12/13 -> 3, 17 -> 4,
  14 -> 6), or its in-hand equip type (v4 `Equip`, `rec+0x24C` of the sender's gun record; the raw id, else the
  manifest base of an alias) above 9. A shield (9) keeps the old projectile verdict (FIRE stripped): it rides in the
  back slot with the secondary gun in hand. An unread type and an unknown slot never judge unsupported.
- `partner_driver.cpp` `TickWeapons`: computed after `Judge`; it wins over every other verdict and clears the
  stand-in. The type is read once per remote equip value (`h.equip_type`). `StripBits(unsupported) = 0x58`; the
  magazine mirror freezes (RELOAD stripped), the reload machine falls back with `reason=weapon` (no wait), the
  raw HOLD counts as `hold_stripped` (DASH stripped, aim-speed parity, as for any stripped aim).
- When the remote puts the equipment away while body 1's gun record is unread, the verdict becomes `mismatch` until
  the next readable tick judges (FIRE/RELOAD stay stripped, HOLD passes).
- Lines (own cap 256, on change of (verdict, slot, equip, type)):
  `partner_weapon=unsupported; remote_slot=3; remote_equip=354; type=12; reason=slot; body_equip=46; strip=0x58`
  and `partner_weapon=unsupported_end; ...` when it clears. The `partner_weapon=match|mismatch` and
  `projectile|type_ok` lines are suppressed while unsupported (no misleading "match"). state.json:
  `weapon.gate "unsupported"`, `fire.refused "weapon_unsup"`, `strip_bits 0x58`.
- The slot follower still refuses targets above 2 (`partner_slot=refused; reason=other; target=3`, once per episode):
  body 1 keeps its gun in hand, lowered (HOLD stripped). See section 3 for the holster.

## 3. Holster: why it is not implemented

Option 1 of weapon-anims 4.1 (write an empty body slot into instance 1's equip-row nibble in `SlotFollow`,
`partner_input_hook.cpp`) was investigated statically and **could not be contract-proven**:

1. The data-only follower is proven only for slot types 0/1/2 (5/6): `SetCurrentSlot 0x11AFA80`'s jump table
   `0x11AFE6C` sends those to the row+0 store and the non-local exit `0x11AFE4B` before any call. Type 3 (support)
   goes to `0x11AFC2C`, which writes the sub-index nibble at `row+0x37` and then **always calls ctl vt+0xA0**
   (`0x11AFC77`) before the locality test; type 4 (item) calls ctl vt+0x70 (`0x11AFC94`). A data-only write for
   nibble 3/4 would skip calls whose effects on a non-local controller are unknown.
2. Holstering to an empty gun slot (nibble 0..2 with an empty handle) reaches gun phase 0 `0x103AED0` -> the
   activation `0x105FDB0` -> the empty-handle branch `0x105FED2` -> `0x10517C0` (un-equip, then `rec+0x3C0 |= 0x400`).
   `0x10517C0` and its callees (model detach, parts and controller notifications) were not proven free of
   local-only side effects (UI, vars) for a non-local id; the follower has always refused empty slots
   (`reason=empty`) for that reason.

So option 2 ships: HOLD|FIRE|RELOAD stripped, the gun stays in hand lowered. A holster key would need (a) a
reachability proof of `0x10517C0` for a non-local id like `nonlocal_shoot_reach()` in the contract tool, and (b) a
hidden test of the un-equip/re-equip cycle on body 1.

## 4. Reload correctness (critic P0.2)

### 4a. The completion base `rec+0x3E8` (the 41/11 handgun)
The non-local completion `0x1054900` builds the new magazine as `base + min(rounds, capacity - base)`
(`0x1054A3C..0x1054A4E`, unsigned 16-bit): a base left from the rifle (31) above the handgun's capacity (11) gave
41. `partner_native::PrepareMagazine` now reports `slot_changed`, and `ShadowRewrite` makes the mirror's validated
write also set `rec+0x3E8` on a reconcile, on a slot change of body 1 (`rec+0x3DA`) and whenever the base exceeds
the capacity of the weapon in hand. A slot change seen while the mirror was frozen (switching strips RELOAD) is
caught at the first write after the freeze (the history keeps the old slot). Line (own cap 256, per new rewrite):
`partner_reload=shadow; reason=slot|cap; slot_writes=..; cap_writes=..; body_slot=..; mag=..; cap=..; m3e8=..`
(hook totals `NativeStats::mag_m3e8_slot` / `mag_m3e8_cap`, also counted in `mag_m3e8_writes`).

### 4b. Reload during a weapon switch (G:812166 `reason=weapon`)
`partner_reload::State::waiting`: a remote reload start while the gate strips RELOAD only transiently
(`In::weapon_wait`: gate mismatch or switching, not unsupported, not gun-absent, no leak strip) is queued
(`partner_reload=queued_weapon`), and replayed with one tap once the gate passes (`partner_reload=replay`, then the
usual retry/entered/complete). It falls back with `remote_done` (the remote finished first), `weapon` (the gate turned
permanent), `weapon_timeout` (`Params::wait_ms` 3000) or the usual body/cls/gate/no_reload reasons. The mirror is held
while waiting.

### 4c. Reload duration parity (`partner_native_reload_rate=adapt`, default off)
- Not on the wire: v4 `rates` carries dash/crawl/crawl-factor only (`partner_input.hpp` `rate_dash..`). A new
  input-frame version (v7) would touch `player_sync`, `partner_net`, the observer hooks and the negotiation, all
  outside this track: too invasive. Adaptive correction instead (the task's fallback).
- How the rate works (pinned): the reload handler `0x1077840` (modes 11/12) op 1 stores the per-id provider's answer
  `*(*(P+0x138)+0x130)` vt+0x28(id, 6) in `rec+0x310` (`0x10778F0`), doubles it with the per-id status bit 26
  (`0x1077922`), and op 2 (the exit) writes 1.0 (`0x10790D9`); these are the **only** stores to `+0x310` inside the
  handler (assertion). The motion segment requests `0x1059620` / `0x10596B0` pass the current value by value to the
  motion controller vt+0x1C8; the enter requests the first segment, the per-frame op 0 requests the later ones
  (`0x107A550`). So a value written after the enter changes the later segments only.
  **0x10778F0 is not patched** (that patch was refused by a safety classifier before).
- Policy (`partner_reload`, pure): the machine pairs body 1's reload with the remote reload it followed (start within
  `pair_ms` 2000 of body 1's enter; whichever exits last completes the pair: `Out::learn`), and `RateLearn` updates
  `k *= (body_ms / remote_ms)^0.7`, clamped 0.5..3.0; pairs shorter than 300 ms or off by more than 3x are ignored; a
  k pinned at a bound while the ratio stays outside 1/1.15..1.15 for 3 reloads latches the adaptation off (k = 1:
  "no effect"). The hook (`MirrorRate`, drain window before the job, after the magazine mirror) writes
  `RateTarget(native, k)` (0.25..4) into instance 1's own `rec+0x310` only while its gun mode is 11/12, after
  re-validating the prim chain and a gun class; the first look at a reload records the native value; a fault turns
  it off until MissionStart.
- Lines: `partner_reload=rate; setting=adapt; verdict=learned|short|ratio|saturated|latched; body_ms=3020;
  remote_ms=2027; ratio_milli=1489; k_milli=1000,1321; samples=1; latched=0` (also logged with `off`, verdict
  `off`, as parity evidence) and at body 1's exit `partner_reload=rate_applied; k_milli=..; reloads=..; writes=..;
  refused=..; last=none|off|prim|cls|native|fault; native_milli=1000; written_milli=1321; faulted=0; len_ms=..`.

## 5. State-driven aim intent (critic P0.5 / 4.6)

- The problem: aim was replayed from the remote's pad only; the 10:35 run had the remote in acts 11/13 for 20 s with
  `held=0x0` and `remote_flags=0x0` while body 1 idled. Toggle/hold aim is a per-machine option, and the remote's
  raw HOLD bit is absent with toggle aim.
- Intent = remote pad HOLD || v5 `GunFlag::Aiming` || the remote's act in {11..17, 19, 20}
  (`partner_slots::AimAct`).
- Driver gate (`AimGate`, every tick, `TickAimIntent`): `partner_native_aim_intent=state` with the gun stage, the weapon
  gate `match` or `standin` (body 1 showed a gun), the remote's live v5 slot 0..2, not sleeping (the partner sleep
  owner), not carried (`coop_carry`), not hidden (ride guard), not riding (ride wire kind != foot), and the remote's
  act not dead/carried/stun/down/heli/vehicle/horse/walker/sleep/DM. Refusal names:
  `off|gate|slot|sleep|carried|hidden|ride|act`.
- Arm fields (not compared by `SameArm`): `aim_intent` (the gate passed) and `aim_act_until_ms` (now + 150 ms while the
  remote's act is an aim act; a stale Arm drops it).
- Hook (`RunWindow`, the main window, per frame): `AimFrame` on that window's sample: the pad bit first (never added),
  then a valid v5 Aiming flag, then the act; a stale sample adds nothing. When it adds, the **sample's held word**
  gets HOLD before `Compose`, so the press/release edges, the level, the hold history (`hold=own`) and the
  `PP+0x27C` latch (`Latch27c` from the held "now" word) follow exactly as for a held RMB, and every drain/walk view
  inherits it. `g_prev` keeps the received sample unchanged. HOLD drops in the first main window after the pad bit,
  the flag and the act have all cleared (the act through the next Arm); a stale/lost link releases through
  Compose's own grace (no stuck HOLD). The weapon gate still strips it downstream (unsupported: 0x58).
  Note: with `partner_native_hold=net` the synthesized HOLD is not in the remote's `long12` word (the hold classifier
  sees a fresh press); the gun profile uses `own`.
- Lines (own budget 2000/mission, at most one per 100 ms, on change of (source, refusal, gate, act aim)):
  `partner_aim_intent=on; source=flag; refusal=none; remote_act=11; act_aim=1; flags=0x7; pad_hold=0; gate=match;
  slot=1; added_flag=..; added_act=..; pad_windows=..` (the last three are the hook's main-window totals
  `NativeStats::aim_added_flag`, `aim_added_act`, `aim_pad_windows`).
- Known limit: binoculars (SUBJECT) may sit in an aim-family act with the gun record not aiming; if the 10:35 case was
  binoculars, the act source makes body 1 aim its gun instead (critic T8 is still open). `partner_native_aim_intent=pad`
  restores the old behaviour.

## 6. Auto-fire stop (critic P0.4)

`partner_slots::DryStep`: once the remote's v4 magazine has read 0 for `kDryGraceMs` (50 ms: the sample that spent the
last round still delivers its FIRE edge, the mirror shows that round) and the remote is not reloading, FIRE is
stripped (`weapon_strip |= 0x10`) until a reload starts (v5 Reloading or mode 11/12) or the magazine reads above 0 (a
refill, a switch, a pickup). No v4 magazine: never stripped. Lines (own cap 256):
`partner_fire=dry_stop; remote_mag=0; remote_cap=30; reason=remote_empty; stops=1; strip=0x10` and
`partner_fire=dry_clear; ...; reason=reload|mag|no_mag`. state.json `fire.refused "remote_dry"`.

## 7. Static RE: thrown, placed and arm objects (the Round-2 plan for native throws)

All verified from the bytes and pinned (contract sites/slots listed); "inferred" marks what is not.

### 7.1 The gun-record modes that play them
The mode table `0x2C556A0` (56 entries, emulated from its initializer) holds the throw/place/arm handlers. Their
upper-graph state requests (StrCode64 immediates found in each handler):

| mode(s) | handler | upper states requested | notes |
|---|---|---|---|
| 37 | `0x1087210` | (throw release) | builds the thrown request (7.2) |
| 39 | `0x1088530` | `stateThrowAction` (`0x10886CB`) | |
| helpers | `0x105AFA8` / `0x105F960` | `stateBehindThrowAction`, `stateThrowAction`, `stateThrowSwitch`, `stateThrowHoldEnd` | called from the throw modes (inferred) |
| 43 | `0x107E480` | `statePlaceHoldStart` | |
| 44 | `0x107D300` | `statePlaceHold` | |
| 45 | `0x107DD00` | `statePlaceHoldEnd` | |
| 46 | `0x107BAC0` | `statePlaceHold` | builds the placed request (7.2) |
| 47 | `0x107ECF0` | `statePlaceSwitchOn` (C4 detonation) | |
| helpers | `0x1059EC0` / `0x105A080` | `statePlaceLowerAction` | |
| 52 | `0x1085B50` | `stateStunArmAction` | no gm service call in the handler; effect path not traced |
| 53 | `0x107FA30` | `stateRocketArmAction` | creates the rocket punch (7.2) |
| 54..56 | `0x1080730`, `0x107A780`, `0x107B2F0` | `stateRocketArmAction` | |

These are gun-record modes run by the gun update for any instance (like reload, modes 11/12), so once body 1 holds
the equipment and receives HOLD/FIRE natively, it plays these upper-graph states itself (graph 2; critic verified
`stateThrowHoldEnd` and `statePlaceSwitchOn` in graph 2/3). ChangeEquip maps types 12/13 -> slot 3, 14 -> slot 6
(critic section 1).

### 7.2 Where the objects are created (filter sites; all owners at request `+0x32`)

| object | builder | service call | request fields | gating for instance 1 offline |
|---|---|---|---|---|
| thrown (grenades, smoke, decoys, magazine) | mode 37 `0x1087A4D..0x1087BF2` (base `rbp-0x79`) and helper `0x1061D90` (base `rbp-0x39`, called at `0x10879ED`) | `*(*(svc+0x98)+0x200)` vt+8 = **`0x15E0230`** | `+0x32` owner `id & 0x1FF`; `+0x3C` equip (low 11 bits, `rec+0x24C`); `+0x3E` flags (bit 0 tested by `0x15E0230`) | skipped only when `P+0x204` bit 8 (S_IS_ONLINE) **and** id != `P+0x218` (`0x10879BF`, `0x1087A27`): **offline, instance 1 would create a real object** |
| placed (C4, mines, decoys, cage) | mode 46 `0x107C7DC..0x107CA33` (base `rsp+0x60`, frame `rbp = rsp+0x100`) | `*(*(svc+0x98)+0x208)` vt+0x48 = **`0xFCCD80`** | `+0x00` position, `+0x10` rotation, `+0x20` attach target id (`0x8800...` \| `rec+0x3D8`), `+0x30` equip, `+0x32` owner, `+0x3C` flags | no locality test on this path: **instance 1 would place a real object** |
| class-3 projectile (grenade launcher 7, missile 8) | the model group shot `0x1302F50` -> `0x1303450` (base `rbp-0x70`) | `*(*(svc+0x98)+0x210)` vt+0 = **`0x1337760`** | `+0x2E` projectile id, `+0x32` owner = shot record `+0x2A` (`0x13035D3`), `+0x34` | the shot handler runs for every instance (as RequestBullet): **instance 1's launcher shots would be real** |
| rocket punch / Mosquito (arm, slot 6) | mode 53 op 10 `0x1080321..0x108064F` (base `rsp+0x40`) | the projectile service vt+0x58 = **`0x1337920`** | `+0x32` owner `id & 0x1FF`, `+0x2C`, `+0x3E` | the block is entered only after `test byte [rec+0x3C0], 1` at `0x1080314` (local bit, clear for instance 1; no other entry, op table `0x10806FC`): **local-only, a body-1 punch would be visual only** (inferred until a census confirms) |
| stun arm | mode 52 | not located | | unknown |
| bullets (types 1..6) | `0x1041110` | RequestBullet `0xD32C60` (already filtered) | `+0x2A` owner | filtered today |

Service vtables (each request function sits only in its service's live and base iface vtable, read-only data, no
direct caller; contract `anim_round1_assertions`): thrown `0x2429AD0` (factory `0x15DE2AA`) / `0x24299A0` (dtor
`0x15DDF9B`, clears gm+0x200); placed `0x23A9FD0` (factory `0xFC79DA`) / `0x23A9D20` (dtor `0xFC52DB`, clears
gm+0x208); projectile `0x23E2D50` (factory `0x133648A`) / `0x23E2BD0` (dtor `0x1336065`, clears gm+0x210). Each
request function takes (this, request) only (prologues pinned); the gun-code call sites ignore rax.

### 7.3 Round-2 proposal
1. **Owner filters** in `partner_fire_filter` on the census slots, armed with instance 1's id like RequestBullet:
   thrown (`0x2429AD0`/`0x24299A0` +8), placed (`0x23A9FD0`/`0x23A9D20` +0x48), projectile (`0x23E2D50`/`0x23E2BD0`
   +0): drop a request whose `+0x32` equals the armed id, forward everything else (soldiers throw grenades through
   the same thrown service: their owner type bits are non-zero). Decide the drop return value per function first
   (rax is unused at our call sites; other callers unchecked). The rocket slot (+0x58) needs no filter if the census
   confirms it is local-only.
2. **Gate**: let `BulletType` admit 7/8 while the projectile filter is live; let FIRE/HOLD reach body 1 for slot 3
   and 6 only while the thrown+placed filters are live (and the explosion/noise census is clean).
3. **Equip and follow**: ChangeEquip for slot 3 (type 12/13, `EQP_BLOCK_COMMON`, no preload) and 6 (type 14), plus
   a follower proof for slot types 3 and 6 (`0x11AFC2C` calls ctl vt+0xA0; `0x11AFC82` vt+0x70 for items): either
   prove those calls for a non-local id or use the call variant (lab first).
4. Census first (section 8): confirm owners (instance 0 throws must show `owner=0x0` on the host, body 1 nothing),
   the explosion's noise/damage owners, and whether the rocket punch is local-only.

## 8. Census (lab key `partner_fire_census=1`, critic P2.7 / weapon-anims A5)

- Recording happens inside the existing bullet and noise hooks (RequestBullet slot 0 of `0x2337DE8`/`0x2337D18`,
  AddNoise slot 0 of `0x21876A8`) for **every** owner, before the filter's own verdict (unchanged), plus four
  forwarding slots installed by the bullet filter's `Install` when the census is on (once per process): projectile
  (vt+0), rocket (vt+0x58), thrown (vt+8), placed (vt+0x48), section 7.2. A slot that fails its prologue or slot check
  is refused and left alone (`slot_refused`). The hooks write a 256-entry lock-free ring (any thread); the driver
  drains it every tick.
- Fields: bullet: owner `+0x2A`, bullet id `+0x2E` (u16, from the equip parameters `+0x74/+0x76`), kind `+0x30`, sub
  `+0x31`, flags `+0x32` (`0x1303170`, pinned); noise: owner `+0x12`, kind `+0x10`, flags `+0x11`; slots: owner
  `+0x32`, id/aux as in 7.2. Field meanings beyond the owner are raw (inferred).
- Lines: `partner_fire_census=on; projectile=live; rocket=live; thrown=live; placed=live` (or the refusal per slot),
  `partner_fire_census=<bullet|noise|projectile|rocket|thrown|placed>; owner=0x0; owner_index=0; owner_type=0; id=..;
  b=..,..,..; aux=..; dropped=0|1; fields=1; thread=..; seq=..` at most 20/s and 4000/mission (the rest counted), and
  `partner_fire_census=summary; bullets=..; noises=..; projectiles=..; rockets=..; thrown=..; placed=..; recorded=..;
  skipped=..; lost=..; slots=live,live,live,live` when lines were skipped or lost.
- Stimulus helpers: `debug support` (read-only `vars.supportWeapons` list) and `debug slot
  <hip|back|secondary|support|item|hand> <sub>` (the retail `Player.SetCurrentSlot{slotType, subIndex}`,
  `0x9DDD30` -> ctl vt+0x10 for the local player), `lua/coop/debug_equipment.lua`.

## 9. Telemetry (A6)

- `partner_weapon_ctx; remote_slot=..; remote_equip=..; type=..; remote_act=..; body_slot=..; body_equip=..; gate=..;
  unsupported=..; strip=0x..; aim=<AimRefusal>; dry=0|1; changes=N`: on every change of (remote slot, equip, type, act
  outside locomotion 1..6, body slot, body equip, gate, unsupported verdict), at most every 100 ms, no cap (bounded by
  the change rate; `changes` counts the changes folded into the line).
- `partner_native_gun; ...; changes=N`: change lines at most every 100 ms on their own cap (3600/mission; the old
  64-line cap went blind about 30 s into a mission: `partner_native_gun=capped; limit=64`), the 1 Hz line continues
  while the state keeps changing.

## 10. Hidden tests (coordinator) and expected log signatures

Static first: `python tools/generate_partner_contract.py --check` -> `554 code sites, 113 vtable slots ... match`.
Both logs: `partner_code_gate=ok`.

1. **Census + unsupported gate**: `tools/worker/scenarios/partner-fire-census.txt` (header lists every expected
   line). `-PartnerProfile native -NativeStage gun -BuildProfile research`. Key evidence:
   - both: `partner_fire_census=on; projectile=live; rocket=live; thrown=live; placed=live`;
   - host fire: `partner_fire_census=bullet; owner=0x0; ...; dropped=0`; guest fire on the host:
     `partner_fire_census=bullet; owner=0x1; ...; dropped=1`;
   - host throws support entry 0: host `partner_fire_census=thrown; owner=0x0; ...; id=<grenade id low bits>`; guest:
     `partner_weapon=unsupported; remote_slot=3; remote_equip=<id>; type=12; reason=slot; ...; strip=0x58`,
     `partner_weapon_ctx; remote_slot=3; ...; gate=unsupported; unsupported=slot; strip=0x58; ...`, frames: body 1
     does **not** raise its rifle during `support-aim`, and **no** `partner_fire_census=thrown; owner=0x1` on the guest;
   - placed entry 1: host `partner_fire_census=placed; owner=0x0; id=<equip>`;
   - arm: host `partner_fire_census=rocket; owner=0x0` only with a rocket arm; guest `remote_slot=6; ... type=14`;
   - back to the gun: guest `partner_weapon=unsupported_end; ...`, `partner_aim_intent=on; source=pad`, FIRE live.
   Report: every census line whose owner is neither 0x0 nor the peer's partner id, the explosion lines, any
   `slot_refused`.
2. **Reload queue, shadow and rate**: `tools/worker/scenarios/partner-native-weapons.txt` twice: default, then
   `-LabSetting partner_native_reload_rate=adapt`.
   - `switch2-reload` (secondary): `partner_reload=complete; ...` with `gun.mag <= gun.cap` and `gun.m3e8 <= gun.cap`
     after the hip->secondary switch (never 41/11); `partner_reload=shadow; reason=slot; ...; m3e8=<= cap` at the
     first mirror write after each switch.
   - `aim-switch` followed by an immediate R (if added): `partner_reload=queued_weapon`, then `partner_reload=replay`
     and `entered` instead of `fallback; ...; reason=weapon`.
   - adapt run: after each aim-reload `partner_reload=rate; setting=adapt; verdict=learned; body_ms=..; remote_ms=..;
     ratio_milli=..; k_milli=1000,<k>` and at the exit `partner_reload=rate_applied; ...; writes>=1; last=none;
     native_milli=<~1000>; written_milli=<native*k>`; over the burst reloads `len_ms` approaches `remote_len_ms`
     (ratio_milli toward 1000). A `verdict=latched` line means the later-segment write has no effect: report it (the
     enter's first segment carries the whole reload for that weapon).
3. **Auto-fire stop**: the `firehold` section of `partner-native-weapons.txt` with the shooter emptying its magazine
   (or `auto-reload`): receiver `partner_fire=dry_stop; remote_mag=0; ...; reason=remote_empty` within ~50 ms of the
   shooter's last round, `fire.suppressed` stops growing while the shooter holds the trigger on an empty magazine, then
   `partner_fire=dry_clear; ...; reason=reload` at its reload.
4. **Aim intent**: any aim section (`aim`, `aim-switch`): receiver `partner_aim_intent=on; source=pad|flag; refusal=none`,
   the line's `added_flag` / `added_act` counters grow only while the shooter's v5 Aiming is set (or its act is an aim
   act) without its HOLD bit (the end of an aim, a toggle-aim shooter); body 1's `gun.bit18` follows the shooter's aim
   and drops shortly after the shooter's flag and act clear (a held HOLD is never left behind: check that `gun.bit18`
   is 0 in the capture after every aim section). A manual two-PC check with toggle aim on the remote is the decisive
   test (the 10:35 case).
5. **partner_native_gun**: in any long run the change lines continue past 64 (`changes=` counts), no
   `partner_native_gun=capped; limit=64`.

## 11. Files

- Policy (pure, `mgs_coop_core`): `native/partner_slots.{hpp,cpp}` (unsupported, aim gate/frame, dry stop),
  `native/partner_reload.{hpp,cpp}` (waiting state, rate pairing, RateLearn/RateTarget),
  `native/include/mgscoop/partner_native.hpp` (slot_changed, ShadowRewrite).
- Hook: `native/partner_input_hook.cpp` (aim intent in `RunWindow`, `MirrorRate`, the 3E8 rule in `MirrorMag`, stats),
  `native/include/mgscoop/partner_input.hpp` (Arm fields), `partner_input_hook.hpp` (NativeStats, RateRefusal).
- Census: `native/partner_fire_filter.{hpp,cpp}`.
- Driver: `native/partner_driver.cpp` (weapon gate, dry stop, reload wait/rate lines, BuildArm, TickAimIntent,
  TickWeaponCtx, TickFireCensus, partner_native_gun), `partner_driver.hpp` (Mission/Settings fields, cadences).
- Settings: `native/lab_config.cpp`, `lab_config.hpp`, `tools/lab/LabCommon.psm1`, the worker allow-lists.
- Lua: `lua/coop/debug_equipment.lua` (`support`, `slot`), `lua/coop/debug_menu.lua` (dispatch).
- Contract: `tools/generate_partner_contract.py`, `native/partner_player_contract.hpp`.
- Tests: `tests/partner_native_tests.cpp` (TestAnim*), `tests/partner_driver_tests.cpp` (TestAnimRound1Weapons),
  `tests/partner_input_hook_tests.cpp` (TestFireCensus), `tests/lab_config_tests.cpp`, `tests/debug_menu_lua_check.lua`,
  `tests/worker_session_tests.ps1`.
- Scenario: `tools/worker/scenarios/partner-fire-census.txt`.

## 12. Open items

- The holster (section 3) and native throws/places/arm (section 7.3) are Round 2.
- The reload rate's effect is inferred (later segments only); the census-free hidden test decides whether `adapt`
  becomes the gun-profile default.
- The stun arm's effect path and the explosion's damage/noise owners are not located.
- Binoculars vs. the act-based aim intent (critic T8).
- Coordination: nothing here touches the camera agent's area. The census slots are global vtable swaps (forwarders)
  only with the lab key.
