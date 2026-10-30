#pragma once
// F1 vehicles, passenger camera (docs/native-puppet.md "Vehicle boarding",
// "Passenger camera"; research scratchpad complete/re-passcam). The pure part of
// partner_pass_camera: the driver's decision to connect, the connect / release
// plan for the local camera, and the driver's per-boarding episode (connect /
// refused / release lines).
//
// Stock (mgsvtpp 1.0.15.4, Ghidra read-only):
//   The local player's camera CAM = *(P+0xB8) (PlayerCamera, vtable 0x23B8250)
//   picks its module every update (0x1411194B6..0x1411194E5): the first of the
//   six modules at CAM+0xBD0 whose vt+0x18 (CanActivate) is true becomes the
//   mode CAM+0x340. The VehicleCamera's CanActivate 0x12D0F60 is true only for
//   act 0x6F (driver) and 0x70 (get-off), so a back-seat passenger (act 0x6E,
//   the passenger phase 0x1250CF0) keeps the always-true fallback module (mode
//   5): it orbits the seated body, and its collision ray hits the jeep (the
//   interior close-up of run 59cfc415, CAM+0x1F0 = 0).
//   Every module's position solve ends in PlayerCamera vt+0xA8 (0x1121D30):
//   when CAM+0x1F0 has its top bit set, its collision query ignores that owner's
//   geometry (flag 0x800000; PlayerCamera::GetIgnoreColliosionGeoOwnerId
//   0x1114450 returns +0x1F0). The VehicleCamera connects the ridden vehicle
//   there for the driver and the get-off: CAM+0x1F0 = 0x8800000000000000 |
//   (0x6400 | idx), CAM+0x200 = 1; its Deactivate 0x12D0ED0 and the ride's
//   FinishState 0x124C850 clear it (CAM+0x1F0 / +0x1F8 / +0x200 = 0).
//   Granting the VehicleCamera to act 0x6E (wp/pass-cam, hidden run 4ae70de7)
//   does not work: its passenger branch keeps the camera base on the body
//   (CAM+0x20C = 0) but, with the light vehicle's driver parameter set, the
//   camera stayed where the passenger boarded while the jeep drove away.
//
// The fix: while the local passenger report says seat 2/3 of a vehicle, the
// driver tick writes the stock handle of that vehicle to CAM+0x1F0 (CAM+0x200 =
// 1) of the local player's camera, unless the VehicleCamera is the active module
// (the get-off: stock owns the handle) or another owner holds it. The fallback
// camera then orbits the seated passenger without hitting the jeep. The stock
// get-off (VehicleCamera Deactivate) and FinishState clear it; a handle of ours
// still there kReleaseMs after the report ended is cleared by the driver.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mgscoop::partner_pass_camera {

// Acts (u16 (*(Q+0x18))[id]).
inline constexpr std::uint16_t kActPassenger = 0x6E, kActDriver = 0x6F, kActGetOff = 0x70;
inline constexpr std::uint16_t kVehicleBase = 0x6400;  // vehicle id = 0x6400 | (u16 unit+0x78 & 0x1FF)
inline constexpr std::uint16_t kVehicleUnknown = 0xFFFF;
inline constexpr std::uint8_t kSeatUnknown = 0xFF;
inline constexpr std::uint8_t kBackFirst = 2, kBackLast = 3;
// The ignore-collision owner handle (0x12D1FEE, 0x12594E0, 0x1250A90).
inline constexpr std::uint64_t kConnectTag = 0x8800000000000000ull;
inline constexpr std::uint64_t ConnectHandle(std::uint16_t vehicle) noexcept { return kConnectTag | vehicle; }

// Offsets (pinned in tools/generate_partner_contract.py, PASS_CAMERA_SITES).
inline constexpr std::size_t kModuleCam = 0x08;      // module (CAM+0xBD0[i]) + 8 = CAM
inline constexpr std::size_t kCamP = 0x348;          // CAM+0x348 = P
inline constexpr std::size_t kPCam = 0xB8;           // P+0xB8 = CAM (the local player's camera)
inline constexpr std::size_t kPLocalId = 0x218;      // u32 P+0x218 (the id CanActivate reads)
inline constexpr std::size_t kPQ = 0x60;             // P+0x60 = Q
inline constexpr std::size_t kQAct = 0x18;           // u16 (*(Q+0x18))[id]
inline constexpr std::size_t kPUnits = 0x120;        // (*(P+0x120))[id] = the ridden unit
inline constexpr std::size_t kUnitIndex = 0x78;      // u16 unit+0x78
inline constexpr std::size_t kCamConnect = 0x1F0;    // u64 ignore-collision owner
inline constexpr std::size_t kCamTarget = 0x1F8;     // u64 ignore target-check owner
inline constexpr std::size_t kCamConnectOn = 0x200;  // u32
inline constexpr std::size_t kCamPrevMode = 0x33C, kCamMode = 0x340;  // i32 module index (6: none)
inline constexpr std::size_t kCamModules = 0xBD0;    // module*[6]
inline constexpr unsigned kModuleCount = 6;

// --- The driver's decision ----------------------------------------------------
// Passenger mode only; the pinned blocks verified; no mission latch (the
// board's or this one's); the local back-seat passenger reported (seat 2/3 of
// a vehicle-type id). Nothing else is ever connected.
struct Arm {
    bool armed = false;
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t seat = kSeatUnknown;
};
struct ArmIn {
    bool passenger_mode = false;
    bool installed = false;
    bool uncertain = false;                 // board_uncertain or pass_camera's own latch
    bool passenger = false;                 // partner_board::LocalPassenger::valid
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t seat = kSeatUnknown;
    bool seat_one = false;                  // M6: partner_ride_front / partner_ride_cab (seat 1 is a passenger seat)
};
inline Arm ArmFor(const ArmIn& in, const char*& reason) noexcept {
    Arm a{};
    if (!in.passenger_mode) { reason = "mode"; return a; }
    if (!in.passenger) { reason = "not_passenger"; return a; }
    if (!in.installed) { reason = "install"; return a; }
    if (in.uncertain) { reason = "uncertain"; return a; }
    if ((in.seat < kBackFirst || in.seat > kBackLast) && !(in.seat_one && in.seat == 1)) { reason = "seat"; return a; }
    if (in.vehicle == kVehicleUnknown || (in.vehicle >> 9) != (kVehicleBase >> 9)) { reason = "vehicle"; return a; }
    a.armed = true;
    a.vehicle = in.vehicle;
    a.seat = in.seat;
    reason = "armed";
    return a;
}

// --- The local camera ----------------------------------------------------------
struct Camera {
    bool ok = false;
    std::uint64_t connect = 0, target = 0;
    std::uint32_t on = 0;
    std::int32_t mode = -1, prev = -1;
    std::int32_t vehicle_mode = -1;  // the VehicleCamera's index among CAM+0xBD0[0..5] (-1: none)
};
// The VehicleCamera is the active module (the driver, the get-off): it owns CAM+0x1F0.
inline bool StockModule(const Camera& cam) noexcept { return cam.vehicle_mode >= 0 && cam.mode == cam.vehicle_mode; }
enum class Write : std::uint8_t { none, set, clear };
// Connect: write the vehicle's handle unless it is already there, the stock
// module owns the field, or another owner holds it. Reasons (static): camera,
// stock_module, held, other_owner, connect.
inline Write ConnectPlan(const Camera& cam, std::uint16_t vehicle, const char*& reason) noexcept {
    if (!cam.ok) { reason = "camera"; return Write::none; }
    if (StockModule(cam)) { reason = "stock_module"; return Write::none; }
    const std::uint64_t handle = ConnectHandle(vehicle);
    if (cam.connect == handle && cam.on == 1) { reason = "held"; return Write::none; }
    if (cam.connect != 0 && cam.connect != handle) { reason = "other_owner"; return Write::none; }
    reason = "connect";
    return Write::set;
}
// Release: clear only our own handle, and never under the stock module. Reasons:
// camera, not_ours, stock_module, ours.
inline Write ReleasePlan(const Camera& cam, std::uint16_t vehicle, const char*& reason) noexcept {
    if (!cam.ok) { reason = "camera"; return Write::none; }
    if (cam.connect != ConnectHandle(vehicle)) { reason = "not_ours"; return Write::none; }
    if (StockModule(cam)) { reason = "stock_module"; return Write::none; }
    reason = "ours";
    return Write::clear;
}

// --- The driver's episode (one per boarding) ----------------------------------
// connect: the camera holds this vehicle's handle (CAM+0x1F0 == ConnectHandle,
// CAM+0x200 == 1); refused: not within kConnectMs (the plan's reason, or
// not_connected), or not armed while seated (once per boarding); release: after
// the passenger report ended, the camera dropped the handle (stock Deactivate /
// FinishState: CAM+0x1F0 == 0 and CAM+0x200 == 0, or another owner), or
// kReleaseMs passed: our handle is then cleared by the driver (clear = true,
// reason cleared) unless the stock module holds it (still_connected, ok = 0).
inline constexpr std::uint64_t kConnectMs = 3000;
inline constexpr std::uint64_t kReleaseMs = 8000;
enum class Event : std::uint8_t { none, connect, refused, release };
inline const char* EventName(Event e) noexcept {
    return e == Event::connect ? "connect" : e == Event::refused ? "refused" : e == Event::release ? "release" : "none";
}
struct Episode {
    bool active = false;
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t seat = kSeatUnknown;
    std::uint64_t start_ms = 0;
    std::uint32_t writes_at_start = 0;
    bool connected = false, refused = false;
    bool releasing = false;
    std::uint64_t release_ms = 0;
};
struct StepIn {
    bool passenger = false;          // the local passenger report
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t seat = kSeatUnknown;
    bool armed = false;              // ArmFor granted this tick
    const char* arm_reason = "mode";
    const char* plan = "none";       // this tick's ConnectPlan reason (armed only)
    Camera cam;                      // read after this tick's write
    std::uint32_t writes = 0;        // the connect's process counter
    std::uint64_t now_ms = 0;
};
struct StepOut {
    Event event = Event::none;
    const char* reason = "none";
    std::uint32_t ms = 0;
    bool ok = false;                 // release only
    bool clear = false;              // release only: the driver clears our handle now
    std::uint16_t vehicle = kVehicleUnknown;  // the episode's (also after it ended)
    std::uint8_t seat = kSeatUnknown;
};
inline std::uint32_t ClampMs(std::uint64_t ms) noexcept { return ms > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(ms); }
inline StepOut Step(Episode& e, const StepIn& in) noexcept {
    StepOut out{};
    const bool same = e.active && in.passenger && in.vehicle == e.vehicle && in.seat == e.seat;
    if (e.active && !same) {
        // The boarding ended (or changed seat/vehicle): wait for the stock release.
        e.active = false;
        e.releasing = true;
        e.release_ms = in.now_ms;
    }
    if (e.releasing && !e.active) {
        out.vehicle = e.vehicle;
        out.seat = e.seat;
        const bool ours = in.cam.ok && in.cam.connect == ConnectHandle(e.vehicle);
        const std::uint32_t ms = ClampMs(in.now_ms - e.release_ms);
        if (in.cam.ok && in.cam.connect == 0 && in.cam.on == 0) {
            e.releasing = false;
            out = {Event::release, "stock", ms, true, false, e.vehicle, e.seat};
        } else if (in.cam.ok && !ours && in.cam.connect != 0) {
            e.releasing = false;
            out = {Event::release, "other", ms, true, false, e.vehicle, e.seat};
        } else if (in.now_ms - e.release_ms >= kReleaseMs) {
            e.releasing = false;
            const bool clear = ours && !StockModule(in.cam);
            out = {Event::release, clear ? "cleared" : in.cam.ok ? "still_connected" : "cam_read", ms, false, clear, e.vehicle, e.seat};
        }
        if (out.event != Event::none || !in.passenger) return out;
    }
    if (!in.passenger) return out;
    if (!e.active) {
        e = Episode{};
        e.active = true;
        e.vehicle = in.vehicle;
        e.seat = in.seat;
        e.start_ms = in.now_ms;
        e.writes_at_start = in.writes;
    }
    out.vehicle = e.vehicle;
    out.seat = e.seat;
    if (e.connected || e.refused) return out;
    if (!in.armed) {
        e.refused = true;
        out.event = Event::refused;
        out.reason = in.arm_reason;
        out.ms = ClampMs(in.now_ms - e.start_ms);
        return out;
    }
    if (in.cam.ok && in.cam.connect == ConnectHandle(e.vehicle) && in.cam.on == 1) {
        e.connected = true;
        out.event = Event::connect;
        out.reason = "handle";
        out.ms = ClampMs(in.now_ms - e.start_ms);
        return out;
    }
    if (in.now_ms - e.start_ms >= kConnectMs) {
        e.refused = true;
        out.event = Event::refused;
        // Why the plan did not connect (stock_module, other_owner), else not_connected.
        const bool named = in.plan && (std::strcmp(in.plan, "stock_module") == 0 || std::strcmp(in.plan, "other_owner") == 0);
        out.reason = !in.cam.ok ? "cam_read" : named ? in.plan : "not_connected";
        out.ms = ClampMs(in.now_ms - e.start_ms);
    }
    return out;
}

} // namespace mgscoop::partner_pass_camera
