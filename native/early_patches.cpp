// Original co-op lab DllMain-early patches: SteamNetworking gate, identity/save
// getter wrappers, mutex rename and XInput routing. IAT pointer writes only; no
// game calls.
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
char lower(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
// Case-insensitive prefix match; returns the remainder or null.
const char* skip_prefix(const char* text, const char* prefix) noexcept {
    std::size_t i = 0;
    for (; prefix[i]; ++i)
        if (lower(text[i]) != prefix[i]) return nullptr;
    return text + i;
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

const char* mutex_rename_blocker(const IsolationState& state) noexcept {
    if (!state.second_instance) return "first_instance";
    if (!state.saves_configured) return "save_dir_required";
    if (!state.saves_ok) return "save_isolation_failed";
    if (state.want_identity && !state.identity_ok) return "identity_isolation_failed";
    if (state.want_gate && !state.gate_ok) return "networking_gate_failed";
    return nullptr;
}

namespace {
// Written once in DllMain (InstallEarlyLabPatches) before any other thread
// reads it; read by the observer at install.
volatile long g_saves_redirected = 0;
}  // namespace

bool SavesRedirected() noexcept { return g_saves_redirected != 0; }
void set_saves_redirected_for_test(bool value) noexcept { g_saves_redirected = value ? 1 : 0; }

bool format_play_line(std::uint32_t refused, bool loopback, bool mutex_renamed, bool saves_redirected, char* out,
                      std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    // "play=1; loopback=0; mutex=unchanged; refused=0x" + up to 8 hex digits + "; saves=redirected\r\n".
    const char* parts[] = {"play=1; loopback=", loopback ? "1" : "0", "; mutex=", mutex_renamed ? "renamed" : "unchanged",
                           "; refused=0x"};
    std::size_t at = 0;
    const auto put = [&](const char* text) noexcept {
        for (std::size_t i = 0; text[i]; ++i) {
            if (at + 1 >= capacity) return false;
            out[at++] = text[i];
        }
        return true;
    };
    for (const char* part : parts)
        if (!put(part)) { out[0] = 0; return false; }
    char hex[9]{};
    std::size_t digits = 0;
    std::uint32_t value = refused;
    do {
        const unsigned nibble = value & 0xFu;
        hex[digits++] = static_cast<char>(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        value >>= 4;
    } while (value && digits < 8);
    char ordered[9]{};
    for (std::size_t i = 0; i < digits; ++i) ordered[i] = hex[digits - 1 - i];
    if (!put(ordered) || !put("; saves=") || !put(saves_redirected ? "redirected" : "unchanged") || !put("\r\n")) {
        out[0] = 0;
        return false;
    }
    out[at] = 0;
    return true;
}

bool is_local_master_url(const char* url) noexcept {
    if (!url) return false;
    const char* rest = skip_prefix(url, "http://");
    if (!rest) rest = skip_prefix(url, "https://");
    if (!rest) return false;
    const char* after = skip_prefix(rest, "127.0.0.1");
    if (!after) after = skip_prefix(rest, "localhost");
    if (!after) return false;
    if (*after == ':') {
        ++after;
        std::size_t digits = 0;
        unsigned port = 0;
        while (after[digits] >= '0' && after[digits] <= '9') {
            port = port * 10 + static_cast<unsigned>(after[digits] - '0');
            if (++digits > 5) return false;
        }
        if (!digits || port == 0 || port > 65535) return false;
        after += digits;
    }
    return *after == 0 || *after == '/';
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
#define MGS_COOP_PROBE_VERSION "0.11.1"
#endif

namespace mgscoop::lab {
namespace {
constexpr DWORD kPathCapacity = 1024;
constexpr std::uint32_t kSlotCreateMutexW = 0x208D170;
constexpr std::uint32_t kSlotSteamNetworking = 0x208E138;
constexpr std::uint32_t kSlotSteamUser = 0x208E128;
constexpr std::uint32_t kSlotSteamFriends = 0x208E110;
constexpr std::uint32_t kSlotSteamRemoteStorage = 0x208E118;
constexpr std::uint32_t kSlotXInputGetState = 0x208E070;
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

// Every IAT slot written here, for the post-entry verification.
struct InstalledSlot {
    const char* name;
    std::uint32_t rva;
    const void* wrapper;
};
constexpr unsigned kMaxInstalled = 6;
InstalledSlot g_installed[kMaxInstalled]{};
unsigned g_installed_count = 0;
bool g_gate_installed = false;

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

void record_installed(const char* name, std::uint32_t rva, const void* wrapper) noexcept {
    if (g_installed_count < kMaxInstalled) g_installed[g_installed_count++] = {name, rva, wrapper};
}

// Compare a contract byte gate against a loaded image at its RVA.
bool gate_matches(const std::uint8_t* base, const steam_shim_contract::ByteGate& gate) noexcept {
    const std::uint8_t* at = base + gate.rva;
    for (std::uint32_t i = 0; i < gate.size; ++i)
        if (at[i] != gate.bytes[i]) return false;
    return true;
}

bool pe_identity(const std::uint8_t* base, std::uint32_t timestamp, std::uint32_t size_of_image) noexcept {
    const auto pe = *reinterpret_cast<const std::uint32_t*>(base + 0x3C);
    if (*reinterpret_cast<const std::uint32_t*>(base + pe) != 0x00004550) return false;
    return *reinterpret_cast<const std::uint32_t*>(base + pe + 8) == timestamp &&
           *reinterpret_cast<const std::uint32_t*>(base + pe + 24 + 56) == size_of_image;
}

// Validate the exe identity cheaply (DllMain-safe; no full hash under the lock).
bool exe_fingerprint_ok(const std::uint8_t* base) noexcept {
    __try {
        if (!pe_identity(base, steam_shim_contract::kExeTimeDateStamp, steam_shim_contract::kExeSizeOfImage))
            return false;
        if (!gate_matches(base, steam_shim_contract::exe_mutex_block)) return false;
        if (!gate_matches(base, steam_shim_contract::exe_title)) return false;
        if (!gate_matches(base, steam_shim_contract::exe_identity_block)) return false;
        if (!gate_matches(base, steam_shim_contract::exe_xinput_thunk)) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// steam_api64 identity: TimeDateStamp/SizeOfImage plus the exact getter export
// bytes (the interface versions the proxies assume come from this build).
bool steam_bytes_ok(const std::uint8_t* base) noexcept {
    __try {
        if (!pe_identity(base, steam_shim_contract::kSteamTimeDateStamp, steam_shim_contract::kSteamSizeOfImage))
            return false;
        if (!gate_matches(base, steam_shim_contract::steam_user_getter)) return false;
        if (!gate_matches(base, steam_shim_contract::steam_friends_getter)) return false;
        if (!gate_matches(base, steam_shim_contract::steam_storage_getter)) return false;
        if (!gate_matches(base, steam_shim_contract::steam_networking_getter)) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}
bool steam_fingerprint_ok(HMODULE steam) noexcept {
    if (!steam) return false;
    const auto* base = reinterpret_cast<const std::uint8_t*>(steam);
    if (!steam_bytes_ok(base)) return false;
    const auto at = [&](std::uint32_t rva) { return reinterpret_cast<FARPROC>(const_cast<std::uint8_t*>(base) + rva); };
    return GetProcAddress(steam, "SteamUser") == at(steam_shim_contract::steam_user_getter.rva) &&
           GetProcAddress(steam, "SteamFriends") == at(steam_shim_contract::steam_friends_getter.rva) &&
           GetProcAddress(steam, "SteamRemoteStorage") == at(steam_shim_contract::steam_storage_getter.rva) &&
           GetProcAddress(steam, "SteamNetworking") == at(steam_shim_contract::steam_networking_getter.rva);
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

// ----- Identity / save getter wrappers -----
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
                      std::uint32_t expected_rva, void* expected_pointer, void* replacement) noexcept {
    if (!expected_pointer) return false;
    const MappedImageSpan mapped{base, span};
    const auto slot = find_import_slot(static_cast<const std::uint8_t*>(base), span, dll, symbol,
                                       readable_image_range, &mapped);
    if (slot.status != ImportStatus::found || slot.offset != expected_rva) return false;
    const auto result = exchange_import(base, span, slot.offset, expected_pointer, replacement);
    if (result.status != PatchStatus::installed || !result.replacement_installed) return false;
    record_installed(symbol, expected_rva, replacement);
    return true;
}

void* steam_export(HMODULE steam, const char* name) noexcept {
    return steam ? reinterpret_cast<void*>(GetProcAddress(steam, name)) : nullptr;
}

void log_reason(const char* prefix, const char* reason) noexcept {
    char line[160]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "%s; reason=%s\r\n", prefix, reason))) log_line(line);
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

    // ---- What the configuration asks for ----
    IsolationState iso{};
    iso.second_instance = lab.mutex_suffix[0] != 0;
    iso.want_identity = lab.self_steamid != 0;
    iso.want_gate = lab.loopback || lab.self_steamid != 0;
    iso.saves_configured = lab.save_dir[0] != 0;

    identity::Settings settings{};
    settings.self_id = lab.self_steamid;
    settings.peer_id = lab.peer_steamid;
    settings.loopback = lab.loopback;
    settings.log = &log_line;
    for (int i = 0; i < 63 && lab.self_name[i]; ++i) settings.self_name[i] = lab.self_name[i];

    const char* identity_reason = nullptr;
    const char* saves_reason = nullptr;
    if (iso.want_identity && !lab.loopback) {
        // A fake id without the loopback emulator would have no networking at
        // all (the gate hands out a dead object); refuse it instead.
        identity_reason = "loopback_disabled";
        settings.self_id = 0;
    }
    if (iso.saves_configured) {
        if (save::save_dir_fits(lab.save_dir)) {
            for (int i = 0; i < 259 && lab.save_dir[i]; ++i) settings.save_dir[i] = lab.save_dir[i];
        } else {
            saves_reason = "save_dir_too_long";
        }
    }
    const bool want_user = identity::needs_user_proxy(settings);
    const bool want_friends = identity::needs_friends_proxy(settings);
    const bool want_storage = settings.save_dir[0] != 0;
    const bool want_steam = want_user || want_friends || want_storage || iso.want_gate;

    HMODULE steam = GetModuleHandleW(L"steam_api64.dll");
    const bool steam_ok = want_steam && steam_fingerprint_ok(steam);
    if (want_steam && !steam_ok) {
        const char* why = steam ? "steam_api64_mismatch" : "steam_api64_not_mapped";
        if (!identity_reason) identity_reason = why;
        if (!saves_reason) saves_reason = why;
    }
    bool configured = false;
    if (steam_ok && (want_user || want_friends)) {
        configured = identity::configure(settings);
        if (!configured) {
            if (!identity_reason) identity_reason = "identity_settings_rejected";
            if (!saves_reason) saves_reason = "identity_settings_rejected";
        }
    }
    if (steam_ok && want_storage) save::configure(settings.save_dir);

    // ---- 1. SteamNetworking gate: never real networking in loopback/fake-id mode ----
    if (iso.want_gate) {
        if (steam_ok && patch_named_slot(base, span, "steam_api64.dll", "SteamNetworking", kSlotSteamNetworking,
                                         steam_export(steam, "SteamNetworking"),
                                         reinterpret_cast<void*>(&loopback_networking_gate))) {
            iso.gate_ok = true;
            g_gate_installed = true;
            log_line(lab.loopback ? "networking=gated; mode=loopback\r\n" : "networking=gated; mode=dead\r\n");
        } else {
            log_reason("networking=gate_failed", steam_ok ? "slot_or_export_mismatch" : "steam_api64_unverified");
        }
    }

    // ---- 2. Identity / save getter wrappers ----
    bool user_ok = false, friends_ok = false, storage_ok = false;
    if (configured && want_user) {
        void* expected = steam_export(steam, "SteamUser");
        user_ok = patch_named_slot(base, span, "steam_api64.dll", "SteamUser", kSlotSteamUser, expected,
                                   reinterpret_cast<void*>(&User_Getter));
        if (user_ok) g_real_user = reinterpret_cast<SteamGetterFn>(expected);
    }
    if (configured && want_friends) {
        void* expected = steam_export(steam, "SteamFriends");
        friends_ok = patch_named_slot(base, span, "steam_api64.dll", "SteamFriends", kSlotSteamFriends, expected,
                                      reinterpret_cast<void*>(&Friends_Getter));
        if (friends_ok) g_real_friends = reinterpret_cast<SteamGetterFn>(expected);
    }
    if (steam_ok && want_storage) {
        void* expected = steam_export(steam, "SteamRemoteStorage");
        storage_ok = patch_named_slot(base, span, "steam_api64.dll", "SteamRemoteStorage", kSlotSteamRemoteStorage,
                                      expected, reinterpret_cast<void*>(&Storage_Getter));
        if (storage_ok) g_real_storage = reinterpret_cast<SteamGetterFn>(expected);
    }

    // ---- Truthful status lines ----
    iso.identity_ok = settings.self_id != 0 && configured && user_ok && friends_ok;
    iso.saves_ok = want_storage && configured && user_ok && storage_ok;
    g_saves_redirected = iso.saves_ok ? 1 : 0;
    {
        char line[400]{};
        if (iso.identity_ok) {
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "identity=fake; steamid=%llu\r\n",
                                           static_cast<unsigned long long>(settings.self_id))))
                log_line(line);
        } else if (iso.want_identity) {
            log_reason("identity=real", identity_reason ? identity_reason
                                        : !user_ok ? "steamuser_slot_mismatch" : "steamfriends_slot_mismatch");
        } else {
            log_line("identity=real\r\n");
        }
        if (want_friends && configured && !friends_ok && !iso.want_identity)
            log_line("friends=unwrapped; reason=steamfriends_slot_mismatch\r\n");
        if (iso.saves_ok) {
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "saves=redirected; dir=%s\r\n", settings.save_dir)))
                log_line(line);
        } else if (iso.saves_configured) {
            log_reason("saves=unchanged", saves_reason ? saves_reason
                                          : !user_ok ? "steamuser_slot_mismatch" : "steamremotestorage_slot_mismatch");
        } else {
            log_line("saves=unchanged\r\n");
        }
        // Two-PC play (lab.ini mode=play): the refusals and the save result
        // the launcher checks before trusting the session.
        if (lab.mode == LabMode::play &&
            format_play_line(lab.play_refused, lab.loopback, iso.second_instance, iso.saves_ok, line, ARRAYSIZE(line)))
            log_line(line);
    }

    // ---- 3. CreateMutexW rename (second instance), only when isolated ----
    if (iso.second_instance) {
        if (const char* blocker = mutex_rename_blocker(iso)) {
            log_reason("mutex=unchanged; detail=isolation_failed", blocker);
        } else {
            for (int i = 0; i < 31 && lab.mutex_suffix[i]; ++i) g_mutex_suffix[i] = lab.mutex_suffix[i];
            HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
            void* expected = kernel ? reinterpret_cast<void*>(GetProcAddress(kernel, "CreateMutexW")) : nullptr;
            if (patch_named_slot(base, span, "KERNEL32.dll", "CreateMutexW", kSlotCreateMutexW, expected,
                                 reinterpret_cast<void*>(&Mutex_Wrapper))) {
                g_real_create_mutex = reinterpret_cast<CreateMutexWFn>(expected);
                char line[64]{};
                if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "mutex=renamed; suffix=%s\r\n", g_mutex_suffix)))
                    log_line(line);
            } else {
                log_line("mutex=unchanged; reason=slot_or_export_mismatch\r\n");
            }
        }
    } else {
        log_line("mutex=unchanged\r\n");
    }

    // ---- 4. XInput routing ----
    g_xinput_setting = lab.xinput;
    if (lab.xinput == kXInputPassthrough) {
        log_line("xinput=passthrough\r\n");
    } else {
        HMODULE xinput = GetModuleHandleW(L"XINPUT1_3.dll");
        void* expected = xinput ? reinterpret_cast<void*>(GetProcAddress(xinput, "XInputGetState")) : nullptr;
        // XInputGetState is imported by ordinal, so validate by RVA and the
        // resolved export rather than by import name.
        auto* iat = reinterpret_cast<void* volatile*>(base + kSlotXInputGetState);
        if (expected && *iat == expected) {
            const auto result = exchange_import(base, span, kSlotXInputGetState, expected,
                                                reinterpret_cast<void*>(&XInput_Wrapper));
            if (result.status == PatchStatus::installed && result.replacement_installed) {
                g_real_xinput = reinterpret_cast<XInputGetStateFn>(expected);
                record_installed("XInputGetState", kSlotXInputGetState, reinterpret_cast<const void*>(&XInput_Wrapper));
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

bool networking_gate_installed() noexcept { return g_gate_installed; }

const char* ProbeVersion() noexcept { return MGS_COOP_PROBE_VERSION; }

unsigned VerifyEarlyLabPatches(void (*log)(const char* line) noexcept) noexcept {
    const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    unsigned lost = 0;
    char line[128]{};
    for (unsigned i = 0; base && i < g_installed_count; ++i) {
        const InstalledSlot& slot = g_installed[i];
        const void* current = *reinterpret_cast<const void* const volatile*>(base + slot.rva);
        if (current == slot.wrapper) continue;
        ++lost;
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "early=lost; slot=%s; rva=0x%lx\r\n", slot.name,
                                       static_cast<unsigned long>(slot.rva)))) {
            log_line(line);
            if (log) log(line);
        }
    }
    if (!lost && SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "early=verified; slots=%u\r\n", g_installed_count))) {
        log_line(line);
        if (log) log(line);
    }
    return lost;
}

}  // namespace mgscoop::lab
#endif  // _WIN32
