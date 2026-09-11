#define WIN32_LEAN_AND_MEAN
#include "steam_init_fixture_api.hpp"

namespace {
FixtureMode mode = FixtureMode::True;
FixtureInvoke invoke = nullptr;
volatile LONG calls = 0;
volatile LONG nested_calls = 0;
volatile LONG other_thread_calls = 0;
volatile LONG wait_entered = 0;
volatile LONG resumed_after_detach = 0;
std::uint64_t inner_raw = 0;
thread_local unsigned nesting = 0;
DWORD WINAPI other_worker(void*) {
    InterlockedIncrement(&other_thread_calls);
    inner_raw = invoke();
    return 0;
}
}
extern "C" void FixtureConfigure(FixtureMode selected, FixtureInvoke callback) {
    mode = selected;
    invoke = callback;
}
extern "C" void FixtureReadState(FixtureApiState* state) {
    *state = {InterlockedCompareExchange(&calls, 0, 0), InterlockedCompareExchange(&nested_calls, 0, 0),
              InterlockedCompareExchange(&other_thread_calls, 0, 0), InterlockedCompareExchange(&wait_entered, 0, 0),
              InterlockedCompareExchange(&resumed_after_detach, 0, 0), inner_raw};
}
// Called only by the fixture's assembly API export. Its upper result bits are
// deliberately replaced by that wrapper so an EAX-based boolean check fails.
extern "C" bool FixtureSteamInitBody() {
    const auto call = InterlockedIncrement(&calls);
    if (mode == FixtureMode::Repeated && call == 1) {
        RaiseException(fixture_unwind_exception, 0, 0, nullptr);
        return false;
    }
    if (mode == FixtureMode::Nested) {
        if (nesting) return false;
        ++nesting;
        InterlockedIncrement(&nested_calls);
        inner_raw = invoke();
        --nesting;
        return true;
    }
    if (mode == FixtureMode::OtherThread) {
        if (call != 1) return false;
        HANDLE worker = CreateThread(nullptr, 0, other_worker, nullptr, 0, nullptr);
        if (!worker) return false;
        const auto wait = WaitForSingleObject(worker, 30000);
        CloseHandle(worker);
        return wait == WAIT_OBJECT_0;
    }
    if (mode == FixtureMode::ReturnTimeout) {
        InterlockedExchange(&wait_entered, 1);
        const auto deadline = GetTickCount64() + 45000;
        while (IsDebuggerPresent() && GetTickCount64() < deadline) Sleep(10);
        if (IsDebuggerPresent()) return false;
        InterlockedExchange(&resumed_after_detach, 1);
    }
    return mode != FixtureMode::False;
}
