#pragma once
// WS4 v1 (docs/anim-round2-throws.md "WS4 v1"): the read-only safety checks for
// body 1's placed C4 and arm holds. Pure: template readers over
//   Read = bool(std::uintptr_t address, void* out, std::size_t size) noexcept
// (read-only and fault-safe; the input hook passes its __try reader, the tests
// a bounds-checked fake), plus the before/after verdicts. Nothing here writes
// game memory or calls game code. Instead of the planned mode-46 code patch
// (P3, dropped after the census), every body-1 job bracket compares:
//  - placed: the VIEWER's (instance 0's) gun plugin placement latch (+0x350
//    bit 3, u16 +0x3D8, u16 +0x3E8; mode 46 op 1 0x107BD64..0x107BD8B writes it
//    on the plugin of the record's own instance, same_prim=0) and the placed
//    service owner table (partner_placed_census::ReadService: owner rows, live
//    counts, equips). A change inside body 1's job latches native support off
//    for the mission (partner_support=refused; reason=placed_shared_write).
//  - arm: the viewer's arm energy, the state ctl vt+0xE8 (0x11AF820 ->
//    0x11AF5E0, no instance id) writes: the float Y+0x1C[i], the slot-6 store
//    word u16 [[Y+0x98] + 2*5] (the count ctl vt+0xC0 sets with k = 5), the
//    u32 ctl+0x58[i] and the u32 vars+0x670 sum. A change inside body 1's job
//    latches arms off for the mission, as does body 1 ever entering an arm
//    use mode 52..56 (the watchdog).
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_placed_census.hpp"
#include "mgscoop/partner_support_policy.hpp"

namespace mgscoop::partner_placed_policy {
namespace k {
// The gun prim (its vtable, prim+0x8 = block, prim+0x58 = record) as in
// partner_support_policy::rec240; the latch fields as in the census.
inline constexpr std::uintptr_t kPrimVtRva = partner_support_policy::rec240::kPrimVtRva;
inline constexpr std::size_t kPrimBlk = partner_support_policy::rec240::kPrimBlk, kPrimRec = partner_support_policy::rec240::kPrimRec;
inline constexpr std::size_t kPrimPlaceFlags = partner_placed_census::k::kPrimPlaceFlags;
inline constexpr std::size_t kPrimPlaceTarget = partner_placed_census::k::kPrimPlaceTarget;
inline constexpr std::size_t kPrimPlaceLatched = partner_placed_census::k::kPrimPlaceLatched;
inline constexpr std::uint32_t kPrimPlaceBit = partner_placed_census::k::kPrimPlaceBit;
// The arm energy chain (contract: P+0xC8 = ctl, ctl vtable 0x23C1720 with
// vt+0xE8 = 0x11AF820, ctl+0x18 = P, ctl+0x8 = Y with the store vtable
// 0x23894B0 or 0x23895D0, ctl+0x20 = vars = *(vars_slot_rva)). 0x11AF5E0:
// i = 0 for 0x203..0x206 (515..518), 2 / 1 / 3 for 0x1EB / 0x1EC / 0x1ED, other
// equips return before any write; movss [Y+0x1C+4i] (0x11AF671), ctl vt+0xC0
// (the stock setter: [[Y+0x98]+2k] through store vt+0x78 0xEE9730 and
// vars+0x614+2k), [ctl+0x58+4i] (0x11AF790) and vars+0x670 = the sum of the
// four (0x11AF7AA). Store vt+0x88 0xEE9050 reads u16 [[Y+0x98]+2k].
inline constexpr std::size_t kPEquip = 0xC8, kCtlStore = 0x8, kCtlPlayer = 0x18, kCtlVars = 0x20, kCtlFrac = 0x58;
inline constexpr std::uintptr_t kCtlVtRva = 0x23C1720, kStoreVtRva = 0x23894B0, kStoreVtDerivedRva = 0x23895D0, kVarsSlotRva = 0x2C16920;
inline constexpr std::size_t kStoreEnergy = 0x1C, kStoreCounts = 0x98, kVarsEnergySum = 0x670;
inline constexpr unsigned kEnergySlots = 4, kArmWord = 5;
} // namespace k

// --- Placed: the viewer's latch and the owner table ------------------------------------
struct ViewerLatch {
    bool ok = false;
    std::uint32_t p350 = 0;
    std::uint16_t target = 0xFFFF, latched = 0xFFFF;  // u16 +0x3D8, u16 +0x3E8
};
constexpr bool LatchBit(const ViewerLatch& v) noexcept { return (v.p350 & k::kPrimPlaceBit) != 0; }
// Only bit 3 of +0x350 is the placement latch: the viewer's own activation
// toggles bit 4 (0x105FDB0, local record only) and its rocket arm bits 7/9.
constexpr bool SameLatch(const ViewerLatch& a, const ViewerLatch& b) noexcept {
    return LatchBit(a) == LatchBit(b) && a.target == b.target && a.latched == b.latched;
}
// The viewer's gun plugin, re-validated: its vtable, prim+0x58 == the viewer's
// record and never body 1's block (prim+0x8 != blk1) or prim.
template <class Read>
ViewerLatch ReadViewerLatch(Read&& read, std::uintptr_t base, std::uintptr_t prim, std::uintptr_t rec, std::uintptr_t blk1,
                            std::uintptr_t body_prim) noexcept {
    ViewerLatch v{};
    std::uintptr_t vt = 0, pblk = 0, prec = 0;
    if (!base || !prim || !rec || prim == body_prim || !read(prim, &vt, sizeof(vt)) || vt != base + k::kPrimVtRva ||
        !read(prim + k::kPrimRec, &prec, sizeof(prec)) || prec != rec || !read(prim + k::kPrimBlk, &pblk, sizeof(pblk)) || !pblk ||
        pblk == blk1)
        return v;
    if (!read(prim + k::kPrimPlaceFlags, &v.p350, sizeof(v.p350)) || !read(prim + k::kPrimPlaceTarget, &v.target, sizeof(v.target)) ||
        !read(prim + k::kPrimPlaceLatched, &v.latched, sizeof(v.latched)))
        return ViewerLatch{};
    v.ok = true;
    return v;
}
struct PlacedSnap {
    ViewerLatch latch{};
    bool svc_ok = false;
    std::uint32_t svc_digest = 0;  // partner_placed_census::Service::digest (owner rows, live counts, equips)
    std::uint32_t count = 0;
    int index0 = -1, index1 = -1;
    std::uint8_t live0 = 0, live1 = 0;
};
template <class Read>
PlacedSnap ReadPlaced(Read&& read, std::uintptr_t base, std::uintptr_t viewer_prim, std::uintptr_t viewer_rec, std::uintptr_t blk1,
                      std::uintptr_t body_prim) noexcept {
    PlacedSnap s{};
    s.latch = ReadViewerLatch(read, base, viewer_prim, viewer_rec, blk1, body_prim);
    const partner_placed_census::Service svc = partner_placed_census::ReadService(read, base);
    if (!svc.refusal) {
        s.svc_ok = true;
        s.svc_digest = svc.digest;
        s.count = svc.count;
        s.index0 = svc.index0;
        s.index1 = svc.index1;
        s.live0 = svc.live0;
        s.live1 = svc.live1;
    }
    return s;
}
// same: nothing moved; unread: a side was unreadable (never latches, never
// admits a new C4 input); latch / service: the viewer's latch or the owner
// table changed inside body 1's job (fail closed: native support off).
enum class Placed : std::uint8_t { same, unread, latch, service };
constexpr const char* PlacedName(Placed v) noexcept {
    return v == Placed::unread ? "unread" : v == Placed::latch ? "latch" : v == Placed::service ? "service" : "same";
}
constexpr Placed ComparePlaced(const PlacedSnap& before, const PlacedSnap& after) noexcept {
    if (before.latch.ok && after.latch.ok && !SameLatch(before.latch, after.latch)) return Placed::latch;
    if (before.svc_ok && after.svc_ok && before.svc_digest != after.svc_digest) return Placed::service;
    if (!before.latch.ok || !after.latch.ok || !before.svc_ok || !after.svc_ok) return Placed::unread;
    return Placed::same;
}
constexpr bool PlacedLatches(Placed v) noexcept { return v == Placed::latch || v == Placed::service; }
// The bracket compares when body 1 was in the place family before or after its job.
constexpr bool PlacedBracket(std::uint8_t mode_before, std::uint8_t mode_after) noexcept {
    return partner_support_policy::PlaceMode(mode_before) || partner_support_policy::PlaceMode(mode_after);
}

// --- Arms: the viewer's energy and the use-mode watchdog -------------------------------
struct ArmEnergy {
    bool ok = false;
    std::uint16_t word = 0;                     // u16 [[Y+0x98] + 2*5]
    std::uint32_t energy[k::kEnergySlots]{};    // float bits Y+0x1C[i]
    std::uint32_t frac[k::kEnergySlots]{};      // u32 ctl+0x58[i]
    std::uint32_t vars_sum = 0;                 // u32 vars+0x670
};
template <class Read>
ArmEnergy ReadArmEnergy(Read&& read, std::uintptr_t base, std::uintptr_t P) noexcept {
    ArmEnergy e{};
    std::uintptr_t ctl = 0, vt = 0, player = 0, store = 0, svt = 0, counts = 0, vars = 0, vars_slot = 0;
    if (!base || !P || !read(P + k::kPEquip, &ctl, sizeof(ctl)) || !ctl || !read(ctl, &vt, sizeof(vt)) || vt != base + k::kCtlVtRva ||
        !read(ctl + k::kCtlPlayer, &player, sizeof(player)) || player != P || !read(ctl + k::kCtlStore, &store, sizeof(store)) || !store ||
        !read(store, &svt, sizeof(svt)) || (svt != base + k::kStoreVtRva && svt != base + k::kStoreVtDerivedRva) ||
        !read(ctl + k::kCtlVars, &vars, sizeof(vars)) || !vars || !read(base + k::kVarsSlotRva, &vars_slot, sizeof(vars_slot)) ||
        vars_slot != vars || !read(store + k::kStoreCounts, &counts, sizeof(counts)) || !counts)
        return e;
    if (!read(counts + 2 * k::kArmWord, &e.word, sizeof(e.word)) || !read(store + k::kStoreEnergy, e.energy, sizeof(e.energy)) ||
        !read(ctl + k::kCtlFrac, e.frac, sizeof(e.frac)) || !read(vars + k::kVarsEnergySum, &e.vars_sum, sizeof(e.vars_sum)))
        return ArmEnergy{};
    e.ok = true;
    return e;
}
constexpr bool SameEnergy(const ArmEnergy& a, const ArmEnergy& b) noexcept {
    if (a.word != b.word || a.vars_sum != b.vars_sum) return false;
    for (unsigned i = 0; i < k::kEnergySlots; ++i)
        if (a.energy[i] != b.energy[i] || a.frac[i] != b.frac[i]) return false;
    return true;
}
enum class Energy : std::uint8_t { same, unread, changed };
constexpr const char* EnergyName(Energy v) noexcept { return v == Energy::unread ? "unread" : v == Energy::changed ? "changed" : "same"; }
constexpr Energy CompareEnergy(const ArmEnergy& before, const ArmEnergy& after) noexcept {
    return !before.ok || !after.ok ? Energy::unread : SameEnergy(before, after) ? Energy::same : Energy::changed;
}
// FNV-1a over the energy fields (0 unread), for the lines.
constexpr std::uint32_t EnergyDigest(const ArmEnergy& e) noexcept {
    if (!e.ok) return 0;
    std::uint32_t h = 2166136261u;
    const auto mix = [&h](std::uint32_t v) {
        for (unsigned b = 0; b < 4; ++b) { h = (h ^ ((v >> (8 * b)) & 0xFFu)) * 16777619u; }
    };
    mix(e.word);
    for (unsigned i = 0; i < k::kEnergySlots; ++i) { mix(e.energy[i]); mix(e.frac[i]); }
    mix(e.vars_sum);
    return h ? h : 1u;
}
// The watchdog (read-only): body 1's rec+0x2C0 in an arm use mode latches arms
// off for the mission, once (true on the latching step).
struct ArmWatch {
    bool latched = false;
    std::uint8_t mode = 0xFF;
};
constexpr bool StepWatch(ArmWatch& w, std::uint8_t mode) noexcept {
    if (w.latched || !partner_support_policy::ArmUseMode(mode)) return false;
    w.latched = true;
    w.mode = mode;
    return true;
}

} // namespace mgscoop::partner_placed_policy
