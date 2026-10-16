// WS4 v1 (docs/anim-round2-throws.md "WS4 v1"): the read-only placed / arm safety checks
// (partner_placed_policy.hpp) on fake memory, the watchdog, and the contract pins they rely on.
// Synthetic only: a byte map stands in for the game; nothing here runs or reads game code.
#include "mgscoop/partner_placed_policy.hpp"
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
} // namespace

int main() {
    ViewerLatch();
    PlacedCompare();
    ArmEnergy();
    Watchdog();
    ContractPins();
    std::printf("partner_placed_policy_tests: %u checks (fake memory; read-only WS4 v1 digests; no gameplay evidence)\n", checks);
    return 0;
}
