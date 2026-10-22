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
};
enum class Edge : std::uint8_t { none, begin, end };

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

} // namespace mgscoop::partner_mount_policy
