# Campaign roadmap: what it takes to play the whole game in co-op

Goal: two players finish the entire Phantom Pain together. That means the prologue, Chapter 1 and 2, every story mission, the side ops, Mother Base events and the endings, in one shared world.

`[x]` done (free roam / Mother Base) · `[~]` partly done · `[ ]` not started

Today co-op runs in Mother Base (30050), Afghanistan free roam (30010) and Africa free roam (30020). The rest of this list is what remains.

## 1. Shared world
- [~] **Sync enemies.** Kills, sleep and Fultons are shared; positions, patrols, AI state and animations are not (each PC runs its own guards).
- [~] **Sync alerts and CP state.** Escalations are shared. Not yet shared: caution and search behaviour, reinforcement calls, CP radio.
- [ ] **Sync enemy perception.** Either player can be spotted, and the enemy reacts to the right player on both PCs.
- [ ] **Sync hold-ups, interrogation and CQC** on enemies.
- [ ] **Sync prisoners and hostages:** position, carry, extraction.
- [ ] **Sync other vehicles:** enemy and parked vehicles, spawns, movement, damage, destruction and Fulton. VEHICLE_GONE exists but is off by default.
- [ ] **Sync enemy helicopters, tanks and walker gears.**
- [ ] **Sync animals** (and their capture).
- [~] **Sync destructibles and gimmicks.** The design exists. Not yet shared: AA radars, comms, generators, mines, gates, lights.
- [ ] **Sync pickups and resources:** materials, diamonds, plants, posters, intel files, item boxes. Each pickup happens once.
- [ ] **Sync support:** supply drops, support fire, air strikes, smoke and decoys.
- [x] **Sync time and weather.**
- [~] **Sync travel.** Host-led travel works between the ACC, Mother Base and free roam. Mission deploys are still missing.

## 2. Players and partner body
- [x] **On foot:** movement, stances, aim, reload, weapon switch and throws.
- [x] **Downed, revive, sleep and carry** between players.
- [x] **Driving** (driver seat) and the jeep back seat.
- [~] **Traversal.** Ladders and ledges work. Still missing: wall cracks, pipes, hanging and vaulting edge cases.
- [~] **Gear and gestures.** The cardboard box and pickup work. Still missing: hand signs, knocking, box exit motion, C4 and mines in online play.
- [ ] **Aim and shoot from vehicles** and as a passenger; mounted gun positions.
- [ ] **Show the partner on D-Horse and D-Walker,** instead of hiding them.
- [ ] **Bionic arms:** stun, rocket and the others, beyond Hand of Jehuty.
- [ ] **Show the partner's Fulton** balloon, and items they carry or throw (decoys, magazines, smoke).
- [ ] **Avatar** shown correctly on the partner's screen.
- [ ] **Shared marking:** enemies marked by one player show for both; iDroid map markers.
- [ ] **Reflex mode policy:** off, or synchronized for both players.
- [ ] **Partner damage from enemies** reconciled. Today each PC counts only its own player's hits.

## 3. Buddies
- [~] **D-Dog.** The guest's dog follows the host's. Attacks, spotting and health are local.
- [ ] **Sync D-Horse, D-Walker and Quiet** as buddies: one shared buddy, plus each player's own.
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
- [ ] **Interactive scripted scenes,** like the hospital escape and ride sections. Both players need real roles, not spectators.
- [ ] **Story flags, mission unlocks and chapter transitions** driven by the host.
- [ ] **Endings and post-game states.**

## 6. Mother Base and progression
- [x] **Save handoff:** the joiner gets the host's progress automatically.
- [ ] **One shared campaign save** owned by the host, with the guest's own record.
- [ ] **Rewards once:** GMP, resources, staff, recruits and Fulton credits are never doubled.
- [ ] **Base management:** development, staff assignment and platform building. The host decides; the guest can view.
- [ ] **Mother Base visits and events:** staff greetings, shower, Paz, demos.
- [ ] **FOB** stays out of co-op: block it, or end co-op cleanly first.

## 7. Netcode and sessions
- [x] **Steam P2P** with join codes and invites; no port forwarding.
- [ ] **Late join and reconnect** at a safe point, without duplicate enemies or reset objectives.
- [ ] **Host leaves:** stop safely and keep the last checkpoint.
- [ ] **Desync detection and resync** for mission state, not just enemies.
- [ ] **Bad connections:** test and tune at 150–250 ms and 3–5 % loss in every mission type.
- [ ] **3–4 players** (optional, after two-player is complete).

## 8. Testing and release
- [ ] **Real two-PC sessions** on each release.
- [ ] **A test plan per mission:** start, objectives, boss, extraction, checkpoint reload.
- [ ] **A full campaign playthrough in co-op,** prologue to ending.

## Suggested order
1. **Two-PC stability:** finish the open bugs from real sessions.
2. **One story mission end to end:** pick an open-world Chapter 1 mission (for example episode 3 or 4). Sync mission load, objectives, enemies, extraction and results.
3. **Enemy AI sync** (positions and perception). Every mission depends on it.
4. **Cutscenes** and radio.
5. **Bosses** and the special missions.
6. **The hospital prologue and the endings:** the hardest, bespoke sequences.
7. **Progression,** Mother Base and side ops.
