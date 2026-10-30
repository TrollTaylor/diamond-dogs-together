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
#include <cmath>
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
    bool off = false;                       // partner_pass_camera=off (the stock collision for the whole ride)
};
inline Arm ArmFor(const ArmIn& in, const char*& reason) noexcept {
    Arm a{};
    if (!in.passenger_mode) { reason = "mode"; return a; }
    if (!in.passenger) { reason = "not_passenger"; return a; }
    if (in.off) { reason = "off"; return a; }
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

// --- The aim camera (docs/vehicles-companions-design.md 8.25) ------------------
// partner_pass_camera=off|connect|drop|aim (lab and play): off never connects (and clears our handle: the stock
// collision for the whole ride); connect keeps the handle for the whole ride (0.14.4); drop keeps it except while the
// local passenger aims and kAimHoldMs after: the stock collision then keeps the shoulder (aim) camera out of the jeep's
// body, which with the handle stood outside the jeep and looked through it (8.22 / 8.24 frames, run 4f1a072f seat 2);
// aim is drop plus the shot guard below. The lab lever "passcam off|connect|drop|aim|lab|status" overrides the key on
// this copy (lab only).
enum class Mode : std::uint8_t { off = 0, connect = 1, aim = 2, drop = 3 };
inline const char* ModeName(Mode m) noexcept {
    return m == Mode::off ? "off" : m == Mode::connect ? "connect" : m == Mode::drop ? "drop" : "aim";
}
inline bool ParseMode(const char* text, std::size_t n, Mode& out) noexcept {
    if (!text) return false;
    if (n == 3 && std::memcmp(text, "off", 3) == 0) { out = Mode::off; return true; }
    if (n == 7 && std::memcmp(text, "connect", 7) == 0) { out = Mode::connect; return true; }
    if (n == 4 && std::memcmp(text, "drop", 4) == 0) { out = Mode::drop; return true; }
    if (n == 3 && std::memcmp(text, "aim", 3) == 0) { out = Mode::aim; return true; }
    return false;
}
inline bool DropsWhileAiming(Mode m) noexcept { return m == Mode::drop || m == Mode::aim; }
// The local gun aims: the qualified gun record's aim flag (rec+0x3C0 bit 18, partner_input GunFlag::Aiming) or its
// current mode in the aim / fire / reload family (partner_seat_aim census, run 6fc3098b: RMB 6 / 7, RMB+LMB 9 / 10,
// an empty magazine 11 / 12).
inline constexpr std::uint32_t kGunAimBit = 1u << 18;
inline bool GunAims(bool gun_ok, std::uint8_t mode, std::uint32_t flags3c0) noexcept {
    if (!gun_ok) return false;
    return (flags3c0 & kGunAimBit) != 0 || mode == 6 || mode == 7 || mode == 9 || mode == 10 || mode == 11 || mode == 12;
}
inline constexpr std::uint64_t kAimHoldMs = 600;
enum class AimEvent : std::uint8_t { none, drop, restore };
inline const char* AimEventName(AimEvent e) noexcept { return e == AimEvent::drop ? "drop" : e == AimEvent::restore ? "restore" : "none"; }
struct AimDrop {
    bool dropped = false;
    std::uint64_t last_aim_ms = 0;
    std::uint32_t drops = 0, restores = 0;
};
struct AimOut {
    bool want = true;          // the driver keeps (connects) the handle this tick
    AimEvent event = AimEvent::none;
};
// One tick of an armed boarding (`armed`: ArmFor granted it). Modes drop and aim drop the handle from the first aiming
// tick until kAimHoldMs after the last one; connect and off never drop (off never arms). Not armed: the state resets (a
// drop never outlives the boarding; the counters stay).
inline AimOut AimStep(AimDrop& s, Mode mode, bool armed, bool aiming, std::uint64_t now_ms) noexcept {
    AimOut o{};
    if (!armed) {
        s.dropped = false;
        s.last_aim_ms = 0;
        return o;
    }
    if (aiming) s.last_aim_ms = now_ms ? now_ms : 1;
    const bool hold =
        DropsWhileAiming(mode) && s.last_aim_ms != 0 && (aiming || now_ms < s.last_aim_ms || now_ms - s.last_aim_ms < kAimHoldMs);
    if (hold && !s.dropped) {
        s.dropped = true;
        ++s.drops;
        o.event = AimEvent::drop;
    } else if (!hold && s.dropped) {
        s.dropped = false;
        ++s.restores;
        o.event = AimEvent::restore;
    }
    o.want = !s.dropped;
    return o;
}

// --- The lab trace (partner_pass_camera_trace=1, lab only; design 8.25) -------
// Read only: the local camera's position CAM+0xE0 (the eye GunAimPointRelAngles 0x1042F80 and the aim-point solve
// 0x1097710 measure from), its rotation CAM+0x90 (that solve's fallback forward), the look angles CAM+0x268..0x274
// (written by the look updates, e.g. vt+0xF0 0x111F830 adds to +0x270 / +0x274 and copies them to +0x268 / +0x26C),
// the flag words CAM+0x204 and CAM+0xD38 (bit 2: a pending recoil request, vt+0xF8 0x111A710 -> CAM+0xDB0).
inline constexpr std::size_t kCamRot = 0x90, kCamPos = 0xE0, kCamAngles = 0x268, kCamFlags = 0x204, kCamRequests = 0xD38;
// Also the position solve's inputs (0x1121D30 reads them): the offset CAM+0x140..0x14C, CAM+0x180..0x18C and its
// scales CAM+0x254 / +0x258 (the trace's off / piv / sc fields: where the aim camera sits relative to its pivot).
inline constexpr std::size_t kCamOffset = 0x140, kCamPivot = 0x180, kCamScale = 0x254;
struct View {
    bool ok = false;
    float pos[3]{}, rot[4]{}, ang[4]{};
    float off[4]{}, piv[4]{}, sc[2]{};
    std::uint32_t flags = 0, requests = 0;
};
// --- The shot guard (mode aim; design 8.25) --------------------------------------
// [O] run 4f1a072f (trace, seats 3 and 2, the handle held or dropped alike): a seated passenger's shot (gun mode 9 / 10)
// drives the camera's look pitch CAM+0x270 from the aimed value to the top of its range (-1.221 rad, 70 deg up) in
// about 0.5 s (an exponential approach, also pulling the yaw CAM+0x274 by up to 0.3 rad per shot), and nothing brings
// it back: the view goes to the sky and stays pitched up after the aim. Its source is not found [U] (not the recoil
// request vt+0xF8, a random shake; not our handle). The guard keeps the player's own aim: while the passenger aims and
// does not fire, the look angles are its reference; from the first firing tick until kGuardHoldMs after the last one,
// a pitch more than kKickUp above (or kKickDown below) the reference, or a yaw more than kKickYaw beside it, is written
// back to that bound (CAM+0x268 / +0x270 the pitch, +0x26C / +0x274 the yaw, the stock look update's order:
// 0x111FCB1). Pitch: negative looks up.
inline constexpr std::size_t kCamPitch = 0x270, kCamYaw = 0x274, kCamPitchPrev = 0x268, kCamYawPrev = 0x26C;
inline constexpr float kKickUp = 0.05f, kKickDown = 0.03f, kKickYaw = 0.035f;
inline constexpr std::uint64_t kGuardHoldMs = 350;
inline constexpr float kPi = 3.14159265f;
inline float WrapPi(float a) noexcept {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}
inline bool GunFires(bool gun_ok, std::uint8_t mode) noexcept { return gun_ok && (mode == 9 || mode == 10); }
struct Guard {
    bool ref_ok = false;
    float ref_pitch = 0.0f, ref_yaw = 0.0f;
    std::uint64_t last_fire_ms = 0;
    bool active = false;             // a shot window is open
    std::uint32_t window_writes = 0;  // writes in the open window
    float window_max = 0.0f;          // the largest pull away from the reference seen in it (rad)
    std::uint32_t windows = 0, writes = 0;
};
enum class GuardEvent : std::uint8_t { none, open, close };
struct GuardOut {
    bool write = false;
    float pitch = 0.0f, yaw = 0.0f;  // the values to write (write only)
    GuardEvent event = GuardEvent::none;
};
// One tick. `on`: mode aim, an armed boarding, the angles read (ang_ok). Not on: the state resets (a window closes
// without a line; the counters stay).
inline GuardOut GuardStep(Guard& g, bool on, bool aiming, bool firing, bool ang_ok, float pitch, float yaw,
                          std::uint64_t now_ms) noexcept {
    GuardOut o{};
    if (!on || !ang_ok || !std::isfinite(pitch) || !std::isfinite(yaw)) {
        g.ref_ok = false;
        g.last_fire_ms = 0;
        g.active = false;
        return o;
    }
    if (firing) g.last_fire_ms = now_ms ? now_ms : 1;
    const bool window = g.last_fire_ms != 0 && (firing || now_ms < g.last_fire_ms || now_ms - g.last_fire_ms < kGuardHoldMs);
    if (!window) {
        if (g.active) {
            g.active = false;
            o.event = GuardEvent::close;
        }
        g.last_fire_ms = 0;
        g.ref_ok = aiming;
        if (aiming) { g.ref_pitch = pitch; g.ref_yaw = yaw; }
        return o;
    }
    if (!g.ref_ok) {
        // A shot with no aimed tick before it: its first tick is the reference.
        g.ref_ok = true;
        g.ref_pitch = pitch;
        g.ref_yaw = yaw;
    }
    if (!g.active) {
        g.active = true;
        g.window_writes = 0;
        g.window_max = 0.0f;
        ++g.windows;
        o.event = GuardEvent::open;
    }
    const float dp = pitch - g.ref_pitch, dy = WrapPi(yaw - g.ref_yaw);
    const float pull = std::fabs(dp) > std::fabs(dy) ? std::fabs(dp) : std::fabs(dy);
    if (pull > g.window_max) g.window_max = pull;
    const float p = dp < -kKickUp ? g.ref_pitch - kKickUp : dp > kKickDown ? g.ref_pitch + kKickDown : pitch;
    const float y = dy > kKickYaw ? WrapPi(g.ref_yaw + kKickYaw) : dy < -kKickYaw ? WrapPi(g.ref_yaw - kKickYaw) : yaw;
    if (p != pitch || y != yaw) {
        o.write = true;
        o.pitch = p;
        o.yaw = y;
        ++g.window_writes;
        ++g.writes;
    }
    return o;
}

inline constexpr std::uint64_t kTraceAimMs = 100, kTraceIdleMs = 1000, kTraceAfterMs = 4000;
inline constexpr unsigned kTraceCap = 1500;
// The cadence: every kTraceAimMs while aiming or within kTraceAfterMs after, else every kTraceIdleMs while seated.
inline bool TraceDue(std::uint64_t last_ms, std::uint64_t last_aim_ms, bool aiming, std::uint64_t now_ms) noexcept {
    const bool fast = aiming || (last_aim_ms && now_ms >= last_aim_ms && now_ms - last_aim_ms < kTraceAfterMs);
    const std::uint64_t every = fast ? kTraceAimMs : kTraceIdleMs;
    return !last_ms || now_ms < last_ms || now_ms - last_ms >= every;
}

} // namespace mgscoop::partner_pass_camera
