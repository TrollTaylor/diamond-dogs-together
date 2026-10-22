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
    TestVisibility();
    std::printf("%u partner mount checks passed: horse 121/122 and D-Walker 123..126 acts, hide decision, episode lines, "
                "ride visibility with partner_native_mount=hide\n",
                checks);
    return 0;
}
