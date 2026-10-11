#include "mgscoop/downed_damage.hpp"
#include <cmath>
namespace mgscoop::downed_damage {
bool NaturalBodyPart(std::uint8_t body_part) noexcept { return body_part <= 11; }
bool SleepDamage(std::uint8_t mode, float stun) noexcept {
    return mode == 2 && std::isfinite(stun) && stun > 0;
}
bool UnsafeOfflineSleep(bool exactOfflinePair, std::uint32_t target,
    std::uint16_t /*attacker*/, std::uint16_t flags, std::uint8_t body_part, float stun) noexcept {
    // Exact updater11F8F7D..904B has no attacker-id admission check: it copies
    // the raw attacker word into the notification. NPC, self and sentinel IDs
    // can reach the same offline-null synchronizer as a teammate. This guard
    // retains the exact paired-player/direct/part-zero/positive-stun subset,
    // independently of the active downed lease; it never grants a sleep state.
    return exactOfflinePair && target <= 1 &&
        (flags & 2u) && body_part == 0 && std::isfinite(stun) && stun > 0;
}
bool FriendlyReplicaHit(bool scoped, std::uint32_t target, std::uint32_t local,
    std::uint16_t attacker, std::uint16_t flags, std::uint8_t body_part,
    float damage, std::uint8_t mode, float stun) noexcept {
    return scoped && target == 1 && local == 0 && attacker == 0 &&
        (flags & 1u) && NaturalBodyPart(body_part) && std::isfinite(damage) && damage >= 0 &&
        (damage > 0 || SleepDamage(mode, stun));
}
Decision Evaluate(bool scoped, std::uint32_t target, std::uint32_t local,
    std::uint16_t attacker, std::uint16_t flags, std::uint8_t body_part,
    float life, float damage, float stun, std::uint8_t mode, bool protected_local) noexcept {
    if (!scoped || target > 1 || local > 1 || !std::isfinite(life) || life <= 0 ||
        life > 1e8f || !std::isfinite(damage) || !std::isfinite(stun)) return {};
    if (target == local && protected_local) return {true, 0};
    const bool sleep = attacker <= 1 && attacker != target && (flags & 2u) &&
        NaturalBodyPart(body_part) && SleepDamage(mode, stun);
    const bool lethal = damage >= life && damage > 0;
    if (!sleep && !lethal) return {};
    // The replica cannot die or enter the base game's online sleep state.
    // Actual friendly hits are handed off separately to the real owner.
    if (target != local) return {true, 0};
    return {true, static_cast<std::uint8_t>(sleep ? 2 : 1)};
}
}
