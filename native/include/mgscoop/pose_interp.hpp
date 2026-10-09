#pragma once
// Snapshot interpolation for the offline partner spike's remote pose
// (docs/offline-partner-spike-plan.md 2.3 item 3 and 2.6). Pure, portable,
// deterministic: every time is passed in, nothing allocates or throws, and the
// ring is constant-initializable (a POD-style global in partner_net).
//
// Rules:
// - Snapshots carry the sender's millisecond clock (u32, unwrapped on push).
//   offset = min over the ring of (receive_ms - sender_ms); the render time on
//   the sender timeline is now - offset - delay.
// - Between two snapshots: linear position, shortest-arc yaw, stance/flags of
//   the newer one. A jump of more than snap_distance_m is never interpolated:
//   the older pose holds until render time reaches the newer one (snap).
// - Past the newest snapshot: extrapolate with the last velocity for at most
//   extrapolate_cap_ms, then hold. Never across a snap-sized or speed-qualified jump.
// - Before the oldest: hold the oldest.
// - delay 0 means the latest snapshot only.
// - Status by age of the newest receive: live <= stale_ms < stale <= lost_ms < lost.
//   Stale and lost hold the pose; the spike never hides the partner on loss.
#include <cstddef>
#include <cstdint>

namespace mgscoop::pose {

inline constexpr std::uint32_t stale_ms = 500;
inline constexpr std::uint32_t lost_ms = 3000;
inline constexpr std::uint32_t extrapolate_cap_ms = 100;
inline constexpr float snap_distance_m = 5.0f;
// A bracketing snapshot pair that moved more than jump_distance_m faster than
// jump_speed_mps in sender time is a jump (a teleport, never locomotion);
// partner_recon places on it (docs/native-puppet.md "Movement").
inline constexpr float jump_distance_m = 1.5f;
inline constexpr float jump_speed_mps = 20.0f;

struct Snapshot {
    std::uint32_t sender_ms;  // sender clock (PlayerTransformPacket::tick_ms)
    float x, y, z;            // metres
    float yaw;                // radians
    std::uint8_t stance;
    std::uint8_t flags;
    // Animation extension (partner wire v2); anim_valid false for a v1 frame.
    // Discrete: a sample carries the fields of the snapshot it holds or
    // interpolates towards (never blended).
    bool anim_valid;
    std::uint16_t act;
    std::uint8_t anim_stance, anim_speed;
    std::uint16_t equip_id;  // discrete current gun; zero when unknown
};

enum class Status : std::uint8_t { none, live, stale, lost };
const char* StatusName(Status status) noexcept;
Status StatusForAge(std::uint64_t age_ms) noexcept;
// Wraps an angle to [-pi, pi).
float WrapAngle(float radians) noexcept;

struct Sample {
    bool valid;          // false only when the ring is empty
    float x, y, z, yaw;
    std::uint8_t stance, flags;
    bool anim_valid;
    std::uint16_t act;
    std::uint8_t anim_stance, anim_speed;
    Status status;
    std::uint32_t age_ms;  // now - newest receive (saturating)
    bool interpolated;     // between two snapshots
    bool snapped;          // a snap-sized jump was held instead of interpolated
    bool extrapolated;     // moved past the newest snapshot
    bool held;             // clamped: extrapolation cap, oldest hold, or no velocity
    std::uint16_t equip_id;  // same snapshot as stance/flags; never blended
    // The unwrapped sender time this position belongs to (the render time
    // while interpolating, the held snapshot's time while holding, the newest
    // plus the capped extrapolation): speeds over two samples use sender time,
    // so arrival jitter never looks like a teleport. Valid with `time_valid`.
    std::int64_t sender_ms;
    // Most recent speed-qualified jump whose destination the sampled pose
    // reached. Identity persists through newer arrivals while the pair remains
    // in the ring; consumers act once per jump_ms, never on the pre-jump hold.
    bool jump;
    std::int64_t jump_ms;
    bool time_valid;      // false for legacy/synthetic sample providers
    // Nonzero receive-ring identity. Only Reset changes it; interpolation and
    // alignment rewinds keep it, so consumers can distinguish a new peer timeline.
    std::uint32_t generation{};
};

// A source-history cursor is private receive state, never a game/action token.
// Sender times are the same strictly accepted, unwrapped times used by the ring.
struct HistoryCursor { std::uint32_t generation{}; std::int64_t sender_ms{-1}; };
struct SourceRecord { Snapshot snapshot{}; std::int64_t sender_ms{}; std::uint64_t receive_ms{}; };
struct RemoteFrame {
    Sample visual{};
    SourceRecord records[16]{};
    std::uint8_t count{};
    HistoryCursor next{};
    std::uint64_t now_ms{};
    bool reset{}, overflow{};
};

class SnapshotRing {
public:
    static constexpr std::size_t capacity = 16;
    constexpr SnapshotRing() noexcept = default;
    // Accepts only a snapshot whose sender time is strictly newer than the
    // newest one (signed 32-bit difference). Returns false otherwise.
    bool Push(const Snapshot& snapshot, std::uint64_t receive_ms) noexcept;
    Sample SampleAt(std::uint64_t now_ms, std::uint32_t delay_ms) const noexcept;
    // Read visual pose and accepted raw history together under the caller's
    // existing receive lock. A new/reset/overrun cursor receives only a current
    // baseline; it cannot manufacture old pivots from missing history.
    RemoteFrame ReadFrame(std::uint64_t now_ms, std::uint32_t delay_ms, const HistoryCursor& cursor) const noexcept;
    void Reset() noexcept;
    std::size_t size() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }
    // Receive time of the newest snapshot (0 when empty).
    std::uint64_t newest_receive_ms() const noexcept;

private:
    struct Entry {
        Snapshot snapshot;
        std::int64_t sender_time;  // unwrapped sender clock
        std::uint64_t receive_ms;
        std::int64_t previous_sender_ms;
    };
    const Entry& at(std::size_t index) const noexcept { return entries_[(head_ + index) % capacity]; }
    Entry entries_[capacity]{};
    std::size_t head_ = 0;   // oldest entry
    std::size_t count_ = 0;
    std::uint32_t generation_ = 1;
};
static_assert(SnapshotRing::capacity <= sizeof(RemoteFrame::records) / sizeof(SourceRecord),
              "paired raw source frame must hold the entire snapshot ring");

} // namespace mgscoop::pose
