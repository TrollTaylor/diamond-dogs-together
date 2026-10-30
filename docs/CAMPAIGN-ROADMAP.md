# Campaign roadmap: what it takes to play the whole game in co-op

Goal: two players finish the entire Phantom Pain together. That means the prologue, Chapter 1 and 2, every story mission, the side ops, Mother Base events and the endings, in one shared world.

`[x]` done (free roam / Mother Base) · `[~]` partly done · `[ ]` not started

As of 0.14.3 (2026-10-28: version bump `a0653b5`, package built at `a4c18fa`). Today co-op runs in Mother Base (30050), Afghanistan free roam (30010) and Africa free roam (30020). The 0.14.x fixes are proven in the hidden worker only; the last real two-PC session was 0.13.6. The rest of this list is what remains.

## 1. Shared world
- [~] **Sync enemies.** Kills, sleep, wake-ups, Fultons and vanishing are shared. Patrol and combat position correction exists but works in lab only (play refuses the soldier warp), so each PC still moves its own guards; AI state and animations are not shared.
- [~] **Sync alerts and CP state.** Escalations are shared, and an alert the guest causes also alerts, on the host's PC, the guard they fought. Not yet shared: caution and search behaviour, reinforcement calls, CP radio.
- [ ] **Sync enemy perception.** Either player can be spotted, and the enemy reacts to the right player on both PCs.
- [~] **Sync hold-ups, interrogation and CQC** on enemies. The hold-up state is shared (no hands-up pose); interrogation and CQC are not.
- [ ] **Sync prisoners and hostages:** position, carry, extraction.
- [ ] **Sync enemy helicopters and walker gears.** (Enemy trucks, convoys, reinforcement APCs and tanks, side-op armor and parked vehicles are in section 3.)
- [ ] **Sync animals** (and their capture).
- [ ] **Sync destructibles and gimmicks:** AA radars, comms, generators, mines, gates, lights. Only the design exists (`world-sync-design.md`).
- [ ] **Sync pickups and resources:** materials, diamonds, plants, posters, intel files, item boxes. Each pickup happens once.
- [ ] **Sync support:** supply drops, support fire, air strikes, smoke and decoys.
- [x] **Sync time and weather.**
- [~] **Sync travel.** Host-led travel works between the ACC, Mother Base and free roam. Mission deploys are still missing.

## 2. Players and partner body
- [x] **On foot:** movement, stances, aim, reload, weapon switch and throws.
- [x] **Downed, revive, sleep and carry** between players (0.14.3: the box counts as downed).
- [x] **Guest character:** story (Quiet or Ocelot), Ocelot, a DD soldier or the avatar can be picked. The avatar still shows as Snake on the partner's screen (see "Avatar" below).
- [x] **Partner name tag, friendly crosshair, CO-OP status line and toasts.**
- [x] **Body safety:** sea deaths, ledge and carry falls.
- [~] **Traversal.** Ladders, vaults and climbing onto ledges work. Pipes and wall climbing are built but not yet seen on film; wall cracks are missing.
- [~] **Gear and gestures.** The cardboard box, pickup, held C4 and Hand of Jehuty show. Hand signs and knocking are built but not yet seen on film. Still missing: box exit motion, C4 placing and mines in online play.
- [ ] **Bionic arms:** stun, rocket and the others, beyond Hand of Jehuty.
- [~] **Show what the partner throws and Fultons.** Grenades and empty magazines show. Decoys, smoke and mines do not; the partner's Fulton balloon is unchecked.
- [ ] **Avatar** shown correctly on the partner's screen (today the partner sees Snake).
- [ ] **Shared marking:** enemies marked by one player show for both; iDroid map markers.
- [ ] **Reflex mode policy:** off, or synchronized for both players.
- [ ] **Partner damage from enemies** reconciled. Today each PC counts only its own player's hits.

## 3. Vehicles and companions

Design, scope table, experiments and milestones M1-M13: [vehicles-companions-design.md](vehicles-companions-design.md)
(revision 2). M1-M4 are integrated on `dev/oct7-continue` (2026-10-29, unreleased, not two-PC tested): M1 and M2
behind the lab key `companion_host_owned` (off in lab and play), M3, the sortie vehicle, behind `world_relief` (off in
lab and play), and M4's vehicle losses and vehicle state on by default in play in Afghanistan and Africa (off in the
lab unless named; the wreck on the guest's PC behind `world_vehicle_wreck`, off). The rest is not built yet. Decided 2026-10-28: one companion per session, the host's; the host
alone gives it orders; both players get a D-Walker when the host brings one. The support helicopter and vehicle
weapons come later. Decided by the user on 2026-10-29 and integrated on `dev/oct7-continue` (merges `4e3d0e5` and
`92c4f9f`; unreleased, not two-PC tested; per decision in the design's section 10 table):
- D7: the guest's copy of the host's D-Dog cannot be hurt on the guest's PC, follows the host dog's state and comes
  back after a death the guard cannot see. Built and proven in the lab with frames on both PCs' copies (`3922cc80`,
  `606048af`, `cc2e15d2`); **off by default** with M1's key (a default session installs nothing: `19e438fe`,
   and the combined smoke `a1d7aaa1`); not proven in play (play cannot drive the paths, and guards never fired at the dog there). See D-Dog below.
  Since 2026-10-29 (user approval; `wp/dog-heroism`, merged on `dev/oct7-continue` in `101d925`, unreleased): the heroism and bond the stock game took from the
  guest at each death of its copy are given back on the guest, once per death (lab `c7fdfdbf`, control `2243bbc6`;
  off with the same key).
- D11: the guest's LOADOUT and (the reviewer's pick) VEHICLES supply drops are refused in co-op with a toast; the
  host's are untouched. **On by default in play**; frames on both copies in play over `-WanSim bad` (`f729d7f4`,
  `33a7fa6e`; again in the combined smoke of the merged head, `a1d7aaa1`, and in the smoke of the merged fixes after 0.14.4, `32c3893a`).
- D12: a vehicle the guest destroys is destroyed on the host's PC too when the guest stood within 300 m (refused while
  the host rides it; it waits while D-Dog or a soldier sits in it; removed instead of wrecked while a player is within
  15 m). **On by default in play** in Afghanistan and Africa (`f729d7f4`, `33a7fa6e`; see Vehicle losses). Fixed
  2026-10-29 (the user's pick, `wp/vehicle-fixes2`, merged on `dev/oct7-continue` in `f1e80be`, unreleased): a vehicle with only soldiers aboard is wrecked on the
  host's PC at once, crew included (`world_vehicle_crew`, on in play; frames on both copies in the lab `ac0f7656` and
  in play `8995e351`, again at the shipped defaults `34d5744d`). After the review (2026-10-30): an armoured vehicle's
  own blast knocked a player down 25 m away, so it waits while a player stands within 50 m of a truck, armoured
  vehicle or tank (15 m of a jeep) and is wrecked once both are clear (frames on both copies, lab `c0a385a5`, play
  `c6db67b8`, again at the shipped defaults `98d84a21`). Open: an enemy vehicle that drives off in a fight moves separately on each PC (M8), so the guest's
  destruction of it stays split (`247a4eed`); a launcher or rifle kill at range not run.
- D11 fix (2026-10-29, `wp/vehicle-fixes2`, merged in `f1e80be`, unreleased): the guest's LOADOUT and VEHICLES items are also inactive in
  the window after a host-led travel before the two copies pair (the lab `ac0f7656`, `d3f5ca56`; both refusals with
  their toasts on the guest's screen before PAIRED in `251adcb6`, the host held on its loading screen; in play over
  `-WanSim bad` both refused with their toasts before PAIRED, the host held on its tips screen, `98d84a21`; LOADOUT
  also in `c6db67b8`). If the host never arrives, the items come back after 5 minutes.
- D13: VEHICLE_GONE and VEHICLE_STATE stay on in play in Afghanistan and Africa; Mother Base and the wreck parity stay
  lab only (nothing changed). The combined smoke `a1d7aaa1` (shipped defaults, `-WanSim bad`) had a jeep the guest
  drove with the host in a back seat parked where the guest left it, on both screens; again in `32c3893a` (2026-10-30, the merged fixes after 0.14.4, the shipped defaults with the crew rule on and the damage report off).
- D14: a Fultoned world vehicle is rewarded to both players' saves, once each (the sortie vehicle keeps one reward,
  the host's). **On by default in play** (`c0203fdd` lab with the mission end, `70146170`, `47b95ae7` in play; the host's Fulton again in `a1d7aaa1` and `32c3893a`);
  APCs, tanks and trucks not yet run.
- D15: the guest's bail out of a rolling jeep is left as it is (not run) until the two-PC session.

Vehicles:
- [x] **Driving** world vehicles: the partner is seated by name or by id, with smooth movement and tilt.
- [x] **Jeep back seats** for a passenger, with the passenger camera.
- [~] **Sortie vehicle on both screens, chosen by the host only** (M3). Built behind a lab key (`world_relief`, integrated on `dev/oct7-continue` 2026-10-29, unreleased), off by default: it was on by default for a few hours, until a review found that the play rehearsals behind that never drove it, ended a mission or removed it, that a guest's Fulton of it was rewarded twice when vehicle losses are off, and that the guest's LOADOUT drop was blocked before you decided (D11). The last two are fixed; the default stays off until a play test can cover driving, a mission end and a removal. What works: each PC's own game draws the host's sortie vehicle from the host's choice, at the same spot and with the same paint; a continue or a late join gets it mid mission. Proven in the hidden worker with frames on both PCs' copies (lab): the Soviet jeep, the western tank and the western truck in Africa, the western jeep and APC in Afghanistan, the host driving it on the guest's screen, the guest's vehicle request refused with a toast, the mission-end reward once (the host's), the host's Fulton removing it on both, and the guest's Fulton of it rewarded once, on the host, with vehicle losses off and on (runs `5c35644d`, `cbc30b91`; earlier `d18e8fa0`, `1a5c1fa0`; in play with a bad network only the arrival and the refusal, `40c09484`, `98e4af59`). The guest's LOADOUT drop is refused in co-op since D11 (2026-10-29: the toast "Only the host can order a loadout drop in co-op", frame in play `70146170`; the host's LOADOUT list still opens; on by default in play, whatever `world_relief` says). Since the D11 review (2026-10-29) the guest's VEHICLES drop is refused in co-op too, whatever `world_relief` says, with the toast "Only the host can bring or request a vehicle in co-op." (frames on both screens in play over `-WanSim bad`, `f729d7f4`; the host's VEHICLES list opens). Not yet: its destruction, a play test of driving, a mission end and a removal, the host's in-mission vehicle drop (later), a two-PC test.
- [~] **Vehicle losses shared:** Fulton and destruction of world vehicles (M4, integrated on `dev/oct7-continue` 2026-10-29, unreleased). Done, with hidden-worker frames on both screens: either player's own Fulton by hand in play over `-WanSim bad` (`e44516d7`; the guest's also in the lab, `5179b50b`), the removal held while the guest sits in the jeep (`92720b75`), the engine's Fulton (`92720b75`, `075c1ea4`). A jeep the host blows up with C4 in play disappears from the guest's PC (`4bbbfde4`, `b31a1b65`, no vehicle key named; `b31a1b65` frames the burning wreck on the host's screen and the empty spot on the guest's). Leaving a wreck on the guest's PC too (`0edadbe2`, a lab lever) stays behind its lab key until its blast on the guest's PC is measured with live guards. On by default in play in Afghanistan and Africa since `482eb74`; Mother Base lab only (no hidden run there); not two-PC tested. A jeep the guest destroys is now destroyed on the host's PC too (D12, 2026-10-29, on by default in play): the guest's PC asks, and the host's PC wrecks its copy when the guest stood within 300 m (or the host's copy was already badly damaged); never while the host sits in it, and it waits (up to 2 minutes) while D-Dog or a soldier sits in it (`7762536a`, D-Dog). The host's wreck explodes about 8 s later like any burning vehicle, hard enough to take 3500 of 7800 life from a player beside it (`7762536a`), so while either player stands within 15 m of the host's copy it just disappears on the host's PC instead (in play over `-WanSim bad` the host 8 m away kept full life and saw the jeep vanish; 27 m away it saw the jeep burn and the burnt wreck, `f729d7f4`). Earlier frames: `c0203fdd` (lab), `70146170` (the guest's real C4 in play). If the host refuses (for example while the host sits in that jeep, `09333f05`), the guest keeps a wreck the host does not have, and the jeep then exists only on the host's PC for the rest of the mission. Fulton rewards (D14): both players' saves get the vehicle, once each (each save +2 light vehicles after one Fulton by each player and the mission end, `c0203fdd`; the Fulton fee only on the player who Fultoned). The sortie vehicle's loss is M3.
- [~] **Vehicle damage shared** short of destruction (M4): body damage follows the host's PC both ways, so the host's hits show on the guest's PC (`e235bda6`, `d9b48fcb`, `4bbbfde4` in play); the guest's own gunfire damage can reach the host's PC too (VEHICLE_DAMAGE, `wp/vehicle-fixes2`, merged in `f1e80be`, unreleased; lab `ac0f7656`, play `247a4eed`: 2020 -> 1990 on both past the refresh, and `34d5744d`), but only behind the lab key `world_vehicle_damage`: the review of 2026-10-30 turned it off in play again, because each PC's own guards also hit the vehicle and their rounds on the two PCs add up in a firefight (`846c1147`), and no run with live guards proved the guard against that. Off, the guest's damage is put back at the host's next state (`4bbbfde4`: 1602 back to 2270); wheels and burning are not shared (no write for them was found in the vehicle commands).
- [~] **Parked and pushed vehicles** kept in place by the host (M4): a vehicle the partner drives and leaves stays there on both screens (`d9b48fcb`, `4bbbfde4` in play, frame `play-left-both` on both screens; with the host in a back seat, `a1d7aaa1` in play at the shipped defaults; RIDE_FAST's last placement puts it there and VEHICLE_STATE then keeps the host's pose). When the host dives out of a rolling jeep, the guest's copy, stopped 8 m short, is moved to where the host's came to rest (`b31a1b65` in play; frame `play-bail-both` on both screens after the move). Pushes: a host-side push follows on the guest's PC, a guest-side push is put back and a push is held while a player stands at the vehicle, shown with the lab lever `vehicle_push` only (frames on both screens in `3585869d`; `d407f115`, `0edadbe2`, `72366eb5`); a physical shove (a player or another vehicle) was never tested. On by default in play since `482eb74` (Afghanistan and Africa); not two-PC tested.
- [ ] **Enemy-driven vehicles:** trucks, convoys, reinforcement APCs and tanks, and side-op armored vehicles and tanks, shown where the host has them (M8, needs enemy sync in play).
- [~] **More passenger seats** (M6, design 8.20 / 8.21): the jeep's front seat (lab key `partner_ride_front`; both roles, frames on both screens while moving in the lab: `f49c167d`, `3c748d46`; since the review fixes it is offered only when the driver's screen has that seat free, and with the driver's D-Dog in it the passenger takes a back seat: lab `f115a2b3`, play `56322b36` with moving frames on both screens; the seat-1 path itself has no play rehearsal with that code), the truck cab (lab key `partner_ride_cab`; both roles with moving frames on both screens and both get-offs: `0f145a4e`), the seat race (lab key `partner_ride_race`, SEAT op 54: the guest waits for the host's grant, so nobody becomes a second driver even when both press at once: lab `43473119`, `f115a2b3`, play `56322b36`); all off in play until the user decides D16 / D17. Merged into `dev/oct7-continue` (`c616d8b`, unreleased); the combined play smoke at the shipped defaults (`a6830b7c`, design 8.24) still offers the back seats only. The truck bed and the APC / tank crew are not built.
- [ ] **Aim and shoot from vehicles:** as a passenger (M7); the driver's vehicle weapons (tank cannon and machine gun, APC guns) and mounted gun positions (M13, later). M7 tried on `wp/seat-shoot` (2026-10-30, design doc 8.22 and 8.23, eight hidden runs; merged in `5a18d3b`, unreleased): a back-seat passenger aims and fires its gun on its own screen (no frame shows the gun raised) and the aim check is skipped while riding, but it can only aim across the jeep, its aim swings up at each shot, no seated shot landed in six runs (the same method killed on foot), and the partner's raised gun does not show on the driver's screen (the graph-2 pose behind the lab key `partner_seat_gun` runs but draws nothing). The combined play smoke `a6830b7c` (shipped defaults, design 8.24) saw the same stock aim and fire in play, with the camera inside the jeep's body. Next step: decision D18.
- [~] **Helicopter.** Host-led travel works. Riding together visibly (M12, research first) and the door gun (M13) are missing.

Companions:
- [~] **One companion per session, the host's** (M1): the guest's PC loads the host's buddy with its look; the guest cannot pick, call or order one (a dismiss through the iDroid is not yet refused); the guest's own save keeps its own buddy. Built behind a lab key and proven in the hidden worker on both PCs' copies with frames (the dog in Mother Base, the guest's call menu off, the refusal toasts; runs `489b3c6f`, `fa397547`) and in a play rehearsal (`b83c9f7a`). Since the 2026-10-29 review fixes the guest's PC shows only a companion it can hold still (the dog): a host horse, walker or Quiet is not shown there until its milestone (M1 had drawn a host horse 4 m in front of the guest, where the guest could ride it), and both PCs must run the key (frames: the host's horse on the host's PC and none on the guest's, runs `9165baeb`, `b651b42c`). A dismiss through the iDroid is not yet refused. Play default off until the companion is shown never to act or get hurt on its own on the guest's PC (with M2); the iDroid buddy request is not yet checked; with the key on the guest has no Knock or Sonar while the host's companion is out; no two-PC test yet.
- [~] **Quiet only when the guest is not playing her** (M1): not offered to the host in sortie prep, removed if out when the guest joins, and the Mother Base cell Quiet hidden. Built behind the same key: the host's Quiet is sent away and her type disabled, and the cell Quiet is gone on both PCs (frames, runs `b993af09`, `de43726a` in play; that the hide, not a cell that never spawned, emptied the cage is not yet shown). The 2026-10-29 review found that the sortie rewrite was undone when the game reloads the save at mission start (Quiet loaded and was sent away on the first frame), and that a host who picked another buddy got Quiet back; fixed so a host's own choice is saved and loaded and Quiet stays in the host's save (runs `74d5ba7e`, `9165baeb`: no Quiet ever loaded on the host, the host's dog kept through a checkpoint Continue and a trip to Africa, frames of it on the host's PC). Not yet checked: the host's sortie-prep and iDroid buddy lists; no two-PC test yet.
- [~] **D-Dog** on both screens, following the host only (M2). With M1's lab key the guest's PC loads the host's dog whatever the guest deployed (Mother Base included), holds it at the host dog's pose and never lets it act on its own: in the hidden worker it stays within 3 m walking (one window at 3.11 m) and 5 m running in the measured legs (a few unmarked windows reached 6-8 m, cause unknown), stays with the host when the guest is 40 m away or the host sits in the helicopter (one window at 3.0 m before the host's body appeared), and stood beside a guard in combat for up to 3 minutes without engaging (frames on both screens, runs `d937191f`, `ffb2d7ab`, `994d252f`; those frames predate the last two hold changes; with the current hold the dog stands beside the host on both screens in run `b651b42c`). Since the user's decision D7 (2026-10-29, `wp/dog-guard`, integrated on `dev/oct7-continue` in `4e3d0e5`) the guest's copy cannot be hurt on the guest's PC (a native guard drops its hits: about 85 s of guard fire through it, no death, run `3922cc80`), follows the host's dog (hurt, asleep, down: "D-Dog Near Death" and its extraction on both screens, `3f69e3b7`, `606048af`), and comes back about 2.6 s after a death the guard cannot see (frames on both screens, `606048af`); a play rehearsal with the guard passed (`1713b0a1`), but its guards never fired at the dog. Play default still off: the guard's drop, the follow and the restore are proven in the lab only, and M1's review fixes have no play rehearsal. The dog's attack at the right guard and its enemy marks on the guest's screen wait for enemy sync in play and shared marking; the dog's takedown result reaching the guest is not yet shown. Review fixes (2026-10-29): the guard now installs only under the lab key (a default play session installs nothing, run `19e438fe`), holds through a link loss (it kept dropping guard fire for about 25 s with a lab lever that makes the guard see the link as lost, and a death in that state waited for the host's state before the restore, run `9fa48400`; a real link loss not yet run), and the restore no longer rewrites the game's buddy flags (restore, down and re-call again with frames on both screens, `cc2e15d2`). Each death of the guest's copy cost the guest's PC 30 heroism and 2 bond points (the stock "D-Dog Near Death"); since the user's approval (2026-10-29, `wp/dog-heroism`, merged on `dev/oct7-continue` in `101d925`, unreleased; behind M1's key, so off in lab and play) the guest gets exactly that back, once per death, while the host's own dog's death and a guest's own dog keep the stock penalty (lab run `c7fdfdbf` with the notice on the guest's screen and equal readbacks before and after; control `2243bbc6`; no play rehearsal). No two-PC test yet.
- [ ] **Quiet** as in the normal game, synced (M5). Her shots' results are shared; the mirror on the guest's PC must never fire on its own.
- [~] **Companions safe on the guest's PC:** the guest's copy of a companion never attacks on its own and is not hurt by the guest's (unsynced) guards (M1-M10). D-Dog: never attacks on its own (held, E3 passes) and, with the D7 guard (`wp/dog-guard`), cannot be hurt there either (E19 passes, runs `3922cc80`, `7fe2845e`, `cc2e15d2`; lab proof only, play default off; since the 2026-10-29 review the guard installs nothing unless the lab key is on); Quiet, the horse and the walker are not shown on the guest's PC until their milestones (2026-10-29 review fixes).
- [ ] **D-Walker,** one each when the host brings D-Walker (M9). Today the partner's walker is missing and its rider hidden.
- [ ] **D-Horse:** the host's horse on both screens, ridden by either player (M10). Today a riding partner is hidden.
- [ ] **D-Horse for two:** one steers, the other rides behind and shoots, in either order (M11). Blocked on finding a way to place a horse every frame.
- [ ] **Buddy story events:** Quiet's arc, D-Dog growing up, bond levels.

## 4. Missions
- [ ] **Load story missions together:** deploy from the ACC, matching mission start and start positions.
- [ ] **Sync mission state:** sequence, svars, objectives, targets, intel. The design exists in `world-sync-design.md`.
- [ ] **Shared mission start:** the briefing, deployment choices (landing zone, time, buddy, loadout) and a ready check.
- [ ] **Shared mission end:** extraction by heli or leaving the hot zone, the results screen, rewards granted once.
- [~] **Checkpoints, retry and game over.** These work in free roam; extend them to every mission.
- [ ] **Bosses** with one shared phase and health:
  - Skulls;
  - Quiet (episode 11);
  - the Man on Fire (the prologue and episode 20);
  - Sahelanthropus (episodes 31 and 50).
- [ ] **Escorts and critical NPCs:** one NPC, shared carry and safety rules.
- [ ] **Special missions:**
  - the hospital prologue (Ground Zeroes-style crawl);
  - the quarantine on Mother Base (episode 43);
  - A Quiet Exit (episode 45);
  - Truth (episode 46).
- [ ] **Mission variants:** Subsistence, Extreme and Total Stealth.
- [ ] **Side ops:** shared spawning, objectives and completion.
- [ ] **Mission tasks and challenges:** counted once, for the right player.

## 5. Cutscenes and story
- [ ] **Figure out how to do cutscenes.** Play them on both PCs at the same time, hide or place the partner during the scene, and use a skip vote.
- [ ] **Radio calls and story codec** at the same moment for both players.
- [ ] **Interactive scripted scenes,** like the hospital escape and the prologue horse ride. Both players need real roles, not spectators.
- [ ] **Story flags, mission unlocks and chapter transitions** driven by the host.
- [ ] **Endings and post-game states.**

## 6. Mother Base and progression
- [x] **Save handoff:** the joiner gets the host's progress automatically.
- [~] **One shared campaign save** owned by the host, with the guest's own record. The host's checkpoints are copied into the guest's co-op save; each PC still writes its own.
- [ ] **Rewards once:** GMP, resources, staff, recruits and Fulton credits are never doubled. Needs a decision: today Fulton recruits go into both co-op saves on purpose.
- [ ] **Base management:** development, staff assignment and platform building. The host decides; the guest can view.
- [ ] **Mother Base visits and events:** staff greetings, shower, Paz, demos.
- [~] **FOB** stays out of co-op. Co-op PCs run offline, so FOB cannot be reached; a clean refusal in the menu is not checked yet.

## 7. Netcode and sessions
- [x] **Steam P2P** with join codes and invites; no port forwarding.
- [~] **Late join and reconnect** at a safe point, without duplicate enemies or reset objectives. Works in free roam and Mother Base; missions remain.
- [~] **Host leaves:** the guest is told at once and waits to reconnect. Keeping the last checkpoint is missing.
- [ ] **Desync detection and resync** for mission state, not just enemies.
- [~] **Bad connections:** test and tune at 150–250 ms and 3–5 % loss in every mission type. Free roam passed at about 180 ms and up to 4 % loss; nothing above that or in missions.
- [ ] **3–4 players** (optional, after two-player is complete).

## 8. Testing and release
- [~] **Real two-PC sessions** on each release. Done for 0.13.5 and 0.13.6; none since 0.14.0.
- [ ] **A test plan per mission:** start, objectives, boss, extraction, checkpoint reload.
- [ ] **A full campaign playthrough in co-op,** prologue to ending.
- [x] **One-exe launcher:** setup, backup of the real save, co-op save copy, Host/Join, Share save, Collect logs, Uninstall, and the update checker.
- [x] **Developer testing:** the hidden background worker, fast test boot, save seeds and parallel tests.
- [x] **Release checks cover what ships** (2026-10-29, `wp/launcher-fixes2`, merged on `dev/oct7-continue` in `416b2fa`, unreleased): the Release receipt runs the fixture of every shipped module (311 tests on the branch, 312 after the merge with `world_vehicle_damage_lua_check`; a test it neither runs nor excludes by name is refused), and the source zip refuses files git does not track. Also fixed from the 0.14.4 review: two automatic launcher setups could run at once after a window closed during setup (hidden end-to-end runs `r4`, `r5`), and the tag and network wording in the README and notices. Round 2 of that review (same branch): the source zip is described against `build.source_commit` (the tag can differ), the package test no longer goes red on a developer's untracked files, the untracked-file refusal fails closed without git, hidden-run preflights skip the launcher's helper-process and icon checks (Release only), an abandoned setup lock is tested, and a start during the closing window's last ~50 ms waits for the folder instead of exiting (unit tests; no end-to-end run of that). After the merge, a hidden run's Research preflight ran the merged list (310 tests) and passed (`20261030-044444-8a250766...`, the smoke `32c3893a`); no Normal-profile preflight yet.

## Suggested order
1. **Two-PC check of 0.14.3:** ship it to both PCs and run the next two-PC test (the 2026-10-23 bugs are fixed but not tested on two PCs). Step 2 runs in parallel with it.
2. **Vehicles and companions, cheapest first** (the user's first priority, 2026-10-28): the host owns the buddy and the Quiet lock (M1), D-Dog stage 1 (M2), the sortie vehicle (M3), vehicle losses, damage and parked vehicles (M4). Then Quiet, seats and passenger shooting (M5-M7); the walker and the shared horse (M9-M11) after their experiments. The dog's and Quiet's full combat sync (stage 2) and enemy-driven vehicles (M8) come after step 4.
3. **One story mission end to end:** pick an open-world Chapter 1 mission (for example episode 3 or 4). Sync mission load, objectives, enemies, extraction and results.
4. **Enemy AI sync** (positions in play, and perception). Every mission depends on it, and so do enemy-driven vehicles (M8).
5. **Cutscenes** and radio.
6. **Bosses** and the special missions.
7. **The hospital prologue and the endings:** the hardest, bespoke sequences.
8. **Progression,** Mother Base and side ops.
