// F1 passenger camera (docs/native-puppet.md "Passenger camera"): the armed
// word (pack, lease), the wrapper's decision (never the driver, never another
// camera, only the armed vehicle), the driver's arm, the per-boarding episode
// (connect, refused, release), the CanActivate wrapper with a bound fake
// original on a synthetic camera / player chain (stock first, no reads when
// disarmed, SEH faults, last error kept), the camera read, and the pinned
// blocks plus the two-slot install on a synthetic image. Nothing here touches
// MGSV; the fixture buffers are never executed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "mgscoop/partner_pass_camera.hpp"
#include "mgscoop/partner_pass_camera_policy.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace pc = mgscoop::partner_pass_camera;
namespace c = mgscoop::partner_player_contract;
unsigned checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %u: %s\n", checks, message); std::exit(1); }
}
bool Same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

void TestPackAndLease() {
    pc::Arm a{};
    a.armed = true; a.vehicle = 25608; a.seat = 2; a.until_ms = 0xFEDCBA98u;
    const pc::Arm b = pc::Unpack(pc::Pack(a));
    Check(b.armed && b.vehicle == 25608 && b.seat == 2 && b.until_ms == 0xFEDCBA98u, "pack round trip");
    Check(pc::Pack(pc::Arm{}) == 0 && !pc::Unpack(0).armed, "disarmed is word 0");
    Check(!pc::Unpack(pc::Pack(a) ^ (std::uint64_t{3} << 24)).armed, "an unknown flag byte reads as disarmed");
    Check(pc::LeaseLive(1000, 900) && pc::LeaseLive(1000, 1000) && !pc::LeaseLive(1000, 1001), "the lease ends at until");
    Check(pc::LeaseLive(2500, 1000) && !pc::LeaseLive(2501, 1000), "a lease further than 1.5 s ahead is a foreign clock");
    Check(pc::LeaseLive(3, 0xFFFFFFF0u) && !pc::LeaseLive(0xFFFFFFF0u, 3), "the lease is wrap-safe");
    Check(pc::ConnectHandle(25608) == 0x8800000000006408ull, "the stock handle encoding (the get-off's 0x8800000000006408)");
    Check(c::cam_connect == pc::kCamConnect && c::cam_connect_on == pc::kCamConnectOn && c::p_cam == pc::kPCam, "contract offsets");
}

pc::Arm Armed(std::uint16_t vehicle = 25608, std::uint8_t seat = 2, std::uint32_t until = 2000) {
    pc::Arm a{};
    a.armed = true; a.vehicle = vehicle; a.seat = seat; a.until_ms = until;
    return a;
}
pc::Live Seated(std::uint16_t act = pc::kActPassenger, std::uint16_t index = 8) {
    pc::Live l{};
    l.read_ok = true; l.cam_match = true; l.act = act; l.unit = true; l.index = index;
    return l;
}

void TestGrant() {
    const char* why = nullptr;
    Check(pc::Grant(Armed(), 1000, Seated(), why) && Same(why, "grant"), "the armed passenger of 25608 is granted");
    Check(!pc::Grant(pc::Arm{}, 1000, Seated(), why) && Same(why, "off"), "disarmed: never");
    Check(!pc::Grant(Armed(25608, 2, 999), 1000, Seated(), why) && Same(why, "lease"), "a lapsed arm: never");
    pc::Live l = Seated();
    l.read_ok = false;
    Check(!pc::Grant(Armed(), 1000, l, why) && Same(why, "read"), "an unread chain: never");
    l = Seated();
    l.cam_match = false;
    Check(!pc::Grant(Armed(), 1000, l, why) && Same(why, "camera"), "not the local player's camera: never");
    Check(!pc::Grant(Armed(), 1000, Seated(pc::kActDriver), why) && Same(why, "act"), "the driver: never (stock owns it)");
    Check(!pc::Grant(Armed(), 1000, Seated(pc::kActGetOff), why) && Same(why, "act"), "the get-off: stock owns it");
    Check(!pc::Grant(Armed(), 1000, Seated(1), why) && Same(why, "act"), "on foot: never");
    l = Seated();
    l.unit = false;
    Check(!pc::Grant(Armed(), 1000, l, why) && Same(why, "unit"), "no ridden unit: never");
    Check(!pc::Grant(Armed(), 1000, Seated(pc::kActPassenger, 0x200), why) && Same(why, "unit"), "an index past 0x1FF: never");
    Check(!pc::Grant(Armed(), 1000, Seated(pc::kActPassenger, 3), why) && Same(why, "vehicle"), "another vehicle: never");
}

void TestArmFor() {
    pc::ArmIn in{};
    in.passenger_mode = true; in.installed = true; in.live = true; in.passenger = true; in.vehicle = 25608; in.seat = 3; in.now_ms = 0x1'0000'0100ull;
    const char* why = nullptr;
    pc::Arm a = pc::ArmFor(in, why);
    Check(a.armed && a.vehicle == 25608 && a.seat == 3 && a.until_ms == 0x100u + pc::kArmLeaseMs && Same(why, "armed"),
          "armed for the reported back seat, until now + lease (low 32 bits)");
    pc::ArmIn x = in; x.passenger_mode = false;
    Check(!pc::ArmFor(x, why).armed && Same(why, "mode"), "block / off: never");
    x = in; x.passenger = false;
    Check(!pc::ArmFor(x, why).armed && Same(why, "not_passenger"), "no local passenger: never");
    x = in; x.installed = false;
    Check(!pc::ArmFor(x, why).armed && Same(why, "install"), "not installed");
    x = in; x.live = false;
    Check(!pc::ArmFor(x, why).armed && Same(why, "patch_lost"), "the slot swap lost");
    x = in; x.uncertain = true;
    Check(!pc::ArmFor(x, why).armed && Same(why, "uncertain"), "a mission latch");
    x = in; x.seat = 0;
    Check(!pc::ArmFor(x, why).armed && Same(why, "seat"), "the driver's seat: never");
    x = in; x.seat = 1;
    Check(!pc::ArmFor(x, why).armed && Same(why, "seat"), "seat 1: never");
    x = in; x.vehicle = 0xFFFF;
    Check(!pc::ArmFor(x, why).armed && Same(why, "vehicle"), "an unknown vehicle");
    x = in; x.vehicle = 0x2003;
    Check(!pc::ArmFor(x, why).armed && Same(why, "vehicle"), "not a vehicle-type id");
}

pc::Camera Cam(std::uint64_t connect, std::uint32_t on, std::int32_t mode = 3, std::int32_t vmode = 3) {
    pc::Camera k{};
    k.ok = true; k.connect = connect; k.on = on; k.mode = mode; k.prev = 1; k.vehicle_mode = vmode;
    return k;
}
pc::StepIn In(bool passenger, std::uint64_t now, std::uint32_t grants, pc::Camera cam, bool armed = true) {
    pc::StepIn in{};
    in.passenger = passenger; in.vehicle = passenger ? 25608 : 0xFFFF; in.seat = passenger ? 2 : 0xFF;
    in.armed = armed; in.arm_reason = armed ? "armed" : "install"; in.cam = cam; in.grants = grants; in.now_ms = now;
    return in;
}

void TestEpisode() {
    {
        // A boarding: connect after a grant and the stock handle; once only; release at the stock clear.
        pc::Episode e{};
        Check(pc::Step(e, In(false, 100, 0, pc::Camera{})).event == pc::Event::none && !e.active, "on foot: nothing");
        pc::StepOut o = pc::Step(e, In(true, 1000, 5, Cam(0, 0, 1, 3)));
        Check(o.event == pc::Event::none && e.active && e.grants_at_start == 5, "boarded: waiting for the connect");
        o = pc::Step(e, In(true, 1100, 5, Cam(pc::ConnectHandle(25608), 1)));
        Check(o.event == pc::Event::none, "a handle without our grant is not ours (the stock get-off of an earlier ride)");
        o = pc::Step(e, In(true, 1200, 9, Cam(pc::ConnectHandle(25608), 1)));
        Check(o.event == pc::Event::connect && Same(o.reason, "vehicle_camera") && o.ms == 200 && o.vehicle == 25608 && o.seat == 2,
              "connect: our grant and the vehicle's handle");
        Check(pc::Step(e, In(true, 1300, 12, Cam(pc::ConnectHandle(25608), 1))).event == pc::Event::none, "connect logs once");
        Check(pc::Step(e, In(true, 9000, 12, Cam(pc::ConnectHandle(25608), 1))).event == pc::Event::none, "no timeout after the connect");
        // The get-off (act 0x70): the report ends, the stock VehicleCamera still holds the handle.
        o = pc::Step(e, In(false, 9100, 12, Cam(pc::ConnectHandle(25608), 1)));
        Check(o.event == pc::Event::none && !e.active && e.releasing, "get-off: releasing, the handle still held");
        o = pc::Step(e, In(false, 10100, 12, Cam(0, 0, 1, 3)));
        Check(o.event == pc::Event::release && Same(o.reason, "stock") && o.ok && o.ms == 1000 && o.vehicle == 25608,
              "release: FinishState / Deactivate cleared it");
        Check(pc::Step(e, In(false, 10200, 12, Cam(0, 0))).event == pc::Event::none && !e.releasing, "released once");
    }
    {
        // No grant within kConnectMs: refused no_grant (a module before the VehicleCamera won); once.
        pc::Episode e{};
        pc::Step(e, In(true, 1000, 0, Cam(0, 0, 1, 3)));
        Check(pc::Step(e, In(true, 3999, 0, Cam(0, 0, 1, 3))).event == pc::Event::none, "inside the connect window");
        pc::StepOut o = pc::Step(e, In(true, 4000, 0, Cam(0, 0, 1, 3)));
        Check(o.event == pc::Event::refused && Same(o.reason, "no_grant") && o.ms == 3000, "refused: no grant");
        Check(pc::Step(e, In(true, 9000, 3, Cam(pc::ConnectHandle(25608), 1))).event == pc::Event::none, "refused logs once per boarding");
    }
    {
        // Granted but the handle never came: not_connected; an unread camera: cam_read.
        pc::Episode e{};
        pc::Step(e, In(true, 1000, 0, Cam(0, 0)));
        pc::StepOut o = pc::Step(e, In(true, 4000, 2, Cam(0, 0)));
        Check(o.event == pc::Event::refused && Same(o.reason, "not_connected"), "granted, no handle");
        pc::Episode f{};
        pc::Step(f, In(true, 1000, 0, Cam(0, 0)));
        o = pc::Step(f, In(true, 4000, 2, pc::Camera{}));
        Check(o.event == pc::Event::refused && Same(o.reason, "cam_read"), "granted, camera unread");
    }
    {
        // Not armed at boarding: refused with the arm's reason, at once.
        pc::Episode e{};
        pc::StepOut o = pc::Step(e, In(true, 1000, 0, Cam(0, 0), false));
        Check(o.event == pc::Event::refused && Same(o.reason, "install") && o.ms == 0, "refused: the arm's reason");
    }
    {
        // Release by another owner, and the release timeout.
        pc::Episode e{};
        pc::Step(e, In(true, 1000, 0, Cam(0, 0)));
        pc::Step(e, In(true, 1100, 1, Cam(pc::ConnectHandle(25608), 1)));
        pc::Step(e, In(false, 2000, 1, Cam(pc::ConnectHandle(25608), 1)));
        pc::StepOut o = pc::Step(e, In(false, 2100, 1, Cam(pc::ConnectHandle(25601), 1)));
        Check(o.event == pc::Event::release && Same(o.reason, "other") && o.ok, "another owner took the camera");
        pc::Episode f{};
        pc::Step(f, In(true, 1000, 0, Cam(0, 0)));
        pc::Step(f, In(true, 1100, 1, Cam(pc::ConnectHandle(25608), 1)));
        pc::Step(f, In(false, 2000, 1, Cam(pc::ConnectHandle(25608), 1)));
        Check(pc::Step(f, In(false, 9999, 1, Cam(pc::ConnectHandle(25608), 1))).event == pc::Event::none, "inside the release window");
        o = pc::Step(f, In(false, 10000, 1, Cam(pc::ConnectHandle(25608), 1)));
        Check(o.event == pc::Event::release && Same(o.reason, "still_connected") && !o.ok && o.ms == 8000, "release timeout: ok=0");
    }
    {
        // A second boarding after the release is a new episode with its own counters.
        pc::Episode e{};
        pc::Step(e, In(true, 1000, 0, Cam(0, 0)));
        pc::Step(e, In(true, 1100, 1, Cam(pc::ConnectHandle(25608), 1)));
        pc::Step(e, In(false, 2000, 1, Cam(0, 0)));
        pc::StepOut o = pc::Step(e, In(true, 3000, 4, Cam(0, 0)));
        Check(o.event == pc::Event::none && e.active && e.grants_at_start == 4 && !e.connected, "a new boarding");
        o = pc::Step(e, In(true, 3100, 5, Cam(pc::ConnectHandle(25608), 1)));
        Check(o.event == pc::Event::connect, "connects again");
    }
}

// --- The wrapper on a synthetic chain ------------------------------------------
struct Chain {
    unsigned char module[0x40]{};
    unsigned char cam[0x1000]{};
    unsigned char player[0x400]{};
    unsigned char q[0x40]{};
    std::uint16_t acts[2]{};
    std::uintptr_t units[2]{};
    unsigned char unit[0x100]{};
    static constexpr std::uintptr_t kCamVt = 0x1423B8250, kModuleVt = 0x1423D67F0, kModuleBaseVt = 0x1423D6720;
    template <class T> static void Put(unsigned char* base, std::size_t at, T value) { std::memcpy(base + at, &value, sizeof(T)); }
    std::uintptr_t A(const void* p) const { return reinterpret_cast<std::uintptr_t>(p); }
    Chain() {
        Put<std::uintptr_t>(module, 0, kModuleVt);
        Put<std::uintptr_t>(module, pc::kModuleCam, A(cam));
        Put<std::uintptr_t>(cam, 0, kCamVt);
        Put<std::uintptr_t>(cam, pc::kCamP, A(player));
        Put<std::uintptr_t>(player, pc::kPCam, A(cam));
        Put<std::uint32_t>(player, pc::kPLocalId, 0);
        Put<std::uintptr_t>(player, pc::kPQ, A(q));
        Put<std::uintptr_t>(q, pc::kQAct, A(acts));
        Put<std::uintptr_t>(player, pc::kPUnits, A(units));
        acts[0] = pc::kActPassenger;
        acts[1] = 1;
        units[0] = A(unit);
        Put<std::uint16_t>(unit, pc::kUnitIndex, 8);
    }
};
struct Fakes {
    unsigned original = 0;
    bool answer = false;
    bool fault = false;
    DWORD error_seen = 0;
    std::uint64_t now = 1000;
} g;
volatile std::uint8_t* volatile g_null = nullptr;
bool FakeOriginal(void*) {
    ++g.original;
    g.error_seen = GetLastError();
    SetLastError(77);
    if (g.fault) return *g_null != 0;  // an access fault inside the original
    return g.answer;
}
std::uint64_t FakeNow() noexcept { return g.now; }
void Bind() {
    pc::detail::ResetForTest();
    g = Fakes{};
    pc::detail::BindForTest(&FakeOriginal, &FakeNow, Chain::kCamVt, Chain::kModuleVt, Chain::kModuleBaseVt);
}

void TestWrapper() {
    Chain k;
    {
        // The stock answer first: true passes through without any read (the driver, the get-off).
        Bind();
        g.answer = true;
        pc::Publish(pc::Pack(Armed(25608, 2, 2000)));
        SetLastError(1234);
        Check(pc::detail::HookForTest(k.module) && g.original == 1 && GetLastError() == 1234, "stock true: true, the last error kept");
        pc::Stats s{};
        pc::ReadStats(s);
        Check(s.calls == 1 && s.stock_true == 1 && s.grants == 0 && s.refusals == 0, "counted as stock");
    }
    {
        // Disarmed: the stock false, no read (a garbage module never faults).
        Bind();
        unsigned char garbage[16]{};
        std::memset(garbage, 0xFF, sizeof(garbage));
        Check(!pc::detail::HookForTest(garbage) && g.original == 1, "disarmed: stock false");
        pc::Stats s{};
        pc::ReadStats(s);
        Check(s.faults == 0 && s.grants == 0 && s.refusals == 0, "disarmed: nothing read");
    }
    {
        // Armed: the reported passenger of 25608 (index 8) is granted; the original ran first.
        Bind();
        pc::Publish(pc::Pack(Armed(25608, 2, 2000)));
        SetLastError(4321);
        Check(pc::detail::HookForTest(k.module) && g.original == 1 && g.error_seen == 4321 && GetLastError() == 4321,
              "armed passenger: granted, the caller's last error kept");
        pc::Stats s{};
        pc::ReadStats(s);
        Check(s.grants == 1 && Same(s.last_reason, "grant") && s.last_act == pc::kActPassenger, "the grant is counted");
        // The base vtable is the same module (construction / destruction).
        Chain::Put<std::uintptr_t>(k.module, 0, Chain::kModuleBaseVt);
        Check(pc::detail::HookForTest(k.module), "the base VehicleCamera vtable: granted too");
        Chain::Put<std::uintptr_t>(k.module, 0, Chain::kModuleVt);
    }
    {
        // Every refusal: the driver act, another vehicle, another camera, a foreign module, no unit, the lease.
        Bind();
        pc::Publish(pc::Pack(Armed(25608, 2, 2000)));
        k.acts[0] = pc::kActDriver;
        Check(!pc::detail::HookForTest(k.module), "act 0x6F with a stock false: not ours");
        k.acts[0] = pc::kActPassenger;
        pc::Publish(pc::Pack(Armed(25601, 2, 2000)));
        Check(!pc::detail::HookForTest(k.module), "armed for another vehicle");
        pc::Publish(pc::Pack(Armed(25608, 2, 2000)));
        unsigned char other[0x1000]{};
        Chain::Put<std::uintptr_t>(k.player, pc::kPCam, reinterpret_cast<std::uintptr_t>(other));
        Check(!pc::detail::HookForTest(k.module), "P's camera is another one");
        Chain::Put<std::uintptr_t>(k.player, pc::kPCam, reinterpret_cast<std::uintptr_t>(k.cam));
        Chain::Put<std::uintptr_t>(k.cam, 0, 0x1400000000ull);
        Check(!pc::detail::HookForTest(k.module), "not a PlayerCamera");
        Chain::Put<std::uintptr_t>(k.cam, 0, Chain::kCamVt);
        Chain::Put<std::uintptr_t>(k.module, 0, 0x1423D1DB0ull);
        Check(!pc::detail::HookForTest(k.module), "not a VehicleCamera module (the subjective camera's vtable)");
        Chain::Put<std::uintptr_t>(k.module, 0, Chain::kModuleVt);
        k.units[0] = 0;
        Check(!pc::detail::HookForTest(k.module), "no ridden unit");
        k.units[0] = reinterpret_cast<std::uintptr_t>(k.unit);
        g.now = 2001;
        Check(!pc::detail::HookForTest(k.module), "the arm lapsed");
        g.now = 1000;
        pc::Stats s{};
        pc::ReadStats(s);
        Check(s.grants == 0 && s.refusals == 7 && s.faults == 0 && Same(s.last_reason, "lease"), "seven refusals, no fault");
        Check(pc::detail::HookForTest(k.module), "the chain restored: granted");
    }
    {
        // SEH: a bad camera pointer while armed, and a faulting original.
        Bind();
        pc::Publish(pc::Pack(Armed(25608, 2, 2000)));
        Chain bad;
        Chain::Put<std::uintptr_t>(bad.module, pc::kModuleCam, 0x10);
        Check(!pc::detail::HookForTest(bad.module), "an unreadable camera: false");
        pc::Stats s{};
        pc::ReadStats(s);
        Check(s.faults == 1 && Same(s.last_reason, "read"), "the read fault is counted");
        g.fault = true;
        Check(!pc::detail::HookForTest(bad.module), "a faulting original: false");
        pc::ReadStats(s);
        Check(s.faults == 3, "the original's fault and the read fault are counted");
        pc::detail::ResetForTest();
    }
}

void TestReadCamera() {
    Chain k;
    const std::uintptr_t base = 0x140000000;
    Chain::Put<std::uintptr_t>(k.cam, 0, base + c::cam_vtable_rva);
    Chain::Put<std::uint64_t>(k.cam, pc::kCamConnect, pc::ConnectHandle(25608));
    Chain::Put<std::uint64_t>(k.cam, pc::kCamTarget, 0);
    Chain::Put<std::uint32_t>(k.cam, pc::kCamConnectOn, 1);
    Chain::Put<std::int32_t>(k.cam, pc::kCamMode, 3);
    Chain::Put<std::int32_t>(k.cam, pc::kCamPrevMode, 1);
    unsigned char other[16]{};
    Chain::Put<std::uintptr_t>(other, 0, base + 0x23D1DB0);
    Chain::Put<std::uintptr_t>(k.module, 0, base + c::pass_cam_module_vtable_rva);
    Chain::Put<std::uintptr_t>(k.cam, pc::kCamModules + 0, reinterpret_cast<std::uintptr_t>(other));
    Chain::Put<std::uintptr_t>(k.cam, pc::kCamModules + 8, 0);
    Chain::Put<std::uintptr_t>(k.cam, pc::kCamModules + 24, reinterpret_cast<std::uintptr_t>(k.module));
    pc::Camera cam{};
    Check(pc::ReadCamera(reinterpret_cast<const void*>(base), reinterpret_cast<std::uintptr_t>(k.player), cam) && cam.ok &&
              cam.connect == 0x8800000000006408ull && cam.on == 1 && cam.mode == 3 && cam.prev == 1 && cam.vehicle_mode == 3,
          "the camera's connect fields and the VehicleCamera's index");
    Chain::Put<std::uintptr_t>(k.cam, 0, base + 0x10);
    Check(!pc::ReadCamera(reinterpret_cast<const void*>(base), reinterpret_cast<std::uintptr_t>(k.player), cam) && !cam.ok,
          "not a PlayerCamera: unread");
    Check(!pc::ReadCamera(reinterpret_cast<const void*>(base), 0x10, cam) && !cam.ok, "an unreadable P: unread");
}

// --- The pinned blocks and the install on a synthetic image -----------------------
struct Image {
    unsigned char* base = nullptr;
    std::size_t span = 0;
    Image() {
        span = c::pass_cam_module_vtable_rva + 0x100;
        span = (span + 0xFFFF) & ~static_cast<std::size_t>(0xFFFF);
        base = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(base != nullptr, "fixture image");
        std::memset(base, 0xCC, 0x1300000);
        for (const auto& s : c::code_sites)
            if (s.rva + s.size <= 0x1300000) std::memcpy(base + s.rva, s.bytes, s.size);
        Slot(c::pass_cam_module_vtable_rva, Addr(c::pass_cam_can_activate_rva));
        Slot(c::pass_cam_module_base_vtable_rva, Addr(c::pass_cam_can_activate_rva));
    }
    ~Image() { if (base) VirtualFree(base, 0, MEM_RELEASE); }
    std::uintptr_t Addr(std::uintptr_t rva) const { return reinterpret_cast<std::uintptr_t>(base) + rva; }
    void Slot(std::uintptr_t vtable, std::uintptr_t value) { std::memcpy(base + vtable + c::pass_cam_slot, &value, 8); }
    std::uintptr_t SlotValue(std::uintptr_t vtable) const { std::uintptr_t v = 0; std::memcpy(&v, base + vtable + c::pass_cam_slot, 8); return v; }
};
std::vector<std::string> g_lines;
void Log(const char* line) noexcept { g_lines.emplace_back(line); }
bool Logged(const char* part) {
    for (const auto& l : g_lines) if (l.find(part) != std::string::npos) return true;
    return false;
}

void TestVerifyAndInstall() {
    Check(sizeof(c::pass_camera_blocks) / sizeof(c::pass_camera_blocks[0]) == 9, "nine passenger camera blocks are pinned");
    Check(c::pass_cam_can_activate_rva == 0x12d0f60 && c::pass_cam_module_vtable_rva == 0x23d67f0 &&
              c::pass_cam_module_base_vtable_rva == 0x23d6720 && c::pass_cam_slot == 0x18,
          "the CanActivate and its two slots");
    bool pinned = false, live_slot = false, base_slot = false;
    for (const auto& s : c::vtable_slots) {
        if (s.vtable_rva == c::pass_cam_module_vtable_rva && s.offset == c::pass_cam_slot && s.target_rva == c::pass_cam_can_activate_rva) live_slot = true;
        if (s.vtable_rva == c::pass_cam_module_base_vtable_rva && s.offset == c::pass_cam_slot && s.target_rva == c::pass_cam_can_activate_rva)
            base_slot = true;
    }
    for (const auto& s : c::code_sites)
        if (s.rva == c::pass_cam_can_activate_rva && s.size == 39) pinned = true;
    Check(pinned && live_slot && base_slot, "the whole CanActivate body and both slots are in the contract");
    {
        Image img;
        std::uintptr_t mismatch = 0;
        Check(pc::detail::VerifyImage(img.base, img.span, mismatch) == nullptr && mismatch == 0, "the blocks verify");
        img.base[0x12D0F7E] ^= 1;  // the act test 0x6F
        Check(Same(pc::detail::VerifyImage(img.base, img.span, mismatch), "bytes") && mismatch == 0x12D0F60, "a changed CanActivate refuses");
        img.base[0x12D0F7E] ^= 1;
        img.base[0x124CD60] ^= 1;
        Check(Same(pc::detail::VerifyImage(img.base, img.span, mismatch), "bytes") && mismatch == 0x124CD38, "a changed FinishState clear refuses");
        img.base[0x124CD60] ^= 1;
        Check(Same(pc::detail::VerifyImage(img.base, 0x1000000, mismatch), "span"), "a short span refuses");
        Check(Same(pc::detail::VerifyImage(nullptr, img.span, mismatch), "base"), "no image");
    }
    {
        // Install: both slots point at the wrapper and stay live; a second call returns the first result.
        pc::detail::ResetForTest();
        g_lines.clear();
        Image img;
        Check(pc::Install(img.base, img.span, &FakeNow, &Log) == nullptr && pc::Installed() && pc::PatchLive(img.base),
              "installed and live on the synthetic image");
        const std::uintptr_t hook = img.SlotValue(c::pass_cam_module_vtable_rva);
        Check(hook != img.Addr(c::pass_cam_can_activate_rva) && img.SlotValue(c::pass_cam_module_base_vtable_rva) == hook,
              "both slots hold the wrapper");
        Check(Logged("partner_pass_camera=installed; slots=0x23d67f0+0x18,0x23d6720+0x18; target=0x12d0f60; blocks=9;"), "the install line");
        Check(pc::Install(img.base, img.span, &FakeNow, &Log) == nullptr, "a second install is a no-op");
        img.Slot(c::pass_cam_module_base_vtable_rva, img.Addr(c::pass_cam_can_activate_rva));
        Check(!pc::PatchLive(img.base), "a restored base slot is not live");
        img.Slot(c::pass_cam_module_base_vtable_rva, hook);
        Check(pc::PatchLive(img.base), "live again");
        pc::detail::ResetForTest();
    }
    {
        // A slot that does not hold the original: nothing swapped.
        g_lines.clear();
        Image img;
        img.Slot(c::pass_cam_module_base_vtable_rva, img.Addr(0x12D0F90));
        Check(Same(pc::Install(img.base, img.span, &FakeNow, &Log), "slot_base") && !pc::Installed() && !pc::PatchLive(img.base) &&
                  img.SlotValue(c::pass_cam_module_vtable_rva) == img.Addr(c::pass_cam_can_activate_rva),
              "a foreign base slot refuses before any swap");
        Check(Logged("partner_pass_camera=refused; reason=slot_base; slot=0x23d6738"), "the refusal line names the slot");
        Check(Same(pc::Install(img.base, img.span, &FakeNow, &Log), "slot_base"), "the refusal sticks for the process");
        pc::detail::ResetForTest();
        g_lines.clear();
        Image other;
        other.Slot(c::pass_cam_module_vtable_rva, 0);
        Check(Same(pc::Install(other.base, other.span, &FakeNow, &Log), "slot_live") &&
                  Logged("partner_pass_camera=refused; reason=slot_live; slot=0x23d6808"), "a foreign live slot refuses");
        pc::detail::ResetForTest();
    }
    {
        // A pinned block differs: nothing swapped.
        g_lines.clear();
        Image img;
        img.base[0x12D1FF0] ^= 1;
        Check(Same(pc::Install(img.base, img.span, &FakeNow, &Log), "bytes") &&
                  img.SlotValue(c::pass_cam_module_vtable_rva) == img.Addr(c::pass_cam_can_activate_rva) &&
                  Logged("partner_pass_camera=refused; reason=bytes; rva=0x12d1fca"),
              "a changed connect refuses before any swap");
        pc::detail::ResetForTest();
    }
}
} // namespace

int main() {
    TestPackAndLease();
    TestGrant();
    TestArmFor();
    TestEpisode();
    TestWrapper();
    TestReadCamera();
    TestVerifyAndInstall();
    std::printf("partner_pass_camera_tests: %u checks passed\n", checks);
    return 0;
}
