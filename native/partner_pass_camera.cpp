// Original MGS Co-op F1 vehicles, passenger camera (docs/native-puppet.md
// "Passenger camera"; partner_pass_camera.hpp). The VehicleCamera module's
// CanActivate slot (+0x18 of 0x23D67F0 and 0x23D6720) points at Hook: the stock
// answer, and also true for the local back-seat passenger the driver tick armed.
// The wrapper only reads (SEH) and counts; the driver logs. Nothing here writes
// the camera or the player; the stock module does the connect and the release.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <atomic>
#include <cstring>
#include "mgscoop/partner_pass_camera.hpp"
#include "mgscoop/near_code.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_pass_camera {
namespace {
namespace c = mgscoop::partner_player_contract;

static_assert(c::pass_cam_module_cam == kModuleCam && c::pass_cam_cam_p == kCamP && c::p_cam == kPCam, "camera links");
static_assert(c::p_local_id == kPLocalId && c::q_act == kQAct && c::board_p_units == kPUnits && c::board_unit_index == kUnitIndex,
              "player offsets");
static_assert(c::cam_connect == kCamConnect && c::cam_connect_on == kCamConnectOn && c::pass_cam_target == kCamTarget, "connect fields");
static_assert(c::pass_cam_mode == kCamMode && c::pass_cam_prev_mode == kCamPrevMode && c::pass_cam_modules == kCamModules, "mode fields");

// --- Process state (the DLL is pinned; nothing is torn down) ------------------
volatile LONG g_install_tried = 0;
volatile LONG g_installed = 0;
const char* g_refusal = nullptr;
std::uintptr_t g_base = 0;
LogFn g_log = nullptr;
NowFn g_now = nullptr;
detail::CanActivateFn g_original = nullptr;
std::uintptr_t g_cam_vtable = 0, g_module_vtable = 0, g_module_base_vtable = 0;

std::atomic<std::uint64_t> g_arm{0};
std::atomic<std::uint32_t> g_calls{0}, g_stock_true{0}, g_grants{0}, g_refusals{0}, g_faults{0};
std::atomic<std::uint32_t> g_last_act{0};
std::atomic<const char*> g_last_reason{"none"};

void LogFormat(const char* format, ...) noexcept {
    if (!g_log) return;
    char line[256]{};
    va_list arguments;
    va_start(arguments, format);
    const HRESULT result = StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    if (SUCCEEDED(result)) g_log(line);
}

bool IsAccessFault(DWORD code) noexcept { return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR; }
int ReadFilter(DWORD code) noexcept { return IsAccessFault(code) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH; }

template <class T> bool ReadValue(std::uintptr_t at, T& out) noexcept {
    if (!at) return false;
    __try {
        out = *reinterpret_cast<const volatile T*>(at);
        return true;
    } __except (ReadFilter(GetExceptionCode())) {
        return false;
    }
}

// The original CanActivate under SEH (a fault answers false and counts).
bool CallOriginal(void* module, bool& faulted) noexcept {
    faulted = false;
    if (!g_original) return false;
    __try {
        return g_original(module);
    } __except (ReadFilter(GetExceptionCode())) {
        faulted = true;
        return false;
    }
}

// What the wrapper reads from the module's own camera (the same chain the stock
// CanActivate and the connect 0x12D1E80 read), plus the camera identity checks.
Live ReadLive(void* module) noexcept {
    Live l{};
    const auto m = reinterpret_cast<std::uintptr_t>(module);
    std::uintptr_t module_vt = 0, cam = 0, cam_vt = 0, P = 0, p_cam = 0, Q = 0, acts = 0, units = 0, unit = 0;
    std::uint32_t local = 0;
    std::uint16_t act = 0, index = 0xFFFF;
    if (!ReadValue(m, module_vt) || !ReadValue(m + kModuleCam, cam) || !ReadValue(cam, cam_vt) || !ReadValue(cam + kCamP, P) ||
        !ReadValue(P + kPCam, p_cam) || !ReadValue(P + kPLocalId, local) || local > 1 || !ReadValue(P + kPQ, Q) ||
        !ReadValue(Q + kQAct, acts) || !ReadValue(acts + 2u * local, act) || !ReadValue(P + kPUnits, units) ||
        !ReadValue(units + 8u * local, unit))
        return l;
    if (unit && !ReadValue(unit + kUnitIndex, index)) return l;
    l.read_ok = true;
    // The local player's camera only: this module's camera is P's camera, a
    // PlayerCamera, and the module is a VehicleCamera.
    l.cam_match = p_cam == cam && cam_vt == g_cam_vtable && (module_vt == g_module_vtable || module_vt == g_module_base_vtable);
    l.act = act;
    l.unit = unit != 0;
    l.index = index;
    return l;
}

// VehicleCamera vt+0x18 (RCX = the module; returns AL). The stock answer first;
// only a false stock answer with an armed word reads anything more.
bool Hook(void* module) noexcept {
    const DWORD error = GetLastError();
    g_calls.fetch_add(1, std::memory_order_relaxed);
    bool faulted = false;
    if (CallOriginal(module, faulted)) {
        g_stock_true.fetch_add(1, std::memory_order_relaxed);
        SetLastError(error);
        return true;
    }
    if (faulted) g_faults.fetch_add(1, std::memory_order_relaxed);
    const Arm a = Unpack(g_arm.load(std::memory_order_acquire));
    if (!a.armed) {
        SetLastError(error);
        return false;
    }
    const std::uint32_t now = g_now ? static_cast<std::uint32_t>(g_now()) : 0u;
    const Live l = ReadLive(module);
    if (!l.read_ok) g_faults.fetch_add(1, std::memory_order_relaxed);
    const char* reason = "none";
    const bool grant = Grant(a, now, l, reason);
    g_last_reason.store(reason, std::memory_order_relaxed);
    g_last_act.store(l.act, std::memory_order_relaxed);
    (grant ? g_grants : g_refusals).fetch_add(1, std::memory_order_relaxed);
    SetLastError(error);
    return grant;
}

const c::Site* SiteAt(std::uintptr_t rva) noexcept {
    for (const c::Site& s : c::code_sites)
        if (s.rva == rva) return &s;
    return nullptr;
}

bool SlotHolds(std::uintptr_t slot, std::uintptr_t value) noexcept {
    std::uintptr_t live = 0;
    return ReadValue(slot, live) && live == value;
}
} // namespace

namespace detail {
const char* VerifyImage(const void* base, std::size_t span, std::uintptr_t& mismatch_rva) noexcept {
    mismatch_rva = 0;
    const auto* image = static_cast<const unsigned char*>(base);
    if (!image) return "base";
    for (const std::uintptr_t rva : c::pass_camera_blocks) {
        mismatch_rva = rva;
        const c::Site* site = SiteAt(rva);
        if (!site) return "contract";
        if (site->rva > span || site->size > span - site->rva) return "span";
        bool same = true;
        __try {
            same = std::memcmp(image + site->rva, site->bytes, site->size) == 0;
        } __except (ReadFilter(GetExceptionCode())) {
            return "read";
        }
        if (!same) return "bytes";
    }
    mismatch_rva = 0;
    return nullptr;
}

void BindForTest(CanActivateFn original, NowFn now, std::uintptr_t cam_vtable, std::uintptr_t module_vtable,
                 std::uintptr_t module_base_vtable) noexcept {
    g_original = original;
    g_now = now;
    g_cam_vtable = cam_vtable;
    g_module_vtable = module_vtable;
    g_module_base_vtable = module_base_vtable;
}
bool HookForTest(void* module) noexcept { return Hook(module); }
void ResetForTest() noexcept {
    g_install_tried = g_installed = 0;
    g_refusal = nullptr;
    g_base = 0;
    g_log = nullptr;
    g_now = nullptr;
    g_original = nullptr;
    g_cam_vtable = g_module_vtable = g_module_base_vtable = 0;
    g_arm.store(0);
    g_calls.store(0);
    g_stock_true.store(0);
    g_grants.store(0);
    g_refusals.store(0);
    g_faults.store(0);
    g_last_act.store(0);
    g_last_reason.store("none");
}
} // namespace detail

const char* Install(const void* base, std::size_t span, NowFn now, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_install_tried, 1, 0) != 0) return g_installed ? nullptr : g_refusal;
    const DWORD error = GetLastError();
    g_base = reinterpret_cast<std::uintptr_t>(base);
    g_log = log;
    g_now = now;
    std::uintptr_t mismatch = 0;
    if (const char* why = detail::VerifyImage(base, span, mismatch)) {
        g_refusal = why;
        LogFormat("partner_pass_camera=refused; reason=%s; rva=0x%llx\r\n", why, static_cast<unsigned long long>(mismatch));
        SetLastError(error);
        return g_refusal;
    }
    const std::uintptr_t target = g_base + c::pass_cam_can_activate_rva;
    const std::uintptr_t live = g_base + c::pass_cam_module_vtable_rva + c::pass_cam_slot;
    const std::uintptr_t dying = g_base + c::pass_cam_module_base_vtable_rva + c::pass_cam_slot;
    const bool live_ok = span >= c::pass_cam_module_vtable_rva + c::pass_cam_slot + 8 && SlotHolds(live, target);
    const bool dying_ok = span >= c::pass_cam_module_base_vtable_rva + c::pass_cam_slot + 8 && SlotHolds(dying, target);
    if (!live_ok || !dying_ok) {
        g_refusal = !live_ok ? "slot_live" : "slot_base";
        LogFormat("partner_pass_camera=refused; reason=%s; slot=0x%llx\r\n", g_refusal,
                  static_cast<unsigned long long>((!live_ok ? live : dying) - g_base));
        SetLastError(error);
        return g_refusal;
    }
    g_original = reinterpret_cast<detail::CanActivateFn>(target);
    g_cam_vtable = g_base + c::cam_vtable_rva;
    g_module_vtable = g_base + c::pass_cam_module_vtable_rva;
    g_module_base_vtable = g_base + c::pass_cam_module_base_vtable_rva;
    const auto hook = reinterpret_cast<std::uintptr_t>(&Hook);
    bool warn = false;
    if (const char* why = near_code::PatchQword(reinterpret_cast<void*>(live), target, hook)) {
        if (std::strcmp(why, "protect_restore") != 0) {
            g_refusal = why;
            LogFormat("partner_pass_camera=refused; reason=%s; at=live\r\n", why);
            SetLastError(error);
            return g_refusal;
        }
        warn = true;
    }
    if (const char* why = near_code::PatchQword(reinterpret_cast<void*>(dying), target, hook)) {
        if (std::strcmp(why, "protect_restore") != 0) {
            // The base slot failed: the live one goes back to the stock function.
            const char* back = near_code::PatchQword(reinterpret_cast<void*>(live), hook, target);
            const bool reverted = !back || std::strcmp(back, "protect_restore") == 0;
            g_refusal = reverted ? "base_failed" : "base_revert";
            LogFormat("partner_pass_camera=refused; reason=%s; why=%s; live_reverted=%u\r\n", g_refusal, why, reverted ? 1u : 0u);
            SetLastError(error);
            return g_refusal;
        }
        warn = true;
    }
    InterlockedExchange(&g_installed, 1);
    LogFormat("partner_pass_camera=installed; slots=0x%llx+0x%llx,0x%llx+0x%llx; target=0x%llx; blocks=%u; warn=%s\r\n",
              static_cast<unsigned long long>(c::pass_cam_module_vtable_rva), static_cast<unsigned long long>(c::pass_cam_slot),
              static_cast<unsigned long long>(c::pass_cam_module_base_vtable_rva), static_cast<unsigned long long>(c::pass_cam_slot),
              static_cast<unsigned long long>(c::pass_cam_can_activate_rva),
              static_cast<unsigned>(sizeof(c::pass_camera_blocks) / sizeof(c::pass_camera_blocks[0])), warn ? "protect_restore" : "none");
    SetLastError(error);
    return nullptr;
}

bool Installed() noexcept { return g_installed != 0; }
const char* InstallRefusal() noexcept { return g_refusal; }

bool PatchLive(const void* base) noexcept {
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    if (!g_installed || !b) return false;
    const auto hook = reinterpret_cast<std::uintptr_t>(&Hook);
    return SlotHolds(b + c::pass_cam_module_vtable_rva + c::pass_cam_slot, hook) &&
           SlotHolds(b + c::pass_cam_module_base_vtable_rva + c::pass_cam_slot, hook);
}

void Publish(std::uint64_t word) noexcept { g_arm.store(word, std::memory_order_release); }
std::uint64_t Published() noexcept { return g_arm.load(std::memory_order_acquire); }

void ReadStats(Stats& out) noexcept {
    out = Stats{};
    out.calls = g_calls.load(std::memory_order_relaxed);
    out.stock_true = g_stock_true.load(std::memory_order_relaxed);
    out.grants = g_grants.load(std::memory_order_relaxed);
    out.refusals = g_refusals.load(std::memory_order_relaxed);
    out.faults = g_faults.load(std::memory_order_relaxed);
    out.last_reason = g_last_reason.load(std::memory_order_relaxed);
    out.last_act = static_cast<std::uint16_t>(g_last_act.load(std::memory_order_relaxed));
}

bool ReadCamera(const void* base, std::uintptr_t P, Camera& out) noexcept {
    out = Camera{};
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    std::uintptr_t cam = 0, cam_vt = 0;
    if (!b || !P || !ReadValue(P + kPCam, cam) || !ReadValue(cam, cam_vt) || cam_vt != b + c::cam_vtable_rva) return false;
    Camera v{};
    if (!ReadValue(cam + kCamConnect, v.connect) || !ReadValue(cam + kCamTarget, v.target) || !ReadValue(cam + kCamConnectOn, v.on) ||
        !ReadValue(cam + kCamMode, v.mode) || !ReadValue(cam + kCamPrevMode, v.prev))
        return false;
    for (unsigned i = 0; i < kModuleCount; ++i) {
        std::uintptr_t module = 0, vt = 0;
        if (!ReadValue(cam + kCamModules + 8u * i, module) || !module) continue;
        if (ReadValue(module, vt) && (vt == b + c::pass_cam_module_vtable_rva || vt == b + c::pass_cam_module_base_vtable_rva)) {
            v.vehicle_mode = static_cast<std::int32_t>(i);
            break;
        }
    }
    v.ok = true;
    out = v;
    return true;
}

} // namespace mgscoop::partner_pass_camera
