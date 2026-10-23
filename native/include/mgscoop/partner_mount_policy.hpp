#pragma once
// partner_native_mount (docs/mounts.md): the remote's own D-Horse and D-Walker
// acts on the viewer. Pure, portable and deterministic (mgs_coop_core).
//
// The viewer's copy has no GameObject for the partner's mount (one buddy block,
// declared count 1; docs/mounts.md "What the viewer's copy holds"), so the only
// sound picture is no rider at all. The ride wire hides a horse rider (Lua
// reports ON_HORSE as kind 3), but the sender reports a D-Walker rider as on
// foot: PlayerInfo 0x1412CE870 case 0x7D sets no ON_* status that Lua binds
// (only +0xC4 bit 6, status 38, which has no Lua name), and partner_ride.lua
// asks only ON_HELICOPTER / ON_VEHICLE / ON_HORSE. The horse get-on / get-off
// act 122 is reported as foot as well. With partner_native_mount=hide the
// viewer hides body 1 by the remote's act instead, like the heli acts 97..108.
//
// Act census (mgsvtpp.exe 1.0.15.4, word stores to Q+0x18[idx]):
//   121 (0x79) horse ride. Writer 0x1412445F0 (store 0x1412446D5); PlayerInfo
//       case 0x79 sets ON_HORSE (25) and HORSE_IDLE..HORSE_GALLOP (44..47).
//   122 (0x7A) horse get on / off and turns. Ten writers 0x141234561..0x141236798.
//   123 (0x7B) D-Walker get on. Writer 0x141266200 (store 0x141266399).
//   124 (0x7C) no writer in the player action code; kept in the D-Walker range
//       (partner_slots AimBlockedAct already treats 122..126 as mounts).
//   125 (0x7D) D-Walker drive. RideWalkerGearActionImpl::StateDrive 0x141260CC0
//       (store 0x1412617E4).
//   126 (0x7E) D-Walker leave / fall. 0x1412604D0 (store 0x141260617) and
//       0x141260A3B, 0x1412624EB, 0x1412629FA.
#include <cstdint>
#include "mgscoop/partner_ride_wire.hpp"

namespace mgscoop::partner_mount_policy {

enum class Mount : std::uint8_t { none, horse, walker };

inline constexpr std::uint16_t kHorseActFirst = 121, kHorseActLast = 122;
inline constexpr std::uint16_t kWalkerActFirst = 123, kWalkerActLast = 126;

constexpr Mount MountOf(std::uint16_t act) noexcept {
    return act >= kHorseActFirst && act <= kHorseActLast    ? Mount::horse
           : act >= kWalkerActFirst && act <= kWalkerActLast ? Mount::walker
                                                             : Mount::none;
}

constexpr const char* MountName(Mount mount) noexcept {
    return mount == Mount::horse ? "horse" : mount == Mount::walker ? "walker" : "none";
}

// The viewer hides body 1 for this remote act: the key is hide, the remote's
// act is valid (fresh animation sample) and it is a horse or D-Walker act.
constexpr bool Hide(bool hide_mode, bool act_valid, std::uint16_t act) noexcept {
    return hide_mode && act_valid && MountOf(act) != Mount::none;
}

// One decision line per episode (the driver logs from its tick):
// partner_mount=hide when the act rule starts hiding, partner_mount=end when it
// stops. A change between horse and walker inside one episode is not an edge.
struct Episode {
    bool active{};
    Mount mount{};
    std::uint16_t act{};
    std::uint64_t since_ms{};
    unsigned episodes{};
    bool unrealize_logged{};  // this episode's partner_mount=unrealize line
};
enum class Edge : std::uint8_t { none, begin, end };

// Clearing the native realize bits alone does not take body 1 off the screen:
// Lua's all-instance-active mode (Player.SetAllInstanceActiveMode, P+0x204 bit
// 0x2000) keeps instance 1 realized, and the body stood idle at the get-on spot
// for the whole ride (hidden runs b264ad58 / 251abcef, horse and D-Walker). While
// a mount episode hides body 1 and Lua still reports all-active, the hidden reply
// carries CLEAR_ALLACTIVE (Lua sets the mode off: instance 1 unrealized, the stock
// single-player state); the show path's stage policy sends SET_ALLACTIVE again.
// G5 (partner_native_unrealize_all) applies the same rule to a RideEpisode.
constexpr bool Unrealize(bool episode_active, bool hidden, bool lua_all_active) noexcept {
    return episode_active && hidden && lua_all_active;
}

// Whether the episode holds this tick: the act rule hid (rule), or an open
// episode is still hidden and this tick's act sample is not valid (a missing
// sample proves nothing; only a valid non-mount act or a shown body ends it).
constexpr bool Holds(bool rule, bool episode_active, bool hidden, bool act_valid) noexcept {
    return rule || (episode_active && hidden && !act_valid);
}

inline Edge Step(Episode& e, bool hiding, std::uint16_t act, std::uint64_t now_ms) noexcept {
    if (hiding && !e.active) {
        e.active = true;
        e.mount = MountOf(act);
        e.act = act;
        e.since_ms = now_ms;
        e.unrealize_logged = false;
        ++e.episodes;
        return Edge::begin;
    }
    if (!hiding && e.active) {
        e.active = false;
        return Edge::end;
    }
    return Edge::none;
}

// The episode's length so far (0 for a clock that went backwards).
constexpr std::uint64_t EpisodeMs(const Episode& e, std::uint64_t now_ms) noexcept {
    return now_ms >= e.since_ms ? now_ms - e.since_ms : 0;
}

// --- G5: the other ride hides (partner_native_unrealize_all, docs/native-puppet.md "Ride hides") ---
// The heli acts 97..108 or a heli ride, a vehicle ride drawn hidden (partner_native_ride=hide, or seat mode
// refused / unavailable for this vehicle and seat: the seat fallback hide) and a horse rider by its ride kind
// 3 (partner_native_mount off) clear only the native realize bits as well, so the body stays drawn as a frozen
// standing body at the get-on spot while Lua's all-active mode is on (filmed for the horse kind-3 hide in
// 251abcef). With the key they unrealize like a mount episode: CLEAR_ALLACTIVE while Lua reports all-active,
// one partner_ride=unrealize line per episode; the show path's SET_ALLACTIVE realizes the body again.
enum class Ride : std::uint8_t { none, heli, vehicle, horse, other };

constexpr bool HeliAct(bool act_valid, std::uint16_t act) noexcept {
    return act_valid && act >= partner_ride_wire::kHeliActFirst && act <= partner_ride_wire::kHeliActLast;
}

// The ride this tick hides for: the remote's heli act first (as partner_ride_wire::Observe), then the live
// ride kind (a stale or missing ride state names none).
constexpr Ride RideOf(bool act_valid, std::uint16_t act, bool ride_live, partner_ride_wire::Kind kind) noexcept {
    namespace rw = partner_ride_wire;
    if (HeliAct(act_valid, act)) return Ride::heli;
    if (!ride_live) return Ride::none;
    return kind == rw::Kind::heli      ? Ride::heli
           : kind == rw::Kind::vehicle ? Ride::vehicle
           : kind == rw::Kind::horse   ? Ride::horse
           : kind == rw::Kind::foot    ? Ride::none
                                       : Ride::other;
}

constexpr const char* RideName(Ride ride) noexcept {
    return ride == Ride::heli      ? "heli"
           : ride == Ride::vehicle ? "vehicle"
           : ride == Ride::horse   ? "horse"
           : ride == Ride::other   ? "other"
                                   : "none";
}

// A ride episode starts: the key is on, the visibility hid body 1 as a ride this tick (Visibility reason
// "ride": not air, dead or a stale ride lease), it is a heli / vehicle / horse / other ride, and no mount
// episode owns body 1 (partner_native_mount=hide logs its own partner_mount=unrealize).
constexpr bool RideRule(bool key, Ride ride, bool hidden_as_ride, bool mount_episode) noexcept {
    return key && ride != Ride::none && hidden_as_ride && !mount_episode;
}

// This tick proves the remote is off the ride: a valid act that is no heli act, and the ride wire says foot
// (or carries no ride state at all). A missing act sample or a stale ride lease proves nothing.
constexpr bool OffRide(bool act_valid, std::uint16_t act, bool ride_have, bool ride_live, partner_ride_wire::Kind kind) noexcept {
    return act_valid && !HeliAct(act_valid, act) && (!ride_have || (ride_live && kind == partner_ride_wire::Kind::foot));
}

// The ride episode holds: the rule, or an open episode whose body is still hidden and no tick proved the
// remote off the ride (a shown body ends it: a seated vehicle rider, the stable-foot show).
constexpr bool RideHolds(bool rule, bool episode_active, bool hidden, bool off_ride) noexcept {
    return rule || (episode_active && hidden && !off_ride);
}

struct RideEpisode {
    bool active{};
    Ride ride{};
    std::uint16_t act{};
    std::uint8_t kind{};
    std::uint64_t since_ms{};
    unsigned episodes{};
    bool unrealize_logged{};  // this episode's partner_ride=unrealize line (a CLEAR went out)
};

inline Edge StepRide(RideEpisode& e, bool holding, Ride ride, std::uint16_t act, std::uint8_t kind, std::uint64_t now_ms) noexcept {
    if (holding && !e.active) {
        e.active = true;
        e.ride = ride;
        e.act = act;
        e.kind = kind;
        e.since_ms = now_ms;
        e.unrealize_logged = false;
        ++e.episodes;
        return Edge::begin;
    }
    if (!holding && e.active) {
        e.active = false;
        return Edge::end;
    }
    return Edge::none;
}

constexpr std::uint64_t EpisodeMs(const RideEpisode& e, std::uint64_t now_ms) noexcept {
    return now_ms >= e.since_ms ? now_ms - e.since_ms : 0;
}

// The hidden-ride sample gap (play runs fbde3dcc / 5a044a4d: after each hidden mount episode the native
// drive fell back to clips with partner_native_net; reason=net_quality; max_gap about the ride length): the
// native tick that feeds the link quality does not run while body 1 is hidden, so the ride itself looked
// like one long inter-arrival gap. The seated vehicle rider (partner_native_ride=seat) skips the native tick
// the same way (play run fad26e25: max_gap=13143 and net_quality right after a 13 s seated ride). With the
// key, every tick a ride owns body 1 (hidden as a ride: any ride, the mount hide included; or the seat flow)
// feeds the tracker as excluded (partner_native_net::QualityIn::excluded).
constexpr bool ExcludeFromNetQuality(bool key, bool hidden_as_ride, bool seat_owns) noexcept {
    return key && (hidden_as_ride || seat_owns);
}

} // namespace mgscoop::partner_mount_policy
