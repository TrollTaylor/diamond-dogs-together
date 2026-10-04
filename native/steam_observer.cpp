#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <strsafe.h>
#include <intrin.h>
#include <new>
#include "mgscoop/import_table.hpp"
#include "mgscoop/win_import_patch.hpp"
#include "mgscoop/steam_observer.hpp"
#include "mgscoop/steam_lifecycle.hpp"
#include "mgscoop/steam_getter_observation.hpp"
#include "mgscoop/game_script_observer.hpp"
#include "mgscoop/master_server_patch.hpp"
#include "mgscoop/lab_config.hpp"
#include "mgscoop/steam_loopback.hpp"
#include "mgscoop/steam_callbacks.hpp"
#include "mgscoop/steam_identity.hpp"
#include "mgscoop/early_patches.hpp"

namespace {
constexpr DWORD path_capacity = 32768;
constexpr unsigned char accepted_sha256[32] = {
    0x08,0x5c,0x2f,0x82,0xd1,0xc9,0x63,0xc4,0x0b,0x3d,0x2d,0x55,0x78,0x66,0x61,0xdf,
    0xee,0x2b,0x18,0xcb,0xbf,0x38,0x8a,0x71,0x0c,0x00,0xfa,0x76,0xc5,0xe9,0xbb,0x45};
constexpr unsigned char accepted_steam_sha256[32] = {
    0x08,0xbd,0x01,0x52,0x35,0x6e,0x56,0x19,0xee,0x56,0xa6,0x71,0x4a,0xc9,0x51,0xb6,
    0x80,0x0d,0xbb,0x21,0x82,0x16,0x93,0xa4,0xef,0xaf,0x85,0x27,0x84,0x42,0xc6,0x0a};
INIT_ONCE observer_once = INIT_ONCE_STATIC_INIT;
HANDLE observation_log = INVALID_HANDLE_VALUE;
void(__cdecl* original_callbacks)() = nullptr;
mgscoop::SteamInitCall original_init = nullptr;
mgscoop::SteamNetworkingGetter original_networking = nullptr;
constexpr std::uintptr_t networking_returns[] = {
    0x1a5f74f,0x1a5f75e,0x1a5f78f,0x1a5f79e,0x1a5f8e6,
    0x1a5f8f1,0x1a5fadf,0x1a5fb9c,0x1a5fc15};
// Three outcome bits per known return site plus one bucket for all other sites.
// Logs at most 30 outcome discoveries and one multiple-thread discovery.
volatile LONG networking_outcomes[ARRAYSIZE(networking_returns) + 1]{};
volatile LONG networking_first_thread = 0;
volatile LONG networking_multiple_threads = 0;
// Constructed outside loader lock after fingerprint checks. The pinned observer
// retains this one bounded allocation until process exit; no unload destructor
// runs while an observed Steam call might still be using its tracker.
mgscoop::SteamLifecycleTracker* init_tracker = nullptr;
volatile LONG64 calls = 0;
volatile LONG first_thread = 0;
volatile LONG multiple_threads = 0;
void* game_base = nullptr;
std::size_t game_span = 0;

// Co-op lab loopback integration. Off unless lab.ini enables loopback and the
// getter/callback slots verify; when off, the observer behaves exactly as before.
bool loopback_active = false;
// Lab loopback or fake identity: the SteamNetworking getter must never reach
// real Steam (fail closed to the emulator or the dead object).
bool lab_networking_gated = false;
using SteamRegisterCallbackFn = void(MGSCOOP_STEAM_CALL*)(void*, int);
using SteamUnregisterCallbackFn = void(MGSCOOP_STEAM_CALL*)(void*);
SteamRegisterCallbackFn original_register_callback = nullptr;
SteamUnregisterCallbackFn original_unregister_callback = nullptr;

bool fingerprint_matches(const wchar_t* path, const unsigned char (&expected)[32]) noexcept {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char result[32]{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    unsigned char buffer[65536];
    DWORD count = 0;
    while (ok) {
        if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr)) { ok = false; break; }
        if (!count) break;
        if (BCryptHashData(hash, buffer, count, 0) < 0) ok = false;
    }
    if (ok) ok = BCryptFinishHash(hash, result, sizeof(result), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    unsigned char difference = 0;
    for (unsigned i = 0; i < sizeof(result); ++i) difference |= result[i] ^ expected[i];
    return ok && difference == 0;
}
void log_line(const char* text) noexcept {
    if (observation_log == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(observation_log, text, static_cast<DWORD>(lstrlenA(text)), &written, nullptr);
}
std::uint32_t read_error() noexcept { return GetLastError(); }
void write_error(std::uint32_t value) noexcept { SetLastError(value); }
struct InitCaller {
    void* return_address;
};
void log_init_event(void* context, const mgscoop::SteamInitEvent& event) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(static_cast<InitCaller*>(context)->return_address);
    const auto base = reinterpret_cast<std::uintptr_t>(game_base);
    const bool in_game = address >= base && address - base < game_span;
    const bool completed = event.stage == mgscoop::SteamInitStage::Completed;
    const char* result = !completed ? "pending" : event.completion == mgscoop::SteamInitCompletion::ReturnedTrue ? "true" :
        event.completion == mgscoop::SteamInitCompletion::ReturnedFalse ? "false" : "exception";
    const auto evidence = init_tracker->snapshot();
    char line[640]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
        "init_stage=%s ticket=%llu thread=%llu result=%s tick_ms=%llu caller_in_game=%d caller_rva=0x%llx "
        "evidence_available=%d entries=%llu active=%llu maximum_overlap=%llu multiple_init_threads=%d lost=%llu invalid=%llu "
        "shutdown_coverage=none context_queries=disabled\r\n",
        completed ? "completion" : "start", static_cast<unsigned long long>(event.ticket.value),
        static_cast<unsigned long long>(event.thread), result, GetTickCount64(), in_game ? 1 : 0,
        in_game ? static_cast<unsigned long long>(address - base) : 0ull, evidence.snapshot_available ? 1 : 0,
        static_cast<unsigned long long>(evidence.init_entries), static_cast<unsigned long long>(evidence.active_observations),
        static_cast<unsigned long long>(evidence.maximum_overlap), evidence.multiple_init_threads ? 1 : 0,
        static_cast<unsigned long long>(evidence.lost_observations), static_cast<unsigned long long>(evidence.invalid_completions)))) {
        log_line(line);
    }
}
__declspec(noinline) bool __cdecl observed_init() {
    const DWORD incoming_error = GetLastError();
    InitCaller caller{_ReturnAddress()};
    const mgscoop::SteamInitObservationContext context{
        GetCurrentThreadId(), incoming_error, read_error, write_error, log_init_event, &caller};
    // The forwarding helper preserves bool/LastError and rethrows the original
    // C++ exception. It never initializes Steam on the observer's own initiative.
    return mgscoop::forward_observed_steam_init(original_init, *init_tracker, context);
}
void log_networking_result(void* context, mgscoop::SteamGetterResult result) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(static_cast<InitCaller*>(context)->return_address);
    const auto base = reinterpret_cast<std::uintptr_t>(game_base);
    const bool in_game = address >= base && address - base < game_span;
    const auto rva = in_game ? address - base : 0;
    std::size_t bucket = ARRAYSIZE(networking_returns);
    if (in_game) for (std::size_t i = 0; i < ARRAYSIZE(networking_returns); ++i) {
        if (networking_returns[i] == rva) { bucket = i; break; }
    }
    const LONG bit = result == mgscoop::SteamGetterResult::ReturnedNull ? 1 :
        result == mgscoop::SteamGetterResult::ReturnedNonNull ? 2 : 4;
    const bool new_outcome = (InterlockedOr(&networking_outcomes[bucket], bit) & bit) == 0;
    const LONG thread = static_cast<LONG>(GetCurrentThreadId());
    const LONG first = InterlockedCompareExchange(&networking_first_thread, thread, 0);
    const bool new_thread = first != 0 && first != thread &&
        InterlockedExchange(&networking_multiple_threads, 1) == 0;
    if (!new_outcome && !new_thread) return;
    char line[480]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
        "networking_getter_result=%s thread=%lu tick_ms=%llu caller_in_game=%d caller_rva=0x%llx known_caller=%d "
        "new_outcome=%d new_thread=%d pointer_retained=false context_queries=disabled\r\n",
        bit == 1 ? "null" : bit == 2 ? "non_null" : "exception",
        static_cast<unsigned long>(thread), GetTickCount64(), in_game ? 1 : 0,
        static_cast<unsigned long long>(rva), bucket < ARRAYSIZE(networking_returns) ? 1 : 0,
        new_outcome ? 1 : 0, new_thread ? 1 : 0))) log_line(line);
}
__declspec(noinline) ISteamNetworking* __cdecl observed_networking() {
    const DWORD incoming_error = GetLastError();
    // Co-op lab loopback / fake identity: hand the game our emulator (started
    // lazily on the first call, outside DllMain) or, when it is not running,
    // the dead object. Never real Steam networking and never null (Send at
    // 0x1A5FB60 and Read at 0x1A5FAC0 do not null-check the getter result).
    if (lab_networking_gated) {
        void* object = mgscoop::lab::loopback_networking_object();
        SetLastError(incoming_error);
        return reinterpret_cast<ISteamNetworking*>(object);
    }
    InitCaller caller{_ReturnAddress()};
    return mgscoop::forward_observed_networking_getter(original_networking,
        {incoming_error, read_error, write_error, log_networking_result, &caller});
}
__declspec(noinline) void MGSCOOP_STEAM_CALL observed_register_callback(void* cb, int id) noexcept {
    const DWORD incoming_error = GetLastError();
    SetLastError(incoming_error);
    original_register_callback(cb, id);
    const DWORD outgoing = GetLastError();
    mgscoop::lab::callbacks::on_register(cb, id);
    SetLastError(outgoing);
}
__declspec(noinline) void MGSCOOP_STEAM_CALL observed_unregister_callback(void* cb) noexcept {
    const DWORD incoming_error = GetLastError();
    // Untrack before forwarding so a concurrent drain cannot invoke an object
    // the game is about to tear down.
    mgscoop::lab::callbacks::on_unregister(cb);
    SetLastError(incoming_error);
    original_unregister_callback(cb);
}
HANDLE loopback_log_handle = INVALID_HANDLE_VALUE;
void loopback_log_line(const char* text) noexcept {
    if (loopback_log_handle == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(loopback_log_handle, text, static_cast<DWORD>(lstrlenA(text)), &written, nullptr);
}
// Resolve the real account SteamID through the shipped ISteamUser (SteamUser018
// GetSteamID[2], hidden-return ABI). SteamAPI_Init has completed by the observer
// install point, so this mirrors the game's own 0x143E call.
std::uint64_t resolve_real_steamid(HMODULE steam) noexcept {
    auto getter = reinterpret_cast<void*(MGSCOOP_STEAM_CALL*)()>(GetProcAddress(steam, "SteamUser"));
    if (!getter) return 0;
    void* user = getter();
    if (!user) return 0;
    auto* const* vtable = *static_cast<void* const* const*>(user);
    using GetIdFn = std::uint64_t*(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t*);
    std::uint64_t id = 0;
    reinterpret_cast<GetIdFn>(vtable[mgscoop::steam::user_slot::GetSteamID])(user, &id);
    return id;
}
// The persona this instance announces in its loopback HELLO when lab.ini sets
// no self_name: the real SteamFriends015 GetPersonaName[0] (a local query).
void resolve_real_persona(HMODULE steam, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return;
    out[0] = 0;
    auto getter = reinterpret_cast<void*(MGSCOOP_STEAM_CALL*)()>(GetProcAddress(steam, "SteamFriends"));
    if (!getter) return;
    void* friends = getter();
    if (!friends) return;
    auto* const* vtable = *static_cast<void* const* const*>(friends);
    using NameFn = const char*(MGSCOOP_STEAM_CALL*)(void*);
    const char* name = reinterpret_cast<NameFn>(vtable[mgscoop::steam::friends_slot::GetPersonaName])(friends);
    if (!name) return;
    std::size_t i = 0;
    for (; name[i] && i + 1 < capacity; ++i) out[i] = (name[i] >= 0x20 && name[i] <= 0x7e) ? name[i] : '?';
    out[i] = 0;
}
// Wrap the Register/Unregister IATs, configure the callback registry and the
// loopback emulator, and arm the networking getter. Lab-gated; a no-op when
// lab.ini is absent or loopback is off.
void InstallLoopbackIfEnabled(HMODULE steam, bool networking_routed) noexcept {
    const mgscoop::LabConfig& lab = mgscoop::GetLabConfig();
    if (!lab.present || !lab.loopback) return;
    // Every refusal below leaves the gate handing out the dead object: the game
    // gets no networking rather than real Steam networking.
    if (!networking_routed) { log_line("loopback=disabled; reason=networking_slot_unwrapped; networking=dead_object\r\n"); return; }
    if (!steam) { log_line("loopback=disabled; reason=steam_api64_not_mapped; networking=dead_object\r\n"); return; }

    std::uint64_t self_id = lab.self_steamid;
    if (self_id == 0) {  // host keeps its real identity
        self_id = resolve_real_steamid(steam);
        if (!mgscoop::steam::is_individual_steamid(self_id)) {
            log_line("loopback=disabled; reason=real_steamid_unavailable; networking=dead_object\r\n");
            return;
        }
    }

    const mgscoop::MappedImageSpan mapped{game_base, game_span};
    auto reg = mgscoop::find_import_slot(static_cast<const std::uint8_t*>(game_base), game_span,
                                         "steam_api64.dll", "SteamAPI_RegisterCallback", mgscoop::readable_image_range, &mapped);
    auto unreg = mgscoop::find_import_slot(static_cast<const std::uint8_t*>(game_base), game_span,
                                           "steam_api64.dll", "SteamAPI_UnregisterCallback", mgscoop::readable_image_range, &mapped);
    void* reg_fn = reinterpret_cast<void*>(GetProcAddress(steam, "SteamAPI_RegisterCallback"));
    void* unreg_fn = reinterpret_cast<void*>(GetProcAddress(steam, "SteamAPI_UnregisterCallback"));
    bool reg_ok = reg.status == mgscoop::ImportStatus::found && reg.offset == 0x208e0f8 && reg_fn;
    bool unreg_ok = unreg.status == mgscoop::ImportStatus::found && unreg.offset == 0x208e0f0 && unreg_fn;
    if (reg_ok) {
        const auto r = mgscoop::exchange_import(game_base, game_span, reg.offset, reg_fn, reinterpret_cast<void*>(&observed_register_callback));
        reg_ok = r.status == mgscoop::PatchStatus::installed && r.replacement_installed;
        if (reg_ok) original_register_callback = reinterpret_cast<SteamRegisterCallbackFn>(reg_fn);
    }
    if (unreg_ok) {
        const auto r = mgscoop::exchange_import(game_base, game_span, unreg.offset, unreg_fn, reinterpret_cast<void*>(&observed_unregister_callback));
        unreg_ok = r.status == mgscoop::PatchStatus::installed && r.replacement_installed;
        if (unreg_ok) original_unregister_callback = reinterpret_cast<SteamUnregisterCallbackFn>(unreg_fn);
    }
    if (!reg_ok || !unreg_ok) {
        // Roll back any half-install: without both wrappers the synthetic queue
        // could never be dispatched or could invoke a torn-down object.
        if (reg_ok && original_register_callback)
            mgscoop::exchange_import(game_base, game_span, reg.offset, reinterpret_cast<void*>(&observed_register_callback), reg_fn);
        if (unreg_ok && original_unregister_callback)
            mgscoop::exchange_import(game_base, game_span, unreg.offset, reinterpret_cast<void*>(&observed_unregister_callback), unreg_fn);
        original_register_callback = nullptr;
        original_unregister_callback = nullptr;
        log_line("loopback=disabled; reason=callback_iat_mismatch; networking=dead_object\r\n");
        return;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(game_base);
    mgscoop::lab::callbacks::configure(reinterpret_cast<const void*>(base + 0x24bf400),
                                       reinterpret_cast<const void*>(base + 0x24bf418));

    wchar_t exe_dir[path_capacity]{};
    if (GetModuleFileNameW(nullptr, exe_dir, path_capacity)) {
        wchar_t* sep = nullptr;
        for (wchar_t* p = exe_dir; *p; ++p) if (*p == L'\\') sep = p;
        if (sep) {
            *sep = 0;
            wchar_t log_path[path_capacity]{};
            if (SUCCEEDED(StringCchPrintfW(log_path, path_capacity, L"%s\\mgs-coop\\loopback.log", exe_dir)))
                loopback_log_handle = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
    }

    mgscoop::lab::EmulatorConfig cfg{};
    cfg.self_id = self_id;
    cfg.peer_id = lab.peer_steamid;
    cfg.peer_port = lab.peer_port;
    cfg.timeout_ms = 10000;
    for (int i = 0; i < 63 && lab.self_name[i]; ++i) cfg.self_name[i] = lab.self_name[i];
    if (!cfg.self_name[0]) resolve_real_persona(steam, cfg.self_name, sizeof(cfg.self_name));
    cfg.log = &loopback_log_line;
    cfg.on_peer = &mgscoop::lab::identity::note_peer;
    mgscoop::lab::LoopbackOptions options{};
    options.listen_port = lab.listen_port;
    options.latency_ms = lab.net_latency_ms;
    options.loss_permille = lab.net_loss_permille;
    options.trace_packets = lab.trace_packets;
    mgscoop::lab::loopback_configure(cfg, options);
    loopback_active = true;
    char line[160]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "loopback=armed; self=%llu; peer=%llu; listen_port=%u; peer_port=%u\r\n",
            static_cast<unsigned long long>(self_id), static_cast<unsigned long long>(lab.peer_steamid),
            lab.listen_port, lab.peer_port)))
        log_line(line);
}
__declspec(noinline) void __cdecl observed_callbacks() {
    const DWORD incoming_error = GetLastError();
    void* const return_address = _ReturnAddress();
    // No metadata or logging occurs before forwarding. Preserve the caller's
    // error state, then preserve the original API's error state on return.
    SetLastError(incoming_error);
    original_callbacks();
    // Co-op lab: drain synthetic 1202/1203 on this same thread, right after the
    // real callbacks ran, so the packet manager observes them as native events.
    if (loopback_active) mgscoop::lab::callbacks::drain();
    const DWORD outgoing_error = GetLastError();
    const auto count = InterlockedIncrement64(&calls);
    const auto thread = static_cast<LONG>(GetCurrentThreadId());
    const auto previous_thread = InterlockedCompareExchange(&first_thread, thread, 0);
    const bool new_thread = previous_thread != 0 && previous_thread != thread && InterlockedExchange(&multiple_threads, 1) == 0;
    if (count == 1 || count == 60 || count == 600 || count == 3600 || new_thread) {
        const auto address = reinterpret_cast<std::uintptr_t>(return_address);
        const auto base = reinterpret_cast<std::uintptr_t>(game_base);
        const bool in_game = address >= base && address - base < game_span;
        const auto evidence = init_tracker ? init_tracker->snapshot() : mgscoop::SteamLifecycleEvidence{};
        const char* init_observation = !evidence.snapshot_available ? "unavailable" :
            evidence.init_entries == 0 ? "unobserved" : "observed";
        char line[512]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "callback_count=%lld thread=%lu first_thread=%ld multiple_threads=%ld tick_ms=%llu caller_in_game=%d caller_rva=0x%llx "
            "init_observation=%s init_entries=%llu init_true_returns=%llu context_queries=disabled\r\n",
            count, static_cast<unsigned long>(thread), first_thread, multiple_threads,
            GetTickCount64(), in_game ? 1 : 0, in_game ? static_cast<unsigned long long>(address - base) : 0ull,
            init_observation, static_cast<unsigned long long>(evidence.init_entries),
            static_cast<unsigned long long>(evidence.true_returns)))) log_line(line);
    }
    SetLastError(outgoing_error);
}
BOOL CALLBACK install(PINIT_ONCE, PVOID, PVOID*) noexcept {
    wchar_t exe[path_capacity]{};
    const auto size = GetModuleFileNameW(nullptr, exe, path_capacity);
    if (!size || size >= path_capacity) return TRUE;
    wchar_t* separator = nullptr;
    for (auto* p = exe; *p; ++p) if (*p == L'\\') separator = p;
    if (!separator || lstrcmpiW(separator + 1, L"mgsvtpp.exe") != 0) return TRUE;
    if (!fingerprint_matches(exe, accepted_sha256)) return TRUE;
    HMODULE self = nullptr;
    wchar_t self_path[path_capacity]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&install), &self)) return TRUE;
    const auto self_size = GetModuleFileNameW(self, self_path, path_capacity);
    if (!self_size || self_size >= path_capacity) return TRUE;
    wchar_t expected_proxy[path_capacity]{};
    *separator = 0;
    if (FAILED(StringCchPrintfW(expected_proxy, path_capacity, L"%s\\dinput8.dll", exe)) || lstrcmpiW(expected_proxy, self_path) != 0) return TRUE;
    wchar_t log_path[path_capacity]{};
    if (FAILED(StringCchPrintfW(log_path, path_capacity, L"%s\\mgs-coop\\steam-observer.log", exe))) return TRUE;
    observation_log = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    log_line("\r\nMGS Co-op original Steam import observer 0.4.0; fingerprint=accepted\r\n");
    log_line("init_observation=unobserved_at_install; installation_after_DirectInput_may_miss_init=true; shutdown_coverage=none; context_queries=disabled\r\n");
    const HMODULE steam = GetModuleHandleW(L"steam_api64.dll");
    wchar_t steam_path[path_capacity]{}, expected_steam[path_capacity]{};
    const auto steam_path_size = steam ? GetModuleFileNameW(steam, steam_path, path_capacity) : 0;
    if (!steam_path_size || steam_path_size >= path_capacity || FAILED(StringCchPrintfW(expected_steam, path_capacity, L"%s\\steam_api64.dll", exe)) || lstrcmpiW(expected_steam, steam_path) != 0) {
        log_line("status=disabled; reason=unverified_steam_module_location\r\n"); return TRUE;
    }
    const auto callback_function = GetProcAddress(steam, "SteamAPI_RunCallbacks");
    const auto init_function = GetProcAddress(steam, "SteamAPI_Init");
    const auto networking_function = GetProcAddress(steam, "SteamNetworking");
    MODULEINFO module{};
    if ((!callback_function && !init_function) || !K32GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr), &module, sizeof(module))) {
        log_line("status=disabled; reason=module_or_export_unavailable\r\n"); return TRUE;
    }
    game_base = module.lpBaseOfDll;
    game_span = module.SizeOfImage;
    const mgscoop::MappedImageSpan mapped{game_base, game_span};
    const auto callback_slot = mgscoop::find_import_slot(static_cast<const std::uint8_t*>(game_base), game_span, "steam_api64.dll", "SteamAPI_RunCallbacks", mgscoop::readable_image_range, &mapped);
    const auto init_slot = mgscoop::find_import_slot(static_cast<const std::uint8_t*>(game_base), game_span, "steam_api64.dll", "SteamAPI_Init", mgscoop::readable_image_range, &mapped);
    const auto networking_slot = mgscoop::find_import_slot(static_cast<const std::uint8_t*>(game_base), game_span, "steam_api64.dll", "SteamNetworking", mgscoop::readable_image_range, &mapped);
    MODULEINFO steam_info{};
    bool networking_export_verified = false;
    if (networking_function && fingerprint_matches(steam_path, accepted_steam_sha256) &&
        K32GetModuleInformation(GetCurrentProcess(), steam, &steam_info, sizeof(steam_info))) {
        const mgscoop::MappedImageSpan steam_span{steam_info.lpBaseOfDll, steam_info.SizeOfImage};
        constexpr unsigned char getter_bytes[]{0x48,0x8b,0x05,0x71,0xb0,0x02,0x00,0xc3};
        const auto* entry = static_cast<const unsigned char*>(steam_info.lpBaseOfDll) + 0x5e30;
        if (reinterpret_cast<const void*>(networking_function) == entry &&
            mgscoop::readable_image_range(&steam_span, 0x5e30, sizeof(getter_bytes))) {
            networking_export_verified = true;
            for (std::size_t i = 0; i < sizeof(getter_bytes); ++i)
                networking_export_verified = networking_export_verified && entry[i] == getter_bytes[i];
        }
    }
    const bool can_observe_callbacks = callback_function && callback_slot.status == mgscoop::ImportStatus::found;
    const bool can_observe_init = init_function && init_slot.status == mgscoop::ImportStatus::found;
    if (!can_observe_callbacks && !can_observe_init) { log_line("status=disabled; reason=unique_import_not_found\r\n"); return TRUE; }
    // Pin before making a permanent IAT reference. This diagnostic deliberately
    // remains loaded until process exit; it has no unload/reload operation.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&observed_callbacks), &pinned)) {
        log_line("status=disabled; reason=wrapper_pin_failed\r\n"); return TRUE;
    }
    try { init_tracker = new(std::nothrow) mgscoop::SteamLifecycleTracker; }
    catch (...) { init_tracker = nullptr; }
    if (can_observe_init && init_tracker) {
        original_init = reinterpret_cast<mgscoop::SteamInitCall>(init_function);
        const auto result = mgscoop::exchange_import(game_base, game_span, init_slot.offset, reinterpret_cast<void*>(init_function), reinterpret_cast<void*>(&observed_init));
        char line[280]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "symbol=SteamAPI_Init status=%d replacement_installed=%d protection_restored=%d slot_rva=0x%llx\r\n",
            static_cast<int>(result.status), result.replacement_installed ? 1 : 0, result.protection_restored ? 1 : 0,
            static_cast<unsigned long long>(init_slot.offset)))) log_line(line);
    } else {
        log_line("symbol=SteamAPI_Init status=disabled reason=unique_import_export_or_tracker_unavailable\r\n");
    }
    if (can_observe_callbacks) {
        original_callbacks = reinterpret_cast<void(__cdecl*)()>(callback_function);
        const auto result = mgscoop::exchange_import(game_base, game_span, callback_slot.offset, reinterpret_cast<void*>(callback_function), reinterpret_cast<void*>(&observed_callbacks));
        char line[280]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "symbol=SteamAPI_RunCallbacks status=%d replacement_installed=%d protection_restored=%d slot_rva=0x%llx\r\n",
            static_cast<int>(result.status), result.replacement_installed ? 1 : 0, result.protection_restored ? 1 : 0,
            static_cast<unsigned long long>(callback_slot.offset)))) log_line(line);
    } else {
        log_line("symbol=SteamAPI_RunCallbacks status=disabled reason=unique_import_or_export_unavailable\r\n");
    }
    const mgscoop::LabConfig& lab = mgscoop::GetLabConfig();
    lab_networking_gated = lab.present && (lab.loopback || lab.self_steamid != 0);
    if (lab.present) mgscoop::lab::VerifyEarlyLabPatches(log_line);
    bool networking_wrapped = false;
    if (lab.present && mgscoop::lab::networking_gate_installed()) {
        // DllMain already routed 0x208E138 to the lab gate (emulator or dead
        // object); leave it in place.
        networking_wrapped = true;
        log_line("symbol=SteamNetworking status=lab_gate slot_rva=0x208e138\r\n");
    } else if (networking_export_verified && networking_slot.status == mgscoop::ImportStatus::found && networking_slot.offset == 0x208e138) {
        original_networking = reinterpret_cast<mgscoop::SteamNetworkingGetter>(networking_function);
        const auto result = mgscoop::exchange_import(game_base, game_span, networking_slot.offset,
            reinterpret_cast<void*>(networking_function), reinterpret_cast<void*>(&observed_networking));
        networking_wrapped = result.status == mgscoop::PatchStatus::installed && result.replacement_installed;
        char line[280]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "symbol=SteamNetworking status=%d replacement_installed=%d protection_restored=%d slot_rva=0x%llx\r\n",
            static_cast<int>(result.status), result.replacement_installed ? 1 : 0, result.protection_restored ? 1 : 0,
            static_cast<unsigned long long>(networking_slot.offset)))) log_line(line);
    } else log_line("symbol=SteamNetworking status=disabled reason=unverified_export_or_import\r\n");

    // Co-op master-server redirect (no-op when coop.ini sets no master_url). The
    // implementation is owned by another work package; its stub refuses safely.
    const char* master_url = mgscoop::GetCoopConfig().master_url;
    const bool master_ok = mgscoop::master_server::Install(game_base, game_span, master_url, log_line);
    if (lab.present && lab.self_steamid != 0) {
        // The guest's auth ticket is rewritten for fuse only while the game's
        // master server really is a local fuse instance.
        if (master_ok && master_url[0] && mgscoop::lab::is_local_master_url(master_url)) {
            mgscoop::lab::identity::arm_ticket_rewrite(true);
            log_line("identity=ticket_rewrite_armed; master=local\r\n");
        } else {
            log_line("identity=ticket_unrewritten; reason=master_redirect_inactive\r\n");
        }
    }

    InstallLoopbackIfEnabled(steam, networking_wrapped);

    log_line("scope=natural_init_callback_and_networking_getter_observation_only; sdk_context_queries=none; shutdown_coverage=none; game_function_calls=none; lua_owner_thread=unverified; multiplayer=not_connected\r\n");
    InstallGameScriptObserver(game_base, game_span, exe);
    return TRUE;
}
}
void InstallSteamCallbackObserver() noexcept {
    const DWORD error = GetLastError();
    InitOnceExecuteOnce(&observer_once, install, nullptr, nullptr);
    SetLastError(error);
}
