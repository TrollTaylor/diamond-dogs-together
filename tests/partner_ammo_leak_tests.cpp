// The viewer's weapon/ammo vars block around body 1's job (two-PC test
// 2026-10-23 item 3): field-level diff, benign item changes, the leak verdict
// and the mission-scoped strip. Pure; no game process.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mgscoop/partner_ammo_leak.hpp"
#include "partner_player_contract.hpp"

namespace {
unsigned checks = 0;
void Check(bool ok, const char* why) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "partner_ammo_leak check %u: %s\n", checks, why); std::exit(1); }
}
namespace al = mgscoop::partner_ammo_leak;
using Block = std::uint8_t[al::kSize];
void Put(Block& b, std::size_t vars_offset, unsigned size, std::uint16_t value) {
    const std::size_t at = vars_offset - al::kBegin;
    b[at] = static_cast<std::uint8_t>(value & 0xFF);
    if (size == 2) b[at + 1] = static_cast<std::uint8_t>(value >> 8);
}
// A plausible viewer block: weapons 871/873/875, magazines, stocks, items 0,468,451.
void Fill(Block& b) {
    std::memset(b, 0, sizeof(b));
    const std::uint16_t weapons[3] = {871, 873, 875};
    for (unsigned i = 0; i < 3; ++i) {
        Put(b, 0x548 + 2 * i, 2, weapons[i]);
        Put(b, 0x554 + 2 * i, 2, static_cast<std::uint16_t>(9 + i));
    }
    Put(b, 0x5A4, 2, 0);
    Put(b, 0x5A6, 2, 468);
    Put(b, 0x5A8, 2, 451);
    for (unsigned k = 0; k < 46; ++k) {
        Put(b, 0x5E6 + k, 1, static_cast<std::uint8_t>(k + 1));
        Put(b, 0x614 + 2 * k, 2, static_cast<std::uint16_t>(40 + k));
    }
}
}  // namespace

int main() {
    namespace c = mgscoop::partner_player_contract;
    // The table is the contract's block, in registration order.
    Check(al::kBegin == c::vars_ammo_begin && al::kEnd == c::vars_ammo_end, "the block is the contract's vars+0x548..+0x670");
    Check(std::strcmp(al::MemberAt(0).name, "weapons") == 0 && al::MemberAt(0).offset == c::vars_weapons,
          "member 0 is vars.weapons at the contract offset");
    Check(std::strcmp(al::MemberAt(8).name, "items") == 0 && al::MemberAt(8).offset == c::vars_items,
          "member 8 is vars.items at the contract offset");
    Check(std::strcmp(al::MemberAt(al::kStockCounts).name, "ammoStockCounts") == 0 &&
              al::MemberAt(al::kStockCounts).offset == c::support_stock_counts && al::MemberAt(al::kStockCounts).count == c::support_stock_count,
          "ammoStockCounts is the support lane's u16[46] at +0x614");
    Check(al::kItemStockFirst == c::inv_item_stock_base, "item stocks start at the inventory's item stock base");
    // Element kinds.
    Check(al::ElementKind(al::kStockCounts, 6) == al::Kind::ammo && al::ElementKind(al::kStockCounts, 13) == al::Kind::ammo,
          "support slot-3 stocks (6..13) are ammo");
    Check(al::ElementKind(al::kStockCounts, 14) == al::Kind::item && al::ElementKind(al::kStockCounts, 21) == al::Kind::item &&
              al::ElementKind(al::kStockIds, 17) == al::Kind::item,
          "item stocks (14..21) are items in both stock tables");
    Check(al::ElementKind(al::kStockCounts, 22) == al::Kind::ammo && al::ElementKind(al::kStockCounts, 0) == al::Kind::ammo,
          "every other stock element is ammo");
    Check(al::ElementKind(1, 0) == al::Kind::ammo && al::ElementKind(2, 5) == al::Kind::ammo, "magazines are ammo");
    Check(al::ElementKind(9, 0) == al::Kind::weapon && al::ElementKind(11, 3) == al::Kind::weapon,
          "unnamed bytes are never benign");

    Block before{}, after{};
    Fill(before);
    std::memcpy(after, before, sizeof(after));
    auto d = al::Compare(before, after);
    Check(d.elements == 0 && d.members == 0 && al::Classify(d) == al::Verdict::none, "an unchanged block: no verdict");

    // The online case: the viewer's own box comes off (item stock 0xE + sub 1 drops by one).
    Put(after, 0x614 + 2 * (0xE + 1), 2, 54);
    d = al::Compare(before, after);
    Check(d.elements == 1 && d.item && !d.ammo && !d.weapon && d.members == (1u << al::kStockCounts),
          "the box stock change is one item element of ammoStockCounts");
    Check(d.kept == 1 && d.first[0].member == al::kStockCounts && d.first[0].element == 15 && d.first[0].before == 55 &&
              d.first[0].after == 54,
          "the change names its member, element and values");
    char text[128]{};
    Check(al::FormatChanges(d, text, sizeof(text)) && std::strcmp(text, "ammoStockCounts[15]:55>54") == 0,
          "the log names ammoStockCounts[15]:55>54");
    Check(al::Classify(d) == al::Verdict::benign, "an item-only change inside body 1's job is benign (no strip)");
    // The item list and its stock id too (the box removed from the items).
    Put(after, 0x5A6, 2, 0);
    Put(after, 0x5E6 + 0xE + 1, 1, 0);
    d = al::Compare(before, after);
    Check(d.elements == 3 && al::Classify(d) == al::Verdict::benign, "items and item stock ids stay benign");

    // Never let a viewer-side ammo change slip through.
    struct Case { std::size_t offset; unsigned size; std::uint16_t value; const char* why; };
    const Case leaks[] = {
        {0x554, 2, 8, "a magazine (ammoInWeapons)"},
        {0x560 + 2, 2, 3, "a sub magazine (ammoSubInWeapons)"},
        {0x614 + 2 * 3, 2, 1, "a weapon ammo stock"},
        {0x614 + 2 * 6, 2, 2, "a support slot-3 stock"},
        {0x614 + 2 * 22, 2, 7, "a stock after the item stocks"},
        {0x5E6 + 2, 1, 99, "an ammo stock id"},
        {0x548 + 2, 2, 46, "a weapon id"},
        {0x584, 2, 349, "a support weapon"},
        {0x5D4, 2, 516, "the hand equip"},
        {0x56C, 1, 1, "an unnamed per-weapon byte"},
        {0x5C4, 1, 1, "an unnamed u8[16] byte"},
        {0x57E, 1, 1, "isInitialWeapon"},
    };
    for (const Case& k : leaks) {
        Block alone{};
        std::memcpy(alone, before, sizeof(alone));
        Put(alone, k.offset, k.size, k.value);
        d = al::Compare(before, alone);
        Check(d.elements == 1 && al::Classify(d) == al::Verdict::leak, k.why);
        // An item change in the same window never hides it.
        Put(alone, 0x614 + 2 * (0xE + 1), 2, 54);
        d = al::Compare(before, alone);
        Check(d.elements == 2 && d.item && al::Classify(d) == al::Verdict::leak, "an item change never masks a leak");
    }
    // Many changes: four kept, '+' marks the rest.
    Block many{};
    std::memcpy(many, before, sizeof(many));
    for (unsigned i = 0; i < 6; ++i) Put(many, 0x554 + 2 * i, 2, static_cast<std::uint16_t>(100 + i));
    d = al::Compare(before, many);
    Check(d.elements == 6 && d.kept == al::kChangesKept && al::FormatChanges(d, text, sizeof(text)) &&
              text[std::strlen(text) - 1] == '+' && al::Classify(d) == al::Verdict::leak,
          "the log keeps four changes and marks more");
    Check(al::FormatChanges(d, text, 8) == 0 && text[0] == 0, "a short buffer refuses instead of truncating");

    // The play strip is mission-scoped: this mission's counters only.
    Check(!al::StripsFireReload(true, {0, 0, false}), "a clean mission: no strip");
    Check(al::StripsFireReload(true, {1, 0, false}) && al::StripsFireReload(true, {0, 1, false}) &&
              al::StripsFireReload(true, {0, 0, true}),
          "a shot leak, an ammo leak or the mirror's own leak stop strips FIRE and RELOAD in play");
    Check(!al::StripsFireReload(false, {3, 3, true}), "the lab only counts");
    std::printf("%u partner ammo leak checks; no game calls\n", checks);
    return 0;
}
