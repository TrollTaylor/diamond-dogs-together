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
// The noise filter (same armed owner).
volatile LONG g_noise_installed = 0;
bool g_noise_ready = false;
detail::NoiseFn g_noise_original = nullptr;
detail::NoiseFn g_noise_test_original = nullptr;
volatile LONG g_noise_dropped = 0, g_noise_passed = 0, g_noise_last_owner = 0xFFFF;
// Anim round 1: the census ring (any thread writes, the driver drains) and the
// projectile census slot.
volatile LONG g_census_on = 0;
volatile LONG g_c_seq = 0, g_c_bullets = 0, g_c_noises = 0, g_c_projectiles = 0;
CensusEntry g_c_ring[kCensusRing]{};
volatile LONG g_proj_installed = 0;
bool g_proj_ready = false;
const char* g_proj_refusal = nullptr;
detail::ProjectileFn g_proj_original = nullptr;
detail::ProjectileFn g_proj_test_original = nullptr;
std::uintptr_t g_proj_base = 0;

void Record(CensusKind kind, bool dropped, bool fields_ok, std::uint16_t owner, std::uint16_t id, std::uint8_t b0, std::uint8_t b1,
            std::uint8_t b2, std::uint16_t aux) noexcept {
    const std::uint32_t seq = static_cast<std::uint32_t>(InterlockedIncrement(&g_c_seq));
    CensusEntry& e = g_c_ring[seq % kCensusRing];
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&e.seq), 0);  // torn until the fields are in
    e.kind = kind;
    e.dropped = dropped;
    e.fields_ok = fields_ok;
    e.owner = owner;
    e.id = id;
    e.b0 = b0;
    e.b1 = b1;
    e.b2 = b2;
    e.aux = aux;
    e.thread = GetCurrentThreadId();
    MemoryBarrier();
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&e.seq), static_cast<LONG>(seq));
}
// The record bytes the census reads (inside the caller's own record; SEH for a
// caller with a shorter record).
bool ReadRecord(const void* record, std::size_t offset, void* out, std::size_t size) noexcept {
    if (!record) return false;
    __try {
        std::memcpy(out, static_cast<const unsigned char*>(record) + offset, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

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
    const bool drop = detail::Drops(static_cast<std::uint16_t>(g_owner), owner);
    if (g_census_on) {
        std::uint16_t id = 0;
        std::uint8_t kind[3]{};
        const bool ok = ReadRecord(record, c::shot_bullet_id, &id, sizeof(id)) && ReadRecord(record, c::shot_kind, kind, sizeof(kind));
        InterlockedIncrement(&g_c_bullets);
        Record(CensusKind::bullet, drop, ok, owner, id, kind[0], kind[1], kind[2], 0);
    }
    if (drop) {
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

const c::Site* NoiseSite() noexcept {
    for (const c::Site& s : c::code_sites)
        if (s.rva == c::noise_fn_rva) return &s;
    return nullptr;
}
// The record is {pos[4], u8 kind, u8 flags, u16 owner} (0x1303CF0..0x1303CFB);
// AddNoise keeps the owner (0x5152F9). Any thread: atomics only.
void NoiseHook(void* self, const void* record) noexcept {
    std::uint16_t owner = 0xFFFF;
    if (record) std::memcpy(&owner, static_cast<const unsigned char*>(record) + c::noise_owner, sizeof(owner));
    InterlockedExchange(&g_noise_last_owner, static_cast<LONG>(owner));
    if (g_census_on) {
        std::uint8_t kind[2]{};
        const bool ok = ReadRecord(record, c::noise_kind, kind, sizeof(kind));
        InterlockedIncrement(&g_c_noises);
        Record(CensusKind::noise, detail::Drops(static_cast<std::uint16_t>(g_owner), owner), ok, owner, kind[0], kind[1], 0, 0, 0);
    }
    if (detail::Drops(static_cast<std::uint16_t>(g_owner), owner)) {
        InterlockedIncrement(&g_noise_dropped);
        return;
    }
    InterlockedIncrement(&g_noise_passed);
    const detail::NoiseFn original = g_noise_test_original ? g_noise_test_original : g_noise_original;
    if (original) original(self, record);
}
std::uintptr_t NoiseHookAddress() noexcept { return reinterpret_cast<std::uintptr_t>(&NoiseHook); }

// The projectile census hook (any thread): records the request and forwards
// it unchanged; never drops (the projectile filter is round 2).
void* ProjectileHook(void* iface, const void* request) noexcept {
    if (g_census_on) {
        std::uint16_t owner = 0xFFFF, id = 0, aux = 0;
        const bool ok = ReadRecord(request, c::proj_owner, &owner, sizeof(owner)) && ReadRecord(request, c::proj_id, &id, sizeof(id)) &&
                        ReadRecord(request, c::proj_aux, &aux, sizeof(aux));
        InterlockedIncrement(&g_c_projectiles);
        Record(CensusKind::projectile, false, ok, owner, id, 0, 0, 0, aux);
    }
    const detail::ProjectileFn original = g_proj_test_original ? g_proj_test_original : g_proj_original;
    return original ? original(iface, request) : nullptr;
}
std::uintptr_t ProjectileHookAddress() noexcept { return reinterpret_cast<std::uintptr_t>(&ProjectileHook); }
const c::Site* ProjectileSite() noexcept {
    for (const c::Site& s : c::code_sites)
        if (s.rva == c::proj_request_rva) return &s;
    return nullptr;
}
// Once per process, only with the census on: the live and the base iface
// vtable slot 0 (0x23E2D50, 0x23E2BD0) hold 0x1337760 (prologue checked);
// each is exchanged with one locked qword exchange; the first is swapped back
// when the second fails.
void InstallProjectileCensus(std::uintptr_t base, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_proj_installed, 1, 0) != 0) return;
    const std::uintptr_t target = base + c::proj_request_rva;
    const std::uintptr_t live = base + c::proj_slot_live_rva, dying = base + c::proj_slot_base_rva;
    const c::Site* site = ProjectileSite();
    unsigned char prologue[sizeof(c::Site::bytes)]{};
    std::uintptr_t live_value = 0, dying_value = 0;
    const char* result = nullptr;
    if (!base || !site || !Read(target, prologue, site->size) || std::memcmp(prologue, site->bytes, site->size) != 0) result = "bytes";
    else if (!ReadValue(live, live_value) || live_value != target) result = "slot_live";
    else if (!ReadValue(dying, dying_value) || dying_value != target) result = "slot_base";
    else {
        g_proj_original = reinterpret_cast<detail::ProjectileFn>(target);
        const std::uintptr_t hook = ProjectileHookAddress();
        if (const char* why = near_code::PatchQword(reinterpret_cast<void*>(live), target, hook))
            if (std::strcmp(why, "protect_restore") != 0) result = why;
        if (!result)
            if (const char* why = near_code::PatchQword(reinterpret_cast<void*>(dying), target, hook))
                if (std::strcmp(why, "protect_restore") != 0) {
                    result = why;
                    near_code::PatchQword(reinterpret_cast<void*>(live), hook, target);
                }
    }
    g_proj_refusal = result;
    g_proj_base = base;
    if (!result) g_proj_ready = true;
    if (log) {
        char line[224]{};
        if (g_proj_ready)
            StringCchPrintfA(line, ARRAYSIZE(line),
                             "partner_fire_census=projectile_installed; slots=0x23e2d50,0x23e2bd0; target=0x1337760; owner_field=0x32; "
                             "forwards=all\r\n");
        else
            StringCchPrintfA(line, ARRAYSIZE(line), "partner_fire_census=projectile_refused; reason=%s\r\n", result);
        log(line);
    }
}
bool ProjectileLive(std::uintptr_t base) noexcept {
    if (!g_proj_ready) return false;
    std::uintptr_t live = 0, dying = 0;
    return ReadValue(base + c::proj_slot_live_rva, live) && ReadValue(base + c::proj_slot_base_rva, dying) &&
           live == ProjectileHookAddress() && dying == ProjectileHookAddress();
}
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
    InterlockedExchange(&g_noise_installed, 0);
    g_noise_ready = false;
    g_noise_original = nullptr;
    InterlockedExchange(&g_noise_dropped, 0);
    InterlockedExchange(&g_noise_passed, 0);
    InterlockedExchange(&g_noise_last_owner, 0xFFFF);
    InterlockedExchange(&g_census_on, 0);
    InterlockedExchange(&g_c_seq, 0);
    InterlockedExchange(&g_c_bullets, 0);
    InterlockedExchange(&g_c_noises, 0);
    InterlockedExchange(&g_c_projectiles, 0);
    std::memset(g_c_ring, 0, sizeof(g_c_ring));
    InterlockedExchange(&g_proj_installed, 0);
    g_proj_ready = false;
    g_proj_refusal = nullptr;
    g_proj_original = nullptr;
    g_proj_base = 0;
}
void SetOriginalForTest(RequestFn original) noexcept { g_test_original = original; }
NoiseFn NoiseHookEntry() noexcept { return &NoiseHook; }
void SetNoiseOriginalForTest(NoiseFn original) noexcept { g_noise_test_original = original; }
ProjectileFn ProjectileHookEntry() noexcept { return &ProjectileHook; }
void SetProjectileOriginalForTest(ProjectileFn original) noexcept { g_proj_test_original = original; }
void RecordForTest(CensusKind kind, bool dropped, std::uint16_t owner, std::uint16_t id) noexcept {
    Record(kind, dropped, true, owner, id, 0, 0, 0, 0);
}
} // namespace detail

const char* Install(const void* base_ptr, LogFn log) noexcept {
    // Anim round 1: the projectile census slot comes with the bullet filter, only with the census on.
    if (g_census_on && base_ptr) InstallProjectileCensus(reinterpret_cast<std::uintptr_t>(base_ptr), log);
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

const char* InstallNoise(const void* base_ptr, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_noise_installed, 1, 0) != 0) return g_noise_ready ? nullptr : "disabled";
    const DWORD error = GetLastError();
    const auto base = reinterpret_cast<std::uintptr_t>(base_ptr);
    const std::uintptr_t target = base + c::noise_fn_rva;
    const std::uintptr_t slot = base + c::noise_vtable_rva;
    const char* result = nullptr;
    const c::Site* site = NoiseSite();
    unsigned char prologue[sizeof(c::Site::bytes)]{};
    std::uintptr_t value = 0;
    if (!base || !site || !Read(target, prologue, site->size) || std::memcmp(prologue, site->bytes, site->size) != 0) {
        result = "bytes";
    } else if (!ReadValue(slot, value) || value != target) {
        result = "slot";
    } else {
        g_noise_original = reinterpret_cast<detail::NoiseFn>(target);
        if (const char* why = near_code::PatchQword(reinterpret_cast<void*>(slot), target, NoiseHookAddress()))
            if (std::strcmp(why, "protect_restore") != 0) result = why;  // protect_restore: the slot holds the hook
    }
    if (!result) g_noise_ready = true;
    if (log) {
        char line[192]{};
        if (g_noise_ready)
            StringCchPrintfA(line, ARRAYSIZE(line), "partner_noise=installed; slot=0x21876a8; target=0x515270; owner_field=0x12\r\n");
        else
            StringCchPrintfA(line, ARRAYSIZE(line), "partner_noise=refused; reason=%s; fire=masked\r\n", result);
        log(line);
    }
    SetLastError(error);
    return g_noise_ready ? nullptr : result;
}

bool NoiseInstalled() noexcept { return g_noise_ready; }

bool NoisePatchLive(const void* base_ptr) noexcept {
    if (!g_noise_ready) return false;
    std::uintptr_t value = 0;
    return ReadValue(reinterpret_cast<std::uintptr_t>(base_ptr) + c::noise_vtable_rva, value) && value == NoiseHookAddress();
}

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
    out.noise_installed = g_noise_ready;
    out.noise_live = NoisePatchLive(base);
    out.noise_dropped = static_cast<std::uint32_t>(g_noise_dropped);
    out.noise_passed = static_cast<std::uint32_t>(g_noise_passed);
    out.noise_last_owner = static_cast<std::uint16_t>(g_noise_last_owner);
}

const char* CensusKindName(CensusKind kind) noexcept {
    switch (kind) {
    case CensusKind::noise: return "noise";
    case CensusKind::projectile: return "projectile";
    default: return "bullet";
    }
}

void SetCensus(bool on) noexcept { InterlockedExchange(&g_census_on, on ? 1 : 0); }
bool CensusOn() noexcept { return g_census_on != 0; }

std::size_t DrainCensus(std::uint32_t& cursor, CensusEntry* out, std::size_t max, std::uint32_t& lost) noexcept {
    lost = 0;
    const std::uint32_t head = static_cast<std::uint32_t>(g_c_seq);
    if (head - cursor > kCensusRing) {
        lost += head - cursor - static_cast<std::uint32_t>(kCensusRing);
        cursor = head - static_cast<std::uint32_t>(kCensusRing);
    }
    std::size_t n = 0;
    while (cursor != head && n < max) {
        const std::uint32_t want = cursor + 1;
        const CensusEntry& e = g_c_ring[want % kCensusRing];
        const std::uint32_t before = static_cast<std::uint32_t>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(
                                                                     const_cast<std::uint32_t*>(&e.seq)), 0, 0));
        MemoryBarrier();
        CensusEntry copy = e;
        MemoryBarrier();
        const std::uint32_t after = static_cast<std::uint32_t>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(
                                                                    const_cast<std::uint32_t*>(&e.seq)), 0, 0));
        cursor = want;
        if (before != want || after != want) { ++lost; continue; }  // overwritten or still being written
        copy.seq = want;
        out[n++] = copy;
    }
    return n;
}

void ReadCensusTotals(CensusTotals& out) noexcept {
    out = CensusTotals{};
    out.projectile_installed = g_proj_ready;
    out.projectile_live = g_proj_base && ProjectileLive(g_proj_base);
    out.projectile_refusal = g_proj_refusal;
    out.bullets = static_cast<std::uint32_t>(g_c_bullets);
    out.noises = static_cast<std::uint32_t>(g_c_noises);
    out.projectiles = static_cast<std::uint32_t>(g_c_projectiles);
    out.recorded = static_cast<std::uint32_t>(g_c_seq);
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
