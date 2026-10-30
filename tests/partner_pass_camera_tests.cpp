// F1 passenger camera (docs/native-puppet.md "Passenger camera"): the driver's
// arm (only the reported back seat of a vehicle), the connect / release plans
// (never under the stock VehicleCamera, never over another owner, only our own
// handle cleared), the per-boarding episode (connect, refused, release, the
// driver's clear), Connect on a synthetic player / camera chain (the stock
// encoding, the PlayerCamera vtable check, SEH faults, last error kept), the
// camera read, and the pinned blocks plus the install on a synthetic image.
// Nothing here touches MGSV; the fixture buffers are never executed.
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
#include "mgscoop/lab_config.hpp"
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

void TestHandleAndOffsets() {
    Check(pc::ConnectHandle(25608) == 0x8800000000006408ull, "the stock handle encoding (the get-off's 0x8800000000006408)");
    Check(c::cam_connect == pc::kCamConnect && c::cam_connect_on == pc::kCamConnectOn && c::p_cam == pc::kPCam, "contract offsets");
    Check(c::pass_cam_mode == pc::kCamMode && c::pass_cam_modules == pc::kCamModules, "mode offsets");
}

void TestArmFor() {
    pc::ArmIn in{};
    in.passenger_mode = true; in.installed = true; in.passenger = true; in.vehicle = 25608; in.seat = 3;
    const char* why = nullptr;
    pc::Arm a = pc::ArmFor(in, why);
    Check(a.armed && a.vehicle == 25608 && a.seat == 3 && Same(why, "armed"), "armed for the reported back seat");
    pc::ArmIn x = in; x.passenger_mode = false;
    Check(!pc::ArmFor(x, why).armed && Same(why, "mode"), "block / off: never");
    x = in; x.passenger = false;
    Check(!pc::ArmFor(x, why).armed && Same(why, "not_passenger"), "no local passenger: never");
    x = in; x.installed = false;
    Check(!pc::ArmFor(x, why).armed && Same(why, "install"), "not installed");
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

// mode 5 = the fallback module, vmode 3 = the VehicleCamera's index.
pc::Camera Cam(std::uint64_t connect, std::uint32_t on, std::int32_t mode = 5, std::int32_t vmode = 3) {
    pc::Camera k{};
    k.ok = true; k.connect = connect; k.on = on; k.mode = mode; k.prev = 1; k.vehicle_mode = vmode;
    return k;
}

void TestPlans() {
    const char* why = nullptr;
    Check(pc::ConnectPlan(Cam(0, 0), 25608, why) == pc::Write::set && Same(why, "connect"), "a free field: connect");
    Check(pc::ConnectPlan(Cam(pc::ConnectHandle(25608), 1), 25608, why) == pc::Write::none && Same(why, "held"), "already ours: held");
    Check(pc::ConnectPlan(Cam(pc::ConnectHandle(25608), 0), 25608, why) == pc::Write::set && Same(why, "connect"),
          "our handle without the on flag: rewritten");
    Check(pc::ConnectPlan(Cam(pc::ConnectHandle(25601), 1), 25608, why) == pc::Write::none && Same(why, "other_owner"),
          "another owner: never overwritten");
    Check(pc::ConnectPlan(Cam(0, 0, 3, 3), 25608, why) == pc::Write::none && Same(why, "stock_module"),
          "the VehicleCamera active (driver, get-off): stock owns the field");
    Check(pc::ConnectPlan(Cam(0, 0, 5, -1), 25608, why) == pc::Write::set, "no VehicleCamera among the modules: still connect");
    Check(pc::ConnectPlan(pc::Camera{}, 25608, why) == pc::Write::none && Same(why, "camera"), "an unread camera: never");
    Check(pc::ReleasePlan(Cam(pc::ConnectHandle(25608), 1), 25608, why) == pc::Write::clear && Same(why, "ours"), "our handle: clear");
    Check(pc::ReleasePlan(Cam(0, 0), 25608, why) == pc::Write::none && Same(why, "not_ours"), "nothing there");
    Check(pc::ReleasePlan(Cam(pc::ConnectHandle(25601), 1), 25608, why) == pc::Write::none && Same(why, "not_ours"),
          "another owner: never cleared");
    Check(pc::ReleasePlan(Cam(pc::ConnectHandle(25608), 1, 3, 3), 25608, why) == pc::Write::none && Same(why, "stock_module"),
          "under the VehicleCamera: stock clears it");
    Check(pc::ReleasePlan(pc::Camera{}, 25608, why) == pc::Write::none && Same(why, "camera"), "an unread camera");
}

pc::StepIn In(bool passenger, std::uint64_t now, std::uint32_t writes, pc::Camera cam, bool armed = true, const char* plan = "connect") {
    pc::StepIn in{};
    in.passenger = passenger; in.vehicle = passenger ? 25608 : 0xFFFF; in.seat = passenger ? 2 : 0xFF;
    in.armed = armed; in.arm_reason = armed ? "armed" : "install"; in.plan = plan; in.cam = cam; in.writes = writes; in.now_ms = now;
    return in;
}

void TestEpisode() {
    {
        // A boarding: connect once the handle shows; once only; release at the stock clear.
        pc::Episode e{};
        Check(pc::Step(e, In(false, 100, 0, pc::Camera{})).event == pc::Event::none && !e.active, "on foot: nothing");
        pc::StepOut o = pc::Step(e, In(true, 1000, 5, Cam(0, 0)));
        Check(o.event == pc::Event::none && e.active && e.writes_at_start == 5, "boarded: waiting for the handle");
        o = pc::Step(e, In(true, 1016, 6, Cam(pc::ConnectHandle(25608), 1)));
        Check(o.event == pc::Event::connect && Same(o.reason, "handle") && o.ms == 16 && o.vehicle == 25608 && o.seat == 2,
              "connect: the vehicle's handle on the camera");
        Check(pc::Step(e, In(true, 1100, 6, Cam(pc::ConnectHandle(25608), 1), true, "held")).event == pc::Event::none, "connect logs once");
        Check(pc::Step(e, In(true, 9000, 6, Cam(pc::ConnectHandle(25608), 1), true, "held")).event == pc::Event::none,
              "no timeout after the connect");
        // The get-off (act 0x70): the report ends, the stock VehicleCamera holds the handle.
        o = pc::Step(e, In(false, 9100, 6, Cam(pc::ConnectHandle(25608), 1, 3, 3)));
        Check(o.event == pc::Event::none && !e.active && e.releasing, "get-off: releasing, the handle still held");
        o = pc::Step(e, In(false, 10100, 6, Cam(0, 0)));
        Check(o.event == pc::Event::release && Same(o.reason, "stock") && o.ok && !o.clear && o.ms == 1000 && o.vehicle == 25608,
              "release: the VehicleCamera's Deactivate / FinishState cleared it");
        Check(pc::Step(e, In(false, 10200, 6, Cam(0, 0))).event == pc::Event::none && !e.releasing, "released once");
    }
    {
        // Never connected within kConnectMs: refused with the plan's reason (another owner, the stock
        // module) or not_connected; once per boarding.
        pc::Episode e{};
        pc::Step(e, In(true, 1000, 0, Cam(pc::ConnectHandle(25601), 1), true, "other_owner"));
        Check(pc::Step(e, In(true, 3999, 0, Cam(pc::ConnectHandle(25601), 1), true, "other_owner")).event == pc::Event::none,
              "inside the connect window");
        pc::StepOut o = pc::Step(e, In(true, 4000, 0, Cam(pc::ConnectHandle(25601), 1), true, "other_owner"));
        Check(o.event == pc::Event::refused && Same(o.reason, "other_owner") && o.ms == 3000, "refused: another owner");
        Check(pc::Step(e, In(true, 9000, 3, Cam(pc::ConnectHandle(25608), 1))).event == pc::Event::none, "refused logs once per boarding");
        pc::Episode f{};
        pc::Step(f, In(true, 1000, 0, Cam(0, 0, 3, 3), true, "stock_module"));
        o = pc::Step(f, In(true, 4000, 0, Cam(0, 0, 3, 3), true, "stock_module"));
        Check(o.event == pc::Event::refused && Same(o.reason, "stock_module"), "refused: the VehicleCamera kept the field");
        pc::Episode g{};
        pc::Step(g, In(true, 1000, 0, Cam(0, 0)));
        o = pc::Step(g, In(true, 4000, 1, Cam(0, 0)));
        Check(o.event == pc::Event::refused && Same(o.reason, "not_connected"), "written, but the handle never showed");
        pc::Episode k{};
        pc::Step(k, In(true, 1000, 0, Cam(0, 0)));
        o = pc::Step(k, In(true, 4000, 2, pc::Camera{}, true, "camera"));
        Check(o.event == pc::Event::refused && Same(o.reason, "cam_read"), "camera unread");
    }
    {
        // Not armed at boarding: refused with the arm's reason, at once.
        pc::Episode e{};
        pc::StepOut o = pc::Step(e, In(true, 1000, 0, Cam(0, 0), false, "none"));
        Check(o.event == pc::Event::refused && Same(o.reason, "install") && o.ms == 0, "refused: the arm's reason");
    }
    {
        // Release by another owner, and the release timeout: our handle is cleared by the driver,
        // the stock module's is left alone.
        pc::Episode e{};
        pc::Step(e, In(true, 1000, 0, Cam(0, 0)));
        pc::Step(e, In(true, 1100, 1, Cam(pc::ConnectHandle(25608), 1)));
        pc::Step(e, In(false, 2000, 1, Cam(pc::ConnectHandle(25608), 1)));
        pc::StepOut o = pc::Step(e, In(false, 2100, 1, Cam(pc::ConnectHandle(25601), 1)));
        Check(o.event == pc::Event::release && Same(o.reason, "other") && o.ok && !o.clear, "another owner took the camera");
        pc::Episode f{};
        pc::Step(f, In(true, 1000, 0, Cam(0, 0)));
        pc::Step(f, In(true, 1100, 1, Cam(pc::ConnectHandle(25608), 1)));
        pc::Step(f, In(false, 2000, 1, Cam(pc::ConnectHandle(25608), 1)));
        Check(pc::Step(f, In(false, 9999, 1, Cam(pc::ConnectHandle(25608), 1))).event == pc::Event::none, "inside the release window");
        o = pc::Step(f, In(false, 10000, 1, Cam(pc::ConnectHandle(25608), 1)));
        Check(o.event == pc::Event::release && Same(o.reason, "cleared") && o.clear && !o.ok && o.ms == 8000,
              "release timeout: the driver clears our handle");
        pc::Episode g{};
        pc::Step(g, In(true, 1000, 0, Cam(0, 0)));
        pc::Step(g, In(true, 1100, 1, Cam(pc::ConnectHandle(25608), 1)));
        pc::Step(g, In(false, 2000, 1, Cam(pc::ConnectHandle(25608), 1, 3, 3)));
        o = pc::Step(g, In(false, 10000, 1, Cam(pc::ConnectHandle(25608), 1, 3, 3)));
        Check(o.event == pc::Event::release && Same(o.reason, "still_connected") && !o.clear && !o.ok,
              "release timeout under the VehicleCamera: left to stock");
    }
    {
        // A second boarding after the release is a new episode with its own counters.
        pc::Episode e{};
        pc::Step(e, In(true, 1000, 0, Cam(0, 0)));
        pc::Step(e, In(true, 1100, 1, Cam(pc::ConnectHandle(25608), 1)));
        pc::Step(e, In(false, 2000, 1, Cam(0, 0)));
        pc::StepOut o = pc::Step(e, In(true, 3000, 4, Cam(0, 0)));
        Check(o.event == pc::Event::none && e.active && e.writes_at_start == 4 && !e.connected, "a new boarding");
        o = pc::Step(e, In(true, 3100, 5, Cam(pc::ConnectHandle(25608), 1)));
        Check(o.event == pc::Event::connect, "connects again");
    }
}

// --- Connect on a synthetic chain ------------------------------------------------
struct Chain {
    unsigned char cam[0x1000]{};
    unsigned char player[0x400]{};
    unsigned char vehicle_module[0x40]{};
    unsigned char fallback_module[0x40]{};
    static constexpr std::uintptr_t kBase = 0x140000000;
    template <class T> static void Put(unsigned char* base, std::size_t at, T value) { std::memcpy(base + at, &value, sizeof(T)); }
    template <class T> T Get(std::size_t at) const { T v{}; std::memcpy(&v, cam + at, sizeof(T)); return v; }
    static std::uintptr_t A(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
    Chain() {
        Put<std::uintptr_t>(player, pc::kPCam, A(cam));
        Put<std::uintptr_t>(cam, 0, kBase + c::cam_vtable_rva);
        Put<std::uintptr_t>(vehicle_module, 0, kBase + c::pass_cam_module_vtable_rva);
        Put<std::uintptr_t>(fallback_module, 0, kBase + 0x23B29E0);
        Put<std::uintptr_t>(cam, pc::kCamModules + 24, A(vehicle_module));
        Put<std::uintptr_t>(cam, pc::kCamModules + 40, A(fallback_module));
        Mode(5);
    }
    void Mode(std::int32_t mode) { Put<std::int32_t>(cam, pc::kCamMode, mode); Put<std::int32_t>(cam, pc::kCamPrevMode, 1); }
    void Field(std::uint64_t connect, std::uint32_t on) {
        Put<std::uint64_t>(cam, pc::kCamConnect, connect);
        Put<std::uint32_t>(cam, pc::kCamConnectOn, on);
    }
    const void* Base() const { return reinterpret_cast<const void*>(kBase); }
    std::uintptr_t P() const { return A(player); }
};

void TestConnect() {
    pc::detail::ResetForTest();
    pc::Stats st{};
    {
        Chain k;
        pc::Camera cam{};
        SetLastError(1234);
        const char* why = pc::Connect(k.Base(), k.P(), 25608, true, cam);
        Check(Same(why, "connect") && GetLastError() == 1234, "connect: the plan's reason, last error kept");
        Check(k.Get<std::uint64_t>(pc::kCamConnect) == 0x8800000000006408ull && k.Get<std::uint32_t>(pc::kCamConnectOn) == 1,
              "the stock encoding at CAM+0x1F0 / +0x200");
        Check(cam.ok && cam.connect == 0x8800000000006408ull && cam.on == 1 && cam.mode == 5 && cam.vehicle_mode == 3,
              "the camera read back after the write");
        pc::ReadStats(st);
        Check(st.writes == 1 && st.clears == 0 && st.faults == 0, "one write counted");
        Check(Same(pc::Connect(k.Base(), k.P(), 25608, true, cam), "held"), "a second tick only reads");
        pc::ReadStats(st);
        Check(st.writes == 1, "held: no write");
        // The get-off: the VehicleCamera becomes the mode and owns the field.
        k.Mode(3);
        Check(Same(pc::Connect(k.Base(), k.P(), 25608, false, cam), "stock_module") &&
                  k.Get<std::uint64_t>(pc::kCamConnect) == 0x8800000000006408ull,
              "never cleared under the stock module");
        k.Mode(5);
        Check(Same(pc::Connect(k.Base(), k.P(), 25608, false, cam), "ours") && k.Get<std::uint64_t>(pc::kCamConnect) == 0 &&
                  k.Get<std::uint32_t>(pc::kCamConnectOn) == 0 && cam.connect == 0,
              "our own handle cleared");
        pc::ReadStats(st);
        Check(st.clears == 1, "one clear counted");
    }
    {
        // Another owner, the stock module, a foreign camera: nothing written.
        Chain k;
        pc::Camera cam{};
        k.Field(pc::ConnectHandle(25601), 1);
        Check(Same(pc::Connect(k.Base(), k.P(), 25608, true, cam), "other_owner") &&
                  k.Get<std::uint64_t>(pc::kCamConnect) == pc::ConnectHandle(25601),
              "another owner is never overwritten");
        Check(Same(pc::Connect(k.Base(), k.P(), 25608, false, cam), "not_ours") &&
                  k.Get<std::uint64_t>(pc::kCamConnect) == pc::ConnectHandle(25601),
              "another owner is never cleared");
        k.Field(0, 0);
        k.Mode(3);
        Check(Same(pc::Connect(k.Base(), k.P(), 25608, true, cam), "stock_module") && k.Get<std::uint64_t>(pc::kCamConnect) == 0,
              "the driver's / get-off's VehicleCamera: stock");
        k.Mode(5);
        Chain::Put<std::uintptr_t>(k.cam, 0, Chain::kBase + 0x10);
        Check(Same(pc::Connect(k.Base(), k.P(), 25608, true, cam), "camera") && !cam.ok && k.Get<std::uint64_t>(pc::kCamConnect) == 0,
              "not a PlayerCamera: nothing written");
        Check(Same(pc::Connect(k.Base(), 0x10, 25608, true, cam), "camera") && !cam.ok, "an unreadable P: nothing written");
    }
    {
        // A camera page that faults on write: counted, reported, nothing else touched.
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, si.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(page != nullptr, "fixture page");
        unsigned char player[0x400]{};
        Chain::Put<std::uintptr_t>(player, pc::kPCam, reinterpret_cast<std::uintptr_t>(page));
        Chain::Put<std::uintptr_t>(page, 0, Chain::kBase + c::cam_vtable_rva);
        Chain::Put<std::int32_t>(page, pc::kCamMode, 5);
        DWORD old = 0;
        Check(VirtualProtect(page, si.dwPageSize, PAGE_READONLY, &old) != 0, "read-only camera page");
        pc::Camera cam{};
        pc::ReadStats(st);
        const std::uint32_t faults = st.faults;
        Check(Same(pc::Connect(reinterpret_cast<const void*>(Chain::kBase), reinterpret_cast<std::uintptr_t>(player), 25608, true, cam), "fault"),
              "a faulting write reports fault");
        pc::ReadStats(st);
        Check(st.faults == faults + 1, "the fault is counted (the driver latches the mission)");
        VirtualFree(page, 0, MEM_RELEASE);
    }
    pc::detail::ResetForTest();
}

void TestReadCamera() {
    Chain k;
    k.Field(pc::ConnectHandle(25608), 1);
    Chain::Put<std::uint64_t>(k.cam, pc::kCamTarget, 0);
    k.Mode(3);
    pc::Camera cam{};
    Check(pc::ReadCamera(k.Base(), k.P(), cam) && cam.ok && cam.connect == 0x8800000000006408ull && cam.on == 1 && cam.mode == 3 &&
              cam.prev == 1 && cam.vehicle_mode == 3,
          "the camera's connect fields and the VehicleCamera's index");
    Chain::Put<std::uintptr_t>(k.cam, pc::kCamModules + 24, 0);
    Check(pc::ReadCamera(k.Base(), k.P(), cam) && cam.vehicle_mode == -1, "no VehicleCamera registered: -1");
    Chain::Put<std::uintptr_t>(k.cam, 0, Chain::kBase + 0x10);
    Check(!pc::ReadCamera(k.Base(), k.P(), cam) && !cam.ok, "not a PlayerCamera: unread");
    Check(!pc::ReadCamera(k.Base(), 0x10, cam) && !cam.ok, "an unreadable P: unread");
}

// --- Design 8.27: the aim camera ----------------------------------------------------
void TestModeAndAim() {
    pc::Mode m{};
    Check(pc::ParseMode("off", 3, m) && m == pc::Mode::off && pc::ParseMode("connect", 7, m) && m == pc::Mode::connect &&
              pc::ParseMode("aim", 3, m) && m == pc::Mode::aim && pc::ParseMode("drop", 4, m) && m == pc::Mode::drop,
          "the four modes parse");
    for (const char* bad : {"", "on", "Aim", "aim2", "connected", "lab", "dro", "drops"})
        Check(!pc::ParseMode(bad, std::strlen(bad), m), "anything else refuses");
    Check(Same(pc::ModeName(pc::Mode::off), "off") && Same(pc::ModeName(pc::Mode::connect), "connect") &&
              Same(pc::ModeName(pc::Mode::aim), "aim") && Same(pc::ModeName(pc::Mode::drop), "drop"),
          "mode names");
    Check(static_cast<int>(pc::Mode::off) == 0 && static_cast<int>(pc::Mode::connect) == 1 && static_cast<int>(pc::Mode::aim) == 2 &&
              static_cast<int>(pc::Mode::drop) == 3,
          "the values the lab key and the lever share");
    Check(pc::DropsWhileAiming(pc::Mode::drop) && !pc::DropsWhileAiming(pc::Mode::aim) && !pc::DropsWhileAiming(pc::Mode::connect) &&
              !pc::DropsWhileAiming(pc::Mode::off),
          "only drop drops the handle while aiming (runs 17fce0b7 / 43e5b981: seat 2's gun blocked without it)");
    Check(pc::Guards(pc::Mode::aim) && pc::Guards(pc::Mode::drop) && !pc::Guards(pc::Mode::connect) && !pc::Guards(pc::Mode::off),
          "aim and drop guard the shots; connect (0.14.4) and off do not");
    // ArmFor: mode off never arms (reported once a passenger is seated).
    pc::ArmIn in{};
    in.passenger_mode = true; in.installed = true; in.passenger = true; in.vehicle = 25608; in.seat = 2; in.off = true;
    const char* why = nullptr;
    Check(!pc::ArmFor(in, why).armed && Same(why, "off"), "partner_pass_camera=off: never connected");
    in.passenger = false;
    Check(!pc::ArmFor(in, why).armed && Same(why, "not_passenger"), "off says nothing until a passenger is seated");
    // GunAims: the aim flag or a mode of the aim / fire / reload family; nothing without the gun read.
    Check(pc::GunAims(true, 0, pc::kGunAimBit) && pc::GunAims(true, 7, 0) && pc::GunAims(true, 6, 0) && pc::GunAims(true, 9, 0) &&
              pc::GunAims(true, 10, 0) && pc::GunAims(true, 11, 0) && pc::GunAims(true, 12, 0),
          "the aim flag and the aim, fire and reload modes aim");
    Check(!pc::GunAims(true, 0, 0) && !pc::GunAims(true, 1, 0) && !pc::GunAims(true, 8, 0) && !pc::GunAims(true, 13, 0) &&
              !pc::GunAims(false, 7, pc::kGunAimBit),
          "idle, a weapon change, an unread gun: not aiming");
    Check(pc::kGunAimBit == 0x40000u, "rec+0x3C0 bit 18");
}

void TestAimStep() {
    pc::AimDrop s{};
    // drop: dropped from the first aiming tick until kAimHoldMs after the last one.
    pc::AimOut o = pc::AimStep(s, pc::Mode::drop, true, false, 1000);
    Check(o.want && o.event == pc::AimEvent::none && !s.dropped, "seated, not aiming: the handle stays");
    o = pc::AimStep(s, pc::Mode::drop, true, true, 1100);
    Check(!o.want && o.event == pc::AimEvent::drop && s.dropped && s.drops == 1, "the aim drops the handle at once");
    o = pc::AimStep(s, pc::Mode::drop, true, true, 1500);
    Check(!o.want && o.event == pc::AimEvent::none, "still aiming: still dropped, no second event");
    o = pc::AimStep(s, pc::Mode::drop, true, false, 1500 + pc::kAimHoldMs - 1);
    Check(!o.want && o.event == pc::AimEvent::none, "within the hold after the aim: still dropped");
    o = pc::AimStep(s, pc::Mode::drop, true, false, 1500 + pc::kAimHoldMs);
    Check(o.want && o.event == pc::AimEvent::restore && !s.dropped && s.restores == 1, "the hold over: restored");
    o = pc::AimStep(s, pc::Mode::drop, true, false, 9000);
    Check(o.want && o.event == pc::AimEvent::none, "idle again");
    // A shot inside the hold renews it.
    pc::AimStep(s, pc::Mode::drop, true, true, 10000);
    pc::AimStep(s, pc::Mode::drop, true, false, 10300);
    o = pc::AimStep(s, pc::Mode::drop, true, true, 10500);
    Check(!o.want && o.event == pc::AimEvent::none && s.drops == 2, "a new aim within the hold keeps the drop");
    o = pc::AimStep(s, pc::Mode::drop, true, false, 10500 + pc::kAimHoldMs - 1);
    Check(!o.want, "the hold counts from the last aiming tick");
    // Not armed (the get-off, a latch, mode off): the drop never outlives the boarding.
    o = pc::AimStep(s, pc::Mode::drop, false, true, 11000);
    Check(o.want && o.event == pc::AimEvent::none && !s.dropped && s.last_aim_ms == 0, "not armed: reset without an event");
    // connect (0.14.4) never drops.
    pc::AimDrop k{};
    for (std::uint64_t t = 1000; t < 3000; t += 100) {
        o = pc::AimStep(k, pc::Mode::connect, true, true, t);
        Check(o.want && o.event == pc::AimEvent::none && !k.dropped, "connect keeps the handle while aiming");
    }
    // A clock of 0 still counts as an aim.
    pc::AimDrop z{};
    o = pc::AimStep(z, pc::Mode::drop, true, true, 0);
    Check(!o.want && z.last_aim_ms == 1, "an aim at clock 0");
    // aim (the default) keeps the handle while aiming, like connect.
    pc::AimDrop ka{};
    o = pc::AimStep(ka, pc::Mode::aim, true, true, 2000);
    Check(o.want && o.event == pc::AimEvent::none && !ka.dropped, "aim: the handle stays while aiming");
}

// The camera rotation for a world yaw and up-pitch (no roll): qy(yaw) * qx(-up), the convention CAM+0x90 follows
// (4f1a072f: forward = q (0, 0, 1) q*, yaw = atan2(fx, fz), up = atan2(fy, |fxz|)).
struct Rot {
    float q[4];
};
Rot Look(float yaw, float up) {
    const float sy = std::sin(0.5f * yaw), cy = std::cos(0.5f * yaw), sx = std::sin(-0.5f * up), cx = std::cos(-0.5f * up);
    return Rot{{cy * sx, sy * cx, -sy * sx, cy * cx}};
}
bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
pc::GuardIn Tick(bool aiming, bool firing, float pitch, float yaw, const Rot* r, std::uint64_t now, bool module = true, bool look_x = false,
                 bool look_y = false) {
    pc::GuardIn in{};
    in.on = true;
    in.aiming = aiming;
    in.firing = firing;
    in.aim_module = module;
    in.ang_ok = true;
    in.look_x = look_x;
    in.look_y = look_y;
    in.pitch = pitch;
    in.yaw = yaw;
    in.rot = r ? r->q : nullptr;
    in.now_ms = now;
    return in;
}

// The rotation math against run 4f1a072f's trace (CAM+0x90 next to the aim target ray and the body yaw).
void TestGuardMath() {
    const float seated[4] = {0.003f, -0.854f, 0.004f, 0.519f};    // seat 3 seated, the body yaw -117.36 deg
    const float aimed[4] = {-0.018f, 0.851f, -0.040f, -0.521f};   // seat 3 aiming, the target ray -117.0 deg, 5.0 deg down
    Check(Near(pc::WorldYaw(seated), -2.0484f, 0.01f) && Near(pc::UpPitch(seated), 0.0f, 0.02f), "4f1a072f seated: yaw -117.4 deg, level");
    Check(Near(pc::WorldYaw(aimed), -2.0423f, 0.01f) && Near(pc::UpPitch(aimed), -0.0873f, 0.01f), "4f1a072f aiming: yaw -117.0 deg, 5 deg down");
    const Rot l = Look(1.2f, 0.3f);
    Check(pc::QuatOk(l.q) && Near(pc::WorldYaw(l.q), 1.2f) && Near(pc::UpPitch(l.q), 0.3f), "Look(): yaw and up-pitch round trip");
    float out[4]{};
    pc::PitchDown(l.q, 0.25f, out);
    Check(pc::QuatOk(out) && Near(pc::UpPitch(out), 0.05f) && Near(pc::WorldYaw(out), 1.2f), "PitchDown: the pitch drops, the yaw stays");
    pc::PitchDown(l.q, -0.2f, out);
    Check(Near(pc::UpPitch(out), 0.5f) && Near(pc::WorldYaw(out), 1.2f), "a negative turn raises it");
    pc::PitchDown(aimed, 0.1f, out);
    Check(Near(pc::UpPitch(out), -0.1873f, 0.01f) && Near(pc::WorldYaw(out), pc::WorldYaw(aimed), 0.01f), "the measured rotation with its small roll");
    pc::YawTurn(l.q, 0.5f, out);
    Check(pc::QuatOk(out) && Near(pc::WorldYaw(out), 1.7f) && Near(pc::UpPitch(out), 0.3f), "YawTurn: the world yaw turns, the pitch stays");
    const Rot w = Look(3.0f, -0.2f);
    pc::YawTurn(w.q, 0.4f, out);
    Check(Near(pc::WorldYaw(out), pc::WrapPi(3.4f)) && Near(pc::UpPitch(out), -0.2f), "YawTurn across the wrap");
    const float zero[4] = {0, 0, 0, 0}, nanq[4] = {std::nanf(""), 0, 0, 1};
    Check(!pc::QuatOk(zero) && !pc::QuatOk(nanq) && !pc::QuatOk(nullptr), "a zero, NaN or missing rotation is not one");
}

// The shot guard (run 4f1a072f: a seated shot pulls the camera from the aimed pitch to 70 deg up within 0.5 s; run
// a64c40d4: with the pitch held the pull turns the view sideways, 0.93 rad in a four-shot burst). Review fixes 8.28:
// the player's own look input passes (everything follows it), the pull up and sideways without input is held.
void TestGuard() {
    pc::Guard g{};
    Rot r = Look(1.0f, -0.05f);
    // Aiming, not firing: the references follow the player's own aim; nothing is written.
    pc::GuardOut o = pc::GuardStep(g, Tick(true, false, 0.05f, 1.57f, &r, 1000));
    Check(!o.write && !o.write_rot && o.event == pc::GuardEvent::none && g.ref_ok && g.ref_pitch == 0.05f && g.up_ok &&
              Near(g.ref_up, -0.05f) && Near(g.ref_yaw, 1.0f),
          "aimed: the references (angle pitch, world up-pitch and yaw)");
    r = Look(1.1f, -0.10f);
    o = pc::GuardStep(g, Tick(true, false, 0.10f, 1.67f, &r, 1050));
    Check(!o.write && g.ref_pitch == 0.10f && Near(g.ref_up, -0.10f) && Near(g.ref_yaw, 1.1f), "the player's own aim moves the references");
    // The shot: a window opens; a small kick stays.
    r = Look(1.12f, -0.08f);
    o = pc::GuardStep(g, Tick(true, true, 0.08f, 1.67f, &r, 1100));
    Check(!o.write && !o.write_rot && o.event == pc::GuardEvent::open && g.active && g.windows == 1, "the shot opens a window; a 1 deg kick stays");
    // The pull up and sideways (no input): the angle pitch and CAM+0x90 written back to the references.
    r = Look(1.16f, 0.25f);
    o = pc::GuardStep(g, Tick(true, true, -0.25f, 1.69f, &r, 1150));
    Check(o.write && o.pitch == 0.10f && o.write_rot && Near(pc::UpPitch(o.rot), -0.10f) && Near(pc::WorldYaw(o.rot), 1.1f),
          "the pull: the angle pitch and the rotation back at the references");
    Check(g.window_writes == 1 && g.rot_writes == 1 && Near(g.window_max, 0.35f), "counted, the pull measured");
    // The pull sideways alone (a64c40d4: at most ~0.03 rad per tick): held.
    r = Look(1.14f, -0.10f);
    o = pc::GuardStep(g, Tick(true, true, 0.10f, 1.70f, &r, 1200));
    Check(!o.write && o.write_rot && Near(pc::WorldYaw(o.rot), 1.1f) && Near(pc::UpPitch(o.rot), -0.10f), "the pull sideways: held");
    // F1: the player's own turn (look input x) passes inside the window, and the yaw reference follows it.
    r = Look(1.6f, -0.10f);
    o = pc::GuardStep(g, Tick(true, true, 0.10f, 2.17f, &r, 1250, true, true, false));
    Check(!o.write && !o.write_rot && Near(g.ref_yaw, 1.6f) && g.look_ticks == 1, "a mouse turn of 0.5 rad inside the window: nothing written");
    // The next tick, without input: the pull takes the turn back and lifts the view (run d60dbb48: with a 60 ms latch
    // the pull took back each mouse step of a burst; 049aa5ed: following both axes let it lift the view 20-23 deg):
    // the turn is kept, the pull held.
    r = Look(1.56f, 0.15f);
    o = pc::GuardStep(g, Tick(true, true, -0.15f, 2.19f, &r, 1268));
    Check(o.write && o.pitch == 0.10f && o.write_rot && Near(pc::UpPitch(o.rot), -0.10f) && Near(pc::WorldYaw(o.rot), 1.6f) &&
              Near(g.ref_yaw, 1.6f) && g.look_ticks == 1,
          "the tick after a mouse turn: the turn kept, the pull back and up held");
    // A yaw jump without input bigger than any pull (a mouse frame the tick did not sample): the player's.
    r = Look(1.9f, -0.10f);
    o = pc::GuardStep(g, Tick(true, true, 0.10f, 2.19f, &r, 1286));
    Check(!o.write && !o.write_rot && Near(g.ref_yaw, 1.9f) && g.look_ticks == 2, "a 0.3 rad yaw jump in one tick: followed");
    // After it: the pull held at the player's new aim.
    r = Look(1.9f, 0.20f);
    o = pc::GuardStep(g, Tick(true, true, -0.20f, 2.19f, &r, 1304));
    Check(o.write && o.pitch == 0.10f && o.write_rot && Near(pc::UpPitch(o.rot), -0.10f) && Near(pc::WorldYaw(o.rot), 1.9f),
          "after the input: the pull held at the player's new aim");
    // F1: the player's aim UP with input y passes too; a sideways pull on that tick is still held.
    r = Look(1.94f, 0.10f);
    o = pc::GuardStep(g, Tick(true, true, -0.10f, 2.19f, &r, 1500, true, false, true));
    Check(!o.write && o.write_rot && Near(pc::WorldYaw(o.rot), 1.9f) && Near(pc::UpPitch(o.rot), 0.10f) && g.ref_pitch == -0.10f &&
              Near(g.ref_up, 0.10f),
          "aim up with input y: passes, the pitch references follow; the yaw is still held");
    // F2: the jeep turns without input: the stock aim camera keeps its world yaw (a64c40d4: CAM+0x90's yaw constant
    // while the body yaw turned 58 deg), the vehicle-relative angle yaw changes: nothing written.
    r = Look(1.9f, 0.10f);
    for (std::uint64_t t = 1700; t <= 1850; t += 50) {
        const float rel = 2.19f - 0.002f * static_cast<float>(t - 1700);
        o = pc::GuardStep(g, Tick(true, true, -0.10f, rel, &r, t));
        Check(!o.write && !o.write_rot, "a turning jeep: the world-stable aim camera needs no write");
    }
    // The hold's close settles a remaining climb and yaw under the kick bounds.
    r = Look(1.92f, 0.13f);
    o = pc::GuardStep(g, Tick(true, false, -0.13f, 1.89f, &r, 1850 + pc::kGuardHoldMs - 1));
    Check(!o.write && !o.write_rot && g.active, "within kGuardHoldMs: 0.02 rad aside and 0.03 rad up stay");
    o = pc::GuardStep(g, Tick(true, false, -0.13f, 1.89f, &r, 1850 + pc::kGuardHoldMs));
    Check(o.event == pc::GuardEvent::close && o.end == pc::GuardEnd::hold && o.settle && o.write && o.pitch == -0.10f && o.write_rot &&
              Near(pc::UpPitch(o.rot), 0.10f) && Near(pc::WorldYaw(o.rot), 1.9f) && !g.active && g.settles == 1,
          "the hold's close settles the remaining climb and yaw");
    Check(g.ref_pitch == -0.10f && Near(g.ref_up, 0.10f) && Near(g.ref_yaw, 1.9f), "the settled references");
    // The next aimed tick: the references follow the player again.
    r = Look(1.7f, 0.05f);
    o = pc::GuardStep(g, Tick(true, false, -0.05f, 1.9f, &r, 2400));
    Check(!o.write && g.ref_pitch == -0.05f && Near(g.ref_up, 0.05f) && Near(g.ref_yaw, 1.7f) && o.event == pc::GuardEvent::none,
          "after the window: follows again");
    Check(pc::GunFires(true, 9) && pc::GunFires(true, 10) && !pc::GunFires(true, 7) && !pc::GunFires(false, 9), "firing is gun mode 9 / 10");
    Check(pc::kKickUp <= 0.06f && pc::kKickYaw <= 0.04f && pc::kSettleUp < pc::kKickUp && pc::kSettleYaw < pc::kKickYaw &&
              pc::kGuardHoldMs >= 200 && pc::kGuardHoldMs <= 600 && pc::kYawJump > pc::kKickYaw && pc::kYawJump <= 0.06f,
          "at most about 3 deg of kick, a hold shorter than a shot cycle, a yaw jump above the kick");
}

// A burst without input (a64c40d4 host: the yaw pulled 0.93 rad in 1.2 s): every tick's pull sideways and up is held.
void TestGuardBurst() {
    pc::Guard g{};
    Rot r = Look(-0.2f, -0.09f);
    pc::GuardStep(g, Tick(true, false, 0.09f, 1.2f, &r, 1000));
    // The game: each tick the pull adds 0.03 rad sideways and 0.04 rad up to whatever the camera holds (the angle pitch
    // follows the rotation); the guard's writes replace it.
    float yaw = -0.2f, up = -0.09f, pitch = 0.09f, worst_yaw = 0.0f, worst_up = 0.0f;
    unsigned writes = 0;
    for (int tick = 1; tick <= 70; ++tick) {
        yaw -= 0.03f;
        up += 0.04f;
        pitch -= 0.04f;
        worst_yaw = std::fabs(yaw + 0.2f) > worst_yaw ? std::fabs(yaw + 0.2f) : worst_yaw;
        worst_up = up + 0.09f > worst_up ? up + 0.09f : worst_up;
        const Rot pulled = Look(yaw, up);
        const pc::GuardOut o = pc::GuardStep(g, Tick(true, true, pitch, 1.2f, &pulled, 1000 + 18u * static_cast<unsigned>(tick)));
        if (o.write_rot) {
            yaw = pc::WorldYaw(o.rot);
            up = pc::UpPitch(o.rot);
            ++writes;
        }
        if (o.write) pitch = o.pitch;
    }
    Check(g.windows == 1 && g.active && writes >= 30, "one window for the burst, written about every second tick");
    Check(worst_yaw < 0.07f && worst_up < 0.09f, "the burst's pull never got two ticks past the aim (a64c40d4 without it: 0.93 rad)");
}

// The rules for a pitch going down without input (the player's mouse is the input path; these are the vehicle's).
void TestGuardDownRules() {
    pc::Guard g{};
    Rot r = Look(1.9f, -0.10f);
    pc::GuardStep(g, Tick(true, false, 0.10f, 2.17f, &r, 1000));
    pc::GuardStep(g, Tick(true, true, 0.10f, 2.17f, &r, 1050));
    // The angle down with the world down (the player's aim, seen without input): both references follow.
    r = Look(1.9f, -0.30f);
    pc::GuardOut o = pc::GuardStep(g, Tick(true, true, 0.30f, 2.17f, &r, 1100));
    Check(!o.write && !o.write_rot && g.ref_pitch == 0.30f && Near(g.ref_up, -0.30f), "aim down: passes, the references follow");
    r = Look(1.9f, -0.10f);
    o = pc::GuardStep(g, Tick(true, true, 0.10f, 2.17f, &r, 1150));
    Check(o.write && o.pitch == 0.30f && o.write_rot && Near(pc::UpPitch(o.rot), -0.30f), "the pull up from the lower aim is held there");
    // A bump of the vehicle alone (the world pitch down, the angle unchanged) moves no reference: no ratchet.
    r = Look(1.9f, -0.40f);
    o = pc::GuardStep(g, Tick(true, true, 0.30f, 2.17f, &r, 1200));
    Check(!o.write && !o.write_rot && g.ref_pitch == 0.30f && Near(g.ref_up, -0.30f), "a bump down: the references stay");
    r = Look(1.9f, -0.30f);
    o = pc::GuardStep(g, Tick(true, true, 0.30f, 2.17f, &r, 1250));
    Check(!o.write && !o.write_rot, "back from the bump: nothing written");
    // The vehicle noses up (the world pitch up, the angle unchanged): CAM+0x90 turned down once; the angle then reads
    // the drop (followed), the world stays at its reference: no runaway.
    r = Look(1.9f, -0.20f);
    o = pc::GuardStep(g, Tick(true, true, 0.30f, 2.17f, &r, 1300));
    Check(o.write_rot && !o.write && Near(pc::UpPitch(o.rot), -0.30f), "nose up: the world pitch held");
    const std::uint32_t rot_before = g.rot_writes;
    for (std::uint64_t t = 1350; t <= 1500; t += 50) {
        r = Look(1.9f, -0.30f);
        o = pc::GuardStep(g, Tick(true, true, 0.40f, 2.17f, &r, t));
        Check(!o.write && !o.write_rot, "nose up, the next ticks: nothing more");
    }
    Check(g.rot_writes == rot_before && g.ref_pitch == 0.40f && Near(g.ref_up, -0.30f), "no runaway: the world reference stayed");
}

// F4: shot after shot, each window ending a little above the aim: no climb.
void TestGuardNoClimb() {
    pc::Guard g{};
    Rot r = Look(0.5f, -0.05f);
    pc::GuardStep(g, Tick(true, false, 0.05f, 1.57f, &r, 1000));
    std::uint64_t t = 1000;
    for (int shot = 0; shot < 10; ++shot) {
        t += 100;
        Rot up = Look(0.5f, 0.20f);
        pc::GuardOut o = pc::GuardStep(g, Tick(true, true, -0.20f, 1.57f, &up, t));
        Check(o.write && o.pitch == 0.05f && o.write_rot && Near(pc::UpPitch(o.rot), -0.05f), "each shot's pull held at the first aim");
        // The window ends 0.04 rad above the aim (under kKickUp, over kSettleUp).
        Rot left = Look(0.5f, -0.01f);
        o = pc::GuardStep(g, Tick(true, false, 0.01f, 1.57f, &left, t + 50));
        Check(!o.write && !o.write_rot, "0.04 rad inside the hold stays");
        t += 50 + pc::kGuardHoldMs;
        o = pc::GuardStep(g, Tick(true, false, 0.01f, 1.57f, &left, t));
        Check(o.event == pc::GuardEvent::close && o.settle && o.pitch == 0.05f && Near(pc::UpPitch(o.rot), -0.05f), "settled at the close");
        // The game now shows the settled values.
        Rot settled = Look(0.5f, -0.05f);
        t += 100;
        o = pc::GuardStep(g, Tick(true, false, 0.05f, 1.57f, &settled, t));
    }
    Check(g.windows == 10 && g.settles == 10 && g.ref_pitch == 0.05f && Near(g.ref_up, -0.05f), "ten shots: the reference never climbed");
}

// F3: nothing outside the aim camera; an aim end closes the window at once, without a write.
void TestGuardAimGate() {
    pc::Guard g{};
    Rot r = Look(0.0f, 0.0f), up = Look(0.0f, 0.5f);
    pc::GuardOut o = pc::GuardStep(g, Tick(true, true, -0.5f, 0.0f, &up, 1000, false));
    Check(!o.write && !o.write_rot && o.event == pc::GuardEvent::none && !g.active && g.windows == 0, "firing outside the aim module: nothing");
    o = pc::GuardStep(g, Tick(false, true, -0.5f, 0.0f, &up, 1050));
    Check(!o.write && !o.write_rot && !g.active, "firing but not aiming: nothing");
    pc::GuardStep(g, Tick(true, false, 0.0f, 0.0f, &r, 1100));
    o = pc::GuardStep(g, Tick(true, true, 0.0f, 0.0f, &r, 1150));
    Check(o.event == pc::GuardEvent::open && g.active, "a window opens in the aim module");
    // The aim ends (RMB released, the gun out of the fire modes) with the view pulled: closed now, nothing written.
    o = pc::GuardStep(g, Tick(false, false, -0.5f, 0.0f, &up, 1200));
    Check(o.event == pc::GuardEvent::close && o.end == pc::GuardEnd::aim_end && !o.write && !o.write_rot && !o.settle && !g.active &&
              !g.ref_ok && g.last_fire_ms == 0,
          "the aim end closes the window without a write");
    // The module leaves the aim camera inside a window (the seated fallback, the stock VehicleCamera): the same.
    pc::GuardStep(g, Tick(true, false, 0.0f, 0.0f, &r, 1300));
    pc::GuardStep(g, Tick(true, true, 0.0f, 0.0f, &r, 1350));
    o = pc::GuardStep(g, Tick(true, true, -0.5f, 0.0f, &up, 1400, false));
    Check(o.event == pc::GuardEvent::close && o.end == pc::GuardEnd::aim_end && !o.write && !o.write_rot, "the module left the aim camera: closed");
    o = pc::GuardStep(g, Tick(true, true, -0.5f, 0.0f, &up, 1450, false));
    Check(o.event == pc::GuardEvent::none && !o.write && !o.write_rot, "and nothing after it");
    Check(Same(pc::GuardEndName(pc::GuardEnd::hold), "hold") && Same(pc::GuardEndName(pc::GuardEnd::aim_end), "aim_end"), "end names");
}

// The rest: the first-tick reference, an unread rotation, off / unread / NaN.
void TestGuardEdges() {
    pc::Guard f{};
    Rot r = Look(-2.0f, 0.1f);
    pc::GuardOut o = pc::GuardStep(f, Tick(true, true, -0.1f, -2.0f, &r, 5000));
    Check(!o.write && !o.write_rot && o.event == pc::GuardEvent::open && f.ref_pitch == -0.1f && Near(f.ref_up, 0.1f) && Near(f.ref_yaw, -2.0f),
          "a shot with no aimed tick: its first tick is the reference");
    pc::Guard n{};
    pc::GuardStep(n, Tick(true, false, 0.0f, 0.0f, nullptr, 1000));
    o = pc::GuardStep(n, Tick(true, true, -1.0f, 0.0f, nullptr, 1100));
    Check(o.write && o.pitch == 0.0f && !o.write_rot && !n.up_ok, "without the rotation: the angle pitch only");
    o = pc::GuardStep(n, Tick(true, true, -1.0f, 0.0f, &r, 1150));
    Check(o.write && !o.write_rot, "a rotation read only later in the window: no world reference for it");
    pc::GuardIn off = Tick(true, true, 1.0f, 1.0f, &r, 6200);
    off.on = false;
    o = pc::GuardStep(n, off);
    Check(!o.write && !o.write_rot && !n.active && !n.ref_ok && n.last_fire_ms == 0 && o.event == pc::GuardEvent::none, "off: reset, no line");
    pc::GuardIn unread = Tick(true, true, 1.0f, 1.0f, &r, 6300);
    unread.ang_ok = false;
    o = pc::GuardStep(n, unread);
    Check(!o.write && !n.ref_ok, "unread angles: nothing");
    o = pc::GuardStep(n, Tick(true, true, std::nanf(""), 1.0f, &r, 6400));
    Check(!o.write && !n.ref_ok, "a NaN angle: nothing");
    Check(pc::kCamRotation == 0x90 && c::pass_cam_rotation == pc::kCamRotation && pc::kAimModuleVtableRva == 0x23D4D20 &&
              c::pass_cam_aim_module_vtable_rva == pc::kAimModuleVtableRva,
          "the rotation offset and the aim module's vtable match the contract");
}

void TestTraceDue() {
    Check(pc::TraceDue(0, 0, false, 5000), "the first line is due");
    Check(!pc::TraceDue(5000, 0, false, 5000 + pc::kTraceIdleMs - 1) && pc::TraceDue(5000, 0, false, 5000 + pc::kTraceIdleMs),
          "idle: every kTraceIdleMs");
    Check(!pc::TraceDue(5000, 0, true, 5000 + pc::kTraceAimMs - 1) && pc::TraceDue(5000, 0, true, 5000 + pc::kTraceAimMs),
          "aiming: every kTraceAimMs");
    Check(pc::TraceDue(5000, 4900, false, 5100) && !pc::TraceDue(5000, 4900 - pc::kTraceAfterMs, false, 5100),
          "fast for kTraceAfterMs after the aim");
    Check(pc::TraceDue(9000, 0, false, 5000), "a clock that went back: due");
}

void TestReadViewAndLever() {
    Chain k;
    const float pos[3] = {-650.5f, 3.25f, 880.75f}, rot[4] = {0.1f, 0.2f, 0.3f, 0.9f}, ang[4] = {0.25f, -1.5f, 0.3f, -1.25f};
    for (unsigned i = 0; i < 3; ++i) Chain::Put<float>(k.cam, pc::kCamPos + 4u * i, pos[i]);
    for (unsigned i = 0; i < 4; ++i) {
        Chain::Put<float>(k.cam, pc::kCamRot + 4u * i, rot[i]);
        Chain::Put<float>(k.cam, pc::kCamAngles + 4u * i, ang[i]);
    }
    Chain::Put<std::uint32_t>(k.cam, pc::kCamFlags, 0x400001u);
    Chain::Put<std::uint32_t>(k.cam, pc::kCamRequests, 4u);
    Chain::Put<float>(k.cam, pc::kCamOffset + 4, 0.6f);
    Chain::Put<float>(k.cam, pc::kCamPivot + 8, -1.2f);
    Chain::Put<float>(k.cam, pc::kCamScale + 4, 0.75f);
    pc::View v{};
    Check(pc::ReadView(k.Base(), k.P(), v) && v.ok && v.pos[0] == pos[0] && v.pos[2] == pos[2] && v.rot[3] == rot[3] &&
              v.ang[1] == ang[1] && v.ang[3] == ang[3] && v.flags == 0x400001u && v.requests == 4u && v.off[1] == 0.6f &&
              v.piv[2] == -1.2f && v.sc[1] == 0.75f,
          "the trace reads the position, rotation, look angles, the solve's offsets and the flag words");
    Check(pc::kCamOffset == 0x140 && pc::kCamPivot == 0x180 && pc::kCamScale == 0x254, "the solve's input offsets");
    Check(pc::kCamPos == 0xE0 && pc::kCamRot == 0x90 && pc::kCamAngles == 0x268 && pc::kCamFlags == 0x204 && pc::kCamRequests == 0xD38,
          "the trace offsets");
    Chain::Put<std::uintptr_t>(k.cam, 0, Chain::kBase + 0x10);
    Check(!pc::ReadView(k.Base(), k.P(), v) && !v.ok, "not a PlayerCamera: unread");
    Check(!pc::ReadView(k.Base(), 0x10, v) && !v.ok, "an unreadable P: unread");
    // The lever: lab only, three modes, lab clears, status reports; any other text is not a passcam command.
    pc::detail::ResetForTest();
    char r[160]{};
    bool ok = true;
    Check(!pc::HandleCommand("aimpt off", true, r, sizeof(r), ok) && !ok, "another command is not ours");
    Check(!pc::HandleCommand("passcamx aim", true, r, sizeof(r), ok), "a longer word is not ours");
    Check(pc::Override() == -1, "no override at first");
    Check(pc::HandleCommand("passcam aim", false, r, sizeof(r), ok) && !ok && std::strstr(r, "reason=lab_only") && pc::Override() == -1,
          "refused outside the lab, nothing changes");
    Check(pc::HandleCommand("passcam off", true, r, sizeof(r), ok) && ok && pc::Override() == 0 && std::strstr(r, "passcam=off; override=set"),
          "off");
    Check(pc::HandleCommand("passcam connect", true, r, sizeof(r), ok) && ok && pc::Override() == 1, "connect");
    Check(pc::HandleCommand("passcam aim", true, r, sizeof(r), ok) && ok && pc::Override() == 2, "aim");
    Check(pc::HandleCommand("passcam status", true, r, sizeof(r), ok) && ok && pc::Override() == 2 && std::strstr(r, "passcam=aim"),
          "status keeps the override");
    Check(pc::HandleCommand("passcam bogus", true, r, sizeof(r), ok) && !ok && std::strstr(r, "reason=args") && pc::Override() == 2,
          "bad arguments refuse");
    Check(pc::HandleCommand("passcam", true, r, sizeof(r), ok) && !ok, "no argument refuses");
    Check(pc::HandleCommand("passcam drop", true, r, sizeof(r), ok) && ok && pc::Override() == 3 && std::strstr(r, "passcam=drop"), "drop");
    Check(pc::HandleCommand("passcam lab", true, r, sizeof(r), ok) && ok && pc::Override() == -1 && std::strstr(r, "passcam=lab; override=none"),
          "lab clears the override");
    pc::detail::ResetForTest();
}

void TestWritePitchAndModeVtable() {
    pc::detail::ResetForTest();
    Chain k;
    Chain::Put<float>(k.cam, pc::kCamYaw, 1.6f);
    Chain::Put<float>(k.cam, pc::kCamYawPrev, 1.5f);
    Check(pc::WritePitch(k.Base(), k.P(), -0.05f) && k.Get<float>(pc::kCamPitch) == -0.05f && k.Get<float>(pc::kCamPitchPrev) == -0.05f &&
              k.Get<float>(pc::kCamYaw) == 1.6f && k.Get<float>(pc::kCamYawPrev) == 1.5f,
          "the guard writes the pitch to +0x270 / +0x268 and never the yaw (+0x274 / +0x26C, design 8.28)");
    Check(pc::kCamPitch == 0x270 && pc::kCamYaw == 0x274 && pc::kCamPitchPrev == 0x268 && pc::kCamYawPrev == 0x26C &&
              c::pass_cam_pitch == pc::kCamPitch && c::pass_cam_yaw == pc::kCamYaw && c::pass_cam_eye == pc::kCamPos,
          "the look-angle offsets match the contract");
    pc::Stats st{};
    pc::ReadStats(st);
    Check(st.angles == 1 && st.faults == 0 && st.writes == 0, "one angle write counted (not a handle write)");
    const float q[4] = {0.1f, 0.7f, 0.1f, -0.7f};
    Check(pc::WriteRotation(k.Base(), k.P(), q) && k.Get<float>(pc::kCamRotation) == 0.1f && k.Get<float>(pc::kCamRotation + 12) == -0.7f,
          "the rotation hold writes CAM+0x90..0x9C");
    pc::ReadStats(st);
    Check(st.angles == 2, "the rotation write counted with the angle writes");
    // The current mode's module vtable: the fallback module at index 5.
    Check(pc::ModeVtable(k.Base(), k.P()) == 0x23B29E0, "the trace's mvt: the fallback module");
    k.Mode(3);
    Check(pc::ModeVtable(k.Base(), k.P()) == c::pass_cam_module_vtable_rva, "the VehicleCamera");
    k.Mode(6);
    Check(pc::ModeVtable(k.Base(), k.P()) == 0, "no module: 0");
    // Not a PlayerCamera: nothing written.
    Chain::Put<float>(k.cam, pc::kCamPitch, 0.5f);
    Chain::Put<std::uintptr_t>(k.cam, 0, Chain::kBase + 0x10);
    Check(!pc::WritePitch(k.Base(), k.P(), 0.0f) && k.Get<float>(pc::kCamPitch) == 0.5f, "not a PlayerCamera: nothing written");
    {
        // A camera page that faults on write: counted as a fault (the driver latches the mission).
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, si.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(page != nullptr, "fixture page");
        unsigned char player[0x400]{};
        Chain::Put<std::uintptr_t>(player, pc::kPCam, reinterpret_cast<std::uintptr_t>(page));
        Chain::Put<std::uintptr_t>(page, 0, Chain::kBase + c::cam_vtable_rva);
        DWORD old = 0;
        Check(VirtualProtect(page, si.dwPageSize, PAGE_READONLY, &old) != 0, "read-only camera page");
        Check(!pc::WritePitch(reinterpret_cast<const void*>(Chain::kBase), reinterpret_cast<std::uintptr_t>(player), 0.0f),
              "a faulting angle write reports false");
        pc::ReadStats(st);
        Check(st.faults == 1 && st.angles == 2, "the fault is counted");
        Check(!pc::WriteRotation(reinterpret_cast<const void*>(Chain::kBase), reinterpret_cast<std::uintptr_t>(player), q),
              "a faulting rotation write reports false");
        pc::ReadStats(st);
        Check(st.faults == 2, "that fault is counted too");
        VirtualFree(page, 0, MEM_RELEASE);
    }
    pc::detail::ResetForTest();
}

// --- Design 8.33 (mode view): the seated aim view and the camera after a get-off -----------------------------------
void TestViewMode() {
    pc::Mode m{};
    Check(pc::ParseMode("view", 4, m) && m == pc::Mode::view && Same(pc::ModeName(pc::Mode::view), "view") &&
              static_cast<int>(pc::Mode::view) == 4 && static_cast<int>(mgscoop::PartnerPassCamera::view) == 4,
          "view parses, is named and equals the lab_config value");
    Check(pc::Guards(pc::Mode::view) && pc::Views(pc::Mode::view) && !pc::Views(pc::Mode::aim) && !pc::Views(pc::Mode::drop) &&
              !pc::DropsWhileAiming(pc::Mode::view),
          "view keeps the shot guard and the handle; only view raises and turns");
    char r[160]{};
    bool ok = false;
    pc::detail::ResetForTest();
    Check(pc::HandleCommand("passcam view", true, r, sizeof(r), ok) && ok && pc::Override() == 4 && std::strstr(r, "passcam=view"),
          "the lab lever takes view");
    pc::detail::ResetForTest();
    Check(c::pass_cam_aim_variants == 9 && c::pass_cam_aim_variant_ids[0] == 0 && c::pass_cam_aim_variant_ids[3] == 0x21 &&
              c::pass_cam_aim_variant_ids[8] == 0x35 && pc::VariantIndex(c::pass_cam_aim_variant_ids, 9, 0x22) == 6 &&
              pc::VariantIndex(c::pass_cam_aim_variant_ids, 9, 7) == -1 && pc::VariantIndex(nullptr, 9, 0) == -1,
          "the variant ids of 0x23D4870 and their index");
    Check(pc::kAimObj == 0x20 && pc::kAimFlag == 0x174 && pc::kAimTable == 0x50 && pc::kAimStride == 0x20 && pc::kCamVariant == 0x344 &&
              pc::kCamRequest == 0xD80 && pc::kCamRequestFlags == 0xD38,
          "the offsets of the pinned sites");
}

void TestRaisePlan() {
    const float stock[6] = {0, 0, 0, 0, 0, 1.0f};
    const float raised[6] = {0, pc::kRaiseM, 0, 0, 0, 1.0f};
    pc::RaiseView v{};
    v.ok = v.module = true;
    v.variant = 0;
    v.index = 0;
    std::memcpy(v.entry, stock, sizeof(stock));
    pc::Raise r{};
    const char* why = nullptr;
    Check(pc::RaisePlan(r, false, v, pc::kRaiseM, why) == pc::RaiseWrite::none && Same(why, "end"), "not seated: nothing");
    Check(pc::RaisePlan(r, true, v, pc::kRaiseM, why) == pc::RaiseWrite::apply && Same(why, "apply"), "seated: apply");
    pc::RaiseDone(r, pc::RaiseWrite::apply, 0, true);
    Check(r.applied && r.index == 0 && r.applies == 1, "applied to entry 0");
    v.flag = 1;
    std::memcpy(v.entry, raised, sizeof(raised));
    Check(pc::RaisePlan(r, true, v, pc::kRaiseM, why) == pc::RaiseWrite::none && Same(why, "held"), "held while seated");
    pc::RaiseView gone{};
    Check(pc::RaisePlan(r, true, gone, pc::kRaiseM, why) == pc::RaiseWrite::none && Same(why, "camera") && r.applied,
          "an unread camera keeps the state (no write)");
    v.variant = 1;
    v.index = 1;
    Check(pc::RaisePlan(r, true, v, pc::kRaiseM, why) == pc::RaiseWrite::restore && Same(why, "restore_variant"),
          "a changed variant restores the entry written");
    pc::RaiseDone(r, pc::RaiseWrite::restore, 0, true);
    Check(!r.applied && r.index == -1 && r.restores == 1, "restored");
    v.flag = 0;
    std::memcpy(v.entry, stock, sizeof(stock));
    Check(pc::RaisePlan(r, true, v, pc::kRaiseM, why) == pc::RaiseWrite::apply, "the new variant is applied");
    pc::RaiseDone(r, pc::RaiseWrite::apply, 1, true);
    Check(pc::RaisePlan(r, false, v, pc::kRaiseM, why) == pc::RaiseWrite::restore && Same(why, "end"), "the boarding ended: restore");
    pc::RaiseDone(r, pc::RaiseWrite::restore, 1, true);
    Check(!r.applied && r.restores == 2, "restored at the end");
    // Refusals.
    pc::Raise o{};
    v.flag = 1;
    Check(pc::RaisePlan(o, true, v, pc::kRaiseM, why) == pc::RaiseWrite::none && Same(why, "other_owner") && Same(o.refused, "other_owner"),
          "a set flag is another owner's: refused");
    v.flag = 0;
    Check(pc::RaisePlan(o, true, v, pc::kRaiseM, why) == pc::RaiseWrite::none && Same(why, "refused"), "latched for the boarding");
    Check(pc::RaisePlan(o, false, v, pc::kRaiseM, why) == pc::RaiseWrite::none && o.refused == nullptr, "the latch ends with the boarding");
    pc::Raise e{};
    v.entry[1] = 0.3f;
    Check(pc::RaisePlan(e, true, v, pc::kRaiseM, why) == pc::RaiseWrite::none && Same(why, "entry"), "an entry that is not the constructor's");
    pc::Raise n{};
    std::memcpy(v.entry, stock, sizeof(stock));
    v.index = -1;
    Check(pc::RaisePlan(n, true, v, pc::kRaiseM, why) == pc::RaiseWrite::none && Same(why, "variant") && n.refused == nullptr,
          "a variant outside the list: nothing (retried)");
    v.index = 0;
    v.module = false;
    Check(pc::RaisePlan(n, true, v, pc::kRaiseM, why) == pc::RaiseWrite::none && Same(why, "module"), "no aim module: nothing");
    // Someone else rewrote our entry: never written again this boarding, and never restored over theirs.
    pc::Raise l{};
    v.module = true;
    Check(pc::RaisePlan(l, true, v, pc::kRaiseM, why) == pc::RaiseWrite::apply, "apply");
    pc::RaiseDone(l, pc::RaiseWrite::apply, 0, true);
    v.flag = 1;
    v.entry[1] = 0.25f;
    Check(pc::RaisePlan(l, true, v, pc::kRaiseM, why) == pc::RaiseWrite::none && Same(why, "lost") && !l.applied,
          "a changed entry is lost, not restored");
    Check(pc::RaisePlan(l, false, v, pc::kRaiseM, why) == pc::RaiseWrite::none, "and not restored at the end");
    // A failed apply leaves nothing applied.
    pc::Raise f{};
    pc::RaiseDone(f, pc::RaiseWrite::apply, 0, false);
    Check(!f.applied && f.applies == 0, "a failed apply is not applied");
    // Design 8.34: a failed restore keeps the ownership (retried) up to kRaiseRetryMax, then gives it up once.
    pc::Raise q{};
    pc::RaiseDone(q, pc::RaiseWrite::apply, 2, true);
    Check(!pc::RaiseDone(q, pc::RaiseWrite::restore, 2, false) && q.applied && q.index == 2 && q.retries == 1 && q.restores == 0,
          "a failed restore is kept for a retry");
    Check(!pc::RaiseDone(q, pc::RaiseWrite::restore, 2, true) && !q.applied && q.retries == 0 && q.restores == 1, "the retry lands");
    pc::RaiseDone(q, pc::RaiseWrite::apply, 2, true);
    bool gave_up = false;
    for (std::uint32_t i = 0; i < pc::kRaiseRetryMax; ++i) gave_up = pc::RaiseDone(q, pc::RaiseWrite::restore, 2, false);
    Check(gave_up && !q.applied && q.index == -1 && q.abandoned == 1, "given up after kRaiseRetryMax failed restores");
    // The teardown's last try: forgotten either way.
    pc::RaiseDone(q, pc::RaiseWrite::apply, 2, true);
    pc::RaiseForget(q, false);
    Check(!q.applied && q.abandoned == 2, "teardown: a failed last try is abandoned");
    pc::RaiseDone(q, pc::RaiseWrite::apply, 2, true);
    pc::RaiseForget(q, true);
    Check(!q.applied && q.restores == 2, "teardown: restored");
    pc::RaiseForget(q, true);
    Check(q.restores == 2 && q.abandoned == 2, "teardown: nothing when nothing is ours");
    // Design 8.34 [I] guard: the target offset grows past kRaiseLimitM while ours: restored, the boarding refused.
    pc::Raise g{};
    v = pc::RaiseView{};
    v.ok = v.module = true;
    v.index = 0;
    std::memcpy(v.entry, stock, sizeof(stock));
    v.target_ok = true;
    v.target[1] = 0.7f;
    Check(pc::RaisePlan(g, true, v, pc::kRaiseM, why) == pc::RaiseWrite::apply, "grow: apply");
    pc::RaiseDone(g, pc::RaiseWrite::apply, 0, true);
    v.flag = 1;
    std::memcpy(v.entry, raised, sizeof(raised));
    Check(pc::RaisePlan(g, true, v, pc::kRaiseM, why) == pc::RaiseWrite::none && Same(why, "held"), "grow: a sane offset is held");
    v.target[1] = pc::kRaiseLimitM + 0.1f;
    Check(pc::RaisePlan(g, true, v, pc::kRaiseM, why) == pc::RaiseWrite::restore && Same(why, "grow") && Same(g.refused, "grow"),
          "grow: restored and refused");
    pc::RaiseDone(g, pc::RaiseWrite::restore, 0, true);
    v.flag = 0;
    std::memcpy(v.entry, stock, sizeof(stock));
    Check(pc::RaisePlan(g, true, v, pc::kRaiseM, why) == pc::RaiseWrite::none && Same(why, "refused"), "grow: not applied again this boarding");
    v.target[1] = -1.0e9f;
    pc::Raise gn{};
    pc::RaiseDone(gn, pc::RaiseWrite::apply, 0, true);
    v.flag = 1;
    std::memcpy(v.entry, raised, sizeof(raised));
    Check(pc::RaisePlan(gn, true, v, pc::kRaiseM, why) == pc::RaiseWrite::restore && Same(why, "grow"), "grow: either sign");
    // Seats 1 and 2 are raised; seat 3 (clear view) and the driver never.
    Check(pc::ViewSeat(1) && pc::ViewSeat(2) && !pc::ViewSeat(3) && !pc::ViewSeat(0) && !pc::ViewSeat(0xFF), "the view seats");
}

void TestTurnStep() {
    pc::Turn t{};
    pc::Camera cam{};
    cam.ok = true;
    cam.mode = 5;
    cam.vehicle_mode = 3;
    const char* why = nullptr;
    Check(pc::TurnStep(t, false, cam, 1000, why) == pc::TurnAct::none, "nothing pending");
    pc::TurnArm(t, 25608, 1, 1000);
    Check(t.pending && t.vehicle == 25608 && t.seat == 1, "armed at the end of the boarding");
    Check(pc::TurnStep(t, false, cam, 1100, why) == pc::TurnAct::none && Same(why, "wait"), "waits for the stock get-off camera");
    cam.mode = 3;
    Check(pc::TurnStep(t, false, cam, 1200, why) == pc::TurnAct::none && Same(why, "stock") && t.seen_stock,
          "never under the VehicleCamera (the get-off)");
    cam.mode = 5;
    Check(pc::TurnStep(t, false, cam, 1300, why) == pc::TurnAct::request && Same(why, "request"), "the stock camera let go: request");
    pc::TurnDone(t, true, 1.0f, 1300);
    Check(!t.pending && t.requests == 1 && t.check_ms == 1300 + pc::kTurnCheckMs, "spent; a check is due");
    Check(pc::TurnStep(t, false, cam, 1300 + pc::kTurnCheckMs, why) == pc::TurnAct::check && t.check_ms == 0, "the check line once");
    Check(pc::TurnStep(t, false, cam, 5000, why) == pc::TurnAct::none, "then nothing");
    // Design 8.34: no VehicleCamera seen and no stock clear: no time fallback, it waits and then expires.
    pc::Turn w{};
    pc::TurnArm(w, 25608, 2, 2000);
    Check(pc::TurnStep(w, false, cam, 3600, why) == pc::TurnAct::none && Same(why, "wait"), "no stock get-off seen: waits");
    Check(pc::TurnStep(w, false, cam, 2000 + pc::kTurnMs, why) == pc::TurnAct::expire && Same(why, "timeout"), "and expires");
    // The stock clear of the handle (the release line's reason=stock): the request follows.
    pc::TurnArm(w, 25608, 2, 2000);
    pc::TurnStockClear(w);
    Check(w.stock_clear && pc::TurnStep(w, false, cam, 2100, why) == pc::TurnAct::request, "the stock clear: request");
    pc::TurnDone(w, false, 0.0f, 2100);
    Check(!w.pending && w.expired == 2 && w.check_ms == 0, "a refused request is spent without a check");
    pc::TurnStockClear(w);
    Check(pc::TurnStep(w, false, cam, 2200, why) == pc::TurnAct::none, "a stock clear without a pending turn requests nothing");
    pc::TurnArm(w, 25608, 2, 2000);
    Check(!w.stock_clear, "a new arm forgets the stock clear");
    pc::TurnStockClear(w);
    cam.mode = 3;
    Check(pc::TurnStep(w, false, cam, 2100, why) == pc::TurnAct::none && Same(why, "stock"), "the stock clear still waits for the VehicleCamera");
    cam.mode = 5;
    // The mode left view: cancelled.
    Check(pc::TurnCancel(w) && !w.pending && w.expired == 3 && !pc::TurnCancel(w), "cancelled once");
    // Seated again, or never let go: expired.
    pc::Turn s{};
    pc::TurnArm(s, 25608, 3, 100);
    Check(pc::TurnStep(s, true, cam, 200, why) == pc::TurnAct::expire && Same(why, "seated") && !s.pending, "boarded again: expired");
    pc::Turn o{};
    pc::TurnArm(o, 25608, 3, 100);
    cam.mode = 3;
    Check(pc::TurnStep(o, false, cam, 100 + pc::kTurnMs, why) == pc::TurnAct::expire && Same(why, "timeout"), "never let go: timeout");
    Check(Same(pc::RequestGate(0x1, 0), "busy") && Same(pc::RequestGate(0x1000, 0), "busy") && Same(pc::RequestGate(0, 0x200), "locked") &&
              pc::RequestGate(0x2, 0x4) == nullptr,
          "the request's own gate (0x111A920)");
}

// The native writes on a synthetic camera: the aim module's object, the request fields.
// Design 8.35 [O 89cfa402]: the seated aim lift (CAM+0x154 while the local passenger aims seated in the aim module with
// CAM+0x204 bit 2 clear: the stock never adds the entry then).
void TestLiftPlan() {
    pc::Lift l{};
    pc::LiftIn in{};
    in.want = in.aiming = in.aim_module = in.flags_ok = in.target_ok = true;
    in.flags = 0x63110u;
    in.target_y = 0.0f;
    const char* why = nullptr;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::lift && Same(why, "lift"), "lift: aiming seated, bit 2 clear, the target 0");
    pc::LiftDone(l, pc::LiftWrite::lift, true);
    Check(l.active && l.lifts == 1, "lift: active after the write");
    in.target_y = pc::kRaiseM;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::none && Same(why, "held"), "lift: held");
    in.target_y = 0.0f;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::lift && Same(why, "rewrite") && l.rewrites == 1, "lift: the game wrote 0 back: lifted again");
    pc::LiftDone(l, pc::LiftWrite::lift, true);
    Check(l.lifts == 1 && l.active, "lift: a rewrite is not a new lift");
    in.target_y = pc::kRaiseM;
    in.aiming = false;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::drop && Same(why, "drop"), "lift: the aim ends: dropped while ours");
    pc::LiftDone(l, pc::LiftWrite::drop, true);
    Check(!l.active && l.drops == 1, "lift: dropped");
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::none && Same(why, "none"), "lift: nothing while not aiming");
    in.aiming = true;
    in.target_y = 0.0f;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::lift, "lift: the next aim lifts again");
    pc::LiftDone(l, pc::LiftWrite::lift, true);
    in.aiming = false;
    in.target_y = 0.7f;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::none && Same(why, "gone") && !l.active, "lift: another target at the aim end is left alone");
    in.aiming = true;
    in.target_y = 0.0f;
    in.flags = 0x63114u;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::none && Same(why, "params"), "lift: bit 2 set: the stock applies the entry itself");
    in.flags = 0x63110u;
    in.aim_module = false;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::none && Same(why, "none"), "lift: not the aim module");
    in.aim_module = true;
    in.target_y = 0.3f;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::none && Same(why, "target") && !l.refused, "lift: a target that is not 0 waits (no latch)");
    in.target_ok = false;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::none && Same(why, "camera"), "lift: the target unread");
    in.target_ok = true;
    in.target_y = 0.0f;
    in.flags_ok = false;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::none && Same(why, "camera"), "lift: CAM+0x204 unread");
    in.flags_ok = true;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::lift, "other: lift");
    pc::LiftDone(l, pc::LiftWrite::lift, true);
    in.target_y = 0.25f;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::none && Same(why, "other") && !l.active && Same(l.refused, "other"),
          "other: someone else's value while ours: refused");
    in.target_y = 0.0f;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::none && Same(why, "other"), "other: latched for the boarding");
    in.want = false;
    Check(pc::LiftPlan(l, in, why) == pc::LiftWrite::none && !l.refused, "other: the boarding end clears the latch");
    pc::Lift r{};
    in.want = true;
    Check(pc::LiftPlan(r, in, why) == pc::LiftWrite::lift, "cap: lift");
    pc::LiftDone(r, pc::LiftWrite::lift, true);
    for (std::uint32_t i = 0; i < pc::kLiftRewriteMax; ++i) pc::LiftDone(r, pc::LiftPlan(r, in, why), true);
    Check(r.rewrites == pc::kLiftRewriteMax && r.active, "cap: lifted again up to the cap");
    Check(pc::LiftPlan(r, in, why) == pc::LiftWrite::none && Same(why, "rewritten") && !r.active, "cap: then refused for the boarding");
    pc::Lift f{};
    Check(pc::LiftPlan(f, in, why) == pc::LiftWrite::lift, "fail: lift");
    pc::LiftDone(f, pc::LiftWrite::lift, false);
    Check(!f.active && f.lifts == 0, "fail: a failed lift is not active (tried again next tick)");
}

void TestLiftWrites() {
    pc::detail::ResetForTest();
    Chain k;
    pc::RaiseView v{};
    Chain::Put<std::uint32_t>(k.cam, pc::kCamFlags, 0x63110u);
    Check(pc::ReadRaise(k.Base(), k.P(), v) && v.flags_ok && v.flags == 0x63110u, "CAM+0x204 is read with the raise view");
    Check(pc::WriteLift(k.Base(), k.P(), true, pc::kRaiseM), "lift over the stock 0");
    float y = 0.0f;
    std::memcpy(&y, k.cam + pc::kCamAimTarget + 4, 4);
    Check(y == pc::kRaiseM, "CAM+0x154 = the raise");
    Check(!pc::WriteLift(k.Base(), k.P(), true, pc::kRaiseM), "no lift over a value that is not 0");
    Check(pc::WriteLift(k.Base(), k.P(), false, pc::kRaiseM), "drop ours");
    std::memcpy(&y, k.cam + pc::kCamAimTarget + 4, 4);
    Check(y == 0.0f, "CAM+0x154 = 0 again");
    Chain::Put<float>(k.cam, pc::kCamAimTarget + 4, 0.7f);
    Check(!pc::WriteLift(k.Base(), k.P(), false, pc::kRaiseM), "no drop over another value");
    std::memcpy(&y, k.cam + pc::kCamAimTarget + 4, 4);
    Check(y == 0.7f, "another value is left alone");
}

void TestRaiseAndTurnWrites() {
    pc::detail::ResetForTest();
    Chain k;
    alignas(16) static unsigned char aim_obj[0x200];
    std::memset(aim_obj, 0, sizeof(aim_obj));
    for (unsigned i = 0; i < 9; ++i) Chain::Put<float>(aim_obj, pc::kAimTable + pc::kAimStride * i + 0x14, 1.0f);
    Chain::Put<std::uintptr_t>(aim_obj, pc::kAimObj, Chain::kBase + pc::kAimModuleVtableRva);
    Chain::Put<std::uintptr_t>(k.cam, pc::kCamModules + 16, Chain::A(aim_obj + pc::kAimObj));
    Chain::Put<std::int32_t>(k.cam, pc::kCamVariant, 0x21);
    pc::RaiseView v{};
    Check(pc::ReadRaise(k.Base(), k.P(), v) && v.ok && v.module && v.variant == 0x21 && v.index == 3 && v.flag == 0 && pc::EntryStock(v.entry),
          "the aim module found by its vtable; variant 0x21 is entry 3");
    Check(pc::WriteRaise(k.Base(), k.P(), 3, true, pc::kRaiseM), "apply");
    float y = 0.0f, scale = 0.0f;
    std::memcpy(&y, aim_obj + pc::kAimTable + pc::kAimStride * 3 + 4, 4);
    std::memcpy(&scale, aim_obj + pc::kAimTable + pc::kAimStride * 3 + 0x14, 4);
    Check(aim_obj[pc::kAimFlag] == 1 && y == pc::kRaiseM && scale == 1.0f, "entry 3 = (0, raise, 0, 0, 0, 1) and the flag set");
    Check(pc::ReadRaise(k.Base(), k.P(), v) && v.flag == 1 && pc::EntryRaised(v.entry, pc::kRaiseM), "read back raised");
    Check(pc::WriteRaise(k.Base(), k.P(), 3, false, pc::kRaiseM) && aim_obj[pc::kAimFlag] == 0 && pc::ReadRaise(k.Base(), k.P(), v) &&
              pc::EntryStock(v.entry),
          "restore: the flag cleared, the constructor's entry");
    // Design 8.34: a restore writes only over our own entry (the teardown may run late).
    Check(!pc::WriteRaise(k.Base(), k.P(), 3, false, pc::kRaiseM) && aim_obj[pc::kAimFlag] == 0, "restore: nothing when the flag is not ours");
    Check(pc::WriteRaise(k.Base(), k.P(), 3, true, pc::kRaiseM), "apply again");
    Chain::Put<float>(aim_obj, pc::kAimTable + pc::kAimStride * 3 + 4, 0.25f);
    Check(!pc::WriteRaise(k.Base(), k.P(), 3, false, pc::kRaiseM) && aim_obj[pc::kAimFlag] == 1, "restore: another owner's entry is left");
    Chain::Put<float>(aim_obj, pc::kAimTable + pc::kAimStride * 3 + 4, pc::kRaiseM);
    Check(pc::WriteRaise(k.Base(), k.P(), 3, false, pc::kRaiseM) && aim_obj[pc::kAimFlag] == 0, "restore: ours again, written back");
    // The target offset CAM+0x150 is read with the entry.
    Chain::Put<float>(k.cam, pc::kCamAimTarget + 4, 0.35f);
    Check(pc::ReadRaise(k.Base(), k.P(), v) && v.target_ok && v.target[1] == 0.35f, "the target offset is read");
    Check(!pc::WriteRaise(k.Base(), k.P(), 9, true, pc::kRaiseM) && !pc::WriteRaise(k.Base(), k.P(), -1, true, pc::kRaiseM),
          "an index outside the list is refused");
    pc::Stats st{};
    pc::ReadStats(st);
    Check(st.raises == 4 && st.faults == 0 && st.writes == 0, "four entry writes counted (the refused restores are not)");
    // No aim module: read ok, module false; write refused.
    Chain::Put<std::uintptr_t>(k.cam, pc::kCamModules + 16, 0);
    Check(pc::ReadRaise(k.Base(), k.P(), v) && v.ok && !v.module && v.index == -1, "no aim module");
    Check(!pc::WriteRaise(k.Base(), k.P(), 3, true, pc::kRaiseM) && aim_obj[pc::kAimFlag] == 0, "nothing written without the module");
    // The rotation request: the stock fields and gate.
    Chain::Put<std::uint32_t>(k.cam, pc::kCamRequestFlags, 0x40);
    Check(pc::RequestTurn(k.Base(), k.P(), 0.12f, -1.5f, 0.4f) == nullptr, "the request is made");
    Check(k.Get<std::uint32_t>(pc::kCamRequestFlags) == 0x42 && k.Get<float>(pc::kCamRequest) == 0.4f && k.Get<float>(pc::kCamRequest + 4) == 0.4f &&
              k.Get<float>(pc::kCamRequest + 8) == 0.8f && k.Get<std::uint32_t>(pc::kCamRequest + 0xC) == 0x400126 &&
              k.Get<float>(pc::kCamRequest + 0x10) == 0.12f && k.Get<float>(pc::kCamRequest + 0x14) == -1.5f,
          "CAM+0xD38 |= 2, CAM+0xD80.. = {interp, 0.4, 0.8, 0x400126, pitch, yaw}");
    Chain::Put<std::uint32_t>(k.cam, pc::kCamRequestFlags, 0x1000);
    Chain::Put<float>(k.cam, pc::kCamRequest + 0x14, 9.0f);
    Check(Same(pc::RequestTurn(k.Base(), k.P(), 0.1f, 0.2f, 0.4f), "busy") && k.Get<float>(pc::kCamRequest + 0x14) == 9.0f &&
              k.Get<std::uint32_t>(pc::kCamRequestFlags) == 0x1000,
          "busy: nothing written");
    Chain::Put<std::uint32_t>(k.cam, pc::kCamRequestFlags, 0);
    Chain::Put<std::uint32_t>(k.cam, pc::kCamFlags, 0x200);
    Check(Same(pc::RequestTurn(k.Base(), k.P(), 0.1f, 0.2f, 0.4f), "locked") && k.Get<std::uint32_t>(pc::kCamRequestFlags) == 0,
          "locked (CAM+0x204 bit 9): nothing written");
    pc::ReadStats(st);
    Check(st.turns == 1, "one request counted");
    Chain::Put<std::uintptr_t>(k.cam, 0, Chain::kBase + 0x10);
    Check(Same(pc::RequestTurn(k.Base(), k.P(), 0.1f, 0.2f, 0.4f), "camera") && !pc::ReadRaise(k.Base(), k.P(), v),
          "not a PlayerCamera: refused");
    pc::detail::ResetForTest();
}

// --- The pinned blocks and the install on a synthetic image -----------------------
struct Image {
    unsigned char* base = nullptr;
    std::size_t span = 0;
    Image() {
        span = 0x1300000;
        base = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(base != nullptr, "fixture image");
        std::memset(base, 0xCC, span);
        for (const auto& s : c::code_sites)
            if (s.rva + s.size <= span) std::memcpy(base + s.rva, s.bytes, s.size);
    }
    ~Image() { if (base) VirtualFree(base, 0, MEM_RELEASE); }
};
std::vector<std::string> g_lines;
void Log(const char* line) noexcept { g_lines.emplace_back(line); }
bool Logged(const char* part) {
    for (const auto& l : g_lines) if (l.find(part) != std::string::npos) return true;
    return false;
}

void TestVerifyAndInstall() {
    Check(sizeof(c::pass_camera_blocks) / sizeof(c::pass_camera_blocks[0]) == 19,
          "eighteen passenger camera blocks are pinned (8.27: the look angles, the eye, the rotation; 8.33: the aim module's "
          "variant entry and the rotation request)");
    bool pinned = false, solve = false, collision = false;
    for (const auto& s : c::vtable_slots)
        if (s.vtable_rva == c::cam_vtable_rva && s.offset == 0xA8 && s.target_rva == 0x1121D30) solve = true;
    for (const auto& s : c::code_sites) {
        if (s.rva == c::pass_cam_can_activate_rva && s.size == 39) pinned = true;
        if (s.rva == 0x1121D8F) collision = true;
    }
    Check(pinned && solve && collision, "the CanActivate body, the position solve slot and its +0x1F0 test are in the contract");
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
        img.base[0x1121D92] ^= 1;  // the +0x1F0 displacement of the collision query
        Check(Same(pc::detail::VerifyImage(img.base, img.span, mismatch), "bytes") && mismatch == 0x1121D8F, "a changed collision query refuses");
        img.base[0x1121D92] ^= 1;
        Check(Same(pc::detail::VerifyImage(img.base, 0x1000000, mismatch), "span"), "a short span refuses");
        Check(Same(pc::detail::VerifyImage(nullptr, img.span, mismatch), "base"), "no image");
    }
    {
        // Install: verified once; a second call returns the first result.
        pc::detail::ResetForTest();
        g_lines.clear();
        Image img;
        Check(pc::Install(img.base, img.span, &Log) == nullptr && pc::Installed(), "installed on the synthetic image");
        Check(Logged("partner_pass_camera=installed; mode=connect; field=0x1f0; blocks=18"), "the install line");
        Check(pc::Install(img.base, img.span, &Log) == nullptr && g_lines.size() == 1, "a second install is a no-op");
        pc::detail::ResetForTest();
    }
    {
        // A pinned block differs: refused for the process.
        g_lines.clear();
        Image img;
        img.base[0x12D1FF0] ^= 1;
        Check(Same(pc::Install(img.base, img.span, &Log), "bytes") && !pc::Installed() &&
                  Logged("partner_pass_camera=refused; reason=bytes; rva=0x12d1fca"),
              "a changed connect encoding refuses");
        img.base[0x12D1FF0] ^= 1;
        Check(Same(pc::Install(img.base, img.span, &Log), "bytes") && !pc::Installed(), "the refusal sticks for the process");
        pc::detail::ResetForTest();
    }
}
} // namespace

int main() {
    TestHandleAndOffsets();
    TestArmFor();
    TestPlans();
    TestEpisode();
    TestConnect();
    TestReadCamera();
    TestModeAndAim();
    TestAimStep();
    TestGuardMath();
    TestGuard();
    TestGuardBurst();
    TestGuardDownRules();
    TestGuardNoClimb();
    TestGuardAimGate();
    TestGuardEdges();
    TestTraceDue();
    TestReadViewAndLever();
    TestWritePitchAndModeVtable();
    TestViewMode();
    TestRaisePlan();
    TestTurnStep();
    TestRaiseAndTurnWrites();
    TestLiftPlan();
    TestLiftWrites();
    TestVerifyAndInstall();
    std::printf("partner_pass_camera_tests: %u checks passed\n", checks);
    return 0;
}
