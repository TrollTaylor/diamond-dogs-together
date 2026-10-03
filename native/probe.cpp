// Original MGS Co-op loader with gated Steam import observation. No gameplay modifications.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <winver.h>
#include <strsafe.h>
#include "mgscoop/steam_observer.hpp"

#if !defined(_M_X64) && !defined(__x86_64__)
#error The MGS Co-op probe supports only Windows x64.
#endif

#ifndef MGS_COOP_PROBE_VERSION
#define MGS_COOP_PROBE_VERSION "0.10.33"
#endif

namespace {
constexpr DWORD kPathCapacity = 32768;
INIT_ONCE load_once = INIT_ONCE_STATIC_INIT;
INIT_ONCE probe_once = INIT_ONCE_STATIC_INIT;
HMODULE system_dinput = nullptr;
HRESULT load_error = E_FAIL;

using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using SimpleFn = HRESULT(WINAPI*)();
using ClassFn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
using JoystickFn = const void*(WINAPI*)();
CreateFn create_input = nullptr;
SimpleFn can_unload = nullptr;
ClassFn get_class = nullptr;
SimpleFn register_server = nullptr;
SimpleFn unregister_server = nullptr;
JoystickFn joystick_format = nullptr;

// Only constant-initialized POD globals: DLL unload runs no C++ destructors.
bool ModulePath(HMODULE module, wchar_t* path) noexcept {
    const DWORD length = GetModuleFileNameW(module, path, kPathCapacity);
    return length != 0 && length < kPathCapacity;
}

bool ParentDirectory(wchar_t* path) noexcept {
    wchar_t* last_separator = nullptr;
    for (wchar_t* cursor = path; *cursor; ++cursor) {
        if (*cursor == L'\\') last_separator = cursor;
    }
    if (!last_separator) return false;
    *last_separator = L'\0';
    return true;
}

const wchar_t* Filename(const wchar_t* path) noexcept {
    const wchar_t* result = path;
    for (; *path; ++path) if (*path == L'\\') result = path + 1;
    return result;
}

BOOL CALLBACK LoadSystemInput(PINIT_ONCE, PVOID, PVOID*) noexcept {
    wchar_t path[kPathCapacity]{};
    const UINT length = GetSystemDirectoryW(path, kPathCapacity);
    if (length == 0 || length >= kPathCapacity ||
        FAILED(StringCchCatW(path, kPathCapacity, L"\\dinput8.dll"))) {
        load_error = HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
        return TRUE;
    }

    HMODULE loaded = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loaded) {
        load_error = HRESULT_FROM_WIN32(GetLastError());
        return TRUE;
    }

    // Refuse redirection back to this proxy or any non-System32 module.
    wchar_t actual[kPathCapacity]{};
    if (!ModulePath(loaded, actual) || lstrcmpiW(path, actual) != 0) {
        FreeLibrary(loaded);
        load_error = HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
        return TRUE;
    }

    system_dinput = loaded;
    create_input = reinterpret_cast<CreateFn>(GetProcAddress(loaded, "DirectInput8Create"));
    can_unload = reinterpret_cast<SimpleFn>(GetProcAddress(loaded, "DllCanUnloadNow"));
    get_class = reinterpret_cast<ClassFn>(GetProcAddress(loaded, "DllGetClassObject"));
    register_server = reinterpret_cast<SimpleFn>(GetProcAddress(loaded, "DllRegisterServer"));
    unregister_server = reinterpret_cast<SimpleFn>(GetProcAddress(loaded, "DllUnregisterServer"));
    joystick_format = reinterpret_cast<JoystickFn>(GetProcAddress(loaded, "GetdfDIJoystick"));
    load_error = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    // Keep the real DLL loaded for process lifetime. Unloading under loader lock
    // could invalidate DirectInput objects returned to the game.
    return TRUE;
}

void EnsureSystemInput() noexcept {
    InitOnceExecuteOnce(&load_once, LoadSystemInput, nullptr, nullptr);
}

void WriteText(HANDLE log, const char* text) noexcept {
    const DWORD size = static_cast<DWORD>(lstrlenA(text));
    DWORD written = 0;
    WriteFile(log, text, size, &written, nullptr);
}

void WriteVersion(HANDLE log, const wchar_t* exe_path) noexcept {
    DWORD unused = 0;
    const DWORD size = GetFileVersionInfoSizeW(exe_path, &unused);
    if (size == 0 || size > 1024 * 1024) {
        WriteText(log, "game_file_version=unavailable\r\n");
        return;
    }
    void* data = HeapAlloc(GetProcessHeap(), 0, size);
    if (!data) return;
    VS_FIXEDFILEINFO* version = nullptr;
    UINT version_size = 0;
    if (GetFileVersionInfoW(exe_path, 0, size, data) &&
        VerQueryValueW(data, L"\\", reinterpret_cast<void**>(&version), &version_size) &&
        version && version_size >= sizeof(VS_FIXEDFILEINFO) && version->dwSignature == 0xFEEF04BD) {
        char line[160]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                "game_file_version=%u.%u.%u.%u (metadata only; no hook compatibility claim)\r\n",
                HIWORD(version->dwFileVersionMS), LOWORD(version->dwFileVersionMS),
                HIWORD(version->dwFileVersionLS), LOWORD(version->dwFileVersionLS)))) {
            WriteText(log, line);
        }
    } else {
        WriteText(log, "game_file_version=unavailable\r\n");
    }
    HeapFree(GetProcessHeap(), 0, data);
}

BOOL CALLBACK RecordProbe(PINIT_ONCE, PVOID context, PVOID*) noexcept {
    wchar_t exe_path[kPathCapacity]{};
    if (!ModulePath(nullptr, exe_path) || lstrcmpiW(Filename(exe_path), L"mgsvtpp.exe") != 0) return TRUE;

    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&RecordProbe), &self)) return TRUE;

    wchar_t output_path[kPathCapacity]{};
    if (!ModulePath(self, output_path) || !ParentDirectory(output_path)) return TRUE;
    // The proxy is intended to live beside mgsvtpp.exe. Derive the log location
    // from that loaded module, never from the process working directory.
    wchar_t exe_directory[kPathCapacity]{};
    if (FAILED(StringCchCopyW(exe_directory, kPathCapacity, exe_path)) || !ParentDirectory(exe_directory) ||
        lstrcmpiW(output_path, exe_directory) != 0) return TRUE;
    if (FAILED(StringCchCatW(output_path, kPathCapacity, L"\\mgs-coop"))) return TRUE;
    if (!CreateDirectoryW(output_path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return TRUE;
    if (FAILED(StringCchCatW(output_path, kPathCapacity, L"\\probe.log"))) return TRUE;

    HANDLE log = CreateFileW(output_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return TRUE;

    WriteText(log, "\r\nMGS Co-op loader probe " MGS_COOP_PROBE_VERSION "\r\n");
    WriteText(log, "architecture=x64\r\nprocess=mgsvtpp.exe\r\n");
    SYSTEMTIME now{};
    GetSystemTime(&now);
    char line[192]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "time_utc=%04u-%02u-%02uT%02u:%02u:%02uZ\r\nDirectInput8Create_hresult=0x%08lX\r\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            static_cast<unsigned long>(*static_cast<const HRESULT*>(context))))) WriteText(log, line);
    WriteVersion(log, exe_path);
    WriteText(log, system_dinput ? "system_dinput=loaded_from_System32\r\n" : "system_dinput=unavailable\r\n");

    // Export presence is capability inventory only. No Steam exports are called,
    // and no Steam module is loaded if the game has not loaded it already.
    const HMODULE steam = GetModuleHandleW(L"steam_api64.dll");
    WriteText(log, steam ? "steam_api64=already_loaded\r\n" : "steam_api64=not_loaded_at_probe_time\r\n");
    const char* const exports[] = {
        "SteamAPI_Init", "SteamAPI_RunCallbacks", "SteamAPI_RegisterCallback",
        "SteamAPI_GetHSteamUser", "SteamAPI_GetHSteamPipe", "SteamClient", "SteamUser",
        "SteamFriends", "SteamMatchmaking", "SteamNetworking",
        "SteamAPI_SteamNetworkingSockets_v012", "SteamAPI_SteamNetworkingMessages_SteamAPI_v002"
    };
    for (const char* name : exports) {
        const char* status = !steam ? "module_not_loaded" : GetProcAddress(steam, name) ? "present" : "absent";
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "export.%s=%s\r\n", name, status))) WriteText(log, line);
    }
    WriteText(log, "scope=loader_plus_gated_steam_and_opt_in_script_adapter; bootstrap_replacement=marker_controlled; networking=disabled; save_access=none\r\n");
    CloseHandle(log);
    return TRUE;
}
} // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* output, LPUNKNOWN outer) {
    const DWORD incoming_error = GetLastError();
    EnsureSystemInput();
    SetLastError(incoming_error);
    HRESULT result = load_error;
    if (create_input) result = create_input(instance, version, iid, output, outer);
    else if (output) *output = nullptr;
    const DWORD forwarded_error = GetLastError();
    InitOnceExecuteOnce(&probe_once, RecordProbe, &result, nullptr);
    if (SUCCEEDED(result)) InstallSteamCallbackObserver();
    SetLastError(forwarded_error);
    return result;
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    const DWORD incoming_error = GetLastError();
    EnsureSystemInput();
    SetLastError(incoming_error);
    return can_unload ? can_unload() : S_FALSE;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID class_id, REFIID iid, LPVOID* output) {
    const DWORD incoming_error = GetLastError();
    EnsureSystemInput();
    SetLastError(incoming_error);
    if (get_class) return get_class(class_id, iid, output);
    if (output) *output = nullptr;
    return load_error;
}

extern "C" HRESULT WINAPI DllRegisterServer() {
    const DWORD incoming_error = GetLastError();
    EnsureSystemInput();
    SetLastError(incoming_error);
    return register_server ? register_server() : load_error;
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
    const DWORD incoming_error = GetLastError();
    EnsureSystemInput();
    SetLastError(incoming_error);
    return unregister_server ? unregister_server() : load_error;
}

extern "C" const void* WINAPI GetdfDIJoystick() {
    const DWORD incoming_error = GetLastError();
    EnsureSystemInput();
    SetLastError(incoming_error);
    return joystick_format ? joystick_format() : nullptr;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
