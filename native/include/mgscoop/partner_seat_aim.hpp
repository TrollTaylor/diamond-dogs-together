#pragma once
// G2: aiming and shooting from the back seat, the read-only census (docs/native-puppet.md "Seat aim";
// research scratchpad complete/re-seat-aim.md). Lab key partner_seat_aim=0|1 (default 0, lab only:
// play forces it off and the observer also requires lab mode). Nothing here writes game memory or
// calls game code: the readers are templates over
//   Read = bool(std::uintptr_t address, void* out, std::size_t size) noexcept
// (the observer passes ReadProcessMemory on its own process, the tests a bounds-checked fake), plus
// pure change detection, per-seat episodes, budgets and line formatting.
//
// Why (static, 1.0.15.4; every site is pinned in tools/generate_partner_contract.py SEAT_AIM_SITES):
//  - the player's gun plugin (AttackActionImpl::PollingImpl 0x1054210) enables the weapon through
//    EnableCheck 0x103E010: OrCheck(flags 0xC3, 0xC2) on the per-id flag record
//    rec = *(PS+0x280) + (id - *(PS+8))*0x20 refuses (0x103E192), else word 0 bit 24 (flag 0x18)
//    is set; PollingForLocal 0x1053930 starts a hold only with flag 0x18 set and flag 0xCF (word 6
//    bit 15) clear (0x1053B70);
//  - the ride plugin's Exec stage 0 (0x1247320) sets flag 0xC3 for an offline or local body
//    (0x12473A2) and then runs the ride phase with event 1; the back-seat passenger phase 0x1250CF0
//    event 1 clears flag 0xC3 again once its GetOn handed over to the seat loop (work+0x5C > 0,
//    0x1250FD6..0x1250FF8). The driver and get-off phases do not: a stock back-seat passenger is
//    allowed its weapon, a driver is not;
//  - UpdateAttackType 0x108CD40 picks the gun posture rec+0x3D4 from the act: 99 (the heli door,
//    RideHeli StateAtk 0x121FEB0) -> 5, 0x79 (horse) -> 3, 0x88 -> 6, else from flags (2, 1, 4) or
//    0; act 0x6E (the back seat) has no case, so a seated passenger would aim with posture 0 (the
//    standing upper-body set, graph 2) over its seated loop (graph 0).
// The census logs what the live game does with that, for the local player (k = 0) while its act is a
// vehicle ride act (0x6E passenger, 0x6F driver, 0x70 get-off) and for body 1 (k = 1) while the seat
// flow holds it: act, upper act, gun mode (7 aim, 9/10 fire, 11/12 reload), posture, the flag bits,
// and for k = 0 the camera mode and its ignore-collision handle.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "mgscoop/partner_placed_census.hpp"

namespace mgscoop::partner_seat_aim {
namespace k {
// Q per-id arrays: u16 act (*(Q+0x18))[id], u8 upper act (*(Q+0x20))[id].
inline constexpr std::size_t kQActs = 0x18, kQUpper = 0x20;
// The per-id flag record (FlagContainer<7>, OrCheck 0xFF3E90, Set 0xFF51D0): flag n is word n >> 5, bit n & 31.
inline constexpr std::size_t kPsFlags = 0x280, kPsBaseId = 0x8, kFlagStride = 0x20, kFlagWord6 = 0x18;
inline constexpr std::uint32_t kMaxIndex = 64;
inline constexpr std::uint32_t kW0Armed = 0x1000000;    // flag 0x18: EnableCheck's verdict (weapon usable)
inline constexpr std::uint32_t kW6NoWeaponB = 0x4;      // flag 0xC2: EnableCheck refuses (heli, carry, ... states set it)
inline constexpr std::uint32_t kW6NoWeapon = 0x8;       // flag 0xC3: the ride's stage 0 sets it, the passenger phase clears it
inline constexpr std::uint32_t kW6NoHold = 0x8000;      // flag 0xCF: PollingForLocal starts no hold
inline constexpr std::uint32_t kW6Slow = 0x1000000;     // flag 0xD8: the passenger phase (event 10, slow vehicle) and heli StateAtk
inline constexpr std::uint32_t kW6Bits = kW6NoWeaponB | kW6NoWeapon | kW6NoHold | kW6Slow;
// The gun record (partner_player::ReadGun): the requested next state u8 rec+0x2C1, the posture u8 rec+0x3D4.
inline constexpr std::size_t kRecNext = 0x2C1, kRecPosture = 0x3D4;
inline constexpr std::uint16_t kActPassenger = 0x6E, kActDriver = 0x6F, kActGetOff = 0x70;
inline constexpr std::uint8_t kUpperGunHold = 2;  // StateGunHoldStart enter 0x10756C9: Q+0x20[id] = 2
inline constexpr std::uint8_t kModeAim = 7, kModeFire = 9, kModeFire2 = 10, kModeReload = 11, kModeReload2 = 12;
// Budgets: sample lines at most 10 a second and 1200 a mission; end / status lines 2 a second and 400.
inline constexpr unsigned kSamplePerSecond = 10, kSampleCap = 1200, kOtherPerSecond = 2, kOtherCap = 400;
inline constexpr std::uint64_t kStatusMs = 5000;
// One episode step adds at most this much time (a stalled tick does not count as minutes of aiming).
inline constexpr std::uint32_t kMaxStepMs = 1000;
} // namespace k

// --- What is watched ----------------------------------------------------------------
constexpr bool RideAct(std::uint16_t act) noexcept {
    return act == k::kActPassenger || act == k::kActDriver || act == k::kActGetOff;
}
// k = 0: the local player's act is a vehicle ride act, or the native passenger report holds a back seat.
constexpr bool WatchLocal(bool act_ok, std::uint16_t act, bool passenger_report) noexcept {
    return passenger_report || (act_ok && RideAct(act));
}
// k = 1: the seat flow owns body 1 this tick (Mission::ride_seated) or plays its seat chain (arm / play / seated / exit).
constexpr bool WatchBody1(bool ride_seated, bool seat_flow_active) noexcept { return ride_seated || seat_flow_active; }
constexpr const char* WhoName(std::uint32_t k) noexcept { return k == 0 ? "local" : "body1"; }

// --- One instance's sample ---------------------------------------------------------
struct Sample {
    bool ok = false;        // act and upper act read
    bool gun_ok = false;    // the qualified ReadGun and its fields read
    bool flags_ok = false;  // the flag record read
    bool cam_ok = false;    // k = 0 only: the local camera read
    std::uint16_t act = 0xFFFF;
    std::uint8_t upper = 0xFF;
    std::uint8_t mode = 0xFF, next = 0xFF, posture = 0xFF, cls = 0xFF, hand = 0xFF;
    std::uint16_t equip = 0;
    std::uint32_t w0 = 0, w6 = 0;
    std::int32_t cam_mode = -1;
    std::uint64_t cam_handle = 0;
    std::uint8_t seat = 0xFF;       // the back seat (k = 0: the native passenger report; k = 1: the seat flow)
    std::uint16_t vehicle = 0xFFFF;
};
struct Flags {
    bool ok = false;
    std::uint32_t w0 = 0, w6 = 0;
};
// The flag record of `id` under PS: *(PS+0x280) + (id - *(PS+8))*0x20, words 0 and 6. Reads only.
template <class Read>
Flags ReadFlags(Read&& read, std::uintptr_t ps, std::uint32_t id) noexcept {
    Flags f{};
    std::uintptr_t records = 0;
    std::uint32_t base = 0;
    if (!ps || !read(ps + k::kPsFlags, &records, sizeof(records)) || !records || !read(ps + k::kPsBaseId, &base, sizeof(base)) ||
        id < base || id - base >= k::kMaxIndex) return f;
    const std::uintptr_t rec = records + static_cast<std::uintptr_t>(id - base) * k::kFlagStride;
    f.ok = read(rec, &f.w0, sizeof(f.w0)) && read(rec + k::kFlagWord6, &f.w6, sizeof(f.w6));
    if (!f.ok) f = Flags{};
    return f;
}
// u16 (*(Q+0x18))[id] and u8 (*(Q+0x20))[id]. Reads only.
template <class Read>
bool ReadActs(Read&& read, std::uintptr_t Q, std::uint32_t id, std::uint16_t& act, std::uint8_t& upper) noexcept {
    std::uintptr_t acts = 0, uppers = 0;
    if (!Q || id >= k::kMaxIndex || !read(Q + k::kQActs, &acts, sizeof(acts)) || !acts || !read(Q + k::kQUpper, &uppers, sizeof(uppers)) ||
        !uppers) return false;
    std::uint16_t a = 0;
    std::uint8_t u = 0;
    if (!read(acts + 2u * id, &a, sizeof(a)) || !read(uppers + id, &u, sizeof(u))) return false;
    act = a;
    upper = u;
    return true;
}
// rec+0x2C1 and rec+0x3D4 of a gun record ReadGun returned. Reads only.
template <class Read>
bool ReadGunExtra(Read&& read, std::uintptr_t rec, std::uint8_t& next, std::uint8_t& posture) noexcept {
    std::uint8_t n = 0, p = 0;
    if (!rec || !read(rec + k::kRecNext, &n, sizeof(n)) || !read(rec + k::kRecPosture, &p, sizeof(p))) return false;
    next = n;
    posture = p;
    return true;
}

constexpr bool Armed(const Sample& s) noexcept { return s.flags_ok && (s.w0 & k::kW0Armed) != 0; }
constexpr bool Bit(const Sample& s, std::uint32_t mask) noexcept { return s.flags_ok && (s.w6 & mask) != 0; }
constexpr bool Aiming(const Sample& s) noexcept {
    return (s.ok && s.upper == k::kUpperGunHold) || (s.gun_ok && s.mode == k::kModeAim);
}
constexpr bool Firing(const Sample& s) noexcept { return s.gun_ok && (s.mode == k::kModeFire || s.mode == k::kModeFire2); }
constexpr bool Reloading(const Sample& s) noexcept { return s.gun_ok && (s.mode == k::kModeReload || s.mode == k::kModeReload2); }
// The fields a sample line reports; another value of any of them is a new line.
constexpr bool SameKey(const Sample& a, const Sample& b) noexcept {
    return a.ok == b.ok && a.gun_ok == b.gun_ok && a.flags_ok == b.flags_ok && a.cam_ok == b.cam_ok && a.act == b.act &&
           a.upper == b.upper && a.mode == b.mode && a.posture == b.posture && a.cls == b.cls && a.equip == b.equip &&
           a.hand == b.hand && Armed(a) == Armed(b) && (a.w6 & k::kW6Bits) == (b.w6 & k::kW6Bits) && a.cam_mode == b.cam_mode &&
           (a.cam_handle != 0) == (b.cam_handle != 0) && a.seat == b.seat && a.vehicle == b.vehicle;
}

// --- Per-watch episode -------------------------------------------------------------
struct Episode {
    bool active = false;
    std::uint64_t start_ms = 0, last_ms = 0;
    unsigned samples = 0, aims = 0, fires = 0, reloads = 0;
    std::uint32_t armed_ms = 0, aim_ms = 0;
    bool was_armed = false, was_aiming = false, was_firing = false, was_reloading = false;
    std::uint8_t aim_posture = 0xFF;   // the posture of the last aim seen (0xFF: none)
    std::uint16_t first_act = 0xFFFF;  // the act of the first read sample
    std::uint16_t vehicle = 0xFFFF;
    std::uint8_t seat = 0xFF;
};
inline void Begin(Episode& e, std::uint64_t now_ms) noexcept {
    e = Episode{};
    e.active = true;
    e.start_ms = e.last_ms = now_ms;
}
// One read sample of an active episode: time armed / aiming since the last step, rising edges.
inline void Step(Episode& e, const Sample& s, std::uint64_t now_ms) noexcept {
    if (!e.active || !s.ok) return;
    std::uint64_t dt = now_ms >= e.last_ms ? now_ms - e.last_ms : 0;
    if (dt > k::kMaxStepMs) dt = k::kMaxStepMs;
    if (e.samples) {
        if (e.was_armed) e.armed_ms += static_cast<std::uint32_t>(dt);
        if (e.was_aiming) e.aim_ms += static_cast<std::uint32_t>(dt);
    } else {
        e.first_act = s.act;
    }
    e.last_ms = now_ms;
    ++e.samples;
    const bool armed = Armed(s), aiming = Aiming(s), firing = Firing(s), reloading = Reloading(s);
    if (aiming && !e.was_aiming) ++e.aims;
    if (firing && !e.was_firing) ++e.fires;
    if (reloading && !e.was_reloading) ++e.reloads;
    if (aiming && s.gun_ok) e.aim_posture = s.posture;
    e.was_armed = armed;
    e.was_aiming = aiming;
    e.was_firing = firing;
    e.was_reloading = reloading;
    if (s.vehicle != 0xFFFF) e.vehicle = s.vehicle;
    if (s.seat != 0xFF) e.seat = s.seat;
}

// --- Lines ---------------------------------------------------------------------------
// "partner_seat_aim=on; mission=..; read_only=1; writes=0; calls=0"
inline bool FormatOn(char* out, std::size_t capacity, std::uint32_t mission) noexcept {
    if (!out || !capacity) return false;
    const int n = std::snprintf(out, capacity, "partner_seat_aim=on; mission=%u; read_only=1; writes=0; calls=0\r\n", unsigned(mission));
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}
// "partner_seat_aim=sample; who=..; k=..; act=0x..; upper=0x..; mode=..; next=..; posture=..; cls=..; equip=..; hand=..;
//  armed=..; c2=..; c3=..; cf=..; d8=..; seat=..; vehicle=..[; cam_mode=..; cam_handle=0x..]"
// Unread groups print -1 (gun fields), na (flags) or are left out (camera).
inline bool FormatSample(char* out, std::size_t capacity, std::uint32_t k, const Sample& s) noexcept {
    if (!out || !capacity) return false;
    char gun[96]{}, flags[64]{}, cam[64]{};
    if (s.gun_ok)
        std::snprintf(gun, sizeof(gun), "mode=%u; next=%u; posture=%u; cls=%u; equip=%u; hand=%u", unsigned(s.mode), unsigned(s.next),
                      unsigned(s.posture), unsigned(s.cls), unsigned(s.equip), unsigned(s.hand));
    else
        std::snprintf(gun, sizeof(gun), "mode=-1; next=-1; posture=-1; cls=-1; equip=-1; hand=-1");
    if (s.flags_ok)
        std::snprintf(flags, sizeof(flags), "armed=%u; c2=%u; c3=%u; cf=%u; d8=%u", Armed(s) ? 1u : 0u, Bit(s, k::kW6NoWeaponB) ? 1u : 0u,
                      Bit(s, k::kW6NoWeapon) ? 1u : 0u, Bit(s, k::kW6NoHold) ? 1u : 0u, Bit(s, k::kW6Slow) ? 1u : 0u);
    else
        std::snprintf(flags, sizeof(flags), "armed=na; c2=na; c3=na; cf=na; d8=na");
    if (s.cam_ok)
        std::snprintf(cam, sizeof(cam), "; cam_mode=%d; cam_handle=0x%llx", int(s.cam_mode), static_cast<unsigned long long>(s.cam_handle));
    const int n = std::snprintf(out, capacity, "partner_seat_aim=sample; who=%s; k=%u; act=0x%x; upper=0x%02x; %s; %s; seat=%d; vehicle=%d%s\r\n",
                                WhoName(k), unsigned(k), unsigned(s.act), unsigned(s.upper), gun, flags,
                                s.seat == 0xFF ? -1 : int(s.seat), s.vehicle == 0xFFFF ? -1 : int(s.vehicle), cam);
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}
// "partner_seat_aim=end; who=..; k=..; reason=..; ms=..; samples=..; armed_ms=..; aim_ms=..; aims=..; fires=..; reloads=..;
//  posture=..; first_act=0x..; seat=..; vehicle=.."
inline bool FormatEnd(char* out, std::size_t capacity, std::uint32_t k, const Episode& e, std::uint64_t now_ms, const char* reason) noexcept {
    if (!out || !capacity) return false;
    const std::uint64_t ms = now_ms >= e.start_ms ? now_ms - e.start_ms : 0;
    const int n = std::snprintf(out, capacity,
                                "partner_seat_aim=end; who=%s; k=%u; reason=%s; ms=%llu; samples=%u; armed_ms=%u; aim_ms=%u; aims=%u; "
                                "fires=%u; reloads=%u; posture=%d; first_act=0x%x; seat=%d; vehicle=%d\r\n",
                                WhoName(k), unsigned(k), reason ? reason : "watch", static_cast<unsigned long long>(ms), e.samples,
                                unsigned(e.armed_ms), unsigned(e.aim_ms), e.aims, e.fires, e.reloads,
                                e.aim_posture == 0xFF ? -1 : int(e.aim_posture), unsigned(e.first_act), e.seat == 0xFF ? -1 : int(e.seat),
                                e.vehicle == 0xFFFF ? -1 : int(e.vehicle));
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}

// --- One instance's track ------------------------------------------------------------
struct Track {
    bool have = false;  // `last` is the reference of the next change
    Sample last{};
    Episode episode{};
};
struct Budgets {
    partner_placed_census::Budget sample{}, other{};
};
// Ends an active episode with an end line (other budget). Returns true when a line was emitted.
template <class Emit>
bool Finish(Track& t, std::uint32_t k, std::uint64_t now_ms, const char* reason, Budgets& b, Emit&& emit) noexcept {
    if (!t.episode.active) return false;
    char line[400]{};
    bool emitted = false;
    if (partner_placed_census::Take(b.other, now_ms, k::kOtherPerSecond, k::kOtherCap)) {
        if (FormatEnd(line, sizeof(line), k, t.episode, now_ms, reason)) { emit(line); emitted = true; }
    } else if (partner_placed_census::CapReport(b.other)) {
        emit("partner_seat_aim=capped; kind=other; limit=400\r\n");
    }
    t = Track{};
    return emitted;
}
// One observer tick for instance k: `watch` says whether k is watched now; `cur` is this tick's sample
// (cur.ok false: unread, the reference stays). Emits the end line of a finished episode and a sample
// line when a watched sample differs from the reference (sample budget; over this second's budget the
// reference stays so the change is retried; after the cap the reference moves and capped is said once).
template <class Emit>
void Tick(Track& t, std::uint32_t k, bool watch, const Sample& cur, std::uint64_t now_ms, Budgets& b, Emit&& emit) noexcept {
    if (!watch) {
        Finish(t, k, now_ms, "watch", b, emit);
        return;
    }
    if (!t.episode.active) Begin(t.episode, now_ms);
    if (!cur.ok) return;
    Step(t.episode, cur, now_ms);
    if (t.have && SameKey(t.last, cur)) return;
    if (partner_placed_census::Take(b.sample, now_ms, k::kSamplePerSecond, k::kSampleCap)) {
        char line[400]{};
        if (FormatSample(line, sizeof(line), k, cur)) emit(line);
    } else if (!b.sample.capped) {
        return;  // over this second's budget: retried against the same reference
    } else if (partner_placed_census::CapReport(b.sample)) {
        emit("partner_seat_aim=capped; kind=sample; limit=1200\r\n");
    }
    t.last = cur;
    t.have = true;
}
// "partner_seat_aim=status; local=..; body1=..; k0_act=0x..; k0_upper=0x..; k0_mode=..; k0_armed=..; k0_c3=..; k1_act=0x..;
//  k1_upper=0x..; k1_mode=..; k1_armed=..; k1_c3=..": every kStatusMs while either instance is watched.
inline bool FormatStatus(char* out, std::size_t capacity, const Track& local, const Track& body1) noexcept {
    if (!out || !capacity) return false;
    const auto armed = [](const Track& t) noexcept -> int { return t.have && t.last.flags_ok ? (Armed(t.last) ? 1 : 0) : -1; };
    const auto c3 = [](const Track& t) noexcept -> int { return t.have && t.last.flags_ok ? (Bit(t.last, k::kW6NoWeapon) ? 1 : 0) : -1; };
    const auto mode = [](const Track& t) noexcept -> int { return t.have && t.last.gun_ok ? int(t.last.mode) : -1; };
    const int n = std::snprintf(out, capacity,
                                "partner_seat_aim=status; local=%u; body1=%u; k0_act=0x%x; k0_upper=0x%02x; k0_mode=%d; k0_armed=%d; "
                                "k0_c3=%d; k1_act=0x%x; k1_upper=0x%02x; k1_mode=%d; k1_armed=%d; k1_c3=%d\r\n",
                                local.episode.active ? 1u : 0u, body1.episode.active ? 1u : 0u, unsigned(local.have ? local.last.act : 0xFFFF),
                                unsigned(local.have ? local.last.upper : 0xFF), mode(local), armed(local), c3(local),
                                unsigned(body1.have ? body1.last.act : 0xFFFF), unsigned(body1.have ? body1.last.upper : 0xFF), mode(body1),
                                armed(body1), c3(body1));
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}
} // namespace mgscoop::partner_seat_aim
