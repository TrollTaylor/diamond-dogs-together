// Original executable used only to test the debugger observer. No Steam SDK.
#define WIN32_LEAN_AND_MEAN
#include "steam_init_fixture_api.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>

extern "C" std::uint64_t FixtureCall();
extern "C" void** FixtureIatAddress();
extern "C" unsigned char FixtureBeforeCall;

namespace {
FixtureMode mode = FixtureMode::True;
HANDLE completed_event = nullptr;
HANDLE release_event = nullptr;
std::uint64_t raw_result = 0;
volatile LONG caught_exceptions = 0;
volatile LONG foreign_calls = 0;
volatile LONG nohit_resumed = 0;
bool (*original_init)() = nullptr;
bool foreign_init() {
    InterlockedIncrement(&foreign_calls);
    return original_init();
}
bool set_import(void* function) {
    auto slot = FixtureIatAddress();
    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) return false;
    InterlockedExchangePointer(slot, function);
    DWORD ignored = 0;
    return VirtualProtect(slot, sizeof(*slot), previous, &ignored) != FALSE;
}
__declspec(noinline) bool invoke_once(std::uint64_t* value) {
    __try {
        *value = FixtureCall();
        return true;
    } __except (GetExceptionCode() == fixture_unwind_exception ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        InterlockedIncrement(&caught_exceptions);
        return false;
    }
}
DWORD WINAPI worker_main(void*) {
    if (mode == FixtureMode::NoHit) {
        const auto deadline = GetTickCount64() + 45000;
        while (IsDebuggerPresent() && GetTickCount64() < deadline) Sleep(10);
        if (IsDebuggerPresent()) return 4;
        InterlockedExchange(&nohit_resumed, 1);
    }
    if (mode == FixtureMode::Repeated) {
        std::uint64_t discarded = 0;
        if (invoke_once(&discarded)) return 5;
    }
    if (!invoke_once(&raw_result)) return 6;
    SetEvent(completed_event);
    WaitForSingleObject(release_event, 30000);
    return 0;
}
bool parse_mode(const wchar_t* name) {
    struct Entry { const wchar_t* name; FixtureMode mode; };
    const Entry modes[] = {{L"True", FixtureMode::True}, {L"False", FixtureMode::False},
        {L"Nested", FixtureMode::Nested}, {L"OtherThread", FixtureMode::OtherThread},
        {L"Repeated", FixtureMode::Repeated}, {L"WrongOwner", FixtureMode::WrongOwner},
        {L"NoHit", FixtureMode::NoHit}, {L"ReturnTimeout", FixtureMode::ReturnTimeout}};
    for (const auto& candidate : modes) {
        if (wcscmp(name, candidate.name) == 0) { mode = candidate.mode; return true; }
    }
    return false;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || !parse_mode(argv[2])) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    FixtureConfigure(mode, FixtureCall);
    auto module = GetModuleHandleW(L"steam_init_fixture_api.dll");
    original_init = reinterpret_cast<bool (*)()>(GetProcAddress(module, "FixtureSteamInit"));
    if (!original_init || *FixtureIatAddress() != reinterpret_cast<void*>(original_init)) return 3;
    std::array<unsigned char, 8> call_bytes{};
    std::array<unsigned char, 17> api_bytes{};
    std::memcpy(call_bytes.data(), &FixtureBeforeCall, call_bytes.size());
    std::memcpy(api_bytes.data(), reinterpret_cast<const void*>(original_init), api_bytes.size());
    if (mode == FixtureMode::WrongOwner && !set_import(reinterpret_cast<void*>(&foreign_init))) return 4;
    completed_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    release_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!completed_event || !release_event) return 5;
    HANDLE worker = CreateThread(nullptr, 0, worker_main, nullptr, 0, nullptr);
    if (!worker) return 6;
    const auto wait = WaitForSingleObject(completed_event, 50000);
    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS | CONTEXT_CONTROL;
    const auto previous_suspend = SuspendThread(worker);
    const bool context_read = previous_suspend != static_cast<DWORD>(-1) && GetThreadContext(worker, &context);
    if (previous_suspend != static_cast<DWORD>(-1)) ResumeThread(worker);
    FixtureApiState state{};
    FixtureReadState(&state);
    const bool detached = !IsDebuggerPresent();
    const bool registers_clear = context_read && (context.Dr7 & 0xffu) == 0;
    const bool bytes_preserved = std::memcmp(call_bytes.data(), &FixtureBeforeCall, call_bytes.size()) == 0 &&
        std::memcmp(api_bytes.data(), reinterpret_cast<const void*>(original_init), api_bytes.size()) == 0;
    const bool restored = mode != FixtureMode::WrongOwner || set_import(reinterpret_cast<void*>(original_init));
    const bool iat_original = *FixtureIatAddress() == reinterpret_cast<void*>(original_init);
    const bool expected_bool = mode != FixtureMode::False;
    const bool bool_preserved = (raw_result & 0xffu) == static_cast<unsigned>(expected_bool);
    const bool upper_bits_preserved = (raw_result & ~std::uint64_t(0xff)) == fixture_upper_bits;
    const LONG expected_calls = mode == FixtureMode::Nested || mode == FixtureMode::OtherThread || mode == FixtureMode::Repeated ? 2 : 1;
    const bool nested_ok = mode != FixtureMode::Nested || (state.nested_calls == 1 && state.inner_raw == fixture_upper_bits);
    const bool other_ok = mode != FixtureMode::OtherThread || (state.other_thread_calls == 1 && state.inner_raw == fixture_upper_bits);
    const bool repeated_ok = caught_exceptions == (mode == FixtureMode::Repeated ? 1 : 0);
    const bool timeout_ok = mode != FixtureMode::ReturnTimeout || (state.wait_entered == 1 && state.resumed_after_detach == 1);
    const bool nohit_ok = mode != FixtureMode::NoHit || nohit_resumed == 1;
    const bool owner_ok = mode != FixtureMode::WrongOwner || foreign_calls == 1;
    // The replacement is a C++ bool wrapper and may canonicalize upper bits.
    const bool raw_ok = mode == FixtureMode::WrongOwner || upper_bits_preserved;
    const bool passed = wait == WAIT_OBJECT_0 && detached && registers_clear && bytes_preserved && restored &&
        iat_original && previous_suspend == 0 && state.calls == expected_calls && bool_preserved && raw_ok &&
        nested_ok && other_ok && repeated_ok && timeout_ok && nohit_ok && owner_ok;
    FILE* file = nullptr;
    if (_wfopen_s(&file, argv[1], L"wb") != 0 || !file) return 7;
    std::fprintf(file, "{\"passed\":%s,\"calls\":%ld,\"bool_preserved\":%s,\"raw_result\":%llu,\"upper_bits_preserved\":%s,"
        "\"debugger_detached\":%s,\"registers_clear\":%s,\"bytes_preserved\":%s,\"iat_restored\":%s,"
        "\"wait_result\":%lu,\"previous_suspend_count\":%lu,\"nested_calls\":%ld,\"other_thread_calls\":%ld,"
        "\"inner_raw\":%llu,\"caught_exceptions\":%ld,\"foreign_calls\":%ld,\"wait_entered\":%ld,\"resumed_after_detach\":%s}\n",
        passed ? "true" : "false", state.calls, bool_preserved ? "true" : "false", static_cast<unsigned long long>(raw_result),
        upper_bits_preserved ? "true" : "false", detached ? "true" : "false", registers_clear ? "true" : "false",
        bytes_preserved ? "true" : "false", iat_original && restored ? "true" : "false", wait, previous_suspend,
        state.nested_calls, state.other_thread_calls, static_cast<unsigned long long>(state.inner_raw), caught_exceptions,
        foreign_calls, state.wait_entered, state.resumed_after_detach || nohit_resumed ? "true" : "false");
    std::fclose(file);
    SetEvent(release_event);
    WaitForSingleObject(worker, 10000);
    CloseHandle(worker);
    CloseHandle(completed_event);
    CloseHandle(release_event);
    return passed ? 0 : 1;
}
