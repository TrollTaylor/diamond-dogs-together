// Deterministic timelines for the offline partner spike's SnapshotRing:
// interpolation, jitter offset, shortest-arc yaw, snap, extrapolation cap,
// stale/lost, latest-only mode, capacity and clock wrap. Pure; no game calls.
#include "mgscoop/pose_interp.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
mgscoop::pose::Snapshot At(std::uint32_t ms, float x, float yaw = 0.0f, std::uint8_t stance = 0, std::uint8_t flags = 0) {
    return mgscoop::pose::Snapshot{ms, x, 1.0f, -x, yaw, stance, flags};
}
constexpr float kPi = 3.14159265358979f;
} // namespace

int main() {
    using namespace mgscoop::pose;

    // Constant-initializable global use.
    static SnapshotRing global_ring;
    Check(global_ring.empty() && global_ring.size() == 0 && global_ring.newest_receive_ms() == 0, "global ring empty");

    // Empty.
    SnapshotRing ring;
    Sample s = ring.SampleAt(1000, 100);
    Check(!s.valid && s.status == Status::none, "empty ring");

    // Single snapshot: holds it; status live.
    Check(ring.Push(At(1000, 0.0f), 1010), "push first");
    s = ring.SampleAt(1010, 100);
    Check(s.valid && s.held && Near(s.x, 0.0f) && s.status == Status::live && s.age_ms == 0, "single snapshot held");
    s = ring.SampleAt(1500, 100);
    Check(s.valid && s.held && !s.extrapolated && Near(s.x, 0.0f), "single snapshot never extrapolates");

    // Non-increasing sender times are refused.
    Check(!ring.Push(At(1000, 5.0f), 1020) && !ring.Push(At(999, 5.0f), 1020) && ring.size() == 1, "stale push refused");

    // Linear interpolation with a constant 20 ms transit.
    ring.Reset();
    Check(ring.empty(), "reset");
    ring.Push(At(1000, 0.0f), 1020);
    ring.Push(At(1100, 1.0f, 0.0f, 1, 4), 1120);
    ring.Push(At(1200, 2.0f, 0.0f, 2, 0), 1220);
    s = ring.SampleAt(1270, 100);  // render = 1270 - 20 - 100 = 1150
    Check(s.valid && s.interpolated && Near(s.x, 1.5f) && Near(s.z, -1.5f) && Near(s.y, 1.0f), "midpoint");
    Check(s.stance == 2 && s.flags == 0, "stance/flags from the newer snapshot");
    s = ring.SampleAt(1145, 100);  // render 1025
    Check(s.interpolated && Near(s.x, 0.25f) && s.stance == 1 && s.flags == 4, "quarter");
    s = ring.SampleAt(1220, 100);  // render 1100 exactly on a snapshot
    Check(Near(s.x, 1.0f), "on a snapshot");
    s = ring.SampleAt(1100, 100);  // render 980 before the oldest
    Check(s.held && Near(s.x, 0.0f), "before oldest holds oldest");

    // Jitter: the offset is the minimum transit, so late packets do not pull
    // the render time back.
    ring.Reset();
    ring.Push(At(1000, 0.0f), 1020);
    ring.Push(At(1100, 1.0f), 1160);  // 60 ms late
    ring.Push(At(1200, 2.0f), 1225);
    s = ring.SampleAt(1270, 100);  // offset 20 -> render 1150
    Check(s.interpolated && Near(s.x, 1.5f), "jitter uses minimum transit");

    // Shortest-arc yaw across +-pi.
    ring.Reset();
    ring.Push(At(1000, 0.0f, 3.0f), 1000);
    ring.Push(At(1100, 0.1f, -3.0f), 1100);
    s = ring.SampleAt(1150, 100);  // render 1050, f = 0.5
    Check(Near(std::fabs(s.yaw), kPi, 1e-3f), "yaw crosses pi, not zero");
    s = ring.SampleAt(1125, 100);  // f = 0.25
    Check(Near(s.yaw, 3.0f + 0.0708f, 1e-3f), "yaw quarter along the short arc");
    ring.Reset();
    ring.Push(At(1000, 0.0f, -3.0f), 1000);
    ring.Push(At(1100, 0.1f, 3.0f), 1100);
    s = ring.SampleAt(1125, 100);
    Check(Near(s.yaw, -3.0f - 0.0708f, 1e-3f), "yaw short arc, other direction");
    Check(Near(WrapAngle(3.5f * kPi), -0.5f * kPi, 1e-4f) && Near(WrapAngle(-kPi / 2), -kPi / 2), "WrapAngle");

    // Snap: a jump over 5 m holds the older pose until render reaches the newer one.
    ring.Reset();
    ring.Push(At(1000, 0.0f), 1000);
    ring.Push(At(1100, 10.0f), 1100);
    s = ring.SampleAt(1199, 100);  // render 1099
    Check(s.snapped && Near(s.x, 0.0f) && !s.interpolated, "snap holds older pose");
    s = ring.SampleAt(1200, 100);  // render 1100
    Check(!s.snapped && Near(s.x, 10.0f), "snap lands on newer pose");
    s = ring.SampleAt(1250, 100);  // render 1150: no extrapolation across a snap
    Check(s.held && !s.extrapolated && Near(s.x, 10.0f), "no extrapolation across a snap");
    ring.Reset();
    ring.Push(At(1000, 0.0f), 1000);
    ring.Push(At(1100, 3.5f), 1100);  // 3.5 m in x and z: 4.95 m < 5 m
    s = ring.SampleAt(1150, 100);
    Check(s.interpolated && Near(s.x, 1.75f), "just under the snap distance interpolates");

    // Extrapolation: at most 100 ms past the newest snapshot, then hold.
    ring.Reset();
    ring.Push(At(1000, 0.0f, 0.0f), 1000);
    ring.Push(At(1100, 1.0f, 0.1f), 1100);
    s = ring.SampleAt(1250, 100);  // render 1150 -> 50 ms ahead
    Check(s.extrapolated && !s.held && Near(s.x, 1.5f) && Near(s.yaw, 0.15f), "extrapolate 50 ms");
    s = ring.SampleAt(1400, 100);  // render 1300 -> capped at 100 ms
    Check(s.extrapolated && s.held && Near(s.x, 2.0f) && Near(s.yaw, 0.2f), "extrapolation capped then held");
    s = ring.SampleAt(5000, 100);
    Check(s.held && Near(s.x, 2.0f), "long silence holds the capped pose");

    // Staleness by age of the newest receive.
    Check(StatusForAge(0) == Status::live && StatusForAge(500) == Status::live && StatusForAge(501) == Status::stale &&
          StatusForAge(3000) == Status::stale && StatusForAge(3001) == Status::lost, "status thresholds");
    Check(ring.SampleAt(1600, 100).status == Status::live, "age 500 live");
    Check(ring.SampleAt(1601, 100).status == Status::stale, "age 501 stale");
    Check(ring.SampleAt(4101, 100).status == Status::lost && ring.SampleAt(4101, 100).valid, "lost still holds a pose");
    Check(ring.SampleAt(4101, 100).age_ms == 3001, "age reported");
    Check(ring.SampleAt(900, 100).age_ms == 0, "clock before receive saturates to 0");
    Check(StatusName(Status::live)[0] == 'l' && StatusName(Status::none)[0] == 'n', "status names");

    // delay 0: newest snapshot only.
    s = ring.SampleAt(1100, 0);
    Check(!s.interpolated && !s.extrapolated && Near(s.x, 1.0f) && Near(s.yaw, 0.1f), "latest only");
    s = ring.SampleAt(9000, 0);
    Check(Near(s.x, 1.0f) && s.status == Status::lost, "latest only, lost");

    // Capacity: the oldest entries are dropped.
    ring.Reset();
    for (std::uint32_t i = 0; i < 20; ++i) Check(ring.Push(At(1000 + i * 10, static_cast<float>(i) * 0.1f), 1000 + i * 10), "fill");
    Check(ring.size() == SnapshotRing::capacity && ring.newest_receive_ms() == 1190, "capacity");
    s = ring.SampleAt(1190, 155);  // render = 1035, before the kept oldest (1040)
    Check(s.held && Near(s.x, 0.4f), "dropped entries are gone");

    // Sender clock wrap (u32 milliseconds).
    ring.Reset();
    Check(ring.Push(At(0xFFFFFFB0u, 0.0f), 5000) && ring.Push(At(0x00000014u, 1.0f), 5100), "push across wrap");
    s = ring.SampleAt(5150, 100);  // offset = 5000 - 0xFFFFFFB0 (unwrapped); render midpoint
    Check(s.interpolated && Near(s.x, 0.5f), "interpolation across wrap");
    Check(!ring.Push(At(0xFFFFFFF0u, 2.0f), 5200), "older across wrap refused");

    // Animation extension fields are discrete: a sample carries those of the
    // snapshot it holds or interpolates towards, never a blend.
    ring.Reset();
    Snapshot a = At(1000, 0.0f), b = At(1100, 1.0f);
    a.anim_valid = true; a.act = 1; a.anim_stance = 1; a.anim_speed = 0;
    b.anim_valid = true; b.act = 4; b.anim_stance = 2; b.anim_speed = 2;
    a.flags = 1; a.equip_id = 10004;
    b.flags = 10; b.equip_id = 30103;
    Check(ring.Push(a, 2000) && ring.Push(b, 2100), "push anim snapshots");
    s = ring.SampleAt(2150, 100);  // render 1050: between a and b
    Check(s.interpolated && s.anim_valid && s.act == 4 && s.anim_stance == 2 && s.anim_speed == 2, "anim of the newer snapshot");
    Check(s.flags == 10 && s.equip_id == 30103, "interpolated gun metadata is discrete newer data");
    s = ring.SampleAt(2050, 100);  // render 950: before the oldest
    Check(s.held && s.anim_valid && s.act == 1 && s.anim_stance == 1, "held oldest keeps its anim");
    Check(s.flags == 1 && s.equip_id == 10004, "oldest hold keeps its gun metadata");
    s = ring.SampleAt(2300, 0);
    Check(s.anim_valid && s.act == 4 && s.anim_speed == 2, "latest-only keeps the newest anim");
    Check(s.flags == 10 && s.equip_id == 30103, "latest-only gun metadata");
    s = ring.SampleAt(2300, 100);  // extrapolated past b
    Check(s.extrapolated && s.act == 4, "extrapolation keeps the newest anim");
    Check(s.flags == 10 && s.equip_id == 30103, "extrapolation does not fabricate gun transitions");
    Snapshot v1 = At(1200, 1.1f);
    Check(ring.Push(v1, 2200), "push a v1 snapshot");
    s = ring.SampleAt(2400, 0);
    Check(!s.anim_valid && s.act == 0, "a v1 snapshot has no anim");
    Check(s.flags == 0 && s.equip_id == 0, "legacy/default snapshot clears rich action state");
    ring.Reset(); a = At(1000, 0); b = At(1100, 20);
    a.equip_id = 10004; a.flags = 1; b.equip_id = 30103; b.flags = 10;
    ring.Push(a, 2000); ring.Push(b, 2100);
    s = ring.SampleAt(2150, 100);
    Check(s.snapped && s.equip_id == 10004 && s.flags == 1, "snap hold preserves older action metadata until destination");

    std::printf("%u pose interpolation checks passed: lerp, jitter offset, shortest-arc yaw, snap, extrapolation cap, "
                "stale/lost, latest-only, capacity, clock wrap, discrete anim fields; deterministic, no game calls\n", checks);
    return 0;
}
