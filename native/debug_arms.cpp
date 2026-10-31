// Debug menu arms (docs/debug-menu.md "Give weapon" and "Friendly fire"): red stub, filled in by the green commit.
#include "mgscoop/debug_arms.hpp"

namespace mgscoop::debug_arms {
std::size_t FormatResidentReply(bool, std::uint32_t, const Entry*, std::size_t, char*, std::size_t) noexcept { return 0; }
bool ParseState(const char*, std::size_t, State&) noexcept { return false; }
void Store(const State&) noexcept {}
State Get() noexcept { return State{}; }
const char* KindName(std::uint8_t) noexcept { return nullptr; }
bool FormatGunLabel(const Gun&, char*, std::size_t) noexcept { return false; }
const Support* SupportItems(std::size_t& count) noexcept { count = 0; return nullptr; }
bool FormatGiveGun(char*, std::size_t, std::uint16_t) noexcept { return false; }
bool FormatGiveSupport(char*, std::size_t, const char*) noexcept { return false; }
bool FormatFriendlyFire(char*, std::size_t, bool) noexcept { return false; }
} // namespace mgscoop::debug_arms
