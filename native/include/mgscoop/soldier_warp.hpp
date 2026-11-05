#pragma once
#include <cstddef>
#include <cstdint>

// Debug "Spawn enemy here" (docs/debug-menu.md): sends the engine's own
// set-transform message 0x287158b07327 to one soldier GameObjectId through the
// GameObject manager's SendCommand, as the player carry code does at
// 0x1055b66. Contract: native/soldier_warp_contract.hpp
// (tools/generate_soldier_warp_contract.py). Default off (lab.ini
// debug_soldier_warp=1); the caller owns the thread, mission, role and
// command gates and calls this only on the Lua owner thread.
namespace mgscoop::soldier_warp {
inline constexpr float kMaxRangeM = 20.0f;  // target to local player (default Request::max_range)
// Upper bound of Request::max_range: a guest spawn-sync correction (world_place
// kind 3) may move a guard up to 60 m from the guest's view of the host.
inline constexpr float kMaxRangeLimitM = 60.0f;
// Enemy follow (lab.ini soldier_follow, docs/world-sync-design.md 7.8): a correction
// grant whose target lies more than this from BOTH players refuses `far` before the
// range check (the four anomaly latches logged before 0.14.7 were guards 300-500 m away).
inline constexpr float kFollowFarM = 250.0f;

// Exact bytes of every contract site and data qword in the running image.
// nullptr when all match, else a reason; `mismatch` gets the first RVA.
const char* Verify(std::uintptr_t base, std::size_t span, std::uintptr_t& mismatch) noexcept;

struct Request {
    std::uint32_t go_id{};
    float pos[3]{};     // ground position, metres (the handler adds the body offset T+0x160 to y)
    float yaw{};        // radians; quaternion {0, sin(yaw/2), 0, cos(yaw/2)}
    float player[3]{};  // reference position, metres (host: the local player; guest: its view of the host)
    float max_range{kMaxRangeM};  // metres, (0, kMaxRangeLimitM]; anything else refuses `input`
};

// WARP_V1 receipt text from lua/coop/debug_spawn.lua:
// "<command id> <GameObjectId> <x_mm> <y_mm> <z_mm> <yaw_mrad>", single spaces, decimal
// integers only. Fills `command` and request.go_id/pos/yaw (not player); false on any
// other text or out-of-range field (id 1..2^32-1, GameObjectId <= 0xffff, |mm| <= 1e8,
// |yaw| <= 8000 mrad).
bool ParseReceipt(const char* text, std::size_t size, std::uint32_t& command, Request& request) noexcept;

// Read-only census of the dispatch chain (RVAs are 0 when outside the image).
struct Chain {
    std::uintptr_t type_object{}, type_vt{}, count_fn{}, handle_object{}, handle_vt{}, hop{}, leaf{}, leaf_vt{};
    unsigned char hop_bytes[24]{};
    bool hop_read{}, pinned{};              // pinned: the hop provably reaches 0x1360f90 on `leaf`
    // typed: the live soldier dispatch (native/soldier_relay_contract.hpp): the handle vtable's hop 0xbf5450 reaches the
    // soldier type dispatcher 0x9e9ab0 through the object at handle+0x20 (every census line of run e583ee62).
    bool typed{};
    bool leaf_state{};                      // the three fields below were read (pinned only)
    std::uint32_t count{}, realized{}, stance{};
};

struct Result {
    // nullptr: the message was sent and the engine gave one of its two set-transform
    // answers: out=0 handled=1 (moved) or out=-1 handled=0 (refused and not relayed: not
    // realized or no character on a pinned hop, no slot on the typed dispatch, no handle).
    // Refusals before sending: contract type input range manager registry chain
    // leaf (a recognized forwarder hop that does not reach the verified handler)
    // unrealized (r=-1) index stance. After sending: fault (SEH), relayed (the typed
    // dispatch answered out=0 handled=0: the handler refused a guard whose slot exists on
    // this copy - not realized here, or realized with no character - and the dispatcher
    // passed the message to the guard's unrealized form or realized component, which did
    // not handle it, so nothing moved; whether he was realized is not read [U]; or
    // out=0x8ce64801, the handle's mode 2) and anomaly (any other out/handled pair, or
    // out=0 handled=0 on an untyped chain).
    const char* refusal{};
    std::int32_t out{-2};
    std::uint8_t handled{};
    bool sent{};
    Chain chain{};
};

// Caller has already verified the contract (Verify) for this image.
Result Warp(std::uintptr_t base, std::size_t span, const Request& request) noexcept;

// Per-command bound, owned by the single caller thread. One "spawn_enemy here"
// command (or one guest spawn-sync grant, id 0x80000000|epoch<<16|seq) moves one
// guard: a fixed GameObjectId, at most kMaxAttempts calls (the menu retries 151
// times at most) and none after the engine handled it.
// A fault latches the feature off for the rest of the process. An anomaly (an answer
// none of the pinned paths explains) counts against its guard (enemy follow multiplies
// warps, docs/world-sync-design.md 7.8): kSoldierAnomalies on one GameObjectId latch that
// guard off, kProcessAnomalies across guards latch the feature off, both for the rest of
// the process. Enemy latch (7.14, run e583ee62: five relays in a firefight latched the
// process): a relayed answer is never counted. It backs only that guard off, without
// sending: kRelayBackoffMs, doubled for each further relay in a row up to
// kRelayBackoffMaxMs; a move clears it.
inline constexpr unsigned kMaxAttempts = 160;
inline constexpr unsigned kSoldierAnomalies = 2, kProcessAnomalies = 5, kTrackedSoldiers = 32;
inline constexpr std::uint64_t kRelayBackoffMs = 3000, kRelayBackoffMaxMs = 30000;
inline constexpr unsigned kRelayTracked = 64;
struct Gate {
    std::uint32_t command{}, go_id{};
    unsigned attempts{};
    bool landed{};
    const char* latched{};
    unsigned anomalies{};  // every anomaly of this process
    struct Soldier {
        std::uint16_t go;
        std::uint8_t anomalies;
    };
    Soldier soldiers[kTrackedSoldiers]{};  // the guards with an anomaly (the first kTrackedSoldiers)
    unsigned soldier_count{};
    struct Relay {
        std::uint16_t go;
        std::uint8_t streak;   // relays in a row (a move clears it)
        std::uint64_t until;   // ms: backed off before this
    };
    Relay relays[kRelayTracked]{};  // the guards relayed lately (the oldest backoff is reused when full)
    unsigned relayed{};             // every relayed answer of this process
    std::uint64_t now{};            // ms of the last Admit / AdmitDrive
    // nullptr admits one Warp call; else latched, soldier (this guard is latched),
    // backoff (this guard was relayed lately), target, landed or budget.
    const char* Admit(std::uint32_t command_id, std::uint32_t go, std::uint64_t now_ms = 0) noexcept;
    // The latch reason when this result newly latches the feature off ("fault", or
    // "anomaly" at the kProcessAnomalies-th anomaly), "soldier" when it newly latches
    // only its guard, "relayed" when it backs only its guard off, else nullptr.
    const char* Record(const Result& result) noexcept;
    // Anomalies recorded against this GameObjectId (0 if none).
    unsigned SoldierAnomalies(std::uint32_t go) const noexcept;
    // This GameObjectId's relay backoff: ms left at now_ms (0: none) and relays in a row.
    std::uint64_t BackoffLeft(std::uint32_t go, std::uint64_t now_ms) const noexcept;
    unsigned RelayStreak(std::uint32_t go) const noexcept;
    // Enemy sync M4's drive (no command budget: one Warp per step): nullptr admits one Warp of `go` and makes it the
    // guard Record() counts an anomaly against; else latched, soldier (this guard is latched) or backoff.
    const char* AdmitDrive(std::uint32_t go, std::uint64_t now_ms = 0) noexcept;
};

// Guest grant admission (game_script_observer.cpp GuestWarpReceipt), after the
// role, contract, mission and grant checks. Three rules, in this order:
// - lab key: lab mode with lab.ini debug_soldier_warp=1, any grant kind (spawn sync);
// - enemy follow (docs/world-sync-design.md 7.8): play or lab with lab.ini
//   soldier_follow=1, a free roam or Mother Base mission (30010, 30020, 30050) and a
//   correction (world_place kind 3) only, so the guest's copies of the guards near
//   either player stand where the host's do;
// - Mother Base staff (docs/world-sync-design.md 7.7): play or lab, the current mission
//   is 30050 and the grant is a correction only, never a warp, route or come, so the
//   guest's copy of a staff member stands where the host's does.
// nullptr admits (`grant` tells which rule); else "role" (neither play nor lab) or
// "disabled" (no rule applies). The staff and follow rules take the range from the
// nearer of the two players; follow refuses a target beyond kFollowFarM from both.
inline constexpr unsigned short kStaffMission = 30050;
inline constexpr bool FollowMission(unsigned short mission) noexcept {
    return mission == 30010 || mission == 30020 || mission == kStaffMission;
}
enum class GuestGrant : unsigned char { none, lab, follow, staff };
struct GuestRule {
    bool lab{}, play{}, key{};  // lab mode, play mode, lab.ini debug_soldier_warp
    unsigned short mission{};   // current mission
    bool correct{};             // the grant is a correction (kind 3)
    bool follow{};              // lab.ini soldier_follow
};
const char* GuestAdmit(const GuestRule& rule, GuestGrant& grant) noexcept;
// Enemy sync M3 (docs/world-sync-design.md 7.9): the host warps its own copy of a guard the guest owns, only from the
// grant of the guest's ENEMY_PLACE (a correction), in play or lab with soldier_owner, in 30010 / 30020 / 30050. nullptr
// admits; otherwise the refusal ("role", "disabled", "mission", "kind").
struct HostRule {
    bool lab{}, play{};         // lab mode, play mode
    bool host{};                // this process is the host
    unsigned short mission{};   // current mission
    bool correct{};             // the grant is a correction (kind 3)
    bool owner{};               // lab.ini soldier_owner
};
const char* HostAdmit(const HostRule& rule) noexcept;
// Enemy sync M4 (docs/world-sync-design.md 7.10): the soft follow of a copy toward the owner's SOLDIER_SNAP.
// DriveTarget: the owner's position at render time `t` (receiver ms): between prev and cur by their receive times, else
// cur moved on by their velocity for at most kDriveExtrapolateMs; false without cur or when cur is kDriveStaleMs old.
inline constexpr std::uint64_t kDriveDelayMs = 100, kDriveExtrapolateMs = 250, kDriveStaleMs = 1000;
struct DriveSample {
    bool valid{};
    std::uint64_t rx_ms{};
    float pos[3]{};
    float yaw{};
};
bool DriveTarget(const DriveSample& prev, const DriveSample& cur, std::uint64_t t, float (&out)[3], float& yaw) noexcept;
// DriveStep: from this copy (`from`, GetPosition metres) toward `to` (the target, the request's y convention): planar
// error; a guard at rest starts moving beyond start_m and stops under stop_m (`active` carries it); a step is at most
// max_step metres planar; beyond snap_m the whole way (hard). y: the target's when hard or more than 1 m off this copy's
// feet (GetPosition y - feet_m), else this copy's own feet plus lift_m (it stays on its own ground). Enemy gaps
// (place_lift, place_sink): Warp takes GetPosition-space y (corrections read back +0.40 over a request 0.4 m under the
// owner, or -0.01 when the engine left them there and they fell a storey), so with the key the level y is this copy's
// own GetPosition y + kLiftLevelM (feet_m kLiftFeetM) instead of 0.7 m under it.
inline constexpr float kDriveFeetM = 0.8f, kLiftFeetM = 0.0f, kLiftLevelM = 0.02f;
// Enemy gaps 3 (lab key drive_vert, docs/world-sync-design.md 7.18): with vert_m > 0 a copy at rest in x/z that stands
// more than vert_m above or below the target is put on the target's spot (vert): the planar rest test never moved one
// (drive_sink at the defaults: 2929558510 2.2-3.3 m under its target at 0.14-1.05 m planar in 6348f361 / 41f2b07f /
// 31a799e3 / b77e2104, while M1 / M3 corrections skip a driven guard). DriveVertDue: the gap held kDriveVertMs.
inline constexpr float kDriveVertM = 1.5f;
inline constexpr std::uint64_t kDriveVertMs = 1000;
struct DriveStepResult {
    float pos[3]{};
    float error{};
    bool move{}, hard{}, active{}, vert{};
};
DriveStepResult DriveStep(const float (&from)[3], const float (&to)[3], bool active, float start_m, float stop_m, float max_step,
                          float snap_m, float lift_m, float feet_m = kDriveFeetM, float vert_m = 0.0f) noexcept;
// True once |gap_m| (the target's y minus this copy's) has stayed over kDriveVertM for kDriveVertMs (since_ms carries
// when it began; a smaller gap clears it).
bool DriveVertDue(float gap_m, std::uint64_t now_ms, std::uint64_t& since_ms) noexcept;
// Enemy round 7 track A (design 7.20): no thrash. A copy is snapped at most kDriveVertMax times in kDriveVertWindowMs
// from the first snap of the window (round 6: copies popped up and dropped again 1.4-1.7 s after a snap). Allow: may
// this due snap be taken (an expired window starts over: window 0, count 0); Count: a snap was taken.
inline constexpr unsigned kDriveVertMax = 2;
inline constexpr std::uint64_t kDriveVertWindowMs = 10000;
bool DriveVertAllow(std::uint64_t now_ms, std::uint64_t& window_ms, unsigned& count) noexcept;
void DriveVertCount(std::uint64_t now_ms, std::uint64_t& window_ms, unsigned& count) noexcept;
// The receipts' refusal reply: return {refused="<why>"}, with ,b=<back_ms> (the ms left of that guard's relay backoff,
// so Lua's realize hold retries when it ends) when back_ms > 0. False when it does not fit.
bool FormatRefusal(char* out, std::size_t capacity, const char* why, std::uint64_t back_ms) noexcept;
// Who may drive (DriveReceipt): play or lab with soldier_follow + soldier_snap, 30010 / 30020 / 30050; a host only with
// soldier_owner (its copies of guest-owned guards). nullptr admits; else "role", "disabled", "mission".
struct DriveRule {
    bool lab{}, play{}, host{};
    unsigned short mission{};
    bool snap{}, owner{};
    bool puppet{}, puppet_key{};  // M7 probe: the puppet drive asked for, and lab.ini soldier_puppet (a guest only)
    bool mirror{}, act_key{};     // FM1: the mirror drive (mode 2) asked for, and lab.ini soldier_act (a guest only for now)
};
// FM1, the mirror drive (DRIVE_V1 mode 2): a mirrored copy walks on its own legs toward the owner's request, so the drive
// never steps it under kMirrorSnapM and moves it the whole way beyond (a lost stream, a far catch-up).
inline constexpr float kMirrorSnapM = 5.0f;
const char* DriveAdmit(const DriveRule& rule) noexcept;
// The nearer-player range and the `far` refusal (enemy follow): the squared distance
// from `pos` to the nearer of `host` (only when host_valid: the remote pose is fresh)
// and `self` (only when self_valid), and which one is nearer (true: self); +inf and
// false when neither is valid.
float NearerSquared(const float (&pos)[3], const float (&host)[3], const float (&self)[3], bool host_valid, bool self_valid,
                    bool& self_nearer) noexcept;
// Enemy round 8 track A (range_flat, docs/world-sync-design.md 7.22): the same on the ground plane (x/z; y ignored), as the
// M3 owner rule (world_place.lua P.ownerOf); and the request's reference y set to its target's so Warp's range is flat too.
float NearerSquaredFlat(const float (&pos)[3], const float (&host)[3], const float (&self)[3], bool host_valid, bool self_valid,
                        bool& self_nearer) noexcept;
void FlattenRange(Request& request) noexcept;
// Enemy round 9 track A (bug 2, docs/world-sync-design.md 7.24): the partner pose the nearer-player rules (the host's M3
// placement and soft-follow range, PEER for the owner rule) use. The body driver's command while it has one (as before);
// while this PC hides the partner's body for range (partner_range=hide, beyond 200 m) the driver has none, and the
// partner's network pose sample, at most kRangePoseMaxAgeMs old, stands in (run 8b581a26: 142 of 142 host placements
// refused while the guest's pose read 0). Unknown otherwise.
// Round 9 land step (the review): the network sample only with net_allowed (range_flat named, both roles; off at the
// defaults as 0.15.2). range_hidden also starts true at every 30010/30020 mission start until partner_range=near.
enum class RemoteSource : unsigned char { none, cmd, net };
inline constexpr unsigned kRangePoseMaxAgeMs = 1000;
RemoteSource RemoteRangeSource(bool have_cmd, bool range_hidden, bool net_valid, unsigned net_age_ms, bool net_allowed) noexcept;
const char* RemoteSourceName(RemoteSource source) noexcept;
// The player point a nearer-player range is measured from: the same partner pose the nearer test used when the partner
// is nearer, else this player's (the review: DriveReceipt took the command pose, 0,0,0 while hidden, and was refused).
void RangePlayer(bool partner_nearer, const float (&own)[3], const float (&partner)[3], float (&out)[3]) noexcept;
// Whether a guest process needs the startup Verify() byte gate for either rule.
bool GuestMayWarp(bool lab, bool play, bool key) noexcept;

// One "soldier_warp_census; ..." line ("\r\n" terminated); 0 on failure.
std::size_t FormatCensus(std::uintptr_t base, std::size_t span, std::uint32_t go_id, const Result& result, char* out,
                         std::size_t capacity) noexcept;
// The chain's name in the warp log lines and replies (world_place=warp / host_warp, debug_menu=soldier_warp): pinned,
// typed or engine, the same name the census line prints.
const char* LeafName(const Chain& chain) noexcept;
} // namespace mgscoop::soldier_warp
