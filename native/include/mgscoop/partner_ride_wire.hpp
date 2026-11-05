#pragma once
// Channel 2 ride state. The authenticated link session owns epoch rejection;
// this 12-byte append-only body intentionally carries no mission/native token.
// Version 1: magic, 1, kind, 0, revision (LE16), 0, 0, sender_ms (LE32).
// Version 2 (docs/anim-round1-seat.md): byte 3 the sender's native seat
// (0..3, kSeatUnknown) and bytes 6..7 its vehicle GameObject id (LE16,
// kVehicleUnknown). Sent only for kind=vehicle with a known seat or vehicle;
// every other state keeps version 1, so an old receiver only misses the seat.
// Decode accepts both versions strictly (v1 reserved bytes stay zero; a v2
// non-vehicle kind carries no seat and no vehicle).
#include <cstddef>
#include <cstdint>
namespace mgscoop::partner_ride_wire {
inline constexpr std::uint8_t kChannel=2, kMagic=0x52, kVersion=2, kVersion1=1;
inline constexpr std::size_t kSize=12;
inline constexpr std::uint8_t kSeatUnknown=0xFF, kSeatMax=3;
inline constexpr std::uint16_t kVehicleUnknown=0xFFFF;
// The vehicle GameObject type (id >> 9): the DM anchor 0x1198770 seats only this type.
inline constexpr std::uint16_t kVehicleType=50;
// Exact current-build heli handler census: move-design.md section2, including
// immediate writers98/99/101/103/105/106/107/108 and observed flight100/102,
// plus act 97 (boarding the support heli, critic 4.10: outside the old range,
// the partner stood beside an unmirrored heli and then vanished).
inline constexpr std::uint16_t kHeliActFirst=97, kHeliActLast=108;
inline constexpr std::uint64_t kHeartbeatMs=2000, kLeaseMs=5000;
// The air rule's candidate (body falling right after our placement while the
// remote stands) must hold this long: a Warp's one-to-few-frame act 25 after a
// teleport placement is not a fall (action-anims 2.3; every air hide in the
// snapshot was one).
inline constexpr std::uint64_t kAirHoldMs=250;
enum class Kind : std::uint8_t { foot, heli, vehicle, horse, walker, other };
struct Packet {
    Kind kind{}; std::uint16_t revision{}; std::uint32_t sender_ms{};
    std::uint8_t seat=kSeatUnknown; std::uint16_t vehicle=kVehicleUnknown;
    std::uint8_t version=kVersion1;  // decoded version (Encode chooses its own)
};
bool IsRide(const std::uint8_t*, std::size_t) noexcept;
// The version Encode writes: 2 for a vehicle with a known seat or vehicle id.
std::uint8_t VersionFor(const Packet&) noexcept;
bool Encode(const Packet&, std::uint8_t (&)[kSize]) noexcept;
bool Decode(const std::uint8_t*, std::size_t, Packet&) noexcept;
// Whether two packets carry the same ride state (kind, seat, vehicle).
inline bool SameState(const Packet& a, const Packet& b) noexcept {
    return a.kind==b.kind && a.seat==b.seat && a.vehicle==b.vehicle;
}
struct Store { bool have{}; Packet packet{}; std::uint32_t epoch{}; std::uint64_t received_ms{}; unsigned received{}, invalid{}, stale{}, duplicates{}; };
bool Accept(Store&, const std::uint8_t*, std::size_t, std::uint32_t epoch, std::uint64_t now) noexcept;
bool Live(const Store&, std::uint32_t epoch, std::uint64_t now) noexcept;
// A seat the seated visibility can use: a known native seat in a vehicle-type id.
inline bool SeatUsable(std::uint8_t seat, std::uint16_t vehicle) noexcept {
    return seat<=kSeatMax && vehicle!=kVehicleUnknown && (vehicle>>9)==kVehicleType;
}
// Pure hide/re-show policy. Repeated/held sender samples cannot prove landing.
struct Visibility {
    bool hidden{}, air{}, dead_retry{}, dead_locked{}, have_sample{}, stable{};
    // seat mode: the remote rides a usable seat of a vehicle (drawn seated, not hidden).
    bool seated{};
    // partner_native_mount=hide: this observation hid by the remote's horse /
    // D-Walker act (partner_mount_policy), whatever the ride kind says.
    bool mount{};
    // world_horse_share (horse plan M1): this observation shows body 1 as a horse rider on this copy's placed
    // shared D-Horse (In::horse_shown and the remote's act 121, or 122 inside a shown ride), not hidden.
    bool horse{};
    // The air candidate has held since air_since_ms (air_pending).
    bool air_pending{};
    std::uint64_t air_since_ms{};
    std::int64_t sender_ms{}, stable_since{};
    std::uint32_t generation{};
    double x{}, y{}, z{};
    std::uint64_t place_ms{}, retry_ms{};
    const char* reason{};
};
struct In {
    bool fresh{}, time_valid{}, jump{}, anim_valid{}, body_anim_valid{}, body_valid{}, ride_have{}, ride_live{}, lease_live{}, rearmed{};
    Kind kind{}; std::uint16_t remote_act{}, body_act{};
    std::uint32_t generation{};
    std::int64_t sender_ms{}; double x{}, y{}, z{}, body_y{}; std::uint64_t now_ms{};
    // partner_native_ride=seat in a seat-capable mission (the driver decides);
    // the live ride's seat and vehicle (kSeatUnknown / kVehicleUnknown otherwise).
    bool seat_mode{};
    std::uint8_t seat=kSeatUnknown; std::uint16_t vehicle=kVehicleUnknown;
    // partner_native_mount=hide (docs/mounts.md): a remote horse / D-Walker act
    // 121..126 hides like a ride even when the sender reports foot.
    bool mount_hide{};
    // world_horse_share (horse plan M1): this copy placed the shared D-Horse from the rider's stream under
    // horse_drive::kFreshMs ago. A horse ride act then shows body 1 (Visibility::horse) instead of hiding it.
    bool horse_shown{};
};
// The remote rides the shared D-Horse in view (pure): on a fresh placed horse (horse_shown) the ride wire's live
// ON_HORSE (ride_horse) or the ride act 121; a missing act sample keeps a shown ride (it proves nothing: run 3d1439e6
// lost the sample once body 1 was unrealized); 122 (the get-off, turns) holds a shown ride, also after the final pose
// (the seat overlay then ends with the dismount's foot act and its own stop, never by a hide).
constexpr bool HorseRider(bool horse_shown, bool act_valid, std::uint16_t act, bool shown_before, bool ride_horse) noexcept {
    return (horse_shown && (ride_horse || (act_valid && act == 121) || (!act_valid && shown_before))) ||
           (act_valid && act == 122 && shown_before);
}
void Observe(Visibility&, const In&) noexcept;
void Placed(Visibility&, std::uint64_t now) noexcept;

// Version 3 addendum (WS3 vehicles, docs/vehicle-ws3.md): 16 bytes, the same
// magic, sent NEXT TO the v1/v2 packet (never instead of it) and only while a
// vehicle lab key (partner_ride_seat_id / partner_ride_tilt) is on. An older
// receiver drops it as an invalid ride packet (its size and version are not
// v1/v2) and keeps its stored v1/v2 state.
//   [0] 0x52 [1] 3 [2] features (ExtFeature) [3] category (vehicle unit+0x6C >> 4, kCategoryUnknown)
//   [4..5] the v1/v2 ride revision it describes (LE16, never 0) [6..7] vehicle id (LE16, kVehicleUnknown)
//   [8] seat (0..3, kSeatUnknown) [9] type (unit+0x6C, 0xFF unknown) [10..11] 0 [12..15] sender_ms (LE32)
inline constexpr std::uint8_t kVersionExt = 3;
inline constexpr std::size_t kExtSize = 16;
inline constexpr std::uint8_t kCategoryUnknown = 0xFF, kTypeUnknown = 0xFF;
namespace ExtFeature {
inline constexpr std::uint8_t SeatId = 1 << 0;  // the sender offers its vehicle category (seat by id)
inline constexpr std::uint8_t Tilt = 1 << 1;    // the sender sends and accepts RIDE_TILT (world op 0x2D)
inline constexpr std::uint8_t Known = 0x03;
}
inline constexpr std::uint64_t kExtHeartbeatMs = 2000;
struct Ext {
    std::uint8_t features{}, category=kCategoryUnknown, seat=kSeatUnknown, type=kTypeUnknown;
    std::uint16_t revision{}, vehicle=kVehicleUnknown;
    std::uint32_t sender_ms{};
};
inline bool IsExt(const std::uint8_t* b, std::size_t n) noexcept { return b && n==kExtSize && b[0]==kMagic && b[1]==kVersionExt; }
bool EncodeExt(const Ext&, std::uint8_t (&)[kExtSize]) noexcept;
bool DecodeExt(const std::uint8_t*, std::size_t, Ext&) noexcept;
inline bool SameExt(const Ext& a, const Ext& b) noexcept {
    return a.features==b.features && a.category==b.category && a.seat==b.seat && a.type==b.type && a.revision==b.revision && a.vehicle==b.vehicle;
}
struct ExtStore { bool have{}; Ext ext{}; std::uint32_t epoch{}; std::uint64_t received_ms{}; unsigned received{}, invalid{}, stale{}; };
// Newer sender_ms of the same epoch (or a new epoch) replaces the stored addendum.
bool AcceptExt(ExtStore&, const std::uint8_t*, std::size_t, std::uint32_t epoch, std::uint64_t now) noexcept;
bool ExtLive(const ExtStore&, std::uint32_t epoch, std::uint64_t now) noexcept;
// The remote's vehicle category for the stored ride packet: kCategoryUnknown
// unless a live addendum describes that exact revision, vehicle and seat.
std::uint8_t RemoteCategory(const ExtStore&, const Packet& ride, std::uint32_t epoch, std::uint64_t now) noexcept;

// Version 4 seat addendum (M6 seat 1, review R3; docs/vehicles-companions-design.md 8.21): 12 bytes, the
// same magic, sent NEXT TO the v1/v2 packet (never instead of it) and only while partner_ride_front or
// partner_ride_cab is on with partner_ride_board=passenger. It tells the peer whether this copy draws a
// seat-1 passenger (its receiver gate accepts seat 1) and, while this copy's player drives, whether seat 1
// of this copy's vehicle is taken (its D-Dog rides there, a soldier). The peer's board offers seat 1 only
// with a live addendum that describes the ride it sees, accepts that class and has seat 1 free. An older
// receiver (0.14.4 and the v3 builds) drops it as an invalid ride packet (version 4; one
// partner_ride=refused; reason=packet_or_session line) and keeps its stored state.
//   [0] 0x52 [1] 4 [2] flags (SeatFlag) [3] 0 [4..5] the v1/v2 ride revision it describes (LE16, never 0)
//   [6..7] the vehicle this copy's player drives (LE16, kVehicleUnknown otherwise) [8..11] sender_ms (LE32)
inline constexpr std::uint8_t kVersionSeat = 4;
inline constexpr std::size_t kSeatExtSize = 12;
namespace SeatFlag {
inline constexpr std::uint8_t Front = 1 << 0;     // draws a light vehicle's seat-1 passenger (partner_ride_front)
inline constexpr std::uint8_t Cab = 1 << 1;       // draws a truck cab's seat-1 passenger (partner_ride_cab)
inline constexpr std::uint8_t OneTaken = 1 << 2;  // rider[1] of this copy's `vehicle` is not empty (needs a vehicle)
inline constexpr std::uint8_t Known = 0x07;
}
struct SeatExt {
    std::uint8_t flags{};
    std::uint16_t revision{}, vehicle=kVehicleUnknown;
    std::uint32_t sender_ms{};
};
inline bool IsSeatExt(const std::uint8_t* b, std::size_t n) noexcept { return b && n==kSeatExtSize && b[0]==kMagic && b[1]==kVersionSeat; }
inline bool SameSeatExt(const SeatExt& a, const SeatExt& b) noexcept { return a.flags==b.flags && a.revision==b.revision && a.vehicle==b.vehicle; }
bool EncodeSeatExt(const SeatExt&, std::uint8_t (&)[kSeatExtSize]) noexcept;
bool DecodeSeatExt(const std::uint8_t*, std::size_t, SeatExt&) noexcept;
struct SeatExtStore { bool have{}; SeatExt ext{}; std::uint32_t epoch{}; std::uint64_t received_ms{}; unsigned received{}, invalid{}, stale{}; };
// Newer sender_ms of the same epoch (or a new epoch) replaces the stored seat addendum.
bool AcceptSeatExt(SeatExtStore&, const std::uint8_t*, std::size_t, std::uint32_t epoch, std::uint64_t now) noexcept;
// The flags for the stored ride packet: 0 unless a live seat addendum (same epoch, inside the lease)
// describes that exact revision and vehicle of a vehicle ride.
std::uint8_t SeatFlagsFor(const SeatExtStore&, const Packet& ride, std::uint32_t epoch, std::uint64_t now) noexcept;

// Version 5 steer addendum (r1101 vanim, docs/vehicle-ws3.md "Driver pose"; lab.ini partner_ride_steer, follows the
// gun profile since the r1101 Prove phase): 16 bytes, the same magic, sent NEXT TO the v1/v2 packet only while the sender's own player drives (seat 0)
// with the key on. It carries the three ride blends the stock driver phase (0x124E140 sub-phase 2, 0x124FC37) writes
// into the driver's rate record every frame: +0x34 steer (0.5 straight, 0 full left, 1 full right), +0x30 accel and
// +0x08 aux (the speed blend), each 0..1 in 1/250 steps. The viewer writes them into body 1's rate record while it
// seats the peer as the driver: body 1 never runs RideVehicleAction, so its record keeps the constructor's 0.0 steer
// (full left) without them. An older receiver drops it as an invalid ride packet (version 5; one
// partner_ride=refused; reason=packet_or_session line) and keeps its stored state.
//   [0] 0x52 [1] 5 [2] seat (0..3) [3] 0 [4..5] the v1/v2 ride revision it describes (LE16, never 0)
//   [6..7] vehicle id (LE16, type 50) [8] steer [9] accel [10] aux (0..250) [11] 0 [12..15] sender_ms (LE32)
inline constexpr std::uint8_t kVersionSteer = 5;
inline constexpr std::size_t kSteerSize = 16;
inline constexpr std::uint8_t kSteerUnits = 250, kSteerNeutral = 125;
// At most one steer packet per kSteerMinMs; a change of kSteerStep units in any blend (or a new ride) is sent at
// once, else a heartbeat every kSteerHeartbeatMs; the viewer uses a packet for kSteerLeaseMs.
// Review vanim-4: 100 ms (at most 10 Hz): the addendum is appended to reliable channel 2, whose 32-message window it
// shares with the carry events (no retry), the 'V' receipts, ride revisions, gestures, caps and the manifest.
inline constexpr std::uint64_t kSteerMinMs = 100, kSteerHeartbeatMs = 500, kSteerLeaseMs = 1500;
inline constexpr std::uint8_t kSteerStep = 2;
// Review vanim-4: a due packet is held back (latest wins: the next due tick sends the newest) while this many messages
// or more wait unacknowledged on the shared reliable channel 2 (its window is 32): an ack stall never fills the window
// with steer packets.
inline constexpr unsigned kSteerQueueMax = 8;
struct Steer {
    std::uint8_t seat=kSeatUnknown, steer=kSteerNeutral, accel{}, aux{};
    std::uint16_t revision{}, vehicle=kVehicleUnknown;
    std::uint32_t sender_ms{};
};
inline bool IsSteer(const std::uint8_t* b, std::size_t n) noexcept { return b && n==kSteerSize && b[0]==kMagic && b[1]==kVersionSteer; }
bool EncodeSteer(const Steer&, std::uint8_t (&)[kSteerSize]) noexcept;
bool DecodeSteer(const std::uint8_t*, std::size_t, Steer&) noexcept;
inline bool SameSteer(const Steer& a, const Steer& b) noexcept {
    return a.seat==b.seat && a.steer==b.steer && a.accel==b.accel && a.aux==b.aux && a.revision==b.revision && a.vehicle==b.vehicle;
}
struct SteerStore { bool have{}; Steer steer{}; std::uint32_t epoch{}; std::uint64_t received_ms{}; unsigned received{}, invalid{}, stale{}; };
// Newer sender_ms of the same epoch (or a new epoch) replaces the stored steer.
bool AcceptSteer(SteerStore&, const std::uint8_t*, std::size_t, std::uint32_t epoch, std::uint64_t now) noexcept;
// The stored steer for this ride: true only for a packet of the same epoch inside kSteerLeaseMs that describes the
// ride's exact revision, vehicle and seat of a vehicle ride.
bool SteerFor(const SteerStore&, const Packet& ride, std::uint32_t epoch, std::uint64_t now, Steer& out) noexcept;
// A blend (0..1, clamped) in 1/250 units; false for a non-finite value. SteerValue is the inverse.
bool SteerUnitsOf(float value, std::uint8_t& out) noexcept;
float SteerValue(std::uint8_t units) noexcept;
// The sender's rule: a first packet, a new ride (revision, vehicle, seat), or a blend moved by kSteerStep units is due
// once kSteerMinMs passed since the last send; anything else after kSteerHeartbeatMs.
bool SteerSendDue(bool have_last, const Steer& last, std::uint64_t last_ms, const Steer& want, std::uint64_t now) noexcept;
// The viewer's plan for body 1's rate record: write only while the key is on and body 1 is seated (the seat episode's
// loop confirmed) as the driver (seat 0) of `vehicle`; the remote's blends when a live packet describes that vehicle and
// seat, else neutral (steer 0.5, accel 0, aux 0: never the constructor's full left). Otherwise no write (the caller puts
// back what it saved before its first write).
struct SteerPlanIn {
    bool on{}, seated{};
    std::uint8_t seat=kSeatUnknown;
    std::uint16_t vehicle=kVehicleUnknown;
    bool have_remote{};
    Steer remote{};
};
struct SteerPlan {
    bool write{};
    float steer{}, accel{}, aux{};
    const char* source = "none";  // "remote" | "neutral" | "none"
};
SteerPlan PlanSteer(const SteerPlanIn&) noexcept;

// What the existing partner transform stream can drive for a mount.
// That stream is the other player's position, heading and optional animation.
// It has no horse-body transform, no vehicle-body transform and no walker arm.
// rider_anchor is only the player transform already applied to the partner
// body. It does not move a horse, write a mounted animation, invent a vehicle
// pose, or arm a walker. (The seated vehicle rider of partner_native_ride=seat
// is a direct motion anchored to the receiver's own vehicle copy, not this stream.)
struct StreamPose {
    bool valid{};
    bool anim_valid{};
    std::uint16_t act{};
};
struct MountFollow {
    bool rider_anchor{};
    bool horse_entity{};
    bool mounted_animation{};
    bool vehicle_pose{};
    bool walker_arm{};
};
inline MountFollow FollowStream(Kind kind, StreamPose pose) noexcept {
    MountFollow out{};
    if (kind == Kind::horse && pose.valid) {
        out.rider_anchor = true;
        // Acts 1..6 are on-foot locomotion. Any other act is unverified.
        // Neither is a mounted-animation write.
        if (pose.anim_valid) out.mounted_animation = false;
    }
    if (kind == Kind::vehicle && pose.valid) out.vehicle_pose = false;
    if (kind == Kind::walker && pose.valid) out.walker_arm = false;
    return out;
}
}
