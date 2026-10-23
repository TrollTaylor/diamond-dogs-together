// partner_mount_policy (docs/mounts.md): the remote's horse / D-Walker acts,
// the hide decision, the episode lines, and the ride visibility with
// partner_native_mount=hide (a D-Walker rider the sender reports on foot).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "mgscoop/partner_mount_policy.hpp"
#include "mgscoop/partner_ride_wire.hpp"

namespace {
namespace pm = mgscoop::partner_mount_policy;
namespace rw = mgscoop::partner_ride_wire;
unsigned checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "failed check %u: %s\n", checks, message);
        std::exit(1);
    }
}
bool Named(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

void TestActs() {
    // The act census: 121/122 horse, 123..126 D-Walker, nothing else.
    for (unsigned act = 0; act <= 0xFFFF; ++act) {
        const auto mount = pm::MountOf(static_cast<std::uint16_t>(act));
        const auto want = act == 121 || act == 122 ? pm::Mount::horse : act >= 123 && act <= 126 ? pm::Mount::walker : pm::Mount::none;
        if (mount != want) Check(false, "MountOf matches the act census for every act");
    }
    Check(true, "MountOf matches the act census for every act");
    Check(pm::MountOf(120) == pm::Mount::none && pm::MountOf(127) == pm::Mount::none, "the neighbours 120 and 127 are no mount");
    Check(pm::MountOf(111) == pm::Mount::none && pm::MountOf(110) == pm::Mount::none, "vehicle seated / passenger acts are no mount");
    Check(pm::MountOf(97) == pm::Mount::none && pm::MountOf(108) == pm::Mount::none, "heli acts are not mounts (the ride wire hides them)");
    Check(pm::MountOf(139) == pm::Mount::none, "a direct motion is no mount");
    Check(Named(pm::MountName(pm::Mount::horse), "horse") && Named(pm::MountName(pm::Mount::walker), "walker") &&
              Named(pm::MountName(pm::Mount::none), "none"),
          "mount names");
    static_assert(pm::MountOf(125) == pm::Mount::walker, "constexpr: D-Walker drive");
    static_assert(pm::MountOf(121) == pm::Mount::horse, "constexpr: horse ride");
}

void TestHide() {
    Check(pm::Hide(true, true, 125) && pm::Hide(true, true, 121) && pm::Hide(true, true, 122) && pm::Hide(true, true, 123) &&
              pm::Hide(true, true, 126),
          "hide mode, a valid mount act: hide");
    Check(!pm::Hide(false, true, 125) && !pm::Hide(false, true, 121), "off: never by act");
    Check(!pm::Hide(true, false, 125), "an invalid act sample never hides by act");
    Check(!pm::Hide(true, true, 1) && !pm::Hide(true, true, 6) && !pm::Hide(true, true, 25) && !pm::Hide(true, true, 111),
          "foot, fall and vehicle acts never hide by act");
}

void TestEpisode() {
    pm::Episode e{};
    Check(pm::Step(e, false, 1, 100) == pm::Edge::none && !e.active && e.episodes == 0, "nothing to end before an episode");
    Check(pm::Step(e, true, 123, 1000) == pm::Edge::begin && e.active && e.mount == pm::Mount::walker && e.act == 123 && e.since_ms == 1000 &&
              e.episodes == 1,
          "the first hiding tick begins an episode");
    Check(pm::Step(e, true, 125, 1100) == pm::Edge::none && e.act == 123, "the same episode across acts: no edge, first act kept");
    Check(pm::Step(e, true, 121, 1200) == pm::Edge::none && e.mount == pm::Mount::walker, "a change of mount inside an episode is no edge");
    Check(pm::EpisodeMs(e, 3500) == 2500 && pm::EpisodeMs(e, 500) == 0, "episode length (a clock that went back reads 0)");
    Check(pm::Step(e, false, 1, 3500) == pm::Edge::end && !e.active, "the first non-hiding tick ends it");
    Check(pm::Step(e, false, 1, 3600) == pm::Edge::none, "one end line");
    Check(pm::Step(e, true, 121, 4000) == pm::Edge::begin && e.mount == pm::Mount::horse && e.episodes == 2, "a new episode counts");
    // Holds: the rule, or an open hidden episode without a valid act sample.
    Check(pm::Holds(true, false, false, true), "the rule holds");
    Check(pm::Holds(false, true, true, false), "an open hidden episode holds over a missing act sample");
    Check(!pm::Holds(false, true, true, true), "a valid non-mount act ends it although the body is still hidden");
    Check(!pm::Holds(false, true, false, false), "a shown body ends it");
    Check(!pm::Holds(false, false, true, false), "no open episode: nothing holds");
    // The per-episode unrealize line resets when a new episode begins.
    e.unrealize_logged = true;
    Check(pm::Step(e, false, 1, 4100) == pm::Edge::end && e.unrealize_logged, "the end keeps the flag of the episode that ended");
    Check(pm::Step(e, true, 125, 4200) == pm::Edge::begin && !e.unrealize_logged && e.episodes == 3, "a new episode logs its own unrealize");
}

void TestUnrealize() {
    // Hidden runs b264ad58 / 251abcef: the cleared bits left body 1 standing at the get-on spot while Lua's all-active
    // mode stayed on. A hidden mount episode asks Lua to clear the mode; nothing else does.
    Check(pm::Unrealize(true, true, true), "an open, hidden episode with Lua all-active: unrealize");
    Check(!pm::Unrealize(true, true, false), "Lua already reports all-active off: nothing to clear");
    Check(!pm::Unrealize(false, true, true), "no mount episode (a horse kind-3 or heli hide, the far latch): unchanged");
    Check(!pm::Unrealize(true, false, true), "a shown body is never unrealized");
    Check(!pm::Unrealize(false, false, false), "nothing at all");
    static_assert(pm::Unrealize(true, true, true) && !pm::Unrealize(false, true, true), "constexpr");
}

rw::In Sample(std::uint16_t act, std::int64_t sender_ms);

// G5 (partner_native_unrealize_all): the other ride hides (heli, the vehicle drawn hidden, the horse kind-3 hide
// without the mount key) left the same frozen standing body (251abcef, the horse kind-3 hide); with the key they
// open a RideEpisode that unrealizes like a mount episode.
void TestRideOf() {
    // The heli act rule first (as partner_ride_wire::Observe), whatever the ride wire says.
    for (std::uint16_t act = 97; act <= 108; ++act) {
        Check(pm::RideOf(true, act, false, rw::Kind::foot) == pm::Ride::heli && pm::RideOf(true, act, true, rw::Kind::vehicle) == pm::Ride::heli,
              "acts 97..108: heli, with or without a live ride packet");
        Check(pm::RideOf(false, act, false, rw::Kind::foot) == pm::Ride::none, "an invalid act sample is no heli act");
    }
    Check(pm::RideOf(true, 96, false, rw::Kind::foot) == pm::Ride::none && pm::RideOf(true, 109, false, rw::Kind::foot) == pm::Ride::none,
          "the neighbours 96 and 109 are no heli act");
    // Then the live ride kind.
    Check(pm::RideOf(true, 1, true, rw::Kind::heli) == pm::Ride::heli, "a heli ride kind (the seated passenger in flight)");
    Check(pm::RideOf(true, 111, true, rw::Kind::vehicle) == pm::Ride::vehicle, "a vehicle ride");
    Check(pm::RideOf(true, 121, true, rw::Kind::horse) == pm::Ride::horse && pm::RideOf(false, 0, true, rw::Kind::horse) == pm::Ride::horse,
          "a horse ride by kind 3 (the act sample does not matter)");
    Check(pm::RideOf(true, 1, true, rw::Kind::walker) == pm::Ride::other && pm::RideOf(true, 1, true, rw::Kind::other) == pm::Ride::other,
          "the kinds Lua never sends: other");
    Check(pm::RideOf(true, 1, true, rw::Kind::foot) == pm::Ride::none, "foot: none");
    Check(pm::RideOf(true, 111, false, rw::Kind::vehicle) == pm::Ride::none && pm::RideOf(true, 121, false, rw::Kind::horse) == pm::Ride::none,
          "a stale ride lease names no ride (the visibility hides it as ride_stale, never unrealized)");
    Check(Named(pm::RideName(pm::Ride::heli), "heli") && Named(pm::RideName(pm::Ride::vehicle), "vehicle") &&
              Named(pm::RideName(pm::Ride::horse), "horse") && Named(pm::RideName(pm::Ride::other), "other") &&
              Named(pm::RideName(pm::Ride::none), "none"),
          "ride names");
    static_assert(pm::RideOf(true, 102, false, rw::Kind::foot) == pm::Ride::heli && pm::HeliAct(true, 97) && !pm::HeliAct(true, 121),
                  "constexpr: heli acts");
}

void TestRideEpisode() {
    // The rule: key, a ride, hidden as a ride (not air / dead / ride_stale), no mount episode.
    Check(pm::RideRule(true, pm::Ride::vehicle, true, false), "key, a vehicle hidden as a ride: the rule");
    Check(!pm::RideRule(false, pm::Ride::vehicle, true, false) && !pm::RideRule(false, pm::Ride::heli, true, false) &&
              !pm::RideRule(false, pm::Ride::horse, true, false),
          "key off: never (today's bits-only hide)");
    Check(!pm::RideRule(true, pm::Ride::none, true, false), "no ride (a foot sample, a stale lease): never");
    Check(!pm::RideRule(true, pm::Ride::heli, false, false), "not hidden as a ride (shown, air, dead, ride_stale): never");
    Check(!pm::RideRule(true, pm::Ride::horse, true, true), "a mount episode owns body 1 (partner_native_mount=hide logs its own line)");
    // Off the ride: a valid non-heli act and the wire says foot (or has no ride state).
    Check(pm::OffRide(true, 1, true, true, rw::Kind::foot) && pm::OffRide(true, 1, false, false, rw::Kind::foot),
          "a foot act and a foot ride state (or none): off the ride");
    Check(!pm::OffRide(true, 102, true, true, rw::Kind::foot), "a heli act is still the ride (the getting-off acts)");
    Check(!pm::OffRide(false, 1, true, true, rw::Kind::foot), "a missing act sample proves nothing");
    Check(!pm::OffRide(true, 1, true, true, rw::Kind::vehicle) && !pm::OffRide(true, 122, true, true, rw::Kind::horse),
          "a live vehicle / horse ride state is still the ride");
    Check(!pm::OffRide(true, 1, true, false, rw::Kind::foot), "a stale ride lease proves nothing");
    Check(pm::RideHolds(true, false, true, false) && pm::RideHolds(false, true, true, false), "the rule, or an open hidden episode");
    Check(!pm::RideHolds(false, true, true, true), "proved off the ride: ends although the body still waits for the foot proof");
    Check(!pm::RideHolds(false, true, false, false), "a shown body (seated, or the stable-foot show) ends it");
    Check(!pm::RideHolds(false, false, true, false), "no open episode: nothing holds");
    // The episode edges, one line each.
    pm::RideEpisode e{};
    Check(pm::StepRide(e, false, pm::Ride::none, 1, 0, 100) == pm::Edge::none && e.episodes == 0, "nothing before an episode");
    Check(pm::StepRide(e, true, pm::Ride::vehicle, 111, 2, 1000) == pm::Edge::begin && e.active && e.ride == pm::Ride::vehicle && e.act == 111 &&
              e.kind == 2 && e.since_ms == 1000 && e.episodes == 1 && !e.unrealize_logged,
          "the first holding tick begins it");
    e.unrealize_logged = true;
    Check(pm::StepRide(e, true, pm::Ride::heli, 102, 1, 1500) == pm::Edge::none && e.ride == pm::Ride::vehicle && e.unrealize_logged,
          "the same episode: no edge, the first ride kept, one unrealize line");
    Check(pm::EpisodeMs(e, 4000) == 3000 && pm::EpisodeMs(e, 10) == 0, "episode length");
    Check(pm::StepRide(e, false, pm::Ride::none, 1, 0, 4000) == pm::Edge::end && !e.active && e.unrealize_logged,
          "the first non-holding tick ends it (the flag stays for the end line)");
    Check(pm::StepRide(e, false, pm::Ride::none, 1, 0, 4100) == pm::Edge::none, "one end line");
    Check(pm::StepRide(e, true, pm::Ride::horse, 121, 3, 5000) == pm::Edge::begin && e.episodes == 2 && !e.unrealize_logged &&
              e.ride == pm::Ride::horse,
          "a new episode logs its own unrealize");
    // The unrealize decision is the mount episode's rule (Unrealize) on the ride episode.
    Check(pm::Unrealize(e.active, true, true) && !pm::Unrealize(e.active, true, false) && !pm::Unrealize(false, true, true),
          "CLEAR while open, hidden and Lua all-active");
}

// The policy against partner_ride_wire::Observe: every other ride hide is hidden with reason "ride", so the rule
// opens an episode; air and a stale lease never do.
void TestRideVisibility() {
    const auto rule = [](const rw::Visibility& v, const rw::In& in, bool mount_episode) {
        const bool as_ride = v.hidden && Named(v.reason, "ride");
        return pm::RideRule(true, pm::RideOf(in.anim_valid, in.remote_act, in.ride_live, in.kind), as_ride, mount_episode);
    };
    {
        // The heli act at boarding (act 97), no ride packet yet.
        rw::Visibility v{};
        rw::In in = Sample(97, 1000);
        rw::Observe(v, in);
        Check(v.hidden && Named(v.reason, "ride") && rule(v, in, false), "heli boarding: hidden as a ride, the rule opens");
    }
    {
        // A vehicle the seat mode cannot use (partner_native_ride=hide, or a refused / unavailable seat): hidden.
        rw::Visibility v{};
        rw::In in = Sample(111, 1000);
        in.ride_have = in.ride_live = true;
        in.kind = rw::Kind::vehicle;
        in.seat = 0;
        in.vehicle = 25603;
        in.seat_mode = false;
        rw::Observe(v, in);
        Check(v.hidden && !v.seated && rule(v, in, false), "the vehicle fallback hide: the rule opens");
        // Seat mode with a usable seat draws it seated: no episode.
        rw::Visibility s{};
        in.seat_mode = true;
        rw::Observe(s, in);
        Check(!s.hidden && s.seated && !rule(s, in, false), "a seated driver is shown: no episode");
    }
    {
        // The horse rider by kind 3 with partner_native_mount off (the 251abcef frozen body).
        rw::Visibility v{};
        rw::In in = Sample(121, 1000);
        in.ride_have = in.ride_live = true;
        in.kind = rw::Kind::horse;
        rw::Observe(v, in);
        Check(v.hidden && !v.mount && rule(v, in, false), "the horse kind-3 hide: the rule opens");
        Check(!rule(v, in, true), "with partner_native_mount=hide the mount episode owns it");
    }
    {
        // A stale ride lease hides as ride_stale: no episode.
        rw::Visibility v{};
        rw::In in = Sample(1, 1000);
        in.ride_have = true;
        in.ride_live = false;
        in.kind = rw::Kind::vehicle;
        rw::Observe(v, in);
        Check(v.hidden && Named(v.reason, "ride_stale") && !rule(v, in, false), "ride_stale: never unrealized");
    }
}

rw::In Sample(std::uint16_t act, std::int64_t sender_ms) {
    rw::In in{};
    in.fresh = in.time_valid = in.anim_valid = in.body_anim_valid = in.lease_live = true;
    in.remote_act = act;
    in.sender_ms = sender_ms;
    in.now_ms = static_cast<std::uint64_t>(sender_ms);
    in.body_act = 1;
    return in;
}

void TestVisibility() {
    // Today (mount off): a D-Walker rider the sender reports on foot is drawn on foot.
    {
        rw::Visibility v{};
        rw::In in = Sample(125, 1000);
        in.ride_have = in.ride_live = true;
        in.kind = rw::Kind::foot;
        rw::Observe(v, in);
        Check(!v.hidden && !v.mount && v.have_sample, "off: a walker act with kind foot stays a foot sample");
    }
    // Hide: the act hides like a ride, with or without a ride packet.
    for (const bool have : {false, true}) {
        rw::Visibility v{};
        rw::In in = Sample(125, 1000);
        in.mount_hide = true;
        in.ride_have = in.ride_live = have;
        in.kind = rw::Kind::foot;
        rw::Observe(v, in);
        Check(v.hidden && v.mount && Named(v.reason, "ride") && !v.have_sample, "hide: the walker act hides as ride (no foot sample)");
        // The act sample goes missing: stays hidden, the rule does not hold.
        in.anim_valid = false;
        in.sender_ms = in.now_ms = 1100;
        rw::Observe(v, in);
        Check(v.hidden && !v.mount, "a missing act sample keeps the body hidden");
        // Back on foot: shown after 500 ms of calm samples, like any ride.
        in.anim_valid = true;
        in.remote_act = 1;
        for (int i = 0; i <= 5; ++i) {
            in.sender_ms = 2000 + i * 100;
            in.now_ms = static_cast<std::uint64_t>(in.sender_ms);
            rw::Observe(v, in);
            if (i < 5) Check(v.hidden && !v.mount, "foot samples: waiting for the stable proof");
        }
        Check(!v.hidden && v.reason == nullptr && !v.mount, "foot again: shown after 500 ms of calm samples");
    }
    // Every mount act hides; horse acts too (122 is reported on foot by the sender).
    for (std::uint16_t act = 121; act <= 126; ++act) {
        rw::Visibility v{};
        rw::In in = Sample(act, 1000);
        in.mount_hide = true;
        rw::Observe(v, in);
        Check(v.hidden && v.mount, "acts 121..126 hide in hide mode");
    }
    // A horse rider the wire already hides: the act rule also holds (one reason: ride).
    {
        rw::Visibility v{};
        rw::In in = Sample(121, 1000);
        in.mount_hide = true;
        in.ride_have = in.ride_live = true;
        in.kind = rw::Kind::horse;
        rw::Observe(v, in);
        Check(v.hidden && v.mount && Named(v.reason, "ride"), "a horse rider: hidden, reason ride");
        in.mount_hide = false;
        rw::Observe(v, in);
        Check(v.hidden && !v.mount && Named(v.reason, "ride"), "the wire alone still hides the horse rider");
    }
    // A mount act never counts as a usable vehicle seat.
    {
        rw::Visibility v{};
        rw::In in = Sample(125, 1000);
        in.mount_hide = true;
        in.ride_have = in.ride_live = true;
        in.kind = rw::Kind::vehicle;
        in.seat_mode = true;
        in.seat = 0;
        in.vehicle = 25603;
        rw::Observe(v, in);
        Check(v.hidden && !v.seated && v.mount, "a walker act in a seat-mode vehicle ride is hidden, never seated");
        in.remote_act = 111;
        rw::Observe(v, in);
        Check(!v.hidden && v.seated && !v.mount, "the seated act shows the seated driver again");
    }
    // Heli acts keep their own rule; the mount flag stays clear.
    {
        rw::Visibility v{};
        rw::In in = Sample(99, 1000);
        in.mount_hide = true;
        rw::Observe(v, in);
        Check(v.hidden && !v.mount && Named(v.reason, "ride"), "heli acts hide by the heli rule, not the mount rule");
    }
}

} // namespace

int main() {
    TestActs();
    TestHide();
    TestEpisode();
    TestUnrealize();
    TestVisibility();
    TestRideOf();
    TestRideEpisode();
    TestRideVisibility();
    std::printf("%u partner mount checks passed: horse 121/122 and D-Walker 123..126 acts, hide decision, episode lines, "
                "unrealize decision, ride visibility with partner_native_mount=hide, G5 ride episodes (heli, vehicle, "
                "horse kind 3)\n",
                checks);
    return 0;
}
