// WS4 v1 (docs/anim-round2-throws.md "WS4 v1"): the read-only placed / arm safety checks
// (partner_placed_policy.hpp) on fake memory, the watchdog, and the contract pins they rely on.
// Synthetic only: a byte map stands in for the game; nothing here runs or reads game code.
#include "mgscoop/partner_placed_policy.hpp"
#include "mgscoop/partner_place_mirror.hpp"
#include "mgscoop/partner_support_policy.hpp"
#include "partner_player_contract.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>

namespace pp = mgscoop::partner_placed_policy;
namespace pk = mgscoop::partner_placed_policy::k;
namespace ck = mgscoop::partner_placed_census::k;
namespace sp = mgscoop::partner_support_policy;
namespace c = mgscoop::partner_player_contract;

// The pure header's offsets are the generated contract's (the hook static_asserts the same).
static_assert(pk::kPrimVtRva == c::gun_prim_vt_rva && pk::kPrimBlk == c::prim_blk && pk::kPrimRec == c::prim_gun_rec, "gun prim chain");
static_assert(pk::kPrimPlaceFlags == c::prim_place_flags && pk::kPrimPlaceTarget == c::prim_place_target &&
                  pk::kPrimPlaceLatched == c::prim_place_latched && pk::kPrimPlaceBit == c::prim_place_bit,
              "the mode-46 op-1 latch (pinned store block 0x107BD64)");
static_assert(pk::kPEquip == c::p_equip && pk::kCtlVtRva == c::equip_ctl_vt_rva && pk::kCtlStore == c::ctl_inventory &&
                  pk::kCtlPlayer == c::ctl_player && pk::kCtlVars == c::ctl_vars && pk::kCtlFrac == c::ctl_arm_frac,
              "the equip ctl chain");
static_assert(pk::kStoreVtRva == c::store_vt_rva && pk::kStoreVtDerivedRva == c::store_vt_derived_rva &&
                  pk::kStoreEnergy == c::store_arm_energy && pk::kStoreCounts == c::store_counts && pk::kVarsSlotRva == c::vars_slot_rva &&
                  pk::kVarsEnergySum == c::vars_arm_energy_sum && pk::kArmWord == c::arm_energy_k && pk::kEnergySlots == c::arm_energy_slots,
              "the arm energy fields 0x11AF5E0 writes");
static_assert(c::arm_equip_first == 515 && c::arm_equip_last == 521, "ctl vt+0x1F0's arm range is ArmEquip's (not the lane)");
static_assert(mgscoop::partner_support_policy::ArmEquip(c::arm_equip_first) && mgscoop::partner_support_policy::ArmEquip(c::arm_equip_last) &&
                  mgscoop::partner_support_policy::ArmLaneEquip(516) && !mgscoop::partner_support_policy::ArmLaneEquip(c::arm_equip_first),
              "WS4-1: the arm lane is 516 inside ctl vt+0x1F0's range; the stun arm 515 is not in it");
static_assert(c::arm_equip_rva == 0x11B1B60 && c::ctl_vt_arm_equip == 0x1F0 && c::arm_energy_rva == 0x11AF820 &&
                  c::ctl_vt_arm_energy == 0xE8 && c::store_count_read_rva == 0xEE9050 && c::store_vt_count == 0x88,
              "the WS4 v1 call/read sites");

namespace {
unsigned checks = 0;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "failed: %s\n", label);
        std::exit(1);
    }
}
bool Pinned(std::uintptr_t rva) {
    for (const auto& s : c::code_sites)
        if (s.rva == rva) return true;
    return false;
}
bool Slot(std::uintptr_t vt, std::size_t off, std::uintptr_t target) {
    for (const auto& s : c::vtable_slots)
        if (s.vtable_rva == vt && s.offset == off && s.target_rva == target) return true;
    return false;
}

struct Fake {
    std::map<std::uintptr_t, unsigned char> bytes;
    template <class T>
    void Put(std::uintptr_t at, T value) {
        unsigned char raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes[at + i] = raw[i];
    }
    void Erase(std::uintptr_t at, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) bytes.erase(at + i);
    }
    bool Read(std::uintptr_t at, void* out, std::size_t size) const {
        auto* o = static_cast<unsigned char*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = bytes.find(at + i);
            if (it == bytes.end()) return false;
            o[i] = it->second;
        }
        return true;
    }
    auto Reader() const {
        return [this](std::uintptr_t at, void* out, std::size_t size) noexcept { return Read(at, out, size); };
    }
};

constexpr std::uintptr_t kBase = 0x140000000ull;
// The viewer's gun plugin/record and body 1's.
constexpr std::uintptr_t kPrim0 = 0x7FF000100000ull, kRec0 = 0x7FF000110000ull, kBlk0 = 0x7FF000120000ull;
constexpr std::uintptr_t kPrim1 = 0x7FF000200000ull, kRec1 = 0x7FF000210000ull, kBlk1 = 0x7FF000220000ull;
// The placed service.
constexpr std::uintptr_t kRoot = 0x7FF000300000ull, kSvc = 0x7FF000310000ull, kGm = 0x7FF000320000ull, kIface = 0x7FF000330000ull;
constexpr std::uintptr_t kOwners = 0x7FF000340000ull, kFlags = 0x7FF000350000ull, kEquips = 0x7FF000360000ull;
// The player system, equip ctl, store and vars.
constexpr std::uintptr_t kP = 0x7FF000400000ull, kCtl = 0x7FF000410000ull, kStore = 0x7FF000420000ull, kCounts = 0x7FF000430000ull;
constexpr std::uintptr_t kVars = 0x7FF000440000ull;

void Prim(Fake& m, std::uintptr_t prim, std::uintptr_t rec, std::uintptr_t blk, std::uint32_t p350, std::uint16_t target, std::uint16_t latched) {
    m.Put<std::uintptr_t>(prim, kBase + pk::kPrimVtRva);
    m.Put<std::uintptr_t>(prim + pk::kPrimBlk, blk);
    m.Put<std::uintptr_t>(prim + pk::kPrimRec, rec);
    m.Put<std::uint32_t>(prim + pk::kPrimPlaceFlags, p350);
    m.Put<std::uint16_t>(prim + pk::kPrimPlaceTarget, target);
    m.Put<std::uint16_t>(prim + pk::kPrimPlaceLatched, latched);
}
// One owner row (the lab census: count 1, per 32, first 200), owner `owner`, `live` C4 395 charges.
void Service(Fake& m, std::uint16_t owner, unsigned live) {
    m.Put<std::uintptr_t>(kBase + ck::kServiceRootRva, kRoot);
    m.Put<std::uintptr_t>(kRoot + ck::kRootService, kSvc);
    m.Put<std::uintptr_t>(kSvc + ck::kSvcGm, kGm);
    m.Put<std::uintptr_t>(kGm + ck::kGmPlaced, kIface);
    m.Put<std::uintptr_t>(kIface, kBase + ck::kPlacedLiveVtRva);
    m.Put<std::uint32_t>(kIface + ck::kFirst, 200);
    m.Put<std::uint32_t>(kIface + ck::kOwnerCount, 1);
    m.Put<std::uint32_t>(kIface + ck::kPerOwner, 32);
    m.Put<std::uintptr_t>(kIface + ck::kOwners, kOwners);
    m.Put<std::uintptr_t>(kIface + ck::kFlags, kFlags);
    m.Put<std::uintptr_t>(kIface + ck::kEquips, kEquips);
    m.Put<std::uint16_t>(kOwners, owner);
    for (std::uint32_t s = 200; s < 232; ++s) {
        for (std::size_t b = 0; b < ck::kFlagStride; ++b) m.Put<std::uint8_t>(kFlags + s * ck::kFlagStride + b, 0);
        m.Put<std::uint32_t>(kEquips + s * ck::kEquipStride, 0);
    }
    for (unsigned i = 0; i < live; ++i) {
        m.Put<std::uint8_t>(kFlags + (200 + i) * ck::kFlagStride, ck::kFlagLive);
        m.Put<std::uint16_t>(kEquips + (200 + i) * ck::kEquipStride, 395);
    }
}
Fake Placed() {
    Fake m;
    Prim(m, kPrim0, kRec0, kBlk0, 0x10, 0xFFFF, 0xFFFF);  // the census: p350=0x10 (bit 4), latch clear
    Prim(m, kPrim1, kRec1, kBlk1, 0x0, 0xFFFF, 0xFFFF);
    Service(m, 0xFFFF, 0);                               // id 65535 until the first place
    return m;
}
pp::PlacedSnap Snap(const Fake& m) { return pp::ReadPlaced(m.Reader(), kBase, kPrim0, kRec0, kBlk1, kPrim1); }

void ViewerLatch() {
    Fake m = Placed();
    const auto r = m.Reader();
    pp::ViewerLatch v = pp::ReadViewerLatch(r, kBase, kPrim0, kRec0, kBlk1, kPrim1);
    Check(v.ok && v.p350 == 0x10 && !pp::LatchBit(v) && v.target == 0xFFFF && v.latched == 0xFFFF, "latch: the viewer's census values");
    Check(!pp::ReadViewerLatch(r, kBase, kPrim1, kRec1, kBlk1, kPrim1).ok, "latch: body 1's own prim is refused");
    Check(!pp::ReadViewerLatch(r, kBase, kPrim1, kRec1, kBlk1, 0).ok, "latch: a prim on body 1's block is refused");
    Check(!pp::ReadViewerLatch(r, kBase, kPrim0, kRec1, kBlk1, kPrim1).ok, "latch: prim+0x58 must be the viewer's record");
    Check(!pp::ReadViewerLatch(r, kBase, 0, kRec0, kBlk1, kPrim1).ok && !pp::ReadViewerLatch(r, 0, kPrim0, kRec0, kBlk1, kPrim1).ok,
          "latch: no prim or base");
    m.Put<std::uintptr_t>(kPrim0, kBase + 0x234B840);
    Check(!pp::ReadViewerLatch(m.Reader(), kBase, kPrim0, kRec0, kBlk1, kPrim1).ok, "latch: a foreign vtable is refused");
    m = Placed();
    m.Erase(kPrim0 + pk::kPrimPlaceLatched, 2);
    Check(!pp::ReadViewerLatch(m.Reader(), kBase, kPrim0, kRec0, kBlk1, kPrim1).ok, "latch: an unreadable field is unread");
    // Only bit 3 of +0x350 and the two words count: the viewer's own bit 4 (activation) and bits 7/9 (rocket) do not.
    pp::ViewerLatch a{}, b{};
    a.ok = b.ok = true;
    a.p350 = 0x10;
    b.p350 = 0x290;
    Check(pp::SameLatch(a, b), "latch: bits 4/7/9 are not the placement latch");
    b.p350 = 0x18;
    Check(!pp::SameLatch(a, b), "latch: bit 3 is");
    b = a;
    b.target = 0x8801;
    Check(!pp::SameLatch(a, b), "latch: +0x3D8 is");
    b = a;
    b.latched = 0x8801;
    Check(!pp::SameLatch(a, b), "latch: +0x3E8 is");
}

void PlacedCompare() {
    Fake m = Placed();
    const pp::PlacedSnap before = Snap(m);
    Check(before.latch.ok && before.svc_ok && before.count == 1 && before.index0 == -1 && before.index1 == -1, "placed: readable, owner 65535");
    Check(pp::ComparePlaced(before, Snap(m)) == pp::Placed::same, "placed: nothing moved");
    // Body 1's job places nothing for the viewer: its own prim's latch moving is not the viewer's.
    Prim(m, kPrim1, kRec1, kBlk1, 0x8, 0x8801, 0x8801);
    Check(pp::ComparePlaced(before, Snap(m)) == pp::Placed::same, "placed: body 1's own plugin latch (same_prim=0) is not checked");
    // The viewer's latch moving inside body 1's job: latch (fail closed).
    m = Placed();
    Prim(m, kPrim0, kRec0, kBlk0, 0x18, 0x8801, 0x8801);
    pp::Placed v = pp::ComparePlaced(before, Snap(m));
    Check(v == pp::Placed::latch && pp::PlacedLatches(v) && !std::strcmp(pp::PlacedName(v), "latch"), "placed: the viewer's latch moved");
    m = Placed();
    Prim(m, kPrim0, kRec0, kBlk0, 0x10, 0x1234, 0xFFFF);
    Check(pp::ComparePlaced(before, Snap(m)) == pp::Placed::latch, "placed: the viewer's +0x3D8 moved");
    // The owner table: a charge registered, an owner row for id 1, a charge gone.
    m = Placed();
    Service(m, 0, 1);
    v = pp::ComparePlaced(before, Snap(m));
    Check(v == pp::Placed::service && pp::PlacedLatches(v), "placed: owner 0 gained a live C4 inside body 1's job");
    {
        Fake a = Placed();
        Service(a, 0, 2);
        const auto two = Snap(a);
        Service(a, 0, 1);
        Check(pp::ComparePlaced(two, Snap(a)) == pp::Placed::service && two.live0 == 2 && two.index0 == 0, "placed: owner 0 lost a charge");
        Service(a, 1, 1);
        const auto one = Snap(a);
        Check(one.index1 == 0 && one.live1 == 1 && pp::ComparePlaced(two, one) == pp::Placed::service, "placed: an owner-1 row appeared");
    }
    // Unread on either side never latches.
    m = Placed();
    m.Erase(kIface, 8);
    v = pp::ComparePlaced(before, Snap(m));
    Check(v == pp::Placed::unread && !pp::PlacedLatches(v), "placed: an unreadable service after the job is unread");
    m = Placed();
    m.Erase(kPrim0, 8);
    Check(pp::ComparePlaced(before, Snap(m)) == pp::Placed::unread, "placed: an unreadable viewer latch is unread");
    // A latch move still wins over an unread service (it is evidence).
    m = Placed();
    Prim(m, kPrim0, kRec0, kBlk0, 0x18, 0xFFFF, 0xFFFF);
    m.Erase(kIface, 8);
    Check(pp::ComparePlaced(before, Snap(m)) == pp::Placed::latch, "placed: a latch move with the service unread still latches");
    // The bracket: body 1 in the place family before or after its job.
    for (unsigned b = 0; b < 64; ++b)
        for (unsigned a = 0; a < 64; ++a)
            Check(pp::PlacedBracket(static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)) ==
                      ((b >= 43 && b <= 47) || (a >= 43 && a <= 47)),
                  "placed: the bracket is the place family 43..47");
    Check(!pp::PlacedBracket(0xFF, 0xFF), "placed: unread modes are no bracket");
}

void Arms(Fake& m, float energy, std::uint16_t word, std::uint32_t frac, std::uint32_t sum) {
    m.Put<std::uintptr_t>(kP + pk::kPEquip, kCtl);
    m.Put<std::uintptr_t>(kCtl, kBase + pk::kCtlVtRva);
    m.Put<std::uintptr_t>(kCtl + pk::kCtlPlayer, kP);
    m.Put<std::uintptr_t>(kCtl + pk::kCtlStore, kStore);
    m.Put<std::uintptr_t>(kCtl + pk::kCtlVars, kVars);
    m.Put<std::uintptr_t>(kBase + pk::kVarsSlotRva, kVars);
    m.Put<std::uintptr_t>(kStore, kBase + pk::kStoreVtDerivedRva);
    m.Put<std::uintptr_t>(kStore + pk::kStoreCounts, kCounts);
    for (unsigned k = 0; k < 8; ++k) m.Put<std::uint16_t>(kCounts + 2 * k, static_cast<std::uint16_t>(k == pk::kArmWord ? word : 12));
    for (unsigned i = 0; i < pk::kEnergySlots; ++i) {
        m.Put<float>(kStore + pk::kStoreEnergy + 4 * i, i == 0 ? energy : 100.0f);
        m.Put<std::uint32_t>(kCtl + pk::kCtlFrac + 4 * i, i == 0 ? frac : 0u);
    }
    m.Put<std::uint32_t>(kVars + pk::kVarsEnergySum, sum);
}
void ArmEnergy() {
    Fake m;
    Arms(m, 100.0f, 54000, 1000, 1000);
    const auto r = m.Reader();
    const pp::ArmEnergy e = pp::ReadArmEnergy(r, kBase, kP);
    float f0 = 0;
    std::memcpy(&f0, &e.energy[0], 4);
    Check(e.ok && e.word == 54000 && f0 == 100.0f && e.frac[0] == 1000 && e.vars_sum == 1000, "arm energy: the word k=5, Y+0x1C, ctl+0x58, vars+0x670");
    Check(pp::EnergyDigest(e) != 0 && pp::EnergyDigest(pp::ArmEnergy{}) == 0, "arm energy: digest 0 only unread");
    Check(pp::CompareEnergy(e, pp::ReadArmEnergy(r, kBase, kP)) == pp::Energy::same, "arm energy: unchanged");
    {
        Fake b = m;
        Arms(b, 99.5f, 54000, 1000, 1000);
        Check(pp::CompareEnergy(e, pp::ReadArmEnergy(b.Reader(), kBase, kP)) == pp::Energy::changed, "arm energy: Y+0x1C moved");
        Arms(b, 100.0f, 53999, 1000, 1000);
        Check(pp::CompareEnergy(e, pp::ReadArmEnergy(b.Reader(), kBase, kP)) == pp::Energy::changed, "arm energy: the k=5 word moved");
        Arms(b, 100.0f, 54000, 999, 1000);
        Check(pp::CompareEnergy(e, pp::ReadArmEnergy(b.Reader(), kBase, kP)) == pp::Energy::changed, "arm energy: ctl+0x58 moved");
        Arms(b, 100.0f, 54000, 1000, 999);
        const auto moved = pp::ReadArmEnergy(b.Reader(), kBase, kP);
        Check(pp::CompareEnergy(e, moved) == pp::Energy::changed && pp::EnergyDigest(moved) != pp::EnergyDigest(e), "arm energy: vars+0x670 moved");
        // Another stock word (a grenade count, k != 5) is not the arm's: the per-job support digest covers it.
        Arms(b, 100.0f, 54000, 1000, 1000);
        b.Put<std::uint16_t>(kCounts + 2 * 6, 11);
        Check(pp::CompareEnergy(e, pp::ReadArmEnergy(b.Reader(), kBase, kP)) == pp::Energy::same, "arm energy: only word k=5");
    }
    // The chain refusals (each one unread, never a verdict).
    const auto unread = [&](Fake& f, const char* label) {
        const auto x = pp::ReadArmEnergy(f.Reader(), kBase, kP);
        Check(!x.ok && pp::CompareEnergy(e, x) == pp::Energy::unread, label);
    };
    {
        Fake b = m;
        b.Put<std::uintptr_t>(kCtl, kBase + 0x2295490);
        unread(b, "arm energy: a foreign ctl vtable");
    }
    {
        Fake b = m;
        b.Put<std::uintptr_t>(kCtl + pk::kCtlPlayer, kP + 8);
        unread(b, "arm energy: ctl+0x18 is not P");
    }
    {
        Fake b = m;
        b.Put<std::uintptr_t>(kStore, kBase + 0x234B840);
        unread(b, "arm energy: a foreign store vtable");
    }
    {
        Fake b = m;
        b.Put<std::uintptr_t>(kStore, kBase + pk::kStoreVtRva);
        Check(pp::ReadArmEnergy(b.Reader(), kBase, kP).ok, "arm energy: the base store vtable reads");
    }
    {
        Fake b = m;
        b.Put<std::uintptr_t>(kBase + pk::kVarsSlotRva, kVars + 0x1000);
        unread(b, "arm energy: ctl+0x20 is not the vars global");
    }
    {
        Fake b = m;
        b.Put<std::uintptr_t>(kStore + pk::kStoreCounts, 0);
        unread(b, "arm energy: no count array");
    }
    {
        Fake b = m;
        b.Erase(kStore + pk::kStoreEnergy, 4);
        unread(b, "arm energy: an unreadable energy float");
    }
    {
        Fake b = m;
        b.Erase(kVars + pk::kVarsEnergySum, 4);
        unread(b, "arm energy: an unreadable vars sum");
    }
    Check(!pp::ReadArmEnergy(r, kBase, 0).ok && !pp::ReadArmEnergy(r, 0, kP).ok, "arm energy: no P or base");
}

void Watchdog() {
    pp::ArmWatch w{};
    for (unsigned mode : {0u, 1u, 2u, 3u, 7u, 28u, 43u, 51u, 57u, 0xFFu})
        Check(!pp::StepWatch(w, static_cast<std::uint8_t>(mode)) && !w.latched, "watchdog: aim/idle/other modes never trip");
    Check(pp::StepWatch(w, 55) && w.latched && w.mode == 55, "watchdog: Jehuty's use mode 55 latches arms off");
    Check(!pp::StepWatch(w, 53) && w.mode == 55, "watchdog: latched once (the first mode is kept)");
    for (unsigned mode = 52; mode <= 56; ++mode) {
        pp::ArmWatch x{};
        Check(pp::StepWatch(x, static_cast<std::uint8_t>(mode)) && sp::ArmUseMode(static_cast<std::uint8_t>(mode)), "watchdog: every use mode 52..56");
    }
}

void ContractPins() {
    // ChangeEquip's type-14 branch, ctl vt+0x1F0 whole, the energy setter, the store read and the latch block.
    for (std::uintptr_t rva : {0x11AC379ull, 0x11AC4C6ull, 0x11B1B60ull, 0x11B1BB0ull, 0x11B1C00ull, 0x11B1C50ull, 0x11B1CA0ull, 0x11B1CF0ull,
                               0x11B1D40ull, 0x11AF820ull, 0x11AF870ull, 0x11AF5F1ull, 0x11AF65Dull, 0x11AF78Dull, 0xEE9050ull, 0x107BD64ull,
                               0x1060465ull, 0x1060495ull, 0x10605BFull})
        Check(Pinned(rva), "contract: a WS4 v1 code site is pinned");
    Check(Slot(0x23C1720, 0x1F0, 0x11B1B60) && Slot(0x23C1720, 0xE8, 0x11AF820) && Slot(0x23894B0, 0x88, 0xEE9050) &&
              Slot(0x23895D0, 0x88, 0xEE9050),
          "contract: the WS4 v1 vtable slots are pinned");
    // The whole 0x11B1B60 body: seven contiguous parts ending at its ret (0x11B1D66).
    std::size_t covered = 0;
    for (const auto& s : c::code_sites)
        if (s.rva >= 0x11B1B60 && s.rva < 0x11B1D67) covered += s.size;
    Check(covered == 0x207, "contract: ctl vt+0x1F0 is pinned whole (0x207 bytes)");
    // The pinned latch block names the offsets the viewer-latch digest reads.
    for (const auto& s : c::code_sites)
        if (s.rva == 0x107BD64) {
            std::uint32_t target = 0, flags = 0, latched = 0;
            std::memcpy(&target, s.bytes + 4, 4);    // 41 0f b7 86 <d8 03 00 00>: movzx eax, word [r14+0x3D8]
            std::memcpy(&flags, s.bytes + 16, 4);    // 41 83 8e <50 03 00 00> 08: or dword [r14+0x350], 8
            std::memcpy(&latched, s.bytes + 25, 4);  // 66 41 89 86 <e8 03 00 00>: mov word [r14+0x3E8], ax
            Check(target == pk::kPrimPlaceTarget && flags == pk::kPrimPlaceFlags && s.bytes[20] == pk::kPrimPlaceBit &&
                      latched == pk::kPrimPlaceLatched,
                  "contract: the latch block's offsets are the digest's");
        }
}

// --- WS4 v2: the place / detonate mirror (partner_place_mirror.hpp) -----------------------
namespace pm = mgscoop::partner_place_mirror;
pm::In Body(std::uint8_t remote_mode, std::uint8_t body_mode, std::uint32_t body_3cc, std::uint64_t now, std::uint8_t bits = 0x14,
            bool place_valid = true) {
    pm::In x{};
    x.key = true; x.lane = true; x.remote_valid = true; x.remote_mode = remote_mode;
    x.place_valid = place_valid; x.place_bits = place_valid ? bits : 0;
    x.body_ok = true; x.body_equip = 395; x.body_cls = 3; x.body_mode = body_mode; x.body_3cc = body_3cc; x.now_ms = now;
    return x;
}
pm::After AfterJob(std::uint8_t mode, std::uint32_t w3cc, std::uint8_t next = 0) {
    pm::After a{};
    a.ok = true; a.mode = mode; a.mode_next = next; a.w3cc = w3cc;
    return a;
}
void PlaceMirror() {
    // The census words: the viewer's ground place (mode 46) and its preview (mode 44); a wall class.
    Check(pm::WireBits(0x1500060u) == 0x14 && pm::WireSurface(0x14) == 1, "wire: the census' ground place names class 1");
    Check(pm::WireBits(0x1400000u) == 0x10 && pm::WireSurface(0x10) == 1, "wire: a preview-only word names its preview class");
    Check(pm::WireSurface(pm::WireBits(0x200000u | 0x800000u)) == 2 && pm::WireSurface(0) == 0, "wire: wall class 2; none 0");
    Check(pm::WireSurface(0x0C | 0x10) == 3, "wire: the placed class wins over the preview class");

    // A place: the remote's 44 -> 46 edge, body 1 holding C4 in 44 (census word 0x1000060).
    pm::State s{};
    pm::Out o = pm::Step(s, Body(44, 44, 0x1000060u, 1000));
    Check(o.act == pm::Act::none && o.edge == pm::Act::none && s.pending == pm::Act::none, "place: no edge while the remote readies");
    o = pm::Step(s, Body(46, 44, 0x1000060u, 1016));
    Check(o.edge == pm::Act::place && o.act == pm::Act::place && o.why == pm::Why::wrote && s.surface == 1 && !s.surface_default,
          "place: the remote's 46 edge writes at once");
    Check(o.value == (0x1000060u | 0x40000u | 0x400000u), "place: bit 18 and the class in 22-23, nothing else");
    Check((o.value ^ 0x1000060u) == (pm::k::kPlaceReq | (1u << pm::k::kPreviewShift)),
          "place: only the request and preview bits change");
    pm::Verdict v = pm::Judge(s, AfterJob(46, 0x1500060u), 1017);
    Check(v.act == pm::Act::place && v.taken && !v.revert && !v.retry && s.taken == 1, "place: body 1 entered 46 (taken)");
    o = pm::Step(s, Body(46, 46, 0x1500060u, 1033));
    Check(o.act == pm::Act::none && o.edge == pm::Act::none, "place: one write per remote episode");
    v = pm::Judge(s, AfterJob(46, 0x1500060u), 1034);
    Check(v.act == pm::Act::none, "verdict: nothing in flight");
    // Back to 44 and a second place: a new episode, a second write.
    o = pm::Step(s, Body(44, 44, 0x1410060u, 2000));
    o = pm::Step(s, Body(46, 44, 0x1410060u, 2100));
    Check(o.act == pm::Act::place && s.edges == 2 && s.writes == 2, "place: the next 46 edge is a new episode");
    v = pm::Judge(s, AfterJob(44, 0x1410060u, 46), 2101);
    Check(v.taken, "place: the queued next mode (rec+0x2C1 = 46) counts as taken");

    // Not taken: our bits go back to their values before the write, then a retry.
    s = pm::State{};
    o = pm::Step(s, Body(46, 43, 0x60u, 3000));
    Check(o.act == pm::Act::place && o.value == (0x60u | 0x40000u | 0x400000u), "retry: 43 takes a place write too");
    v = pm::Judge(s, AfterJob(43, o.value), 3001);
    Check(!v.taken && v.revert && v.value == 0x60u && v.retry && s.pending == pm::Act::place && s.reverted == 1,
          "retry: not taken -> reverted to the word before the write, pending again");
    o = pm::Step(s, Body(46, 44, 0x1000060u, 3017));
    Check(o.act == pm::Act::place && s.tries == 2 && o.edge == pm::Act::none, "retry: the same episode writes again (no new edge)");
    v = pm::Judge(s, AfterJob(44, 0x1000060u), 3018);
    Check(!v.taken && !v.revert && v.retry, "retry: a consumed request is never reverted");
    // The tries cap ends the episode.
    for (unsigned i = 0; i < 10; ++i) {
        o = pm::Step(s, Body(46, 44, 0x1000060u, 3020 + i));
        if (o.act != pm::Act::none) v = pm::Judge(s, AfterJob(44, o.value), 3020 + i);
    }
    Check(s.pending == pm::Act::none && s.tries == pm::k::kMaxTries && s.writes == pm::k::kMaxTries, "retry: at most kMaxTries writes");

    // An edge waits for body 1 (lagging in 0 / 45) and expires after kPendingMs.
    s = pm::State{};
    o = pm::Step(s, Body(46, 0, 0x0u, 5000));
    Check(o.edge == pm::Act::place && o.act == pm::Act::none && o.why == pm::Why::mode && s.pending == pm::Act::place,
          "wait: body 1 not in 43/44 yet");
    o = pm::Step(s, Body(46, 45, 0x0u, 5000 + pm::k::kPendingMs));
    Check(o.why == pm::Why::mode && s.pending == pm::Act::place, "wait: still within the bound");
    o = pm::Step(s, Body(46, 44, 0x1000060u, 5001 + pm::k::kPendingMs));
    Check(o.dropped == pm::Act::place && o.why == pm::Why::expired && o.dropped_after == pm::Why::mode && o.act == pm::Act::none &&
              s.dropped == 1,
          "wait: expired (names what it waited on), never late");
    // The remote leaving the place family drops it; a frame without the gun group changes nothing.
    s = pm::State{};
    pm::Step(s, Body(46, 0, 0x0u, 6000));
    pm::In gone = Body(0, 0, 0x0u, 6010);
    gone.remote_valid = false;
    o = pm::Step(s, gone);
    Check(s.pending == pm::Act::place && o.dropped == pm::Act::none, "frames: an invalid gun group neither drops nor edges");
    o = pm::Step(s, Body(0, 0, 0x0u, 6020));
    Check(o.dropped == pm::Act::place && o.why == pm::Why::left && s.pending == pm::Act::none, "frames: the remote put the C4 away");
    pm::In flicker = Body(46, 44, 0x1000060u, 6030);
    flicker.remote_valid = false;
    o = pm::Step(s, flicker);
    Check(o.edge == pm::Act::none && s.pending == pm::Act::none, "frames: an invalid frame with 46 is no edge");

    // A detonate: only from 44, bit 19 alone.
    s = pm::State{};
    pm::Step(s, Body(44, 44, 0x3410060u, 7000));
    o = pm::Step(s, Body(47, 43, 0x2410060u, 7016));
    Check(o.edge == pm::Act::detonate && o.act == pm::Act::none && o.why == pm::Why::mode, "detonate: 43 waits for 44");
    o = pm::Step(s, Body(47, 44, 0x3410060u, 7032));
    Check(o.act == pm::Act::detonate && o.value == (0x3410060u | 0x80000u), "detonate: bit 19 only (22-23 untouched)");
    v = pm::Judge(s, AfterJob(47, 0x1410060u), 7033);
    Check(v.taken && v.act == pm::Act::detonate, "detonate: body 1 entered 47");
    s = pm::State{};
    o = pm::Step(s, Body(47, 44, 0x1000060u, 7100));
    v = pm::Judge(s, AfterJob(45, o.value), 7101);
    Check(v.revert && v.value == 0x1000060u, "detonate: not taken -> bit 19 reverted");

    // Gates: the key, the lane, the body record, C4 class 3, a request already set.
    const auto gate = [](pm::In x, pm::Why want, const char* what) {
        pm::State g{};
        const pm::Out r = pm::Step(g, x);
        Check(r.act == pm::Act::none && r.why == want && g.pending == pm::Act::place && g.writes == 0, what);
    };
    pm::In x = Body(46, 44, 0x1000060u, 8000);
    x.key = false; gate(x, pm::Why::key, "gate: no key, no write");
    x = Body(46, 44, 0x1000060u, 8000); x.lane = false; gate(x, pm::Why::lane, "gate: the C4 lane not admitted");
    x = Body(46, 44, 0x1000060u, 8000); x.body_ok = false; gate(x, pm::Why::body, "gate: body 1's record unread");
    x = Body(46, 44, 0x1000060u, 8000); x.body_equip = 46; gate(x, pm::Why::equip, "gate: body 1 holds a gun");
    x = Body(46, 44, 0x1000060u, 8000); x.body_equip = 396; gate(x, pm::Why::equip, "gate: a mine (not 391..395)");
    x = Body(46, 44, 0x1000060u, 8000); x.body_cls = 2; gate(x, pm::Why::equip, "gate: not class 3");
    x = Body(46, 44, 0x1040060u, 8000); gate(x, pm::Why::busy, "gate: bit 18 already set");
    x = Body(46, 44, 0x1080060u, 8000); gate(x, pm::Why::busy, "gate: bit 19 already set");
    // A v6 peer (no place word) places on the ground class; a wall word keeps its class.
    s = pm::State{};
    o = pm::Step(s, Body(46, 44, 0x1000060u, 9000, 0, false));
    Check(o.act == pm::Act::place && s.surface == pm::k::kDefaultSurface && s.surface_default &&
              o.value == (0x1000060u | 0x40000u | (1u << 22)),
          "surface: no place word -> the ground class");
    s = pm::State{};
    o = pm::Step(s, Body(46, 44, 0x1000060u, 9100, 0x2A));
    Check(o.act == pm::Act::place && s.surface == 2 && (o.value & pm::k::kPreviewMask) == (2u << 22), "surface: a wall place keeps class 2");
    Check(std::strcmp(pm::ActName(pm::Act::detonate), "detonate") == 0 && std::strcmp(pm::WhyName(pm::Why::left), "remote_left") == 0,
          "names");
}
void PlaceMirrorPins() {
    // The mirror's bits and modes are the contract's.
    Check(pm::k::kPlaceReq == c::gun3cc_place_req && pm::k::kDetReq == c::gun3cc_detonate_req && pm::k::kPreviewMask == c::gun3cc_preview_mask &&
              pm::k::kSurfaceMask == c::gun3cc_surface_mask && c::gun_surface == 0x3CC,
          "contract: the rec+0x3CC bits");
    for (std::uintptr_t rva : {0x103E485u, 0x103E48Au, 0x103E4C0u, 0x103E4F3u, 0x107EBD3u, 0x107DC2Au, 0x10967A5u, 0x109687Cu, 0x109689Fu,
                               0x107BBE1u, 0x107BD27u, 0x107C6FAu, 0x107EFCDu, 0x107F2CAu, 0x105327Fu, 0x105333Eu})
        Check(Pinned(rva), "contract: every WS4 v2 site is pinned");
    const auto site = [](std::uintptr_t rva) -> const c::Site* {
        for (const auto& s : c::code_sites)
            if (s.rva == rva) return &s;
        return nullptr;
    };
    const auto has = [](const c::Site* s, std::initializer_list<unsigned char> pat) {
        if (!s) return false;
        const std::size_t n = pat.size();
        for (std::size_t i = 0; i + n <= s->size; ++i)
            if (std::memcmp(s->bytes + i, pat.begin(), n) == 0) return true;
        return false;
    };
    const auto mask_of = [](const c::Site* s) {  // 81 a? cc 03 00 00 <imm32>
        std::uint32_t m = 0;
        if (s && s->size >= 10) std::memcpy(&m, s->bytes + 6, 4);
        return m;
    };
    Check(has(site(0x107EBD3), {0xc1, 0xe8, 0x12}) && has(site(0x107EBD3), {0xbf, 0x2e, 0, 0, 0}), "mode 43 op 0: bit 18 -> 46");
    Check(has(site(0x107DC2A), {0xc1, 0xe8, 0x12}) && has(site(0x107DC2A), {0xbf, 0x2e, 0, 0, 0}) &&
              has(site(0x107DC2A), {0xc1, 0xea, 0x13}) && has(site(0x107DC2A), {0xbf, 0x2f, 0, 0, 0}),
          "mode 44 op 0: bit 18 -> 46, bit 19 -> 47");
    Check(has(site(0x109687C), {0x0f, 0xba, 0xf0, 0x13}) && has(site(0x109687C), {0x0f, 0xba, 0xe8, 0x12}),
          "decode: btr 19 / bts 18 (its only bit-18 write)");
    Check(~mask_of(site(0x107BBE1)) == (pm::k::kPlaceReq | 0x30000u), "mode 46 op 1 consumes bit 18 (with 16-17)");
    Check(~mask_of(site(0x107EFCD)) == pm::k::kDetReq && ~mask_of(site(0x105333E)) == pm::k::kDetReq,
          "mode 47 op 1 and the non-local detonation only clear bit 19");
    Check(has(site(0x107BD27), {0xc1, 0xe0, 0x14}) && has(site(0x107BD27), {0xc1, 0xe8, 0x02}) &&
              has(site(0x107BD27), {0x25, 0x00, 0x00, 0x30, 0x00}),
          "mode 46 op 1: probe class << 20, else fold 22-23 into 20-21");
    Check(has(site(0x105327F), {0xf6, 0x82, 0xc0, 0x03, 0x00, 0x00, 0x01}) && has(site(0x107C6FA), {0xf6, 0x87, 0xc0, 0x03, 0x00, 0x00, 0x01}),
          "the detonation and the placement test rec+0x3C0 bit 0 (local only)");
}
} // namespace

int main() {
    ViewerLatch();
    PlacedCompare();
    ArmEnergy();
    Watchdog();
    ContractPins();
    PlaceMirror();
    PlaceMirrorPins();
    std::printf("partner_placed_policy_tests: %u checks (fake memory; read-only WS4 v1 digests, the WS4 v2 mirror policy; no gameplay evidence)\n", checks);
    return 0;
}
