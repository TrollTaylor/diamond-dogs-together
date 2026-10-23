#pragma once
// G4 truck test levers (docs/native-puppet.md "Truck test levers"; lab.ini debug_vehicle_door=1,
// lab only, default 0, play forces it off): the stock door spot of one vehicle for the lab's goto
// levers in lua/coop/debug_vehicle.lua (goto_vehicle truck, goto_partner_vehicle, vehicle_door).
//
// Why: the truck half of coop-vehicle-passenger.txt never boarded (hidden run 59cfc415). The old
// goto_vehicle spot is 1.9 m beside the record's centre, which for a truck is beside the cargo bed.
// Light vehicles (GetVehicleType 1/2) have no door gate, but RideVehicle CanExec 0x1246650 gates
// trucks (types 3/4, 0x1246B13..0x1246CA0; every byte pinned in tools/generate_partner_contract.py
// DOOR_SITES / DOOR_DATA):
//   seat_s, seat_o = the seat world matrices' translations 0xB7AF90(unit, out, s) of the front seat
//                    s the pre-query chose (0 the driver's, 1 the front passenger's) and the other one
//   door = seat_s + 0.4 * unit(seat_s - seat_o)                                   (0x21124C0 = 0.4f)
//   ride icon only if |player - door| < 1.4 (3D, 0x218D51C) and (player - door).xz . (seat_s - seat_o).xz >= 0
// with player = CS vt+0x388(body)+0x20. DoorSpot mirrors that door point and puts the stand spot
// out_cm further out along the door's outward xz axis, facing the seat.
//
// The receipt (lua/coop/debug_vehicle.lua door()):
//   return 'MGS_COOP_VEHICLE_DOOR_V1' -- <id|partner> <seat 0|1> <out_cm>
//   -> return {ok=true,id=..,peer=0|1,pseat=..,type=..,seats=..,rider0=..,x=..,y=..,z=..,face=..,
//              dx=..,dy=..,dz=..,ax=..,az=..,rx=..,ry=..,rz=..}
//      (mm and mrad: x,y,z the stand spot, face the yaw toward the seat, d* the stock door point,
//       a* the outward unit axis in thousandths, r* the record position; peer=1 when the id came from
//       the partner's live ride packet, pseat its seat)
//   -> return {ok=false,reason='..'}
// "partner" takes the vehicle id from the partner's live ride packet (kind vehicle): this copy's
// record of the vehicle the partner rides (the copy RIDE_FAST moves). Lab only: off unless the key is
// named, refused outside mode=lab with offline and redirected saves (the give_item rule), on any
// thread but the mission runtime thread and while the mission terminates. The native calls
// (GetVehicleType 0x55E7F0, the seat count 0xA27CC0, the seat matrix 0xB7AF90) only read; they run
// under SEH with the record's identity bytes and the viewer's own act and ridden unit digested before
// and after; a fault or a change latches the mission (reason=uncertain from then on). Nothing is
// written to game memory; the warp itself is the lab's ordinary TppPlayer.Warp of the local player.
#include <cstddef>
#include <cstdint>

namespace mgscoop::vehicle_door {

inline constexpr char kPrefix[] = "return 'MGS_COOP_VEHICLE_DOOR_V1' -- ";
inline constexpr float kDoorOffsetM = 0.4f;   // 0x21124C0 (static_asserted against the contract)
inline constexpr float kDoorRadiusM = 1.4f;   // 0x218D51C
inline constexpr unsigned kOutMaxCm = 150;
inline constexpr float kStandLiftM = 0.5f;    // the warp height above the record (goto_vehicle's jeep rule)
inline constexpr float kMinAxisM = 0.05f;     // seats closer than this have no usable door axis
inline constexpr std::uint16_t kVehicleType = 50;  // GameObject type (id >> 9)
inline constexpr std::uint16_t kNone = 0xFFFF;
inline constexpr std::uint8_t kSeatNone = 0xFF;

enum class Target : std::uint8_t { id, partner };
struct Request {
    Target target = Target::id;
    std::uint16_t id = kNone;   // Target::id only
    std::uint8_t seat = 0;      // 0 the driver's door, 1 the front passenger's
    std::uint16_t out_cm = 0;
};
// The text after kPrefix: "<id> <seat> <out_cm>" or "partner <seat> <out_cm>" with single spaces;
// id a decimal vehicle-type id (id >> 9 == 50), seat 0 or 1, out_cm 0..kOutMaxCm (no signs, no
// leading zeros beyond a single 0). False on anything else.
bool ParseRequest(const char* text, std::size_t size, Request& out) noexcept;

struct GateIn {
    bool enabled = false;    // lab.ini debug_vehicle_door=1 (play forces it off)
    bool lab = false;        // mode=lab, offline, redirected saves
    bool runtime = false;    // the mission runtime thread, not terminating, the partner mission
    bool pins = false;       // the code gate passed and VerifyImage matched
    bool uncertain = false;  // this mission latched a surprise
};
// nullptr, else "off", "lab_only", "runtime", "pins" or "uncertain" (the first that applies, in that order).
const char* GateRefusal(const GateIn& in) noexcept;

// The partner target: the peer's live ride packet must name a vehicle-type id.
// nullptr (id set), else "partner_ride" (no live vehicle ride).
const char* PartnerTarget(bool ride_live, std::uint16_t vehicle, std::uint16_t& id) noexcept;

struct Facts {
    std::uint16_t id = kNone;
    std::uint8_t type = 0xFF;      // GetVehicleType (1/2 light vehicles, 3/4 trucks)
    int seats = -1;                // the seat count 0xA27CC0
    std::uint16_t rider0 = kNone;  // u16 rider[0] (+0x30), 0xFFFF empty
    float rec[4]{};                // record +0x20 position, +0x2C yaw
    float seat0[3]{}, seat1[3]{};  // the seat world matrices' translations (out+0x30) of seats 0 and 1
};
struct Spot {
    const char* refusal = "unset";  // nullptr: valid
    float door[3]{};   // the stock door point of the requested seat
    float axis[2]{};   // the door's outward unit axis (xz)
    float stand[3]{};  // door.xz + out_cm along axis; y = the record's height + kStandLiftM
    float face = 0;    // radians, atan2(seat.x - stand.x, seat.z - stand.z) (TppPlayer.Warp's rotY sense)
};
// The stock door point (CanExec 0x1246B13) of `seat` (0 or 1) and the stand spot out_cm outside it.
// Refusals: "seat" (not 0/1), "out" (out_cm above kOutMaxCm), "nonfinite", "seat_axis" (the two seats
// closer than kMinAxisM in 3D or xz; the stock code would take its NaN fallback there).
Spot DoorSpot(const Facts& facts, unsigned seat, unsigned out_cm) noexcept;

struct Answer {
    const char* refusal = nullptr;  // non-null: {ok=false,reason=..}
    bool peer = false;
    std::uint8_t peer_seat = kSeatNone;
    const Facts* facts = nullptr;
    const Spot* spot = nullptr;
};
// The Lua reply (see above). Returns the length, 0 when it does not fit.
std::size_t FormatAnswer(const Answer& answer, char* out, std::size_t capacity) noexcept;
// One log line: "partner_vehicle_door=answer; target=id|partner; id=..; seat=..; out_cm=..; type=..; seats=..;
// rider0=..; pseat=..; rec_mm=x,y,z; yaw_mrad=..; seat0_mm=x,y,z; seat1_mm=x,y,z; door_mm=x,y,z; stand_mm=x,y,z;
// face_mrad=..\r\n" or "partner_vehicle_door=refused; target=..; id=..; seat=..; reason=..\r\n".
std::size_t FormatLog(const Request& request, const Answer& answer, char* out, std::size_t capacity) noexcept;

// The record identity bytes and the viewer's own state compared around the native calls.
struct Digest {
    std::uint16_t riders[4]{};
    std::uint8_t type6c = 0, realized = 0, slot = 0;
    std::uint16_t index = 0;
    std::uint16_t viewer_act = 0;
    std::uintptr_t viewer_unit = 0;
};
bool SameDigest(const Digest& a, const Digest& b) noexcept;

#ifdef _WIN32
// The door blocks and data words (contract door_blocks / door_data) in a running image: nullptr,
// else "base", "contract", "span", "block" or "data".
const char* VerifyImage(const void* image, std::size_t span) noexcept;
// Reads this copy's record of `id` (partner_vehicle::ReadLive), rider[0], and calls GetVehicleType,
// the seat count and 0xB7AF90 for seats 0 and 1 under SEH; viewer P / Q / local name the viewer's
// own act and ridden unit for the digest. nullptr on success, else a static reason: a
// partner_vehicle refusal ("type", "live", ...), "alignment", "read", "fault" or "changed" (the last
// two are surprises: the caller latches the mission).
const char* ReadFacts(const void* image, std::uint16_t id, std::uintptr_t P, std::uintptr_t Q, std::uint32_t local,
                      Facts& out) noexcept;
#endif

} // namespace mgscoop::vehicle_door
