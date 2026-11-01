#pragma once
// Debug menu arms (docs/debug-menu.md "Give weapon" and "Friendly fire"; lua/coop/debug_arms.lua). Pure parts:
// the loaded-gun reply Lua asks for (MGS_COOP_ARMS_RESIDENT_V1, a copy of the equip writer's last census, no new
// game read), the Lua state the menu shows (MGS_COOP_ARMS_STATE_V1), the support list and the command text.
// The store is SRW-locked: Lua's thread writes it, the menu's render thread reads it.
#include <cstddef>
#include <cstdint>

namespace mgscoop::debug_arms {
inline constexpr unsigned kMaxGuns = 16;
inline constexpr std::uint16_t kAliasFirst = 0x230;  // ids from here on are customized aliases (never listed)
struct Gun {
    std::uint16_t equip = 0;
    std::uint8_t type = 0;   // 1 Assault rifle .. 8 Missile (partner_slots BulletType numbering)
    char code[25]{};         // the TppEquip name without EQP_WP_, [A-Za-z0-9_]
};
struct State {
    bool known = false;          // a state was received in this process
    bool friendly_fire = true;   // the host's switch (default on)
    std::uint32_t mission = 0;   // the mission the gun list belongs to
    std::uint32_t count = 0;
    Gun guns[kMaxGuns]{};
};
// One census entry (equipment_stock_writer::ResidentEntry fields the reply needs).
struct Entry {
    std::uint16_t equip = 0;
    std::uint8_t type = 0, flags = 0;
};
// "return {mission,id,type,...}" for the protected (flags 3) stock guns (id < 0x230, type 1..8), at most kMaxGuns;
// "return nil" without a census. Returns the length written (0 when it does not fit).
std::size_t FormatResidentReply(bool census_ok, std::uint32_t mission, const Entry* entries, std::size_t count, char* out,
                                std::size_t capacity) noexcept;
// The fields after "return 'MGS_COOP_ARMS_STATE_V1' -- ": "<ff 0|1> <mission> <n> [<id>.<type>.<code>]..." exactly n.
bool ParseState(const char* text, std::size_t size, State& out) noexcept;
void Store(const State& value) noexcept;
State Get() noexcept;
// "Assault rifle", "Handgun", "SMG", "Sniper rifle", "Shotgun", "Machine gun", "Grenade launcher", "Missile"; else nullptr.
const char* KindName(std::uint8_t type) noexcept;
// "Assault rifle (East_ar_010)".
bool FormatGunLabel(const Gun& gun, char* out, std::size_t capacity) noexcept;
// The stock support items the picker offers (the common equip block): token "EQP_SWP_<name>" and a label.
struct Support {
    const char* token;
    const char* label;
};
const Support* SupportItems(std::size_t& count) noexcept;
// "give_weapon 39", "give_weapon EQP_SWP_C4" (a listed support token only), "friendly_fire 0|1".
bool FormatGiveGun(char* out, std::size_t capacity, std::uint16_t equip) noexcept;
bool FormatGiveSupport(char* out, std::size_t capacity, const char* token) noexcept;
bool FormatFriendlyFire(char* out, std::size_t capacity, bool on) noexcept;
} // namespace mgscoop::debug_arms
