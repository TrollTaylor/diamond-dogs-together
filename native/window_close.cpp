// The game window's close against the partner's mission teardown (window_close.hpp).
#include "mgscoop/window_close.hpp"

namespace mgscoop::window_close {

// Today's behaviour: the window thread never hands the teardown over and every close goes straight on.
bool NeedsMissionThread(bool, std::uint32_t, bool, std::uint32_t, std::uint32_t) noexcept { return false; }
void Request() noexcept {}
Close OnClose(std::uint64_t) noexcept { return Close::forward; }
void OnDestroy() noexcept {}
Timer OnTimer(std::uint64_t) noexcept { return Timer::stop; }
bool Claim() noexcept { return false; }
bool Finish() noexcept { return false; }
bool Quiet() noexcept { return false; }
void ResetForTest() noexcept {}

} // namespace mgscoop::window_close
