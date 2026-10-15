// Original MGS Co-op automatic save handoff: shared UI state (join-fix phase 2
// P2-14). Contracts stage: a minimal working implementation so the other
// tracks link and test against the header.
// track A replaces this
#include "mgscoop/save_handoff_state.hpp"
#include <atomic>

namespace mgscoop::save_handoff_state {
namespace {
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
View g_view{};
std::atomic<bool> g_keep{false};
std::atomic<bool> g_unlocked{false};

constexpr const char* kTokenNames[kTokenCount] = {
    "off", "idle", "same", "wait_host", "offered", "sending", "partner_staged", "partner_restarting",
    "receiving", "staged", "countdown", "exiting", "kept", "refused", "failed", "applied",
};

void Lock() noexcept {
    while (g_lock.test_and_set(std::memory_order_acquire)) {
    }
}
void Unlock() noexcept { g_lock.clear(std::memory_order_release); }
}  // namespace

const char* TokenName(std::uint8_t token) noexcept { return token < kTokenCount ? kTokenNames[token] : kTokenNames[0]; }

void Publish(const View& view) noexcept {
    Lock();
    g_view = view;
    Unlock();
}

View Get() noexcept {
    Lock();
    View view = g_view;
    Unlock();
    view.travel_unlocked = g_unlocked.load(std::memory_order_acquire);
    return view;
}

void RequestKeepOwnSave() noexcept { g_keep.store(true, std::memory_order_release); }
bool TakeKeepOwnSave() noexcept { return g_keep.exchange(false, std::memory_order_acq_rel); }

void SetTravelUnlocked(bool unlocked) noexcept { g_unlocked.store(unlocked, std::memory_order_release); }
bool TravelUnlocked() noexcept { return g_unlocked.load(std::memory_order_acquire); }

void ResetForTest() noexcept {
    Lock();
    g_view = View{};
    Unlock();
    g_keep.store(false, std::memory_order_release);
    g_unlocked.store(false, std::memory_order_release);
}

}  // namespace mgscoop::save_handoff_state
