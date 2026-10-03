## 1. Where the project stands on campaign scope (evidence)

**Nothing campaign-specific is implemented.** Every co-op path is pinned to Mother Base free roam, mission 30050:

- **Lua gates:**
  - `lua/coop/client_join.lua:17`
  - `enemy_targeting.lua:16`
  - `host_admission.lua:24`
  - `damage_parameters.lua:61`
  - `host_session_trial.lua:10`
  - `equipment_loading.lua:7`
  - `marker_loading.lua:18`
  - `player_package_selection.lua:19` (the f30050 -> mgs_coop_f30050 swap)
  - `respawn.lua:40`
  - `session_feedback.lua:24`
  - `session_lifecycle.lua:22`
- **Native gates:**
  - `native/host_session_trial.cpp:92` (`mission==30050` read from `vars+0x12`)
  - `native/dynamite_session_port.cpp:133`
  - `native/full_close_patch.cpp:57`
  - `native/game_script_observer.cpp:301` (reads `base+0x2c01372`)

The register `data/campaign-support.json` lists 51 entries (episodes 0-50). All are `unimplemented`/`unverified`, written for 4 players, and still declare no external dependency. That contradicts the user's later authorization to reuse Dynamite.

`docs/campaign-design.md` sets acceptance bars that cannot all be met:
- four players;
- no spectator fallback (line 11);
- "Snake plus three authored survivors" in the hospital (line 61).

**Live evidence is host-only.** An owned host reached state 3 with accept=1 in 30050 (`docs/STATUS.md`, 0.10.33). No guest has joined, so no campaign claim of any kind is possible yet.

**Mismatch with upstream.** Dynamite never enabled co-op in 30050:
- 30050 is not in `IsCoop` (`moddata/standalone/TppMain.lua:99-147`).
- 30050 gets no `coop_essentials.fpk` in TppMissionList.
- Mother Base has no hostile AI, so it cannot demonstrate shared combat.

The client join flow also differs. Dynamite's client connects from the ACC before loading the mission (`webui/docs/docs.md:239-246`; `hook/src/DynamiteLua.cpp:100-102`). mgs-coop's Ctrl+F8 joins after both peers have already loaded 30050 (`docs/manual-join.md`).

## 2. How Dynamite actually makes missions "co-op" (the template to port)

**Per mission.** Dynamite replaces the whole `sNNNNN_sequence.lua` with small inserts:
- DeadInFOB arms a 10 s `RespawnPlayer` timer, which clears 4 HUD statuses and calls `Player.RequestToRespawn`.
- Nt/Network messages are announced in the log.
- The gameplay sequence `OnEnter`, guarded by `S_IS_ONLINE`, calls `TppCoder.SetWorldCenter(Vector3(..))`, `TppNetworkUtil.SessionEnableAccept(true)` and `Dynamite.StartNearestEnemyThread()`.
- Missions with a `TppMission.Reload` re-arm accept on `OnReload` (`s10070_sequence.lua:2445-2447`).
- Objective checks occasionally become either-player checks (`s10040:887`, Honey Bee).
- 10020 closes the session before its MB-transition movie (`s10020:3985-3991`).

**System scripts.**
- `TppMain.OnAllocate` sets timeout 10, calls `CreateHostSession` on the host and sets `S_IS_ONLINE` before `Mission.Start`.
- It creates one equipment ghost group, skips `ForceChangePlayerFromOcelot`, and ignores marker requests until `OnMissionCanStart` ends.
- `TppMission.OnPlayerDead` is fully disabled (`TppMission.lua:2230-2247`).
- The session closes on abort, clear, finalize and terminate (`TppMission.lua:715-738, 1089-1097, 1285-1292`; `TppMain.lua:940-946`).
- Saves are redirected to `DYNAMITE_*` (`TppDefine.lua:187-192`) and re-keyed by filename (`savebackup.go:180-213`).

**Model consequences.**
- Each player runs their own mission script on their own save. There is no objective or progression sync.
- The engine's FOB sync carries only soldier vitals and host-issued movement commands (`MAKING_OF.md:206-221`; "11 sync instances" at 314-316).
- Buddies, interrogations, supply drops and gimmicks are local or desynced (357-389).
- One player's exfiltration ends the mission. Fultoning the partner fails it.
- Pause-menu restart loads forever. Free roam was deliberately excluded (391-396).

**Mid-mission reloads.** `docs/issue_7.md` shows that `TppMission.Reload` combined with block-context network allocations (tag 0x73) causes an endless host load, and that a reload clears acceptance. mgs-coop's allocation bridge already addresses the allocation half.

## 3. Classification of all content

Classes:
- **A**: likely workable with Dynamite-style per-mission plumbing.
- **B**: needs mission-specific script adaptation or extra sync.
- **C**: likely infeasible to make faithful co-op; play solo or as a labelled parallel experience.

Dynamite status is for exe 1.0.15.3 and is community-reported; it is not verified on 1.0.15.4.

### Main missions

| Ep | Code | Title | Dynamite | Class | Why |
|---|---|---|---|---|---|
| 0 | 10010 | Awakening (prologue) | disabled | C | CYPR hospital, no heli start, hospital parts/camo, forced crawl, Ishmael-led and rail segments, layer packs s10010_l01/l02, new-save start. A playable second patient needs bespoke authored staging; no source supports it. Host-solo. |
| 1 | 10020 | Phantom Limbs | finishable | B | 16 sequences / 6 demo sequences / 1 Reload. Join point is an arrival demo. Skulls unsynced (#55). D-Horse desync. Must close the session before the MB movie. |
| 2 | 10030 | Diamond Dogs | disabled (tutorial) | B-hard | MTBS tutorial with Ocelot, Ocelot-specific game-over, demo-driven. Nearest to the existing MB work but needs a bespoke adapter. |
| 3 | 10036 | A Hero's Way | finishable | A | 2 sequences, no demos or Reload. |
| 4 | 10043 | C2W | finishable | A | 2 sequences. Objective fix landed in Dynamite v0.5.4. |
| 5 | 10033 | Over the Fence | finishable (best tested) | A | First target. Known issue: prisoner dialogue loops (hostage-flag patch). |
| 6 | 10040 | Where Do the Bees Sleep | finishable | B | Honey Bee either-player check. Skulls unsynced. 3 demo sequences. |
| 7 | 10041 | Red Brass | finishable | A | Commanders stuck (#40). |
| 8 | 10044 | Occupation Forces | finishable | B | Tank spawn host-only (#19). Vehicle seats/desync. |
| 9 | 10054 | Backup, Back Down | finishable | A | Convoy/timer runs locally per client (risk). |
| 10 | 10052 | Angel with Broken Wings | finishable | A | 1 demo sequence. |
| 11 | 10050 | Cloaked in Silence | finishable (v0.6.0) | B | Needs the BossQuiet message sync port. No-heli start. Recruit choice. |
| 12 | 10070 | Hellbound | finishable | B | 4 Reloads (issue 7). Accept must be re-armed on OnReload. Sahelanthropus chase. |
| 13 | 10080 | Pitch Dark | finishable | B | Aborts if the partner connects late (#3). 1 Reload. No-heli start. |
| 14 | 10086 | Lingua Franca | finishable | B | Interrogation never starts (#83); conversations are local. |
| 15 | 10082 | Footprints of Phantoms | finishable | A/B | Walker gears not fultonable (#21). |
| 16 | 10090 | Traitors' Caravan | ? | B | Skulls for one player only (#41). No armored vehicle (#42). |
| 17 | 10091 | Rescue the Intel Agents | finishable | A | Extraction-order confusion. |
| 18 | 10100 | Blood Runs Deep | finishable | B | Child NPC sync issues. Protected NPCs. |
| 19 | 10195 | On the Trail | finishable | B | Vehicle bugs. Tailing a moving target. |
| 20 | 10110 | Voices | finishable | B | Man on Fire encounter. 1 Reload. |
| 21 | 10121 | The War Economy | finishable | B | Walker-gear rider invisible (#116); #21. |
| 22 | 10115 | Retake the Platform | disabled | B-hard | FOB-tutorial emergency mission on MB with an instant-failure condition. |
| 23 | 10120 | The White Mamba | finishable | B | Boss (Eli) is local. |
| 24 | 10085 | Close Contact | finishable | A | |
| 25 | 10200 | Aim True, Ye Vengeful | finishable | A | |
| 26 | 10211 | Hunting Down | finishable | A | |
| 27 | 10081 | Root Cause | finishable | A | |
| 28 | 10130 | Code Talker | ? | B | Client cannot carry Code Talker (#15). Join point is the Skulls fight. 2 Reloads. |
| 29 | 10140 | Metallic Archaea | ? | B | Skulls. 6 demo sequences / 2 Reloads. No-heli start. |
| 30 | 10150 | Skull Face | ? | B | 4 Reloads. Scripted ride. Leads into 10151. |
| 31 | 10151 | Sahelanthropus | ? (boss local) | C* | Each client fights its own boss. Completable as a parallel fight, not a shared one. DD players are switched to Snake via a Reload. Chapter 1 ends here. |
| 32 | 10045 | To Know Too Much | finishable | A | #21. |
| 33 | 11043 | [Subsistence] C2W | disabled | B (variant) | Enable once the base mission passes. Subsistence forces loadout and parts. |
| 34 | 11054 | [Extreme] Backup, Back Down | disabled | B (variant) | Extreme damage plus respawn changes the difficulty intent. |
| 35 | 10093 | Cursed Legacy | ? | B | Container claim and timer. |
| 36 | 11082 | [TS] Footprints | disabled | C/B-hard | Instant fail on detection; local AI perception produces false fails. |
| 37 | 11090 | [Extreme] Caravan | disabled | B-hard | Inherits ep16 issues. |
| 38 | 10156 | Extraordinary | finishable | B | Client cannot pick up the film canister. |
| 39 | 11033 | [TS] Over the Fence | disabled | C/B-hard | Total Stealth. |
| 40 | 11050 | [Extreme] Cloaked in Silence | disabled | B | Needs Quiet sync. |
| 41 | 10171 | Proxy War Without End | ? | B | |
| 42 | 11140 | [Extreme] Metallic Archaea | disabled | B-hard | |
| 43 | 10240 | Shining Lights | finishable | B | MB quarantine. Forced Snake. Crash on skipped cutscenes (#16). 14 demo calls / 2 Reloads. Irreversible staff kills. |
| 44 | 11080 | [TS] Pitch Dark | disabled | C/B-hard | |
| 45 | 10260 | A Quiet Exit | ? | B | 18 sequences / 10 demo sequences. Quiet ally is a local buddy. |
| 46 | 10280 | Truth (final mission) | disabled; in IsCoop but no accept/pack | C | Hospital replay plus identity reveal. Host-solo. |
| 47 | 11121 | [TS] War Economy | disabled | C/B-hard | |
| 48 | 11130 | [Extreme] Code Talker | disabled | B-hard | |
| 49 | 11044 | [Subsistence] Occupation Forces | disabled | B (variant) | |
| 50 | 11151 | [Extreme] Sahelanthropus | disabled | C* | Local boss. |

Totals: about 13 A, about 25 B (including variants and the MB story missions), and C for 10010, 10280, 10151/11151 as a shared boss, and the four Total Stealth variants.

### Other content

- **Free roam 30010 (Afghanistan) / 30020 (Africa).** B-hard generic work. Dynamite excluded it. Needs:
  - IsCoop, pack-list and accept/world-center plumbing for the free sequence;
  - consistent quest-block activation (quest blocks are probably loaded around each player's local position; unverified);
  - animal and decoy client crashes handled (MAKING_OF 200-202);
  - cbox delivery and fast travel (Dynamite already special-cases `WarpByCboxDelivery` for 30010/30020);
  - mission start from free roam (session close, re-create and re-join).
- **Side ops.** 167 `QUEST_DEFINE` entries. They depend on free roam or MB. Bulk extract/eliminate ops are B; story-unlocking quests (99010-99080 family, mtbs_return_quiet) are B-hard. Under the independent-save model each player needs those unlocks, or the guest uses a copy of the host's save.
- **Mother Base.**
  - 30050 is the current scaffolding. It suits a connection and visibility smoke test and a hub, but has no enemies.
  - 30150 (zoo) has animal crash risk.
  - 30250 (ward) and the base arrival demos are story events. Play them per client or host-only (STATUS records an automatic arrival cutscene).
  - 10030, 10115 and 10240 are above.
  - FOB 50050 and MGO are excluded as official online modes.
- **Prologue / epilogue / chapter transitions.**
  - Prologue = 10010, and the closing story mission = 10280. Both are C, played host-solo.
  - The Chapter 1 credits after 10151 and the Chapter 2 interstitials are noninteractive. With Dynamite's close-on-clear they happen solo on each save.
  - Mission 51 (10230) and the nuclear-disarmament ending (official online) are out of scope.

## 4. Generic systems needed (ordered)

1. **Mission allowlist** replacing the 30050 gates. Re-verify the CloseSession route for 10xxx: wrapper `0xA1A710`, branch `0xA1A759`/`0xA1A75A`. Only the 30050 path (`+0x60 -> 0x312B80`) is mapped.
2. **Join plumbing per mission, data-driven.**
   - Actor package appended in TppMissionList: `coop_essentials` = `two_player2_instances` + `player2_additional_parts_x1` + `player2_online_motion` + `player2_mtbs_motion_loader` + FOB MTAR.
   - Host session and `S_IS_ONLINE` before `Mission.Start`.
   - Equipment ghost group.
   - Join-sequence `SessionEnableAccept(true)`, plus `OnReload`.
   - `SetWorldCenter` (coordinates in key_facts).
   - Marker guard.
   - Prefer wrapping TppSequence/TppMain over replacing 35 sequence fpkds.
3. **Client flow aligned to Dynamite.** The client connects from the ACC/heli after the host is accepting, then loads the same mission. The host waits at the LZ. No late join.
4. **Death policy generalized.** Extend `respawn.lua`/`death_policy.lua` beyond 30050. Handle fall deaths and scripted deaths. Keep mission-critical NPC deaths failing the mission.
5. **Session close on every exit**: clear, abort, game-over finalize, terminate, and the explicit pre-movie closes.
6. **Save isolation**: TppDefine save-name redirect plus a re-key copy tool. Required before any story mission.
7. **Progression**: v1 is independent isolated saves (Dynamite). v2 is host-authoritative: copy the host save to the guest at session start and discard the guest's writes.
8. **Mission-select sync**: v1 is manual agreement. v2 is a host broadcast of the mission code via a ported SyncVar/RequestVar or the typed payload, plus a guest prompt.
9. **Cutscenes**: v1 plays them locally with a "do not skip" rule. v2 adds a skip/ready barrier message.
10. **Checkpoint/continue**: disable pause-menu restart and return-to-checkpoint in co-op, or turn them into an explicit close/reload/re-join. On game over, return both players to the ACC.
11. **Results**: extend suppression (0x7F31F0; callers 0x88816E and 0x160154E) to all co-op missions. Add a master-server redirect (Dynamite: fuse at 127.0.0.1:6667).
12. **Boss sync**: port the BossQuiet messages for Quiet and the Skulls family. Label Sahelanthropus, Eli and Man on Fire as local.
13. **Player-type overrides** so the guest can stay a DD soldier in Snake-forced missions (10240, 10151, Subsistence).

## 5. Staged plan

- **S0** First real guest join, smoke-tested on 30050.
- **S1** 10033 end-to-end: join, both see each other, enemies react to both, respawn, exfiltration, close, results suppressed, isolated saves.
- **S2** The A list.
- **S3** B missions with predicate, vehicle and escort fixes, then story-transition missions (10020, 10070, 10080, 10240, 10260).
- **S4** Boss sync (10050/11050, Skulls missions). Parallel-boss labelling for 10151/11151.
- **S5** Subsistence/Extreme variants, then 10030 and 10115.
- **S6** Free roam and side ops as a separate project. Total Stealth only with a host-authoritative detection rule.

10010 and 10280 stay host-solo unless the user funds a bespoke second-patient adapter.

## 6. What cannot be verified without real two-client play

- Guest actor visibility and control.
- AI reacting to the guest.
- Damage in both directions.
- Respawn after the native branch patch.
- Objective triggers firing correctly for the client.
- Divergence of local mission scripts.
- Boss and vehicle sync.
- Cutscene and Reload behaviour while connected.
- Mission-clear and result flow, and session close with a peer.
- Whether a guest save copied from the host loads.

A single-PC two-instance setup (second Steam identity or emulator, isolated saves with the mission unlocked) can test function, but not internet latency and loss. Dynamite's statuses are third-party reports on a different exe; treat them as priors, not evidence.