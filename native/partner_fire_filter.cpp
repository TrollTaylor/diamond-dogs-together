// Original MGS Co-op native puppet: fire visuals without real shots
// (docs/native-puppet.md "Weapons"; partner_fire_filter.hpp).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstring>
#include "mgscoop/partner_fire_filter.hpp"
#include "mgscoop/near_code.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_fire_filter {
namespace {
namespace c = mgscoop::partner_player_contract;

volatile LONG g_installed = 0;
bool g_ready = false;
std::uintptr_t g_base = 0;
detail::RequestFn g_original = nullptr;
detail::RequestFn g_test_original = nullptr;
volatile LONG g_owner = 0xFFFF;   // the armed owner id (0xFFFF = disarmed)
volatile LONG g_owner_id = 0xFFFF;  // the last armed id (owner_disarmed counts it while disarmed)
volatile LONG g_suppressed = 0, g_passed = 0, g_passed_player = 0, g_owner_disarmed = 0;
volatile LONG g_last_thread = 0, g_last_owner = 0xFFFF;

const c::Site* RequestSite() noexcept {
    for (const c::Site& s : c::code_sites)
        if (s.rva == c::fire_request_rva) return &s;
    return nullptr;
}

bool Read(std::uintptr_t address, void* out, std::size_t size) noexcept {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &read) &&
           read == size;
}
template <class T>
bool ReadValue(std::uintptr_t address, T& value) noexcept { return Read(address, &value, sizeof(value)); }

void* Hook(void* iface, const void* record) noexcept {
    InterlockedExchange(&g_last_thread, static_cast<LONG>(GetCurrentThreadId()));
    std::uint16_t owner = 0xFFFF;
    if (record) std::memcpy(&owner, static_cast<const unsigned char*>(record) + c::shot_owner, sizeof(owner));
    InterlockedExchange(&g_last_owner, static_cast<LONG>(owner));
    if (detail::Drops(static_cast<std::uint16_t>(g_owner), owner)) {
        InterlockedIncrement(&g_suppressed);
        return nullptr;  // as RequestBullet itself when its slot allocator yields nothing (0xD32CB4)
    }
    if (owner != 0xFFFFu && owner == static_cast<std::uint16_t>(g_owner_id)) InterlockedIncrement(&g_owner_disarmed);
    InterlockedIncrement(&g_passed);
    if ((owner & c::shot_owner_type_mask) == 0) InterlockedIncrement(&g_passed_player);
    const detail::RequestFn original = g_test_original ? g_test_original : g_original;
    return original ? original(iface, record) : nullptr;
}

std::uintptr_t HookAddress() noexcept { return reinterpret_cast<std::uintptr_t>(&Hook); }
} // namespace

namespace detail {
RequestFn HookEntry() noexcept { return &Hook; }
void ResetForTest() noexcept {
    InterlockedExchange(&g_installed, 0);
    g_ready = false;
    g_base = 0;
    g_original = nullptr;
    InterlockedExchange(&g_owner, 0xFFFF);
    InterlockedExchange(&g_owner_id, 0xFFFF);
    InterlockedExchange(&g_suppressed, 0);
    InterlockedExchange(&g_passed, 0);
    InterlockedExchange(&g_passed_player, 0);
    InterlockedExchange(&g_owner_disarmed, 0);
    InterlockedExchange(&g_last_owner, 0xFFFF);
}
void SetOriginalForTest(RequestFn original) noexcept { g_test_original = original; }
} // namespace detail

const char* Install(const void* base_ptr, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0) return g_ready ? nullptr : "disabled";
    const DWORD error = GetLastError();
    const auto base = reinterpret_cast<std::uintptr_t>(base_ptr);
    const std::uintptr_t target = base + c::fire_request_rva;
    const std::uintptr_t live = base + c::fire_slot_live_rva, dying = base + c::fire_slot_dying_rva;
    const char* result = nullptr;
    const char* where = "";
    const c::Site* site = RequestSite();
    unsigned char prologue[sizeof(c::Site::bytes)]{};
    std::uintptr_t live_value = 0, dying_value = 0;
    if (!base || !site || !Read(target, prologue, site->size) || std::memcmp(prologue, site->bytes, site->size) != 0) {
        result = "bytes";
        where = "0xd32c60";
    } else if (!ReadValue(live, live_value) || live_value != target) {
        result = "slot";
        where = "0x2337de8";
    } else if (!ReadValue(dying, dying_value) || dying_value != target) {
        result = "slot";
        where = "0x2337d18";
    } else {
        g_base = base;
        g_original = reinterpret_cast<detail::RequestFn>(target);
        const std::uintptr_t hook = HookAddress();
        if (const char* why = near_code::PatchQword(reinterpret_cast<void*>(live), target, hook)) {
            // protect_restore: the exchange happened (the slot holds the hook).
            if (std::strcmp(why, "protect_restore") != 0) { result = why; where = "0x2337de8"; }
        }
        if (!result) {
            if (const char* why = near_code::PatchQword(reinterpret_cast<void*>(dying), target, hook)) {
                if (std::strcmp(why, "protect_restore") != 0) {
                    result = why;
                    where = "0x2337d18";
                    // Leave nothing half-installed: the live slot goes back to RequestBullet.
                    near_code::PatchQword(reinterpret_cast<void*>(live), hook, target);
                }
            }
        }
    }
    if (!result) g_ready = true;
    if (log) {
        char line[224]{};
        if (g_ready)
            StringCchPrintfA(line, ARRAYSIZE(line),
                             "partner_fire=installed; slots=0x2337de8,0x2337d18; target=0xd32c60; hook=external; owner_field=0x2a\r\n");
        else
            StringCchPrintfA(line, ARRAYSIZE(line), "partner_fire=refused; reason=%s; site=%s; fire=masked\r\n", result, where);
        log(line);
    }
    SetLastError(error);
    return g_ready ? nullptr : result;
}

bool Installed() noexcept { return g_ready; }

bool PatchLive(const void* base_ptr) noexcept {
    if (!g_ready) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(base_ptr);
    std::uintptr_t live = 0, dying = 0;
    return ReadValue(base + c::fire_slot_live_rva, live) && ReadValue(base + c::fire_slot_dying_rva, dying) &&
           live == HookAddress() && dying == HookAddress();
}

bool Arm(std::uint16_t owner) noexcept {
    if (owner == 0xFFFFu || (owner & c::shot_owner_type_mask) != 0) return false;
    InterlockedExchange(&g_owner_id, static_cast<LONG>(owner));
    InterlockedExchange(&g_owner, static_cast<LONG>(owner));
    return true;
}

void Disarm() noexcept { InterlockedExchange(&g_owner, 0xFFFF); }

bool Armed() noexcept { return static_cast<std::uint16_t>(g_owner) != 0xFFFFu; }

void ReadStats(const void* base, Stats& out) noexcept {
    out = Stats{};
    out.installed = g_ready;
    out.live = PatchLive(base);
    out.owner = static_cast<std::uint16_t>(g_owner);
    out.armed = out.owner != 0xFFFFu;
    out.suppressed = static_cast<std::uint32_t>(g_suppressed);
    out.passed = static_cast<std::uint32_t>(g_passed);
    out.passed_player = static_cast<std::uint32_t>(g_passed_player);
    out.owner_disarmed = static_cast<std::uint32_t>(g_owner_disarmed);
    out.last_thread = static_cast<std::uint32_t>(g_last_thread);
    out.last_owner = static_cast<std::uint16_t>(g_last_owner);
}

void ReadCensus(const void* base_ptr, std::size_t image_span, Census& out) noexcept {
    out = Census{};
    const auto base = reinterpret_cast<std::uintptr_t>(base_ptr);
    std::uintptr_t root = 0, gm = 0, noise_owner = 0;
    if (!base || !ReadValue(base + c::service_root_rva, root) || !root || !ReadValue(root + c::root_service, out.svc) || !out.svc)
        return;
    if (ReadValue(out.svc + c::svc_gm, gm) && gm && ReadValue(gm + c::gm_bullets, out.bullets) && out.bullets &&
        ReadValue(out.bullets, out.bullets_vt))
        out.bullets_ok = out.bullets_vt == base + c::fire_slot_live_rva;
    if (ReadValue(out.svc + c::svc_noise_owner, noise_owner) && noise_owner &&
        ReadValue(noise_owner + c::noise_owner_iface, out.noise) && out.noise && ReadValue(out.noise, out.noise_vt) && out.noise_vt &&
        ReadValue(out.noise_vt, out.noise_fn0)) {
        out.noise_vt_in_image = out.noise_vt >= base && out.noise_vt - base < image_span;
        out.noise_fn0_in_image = out.noise_fn0 >= base && out.noise_fn0 - base < image_span;
        if (out.noise_vt_in_image) out.noise_vt -= base;
        if (out.noise_fn0_in_image) out.noise_fn0 -= base;
    }
    if (out.bullets_vt >= base && out.bullets_vt - base < image_span) out.bullets_vt -= base;
    out.ok = true;
}

} // namespace mgscoop::partner_fire_filter
