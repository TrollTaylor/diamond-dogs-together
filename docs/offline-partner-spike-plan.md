# Spike plan P2-OFFLINE-1: make the partner visible and moving, fully offline (one PC, two instances)

Evidence labels used below:
- **[V]**: I re-checked it statically while writing this plan (capstone on 1.0.15.4; RVA given).
- **[R]**: from the research pass, static, not re-checked here.
- **[L]**: seen in an earlier live log.
- **[S]**: speculation, to be settled live.

Nothing was edited or launched. My scratch scripts are in `C:/Users/taylo/AppData/Local/Temp/claude/D--Games-steamapps-common-MGS-TPP/e7a6e70e-f530-4f59-b4a3-60e868204a66/scratchpad/p2/`.

## 0. Decision summary

- **Primary route.** Realize the game's own second player instance (R = 1) at Mother Base 30050:
  - native code sets the two realize gate bits;
  - Lua calls `Player.SetAllInstanceActiveMode(true)` and `Player.CopyPartsInfoFromLocalPlayer`;
  - Lua sends a TppPlayer2 `Warp` to index 1 every tick.
- **Mode.** Everything runs in a new `offline-v1` marker mode. No session is created and S_IS_ONLINE is never set.
- **Transport.** Our own second `UdpTransport` on ports 47820/47821.
- **Switching.** Every step can be changed at runtime through a trigger file, so the operator can walk through E1 to E4 in one boot.
- **Cheapest decisive experiments, in order:**
  - E1: does a Lua Warp write instance 1's position slot?
  - E2: does all-active mode alone realize it?
  - E3: do the bits plus copied parts give a realized, loaded, visible instance?
  - E4: does it move without networking (orbit)?
  - E6: does it move from the other instance's packets?
- **Hard rule.** Never set P+0x204 bit 0x100 (S_IS_ONLINE) after construction. [V] At 0x12CC6B6..0x12CC6D6, when bit 8 is set, the per-frame Q pre-update loads `[P+0xA8]` and calls its vtable +0x1C8. P+0xA8, the synchronizer, is null offline [R], so this is a certain null dereference. The same goes for any S_IS_ONLINE experiment.

## 1. Ranked approaches

Notation (all [R], with the parts I re-checked marked [V]):
- P = `*(base+0x2A82C30)`, the player game object's secondary view. Its vtable is base+0x2295490 [V, also checked by the existing `player_type_contract.hpp`].
- Q = `*(P+0x60)`. Its vtable is base+0x23AD970 [V].
- S = `*(Q+0x1E8)`. [V] The getter 0xFDEEF0 is `mov rax,[rcx+0x1E8]; ret`, and it occupies Q vtable slots +8 and +0x10.
- Pointers inside S:
  - `*(S+0)` points to the "exists" bitset bytes.
  - `*(S+8)` points to the "local wanted" bitset.
  - `*(S+0x20)` points to the position array: 16-byte entries `{x,y,z,yaw}` per instance.
- Per-instance arrays:
  - `ids = *(u16**)(P+0x70)`
  - `pool = *(u8**)(P+0x78)`
  - `parts = *(u8**)(P+0x90)`
- `blocks = *(P+0x378)`, stride 0x6A8.
- An instance is realized iff `pool[i] != 0xFF` and `ids[i] != 0xFFFF`. This is the 0x9CD160 guard, and Warp uses the same check (0x9CEB8B..0x9CEBCC) [V].

### Rank 1: hybrid (native realize bits, Lua appearance and mode, per-tick Lua Warp)

**What the realize policy requires.** The policy is 0x121B5E0 [V fingerprint `48895c24185556574154415541564157`; slot 0x23CA168 is vtable+0x10]. For a non-local instance i it needs all of the following [V, 0x121B850..0x121B979]:
1. The slot loop guard `P.vt+0x1A0` returns true. [V] That slot is wrapper 0x9C5260, which jumps to 0x9B3D60 with rcx = `*(0x2C1B0F8)`. The guard is true if `[[g+0x10]+0xB8]==0`, or if `[...+0x120]==2`.
2. Bit i is set in `**S`.
3. `(*(S+0x20))[i]` lies inside the area rectangle, and the area's vt+0x20(x, z, 0, 1) returns true. Both checks are skipped when `*(u8*)(policy+0x10) != 0` (0x121B8FF, 0x121B933).
4. One of:
   - bit i is set in both `Q+0x194` and `Q+0x198` (0x121B94B / 0x121B953);
   - or P+0x204 bit 13 is set and `P.vt+0x198(i)` returns true (wrapper 0x9C5730 to 0x9B3FE0).
5. The wanted list is capped at realizedCount, which is 2.

**How we supply each input:**

| Input | How | Evidence |
|---|---|---|
| Position | `GameObject.SendCommand({type="TppPlayer2",index=1},{id="Warp",pos={x,y,z},rotY=rad})` goes through dispatcher 0x9C5F80, then `P.vt+0x1B0` = 0x9CEAE0 | [V] slot. [V] 0x9CEB4E..0x9CEB78 writes `(*(S+0x20))[idx]` for any index. `pos` must be a plain 3-number table [R]. |
| Exists bit | `Player.SetAllInstanceActiveMode(true)` (0x9DCD20) sets P+0x204 bit 0x2000. Every frame, 0x12CC5F0 then ORs bits 0..count-1 into `**S`, writes `Q+0x170` = mask, and **returns before any online apply** | [V] fingerprint `48895c2408574883ec20488bd9e89efb`. [V] 0x12CC633..0x12CC6B5. |
| Q+0x194 and Q+0x198 bit 1 | Native `_InterlockedOr` from the receipt handler | Reads [V]. No offline writer [R]. |
| Appearance | `Player.CopyPartsInfoFromLocalPlayer{instanceIndex=1}` (0x9D13F0) | [V] Copies Q+0x40/48/50/58 (bytes), Q+0x60 (word) and the 0x4F-byte avatar record at Q+0xE8, then sets bit idx in Q+0x180. **No null check on P. The index defaults to 0 and has no bounds check.** |

**What should follow.** The realizer runs 0xBF7140 [V], which calls the policy, commits, and then calls block realize 0x9C85D0 [R]. UpdatePartsStatus (0x9CD030) consumes the Q+0x180 dirty bit and requests the parts through 0x9B47D0 and then 0x9B41B0. [V] That request goes ahead only while dword 0x2C1B0F0 == 0. Parts-load state 3 means loaded [S]; the mirror code compares `[P+0x90][1]` with 3 at 0x1085088 [V].

**Movement.** Warp every tick. For a realized instance, Warp also:
- sets the body transform: obj vt+0x70, then vt+0x68;
- then calls `blk+0xC0` vt+0x90 and `blk+0xA0` vt+0x80, which look like resets [V 0x9CEBD4..0x9CEC2C].

Camera, stage-block and WaitBlock handling only run for the local index [R].

**Preconditions:**
- The two-slot archive is active at 30050.
- The construction is offline: P+0x204 bit 0x100 is clear and P+0xA8 == 0.
- The local index is 0, which is always true offline [R 0x9BD9CD].
- [L] "players=ready; limit=2" was only seen in client-v1, which sets S_IS_ONLINE (`lua/coop/host_session_trial.lua:26-29`). It must be re-checked in offline-v1 (E0).

**Failure modes:**
- (a) ids[1] stays 0xFFFF because the area check fails, the S0 bit gets rewritten, or the bits get cleared.
- (b) The instance realizes, but its parts never reach state 3.
- (c) The parts load, but nothing is drawn.
- (d) The Warp resets cause jitter or freeze the animation.
- (e) The instance realizes and unrealizes repeatedly when the pose leaves the area.

**Crash risks:**
- Highest [S]: the non-local body update may touch online-only components, starting with the null P+0xA8.
- Trap callbacks seeing a non-local player (Dynamite precedent). `trap_patch.cpp` stays inactive without a session.
- Teardown while instance 1 is realized.
- Out-of-bounds index: 0xBFA290, 0x9C5F80, 0x9D13F0 and 0x9DF530 do no bounds checks. Clamp R to 1.

**Supporting precedent [R]:** the prologue mirror path realizes instance 1 in single-player (0x9CCC13 reads slot[1]).

**Confidence (subjective):**

| Outcome | Probability |
|---|---|
| ids[1] != 0xFFFF | about 65% |
| Parts loaded | about 55% |
| Visible | about 45% |
| Stable for 2 minutes, given visible | about 50% |
| Moves with Warp, given visible | about 80% |
| **Visible and moving overall** | **about 35-40%** |

### Rank 2: native drive variants (same realization, different driving)

- **2a, slot-only apply.** Native code writes `(*(S+0x20))[1]` every tick, with no Warp and so no resets. This is the array the FOB transform receive (0x12A53A0) fills [R], so it tests whether the remote body follows S+0x20 offline. That is the basis for smooth movement later. Confidence 30% [S].
- **2b, native Warp call.** Only if the Lua Warp fails (E1 FAIL). Call `((void(*)(void*,uint32_t,const float*,const float*,bool))(base+0x9CEAE0))(P,1,pos,quat,false)` with 16-byte-aligned `pos` and `quat`: Warp uses movaps on both, at 0x9CEB30 and 0x9CEB99 [V].
- **2c, smooth animated drive (next spike).**
  - Writes: S+0x20; `Q.vt+0x18` SetPosition 0xFE0080; `Q.vt+0x20` Interpolate 0xFE02F0, which is never called offline; the arrays Q+0x18/0x28/0x30/0x38; the records Q+0x1D8/0x1E0/0x1F8 (keep the local vtable pointer at +0 of the 0x40-byte object).
  - [V] Q vtable slots: +0x18 0xFE0080, +0x20 0xFE02F0, +0x38/+0x40 0xFDEF40/0xFE00D0, +0x48/+0x50 0xFDEF50/0xFE01B0, +0x60/+0x68 0xFE0200/0xFE0220.
  - Which of these the body actually reads is unknown. E5 and E10 come first. Confidence low-medium.

### Rank 3: pure Lua (all-active mode plus Warp, no native writes)

This realizes the instance only if `0x9B3FE0(1)` returns true. [V] That happens when:
- dword 0x2C1B0F0 != 0. It has 23 compare-only sites and no RIP-relative writer in .xtext. When it is non-zero, the parts request 0x9B47D0 is skipped entirely. So it is probably a suppression flag that is 0 in normal play [S].
- Or g = `*(0x2C1B0F8)` and `*(*(g+0x10)+0x17C+4*slot) != 0`, where slot = 1 for R = 1 > local. [V] It is bounded by `+0x168` + `+0x16C`. This is probably a per-parts-block flag [S].

E2 settles this at no extra cost. Confidence 20%.

### Rank 4: mirror mode (diagnostic only)

Set bits 13 and 14 (0x6000) with `SetAllInstanceActiveMode(true)` plus `SetOtherInstanceIsForMirrorMode(true)`. UpdatePartsStatus then copies the local appearance onto the other instances (0x9CE786) [R]. Realization still needs the bits.

Risk: the local parts update waits for instance 1's parts state 3 (0x9CD2E2) [R]. The other mirror readers, 0x9CCC67 and 0x1085068, also require both bits [V]. Use only in branch F3c of E3.

### Rank 5: NPC puppet fallback

Start this only if E3 fails twice with different variants.
- **5a, Ocelot (TppOcelot2).** Hostage-family dispatcher with a real `Warp{position=Vector3, degRotationY, interpTime}` [R]. Infinite Heaven loads `f30050_ocelot.fpk` itself, so a pack-list change may be needed [R].
- **5b, Mother Base security soldier.** It has no Warp or SetPosition [R 0x1356170]. Use `SetPuppet{enabled=true}` plus a re-issued `SpecialAction{startPos,startRot}`, or a route snap. This needs a gani path or route that is resident on Mother Base, which needs separate research.
- Confidence: visible and repositioned about 40-50%; poor animation.

### Rejected

- **Feeding the synchronizer, S_IS_ONLINE, or creating P+0xA8:** out of scope (session gates), and it crashes as described in section 0.
- **"Realize" command:** not a TppPlayer2 command [R].
- **Commit 554cc4f:** it also gated its Warp on a `vars.playerPos` that does not exist [R].

## 2. Minimal implementation

### 2.1 Mode and configuration

- **New marker `offline-v1\n`** (11 bytes) in `mgs-coop/script-observe.enabled`. In `InstallGameScriptObserver` (`native/game_script_observer.cpp:963-978`):
  - `offline_enabled = read==11 && !memcmp(mode,"offline-v1\n",11); host_enabled |= offline_enabled;`
  - This brings in the same things host-v1 has: allocation, archive, the TppMain host-trial splice, the mission boundary, runtime-thread pinning and two-slot package selection.
  - `host_create_enabled` and `client_enabled` stay false. So there is no Dynamite player loading or ghost patch, and no character, targeting, damage or respawn receipts.
  - Log `mode=offline_v1; sessions=0; s_is_online=never; partner=<0|1>`.
- **HOST_CREATE receipt** (`game_script_observer.cpp:640-663`): if `offline_enabled`, do not call `TryCreateHostSession`. Log `host_create=skipped; mode=offline_v1` and reply `return false`. `host_session_trial.lua` then never sets S_IS_ONLINE, because lines 26-29 only run for `true` or `'client'`.
- **Zero-code fallback:** marker `host-v1` on both instances. The only difference is that TryCreateHostSession is called and refuses (`host_session_trial.cpp:180-181`).
- **lab.ini keys** (`native/lab_config.cpp:141-178`; unknown keys are already ignored):

| Key | Values (default) |
|---|---|
| `net` | 0/1 |
| `net_listen_port`, `net_peer_port` | host 47820/47821, guest 47821/47820. Must differ from each other and from `listen_port`/`peer_port`; otherwise `net=0` and `invalid_values++`. |
| `partner` | 0/1 |
| `partner_stage` | 0..3 (0) |
| `partner_source` | fixed / orbit / net (fixed) |
| `partner_apply` | warp / slot / both (warp) |
| `partner_rate_hz` | 1..60 (30) |
| `partner_interp_ms` | 0..500 (100) |
| `partner_offset_cm` | 50..1000 (300) |
| `partner_teardown` | clear / keep (clear) |
| `partner_force_exists` | 0/1 (0) |
| `partner_policy_relax` | 0/1 (0; last resort) |
| `partner_anim_probe` | 0/1 (0) |
| `crash_probe` | 0/1 (1) |

  The existing `net_latency_ms` and `net_loss_permille` keys are reused.
- **Runtime trigger file** `<game>/mgs-coop/partner.trigger`: one line containing any of `stage=N source=fixed|orbit|net apply=warp|slot|both`, or `stop`. Native code polls it every 30 ticks, reads at most 63 bytes, deletes it, and logs it. Use a new helper next to `ConsumeTriggerFile` (`game_script_observer.cpp:177`).

### 2.2 Receipt grammar

Every receipt has name == buffer, checked bytewise as in `character_receipt.cpp` Classify. Size is at most 200.

| Receipt | Direction / thread | Native reply |
|---|---|---|
| `return 'MGS_COOP_PARTNER_CFG_V1'` | Once, after OnMissionGameStart returns; runtime thread | `return true` if offline_enabled, partner=1, the code gate passed and the state is valid. Otherwise `return false`, or not replaced in other modes, in which case Lua gets the literal string and stays inactive. |
| `return 'MGS_COOP_PARTNER_V1' -- <frame> <x_mm> <y_mm> <z_mm> <yaw_cdeg> <cam_cdeg> <stance> <move> <luaflags>` | Every frame; 9 strictly parsed signed ints (stance 0..2, move 0..3, luaflags bit0 = all-active set, bit1 = parts copied) | **Quiet** (no entry/return log lines): `return <op>,<R>,<x_mm>,<y_mm>,<z_mm>,<yaw_mrad>,<status>,<age_ms>`. Not replaced (Lua sees a string, so it does nothing) when not ready or when `mission_terminating`. |
| `return 'MGS_COOP_PARTNER_LOG_V1' -- event=...` | Lua events only (at most 64 per mission) | Logs `partner_lua=<payload>`; replies `return true`. |
| `return 'MGS_COOP_PARTNER_STOP_V1'` | OnTerminate; allowed while terminating | Clears the bits if teardown=clear, resets the ring, logs, replies `return true`. |

- **op bits:** 1 = WARP, 2 = SET_ALLACTIVE, 4 = COPY_PARTS, 8 = CLEAR_ALLACTIVE, 16 = SET_PARTS_EXPLICIT (variant F3b), 32 = MIRROR_ON (diagnostic F3c).
- **status values:** 0 synthetic, 1 live, 2 stale, 3 lost.
- **Stages, decided natively:**
  - 0: probe only, op = 0.
  - 1: WARP only.
  - 2: WARP + SET_ALLACTIVE.
  - 3: WARP + SET_ALLACTIVE + COPY_PARTS, and native ORs of Q+0x194/0x198. The ORs start from the tick after the first pose was applied, so that S+0x20[1] is already in the area.
  - `apply=slot` removes WARP and makes native code write `(*(S+0x20))[1]` itself; `apply=both` does both.
- **Pose sources:**
  - fixed: local position + `offset × (sin cam, 0, cos cam)`. [S] axis convention; if it is wrong, orbit still works.
  - orbit: radius = offset, 8-second period, yaw tangent to the circle.
  - net: interpolated remote pose. No WARP until the first packet arrives.

### 2.3 Native files

**Portable (`mgs_coop_core`, with unit tests):**
1. `native/include/mgscoop/lab_config.hpp` and `native/lab_config.cpp`: the keys above plus validation (about 40 LOC). Tests in `tests/lab_config_tests.cpp`.
2. New `native/include/mgscoop/partner_receipt.hpp` and `native/partner_receipt.cpp`: `Classify`, `ParseTick` (9 ints, no strtod), `FormatReply`, and `SanitizeLogPayload`. Integer formatting only, as in `character_receipt.cpp:81-103` (about 150 LOC). Tests in `tests/partner_receipt_tests.cpp`.
3. New `native/include/mgscoop/pose_interp.hpp` and `native/pose_interp.cpp`: `SnapshotRing<16>` with `Push(frame, recv_ms)` and `Sample(now, delay_ms)`:
   - linear interpolation on the sender timeline, offset = min(recv - sender_tick);
   - shortest-arc yaw;
   - snap when the jump is more than 5 m;
   - extrapolate at most 100 ms, then hold;
   - stale after 500 ms, lost after 3 s;
   - delay 0 means latest snapshot only.
   About 150 LOC, deterministic tests.
4. `native/player_sync.{hpp,cpp}`: add noexcept fixed-buffer `encode_transform_frame(session, epoch, pkt, uint8_t(&)[56])` and `decode_transform_frame(...)`, byte-identical to `wire::encode(encode_transform(...))` (about 60 LOC; the tests cross-check against `wire::decode`). The frame is the 28-byte MGSC header (kind = snapshot 2, session = lab constant 0x4D4753434F4F5001, epoch = mission code from `ReadMission` (0x2C01372), sequence, length 28) followed by `PlayerTransformPacket`:
   - position in metres;
   - `rot_y` in radians, normalized to [-π, π];
   - `stance` 0..2;
   - `action_flags` bit 2 = Sprinting when move == 3;
   - `tick_ms` = sender QPC milliseconds.

**Windows-specific:**

5. New `native/partner_player_contract.hpp` (hand-written, or `tools/generate_partner_contract.py` following the player_type pattern):
   - RVAs: player slot 0x2A82C30; P vtable 0x2295490 with slot +0x1B0 == 0x9CEAE0; Q vtable 0x23AD970; policy vtable 0x23CA158.
   - Offsets: flags 0x204, count 0x208, realized count 0x20C, local 0x214, Q 0x60, ids 0x70, pool 0x78, parts 0x90, realizer 0xA0, sync 0xA8, realized bitset 0x380; Q: 0x170, 0x180, 0x194, 0x198, S at 0x1E8; S: exists 0, local 8, pos 0x20.
   - Record bank: bank at 0x2BD3FD0, records at 0x2BD3FF0, stride 0xE0, position at +0x10, status at +0xC0 (`docs/player-update-investigation.md:24-35`).
   - Globals: 0x2C1B0F0 and 0x2C1B0F8.
   - 16-byte fingerprints [V]:

| RVA | Bytes |
|---|---|
| 0x9CEAE0 | `4c8bdc5741574881eca8000000488b05` |
| 0x9DCD20 | `48895c2408574883ec20488bd9e89efb` |
| 0x121B5E0 | `48895c24185556574154415541564157` |
| 0x12CC5F0 | `48895c24184889742420574883ec200f` |
| 0x9D13F0 | `40574883ec3048c7442420feffffff48` |
| 0x9DF530 | `405556574154415541564157488bec48` |
| 0xFDEEF0 | `488b81e8010000c3` |

   - Semantic sites: 0x121B94B `858194010000`, 0x121B953 `858198010000`, 0x12CC633 `418b8904020000 8bc1 c1e80d a801 7473`.
6. New `native/include/mgscoop/partner_player.hpp` and `native/partner_player.cpp` (about 250 LOC):
   - `CodeGate(base, span)`, run once.
   - `ReadState(base, State&)`: reads only, through the `ReadValue`/ReadProcessMemory pattern of `player_type_patch.cpp:185-219`, with both vtable checks.
   - `OrRealizeBits` / `ClearRealizeBits`: `__try { _InterlockedOr((volatile long*)(Q+0x194), 2); ... (Q+0x198) }`.
   - `WriteSlot(R, xyzw)`: one aligned 16-byte store.
   - `ForceExists(R)`: `**S |= 2`.
   - `PolicyRelax(on)`: policy = `*(*(P+0xA0)+0x50)`, vtable must equal base+0x23CA158, writes byte +0x10. Last resort, restored at stop.
   - `FormatProbe` in integer millimetres. Avoid `%f` to stay locale-free.
   - Write guards, all mandatory: P passes its vtable check; flags bit 0 set; **bit 0x100 clear**; P+0xA8 == 0; count == 2; local == 0; R == 1; Q passes its vtable check; S, `*S` and `*(S+0x20)` non-null; not `mission_terminating`.
7. New `native/include/mgscoop/partner_net.hpp` and `native/partner_net.cpp` (`mgs_coop_lab`, about 220 LOC):
   - Global second `lab::UdpTransport` (constant-initialized and trivially destructible, `udp_transport.hpp:98`), started with INIT_ONCE from `InstallGameScriptObserver`, never from DllMain.
   - `SubmitLocal()` sends on the game thread, non-blocking, rate-limited by QPC.
   - The receive sink only decodes into the ring under an exclusive SRWLOCK. It drops wrong-session frames, epochs other than the current mission, and old sequences using `newer_sequence` (`session.cpp:12-15`). It accepts a sequence restart after a 2-second gap.
   - `on_idle` every 250 ms: a keepalive if nothing was sent for 1 s, and a stats line every 10 s only when something changed.
   - `ResetRemote()` at `runtime_enter` and `termination_returned` (`SelectMissionBoundary`, observer lines 342-398).
8. New `native/crash_probe.cpp` (optional, about 50 LOC): `AddVectoredExceptionHandler(0, ...)`.
   - Logs only `EXCEPTION_ACCESS_VIOLATION` whose address lies inside the game image, for the first 16 distinct RVAs.
   - Returns `EXCEPTION_CONTINUE_SEARCH`.
   - Line format: `crash_probe=av; rva=0x..; access=read|write; target=0x..; thread=..; stage=..; realized=..`.
9. `native/game_script_observer.cpp` (about 120 LOC):
   - the marker;
   - the HOST_CREATE skip;
   - a partner fast path **immediately after `AcquireSRWLockExclusive(&receipt_lock)` (~line 569)**: `if(offline_enabled && partner_on){kind=Classify(...); if(kind) return HandlePartner(kind,...);}`;
   - `thread_local bool quiet_replacement`, set only for the tick receipt. In `ObserveReturn` (~938): `if(quiet_replacement){quiet_replacement=false;InterlockedIncrement64(&returns);return;}`;
   - trigger polling;
   - configuration log lines;
   - boundary resets.

   Tick handler outline:

```text
OnRuntimeThread && !mission_terminating, else return false
ParseTick; ReadState (refuse -> change-only log; return false)
poll trigger every 30 ticks
pose = source(fixed|orbit|net)
if stage>=3 && pose_applied_before && !bits_set -> OrRealizeBits; log
if stage>=3 && bits_set && (q194&2)==0      -> log partner_bits=lost (cap 8); re-OR
if force_exists && stage>=2 && !(s0&2) for >10 ticks -> ForceExists
if apply has slot -> WriteSlot(1, pose)
op = by stage/apply/one-shots; transitions (realized/parts) -> immediate log lines
partner_net::SubmitLocal(local pose) at rate_hz
probe line <=1 Hz, change-only on discrete fields, forced every 10 s
FormatReply -> thread_local char[128]; quiet_replacement=true; return true
```

### 2.4 Lua: new `lua/coop/partner_sync.lua`

Splice it into the composed TppMain right after `session_feedback_source`. In `native/bootstrap_patch.cpp` `SelectHostTrialPatch` (lines 128-216): add the size to `result_size` (~147), add a `memcpy` after the feedback copy (~196), and keep the `static_assert` chain. Also add `native/partner_sync_source.hpp.in` and its `configure_file`, a Lua mock check in `cmake/lua_checks.cmake` plus `tests/partner_sync_lua_check.lua`, and an order assert in `tests/host_trial_patch_check.cpp`.

`session_lifecycle` is appended last, so its OnTerminate runs first. STOP therefore arrives while `mission_terminating` is already true, which is why STOP is allowed then.

```lua
-- MGS co-op offline partner spike (original). Inert unless native answers
-- MGS_COOP_PARTNER_CFG_V1 with true (offline-v1 + lab.ini partner=1).
do
    local start, update, terminate = e.OnMissionGameStart, e.OnUpdate, e.OnTerminate
    local active, failed, frame, allActive, partsCopied = false, false, 0, false, false
    local fmt, floor, gsub, sub = string.format, math.floor, string.gsub, string.sub
    local function receipt(source)
        local chunk = loadstring(source)
        if type(chunk) ~= "function" then return nil end
        return chunk()
    end
    local function note(text)
        receipt("return 'MGS_COOP_PARTNER_LOG_V1' -- " .. sub(gsub(tostring(text), "[^%w_=:,.;%-]", "_"), 1, 160))
    end
    local function has(s) return PlayerInfo.OrCheckStatus{s} == true end
    local function tick()
        if not active or failed or vars.missionCode ~= 30050 or not Player.IsInstanceValid() then return end
        frame = frame + 1
        local stance = (has(PlayerStatus.CRAWL) and 2) or (has(PlayerStatus.SQUAT) and 1) or 0
        local move = (has(PlayerStatus.DASH) and 3) or (has(PlayerStatus.RUN) and 2) or (has(PlayerStatus.WALK) and 1) or 0
        local cam = (vars.playerCameraRotation and vars.playerCameraRotation[1]) or 0
        local op, R, x, y, z, ry = receipt(fmt("return 'MGS_COOP_PARTNER_V1' -- %d %d %d %d %d %d %d %d %d",
            frame % 1000000, floor(vars.playerPosX * 1000 + 0.5), floor(vars.playerPosY * 1000 + 0.5),
            floor(vars.playerPosZ * 1000 + 0.5), floor(vars.playerRotY * 100 + 0.5), floor(cam * 100 + 0.5),
            stance, move, (allActive and 1 or 0) + (partsCopied and 2 or 0)))
        if type(op) ~= "number" or op <= 0 then return end
        if R ~= 1 or R == PlayerInfo.GetLocalPlayerIndex() then
            failed = true; note("event=refused;reason=bad_index;R=" .. tostring(R)); return
        end
        if op % 16 >= 8 and allActive then Player.SetAllInstanceActiveMode(false); allActive = false; note("event=allactive_cleared") end
        if op % 4 >= 2 and not allActive then Player.SetAllInstanceActiveMode(true); allActive = true; note("event=allactive_set") end
        if op % 8 >= 4 and not partsCopied then
            Player.CopyPartsInfoFromLocalPlayer{instanceIndex = R}; partsCopied = true; note("event=parts_copied;R=1")
        end
        if op % 32 >= 16 then
            Player.SetPartsInfoAtInstanceIndex{instanceIndex = R, type = vars.playerType, partsType = vars.playerPartsType,
                camoType = vars.playerCamoType, handType = vars.playerHandType, faceId = vars.playerFaceId}
            note("event=parts_set_explicit")
        end
        if op % 64 >= 32 then Player.SetOtherInstanceIsForMirrorMode(true); note("event=mirror_on") end
        if op % 2 == 1 and type(x) == "number" and type(y) == "number" and type(z) == "number" and type(ry) == "number" then
            GameObject.SendCommand({type = "TppPlayer2", index = R},
                {id = "Warp", pos = {x / 1000, y / 1000, z / 1000}, rotY = ry / 1000})
        end
    end
    local function started(...)
        frame, failed, allActive, partsCopied = 0, false, false, false
        active = vars.missionCode == 30050 and receipt("return 'MGS_COOP_PARTNER_CFG_V1'") == true
        if active then note("event=started") end
        return ...
    end
    local function updated(...)
        local ok, failure = pcall(tick)
        if not ok then failed = true; pcall(note, "event=error;msg=" .. tostring(failure)) end
        return ...
    end
    local function stopping()
        if not active then return end
        active = false
        if allActive then pcall(Player.SetAllInstanceActiveMode, false); allActive = false end
        receipt("return 'MGS_COOP_PARTNER_STOP_V1'")
    end
    if type(start) == "function" then e.OnMissionGameStart = function(...) return started(start(...)) end end
    if type(update) == "function" then e.OnUpdate = function(...) return updated(update(...)) end end
    if type(terminate) == "function" then e.OnTerminate = function(...) pcall(stopping); return terminate(...) end end
end
```

The variable names `vars.playerHandType` and `vars.playerFaceId` are [S]; they are used only by the F3b variant. If a Lua error occurs, the script stops, notes the error and stops ticking. The native side then sees no more tick receipts.

### 2.5 Lab scripts (`tools/lab`)

- **`lab.json`:**
  - `ports.netHost`: 47820, `ports.netGuest`: 47821;
  - new `offline`: `{ "masterPort": 47899, "marker": "offline-v1" }`;
  - `partner` defaults;
  - characters: `default` for **both** during the spike (the guest is currently `quiet`).
- **`LabCommon.psm1`:**
  - `New-LabIniText` (line 219) gets net and partner parameters.
  - `Get-LabInstanceSettings` (line 323) gets `-Offline`: Marker `offline-v1`, MasterUrl `http://127.0.0.1:47899/tppstm/gate`, and net ports per role. That URL passes `Test-LabUrl`: 34 characters, ends in `/tppstm/gate`.
  - `New-CoopIniText` (line 266) needs no change.
- **`Prepare-LabInstance.ps1`:** add `-Offline`, which writes the marker, lab.ini and coop.ini as above.
- **`Start-Lab.ps1`:** add `-Offline`:
  - `$expectedUrl` becomes the dead URL (line 77);
  - skip the fuse exe check, start and TCP 6667 check;
  - require **no listener** on TCP 47899 (`Test-LocalPortFree` true) and free UDP 47820/47821;
  - expect the `offline-v1` marker for both roles (replacing the check at lines ~118-122);
  - require `net=1` and `partner=1` in lab.ini;
  - keep the safety gate as is: `master_server=redirected` with the expected (dead) URL, `saves=redirected`, and for the guest `mutex=renamed` and `identity=fake`;
  - record `offline=true` in run.json.
- **`Stop-Lab.ps1`:** delete any leftover `partner.trigger`.
- **New `Set-PartnerTrigger.ps1 -Role host|guest -Text 'stage=3 source=orbit apply=warp'`:** writes a temp file and renames it into `<dir>/mgs-coop/partner.trigger`. No window focus is needed.
- **New `Test-PartnerSpike.ps1 -Experiment E0..E8 [-Role]`:** reads this run's `script-observer.log` from the run.json offsets and prints a JSON verdict with the matched evidence lines, using the regexes in section 2.7.
- **`Send-LabInput.ps1`:** add `shift` (0x2A) for E10. Optionally add `f10`/`f11`.
- **Optional:** `native/world_probe.cpp --partner <pid>` as an out-of-process, read-only cross-check of the same fields.

### 2.6 Threading, rates, staleness

**Threads:**
- **Lua runtime thread** (pinned by RUNTIME_ENTER): parse, all game-memory reads and writes, the transmit call (non-blocking `sendto`), reading the ring under a shared lock, and the reply. All of this runs inside `receipt_lock`.
- **UDP receive thread:** decode, then push into the ring under an exclusive lock. It never touches game memory.
- **Lock order:** `receipt_lock`, then the net lock.
- **Unverified [S]:** whether the Lua thread is the one running Player2GameObjectImpl update 0x9C1F00. The writes are dword ORs on words that nothing writes offline, and single 16-byte stores, so a race is benign for a spike.

**Rates:**
- Lua tick every frame: 16-60 Hz unfocused [L], up to about 60 focused.
- Send at 30 Hz on loopback.
- Interpolation delay 100 ms; 0 means latest snapshot.
- Probe line at most 1 Hz (change-only, forced every 10 s, capped at about 400 per mission).
- Stats every 10 s.

**Staleness:**
- Stale after 500 ms: hold the last pose.
- Lost after 3 s: hold the pose. **The spike never unrealizes on loss**, to avoid thrash.
- Epoch mismatch: drop.
- On a mission change: reset the ring.

### 2.7 Log lines (contract for the automated check)

All lines go to `mgs-coop/script-observer.log`.

**Network:**
- `partner_net=listening; port=%u; peer_port=%u` or `partner_net=disabled; reason=transport_<bind|winsock|socket|thread|bad_config>`
- `partner_net=first_rx; seq=%u; epoch=%u`
- `partner_net=stats; tx=%llu; rx=%llu; drop_epoch=%u; drop_seq=%u; drop_bad=%u; status=%s; last_age_ms=%u; native_us_avg=%u; native_us_max=%u`

**Configuration and state:**
- `partner=configured; mode=offline_v1; stage=%u; source=%s; apply=%s; rate_hz=%u; interp_ms=%u; offset_cm=%u; teardown=%s`
- `partner_cfg=ready; flags=0x%08x; count=%u; realized_count=%u; local=%u; R=1; online_bit=%u; sync=null|set; code_gate=ok` or `partner_cfg=refused; reason=<player_absent|vtable|count|local_index|online_bit|sync_present|q_vtable|s_null|code_gate>`
- `partner_probe=state; t_ms=..; stage=..; ids=%04x,%04x; pool=%02x,%02x; parts=%u,%u; rbits=0x%x; s0=0x%02x; s8=0x%02x; q170=0x%x; q180=0x%x; q194=0x%x; q198=0x%x; glob=%u; pb_guard=%u; pb_flag=%u,%u; pb_state=%u,%u; pos0_mm=x,y,z; yaw0_mrad=..; pos1_mm=x,y,z; yaw1_mrad=..; rec1_mm=x,y,z; rec1_status=0x%08x`
  - `glob` = dword 0x2C1B0F0.
  - `pb_*` come from `*(g+0x10)`: `+0xB8`/`+0x120`, `+0x17C+4*slot` and `+0x10C0+4*slot`.
- `partner_tick=rate; hz=%u; focused=%u` (every 10 s)
- `partner_units; lua_pos_mm=..; mem_pos_mm=..; yaw_cdeg=..; yaw_abs_max_cdeg=..; cam_cdeg=..`
  - `mem_pos` is read from vars, at `*(B+0x2C16920)` + 0xEC/0xF0/0xF4 [R].
  - Logged at start and then every 30 s.

**Transitions and events:**
- `partner_stage=changed; stage=..; source=..; apply=..; via=trigger` or `partner_trigger=rejected; reason=parse`
- `partner_bits=set; R=1; q194=0x..; q198=0x..`, `partner_bits=lost; ...`, `partner_bits=cleared; reason=stop|trigger`
- `partner_s0=forced; R=1`, `partner_policy=relaxed|restored`
- `partner_realized=1; R=1; id=%04x; pool=%02x; ms_after_bits=%u` and `partner_realized=0; R=1; ms_after_realize=%u`
- `partner_parts=loaded; R=1; ms_after_realize=%u` or `partner_parts=timeout; R=1; state=%u; pb_state=%u`
- `partner_warp_check; R=1; apply=..; cmd_mm=..; slot_mm=..; err_mm=%u` (1 Hz while WARP or slot is active)
- `partner_lua=event=...`, `partner_stop; cleared=%u; teardown=%s`
- `crash_probe=installed`, `crash_probe=av; ...`
- With `partner_anim_probe=1`: `partner_anim; q18=%04x; q28=%02x; qd8=%04x; q30=%u; q38=%u; m1d8=<hash>; m1e0=<hash>; m1f8=<hash>` for slot 0, change-only, at most 10 Hz.

## 3. Experiment matrix

**Common setup:**
1. `Prepare-LabInstance.ps1 -Offline`, then `Start-Lab.ps1 -Offline`.
2. Press Enter through the title using `autoTitleScanCodes` and `Send-LabInput`. Watch for the server-failure dialog (section 4).
3. Wait until **both** logs show `partner_cfg=ready`, with a 15-minute timeout.
4. Screenshots come from `Get-LabScreens.ps1`.
5. After each experiment, run `Test-PartnerSpike.ps1 -Experiment Ex`.
6. The guest starts at stage 0 and is the control unless stated otherwise.

**E0: offline boot and baseline (stage 0, about 10 minutes)**
- PASS, on both instances:
  - early.log: `master_server=redirected; ... url=http://127.0.0.1:47899/tppstm/gate`
  - `mode=offline_v1`, `host_create=skipped`, `partner_net=listening`
  - `partner_cfg=ready; ... count=2; ... local=0; R=1; online_bit=0; sync=null`
  - first probe: `ids=0000,ffff; pool=00,ff`; s0 expected `0x01` [S]
  - `partner_net=first_rx` and stats with tx and rx above 0 on both
  - `partner_tick=rate` with the unfocused instance at 10 Hz or more
  - Units: walk in a circle (W+A held for 6 s). PASS if `yaw_abs_max_cdeg > 628`, meaning degrees, and `lua_pos ≈ mem_pos` within 50 mm while both change.
  - No `crash_probe=av` and both processes alive after 5 minutes.
- FAIL, then next step:
  - Redirect missing: the gate kills the instance; fix coop.ini.
  - Stuck at a dialog: send Enter or Esc, then see the section 4 fallback.
  - `count` is 1: the two-slot package was not selected in offline-v1. Check the `mode=players_v1` and PACK receipts, and try `host-v1`.
  - `online_bit=1` or sync set: stop. S_IS_ONLINE got set; audit the host_session_trial path.
  - rx is 0: check the bind reason and port collisions.
  - `yaw_abs_max < 628`: radians. Change the Lua multiplier and the native conversion.

**E1: Warp reaches instance 1 while it is unrealized (host: `stage=1 source=fixed apply=warp`)**
- PASS within 2 s:
  - `partner_warp_check; ... err_mm<=5`, and `pos1_mm` follows the commanded position;
  - ids[1] stays ffff;
  - no `partner_lua=event=error`;
  - `pos0` keeps tracking the local player, and the screenshot shows the camera did not jump.
  - Informational: does `rec1_mm` follow `pos1`? This tells whether the record updater covers non-local instances.
- FAIL:
  - With a Lua error: fix the call shape (`pos` must be a plain table).
  - With no error and `pos1` unchanged: continue with `apply=slot` for E3 and schedule variant 2b (native Warp).

**E2: does all-active alone realize? (host: `stage=2`)**
- Mechanism PASS within 2 s: `partner_lua=event=allactive_set`, flags include 0x2000, `s0=0x03`, `q170=0x3`.
- Outcome A: ids[1] stays ffff for 10 s. This is expected; record `glob`, `pb_flag`, `pb_guard`.
- Outcome B: ids[1] != ffff. Pure-Lua realization works (Rank 3); still run E3 to get the appearance.
- FAIL, s0 stays `0x01`: the all-active branch is not running. Set `partner_force_exists=1` for E3.

**E3: full realization, the decisive one (host: `stage=3`)**
- PASS:
  - `partner_lua=event=parts_copied`, then `partner_bits=set`;
  - `partner_realized=1; R=1` within 1 s of the bits;
  - `partner_parts=loaded` within 15 s;
  - the host screenshot shows a second player model about 3 m from Snake;
  - then 120 s with no `crash_probe=av`, no `partner_realized=0`, and the process alive.
  - Then run the same on the guest.
- F3a, ids[1] stays ffff after the bits:
  - if the s0 bit is clear, rerun with `partner_force_exists=1`;
  - `partner_offset_cm=100`, to keep the pose inside the area;
  - if `partner_bits=lost` appears, an unknown writer exists; keep re-ORing and log it for static follow-up;
  - then `partner_policy_relax=1` (last resort);
  - then Rank 5.
- F3b, realized but `partner_parts=timeout`:
  - check whether q180 bit 1 was consumed, and `pb_state[1]`;
  - trigger the op16 variant (explicit SetPartsInfoAtInstanceIndex);
  - check the archive's `partsBlockCount=2`.
- F3c, parts loaded but invisible:
  - run E4 with orbit to rule out framing;
  - try `apply=both`;
  - op32 mirror diagnostic for at most 10 s;
  - if still invisible, static follow-up on the draw and hide flags (P+0x204 bits written at 0x9CE592/0x9CE74F [R]).
  - Do **not** try bit 0x100.
- F3d, crash:
  - collect the `crash_probe` RVAs;
  - rerun once with `apply=slot`, which removes Warp's body setters and resets;
  - if it still crashes, it is in realization or the non-local update: static follow-up on that RVA, and start Rank 5 in parallel.

**E4: movement without networking (host: `stage=3 source=orbit`)**
- PASS:
  - 4 screenshots taken 2 s apart show the partner at different points around Snake;
  - `err_mm<=5` every second;
  - no `partner_realized=0`;
  - note the pose (idle, T-pose or frozen) for the B plan.
- FAIL, the partner stays at its first spot:
  - if `slot_mm` changes but the body does not, Warp's body path was not taken; check pool and ids at that moment;
  - try `apply=both`.

**E5: apply-mode comparison (orbit; `apply=slot`, then `both`)**
- If the partner moves with slot only, the remote body reads S+0x20, and smooth driving (2c) can build on that. If it does not, 2c must drive `Q.vt+0x18`/`+0x20` and the motion records.
- Record whether there is jitter or animation resets under warp compared with slot.

**E6: network-driven (both instances: `stage=3 source=net`)**
- Steps:
  1. Walk the guest forward for 4 s (W).
  2. Take host screenshots.
  3. Repeat with the roles swapped.
- PASS:
  - the partner appears at the other instance's position and follows it;
  - stats show rx increasing, `status=live`, `last_age_ms<200`;
  - the peer's own `pos0_mm` roughly equals the local `pos1_mm` (within 0.2 m).
- Pause test: press Esc on the guest. Either the host goes stale then lost and holds the pose, or the guest keeps ticking and the host stays live; record which. Resume to get live again.
- FAIL:
  - rx is 0: ports.
  - `drop_epoch`: mission mismatch.
  - Position offset: coordinate mismatch [S]. Compare the numbers.

**E7: impairment (guest lab.ini `net_latency_ms=100 net_loss_permille=200`)**
- PASS: host motion stays continuous; the `drop_*` counters behave as expected. Optional.

**E8: teardown and re-entry**
- Steps:
  1. Trigger `stop`. Expect `partner_bits=cleared`, `partner_lua=event=allactive_cleared`, `partner_realized=0` within 1 s, and the model gone from the screenshot.
  2. Trigger `stage=3`. The partner should realize again.
  3. Leave the mission. Expect `partner_stop`, the existing termination receipts, and the process alive.
  4. Re-enter 30050. Expect `partner_cfg=ready` again.
- FAIL, crash at stop: rerun with `partner_teardown=keep`. If it crashes on exit with `keep`, clearing is required: record which.

**E10: animation-slot survey (read-only, `partner_anim_probe=1`; can run during E0)**
- Host sequence: stand 3 s, walk 3 s, shift+W 3 s, c (crouch), hold c (crawl). Keybinds are [S].
- PASS: stance and move fields change in step with the actions. This feeds the 2c design.

**E9: NPC fallback (only after two different E3 failures)**
- A separate mini-spike: check that Ocelot exists (`GameObject.GetGameObjectId("TppOcelot2", <locator>)` is not NULL_ID), then orbit him with Warp each tick. Otherwise a soldier with SetPuppet plus SpecialAction, after a resident-gani research pass.

**Run order:** E0 (with E10), E1, E2, E3, E4, E5, E6, E8, E7, then E9 only if needed.

## 4. Lab offline mode (to be verified live)

- **coop.ini:** `master_url=http://127.0.0.1:47899/tppstm/gate`, a local port where nothing listens, so connections are refused immediately. Use `character=default` on both instances.
- **Do not start fuse.** No official servers, no login or session gates.
- **Markers:** `offline-v1` on both instances (fallback `host-v1`). These never call the session APIs and never set S_IS_ONLINE.
- **lab.ini:**
  - keep the save redirect, the guest's mutex suffix and fake identity;
  - `loopback=1` can stay (harmless, and the current preflight requires it), or set 0;
  - the net and partner ports listed above.
- **Safety:** the existing gate terminates any instance whose early.log lacks `master_server=redirected` with this exact URL. Without the redirect the game would contact Konami's default servers, so the gate stays mandatory.
- **To verify live in E0:**
  - the game shows a connection-failure or offline notice, then reaches the title (Enter presses as in `autoTitleScanCodes`);
  - there is no long hang or retry loop;
  - the Mother Base free-roam load (30050) works.
- **Precedent [L]:** the 2026-10-03 run with a version-mismatched fuse also ended up offline and reached Mother Base (`docs/testing-2026-10-03.md`).
- **If the dead port hangs:** that fuse configuration is a known-good fallback, but using it is the user's call.

## 5. Risks and unknowns, and which experiment settles each

| # | Risk / unknown | Settled by |
|---|---|---|
| 1 | Two slots allocate in offline-v1 (the earlier "limit=2" was seen under S_IS_ONLINE) | E0 `count=2`, `sync=null` |
| 2 | The Lua Warp reaches index 1 | E1 `err_mm` |
| 3 | Whether all-active alone realizes (0x9B3FE0 path) | E2 outcome A/B, plus `glob` and `pb_flag` |
| 4 | Realization offline (S0 rewrites, area check, bit persistence) | E3 and F3a branches, `s0`, `partner_bits=lost` |
| 5 | Crash in the non-local body update (null P+0xA8 and so on) | E3 plus `crash_probe` RVAs; `apply=slot` isolates the Warp path |
| 6 | Parts load for instance 1 | E3 `partner_parts`, `pb_state` |
| 7 | Visible once loaded | E3 and E4 screenshots, F3c |
| 8 | Warp resets: jitter, no animation | E4 and E5 screenshots |
| 9 | Body follows S+0x20 alone (basis for smooth B) | E5 slot mode |
| 10 | Lua thread differs from the player-update thread (race) | Residual; watch E3/E4 stability. A breakpoint is not available to the operator. |
| 11 | OnUpdate cadence on the unfocused window | E0 `partner_tick=rate` |
| 12 | `vars.playerRotY` units; `vars.playerPos*` freshness | E0 `partner_units` |
| 13 | Thrash when the partner leaves the loaded area | E4 and E6: no `partner_realized=0` |
| 14 | Teardown with a realized instance | E8 (clear vs keep) |
| 15 | Trap callbacks with a non-local player | Watch `crash_probe` during E4 orbit and E6 near triggers. No mitigation in this spike. |
| 16 | Offline boot dialog or hang; contact with official servers | E0 and the safety gate |
| 17 | Log flooding or per-frame cost | E0 log growth (the tick receipt is quiet); `native_us_*` in stats |
| 18 | Remote coordinates differ between the two games | E6 cross-check of `pos0` against the peer's `pos1` |

**Size estimate:** about 950 native LOC (about 450 of them portable and unit-tested), about 110 Lua and about 250 PowerShell. Every new code path is inert unless `offline-v1` and `partner=1` are both set.