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
weapons come later.

Vehicles:
- [x] **Driving** world vehicles: the partner is seated by name or by id, with smooth movement and tilt.
- [x] **Jeep back seats** for a passenger, with the passenger camera.
- [~] **Sortie vehicle on both screens, chosen by the host only** (M3). Built behind a lab key (`world_relief`, integrated on `dev/oct7-continue` 2026-10-29, unreleased), off by default: it was on by default for a few hours, until a review found that the play rehearsals behind that never drove it, ended a mission or removed it, that a guest's Fulton of it was rewarded twice when vehicle losses are off, and that the guest's LOADOUT drop was blocked before you decided (D11). The last two are fixed; the default stays off until a play test can cover driving, a mission end and a removal. What works: each PC's own game draws the host's sortie vehicle from the host's choice, at the same spot and with the same paint; a continue or a late join gets it mid mission. Proven in the hidden worker with frames on both PCs' copies (lab): the Soviet jeep, the western tank and the western truck in Africa, the western jeep and APC in Afghanistan, the host driving it on the guest's screen, the guest's vehicle request refused with a toast, the mission-end reward once (the host's), the host's Fulton removing it on both, and the guest's Fulton of it rewarded once, on the host, with vehicle losses off and on (runs `5c35644d`, `cbc30b91`; earlier `d18e8fa0`, `1a5c1fa0`; in play with a bad network only the arrival and the refusal, `40c09484`, `98e4af59`). The guest's LOADOUT drop stays open until you decide D11. Not yet: its destruction, a play test of driving, a mission end and a removal, the host's in-mission vehicle drop (later), a two-PC test.
- [~] **Vehicle losses shared:** Fulton and destruction of world vehicles (M4, integrated on `dev/oct7-continue` 2026-10-29, unreleased). Done, with hidden-worker frames on both screens: either player's own Fulton by hand in play over `-WanSim bad` (`e44516d7`; the guest's also in the lab, `5179b50b`), the removal held while the guest sits in the jeep (`92720b75`), the engine's Fulton (`92720b75`, `075c1ea4`). A jeep the host blows up with C4 in play disappears from the guest's PC (`4bbbfde4`, `b31a1b65`, no vehicle key named; `b31a1b65` frames the burning wreck on the host's screen and the empty spot on the guest's). Leaving a wreck on the guest's PC too (`0edadbe2`, a lab lever) stays behind its lab key until its blast on the guest's PC is measured with live guards. On by default in play in Afghanistan and Africa since `482eb74`; Mother Base lab only (no hidden run there); not two-PC tested. Open: a jeep the guest destroys stays intact on the host's PC (the game posts no Damage message for a vehicle hit, so the guest's request never starts: `5179b50b`, `35ba7929`, and `72366eb5` where the guest's copy broke on a drive); the sortie vehicle's loss is M3.
- [~] **Vehicle damage shared** short of destruction (M4): body damage follows the host's PC both ways, so the host's hits show on the guest's PC (`e235bda6`, `d9b48fcb`, `4bbbfde4` in play) and the guest's own hits are put back to the host's value at the host's next state (`4bbbfde4` in play: 1602 back to 2270); wheels and burning are not shared (no write for them was found in the vehicle commands); the guest's damage never reaches the host's PC.
- [~] **Parked and pushed vehicles** kept in place by the host (M4): a vehicle the partner drives and leaves stays there on both screens (`d9b48fcb`, `4bbbfde4` in play, frame `play-left-both` on both screens; RIDE_FAST's last placement puts it there and VEHICLE_STATE then keeps the host's pose). When the host dives out of a rolling jeep, the guest's copy, stopped 8 m short, is moved to where the host's came to rest (`b31a1b65` in play; frame `play-bail-both` on both screens after the move). Pushes: a host-side push follows on the guest's PC, a guest-side push is put back and a push is held while a player stands at the vehicle, shown with the lab lever `vehicle_push` only (frames on both screens in `3585869d`; `d407f115`, `0edadbe2`, `72366eb5`); a physical shove (a player or another vehicle) was never tested. On by default in play since `482eb74` (Afghanistan and Africa); not two-PC tested.
- [ ] **Enemy-driven vehicles:** trucks, convoys, reinforcement APCs and tanks, and side-op armored vehicles and tanks, shown where the host has them (M8, needs enemy sync in play).
- [ ] **More passenger seats:** jeep front seat, truck cab, truck bed, inside APCs and tanks (M6).
- [ ] **Aim and shoot from vehicles:** as a passenger (M7); the driver's vehicle weapons (tank cannon and machine gun, APC guns) and mounted gun positions (M13, later).
- [~] **Helicopter.** Host-led travel works. Riding together visibly (M12, research first) and the door gun (M13) are missing.

Companions:
- [~] **One companion per session, the host's** (M1): the guest's PC loads the host's buddy with its look; the guest cannot pick, call or order one (a dismiss through the iDroid is not yet refused); the guest's own save keeps its own buddy. Built behind a lab key and proven in the hidden worker on both PCs' copies with frames (the dog in Mother Base, the guest's call menu off, the refusal toasts; runs `489b3c6f`, `fa397547`) and in a play rehearsal (`b83c9f7a`). Since the 2026-10-29 review fixes the guest's PC shows only a companion it can hold still (the dog): a host horse, walker or Quiet is not shown there until its milestone (M1 had drawn a host horse 4 m in front of the guest, where the guest could ride it), and both PCs must run the key (frames: the host's horse on the host's PC and none on the guest's, runs `9165baeb`, `b651b42c`). A dismiss through the iDroid is not yet refused. Play default off until the companion is shown never to act or get hurt on its own on the guest's PC (with M2); the iDroid buddy request is not yet checked; with the key on the guest has no Knock or Sonar while the host's companion is out; no two-PC test yet.
- [~] **Quiet only when the guest is not playing her** (M1): not offered to the host in sortie prep, removed if out when the guest joins, and the Mother Base cell Quiet hidden. Built behind the same key: the host's Quiet is sent away and her type disabled, and the cell Quiet is gone on both PCs (frames, runs `b993af09`, `de43726a` in play; that the hide, not a cell that never spawned, emptied the cage is not yet shown). The 2026-10-29 review found that the sortie rewrite was undone when the game reloads the save at mission start (Quiet loaded and was sent away on the first frame), and that a host who picked another buddy got Quiet back; fixed so a host's own choice is saved and loaded and Quiet stays in the host's save (runs `74d5ba7e`, `9165baeb`: no Quiet ever loaded on the host, the host's dog kept through a checkpoint Continue and a trip to Africa, frames of it on the host's PC). Not yet checked: the host's sortie-prep and iDroid buddy lists; no two-PC test yet.
- [~] **D-Dog** on both screens, following the host only (M2). With M1's lab key the guest's PC loads the host's dog whatever the guest deployed (Mother Base included), holds it at the host dog's pose and never lets it act on its own: in the hidden worker it stays within 3 m walking (one window at 3.11 m) and 5 m running in the measured legs (a few unmarked windows reached 6-8 m, cause unknown), stays with the host when the guest is 40 m away or the host sits in the helicopter (one window at 3.0 m before the host's body appeared), and stood beside a guard in combat for up to 3 minutes without engaging (frames on both screens, runs `d937191f`, `ffb2d7ab`, `994d252f`; those frames predate the last two hold changes; with the current hold the dog stands beside the host on both screens in run `b651b42c`). Play default off: guards on the guest's PC can kill the guest's copy of the dog, which then stays gone for that mission (the guest's save is protected; a lab repair brought it back once after about 19 s, `033193d3`); what to do about it is the user's decision (D7). The dog's attack at the right guard and its enemy marks on the guest's screen wait for enemy sync in play and shared marking; the dog's takedown result reaching the guest is not yet shown; no play rehearsal or two-PC test yet.
- [ ] **Quiet** as in the normal game, synced (M5). Her shots' results are shared; the mirror on the guest's PC must never fire on its own.
- [~] **Companions safe on the guest's PC:** the guest's copy of a companion never attacks on its own and is not hurt by the guest's (unsynced) guards (M1-M10). D-Dog: never attacks on its own (held, E3 passes), but guards on the guest's PC can kill it (E19 fails, runs `d937191f`, `dc761c98`); Quiet, the horse and the walker are not shown on the guest's PC until their milestones (2026-10-29 review fixes).
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

## Suggested order
1. **Two-PC check of 0.14.3:** ship it to both PCs and run the next two-PC test (the 2026-10-23 bugs are fixed but not tested on two PCs). Step 2 runs in parallel with it.
2. **Vehicles and companions, cheapest first** (the user's first priority, 2026-10-28): the host owns the buddy and the Quiet lock (M1), D-Dog stage 1 (M2), the sortie vehicle (M3), vehicle losses, damage and parked vehicles (M4). Then Quiet, seats and passenger shooting (M5-M7); the walker and the shared horse (M9-M11) after their experiments. The dog's and Quiet's full combat sync (stage 2) and enemy-driven vehicles (M8) come after step 4.
3. **One story mission end to end:** pick an open-world Chapter 1 mission (for example episode 3 or 4). Sync mission load, objectives, enemies, extraction and results.
4. **Enemy AI sync** (positions in play, and perception). Every mission depends on it, and so do enemy-driven vehicles (M8).
5. **Cutscenes** and radio.
6. **Bosses** and the special missions.
7. **The hospital prologue and the endings:** the hardest, bespoke sequences.
8. **Progression,** Mother Base and side ops.
