# Join-fix Phase 2 merged spec: automatic save handoff (host-to-guest transfer on link channel 4, verified staging, guest restart through Play-Coop) and guest travel lock

## Summary
JUDGE VERDICT. Each candidate was scored 0-10 on correctness against the code, save safety, chance on a real two-PC Steam session, testability in the hidden worker, and size. Candidate 1 wins and is the base of this spec.

- Candidate 1 (in-game transfer, WM_CLOSE exit, Play-Coop applies and relaunches): 7 / 8 / 7 / 8 / 5, total about 7.2. WINNER.
  - Strengths: the receiver pulls the data; the exit happens only at the title; the files are applied by tooling after the game process has exited; the rejoin plan for invite and F9-paste guests is sound; the worker can prove it in two sessions; its NOTE codes 5-9 fit the Phase-1 range (K5 accepts 1..15, and K6 reserves 16 for native).
  - Errors: its title-time Continue proof reads TppDefine.SAVE_SLOT.CHECK_POINT. No extracted script contains that slot. Only CHECK_POINT_RESTARTABLE and MISSION_START (build/startup-script-inspection-20260909/package-02.lua) and MB_MANAGEMENT (build/debug-api-source-audit-20261006/lib/TppDebug.lua:302) exist, so that path is dead.
  - Its in-game eligibility check has no settle, so it could snapshot a save written before arrival.
  - Resume, nine message kinds and a 30 s auto-Continue at the title are needless weight.
- Candidate 3 (safety first, F9 click): 6 / 9 / 6 / 7 / 6, total about 6.8.
  - Strengths: the best "need" rule (transfer only when the guest cannot meet the host); no resume; deletes only known file names; status buffers raised to 1536; F9 rows that work at the title.
  - Its host gate is "first title in a supported ACC". That does not prove the Continue target: the friend's 40010 title Continued into 10020, and a progressed save checkpointed in a story mission behaves the same way.
  - It requires an F9 click, so the restart is not automatic as the user asked.
  - Its NOTE codes 16 and 17 clash with Phase-1 K5/K6.
  - Its lock requires W.mode==2, yet its travel-lock scenario runs in lab mode.
  - Its sortie lock is warn-only.
- Candidate 2 (apply before the first read): 6 / 6 / 6 / 5 / 3, total about 5.4.
  - It has the best read-timing analysis and a strong provenance idea, binding a save set to the mission it Continues into.
  - Its in-process gate is speculative. The mission-1 runtime thread is the install thread 86976 (userlogs/C-user-guest.log:31, :49), so a native hold risks a Not Responding window.
  - It applies saves natively inside early Steam getter hooks, needs a CreateFileW IAT wrapper, edits debug_overlay.cpp, and uses NOTE codes 16-22, which clash with Phase 1.

FACT CHECKS against the code:
1. When the game reads the save, and whether a restart is avoidable:
   - world=configured is at t=33377803 and install at 33378531 (C-user-guest.log:22, :31). Mission-1 runtime starts at :49 and the link connects at :57, about 0.5 s later.
   - Mission 1 ends already pointing at the title helicopter 40050 (:75), so the save is read by then. The exact read time is not logged anywhere.
   - After the read, an in-process swap is overwritten from memory by SaveGameData (package-02.lua:431, :902, :983, :2167). ReturnToTitle aborts into the helicopter with isNoSave (package-02.lua:635-640) and does not reload.
   - Conclusion: a restart is not avoidable in v1. Tooling applies the files after the process exits, so nothing the game writes on exit can mix in. The read-timing probe is deferred as an open question.
2. Play-Coop watchdog:
   - It polls every 2 s. On the first exit after it has seen the game it breaks out and calls Invoke-CoopPlayCleanup (tools/friend/Play-Coop.ps1:131-153). The console's finally block also cleans up (:665-690).
   - Invoke-CoopPlayCleanup runs whenever state=='active' (tools/friend/FriendCommon.psm1:586-635). So the candidates' claim "state stays active, so the watchdog skips cleanup" is wrong: with state active, the watchdog does clean up.
   - Protection therefore needs two things: a handoff claim taken under Use-CoopPlayMutex (FriendCommon.psm1:492-505) before any cleanup, and a new cleanup guard that refuses while a live relaunch claim exists.
3. WM_CLOSE:
   - The first WM_CLOSE goes to PartnerShutdown (native/debug_overlay.cpp:390-395; native/game_script_observer.cpp:1220-1242), but only when the overlay has subclassed the window (debug_overlay.cpp:1023-1027).
   - In the worker, a WM_CLOSE exit is graceful in some runs (anim run 873d7197 stop.json: forced_job_termination=false) and forced in others (cf395755: true), so a slow-exit fallback is required.
4. Channel 4: every bound loops on kReliableChannels (link_protocol.cpp:678-700; partner_net.cpp:96-103, :824-828, :1680-1760). Older peers silently drop and never acknowledge channel >= 4 (partner_net.cpp:824), so the receiver must pull and a build gate is needed.
5. Rejoin: a waiting guest never reads link-secret.txt (partner_net.cpp:1295-1312). BuildCode is host-only (:1819-1830). The guest's code must therefore be kept in memory to rejoin after a restart.
6. Lua: F.install runs F.installHooks before it wraps anything (lua/coop/world_follow.lua:244-247), and W.tick calls W.follow.tick dynamically (lua/coop/world_sync.lua:235-248). An inner-wrapper lock chunk therefore needs only one F.tick line in world_follow.lua.
7. The vanilla scripts compare story sequences numerically (package-01.lua:278, TppQuest.lua:1263, package-02.lua:619).
8. The shared UI state cannot live in mgs_debug_menu, because mgs_debug_menu does not link core (CMakeLists.txt:644). It goes in mgs_coop_core, which debug_menu_ui (mgs_debug_overlay, :635) already reaches through Phase-1 coop_text.

MERGED DESIGN. It builds on Phase 1: W.peer, W.note, note codes 5/6, FormatMission, the K12 key position, and the K18 marker with source=handoff.

(g) Save handoff:
- Transport: reliable channel 4 (kReliableChannels 4 -> 5). The guest pulls with WANT. There is no resume: a new link session restarts the transfer, and a verified staged set is reused.
- Host eligibility needs all of these:
  - Lua story state open, meaning str_storySequence >= CLEARD_TO_MATHER_BASE and the three constants are readable and strictly ordered.
  - A Continue proof: the current on-disk set hash equals a set bound to a SUPPORTED mission. A binding comes from (a) a Continue load (k==4) into that mission, or (b) 30 s in-game in 30010/30020/30050 with no save in progress and a stable set. Bindings persist in <save_dir>-handoff\provenance.json, so later sessions can hand off from the title. This is Candidate 2's idea, simplified.
  - Not saving, and a double-read snapshot that matches the binding.
- Guest need (Candidate 3's "scene" policy plus provenance): the sets differ AND one of these holds: the guest's story is not open; its own set is bound to an unsupported mission; its title-helicopter location differs from the host's location. This avoids a handoff every session. lab.ini save_handoff=always forces the literal "if it differs".
- Staging: <save_dir>-handoff\<xfer>.partial, then renamed to <xfer> with handoff.json; request.json is written last.
- Restart: an automatic 10 s countdown at the title only, then the guest posts WM_CLOSE to its own window (found with EnumWindows; no debug_overlay edit). The countdown can be vetoed by pressing Continue twice or with the F9 'Keep my own save' button.
- Apply: Play-Coop or the watchdog claims the request under the play mutex, applies it with the Share-Save import core moved into FriendCommon (aside folder <stamp>-before-handoff; K18 marker source=handoff), and rewrites lab.ini with save_handoff=off. For steam-wait guests it also converts link-rejoin.txt into link-secret.txt plus net_peer_steam. It records the file hashes and relaunches through Steam. One restart per session.

(h) Travel lock:
- A new Lua chunk, world_handoff.lua, installs inner wrappers under world_follow's armed-guest suppression, so the verified follows are untouched.
- It applies only to an unarmed, linked guest whose host is at level 3 in a SUPPORTED mission other than 1, and not loading (W.peer().mission). It is off while the game is replaying a host transition.
- Always allowed: the host's mission, any helicopter, restart, reload, return to title.
- Title Continue: refused softly when the guest's title location differs from the host's, when the guest's save is at the story start, or while a handoff is pending. Pressing again within 10 s goes alone.
- Sortie: held with its arguments, as in the Phase-1 host hold. It is released by choosing it again (go alone), by the F9 unlock, after a 120 s bound, or when it turns into a follow.
- iDroid ReserveMissionClear: refused, with a repeat within 1-30 s going alone.
- The F9 'Travel lock' checkbox (a UI-only row, so it works at the title and in the helicopter) turns the lock off for the process.
- The host is warned (note 6) when it loads into a mission the partner cannot join. The guest is warned (note 11) when its Continue lands somewhere other than the host's mission.
- While a handoff is pending at the title, follow replays are held. This prevents the predicted next real session, where both titles are 40010, from replaying a story-opening save into 30010.

Worker proof takes two sessions: the handoff is staged, the game exits through WM_CLOSE, and a coordinator step applies it; a second session seeds the guest from the first run and pairs.

## Contracts
- P2-0 PHASE-1 REUSE (unchanged meanings):
- K1 CFG r[1..11].
- K16 W.peer() = {mission=r[9], level=r[10], wait=r[11], session, linked} and W.note(code,a,b).
- K6 note codes 5 (travel_locked) and 6 (host_continue_unsupported), now given meanings below.
- K10/K11 coop_text::FormatMission / FormatMissionShort.
- K12 link-status keys inserted after the transport block and before time_utc.
- K13 world_link::Status.remote_mission.
- K18 imported-progress.json marker (source 'handoff') and '-before-handoff' folder detection.
- K19 Collect-Logs listing rows.
Phase 2 starts only after Phase 1 has merged into wp/join-fix.
- P2-1 CFG reply appendix (native/world_receipt.cpp FormatCfgReply; CfgReply in native/include/mgscoop/world_wire.hpp:182-187 gains 'std::uint8_t handoff, lock;' after Phase-1's remote_mission, remote_level and wait). The reply is 'return <r1..r11>,<handoff>,<lock>'.
- r[12] handoff code for Lua: 0 none/off/host role, 1 wait_host (guest needs the host's save; host not eligible yet), 2 receiving, 3 staged_or_countdown, 4 exiting, 5 kept (same/same_scene/declined), 6 failed_or_refused. Lua 'pending' = 1..4; Lua 'busy' = 2..4.
- r[13] lock: 1 when the travel lock may apply (lab.ini travel_lock effective, F9 unlock not set, guest role), else 0.
- Both are filled only by world_link::HandleCfg after Core::Cfg; Core stays pure.
- Lua treats a nil value as 0, so old fixtures with 8 or 11 values keep today's behaviour.
- P2-2 SAVE receipt: "return 'MGS_COOP_WORLD_SAVE_V1' -- <op> <a> <b> <c> <d> <e>". Exactly 6 integers in the strict world_receipt grammar (no sign, no leading zeros, single spaces). Dispatched by SelectWorldReceipt only when world_on and only on the runtime thread; world_quiet=true.
- op 1 REPORT: a=mission 0..99999, b=title 0|1, c=story 0 unknown|1 opening|2 open, d=saving 0|1, e=ingame 0|1. Sent on any change, otherwise every 1000 ms while W.active.
- op 2 CONTINUE: a=target 1..99999 (vars.missionCode at ReservePlayerLoadingPosition k==4), b=title 0|1, c=story; d=e=0.
- op 3 DECLINE: a=1 second_continue | 2 f9; b..e=0.
Replies: 'return true' (accepted), 'return false' (module off or rate-gated: more than 4 per second), or the literal (malformed or refused). Lua ignores the result.
- P2-3 Lua NOTE codes added by Phase 2 (Phase-1 world_link::HandleNote now treats 1..12 as known and 13..15 as unknown; sentences live in coop_text::FormatWorldNote):
- 5 TRAVEL_LOCKED {a=host mission, b=target}
- 6 HOST_PARTNER_CANNOT_JOIN {a=target, b=partner mission}
- 7 TITLE_CONTINUE_LOCKED {a=host mission, b=1 location_differs | 2 story_start | 3 waiting_for_host_save}
- 8 TRAVEL_ALONE {a=target, b=1 repeat | 2 f9_unlock | 3 timeout}
- 9 CONTINUE_DURING_HANDOFF {a=pct 0..100, b=0}
- 10 HANDOFF_KEPT_OWN {a=0, b=0}
- 11 GUEST_LANDING_ELSEWHERE {a=target, b=host mission}
- 12 SORTIE_HELD {a=target, b=host mission}
Phase-1 NoteGate rules apply (1 s post dedupe, 10 s log dedupe).
- P2-4 Channel 4 (native/include/mgscoop/link_protocol.hpp:287: kReliableChannels = 5; comment '0 link, 1 world, 2..3 game, 4 save handoff').
- Only native/save_handoff.cpp sends on channel 4 or drains it, on the receive thread through HandoffHooks.
- DATAGRAM stays channel 1.
- Ascending pack order keeps channels 1..3 first (link_protocol.cpp:673-703).
- The host sends nothing but STATE until it receives a WANT. No channel-4 byte is sent unless the link peer's remote_build (LinkInfo) passes RemoteBuildAtLeast(kHandoffMinVersion). Play builds are identical anyway (link_session.cpp:341-353).
- P2-5 Wire format (little-endian; strict decode; exact length per kind; a violation is dropped and counted; log 'save_handoff=bad_msg; why=<magic|version|kind|size|bounds>' once per 10 s).
Header, 8 bytes: u8 magic 0x53, u8 version 1, u8 kind, u8 flags=0, u32 xfer (nonzero, BCryptGenRandom per offer; 0 only for STATE).
Kinds:
- 1 STATE (both directions, ReliableMode::latest), 64 B: u8 role (1 host, 2 guest), u8 phase (0 unknown, 1 loading, 2 title, 3 ingame), u8 story, u8 mode (0 off, 1 stage, 2 scene, 3 always), u32 mission, u32 bound_mission (provenance of the current set, 0 unknown), u8 set_known, u8 need (guest only), u8 elig (host: 0 unknown, 1 eligible, 2 story_opening, 3 story_unknown, 4 continue_unknown, 5 continue_unsupported, 6 saving, 7 unexpected_files, 8 too_large, 9 off), u8 reserved=0, u8 set_sha256[32], u8 pad[8]=0. Sent on change (at least 500 ms apart), every 5 s, and at every new session.
- 2 OFFER (host to guest), 52+40n B (n=1..6, at most 292): u8 n, u8 0, u16 0, u32 total (<= 8 MiB), u32 bound_mission, u8 set_sha256[32], then n entries {u8 slot 0..5, u8[3] 0, u32 size 1..4 MiB, u8 sha256[32]} in ascending slot order with no duplicates.
- 3 WANT (guest to host), 12 B: u8 mask (bit = slot), u8[3] 0.
- 4 DECLINE (guest to host), 12 B: u8 reason (1 same, 2 same_scene, 3 off, 4 budget, 5 user, 6 not_title, 7 bad_offer, 8 disk_space), u8[3] 0.
- 5 CHUNK (host to guest), at most 1016 B: u8 slot, u8 0, u16 len 1..1000, u32 offset, payload. Sent in offered slot order, with offsets contiguous from 0.
- 6 END (host to guest), 12 B: u32 total.
- 7 RESULT (guest to host), 12 B: u8 status (0 staged, 1 exiting, 2 failed), u8 reason, u16 0.
- 8 ABORT (either direction), 12 B: u8 reason (1 link_lost, 2 user, 3 stall, 4 snapshot_changed, 5 not_title, 6 shutdown, 7 protocol), u8[3] 0.
- P2-6 File table and set hash (native, PowerShell and tests/fixtures/handoff-set.json share them).
Slots:
- 0 287700/local/TPP_GAME_DATA0
- 1 287700/local/TPP_GAME_DATA1
- 2 287700/local/PERSONAL_DATA0
- 3 287700/local/PERSONAL_DATA1
- 4 remote/TPP_GAME_DATA
- 5 remote/PERSONAL_DATA
Rules:
- At least slot 0 or slot 1 must be present.
- Any other name matching ^(TPP_GAME_DATA[0-9]?|PERSONAL_DATA[0-9]?)$ (tools/friend/Share-Save.ps1:55) in those two folders means elig=unexpected_files.
- set_sha256 = SHA-256 (link_crypto.hpp:24-33 / System.Security.Cryptography.SHA256) over the present slots in ascending order. Each contributes u8 slot, u32 size LE and the raw 32-byte file SHA-256.
- Hex is upper case, like Get-FileHash.
- Settings files (TPP_CONFIG_DATA*, TPP_GRAPHICS_CONFIG, MGO_GAME_DATA*) are never hashed, sent or replaced.
- P2-7 Decisions (pure functions in save_handoff_core):
- HostEligible: elig=1 only when all hold:
  - story==2 (Lua: str_storySequence is a number, CLEARD_ESCAPE_THE_HOSPITAL < CLEARD_RECUE_MILLER < CLEARD_TO_MATHER_BASE are numbers in that order, and seq >= CLEARD_TO_MATHER_BASE);
  - the current set equals a provenance entry whose mission is in {30010,30020,30050,40010,40020,40050};
  - saving==0;
  - the Lua report is fresh (<= 3000 ms);
  - no unexpected files; total <= 8 MiB.
  Otherwise elig carries the first failing reason in this order: off, story_unknown, story_opening, unexpected_files, too_large, saving, continue_unknown, continue_unsupported.
- GuestNeed(policy, own, host): 0 when policy is off, or when own.set == host.set. Otherwise policy 'always' gives 1. For 'scene', need=1 when any of these holds:
  - own.story != 2;
  - own.bound_mission != 0 and is not SUPPORTED;
  - Loc(own title-ACC mission) != Loc(host.bound_mission, or host.mission when unbound).
  Loc is world_follow's SUPPORTED map: 10, 20 or 50.
- The host OFFERs only when all hold:
  - the guest STATE has phase==2, mode>=1, need==1, and a set different from the host's;
  - HostEligible;
  - the peer build is ok;
  - fewer than 3 offers for this set in this link session.
- P2-8 Provenance (both roles). Bindings:
- (a) On a Lua op 2 CONTINUE with target M: hash the set (double read, 500 ms apart) and bind it to M (source=continue).
- (b) Lua reports ingame=1, mission M in {30010,30020,30050}, saving=0 continuously for 30000 ms, and the set is unchanged across that window: bind it to M (source=settle).
Storage: up to 8 entries (LRU) in <save_dir>-handoff\provenance.json, written with lab::WriteFileAtomic:
{"format":"mgs-coop-save-provenance","v":1,"entries":[{"set":"<64 HEX>","mission":30010,"source":"continue|settle","utc":"...Z"}]}
It is read at Configure; an unknown v or a malformed file is ignored and rewritten. Log on every new binding: 'save_handoff=provenance; set=<8 hex>; mission=%u; source=%s'.
- P2-9 Guest staging under <save_dir>-handoff (from save_redirect::format_handoff_dir: the trimmed save_dir plus '-handoff'; refused when the longest path would reach 248 chars).
In the product this is <game>\mgs-coop\coop-saves-handoff; in the worker it is run\<role>-saves-handoff.
Order of writes:
1. <xfer8hex>.partial\{287700\local,remote}\<name>
2. handoff.json inside it: {"format":"mgs-coop-save-handoff","v":1,"xfer":"0A1B2C3D","created_utc":"...Z","host_build":"<link remote_build>","host_mission":30010,"set_sha256":"<64 HEX>","files":[{"entry":"287700/local/TPP_GAME_DATA0","bytes":199916,"sha256":"<64 HEX>"}]}. The entry and sha256 forms match Share-Save's share-save.json.
3. MoveFileExW of <xfer>.partial to <xfer>.
4. request.json, written last and only in scene or always mode: {"format":"mgs-coop-save-handoff-request","v":1,"pid":N,"process_start_utc":"...Z","xfer":"0A1B2C3D","set_sha256":"<64 HEX>","mode":"scene|always","rejoin":0|1,"created_utc":"...Z"}.
Tooling also writes applied.json there: {"format":"mgs-coop-save-handoff-applied","v":1,"set_sha256":"..","applied_utc":"..","aside":"<leaf>"}.
Native deletes only these known names and empty folders, never recursively, and deletes request.json first.
- P2-10 Exit gate (guest; pure ExitGate plus glue). PostExit happens only when all of these hold:
- mode is scene or always;
- the state is staged, the request has been written and the RESULT staged has been sent;
- the last Lua REPORT is <= 1500 ms old with title=1, saving=0 and ingame=0;
- save_handoff::NoteMission says no partner mission and not terminating;
- 10000 ms of countdown have elapsed with the title held throughout;
- no decline has arrived (Lua op 3 or the F9 KeepOwnSave request).
On exit: RESULT exiting is sent, then EnumWindows picks the largest visible unowned top-level window of GetCurrentProcessId() and PostMessageW(WM_CLOSE) is posted. With no window found: log 'save_handoff=exit; how=no_window' and toast G9. If the process is still alive 20 s later: log 'save_handoff=exit_slow' and toast G9 (after a WM_CLOSE from the glue's own timer, since PartnerShutdown stops the core; review N1). A decline or the guest's own Continue (op 2) while exiting still drops the request (review SS-1).
If the title is lost for more than 2 s, or on a decline: ABORT, delete request.json then the staged files, and set state kept (decline) or failed (not_title).
- P2-11 lab.ini keys (native/lab_config.cpp next to world :409; unknown keys are already ignored at :452):
- save_handoff=off|stage|scene|always. When the key is absent the value is off; Play-Coop writes it explicitly.
- travel_lock=0|1. Absent means 1 in play and 0 in lab.
The module configures only when all hold: labc.net, NetLink::v2, lab::SavesRedirected(), world>=1 (world_on), save_handoff != off, and format_handoff_dir succeeds. Otherwise it logs 'save_handoff=disabled; reason=off|world_off|legacy_link|saves|path'.
'stage' transfers and stages but never writes request.json and never exits (worker R1 only).
- P2-12 partner_net additions (native/include/mgscoop/partner_net.hpp):
- 'struct HandoffHooks { void (*on_idle)(std::uint64_t now_ms) noexcept; void (*on_session)(std::uint32_t session) noexcept; void (*on_peer_close)() noexcept; }; void SetHandoffHooks(const HandoffHooks*) noexcept;' Set at install time, before Start.
  - on_session is called after each g_world.on_session (partner_net.cpp:854 and ArmSteamGuest :1177).
  - on_peer_close is called after the CLOSE handling, outside the locks.
  - on_idle is called after g_world.on_idle at the end of LinkIdle (:952).
  - All run on the receive or transport thread with no partner_net lock held.
- 'unsigned ReliableQueued(std::uint8_t channel) noexcept;' (shared g_tx_lock).
- 'bool BuildGuestJoinCode(char* out, std::size_t capacity) noexcept;' True only for a Steam guest that started waiting and was armed. ArmSteamGuest copies the secret and host into g_guest_secret/g_guest_host under g_secret_lock before zeroing (:1166); SteamOnShutdown zeroes them.
- 'void PublishHandoffState(std::uint8_t token, std::uint8_t pct) noexcept;' Atomics only.
- The status buffers g_status_last (:124), content (:702) and document (:706) grow from 1024 to 1536.
- P2-13 link-status.json: '"handoff":"<token>","handoff_pct":<0-100>,' is appended after the Phase-1 K12 keys and before "time_utc".
Tokens, in enum order: off, idle, same, wait_host, offered, sending, partner_staged, partner_restarting, receiving, staged, countdown, exiting, kept, refused, failed, applied.
A missing key means off for tools.
- P2-14 Shared UI state, native/include/mgscoop/save_handoff_state.hpp (mgs_coop_core; seqlock POD; any thread; no allocation):
- struct View { bool available; std::uint8_t role, token, pct, why, lua_code; std::uint32_t host_mission, bytes; bool can_keep, travel_unlocked; };
- void Publish(const View&) noexcept; View Get() noexcept;
- void RequestKeepOwnSave() noexcept; bool TakeKeepOwnSave() noexcept;
- void SetTravelUnlocked(bool) noexcept; bool TravelUnlocked() noexcept;
- const char* TokenName(std::uint8_t) noexcept;
Readers: debug_menu_ui (F9), worker_debug (state.json), world_link (CFG r[12..13]) and partner_net (via PublishHandoffState).
- P2-15 HUD and text.
- coop_hud EventKind save_handoff is appended after Phase-1's join_ignored. arg = toast code (G1..G10 = 1..10, H1..H7 = 21..27); text = '<a> <b>' in decimal (mission, pct or kb).
- LinkView appends 'std::uint8_t handoff{}, handoff_pct{};' after Phase-1's fields.
- 'bool coop_text::FormatSaveHandoff(std::uint8_t code, std::uint32_t a, std::uint32_t b, const char* partner, char* out, std::size_t cap) noexcept;'.
- FormatWorldNote gains codes 5..12.
- Zero defaults reproduce every existing FormatLine string.
- P2-16 Native log lines (script-observer.log; '; t_ms=N' appended):
- 'save_handoff=configured; role=%s; mode=%s; channel=4; dir=ok'
- 'save_handoff=disabled; reason=%s'
- 'save_handoff=state_rx; role=%s; phase=%u; story=%u; mission=%u; bound=%u; need=%u; elig=%u; set=%.8s' (on change only)
- 'save_handoff=need; need=%u; why=same|same_scene|story|unsupported|location|always|off'
- 'save_handoff=provenance; set=%.8s; mission=%u; source=%s'
- 'save_handoff=offer; xfer=%08X; files=%u; bytes=%u; set=%.8s; mission=%u'
- 'save_handoff=decide; result=want|decline; reason=%s'
- 'save_handoff=progress; xfer=%08X; pct=25|50|75|100'
- 'save_handoff=staged; xfer=%08X; files=%u; bytes=%u; ms=%u'
- 'save_handoff=countdown; ms=10000'
- 'save_handoff=exit; how=wm_close|no_window'
- 'save_handoff=exit_slow'
- 'save_handoff=abort; why=%s; xfer=%08X'
- 'save_handoff=failed; why=%s'
- 'save_handoff=result_rx; status=%s'
- 'save_handoff=applied_seen; set=%.8s'
Lua lines (W.log):
- 'event=handoff;step=report;title=%d;mission=%d;story=%d;saving=%d;ingame=%d' (on change)
- 'event=handoff;step=lock;fn=%s;kind=%d;target=%d;host=%d;decision=allow|refuse|hold|repeat|unlock|timeout|follow|decline'
- 'event=handoff;step=warn;reason=partner_cannot_join|landing_elsewhere;target=%d'
- 'event=handoff;step=replay_hold;code=%d'
- P2-17 play-session.json v2 additions, read through Get-FriendProperty:
- game_pid and game_start_utc, recorded when the game is seen;
- 'handoff' = {state: 'applying'|'relaunching'|'relaunched'|'failed'|'limit', claimed_by_pid, claimed_start_utc, claimed_utc, xfer, set_sha256, count (0..1), aside, message}.
The session state stays 'active'. Invoke-CoopPlayCleanup gets a guard: it does nothing (Ran=$false, message 'a save handoff restart is in progress') while handoff.state is 'applying' or 'relaunching', the claim is under 300 s old and the claimant process is alive. -Force (used by Play-Coop -Cleanup and after a deadline) bypasses the guard.
- P2-18 PowerShell API.
FriendCommon.psm1:
- Import-CoopProgressFiles -StageRoot -SaveDir -ReplacedRoot -Suffix ('-before-import'|'-before-handoff') -Marker <hashtable>: the Share-Save core (Share-Save.ps1:192-252) moved here. It builds the new copy as a sibling of SaveDir, writes the Phase-1 K18 marker into it, moves the current copy aside, renames the new copy in, and rolls back on failure. Returns the aside path.
- Test-CoopProgressStage -StageRoot -Manifest
- Get-CoopProgressBackups -ReplacedRoot (both suffixes, newest by name)
- Get-CoopHandoffSetSha256 -Entries
- Get-CoopHandoffRequest -HandoffDir -ExpectedPid -ExpectedStartUtc
- Invoke-CoopHandoffAfterExit -GameRoot -ExitedPid -ExitedStartUtc -Owner console|watchdog [-LauncherAlive]: returns @{Action='none'|'deferred'|'relaunched'|'claimed_by_other'|'limit'|'stale'|'failed'; Messages; Aside; Files; Bytes}
- Remove-CoopStaleHandoff -GameRoot
- Invoke-CoopPlayCleanup -Force
CoopPlay.psm1:
- Get-InstancePaths adds CoopSavesHandoff, CoopSavesReplaced and LinkRejoin.
- New-CoopPlayLabIniText adds -SaveHandoff ''|off|stage|scene|always (no line when '') and -TravelLock ''|on|off (writes 'travel_lock=0' only for off), both just before the world line. The default text stays byte-identical.
- Get-CoopHandoffText -Token -Pct -Role (pure).
- P2-19 Worker contract.
Start-WorkerSession and Invoke-WorkerScenario gain:
- -HostSaveSeed ''|real|lab and -GuestSaveSeed ''|real|lab ('' = -SaveSeed);
- -GuestSaveSeedRun <32-hex runId>, which copies build\worker-runtime\<id>\guest-saves\{287700,remote} read-only into the new run.
The LabSetting allow-lists (all three, identical) add save_handoff=(off|stage|scene|always) and travel_lock=[01].
New steps:
- 'titlemenu <host|guest|both> <s>': press Enter every 3 s only while state.json mission is 0 or 1;
- 'title <host|guest> [s]';
- 'waitlog <role> <regex> <s>';
- 'expectstate <role> <field> <regex>';
- 'waitexit <role> <s>';
- 'applyhandoff guest' (Import-CoopProgressFiles with -SaveDir run\guest-saves, -ReplacedRoot run\guest-saves-replaced and the stage named by run\guest-saves-handoff\request.json).
state.json adds "handoff":"<token>","handoff_pct":N,"travel_unlocked":0|1. New CTest names are added to Get-WorkerTests: save_handoff_wire_tests, save_handoff_core_tests, save_handoff_io_tests and world_handoff_lua_check.

## Tracks

### A: native pure core, shared state, build wiring
**Owns:** native/include/mgscoop/save_handoff_wire.hpp, native/save_handoff_wire.cpp, native/include/mgscoop/save_handoff_core.hpp, native/save_handoff_core.cpp, native/include/mgscoop/save_handoff_state.hpp, native/save_handoff_state.cpp, tests/save_handoff_wire_tests.cpp, tests/save_handoff_core_tests.cpp, tests/fixtures/handoff-set.json, CMakeLists.txt

**Depends on:** Phase 1 merged

**Changes:**
- A1 save_handoff_wire.hpp/.cpp (pure, no Windows headers, no allocation; pattern of native/include/mgscoop/partner_caps_wire.hpp:1-60):
- constants: kMagic 0x53, kVersion 1, kChannel 4, kChunk 1000, kMaxFiles 6, kMaxFileBytes 4 MiB, kMaxTotal 8 MiB;
- the P2-6 slot table: SlotEntry(slot) gives '287700/local/TPP_GAME_DATA0'; SlotPath(slot) gives the backslash form; IsProgressName(name) matches the Share-Save pattern;
- Encode/Decode for kinds 1..8 with exact P2-5 layouts; enum class Decode{ok, short_, magic, version, kind, size, bounds};
- SetSha256(entries, n, out32) per P2-6;
- RemoteBuildAtLeast(const char* build, maj, min, pat), which parses the 'X.Y.Z' prefix of a link remote_build.
Decode refuses: n=0 or n>6; unsorted or duplicate slots; slot>5; size 0 or >4 MiB; total>8 MiB or not equal to the sum; no slot 0 or 1; CHUNK len 0 or >1000; a nonzero reserved or pad byte; xfer 0 on any kind except STATE.
- A2 save_handoff_core.hpp/.cpp: class Core(Role).
- Deterministic, with a caller clock.
- Inputs: OnLua(Report{mission,title,story,saving,ingame,at}), OnContinue(target), OnDecline(reason), OnPeer(bytes,size), OnSession(n), OnPeerClose(), OnBuildOk(bool), OnSetHashed(set, entries, stable, unexpected, total), OnSnapshotRead(ok), OnStageDone(ok), OnPartnerMission(mission, terminating), Tick(now).
- Outputs, through an Actions ring: Send{kind, mode}, HashSet, Snapshot, StageWrite{slot, offset, len}, CommitStage, WriteManifest, WriteRequest{rejoin}, WriteProvenance, DeleteStage, PostExit, Toast{code, a, b}, Log, Publish{token, pct, lua_code}.
- Pure helpers: HostEligible, GuestNeed (P2-7), ExitGate (P2-10), Loc(mission), and a Provenance table (8 entries, LRU, Bind/Find).
- JSON format/parse, bounded and ASCII, rejecting unknown v: provenance.json, handoff.json, request.json and applied.json (P2-8, P2-9).
- Timers: STATE every 500 ms on change plus a 5 s heartbeat; host idle offer 30 s; guest chunk stall 30 s; host chunk stall 30 s (sending with no CHUNK queued: failed stall; review N2); host waits 60 s for RESULT; snapshot kept 300 s; countdown 10 s; exit_slow 20 s; Lua freshness 1500 ms (exit) and 3000 ms (phase); settle bind 30 s; at most 3 offers per set per session; at most 4 chunks per tick while ReliableQueued(4) < 16.
- There is no resume: OnSession drops the partial and keeps a verified stage. A new OFFER with the same set is answered DECLINE same when the stage still matches the guest's own set; otherwise the guest reuses the stage and sends RESULT staged without a WANT.
- A3 save_handoff_state.hpp/.cpp exactly as P2-14, in the steam_online_state.hpp seqlock style (native/include/mgscoop/steam_online_state.hpp:1-6). TokenName follows the P2-13 order.
- A4 CMakeLists.txt (single owner; the other tracks send their hunks here):
- add save_handoff_wire.cpp, save_handoff_core.cpp and save_handoff_state.cpp to mgs_coop_core (:129);
- add native/save_handoff.cpp (track B) to mgs_coop_lab (:441);
- register save_handoff_wire_tests and save_handoff_core_tests next to world_wire_tests (:172-181);
- add a WIN32 executable save_handoff_io_tests (tests/save_handoff_io_tests.cpp, track B) linking mgs_coop_lab and mgs_coop_core, next to link_udp_tests (:451-454);
- add HANDOFF after FOLLOW in the world chunk foreach (:351) for lua/coop/world_handoff.lua (track C).
MGS_GAME_LUA_DIR is never set to the main checkout.

**Tests:**
- tests/save_handoff_wire_tests.cpp:
- round trip for kinds 1..8; golden bytes for the STATE and CHUNK headers;
- every P2-5 refusal (one byte short or long, magic, version 2, unknown kind, a nonzero pad, n=7, duplicate slot, slot 6, size 0 or 4 MiB+1, a sum mismatch, no slot 0/1, len 0/1001);
- every encoded message is <= 1024 bytes;
- IsProgressName accepts TPP_GAME_DATA, TPP_GAME_DATA0..9, PERSONAL_DATA and PERSONAL_DATA0..9, and refuses TPP_CONFIG_DATA0, lower case and '..\\x';
- SetSha256 equals the tests/fixtures/handoff-set.json expected value;
- RemoteBuildAtLeast: '0.12.6+c07d0a2' vs 0.13.0 is false, '0.13.0+x' is true, garbage is false.
- tests/save_handoff_core_tests.cpp: two Cores joined by an in-memory ordered link that can drop the session, with fake files.
Transfer cases:
(1) host bound 30010, story 2, guest title 40050, real set different: OFFER, WANT, CHUNKs, staged files equal the host's bytes, request written exactly once after RESULT staged, PostExit only after 10 s at the title in scene mode, never in stage mode.
(2) Identical sets give no OFFER.
(3) Guest title 40010 with host bound 30010 gives need=0 (same_scene); with policy always it gives need=1.
(4) Guest story 1 gives need=1 even at the same location.
(5) Host story 1 or 0 gives elig story_opening/story_unknown and no OFFER.
(6) Host set unbound gives continue_unknown; after OnContinue(30010) plus a stable hash the host becomes eligible.
(7) A binding to 10020 gives continue_unsupported.
(8) A settle bind needs 30 s ingame in 30010 with saving=0 and an unchanged set; a set change or saving=1 inside the window restarts it; mission 40050 never settle-binds.
(9) Session reset at 40%: the partial is dropped, a new OFFER restarts from 0, there are no duplicate bytes, and the result hashes match.
(10) A corrupted chunk gives RESULT failed hash_mismatch, no request and toast G7; the host offers again at most 3 times.
(11) A disk error at StageWrite gives failed, DeleteStage and no request.
(12) Lua decline during receiving or the countdown gives ABORT user, the request deleted first, state kept and no PostExit.
(13) Title lost for more than 2 s during the countdown gives ABORT not_title.
(14) A Lua report older than 1500 ms blocks PostExit.
(15) A partner mission or terminating blocks PostExit.
(16) OnBuildOk(false) gives zero Send actions, STATE included.
(17) Pacing never exceeds 4 per tick or 16 queued; 'full' keeps the cursor.
(18) Unexpected files and too_large refuse.
(19) Provenance LRU of 8 and JSON round trip; unknown v is refused.
(20) Exactly one toast per transition.
- save_handoff_state: Publish/Get round trip; RequestKeepOwnSave is taken once; TokenName returns unique ASCII names of at most 18 chars.

### B: native transport, module glue, receipts, config, telemetry
**Owns:** native/include/mgscoop/link_protocol.hpp, native/include/mgscoop/partner_net.hpp, native/partner_net.cpp, native/include/mgscoop/save_handoff.hpp, native/save_handoff.cpp, native/include/mgscoop/save_redirect.hpp, native/save_redirect.cpp, native/include/mgscoop/world_receipt.hpp, native/world_receipt.cpp, native/include/mgscoop/world_wire.hpp, native/include/mgscoop/world_link.hpp, native/world_link.cpp, native/include/mgscoop/lab_config.hpp, native/lab_config.cpp, native/game_script_observer.cpp, native/worker_debug.cpp, tests/link_protocol_tests.cpp, tests/link_udp_tests.cpp, tests/world_receipt_tests.cpp, tests/lab_config_tests.cpp, tests/steam_shim_patches_tests.cpp, tests/worker_debug_tests.cpp, tests/save_handoff_io_tests.cpp, docs/save-handoff.md, docs/link-protocol.md, docs/world-sync-protocol.md

**Depends on:** A (core, state and CMake), U header declarations first: coop_hud.hpp EventKind::save_handoff and the LinkView fields, coop_reason_text.hpp FormatSaveHandoff, Phase 1 merged (world_link HandleNote, CFG r[9..11], K12 keys)

**Changes:**
- B1 link_protocol.hpp:287: kReliableChannels = 5, with the P2-4 comment. partner_net.hpp:218: channels 1..4. No other constant changes. Check native/steam_link_harness.hpp:59-64, which loops on the constant.
- B2 partner_net (P2-12):
- HandoffHooks: g_handoff sits next to g_world (:142).
- Call sites: on_session after the g_world.on_session calls (:854, :1177); on_peer_close after the CLOSE handling once the locks are released (around :877-884); on_idle after g_world.on_idle (:952).
- ReliableQueued, BuildGuestJoinCode and guest code retention: ArmSteamGuest (:1129-1181) stores the code only when g_config.steam_wait was set at Start (:1295); SteamOnShutdown zeroes it.
- PublishHandoffState atomics follow the PublishPartnerState pattern (:1663-1677).
- FormatStatus (:639-696) appends the P2-13 keys after the Phase-1 world block and before time_utc.
- Buffers grow to 1536 (:124, :702, :706).
- PublishHud (:553-581) copies handoff and handoff_pct into LinkView.
- B3 save_redirect.hpp/.cpp:
- add format_handoff_dir(save_dir, out, size) (P2-9 rules), format_local_path(save_dir, name, out, size) (traversal rules of format_remote_path, save_redirect.hpp:16-21) and bool current_save_dir(char*, size);
- add an SRW g_io_lock: exclusive inside RS_FileWrite and RS_FileDelete, shared inside RS_FileRead (save_redirect.cpp:129-160);
- add read_remote_snapshot(name, buf, cap, size&), which takes the lock shared, so a host read never makes the game's exclusive CREATE_ALWAYS write (:132) fail.
- B4 lab_config.hpp/.cpp:
- new members 'SaveHandoff save_handoff = off; bool save_handoff_given = false; std::uint8_t travel_lock = 0xFF /* absent */;'
- parse 'save_handoff' with ParseSaveHandoff and 'travel_lock' with ParseBool next to 'world' (:409);
- not a play refusal;
- effective travel_lock = given ? value : (mode==play).
- B5 world_receipt.hpp/.cpp:
- add Receipt::save and save_prefix "return 'MGS_COOP_WORLD_SAVE_V1' -- " to the enum (:27-33) and Classify (:56-67);
- add ParseSave(source, size, op, a, b, c, d, e), which takes exactly 6 integers with P2-2 ranges and follows ParseWarp (:121-133);
- FormatCfgReply appends ',%u,%u' for handoff and lock (P2-1);
- update the header grammar comment (:9-20).
world_wire.hpp CfgReply gains 'std::uint8_t handoff, lock;' after Phase-1's fields. Core::Cfg leaves them 0.
- B6 world_link.cpp:
- HandleCfg sets reply.handoff = save_handoff::LuaCode(now) and reply.lock = save_handoff::LockAllowed(), using P2-14 state and lab.ini travel_lock, after Core::Cfg.
- HandleNote: codes 1..12 are known (Phase-1 A14 listed 1..4).
- Code 1 (title story start) posts no toast when save_handoff::GuestHandoffActive() (guest role, mode scene or always, host STATE seen), because the handoff toasts replace it; the log line stays.
- B7 save_handoff.hpp/.cpp (mgs_coop_lab glue).
API: Configure(const Config&); Hooks(); HandleLua(src, size) (runtime thread, under receipt_lock; parses only, stores the report in a seqlock and replies); NoteMission(mission, terminating); LuaCode(now); LockAllowed(); GuestHandoffActive(); Shutdown().
on_idle (receive thread, no partner_net lock):
- feed OnBuildOk from GetLinkInfo().remote_build;
- DrainReliable(4) up to 32 messages into Core::OnPeer;
- take the F9 KeepOwnSave request;
- Tick;
- run Actions:
  - HashSet: one file per tick; CreateFileW with FILE_SHARE_READ|WRITE|DELETE on the local slots, read_remote_snapshot for remote; only when the Lua report is fresh with saving=0; re-hash only when size or mtime changed.
  - Snapshot: VirtualAlloc at most 8 MiB, read twice 500 ms apart, which must equal the bound set.
  - StageWrite: lab::WriteFileAtomic (lab_status.hpp:129-138).
  - CommitStage: MoveFileExW.
  - WriteManifest, WriteRequest and WriteProvenance with lab::WriteFileAtomic.
  - WriteRejoin: <game>\mgs-coop\link-rejoin.txt (one MGS2 code + CRLF), only when BuildGuestJoinCode succeeds; then SecureZero.
  - DeleteStage: known names only.
  - PostExit per P2-10.
  - Toast: coop_hud::Post(save_handoff, ...).
  - Publish: PublishHandoffState and save_handoff_state::Publish.
  - Log.
Configure (guest) deletes leftover *.partial folders and a request.json whose pid is not this process, then reads applied.json for toast G10.
- B8 game_script_observer.cpp:
- call ConfigureSaveHandoff(labc, directory) after ConfigureWorld (:4841) and before partner_net::Start (:4875); it calls partner_net::SetHandoffHooks only when it is configured;
- SelectWorldReceipt (:3366-3381): add 'else if(kind==world::Receipt::save){ if(OnRuntimeThread()) reply=mgscoop::save_handoff::HandleLua(buffer,size); }' and update the thread comment (:3349-3354);
- call save_handoff::NoteMission(allocation_mission,false) next to world_link::MissionRuntime (:3134-3135) and NoteMission(0,true) next to LocalMissionEnd (:3154-3155);
- PartnerShutdown (:1220): call save_handoff::Shutdown() before partner_net::Shutdown().
No camera-agent file is touched.
- B9 worker_debug.cpp (:410-413): add '"handoff":"%s","handoff_pct":%u,"travel_unlocked":%u,' from save_handoff_state::Get(), after Phase-1's world_pair_wait.
- B10 Docs:
- new docs/save-handoff.md: decision tables P2-7/P2-8, wire, files, states, exit, tooling contract, restore through Share-Save [R], and the fact checks from this spec's summary;
- docs/link-protocol.md: channel 4 (:345-357), plus the 7.2 keys and the 1536-byte buffers (it still says 768 at :470);
- docs/world-sync-protocol.md: the SAVE receipt, CFG r[12..13], NOTE 5..12, the travel lock and the replay hold, next to the Phase-1 W3 paragraph.

**Tests:**
- tests/link_protocol_tests.cpp:
- resize the {0,0,0,0} initialisers (:402-403);
- restrict the 'fill every channel' loops (:525-533, :596-604) to channels 1..3 so their packet and budget counts stay valid;
- new: with channels 1..4 all pending, channel 4 packs last and never ahead of a channel-1 first transmission; channel 5 gives bad_channel.
- tests/link_udp_tests.cpp (PartnerNetV2 :445-550):
- 608,012 bytes in 1000-byte chunks over SendReliable(4), paced by ReliableQueued, drained by the peer, bytes equal;
- a forced new session resets channel 4 (ReliableQueued(4)==0);
- HandoffHooks on_idle, on_session and on_peer_close fire;
- after PublishHandoffState(receiving, 42) the file contains '"handoff":"receiving","handoff_pct":42,"time_utc"';
- the :527 substring still matches;
- a worst-case document with all Phase-1 and Phase-2 keys at maximum length is under 1536 bytes.
- tests/world_receipt_tests.cpp:
- the CFG exact strings at :74-81 gain ',0,0' (Phase-1 strings plus 2 values);
- a case with handoff=3, lock=1;
- Classify SAVE_V1;
- ParseSave accepts '1 40050 1 2 0 0', '2 30010 1 2 0 0' and '3 1 0 0 0 0', and refuses 5 or 7 tokens, leading zeros, '-1', op 4, story 3, mission 100000 and a double space.
- tests/lab_config_tests.cpp:
- absent save_handoff gives off and travel_lock absent;
- off|stage|scene|always parse; 'auto' is invalid_values;
- travel_lock=0|1 parses; effective travel_lock defaults to 1 in play and 0 in lab;
- neither key sets a play_refused bit.
- tests/steam_shim_patches_tests.cpp (next to :100-106): format_local_path traversal refusals; format_handoff_dir trims trailing separators and refuses a too-long save_dir; read_remote_snapshot under a concurrent RS_FileWrite never returns torn bytes (two threads, 1000 iterations).
- tests/save_handoff_io_tests.cpp (new, Windows, temp dir under build\):
- glue file actions against fake save trees: hash of 6 fixture files equals the fixture set;
- an extra TPP_GAME_DATA2 gives unexpected_files;
- a file changed between the two snapshot reads is refused;
- stage writes go .partial, then rename, then handoff.json; request.json is written last and only in scene mode;
- a half-written .partial is never treated as ready, and leftovers are deleted by known names only (a foreign file survives);
- provenance.json is written atomically and reloaded;
- PostExit uses an injected window enumerator: the largest visible unowned window gets WM_CLOSE; none gives no_window.
- tests/worker_debug_tests.cpp: state.json carries the handoff keys and stays valid JSON.

### U: native presentation (HUD, coop_text, F9)
**Owns:** native/include/mgscoop/coop_hud.hpp, native/coop_hud.cpp, native/include/mgscoop/coop_reason_text.hpp, native/coop_reason_text.cpp, native/debug_menu_ui.cpp, tests/steam_transport_tests.cpp, tests/debug_overlay_tests.cpp, docs/debug-menu.md

**Depends on:** A (save_handoff_state.hpp), Phase 1 merged (coop_text FormatMission and FormatWorldNote, the EventKinds world_note and join_ignored)

**Changes:**
- U1 (land first; B builds against it):
- coop_hud.hpp: append EventKind save_handoff after join_ignored (:121-135) and append 'std::uint8_t handoff{}, handoff_pct{};' to LinkView after Phase-1's fields (:37-54).
- coop_reason_text.hpp: declare FormatSaveHandoff (P2-15).
- U2 coop_hud.cpp:
- FormatToast (:436-491): case save_handoff strictly parses '<a> <b>' and calls FormatSaveHandoff. Tones: G1/G2/G10 info, G9 warn, G5/G6/G7 warn, H1/H2/H3 info, H4/H6/H7 warn.
- FormatLine connected (:375-382): handoff receiving gives 'CO-OP [connected] %.24s - %s %u ms | getting host save %u%%'; sending gives '... | sending your save %u%%'; staged, countdown or exiting gives 'CO-OP [connected] %.24s | host save ready - MGSV restarts'.
- listening or lost with handoff==partner_restarting (:384-414), with or without v.left (UDP sends no goodbye; review N3): 'CO-OP [%.24s restarting] loading your save - waiting for them'. The host's lost toast is suppressed while partner_restarting (H2 already said so).
- Defaults keep every existing string.
- U3 coop_reason_text.cpp: FormatSaveHandoff sentences for codes G1..G10 and H1..H7, plus FormatWorldNote sentences for codes 5..12 (texts in user_visible_texts). Mission names come from FormatMission (Phase-1 K11). Every sentence is <= 199 chars and ASCII.
- U4 debug_menu_ui.cpp, Partner / Co-op page. Right after the Online section (:665-677), add a 'Shared save' section shown when save_handoff_state::Get().available:
- one or two status lines from the token, with the F9 texts in user_visible_texts;
- button 'Keep my own save' (enabled while can_keep: wait_host, receiving, staged or countdown), which calls RequestKeepOwnSave() and SetLastUiAction("handoff_keep");
- checkbox 'Travel lock (you follow the host's travel)' bound to !TravelUnlocked(); unchecking calls SetTravelUnlocked(true) and SetLastUiAction("travel_unlock").
These rows are UI-only (no command), so the debug_actions.cpp:196 'gameplay' gate never applies and they work at the title and in the helicopter. debug_overlay.cpp is not edited.

**Tests:**
- tests/steam_transport_tests.cpp:
- the existing FormatLine strings (:2122-2233) and Phase-1's are unchanged with default LinkView;
- new exact lines for guest receiving 42%, host sending 7%, guest countdown, and host lost + left + partner_restarting;
- FormatToast for every save_handoff code is <= 200 chars with no leftover '%' and names the mission;
- FormatWorldNote 5..12 give the exact sentences;
- code 13 returns false.
- tests/debug_overlay_tests.cpp:
- the Shared save section renders for each token;
- 'Keep my own save' is disabled when !can_keep and raises RequestKeepOwnSave exactly once;
- the Travel lock checkbox toggles TravelUnlocked;
- the section is hidden when !available.

### C: Lua (world_handoff.lua: save report, travel lock, warnings, replay hold)
**Owns:** lua/coop/world_handoff.lua, lua/coop/world_follow.lua, native/world_sync_source.hpp.in, native/bootstrap_patch.cpp, cmake/lua_checks.cmake, tests/world_handoff_lua_check.lua, tests/world_follow_lua_check.lua, tests/world_follow_game_lua_check.lua, tests/world_sync_lua_check.lua

**Depends on:** B (SAVE_V1 grammar, CFG r[12..13]), Phase 1 merged (W.peer, W.note, F.tick rewrite)

**Changes:**
- C1 New chunk lua/coop/world_handoff.lua (target <= 16,000 bytes LF; the hard cap is 32,000), wrapped in 'do ... end' like world_follow.lua.
Top: local W = e.MgsCoopWorld; local F = W.follow; local X = {esc = {}}; W.handoff = X; plus a SUPPORTED copy of world_follow.lua:7.
Helpers, all read through pcall or W.get:
- title(): W.get(gvars,'ini_isTitleMode')==true and floor(W.mission/10000)==4;
- story(): 0 when str_storySequence or any of CLEARD_ESCAPE_THE_HOSPITAL, CLEARD_RECUE_MILLER, CLEARD_TO_MATHER_BASE is not a number or the three are not strictly increasing; otherwise 2 when seq >= CLEARD_TO_MATHER_BASE, else 1 (guarded-read pattern of lua/coop/character.lua:104-107);
- saving(): TppScriptVars.IsSavingOrLoading()==true (build/player-allocation-lifecycle-20260909/package-00.lua:5);
- ingame(): W.active and not title() and W.inGame();
- code(): r = W.cfg(vars.missionCode, 0); returns r[12] or 0, r[13] or 0;
- X.save(op,a,b,c,d,e): uses W.n checks and sends nothing when a value is invalid.
- C2 X.tick(now) is chained in front of F.tick: 'local prev = F.tick; F.tick = function(now) X.tick(now); return prev(now) end' (W.tick calls W.follow.tick dynamically, world_sync.lua:246). It:
- sends REPORT op 1 on change or every 1000 ms;
- expires X.esc entries;
- runs the sortie hold: release on a mission change (drop), on W.paired or F.queue non-empty (drop, log decision=follow), on TravelUnlocked (r[13]==0: run the stored original, note 8 b=2), after 120000 ms (run the original, note 8 b=3); re-post note 12 every 3 s while held. As implemented (review lua-flow-2/3): dropped first when the live gvars no longer name the held target (the prep screen cancelled or changed; decision=stale), and released when world sync is inactive (W.fail, LEVEL 0: world_sync's inactive OnUpdate calls X.hold while W.isStarted; decision=inactive, note 8 b=2).
- C3 Lock install through F.installHooks (world_follow.lua:244-247; idempotent through X.installed). Inner wrappers are installed BEFORE world_follow wraps, so world_follow's guest() armed suppression stays the outer layer and F.originalDeploy becomes X's wrapper.
Wrapped TppMission functions: ReserveMissionClear, AbortMission, AbortMissionByMenu, ContinueFromCheckPoint, ExecuteContinueFromCheckPoint, OnEndMissionPreparation; plus e.ReservePlayerLoadingPosition.
Review lua-flow-1 (as implemented): the game's title Continue never calls TppMission's Continue functions (title_sequence Seq_Game_TitleMenu.OnSelectContinue -> CheckDlc/ChunkInstalled -> FadeOut -> OnEndFadeOutSelectContinue: VarRestore, ClearTitleMode, ReservePlayerLoadingPosition, Load). So every 4xxxx mission init (F.installHooks, before TppSequence.Init captures self.OnSelectContinue) wraps the shared Seq_Game_TitleMenu.OnSelectContinue and OnSelectRestartHeli from mvars.seq_sequenceTable. At the select (title mode, before any fade or restore): an armed guest (world_follow's guest()) is suppressed (F.request(4); X.decline for a pending handoff); otherwise the title rules below run with target gvars.title_nextMissionCode; a RestartHeli or a Continue into a helicopter-space/10115/50050 next mission does not travel (only the handoff rules). A refused select reopens the menu as the vanilla cancel path does (TppUiCommand.RebootTitleSelectMenu, title_sequence.StartTitleMenu). The kind-4 TppMission wrappers pass through at a title. op 2's title bit is latched at the select (ClearTitleMode runs before the reserve). R6 confirms the menu takes the second press.
Each wrapper passes straight through when F.token is set, when W.host, or when X.decide returns allow.
X.decide(fn, kind, target, now) is pure and returns 'allow' when any of these holds:
- not F.enabled or not W.active or W.level < 3;
- F.session()==nil (world_follow.lua:151-155);
- r[13]~=1;
- p = W.peer() is nil, not linked, or p.level < 3;
- m = p.mission is 0 or 1, or not SUPPORTED[m];
- F.current or #F.queue > 0;
- target is nil or 0, or equals m, or is 4xxxx, or equals W.mission;
- an escape for (fn, target) passed within the last 2000 ms (one press can hit two wrappers).
Title Continue (kind 4 while title()):
- r[12] in 2..4: refuse, note 9 (checked before r[13], after the escape: the veto works with the travel lock off; review SS-2);
- r[12]==1: refuse, note 7 b=3;
- story()==1: note 7 b=2;
- SUPPORTED[W.mission] ~= SUPPORTED[m]: note 7 b=1;
- otherwise allow.
OnEndMissionPreparation: target = gvars.mis_nextMissionCodeForMissionClear, or the emergency code when it is nonzero; returns 'hold'.
ReserveMissionClear and AbortMission with args.nextMissionId: return 'refuse'.
- C4 Refuse, hold and escape:
- Refuse: W.note(code, a, b); log decision=refuse; return false for ReserveMissionClear (as world_follow.lua:259 does), nil otherwise.
- Repeat within 1000..10000 ms for title Continue, or 1000..30000 ms for ReserveMissionClear/AbortMission: allow once, set X.esc[fn..target], note 8 b=1, log decision=repeat. A title-Continue repeat while r[12] is 1..4 also sends X.save(3,1,0,0,0,0) and note 10 first.
- Hold (sortie): X.held = {fn=original, args, n, target, at, mission}; note 12; log decision=hold. A second OnEndMissionPreparation at least 1000 ms later releases it with the NEW args (note 8 b=1) unless the new target is allowed (then run it with no note).
- C5 e.ReservePlayerLoadingPosition inner wrapper. k is computed as world_follow.lua:297-300 does. Then:
- k==4: X.save(2, vars.missionCode, title() and 1 or 0, story(), 0, 0).
- Host warning: W.host and k>0 and p linked and not SUPPORTED[vars.missionCode] gives W.note(6, vars.missionCode, p.mission) and a warn log; rate limit once per target per mission.
- Guest warning: not W.host and k==4 and the lock applies and X.decide would refuse vars.missionCode gives W.note(11, vars.missionCode, m); warn only.
Always return the original.
- C6 Replay hold: X.replayHold() = not W.host and title() and r[12] in 1..4.
world_follow.lua edit (the only one): in F.tick's guest replay section, right after the 120 s queue-timeout block and before the 'if W.mission ~= t[3]' line, add 'if F.replayHold and F.replayHold() then return end'. The timeout still refuses the TRANSITION with FOLLOW_ACK 0 after 120 s.
C1 sets F.replayHold = X.replayHold and logs step=replay_hold once per change.
- C7 Splice:
- native/world_sync_source.hpp.in adds world_handoff_source;
- native/bootstrap_patch.cpp adds it to the size sum (:166-167) and inserts it right after world_follow_source in the splice list (:240);
- cmake/lua_checks.cmake inserts world_handoff.lua after world_follow.lua in the composed list (:269) and registers mgs_add_lua_check(world_handoff_lua_check "${_mgs_coop}/world_handoff.lua" "${_mgs_coop}/world_follow.lua" "${_mgs_coop}/world_follow_seed.lua") next to :124.
The CMake foreach entry belongs to A4.

**Tests:**
- tests/world_handoff_lua_check.lua (new). Fixture modelled on tests/world_follow_lua_check.lua:8-69: W.cfg returns 13 values from c.r12 and c.r13; W.peer returns c.peer; W.note and the SAVE receipt are recorded; TppDefine.STORY_SEQUENCE is {100,110,120}.
Cases:
- REPORT on change and at 1000 ms;
- story() classes, including missing and unordered constants giving 0;
- the X.decide table: no link, r13=0, peer nil, level 2, mission 0, 1 or 10020, F.current, queue, ACC target, target==host, target==W.mission;
- title Continue: location refuse (note 7 b=1), story refuse (b=2), waiting refuse (b=3), busy (note 9); a repeat at +2 s allows and sends SAVE op 3 when pending; +0.5 s and +11 s refuse;
- ReserveMissionClear{10036} returns false with note 5; a repeat at +5 s allows; +31 s refuses;
- sortie: hold with stored args; a second press at +1 s runs the original once with the new args; an allowed new target runs at once; r13 dropping to 0 releases (note 8 b=2); 120 s releases (b=3); W.paired drops the hold with no original call;
- F.token passes through; W.host passes through;
- host warning into 10020 when linked, once per target, never unlinked;
- guest landing warning;
- replayHold true only at the title with r12 1..4;
- the chunk is <= 16,000 bytes.
- tests/world_follow_lua_check.lua: all Phase-1 cases pass unchanged with F.replayHold nil. New: F.replayHold returning true keeps a queued TRANSITION, the 120 s timeout still sends FOLLOW_ACK 0, and false resumes replay.
- tests/world_follow_game_lua_check.lua (extracted TppMission, TppMain, TppPlayer), with world_handoff composed in:
- the title-Continue follow (:178-205) and the level0/inactive Continue (:285-299) stay green;
- new: an unarmed linked guest's real ExecuteContinueFromCheckPoint at a 40050 title with the host in 30010 is refused before any vanilla side effect (no VarRestore), and the repeat at +2 s runs vanilla.
- tests/world_sync_lua_check.lua: the loadstring stub (:87-136) accepts MGS_COOP_WORLD_SAVE_V1 with the strict grammar and records it, and still errors on other unknown receipts. The composed byte-identical splice (:1442-1531) includes world_handoff.lua after world_follow.lua.

### D: PowerShell tooling (apply, restart, cleanup guard, rejoin, Share-Save, guide)
**Owns:** tools/CoopPlay.psm1, tools/friend/FriendCommon.psm1, tools/friend/Play-Coop.ps1, tools/friend/Share-Save.ps1, tools/friend/Install-Coop.ps1, tools/friend/Collect-Logs.ps1, tools/friend/Uninstall-Coop.ps1, tools/friend/FRIEND-GUIDE.txt, tests/friend_package_tests.ps1, docs/two-pc-play.md

**Depends on:** B (file formats P2-9, lab.ini keys P2-11, link-status keys P2-13), Phase 1 merged (K18 marker, Get-FriendImportedSaveInfo, K19 listing)

**Changes:**
- D1 CoopPlay.psm1:
- Get-InstancePaths (:41-70) adds CoopSavesHandoff (mgs-coop\coop-saves-handoff), CoopSavesReplaced and LinkRejoin (mgs-coop\link-rejoin.txt).
- New-CoopPlayLabIniText (:767-848) adds -SaveHandoff and -TravelLock per P2-18; the default output is byte-identical.
- Add Get-CoopHandoffText.
- Format-CoopStatusLine (:1080-1164): when handoff is present and not off, idle or same, add one segment before the mission text (texts in user_visible_texts). While the token is wait_host, receiving, staged, countdown or exiting, leave out the Phase-1 Share-Save story sentence. A document without the keys gives the same strings as Phase 1.
- D2 FriendCommon.psm1:
- Move Share-Save's validation and import core (Share-Save.ps1:155-166, 192-252) into Import-CoopProgressFiles and Test-CoopProgressStage (P2-18) with the same behaviour: pattern, 4 MB per file, sha256 against the manifest, TPP_GAME_DATA0/1 required, settings kept, old progress files dropped, aside, rollback, staging removed. Write the Phase-1 K18 marker into the new copy with the given source.
- Add Get-CoopProgressBackups (both suffixes), Get-CoopHandoffSetSha256 (P2-6) and Get-CoopHandoffRequest (format and v; pid and start equal to the exited game; created_utc after session.started_utc and under 30 min old; Assert-NoReparsePoint; the staged dir exists; Test-CoopProgressStage against handoff.json; the recomputed set equals set_sha256).
- Add Remove-CoopStaleHandoff (removes *.partial, request.json and staged dirs whose session is not live; keeps provenance.json and applied.json).
- Add the Invoke-CoopPlayCleanup guard and -Force (P2-17).
- $script:FixedPathSuffixes (:79-90) gains '\mgs-coop\coop-saves-handoff\00000000.partial\287700\local\TPP_GAME_DATA0.mgs-coop-00000000000000000000000000000000.tmp' and '\mgs-coop\coop-saves-replaced\00000000-000000-99-before-handoff\287700\local\TPP_GRAPHICS_CONFIG'.
- D3 Invoke-CoopHandoffAfterExit (P2-18). Under Use-CoopPlayMutex:
- Session not active or role not guest: none.
- No valid request: none (an invalid one is deleted: stale).
- handoff.count >= 1: delete it, state limit, return limit.
- handoff.state applying or relaunching with a live claimant other than $PID: claimed_by_other.
- Owner watchdog with -LauncherAlive and the request under 20 s old: deferred.
- Claim: state applying, claimed_by_pid=$PID, claimed_start_utc; save the session.
- Import-CoopProgressFiles -Suffix '-before-handoff' with marker source 'handoff'.
- Write applied.json.
- Rewrite lab.ini with New-CoopPlayLabIniText using the session's recorded parameters plus -SaveHandoff off. If link-rejoin.txt exists and the old lab.ini had net_steam_wait=1: parse it with ConvertFrom-CoopSteamJoinCode (CoopPlay.psm1:578), use -SteamPeer <host> instead of -SteamWait, and write link-secret.txt.
- Record every new file sha256 in session.files before the atomic write, as Play-Coop.ps1:575-580 does; delete link-rejoin.txt, request.json and the staged dir.
- state relaunching, count 1; save.
Outside the mutex:
- wait until no game process remains in the folder (at most 30 s) plus 5 s;
- Start-Process 'steam://rungameid/287700' unless MGS_COOP_TEST_NO_LAUNCH=1;
- return relaunched.
Any failure: state failed with a message, no relaunch, return failed (the caller then cleans up).
- D4 Play-Coop.ps1:
- New params '[ValidateSet('scene','always','off')][string]$SaveHandoff='scene'' and '[ValidateSet('on','off')][string]$TravelLock='on''. Append '-SaveHandoff $SaveHandoff -TravelLock $TravelLock' to both New-CoopPlayLabIniText calls (:546, :548) after '-Partner $Partner', so the substring asserted at tests :644-648 is kept.
- Record the lab.ini parameters in play-session.json, for D3.
- Preflight: Remove-CoopStaleHandoff.
- When the game is seen (:612), record game_pid and game_start_utc.
- Wrap launch, evidence and status (:597-663) in 'for ($run = 0; $run -lt 2; $run++)'. When the status loop ends because the pid died, call Invoke-CoopHandoffAfterExit -Owner console.
  - relaunched or claimed_by_other: print the console lines, reset the log offsets (:601-604), skip Start-Process on run 1, wait for the new game (retry Start-Process once at 60 s, print the manual line at 120 s, give up at 300 s and clean up with -Force), set handoff.state relaunched, and continue.
  - Anything else: fall through to the finally block (:665-690).
- The status loop prints one-time lines from Get-CoopHandoffText when the token changes.
- SupportedLoaderVersions (:94) gets the Phase-2 loader version.
- D5 Play-Coop.ps1 watchdog (:131-153):
- remember the last game pid and start;
- on 'elseif ($seen)', call Invoke-CoopHandoffAfterExit -Owner watchdog -LauncherAlive:(Test-CoopProcessAlive launcher):
  - deferred: sleep 2 s and loop;
  - relaunched or claimed_by_other: $seen=$false and $restartUntil = now + 180 s (MGS_COOP_TEST_RESTART_WAIT_SEC overrides), then loop;
  - otherwise: break to cleanup;
- the launcher-gone branch (:142-149) does not break while handoff.state is applying or relaunching and $restartUntil has not passed;
- at the deadline: Invoke-CoopPlayCleanup -Force.
- D6 Share-Save.ps1:
- Import uses Import-CoopProgressFiles (source share-save);
- Restore uses Get-CoopProgressBackups and names the kind ('...kept before the automatic save handoff' or '...before the import');
- menu text '[R] Restore the co-op copy the last import or automatic save handoff replaced';
- the live-session and game-running refusals (:85-93) stay.
- D7 Other tools:
- Install-Coop.ps1: when the Phase-1 guard reports source handoff or a '-before-handoff' folder, the prompt names "your partner's save (automatic save handoff on <date>)"; same double confirmation.
- Collect-Logs.ps1: the K19 listing adds the coop-saves-handoff/ rows; the allow-list reads coop-saves-handoff\provenance.json, applied.json and request.json (<= 64 KB, no secrets); link-rejoin.txt is never read and is added to the header list of files never collected (:7-18); summary gains 'handoff=<play-session handoff.state|none>; asides_handoff=<n>'.
- Uninstall-Coop.ps1 removes link-rejoin.txt and coop-saves-handoff\*.partial, request.json and staged dirs (keeping provenance.json), and never touches coop-saves-replaced.
- D8 FRIEND-GUIDE.txt (ASCII, CRLF, no renumbering, no 'SteamID64'):
- inside section 3, a subsection 'SAME PROGRESS AUTOMATICALLY (save handoff)' placed after Phase-1's 'SAME PROGRESS FOR BOTH';
- a TRAVEL LOCK paragraph in EXPERIMENTAL SHARED WORLD;
- section 6 bullets for the new messages;
- section 7: Share-Save [R] also undoes an automatic handoff;
- the options -SaveHandoff off and -TravelLock off.
docs/two-pc-play.md gets the same flow and the new HUD and status lines.

**Tests:**
- tests/friend_package_tests.ps1, unit:
- New-CoopPlayLabIniText default text is byte-identical; -SaveHandoff scene adds exactly 'save_handoff=scene' before the world line; -TravelLock off adds 'travel_lock=0'; lab_ini_check (-IniCheck) accepts both;
- Get-CoopHandoffSetSha256 equals tests/fixtures/handoff-set.json;
- Get-CoopHandoffText and Format-CoopStatusLine for each token; documents without the keys keep the Phase-1 strings (:467, :516, :524, :532, :539, :1594).
- Import core in installed fixture games (after :922-1011):
- settings are kept and progress is replaced;
- an extra old PERSONAL_DATA is dropped;
- the aside folder is named <stamp>-before-handoff and holds the old tree;
- the marker has source=handoff;
- rollback works when the final Move-Item is blocked (a handle is held on the target);
- staging is removed;
- Test-CoopProgressStage rejects bad names, oversize files, hash and set mismatch, a missing TPP_GAME_DATA0/1 and reparse points;
- Share-Save Export, Import and Restore round trip with -NonInteractive; [R] picks the newest of -before-import and -before-handoff.
- Invoke-CoopHandoffAfterExit with MGS_COOP_TEST_NO_LAUNCH=1:
- a valid request is applied; lab.ini is rewritten with save_handoff=off and its new sha256 recorded; the session stays active with handoff.count=1;
- with a steam-wait lab.ini plus link-rejoin.txt: link-secret.txt and net_peer_steam are written, both hashes recorded, and link-rejoin.txt deleted;
- count=1 gives limit; a pid or start mismatch gives stale (deleted); a live claimant gives claimed_by_other; a watchdog with the launcher alive gives deferred;
- Invoke-CoopPlayCleanup during relaunching does nothing; with -Force, or once the claimant is dead, it cleans up every play file.
- Full launcher harness (Invoke-PlayLaunch :1531-1600):
- the stand-in game writes the staged set, handoff.json and request.json with its own pid and start, then exits;
- Play-Coop applies and prints the restart lines with no cleanup in between;
- the test starts a second stand-in; Play-Coop attaches to it; at the second exit 'MGSV exited.' is printed and cleanup removes all 4 play files;
- the -Watch variant with the console killed: the watchdog applies, waits for the second stand-in and cleans up only after it exits; with the console alive the watchdog defers.
- Other:
- the Install-Coop guard detects '-before-handoff' (next to :1177-1188);
- Collect-Logs lists coop-saves-handoff and never includes link-rejoin.txt content (the leak check :1624-1627 stays);
- guide needles add 'SAME PROGRESS AUTOMATICALLY', '-SaveHandoff off' and 'TRAVEL LOCK';
- the package lists (:735-749, :788-791) are unchanged because no tools/friend file is added;
- the ASCII, CRLF and 5.1 parse checks (:859-870) pass.

### E: hidden worker (per-role seeds, non-gameplay steps, exit and apply, scenarios)
**Owns:** tools/worker/Start-WorkerSession.ps1, tools/worker/Invoke-WorkerScenario.ps1, tools/worker/Invoke-WorkerChecks.ps1, tools/worker/Stop-WorkerSession.ps1, tools/worker/README.md, tools/worker/scenarios/handoff-stage.txt, tools/worker/scenarios/handoff-restart.txt, tools/worker/scenarios/handoff-after-restart.txt, tools/worker/scenarios/travel-lock.txt, tests/worker_session_tests.ps1, tests/worker_loading_tests.ps1

**Depends on:** A, B, C (runtime behaviour, state.json keys, CTest names), D (Import-CoopProgressFiles for applyhandoff)

**Changes:**
- E1 Start-WorkerSession.ps1:
- next to $SaveSeed (:37), add '[ValidateSet('','real','lab')][string]$HostSaveSeed=''', the same for $GuestSaveSeed, and '[ValidatePattern('^$|^[0-9a-f]{32}$')][string]$GuestSaveSeedRun=''' (exclusive with GuestSaveSeed);
- in the per-role loop (:317-325) choose the role's seed, reusing the two existing branches unchanged;
- 'run' copies <repo>\build\worker-runtime\<id>\guest-saves\{287700,remote} after Test-WorkerRunId and Assert-WorkerPlainPath -MustExist (Stop-WorkerSession.ps1:16-19 pattern), and requires a TPP_GAME_DATA file;
- record row.save_seed_kind and save_seed_run;
- the LabSetting pattern (:45) adds save_handoff and travel_lock (P2-19).
- E2 Invoke-WorkerScenario.ps1:
- pass the new params through (:107, :231);
- the LabSetting pattern (:111) and labsetting step regex (:156) match Start's list;
- new steps (validator :182-216, dispatch :800-897): titlemenu (restrict the :811-821 loop and stop at 40010/40020/40050), single-role title, waitlog (the tail reader of Invoke-ScenarioWorldTravel :253-286, starting at session.json roles.<role>.log_offsets), expectstate (Read-RoleRaw :308-310), waitexit (the owned lease pid is gone; record forced=false; later steps for that role fail fast), applyhandoff guest (Import-Module tools/friend/FriendCommon.psm1; read run\guest-saves-handoff\request.json; Import-CoopProgressFiles with run paths; record the before and after progress hashes in scenario.json);
- results go into $expectations / $expectFailures (:288-297);
- all writes stay under the run directory.
- E3 Stop-WorkerSession.ps1 and the coordinator tolerate a role that exited after waitexit: no WM_CLOSE for it, but the baseline loader is still restored and real saves compared (:48-91, :135-152).
- E4 Scenarios (each header gives the run command and the evidence):
- handoff-stage.txt: titlemenu both 240; title host 240; waitlog host 'save_handoff=provenance; .*mission=30010' 120; waitlog host 'save_handoff=offer; ' 60; waitlog guest 'save_handoff=staged; ' 90; expectstate guest mission ^40050$; expectstate guest handoff ^staged$.
- handoff-restart.txt: the same steps, then waitlog guest 'save_handoff=exit; how=wm_close' 60; waitexit guest 90; applyhandoff guest.
- handoff-after-restart.txt: titlemenu both 240; waitlog guest 'save_handoff=need; need=0' 120; waitlog guest 'world=paired; epoch=[0-9]+; mission=40010' 120; title host 240; waitmission host 30010 300; waitmission guest 30010 300.
- travel-lock.txt: titlemenu both 240; title host 240; waitmission host 30010 300; guest press enter; waitlog guest 'world_lua=event=handoff; step=lock; .*decision=refuse' 20; wait 4000; expectstate guest mission ^40050$; guest press enter; waitlog guest 'decision=repeat' 20; waitmission guest 30050 300.
- E5 Invoke-WorkerChecks.ps1 Get-WorkerTests (:47-87): add save_handoff_wire_tests, save_handoff_core_tests, save_handoff_io_tests (Windows) and world_handoff_lua_check. This lands together with A, B and C, because a listed but unregistered test throws. README.md documents the new parameters, the steps and the two-session restart proof.
- E6 Not in v1: a supervisor 'relaunch guest' control verb (worker_supervisor.cpp:405-414, :509-519). It stays an open question.

**Tests:**
- tests/worker_session_tests.ps1:
- update the seed ValidateSet and form assertions (:339-345) for the new params;
- the three LabSetting allow-lists stay identical (:128-162, :231-259, :457-522);
- the validator (:398-420) accepts 'titlemenu both 240', 'title guest 200', 'waitlog host ^save_handoff= 30', 'expectstate guest handoff ^staged$', 'waitexit guest 90' and 'applyhandoff guest';
- the validator refuses 'title both', 'titlemenu guest 1', 'waitexit both 60', 'applyhandoff host' and 'expectstate guest secret x';
- every shipped scenario validates (:403-410);
- the Start stub (:1079-1083) has the new params.
- tests/worker_loading_tests.ps1 (:11-110): update the extracted title and waitmission helpers if their shape changes.

## user_visible_texts
- GUEST TOASTS (save_handoff codes):
- G1: "Getting the host's save (<kb> KB) so you both start in the same place. Your own co-op save is kept (Share-Save.bat [R] brings it back)."
- G2 (repeated every second): "Host's save ready. MGSV restarts in <n> s and Play-Coop starts it again on the host's save. Press Continue twice to keep your own save."
- G3 (stage mode): "Host's save stored for a test; it is not applied."
- G4: "Your save starts somewhere other than the host's. Once the host is in Mother Base or free roam, their save is copied to you and MGSV restarts once."
- G5: "The host's save is still in the story opening, so it is not shared. The player whose save reaches Mother Base should host."
- G6: "The host's save continues into <mission>, which co-op cannot join. The host should go to Mother Base or free roam first."
- G7: "Getting the host's save failed (<why>). Nothing was changed."
- G8: "You kept your own save for this session."
- G9: "Host's save ready: quit MGSV (title menu > Exit) and Play-Coop starts it again on the host's save."
- G10: "You now play on the host's save. Share-Save.bat [R] brings your previous co-op save back."
- HOST TOASTS:
- H1: "Sending your save to <partner> so you both start in the same place."
- H2: "<partner> is restarting MGSV on your save (about a minute). Wait here; they arrive in <mission>."
- H3: "<partner>'s save starts somewhere else. Go to Mother Base or free roam: your save is then copied to them."
- H4: "Your save is still in the story opening, so it is not shared. Let <partner> host if their save reaches Mother Base."
- H5: "<partner> kept their own save."
- H6: "Sending your save failed (<why>). Use Share-Save.bat instead."
- H7: "Your save continues into <mission>, which co-op cannot join. Go to Mother Base or free roam first."
- NOTE TOASTS (Lua codes 5..12):
- 5: "The host is in <host mission>. Go there or to a helicopter, or do the same again within 30 s to travel alone."
- 6: "Your partner cannot join <mission> (no co-op there). Meet in Mother Base, free roam or the helicopter."
- 7 b=1: "The host is in <host mission>, but your save continues elsewhere. Press Continue again within 10 s to play alone."
- 7 b=2: "Your save is at the story start; the host cannot join it. Press Continue again within 10 s to play it anyway."
- 7 b=3: "The host's save is copied to you once they are in Mother Base or free roam. Press Continue again within 10 s to play your own save."
- 8: "Travelling alone. The host leads co-op travel again once you are in the same place."
- 9: "Getting the host's save (<pct>%); MGSV restarts on it. Press Continue again within 10 s to keep your own save."
- 10: "Keeping your own save for this session."
- 11: "You are loading <mission>; the host is in <host mission>. Meet in a helicopter, Mother Base or free roam."
- 12: "The host is in <host mission>. Choose that destination, or choose this one again to fly alone (it leaves alone after 2 min)."
- HUD status lines:
- "CO-OP [connected] <name> - <route> <rtt> ms | getting host save <pct>%"
- "CO-OP [connected] <name> - <route> <rtt> ms | sending your save <pct>%"
- "CO-OP [connected] <name> | host save ready - MGSV restarts"
- "CO-OP [<name> restarting] loading your save - waiting for them"
- F9 Partner / Co-op > Shared save:
- Status lines:
  - off: "Automatic save sharing is off (Play-Coop -SaveHandoff off)."
  - idle/same: "Your save matches the host's place; nothing to share."
  - wait_host: "Waiting for the host to reach Mother Base or free roam; their save is then copied to you."
  - receiving: "Receiving the host's save: <pct>% of <kb> KB."
  - countdown: "Host's save ready: MGSV restarts in <n> s. Your current co-op save is kept aside (Share-Save.bat [R])."
  - kept: "You kept your own save for this session."
  - refused: "The host's save cannot be shared: <reason>."
  - failed: "Getting the host's save failed: <reason>. Nothing was changed."
  - host lines: "Sending your save: <pct>%" / "Your partner is restarting on your save."
- Button: "Keep my own save".
- Checkbox: "Travel lock (you follow the host's travel)".
- PLAY-COOP status segments:
- guest: " | getting the host's save <pct>%", " | host save ready, MGSV restarts", " | waiting for the host's save";
- host: " | sending your save to your friend <pct>%", " | your friend is restarting on your save";
- both: " | save not shared: <reason>".
- PLAY-COOP console:
- "MGSV closed to load the host's save."
- "Applying the host's save (<n> files, <kb> KB)..."
- "Your previous co-op save is kept at mgs-coop\coop-saves-replaced\<stamp>-before-handoff (Share-Save.bat [R] brings it back)."
- "Starting MGSV again with the same join code..."
- "MGSV restarted (process <pid>)."
- "The background helper is restarting MGSV with the host's save..."
- "Could not apply the host's save: <reason>. Your co-op save is unchanged."
- "A host save from an earlier session was not applied and was removed."
- "Steam did not start MGSV again within 60 s: start MGS V from Steam yourself; co-op stays on for <n> more seconds." (only after the console's own relaunch check passed with at least 90 s left; review SS-4)
- "Co-op was switched off while MGSV was restarting (the helper could not start it again); do not start MGS V from Steam now. Start Play-Coop.bat again." / "MGSV could not be started again: <reason>. Do not start MGS V from Steam now; start Play-Coop.bat again." (the console's run-1 wait stops at once; review SS-4)
- "The host's save was already applied once this session; the newer one was not used."
- Join hint: "Save sharing: if your co-op save starts somewhere else than the host's, you get the host's save at the title screen and MGSV restarts once (your own copy is kept; Share-Save.bat [R]). -SaveHandoff off turns this off."
- SHARE-SAVE:
- menu "[R] Restore the co-op copy the last import or automatic save handoff replaced";
- restore lines "Restoring the co-op save copy kept before the automatic save handoff at <path>" / "...kept before the import at <path>".
- INSTALL-COOP prompt when an automatic handoff is detected: "Your co-op save copy holds your partner's save (automatic save handoff on <yyyy-MM-dd HH:mm>)." It is followed by the Phase-1 replace and REPLACE confirmations.
- FRIEND-GUIDE subsection 'SAME PROGRESS AUTOMATICALLY (save handoff)':
- "When you JOIN and your co-op save would start somewhere other than the host's (or is in the story opening), the host's save is copied to you at the title screen and MGSV restarts once on it (about a minute; Play-Coop does it, keep its window open)."
- "The host's save is only shared once it continues into Mother Base or free roam, never from the story opening."
- "Press Continue twice, or F9 > Partner / Co-op > Shared save > Keep my own save, to keep yours."
- "Your own co-op copy is kept: Share-Save.bat [R] brings it back. Play-Coop -SaveHandoff off turns this off."
- FRIEND-GUIDE TRAVEL LOCK: "While connected, the joiner follows the host. If the joiner tries to go somewhere else, a message names where the host is; doing the same again within 10-30 seconds travels alone. A held take-off leaves alone after 2 minutes. F9 > Partner / Co-op > Travel lock turns it off for the session; Play-Coop -TravelLock off turns it off."
- link-status: '"handoff":"<off|idle|same|wait_host|offered|sending|partner_staged|partner_restarting|receiving|staged|countdown|exiting|kept|refused|failed|applied>","handoff_pct":<0-100>,'

## hidden_runtime_checks
- R0 Preconditions:
- Work only in D:/mgs-coop-wt/join-fix, with Phase 1 merged.
- Leave MGS_GAME_LUA_DIR unset (default <worktree>/build) and never point it at C:/Users/taylo/OneDrive/Documents/ChatGPT/mgs-coop.
- Take the verify lock: check C:\Users\taylo\AppData\Local\Temp\mgs-coop-verify.lock\owner.txt first, then mkdir and write owner.txt.
- Run one worker session at a time (Local\MGSCoopBackgroundSession); the anim worktree runs sessions often, so coordinate.
- Never use the visible desktop.
- Use -GraphicsQuality low for multi-load runs (docs/STATUS.md:320, error 1455).
- After every run: scenario.json has Closed, BaselineRestored and RealSavesUnchanged true, IsolationFailed false and Failure empty. No new file or folder may appear under D:\Games\steamapps\common\MGS_TPP*\mgs-coop other than logs and link-status.json: all handoff, provenance and aside paths must sit under build\worker-runtime\<run>, and no link-rejoin.txt may exist (UDP only).
- R1 Full CTest: 'pwsh -File tools/build.ps1 -Configuration Release -ReleaseGun'. Everything must pass, including:
- the new save_handoff_wire_tests, save_handoff_core_tests, save_handoff_io_tests and world_handoff_lua_check;
- link_protocol_tests, link_udp_tests, world_receipt_tests, lab_config_tests, steam_transport_tests, debug_overlay_tests and worker_debug_tests;
- world_follow_lua_check, world_follow_game_lua_check, world_sync_lua_check and world_sync_composed_lua_check;
- friend_package_tests, worker_session_tests and lab_scripts_tests.
Then 'pwsh -File tools/worker/Invoke-WorkerChecks.ps1 -Profile Normal -EnableBackgroundWorker' passes with the extended allow-list.
- R2 Transfer and staging, lab:
- Command: 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/handoff-stage.txt -SaveSeed lab -GuestSaveSeed real -LabSetting save_handoff=stage -GraphicsQuality low'.
- Host: 'save_handoff=provenance; set=...; mission=30010; source=continue|settle', then 'save_handoff=offer'.
- Guest: 'save_handoff=need; need=1; why=location' and 'save_handoff=staged; ... files=6; bytes=608012' (or the actual lab-seed count). It stays at 40050, with no exit in stage mode.
- run\guest-saves-handoff\<xfer>\ hashes equal handoff.json, which equals the host's progress files at offer time; run\guest-saves is unchanged.
- No world=drop or overflow; world=stats bad_op=0.
- R3 The same transfer over the play link: 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab -GuestSaveSeed real -LabSetting save_handoff=stage -ScenarioPath tools/worker/scenarios/handoff-stage.txt -GraphicsQuality low'. Pass: as R2, plus 'save_handoff=configured; role=...; channel=4' in both logs.
- R4 Restart, session 1:
- Command: 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/handoff-restart.txt -SaveSeed lab -GuestSaveSeed real -LabSetting host:save_handoff=stage -LabSetting guest:save_handoff=scene -GraphicsQuality low'.
- Guest: 'save_handoff=countdown', then 'save_handoff=exit; how=wm_close'. The process exits within 90 s with no forced termination. The host logs 'save_handoff=result_rx; status=exiting', then (UDP, the worker) 'link=lost; after_ms=...' with its handoff token partner_restarting; 'link=peer_left' appears only on Steam. Runtime correction (R4 run 954789292db2): 'partner_shutdown=hooks_off; teardown=other_thread' IS logged at the title, because PartnerMissionStart arms every runtime allocation including the 40050 title (base 0.12.1 logs it there too); it is not a handoff signal. handoff-restart.txt keeps the session open for the host's 3 s link=lost and captures the host H2 toast with coop_hud=full.
- The host's state.json hud toast starts with the H2 text.
- applyhandoff: run\guest-saves progress equals the staged manifest; run\guest-saves-replaced\<stamp>-before-handoff holds the old real-seed tree; settings files are the guest's own; imported-progress.json has source=handoff.
- R5 Restart, session 2:
- Command: 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/handoff-after-restart.txt -SaveSeed lab -GuestSaveSeedRun <R4 runId> -LabSetting save_handoff=scene -GraphicsQuality low'.
- The guest title is 40010 and the guest logs 'save_handoff=need; need=0; why=same' (no offer). Runtime correction (R5 run 318f135d): the host restarts from the same lab seed, so the applied guest copy equals the host's set (both 3ACACF1D) and GuestNeed answers same before the location rule; why=same_scene needs a different set at the same place.
- world=paired at 40010; the host's title Continue is followed (kind 4 plus title seeds); both reach 30010 and pair there.
- R6 Travel lock with escape:
- Command: 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/travel-lock.txt -SaveSeed lab -GuestSaveSeed real -LabSetting travel_lock=1 -LabSetting save_handoff=off -GraphicsQuality low'.
- The first guest Enter at the 40050 title logs 'decision=refuse' with note 7 b=1; the guest is still at 40050 after 4 s.
- The second Enter logs 'decision=repeat' and the guest reaches 30050.
- This also answers whether a swallowed title Continue can be pressed again.
- R7 Regressions, which must keep their documented results:
- pack-mafr-real.txt (-SaveSeed real; lab defaults: handoff off, lock 0): MB to ACC to 30020 follow, no 'step=lock' line.
- play-native.txt with -Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab: no save_handoff key means 'save_handoff=disabled; reason=off'; the lock defaults on in play but stays inert because the guest pairs and arms at the title; no 'decision=refuse'.
- coop-hud.txt, plus a -LabSetting coop_hud=off run: HUD strings unchanged.
- partner-native-checkpoint.txt and Phase-1 heli-alone.txt: unchanged evidence.
- Before building a friend package, re-run 'Invoke-WorkerChecks -Profile Release'.
- R8 Not runnable in the worker; covered by unit tests and the Play-Coop stand-in harness, to be confirmed in the next real two-PC session:
- the story-opening refusal (no such seed exists; do not create one from real saves);
- cross-account acceptance of the transferred TPP_GAME_DATA (Share-Save worked once on 10-09);
- the Steam steam-wait rejoin through link-rejoin.txt;
- the Play-Coop relaunch through Steam;
- whether a held sortie can be chosen again.
Real two-PC script:
(1) the user hosts with the save that Continues into 30010 and the friend (story-opening save) joins. Both titles are probably 40010, so the replay hold and toast G4 apply. The user Continues; within about 35 s the friend gets G1, then G2, MGSV restarts, the friend Continues into 30010, and they pair.
(2) The friend hosts: G5/H4, no transfer.
(3) An F9-paste join reconnects after the restart without a new code.

## risks_and_mitigations
- Cleanup race after the guest exits: Play-Coop's finally block (Play-Coop.ps1:665-690) and the watchdog (:139-151) both clean up, and Invoke-CoopPlayCleanup runs whenever state is active (FriendCommon.psm1:586-635). A relaunch that comes after cleanup would run plain MGSV on the REAL saves.
Mitigation:
- the handoff is claimed under the play mutex before any cleanup call;
- a cleanup guard refuses during a live relaunch claim (P2-17);
- the watchdog defers to a live console;
- relaunch happens only after the apply succeeded and the hashes were re-recorded;
- a 300 s bound, then a forced cleanup.
Covered by the stand-in harness tests.
- Pushing a save that cannot reach a co-op scene.
Mitigation: two independent gates. First, the story must be open, with the constants readable and strictly ordered. Second, the set must be bound to a SUPPORTED mission by observed evidence: a Continue target, or a 30 s in-game settle in 30010/30020/30050. Unknown always refuses. A wrong push is restorable through the aside folder and Share-Save [R].
Residual: a settle bind could race a SaveGameData(next) written just before leaving (package-02.lua:902). This is blocked because Lua reports loading at once and the set must stay unchanged for 30 s.
- Repeated handoffs that discard the guest's own progress.
Mitigation:
- the 'scene' policy transfers only when the guest cannot meet the host (story not open, bound to an unsupported mission, or a different title location);
- the relaunch rewrites lab.ini with save_handoff=off and handoff.count=1;
- at most 3 offers per set per link session.
The literal 'always' is available through lab.ini and Play-Coop -SaveHandoff always.
- Host snapshot colliding with the game's save writes. Remote slots are written with exclusive CREATE_ALWAYS (save_redirect.cpp:132); local slots use unhooked I/O with an unknown share mode.
Mitigation:
- g_io_lock around RS_FileWrite, RS_FileRead and RS_FileDelete, and read_remote_snapshot takes it shared;
- local reads only while the Lua report is fresh with saving=0, one file per tick, with full share flags and a double read;
- bytes are served from memory, and a mismatch gives ABORT snapshot_changed.
- WM_CLOSE might not exit quickly. The worker saw both graceful and forced closes (stop.json 873d7197 vs cf395755). The close may also not reach PartnerShutdown if the overlay never subclassed the window.
Mitigation:
- exit only at the title, where no partner mission is armed (game_script_observer.cpp:1214-1219);
- log exit_slow and show toast G9 after 20 s;
- tooling applies on any exit, a crash included, because request.json is written only after verified staging.
- A Steam invite or F9-paste guest has no link-secret.txt (Play-Coop.ps1:553-554), and the DLL reads the secret only at startup (partner_net.cpp:1295-1312).
Mitigation:
- the guest's code is kept in memory and zeroed at shutdown;
- link-rejoin.txt is written only at the exit request and only for steam-wait guests;
- Play-Coop converts it under the mutex into link-secret.txt plus net_peer_steam, records the hashes and deletes it;
- it is never collected.
The host keeps its secret for the whole process (partner_net.cpp:1316-1321).
- Breaking verified follows.
Mitigation:
- the lock is an inner wrapper below world_follow's guest() suppression, so armed guests never reach it;
- ACC targets and the host's mission are always allowed;
- it passes through when F.token is set;
- it is inert while F.current or the queue is non-empty, and inert with r[13] nil, so old fixtures are unaffected;
- the replay hold applies only at the title while a handoff is pending, and the 120 s queue timeout still applies.
R6 and R7 cover it.
- Trapping the guest.
Mitigation:
- every refusal is soft (a repeat goes alone);
- the sortie hold has a 120 s bound;
- the F9 Travel lock checkbox is UI-only and works at the title and in the helicopter;
- the lock is off when the host is at mission 0 or 1, loading, unsupported or unlinked;
- Play-Coop -TravelLock off turns it off;
- the countdown veto works by pressing Continue twice or with F9 Keep my own save.
Open: whether a swallowed sortie can be chosen again. The bound and F9 cover it either way.
- Channel 4 starving world or game traffic, or an old lab peer that never acknowledges.
Mitigation:
- ascending pack order puts channels 1..3 first;
- at most 4 new chunks per tick and 16 queued;
- the guest pulls with WANT;
- RemoteBuildAtLeast gates every channel-4 byte;
- play builds are identical anyway (link_session.cpp:341-353).
Cost: about 66 KB of extra static reliable slots, plus at most 8 MiB per snapshot, freed on every exit path.
- link-status overflow: Phase 1 brings the worst case to about 950 bytes, and Phase 2 adds about 36.
Mitigation: 1536-byte buffers (partner_net.cpp:124, :702, :706) and a max-length unit test. FormatStatus fails closed.
- The read timing is unknown, and an in-process apply is unsafe: memory overwrites the files through SaveGameData (package-02.lua:431/:902/:983/:2167), and a native hold on the install thread risks Not Responding.
Mitigation: v1 always restarts and applies after the process exits. The pre-read gate (candidate 2) is deferred behind a probe.
- Cross-account acceptance of a transferred save is unverified on 0.12.x (Share-Save.ps1:17-20).
Mitigation: the aside copy, [R] restore, toast G10 naming [R], and a real two-PC confirmation before calling the feature done.
- Lua size and splice: the new chunk lives in its own file, under 16,000 bytes, with its own check and the composed splice check. world_follow.lua gets one line.
- Restricted files: debug_overlay.cpp is not edited (the window is found with EnumWindows), and no camera-agent file is touched (coop_carry*, teammate_targeting*, native_ui*, world_revive_input.lua). Any tools/*.ps1 or .psm1 change invalidates the Release receipt (package.ps1:58-83), so it is re-run before packaging.

## open_questions
- Consent: the user approved an automatic handoff. Is the 10 s on-screen countdown with its veto enough, or should the first-ever handoff on a PC also ask once in Play-Coop (Join: 'Use the host's progress automatically? [Y/n]')?
- Policy: 'scene' (transfer only when the guest cannot meet the host) is the default here, to avoid replacing the guest's progress every session. Is that the right reading of 'if it differs', with 'always' kept as an option?
- In a first session, the host is eligible only after it has Continued into Mother Base or free roam: the settle bind takes 30 s, or a Continue bind. Is it acceptable that the guest waits at the title meanwhile? Persisted provenance makes title-time handoff work in later sessions.
- Should the in-process 'apply before the first read' path (candidate 2) be probed later? That needs a save_io=first log and extraction of TppSave.lua and the mission-1 sequence.
- Should old '-before-handoff' aside folders be pruned (for example, keep the newest 5)? There is no pruning in v1.
- Is a supervisor 'relaunch guest' verb wanted, for a one-session worker proof, or is the two-session R4 plus R5 proof enough?
- Play-Coop -World 0 sessions get neither the handoff nor the lock, because world receipts need world_on. Is that acceptable?
- Should the sortie hold release alone after 120 s (as specified, mirroring the Phase-1 host hold), or stay held until the F9 unlock or a second choice?
