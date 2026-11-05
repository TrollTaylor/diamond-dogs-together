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
    (void)host_request; (void)own_dest; (void)own_valid;
    return true;
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

#ifdef _WIN32
namespace {
namespace c = mgscoop::soldier_act_contract;
using Update = void(__fastcall*)(void* m);
using PackRequest = void(__fastcall*)(void* fob, void* out70, void* r, unsigned idx);
using PackOrder = void(__fastcall*)(void* fob, void* out18, void* order);
using UnpackRequest = void(__fastcall*)(void* fob, void* r, const void* in70, unsigned idx);
using UnpackOrder = void(__fastcall*)(void* fob, void* order, const void* in18);
using Post = void(__fastcall*)(void* m, unsigned idx, const void* req);

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
std::atomic<std::uint32_t> g_cExpired{0};  // fire-drop mask bits that lapsed (no ACT_V1 m within kActFreshMs)
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
    char line[1100]{};
    const int n = std::snprintf(line, sizeof(line),
        "soldier_act_census; t=%llu; idx=%u; d=%.1f; p=%.2f,%.2f,%.2f; v=%.2f; ex=%u; rz=%u; ac=%u; own=%u; peer=%u; mode=0x%x; "
        "puppet=%u; st=%u; np=%u; npf=0x%02x; npp=%.2f,%.2f,%.2f; r19e=0x%04x; r1a0=0x%04x; r1d2=0x%04x; packed=%d; ka=%u; "
        "kb=%u; dm=%u; gait=%u; post=%u; fm=%u; b66=%u; path=%u; wep=0x%02x; dest=%.2f,%.2f,%.2f; w60=%s; o18=%s; o=%s; "
        "aim=%.2f,%.2f,%.2f; ammo=%s; mag=%u\r\n",
        static_cast<unsigned long long>(now), i, d, p[0], p[1], p[2], speed, Bit(v.exists, i) ? 1u : 0u, Bit(v.realized, i) ? 1u : 0u,
        Bit(v.active, i) ? 1u : 0u, own, v.peer, v.mode, Puppet(v, i) ? 1u : 0u, stance, covered ? 1u : 0u, npf, np[0], np[1], np[2],
        r19e, r1a0, r1d2, packed, f.kind_a, f.kind_b, f.dest_mode, f.gait, f.posture, f.face_mode, f.b66, f.path, f.weapon, f.dest[0],
        f.dest[1], f.dest[2], w61, ord18, order, aim[0], aim[1], aim[2], ammoHex, mag);
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
bool UnpackRequestSafe(const View& v, unsigned i, const unsigned char (&w)[0x70], const float (&at)[3]) noexcept {
    const std::uintptr_t r = Record(v, i);
    const std::uintptr_t slotTable = v.s + c::s_netpos;
    std::uintptr_t table = 0;
    Rd(slotTable, table);
    std::uint32_t count = 0;
    if (table) Rd(table + c::net_count, count);
    std::uintptr_t data = 0;
    if (table) Rd(table + c::net_data, data);
    const bool covered = table && data && i < count;
    unsigned char saved[0x18]{}, entry[0x18]{};
    PrefillEntry(entry, at, 0.0f);
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
        *reinterpret_cast<volatile std::uint16_t*>(r + c::r_flags) |= 0x80u;
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
                if (UnpackRequestSafe(v, i, w, haveOwner ? owner : own)) {
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
    char line[512]{};
    if (FormatFrameLine(now, ReadCounters(), line, sizeof(line))) Emit(line);
    g_frameN = 0;
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
            else MirrorRx(v, now);
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
    if (g_census.load(std::memory_order_relaxed) || g_actOn.load(std::memory_order_relaxed) || g_probe.active ||
        g_probe.pending.load(std::memory_order_relaxed))
        StepSafe(reinterpret_cast<std::uintptr_t>(m));
    g_original(m);
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
    if (log) log("soldier_act=installed; rva=0x13735c0; sites=17; slots=6\r\n");
    return true;
}

bool Installed() noexcept { return g_installed.load(); }
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

bool QueryFresh(std::uint16_t slot) noexcept {
    std::uint64_t now = 0;
    world::Act act{};
    if (!g_actOn.load() || !world_link::ActLatest(slot, now, act) || !act.req_valid) return false;
    return now >= act.req_rx_ms && now - act.req_rx_ms <= kActFreshMs;
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
    k.expired = g_cExpired.load(); k.order_clear = g_cOrderClear.load();
    return k;
}

bool FormatFrameLine(std::uint64_t now, const Counters& k, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const int n = std::snprintf(out, capacity,
        "soldier_act_frame; t=%llu; frames=%u; p95_ms=%.2f; mirrored=%u; tx=%u; staged=%u; rx=%u; applied=%u; orders=%u; refuse_table=%u; "
        "fault=%u; remap=%u; puppet_fire=%u; puppet_noise=%u; shots=%u; same=%u; unrealized=%u; payload_ids=%u; due=%u; reapply=%u; "
        "expired=%u; order_clear=%u\r\n",
        static_cast<unsigned long long>(now), k.frames, k.frame_p95_us / 1000.0, k.mirrored, k.tx_parts, k.staged, k.rx, k.applied,
        k.orders, k.refuse_table, k.refuse_fault, k.remap_player, k.fire_dropped, k.noise_dropped, k.local_shots, k.same,
        k.unrealized, k.payload_ids, k.due, k.reapply_moving, k.expired, k.order_clear);
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
