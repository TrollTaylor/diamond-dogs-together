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

// --- The aim camera (docs/vehicles-companions-design.md 8.27) ------------------
// partner_pass_camera=off|connect|aim|drop (lab and play): off never connects (and clears our handle: the stock
// collision for the whole ride); connect keeps the handle for the whole ride (0.14.4); aim keeps it and adds the shot
// guard below (the default); drop adds the guard too but drops the handle while the local passenger aims and
// kAimHoldMs after, so the stock collision keeps the low seated aim camera out of the jeep's body: in seat 3 the view
// stays clear, but in seat 2 the collision pulls the camera into the passenger (cam at the pivot, the aim target
// 0-1 m away) and the gun refuses to fire (gun mode 15; runs 17fce0b7, 43e5b981), so drop is not the default. The lab
// lever "passcam off|connect|aim|drop|lab|status" overrides the key on this copy (lab only).
// view (design 8.38 / 8.42) is aim plus the camera turned behind the player after a get-off from seat 1 or 2 (the end of
// this file). The seated aim raise of 8.38 was inert in game (8.40: the aim module's parameter record rewrites
// CAM+0x150..0x15C every tick) and is gone (8.42).
enum class Mode : std::uint8_t { off = 0, connect = 1, aim = 2, drop = 3, view = 4 };
inline const char* ModeName(Mode m) noexcept {
    return m == Mode::off ? "off" : m == Mode::connect ? "connect" : m == Mode::drop ? "drop" : m == Mode::view ? "view" : "aim";
}
inline bool ParseMode(const char* text, std::size_t n, Mode& out) noexcept {
    if (!text) return false;
    if (n == 3 && std::memcmp(text, "off", 3) == 0) { out = Mode::off; return true; }
    if (n == 7 && std::memcmp(text, "connect", 7) == 0) { out = Mode::connect; return true; }
    if (n == 4 && std::memcmp(text, "drop", 4) == 0) { out = Mode::drop; return true; }
    if (n == 3 && std::memcmp(text, "aim", 3) == 0) { out = Mode::aim; return true; }
    if (n == 4 && std::memcmp(text, "view", 4) == 0) { out = Mode::view; return true; }
    return false;
}
inline bool DropsWhileAiming(Mode m) noexcept { return m == Mode::drop; }
inline bool Guards(Mode m) noexcept { return m == Mode::aim || m == Mode::drop || m == Mode::view; }
inline bool Views(Mode m) noexcept { return m == Mode::view; }
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
// One tick of an armed boarding (`armed`: ArmFor granted it). Mode drop drops the handle from the first aiming tick
// until kAimHoldMs after the last one; connect and aim keep it, off never arms. Not armed: the state resets (a
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

// --- The lab trace (partner_pass_camera_trace=1, lab only; design 8.27) -------
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
// --- The shot guard (modes aim and drop; design 8.27, review fixes 8.28) ----------------------
// [O] run 4f1a072f (trace, seats 3 and 2, the handle held or dropped alike): a seated passenger's shot (gun mode 9 / 10)
// drives the camera from the aimed pitch to the top of its range (-1.221 rad, 70 deg up) in about 0.5 s, and nothing
// brings it back: the view goes to the sky and stays pitched up after the aim. Its source is not found [U] (not the
// recoil request vt+0xF8, a random shake; not our handle). [O] runs 17fce0b7 / 43e5b981: the state the shot moves is the
// rotation CAM+0x90 (PlayerCamera vt+0x88 0x1120B00 writes it with the look angles; the aim-point solve reads it), a
// Hamilton quaternion (x, y, z, w): forward = q (0, 0, 1) q*, world yaw = atan2(fx, fz), up-pitch = atan2(fy, |fxz|)
// (4f1a072f: within 0.2 deg of the aim target ray). While aiming, the look angles CAM+0x270 (pitch, negative looks up)
// and +0x274 (yaw) are vehicle-relative and follow CAM+0x90 about a tick later (7e79996f). The pull goes up; the yaw
// moved less than 1 deg per window while the pitch was held (7e79996f).
// The guard holds the pull (up and sideways) and lets the player's own look input, the player's aim down and the
// vehicle's turn pass:
//  - reference: while aiming (in the aim module) and not firing, the angle pitch and the world up-pitch and yaw of
//    CAM+0x90 (the stock aim camera keeps its world yaw while the jeep turns: run a64c40d4);
//  - the player's look input, per axis (the effective right stick, which carries the mouse: x the yaw, y the pitch):
//    on its tick that axis's references follow the camera, the other axis stays held; a world yaw jump above kYawJump
//    counts as x input. No latch: following both axes for 150 ms after a mouse turn let the pull lift the view 20-23
//    deg (run 049aa5ed), and a 60 ms latch let it take back each mouse step of a burst (d60dbb48); the camera applies
//    the mouse on the tick its input shows (the trace);
//  - window: from the first firing tick until kGuardHoldMs after the last one. The angle pitch going down moves its
//    reference down (the player's aim), and the world reference by as much as the world also went down (a bump of the
//    vehicle alone moves neither). An angle pitch more than kKickUp above its reference is written back to it
//    (CAM+0x270 / +0x268); a world up-pitch more than kKickUp above its reference turns CAM+0x90 down about its own
//    right axis by the excess (q * qx(excess)); a world yaw more than kKickYaw beside its reference turns CAM+0x90 back
//    about the world vertical (qy * q). Run a64c40d4 (v2, yaw free): the pull turned a burst 0.93 rad sideways;
//  - a window that ends with the hold settles a remaining climb above kSettleUp (no climb from shot to shot);
//  - nothing is written outside the aim module (vtable kAimModuleVtableRva, CAM+0x340 index 2) or after the aim ends:
//    an open window closes at once (end=aim_end).
// [U] Whether every mouse delta shows in the right stick on the tick the driver samples (the latch covers a tick of lag).
inline constexpr std::size_t kCamPitch = 0x270, kCamYaw = 0x274, kCamPitchPrev = 0x268, kCamYawPrev = 0x26C;
inline constexpr std::size_t kCamRotation = 0x90;
inline constexpr float kKickUp = 0.05f, kSettleUp = 0.01f;
// The world yaw of CAM+0x90 without the player's look input: held within kKickYaw (the pull also turns it sideways:
// 0.93 rad in a four-shot burst with the pitch held, run a64c40d4 host); a close settles it above kSettleYaw.
inline constexpr float kKickYaw = 0.035f, kSettleYaw = 0.01f;
// The player's own look input (the effective right stick, which carries the mouse: PP+0x474 / +0x47C with PP+0x488
// bit 1, the PP update 0x1207157; run 049aa5ed: look=-3600 milli on a mouse step of -60): above kLookInput on a tick,
// that axis follows the camera on that tick.
inline constexpr float kLookInput = 0.02f;
// A world yaw jump bigger than any pull in one tick (the pull moved it at most ~0.03 rad per tick: a64c40d4, d60dbb48)
// counts as the player's input (a mouse frame the driver's tick did not sample).
inline constexpr float kYawJump = 0.05f;
inline constexpr std::uint64_t kGuardHoldMs = 350;
// The aim camera module (CAM+0xBD0[2] while RMB is held: runs 4f1a072f..06aa3fc6 mvt=0x23d4d20; its CanActivate
// 0x12BBAE0 reads the gun record's aim bit and the pad's aim button). Pinned in the contract (slots).
inline constexpr std::uintptr_t kAimModuleVtableRva = 0x23D4D20;
inline constexpr float kPi = 3.14159265f;
inline float WrapPi(float a) noexcept {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}
inline bool GunFires(bool gun_ok, std::uint8_t mode) noexcept { return gun_ok && (mode == 9 || mode == 10); }
inline bool QuatOk(const float* q) noexcept {
    if (!q) return false;
    float n = 0.0f;
    for (unsigned i = 0; i < 4; ++i) {
        if (!std::isfinite(q[i])) return false;
        n += q[i] * q[i];
    }
    return n > 0.9f && n < 1.1f;
}
// The world up-pitch of a camera rotation (rad, positive looks up): forward = q (0, 0, 1) q*.
inline float UpPitch(const float* q) noexcept {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float fx = 2.0f * (x * z + w * y), fy = 2.0f * (y * z - w * x), fz = 1.0f - 2.0f * (x * x + y * y);
    return std::atan2(fy, std::sqrt(fx * fx + fz * fz));
}
// The world yaw of a camera rotation (rad; the body yaw's convention atan2(fx, fz)).
inline float WorldYaw(const float* q) noexcept {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    return std::atan2(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y));
}
// q turned down by `down` rad about its own right axis (q * qx(down), normalized): the up-pitch drops by `down`, the yaw
// and roll stay (exact without roll).
// q turned by `turn` rad about the world vertical (qy(turn) * q, normalized): the world yaw grows by `turn`, the
// up-pitch and roll stay.
inline void YawTurn(const float* q, float turn, float* out) noexcept {
    const float s = std::sin(0.5f * turn), c = std::cos(0.5f * turn);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float r[4] = {c * x + s * z, c * y + s * w, c * z - s * x, c * w - s * y};
    const float n = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
    for (unsigned i = 0; i < 4; ++i) out[i] = n > 0.0f ? r[i] / n : q[i];
}
inline void PitchDown(const float* q, float down, float* out) noexcept {
    const float s = std::sin(0.5f * down), c = std::cos(0.5f * down);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float r[4] = {w * s + x * c, y * c + z * s, z * c - y * s, w * c - x * s};
    const float n = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
    for (unsigned i = 0; i < 4; ++i) out[i] = n > 0.0f ? r[i] / n : q[i];
}
struct GuardIn {
    bool on = false;          // mode aim / drop and an armed boarding (the local back-seat passenger)
    bool aiming = false;      // GunAims
    bool firing = false;      // GunFires
    bool aim_module = false;  // the active camera module is the aim camera (kAimModuleVtableRva)
    bool ang_ok = false;      // the look angles were read
    bool look_x = false, look_y = false;  // the player's own look input this tick: the right stick / mouse x (yaw), y (pitch)
    float pitch = 0.0f, yaw = 0.0f;  // CAM+0x270 / +0x274
    const float* rot = nullptr;      // CAM+0x90 (null: unread)
    std::uint64_t now_ms = 0;
};
struct Guard {
    bool ref_ok = false;
    float ref_pitch = 0.0f;   // the angle pitch reference (vehicle-relative while aiming)
    bool up_ok = false;
    float ref_up = 0.0f;      // the world up-pitch reference of CAM+0x90
    float open_yaw = 0.0f, last_yaw = 0.0f;  // the angle yaw at the window's open and its last tick
    float ref_yaw = 0.0f;     // the world yaw reference of CAM+0x90 (with ref_up)
    bool prev_ok = false;
    float prev_yaw = 0.0f;    // the world yaw CAM+0x90 had after the last tick (as written, or as read)
    std::uint32_t look_ticks = 0;     // window ticks that followed the player's input (the open window)
    std::uint64_t last_fire_ms = 0;
    bool active = false;              // a shot window is open
    std::uint32_t window_writes = 0;  // ticks with a write in the open window (its settle included)
    float window_max = 0.0f;          // the largest pull up seen in it (rad)
    std::uint32_t windows = 0, writes = 0, rot_writes = 0, settles = 0;
};
enum class GuardEvent : std::uint8_t { none, open, close };
enum class GuardEnd : std::uint8_t { none, hold, aim_end };
inline const char* GuardEndName(GuardEnd e) noexcept { return e == GuardEnd::hold ? "hold" : e == GuardEnd::aim_end ? "aim_end" : "none"; }
struct GuardOut {
    bool write = false;
    float pitch = 0.0f;              // the angle pitch for CAM+0x270 / +0x268 (write only; the yaw is never written)
    bool write_rot = false;
    float rot[4]{};                  // the rotation for CAM+0x90 (write_rot only)
    GuardEvent event = GuardEvent::none;
    GuardEnd end = GuardEnd::none;   // close only
    bool settle = false;             // close only: the hold's close wrote a remaining climb back
};
namespace detail {
// One correction: follows the player's aim down, then writes back a pull up above `up_bound` or sideways beyond
// `yaw_bound`. Returns the largest pull (rad).
inline float GuardCorrect(Guard& g, const GuardIn& in, bool rot_now, float up_now, float up_bound, float yaw_bound, GuardOut& o) noexcept {
    const float down = in.pitch - g.ref_pitch;  // > 0: the angle went below its reference (pitch negative looks up)
    if (down > 0.0f) {
        g.ref_pitch = in.pitch;
        const float world_down = g.ref_up - up_now;
        if (g.up_ok && rot_now && world_down > 0.0f) g.ref_up -= world_down < down ? world_down : down;
    }
    // Each state against its own reference: the angles follow CAM+0x90 a tick late, so a rotation turned by the angles'
    // (late) excess would overshoot.
    const float u_ang = g.ref_pitch - in.pitch;
    const bool world = g.up_ok && rot_now;
    const float u_rot = world ? up_now - g.ref_up : 0.0f;
    const float dyaw = world ? WrapPi(WorldYaw(in.rot) - g.ref_yaw) : 0.0f;
    if (u_ang > up_bound) {
        o.write = true;
        o.pitch = g.ref_pitch;
    }
    const bool turn_up = world && u_rot > up_bound, turn_yaw = world && std::fabs(dyaw) > yaw_bound;
    if (turn_up || turn_yaw) {
        float down_q[4];
        PitchDown(in.rot, turn_up ? u_rot : 0.0f, down_q);
        YawTurn(down_q, turn_yaw ? -dyaw : 0.0f, o.rot);
        o.write_rot = true;
        ++g.rot_writes;
    }
    if (o.write || o.write_rot) ++g.writes;
    float pull = u_ang > u_rot ? u_ang : u_rot;
    if (std::fabs(dyaw) > pull) pull = std::fabs(dyaw);
    return pull;
}
// The references taken from this tick (the player's own aim).
inline void GuardFollow(Guard& g, const GuardIn& in, bool rot_now, float up_now) noexcept {
    g.ref_ok = true;
    g.ref_pitch = in.pitch;
    g.up_ok = rot_now;
    if (rot_now) {
        g.ref_up = up_now;
        g.ref_yaw = WorldYaw(in.rot);
    }
}
// The player's look input per axis: x moves the world yaw reference, y the pitch references (angle and world).
inline void GuardFollowAxes(Guard& g, const GuardIn& in, bool rot_now, float up_now, bool x, bool y) noexcept {
    if (y) {
        g.ref_pitch = in.pitch;
        if (rot_now && g.up_ok) g.ref_up = up_now;
    }
    if (x && rot_now && g.up_ok) g.ref_yaw = WorldYaw(in.rot);
}
} // namespace detail
// One tick. Not on (another mode, not armed, the angles unread or not finite): the state resets (a window closes without
// a line; the counters stay).
inline GuardOut GuardStep(Guard& g, const GuardIn& in) noexcept {
    GuardOut o{};
    if (!in.on || !in.ang_ok || !std::isfinite(in.pitch) || !std::isfinite(in.yaw)) {
        g.ref_ok = g.up_ok = false;
        g.last_fire_ms = 0;
        g.prev_ok = false;
        g.active = false;
        return o;
    }
    const bool rot_now = QuatOk(in.rot);
    const float up_now = rot_now ? UpPitch(in.rot) : 0.0f;
    if (!in.aiming || !in.aim_module) {
        // Not aiming in the aim camera: nothing is written; an open window closes now.
        if (g.active) {
            g.active = false;
            o.event = GuardEvent::close;
            o.end = GuardEnd::aim_end;
        }
        g.ref_ok = g.up_ok = false;
        g.last_fire_ms = 0;
        g.prev_ok = false;
        return o;
    }
    if (in.firing) g.last_fire_ms = in.now_ms ? in.now_ms : 1;
    const bool window = g.last_fire_ms != 0 && (in.firing || in.now_ms < g.last_fire_ms || in.now_ms - g.last_fire_ms < kGuardHoldMs);
    const bool jump = g.prev_ok && rot_now && std::fabs(WrapPi(WorldYaw(in.rot) - g.prev_yaw)) > kYawJump;
    const bool follow_x = in.look_x || jump, follow_y = in.look_y;
    // The world yaw CAM+0x90 is left with after this tick (as written, or as read): the next tick's jump test.
    const auto leave = [&](const GuardOut& out) noexcept {
        g.prev_ok = rot_now;
        if (rot_now) g.prev_yaw = WorldYaw(out.write_rot ? out.rot : in.rot);
    };
    if (!window) {
        bool settled = false;
        if (g.active) {
            g.last_yaw = in.yaw;
            if (g.ref_ok) {
                detail::GuardFollowAxes(g, in, rot_now, up_now, follow_x, follow_y);
                detail::GuardCorrect(g, in, rot_now, up_now, kSettleUp, kSettleYaw, o);
            }
            settled = o.write || o.write_rot;
            if (settled) {
                ++g.settles;
                ++g.window_writes;
            }
            o.settle = settled;
            g.active = false;
            o.event = GuardEvent::close;
            o.end = GuardEnd::hold;
        }
        g.last_fire_ms = 0;
        // The references follow the player's aim (after a settle: the settled values).
        if (!settled) {
            detail::GuardFollow(g, in, rot_now, up_now);
        } else {
            if (!o.write) g.ref_pitch = in.pitch;
            if (!o.write_rot && rot_now) {
                g.up_ok = true;
                g.ref_up = up_now;
                g.ref_yaw = WorldYaw(in.rot);
            }
        }
        leave(o);
        return o;
    }
    if (!g.ref_ok) detail::GuardFollow(g, in, rot_now, up_now);  // a shot with no aimed tick: its first tick is the reference
    if (!g.active) {
        g.active = true;
        g.window_writes = 0;
        g.window_max = 0.0f;
        g.look_ticks = 0;
        g.open_yaw = in.yaw;
        ++g.windows;
        o.event = GuardEvent::open;
    }
    g.last_yaw = in.yaw;
    if (follow_x || follow_y) {
        // The player's own look input: that axis follows the camera; the other stays held.
        detail::GuardFollowAxes(g, in, rot_now, up_now, follow_x, follow_y);
        ++g.look_ticks;
    }
    const float pull = detail::GuardCorrect(g, in, rot_now, up_now, kKickUp, kKickYaw, o);
    if (pull > g.window_max) g.window_max = pull;
    if (o.write || o.write_rot) ++g.window_writes;
    leave(o);
    return o;
}
// The yaw the open (or last) window moved (rad, wrapped): the player's turn and the vehicle's, which pass.
inline float WindowYaw(const Guard& g) noexcept { return WrapPi(g.last_yaw - g.open_yaw); }

inline constexpr std::uint64_t kTraceAimMs = 100, kTraceIdleMs = 1000, kTraceAfterMs = 4000;
inline constexpr unsigned kTraceCap = 1500;
// The cadence: every kTraceAimMs while aiming or within kTraceAfterMs after, else every kTraceIdleMs while seated.
inline bool TraceDue(std::uint64_t last_ms, std::uint64_t last_aim_ms, bool aiming, std::uint64_t now_ms) noexcept {
    const bool fast = aiming || (last_aim_ms && now_ms >= last_aim_ms && now_ms - last_aim_ms < kTraceAfterMs);
    const std::uint64_t every = fast ? kTraceAimMs : kTraceIdleMs;
    return !last_ms || now_ms < last_ms || now_ms - last_ms >= every;
}

// The seats whose get-off turns the camera (mode view): seat 1 (front) and seat 2 (left back). Seat 3 (the right back
// seat) leaves the camera behind the player, looking forward (299de3e1).
inline bool ViewSeat(std::uint8_t seat) noexcept { return seat == 1 || seat == 2; }

// --- Seat first person (lab.ini partner_seat_fpv; design 8.46) -------------------------------------------------------
// [O] 9a0c1f27: from seats 1 and 2 the stock seated aim looks into the panel, the own leg or the roll bar; the stock F tap
// gives the first-person view (the subjective camera module, CAM+0x340 = 1) with a clear view. [S] The tap is answered by
// the gun plugin's PollingForSubjectiveCamera 0x1053E80: while the gun hold gate is open (flag 0x19 or rec+0x3C0 bit 14)
// it returns bit m of the subjective camera impl+0x60 (m from the hand slot rec+0x3DA: FpvBit), which
// ToggleAimFpvModeOnPad 0x12A0580 flips on pad button 10 (F); the bit is sticky per slot (the next aim with that slot
// starts in first person). FpvStep sets the slot's bit while the local passenger aims from seat 1 or 2 and clears the
// bits it set when the aim ends, at the get-off, a seat change and the key's end; a bit seen set during an aim that goes
// clear while the aim goes on is the player's own F tap: no more bits are set for the rest of that boarding (the stock
// toggle stays the player's).
inline constexpr std::uint32_t kFpvSlotMask = 0x11F;
inline constexpr std::size_t kSubjImpl = 0x20, kSubjToggles = 0x60;  // impl = module - 0x20; the toggles impl+0x60
inline constexpr std::uintptr_t kSubjModuleVtableRva = 0x23D1F00;
// The gun poller's slot map (table 0x1054030): slot rec+0x3DA -> toggle bit; -1: a slot the poller never answers.
inline int FpvBit(std::uint8_t slot) noexcept {
    switch (slot) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 2;
    case 3: return 4;
    case 5: return 3;
    case 6: return 8;
    default: return -1;
    }
}
// Seats 1 (front) and 2 (left back); seat 3 aims clear over the shoulder and the driver is stock.
inline bool FpvSeat(std::uint8_t seat) noexcept { return seat == 1 || seat == 2; }
// The gun is ready (aimed 7, firing 9 / 10): the raise (6) is done and clear, so its muzzle probe saw a clear shoulder
// pose, which first person then keeps (shot.md: GunMuzzleProbeIssue 0x1062A30 skips the pose update with rec+0x3C0 bit 13).
// Not the blocked mode 15 (the red no-fire icon): design 8.51 [O runs 2a394a64 / ff4134f4], 5 of 5 bits set at 15 left the
// gun at 15 with the rifle body across the first-person view and no shot. A blocked aim keeps the stock shoulder view and
// switches only if it turns clear (15 -> 7; [I] the shoulder pose follows the view, no run has shown it).
inline bool FpvReady(bool gun_ok, std::uint8_t mode) noexcept { return gun_ok && (mode == 7 || mode == 9 || mode == 10); }
enum class FpvEvent : std::uint8_t { none, arm, set, opt_out, restore, stop, retry, fallback };
// Design 8.56 [O run 859964ce]: a set at 7 does not keep the aim clear. From seat 1 looking 8 deg down the shoulder raise
// reached 7, the set took the gun to 15 within 17 ms and it stayed 15 (rifle across, nothing fired), while the first
// person looked about 3.5 deg lower than the shoulder aim at the same turn. Our bit read set while the gun stays at 15 for
// kFpvBlockedMs is cleared (fallback): the stock shoulder view and its red mark come back. In that aim it is set again only
// once the shoulder aim (the aim camera module's look angles) has turned up by kFpvRearmPitch or sideways by kFpvRearmYaw
// from where it came back, and at most kFpvFallbacksPerAim falls per aim, so the view cannot flicker; the next aim sets at
// 7 / 9 / 10 as before.
inline constexpr std::uint64_t kFpvBlockedMs = 100;
inline constexpr float kFpvRearmPitch = 0.07f;  // rad up (the look pitch is positive down), more than the 3.5 deg offset
inline constexpr float kFpvRearmYaw = 0.175f;   // rad (10 deg) either way
inline constexpr std::uint8_t kFpvFallbacksPerAim = 2;
// A clear of ours stays pending until a read shows the bit clear (a failed write, the game's plain store over our
// compare-exchange); it is retried on later ticks, at most kFpvClearTries times, then given up (design 8.47).
inline constexpr std::uint32_t kFpvClearTries = 30;
inline const char* FpvEventName(FpvEvent e) noexcept {
    switch (e) {
    case FpvEvent::arm: return "arm";
    case FpvEvent::retry: return "retry";
    case FpvEvent::set: return "set";
    case FpvEvent::opt_out: return "opt_out";
    case FpvEvent::restore: return "restore";
    case FpvEvent::stop: return "stop";
    case FpvEvent::fallback: return "fallback";
    default: return "none";
    }
}
struct Fpv {
    bool active = false;    // a seat 1 / 2 boarding with the switch on
    bool opt_out = false;   // the player's own F tap this boarding: nothing more is set
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t seat = kSeatUnknown;
    std::uint32_t ours = 0;  // the toggle bits this switch set and has not cleared
    std::uint32_t seen = 0;  // the bits seen set during this aim
    bool aim = false;        // the last tick aimed
    std::uint32_t pending = 0;        // ours, cleared but not yet read clear (kept over a boarding's end and a mission's)
    std::uint32_t pending_tries = 0;  // retries of the pending clear
    std::uint32_t sets = 0, restores = 0, aims = 0, opt_outs = 0, give_ups = 0;
    // Design 8.56: the blocked first person's fall back to the shoulder view (FpvStep).
    bool fallback = false;         // this aim fell back: no set until the shoulder aim turns (kFpvRearm*)
    bool ref_ok = false;           // the shoulder aim's look angles where it came back
    float ref_pitch = 0.0f, ref_yaw = 0.0f;
    std::uint8_t aim_falls = 0;    // falls this aim (kFpvFallbacksPerAim)
    std::uint64_t blocked_ms = 0;  // the first tick of the current run of 15 with our bit set (0: none)
    std::uint32_t fallbacks = 0;
    std::uint32_t fell = 0;        // fall-back clears not yet read clear: cleared again at the aim end and the stop (review)
};
struct FpvIn {
    bool want = false;     // the key (or the lab lever) on, the hooks there, nothing latched
    bool seated = false;   // the local passenger report
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t seat = kSeatUnknown;
    bool aiming = false;   // GunAims of the local gun
    bool ready = false;    // the gun is ready (FpvReady: mode 7, 9, 10; not the blocked 15): the bit is set only then
    bool read_ok = false;  // the subjective module was found
    std::uint32_t toggles = 0;  // impl+0x60
    bool slot_ok = false;
    std::uint8_t slot = 0;      // rec+0x3DA
    bool blocked = false;       // aiming with the gun at the blocked mode 15 (design 8.56)
    std::uint64_t now_ms = 0;
    bool ang_ok = false;        // the aim camera module's look angles (read only during a fall)
    float pitch = 0.0f, yaw = 0.0f;
};
struct FpvOut {
    FpvEvent event = FpvEvent::none;
    std::uint32_t set = 0, clear = 0;  // the toggle bits to set / clear (never both)
    const char* reason = "none";
};
// A tick with no other event retries the pending clear (unread too: the write finds the module itself); given up after
// kFpvClearTries retries.
inline FpvOut FpvRetry(Fpv& s, FpvOut o) noexcept {
    if (!s.pending || o.event != FpvEvent::none) return o;
    o.event = FpvEvent::retry;
    if (++s.pending_tries > kFpvClearTries) {
        o.reason = "give_up";
        s.pending = 0;
        s.pending_tries = 0;
        ++s.give_ups;
        return o;
    }
    o.reason = "pending";
    o.clear = s.pending;
    return o;
}
inline void FpvFallReset(Fpv& s) noexcept {
    s.fallback = s.ref_ok = false;
    s.ref_pitch = s.ref_yaw = 0.0f;
    s.aim_falls = 0;
    s.blocked_ms = 0;
}
inline FpvOut FpvStep(Fpv& s, const FpvIn& in) noexcept {
    FpvOut o{};
    // A pending clear is confirmed by a read that shows the bit clear.
    if (s.pending && in.read_ok) s.pending &= in.toggles & kFpvSlotMask;
    if (!s.pending) s.pending_tries = 0;
    if (s.fell && in.read_ok) s.fell &= in.toggles & kFpvSlotMask;
    const bool seat_ok = in.want && in.seated && FpvSeat(in.seat);
    if (s.active && (!seat_ok || in.vehicle != s.vehicle || in.seat != s.seat)) {
        // The boarding ends (the get-off, a seat change, the key off): the bits still ours are cleared (unread: all of them)
        // and stay pending until a read shows them clear.
        o.event = FpvEvent::stop;
        o.reason = !in.want ? "off" : !in.seated ? "get_off" : "seat";
        const std::uint32_t mine = s.ours | s.fell;
        const std::uint32_t still = (in.read_ok ? (mine & in.toggles) : mine) & kFpvSlotMask;
        if (still) ++s.restores;
        s.pending |= still;
        o.clear = s.pending;
        s.active = s.opt_out = s.aim = false;
        s.ours = s.seen = s.fell = 0;
        FpvFallReset(s);
        s.vehicle = kVehicleUnknown;
        s.seat = kSeatUnknown;
        return o;
    }
    if (!seat_ok) return FpvRetry(s, o);
    if (!s.active) {
        s.active = true;
        s.opt_out = s.aim = false;
        s.ours = s.seen = 0;
        FpvFallReset(s);
        s.vehicle = in.vehicle;
        s.seat = in.seat;
        o.event = FpvEvent::arm;
        o.reason = "seat";
        return o;
    }
    if (!in.read_ok) return FpvRetry(s, o);
    const std::uint32_t toggles = in.toggles & kFpvSlotMask;
    if (!in.aiming) {
        s.aim = false;
        s.seen = 0;
        FpvFallReset(s);
        if (s.ours | s.fell) {
            const std::uint32_t still = (s.ours | s.fell) & toggles;
            s.ours = s.fell = 0;
            if (still) {
                s.pending |= still;
                o.clear = s.pending;
                o.event = FpvEvent::restore;
                o.reason = "aim_end";
                ++s.restores;
            }
        }
        return FpvRetry(s, o);
    }
    if (!s.aim) {
        s.aim = true;
        s.seen = 0;
        FpvFallReset(s);
        ++s.aims;
    }
    if (s.opt_out) return FpvRetry(s, o);
    // The player's own F tap: a bit seen set during this aim is clear now (our own pending bits never count).
    const std::uint32_t dropped = s.seen & ~toggles;
    if (dropped) {
        s.opt_out = true;
        s.ours &= ~dropped;
        s.seen = 0;
        ++s.opt_outs;
        o.event = FpvEvent::opt_out;
        o.reason = "tap";
        return o;
    }
    s.seen |= toggles & ~s.pending;
    // Design 8.56: our bit read set and the gun held at 15 for kFpvBlockedMs: clear it (the shoulder view back). Our own
    // clear leaves `seen`, so it is no opt-out; it stays pending until a read shows it clear.
    const std::uint32_t held = s.ours & toggles;
    if (in.blocked && held) {
        if (!s.blocked_ms) s.blocked_ms = in.now_ms ? in.now_ms : 1;
        if (in.now_ms >= s.blocked_ms && in.now_ms - s.blocked_ms >= kFpvBlockedMs) {
            s.ours &= ~held;
            s.seen &= ~held;
            s.pending |= held;
            s.fell |= held;
            s.blocked_ms = 0;
            s.fallback = true;
            s.ref_ok = false;
            if (s.aim_falls < 0xFF) ++s.aim_falls;
            ++s.fallbacks;
            o.event = FpvEvent::fallback;
            o.reason = "blocked";
            o.clear = s.pending;
            return o;
        }
    } else {
        s.blocked_ms = 0;
    }
    if (!in.ready) return FpvRetry(s, o);
    const int bit = in.slot_ok ? FpvBit(in.slot) : -1;
    if (bit < 0) return FpvRetry(s, o);
    const std::uint32_t b = 1u << bit;
    const char* set_reason = "ready";
    if (s.fallback) {
        // The shoulder view after a fall: the first look angles read with the bit clear are the reference; set again only
        // once the aim has turned up or sideways from it, and not after kFpvFallbacksPerAim falls.
        if ((s.pending & b) || (toggles & b) || !in.ang_ok || !std::isfinite(in.pitch) || !std::isfinite(in.yaw))
            return FpvRetry(s, o);
        if (!s.ref_ok) {
            s.ref_ok = true;
            s.ref_pitch = in.pitch;
            s.ref_yaw = in.yaw;
            return FpvRetry(s, o);
        }
        const bool turned = s.ref_pitch - in.pitch >= kFpvRearmPitch || std::fabs(WrapPi(in.yaw - s.ref_yaw)) >= kFpvRearmYaw;
        if (!turned || s.aim_falls >= kFpvFallbacksPerAim) return FpvRetry(s, o);
        s.fallback = s.ref_ok = false;
        set_reason = "rearm";
    }
    if (s.pending & b) {
        // Our own clear not landed yet and this aim wants the bit again: ours again, no clear.
        s.pending &= ~b;
        if (toggles & b) s.ours |= b;
    }
    s.fell &= ~b;
    if (!(toggles & b)) {
        o.set = b;
        s.ours |= b;
        ++s.sets;
        o.event = FpvEvent::set;
        o.reason = set_reason;
    }
    return FpvRetry(s, o);
}

// --- The camera after a get-off (mode view; design 8.38) ------------------------------------------------------------
// [O] runs 37b94432 / 1f81de65 / 83defaeb: after the stock get-off from seat 1 (and seat 2) the camera is left on the
// driver's side looking across the jeep, and its collision (the handle cleared by the stock get-off) pulls it in close
// to the driver with the player out of view; after seat 3 (299de3e1) it stays behind, looking forward. Mode view arms
// only on an observed get-off from seat 1 or 2 (the body's act kActGetOff 0x70 when the report ends: never on a death,
// a lost report or a latch) and asks once, after the stock get-off let go (the stock clear of the handle, or the
// VehicleCamera seen active and gone; no time fallback) and before kTurnMs, for the stock camera
// rotation of RequestToSetCameraRotation (PlayerCamera vt+0xE0 0x111A920, pinned: unless CAM+0xD38 & 0x1001 or CAM+0x204
// bit 9, CAM+0xD38 |= 2 and CAM+0xD80..0xD94 = {interp, 0.4, 0.8, 0x400126, pitch, yaw}) toward the vehicle's forward
// (the vehicle record's yaw: forward = (sin, 0, cos), the camera's world yaw atan2(fx, fz)), kTurnPitch down (positive
// looks down, like the Lua rotX): the camera swings behind the player along the jeep's side, as after seat 3.
inline constexpr float kTurnPitch = 0.12f, kTurnInterp = 0.4f;
inline constexpr float kTurnSpeed = 0.4f, kTurnBlend = 0.8f;  // the Lua binding's two constants (0x2104860, 0x20C5CB0)
inline constexpr std::uint32_t kTurnFlags = 0x400126;
inline constexpr std::uint64_t kTurnMs = 6000, kTurnCheckMs = 900;
// Design 8.42: an on-foot aim while the turn is pending holds it; after the aim the request waits kTurnSettleMs (and while
// the aim camera module is still the mode), within kTurnAfterAimMs of the aim's end (past kTurnMs if need be); an aim
// still held kTurnAimMaxMs after the get-off drops it.
inline constexpr std::uint64_t kTurnSettleMs = 300, kTurnAfterAimMs = 3000, kTurnAimMaxMs = 20000;
inline constexpr std::size_t kCamRequestFlags = 0xD38, kCamRequest = 0xD80;
inline constexpr std::uint32_t kRequestBusy = 0x1001, kRequestBit = 2, kCamLockBit = 1u << 9;
enum class TurnAct : std::uint8_t { none, request, expire, check, hold };
struct Turn {
    bool pending = false;
    bool seen_stock = false;     // the VehicleCamera was the active module since the boarding ended
    bool stock_clear = false;    // the stock get-off cleared the handle (the release line's reason=stock)
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t seat = kSeatUnknown;
    std::uint64_t since_ms = 0;
    std::uint64_t check_ms = 0;  // a request was made: its check line is due then (0: none)
    float yaw = 0.0f;            // the requested world yaw
    bool aimed = false;          // the player aimed on foot while the turn was pending (design 8.42)
    std::uint64_t aim_end_ms = 0;  // the first tick after that aim (0: aiming, or no aim)
    std::uint32_t requests = 0, expired = 0;
};
// The boarding ended (a get-off, a seat change, a lost report): arm the turn for that vehicle and seat.
inline void TurnArm(Turn& t, std::uint16_t vehicle, std::uint8_t seat, std::uint64_t now_ms) noexcept {
    t.pending = true;
    t.seen_stock = false;
    t.stock_clear = false;
    t.vehicle = vehicle;
    t.seat = seat;
    t.since_ms = now_ms ? now_ms : 1;
    t.aimed = false;
    t.aim_end_ms = 0;
}
// The stock get-off cleared the handle: the get-off ran (the request follows once no VehicleCamera is active).
inline void TurnStockClear(Turn& t) noexcept {
    if (t.pending) t.stock_clear = true;
}
// The mode left view (the lab lever) or the mission latched: a pending turn is dropped. True when one was.
inline bool TurnCancel(Turn& t) noexcept {
    const bool was = t.pending;
    t.pending = false;
    t.check_ms = 0;
    if (was) ++t.expired;
    return was;
}
// One tick. Reasons (static): check, seated, timeout, look, aiming, settle, wait, stock, request. look: the player's own
// camera input this tick (the effective right stick or mouse above kLookInput); aiming: the local gun aiming on foot;
// aim_cam: the aim camera module is the camera's mode (read while settling after an aim). Design 8.41: a look while the
// turn is pending (under the stock get-off or after it, aiming or not) gives the player the camera: the turn is dropped.
// Design 8.42: an aim holds it (hold on the first aiming tick, never a request while aiming); after the aim, the request
// follows kTurnSettleMs later once the aim module let go, with the stock get-off's own conditions, within kTurnAfterAimMs
// of the aim's end. A re-boarding drops it first.
inline TurnAct TurnStep(Turn& t, bool seated, const Camera& cam, std::uint64_t now_ms, const char*& reason, bool look = false,
                        bool aiming = false, bool aim_cam = false) noexcept {
    reason = "none";
    if (t.check_ms && now_ms >= t.check_ms) {
        t.check_ms = 0;
        reason = "check";
        return TurnAct::check;
    }
    if (!t.pending) return TurnAct::none;
    const std::uint64_t age = now_ms >= t.since_ms ? now_ms - t.since_ms : 0;
    if (seated) { t.pending = false; ++t.expired; reason = "seated"; return TurnAct::expire; }
    if (look) { t.pending = false; ++t.expired; reason = "look"; return TurnAct::expire; }
    if (aiming) {
        if (age >= kTurnAimMaxMs) { t.pending = false; ++t.expired; reason = "timeout"; return TurnAct::expire; }
        if (cam.ok && StockModule(cam)) t.seen_stock = true;
        const bool first = !t.aimed;
        t.aimed = true;
        t.aim_end_ms = 0;
        reason = "aiming";
        return first ? TurnAct::hold : TurnAct::none;
    }
    if (t.aimed && !t.aim_end_ms) t.aim_end_ms = now_ms ? now_ms : 1;
    const bool after_aim = t.aimed && now_ms >= t.aim_end_ms && now_ms - t.aim_end_ms < kTurnAfterAimMs;
    if (age >= kTurnMs && !after_aim) { t.pending = false; ++t.expired; reason = "timeout"; return TurnAct::expire; }
    if (t.aimed && (now_ms < t.aim_end_ms || now_ms - t.aim_end_ms < kTurnSettleMs || aim_cam)) {
        if (cam.ok && StockModule(cam)) t.seen_stock = true;
        reason = "settle";
        return TurnAct::none;
    }
    if (!cam.ok) { reason = "wait"; return TurnAct::none; }
    if (StockModule(cam)) { t.seen_stock = true; reason = "stock"; return TurnAct::none; }
    if (!t.seen_stock && !t.stock_clear) { reason = "wait"; return TurnAct::none; }
    reason = "request";
    return TurnAct::request;
}
// The request was made (ok) or refused (its own gate, the vehicle unread): either way the turn is spent.
inline void TurnDone(Turn& t, bool ok, float yaw, std::uint64_t now_ms) noexcept {
    t.pending = false;
    if (ok) {
        ++t.requests;
        t.yaw = yaw;
        t.check_ms = (now_ms ? now_ms : 1) + kTurnCheckMs;
    } else {
        ++t.expired;
    }
}
// The request's own gate (0x111A920): busy while CAM+0xD38 & 0x1001, locked while CAM+0x204 bit 9.
inline const char* RequestGate(std::uint32_t d38, std::uint32_t f204) noexcept {
    if (d38 & kRequestBusy) return "busy";
    if (f204 & kCamLockBit) return "locked";
    return nullptr;
}

} // namespace mgscoop::partner_pass_camera
