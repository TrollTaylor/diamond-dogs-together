#pragma once
// Local downed body, exact-game callbacks only. The coordinator must verify
// the complete damage, pad-binding and existing partner contracts first.
#include <cstdint>
#include "mgscoop/partner_player.hpp"
namespace mgscoop::downed_player {
inline constexpr std::uint32_t kBlockedButtons = 0xFDDFFFFFu; // preserve keyboard W / walk; sticks and look untouched
inline constexpr std::uint32_t kBlockedTriggers = 3u;
inline constexpr std::uint32_t kSleepingButtons = 0xFFFFFFFFu; // immobilized fallback; camera remains retail-owned
inline constexpr std::uint32_t kReviveActionHash = 0x71FE74BFu; // exact Fox.StrCode32("MgsCoopRevive")
struct Reply {
    bool ready, down, held, applied;
    bool reviveMaskObserved, reviveMaskPresent;
    std::uint32_t generation;
    std::uint8_t cause;
    std::int32_t pos[3];
    std::uint32_t hitSequence, hitDamageMilli;
    std::uint8_t hitCause;
    const char* reason;
    bool nativeSleepActive=false, nativeSleepReleaseReady=false;
    bool carryHeld=false; // raw RELOAD/CARRY (R), for horse cargo unloading
};
// Process-owned addresses, after BOTH exact-image gates. Never enable from a
// Lua claim. False leaves all gameplay and damage unchanged.
void Configure(const partner_player::Addresses& addresses, bool verified) noexcept;
// Separate immutable exact transition/seed gate, supplied by the coordinator.
// Toggle is worker/copy-save/local-body0 only and refuses a live downed lease.
void ConfigureNativeSleepTest(bool passed) noexcept;
bool SetNativeSleepTest(bool enabled) noexcept;
// Called by the natural Lua owner. `admitted` is supplied by the native paired
// world/mission/save gate. Commands: 0 poll/arm, 1 confirm lock, 2 exact-generation
// revive, 3 disarm an owned old mission/epoch, 4 read the observed named revive
// ACTION mask (generation argument must equal kReviveActionHash), 5 worker-only
// authorized native sleep wake/preflight while named masks remain. No Lua pointers.
// `boxed` (commands 0 and 1 only): the Lua owner's PlayerStatus CBOX for the
// local player this poll; a down cycle then accepts the box as its posture
// (detail::DownedPosture). It never arms, revives or writes anything itself.
bool Control(std::uint32_t mission, std::uint16_t epoch, bool admitted,
             std::uint8_t command, std::uint32_t generation, Reply& out, bool boxed = false) noexcept;
// Read-only admission used by the coordinator's exact damage hook for both the
// local body and offline partner clone. No legacy Steam/FOB role is consulted.
bool Scope(std::uintptr_t P, std::uint32_t& local) noexcept;
// Cause 1 lethal, 2 teammate sleep. Called BEFORE the game's health/stun apply.
// It latches one generation, keeps positive health and queues prone on the Lua
// owner. Repeated damage while down does not advance the generation.
bool CaptureHit(std::uintptr_t P, std::uint32_t localIndex, std::uint8_t cause) noexcept;
bool IsProtected(std::uintptr_t P, std::uint32_t id) noexcept;
struct Snapshot {
    bool ready, armed, down, controlsApplied;
    std::uint32_t mission, generation;
    std::uint16_t epoch;
    std::uint8_t cause;
    std::uint64_t lastPoll, lastControls;
    float life, maximum;
    const char* reason;
    bool nativeSleepEnabled=false, nativeSleepActive=false;
    float nativeStun=0, nativeMaximumStun=0;
    std::uint8_t nativePhase=0;std::uint32_t nativeSampleSequence=0;std::uint64_t nativeSampleAt=0;
};
Snapshot GetSnapshot() noexcept; // cache only; no game reads/calls
// Numeric diagnostic cache only; this cannot confer input/write/retirement authority.
struct NativeRestDiagnostics {
 bool enabled=false,active=false,aborted=false,descriptorObserved=false;
 std::uint32_t stage=0,generation=0,sampleSequence=0,capSequence=0,currentSequence=0,descriptorFlags=0;
 std::uint64_t checks=0,successfulCas=0,sampledAt=0,capAt=0,pendingAt=0;
 float stun=0,maximumStun=0;const char* reason="none";
};
NativeRestDiagnostics GetNativeRestDiagnostics() noexcept; // cache only; no game reads/calls
bool PassiveOfflineContext(std::uintptr_t P, std::uint32_t& local) noexcept;
// Natural local-attacker0 -> replica1 only, admitted by the exact damage hook.
bool QueueFriendlyHit(std::uintptr_t P, float damage, std::uint8_t cause) noexcept;
// Cmd0 acknowledges one cached outgoing event; cmd1 applies an authenticated
// peer event to this natural owner with exact target cycle and serial guards.
bool FriendlyControl(std::uint32_t mission, std::uint16_t epoch, bool admitted,
    std::uint8_t command, std::uint32_t sequence, std::uint32_t targetGeneration,
    std::uint32_t damageMilli, std::uint8_t cause, Reply& out) noexcept;
// Only the exact natural PP update completion calls this adapter. Original
// retail input has already run once outside our lock; no Lua pointer is admitted.
struct PadObservation {
    bool observed, capped;
    float before, after;
    std::uint32_t local, thread, generation;
    const char* reason;
};
PadObservation AfterPadUpdate(std::uintptr_t pad, bool outerUpdate, bool exactCaller) noexcept;
void Shutdown() noexcept;
namespace detail {
inline constexpr std::uint64_t kScopeLeaseMs = 750;
// Initial hit reactions can postpone a natural stance request. Protect only the
// current fresh owned positive-health cycle for a bounded acquisition interval;
// a confirmed lock that later disappears retains the shorter failure deadline.
inline constexpr std::uint64_t kControlsAcquireMs = 6000;
inline constexpr std::uint64_t kControlsLostMs = 1500;
bool ControlsExpired(std::uint64_t now, std::uint64_t downAt, std::uint64_t lastControls,
    bool previouslyApplied, bool currentlyApplied) noexcept;
// The live local owner's bodies, as read by partner_player::ReadState
// (realized = id and pool set; parts = the parts-load state byte).
struct BodyView { bool realized; std::uint16_t id; std::uint8_t parts; };
// Live(): the local body 0 is realized, id 0 and loaded. Body 1 (the partner)
// is either realized as id 1 or deliberately absent: partner_mount and the
// ride hides clear the all-instance mode while the partner rides a horse, a
// D-Walker or a helicopter (instance 1 unrealized, the stock single-player
// state). Body 1's parts state never decides the local player's protection
// (two-PC test 2026-10-23 item 6: the guest had no downed protection for the
// 14 s of the host's horse ride).
bool LiveBodies(const BodyView& local, const BodyView& partner, std::uint8_t loaded) noexcept;
const char* ControlsPendingReason(bool prone, bool masked, bool crawl) noexcept;
// Two-PC test 2026-10-23 item 4: friendly fire downed a host who was in a
// cardboard box. The prone request cannot take a boxed body
// (controls_pending_prone x6, then controls_timeout_prone), and the timeout
// latched `failed` for the rest of the mission: no downed safety net, every
// later poll controls_timeout, three darts ignored.
// The posture a non-native down cycle accepts this poll: the stock sleep graph
// owns its own posture; the local player's own cardboard box (the Lua owner
// reports PlayerStatus CBOX) is accepted as it is: no prone request, the box
// is already a low posture, and the named masks and the input cap still apply.
// Otherwise the prone stance request runs and stance 3 is required.
enum class Posture : std::uint8_t { prone_request, sleep, box };
Posture DownedPosture(bool nativeSleep, bool boxed) noexcept;
// AfterPadUpdate: body 0's current stance keeps the applied controls (prone,
// or the box posture this cycle accepted).
bool PostureHeld(std::uint8_t stance, bool box) noexcept;
// A controls timeout ends the cycle (health and crawl restored) but never
// latches the owner off: the abandoned cycle's generation is retired (a new
// one starts, so Lua and the peer see an abandoned cycle, never a revive) and
// the next poll with a good Live() re-arms.
struct TimeoutPlan { bool clear, latch, newGeneration; };
TimeoutPlan ControlsTimeout() noexcept;
// Cap freshly computed movement magnitude only. Heading, look, source axes and
// deadzone remain retail-owned. Next natural update restores current real input.
inline constexpr float kDownedInputMagnitude = .25f;
struct PadScope {
    bool freshUpdate, outerUpdate, exactCaller, verified, stopped, failed, armed;
    std::uint32_t ownerThread, thread;
    std::uint64_t now, poll, sequenceBefore, sequenceAfter;
    bool batchBefore, batchAfter;
};
const char* PadRefusal(const PadScope& scope) noexcept;
struct PadPlan { bool valid, write; float magnitude; };
PadPlan PlanPadMagnitude(float current, bool down, bool controlsApplied, std::uint8_t cause = 1) noexcept;
std::uint32_t ButtonsForCause(std::uint8_t cause) noexcept;
// Sleep retains the exact pre-dart life; it does not apply lethal injury or grant healing.
float IncapacitatedHealth(std::uint8_t cause, float original, float maximum) noexcept;
float RecoveredHealth(std::uint8_t cause, float original, float maximum) noexcept;
// Expired or reversed owner polls cannot authorize damage protection.
bool FreshScope(std::uint64_t now, std::uint64_t poll) noexcept;
inline constexpr std::uint64_t kFriendlyRecoveryMs = 4000; // exact retail ordinary hurt timer
// Per-record downed crawl cap. Only base+product are owned; retail factor/dash remain untouched.
inline constexpr float kDownedCrawlBase = .5f; // existing wire minimum; half the ordinary base
struct CrawlLease { bool applied; float original, written; };
struct CrawlPlan { bool valid; bool write; float base, product; CrawlLease next; };
struct CrawlIdentity { std::uintptr_t player, state, service, record; };
enum class CrawlOwner : std::uint8_t { retry, owned, retired };
// Both current identities must come from complete typed reads; HP is irrelevant to speed restore.
CrawlOwner CheckCrawlOwner(const CrawlIdentity& bound, bool firstValid, const CrawlIdentity& first,
    bool secondValid, const CrawlIdentity& second) noexcept;
CrawlPlan PlanCrawl(const CrawlLease& lease, float base, float factor, float product, bool restore) noexcept;
struct RegenCeiling { std::uint64_t at; float life; bool active; };
// A bounded recovery lease only suppresses increases; lower native HP is retained.
float RegenTarget(RegenCeiling& state, std::uint64_t now, float current, bool alive) noexcept;
struct Cycle { std::uint32_t generation; bool down; std::uint8_t cause; };
void NewScene(Cycle& state) noexcept;
bool Enter(Cycle& state, std::uint8_t cause) noexcept;
bool Revive(Cycle& state, std::uint32_t generation) noexcept;
float ProtectedHealth(float maximum) noexcept;
float RevivedHealth(float maximum) noexcept;
// Exact positive ceil conversion used by the game's vars.playerLife mirror.
// Rejects unsupported U16 maxima and values before either native field write.
bool HealthWord(float current, float maximum, std::uint16_t& word) noexcept;
// Classify the exact lease drift without reading pointers or mutating native state.
enum class IdentityChange : std::uint8_t { none, player, state, life, vars, maximum };
struct Identity { std::uintptr_t player, state, life, vars; float maximum; };
IdentityChange CompareIdentity(const Identity& owned, const Identity& current) noexcept;
// Rebind is a new healthy poll lease, never a way to move an active down cycle.
bool HealthyRebind(IdentityChange change, bool down, std::uint8_t command, bool coherent) noexcept;
const char* IdentityReason(IdentityChange change) noexcept;
const char* RebindReason(IdentityChange change) noexcept;
struct ButtonMaskEntry { std::uint32_t name, buttons; };
// Read-only coherent retail OR; present reports one exact owned ACTION entry.
// Coherent absence is observable but never permits raw ACTION use.
bool ReviveActionMask(const ButtonMaskEntry (&entries)[8], std::uint32_t aggregate, std::uint32_t& otherButtons, bool& present) noexcept;
}
} // namespace mgscoop::downed_player
