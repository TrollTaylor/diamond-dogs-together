// Original co-op lab Steam interface forwarding proxies (identity + save).
#include "mgscoop/steam_identity.hpp"

#include "mgscoop/save_redirect.hpp"

#include <cstring>

namespace mgscoop::lab {

std::size_t encode_forward_thunk(std::uint8_t* out, const void* real, unsigned slot) noexcept {
    std::size_t n = 0;
    out[n++] = 0x48;
    out[n++] = 0xB9;  // mov rcx, imm64
    const std::uint64_t imm = reinterpret_cast<std::uint64_t>(real);
    std::memcpy(out + n, &imm, 8);
    n += 8;
    out[n++] = 0x48;
    out[n++] = 0x8B;
    out[n++] = 0x01;  // mov rax, [rcx]
    out[n++] = 0xFF;
    out[n++] = 0xA0;  // jmp qword ptr [rax + disp32]
    const std::uint32_t disp = slot * 8u;
    std::memcpy(out + n, &disp, 4);
    n += 4;
    while (n < kThunkStride) out[n++] = 0xCC;  // int3 padding
    return n;
}

}  // namespace mgscoop::lab

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace mgscoop::lab {

void* build_forwarding_proxy(void* real, unsigned slot_count, const ProxyOverride* overrides,
                             unsigned override_count, void* context) noexcept {
    if (!real || !slot_count) return nullptr;
    for (unsigned i = 0; i < override_count; ++i)
        if (overrides[i].slot >= slot_count) return nullptr;
    auto* code = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, static_cast<SIZE_T>(slot_count) * kThunkStride, MEM_COMMIT | MEM_RESERVE,
                     PAGE_READWRITE));
    auto* vtable = static_cast<const void**>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, static_cast<SIZE_T>(slot_count) * sizeof(void*)));
    auto* header = static_cast<ProxyHeader*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ProxyHeader)));
    if (!code || !vtable || !header) {
        if (vtable) HeapFree(GetProcessHeap(), 0, vtable);
        if (header) HeapFree(GetProcessHeap(), 0, header);
        if (code) VirtualFree(code, 0, MEM_RELEASE);
        return nullptr;
    }
    for (unsigned slot = 0; slot < slot_count; ++slot) {
        const void* override_fn = nullptr;
        for (unsigned i = 0; i < override_count; ++i)
            if (overrides[i].slot == slot) { override_fn = overrides[i].fn; break; }
        if (override_fn) {
            vtable[slot] = override_fn;
        } else {
            std::uint8_t* at = code + static_cast<std::size_t>(slot) * kThunkStride;
            encode_forward_thunk(at, real, slot);
            vtable[slot] = at;
        }
    }
    DWORD previous = 0;
    if (!VirtualProtect(code, static_cast<SIZE_T>(slot_count) * kThunkStride, PAGE_EXECUTE_READ, &previous)) {
        HeapFree(GetProcessHeap(), 0, vtable);
        HeapFree(GetProcessHeap(), 0, header);
        VirtualFree(code, 0, MEM_RELEASE);
        return nullptr;
    }
    FlushInstructionCache(GetCurrentProcess(), code, static_cast<SIZE_T>(slot_count) * kThunkStride);
    header->vtable = vtable;
    header->real = real;
    header->context = context;
    return header;
}

namespace identity {
namespace {
Settings g_settings{};
bool g_configured = false;
void* g_user_real = nullptr;
void* g_user_proxy = nullptr;
void* g_friends_real = nullptr;
void* g_friends_proxy = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;

void* real_of(void* self) noexcept { return static_cast<ProxyHeader*>(self)->real; }

template <typename Fn>
Fn real_slot(void* real, unsigned slot) noexcept {
    auto* const* vtable = *static_cast<void* const* const*>(real);
    return reinterpret_cast<Fn>(vtable[slot]);
}

// ----- ISteamUser overrides -----
// GetSteamID returns CSteamID by value through a hidden return pointer.
std::uint64_t* MGSCOOP_STEAM_CALL User_GetSteamID(void* /*self*/, std::uint64_t* out) noexcept {
    if (out) *out = g_settings.self_id;
    return out;
}
bool MGSCOOP_STEAM_CALL User_GetUserDataFolder(void* self, char* buf, int cub) noexcept {
    if (g_settings.save_dir[0] && save::format_user_data_folder(g_settings.save_dir, buf, cub)) return true;
    using Fn = bool(MGSCOOP_STEAM_CALL*)(void*, char*, int);
    return real_slot<Fn>(real_of(self), steam::user_slot::GetUserDataFolder)(real_of(self), buf, cub);
}
std::uint32_t MGSCOOP_STEAM_CALL User_GetAuthSessionTicket(void* self, void* ticket, int cb_max,
                                                           std::uint32_t* pcb) noexcept {
    using Fn = std::uint32_t(MGSCOOP_STEAM_CALL*)(void*, void*, int, std::uint32_t*);
    const std::uint32_t handle =
        real_slot<Fn>(real_of(self), steam::user_slot::GetAuthSessionTicket)(real_of(self), ticket, cb_max, pcb);
    // fuse reads the SteamID from ticket bytes 12..19 (little-endian). Rewrite
    // it to the fake id so the local master server tells the two players apart.
    if (g_settings.rewrite_auth_ticket && g_settings.self_id && ticket && pcb && *pcb >= 20)
        std::memcpy(static_cast<std::uint8_t*>(ticket) + 12, &g_settings.self_id, 8);
    return handle;
}

// ----- ISteamFriends overrides -----
const char* MGSCOOP_STEAM_CALL Friends_GetPersonaName(void* self) noexcept {
    if (g_settings.self_name[0]) return g_settings.self_name;
    using Fn = const char*(MGSCOOP_STEAM_CALL*)(void*);
    return real_slot<Fn>(real_of(self), steam::friends_slot::GetPersonaName)(real_of(self));
}
const char* MGSCOOP_STEAM_CALL Friends_GetFriendPersonaName(void* self, std::uint64_t id) noexcept {
    if (is_loopback_id(id) && g_settings.peer_name[0]) return g_settings.peer_name;
    using Fn = const char*(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t);
    return real_slot<Fn>(real_of(self), steam::friends_slot::GetFriendPersonaName)(real_of(self), id);
}
void MGSCOOP_STEAM_CALL Friends_ActivateGameOverlayToUser(void* self, const char* dialog,
                                                          std::uint64_t id) noexcept {
    if (is_loopback_id(id)) return;  // no real overlay for a loopback peer
    using Fn = void(MGSCOOP_STEAM_CALL*)(void*, const char*, std::uint64_t);
    real_slot<Fn>(real_of(self), steam::friends_slot::ActivateGameOverlayToUser)(real_of(self), dialog, id);
}
bool MGSCOOP_STEAM_CALL Friends_RequestUserInformation(void* self, std::uint64_t id, bool name_only) noexcept {
    if (is_loopback_id(id)) return false;  // info already resolved; nothing pending
    using Fn = bool(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t, bool);
    return real_slot<Fn>(real_of(self), steam::friends_slot::RequestUserInformation)(real_of(self), id, name_only);
}
}  // namespace

bool configure(const Settings& settings) noexcept {
    if (settings.self_id != 0 && !steam::is_individual_steamid(settings.self_id)) return false;
    AcquireSRWLockExclusive(&g_lock);
    g_settings = settings;
    g_configured = true;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

bool configured() noexcept { return g_configured; }

bool is_loopback_id(std::uint64_t id) noexcept {
    return id != 0 && (id == g_settings.self_id || (g_settings.peer_id != 0 && id == g_settings.peer_id));
}

void* wrap_user(void* real) noexcept {
    if (!real || !g_configured) return real;
    if (g_settings.self_id == 0 && !g_settings.save_dir[0]) return real;  // nothing to override
    AcquireSRWLockExclusive(&g_lock);
    if (real == g_user_real && g_user_proxy) {
        void* cached = g_user_proxy;
        ReleaseSRWLockExclusive(&g_lock);
        return cached;
    }
    ProxyOverride overrides[3];
    unsigned n = 0;
    if (g_settings.self_id != 0) {
        overrides[n++] = {steam::user_slot::GetSteamID, reinterpret_cast<const void*>(&User_GetSteamID)};
        if (g_settings.rewrite_auth_ticket)
            overrides[n++] = {steam::user_slot::GetAuthSessionTicket,
                              reinterpret_cast<const void*>(&User_GetAuthSessionTicket)};
    }
    if (g_settings.save_dir[0])
        overrides[n++] = {steam::user_slot::GetUserDataFolder,
                          reinterpret_cast<const void*>(&User_GetUserDataFolder)};
    void* proxy = build_forwarding_proxy(real, steam::user_slot::kCount, overrides, n, &g_settings);
    if (proxy) {
        g_user_real = real;
        g_user_proxy = proxy;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return proxy ? proxy : real;
}

void* wrap_friends(void* real) noexcept {
    if (!real || !g_configured || g_settings.self_id == 0) return real;
    AcquireSRWLockExclusive(&g_lock);
    if (real == g_friends_real && g_friends_proxy) {
        void* cached = g_friends_proxy;
        ReleaseSRWLockExclusive(&g_lock);
        return cached;
    }
    ProxyOverride overrides[4] = {
        {steam::friends_slot::GetPersonaName, reinterpret_cast<const void*>(&Friends_GetPersonaName)},
        {steam::friends_slot::GetFriendPersonaName, reinterpret_cast<const void*>(&Friends_GetFriendPersonaName)},
        {steam::friends_slot::ActivateGameOverlayToUser,
         reinterpret_cast<const void*>(&Friends_ActivateGameOverlayToUser)},
        {steam::friends_slot::RequestUserInformation,
         reinterpret_cast<const void*>(&Friends_RequestUserInformation)},
    };
    void* proxy = build_forwarding_proxy(real, steam::friends_slot::kCount, overrides, 4, &g_settings);
    if (proxy) {
        g_friends_real = real;
        g_friends_proxy = proxy;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return proxy ? proxy : real;
}

void reset_for_test() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_settings = Settings{};
    g_configured = false;
    g_user_real = g_user_proxy = nullptr;
    g_friends_real = g_friends_proxy = nullptr;
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace identity
}  // namespace mgscoop::lab
#endif  // _WIN32
