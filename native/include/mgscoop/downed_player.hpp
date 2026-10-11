#pragma once
// Local downed body, exact-game callbacks only. The coordinator must verify
// the complete damage, pad-binding and existing partner contracts first.
#include <cstdint>
#include "mgscoop/partner_player.hpp"
namespace mgscoop::downed_player {
inline constexpr std::uint32_t kBlockedButtons = 0xFDDFFFFFu; // preserve keyboard W / walk; sticks and look untouched
inline constexpr std::uint32_t kBlockedTriggers = 3u;
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
};
// Process-owned addresses, after BOTH exact-image gates. Never enable from a
// Lua claim. False leaves all gameplay and damage unchanged.
void Configure(const partner_player::Addresses& addresses, bool verified) noexcept;
// Called by the natural Lua owner. `admitted` is supplied by the native paired
// world/mission/save gate. Commands: 0 poll/arm, 1 confirm lock, 2 exact-generation
// revive, 3 disarm an owned old mission/epoch, 4 read the observed named revive
// ACTION mask (generation argument must equal kReviveActionHash). No Lua pointers are accepted.
bool Control(std::uint32_t mission, std::uint16_t epoch, bool admitted,
             std::uint8_t command, std::uint32_t generation, Reply& out) noexcept;
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
};
Snapshot GetSnapshot() noexcept; // cache only; no game reads/calls
bool PassiveOfflineContext(std::uintptr_t P, std::uint32_t& local) noexcept;
// Natural local-attacker0 -> replica1 only, admitted by the exact damage hook.
bool QueueFriendlyHit(std::uintptr_t P, float damage, std::uint8_t cause) noexcept;
// Cmd0 acknowledges one cached outgoing event; cmd1 applies an authenticated
// peer event to this natural owner with exact target cycle and serial guards.
bool FriendlyControl(std::uint32_t mission, std::uint16_t epoch, bool admitted,
    std::uint8_t command, std::uint32_t sequence, std::uint32_t targetGeneration,
    std::uint32_t damageMilli, std::uint8_t cause, Reply& out) noexcept;
void Shutdown() noexcept;
namespace detail {
inline constexpr std::uint64_t kScopeLeaseMs = 750;
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
