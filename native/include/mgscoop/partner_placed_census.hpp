#pragma once
// WS4 step 0: the read-only C4 / bionic-arm census (docs/anim-round2-throws.md
// "WS4 step 0: placed and arm census"). Lab key partner_placed_census=1
// (default 0, forced off in play). Nothing here writes game memory or calls
// game code: the readers are templates over
//   Read = bool(std::uintptr_t address, void* out, std::size_t size) noexcept
// (read-only and fault-safe; the observer passes ReadProcessMemory on its own
// process, the tests a bounds-checked fake), plus pure change detection,
// budgets and line formatting. The gun records come from the qualified
// partner_player::ReadGun (block, node, iface and prim vtable identities).
//
// The questions (spec WS4 "Step 0", critic #8-#12):
//  Q1 the gun-record class (rec+0x27C>>6)&7 and mode rec+0x2C0 of the VIEWER's
//     own type-13 C4 (391..395) and type-14 arm items (515 stun, 516, 517/518
//     rocket) while equipped, placing, detonating or arming (body 1's record
//     is logged the same way, k=1);
//  Q2 whether owner id 1 is registered in the placed-object service's owner
//     table (u16 iface+0xB0[0..iface+0x6C)), with each owner's live charges;
//  Q3 the gun plugin's placement latch (plugin+0x350 bit 3, u16 plugin+0x3E8
//     copied from u16 plugin+0x3D8 by mode 46 op 1 for any offline id) of each
//     instance, and whether both instances share one plugin (same_prim);
//  Q5 whether body 1's drain window shadows the PlayerPad fields the stun
//     arm's op 0 reads directly ([P+0xB0]+0x10 float > 0.1 and +0x2C bit 0):
//     the input hook publishes the viewer's saved values and body 1's values
//     after PRE and before POST (PublishPad), read-only.
// Q4 (the attacker ids of Dead/Unconscious/Neutralize/Dying for C4 and stun
// arm kills) is the Lua world observer's event=obs_att line (same key, CFG
// r[14]).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mgscoop::partner_placed_census {
namespace k {
// Service chain (contract: 0xBFEFAD service root 0x2C3A8A0, 0xBFF050 = *(root+8),
// place 0x107CA18 gm = *(svc+0x98), iface = *(gm+0x208); iface vtable
// 0x23A9FD0 live (factory 0xFC79DA) and 0x23A9D20 base (dtor 0xFC52DB, which
// clears gm+0x208)).
inline constexpr std::uintptr_t kServiceRootRva = 0x2C3A8A0, kPlacedLiveVtRva = 0x23A9FD0, kPlacedBaseVtRva = 0x23A9D20;
inline constexpr std::size_t kRootService = 0x8, kSvcGm = 0x98, kGmPlaced = 0x208;
// The owner table exactly as the detonation 0xFC54F0 (iface vt+0x68) reads it
// (contract 0xFC557A..0xFC5612): owner index i of u16 iface+0xB0[0..count),
// count u32 iface+0x6C; the owner's slots are first + per*i + j (first u32
// iface+0x68, per u32 iface+0x70), slot flags byte iface+0x90[slot*3] bit 0
// live, slot equip u16 iface+0x18[slot*4], age u16 iface+0xD8[per*i + j].
inline constexpr std::size_t kFirst = 0x68, kOwnerCount = 0x6C, kPerOwner = 0x70, kEquips = 0x18, kEquipStride = 4;
inline constexpr std::size_t kFlags = 0x90, kFlagStride = 3, kOwners = 0xB0, kAge = 0xD8;
inline constexpr std::uint8_t kFlagLive = 1;
// The gun plugin (the prim of partner_player::ReadGun): mode 46 op 1
// (gate 0x107BC5B, offline or local id) sets +0x350 bit 3 and u16 +0x3E8 =
// u16 +0x3D8 (0x107BD64..0x107BD8B), or clears both when +0x3D8 is 0xFFFF.
inline constexpr std::size_t kPrimPlaceFlags = 0x350, kPrimPlaceTarget = 0x3D8, kPrimPlaceLatched = 0x3E8;
inline constexpr std::uint32_t kPrimPlaceBit = 0x8;
// The gun record: the mode's state byte, the placement surface word
// (bits 20..23; the online-remote branch folds 22..23 into 20..21) and the
// slot sub.
inline constexpr std::size_t kRecState = 0x2C2, kRecSurface = 0x3CC, kRecSub = 0x3DC;
// The stun arm (gun mode 52, 0x1085B50) op 0: byte [P+0xB0]+0x2C bit 0 and
// float [P+0xB0]+0x10 > 0.1 (PP+0x10 is in the window's derived block, +0x2C
// in its private masks).
inline constexpr std::size_t kPadMag = 0x10, kPadMask = 0x2C;
// Bounds: a larger table is refused, never read.
inline constexpr std::uint32_t kMaxOwners = 64, kMaxPerOwner = 64;
inline constexpr unsigned kListedOwners = 4, kListedEquips = 4;
} // namespace k

// --- Equipment and mode classes -----------------------------------------------
constexpr bool C4Equip(std::uint16_t equip) noexcept { return equip >= 391 && equip <= 395; }
constexpr bool ArmEquip(std::uint16_t equip) noexcept { return equip >= 515 && equip <= 518; }
constexpr bool PlaceMode(std::uint8_t mode) noexcept { return mode >= 43 && mode <= 47; }
constexpr bool ArmMode(std::uint8_t mode) noexcept { return mode >= 52 && mode <= 56; }
constexpr std::uint8_t GunClass(std::uint32_t word27c) noexcept { return static_cast<std::uint8_t>((word27c >> 6) & 7u); }
constexpr std::uint8_t kTypePlaced = 13, kTypeArm = 14;

// --- Rate budget ------------------------------------------------------------------
// At most per_second lines in any one-second window and cap per mission; the
// first refusal after the cap reports capped once.
struct Budget {
    std::uint64_t window_ms = 0;
    unsigned in_window = 0, total = 0;
    bool capped = false, cap_reported = false;
};
inline bool Take(Budget& b, std::uint64_t now_ms, unsigned per_second, unsigned cap) noexcept {
    if (b.total >= cap) { b.capped = true; return false; }
    if (now_ms < b.window_ms || now_ms - b.window_ms >= 1000) { b.window_ms = now_ms; b.in_window = 0; }
    if (b.in_window >= per_second) return false;
    ++b.in_window;
    ++b.total;
    return true;
}
// True exactly once after the cap was hit (the "capped" line).
inline bool CapReport(Budget& b) noexcept {
    if (!b.capped || b.cap_reported) return false;
    b.cap_reported = true;
    return true;
}

// --- Q1 / Q3: one instance's gun record and plugin latch ---------------------------
struct GunSample {
    bool ok = false;        // the qualified ReadGun and its fields read
    bool extra_ok = false;  // state, sub and surface read
    bool prim_ok = false;   // the plugin latch read
    std::uintptr_t prim = 0, rec = 0;
    std::uint16_t equip = 0;
    std::uint8_t type = 0;  // the equip's packed-descriptor type; 0 unread
    std::uint8_t cls = 0, mode = 0xFF, mode2 = 0xFF, state = 0xFF, slot = 0xFF, hand = 0xFF, sub = 0xFF;
    std::uint32_t w27c = 0, f3c0 = 0, f3cc = 0;
    std::uint32_t p350 = 0;
    std::uint16_t p3d8 = 0xFFFF, p3e8 = 0xFFFF;
};
// The fields the qualified reader does not carry: rec+0x2C2/+0x3DC/+0x3CC and
// the plugin latch. Reads only.
template <class Read>
void ReadGunExtra(Read&& read, GunSample& s) noexcept {
    s.extra_ok = s.rec && read(s.rec + k::kRecState, &s.state, sizeof(s.state)) && read(s.rec + k::kRecSub, &s.sub, sizeof(s.sub)) &&
                 read(s.rec + k::kRecSurface, &s.f3cc, sizeof(s.f3cc));
    s.prim_ok = s.prim && read(s.prim + k::kPrimPlaceFlags, &s.p350, sizeof(s.p350)) &&
                read(s.prim + k::kPrimPlaceTarget, &s.p3d8, sizeof(s.p3d8)) && read(s.prim + k::kPrimPlaceLatched, &s.p3e8, sizeof(s.p3e8));
}
constexpr bool Latched(const GunSample& s) noexcept { return s.prim_ok && (s.p350 & k::kPrimPlaceBit) != 0; }
// C4 or an arm in hand, a place/arm mode, or the plugin latch set.
constexpr bool Interesting(const GunSample& s) noexcept {
    return s.ok && (s.type == kTypePlaced || s.type == kTypeArm || C4Equip(s.equip) || ArmEquip(s.equip) || PlaceMode(s.mode) ||
                    ArmMode(s.mode) || Latched(s));
}
constexpr bool SameKey(const GunSample& a, const GunSample& b) noexcept {
    return a.ok == b.ok && a.prim_ok == b.prim_ok && a.equip == b.equip && a.cls == b.cls && a.mode == b.mode && a.state == b.state &&
           Latched(a) == Latched(b) && a.p3e8 == b.p3e8 && a.p3d8 == b.p3d8;
}
// Log the first interesting sample, then every key change while the previous
// or the new sample is interesting (so the exit from C4/arm is logged too).
constexpr bool LogGun(bool have_prev, const GunSample& prev, const GunSample& cur) noexcept {
    if (!have_prev) return Interesting(cur);
    return !SameKey(prev, cur) && (Interesting(prev) || Interesting(cur));
}
inline std::size_t FormatGun(char* out, std::size_t capacity, unsigned instance, const GunSample& s, int same_prim) noexcept {
    if (!out || !capacity) return 0;
    const int n = std::snprintf(out, capacity,
        "partner_placed_census=gun; k=%u; equip=%u; type=%u; cls=%u; mode=%u; mode2c1=%u; state=%u; slot=%u; hand=%u; sub=%u; "
        "w27c=0x%x; f3c0=0x%x; f3cc=0x%x; surface=%u; p350=0x%x; latched=%u; p3d8=0x%x; p3e8=0x%x; prim_ok=%u; same_prim=%d; "
        "c4=%u; arm=%u\r\n",
        instance, unsigned(s.equip), unsigned(s.type), unsigned(s.cls), unsigned(s.mode), unsigned(s.mode2), unsigned(s.state),
        unsigned(s.slot), unsigned(s.hand), unsigned(s.sub), s.w27c, s.f3c0, s.f3cc, unsigned((s.f3cc >> 20) & 0xFu), s.p350,
        Latched(s) ? 1u : 0u, unsigned(s.p3d8), unsigned(s.p3e8), s.prim_ok ? 1u : 0u, same_prim, C4Equip(s.equip) ? 1u : 0u,
        ArmEquip(s.equip) ? 1u : 0u);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) { out[0] = 0; return 0; }
    return static_cast<std::size_t>(n);
}

// --- Q2: the placed-object service's owner table -------------------------------------
struct OwnerRow {
    std::uint16_t owner = 0xFFFF;
    std::uint8_t live = 0;  // live slots (flags bit 0)
    std::uint8_t listed = 0;
    std::uint16_t equips[k::kListedEquips]{};
};
struct Service {
    // nullptr: read. Else root | svc | gm | iface (no service yet) | dying
    // (the base vtable: being destroyed) | vtable | fields | count | per |
    // owners | slots.
    const char* refusal = "unread";
    std::uintptr_t iface = 0;
    std::uint32_t first = 0, count = 0, per = 0;
    int index0 = -1, index1 = -1;  // owner 0 / owner 1 index in the table
    std::uint8_t live0 = 0, live1 = 0;
    std::uint32_t live_total = 0;
    unsigned rows = 0;  // listed (at most kListedOwners)
    OwnerRow row[k::kListedOwners]{};
    std::uint32_t digest = 0;  // FNV-1a of everything above (change detection)
};
namespace detail {
inline void Fnv(std::uint32_t& h, const void* data, std::size_t size) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 16777619u; }
}
} // namespace detail
template <class Read>
Service ReadService(Read&& read, std::uintptr_t base) noexcept {
    Service s{};
    std::uintptr_t root = 0, svc = 0, gm = 0, iface = 0, vt = 0, owners = 0, flags = 0, equips = 0;
    const auto done = [&](const char* why) noexcept {
        s.refusal = why;
        std::uint32_t h = 2166136261u;
        const unsigned char tag = why ? static_cast<unsigned char>(why[0]) : 0;
        detail::Fnv(h, &tag, 1);
        detail::Fnv(h, &s.count, sizeof(s.count));
        detail::Fnv(h, &s.per, sizeof(s.per));
        detail::Fnv(h, &s.first, sizeof(s.first));
        detail::Fnv(h, &s.index0, sizeof(s.index0));
        detail::Fnv(h, &s.index1, sizeof(s.index1));
        detail::Fnv(h, &s.live0, sizeof(s.live0));
        detail::Fnv(h, &s.live1, sizeof(s.live1));
        detail::Fnv(h, &s.live_total, sizeof(s.live_total));
        for (unsigned i = 0; i < s.rows; ++i) detail::Fnv(h, &s.row[i], sizeof(s.row[i]));
        s.digest = h;
        return s;
    };
    if (!base || !read(base + k::kServiceRootRva, &root, sizeof(root)) || !root) return done("root");
    if (!read(root + k::kRootService, &svc, sizeof(svc)) || !svc) return done("svc");
    if (!read(svc + k::kSvcGm, &gm, sizeof(gm)) || !gm) return done("gm");
    if (!read(gm + k::kGmPlaced, &iface, sizeof(iface)) || !iface) return done("iface");
    if (!read(iface, &vt, sizeof(vt))) return done("iface");
    s.iface = iface;
    if (vt == base + k::kPlacedBaseVtRva) return done("dying");
    if (vt != base + k::kPlacedLiveVtRva) return done("vtable");
    if (!read(iface + k::kFirst, &s.first, sizeof(s.first)) || !read(iface + k::kOwnerCount, &s.count, sizeof(s.count)) ||
        !read(iface + k::kPerOwner, &s.per, sizeof(s.per)) || !read(iface + k::kOwners, &owners, sizeof(owners)) ||
        !read(iface + k::kFlags, &flags, sizeof(flags)) || !read(iface + k::kEquips, &equips, sizeof(equips)))
        return done("fields");
    if (s.count > k::kMaxOwners) return done("count");
    if (s.per > k::kMaxPerOwner) return done("per");
    std::uint16_t ids[k::kMaxOwners]{};
    if (s.count && (!owners || !read(owners, ids, s.count * sizeof(ids[0])))) return done("owners");
    if (s.count && s.per && (!flags || !equips)) return done("slots");
    for (std::uint32_t i = 0; i < s.count; ++i) {
        const std::uint64_t slot0 = std::uint64_t(s.first) + std::uint64_t(s.per) * i;
        unsigned char f[k::kMaxPerOwner * k::kFlagStride]{};
        std::uint16_t e[k::kMaxPerOwner * (k::kEquipStride / 2)]{};
        if (s.per && (!read(flags + static_cast<std::uintptr_t>(slot0 * k::kFlagStride), f, s.per * k::kFlagStride) ||
                      !read(equips + static_cast<std::uintptr_t>(slot0 * k::kEquipStride), e, s.per * k::kEquipStride)))
            return done("slots");
        OwnerRow r{};
        r.owner = ids[i];
        for (std::uint32_t j = 0; j < s.per; ++j) {
            if (!(f[j * k::kFlagStride] & k::kFlagLive)) continue;
            ++r.live;
            if (r.listed < k::kListedEquips) r.equips[r.listed++] = e[j * (k::kEquipStride / 2)];
        }
        s.live_total += r.live;
        if (ids[i] == 0 && s.index0 < 0) { s.index0 = static_cast<int>(i); s.live0 = r.live; }
        if (ids[i] == 1 && s.index1 < 0) { s.index1 = static_cast<int>(i); s.live1 = r.live; }
        if (s.rows < k::kListedOwners) s.row[s.rows++] = r;
    }
    return done(nullptr);
}
inline std::size_t FormatService(char* out, std::size_t capacity, const Service& s) noexcept {
    if (!out || !capacity) return 0;
    char rows[160]{};
    std::size_t at = 0;
    for (unsigned i = 0; i < s.rows && at < sizeof(rows); ++i) {
        const OwnerRow& r = s.row[i];
        int n = std::snprintf(rows + at, sizeof(rows) - at, "%s%u:%u", i ? "," : "", unsigned(r.owner), unsigned(r.live));
        if (n < 0 || static_cast<std::size_t>(n) >= sizeof(rows) - at) break;
        at += static_cast<std::size_t>(n);
        for (unsigned j = 0; j < r.listed; ++j) {
            n = std::snprintf(rows + at, sizeof(rows) - at, "%s%u", j ? "/" : "[", unsigned(r.equips[j]));
            if (n < 0 || static_cast<std::size_t>(n) >= sizeof(rows) - at) break;
            at += static_cast<std::size_t>(n);
        }
        if (r.listed && at + 1 < sizeof(rows)) { rows[at++] = ']'; rows[at] = 0; }
    }
    const int n = std::snprintf(out, capacity,
        "partner_placed_census=service; reason=%s; count=%u; per=%u; first=%u; owner0=%d; owner1=%d; live0=%u; live1=%u; "
        "live_total=%u; owners=%s\r\n",
        s.refusal ? s.refusal : "ok", s.count, s.per, s.first, s.index0, s.index1, unsigned(s.live0), unsigned(s.live1), s.live_total,
        s.rows ? rows : "none");
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) { out[0] = 0; return 0; }
    return static_cast<std::size_t>(n);
}

// --- Q5: the PlayerPad fields the stun arm's op 0 reads, around body 1's job ---------
struct PadSample {
    std::uint32_t windows = 0;  // windows sampled (process total)
    bool pre_ok = false, post_ok = false;
    float viewer_mag = 0.0f;           // the window's saved host PP+0x10
    std::uint32_t viewer_mask = 0;     // the window's saved host PP+0x2C
    float body_mag_pre = 0.0f, body_mag_post = 0.0f;
    std::uint32_t body_mask_pre = 0, body_mask_post = 0;
    std::uint8_t mode_pre = 0xFF, mode_post = 0xFF;  // body 1's rec+0x2C0 (0xFF unread)
};
// Body 1's job would see different values from the viewer's live pad at
// these two fields (bit-exact float, mask bit 0).
inline bool PadDiffers(const PadSample& s) noexcept {
    if (!s.pre_ok) return false;
    std::uint32_t a = 0, b = 0;
    std::memcpy(&a, &s.viewer_mag, sizeof(a));
    std::memcpy(&b, &s.body_mag_pre, sizeof(b));
    return a != b || ((s.viewer_mask ^ s.body_mask_pre) & 1u) != 0;
}
constexpr bool PadArm(const PadSample& s) noexcept { return ArmMode(s.mode_pre) || ArmMode(s.mode_post); }
// Log a new sample while body 1 is in an arm mode, or when the differs/arm
// verdict changes (the caller rate-limits).
inline bool LogPad(bool have_prev, const PadSample& prev, const PadSample& cur) noexcept {
    if (cur.windows == prev.windows && have_prev) return false;
    if (PadArm(cur)) return true;
    return !have_prev || PadDiffers(prev) != PadDiffers(cur) || PadArm(prev) != PadArm(cur);
}
inline std::size_t FormatPad(char* out, std::size_t capacity, const PadSample& s) noexcept {
    if (!out || !capacity) return 0;
    const int n = std::snprintf(out, capacity,
        "partner_placed_census=pad; windows=%u; pre_ok=%u; post_ok=%u; viewer_mag=%.3f; viewer_mask=0x%x; body_mag=%.3f; "
        "body_mask=0x%x; body_mag_post=%.3f; body_mask_post=0x%x; mode=%u; mode_post=%u; differs=%u; arm=%u\r\n",
        s.windows, s.pre_ok ? 1u : 0u, s.post_ok ? 1u : 0u, double(s.viewer_mag), s.viewer_mask, double(s.body_mag_pre),
        s.body_mask_pre, double(s.body_mag_post), s.body_mask_post, unsigned(s.mode_pre), unsigned(s.mode_post),
        PadDiffers(s) ? 1u : 0u, PadArm(s) ? 1u : 0u);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) { out[0] = 0; return 0; }
    return static_cast<std::size_t>(n);
}
// The hook's publication (one writer: the hook thread; readers: the runtime
// thread). A seqlock: ReadPad returns false while a write is in flight or
// before the first sample. SetPadCensus arms the hook's sampling (the
// observer sets it from the lab key every tick).
void SetPadCensus(bool on) noexcept;
bool PadCensusOn() noexcept;
void PublishPad(const PadSample& sample) noexcept;
bool ReadPad(PadSample& out) noexcept;
namespace detail {
void ResetPadForTest() noexcept;
}

} // namespace mgscoop::partner_placed_census
