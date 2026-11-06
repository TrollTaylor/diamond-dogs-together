#pragma once
// Enemy sync M7 FM1 (docs/world-sync-design.md 7.15, the far mirror; plan "owner-brains" act layer): probe P1.
// The engine's own FOB act sync (native/soldier_act_contract.hpp, tools/generate_soldier_act_contract.py) serialises each
// guard's 0x1e0-byte request record R (written by his brain, read by his body) on the owner and applies it on the other
// peer. This module hooks the request manager update 0x13735c0 (this = M, every frame, the soldier thread) read-only for
// the research census (lab key act_census): every 0.5 s one soldier_act_census line per realized guard within 250 m of
// the local player (the network-position table of U1, S's mode/peer/owner bytes, R's flags, the request and order packed
// with the engine's own 0x13704a0 / 0x1370900 into scratch buffers, gait / posture / kinds, the aim point, the weapon
// bytes), a soldier_engage_bins line per second (Defend hits on the local player per shooter, with his distance), and a
// soldier_act_bands line every 10 s (realized copies per 25 m band: the realize radius).
// The research command act_probe (U2: does a puppet's body run a posted request?) puppets one guard (SetPuppet's bit 27
// of his flag record) and posts another guard's request to him every update for a while on this one copy.
#include <cstddef>
#include <cstdint>

namespace mgscoop::soldier_act {

inline constexpr unsigned kMaxSoldiers = 512;
inline constexpr float kCensusRangeM = 320.0f;  // past the far mirror's 300 m range (lua/coop/world_mirror.lua M.RANGE)
inline constexpr std::uint64_t kCensusPeriodMs = 500, kBinsPeriodMs = 1000, kBandsPeriodMs = 10000, kPayloadPeriodMs = 2000;
inline constexpr unsigned kBands = 11;  // 25 m bands 0..250 m, then beyond

// The fixed fields of a packed 0x70 request (0x13704a0's writes, docs above).
struct Fixed {
    std::uint8_t path{}, dest_mode{}, kind_a{}, kind_b{}, gait{}, b66{}, posture{}, face_mode{}, f69{}, f6a{}, f6b{}, f6c{},
        f6d{}, weapon{};
    float dest[3]{};  // the packed +0x40 position (22/20/22 bits, 1 cm), dest modes 0..2
};
Fixed DecodeFixed(const unsigned char (&wire)[0x70]) noexcept;
// The engine's packed position (0x136f810's decode): x = (v >> 42) - 0x200000, y = ((v >> 22) & 0xfffff) - 0x80000,
// z = (v & 0x3fffff) - 0x200000, each times 0.01 m; v is the little-endian u64 at the field.
void DecodePacked(std::uint64_t v, float (&out)[3]) noexcept;
std::uint64_t EncodePacked(const float (&in)[3]) noexcept;
// Action kinds that 0x136f810 gives the catch-up (the moving kinds).
bool MovingKind(std::uint8_t kind) noexcept;
// The 25 m band of a distance (kBands - 1 beyond 250 m).
unsigned Band(float metres) noexcept;
// Lower-case hex of `size` bytes into out (2 * size + 1); false when it does not fit.
bool Hex(const unsigned char* bytes, std::size_t size, char* out, std::size_t capacity) noexcept;

// FM1, the far mirror (lab key soldier_act; docs/world-sync-design.md 7.15). The owner's copy packs each mirrored guard's
// request and order (0x13704a0 / 0x1370900, the hook thread) and sends them as SOLDIER_ACT parts; the other copy's puppet
// takes them (0x136f810 with its network-position entry pre-filled with its own position, then R+0x19e |= 0x80; the
// order 0x13701f0 every update, R+0x1d2 |= 1) and runs them with its own legs, aim and fire motions.
inline constexpr std::uint64_t kActMinMs = 100, kActResendMs = 1000, kActFreshMs = 1500;
// The puppet side re-applies an unchanged request only every kActReapplyMs (the 1 s resend keeps the stream fresh; the
// FOB peer applies only new requests, and a re-posted request restarts its motion).
inline constexpr std::uint64_t kActReapplyMs = 5000;
// FM1 round 6 (docs/world-sync-design.md 7.19): while the owner copy moves, an unchanged request is re-applied every
// kActReapplyMovingMs, so the engine's catch-up in the unpack (gait + 1 at 2..5 m from the owner position, gait 3 and that
// destination beyond 5 m) runs every second (runs adecad01 / 80cc0176: with 5 s the walkers lagged 5-12 m, speed ratio p50
// 0.19 / 0.78). The owner copy moves while its order-part position is >= kActMoveM (3D) from where it was at the last apply.
inline constexpr std::uint64_t kActReapplyMovingMs = 1000;
inline constexpr float kActMoveM = 0.3f;
bool OwnerMoved(const float (&was)[3], const float (&now)[3]) noexcept;
// Whether the hook unpacks the mailbox request now: a first (or re-mirrored) or changed request at once, an unchanged one
// kActReapplyMovingMs after the last apply while the owner copy moves, else kActReapplyMs.
bool ReapplyDue(bool first, bool changed, bool moving, std::uint64_t age_ms) noexcept;
// The ACT_V1 p reply "return {p=1,pl=<limit>}": the local player taken, and the phase limit of lab key soldier_act (0 every
// phase, soldier_act=1; 1 calm / caution only, soldier_act=calm), which the far rule reads (world_mirror.lua M.lim).
bool FormatPlayerReply(unsigned phase_limit, char* out, std::size_t capacity) noexcept;
inline constexpr unsigned kActPartsPer100Ms = 24;
// Players 0 and 1 swap between the copies (GameObjectId type 0); any other id is unchanged.
std::uint16_t RemapPlayer(std::uint16_t id) noexcept;
// U3 (0x1413816e0, the action B kind 1 serialiser = the aim): its target mode is the byte at wire +0x35 (R+0x4c & 7);
// modes 2 (u16 id + u16 part) and 4 (u16 id) carry the target's GameObjectId big-endian at wire +0x20 (B+0x00), mode 1 a
// packed position, mode 3 a u32. Census runs 3bc7280b / 963ab276: 1552 of 1669 host aims are mode 4, every one id 0 (the
// host's own player), so the puppet would aim at the guest without the swap. Swaps a player id there (RemapPlayer); true when changed.
// The u16 0x0400 the census scan found at wire +0x34 is B+0x14 = R+0x5c (0) and B+0x15 = this mode (4): not an id.
bool RemapAimTarget(unsigned char (&wire)[0x70]) noexcept;
// Whether two packed requests differ beyond jitter: the destination (+0x40) moved >= 5 cm, a facing position (face mode
// 2, +0x50) >= 5 cm, a facing yaw (face mode 1, u16 at +0x50) by >= 1 degree (182 of 65536), or any other byte.
bool RequestChanged(const unsigned char (&a)[0x70], const unsigned char (&b)[0x70]) noexcept;
// Orders: a target position (mode 1, packed at +0) moved >= 5 cm, or any other byte.
bool OrderChanged(const unsigned char (&a)[0x18], const unsigned char (&b)[0x18]) noexcept;
// Round 6, run fbf3522a (design 7.19): the order unpack 0x1413701f0 writes the target (+0x00..+0x0b) and its mode (the low
// 3 bits of +0x0c) only for packed modes 1..4 (wire +0x0e); a packed order without a target leaves the puppet's own
// stale one (his brain's, from before he was a puppet), and his legs walked to it, away from the host copy's request
// (guard 15: 15.7 m off at the host copy's speed). Whether a packed order names a target (wire +0x0e is 1..4):
bool OrderHasTarget(const unsigned char (&packed)[0x18]) noexcept;
// Clears the target of an unpacked order record (R+0x1b0, 0x20 bytes): +0x00..+0x0b zero, +0x0c's low 3 bits zero (as the
// owner's record of an order without a target); true when it held one.
bool ClearOrderTarget(unsigned char (&record)[0x20]) noexcept;
// Round 6, runs fbf3522a / f11fe8a1 (design 7.19): a puppet keeps walking his own leg (the destination his own brain had
// before the take) at the host copy's speed although his record holds the host copy's request (packed byte-identical):
// takes whose own destination was the host copy's replayed at p50 0.0 m, the others at 3.5 m (guards 15 / 18: 56-58 m
// apart, 7-11 m off). A candidate is taken only when the host copy's request is not a move (action A kind 1) or this
// copy's own destination (R+0x80) is within kActLegM (flat) of its destination; unknown own destination: not a move match.
inline constexpr float kActLegM = 3.0f;
bool LegMatch(const unsigned char (&host_request)[0x70], const float (&own_dest)[3], bool own_valid) noexcept;
// The rate cap: a change goes out kActMinMs after the last send at the earliest, an unchanged record every kActResendMs.
bool DueSend(bool changed, std::uint64_t last_ms, std::uint64_t now_ms) noexcept;
// The network-position entry 0x136f810 reads (0x18 bytes): xyz, yaw, flags +0x14 bit 0 (valid).
void PrefillEntry(unsigned char (&entry)[0x18], const float (&pos)[3], float yaw) noexcept;
// Enemy round 8 track A (quiet_place, docs/world-sync-design.md 7.22): the quiet warp 0x136ece0 (the request unpack's
// network-position warp) moves a copy only when his slot position is more than 15 m (225.0 squared) from the target. For
// a nearer correction the slot position is biased kQuietBiasM along x first (the function overwrites it with the target
// before anything reads it). True when `biased` is to be written; false when the warp moves him as it is.
inline constexpr float kQuietWarpSq = 225.0f, kQuietBiasM = 20.0f;
bool QuietBias(const float (&slot)[3], const float (&target)[3], float (&biased)[3]) noexcept;
// "world_place=quiet; go=; seq=; moved=; bias=; err_m=; dy_m=\r\n" (err_m: x/z, dy_m: y of the slot read back after the
// call minus the target). False when it does not fit.
bool FormatQuietLine(std::uint32_t go, std::uint16_t seq, unsigned moved, unsigned bias, float err_m, float dy_m, char* out,
                     std::size_t capacity) noexcept;
// Enemy round 9 track A (quiet_place, docs/world-sync-design.md 7.24): the quiet move only for a guard at rest in his own
// brain and in the realized list the warp reads. Run 1befcef4: after 22 of his 43 quiet moves guard 1904171355 walked
// 9.4-14.1 m on along his brain's old path (the warp leaves it; the set-transform message's kind 3 had stopped him), and
// in 5 of the 8 off-target moves the body did not move while the slot table read the target (0x136ece0 writes the slot,
// then moves the body only for an idx in the u16 list *(S+0x18)+0x28, count +0x44). The hook keeps a rest view of every
// realized guard (every kRestMs): listed, his slot speed on the ground plane over the last two refreshes (-1 unknown), and
// his follower's path (planner vt[0x20], follower vt[0x90] / vt[0xa0]: has, its last point).
struct RestView {
    std::uint64_t at;  // 0: none
    bool realized, listed, has;
    float speed_mps;
    float pos[3], end[3];
    // Enemy round 11 track A (design 7.31): the stance nibble S+0x98 records +0x34 & 0xf the set-transform handler reads
    // (0xff: not read).
    std::uint8_t stance = 0xff;
};
// Round 11 (design 7.31): walk = a walking guard or one with a path ahead: the quiet move, then his re-plan (QuietPlan);
// stance = stance 3-4 or unread: the message (its rotated pose; no action post there). moving / path are no longer given.
enum class QuietWhy : std::uint8_t { ok, unknown, stale, unrealized, unlisted, moving, path, walk, stance };
inline constexpr std::uint64_t kRestMs = 250, kRestStaleMs = 1500;
inline constexpr float kRestSpeedMps = 0.5f, kRestPathM = 2.0f;
// ok only when the view is at most kRestStaleMs old, realized, listed, his speed known and at most kRestSpeedMps, and no
// path of his ends more than kRestPathM (x/z) from both where he stands and the target (he would walk there after it).
QuietWhy QuietChoose(const RestView& r, const float (&target)[3], std::uint64_t now_ms) noexcept;
const char* QuietWhyName(QuietWhy why) noexcept;
// Enemy round 11 track A (design 7.31): after a quiet move of a walker, his re-plan from where he now stands to his own
// brain's destination R+0x80 (the shadow plan 0x1413725f0 of act_plan): only for a started walk (dest mode 1,
// PlanGate); his brain's live slot of another type is cancelled first (PlanSlot); another type with no handle: busy.
// slot_read: whether his planner slot was read at all (the review: an unread slot is no empty slot): none if not.
enum class QuietPlan : std::uint8_t { none, busy, plan, cancel_then_plan };
QuietPlan QuietPlanChoose(unsigned dest_mode, std::uint16_t r19c, std::uint16_t r19e, std::uint64_t slot_hash,
                          std::uint16_t slot_handle, bool slot_read) noexcept;
const char* QuietPlanName(QuietPlan step) noexcept;
// Enemy round 12 track A (design 7.31): which place warps ask for the quiet move (QuietCheck then decides). off_screen:
// the guest's Lua said the guard is off its player's screen; host: the host's M3 placement (WORLD_WARP_HOST_V1);
// correction: grant kind 3; go: the GameObjectId (type 2 = soldier).
bool QuietAsk(bool quiet_place, bool off_screen, bool host, bool correction, std::uint32_t go) noexcept;
// Whether the request manager hook (and its rest views) runs the quiet moves on this role: lab key, soldier_follow, and
// for a host soldier_owner (M3).
bool QuietHook(bool quiet_place, bool soldier_follow, bool soldier_owner, bool host) noexcept;
// Whether idx is among the first count entries of the realized list.
bool ListHas(const std::uint16_t* list, std::uint32_t count, unsigned idx) noexcept;
#ifdef _WIN32
// The same read under SEH (the review: QuietOne read the game's list outside its __try): 1 listed, 0 not, -1 fault.
int ListFind(const std::uint16_t* list, std::uint32_t count, unsigned idx) noexcept;
#endif

// Enemy round 8 track C (docs/world-sync-design.md 7.23; native-only lab key act_plan, off unless named; no CFG bit, no
// wire): the walk-request start 0x1413725f0 plans a path only for a FOB remote copy (S+0x2a4 bit 2 and the owner byte !=
// S+0x2a8). Offline it only sets R+0x19e bit 1, and the path step 0x141372b70 drives the puppet along whatever path his
// follower holds: his own brain's old one (the enemy7 C-fm1 plan, finding 1). With act_plan the hook, after the manager
// update's original, calls 0x1413725f0 again for each puppet whose mode-1 request it applied this update, with a shadow
// gate object, so the planner plans from where he stands to R+0x80 (the FOB life cycle: the next start cancels the
// handle R+0x19c). Plan when: dest mode 1, R+0x19c holds no handle (0xffff), (R+0x19e & 3) == 2 (the offline start ran),
// and the request was applied this update.
bool PlanGate(unsigned dest_mode, std::uint16_t r19c, std::uint16_t r19e, bool applied_now) noexcept;
// The shadow gate: 0x1413725f0 reads its first argument only at +0x70 -> S' (+0x2a4 bit 2 online; +0x2a8 this peer; +0x18
// -> +0x18 the owner bytes, [idx] != +0x2a8 = a remote copy). BuildShadow links p -> s -> slots -> owner (all 1, peer 0).
struct alignas(16) Shadow {
    unsigned char p[0x80];
    unsigned char s[0x2b0];
    unsigned char slots[0x20];
    unsigned char owner[kMaxSoldiers];
};
void BuildShadow(Shadow& shadow) noexcept;
// What 0x1413725f0's gate (0x141372635..0x141372664) reads from a first argument p for guard idx: true = it plans.
bool ShadowGatePasses(const void* p, unsigned idx) noexcept;
// The gait catch-up (R+0xb0 bits 1..3) after the unpack, after the unpack's own mode 0 rule (0x14136f810) for a mode 1 walk
// but from kPlanCatchM (round 9 C; the engine's rule starts at 2 m): the owner copy ahead by 1..5 m: gait + 1 (cap 3);
// beyond 5 m: 3; never lowered; not ahead (<= 1 m, NaN): unchanged.
unsigned GaitCatchUp(float ahead_m, unsigned gait) noexcept;
// How far (flat) the owner copy is ahead of this copy toward dest: their distance when the owner copy is nearer dest, else 0.
float AheadM(const float (&self)[3], const float (&owner)[3], const float (&dest)[3]) noexcept;
// The lent entry's yaw (radians; the 15 m warp 0x14136ece0 writes it to the slot): face mode 1 (wire +0x68) -> the request's
// facing (wire +0x50, big-endian int16 * pi/32767 as the unpack's 0x142338970), else `fallback` (this copy's own yaw).
float EntryYaw(const unsigned char (&wire)[0x70], float fallback) noexcept;
// A walker (action A kind 1) is taken only within kActTakeNearM (flat) of the owner copy (LegMatch tests destinations, not
// positions; a take beyond 15 m opens with the unpack's warp). Unknown positions: not taken. Other kinds: as before.
inline constexpr float kActTakeNearM = 10.0f;
bool TakeNear(const unsigned char (&host_request)[0x70], const float (&self)[3], const float (&owner)[3], bool have) noexcept;
// Whether the unpack's 15 m warp moves this copy: beyond 225.0 squared (3D) of the entry position.
bool Warp15(const float (&self)[3], const float (&entry)[3]) noexcept;
// A plan's path end within kPlanEndM (3D) of R+0x80; checked kPlanCheckMs after the plan (soldier_act_plan line).
inline constexpr float kPlanEndM = 1.0f;
inline constexpr std::uint64_t kPlanCheckMs = 300;
bool PlanEndNear(const float (&end)[3], const float (&dest)[3]) noexcept;
// Session 1 (run fcfb3acc: the planner *(S+0x30) has vtable 0x23a1610; read-only decompile of vt[0] 0x140fa2430): the
// per-guard slot *(planner+0x48)+idx*0x10 is {u48 type hash, u16 handle +8, u16 counter +0xa, u16 done +0xc}; vt[0]
// refuses (0xffff) a request whose type hash (move +0x70) differs from a non-zero slot hash. The request manager's move
// hash (0x141372694) is kMoveHash. A slot his own brain's walk holds with a live handle is cancelled first (vt[0x10] with
// that handle, the engine's cancel: the query and the slot); one with another type and no handle is left alone (busy).
inline constexpr std::uint64_t kMoveHash = 0x2c505bab2efcull;
enum class SlotStep { plan, cancel_then_plan, busy };
SlotStep PlanSlot(std::uint64_t slot_hash, std::uint16_t slot_handle) noexcept;
// When a puppet is released, the slot is still ours (kMoveHash) and its live handle is the one R+0x19c holds: cancel it,
// so his brain's next walk is not refused.
bool ReleaseCancel(std::uint64_t slot_hash, std::uint16_t slot_handle, std::uint16_t r19c) noexcept;
// Enemy round 9 track C (act_plan, design 7.26): plan once per leg. Run 226fc789 (act_plan=1): 96 % of the plans ended
// within 1 m of R+0x80, but every moving owner's 1 s re-apply restarted the request (the engine's start cancels R+0x19c)
// and so planned again (guard 18: 98 plans, one per 1.01 s), and the puppet trailed the host copy by about 1 s (place error
// p50 1.31 / p90 2.96 m): the catch-up raised the gait only beyond 2 m. Now, with act_plan:
// - the catch-up starts at kPlanCatchM: the owner copy ahead by 1..5 m: gait + 1 (cap 3); beyond 5 m: 3;
// - a due re-apply of a walk is held (no unpack, no restart, no new plan) when PlanHold: not a first apply; the request is
//   the planned one but for a walk destination (wire +0x40, dest mode wire +0x61 == 1) moved at most kPlanMoveM
//   (RequestChangedFar false); our plan lives (PlanLiveFor: R+0x19c is the handle of our last plan, his planner slot holds
//   our type with that handle, and its 300 ms check did not find the path end far or missing); the unpack's 15 m warp
//   would not move him; and the catch-up gait now equals the gait R holds (a gait change needs a restart: each command
//   copies the gait from R when it is issued).
inline constexpr float kPlanCatchM = 1.0f;
inline constexpr float kPlanMoveM = 1.0f;
bool RequestChangedFar(const unsigned char (&planned)[0x70], const unsigned char (&now)[0x70]) noexcept;
bool PlanLiveFor(std::uint16_t ours, std::uint16_t r19c, std::uint64_t slot_hash, std::uint16_t slot_handle, bool check_bad) noexcept;
bool PlanHold(bool first, bool changed_far, bool plan_live, bool warp, unsigned gait_now, unsigned gait_want) noexcept;
// Enemy round 10 track C (act_plan, design 7.29; run 226fc789: 1 of 4 calm walkers taken, the others waited on LegMatch
// (leg_wait 775) and the 10 m rule (near_wait 53) and stayed 17-45 m off). With act_plan a walker is taken whatever his leg
// or distance: the plan walks him onto the host copy's path, and beyond 15 m the take opens with the unpack's warp. Without
// it, LegMatch as round 6. 0 taken, 1 leg_wait, 2 near_wait.
// Round 11 C: the third argument is no_warp (!TakeWarps): with act_plan a take that would open with the unpack's warp
// waits (near_wait); a walk glides (GlideEntry), so walkers are still taken whatever their leg or distance.
unsigned TakeWait(bool plan, bool leg_match, bool no_warp) noexcept;
// The plan check (round 10 C; 226fc789 / c0c3d088: 4 and 2 plans with no path yet at 300 ms): kPlanCheckMs after a plan,
// then every kPlanCheckMs up to kPlanRechecks more times while the path is missing or ends beyond kPlanEndM of R+0x80 (the
// query may still run); the last check's verdict is the one counted.
inline constexpr unsigned kPlanRechecks = 4;
enum class PlanVerdict { again, ok, partial, nopath };
PlanVerdict PlanCheckVerdict(bool path, bool end_near, unsigned tries) noexcept;
// A partial path (round 10 C; 77785f31: caution runners stood while re-planned every 1 s on partial paths, then jumped
// 16 m): its end beyond kPlanEndM of R+0x80 at the last check. The hold keeps it while this copy is more than kPlanPartialM
// (flat) from that end; nearer, the next due re-apply plans again from where he stands. Only no path at all is "bad".
inline constexpr float kPlanPartialM = 3.0f;
bool PartialHolds(bool partial, const float (&self)[3], const float (&end)[3]) noexcept;
// The release (round 10 C; the release freeze of 77785f31: five released walkers stood 34-48 s in ka 1 / dm 1 with an
// empty slot, because the release cancelled our plan and left the host copy's walk in R). A released copy whose R holds a
// walk (dest mode 1) walks it on to R+0x80: our live plan is kept (keep), or with no handle, (R+0x19e & 3) == 2 and a free
// slot it is planned again (replan). Not a walk: our live handle is cancelled as before (cancel). His brain's type in the
// slot: left alone (none).
enum class ReleaseStep { none, cancel, keep, replan };
ReleaseStep ReleaseChoose(unsigned dest_mode, std::uint16_t r19e, std::uint64_t slot_hash, std::uint16_t slot_handle,
                          std::uint16_t r19c) noexcept;
// A kept / re-planned released walk ends with our handle cancelled (finish: R+0x19c 0xffff) once he has stood within
// kReleaseArriveM (flat) of R+0x80 for kReleaseArriveMs, or after kReleaseWalkMs; it is over with nothing to do once R+0x19c
// or his slot no longer holds our handle (the engine's start of his brain's next request cancelled it). near_ms: how long
// he has been within kReleaseArriveM (0: not).
inline constexpr float kReleaseArriveM = 1.5f;
inline constexpr std::uint64_t kReleaseArriveMs = 2000, kReleaseWalkMs = 180000;
enum class ReleaseWalk { walk, finish, over };
ReleaseWalk ReleaseWalkStep(bool ours_live, std::uint64_t near_ms, std::uint64_t age_ms) noexcept;
// Enemy round 11 track C (act_plan, design 7.33; runs 15aa58cb / 689e654c).
// - MirrorLease: with act_plan an m whose mail is stale keeps the hook's last fresh lease (it lapses kActFreshMs after the
//   last fresh m) instead of ending it at once (689e654c: the hook released guard 18 and took him again 10 times while
//   Lua held him: one late resend over -WanSim bad). A fresh m stamps now.
std::uint64_t MirrorLease(bool fresh, std::uint64_t was_ms, std::uint64_t now_ms) noexcept;
// - GlideEntry: with act_plan a walk's lent entry (wire +0x61 == 1) lies at most kGlideM (3D) from this copy, toward the
//   owner copy, so the unpack's 15 m warp never moves him (689e654c: 5 take warps of 18-49 m a run, 5 at once at the
//   caution retake): the plan and the catch-up gait (from the owner copy's real position) walk or run him onto the host
//   copy's walk. True when clamped; else entry = owner.
inline constexpr float kGlideM = 14.0f;
bool GlideEntry(const float (&self)[3], const float (&owner)[3], float (&entry)[3]) noexcept;
// - TakeWarps: a take that would open with the unpack's warp: a request that is not a walk (wire +0x61 != 1) with this copy
//   beyond 15 m (3D, Warp15) of the owner copy; a walk glides. Unknown positions: no warp (the unpack lends his own).
bool TakeWarps(const unsigned char (&host_request)[0x70], const float (&self)[3], const float (&owner)[3], bool have) noexcept;
// - The release watch: for kWatchMs after a release (and after a released walk's end) a copy whose brain re-targeted a
//   walk with no path (ReleaseFreeze: dest mode 1, (R+0x19e & 3) == 2, R+0x19c 0xffff, a free or move slot, no follower
//   path, R+0x80 beyond kReleaseArriveM flat) for kWatchFreezeMs is planned to R+0x80 with the shadow plan (689e654c:
//   guard 15 stood 15 s at his kept walk's end: his brain set R+0x80 to its next route point with no new request, R+0x19c
//   and his slot cleared, no path). His brain's own type in his slot, or kWatchMs, ends the watch.
inline constexpr std::uint64_t kWatchMs = 60000, kWatchFreezeMs = 500;
bool ReleaseFreeze(unsigned dest_mode, std::uint16_t r19e, std::uint16_t r19c, std::uint64_t slot_hash, std::uint16_t slot_handle,
                   bool path, float d_dest_m) noexcept;
enum class WatchStep { wait, plan, end };
WatchStep WatchChoose(bool freeze, std::uint64_t freeze_ms, std::uint64_t age_ms, std::uint64_t slot_hash) noexcept;
// - CornerTarget (the owner side): with act_plan the host sends a walk's destination as the next point of the host copy's
//   own follower path, not the leg's end (15aa58cb / 689e654c: guard 15's host copy walked the route's L - north 15 m,
//   then east - while the puppet's plan took the straight line to the leg's end and led by p50 3 m, p90 6.6-10.8 m). The
//   point after the segment nearest `self` (flat); within kCornerM (flat) of it, the one after. False (send the leg's end)
//   with fewer than 2 points or when that point lies within kPlanEndM (3D) of `dest`.
inline constexpr float kCornerM = 2.0f;
bool CornerTarget(const float (*points)[3], unsigned count, const float (&self)[3], const float (&dest)[3], float (&target)[3]) noexcept;
// Enemy round 13 track C (design 7.33 round 13; the user's decision D-FM3 (b)): a far-mirror walk may open with the
// unpack's 15 m warp onto the owner copy's spot (no glide) only while both this copy's and the owner copy's spots are
// outside the guest's view. Lua stamps that per puppet with ACT_V1 w after his m (the M-series 100-degree camera cone of
// world_place_guest.lua with no distance cut: binoculars and scopes look along the camera's yaw); the stamp lapses
// kOffViewMs after the last w. GlideOn: with act_plan a walk (wire +0x61 == 1) with this copy's position known glides
// (GlideEntry) unless the stamp is fresh.
inline constexpr std::uint64_t kOffViewMs = 1000;
bool OffViewFresh(std::uint64_t stamp_ms, std::uint64_t now_ms) noexcept;
bool GlideOn(bool plan, bool walk, bool have_self, bool off_view) noexcept;
// Round 13 C (run 1fb060aa: guard 18's host copy stood 44.8 m from his guest copy at pairing, so his take waited on
// near_wait and came 11 s later in the guest's view as a 35.9 m glide): TakeNoWarp - a take that would open with the
// unpack's warp (TakeWarps) is allowed while the off-view stamp is fresh (Lua sends ACT_V1 w for a candidate too, from the
// q reply's owner position, then asks q again).
bool TakeNoWarp(bool take_warps, bool off_view) noexcept;
// Round 13 C (1fb060aa: guard 246's host copy stood 78 s with a walk request and a follower path while his puppet walked
// the corner target 12.7 m ahead and waited there; the place error counted 12.7 -> 0.7 m as the host copy then walked up):
// with act_plan the host sends a walk's destination as the host copy's own position while he has stayed within
// kOwnerStillM (flat) of one spot for kOwnerStillMs. OwnerStill keeps that spot and its time per guard (ref, ref_at).
inline constexpr float kOwnerStillM = 0.5f;
inline constexpr std::uint64_t kOwnerStillMs = 2000;
bool OwnerStill(const float (&self)[3], float (&ref)[3], std::uint64_t& ref_at, std::uint64_t now_ms) noexcept;

#ifdef _WIN32
using LogFn = void (*)(const char* line) noexcept;
// Round 8 track C: lab key act_plan (the runtime thread, after ConfigureMirror).
void SetPlan(bool on) noexcept;
bool PlanOn() noexcept;
// `base`/`span`: the image. Verifies every site and slot of soldier_act_contract.hpp and hooks the manager update (MinHook).
// Logs soldier_act=installed | refused; reason=.... Idempotent. The hook does nothing but remember M until the census or
// a probe is on.
bool Install(const void* base, std::size_t span, LogFn log) noexcept;
bool Installed() noexcept;
void SetCensus(bool on) noexcept;
bool CensusOn() noexcept;
// The local player position for the census distances (the runtime thread publishes it; the hook reads it).
void SetLocalPlayer(const float (&pos)[3], std::uint64_t now_ms) noexcept;
// U2 probe: puppet guard `dst` (SetPuppet bit) and post guard `src`'s request to him every update for `ms` (<= 30000);
// dst/src 0xffff = auto (dst: the realized guard nearest the local player within 40 m; src: a realized guard 8..60 m
// from him, a moving one first). mode 0 native (pack 0x13704a0 + unpack 0x136f810 with the entry pre-filled; refused
// unless the network-position table covers dst), 1 raw (the post 0x1368980 of src's own request bytes), 2 auto (native
// when the table covers dst, else raw). False when one is already running or the hook is not installed.
bool StartProbe(std::uint16_t dst, std::uint16_t src, std::uint32_t ms, unsigned mode) noexcept;
// "act;installed=;census=;m=;s=;calls=;lines=;faults=;probe=..." for the act_status reply.
bool StatusLine(char* out, std::size_t capacity) noexcept;
// FM1 (the runtime thread). Configure: soldier_act on this copy (both roles; the hook is installed with it).
void ConfigureMirror(bool on, bool host) noexcept;
bool MirrorOn() noexcept;
// Owner: send guard `idx` (roster `slot`) for kActFreshMs; the hook packs it every update.
void SetTx(unsigned idx, std::uint16_t slot, std::uint64_t now_ms) noexcept;
// Owner: sends the staged parts (world_link::SendAct), at most kActPartsPer100Ms per 100 ms. Parts sent.
unsigned SendStaged(std::uint64_t now_ms) noexcept;
// Puppet side: mirror guard `idx` (roster `slot`) or release him. On, pulls the newest SOLDIER_ACT of the slot into the
// hook's mailbox; true when the request is fresh (kActFreshMs). Off clears his mailbox and the fire-drop bit.
bool SetMirror(unsigned idx, std::uint16_t slot, bool on, std::uint64_t now_ms) noexcept;
// Round 13 C (D-FM3 (b)): ACT_V1 w - puppet `idx`'s and his owner copy's spots are outside the guest's view now (OffViewFresh).
void SetOffView(unsigned idx, std::uint64_t now_ms) noexcept;
void ReleaseAll() noexcept;
// Candidate side: whether the slot's newest SOLDIER_ACT request is fresh (kActFreshMs); arms nothing (no mailbox, no
// hook apply, no fire drop). Lua sends m only once the guard is a puppet (run 3bc7280b: m on candidates armed the fire
// drop of 9 guards whose own brain still ran).
// Round 6: and, for a host copy's move, this copy's own destination on his leg (LegMatch; frame line leg_wait).
bool QueryFresh(std::uint16_t slot, unsigned idx) noexcept;
// Research command "mirror off|on": while paused, ACT_V1 m is refused "paused" (Lua releases every puppet).
void SetPaused(bool paused) noexcept;
bool Paused() noexcept;
struct Counters {
    std::uint32_t tx_parts, staged, rx, applied, orders, refuse_table, refuse_fault, refuse_stale, remap_player, payload_ids,
        mirrored, fire_dropped, noise_dropped, local_shots, frame_p95_us, frames;
    // Round 6: due = request applies the rule asked for (first, changed, moving 1 s, still 5 s) on realized puppets;
    // reapply_moving = those that were a moving owner's 1 s re-apply; same / unrealized / expired as the frame line's.
    std::uint32_t due, reapply_moving, same, unrealized, expired, order_clear, leg_wait;  // order_clear: stale order targets cleared; leg_wait: walkers not taken (LegMatch)
    // Round 8 track C (act_plan): plan = 0x1413725f0 called and returned 1; plan_skip = a mode-1 apply not planned (PlanGate
    // false or no planner / follower); warp15 = unpacks whose 15 m warp moved the copy (soldier_act_warp15 line, read back);
    // plan_ok / plan_far / plan_nopath = the path end kPlanCheckMs after a plan within / beyond kPlanEndM of R+0x80, or no
    // path; near_wait = walkers not taken beyond kActTakeNearM of the owner copy (TakeNear).
    std::uint32_t plan, plan_skip, warp15, plan_ok, plan_far, plan_nopath, near_wait;
    std::uint32_t gait_up, plan_fault;  // gait_up: requests whose gait the catch-up raised; plan_fault: PlanStep faults
    // plan_cancel: a slot of another type cancelled before the plan; plan_busy: another type with no handle (not planned);
    // plan_none: 0x1413725f0 ran but the planner refused (R+0x19c 0xffff); plan_release: our handle cancelled at a release.
    std::uint32_t plan_cancel, plan_busy, plan_none, plan_release;
    std::uint32_t plan_hold;  // round 9 C: due re-applies held (PlanHold: no restart, no new plan)
    // Round 10 C: rel_keep / rel_replan = released walks kept / planned again (ReleaseChoose); rel_end = released walks
    // finished or over; recheck = plan checks run again (PlanCheckVerdict again).
    std::uint32_t rel_keep, rel_replan, rel_end, recheck;
    // Round 11 C: glide = walk applies whose entry GlideEntry clamped; rel_watch = released copies planned by the watch;
    // corner = host requests sent with the host copy's next path point (CornerTarget).
    std::uint32_t glide, rel_watch, corner;
};
Counters ReadCounters() noexcept;
// The 1 Hz "soldier_act_frame; t=..; ..." line (CRLF-terminated): the frame interval p95, the counters, payload_ids (the
// aim-target swaps, RemapAimTarget), due and reapply. False when it does not fit.
bool FormatFrameLine(std::uint64_t now, const Counters& k, char* out, std::size_t capacity) noexcept;
// The ACT_V1 reply, a Lua table constructor: {f,tx,rx,ap,od,sh,mi,fd,fn} (od: orders re-posted; never a Lua reserved
// word as a key: run 963ab276's {..,or=..} did not compile in Lua, so the guest's Lua never saw f=1 and set no puppet).
// With owner_mm (the m reply), ",ox=,oy=,oz=" the owner copy's position in mm from the newest order part (run 94a7bf2a:
// guard 19's host copy climbed a tower; his puppet stood at its foot 7 m off, and nothing warps a copy under 15 m).
bool FormatActReply(bool fresh, const Counters& k, char* out, std::size_t capacity, const std::int32_t* owner_mm = nullptr) noexcept;
// The owner copy's position (mm) in guard idx's mailbox (the order part, flag bit 0); false when none.
bool MailOwner(unsigned idx, std::int32_t (&mm)[3]) noexcept;
// Enemy round 9 (quiet_place, design 7.24): QuietChoose on the hook's newest rest view of guard idx (unknown before the
// hook has made one). Runtime thread; read only. SetQuietRest turns the hook's rest views on (the guest with the key).
QuietWhy QuietCheck(unsigned idx, const float (&target)[3]) noexcept;
void SetQuietRest(bool on) noexcept;
// Stages a quiet move of guard idx (go: his GameObjectId, for the line) to pos / yaw: the hook runs 0x136ece0 in its next
// update (before the engine's own) and logs world_place=quiet. False when the hook is not installed or idx is out of range.
bool StageQuiet(unsigned idx, std::uint32_t go, std::uint16_t seq, const float (&pos)[3], float yaw, bool replan = false) noexcept;
#endif

} // namespace mgscoop::soldier_act
