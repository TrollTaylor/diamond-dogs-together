# Vehicles and companions: design

Status: design only, nothing built. Revision 2 (2026-10-28, after review; see section 11). Branch `wp/companions` from
`dev/oct7-continue` `48ce7da` (0.14.3; package built at `a4c18fa`). Target exe: `mgsvtpp.exe` 1.0.15.4, SHA-256
`085c2f82...bb45`.

Inputs: five read-only research passes of 2026-10-28 (roadmap audit, prior experiments, vehicles, buddies, tandem
riding) and one review pass. Their extracted game files and notes are in the session scratch folder
`.../scratchpad/companions/{vehicles,buddies,tandem,critic}/` (not in the repo). REA ledger unknowns they recorded:
`unk_59943e57` (relief selection var), `unk_bfa846ef` (unnamed TppVehicle2 commands), `unk_633bc6fb` (call menu and
Quiet commands), `unk_945d974f` (mirror buddy side effects), `unk_149ed100` (horse rider capacity).

**Native reads without Ghidra.** The shared Ghidra server refused connections on 2026-10-28 in every pass, including
this revision (`127.0.0.1:8080`, WinError 10061) [O]. REA has no native provider on this PC [O]. Every native read in
this document was made with pefile + capstone on the exe file, read-only (the review's tool is
`.../scratchpad/companions/critic/tools/dis.py`). The static steps of E11 and E15 use the same method; they are not
blocked on Ghidra, only slower (no decompiler, no xref database: callers are found by scanning for `call rel32` and
RIP-relative references) [I].

Every claim carries a tag:
- **[O]** observed: code, data, a log or a run read by someone, with path:line, RVA, run id or file hash;
- **[I]** inferred from observed facts;
- **[U]** unknown, needs an experiment (section 8).

RVAs are absolute (image base `0x140000000`) unless written as a short offset like `0xA44740`, which is image
relative. Run ids are hidden-worker runs recorded in `docs/STATUS.md`. Log lines written by world Lua appear in the
game log as `world_lua=event=...; key=value; ...` with `; ` separators (the observer reformats the `;` of the Lua
source; e.g. `tools/worker/scenarios/body-safety-sea-death.txt:62`) [O]; proofs below use that printed form.

## 1. Requirements

The user's request of 2026-10-28, restated as rules that a hidden run or a two-PC session can check. "Host" is the
player who hosts (always Snake); "guest" is the joiner; "copy" is one PC's game. Where the request can be read more
than one way, the rule states the recommended reading and section 10 asks the user.

| # | Rule | Pass signal |
|---|---|---|
| R1 | **Sortie vehicle on both screens, host's choice only.** A vehicle the host brings from sortie prep exists on both copies, same type and paint, at the same place. The guest's own sortie-prep vehicle never spawns. Only the host can bring or request a vehicle. Mission-end rewards for it are granted once. | Within 10 s of arrival, `GetPosition` of the host's relief vehicle answers on both copies within 1 m and 5 deg. When the partner drives it, the viewer logs `partner_ride_seat=confirmed; vehicle=25600`, never `reason=absent`. A guest request is refused with a toast. Its Fulton or destruction removes it on both copies. One mission-end seize, on the host. |
| R2 | **One of each companion.** Recommended reading (decision D5): at most one D-Dog, one D-Horse and one Quiet in the session, and it is the host's buddy; the guest's copy shows that same companion with the same look and no buddy of its own; the guest cannot deploy, call, order, dismiss or swap a buddy. The literal alternative (each player may bring a different companion) is D5 (b). | `buddy_status` on both copies names the same kind; the guest's copy never shows a second buddy; a guest attempt through the user's own path (iDroid, call menu, horse call) is refused with a toast. |
| R3 | **D-Walker, one per player** when the host deploys D-Walker (the user's "maybe"; decision D2 covers the other cases). | Two walkers on each screen; each player drives their own; each sees the other's walker move with its pilot. |
| R4 | **D-Dog synced, follows the host only.** One dog, visible on both screens, near the host's dog pose, following the host on both copies. What the dog does to enemies happens once, on the host's copy. "Completely" has two stages (section 6.2): before enemy positions sync in play, the guest sees the dog follow the host and sees the results of its attacks, but not the attack itself at the right guard, and not its marks; after enemy sync (and shared marking) those follow (F2b). | Guest-copy dog within 3 m of the host dog (5 m while running) after 10 s; no dog follows the guest; the mirror dog never engages on the guest's copy (no `mirror_engaged` line); the host dog's kills arrive as host outcomes. |
| R5 | **Quiet only when the guest is not playing her.** If the guest plays Quiet, buddy Quiet is not offered to the host (sortie prep and iDroid), is removed if already out, and the Mother Base cell Quiet is not shown beside the guest's Quiet. The guest's Quiet character always wins over a host buddy choice. Otherwise Quiet behaves as in the normal game on the host's copy and is mirrored on the guest's screen (position, cloak, the effects of her shots). | Guest character `quiet` (or `story` resolving to Quiet): the host's buddy menu frame shows no Quiet entry (or, if E4 shows the menu ignores the mask, a refusal toast); no buddy Quiet on either copy; the host logs the lock. Otherwise Quiet visible on both copies within 5 m, and the mirror never fires. |
| R6 | **D-Horse for two.** One horse, visible on both screens. Either player can mount it; the first one steers. A second player who mounts sits behind and can aim and shoot while the other steers; reload, weapon switch and binoculars as far as the stock horse rider allows (E16); throws and marking are later items. Both screens show both riders on one horse. | Frames on both copies show one horse with two riders in both role orders; the passenger's shots register (outcome sync); the passenger never steers. |
| R7 | **All vehicles synced** (scope in section 1.1). Every TppVehicle2 in the scene (authored jeeps and trucks, Mother Base jeeps, relief and reinforcement vehicles) has one state: whether it exists, where it is when no player and no AI drives it, its damage (body life, wheels, burning), and whether it is destroyed or Fultoned. A vehicle a player drives follows that player's copy. | After any loss, damage, push or drive, the other copy matches within 1 m / 5 deg after 2 s; a vehicle removed on one copy is removed on both (including index 0, the relief vehicle). |
| R8 | **Passengers wherever it makes sense:** light vehicle front and back seats, the truck cab and the truck bed, inside APCs and tanks (hidden crew), the shared D-Horse, and the support helicopter (M12, research first). Passengers can shoot where the game's poses allow it (light vehicle back seat, horse). | Each seat boards from either role, shows on both screens, and exits with the stock get-off. |
| R9 | **Host-only change.** Anything that creates, removes or swaps a companion, a sortie vehicle or a support vehicle is the host's action. A guest attempt is refused visibly, never left to diverge. | Every guest attempt logs a refusal and shows a toast. |

### 1.1 Scope of "all vehicles" and "passengers"

The user asked for "ALL VEHICLES" and "passengers in any vehicles that would make sense". This plan covers the rows
marked *in*; the others need the user's word (decision D6) because they depend on work outside this plan.

| Item | This plan | Where | Depends on |
|---|---|---|---|
| Authored jeeps and trucks: driven, parked, pushed, lost | in | M4 | E5 |
| Vehicle damage short of loss (body life, wheels, burning) | in | M4 | read API [U], E5 |
| Mother Base jeeps | in | M4 | - |
| Sortie (relief) vehicle: light vehicle, truck | in | M3 | E2, E17, E18 |
| Sortie (relief) vehicle: APC, tank, other faction's jeep | in via route A only | M3 | E2 |
| In-mission vehicle drop (iDroid "relief" request) | guest refusal in; host drop on both copies is research (route C) | M3 / D6 | E4 |
| AI-driven trucks, LRRP and convoys (`RegisterConvoy`) | in, after enemy sync in play | M8 | E10, enemy sync |
| Reinforcement WAV and tanks | in, after enemy sync in play | M8 | E10, enemy sync |
| Side-op armored vehicles and tanks (`quest_defeat_armor_vehicle` q52010..q52140, `quest_defeat_tunk` q52015..q52145, `TppQuest.lua:10` [O]) | in, after enemy sync and side-op sync | M8 | roadmap "Side ops" |
| Enemy helicopters, enemy walker gears | out (roadmap section 1) | - | D6 |
| Support helicopter: both players ride it visibly | research milestone | M12 | E22 |
| Driver-fired vehicle weapons (tank cannon and HMG, APC guns), mounted gun emplacements, heli door gun | later milestone | M13 | D6 |
| Seats: light vehicle front and back, truck cab, truck bed | in | M6 | E6, E7 |
| Seats: APC and tank crew (passenger hidden inside) | in (stretch) | M6 | - |
| Passenger shooting: light vehicle back seats, horse pillion | in | M7, M11 | E8, E13 |
| Passenger shooting: truck bed | stretch | M7 | E7 |

## 2. What exists today

| Area | State | Evidence |
|---|---|---|
| Partner drives a world vehicle | Body 1 is drawn by a DirectMotion GetOn/Loop/GetOff chain anchored to the viewer's copy of the vehicle (`CNP_ppos_c`, seat matrix `0xB7AF90`). That copy is placed every frame by RIDE_FAST (op 44, 20 Hz, 100 ms behind the sender tick) plus RIDE_TILT (op 45). Named seat, or by id for unnamed vehicles. The mod never writes the vehicle's `rider[]`, so on the viewer's copy a vehicle the partner drives answers `GetRiderId` seat 0 as empty. | [O] `docs/anim-round1-seat.md:23-87`, `docs/vehicle-ws3.md:88-171`, `lua/coop/world_ride_pose.lua:1-26`, `native/include/mgscoop/partner_board_policy.hpp:16-24`; runs `abb0b7cf` (by id, MB jeep 25603), `0a2314f2` (tilt) |
| Jeep back seat | `partner_ride_board=passenger` (default in lab and play) retargets stock ride sites `0x12458AC` (pre-query) and `0x1251479` (phase 1) to the free back seats 2..3 of a light vehicle (type 1/2, 3-4 seats), never the peer's seat. The stock passenger phase `0x1250CF0` (act 0x6E) attaches the viewer to the copy RIDE_FAST moves; a passenger camera handle write fixes the view. | [O] `partner_board_policy.hpp:10-50`; `docs/native-puppet.md:270-517`; runs `59cfc415`, `495e7c3a` (lab), `de949f80` (play) |
| Trucks, APCs, tanks as passenger | Blocked by policy: passenger mode searches only light vehicles (types 1/2) and falls back to block for every other type. The block fallback is proven on a truck: in `84e3db0e` the guest's copy of the host's truck was placed by RIDE_FAST (`goto_partner_vehicle` answered `fast=1`) and its E logged the expected `board_block; ... reason=type; type=4; seats=2`. | [O] `partner_board_policy.hpp:18-22`; `docs/STATUS.md:949-957` (run `84e3db0e`: `veh_trc_0000` = 25602, type 4, seats=2) |
| Sortie (relief) vehicle | Exists on the chooser's copy only. 0.14.3 keeps the driver visible on the other copy (`reason=absent`, standing at seat height with no vehicle). | [O] `docs/two-pc-test-2026-10-23.md:94-101`; `native/partner_driver_seat.inc` (`SeatFallbackHere`); run `31adc898` |
| Vehicle removal | VEHICLE_GONE / REQ_VEHICLE_GONE (ops 46/47) make Fulton and destruction host-authoritative and never remove a ridden vehicle. The receiver matches by locator, type and position (nearest of its type), never by id alone, and refuses index 0 (`index0`). Off by default (`world_vehicle_gone=0`, the launcher never writes it). Parts A and D proven (`075c1ea4`); part C (hold while seated) and a manual Fulton never ran. | [O] `lua/coop/world_vehicle.lua:1-32, 148-157, 183-199`; `native/include/mgscoop/lab_config.hpp:700-704`; `docs/STATUS.md:592-611` |
| Parked and AI-driven vehicles | Not synced. Each copy places and drives its own trucks (one was AI-driven 490 m away in `a42ddafe`); a wreck existed on one copy only (`7561ab63`). | [O] `docs/world-sync-protocol.md:1174-1176`; `docs/STATUS.md:920-921, 954-958` |
| D-Dog | The host sends its dog pose every 250 ms (BUDDY_STATE op 39). The guest borrows its **own** D-Dog and holds it there (`LuaAiStayAndSnarl` every 250 ms, `SetWarpPosition` beyond 15 m). Needs world level 3. Only proof: lab run `7169753d` (guest dog 1.48 m from the host dog). | [O] `lua/coop/world_buddy.lua:235-297`; `docs/STATUS.md:2208` |
| D-Dog gaps | The guest's copy shows no dog when the guest deployed horse, walker or Quiet (`guest_buddy_busy`, `:260-263`) or no buddy (the guard needs `IsBlockActive()`, `:61-67`, which is false with no buddy deployed); the host never sees a dog only the guest deployed (the guest releases it to its own AI, `:254-257`). Both roles' own-dog reads go through the receipt `MGS_COOP_BUDDY_POSE_V1`, whose `field_ready` needs `pp::Realized(player,1)` and `parts[1]==3`: the read refuses on either copy while body 1 is unrealized there (heli, mount hide, ride hide). Mother Base sessions of 2026-10-23 logged no buddy line at all. | [O] code lines cited; `native/game_script_observer.cpp:4015-4018`; `docs/STATUS.md:1996`; 2026-10-23 host log (refused `scope` for ~50 s in 30020) and friend log (no buddy line in 30050). Which guard failed in MB is [U] (a failed guard logs nothing). |
| Mirror attribution | On the guest's copy an attacker of class `buddy` (any D-Dog or Quiet) or `local` (the local player, **any** horse, **any** player walker: `Tpp.IsHorse` / `Tpp.IsPlayerWalkerGear` are class checks) counts as the guest's own and is forwarded as REQ_OUTCOME. A local state change that is not attributed is only counted and logged (`event=divergence; why=not_attributed`); nothing reverts it. | [O] `lua/coop/world_outcome.lua:27-38`, `lua/coop/world_phase.lua:27-29`, `Tpp.lua:311,313` (extracted), `lua/coop/world_enemy.lua:63, 296-305` |
| Sortie choices | World follow (op 9-11 seeds plus the kind-6 replay) carries mission, heli route, layout, cluster and deploy time. No buddy and no vehicle key. The guest never deploys itself: its `OnEndMissionPreparation` sends a request the host rejects (`host_only`). | [O] `lua/coop/world_follow_seed.lua:4-15`; `lua/coop/world_follow.lua:303-307, 412-413` |
| Guest checkpoints | When the host checkpoints, the guest runs its **own** stored `TppMission.UpdateCheckPoint` original (CHECKPOINT op, `atCurrentPosition` or the named checkpoint). The guest's co-op save stores the guest copy's own vars; no host var is copied. | [O] `lua/coop/world_recovery.lua:60-100` |
| Partner on D-Horse / D-Walker | Hidden and unrealized on the viewer's screen (acts 121/122, 123-126). RIDE_BODY (op 42) never moves the viewer's own horse or walker (user decision 2026-10-22); a pose applies only to an id in `R.mirrors`, and nothing registers one. Probe run `251abcef`: DM `stateRideOnIdle` / `stateGallop` put body 1 into a seated / hunched rider pose with nothing under it (`code_match=0` although the pose changed). | [O] `docs/mounts.md:21-25, 52-85, 329`; `lua/coop/world_ride_pose.lua:33-40`; runs `bfd6fcc4`, `2ab88411` (lab), `5a044a4d` (play) |
| Two riders on one horse | Never worked. (1) `world_horse_cargo.lua` (op 43, lab `cargo_test 1`) warps the guest's own player onto an offset of the host's horse each tick (0.55 m back, 1.55 m up), stance SQUAT, pad mask `buttons=0xFFFFFFA7` (every button bit except 0x58 masked); its native side is the `MGS_COOP_CARGO_V2` receipt, which calls `coop_carry::SetCargoHold/SetCargoLocal/RequestLocalPutDown`; no recorded run result. (2) Native Player cargo on D-Horse stopped at the stock horse-load finder (`cda340da`, `2d94f364`). (3) The capacity-two archive gives two horses per copy, but each player mounted only its own (`ed3c1cd4`); the peer riders in `f6a2b54c` were a display bug. | [O] `lua/coop/world_horse_cargo.lua:1-12, 113-122, 315`; `native/game_script_observer.cpp:4459-4470`; `docs/mounts.md:132-137`; `docs/STATUS.md:2224-2253`; tag `archive/research-2026-10-23` evidence |
| Capacity two | Lab-only archive `mgs-coop-buddy-capacity-variants-v2.dat` (17,501,184 bytes, SHA-256 `4b290cdd...5ec9`): seven horse FPKDs and `buddy_wg2_00` re-authored to count 2 with a second locator 4 m away; `MGS_LOCAL_BUDDY_CAPACITY_TEST`, profile `buddy_capacity_two_v1`. Two realized horses and two walkers per copy; the guest's ACTION mounted no walker (`f145f79f`, `6e789152`). Memory budget never measured. | [O] `docs/STATUS.md:2216-2256`; `lua/coop/world_buddy_ride.lua:183-197` |
| Different buddy types per copy | Each copy can hold a different buddy type locally: run `87a5ce1e` had the guest summon D-Horse (F9) while the host kept D-Dog. Neither copy showed the other's companion. | [O] tag `archive/research-2026-10-23` `docs/evidence/horse-recall-actual-87a-2026-10-11.json` |
| Quiet | The guest's Quiet character falls back to a DD soldier when the guest's **own** `vars.buddyType` is Quiet at Gather, or in 10050/11050/10260. Nothing stops the host deploying Quiet while the guest plays her. No Quiet sync exists; the guest treats host kind 4 as `no_host_dog`. Mother Base has a separate cell Quiet (`TppMbQuiet`, `f30050_sequence.lua:2004-2009`, gated by `TppStory.CanArrivalQuietInMB`). | [O] `lua/coop/character.lua:79-86, 121-124`; `docs/characters.md` table; `world_buddy.lua:254-256`; extracted f30050 script |
| F9 summon | Release F9 "Summon D-Horse": with no buddy loaded, stock `ReserveCallBuddy` + `Load` and a `vars.buddyType` write (`deployHorse`); with a block active, `W.api.buddyCall` (CallBuddy bound at hook time) and no var write. Creates a horse on one copy only; the 2026-10-23 host horse was invisible to the guest. | [O] `lua/coop/debug_buddy.lua:43-84, 200-215`; `world_buddy.lua:37`; `docs/two-pc-test-2026-10-23.md:16, 109` |
| Seated aim | The 2026-10-22 two-PC session found that aiming while seated, including as a back-seat passenger, produced aim samples that latched the aim self-check (diag #7). Run `637b47ea` (lab) had the back-seat guest hold aim: `aim_dir=refused; reason=ride` while seated, frame label `aim-seated`. So a stock passenger enters aim; whether the gun raises visibly and fires is [U]. A seat-aim census is an uncommitted tree in `D:/mgs-coop-wt/seat-aim` on 0.13.5 `cc973a8`. | [O] `docs/STATUS.md:785-789, 850-852`; `tools/worker/scenarios/coop-vehicle-seated-aim.txt:1-20`; `git status` of the seat-aim tree |

## 3. Game mechanics

### 3.1 The buddy block (one buddy per copy)

- **Enums** [O] (`0x140A46DC0`): BuddyType NONE 0, HORSE 1, DOG 2, QUIET 3, WALKER_GEAR 4, BATTLE_GEAR 5, DISABLE 7;
  BuddyInitStatus NORMAL 0, HALO 1, RIDE 2, ENTRY_GATE 3, ESPIONAGE 4.
- **Vars** [O] (declaration code from `0x14099EA60`; buddies notes `vars_buddy_offsets.txt`): `buddyType` vars+0x2D68,
  `sortieBuddyType` mask +0x2D6A, `disableCallBuddyType` +0x2D6B, `deadBuddyType` +0x2D6C, `initialBuddyStatus`
  +0x2D6D, `initialBuddyPos` +0x2D70, `initialBuddyRotY` +0x2D7C, `buddyCommonFlag` +0x2D80, `buddyCallCount`
  +0x2D88, `buddyFriendlyPoint` +0x2DE4; look vars `buddyQuietEquipType` +0x2E44, `buddyQuietCostumeType` +0x2E46,
  `buddyDogFlag` +0x2E47, `buddyDogMode` +0x2E48, `buddyDogEquipType` +0x2E49, `buddyDogEyeType` +0x2E4A,
  `buddyDogColorType` +0x2E4B, `buddyGear*` +0x2E4C..+0x2E60 (ammo, grades, head, arm, weapons, coloring),
  `buddyHorseType` +0x2E61, `buddyHorseColorType` +0x2E62, `buddyHorseHeadDesignType` +0x2E63,
  `buddyHorseFootDesignType` +0x2E64. Neighbours: `reliefVehicleCarryAmmo` +0x2D60, `reliefVehicleCarryState`
  +0x2D66. These are saved vars.
- **One controller** [O]: singleton at `0x142C28930`, requested type +0x174, active type +0x175 (what
  `GetActiveBuddyType` returns), six fixed blocks (main, `_Cmn`, `_Sub0..3`). `CreateBlock` / `DeleteBlock` are the
  shared no-op stub `0x141C658E0`. Every retail buddy FOX2 declares one GameObject with totalCount 1. So a copy holds
  one buddy type at a time [I, strong].
- **Load** [O]: `TppMain.OnAllocate` runs `TppMission.SetSortieBuddy()` (masks only), then, outside 10260,
  `TppMission.ResetQuietEquipIfUndevelop()` (`TppMain.lua:102-103`; resets `buddyQuietEquipType` 4 when its equipment
  is not developed, `TppMission.lua:2336`), the mission's `DISABLE_BUDDY_TYPE`, `sequence.OnBuddyBlockLoad()`, then
  `TppBuddy2BlockController.Load()` **only in Afghanistan and Africa** (`TppMain.lua:102-122`). The Load core
  `0x140A485F0` copies vars `buddyType`, `initialBuddyStatus`, `initialBuddyPos`, `initialBuddyRotY` into the
  controller and checks no unlock mask.
- **Mother Base** [O]: f30050 disables HORSE, WALKER_GEAR, BATTLE_GEAR; `TppMbFreeDemo` adds
  `/Assets/tpp/pack/mission2/free/f30050/f30050_Buddy.fpk` only when `vars.buddyType` is DOG or QUIET at pack-list
  time, then `ReserveCallBuddy(vars.buddyType, BuddyInitStatus.RIDE, 0, 0)` + `Load()` (`f30050_sequence.lua:139-143,
  295-308, 972-973`; `TppMbFreeDemo.lua:4, 276-277`). Separately, the cell Quiet `TppMbQuiet` index 0 lives on the
  medical platform (`f30050_sequence.lua:2004-2009`, `trap_toQuietPrison`, `StartWakeUp`) with the `f30050_mbQuiet`
  demos.
- **Mid-mission spawn** [O]: `ReserveCallBuddy(type, initStatus, pos, rotY)` then `Load()` works with no buddy
  deployed: the F9 horse summon does exactly that (`debug_buddy.lua:46-70`) and the 2026-10-23 host horse came from it.
  `CallBuddy(type, pos, rotY)` is called with NONE and DOG while the block is active (`world_buddy.lua:156, 172, 270`);
  that it also swaps a loaded horse, walker or Quiet for another type is [I]. Whether native CallBuddy writes
  `vars.buddyType` is [U] (the Lua `CallBuddy` path in `debug_buddy.lua` writes nothing).
- **Refusals** [O]: CallBuddy's core `0x140A44740` refuses when the block pointer (+0x68) is null, a pending byte is
  set, bytes +0x182..+0x186 are set, or bit `1<<type` is set in the TppBuddyService disable byte `0x142C2919E`
  (written by `SetDisableBuddyType` / `SetDisableAllBuddy`; Quiet in 10260 is the only exception). `DeleteBuddy`
  (`0xA45980` -> `0xA45850`) requests NONE through the same gates. `SetDisableCallBuddyType` writes the saved var +0x2D6B
  instead. **The same byte that blocks a player's call also blocks our own `CallBuddy`** [O, readers `0xA447D3`,
  `0xA458AC`].
- **Orders and calls** [U]: the player's buddy orders (dog attack/stay, Quiet scout/cover/fire, horse, walker modes)
  go through the native call menu (`CallMenuImpl::UpdateSelectKeyboard` `0x179CE60`), not Lua [O for the function, U
  for the full path]. The horse call (whistle) path is [U]. Whether the iDroid buddy deploy and the sortie-prep UI
  honour the disable byte is [U] (REA `unk_633bc6fb`). E4 answers all of these on the guest's copy.

### 3.2 Per-companion commands

| Companion | GameObject type, id on each copy | Lua commands | Placement |
|---|---|---|---|
| D-Dog `TppBuddyDog2` | 19, `9728` | Dispatcher `0x140C7D220` [O]: `LuaAiStayAndSnarl` (`0x24824B9A`), `GetPlant`, `SetWarpPosition` (`pos`), `GetPlacedItem`; scripts also send `SetMotherBaseCenterAndRadius` | Held by `LuaAiStayAndSnarl` target + `SetWarpPosition` [O, run `7169753d`]. Whether the held dog still attacks a guard near it is [U] (E3) |
| D-Horse `TppHorse2` | 13, `6656` (`6657` in capacity two) | Dispatcher `0x140E4D250` [O]: `GetRidePlayer`, `SetCallHorse{startPosition, goalPosition}`, `HorseForceStop`, `SetCombatGrade`, `SetMaxLife`, four unnamed hashes `0x9BBA9494`, `0x442D1EEC`, `0xC41E0C02`, `0xD1233CDA` | **No verified per-frame placement** [O]. `SetCallHorse` at RIDE_BODY rates made idle horses leap and hang in the air (`docs/mounts.md:52-62`, filmed) |
| D-Walker `TppWalkerGear2` | 44, `22528` (`22529`) | Dispatcher `0x141706190` [O], 16 commands incl. `SetPosition{pos, rotY}`, `NoRideOn`, `IsPlayerRiding`, `ForceStop` | `SetPosition` [O]; it does not arm a body for ACTION (`6e789152`) |
| Quiet `TppBuddyQuiet2` | 18 [I, type array `0x2A7C690`] | Dispatcher not found [U]; type handle `0x142C36A38` [O]. Controller requests exist: `RequestMoveToPosition`, `RequestWalkToPosition`, `RequestMoveToSnipePoint`, `RequestEspionageToPosition` [O, table `0x140A45BE0`]; argument shapes [U] | [U] |

The native buddy AI serves its copy's local player; there is no Lua follow-target setter [I]. A mirror left to its own
AI follows the guest, which is why `world_buddy.lua` holds the guest's dog [O].

### 3.3 Riding

- **One rider per horse** [O]: mount handler `0xE59C20` (505 bytes, SHA-256 `9e35a6fc...`) accepts event
  `0x5167C60042B5`, stores one rider at horse record +0xE and the occupied byte at +0x2DD; `GetRidePlayer` returns the
  occupied flag (`docs/STATUS.md:2253`). A second rider has no native slot [I].
- **Rider acts and states** [O]: act 121 ride, 122 get on/off and turns; statuses ON_HORSE 25, HORSE_IDLE..GALLOP 44-47
  (PlayerInfo `0x1412CE870`); rider states `stateRideOnIdle` `f241f3bcf60b`, `stateTrot`, `stateCanter`, `stateGallop`
  in `TppPlayer2_layers.mog` (`docs/mounts.md` sections 3 and 5). The DM anchor `0x1198770` writes a seat matrix for
  GameObject types 50 and 4 only, not 13 or 44 (section 4).
- **The rider DM plays on body 1** [O, `251abcef`, `docs/mounts.md:329`]: `stateRideOnIdle` and `stateGallop` put body 1
  into a seated / hunched rider pose at ground level with nothing under it. This lowers E12's risk: the pose exists on
  body 1; what is missing is the anchor (a horse under it).
- **Rider geometry** [U as a TppHorse2 offset]: three sources disagree and use different horse classes and frames.
  (a) A single rider at (0, 1.40, 0.40) m from the root in the p31_010010/020 demo end transforms; (b) the prologue
  pillion in `ocelot_horse`'s frame (`s10010_d11.fpkd` demos p21_020030_000, p21_020040_001): Snake at about
  (-0.01..-0.08, 1.11, -0.05..-0.18), Ocelot at (-0.02, 1.06-1.08, 0.33-0.35); (c) the cargo prototype's guess, 0.55 m
  back and 1.55 m up (`world_horse_cargo.lua:12`) [O for each number]. The 0.3 m height gap between (a) and (b)
  suggests a different root convention [I]. The rear-rider offset on D-Horse is measured in E12 (the driver's census
  pose against its own horse root), not taken from these.
- **Horse connection points** [O]: `hrs0_main0_def.fcnp` (identical in `buddy_horse2_00` and `s10010_s07`) has reins
  `CNP_BHANDL/R`, `CNP_EJECT`, `CNP_MAGNET` and limbs; no rear-seat CNP. The rear rider is placed by animation [I].
- **D-Walker** [O]: acts 123 (get on), 125 (drive, `RideWalkerGearActionImpl::StateDrive` `0x141260CC0`), 126 (leave);
  no ON_* status, so a walker rider reports `ride=0`; drive node `0x23B9A991BFB` has no name for the Lua DM binding.

### 3.4 The prologue pillion (the user's reference)

- The ride the user remembers is the end of the prologue s10010, `Seq_Game_VolginRide` [O,
  `s10010_sequence.lua:922-923, 11792, 11867`]: Snake sits behind Ocelot on **one** horse, Ocelot steers along an
  authored route `rts_ocelotHorce` (`:16222`), and a timer hands Snake a gun (`RequestHandsGunToPlayer` to
  `{type=TppPlayerHorse2forVr}`, `EQP_WP_Volgin_sg_010`, `:2022-2023`).
- The Episode 1 (s10020) opening ride with Ocelot is a cutscene with **two** horses side by side (p31_010010 /
  p31_010020, `s10020_demo.lua:8-31`) [O].
- Not reusable as is [I]: it needs mission-only classes (`TppHorse2forVr` / `TppPlayerHorse2forVr`, block
  `s10010_s07`), the global player SequenceWork 2 (`cypr_player_volgin_ride.lua`, provider `0x2C1BF20`, no per-body
  argument), an NPC driver and an authored route; the archived verdict is `portable_to_D_Horse=false` [O, tag
  `archive/research-2026-10-23` `riding-research-proof.json`]. `RequestHandsGunToPlayer` is matched only in the
  `TppHorse2forVr` dispatcher `0x141807AC0` [O].
- Reusable [O/I]: the same horse model and rig; `player2_cypr.mtar` (184 clips, 7 referenced by the resident graph,
  clip names unknown) as a source of rear-seat clips if ever needed; the player graph has **no** named pillion state
  (bounded search, tandem `gapsolve2.py`). The seat offsets are a hint only (3.3).
- Vanilla D-Horse riders can draw and fire weapons [I, gameplay knowledge; weapon-class limits U, E16].

### 3.5 Relief (sortie) vehicles

- The sortie-prep vehicle is the retail "relief vehicle": **always TppVehicle2 index 0, id 25600** [O,
  `TppMission.lua:1549-1583` uses `CreateGameObjectId("TppVehicle2", 0)`].
- A native `Vehicle ReliefBlock` loads one of ten packs `/Assets/tpp/pack/vehicle/veh_rl_{west,east}_{lv,trc,wav*,tnk}`
  chosen by byte +0x2B of the request record at `[this+0x58]` (selector `0xB39385..0xB393B4`, path table built by
  `0xB3A1E0`) [O]. That the record is the sortie request is [I]. The packs include a parachute, and the UI has
  `CMD_REQUEST_RELIEF` (`0x824460`), so the same system probably serves the in-mission vehicle drop [I].
- Placement [O]: `TppBuddyService.AdjustFromDropPoint(gvars.heli_missionStartRoute, EntryBuddyType.VEHICLE, 6, 0)`
  near the heli drop point (`TppHelicopter.lua:126-127`), overridden per assault LZ (`TppLandingZone.lua:96-97`).
  `Vehicle.SaveCarry` / `LoadCarry` carry it across missions (`TppMission.lua:1593-1613`, `TppVarInit.lua:123-124`).
- **Mission-end seize** [O, `TppMission.lua:1549-1583`]: `SeizeReliefVehicleOnAbort` (with save: `TppTerminal.OnFulton`
  when the player has Fulton Cargo 2 or Wormhole; otherwise `Seize{Fulton, CheckFultonType, DirectAccount}`),
  `SeizeReliefVehicleOnClear` (`Seize{Fulton, CheckFultonType[, CheckFarFromPlayer]}` unless the player is in it) and
  `SeizeReliefVehicleOnForceGoToMb` (`Seize{..., DirectAccount}`) all act on index 0. If both copies hold index 0,
  both would credit it to Mother Base resources [I]: a double reward unless the guest's seize is suppressed (E18).
  Whether the sortie's GMP cost is also charged on the guest's replay is [U] (E18).
- **The var that holds the chosen relief type is not identified** [U, `unk_59943e57`]. Candidates: the
  `sortieLoadoutInfoU8buf/U16buf/U32buf` buffers, `userPresetVehiclePaintType`, `reliefVehicleCarry*`.
- **Index 0 cannot be spawned, respawned or despawned from Lua** [O]: `ParseLuaSpawnIndex` `0x140B304E0` rejects 0 and
  indices at or above the pool count (callers in Spawn `0xB31E70`, Respawn `0xB31710`, Despawn `0xB30AF0`), and
  `world_vehicle.lua` refuses `index0` (`:148-157`). So a Fulton or destruction of index 0 on one copy cannot be
  mirrored on the other with today's receiver (K19, E17).
- **Only loaded bodies can be drawn** [O]: free roam loads the location faction's light vehicle and truck bodies only
  (30010 east, 30020 west, 30050 west LV); a relief pack adds one instance of its class.
- `Spawn{index>=1, type, subType, paintType, class, position, rotY, ...}` exists (parameter parser `0xB2F960`) [O];
  whether it works in free roam with 3 bodies already realized is [U].

### 3.6 World vehicles

- Pools [O]: 30010 `instanceCount` 22 (2+6+13+1), `veh_lv_0000..0012` (EASTERN_LIGHT_VEHICLE) and `veh_trc_0000..0005`
  (EASTERN_TRUCK); 30020 24 (2+10+11+1), `veh_lv_0000..0010` and `veh_trc_0000..0009` (WESTERN_TRUCK item box,
  container, cistern); spawned at `OnInitialize` by `TppEnemy.SpawnVehicles` (`f30010_enemy.lua:34-116`,
  `f30020_enemy.lua:37-148`). Mother Base spawns up to 4 WESTERN_LIGHT_VEHICLE at `veh_clXX_clYY_0000` by cluster and
  grade (`f30050_enemy.lua:52-77, 530-603`); that these are the project's "unnamed" 25600..25603 is [I].
- **Ids are not assigned in list order** [O]: in 30020 `veh_lv_0000` = 25608 (index 8) and `veh_trc_0000` = 25602
  (index 2), although the light vehicles come first in the spawn list (`docs/STATUS.md:76, 949-951`). Equal ids on both
  copies have been seen in every run so far [O, e.g. 25608 on both copies in `31adc898`], but nothing guarantees it
  [I]. The existing receiver therefore trusts an id only as the nearest vehicle of its type to the reported position
  (`world_vehicle.lua:183-199`) [O]; VEHICLE_STATE does the same (section 5).
- Enemy vehicles have no route of their own; soldiers drive them (`SetLrrp{travelPlan}`, `SetRelativeVehicle`,
  `TppEnemy.lua:1500-1508`) [O].
- Reinforcements [O]: script block `reinforce_block`, types EAST/WEST WAV, WAV_ROCKET, WAV_CANNON, TANK, HELI; CP messages
  `RequestLoadReinforce`, `RequestAppearReinforce`, `CancelReinforce`; the vehicle appears by
  `Respawn{name='reinforce_vehicle_0000', type=9, ...}` with driver `reinforce_soldier_driver`
  (`TppReinforceBlock.lua:46-200`). The only random input is `TppRevenge.SelectReinforceType` [O], so a host event can
  drive the same steps on the guest [I].
- Side ops with armor [O]: `quest_defeat_armor_vehicle` (q52010..q52140, step 10) and `quest_defeat_tunk`
  (q52015..q52145) (`TppQuest.lua:10`, extracted).
- Instance commands [O] (`0xB2B710..0xB2C1A7`): `GetPosition`, `SetPosition{position, rotY}`, `GetVehicleType`,
  `GetRiderId`, `Seize` (Fulton, ...), `ForceStop{enabled}`, `IsAlive`, `SetIgnoreDisableNpc`; dictionary matches
  [I]: `SetVehicleType`, `SetBodyLife`, `SetBodyDamageRate`, `SetWheelDamageRate`, `GetState`. Class commands: `Spawn`,
  `Respawn`, `Despawn`, `IsAlive`, `StoreToSVars`, `RestoreFromSVars`, `RegisterConvoy`. A read of body life or wheel
  damage is [U].
- **M4 read of the dispatcher (capstone, 2026-10-28)** [O]: the instance dispatcher `0x140B2B710` compares exactly 21
  command hashes (plus the `Seize` options); `SetBodyLife` (`0x3BA5A795`, handler `0xB2BC69`, key `life`) writes a u16 through the object's vt+0x98 =
  `0xB2DD00` (`*(object+0x150) + index*0x18`, 12 u16), `SetBodyDamageRate` (`0x198238FA`, key `rate`) and
  `SetWheelDamageRate` (`0xB8BD1B91`) write per-index float rates, `GetState` (`0x8770E528`) answers two bits
  (`V+8[index*32+0x1E] & 0x80`, rec+0x70 & 0x18000), and none of the 21 reads a life [O]. `StoreToSVars` (class
  `0xB99AB5A1` -> `0xB35BC0`) copies all 12 words into svars `ene_veh_life` and encodes `ene_veh_state` from rec+0x70
  (bits 3, 9..14 and `0xC0000`) and rec+0x7C bit 14 [O]; Spawn/Respawn take `bodyLife` / `wheelLife` (`0xB2FFA6`,
  `0xB325CE`) [O]. So word 0 is the body life [O] and words 1..11 the wheel / part lives [I]. M4 reads the block
  natively (read-only receipt `MGS_COOP_VEHICLE_LIFE_V1`, docs/world-sync-protocol.md section 17); the seven other
  hashes are unnamed (`0x558A8535` writes a byte at `[obj+0x160][idx]`, `0x29AC74C7` / `0xEA883EE6` set / clear bits at
  `[obj+0x180][idx]`, `0x26A17EF3` / `0xF13DF67E` clear / set rec+0x7C bit 13, `0x84692F8D` `speed` -> V vt+0x78,
  `0xE51E5C9B` V vt+0x80 = `0x16F8240`, which clears a per-index move slot) [O]; no wheel or burning write exists [I].
  `VehicleBroken` (`0x82AAD646`) is posted only by one state-update function (immediates at `0x1416FA899`,
  `0x1416FAA7C`, `0x1416FAD5D`), never by `SetBodyLife` itself [O].
- Messages [O]: `VehicleBroken`, `VehicleAction` (GOT_IN, GOT_OUT, FELL_OFF), `PlacedIntoVehicle`, `VehicleDisappeared`.
- Checkpoints store all vehicle state in svars `ene_veh_*` (`TppEnemy.lua:1185-1193, 1321-1351`) [O].
- The exe has a "Vehicle Synchronizer" memory tag (presumably FOB/MGO) [O string `0x22889C8`]; project rules exclude
  the game's online sync.

### 3.7 Seats and passenger poses

| Vehicle | Seats with player states [O, MOG hashes] | Notes |
|---|---|---|
| Light vehicle | 0 driver, 1 front right, 2 back left, 3 back right: GetOn / Loop / GetOff each | Stock phase 1 has **two** sites that treat seats 0 and 1 alike [O, capstone]: `0x141251832..0x141251840` passes seat 0 for any seat below 2 (`and al,0xf; cmp al,2; jb -> mov eax,r15d`), and `0x14125188E..0x14125189B` selects phase 3 (driver) for any seat below 2 and phase 2 (passenger, `0x1250CF0`) only for seats 2 and up (`cmp al,2; setb al; add al,2; mov [rbx+0x55],al`). A stock player never takes seat 1 |
| Truck | 0 driver, 1 front right: GetOn/GetOff for both, `stateTruckFrontLeftLoop` only; bed hide `stateTruckHide`, `stateTruckHideLoop`, `stateTruckHideToWait` | `veh_trc_0000` reads `seats=2` (`84e3db0e`) [O]: the bed is not a seat index. The mod borrows `stateVehicleFrontRightLoop` for truck seat 1 (`partner_sync.lua:207-212`); seat 1 of an empty copy is remapped to the driver by `0xB79A3F` [O]. Var `vehicleGameObjectIdOnPlayerOnCargo` tracks a player on a vehicle's cargo [O]. Truck CNPs `CNP_ppos_a,b`, `CNP_CARG`, `CNP_DECK_A` [O] |
| APC (wav/sav) | none | CNP `ppos_a,b,c` [O]; driver only, driver hidden |
| Tank (nbt/mbt) | none | CNP `ppos_a,b`; 12.7 mm HMG on `CNP_awp_a` is the driver's second weapon (`VEHICLE_TOGGLE_WEAPON`) [O/I] |
| Support heli | `stateHeliIdle`, `stateHeliRideOn` | AI-flown; partner hidden in the heli (acts 97..108) [O] |

No retail player state combines a vehicle seat with aiming [O, bounded search]. A stock back-seat passenger does enter
aim: the 2026-10-22 diag #7 and run `637b47ea` (section 2) [O]. Static reading adds that it can raise and fire: Ride Exec
stage 0 sets no-weapon flag 0xC3 at `0x12473A2`, the passenger phase clears it at `0x1250FF8`, and EnableCheck
`0x103E010` refuses only on 0xC3/0xC2 [I]. Whether the gun shows and a shot lands is [U] (E8). Retail also seats the
player in an NPC-driven jeep's back seat by a scripted direct motion (`s10150_sequence.lua:1765-1800`,
`stateDirectPlayRideRearLeftSeatOnVehicle`) [O].

## 4. Authority model

One principle: **the host owns everything that exists in the world; whoever rides a thing owns its pose while riding;
every copy may refuse what would remove something under its own rider; a mirror never acts.**

| What | Owner | How the other copy follows | Guest may | Guest may not |
|---|---|---|---|---|
| Sortie buddy type and look | Host (sortie prep) | Follow seeds before the guest's `OnAllocate` (F1) | Pick its own character (wins over Quiet, F1.4) | Pick, call, dismiss or swap a buddy; any guest sortie-prep buddy choice is overwritten |
| Sortie (relief) vehicle choice | Host | Seeds (route A) or VEHICLE_SPAWN (route B) (F3) | Ride it, drive it | Choose or request one |
| In-mission buddy calls, vehicle drops, F9 summons | Host | COMPANION kind change / VEHICLE_SPAWN | - | Any; refused with a toast on the path E4 finds |
| Companion pose (dog, Quiet, unridden horse) | Host's copy | COMPANION stream, the guest copy holds its instance there | Walk with it | Move it, call or whistle it |
| D-Horse while ridden | The driver's copy | MOUNT_FAST from the driver | Drive it (if first on) or sit behind | Steer while passenger |
| Each D-Walker | Its pilot's copy | MOUNT_FAST | Drive its own | Mount the host's |
| Vehicle a player drives | That player's copy | RIDE_FAST / RIDE_TILT (existing) | Drive any free vehicle | Take a seat the peer holds |
| Vehicle nobody drives (parked, pushed) | Host's copy | VEHICLE_STATE (never for a vehicle with a player or NPC rider, F4) | Push it locally (corrected by the host) | - |
| AI-driven and reinforcement vehicles | Host's copy | RIDE_FAST from the host + local AI stopped (F8) | - | - |
| Vehicle damage, destruction and Fulton | Host decides | VEHICLE_STATE (damage) / VEHICLE_GONE (guest asks with REQ_VEHICLE_GONE) | Damage, destroy or Fulton on its copy (becomes a request) | Keep a vehicle the host removed |
| Seat occupancy | Host arbitrates | SEAT grant/deny | Claim a free seat | Keep a seat the host denied |
| Companion orders | Host | none needed (they run on the host's copy) | See decision D1 | Order the mirror on its own copy (refused, E4) |
| Companion health, death, hospital | Host's copy | COMPANION `state` | - | - |
| Combat effects of companions | Host's copy only | ENEMY_OUTCOME etc. (existing) | - | - |

Conflict rules:
1. **Terminal wins.** A removal (VEHICLE_GONE) or a companion death on the host beats any pose; ridden vehicles are
   never removed under a local rider (existing rule, `world_vehicle.lua:119-132`) [O]; the removal waits until the
   rider leaves (part C).
2. **Rider beats owner.** While a player rides a horse, walker or vehicle, that copy's pose stream wins on both copies;
   when the ride ends, the last pose is sent reliably (RIDE_BODY's final pose, existing [O]) and ownership returns to
   the host. For the shared horse the handoff back to local control follows decision D4 (no placement while the local
   player is within mount range).
3. **Seat races go to the host.** Both copies check seats locally first (the existing board policy never offers the
   peer's seat) [O]; if both claim the same seat within one round trip, the host's SEAT grant decides and the loser gets
   the stock get-off.
4. **Mirrors never engage.** A companion instance on a copy that mirrors the other player's companion (the dog, Quiet,
   an unridden or partner-ridden horse, the partner's walker) must cause no Damage, Dead, Sleep, Faint or Fulton
   message on that copy. This is a hold, not an attribution trick, because nothing repairs a split afterwards: an
   unattributed local kill on the guest is only logged (`world_enemy.lua:296-305`) [O], and a forwarded one
   (today's `buddy`/`local` classes, `world_enemy.lua:63`) becomes a phantom host-side kill whenever guard positions
   differ, which they do in play [O lab-only soldier warp, `world_place_guest.lua:165`; I for the effect]. Each
   companion's hold mechanism is named in section 6.12 (F12). Attribution becomes id-aware (6.12): a mirror attacker is
   a new class `mirror`, logged as `mirror_engaged` and never forwarded. If a hold cannot be made reliable, the
   companion's combat is not silently degraded: decision D7 applies.
5. **Mirrors are not hurt.** Guards on a copy are not synced in play, so they can shoot the mirror. A mirror is made
   invulnerable where an API exists, and its health, death and hospital come only from the host's COMPANION state
   (6.12, E19).
6. **Unknown means hidden, not wrong.** If a copy cannot show something (a missing vehicle, an unplaceable mount), it
   keeps today's fallback (visible driver for an absent vehicle; hidden rider for a mount) rather than drawing it in the
   wrong place.

## 5. Wire design

All new traffic rides the existing world bundle (`W` magic, ops with `u8 op | u8 len | u16 epoch | body`; an unknown op
is skipped by its length; `docs/world-sync-protocol.md` section 3) [O]. Used codes today: 1-28, 32-47 and the local
112-120; 29-31 are reserved [O, `lua/coop/world_sync.lua:4-10`, `native/world_wire.cpp`]. New codes start at 48. Every
new op gets a schema row in `native/world_wire.cpp` and a Lua handler in a new module (a world Lua chunk may be at
most 32000 bytes, `docs/STATUS.md:2017`) [O]. Both PCs must run the same version (`README.md` Troubleshooting) [O], so
a field change is a new op, never a resized old one.

| Code | Name | Dir | Wire | Rate | Fields | Replaces / reuses |
|---|---|---|---|---|---|---|
| 9-11 | TRANSITION / SEED / SEEDS_END | H->G | R | per travel | **New seed keys** (vars): `buddyType`, the look vars of 3.1, `initialBuddyStatus`; after E2 the relief selection var(s) and `reliefVehicleCarryType/Attitude/Life/Ammo/State`. Sent only after the Quiet lock (F1.4) | `world_follow_seed.lua` key list (`{name, dims, scale, true}` = vars) [O] |
| 48 | COMPANION | H->G (both for walker slot 1) | U, and R once per kind or state change | 10 Hz while moving, 2 Hz idle, plus every change | seq, sender tick, kind 1 dog / 2 horse / 3 walker / 4 Quiet, slot (0 host's, 1 guest's walker), present, xyz mm, yaw mrad, state byte (dog: follow/stay/attack; Quiet: standby/snipe/cloak/down; horse: gait 0..3; walker: idle/drive; all: hurt/hospital/dead), rider bits (seat 0 role, seat 1 role), target id (attack target, 0 if none; used after enemy sync, F2b) | Supersedes BUDDY_STATE 39 / ACK 40 (same idea, more fields) |
| 49 | COMPANION_ACK | G->H | R | 1 Hz | seq, kind, result, error cm | Same as BUDDY_ACK |
| 50 | MOUNT_FAST | both | U | 20 Hz while ridden | sender tick, kind 1 horse / 2 walker, slot, xyz mm, heading mrad, gait | RIDE_FAST's track machinery (100 ms render delay, extrapolation, stale rules) keyed by (kind, slot) instead of a per-copy id |
| 44 | RIDE_FAST | both | U | 20 Hz | unchanged | **Also sent by the host** for AI-driven vehicles near the guest (F8), 10 Hz |
| 51 | VEHICLE_STATE | H->G | R | on change (>0.5 m, >5 deg, damage or state change), at most 2 Hz per vehicle; on pairing a rate-limited sweep (4 vehicles per 250 ms) | vehicle id, locator hash (0 if unnamed), type, flags (alive, broken, npc-ridden, player-ridden, relief, burning), xyz mm, yaw mrad, body life per mille, wheel damage bits (read API [U]) | New |
| 52 | VEHICLE_SPAWN | H->G | R | per event | host vehicle id, cause (relief, drop, reinforce), type, subType, paintType, class, xyz, yaw | New; the guest records host id -> local id |
| 53 | REINFORCE | H->G | R | per event | cp hash, step (load, appear, cancel), reinforce type, coloring | New |
| 54 | SEAT | both | R | per event | seq, target (vehicle id, or horse slot with a horse flag), seat, role, action (claim, grant, deny, release) | New; host arbitrates |
| 55 | COMPANION_CMD | G->H | R | per press | kind, order id | Only if decision D1 is "forwarded" |
| 56 | CHARACTER | G->H | R | at pairing, and on change | the guest's resolved character (story resolved locally), Quiet flag | New, unless HELLO can carry it (HELLO's spare fields [U]); must arrive before the host's `sendSeeds` (F1.4) |
| 46/47 | VEHICLE_GONE / REQ | as today | R | per event | unchanged | Turned on by default after E5; index 0 per E17 |

**Ids.** Vehicle ids are per-copy pool slots and are not assigned in spawn-list order (3.6) [O]. VEHICLE_STATE
carries the locator hash, the type and the position; the guest applies it only to the vehicle that the locator names,
or, if unnamed, to the nearest live vehicle of that type within 40 m that `decideId` would trust (`world_vehicle.lua`
`:183-199`, the same check VEHICLE_GONE uses) [O mechanism]; otherwise it refuses with a log line. Companions are
addressed by (kind, slot), never by GameObject id, because in capacity two the host's walker is index 0 on the host's
copy and index 1 on the guest's (section 6.9).

**Budget** [I, to be measured]. Worst case per direction, host to guest, ops per second: COMPANION 10, MOUNT_FAST 20
(the host's walker), RIDE_FAST + RIDE_TILT 20 + its existing tilt rate while the host drives, host AI-vehicle
RIDE_FAST 10 per vehicle within 300 m (4 vehicles: 40), VEHICLE_STATE up to 2 per changing vehicle (a firefight with
4 vehicles: 8) and the pairing sweep (16 per second for about 1.5 s). That is about 100-120 ops/s on top of today's
puppet and world traffic, and the same order guest to host while both drive. The earlier "60 datagrams, 2 KB/s" was
an undercount. Reliable ops (VEHICLE_STATE, the sweep, SEAT) share the world reliable queue with kill and alert ops
[O, one reliable world channel, `link_protocol.hpp:287`], so a sweep under loss can delay an ENEMY_OUTCOME
(head-of-line) [I]. M4's proof measures this under `-WanSim bad`: the time from a host `event=commit` to the guest's
`event=apply` for a kill made during the pairing sweep.

## 6. Per-feature approach

### 6.1 F1: host-owned companion at sortie (M1)

1. **Seeds.** Add the buddy vars of 3.1 to `world_follow_seed.lua`. The guest applies seeds in its `TppMission.Load`
   wrapper (`world_follow.lua:484-496`) [O], before its next `OnAllocate`, so the retail `Load()` and the Mother Base
   pack list read the host's buddy [I; E1 checks MB order]. This reverses `docs/world-sync-protocol.md` section 15's
   principle that the module "does not change saved buddy selection or force Load" [O, `:1050`]; the reversal is
   deliberate and limited to co-op sessions.
2. **The guest's save.** Seeded vars are saved vars, and the guest writes its own save at every host checkpoint
   (W4 runs the guest's own `UpdateCheckPoint`, `world_recovery.lua:60-100`) [O]. So without care, the host's
   `buddyType`, look vars and relief vars end up in the guest's co-op save, including a buddy the guest's save never
   obtained (the Load core checks no unlock, 3.1) [I]. And on a guest whose Mother Base lacks Quiet's weapon (equip
   type 4), `ResetQuietEquipIfUndevelop` resets the seeded Quiet weapon at `OnAllocate` [O code; I effect: the mirror
   Quiet shows a different rifle, harmless if she never fires]. Decision D8 picks the policy; the recommendation is
   (b): the guest snapshots its own buddy and relief vars at pairing, restores them around each checkpoint write
   (restore, call the stored original, re-apply the host's values) and at unpair. The Load core reads the vars once
   at `OnAllocate` [I from `0x140A485F0`], so restoring them around the save write does not change the loaded buddy;
   other readers between (`character.lua` Gather, the MB pack list) run at mission load, not at checkpoints [I]. E3
   checks the save vars and the case where the guest's save lacks the host's companion.
3. **Late join, reconnect, mid-mission change.** On COMPANION kind change or a mismatch at pairing, the guest
   reconciles: `CallBuddy(kind, hostPos, yaw)` while its block is active, otherwise `ReserveCallBuddy(kind, NORMAL,
   pos, yaw)` + `Load()` (the F9 pattern, `debug_buddy.lua:46-70`) [O works mid-mission]; host NONE -> `DeleteBuddy`
   (never a release to the guest's AI).
4. **Guest requests refused.** Three paths, each found and tested in E4 on the guest's copy:
   - Lua entry points: wrap `TppBuddy2BlockController.CallBuddy`, `CallBuddyWithHalo`, `ReserveCallBuddy`,
     `DeleteBuddy` and the F9 summon; refuse with a toast unless the co-op module itself calls (it keeps the originals
     bound at hook time, as `world_buddy.lua:35-37` does) [I]. Note that `debug_buddy.lua`'s horse summon calls
     `W.api.buddyCall`, an original bound at hook time, so it bypasses these wrappers [O]; it needs its own host-only
     guard, and it does not prove the user's path.
   - The iDroid buddy deploy and the call menu (orders, horse call): whether they reach Lua at all is [U]. If not, the
     candidates are a pad mask on the call-menu button while the guest has no companion of its own, or the disable
     byte (below).
   - Fallback: `SetDisableBuddyType` for every type except the host's, accepted only if E4 shows the guest never needs
     to call another type that mission (the same byte blocks our own calls) [O].
   Decision D1 (a) depends on these: if the guest's orders cannot be refused, the guest's order moves its own mirror
   and splits the copies [I], so D1 (a) is only promised after E4 passes.
5. **Quiet lock, in this order** (the guest's Quiet character always wins):
   1. The guest resolves its character first, on its own copy, from its setting and its own save (for `story`:
      `C.Story(x.quiet)`, `character.lua:79-81`) and sends it in CHARACTER (op 56) or HELLO. In co-op the
      `buddy_quiet` fallback (`character.lua:83-86`, which reads the guest's `vars.buddyType` at Gather) is resolved
      **before** seeds are applied, so a seeded host buddy can never turn the guest's Quiet into a DD soldier [O race
      source; I fix].
   2. The host receives it before `F.sendSeeds`. If the guest plays Quiet: `TppBuddyService.SetDisableBuddyType(
      BuddyType.QUIET)` while the host is in the ACC, before sortie prep opens; if `vars.buddyType == QUIET`, set it to
      NONE in the host's `TppMission.Load` wrapper before the seeds go out (s10260 precedent, `s10260_sequence.lua:
      435-443`) and restore the saved value after the buddy block has copied it (`0x140A485F0` reads it once) [I].
      Seeds therefore never carry QUIET while locked.
   3. R5's pass signal is the host's sortie-prep (and iDroid) buddy list without Quiet. Whether those menus honour the
      disable byte is E4; if they do not, the fallback is the toast "Quiet is your partner's character" plus the NONE
      rewrite above, and the user is told (section 10, "Things the user should know").
   4. Mid-mission (a late join while the host's Quiet is out): host `DeleteBuddy()` (s10050 precedent) plus the toast.
   5. Mother Base: when the guest plays Quiet, hide the cell Quiet `TppMbQuiet` on both copies (the guest's own copy
      would otherwise show two Quiets too) [I]. The command to hide her is [U] (E20); candidates: a `TppMbQuiet`
      command, or keeping the `trap_toQuietPrison` handler from sending `StartWakeUp` plus an unrealize.
   Never use `SetDisableCallBuddyType` (a saved var) [O].

### 6.2 F2: D-Dog synced (M2)

On top of F1 the guest's copy loads its own instance of the host's dog, so `world_buddy.lua`'s existing hold path
applies. Changes:
- Guard: drop the `IsBlockActive()` requirement for the reconcile path (F1.3); keep it for the hold.
- Never release to the guest's AI: host has no dog -> `DeleteBuddy` on the guest (today `B.release` calls
  `CallBuddy(DOG, here)` back to normal AI when the original was a dog, `:168-172`) [O].
- Read scope, both roles: the own-dog read must not need body 1 realized. Today `field_ready` requires
  `pp::Realized(player,1)` (`game_script_observer.cpp:4015-4018`) for the same receipt on both copies [O], so the
  guest's hold also fails (`dog_unreadable`) while the host is hidden on the guest's copy (heli, mount hide, ride hide),
  and the mirror then follows the guest [I]. Lift that term for the dog pose read (keep it for anything that touches
  body 1); send `present=0` only when the dog itself is unreadable.
- Rate: COMPANION at 10 Hz while the host dog moves; hold every 100 ms; warp beyond 15 m (as today).
- Engagement hold and protection: F12 (6.12).
- Mother Base: the seeded `buddyType` DOG makes the guest's copy load `f30050_Buddy.fpk` [I, E1].
- Doc drift: `docs/world-sync-protocol.md` section 15 still says `RequestMoveToPosition` with a 0.75 m stop; the code
  sends `LuaAiStayAndSnarl` [O].

**What "synced completely" means, and what waits.** Enemy patrol and combat positions are corrected in lab only (play
refuses the soldier warp, `world_place_guest.lua:165`) [O], so in play the guard the host's dog bites can be somewhere
else on the guest's copy [I]. Stage 1 (M2, before enemy sync in play): one dog on both screens following the host;
the host's dog acts on the host's copy only; its kills, sleeps and Fultons reach the guest as host outcomes
(ENEMY_OUTCOME, at the guard's id wherever that guard stands on the guest's copy); the mirror holds and never engages.
Stage 2 (F2b, named follow-up after enemy sync in play and "Shared marking"): the COMPANION `attack` state with the
target id shows the mirror's attack on the same guard; the dog's spotting marks (D-Dog's main field job) are forwarded
as soldier ids and marked on the guest's copy. The marking message and the Lua marker call are [U]. The user is told
which parts of "completely" wait (section 10).

### 6.3 F3: sortie vehicle on both copies (M3)

- **Route A (preferred if E17 passes).** E2 finds the var(s) that select the relief vehicle. Seed them with
  `reliefVehicleCarry*`. The guest's own engine then loads the same relief pack and places its own index 0 (25600) at
  the same drop point, because the heli route is already seeded (`AdjustFromDropPoint(gvars.heli_missionStartRoute,
  ...)`) [I]. Its pose afterwards follows the general rules: RIDE_FAST while driven (the by-id seat handles unnamed
  ids: proven on the MB jeep 25603 in `abb0b7cf`, and it already asks for 25600 in free roam, answering `absent` in
  `31adc898` because the instance was missing) [O], VEHICLE_STATE while parked. Two costs:
  - **Removal of index 0** cannot use Despawn (3.5) [O]. E17 tests candidates on the receiver: `Seize` with `Fulton`
    but without `DirectAccount` (does it remove without crediting?), `SetBodyLife` 0 for a destruction (dictionary
    name [I]), a far `SetPosition` plus a hide, or a native path. Until one passes, route A cannot meet R7 for the relief
    vehicle.
  - **Mission-end seize** runs on both copies (3.5) [O code, I double credit]. The guest wraps
    `TppMission.SeizeReliefVehicleOnAbort/OnClear/OnForceGoToMb` to do nothing in co-op, and its
    `reliefVehicleCarry*` follows the host's (seeded) instead of its own `SaveCarry` [I]. E18 checks resources and GMP
    before and after a mission end on both copies, and whether the guest's replayed sortie charged the vehicle's cost.
- **Route B (fallback).** Host sends VEHICLE_SPAWN{25600, type, subType, paint, class, pos, yaw}; the guest spawns
  `Spawn{index=n>=1, ...}` of the same class into a free pool slot and maps 25600 -> its id in RIDE_FAST, the seat
  gate and VEHICLE_GONE. Because the guest's copy is an index >= 1, Despawn works and no seize runs on the guest [I].
  Works only for the location faction's light vehicle and truck (only those bodies are loaded) [O]; an APC, a tank or
  the other faction's jeep stays `absent` on the guest (today's fallback).
- **Route C.** The in-mission air drop (`CMD_REQUEST_RELIEF`, entry unknown) on the guest's copy: research only (D6).
- Guest choice: overwritten by the seed (A) or never spawned (B); the guest never sorties itself anyway [O,
  `world_follow.lua:412-413`]. In-mission vehicle requests by the guest are refused (E4 finds the path).
- Carry across missions: `Vehicle.SaveCarry` / `LoadCarry` state is in `reliefVehicleCarry*` vars, seeded too [I];
  the guest's save gets the host's values unless D8 (b) restores its own.

### 6.4 F4: vehicle losses, damage and parked vehicles (M4)

- Turn on VEHICLE_GONE in play after E5 (part C hold while seated; a manual player Fulton). Destroyed vehicles: try
  `SetBodyLife{0}`-style wreck parity (dictionary name [I]) inside E5; otherwise keep Despawn. Index 0: per E17.
- VEHICLE_STATE, host side: the host scans the pool once per second (ids 25600..25600+instanceCount-1, `IsAlive`,
  `GetPosition`, `GetRiderId`, the damage read if one exists [U]) and sends changes. It **skips**:
  - any vehicle the local player rides (`vars.playerVehicleGameObjectId`);
  - any id with a live partner RIDE_FAST track (`world_vehicle.lua` latches it for 10 s past the last datagram,
    `:20, 116, 425-427`) [O], because the guest's seat chain never writes `rider[]`, so `GetRiderId` reads a
    guest-driven vehicle as empty on the host's copy [O, `partner_board_policy.hpp:16-24`];
  - any vehicle with an NPC in any seat (`GetRiderId` on the host's copy).
- VEHICLE_STATE, guest side: apply `SetPosition{position, rotY}` only to a vehicle that matches (section 5 ids), that
  neither player rides on the guest's copy, and that has no NPC rider on the guest's copy. Until enemy positions sync
  in play (M8), the guest's copy drives its own trucks with its own AI (`a42ddafe`) [O], so the parked sync is further
  limited to vehicles with **no LRRP or travel-plan assignment on either copy**: a static per-location exclusion list
  from the vehicles named by `SetRelativeVehicle` / travel plans in `f30010_enemy.lua` / `f30020_enemy.lua` [I].
- Damage short of loss: flags and body life in VEHICLE_STATE; the guest applies them with `SetBodyLife` /
  `SetWheelDamageRate` [I names, U semantics]; a burning flag is display-only until a write is found [U].
- Host-only vehicle requests: the guest's iDroid vehicle request is refused (E4).

### 6.5 F5: Quiet (M5)

- F1 gives the guest's copy its own instance of Quiet (seeded `buddyType` and look). Hold it at the host's Quiet pose
  with `RequestMoveToPosition` / `RequestWalkToPosition` or a warp, once E15 names her commands [U].
- Her shots: only the host's Quiet acts. The mirror must never engage (rule 4): the hold mechanism is F12's Quiet row,
  found in E15. If E15 cannot make her inert, the fallback is **not** chosen silently: decision D7 asks the user
  (hide the mirror Quiet, so only the host sees her, or wait for enemy sync).
- State byte: standby, snipe, cloak, down/hospital; cloak and her sniper point show on the guest from the stream.
- Buddy events (hospital, lost, dying flags `BuddyCommonFlag`) stay host-side; the guest copy's own Quiet state must not
  change through the mirror (E3 checks saved var drift; E19 the damage path) [U].
- Same two-stage meaning as the dog (6.2): before enemy sync in play the guard Quiet shoots may stand elsewhere on the
  guest's copy; the guest sees the outcome, not the shot at the right place [I].

### 6.6 F6: more passenger seats (M6)

- **Light vehicle front seat (1).** States exist [O]. Stock phase 1 treats seat 1 like seat 0 at **two** sites [O,
  3.7]: the seat passed to TryToRide (`0x141251832..0x141251840`) and the phase selected (`0x14125188E..0x14125189B`,
  driver phase 3 for seats below 2). Keeping seat 1 needs both changed (seat passed through, phase 2), as pinned
  patches beside the existing site B retarget, with contract pins in `tools/generate_partner_contract.py` [I]. Until
  E6 shows what the stock passenger phase does with seat 1, seat 1 stays out of the passenger design. The viewer's
  seat chain already maps seat 1 (`partner_sync.lua` seat table) [O].
- **Truck cab (1).** Same two patches, plus the truck door gate (CanExec: within 1.4 m of seat + 0.4 m outward; the G4
  `debug_vehicle_door` lever, lab only, `docs/native-puppet.md:520-542`) [O]. Seat-1 loop borrowed from the light
  vehicle [O]. E6 first reads the truck's seat numbering (it reads `seats=2`, `84e3db0e`).
- **Truck bed.** Stock `stateTruckHide` / `Loop` / `ToWait` and `vehicleGameObjectIdOnPlayerOnCargo` [O names]. The bed
  is not a seat index (`seats=2`), so body 1 is anchored to the bed's CNP (`CNP_CARG` / `CNP_DECK_A`) or a measured
  offset from the truck root, never to a seat index 2 or 3, whose effect in `0xB7AF90` is [U] and could read out of
  bounds [I]. E7 first reads the truck's seat count and CNP matrices. Bed shooting is a stretch goal.
- **APC and tank.** No passenger states [O]. "Crew" seat: the passenger is hidden (unrealize, like the heli hide) and
  carried with the vehicle on its own copy, camera on the vehicle. A gunner seat for the tank HMG is M13.
- Seat races: SEAT op (section 5).

### 6.7 F7: shooting as a passenger (M7)

- Local: a stock back-seat passenger already enters aim (`637b47ea`, diag #7, 3.7) [O]. E8 checks frame `aim-seated`
  of `637b47ea` first, then narrows to firing and hit registration. The seat-aim census is an uncommitted tree on
  0.13.5 (`cc973a8`) that changes `game_script_observer.cpp`, `partner_player_contract.hpp`, `lab_config` and tests
  [O]; it is **ported** onto `wp/companions` (a merge commit, never a rebase), and the partner contract is regenerated
  and re-pinned for 0.14.3's count [I]. If the passenger can fire, enable it and keep the seated aim latch fix.
- Partner view: design A from the seat-aim research: a graph-2 `GunHoldStart` (`0x50130AE2E584`) / `GunHold`
  (`0xAA0EA22A5E0E`) pose on the DM-seated body 1, plus letting the aim mirror run for a seated partner
  (`partner_aim::RideGate` refuses today) [O design, U runtime: E9].
- Hits: the shooter's own copy decides, as on foot (REQ_OUTCOME) [O].

### 6.8 F8: AI-driven, reinforcement and side-op vehicles (M8)

- The host streams RIDE_FAST for vehicles whose seat 0 holds an NPC (`GetRiderId`) within 300 m of the guest. The guest
  stops its own AI driving with `ForceStop{enabled=true}` and places the vehicle per frame [U: E10].
- Reinforcements: the host sends REINFORCE steps; the guest runs the same `TppReinforceBlock` steps locally and ignores
  its own CP's reinforcement requests (wrap the CP message handlers on the guest) [I]. The vehicle then follows the
  host's RIDE_FAST.
- Side-op armor and convoys (1.1): the same per-frame placement once side-op spawning is shared (roadmap "Side ops").
- Depends on enemy position sync reaching play: the soldier warp receipt is lab-only today
  (`world_place_guest.lua:165`, `GuestWarpReceipt` refuses unless `mode=lab` and `debug_soldier_warp=1`) [O].

### 6.9 F9: a D-Walker each (M9)

- Only when the host's buddy is WALKER_GEAR (decision D2); never in Mother Base (f30050 disables it) [O].
- Ship the capacity-two `buddy_wg2_00` in the runtime archive for that case only (today lab-only) [O]; measure memory
  first (E14).
- On each copy index 0 is the local player's walker and index 1 the partner's mirror (`docs/mounts.md` section 7 step
  0); register index 1 in `R.mirrors` [O mechanism]; `NoRideOn` on index 1 so ACTION mounts index 0 (E14) [U].
- MOUNT_FAST from each pilot; the other copy `SetPosition{pos, rotY}` per frame [O command].
- Pilot drawing: body 1 pinned to the walker with a seated DM pose (the walker drive node has no Lua name) [O]; until a
  frame shows it, the walker moves with its pilot hidden (today's hide).
- Walker gun hits: the pilot's copy decides. Today `attClass` counts **any** player walker as `local`
  (`world_outcome.lua:30`, `Tpp.IsPlayerWalkerGear` is a class check) [O], so the mirror walker's hits on the guest's
  copy would be forwarded as the guest's. F12's id-aware attribution makes only the walker this copy pilots `local`.

### 6.10 F10: the shared D-Horse, one rider (M10)

- Both copies hold the host's horse as index 0 (6656) through F1; Mother Base has no horse [O].
- Pose owner: the rider's copy; unridden: the host. The other copy places its instance per frame with the API E11 finds
  (candidates: the `SetCallHorse` start warp, one of the four unnamed dispatcher hashes, a native transform write)
  [U]. Gait has no known write API (`comm+0x58`) [O]; v1 accepts a sliding horse.
- Rider drawn on the other copy: body 1 DM `stateRideOnIdle` / `Trot` / `Canter` / `Gallop` from the streamed gait
  (statuses 44..47); the pose plays on body 1 (`251abcef`, 3.3) [O]; it is pinned at the saddle offset E12 measures,
  through a type-13 branch of anchor `0x1198770` or a per-tick transform pin [I]. Lift `partner_native_mount=hide` for
  the horse only when the instance is placed.
- **RIDE_BODY rule: decision D4.** The 2026-10-22 decision protects the viewer's own horse from the partner's stream
  [O, `docs/mounts.md:21-25, 67-85`]; the failure it fixed was a horse pulled away so that "its first E press found no
  horse to mount" (`mounts.md:59-65`). Under R2 both copies' index 0 is the host's horse, and either player may ride
  it (R6), so placing a copy's index 0 from the partner's stream is needed in **both** directions (the host's copy too,
  when the guest rides). That reverses the user's rule for the shared horse, so it is the user's call (D4), not a
  silent design choice. The proposed handoff rule: per-frame placement applies only while the partner's MOUNT_FAST
  track is live; it stops (after one final reliable pose) when the partner dismounts; it never applies while the local
  player is within 4 m of the horse and not riding (mount range; the stock ride-icon range is [U], E11 measures it), and
  never while the local copy rides it. M10 checks that the first E always mounts, in both roles.
- **Not taken for v1: `mounts.md` section 7 step 4** (body 1 natively mounts the horse through the stock ride, driven by
  the replayed pad). It would give the horse its own gait animation, but it needs the unproven ACTION start for
  instance 1, the horse would then follow body 1's replayed pad and be corrected by recon (two sources of motion), and
  it uses the horse's single rider slot on the viewer's copy, which F11 v2 needs for the passenger [I]. It is the
  fallback if E11 finds no placement (E21, research).
- Attribution: the horse is `local` only while the local player rides it; otherwise `mirror` (F12) [I fix; O today:
  any horse is `local`, `world_outcome.lua:30`].

### 6.11 F11: two on one horse (M11)

- SEAT claim for seat 1 (pillion) on horse slot 0; the host grants it only while seat 0 is held.
- **Driver's copy:** the passenger is body 1, drawn with `stateRideOnIdle` pinned at the rear offset E12 measures [U];
  E12 films it at idle, trot and gallop.
- **Passenger's copy, v1:** the local player is carried at the rear offset of its copy's horse. v1 is **Lua-only** and
  does not use the `MGS_COOP_CARGO_V1/V2` receipt, which drives `coop_carry` (`game_script_observer.cpp:4459-4470`)
  [O]; `native/coop_carry*` is not edited and its carry state is not repurposed. If a native helper is needed, it is a
  new receipt in a new file. Two open parts, both in E13:
  - **Pad mask.** The prototype mask `buttons=0xFFFFFFA7` leaves only bits 0x58 free and sets stance SQUAT
    (`world_horse_cargo.lua:120, 315`) [O]; it does not keep aim and fire. F11 needs a new mask built from the
    `PlayerPad` constant names (not literal bits): free = fire, hold/aim, reload, weapon switch, binoculars; masked =
    move stick, sprint, stance, ACTION/E, CQC, roll. The names and bit values are [U]; E13 reads them from Lua.
  - **The passenger's own pose.** A squatting player warped each tick (the prototype) would disagree with the driver's
    screen, which shows body 1 in `stateRideOnIdle` [I]. v1 plays a DM riding pose on the local player (body 0) if the
    Lua DM binding works on body 0 together with aim [U, E13]; otherwise v2.
- **Passenger's copy, v2 (if v1 aims badly or cannot pose):** the passenger natively mounts its copy's horse (its single
  rider slot is free there, the driver being body 1), the horse is placed from the driver's stream shifted forward by the
  measured rear offset so both copies agree on world positions, and the passenger's steering input is masked. Needs
  F10's placement [I].
- Shots register on the passenger's copy (REQ_OUTCOME) [O rule]; the horse's own hits follow F12 (only the rider who
  steers on that copy counts the horse as `local`).
- The driver gets off: the passenger gets off too (v1).
- Either role order: host steers with the guest behind, and guest steers with the host behind (R6).
- Partner view of the passenger aiming: same open question as E9. Throws and marking from the pillion: later.

### 6.12 F12: mirror safety (M1, M2, M5, M9, M10)

Rules 4 and 5 of section 4, per companion. Each hold must be named and tested before the milestone that shows the
mirror in play.

| Mirror | Engagement hold (candidates; E3, E15) | Protection (E19) | Attribution |
|---|---|---|---|
| D-Dog (guest's copy) | `LuaAiStayAndSnarl` at the host dog's pose (today) [O]; whether it still bites a guard within reach is [U]. Candidates if it does: a stay order through a Lua command [U], or an unrealize while a guard is within 5 m (the mirror vanishes briefly rather than acting) | [U]: no dog life command known; else carry death/hospital from COMPANION only and ignore the guest copy's own dog damage | `mirror` |
| Quiet (guest's copy) | A cease-fire order or engagement flag through her controller requests [U, E15] | [U] | `mirror` |
| D-Horse (non-riding copy) | A horse does not attack; its collisions with guards are possible [I] | `SetMaxLife` (dispatcher `0x140E4D250`) [O name, U effect] | `local` only while the local player rides it; else `mirror` |
| Partner's D-Walker (index 1) | No pilot on that copy, so no gunfire; collisions possible [I] | Walker life command [U] (16-command dispatcher) | `local` only for the walker this copy pilots; else `mirror` |

- **Id-aware attribution** (fixes `world_outcome.lua:27-38`): `W.attClass` returns `local` for the local player, the
  horse while `GetRidePlayer` / `vars.playerVehicleGameObjectId` says the local player rides it, and the walker index
  this copy pilots; `mirror` for any registered mirror (`R.mirrors`, the guest's held dog and Quiet, the shared horse
  when not ridden locally); `buddy` only for a companion this copy owns (the host's own, or the guest's under D5 (b));
  `other` otherwise. `mirror` is logged as `world_lua=event=mirror_engaged; kind=<k>; msg=<Damage|Dead|Sleep|Faint|
  Fulton>; ent=<id>;` and counted, and never forwarded. A new fixture test
  `tests/world_outcome_attclass_lua_check.lua` pins: a mirror walker (index 1), an unridden horse and a partner-ridden
  horse classify as `mirror`; the locally piloted walker and the locally ridden horse as `local`.
- **Why not forward mirror outcomes** (today's behaviour for `buddy`): a forwarded mirror kill becomes a host-side kill
  of a guard that, in play, may stand elsewhere on the host's copy, so the mirror would act in the world [I]. Not
  forwarding leaves a split that nothing repairs [O]. Neither is acceptable, which is why the hold is the requirement.
- **Hurt mirrors:** the guest copy's own buddy health, `deadBuddyType` (+0x2D6C) and `BuddyCommonFlag` must not change
  through the mirror [U]; E19 has guards shoot each mirror and reads those vars.

## 7. Risk register

| # | Risk | Likelihood / impact | Mitigation | Gate |
|---|---|---|---|---|
| K1 | Seeded buddy and relief vars are written into the guest's co-op save at each host checkpoint, because W4 runs the guest's own `UpdateCheckPoint` (`world_recovery.lua:60-100`); a guest save that never obtained the companion keeps `buddyType` set to it | High / medium | D8 (b): restore the guest's own vars around each checkpoint write and at unpair; diff save vars | E3 |
| K2 | A mirror companion engages on the guest's copy (bites, shoots, Fultons); nothing reverts the result, and forwarding it creates phantom host kills | High / high | F12 holds; `mirror` class, logged; E3/E15 pass only on zero engagements; otherwise D7 | E3, E15 |
| K3 | Seeds land after the Mother Base pack list: no dog or Quiet in MB | Medium / medium | Apply the buddy keys earlier if needed | E1 |
| K4 | The relief selection var cannot be found | Medium / medium | Route B (location LV/truck only), keep `absent` for the rest | E2 |
| K5 | `SetDisableBuddyType` also blocks our own `CallBuddy` | Certain / medium | Lua wrappers instead of the mask | E4 |
| K6 | No per-frame horse placement exists | High / high (blocks M10, M11) | Static research first; fallback: hidden rider (today), D3, or E21 | E11 |
| K7 | One rider slot per horse | Certain / high | Non-native passenger (E12, E13) | E12, E13 |
| K8 | Per-tick Warp breaks aim, camera or stance for the passenger | Medium / high | v2 native rider | E13 |
| K9 | Capacity-two walker: memory, stability, guest ACTION mounted nothing | High / medium | `NoRideOn`, measure memory, walker only for D2 (a) | E14 |
| K10 | AI drivers fight per-frame placement; enemy positions are lab-only | High / medium | ForceStop; depend on the enemy-sync milestone | E10 |
| K11 | Seat races within one round trip | Low / low | SEAT op, host grant | M6 proof |
| K12 | The seat-1 patches are new native code at two sites (seat passed, phase selected) | Medium / medium | Pinned bytes in the contract generator like the board hooks; seat 1 out until E6 | E6 |
| K13 | Vehicle ids differ between copies (ids are not list-ordered) | Low / high | Locator hash + nearest-of-type check before any SetPosition | M4 proof |
| K14 | Removing a vehicle under a seated player | Medium / high | VEHICLE_GONE part C | E5 |
| K15 | Quiet's commands unknown | High / medium | Static read first (capstone) | E15 |
| K16 | Late join mid-mission: the guest's buddy must be swapped while loaded | Medium / medium | `ReserveCallBuddy` + `Load` path (F1.3) | M1 proof |
| K17 | Reversal of the 2026-10-22 RIDE_BODY rule for the shared horse; the "first E finds no horse" failure returns | Medium / medium | Decision D4; handoff rule (6.10); tests pin "never the viewer's own buddy" for every other case; M10 checks the first E in both roles | D4, M10 |
| K18 | `SetDisableBuddyType` scope (when the byte resets) | Unknown / low | Read it at each mission start | E4 |
| K19 | Relief index 0 cannot be removed on the receiver (Despawn rejects index 0; VEHICLE_GONE refuses `index0`), so its Fulton or destruction splits the copies under route A | Certain (route A) / medium | E17 candidates; else route B for LV/truck | E17 |
| K20 | Mission-end relief seize and carry run on both copies: double Mother Base credit; the guest's replayed sortie may charge GMP | High (route A) / medium | Wrap `SeizeReliefVehicleOn*` on the guest; measure GMP | E18 |
| K21 | Guards on the guest's copy hurt or kill a mirror; the guest copy enters a dead, hospital or retreat state the host does not have, and the block may drop the mirror | High / high | Invulnerability where an API exists; death and hospital only from the host's COMPANION state | E19 |
| K22 | The guest's own call menu, buddy orders or horse whistle move its mirror (native paths, not Lua) | High / high | E4 finds the path; pad mask or disable byte; D1 (a) only after E4 | E4 |
| K23 | Any horse and any player walker count as `local` today, so mirrors' hits are forwarded as the guest's | Certain / high | Id-aware attribution + fixture test (F12) | M9-M11 tests |
| K24 | VEHICLE_STATE moves a vehicle that the guest drives (rider[] is empty on the host's copy) or that a guest-local NPC drives | High / high | Skip RIDE_FAST-latched ids and NPC-ridden ids on either copy; LRRP exclusion until M8 | M4 proof |
| K25 | The guest's Quiet character loses to a seeded `buddyType` QUIET at Gather (falls back to a DD soldier) | Medium / medium | Resolve the character before seeds; host lock before `sendSeeds` (F1.5) | M1 proof |
| K26 | Two Quiets in Mother Base (cell `TppMbQuiet` plus the guest's Quiet) | High / low | Hide the cell Quiet on both copies | E20 |
| K27 | Reliable sweeps delay kill and alert ops (head-of-line under loss) | Medium / medium | Rate-limited sweep; measure commit-to-apply latency under `-WanSim bad` | M4 proof |
| K28 | Static reads are slower without Ghidra | Certain / low | Capstone reads (header); no milestone is blocked on Ghidra | - |
| K29 | `Seize` on a mirror credits the receiver (if E17 uses it) | Medium / medium | Test without `DirectAccount`; diff resources | E17 |

## 8. Experiments before the risky milestones

All hidden-worker runs: background desktop, copied saves, lab mode unless stated, `boot` per `docs/test-boot.md`.
Static steps use capstone on the exe file (header).

| # | Hypothesis | Steps | Pass / fail |
|---|---|---|---|
| E1 | Seeding `buddyType` + look vars before the guest's Load makes the guest's copy load the host's buddy, in free roam and Mother Base | Lab build with the seed keys behind a lab key. Host seed with D-Dog deployed, guest seed with no buddy or a horse. `boot 30010 828.18 338.20 1630.51 95` (`afgh-ledge`), then `boot 30050 22.61 1.80 -15.55 0` (`mb-ladder`). `both run buddy_status` (gains a `vars_type=` field) | Pass: guest `buddy:G a1 ... kdog`, `vars_type=2`, `IsBlockActive` true, MB dog arrives with the heli. Fail: `buddy_block_inactive`, or the guest's own buddy |
| E2 | The relief choice lives in a saved var or sortie buffer the census can read | Static first: capstone scan for writers of the ReliefBlock record byte +0x2B and for the `reliefVehicleCarry*` and `sortieLoadoutInfo*` declarations. Runtime: a lab census command dumps `sortieLoadoutInfoU8buf/U16buf/U32buf`, `userPresetVehiclePaintType`, `reliefVehicleCarry*` and the record byte +0x2B at ReliefBlock load. The seed with a vehicle chosen comes from a scripted sortie-prep selection in the hidden worker if the worker can reach that menu [U], else from a co-op save the user chooses to share (D10); never from a real save | Pass: one field changes with the choice and matches the loaded `veh_rl_*` pack; a lab lever that writes it reproduces the choice. Fail: route B |
| E3 | The guest's mirror buddy never engages, and the save policy holds | E1 setup, enemies near (no `clear_enemies`), a guard walked to within 3 m of the mirror dog for 120 s; the host's dog ordered to attack a different guard. Log every Damage, Dead, Sleep, Faint and Fulton message on the guest's copy whose attacker is the mirror (`mirror_engaged`). Diff guest save vars +0x2D68..+0x2E64 and `reliefVehicleCarry*` before and after a checkpoint and a mission exit, with D8 (b) on and off. Second case: a guest save that never obtained the host's companion (a seed without D-Walker developed, host deploys D-Walker) | Pass: zero `mirror_engaged` lines; with D8 (b) the guest save keeps its own buddy vars; the locked-companion case loads for display and leaves the guest save unchanged. Fail: name a stronger hold (F12) or take D7 |
| E4 | Every guest buddy and vehicle request path is known and refusable | Guest's copy, each path with a log line saying which hook fired (or none): iDroid buddy deploy; the call menu's orders to the mirror dog (attack, stay) and to Quiet; the horse call/whistle; F9 summon; iDroid vehicle drop. For each: try the Lua wrapper, then a pad mask on the menu button, then the disable byte; show the toast. Host: `SetDisableBuddyType(QUIET)` in the ACC, then frames of the sortie-prep and iDroid buddy lists, and when the byte clears | Pass: each path refused by a named method, frame of the toast; Quiet absent from the host's menus. Fail: list the paths that stay native-only; D1 (a) is then not promised and the user is told |
| E5 | VEHICLE_GONE is safe to default on | `coop-vehicle-gone.txt` part C (guest seated in the jeep while the host loses it) and a manual player Fulton; also try a wreck instead of Despawn | Pass: the removal waits for the rider, then applies; the Fulton removes on both. Fail: keep off |
| E6 | Seat numbers and the seat-1 offer | `GetRiderId` counts for LV, truck, WAV, tank and 25600 in 30010/30020; the seat matrices and the bed CNPs (`CNP_CARG`, `CNP_DECK_A`) of a truck; truck half of `coop-vehicle-passenger.txt` with `labsetting debug_vehicle_door=1`; a lab lever that patches **both** phase-1 sites (`0x141251832..840` seat passed, `0x14125188E..89B` phase 2) for seat 1 | Pass: counts and bed geometry known; seat 1 boards and the stock passenger phase runs. Fail: seat 1 stays driver-only |
| E7 | The truck bed works as a passenger seat | Host drives `veh_trc_000N`; the guest uses the stock bed hide on its copy; body 1 DM `stateTruckHideLoop` anchored to the bed CNP or offset from E6 on the host copy | Pass: frames show the guest in the bed on both copies while moving. Fail: bed out of scope |
| E8 | A local back-seat passenger can fire and hit | First: look at frame `aim-seated` of `637b47ea` (is the gun raised?). Then port the seat-aim census onto `wp/companions` (merge commit; contract regenerated and re-pinned) and run `coop-vehicle-seat-aim.txt` with `-SaveSeed africa-jeep`: fire at a target while the host drives | Pass: a shot lands and registers. Fail: needs a native aim enable |
| E9 | Body 1 can show a gun hold while DM-seated | Lab FRAME test of graph-2 `GunHoldStart` / `GunHold` on the seated body 1; check it keeps the remote's gun | Pass: a weapon pose draws in the seat. Fail: partner aim stays hidden |
| E10 | `ForceStop` stops an AI driver so per-frame placement holds | Lab: `ForceStop{enabled=true}` on an LRRP truck with its driver, then `SetPosition` each frame | Pass: no fight, smooth. Fail: M8 needs AI suppression another way |
| E11 | A per-frame horse placement exists | Static (capstone): the `SetCallHorse` start-position path in `0x140E4D250`, the four unnamed hashes, any `TppHorse2` transform write. Then a lab run placing the host's horse on the guest's copy at 20 Hz; measure the stock mount range | Pass: smooth, no leap. Fail: K6 fallback |
| E12 | A rear rider can be drawn on the driver's copy, and where it sits | Lab: host rides D-Horse; read the driver's census pose against its horse root to fix the saddle frame on TppHorse2; then DM `stateRideOnIdle` on body 1 pinned behind it at 20 Hz; frames at idle, trot, gallop | Pass: body 1 stays behind the saddle with bounded slide; the offset is recorded. Fail: a type-13 anchor branch |
| E13 | The passenger can aim and fire while carried, Lua-only | Lab: read the `PlayerPad` names and build the F11 mask; carry the local player at the E12 offset without the cargo receipt; try a DM riding pose on body 0; aim and shoot at a target from a moving horse | Pass: aim holds, the pose matches the driver's view, shots register. Fail: v2 native rider |
| E14 | Two walkers can ship and each player rides their own | Capacity-two walker profile: `NoRideOn` on the peer's walker; ACTION on each copy; SetPosition stream; process memory before and after | Pass: each mounts its own, the peer's moves smoothly, memory rise acceptable. Fail: D2 (a) only with hidden pilots, or drop |
| E15 | Quiet can be mirrored and held inert | Static (capstone): find the `TppBuddyQuiet2` dispatcher from the type handle `0x142C36A38`, and an order or flag that stops her engaging. Lab (`quiet-recruited` seed): hold the guest's Quiet at the host's pose with a guard in her sight for 120 s | Pass: held, zero `mirror_engaged`. Fail: D7 |
| E16 | Vanilla D-Horse rider actions, as the passenger baseline | Lab: host on D-Horse aims and fires primary and secondary, reloads, switches weapon, uses binoculars | Records which actions work from a horse |
| E17 | Relief index 0 can be removed on a receiver | Lab, route A setup or the lab relief seed: on one copy try `Seize{Fulton}` without `DirectAccount`, `SetBodyLife` 0, far `SetPosition` + hide; diff resources | Pass: one method removes (or wrecks) index 0 with no credit. Fail: route B only |
| E18 | The guest's mission-end relief seize and carry can be suppressed, and the sortie cost is charged once | Lab: mission end (clear, abort with save) with the relief vehicle on both copies; guest wraps `SeizeReliefVehicleOn*`; read GMP and vehicle resources on both before deploy, after deploy and after the end | Pass: one credit (host), one charge. Fail: route B |
| E19 | Mirrors can be protected | Lab: guards shoot the mirror dog, the shared horse (unridden) and the partner's walker on the guest's copy; try `SetMaxLife` (horse) and any walker/dog life command; read `deadBuddyType`, `BuddyCommonFlag`, the dog's state | Pass: no mirror dies or retreats on the guest's copy; vars unchanged. Fail: K21 stands, D7 |
| E20 | The Mother Base cell Quiet can be hidden | Static: `TppMbQuiet` commands (strings, dispatcher). Lab (`quiet-recruited`): guest character `quiet`, both walk to the cell (spot captured as seed `mb-quiet-cell`) | Pass: no cell Quiet on either copy while the guest is Quiet. Fail: keep the guest's Quiet away from the cell (toast) |
| E21 | Body 1 can natively mount the shared horse (`mounts.md` section 7 step 4) | Research only: the ACTION start for instance 1 (the F3 icon path) and the ride component for idx 1 | Records feasibility for F10's fallback |
| E22 | Both players can ride the support heli visibly | Research: the heli's seats and player states (`stateHeliIdle`, `stateHeliRideOn`), today's acts 97..108 hide, whether body 1 can be DM-seated in the heli | Records a design for M12 |

### 8.1 Results

**E5 (2026-10-28, branch `wp/vehicle-state`): passed for the hold and the engine's Fulton; the player's own Fulton
was not reproduced; a wreck replaces Despawn for destroyed vehicles.** Hidden runs (lab, research build, gun stage;
each Closed, BaselineRestored, RealSavesUnchanged, no `step=error`, fault or `viewer_changed`; frames in the session
scratch folder `complete/frames/vehicle-state/`):
- `92720b75` (`coop-vehicle-gone.txt` reordered: C first on J1, then A, B, D, and a new E; Wakh Sind, lab seed).
  **C passed** [O]: the guest sat in J1 = 25608 (`vehicle;id=25608;seats=4;riders=0,9728,...;localSeat=1`: the guest
  drives, D-Dog 9728 rides along) while the host lost it (`vehicle_remove nearest sync` -> host `step=tx; id=25608;
  cause=3; actor=2`); the guest logged `step=rx`, `step=hold; reason=local_rider`, after its get-off `step=hold;
  reason=occupied` (D-Dog still in the seat for 2.5 s) and then `step=applied; id=25608; via=locator; src=rx; ok=1`.
  Frames `hold-guest-seated` (guest: seated in the jeep the host's copy no longer has; host: the jeep gone, the guest
  standing at seat height, the 0.14.3 fallback) and `hold-applied` (gone on both). A (25615) and D (the engine's
  own Fulton of 25617: `watch; m=Seize` -> `obs; m=Fulton` -> `lost` 3.2 s later -> host `applied; src=req` ->
  `req_done`) passed again. B did not reproduce: the host did not board J3 from its goto spot (`vehicle_not_riding`),
  so its divergence lines never came (B was proven in `075c1ea4`). **E (the player's own Fulton) not reproduced** [O]:
  the guest held E 4.5 s beside J5 at its goto spot; it neither boarded nor Fultoned (frames `pfulton-*`, no
  `FultonInfo`); whether the lab seed has the cargo Fulton, and which input starts it, is [U]. The real-Fulton
  evidence stays the 2026-10-22 two-PC friend log (`obs; m=FultonInfo; ent=0; id=25603; att=local`) [O] plus the
  stock `Seize` readback proven twice (`075c1ea4`, `92720b75`) [O].
- `d407f115` (`vehicle-state.txt`, Africa, `africa-jeep` seed): **`SetBodyLife{life=0}` is the engine's own
  destruction** [O]. On the guest's copy alone: `vehicle_life 25608 0` -> `obs; m=VehicleBroken; id=25608` at once,
  IsAlive false, the life word reads 65535 and the state word 0x3F7 (every StoreToSVars bit), frames
  `m4-guest-life0-*`: the jeep explodes and burns on the guest's screen only. The guest did not request it (no local
  Damage within 10 s, as designed). Then the host's `vehicle_life 25608 0` broke the host's copy (`VehicleBroken` ->
  `vehicle_gone step=watch; cause=2` -> `lost` -> `tx`), and the guest answered `step=absent; reason=wreck`: both
  copies held a wreck. Both wrecks then left the pool on both copies (GetPosition no longer answers) about 20 s later.
  Body life 200 (of 2300) shows no smoke or fire on either copy and leaves the state word at 1 [O], so the burning
  state is not a pure function of the life word [I].
- `0edadbe2` (`vehicle-state.txt`, build `2a3d97d`, wreck parity on): **the receiver's wreck passed** [O]. The host's
  `vehicle_life 25608 0` -> host `watch; m=VehicleBroken; cause=2; att=none` -> `lost` -> `tx; cause=2; actor=0` ->
  guest `rx` -> `applied; id=25608; via=wreck; src=rx; ok=1; ms=116`; frames `m4-wreck-01..04`: the explosion and the
  fire at the same jeep on both screens, and both copies then read `alive=0; life=0; busy=gone_here`. The guest's own
  `vehicle_life nearest 0` (25610) wrecked only the guest's copy: its two shots had missed (life stayed 2300), so no
  local Damage preceded the break and no request went out (as designed), and the host's next refresh of 25610 was
  refused `absent` there. That split stays until a real loss (`docs/world-sync-protocol.md` section 17, "Not
  covered").
- `a50214f7` (`vehicle-gone-player.txt`): stopped at `placebehind` right after the guest's 990 m warp (`viewer landing
  starts without a valid partner pose`) before any vehicle step; the scenario now teleports both copies (`2464f4e`).
- `46b8fc12` (`vehicle-gone-player.txt`, build `2464f4e`): **the player's Fulton again not reproduced; diagnosed**
  [O]. Frame `pf-before` at the spot beside veh_lv_0002 (25610) shows only `E: get in` and no Fulton prompt, so the
  3 s E hold boarded the jeep (`partner_status=local; names=on_vehicle; vehicle=25610; seat=0`; frames `pf-lift-*`:
  the guest in the driver's seat). No `FultonInfo`, so no `watch` line. The host logged `vehicle_state step=skip;
  reason=partner_ride; id=25610` at once (VEHICLE_STATE stayed off the guest's ride). The seated guest's next
  `teleport` and `goto_vehicle` answered `warp_readback_timeout`, and the host's `placebehind` then stopped the run
  (`viewer landing lost its scene or valid partner pose`; Closed, BaselineRestored, RealSavesUnchanged). This is a
  scenario input failure, not a mod defect. The same coordinates showed the Fulton prompt in `0edadbe2` (frame
  `m4-req-before`, reached by `goto_vehicle jeep`); what hides it is [U]. The guest's lab boot spot beside
  veh_lv_0000 showed it in both `d407f115` and `0edadbe2` (frame `m4-start`, "hold E: Fulton 100%, x24").
- `e235bda6` (`vehicle-state-play.txt`, **play, `-WanSim bad`**, lab seed, 30010, build `d6f54f1`): VEHICLE_STATE's
  lines all passed [O]: `step=on; lrrp=6` on both, `sweep` -> `sweep_done; sent=13; pool=23`, the guest `same` for all
  26 received states; the guest drove J1 = 25608 (`vehicle;id=25608;...;localSeat=1`) -> host `skip;
  reason=partner_ride` -> after the get-off `resume` -> `tx; why=moved; d_mm=14235` -> guest `hold; reason=occupied`
  (D-Dog still seated) and then every later state within 2 mm of the guest's own pose; the host's shots and the
  guards' fire -> 10 `tx; why=life` (2300 -> 592) -> each `applied; moved=0; ... life=` on the guest 115..301 ms
  later (median 127 ms). The host's C4 did not destroy the jeep (no `VehicleBroken`; life stayed 592), so the play
  wreck lines never came. Shutdown reported `Forced=True` (as `59cfc415` and `de949f80` did). Frames
  `play-left` show the guest beside the jeep on the guest's screen only (the host stood at the 30010 ledge, out of
  sight); `play-damaged` shows the parked jeep on the host's screen.
- `e44516d7` (`vehicle-gone-play.txt`, **play, `-WanSim bad`**, `africa-jeep` seed, build `2464f4e` plus the
  scenario): **each player's own Fulton by hand passed, both directions; every expectation met** [O]. Guest:
  teleported to its lab boot spot beside veh_lv_0000 (25608), frame `gp-guest-before` shows the stock prompt "hold E:
  Fulton 100%, x24"; the E hold -> `obs; m=FultonInfo; id=25608; att=local` -> `watch; m=FultonInfo; cause=1;
  att=local` -> `lost` 3.0 s later -> `req; cause=1` -> host `rx_req` -> `applied; id=25608; via=locator; src=req;
  ok=1` (132 ms after the request) -> guest `req_done; result=gone; ms=250`. Frames: `gp-guest-hold` (the guest
  kneels at the jeep), `gp-guest-lift-01` (the balloon lifts it; "Fulton Recovery Expenses: [GMP -10000]" on the
  guest's copy), `gp-guest-after` on both screens (no jeep; the host's `gp-guest-before` / `gp-guest-after` show it
  there and then gone). Host: `goto_vehicle jeep` to the door spot of veh_lv_0002 (25610), frame `gp-host-before`
  shows the same prompt; the E hold -> `obs; m=Fulton; att=none` and `m=FultonInfo; att=local` -> `watch; m=Fulton;
  cause=1` -> VEHICLE_STATE `skip; reason=own_watch` -> `lost` 3.0 s later -> `tx; cause=1; actor=0` -> guest `rx` ->
  `applied; id=25610; via=locator; src=rx; ok=1` 116 ms later; frames `gp-host-lift-02` (the jeep in the air on the
  host's screen) and `gp-host-after` on both (gone). Both copies end with `gone=25608,25610`. VEHICLE_STATE ran
  alongside (`sweep_done; sent=14`, 54 refreshes, 69 `same` on the guest, no `applied`). **K27** [O]: the host's
  first `clear_enemies` ran inside the pairing sweep (PAIRED 158995745 -> `sweep_done` 159000617); its four kills
  committed at 158998978..159011 (`cause=8`, align `discovered`) and applied on the guest at 158999300..341
  (`src=resync`, `result=ok`): 322..330 ms. Eight kills outside the sweep: commit -> `apply; src=outcome` 299 ms.
  Lab without WanSim (`46b8fc12`): 215..230 ms. The sweep (14 reliable ops in about 5 s) adds no visible delay [I].
  Shutdown `Forced=True` again. Which spots offer the vehicle Fulton prompt: the guest's lab boot spot (three runs)
  and the `goto_vehicle` door spot of 25610 (two runs) did; a teleport to those door-spot coordinates did not
  (`46b8fc12`) [O]; why is [U].
- `d9b48fcb` (`vehicle-state-play.txt` without its C4 part, **play, `-WanSim bad`**, lab seed, build `7a33ffa`): every
  expectation met, `Forced=False` [O]. Sweep `sent=13; pool=23` and 26 `same`; the guest drove J1 = 25608 ->
  host `skip; reason=partner_ride` -> `resume` -> `tx; why=moved; d_mm=13969` -> guest `hold; reason=occupied` (D-Dog);
  frame `play-left-both`: the jeep where the guest left it in Wialo Village on both screens (the host 7 m behind the
  guest), `vehicle_state_status 25608` 577238,318956,1171541 (host) and 577229,318958,1171543 (guest): 9 mm apart, yaw
  equal. The host's shots -> `tx; why=life; life=2270` -> guest `applied; moved=0; d_mm=9; life=2270; was=2300` 125 ms
  later; both copies then read life 2270 (frames `play-damaged`). The guest's state word stays 3 against the host's 1
  after its drive (bit 2; not written, no write exists) [O].
- `5179b50b` (`vehicle-gone-player.txt` revised, lab, build `57d9195`). **The guest's Fulton by hand of 25610 from its
  `goto_vehicle` door spot passed** [O]: `watch; m=FultonInfo; att=local` -> `lost` 3.2 s later -> `req` -> host
  `applied; id=25610; via=locator; src=req; ok=1` -> `req_done; ms=66`; frames `pf-hold`, `pf-lift-*`, `pf-after` on
  both screens. **The guest's own destruction was not requested** [O]: its five rounds from the boot spot hit
  veh_lv_0000 (life 2260 -> 2230), its lever break 2 s later posted `obs; m=VehicleBroken; att=none`, and no `watch`
  started (no local hit recorded), so the guest's copy holds a wreck (frames `gw-*`) and the host's jeep stays intact.
- `35ba7929` (`vehicle-damage-census.txt`, lab, build `02d1e95`, the WS4 attacker census on): **the game posts no
  GameObject `Damage` message for a vehicle hit by gunfire** [O]. The census logged the four soldiers' `Damage`
  (a1=79, a2=65535, a3=4) on both copies but nothing for 25608, although the guest's rounds took its life from 2260 to
  2230. VehicleBroken carries only `(id, Start|End)` (`TppHero.lua:248`, `TppEnemy.lua:2072`, extracted) [O]. So the
  0.13.6 rule "VehicleBroken within 10 s of a local Damage" never attributes a guest's destruction by gunfire [O];
  explosives are untested [U]. An attribution that does not rely on that message (a native hook on the vehicle damage
  path, or the local player's own fire near the vehicle) is the next step; until then the guest's destruction stays a
  split, as in 0.13.6 (`7561ab63`) [O].

Decision (E5 pass rule, D7 not needed): **E5 passed for removals the host decides and for Fultons** [O]. The hold under
a seated rider (`92720b75` C), the engine's Fulton (`075c1ea4`, `92720b75` D) and each player's own Fulton by hand
(`e44516d7` in play over `-WanSim bad`; the guest's also in the lab, `5179b50b`) remove the vehicle on both copies. A
destroyed vehicle can be wrecked on the receiver with `SetBodyLife{life=0}` instead of Despawn (`0edadbe2`, lab, a
lever-made destruction on the host); a Fulton stays a Despawn (there is no balloon on the receiver). A destruction on
the guest's copy is not requested (`35ba7929`, above), which leaves today's split. The play defaults of VEHICLE_GONE
and VEHICLE_STATE flipped on (`482eb74`; section 9 M4). **Amended after the M4 review (below):** the wreck rode on
VEHICLE_STATE's key and so reached play with no play rehearsal of a destruction and an unmeasured blast on the
receiver; it now has its own key `world_vehicle_wreck`, off in lab and play unless named, so in play a destroyed
vehicle is a Despawn on the receiver; and both play defaults cover 30010 and 30020 only (Mother Base is lab only).

**M4 review (2026-10-29, verdict fix_first) and fixes (`c423b53`, `4bf9613`, `0dfac60`, `c98ad98`, `57fc2f7`).** An
independent review of `243e1a8` found four major and six minor problems; each was checked against the code and the
sliced run logs before it was fixed, and all held [O]:
- *Index 0 after the wreck:* `V.remove` returned `wreck` before the `id % 512 == 0` refusal, so a destroyed 25600 (the
  relief vehicle) was wrecked on the receiver (rx and req; the reviewer's fixture probe). Index 0 is now refused first.
- *Wreck parity in play without a rehearsal:* it rode on `W.vehicleStateOn`, which `482eb74` turned on in play, while
  its only play attempt (`e235bda6`) never destroyed the jeep and its blast on the receiver (it knocked the guest down
  in `5179b50b`) was never measured with live guards. It now has its own key `world_vehicle_wreck` (CFG r[15] bit 4),
  off in lab and play unless named; in play a destroyed vehicle is a Despawn on the receiver. A wreck that does not
  take falls back to the Despawn (`step=fallback`).
- *The play flip:* Mother Base was in both modules' missions with no 30050 run (and the never-run index Despawn); both
  modules are now lab only there (`step=off; reason=mb_lab_only` in play), and a named VEHICLE_STATE match must lie
  within 40 m (`far`). The rehearsals had named the keys; `4bbbfde4` (below) names none.
- *Body life downwards only:* guest-only hits added up between states (`e235bda6`: the guest's copy 102 below the
  host's), so its copy would reach 0 first, a split nothing repairs. Life is now host-authoritative both ways, and a
  host life of 0 is written as 1 (destruction is VEHICLE_GONE's).
- Minor: a failed placement now logs `refused; place_failed` (it logged `same`); `player_near` also holds when the
  local player is within 3.5 m of the vehicle's current spot; waitlog read from the session start, so several
  post-event expectations were met by earlier lines: the worker's new `logmark` step, a line per host change (`f=1`)
  and after each hold; the roadmap's `[x]` for pushed vehicles is `[~]` (a lab-lever push, then framed on the guest's
  screen only; `3585869d` frames it on both, still with the lever); the README no longer promises a destruction parity that play had not shown.

Claims corrected [O]: `d9b48fcb` showed VEHICLE_STATE's non-interference with a drive and leave (the guest's stats
`applied=1; moved=0`, the one apply a life), not a pose it applied: the shared left pose is RIDE_FAST's last
placement. "Every expectation met" in `d9b48fcb`, `e235bda6` and `0edadbe2` was log-reviewed, not expectation-proven,
for the post-drive lines: the guest's `(same|applied)` matched the pre-drive sweep line (`t_ms=160308032`,
`151330449`) and `0edadbe2`'s post-drive host `tx` matched the push tx (`seq=16`). A host-side destruction becoming a
wreck on the guest's copy was shown only with the lab lever (`0edadbe2`).

Hidden runs after the fixes (lab or play as stated; copied saves, research build, gun stage, low graphics; each
Closed, BaselineRestored, RealSavesUnchanged, no `step=error`, `viewer_changed` or fault line):
- `72366eb5` (`vehicle-state.txt`, lab, `africa-jeep`, build `0dfac60`; 8 expectation failures, all after the drive).
  `step=on; wreck=1` on both copies. The push (`tx; d_mm=3000` -> guest `applied; moved=1; err_mm=0` 16 ms later),
  the guest push put back by the refresh (`f=5`) and **the near-player hold** passed [O]: with the guest at the jeep's
  door the host's 2 m push gave guest `hold; id=25608; here=25608; reason=player_near` 32 ms after the host's `tx`, and
  `applied; moved=1; d_mm=2000` once the guest was moved clear, 2.2 s later (frames `m4-near-held` on the guest's
  screen: the guest at the door; the host's camera faced away). That push had left the jeep against the watchtower:
  on the guest's drive its copy broke (`obs; m=VehicleBroken; att=none` on the guest's copy only; frame `m4-stopped`:
  the guest on foot, no jeep) while the host's stayed intact, the expected split for a guest-side destruction (no
  request). Every later part found no jeep on the guest's copy (refused `absent`, `vehicle_not_alive`, the host's
  wreck answered `absent; no_instance`). The scenario now pushes back towards the boot spot (`57fc2f7`).
- `4bbbfde4` (`vehicle-state-play.txt`, **play, `-WanSim bad`, no vehicle key named**, lab seed, build `c98ad98`;
  **every expectation met**, 0 failures, each post-event one after a `logmark`; shutdown `Forced=True` as in
  `e235bda6` and `e44516d7`). `vehicle_state` and `vehicle_gone` `step=on` on both copies with `wreck=0`, sweep
  `sent=13`. Drive and leave: `skip; partner_ride` -> `resume` -> `tx; why=moved; d_mm=13969` -> guest `hold;
  occupied` (D-Dog) -> `same; d_mm=0; f=1`, readbacks 9 mm apart, frame `play-left-both` (the jeep where the guest left
  it on both screens). The host's shots: `tx; why=life; life=2270` -> guest `applied; life=2270; was=2300` 151 ms
  later. **The guest's own shots** took its copy alone to 1602; the host's refresh put it back (`applied; moved=0;
  life=2270; was=1602; f=5`; two more host changes before the C4 went off, life 2194 and 2116 from a cause not
  identified, met the guest's copy below them and raised it again, `was=1979`, `was=2023`).
  **A real destruction in play**: the host's C4 -> `watch; m=VehicleBroken; cause=2; att=none` -> `lost` -> `tx;
  cause=2` -> guest `rx` -> `applied; id=25608; via=locator; src=rx; ok=1` 132 ms after the tx; readbacks: host
  `alive=0; life=0` (the wreck), guest `alive=0; pos=0,0,0` (no instance); frames `play-c4-placed` (the jeep on both
  screens), `play-destroyed` (smoke and the burning wreck on the host's screen; the guest's camera had turned away
  from the spot).
- `3585869d` (`vehicle-state.txt`, lab, `africa-jeep`, build `57fc2f7`, the Lua of `c98ad98`; **every expectation
  met**, 36, each post-event one after a `logmark`; `Forced=False`). `step=on; wreck=1` on both copies, sweep
  `sent=14`, the guest `same` for 25608 at the start. **The lever push framed on both screens** [O]: `tx; d_mm=3000`
  -> guest `applied; moved=1; err_mm=0` 17 ms later, readbacks 2 mm apart; frames `m4-before-push` / `m4-pushed` (the
  jeep moves 3 m on the host's screen and on the guest's). The guest's own push (`m4-guest-pushed`: moved on the
  guest's screen only) put back by the refresh 7.8 s later (`applied; moved=1; d_mm=2997; f=5`, `m4-corrected`: the
  same spot on both again). The near-player hold: with the guest at the jeep's door the host's 2 m push gave guest
  `hold; reason=player_near` 32 ms after the tx (`m4-near-held`: moved on the host's screen, unmoved beside the
  guest on the guest's) and `applied; moved=1; d_mm=2000` 2.0 s later once the guest stood clear (`m4-near-applied`,
  the same spot on both), readbacks 2 mm apart. The guest's drive and leave: `skip; partner_ride` -> `resume` -> `tx;
  why=moved; d_mm=25750` -> guest `hold; occupied` -> `same; d_mm=1; f=1` (readbacks 1 mm apart; frame `m4-left` on
  the guest's screen, the host's camera faced the jeep's old spot). Body life: the host's shots `tx; why=life;
  life=2270` -> guest `applied; life=2270; was=2300` 33 ms later; the guest-only lever drop to 1500 put back by the
  refresh 8.5 s later (`life=2270; was=1500; f=5`); the host's lever 200 -> guest `life=200`. The wreck with the key
  named: the host's lever `vehicle_life 25608 0` -> `watch; m=VehicleBroken; cause=2` -> `tx` -> guest `applied;
  via=wreck; src=rx; ok=1` 149 ms after the tx; frames `m4-wreck-01` / `-03` (fire and smoke at the jeep on both
  screens, godmode on); readbacks `alive=0; life=0` on both, 2 mm apart.
- `e20f76bc` (`vehicle-state-play-host.txt`, play, build `7d82def`): stopped at its first `placebehind` (`viewer landing
  starts without a valid partner pose`: only the host had made the 800 m warp to the jeep); both players now go to the
  jeep first (`5b7522a`).
- `b31a1b65` (`vehicle-state-play-host.txt`, **play, `-WanSim bad`, no vehicle key named**, lab seed, build `5b7522a`;
  **every expectation met**, `Forced=False`). The host boards J1 and dives out with the throttle held: host `skip;
  reason=local_rider` (the first in any host log) -> `skip; reason=occupied` (D-Dog stayed in) -> `resume` 3.9 s after
  the host's get-off -> `tx; why=moved; d_mm=18590` -> guest `applied; id=25608; from=25608; moved=1; d_mm=7970;
  err_mm=0; dyaw=258` 334 ms later [O]: the guest's copy had stopped where RIDE_FAST last put it and the host's rolled
  8 m on, so this is a real divergence corrected in play, the first `moved=1` in any play run. Readbacks 46 mm apart
  afterwards; frame `play-bail-both` (the jeep at the same spot by the same building on both screens; the guest's 18 m
  spot faced a wall during the bail, so the divergence itself is logs only). The C4 part: `play-c4-placed` (the jeep
  behind sandbags on the guest's screen) -> host `watch; m=VehicleBroken; cause=2` -> `tx` -> guest `applied;
  via=locator; src=rx; ok=1` 151 ms after the tx -> `play-destroyed` / `play-destroyed-after` (the burning, overturned
  wreck on the host's screen, nothing at that spot and no explosion on the guest's) [O].

## 9. Milestones

Ordered by value and risk. Each milestone ships behind a lab key first. Before its play default flips: a hidden-worker
FRAME shows it (project rule) **and** a `-Mode play -WanSim bad` rehearsal passes the same lines (as `5a044a4d` did for
mounts). M1, M2 and M3 also need a two-PC Steam session that records the same lines on both PCs before they count as
done (the last two-PC session ran 0.13.6). Log lines marked *(new)* are added by the milestone; the others exist.

**Seeds.** Existing: `afgh-ledge` (30010 828.18 338.20 1630.51 95), `africa-jeep` (30020 -656.01 1.20 877.73 62),
`mb-ladder` (30050 22.61 1.80 -15.55 0), `quiet-recruited` (30050, Quiet unlocked); the lab saves deploy D-Dog. New
seeds, made by a new lab-only lever `debug seed_buddy <horse|walker|quiet>` that sets `vars.buddyType` and default look
vars on the copied save and takes a checkpoint, then `tools/worker/Save-WorkerSeed.ps1`; every new seed is checked by
booting it and reading `buddy_status` `vars_type=` (not by `run horse_spawn`, whose CallBuddy path writes no var,
`debug_buddy.lua:70, 208-212`):
- `afgh-horse`, `afgh-walker`: from `afgh-ledge` (the walker seed needs D-Walker developed in the lab save [U]);
- `afgh-quiet`: from `quiet-recruited`, after host-led travel to 30010;
- `afgh-relief-lv`: per E2 (a lever once the selection var is known; before that a scripted sortie-prep selection, or a
  co-op save the user shares, D10);
- `mb-quiet-cell`: a spot beside the Quiet cell, captured in E20.

### M1. The host owns the companion (seeds, refusals, Quiet lock)

- Scope: F1, F12 attribution. Buddy keys in the follow seeds; D8 save policy; guest-side reconcile; guest request
  refusal on every path E4 found, with toasts; CHARACTER op and the host Quiet lock before `sendSeeds`; MB cell Quiet
  hide; F9 summon host-only; id-aware `attClass` with its fixture test.
- Depends on: E1, E3 (save part), E4, E20 (MB Quiet part).
- Proof `companion-host-owned.txt` (`labsetting companion_host_owned=1`):
  - Run A (dog): `-HostSaveSeed afgh-ledge` (D-Dog), `-GuestSaveSeed afgh-horse`; `boot 30010 828.18 338.20 1630.51 95`.
    `waitlog host 'world_lua=event=companion; role=host; step=seed; kind=dog;'` *(new)*;
    `waitlog guest 'world_lua=event=companion; role=guest; step=loaded; kind=dog; source=seed;'` *(new)*;
    `both run buddy_status` -> `expect kdog` and `vars_type=2` on both; `both capture m1-dog`. The guest uses the
    user's path found in E4 (iDroid deploy) -> `waitlog guest 'world_lua=event=companion; role=guest; step=refused;
    path='` *(new)*, `guest capture m1-refused-toast`; then `guest run horse_spawn` -> `refused` (debug lever guard).
  - Run B (Quiet): `-HostSaveSeed afgh-quiet`, guest character `quiet`. `waitlog host 'world_lua=event=companion;
    role=host; step=quiet_lock; via=disable_type; guest_char=quiet;'` *(new)*; `host capture m1-quiet-menu` (the
    buddy list without Quiet, if E4 found a way to open it in the worker; else the toast); `both capture m1-quiet-field`
    (no buddy Quiet on either screen; the guest's Quiet body only); the guest's `character=` report line (`native/character_receipt.cpp`) shows Quiet
    applied, not the `buddy_quiet` fallback. Late-join variant: the guest joins after the host's Quiet is out ->
    `step=quiet_lock; via=delete_buddy;`.
  - Run C (Mother Base): `boot 30050 22.61 1.80 -15.55 0`; `waitlog guest '...step=loaded; kind=dog; source=seed;'`;
    `both capture m1-mb-dog`. With guest character `quiet` at `mb-quiet-cell`: `waitlog host
    'world_lua=event=companion; role=host; step=mbquiet_hidden;'` *(new)*, `both capture m1-mb-quiet-cell`.
  - Rehearsal: Runs A and B with `-Mode play -WanSim bad` (`title`, `waitgameplay`). Exit: a two-PC session.

### M2. D-Dog synced and following the host (stage 1)

- Scope: F2 and F12's dog row. Never release to the guest's AI; own-dog read without body 1 on both roles; COMPANION
  at 10 Hz; late-join reconcile; doc drift fix. Stage 2 (F2b: the attack shown at the right guard, the dog's marks on
  the guest) is a named follow-up after enemy sync in play and "Shared marking".
- Depends on: M1, E3, E19.
- Proof `companion-dog.txt`: `boot 30010 ...`; host walks 60 m (`hold w`) and runs 100 m (`burst`).
  - Walking: `waitlog guest 'world_lua=event=buddy; role=guest; result=target_submitted;
    error_cm=([0-9]{1,2}|[12][0-9]{2}|300)([^0-9]|$)'` (R4's 3 m); running: the same with
    `([0-9]{1,2}|[1-4][0-9]{2}|500)` (5 m). After M2 the line moves to COMPANION_ACK with the same fields.
  - `both capture m2-dog-walk`, `both capture m2-dog-run`: the dog behind the host on both screens.
  - `placebehind guest host 40 0`: `both capture m2-dog-guest-away` (the dog stays with the host).
  - Host hidden on the guest's copy: the host boards the support heli (body 1 hidden by acts 97..108) -> no
    `dog_unreadable` on the guest for 30 s; `guest capture m2-dog-host-in-heli`. What the host's dog does when the host
    boards is [U].
  - Engagement: no `clear_enemies`; a guard within 3 m of the mirror for 120 s -> no `mirror_engaged` line in the guest's log
    (post-run check); the host's dog takes down a guard -> `waitlog host 'world_lua=event=commit; ent=.*;
    actor=host;'` and `waitlog guest 'world_lua=event=apply; ent=.*; src=host;'` for the same `ent`, no
    `event=divergence` for it.
  - Repeat in 30050 (`mb-ladder`, `both capture m2-mb-dog`) and the `-Mode play -WanSim bad` rehearsal.
  - Exit: a two-PC session.

### M3. The sortie vehicle on both copies

- Scope: F3 route A (with E17 and E18) or route B; guest vehicle requests refused.
- Depends on: E2, E17, E18, M1 (seeds), E4.
- Proof `companion-relief.txt`: host seed `afgh-relief-lv`; `boot 40010`, then `host worldtrigger goto=30010
  goto_lz=1` (a real LZ, so the relief vehicle is placed at a drop point; `boot` with coordinates uses route 0, which
  may place none [U]); a census of 25600 on both (`both run vehicle_seats`) -> alive on both within 1 m; `both capture
  m3-relief-arrival`. Host drives it 100 m -> `waitlog guest 'partner_ride_seat=confirmed; vehicle=25600;'`, never
  `reason=absent`; `both capture m3-relief-drive`. The guest's iDroid vehicle request -> `step=refused; path=` *(new)*,
  `guest capture m3-request-refused`. Removal: host Fultons it -> `waitlog guest 'world_lua=event=vehicle_gone;
  step=applied; id=25600;'` (or the E17 method's line), `both capture m3-relief-gone`. Mission end: `waitlog guest
  'world_lua=event=relief; step=seize_skipped;'` *(new)*, resources diffed. If route B is used, repeat with a host seed
  that chose the other faction's jeep (`afgh-relief-west`) and record the expected `absent` on the guest.
- Rehearsal `-Mode play -WanSim bad`; exit: a two-PC session.

### M4. Vehicle losses, damage and parked vehicles

- Scope: F4. VEHICLE_GONE on in play; VEHICLE_STATE with the skips of 6.4; damage parity where a read exists; wreck
  parity if E5 allows.
- Depends on: E5.
- Proof: `coop-vehicle-gone.txt` parts A-D (existing lines `world_lua=event=vehicle_gone; step=applied;`, frames
  `gone-a-before` / `gone-a-after`, `fulton-after`); new `vehicle-state.txt` (`-SaveSeed africa-jeep`, `boot 30020
  -656.01 1.20 877.73 62`):
  - Push lever: a new lab lever `debug vehicle_push <id> <dx> <dz>` *(new)* moves the Masa Village jeep 3 m on the host's
    copy only (a reproducible stand-in for a shove) -> `waitlog guest 'world_lua=event=vehicle_state; step=applied;
    id=25608;'` *(new)* within 2 s, error under 1 m; `both capture m4-pushed`.
  - The guest drives the jeep away and leaves it -> `waitlog host 'world_lua=event=vehicle_state; step=skip;
    reason=partner_ride;'` *(new)* while it drives (no VEHICLE_STATE against RIDE_FAST); afterwards the host's copy keeps
    the guest's final pose; `both capture m4-left`.
  - Damage: the host shoots the parked jeep -> `step=applied; ... life=` on the guest; `both capture m4-damaged`.
  - Under `-WanSim bad`: a kill during the pairing sweep -> the gap between host `event=commit` and guest `event=apply`
    for that `ent` is recorded (K27).
- Rehearsal `-Mode play -WanSim bad` before VEHICLE_GONE's play default flips.
- **Status (2026-10-29, branch `wp/vehicle-state`, after the M4 review): built and proven in hidden runs, lab and
  play; VEHICLE_GONE and VEHICLE_STATE on by default in play at 30010 and 30020 (`482eb74`, narrowed by `c423b53`);
  Mother Base and the wreck parity lab only; not two-PC tested.** Details and run ids in section 8.1 and
  `docs/STATUS.md` (M4, M4 review fixes).
  - Play default [O]: the project rule (a FRAME on both copies and a passing `-Mode play -WanSim bad` rehearsal) is
    now met for every path that acts in play at 30010 and 30020 with no vehicle key named: VEHICLE_GONE's Fultons
    (`e44516d7`) and its destruction as a Despawn (`b31a1b65`, frames on both; `4bbbfde4`); VEHICLE_STATE's pose
    (frames on both in the lab, `3585869d`; in play `b31a1b65`, `moved=1`, the after-frame on both) and body life both
    ways (frames `3585869d`; play `4bbbfde4`, `e235bda6`, `d9b48fcb`). Its holds acted in play as `occupied`,
    `local_rider` and `partner_ride`; `player_near` and VEHICLE_GONE's seated-rider hold are lab-proven only
    (`3585869d`, `92720b75`). The wreck parity has its own key `world_vehicle_wreck` (off in lab and play unless
    named) and Mother Base is lab only (no hidden run covered 30050), so neither is in the play default.
  - VEHICLE_GONE [O]: `coop-vehicle-gone.txt` A, C (the hold while the guest sits in the jeep) and D in `92720b75`
    (B in `075c1ea4`); each player's own Fulton by hand in play over `-WanSim bad` (`e44516d7`, frames on both
    screens) and the guest's in the lab (`5179b50b`); **a real destruction in play** (`4bbbfde4`, `b31a1b65`: the
    host's C4 -> `watch; m=VehicleBroken; cause=2` -> `tx` -> guest `applied; via=locator; src=rx; ok=1` 132 / 151 ms
    later; readbacks `alive=0` on both copies, the guest's with no instance; `b31a1b65` frames `play-c4-placed` ->
    `play-destroyed-after`: the burning wreck on the host's screen, nothing at that spot on the guest's); the
    receiver's wreck with the key named (`0edadbe2`, `3585869d`, lever-made, frames `m4-wreck-*` on both). Index 0
    is refused before any removal form (fixture tests; no relief vehicle in a hidden run). Not working: a jeep the
    guest destroys is not requested, because the game posts no `Damage` message for a vehicle hit (`5179b50b`,
    `35ba7929`); run `72366eb5` shows the resulting split in the lab (the guest's copy broke on a drive, the host's
    stayed intact) [O].
  - VEHICLE_STATE [O]: the push lever, framed on both screens (`3585869d`; earlier `d407f115`, `0edadbe2`,
    `72366eb5` on the guest's screen), and the host's correction of a guest push; the near-player hold (`72366eb5`,
    `3585869d`: the guest at the jeep's door, the host's push -> guest `hold; reason=player_near`, applied 2.0..2.2 s
    later once the guest stood clear); drive and leave with no state against RIDE_FAST (`skip; reason=partner_ride`
    -> `resume` -> `tx; why=moved` -> guest `hold; occupied` -> `same; f=1`) in play (`4bbbfde4`, after a logmark;
    `d9b48fcb`, log-reviewed) and the lab (`3585869d`): VEHICLE_STATE keeps out of the way and then confirms the pose
    that RIDE_FAST's last placement left (readbacks 1..9 mm apart; frame `play-left-both`); **the host's bail in
    play** (`b31a1b65`): the host dives out of the rolling jeep, its copy rolls 8 m past where the guest's stopped
    (RIDE_FAST's last placement), and the host's `skip; local_rider` -> `skip; occupied` (D-Dog) -> `resume` -> `tx;
    why=moved` gives guest `applied; moved=1; d_mm=7970; err_mm=0` 334 ms later, the first pose VEHICLE_STATE applied
    in play (frame `play-bail-both`: the same spot on both screens; readbacks 46 mm apart); body life both ways in
    play (`4bbbfde4`: the host's shots `life=2270; was=2300` 151 ms after the host's change; the guest's own shots took
    its copy to 1602, and the host's refresh put it back, `life=2270; was=1602; f=5`).
  - Damage parity is the body life only [O]. Wheel and part lives are read and sent (the zero mask) but not written:
    the 21-hash instance dispatcher has no wheel or burning write (3.6) [O]. Fire is not a function of the life word
    (life 200 shows none, `d407f115`) [O], so burning is not synced [I]; what a life raise does on a copy that burns
    is [U].
  - K27 [O]: kills inside the pairing sweep, `-WanSim bad`: 322..330 ms commit -> apply via `src=resync`; outside it
    299 ms via `src=outcome` (`e44516d7`): different delivery paths, so "no visible delay" stays [I].
  - Open: a two-PC session; a destruction the guest causes stays on the guest's copy (no attribution, above) [O]; the
    wreck parity in play needs a play rehearsal of a real destruction and a lab run with live guards and godmode off
    that shows no extra guest-side kills or chain wrecks (its blast knocked the guest down in `5179b50b`) [U], and its
    fallback on a far, unrealized receiver copy has not run [U]; Mother Base needs a 30050 hidden run (the cluster
    jeeps by name, the index Despawn) [U]; a physical shove (player or vehicle collision) was never tested, only the
    lever and the host's bail [U]; the guest's bail out of a rolling jeep would bring its jeep back to where the
    host's copy stopped once the guest stands 3.5 m clear [I], not run; change -> apply latency against R7's 2 s is
    measured only from tx (16..334 ms; the scan adds up to about 1.6 s, and a seated D-Dog holds the host's send: 4.2 s
    from the host's get-off to the guest's apply in `b31a1b65`) [I]; Fulton rewards against save resources [U]; why a
    teleport beside a jeep hides the Fulton prompt [U]; AI-driven, LRRP and reinforcement vehicles are M8; the relief
    vehicle (index 0) is M3 / E17.

### M5. Quiet synced

- Scope: F5 and F12's Quiet row (the lock is in M1). Same two stages as the dog.
- Depends on: M1, E15, E3, E19.
- Proof `companion-quiet.txt`: host seed `afgh-quiet`, guest character `ocelot`; `boot 30010 ...`; `waitlog guest
  'world_lua=event=companion; role=guest; step=hold; kind=quiet; error_cm='` *(new)* under 500; `both capture
  m5-quiet-follow`. The host orders her to a snipe point -> `both capture m5-quiet-snipe` (cloak and point on both).
  A guard she shoots: `waitlog host 'world_lua=event=commit; ent=.*; actor=host;'`, `waitlog guest
  'world_lua=event=apply; ent=.*; src=host;'`, no `mirror_engaged` line in the guest's log (post-run check). Rehearsal
  `-Mode play -WanSim bad`.

### M6. More passenger seats

- Scope: F6: light vehicle front seat, truck cab, truck bed; APC/tank crew seat as a stretch.
- Depends on: E6, E7; SEAT op.
- Proof: extend `coop-vehicle-passenger.txt` (`afgh-ledge` for trucks, `africa-jeep` for jeeps): for each seat and both
  roles, `waitlog <passenger> 'partner_ride=local_passenger; vehicle=<id>; seat=<n>; state=on;'` and `waitlog <driver>
  'partner_ride_seat=confirmed; vehicle=<id>; seat=<n>;'`, `both capture m6-<vehicle>-<seat>-drive`, stock get-off
  (`exit; reason=foot; via=getoff`); a seat race (both press E on the same seat within 100 ms) ->
  `world_lua=event=seat; action=deny;` *(new)* on one copy, one seated and the other on foot (`both capture
  m6-race`). Rehearsal `-Mode play -WanSim bad`.

### M7. Shooting as a passenger

- Scope: F7, light vehicle back seats first, then the front seat and the truck bed if E7 shows a usable pose.
- Depends on: E8, E9, M6 for the extra seats.
- Proof: `coop-vehicle-seat-aim.txt` with `-SaveSeed africa-jeep`: the passenger aims and kills a guard while the host
  drives -> `waitlog guest 'world_lua=event=req; rid=.*; ent='`, `waitlog host 'world_lua=event=commit; ent=.*;
  actor=guest;'`; `guest capture m7-seat-aim` (gun raised), `host capture m7-partner-gun` (the partner's gun raised in
  the seat); no `reason=convention`. Rehearsal `-Mode play -WanSim bad`.

### M8. AI-driven, reinforcement and side-op vehicles

- Scope: F8.
- Depends on: E10 and the enemy position sync reaching play (roadmap "Sync enemies"); side-op armor also on side-op
  sync.
- Proof: 30020 LRRP truck scenario: an AI truck passes both players -> `waitlog guest 'world_lua=event=ai_vehicle;
  step=placed; id='` *(new)*, error under 2 m; `both capture m8-lrrp`; a reinforcement APC called by an alert ->
  `world_lua=event=reinforce; step=appear;` *(new)* on both, once; `both capture m8-reinforce`. Rehearsal
  `-Mode play -WanSim bad`.

### M9. A D-Walker each

- Scope: F9 (only if decision D2 keeps it), F12's walker row.
- Depends on: E14, E19.
- Proof: host seed `afgh-walker`; `boot 30010 ...`; each player mounts with one ACTION -> `<each> require localMounted=1` (the walker
  status line, `debug_buddy.lua:111`); `waitlog <each> 'world_lua=event=mount_fast; step=placed; kind=walker; slot=1;'` *(new)*;
  `both capture m9-walkers` (two walkers, each moving with its pilot); a walker kill -> `event=commit` once, and
  no `mirror_engaged` line on the other copy (post-run check). Rehearsal `-Mode play -WanSim bad`.

### M10. The shared D-Horse

- Scope: F10 (only after decision D4).
- Depends on: D4, E11 (blocking), M1, E19.
- Proof, both role orders: (a) host seed `afgh-horse` as the host; `boot 30010 ...`; the host rides 200 m ->
  `waitlog guest 'world_lua=event=mount_fast; step=placed; kind=horse; slot=0;'` *(new)*; `both capture
  m10-host-rides` (the horse and a drawn rider on the guest's screen). The host dismounts beside the guest; the guest
  presses E once -> `guest require localMounted=1` (the first E mounts, as in `partner-mount.txt` part 3);
  `both capture m10-guest-mounts`. (b) The guest rides 200 m -> the same line on the host;
  `both capture m10-guest-rides`; the guest dismounts beside the host, the host's first E mounts. Rehearsal
  `-Mode play -WanSim bad`.

### M11. Two on one horse

- Scope: F11.
- Depends on: M10, E12, E13, E16.
- Proof, both role orders: (a) the host rides, the guest presses E beside the horse and sits behind -> `waitlog host
  'world_lua=event=seat; action=grant; target=horse;'` *(new)*, `both capture m11-two-riders`; the guest shoots a target
  while the host gallops -> `waitlog host 'world_lua=event=commit; ent=.*; actor=guest;'`, `both capture
  m11-pillion-shot`; the host gets off and the guest gets off too. (b) The same with the guest steering and the host
  behind. Rehearsal `-Mode play -WanSim bad`.

### M12. Riding the support helicopter together (research, then design)

- Scope: E22; then a design for drawing both players in the heli (seats, body 1 DM in the heli instead of the acts
  97..108 hide). Gated on decision D6.

### M13. Vehicle weapons and mounted guns (later)

- Scope: driver-fired weapons (tank cannon and HMG, APC guns), mounted gun emplacements, the heli door gun: the shooter's
  copy decides hits (as on foot), the partner's view shows the turret. Gated on D6; design not started.

## 10. Decisions for the user

**Answered by the user on 2026-10-28:** D5 (a) one companion per session, the host's; D2 (a) both players get a
D-Walker when the host deploys D-Walker; D1 (a) the host alone gives companion orders, and the guest's attempts are
refused with a toast; D10 yes, the launcher's co-op save (never the real save) may be copied read-only into the seed
library when a proof needs a sortie vehicle or Quiet. **Defaults taken until the user says otherwise:** D3 a riding
partner stays hidden until M10; D4 (a) the 2026-10-22 rule is lifted for the shared horse only (it follows from the
user's 'ridden by both players at once'); D6 the support heli (M12), vehicle weapons (M13) and the host's in-mission
vehicle drop come later, enemy helicopters and walker gears stay in the enemy section; D7 is decided with the user once
E3/E15/E19 have run; D8 (b) the guest's own buddy vars are restored; D9 (b) M1 starts now, alongside the user's
two-PC check of 0.14.3.

The original questions:

- **D1. What may the guest do with the host's companion?** (a) Ride the horse and walk with the others, but give no
  orders: the host alone commands D-Dog, Quiet and the horse (recommended for v1; only promised if E4 finds a way to
  refuse the guest's own call-menu orders and horse call). (b) The guest's orders are forwarded and run on the host's
  copy (COMPANION_CMD, needs the call-menu path, E4).
- **D2. D-Walker for the guest.** (a) Both players get a walker only when the host deploys D-Walker (fits the engine:
  one buddy type per copy). (b) The guest always gets a walker, whatever the host's buddy (needs a second buddy block:
  new native work, high risk).
- **D3. Until the shared horse is drawn (M10):** keep a riding partner hidden (today), or show the rider floating at
  saddle height with no horse under them, like the vehicle `absent` fallback.
- **D4. May a copy's own horse be moved by the partner's riding?** Your rule of 2026-10-22 says the partner's stream
  never moves your own horse (it fixed horses being pulled away so the first E found none). A shared D-Horse that either
  player can ride needs that rule lifted for the shared horse, in both directions, with the handoff in 6.10: placement
  only while the partner rides, never while you stand within mount range or ride it. (a) Lift it for the shared horse
  only (recommended; M10 tests that the first E always mounts). (b) Keep the rule: then only the host rides the horse,
  and a guest on it stays hidden.
- **D5. "Only 1 of each companion":** (a) the session has one companion, the host's; the guest brings none
  (recommended; fits the engine's one buddy per PC). (b) Each player may bring a different companion (for example the
  host's D-Dog and the guest's D-Horse), never two of the same kind; each PC then needs the other player's companion as
  a mirror beside its own, which needs a second buddy block (new native work, high risk). Each PC can already hold a
  different kind locally (`87a5ce1e`), but neither shows the other's.
- **D6. Scope of "all vehicles" and "passengers" (1.1).** In this plan: authored, MB, relief, AI-driven, reinforcement
  and side-op vehicles; damage; seats in jeeps, trucks, the truck bed, APC/tank crew and the horse. Asked: (a) the
  support heli ride-together as M12 now, or later; (b) vehicle weapons and mounted guns (M13) now or later; (c) enemy
  helicopters and walker gears stay in the roadmap's enemy section; (d) the host's in-mission vehicle drop on both
  PCs: research now or later.
- **D7. If a mirror cannot be held inert (E3, E15, E19 fail):** (a) hide the mirror on the guest's screen (the host's
  companion is then visible only to the host) until enemy sync in play; (b) show it and accept that the guest sometimes
  sees it act on its own (results not synced); (c) delay the milestone until enemy sync in play.
- **D8. The guest's co-op save and the host's buddy:** (a) accept it (the guest's co-op save takes the host's buddy,
  look and relief choices); (b) the guest's own buddy vars are restored around each save write and at the end of the
  session (recommended); (c) restore them only at the end of the session.
- **D9. Order of work:** (a) run the two-PC check of 0.14.3 first, then M1 (recommended: the 2026-10-23 fixes are not yet
  tested on two PCs); (b) start M1 in parallel with that check.
- **D10. Seeds that need the sortie-prep menu** (a relief vehicle chosen, Quiet deployed): if the hidden worker cannot
  drive the menu, may the work use a co-op save you share (launcher Tools > Share save) as a seed? It would be copied
  into the seed library and never written back.

Things the user should know (not decisions): D-Dog "completely" has two stages (6.2): stage 1 (M2) gives one dog that
follows the host on both screens and synced results; the dog's attack at the right guard and its marks on the guest's
screen wait for enemy sync in play and shared marking. The same holds for Quiet's shots. The two-rider horse depends on
E11 (no known horse placement API yet). If E4 shows that the sortie-prep menu ignores the disable mask, Quiet
stays in the host's menu while the guest plays her; picking her then deploys no buddy and shows a toast.

## 11. Review log

Revision 2 answers the review of revision 1 (`a2ef9a2`). Each point was checked against the repo, the extracted
scripts and the exe before editing.

| Review point | Verdict | Check and change |
|---|---|---|
| Critical: mirror outcomes are not "overwritten by host outcomes"; moving them to `other` makes splits permanent | Accepted | `world_enemy.lua:296-305` only logs a divergence [O]. Rule 4 is now "mirrors never engage"; new class `mirror`, logged, never forwarded; F12 names a hold per companion; E3/E15 pass only on zero engagements; D7 if they fail. Kept `mirror` rather than `other` so the case is visible in logs |
| `attClass` counts any horse and any player walker as `local` | Accepted | `world_outcome.lua:30`, `world_phase.lua:28-29`, `Tpp.lua:311,313` [O]. Id-aware attribution in F12, applied in F9-F11, with a fixture test |
| F4 scan treats a guest-driven vehicle as parked; guest NPC drivers | Accepted | `partner_board_policy.hpp:16-24` (rider[] never written) [O]. F4 skips RIDE_FAST-latched and NPC-ridden ids on either copy; LRRP exclusion until M8 |
| Relief index 0 cannot be removed; double mission-end seize | Accepted | `0x140B304E0` rejects 0 (capstone) and `world_vehicle.lua:157` refuses `index0`; `TppMission.lua:1549-1583` seizes index 0 [O]. K19, K20, E17, E18; M3 proof covers removal and seize |
| RIDE_BODY reversal is a user decision | Accepted | `mounts.md:21-25, 59-85` [O]. D4 with a handoff rule; M10 checks the first E in both roles |
| "One of each companion" narrowed without asking | Accepted, evidence corrected | D5 added; R2 is now the recommendation. `87a5ce1e` shows each copy can hold a different kind locally, not a cross-copy display, so (b) still needs a second block |
| Guest orders and horse call not covered (D1 a) | Accepted | E4 extended to the call menu and horse call; D1 (a) depends on it; K22 |
| K1 mechanism wrong (W4 does not copy host vars) | Accepted | `world_recovery.lua:60-100` [O]. K1 rewritten; D8; E3 adds the locked-companion case; `ResetQuietEquipIfUndevelop` noted (`TppMain.lua:102-103`, `TppMission.lua:2336`) |
| D-Dog and Quiet "synced" before enemy sync in play | Accepted | `world_place_guest.lua:165` [O]. Two stages in R4, 6.2, 6.5; F2b follow-up (attack state, marking); the user is told |
| No risk for damage to mirrors | Accepted | K21, rule 5, E19 |
| Quiet: MB cell Quiet, menu removal, precedence race | Accepted | `f30050_sequence.lua:2004-2009` [O]; F1.5 order (character first, CHARACTER op, lock before `sendSeeds`); menu frame as R5's pass signal; E20; K25, K26 |
| Scope of "all vehicles" and passengers narrowed silently | Accepted | Section 1.1 table; side-op armor (`TppQuest.lua:10` [O]) in M8; M12 heli, M13 weapons; D6 |
| F11 v1 relied on the coop_carry receipt and a mask that blocks aim | Accepted | `game_script_observer.cpp:4459-4470`, `world_horse_cargo.lua:120, 315` [O]. v1 is Lua-only; mask from `PlayerPad` names; passenger pose in E13 |
| Proofs lack named lines, frames, play rehearsals, reverse roles, two-PC exits | Accepted | Every milestone names waitlog lines (new ones marked) and capture labels; `-Mode play -WanSim bad` before each play flip; M10/M11 both role orders; two-PC exit for M1-M3; the push uses a lab lever |
| M2 regex used `;` and allowed 999 cm | Accepted | Observer prints `; ` (`body-safety-sea-death.txt:62`) [O]; regex bounded to 300 / 500 cm |
| M1 proof used a debug lever and an unverified seed | Accepted, partly | `debug_buddy.lua:70, 208-212`, `world_buddy.lua:37` [O]. Proof uses the E4 user path; seeds made by a new lever and checked by `vars_type`. **Rejected:** seeding from the user's real save (project rule: real saves are never touched); replaced by D10 (a co-op save the user chooses to share) |
| Seat 1 also sent to the driver phase at `0x14125188E` | Accepted | Capstone read confirms `cmp al,2; setb al; add al,2; mov [rbx+0x55],al` [O]. F6, E6, K12 name both sites |
| Guest's own dog read also needs body 1 realized | Accepted | `game_script_observer.cpp:4015-4018` [O]. Lifted for both roles; M2 heli step |
| Truck bed anchored to seat 2/3 on a 2-seat truck | Accepted | `84e3db0e` `seats=2` [O]. Bed anchored to a CNP or offset; E6/E7 read geometry first |
| Truck row said the truck half never ran | Accepted | `84e3db0e` passed it (`fast=1`, `board_block; reason=type`) [O]. Row corrected |
| Seated aim called "never run" | Accepted | `637b47ea`, diag #7 [O]. Cited in 2, 3.7, F7; E8 starts from frame `aim-seated` and narrows to firing |
| `251abcef` and `mounts.md` section 7 step 4 not used | Accepted | Cited in 3.3, F10, E12; step 4 discussed in F10 and kept as E21 |
| Rider geometry mixed frames | Accepted | 3.3 lists the three sources; the offset is [U] until E12 |
| Ids "same list order" | Accepted | `veh_lv_0000`=25608, `veh_trc_0000`=25602 [O]; nearest-of-type check before any SetPosition |
| Budget undercounted; no `-WanSim bad` proof | Accepted | Recounted (still [I]); rate-limited sweep; K27; M4 measures commit-to-apply latency |
| Ghidra unavailable | Accepted | Confirmed again in this revision (WinError 10061) [O]. Header states the capstone method; no milestone blocked |
| Seat-aim tree is an uncommitted 0.13.5 tree | Accepted | `git status` there shows the changed files on `cc973a8` [O]. E8 is a port onto `wp/companions` with the contract regenerated |
| Roadmap: dropped and misstated lines | Accepted | See the roadmap: weapons and mounted guns, vehicle damage re-added; sortie vehicle `[ ]`; vehicle losses `[~]`; avatar qualified; tank overlap removed |
| Process note: two-PC check placed first without asking | Accepted | D9 |
