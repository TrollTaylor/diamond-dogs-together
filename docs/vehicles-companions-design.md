# Vehicles and companions: design

Status: revision 2 of the design (2026-10-28, after review; see section 11). M1 and M2 (D-Dog stage 1) are built on
`wp/companion-host` behind the lab key `companion_host_owned` (play default off; results in 8.1 and 8.2, status in
section 9); the rest is design. Branch
`wp/companions` from `dev/oct7-continue` `48ce7da` (0.14.3; package built at `a4c18fa`). Target exe: `mgsvtpp.exe`
1.0.15.4, SHA-256 `085c2f82...bb45`.

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
| Seated aim | The 2026-10-22 session's latch (diag #7) is fixed since 0.13.6: the sender runs no self-check while the local player rides (`aim_dir=refused; reason=ride; checks=0`, also in every M7 run). A stock back-seat passenger raises and fires its own gun (upper act 2, gun modes 6/7; fire upper 3 / mode 9; the magazine goes down), but its body faces into the jeep (seat 2 the right side, seat 3 the left) and its aim range is +-90 deg around that, and no seated shot was seen to land in 8 hidden runs (8.20). The partner's raised gun on the driver's screen (design A) does not show (8.20). | [O] runs `6fc3098b`, `30a6a21a`, `03ceff68`, `a3f7c4e1`, `d7cf5a4b` (8.20); `native/partner_driver.cpp` `SenderV6` |

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
  that it also swaps a loaded horse, walker or Quiet for another type is [I] (dog to horse and back: [O], 8.1). Native
  CallBuddy writes `vars.buddyType` [O since 8.1: the host's `vars_type` went 2 -> 1 after a swap; it was U].
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
| D-Dog `TppBuddyDog2` | 19, `9728` | Dispatcher `0x140C7D220` [O]: `LuaAiStayAndSnarl` (`0x24824B9A`), `GetPlant`, `SetWarpPosition` (`pos`), `GetPlacedItem`; scripts also send `SetMotherBaseCenterAndRadius` and `SetEnabled` (`TppDemo`) | `LuaAiStayAndSnarl` fields [O, 8.2]: `position`, `useMarker`, `look`, `dash`, `warp`, `ignoreMarker` -> vt+0x98 -> `0x140C7DA10`, which sets the persistent mode 1/2 (look given and `useMarker` false: the retail `s10040` "stop AI"; the Dog AI `0x140C7E650` skips its update for them) or 3/4 (`useMarker` true or no look), +1 with `dash`; `warp` places the dog at `position` facing `look`. **Held by mode 1/2 + `warp=1` at 10 Hz** (M2, runs `ffb2d7ab`, `4cd88ea2`); mode 1/2 without warp never moves the dog (run `8c0b425f`), mode 3/4 walks it there itself but lagged 7-15 m behind a moving host |
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
- **The chosen relief type is `vars.reliefVehicleCarryType`** (low byte 1..10; 0 none) with
  `vars.reliefVehicleCarryAttitude[0..3]` (x, y, z, rotY) [O, E2, section 8.4; resolves `unk_59943e57`]: the vehicle
  manager's SetRelief writes them at the sortie and `Vehicle.LoadCarry` reads them at the next mission start; the
  sortie loadout buffers do not hold the choice.
- **A mid-mission `Vehicle.LoadCarry()` spawns it** [O, section 8.5]: with the carry vars written, the call gives
  index 0 within 8 s, at the same pose on each copy, in a free-roam mission that started without it.
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
`0x103E010` refuses only on 0xC3/0xC2 [I]. Measured (8.20): the clear happens once the seat loop runs (`armed=1; c3=0`),
RMB gives upper act 2 / modes 6-7 and a shot upper act 3 / mode 9 with the standing upper-body set (`posture=0`) [O];
the seated body's root faces into the jeep (seat 2: the jeep's yaw - 90 deg, seat 3: + 90 deg) and the aim range is
+-90 deg around it [O]; no seated shot was seen to land [O, 8.20]. Retail also seats the
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
112-120; 29-31 are reserved [O, `lua/coop/world_sync.lua:4-10`, `native/world_wire.cpp`] (at design time; built
since and integrated on `dev/oct7-continue`: 48, 49, 51, 52, 56 and 57; on `wp/vehicle-fixes2` 58, VEHICLE_DAMAGE;
50 and 53-55 stay reserved). New codes start at 48. Every
new op gets a schema row in `native/world_wire.cpp` and a Lua handler in a new module (a world Lua chunk may be at
most 32000 bytes, `docs/STATUS.md:2017`) [O]. Both PCs must run the same version (`README.md` Troubleshooting) [O], so
a field change is a new op, never a resized old one.

| Code | Name | Dir | Wire | Rate | Fields | Replaces / reuses |
|---|---|---|---|---|---|---|
| 9-11 | TRANSITION / SEED / SEEDS_END | H->G | R | per travel | **New seed keys** (vars): `buddyType`, the look vars of 3.1, `initialBuddyStatus`; after E2 the relief selection var(s) and `reliefVehicleCarryType/Attitude/Life/Ammo/State`. Sent only after the Quiet lock (F1.4) | `world_follow_seed.lua` key list (`{name, dims, scale, true}` = vars) [O] |
| 48 | COMPANION | H->G (both for walker slot 1) | R (M1/M2: on a kind change and every 5 s, with the last streamed pose; the 10 Hz stream is op 57) | on change and every 5 s | seq, sender tick, kind 1 dog / 2 horse / 3 walker / 4 Quiet, slot (0 host's, 1 guest's walker), present, xyz mm, yaw mrad, state byte (dog: follow/stay/attack; Quiet: standby/snipe/cloak/down; horse: gait 0..3; walker: idle/drive; all: hurt/hospital/dead), rider bits (seat 0 role, seat 1 role), target id (attack target, 0 if none; used after enemy sync, F2b) | Supersedes BUDDY_STATE 39 / ACK 40 (same idea, more fields) |
| 49 | COMPANION_ACK | G->H | R | 1 Hz | seq, kind, result (0 holding at the host's pose, 1 loading or unreadable, 2 refused or another kind, 3 stale (held at the last pose), 5 held where it stands), error cm | Same as BUDDY_ACK (built in M2) |
| 57 | COMPANION_POSE | H->G | U (its own latest-value slot, newest sender tick wins) | 10 Hz while the companion moves (5 cm since the last send), 2 Hz idle | sender tick, kind, slot, present, xyz mm, yaw mrad (the host player's heading), state; the receiver appends its latency | The U half of COMPANION (one op has one wire type); built in M2 |
| 50 | MOUNT_FAST | both | U | 20 Hz while ridden | sender tick, kind 1 horse / 2 walker, slot, xyz mm, heading mrad, gait | RIDE_FAST's track machinery (100 ms render delay, extrapolation, stale rules) keyed by (kind, slot) instead of a per-copy id |
| 44 | RIDE_FAST | both | U | 20 Hz | unchanged | **Also sent by the host** for AI-driven vehicles near the guest (F8), 10 Hz |
| 51 | VEHICLE_STATE | H->G | R | on change (>0.5 m, >5 deg, damage or state change), at most 2 Hz per vehicle; on pairing a rate-limited sweep (4 vehicles per 250 ms) | vehicle id, locator hash (0 if unnamed), type, flags (alive, broken, npc-ridden, player-ridden, relief, burning), xyz mm, yaw mrad, body life per mille, wheel damage bits (read API [U]) | New |
| 52 | VEHICLE_SPAWN | H->G | R | as built in M3: on change and every 5 s in free roam (one 31-byte op on the reliable world channel, so it shares the head-of-line exposure of kill and alert ops under loss [I]) | as built: seq, the host's mission, id 25600, cause (1 relief; 2 drop, 3 reinforce reserved), the host's carry type, paint, flags (alive, carry pose), xyz mm (the carry attitude, or the live pose 0.5 m up), yaw mrad | New; M3 uses it for index 0 only (the guest's reconcile, section 8.6) |
| 53 | REINFORCE | H->G | R | per event | cp hash, step (load, appear, cancel), reinforce type, coloring | New |
| 54 | SEAT | both | R | per event | seq, target (vehicle id, or horse slot with a horse flag), seat, role, action (claim, grant, deny, release) | New; host arbitrates |
| 55 | COMPANION_CMD | G->H | R | per press | kind, order id | Only if decision D1 is "forwarded" |
| 56 | CHARACTER | G->H | R | at pairing, and on change | the guest's resolved character (story resolved locally), Quiet flag | New, unless HELLO can carry it (HELLO's spare fields [U]); must arrive before the host's `sendSeeds` (F1.4) |
| 58 | VEHICLE_DAMAGE | G->H | R | per drop, at most one per vehicle per 500 ms and 8 per second (the host takes at most 3 per vehicle and 16 per second, each at most half its copy's life: 8.15) | as built (section 8.14): seq, id, locator hash, type, flags (1 the guest rides it, 2 sent before a VEHICLE_STATE write), the guest copy's life after the drop (1..65534), the drop, xyz mm | New (the vehicle fixes of 2026-10-29): the guest's own damage reaches the host's copy; a partner without it skips it by its length |
| 46/47 | VEHICLE_GONE / REQ | as today | R | per event | unchanged | Turned on by default after E5; index 0 per E17. With `world_relief` on, REQ_VEHICLE_GONE for 25600 is the relief module's (a guest-side loss of the host's sortie vehicle, sent whatever `world_vehicle_gone` says; section 8.7, the review) |

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
- **Status (2026-10-30, `wp/seat-shoot`, 8.20):** the census is ported (merge commit, contract re-pinned); the seated
  aim never latches the check [O]; design A is built behind the lab key `partner_seat_gun` and its hold runs, but the
  partner's gun does not show [O]; no seated shot was seen to land [O]. M7 is not done; the next step is design B or a
  native passenger-aim study (D16).

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
| D-Dog (guest's copy) | **Built (M2): the stop-AI form of `LuaAiStayAndSnarl` (mode 1/2, whose AI update the Dog AI skips) placed by the command's own warp at 10 Hz** [O, 8.2]. 176 s within 1.4 m of a guard in an alert: zero `mirror_engaged` (run `4cd88ea2`). Fallbacks if needed: `SetEnabled=false` while a guard is within 5 m (the `TppDemo` command) [U] | **Passes with the D7 guard (8.9)** [O]: a native filter on the dog's damage handler drops every hit on the guest's copy while the guest's companion module holds it (about 85 s of guard fire through it, 50 hits dropped, no death: run `3922cc80`); the copy follows the host dog's life, sleep and death, and a death the guard cannot see is answered by the restore fallback (about 2.6 s). Before it, E19 failed (8.2: `d937191f`, `dc761c98`; `SetDisableDamage` has no dog handler). Review fixes (8.10): the guard's slots are installed only on a PC running the key; through a link loss an armed guard is held drop-only until the session or mission ends; a death while the host's state is unknown waits for it | `mirror` |
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
| K21 | Guards on the guest's copy hurt or kill a mirror; the guest copy enters a dead, hospital or retreat state the host does not have, and the block may drop the mirror | High / high | Invulnerability where an API exists; death and hospital only from the host's COMPANION state. Dog: no API exists, so a native guard drops its hits on the guest's copy, its death comes only from the host's state byte, and a death the guard cannot see is restored (D7, 8.9) | E19 |
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

Numbering (integration on `dev/oct7-continue`, 2026-10-29): the three branches numbered their results on their own.
`wp/companion-host`'s 8.1-8.3 keep their numbers, `wp/relief`'s 8.2-8.5 are now 8.4-8.7 and `wp/vehicle-state`'s 8.1
is now 8.8. References in this file and in `docs/STATUS.md` were updated; commit messages and run notes keep the
branch numbers.

### 8.1 Results of the M1 experiments (2026-10-29, branch `wp/companion-host`)

Hidden-worker runs on loopback: research build, native gun stage, copied saves, low graphics, `companion_host_owned=1`;
the guest plays Quiet (`coop.ini character=quiet`) in every run. Evidence: each copy's `script-observer.log` sliced at
the session offsets (`host-observer.slice.log`, `guest-observer.slice.log` in the run folder, archived under
`%USERPROFILE%\mgs-coop-run-archive\companions\<run>`), and frames saved as JPEG in the session scratch folder
`complete/frames/companion-host/` as `<first four characters of the run id>-<role>-<label>.jpg`. Every run closed with
the baseline restored and the real saves unchanged.

| Run | Scenario and seeds (host / guest) | What it is for |
|---|---|---|
| `dd4c61ff` | `companion-seeds-e4.txt`, `quiet-recruited` both | E4 census frames; the first seed set, superseded (it wrote only `buddyType`) |
| `8ad7a986` | `companion-host-owned.txt`, first form; `afgh-ledge` / first `afgh-horse` | the guest's own buddy came back from `buddyTypeMissionStart` (`vars_type=2_own=2`, no `own_kept ... own=horse`), which led to the MissionStart keys (`c7b38d7`) |
| `a4296449` | `companion-seeds-e4.txt` | the seeds `afgh-horse` (guest) and `afgh-quiet` (host), with the MissionStart copies |
| `6306df68` | `companion-host-owned.txt`, second form | every expectation met, but no frame shows a companion: the lever called it 4 m to the side, off both cameras |
| `9e8f22a7` | `companion-host-owned-mb.txt`, first form; `africa-jeep` / guest save of `6306df68` | Mother Base load; a host warp dropped the host's kind for 3.5 s and the guest's 3 s debounce sent its dog away and loaded it again (now 8 s, 15 s for none); the cell warp landed in the sea (MISSION FAILED) |
| `489b3c6f` | **run A**, `companion-host-owned.txt`; `afgh-ledge` / `afgh-horse` | E1 free roam, E3 writes, E4, the reconcile both ways; no expectation failed |
| `fa397547` | **run C**, `companion-host-owned-mb.txt`; `africa-jeep` / guest save of `489b3c6f` | E1 Mother Base, the E3 read-back, the E20 refusal, the D-Walker check; no expectation failed |
| `b993af09` | **run B**, `companion-host-owned-quiet.txt`; `afgh-quiet` / `afgh-ledge` | the Quiet lock, the E20 frames; 3 expectations failed, all three wrong in the scenario (below) |
| `de43726a` | Quiet play rehearsal, `companion-host-owned-quiet-play.txt`, `-Mode play -WanSim bad` | the late-join Quiet lock in play; 3 expectations failed, all three wrong in the scenario (below) |
| `b83c9f7a` | dog play rehearsal, `companion-host-owned-play.txt`, `-Mode play -WanSim bad` | E1 through the seeds in play, the CALL mask, the F9 refusal, the host's F9 swap and the guest's reconcile; no expectation failed |

**Static reads** (capstone on the exe `085c2f82...bb45`, read-only; the Ghidra server was not used) [O]:
- Buddy var declarations `0x14099EA60`: descriptor `+0x14` holds the count (low 16 bits) and the type (byte 2: 5 u8,
  7 u16, 3 float, 2 u32). `buddyType` u8, `initialBuddyPos` float[3], `buddyGearAmmoCount` and
  `buddyGearStockAmmoCount` u16[2], `buddyQuietEquipType` u16, the other look vars u8. The mission-start copies:
  `buddyTypeMissionStart` +0x2D08, `initialBuddyStatusMissionStart` +0x2D0C, `initialBuddyPosMissionStart` +0x2D10,
  `initialBuddyRotYMissionStart` +0x2D1C.
- `TppBuddyService.SetVarsMissionStart` (`0xA54E10` -> `0xA54C60`, `TppMission.lua:976`) writes
  `buddyTypeMissionStart` from the service's reserved type (+0x16C) when that is non-zero, else from `buddyType`
  (re-read 2026-10-29: `0x140A54C84..0x140A54C8F`). `ResetVarsMissionStart` (`0xA53850` -> `0xA536D0`,
  `TppVarInit.InitializeForNewMission`, before the block `Load`) copies every `*MissionStart` back into the live vars and
  sets the reserve from `buddyType`. **So a new mission loads `buddyTypeMissionStart`, and a checkpoint continue the live
  `buddyType`;** M1 seeds and keeps both (`c7b38d7`).
- `TppBuddyService.SetDisable` sets bit 0x20 of the service flags `0x142C290D0` (read by `IsRideHeli`); it is not an
  order lock. `IsEnableBuddyCommand` (`0xA52200` -> `0xA52140`) compares bond levels. Neither locks orders.
- The CallBuddy core `0xA44740` (it tests the disable byte at service +0x16E) is reached from the Lua bindings
  `CallBuddy`, `CallBuddyWithHalo` and `RequestChangeBuddyForEntryGate`, from `0xA439C0` (called by the controller
  update at `0xA49F26`) and from `0xA49AF0`. A native request that reaches the controller meets the disable byte [I: the
  iDroid caller itself was not traced].
- The Mother Base cell Quiet is the quest `mtbs_wait_quiet`, opened while `TppStory.CanArrivalQuietInMB()` (no argument)
  answers true; the `f30050` cell assets ask `CanArrivalQuietInMB(true)`.

| # | Result | Evidence |
|---|---|---|
| E1 | **Pass, free roam and Mother Base.** Free roam: the host logs `seed; kind=dog; live=dog; mission=30010`, the guest `own_kept; why=seed; own=horse` and, 10 s later, `loaded; kind=dog; source=seed; mission=30010`; `buddy_status` reads `kdog ... vars_type=2` on both (guest `own=1`) [O]. The host's frames show the dog beside Snake; on the guest's copy the dog is held 2.5 m from the host dog's pose (`target_submitted; error_cm=250`, `world_buddy.lua`), but the guest's own body and the tall grass hide it, and only its buddy marker shows ("8m") [O]. Mother Base: `loaded; kind=dog; source=seed; mission=30050`, `kdog` on both; the guest's frame shows the dog beside the host's body, the host's frame its legs and vest beside Snake behind a railing [O]. Mid-mission (K16): the host swaps to D-Horse and back (`companion_lab call_here horse`, then `dog`); each time the guest follows with its own `CallBuddy` 8 s later (`reconcile; via=call; kind=horse; have=dog`, then `kind=dog; have=horse`), and both screens show a horse with the same coat; on the guest's it stands 4 m in front of the guest, because M1 sends the kind only (the pose is M2/M10) [O]. That broke section 4 rule 6 (the horse in the wrong place, mountable on the guest's copy): since the review fixes (8.3) the guest's copy shows no horse. The native `CallBuddy` writes `vars.buddyType` (the host's `vars_type` went 2 -> 1 after the swap; answers the 3.1 unknown) [O]. The look keys are pinned by the fixture; the frames cannot separate the seeded look from the guest's own, because both saves come from one lab save [I]. | `489b3c6f` frames `489b-host-m1-dog`, `-m1-dog-2`, `-m1-callmenu`, `489b-guest-m1-reconcile-dog`, `489b-{host,guest}-m1-reconcile-horse`; `fa397547` frames `fa39-guest-m1-mb-dog`, `-m1-mb-dog-2`, `fa39-host-m1-mb-dog`; superseded `dd4c61ff`, `8ad7a986`, `6306df68`, `9e8f22a7` |
| E3 (save part) | **Pass.** Run A's guest writes its own buddy at every save while the host's dog is live: the mission-start writes (`save_own; slot=0/1/6; own=horse; live=dog; n=1..3`), the checkpoint (`event=recovery; step=checkpoint` and `save_own; slot=0; own=horse; live=dog; n=30` in the same millisecond, `t_ms=159384081`), the exit to 40010 (`n=37`), the 30020 start (`n=65`) and the 30020 checkpoint (`n=87`); no save ran while the horse was out [O]. Run C booted the guest from that save and read it back: `own_kept; why=seed; own=horse` [O]. Every write after the start had the host's dog live, so this read-back tells D8 (b) from a copy of the host's vars. Without the key no buddy key is applied (fixture `world_companion_lua_check`) [O fixture]; D8 (b) off was not run in the game. Not run: the locked-companion case (a guest save without the host's companion) and the engagement part (a guard next to the mirror, now with M2). No `mirror_engaged` line in any run, but no guard came near a companion in them [O]. | `489b3c6f`, `fa397547`; fixture |
| E4 | **Found and refused by a named method, except the iDroid buddy request (not reached).** (1) Call menu (hold Q): taken at the same moment, the host's frame shows its menu (Knock, Wait, Get it, DD (call), Sonar) and the guest's none: the guest's `Player.SetPadMask` on `PlayerPad.CALL` (setting `MgsCoopCompanion`), logged `guard; path=call_menu; via=pad_mask;` with toast 13/1 [O]. The mask is on only while the host has a companion out, and off in a CQC hold (CALL also interrogates); **so the guest has no Knock and no Sonar while the host's companion is out** [O code and fixture]. (2) F9 summon: `refused; path=f9_horse_spawn;` and the toast "Only the host can call, change or send away the companion in co-op." [O]. (3) Lua entry points (`CallBuddy`, `CallBuddyWithHalo`, `ReserveCallBuddy`, `DeleteBuddy`, `TppBuddyService.SummonBuddy`): wrapped with a census; no game path called any of them in any run (zero `step=hook` lines) [O], so the player's buddy paths are native [I: no positive control showed a wrapper intercepting a game call; review 2026-10-29]; a guest call of another type is refused with toast 13/2 (fixture) [O]. (4) iDroid: Tab opens the map page; its buddy request was not reached blind. The guest sets the disable byte for every type that is neither the host's nor loaded (`guard; path=idroid_swap; via=disable_type; allowed=<kind>;`), which the CallBuddy core tests [I for the iDroid path; a request for the host's own type stays possible]. A dismiss is not covered: `DeleteBuddy` requests NONE, which the byte list never disables, so D1 (a)'s "dismiss refused with a toast" is unproven for the iDroid path [I]. (5) Vehicle drop: not tried (D6). Host side: the sortie-prep and iDroid buddy lists were not opened; R5 rests on the disable byte plus the fallback (rewrite to none, toast 13/3). | `489b-host-m1-callmenu`, `489b-guest-m1-callmenu`, `489b-guest-m1-refused-toast`, play `b83c-*-m1p-callmenu`, `b83c-guest-m1p-refused-toast`; `dd4c-guest-e4-guest-idroid`; every run (zero `step=hook` lines) |
| E20 | **Pass for the refusal [O]; that the wrapper is what hid her is [I] (review 2026-10-29).** Both copies refuse the cell quest once a player comes near the Medical platform: `mbquiet_hidden; stock=true;` on host and guest (`stock=true` is the retail answer, so she would be there) [O]. In run B both players stand beside the cell, and both frames show the empty cage [O]; there is no control frame of Quiet in the cell at that spot with the hide off, so the frames cannot separate the wrapper from a cell that never spawned (fast boot, quest block, trap trigger). While the hide holds, `f30050`'s `NeedPlayQuietWishGoMission` story-demo gate also answers false; no save diff of quest or story vars was taken. The first warp, 2.5 m east of her radio, was the sea; `companion_cell goto` now lands 0.8 m from the radio toward the plant's centre (`-141.96,-3.20,-971.34`), where the seed `mb-quiet-cell` was taken. | `fa397547`, `b993af09` frames `b993-host-m1-mb-quiet-cell`, `b993-guest-m1-mb-quiet-cell`; `9e8f22a7` |

**Run B, the Quiet lock (`b993af09`).** The host learns the guest's character in the helicopter
(`character; guest_code=6; guest_char=quiet`) and disables the type (`quiet_lock; via=disable_type`). The boot's
helicopter had the host's Quiet out (its save deploys her), so the lock sent her away there
(`own_kept; why=quiet_lock; own=quiet`, `quiet_lock; via=delete_buddy`) before the sortie; the seeds carried none
(`seed; kind=none`), the guest loaded none and logged `reconcile_skip; reason=self_quiet` while the host's kind read
Quiet for a moment [O]. The host's toast "Your partner plays Quiet, so buddy Quiet stays at Mother Base this session."
shows right after the landing, and no buddy Quiet is on either screen [O]. The three failed expectations were the
scenario's: the waitlog for `via=sortie_rewrite` (Quiet was already deleted, so the Load wrapper found nothing to
rewrite) and `knone` twice (Mother Base has no buddy block without a dog or Quiet: `buddy_block_inactive`,
`vars_type=0`); the scenario now expects those. At Mother Base the host's `buddyTypeMissionStart` read 3 again
(`companion_status ... vars_type=0; start=3`) [O; which native write put it back is U], and a restart copies that into
`buddyType`; `ae4c3e9` rewrites it while the lock holds (`via=start_rewrite`, at most every 5 s; fixture, and seen in
the play rehearsal). The guest's own character line reads `character=apply; mission=30050; requested=quiet;
applied=dd_male; reason=bypass_unavailable`: the request is Quiet and the reason is not `buddy_quiet`, so the seeded
buddy did not decide the guest's character (K25) [O]. The DD body comes from the research worker: its guest copy has
logged `player_type_patch=refused; reason=body_hash` at every load since line 1,119,397 of the shared guest log, long
before M1's first line (1,441,442), so the guest's Quiet body could not be filmed [O, not an M1 effect].

**The Quiet play rehearsal, late join (`de43726a`, `-Mode play -WanSim bad`, link 181 ms).** Play's title Continue
lands both in the helicopter, where they pair with the host's Quiet out: `character`, `disable_type`, `delete_buddy`;
the continue into 30010 rewrites the sortie (`sortie_rewrite`) and the seeds carry none; the guest loads none
(`loaded; kind=none; source=seed; mission=30010`). The host's own save keeps its Quiet (D8 b), so 30010 loaded her
again on the host and the first world tick sent her away (`delete_buddy`, `start_rewrite` at the same millisecond as
`disable_type`) [O]. **Review 2026-10-29 (major 1):** the `sortie_rewrite` did not work: `TppMain.OnAllocate` restores
vars from the save slots (`VarRestoreOnMissionStart` / `OnContinueFromCheckPoint`) before `SetSortieBuddy` and the block
`Load`, and the host's session-long snapshot wrote Quiet into every slot; what protected was the first-tick delete.
Fixed in 8.3. Both frames show no buddy Quiet [O]. The three failed expectations were the scenario's: it
expected the guest to Continue with its own dog and send it away (`reconcile; via=delete`), but play's Continue goes
through the helicopter, so the guest's load is a co-op travel and its dog never loaded; and `knone` twice
(`buddy_block_inactive`, `vars_type=0`). The scenario now expects the observed path.

**The dog play rehearsal (`b83c9f7a`, `-Mode play -WanSim bad`, link 175-185 ms).** Play's Continue lands both in the
helicopter, where they pair; the host's continue into 30010 seeds its dog, and the guest keeps its horse and loads the
dog (`own_kept; why=seed; own=horse`, `loaded; kind=dog; source=seed; mission=30010`); `kdog ... vars_type=2` on both,
guest `own=1`. The guest's held dog stood 4 cm from the host dog's pose (`target_submitted; error_cm=4`). The guest's F9
summon is refused with its toast; the host's F9 summon swaps to D-Horse and the guest follows 8 s later
(`reconcile; via=call; kind=horse; have=dog`), `khorse` on both [O]. Frames: the host's call menu and none on the
guest's screen, and the guest's toast; the scene is at night with both players on one spot, and no frame shows the dog
or the horse [O]. No expectation failed.

**Seeds** (section 9; `D:\mgs-coop-seeds`, made with `debug seed_buddy` and `tools/worker/Save-WorkerSeed.ps1`):
`afgh-horse` (guest; checked: `own_kept ... own=horse`, `own=1` in `6306df68` and `489b3c6f`), `afgh-quiet` (host;
checked: `own_kept; why=quiet_lock; own=quiet`, `own=3` in `b993af09`), `mb-quiet-cell` (both, from `b993af09`, the
cell spot; host own Quiet, guest own dog; not booted yet), `mb-walker` (host, from `fa397547`: `seed_buddy walker` read
`obtained=true; sortie=true`, so D-Walker is developed in the lab save; Mother Base location, `buddyTypeMissionStart`
WALKER_GEAR, so `boot 30010 ...` sorties the walker; not booted yet). It stands in for the planned `afgh-walker`.
### 8.2 Results of the M2 experiments (2026-10-29, branch `wp/companion-host`)

Hidden-worker runs on loopback as in 8.1 (research build, native gun stage, copied saves, low graphics,
`companion_host_owned=1`, the guest plays Quiet and is drawn as a DD soldier). Seeds `afgh-ledge` (host, D-Dog) and
`afgh-horse` (guest, D-Horse). Frames as JPEG in the session scratch folder `complete/frames/companion-dog/` as
`<run4>-<role>-<label>.jpg`; log slices and BMPs in `%USERPROFILE%\mgs-coop-run-archive\companions\<run>`. Every run
closed with the baseline restored and the real saves unchanged (runs `d937191f` and later closed with `Forced=True`
after `owned_shutdown_requested`; the baseline and real-save hashes were verified). `event=companion_track` is a 2 s
window of the guest's hold: the host dog's mean speed (band `walk` below 3.5 m/s with R4's 3 m, `run` from 3.5 m/s
with 5 m), the distance between the guest copy's dog and its target (the host dog's pose led by the sample's age)
before each placement, `within`, the scenario's `leg` (lab lever `companion_lab mark`), the hold `mode` and the count
of extra warps.

| Run | Scenario | What it is for |
|---|---|---|
| `8c0b425f` | `companion-dog.txt`, first form | the hold as M1 sent it (`LuaAiStayAndSnarl` with `look`, `useMarker=false`): the guest copy's dog never moved (14 m from the host dog at rest for 4 min); the legs ran into the rock at the boot spot |
| `ffb2d7ab` | second form | `marker` (`useMarker=true`) against `warp` (`warp=1`), legs on the open grass 32 m west of the boot spot |
| `4cd88ea2` | third form, `warp` the default | the R4 legs, the guest 40 m away, E3 beside a guard (warp, marker, protection lever) |
| `52010d22` | `companion-dog-mb.txt`, host `africa-jeep` | void: the host's seed sits in the 40020 helicopter, the guest's in 40010; the guest's boot stopped at `follow_host` |
| `994d252f` | `companion-dog-mb.txt` | Mother Base legs, then host-led travel to 30010 with the host left in its helicopter |
| `ba6d6ff8` | `companion-dog-play.txt`, `-Mode play -WanSim bad` | the play rehearsal: the dog stayed unrealized on both copies for 136 s, so the legs ran with no dog; 3 expectations failed (below) |
| `d937191f` | `companion-dog.txt`, current form (`4b4fc84`, `0f839d9`) | the legs with the lead fix (`4cd7ee6`), away, E3 beside a guard in an alert, the host's dog ordered to take that guard down, E19 (the guest's dog in the line of fire) |
| `dc761c98` | `companion-dog-e19.txt` (`84e4f1d`) | E19 again with the protection lever held, the D8 (b) save after the dog's death, a second takedown try |
| `86421fa6` | `companion-dog-repair.txt`, first form (`0109eaa`, `1de1b27`, `1166fdb`) | void for the repair: the shield never engaged (the held dog stood with the host's dog 30 m away, outside the 30 m soldier scan), so the dog did not die; 2 expectations failed |
| `033193d3` | `companion-dog-repair.txt`, second form (`8f40c73`) | the legs with the drift warp, then the dog brought to the guard and shielded with the repair lever on; no expectation failed |

**Static reads (capstone on `085c2f82...bb45`, read-only)** [O]: the dog dispatcher `0x140C7D220` compares the command
hash with four values (`0x24824B9A` `LuaAiStayAndSnarl`, `0x2E6160B7`, `0x5DB1CEDE`, `0xE60574FA`) and returns -1 for
any other command (`0x140C7D299`): `SetDisableDamage` has no handler on the dog [O; that no other path takes it is I,
and `dc761c98` agrees]. The `LuaAiStayAndSnarl` branch (`0x140C7D4D6..0x140C7D7FB`) reads `position` (absent: the
"omission cleanup", mode 0), `useMarker`, `look`, `dash`, `warp` and `ignoreMarker` (bit 0x400 of the record) and calls
vt+0x98 (`0x140C7DA10`) with the fifth argument set to 1 when `useMarker` is true or `look` is absent. That function sets
the record mode +0x50 to 1 (no dash, fifth 0), 3 (no dash, fifth 1), 2 or 4 (with dash), copies the target to +0x30 and
`look` to +0x40, and with `warp` writes the position and the heading (`atan2` of look minus position) into the dog's
position array and its transform (vt+0x68, +0x70). The Dog AI update `0x140C7E650` jumps to its end for modes 1 and 2
(after `0x140C7EAA6`, mode not 0 and not 3/4 -> `0x140C7EDE2`): the stop-AI form of the retail `s10040` "SetDogStopAI"
[I: no movement, no target selection].

| # | Result | Evidence |
|---|---|---|
| Dog realized | **At the `afgh-ledge` spot (yaw 95, facing the rock) the dog stays unrealized on a copy (receipt `dog_unrealized`, probe `n_real`) until that copy's camera turns or its player is moved** [O]: realized 2-5 s after the host's teleport (`4cd88ea2`, `ffb2d7ab`, 74 s after the load), after `resetpose` (`8c0b425f`, 34 s), and in the play run on each copy separately after its own mouse turn (host between 4 and 5 s after its turn, guest 1 s after its turn, 136 s after the load; `ba6d6ff8`). Mother Base and the heli landing realized it within 10-40 s with no turn. That the stock buddy spawns the dog out of the camera's view is [I]. The scenarios now move the host and turn both cameras before the dog is awaited (`4b4fc84`); `d937191f` then met `target_submitted` at once | probe lines and capture ticks of the runs named |
| Hold | **Mode 1/2 placed by `warp=1` (the `warp` hold, default).** Mode 1/2 without warp never moved the dog (`8c0b425f`). Mode 3/4 (`marker`) walks it itself: 2-3 cm at rest but 7-15 m behind whenever the host moved, 13 stuck warps (`ffb2d7ab`). `warp`: `4cd88ea2` walk 0.87-1.46 m max per window, run 1.1-4.4 m, dash 1.0-2.8 m; `d937191f` walk 1.37-3.11 m (p90 1.11-2.73; one window of 6 over R4's 3 m, by 11 cm), run 1.09-3.91 m, dash 1.18-2.04 m [O]. Right after the run leg one idle window read 5.56 m (`4cd88ea2`) and 4.41 m (`d937191f`, after the lead fix `4cd7ee6`): the target no longer changed and the same warp command was not applied again while the stop-AI dog slid on [I]; since `1de1b27` a dog more than 1 m off an unchanged target for 200 ms gets the stock `SetWarpPosition`: in `033193d3` the idle window after the run read 2.95 m and 4 such warps fired, but one walk window read 3.21 m and one idle window in the dash leg 3.47 m [O]. So walking stays inside R4's 3 m in all but one window per run (3.11 m, 3.21 m); what makes those single windows is [U] | track lines; frames: `ffb2-{host,guest}-m2-dog-warp-walk` (the dog beside the host on both screens), `d937-host-m2-dog-idle`, `d937-host-m2-dog-walk` (host's screen), `d937-guest-m2-dog-run` (guest's screen: the dog beside the host); `d937-guest-m2-dog-walk` shows only its marker (the guest's body hides it) |
| Away | With the guest 40 m behind the host the guest copy's dog stayed with the host dog (idle windows 2-23 cm, `leg=away`) [O]. The frames show the host 40 m away with the dog's marker beside it; the dog's body is not resolved at that distance | `4cd88ea2`, `d937191f`; `4cd8-guest-m2-dog-guest-away`, `d937-*-m2-dog-guest-away` |
| Never released | No `result=released` line in any M2 run; `hold_here` / `hold_last` while the host's dog was not yet readable [O] | all runs |
| E3 (engagement) | **Pass for the `warp` hold** [O]. A guard warped beside the host (spawn sync on the guest's copy, `div_m` 0.0-0.5); the guest's dog held 1.37-1.53 m from it (lab lever `companion_lab near_guard on`): 176 s within 3 m in `4cd88ea2` (`s3=176`), 155 s in `d937191f`, 84 s in `dc761c98`. Each time the camp went to phase 3 about 5 s after the spawn (`event=phase; ...; phase=3; src=rx`) and the guard stood in combat (`place_combat`). Zero `mirror_engaged`, zero `event=divergence`, no kill or sleep message with the dog as attacker on the guest's copy in any run; 30 s of the `marker` form beside the guard in `4cd88ea2` engaged nothing either. Not covered: E3's other half, "the host's dog ordered to attack a different guard" (no takedown was reached, Kill row) | slices; frame `d937-guest-m2-kill-2` (the guest's dog beside the guard, the alert indicator) |
| E19 (dog) | **Fail: guards on the guest's copy kill the guest's copy of the dog, and the protection lever does not stop it** [O]. Lab lever `companion_lab near_guard shield` holds the dog 1 m from the prone guest toward the guard, so the guard's fire at the guest crosses it. `d937191f`: `event=obs; m=Dead; ent=0; id=9728; att=other` after 76 s of fire (41 s with `SetDisableDamage` sent, 35 s after it was turned off); `dc761c98`: protection sent before and held for the whole window, Dead after 13 s. Both: `deadBuddyType` 0 -> 4 (the DOG bit, [I]: `vars_type=2` is DOG, 4 = 1 << 2) and `buddyCommonFlag` 32 -> 0 (bit 5, meaning [U]) within 0.8 s; the host's dog unhurt. In `dc761c98` the dog stayed readable for about 8 s (the hold placed it beside the host again), then the guest's buddy block unloaded (`buddy_block_inactive`, `active=none`); M1's reconcile asked for the dog twice (`reconcile; via=load; kind=dog; have=none; ok=1`) and no dog came back before the run ended, about 7 s after the first reconcile (t=178664387 to the last line at t=178671467): that it stays gone for the rest of the mission is [I] (review 2026-10-29) (K21). No `Damage` message for the dog was ever observed, only `Dead`, so the hurt before a death is not visible to the Lua hook [O] | slices; frames `d937-guest-m2-shield-4` (the dog down beside the prone guest), `d937-guest-m2-shield-5` (gone), `dc76-guest-e19-shield-dead` (the dog down, blood) |
| D8 (b) and the death | Before `84e4f1d` the guest's own snapshot held only the follow-seed buddy keys, so a save after the mirror died would have written `deadBuddyType=4` into the guest's own save [I from the key list]. Since `84e4f1d` it also keeps `deadBuddyType` and `buddyCommonFlag` (own-only, never seeded): `dc761c98`'s checkpoint after the death logged `save_own; slot=0; own=horse; live=dog; n=30; own_dead=0; live_dead=4; own_flag=32; live_flag=0` [O] | `dc761c98` |
| Repair candidate (D7) | **Works once in the lab** [O]: with `companion_lab repair on`, the dog's Dead message (`033193d3`, after 81 s of fire) was followed at once by `companion_repair; step=vars; dead=4_0; flag=32_32` and 0.8 s later `dead=0_0; flag=0_32`; about 10 s later the guest's block unloaded (probe `n_select`, `buddy_block_inactive`), M1's reconcile asked for the dog (`reconcile; via=load; kind=dog; have=none; ok=1`, 18 s after the death) and this time it loaded: `active=2` 0.7 s later, then held at the host dog's pose again (8 cm idle, run windows 1.7-3.5 m), `kdog` to the end. The dog was gone on the guest's screen for about 19 s. Compare `dc761c98` without the lever: the same reload never brought it back. No frame shows the returned dog (it stood with the host's dog 25 m off both cameras). The checkpoint after it wrote `own_dead=0; live_dead=0; own_flag=32; live_flag=0` (the game cleared bit 5 again on the reload) | `033193d3` slices; frames `0331-guest-rep-dead`, `0331-*-rep-after-*` (no dog in view) |
| Host hidden | **Pass.** After host-led travel from Mother Base to 30010 the guest left its helicopter while the host stayed aboard: on the guest's copy body 1 was unrealized (`partner_probe=state; ids=0000,ffff; parts=3,0`) until `allactive_set` about 42 s later, and through those 42 s the guest's own-dog read answered (`target_submitted`; track windows of 5-137 cm with the host in the helicopter once `allactive_set` came, but before it, at tick 170048953, `leg=heli` windows reached 213, 262, 282 and 302 cm, the last outside R4's 3 m; review 2026-10-29); the probe never refused `n_scope` on either copy (only `n_real`, the dog itself unrealized for the first 40 s after the arrival) [O]. Before M2 the receipt's `field_ready` needed body 1 (2026-10-23 host log: refused `scope` for about 50 s in 30020). Right after the guest's dog was realized at the landing zone, two `run` windows (`leg=-`) read 6.3 and 7.0 m while the host's dog ran off the landing zone; their cause is [U] | `994d252f`; frames `994d-guest-m2-dog-host-in-heli` (the dog in the grass, dusk), `994d-*-m2-dog-after-heli` |
| Mother Base | The walk-band windows of the walk leg on the command deck within 0.82 m (the leg's first window read `err_max_cm=2780` and needed a warp, t=169894978; review 2026-10-29); the dog beside the host on both screens. In `994d252f` the run leg hit a container wall at the `mb-ladder` spot and never ran. Not repeated: the corrected scenario (the run leg facing the open deck) was not run, the last runs went to E19 and the repair | `994d252f`; frames `994d-{host,guest}-m2-mb-dog-walk`, `994d-*-m2-mb-dog` |
| Kill commit/apply | **Not reached** [O]. `d937191f`: the host marked the guard (`enemy mark nearest`), aimed at it, held CALL (Q) and pressed 3: the call menu showed "3:Get it" selected, but the host's dog stood 30 m away and nothing was taken down. `dc761c98`: the dog was first called onto the guard (`companion_lab call_here`, `warp=1`), then CALL + 3 once after an aim and once while aiming: no takedown. No host `event=obs` with `att=buddy` in any run, so no `commit ... actor=host` / guest `apply ... src=outcome` for a dog takedown. How the call menu's attack picks its target is [U]. The outcome path a dog takedown would use (a host commit, the guest's apply) is the existing one and M2 does not touch it [I] | frames `d937-host-m2-getit-menu`, `d937-host-m2-getit-pick`, `dc76-host-e19-getit-*` |
| Play | `ba6d6ff8` (`-Mode play -WanSim bad`): pairing, the seeds and the dog's load in play worked (`loaded; kind=dog; source=seed`, `kdog` on both, the hold mode `warp`), but the dog was never realized during the legs (Dog realized row), so 3 expectations failed and no frame shows a dog. Not re-run: the play default cannot flip while E19 fails | `ba6d6ff8` |

### 8.3 Review fixes (2026-10-29, branch `wp/companion-host`)

An independent review of M1 and M2 (at `35c6f51`, verdict "ship behind the lab key") found two major and nine minor
defects, all on the key-on path, and refuted several claims (corrected in place in 8.1, 8.2, 9 and 10, marked "review
2026-10-29"). Fixes `c6809a4`..`090c6f0`; each has a fixture test that fails on the code before it. The key stays off by
default in lab and play; no play default changed.

| Finding | Fix | Evidence |
|---|---|---|
| **Major 1.** The host's Quiet lock kept one session-long snapshot of its buddy vars and wrote it into every host save. `TppMain.OnAllocate` restores vars from the save slots (`TppVarInit.InitializeForNewMission` -> `TppSave.VarRestoreOnMissionStart`; `InitializeForContinue` -> `VarRestoreOnContinueFromCheckPoint`) before `StageBlockCurrentPosition(true)`, `SetSortieBuddy` and the block `Load` [O, extracted `TppMain.lua`, `TppVarInit.lua`], so the `TppMission.Load` rewrite could be undone (`de43726a`) and a host's later non-Quiet choice was saved as Quiet, loaded and deleted [I, strong] | The host **parks** a Quiet the lock removes, per var (`buddyType`, `buddyTypeMissionStart`): the live var reads NONE; each save writes QUIET only for a parked var that reads NONE (`step=save_quiet`); `StageBlockCurrentPosition(true)` parks her again after the restore on a load whose `TppMission.Load` lock decision was latched (`via=stage_rewrite`); a live or loaded non-Quiet companion drops the park (`step=park_dropped`); an unlocked load puts her back (`step=quiet_back`). The host keeps no buddy snapshot (`C.own` is guest-only) | Fixture (`world_companion_lua_check`): the sortie save, the slot restore and the block Load with a host D-Dog under the lock load the dog; a checkpoint continue after a mid-mission dog call loads the dog; Quiet stays in the host's saves while the block loads none. Run `74d5ba7e` [O]: the helicopter Quiet sent away and both vars parked (`delete_buddy`, `start_rewrite`), the sortie saves wrote Quiet back (`save_quiet; vars=2` in slots 0, 1, 6), 30010 loaded none on the host (`vars_type=0; start=0`) and **no `delete_buddy` after the host's 30010 start** (`de43726a` deleted her at the first tick); the host's dog call ended the park (`park_dropped; var=buddyType; live=dog`); the checkpoint wrote the dog (`save_quiet; vars=1`: only the sortie copy parked); the Continue (kind 4) seeded and loaded the dog on both copies (`seed; kind=dog; live=dog`, guest `loaded; kind=dog; source=seed`). Run `9165baeb` repeated all of it and went on: after the Continue, host-led travel 30010 -> 40010 (mission clear: `SetVarsMissionStart` wrote the dog into the parked sortie copy, `park_dropped; var=buddyTypeMissionStart; live=dog`) -> 30020: the host loaded its dog (`vars_type=2; start=2`, `kdog` on both copies), nowhere a Quiet (`kquiet` 0 lines in both runs). FRAME: `9165-host-qf-dog-2` and `9165-host-qf-continue-dog-2` (the host's D-Dog beside Snake before and after the Continue). No `stage_rewrite` and no `sortie_rewrite` was logged in either run: no restore brought a Quiet back in these paths (the parked vars were already NONE, and the slots' Quiet sortie copy did not come back on the new mission or the Continue), so the stage hook's own effect in game is [I] (fixture only) |
| **Major 2.** The guest's copy loaded any host companion (seeds or reconcile) with no hold; a horse was called 4 m in front of the guest (COMPANION carries a pose only for the dog), where the guest could mount it; a non-Quiet guest got a full-AI Quiet | `C.shown(t)`: a kind with a hold (`C.HOLD`, M2: DOG) or NONE; never QUIET beside a guest who plays her. The guest filters the seeded `buddyType` / `buddyTypeMissionStart` before the retail Load (`step=seed_filter; reason=no_hold|self_quiet`), the reconcile aims at the shown kind (`reconcile_skip`, then `DeleteBuddy` after 15 s), a held kind is called only at the host's pose (`reconcile_wait; reason=no_pose` without one), the disable byte and the Lua guard use the shown kind; the CALL mask still follows the host's real kind | Fixture. Run `74d5ba7e` [O]: host `call_here horse` -> guest `reconcile_skip; reason=no_hold; kind=horse`, 14.5 s later `reconcile; via=delete; kind=none; have=dog`; the guest's `buddy_status` never read `khorse` (0 lines in both runs); back to the dog: `reconcile; via=load; kind=dog; have=none`, held 3-30 cm from the host dog. FRAME (`9165baeb`, `b651b42c`): `9165-host-qf-horse` and `b651-host-gv-horse` show the host's D-Horse beside Snake, `9165-guest-qf-horse` and `b651-guest-gv-horse` (same moments) Snake and no horse (in `b651` the guest's camera had shown the dog and its marker at that spot a minute before) |
| Minor: CHARACTER order. CHARACTER could arrive after the host's sortie seeds (a deploy held for pairing ran at once) | The host's deploy also waits for the guest's CHARACTER, at most 10 s after pairing (`world_follow.lua` `F.waitPeer`, `deploy_wait; reason=character`); the guest's seed filter never loads a seeded Quiet beside its Quiet | Fixture (`world_companion_lua_check`, `world_follow_lua_check` unchanged). In game the deploy came long after pairing (not exercised) |
| Minor: the session end (host). `C.guestQuiet` was never cleared; the cell-Quiet hide checked neither pairing nor the module | The session ends 10 s after a link-session or level-0 unpair, else after 120 s alone in game while no host transition is pending (`step=session_end`); the host then forgets the guest's character and the lock lifts. The hide needs the host's key with a Quiet guest, or the guest's live module | Fixture |
| Minors: the reconcile. The reconcile ignored `C.keep`'s result and retried every 5 s forever | It calls only with the guest's own snapshot; six tries 5 s apart and doubling, then one `reconcile_gave_up` line | Fixture |
| Minor: the session end (guest). After an unpair the guest's mirror dog stayed frozen and its own vars came back only at the next solo load | At the session end the guest deletes the host's companion (the module's own call; never a release to its AI), `world_buddy` forgets it, and `C.own` is restored (D8 b) | Fixture only: the worker's `world=0` unpairs only the copy that runs it |
| Minor: key agreement. `companion_host_owned` is local to each copy | Each copy shows its key in its own traffic (the guest's CHARACTER, the host's COMPANION); `C.live()` (the peer's seen) runs the module, `world_buddy`'s M2 path (else its BUDDY_STATE path), the mirror attribution and the F9 refusal; the agreement lasts until the session ends. No wire change | Fixture; run `74d5ba7e`: `step=peer; on=1` on both copies within 0.1 s of pairing |
| Minor: `attClass`. An own horse or walker counted `buddy` with the key off; `world_vehicle.lua` counts only `local` | Without the module an own horse or walker is `local` again, ridden or not (as before M1); with it, `local` only while `vars.playerVehicleGameObjectId` names it. That a walker pilot sets that var stays [U]; with the module on the guest's copy has no walker (no hold, M9) | Fixture (`world_outcome_attclass_lua_check`) |
| Minor: lab gate. `companion_cell goto` worked in play | Lab only (the read-only report stays) | Fixture |
| Minor: chunk size. Both chunks near the 32000-byte limit | The M2 hold and the companion lab levers moved to a new world chunk `lua/coop/world_companion_dog.lua` (spliced after `world_companion.lua`): `world_buddy.lua` 17.1 KB, `world_companion_dog.lua` 22.5 KB, `world_companion.lua` 29.2 KB after the fixes (M5 should give Quiet's hold its own chunk too) | CMake configure, `host_trial_patch_check`, the composed `world_sync` check |

Also: the lab lever `companion_lab call_here` now loads with `ReserveCallBuddy` + `Load` when the host has no buddy
block (the Quiet lock loaded none) and sets `vars.buddyType` as a sortie-prep choice would; it stands in for the
sortie-prep choice, which the worker cannot reach.

**The M2 hold on the shipped code** (review proof gap: no frame of the dog after `1de1b27` / `84e4f1d`): in `9165baeb`
the guest's copy held the host's dog 26-30 cm from the host dog's pose (`target_submitted; error_cm=26`), the host's frames
show the dog beside Snake (`9165-host-qf-dog-2`), and the guest's frames show only its white head above a rock rise under
its buddy marker (`9165-guest-qf-dog-2-zoom`, `9165-guest-qf-continue-dog-2-zoom`). `b651b42c` placed the guest 4 m to
the other side: FRAME on both copies, the host's D-Dog beside Snake on the host's screen (`b651-host-gv-dog-2`) and the
same dog beside Snake under its marker on the guest's (`b651-guest-gv-dog`, `b651-guest-gv-dog-3`); then the host's
D-Horse on the host's screen (`b651-host-gv-horse`) and no horse and no marker on the guest's (`b651-guest-gv-horse`).

**Runs.** `74d5ba7e` (`companion-host-quiet-dog.txt`, host `afgh-quiet`, guest `afgh-horse` and Quiet, lab): no
expectation failed, closed with the baseline restored and the real saves unchanged; it stopped at the first
`resetpose` after the Continue (the worker refuses a benchmark pose across a mission generation), so its 30020 leg did
not run. Its frames (`74d5-*-qd-dog`, `-qd-horse`) do not show the companion: at the boot spot the host's body covers
it on both cameras. `9165baeb` (`companion-host-quiet-dog-frames.txt`, same seeds, open grass, debug teleport after
the Continue): no expectation failed, closed clean; the 30020 frames are at night and show no companion. `b651b42c`
(`companion-host-dog-guestview.txt`, same seeds, the guest's view): no expectation failed, closed clean. All three are
archived under `%USERPROFILE%\mgs-coop-run-archive\companions\<run>`; frames as JPEG in the session scratch folder
`complete/frames/companion-host/` as `<run4>-<role>-<label>.jpg`.
### 8.4 Results: E2, E17, E18 (sortie vehicle, 2026-10-29, `wp/relief`)

Lab levers and a census in `lua/coop/world_relief.lua` (debug `relief_census`, `relief_arm`, `relief_seize`,
`relief_remove`, `relief_face`; every lever but the census refuses outside lab mode, and its game hooks install only
with the first lever). Scenarios `tools/worker/scenarios/relief-e2.txt`, `relief-e17.txt`, `relief-coop.txt`. Four
hidden runs (all closed with the baseline restored and the real saves unchanged); frames as JPEG in the session
scratch folder `complete/frames/relief/`. The shared Ghidra server refused again (WinError 10061) [O]; static reads
are capstone on the exe (notes in the session scratch `companions/relief/notes-static.md`).

**E2: passes [O].**
- Static [O]: `vars` base `[0x142C16920]`; `reliefVehicleCarryType` vars+0x2D34 (u32 slot: low byte the relief type,
  byte 1 written at realize by `0x140B34E69`), `reliefVehicleCarryAttitude` vars+0x2D38 (4 floats), Life +0x2D48, Ammo
  +0x2D60, State +0x2D66; `userPresetVehiclePaintType` vars+0x1BB1 (10). The vehicle manager (singleton
  `[0x142C20730]`, getter `0x140A27A50`, vtable `0x1422AC8E0`) embeds the relief record at +0x30 (flags +0x58, type
  +0x5B = the ReliefBlock's record byte +0x2B, paint +0x5C; vtable +0x100 hands the block `mgr+0x30`). vtable +0x78
  `0x140A28980` (SetRelief) writes the record and the carry vars; vtable +0x58 `0x140A29320` (RequestRelief) is what
  `Vehicle.LoadCarry` (`0x140A27FB0`) calls with the carry type and pose, after clearing the low byte in 40010 /
  40020 / 40050. The paint is `userPresetVehiclePaintType` of that type, re-read at every request (`0x140A27270` ->
  `0x140A28AF0`). Type t loads pack t of the vector `0x142C36A00` built by `0x140B3A1E0`: 1 west_lv, 2 east_lv, 3
  west_trc, 4 east_trc, 5 west_wav_machinegun, 6 west_wav_cannon, 7 east_wav, 8 east_wav_rocket, 9 west_tnk, 10
  east_tnk. The UI calls SetRelief (`0x1416BC4B0`, from a loadout-delivery path) with loadout vehicle id 0x0D..0x16
  mapped by `0x14095D230` (0x0D->2, 0x0E->1, 0x0F->4, 0x10->3, 0x11->7, 0x12->5, 0x13->8, 0x14->6, 0x15->10, 0x16->9).
- Real choice [O, run `5874afcd`, seed `coop-relief-30020` (decision D10: the launcher's co-op save copy,
  `<game>\mgs-coop\coop-saves` per `native/launcher/core/core_paths.cpp`, copied read-only)]: the host's 2026-10-23
  save continues in 30020 with `reliefVehicleCarryType=514` (type 2, east LV), carry pose (263.57, 1.56, -228.88), and
  25600 alive with `GetVehicleType` 2 at (263.56, -5.52, -228.88) (frame `host-relief-coop-continue`: the jeep). The
  current sortie loadout slot bytes are identical to the lab save's, so the choice is not in `sortieLoadoutInfoU8buf`.
  `GetVehicleType` answers 1 for the west LV and 2 for the east LV [O], the enum order of the exe strings [I].
- Lever [O]: `relief_arm` writing the carry vars at the ACC sortie (`TppMission.SelectNextMissionHeliStartRoute`, where
  the sortie prep's SetRelief writes) reproduces the choice. A write at the destination's mission start did nothing
  (run `698e67cf`: the game's `Vehicle` table refuses writes; `InitializeForNewMission` runs on the loading thread,
  whose world logs are dropped).
- Route A emulated on both copies [O, runs `fafd3fdd`, `9f01d603`]: with the lever on both copies (as a seed would
  give the guest the host's choice), each copy's own engine loaded the same pack and placed its own 25600 at the same
  spot: type 1 at (1226.94, 19.43, -127.08) at the 30020 savannahEast_N LZ and type 2 at (-347.95, 315.86, 763.58) at
  the 30010 enemyBase_S LZ, identical to 0.01 m on both copies in both runs; frames `fafd3fdd-*-relief-arrival1` and
  `9f01d603-*-relief-e17-quiet-before` show the jeep on both screens. The LZ drop point overrides the armed pose
  (`AdjustFromDropPoint`; the carry attitude reads the LZ point after arrival). Without the lever on the guest (run
  `5874afcd`) the guest's copy had no index 0 (today's state).

**E18: passes [O].**
- At the 30020 mission end with 25600 alive on both copies (run `fafd3fdd`): the host's stock
  `SeizeReliefVehicleOnClear` ran (`step=seize; alive=1`), then `OnFultonVehicle(25600, ..., 14)` and
  `AddTempResource(14)`, and the host's `4wdWest` went 1 -> 2 at the next mission; the guest's wrapper skipped it
  (`step=seize_skipped; alive=1`), no credit line, its `4wdWest` stayed 1. GMP 313850 on both copies throughout.
- Unsuppressed, both copies credit [O host, I guest baseline]: in run `5874afcd` the guest (no wrapper) ran its own
  stock seize and showed `4wdEast=1` at the next mission, as the host did (both started 0 from the same save).
- Charge: no lever spawn changed GMP on either copy, and the guest never runs the sortie prep (world follow replays
  the host's sortie) [O]; the host's real sortie-prep charge is not observable in the worker [U].
- `SeizeReliefVehicleOnClear` also runs at the ACC's mission clear (twice per lab goto), with `alive=0` [O].

**E17: passes with `Seize{"Instant"}` plus a skip of the receiver's `TppTerminal.OnFulton` for 25600 [O].**
- `Instant` (run `fafd3fdd`), `FadeOut` (run `9f01d603`, the guest's copy) and `Fulton` (run `9f01d603`, the host's
  copy), each without DirectAccount / CheckFultonType (the `Seize` parser `0x140B2B8FD`: method Fulton / FadeOut
  `0xEDCE2762` / Instant `0x85D95BDF`; flags CheckFultonType 1, CheckFarFromPlayer 2, DirectAccount 4), all remove index
  0 from that copy only (gone within 8 s; frame `9f01d603-guest-relief-e17-fadeout-after` has no jeep while
  `9f01d603-host-relief-e17-fadeout-after` still has the host's), and all run the stock credit path: `Fulton` (att
  none), `FultonInfo` and `Neutralize` (att local), `TppTerminal.OnFulton` -> `OnFultonVehicle` -> `AddTempResource`.
  That temporary credit is real: after the guest's FadeOut its `4wdWest` went 1 -> 2 at the next mission end.
- `relief_remove instant quiet` (run `9f01d603`): the guest's copy skipped its `TppTerminal.OnFulton` for 25600
  (`step=credit_skipped`), no `OnFultonVehicle` / `AddTempResource`, resources unchanged, 25600 gone after 8 s and 20 s
  (frame `9f01d603-guest-relief-e17-quiet-after`). That no commit follows at the mission end is [I] (no temp
  resource was added).
- The receiver's own removal still logs `Neutralize` of 25600 with `att=local` [O]: VEHICLE_GONE's guest observer
  must not take it for a guest loss.
- A warp next to the vehicle at its height (`relief_goto`, run `fafd3fdd`) put both players under the enemyBase_S
  slope: both fell, were downed and the session ended in a shared game over. `relief_face` (a turn in place) replaced it.

**Route decision: route A** (the same relief vehicle spawned by each copy's own engine from the host's choice). It
covers all ten relief types, including the APC, the tank and the other faction's jeep that route B cannot draw, keeps
the id 25600 on both copies, and its two costs are solved: the guest's mission-end seize is skipped (E18) and a
removal on the receiver is `Seize{"Instant"}` with its `OnFulton` skipped (E17). Not run: relief types 3..10 [I from
the pack vector], a non-zero paint preset, a guest continue with its own 25600, and the seed path itself (M3).
Round 2 (section 8.5) adds the continue and late-join case, a mid-mission `Vehicle.LoadCarry()` with the host's carry
values (proven on both copies), and shows that the pack draws bodies the location does not load (the east jeep in
Africa on both copies, a tank on the host's copy). Route A stands.

### 8.5 Results, round 2: the ACC, a mid-mission load, the seed and a tank (2026-10-29, `wp/relief`)

New lab lever `relief_load <type> <x> <y> <z> <rotY>` in `world_relief.lua` (lab, free roam, index 0 absent, nothing
armed; refused otherwise): it writes `reliefVehicleCarryType` and `reliefVehicleCarryAttitude` and calls
`Vehicle.LoadCarry()` at once. Scenarios `relief-midload.txt` (run `612afff7`, seed `lab`) and `relief-continue.txt`
(run `fc6f1f01`, seed `afgh-relief-lv`). Both runs closed with the baseline restored and the real saves unchanged;
frames `612afff7-*-relief-r2-*` and `fc6f1f01-*-relief-r2-*` in the session scratch folder `complete/frames/relief/`.
These two runs use up the task's budget of six hidden runs.

- **Sortie-prep reach: partly [O, `612afff7`].** `boot 40010` leaves both copies in the ACC
  (`lab_boot=place; step=gameplay; mission=40010; via=acc`; frame `host-relief-r2-acc-0`: Snake seated in the heli,
  no menu). `press tab` opens the iDroid on its MAP tab (frame `acc-tab`: MOTHER BASE / MAP / MISSIONS, tabs switched
  with keys 1 and 3, GMP 313850); a second Tab closes it (`acc-tab2`). The MISSIONS tab, where a sortie prep starts,
  needs key 3, and the worker's virtual keys do not include it (`Invoke-WorkerScenario.ps1:273`,
  `Send-WorkerCommand.ps1:49`, `native/worker_scenario.cpp:17`: w a s d enter space escape e r shift ctrl 1 2 f c q x z
  tab lmb rmb). A scripted sortie-prep selection therefore needs key 3 added to those three lists plus a menu path of
  about ten steps [U]; it was not tried further. Nothing in M3 depends on it: `relief_arm` writes what SetRelief
  writes, and seed `coop-relief-30020` holds a real choice. The host's real sortie-prep GMP charge stays [U].
- **A mid-mission load spawns index 0 [O, `612afff7`].** In 30020 at the savannahEast_N LZ, with no relief vehicle on
  either copy (`ct=512; t=0; alive=0`), the host's `relief_load 2 1227.17 25.84 -127.05 -1.571` (the carry attitude
  the engine itself used at this LZ in `fafd3fdd` and `9f01d603`) gave 25600 alive, `GetVehicleType` 2, at
  (1227.24, 19.43, -127.05) within 8 s, `ct=514` (byte 1 set at realize), while the guest's copy still had none
  (frame `host-relief-r2-midload-host`: the jeep). The guest's own `relief_load` with the same values, 23 s later,
  gave the same vehicle at the identical pose (to 0.01 m) within 8 s; frames `host-relief-r2-midload-both` and
  `guest-relief-r2-midload-both` show the jeep on both screens. No error, no crash, GMP unchanged on both copies. This
  is the path for a guest continue or a late join (M3 item 6): a copy whose mission started without the relief
  vehicle gets it mid mission from the host's carry values.
- **The pack brings bodies the location lacks [O].** The east jeep (type 2) drew in Africa, whose free roam loads only
  the west light vehicle and truck (`612afff7`, both copies). The west tank (type 9) drew at the same Africa LZ on the
  host's copy (`fc6f1f01`: 25600 alive, `GetVehicleType` 7, at (1227.19, 19.40, -127.03), carry life 4500 in nine
  slots, ammo 100 / 500; frame `fc6f1f01-host-relief-r2-tank`). No free roam loads a tank body, so route B could draw
  neither. The guest's tank was not tested: its lever was refused (last bullet). `GetVehicleType` numbers are not
  relief types (type 9 answers 7).
- **E18 with a mid-loaded vehicle [O, `612afff7`].** At the 30020 mission end the host's stock seize credited it
  (`OnFultonVehicle(25600, ..., 13)`, `AddTempResource 13`; the host's `4wdEast` 1 -> 2 at the next census); the
  guest's was skipped (`seize_skipped; alive=1`; its `4wdEast` stayed 1). GMP 313850 on both throughout.
- **Seed `afgh-relief-lv` [O].** From run `612afff7`: the host alone armed type 2 at the ACC sortie to 30010
  (`goto_lz=1`, enemyBase_S). The host's mission-start checkpoint holds `ct=514; cp=-347.97,321.89,763.57,2.488` with
  25600 at (-347.95, 315.86, 763.58) (frame `612afff7-host-relief-r2-seed-arrival`); the guest's holds none
  (`ct=512; alive=0`). Run `fc6f1f01` booted it with `boot fast`: the host's Continue restored the jeep at
  (-347.97, 315.85, 763.60) (frame `fc6f1f01-host-relief-r2-continue-load`, at night because the time command was
  refused).
- **Harness limit [O, `fc6f1f01`; also `5874afcd`].** After `boot fast` the guest's copy reloads (the continue
  alignment; frame `fc6f1f01-guest-relief-r2-continue`: the loading tips) and then waits on the "Space: Resume Game"
  page (frame `guest-relief-r2-continue-load`). Until the next travel every guest debug command is refused with
  `SEND_REFUSED: Debug actions require a fresh owned gameplay mission generation`, so the guest's load from the seed
  values and the guest's tank lever never ran in `fc6f1f01`. A scenario that continues needs `guest press space`
  after `boot fast` [U], or a host-led travel first. The host's commands were refused for the first ~20 s too.

### 8.6 Results: M3 built, route A (2026-10-29, `wp/relief`)

M3 is built in `lua/coop/world_relief_sync.lua` on top of M1's seed infrastructure (merge of `e08dc20`, commit
`d911f0c`), behind the lab key `world_relief=0|1` (CFG `r[17]`, lab and play). The key was off by default while the
proof ran, on by default from `5a6bac8`, and is **off by default again since `4d18561`** (the review of 2026-10-29,
section 8.7: the play rehearsals behind the flip covered no drive, mission end or removal, a guest Fulton was credited
twice with VEHICLE_GONE off, and D11 was shipped undecided). Hidden-worker runs on loopback: research build,
native gun stage, copied saves, low graphics; each run closed with the baseline restored and the real saves unchanged.
Evidence: each copy's `script-observer.log` sliced at the session offsets (in the archived run folder
`%USERPROFILE%\mgs-coop-run-archive\companions\<run>`), and frames saved as JPEG in the session scratch folder
`complete/frames/relief/` as `<run8>-<role>-<label>.jpg`.

| Run | Scenario, seed | Result |
|---|---|---|
| `25b9c0ae` | `companion-relief.txt`, `lab` (build `b622340`) | Every expectation met but one, a wrong expectation (`seize_skipped; alive=1`: VEHICLE_GONE had already removed the guest's copy; now `alive=[01]`). Superseded by `d18e8fa0` |
| `1914e76c` | `companion-relief-continue.txt`, `afgh-relief-lv` | Every expectation met. The continue through the seeds, the guest's real iDroid vehicle request refused with its toast, a non-zero paint on both copies |
| `8a81f776` | `companion-relief-late.txt`, `afgh-relief-lv` | Every expectation met. Found: a mid-mission `Vehicle.LoadCarry` draws index 0 at the mission's drop point, not at the written pose (fixed in `c818a5a`, `6358845`) |
| `d3b23cca` | `companion-relief-late.txt` after the fix | Every expectation met. The late-loaded jeep is placed at the host's pose (0.25 m) |
| `b7d65be9` | `companion-relief-play.txt`, `-Mode play -WanSim bad`, `coop-relief-30020` / `lab` | Scenario error: the host's save is in Africa and the guest's in Afghanistan, so the two titles landed in different helicopters (40020, 40010) and never paired. The scenario now uses `afgh-relief-lv` for both |
| **`d18e8fa0`** | **`companion-relief.txt`, `lab`, at `1f3acd8`** (`world_vehicle_gone=1` named) | **The M3 FRAME proof. No expectation failed**, but the guest's mission-end seize-skip expectation matched its boot-travel line and the three Fulton-removal expectations matched the mission-end lines (waitlog read from the session start; section 8.7): those phases rest on reading the slice, where the real lines are. Arrival in Africa (type 2), the drive, the guest's real iDroid request, the mission end, the arrival in Afghanistan (type 1), the host's Fulton. Superseded as the proof by `5c35644d` |
| **`40c09484`** | **`companion-relief-play.txt`, `-Mode play -WanSim bad`, `afgh-relief-lv`** | **The play rehearsal. No expectation failed.** Paired in the helicopter, the seeds in play, the same jeep at the same spot on both copies, the guest's real iDroid request refused with its toast |
| **`98e4af59`** | **`companion-relief-play-default.txt`, `-Mode play -WanSim bad`, host `coop-relief-30020` (D10), guest `africa-jeep`** | **The default in play while it was on. No expectation failed.** Arrival and refusal only (no drive, mission end or removal; the host's frames show grass). Neither `world_relief` nor `world_vehicle_gone` named (the session's lab settings: `nametag`, `coop_hud` only); `relief_sync; on=1` on both; the real sortie-prep jeep of the user's 2026-10-23 two-PC save on both copies; the refusal and its toast |
| **`1a5c1fa0`** | **`companion-relief-heavy.txt`, `lab`, keys at their defaults (`world_relief` on then)** | **No expectation failed.** The west tank (type 9) in Africa and the west wheeled APC (type 5) in Afghanistan on both copies; the mission end with the tank alive on both and VEHICLE_GONE off: one credit (the host's). The guest's `seize_skipped` expectation matched its boot line (`alive=0`); the `alive=1` line of the tank's mission end is in the slice (`guest.log:2983`), not checked by the harness |

**The host's choice spawns the same vehicle on both copies [O, `d18e8fa0`].**
- The host alone arms type 2 (east LV) for its sortie (`relief_arm`, which writes what the sortie prep's SetRelief
  writes, at the ACC sortie). Its seed line `step=seed; type=2; ct=2; cp=1227.17,25.84,-127.05,-1.571; mission=30020`
  goes with the follow seeds; the guest logs `step=seed_apply; type=2; ct=2; paint=0; kind=6; mission=30020` (after
  `step=own_kept; why=seed; own_type=0` at the first seed, D8 b). Both copies log `step=arrived; type=2; vt=2;
  paint=0; x=1227.17; y=25.84; z=-127.05; yaw=-1.571; mission=30020` (host `source=carry`, guest `source=seed`), and
  `relief_census` reads 25600 at (1227.24, 19.43, -127.05) on both once it has settled. Frames
  `d18e8fa0-host-m3-relief-arrival` and `d18e8fa0-guest-m3-relief-arrival`: the east jeep behind the same tree on both
  screens. Africa's free roam never loads that body.
- The second sortie (type 1, west LV, 30010 enemyBase_S LZ): both copies `arrived; type=1; vt=1; x=-347.97; y=321.89;
  z=763.57; yaw=2.488` and census (-347.96, 315.91, 763.41) on both; frames `d18e8fa0-*-m3-relief-arrival2`: the west
  jeep in the same place on both screens. Afghanistan's free roam never loads that body.
- Paint (`1914e76c`): the host arms type 2 with `Vehicle.paintType.FOVA_1` (5; the census reads FOVA_0..2 = 4, 5, 6 and
  NONE = 0); the seeds carry the preset; both copies log `step=arrived; type=2; vt=2; paint=5` and read the preset
  array `p=5,0,0,...`. Frames `1914e76c-*-m3-relief-paint`: the same tan camouflage jeep on both screens (the default
  paint of `d18e8fa0` is olive green).
- In play (`40c09484`, `-WanSim bad`, link 176-181 ms): both copies pair in the Afghanistan helicopter (40010); the
  host's continue is a co-op travel (`step=seed; type=2; ct=514; cp=-347.97,321.89,763.57,2.488; mission=30010`); the
  guest applies it (`seed_apply; type=2; ct=514; kind=4`) and both log `arrived; type=2; vt=2; x=-347.95; y=315.86;
  z=763.58; yaw=2.495` (host `source=carry`, guest `source=seed`); census host (-347.97, 763.60), guest
  (-347.95, 763.58). Frames `40c09484-host-m3p-relief-2` (the jeep at the right edge, at night) and
  `40c09484-guest-m3p-relief-3` (the jeep beside the host).
- Continue (`1914e76c`, seed `afgh-relief-lv`, lab): the guest applies the host's carry before its own continue load and
  its engine restores the jeep: guest (-347.95, 315.86, 763.58), host (-347.97, 315.85, 763.60); frames
  `1914e76c-*-m3-relief-continue`.
- The default in play (`98e4af59`, `-WanSim bad`, no key named): the host's save is the launcher's co-op save copy
  (seed `coop-relief-30020`, decision D10: the user's own sortie-prep choice, type 2, left out in 30020 in the
  2026-10-23 two-PC session), the guest's a lab save in Africa without one. Both pair in the Africa helicopter (40020);
  the host's continue carries `step=seed; type=2; ct=514; cp=263.57,1.56,-228.88,-0.524; mission=30020`, the guest
  applies it (`seed_apply; kind=4`) and both log `arrived; type=2; vt=2; x=263.56; y=-5.52; z=-228.88; yaw=-0.521`
  (host `source=carry`, guest `source=seed`); `relief_status` reads `on=1` on both. Frame
  `98e4af59-guest-m3d-relief`: the jeep beside the guest at night; the host's four frames look down at the grass
  (no jeep in view: the host's copy is shown by its census and the `40c09484` frames).
- Heavy vehicles (`1a5c1fa0`, lab, keys at their defaults): the host alone arms type 9 (west tank) for Africa; the
  guest applies `seed_apply; type=9; ct=9` and both log `arrived; type=9; vt=7` at the drop point and census
  (1227.19, -127.03) with life 4500 in nine slots and ammo 100 / 500 on both. Then type 5 (west wheeled APC with the
  machine gun) for 30010: both `arrived; type=5; vt=5` and census (-347.87 / -347.86, 763.54), life 2800, ammo 500.
  Frames `1a5c1fa0-*-m3-relief-tank` and `-apc`: the same tank behind the same bush and the same APC on both
  screens. No free roam loads either body, so only the relief pack draws them (route B could not).

**The host drives it; the guest sees the host seated [O, `d18e8fa0`].** The guest's copy seats the host by id in its
own 25600 (`partner_ride_seat=byid; vehicle=25600; seat=0; offer=1; reason=none`, then `confirmed`), never
`reason=absent`, and the get-off ends it (`exit; vehicle=25600; seat=0; reason=foot; via=getoff`). Frames
`d18e8fa0-guest-m3-relief-boarded` and `-drive-01..08`: the host in the driver's seat as the jeep drives past the guest.
After the drive both copies read the jeep at (1170.70, 15.63, -131.43).

**Guest vehicle requests are refused with a toast [O, `d18e8fa0`, `40c09484`, `1914e76c`].** While paired in free roam
the guest's iDroid items `MSN_DROP_VEHICLE` and `MSN_DROP_LOADOUT` are inactive (`TppUiCommand.SetMbTopMenuItemActive`,
re-applied every 2 s while the iDroid is closed; `step=guard; path=idroid_drop; via=menu_inactive`). The worker walks
the guest's real path: Tab (MAP), key 3 (MISSIONS, a new worker key), Enter (SUPPLY DROP), S S S (VEHICLES), Enter.
The stock Terminal message `MbDvcActSelectNonActiveMenu` (arg `StrCode32("MSN_DROP_VEHICLE")`) logs `step=refused;
path=idroid_drop_vehicle; via=menu_inactive` and shows the toast "Only the host can bring or request a vehicle in
co-op." (`hud=toast; kind=world_note; code=13; a=4; on=1`). Frames `d18e8fa0-guest-m3-request-refused` and
`40c09484-guest-m3p-request-refused`: LOADOUT and VEHICLES marked inactive, VEHICLES selected, the toast at the top
right. The host's items are unchanged. The guest never runs the sortie prep (world follow), so its own sortie choice
never spawns [O]. In these runs the LOADOUT drop was refused too, because a loadout delivery can carry a vehicle
(SetRelief `0x1416BC4B0`, section 8.4), even when the guest's loadout holds none; since `c7c1ed3` that block is the lab
lever `relief_lab loadout on` only, until the user decides D11 (frame `5c35644d-vs-d18e8fa0-guest-idroid-loadout`).

**One mission-end credit [O, `d18e8fa0`].** At the 30020 mission end the host's stock `SeizeReliefVehicleOnClear`
Fultons its index 0 (`step=seize; alive=1`, then `OnFulton(25600, 0, 0, 13)`, `OnFultonVehicle`, `AddTempResource 13`;
the host's `4wdE` 1 -> 2 at the next census). That Fulton is a host loss to VEHICLE_GONE (`step=lost; id=25600;
cause=1`): the guest's copy removes its index 0 quietly (`step=remove; via=seize_instant; why=vehicle_gone; ok=1`,
`step=credit_skipped; fn=OnFulton`, `vehicle_gone; step=applied; ... ms=283`) and then skips its own seize
(`step=seize_skipped; alive=0`); its `4wdE` stays 1. Without VEHICLE_GONE the guest's skip meets the vehicle alive
(`alive=1`): in `1a5c1fa0` (VEHICLE_GONE off, its default) the host's seize credits the tank (`AddTempResource 22`;
the host's `tnkW` 0 -> 1 at the next census) and the guest's skip logs `seize_skipped; alive=1` (its `tnkW` stays 0;
read in the slice, see the table), and in `5c35644d` the same for the east jeep, checked by phase-scoped waitlogs
(section 8.7).
GMP 313850 on both copies throughout. The guest never runs the sortie prep, so it is
never charged [O]; the host's real sortie-prep charge stays [U] (no lever charges it).

**The host's Fulton removes it on both copies, credited once [O, `d18e8fa0` with `world_vehicle_gone=1`; `5c35644d`
with it off, section 8.7].** In 30010 the host Fultons its index 0 (lab
lever `vehicle_remove nearest fulton`: the engine's own Seize Fulton): host `OnFulton(25600, 0, 0, 14)` and
`AddTempResource 14`, `vehicle_gone; step=lost; id=25600`; the guest removes its copy with `Seize{"Instant"}`
(`step=remove; via=seize_instant; why=vehicle_gone`, `credit_skipped`, `vehicle_gone; step=applied; id=25600;
via=seize_instant; ms=250`), where VEHICLE_GONE refused `index0` before. Census `gone`: `alive=0` on both. Frames
`d18e8fa0-*-m3-relief-gone`, taken from the same spot as `-arrival2`: no jeep on either screen. The receiver's own
removal is never taken for a guest loss (no `vehicle_gone; step=watch` on the guest). A destruction takes the same
path (VEHICLE_GONE cause 2) [I; not run: the worker has no lever that destroys a vehicle]. VEHICLE_GONE itself
(`world_vehicle_gone`) is still off by default; without it the guest's reconcile removes a copy the host lacks after
15 s (`why=host_none`): fixture only when this was written, run in `5c35644d` since (section 8.7). The guest's own
Fulton of index 0 was not handled at all here: with VEHICLE_GONE off it credited the guest's Mother Base and the
host's seize credited it again (the review, section 8.7) [I; fixed and run since].

**Late join and a moved vehicle [O, `8a81f776`, `d3b23cca`].**
- In `8a81f776` the guest's continue did not bring the jeep through the seeds (its continue reload ran without them);
  the host's VEHICLE_SPAWN did: `step=host_state; alive=1; type=2`, then after 10 s `step=load; why=host_has; type=2;
  ct=514; write=1; call=1; x=-347.97; y=321.89; z=763.57; yaw=2.488` and `step=arrived; ... source=load` at the carry
  pose (the host's jeep stood on it).
- Then the host drove the jeep 25 m from the Africa drop point and left it; the guest's lab lever `relief_lab forget`
  removed the guest's copy (as a guest that joins late never had one) and the reconcile loaded it again from
  VEHICLE_SPAWN with the host's live pose (`x=1201.73; y=18.91; z=-128.63`), but the engine drew it at the drop point
  (1227.17, 25.59, -127.05): a mid-mission `Vehicle.LoadCarry` ignores the written attitude when the relief record
  already holds that type [O effect; I cause]. Frame `8a81f776-guest-m3-relief-lateload`: no jeep beside the host.
- Fix (`c818a5a`, `6358845`, tests first): after a load, a vehicle that appears more than 3 m from the host's
  VEHICLE_SPAWN pose is set there with the RIDE_FAST placement (`SetPosition{position, rotY}`; `step=placed;
  why=load`); a parked copy more than 20 m off is placed the same way (`why=far`) instead of being removed and
  reloaded. Run `d3b23cca` (the same scenario, at `c818a5a`): the host's jeep at (1201.63, 16.91, -128.66) after the
  drive; the guest's reload drawn at the drop point and at once `step=placed; why=load; ok=1`; the guest's census then
  reads (1201.48, 16.89, -128.86), 0.25 m from the host's. Frame `d3b23cca-guest-m3-relief-lateload`: the jeep beside
  the host on the guest's screen. `6358845` (any loaded vehicle more than 3 m off is placed, whatever the carry flag)
  has fixture tests only.
- The reload never sends a VEHICLE_GONE request for the copy it replaces (the guest's own removal is marked gone
  first), so the host's vehicle stays.

**D8 (b) [O logs; fixture].** As built (like M1), the guest's own values are what every save writes while the host's
are live, and its live vars get them back at its next solo `TppMission.Load`, not at unpair or the session's end:
restoring them mid mission would leave them unlike the vehicle its copy has drawn. A paint preset the guest changes
during a session is not kept [I]. Every guest save while the host's carry is live writes the guest's own values
(`step=save_own; own_type=0; live_type=2`, `d18e8fa0` and `40c09484`), and a solo load gives them back
(`own_restored; why=solo`, fixture test). The host's paint preset reaches the guest's vars but never its save. The save
contents were not read back from disk in these runs [U].

**Not shown.** Relief types 3, 4, 6..8 and 10 on both copies (types 1, 2, 5 and 9 ran); a destruction of index 0; the guest's iDroid loadout item selected (inactive in the frames; its refusal
shares the path of the vehicle item); the host's real sortie-prep selection and GMP charge [U]; route C (the host's
in-mission drop) [U]; a two-PC session.

### 8.7 Review of M3 and the fixes (2026-10-29, `wp/relief`)

An independent review of `wp/relief` at `c282538` (the review log in section 11) found the code sound on route A but
the default flip unproven. Checked first, then fixed with tests written first:

- **The default is off again (`4d18561`).** The play rehearsals behind `5a6bac8` covered only the arrival and the
  refusal, and `40c09484` named `world_vehicle_gone=1`, which is not a default. Play refuses `worldtrigger` and the
  Fulton levers [O, `world_goto.lua:31`, `world_relief.lua:293`, `world_vehicle.lua:483`], so a hidden play rehearsal
  cannot drive a mission end or a removal yet.
- **A guest-side loss of index 0 is the host's to credit (`c7c1ed3`).** With VEHICLE_GONE off (its default), the guest's
  stock `TppTerminal.OnFulton` credited a guest Fulton of 25600 on the guest's Mother Base, nothing told the host, and
  the host's mission-end seize credited it again [O code; I effect, not run before the fix]. Now a co-op guest's
  `OnFulton` never credits 25600 (`credit_skipped; why=guest_fulton`, `own_removal` or `quiet`); its own Fulton, or a
  destruction by its player (`Dead` with `att=local`, `VehicleBroken` within 10 s of a local hit), sends
  REQ_VEHICLE_GONE for 25600 whatever `world_vehicle_gone` says (`step=lost_tx`, once paired; again after 20 s without
  RECEIVED); VEHICLE_GONE never watches index 0 on the guest while relief is on, and the guest marks it gone so the
  host's announcement removes nothing else. The host (`step=lost_rx`) runs its own stock `Seize {"Fulton"}` for a Fulton
  (`step=host_fulton`; its stock `OnFulton` credits it once) or `Seize {"Instant"}` for a destruction (no credit), and
  refuses with REJECT 8 while it rides the vehicle (`step=lost_refused`). The guest ends the request when the host's
  VEHICLE_SPAWN says `alive=0` (`step=lost_done`); after a refusal or 45 s without a host loss (`step=lost_timeout`) its
  reconcile loads the host's vehicle again.
- **Reconcile (`c7c1ed3`).** A copy removed because the host had none (`host_none`) is loaded again when the host's comes
  back; placements have their own budget (6), loads and removals keep 3, reset when the host's state changes. The dead
  `cause == 2` exemption in the VEHICLE_GONE receiver wrapper is gone (index 0 is removed quietly for any cause until
  M4 sets a wreck rule).
- **Refusal (`c7c1ed3`).** The guest's VEHICLES drop stays inactive while the link drops inside the co-op mission (the
  final stop or a solo load activate it again; active is the state `TppTerminal.SetUp` leaves, `package-10.lua:675-686`
  [O]); a mission's first guard pass applies even with the iDroid open. The LOADOUT block is the lab lever
  `relief_lab loadout on|off`, off by default, until the user decides D11.
- **The harness (`c95998a`).** `waitlog` read each log from the session start, so in `d18e8fa0` and `1a5c1fa0` the
  mission-end and Fulton-removal expectations were met by earlier phases' lines (the guest's boot-travel
  `seize_skipped; alive=0` at t_ms 175869764; the phase-D lines at 176035214..262 for the three phase-E waitlogs; in
  `1a5c1fa0` the boot line at 178929715) [O, the reviewer's reading, rechecked in `slice-d18e8fa0` and `slice-1a5c1fa0`]. The real lines are
  in the slices, so those behaviours rest on reading them, not on the harness. New worker step `logmark
  <host|guest|both>`: later waitlogs of that role read from its last complete line at the mark. The seize, remove and
  credit lines now name their mission.

| Run | Scenario, seed | Result |
|---|---|---|
| **`5c35644d`** | **`companion-relief.txt` at `c95998a`, `lab`, `world_relief=1`, VEHICLE_GONE off (default)** | **The M3 FRAME proof; all 38 expectations met, each after its phase's `logmark`.** Closed, baseline restored, real saves unchanged |
| **`cbc30b91`** | **`companion-relief-guest-fulton.txt`, `lab`, `world_relief=1`, `world_vehicle_gone=1`** | **All 18 expectations met.** The guest's Fulton with VEHICLE_GONE on: one request, one credit (the host's), nothing removed twice. Closed, baseline restored, real saves unchanged |

**Run `5c35644d`, phase by phase [O, slices `slice-5c35644d`, frames `complete/frames/relief/5c35644d-*`].**
- A, Africa type 2 (east LV): both copies `arrived; type=2; vt=2; x=1227.17; y=25.84; z=-127.05; mission=30020` (host
  `source=carry`, guest `source=seed`); census (1227.24, 19.43, -127.05) on both; the guest's `guard; ... items=1`.
- B, the host drives it: the guest logs `partner_ride_seat=confirmed; vehicle=25600; seat=0` and the get-off `exit`;
  frame `guest-m3-relief-drive-02`: the host in the driver's seat of the jeep.
- C, the guest's real iDroid path: `refused; path=idroid_drop_vehicle` and `hud=toast; code=13; a=4`; frame
  `guest-m3-request-refused`: VEHICLES selected and marked inactive, LOADOUT not marked (compare
  `5c35644d-vs-d18e8fa0-guest-idroid-loadout`, where `d18e8fa0` marked both), the toast at the top right.
- D, the 30020 mission end with the jeep alive on both and VEHICLE_GONE off: host `seize; fn=SeizeReliefVehicleOnClear;
  alive=1; mission=30020`, `OnFultonVehicle`, `AddTempResource 13`; guest `seize_skipped; ...; alive=1; mission=30020`.
- E, Afghanistan type 1 (west LV): both `arrived; type=1; x=-347.97; y=321.89; z=763.57; mission=30010`; frames
  `*-m3-relief-arrival2`: the jeep at the same spot on both screens.
- F, **the guest Fultons its copy** (lab lever `relief_remove fulton`: the stock `Seize {"Fulton"}`, which ran the
  guest's stock `OnFulton(25600, 0, 0, 14)`): guest `credit_skipped; fn=OnFulton; why=guest_fulton; mission=30010`
  and `lost_tx; rid=2; cause=1; try=1; sent=1`; host `lost_rx; rid=2; cause=1`, `host_fulton; ok=1`, its own
  `OnFulton`, `OnFultonVehicle` and `AddTempResource 14` 33 ms later, `state_tx; alive=0` 3.1 s later; guest
  `host_state; alive=0` and `lost_done; rid=2; ms=3203`. The guest's log has no `OnFultonVehicle` or `AddTempResource`
  line in the whole session. Frames `*-m3-relief-gone`, from the same spot as `-arrival2`: no jeep on either screen.
- G, Africa type 3 (west truck; first run of that type): both `arrived; type=3; vt=3` at the drop point; frames
  `*-m3-relief-arrival3`: the truck on both screens. The census after the 30010 end reads host `4wdE=2; 4wdW=2`
  (start 1, 1: the D and F credits) and guest `4wdE=1; 4wdW=1`; GMP 313850 on both throughout. **The host Fultons its
  own** (`relief_remove fulton`): host `OnFulton`, `AddTempResource 16`, `state_tx; alive=0`; the guest logs
  `host_state; alive=0; type=3` and 15.05 s later `remove; via=seize_instant; why=host_none; ok=1; mission=30020` and
  `credit_skipped; why=quiet` (no credit): the reconcile path at the VEHICLE_GONE default. Frames `*-m3-relief-gone2`:
  no truck on either screen.

**Run `cbc30b91` [O, slices `slice-cbc30b91`, frames `cbc30b91-*`, grid `cbc30b91-grid-arrival-gone`].** VEHICLE_GONE on. The east jeep at the Africa drop point on both copies (`arrived; type=2; x=1227.17; z=-127.05`). The guest Fultons its copy: guest `credit_skipped; why=guest_fulton; mission=30020`, `lost_tx; rid=3`, and no `vehicle_gone; step=watch` or `step=req` for 25600 (VEHICLE_GONE's own guest path stayed out); host `lost_rx; rid=3`, `host_fulton; ok=1`, its VEHICLE_GONE `watch; id=25600; m=Fulton; att=none`, the stock `OnFulton`, `OnFultonVehicle` and `AddTempResource 13`, then `vehicle_gone; step=lost; id=25600` and `tx` 3.2 s later; guest `vehicle_gone; step=rx; id=25600` and nothing more (no `remove`, no `matched`: it was marked gone), `host_state; alive=0` and `lost_done; rid=3; ms=3269`; `vehicle_gone_status` on both `gone=25600; req=0`. After the travel to 30010 the census reads host `4wdE` 1 -> 2, guest `4wdE` 1. Frames: the jeep behind the same tree on both screens, then gone on both.

Not covered by these runs: a player's own Fulton or destruction of index 0 (the worker has the stock Seize lever only
[I that the player's Fulton runs the same `OnFulton`, `TppTerminal.lua` FultonInfo -> `OnFulton`]); the refusal guard
across a real link drop; the host refusing while it rides the vehicle and the guest's reload after a refusal, a
timeout or a returning `host_none` (fixtures); the D8 (b) save contents read back from disk; a play rehearsal of a
drive, a mission end and a removal.

### 8.8 Results: E5 and M4, vehicle losses, damage and parked vehicles (2026-10-28/29, branch `wp/vehicle-state`)

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

### 8.9 D7: the guest's copy of D-Dog cannot be hurt, follows the host's dog, and comes back (2026-10-29, branch `wp/dog-guard`)

The user's decision D7 (2026-10-29): the guest's copy of the host's dog must not be killable by anything on the guest's
PC; its health and state follow the host's dog; where a guard cannot cover a case, the copy is brought back from the
host's live state when it dies (the restore fallback). Built on `wp/dog-guard` (from `dev/oct7-continue` `e2249e4`)
behind M1's key `companion_host_owned`.

**Static reads (capstone on `085c2f82...bb45`, read-only; no Ghidra/REA session was used)** [O]:
- **No invincibility command.** The dog answers three Lua dispatchers: `0x140C7D220` (`LuaAiStayAndSnarl`, `GetPlant`,
  `SetWarpPosition`, `GetPlacedItem`; 8.2), `0x140C56770` (`SetMode`, which writes `vars.buddyDogMode` +0x2E48,
  `SetMotherBaseCenterAndRadius` and `SetEnabled`, which sets bits 59/60 of a per-instance status qword) and
  `0x140CB9CC0` (`WarpCommand`, `SetActionCommand` and two unnamed action commands, `0xBBDE553F` an enabled bit and
  `0xD673E829` a frame/time value). None takes a damage or life argument. `SetDisableDamage` (`0x15A7E67B`) appears only
  in two soldier/hostage dispatchers (`0x140E85E10`, `0x141356170`).
- **The dog's life record.** A life controller (factory `0x140CA4900`, 0x70 bytes, vtables `0x14231FD30` / `+0x20`
  `0x14231FD60`, interface hash `0xC95E9AF7A1DD` queried by the dog's setup `0x140C55BF0`) keeps one 0x20-byte record per
  dog instance, initialised to life 10000 = maximum (`0x140CA49E0`): +0 life, +2 maximum, +4 pending life damage, +6
  stun, +8 stun maximum, +0xA pending stun, +0x14 a regeneration delay (15.0 after a hit), +0x1B state (0 normal, 1
  stunned/asleep, 2 down), +0x1C a stun level, +0x1D flags. Its per-frame update (`0x140CA4B90`, secondary slot
  `0x14231FD60`) runs `0x140CA4C40` per record: flag 8 refills life, flag 4 **forces life to 0** (a death flag, not
  invulnerability), pending damage is subtracted, stun 0 makes state 1, life 0 state 2, and the change to 2 posts
  GameObject message `Dead` (`0xCFC364D6`) for id `0x2600 | index` (the E19 Dead message).
- **The dog's damage handler.** Factory `0x140C89110` (0xB0 bytes, vtables `0x14231AD00` / `+0x20` `0x14231AD30`);
  `ProcessDamages` `0x140C89510` (secondary slot 1, `0x14231AD38`; this, instance, hits of 0x30 bytes, count) turns every
  hit into pending life damage (+4) and stun (+0xA) on the instance's record. The generic
  `DamageControllerImpl::Update` (`0x140DA55F0`, the body `combat_role_patch` pins) calls it only for an instance with
  hits (`0x140DA5C77`, `0x140DA616C`: `test r13d,r13d` before the call), so skipping the call equals "no hit this frame".
  Hit +0x22 is the attacker id, +0x28 the attack id. The handler returns at once when bit 24 of a per-instance status
  dword is set; no writer of that bit was found in the dog's code [U].
- **Damage parameters** are per attack, shared by every target (soldiers, players, the dog): changing them cannot single
  out the mirror dog. Rejected.
- **So the guard is a native filter on two exact vtable slots** (`native/dog_guard.cpp`; the bodies above, the
  `DamageControllerImpl` call sites and both slot values pinned by SHA-256 in `dog_guard.hpp`; installed at startup with
  the other partner hooks, inert until armed; **correction (review, 8.10):** until `e38f2f4` "installed at startup" meant
  on every PC whose partner gate passed, whatever `companion_host_owned` said, play included; since then only under the
  key): `ProcessDamages` for instance 0 is skipped while the guest's lease is
  armed (every hit: guards' bullets and melee, explosions, vehicles, the guest's own weapons); the life update drops any
  pending damage on record 0, undoes a forced death the module did not issue, and applies the host's dog state.

**Built** (`cb848ed`, `a2f84ee`; fixtures written first: `dog_guard_tests` failed to link, `world_companion_guard_lua_check`
failed its first check on an empty chunk):
- `native/dog_guard.cpp`, `native/dog_guard_policy.cpp`, `native/include/mgscoop/dog_guard.hpp`: the two slot hooks and
  the pure policy (`Encode`, `Decode`, `Guard`, `Lab`, the lease). The guard is armed only by a **lease** that the guest's
  companion module renews: `kLeaseFrames` = 180 of the dog's own life updates, so a pause never expires it and a stopped
  Lua tick does; disarmed at mission start, end and termination. Receipt `MGS_COOP_DOG_GUARD_V1` (mission, epoch, role,
  arm, the host's byte, lab op, percent; arming and lab ops need the paired exact scope, lab ops lab mode, and the role
  must be this copy's native role, so the host can never arm). It answers the record 0 snapshot (life, maximum, stun,
  state, flags, the encoded byte) and the counters (drops, hits, writes, kills, undone deaths, the last attacker and
  attack id). Log lines `dog_guard=installed`, `dog_guard_code_gate=ok`, `dog_guard=drop;n=..;attacker=..;attack=..`
  (the first 16, then every 50th), `dog_guard=follow;byte=..;life=a>b;...;kill=..` (a new byte, a kill, an undone
  death), `dog_guard=lab;...`, `dog_guard=disarmed;why=..`.
- **The host dog's state byte** on the wire: bit 7 valid, bits 5-6 state (0 normal, 1 stunned or asleep, 2 down), bits
  0-4 life in 31ths. It rides COMPANION_POSE `state` (op 0x39) and COMPANION `state` (op 0x30), both sent 0 before:
  **no wire change** (same ops, sizes and ranges; an older host sends 0 = "not valid", and the guard then drops hits but
  follows nothing; an older guest ignores the field). `docs/world-sync-protocol.md` section 15 has the note. A byte
  change is sent at once in COMPANION_POSE.
- `lua/coop/world_companion_guard.lua`, a new world chunk spliced after `world_companion_dog.lua` (bootstrap, source
  header, `host_trial_patch_check`, `lua_checks`): the host reads its own dog's byte with every COMPANION_POSE read
  (`event=dog_guard;role=host;step=state;byte=..;state=..;life31=..`); the guest arms whenever its active buddy is the
  dog and it is paired (re-armed every 500 ms, at once on a new byte; `step=armed`, `step=drops` every 5 s while hits
  are dropped, `step=disarmed;why=..` once), and runs the **restore fallback** (shipped behaviour under the key):
  - a Dead message for 9728 on the guest while the host's dog lives (its byte not down) -> `event=dog_restore;step=dead;
    host=alive`; the dead-buddy vars come back at once to the values before the death (`step=vars`; the DOG bit stopped
    every reload in `dc761c98`); after 1.5 s the copy is sent away (`step=delete`, the module's own `DeleteBuddy`); once
    the block reads none, M1's reconcile is primed to load the host's dog now at the host's pose instead of after 8 s
    (`step=reload;n=..`, then the reconcile's own `step=reconcile;via=load`); `step=back;ms=..` when the dog reads again;
    at most three restores per mission (`step=limit`), 60 s per restore (`step=gave_up`);
  - a copy gone without a Dead message for 2 s while the host's dog is out and present primes the reconcile once
    (`step=vanished`);
  - a death while the host's dog is down is the mirrored one (`step=dead;host=down`): no send-away, no reload; the
    host's re-call brings it back through the reconcile (`step=recalled`).
- Lab levers (lab mode only, refused in play): `companion_lab dog_life|dog_stun <0..100>` (host: its own dog's record;
  life 0 is the native kill flag), `recall` (host: `UnsetDeadBuddyType(DOG)`, then `call_here dog`), `kill_mirror`
  (guest: the kill flag on its copy), `guard off|on` (guest), `link_sim on|off` (guest, added in the review resume,
  8.10: the guard module alone sees the link as lost, so it holds an armed guard and reads the host's state as
  unknown; cleared at mission start); `companion_lab dog_state` (both roles, also in play: the receipt; an armed guest
  reads by re-arming, because a guest's arm 0 is a disarm; a disarmed guest and the host read the live record).

**Runs** (hidden worker, loopback, research build, native gun stage, copied saves, low graphics,
`companion_host_owned=1`; host seed `afgh-ledge` with D-Dog, guest `afgh-horse`, the guest plays Quiet and is drawn as a
DD soldier; every run closed with the baseline restored and the real saves unchanged). Log slices cut at the session's
own process end (another agent's later run appends to the same game logs), archived with the run folders under
`%USERPROFILE%\mgs-coop-run-archive\decisions\<run>`; frames as JPEG in the session scratch folder `complete/frames/dog/`
as `<run4>-<role>-<label>.jpg`.

| Run | Scenario | What it is for |
|---|---|---|
| `3922cc80` | `companion-dog-guard.txt` (`6037827`) | E19 again with the guard; the restore fallback; no expectation failed |
| `3f69e3b7` | `companion-dog-follow.txt` (`c622f58`) | the follow (hurt, asleep, awake, down, the host's re-call), the restore filmed, the legs; one expectation failed (below) |
| `1713b0a1` | `companion-dog-guard-play.txt` (`aa405fd` tree), `-Mode play -WanSim bad` | the play rehearsal of M1 + M2 with the guard; no expectation failed |
| `606048af` | `companion-dog-follow-frames.txt` (`aa405fd`) | the same follow and restore with the guest side-on (frames of the dog on both screens), the walk and run legs again; no expectation failed |
| `f7033e2e` | `companion-dog-guard-play-fight.txt` (`3090ebe`), `-Mode play -WanSim bad` | a longer guard fight in play (about 190 s, the guest prone beside the dog); one expectation failed: no guard fired at the dog |

| # | Result | Evidence |
|---|---|---|
| Guard installed | **Both copies** `dog_guard=installed;slots=2;scope=guest_instance0_lease;disk_writes=0` and `dog_guard_code_gate=ok` in every run: every pin and both slot values matched the installed exe [O]. The host never armed (its receipts are role 0, arm 0). **Review (8.10):** the install had no key gate, so the two slots were patched on both PCs in play too (`1713b0a1`, `f7033e2e`: `mode=play` with `dog_guard=installed`), and the pass-through of an unarmed hook never handled a real hit in these runs; fixed in `e38f2f4` (installed only under the key) and the pass-through run in 8.10 | slices |
| E19 (dog), the guard | **Pass** [O]. The guest's copy, armed with the host's byte (`step=armed; reason=ok; byte=159`), was held 1 m from the prone guest toward a guard in an alert (`near_guard shield`, as `d937191f`/`dc761c98`, which killed it after 76 s and 13 s) for about 85 s of fire: 50 hits dropped (`dog_guard=drop`, attacker 1116, attack id 333; `step=drops` every 5 s), `dog_state` read `life=10000; state=0` after about 60 s and 85 s, no `Dead` and no `companion_hurt` for 9728 on the guest's copy until the lab's own kill later; `buddy_status` `kdog`, `hurt0`. Frames: the copy standing over the prone guest after 8 s and 33 s of fire (`3922-guest-grd-shield-1`, `-3`), crouched beside it after 85 s (`-5`); the host's frames from the shield (`3922-host-grd-shield-1`, `-3`, `-5`) show the prone guest under fire but **not the host's dog** (correction, review 8.10: an earlier version said they showed it elsewhere, unhurt); that the host's dog was unhurt rests on the host's logs only (its byte steady at 159; the shield lever moves only the guest's copy). Not separately measured: explosions, vehicles and the guest's own weapons go through the same `ProcessDamages` call [I, static: the only caller is the generic damage controller] | `3922cc80` |
| Restore fallback | **Pass, about 2.6 s** [O]. `companion_lab kill_mirror` (the native kill flag on the guest's copy, as a death the guard cannot see) while the host's dog lives: `Dead` for 9728 -> `dog_restore; step=dead; host=alive` in the same ms -> `step=vars` (`deadBuddyType` 4 -> 0, `buddyCommonFlag` 0 -> 32) 84 ms later (and, review 8.10, again after the send-away in every run, `dead=0_0; flag=0_32` 0.1 s after `step=delete`, against the stock game's own clear of that flag; the repair is one-shot and DOG-bit-only since `d413778`) -> `step=delete` at 1.5 s -> `step=reload; n=1` and M1's `reconcile; via=load; kind=dog; have=none; ok=1` at 1.7-1.8 s -> `step=back; ms=2585` (both runs: 2585 ms), the guard armed again, `kdog`, held 22-24 cm from the host dog's pose. On the guest's screen the copy is carried off by the stock Fulton balloon at the death ("D-Dog" extraction, `3f69-guest-rst-dead`) while the host's dog stands beside Snake on the host's (`3f69-host-rst-dead`); in `3f69e3b7` the host's body hid the returned dog on the guest's screen, so `606048af` filmed it side-on: **FRAME on both copies** before (`6060-*-rst2-before`: the dog beside the host on both), at the death (`6060-guest-rst2-dead`: the copy carried off; `6060-host-rst2-dead`: the host's dog unaffected beside Snake) and after the restore (`6060-guest-rst2-back`: the copy standing beside the host again; `6060-host-rst2-back`). Same timing in all three runs (`back; ms=2585`) | `3922cc80`, `3f69e3b7`, `606048af` |
| Follow: hurt | [O] host `companion_lab dog_life 30` -> `dog_guard=lab;op=1;pct=30;life=10000>3000` -> host `step=state; byte=138; state=0; life31=10` -> guest `dog_guard=follow;byte=0x8a;life=10000>3226` -> guest `dog_state` `life=3230`; the host's regeneration followed (`byte=0x8b` -> 3549, `0x8c` -> 3871). Frames: the guest's copy standing beside the host at 30 % (`3f69-guest-fol-hurt`, `6060-guest-f2-hurt`); on the host's screen the host's body hides its dog (marker only, `*-host-*-hurt`). That a hurt D-Dog looks or moves differently is [U] (no limp seen) | `3f69e3b7` |
| Follow: asleep and awake | [O] host `dog_stun 0` -> host `state=1; byte=170` -> guest `follow;byte=0xaa;...;stun=10000>0` -> guest `dog_state` `stun=9; state=1`; host `dog_stun 100` -> host `state=0` -> guest `stun=8>10000` -> `state=0`. Frames: the `STN` (stunned) marker over the dog on both screens (`3f69-*-fol-asleep`; bodies hidden by grass and the host) | `3f69e3b7` |
| Follow: down (the host's dog dies) | **Pass, FRAME on both copies** [O]. Host `dog_life 0` -> host `Dead` for 9728 (t=202395226) and `byte=192; state=2` -> guest `dog_guard=follow;byte=0xc0;...;kill=1` -> guest `Dead` 101 ms after the host's -> `dog_restore; step=dead; host=down` (no send-away, no reload; vars repaired). Both screens show the notice "D-Dog Near Death" and the dog carried off by the Fulton balloon from the same spot (`3f69-host-fol-down`, `3f69-guest-fol-down`; again `6060-*-f2-down`): the stock retreat, on each copy its own | `3f69e3b7`, `606048af` |
| The host's re-call | [O] host `companion_lab recall` -> `recall;dead=4>0;call_here;kind=dog;result=load` (the host's own dead DOG bit cleared, the dog loaded 5 m in front) -> guest M1 `reconcile; via=load; kind=dog; have=none; ok=1` 8 s later (its debounce) -> `dog_restore; step=recalled; ms=36052` (from the death) -> `target_submitted`, `kdog` on both, armed with `byte=159`. **FRAME on both copies** in `606048af` (`6060-*-f2-recall`: the dog back beside Snake on both screens; `recalled; ms=36069`) | `3f69e3b7`, `606048af` |
| Legs on the shipped hold | **Pass, FRAME on both copies** [O]. `606048af` (the host's dog called beside the host first, the guest watching side-on): marked walk windows 0.35 and 1.09 m, run windows 1.07 and 2.57 m (`within=1`); **not all within the limits** (correction, review 8.10): in the run2 leg one walk window as the dog stopped after the run read 3.73 m (`within=0`, t=203602482) and in the restore leg an idle window about 9.5 s after `step=back` read 3.65 m (`within=0`, t=203618383, cause [U]); frames `6060-*-f2-walk` and `6060-*-f2-run` show the dog with the host on both screens. Unmarked windows of the other runs: walk 0.20-1.85 m (lab), 0.37-2.93 m (play), run 0.16-1.46 m (lab), 1.46-2.77 m (play); the first window after a teleport reads 17-37 m (the hold's warp beyond 15 m). `3f69e3b7`'s marked walk leg failed its waitlog because the host's dog stayed where it was (idle windows `speed_cms=0`, 2.17-2.32 m, 21 drift warps: the copy is warped back to an unchanged target 2.2 m off, cause [U]) | `606048af`, `3f69e3b7`, `3922cc80`, `1713b0a1` |
| Play rehearsal (M1 + M2 with the guard) | **Pass** [O] (`1713b0a1`, `-Mode play -WanSim bad`, link RTT 175-187 ms, loss up to 12 %): play's title Continue, pairing in the helicopter, the host's seeds and the guest's `loaded; kind=dog; source=seed; mission=30010`, the guard armed with the host's byte (`byte=159`), walk and run windows within (above), `kdog` on both; the host's `cp_phase nearest alert` reached the guest's copy (`phase=3; src=rx; applied=1`) and both screens show "Spotted unidentified enemy combatant. Currently engaging!" with the dog beside Snake (`1713-*-gp-fight-1`, `-2`, `-3`); `dog_state` `armed=1; life=10000; state=0` twice, no `Dead`. **No guard fired at the dog in the 55 s window** (`drops=0`). `f7033e2e` tried again with the guest prone beside the dog and about 190 s of alert: its only failed expectation was that waitlog (`drops=0`; the guest's copy logged the host's alert as `phase=3; src=rx; applied=0` this time, and no guard reached the players); both screens show the dog standing over the prone guest beside Snake throughout (`f703-*-pf-fight-1..3`), `dog_state` `armed=1; life=10000; state=0`, no Dead. The guard's drop is proven in the lab only. Play refuses the follow, restore and re-call levers, so those paths have no play rehearsal | `1713b0a1`, `f7033e2e` |

### 8.10 D7 review fixes (2026-10-29, branch `wp/dog-guard`)

An independent review of `wp/dog-guard` at `8a294ce` (verdict: fix first) raised one major and four minor findings
and refuted four claims. Each finding was checked against the code before any change; all five held. Fixes, each with
a test that failed first (red logs kept in the session scratch folder `decisions/dog/`: `fix-red-native.log`,
`fix-red-lua.log`, `fix-red-lua2.log`; for the resume's lab lever and log marker `fix-red-lua3.log`, `fix-red-lua4.log`):

| # | Finding (review) | Verified | Fix | Commit |
|---|---|---|---|---|
| 1 (major) | The two vtable slots were installed on every PC whose partner gate passed, play included, whatever `companion_host_owned` said; the unarmed `ProcessDamages` pass-through never handled a real hit | **Yes** [O]: `dog_guard_ok = partner_gate_ok && Install(...)` had no key gate; `1713b0a1` / `f7033e2e` logged `mode=play` with `dog_guard=installed` on both PCs | `dog_guard::InstallRefusal`: installed only under the key (both roles); with the key off the observer logs `dog_guard=off;reason=key_off;slots=0` and neither slot is touched. The pass-through was run (below) | `e38f2f4` |
| 2 | Any link or scope loss dropped the guard at once (E19's failure back during a WAN drop), and a death while the host's state was unknown was filed as "host down" (mirrored: no send-away, no reload) | **Yes** [O]: Lua armed only while `W.paired and eff>=3`; the native refused `scope` and disarmed when `paired`, `link_up`, `effective` or the epoch failed; `hostState` returned alive=false with no fresh source | Native `dog_guard::Admit`: a full arm still needs the paired exact scope, but through a link loss an **armed** guest's lease is **held** drop-only (reply `reason='hold'`, follow 0, no lab op) until the session ends (the guest's Lua stops renewing at `C.endSession`) or the mission does (native `Disarm`); receipt arm 2 = hold, which never starts the guard. Lua: an unpair no longer disarms; an armed guest sends the hold every 500 ms while unpaired (`step=held`). `hostState` now says whether the host's state is **known**: a death with no fresh source is `step=dead;host=unknown` and waits (no send-away, no reload, no mirror) until it is known (`step=host_known;host=alive|down`), 60 s at most; a stale tick no longer flips a pending restore to mirrored | `e38f2f4`, `d413778` |
| 3 | `repairVars` rewrote both whole words every 100 ms for 10 s after a death, fighting the stock game's `buddyCommonFlag` clears (after `DeleteBuddy`, at the retreat) | **Yes** [O]: the second `step=vars; dead=0_0; flag=0_32` 0.1 s after `step=delete` in all three restore runs, and at +9.3 s in both mirrored deaths | Only the DOG bit of `deadBuddyType` (the bit that stops every reload: `dc761c98`, `033193d3`) is cleared: once after the death (`step=vars;why=death;dead=a>b;ok=1`) and once more before a primed reload if it is set again (`why=reload`); `buddyCommonFlag` is the game's | `d413778` |
| 4 | The reply's record fields were not age-gated (a deleted dog's record was reported after a reload: `life=0; state=2` at 3922 201073761); the native follow log cap never reset; `dropLogs` was unused | **Yes** [O] | The reply leaves out the record fields unless the snapshot is under 1 s old (`dog_guard::Fresh`; the Lua then logs `life=nil`) and reports `fresh=`; `companion_lab dog_state` on a disarmed guest reads the live record (arm 0) instead of the last arm's reply and names `installed=` and `fresh=`; the drop and follow log caps are per mission (reset by `Disarm`), `dropLogs` is that drop counter. **Residual** (resume, run `9fa48400`): the age gate alone does not cover the first second after a send-away: the re-arm 0.9 s after `step=delete` read `life=0; state=2` as fresh (record 0 was still the dead copy's while the reload ran; 100 ms later the copy read again and `dog_state` gave `life=10000; state=0`). The armed / held / refused and drops lines now end with `restoring=0|1` (1 while a restore is in flight) | `e38f2f4`, `5ef2470`, `19280d3` |
| 5 | The fixture lacked the risky branches (stale host state, link loss, play refusals of `recall`, `kill_mirror`, `guard`, a stock writer after the repair) | **Yes** [O] | `world_companion_guard_lua_check` 65 -> 99 checks: hold and its 500 ms renewal, re-pair, no arm while never armed, a refused hold, the session end, the native's own hold, a stale record, the unknown / known-alive / known-down deaths, a stale tick on a pending restore, a host known to have no dog out, the one-shot DOG bit (a stock flag writer over 8 ticks: one vars line; another type's dead bit kept; the bit set again before the reload), play refusals of `dog_stun`, `recall`, `kill_mirror` and `guard`; `dog_guard_tests` adds the install gate, 30 admission cases, `Fresh` and the log caps. The play refusals already held in the code (regression checks, not red). Resume: 99 -> 115 (the `link_sim` lab lever: hold while paired, renewal, an unknown-state death and its restore, no arm before a first arm, cleared at mission start, guest-only, lab-only, arguments; the `restoring=` marker) | `e38f2f4`, `d413778`, `5ef2470`, `c256368`, `19280d3` |

Refuted claims corrected in 8.9 and in STATUS (`3d0046c`): "Defaults: unchanged" / "inert" (finding 1); the E19 host
frames (they show the prone guest, not the host's dog: "unhurt" rests on the host's logs); the vars repair "84 ms
later" (it also ran after every send-away); `606048af`'s legs "all within" (a 3.73 m walk window and a 3.65 m idle
window after the restore read `within=0`).

**Runs** (hidden worker, loopback unless named, research build, native gun stage, copied saves, `afgh-ledge` host /
`afgh-horse` guest; every run closed with the baseline restored and the real saves unchanged; archived under
`%USERPROFILE%\mgs-coop-run-archive\decisions\<run>`; frames as JPEG in the session scratch folder
`complete/frames/dog/<run4>-<role>-<label>.jpg`):

| Run | Scenario | What it is for |
|---|---|---|
| `7fe2845e` | `companion-dog-guard-review.txt` (`5ef2470`), lab, `companion_host_owned=1` | E19 through the new admission; the guard turned off in the line of fire (the pass-through, a real death, the restore with the one-shot repair); the kill restore again. One expectation failed: a waitlog for a dog `Damage` message (below) |
| `19e438fe` | `companion-dog-guard-play-off.txt` (`8afc2f5`), `-Mode play -WanSim bad` (RTT 176-187 ms), **default keys** | the play default after finding 1: nothing installed on either PC; no expectation failed |
| `cc2e15d2` | `companion-dog-guard-review-frames.txt` (`8afc2f5`), lab, `companion_host_owned=1` | the same paths with the host's dog called beside the host: the kill restore, the host's dog down and re-called, E19 with the guard off; no expectation failed |
| `9fa48400` | `companion-dog-guard-hold.txt` (`61db3cb`), lab, `companion_host_owned=1` | resume: the hold and the unknown-host-state wait at runtime through the guest's lab lever `companion_lab link_sim on` / `off` (`c256368`), which makes the guard module alone see the link as lost (the world link stays up); no expectation failed |

| # | Result | Evidence |
|---|---|---|
| Install gate | **Pass, both copies** [O]. Default play session: `dog_guard=off;reason=key_off;slots=0` on host and guest, no `dog_guard=installed` line, and `companion_lab dog_state` answers `ready=false;reason=admission;...;installed=0` on both (the host twice, before and during an alert); the host's own D-Dog fights beside Snake on the host's screen with the stock code ("Currently engaging", `19e4-host-po-fight-1`), the guest's screen shows the host ahead (`19e4-guest-po-fight-1`). With the key on (`7fe2845e`, `cc2e15d2`) both copies log `dog_guard=installed;slots=2` and `dog_guard_code_gate=ok` as before | `19e438fe`, `7fe2845e`, `cc2e15d2` |
| E19 through the new admission | **Pass** [O]. Armed `reason=ok; byte=159`; with the shield 13 hits dropped in about 22 s (`7fe2845e`) and 4 in 6 s (`cc2e15d2`), attacker 1116, attack 333, `dog_state` `life=10000; state=0`, no Dead while armed; FRAME of the copy standing over the prone guest (`cc2e-guest-pv-shield`, `7fe2-guest-grd-shield-2`) | `7fe2845e`, `cc2e15d2` |
| The unarmed hook passes hits through (the guest's PC) | **Pass** [O]. With `companion_lab guard off` (the lease disarmed, both hooks still installed) and the copy still held in the line of fire, it died a real death: `obs; m=Dead; id=9728; att=other` 70.7 s (`7fe2845e`) and 78.3 s (`cc2e15d2`) after the guard went off, as without the guard before D7 (`d937191f` 76 s, `dc761c98` 13 s), against no death in the guarded windows. FRAME: the copy lying dead in the grass beside the prone guest (`cc2e-guest-pv-dead`). The dog posts no `Damage` message the Lua observer sees (the failed waitlog of `7fe2845e`; none in the pre-D7 E19 runs `d937191f`, `dc761c98` either), so the life falling is not logged step by step [O absent]. The host's PC runs the same unarmed branch; the host's dog took **no** real hit in any run (its byte 159 throughout `7fe2845e` and `cc2e15d2`; in `cc2e15d2` it stood 30 m off, `cc2e-host-pv-dead`), so the host-side pass-through is [I] from the shared code path | `7fe2845e`, `cc2e15d2` |
| Restore with the one-shot repair | **Pass, four restores** [O]: the guard-off death and the kill in each run: exactly **one** `step=vars; why=death; dead=4_0; ok=1` per death, 85-105 ms after it, no other vars write; `step=delete` at 1.5 s, `step=reload`, `reconcile; via=load`, `step=back; ms=` 2485, 2485, 2552, 2669. `mirror_vars` shows the game clearing `buddyCommonFlag` 32 -> 0 about 0.9 s after the first death and leaving it 0, and **every later reload worked with the flag at 0** (so bit 5 does not gate the reload [O]; its meaning stays [U]). FRAME on both copies (`cc2e15d2`): the copy carried off by the Fulton balloon on the guest's screen (`cc2e-guest-rv-dead`), back beside the host (`cc2e-guest-rv-back`), the host's dog beside Snake on the host's (`cc2e-host-rv-back`) | `7fe2845e`, `cc2e15d2` |
| Mirrored down and re-call, one-shot repair | **Pass, FRAME on both copies** [O] (`cc2e15d2`): host `dog_life 0` -> host Dead (208761080) -> guest `dog_guard=follow;byte=0xc0;...;kill=1` -> guest Dead 197 ms later, `step=dead; host=down`, one vars line; "D-Dog Near Death" and the extraction on both screens (`cc2e-*-rv-down`); the host's `recall` -> guest `reconcile; via=load` -> `step=recalled; ms=36703` (with `buddyCommonFlag` 0) -> the dog beside Snake on both screens (`cc2e-*-rv-recall`) | `cc2e15d2` |
| Fresh record only | [O] the host's first state line in each run and the line right after its re-call read `byte=0; ...; life=nil; max=nil` (the new dog's record not yet updated), then `byte=159; life=10000`; the guest's arm after each reload reads the reloaded copy's fresh record (`life=10000; state=0`) | `7fe2845e`, `cc2e15d2` |
| Hold under guard fire (`link_sim`) | **Pass** [O] (`9fa48400`). The copy armed with the host's byte in the E19 shield (1 hit dropped), then `link_sim on` (212088794): `step=held; reason=hold; byte=0` 83 ms later (receipt arm 2, native `dog_guard::Admit` grant hold, follow 0) and `dog_state` `reason=hold; armed=1; life=10000; state=0` twice; the guard kept firing and the hold kept dropping: drops 4, 8, 11, 14 in `step=drops` lines and 16 when the guard went off about 25 s later, attacker 1116, attack 333, `life=10000; state=0`, no Dead. FRAME: the copy standing over the prone guest with the guard aiming at them on the guest's screen during the hold (`9fa4-guest-hd-held-1`, `-2`); the host's screen shows the prone guest under fire (`9fa4-host-hd-held-1`; the copy exists only on the guest's PC). That a **real** unpair or link drop takes this path is [I]: the lever skips only the guard module's link test (`W.paired`, `eff`) and its host-state sources; the unpair itself is covered by the fixture and `dog_guard_tests` (no worker lever cuts the world link, no run lost it) | `9fa48400` |
| Unknown host state at a death (`link_sim`) | **Pass** [O] (`9fa48400`). `guard off` under `link_sim` (212113667), the copy still in the line of fire: a real death 71.5 s later (`Dead; att=other`) -> `step=dead; host=unknown; byte=0; armed=0` -> one `step=vars; why=death` (+52 ms) -> **nothing for 5.1 s** (no send-away, no reload, no mirror) -> `link_sim off` -> `step=host_known; host=alive; ms=5171` -> `step=delete` in the same tick -> `step=reload; n=1` -> `reconcile; via=load` -> `step=back; ms=6172`; `kdog` on both, `dog_state` `life=10000; state=0; restores=1`. FRAME: 1.5 s after the death the stock retreat lifts the copy away on the guest's screen (`9fa4-guest-hd-unk-dead`), 4.5 s after it the copy is gone and the module has not sent it away (`9fa4-guest-hd-unk-wait`); the host's screen shows no message (`9fa4-host-hd-unk-dead`) and later its dog beside Snake (`9fa4-host-hd-back`). The guest's frames after the restore face away from the copy (`back` rests on the logs) | `9fa48400` |
| Heroism at a copy's death (new) | [O] (`9fa48400`): when the guest's copy dies on the guest's PC the stock game shows "D-Dog Near Death [Heroism -30]" there (`9fa4-guest-hd-unk-dead`; not on the host's screen). So every death the guard cannot see (the restore cases) costs the guest's PC 30 heroism; whether that reaches the guest's own save is [U] (D8 (b) restores the buddy vars only). The mirrored down shows it on both PCs (`cc2e-*-rv-down`), where the host's dog really fell. **Since 8.16 (user decision 2026-10-29) the guest gets it back** | `9fa48400`, `cc2e15d2` |
| Host-side pass-through | [I], answered from the code: `ProcessHook` drops only when `count && index==0 && Armed(...)`; nothing in it depends on the role, and a host can never arm (`dog_guard::Admit` returns `role` for an arm from a host, `Control` arms only `q.guest && q.arm`), so on the host's PC every hit takes the same `originalProcess` branch that the guest's unarmed hook took to a real death (`7fe2845e`, `cc2e15d2`, `9fa48400`). The host's dog took no real hit in any run (its byte 159 throughout). With the key off nothing is installed on either PC (`19e438fe`) | `7fe2845e`, `cc2e15d2`, `9fa48400`, `19e438fe` |
| Log caps per mission | [I] code read (`Disarm` resets them); no run reached a second mission | - |

**Still open after the fixes:** the hold and the unknown-state restore are proven at runtime only through the lab
lever (`9fa48400`); a real link loss or unpair with the dog held was never run (no worker lever cuts the world link);
the guest's PC loses 30 heroism at each death of the copy (above; given back since 8.16);
the host's dog never took a real hit with the hooks installed; the guard's drop, the follow, the restore and the
re-call still have no play rehearsal under the key (play refuses their levers, and natural guard fire at the dog has
not happened in play); which native code sets the kill flag in a stock death, and whether undoing one the module did
not issue could leave a stuck live copy, stay [U]; deaths the guard cannot see other than the lab kill and the
guard-off fire (fall, drowning, out of bounds) were not run; no Mother Base run with the guard; no two-PC session.
`companion_host_owned` stays **off in lab and play**; no play default changed.

### 8.11 Results: the vehicle decisions D11, D12, D14 (2026-10-29, branch `wp/vehicle-decisions`)

The user's decisions of 2026-10-29 (section 10) are built in a new world chunk, `lua/coop/world_vehicle_rules.lua`
(spliced after `world_vehicle_state.lua`; `docs/world-sync-protocol.md` section 19), each behind its own lab and play
key in the CFG r[15] vehicle bitmask (bit 8 `world_vehicle_break`, bit 16 `world_vehicle_credit`, bit 32
`world_loadout_block`). No wire change: D12 reuses REQ_VEHICLE_GONE / VEHICLE_GONE / REJECT, D14 only changes how a
receiver removes its copy, D11 is local to the guest. Tests first (`world_vehicle_rules_lua_check`, 67 checks; the
r[15], LOADOUT and lab-config checks), then hidden runs (research build, native gun stage, copied saves, low graphics;
each Closed, BaselineRestored, RealSavesUnchanged, no `step=error`, `viewer_changed` or fault line; `Forced=True` at
shutdown as in the earlier play runs and the lab runs here). Slices in the session scratch `decisions/vehicles/slice-<run8>`,
runs archived under `%USERPROFILE%\mgs-coop-run-archive\decisions\<run>`, frames as JPEG in `complete/frames/vehicles/`
(`<run8>-<role>-<label>.jpg`; contact sheets `g-c02-*`, `g-701-*`, `g-cf0-*`).

| Run | Scenario, mode | Result |
|---|---|---|
| `cf0aeeba` | `vehicle-decisions-lab.txt`, lab, `africa-jeep`, every vehicle key named | Both D14 Fultons met every expectation (frames `g-cf0-guestfulton`, `g-cf0-hostfulton`); stopped at D12's `placebehind` (the guest's 900 m `goto_vehicle` warp alone: no valid partner pose yet, as `e20f76bc`); fixed in `bbdc687` (both go to the jeep first) |
| **`c0203fdd`** | **the same, lab, at `bbdc687`** | **The FRAME proof. Every expectation met.** D14 both directions with the commit readback, D12 (a lever break) wrecked on both screens, D11 in 30010 |
| **`70146170`** | **`vehicle-decisions-play.txt`, `-Mode play -WanSim bad`, the three new keys named (off by default then), VEHICLE_GONE / VEHICLE_STATE at their play defaults** | **The play rehearsal. Every expectation met** (link 175-185 ms). D14 both directions, D12 with the guest's real C4, D11 with the toast drawn |
| `243ae269` | `vehicle-decisions-refuse.txt`, lab, `africa-jeep`, `world_vehicle_break=1` | Stopped at a `placebehind` behind the seated host (a seated partner is no landing pose); fixed in `73313b0` |
| **`09333f05`** | **the same at `73313b0`** | **D12's refusal, live. Every expectation met.** The host seated in 25608, the guest's lever break of its copy -> host `break_judge; ok=0; why=ridden; by=local_rider` -> REJECT 8 -> guest `break_refused; here=wreck`; frames `g-093-refuse` |
| **`47b95ae7`** | **`vehicle-decisions-play.txt` at `f4ec5c1`, play, `-WanSim bad`, no vehicle key named (the shipped defaults)** | **Every expectation met, `Forced=False`.** Both copies `vehicle_rules; step=on; ...; break=1; credit=1; loadout=1; gone=1` from the play defaults; the same D14 (receiver Seize 150 / 133 ms), D12 (judge `near; d_mm=13842`, host wreck 200 ms) and D11 lines (toast drawn) as `70146170`; frames `g-47b-default` |

**D14: a world vehicle Fultoned by either player is credited to both co-op saves, once each [O, `c0203fdd`,
`70146170`, `cf0aeeba`].**
- The guest Fultons veh_lv_0000 (25608) by hand at Masa Village ("hold E: Fulton"): its own engine credits it
  (`event=relief; step=credit; fn=OnFulton; args=25608,0,0,14`, `OnFultonVehicle`, `AddTempResource; args=14`, and
  `event=vehicle_rules; step=credit; fn=OnFultonVehicle; id=25608; res=14; via=own; role=guest`), VEHICLE_GONE's
  `watch; m=FultonInfo; att=local` -> `lost` 3.2 s later -> `req; cause=1`; the host's receiver (`rx_req`) runs
  `credit_seize; id=25608; role=host` (the stock `Seize {"Instant"}`), its engine posts the Fulton and credits it on the
  host's copy (`OnFulton(25608, 0, 0, 14)`, `OnFultonVehicle`, `AddTempResource 14`, `credit; via=receiver; role=host`),
  `applied; id=25608; via=seize; src=req; ok=1` 266 ms after the request (`c0203fdd`; 241 ms in `cf0aeeba`, 133 ms in
  play), and the guest's request ends (`req_done; result=gone`).
- The host Fultons veh_lv_0002 (25610) by hand from its `goto_vehicle` door spot: the host's own credit (`via=own`),
  `tx; cause=1; actor=0`; the guest's receiver `credit_seize; id=25610; role=guest`, its engine's `OnFulton(25610, ...,
  14)` -> `AddTempResource 14` (`via=receiver`), `applied; via=seize; src=rx; ok=1` 275 ms after the `rx` (116 ms in
  play).
- The commit readback (`relief_census`, `GetResourceUsableCount`): before anything `4wdW=1; gmp=313850` on both copies;
  after both Fultons, before the commit, `4wdW=1; gmp=303850` on both (the temporary resources; each copy paid the stock
  -10000 "Fulton Recovery Expenses" for its own hand Fulton only, the receiver's Seize charged nothing); after the lab
  goto to 30010 (the mission end commits them) `4wdW=3` on **both** copies, GMP unchanged: each save +2, its own
  Fulton and the other player's, once each [O, `c0203fdd`]. In play (`70146170`) the same lines and the same
  pre-commit census (`4wdW=1`, GMP 313850 -> 303850 on both); play has no mission end in the worker, so the commit
  there is [I] from the lab.
- Frames: `c0203fdd-*-d14-guest-before` / `-after` and `-d14-host-before` / `-after` (the jeep on both screens, then gone
  on both), `-lift-NN` (the balloon on the Fultoning copy's screen only); the same in play (`g-701-d14`).
- Each copy logged exactly one `OnFultonVehicle` per jeep (two per copy in all), no `credit_missing`, no `fallback`.
  The sortie vehicle (index 0) is never seized for a credit (fixture: VEHICLE_GONE refuses it, M3 keeps its host-only
  credit) [fixture]. A receiver's Seize that does not take within 2 s falls back to the Despawn and logs
  `credit_missing` [fixture]; a receiver whose copy of the vehicle is far and unrealized was not run [U].

**D12: a vehicle the guest destroys is judged by the host and wrecked on both copies [O, `c0203fdd`, `70146170`].**
- Lab (`c0203fdd`): both players at the next jeep (veh_lv_0001 = 25609, behind the cargo loading zone), the host 12 m
  behind the guest. The guest's lever `vehicle_life nearest 0` (the engine's own destruction on its copy only, as an
  explosive would do, with no attribution) -> `event=vehicle_rules; step=break_seen; id=25609` -> `vehicle_gone; step=watch;
  m=VehicleBroken; cause=2; att=none` -> `req; cause=2` -> host `break_judge; id=25609; ok=1; why=near; d_mm=1899;
  life=2300; peak=2300` -> `applied; id=25609; via=wreck; src=req; ok=1; ms=66` (SetBodyLife 0 on the host's copy) ->
  `tx; cause=2; actor=1` -> guest `req_done; result=gone`. Frames `c0203fdd-*-d12-wreck-01..04` / `-after`
  (`g-c02-d12`): on the guest's screen the burning jeep; on the host's, a fire and smoke cloud rising at the jeep from
  behind the host's own body, with the jeep's visible rear half unchanged and its front never in view (corrected by the
  review of 2026-10-29: no host frame of this run shows the wrecked jeep itself; the wreck on the host's copy rests on
  `applied; via=wreck; ok=1` and the IsAlive readback) [O logs, I frame].
- Play (`70146170`, `-WanSim bad`): the guest's own C4 (three charges, detonated about 14 m away) -> `break_seen` ->
  `req; cause=2` 235 ms later -> host `break_judge; ok=1; why=near; d_mm=13842` -> `applied; via=wreck; src=req; ok=1;
  ms=216` -> guest `req_done` 233 ms after its request. Frames `70146170-guest-d12p-placed` (the jeep with the charge's
  light), `-blast-02/04`, `-after` (the explosion and the burning jeep on the guest's screen) and `-host-d12p-after` (the
  smoke column rising from that spot on the host's screen) (`g-701-d12`). Corrected by the review: the jeep is never
  visible on the host's screen in these play frames (`70146170`, `47b95ae7`), before or after; the figure at the centre
  is the host's own third-person body (the partner's Steam persona nametag below its torso), not the guest's. The host-side
  wreck rests on the logs; section 8.12 frames it.
- One host-side outcome: one `break_judge`, one `applied`, one `tx` per break; `vehicle_gone_status` `j=1; a=1; r=0`.
  A destruction has no credit or charge (GMP unchanged on both copies).
- The rule as first built (superseded by the review fixes, section 8.12: 300 m, cause 4, holds instead of refusals for
  an occupied seat, an unknown position and the rate) [fixture; the ridden refusal also O, `09333f05`]: refused at once
  while someone rides the host's copy
  (`why=ridden`; answered as gone instead when the host's copy is broken too), refused when the guest's DOWNED_STATUS position is more than 60 m from
  the reported pose (`guest_far`) or older than 3 s (`guest_unknown`) unless the host's copy is at a quarter of its peak
  life or less (`heavy`), at most 4 applied per 20 s (`rate`), never index 0; the guest asks at most 6 per 60 s and never
  for a break it caused itself (its own wreck or removal in the last 5 s). The far, unknown and rate refusals ran only in
  the fixtures. `occupied` is any filled rider[] seat on the host's copy, D-Dog or a soldier included: D-Dog stays in a
  jeep for a few seconds after the host gets out (`b31a1b65`), and a break then is refused too [I from the rule].
- **When the host refuses** [O, `09333f05`]: the host sat in the Masa Village jeep (25608, D-Dog beside it) while the
  guest's lever broke the guest's copy. Guest `break_seen` -> `req; cause=2` -> host `break_judge; ok=0; why=ridden;
  by=local_rider` and `refused; src=req; reason=ridden` (no `applied`, no wreck on the host) -> guest `req_done;
  result=reject; reason=8` and `break_refused; id=25608; ...; here=wreck`. What each copy then shows (frames
  `09333f05-*-refuse-*`, sheet `g-093-refuse`): the guest's screen the jeep burning beside the seated host's body and
  the blast knocking the guest down (godmode on), the host's screen the intact jeep with the host in it; after the host
  got out, VEHICLE_STATE's next state for 25608 was refused on the guest (`refused; reason=absent`: the named jeep is
  not alive there); 25 s later the guest's wreck had left its pool (`vehicle_state_status 25608`: `pos=0,0,0`) while
  the host's jeep stood intact (`alive=1; life=2300`). So a refused break leaves the jeep on the host's PC only, for the
  rest of the mission. No Lua command undoes a wreck: `SetBodyLife` writes the life word only and `IsAlive` reads
  rec+0x70 bits that none of the 21 instance commands is known to clear (section 3.6) [I], so there is no resync; the
  divergence is logged (`break_refused`) and stays.
- The host's wreck is the engine's destruction, blast included, on the authoritative copy: guards it kills there are
  host kills (synced as usual) and a second vehicle it breaks is a host loss (announced). Measured only with godmode on
  and the area cleared (`clear_enemies 100`) [U with live guards]; it is not the receiver wreck of `world_vehicle_wreck`
  (the guest's copy), which stays lab only.

**D11: the guest's LOADOUT drop is refused in co-op, the host's works [O, `c0203fdd`, `70146170`].**
- Guest: `event=vehicle_rules; step=guard; path=idroid_drop_loadout; via=menu_inactive` once per mission while paired
  (30020; after the lab goto again in 30010); the real iDroid path (Tab, key 3 MISSIONS, Enter SUPPLY DROP, S S LOADOUT,
  Enter) -> `step=refused; path=idroid_drop_loadout; via=menu_inactive` and `hud=toast; kind=world_note; code=13; a=5`.
  In play the toast is drawn (`on=1`; the lab's HUD is off unless named, `on=0`): frame `70146170-guest-d11p-guest-refused`
  shows LOADOUT marked inactive and selected, and at the top right "Only the host can order a loadout drop in co-op: it
  can carry a vehicle." Nothing else in the guest's iDroid changed: VEHICLES is not marked (`world_relief` off, its
  default), the other items as before (frames `*-d11*-guest-loadout`).
- Host: no guard or refusal line on the host (its item is never touched); its real iDroid path to LOADOUT shows the item
  active and Enter opens the LOADOUTS list (LOADOUT 1-3 with their GMP costs; frames `*-host-d11*-host-loadout-open`, lab
  and play). The drop itself was not ordered.
- Not tied to `world_relief` [O static, section 8.4; I effect]: a loadout delivery calls the vehicle manager's SetRelief
  (`0x1416BC4B0`, loadout vehicle ids 0x0D..0x16), and with `world_relief` off each copy's index 0 is its own (`5874afcd`:
  the guest's copy had none when only the host armed one), so a vehicle a guest loadout brings would exist on the guest's
  copy only. A guest loadout holding a vehicle was not run (the block makes the question moot).
- The guard also ran in mission 1 (the title, paired early by the fast boot) and the ACC (40020), unguarded at their stop
  [O, `cf0aeeba`]: harmless (no supply drop there; `TppTerminal.SetUp` resets the items).

**Play defaults.** All three keys are on in play unless named (`f4ec5c1`) and off in the lab unless named: each acting
path has its FRAME on both copies (`c0203fdd`) and a passing `-Mode play -WanSim bad` rehearsal (`70146170`, keys
named) and again at the shipped defaults with no key named (`47b95ae7`). D12 and D14 act only where VEHICLE_GONE runs (30010 and 30020 in play; Mother Base lab only).

**Open.** D12's far, unknown and rate refusals live (only `ridden` ran, `09333f05`); the host's wreck blast with live guards and godmode off; a guest's
unattributed break far from the host's realized area; D14 for an APC, a tank or a truck (only the light vehicle, resource
14, ran); a receiver whose copy is unrealized; the guest's VEHICLES drop with `world_relief` off (still active: M3's block
rides on `world_relief`; D11 as decided covers LOADOUT); a two-PC session. (After the review: the far refusal, the blast
measurement and the VEHICLES block are in section 8.12.)

### 8.12 Results: the D11 / D12 review fixes and the blast rule (2026-10-29, branch `wp/vehicle-decisions`)

The independent review of `7d8c95e` (section 11) returned `fix_first`; the user took the reviewer's picks for the two
open questions (section 10, D11 and D12). Built in `8b00f80` (the review fixes, tests first: `rv/fast-failfirst.log`,
the rules, vehicle, relief-sync and wire checks failed before the code) and `0e79337` (the blast rule below, tests first:
the rules check failed at check 3). What changed:
- D12 judge: only the host's own player in its copy refuses at once (`ridden`, REJECT 8); another filled seat (D-Dog, a
  soldier), the partner's ride, an unknown guest position and the rate are holds (VEHICLE_GONE's 250 ms re-check,
  REJECT 9 after 120 s). "Close enough" is 300 m, judged on the closer of the guest's position when the request arrived
  and its current one. The guest queues breaks beyond 6 per 60 s (32 at most). An unattributed break goes out as
  REQ_VEHICLE_GONE cause 4, which a host without `world_vehicle_break` refuses with REJECT 11 (wire range 1..4,
  `docs/world-sync-protocol.md` section 19). One `break_judge` line per request.
- **The blast rule** (`0e79337`): the host applies a judged destruction as the Despawn instead of the wreck while its own
  player or the partner stands within 15 m of its copy, or a position cannot be read (`step=break_safe;id=;near=host|
  partner|unknown;d_mm=;form=despawn`). Why: run `7762536a` below.
- D11: under `world_loadout_block` the guest's VEHICLES drop (`MSN_DROP_VEHICLE`, NOTE 13 a 4) is inactive too in every
  co-op mission, whatever `world_relief` says; `world_relief_sync.lua` leaves that item to the rules module.

Hidden runs (research build, native gun stage, copied saves, low graphics; each Closed, BaselineRestored,
RealSavesUnchanged, no `step=error`, `viewer_changed` or fault line). Slices in the session scratch
`decisions/vehicles/slice-<run8>`, runs and their probe folders archived under
`%USERPROFILE%\mgs-coop-run-archive\decisions\<id>`, frames in `complete/frames/vehicles/` (`<run8>-<role>-<label>.jpg`;
sheets `g-776-*`, `g-f72-*`, `g-33a-play`).

| Run | Scenario, mode | Result |
|---|---|---|
| `7762536a` | `vehicle-decisions-review-lab.txt` at `157d73d` (before the blast rule), lab, `africa-jeep`, keys named | Every expectation met but one regex: A's judge line was right (`ok=0; why=guest_far; d_mm=1196593`: 25609 is 1.2 km from the guest's boot spot, not the 320 m the scenario assumed). `Forced=False`. The occupied hold, the far refusal, the blast measurement below |
| **`f729d7f4`** | **`vehicle-decisions-review-play.txt` at `3aac7be`, `-Mode play -WanSim bad`, no vehicle key named (the shipped defaults)** | **The FRAME proof on both copies of the blast rule, the wreck and the guest's VEHICLES block.** Every expectation met but the log line of the VEHICLES toast: the HUD logs one toast line per NOTE code per 10 s (`kToastLogGapMs`) and the item was picked 5.6 s after LOADOUT; the frame shows that toast drawn. Link 175-190 ms, loss up to 2.3 %. `Forced=False` |
| **`33a7fa6e`** | **the same at `f466d30` (waits past the toast-log gap), play, `-WanSim bad`, shipped defaults** | **The passing play rehearsal: every expectation met**, `Forced=False`, both toasts logged (LOADOUT, then VEHICLES 10.7 s later). The same lines as `f729d7f4`: 2a `break_judge; d_mm=19829` -> `break_safe; near=host; d_mm=8188` -> `via=locator`; 2b `d_mm=21495` -> `via=wreck; ms=166`; the host's life 7801 throughout. Link about 180 ms, loss 2.4 %. Sheet `g-33a-play` |
| `0f3f00f1` | `vehicle-decisions-review-range.txt` at `f466d30`, lab, keys named | Every expectation met, `Forced=False`. K: a soldier-crewed patrol jeep held, not applied (below). R: a break 120 m from the guest judged near and wrecked |

**The occupied hold [O, `7762536a`].** The host drove 25608 with D-Dog beside it, got out (`vehicle_state; step=skip;
reason=occupied` while D-Dog stayed seated) and the guest's lever broke the guest's copy: guest `break_seen` -> `req;
cause=4` -> host `rx_req; cause=4` and `hold; id=25608; src=req; reason=occupied` -> 568 ms later (D-Dog out)
`break_judge; ok=1; why=near; d_mm=1570` -> `applied; via=wreck; ms=250` -> guest `req_done; result=gone` (633 ms after
its request). Frames `7762536a-*-occ-*` (sheet `g-776-occ`): the host's jeep with D-Dog in it, then the burnt wreck on
both screens. No `ridden` refusal. (At `157d73d`, before the blast rule; with it the host beside its copy now gets the
Despawn.) An enemy-crewed vehicle [O, `0f3f00f1`]: the patrol jeep 25623 (unnamed, driven by a soldier, 477 m from the guest's
boot spot, guards not cleared). The guest's lever broke the guest's copy; its driver died there unattributed
(`obs; m=Dead; ent=1856151639; att=none` -> `divergence; why=not_attributed`, nothing forwarded), so on the host the
driver stayed seated: `rx_req; cause=4` -> `hold; id=25623; src=req; reason=occupied`, still held 95 s later when the
run ended (`vehicle_gone_status`: `held=25623`; the jeep alive with `life=2800` on the host's copy, gone on the guest's).
VEHICLE_GONE's cap answers such a hold with REJECT 9 at 120 s [fixture], and with the guest 477 m away the judge would
refuse it anyway (`guest_far`). So a crewed vehicle the guest destroys with an unattributed blast stays only on the
host's PC, with its crew: the user's pick (hold, never refuse at once) cannot help there. That is a question for the
user (section 10, D12).

**The far refusal [O, `7762536a`].** The guest's lever broke 25609 1197 m away: host `break_judge; ok=0; why=guest_far;
d_mm=1196593; life=2300; peak=2300` -> `refused; reason=guest_far` -> REJECT 8 -> guest `req_done; result=reject;
reason=8` and `break_refused; here=wreck`; the host's 25609 stayed `alive=1; life=2300`, the guest's a wreck.

**The host-side blast, measured [O, `7762536a`].** `SetBodyLife 0` starts the fire at once (the first `VehicleBroken`);
the damaging explosion comes about 8 s later (a second `VehicleBroken`, 8.02 s on the host, 8.02 s on the guest for its
own copy). In the occupied phase it hit the host standing beside its copy for 3500 of 7800 life (`downed_damage; id=0;
attacker=65535; part=9; damage=3500`; godmode on, so nothing was taken) and the guest beside its own copy the same
(3500 of 6600). In phase C (the host at 25610's door, godmode off, one guard 5 m away set to ignore the players) the
host was downed, but by that guard's rifle (`attacker=1120`, 342 a hit, 7800 -> 960 over about 7 s), not the blast; the
guard died about 8 s after the wreck on both copies, each from its own copy's explosion (`obs; m=Dead; att=none` on
both, 120 ms apart; the host's commit then the guest's `terminal_local`). So the host-side explosion is the engine's own,
as strong as on the guest's copy, and it reaches a player within a few metres of the host's copy, whoever caused it.

**The blast rule, in play [O, `f729d7f4`].**
- 2a, 25608 at Masa Village, the host 8 m from it with godmode off, the guest's real C4 (three charges, detonated about
  20 m away): guest `break_seen` -> `req; cause=4` -> host `break_judge; ok=1; why=near; d_mm=19748` -> `break_safe;
  id=25608; near=host; d_mm=8188; form=despawn` -> `applied; via=locator; ms=0` -> guest `req_done` 233 ms after its
  request. The host's life read 7801 of 7801 before, 2 s after and 12 s after (past the explosion window); no
  `downed_damage` sample on the host. Frames `f729d7f4-*-d12a-*` (sheets `g-f72-d12a`, `g-f72-d12a-hostzoom`): on the
  host's screen the jeep beside the tower, then gone, with no fire; on the guest's the blast and the burning wreck. The
  host's copy reads `pos=0,0,0` (despawned), the guest's `alive=0` (the wreck).
- 2b, 25610 in the swamp, the guest's C4 detonated about 21 m away, the host about 27 m from the jeep with godmode off:
  `break_judge; ok=1; why=near; d_mm=21478` -> no `break_safe` -> `applied; via=wreck; ms=183` -> guest `req_done` 467 ms;
  the host's second `VehicleBroken` came 8.0 s after the wreck, the host's life stayed 7801. Frames `f729d7f4-*-d12b-*`
  (`g-f72-d12b`, `g-f72-d12b-hostzoom`): on the host's screen the jeep 27 m away, then burning, then the burnt wreck
  (the first host frame of the wrecked jeep itself, closing the review's frame gap); on the guest's the blast and the
  smoke column.

**D11, VEHICLES [O, `f729d7f4`].** With `world_relief` off (its default) the guest's iDroid shows LOADOUT and VEHICLES
both marked inactive; LOADOUT selected -> `refused; path=idroid_drop_loadout` and its toast; VEHICLES selected ->
`refused; path=idroid_drop_vehicle` and the toast "Only the host can bring or request a vehicle in co-op." (frame
`f729d7f4-guest-d11r-guest-vehicles-refused`; its log line was rate limited, above); no `event=relief` line (the relief
module left the item alone). The host's VEHICLES item opens its list (ZaAZ-S84/4W, APE T-41LV; frame
`f729d7f4-host-d11r-host-vehicles-open`). Sheet `g-f72-d11`.

**Side finding for D7 (not this track's code) [O, `f729d7f4`].** In 2a the guest's own C4 left `obs; m=Dead; id=9728;
att=local` in the guest's log and "D-Dog Near Death (Heroism -60)" / "Bond with D-Dog Has Decreased Significantly" on
the guest's screen: the guest's copy of D-Dog (placed by `world_buddy` from the host's dog, which stood beside the host
8 m from the jeep) was in the guest's blast. On the host's PC nothing happened (the Despawn). This is the case D7 asks to
guard (the guest's copy of the host's dog must not be killable on the guest's PC); it is reported to that track.

**Play defaults.** All three keys stay on in play (`f4ec5c1`), each now with its FRAME on both copies and a passing
`-Mode play -WanSim bad` rehearsal of the behaviour as shipped: `world_vehicle_break` with the review fixes and the blast
rule (FRAME `f729d7f4` and `33a7fa6e`: the host's jeep vanishing beside the host, the guest's blast and wreck, the
host's burnt wreck 27 m away; rehearsal `33a7fa6e`, no key named), `world_loadout_block` with VEHICLES (the same runs:
both toasts drawn on the guest's screen, the host's VEHICLES list), `world_vehicle_credit` unchanged since `47b95ae7`
(its code path did not change in the fixes; FRAME `c0203fdd`, rehearsals `70146170` and `47b95ae7`).

**The 60-300 m band [O logs, `0f3f00f1`].** A placebehind chain put the host 82 m and the guest 120 m from 25608 on
a hillside; the guest's lever broke its copy: `break_judge; ok=1; why=near; d_mm=120293` (refused under the first 60 m
rule) -> no `break_safe` -> `applied; via=wreck; ms=116` -> guest `req_done` 66 ms. The frames (`0f3f00f1-*-range-*`)
show only the slope between the players and the jeep, so this case rests on the logs of both copies. A real
launcher or rifle kill at that range was not run [I: the judge reads only positions].

**Open.** a soldier-crewed vehicle (above: held, then refused) is a question for the user; a multi-hit kill spread over more than about 10 s (VEHICLE_STATE restores the host's life between
hits, so the guest's copy may never break) [I]; D-Dog or a buddy near the host's copy when it is wrecked (the rule
checks the two players only) [U]; the blast rule's 15 m is a margin over the measured point-blank hit, not a measured
radius [I]; a two-PC session.

### 8.13 Results: the combined play smoke of the integrated decisions (2026-10-29, `dev/oct7-continue`)

One hidden run of the merged head (`wp/integ-smoke` = `dev/oct7-continue` `d9ce8d3`: the two merges `4e3d0e5` and
`92c4f9f` plus the scenario), built incrementally in the worktree `D:/mgs-coop-wt/relief` (research worker build,
native gun stage). Scenario `tools/worker/scenarios/integ-smoke-play.txt`, `-Mode play -WanSim bad`, seed
`africa-jeep` (30020), **the shipped play defaults**: no labsetting line and no lab lever. Run
`a1d7aaa1c1894b13a12e894cba3a9db8`: **every expectation met** (ExpectFailures empty), Closed, BaselineRestored,
RealSavesUnchanged, `Forced=False`; no `step=error`, `event=error`, `viewer_changed`, non-zero `faults=` or `via=wreck`
line on either copy. Link: RTT 175-189 ms, loss up to 14.2 %. Slices (cut at the session offsets of both copies'
`script-observer.log`; no other run appended) in the session scratch `decisions/integrate/slice-a1d7aaa1`; frames
`complete/frames/integrate/a1d7aaa1-<role>-<label>.jpg`, looked at for every claim below. Archived under
`%USERPROFILE%\mgs-coop-run-archive\decisions\a1d7aaa1...` (with the worker's probe folder `4d714dd7...`).

| Part | Result |
|---|---|
| Pairing and defaults | [O] Both copies reached gameplay (52.6 s / 50.7 s after launch) and paired (`link=connected`, `remote_build=0.14.3+d9ce8d3`). Both logged `vehicle_gone; step=on; mission=30020; ...; wreck=0` and `vehicle_rules; step=on; ...; break=1; credit=1; loadout=1; gone=1` from the defaults; the host's sweep finished and every sweep state the guest logged was `step=same` (d_mm=0). Frame `smoke-start` on both screens: the jeep with the partner beside it (with its persona name tag). |
| D7 at the default | [O] `companion_host_owned` is off in play, so `dog_guard=off;reason=key_off;slots=0` on both copies and `companion_lab dog_state` read `installed=0` on both: nothing patched. Each PC keeps its own D-Dog (`buddy_status` `kdog`, `vars_type=2` on both; the host's dog beside Snake in the host's frames, the guest's own dog rode in the guest's jeep: `riders=0,9728`). No host-owned dog to check on the guest's screen, by design. |
| D11 | [O] The guest's iDroid LOADOUT and VEHICLES items were refused (`step=refused; path=idroid_drop_loadout` / `idroid_drop_vehicle`), each with its toast drawn (frames `d11-guest-loadout-refused`: "Only the host can order a loadout drop in co-op: it can carry a vehicle."; `d11-guest-vehicles-refused`: "Only the host can bring or request a vehicle in co-op."; both items red-marked); the host's VEHICLES item is active and its list opens (`d11-host-vehicles`, `d11-host-vehicles-open`: ZaAZ-S84/4W and APE T-41LV). |
| A drive: the guest drives, the host rides in the back | [O] The guest boarded 25608 as driver (`vehicle;id=25608;...;localSeat=1`); the host's VEHICLE_STATE skipped it (`reason=partner_ride`); the host's E at the driver door became a back seat (`board_policy; ... mode=passenger` -> `board_passenger; seat=2; site=phase1` -> `local_passenger; state=on` -> `pass_camera; state=connect; reason=handle`), and the guest's PC seated the host's body there (`partner_ride_seat=confirmed; vehicle=25608; seat=2`). Frames on both screens: `drv-seated` (the guest's body at the wheel and the host in the back on the host's screen; the same on the guest's, with the guest's own dog in the front seat), `drv-pass-NN` (the host's own passenger view of the moving jeep), `drv-driver-NN` (the guest's driver view with the host's body in the back), `drv-stopped`. The host got off (`local_passenger; state=off`, `pass_camera; state=release; reason=stock; ok=1`; `drv-host-off-NN`), then the guest. |
| Parked afterwards | [O] `step=resume; id=25608; was=partner_ride` -> `step=tx; why=moved; d_mm=28138` -> the guest `step=same; d_mm=1`: the host's copy, moved by RIDE_FAST, already stood where the guest left it. Readbacks 1 mm apart right after the park, and about 35 s later 16 mm apart in x and 74 mm in height (host `-652084,420,850560`, guest `-652068,346,850558`). Frames `drv-parked` and `drv-parked-both` on both screens: the jeep parked by the same trees and rock, the guest beside it. [O, meaning U] the state word then read `sv=1` on the host's copy and `sv=3` on the guest's (VEHICLE_STATE logs it and does not write it). The host's copy read `reason=occupied` while the host sat in the back (the passenger in `rider[2]`), as designed. |
| D14, a vehicle Fulton | [O] The host's hand Fulton of 25610 (swamp door spot): host `watch; m=Fulton; cause=1` -> `credit; via=own; res=14` -> `tx; cause=1` -> guest `credit_seize` -> `credit; via=receiver; res=14` -> `applied; via=seize; src=rx; ok=1; ms=250`. Census: GMP 313850 -> 303850 on the host (its own Fulton fee) and unchanged on the guest; `4wdW=1` on both (no mission end in play, so no commit, as in `70146170`). Frames `fult-before` (both), `fult-hold`, `fult-lift-NN` (the jeep lifted on the host's screen), `fult-after` (the jeep gone on both screens). |

Not in this smoke: D12 (its play rehearsals are `f729d7f4` and `33a7fa6e`), any death of a dog copy, the guard under
the key, a mission end, D15. No defect found.

### 8.14 Results: the vehicle fixes of 2026-10-29 (D12 crew, the guest's vehicle damage report, D11 before PAIRED; branch `wp/vehicle-fixes2`)

The user approved three fixes on 2026-10-29 after the 0.14.4 pre-release review: D12's open question (section 10: a
soldier-crewed vehicle the guest destroys is wrecked on the host's PC at once), the review finding
`game-d11-prepair-gap` and the finding `game-vstate-heals-guest-damage`. Built on `wp/vehicle-fixes2` (from
`dev/oct7-continue` `01fd055`), tests first (each fixture failed before its code); wire and keys in
`docs/world-sync-protocol.md` section 20.

**What was built.**
- **D12 crew** (key `world_vehicle_crew`, CFG `r[15]` bit 64, `world_vehicle_rules.lua`): a guest destruction request
  that VEHICLE_GONE would hold as `occupied` is checked seat by seat; when every filled seat is an NPC soldier (world
  sync's roster, or a TppSoldier2 type index) and the partner does not ride it, the host judges it at once and wrecks
  its copy through the normal path (`break_judge`, `break_crew;crew=<seats>`, `applied;via=wreck`); held (`crew_near`)
  while a player stands within 15 m of the host's copy (a Despawn under seated soldiers is untested). D-Dog, a buddy, an
  unknown rider, the partner's ride and the host's own player keep the old hold or refusal. Before: held 120 s, then
  REJECT 9 and a split (`0f3f00f1`).
- **The guest's vehicle damage report** (key `world_vehicle_damage`, bit 128, new chunk `world_vehicle_damage.lua`, new
  op VEHICLE_DAMAGE 0x3A): the guest reports body-life drops of its own copy that come within 3 s of its own fire
  (PlayerStatus GUN_FIRE or GUN_FIRE_SUPPRESSOR; in the lab the lever `vehicle_life` stands in) within 300 m of its
  player, never for the host's ride, index 0, an LRRP truck or a wreck; the host applies `max(life, here - drop, 1)`
  (the lower life wins, never more than the guest's drop, never 0) and its VEHICLE_STATE then carries that life, so the
  guest's damage stays on both copies. VEHICLE_STATE no longer raises the guest's copy over a report in flight (3 s).
- **D11 before PAIRED** (no key: part of `world_loadout_block`): the guest's follow arrival marks the mission co-op at
  once, so its LOADOUT and VEHICLES items are inactive before PAIRED (also `world_relief_sync.lua`'s VEHICLES guard).
- Worker: `waitmission <role> <mission> <timeoutSec> only` resumes only that role's loading screen (the other stays on
  its tips screen), which holds the pair off for the D11 proof.

Hidden runs (research build, native gun stage, copied saves, `africa-jeep` seed, low graphics; each Closed,
BaselineRestored, RealSavesUnchanged, no `step=error`, `viewer_changed` or fault line; `Forced=True` at shutdown for
`846c1147`, `ac0f7656` and `247a4eed`, `Forced=False` for `d3f5ca56`, `8995e351`, `251adcb6` and `34d5744d`; corrected
by the review, section 8.15, which said `Forced=True` for all). Slices in the session scratch `next2/vehicles/slice-<run8>`, runs archived under
`%USERPROFILE%\mgs-coop-run-archive\next2\<id>`, frames as JPEG in `complete/frames/next2-vehicles/`
(`<run8>-<role>-<label>.jpg`; sheets `next2/vehicles/g-*.jpg`), looked at for every frame claim below.

| Run | Scenario, mode | Result |
|---|---|---|
| `846c1147` | `vehicle-fixes2-lab.txt`, lab, every key named, build `20a7120` | The report path worked; the guards' rounds on both copies were counted twice and broke the jeep (below), which led to the fire gate (`d8497c5`). Stopped at A2 (`goto_vehicle` picked a jeep 600 m away) |
| **`ac0f7656`** | **the same at `d8497c5`** | **The lab FRAME run.** Every expectation met but the D11 refusal: the damage report (A, A2, A3), D12 crew (B, the burning armoured vehicle on both screens), D11's guard 29 s before PAIRED (C); the guest was still riding the helicopter in, where its iDroid did not open |
| `247a4eed` | `vehicle-fixes2-play.txt`, **play, `-WanSim bad`**, the two new keys named, build `d8497c5` | Parts 1 (D11 at the session's follow arrival: refused, toast drawn) and 2 (the guest's real round reported and kept on both copies) passed; the host's own rounds missed; part 3 failed outside the rule: the armoured vehicle, alerted by the guest placing charges in front of its driver, drove apart on the two copies (50 m) and VEHICLE_GONE refused the request as far |
| `d3f5ca56` | `vehicle-fixes2-d11.txt`, lab, `world_loadout_block` named, build `9538f86` | The guard held 104 s before PAIRED with the guest on foot at the LZ and the host on its tips screen; the guest's first Tab came during the arrival popups, so the iDroid opened only after the last step and no refusal was logged |
| **`8995e351`** | **`vehicle-fixes2-play.txt` at `da2043d`, play, `-WanSim bad`, `world_vehicle_crew` named, the damage report at its play default (on since `9538f86`)** | **The crew's play rehearsal passed** (the guest's real C4 from behind, frames on both screens) and D11 again; the damage part found no jeep: the Masa Village guards had broken 25608 on the host's copy (its VEHICLE_GONE removed the guest's copy) before the part cleared them. `Forced=False`, link 175-185 ms, loss up to 13.3 % |
| (none) | `vehicle-fixes2-d11.txt` at `f6823b9` (with an uncommitted failing-first test) | Never launched: the worker's audited CTest snapshot held `lab_config_tests` written before its code (check 1275), so the checks refused the run; not counted as a game run |
| **`251adcb6`** | **`vehicle-fixes2-d11.txt` at `f6823b9`, lab, `world_loadout_block` named, the HUD on** | **The D11 FRAME before PAIRED. Every expectation met**, `Forced=False`: both items inactive and refused with both toasts on the guest's screen while the host waited on its tips screen; the pair formed 96 s after the guest's arrival |
| **`34d5744d`** | **`vehicle-fixes2-play.txt` at `f6823b9`, play, `-WanSim bad`, no vehicle key named (the shipped defaults: both fixes on in play)** | **The play rehearsal at the shipped defaults. Every expectation met**, `Forced=False`, link 175-186 ms, loss up to 13.3 %: D11 (refused, toast), the guest's round reported and kept on both copies, the host's round synced and never reported back, the crew wrecked at once with the guest's real C4 (frames on both screens) |

**The guest's vehicle damage report.**
- *`846c1147`: the path, and why the fire gate* [O, logs of both copies]. The seed's jeep 25608 read life 2140 on both
  copies (the guards' rounds of the boot). The guest's lever `vehicle_life 25608 1500` (its copy only) -> guest
  `event=vehicle_damage;step=tx;id=25608;life=1500;drop=640;f=0;seq=1` -> host `step=applied;id=25608;from=25608;
  life=1500;was=2140;drop=640;d_mm=1566` 17 ms later -> host `vehicle_state;step=tx;id=25608;why=life;life=1500` 152 ms
  after that. The guards around Masa Village were not cleared and shot at both players beside the jeep: the host's own
  copy dropped (1460 -> 1340, 1295 -> 1215) and the drops the guest's own guards caused on its copy (40, 45, 49, 60)
  were reported and applied on top (`life=1460;was=1500;drop=40` ... `life=1106;was=1166;drop=60`) until the host's
  copy broke at 1106 (VehicleBroken; VEHICLE_GONE cause 2 to the guest). Each copy's guards hit vehicles on that copy, so
  without attribution the guest's AI damage adds to the host's: the module now reports only drops within 3 s of the
  guest's own fire (`d8497c5`).
- *`ac0f7656`, lab, the guards cleared first* [O, logs of both copies; frames `ac0f7656-*-dmg-*`, `-shots-*`,
  `-host-damage`, sheet `g-ac0-dmg`: the jeep on both screens, with no visible damage at these lives, as `d407f115`
  found for body life alone]. A: the guest's lever 2260 -> 1500 (`step=lever_hit`) -> `tx;life=1500;drop=760` -> host
  `applied;life=1500;was=2260;drop=760;d_mm=1512` 19 ms later -> its `vehicle_state;tx;why=life;life=1500`; both copies
  read 1500 two seconds later and again 14 s later, past the host's 10 s refresh (before the fix the refresh put a
  guest-only drop back: `3585869d`, `4bbbfde4`). A2: the guest's real rounds from its boot spot (five, one hit):
  `tx;life=1470;drop=30` -> host `applied;life=1470;was=1500;d_mm=2022` 32 ms later. A3: the host's own lever drop to
  1000 -> its `tx;why=life;life=1000` -> guest `vehicle_state;step=applied;...;life=1000;was=1470` 32 ms later, and no
  guest `vehicle_damage;step=tx` after it (never reported back).
- *`247a4eed`, play, `-WanSim bad`* [O, logs of both copies; frames `247a4eed-*-dmg-*`]: the guest's real round 2020 ->
  1990 -> `tx;life=1990;drop=30` -> host `applied;life=1990;was=2020;d_mm=2023` 117 ms later -> `vehicle_state;tx;
  why=life;life=1990` 17 ms after that; both copies read 1990 right after and 14 s later. Link RTT 176-186 ms, loss up
  to 12.1 %. The host's own rounds from its boot spot missed (no host `tx`); the host's damage reaching the guest is the
  unchanged M4 path (`4bbbfde4`, `e235bda6`, `d9b48fcb` in play; `ac0f7656` A3 with this module on).
- *`34d5744d`, play, `-WanSim bad`, the shipped defaults (no key named)* [O, logs of both copies; frames
  `34d5744d-*-dmg-*`, `-host-damage`, sheet `g-34d-play`: the jeep on the guest's screen and at the host's door]: both
  copies `vehicle_damage;step=on` from the play default; the guest's real round 2300 -> 2270 -> `tx;life=2270;drop=30`
  -> host `applied;life=2270;was=2300;d_mm=2022` 122 ms later -> `vehicle_state;tx;why=life;life=2270` 32 ms after
  that; both copies 2270 right after and 14 s later. The host's own round 2270 -> 2240 -> its `tx;why=life;life=2240`
  -> guest `vehicle_state;step=applied;life=2240;was=2270` 114 ms later, and no guest `vehicle_damage;step=tx` after it
  (the host's damage is never reported back).
- A wreck's life word reads near 65535 and still falls (the guest's copy of 25623 read 65488 -> 65484 after its
  destruction in `247a4eed`), so the guest now reports only a live vehicle (`9538f86`, `step=skip;reason=not_alive`).
- Not covered [I/U]: who hit the vehicle is still not known: a guard's round in the 3 s after the guest fired is
  reported too [I]; explosives short of a destruction (a grenade, a C4 that does not break it) are not GUN_FIRE and are
  not reported [I]; the guest's collisions while it drives are not reported [I]; parts and wheels are not synced (only
  body life, as M4); a drop is counted once (the lower life wins) only when the host copy's own hit came before the
  report, so simultaneous damage by both players can be undercounted by the smaller share [I], and AI fire on the two
  copies is separate rounds that add up whenever the report comes first (`846c1147`; corrected by the review, 8.15)
  [O]; a two-PC session.

**D12 crew.**
- *`ac0f7656`, lab* [O, logs of both copies; frames `ac0f7656-*-crew-*`, sheet `g-ac0-crew`]. 25623 is the side-op
  armoured vehicle of "Eliminate the Armored Vehicle Unit 04" (unnamed, life 2800) with one soldier aboard, the quest
  soldier `sol_quest_0000` (ent 1856151639); VEHICLE_STATE skips it as `occupied` on both copies. The host 30 m away,
  the guest 25 m (the host first asked `enemy ignore nearest` from 12 m, applied on both copies). The guest's lever
  breaks its copy (`vehicle_life 25623 0`) -> `break_seen` -> `req;cause=4` -> host `rx_req` -> `break_judge;id=25623;
  ok=1;why=near;d_mm=23694;life=2800;peak=2800` -> `break_crew;crew=1` -> `tx;cause=2;actor=1` -> `applied;via=wreck;
  src=req;ok=1;ms=166` -> guest `req_done;result=gone` 66 ms after its request; no `hold` line, no REJECT. Host status
  afterwards `alive=0`. Frames: before, the vehicle beside the road on both screens; then fire and smoke at it on both;
  the host's screen shows it burning for the rest of the run; on the guest's its own copy's blast knocked the guest
  down 24 m away (godmode on).
- *The crew's fate* [O]: on each copy the wreck's later explosion (about 8 s after the break, as `7762536a` measured)
  killed the driver: host `obs;m=Dead;ent=1856151639;att=none` (and `Neutralize`) 7.9 s after its wreck -> one
  `commit;ent=1856151639;state=5;ver=2;actor=host;cause=2`; guest `obs;m=Dead` 8.0 s after its own break ->
  `divergence;why=not_attributed` (nothing forwarded) -> the host's outcome `apply;state=5;ver=2;src=outcome;
  result=terminal_local;readback=5` 17 ms after the host's commit. One commit, one apply that found him dead already:
  the death counts once on each copy, and nobody gets the kill (both deaths are unattributed, the guest's lever is no
  player hit).
- *`247a4eed`, play* [O]: the rule was never reached. The guest teleported 3 m east of the vehicle (in front of its
  driver) to place three charges; the soldier entered combat on both copies (`place_combat`) and the vehicle drove off
  and fired, separately on each copy (the host's copy 36 m west, then back; the guest's 5 m east); the guest's charges
  broke its copy where it stood, the request reached the host 50 m from the host's copy, and VEHICLE_GONE refused it
  (`refused;reason=far;d_mm=50384`, REJECT 8) before any seat check. Frames `247a4eed-*-crew-*`: the vehicle exploding
  on the guest's screen, an empty road on the host's. An AI-driven vehicle in combat moves separately on each PC (M8);
  the crew rule cannot help there.
- *`8995e351`, play, `-WanSim bad`* [O, logs of both copies; frames `8995e351-*-crew-*`, sheet `g-899-crew`]. The host
  asked the ignore from 9 m behind the vehicle (west; its hull lies east-west, front east), then stood 30 m away; the
  guest placed three charges from 5 m behind it and detonated them from 25 m. The vehicle did not drive off (the host's
  readbacks before and after stay within 0.11 m). Guest: `VehicleBroken` -> its driver died at once in its copy's blast (`obs;m=Dead;
  att=none` -> `divergence;why=not_attributed`) -> `break_seen` -> `req;cause=4` -> host `rx_req` 149 ms later ->
  `break_judge;ok=1;why=near;d_mm=25079;life=2800;peak=2800` -> `break_crew;crew=1` -> `applied;via=wreck;ms=166` ->
  guest `req_done` 266 ms after its request. The host's wreck exploded 8.0 s later and killed the driver there: one
  `commit;state=5;ver=2;cause=2`, applied on the guest 101 ms later as `terminal_local` (already dead there). Host
  readback `alive=0;life=0;sv=1013` (the wreck), guest `alive=0`. Frames: the vehicle at the road on both screens,
  then the guest's blast and smoke on the guest's screen, the host's own explosion 8 s later and the burning wreck on
  the host's.
- *`34d5744d`, play, the shipped defaults* [O, logs of both copies; frames `34d5744d-*-crew-*`]: the same approach and
  the same chain with no key named: guest `break_seen` -> `req;cause=4` -> host `rx_req` 118 ms later -> `break_judge;
  ok=1;why=near;d_mm=25080` -> `break_crew;crew=1` -> `applied;via=wreck;ms=183` -> guest `req_done` 233 ms after its
  request; the driver died at once in the guest's blast (`divergence;why=not_attributed`) and 8.0 s after the wreck
  in the host's (one `commit;state=5`, applied on the guest as `terminal_local` 101 ms later). Host readback `alive=0;
  life=0;sv=1013`, guest `alive=0`. Frames: the vehicle at the road on both screens, the host's explosion, the
  burning wreck on both; the guest was thrown down 25 m away by its own copy's blast (godmode on).

**D11 before PAIRED.**
- *`ac0f7656`, lab* [O, logs; frames `ac0f7656-*-d11p-*`, sheet `g-ac0-d11`]: the host's lab goto to 30010 with only the
  guest's loading resumed (`waitmission guest 30010 240 only`): guest `follow;step=arrived;mission=30010` -> both
  `vehicle_rules;step=guard;...;mission=30010` 1 ms later -> PAIRED 29.1 s later, after the host's loading was resumed;
  `world_paired` read false twice in between. The host's frames show its tips screen ("[HINT] Direct Contracts ... Space
  Resume Game") the whole time; the guest rode the helicopter in, and its Tab there opened nothing (frames
  `d11p-guest-loadout` / `-refused`: the helicopter's cabin).
- *`d3f5ca56`, lab, on foot* [O, logs; frames `d3f5ca56-*-d11f-*`]: the guest got out at the LZ (position 800.4,
  339.3, 1637.5); the guard held from the arrival to PAIRED 104 s later with no `unguard` between; the first Tab came
  during the checkpoint and Materials Processing popups (frame `d11f-guest-onfoot`), so the iDroid opened only after
  the scenario's last key (its MAP tab in `d11f-paired`), and no refusal was logged.
- *`251adcb6`, lab, on foot, the popups waited out* [O, logs; frames `251adcb6-*-d11f-*`, sheet `g-251-d11`]: guest
  `follow;step=arrived;mission=30010` -> both guard lines 1 ms later (`world=pair_wait;reason=remote_loading`) -> the
  guest got out at the LZ (position 800.3, 339.3, 1636.6), waited 12 s, opened its iDroid (frame `d11f-guest-idroid`,
  the MAP tab) -> MISSIONS, SUPPLY DROP: LOADOUT and VEHICLES both marked inactive; LOADOUT selected -> `step=refused;
  path=idroid_drop_loadout` and `hud=toast;code=13;a=5;partner=0;on=1` 74.9 s after the arrival (frame
  `d11f-guest-loadout-refused`: "Only the host can order a loadout drop in co-op: it can carry a vehicle.");
  VEHICLES selected 12.6 s later -> `refused;path=idroid_drop_vehicle` and the toast `a=4` (frame
  `d11f-guest-vehicles-refused`: "Only the host can bring or request a vehicle in co-op."); `world_paired` read false
  four times; the host's screen showed its tips screen throughout (`d11f-host-waiting`, `-still-waiting`); after the
  host's loading was resumed, PAIRED came 95.9 s after the guest's arrival and 8.3 s after the second refusal.
  Before the fix the guard waited for PAIRED (finding `game-d11-prepair-gap`), so both items were active for those
  96 s.
- *`247a4eed`, play* [O]: the session's own follow (title -> ACC -> 30020): the guest's `arrived` then PAIRED 1.3 s
  later (the host was first, as in every play run), the guard at PAIRED; the guest's real iDroid path: LOADOUT refused,
  `hud=toast;code=13;a=5;on=1` (frame `247a4eed-guest-d11-guest-refused`: LOADOUT selected, the toast at the top).
  The pre-pair window itself does not occur in the play worker (the host always pairs first there). **Corrected by the
  review (8.15):** in `247a4eed`, `8995e351` and `34d5744d` the guest's first guard line has the same millisecond as
  its `event=paired` (`34d5744d`: arrived 229023182, paired 229024232, guard 229024233), so these runs show the old
  paired refusal, not the fix; the pre-pair path was rehearsed in play only in 8.15.

**Play defaults** (superseded by the review, section 8.15: both keys are off in play unless named again, and the
claims below that the damage FRAME shows the shared life and that the D11 fix ran in play are refuted there). Both
new keys were on in play unless named and off in the lab unless named, like the other vehicle
modules. `world_vehicle_damage` (`9538f86`): FRAME on both copies `ac0f7656` (lab) and the play rehearsal `247a4eed`
(keys named); `world_vehicle_crew` (`f6823b9`): FRAME `ac0f7656` (lab) and `8995e351` (play, frames on both) and the
play rehearsal `8995e351` (crew named). Both again at the shipped defaults with no key named in `34d5744d` (every
expectation met). The D11 fix rides on `world_loadout_block` (on): its FRAME before PAIRED is `251adcb6` (lab, the
host held on its tips screen by `waitmission ... only`); in play the fix runs at every follow arrival (`247a4eed`,
`8995e351`, `34d5744d`: refused with its toast) but the pre-pair window does not occur in the worker, where the host
always pairs first [I for play].

**Open.** An enemy vehicle that drives off in a fight moves separately on each PC (M8), so a guest's destruction of it
can be refused as far and stay split (`247a4eed`); `crew_near` and a crewed vehicle with a player within 15 m [U]; the
guest's explosives short of a destruction, its collisions and the guards' rounds inside its 3 s window are not (or
wrongly) attributed [I]; a D-Dog or buddy rider together with soldiers keeps the old hold [fixture]; the pre-pair
window in play and every case on two PCs.

### 8.15 Results: the review of the vehicle fixes (2026-10-29/30, branch `wp/vehicle-fixes2`)

An independent review of 8.14 (verdict `ship_behind_lab_key`) found three major and four minor issues and refuted four
claims. Each finding was checked against the code, the archived slices and the frames before the fix; the table is in
section 11 ("Review of the vehicle fixes"). Tests first for every rule (logs `next2/vehicles/failfirst-*.log`).

**What changed.**
- **The guest's vehicle damage report is off in play again** (`795bdfc`; lab key, named in play to use it). Its fire
  gate was never run against live AI fire: every damage run after `846c1147` cleared the guards first, and the only
  run with live guards (`8995e351`) logged no drop for 25608 on the guest. The gate cannot tell who hit the vehicle:
  in a firefight the guest fires within every 3 s window, its own copy's guards hit the vehicle on its copy, and those
  rounds are separate from the host copy's guards' rounds. The host's `max()` deduplicates only when the host copy's
  own hit lands before the report, so AI fire adds up otherwise (`846c1147`) [O]. Section 20 of
  docs/world-sync-protocol.md and 8.14 said "a hit both copies took counts once": corrected.
- **Host-side bounds for VEHICLE_DAMAGE** (`8f20483`): the host takes at most half of its copy's life per report
  (logged `cap=1`), at most 3 reports per vehicle and 16 over all vehicles per second (more: `refused;reason=rate`), and
  counts what it took (`D.given`). D12's heavy rule (a far request is plausible when the host's copy is down to a
  quarter of its highest life) now counts the host copy's own damage only, so a misattributed report can no longer make
  a far destruction request plausible [fixture].
- **D12 blast distance by vehicle type** (`77be734`): the armoured vehicle's own `SetBodyLife 0` destruction threw the
  guest flat 25 m away (`ac0f7656`, frames `crew-blast-01/02`: the guest lies at 24-25 m with damage arcs toward the
  vehicle) [O], beyond the 15 m taken from a jeep (`7762536a`). `VR.blastMm` keeps 15 m for Vehicle.type
  `EASTERN_LIGHT_VEHICLE` / `WESTERN_LIGHT_VEHICLE` (compared by name) and uses 50 m for every other type, an unknown
  type or a missing binding, for `crew_near` and `break_safe` alike (both lines now carry `blast_mm=`). An unnamed copy
  has no Despawn outside Mother Base (VEHICLE_GONE refuses it as `no_locator`, REJECT 8, and the copies would differ),
  so with a player inside its blast the request is now held (`break_hold;why=blast_near`) until the players are clear
  (REJECT 9 after 120 s), instead of the `break_safe` Despawn that cannot run [fixture; `blast_near` itself not run
  live].
- **D11's pre-pair mark lapses** (`017e777`): the arrival marks the guest's mission co-op before PAIRED (8.14); if the
  host's game fails or quits while loading, PAIRED never comes and the items stayed inactive for the whole solo
  mission. The mark now lapses 300 s after the arrival without PAIRED (`step=prepair_lapsed;mission=;ms=300000`, once,
  in `world_vehicle_rules.lua` and `world_relief_sync.lua`); PAIRED in time keeps the old rule; a late PAIRED makes the
  mission co-op again [fixture; the lapse not run live].
- **Worker step `titlecontinue <role> <s>`** (`5293dc2`): Enter only while that role's title runs (loading.json
  `running` for 40010/40020/40050) and done once its next allocation is named, so its next loading screen is never
  pressed. With `waitmission guest <m> <s> only` it holds the host on its tips screen in `-Mode play` (one Enter is not
  enough: `c07b8136` needed four at the running title).
- **`world_vehicle_crew` on in play again** (`3929a6c`), after the new path's lab FRAME (`c0a385a5`) and play rehearsal
  (`c6db67b8`), then rehearsed at the shipped defaults (`98d84a21`); the D11 fix stays on (part of
  `world_loadout_block`) after its play rehearsals (`c6db67b8`, `98d84a21`, below).

Hidden runs (research build, native gun stage, copied saves, `africa-jeep` seed; each Closed, BaselineRestored,
RealSavesUnchanged, no `step=error`, `viewer_changed` or non-zero `faults=` line; archived under
`%USERPROFILE%\mgs-coop-run-archive\next2\<id>`; slices in the session scratch `next2/vehicles/slice-<run8>`; frames as
JPEG in `complete/frames/next2-vehicles/<run8>-<role>-<label>.jpg`, sheets `next2/vehicles/g-c0a-*.jpg`,
`g-c6d-*.jpg`, `g-98d-*.jpg`; every frame claim below was looked at):

| Run | Scenario, mode | Result |
|---|---|---|
| **`c0a385a5`** | **`vehicle-review-lab.txt` at `2d47238`, lab, every key named** | **Every expectation met** (`Forced=True` at shutdown): the host's half bound twice, the crew hold with the guest 33 m away and the wreck once it stood 63 m away, the host 60 m away |
| **`c6db67b8`** | **`vehicle-review-play.txt` at `3d2fc8b`, play, `-WanSim bad`, `world_vehicle_crew` named** | `Forced=False`, link 176-188 ms, loss up to 3.8 %. D11 before PAIRED in play: LOADOUT refused with its toast while unpaired, the host on its tips screen; the VEHICLES refusal not reached (the alerted Masa guards' hits closed the guest's iDroid; 2 expectations failed). Crew: every expectation met (the hold, the release, the wreck, frames on both) |
| **`98d84a21`** | **`vehicle-review-play.txt` at `b4cd471`, play, `-WanSim bad`, no vehicle key named (the shipped defaults after `3929a6c`)** | `Forced=False`, link 176-190 ms, loss up to 3.2 %. Every behaviour met: D11 before PAIRED (both items refused, both toasts drawn, the host on its tips screen), the crew hold and wreck at the default, no VEHICLE_DAMAGE line on either copy (off by default). One expectation failed by the scenario's design: the VEHICLES toast was drawn (frame) but its log line fell inside the HUD toast log's 10 s gap per note code (`kToastLogGapMs`; both items use NOTE 13), because the scenario waited 3.5 s between the items |

**VEHICLE_DAMAGE's host bound** (`c0a385a5`, lab, the guards cleared) [O, logs of both copies; frames
`c0a385a5-*-dmg-*`: the jeep on both screens; the engine draws no damage at these lives (`d407f115`), so the frames do
not show the shared life]. The seed's jeep 25608 read 2260 on both copies. The guest's lever `vehicle_life 25608 600`
-> `tx;life=600;drop=1660` -> host `applied;life=1130;was=2260;drop=1660;d_mm=1566;f=0;cap=1` 27 ms later -> the host's
VEHICLE_STATE -> guest `vehicle_state;applied;life=1130;was=600` 146 ms after its report (the guest's copy raised to the
host's capped life: VEHICLE_STATE keeps a lower life only while the host's state is not below the life before the
drop). Again to 300: `tx;life=300;drop=830` -> host `applied;life=565;was=1130;cap=1` -> guest `applied;life=565`.
Both copies read 565 two seconds later and 14 s later, past the host's refresh.

**D12 crew with the blast distance by type.**
- *`c0a385a5`, lab* [O, logs of both copies; frames `c0a385a5-*-crew-*`, sheets `g-c0a-blast35`, `g-c0a-wreck`]: the
  host asked the ignore from 12 m, then stood 60 m west of 25623 (`position=-433.265,-0.865,489.178`), the guest 33 m
  east (`-341.435,-1.964,489.182`). The guest's lever broke its own copy: frames `crew-blast35-01..05` show the
  explosion and the guest standing throughout (no knockdown at 33 m, where 25 m knocked the guest flat in `ac0f7656`);
  no `downed_damage` sample on the guest (but the 25 m knockdown logged none either, so the hook does not see that
  blast's damage) [O for the knockdown, U for its damage]. Guest `break_seen` -> `req;cause=4` -> host `rx_req` 32 ms
  later -> `break_hold;why=crew_near;near=partner;d_mm=33479;crew=1;blast_mm=50000` (no judge, no wreck); frames
  `crew-held`: the guest's copy burning on its screen, the host's copy intact on the host's. The guest moved 63 m east
  (`-311.509,-2.072,489.182`) -> host `break_judge;ok=1;why=near;d_mm=33470` (the guest's position at the request's
  arrival) -> `break_crew;crew=1` -> `applied;via=wreck;ms=250` -> guest `req_done` 10.2 s after its request. Frames
  `host-crew-wreck-*` and `-after-*`: the fire, then the 8 s explosion's smoke column at the vehicle; the host 60 m away
  stays on its feet, no `downed_damage` sample on the host. The driver died once on each copy: guest `obs;m=Dead` 8.0 s
  after its own break -> `divergence;why=not_attributed`; host `obs;m=Dead` 8.0 s after its wreck -> one
  `commit;state=5;cause=2` -> guest `apply;result=terminal_local` 17 ms later.
- *`c6db67b8`, play, `-WanSim bad`* [O, logs of both copies; frames `c6db67b8-*-crew-*`, sheet `g-c6d-crew`]: the host
  asked the ignore from 9 m, stood 60 m west; the guest placed three charges from 5 m behind the vehicle and detonated
  them from 25 m (`-398.425,-0.732,496.675`). Guest `VehicleBroken` (its driver died at once there, `not_attributed`)
  -> `req;cause=4` -> host `rx_req` 110 ms later -> `break_hold;why=crew_near;near=partner;d_mm=24702;crew=1;
  blast_mm=50000`; frames `crew-held`: the burning wreck on the guest's screen, the intact vehicle on the host's. The
  guest moved 70 m west (`-442.943,-1.940,496.492`) -> host `break_judge;ok=1;why=near;d_mm=24661` -> `break_crew;
  crew=1` -> `applied;via=wreck;ms=250` -> guest `req_done` 10.4 s after its request; the host's wreck exploded 8.05 s
  later and killed the driver there: one `commit;state=5;cause=2`, applied on the guest as `terminal_local` 127 ms
  later. Frames `host-crew-wreck-06` (fire at the vehicle), `-after-02/04` (the smoke column and the burning wreck), the
  host on its feet 60 m away. Host readback `alive=0;life=0;sv=1013`, guest `alive=0`.
- *`98d84a21`, play, `-WanSim bad`, the shipped defaults* [O, logs of both copies; frames `98d84a21-*-crew-*`, sheet
  `g-98d-crew`]: the same approach with no key named (`vehicle_rules;step=on;...;crew=1` from the play default): host
  `rx_req` 370 ms after the guest's request -> `break_hold;why=crew_near;near=partner;d_mm=25089;crew=1;blast_mm=50000`
  -> the guest 70 m west -> `break_judge;ok=1;why=near` -> `break_crew;crew=1` -> `applied;via=wreck;ms=250` -> guest
  `req_done` 10.5 s after its request; the driver died once on each copy (one `commit;state=5`, a `terminal_local`
  apply on the guest 122 ms later). Frames: the intact vehicle on the host's screen while the guest's copy burns, then
  the host's fire and smoke column; the host on its feet 60 m away.

**D11 before PAIRED in play.**
- *`c6db67b8`, play, `-WanSim bad`* [O, logs of both copies; frames `c6db67b8-*-d11p-*`, sheet `g-c6d-d11`]: both
  titles paired at 40020; `titlecontinue host` -> host `follow;tx;src=40020;next=30020;kind=4`; only the guest's
  loading was resumed: guest `follow;arrived;mission=30020` -> both guard lines 1.3 s later
  (`world=pair_wait;reason=remote_loading`), `world_paired` false four times; the guest's real iDroid path: MISSIONS,
  SUPPLY DROP, LOADOUT and VEHICLES both marked inactive; LOADOUT -> `step=refused;path=idroid_drop_loadout` and
  `hud=toast;code=13;a=5;partner=0;on=1` 25.5 s after the arrival (frame `d11p-guest-loadout-refused`: "Only the host
  can order a loadout drop in co-op: it can carry a vehicle."); the host's screen showed its tips screen ("Space Resume
  Game") in `d11p-host-waiting` and `-still-waiting`; PAIRED came 54.9 s after the arrival, once the host's loading was
  resumed. The VEHICLES refusal was not reached: the seed's Masa Village guards were in combat at the arrival (night,
  "Combat positions!") and their hits (`downed_damage` attacker 1080, 1600 damage) closed the guest's iDroid (frame
  `d11p-guest-vehicles`: the guest prone, no iDroid) [O for the closed iDroid, I for its cause].
- *`98d84a21`, play, `-WanSim bad`, the shipped defaults* [O, logs of both copies; frames `98d84a21-*-d11p-*`, sheet
  `g-98d-d11`, zoom `g-98d-toast-zoom`]: the same start; the guest arrived (`follow;arrived;mission=30020`), the guard
  lines 1.05 s later, then the guest moved 70 m west of the armoured vehicle (away from the alerted Masa Village
  guards) and opened its iDroid: LOADOUT -> `refused;path=idroid_drop_loadout` and `hud=toast;code=13;a=5;partner=0`
  28.1 s after the arrival (frame `d11p-guest-loadout-refused`, the toast drawn); VEHICLES -> `refused;
  path=idroid_drop_vehicle` 33.7 s after the arrival, and the frame `d11p-guest-vehicles-refused` shows its toast
  ("Only the host can bring or request a vehicle in co-op.") under the HUD's status line, which names the partner as
  `loading`; the toast's log line was not written (the HUD logs one toast per note code per 10 s). `world_paired` read
  false four times; the host's frames show its tips screen; PAIRED 45.2 s after the arrival.

**Play defaults.** `world_vehicle_damage`: off in play unless named (no live-AI-fire proof). `world_vehicle_crew`: on
in play unless named, with the blast distance by type (lab FRAME `c0a385a5`, the play rehearsal `c6db67b8` with the key
named, again at the shipped defaults `98d84a21`). The D11 fix stays part of `world_loadout_block` (on): FRAME before
PAIRED in the lab `251adcb6`, in play `c6db67b8` (LOADOUT) and `98d84a21` (both items, both toasts drawn).

**Open.** The damage report against live AI fire, or a shot attribution, before it can be on in play [U]; the
armoured blast's damage (not its knockdown) between 25 and 50 m [U]; `blast_near` and the pre-pair lapse live [U];
`crew_near` with the host's own player inside the 50 m [fixture]; a Despawn under seated soldiers [U]; an alerted
enemy vehicle that drives off (M8); every case on two PCs.
### 8.16 Heroism at a copy's death: given back on the guest (2026-10-29, branch `wp/dog-heroism`)

The user approved it on 2026-10-29: when the guest's copy of the host's D-Dog dies on the guest's PC (8.10, `9fa48400`:
"D-Dog Near Death [Heroism -30]" on the guest's screen only; `f729d7f4`: "(Heroism -60)" and "Bond with D-Dog Has
Decreased Significantly" for the guest's own C4), the guest's save must not pay for a death that is not real. Built on
`wp/dog-heroism` (from `dev/oct7-continue` `01fd055`) behind M1's key `companion_host_owned`; Lua only, guest only.

**Static reads (capstone on `085c2f82...bb45`, read-only; no Ghidra/REA session)** [O]:
- **No Lua.** The extracted `TppHero.lua` has no buddy entry; the penalty is native.
- **Where it is applied.** The dog's life update (`0x140CA4C40`, 8.9) on the change to state 2 (`0x140CA4FF6`) reads the
  last attacker (`[record+0x60]` slot `0xD0`), sets *by player* when it is a player object (`id != 0xFFFF` and
  `(id & 0xFE00) == 0`, ids 0..511), calls the buddy service's slot `0x250` with (BuddyType 2 = DOG, by player)
  (`0x140CA504E`), builds the announce from the service's `+0xA4` (the penalty: the "[Heroism -30]" of the notice) and
  then posts `Dead` (`0x140CA5105`). The service is the static `BuddyServiceImpl` at `0x142C29030` (vtable
  `0x1422BC070`, constructor `0x140A4D6B0`; the Lua `TppBuddyService` bindings call the same object).
- **The penalty** (slot `0x250` `0x140A4F6A0` jumps to slot `0x258` = `0x140A4F440`): returns at once when the type's bit
  in `deadBuddyType` (vars `+0x2D6C`, slot `0x3C0`) is already set; else heroism 60 and demon points 150 (types 4 and 5:
  30 and 75), halved heroism and **no** demon points when no player made the kill; the bond of the type's
  BuddyFriendlyType (slot `0x408`: HORSE 2, DOG 1, QUIET 0) changes by -100 raw (a player's kill) or -20 raw (slot
  `0x420`, `vars.buddyFriendlyPoint` u16[8] at `+0x2DE4`, clamped 0..1000; `GetFriendlyPoint` returns raw / 10, so -10 or
  -2 bond points); heroism `[MBM+0xA28]` loses min(penalty, the total `[+0xA20]+[+0xA28]`) through the same virtual
  `+0x168` that the Lua `SubHeroicPoint` / `AddHeroicPoint` use; demon points `[MBM+0xA38]` grow; the penalty is kept
  at `+0xA4`; then the type's `deadBuddyType` bit is set (slot `0x3C8`: the 0 -> 4 seen since E19). So for D-Dog: **30
  heroism and 2 bond points** for a guard's kill, **60 heroism, 10 bond points and +150 demon points** for a player's
  kill (the guest's own C4 in `f729d7f4`, or the host's body, player 1 on the guest's PC).
- **The Lua side:** `TppMotherBaseManagement.GetHeroicPoint` (`0x140A8E550`, the clamped total), `AddHeroicPoint`
  (`0x140A87EB0`, `+0xA28` += n), `GetOgrePoint` / `SubOgrePoint` (`0x140A914B0` / `0x140AB1450`, `+0xA38`);
  `TppBuddyService.Get/Set/AddFriendlyPoint` (`0x140A51170`, `0x140A54450`, `0x140A4DA70`; points x 10 = raw).
- **Prevent or give back.** The only Lua-reachable gate is the `deadBuddyType` bit, and a set DOG bit stops every
  reload (8.2, `dc761c98`) and makes the stock scripts treat D-Dog as dead (`IsDeadBuddyType`), so it cannot be kept set
  on a live copy, and a death the guard cannot see cannot be foreseen. The notice itself is posted by the same native
  code. So the guest **gives the amount back** right after the death; the notice still shows.

**Built** (`85ba7ec`, `159ae49`, `e905f99`; fixtures written first: `world_companion_guard_lua_check` failed its check 116
before the give-back and its check 143 before `kill_own`, red logs in the session scratch `next2/dog/red-lua.log`,
`red-lua2.log`; 115 -> 149 checks):
- `lua/coop/world_companion_guard.lua`, guest only, while `C.applies()` (the key on both PCs, the guest's dog being
  the host's copy): every 100 ms tick samples heroism (`GetHeroicPoint`), demon points (`GetOgrePoint`), the raw dog
  bond (`vars.buddyFriendlyPoint[BuddyFriendlyType.DOG]`) and the DOG bit; a sample in which the bit has just appeared
  (the penalty landed, its `Dead` not yet seen) keeps the one before it (for 1 s at most). At each `Dead` of 9728 it
  compares that sample with the values now and gives back what was lost, **never more than the stock amount** (30
  heroism and 20 raw bond, or 60, 100 raw and the 150 demon points for a player's kill, from the attacker id; with no
  id, at most the player amounts), and nothing when the bit was already set before the death (the stock gate: the
  game took nothing): `AddHeroicPoint{heroicPoint=n}`, the raw bond written back, `SubOgrePoint{ogrePoint=n}`. Once
  per death (a repeated `Dead` within 1 s is the same death); a penalty that lands after the `Dead` is settled at the
  next tick, and nothing lost within 1 s is logged once (`step=none`). Every copy death counts: the restore cases,
  an unknown host state, and the mirrored down (the host's dog really fell; the host pays on its own PC, the guest's
  save does not pay for the host's dog). Log `event=dog_hero;role=guest;step=given;by=other|player|unknown;att=<id>;
  h=<before>:<after the penalty>:<readback>;b=...;o=...;give=<heroism>,<raw bond>,<demon points>;ms=..;n=<death>`
  (`step=none`, `step=no_sample`, `step=error`), within W.log's 160 bytes.
- `lua/coop/world_sync.lua` hands the raw attacker id to `W.companionHurt` (the stock rule's player test needs the id).
- Lab lever `companion_lab hero` (both roles; also in play: read only): `hero;role=..;heroic=..;ogre=..;bond=..;
  bond_raw=..;dead_bit=..;applies=..;deaths=<given back this mission>`.
- Lab lever `companion_lab kill_own` (guest, lab only; refused while the session is host-owned, where `kill_mirror` is
  the lever): the native kill flag on the guest's **own** dog through a lab op with arm 0 (`dog_guard::Admit` grants a
  lab op without an arm while linked), for the control below.
- Nothing changes on the host or with the key off (the shipped default), and nothing on the wire (protocol section 15).

**Runs** (hidden worker, loopback, research build, native gun stage, copied saves, low graphics; every run closed with
the baseline restored and the real saves unchanged; slices cut at the session offsets of both copies'
`script-observer.log`, no other run appended; archived under `%USERPROFILE%\mgs-coop-run-archive\next2\<run>` with the
worker's probe folder; frames as JPEG in the session scratch `complete/frames/dog/<run4>-<role>-<label>.jpg`, each
looked at):

| Run | Scenario | What it is for |
|---|---|---|
| `c7fdfdbf` | `companion-dog-heroism.txt` (`159ae49`), lab, `companion_host_owned=1`, host `afgh-ledge`, guest `afgh-horse` | three copy deaths with readbacks on both copies: the lab kill, the host's dog down (mirrored), a real death by guard fire with the guard off; **every expectation met** (ExpectFailures empty) |
| `a7f00043` | `companion-dog-heroism-off.txt` first version (`159ae49`), lab, both keys off, `africa-jeep` | the control by the guest's own C4: **failed** (two expectations: no `Dead`). The C4 went off near the host's body, about 18 m from the guest, not at the dog: the host's dog (and the guest's, held at its pose by the legacy mirror) had moved away (both read at the same spot about 30 m south afterwards), and the blast on the guest's PC reached the host through the friendly hit (`friendly_hit=owner; damage_milli=5500000; applied=1` on the host, the host downed despite godmode: "WOUNDED, Crawl to cover", `a7f0-host-hc-blast-02`). Nothing of this branch ran (`dog_guard=off; reason=key_off` on both, no `dog_hero`); the guest's heroism fell 10 (its own blast injury [I]) |
| `2243bbc6` | `companion-dog-heroism-off.txt` (`e905f99`), lab, `companion_host_owned=1` on the guest only, `africa-jeep` | the control: the guest's own dog dies by `kill_own` in a session that is not host-owned; **every expectation met** |

| # | Result | Evidence |
|---|---|---|
| The lab kill while the host's dog lives | **Pass, both copies** [O]. Guest readback before `heroic=244995; ogre=48950; bond=100; bond_raw=1000; dead_bit=0; applies=1`; `kill_mirror` -> `Dead` 9728 (`att=none`, raw 65535) -> `dog_restore; step=dead; host=alive` and in the next ms `dog_hero; step=given; by=other; att=65535; h=244995:244965:244995; b=1000:980:1000; o=48950:48950:48950; give=30,20,0; ms=0; n=1` (the stock penalty exactly 30 heroism and 20 raw bond, demon points untouched, all back). Guest readbacks right after the death and after `step=back` (2719 ms): `heroic=244995; bond_raw=1000; dead_bit=0; deaths=1`, the same as before. Host readbacks before, after and after the restore: `heroic=244995; ogre=48950; bond_raw=1000` unchanged, no `dog_hero` line on the host in the whole run. FRAME: on the guest's screen the copy lying in the grass beside the host before (`c7fd-guest-hr-before`), carried off by the stock Fulton balloon at +0.3 s (`hr-dead`) and the stock notice "D-Dog Near Death [Heroism -30]" at +1.5 s (`hr-dead-2`: the notice still shows; the heroism is back); on the host's screen at the same moment the host's dog stands beside Snake and no notice shows (`c7fd-host-hr-dead-2`); the copy back beside the host (`c7fd-guest-hr-back`) | `c7fdfdbf` |
| The host's dog down (mirrored) | **Pass, both copies** [O]. Host `dog_life 0` -> the host's own stock penalty stays: host readback `heroic=244995 -> 244965; bond_raw=1000 -> 980; dead_bit=1` (a host-side death, left alone); the guest's copy follows (`dog_guard=follow; byte=0xc0; kill=1`) -> `Dead` (`att=none`) -> `step=dead; host=down` -> `dog_hero; step=given; by=other; att=65535; h=244995:244965:244995; b=1000:980:1000; ...; n=2` -> guest readback `heroic=244995; bond_raw=1000; deaths=2`, again after the host's re-call (`step=recalled; ms=34134`). FRAME: "D-Dog Near Death [Heroism -30]" and the extraction on both screens (`c7fd-host-hr-down`, `c7fd-guest-hr-down`): the host paid, the guest got it back | `c7fdfdbf` |
| A real death by guard fire (guard off) | **Pass** [O]. The copy held in the line of fire of an alerted guard with the guard off: `Dead` 9728 `att=other` 62.8 s after `guard off` -> `step=dead; host=alive; armed=0; drops=4` -> `dog_hero; step=given; by=other; att=1206; h=244985:244955:244985; b=1000:980:1000; o=48950:48950:48950; give=30,20,0; ms=0; n=3` -> restore `step=back; ms=2585`; guest readbacks just before `guard off` and after the death and after the restore all `heroic=244985; bond_raw=1000` (`deaths=3`). The guest's heroism had dropped 244995 -> 244985 between the re-call and the shield, and the host's 244965 -> 244955 -> 244945 during the fight: the stock game's own 10-point combat penalties on each PC (their cause, an alert or an injury under godmode, [I]); the give-back measured only the death (its sample was taken within 100 ms before it). FRAME: the notice "D-Dog Near Death [Heroism -30]" on the guest's screen beside the prone guest and the guard (`c7fd-guest-hr-fire-dead`); no notice on the host's (`c7fd-host-hr-fire-dead`, the prone guest under fire) | `c7fdfdbf` |
| Once per death; the host and the key | [O] three deaths, three `step=given` lines (`n=1..3`), the readback's `deaths` 1, 2, 3; no `dog_hero` line on the host (its own dog's death kept the stock penalty, above); no `step=error`, `event=error` or `viewer_changed` on either copy | `c7fdfdbf` |
| Control: the guest's own dog keeps the stock penalty | **Pass, both copies** [O]. The host runs without the key (`dog_guard=off; reason=key_off`), the guest with it (`dog_guard=installed`, `dog_guard_code_gate=ok`); without the host's COMPANION signal the guest's peer agreement never forms, so its readback says `applies=0` and its buddy is its own (`kdog`, `vars_type=2`; the legacy mirror holds it at the host dog's pose: `result=target_submitted`). Readbacks before: guest and host `heroic=244985; ogre=48950; bond_raw=1000; dead_bit=0`. `kill_own` -> `kill_own; ready=true; reason=ok; installed=1` -> `dog_guard=lab; op=1; pct=0; kill=1; wrote=1; guest=1` -> `Dead` 9728 (`att=none`) -> guest readbacks right after and 5 s later `heroic=244955; bond=98; bond_raw=980; dead_bit=1; applies=0; deaths=0`: **30 heroism and 20 raw bond lost and not given back**; no `dog_hero` and no `dog_restore` line on either copy; host readbacks `heroic=244985; bond_raw=1000` unchanged. FRAME: the stock notice "D-Dog Near Death [Heroism -30]" on the guest's screen (`2243-guest-hc-dead-2`; the dog itself is out of view behind the host's tree, so the death rests on the log and the notice); none on the host's (`2243-host-hc-dead-2`). Not run: a session with the key off on **both** copies and a death of the guest's dog (no lever reaches its record without the guard installed; the C4 control failed, above): that case is the fixture's (`key off: the guest's own dog's death keeps it`) and the code's (`G.hurt` and the tick return before anything while `C.applies()` is false, and `C.applies()` needs the key) [I] | `2243bbc6` |

**Still open:** play has no rehearsal of the give-back: `companion_host_owned` stays **off in lab and play** (unchanged),
and under the key play refuses every lever that kills the copy (`kill_mirror`, `guard`, `dog_life`), as it does for
the rest of D7 (section 9 M2). The by-player branch (60 heroism, 100 raw bond, 150 demon points) is fixture-only:
none of the three copy deaths had a player attacker (`att=65535` twice, a guard `1206` once) [I from the stock rule].
Whether the give-back reaches the guest's **save** rests on the stock save of `mbmTppHeroicPoint` and
`buddyFriendlyPoint` (both saved vars) [I]: no run reached a checkpoint or mission end after a death. The stock play
counters of heroism added and taken (`mbmPdTotalAddedHeroicPoint` / `...Subtracted...`) both move by the amount
(the virtual `+0x168` that `AddHeroicPoint` and the penalty share) [I]. The stock notice cannot be suppressed from Lua
(it is posted by the same native code) and still shows. A death in Mother Base, a fall or drowning were not run; no
two-PC session.
### 8.17 The 0.14.4 review's launcher and package findings (2026-10-29, branch `wp/launcher-fixes2`)

Release tooling and the launcher only. No vehicle or companion behaviour, play default, op code, CFG index or lab key
changes, so the milestones (section 9) and the decisions (section 10) stand as they are. What touches this design:
the Release receipt (the check run a player package is built from) now also runs the fixtures of this design's shipped
code. Before, it left out `dog_guard_tests` (D7's native guard), the companion, buddy companion, dog guard, relief
and relief sync Lua checks (M1-M3, D7) and the outcome attribution check (F12); only the full CTest covered them.
Receipt `20261030-014929-6188027e7b644a37bf51164ae9fc7020` runs 311 tests, all passed [O], and refuses any registered
test it neither runs nor excludes by name. The other four findings (two automatic launcher setups at once after a
close during setup, the tag and network wording, untracked files in the source zip) are in docs/STATUS.md, section
"the 0.14.4 review's launcher and package findings fixed", with their failing-first tests and hidden end-to-end runs.

### 8.18 Round 2 of that review (2026-10-29, branch `wp/launcher-fixes2`)

Release tooling, the launcher and tests only; sections 9 and 10 stand as they are (no behaviour, op code, CFG index or
lab key change, no play default). What touches this design: the eleven fixtures of its shipped code that 8.17 added to
the Release receipt (`dog_guard_tests`, four partner fixtures, and the companion, buddy companion, dog guard, relief,
relief sync and outcome attribution Lua checks) stay in every profile's list, so each hidden run's preflight runs them
too (0.02-0.16 s each in the receipt [O]). Only the launcher's two checks (`ddt_local_watch_process`,
`launcher_icon_tests`) moved to the Release receipt alone [O: `-PlanOnly`, 309 tests for Normal and Research against
311 for Release]. No Normal-profile worker check ran [U]. The other findings (the tag and source zip wording, the
package test and a developer's untracked files, the fail-closed refusal, the abandoned setup lock, a start in the
closing window's last moments, the end-to-end harness outside `tools/worker`) are in docs/STATUS.md, section "the
review of those launcher and package fixes, round 2".

### 8.19 Results: the fixes after 0.14.4 merged, and their combined play smoke (2026-10-30, `dev/oct7-continue`)

The vehicle fixes (8.14, 8.15; merge `f1e80be`), the dog's heroism give-back (8.16; merge `101d925`) and the launcher
and package fixes (8.17, 8.18; merge `416b2fa`) are merged into `dev/oct7-continue`; unreleased, not two-PC tested.
The three tracks each wrote a section 8.14, so the dog's became 8.16 and the launcher's 8.17 / 8.18. No op code or CFG
index clashed: VEHICLE_DAMAGE `0x3A` and `r[15]` bits 64 / 128 are the vehicle fixes' only additions, the other two
tracks add none. Play defaults after the merge: `world_vehicle_crew` **on**, `world_vehicle_damage` **off** (lab key),
the D11 pre-pair mark on with `world_loadout_block`, `companion_host_owned` off (so neither D7's guard nor the heroism
give-back runs in a default session).

One hidden run of the merged head (`wp/next2-smoke` = `8cb81f8`, built incrementally in `D:/mgs-coop-wt/relief`,
research worker build, native gun stage). Scenario `tools/worker/scenarios/next2-smoke-play.txt` (8.13's parts, with
the rules' on-line now carrying `crew=1`), `-Mode play -WanSim bad`, seed `africa-jeep` (30020), **the shipped play
defaults**: no labsetting line and no lab lever. Run `32c3893a301e4d579d76894397047bde`: **every expectation met**
(ExpectFailures empty), Closed, BaselineRestored, RealSavesUnchanged, `Forced=False`; no `step=error`, `event=error`,
`viewer_changed`, non-zero `faults=`, `via=wreck`, `event=vehicle_damage` or `event=dog_hero` line on either copy.
Link: RTT 174-189 ms, loss up to 13.3 %. Slices in the session scratch `next2/integrate/slice-32c3893a`; frames
`complete/frames/next2-integrate/32c3893a-<role>-<label>.jpg`, looked at for every claim below. Archived under
`%USERPROFILE%\mgs-coop-run-archive\next2\32c3893a...` (with the probe folder `df567698...`). Its preflight
(`20261030-044444-8a250766...`, Research profile with the background worker on) ran the merged list's 310 tests, all
passed [O].

| Part | Result |
|---|---|
| Pairing and defaults | [O] Both copies reached gameplay (53.0 s / 51.0 s after launch) and paired (`remote_build=0.14.4+8cb81f8`; PAIRED epoch 3 in 30020, 265 soldiers matched). Both logged `vehicle_gone; step=on; ...; wreck=0`, `vehicle_rules; step=on; ...; break=1; credit=1; loadout=1; gone=1; crew=1` and no `vehicle_damage` on-line; the host's sweep finished and every guest VEHICLE_STATE line was `step=same`. `dog_guard=off;reason=key_off;slots=0` and `dog_state ... installed=0` on both. Frame `smoke-start` on both screens: the jeep with the partner beside it. |
| D11 | [O] The guest's LOADOUT and VEHICLES items inactive and refused with both toasts drawn (frames `d11-guest-loadout-refused`, `d11-guest-vehicles-refused`); the host's VEHICLES list opened (`d11-host-vehicles-open`). The pre-pair window did not occur here (PAIRED 1.1 s after the guest's arrival); its rehearsals are `c6db67b8` and `98d84a21` (8.15). |
| A drive with a passenger, then parked | [O] The guest drove 25608 with the host in a back seat (`board_passenger ... seat=2` / `partner_ride_seat=confirmed ... seat=2`; frames `drv-seated` on both screens, `drv-pass-NN` from the host's seat, `drv-driver-NN`), stopped under a tree (`drv-stopped` on both). The host's VEHICLE_STATE skipped 25608 while ridden, then `resume; was=partner_ride` -> `tx; why=moved; d_mm=23664` -> the guest `step=same; d_mm=1`: the jeep stands under the same tree on both screens (`drv-parked-both`). |
| The guest's damage staying damaged | Not run: `world_vehicle_damage` is off in play (8.15), so in a default session the guest's damage is still put back at the host's next state (M4). |
| D14, a vehicle Fulton | [O] The host's hand Fulton of 25610: host `credit; via=own; res=14`, guest `credit_seize` -> `credit; via=receiver; res=14` -> `applied; via=seize; ok=1; ms=133`, one credit per copy; the lift on the host's screen (`fult-lift-03`) and the jeep gone on both (`fult-after`). |

Not in this smoke: D12's crew rule (its rehearsals at the shipped defaults are `34d5744d` and `98d84a21`), the D11 window
before PAIRED, a dog death, the launcher. No defect found.

### 8.20 Results: M7, shooting as a passenger (2026-10-30, branch `wp/seat-shoot`)

Branch `wp/seat-shoot` from `dev/oct7-continue` `60e0d13`, in `D:/mgs-coop-wt/companion-host`; local only. The 0.13.5
seat-aim census (`D:/mgs-coop-wt/seat-aim`, left uncommitted on 2026-10-22) is committed there as `c87b270` and merged
with a merge commit (`5e2ce28`: both sides' keys, allow-lists, tests and docs kept; the partner contract regenerated).
Hidden runs (research build, native gun stage, seed `africa-jeep`, lab; every one Closed, BaselineRestored,
RealSavesUnchanged; `Forced=True` in five of them): `6fc3098b28344e27afe917f36a600a72` (E8/E9 census),
`d40e412636b04b58b5e4afeb40202dfa`, `30a6a21a4c784049b79c259dd920977c`, `6e0edd8b577f4df48f80b78cf3fd7541`,
`03ceff689ef24091a4194b0ab1a6fbe7`, `d3409f579a724f5589113b0b69f1ce7f`, `a3f7c4e184d0490c833a4ee14a75beb1`
(`coop-vehicle-seat-shoot.txt` as it changed) and `d7cf5a4b4e3e4c6c82b136fdd088e4e2` (`coop-vehicle-seat-shoot-diag.txt`).
The budget of eight runs is spent; no play rehearsal ran. Slices in the session scratch `m6m7/shoot/slice-<run8>`,
frames `complete/frames/shoot/<run8>-<role>-<label>.jpg` (looked at for every claim below); run folders archived under
`%USERPROFILE%\mgs-coop-run-archive\m6m7\<id>`.

**E8, the local passenger.**
- [O] A stock back-seat passenger raises and fires its own gun with no patch (`6fc3098b`, census on): once the seat loop
  runs the passenger phase clears flag 0xC3 (`partner_seat_aim=sample; who=local; k=0; act=0x6e; ... armed=1; c2=0;
  c3=0`); RMB gives upper act 2 with gun modes 6 / 7 (`posture=0`, the standing upper-body set), RMB+LMB upper act 3 /
  mode 9, an empty magazine upper 5 / mode 11 (reload). The magazine went 8 -> 1 on the HUD; the act stayed 0x6E and the
  passenger camera handle `0x8800000000006408` throughout. Frame `aim-seated` of `637b47ea` (archived as
  `complete/frames/0136-safety/seated-aim-637b47ea-lab-seated-and-on-foot.jpg`) shows the gun on the guest's back: it was
  captured after the RMB holds ended, so it never showed an aim.
- [O] The aim self-check never latched: `aim_dir=refused; reason=ride; checks=0; misses=0` while seated in `6fc3098b`,
  and with `partner_seat_gun` on `aim_dir=valid; reason=seat; checks=0; misses=0` (below) in every later run. No
  `reason=convention` in any run. Nothing needed fixing: the 0.13.6 `RideGate` already skips the check while riding.
- [O] The seated body faces into the jeep: body 1's yaw on the driver's copy was 62.6 deg in seat 2 (`30a6a21a`) and
  -117.4 deg in seat 3 (`03ceff68`, `d3409f57`) with the jeep at 152.6 deg, and the passenger's own sent directions lie
  within +-90 deg of the same values. The stock aim range (`0x108E7D0`, the default class: 75 deg pitch, 90 deg yaw) is
  measured from that root, so a passenger aims across the jeep, forward or back, never out of its own side: aiming out
  of the left of seat 2 (`30a6a21a`) or the right of seat 3 (`03ceff68`) clamped (`r60` 0 or 1).
- [O] The aim camera sits behind the passenger: from seat 2 aiming across to the right it stood outside the left side
  and looked through the jeep's body (frames `d40e4126-guest-m7-seat-aim-*`: the wheel arch and panel; the passenger
  camera skips the jeep's collision); from seat 3 aiming across to the left it sits over the passenger's head with a
  clear view (`6fc3098b`, `d3409f57`, `a3f7c4e1`). The passenger camera keeps the direction the player faced when it
  boarded.
- [O] At the shot the aim swung up: with the bursts of `d3409f57` the sent pitch reached -74.7 deg (`r5c` 0, the top of
  the range) and frames `m7-fire-guest-1/2` show the camera at the sky under the passenger's head; in `a3f7c4e1` single
  shots (LMB taps under a held RMB) did the same in each post-shot frame. In `6fc3098b` the held bursts put the camera
  inside the passenger's own head (`seat-fire-guest-02..04`). Cause [U] (recoil applied to the standing set over a
  seated root is the guess [I]).
- [O] No seated shot was seen to land: the targets were a guard warped 9-16 m away and put to sleep (`enemy sleep
  nearest`), aimed at with the new lever `enemy_aim_nearest` (below). In `a3f7c4e1` the reticle was red on the
  sleeping guard right before the shots (frame `m7-seat-aim-02`), 7 fire samples followed (gun mode 9; the HUD
  magazine 8 -> 3), and the guest's copy logged no
  Damage or Dead for him and no REQ_OUTCOME. In `d7cf5a4b` (the diagnostic: shots while driving, standing still and in
  first person, then on foot) the pre-shot frames did not show the guard under the reticle, and the on-foot control
  missed as well, so the test method itself is not reliable: whether a seated shot can land is [U].
- [O] The board needs the player to face the jeep: from 1.9 m out on the passenger side facing away neither copy
  boarded (`6e0edd8b`); the debug command `goto_vehicle jeep right` (new) gives the passenger-side spot facing it, and
  seat 3 boarded from there (`03ceff68` and later).

**E9, the partner's gun on the driver's copy (design A).**
- [O] Baseline (`6fc3098b`): body 1 in the seat flow has act 0x8B, upper act 0, flag 0xC3 set and the stand-in gun 46 in
  hand, and stays seated with its gun on its back while the remote aims and fires (frames `seat-aim-host-*`,
  `seat-fire-host-*`); the remote's live pose flags say so (`partner_action_state; flags=0x1|0x3; act=110`).
- Built (lab key `partner_seat_gun`, off in lab and play; docs/native-puppet.md "Seat gun"): the gesture hook puts
  graph 2 of the DM-seated body 1 on GunHoldStart while the remote passenger aims, would chain to GunHold at the raise
  clip's end, writes body 1's aim rates from the remote's seated direction and returns stateNull when the aim stops.
- [O] Its first run (`d40e4126`) started no hold at all: the driver tick returns from the seat-owned branch before
  `TickGesture`; fixed (`8c0d482`).
- [O] From `30a6a21a` on, every aim ran the whole path on the driver's copy (the host's in the guest-shoots order, the
  guest's in the reverse order; 20 holds in five runs, each ended by its release): `partner_seat_gun=hold; seq=..;
  vehicle=25608; seat=2|3` -> `partner_gesture=submit; graph=2; node=0x50130ae2e584; exist=1; kind=gun_start; ...;
  body_act=139` -> `applied; ...; current=0x50130ae2e584; ok=1` -> `partner_seat_gun=rates; ok=1` (the written `r5c` /
  `r60` read back on body 1) -> `release; cause=aim` -> `partner_gesture=ended; via=end; cause=release; ...; null=1`;
  no `uncertain`, `viewer_changed` or fault line. The raise never reached its motion end (vt+0x228 stayed false for up
  to 5.6 s), so GunHold was never requested.
- [O] **No visible change:** the driver's frames during the holds (`30a6a21a-host-m7-partner-gun-01..03` against
  `m7-seated`, cropped at full size; the same in `03ceff68`, `d3409f57`, `a3f7c4e1`) show the seated partner with its gun
  on its back in the same pose. A graph-2 node alone does not draw a raised gun on a DM-seated body [O]; why (the upper
  layer's weight under the DM clip, the weapon not in hand, the aim IK controller that StateGunHoldStart also drives
  through `*(*(ctx+0x48)+0x18)` vt+0x38/+0x80/+0x48) is [U].
- The seated direction (sender, key on): `aim_dir=valid; reason=seat` [O]; with the hold showing nothing it has no
  visible use yet.

**Defaults and wire.** Nothing flips: `partner_seat_gun` stays off in lab and play, `partner_seat_aim` stays lab-only.
No op and no CFG index were added. A 0.14.4 peer receiving the seated v6 direction uses it only in its native drive,
which the seat flow keeps disarmed [I]. The front seat and the truck bed depend on M6 and were not tried.

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
- `afgh-relief-lv`: **built** (run `612afff7`, section 8.5): 30010 enemyBase_S LZ, the host's save with relief type 2
  (east jeep) at the LZ, the guest's without (the continue / late-join case). Boot it with `boot fast` (a
  `boot <mission>` passes the ACC, whose LoadCarry clears the carry) and press Space on the guest (section 8.5). The
  real D10 choice is seed `coop-relief-30020` (30020, both roles from the same save);
- `mb-quiet-cell`: a spot beside the Quiet cell, captured in E20.

Seed status (2026-10-29, 8.1): `afgh-horse` and `afgh-quiet` made and checked; `mb-quiet-cell` made (run `b993af09`);
D-Walker is developed in the lab save, and `mb-walker` (run `fa397547`, Mother Base location, walker in the sortie
copy) stands in for `afgh-walker`; the last two are not boot-checked yet. `afgh-relief-lv` belongs to M3.

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
- **Status (2026-10-29): built on `wp/companion-host` behind `companion_host_owned`; play default off.** Results and
  run ids in 8.1. Integrated on `dev/oct7-continue` (merge `23c2cac`, 2026-10-29; unreleased; no hidden run of the
  integrated head).
  - Proven in the hidden worker, logs on both copies: the buddy keys in the follow seeds (34 keys, 40 values, with the
    MissionStart copies); the guest loads the host's buddy in free roam and Mother Base and keeps its own for its saves
    (D8 b, read back in a later session); the reconcile to the host's kind (8 s debounce, 15 s for none); the CALL pad
    mask, the F9 refusal and their toasts; the CHARACTER op before the sortie seeds; the Quiet lock (disable byte,
    `delete_buddy` in the helicopter and at a mission start, `start_rewrite`; the `sortie_rewrite` was logged but undone
    by the OnAllocate slot restore, review 2026-10-29, fixed in 8.3); the Mother Base cell
    Quiet refused on both copies; the id-aware `attClass` (fixture `world_outcome_attclass_lua_check`).
  - FRAME on both copies: the dog in Mother Base (guest clear, host partly behind a railing; in free roam the host's
    frame is clear and the guest's shows only the dog's marker), a horse after the reconcile (on the guest's copy in the
    wrong place, 4 m in front of the guest: removed by the review fixes, 8.3), no buddy Quiet while the guest plays
    Quiet, the empty cell (the wrapper's part is [I], E20); the call menu on the host and none on the guest; the refusal toasts.
  - Play rehearsal (`-Mode play -WanSim bad`): the Quiet lock (`de43726a`) and the dog path (`b83c9f7a`: the seeds in
    play, the CALL mask with the host's call menu and none on the guest's in frames, the F9 refusal toast, the
    reconcile to the host's F9 horse; no expectation failed). Both parts of the project rule (a FRAME and a play
    rehearsal) are met for what these runs show.
  - Left out of this list before the review (2026-10-29): the M1 build released the guest's mirror dog to the guest's own
    AI (`result=released` in `489b3c6f` at t=159355685 and t=159385866, `fa397547` at t=159956883, `b83c9f7a` at
    t=161934838); M2's never-release supersedes it.
  - **Why the play default stays off:** the guest's copy now runs a real buddy AI for the host's companion (the dog is
    held at the host dog's pose by `world_buddy.lua`; a reconciled horse stands where it was called). E3's engagement
    part (with M2), E15 and E19 have not run, so nothing yet shows that this companion never bites, shoots or is hurt
    on the guest's copy; decision D7 is to be taken with the user then. A two-PC session is also still needed.
  - Not done: the iDroid buddy request and the host's sortie-prep and iDroid buddy lists (not reached in the worker;
    R5's menu frame missing); the locked-companion save case; `mb-quiet-cell` and `mb-walker` not booted; the guest's
    Quiet body not filmed (the worker's guest copy refuses the Quiet reset bypass with `body_hash`, before M1).
  - **Review fixes (2026-10-29, 8.3):** the Quiet lock parks the host's Quiet instead of keeping a session-long copy of
    its buddy vars, so a host who calls or picks another buddy keeps it through saves, a Continue and a new mission
    (runs `74d5ba7e`, `9165baeb`: no Quiet ever loaded on the host after its sortie, the host's dog through a Continue
    and a travel to 30020; FRAME of the host's dog before and after the Continue); the guest's PC shows only a host
    companion it can hold, the dog (FRAME `9165baeb`, `b651b42c`: the host's D-Horse on the host's screen, none on the
    guest's);
    both PCs must run the key. Fixture only: the stage hook's effect in game, the session end, the deploy wait for
    CHARACTER, the reconcile backoff. No play rehearsal of the fixes, so the play default stays off.

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
- **Status (2026-10-29): built on `wp/companion-host` behind `companion_host_owned` (M1's key); play default off.
  E19 failed in 8.2; the user's D7 (guard, follow, restore) is built on `wp/dog-guard` and E19 passes (8.9; review
  fixes 8.10).** Results and run ids in 8.2, 8.3, 8.9 and 8.10. Integrated on `dev/oct7-continue`
  (merge `23c2cac`; unreleased); D7 integrated there too (merge `4e3d0e5`, 2026-10-29); the combined play smoke of the
  merged head (8.13) ran at the shipped defaults, so with the key off and no dog hold to see.
  - Built: COMPANION_POSE (op 57, datagram, 10 Hz moving / 2 Hz idle) and COMPANION_ACK (op 49, 1 Hz); the guest holds
    its copy of the host's dog with the stop-AI form of `LuaAiStayAndSnarl` placed by its own `warp=1` every 100 ms,
    led by the sample's age, plus the stock `SetWarpPosition` beyond 15 m and for a dog drifting over 1 m off an
    unchanged target; the dog is never released to the guest's AI; the own-dog read no longer needs body 1 (both
    roles); the M1 COMPANION op carries the dog's pose for the late-join reconcile; D8 (b) also keeps the guest's own
    `deadBuddyType` and `buddyCommonFlag`. Doc drift fixed (`world-sync-protocol.md` section 15).
  - Proven in the hidden worker (lab), logs on both copies: walk windows within 3 m but one at 3.11 m, run and
    dash within 5 m in the marked legs (not every window: `994d252f` also logged run windows of 626 and 703 cm and a walk
    window of 768 cm with `leg=-`, cause [U]; review 2026-10-29), the guest 40 m away (2-23 cm), the host hidden in the
    helicopter (42 s of reads with body 1 unrealized; windows up to 302 cm before `allactive_set`), Mother Base walk
    band (0.82 m; the leg's first window 27.8 m before a warp), never released, E3 (the held dog beside a guard in combat for
    84-176 s: zero `mirror_engaged`). FRAME: the dog beside the host on both screens (`ffb2d7ab` warp-walk pair;
    `d937191f` host idle/walk, guest run), at Mother Base on both (`994d252f`), beside a guard on the guest's screen
    (`d937191f`). With the drift warp (`033193d3`): walk, run and dash met their waitlogs again, one walk window at 3.21 m.
    Review 2026-10-29: those frames come from code before the drift warp (`1de1b27`) and the own flags (`84e4f1d`); the
    only run on that code, `033193d3`, has no frame with the dog on either screen; for the shipped hold see 8.3.
  - Not met (before D7; since then see 8.9): **E19** (guards on the guest's copy kill its dog: `deadBuddyType` 0 -> 4, then the guest's block unloads
    and the dog does not come back; `SetDisableDamage` does nothing); the dog's takedown for the commit/apply check
    (the call menu's "Get it" took nobody down in two runs); the play rehearsal (`ba6d6ff8` ran with the dog
    unrealized; not re-run because the default cannot flip while E19 fails); the Mother Base run leg (scenario fixed,
    not re-run); a two-PC session.
    The D7 candidate `companion_lab repair` (write back the dog's death flags) let M1's reconcile reload the dog after
    a guest-side death once (`033193d3`, gone about 19 s); logs only, no frame of the returned dog.
  - **Why the play default stays off (before D7):** E19 failed (above); D7 was the user's call (section 10). Since
    D7 (8.9), see the last bullet of this list.
  - **Review fixes (2026-10-29, 8.3):** the hold moved unchanged to `lua/coop/world_companion_dog.lua`; it runs only
    when both PCs run the key. On the shipped hold code (`9165baeb`): the guest's copy held the dog 26-30 cm from the
    host dog's pose; FRAME on the host's screen (`9165-host-qf-dog-2`), on the guest's only the dog's head above a rock
    rise under its marker (`9165-guest-qf-dog-2-zoom`).
    `b651b42c` (`companion-host-dog-guestview.txt`, the guest 8 m behind and 4 m to the side): the dog beside the host on
    both screens (`b651-host-gv-dog-2`; `b651-guest-gv-dog`, `-gv-dog-3`), held 2-214 cm (the 214 cm sample right after a
    drift warp; every window `within=1`). This answers the review's proof gap for the walk-free idle hold only; the
    legs (walk, run, dash) were not re-run on this code.
  - **D7 built and proven (2026-10-29, 8.9; branch `wp/dog-guard`):** the guest's copy of the host's dog cannot be hurt
    on the guest's PC (a native filter on the dog's damage handler, armed by the guest's companion module), follows
    the host dog's life, sleep and death through a dog-state byte in COMPANION_POSE / COMPANION `state` (no wire
    change), and comes back from the host's live state when it dies anyway (the restore fallback, about 2.6 s). E19
    passes: about 85 s of guard fire through the copy, 50 hits dropped, no death (`3922cc80`, FRAME of the copy standing
    in the line of fire). FRAME on both copies of the host's dog's death mirrored on the guest's ("D-Dog Near Death" and
    the extraction on both screens, `3f69e3b7`); the restore and the host's re-call in logs with frames on the host's
    screen and the guest's (FRAME on both copies with the guest side-on in `606048af`: the restore before, at the death and after; the re-call; the walk and run legs). Play rehearsal of M1 + M2 with the guard (`1713b0a1`, `-Mode play -WanSim bad`,
    no expectation failed): the seeds, one dog following the host on both screens (walk 0.37-2.93 m, run 1.46-2.77 m),
    the guard armed with the host's byte, the host's alert applied on the guest's copy with the dog beside the host on
    both screens; no guard fired at the dog in that window (0 hits dropped), nor in a second, longer play fight
    (`f7033e2e`, about 190 s), so the guard's drop itself is lab-only proof.
  - **Why the play default stays off (2026-10-29):** the key covers M1 and M2 together, and not every path under it has
    a FRAME on both copies and a passing play rehearsal: (1) the guard's drop, the follow (hurt, asleep, down), the
    restore and the re-call are proven in the lab only: play refuses the levers that drive them (`near_guard shield`,
    `dog_life`, `dog_stun`, `kill_mirror`, `recall`), and the guards of both play rehearsals never fired at the dog; (2) M1's
    review fixes have no play rehearsal (the Quiet park with the host's Quiet deployed, the guest shown no horse,
    walker or Quiet, the session end, the reconcile backoff); (3) the iDroid buddy request and dismiss are not refused
    on the guest's copy, and the host's sortie-prep and iDroid buddy lists were not reached (R5); (4) no two-PC
    session. The dog's takedown result reaching the guest
    (commit/apply) is still not reached. Since the review (8.10): (5) the guard's hold under fire and the restore's
    unknown-host-state wait are proven at runtime through the guest's lab lever `link_sim` (`9fa48400`), but a real link
    loss or unpair with the dog held was never run (no worker lever cuts the link; tests only); (6) the host's dog never
    took a real hit with the hooks installed (the host-side pass-through is [I] from the shared code path); (7) each
    death of the guest's copy cost the guest's PC 30 heroism and 2 bond points (stock "D-Dog Near Death", `9fa48400`);
    since 8.16 the guest gets exactly that back, once per death (lab `c7fdfdbf`: the lab kill, the mirrored down and a
    guard-fire death, readbacks equal before and after on the guest, the host's own death left to the stock game; the
    control `2243bbc6`: the guest's own dog in a session that is not host-owned keeps the stock -30); like the rest of
    D7 it has no play rehearsal (play refuses the levers that kill the copy). Merged on `dev/oct7-continue` (2026-10-30, `101d925`):
    with the key off, the merged head's play smoke `32c3893a` (8.19) logged no `dog_hero` line on either copy.
  - **Review fixes (2026-10-29, 8.10; `e38f2f4`, `d413778`, `5ef2470`):** the guard's two slots are installed only on a
    PC running the key (a default play session installs nothing: `19e438fe`, `-Mode play -WanSim bad`, both copies
    `dog_guard=off;reason=key_off;slots=0`); an armed guard is held drop-only through a link loss until the session or
    mission ends; a death while the host's state is unknown waits for it; the restore clears only the dead DOG bit,
    once (four restores and a mirrored down and re-call on that code, FRAME on both copies in `cc2e15d2`); the receipt
    reports the record only when fresh. The unarmed hook's pass-through is proven on the guest's PC (the copy died of
    guard fire with the guard off, `7fe2845e`, `cc2e15d2`). Resume (`c256368`, `19280d3`): the hold kept dropping guard
    hits for about 25 s and a death with the host's state unknown waited 5.1 s and then restored, both through the lab
    lever `link_sim` (`9fa48400`, FRAME of the guarded copy under fire and of its stock retreat on the guest's screen);
    an arm during a restore is marked `restoring=1`.

### M3. The sortie vehicle on both copies

- **Status (2026-10-29): built on `wp/relief` (route A), lab key `world_relief=0|1` (CFG `r[17]`, lab and play),
  off by default** (on from `5a6bac8` until the review of 2026-10-29 reverted it in `4d18561`, section 8.7). In
  `lua/coop/world_relief_sync.lua` on M1's seed infrastructure; results, run ids and frames in sections 8.6 and 8.7.
  The lab FRAME proof is now `companion-relief.txt` run `5c35644d` (phase-scoped waitlogs, VEHICLE_GONE off; every
  expectation met), with `companion-relief-guest-fulton.txt` run `cbc30b91` for VEHICLE_GONE on. The play default stays off:
  play refuses `worldtrigger` and the Fulton levers, so no hidden play rehearsal can drive a mission end or a removal
  yet, and the earlier rehearsals (`40c09484`, `98e4af59`) covered only the arrival and the refusal. The flip needs
  such a rehearsal (or the user's two-PC session) and the user's D11. D11 is decided and built outside M3
  (`world_loadout_block`, section 8.11); since its review the guest's VEHICLES drop is blocked there too, whatever
  `world_relief` says (section 8.12, `f729d7f4`).
  - **Integration:** `wp/relief` contains M1 (the merge `d911f0c` of `wp/companion-host` `e08dc20`). Done in that
    order on `dev/oct7-continue` (2026-10-29): `wp/companion-host` (`23c2cac`), then `wp/relief` (`c5d0de0`), then
    `wp/vehicle-state` (`bf24931`); unreleased, no hidden run of the integrated head. Since M4, VEHICLE_GONE is on by
    default in play at 30010 and 30020, so a play session that names `world_relief=1` takes the VEHICLE_GONE-on
    removal path (lab `cbc30b91`); the VEHICLE_GONE-off path (`host_none`, lab `5c35644d`) is the lab's and any
    session that names `world_vehicle_gone=0` [I: the composition of the two modules is fixture- and lab-proven,
    not run in play].
  - Proven in the hidden worker with frames on both copies: the host's choice drawn by both copies at the same spot
    (types 1, 2, 5 and 9, the default and the FOVA_1 paint), in lab and in play; the host seated in it on the guest's screen
    while driving; the guest's real iDroid vehicle request refused with the toast; one mission-end credit (the
    host's); the host's Fulton removing it on both copies with one credit (VEHICLE_GONE on: `d18e8fa0`; off, through
    the reconcile's `host_none`: `5c35644d`); the guest's Fulton of it credited once, on the host, with VEHICLE_GONE
    off (`5c35644d`) and on (`cbc30b91`); the west truck (type 3); the continue through the seeds; the late join through
    VEHICLE_SPAWN (placed at the host's pose after a drive).
  - Built: (1) seed keys `reliefVehicleCarryType`, `reliefVehicleCarryAttitude[4]`, `...Life[12]`, `...Ammo[3]`,
    `...State`, `userPresetVehiclePaintType[10]` (key[6] `relief`, 31 values, always sent, applied by the guest only
    with the key, in its `TppMission.Load` wrapper: the guest's destination LoadCarry draws the host's choice, so the
    replayed-sortie write of the experiments is not needed); (2) the guest's mission-end seize skipped
    (`step=seize_skipped`); (3) D8 (b) through a `TppSave.SaveVarsToSlot` wrapper and a solo-load restore
    (`own_kept`, `save_own`, `own_restored`; the live vars come back at the next solo load, not at unpair: section
    8.6); (3b) a guest-side loss of index 0 (its Fulton, or a destruction by its player) is REQ_VEHICLE_GONE for 25600
    whatever `world_vehicle_gone` says, the co-op guest's `OnFulton` never credits it, the host Fultons its own (its
    stock credit) or removes it quietly, and refuses while it rides it (`c7c1ed3`); (4) VEHICLE_GONE removes index 0 on the receiver with `Seize{"Instant"}`
    and the one-time `OnFulton` skip (the receiver's own removal is marked gone, so it is never taken for a guest
    loss); (5) the guest's iDroid `MSN_DROP_VEHICLE` inactive in a co-op free-roam mission, also while the link drops
    inside it (`SetMbTopMenuItemActive`), the stock `MbDvcActSelectNonActiveMenu` message giving the refusal and NOTE 13
    a 4; `MSN_DROP_LOADOUT` the same (a 5) only with the lab lever `relief_lab loadout on` (D11 open; since D11 was
    decided, 2026-10-29, the LOADOUT block is `world_vehicle_rules.lua`'s, key `world_loadout_block`, on in play,
    whatever `world_relief` says, and the lever is gone: section 8.11);
    (6) VEHICLE_SPAWN (op 52) and the guest's reconcile: load a missing index 0 (not one this copy lost: VEHICLE_GONE
    owns that), place one that appears 3 m off or stands 20 m off (own budget), remove one the host lacks for 15 s or
    of another type and load it again if the host's comes back; (7) not built: route C. Lab levers: `relief_status`,
    `relief_lab nonactive vehicle|loadout`, `relief_lab loadout on|off`, `relief_lab forget` (guest), and
    `relief_arm ... [paint]`; worker key `3` (the iDroid MISSIONS tab); worker step `logmark`.
  - Not shown: types 4, 6..8 and 10 on both copies; a destruction of index 0 (no worker lever; the guest-side
    request for it is fixture only); a player's own Fulton (the levers run the stock Seize Fulton, which runs the same
    `OnFulton`) [I]; the guest's refusal guard across a real link drop, and a reload after `host_none` when the host's
    vehicle comes back (fixtures); the host's real sortie-prep selection and GMP charge [U]; a mid-mission load of a
    heavy type; route C; a play rehearsal of a drive, a mission end and a removal; a two-PC session (the exit; M3 does
    not count as done before it).
  - Before the build: E2, E17 and E18 passed and **route A** was chosen (section 8.4); round 2 (section 8.5) proved
    the mid-mission load for a continue or late join and built seed `afgh-relief-lv`.
- Scope: F3 route A; guest vehicle requests refused.
- What M3 needs (route A): (1) seed keys `reliefVehicleCarryType` (the whole u32), `reliefVehicleCarryAttitude[0..3]`
  and `userPresetVehiclePaintType[0..9]` (the paint is re-read at the request); applied in the guest's
  `TppMission.Load` wrapper, which runs after its ACC LoadCarry cleared them and before the destination's LoadCarry
  [I; the lever proved the write at the replayed `SelectNextMissionHeliStartRoute`]; the host's values are snapshotted
  after its sortie prep's SetRelief. (2) The guest wraps `TppMission.SeizeReliefVehicleOnAbort/OnClear/OnForceGoToMb`
  to skip and log `event=relief; step=seize_skipped` (E18 lever). (3) D8 (b) restores the guest's own carry and paint
  vars around each save write and at unpair; `Vehicle.SaveCarry` cannot be wrapped (the `Vehicle` table refuses
  writes), so the restore is the guard. (4) Index 0 removal on the receiver for VEHICLE_GONE: `Seize{"Instant"}` with
  that copy's `TppTerminal.OnFulton` for 25600 skipped once, never Despawn; the guest's VEHICLE_GONE observer ignores
  its own applied removal (`Neutralize` of 25600 arrives with `att=local`). (5) Refusals (E4): the guest's iDroid
  `MSN_DROP_VEHICLE` and `MSN_DROP_LOADOUT` (a loadout delivery can carry a vehicle through SetRelief); candidates
  `TppUiCommand.SetMbTopMenuItemActive(menu, false)` [I] or a Lua wrapper; the guest never runs the sortie prep [O].
  (6) Continue and late join (section 8.5): when the guest's copy is in free roam without index 0 while the host's
  has it (a host continue, a guest join or rejoin mid mission), the host sends its `reliefVehicleCarryType` and
  `reliefVehicleCarryAttitude` (a reliable op or the pairing seeds) and the guest's copy writes them and calls
  `Vehicle.LoadCarry()` [O, `relief_load`, run `612afff7`: the same pose to 0.01 m], after snapshotting its own carry
  vars for D8 (b). If the host's vehicle has moved since its carry was written, the guest's copy appears at the carry
  pose and the general rules (RIDE_FAST, VEHICLE_STATE) move it [I]. Never call it over a live index 0 or in the ACC.
  (7) The host's in-mission drop (route C, D6 later) may use the same call [I]; the drop's parachute path is untested.
- Depends on: E2, E17, E18 (done), M1 (seeds), E4.
- Proof `companion-relief.txt`: the hidden worker opens the iDroid in the ACC, and key 3 (a worker key since M3)
  reaches its MISSIONS tab, but the sortie prep itself is not scripted (section 8.5), so the host's choice comes from the
  lab lever `host run relief_arm <type> ...` on the host only (seeds carry it to the guest), or from seed
  `coop-relief-30020` (decision D10; a real choice, type 2, saved in 30020, ACC 40020; the ACC clears the carry, so a
  sortie from it needs the lever again); `boot 30010 828.18 338.20 1630.51 95`, then `host worldtrigger goto=30020
  goto_lz=0` (a real LZ: the drop point places the vehicle) and `host`/`guest run relief_census` -> `census_v0;
  alive=1; vt=1` at the same x/z on both (runs `fafd3fdd`, `9f01d603` give (1226.94, -127.08)); `host`/`guest run
  relief_face`, `both capture m3-relief-arrival`. Host drives it 100 m -> `waitlog guest 'partner_ride_seat=confirmed; vehicle=25600;'`, never
  `reason=absent`; `both capture m3-relief-drive`. The guest's iDroid vehicle request -> `step=refused; path=` *(new)*,
  `guest capture m3-request-refused`. Removal: host Fultons it -> `waitlog guest 'world_lua=event=vehicle_gone;
  step=applied; id=25600;'` (or the E17 method's line), `both capture m3-relief-gone`. Mission end: `waitlog guest
  'world_lua=event=relief; step=seize_skipped;'` *(new)*, resources diffed. If route B is used, repeat with a host seed
  that chose the other faction's jeep (`afgh-relief-west`) and record the expected `absent` on the guest. Continue
  case: `-SaveSeed afgh-relief-lv`, `boot fast`, `guest press space`, then the guest's copy loads the host's choice
  -> `waitlog guest 'world_lua=event=relief; step=load;'` (the M3 form of the lever's line) and `census_v0; alive=1`
  at the host's x/z (-347.97, 763.60) on both, `both capture m3-relief-continue`.
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
  Mother Base and the wreck parity lab only; not two-PC tested.** Details and run ids in section 8.8 and
  `docs/STATUS.md` (M4, M4 review fixes). Integrated on `dev/oct7-continue` (merge `bf24931`, 2026-10-29; unreleased;
  no hidden run of the integrated head). There, index 0 stays refused by VEHICLE_GONE and skipped by VEHICLE_STATE
  unless `world_relief` is named (M3 then owns it).
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
    is refused before any removal form (fixture tests; no relief vehicle in a hidden run). A jeep the guest destroys
    was not requested, because the game posts no `Damage` message for a vehicle hit (`5179b50b`, `35ba7929`); run
    `72366eb5` shows the resulting split in the lab (the guest's copy broke on a drive, the host's stayed intact) [O].
    **Since D12 (2026-10-29, section 8.11) it is requested and judged by the host** (key `world_vehicle_break`, on in
    play): the guest's lever break (`c0203fdd`) and its real C4 in play (`70146170`) were wrecked on the host's copy
    too [O]; a refusal leaves that split [fixture, I]. Since the review (section 8.12) the host waits for a seat D-Dog
    or a soldier fills (`7762536a`), judges against 300 m, and removes its copy with a Despawn instead of the wreck
    while a player stands within 15 m of it (the blast rule; `f729d7f4` in play, host frames of both forms) [O].
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
    from the host's get-off to the guest's apply in `b31a1b65`) [I]; why a
    teleport beside a jeep hides the Fulton prompt [U]; AI-driven, LRRP and reinforcement vehicles are M8; the relief
    vehicle (index 0) is M3 / E17.
  - **Decisions D12-D15 (2026-10-29, branch `wp/vehicle-decisions`, section 8.11; unreleased, not two-PC tested).**
    D12 and D14 are built in `lua/coop/world_vehicle_rules.lua` behind `world_vehicle_break` and
    `world_vehicle_credit`, on by default in play where VEHICLE_GONE runs (30010 / 30020), off in the lab unless
    named. D12: the guest's unattributed break -> REQ_VEHICLE_GONE cause 2 (cause 4 since the review) -> the host's
    plausibility judge -> the host's copy wrecked, or despawned while a player is within 15 m of it (`c0203fdd` lab
    FRAME on both screens; `70146170` play rehearsal with the guest's C4; review fixes and the blast rule in section
    8.12: `7762536a`, `f729d7f4`). D14:
    the receiver's `Seize {"Instant"}` credits the Fultoned vehicle to its own save too; "Fulton rewards against save
    resources" is now [O]: `4wdW` 1 -> 3 on both copies after the mission end for one Fulton by each player
    (`c0203fdd`), GMP charged only to the Fultoning player. D13: the M4 play defaults stay as they are. D15: the
    guest's bail stays as described above (not run; for the two-PC session).
  - **Integrated on `dev/oct7-continue` (2026-10-29, merge `92c4f9f`; unreleased, not two-PC tested).** The combined
    play smoke of the merged head (8.13, `a1d7aaa1`, `-Mode play -WanSim bad`, the shipped defaults) met every
    expectation: the D11 refusals with both toasts, a jeep the guest drove with the host in a back seat and then parked
    where the guest left it on both screens, and the host's hand Fulton credited on both copies (D14). D12 was not in it.
  - **The vehicle fixes of 2026-10-29 (section 8.14, branch `wp/vehicle-fixes2`; unreleased, not two-PC tested).** The
    guest's own gunfire damage now reaches the host's copy (VEHICLE_DAMAGE, key `world_vehicle_damage`, on in play
    where VEHICLE_STATE runs: lab FRAME `ac0f7656`, play `247a4eed`), so it is no longer undone at the host's next
    state; a soldier-crewed vehicle the guest destroys is wrecked on the host's copy at once (`world_vehicle_crew`, on in
    play: lab FRAME `ac0f7656`, play `8995e351`); D11's items are inactive before PAIRED after a host-led follow (lab
    `ac0f7656`, `d3f5ca56`; FRAME of both refusals before PAIRED `251adcb6`). **After the review (section 8.15):** the
    damage report is off in play again (lab key; no live-AI-fire proof) and the host bounds each report (half the life,
    3 per vehicle and 16 per second); the crew rule holds while a player stands within the blast distance by type (15 m
    light vehicles, 50 m every other type; lab FRAME `c0a385a5`, play `c6db67b8`, at the shipped defaults `98d84a21`, on
    in play); the D11 pre-pair mark lapses after 300 s without PAIRED and was rehearsed in play (`c6db67b8`, `98d84a21`:
    both items refused with both toasts drawn before PAIRED, the host on its tips screen).
  - **Merged on `dev/oct7-continue` (2026-10-30, merge `f1e80be`; unreleased, not two-PC tested).** The combined play
    smoke of the merged head (8.19, `32c3893a`, `-Mode play -WanSim bad`, the shipped defaults: crew on, damage report
    off) met every expectation: the D11 refusals with both toasts, a jeep the guest drove with the host in a back seat
    and then parked where the guest left it on both screens, and the host's hand Fulton credited on both copies (D14).
    D12 and the damage report were not in it.

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
- **Status (2026-10-30, `wp/seat-shoot`; section 8.20): not done.** [O] The seated aim never latches the self-check
  (`checks=0` in every run). [O] A stock back-seat passenger raises and fires its gun (`6fc3098b`). [O] Design A is built
  behind the lab key `partner_seat_gun` (off in lab and play): the driver's copy holds the seated partner's graph 2 on
  GunHoldStart for every aim (`30a6a21a`, `03ceff68`, `d3409f57`, `a3f7c4e1`), but its frames show no raised gun.
  [O] No seated shot was seen to land on a sleeping guard 8-16 m away in five runs, and in the last one (`d7cf5a4b`) the
  on-foot control missed too, so whether a seated shot can land is still [U]. Not met: the kill lines (REQ_OUTCOME
  and the host's `actor=guest` commit), the reversed order's kill, `m7-partner-gun` (no raised gun on the driver's
  screen), the play rehearsal (the hidden-run budget was spent; nothing flips). The shooter's own `m7-seat-aim`
  frames show the reticle over its head (red on the guard in `a3f7c4e1`) but not the gun itself. The front seat and
  the truck bed wait for M6. Next: D16.

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
E3/E15/E19 have run (E3 and E19 have run for the dog, 8.2: E3 passes, E19 fails; the user answered D7 for the dog on 2026-10-29, below, and E19 passes with it, 8.9); D8 (b) the guest's own buddy vars are restored; D9 (b) M1 starts now, alongside the user's
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
  **For the dog (M2, 8.2):** the hold keeps it inert (E3 passes) but guards on the guest's copy can kill it (E19
  fails); the guest then sees no dog (for the rest of the mission [I]: `dc761c98` ended 7 s after the first reload try)
  while the host's lives, and the guest's save is
  protected (D8 b). The options for the dog: (a) hide it on the guest's screen until enemy sync in play; (b) show it
  and accept that a guest-side death removes it until the next mission, or (b2) with the repair of 8.2 (it worked
  once in the lab: the dog drops, is gone for about 19 s and is loaded again beside the host); (c) keep M2 in the
  lab until enemy sync in play; (d) new native work: a damage guard that drops hits on id 9728 on the guest's copy (the
  downed-player damage hooks show the shape) [U].
  **Answered by the user on 2026-10-29:** the guest's copy cannot be killed by anything on the guest's PC, its health and
  state follow the host's dog, and where the guard cannot cover a case the copy is brought back from the host's live
  state (the restore fallback). Built as (d) plus the follow and the fallback on `wp/dog-guard` (8.9); the key stays off
  in play until every path under it has its FRAME and play rehearsal (section 9 M2). After the review of 2026-10-29
  (8.10) the guard is installed only under the key, holds through a link loss, and the restore no longer rewrites
  `buddyCommonFlag`; no new decision is needed for D7 itself. Open for the user: whether lab-only proof of the guard's
  drop, the follow and the restore is acceptable for the play default (play cannot drive those paths), section 9 M2;
  and (found in the resume run `9fa48400`, 8.10) whether the 30 heroism the stock game takes from the guest's PC at
  each death of the copy (the restore cases) should be given back. **Answered by the user on 2026-10-29: give it
  back.** Built in 8.16 (`wp/dog-heroism`): under the key the guest gets back exactly the heroism, bond and demon
  points the stock dying penalty took, once per death, for every death of its copy (the restore cases and, as built,
  the mirrored down too: the host pays for its own dog on its own PC); the host and a guest whose dog is its own keep
  the stock game.
- **D8. The guest's co-op save and the host's buddy:** (a) accept it (the guest's co-op save takes the host's buddy,
  look and relief choices); (b) the guest's own buddy vars are restored around each save write and at the end of the
  session (recommended); (c) restore them only at the end of the session.
- **D9. Order of work:** (a) run the two-PC check of 0.14.3 first, then M1 (recommended: the 2026-10-23 fixes are not yet
  tested on two PCs); (b) start M1 in parallel with that check.
- **D10. Seeds that need the sortie-prep menu** (a relief vehicle chosen, Quiet deployed): if the hidden worker cannot
  drive the menu, may the work use a co-op save you share (launcher Tools > Share save) as a seed? It would be copied
  into the seed library and never written back.
- **D11 (new, M3). The guest's LOADOUT supply drop.** A loadout delivery can carry a vehicle (SetRelief
  `0x1416BC4B0`, section 8.4), so M3 could make the guest's iDroid LOADOUT drop inactive in co-op free roam, like
  VEHICLES, even when the guest's loadout holds no vehicle. (a) Block it (built behind the lab lever
  `relief_lab loadout on`; off by default since the review of 2026-10-29, which found it shipped on before you
  decided). (b) Allow a loadout without a vehicle (needs the loadout's vehicle slot read before the drop: research).
  Until you decide, the guest's LOADOUT drop stays active: a loadout that brings a vehicle charges the guest and asks
  the guest's own engine for a relief vehicle (index 0) [I]; what it does while the host's is live there is [U]; a
  copy of another type is removed by the reconcile after 10 s without a credit and the host's loaded again [fixture].
  **Decided 2026-10-29: (a), block it in co-op with a short toast; only the host picks vehicles.** Built in
  `lua/coop/world_vehicle_rules.lua` behind the key `world_loadout_block`, not tied to `world_relief` (section 8.11:
  with `world_relief` off nothing gives the host's copy a vehicle the guest's loadout brings). The lab lever
  `relief_lab loadout on|off` is gone. **Follow-up decided 2026-10-29 (the user took the reviewer's pick): the guest's
  VEHICLES drop is blocked too in co-op, whatever `world_relief` says** (it was blocked only with `world_relief` on, its
  default off, so a guest could still order a vehicle that only its own copy had). Same key, same guard, NOTE 13 a 4;
  `world_relief_sync.lua` leaves that item to the rules module (section 8.12). The guest's sortie-prep vehicle with
  `world_relief` off is still the guest's own (M3, README "Sortie vehicles"): not part of this decision. **Fix of
  2026-10-29 (the 0.14.4 review finding `game-d11-prepair-gap`, approved by the user):** after a host-led follow the
  guest's copy could reach gameplay before PAIRED with both items still active (the last mission's final stop carried
  over); the follow arrival now marks the mission co-op at the guest's start (section 8.14). After the review (8.15)
  that mark lapses 300 s after the arrival without PAIRED (a host whose game failed while loading), and the window was
  rehearsed in play (`c6db67b8`, `98d84a21`: both items refused with their toasts before PAIRED, the host on its tips
  screen).

- **D12 (new, M4). A vehicle the guest destroys.** The game posts no `Damage` message for a vehicle hit (`35ba7929`),
  so the guest's request never starts: a jeep the guest destroys stays a wreck on the guest's PC only and intact on
  the host's (`72366eb5`). (a) A native hook on the vehicle damage path (static research first); (b) a heuristic: the
  local player fired near the vehicle within N s; (c) forward every unattributed guest-side break to the host. Until
  you decide, it stays as in 0.13.6. **Decided 2026-10-29: (c) with a plausibility check on the host** ("the guest
  was close enough", so both copies agree): built behind the key `world_vehicle_break`; the rule (60 m, a heavily
  damaged host copy, never under a rider of the host's copy, 4 per 20 s) and what a refused guest copy shows are in
  section 8.11. **Follow-up decided 2026-10-29 (the user took the reviewer's pick): only the host's own player in the
  vehicle refuses a break at once; D-Dog or a soldier in a seat holds it until the seat is free** (VEHICLE_GONE's hold,
  REJECT 9 after 120 s). The review fixes (section 8.12) also widened "close enough" to 300 m (launcher and rifle
  kills), judge the guest's position when the request arrived, hold instead of refuse for the rate and an unknown
  position, and send the unattributed break as cause 4 so a host without the key refuses it (REJECT 11). **Safety rule
  added after the review (not a new decision; section 8.12):** the host's wreck explodes about 8 s later with the
  engine's blast (3500 of 7800 life on a player beside it, `7762536a`), so while the host's player or the partner
  stands within 15 m of the host's copy the host removes it with a Despawn instead (the jeep just disappears on the
  host's PC; the guest keeps its wreck). **Open question for you:** a soldier-crewed vehicle the guest blows up: the
  crew's deaths on the guest's copy are unattributed, so the host's crew may stay seated and the hold then ends in a
  refusal after 120 s (the jeep stays only on the host's PC). Keep that, or wreck a crewed host copy at once (the
  engine's blast then kills or ejects the crew there too)? See section 8.12 for what the hidden run showed.
  **Decided 2026-10-29 (the user approved the fix): wreck a crewed host copy at once.** Built behind
  `world_vehicle_crew` (section 8.14): a hold whose only reason is an NPC crew is judged at once and applied as the
  host's wreck (held only while a player stands within 15 m of the host's copy); the engine's blast killed the driver on
  the host's copy too, and its death counted once on each copy (`ac0f7656`, `8995e351`). On in play. **Review (8.15):**
  the armoured vehicle's own blast knocked the guest flat 25 m away (`ac0f7656`), so the hold (and `break_safe`) now
  uses 50 m for every type but the light vehicles (15 m); an unnamed copy with a player inside it is held
  (`blast_near`) instead of a Despawn that cannot run. Proven with the hold and the wreck once clear on both screens
  (lab `c0a385a5`, play `c6db67b8`, the shipped defaults `98d84a21`); on in play again. An enemy vehicle
  that drives off in a fight moves separately on each PC, so the host's copy may be too far from where the guest's
  broke: VEHICLE_GONE then refuses it and the split stays (`247a4eed`; M8).
- **D13 (new, M4). The M4 play defaults.** VEHICLE_GONE (Fulton, and a destruction shown as a disappearance) and
  VEHICLE_STATE (pose and body life) are on by default in play in Afghanistan and Africa (`482eb74`, narrowed by
  `c423b53`); the project rule is met for those paths (section 9 M4). The wreck parity and Mother Base stay lab only.
  Confirm, or turn them off in play until a two-PC session. **Decided 2026-10-29: keep VEHICLE_GONE and VEHICLE_STATE
  on in play in Afghanistan and Africa; Mother Base and the wreck parity stay lab only.** Nothing changed.
- **D14 (new, M4). Fulton rewards for world vehicles.** Today only the Fultoning player's save is paid and gets the
  vehicle; the other copy only removes it. Keep, or credit both co-op saves (like the soldier Fulton decision D1 of
  world sync)? **Decided 2026-10-29: credit both co-op saves, once each, like Fulton recruits; the sortie vehicle keeps
  M3's rule (one credit, the host's).** Built behind the key `world_vehicle_credit` (section 8.11).
- **D15 (new, M4). The guest bailing out of a rolling jeep** [I, not run]: the guest's copy would be moved back to
  where the host's copy stopped once the guest stands 3.5 m clear. Accept, or hold VEHICLE_STATE longer after a
  guest's get-off? **Decided 2026-10-29: accepted for now; check it in the two-PC session.** Nothing changed (the
  guest's bail was not run in the hidden worker).
- **D16 (new, M7, 2026-10-30). Shooting as a passenger: how to continue** (8.20). A back-seat passenger can already
  raise and fire its gun on its own screen, but in eight hidden runs no seated shot was seen to land, its aim covers only
  the half of the world on the far side of the jeep (the seated body faces inward), the camera can end up behind or
  inside the jeep's body, and the partner's raised gun does not show on the driver's screen with the cheap design (a
  graph-2 pose only). Options: (a) a static study first (no runs): what the gun plugin needs on a seated body (the aim
  IK controller, the weapon in hand, flag 0xC3 on body 1) and what makes the passenger's aim pitch jump at each shot,
  then design B (body 1's own gun plugin while seated) with a new run budget; (b) park M7 and move on (M8, M9-M11),
  keeping the lab key and the census; (c) a smaller goal: the passenger may aim and fire on its own screen (as today),
  with no partner gun on the driver's screen and no promise that it hits. **Recommended: (a).**

**The user's decisions of 2026-10-29, as integrated (`dev/oct7-continue`: `4e3d0e5` merges `wp/dog-guard`, `92c4f9f`
merges `wp/vehicle-decisions`; unreleased, nothing pushed, not two-PC tested).** D7 and D11-D15 are all decided by the
user on 2026-10-29; D11 and D12 also took the reviewer's picks of their reviews. What is built, its play default, the
hidden runs that prove it (FRAME = frames on both copies) and what is still open; results in 8.9-8.12, the combined
play smoke of the merged head in 8.13. Claims the reviews refuted are corrected in those sections and are not repeated
here.

| Decision | Built | Play default | Proven | Unproven or for the two-PC session |
|---|---|---|---|---|
| D7 (D-Dog on the guest's PC) | Native guard `dog_guard` (two exact vtable slots: the dog's `ProcessDamages` and its life update) plus `lua/coop/world_companion_guard.lua`: the guest's copy drops every hit while armed, follows the host dog's state byte (hurt, asleep, down), is brought back from the host's live state after a death the guard cannot see (about 2.6 s), holds drop-only through a link loss, waits when the host's state is unknown, and (8.16, `wp/dog-heroism`) gives the guest back the heroism, bond and demon points each copy death costs it. No new op (the byte rides COMPANION / COMPANION_POSE). Installed only under `companion_host_owned`, on both roles | **Off** (the guard follows `companion_host_owned`, off in lab and play; a default session installs nothing: `19e438fe`, the combined smoke `a1d7aaa1` [O], and the merged head's `32c3893a` [O], no `dog_hero` line either) | Lab [O]: E19 passes (`3922cc80`: 50 hits dropped in about 85 s; `7fe2845e`, `cc2e15d2`), the restore (`3922cc80`, `3f69e3b7`, `606048af`, FRAME `606048af` and `cc2e15d2`), the follow (down: "D-Dog Near Death" and the extraction on both screens, `3f69e3b7`, `606048af`; asleep: the STN marker on both, `3f69e3b7`) and the re-call (FRAME `606048af`, `cc2e15d2`), the hold under a simulated link loss (`9fa48400`). Play rehearsals with the key named: `1713b0a1` (passed), `f7033e2e` (no guard fired at the dog) | The guard's drops, the follow, the restore and the re-call in play (play refuses their levers; no guard ever fired at the dog in play); a real link loss [I]; the host-side pass-through [I]; falls, drowning, out of bounds; the heroism give-back (8.16, `wp/dog-heroism`: lab `c7fdfdbf`, control `2243bbc6`) has no play rehearsal and its by-player branch is fixture-only; M1's own gaps keep the key off; two-PC |
| D11 (the guest's LOADOUT drop) | `world_vehicle_rules.lua`, key `world_loadout_block` (CFG `r[15]` bit 32): the guest's iDroid LOADOUT item is inactive in co-op and refused with a toast (NOTE 13 a 5); by the reviewer's pick the VEHICLES item too (NOTE 13 a 4), whatever `world_relief` says; the host's items are untouched. Since the fix of 2026-10-29 (8.14) also before PAIRED after a host-led follow | **On** | FRAME `c0203fdd` (lab, LOADOUT; the toast is logged, the lab HUD is off), `70146170` and `47b95ae7` (play, LOADOUT toast drawn), `f729d7f4` and `33a7fa6e` (play `-WanSim bad`, shipped defaults: both items refused with both toasts drawn, the host's VEHICLES list opens), again in the combined smoke `a1d7aaa1`. Before PAIRED: lab `ac0f7656`, `d3f5ca56` (the guard 29 s and 104 s before PAIRED, the host held on its tips screen, frames); FRAME `251adcb6` (both items refused with both toasts before PAIRED, 96 s window); in play `c6db67b8` and `98d84a21` (`-WanSim bad`, `titlecontinue` + `waitmission guest ... only`: both items refused with both toasts drawn before PAIRED, the host on its tips screen; the pre-pair mark lapses after 300 s without PAIRED, 8.15); the paired refusals again in the merged head's smoke `32c3893a` (8.19, frames) | The guest's sortie-prep vehicle with `world_relief` off is still the guest's own (M3, not part of D11); the lapse live [fixture]; two-PC |
| D12 (a vehicle the guest destroys) | Key `world_vehicle_break` (bit 8): the guest's unattributed break goes to the host as REQ_VEHICLE_GONE cause 4; the host judges it (the guest within 300 m of the wreck at arrival or now, or the host's copy already badly damaged), refuses it (REJECT 8) when the guest was too far, or at once while the host's own player rides it, holds while D-Dog or a soldier is seated or the position or rate is not clear (REJECT 9 after 120 s), and a host without the key answers REJECT 11. The host wrecks its copy, or removes it with a Despawn while either player is within 15 m (the blast rule, added after the review) | **On** where VEHICLE_GONE runs (30010, 30020) | FRAME `c0203fdd` (lab lever; the host's screen shows the fire and smoke behind the host's body, not the burning jeep), `09333f05` (the ridden refusal), `7762536a` (a seated D-Dog held, then the wreck; a far refusal; the blast measured at 3500 of 7800 life), `f729d7f4` and `33a7fa6e` (play `-WanSim bad`, shipped defaults: the guest's real C4 with the host 8 m away -> Despawn, host life unchanged; with nobody within 15 m -> the wreck, seen intact, burning and burnt on the host's screen). Earlier play `70146170`, `47b95ae7` (the host's frames show the smoke column, not the jeep). 120 m judged near (`0f3f00f1`, logs only). **Crew (2026-10-29, `world_vehicle_crew`, on in play):** a soldier-crewed armoured vehicle wrecked at once, FRAME lab `ac0f7656`, play `8995e351` (the guest's real C4), again at the shipped defaults `34d5744d`. **Blast distance by type (review, 8.15):** 15 m light vehicles, 50 m every other type for the hold and `break_safe`; `crew_near` held and then wrecked once clear, FRAME lab `c0a385a5`, play `c6db67b8`, again at the shipped defaults `98d84a21` | An enemy vehicle that drives off in a fight moves separately on each PC and is then refused as far (`247a4eed`, M8); the armoured blast knocked a player down at 25 m and not at 33 m (`ac0f7656`, `c0a385a5`), its damage there unmeasured [U]; `blast_near` live [fixture]; a real launcher or rifle kill at 60-300 m; the 15 m margin is not a measured radius [I]; D-Dog or a buddy near the host's copy at the wreck [U]; multi-hit kills by the guest's explosives spread over more than about 10 s [I] (its gunfire now accumulates on both copies, 8.14); two-PC |
| D13 (the M4 play defaults) | Nothing changed: VEHICLE_GONE and VEHICLE_STATE stay on in play in Afghanistan and Africa; Mother Base and the wreck parity (`world_vehicle_wreck`) stay lab only | **On** (30010, 30020) | The M4 proofs (section 9 M4: `4bbbfde4`, `b31a1b65`, `e44516d7`); in the combined smokes `a1d7aaa1` and `32c3893a` (8.19, the merged fixes) a jeep the guest drove with the host in a back seat stayed parked where the guest left it, FRAME | Mother Base in play; two-PC |
| The guest's vehicle damage (fix of 2026-10-29, the 0.14.4 review finding `game-vstate-heals-guest-damage`) | Key `world_vehicle_damage` (bit 128), `world_vehicle_damage.lua`, op VEHICLE_DAMAGE 0x3A: drops of the guest's copy within 3 s of its own gunfire and 300 m are reported; the host applies the lower life (at most the guest's drop, at most half its copy's life per report, 3 reports per vehicle and 16 per second, never 0); VEHICLE_STATE carries it back and no longer heals a report in flight; D12's heavy rule ignores reported drops | **Off** (lab key; off in play again since the review, 8.15) | Logs of both copies: lab `ac0f7656`, play `247a4eed` (the guest's real round kept on both copies past the refresh), `34d5744d`; the host bound in the lab `c0a385a5`. The frames show only the jeep (no visible damage at these lives) | AI fire on the two copies adds up whenever the report comes first (`846c1147`): no run with live guards since the fire gate, no shot attribution [U]; the guest's explosives short of a destruction and its collisions are not reported [I]; two-PC |
| D14 (Fulton rewards for world vehicles) | Key `world_vehicle_credit` (bit 16): the receiver removes its copy with `Seize {"Instant"}`, so its own engine credits the vehicle too (`OnFultonVehicle`); once per copy, never index 0; the sortie vehicle keeps M3's rule (one credit, the host's) | **On** where VEHICLE_GONE runs | FRAME `c0203fdd` (lab, both directions; `4wdW` 1 -> 3 on both copies after the mission end; the GMP fee only on the player who Fultoned), play `-WanSim bad` `70146170` and `47b95ae7` (both directions, frames on both screens); the host's Fulton again in the combined smokes `a1d7aaa1` and `32c3893a` (8.19) | APCs, tanks and trucks; a far or unrealized receiver; the commit in play rests on the lab mission end [I]; two-PC |
| D15 (the guest bailing out of a rolling jeep) | Nothing changed: the guest's copy snaps back to where the host's stopped once the guest stands 3.5 m clear [I] | (VEHICLE_STATE, on) | Not run | Check in the two-PC session |

Open after the integration (2026-10-29; behaviour to confirm, not new decisions): with `companion_host_owned` on, the
guest's PC shows no host D-Horse, D-Walker or Quiet until M10, M9 and M5 add their holds (M1 had drawn a horse, in the
wrong place); while the host's Quiet is parked, a host who actively picks "none" at sortie prep is saved as Quiet (the
code cannot tell that choice from the lock's NONE).

Things the user should know (not decisions): D-Dog "completely" has two stages (6.2): stage 1 (M2) gives one dog that
follows the host on both screens and synced results; the dog's attack at the right guard and its marks on the guest's
screen wait for enemy sync in play and shared marking. The same holds for Quiet's shots. The two-rider horse depends on
E11 (no known horse placement API yet). If E4 shows that the sortie-prep menu ignores the disable mask, Quiet
stays in the host's menu while the guest plays her; picking her then deploys no buddy and shows a toast. With M1's
key on, the guest's call button is masked while the host has a companion out (that is what refuses the guest's buddy
orders and whistle), so the guest has no Knock and no Sonar then either; in a CQC hold the button works (interrogation).

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

### Review of D7 (2026-10-29, branch `wp/dog-guard` at `8a294ce`)

Verdict: fix first. All five findings were verified against the code and fixed with tests that failed first; the
details, commits and the four hidden runs that re-proved the changed behaviour (`7fe2845e`, `19e438fe` play,
`cc2e15d2`, and in the resume `9fa48400`) are in 8.10.

| Review point | Verdict | Check and change |
|---|---|---|
| Major: the guard's slots were installed in play whatever `companion_host_owned` said; the unarmed pass-through had no runtime proof; STATUS said "Defaults: unchanged" | Accepted | `game_script_observer.cpp` had no key gate [O]. `dog_guard::InstallRefusal` (`e38f2f4`); proven off in a default play session (`19e438fe`); the pass-through proven on the guest's PC (`7fe2845e`, `cc2e15d2`, `9fa48400`); on the host's PC it is the same role-free branch (a host never arms), [I], since the host's dog took no real hit in any run; STATUS and 8.9 corrected (`3d0046c`) |
| A link or scope loss dropped the guard at once; an unknown host state was filed as "down" | Accepted | Lua line 153 and the native scope check [O]. `dog_guard::Admit` hold, the Lua hold (arm 2) and the unknown state (`e38f2f4`, `d413778`); at runtime through the lab lever `link_sim` (`c256368`, run `9fa48400`: about 25 s of guard hits dropped under the hold, an unknown-state death waited 5.1 s and then restored); a real link loss not run |
| The vars repair rewrote both words every 100 ms for 10 s | Accepted | The second `flag=0_32` in every restore run [O]. DOG bit only, once (`d413778`); one vars line per death in four restores and a mirrored death |
| Stale record fields, a never-reset follow cap, unused `dropLogs` | Accepted | `Fresh`, per-mission caps reset by `Disarm`, `dropLogs` as the drop log counter (`e38f2f4`); `dog_state` reads the live record when disarmed (`5ef2470`); the symptom came back inside the 1 s gate right after a send-away (`9fa48400`), so an arm during a restore is marked `restoring=1` (`19280d3`) |
| The fixture lacked the risky branches | Accepted | 65 -> 99 checks (`d413778`, `5ef2470`); 115 with the resume's lever and marker (`c256368`, `19280d3`) |
| Refuted: legs "all within", E19 host frame "the host's dog unhurt", "vars repaired after 84 ms", "Defaults: unchanged" | Accepted | Corrected in 8.9 and STATUS (`3d0046c`) |

### Integration of M1-M4 (2026-10-29, `dev/oct7-continue`)

Merges (merge commits only): `8843d20` (`wp/companions`, this design), `23c2cac` (`wp/companion-host`, M1 + M2 + the
review fixes), `c5d0de0` (`wp/relief`, M3 + its review fixes), `bf24931` (`wp/vehicle-state`, M4 + its review fixes).
No world-op number was shared, so none changed: COMPANION 0x30, COMPANION_ACK 0x31, VEHICLE_STATE 0x33, VEHICLE_SPAWN
0x34, CHARACTER 0x38, COMPANION_POSE 0x39 (59 ops); 0x32 and 0x35-0x37 stay reserved (section 5). CFG: r[15] the
vehicle modules bitmask, r[16] the companion module, r[17] the relief module. World chunk order: `world_vehicle`,
`world_vehicle_state`, `world_companion`, `world_companion_dog`, `world_relief`, `world_relief_sync`. One worker
`logmark` implementation (`wp/vehicle-state`'s; `wp/relief`'s fixture checks kept). Section 8 renumbered (see the note
there). `debug_vehicle.lua` was condensed to stay within the 16000-byte literal check in its CRLF form (15838 bytes).
Full CTest 311/311 and the five contract `--check` scripts pass on `bf24931`. No hidden run of the integrated head
(the task's rule asked for one only if an op number changed); the modules' composition in game is [I].

### Integration of the decision tracks (2026-10-29, `dev/oct7-continue`)

Merges (merge commits only, local): `4e3d0e5` (`wp/dog-guard` at `2ba81c0`: D7 and its review fixes; no conflict) and
`92c4f9f` (`wp/vehicle-decisions` at `3735c85`: D11, D12, D14 and their review fixes). Both reviews' verdicts were "fix
first" and both fix passes finished unblocked, so both tracks were merged. Conflicts: the world chunk lists in
`CMakeLists.txt`, `cmake/lua_checks.cmake`, `native/bootstrap_patch.cpp` and `tests/host_trial_patch_check.cpp`
(resolved to the order of `native/world_sync_source.hpp.in`: `world_vehicle_state`, `world_vehicle_rules`,
`world_companion`, `world_companion_dog`, `world_companion_guard`, `world_relief`, `world_relief_sync`); `docs/STATUS.md`
(all four sections kept); this document (both tracks had added 8.9 and 8.10: the dog track keeps them, the vehicle
track's became 8.11 and 8.12, and every reference that branch added was renumbered, in this document, STATUS,
protocol section 19, the `world_vehicle_rules.lua` header, its fixture and the `vehicle-decisions-*` scenarios). Op
codes and CFG indices stay unique: D7 adds neither (its state byte rides COMPANION 0x30 and COMPANION_POSE 0x39, and it
reads `r[16]`); the vehicle track adds REQ_VEHICLE_GONE cause 4 and REJECT 11 (protocol section 19, version note there)
and the `r[15]` bits 8 / 16 / 32. Full CTest 314/314 and the five contract `--check` scripts pass on `92c4f9f`. The
reviewers' refutations stand as corrected on the track branches (8.9, 8.11, STATUS); none of them is repeated in the
decision table of section 10. Combined play smoke of the merged head: 8.13.

### Review of M3 (2026-10-29, branch `wp/relief` at `c282538`)

An independent review of M3 found the code sound on route A but the default flip unproven. Each point was checked against
the code and the archived slices before changing anything (section 8.7 has the runs that followed).

| Review point | Verdict | Check and change |
|---|---|---|
| Major: with `world_vehicle_gone` off (its default), a guest Fulton of index 0 is credited on the guest's Mother Base and again by the host's mission-end seize, and the copies diverge | Accepted | `world_relief_sync.lua` skipped the guest's `OnFulton` only for its own quiet removal, and VEHICLE_GONE is off by default [O code; I effect]. Fixed in `c7c1ed3`: a co-op guest's `OnFulton` never credits 25600; its own Fulton or destruction is REQ_VEHICLE_GONE for 25600 whatever `world_vehicle_gone` says; the host Fultons its own (its stock credit, once) or removes it quietly for a destruction; refused while the host rides it. Proven in run `5c35644d` (VEHICLE_GONE off) and `cbc30b91` (on) |
| Major: the M3 scenarios' waitlogs could not fail (matched earlier phases' lines) | Accepted | `Invoke-WorkerScenario.ps1` read each log from the session start [O]; the slices show the matches named by the review [O]. New worker step `logmark` (`c95998a`); `companion-relief.txt` rewritten with a mark per phase and lines that name their mission |
| Major: the play rehearsals behind the default cover only the arrival and the refusal; `40c09484` named `world_vehicle_gone=1` | Accepted | Play refuses `worldtrigger` and the Fulton levers [O, `world_goto.lua:31`, `world_relief.lua:293`, `world_vehicle.lua:483`], so no hidden play rehearsal can drive a mission end or a removal yet. The default flip of `5a6bac8` is reverted (`4d18561`): `world_relief` is off by default again in lab and play |
| Major: D11 (the guest's LOADOUT block) shipped on before the user decided | Accepted | The LOADOUT block is the lab lever `relief_lab loadout on|off`, off by default (`c7c1ed3`); D11 stays open |
| Minor: a host_none removal never reloads; one budget for placements, loads and removals | Accepted | `RS.reload` is set for every reconcile removal; placements have their own budget (6), loads and removals 3, reset when the host's state changes (`c7c1ed3`, fixtures) |
| Minor: dead `cause == 2` branch in the VEHICLE_GONE receiver wrapper | Accepted | `V.consider` calls `V.remove(q.id)` [O]. The branch and its comment are gone; the receiver removes index 0 quietly for any cause until M4 sets a wreck rule |
| Minor: D8 (b) restores at the next solo load, not at unpair or session end | Accepted as a documented deviation | M1 does the same (`world_companion.lua` `C.release("solo")` only) [O]. Restoring the live vars mid mission would leave them unlike the vehicle drawn on that copy; saves write the guest's own values while the host's are live (the guarantee D8 b asks for), and the live vars return at the next solo `TppMission.Load`. A paint preset the guest changes during a session is lost [I; the guest never runs the sortie prep in co-op, where presets are chosen] |
| Minor: the refusal guard re-enables the items during unpaired gaps; forces them active; does nothing while the iDroid is open | Partly accepted | Unpaired gaps: fixed, the item stays inactive in the co-op mission until the final stop or a solo load. "Forces active": `TppTerminal._SetUpDvcMenu` and `SetUpDvcMenuAll` call `InitAllMbTopMenuItemActive(true)` and every story list names both drop items `active=true` (the extracted `TppTerminal`, `package-10.lua:675-686`) [O], so active is the state its `SetUp` leaves; a mission script calling `SetActiveTerminalMenu` was not looked for [U]. iDroid open: a mission's first pass now applies even while it is open |
| Minor: comments and docs drifted (default, 2 m lift, Fulton parity at defaults, VEHICLE_SPAWN rate) | Accepted | Corrected in the module, `world_wire.cpp`, section 5 (rate as built), section 8.6, STATUS and the roadmap |
| Minor: merging `wp/relief` also brings M1 (`d911f0c`) | Accepted | Integration note in section 9 M3 and STATUS: integrate `wp/companion-host` first, or accept M1 with M3 |

### Review of the vehicle decisions D11, D12, D14 (2026-10-29, branch `wp/vehicle-decisions` at `7d8c95e`)

The independent review returned `fix_first`. The user took the reviewer's picks for the two open questions. Each
finding was checked against the code and the archived slices before the fix (section 8.12 has the runs).

| Finding | Verdict | Check and change |
|---|---|---|
| Major: D12 refused at once for any filled seat (`occupied`: D-Dog, a soldier crew), which leaves a permanent split | Accepted (the user's pick) | `world_vehicle_rules.lua` mapped `local_rider` and `occupied` to `ridden` [O]. Since `8b00f80` only `local_rider` refuses; `occupied` is VEHICLE_GONE's hold (250 ms re-check, REJECT 9 after 120 s); fixtures for D-Dog and a soldier. Live with D-Dog in the jeep (`7762536a`): `hold; reason=occupied`, then 568 ms later `break_judge; ok=1; why=near` and the host's copy wrecked, frames on both screens [O]. An enemy-crewed vehicle (`0f3f00f1`): its driver died unattributed on the guest's copy, so on the host it stayed seated and the request was held (`occupied`) for the 95 s left of the run [O]; the cap ends such a hold with REJECT 9 at 120 s [fixture], leaving that vehicle only on the host's PC. Asked of the user (section 10, D12) |
| Major: the host-side SetBodyLife 0 blast was never measured with godmode off or live guards, yet on in play | Accepted; measured, then a rule | `7762536a`: the wreck's damaging explosion comes about 8 s after SetBodyLife 0 (the second `VehicleBroken`) and hit the host beside the jeep for 3500 of 7800 life (`downed_damage; attacker=65535; damage=3500`, godmode on) and the guest beside its own copy the same; the live guard 5 m from the jeep died on both copies, each from its own copy's explosion (`att=none` on each). With godmode off the host was downed, but by that guard's rifle (`attacker=1120`, 342 per hit), not the blast. Fix `0e79337`: while the host's own player or the partner (DOWNED_STATUS) stands within 15 m of the host's copy, or a position cannot be read, the host applies the destruction as a Despawn (`break_safe`), else the wreck; fixtures first. In play over `-WanSim bad` with the host's godmode off (`f729d7f4`, the passing rehearsal `33a7fa6e`): 8 m away the host got the Despawn and kept 7801 of 7801 life, 27 m away the wreck, life unchanged; frames on both screens [O] |
| Major: 60 m refuses launcher, rifle and support kills; the heavy rule cannot fire for guest damage | Accepted | 300 m (`8b00f80`); the 40 m pose match and nearest-of-type still pick the vehicle. Fixture: 150 m applied. Live: near at 1.6 and 11.4 m (`7762536a`), 19.8 and 21.5 m (`f729d7f4`, `33a7fa6e`) and 120 m (`0f3f00f1`: applied as the wreck; logs on both copies, the frames show only the slope between); far refused at 1197 m (`7762536a`, REJECT 8, `break_refused`, the host's copy intact) [O]. A real launcher or rifle kill at range was not run (the judge reads only positions) [I]. A multi-hit kill spread over more than about 10 s may never break on the guest's copy (VEHICLE_STATE restores the host's life between hits) [I], documented in the README |
| Major: the guest's VEHICLES drop stays open with `world_relief` off | Accepted (the user's pick) | `world_loadout_block` greys `MSN_DROP_VEHICLE` too in every co-op mission (NOTE 13 a 4); `world_relief_sync.lua` leaves the item to it (`8b00f80`, fixtures). Live in play over `-WanSim bad`: both items marked inactive on the guest's iDroid, both toasts drawn, the host's VEHICLES list opens (`f729d7f4`, `33a7fa6e`) [O] |
| Minor: the guest's position is read at judge time, not at the break | Accepted | The position at the request's arrival is kept; the closer of it and the current one is judged; no known position holds instead of refusing (fixtures) |
| Minor: rate limits drop requests | Accepted | Host: `rate` holds until the window frees; guest: a queue of 32, logged when full (fixtures) |
| Minor: key parity between the PCs is not enforced | Accepted, another fix | No HELLO change: the guest sends an unattributed break as REQ_VEHICLE_GONE cause 4 (wire range 1..4, protocol section 19 version note), which a host without `world_vehicle_break` refuses with REJECT 11, so nothing is applied unjudged (fixtures, `world_wire_tests`); cause 4 on the wire in `7762536a`, `f729d7f4`, `33a7fa6e` and `0f3f00f1` |
| Minor: nits (G.remove before its type check, two judge lines for a rate refusal, stale comments, status cut) | Accepted | All four fixed in `8b00f80` (type check first; one judge line at the outcome; the Invoke-WorkerScenario help, LabCommon and `world_sync_source.hpp.in` comments; the counters kept when the gone list is long), fixture checks for the first, second and fourth |
| Refuted: 8.11 "the burning front of the jeep" on the host's screen (`c0203fdd`) | Accepted | Corrected in 8.11 (`157d73d`): smoke and fire behind the host's body, the jeep's visible rear unchanged |
| Refuted: 8.11 and STATUS "hidden behind the guest's body" (`70146170`, `47b95ae7`) | Accepted | Corrected in 8.11 (`157d73d`) and in the STATUS section of the decisions (`f60d6ce`): the host's own body; the jeep is never in view on the host's screen in those frames. New host frames of the wreck itself: `7762536a` (`occ-after`, `blast-later`, at the door) and `f729d7f4` / `33a7fa6e` (`d12b`: the jeep 27 m away, then burning, then the burnt wreck) |

### Review of the vehicle fixes (2026-10-29/30, branch `wp/vehicle-fixes2` at `116847e`)

The independent review returned `ship_behind_lab_key`. Each finding was checked against the code, the archived slices
and the frames before the fix (section 8.15 has the runs).

| Finding | Verdict | Check and change |
|---|---|---|
| Major: `world_vehicle_damage` on in play without its fire gate ever meeting live AI fire; the gate does not attribute the shot, and `max()` dedups only when the host's hit comes first | Accepted | `846c1147` host slice: host-own drops interleaved with applied reports, added up until the jeep broke [O]; every later damage run cleared the guards first (`vehicle-fixes2-lab.txt`, `-play.txt`), `8995e351` logged no drop on the guest [O]. Off in play again (`795bdfc`); host bounds per report and per second (`8f20483`); the docs no longer say AI fire counts once. Shot attribution and a live-guard rehearsal stay open |
| Major: the crew rule's 15 m came from a jeep; the armoured vehicle's blast knocked the guest flat about 24 m away | Accepted | Frames `ac0f7656-guest-crew-blast-01/02` looked at: the guest at 25 m lies flat with damage arcs [O]. Blast distance by type, 50 m for every type but the light vehicles, for `crew_near` and `break_safe`; an unnamed copy is held (`blast_near`) instead of a Despawn that cannot run (`77be734`). Lab FRAME `c0a385a5` (no knockdown at 33 m; the hold, then the wreck with the host 60 m away), play `c6db67b8`; crew on in play again (`3929a6c`), again at the shipped defaults `98d84a21` |
| Major: the D11 pre-PAIRED fix ran in play by default without a play rehearsal of that window; the play runs show only the paired refusal | Accepted | Slices: in `34d5744d`, `8995e351`, `247a4eed` the guest's first guard line has the same millisecond as `event=paired` [O]. New worker step `titlecontinue` (`5293dc2`) holds the host on its tips screen in `-Mode play`; `c6db67b8` (LOADOUT) and `98d84a21` (both items, both toasts drawn) refused before PAIRED; the mark lapses 300 s after the arrival without PAIRED (`017e777`) |
| Minor: the host takes the guest's drop at face value; reported drops feed D12's heavy rule | Accepted | `D.apply` had no host-side limit [O]. Half the host copy's life per report, 3 per vehicle and 16 over all per second; heavy counts own damage only (`8f20483`, fixtures; the half bound live in `c0a385a5`) |
| Minor: protocol section 20 says the guards' fire counts once | Accepted | Rewritten: counted once only when the host copy's own hit came first; AI fire adds up otherwise |
| Minor: README states the crew hold as fact and the guest's gunfire as shared without limits | Accepted | The hold now has live proof (`c0a385a5`, `c6db67b8`) and the README names its distances; the damage report is off in play, so the README no longer says the guest's gunfire is shared |
| Minor: comments still say the keys are off in play; 8.14 says `Forced=True` for every run | Accepted | Comments in `world_vehicle_damage.lua`, `lab_config.cpp`/`.hpp`, `lab_config_tests.cpp`, `LabCommon.psm1` and the scenario help say which default applies (`3929a6c`); 8.14's header names the four `Forced=False` runs (`3d2fc8b`) |
| Refuted: "D11 fix in play" (`247a4eed`, `8995e351`, `34d5744d`) | Accepted | Corrected in 8.14 and STATUS; the pre-pair window was first rehearsed in play in `c6db67b8` |
| Refuted: protocol section 20 "a hit both copies took (the guards' fire) counts once" | Accepted | Corrected (above) |
| Refuted: the damage play default "with a FRAME proof on both copies" | Accepted | The frames show the jeep, not the shared life (the engine draws no damage at these lives); the claims rest on logs, and the key is off in play |
| Refuted: 8.14 "`Forced=True` at shutdown as in the earlier runs" | Accepted | Corrected in 8.14 |
### The 0.14.4 pre-release review: launcher and package findings (2026-10-29, branch `wp/launcher-fixes2`)

All five were confirmed, non-blocking and outside this design's behaviour (release tooling and the launcher); fixed with
failing-first tests. Details and runs: docs/STATUS.md, section "the 0.14.4 review's launcher and package findings fixed".

| Finding | Verdict | Change |
|---|---|---|
| Two automatic launcher setups can run at once after a window closes during setup | Accepted, both ends | `8c7a0da`: the closing window ends its setup before any join and holds the folder until then; a per-folder setup lock makes the next window wait. Hidden end-to-end runs `r4`, `r5` [O] |
| The Release receipt omits new shipped tests | Accepted, widened | `bc56c0d`: 13 fixtures added (298 -> 311), four of them missing since before 0.14.4; a registered test that is neither run nor excluded by name is refused. Receipt `20261030-014929-6188027e7b644a37bf51164ae9fc7020` [O] |
| "The tag holds the same source" is inexact | Accepted | `d6da17f`: ~~the tag holds the same code, apart from the package record in docs/STATUS.md~~ (refuted in round 2, below; corrected by `2dcd0e3`); `build.source_commit` names the commit |
| The notices say the exe downloads nothing | Accepted | `d6da17f`: its only network access is the update check and, on Update now, the download from this repository's releases |
| Untracked files could be published in the source zip | Accepted | `c78ed3b`: package.ps1 refuses them by name before writing anything; `source_dirty` counts them |

### The 0.14.4 pre-release review, round 2: the launcher and package fixes (2026-10-29, branch `wp/launcher-fixes2`)

Verdict of the review: fix first (one major, seven minor). Each finding was checked against the code before it was
changed. Details, failing-first tests and runs: docs/STATUS.md, section "the review of those launcher and package fixes,
round 2".

| Finding | Verdict | Change |
|---|---|---|
| Major: the tag/zip wording is still false (16 tracked paths missing from the zip, MinHook only a link, later docs commits) | Accepted (confirmed with `git ls-tree`) | `2dcd0e3`: the texts compare the zip with `build.source_commit` and list what it leaves out; `friend_package_tests` checks the wording and the left-out kinds |
| `friend_package_tests` depends on the developer's untracked files | Accepted | `f93dc74`: the real-tree run gives its child gits an excludes file of exactly those files; `package.ps1` keeps its refusal |
| The shared test list adds a slow process test to every hidden-run preflight | Accepted, other fix | `8db0313`: `launcher_icon_tests` and `ddt_local_watch_process` run only in the Release receipt (309 / 311); a Normal check would need a new D: build folder, so none ran [U] |
| No test of an abandoned setup lock; e2e window C was vacuous | Accepted | `9b20ce7`: two sub-tests; a mutation check (WAIT_ABANDONED dropped) fails 5 checks. STATUS says window C never tried a setup |
| STATUS overclaims "does not depend on which program runs the setup" | Accepted | Docs: "a setup run by Diamond Dogs Together from this version on"; older exes and the dev install tools take no lock |
| A start during the ~50 ms closing gap exits without a window | Accepted | `bc9f305`: the claim waits up to 2 s when no window holds the folder, then looks once more; real-mutex unit tests [O], no end-to-end run [U] |
| The untracked-file refusal fails open without git | Accepted | `6240a0a`: with a `.git` entry, a git that cannot be run refuses the package |
| The launcher e2e ran outside `tools/worker` and without the verify lock | Accepted as a record | STATUS records it as a deviation, not sanctioned; no such harness ran in round 2; the user decides whether it may |
