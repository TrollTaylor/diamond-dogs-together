// WS4 step 0: the read-only C4 / bionic-arm census readers (partner_placed_census.hpp)
// on fake memory. Synthetic only: a byte map stands in for the game; nothing here
// runs or reads game code. Also checks the generated contract carries the new pins.
#include "mgscoop/partner_placed_census.hpp"
#include "partner_player_contract.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>

namespace pc = mgscoop::partner_placed_census;
namespace k = mgscoop::partner_placed_census::k;
namespace c = mgscoop::partner_player_contract;

namespace {
unsigned checks = 0;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "failed: %s\n", label);
        std::exit(1);
    }
}

// A sparse fake address space: a read succeeds only when every byte was put.
struct Fake {
    std::map<std::uintptr_t, unsigned char> bytes;
    unsigned reads = 0;
    template <class T>
    void Put(std::uintptr_t at, T value) {
        unsigned char raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes[at + i] = raw[i];
    }
    void Erase(std::uintptr_t at, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) bytes.erase(at + i);
    }
    bool Read(std::uintptr_t at, void* out, std::size_t size) {
        ++reads;
        auto* o = static_cast<unsigned char*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = bytes.find(at + i);
            if (it == bytes.end()) return false;
            o[i] = it->second;
        }
        return true;
    }
};

constexpr std::uintptr_t kBase = 0x140000000ull;
constexpr std::uintptr_t kRoot = 0x7FF000010000ull, kSvc = 0x7FF000020000ull, kGm = 0x7FF000030000ull;
constexpr std::uintptr_t kIface = 0x7FF000040000ull, kOwners = 0x7FF000050000ull, kFlags = 0x7FF000060000ull;
constexpr std::uintptr_t kEquips = 0x7FF000070000ull;

// The placed service with the given owners, `per` slots each from `first`;
// every slot exists (flags 0, equip 0) unless set live below.
void Service(Fake& m, std::initializer_list<std::uint16_t> owners, std::uint32_t per, std::uint32_t first = 0) {
    m.Put<std::uintptr_t>(kBase + k::kServiceRootRva, kRoot);
    m.Put<std::uintptr_t>(kRoot + k::kRootService, kSvc);
    m.Put<std::uintptr_t>(kSvc + k::kSvcGm, kGm);
    m.Put<std::uintptr_t>(kGm + k::kGmPlaced, kIface);
    m.Put<std::uintptr_t>(kIface, kBase + k::kPlacedLiveVtRva);
    m.Put<std::uint32_t>(kIface + k::kFirst, first);
    m.Put<std::uint32_t>(kIface + k::kOwnerCount, static_cast<std::uint32_t>(owners.size()));
    m.Put<std::uint32_t>(kIface + k::kPerOwner, per);
    m.Put<std::uintptr_t>(kIface + k::kOwners, kOwners);
    m.Put<std::uintptr_t>(kIface + k::kFlags, kFlags);
    m.Put<std::uintptr_t>(kIface + k::kEquips, kEquips);
    std::uint32_t i = 0;
    for (std::uint16_t o : owners) m.Put<std::uint16_t>(kOwners + 2 * i++, o);
    const std::uint32_t slots = first + per * static_cast<std::uint32_t>(owners.size());
    for (std::uint32_t s = 0; s < slots; ++s) {
        for (std::size_t b = 0; b < k::kFlagStride; ++b) m.Put<std::uint8_t>(kFlags + s * k::kFlagStride + b, 0);
        m.Put<std::uint32_t>(kEquips + s * k::kEquipStride, 0);
    }
}
void Live(Fake& m, std::uint32_t slot, std::uint16_t equip) {
    m.Put<std::uint8_t>(kFlags + slot * k::kFlagStride, k::kFlagLive);
    m.Put<std::uint16_t>(kEquips + slot * k::kEquipStride, equip);
}
pc::Service Read(Fake& m) {
    return pc::ReadService([&](std::uintptr_t at, void* out, std::size_t size) noexcept { return m.Read(at, out, size); }, kBase);
}
bool Has(const char* text, const char* needle) { return std::strstr(text, needle) != nullptr; }

void ServiceChain() {
    Fake m;
    Check(std::strcmp(Read(m).refusal, "root") == 0, "service: no root global -> root");
    m.Put<std::uintptr_t>(kBase + k::kServiceRootRva, kRoot);
    Check(std::strcmp(Read(m).refusal, "svc") == 0, "service: unreadable root+8 -> svc");
    m.Put<std::uintptr_t>(kRoot + k::kRootService, 0);
    Check(std::strcmp(Read(m).refusal, "svc") == 0, "service: null svc -> svc");
    m.Put<std::uintptr_t>(kRoot + k::kRootService, kSvc);
    Check(std::strcmp(Read(m).refusal, "gm") == 0, "service: unreadable gm -> gm");
    m.Put<std::uintptr_t>(kSvc + k::kSvcGm, kGm);
    m.Put<std::uintptr_t>(kGm + k::kGmPlaced, 0);
    Check(std::strcmp(Read(m).refusal, "iface") == 0, "service: gm+0x208 not built yet -> iface");
    Service(m, {0}, 4);
    m.Put<std::uintptr_t>(kIface, kBase + k::kPlacedBaseVtRva);
    const pc::Service dying = Read(m);
    Check(std::strcmp(dying.refusal, "dying") == 0 && dying.count == 0, "service: the base (dtor) vtable -> dying, nothing read");
    m.Put<std::uintptr_t>(kIface, kBase + 0x23B3E80);
    Check(std::strcmp(Read(m).refusal, "vtable") == 0, "service: a foreign vtable -> vtable");
    m.Put<std::uintptr_t>(kIface, kBase + k::kPlacedLiveVtRva);
    Check(Read(m).refusal == nullptr, "service: the live vtable reads");
    m.Erase(kIface + k::kPerOwner, 4);
    Check(std::strcmp(Read(m).refusal, "fields") == 0, "service: an unreadable field -> fields");
    m.Put<std::uint32_t>(kIface + k::kPerOwner, k::kMaxPerOwner + 1);
    Check(std::strcmp(Read(m).refusal, "per") == 0, "service: per above the bound -> per (never read)");
    m.Put<std::uint32_t>(kIface + k::kPerOwner, 4);
    m.Put<std::uint32_t>(kIface + k::kOwnerCount, k::kMaxOwners + 1);
    Check(std::strcmp(Read(m).refusal, "count") == 0, "service: count above the bound -> count (never read)");
    m.Put<std::uint32_t>(kIface + k::kOwnerCount, 2);
    Check(std::strcmp(Read(m).refusal, "owners") == 0, "service: the owner table shorter than its count -> owners");
    m.Put<std::uint32_t>(kIface + k::kOwnerCount, 1);
    m.Put<std::uintptr_t>(kIface + k::kFlags, 0);
    Check(std::strcmp(Read(m).refusal, "slots") == 0, "service: no flags array -> slots");
    m.Put<std::uintptr_t>(kIface + k::kFlags, kFlags);
    m.Erase(kEquips + 3 * k::kEquipStride, 4);
    Check(std::strcmp(Read(m).refusal, "slots") == 0, "service: an unreadable slot equip -> slots");
}

void ServiceOwners() {
    // Offline two-instance setup as the census will see it if owner 1 is absent:
    // only the viewer's owner 0 (two charges) and an unrelated owner.
    Fake m;
    Service(m, {0, 0x8001}, 4);
    Live(m, 1, 391);
    Live(m, 3, 392);
    Live(m, 4 + 0, 999);
    pc::Service s = Read(m);
    Check(s.refusal == nullptr && s.count == 2 && s.per == 4 && s.first == 0, "owners: count / per / first");
    Check(s.index0 == 0 && s.live0 == 2 && s.index1 == -1 && s.live1 == 0 && s.live_total == 3, "owners: owner 0 with 2 live, no owner 1");
    Check(s.rows == 2 && s.row[0].owner == 0 && s.row[0].listed == 2 && s.row[0].equips[0] == 391 && s.row[0].equips[1] == 392 &&
              s.row[1].owner == 0x8001 && s.row[1].live == 1 && s.row[1].equips[0] == 999,
          "owners: rows list each owner's live equips");
    char line[512]{};
    Check(pc::FormatService(line, sizeof(line), s) && Has(line, "partner_placed_census=service; reason=ok; count=2; per=4; first=0; owner0=0; owner1=-1;") &&
              Has(line, "owners=0:2[391/392],32769:1[999]"),
          "owners: the service line");
    // Owner 1 registered (a first offset in front): its slots are first + per*index + j.
    Fake m2;
    Service(m2, {0, 1}, 4, 8);
    Live(m2, 8 + 4 * 1 + 2, 395);
    pc::Service s2 = Read(m2);
    Check(s2.refusal == nullptr && s2.index0 == 0 && s2.live0 == 0 && s2.index1 == 1 && s2.live1 == 1 && s2.row[1].equips[0] == 395,
          "owners: owner 1 registered, its slot found at first + per*index + j");
    Check(pc::FormatService(line, sizeof(line), s2) && Has(line, "owner1=1; live0=0; live1=1;"), "owners: owner 1 in the line");
    // Change detection: the digest moves with a charge and stays put otherwise.
    const std::uint32_t d0 = Read(m2).digest;
    Check(Read(m2).digest == d0, "digest: stable without a change");
    Live(m2, 8 + 1, 393);
    Check(Read(m2).digest != d0, "digest: a new owner-0 charge changes it");
    // A long owner list is summarized: listed rows stop at kListedOwners, totals still count all.
    Fake m3;
    Service(m3, {0, 2, 3, 4, 1, 6}, 2);
    Live(m3, 4 * 2 + 1, 394);
    pc::Service s3 = Read(m3);
    Check(s3.rows == k::kListedOwners && s3.index1 == 4 && s3.live1 == 1 && s3.live_total == 1, "owners: owner 1 past the listed rows");
    // Nothing allocated yet (count 0): read, no owners.
    Fake m4;
    Service(m4, {}, 4);
    pc::Service s4 = Read(m4);
    Check(s4.refusal == nullptr && s4.count == 0 && s4.index0 == -1 && s4.index1 == -1 && pc::FormatService(line, sizeof(line), s4) &&
              Has(line, "owners=none"),
          "owners: an empty table");
}

void GunExtra() {
    Fake m;
    constexpr std::uintptr_t kRec = 0x7FF000100000ull, kPrim = 0x7FF000200000ull;
    m.Put<std::uint8_t>(kRec + k::kRecState, 3);
    m.Put<std::uint8_t>(kRec + k::kRecSub, 2);
    m.Put<std::uint32_t>(kRec + k::kRecSurface, 0x00C00000u);
    m.Put<std::uint32_t>(kPrim + k::kPrimPlaceFlags, 0x18u);
    m.Put<std::uint16_t>(kPrim + k::kPrimPlaceTarget, 0x1234);
    m.Put<std::uint16_t>(kPrim + k::kPrimPlaceLatched, 0x1234);
    const auto read = [&](std::uintptr_t at, void* out, std::size_t size) noexcept { return m.Read(at, out, size); };
    pc::GunSample s{};
    s.ok = true;
    s.rec = kRec;
    s.prim = kPrim;
    s.equip = 391;
    s.type = 13;
    s.w27c = (2u << 6) | 0x1Fu;
    s.cls = pc::GunClass(s.w27c);
    s.mode = 46;
    pc::ReadGunExtra(read, s);
    Check(s.extra_ok && s.state == 3 && s.sub == 2 && s.f3cc == 0x00C00000u, "gun: state / sub / surface read");
    Check(s.prim_ok && s.p350 == 0x18u && pc::Latched(s) && s.p3d8 == 0x1234 && s.p3e8 == 0x1234, "gun: the plugin latch read");
    char line[640]{};
    Check(pc::FormatGun(line, sizeof(line), 0, s, 0) &&
              Has(line, "partner_placed_census=gun; k=0; equip=391; type=13; cls=2; mode=46; mode2c1=255; state=3;") &&
              Has(line, "f3cc=0xc00000; surface=12; p350=0x18; latched=1; p3d8=0x1234; p3e8=0x1234; prim_ok=1; same_prim=0; c4=1; arm=0"),
          "gun: the line");
    // No prim: the latch is unread (not latched), the record fields still read.
    pc::GunSample t{};
    t.ok = true;
    t.rec = kRec;
    pc::ReadGunExtra(read, t);
    Check(t.extra_ok && !t.prim_ok && !pc::Latched(t), "gun: no prim, latch unread");
    pc::GunSample u{};
    u.ok = true;
    u.rec = 0x7FF000300000ull;
    u.prim = kPrim;
    pc::ReadGunExtra(read, u);
    Check(!u.extra_ok && u.prim_ok, "gun: an unreadable record leaves extra_ok false");
}

void GunPolicy() {
    pc::GunSample gun{};
    gun.ok = true;
    gun.equip = 46;
    gun.type = 1;
    gun.cls = 1;
    gun.mode = 7;
    pc::GunSample c4 = gun;
    c4.equip = 393;
    c4.type = 13;
    c4.cls = 2;
    c4.mode = 44;
    pc::GunSample arm = gun;
    arm.equip = 515;
    arm.type = 14;
    arm.cls = 3;
    arm.mode = 52;
    Check(!pc::Interesting(gun) && pc::Interesting(c4) && pc::Interesting(arm), "policy: a gun is not interesting, C4 and the arm are");
    Check(pc::C4Equip(391) && pc::C4Equip(395) && !pc::C4Equip(390) && !pc::C4Equip(396), "policy: C4 equips 391..395");
    Check(pc::ArmEquip(515) && pc::ArmEquip(517) && pc::ArmEquip(518) && !pc::ArmEquip(514) && !pc::ArmEquip(519), "policy: arm equips");
    Check(pc::PlaceMode(43) && pc::PlaceMode(47) && !pc::PlaceMode(42) && !pc::PlaceMode(48), "policy: place modes 43..47");
    Check(pc::ArmMode(52) && pc::ArmMode(56) && !pc::ArmMode(51) && !pc::ArmMode(57), "policy: arm modes 52..56");
    Check(pc::GunClass((3u << 6) | 0x3Fu) == 3 && pc::GunClass(0x40u) == 1, "policy: class (rec+0x27C>>6)&7");
    {
        pc::GunSample x = gun;
        x.mode = 46;
        Check(pc::Interesting(x), "policy: a place mode with an unknown equip is interesting");
        x = gun;
        x.prim_ok = true;
        x.p350 = k::kPrimPlaceBit;
        Check(pc::Interesting(x), "policy: the plugin latch alone is interesting");
        x.ok = false;
        Check(!pc::Interesting(x), "policy: an unread sample never is");
    }
    Check(!pc::LogGun(false, pc::GunSample{}, gun) && pc::LogGun(false, pc::GunSample{}, c4), "log: the first sample only when interesting");
    Check(pc::LogGun(true, gun, c4), "log: gun -> C4 logged");
    Check(!pc::LogGun(true, c4, c4), "log: no change, no line");
    {
        pc::GunSample placing = c4;
        placing.mode = 46;
        placing.state = 3;
        Check(pc::LogGun(true, c4, placing), "log: 44 -> 46 logged");
        pc::GunSample latched = placing;
        latched.prim_ok = true;
        latched.p350 = k::kPrimPlaceBit;
        latched.p3e8 = 7;
        Check(pc::LogGun(true, placing, latched), "log: the plugin latch setting is logged");
        pc::GunSample state = latched;
        state.state = 4;
        Check(pc::LogGun(true, latched, state), "log: a state change is logged");
    }
    Check(pc::LogGun(true, c4, gun), "log: C4 -> gun (the exit) logged");
    pc::GunSample gun2 = gun;
    gun2.mode = 9;
    Check(!pc::LogGun(true, gun, gun2), "log: gun mode changes are not logged");
    pc::GunSample latched_gun = gun;
    latched_gun.prim_ok = true;
    latched_gun.p350 = k::kPrimPlaceBit;
    Check(pc::LogGun(true, gun, latched_gun) && pc::LogGun(true, latched_gun, gun), "log: a latch left on a gun is logged both ways");
}

void Budgets() {
    pc::Budget b{};
    unsigned taken = 0;
    for (unsigned i = 0; i < 15; ++i) taken += pc::Take(b, 1000, 10, 25) ? 1u : 0u;
    Check(taken == 10 && !b.capped, "budget: 10 per second");
    Check(!pc::Take(b, 1999, 10, 25) && pc::Take(b, 2000, 10, 25), "budget: the next second refills");
    for (unsigned i = 0; i < 9; ++i) pc::Take(b, 2000, 10, 25);
    taken = 0;
    for (unsigned i = 0; i < 20; ++i) taken += pc::Take(b, 3000, 10, 25) ? 1u : 0u;
    Check(taken == 5 && b.total == 25 && b.capped, "budget: the mission cap");
    Check(pc::CapReport(b) && !pc::CapReport(b), "budget: capped reported once");
    pc::Budget back{};
    Check(pc::Take(back, 5000, 1, 10) && pc::Take(back, 100, 1, 10), "budget: a clock that went back opens a new window");
}

void PadPolicy() {
    pc::detail::ResetPadForTest();
    pc::PadSample out{};
    Check(!pc::PadCensusOn() && !pc::ReadPad(out), "pad: off and nothing published");
    pc::SetPadCensus(true);
    Check(pc::PadCensusOn(), "pad: armed");
    pc::PadSample s{};
    s.windows = 3;
    s.pre_ok = s.post_ok = true;
    s.viewer_mag = 0.8f;
    s.viewer_mask = 0x100;
    s.body_mag_pre = 0.0f;
    s.body_mag_post = 0.0f;
    s.body_mask_pre = 0x1;
    s.mode_pre = 52;
    s.mode_post = 52;
    pc::PublishPad(s);
    Check(pc::ReadPad(out) && out.windows == 3 && out.viewer_mag == 0.8f && out.body_mask_pre == 1u && out.mode_pre == 52,
          "pad: the published sample reads back");
    Check(pc::PadDiffers(out) && pc::PadArm(out), "pad: differs, arm mode");
    pc::PadSample same = s;
    same.body_mag_pre = same.viewer_mag;
    same.body_mask_pre = same.viewer_mask;
    Check(!pc::PadDiffers(same), "pad: identical fields do not differ");
    same.body_mask_pre = same.viewer_mask | 0x2u;
    Check(!pc::PadDiffers(same), "pad: only mask bit 0 counts (the op-0 test)");
    same.body_mask_pre = same.viewer_mask ^ 1u;
    Check(pc::PadDiffers(same), "pad: mask bit 0 differs");
    pc::PadSample unread = s;
    unread.pre_ok = false;
    Check(!pc::PadDiffers(unread), "pad: an unread PRE never differs");
    pc::PadSample idle = same;
    idle.mode_pre = idle.mode_post = 7;
    idle.windows = 4;
    Check(pc::LogPad(false, pc::PadSample{}, idle), "pad log: the first sample");
    pc::PadSample idle2 = idle;
    idle2.windows = 5;
    Check(!pc::LogPad(true, idle, idle2), "pad log: no arm mode and no verdict change: no line");
    Check(!pc::LogPad(true, idle, idle), "pad log: the same window is never logged twice");
    pc::PadSample arm = idle2;
    arm.windows = 6;
    arm.mode_post = 52;
    Check(pc::LogPad(true, idle2, arm), "pad log: body 1 entering an arm mode");
    pc::PadSample arm2 = arm;
    arm2.windows = 7;
    arm2.mode_pre = 52;
    Check(pc::LogPad(true, arm, arm2), "pad log: every window while in an arm mode");
    pc::PadSample flip = idle2;
    flip.windows = 8;
    flip.body_mask_pre ^= 1u;
    Check(pc::LogPad(true, idle2, flip), "pad log: the differs verdict changing");
    char line[512]{};
    Check(pc::FormatPad(line, sizeof(line), s) &&
              Has(line, "partner_placed_census=pad; windows=3; pre_ok=1; post_ok=1; viewer_mag=0.800; viewer_mask=0x100; body_mag=0.000; "
                        "body_mask=0x1;") &&
              Has(line, "mode=52; mode_post=52; differs=1; arm=1"),
          "pad: the line");
    pc::SetPadCensus(false);
    Check(!pc::PadCensusOn() && pc::ReadPad(out), "pad: disarming keeps the last sample readable");
    pc::detail::ResetPadForTest();
}

void ContractPins() {
    const auto site = [](std::uintptr_t rva) {
        for (const c::Site& s : c::code_sites)
            if (s.rva == rva) return true;
        return false;
    };
    Check(site(0xFC557A) && site(0xFC55A9) && site(0xFC55E7) && site(0x107BC5B),
          "contract: the owner-table reader and the mode-46 gate are pinned");
    bool slot = false;
    for (const c::Slot& s : c::vtable_slots)
        if (s.vtable_rva == k::kPlacedLiveVtRva && s.offset == c::placed_vt_detonate && s.target_rva == c::placed_detonate_rva) slot = true;
    Check(slot, "contract: placed iface vt+0x68 = 0xFC54F0");
    Check(c::placed_owner_count == k::kOwnerCount && c::placed_owners == k::kOwners && c::placed_per_owner == k::kPerOwner &&
              c::prim_place_flags == k::kPrimPlaceFlags && c::pad_stun_mag == k::kPadMag && c::pad_stun_mask == k::kPadMask,
          "contract: the census offsets");
}
} // namespace

int main() {
    ServiceChain();
    ServiceOwners();
    GunExtra();
    GunPolicy();
    Budgets();
    PadPolicy();
    ContractPins();
    std::printf("partner_placed_census_tests: %u checks passed\n", checks);
    return 0;
}
