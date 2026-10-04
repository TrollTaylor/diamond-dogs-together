// Original co-op lab DllMain-early patches: mutex rename, identity/save getter
// wrappers and XInput routing. IAT pointer writes only; no game calls.
#include "mgscoop/early_patches.hpp"

#include "mgscoop/lab_config.hpp"

#include <cstddef>

namespace mgscoop::lab {
namespace {
bool ascii_suffix_ok(const char* suffix) noexcept {
    if (!suffix || !*suffix) return false;
    for (std::size_t i = 0; suffix[i]; ++i) {
        const char c = suffix[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}
std::size_t wlen(const wchar_t* s) noexcept {
    std::size_t n = 0;
    while (s[n]) ++n;
    return n;
}
bool wequal(const wchar_t* a, const wchar_t* b) noexcept {
    for (std::size_t i = 0;; ++i) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
}
}  // namespace

const wchar_t* mutex_effective_name(const wchar_t* requested, const char* suffix, wchar_t* buf,
                                    std::size_t buf_chars, bool& renamed) noexcept {
    renamed = false;
    if (!requested) return requested;
    if (!ascii_suffix_ok(suffix) || !buf || !buf_chars) return requested;
    if (!wequal(requested, kMutexTitle)) return requested;
    const std::size_t base = wlen(requested);
    std::size_t add = 0;
    while (suffix[add]) ++add;
    if (base + add + 1 > buf_chars) return requested;  // would not fit
    for (std::size_t i = 0; i < base; ++i) buf[i] = requested[i];
    for (std::size_t i = 0; i < add; ++i) buf[base + i] = static_cast<wchar_t>(suffix[i]);
    buf[base + add] = 0;
    renamed = true;
    return buf;
}

XInputRoute route_xinput(int setting, unsigned requested_index) noexcept {
    if (setting == kXInputPassthrough) return {XInputAction::Passthrough, requested_index};
    if (setting == kXInputNone) return {XInputAction::Disconnected, requested_index};
    const unsigned pad = static_cast<unsigned>(setting);  // 0..3 validated by lab_config
    if (requested_index == 0) return {XInputAction::Remap, pad};
    return {XInputAction::Disconnected, requested_index};
}

}  // namespace mgscoop::lab

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <xinput.h>
#include <strsafe.h>

#include "mgscoop/import_table.hpp"
#include "mgscoop/win_import_patch.hpp"
#include "mgscoop/steam_identity.hpp"
#include "mgscoop/save_redirect.hpp"
#include "mgscoop/steam_loopback.hpp"
#include "mgscoop/steam_abi.hpp"
#include "steam_shim_contract.hpp"

#ifndef MGS_COOP_PROBE_VERSION
#define MGS_COOP_PROBE_VERSION "0.10.33"
#endif

namespace mgscoop::lab {
namespace {
constexpr DWORD kPathCapacity = 1024;
HANDLE g_log = INVALID_HANDLE_VALUE;

using CreateMutexWFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using SteamGetterFn = void*(MGSCOOP_STEAM_CALL*)();

CreateMutexWFn g_real_create_mutex = nullptr;
XInputGetStateFn g_real_xinput = nullptr;
SteamGetterFn g_real_user = nullptr;
SteamGetterFn g_real_friends = nullptr;
SteamGetterFn g_real_storage = nullptr;
int g_xinput_setting = kXInputPassthrough;
char g_mutex_suffix[32]{};

void log_line(const char* text) noexcept {
    if (g_log == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(g_log, text, static_cast<DWORD>(lstrlenA(text)), &written, nullptr);
}

void open_log(const wchar_t* exe_dir) noexcept {
    wchar_t dir[kPathCapacity]{};
    if (FAILED(StringCchPrintfW(dir, kPathCapacity, L"%s\\mgs-coop", exe_dir))) return;
    CreateDirectoryW(dir, nullptr);
    wchar_t path[kPathCapacity]{};
    if (FAILED(StringCchPrintfW(path, kPathCapacity, L"%s\\early.log", dir))) return;
    g_log = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
}

// Compare a contract byte gate against the loaded image at its RVA.
bool gate_matches(const std::uint8_t* base, const steam_shim_contract::ByteGate& gate) noexcept {
    const std::uint8_t* at = base + gate.rva;
    for (std::uint32_t i = 0; i < gate.size; ++i)
        if (at[i] != gate.bytes[i]) return false;
    return true;
}

// Validate the exe identity cheaply (DllMain-safe; no full hash under the lock).
bool exe_fingerprint_ok(const std::uint8_t* base) noexcept {
    __try {
        const auto pe = *reinterpret_cast<const std::uint32_t*>(base + 0x3C);
        if (*reinterpret_cast<const std::uint32_t*>(base + pe) != 0x00004550) return false;
        const auto timestamp = *reinterpret_cast<const std::uint32_t*>(base + pe + 8);
        const auto size_of_image = *reinterpret_cast<const std::uint32_t*>(base + pe + 24 + 56);
        if (timestamp != steam_shim_contract::kExeTimeDateStamp) return false;
        if (size_of_image != steam_shim_contract::kExeSizeOfImage) return false;
        if (!gate_matches(base, steam_shim_contract::exe_mutex_block)) return false;
        if (!gate_matches(base, steam_shim_contract::exe_title)) return false;
        if (!gate_matches(base, steam_shim_contract::exe_identity_block)) return false;
        if (!gate_matches(base, steam_shim_contract::exe_xinput_thunk)) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// ----- CreateMutexW wrapper (append suffix only for the exact title) -----
HANDLE WINAPI Mutex_Wrapper(LPSECURITY_ATTRIBUTES sa, BOOL initial, LPCWSTR name) noexcept {
    wchar_t renamed[128]{};
    bool did_rename = false;
    LPCWSTR effective = name;
    if (name) effective = mutex_effective_name(name, g_mutex_suffix, renamed, ARRAYSIZE(renamed), did_rename);
    return g_real_create_mutex(sa, initial, effective);  // last error set by the real call
}

// ----- XInputGetState routing -----
DWORD WINAPI XInput_Wrapper(DWORD index, XINPUT_STATE* state) noexcept {
    const DWORD incoming = GetLastError();
    const XInputRoute route = route_xinput(g_xinput_setting, index);
    if (route.action == XInputAction::Passthrough) return g_real_xinput(index, state);
    if (route.action == XInputAction::Remap) return g_real_xinput(route.index, state);
    if (state) {
        XINPUT_STATE empty{};
        *state = empty;
    }
    SetLastError(incoming);
    return ERROR_DEVICE_NOT_CONNECTED;
}

// ----- Identity / save / loopback getter wrappers -----
void* MGSCOOP_STEAM_CALL User_Getter() noexcept {
    const DWORD error = GetLastError();
    void* real = g_real_user();
    void* wrapped = identity::wrap_user(real);
    SetLastError(error);
    return wrapped;
}
void* MGSCOOP_STEAM_CALL Friends_Getter() noexcept {
    const DWORD error = GetLastError();
    void* real = g_real_friends();
    void* wrapped = identity::wrap_friends(real);
    SetLastError(error);
    return wrapped;
}
void* MGSCOOP_STEAM_CALL Storage_Getter() noexcept {
    const DWORD error = GetLastError();
    void* real = g_real_storage();
    void* wrapped = save::wrap_remote_storage(real);
    SetLastError(error);
    return wrapped;
}

// Patch an IAT data slot located by import name; require the expected RVA and
// the expected current pointer (the resolved export). Returns true on success.
bool patch_named_slot(void* base, std::size_t span, const char* dll, const char* symbol,
                      std::size_t expected_rva, void* expected_pointer, void* replacement) noexcept {
    const MappedImageSpan mapped{base, span};
    const auto slot = find_import_slot(static_cast<const std::uint8_t*>(base), span, dll, symbol,
                                       readable_image_range, &mapped);
    if (slot.status != ImportStatus::found || slot.offset != expected_rva) return false;
    const auto result = exchange_import(base, span, slot.offset, expected_pointer, replacement);
    return result.status == PatchStatus::installed && result.replacement_installed;
}
}  // namespace

void InstallEarlyLabPatches() noexcept {
    const LabConfig& lab = GetLabConfig();
    if (!lab.present) return;  // default behaviour unchanged with no lab.ini

    wchar_t exe[kPathCapacity]{};
    const DWORD exe_size = GetModuleFileNameW(nullptr, exe, kPathCapacity);
    if (!exe_size || exe_size >= kPathCapacity) return;
    wchar_t* separator = nullptr;
    for (wchar_t* p = exe; *p; ++p)
        if (*p == L'\\') separator = p;
    if (!separator || lstrcmpiW(separator + 1, L"mgsvtpp.exe") != 0) return;
    *separator = 0;  // exe now holds the directory
    open_log(exe);

    char start[160]{};
    if (SUCCEEDED(StringCchPrintfA(start, ARRAYSIZE(start), "\r\nearly=start; version=%s; lab=1; role=%s\r\n",
                                   MGS_COOP_PROBE_VERSION,
                                   lab.role == LabRole::host ? "host" : "guest")))
        log_line(start);

    HMODULE self = GetModuleHandleW(nullptr);
    auto* base = reinterpret_cast<std::uint8_t*>(self);
    MODULEINFO info{};
    if (!self || !K32GetModuleInformation(GetCurrentProcess(), self, &info, sizeof(info))) {
        log_line("early=refused; reason=module_info_unavailable\r\n");
        return;
    }
    const std::size_t span = info.SizeOfImage;
    if (!exe_fingerprint_ok(base)) {
        log_line("early=refused; reason=fingerprint_or_code_bytes_mismatch\r\n");
        return;
    }

    // Record identity/save/loopback settings for the lazily-built proxies.
    identity::Settings identity_settings{};
    identity_settings.self_id = lab.self_steamid;
    identity_settings.peer_id = lab.peer_steamid;
    identity_settings.rewrite_auth_ticket = lab.self_steamid != 0;
    for (int i = 0; i < 63 && lab.self_name[i]; ++i) identity_settings.self_name[i] = lab.self_name[i];
    if (lab.save_dir[0])
        for (int i = 0; i < 259 && lab.save_dir[i]; ++i) identity_settings.save_dir[i] = lab.save_dir[i];
    const bool want_identity = lab.self_steamid != 0 || lab.save_dir[0];
    if (want_identity) identity::configure(identity_settings);
    if (lab.save_dir[0]) save::configure(lab.save_dir);

    // --- CreateMutexW rename (guest) ---
    if (lab.mutex_suffix[0]) {
        for (int i = 0; i < 31 && lab.mutex_suffix[i]; ++i) g_mutex_suffix[i] = lab.mutex_suffix[i];
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        void* expected = kernel ? reinterpret_cast<void*>(GetProcAddress(kernel, "CreateMutexW")) : nullptr;
        if (expected && patch_named_slot(base, span, "KERNEL32.dll", "CreateMutexW", 0x208D170, expected,
                                         reinterpret_cast<void*>(&Mutex_Wrapper))) {
            g_real_create_mutex = reinterpret_cast<CreateMutexWFn>(expected);
            char line[64]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "mutex=renamed; suffix=%s\r\n", g_mutex_suffix)))
                log_line(line);
        } else {
            log_line("mutex=unchanged; reason=slot_or_export_mismatch\r\n");
        }
    } else {
        log_line("mutex=unchanged\r\n");
    }

    // --- Steam identity/save getter wrappers (steam_api64 is already mapped) ---
    HMODULE steam = GetModuleHandleW(L"steam_api64.dll");
    if (want_identity && steam) {
        if (lab.self_steamid != 0 || lab.save_dir[0]) {
            void* expected = reinterpret_cast<void*>(GetProcAddress(steam, "SteamUser"));
            if (expected && patch_named_slot(base, span, "steam_api64.dll", "SteamUser", 0x208E128, expected,
                                             reinterpret_cast<void*>(&User_Getter)))
                g_real_user = reinterpret_cast<SteamGetterFn>(expected);
            else
                log_line("identity=warn; reason=steamuser_slot_mismatch\r\n");
        }
        if (lab.self_steamid != 0) {
            void* expected = reinterpret_cast<void*>(GetProcAddress(steam, "SteamFriends"));
            if (expected && patch_named_slot(base, span, "steam_api64.dll", "SteamFriends", 0x208E110, expected,
                                             reinterpret_cast<void*>(&Friends_Getter)))
                g_real_friends = reinterpret_cast<SteamGetterFn>(expected);
            else
                log_line("identity=warn; reason=steamfriends_slot_mismatch\r\n");
        }
        if (lab.save_dir[0]) {
            void* expected = reinterpret_cast<void*>(GetProcAddress(steam, "SteamRemoteStorage"));
            if (expected && patch_named_slot(base, span, "steam_api64.dll", "SteamRemoteStorage", 0x208E118, expected,
                                             reinterpret_cast<void*>(&Storage_Getter)))
                g_real_storage = reinterpret_cast<SteamGetterFn>(expected);
            else
                log_line("saves=warn; reason=steamremotestorage_slot_mismatch\r\n");
        }
    } else if (want_identity && !steam) {
        log_line("identity=warn; reason=steam_api64_not_mapped\r\n");
    }

    {
        char line[96]{};
        if (lab.self_steamid != 0) {
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "identity=fake; steamid=%llu\r\n",
                                           static_cast<unsigned long long>(lab.self_steamid))))
                log_line(line);
        } else {
            log_line("identity=real\r\n");
        }
        if (lab.save_dir[0]) {
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "saves=redirected; dir=%s\r\n", lab.save_dir)))
                log_line(line);
        } else {
            log_line("saves=unchanged\r\n");
        }
    }

    // --- XInput routing ---
    g_xinput_setting = lab.xinput;
    if (lab.xinput == kXInputPassthrough) {
        log_line("xinput=passthrough\r\n");
    } else {
        HMODULE xinput = GetModuleHandleW(L"XINPUT1_3.dll");
        void* expected = xinput ? reinterpret_cast<void*>(GetProcAddress(xinput, "XInputGetState")) : nullptr;
        // XInputGetState is imported by ordinal, so validate by RVA and the
        // resolved export rather than by import name.
        auto* iat = reinterpret_cast<void* volatile*>(base + 0x208E070);
        if (expected && *iat == expected) {
            const auto result = exchange_import(base, span, 0x208E070, expected,
                                                reinterpret_cast<void*>(&XInput_Wrapper));
            if (result.status == PatchStatus::installed && result.replacement_installed) {
                g_real_xinput = reinterpret_cast<XInputGetStateFn>(expected);
                if (lab.xinput == kXInputNone) {
                    log_line("xinput=none\r\n");
                } else {
                    char line[32]{};
                    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "xinput=pad %d\r\n", lab.xinput)))
                        log_line(line);
                }
            } else {
                log_line("xinput=passthrough; reason=slot_exchange_failed\r\n");
            }
        } else {
            log_line("xinput=passthrough; reason=slot_or_export_mismatch\r\n");
        }
    }
}

}  // namespace mgscoop::lab
#endif  // _WIN32
