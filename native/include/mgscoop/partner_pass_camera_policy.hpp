#pragma once
// F1 vehicles, passenger camera (docs/native-puppet.md "Vehicle boarding",
// "Passenger camera"; research scratchpad complete/re-passcam). The pure part of
// partner_pass_camera: the armed word the driver tick publishes, the decision
// the VehicleCamera CanActivate wrapper makes, and the driver's per-boarding
// episode (connect / refused / release lines).
//
// Stock (mgsvtpp 1.0.15.4, Ghidra read-only):
//   The local player's camera CAM = *(P+0xB8) (PlayerCamera, vtable 0x23B8250)
//   picks its module every update (0x1411194B6..0x1411194E5): the first of the
//   six modules at CAM+0xBD0 whose vt+0x18 (CanActivate) is true becomes the
//   mode CAM+0x340 (the old one is kept at CAM+0x33C; old vt+0x08 Deactivate,
//   new vt+0x00 Activate). The always-true fallback (vtable 0x23B29E0) must sit
//   after the VehicleCamera, which the driver gets.
//   VehicleCamera (tpp::gm::player::(anon)::VehicleCamera, module vtable
//   0x23D67F0, base 0x23D6720) CanActivate 0x12D0F60 is
//       act = (*(Q+0x18))[P+0x218]; return act - 0x6F <= 1   (0x6F driver, 0x70 get-off)
//   so a stock back-seat passenger (act 0x6E, the passenger phase 0x1250CF0)
//   never gets it: its normal camera orbits the body inside the jeep and its
//   collision ray hits the jeep (the frames' interior close-up, cam1f0 = 0).
//   The VehicleCamera's own update (vt+0x20 0x12D2050 -> 0x12D1E80) already
//   handles 0x6E: CAM+0x20C = 0 (the camera base stays the body), and the
//   ignore-collision owner CAM+0x1F0 = 0x8800000000000000 | (0x6400 | idx) with
//   CAM+0x200 = 1 (PlayerCamera::GetIgnoreColliosionGeoOwnerId 0x1114450 reads
//   +0x1F0; the camera's collision skips that vehicle's geometry); the same
//   encoding the driver's per-frame 0x12594E0 and the get-off's 0x1250A90 use.
//   Its Deactivate 0x12D0ED0 and the ride's FinishState 0x124C850
//   (0x124CD38..0x124CD86) clear CAM+0x1F0/+0x1F8/+0x200.
//
// The fix: the CanActivate slot of both VehicleCamera vtables points at a
// wrapper that returns the stock answer, and also true while the driver has
// armed it for the local back-seat passenger it reports (act 0x6E, seat 2/3,
// the vehicle of P+0x120[local]). Nothing writes the camera: the stock module
// activates, connects and, once the act leaves 0x6E/0x70, deactivates.
#include <cstddef>
#include <cstdint>

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

// --- The armed word (the driver tick; one atomic u64) ------------------------
// vehicle | seat << 16 | 1 << 24 (armed) | until_ms << 32. 0 = off.
inline constexpr std::uint32_t kArmLeaseMs = 1500;  // a stalled driver tick drops the arm by itself
struct Arm {
    bool armed = false;
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t seat = kSeatUnknown;
    std::uint32_t until_ms = 0;
};
inline std::uint64_t Pack(const Arm& a) noexcept {
    if (!a.armed) return 0;
    return static_cast<std::uint64_t>(a.vehicle) | static_cast<std::uint64_t>(a.seat) << 16 | std::uint64_t{1} << 24 |
           static_cast<std::uint64_t>(a.until_ms) << 32;
}
inline Arm Unpack(std::uint64_t word) noexcept {
    Arm a{};
    a.armed = ((word >> 24) & 0xFFu) == 1u;
    if (!a.armed) return Arm{};
    a.vehicle = static_cast<std::uint16_t>(word & 0xFFFFu);
    a.seat = static_cast<std::uint8_t>((word >> 16) & 0xFFu);
    a.until_ms = static_cast<std::uint32_t>(word >> 32);
    return a;
}
// Wrap-safe: until is not in the past and at most kArmLeaseMs ahead.
inline bool LeaseLive(std::uint32_t until_ms, std::uint32_t now_ms) noexcept {
    return static_cast<std::uint32_t>(until_ms - now_ms) <= kArmLeaseMs;
}

// --- The wrapper's decision ---------------------------------------------------
// Only consulted when the stock CanActivate said false. `live` is what the
// wrapper read under SEH from the module's own camera: read_ok (every read
// succeeded), cam_match (*(P+0xB8) is this module's camera and its vtable is
// PlayerCamera: the local player's camera only), the local act, the unit of
// P+0x120[local] (0: none) and its u16 index.
struct Live {
    bool read_ok = false;
    bool cam_match = false;
    std::uint16_t act = 0;
    bool unit = false;
    std::uint16_t index = 0xFFFF;
};
// True: grant (the VehicleCamera may activate for the armed passenger). The
// reason is static text: off, lease, read, camera, act, unit, vehicle, grant.
inline bool Grant(const Arm& a, std::uint32_t now_ms, const Live& l, const char*& reason) noexcept {
    if (!a.armed) { reason = "off"; return false; }
    if (!LeaseLive(a.until_ms, now_ms)) { reason = "lease"; return false; }
    if (!l.read_ok) { reason = "read"; return false; }
    if (!l.cam_match) { reason = "camera"; return false; }
    // Never the driver (stock already true) or any other act: the passenger phase only.
    if (l.act != kActPassenger) { reason = "act"; return false; }
    if (!l.unit || l.index >= 0x200) { reason = "unit"; return false; }
    if (static_cast<std::uint16_t>(kVehicleBase | l.index) != a.vehicle) { reason = "vehicle"; return false; }
    reason = "grant";
    return true;
}

// --- The driver's arm ---------------------------------------------------------
// Passenger mode only; the camera patch installed and live; no mission latch
// (the board's or this one's); the local back-seat passenger reported (seat
// 2/3 of a vehicle-type id). Nothing else is ever armed.
struct ArmIn {
    bool passenger_mode = false;
    bool installed = false, live = false;
    bool uncertain = false;                 // board_uncertain or pass_camera's own latch
    bool passenger = false;                 // partner_board::LocalPassenger::valid
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t seat = kSeatUnknown;
    std::uint64_t now_ms = 0;
};
inline Arm ArmFor(const ArmIn& in, const char*& reason) noexcept {
    Arm a{};
    if (!in.passenger_mode) { reason = "mode"; return a; }
    if (!in.passenger) { reason = "not_passenger"; return a; }
    if (!in.installed) { reason = "install"; return a; }
    if (!in.live) { reason = "patch_lost"; return a; }
    if (in.uncertain) { reason = "uncertain"; return a; }
    if (in.seat < kBackFirst || in.seat > kBackLast) { reason = "seat"; return a; }
    if (in.vehicle == kVehicleUnknown || (in.vehicle >> 9) != (kVehicleBase >> 9)) { reason = "vehicle"; return a; }
    a.armed = true;
    a.vehicle = in.vehicle;
    a.seat = in.seat;
    a.until_ms = static_cast<std::uint32_t>(in.now_ms + kArmLeaseMs);
    reason = "armed";
    return a;
}

// --- The driver's episode (one per boarding) ----------------------------------
// connect: the stock VehicleCamera holds this vehicle's handle (CAM+0x1F0 ==
// ConnectHandle, CAM+0x200 == 1) after at least one grant of this boarding;
// refused: not within kConnectMs, or the arm was refused while seated (once per
// boarding); release: after the passenger report ended, the camera dropped the
// handle (stock Deactivate / FinishState: CAM+0x1F0 == 0 and CAM+0x200 == 0, or
// another owner), or kReleaseMs passed (ok = 0).
inline constexpr std::uint64_t kConnectMs = 3000;
inline constexpr std::uint64_t kReleaseMs = 8000;
enum class Event : std::uint8_t { none, connect, refused, release };
inline const char* EventName(Event e) noexcept {
    return e == Event::connect ? "connect" : e == Event::refused ? "refused" : e == Event::release ? "release" : "none";
}
struct Camera {
    bool ok = false;
    std::uint64_t connect = 0, target = 0;
    std::uint32_t on = 0;
    std::int32_t mode = -1, prev = -1;
    std::int32_t vehicle_mode = -1;  // the VehicleCamera's index among CAM+0xBD0[0..5] (-1: none)
};
struct Episode {
    bool active = false;
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t seat = kSeatUnknown;
    std::uint64_t start_ms = 0;
    std::uint32_t grants_at_start = 0;
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
    Camera cam;
    std::uint32_t grants = 0;        // the wrapper's process counter
    std::uint64_t now_ms = 0;
};
struct StepOut {
    Event event = Event::none;
    const char* reason = "none";
    std::uint32_t ms = 0;
    bool ok = false;                 // release only
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
        if (in.cam.ok && in.cam.connect == 0 && in.cam.on == 0) {
            e.releasing = false;
            out = {Event::release, "stock", ClampMs(in.now_ms - e.release_ms), true, e.vehicle, e.seat};
        } else if (in.cam.ok && !ours && in.cam.connect != 0) {
            e.releasing = false;
            out = {Event::release, "other", ClampMs(in.now_ms - e.release_ms), true, e.vehicle, e.seat};
        } else if (in.now_ms - e.release_ms >= kReleaseMs) {
            e.releasing = false;
            out = {Event::release, in.cam.ok ? "still_connected" : "cam_read", ClampMs(in.now_ms - e.release_ms), false, e.vehicle, e.seat};
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
        e.grants_at_start = in.grants;
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
    const bool granted = in.grants != e.grants_at_start;
    if (granted && in.cam.ok && in.cam.connect == ConnectHandle(e.vehicle) && in.cam.on == 1) {
        e.connected = true;
        out.event = Event::connect;
        out.reason = "vehicle_camera";
        out.ms = ClampMs(in.now_ms - e.start_ms);
        return out;
    }
    if (in.now_ms - e.start_ms >= kConnectMs) {
        e.refused = true;
        out.event = Event::refused;
        out.reason = !granted ? "no_grant" : !in.cam.ok ? "cam_read" : "not_connected";
        out.ms = ClampMs(in.now_ms - e.start_ms);
    }
    return out;
}

} // namespace mgscoop::partner_pass_camera
