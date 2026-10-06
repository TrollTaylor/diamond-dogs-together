#include "debug_overlay_fixture.hpp"
#include <cstring>
// Separate translation unit, no LTO: these represent real COM method calls
// intercepted through the same MinHook detours used by DirectInput.
__declspec(noinline) HRESULT STDMETHODCALLTYPE DebugFixtureState(IDirectInputDevice8W* raw, DWORD bytes, LPVOID output) {
    auto& value = *reinterpret_cast<DebugInputFixture*>(raw);
    ++value.polls;
    if (SUCCEEDED(value.result) && output) std::memset(output, 0x80, bytes);
    SetLastError(0x12345678);
    return value.result;
}
__declspec(noinline) HRESULT STDMETHODCALLTYPE DebugFixtureData(IDirectInputDevice8W* raw, DWORD bytes, LPDIDEVICEOBJECTDATA output, LPDWORD count, DWORD flags) {
    auto& value = *reinterpret_cast<DebugInputFixture*>(raw);
    ++value.polls; value.last_flags = flags;
    if (SUCCEEDED(value.result) && count) {
        if (output && *count) { std::memset(output, 0x80, bytes); *count = 1; }
        else if (!output) { ++value.flushes; *count = 0; }
    }
    SetLastError(0x12345678);
    return value.result;
}
HRESULT STDMETHODCALLTYPE DebugFixtureInfo(IDirectInputDevice8W* raw, LPDIDEVICEINSTANCEW info) {
    if (!info || info->dwSize != sizeof(*info)) return DIERR_INVALIDPARAM;
    info->dwDevType = reinterpret_cast<DebugInputFixture*>(raw)->type;
    return DI_OK;
}
DebugPadFixture debug_pad_fixture[XUSER_MAX_COUNT]{};
__declspec(noinline) DWORD WINAPI DebugFixturePad(DWORD index, XINPUT_STATE* state) {
    if (index >= XUSER_MAX_COUNT) return ERROR_BAD_ARGUMENTS;
    auto& value = debug_pad_fixture[index]; ++value.polls;
    if (value.result == ERROR_SUCCESS && state) *state = value.state;
    SetLastError(0x87654321);
    return value.result;
}
