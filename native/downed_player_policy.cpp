#include "mgscoop/downed_player.hpp"
#include <cmath>
#include <cstring>
namespace mgscoop::downed_player {
namespace detail {
namespace {
bool SameCrawl(float a, float b) noexcept { return std::memcmp(&a, &b, sizeof(float)) == 0; }
}
bool ControlsExpired(std::uint64_t now, std::uint64_t downAt, std::uint64_t lastControls,
    bool previouslyApplied, bool currentlyApplied) noexcept {
    // Confirm this frame before considering age; a paused still-locked body
    // never fails merely because its first hit timestamp is old.
    if (currentlyApplied) return false;
    const auto at = previouslyApplied ? lastControls : downAt;
    const auto limit = previouslyApplied ? kControlsLostMs : kControlsAcquireMs;
    return now < at || now - at > limit;
}
const char* ControlsPendingReason(bool prone, bool masked, bool crawl) noexcept {
    if (!prone) return "controls_pending_prone";
    if (!masked) return "controls_pending_mask";
    if (!crawl) return "controls_pending_crawl";
    return "downed";
}
const char* PadRefusal(const PadScope& s) noexcept {
    if (!s.freshUpdate) return "not_fresh";
    if (!s.outerUpdate) return "nested";
    if (!s.exactCaller) return "caller";
    if (!s.verified || s.stopped || s.failed || !s.armed) return "context";
    if (!s.ownerThread || s.ownerThread != s.thread) return "thread";
    if (!FreshScope(s.now, s.poll)) return "expired";
    if ((s.sequenceBefore & 1u) || s.sequenceBefore != s.sequenceAfter) return "window";
    if (s.batchBefore || s.batchAfter) return "batch";
    return nullptr;
}
PadPlan PlanPadMagnitude(float current, bool down, bool controlsApplied, std::uint8_t cause) noexcept {
    if (!std::isfinite(current) || current < 0 || current > 1 || (down && cause != 1 && cause != 2)) return {};
    const float limit = cause == 2 ? 0.f : kDownedInputMagnitude;
    const float target = down && controlsApplied && current > limit ? limit : current;
    return {true, !SameCrawl(target, current), target};
}
std::uint32_t ButtonsForCause(std::uint8_t cause) noexcept {
    return cause == 1 ? kBlockedButtons : cause == 2 ? kSleepingButtons : 0;
}
float IncapacitatedHealth(std::uint8_t cause, float original, float max) noexcept {
    std::uint16_t word = 0;
    if (!HealthWord(original, max, word) || (cause != 1 && cause != 2)) return 0;
    return cause == 2 ? original : ProtectedHealth(max);
}
float RecoveredHealth(std::uint8_t cause, float original, float max) noexcept {
    std::uint16_t word = 0;
    if (!HealthWord(original, max, word) || (cause != 1 && cause != 2)) return 0;
    return cause == 2 ? original : RevivedHealth(max);
}
CrawlOwner CheckCrawlOwner(const CrawlIdentity& bound, bool firstValid, const CrawlIdentity& first,
    bool secondValid, const CrawlIdentity& second) noexcept {
    if (!firstValid || !secondValid || !bound.player || !bound.state || !bound.service || !bound.record ||
        !first.player || !first.state || !first.service || !first.record ||
        first.player != second.player || first.state != second.state ||
        first.service != second.service || first.record != second.record) return CrawlOwner::retry;
    return first.player == bound.player && first.state == bound.state &&
        first.service == bound.service && first.record == bound.record ? CrawlOwner::owned : CrawlOwner::retired;
}
CrawlPlan PlanCrawl(const CrawlLease& lease, float base, float factor, float product, bool restore) noexcept {
    CrawlPlan p{};
    // Coherent retail product is required before either proposed write.
    if (!std::isfinite(base) || !std::isfinite(factor) || !std::isfinite(product) ||
        base <= 0 || base > 16 || factor <= 0 || factor > 16 || !SameCrawl(base * factor, product)) return p;
    if (lease.applied && (!std::isfinite(lease.original) || !std::isfinite(lease.written) ||
        lease.original <= 0 || lease.original > 16 || lease.written <= 0 || lease.written > 16)) return p;
    p.valid = true; p.base = base; p.product = product;
    if (restore) {
        // A retail rewrite takes priority; never put an old original over it.
        if (lease.applied && SameCrawl(base, lease.written)) p.base = lease.original;
    } else {
        p.next.applied = true;
        p.next.original = lease.applied && SameCrawl(base, lease.written) ? lease.original : base;
        p.next.written = p.next.original < kDownedCrawlBase ? p.next.original : kDownedCrawlBase;
        p.base = p.next.written;
    }
    p.product = p.base * factor;
    p.write = !SameCrawl(p.base, base) || !SameCrawl(p.product, product);
    return p;
}
bool FreshScope(std::uint64_t now, std::uint64_t poll) noexcept {
    return now >= poll && now - poll <= kScopeLeaseMs;
}
IdentityChange CompareIdentity(const Identity& owned, const Identity& current) noexcept {
    if (owned.player != current.player) return IdentityChange::player;
    if (owned.state != current.state) return IdentityChange::state;
    if (owned.life != current.life) return IdentityChange::life;
    if (owned.vars != current.vars) return IdentityChange::vars;
    if (owned.maximum != current.maximum) return IdentityChange::maximum;
    return IdentityChange::none;
}
bool HealthyRebind(IdentityChange change, bool down, std::uint8_t command, bool coherent) noexcept {
    return change != IdentityChange::none && !down && command == 0 && coherent;
}
const char* IdentityReason(IdentityChange change) noexcept {
    switch (change) {
    case IdentityChange::player: return "identity_player";
    case IdentityChange::state: return "identity_state";
    case IdentityChange::life: return "identity_life";
    case IdentityChange::vars: return "identity_vars";
    case IdentityChange::maximum: return "identity_maximum";
    default: return "identity";
    }
}
const char* RebindReason(IdentityChange change) noexcept {
    switch (change) {
    case IdentityChange::player: return "alive_rebound_player";
    case IdentityChange::state: return "alive_rebound_state";
    case IdentityChange::life: return "alive_rebound_life";
    case IdentityChange::vars: return "alive_rebound_vars";
    case IdentityChange::maximum: return "alive_rebound_maximum";
    default: return "alive";
    }
}
bool ReviveActionMask(const ButtonMaskEntry (&entries)[8], std::uint32_t aggregate, std::uint32_t& otherButtons, bool& present) noexcept {
    otherButtons = 0; present = false;
    std::uint32_t combined = 0, other = 0;
    bool owned = false;
    for (unsigned i = 0; i < 8; ++i) {
        const auto& entry = entries[i];
        if (!entry.name) continue; // Retail ignores stale values in vacant entries.
        for (unsigned j = 0; j < i; ++j) if (entries[j].name == entry.name) return false;
        combined |= entry.buttons;
        if (entry.name == kReviveActionHash) {
            if (entry.buttons != 0x20u) return false;
            owned = true;
        } else other |= entry.buttons;
    }
    if (combined != aggregate) return false;
    otherButtons = other; present = owned;
    return true;
}
float RegenTarget(RegenCeiling& s, std::uint64_t now, float current, bool alive) noexcept {
    if (!s.active) return current;
    if (!alive || !std::isfinite(current) || current <= 0 || !std::isfinite(s.life) || s.life <= 0 ||
        now < s.at || now - s.at >= kFriendlyRecoveryMs) {
        s = {}; return current;
    }
    if (current < s.life) s.life = current;
    return s.life;
}
void NewScene(Cycle& s) noexcept {
    ++s.generation; if (!s.generation) ++s.generation;
    s.down = false; s.cause = 0;
}
bool Enter(Cycle& s, std::uint8_t cause) noexcept {
    if (cause != 1 && cause != 2) return false;
    if (s.down) return true;
    ++s.generation; if (!s.generation) ++s.generation;
    s.down = true; s.cause = cause; return true;
}
bool Revive(Cycle& s, std::uint32_t generation) noexcept {
    if (!s.down || !generation || s.generation != generation) return false;
    s.down = false; s.cause = 0; return true;
}
bool HealthWord(float current, float max, std::uint16_t& word) noexcept {
    word = 0;
    if (!std::isfinite(current) || !std::isfinite(max) || max < 1.f || max > 65535.f || current <= 0 || current > max) return false;
    const auto rounded = std::ceil(current);
    if (rounded < 1.f || rounded > 65535.f) return false;
    word = static_cast<std::uint16_t>(rounded); return true;
}
float ProtectedHealth(float max) noexcept {
    return std::isfinite(max) && max >= 1.0f && max <= 1e8f ? (max * .05f >= 1.0f ? max * .05f : 1.0f) : 0.0f;
}
float RevivedHealth(float max) noexcept {
    return std::isfinite(max) && max >= 1.0f && max <= 1e8f ? (max * .5f >= 1.0f ? max * .5f : 1.0f) : 0.0f;
}
}

} // namespace mgscoop::downed_player
