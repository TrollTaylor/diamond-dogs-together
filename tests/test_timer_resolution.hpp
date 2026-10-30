// Test executables that poll with short Sleep() calls ask Windows for a 1 ms timer
// resolution while they run. At the default resolution (15.6 ms) Sleep(2) sleeps
// about 15.6 ms, so a fixture that pumps a fake clock 1000 times with Sleep(2) in
// between spends 15 s waiting instead of 3 s. Windows 11 drops the request of a
// process without a visible window after about 2.5 s (Sleep(2) back at 15.4 ms,
// measured 2026-10-30) unless the process opts out of that power throttling
// (PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION with the state bit clear).
// Only this test process is affected: timeBeginPeriod is per process since
// Windows 10 2004, and both settings end with the process (or the scope).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

namespace mgscoop::test {

class FineTimerResolution {
public:
    FineTimerResolution() noexcept {
        PROCESS_POWER_THROTTLING_STATE state{};
        state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        state.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        state.StateMask = 0;   // keep honouring this process's timer resolution
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof state);
        active_ = timeBeginPeriod(1) == TIMERR_NOERROR;
    }
    ~FineTimerResolution() {
        if (active_) timeEndPeriod(1);
    }
    FineTimerResolution(const FineTimerResolution&) = delete;
    FineTimerResolution& operator=(const FineTimerResolution&) = delete;

private:
    bool active_ = false;
};

}  // namespace mgscoop::test
