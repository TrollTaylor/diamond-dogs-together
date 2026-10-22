#pragma once
// WS4 v2 (docs/anim-round2-throws.md "WS4 v2"): the remote's C4 place (gun mode
// 46) and detonate (47) mirrored into body 1's own gun record, so body 1 kneels
// to place and plays the detonator motion on the viewer's screen. Pure: the
// input hook reads body 1's record, asks Step for at most one write before body
// 1's job (inside its window, with the support lease and the C4 lane admitted)
// and Verdict after the job. The only memory it ever changes is body 1's own
// rec+0x3CC, and only these bits: 18 (place request), 19 (detonate request) and
// 22-23 (the preview surface class). Lab key partner_native_place_mirror.
//
// Why a record write and not the remote's FIRE / ACTION (static, pinned in
// tools/generate_partner_contract.py "WS4 v2"):
//  - the per-record gun update 0x103E2D0 calls the decode 0x1095DF0, then the
//    mode's op 4 and op 0 (the next mode into rec+0x2C1), then the transition
//    0x1040A00, so a pre-job write is what op 0 sees;
//  - mode 43's op 0 picks 46 on rec+0x3CC bit 18; mode 44's op 0 picks 46 on bit
//    18 and 47 on bit 19;
//  - the decode sets 18 only on FIRE with a preview class in bits 22-23, which
//    only the local-only preview writes (mode 44 op 2 runs 0x104CF00 behind
//    rec+0x3C0 bit 0), and 19 only on ACTION with a live charge (rec+0x280 bit
//    23); body 1 never has either. It never clears 18;
//  - mode 46 op 1 clears 16..18 (0x107BBE1), puts its own probe's class into
//    20-21 and folds 22-23 into 20-21 when the probe finds none (0x107BD2C);
//    the placement itself (op 9) runs only for the local record (bit 0);
//  - mode 47 op 1 clears 19 (0x107EFCD); its op 10 detonation 0x1053270 only
//    clears 19 for a non-local record (0x105327F).
// So the write asks for the motion only: no charge is placed or set off in the
// viewer's world. The WS4 v1 digests (the viewer's placement latch, the placed
// owner table, the slot-3 stock words) still bracket every body-1 job and latch
// native support off for the mission on any change.
//
// The class bits and the decode (hidden run 289badb6): with a class in 22-23
// and a FIRE press in the same body-1 job, the decode's class-3 path asks the
// placed iface vt+0x40 0xFCB1F0, whose 0xFC9C00 claims the free owner row for
// id 1 (iface+0xB0: 65535 -> 1) in the viewer's placed table; the owner-table
// digest caught it and latched support off. So:
//  - a place write carries the class only in a job whose press word, read the
//    way the decode reads it (PP vt+0x40 0x1205E80: [PP+8]+0x50, or PP+0x468
//    when 0, & ~PP+0x28), has no FIRE (0x10); with a press the write waits up
//    to kMaxDefer jobs, then goes without the class;
//  - once body 1 is in 46 (rec+0x3C0 bit 26 set: the decode's place path is
//    off), Align copies 46 op 1's class (20-21) into 22-23 so its states 1-3
//    never see a mismatch (they abort the kneel); 46 op 2 clears 20-24
//    (0x107C2AB) on the way out.
#include <cstdint>

namespace mgscoop::partner_place_mirror {
namespace k {
inline constexpr std::uint32_t kPlaceReq = 1u << 18, kDetReq = 1u << 19;
inline constexpr unsigned kSurfaceShift = 20, kPreviewShift = 22;
inline constexpr std::uint32_t kSurfaceMask = 3u << kSurfaceShift, kPreviewMask = 3u << kPreviewShift;
// The bits a place write may change (and a revert restores).
inline constexpr std::uint32_t kPlaceBits = kPlaceReq | kDetReq | kPreviewMask;
inline constexpr std::uint8_t kModeHold = 43, kModeReady = 44, kModePlace = 46, kModeDetonate = 47;
inline constexpr std::uint8_t kClassC4 = 3;  // (rec+0x27C >> 6) & 7 of a C4 (activation kind 13, 0x1060495)
// Ground: the census' ground places carry class 1 in 20-21 and in 22-23 (runs
// a93db206 / d05a1267); used when the sender's word names none (a v6 peer).
inline constexpr std::uint8_t kDefaultSurface = 1;
// A remote edge waits this long for body 1 to reach 43/44 (network lag, the
// draw); a write op 0 did not take is reverted and retried this many times.
inline constexpr std::uint64_t kPendingMs = 1500;
inline constexpr unsigned kMaxTries = 8;
// A FIRE press in the job holds a place write back this many jobs (it is an
// edge: the next job has none); then the write goes without the class.
inline constexpr unsigned kMaxDefer = 4;
// Align: after a taken place, while body 1 is in 46 at state <= 3 with
// rec+0x3C0 bit 26, for at most this long.
inline constexpr std::uint64_t kAlignMs = 3000;
inline constexpr std::uint8_t kAlignLastState = 3;
inline constexpr std::uint32_t kGunBusy = 1u << 26;  // rec+0x3C0 bit 26 (46 op 1 sets it, op 2 clears it)
inline constexpr std::uint32_t kPadFire = 0x10;      // the press word's FIRE (the decode's place trigger)
} // namespace k

constexpr bool C4Equip(std::uint16_t equip) noexcept { return equip >= 391 && equip <= 395; }
constexpr bool FamilyMode(std::uint8_t mode) noexcept { return mode >= 43 && mode <= 47; }
// The sender's place word (partner_input::InputPacket::place_bits) from its rec+0x3CC.
constexpr std::uint8_t WireBits(std::uint32_t w3cc) noexcept { return static_cast<std::uint8_t>((w3cc >> 18) & 0x3Fu); }
// The class a place word names: the placed class (rec bits 20-21, word bits
// 2-3), else the preview class (22-23, word bits 4-5); 0 none.
constexpr std::uint8_t WireSurface(std::uint8_t bits) noexcept {
    const std::uint8_t placed = static_cast<std::uint8_t>((bits >> 2) & 3u), preview = static_cast<std::uint8_t>((bits >> 4) & 3u);
    return placed ? placed : preview;
}

enum class Act : std::uint8_t { none, place, detonate, align };
constexpr const char* ActName(Act a) noexcept {
    return a == Act::place ? "place" : a == Act::detonate ? "detonate" : a == Act::align ? "align" : "none";
}
// Why Step wrote nothing (or wrote: wrote), or why a pending episode was dropped.
enum class Why : std::uint8_t { idle, key, lane, body, equip, mode, busy, wrote, expired, left, tries, fire };
constexpr const char* WhyName(Why w) noexcept {
    switch (w) {
    case Why::key: return "key";
    case Why::lane: return "lane";
    case Why::body: return "body_unread";
    case Why::equip: return "body_not_c4";
    case Why::mode: return "body_mode";
    case Why::busy: return "busy";
    case Why::wrote: return "wrote";
    case Why::expired: return "expired";
    case Why::left: return "remote_left";
    case Why::tries: return "tries";
    case Why::fire: return "fire_press";
    default: return "idle";
    }
}

struct In {
    bool key = false;            // partner_native_place_mirror (needs the C4 lane key)
    bool lane = false;           // this job: the support lease and the C4 lane admitted (Kind::placed)
    bool remote_valid = false;   // the live frame's v5 gun group (GunFlag::Valid)
    std::uint8_t remote_mode = 0;
    bool place_valid = false;    // v7 GunFlag::Place
    std::uint8_t place_bits = 0;
    bool body_ok = false;        // body 1's record read and qualified
    std::uint16_t body_equip = 0;
    std::uint8_t body_cls = 0, body_mode = 0xFF, body_state = 0xFF;  // rec+0x2C2: the mode's state
    std::uint32_t body_3cc = 0, body_3c0 = 0;
    // The decode would see a FIRE press in this job (its own read of the press
    // word; true when unread): no class bits in a place write.
    bool fire_press = true;
    std::uint64_t now_ms = 0;
};
struct State {
    std::uint8_t remote_last = 0;     // the remote's last gun mode (edge detector)
    Act pending = Act::none;          // the remote episode waiting for a write
    std::uint64_t pending_ms = 0;     // its edge time
    std::uint8_t surface = 0;         // its class for 22-23
    bool surface_default = false;
    unsigned tries = 0;               // writes for this episode
    unsigned deferrals = 0;           // jobs a place write waited for a FIRE press to pass
    Why last_why = Why::idle;         // the last refusal while pending (the drop line names it)
    std::uint64_t align_until_ms = 0; // Align may run until then (set by a taken place)
    Act wrote = Act::none;            // the write in flight (Step -> Verdict)
    std::uint32_t before = 0, value = 0;
    std::uint32_t edges = 0, writes = 0, taken = 0, reverted = 0, dropped = 0, aligned = 0, unclassed = 0;
};
struct Out {
    Act act = Act::none;        // a write: value is body 1's new rec+0x3CC
    std::uint32_t value = 0;
    Why why = Why::idle;
    Act edge = Act::none;       // a new remote episode this step
    Act dropped = Act::none;    // a pending episode dropped this step (why: expired, remote_left, tries)
    Why dropped_after = Why::idle;  // the refusal it was waiting on
    bool classed = false;       // a place write carries the class in 22-23
};

namespace detail {
inline void Drop(State& s, Out& o, Why why) noexcept {
    o.dropped = s.pending;
    o.dropped_after = s.last_why;
    o.why = why;
    s.pending = Act::none;
    ++s.dropped;
}
} // namespace detail

// One body-1 job (before it). Edges come from the remote's gun mode alone (a
// frame without the gun group changes nothing); a write needs the key, the C4
// lane, body 1 holding a qualified C4 in 43/44 (place) or 44 (detonate) and
// neither request bit already set.
inline Out Step(State& s, const In& x) noexcept {
    Out o{};
    if (x.remote_valid) {
        const std::uint8_t m = x.remote_mode;
        if ((m == k::kModePlace || m == k::kModeDetonate) && m != s.remote_last) {
            s.pending = m == k::kModePlace ? Act::place : Act::detonate;
            s.pending_ms = x.now_ms;
            s.tries = 0;
            s.deferrals = 0;
            s.last_why = Why::idle;
            const std::uint8_t w = x.place_valid ? WireSurface(x.place_bits) : 0;
            s.surface = w ? w : k::kDefaultSurface;
            s.surface_default = w == 0;
            ++s.edges;
            o.edge = s.pending;
        } else if (s.pending != Act::none && !FamilyMode(m)) {
            detail::Drop(s, o, Why::left);
        }
        s.remote_last = m;
    }
    if (s.pending != Act::none && (x.now_ms < s.pending_ms || x.now_ms - s.pending_ms > k::kPendingMs)) detail::Drop(s, o, Why::expired);
    if (s.pending == Act::none) return o;
    const bool place = s.pending == Act::place;
    Why why = Why::wrote;
    if (!x.key) why = Why::key;
    else if (!x.lane) why = Why::lane;
    else if (!x.body_ok) why = Why::body;
    else if (!C4Equip(x.body_equip) || x.body_cls != k::kClassC4) why = Why::equip;
    else if (place ? x.body_mode != k::kModeHold && x.body_mode != k::kModeReady : x.body_mode != k::kModeReady) why = Why::mode;
    else if (x.body_3cc & (k::kPlaceReq | k::kDetReq)) why = Why::busy;
    else if (s.tries >= k::kMaxTries) { detail::Drop(s, o, Why::tries); return o; }
    else if (place && x.fire_press && s.deferrals < k::kMaxDefer) { ++s.deferrals; why = Why::fire; }
    if (why != Why::wrote) {
        o.why = why;
        s.last_why = why;
        return o;
    }
    o.act = s.pending;
    o.classed = place && !x.fire_press;
    if (place && !o.classed) ++s.unclassed;
    o.value = place ? (x.body_3cc & ~k::kPreviewMask) | k::kPlaceReq |
                          (o.classed ? std::uint32_t(s.surface & 3u) << k::kPreviewShift : 0u)
                    : x.body_3cc | k::kDetReq;
    o.why = Why::wrote;
    s.wrote = s.pending;
    s.before = x.body_3cc;
    s.value = o.value;
    ++s.tries;
    ++s.writes;
    s.pending = Act::none;
    return o;
}

// Body 1's record after the job.
struct After {
    bool ok = false;
    std::uint8_t mode = 0xFF, mode_next = 0;  // rec+0x2C0, rec+0x2C1
    std::uint32_t w3cc = 0;
};
struct Verdict {
    Act act = Act::none;     // the write this verdict is about (none: no write in flight)
    bool taken = false;      // body 1 entered (or is entering) the requested mode
    bool revert = false;     // our request is still set: write value back
    std::uint32_t value = 0;
    bool retry = false;      // the episode is pending again (within its TTL and tries)
};
// After the job of a Step that wrote: taken when body 1's mode (or the queued
// next mode) is the requested one; otherwise our request bit must not linger
// for a later op 0, so the written bits go back to their values before the
// write (only while our request is still set) and the episode retries.
inline Verdict Judge(State& s, const After& a, std::uint64_t now_ms) noexcept {
    Verdict v{};
    if (s.wrote == Act::none) return v;
    v.act = s.wrote;
    const bool place = s.wrote == Act::place;
    s.wrote = Act::none;
    const std::uint8_t target = place ? k::kModePlace : k::kModeDetonate;
    if (a.ok && (a.mode == target || a.mode_next == target)) {
        v.taken = true;
        ++s.taken;
        if (place) s.align_until_ms = now_ms + k::kAlignMs;
        return v;
    }
    const std::uint32_t req = place ? k::kPlaceReq : k::kDetReq;
    const std::uint32_t bits = place ? k::kPlaceBits : k::kDetReq;
    if (a.ok && (a.w3cc & req)) {
        v.revert = true;
        v.value = (a.w3cc & ~bits) | (s.before & bits);
        ++s.reverted;
    }
    if (s.tries < k::kMaxTries && now_ms >= s.pending_ms && now_ms - s.pending_ms <= k::kPendingMs) {
        s.pending = place ? Act::place : Act::detonate;
        v.retry = true;
    }
    return v;
}

// While body 1 kneels (46, state <= 3, rec+0x3C0 bit 26: the decode's place
// path is off) after a taken place: 22-23 := 20-21 (46 op 1's own class) when
// they differ, so states 1-3 do not abort. Needs the key and the lane like a
// write; Act::align with the new rec+0x3CC, or none.
inline Out Align(State& s, const In& x) noexcept {
    Out o{};
    if (!x.key || !x.lane || !x.body_ok || !C4Equip(x.body_equip) || x.body_cls != k::kClassC4 || x.body_mode != k::kModePlace ||
        x.body_state > k::kAlignLastState || !(x.body_3c0 & k::kGunBusy) || x.now_ms > s.align_until_ms)
        return o;
    const std::uint32_t placed = (x.body_3cc & k::kSurfaceMask) >> k::kSurfaceShift;
    const std::uint32_t preview = (x.body_3cc & k::kPreviewMask) >> k::kPreviewShift;
    if (placed == preview) return o;
    o.act = Act::align;
    o.why = Why::wrote;
    o.value = (x.body_3cc & ~k::kPreviewMask) | (placed << k::kPreviewShift);
    ++s.aligned;
    return o;
}

} // namespace mgscoop::partner_place_mirror
