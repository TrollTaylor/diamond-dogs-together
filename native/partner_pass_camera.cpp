// Original MGS Co-op F1 vehicles, passenger camera (docs/native-puppet.md
// "Passenger camera"; partner_pass_camera.hpp). Connect writes the ridden
// vehicle's stock ignore-collision handle to the local player's camera while
// the driver tick reports the local back-seat passenger, so the fallback
// camera's collision query skips the jeep; it never writes under the stock
// VehicleCamera or over another owner, and clears only its own handle. No code
// or vtable is patched and no game function is called. The driver logs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <atomic>
#include <cstring>
#include "mgscoop/partner_pass_camera.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_pass_camera {
namespace {
namespace c = mgscoop::partner_player_contract;

static_assert(c::pass_cam_module_cam == kModuleCam && c::pass_cam_cam_p == kCamP && c::p_cam == kPCam, "camera links");
static_assert(c::p_local_id == kPLocalId && c::q_act == kQAct && c::board_p_units == kPUnits && c::board_unit_index == kUnitIndex,
              "player offsets");
static_assert(c::cam_connect == kCamConnect && c::cam_connect_on == kCamConnectOn && c::pass_cam_target == kCamTarget, "connect fields");
static_assert(c::pass_cam_mode == kCamMode && c::pass_cam_prev_mode == kCamPrevMode && c::pass_cam_modules == kCamModules, "mode fields");
static_assert(c::pass_cam_pitch == kCamPitch && c::pass_cam_yaw == kCamYaw && c::pass_cam_pitch_prev == kCamPitchPrev &&
                  c::pass_cam_yaw_prev == kCamYawPrev && c::pass_cam_eye == kCamPos && kCamAngles == kCamPitchPrev &&
                  c::pass_cam_rotation == kCamRotation && kCamRot == kCamRotation,
              "the look angles and the eye (design 8.25)");
static_assert(c::pass_cam_aim_module_vtable_rva == kAimModuleVtableRva, "the aim camera module (design 8.26)");

// --- Process state (the DLL is pinned; nothing is torn down) ------------------
volatile LONG g_install_tried = 0;
volatile LONG g_installed = 0;
const char* g_refusal = nullptr;
LogFn g_log = nullptr;
std::atomic<std::uint32_t> g_writes{0}, g_clears{0}, g_faults{0}, g_angles{0};
volatile LONG g_override = -1;  // the lab lever's Mode, -1 none

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

// The two connect fields in the stock order (0x12D1FEE: CAM+0x200, then CAM+0x1F0).
bool WriteConnect(std::uintptr_t cam, std::uint64_t handle, std::uint32_t on) noexcept {
    __try {
        *reinterpret_cast<volatile std::uint32_t*>(cam + kCamConnectOn) = on;
        *reinterpret_cast<volatile std::uint64_t*>(cam + kCamConnect) = handle;
        return true;
    } __except (ReadFilter(GetExceptionCode())) {
        return false;
    }
}

// The local player's camera: *(P+0xB8) with the PlayerCamera vtable (0 when not).
std::uintptr_t LocalCamera(std::uintptr_t base, std::uintptr_t P) noexcept {
    std::uintptr_t cam = 0, cam_vt = 0;
    if (!base || !P || !ReadValue(P + kPCam, cam) || !ReadValue(cam, cam_vt) || cam_vt != base + c::cam_vtable_rva) return 0;
    return cam;
}

const c::Site* SiteAt(std::uintptr_t rva) noexcept {
    for (const c::Site& s : c::code_sites)
        if (s.rva == rva) return &s;
    return nullptr;
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

void ResetForTest() noexcept {
    g_install_tried = g_installed = 0;
    g_refusal = nullptr;
    g_log = nullptr;
    g_writes.store(0);
    g_clears.store(0);
    g_faults.store(0);
    g_angles.store(0);
    g_override = -1;
}
} // namespace detail

const char* Install(const void* base, std::size_t span, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_install_tried, 1, 0) != 0) return g_installed ? nullptr : g_refusal;
    const DWORD error = GetLastError();
    g_log = log;
    std::uintptr_t mismatch = 0;
    if (const char* why = detail::VerifyImage(base, span, mismatch)) {
        g_refusal = why;
        LogFormat("partner_pass_camera=refused; reason=%s; rva=0x%llx\r\n", why, static_cast<unsigned long long>(mismatch));
        SetLastError(error);
        return g_refusal;
    }
    InterlockedExchange(&g_installed, 1);
    LogFormat("partner_pass_camera=installed; mode=connect; field=0x%llx; blocks=%u\r\n", static_cast<unsigned long long>(kCamConnect),
              static_cast<unsigned>(sizeof(c::pass_camera_blocks) / sizeof(c::pass_camera_blocks[0])));
    SetLastError(error);
    return nullptr;
}

bool Installed() noexcept { return g_installed != 0; }
const char* InstallRefusal() noexcept { return g_refusal; }

bool ReadCamera(const void* base, std::uintptr_t P, Camera& out) noexcept {
    out = Camera{};
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    const std::uintptr_t cam = LocalCamera(b, P);
    if (!cam) return false;
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

const char* Connect(const void* base, std::uintptr_t P, std::uint16_t vehicle, bool on, Camera& out) noexcept {
    const DWORD error = GetLastError();
    Camera cam{};
    ReadCamera(base, P, cam);
    const char* reason = "camera";
    const Write plan = on ? ConnectPlan(cam, vehicle, reason) : ReleasePlan(cam, vehicle, reason);
    if (plan != Write::none) {
        // ReadCamera succeeded, so the camera is the local PlayerCamera.
        const std::uintptr_t c_addr = LocalCamera(reinterpret_cast<std::uintptr_t>(base), P);
        const bool set = plan == Write::set;
        if (c_addr && WriteConnect(c_addr, set ? ConnectHandle(vehicle) : 0, set ? 1u : 0u)) {
            (set ? g_writes : g_clears).fetch_add(1, std::memory_order_relaxed);
        } else {
            g_faults.fetch_add(1, std::memory_order_relaxed);
            reason = "fault";
        }
        ReadCamera(base, P, cam);
    }
    out = cam;
    SetLastError(error);
    return reason;
}

void ReadStats(Stats& out) noexcept {
    out = Stats{};
    out.writes = g_writes.load(std::memory_order_relaxed);
    out.clears = g_clears.load(std::memory_order_relaxed);
    out.faults = g_faults.load(std::memory_order_relaxed);
    out.angles = g_angles.load(std::memory_order_relaxed);
}

std::uintptr_t ModeVtable(const void* base, std::uintptr_t P) noexcept {
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    const std::uintptr_t cam = LocalCamera(b, P);
    std::int32_t mode = -1;
    std::uintptr_t module = 0, vt = 0;
    if (!cam || !ReadValue(cam + kCamMode, mode) || mode < 0 || mode >= static_cast<std::int32_t>(kModuleCount) ||
        !ReadValue(cam + kCamModules + 8u * static_cast<unsigned>(mode), module) || !module || !ReadValue(module, vt) || vt < b)
        return 0;
    return vt - b;
}

bool WritePitch(const void* base, std::uintptr_t P, float pitch) noexcept {
    const std::uintptr_t cam = LocalCamera(reinterpret_cast<std::uintptr_t>(base), P);
    if (!cam) return false;
    __try {
        *reinterpret_cast<volatile float*>(cam + kCamPitch) = pitch;
        *reinterpret_cast<volatile float*>(cam + kCamPitchPrev) = pitch;
    } __except (ReadFilter(GetExceptionCode())) {
        g_faults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_angles.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool WriteRotation(const void* base, std::uintptr_t P, const float (&rot)[4]) noexcept {
    const std::uintptr_t cam = LocalCamera(reinterpret_cast<std::uintptr_t>(base), P);
    if (!cam) return false;
    __try {
        for (unsigned i = 0; i < 4; ++i) *reinterpret_cast<volatile float*>(cam + kCamRotation + 4u * i) = rot[i];
    } __except (ReadFilter(GetExceptionCode())) {
        g_faults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_angles.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool ReadView(const void* base, std::uintptr_t P, View& out) noexcept {
    out = View{};
    const std::uintptr_t cam = LocalCamera(reinterpret_cast<std::uintptr_t>(base), P);
    if (!cam) return false;
    View v{};
    for (unsigned i = 0; i < 3; ++i)
        if (!ReadValue(cam + kCamPos + 4u * i, v.pos[i])) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!ReadValue(cam + kCamRot + 4u * i, v.rot[i]) || !ReadValue(cam + kCamAngles + 4u * i, v.ang[i])) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!ReadValue(cam + kCamOffset + 4u * i, v.off[i]) || !ReadValue(cam + kCamPivot + 4u * i, v.piv[i])) return false;
    if (!ReadValue(cam + kCamScale, v.sc[0]) || !ReadValue(cam + kCamScale + 4u, v.sc[1])) return false;
    if (!ReadValue(cam + kCamFlags, v.flags) || !ReadValue(cam + kCamRequests, v.requests)) return false;
    v.ok = true;
    out = v;
    return true;
}

int Override() noexcept { return static_cast<int>(InterlockedCompareExchange(&g_override, 0, 0)); }

bool HandleCommand(const char* text, bool lab, char* result, std::size_t capacity, bool& ok) noexcept {
    ok = false;
    if (!text || std::strncmp(text, "passcam", 7) != 0 || (text[7] != '\0' && text[7] != ' ')) return false;
    const char* arg = text[7] ? text + 8 : "";
    const std::size_t n = std::strlen(arg);
    Mode mode{};
    const bool status = n == 6 && std::memcmp(arg, "status", 6) == 0;
    const bool clear = n == 3 && std::memcmp(arg, "lab", 3) == 0;
    const bool set = ParseMode(arg, n, mode);
    const char* refusal = !lab ? "lab_only" : !(status || clear || set) ? "args" : nullptr;
    if (!refusal && clear) InterlockedExchange(&g_override, -1);
    if (!refusal && set) InterlockedExchange(&g_override, static_cast<LONG>(mode));
    const int value = Override();
    if (result && capacity) {
        if (refusal)
            StringCchPrintfA(result, capacity, "passcam=refused; reason=%s; use=off|connect|drop|aim|lab|status", refusal);
        else
            StringCchPrintfA(result, capacity, "passcam=%s; override=%s; writes=%u; clears=%u; faults=%u",
                             value < 0 ? "lab" : ModeName(static_cast<Mode>(value)), value < 0 ? "none" : "set",
                             g_writes.load(std::memory_order_relaxed), g_clears.load(std::memory_order_relaxed),
                             g_faults.load(std::memory_order_relaxed));
    }
    ok = refusal == nullptr;
    return true;
}

} // namespace mgscoop::partner_pass_camera
