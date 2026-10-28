#pragma once
// The viewer's weapon/ammo vars block (vars+0x548..+0x670) around body 1's job:
// which fields changed, and whether the change is a leak (two-PC test
// 2026-10-23 item 3). Pure, portable and deterministic (mgs_coop_core).
//
// partner_input_hook digests this block around every deferred job of body 1 (no
// viewer job runs inside it). A changed digest counted as an ammo leak and, in
// play, latched FIRE and RELOAD off for the rest of the PROCESS. In the online
// session the boxed guest bumped into the boxed host (downed_defense attack=85
// both ways inside body 1's job): the host's box came off, the support lane saw
// the u16[46] stock table change in the same window, and the host lost the
// guest's shots and reloads until the game restarted. The digest never said
// which field changed.
//
// The block is 14 contiguous vars registrations (mgsvtpp.exe 1.0.15.4,
// 0x99B0BA..0x99B839, contiguity asserted by tools/generate_partner_contract.py;
// names are the Fox StrCode32 of each registration hash, '?' = no known name):
//   +0x548 weapons u16[6]                 weapon
//   +0x554 ammoInWeapons u16[6]           ammo
//   +0x560 ammoSubInWeapons u16[6]        ammo
//   +0x56C ? u8[6]   (hash 0x97F7957B)    weapon
//   +0x572 ? u8[6]   (hash 0x4729E1E7)    weapon
//   +0x578 ? u8[6]   (hash 0x36EFE588)    weapon
//   +0x57E isInitialWeapon u8[6]          weapon
//   +0x584 supportWeapons u16[16]         weapon
//   +0x5A4 items u16[16]                  item
//   +0x5C4 ? u8[16]  (hash 0x114EF593)    weapon (unnamed: never benign)
//   +0x5D4 handEquip u16                  weapon
//   +0x5D6 ? u8[16]  (hash 0xC0625FA8)    weapon (unnamed: never benign)
//   +0x5E6 ammoStockIds u8[46]            ammo, except elements 14..21: item stocks
//   +0x614 ammoStockCounts u16[46]        ammo, except elements 14..21: item stocks
// The item stocks are the inventory's stock words 0xE + sub (sub 0..7,
// partner_player_contract inv_item_stock_base, partner_cbox_policy kSubs); the
// support slot-3 stocks are elements 6..13 (vars+0x620..+0x62F, the support
// lane's episode words).
//
// Verdict: a change confined to item elements (the item list and the item
// stock ids/counts) is benign: the viewer's own item state (its box knocked off
// in a collision processed inside body 1's job); body 1's FIRE and RELOAD, what
// the play strip guards, never write it. Any weapon or ammo element (weapons,
// magazines, ammo and support stocks, the unnamed per-weapon bytes) is a leak:
// no viewer-side ammo change ever slips through.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_ammo_leak {

inline constexpr std::size_t kBegin = 0x548, kEnd = 0x670, kSize = kEnd - kBegin;
inline constexpr unsigned kMembers = 14;
inline constexpr unsigned kStockIds = 12, kStockCounts = 13;      // member indexes
inline constexpr unsigned kItemStockFirst = 0xE, kItemStocks = 8;  // stock elements 14..21

enum class Kind : std::uint8_t { weapon, ammo, item };
struct Member {
    const char* name;
    std::uint16_t offset;  // from vars (kBegin..kEnd)
    std::uint8_t size;     // element bytes (1 or 2)
    std::uint8_t count;
    Kind kind;
};
const Member& MemberAt(unsigned index) noexcept;  // index < kMembers
// One element's own kind (the item stock elements of the two stock tables are items).
Kind ElementKind(unsigned member, unsigned element) noexcept;
const char* KindName(Kind kind) noexcept;

struct Change {
    std::uint8_t member, element;
    std::uint16_t before, after;
};
inline constexpr unsigned kChangesKept = 4;
struct Diff {
    std::uint32_t members = 0;  // bit per member with a changed element
    unsigned elements = 0;      // changed elements in all
    bool weapon = false, ammo = false, item = false;
    unsigned kept = 0;          // the first kChangesKept changes, in block order
    Change first[kChangesKept]{};
};
// `before` and `after` are kSize bytes each (the block from vars+kBegin).
Diff Compare(const std::uint8_t* before, const std::uint8_t* after) noexcept;

enum class Verdict : std::uint8_t { none, benign, leak };
Verdict Classify(const Diff& diff) noexcept;
const char* VerdictName(Verdict verdict) noexcept;
// "ammoStockCounts[17]:7>6,items[2]:468>0" (kept changes; '+' when more); 0 on overflow.
std::size_t FormatChanges(const Diff& diff, char* out, std::size_t capacity) noexcept;

// The play strip (FIRE and RELOAD) from THIS MISSION's leak counters: a shot
// leak, an ammo leak, or the magazine mirror that turned itself off. Every
// mission starts clean (the old latch outlived the mission and the process).
struct MissionLeaks {
    std::uint32_t shot_leaks, ammo_leaks;
    bool mag_leak_off;
};
bool StripsFireReload(bool play_mode, const MissionLeaks& mission) noexcept;

}  // namespace mgscoop::partner_ammo_leak
