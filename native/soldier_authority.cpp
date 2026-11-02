// Original MGS Co-op: enemy sync M7 damage authority (header soldier_authority.hpp). Pure policy plus a lock-free
// ownership bitmap; no game reads or calls.
#include "mgscoop/soldier_authority.hpp"
#include <atomic>

namespace mgscoop::soldier_authority {
namespace {
std::atomic<bool> g_enabled{false}, g_guest{false};
// Two bits per soldier, as two bitmaps: known (Lua named it) and remote.
std::atomic<std::uint64_t> g_known[kSoldiers / 64]{}, g_remote[kSoldiers / 64]{};
} // namespace

Action Decide(bool enabled, std::uint32_t target, std::uint32_t local, std::uint16_t attacker, bool remote) noexcept {
    if (!enabled || target > 1 || local > 1 || !IsSoldier(attacker)) return Action::stock;
    if (remote) return Action::neutralize;
    return target == local ? Action::stock : Action::forward;
}

const char* ActionName(Action action) noexcept {
    switch (action) {
    case Action::neutralize: return "neutralize";
    case Action::forward: return "forward";
    default: return "stock";
    }
}

void Configure(bool enabled, bool guest) noexcept {
    g_guest.store(guest, std::memory_order_release);
    g_enabled.store(enabled, std::memory_order_release);
}

bool Enabled() noexcept { return g_enabled.load(std::memory_order_acquire); }

void Clear() noexcept {
    for (auto& w : g_known) w.store(0, std::memory_order_release);
    for (auto& w : g_remote) w.store(0, std::memory_order_release);
}

bool Set(std::uint16_t index, bool remote) noexcept {
    if (index >= kSoldiers) return false;
    const std::uint64_t bit = 1ull << (index % 64);
    if (remote) g_remote[index / 64].fetch_or(bit, std::memory_order_acq_rel);
    else g_remote[index / 64].fetch_and(~bit, std::memory_order_acq_rel);
    g_known[index / 64].fetch_or(bit, std::memory_order_acq_rel);
    return true;
}

bool Remote(std::uint16_t attacker) noexcept {
    if (!IsSoldier(attacker)) return false;
    const unsigned index = attacker & 0x1FFu;
    const std::uint64_t bit = 1ull << (index % 64);
    if (!(g_known[index / 64].load(std::memory_order_acquire) & bit)) return g_guest.load(std::memory_order_acquire);
    return (g_remote[index / 64].load(std::memory_order_acquire) & bit) != 0;
}

unsigned RemoteCount() noexcept {
    unsigned n = 0;
    for (unsigned i = 0; i < kSoldiers; ++i) n += Remote(static_cast<std::uint16_t>((kSoldierType << 9) | i)) ? 1u : 0u;
    return n;
}

} // namespace mgscoop::soldier_authority
