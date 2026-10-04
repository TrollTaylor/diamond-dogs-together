PARTNER TRANSFORM SYNC: REUSE ASSESSMENT AND MINIMAL DESIGN (read-only analysis, 2026-10-04)

1. COMPONENT INVENTORY

Links into dinput8 today: mgs_coop_probe links mgs_coop_core (session, protocol, typed_payload, input_receiver, steam_packets, player_sync, lab_config, lab_status) and mgs_coop_lab (steam_loopback, udp_transport, ...). The only networking code that actually runs is the Steam loopback emulator plus its UdpTransport. Everything else is called only from tests.

- udp_transport.{hpp,cpp}: REUSE AS-IS for the lab as a second global instance. It is one 127.0.0.1 socket with SO_EXCLUSIVEADDRUSE (cpp:184-188) and its own receive thread: select() bounded by idle_interval, recvfrom drain, sink.on_datagram, and an on_idle timer (341-384). It accepts only 127.0.0.1:peer_port (109-111, 369). transmit() is thread-safe and non-blocking from the game thread (WSA_FLAG_OVERLAPPED fix 162-168, FIONBIO 182, test steam_loopback_tests.cpp:960-985). It has latency/loss injection. It is constant-init and trivially destructible. For two-PC direct UDP it needs bind_address/peer_address fields (lines 188, 219, 329, 109).
- steam_loopback.*: REFERENCE ONLY, do not reuse for our traffic.
  - Patterns worth copying:
    - INIT_ONCE lazy start outside DllMain (784-792).
    - A sink that only enqueues under an SRWLOCK.
    - Game-thread sends (wire_send 697-711).
    - on_idle timers (tick 586-627).
    - A bounded trace budget, and a stats line every 10 s only when values change (729-738).
  - Why not to reuse it: its P2P semantics post synthetic 1202/1203 into the game's Steam callback path (556-567, 582, 713). Unaccepted packets are invisible (395-430). A 10 s timeout posts 1203.
- steam_packets.cpp: USE LATER as the two-PC Steam backend. It is the flat-API Send/IsAvailable/Read with gates and needs a bound PeerRouter handle (42). Gaps:
  - It has modes 1 and 2 only (44-49); mode 0 is needed.
  - It has no Accept export.
  - It does one read per poll; we need to drain up to 8 per frame.
  - It has no instance acquisition. Use GetProcAddress(steam_api64,'SteamNetworking'), whose getter bytes the observer already verifies (steam_observer.cpp:385-395).
  
  The game uses only channel 0, verified statically at 0x1A5FBA4/0x1A5FBA8, 0x1A5FAE4 and 0x1A5FC24/0x1A5FC95, so pick a non-zero channel such as 0x4D47.
- typed_payload.cpp: NOT FOR THE SPIKE. Position-only pose roster with slot generations; for later host-authoritative roster/input.
- player_sync.cpp: REUSE. PlayerTransformPacket is 28 bytes inside an MGSC wire::Packet (kind snapshot), 56 bytes per datagram. rot_y must be radians with |rot_y| <= 4π (cpp:21-26). Add fixed-buffer encode/decode (no std::vector, no throw).
- input_receiver.cpp / session.cpp / peer_router.hpp: NOT FOR THE SPIKE. They allocate (std::vector/std::set/std::string) and throw (the Session constructor; protocol::encode at protocol.cpp:15-18). They are designed for host-authoritative admission. Reuse only newer_sequence() (session.cpp:12-15). Keep them for a later phase: reliable events, an input-driven player 2, a load/epoch handshake via Session::begin_load/mark_loaded.
- protocol.cpp: REUSE THE LAYOUT. 'MGSC' header of 28 bytes: magic, version, kind, session u64, epoch u32, sequence u32, length u32. Do not call encode/decode on noexcept paths without try/catch; add fixed-buffer equivalents.
- lab_config.*: EXTEND. Unknown keys are ignored (cpp:173), so adding keys is backward-compatible. ParsePort requires >= 1024.
- game_script_observer.cpp: EXTEND (details below).

2. RECEIPT MECHANICS (answers to the specific questions)

- Arguments in: yes. The selector gets the full original buffer and the name, which equals the buffer for loadstring. The established pattern is a comment suffix: "return 'TAG' -- payload" (character.lua:45, character_readback.lua:28, mission_trace.lua:9; prefix classifier at character_receipt.cpp:12-14, 41-42). Use integers (mm, centidegrees) so that neither Lua's nor the C runtime's locale-dependent float formatting is involved. Lua side: string.format('%d', math.floor(v*1000+0.5)). Parse strictly in native code without strtod.
- Values out: yes. The replacement source is compiled in place of the original (script_load_bridge.cpp:40-41) and may return multiple values or a table. Precedent: "return true,2,0,6" (character_receipt.cpp:81-103, consumed at character_readback.lua:39). Multiple values avoid allocating a Lua table each frame. The buffer must only outlive the call (thread_local precedent: observer:282). Never touch the Lua stack.
- 'Not replaced' signal: if native declines, Lua receives the literal tag 'MGS_COOP_POSE_V1' (a string), which means no data.
- Thread: Lua updates run on the mission runtime thread, pinned at OnMissionGameStart via RUNTIME_ENTER (observer:376-387; session_lifecycle.lua:191-194). Gate every pose receipt on OnRuntimeThread() && !mission_terminating. OnAllocate can run on worker threads.
- Per-frame hook: the e.OnUpdate wrapper appended to TppMain (client_join.lua:98-106). mission_main.lua:10 captures TppMain.OnUpdate after the splice. Live proof: the trigger-file join (testing-2026-10-03.md:17).
- Logging: each replaced receipt writes two lines (observer:928-935, 938-945). Add `thread_local bool quiet_replacement`:
  - Set it in the pose fast path.
  - Return before the logging block at line 928.
  - In ObserveReturn: `if(quiet_replacement){quiet_replacement=false;InterlockedIncrement64(&returns);return;}`.
- Mode: the splices and the thread pin exist only in the host-trial TppMain (bootstrap_patch.cpp:117-216, selected at observer:580-583 when host_enabled && archive_mounted). host-v1 (observer:971) gives that offline: session creation is refused (host_session_trial.cpp:180-181) and runtime adoption succeeds without a session (dynamite_session_port.cpp:119). A new 'offline-v1' alias is optional for clarity.

3. PORTS / COLLISION

Ports already in use: loopback emulator 47810/47811 (lab.ini listen_port/peer_port; tools/lab/lab.json ports.host/guest), fuse TCP 6667, inert Goldberg 47584/47585, tests 48140-48161.

Same-port reuse fails closed. A bind error disables whichever transport starts second (TestIntegrationFailClosed). The emulator starts lazily on the game's first SteamNetworking call, so startup order is nondeterministic. Multiplexing on the MGLB socket would couple us to the emulator's refusal paths.

PROPOSAL: new lab.ini keys:
- net=1
- net_listen_port / net_peer_port: host 47820/47821, guest 47821/47820.

Validation in ParseLabConfig:
- The two net ports must differ.
- Neither may equal listen_port or peer_port.
- On failure: net=false and invalid_values++.

Also:
- Add ports.netHost/netGuest to tools/lab/lab.json.
- Add a UDP preflight for them in Start-Lab.ps1:88-90.
- Emit the keys in LabCommon.psm1 New-LabIniText.

The loopback setting can stay as is; for an offline-only run, loopback=0 is cleaner but not required.

4. PIPELINE DESIGN

Lua (OnUpdate, every frame, 30050 only)
  -> loadstring("return 'MGS_COOP_POSE_V1' -- x_mm y_mm z_mm yaw_cdeg stance flags")()
  -> native fast path on the game thread, receipt_lock held:
     a. ParsePoseArgs. Strict; 6 signed ints; |pos| <= 1e9 mm; stance 0..2; flags 0..15.
     b. Convert yaw from centidegrees to radians normalized to [-π, π].
     c. SubmitLocal. If at least 50 ms have passed since the last send (precise_ms via QPC, as in udp_transport.cpp:146-155), build the 56-byte frame and call transmit() (non-blocking, ~10 µs):
        - session = lab token (constant, e.g. 0x4D4753434F4F5001)
        - epoch = ReadMission() (0x2C01372)
        - seq++
        - tick_ms = QPC ms on the sender
     d. SampleRemote(mission). Shared SRW lock on the ring. Interpolate at render time = now - 100 ms on the sender timeline: offset = min(recv_local - sender_tick) over the window, which removes jitter.
        - Position: lerp. Yaw: shortest-arc.
        - Stance/flags: taken from the newer snapshot.
        - Snap with no lerp if the jump is > 5 m within 200 ms (fulton, respawn, etc.).
        - Past the newest snapshot, extrapolate with the last velocity for at most 100 ms, then hold.
        - Status: 1 = live; 2 = stale (age > 500 ms, hold the last pose); 3 = lost (age > 3000 ms, Lua hides the puppet or announces it).
     e. FormatPoseReply into thread_local char[128]: "return st,x_mm,y_mm,z_mm,yaw_mrad,stance,flags,speed_mmps,age_ms". Integer formatter, no C runtime. Set quiet_replacement and return selected.
     f. If the transport is not running or no snapshot exists for this epoch, return false and do not replace (no log).

Receive thread (UdpTransport sink, must only enqueue):
- Decode the fixed frame: magic, version, kind=snapshot, len=28, session == token, decode_transform validity, seq == header seq.
- Drop if epoch differs from the epoch stored by the game thread's last submit.
- Drop if !newer_sequence(seq, last). Exception: accept as a peer restart when more than 2 s have passed since the last frame.
- Push {pose, sender_tick, recv_ms} into the 16-entry ring under the exclusive SRW lock. No allocation and no game calls.

on_idle every 250 ms:
- If nothing was sent for 1 s (menus, loading), send a keepalive. This can be the last pose with a 'hidden' flag bit, or kind=event.
- Write a stats line every 10 s only if values changed (pattern from steam_loopback.cpp:729-738).

Reset the ring at runtime_enter (accepted) and at termination_returned inside SelectMissionBoundary.

Rate and bandwidth: 20 Hz × (56 + 28 IP/UDP) bytes ≈ 1.7 KB/s. On loopback 30-60 Hz is fine; drop the interpolation delay to about 2 send intervals. Lock order: receipt_lock, then the net lock. The receive thread takes only the net lock, so there is no deadlock.

Lua apply: partner_sync.lua calls the puppet adapter, e.g. MgsCoopPartner.OnPose(st, x/1000, y/1000, z/1000, yaw/1000, stance, flags, speed/1000, age). That adapter belongs to the puppet/player2 area and can use the yaw radians directly for SendCommand Warp/WarpAndWaitBlock. Wrap the call in pcall and deactivate on error (session_feedback.lua:63-70 pattern). Announce 'partner connected' on the first status 1 and 'partner signal lost' on status 3 via TppUiCommand.AnnounceLogView.

Lua sketch (a new file appended in SelectHostTrialPatch after session_feedback):

```lua
do local start,update,terminate=e.OnMissionGameStart,e.OnUpdate,e.OnTerminate
 local active,fmt,floor=false,string.format,math.floor
 local function stance()
  if PlayerInfo.OrCheckStatus{PlayerStatus.CRAWL} then return 2 end
  if PlayerInfo.OrCheckStatus{PlayerStatus.SQUAT} then return 1 end
  return 0
 end
 local function step()
  if not active or vars.missionCode~=30050 then return end
  local f=loadstring(fmt("return 'MGS_COOP_POSE_V1' -- %d %d %d %d %d %d",
    floor(vars.playerPosX*1000+.5),floor(vars.playerPosY*1000+.5),floor(vars.playerPosZ*1000+.5),
    floor(vars.playerRotY*100+.5),stance(),
    PlayerInfo.OrCheckStatus{PlayerStatus.DASH} and 4 or 0))
  local st,x,y,z,ry,sn,fl,spd,age=f and f()
  if type(st)~='number' then return end
  if type(MgsCoopPartner)=='table' then MgsCoopPartner.OnPose(st,x/1000,y/1000,z/1000,ry/1000,sn,fl,spd/1000,age) end
 end
 -- wrap OnMissionGameStart (active=true), OnUpdate (pcall(step)), OnTerminate (active=false) as in session_feedback.lua
end
```

5. MINIMAL CODE CHANGES PER FILE (no edits were made)

- native/include/mgscoop/lab_config.hpp + lab_config.cpp: add fields bool net, uint16 net_listen_port, uint16 net_peer_port; three parse branches near cpp:158; validation near 178. tests/lab_config_tests.cpp: cases for the new keys and collisions. About 15 LOC.
- native/include/mgscoop/player_sync.hpp + player_sync.cpp: add `std::size_t encode_transform_frame(uint64 session, uint32 epoch, const PlayerTransformPacket&, uint8_t (&out)[56]) noexcept` and `bool decode_transform_frame(const uint8_t*, size_t, uint64 expected_session, uint32& epoch, PlayerTransformPacket&) noexcept`. Same bytes as wire::encode(encode_transform(...)), so tests can cross-check with wire::decode. About 50 LOC.
- NEW native/include/mgscoop/pose_receipt.hpp + native/pose_receipt.cpp (portable, mgs_coop_core): ParsePoseArgs(const char*, size_t, LocalPose&) and FormatPoseReply(const RemoteSample&, char*, size_t), with no C runtime. About 120 LOC plus tests.
- NEW native/include/mgscoop/pose_interp.hpp + native/pose_interp.cpp (portable): SnapshotRing<16> with Push(frame, recv_ms) and Sample(now_ms, delay, RemoteSample&) implementing the rules in section 4. Deterministic unit tests. About 150 LOC.
- NEW native/include/mgscoop/partner_net.hpp + native/partner_net.cpp (Windows, add to the mgs_coop_lab sources in CMakeLists): Configure(cfg, log); EnsureStarted() via INIT_ONCE (TransportConfig with idle 250 ms; reuse net_latency_ms/net_loss_permille for testing interpolation); SubmitLocal(); SampleRemote(); ResetRemote(); Stats(). The global lab::UdpTransport plus SRWLOCK plus ring lives here. On a start failure it logs 'partner_net=disabled; reason=transport_<err>' once. About 200 LOC.
- native/udp_transport.{hpp,cpp}: no change for the lab. For approach C only, add address fields (about 15 LOC).
- native/game_script_observer.cpp, about 70 LOC total:
  - Include partner_net.hpp.
  - In InstallGameScriptObserver (near 1050, when lab_active && labc.net): Configure + EnsureStarted and log a 'partner_net=listening; port=..; peer_port=..' line.
  - In ObserveEntry right after the lock (569): the fast path for the prefix "return 'MGS_COOP_POSE_V1' -- ". Bounds: size <= 160, name==buffer (bytewise, as in character_receipt Classify).
  - thread_local quiet_replacement, plus the ObserveReturn early exit (938).
  - ResetRemote() at runtime_enter and termination_returned in SelectMissionBoundary (376-398).
- NEW lua/coop/partner_sync.lua + native/partner_sync_source.hpp.in. bootstrap_patch.cpp SelectHostTrialPatch: include it, add sizeof to result_size (147), memcpy it after session_feedback (198), update the static_assert chain. CMakeLists.txt: configure_file. cmake/lua_checks.cmake + tests/partner_sync_lua_check.lua (mock vars/loadstring). tests/host_trial_patch_check.cpp: assert the new header is present.
- tools/lab: lab.json ports.netHost/netGuest; LabCommon.psm1 New-LabIniText writes net=1, net_listen_port, net_peer_port; Start-Lab.ps1 adds a UDP preflight and uses the host-v1 (or offline-v1) marker for the offline spike instead of host-v3/client-v1 (Start-Lab.ps1:122, LabCommon.psm1:342/349).
- Later (approach B): steam_packets add SteamPacketDelivery::Unreliable mapped to mode 0 (3 LOC) and optionally resolve AcceptP2PSessionWithUser. A SteamP2P backend in partner_net that calls send and drains up to 8 per frame on the game thread inside the receipt.

6. TEST PLAN

Offline unit tests:
- Codec round-trip, including cross-checking against wire::decode.
- Interpolation: lerp, wrap, snap, extrapolation cap, stale/lost thresholds.
- Receipt parse/format: malformed input, overflow, negative values.
- lab.ini keys.
- Two UdpTransport instances on test ports such as 48170/48171 exchanging frames with latency and loss injection.

Lab:
- Both instances use host-v1 with net=1.
- Expect a 'partner_net=listening' line in each script-observer.log.
- Expect periodic stats lines with tx/rx growing in both.
- Lua debug announce on the first status 1.
- Measure the per-frame receipt cost.
- Check that the background instance keeps ticking.

7. KEY RISKS

- Rotation units: degrees inferred; convert before encoding.
- noexcept paths with allocating code: use fixed buffers.
- Log flooding if the quiet path is missed.
- An unfocused instance may not tick.
- The SteamUdpSocketImpl 1202 handler question for approach B.
- The bootstrap_patch splice-size bookkeeping.