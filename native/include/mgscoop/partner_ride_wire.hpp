#pragma once
// Channel 2 ride state. The authenticated link session owns epoch rejection;
// this 12-byte append-only body intentionally carries no mission/native token.
#include <cstddef>
#include <cstdint>
namespace mgscoop::partner_ride_wire {
inline constexpr std::uint8_t kChannel=2, kMagic=0x52, kVersion=1;
inline constexpr std::size_t kSize=12;
// Exact current-build heli handler census: move-design.md section2, including
// immediate writers98/99/101/103/105/106/107/108 and observed flight100/102.
inline constexpr std::uint16_t kHeliActFirst=98, kHeliActLast=108;
inline constexpr std::uint64_t kHeartbeatMs=2000, kLeaseMs=5000;
enum class Kind : std::uint8_t { foot, heli, vehicle, horse, walker, other };
struct Packet { Kind kind{}; std::uint16_t revision{}; std::uint32_t sender_ms{}; };
bool IsRide(const std::uint8_t*, std::size_t) noexcept;
bool Encode(const Packet&, std::uint8_t (&)[kSize]) noexcept;
bool Decode(const std::uint8_t*, std::size_t, Packet&) noexcept;
struct Store { bool have{}; Packet packet{}; std::uint32_t epoch{}; std::uint64_t received_ms{}; unsigned received{}, invalid{}, stale{}, duplicates{}; };
bool Accept(Store&, const std::uint8_t*, std::size_t, std::uint32_t epoch, std::uint64_t now) noexcept;
bool Live(const Store&, std::uint32_t epoch, std::uint64_t now) noexcept;
// Pure hide/re-show policy. Repeated/held sender samples cannot prove landing.
struct Visibility {
    bool hidden{}, air{}, dead_retry{}, dead_locked{}, have_sample{}, stable{};
    std::int64_t sender_ms{}, stable_since{};
    std::uint32_t generation{};
    double x{}, y{}, z{};
    std::uint64_t place_ms{}, retry_ms{};
    const char* reason{};
};
struct In {
    bool fresh{}, time_valid{}, jump{}, anim_valid{}, body_anim_valid{}, body_valid{}, ride_have{}, ride_live{}, lease_live{}, rearmed{};
    Kind kind{}; std::uint16_t remote_act{}, body_act{};
    std::uint32_t generation{};
    std::int64_t sender_ms{}; double x{}, y{}, z{}, body_y{}; std::uint64_t now_ms{};
};
void Observe(Visibility&, const In&) noexcept;
void Placed(Visibility&, std::uint64_t now) noexcept;
}
