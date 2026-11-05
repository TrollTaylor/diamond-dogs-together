// Original MGS Co-op: enemy sync M7 damage authority (header soldier_authority.hpp). Pure policy plus a lock-free
// ownership bitmap; no game reads or calls.
#include "mgscoop/soldier_authority.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>

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

namespace {
std::atomic<std::uint32_t> g_defHits[2]{};
std::atomic<std::uint64_t> g_defBy[2][kSoldiers / 64]{};
} // namespace

void NoteDefense(std::uint32_t target, std::uint16_t attacker) noexcept {
    if (target > 1 || !IsSoldier(attacker)) return;
    const unsigned index = attacker & 0x1FFu;
    g_defHits[target].fetch_add(1, std::memory_order_relaxed);
    g_defBy[target][index / 64].fetch_or(1ull << (index % 64), std::memory_order_relaxed);
}

DefenseSnapshot DefenseRead() noexcept {
    DefenseSnapshot s{};
    for (unsigned t = 0; t < 2; ++t) {
        s.hits[t] = g_defHits[t].load(std::memory_order_relaxed);
        for (unsigned w = 0; w < kSoldiers / 64; ++w) s.by[t][w] = g_defBy[t][w].load(std::memory_order_relaxed);
    }
    return s;
}

void DefenseReset() noexcept {
    for (unsigned t = 0; t < 2; ++t) {
        g_defHits[t].store(0, std::memory_order_relaxed);
        for (auto& w : g_defBy[t]) w.store(0, std::memory_order_relaxed);
    }
}

bool DefenseList(const DefenseSnapshot& s, unsigned target, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity || target > 1) return false;
    int n = std::snprintf(out, capacity, "%u:", s.hits[target]);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) return false;
    std::size_t used = static_cast<std::size_t>(n);
    bool first = true;
    for (unsigned i = 0; i < kSoldiers; ++i) {
        if (!(s.by[target][i / 64] & (1ull << (i % 64)))) continue;
        char item[8]{};
        const int k = std::snprintf(item, sizeof(item), "%s%u", first ? "" : ",", i);
        if (k < 0 || static_cast<std::size_t>(k) >= capacity - used) break;
        std::memcpy(out + used, item, static_cast<std::size_t>(k) + 1u);
        used += static_cast<std::size_t>(k);
        first = false;
    }
    return true;
}

} // namespace mgscoop::soldier_authority
