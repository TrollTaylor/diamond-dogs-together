// Enemy sync M7 FM1 probe P1 (native/include/mgscoop/soldier_act.hpp): the request manager census and the U2 probe.
#include "mgscoop/soldier_act.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "mgscoop/soldier_authority.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/world_link.hpp"
#include "mgscoop/world_wire.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "MinHook.h"
#include "soldier_act_contract.hpp"
#endif

namespace mgscoop::soldier_act {

void DecodePacked(std::uint64_t v, float (&out)[3]) noexcept {
    out[0] = static_cast<float>(static_cast<std::int32_t>(static_cast<std::uint32_t>(v >> 42) - 0x200000u)) * 0.01f;
    out[1] = static_cast<float>(static_cast<std::int32_t>((static_cast<std::uint32_t>(v >> 22) & 0xfffffu) - 0x80000u)) * 0.01f;
    out[2] = static_cast<float>(static_cast<std::int32_t>((static_cast<std::uint32_t>(v) & 0x3fffffu) - 0x200000u)) * 0.01f;
}

std::uint64_t EncodePacked(const float (&in)[3]) noexcept {
    const auto clamp = [](float metres, std::int32_t lo, std::int32_t hi) noexcept {
        const float cm = metres * 100.0f;
        if (!(cm == cm)) return std::int32_t{0};
        if (cm <= static_cast<float>(lo)) return lo;
        if (cm >= static_cast<float>(hi)) return hi;
        return static_cast<std::int32_t>(std::lround(cm));
    };
    const std::uint64_t x = static_cast<std::uint32_t>(clamp(in[0], -0x200000, 0x1fffff) + 0x200000) & 0x3fffffu;
    const std::uint64_t y = static_cast<std::uint32_t>(clamp(in[1], -0x80000, 0x7ffff) + 0x80000) & 0xfffffu;
    const std::uint64_t z = static_cast<std::uint32_t>(clamp(in[2], -0x200000, 0x1fffff) + 0x200000) & 0x3fffffu;
    return (x << 42) | (y << 22) | z;
}

Fixed DecodeFixed(const unsigned char (&w)[0x70]) noexcept {
    Fixed f{};
    f.path = w[0x60];
    f.dest_mode = w[0x61];
    f.kind_a = w[0x63];
    f.kind_b = w[0x64];
    f.gait = w[0x65];
    f.b66 = w[0x66];
    f.posture = w[0x67];
    f.face_mode = w[0x68];
    f.f69 = w[0x69];
    f.f6a = w[0x6a];
    f.f6b = w[0x6b];
    f.f6c = w[0x6c];
    f.f6d = w[0x6d];
    f.weapon = w[0x6e];
    std::uint64_t v = 0;
    std::memcpy(&v, w + 0x40, sizeof(v));
    DecodePacked(v, f.dest);
    return f;
}

bool MovingKind(std::uint8_t kind) noexcept {
    switch (kind) {
    case 0x01: case 0x0b: case 0x0c: case 0x0d: case 0x11: case 0x14: case 0x15: case 0x16: case 0x18: case 0x1a: case 0x1f:
        return true;
    default:
        return false;
    }
}

unsigned Band(float metres) noexcept {
    if (!(metres >= 0.0f)) return kBands - 1;
    const unsigned b = static_cast<unsigned>(metres / 25.0f);
    return b < kBands - 1 ? b : kBands - 1;
}

bool Hex(const unsigned char* bytes, std::size_t size, char* out, std::size_t capacity) noexcept {
    if (!out || capacity < size * 2 + 1 || (size && !bytes)) return false;
    static const char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < size; ++i) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 15];
    }
    out[size * 2] = 0;
    return true;
}

std::uint16_t RemapPlayer(std::uint16_t id) noexcept {
    if ((id >> 9) != 0) return id;
    const unsigned index = id & 0x1ffu;
    return index == 0 ? 1 : index == 1 ? 0 : id;
}

bool RemapAimTarget(unsigned char (&w)[0x70]) noexcept {
    if (w[0x64] != 1) return false;  // action B kind (R+0xb5): 1 = the aim
    const unsigned mode = w[0x35] & 7u;  // B+0x15 = R+0x4c & 7
    if (mode != 2 && mode != 4) return false;
    const std::uint16_t was = static_cast<std::uint16_t>((w[0x20] << 8) | w[0x21]);
    const std::uint16_t now = RemapPlayer(was);
    if (now == was) return false;
    w[0x20] = static_cast<unsigned char>(now >> 8);
    w[0x21] = static_cast<unsigned char>(now & 0xffu);
    return true;
}

namespace {
bool PackedNear(const unsigned char* a, const unsigned char* b) noexcept {
    std::uint64_t va = 0, vb = 0;
    std::memcpy(&va, a, 8);
    std::memcpy(&vb, b, 8);
    float pa[3]{}, pb[3]{};
    DecodePacked(va, pa);
    DecodePacked(vb, pb);
    const float dx = pa[0] - pb[0], dy = pa[1] - pb[1], dz = pa[2] - pb[2];
    return dx * dx + dy * dy + dz * dz < 0.05f * 0.05f;
}
} // namespace

bool RequestChanged(const unsigned char (&a)[0x70], const unsigned char (&b)[0x70]) noexcept {
    if (std::memcmp(a, b, 0x40) != 0 || std::memcmp(a + 0x58, b + 0x58, 0x70 - 0x58) != 0) return true;
    if (!PackedNear(a + 0x40, b + 0x40) || std::memcmp(a + 0x48, b + 0x48, 8) != 0) return true;
    const std::uint8_t mode = a[0x68];
    if (mode == 2) return !PackedNear(a + 0x50, b + 0x50);
    if (mode == 1) {
        std::uint16_t ya = 0, yb = 0;
        std::memcpy(&ya, a + 0x50, 2);
        std::memcpy(&yb, b + 0x50, 2);
        const std::uint16_t d = static_cast<std::uint16_t>(ya - yb);
        const unsigned diff = d < 0x8000u ? d : 0x10000u - d;
        return diff >= 182u || std::memcmp(a + 0x52, b + 0x52, 6) != 0;
    }
    return std::memcmp(a + 0x50, b + 0x50, 8) != 0;
}

bool OrderChanged(const unsigned char (&a)[0x18], const unsigned char (&b)[0x18]) noexcept {
    if (std::memcmp(a + 8, b + 8, 0x18 - 8) != 0) return true;
    if (a[0xe] == 1) return !PackedNear(a, b);
    return std::memcmp(a, b, 8) != 0;
}

bool OrderHasTarget(const unsigned char (&packed)[0x18]) noexcept {
    return packed[0xe] >= 1 && packed[0xe] <= 4;
}

bool ClearOrderTarget(unsigned char (&record)[0x20]) noexcept {
    bool had = (record[0xc] & 7u) != 0;
    for (int b = 0; b < 0xc; ++b) {
        had = had || record[b] != 0;
        record[b] = 0;
    }
    record[0xc] = static_cast<unsigned char>(record[0xc] & 0xf8u);
    return had;
}

bool LegMatch(const unsigned char (&host_request)[0x70], const float (&own_dest)[3], bool own_valid) noexcept {
    if (host_request[0x63] != 1) return true;  // not a move: the leg does not matter
    if (!own_valid) return false;
    std::uint64_t v = 0;
    std::memcpy(&v, host_request + 0x40, sizeof(v));
    float d[3]{};
    DecodePacked(v, d);
    const float dx = d[0] - own_dest[0], dz = d[2] - own_dest[2];
    return dx * dx + dz * dz <= kActLegM * kActLegM;  // NaN: no match
}

bool DueSend(bool changed, std::uint64_t last_ms, std::uint64_t now_ms) noexcept {
    if (!last_ms) return true;
    const std::uint64_t age = now_ms >= last_ms ? now_ms - last_ms : 0;
    return changed ? age >= kActMinMs : age >= kActResendMs;
}

bool OwnerMoved(const float (&was)[3], const float (&now)[3]) noexcept {
    const float dx = now[0] - was[0], dy = now[1] - was[1], dz = now[2] - was[2];
    return dx * dx + dy * dy + dz * dz >= kActMoveM * kActMoveM;  // NaN: not moving
}

bool ReapplyDue(bool first, bool changed, bool moving, std::uint64_t age_ms) noexcept {
    if (first || changed) return true;
    return age_ms >= (moving ? kActReapplyMovingMs : kActReapplyMs);
}

bool FormatPlayerReply(unsigned phase_limit, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const int n = std::snprintf(out, capacity, "return {p=1,pl=%u}", phase_limit ? 1u : 0u);
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}

void PrefillEntry(unsigned char (&entry)[0x18], const float (&pos)[3], float yaw) noexcept {
    std::memset(entry, 0, sizeof(entry));
    std::memcpy(entry, pos, sizeof(pos));
    std::memcpy(entry + 0xc, &yaw, sizeof(yaw));
    entry[0x14] = 1;
}

bool PlanGate(unsigned dest_mode, std::uint16_t r19c, std::uint16_t r19e, bool applied_now) noexcept {
    return applied_now && dest_mode == 1 && r19c == 0xffff && (r19e & 3u) == 2u;
}

void BuildShadow(Shadow& shadow) noexcept {
    std::memset(&shadow, 0, sizeof(shadow));
    const std::uintptr_t s = reinterpret_cast<std::uintptr_t>(shadow.s), slots = reinterpret_cast<std::uintptr_t>(shadow.slots),
                         owner = reinterpret_cast<std::uintptr_t>(shadow.owner);
    std::memcpy(shadow.p + 0x70, &s, sizeof(s));
    std::memcpy(shadow.s + 0x18, &slots, sizeof(slots));
    std::memcpy(shadow.slots + 0x18, &owner, sizeof(owner));
    const std::uint32_t online = 4;  // S+0x2a4 bit 2: a FOB session
    std::memcpy(shadow.s + 0x2a4, &online, sizeof(online));
    shadow.s[0x2a8] = 0;  // this peer
    std::memset(shadow.owner, 1, sizeof(shadow.owner));  // every guard owned by the other peer: a remote copy
}

bool ShadowGatePasses(const void* p, unsigned idx) noexcept {
    if (!p || idx >= kMaxSoldiers) return false;
    std::uintptr_t s = 0, slots = 0, owner = 0;
    std::memcpy(&s, static_cast<const unsigned char*>(p) + 0x70, sizeof(s));
    if (!s) return false;
    const auto* sb = reinterpret_cast<const unsigned char*>(s);
    std::uint32_t mode = 0;
    std::memcpy(&mode, sb + 0x2a4, sizeof(mode));
    if (((mode >> 2) & 1u) == 0) return false;
    std::memcpy(&slots, sb + 0x18, sizeof(slots));
    if (!slots) return false;
    std::memcpy(&owner, reinterpret_cast<const unsigned char*>(slots) + 0x18, sizeof(owner));
    if (!owner) return false;
    return reinterpret_cast<const unsigned char*>(owner)[idx] != sb[0x2a8];
}

unsigned GaitCatchUp(float ahead_m, unsigned gait) noexcept {
    if (!(ahead_m > 2.0f)) return gait;  // NaN, behind, under 2 m: unchanged (0x1420e00e0 = 4.0, squared)
    const unsigned want = ahead_m > 5.0f ? 3u : (gait < 3u ? gait + 1u : gait);  // 0x1421a7fc0 = 25.0, squared
    return want > gait ? want : gait;
}

float AheadM(const float (&self)[3], const float (&owner)[3], const float (&dest)[3]) noexcept {
    const auto flat = [](const float (&a)[3], const float (&b)[3]) noexcept {
        const float dx = a[0] - b[0], dz = a[2] - b[2];
        return std::sqrt(dx * dx + dz * dz);
    };
    const float d = flat(self, owner);
    if (!(flat(owner, dest) < flat(self, dest))) return 0.0f;  // NaN or not nearer the destination: not ahead
    return d;
}

float EntryYaw(const unsigned char (&wire)[0x70], float fallback) noexcept {
    if (wire[0x68] != 1) return fallback;
    const auto v = static_cast<std::int16_t>(static_cast<std::uint16_t>((wire[0x50] << 8) | wire[0x51]));
    return static_cast<float>(v) * (3.14159265358979f / 32767.0f);  // the unpack's 0x142338970
}

bool TakeNear(const unsigned char (&host_request)[0x70], const float (&self)[3], const float (&owner)[3], bool have) noexcept {
    if (host_request[0x63] != 1) return true;  // not a walk: as before
    if (!have) return false;
    const float dx = self[0] - owner[0], dz = self[2] - owner[2];
    return dx * dx + dz * dz <= kActTakeNearM * kActTakeNearM;  // NaN: not taken
}

bool Warp15(const float (&self)[3], const float (&entry)[3]) noexcept {
    const float dx = self[0] - entry[0], dy = self[1] - entry[1], dz = self[2] - entry[2];
    return dx * dx + dy * dy + dz * dz > 225.0f;  // 0x142329c00
}

SlotStep PlanSlot(std::uint64_t slot_hash, std::uint16_t slot_handle) noexcept {
    const std::uint64_t h = slot_hash & 0xffffffffffffull;  // vt[0] compares the low 48 bits
    if (h == 0 || h == kMoveHash) return SlotStep::plan;
    return slot_handle != 0xffff ? SlotStep::cancel_then_plan : SlotStep::busy;
}

bool ReleaseCancel(std::uint64_t slot_hash, std::uint16_t slot_handle, std::uint16_t r19c) noexcept {
    return (slot_hash & 0xffffffffffffull) == kMoveHash && slot_handle != 0xffff && slot_handle == r19c;
}

bool PlanEndNear(const float (&end)[3], const float (&dest)[3]) noexcept {
    const float dx = end[0] - dest[0], dy = end[1] - dest[1], dz = end[2] - dest[2];
    return dx * dx + dy * dy + dz * dz <= kPlanEndM * kPlanEndM;
}

bool QuietBias(const float (&slot)[3], const float (&target)[3], float (&biased)[3]) noexcept {
    const float dx = target[0] - slot[0], dy = target[1] - slot[1], dz = target[2] - slot[2];
    if (dx * dx + dy * dy + dz * dz > kQuietWarpSq) return false;
    biased[0] = target[0] + kQuietBiasM;
    biased[1] = target[1];
    biased[2] = target[2];
    return true;
}

bool FormatQuietLine(std::uint32_t go, std::uint16_t seq, unsigned moved, unsigned bias, float err_m, float dy_m, char* out,
                     std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const int n = std::snprintf(out, capacity, "world_place=quiet; go=%u; seq=%u; moved=%u; bias=%u; err_m=%.2f; dy_m=%.2f\r\n",
                                unsigned(go), unsigned(seq), moved, bias, err_m, dy_m);
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}

QuietWhy QuietChoose(const RestView& r, const float (&target)[3], std::uint64_t now_ms) noexcept {
    if (!r.at) return QuietWhy::unknown;
    if (now_ms > r.at && now_ms - r.at > kRestStaleMs) return QuietWhy::stale;
    if (!r.realized) return QuietWhy::unrealized;
    if (!r.listed) return QuietWhy::unlisted;
    if (!(r.speed_mps >= 0.0f) || r.speed_mps > kRestSpeedMps) return QuietWhy::moving;
    if (r.has) {
        const float ex = r.end[0] - r.pos[0], ez = r.end[2] - r.pos[2], tx = r.end[0] - target[0], tz = r.end[2] - target[2];
        const float lim = kRestPathM * kRestPathM;
        if (!(ex * ex + ez * ez <= lim) && !(tx * tx + tz * tz <= lim)) return QuietWhy::path;
    }
    return QuietWhy::ok;
}

const char* QuietWhyName(QuietWhy why) noexcept {
    switch (why) {
    case QuietWhy::ok: return "ok";
    case QuietWhy::unknown: return "unknown";
    case QuietWhy::stale: return "stale";
    case QuietWhy::unrealized: return "unrealized";
    case QuietWhy::unlisted: return "unlisted";
    case QuietWhy::moving: return "moving";
    case QuietWhy::path: return "path";
    }
    return "?";
}

bool ListHas(const std::uint16_t* list, std::uint32_t count, unsigned idx) noexcept {
    if (!list) return false;
    for (std::uint32_t k = 0; k < count; ++k)
        if (list[k] == idx) return true;
    return false;
}

#ifdef _WIN32
int ListFind(const std::uint16_t* list, std::uint32_t count, unsigned idx) noexcept {
    __try {
        return ListHas(list, count, idx) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

namespace {
namespace c = mgscoop::soldier_act_contract;
using Update = void(__fastcall*)(void* m);
using PackRequest = void(__fastcall*)(void* fob, void* out70, void* r, unsigned idx);
using PackOrder = void(__fastcall*)(void* fob, void* out18, void* order);
using UnpackRequest = void(__fastcall*)(void* fob, void* r, const void* in70, unsigned idx);
using UnpackOrder = void(__fastcall*)(void* fob, void* order, const void* in18);
using Post = void(__fastcall*)(void* m, unsigned idx, const void* req);
// Round 8 track C: the walk-request start (P, idx, waypoints, R, *(S+0x18), F, planner) -> 1 planned; the planner's
// follower getter vt[0x20] (planner, idx) -> {obj, u32}; the follower's vt[0x90] has a path / vt[0xa0] the path (obj, u32).
using PlanStart = char(__fastcall*)(void* p, unsigned idx, unsigned waypoints, void* r, void* slots, void* follower, void* planner);
using FollowerOf = void*(__fastcall*)(void* planner, unsigned idx);
using FollowerHas = char(__fastcall*)(void* obj, std::uint32_t key);
using FollowerPathOf = void*(__fastcall*)(void* obj, std::uint32_t key);
using PlannerCancel = void(__fastcall*)(void* planner, unsigned idx, std::uint16_t handle);
using NetWarp = std::uint8_t(__fastcall*)(void* fob, unsigned idx, const float* entry);  // 0x136ece0 (AL: 1 beyond 15 m)

std::uintptr_t g_base = 0;
std::size_t g_span = 0;
LogFn g_log = nullptr;
Update g_original = nullptr;
std::atomic<bool> g_installed{false}, g_census{false};
std::atomic<std::uintptr_t> g_m{0};
std::atomic<std::uint32_t> g_calls{0}, g_lines{0}, g_faults{0}, g_packFaults{0}, g_tid{0}, g_threads{0};
// The local player (runtime thread -> hook thread), a seqlock-free pair of atomics is enough for a census distance.
std::atomic<float> g_px{0.0f}, g_py{0.0f}, g_pz{0.0f};
std::atomic<std::uint64_t> g_pAt{0};

// Census state (hook thread only).
std::uint64_t g_censusAt = 0, g_binsAt = 0, g_bandsAt = 0;
struct Track {
    float pos[3];
    std::uint64_t at;
    std::uint64_t payloadAt;
    std::uint8_t kinds[2];
    bool valid;
};
Track g_track[kMaxSoldiers]{};

// FM1 mirror state. tx/mirror registrations: the runtime thread writes, the hook thread reads (atomics); the staged packs
// and the mailboxes under g_actLock.
std::atomic<bool> g_actOn{false}, g_actHost{false};
std::atomic<std::uint64_t> g_txAt[kMaxSoldiers]{}, g_mirAt[kMaxSoldiers]{};
std::atomic<std::uint16_t> g_txSlot[kMaxSoldiers]{}, g_mirSlot[kMaxSoldiers]{};
SRWLOCK g_actLock = SRWLOCK_INIT;
struct Stage {
    std::uint16_t slot, req_seq, order_seq;
    bool req_dirty, order_dirty;
    std::uint64_t req_at, order_at;  // hook: last staged
    unsigned char req[0x70], order[world::kActOrder];  // order: the packed 0x18, the owner position, flags
};
Stage g_stage[kMaxSoldiers]{};
struct Mail {
    bool req_valid, order_valid;
    std::uint16_t req_seq, order_seq, applied_seq;
    std::uint64_t req_rx_ms;
    unsigned char req[0x70], order[world::kActOrder];
};
Mail g_mail[kMaxSoldiers]{};
// The hook's last applied request per guard (hook thread only); a release bumps the guard's generation (any thread), so a
// guard mirrored again takes his first request whatever it is.
struct Applied {
    bool valid, have_owner;
    std::uint32_t gen;
    std::uint64_t at;
    unsigned char req[0x70];
    float owner[3];  // the owner copy's position at this apply (round 6: moving = OwnerMoved since)
};
Applied g_applied[kMaxSoldiers]{};
std::atomic<std::uint32_t> g_mirGen[kMaxSoldiers]{};
std::atomic<std::uint32_t> g_cSame{0}, g_cUnrealized{0};
// Round 6: request applies the rule asked for (first, changed, moving 1 s, still 5 s) and the moving owner's 1 s re-applies.
std::atomic<std::uint32_t> g_cDue{0}, g_cReapply{0}, g_cOrderClear{0};
// Round 6 (runs fbf3522a / f11fe8a1): each realized non-puppet guard's own destination (his packed request's +0x40) on the
// puppet side, every kCensusPeriodMs, for the take's leg check (LegMatch). The hook writes, the runtime thread reads.
std::atomic<float> g_ownDest[kMaxSoldiers][3]{};
std::atomic<std::uint64_t> g_ownAt[kMaxSoldiers]{};
std::uint64_t g_ownScanAt = 0;
std::atomic<std::uint32_t> g_cLegWait{0};
std::atomic<std::uint32_t> g_cExpired{0};  // fire-drop mask bits that lapsed (no ACT_V1 m within kActFreshMs)
// Round 8 track C (act_plan, design 7.23): the puppets plan their own path (PlanStep after the original update).
std::atomic<bool> g_planOn{false};
std::atomic<std::uint32_t> g_cPlan{0}, g_cPlanSkip{0}, g_cWarp15{0}, g_cPlanOk{0}, g_cPlanFar{0}, g_cPlanNoPath{0}, g_cNearWait{0},
    g_cGaitUp{0}, g_cPlanFault{0}, g_cPlanCancel{0}, g_cPlanBusy{0}, g_cPlanNone{0}, g_cPlanRelease{0};
bool g_planned[kMaxSoldiers]{};  // hook thread: a plan of ours may hold his planner slot (cancelled at his release)
std::atomic<float> g_ownPos[kMaxSoldiers][3]{};  // each realized non-puppet guard's own position (OwnDests), for TakeNear
Shadow g_shadow{};
bool g_shadowBuilt = false;
bool g_appliedNow[kMaxSoldiers]{};  // hook thread: requests unpacked in this update, before the original
struct PlanCheck {
    bool pending;
    std::uint16_t handle;
    std::uint64_t at;
    float dest[3], from[3];
};
PlanCheck g_planCheck[kMaxSoldiers]{};
bool g_plannerLogged = false;
std::atomic<std::uint32_t> g_cTx{0}, g_cStaged{0}, g_cRx{0}, g_cApplied{0}, g_cOrders{0}, g_cTable{0}, g_cFault{0}, g_cStale{0},
    g_cRemap{0}, g_cPayloadIds{0}, g_frameP95{0}, g_frames{0};
std::uint64_t g_sendWindowAt = 0;
unsigned g_sendWindowParts = 0;
// Frame interval census (hook thread): the manager update's interval, p95 over each second.
std::uint64_t g_frameLast = 0, g_frameAt = 0;
std::uint32_t g_frameUs[256]{};
unsigned g_frameN = 0;
LARGE_INTEGER g_qpf{};

// U2 probe state (the runtime thread starts it; the hook thread runs it).
struct Probe {
    std::atomic<bool> pending{false};
    bool active{};
    std::uint16_t dstWant{}, srcWant{};
    std::uint32_t ms{};
    unsigned modeWant{};
    unsigned dst{}, src{}, mode{};  // mode 0 native, 1 raw
    std::uint64_t until{}, logAt{}, applyAt{};
    bool setBit{};
    float start[3]{};
    unsigned applied{}, orders{}, faults{}, refused{};
    unsigned char last[0x70]{};
};
Probe g_probe{};
std::atomic<std::uint32_t> g_probeRuns{0};

// Enemy round 8 (quiet_place): staged quiet moves (the runtime thread writes under g_quietLock, the hook runs them).
struct QuietMove { bool due; std::uint32_t go; std::uint16_t seq; float pos[3]; float yaw; };
QuietMove g_quiet[kMaxSoldiers]{};
SRWLOCK g_quietLock = SRWLOCK_INIT;
std::atomic<bool> g_quietPending{false};
// Enemy round 9 (quiet_place, design 7.24): the rest views (hook thread writes g_restFresh, publishes under g_restLock).
std::atomic<bool> g_restOn{false};
SRWLOCK g_restLock = SRWLOCK_INIT;
RestView g_rest[kMaxSoldiers]{}, g_restFresh[kMaxSoldiers]{};
struct RestHist { std::uint64_t at[2]; float pos[2][3]; };
RestHist g_restHist[kMaxSoldiers]{};
std::uint64_t g_restAt = 0;  // hook thread

// The runtime thread stamps every registration and mailbox with partner_net::NowMs (ACT_V1, world_link::ActLatest); the
// hook compares with the same clock (run 0a271c10: GetTickCount64 here left every registration stale: 25 requests staged
// in five minutes, 0 applied).
std::uint64_t NowMs() noexcept { return partner_net::NowMs(); }

void Emit(const char* line) noexcept {
    if (g_log) g_log(line);
    g_lines.fetch_add(1, std::memory_order_relaxed);
}

template <class T> bool Rd(std::uintptr_t at, T& out) noexcept {
    if (at < 0x10000) return false;
    out = *reinterpret_cast<const T*>(at);
    return true;
}

// The objects the census and the probe use, read from M (all checked; SEH around every caller).
struct View {
    std::uintptr_t m, fob, s, records, slots, recs, weapons, netpos, flags;
    std::uint32_t count, netCount, flagCount, recCount;
    std::uintptr_t exists, realized, active, owner, pos, netData, flagData, recData;
    std::uint32_t mode;
    std::uint8_t peer;
};
bool Resolve(std::uintptr_t m, View& v) noexcept {
    v = {};
    v.m = m;
    std::uintptr_t vt = 0, codec = 0, codecVt = 0;
    if (!Rd(m, vt) || vt != g_base + c::manager_vtable_rva) return false;
    if (!Rd(m + c::m_soldier, v.s) || !Rd(m + c::m_records, v.records) || !Rd(m + c::m_codec, codec) || !v.s || !v.records)
        return false;
    if (!Rd(codec, codecVt) || codecVt != g_base + c::codec_vtable_rva) return false;
    v.fob = m - c::fob_this;
    if (!Rd(v.s + c::s_slots, v.slots) || !v.slots || !Rd(v.slots + c::slots_count, v.count)) return false;
    if (v.count > kMaxSoldiers) v.count = kMaxSoldiers;
    Rd(v.slots + c::slots_exists, v.exists);
    Rd(v.slots + c::slots_realized, v.realized);
    Rd(v.slots + c::slots_active, v.active);
    Rd(v.slots + c::slots_owner, v.owner);
    Rd(v.slots + c::slots_pos, v.pos);
    Rd(v.s + c::s_records, v.recs);
    if (v.recs) { Rd(v.recs + c::rec_data, v.recData); Rd(v.recs + c::rec_count, v.recCount); }
    Rd(v.s + c::s_weapons, v.weapons);
    Rd(v.s + c::s_netpos, v.netpos);
    if (v.netpos) { Rd(v.netpos + c::net_data, v.netData); Rd(v.netpos + c::net_count, v.netCount); }
    Rd(v.s + c::s_flags, v.flags);
    if (v.flags) { Rd(v.flags + c::flag_data, v.flagData); Rd(v.flags + c::flag_count, v.flagCount); }
    Rd(v.s + c::s_mode, v.mode);
    Rd(v.s + c::s_peer, v.peer);
    return v.pos != 0;
}
bool Bit(std::uintptr_t bits, unsigned i) noexcept {
    std::uint8_t b = 0;
    return bits && Rd(bits + (i >> 3), b) && ((b >> (i & 7)) & 1u);
}
std::uintptr_t Record(const View& v, unsigned i) noexcept { return v.records + static_cast<std::uintptr_t>(i) * c::record_stride; }
bool Pos(const View& v, unsigned i, float (&p)[3]) noexcept {
    const auto* f = reinterpret_cast<const float*>(v.pos + static_cast<std::uintptr_t>(i) * c::slots_pos_stride);
    p[0] = f[0]; p[1] = f[1]; p[2] = f[2];
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}
std::uintptr_t FlagDword(const View& v, unsigned i) noexcept {
    if (!v.flagData || i >= v.flagCount) return 0;
    return v.flagData + static_cast<std::uintptr_t>(i) * c::flag_stride + c::flag_dword;
}
bool Puppet(const View& v, unsigned i) noexcept {
    const std::uintptr_t d = FlagDword(v, i);
    std::uint32_t word = 0;
    return d && Rd(d, word) && ((word >> c::flag_puppet_bit) & 1u);
}
bool Covered(const View& v, unsigned i) noexcept { return v.netpos && v.netData && i < v.netCount; }
float Dist(const float (&a)[3], const float (&b)[3]) noexcept {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
bool Player(float (&p)[3], std::uint64_t now) noexcept {
    const std::uint64_t at = g_pAt.load(std::memory_order_acquire);
    p[0] = g_px.load(std::memory_order_relaxed); p[1] = g_py.load(std::memory_order_relaxed); p[2] = g_pz.load(std::memory_order_relaxed);
    return at && now - at < 3000;
}

// Round 8 track C: a code pointer read from a vtable is called only when it lies in the image.
bool InImage(std::uintptr_t fn) noexcept { return fn >= g_base && fn < g_base + g_span; }
struct Follower {
    std::uintptr_t planner, pair, obj;
    std::uint32_t key;
};
// The planner *(S+0x30) and its follower pair for guard i (vt[0x20], as 0x14136f0f2 / 0x141372bae call it).
bool FollowerFor(const View& v, unsigned i, Follower& f) noexcept {
    f = {};
    std::uintptr_t vt = 0, fn = 0;
    if (!Rd(v.s + c::s_planner, f.planner) || !f.planner || !Rd(f.planner, vt) || !vt || !Rd(vt + c::planner_follower, fn) || !InImage(fn))
        return false;
    f.pair = reinterpret_cast<std::uintptr_t>(reinterpret_cast<FollowerOf>(fn)(reinterpret_cast<void*>(f.planner), i));
    if (!f.pair || !Rd(f.pair, f.obj) || !f.obj) return false;
    Rd(f.pair + 8, f.key);
    return true;
}
// The follower's path: has (vt[0x90]), the point count (+0x20) and the last point (+8 stride 0x10), as the path step reads.
bool FollowerPath(const Follower& f, bool& has, std::uint32_t& count, float (&end)[3]) noexcept {
    has = false;
    count = 0;
    end[0] = end[1] = end[2] = 0.0f;
    std::uintptr_t vt = 0, fnHas = 0, fnPath = 0;
    if (!Rd(f.obj, vt) || !vt || !Rd(vt + c::follower_has, fnHas) || !Rd(vt + c::follower_path, fnPath) || !InImage(fnHas) || !InImage(fnPath))
        return false;
    has = reinterpret_cast<FollowerHas>(fnHas)(reinterpret_cast<void*>(f.obj), f.key) != 0;
    if (!has) return true;
    const auto path = reinterpret_cast<std::uintptr_t>(reinterpret_cast<FollowerPathOf>(fnPath)(reinterpret_cast<void*>(f.obj), f.key));
    std::uintptr_t points = 0;
    if (!path || !Rd(path + c::path_points, points) || !Rd(path + c::path_count, count)) return true;
    if (points && count && count < 4096) {
        const auto* e = reinterpret_cast<const float*>(points + static_cast<std::uintptr_t>(count - 1) * 0x10);
        end[0] = e[0]; end[1] = e[1]; end[2] = e[2];
    }
    return true;
}
// The planner's per-guard slot (session 1 decompile of vt[0] 0x140fa2430): {u48 hash, u16 handle +8, u16 done +0xc}.
struct Slot {
    std::uint64_t hash;
    std::uint16_t handle, done;
};
bool PlannerSlot(std::uintptr_t planner, unsigned i, Slot& s) noexcept {
    s = {0, 0xffff, 0xffff};
    std::uintptr_t slots = 0;
    if (!planner || !Rd(planner + c::planner_slots, slots) || !slots) return false;
    const std::uintptr_t e = slots + static_cast<std::uintptr_t>(i) * c::planner_slot_stride;
    Rd(e, s.hash);
    Rd(e + c::slot_handle, s.handle);
    Rd(e + c::slot_done, s.done);
    return true;
}
// The planner is the class the contract pins (vtable 0x23a1610): its cancel is the engine's own vt[0x10].
bool PlannerKnown(std::uintptr_t planner) noexcept {
    std::uintptr_t vt = 0;
    return planner && Rd(planner, vt) && vt == g_base + c::planner_vtable_rva;
}
void CancelSlot(std::uintptr_t planner, unsigned i, std::uint16_t handle) noexcept {
    std::uintptr_t vt = 0, fn = 0;
    if (!Rd(planner, vt) || !Rd(vt + c::planner_cancel, fn) || !InImage(fn)) return;
    reinterpret_cast<PlannerCancel>(fn)(reinterpret_cast<void*>(planner), i, handle);
}

// One line with the planner's and the follower's vtables (RVAs) for the read-only decompile (session 1).
void PlannerLine(const View& v, unsigned i) noexcept {
    if (g_plannerLogged) return;
    Follower f{};
    if (!FollowerFor(v, i, f)) return;
    g_plannerLogged = true;
    std::uintptr_t pvt = 0, fvt = 0, p0 = 0, p10 = 0, p20 = 0, p38 = 0, f90 = 0, f98 = 0, fa0 = 0, fa8 = 0, fd0 = 0;
    Rd(f.planner, pvt);
    Rd(f.obj, fvt);
    if (pvt) { Rd(pvt, p0); Rd(pvt + 0x10, p10); Rd(pvt + 0x20, p20); Rd(pvt + 0x38, p38); }
    if (fvt) { Rd(fvt + 0x90, f90); Rd(fvt + 0x98, f98); Rd(fvt + 0xa0, fa0); Rd(fvt + 0xa8, fa8); Rd(fvt + 0xd0, fd0); }
    const auto rva = [](std::uintptr_t a) noexcept { return static_cast<unsigned long long>(a >= g_base && a < g_base + g_span ? a - g_base : 0); };
    char line[640]{};
    const int n = std::snprintf(line, sizeof(line),
        "soldier_act_planner; idx=%u; planner_vt=0x%llx; p00=0x%llx; p10=0x%llx; p20=0x%llx; p38=0x%llx; follower_vt=0x%llx; key=%u; "
        "f90=0x%llx; f98=0x%llx; fa0=0x%llx; fa8=0x%llx; fd0=0x%llx; pair_in_planner=%lld; obj_is_planner=%u\r\n",
        i, rva(pvt), rva(p0), rva(p10), rva(p20), rva(p38), rva(fvt), f.key, rva(f90), rva(f98), rva(fa0), rva(fa8), rva(fd0),
        static_cast<long long>(f.pair - f.planner), f.obj == f.planner ? 1u : 0u);
    if (n > 0) Emit(line);
}

int PackSafe(const View& v, unsigned i, unsigned char (&w)[0x70], unsigned char (&o)[0x18]) noexcept {
    __try {
        std::memset(w, 0, sizeof(w));
        std::memset(o, 0, sizeof(o));
        const std::uintptr_t r = Record(v, i);
        reinterpret_cast<PackRequest>(g_base + c::pack_request_rva)(reinterpret_cast<void*>(v.fob), w, reinterpret_cast<void*>(r), i);
        reinterpret_cast<PackOrder>(g_base + c::pack_order_rva)(reinterpret_cast<void*>(v.fob), o, reinterpret_cast<void*>(r + c::r_order));
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_packFaults.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
}

void CensusLine(const View& v, unsigned i, const float (&player)[3], std::uint64_t now) noexcept {
    float p[3]{};
    if (!Pos(v, i, p)) return;
    const float d = Dist(p, player);
    if (!(d <= kCensusRangeM)) return;
    Track& t = g_track[i];
    float speed = -1.0f;
    if (t.valid && now > t.at) speed = Dist(p, t.pos) * 1000.0f / static_cast<float>(now - t.at);
    const std::uintptr_t r = Record(v, i);
    std::uint16_t r19e = 0, r1a0 = 0, r1d2 = 0;
    Rd(r + c::r_flags, r19e); Rd(r + c::r_1a0, r1a0); Rd(r + c::r_order_flags, r1d2);
    std::uint8_t own = 0xff, stance = 0xff;
    if (v.owner) Rd(v.owner + i, own);
    float aim[3]{};
    if (v.recData && i < v.recCount) {
        const std::uintptr_t rec = v.recData + static_cast<std::uintptr_t>(i) * c::rec_stride;
        std::uint32_t st = 0;
        if (Rd(rec + c::rec_stance, st)) stance = static_cast<std::uint8_t>(st & 0xfu);
        const auto* a = reinterpret_cast<const float*>(rec + c::rec_aim);
        aim[0] = a[0]; aim[1] = a[1]; aim[2] = a[2];
    }
    unsigned char ammo[8]{};
    std::uint16_t mag = 0xffff;
    std::uintptr_t wa = 0, wm = 0;
    if (v.weapons && Rd(v.weapons + c::wep_ammo, wa) && wa) std::memcpy(ammo, reinterpret_cast<const void*>(wa + static_cast<std::uintptr_t>(i) * 8), 8);
    if (v.weapons && Rd(v.weapons + c::wep_mag, wm) && wm) Rd(wm + (static_cast<std::uintptr_t>(i) * 3 + 2) * 2, mag);
    unsigned char w[0x70]{}, o[0x18]{};
    const int packed = PackSafe(v, i, w, o);
    const Fixed f = DecodeFixed(w);
    const bool covered = Covered(v, i);
    float np[3]{};
    std::uint8_t npf = 0;
    if (covered) {
        const std::uintptr_t e = v.netData + static_cast<std::uintptr_t>(i) * c::net_stride;
        const auto* ef = reinterpret_cast<const float*>(e);
        np[0] = ef[0]; np[1] = ef[1]; np[2] = ef[2];
        Rd(e + c::net_flags, npf);
    }
    char order[0x20 * 2 + 1]{}, ord18[0x18 * 2 + 1]{}, w61[14 * 2 + 1]{}, ammoHex[17]{};
    Hex(reinterpret_cast<const unsigned char*>(r + c::r_order), 0x20, order, sizeof(order));
    Hex(o, sizeof(o), ord18, sizeof(ord18));
    Hex(w + 0x60, 14, w61, sizeof(w61));
    Hex(ammo, sizeof(ammo), ammoHex, sizeof(ammoHex));
    // Round 8 track C: the plan handle R+0x19c, R+0x198, the follower's path (vt[0x90] has, its count and last point; -1:
    // no follower) and R+0x80 (the request's destination), so a path end can be compared with the host copy's destination.
    std::uint16_t r19c = 0;
    std::uint32_t r198 = 0, fn = 0;
    Rd(r + c::r_handle, r19c);
    Rd(r + c::r_198, r198);
    float fend[3]{}, r80[3]{};
    std::memcpy(r80, reinterpret_cast<const void*>(r + c::r_dest), sizeof(r80));
    int fhas = -1;
    Follower fol{};
    Slot sl{0, 0xffff, 0xffff};
    if (FollowerFor(v, i, fol)) {
        PlannerLine(v, i);
        bool has = false;
        if (FollowerPath(fol, has, fn, fend)) fhas = has ? 1 : 0;
        PlannerSlot(fol.planner, i, sl);
    }
    char line[1400]{};
    const int n = std::snprintf(line, sizeof(line),
        "soldier_act_census; t=%llu; idx=%u; d=%.1f; p=%.2f,%.2f,%.2f; v=%.2f; ex=%u; rz=%u; ac=%u; own=%u; peer=%u; mode=0x%x; "
        "puppet=%u; st=%u; np=%u; npf=0x%02x; npp=%.2f,%.2f,%.2f; r19e=0x%04x; r1a0=0x%04x; r1d2=0x%04x; packed=%d; ka=%u; "
        "kb=%u; dm=%u; gait=%u; post=%u; fm=%u; b66=%u; path=%u; wep=0x%02x; dest=%.2f,%.2f,%.2f; w60=%s; o18=%s; o=%s; "
        "aim=%.2f,%.2f,%.2f; ammo=%s; mag=%u; r19c=0x%04x; r198=0x%x; fh=%d; fn=%u; fend=%.2f,%.2f,%.2f; r80=%.2f,%.2f,%.2f; "
        "psh=0x%012llx; psl=0x%04x; psd=0x%04x\r\n",
        static_cast<unsigned long long>(now), i, d, p[0], p[1], p[2], speed, Bit(v.exists, i) ? 1u : 0u, Bit(v.realized, i) ? 1u : 0u,
        Bit(v.active, i) ? 1u : 0u, own, v.peer, v.mode, Puppet(v, i) ? 1u : 0u, stance, covered ? 1u : 0u, npf, np[0], np[1], np[2],
        r19e, r1a0, r1d2, packed, f.kind_a, f.kind_b, f.dest_mode, f.gait, f.posture, f.face_mode, f.b66, f.path, f.weapon, f.dest[0],
        f.dest[1], f.dest[2], w61, ord18, order, aim[0], aim[1], aim[2], ammoHex, mag, r19c, r198, fhas, fn, fend[0], fend[1], fend[2],
        r80[0], r80[1], r80[2], static_cast<unsigned long long>(sl.hash & 0xffffffffffffull), sl.handle, sl.done);
    if (n > 0) Emit(line);
    // The payloads (U3) every 2 s per guard and on a kind change.
    if (packed && (!t.valid || now - t.payloadAt >= kPayloadPeriodMs || t.kinds[0] != f.kind_a || t.kinds[1] != f.kind_b)) {
        char wire[0x70 * 2 + 1]{}, raw[0x40 * 2 + 1]{};
        Hex(w, sizeof(w), wire, sizeof(wire));
        Hex(reinterpret_cast<const unsigned char*>(r + 0x80), 0x40, raw, sizeof(raw));
        const int k = std::snprintf(line, sizeof(line), "soldier_act_payload; t=%llu; idx=%u; ka=%u; kb=%u; w=%s; r80=%s\r\n",
                                    static_cast<unsigned long long>(now), i, f.kind_a, f.kind_b, wire, raw);
        if (k > 0) Emit(line);
        t.payloadAt = now;
        t.kinds[0] = f.kind_a;
        t.kinds[1] = f.kind_b;
    }
    t.pos[0] = p[0]; t.pos[1] = p[1]; t.pos[2] = p[2];
    t.at = now;
    t.valid = true;
}

void Census(const View& v, std::uint64_t now) noexcept {
    float player[3]{};
    const bool havePlayer = Player(player, now);
    if (now - g_censusAt >= kCensusPeriodMs) {
        g_censusAt = now;
        if (havePlayer)
            for (unsigned i = 0; i < v.count; ++i)
                if (Bit(v.realized, i)) CensusLine(v, i, player, now);
    }
    if (now - g_binsAt >= kBinsPeriodMs) {
        g_binsAt = now;
        static std::uint16_t hits[soldier_authority::kSoldiers];
        const std::uint32_t total = soldier_authority::TakeShooterHits(hits);
        if (total) {
            char line[900]{};
            int n = std::snprintf(line, sizeof(line), "soldier_engage_bins; t=%llu; hits=%u; by=", static_cast<unsigned long long>(now), total);
            for (unsigned i = 0; i < soldier_authority::kSoldiers && n > 0 && n < static_cast<int>(sizeof(line)) - 40; ++i) {
                if (!hits[i]) continue;
                float p[3]{};
                const float d = havePlayer && i < v.count && Pos(v, i, p) ? Dist(p, player) : -1.0f;
                n += std::snprintf(line + n, sizeof(line) - static_cast<std::size_t>(n), "%u:%u:%.1f,", i, hits[i], d);
            }
            if (n > 0 && n < static_cast<int>(sizeof(line)) - 3) { line[n] = '\r'; line[n + 1] = '\n'; line[n + 2] = 0; Emit(line); }
        }
    }
    if (now - g_bandsAt >= kBandsPeriodMs) {
        g_bandsAt = now;
        unsigned band[kBands]{}, exists = 0, realized = 0, covered = 0, puppets = 0;
        float farthest = 0.0f;
        for (unsigned i = 0; i < v.count; ++i) {
            if (Bit(v.exists, i)) ++exists;
            if (!Bit(v.realized, i)) continue;
            ++realized;
            if (Covered(v, i)) ++covered;
            if (Puppet(v, i)) ++puppets;
            float p[3]{};
            if (!havePlayer || !Pos(v, i, p)) continue;
            const float d = Dist(p, player);
            ++band[Band(d)];
            if (d > farthest) farthest = d;
        }
        char line[400]{};
        const int n = std::snprintf(line, sizeof(line),
            "soldier_act_bands; t=%llu; count=%u; exists=%u; realized=%u; far_m=%.1f; b=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u; player=%u; "
            "net=%u; net_count=%u; covered=%u; mode=0x%x; peer=%u; puppets=%u; threads=%u; pack_faults=%u\r\n",
            static_cast<unsigned long long>(now), v.count, exists, realized, farthest, band[0], band[1], band[2], band[3], band[4], band[5],
            band[6], band[7], band[8], band[9], band[10], havePlayer ? 1u : 0u, v.netpos ? 1u : 0u, v.netCount, covered, v.mode, v.peer,
            puppets, g_threads.load(), g_packFaults.load());
        if (n > 0) Emit(line);
    }
}

// U2 probe (hook thread, before the manager update).
void ProbeStep(const View& v, std::uint64_t now) noexcept {
    Probe& pr = g_probe;
    char line[600]{};
    if (pr.pending.load(std::memory_order_acquire) && !pr.active) {
        pr.pending.store(false, std::memory_order_release);
        float player[3]{};
        if (!Player(player, now)) { Emit("soldier_act_probe=refused; why=player\r\n"); return; }
        unsigned dst = pr.dstWant, src = pr.srcWant;
        float best = 60.0f;
        if (dst == 0xffff) {
            dst = 0xffff;
            for (unsigned i = 0; i < v.count; ++i) {
                float p[3]{};
                if (!Bit(v.realized, i) || !Pos(v, i, p)) continue;
                const float d = Dist(p, player);
                if (d < best) { best = d; dst = i; }
            }
        }
        if (dst >= v.count || !Bit(v.realized, dst)) { Emit("soldier_act_probe=refused; why=dst\r\n"); return; }
        float dp[3]{};
        Pos(v, dst, dp);
        if (src == 0xffff) {
            float bestMoving = 1e9f, bestAny = 1e9f;
            unsigned moving = 0xffff, any = 0xffff;
            for (unsigned i = 0; i < v.count; ++i) {
                float p[3]{};
                if (i == dst || !Bit(v.realized, i) || !Pos(v, i, p)) continue;
                const float d = Dist(p, dp);
                if (d < 8.0f || d > 60.0f) continue;
                std::uint8_t kind = 0;
                Rd(Record(v, i) + c::r_kind_a, kind);
                if (MovingKind(kind) && d < bestMoving) { bestMoving = d; moving = i; }
                if (d < bestAny) { bestAny = d; any = i; }
            }
            src = moving != 0xffff ? moving : any;
        }
        if (src >= v.count || src == dst || !Bit(v.realized, src)) { Emit("soldier_act_probe=refused; why=src\r\n"); return; }
        const bool covered = Covered(v, dst);
        unsigned mode = pr.modeWant == 2 ? (covered ? 0u : 1u) : pr.modeWant;
        if (mode == 0 && !covered) { Emit("soldier_act_probe=refused; why=table\r\n"); return; }
        const std::uintptr_t fd = FlagDword(v, dst);
        if (!fd) { Emit("soldier_act_probe=refused; why=flags\r\n"); return; }
        auto* word = reinterpret_cast<volatile std::uint32_t*>(fd);
        pr.setBit = ((*word >> c::flag_puppet_bit) & 1u) == 0;
        if (pr.setBit) *word = *word | (1u << c::flag_puppet_bit);
        pr.dst = dst; pr.src = src; pr.mode = mode;
        pr.until = now + pr.ms;
        pr.logAt = 0; pr.applyAt = 0;
        pr.applied = pr.orders = pr.faults = pr.refused = 0;
        std::memset(pr.last, 0, sizeof(pr.last));
        std::memcpy(pr.start, dp, sizeof(dp));
        pr.active = true;
        g_probeRuns.fetch_add(1, std::memory_order_relaxed);
        float sp[3]{};
        Pos(v, src, sp);
        std::snprintf(line, sizeof(line),
                      "soldier_act_probe=start; t=%llu; dst=%u; src=%u; mode=%s; covered=%u; set_bit=%u; d_player=%.1f; d_src=%.1f; ms=%u\r\n",
                      static_cast<unsigned long long>(now), dst, src, mode ? "raw" : "native", covered ? 1u : 0u, pr.setBit ? 1u : 0u, best,
                      Dist(dp, sp), pr.ms);
        Emit(line);
    }
    if (!pr.active) return;
    float dp[3]{}, sp[3]{};
    Pos(v, pr.dst, dp);
    Pos(v, pr.src, sp);
    if (now >= pr.until || pr.dst >= v.count || pr.src >= v.count) {
        if (pr.setBit) {
            if (const std::uintptr_t fd = FlagDword(v, pr.dst)) {
                auto* word = reinterpret_cast<volatile std::uint32_t*>(fd);
                *word = *word & ~(1u << c::flag_puppet_bit);
            }
        }
        pr.active = false;
        std::snprintf(line, sizeof(line), "soldier_act_probe=end; t=%llu; dst=%u; src=%u; moved_m=%.2f; d_src=%.1f; applied=%u; orders=%u; faults=%u\r\n",
                      static_cast<unsigned long long>(now), pr.dst, pr.src, Dist(dp, pr.start), Dist(dp, sp), pr.applied, pr.orders, pr.faults);
        Emit(line);
        return;
    }
    const std::uintptr_t rs = Record(v, pr.src), rd = Record(v, pr.dst);
    unsigned char w[0x70]{}, o[0x18]{};
    if (!PackSafe(v, pr.src, w, o)) { ++pr.faults; return; }
    const bool changed = std::memcmp(w, pr.last, sizeof(w)) != 0;
    if (changed || now - pr.applyAt >= 1000) {
        __try {
            if (pr.mode == 0) {
                // Pre-fill dst's network-position entry with its own position (no 15 m warp) and flag bit 0, unpack, restore.
                const std::uintptr_t e = v.netData + static_cast<std::uintptr_t>(pr.dst) * c::net_stride;
                unsigned char saved[0x18];
                std::memcpy(saved, reinterpret_cast<const void*>(e), sizeof(saved));
                auto* ef = reinterpret_cast<float*>(e);
                ef[0] = dp[0]; ef[1] = dp[1]; ef[2] = dp[2];
                *reinterpret_cast<std::uint8_t*>(e + c::net_flags) |= 1u;
                reinterpret_cast<UnpackRequest>(g_base + c::unpack_request_rva)(reinterpret_cast<void*>(v.fob), reinterpret_cast<void*>(rd), w,
                                                                               pr.dst);
                std::memcpy(reinterpret_cast<void*>(e), saved, sizeof(saved));
                *reinterpret_cast<volatile std::uint16_t*>(rd + c::r_flags) |= 0x80u;
            } else {
                alignas(16) unsigned char req[c::r_request_size];
                std::memcpy(req, reinterpret_cast<const void*>(rs), sizeof(req));
                reinterpret_cast<Post>(g_base + c::post_rva)(reinterpret_cast<void*>(v.m), pr.dst, req);
            }
            ++pr.applied;
            pr.applyAt = now;
            std::memcpy(pr.last, w, sizeof(w));
        } __except (EXCEPTION_EXECUTE_HANDLER) { ++pr.faults; g_faults.fetch_add(1, std::memory_order_relaxed); }
    }
    __try {
        reinterpret_cast<UnpackOrder>(g_base + c::unpack_order_rva)(reinterpret_cast<void*>(v.fob), reinterpret_cast<void*>(rd + c::r_order), o);
        *reinterpret_cast<volatile std::uint16_t*>(rd + c::r_order_flags) |= 1u;
        ++pr.orders;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ++pr.faults; g_faults.fetch_add(1, std::memory_order_relaxed); }
    if (now - pr.logAt >= 500) {
        pr.logAt = now;
        const Fixed f = DecodeFixed(w);
        std::uint8_t dk = 0, dg = 0;
        std::uint32_t bits = 0;
        Rd(rd + c::r_kind_a, dk);
        Rd(rd + c::r_bits, bits);
        dg = static_cast<std::uint8_t>((bits >> 1) & 7u);
        std::snprintf(line, sizeof(line),
                      "soldier_act_probe; t=%llu; dst=%u; src=%u; dp=%.2f,%.2f,%.2f; sp=%.2f,%.2f,%.2f; moved_m=%.2f; d_src=%.1f; ska=%u; "
                      "skb=%u; sgait=%u; sdm=%u; dka=%u; dgait=%u; puppet=%u; applied=%u; orders=%u; faults=%u\r\n",
                      static_cast<unsigned long long>(now), pr.dst, pr.src, dp[0], dp[1], dp[2], sp[0], sp[1], sp[2], Dist(dp, pr.start), Dist(dp, sp),
                      f.kind_a, f.kind_b, f.gait, f.dest_mode, dk, dg, Puppet(v, pr.dst) ? 1u : 0u, pr.applied, pr.orders, pr.faults);
        Emit(line);
    }
}

// FM1, the owner (hook thread): pack every registered guard; stage a changed record (rate-capped) for SendStaged.
void MirrorTx(const View& v, std::uint64_t now) noexcept {
    for (unsigned i = 0; i < v.count; ++i) {
        const std::uint64_t at = g_txAt[i].load(std::memory_order_relaxed);
        if (!at || now - at > kActFreshMs || !Bit(v.realized, i)) continue;
        unsigned char w[0x70]{}, o[0x18]{};
        if (!PackSafe(v, i, w, o)) continue;
        float own[3]{};
        const bool havePos = Pos(v, i, own);
        AcquireSRWLockExclusive(&g_actLock);
        Stage& s = g_stage[i];
        s.slot = g_txSlot[i].load(std::memory_order_relaxed);
        unsigned char prev[0x18];
        std::memcpy(prev, s.order, sizeof(prev));
        const bool reqChanged = RequestChanged(w, s.req), orderChanged = OrderChanged(o, prev);
        if (DueSend(reqChanged, s.req_at, now)) {
            std::memcpy(s.req, w, sizeof(w));
            ++s.req_seq;
            s.req_dirty = true;
            s.req_at = now;
            g_cStaged.fetch_add(1, std::memory_order_relaxed);
        }
        if (DueSend(orderChanged, s.order_at, now)) {
            std::memcpy(s.order, o, sizeof(o));
            const std::uint32_t flags = havePos ? 1u : 0u;
            std::memcpy(s.order + 0x18, own, sizeof(own));
            std::memcpy(s.order + 0x24, &flags, sizeof(flags));
            ++s.order_seq;
            s.order_dirty = true;
            s.order_at = now;
        }
        ReleaseSRWLockExclusive(&g_actLock);
    }
}

// FM1, the puppet side (hook thread, before the manager update): a new request is unpacked into the puppet's record
// with his network-position entry pre-filled with his own position (a table that does not cover him is lent one for the
// call); the order is re-posted every update (offline the manager marks a new order done at once).
struct FakeTable {
    std::uint64_t pad;
    unsigned char* data;
    std::uint32_t count, pad2;
};
alignas(16) unsigned char g_fakeEntries[kMaxSoldiers][0x18]{};
FakeTable g_fake{0, &g_fakeEntries[0][0], kMaxSoldiers, 0};
bool UnpackRequestSafe(const View& v, unsigned i, const unsigned char (&w)[0x70], const float (&at)[3], bool first) noexcept {
    const std::uintptr_t r = Record(v, i);
    const std::uintptr_t slotTable = v.s + c::s_netpos;
    std::uintptr_t table = 0;
    Rd(slotTable, table);
    std::uint32_t count = 0;
    if (table) Rd(table + c::net_count, count);
    std::uintptr_t data = 0;
    if (table) Rd(table + c::net_data, data);
    const bool covered = table && data && i < count;
    const bool plan = g_planOn.load(std::memory_order_relaxed);
    // Round 8 track C: where this copy stands before the unpack (warp15: the unpack's 15 m warp, read back after) and, with
    // act_plan, the lent entry's yaw: a warped puppet faces the request's facing (face mode 1) or keeps his own yaw (the
    // slot entry +0xc), not yaw 0.
    float self[3]{}, ownYaw = 0.0f;
    const bool haveSelf = Pos(v, i, self);
    if (haveSelf) ownYaw = reinterpret_cast<const float*>(v.pos + static_cast<std::uintptr_t>(i) * c::slots_pos_stride)[3];
    if (!std::isfinite(ownYaw)) ownYaw = 0.0f;
    const bool warp = haveSelf && Warp15(self, at);
    unsigned char saved[0x18]{}, entry[0x18]{};
    PrefillEntry(entry, at, plan ? EntryYaw(w, ownYaw) : 0.0f);
    __try {
        if (covered) {
            const std::uintptr_t e = data + static_cast<std::uintptr_t>(i) * c::net_stride;
            std::memcpy(saved, reinterpret_cast<const void*>(e), sizeof(saved));
            std::memcpy(entry + 0xc, saved + 0xc, 4);
            std::memcpy(reinterpret_cast<void*>(e), entry, sizeof(entry));
        } else {
            g_cTable.fetch_add(1, std::memory_order_relaxed);
            std::memcpy(g_fakeEntries[i], entry, sizeof(entry));
            *reinterpret_cast<volatile std::uintptr_t*>(slotTable) = reinterpret_cast<std::uintptr_t>(&g_fake);
        }
        reinterpret_cast<UnpackRequest>(g_base + c::unpack_request_rva)(reinterpret_cast<void*>(v.fob), reinterpret_cast<void*>(r), w, i);
        if (covered) std::memcpy(reinterpret_cast<void*>(data + static_cast<std::uintptr_t>(i) * c::net_stride), saved, sizeof(saved));
        else *reinterpret_cast<volatile std::uintptr_t*>(slotTable) = table;
        // Facing by id (modes 3 / 4): the players swap between the copies.
        std::uint32_t bits = 0;
        Rd(r + c::r_bits, bits);
        const unsigned face = (bits >> 6) & 7u;
        if (face == 3 || face == 4) {
            auto* id = reinterpret_cast<volatile std::uint16_t*>(r + 0x98);
            const std::uint16_t was = *id, now = RemapPlayer(was);
            if (now != was) { *id = now; g_cRemap.fetch_add(1, std::memory_order_relaxed); }
        }
        // Round 8 track C (act_plan): the gait catch-up for a walk (dest mode 1, which the unpack copies as sent): the owner
        // copy ahead toward R+0x80 by 2..5 m: gait + 1 (cap 3), beyond 5 m: 3 (the unpack's own mode 0 rule).
        if (plan && ((bits >> 9) & 0xfu) == 1u) {
            float now3[3]{}, dest[3]{};
            std::memcpy(dest, reinterpret_cast<const void*>(r + c::r_dest), sizeof(dest));
            if (Pos(v, i, now3)) {
                const unsigned gait = (bits >> 1) & 7u, up = GaitCatchUp(AheadM(now3, at, dest), gait);
                if (up != gait) {
                    auto* rb = reinterpret_cast<volatile std::uint32_t*>(r + c::r_bits);
                    *rb = (*rb & ~0xeu) | ((up & 7u) << 1);
                    g_cGaitUp.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        *reinterpret_cast<volatile std::uint16_t*>(r + c::r_flags) |= 0x80u;
        if (plan && i < kMaxSoldiers) g_appliedNow[i] = true;
        if (warp) {
            float after[3]{};
            Pos(v, i, after);
            g_cWarp15.fetch_add(1, std::memory_order_relaxed);
            char line[400]{};
            const int n = std::snprintf(line, sizeof(line),
                "soldier_act_warp15; t=%llu; idx=%u; first=%u; d_m=%.1f; p=%.2f,%.2f,%.2f; at=%.2f,%.2f,%.2f; after=%.2f,%.2f,%.2f; "
                "moved_m=%.1f; yaw=%.3f; plan=%u\r\n",
                static_cast<unsigned long long>(NowMs()), i, first ? 1u : 0u, Dist(self, at), self[0], self[1], self[2], at[0], at[1], at[2],
                after[0], after[1], after[2], Dist(self, after), plan ? EntryYaw(w, ownYaw) : 0.0f, plan ? 1u : 0u);
            if (n > 0) Emit(line);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!covered) *reinterpret_cast<volatile std::uintptr_t*>(slotTable) = table;
        g_cFault.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}
bool UnpackOrderSafe(const View& v, unsigned i, const unsigned char (&o)[0x18]) noexcept {
    const std::uintptr_t r = Record(v, i);
    __try {
        reinterpret_cast<UnpackOrder>(g_base + c::unpack_order_rva)(reinterpret_cast<void*>(v.fob), reinterpret_cast<void*>(r + c::r_order), o);
        // Round 6 (run fbf3522a): an order without a target leaves the puppet's own stale target (the unpack writes one
        // only for modes 1..4) and his legs walk to it: clear it, as the owner's record of that order is.
        if (!OrderHasTarget(o)) {
            unsigned char rec[0x20];
            std::memcpy(rec, reinterpret_cast<const void*>(r + c::r_order), sizeof(rec));
            if (ClearOrderTarget(rec)) {
                std::memcpy(reinterpret_cast<void*>(r + c::r_order), rec, 0xd);
                g_cOrderClear.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // Target by id (mode 2: u16 id + u16 part at R+0x1b0): the players swap between the copies.
        std::uint8_t mode = 0;
        Rd(r + c::r_order_mode, mode);
        if ((mode & 7u) == 2) {
            auto* id = reinterpret_cast<volatile std::uint16_t*>(r + c::r_order);
            const std::uint16_t was = *id, now = RemapPlayer(was);
            if (now != was) { *id = now; g_cRemap.fetch_add(1, std::memory_order_relaxed); }
        }
        *reinterpret_cast<volatile std::uint16_t*>(r + c::r_order_flags) |= 1u;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_cFault.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}
void OwnDests(const View& v, std::uint64_t now) noexcept {
    if (g_ownScanAt && now - g_ownScanAt < kCensusPeriodMs) return;
    g_ownScanAt = now;
    for (unsigned i = 0; i < v.count && i < kMaxSoldiers; ++i) {
        if (!Bit(v.realized, i) || g_mirAt[i].load(std::memory_order_relaxed)) continue;  // a puppet holds the host's request
        unsigned char w[0x70]{}, o[0x18]{};
        if (!PackSafe(v, i, w, o)) continue;
        std::uint64_t p = 0;
        std::memcpy(&p, w + 0x40, sizeof(p));
        float d[3]{};
        DecodePacked(p, d);
        for (int k = 0; k < 3; ++k) g_ownDest[i][k].store(d[k], std::memory_order_relaxed);
        float self[3]{};
        if (Pos(v, i, self))
            for (int k = 0; k < 3; ++k) g_ownPos[i][k].store(self[k], std::memory_order_relaxed);  // round 8 C: TakeNear
        g_ownAt[i].store(now, std::memory_order_release);
    }
}
void MirrorRx(const View& v, std::uint64_t now) noexcept {
    // The fire-drop mask lapses with the apply's own lease (kActFreshMs without ACT_V1 m), whatever Lua forgot to release.
    g_cExpired.fetch_add(soldier_authority::ExpireMirrored(now, kActFreshMs), std::memory_order_relaxed);
    for (unsigned i = 0; i < v.count; ++i) {
        const std::uint64_t at = g_mirAt[i].load(std::memory_order_relaxed);
        if (!at || now - at > kActFreshMs) continue;
        if (!Bit(v.realized, i)) { g_cUnrealized.fetch_add(1, std::memory_order_relaxed); continue; }  // nothing drawn to animate
        unsigned char w[0x70]{}, o[0x18]{};
        float owner[3]{};
        bool haveReq = false, newReq = false, haveOrder = false, haveOwner = false;
        std::uint16_t seq = 0;
        AcquireSRWLockShared(&g_actLock);
        const Mail& m = g_mail[i];
        if (m.req_valid) { std::memcpy(w, m.req, sizeof(w)); seq = m.req_seq; haveReq = true; newReq = m.req_seq != m.applied_seq; }
        if (m.order_valid) {
            std::memcpy(o, m.order, sizeof(o));
            haveOrder = true;
            std::uint32_t flags = 0;
            std::memcpy(&flags, m.order + 0x24, sizeof(flags));
            if (flags & 1u) { std::memcpy(owner, m.order + 0x18, sizeof(owner)); haveOwner = true; }
        }
        ReleaseSRWLockShared(&g_actLock);
        // U3: the aim's target id names the owner's own player (id 0 on the host): swapped, the puppet aims at the host's
        // character here, not at the guest (payload_ids counts the swaps).
        // The mailbox keeps the wire bytes, so every copy taken from it is swapped; counted once per new request.
        if (haveReq && RemapAimTarget(w) && newReq) g_cPayloadIds.fetch_add(1, std::memory_order_relaxed);
        // The entry takes the owner copy's position (the engine's own FOB rule: the copy is warped beyond 15 m of it, and
        // the catch-up runs it toward the packed position), or this copy's own one before the first order part.
        // Round 6 (design 7.19): a first or changed request at once; an unchanged one every 1 s while the owner copy moves
        // (so the catch-up runs every second), else every 5 s (ReapplyDue), whether or not a new part arrived.
        float own[3]{};
        if (haveReq && Pos(v, i, own)) {
            Applied& a = g_applied[i];
            const std::uint32_t gen = g_mirGen[i].load(std::memory_order_relaxed);
            const bool first = !a.valid || a.gen != gen;
            const bool changed = !first && newReq && RequestChanged(w, a.req);
            const bool moving = !first && haveOwner && a.have_owner && OwnerMoved(a.owner, owner);
            if (ReapplyDue(first, changed, moving, now >= a.at ? now - a.at : 0)) {
                g_cDue.fetch_add(1, std::memory_order_relaxed);
                if (UnpackRequestSafe(v, i, w, haveOwner ? owner : own, first)) {
                    g_cApplied.fetch_add(1, std::memory_order_relaxed);
                    if (!first && !changed && moving) g_cReapply.fetch_add(1, std::memory_order_relaxed);
                    std::memcpy(a.req, w, sizeof(w));
                    a.valid = true;
                    a.gen = gen;
                    a.at = now;
                    a.have_owner = haveOwner;
                    if (haveOwner) std::memcpy(a.owner, owner, sizeof(owner));
                }
            } else if (newReq) {
                g_cSame.fetch_add(1, std::memory_order_relaxed);
            }
            if (newReq) {
                AcquireSRWLockExclusive(&g_actLock);
                g_mail[i].applied_seq = seq;
                ReleaseSRWLockExclusive(&g_actLock);
            }
        }
        if (haveOrder && UnpackOrderSafe(v, i, o)) g_cOrders.fetch_add(1, std::memory_order_relaxed);
    }
}

void FrameCensus(std::uint64_t now) noexcept {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    const std::uint64_t us = g_qpf.QuadPart ? static_cast<std::uint64_t>(t.QuadPart) * 1000000ull / static_cast<std::uint64_t>(g_qpf.QuadPart) : 0;
    if (g_frameLast && us > g_frameLast && g_frameN < 256) g_frameUs[g_frameN++] = static_cast<std::uint32_t>(us - g_frameLast);
    g_frameLast = us;
    if (now - g_frameAt < 1000) return;
    g_frameAt = now;
    if (!g_frameN) return;
    std::uint32_t sorted[256];
    std::memcpy(sorted, g_frameUs, g_frameN * sizeof(std::uint32_t));
    for (unsigned a = 1; a < g_frameN; ++a) {  // insertion sort, <= 256
        const std::uint32_t x = sorted[a];
        unsigned b = a;
        while (b && sorted[b - 1] > x) { sorted[b] = sorted[b - 1]; --b; }
        sorted[b] = x;
    }
    const std::uint32_t p95 = sorted[(g_frameN * 95) / 100 < g_frameN ? (g_frameN * 95) / 100 : g_frameN - 1];
    g_frameP95.store(p95, std::memory_order_relaxed);
    g_frames.store(g_frameN, std::memory_order_relaxed);
    // Round 6 (3): the line also prints payload_ids (the aim-target swaps), due and reapply (FormatFrameLine).
    char line[768]{};
    if (FormatFrameLine(now, ReadCounters(), line, sizeof(line))) Emit(line);
    g_frameN = 0;
}

// Round 8 track C (act_plan): kPlanCheckMs after a plan, the follower's path end against R+0x80 (soldier_act_plan line).
void PlanCheckStep(const View& v, unsigned i, std::uint64_t now) noexcept {
    PlanCheck& pc = g_planCheck[i];
    if (!pc.pending || now - pc.at < kPlanCheckMs) return;
    pc.pending = false;
    Follower f{};
    bool has = false;
    std::uint32_t count = 0;
    float end[3]{}, p[3]{};
    const bool read = FollowerFor(v, i, f) && FollowerPath(f, has, count, end);
    Pos(v, i, p);
    const bool path = read && has && count;
    const bool ok = path && PlanEndNear(end, pc.dest);
    (path ? (ok ? g_cPlanOk : g_cPlanFar) : g_cPlanNoPath).fetch_add(1, std::memory_order_relaxed);
    std::uint16_t r19c = 0, r19e = 0;
    Rd(Record(v, i) + c::r_handle, r19c);
    Rd(Record(v, i) + c::r_flags, r19e);
    char line[480]{};
    const int n = std::snprintf(line, sizeof(line),
        "soldier_act_plan; t=%llu; idx=%u; handle=0x%04x; r19c=0x%04x; r19e=0x%04x; ms=%llu; has=%u; n=%u; end=%.2f,%.2f,%.2f; "
        "dest=%.2f,%.2f,%.2f; d_end=%.2f; ok=%u; from=%.2f,%.2f,%.2f; p=%.2f,%.2f,%.2f\r\n",
        static_cast<unsigned long long>(now), i, pc.handle, r19c, r19e, static_cast<unsigned long long>(now - pc.at), has ? 1u : 0u, count,
        end[0], end[1], end[2], pc.dest[0], pc.dest[1], pc.dest[2], path ? Dist(end, pc.dest) : -1.0f, ok ? 1u : 0u, pc.from[0],
        pc.from[1], pc.from[2], p[0], p[1], p[2]);
    if (n > 0) Emit(line);
}

// Round 8 track C (act_plan; hook thread, after the original manager update, so after the engine's own offline start of
// each puppet's new request): 0x1413725f0 again with the shadow gate object, for the planner to plan from where he stands
// to R+0x80 (finding 1: offline the start plans nothing and his follower keeps his brain's old path).
void PlanStep(std::uintptr_t m) noexcept {
    __try {
        View v{};
        if (!Resolve(m, v)) return;
        const std::uint64_t now = NowMs();
        if (!g_shadowBuilt) { BuildShadow(g_shadow); g_shadowBuilt = true; }
        std::uintptr_t slots = 0;
        Rd(v.s + c::s_slots, slots);
        for (unsigned i = 0; i < v.count && i < kMaxSoldiers; ++i) {
            PlanCheckStep(v, i, now);
            // A released puppet: cancel a plan of ours that still holds his slot (his brain's next walk is not refused).
            const std::uint64_t mat = g_mirAt[i].load(std::memory_order_relaxed);
            if (g_planned[i] && (!mat || now - mat > kActFreshMs)) {
                g_planned[i] = false;
                Follower fr{};
                Slot sr{};
                std::uint16_t h = 0xffff;
                const std::uintptr_t rr = Record(v, i);
                Rd(rr + c::r_handle, h);
                if (FollowerFor(v, i, fr) && PlannerKnown(fr.planner) && PlannerSlot(fr.planner, i, sr) && ReleaseCancel(sr.hash, sr.handle, h)) {
                    CancelSlot(fr.planner, i, h);
                    *reinterpret_cast<volatile std::uint16_t*>(rr + c::r_handle) = 0xffff;
                    g_cPlanRelease.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (!g_appliedNow[i]) continue;
            g_appliedNow[i] = false;
            const std::uintptr_t r = Record(v, i);
            std::uint32_t bits = 0;
            std::uint16_t r19c = 0, r19e = 0;
            Rd(r + c::r_bits, bits);
            Rd(r + c::r_handle, r19c);
            Rd(r + c::r_flags, r19e);
            const unsigned mode = (bits >> 9) & 0xfu;
            if (mode != 1) continue;  // modes 0 / 2 are not planned this round (not counted)
            Follower f{};
            if (!PlanGate(mode, r19c, r19e, true) || !slots || !FollowerFor(v, i, f) || !PlannerKnown(f.planner)) {
                g_cPlanSkip.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            // Session 1: the planner refuses a request of another type than the slot's (his brain's walk): cancel that
            // live handle first (the engine's cancel); another type with no handle is left alone.
            Slot sl{};
            PlannerSlot(f.planner, i, sl);
            const SlotStep step = PlanSlot(sl.hash, sl.handle);
            if (step == SlotStep::busy) { g_cPlanBusy.fetch_add(1, std::memory_order_relaxed); continue; }
            if (step == SlotStep::cancel_then_plan) {
                CancelSlot(f.planner, i, sl.handle);
                g_cPlanCancel.fetch_add(1, std::memory_order_relaxed);
            }
            const char planned = reinterpret_cast<PlanStart>(g_base + c::plan_start_rva)(g_shadow.p, i, bits & 1u, reinterpret_cast<void*>(r),
                reinterpret_cast<void*>(slots), reinterpret_cast<void*>(f.pair), reinterpret_cast<void*>(f.planner));
            if (!planned) { g_cPlanSkip.fetch_add(1, std::memory_order_relaxed); continue; }
            g_planned[i] = true;
            std::uint16_t got = 0xffff;
            Rd(r + c::r_handle, got);
            if (got == 0xffff) { g_cPlanNone.fetch_add(1, std::memory_order_relaxed); continue; }
            g_cPlan.fetch_add(1, std::memory_order_relaxed);
            PlanCheck& pc = g_planCheck[i];
            pc.pending = true;
            pc.at = now;
            Rd(r + c::r_handle, pc.handle);
            std::memcpy(pc.dest, reinterpret_cast<const void*>(r + c::r_dest), sizeof(pc.dest));
            Pos(v, i, pc.from);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_cPlanFault.fetch_add(1, std::memory_order_relaxed);
        g_faults.fetch_add(1, std::memory_order_relaxed);
        std::memset(g_appliedNow, 0, sizeof(g_appliedNow));
    }
}

// Enemy round 8 (quiet_place, docs/world-sync-design.md 7.22): the staged quiet moves, each through the engine's own
// network-position warp 0x136ece0(M-0x20, idx, entry) (Ghidra, read only: it reads S = (M-0x20)+0x70's slot table only,
// writes the slot position and yaw, and for a guard in the realized list *(S+0x18)+0x28 moves his body through the
// character controller (S vt[0x100] -> vt[0x388]; rotation vt+0x70, position vt+0x68 with y + controller +0x160); it
// tests no puppet bit, no S+0x2a4 mode and sends no message). A slot within 15 m of the target is biased first (QuietBias).
// The realized list 0x136ece0 looks idx up in before it moves the body (u16 entries *(S+0x18)+0x28, count +0x44).
bool Listed(const View& v, unsigned i) noexcept {
    std::uintptr_t list = 0;
    std::uint32_t count = 0;
    if (!Rd(v.slots + c::slots_list, list) || !Rd(v.slots + c::slots_list_count, count) || !list || count > 4096) return false;
    // Round 9 land step (the review): the list entries are game memory, read under SEH (QuietOne calls this outside its __try).
    const int found = ListFind(reinterpret_cast<const std::uint16_t*>(list), count, i);
    if (found < 0) g_faults.fetch_add(1, std::memory_order_relaxed);
    return found == 1;
}
void QuietOne(const View& v, unsigned i, const QuietMove& q) noexcept {
    float slot[3]{}, biased[3]{}, after[3]{};
    unsigned moved = 0, bias = 0;
    float err = -1.0f, dy = 0.0f;
    float* p = nullptr;
    // Round 9: not in the realized list now, the warp would write the slot and leave the body (1befcef4): no move.
    if (i < v.count && Bit(v.realized, i) && Listed(v, i) && Pos(v, i, slot)) {
        p = reinterpret_cast<float*>(v.pos + static_cast<std::uintptr_t>(i) * c::slots_pos_stride);
        alignas(16) unsigned char entry[0x18]{};
        PrefillEntry(entry, q.pos, q.yaw);
        __try {
            if (QuietBias(slot, q.pos, biased)) {
                bias = 1;
                p[0] = biased[0]; p[1] = biased[1]; p[2] = biased[2];
            }
            moved = reinterpret_cast<NetWarp>(g_base + c::quiet_warp_rva)(reinterpret_cast<void*>(v.fob), i,
                                                                          reinterpret_cast<const float*>(entry)) ? 1u : 0u;
            if (bias && !moved) { p[0] = slot[0]; p[1] = slot[1]; p[2] = slot[2]; }
            if (Pos(v, i, after)) {
                const float dx = after[0] - q.pos[0], dz = after[2] - q.pos[2];
                err = std::sqrt(dx * dx + dz * dz);
                dy = after[1] - q.pos[1];
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (bias) { p[0] = slot[0]; p[1] = slot[1]; p[2] = slot[2]; }
            g_faults.fetch_add(1, std::memory_order_relaxed);
            moved = 0;
        }
    }
    char line[160]{};
    if (FormatQuietLine(q.go, q.seq, moved, bias, err, dy, line, sizeof(line))) Emit(line);
}
void QuietStep(std::uintptr_t m) noexcept {
    g_quietPending.store(false, std::memory_order_relaxed);
    View v{};
    __try {
        if (!Resolve(m, v)) return;
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_faults.fetch_add(1, std::memory_order_relaxed); return; }
    for (unsigned i = 0; i < kMaxSoldiers; ++i) {
        QuietMove q{};
        AcquireSRWLockExclusive(&g_quietLock);
        if (g_quiet[i].due) { q = g_quiet[i]; g_quiet[i].due = false; }
        ReleaseSRWLockExclusive(&g_quietLock);
        if (q.due) QuietOne(v, i, q);
    }
}

// Round 9 (quiet_place, design 7.24): the rest view of every realized guard (QuietCheck reads it on the runtime thread).
void RestStep(const View& v, std::uint64_t now) noexcept {
    std::uintptr_t list = 0, planner = 0;
    std::uint32_t listCount = 0;
    if (!Rd(v.slots + c::slots_list, list) || !Rd(v.slots + c::slots_list_count, listCount) || listCount > 4096) list = 0;
    Rd(v.s + c::s_planner, planner);
    const bool known = PlannerKnown(planner);
    for (unsigned i = 0; i < v.count; ++i) {
        RestView& r = g_restFresh[i];
        RestHist& h = g_restHist[i];
        r = {};
        r.at = now;
        r.speed_mps = -1.0f;
        r.realized = Bit(v.realized, i);
        float p[3]{};
        if (!r.realized || !Pos(v, i, p)) { h = {}; continue; }
        r.pos[0] = p[0]; r.pos[1] = p[1]; r.pos[2] = p[2];
        r.listed = list && ListHas(reinterpret_cast<const std::uint16_t*>(list), listCount, i);
        if (h.at[0] && now > h.at[0] && now - h.at[0] <= 4 * kRestMs) {
            const float dx = p[0] - h.pos[0][0], dz = p[2] - h.pos[0][2];
            r.speed_mps = std::sqrt(dx * dx + dz * dz) * 1000.0f / static_cast<float>(now - h.at[0]);
        }
        h.at[0] = h.at[1];
        std::memcpy(h.pos[0], h.pos[1], sizeof(h.pos[0]));
        h.at[1] = now;
        std::memcpy(h.pos[1], p, sizeof(p));
        Follower f{};
        bool has = false;
        std::uint32_t count = 0;
        float end[3]{};
        if (known && FollowerFor(v, i, f) && FollowerPath(f, has, count, end) && has) {
            r.has = true;
            // A path whose points cannot be read: as if it led away (the message).
            if (count) { r.end[0] = end[0]; r.end[1] = end[1]; r.end[2] = end[2]; }
            else { r.end[0] = p[0] + 1000.0f; r.end[1] = p[1]; r.end[2] = p[2]; }
        }
    }
    AcquireSRWLockExclusive(&g_restLock);
    std::memcpy(g_rest, g_restFresh, sizeof(RestView) * v.count);
    for (unsigned i = v.count; i < kMaxSoldiers; ++i) g_rest[i] = {};
    ReleaseSRWLockExclusive(&g_restLock);
}
void RestSafe(std::uintptr_t m, std::uint64_t now) noexcept {
    __try {
        View v{};
        if (!Resolve(m, v)) return;
        RestStep(v, now);
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_faults.fetch_add(1, std::memory_order_relaxed); }
}

void StepSafe(std::uintptr_t m) noexcept {
    __try {
        View v{};
        if (!Resolve(m, v)) return;
        const std::uint64_t now = NowMs();
        ProbeStep(v, now);
        if (g_actOn.load(std::memory_order_relaxed)) {
            FrameCensus(now);
            if (g_actHost.load(std::memory_order_relaxed)) MirrorTx(v, now);
            else { OwnDests(v, now); MirrorRx(v, now); }
        }
        if (g_census.load(std::memory_order_relaxed)) Census(v, now);
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_faults.fetch_add(1, std::memory_order_relaxed); }
}

void __fastcall UpdateDetour(void* m) {
    g_calls.fetch_add(1, std::memory_order_relaxed);
    const std::uint32_t tid = static_cast<std::uint32_t>(GetCurrentThreadId());
    std::uint32_t first = 0;
    if (!g_tid.compare_exchange_strong(first, tid, std::memory_order_relaxed) && first != tid)
        g_threads.store(2, std::memory_order_relaxed);
    else if (!g_threads.load(std::memory_order_relaxed)) g_threads.store(1, std::memory_order_relaxed);
    g_m.store(reinterpret_cast<std::uintptr_t>(m), std::memory_order_relaxed);
    if (g_restOn.load(std::memory_order_relaxed)) {
        const std::uint64_t now = NowMs();
        if (now - g_restAt >= kRestMs) {
            g_restAt = now;
            RestSafe(reinterpret_cast<std::uintptr_t>(m), now);
        }
    }
    if (g_quietPending.load(std::memory_order_relaxed)) QuietStep(reinterpret_cast<std::uintptr_t>(m));
    if (g_census.load(std::memory_order_relaxed) || g_actOn.load(std::memory_order_relaxed) || g_probe.active ||
        g_probe.pending.load(std::memory_order_relaxed))
        StepSafe(reinterpret_cast<std::uintptr_t>(m));
    g_original(m);
    // Round 8 track C (act_plan): after the engine's own start of each puppet's new request.
    if (g_planOn.load(std::memory_order_relaxed) && g_actOn.load(std::memory_order_relaxed) && !g_actHost.load(std::memory_order_relaxed))
        PlanStep(reinterpret_cast<std::uintptr_t>(m));
}

bool Matches(std::uintptr_t base, std::size_t span, const char** which) noexcept {
    for (const auto& site : c::sites) {
        if (site.rva > span || site.size > span - site.rva ||
            std::memcmp(reinterpret_cast<const void*>(base + site.rva), site.bytes, site.size) != 0) {
            if (which) *which = "site";
            return false;
        }
    }
    for (const auto& slot : c::slots) {
        std::uintptr_t value = 0;
        if (slot.rva > span || span - slot.rva < 8) { if (which) *which = "slot"; return false; }
        std::memcpy(&value, reinterpret_cast<const void*>(base + slot.rva), sizeof(value));
        if (value != base + slot.target) { if (which) *which = "slot"; return false; }
    }
    float radius = 0.0f;
    std::memcpy(&radius, reinterpret_cast<const void*>(base + 0x2329c00), sizeof(radius));
    if (radius != c::netpos_warp_sq) { if (which) *which = "data"; return false; }
    float planDist = 0.0f;
    std::memcpy(&planDist, reinterpret_cast<const void*>(base + c::plan_dist_rva), sizeof(planDist));
    if (planDist != c::plan_dist) { if (which) *which = "data"; return false; }
    return true;
}
} // namespace

bool Install(const void* base, std::size_t span, LogFn log) noexcept {
    g_log = log;
    if (g_installed.load()) return true;
    if (!base || !span) { if (log) log("soldier_act=refused; reason=image\r\n"); return false; }
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    const char* which = nullptr;
    bool ok = false;
    __try { ok = Matches(b, span, &which); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; which = "fault"; }
    if (!ok) {
        char line[96]{};
        std::snprintf(line, sizeof(line), "soldier_act=refused; reason=contract_%s\r\n", which ? which : "?");
        if (log) log(line);
        return false;
    }
    g_base = b;
    g_span = span;
    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) { if (log) log("soldier_act=refused; reason=hook_initialize\r\n"); return false; }
    void* target = reinterpret_cast<void*>(b + c::update_rva);
    if (!g_original && MH_CreateHook(target, reinterpret_cast<void*>(&UpdateDetour), reinterpret_cast<void**>(&g_original)) != MH_OK) {
        if (log) log("soldier_act=refused; reason=hook_create\r\n");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) { if (log) log("soldier_act=refused; reason=hook_enable\r\n"); return false; }
    g_installed.store(true);
    char line[96]{};
    std::snprintf(line, sizeof(line), "soldier_act=installed; rva=0x13735c0; sites=%u; slots=%u\r\n",
                  unsigned(sizeof(c::sites) / sizeof(c::sites[0])), unsigned(sizeof(c::slots) / sizeof(c::slots[0])));
    if (log) log(line);
    return true;
}

bool Installed() noexcept { return g_installed.load(); }
void SetPlan(bool on) noexcept { g_planOn.store(on); }
bool PlanOn() noexcept { return g_planOn.load(); }

QuietWhy QuietCheck(unsigned idx, const float (&target)[3]) noexcept {
    if (!g_installed.load() || !g_restOn.load() || idx >= kMaxSoldiers) return QuietWhy::unknown;
    AcquireSRWLockShared(&g_restLock);
    const RestView r = g_rest[idx];
    ReleaseSRWLockShared(&g_restLock);
    return QuietChoose(r, target, NowMs());
}
void SetQuietRest(bool on) noexcept { g_restOn.store(on); }

bool StageQuiet(unsigned idx, std::uint32_t go, std::uint16_t seq, const float (&pos)[3], float yaw) noexcept {
    if (!g_installed.load() || idx >= kMaxSoldiers) return false;
    AcquireSRWLockExclusive(&g_quietLock);
    g_quiet[idx] = QuietMove{true, go, seq, {pos[0], pos[1], pos[2]}, yaw};
    ReleaseSRWLockExclusive(&g_quietLock);
    g_quietPending.store(true, std::memory_order_release);
    return true;
}
void SetCensus(bool on) noexcept { g_census.store(on); }
bool CensusOn() noexcept { return g_census.load(); }

void SetLocalPlayer(const float (&pos)[3], std::uint64_t now_ms) noexcept {
    if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2])) return;
    g_px.store(pos[0], std::memory_order_relaxed);
    g_py.store(pos[1], std::memory_order_relaxed);
    g_pz.store(pos[2], std::memory_order_relaxed);
    g_pAt.store(now_ms ? now_ms : 1, std::memory_order_release);
}

bool StartProbe(std::uint16_t dst, std::uint16_t src, std::uint32_t ms, unsigned mode) noexcept {
    if (!g_installed.load() || g_probe.active || g_probe.pending.load() || mode > 2) return false;
    g_probe.dstWant = dst;
    g_probe.srcWant = src;
    g_probe.ms = ms > 30000 ? 30000 : (ms < 500 ? 500 : ms);
    g_probe.modeWant = mode;
    g_probe.pending.store(true, std::memory_order_release);
    return true;
}

void ConfigureMirror(bool on, bool host) noexcept {
    g_actHost.store(host);
    g_actOn.store(on && g_installed.load());
    if (!on) ReleaseAll();
}
bool MirrorOn() noexcept { return g_actOn.load(); }

void SetTx(unsigned idx, std::uint16_t slot, std::uint64_t now_ms) noexcept {
    if (idx >= kMaxSoldiers || !slot) return;
    g_txSlot[idx].store(slot, std::memory_order_relaxed);
    g_txAt[idx].store(now_ms ? now_ms : 1, std::memory_order_relaxed);
}

unsigned SendStaged(std::uint64_t now_ms) noexcept {
    if (!g_actOn.load() || !g_actHost.load()) return 0;
    if (now_ms - g_sendWindowAt >= 100) { g_sendWindowAt = now_ms; g_sendWindowParts = 0; }
    unsigned sent = 0;
    for (unsigned i = 0; i < kMaxSoldiers && g_sendWindowParts < kActPartsPer100Ms; ++i) {
        Stage s{};
        AcquireSRWLockExclusive(&g_actLock);
        const bool any = g_stage[i].req_dirty || g_stage[i].order_dirty;
        if (any) {
            s = g_stage[i];
            const unsigned need = (s.req_dirty ? 3u : 0u) + (s.order_dirty ? 1u : 0u);
            if (g_sendWindowParts + need > kActPartsPer100Ms) { ReleaseSRWLockExclusive(&g_actLock); break; }
            g_stage[i].req_dirty = g_stage[i].order_dirty = false;
        }
        ReleaseSRWLockExclusive(&g_actLock);
        if (!any || !s.slot) continue;
        world::Act a{};
        std::uint8_t req[world::kActRequest]{}, order[world::kActOrder]{};  // order: packed 0x18, owner position, flags
        std::memcpy(req, s.req, sizeof(req));
        std::memcpy(order, s.order, sizeof(order));
        (void)a;
        const auto tick = static_cast<std::uint32_t>(now_ms);
        if (s.req_dirty)
            for (unsigned part = 0; part < 3; ++part)
                if (world_link::SendAct(tick, s.slot, s.req_seq, part, req, order) == world::TxStatus::ok) { ++sent; ++g_sendWindowParts; }
        if (s.order_dirty && world_link::SendAct(tick, s.slot, s.order_seq, 3, req, order) == world::TxStatus::ok) { ++sent; ++g_sendWindowParts; }
    }
    g_cTx.fetch_add(sent, std::memory_order_relaxed);
    return sent;
}

bool SetMirror(unsigned idx, std::uint16_t slot, bool on, std::uint64_t now_ms) noexcept {
    if (idx >= kMaxSoldiers) return false;
    if (!on || !g_actOn.load()) {
        g_mirAt[idx].store(0, std::memory_order_relaxed);
        g_mirGen[idx].fetch_add(1, std::memory_order_relaxed);
        soldier_authority::SetMirrored(static_cast<std::uint16_t>(idx), false);
        AcquireSRWLockExclusive(&g_actLock);
        g_mail[idx] = Mail{};
        ReleaseSRWLockExclusive(&g_actLock);
        return false;
    }
    g_mirSlot[idx].store(slot, std::memory_order_relaxed);
    std::uint64_t now = 0;
    world::Act act{};
    const bool have = world_link::ActLatest(slot, now, act);
    bool fresh = false;
    AcquireSRWLockExclusive(&g_actLock);
    Mail& m = g_mail[idx];
    if (have && act.req_valid && (!m.req_valid || m.req_seq != act.req_seq)) {
        std::memcpy(m.req, act.req, sizeof(m.req));
        m.req_seq = act.req_seq;
        m.req_rx_ms = act.req_rx_ms;
        m.req_valid = true;
        g_cRx.fetch_add(1, std::memory_order_relaxed);
    }
    if (have && act.order_valid && (!m.order_valid || m.order_seq != act.order_seq)) {
        std::memcpy(m.order, act.order, sizeof(m.order));
        m.order_seq = act.order_seq;
        m.order_valid = true;
    }
    fresh = m.req_valid && now >= m.req_rx_ms && now - m.req_rx_ms <= kActFreshMs;
    ReleaseSRWLockExclusive(&g_actLock);
    if (!fresh) g_cStale.fetch_add(1, std::memory_order_relaxed);
    g_mirAt[idx].store(fresh ? (now_ms ? now_ms : 1) : 0, std::memory_order_relaxed);
    soldier_authority::SetMirrored(static_cast<std::uint16_t>(idx), fresh, now_ms ? now_ms : 1);  // the fire-drop lease
    return fresh;
}

std::atomic<bool> g_paused{false};
void SetPaused(bool paused) noexcept { g_paused.store(paused); if (paused) ReleaseAll(); }
bool Paused() noexcept { return g_paused.load(); }

bool QueryFresh(std::uint16_t slot, unsigned idx) noexcept {
    std::uint64_t now = 0;
    world::Act act{};
    if (!g_actOn.load() || !world_link::ActLatest(slot, now, act) || !act.req_valid) return false;
    if (!(now >= act.req_rx_ms && now - act.req_rx_ms <= kActFreshMs)) return false;
    // Round 6: a walker is taken only on the host copy's leg (his own pre-take leg is what his legs keep walking).
    float own[3]{};
    bool valid = false;
    if (idx < kMaxSoldiers) {
        const std::uint64_t at = g_ownAt[idx].load(std::memory_order_acquire);
        valid = at && now >= at && now - at <= kActFreshMs;
        for (int k = 0; k < 3; ++k) own[k] = g_ownDest[idx][k].load(std::memory_order_relaxed);
    }
    unsigned char req[0x70];
    std::memcpy(req, act.req, sizeof(req));
    if (!LegMatch(req, own, valid)) { g_cLegWait.fetch_add(1, std::memory_order_relaxed); return false; }
    // Round 8 track C (act_plan): and only within kActTakeNearM (flat) of the owner copy (the order part's position).
    if (g_planOn.load(std::memory_order_relaxed)) {
        float self[3]{}, owner[3]{};
        std::uint32_t flags = 0;
        std::memcpy(&flags, act.order + 0x24, sizeof(flags));
        std::memcpy(owner, act.order + 0x18, sizeof(owner));
        if (idx < kMaxSoldiers)
            for (int k = 0; k < 3; ++k) self[k] = g_ownPos[idx][k].load(std::memory_order_relaxed);
        if (!TakeNear(req, self, owner, valid && act.order_valid && (flags & 1u) != 0)) {
            g_cNearWait.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }
    return true;
}

void ReleaseAll() noexcept {
    for (unsigned i = 0; i < kMaxSoldiers; ++i) { g_mirAt[i].store(0); g_txAt[i].store(0); g_mirGen[i].fetch_add(1); }
    soldier_authority::ClearMirrored();
    AcquireSRWLockExclusive(&g_actLock);
    for (auto& m : g_mail) m = Mail{};
    ReleaseSRWLockExclusive(&g_actLock);
}

Counters ReadCounters() noexcept {
    Counters k{};
    k.tx_parts = g_cTx.load(); k.staged = g_cStaged.load(); k.rx = g_cRx.load(); k.applied = g_cApplied.load();
    k.orders = g_cOrders.load(); k.refuse_table = g_cTable.load(); k.refuse_fault = g_cFault.load(); k.refuse_stale = g_cStale.load();
    k.remap_player = g_cRemap.load(); k.payload_ids = g_cPayloadIds.load(); k.mirrored = soldier_authority::MirroredCount();
    soldier_authority::MirrorDrops(k.fire_dropped, k.noise_dropped);
    k.local_shots = soldier_authority::LocalShots();
    k.frame_p95_us = g_frameP95.load(); k.frames = g_frames.load();
    k.due = g_cDue.load(); k.reapply_moving = g_cReapply.load(); k.same = g_cSame.load(); k.unrealized = g_cUnrealized.load();
    k.expired = g_cExpired.load(); k.order_clear = g_cOrderClear.load(); k.leg_wait = g_cLegWait.load();
    k.plan = g_cPlan.load(); k.plan_skip = g_cPlanSkip.load(); k.warp15 = g_cWarp15.load(); k.plan_ok = g_cPlanOk.load();
    k.plan_far = g_cPlanFar.load(); k.plan_nopath = g_cPlanNoPath.load(); k.near_wait = g_cNearWait.load(); k.gait_up = g_cGaitUp.load();
    k.plan_fault = g_cPlanFault.load(); k.plan_cancel = g_cPlanCancel.load(); k.plan_busy = g_cPlanBusy.load();
    k.plan_none = g_cPlanNone.load(); k.plan_release = g_cPlanRelease.load();
    return k;
}

bool FormatFrameLine(std::uint64_t now, const Counters& k, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const int n = std::snprintf(out, capacity,
        "soldier_act_frame; t=%llu; frames=%u; p95_ms=%.2f; mirrored=%u; tx=%u; staged=%u; rx=%u; applied=%u; orders=%u; refuse_table=%u; "
        "fault=%u; remap=%u; puppet_fire=%u; puppet_noise=%u; shots=%u; same=%u; unrealized=%u; payload_ids=%u; due=%u; reapply=%u; "
        "expired=%u; order_clear=%u; leg_wait=%u; plan=%u; plan_skip=%u; warp15=%u; plan_ok=%u; plan_far=%u; plan_nopath=%u; "
        "near_wait=%u; gait_up=%u; plan_fault=%u; plan_cancel=%u; plan_busy=%u; plan_none=%u; plan_release=%u\r\n",
        static_cast<unsigned long long>(now), k.frames, k.frame_p95_us / 1000.0, k.mirrored, k.tx_parts, k.staged, k.rx, k.applied,
        k.orders, k.refuse_table, k.refuse_fault, k.remap_player, k.fire_dropped, k.noise_dropped, k.local_shots, k.same,
        k.unrealized, k.payload_ids, k.due, k.reapply_moving, k.expired, k.order_clear, k.leg_wait, k.plan, k.plan_skip, k.warp15,
        k.plan_ok, k.plan_far, k.plan_nopath, k.near_wait, k.gait_up, k.plan_fault, k.plan_cancel, k.plan_busy, k.plan_none,
        k.plan_release);
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}

bool MailOwner(unsigned idx, std::int32_t (&mm)[3]) noexcept {
    if (idx >= kMaxSoldiers) return false;
    float p[3]{};
    bool ok = false;
    AcquireSRWLockShared(&g_actLock);
    const Mail& m = g_mail[idx];
    if (m.order_valid) {
        std::uint32_t flags = 0;
        std::memcpy(&flags, m.order + 0x24, sizeof(flags));
        if (flags & 1u) { std::memcpy(p, m.order + 0x18, sizeof(p)); ok = true; }
    }
    ReleaseSRWLockShared(&g_actLock);
    if (!ok) return false;
    for (int k = 0; k < 3; ++k) {
        if (!(p[k] >= -100000.0f && p[k] <= 100000.0f)) return false;
        mm[k] = static_cast<std::int32_t>(p[k] * 1000.0f + (p[k] < 0.0f ? -0.5f : 0.5f));
    }
    return true;
}

bool FormatActReply(bool fresh, const Counters& k, char* out, std::size_t capacity, const std::int32_t* owner_mm) noexcept {
    if (!out || !capacity) return false;
    const int n = owner_mm
        ? std::snprintf(out, capacity, "return {f=%u,tx=%u,rx=%u,ap=%u,od=%u,sh=%u,mi=%u,fd=%u,fn=%u,ox=%d,oy=%d,oz=%d}", fresh ? 1u : 0u,
                        k.tx_parts, k.rx, k.applied, k.orders, k.local_shots, k.mirrored, k.fire_dropped, k.noise_dropped,
                        static_cast<int>(owner_mm[0]), static_cast<int>(owner_mm[1]), static_cast<int>(owner_mm[2]))
        : std::snprintf(out, capacity, "return {f=%u,tx=%u,rx=%u,ap=%u,od=%u,sh=%u,mi=%u,fd=%u,fn=%u}", fresh ? 1u : 0u, k.tx_parts,
                        k.rx, k.applied, k.orders, k.local_shots, k.mirrored, k.fire_dropped, k.noise_dropped);
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}

bool StatusLine(char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const int n = std::snprintf(out, capacity, "act;installed=%u;census=%u;m=%u;calls=%u;lines=%u;faults=%u;pack_faults=%u;threads=%u;probes=%u;probe=%u;expired=%u",
                                g_installed.load() ? 1u : 0u, g_census.load() ? 1u : 0u, g_m.load() ? 1u : 0u, g_calls.load(), g_lines.load(),
                                g_faults.load(), g_packFaults.load(), g_threads.load(), g_probeRuns.load(), g_probe.active ? 1u : 0u,
                                g_cExpired.load());
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}
#endif

} // namespace mgscoop::soldier_act
