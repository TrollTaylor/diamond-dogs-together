#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <xinput.h>
struct DebugInputFixture {
    void** methods{};
    HRESULT result = DI_OK;
    unsigned polls{}, flushes{};
    DWORD last_flags{};
    DWORD type = DI8DEVTYPE_KEYBOARD;
};
HRESULT STDMETHODCALLTYPE DebugFixtureState(IDirectInputDevice8W*, DWORD, LPVOID);
HRESULT STDMETHODCALLTYPE DebugFixtureData(IDirectInputDevice8W*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
HRESULT STDMETHODCALLTYPE DebugFixtureInfo(IDirectInputDevice8W*, LPDIDEVICEINSTANCEW);
struct DebugPadFixture {
    XINPUT_STATE state{};
    DWORD result = ERROR_DEVICE_NOT_CONNECTED;
    unsigned polls{};
};
extern DebugPadFixture debug_pad_fixture[XUSER_MAX_COUNT];
DWORD WINAPI DebugFixturePad(DWORD, XINPUT_STATE*);
