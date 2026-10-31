// The game window's close against the partner's mission teardown (window_close.hpp).
#include "mgscoop/window_close.hpp"
#include <atomic>

namespace mgscoop::window_close {
namespace {
// The teardown: requested by the window thread, claimed then done by the mission thread.
enum : std::uint8_t { kNone, kRequested, kClaimed, kDone };
// The close: only the window thread moves it out of open; held -> released is one compare-exchange, so exactly
// one side (the mission thread's Finish, the timer or a WM_CLOSE past the deadline) posts or forwards the close.
enum : std::uint8_t { kOpen, kHeld, kReleased };
std::atomic<std::uint8_t> teardown{kNone}, close{kOpen};
std::atomic<std::uint64_t> deadline{0};

bool Release() noexcept {
    std::uint8_t held = kHeld;
    return close.compare_exchange_strong(held, kReleased);
}
} // namespace

bool NeedsMissionThread(bool partner_on, std::uint32_t partner_mission, bool mission_running,
                        std::uint32_t mission_thread, std::uint32_t current_thread) noexcept {
    return partner_on && partner_mission && mission_running && mission_thread && mission_thread != current_thread;
}

void Request() noexcept {
    std::uint8_t none = kNone;
    teardown.compare_exchange_strong(none, kRequested);
}

Close OnClose(std::uint64_t now_ms) noexcept {
    const std::uint8_t state = close.load();
    if (state == kReleased) return Close::forward;
    if (state == kHeld) return now_ms >= deadline.load() && Release() ? Close::forward : Close::hold;
    const std::uint8_t work = teardown.load();
    if (work != kRequested && work != kClaimed) {
        close.store(kReleased);
        return Close::forward;
    }
    deadline.store(now_ms + kHoldMs);
    close.store(kHeld);
    // A Finish that ran before the store found nothing held and posted nothing: end the hold here instead. A
    // Finish after it ends the hold itself (and this Release fails), so exactly one close goes on.
    return teardown.load() == kDone && Release() ? Close::forward : Close::hold;
}

void OnDestroy() noexcept { close.store(kReleased); }

Timer OnTimer(std::uint64_t now_ms) noexcept {
    if (close.load() != kHeld) return Timer::stop;
    if (now_ms < deadline.load()) return Timer::wait;
    return Release() ? Timer::release : Timer::stop;
}

bool Claim() noexcept {
    std::uint8_t requested = kRequested;
    return teardown.compare_exchange_strong(requested, kClaimed);
}

bool Finish() noexcept {
    teardown.store(kDone);
    return Release();
}

bool Quiet() noexcept { return teardown.load() != kNone; }

void ResetForTest() noexcept {
    teardown.store(kNone);
    close.store(kOpen);
    deadline.store(0);
}

} // namespace mgscoop::window_close
