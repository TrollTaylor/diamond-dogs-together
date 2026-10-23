#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::downed_damage {
struct Decision { bool bypass{}; std::uint8_t cause{}; };
Decision Evaluate(bool scoped, std::uint32_t target, std::uint32_t local,
    std::uint16_t attacker, std::uint16_t flags, std::uint8_t body_part,
    float life, float damage, float stun, std::uint8_t mode, bool protected_local) noexcept;
bool UnsafeOfflineSleep(bool exactOfflinePair, std::uint32_t target,
    std::uint16_t attacker, std::uint16_t flags, std::uint8_t body_part,
    float stun) noexcept;
// Original Calc mode 2 is the exact serialized sleep flag, not general stun.
bool NaturalBodyPart(std::uint8_t body_part) noexcept;
// A lethal non-natural-part hit on the local player (the sea/out-of-bounds kill,
// part 12). Evaluate leaves it to the stock death; this names it for the log.
bool EnvironmentKill(bool scoped, std::uint32_t target, std::uint32_t local,
    std::uint8_t body_part, float life, float damage) noexcept;
// An already downed (protected) local player keeps its shield, except for an
// environment kill that would be lethal even at full health (damage >= maximum
// life: the sea / out-of-bounds kill, 10000 against a 7200 maximum). That stays
// the stock death (shared game over) instead of a downed player in the water.
bool ProtectedEnvironmentKill(bool scoped, std::uint32_t target, std::uint32_t local,
    std::uint8_t body_part, float life, float max_life, float damage) noexcept;
bool SleepDamage(std::uint8_t mode, float stun) noexcept;
bool FriendlyReplicaHit(bool scoped, std::uint32_t target, std::uint32_t local,
    std::uint16_t attacker, std::uint16_t flags, std::uint8_t body_part,
    float damage, std::uint8_t mode, float stun) noexcept;
bool CodeGate(const void* image, std::size_t span) noexcept;
bool Install(void* image, std::size_t span, void (*log)(const char*) noexcept) noexcept;
bool StatusLine(char* out, std::size_t capacity) noexcept;
// Cached last natural update observation only; no retail reads/calls.
bool PadStatusLine(char* out, std::size_t capacity) noexcept;
void Shutdown() noexcept;
}
