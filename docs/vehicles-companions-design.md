# Vehicles and companions: design

Status: design only, nothing built. Branch `wp/companions` from `dev/oct7-continue` `48ce7da` (0.14.3; package built at
`a4c18fa`). Target exe: `mgsvtpp.exe` 1.0.15.4, SHA-256 `085c2f82...bb45`.

Inputs: five read-only research passes of 2026-10-28 (roadmap audit, prior experiments, vehicles, buddies, tandem
riding). Their native reads used pefile + capstone on the exe file, because the shared Ghidra server refused
connections that day; their extracted game files and notes are in the session scratch folder
`.../scratchpad/companions/{vehicles,buddies,tandem}/` (not in the repo). REA ledger unknowns they recorded:
`unk_59943e57` (relief selection var), `unk_bfa846ef` (unnamed TppVehicle2 commands), `unk_633bc6fb` (call menu and
Quiet commands), `unk_945d974f` (mirror buddy side effects), `unk_149ed100` (horse rider capacity).

Every claim carries a tag:
- **[O]** observed: code, data, a log or a run read by someone, with path:line, RVA, run id or file hash;
- **[I]** inferred from observed facts;
- **[U]** unknown, needs an experiment (section 8).

RVAs are absolute (image base `0x140000000`) unless written as a short offset like `0xA44740`, which is image
relative. Run ids are hidden-worker runs recorded in `docs/STATUS.md`.

## 1. Requirements

The user's request of 2026-10-28, restated as rules that a hidden run or a two-PC session can check. "Host" is the
player who hosts (always Snake); "guest" is the joiner; "copy" is one PC's game.

| # | Rule | Pass signal |
|---|---|---|
| R1 | **Sortie vehicle on both screens, host's choice only.** A vehicle the host brings from sortie prep exists on both copies, same type and paint, at the same place. The guest's own sortie-prep vehicle never spawns. Only the host can bring or request a vehicle. | Within 10 s of arrival, `GetPosition` of the host's relief vehicle answers on both copies within 1 m and 5 deg. When the partner drives it, the viewer logs `partner_ride_seat=confirmed`, never `reason=absent`. A guest request is refused with a toast. |
| R2 | **One of each companion, chosen by the host.** At most one D-Dog, one D-Horse and one Quiet in the session, and it is the host's buddy. The guest's copy shows that same companion with the same look, and no buddy of its own. The guest cannot deploy, call, dismiss or swap a buddy. | `buddy_status` on both copies names the same kind; the guest's copy never shows a second buddy; a guest call is refused. |
| R3 | **D-Walker, one per player** when the host deploys D-Walker (the user's "maybe"; decision D2 covers the other cases). | Two walkers on each screen; each player drives their own; each sees the other's walker move with its pilot. |
| R4 | **D-Dog synced, follows the host only.** One dog, visible on both screens, near the host's dog pose, following the host on both copies. What the dog does to enemies happens once. | Guest-copy dog within 3 m of the host dog (5 m while running) after 10 s; no dog follows the guest; no duplicate outcome from the guest's copy. |
| R5 | **Quiet only when the guest is not playing her.** If the guest plays Quiet, buddy Quiet is unavailable to the host. Otherwise Quiet behaves as in the normal game on the host's copy and is mirrored on the guest's screen (position, cloak, her shots' effects). | Guest character `quiet` (or `story` resolving to Quiet): the host's copy has no Quiet and logs the lock. Otherwise Quiet visible on both copies within 5 m. |
| R6 | **D-Horse for two.** One horse, visible on both screens. Either player can mount it; the first one steers. A second player who mounts sits behind and can aim and shoot while the other steers. Both screens show both riders on one horse. | Frames on both copies show one horse with two riders; the passenger's shots register (outcome sync); the passenger never steers. |
| R7 | **All vehicles synced.** Every TppVehicle2 in the scene (authored jeeps and trucks, Mother Base jeeps, relief and reinforcement vehicles) has one state: whether it exists, where it is when no player drives it, whether it is destroyed or Fultoned. A vehicle a player drives follows that player's copy. | After any loss, push or drive, the other copy matches within 1 m / 5 deg after 2 s; a vehicle removed on one copy is removed on both. |
| R8 | **Passengers wherever it makes sense:** light vehicle front and back seats, the truck cab and the truck bed, inside APCs and tanks, the shared D-Horse. Passengers can shoot where the game's poses allow it (light vehicle back seat, horse). | Each seat boards from either role, shows on both screens, and exits with the stock get-off. |
| R9 | **Host-only change.** Anything that creates, removes or swaps a companion, a sortie vehicle or a support vehicle is the host's action. A guest attempt is refused visibly, never left to diverge. | Every guest attempt logs a refusal and shows a toast. |

## 2. What exists today

| Area | State | Evidence |
|---|---|---|
| Partner drives a world vehicle | Body 1 is drawn by a DirectMotion GetOn/Loop/GetOff chain anchored to the viewer's copy of the vehicle (`CNP_ppos_c`, seat matrix `0xB7AF90`). That copy is placed every frame by RIDE_FAST (op 44, 20 Hz, 100 ms behind the sender tick) plus RIDE_TILT (op 45). Named seat, or by id for unnamed vehicles. | [O] `docs/anim-round1-seat.md:23-87`, `docs/vehicle-ws3.md:88-171`, `lua/coop/world_ride_pose.lua:1-26`; runs `abb0b7cf` (by id, MB jeep 25603), `0a2314f2` (tilt) |
| Jeep back seat | `partner_ride_board=passenger` (default in lab and play) retargets stock ride sites `0x12458AC` (pre-query) and `0x1251479` (phase 1) to the free back seats 2..3 of a light vehicle (type 1/2, 3-4 seats), never the peer's seat. The stock passenger phase `0x1250CF0` (act 0x6E) attaches the viewer to the copy RIDE_FAST moves; a passenger camera handle write fixes the view. | [O] `native/include/mgscoop/partner_board_policy.hpp:10-50`; `docs/native-puppet.md:270-517`; runs `59cfc415`, `495e7c3a` (lab), `de949f80` (play) |
| Trucks, APCs, tanks as passenger | Blocked: passenger mode falls back to block for every type other than 1/2. The truck half of `coop-vehicle-passenger.txt` never ran with both copies' trucks matching. | [O] `partner_board_policy.hpp:18-22`; `docs/STATUS.md:946-957` (run `84e3db0e`: `veh_trc_0000` = 25602, type 4, seats=2, `board_block reason=type`) |
| Sortie (relief) vehicle | Exists on the chooser's copy only. 0.14.3 keeps the driver visible on the other copy (`reason=absent`, standing at seat height with no vehicle). | [O] `docs/two-pc-test-2026-10-23.md:94-101`; `native/partner_driver_seat.inc` (`SeatFallbackHere`); run `31adc898` |
| Vehicle removal | VEHICLE_GONE / REQ_VEHICLE_GONE (ops 46/47) make Fulton and destruction host-authoritative and never remove a ridden vehicle. Off by default (`world_vehicle_gone=0`, the launcher never writes it). Parts A and D proven (`075c1ea4`); part C (hold while seated) and a manual Fulton never ran. | [O] `lua/coop/world_vehicle.lua:1-32`; `native/include/mgscoop/lab_config.hpp:700-704`; `docs/STATUS.md:592-611` |
| Parked and AI-driven vehicles | Not synced. Each copy places and drives its own trucks (one was AI-driven 490 m away in `a42ddafe`); a wreck existed on one copy only (`7561ab63`). | [O] `docs/world-sync-protocol.md:1174-1176`; `docs/STATUS.md:920-921, 954-956` |
| D-Dog | The host sends its dog pose every 250 ms (BUDDY_STATE op 39). The guest borrows its **own** D-Dog and holds it there (`LuaAiStayAndSnarl` every 250 ms, `SetWarpPosition` beyond 15 m). Needs world level 3. Only proof: lab run `7169753d` (guest dog 1.48 m from the host dog). | [O] `lua/coop/world_buddy.lua:235-297`; `docs/STATUS.md:2208` |
| D-Dog gaps | The guest's copy shows no dog when the guest deployed horse, walker or Quiet (`guest_buddy_busy`, `:260-263`) or no buddy (the guard needs `IsBlockActive()`, `:61-67`, which is false with no buddy deployed); the host never sees a dog only the guest deployed (the guest releases it to its own AI, `:254-257`); the host's dog read refuses while body 1 is unrealized (`field_ready` needs `pp::Realized(player,1)` and `parts[1]==3`). Mother Base sessions of 2026-10-23 logged no buddy line at all. | [O] code lines cited; `native/game_script_observer.cpp:4016-4018`; `docs/STATUS.md:1996`; 2026-10-23 host log (refused `scope` for ~50 s in 30020) and friend log (no buddy line in 30050). Which guard failed in MB is [U] (a failed guard logs nothing). |
| Sortie choices | World follow (op 9-11 seeds plus the kind-6 replay) carries mission, heli route, layout, cluster and deploy time. No buddy and no vehicle key. The guest never deploys itself: its `OnEndMissionPreparation` sends a request the host rejects (`host_only`). | [O] `lua/coop/world_follow_seed.lua:4-15`; `lua/coop/world_follow.lua:303-307, 412-413` |
| Partner on D-Horse / D-Walker | Hidden and unrealized on the viewer's screen (acts 121/122, 123-126). RIDE_BODY (op 42) never moves the viewer's own horse or walker (user decision 2026-10-22); a pose applies only to an id in `R.mirrors`, and nothing registers one. | [O] `docs/mounts.md:7-25, 52-85`; `lua/coop/world_ride_pose.lua:33-40`; runs `bfd6fcc4`, `2ab88411` (lab), `5a044a4d` (play) |
| Two riders on one horse | Never worked. (1) `world_horse_cargo.lua` (op 43, lab `cargo_test 1`) warps the guest's own player onto an offset of the host's horse each tick (0.55 m back, 1.55 m up) with a pad mask; no recorded run result. (2) Native Player cargo on D-Horse stopped at the stock horse-load finder (`cda340da`, `2d94f364`). (3) The capacity-two archive gives two horses per copy, but each player mounted only its own (`ed3c1cd4`); the peer riders in `f6a2b54c` were a display bug. | [O] `lua/coop/world_horse_cargo.lua:1-12`; `docs/mounts.md:132-137`; `docs/STATUS.md:2224-2253`; tag `archive/research-2026-10-23` evidence |
| Capacity two | Lab-only archive `mgs-coop-buddy-capacity-variants-v2.dat` (17,501,184 bytes, SHA-256 `4b290cdd...5ec9`): seven horse FPKDs and `buddy_wg2_00` re-authored to count 2 with a second locator 4 m away; `MGS_LOCAL_BUDDY_CAPACITY_TEST`, profile `buddy_capacity_two_v1`. Two realized horses and two walkers per copy; the guest's ACTION mounted no walker (`f145f79f`, `6e789152`). Memory budget never measured. | [O] `docs/STATUS.md:2216-2256`; `lua/coop/world_buddy_ride.lua:183-197` |
| Quiet | The guest's Quiet character falls back to a DD soldier when the guest's **own** `vars.buddyType` is Quiet, or in 10050/11050/10260. Nothing stops the host deploying Quiet while the guest plays her. No Quiet sync exists; the guest treats host kind 4 as `no_host_dog`. | [O] `lua/coop/character.lua:83-86, 123-124`; `docs/characters.md` table; `world_buddy.lua:254-256` |
| F9 summon | Release F9 "Summon D-Horse" (stock `ReserveCallBuddy` + `Load`) creates a horse on one copy only; the 2026-10-23 host horse was invisible to the guest. | [O] `lua/coop/debug_buddy.lua:43-84`; `docs/two-pc-test-2026-10-23.md:16, 109` |
| Shooting from a seat | Not possible today. The seated-aim fix only stops the aim latch (`637b47ea`). A seat-aim census is uncommitted in `D:/mgs-coop-wt/seat-aim`, never run. | [O] `docs/STATUS.md:335, 785, 846-850`; `README.md:70` |

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
- **Load** [O]: `TppMain.OnAllocate` runs `TppMission.SetSortieBuddy()` (masks only), the mission's
  `DISABLE_BUDDY_TYPE`, `sequence.OnBuddyBlockLoad()`, then `TppBuddy2BlockController.Load()` **only in Afghanistan
  and Africa** (`TppMain.lua:102-122`). The Load core `0x140A485F0` copies vars `buddyType`, `initialBuddyStatus`,
  `initialBuddyPos`, `initialBuddyRotY` into the controller and checks no mask.
- **Mother Base** [O]: f30050 disables HORSE, WALKER_GEAR, BATTLE_GEAR; `TppMbFreeDemo` adds
  `/Assets/tpp/pack/mission2/free/f30050/f30050_Buddy.fpk` only when `vars.buddyType` is DOG or QUIET at pack-list
  time, then `ReserveCallBuddy(vars.buddyType, BuddyInitStatus.RIDE, 0, 0)` + `Load()` (`f30050_sequence.lua:139-143,
  295-308, 972-973`; `TppMbFreeDemo.lua:4, 276-277`).
- **Mid-mission spawn** [O]: `ReserveCallBuddy(type, initStatus, pos, rotY)` then `Load()` works with no buddy
  deployed: the F9 horse summon does exactly that (`debug_buddy.lua:46-70`) and the 2026-10-23 host horse came from it.
  `CallBuddy(type, pos, rotY)` is called with NONE and DOG while the block is active (`world_buddy.lua:156, 172, 270`);
  that it also swaps a loaded horse, walker or Quiet for another type is [I].
- **Refusals** [O]: CallBuddy's core `0x140A44740` refuses when the block pointer (+0x68) is null, a pending byte is
  set, or bit `1<<type` is set in the TppBuddyService disable byte `0x142C2919E` (written by `SetDisableBuddyType` /
  `SetDisableAllBuddy`; Quiet in 10260 is the only exception). `DeleteBuddy` (`0xA45980` -> `0xA45850`) requests NONE
  through the same gates. `SetDisableCallBuddyType` writes the saved var +0x2D6B instead. **The same byte that blocks a
  player's call also blocks our own `CallBuddy`** [O, readers `0xA447D3`, `0xA458AC`].
- **Orders** [U]: the player's buddy orders (dog attack/stay, Quiet scout/cover/fire, horse, walker modes) go through
  the native call menu (`CallMenuImpl::UpdateSelectKeyboard` `0x179CE60`), not Lua. Whether the iDroid buddy deploy
  and the sortie-prep UI honour the disable byte is unknown (REA `unk_633bc6fb`).

### 3.2 Per-companion commands

| Companion | GameObject type, id on each copy | Lua commands | Placement |
|---|---|---|---|
| D-Dog `TppBuddyDog2` | 19, `9728` | Dispatcher `0x140C7D220` [O]: `LuaAiStayAndSnarl` (`0x24824B9A`), `GetPlant`, `SetWarpPosition` (`pos`), `GetPlacedItem`; scripts also send `SetMotherBaseCenterAndRadius` | Held by `LuaAiStayAndSnarl` target + `SetWarpPosition` [O, run `7169753d`] |
| D-Horse `TppHorse2` | 13, `6656` (`6657` in capacity two) | Dispatcher `0x140E4D250` [O]: `GetRidePlayer`, `SetCallHorse{startPosition, goalPosition}`, `HorseForceStop`, `SetCombatGrade`, `SetMaxLife`, four unnamed hashes `0x9BBA9494`, `0x442D1EEC`, `0xC41E0C02`, `0xD1233CDA` | **No verified per-frame placement** [O]. `SetCallHorse` at RIDE_BODY rates made idle horses leap and hang in the air (`docs/mounts.md:52-62`, filmed) |
| D-Walker `TppWalkerGear2` | 44, `22528` (`22529`) | Dispatcher `0x141706190` [O], 16 commands incl. `SetPosition{pos, rotY}`, `NoRideOn`, `IsPlayerRiding`, `ForceStop` | `SetPosition` [O]; it does not arm a body for ACTION (`6e789152`) |
| Quiet `TppBuddyQuiet2` | 18 [I, type array `0x2A7C690`] | Dispatcher not found [U]. Controller requests exist: `RequestMoveToPosition`, `RequestWalkToPosition`, `RequestMoveToSnipePoint`, `RequestEspionageToPosition` [O, table `0x140A45BE0`]; argument shapes [U] | [U] |

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
- **Rider geometry** [O] (tandem, `tools/demorel.py` over demo end transforms): a single rider sits at (0, 1.40, 0.40) m
  from the horse root (p31_010010/020); in the prologue pillion Snake sits at about (-0.01..-0.08, 1.11, -0.05..-0.18)
  and Ocelot at (-0.02, 1.06-1.08, 0.33-0.35) in `ocelot_horse`'s frame (`s10010_d11.fpkd` demos p21_020030_000,
  p21_020040_001). The rear rider is 0.40-0.51 m behind the driver, about 1.1 m above the root. "+z is forward" is [I].
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
- Reusable [O/I]: the seat offsets above; the same horse model and rig; `player2_cypr.mtar` (184 clips, 7 referenced by
  the resident graph, clip names unknown) as a source of rear-seat clips if ever needed; the player graph has **no**
  named pillion state (bounded search, tandem `gapsolve2.py`).
- Vanilla D-Horse riders can draw and fire weapons [I, gameplay knowledge; weapon-class limits U].

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
- **The var that holds the chosen relief type is not identified** [U, `unk_59943e57`]. Candidates: the
  `sortieLoadoutInfoU8buf/U16buf/U32buf` buffers, `userPresetVehiclePaintType`, `reliefVehicleCarry*`.
- **Index 0 cannot be spawned, respawned or despawned from Lua** [O]: `ParseLuaSpawnIndex` `0xB304E0` rejects 0 and
  indices at or above the pool count (callers in Spawn `0xB31E70`, Respawn `0xB31710`, Despawn `0xB30AF0`).
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
- Enemy vehicles have no route of their own; soldiers drive them (`SetLrrp{travelPlan}`, `SetRelativeVehicle`,
  `TppEnemy.lua:1500-1508`) [O].
- Reinforcements [O]: script block `reinforce_block`, types EAST/WEST WAV, WAV_ROCKET, WAV_CANNON, TANK, HELI; CP messages
  `RequestLoadReinforce`, `RequestAppearReinforce`, `CancelReinforce`; the vehicle appears by
  `Respawn{name='reinforce_vehicle_0000', type=9, ...}` with driver `reinforce_soldier_driver`
  (`TppReinforceBlock.lua:46-200`). The only random input is `TppRevenge.SelectReinforceType` [O], so a host event can
  drive the same steps on the guest [I].
- Instance commands [O] (`0xB2B710..0xB2C1A7`): `GetPosition`, `SetPosition{position, rotY}`, `GetVehicleType`,
  `GetRiderId`, `Seize` (Fulton, ...), `ForceStop{enabled}`, `IsAlive`, `SetIgnoreDisableNpc`; dictionary matches
  [I]: `SetVehicleType`, `SetBodyLife`, `SetBodyDamageRate`, `SetWheelDamageRate`, `GetState`. Class commands: `Spawn`,
  `Respawn`, `Despawn`, `IsAlive`, `StoreToSVars`, `RestoreFromSVars`, `RegisterConvoy`.
- Messages [O]: `VehicleBroken`, `VehicleAction` (GOT_IN, GOT_OUT, FELL_OFF), `PlacedIntoVehicle`, `VehicleDisappeared`.
- Checkpoints store all vehicle state in svars `ene_veh_*` (`TppEnemy.lua:1185-1193, 1321-1351`) [O].
- The exe has a "Vehicle Synchronizer" memory tag (presumably FOB/MGO) [O string `0x22889C8`]; project rules exclude
  the game's online sync.

### 3.7 Seats and passenger poses

| Vehicle | Seats with player states [O, MOG hashes] | Notes |
|---|---|---|
| Light vehicle | 0 driver, 1 front right, 2 back left, 3 back right: GetOn / Loop / GetOff each | Stock phase 1 turns any seat below 2 into seat 0 (`0x141251832..0x141251840`) [O], so a stock player never takes seat 1 |
| Truck | 0 driver, 1 front right: GetOn/GetOff for both, `stateTruckFrontLeftLoop` only; bed hide `stateTruckHide`, `stateTruckHideLoop`, `stateTruckHideToWait` | The mod borrows `stateVehicleFrontRightLoop` for truck seat 1 (`partner_sync.lua:207-212`); seat 1 of an empty copy is remapped to the driver by `0xB79A3F` [O]. Var `vehicleGameObjectIdOnPlayerOnCargo` tracks a player on a vehicle's cargo [O]. Truck CNPs `CNP_ppos_a,b`, `CNP_CARG`, `CNP_DECK_A` [O] |
| APC (wav/sav) | none | CNP `ppos_a,b,c` [O]; driver only, driver hidden |
| Tank (nbt/mbt) | none | CNP `ppos_a,b`; 12.7 mm HMG on `CNP_awp_a` is the driver's second weapon (`VEHICLE_TOGGLE_WEAPON`) [O/I] |
| Support heli | `stateHeliIdle`, `stateHeliRideOn` | AI-flown; partner hidden in the heli (acts 97..108) [O] |

No retail player state combines a vehicle seat with aiming [O, bounded search]. Static reading says a local back-seat
passenger can still raise and fire its gun: Ride Exec stage 0 sets no-weapon flag 0xC3 at `0x12473A2`, the passenger
phase clears it at `0x1250FF8`, and EnableCheck `0x103E010` refuses only on 0xC3/0xC2 [I, never run]. Retail also
seats the player in an NPC-driven jeep's back seat by a scripted direct motion (`s10150_sequence.lua:1765-1800`,
`stateDirectPlayRideRearLeftSeatOnVehicle`) [O].

## 4. Authority model

One principle: **the host owns everything that exists in the world; whoever rides a thing owns its pose while riding;
every copy may refuse what would remove something under its own rider.**

| What | Owner | How the other copy follows | Guest may | Guest may not |
|---|---|---|---|---|
| Sortie buddy type and look | Host (sortie prep) | Follow seeds before the guest's `OnAllocate` (F1) | Pick its own character | Pick, call, dismiss or swap a buddy; any guest sortie-prep buddy choice is overwritten |
| Sortie (relief) vehicle choice | Host | Seeds (route A) or VEHICLE_SPAWN (route B) (F3) | Ride it, drive it | Choose or request one |
| In-mission buddy calls, vehicle drops, F9 summons | Host | COMPANION kind change / VEHICLE_SPAWN | - | Any; refused with a toast |
| Companion pose (dog, Quiet, unridden horse) | Host's copy | COMPANION stream, the guest copy holds its instance there | Walk with it | Move it |
| D-Horse while ridden | The driver's copy | MOUNT_FAST from the driver | Drive it (if first on) or sit behind | Steer while passenger |
| Each D-Walker | Its pilot's copy | MOUNT_FAST | Drive its own | Mount the host's |
| Vehicle a player drives | That player's copy | RIDE_FAST / RIDE_TILT (existing) | Drive any free vehicle | Take a seat the peer holds |
| Vehicle nobody drives (parked, pushed) | Host's copy | VEHICLE_STATE | Push it locally (overwritten by the host) | - |
| AI-driven and reinforcement vehicles | Host's copy | RIDE_FAST from the host + local AI stopped (F8) | - | - |
| Vehicle destruction and Fulton | Host decides | VEHICLE_GONE (guest asks with REQ_VEHICLE_GONE) | Destroy or Fulton on its copy (becomes a request) | Keep a vehicle the host removed |
| Seat occupancy | Host arbitrates | SEAT grant/deny | Claim a free seat | Keep a seat the host denied |
| Companion orders | Host | none needed (they run on the host's copy) | See decision D1 | - |
| Companion health, death, hospital | Host's copy | COMPANION `state` | - | - |
| Combat effects of companions | Host's copy only | ENEMY_OUTCOME etc. (existing) | - | - |

Conflict rules:
1. **Terminal wins.** A removal (VEHICLE_GONE) or a companion death on the host beats any pose; ridden vehicles are
   never removed under a local rider (existing rule, `world_vehicle.lua:119-132`) [O]; the removal waits until the
   rider leaves (part C).
2. **Rider beats owner.** While a player rides a horse, walker or vehicle, that copy's pose stream wins on both copies;
   when the ride ends, the last pose is sent reliably (RIDE_BODY's final pose, existing [O]) and ownership returns to
   the host.
3. **Seat races go to the host.** Both copies check seats locally first (the existing board policy never offers the
   peer's seat) [O]; if both claim the same seat within one round trip, the host's SEAT grant decides and the loser gets
   the stock get-off.
4. **Mirrors never act.** A companion instance on the guest's copy that mirrors the host's companion causes no world
   change: its kills, sleeps, spotting and damage are not attributed to the guest (today `world_enemy.lua:63` counts a
   buddy attacker as the local player's, `world_outcome.lua:33-35`) [O]; what it does anyway is overwritten by host
   outcomes.
5. **Unknown means hidden, not wrong.** If a copy cannot show something (a missing vehicle, an unplaceable mount), it
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
| 9-11 | TRANSITION / SEED / SEEDS_END | H->G | R | per travel | **New seed keys** (vars): `buddyType`, the look vars of 3.1, `initialBuddyStatus`; after E2 the relief selection var(s) and `reliefVehicleCarryType/Attitude/Life/Ammo/State` | `world_follow_seed.lua` key list (`{name, dims, scale, true}` = vars) [O] |
| 48 | COMPANION | H->G (both for walker slot 1) | U, and R once per kind or state change | 10 Hz while moving, 2 Hz idle, plus every change | seq, sender tick, kind 1 dog / 2 horse / 3 walker / 4 Quiet, slot (0 host's, 1 guest's walker), present, xyz mm, yaw mrad, state byte (dog: follow/stay/attack; Quiet: standby/snipe/cloak/down; horse: gait 0..3; walker: idle/drive), rider bits (seat 0 role, seat 1 role) | Supersedes BUDDY_STATE 39 / ACK 40 (same idea, more fields) |
| 49 | COMPANION_ACK | G->H | R | 1 Hz | seq, kind, result, error cm | Same as BUDDY_ACK |
| 50 | MOUNT_FAST | both | U | 20 Hz while ridden | sender tick, kind 1 horse / 2 walker, slot, xyz mm, heading mrad, gait | RIDE_FAST's track machinery (100 ms render delay, extrapolation, stale rules) keyed by (kind, slot) instead of a per-copy id |
| 44 | RIDE_FAST | both | U | 20 Hz | unchanged | **Also sent by the host** for AI-driven vehicles near the guest (F8), 10 Hz |
| 51 | VEHICLE_STATE | H->G | R | on change (>0.5 m, >5 deg, life/state change), at most 2 Hz per vehicle; full sweep on pairing | vehicle id, locator hash (0 if unnamed), type, flags (alive, broken, npc-ridden, relief), xyz mm, yaw mrad | New |
| 52 | VEHICLE_SPAWN | H->G | R | per event | host vehicle id, cause (relief, drop, reinforce), type, subType, paintType, class, xyz, yaw | New; the guest records host id -> local id |
| 53 | REINFORCE | H->G | R | per event | cp hash, step (load, appear, cancel), reinforce type, coloring | New |
| 54 | SEAT | both | R | per event | seq, target (vehicle id, or horse slot with a horse flag), seat, role, action (claim, grant, deny, release) | New; host arbitrates |
| 46/47 | VEHICLE_GONE / REQ | as today | R | per event | unchanged | Turned on by default after E5 |
| 55 | COMPANION_CMD | G->H | R | per press | kind, order id | Only if decision D1 is "forwarded" |

Ids: authored vehicles get the same id on both copies (spawn order is the same list) [I; seen equal, e.g.
`veh_lv_0000` = 25608 on both]. VEHICLE_STATE carries the locator hash so the guest can check that. Companions are
addressed by (kind, slot), never by GameObject id, because in capacity two the host's walker is index 0 on the host's
copy and index 1 on the guest's (section 6.9). Budget: worst case about 60 small datagrams per second (under 2 KB/s)
on top of today's traffic [I]; the M-proofs record it under `-WanSim bad`.

## 6. Per-feature approach

### 6.1 F1: host-owned companion at sortie (M1)

1. **Seeds.** Add the buddy vars of 3.1 to `world_follow_seed.lua`. The guest applies seeds in its `TppMission.Load`
   wrapper (`world_follow.lua:484-496`) [O], before its next `OnAllocate`, so the retail `Load()` and the Mother Base
   pack list read the host's buddy [I; E1 checks MB order]. The guest's co-op save already receives host checkpoints
   (W4), so the guest's own buddy choice is not something we preserve in co-op [I].
2. **Late join, reconnect, mid-mission change.** On COMPANION kind change or a mismatch at pairing, the guest
   reconciles: `CallBuddy(kind, hostPos, yaw)` while its block is active, otherwise `ReserveCallBuddy(kind, NORMAL,
   pos, yaw)` + `Load()` (the F9 pattern, `debug_buddy.lua:46-70`) [O works mid-mission]; host NONE -> `DeleteBuddy`
   (never a release to the guest's AI).
3. **Guest requests refused.** Wrap the guest copy's Lua entry points `TppBuddy2BlockController.CallBuddy`,
   `CallBuddyWithHalo`, `ReserveCallBuddy`, `DeleteBuddy` and the F9 summon: refuse with a toast unless the co-op module
   itself calls (it keeps the originals bound at hook time, as `world_buddy.lua:35-37` does) [I]. Whether the iDroid
   path reaches these Lua functions is E4. Fallback: `SetDisableBuddyType` for every type except the host's, accepted
   only if E4 shows the guest never needs to call another type that mission (the same byte blocks our own calls) [O].
4. **Quiet lock (host).** The host learns the guest's resolved character at pairing (the guest's setting plus the
   host's Quiet state for `story`; a Lua-readable channel for it is [U], add one to HELLO or a world op if missing). If
   the guest plays Quiet: in the host's `TppMission.Load` wrapper **before `F.sendSeeds`**, if `vars.buddyType ==
   QUIET` set it to NONE (s10260 precedent, `s10260_sequence.lua:435-443`) and restore the saved value after the
   buddy block has copied it (`0x140A485F0` reads it once) [I]; in `OnAllocate`
   `TppBuddyService.SetDisableBuddyType(BuddyType.QUIET)` (mission scoped, CallBuddy checks it) [O]; mid-mission
   `DeleteBuddy()` (s10050 precedent). Toast on the host: "Quiet is your partner's character". Never use
   `SetDisableCallBuddyType` (a saved var) [O].
5. **Guest character rule.** `character.lua` falls back to a DD soldier when `vars.buddyType == QUIET` (`:123-124`). With
   the host's buddy seeded, that check would read the host's buddy; after the host lock it can no longer be Quiet while
   the guest plays her, so the rule stays as a safety net [I].

### 6.2 F2: D-Dog synced (M2)

On top of F1 the guest's copy loads its own instance of the host's dog, so `world_buddy.lua`'s existing hold path
applies. Changes:
- Guard: drop the `IsBlockActive()` requirement for the reconcile path (2 above); keep it for the hold.
- Never release to the guest's AI: host has no dog -> `DeleteBuddy` on the guest (today `B.release` calls
  `CallBuddy(DOG, here)` back to normal AI when the original was a dog, `:168-172`) [O].
- Host read scope: the host's own dog read must not need body 1 realized (`field_ready`,
  `game_script_observer.cpp:4016-4018`); send `present=0` only when the dog itself is unreadable [O cause, I fix].
- Rate: COMPANION at 10 Hz while the host dog moves; hold every 100 ms; warp beyond 15 m (as today).
- Attribution: on the guest's copy an outcome whose attacker is the mirror dog is class `other`, not `buddy`
  (`world_outcome.lua:33-35`, `world_enemy.lua:63`) [O]; the host's dog's effects arrive as host outcomes.
- Mother Base: the seeded `buddyType` DOG makes the guest's copy load `f30050_Buddy.fpk` [I, E1].
- Doc drift: `docs/world-sync-protocol.md` section 15 still says `RequestMoveToPosition` with a 0.75 m stop; the code
  sends `LuaAiStayAndSnarl` [O].
- Out of scope here: the dog's enemy marks on the guest's screen (roadmap "Shared marking").

### 6.3 F3: sortie vehicle on both copies (M3)

- **Route A (preferred).** E2 finds the var(s) that select the relief vehicle. Seed them with
  `reliefVehicleCarry*`. The guest's own engine then loads the same relief pack and places its own index 0 (25600) at
  the same drop point, because the heli route is already seeded (`AdjustFromDropPoint(gvars.heli_missionStartRoute,
  ...)`) [I]. Its pose afterwards follows the general rules: RIDE_FAST while driven (the by-id seat handles unnamed
  ids: proven on the MB jeep 25603 in `abb0b7cf`, and it already asks for 25600 in free roam, answering `absent` in
  `31adc898` because the instance was missing) [O], VEHICLE_STATE while parked.
- **Route B (fallback).** Host sends VEHICLE_SPAWN{25600, type, subType, paint, class, pos, yaw}; the guest spawns
  `Spawn{index=n>=1, ...}` of the same class into a free pool slot and maps 25600 -> its id in RIDE_FAST, the seat
  gate and VEHICLE_GONE. Works only for the location faction's light vehicle and truck (only those bodies are loaded)
  [O]; an APC, a tank or the other faction's jeep stays `absent` on the guest (today's fallback).
- **Route C.** The in-mission air drop (`CMD_REQUEST_RELIEF`, entry unknown) on the guest's copy: research only.
- Guest choice: overwritten by the seed (A) or never spawned (B); the guest never sorties itself anyway [O,
  `world_follow.lua:412-413`]. In-mission vehicle requests by the guest are refused (E4 finds the path).
- Carry across missions: `Vehicle.SaveCarry` / `LoadCarry` state is in `reliefVehicleCarry*` vars, seeded too [I].

### 6.4 F4: vehicle losses and parked vehicles (M4)

- Turn on VEHICLE_GONE in play after E5 (part C hold while seated; a manual player Fulton). Destroyed vehicles: try
  `SetBodyLife{0}`-style wreck parity (dictionary name [I]) inside E5; otherwise keep Despawn.
- VEHICLE_STATE: the host scans the pool once per second (ids 25600..25600+instanceCount-1, `IsAlive`, `GetPosition`,
  `GetRiderId`), skips anything a player rides, and sends changes. The guest `SetPosition{position, rotY}`s non-ridden
  vehicles that neither it nor the partner rides [O commands]. Full sweep on pairing and after a checkpoint load.
- Host-only vehicle requests: the guest's iDroid vehicle request is refused (E4).

### 6.5 F5: Quiet (M5)

- F1 gives the guest's copy its own instance of Quiet (seeded `buddyType` and look). Hold it at the host's Quiet pose
  with `RequestMoveToPosition` / `RequestWalkToPosition` or a warp, once E15 names her commands [U].
- Her shots: only the host's Quiet acts; the mirror must not shoot (E15: an order or flag that keeps her from engaging;
  otherwise attribution `other` plus host outcomes overwrite) [U].
- State byte: standby, snipe, cloak, down/hospital; cloak and her sniper point show on the guest from the stream.
- Buddy events (hospital, lost, dying flags `BuddyCommonFlag`) stay host-side; the guest copy's own Quiet state must not
  change through the mirror (E3 checks saved var drift) [U].

### 6.6 F6: more passenger seats (M6)

- **Light vehicle front seat (1).** States exist [O]. The board policy must let phase 1 keep seat 1 instead of the stock
  normalisation to seat 0 (`0x141251832..0x141251840`): a pinned patch beside the existing site B retarget, with
  contract pins in `tools/generate_partner_contract.py` [I]. The viewer's seat chain already maps seat 1
  (`partner_sync.lua` seat table) [O].
- **Truck cab (1).** Same patch, plus the truck door gate (CanExec: within 1.4 m of seat + 0.4 m outward; the G4
  `debug_vehicle_door` lever, lab only, `docs/native-puppet.md:520-542`) [O]. Seat-1 loop borrowed from the light vehicle
  [O]. E6 first reads the truck's seat numbering.
- **Truck bed.** Stock `stateTruckHide` / `Loop` / `ToWait` and `vehicleGameObjectIdOnPlayerOnCargo` [O names]. E7: can
  the local player use the stock bed hide on a truck the partner drives (the copy RIDE_FAST moves), and does body 1's DM
  `stateTruckHideLoop` land in the bed when anchored with seat 2 or 3? Bed shooting is a stretch goal.
- **APC and tank.** No passenger states [O]. "Crew" seat: the passenger is hidden (unrealize, like the heli hide) and
  carried with the vehicle on its own copy, camera on the vehicle. A gunner seat for the tank HMG is new work, later.
- Seat races: SEAT op (section 5).

### 6.7 F7: shooting as a passenger (M7)

- Local: E8 runs the seat-aim census (uncommitted in `D:/mgs-coop-wt/seat-aim`: `partner_seat_aim.hpp`, tests,
  `coop-vehicle-seat-aim.txt`). If the stock passenger can raise and fire (3.7), enable it and keep the seated aim latch
  fix.
- Partner view: design A from the seat-aim research: a graph-2 `GunHoldStart` (`0x50130AE2E584`) / `GunHold`
  (`0xAA0EA22A5E0E`) pose on the DM-seated body 1, plus letting the aim mirror run for a seated partner
  (`partner_aim::RideGate` refuses today) [O design, U runtime: E9].
- Hits: the shooter's own copy decides, as on foot (REQ_OUTCOME) [O].

### 6.8 F8: AI-driven and reinforcement vehicles (M8)

- The host streams RIDE_FAST for vehicles whose seat 0 holds an NPC (`GetRiderId`) within 300 m of the guest. The guest
  stops its own AI driving with `ForceStop{enabled=true}` and places the vehicle per frame [U: E10].
- Reinforcements: the host sends REINFORCE steps; the guest runs the same `TppReinforceBlock` steps locally and ignores
  its own CP's reinforcement requests (wrap the CP message handlers on the guest) [I]. The vehicle then follows the
  host's RIDE_FAST.
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
- Walker gun hits: the pilot's copy decides (`attClass` already counts the local walker as local,
  `world_outcome.lua:31`) [O].

### 6.10 F10: the shared D-Horse, one rider (M10)

- Both copies hold the host's horse as index 0 (6656) through F1; Mother Base has no horse [O].
- Pose owner: the rider's copy; unridden: the host. The other copy places its instance per frame with the API E11 finds
  (candidates: the `SetCallHorse` start warp, one of the four unnamed dispatcher hashes, a native transform write)
  [U]. Gait has no known write API (`comm+0x58`) [O]; v1 accepts a sliding horse.
- Rider drawn on the other copy: body 1 DM `stateRideOnIdle` / `Trot` / `Canter` / `Gallop` from the streamed gait
  (statuses 44..47), pinned at (0, 1.40, 0.40) in the horse frame, through a type-13 branch of anchor `0x1198770` or a
  per-tick transform pin [I]. Lift `partner_native_mount=hide` for the horse only when the instance is placed.
- **RIDE_BODY rule.** The 2026-10-22 decision protects the viewer's own horse. Under R2 the guest has no horse of its own:
  its index 0 is the host's horse. This feature therefore registers index 0 as a mirror on the non-rider copy while the
  partner rides, which replaces `docs/mounts.md` section 7's "index 0 is never a mirror" for the shared horse only [I;
  stated here so nobody flips it silently].
- Either player may drive: if the guest mounts first, the guest's copy owns the pose and the host's copy places its
  horse from the guest's stream.

### 6.11 F11: two on one horse (M11)

- SEAT claim for seat 1 (pillion) on horse slot 0; the host grants it only while seat 0 is held.
- **Driver's copy:** the passenger is body 1, drawn with `stateRideOnIdle` pinned about 0.45 m behind the saddle
  (rear-rider offset from 3.3) [I]. E12 films it at idle, trot and gallop.
- **Passenger's copy, v1:** the local player is carried at the rear offset of its copy's horse by the
  `world_horse_cargo.lua` method (a Warp every tick, SQUAT) with a pad mask that blocks movement and actions but keeps
  aim, fire, reload and weapon switch [O prototype, U whether aim survives per-tick warps: E13].
- **Passenger's copy, v2 (if v1 aims badly):** the passenger natively mounts its copy's horse (its single rider slot is
  free there, the driver being body 1), the horse is placed from the driver's stream shifted 0.45 m forward so both
  copies agree on world positions, and the passenger's steering input is masked. Needs F10's placement [I].
- Shots register on the passenger's copy (REQ_OUTCOME) [O rule].
- The driver gets off: the passenger gets off too (v1).
- Partner view of the passenger aiming: same open question as E9.

## 7. Risk register

| # | Risk | Likelihood / impact | Mitigation | Gate |
|---|---|---|---|---|
| K1 | Seeded buddy vars change the guest's co-op save (saved vars, call counts, friendly points, Quiet flags) | Medium / low (co-op saves are copies; host checkpoints already overwrite them) | Diff the guest save vars before and after | E3 |
| K2 | The guest copy's mirror companion attacks, spots or alerts on its own, and attribution sends it to the host as a guest action | High / high | Hold its AI; attribute mirror attackers as `other` | E3, E15 |
| K3 | Seeds land after the Mother Base pack list: no dog or Quiet in MB | Medium / medium | Apply the buddy keys earlier if needed | E1 |
| K4 | The relief selection var cannot be found | Medium / medium | Route B (location LV/truck only), keep `absent` for the rest | E2 |
| K5 | `SetDisableBuddyType` also blocks our own `CallBuddy` | Certain / medium | Lua wrappers instead of the mask | E4 |
| K6 | No per-frame horse placement exists | High / high (blocks M10, M11) | Static research first; fallback: hidden rider (today) or decision D3 | E11 |
| K7 | One rider slot per horse | Certain / high | Non-native passenger (E12, E13) | E12, E13 |
| K8 | Per-tick Warp breaks aim, camera or stance for the passenger | Medium / high | v2 native rider | E13 |
| K9 | Capacity-two walker: memory, stability, guest ACTION mounted nothing | High / medium | `NoRideOn`, measure memory, walker only for D2 (a) | E14 |
| K10 | AI drivers fight per-frame placement; enemy positions are lab-only | High / medium | ForceStop; depend on the enemy-sync milestone | E10 |
| K11 | Seat races within one round trip | Low / low | SEAT op, host grant | M6 proof |
| K12 | The seat-1 normalisation patch is new native code | Medium / medium | Pinned bytes in the contract generator like the board hooks | E6 |
| K13 | Vehicle ids differ between copies | Low / high | Locator hash in VEHICLE_STATE; refuse on mismatch | M4 proof |
| K14 | Removing a vehicle under a seated player | Medium / high | VEHICLE_GONE part C | E5 |
| K15 | Quiet's commands unknown | High / medium | Static read first | E15 |
| K16 | Late join mid-mission: the guest's buddy must be swapped while loaded | Medium / medium | `ReserveCallBuddy` + `Load` path (F1.2) | M1 proof |
| K17 | Silent reversal of the 2026-10-22 RIDE_BODY rule | Low / medium | Section 6.10 states the scope; tests pin "never the viewer's own buddy" for every other case | M10 review |
| K18 | `SetDisableBuddyType` scope (when the byte resets) | Unknown / low | Read it at each mission start | E4 |

## 8. Experiments before the risky milestones

All hidden-worker runs: background desktop, copied saves, lab mode unless stated, `boot` per `docs/test-boot.md`.

| # | Hypothesis | Steps | Pass / fail |
|---|---|---|---|
| E1 | Seeding `buddyType` + look vars before the guest's Load makes the guest's copy load the host's buddy, in free roam and Mother Base | Lab build with the seed keys behind a lab key. Host seed with D-Dog deployed, guest seed with no buddy or a horse. `boot 30010 828.18 338.20 1630.51 95` (`afgh-ledge`), then `boot 30050 22.61 1.80 -15.55 0` (`mb-ladder`). `both run buddy_status` | Pass: guest `buddy:G a1 ... kdog`, `IsBlockActive` true, MB dog arrives with the heli. Fail: `buddy_block_inactive`, or the guest's own buddy |
| E2 | The relief choice lives in a saved var or sortie buffer the census can read | Host seed with a vehicle chosen in sortie prep (new seed). Lab census command dumps `sortieLoadoutInfoU8buf/U16buf/U32buf`, `userPresetVehiclePaintType`, `reliefVehicleCarry*`, and the ReliefBlock record byte +0x2B, before and after a deploy, for two different vehicles | Pass: one field changes with the choice and matches the loaded `veh_rl_*` pack. Fail: route B |
| E3 | The guest's mirror buddy causes no world change and no save drift | E1 setup plus enemies near; watch `event=outcome` lines on the guest for a `buddy` attacker; diff guest save vars +0x2D68..+0x2E64 before and after a checkpoint and a mission exit | Pass: no guest-attributed outcome from the mirror; only `buddyType`/look vars differ. Fail: add an attack hold or restore the vars |
| E4 | Guest buddy and vehicle requests can be refused in Lua, and the disable byte's scope is known | Guest: iDroid buddy deploy, F9 summon, iDroid vehicle drop; log which Lua wrapper fires. Host: `SetDisableBuddyType(QUIET)`, then check the iDroid and sortie menus and when the byte clears | Pass: every guest request hits a wrapper. Fail: native-only path, use the mask fallback |
| E5 | VEHICLE_GONE is safe to default on | `coop-vehicle-gone.txt` part C (guest seated in the jeep while the host loses it) and a manual player Fulton; also try a wreck instead of Despawn | Pass: the removal waits for the rider, then applies; the Fulton removes on both. Fail: keep off |
| E6 | Seat numbers and the seat-1 offer | `GetRiderId` counts for LV, truck, WAV, tank and 25600 in 30010/30020; truck half of `coop-vehicle-passenger.txt` with `labsetting debug_vehicle_door=1`; a lab lever that keeps seat 1 at phase 1 | Pass: counts known; seat 1 boards and the stock passenger phase runs. Fail: seat 1 stays driver-only |
| E7 | The truck bed works as a passenger seat | Host drives `veh_trc_000N`; the guest uses the stock bed hide on its copy; body 1 DM `stateTruckHideLoop` with seat 2 and 3 on the host copy | Pass: frames show the guest in the bed on both copies while moving. Fail: bed out of scope |
| E8 | A local back-seat passenger can aim and fire | Run the seat-aim census `coop-vehicle-seat-aim.txt` (commit it first on a fresh branch) with `-SaveSeed africa-jeep` | Pass: the passenger raises the gun and a shot lands. Fail: needs a native aim enable |
| E9 | Body 1 can show a gun hold while DM-seated | Lab FRAME test of graph-2 `GunHoldStart` / `GunHold` on the seated body 1; check it keeps the remote's gun | Pass: a weapon pose draws in the seat. Fail: partner aim stays hidden |
| E10 | `ForceStop` stops an AI driver so per-frame placement holds | Lab: `ForceStop{enabled=true}` on an LRRP truck with its driver, then `SetPosition` each frame | Pass: no fight, smooth. Fail: M8 needs AI suppression another way |
| E11 | A per-frame horse placement exists | Static first (Ghidra read-only once reachable): the `SetCallHorse` start-position path in `0x140E4D250`, the four unnamed hashes, any `TppHorse2` transform write. Then a lab run placing the host's horse on the guest's copy at 20 Hz | Pass: smooth, no leap. Fail: K6 fallback |
| E12 | A rear rider can be drawn on the driver's copy | Lab: host rides D-Horse; DM `stateRideOnIdle` on body 1 pinned at (0, ~1.1-1.4, ~-0.05) in the horse frame at 20 Hz; frames at idle, trot, gallop | Pass: body 1 stays behind the saddle with bounded slide. Fail: a type-13 anchor branch |
| E13 | The passenger can aim and fire while carried | Lab: `cargo_test 1` with a new pad mask that keeps aim/fire; the passenger aims and shoots at a target from a moving horse | Pass: aim holds and shots register. Fail: v2 native rider |
| E14 | Two walkers can ship and each player rides their own | Capacity-two walker profile: `NoRideOn` on the peer's walker; ACTION on each copy; SetPosition stream; process memory before and after | Pass: each mounts its own, the peer's moves smoothly, memory rise acceptable. Fail: D2 (a) only with hidden pilots, or drop |
| E15 | Quiet can be mirrored | Static: find the `TppBuddyQuiet2` dispatcher from the type handle at `0x142C36A38`. Lab (`quiet-recruited` seed): hold the guest's Quiet at the host's pose; check she does not shoot on the guest copy | Pass: held, no shots. Fail: Quiet stays host-only (hidden on the guest) |
| E16 | Vanilla D-Horse rider aim, as the passenger baseline | Lab: host on D-Horse aims and fires primary and secondary | Records which weapon classes work from a horse |

## 9. Milestones

Ordered by value and risk. Each milestone ships behind a lab key first, then flips its play default only after a
hidden-worker FRAME shows it (project rule). New seeds are captured with `tools/worker/Save-WorkerSeed.ps1`.

Existing seeds used: `afgh-ledge` (30010 828.18 338.20 1630.51 95), `africa-jeep` (30020 -656.01 1.20 877.73 62),
`mb-ladder` (30050 22.61 1.80 -15.55 0), `quiet-recruited` (30050, Quiet unlocked); the lab saves deploy D-Dog. New
seeds needed: `afgh-horse`, `afgh-walker` and `afgh-quiet` (a host save with that buddy deployed in 30010; horse and
walker can be captured after `run horse_spawn` / `run walker_spawn`, Quiet needs a Quiet-unlocked save that last
sortied with her), and `afgh-relief-lv` (a host save whose sortie prep has a light vehicle chosen; how to set that
without the sortie-prep UI is part of E2).

### M1. The host owns the companion (seeds, refusals, Quiet lock)

- Scope: F1. Buddy keys in the follow seeds; guest-side reconcile (`CallBuddy` / `ReserveCallBuddy` + `Load` /
  `DeleteBuddy`); guest request wrappers and toasts; host Quiet lock; F9 summon host-only.
- Depends on: E1, E4.
- Proof (`companion-host-owned.txt`): `labsetting companion_host_owned=1`; `-HostSaveSeed` with D-Dog,
  `-GuestSaveSeed` with D-Horse (new seed `afgh-horse`, captured after a `run horse_spawn` and a checkpoint; the lab
  saves deploy D-Dog, `tools/worker/scenarios/downed-partner-unrealized.txt` header, `docs/mounts.md:50`);
  `boot 30010 828.18 338.20 1630.51 95`; `both run buddy_status`, `expect kdog` on both; `guest run horse_spawn`
  -> `expect refused`; a second run with the guest character `quiet` and a host seed with Quiet deployed (new seed
  `afgh-quiet`) -> host `waitlog 'quiet_lock'`, no Quiet on either copy; a third with `boot 30050 22.61 1.80 -15.55 0`
  (`mb-ladder`).

### M2. D-Dog synced and following the host

- Scope: F2. Never release to the guest's AI; host read without body 1; COMPANION at 10 Hz; mirror attribution
  `other`; late-join reconcile; doc drift fix.
- Depends on: M1, E3.
- Proof (`companion-dog.txt`): `boot 30010 ...`; host walks 60 m and runs 100 m (`hold w`, `burst`); `waitlog guest
  'event=buddy;role=guest;result=target_submitted;error_cm=[0-9]{1,3}'`; frames `dog-host-view` / `dog-guest-view`
  show the dog behind the host on both; `placebehind guest host 40 0`: the dog stays with the host; without
  `clear_enemies`, with a guard near the dog: the guest logs no outcome attributed to its mirror dog. Repeat in 30050
  (`mb-ladder`) and once with `-Mode play` (`title`, `waitgameplay`; play refuses `boot`).

### M3. The sortie vehicle on both copies

- Scope: F3 route A (or B after E2 fails); guest vehicle requests refused.
- Depends on: E2, M1 (seeds), E4.
- Proof (`companion-relief.txt`): new host seed `afgh-relief-lv` (sortie prep with a light vehicle chosen); `boot
  40010`, then `host worldtrigger goto=30010 goto_lz=1` (a real LZ, so the relief vehicle is placed at a drop point;
  `boot` with coordinates uses route 0, which may place none [U]); `both run vehicle_seats` / a census of 25600;
  expect it alive on both within 1 m; host drives it 100 m; `waitlog guest 'partner_ride_seat=confirmed; vehicle=25600'`,
  no `reason=absent`; frames on both. If route B is used, repeat with a host seed that chose the other faction's jeep
  (new seed `afgh-relief-west`) and record the expected `absent` on the guest.

### M4. Vehicle losses and parked vehicles

- Scope: F4. VEHICLE_GONE on in play; VEHICLE_STATE; wreck parity if E5 allows.
- Depends on: E5.
- Proof: `coop-vehicle-gone.txt` parts A-D; new `vehicle-state.txt` (`-SaveSeed africa-jeep`, `boot 30020 -656.01
  1.20 877.73 62`): the host shoves the Masa Village jeep with a truck, the guest's copy matches within 1 m after 2 s;
  the guest drives it away and leaves it, the host's copy keeps the guest's final pose.

### M5. Quiet synced

- Scope: F5 (the lock is in M1).
- Depends on: M1, E15, E3.
- Proof (`companion-quiet.txt`): host seed `afgh-quiet` (new), guest character `ocelot`; `boot 30010 ...`; Quiet visible
  on both within 5 m of the host's Quiet; host orders her to a snipe point; a guard she shoots dies on both copies once.

### M6. More passenger seats

- Scope: F6: light vehicle front seat, truck cab, truck bed; APC/tank crew seat as a stretch.
- Depends on: E6, E7; SEAT op.
- Proof: extend `coop-vehicle-passenger.txt` (`afgh-ledge` for trucks, `africa-jeep` for jeeps): each seat from both
  roles, frames on both copies while driving, stock get-off; a seat race (both press E on the same seat within 100 ms)
  ends with one seated and the other on foot.

### M7. Shooting as a passenger

- Scope: F7, light vehicle back seats first, then the front seat and the truck bed if E7 shows a usable pose.
- Depends on: E8, E9, M6 for the extra seats.
- Proof: `coop-vehicle-seat-aim.txt` with `-SaveSeed africa-jeep`: the passenger aims and kills a guard while the host
  drives; the kill appears on both copies; the host's frame shows the partner's gun raised in the seat.

### M8. AI-driven and reinforcement vehicles

- Scope: F8.
- Depends on: E10 and the enemy position sync reaching play (roadmap "Sync enemies").
- Proof: 30020 LRRP truck scenario: an AI truck passes both players; both copies show it within 2 m; a reinforcement
  APC called by an alert appears once on both.

### M9. A D-Walker each

- Scope: F9 (only if decision D2 keeps it).
- Depends on: E14.
- Proof: host seed with D-Walker (new seed `afgh-walker`); `boot 30010 ...`; each player mounts with one ACTION; each
  sees the other's walker move with its pilot; walker fire kills once.

### M10. The shared D-Horse

- Scope: F10.
- Depends on: E11 (blocking), M1.
- Proof: host seed `afgh-horse` as the host; `boot 30010 ...`; the host rides 200 m: the guest's copy shows the horse
  and the drawn rider; then the guest mounts the same horse and rides: the host's copy follows.

### M11. Two on one horse

- Scope: F11.
- Depends on: M10, E12, E13, E16.
- Proof: the host rides, the guest presses E beside the horse and sits behind; frames on both show two riders; the guest
  shoots a target while the host gallops; the kill appears on both; the host gets off and the guest gets off too.

Later, outside this plan: riding the support helicopter together visibly and its door gun; a tank gunner seat; enemy
helicopters and walker gears.

## 10. Decisions for the user

Only genuine product choices; everything else above follows the user's request of 2026-10-28.

- **D1. What may the guest do with the host's companion?** (a) Ride the horse and walk with the others, but give no
  orders: the host alone commands D-Dog, Quiet and the horse (recommended for v1, no new wire). (b) The guest's buddy
  orders are forwarded and run on the host's copy (COMPANION_CMD, needs the call-menu path, E4).
- **D2. D-Walker for the guest.** (a) Both players get a walker only when the host deploys D-Walker (fits the engine:
  one buddy type per copy). (b) The guest always gets a walker, whatever the host's buddy (needs a second buddy block:
  new native work, high risk).
- **D3. Until the shared horse is drawn (M10):** keep a riding partner hidden (today), or show the rider floating at
  saddle height with no horse under him, like the vehicle `absent` fallback.
