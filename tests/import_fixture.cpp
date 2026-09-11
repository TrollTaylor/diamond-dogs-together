#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static volatile LONG call_count = 0;
static volatile LONG incoming_error = 0;
extern "C" __declspec(dllexport) void __cdecl MgsFixtureCallback() {
    InterlockedExchange(&incoming_error, static_cast<LONG>(GetLastError()));
    InterlockedIncrement(&call_count);
    SetLastError(0x1234u);
}
extern "C" __declspec(dllexport) LONG __cdecl MgsFixtureCount() { return call_count; }
extern "C" __declspec(dllexport) DWORD __cdecl MgsFixtureIncomingError() { return static_cast<DWORD>(incoming_error); }
