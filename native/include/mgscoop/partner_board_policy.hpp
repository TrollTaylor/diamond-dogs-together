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
//
// M6 seat 1 (docs/vehicles-companions-design.md 6.6, E6; lab keys
// partner_ride_front (light vehicle front seat) and partner_ride_cab (truck cab
// seat), default off in lab and play). Stock phase 1 treats seat 1 like the
// driver's seat at two sites: 0x1251834 `cmp al,2; jb` passes seat 0 to
// TryToRide for any seat below 2, and 0x1251894 `cmp al,2; setb` selects the
// driver phase 3 for any seat below 2. The site B wrapper sets both cmp
// immediates to 1 for exactly the phase-1 call it steers to seat 1 and back to
// 2 for every other call (both sites run only after site B in the same call,
// on the same thread), so a stock seat 1 (the driver entering by the right door)
// keeps its stock meaning. The seat matrix 0xB799C0 maps seat 1 to seat 0 while
// rider[1] holds a player id (0xB79A3F); with seat 1 on, its `jne` (0xB79A43) is
// a `jmp` for the process (no stock path seats a player in rider[1]: phase 1
// passes seat 0), so the stock passenger phase places the viewer at the front
// right seat (0x141259B80 -> 0xB7AF90(unit, out, seat)). The passenger phase
// plays GetOn for seat 1 of both classes (stateVehicleGetOnFrontRight,
// stateTruckGetOnFrontRight) and the light loop for a light vehicle; a truck
// gets no loop state from 0x14124B2D0 (E6 reads what the graph does).
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
// M6 seat 1: the options (process constants from lab.ini) and the classes.
struct SeatOpts {
    bool front = false;  // partner_ride_front: seat 1 of a light vehicle
    bool cab = false;    // partner_ride_cab: seat 1 of a truck
};
inline bool SeatOne(const SeatOpts& o) noexcept { return o.front || o.cab; }
inline std::uint8_t PackOpts(const SeatOpts& o) noexcept { return static_cast<std::uint8_t>((o.front ? 1u : 0u) | (o.cab ? 2u : 0u)); }
inline SeatOpts UnpackOpts(std::uint8_t bits) noexcept {
    SeatOpts o{};
    o.front = (bits & 1u) != 0;
    o.cab = (bits & 2u) != 0;
    return o;
}
inline constexpr std::uint8_t kFrontSeat = 1;
inline constexpr std::uint8_t kTruckFirst = 3, kTruckLast = 4;
inline constexpr int kFrontSeatsMin = 2, kCabSeatsMin = 2;
// The two phase-1 `cmp al,imm8` immediates (stock 2; 1 only for a steered seat 1).
inline constexpr std::uint8_t kPhaseOneStock = 2, kPhaseOneSeatOne = 1;

// --- The published policy (one atomic u64) -----------------------------------
struct Policy {
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint8_t peer_seat = kSeatUnknown;
    Mode mode = Mode::off;
    std::uint32_t until_ms = 0;  // low 32 bits of the driver clock (partner_net::NowMs)
    // Review R3: the peer driver's copy draws a seat-1 passenger of that class and its seat 1 is free
    // (its live v4 seat addendum for this ride); the mode byte's bits 6 / 7.
    bool front_ok = false, cab_ok = false;
};
inline std::uint64_t Pack(const Policy& p) noexcept {
    const auto mode = static_cast<std::uint8_t>(static_cast<std::uint8_t>(p.mode) | (p.front_ok ? 0x40u : 0u) | (p.cab_ok ? 0x80u : 0u));
    return static_cast<std::uint64_t>(p.vehicle) | static_cast<std::uint64_t>(p.peer_seat) << 16 |
           static_cast<std::uint64_t>(mode) << 24 | static_cast<std::uint64_t>(p.until_ms) << 32;
}
inline Policy Unpack(std::uint64_t word) noexcept {
    Policy p{};
    p.vehicle = static_cast<std::uint16_t>(word & 0xFFFFu);
    p.peer_seat = static_cast<std::uint8_t>((word >> 16) & 0xFFu);
    const auto byte = static_cast<std::uint8_t>((word >> 24) & 0xFFu);
    const auto mode = static_cast<std::uint8_t>(byte & 0x3Fu);
    p.mode = mode == 1 ? Mode::block : mode == 2 ? Mode::passenger : Mode::off;
    p.front_ok = (byte & 0x40u) != 0;
    p.cab_ok = (byte & 0x80u) != 0;
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
// Review R3: the peer's v4 seat addendum flags (partner_ride_wire::SeatFlag, the same values; a
// static_assert in partner_driver.cpp keeps them equal).
inline constexpr std::uint8_t kSeatFlagFront = 1, kSeatFlagCab = 2, kSeatFlagOneTaken = 4;
struct PublishIn {
    Mode mode = Mode::off;
    bool uncertain = false;
    bool ride_live = false;
    bool vehicle_kind = false;
    std::uint8_t seat = kSeatUnknown;
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint64_t received_ms = 0;
    std::uint8_t seat_flags = 0;  // partner_ride_wire::SeatFlagsFor the peer's stored ride (0: none live)
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
    // Seat 1 next to a peer driver only where the driver's copy draws that seat-1 passenger and has seat 1 free.
    const bool one_free = p.mode == Mode::passenger && in.seat == 0 && !(in.seat_flags & kSeatFlagOneTaken);
    p.front_ok = one_free && (in.seat_flags & kSeatFlagFront) != 0;
    p.cab_ok = one_free && (in.seat_flags & kSeatFlagCab) != 0;
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
inline Range DecideSeats(const Policy& p, bool facts_ok, std::uint8_t type, int count, SeatOpts opts = {}) noexcept {
    Range r{};
    if (!facts_ok) { r.reason = "facts_read"; return r; }
    // M6: the truck cab seat 1 (never a seat index 2 or 3 of a truck: its bed is not a seat). Review R3:
    // only with the driver's handshake (cab_ok), else the truck blocks as without the key.
    if (opts.cab && type >= kTruckFirst && type <= kTruckLast) {
        if (count < kCabSeatsMin || count > kPassengerSeatsMax) { r.reason = "seats"; return r; }
        if (!p.cab_ok) { r.reason = "cab_peer"; return r; }
        if (p.peer_seat == kFrontSeat) { r.reason = "no_seat"; return r; }
        r.ok = true;
        r.first = r.last = kFrontSeat;
        r.reason = "cab_seat";
        return r;
    }
    if (type < kLightFirst || type > kLightLast) { r.reason = "type"; return r; }
    // M6: the light vehicle's front seat 1 joins the back seats (a 2-seat light vehicle offers seat 1 only).
    // Review R3: only with the driver's handshake (front_ok); without it the back seats as without the key.
    if (opts.front && !p.front_ok && count < kPassengerSeatsMin) { r.reason = "front_peer"; return r; }
    if (opts.front && p.front_ok) {
        if (count < kFrontSeatsMin || count > kPassengerSeatsMax) { r.reason = "seats"; return r; }
        std::uint8_t first = kFrontSeat;
        std::uint8_t last = static_cast<std::uint8_t>(count - 1 < kBackLast ? count - 1 : kBackLast);
        if (p.peer_seat == first) ++first;
        if (p.peer_seat == last) --last;
        if (first > last) { r.reason = "no_seat"; return r; }
        r.ok = true;
        r.first = first;
        r.last = last;
        r.reason = first == kFrontSeat ? "front_seat" : "back_seats";
        return r;
    }
    if (count < kPassengerSeatsMin || count > kPassengerSeatsMax) { r.reason = "seats"; return r; }
    if (!BackSeats(count, p.peer_seat, r.first, r.last)) { r.reason = "no_back_seat"; return r; }
    r.ok = true;
    r.reason = "back_seats";
    return r;
}
// M6: the phase-1 immediate the site B wrapper wants for this call: 1 only when
// it steered the viewer to seat 1 with the option installed, else stock 2.
inline std::uint8_t PhaseOneLimit(Verdict verdict, std::uint8_t result, bool seat_one) noexcept {
    return seat_one && verdict == Verdict::seats && result == kFrontSeat ? kPhaseOneSeatOne : kPhaseOneStock;
}
// The dword at a phase-1 site (`3C imm8 ..`, little-endian: byte 1 is the immediate) with `imm`.
inline std::uint32_t PhaseOneDword(std::uint32_t dword, std::uint8_t imm) noexcept {
    return (dword & 0xFFFF00FFu) | (static_cast<std::uint32_t>(imm) << 8);
}

// --- M6 seat races (lab key partner_ride_race; docs/vehicles-companions-design.md 4 rule 3, 5 SEAT) ---
// Review R1/R2. Site C (0x1246A4F: RideVehicle CanExec's call of the ride pre-query 0x1245820, retargeted
// only with the key) records the want: the pre-query's final answer (true with seat 0 or 1, after its door
// bits and line check; stock phase 1 makes both seats the driver's) for a vehicle with no peer-driver
// policy. The world SEAT op (lua/coop/world_ride_pose.lua) claims it for the guest; the host denies a
// claim for a vehicle its own player wants or drives (ties go to the host) and grants the rest, yielding
// that vehicle at once (its own deny, refreshed while the guest keeps claiming). The guest HOLDS (a lease
// Lua refreshes) every such driver's seat it has no live grant for, so it never drives an empty vehicle
// before the host answered: no window of one round trip. Site C answers false for a deny or a hold (no
// ride icon; CanExec returns false); site B answers 4 (phase 1's no-seat exit) as the backstop: a deny for
// any stock driver's seat of that vehicle, a hold only for the vehicle site C wants. Once the winner drives,
// the peer-driver policy decides as before (the loser may still take a passenger seat).
inline constexpr std::uint32_t kRaceDenyMaxMs = 5000;
// Site B holds only a vehicle site C wanted this recently (it sees a want every frame the icon would show).
inline constexpr std::uint32_t kRaceWantFreshMs = 1000;
inline bool RaceWantSeat(Verdict verdict, std::uint8_t result) noexcept { return verdict == Verdict::stock && result <= kFrontSeat; }
inline bool RaceDenied(std::uint16_t deny_vehicle, std::uint32_t deny_until, std::uint32_t now_ms, std::uint16_t vehicle) noexcept {
    return deny_vehicle != kVehicleUnknown && vehicle == deny_vehicle && static_cast<std::uint32_t>(deny_until - now_ms) <= kRaceDenyMaxMs;
}
enum class RaceVerdict : std::uint8_t { pass, deny, hold };
struct RaceIn {
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint32_t now_ms = 0;
    std::uint16_t deny_vehicle = kVehicleUnknown;
    std::uint32_t deny_until = 0;
    bool hold_on = false;  // the guest's hold lease (Lua's "hold <ms>")
    std::uint32_t hold_until = 0;
    std::uint16_t grant_vehicle = kVehicleUnknown;  // the host's grant (Lua's "grant <id> <ms>")
    std::uint32_t grant_until = 0;
};
// A deny first; then a live hold unless this vehicle holds a live grant; else pass.
inline RaceVerdict RaceDecide(const RaceIn& in) noexcept {
    if (RaceDenied(in.deny_vehicle, in.deny_until, in.now_ms, in.vehicle)) return RaceVerdict::deny;
    const bool held = in.hold_on && static_cast<std::uint32_t>(in.hold_until - in.now_ms) <= kRaceDenyMaxMs;
    if (held && !RaceDenied(in.grant_vehicle, in.grant_until, in.now_ms, in.vehicle)) return RaceVerdict::hold;
    return RaceVerdict::pass;
}

// The MGS_COOP_BOARD_SEAT_V1 request (game_script_observer.cpp SelectBoardSeatReceipt; review R8: a pure,
// tested parser). Exactly one of: poll | clear | deny <vehicle> <ms> | grant <vehicle> <ms> | hold <ms>,
// single spaces, decimal digits only, nothing after the last number. vehicle 25600..26111 (a TppVehicle2
// id); deny / grant ms 1..kRaceDenyMaxMs; hold ms 0..kRaceDenyMaxMs (0 clears the hold).
enum class SeatCmd : std::uint8_t { bad, poll, clear, deny, grant, hold };
struct SeatRequest {
    SeatCmd cmd = SeatCmd::bad;
    std::uint16_t vehicle = kVehicleUnknown;
    std::uint32_t ms = 0;
};
namespace detail {
// One decimal number of 1..10 digits from p up to end or the next space (p then points there); false on
// anything else (an empty field, a sign, a non-digit, a value above 0xFFFFFFFF).
inline bool SeatNumber(const char*& p, const char* end, std::uint32_t& out) noexcept {
    std::uint64_t v = 0;
    unsigned digits = 0;
    while (p < end && *p != ' ') {
        if (*p < '0' || *p > '9' || ++digits > 10) return false;
        v = v * 10u + static_cast<unsigned>(*p - '0');
        ++p;
    }
    if (!digits || v > 0xFFFFFFFFull) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}
inline bool SeatWord(const char*& p, const char* end, const char* word) noexcept {
    const char* q = p;
    for (; *word; ++word, ++q)
        if (q >= end || *q != *word) return false;
    p = q;
    return true;
}
} // namespace detail
inline SeatRequest ParseSeatRequest(const char* text, std::size_t n) noexcept {
    SeatRequest r{};
    if (!text || !n) return r;
    const char* p = text;
    const char* end = text + n;
    SeatCmd cmd = SeatCmd::bad;
    if (detail::SeatWord(p, end, "poll")) cmd = SeatCmd::poll;
    else if (detail::SeatWord(p, end, "clear")) cmd = SeatCmd::clear;
    else if (detail::SeatWord(p, end, "deny ")) cmd = SeatCmd::deny;
    else if (detail::SeatWord(p, end, "grant ")) cmd = SeatCmd::grant;
    else if (detail::SeatWord(p, end, "hold ")) cmd = SeatCmd::hold;
    else return r;
    if (cmd == SeatCmd::poll || cmd == SeatCmd::clear) {
        if (p != end) return r;
        r.cmd = cmd;
        return r;
    }
    std::uint32_t a = 0, ms = 0;
    if (!detail::SeatNumber(p, end, a)) return r;
    if (cmd == SeatCmd::hold) {
        if (p != end || a > kRaceDenyMaxMs) return r;
        r.cmd = cmd;
        r.ms = a;
        return r;
    }
    if (p >= end || *p != ' ') return r;
    ++p;
    if (!detail::SeatNumber(p, end, ms) || p != end) return r;
    if (a < kVehicleBase || a > (kVehicleBase | 0x1FFu) || ms < 1 || ms > kRaceDenyMaxMs) return r;
    r.cmd = cmd;
    r.vehicle = static_cast<std::uint16_t>(a);
    r.ms = ms;
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
// seat_one (M6, partner_ride_front / partner_ride_cab installed): seat 1 is a
// passenger seat too (only a steered phase 1 seats a player there).
template <class Reader, class Lookup>
LocalPassenger ReadLocalPassenger(Reader& r, Lookup& lookup, std::uintptr_t P, std::uintptr_t Q, std::uint32_t local,
                                  bool seat_one = false) noexcept {
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
    for (std::uint8_t s = seat_one ? kFrontSeat : kBackFirst; s <= kBackLast; ++s) {
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
// M6: seat 1 too while seat 1 is on (the peer's own board offers it only for its class).
inline bool LocalVehicleSeatAllowed(Mode mode, std::uint8_t seat, bool seat_one = false) noexcept {
    return mode == Mode::passenger && ((seat >= kBackFirst && seat <= kBackLast) || (seat_one && seat == kFrontSeat));
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
