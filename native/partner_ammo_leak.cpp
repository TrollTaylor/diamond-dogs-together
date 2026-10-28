#include "mgscoop/partner_ammo_leak.hpp"

#include <cstdio>
#include <cstring>

namespace mgscoop::partner_ammo_leak {
namespace {
constexpr Member kTable[kMembers] = {
    {"weapons", 0x548, 2, 6, Kind::weapon},
    {"ammoInWeapons", 0x554, 2, 6, Kind::ammo},
    {"ammoSubInWeapons", 0x560, 2, 6, Kind::ammo},
    {"u8_56c", 0x56C, 1, 6, Kind::weapon},
    {"u8_572", 0x572, 1, 6, Kind::weapon},
    {"u8_578", 0x578, 1, 6, Kind::weapon},
    {"isInitialWeapon", 0x57E, 1, 6, Kind::weapon},
    {"supportWeapons", 0x584, 2, 16, Kind::weapon},
    {"items", 0x5A4, 2, 16, Kind::item},
    {"u8_5c4", 0x5C4, 1, 16, Kind::weapon},
    {"handEquip", 0x5D4, 2, 1, Kind::weapon},
    {"u8_5d6", 0x5D6, 1, 16, Kind::weapon},
    {"ammoStockIds", 0x5E6, 1, 46, Kind::ammo},
    {"ammoStockCounts", 0x614, 2, 46, Kind::ammo},
};
constexpr bool Contiguous() noexcept {
    std::size_t at = kBegin;
    for (const Member& m : kTable) {
        if (m.offset != at || (m.size != 1 && m.size != 2) || !m.count) return false;
        at += std::size_t{m.size} * m.count;
    }
    return at == kEnd;
}
static_assert(Contiguous(), "the 14 registrations tile vars+0x548..+0x670");
static_assert(kTable[kStockIds].count == 46 && kTable[kStockCounts].count == 46 &&
                  kItemStockFirst + kItemStocks <= 46, "item stock elements lie inside both stock tables");
std::uint16_t Element(const std::uint8_t* block, const Member& m, unsigned element) noexcept {
    const std::size_t at = m.offset - kBegin + std::size_t{m.size} * element;
    return m.size == 2 ? static_cast<std::uint16_t>(block[at] | (block[at + 1] << 8)) : block[at];
}
}  // namespace

const Member& MemberAt(unsigned index) noexcept { return kTable[index < kMembers ? index : 0]; }

Kind ElementKind(unsigned member, unsigned element) noexcept {
    if (member >= kMembers) return Kind::weapon;
    if ((member == kStockIds || member == kStockCounts) && element >= kItemStockFirst && element < kItemStockFirst + kItemStocks)
        return Kind::item;
    return kTable[member].kind;
}

const char* KindName(Kind kind) noexcept {
    return kind == Kind::ammo ? "ammo" : kind == Kind::item ? "item" : "weapon";
}

Diff Compare(const std::uint8_t* before, const std::uint8_t* after) noexcept {
    Diff d{};
    if (!before || !after) return d;
    for (unsigned i = 0; i < kMembers; ++i) {
        const Member& m = kTable[i];
        for (unsigned e = 0; e < m.count; ++e) {
            const std::uint16_t a = Element(before, m, e), b = Element(after, m, e);
            if (a == b) continue;
            d.members |= 1u << i;
            ++d.elements;
            const Kind k = ElementKind(i, e);
            d.weapon = d.weapon || k == Kind::weapon;
            d.ammo = d.ammo || k == Kind::ammo;
            d.item = d.item || k == Kind::item;
            if (d.kept < kChangesKept)
                d.first[d.kept++] = Change{static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(e), a, b};
        }
    }
    return d;
}

Verdict Classify(const Diff& diff) noexcept {
    if (!diff.elements) return Verdict::none;
    // Only the viewer's own item state changed: never written by body 1's FIRE
    // or RELOAD. One weapon or ammo element anywhere makes it a leak.
    return diff.weapon || diff.ammo ? Verdict::leak : Verdict::benign;
}

const char* VerdictName(Verdict verdict) noexcept {
    return verdict == Verdict::leak ? "leak" : verdict == Verdict::benign ? "benign" : "none";
}

std::size_t FormatChanges(const Diff& diff, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    out[0] = 0;
    std::size_t at = 0;
    for (unsigned i = 0; i < diff.kept; ++i) {
        const Change& c = diff.first[i];
        const int n = std::snprintf(out + at, capacity - at, "%s%s[%u]:%u>%u", i ? "," : "", MemberAt(c.member).name,
                                    unsigned(c.element), unsigned(c.before), unsigned(c.after));
        if (n < 0 || static_cast<std::size_t>(n) >= capacity - at) { out[0] = 0; return 0; }
        at += static_cast<std::size_t>(n);
    }
    if (diff.elements > diff.kept) {
        if (at + 1 >= capacity) { out[0] = 0; return 0; }
        out[at++] = '+';
        out[at] = 0;
    }
    if (!at) {
        if (capacity < 5) return 0;
        std::memcpy(out, "none", 5);
        at = 4;
    }
    return at;
}

bool StripsFireReload(bool play_mode, const MissionLeaks& mission) noexcept {
    return play_mode && (mission.shot_leaks || mission.ammo_leaks || mission.mag_leak_off);
}

}  // namespace mgscoop::partner_ammo_leak
