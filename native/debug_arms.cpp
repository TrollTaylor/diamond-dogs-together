// Debug menu arms (docs/debug-menu.md "Give weapon" and "Friendly fire"; lua/coop/debug_arms.lua): the loaded-gun
// reply, the Lua state the menu shows, the support list and the command text. No game call, no new read site: the
// reply formats a copy of the equip writer's last census.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "mgscoop/debug_arms.hpp"
#include <cstdio>
#include <cstring>

namespace mgscoop::debug_arms {
namespace {
SRWLOCK lock = SRWLOCK_INIT;
State stored{};
constexpr Support kSupport[] = {
    {"EQP_SWP_Grenade", "Grenade"},
    {"EQP_SWP_StunGrenade", "Stun grenade"},
    {"EQP_SWP_SmokeGrenade", "Smoke grenade"},
    {"EQP_SWP_SleepingGusGrenade", "Sleeping gas grenade"},
    {"EQP_SWP_Decoy", "Decoy"},
    {"EQP_SWP_C4", "C4"},
    {"EQP_SWP_Magazine", "Empty magazine"},
};
bool IsGun(std::uint32_t equip, std::uint32_t type) noexcept { return equip && equip < kAliasFirst && type >= 1 && type <= 8; }
bool Put(std::size_t capacity, std::size_t& used, int written) noexcept {
    if (written <= 0 || static_cast<std::size_t>(written) >= capacity - used) return false;
    used += static_cast<std::size_t>(written);
    return true;
}
// One unsigned decimal (no leading zero, at most `max`) that ends at `stop` or at `end`.
bool Number(const char*& p, const char* end, std::uint32_t max, std::uint32_t& out, char stop) noexcept {
    std::uint64_t v = 0;
    const char* start = p;
    while (p < end && *p >= '0' && *p <= '9' && p - start < 10) v = v * 10 + static_cast<unsigned>(*p++ - '0');
    if (p == start || v > max || (p - start > 1 && *start == '0')) return false;
    if (p < end && *p != stop) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}
} // namespace

std::size_t FormatResidentReply(bool census_ok, std::uint32_t mission, const Entry* entries, std::size_t count, char* out,
                                std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    out[0] = 0;
    if (!census_ok || !entries) {
        static constexpr char none[] = "return nil";
        if (capacity < sizeof(none)) return 0;
        std::memcpy(out, none, sizeof(none));
        return sizeof(none) - 1;
    }
    std::size_t used = 0;
    if (!Put(capacity, used, std::snprintf(out, capacity, "return {%u", unsigned(mission)))) { out[0] = 0; return 0; }
    unsigned listed = 0;
    for (std::size_t i = 0; i < count && listed < kMaxGuns; ++i) {
        const auto& e = entries[i];
        if (e.flags != 3 || !IsGun(e.equip, e.type)) continue;
        if (!Put(capacity, used, std::snprintf(out + used, capacity - used, ",%u,%u", unsigned(e.equip), unsigned(e.type)))) {
            out[0] = 0;
            return 0;
        }
        ++listed;
    }
    if (capacity - used < 2) { out[0] = 0; return 0; }
    out[used++] = '}';
    out[used] = 0;
    return used;
}

bool ParseState(const char* text, std::size_t size, State& out) noexcept {
    out = State{};
    if (!text || !size || size > 700) return false;
    const char* p = text;
    const char* end = text + size;
    State s{};
    std::uint32_t ff = 0, n = 0;
    if (!Number(p, end, 1, ff, ' ') || p == end) return false;
    ++p;
    if (!Number(p, end, 99999, s.mission, ' ') || p == end) return false;
    ++p;
    if (!Number(p, end, kMaxGuns, n, ' ')) return false;
    for (std::uint32_t i = 0; i < n; ++i) {
        if (p == end || *p != ' ') return false;
        ++p;
        std::uint32_t equip = 0, type = 0;
        if (!Number(p, end, kAliasFirst - 1u, equip, '.') || p == end) return false;
        ++p;
        if (!Number(p, end, 8, type, '.') || p == end) return false;
        ++p;
        if (!IsGun(equip, type)) return false;
        auto& g = s.guns[i];
        g.equip = static_cast<std::uint16_t>(equip);
        g.type = static_cast<std::uint8_t>(type);
        std::size_t len = 0;
        while (p < end && *p != ' ') {
            const char c = *p;
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            if (!ok || len + 1 >= sizeof(g.code)) return false;
            g.code[len++] = c;
            ++p;
        }
        if (!len) return false;
    }
    if (p != end) return false;
    s.count = n;
    s.friendly_fire = ff == 1;
    s.known = true;
    out = s;
    return true;
}

void Store(const State& value) noexcept {
    AcquireSRWLockExclusive(&lock);
    stored = value;
    ReleaseSRWLockExclusive(&lock);
}
State Get() noexcept {
    AcquireSRWLockShared(&lock);
    const State copy = stored;
    ReleaseSRWLockShared(&lock);
    return copy;
}

const char* KindName(std::uint8_t type) noexcept {
    static constexpr const char* kNames[] = {"Assault rifle", "Handgun", "SMG", "Sniper rifle", "Shotgun", "Machine gun",
                                             "Grenade launcher", "Missile"};
    return type >= 1 && type <= 8 ? kNames[type - 1] : nullptr;
}
bool FormatGunLabel(const Gun& gun, char* out, std::size_t capacity) noexcept {
    const char* kind = KindName(gun.type);
    if (!out || !capacity || !kind) return false;
    const int n = std::snprintf(out, capacity, "%s (%s)", kind, gun.code);
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}
const Support* SupportItems(std::size_t& count) noexcept {
    count = sizeof(kSupport) / sizeof(kSupport[0]);
    return kSupport;
}
bool FormatGiveGun(char* out, std::size_t capacity, std::uint16_t equip) noexcept {
    if (!out || !equip || equip >= kAliasFirst) return false;
    const int n = std::snprintf(out, capacity, "give_weapon %u", unsigned(equip));
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}
bool FormatGiveSupport(char* out, std::size_t capacity, const char* token) noexcept {
    if (!out || !token) return false;
    for (const auto& s : kSupport) {
        if (std::strcmp(s.token, token) != 0) continue;
        const int n = std::snprintf(out, capacity, "give_weapon %s", s.token);
        return n > 0 && static_cast<std::size_t>(n) < capacity;
    }
    return false;
}
bool FormatFriendlyFire(char* out, std::size_t capacity, bool on) noexcept {
    if (!out) return false;
    const int n = std::snprintf(out, capacity, "friendly_fire %d", on ? 1 : 0);
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}
} // namespace mgscoop::debug_arms
