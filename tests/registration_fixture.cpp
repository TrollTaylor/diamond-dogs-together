// An original target for validating the debugger command before using MGSV.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <array>
#include <cstring>

namespace {
HANDLE returned_event = nullptr;
HANDLE release_event = nullptr;
volatile LONG calls = 0;
volatile LONG correct_argument = 0;
volatile LONG result = 0;
volatile LONG resumed_after_detach = 0;
volatile LONG nested_inner_result = 0;
volatile LONG return_wait_entered = 0;
bool no_hit = false;
bool nested_return = false;
bool return_timeout = false;
constexpr std::uintptr_t argument_token = 0x12345678;
}
__declspec(noinline) int InvokeRegistrar(void* state);
extern "C" __declspec(dllexport) __declspec(noinline) int FixtureRegistrar(void* state) {
    const LONG invocation = InterlockedIncrement(&calls);
    if (reinterpret_cast<std::uintptr_t>(state) == argument_token) InterlockedIncrement(&correct_argument);
    if (return_timeout) {
        // Entry capture has already resumed this function with its persistent
        // return breakpoint armed. Wait inside the original call until the
        // harness's strict deadline helper detaches the debugger.
        if (IsDebuggerPresent()) InterlockedExchange(&return_wait_entered, 1);
        const auto deadline = GetTickCount64() + 45000;
        while (IsDebuggerPresent() && GetTickCount64() < deadline) Sleep(10);
        if (IsDebuggerPresent()) return -1;
        InterlockedExchange(&resumed_after_detach, 1);
    }
    if (nested_return && invocation == 1) {
        // Reenter through the SAME call instruction as the outer invocation.
        // The inner return has the same TID/address but a different stack frame.
        InterlockedExchange(&nested_inner_result, InvokeRegistrar(state));
    }
    return 37;
}
__declspec(noinline) int InvokeRegistrar(void* state) {
    // A volatile result keeps the real call/return boundary, preventing a tail
    // call that would otherwise give the two invocations different return sites.
    volatile int returned_value = FixtureRegistrar(state);
    return returned_value;
}
DWORD WINAPI fixture_worker(void*) {
    if (no_hit) {
        const auto deadline = GetTickCount64() + 45000;
        while (IsDebuggerPresent() && GetTickCount64() < deadline) Sleep(10);
        if (IsDebuggerPresent()) return 4;
        InterlockedExchange(&resumed_after_detach, 1);
    }
    result = InvokeRegistrar(reinterpret_cast<void*>(argument_token));
    SetEvent(returned_event);
    WaitForSingleObject(release_event, 10000);
    return 0;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) return 2;
    if (argc == 3) {
        if (wcscmp(argv[2], L"--no-hit") == 0) no_hit = true;
        else if (wcscmp(argv[2], L"--nested-return") == 0) nested_return = true;
        else if (wcscmp(argv[2], L"--return-timeout") == 0) return_timeout = true;
        else return 2;
    }
    // A failing diagnostic should report through the harness, not leave a
    // system-owned error dialog over the user's game.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::array<unsigned char, 17> original_bytes{};
    memcpy(original_bytes.data(), reinterpret_cast<const void*>(&FixtureRegistrar), original_bytes.size());
    returned_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    release_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE worker = CreateThread(nullptr, 0, fixture_worker, nullptr, 0, nullptr);
    if (!returned_event || !release_event || !worker) return 3;
    const DWORD wait_result = WaitForSingleObject(returned_event, (no_hit || return_timeout) ? 50000 : 10000);
    bool ok = wait_result == WAIT_OBJECT_0;
    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS | CONTEXT_CONTROL;
    const auto suspended = SuspendThread(worker);
    const bool context_read = suspended != static_cast<DWORD>(-1) && GetThreadContext(worker, &context);
    if (suspended != static_cast<DWORD>(-1)) ResumeThread(worker);
    const bool debugger_detached = !IsDebuggerPresent();
    const bool no_enabled_breakpoints = context_read && (context.Dr7 & 0xffu) == 0;
    const bool bytes_preserved = memcmp(original_bytes.data(), reinterpret_cast<const void*>(&FixtureRegistrar), original_bytes.size()) == 0;
    const LONG expected_calls = nested_return ? 2 : 1;
    const bool arguments_preserved = correct_argument == expected_calls;
    const bool nested_result_verified = !nested_return || nested_inner_result == 37;
    const bool return_timeout_verified = !return_timeout || (return_wait_entered == 1 && resumed_after_detach == 1);
    ok = ok && calls == expected_calls && arguments_preserved && result == 37 && debugger_detached && no_enabled_breakpoints && bytes_preserved && (!no_hit || resumed_after_detach == 1) && nested_result_verified && return_timeout_verified;
    FILE* output = nullptr;
    if (_wfopen_s(&output, argv[1], L"wb") != 0 || !output) ok = false;
    if (output) {
        fprintf(output, "{\"passed\":%s,\"original_calls\":%ld,\"argument_preserved\":%s,\"result\":%ld,\"debugger_detached\":%s,\"breakpoints_disabled\":%s,\"entry_bytes_preserved\":%s,\"wait_result\":%lu,\"previous_suspend_count\":%lu,\"worker_rip\":\"0x%llx\",\"resumed_after_detach\":%s,\"nested_return\":%s,\"nested_inner_result\":%ld,\"nested_result_verified\":%s,\"return_timeout\":%s,\"return_wait_entered\":%s,\"return_timeout_verified\":%s}\n",
                ok ? "true" : "false", calls, arguments_preserved ? "true" : "false", result,
                debugger_detached ? "true" : "false", no_enabled_breakpoints ? "true" : "false", bytes_preserved ? "true" : "false",
                wait_result, suspended, static_cast<unsigned long long>(context.Rip), resumed_after_detach ? "true" : "false",
                nested_return ? "true" : "false", nested_inner_result, nested_result_verified ? "true" : "false",
                return_timeout ? "true" : "false", return_wait_entered ? "true" : "false", return_timeout_verified ? "true" : "false");
        fclose(output);
    }
    SetEvent(release_event);
    WaitForSingleObject(worker, 10000);
    CloseHandle(worker);
    CloseHandle(returned_event);
    CloseHandle(release_event);
    return ok ? 0 : 1;
}
