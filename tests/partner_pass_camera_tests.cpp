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

// --- Design 8.25: the aim camera ----------------------------------------------------
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
    Check(pc::DropsWhileAiming(pc::Mode::drop) && pc::DropsWhileAiming(pc::Mode::aim) && !pc::DropsWhileAiming(pc::Mode::connect) &&
              !pc::DropsWhileAiming(pc::Mode::off),
          "drop and aim drop the handle while aiming");
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
    // aim: dropped from the first aiming tick until kAimHoldMs after the last one.
    pc::AimOut o = pc::AimStep(s, pc::Mode::aim, true, false, 1000);
    Check(o.want && o.event == pc::AimEvent::none && !s.dropped, "seated, not aiming: the handle stays");
    o = pc::AimStep(s, pc::Mode::aim, true, true, 1100);
    Check(!o.want && o.event == pc::AimEvent::drop && s.dropped && s.drops == 1, "the aim drops the handle at once");
    o = pc::AimStep(s, pc::Mode::aim, true, true, 1500);
    Check(!o.want && o.event == pc::AimEvent::none, "still aiming: still dropped, no second event");
    o = pc::AimStep(s, pc::Mode::aim, true, false, 1500 + pc::kAimHoldMs - 1);
    Check(!o.want && o.event == pc::AimEvent::none, "within the hold after the aim: still dropped");
    o = pc::AimStep(s, pc::Mode::aim, true, false, 1500 + pc::kAimHoldMs);
    Check(o.want && o.event == pc::AimEvent::restore && !s.dropped && s.restores == 1, "the hold over: restored");
    o = pc::AimStep(s, pc::Mode::aim, true, false, 9000);
    Check(o.want && o.event == pc::AimEvent::none, "idle again");
    // A shot inside the hold renews it.
    pc::AimStep(s, pc::Mode::aim, true, true, 10000);
    pc::AimStep(s, pc::Mode::aim, true, false, 10300);
    o = pc::AimStep(s, pc::Mode::aim, true, true, 10500);
    Check(!o.want && o.event == pc::AimEvent::none && s.drops == 2, "a new aim within the hold keeps the drop");
    o = pc::AimStep(s, pc::Mode::aim, true, false, 10500 + pc::kAimHoldMs - 1);
    Check(!o.want, "the hold counts from the last aiming tick");
    // Not armed (the get-off, a latch, mode off): the drop never outlives the boarding.
    o = pc::AimStep(s, pc::Mode::aim, false, true, 11000);
    Check(o.want && o.event == pc::AimEvent::none && !s.dropped && s.last_aim_ms == 0, "not armed: reset without an event");
    // connect (0.14.4) never drops.
    pc::AimDrop k{};
    for (std::uint64_t t = 1000; t < 3000; t += 100) {
        o = pc::AimStep(k, pc::Mode::connect, true, true, t);
        Check(o.want && o.event == pc::AimEvent::none && !k.dropped, "connect keeps the handle while aiming");
    }
    // A clock of 0 still counts as an aim.
    pc::AimDrop z{};
    o = pc::AimStep(z, pc::Mode::aim, true, true, 0);
    Check(!o.want && z.last_aim_ms == 1, "an aim at clock 0");
    // drop drops like aim.
    pc::AimDrop dr{};
    o = pc::AimStep(dr, pc::Mode::drop, true, true, 2000);
    Check(!o.want && o.event == pc::AimEvent::drop, "drop: the aim drops the handle");
}

// The shot guard (run 4f1a072f: a seated shot pulls the look pitch from 0.087 to -1.221 rad within 0.5 s).
void TestGuard() {
    pc::Guard g{};
    // Aiming, not firing: the reference follows the player's own aim; nothing is written.
    pc::GuardOut o = pc::GuardStep(g, true, true, false, true, 0.087f, 1.576f, 1000);
    Check(!o.write && o.event == pc::GuardEvent::none && g.ref_ok && g.ref_pitch == 0.087f && g.ref_yaw == 1.576f, "aimed: the reference");
    o = pc::GuardStep(g, true, true, false, true, 0.10f, 1.60f, 1050);
    Check(!o.write && g.ref_pitch == 0.10f && g.ref_yaw == 1.60f, "the player's own turn moves the reference");
    // The shot: a window opens; a small kick stays, the pull up is written back to the bound.
    o = pc::GuardStep(g, true, true, true, true, 0.08f, 1.61f, 1100);
    Check(!o.write && o.event == pc::GuardEvent::open && g.active && g.windows == 1, "the shot opens a window; a 1 deg kick stays");
    o = pc::GuardStep(g, true, true, true, true, -0.331f, 1.62f, 1200);
    Check(o.write && std::fabs(o.pitch - (0.10f - pc::kKickUp)) < 1e-6f && o.yaw == 1.62f, "the pull up is held at kKickUp");
    o = pc::GuardStep(g, true, true, true, true, 0.20f, 1.70f, 1250);
    Check(o.write && std::fabs(o.pitch - (0.10f + pc::kKickDown)) < 1e-6f && std::fabs(o.yaw - (1.60f + pc::kKickYaw)) < 1e-6f,
          "a pull down and sideways are held too");
    o = pc::GuardStep(g, true, true, true, true, 0.10f, 1.40f, 1260);
    Check(o.write && std::fabs(o.yaw - (1.60f - pc::kKickYaw)) < 1e-6f && g.ref_pitch == 0.10f, "the reference stays during the window");
    // Between shots (aiming, not firing) inside the hold: still held, the reference unchanged.
    o = pc::GuardStep(g, true, true, false, true, -0.9f, 1.60f, 1260 + pc::kGuardHoldMs - 1);
    Check(o.write && g.active && g.ref_pitch == 0.10f, "within kGuardHoldMs after the last shot: still held");
    // The hold over: the window closes, the reference follows again.
    o = pc::GuardStep(g, true, true, false, true, 0.05f, 1.60f, 1260 + pc::kGuardHoldMs);
    Check(!o.write && o.event == pc::GuardEvent::close && !g.active && g.ref_pitch == 0.05f && g.window_writes == 4 &&
              std::fabs(g.window_max - 1.0f) < 1e-5f && g.writes == 4,
          "the window closes with its writes and largest pull");
    // A shot with no aimed tick before it: its first tick is the reference.
    pc::Guard f{};
    o = pc::GuardStep(f, true, false, true, true, 0.3f, -2.0f, 5000);
    Check(!o.write && o.event == pc::GuardEvent::open && f.ref_pitch == 0.3f && f.ref_yaw == -2.0f, "a first-tick reference");
    // The yaw wraps at +-pi.
    o = pc::GuardStep(f, true, true, true, true, 0.3f, 3.0f, 5050);
    Check(o.write && std::fabs(o.yaw - (-2.0f - pc::kKickYaw)) < 1e-5f, "-2.0 to 3.0 rad is 1.28 rad the short way round: held");
    pc::Guard w{};
    pc::GuardStep(w, true, true, false, true, 0.0f, 3.12f, 6000);
    o = pc::GuardStep(w, true, true, true, true, 0.0f, -3.13f, 6050);
    Check(!o.write, "3.12 to -3.13 rad is 0.03 rad across the wrap: a kick");
    o = pc::GuardStep(w, true, true, true, true, 0.0f, -3.0f, 6100);
    Check(o.write && std::fabs(o.yaw - pc::WrapPi(3.12f + pc::kKickYaw)) < 1e-5f, "past the wrap: held at the bound, wrapped");
    // Off (another mode, the get-off, an unread camera): the state resets, nothing is written.
    o = pc::GuardStep(w, false, true, true, true, 1.0f, 1.0f, 6200);
    Check(!o.write && !w.active && !w.ref_ok && w.last_fire_ms == 0, "off: reset");
    o = pc::GuardStep(w, true, true, true, false, 1.0f, 1.0f, 6300);
    Check(!o.write && !w.ref_ok, "unread angles: nothing");
    o = pc::GuardStep(w, true, true, true, true, std::nanf(""), 1.0f, 6400);
    Check(!o.write && !w.ref_ok, "a NaN angle: nothing");
    Check(pc::GunFires(true, 9) && pc::GunFires(true, 10) && !pc::GunFires(true, 7) && !pc::GunFires(false, 9), "firing is gun mode 9 / 10");
    Check(pc::kKickUp <= 0.06f && pc::kKickYaw <= 0.04f && pc::kGuardHoldMs >= 200 && pc::kGuardHoldMs <= 600,
          "at most about 3 deg of kick, a hold shorter than a shot cycle");
}

// The rotation hold (run 17fce0b7: the angles held, the view still swung; CAM+0x90 tracks the view).
void TestGuardRotation() {
    const float aimed[4] = {0.0f, 0.852f, 0.0f, -0.5235f};   // ~unit
    const float up[4] = {0.296f, 0.708f, 0.459f, -0.445f};   // the swung view of 4f1a072f (~unit)
    const float kick[4] = {0.01f, 0.852f, 0.01f, -0.5235f};  // ~1.6 deg away
    Check(pc::QuatOk(aimed) && pc::QuatOk(up) && !pc::QuatOk(nullptr), "unit rotations");
    const float zero[4] = {0, 0, 0, 0}, nanq[4] = {std::nanf(""), 0, 0, 1};
    Check(!pc::QuatOk(zero) && !pc::QuatOk(nanq), "a zero or NaN rotation is not one");
    Check(pc::QuatAngle(aimed, aimed) < 1e-3f && pc::QuatAngle(aimed, up) > 1.0f && pc::QuatAngle(aimed, kick) < pc::kKickRot,
          "rotation distances");
    pc::Guard g{};
    pc::GuardOut o = pc::GuardStep(g, true, true, false, true, 0.087f, 1.576f, 1000, aimed);
    Check(!o.write && !o.write_rot && g.rot_ok && g.ref_rot[1] == aimed[1], "aimed: the rotation reference");
    o = pc::GuardStep(g, true, true, true, true, 0.087f, 1.576f, 1100, kick);
    Check(o.event == pc::GuardEvent::open && !o.write_rot, "a small kick of the rotation stays");
    o = pc::GuardStep(g, true, true, true, true, 0.087f, 1.576f, 1200, up);
    Check(!o.write && o.write_rot && o.rot[1] == aimed[1] && o.rot[3] == aimed[3] && g.rot_writes == 1,
          "the swung rotation is written back to the reference while the angles stay");
    Check(g.window_max > 1.0f, "the pull counts the rotation");
    o = pc::GuardStep(g, true, true, false, true, 0.087f, 1.576f, 1200 + pc::kGuardHoldMs, up);
    Check(o.event == pc::GuardEvent::close && !o.write_rot && g.ref_rot[0] == up[0], "after the window the reference follows again");
    // No rotation read: the angles alone, as before.
    pc::Guard n{};
    pc::GuardStep(n, true, true, false, true, 0.0f, 0.0f, 1000);
    o = pc::GuardStep(n, true, true, true, true, -1.0f, 0.0f, 1100);
    Check(o.write && !o.write_rot && !n.rot_ok, "without the rotation: the angles only");
    // A shot with no aimed tick: its first rotation is the reference.
    pc::Guard f{};
    o = pc::GuardStep(f, true, false, true, true, 0.0f, 0.0f, 5000, up);
    Check(f.rot_ok && f.ref_rot[0] == up[0] && !o.write_rot, "a first-tick rotation reference");
    // Off resets the rotation reference.
    pc::GuardStep(f, false, true, true, true, 0.0f, 0.0f, 5100, up);
    Check(!f.rot_ok, "off: the rotation reference reset");
    Check(pc::kCamRotation == 0x90 && c::pass_cam_rotation == pc::kCamRotation && pc::kKickRot <= 0.07f, "the rotation offset and kick");
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

void TestWriteAnglesAndModeVtable() {
    pc::detail::ResetForTest();
    Chain k;
    Check(pc::WriteAngles(k.Base(), k.P(), -0.05f, 1.6f) && k.Get<float>(pc::kCamPitch) == -0.05f && k.Get<float>(pc::kCamPitchPrev) == -0.05f &&
              k.Get<float>(pc::kCamYaw) == 1.6f && k.Get<float>(pc::kCamYawPrev) == 1.6f,
          "the guard writes the pitch to +0x270 / +0x268 and the yaw to +0x274 / +0x26C");
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
    Check(!pc::WriteAngles(k.Base(), k.P(), 0.0f, 0.0f) && k.Get<float>(pc::kCamPitch) == 0.5f, "not a PlayerCamera: nothing written");
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
        Check(!pc::WriteAngles(reinterpret_cast<const void*>(Chain::kBase), reinterpret_cast<std::uintptr_t>(player), 0.0f, 0.0f),
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
    Check(sizeof(c::pass_camera_blocks) / sizeof(c::pass_camera_blocks[0]) == 13, "thirteen passenger camera blocks are pinned (8.25: the look angles, the eye, the rotation)");
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
        Check(Logged("partner_pass_camera=installed; mode=connect; field=0x1f0; blocks=13"), "the install line");
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
    TestGuard();
    TestGuardRotation();
    TestTraceDue();
    TestReadViewAndLever();
    TestWriteAnglesAndModeVtable();
    TestVerifyAndInstall();
    std::printf("partner_pass_camera_tests: %u checks passed\n", checks);
    return 0;
}
