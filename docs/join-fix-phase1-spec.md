# Join-fix Phase 1 spec: partner-scene visibility, bounded heli hold, story-start warning, sticky-arm fix, connection UX, Share-Save tooling

## Summary
Phase 1 makes a mission mismatch visible everywhere and removes the silent helicopter trap. Native world::Core gains a remote-scene model: the partner's mission, level and location for both roles, a "linked" state, and a pair-wait reason. That model reaches Lua through a backward-compatible CFG reply (r[9..11]), reaches link-status.json/HUD/F9/state.json through world_link::Status and partner_net::PublishWorldState atomics, and writes one world=pair_wait log line per change. Lua gets W.peer() and W.note(), plus a single native-owned on-screen route: MGS_COOP_WORLD_NOTE_V1 -> world_link::HandleNote -> coop_hud::PostWorldNote, whose sentences live in coop_text.

The host helicopter hold, in world_follow.lua's OnEndMissionPreparation and its unsupported-target branch, now behaves as follows. It never waits when no partner can come (no link, partner not linked, or partner in another mission and not still replaying the host's last transition; this is the verdict RC4(b) exclusion via FOLLOW_ACK tracking). It shows a toast, re-posts it every 3 s, can be escaped by choosing the deployment again (debounced 1 s), and is bounded at 120 s. Stale holds are dropped when the mission changes.

Other items:
- A title-ACC progress report (storySnake only) warns about the story start and points at Share-Save.
- The sticky F.armed bug gets a safe disarm: empty queue plus 120 s unpaired, or the partner's mission known to be unsupported.
- Small UX fixes: F9 partner-stage suffix, honest time readback (clockOn), unpaired align wording, the HUD/Play-Coop "host has a new code" text, join_ignored toast and F9 result, and kPendingSlots=4.
- Tooling: FRIEND-GUIDE Share-Save section, an import marker plus a two-step Install-Coop reseed guard, and Collect-Logs coop-saves.txt plus the last 3 sessions.

The lab goto delegates its step-2 wait to the new bounded hold, so the hidden worker can exercise the hold (pack-mafr-real) and its timeout (new heli-alone.txt).

Tracks have disjoint files:
- A (native world core and receipts) depends only on three header declarations owned by B.
- B (native presentation and connection UX).
- C (Lua).
- D (PowerShell and friend docs).

Phase 2 reuses W.peer/linked, F.alone/F.following, note codes 5/6 (reserved), world_link::Status (remote_mission, linked, progress, title_mission) and the import-marker format (source=handoff).

## Contracts
- K1 CFG reply (native/world_receipt.cpp FormatCfgReply :134-140; CfgReply native/include/mgscoop/world_wire.hpp:182-187): 'return <level>,<role>,<proto>,<link>,<epoch>,<probe>,<mode>,<session>,<remote_mission>,<remote_level>,<wait>'. r[1..8] are byte-identical to today. r[9] is remote_mission u32, 0 = unknown (no link, no HELLO yet, or the partner between its MISSION_END and its next READY/HELLO). r[10] is remote_level 0..3, 0 = unknown. r[11] is the RAW pair-wait code 0..10 (K2). The reply is filled for phase 0 and phase 1. CfgReply appends 'std::uint32_t remote_mission; std::uint8_t remote_level, wait;' after session. Lua treats r[9]/r[11] == nil as an older loader, which keeps today's behaviour.
- K2 Pair-wait codes (world_wire.hpp new 'namespace pair_wait'): 0 paired, 1 off (level_ 0), 2 no_link, 3 no_hello, 4 remote_off (remote level 0), 5 remote_saves, 6 same_role, 7 local_loading (!local_.valid), 8 remote_loading (!remote_.valid), 9 different_mission, 10 epoch (TryPair host epoch / guest host_mission_ gate). Code 11 'settling' exists only in published views (K4). Evaluation order is exactly TryPair's (world_wire.cpp:657-665). Names are exactly 'paired','off','no_link','no_hello','remote_off','remote_saves','same_role','local_loading','remote_loading','different_mission','epoch','settling', and any other value gives 'unknown'. Both world::PairWaitName (A, log lines) and coop_text::PairWaitName (B, link-status/F9) implement this table.
- K3 Remote state semantics. Core::remote_mission() = link_up_ && remote_hello_ && remote_.valid ? remote_.mission : 0 (the host learns it from READY, the guest from HELLO or READY; it is cleared by a matching remote MISSION_END (world_wire.cpp:499-501) or OnSession, and masked while the link is down). remote_level() = link_up_ && remote_hello_ ? remote_level_ : 0. remote_location() = HELLO v[1] stored for BOTH roles, reset in Configure and OnSession. linked() = link_up_ && remote_hello_ && remote_saves_==1 && remote_role_!=config_.role && remote_level_>=1. local_mission() = local_.valid ? local_.mission : 0. No change to HELLO/READY layouts or the OpSpec table (Table() count stays 50).
- K4 Settle rule (pure world::WaitPublisher in world_wire.hpp/.cpp, owned by world_link). published = raw, except raw 9 is published as 11 until the same (local, remote) pair has stayed different for 15000 ms. The first time 9 is published for a pair, world_link posts note 16 once; a different pair posts again. CFG r[11] carries RAW codes. world_link::Status.pair_wait, link-status, the HUD and F9 carry PUBLISHED codes.
- K5 NOTE receipt: "return 'MGS_COOP_WORLD_NOTE_V1' -- <code> <a> <b>" using the strict world_receipt integer grammar (no sign, no leading zeros, single spaces). code 1..15; a and b 0..4294967295. Receipt::note is appended to the enum; note_prefix = "return 'MGS_COOP_WORLD_NOTE_V1' -- ". It is dispatched by SelectWorldReceipt only when world_on and only on the runtime or termination thread. Replies: 'return true' (handled, or skipped by the rate gate), 'return false' (code with no Phase-1 meaning: 5..15), or the literal (malformed or not configured). Lua ignores the result.
- K6 Note codes. Sent by Lua: 1 PROGRESS{a=title-mode ACC mission, b=1 story start (storySnake) | 0 not}; 2 HELI_WAIT{a=target, b=0}; 3 HELI_ALONE{a=target, b=reason 1 no_partner, 2 partner_elsewhere, 3 repeat, 4 timeout}; 4 HELI_REFUSED{a=unsupported target or nextMissionId (0 if nil), b=0}. Posted by native only: 16 MISMATCH{a=local mission, b=partner mission}. Reserved for Phase 2 (Phase 1 replies return false): 5 travel_locked, 6 host_continue_unsupported.
- K7 Toast route. world_link::HandleNote calls coop_hud::PostWorldNote(code, a, b, partner=Core::remote_mission(), local=Core::local_mission()) only after releasing g_world_lock. The pure world::NoteGate skips a post when the same (code,a,b) was posted under 1000 ms ago, and skips the log line when it was logged under 10000 ms ago. Code 1 sets world_link progress = b?2:1 and title_mission = a, and posts only when b==1. world_link never calls partner_net or coop_hud while holding g_world_lock.
- K8 partner_net API (B declares near PublishPackState, partner_net.hpp:205-217): 'struct WorldView { std::uint8_t level; bool paired; std::uint32_t partner_mission; std::uint8_t pair_wait; std::uint8_t progress; }; void PublishWorldState(const WorldView& view) noexcept;'. It may be called from any thread, uses atomics only and never blocks. A calls it at the end of world_link OnIdleHook (after Flush), Configure and HandleNote. progress: 0 unknown, 1 ok, 2 story_start. partner_net never includes world_link.
- K9 coop_hud API (B). EventKind gets 'world_note' (arg=code; text="<a> <b> <partner> <local>" in decimal, at most 43 chars) and 'join_ignored' (text=via), both appended after no_body. Add 'void PostWorldNote(std::uint8_t code, std::uint32_t a, std::uint32_t b, std::uint32_t partner, std::uint32_t local) noexcept;' (any thread). LinkView appends 'std::uint8_t world_level, pair_wait; bool world_paired; std::uint32_t partner_mission;' after 'session, mission'. Zero defaults must reproduce every existing FormatLine string.
- K10 coop_text API (B; coop_reason_text.hpp/.cpp, pure ASCII): 'bool FormatMission(std::uint32_t mission, char* out, std::size_t capacity) noexcept;', 'bool FormatMissionShort(std::uint32_t mission, char* out, std::size_t capacity) noexcept;' (<=22 chars), 'const char* PairWaitName(std::uint8_t code) noexcept;', and 'bool FormatWorldNote(std::uint8_t code, std::uint32_t a, std::uint32_t b, std::uint32_t partner, std::uint32_t local, char* out, std::size_t capacity) noexcept;'. FormatWorldNote returns false when the note has no toast (code 1 with b=0, unknown codes).
- K11 Mission names, shared by native and PowerShell. Long (FormatMission and Get-CoopMissionName): 0 'a loading screen', 1 'the start screen', 30010 'Afghanistan free roam', 30020 'Africa free roam', 30050 'Mother Base', 40010 'the Afghanistan helicopter', 40020 'the Africa helicopter', 40050 'the Mother Base helicopter', 10000..11999 'story mission N', other 'mission N'. Short (FormatMissionShort, HUD and F9 header): 0 'loading', 1 'start screen', 30010 'Afghanistan', 30020 'Africa', 30050 'Mother Base', 40010 'Afghanistan heli', 40020 'Africa heli', 40050 'Mother Base heli', 10000..11999 'story mission N', other 'mission N'.
- K12 link-status.json (partner_net.cpp FormatStatus :639-696). New keys are appended AFTER the transport block that ends with "left":N, and BEFORE "time_utc": '"world":<0-3>,"paired":<0|1>,"partner_mission":<u32>,"pair_wait":"<K2 name>","progress":"unknown|ok|story_start",'. Gating: level 0 gives paired 0, partner_mission 0, pair_wait 'off'. A state that is not connected gives paired 0, partner_mission 0, pair_wait 'no_link'. Otherwise the published values apply. All existing keys and their order are unchanged ('mission' stays the LOCAL mission). The extra bytes are about 105, within the 1024-byte buffers.
- K13 world_link::Status (world_link.hpp:39) appends, after epoch: 'bool linked{}; std::uint8_t remote_level{}, core_wait{1}, pair_wait{1}, progress{}; std::uint32_t local_mission{}, remote_mission{}, remote_location{}, title_mission{};'. pair_wait is the published code (K4) and core_wait the raw code. The '{}' returned when not configured therefore reads 'off'. GetStatus (world_link.cpp:195-199) fills members by name.
- K14 debug_menu::Snapshot (debug_menu.hpp, after the world_effective line :46) appends 'std::uint32_t world_partner_mission{}, world_remote_level{}, world_pair_wait{1};' (B declares; A fills them in PublishDebugSnapshot, game_script_observer.cpp:1733-1741). The worker state.json (worker_debug.cpp:410-411) adds '"world_partner_mission":%u,"world_pair_wait":%u,' immediately after "world_effective":%u,.
- K15 SteamInfo (partner_net.hpp:264-284) appends 'std::uint32_t join_seq; std::uint8_t join_result;'. join_result is 1 joined, 2 ignored because busy, 3 own code. ArmSteamGuest increments join_seq on every decision. steam_online PlayPass maps changes to g_last_result.
- K16 Lua API (world_sync.lua; usable by Phase 2). W.note(code, a, b) returns true only when native returned true; it sends nothing (returns false) for a non-integer or negative value. W.peer() returns nil when r[11] is not a number (older loader). Otherwise it returns {mission=r[9] or 0, level=r[10] or 0, wait=r[11], session=(r[4]==1 and r[8]>0) and r[8] or nil, linked=session~=nil and (r[11]==0 or r[11]>=7)}.
world_follow.lua exposes: F.alone() -> nil (the partner can come) | 1 | 2; F.following(p); F.hostDeploy={n, mission, target, at, checkAt, notedAt, [1..n]=args}; F.refused={target, mission, at}; F.outgoing.ack (last FOLLOW_ACK status for F.outgoing[1]); F.unpairedAt; F.title={mission, story, at}.
- K17 Log line formats.
Native (Core::Tick; only while level_>=1, link_up_ and !paired_; only when (reason, local, remote) changes; reset when paired or link down; Flush appends '; t_ms=N'):
- 'world=pair_wait; reason=<K2 name>; local=<u>; remote=<u>; remote_level=<u>'
- world_link: 'world=note; code=<u>; a=<u>; b=<u>; partner=<u>'
- 'world=note; code=<u>; result=unknown'
Lua (W.log 'event=follow;' prefix):
- 'step=deploy_wait;reason=ready;target=<t>;partner=<p|nil>'
- 'step=deploy_alone;reason=no_partner|partner_elsewhere|repeat|timeout|disabled;target=<t>'
- 'step=deploy_held;reason=stale;mission=<m>'
- the unchanged first-press line 'step=refused;reason=unsupported_mission;target=..;emergency=..;fn=OnEndMissionPreparation'
- 'step=disarm;reason=partner_mission|unpaired_timeout;partner=<m>'
- 'step=progress;title=<m>;story_start=0|1'
Align: 'event=align;step=wait;reason=unpaired'. Goto: 'event=goto;step=2;result=waiting;reason=held'.
- K18 Import marker: '<game>\\mgs-coop\\coop-saves\\imported-progress.json', written by Share-Save import INTO the new copy before the swap. Content: {"format":"mgs-coop-imported-progress","v":1,"source":"share-save","imported_utc":"<ISO>","exported_utc":"<from zip>","files":[{"entry":"287700/local/TPP_GAME_DATA0","bytes":N,"sha256":".."}]}. It holds no save bytes; Phase 2 uses source 'handoff'.
Detection (FriendCommon Get-FriendImportedSaveInfo -SaveDir -ReplacedRoot): a valid marker wins. Otherwise take the newest coop-saves-replaced child dir by Name (names matching '^\\d{8}-\\d{6}'); if it ends with '-before-import' or '-before-handoff', the copy counts as imported. Otherwise $null.
When Install-Coop reseeds, it moves the marker to '<seedArchive>\\imported-progress.json' after Copy-SaveSeed. Copy-SaveSeed itself is unchanged.
- K19 Collect-Logs 'coop-saves.txt'. Header lines: '# MGS Co-op save folders: names, sizes and times only (no save contents)' and '# path<TAB>bytes|dir<TAB>last_write_utc'. Rows are paths relative to mgs-coop with '/' separators, one per dir and file, under coop-saves/, coop-saves-replaced/, local-test-saves/, save-import-*/ and coop-saves-new-*/. At most 500 rows, then '# truncated: N more'. No file contents. summary.txt gets 'coop_saves=files=<n>; import_marker=present|absent; newest_replaced=<name|none>'. script-observer.log keeps the text from the 3rd-last 'MGS Co-op script adapter' line (all of it if there are fewer), limited to min(16 MB, 3*MaxLogBytes) chars.
- K20 Play-Coop status rule (CoopPlay.psm1 Format-CoopStatusLine). The different-missions explanation is shown only when state=='connected' and pair_wait=='different_mission' and partner_mission!=0. The 'Partner body unavailable' suffix is never added for missions 0, 1, 40010, 40020 or 40050, but is kept for other non-30010/30020/30050 missions. The story suffix is added whenever progress=='story_start'. The guest stale-code clause applies when left=1, state!='connected', reason=='bad_code' and verified==0. Documents without the new keys produce today's strings, except that missions 0, 1 and 4xxxx lose the body suffix.
- K21 Test wiring: no new CTest names (extend existing files), so the Invoke-WorkerChecks allow-list stays valid. There is one new scenario file, tools/worker/scenarios/heli-alone.txt (validated automatically by worker_session_tests). Lua fixtures must stub W.note and W.peer. Every new Lua call to them is type-guarded so that older fixtures keep passing.

## Tracks

### A: native world core, receipts and status plumbing
**Owns:** native/include/mgscoop/world_wire.hpp, native/world_wire.cpp, native/include/mgscoop/world_receipt.hpp, native/world_receipt.cpp, native/include/mgscoop/world_link.hpp, native/world_link.cpp, native/game_script_observer.cpp, native/worker_debug.cpp, tests/world_wire_tests.cpp, tests/world_receipt_tests.cpp, docs/world-sync-protocol.md

**Depends on:** B: header declarations only: partner_net.hpp WorldView+PublishWorldState (K8), coop_hud.hpp PostWorldNote+EventKind::world_note (K9), debug_menu.hpp Snapshot world_* fields (K14). Land B's three header edits first or build A after B merges; world_wire/world_receipt and their tests need nothing from B.

**Changes:**
- A1 world_wire.hpp, after namespace unpair (:100-102): add 'namespace pair_wait { inline constexpr std::uint8_t paired=0, off=1, no_link=2, no_hello=3, remote_off=4, remote_saves=5, same_role=6, local_loading=7, remote_loading=8, different_mission=9, epoch=10, settling=11; }', 'const char* PairWaitName(std::uint8_t code) noexcept;' (K2 table) and 'inline constexpr std::uint64_t kMismatchSettleMs = 15000;'.
- A2 world_wire.hpp CfgReply (:182-187): append 'std::uint32_t remote_mission; std::uint8_t remote_level, wait;' after session (K1).
- A3 world_wire.hpp, Core observers (after :261): 'std::uint32_t local_mission() const noexcept; std::uint32_t remote_mission() const noexcept; std::uint8_t remote_level() const noexcept; std::uint32_t remote_location() const noexcept; bool linked() const noexcept; std::uint8_t pair_wait() const noexcept;' with the K3 semantics and K2 order.
- A4 world_wire.hpp, Core private section (:327-340): add 'std::uint32_t remote_location_ = 0; std::uint8_t logged_wait_ = 0xFF; std::uint32_t logged_wait_local_ = 0, logged_wait_remote_ = 0;' and 'void NotePair() noexcept;'.
- A5 world_wire.hpp/.cpp: add the pure helpers 'struct WaitPublisher { std::uint8_t Update(std::uint8_t raw, std::uint32_t local, std::uint32_t remote, std::uint64_t now_ms, bool& post_mismatch) noexcept; ... }' (K4: raw!=9 returns raw and deactivates while keeping the last pair and posted flag; raw==9 with a new pair restarts the 15 s timer and clears posted; it publishes 11 until 15000 ms, then 9 with post_mismatch=true exactly once per pair) and 'struct NoteGate { void Accept(std::uint8_t code, std::uint32_t a, std::uint32_t b, std::uint64_t now_ms, bool& post, bool& log) noexcept; }' (K7: one slot per code 0..31; post skipped for the same (a,b) within 1000 ms, log skipped within 10000 ms).
- A6 world_wire.cpp OnHello (:597-628): set 'remote_location_ = U(op.v[1]);' right after 'remote_hello_ = true;' (:605), for BOTH roles and before the saves and role refusals. Configure (:287-327) and OnSession (:734-748) reset remote_location_=0 and logged_wait_=0xFF.
- A7 world_wire.cpp: implement pair_wait() in exact TryPair order (paired_ -> 0; level_<1 -> 1; !link_up_ -> 2; !remote_hello_ -> 3; remote_level_<1 -> 4; remote_saves_!=1 -> 5; remote_role_==config_.role -> 6; !local_.valid -> 7; !remote_.valid -> 8; remote_.mission!=local_.mission -> 9; else 10), plus PairWaitName and the other observers.
- A8 world_wire.cpp Core::Tick (:1137-1151): call NotePair() last. NotePair: if paired_ || !level_ || !link_up_ then logged_wait_=0xFF and return. Otherwise compute (w, local_mission(), remote_mission()); when it differs from the logged tuple, store it and Line("world=pair_wait; reason=%s; local=%u; remote=%u; remote_level=%u", ...). No PushLocal: the local op table and control queue sizes are unchanged.
- A9 world_wire.cpp Core::Cfg reply fill (:826-832): reply.remote_mission=remote_mission(); reply.remote_level=remote_level(); reply.wait=pair_wait(); for both phases.
- A10 world_receipt.hpp: update the header grammar (:9-20) so CFG documents 11 reply values and NOTE_V1 is described. Append 'note' to enum Receipt (:27), add 'inline constexpr char note_prefix[] = "return 'MGS_COOP_WORLD_NOTE_V1' -- ";' and 'bool ParseNote(const char* source, std::size_t size, std::uint8_t& code, std::uint32_t& a, std::uint32_t& b) noexcept;' (outputs only on success).
- A11 world_receipt.cpp: Classify (:56-67) adds 'if (Prefixed(source, size, note_prefix)) return Receipt::note;'. ParseNote follows ParseWarp (:121-133): three strict Token()s with single spaces and nothing trailing, code 1..15, a and b 0..4294967295, no negatives. FormatCfgReply (:134-140) uses 'return %u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u' with the three new fields last.
- A12 world_link.hpp: append the K13 members to Status (:39) and declare 'const char* HandleNote(const char* source, std::size_t size) noexcept;' with a comment giving the runtime/termination-thread rule.
- A13 world_link.cpp: add, under g_world_lock, 'world::WaitPublisher g_waits; world::NoteGate g_notes; std::uint8_t g_progress = 0, g_published_wait = 1; std::uint32_t g_title_mission = 0;'. Add a helper PublishWorld(now): under the lock, read the core (level, paired, linked, local_mission, remote_mission, remote_level, pair_wait, remote_location), call g_waits.Update and store g_published_wait. After the lock: partner_net::PublishWorldState({level, paired, remote_mission, g_published_wait, g_progress}); if post_mismatch, also call coop_hud::PostWorldNote(16, local, remote, remote, local). Call PublishWorld at the end of OnIdleHook (after Flush, :143-144), at the end of Configure (:191) and inside HandleNote. GetStatus (:195-199) fills Status by member name, including core_wait, pair_wait=g_published_wait, progress and title_mission.
- A14 world_link.cpp HandleNote: return nullptr unless Configured() and ParseNote succeeds. Then: now=NowMs(); under the lock read partner=remote_mission() and local=local_mission() and run g_notes.Accept. For code==1, set g_progress=b?2:1 and g_title_mission=a. Codes 1..4 are known and 5..15 are unknown. After the lock: when log is set, write 'world=note; code=%u; a=%u; b=%u; partner=%u; t_ms=%llu' (unknown codes: 'world=note; code=%u; result=unknown'). When post is set and the code is known (code 1 only when b==1), call coop_hud::PostWorldNote(code, a, b, partner, local). Then PublishWorld(now). Reply 'return true' for known codes and 'return false' for unknown ones. Include mgscoop/coop_hud.hpp.
- A15 game_script_observer.cpp SelectWorldReceipt (:3355-3385): add 'else if(kind==world::Receipt::note) { if(OnRuntimeThread() || OnTerminationThread()) reply=mgscoop::world_link::HandleNote(buffer,size); }' and keep world_quiet=true for it. Add NOTE (runtime or termination thread) to the thread comment at :3349-3354.
- A16 game_script_observer.cpp PublishDebugSnapshot (:1733-1741): add 'snapshot.world_partner_mission = world_status.remote_mission; snapshot.world_pair_wait = world_status.pair_wait; snapshot.world_remote_level = world_status.remote_level;'. Touch nothing else in the file; it is not a camera-agent file.
- A17 worker_debug.cpp (:410-411): insert '"world_partner_mission":%u,"world_pair_wait":%u,' right after '"world_effective":%u,' using s.world_partner_mission and s.world_pair_wait.
- A18 docs/world-sync-protocol.md:
- §5 Receipts table (:204-210): the CFG row lists 11 values with the r[9..11] meanings (K1); add a NOTE row (K5/K6).
- After the local-ops paragraph (:150-155): document the pair_wait codes and log line (K2/K17) and say 'no new local op'.
- Replace the W3 paragraph at :796-806 ('Initial pairing still requires...Host deployment waits for readiness...') with the Track C behaviour, in substance: the host deployment waits only while a linked level-3 partner is in this scene, loading, or still replaying the host's last transition; it shows an on-screen note, ends after 120 s or a second choice of the deployment (1 s debounce), and a stale hold is dropped on a mission change; an unsupported or emergency target is refused once with a note and allowed when chosen again within 1-30 s, or at once when no partner can come; an armed guest disarms after 120 s unpaired with an empty queue, or at once when the partner's mission is unsupported.
- Update the 'Lab goto' text: step 2 no longer pre-waits; the follow hold decides.

**Tests:**
- world_wire_tests Table(): the count stays 50 (no OpSpec change).
- world_wire_tests Pairing(), new cases: (1) host 30050 and guest 30010 with HELLO+READY exchanged: both report pair_wait()==9, remote_mission() equals the other side's mission on BOTH roles, remote_location()==1 on both, linked() true; three Tick() calls produce exactly one 'world=pair_wait; reason=different_mission; local=30050; remote=30010; remote_level=3' line on the host. (2) After a remote MISSION_END: remote_mission()==0 and pair_wait()==8. (3) After OnSession: 0 and 3. (4) After OnLinkState(false): 0 and 2, and no pair_wait line while the link is down. (5) A HELLO with saves=0 gives 5; the same role gives 6. (6) While paired: pair_wait()==0 and Tick logs no pair_wait line; after Cfg(start) with link up a new line appears.
- world_wire_tests ConfigSession(): assert the new CfgReply fields: 0/0/2 disconnected; remote mission and level after the exchange; 0 after remote MISSION_END, OnSession and link down; the first 8 fields are unchanged.
- world_wire_tests, new WaitPublisher cases: raw 9 publishes 11 at t, 11 at t+14999 and 9 at t+15000 with post_mismatch exactly once. The same pair again yields no second post; a new remote pair restarts the timer and posts again; a non-9 raw value is passed through at once.
- world_wire_tests, new NoteGate cases: an identical (code,a,b) within 1000 ms gives post=false; at 1000 ms post=true; log is limited to once per 10000 ms; a different b posts at once.
- world_wire_tests :765 ('waiting logged once' lines.size()==1) and :905 ('no stats line when nothing changed') must pass UNCHANGED. Both run with the link down or paired, so NotePair stays silent.
- world_receipt_tests :74-81: the exact CFG strings become 'return 3,2,1,1,65535,15,1,4294967295,0,0,0' and 'return 3,2,1,0,65535,15,1,0,0,0,0'. Add a case with nonzero new fields, e.g. ',30010,3,9'.
- world_receipt_tests NOTE: Classify returns note for a valid receipt and none for name!=source. ParseNote accepts '1 40010 1' and '4 0 0', and refuses '0 1 1', '16 1 1', '-1 1 1', '01 1 1', '1  1 1', '1 1', '1 1 1 ', and '1 4294967296 0'.

### B: native presentation (link-status, HUD, F9, coop_text) and connection UX
**Owns:** native/include/mgscoop/partner_net.hpp, native/partner_net.cpp, native/include/mgscoop/coop_hud.hpp, native/coop_hud.cpp, native/include/mgscoop/coop_reason_text.hpp, native/coop_reason_text.cpp, native/include/mgscoop/debug_menu.hpp, native/debug_actions.cpp, native/debug_menu_ui.cpp, native/steam_online.cpp, native/include/mgscoop/link_session.hpp, tests/steam_transport_tests.cpp, tests/link_udp_tests.cpp, tests/debug_menu_tests.cpp, tests/link_session_tests.cpp, docs/link-protocol.md, docs/debug-menu.md, docs/steam-online.md

**Depends on:** 

**Changes:**
- B1 (land first; A builds against it) partner_net.hpp: add WorldView and PublishWorldState (K8) next to PublishPackState (:205-217), and append join_seq/join_result to SteamInfo (K15, :264-284). coop_hud.hpp: append EventKind world_note and join_ignored after no_body (:121-135), declare PostWorldNote (K9), append the LinkView fields after 'session, mission' (:37-54). debug_menu.hpp: append the K14 Snapshot fields after the world_effective line (:46).
- B2 partner_net.cpp: add atomics 'volatile LONG g_world_state = 0x10 /*pair_wait 1 off*/, g_world_partner = 0;' next to g_partner_mission (:110). PublishWorldState packs level (bits 0-1), paired (bit 2), pair_wait (bits 4-7) and progress (bits 8-9) with InterlockedExchange, and stores partner_mission separately.
- B3 partner_net.cpp FormatStatus (:639-696): build a 'char world[128]' block with K12 gating (names from coop_text::PairWaitName; progress names unknown/ok/story_start) and print it as a third %s after partner_status and transport, immediately before "time_utc". Keep every existing key and order. Never exceed the 1024-byte buffers.
- B4 partner_net.cpp PublishHud (:553-581): fill v.world_level, v.pair_wait, v.world_paired and v.partner_mission from the atomics (partner_mission 0 unless state==connected).
- B5 partner_net.cpp ArmSteamGuest (:1129-1180): for the busy branch (:1140-1142), keep the log line and add 'coop_hud::Post(coop_hud::EventKind::join_ignored, 0, via);' plus join_result=2 and ++join_seq. For peer_is_self (:1144-1146), set join_result=3 and ++join_seq. On success (end), set join_result=1 and ++join_seq. GetSteamInfo (:1785-1808) copies both values.
- B6 steam_online.cpp PlayPass (:631-641): keep 'Code submitted; joining...' at paste time. Add a static last_join_seq; when info.join_seq changes, set g_last_result for result 2 to 'That code was not used: you are already connected or connecting.' and for result 3 to 'That code is your own; ask your friend for theirs.'. Result 1 leaves it unchanged.
- B7 coop_hud.cpp:
- PostWorldNote packs Event{kind=world_note, arg=code, text=snprintf("%u %u %u %u", a, b, partner, local)} via Post (:338-346).
- FormatLine connected (:375-382): when partner != nullptr && v.pair_wait == 9 && v.partner_mission && current.drive == PartnerDrive::loading && FormatMissionShort succeeds, print 'CO-OP [connected] %.24s - %s %u ms | partner in %s'. Otherwise keep today's strings.
- reconnecting (:410-412): when v.left && strcmp(v.reason,"bad_code")==0 && !v.verified, print 'CO-OP [the host has a new code] paste it in F9 > Partner / Co-op > Online' (tone warn). Otherwise keep the old text.
- FormatToast (:436-491): add case world_note (strictly parse the 4 unsigned tokens, call coop_text::FormatWorldNote; tones: code 16 warn, 1 warn, 2 info, 3 warn for b in {2,4} else info, 4 warn) and case join_ignored ('That code was not used: you are already connected or connecting to a host.', warn). No default case.
- B8 coop_reason_text.hpp/.cpp: implement FormatMission and FormatMissionShort (K11), PairWaitName (K2), and FormatWorldNote with the exact sentences listed in user_visible_texts (codes 16, 1(b=1), 2 two variants: p!=0 && p!=local vs otherwise, 3 with b=1/2(p!=0 / p==0)/3/4/other, 4). Every sentence fits in 199 chars. Follow the FormatNoBody pattern (:160-170).
- B9 debug_actions.cpp DisabledReason (:193-201). Before the 'gameplay' check, add: 'if (a.command && s.enabled && !s.terminating && !s.gameplay_ready && s.mission && !MissionName(s.mission)) return "mission";'. Replace the kNeedsPartner line with: 'return (!s.world_paired && s.world_pair_wait == 9 && s.world_partner_mission && s.world_partner_mission != s.mission) ? "partner_elsewhere" : "partner_pose";'. Do not call coop_text here (mgs_debug_menu does not link core).
- B10 debug_menu_ui.cpp:
- ReasonText (:164-178) gains {"mission", ...} and {"partner_elsewhere", ...} (exact texts in user_visible_texts).
- Header (:386): !s.world_configured -> Segment(kGrey,"| World off"); paired -> Segment(kGreen,"| World L%u paired", s.world_effective); pair_wait 9 with a partner mission -> Segment(kAmber,"| World L%u: partner in %s", s.world_level, short); else Segment(kGrey,"| World L%u waiting (%s)", s.world_level, coop_text::PairWaitName(...)).
- Partner page (:672-675): when !s.world_paired && s.world_pair_wait==9 && s.world_partner_mission && s.world_partner_mission!=s.mission, show the FormatWorldNote(16, s.mission, partner, partner, s.mission) sentence wrapped in amber before the pose lines.
- World page (:755-756): 'handshake %s' becomes 'paired' or 'waiting (<name>)', plus a line 'Partner mission %u (%s)   partner world level %u' when the partner mission != 0.
- Do not edit debug_overlay.cpp or any camera-agent file.
- B11 link_session.hpp:71-73: set kPendingSlots = 4 (== kGuestNonces) and document why (a burst of queued guest HELLOs no longer evicts the slot the first CONFIRM answers).
- B12 Docs:
- docs/link-protocol.md §7.2 (:452-480): the new keys, their gating and order (before time_utc), and the buffer size of 1024 (not 768).
- docs/debug-menu.md: header and World/Partner texts, the new reasons, and the time readback 'sync=world only when the clock is synced (paired in a Mother Base or free-roam scene)'. This updates the :151 and :363 statements.
- docs/steam-online.md: join_ignored toast and F9 result.

**Tests:**
- steam_transport_tests: the existing FormatLine checks (:2122-2233) pass unchanged. That proves the zero defaults of the new LinkView fields.
- steam_transport_tests, new FormatLine cases:
- connected with a stale partner view, pair_wait=9, partner_mission=10020 -> exactly 'CO-OP [connected] Snake_Fan - Steam relay 61 ms | partner in story mission 10020'.
- pair_wait=9 with a fresh native partner view -> today's '| native; gun matched'.
- pair_wait=11 -> today's text.
- reconnecting with left=1, reason 'bad_code', verified=0 -> 'CO-OP [the host has a new code] paste it in F9 > Partner / Co-op > Online'.
- left=1 with verified=1, or another reason -> the old left text.
- steam_transport_tests coop_text: FormatMission and FormatMissionShort for 0, 1, 30010, 30020, 30050, 40010, 40020, 40050, 10020, 11043 and 12345. PairWaitName for 0..11 and 200 ('unknown'). FormatWorldNote exact sentences for every code and variant; code 1 with b=0 and code 7 return false. The longest variant (code 16 with two helicopter names) is under 200 chars.
- steam_transport_tests toasts: PostWorldNote(2,30010,0,30050,40050) followed by Take -> FormatToast gives the exact heli_wait text with 'now in Mother Base'. join_ignored gives the exact text and tone warn. A Model with a repeated identical world_note extends one toast and does not create a new one.
- link_udp_tests (:518-535 pattern):
- After partner_net::PublishWorldState({3,false,30010,9,2}), the written link-status.json contains '"world":3,"paired":0,"partner_mission":30010,"pair_wait":"different_mission","progress":"story_start","time_utc"'.
- The existing substring '"partner_shown":1,"look":1,"mission":30050' still matches.
- PublishWorldState({0,...}) gives '"world":0,"paired":0,"partner_mission":0,"pair_wait":"off"'.
- debug_menu_tests (:686-704): existing assertions are unchanged. New ones:
- mission=10020, gameplay_ready=false -> 'mission'; mission=0 -> still 'gameplay'.
- partner_goto with world_pair_wait=9, world_partner_mission=30010, mission=30050, !world_paired -> 'partner_elsewhere'.
- The same with world_pair_wait=2 -> 'partner_pose'.
- link_session_tests (near :457-520): deliver 4 queued guest HELLOs to the host in one burst before the guest processes any WELCOME. The guest commits on the first matching WELCOME, the host accepts its CONFIRM, there are zero wrong_session drops, and the existing drop-count assertions (:542-546, :721-723) are unchanged.

### C: Lua follow/hold/title progress/sticky arm, lab goto delegation, F9 Lua fixes
**Owns:** lua/coop/world_sync.lua, lua/coop/world_follow.lua, lua/coop/world_goto.lua, lua/coop/world_align.lua, lua/coop/debug_menu.lua, tests/world_follow_lua_check.lua, tests/world_follow_game_lua_check.lua, tests/world_sync_lua_check.lua, tests/world_goto_lua_check.lua, tests/debug_menu_lua_check.lua, tools/worker/scenarios/heli-alone.txt

**Depends on:** 

**Changes:**
- C1 world_sync.lua: after W.log (:31-33) add W.note(code,a,b) (K16). It uses W.n for each value and returns false without a receipt when any value is nil or negative; it sends PRE.."NOTE_V1' -- "..code.." "..a.." "..b and returns '== true'. After W.cfg (:68-74) add W.peer() exactly as in K16. world_sync.lua stays below 16,000 bytes (LF).
- C2 world_follow.lua helpers (near :10): add 'local function note(code,a,b) if type(W.note)=="function" then pcall(W.note, code, a or 0, b or 0) end end' and 'local function peer() return type(W.peer)=="function" and W.peer() or nil end'. Add F.following(p): true when F.outgoing exists, F.outgoing[4]==W.mission, F.outgoing.ack~=0 and ~=2, F.outgoing.session~=nil and ==p.session, and (p.mission==0 or p.mission==F.outgoing[3]). Add F.alone(): no F.session() -> 1; p=peer(); p==nil -> nil (legacy hold); not p.linked or p.level<3 -> 1; p.mission~=0 and p.mission~=W.mission and not F.following(p) -> 2; else nil.
- C3 world_follow.lua H[OP.FOLLOW_ACK] (:109-111): keep the log line and add 'if W.host and F.outgoing and v[1]==F.outgoing[1] then F.outgoing.ack = v[3] end'.
- C4 world_follow.lua F.clear (:147-150): stop clearing F.hostDeploy (F.tick owns its lifecycle). Everything else stays.
- C5 world_follow.lua OnEndMissionPreparation wrapper (:279-293), host branch (inside 'if on() and W.host'):
(a) Second press while held: if F.hostDeploy and F.hostDeploy.mission==W.mission then, when now-hd.at>=1000, clear it, set F.deploying=false, log deploy_alone reason=repeat, note(3,target or 0,3) and return F.originalDeploy(...) with the NEW args. Under 1000 ms, return and keep the hold.
(b) Unsupported or emergency target: let together = W.paired or F.alone()==nil, r = F.refused, rep = r and r.target==target and r.mission==W.mission and now-r.at>=1000 and now-r.at<=30000.
  - If together and not rep: keep the existing refused log line. If not (r and r.target==target and now-r.at<1000), set F.refused={target=target, mission=W.mission, at=now} and note(4,target or 0,0). Return.
  - Otherwise: F.refused=nil, F.deploying=false, log deploy_alone with reason repeat or no_partner/partner_elsewhere, note(3,target or 0, rep and 3 or F.alone() or 1), and return the original.
(c) Unpaired: if not W.paired, why=F.alone(); if why, set F.deploying=false, log deploy_alone, note(3,target or 0,why) and return the original.
(d) Hold, either unpaired with why==nil or paired with eff<3 as today: F.hostDeploy={...}, n=select('#',...), mission=W.mission, target=target or 0, at=now, checkAt=now, notedAt=now. Log 'step=deploy_wait;reason=ready;target=..;partner=..' and, when unpaired, note(2,target or 0,0). Return.
The guest branch is unchanged.
- C6 world_follow.lua suppressed-wrapper refusal for ReserveMissionClear/AbortMission with an unsupported nextMissionId (:263-269): apply the same together/repeat rule and notes as C5(b) through one shared local function. On refusal it returns false for spec[2] exactly as today; when allowed it calls original(...). Reload, ExecuteReload and ReturnToMission refusals are unchanged.
- C7 world_follow.lua F.tick (:172-177 block replaced; it runs before 'if not on()'):
- title tick: T=F.title; if T and T.story and now-T.at>=15000, then clear F.title if gvars.ini_isTitleMode~=true, else set T.at=now and note(1,T.mission,1).
- hold: hd=F.hostDeploy.
  - hd.mission~=W.mission or not W.host: drop it and log deploy_held stale.
  - W.paired or not F.enabled: clear it, set F.deploying=F.enabled and W.paired and W.eff>=3, log deploy_alone reason=disabled when not F.enabled, and run(F.originalDeploy, unpack(hd,1,hd.n)).
  - now-hd.checkAt>=500: hd.checkAt=now; why=F.alone() or (now-hd.at>=120000 and 4 or nil). If why: clear it, set F.deploying=false, log deploy_alone, note(3,hd.target,why) and run the original. Otherwise, if now-hd.notedAt>=3000, set hd.notedAt=now and note(2,hd.target,0).
- C8 world_follow.lua F.tick guest disarm, inserted after 'if W.host then F.pump(); return end' (:179): if F.armed and not W.paired and #F.queue==0 and not F.current and now-(F.disarmAt or -1e12)>=1000, then set F.disarmAt=now and p=peer(), gone = p and p.mission~=0 and not SUPPORTED[p.mission]. If gone or now-(F.unpairedAt or now)>=120000, set F.armed=false and log 'step=disarm;reason=partner_mission|unpaired_timeout;partner=..'.
- C9 world_follow.lua hooks:
- startHooks (:219-233): add F.hostDeploy=nil, F.refused=nil, F.disarmAt=nil, and 'if F.armed then F.unpairedAt=W.now() end'.
- Title progress in the same startHook: F.title=nil. If floor(W.mission/10000)==4 and W.get(gvars,'ini_isTitleMode')==true, then s = the storySnake read (pcall of TppDefine.STORY_SEQUENCE.CLEARD_ESCAPE_THE_HOSPITAL being a number and TppStory.GetCurrentStorySequence()==it; pcall failure gives nil, following character.lua:104-107). If s~=nil, set F.title={mission=W.mission, story=s, at=W.now()}, note(1,W.mission,s and 1 or 0) and log 'step=progress;title=..;story_start=0|1'.
- pairedHooks (:234): add F.unpairedAt=nil.
- unpairedHooks (:235-240): set F.unpairedAt=W.now() for every reason, before the existing reason 1/5 F.clear.
- The file may grow past 16,000 bytes; the CMake two-literal split (CMakeLists.txt:363-376) handles up to 32,000.
- C10 world_goto.lua step 2 (:82-99):
- Remove the pre-wait 'if W.follow and W.follow.enabled and W.host and not W.paired then ... end' (:84-87); the follow wrapper's hold now decides.
- In the 'if g.check then' readback branch (:49-53): if W.follow and W.follow.hostDeploy, then the first time set g.held=true and call ack(g,2,'waiting;reason=held'); return without readback.
- Timeout (:46): step 1 is 30000; step 2 is 150000 when g.held, else 60000.
- The lab-only gates (W.mode==1, refusal reasons) are unchanged.
- C11 world_align.lua A.reset (:85): change log("step=wait;reason=checkpoint_alignment") to log("step=wait;reason=unpaired"). A.start keeps 'host_snapshot'.
- C12 debug_menu.lua (hard limit 16,000 bytes; about 435 free, keep the delta under 300):
- sync() (:111-116): for kind=='env', on = W.paired==true and type(get(get(W,'env'),'clockOn'))=='function' and W.env.clockOn()==true. The non-env branch is unchanged. The output strings stay exactly 'sync=world' and 'sync=none'. Shorten the :109-110 comment if needed.
- Partner branch (:242): local Wd=get(e,'MgsCoopWorld'); lone = ok and (tonumber(get(Wd,'level')) or 0)>=1 and get(Wd,'active')==true and get(Wd,'paired')~=true. Return ok, ok and (lone and 'partner_applied;partner=not_paired' or 'partner_applied') or 'partner_refused'.
- C13 New tools/worker/scenarios/heli-alone.txt. Its header gives the run command 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/heli-alone.txt -SaveSeed real -GraphicsQuality low' and the evidence (see hidden_runtime_checks). Steps: 'labsetting coop_hud=full', 'title 200', 'wait 4000', 'waitgameplay host 150', 'waitgameplay guest 240', 'guest worldtrigger world=0', 'wait 3000', 'host worldtrigger goto=30020', 'waitmission host 40050 120' (it passes the ACC tips screen with the guarded resume and returns when the ACC runtime starts; waitmission accepts a helicopter-space mission on the runtime-phase loading metadata, because 40xxx never reports gameplay_ready), 'wait 8000', 'host capture heli-hold', 'waitmission host 30020 300', 'waitgameplay host 120', 'host capture heli-alone-arrived', 'guest capture guest-stayed'.

**Tests:**
- world_follow_lua_check fixture (:45-60): add 'W.note=function(code,a,b) c.notes[#c.notes+1]={code,a,b}; return true end' (c.notes={}) and 'W.peer=function() return c.peer end' (nil by default, so legacy). The existing checks at :169-175, :214-220 and :226-231 must pass unchanged.
- world_follow_lua_check, new host-hold cases:
- link=0 and unpaired: a deploy calls the original at once, hostDeploy stays nil, notes contain {3,40050-target,1} and the log has 'deploy_alone;reason=no_partner'.
- c.peer={mission=10020,level=3,wait=9,linked=true,session=55}: deploys at once with note {3,t,2}.
- Following: the host publishes 30050->40050 (W.mission and vars set to 40050), with c.peer={mission=30050,...,session=55} -> held. FOLLOW_ACK {id,40050,2} with peer 40050 -> still held; W.paired=true -> released with F.deploying true. A separate run with FOLLOW_ACK status 0 -> tick after 500 ms releases alone with reason 2.
- A second deploy 1000 ms after the hold calls the original once with the second call's args and note {3,t,3}. A second call within 999 ms changes nothing.
- c.time+=120000 -> releases with note {3,t,4}.
- W.mission changed while held -> hostDeploy dropped, no original call.
- c.time+=3000 -> one more note code 2.
- world_follow_lua_check, unsupported target (story fixture, paired):
- The first press is refused with note {4,10020,0} (the existing check at :231 still holds).
- A press 500 ms later is refused again with no new note.
- A press 1000 ms later calls the original with note {3,10020,3}.
- After 30001 ms it is refused again.
- link=0 and unpaired: allowed at once with note {3,10020,1}.
- ReserveMissionClear{nextMissionId=10020} follows the same rule (the :227 check still holds).
- world_follow_lua_check sticky arm:
- An armed guest, unpairedHooks(4), empty queue: F.tick at +119999 keeps F.armed; at +120000 it disarms and logs 'step=disarm;reason=unpaired_timeout'.
- A queued TRANSITION keeps F.armed past 120 s.
- c.peer mission 10020 disarms on the next 1 s check with reason partner_mission.
- Unpaired reason 1 still clears through F.clear (the :184-188 check is unchanged).
- world_follow_lua_check title progress:
- With gvars.ini_isTitleMode=true, W.mission=40010, TppDefine.STORY_SEQUENCE.CLEARD_ESCAPE_THE_HOSPITAL=100 and TppStory.GetCurrentStorySequence returning 100: startHooks -> note {1,40010,1}; +15000 -> repeated; ini_isTitleMode=false -> F.title cleared and no repeat.
- Sequence 200 -> one note {1,40010,0} and no repeat.
- TppStory nil -> no note.
- world_follow_game_lua_check fixture W (:144-163): add a W.note recorder and leave W.peer nil. The title Continue (:178-205) and level0/inactive Continue (:285-299) checks pass unchanged.
- world_sync_lua_check loadstring stub (:87-136):
- Accept PRE.."NOTE_V1'": check the integer-only grammar like TX, record c.notes and return true.
- The CFG stub returns 11 values when options.peer is set, e.g. {mission=30010, level=3, wait=9}.
New checks:
- W.note(2,30010,0) sends exactly "return 'MGS_COOP_WORLD_NOTE_V1' -- 2 30010 0" and returns true; W.note(2,-1,0) and W.note(2,1.5,0) send nothing and return false.
- W.peer() is nil with the default 7-value CFG.
- With options.peer, W.peer() returns mission 30010, level 3, wait 9 and linked==true; with wait 3, linked==false.
- Section 14 (lab goto) checks are unchanged.
- world_goto_lua_check: rewrite :30-32 into two checks. (1) Unpaired step 2 at the ACC calls SelectNextMissionHeliStartRoute+OnEndMissionPreparation (calls==2) without a goto pre-wait. (2) A stub OnEndMissionPreparation that sets W.follow.hostDeploy={} keeps the request pending with exactly one GOTO_ACK 2 ('reason=held') and no readback; clearing hostDeploy gives ack 1 on the next tick; a hold older than 150000 ms is cancelled with reason=timeout. The other checks (:21-29, :33-46) must still pass.
- debug_menu_lua_check:
- The :641 fixture becomes {paired=true, eff=3, env={clockOn=function() return true end}} and :642 still expects 'time_readback;sync=world'.
- New negative case: clockOn returns false -> 'time_readback;sync=none'. :640 is unchanged.
- The partner checks at :231 and :251-254 are unchanged ('partner_applied').
- New: e.MgsCoopWorld={level=3, active=true, paired=false} -> 'partner_applied;partner=not_paired'; {level=0} or nil -> 'partner_applied'.
- The :9 size check (<=16000) still passes.
- world_align_lua_check: no assertion depends on the wording (verified: no 'checkpoint_alignment' references in tests/docs/scenarios).

### D: PowerShell tooling, Play-Coop status line, Share-Save/Install/Collect-Logs, friend docs
**Owns:** tools/CoopPlay.psm1, tools/friend/FriendCommon.psm1, tools/friend/Install-Coop.ps1, tools/friend/Share-Save.ps1, tools/friend/Collect-Logs.ps1, tools/friend/Play-Coop.ps1, tools/friend/FRIEND-GUIDE.txt, tests/friend_package_tests.ps1, docs/two-pc-play.md

**Depends on:** 

**Changes:**
- D1 CoopPlay.psm1: add a pure, exported Get-CoopMissionName -Mission [UInt32] after Get-CoopValue (:1071-1078), with the K11 long table. ASCII only, Windows PowerShell 5.1-safe.
- D2 CoopPlay.psm1 Format-CoopStatusLine:
- Stale code (:1117-1119): for role guest with left=1, state!='connected', reason=='bad_code' and [int]verified==0, append ' - the host restarted with a new code: paste it in F9 > Partner / Co-op > Online' instead of ' - the host left; waiting for them to come back'. The host branch is unchanged.
- Mission segment (:1159-1163), K20: with $partnerMission=[UInt32]partner_mission and $wait=[string]pair_wait, the connected+different case appends ' - your partner is in '+(Get-CoopMissionName $partnerMission)+'. Different missions never pair: meet in the same Mother Base, free roam or helicopter.'. Otherwise the body suffix applies only when the mission is not in @(0,1,40010,40020,40050) and not in CoopPartnerMissions. Then, if progress=='story_start', append ' Your co-op save is at the story start: quit, then import your partner''s save with Share-Save.bat (option I).'.
- D3 FriendCommon.psm1:
- Get-TextFromLastMarkers -Text -Marker -Count, next to Get-TextFromLastMarker (:211-221): the text from the line start of the Count-th last occurrence, or the whole text if there are fewer.
- Get-FriendImportedSaveInfo -SaveDir -ReplacedRoot (K18): returns $null or [pscustomobject]@{Source='marker'|'folder'; Imported='yyyy-MM-dd HH:mm'; MarkerPath; Folder}.
- Get-FriendSaveListing -ModDirectory -MaxEntries 500 (K19 rows).
All are exported by the existing *-* rule.
- D4 Share-Save.ps1:
- $interactive (:51) becomes Test-FriendInteractive -NonInteractive:$NonInteractive.
- Export re-check (:143-145): really compare each entry's sha256 with the manifest (the comment already promises this).
- Import: after the progress files are copied into $newDir (:240-244) and before Move-CurrentAside (:246), write $newDir\imported-progress.json (K18) with Write-FriendTextFile. The tree swap carries it, so [R] semantics need no change.
- Done text (:253-256): add 'Keep the zip: if this copy is ever replaced, import it again.'.
- D5 Install-Coop.ps1 reseed block (:317-339):
- Compute $imported = Get-FriendImportedSaveInfo -SaveDir $saveDir -ReplacedRoot (Join-Path $paths.Mod 'coop-saves-replaced') when $alreadySeeded -and !$reseed.
- If $imported (both the !$state branch :323-328 and the $state branch :329-333): print 'Your co-op save copy holds your partner''s progress (imported with Share-Save on <Imported>).' and 'Replacing it with a fresh copy of your real save removes that progress from co-op games (it is only moved aside; Share-Save [R] cannot bring it back).'.
  - Interactive: $reseed = (Ask 'Replace it anyway? [y/N]') -match '^(y|yes)$' -and (Ask 'Type REPLACE to confirm (anything else keeps your partner''s progress)') -ceq 'REPLACE'. When it is not replaced, print 'Kept the co-op save copy with your partner''s progress.'.
  - Non-interactive: keep the copy and print 'Kept the co-op save copy with your partner''s imported progress (run with -ReseedSaves to replace it).'.
- When there is no import, today's two branches are unchanged.
- After Copy-SaveSeed (:335-338), if it reseeded and $saveDir\imported-progress.json exists, Move-Item it to $seedArchive\imported-progress.json and print 'Your partner''s imported progress was moved to <seedArchive>. To play on it again, import your partner''s zip again with Share-Save.bat (option I).'.
- Copy-SaveSeed itself is NOT changed.
- D6 Install-Coop.ps1 closing text (:504-511): after the Next: lines, add 'Different progress? Share one save first with Share-Save.bat (FRIEND-GUIDE.txt section 3).'.
- D7 Collect-Logs.ps1:
- The header comment (:7-18) lists coop-saves.txt and 'the last 3 sessions'.
- script-observer.log (:98-101): Get-TextFromLastMarkers -Count 3 with Limit-TextTail -MaxChars [Math]::Min(16777216, 3*$MaxLogBytes).
- After the allow-list (:107-111): Save 'coop-saves.txt' ((Get-FriendSaveListing -ModDirectory $paths.Mod) -join "`r`n") and add it to $included.
- summary.txt (:142-162): add the 'coop_saves=' line (K19).
- Do not collect link-secret.txt, steam-join.ini or any save bytes.
- D8 Play-Coop.ps1: before the status loop line (:647), add Write-Host 'Not in the same place? Both games must Continue into the same Mother Base or free roam; Share-Save.bat gives both players the same progress (FRIEND-GUIDE.txt section 3).'. Keep the existing :647 text byte-identical.
- D9 FRIEND-GUIDE.txt (ASCII, CRLF via package.ps1, no renumbering, no 'SteamID64'):
- Replace the section 1 saves bullet (:94-96).
- Add a first bullet to IN SHORT (:138-146).
- Add the subsection 'SAME PROGRESS FOR BOTH (Share-Save; do this before your first game):' inside section 3, before EXPERIMENTAL SHARED WORLD (:270).
- Add the helicopter-hold bullet to EXPERIMENTAL SHARED WORLD.
- Replace the section 6 :348 bullet with the five bullets in user_visible_texts.
- Add the Share-Save/Install-Coop bullet to section 7 (:362-370).
- The :239 and :244 status-line examples stay byte-identical.
- D10 docs/two-pc-play.md:
- The HUD lines at :313-316 (K-HUD texts).
- The status-line explanation (different missions, story start, host new code).
- A Share-Save 'same progress' step before Quick start.
- The helicopter hold rule and the :183 'Partner body unavailable' note (no longer shown at the title or in the helicopter).

**Tests:**
- friend_package_tests status line (:513-535):
- :524 (10020 body text) is unchanged.
- :532 becomes '[JOIN] link=connecting | partner=none | mission 0'.
- :533-535 are unchanged.
- New: 40050 and 1 end with 'mission 40050' / 'mission 1' and no suffix.
- New: connected with partner_mission=30010, pair_wait=different_mission, mission=10020 gives the exact different-missions line.
- The same with state=connecting adds no explanation.
- progress=story_start appends the exact story sentence.
- Guest left=1, state=reconnecting, reason=bad_code, verified=0 gives the new-code clause; with verified=1 the old clause; the host role keeps 'your friend left'.
- Exact connected lines at :467, :516, :539 and :1594 are unchanged.
- friend_package_tests Get-CoopMissionName: every K11 long-table row.
- friend_package_tests guide (:901-916): add needles 'Share-Save.bat' and 'SAME PROGRESS FOR BOTH'. The :906-915 exact-quote checks still pass. The forbidden needles (:905) still pass. ASCII/CRLF/5.1 parse (:859-870) passes.
- friend_package_tests Share-Save, new block after the installer fixtures (~:1011):
- Export from the host fixture with '-Mode Export -NonInteractive -OutputDirectory'.
- Import into the join fixture with '-Mode Import -ZipPath -NonInteractive'.
- coop-saves\imported-progress.json then exists with source 'share-save' and file hashes equal to the zip manifest; settings files are kept; coop-saves-replaced\*-before-import exists.
- Restore ('-Mode Restore -NonInteractive') moves the marker aside with the imported tree.
- Real saves are hash-unchanged.
- friend_package_tests Install-Coop:
- With a seeded copy containing imported-progress.json, Invoke-FriendAnswers @('1','','y','y','KEEP') keeps the copy and the marker.
- @('1','','y','y','REPLACE') reseeds and moves the marker to coop-saves-replaced\<stamp>\imported-progress.json, and the output names Share-Save.
- A non-interactive reinstall with the marker keeps the copy and prints the -ReseedSaves hint.
- The folder fallback: a newest '*-before-import' dir and no marker is detected the same way.
- The existing :1013-1019, :1177-1184 (answers '1','','y','y' with no import present) and :1668-1670 checks are unchanged and pass.
- friend_package_tests Collect-Logs (:1602-1635):
- The fixture script-observer.log gets 4 adapter starts. The collected log starts at the 3rd-last one and excludes the first session's unique line (replaces the :1629 'last adapter start' assertion).
- The leak checks (:1624-1627) stay.
- coop-saves.txt is listed in the collected files (:1620). It contains 'coop-saves/287700/local/COOP_PROGRESS<TAB>' with a size and no file contents (assert the fixture content string is absent).
- summary.txt contains 'coop_saves=files='.
- The zip stays under 400KB (:1616).
- friend_package_tests: the Play-Coop source substring checks (:644-648) are unchanged, and the package file lists (:735-749, :788-791) are unchanged because there are no new files under tools/friend.

## user_visible_texts
- Toast (note 16, both PCs, after 15 s in different missions): 'Your partner is in <partner long name>; you are in <your long name>. Co-op only pairs when you are both in the same Mother Base, free roam or helicopter.'
- Toast (note 1, title helicopter, story start): 'Your co-op save is at the story start, so Continue goes to a story mission that co-op cannot join. Quit and import your partner's save with Share-Save.bat (option I).'
- Toast (note 2, host hold, partner known elsewhere): 'Waiting up to 2 min for your partner (now in <long name>) to reach this helicopter. Choose the deployment again to fly alone.'
- Toast (note 2, host hold, partner loading or in this helicopter): 'Waiting up to 2 min for your partner to reach this helicopter. Choose the deployment again to fly alone.'
- Toast (note 3 b=1): 'No co-op partner can come on this flight: flying alone.'
- Toast (note 3 b=2, partner known): 'Flying alone: your partner is in <long name>. Meet in the same Mother Base or free roam to play together.'
- Toast (note 3 b=2, partner unknown): 'Flying alone: your partner is in another mission. Meet in the same Mother Base or free roam to play together.'
- Toast (note 3 b=3): 'Flying alone, as you chose the deployment again.'
- Toast (note 3 b=4): 'Your partner did not reach this helicopter within 2 min: flying alone.'
- Toast (note 3, other b): 'Flying alone.'
- Toast (note 4, unsupported destination): 'Your partner cannot follow you there: co-op travel only goes to Mother Base and free roam. Choose the same destination again within 30 s to go alone.'
- Toast (join_ignored): 'That code was not used: you are already connected or connecting to a host.'
- HUD status line, connected and in different missions: 'CO-OP [connected] <name> - <route> <rtt> ms | partner in <short name>'
- HUD status line, reconnecting after the host left with an unverified bad_code: 'CO-OP [the host has a new code] paste it in F9 > Partner / Co-op > Online'
- F9 header segments: '| World L<n> paired', '| World L<n>: partner in <short name>', '| World L<n> waiting (<pair_wait name>)', '| World off'
- F9 Partner page, different missions: the note-16 sentence in amber.
- F9 World page: 'handshake paired' / 'handshake waiting (<pair_wait name>)', plus 'Partner mission <n> (<long name>)   partner world level <n>'
- F9 reason 'mission': 'F9 commands work only in Mother Base and Afghanistan/Africa free roam, not in story missions or the helicopter.'
- F9 reason 'partner_elsewhere': 'Your partner is in another mission. Meet in the same Mother Base or free roam first.'
- F9 Online result (busy): 'That code was not used: you are already connected or connecting.'
- F9 Online result (own code): 'That code is your own; ask your friend for theirs.'
- F9 command results (Lua): 'partner_applied;partner=not_paired' (world sync on but not paired); 'time_readback;sync=none' when the clock is not actually synced. 'time_readback;sync=world' and 'submitted_weather_<w>;sync=world' are unchanged otherwise.
- Play-Coop status suffix, different missions: ' - your partner is in <long name>. Different missions never pair: meet in the same Mother Base, free roam or helicopter.'
- Play-Coop status suffix, story start: ' Your co-op save is at the story start: quit, then import your partner's save with Share-Save.bat (option I).'
- Play-Coop status clause, guest with a stale code: ' - the host restarted with a new code: paste it in F9 > Partner / Co-op > Online'
- Play-Coop start hint: 'Not in the same place? Both games must Continue into the same Mother Base or free roam; Share-Save.bat gives both players the same progress (FRIEND-GUIDE.txt section 3).'
- Install-Coop lines (import detected): 'Your co-op save copy holds your partner's progress (imported with Share-Save on <yyyy-MM-dd HH:mm>).' / 'Replacing it with a fresh copy of your real save removes that progress from co-op games (it is only moved aside; Share-Save [R] cannot bring it back).' / prompt 'Replace it anyway? [y/N]' / prompt 'Type REPLACE to confirm (anything else keeps your partner's progress)' / 'Kept the co-op save copy with your partner's progress.' / 'Kept the co-op save copy with your partner's imported progress (run with -ReseedSaves to replace it).' / 'Your partner's imported progress was moved to <folder>. To play on it again, import your partner's zip again with Share-Save.bat (option I).'
- Install-Coop closing line: 'Different progress? Share one save first with Share-Save.bat (FRIEND-GUIDE.txt section 3).'
- Share-Save import done line: 'Keep the zip: if this copy is ever replaced, import it again.'
- Collect-Logs coop-saves.txt header: '# MGS Co-op save folders: names, sizes and times only (no save contents)'
- FRIEND-GUIDE section 1 saves bullet: '- Saves that can "Continue" into the same supported scene: Mother Base, Afghanistan/Africa free roam, or the corresponding helicopter. The easiest way: one player shares their save with Share-Save.bat (section 3, SAME PROGRESS FOR BOTH). Each installer copies its own player's save.'
- FRIEND-GUIDE IN SHORT first bullet: ' - Before the first game: if your saves are at different points of the story, share one with Share-Save.bat (SAME PROGRESS FOR BOTH below).'
- FRIEND-GUIDE subsection 'SAME PROGRESS FOR BOTH (Share-Save; do this before your first game):' with these bullets:
- Co-op only pairs when both games are in the same Mother Base, free roam or helicopter. A save at the very start of the story (just after the hospital) always Continues into a story mission that co-op cannot join.
- The player whose save already reaches Mother Base and free roam: quit MGSV, double-click Share-Save.bat and press E. It saves mgs-coop-save-<time>.zip on the Desktop. Send that file to the other player.
- The other player: quit MGSV, double-click Share-Save.bat, press I and drag the zip into the window. Your own co-op copy is kept aside, and Share-Save.bat [R] brings it back. Your real saves are never touched.
- Keep the zip. If Install-Coop.bat later asks to replace your co-op save copy, answer N; if it was replaced anyway, import the zip again.
- MGSV saves may be tied to the Steam account. If the game does not accept the imported save (an error, or a new game at the title), run Share-Save.bat and press R.
- FRIEND-GUIDE shared-world bullet: ' - The host's helicopter waits up to 2 minutes for a partner who can come and says so on screen; choose the deployment again to fly alone. With no partner connected, or the partner in another mission, it leaves at once.'
- FRIEND-GUIDE section 6 bullets (replacing :348):
- '"Different missions never pair" (Play-Coop) or "partner in ..." (in game): your partner is in another mission. Meet in the same Mother Base, free roam or helicopter. If one game always Continues into a story mission, share a save (section 3, SAME PROGRESS FOR BOTH).'
- '"Partner body unavailable in this mission": you are in a story mission; meet in Mother Base or free roam.'
- '"Your co-op save is at the story start": see section 3, SAME PROGRESS FOR BOTH.'
- '"the host restarted with a new code" / "[the host has a new code]": paste the host's new code in F9 > Partner / Co-op > Online; your game can keep running.'
- '"That code was not used": you are already connected or connecting.'
- FRIEND-GUIDE section 7 bullet: '- Share-Save.bat (section 3) copies progress between the two players' co-op copies only. Install-Coop.bat asks twice before it replaces a co-op copy that holds your partner's imported progress: answer N to keep it.'

## hidden_runtime_checks
- H0 Preconditions:
- Work only in D:/mgs-coop-wt/join-fix.
- Leave MGS_GAME_LUA_DIR unset (it defaults to <worktree>/build) and never point it at C:/Users/taylo/OneDrive/Documents/ChatGPT/mgs-coop.
- Take the verify lock: mkdir C:\Users\taylo\AppData\Local\Temp\mgs-coop-verify.lock plus owner.txt '<name> <worktree> <ISO time>'. Check owner.txt first, because a join-fix CTest held it at 11:04.
- Run one worker session at a time (Local\MGSCoopBackgroundSession).
- Never launch the game on the visible desktop.
- H1 Full CTest:
- Command: 'pwsh -File tools/build.ps1 -Configuration Release -ReleaseGun' (build/native).
- All tests pass, including world_wire_tests, world_receipt_tests, link_udp_tests, steam_transport_tests, link_session_tests, debug_menu_tests, world_follow_lua_check, world_follow_game_lua_check, world_sync_lua_check, world_sync_composed_lua_check (splices the >16,000-byte world_follow.lua), world_goto_lua_check, debug_menu_lua_check, friend_package_tests and lab_scripts_tests.
- Then release the lock.
- H2 Worker checks: 'pwsh -File tools/worker/Invoke-WorkerChecks.ps1 -Profile Normal -EnableBackgroundWorker' passes; Invoke-WorkerScenario also runs it when no -CheckReceipt is given. No allow-list edit is needed because no CTest names are new.
- H3 Follow regression through the new hold:
- Command: 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/pack-mafr-real.txt -SaveSeed real -GraphicsQuality low'.
- Both roles reach 30020.
- Host script-observer.log shows 'world_lua=event=follow; step=deploy_wait; reason=ready; target=30020', unless the guest paired in the ACC first. Then 'world=paired; ...; mission=40050' and 'step=tx; ...; next=30020; kind=6'.
- Guest shows 'step=replay; ...; kind=6' and 'step=arrived'.
- There is no 'deploy_alone' line.
- Both <game>\mgs-coop\link-status.json files contain '"world":3,"paired":1,"partner_mission":30020,"pair_wait":"paired"' at the end.
- 'world=pair_wait; reason=' lines appear only around the transitions.
- No note-16 toast appears in captures (the travel stays under 15 s or is masked by settling).
- H4 New bounded-hold scenario:
- Command: 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/heli-alone.txt -SaveSeed real -GraphicsQuality low'.
- Host log: 'step=deploy_wait; reason=ready; target=30020', then 'world=note; code=2; a=30020; b=0', then about 120 s later 'step=deploy_alone; reason=timeout; target=30020' and 'world=note; code=3; a=30020; b=4'.
- captures.jsonl heli-hold: host hud.toast starts with 'Waiting up to 2 min for your partner'.
- The host reaches 30020; the guest stays in 30050 (guest-stayed capture mission 30050).
- 'world_goto=failed; reason=acc_timeout' on the host is expected and harmless.
- The scenario total stays under 600 s.
- H5 HUD regression and title progress:
- Command: 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/coop-hud.txt'. The hud.line keeps 'CO-OP [connected] ... - lab link <n> ms' and the old toasts.
- Both logs contain 'world=note; code=1; a=40010; b=0' from the title helicopter.
- Both link-status.json files contain '"progress":"ok"'.
- Repeat with '-LabSetting coop_hud=off': hud.enabled false, and link-status still has the new keys.
- H6 Play rehearsal regression:
- Command: 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -Mode play -PartnerProfile native -NativeStage gun -BuildProfile research -SaveSeed lab -ScenarioPath tools/worker/scenarios/play-native.txt'.
- It passes as before; link-status shows '"world":3,"paired":1' in 30010; no world_note toasts.
- H7 Readback anchors: 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/pack-mb-real.txt -SaveSeed real' still meets '^submitted_weather_rainy;sync=world$', '^time_readback;sync=world$' and '^submitted_weather_sunny;sync=world$'.
- H8 F9 regression: 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/debug-menu.txt' (free roam, lab seed) still meets '^partner_applied' (paired, so no suffix) and '^time_readback'.
- H9 W3 regressions:
- 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/partner-native-checkpoint.txt' (checkpoint, shared death, host_continue): both roles reach 30010.
- 'pwsh -File tools/worker/Invoke-WorkerScenario.ps1 -ScenarioPath tools/worker/scenarios/spawn-sync.txt': its evidence is unchanged and there is no 'step=disarm' line while paired.
- H10 After every run, scenario.json shows Closed, BaselineRestored and RealSavesUnchanged true, Failure empty, and no IsolationFailed. No imported-progress.json and no new coop-saves-replaced folder may exist under D:\Games\steamapps\common\MGS_TPP\mgs-coop or MGS_TPP_guest\mgs-coop (Phase 1 tooling never runs in the worker).
- H11 Not runnable in the worker; covered by unit tests and to be confirmed in the next real two-PC session:
- the story-start toast (no CLEARD_ESCAPE_THE_HOSPITAL seed exists; do not create one from real saves)
- the Steam stale-code HUD text
- the join_ignored toast and F9 result
- the pending-slot race
- whether a second sortie press is reachable
- the Install-Coop, Share-Save and Collect-Logs behaviour (friend_package_tests)
Before building a friend package, re-run 'Invoke-WorkerChecks -Profile Release', because tools/*.ps1 changes invalidate the Release receipt.

## risks_and_mitigations
- Releasing a held deploy too eagerly could break the verified MB->ACC->free-roam follow (verdict RC4(b)). Mitigation: F.following() keeps the hold while F.outgoing[4]==W.mission, no FOLLOW_ACK 0 or 2 has arrived, the session matches and the partner is still in the source mission or loading. An older loader (no r[9..11]) keeps today's hold. H3 is the gating regression.
- The second deploy press may be unreachable in the sortie UI (unknown). Mitigation: a 120 s hard bound (no shorter, verdict RC4(c)), the 3 s re-posted toast, and immediate release when no partner can come. A 1 s debounce stops double calls from bypassing a refusal or hold.
- The mismatch toast could fire during normal host-led travel. Mitigation: the K4 15 s settle (published 'settling'). The HUD suffix and Play-Coop explanation use only the settled code 9. The raw code is used only by Lua decisions and the log.
- Sticky-arm disarm could reopen guest travel during a very slow host load. Mitigation: disarm only with an empty queue, no current replay, no pairing for 120 s (the timer restarts at each guest mission start), or the partner's mission known to be unsupported. Phase 2's travel lock supersedes this.
- Size limits:
- debug_menu.lua has about 435 bytes left (hard 16,000). Keep the delta under 300; trim the sync comment if needed.
- world_follow.lua crosses 16,000 and takes the existing two-literal split (cap 32,000; world_downed.lua already does). world_sync_composed_lua_check verifies the splice.
- world_sync.lua must stay under 16,000 or also take the split.
- link-status 1024-byte buffers: about 105 extra bytes on a worst case of about 846. FormatStatus fails closed (no file write) on overflow. The keys are compact; link_udp_tests checks a document with the new keys.
- HUD line budget of 112 chars: the different-missions suffix uses short names of at most 22 chars (about 99 chars worst case) and replaces the partner-view suffix instead of appending to it.
- Lock order and threads: world_link computes under the leaf g_world_lock and calls partner_net::PublishWorldState and coop_hud::PostWorldNote only after releasing it. NOTE receipts are honoured only on the runtime and termination threads. partner_net never includes world_link.
- Cross-track compile dependency A->B headers. Mitigation: B lands the three header declarations first (B1). world_wire, world_receipt and their tests are independent, so A can unit-test before the merge.
- The lab goto loses its own pairing pre-wait (world_goto.lua:84-87). Mitigation: the lab-only change delegates to the bounded hold, keeps step 2 pending while held (150 s cap), and is gated by H3. Play mode still refuses goto (world_sync_lua_check :1250-1254 unchanged).
- In the lab the host keeps a stale view of a guest switched to world=0 (no wire signal), so heli-alone exercises the 120 s timeout rather than the immediate alone path. The immediate paths are covered by Lua unit tests.
- The story-start key is CLEARD_ESCAPE_THE_HOSPITAL only (verdict RC1-8). It misses saves before the prologue and after 10020/10030, which is accepted. The warning is never keyed on any 1xxxx mission.
- Install-Coop import detection: the marker is primary; the folder-name fallback covers imports made before the marker existed. The non-interactive default keeps the copy (safe). The marker is moved aside on reseed so it never goes stale (verdict RC1-9/RC6-5). Copy-SaveSeed is untouched (worker_session_tests :344, lab_scripts_tests :1915-1923).
- Collect-Logs privacy: names, sizes and UTC times only, relative paths, Hide-FriendSensitiveText and Hide-CoopNetworkText applied, bounded to 500 rows. Keeping 3 sessions grows the zip, bounded by 3*MaxLogBytes.
- The pending-slot change (kPendingSlots=4) is unverified on Steam relays. It is low risk (memory only) and covered by a link_session unit test. Revert it independently if a real session regresses.
- Old link-status readers: Play-Coop falls back to today's text when the keys are absent. The only intended visible change for old documents is no body suffix for missions 0, 1 and 4xxxx (tests :532 updated).

## open_questions
- Is a 120 s bound for the host helicopter hold acceptable, or does the user prefer a shorter bound now that the hold is visible? The verdict says at least 120 s.
- Can the player actually choose the deployment again after OnEndMissionPreparation was swallowed, or does the sortie UI close? Unknown. Confirm in the next real session; the 120 s bound is the guaranteed escape either way.
- Is the typed 'REPLACE' second confirmation in Install-Coop acceptable, or would the user prefer a second [y/N]?
- Should the armed guest's own swallowed travel (REQ_MISSION, silent today) get a Phase 1 toast, or wait for the Phase 2 travel lock (note code 5 reserved)?
- Should the lab goto change (world_goto.lua no longer pre-waits for pairing) ship in Phase 1? It is what lets the hidden worker exercise the hold. The alternative is to keep goto unchanged and rely on unit tests only for the hold.
- Should the kPendingSlots=4 handshake change ship in Phase 1 (optional, unverified on Steam) or be deferred?
- Should the ReserveMissionClear/AbortMission story-destination refusal (iDroid mission pick in free roam) share the Phase 1 note and 1-30 s repeat escape as specified (C6), or stay silent until Phase 2?
