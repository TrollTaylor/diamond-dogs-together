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
};
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
