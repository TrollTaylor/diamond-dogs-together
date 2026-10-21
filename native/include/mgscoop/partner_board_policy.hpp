#pragma once
// F1 vehicles: the board block and passengers both ways (docs/native-puppet.md
// "Vehicle boarding"; research re-vehicle.md sections 2-4 and 6). The pure
// policy of the two nearest-seat call-site redirects (partner_board), the local
// passenger report and the receiver's seat gates. Lab key partner_ride_board
// (off|block|passenger), default off in lab and play until a hidden-worker FRAME.
//
//   site A 0x12458AC  CALL 0x124B3A0(unit, pos, 0, min(count-1,1))  the ride pre-query 0x1245820 (RideVehicle
//                     CanExec 0x1246650 at 0x1246A4F): 4 = no seat, the pre-query fails, no ride icon
//   site B 0x1251479  CALL 0x124B360(unit, pos)                      phase 1 of the ride action 0x1251200:
//                     4 = the stock "no seat" exit (FinishRideSerial), the race backstop
//
// The driver tick publishes one policy word: the peer's live ride of kind
// vehicle as {vehicle, peer seat, mode, until}. While the peer holds seat 0 of
// the vehicle being searched (vehicle == 0x6400 | u16 unit+0x78), block mode
// returns 4 at both sites (the viewer never becomes a second driver of the copy
// the peer drives: rider[0] of that copy is empty because body 1 is drawn there
// by the DM seat chain). Passenger mode searches only the free back seats
// 2..min(count-1,3) of a light vehicle (GetVehicleType 0x55E7F0 in {1,2}, seat
// count 0xA27CC0 in 3..4), never the peer's seat; trucks, armoured vehicles and
// everything else fall back to block. Stock phase 1 passes a back seat unchanged
// to TryToRide (0x1251832..0x1251840) and selects the passenger phase 0x1250CF0
// (0x1251894..0x125189B), which writes act 0x6E and attaches the viewer to the
// copy RIDE_FAST moves. rider[] is never written by us.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_board {

enum class Mode : std::uint8_t { off = 0, block = 1, passenger = 2 };
inline const char* ModeName(Mode mode) noexcept {
    return mode == Mode::block ? "block" : mode == Mode::passenger ? "passenger" : "off";
}

inline constexpr std::uint8_t kNoSeat = 4;           // both nearest-seat searches' "none"
inline constexpr std::uint16_t kVehicleType = 50;    // GameObject type (id >> 9)
inline constexpr std::uint16_t kVehicleBase = 0x6400;  // vehicle id = 0x6400 | (u16 unit+0x78)
inline constexpr std::uint16_t kVehicleUnknown = 0xFFFF;
inline constexpr std::uint8_t kSeatUnknown = 0xFF;
inline constexpr std::uint32_t kLeaseMs = 5000;      // partner_ride_wire::kLeaseMs
// GetVehicleType (cat+0x1B6): 1, 2 light vehicles; 3, 4 trucks; 5..8 armoured
// (inference re-vehicle.md 1.2; FRAME-checked by the truck half of the scenario).
inline constexpr std::uint8_t kLightFirst = 1, kLightLast = 2;
inline constexpr int kPassengerSeatsMin = 3, kPassengerSeatsMax = 4;
inline constexpr std::uint8_t kBackFirst = 2, kBackLast = 3;
// Record and player offsets (pinned in tools/generate_partner_contract.py).
inline constexpr std::size_t kUnitRiders = 0x30;   // u16 rider[seat], 0xFFFF empty
inline constexpr std::size_t kUnitIndex = 0x78;    // u16 index (id & 0x1FF)
inline constexpr std::size_t kQAct = 0x18;         // u16 (*(Q+0x18))[body]
inline constexpr std::size_t kQVehicle = 0xE0;     // u16 (*(Q+0xE0))[body] (vars.playerVehicleGameObjectId)
inline constexpr std::size_t kPUnits = 0x120;      // (*(P+0x120))[body] = the ridden unit (0x125AD78)
inline constexpr std::uint16_t kActPassenger = 0x6E;  // the passenger phase's act (0x1250EAD)
// A steered phase-1 seat must show up as the local passenger this soon.
inline constexpr std::uint64_t kSteerConfirmMs = 3000;

// --- The published policy (one atomic u64) -----------------------------------
struct Policy {
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t peer_seat = kSeatUnknown;
    Mode mode = Mode::off;
    std::uint32_t until_ms = 0;  // low 32 bits of the driver clock (partner_net::NowMs)
};
inline std::uint64_t Pack(const Policy& p) noexcept {
    return static_cast<std::uint64_t>(p.vehicle) | static_cast<std::uint64_t>(p.peer_seat) << 16 |
           static_cast<std::uint64_t>(static_cast<std::uint8_t>(p.mode)) << 24 | static_cast<std::uint64_t>(p.until_ms) << 32;
}
inline Policy Unpack(std::uint64_t word) noexcept {
    Policy p{};
    p.vehicle = static_cast<std::uint16_t>(word & 0xFFFFu);
    p.peer_seat = static_cast<std::uint8_t>((word >> 16) & 0xFFu);
    const auto mode = static_cast<std::uint8_t>((word >> 24) & 0xFFu);
    p.mode = mode == 1 ? Mode::block : mode == 2 ? Mode::passenger : Mode::off;
    p.until_ms = static_cast<std::uint32_t>(word >> 32);
    return p;
}
// Inside the lease (wrap-safe): until is not in the past and at most kLeaseMs
// ahead (a lease further ahead is a foreign clock and refused).
inline bool LeaseLive(std::uint32_t until_ms, std::uint32_t now_ms) noexcept {
    return static_cast<std::uint32_t>(until_ms - now_ms) <= kLeaseMs;
}

// The driver's publication from its ride store: mode off unless the key is on
// and the peer's ride is live (same link epoch, inside the lease), of kind
// vehicle, in a known seat of a vehicle-type id. The mission-scoped uncertain
// latch degrades passenger to block.
struct PublishIn {
    Mode mode = Mode::off;
    bool uncertain = false;
    bool ride_live = false;
    bool vehicle_kind = false;
    std::uint8_t seat = kSeatUnknown;
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint64_t received_ms = 0;
};
inline Policy PolicyFor(const PublishIn& in) noexcept {
    Policy p{};
    if (in.mode == Mode::off || !in.ride_live || !in.vehicle_kind || in.seat > kBackLast || in.vehicle == kVehicleUnknown ||
        (in.vehicle >> 9) != kVehicleType)
        return p;
    p.vehicle = in.vehicle;
    p.peer_seat = in.seat;
    p.mode = in.mode == Mode::passenger && in.uncertain ? Mode::block : in.mode;
    p.until_ms = static_cast<std::uint32_t>(in.received_ms + kLeaseMs);
    return p;
}

// --- The hook's decision (both sites run the same one) ------------------------
// stock: the original search; block: return 4; seats: passenger mode, ask
// DecideSeats with the unit's type and seat count (the only native calls).
enum class Verdict : std::uint8_t { stock, block, seats };
inline Verdict DecideVehicle(const Policy& p, std::uint32_t now_ms, bool index_ok, std::uint16_t index78, const char*& reason) noexcept {
    if (p.mode == Mode::off) { reason = "off"; return Verdict::stock; }
    if (!LeaseLive(p.until_ms, now_ms)) { reason = "lease"; return Verdict::stock; }
    // Only a peer DRIVER: a peer passenger in seat 2/3 leaves the driver's seat
    // to the viewer (that copy is then driven by the viewer's own stream).
    if (p.peer_seat != 0) { reason = "peer_seat"; return Verdict::stock; }
    if (p.vehicle == kVehicleUnknown || (p.vehicle >> 9) != kVehicleType) { reason = "type"; return Verdict::stock; }
    if (!index_ok) { reason = "index_read"; return Verdict::stock; }
    if (index78 >= 0x200 || static_cast<std::uint16_t>(kVehicleBase | index78) != p.vehicle) { reason = "vehicle"; return Verdict::stock; }
    if (p.mode == Mode::block) { reason = "block"; return Verdict::block; }
    reason = "passenger";
    return Verdict::seats;
}
// The free back seats 2..min(count-1,3) without the peer's seat; false: none.
inline bool BackSeats(int count, std::uint8_t peer_seat, std::uint8_t& first, std::uint8_t& last) noexcept {
    if (count < kPassengerSeatsMin) return false;
    first = kBackFirst;
    last = static_cast<std::uint8_t>(count - 1 < kBackLast ? count - 1 : kBackLast);
    if (peer_seat == first) ++first;
    if (peer_seat == last) --last;
    return first <= last;
}
struct Range {
    bool ok = false;  // false: block
    std::uint8_t first = kNoSeat, last = kNoSeat;
    const char* reason = "unread";
};
inline Range DecideSeats(const Policy& p, bool facts_ok, std::uint8_t type, int count) noexcept {
    Range r{};
    if (!facts_ok) { r.reason = "facts_read"; return r; }
    if (type < kLightFirst || type > kLightLast) { r.reason = "type"; return r; }
    if (count < kPassengerSeatsMin || count > kPassengerSeatsMax) { r.reason = "seats"; return r; }
    if (!BackSeats(count, p.peer_seat, r.first, r.last)) { r.reason = "no_back_seat"; return r; }
    r.ok = true;
    r.reason = "back_seats";
    return r;
}

// --- The local passenger report (sender) --------------------------------------
// Lua reports a passenger as on foot (PlayerStatus.ON_VEHICLE is set only for
// act 0x6F in UpdatePlayerInfo 0x12CE870), so native reads it: the local act is
// 0x6E, the vehicle is 0x6400 | u16 unit+0x78 of (*(P+0x120))[local] confirmed
// by the vehicle lookup (else u16 (*(Q+0xE0))[local]), and the seat is the back
// seat s in {2,3} whose rider[s] holds the local index. via: 1 = P+0x120, 2 = Q+0xE0.
struct LocalPassenger {
    bool valid = false;
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t seat = kSeatUnknown;
    std::uint8_t via = 0;
    const char* reason = "unread";
};
inline bool SameReport(const LocalPassenger& a, const LocalPassenger& b) noexcept {
    return a.valid == b.valid && (!a.valid || (a.vehicle == b.vehicle && a.seat == b.seat));
}
namespace detail {
template <class Reader, class T> bool Get(Reader& r, std::uintptr_t at, T& out) noexcept {
    return at && r(at, &out, sizeof(T));
}
} // namespace detail
// reader(address, out, size) -> bool (SEH or ReadProcessMemory in the game, a
// fixture in tests); lookup(id) -> the live unit record of that vehicle id, 0
// when the id is not a live vehicle (partner_vehicle::ReadLive). Reads only.
template <class Reader, class Lookup>
LocalPassenger ReadLocalPassenger(Reader& r, Lookup& lookup, std::uintptr_t P, std::uintptr_t Q, std::uint32_t local) noexcept {
    using detail::Get;
    LocalPassenger out{};
    if (!P || !Q || local > 1) { out.reason = "state"; return out; }
    std::uintptr_t acts = 0;
    std::uint16_t act = 0;
    if (!Get(r, Q + kQAct, acts) || !Get(r, acts + 2u * local, act)) { out.reason = "act_read"; return out; }
    if (act != kActPassenger) { out.reason = "act"; return out; }
    std::uint16_t id = kVehicleUnknown;
    std::uintptr_t rec = 0;
    std::uintptr_t units = 0, unit = 0;
    std::uint16_t index = 0xFFFF;
    if (Get(r, P + kPUnits, units) && Get(r, units + 8u * local, unit) && unit && Get(r, unit + kUnitIndex, index) && index < 0x200) {
        const auto candidate = static_cast<std::uint16_t>(kVehicleBase | index);
        if (lookup(candidate) == unit) { id = candidate; rec = unit; out.via = 1; }
    }
    if (!rec) {
        std::uintptr_t ids = 0;
        std::uint16_t vid = kVehicleUnknown;
        if (Get(r, Q + kQVehicle, ids) && Get(r, ids + 2u * local, vid) && vid != kVehicleUnknown && (vid >> 9) == kVehicleType) {
            const std::uintptr_t found = lookup(vid);
            if (found) { id = vid; rec = found; out.via = 2; }
        }
    }
    if (!rec) { out.via = 0; out.reason = "vehicle"; return out; }
    std::uint16_t riders[4]{};
    if (!r(rec + kUnitRiders, riders, sizeof(riders))) { out.via = 0; out.reason = "riders_read"; return out; }
    const auto me = static_cast<std::uint16_t>(local & 0x1FFu);
    for (std::uint8_t s = kBackFirst; s <= kBackLast; ++s) {
        if (riders[s] != me) continue;
        out.valid = true;
        out.vehicle = id;
        out.seat = s;
        out.reason = "ok";
        return out;
    }
    out.via = 0;
    out.reason = "rider";
    return out;
}

// --- The receiver's seat gates (partner_driver_seat.inc) ----------------------
// The peer seated by id in the vehicle the viewer drives: only a back seat, only
// in passenger mode (today's "local_vehicle" refusal otherwise).
inline bool LocalVehicleSeatAllowed(Mode mode, std::uint8_t seat) noexcept {
    return mode == Mode::passenger && seat >= kBackFirst && seat <= kBackLast;
}
// The peer's seat is the viewer's own passenger seat: refused natively (Lua's
// shared_seat guard needs ON_VEHICLE and cannot see a passenger).
inline bool SharedWithLocal(const LocalPassenger& lp, std::uint16_t vehicle, std::uint8_t seat) noexcept {
    return lp.valid && lp.vehicle == vehicle && lp.seat == seat;
}
// The 3 m far latch is skipped (passenger mode) for the vehicle the viewer
// occupies: its own driven vehicle (the peer's stream lags the real one) or the
// copy it rides as a passenger (body 1 and the viewer move with that copy).
inline bool FarExempt(Mode mode, std::uint16_t vehicle, std::uint16_t local_vehicle, const LocalPassenger& lp) noexcept {
    if (mode != Mode::passenger || vehicle == kVehicleUnknown) return false;
    return vehicle == local_vehicle || (lp.valid && vehicle == lp.vehicle);
}

// --- The driver's mission-scoped surprise check -------------------------------
// One reason (static text) or nullptr. The driver latches the first reason for
// the mission (partner_ride=board_uncertain) and then publishes block instead
// of passenger.
struct SurpriseIn {
    bool installed = false, live = false;   // the two site patches (PatchLive)
    std::uint32_t new_faults = 0;           // hook read/call faults since the last tick
    bool peer_driver = false;               // a published policy with the peer in seat 0
    std::uint16_t policy_vehicle = kVehicleUnknown;
    std::uint16_t local_vehicle = kVehicleUnknown;  // the vehicle the viewer drives
    bool steer_pending = false;             // a phase-1 back seat not yet seen as the local passenger
    std::uint8_t steer_seat = kSeatUnknown;
    std::uint64_t steer_ms = 0, now_ms = 0;
    LocalPassenger lp;
};
inline const char* SurpriseReason(const SurpriseIn& in) noexcept {
    if (in.installed && !in.live) return "patch_lost";
    if (in.new_faults) return "fault";
    if (in.peer_driver && in.policy_vehicle != kVehicleUnknown && in.local_vehicle == in.policy_vehicle) return "two_drivers";
    if (in.steer_pending) {
        if (in.lp.valid && in.lp.seat != in.steer_seat) return "seat_mismatch";
        if (!in.lp.valid && in.now_ms - in.steer_ms >= kSteerConfirmMs) return "steer_unconfirmed";
    }
    return nullptr;
}

} // namespace mgscoop::partner_board
