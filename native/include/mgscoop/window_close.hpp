#pragma once
// The game window's close against the partner's mission teardown (docs/STATUS.md "Teardown AV at close").
//
// WM_CLOSE (the worker's shutdown, a player's Alt+F4 or X) reaches the game window on its own thread, which is
// not the mission (main) thread. PartnerShutdown turned the partner hooks off there but could not run the
// mission teardown (teardown=other_thread in all 208 archived closes), so the engine destroyed the mission with
// the partner's body still realized and its drive armed. About one close in eight then faulted on the mission
// thread: rva 0xbf421a (a GameObject SendCommand after the engine had dropped its registry), then 0x1be23eb.
//
// Now a WM_CLOSE that finds a partner mission running on another thread is held until the mission thread has
// claimed the partner's teardown at its next partner tick (the stop of a natural MISSION_TERMINATING, then the
// mission end), or until kHoldMs pass. Exactly one WM_CLOSE then reaches the game, posted by whichever side
// ended the hold. WM_DESTROY / WM_ENDSESSION cannot wait and go on at once; the mission thread may still claim
// the teardown afterwards. Process-wide and lock-free; the transitions take the caller's clock.
#include <cstdint>

namespace mgscoop::window_close {

inline constexpr std::uint64_t kHoldMs = 1000;

// PartnerShutdown's verdict on the window thread: a partner mission runs (not loading, not terminating) on a
// mission thread other than this one, so its teardown must run there before the game sees the close.
bool NeedsMissionThread(bool partner_on, std::uint32_t partner_mission, bool mission_running,
                        std::uint32_t mission_thread, std::uint32_t current_thread) noexcept;
// Window thread (PartnerShutdown): asks the mission thread for the teardown. Idempotent.
void Request() noexcept;

enum class Close : std::uint8_t { forward, hold };
// Window thread, every WM_CLOSE (after PartnerShutdown on the first one). hold: swallow it (return 0) and,
// the first time, arm the hold timer for kHoldMs.
Close OnClose(std::uint64_t now_ms) noexcept;
// Window thread, WM_DESTROY / WM_ENDSESSION: nothing waits from here on.
void OnDestroy() noexcept;

enum class Timer : std::uint8_t { wait, stop, release };
// Window thread, the hold timer. release: this caller ended the hold and posts the one WM_CLOSE.
Timer OnTimer(std::uint64_t now_ms) noexcept;

// Mission thread, at the top of every partner tick. True once: run the teardown now, then call Finish.
bool Claim() noexcept;
// Mission thread, after the teardown. True: this caller ended the hold and posts the one WM_CLOSE.
bool Finish() noexcept;
// A teardown was requested: the partner tick and CFG stay quiet from here on.
bool Quiet() noexcept;

void ResetForTest() noexcept;

} // namespace mgscoop::window_close
