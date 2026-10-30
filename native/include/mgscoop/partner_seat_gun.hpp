#pragma once
// M7 (docs/vehicles-companions-design.md 6.7 and section 9 M7; docs/native-puppet.md "Seat aim"): the partner's raised
// gun on the seated body 1, design A of the seat-aim research. Pure part (mgs_coop_core): when the viewer's copy holds
// the gun up on body 1, and the aim pose it gives body 1.
//
// While the seat flow (partner_native_ride=seat) holds body 1 seated in a vehicle passenger seat and the remote's live
// input frame says its gun group is Aiming, the driver asks the gesture hook (partner_gesture, body 1's own motion
// update) to put body 1's upper graph (graph 2) on the stock gun-hold nodes:
//   GunHoldStart 0x50130AE2E584  (StateGunHoldStart 0x1075620 event 1: cs vt+0x170 SetCurrentNode(cs, id, 2, node,
//                                 0.0f, 0) at 0x1075A50; the raise clip)
//   GunHold      0xAA0EA22A5E0E  (after the raise clip's motion end, vt+0x228(cs, 1, 2): the held loop)
// and to keep it there while the driver still holds that request (Command::hold_seq). The aim stops (no Aiming for
// kReleaseMs, a stale sample, the seat ends, the key off): the hook returns graph 2 to stateNull, as every gesture ends.
// The DM seat chain keeps graph 0 (the seated legs); nothing else of body 1 is written.
//
// The aim pose: while the hold runs, the hook also writes body 1's per-id aim rates (the rates record of
// *(PS+0x260): +0x5C pitch r5c, +0x60 yaw r60; the gun states' 0x108E7D0 writes them from the aim point, and the
// gun-hold enter sets +0x58 from the gun record right before GunHold, 0x10721C4) from the remote's v6 world aim
// direction relative to body 1's seated yaw, by the inverse of 0x108EC55 for the default class (75 deg pitch, 90 deg
// yaw: partner_player_contract aim_range_deg[0]). The originals are put back when the hold ends.
#include <cmath>
#include <cstdint>

namespace mgscoop::partner_seat_gun {

inline constexpr std::uint64_t kGunHoldStart = 0x50130AE2E584ULL;
inline constexpr std::uint64_t kGunHold = 0xAA0EA22A5E0EULL;
inline constexpr float kStartBlend = 0.0f;   // stock: 0x1075A50 passes 0.0f to vt+0x170
inline constexpr float kHoldBlend = 0.0f;    // the raise clip has ended: no cross-fade needed
inline constexpr std::uint64_t kReleaseMs = 300;   // the remote's Aiming must be gone this long before the hold ends
inline constexpr std::uint64_t kSampleStaleMs = 500;  // an older input sample neither starts nor keeps a hold
inline constexpr std::uint64_t kRetryMs = 1000;    // after a refused or ended hold, the next start waits this long
inline constexpr std::uint64_t kDirStaleMs = 500;  // an older v6 direction is not written (the last pose stays)

// The driver's gate for a hold (nullptr: hold; else the refusal word logged once per change).
struct GateIn {
    bool enabled;        // partner_seat_gun (lab key) with partner_native_gesture (the hook)
    bool installed;      // the gesture hook is live
    bool uncertain;      // the gesture mission latch
    bool seated;         // the seat flow holds body 1 seated (SeatPhase::seated, confirmed by the DM record)
    bool passenger;      // the remote's live ride is a vehicle passenger seat (1..3), not the driver's seat 0
    bool realized;       // body 1 realized with its parts loaded
    const char* block;   // carry / overlay / downed, or nullptr
    bool other_gesture;  // a gesture (pickup, sign, knock) is running or waiting on body 1
};
const char* Gate(const GateIn& in) noexcept;
// GateIn::passenger from the remote's live ride: a passenger seat 1..3 of a vehicle ride, never the driver's seat 0,
// the wire's unknown seat (255) or a seat past a light vehicle's four.
inline bool PassengerSeat(bool vehicle_ride_live, std::uint8_t seat) noexcept { return vehicle_ride_live && seat >= 1 && seat <= 3; }

// The remote's aim: its live pose sample's action flags (sync::ActionFlag::Aiming; run 6fc3098b showed them arriving
// from a back-seat passenger: partner_action_state; flags=0x1|0x3; act=110), or its newest input frame's v5 gun group.
struct AimIn {
    bool pose_live;      // the remote's pose sample is live
    bool pose_aiming;    // its action flags say Aiming
    bool input;          // an input sample exists
    std::uint64_t input_age_ms;
    bool input_aiming;   // frame version >= 5, the gun group valid and Aiming
};
bool RemoteAiming(const AimIn& in) noexcept;

// The hold decision with a release debounce: Aiming starts a hold at once (when the gate passes and no retry wait
// runs); the hold ends kReleaseMs after the last Aiming sample, or at once when the gate falls.
struct State {
    bool want;                 // the driver holds (publishes hold_seq)
    std::uint64_t last_aim_ms; // the last tick the remote aimed
    std::uint64_t retry_ms;    // no new start before this
    std::uint32_t starts, releases;
    const char* refusal;       // the last gate refusal (for the change log)
};
enum class Edge : std::uint8_t { none, start, release };
struct StepOut {
    Edge edge;
    const char* cause;   // release: aim | gate:<word> (the gate refusal)
};
StepOut Step(State& s, const char* gate_refusal, bool aiming, std::uint64_t now_ms) noexcept;
// The hook ended or refused the hold (not the driver): the next start waits kRetryMs.
void HookEnded(State& s, std::uint64_t now_ms) noexcept;

// --- The aim pose for body 1 (the inverse of 0x108EC55 for the default class).
inline constexpr double kPitchRangeDeg = 75.0, kYawRangeDeg = 90.0;
struct RatesIn {
    bool dir_ok;          // a fresh v6 direction (world pitch / yaw, radians)
    double pitch, yaw;
    bool body_ok;         // body 1's seated yaw (radians, the body quaternion's yaw)
    double body_yaw;
};
struct RatesOut {
    bool ok;
    float r5c, r60;       // clamped to [0, 1]; 0.5 / 0.5 = straight ahead of the seat
    bool clamped;         // the remote aims outside body 1's seat range
};
RatesOut Rates(const RatesIn& in) noexcept;
// A rate the hook may write: finite and inside [0, 1].
inline bool RateOk(float v) noexcept { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; }

} // namespace mgscoop::partner_seat_gun
