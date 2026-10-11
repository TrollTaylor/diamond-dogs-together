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
bool SleepDamage(std::uint8_t mode, float stun) noexcept;
bool FriendlyReplicaHit(bool scoped, std::uint32_t target, std::uint32_t local,
    std::uint16_t attacker, std::uint16_t flags, std::uint8_t body_part,
    float damage, std::uint8_t mode, float stun) noexcept;
bool CodeGate(const void* image, std::size_t span) noexcept;
bool Install(void* image, std::size_t span, void (*log)(const char*) noexcept) noexcept;
bool StatusLine(char* out, std::size_t capacity) noexcept;
void Shutdown() noexcept;
}
