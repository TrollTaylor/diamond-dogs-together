// Supply drops synced (docs/world-sync-protocol.md section 25): the observer's portable core. RED stub (r1101 step 1).
#include "mgscoop/supply_sync.hpp"

namespace mgscoop::supply_sync {
bool DecodeRequest(const unsigned char*, std::size_t, Event&) noexcept { return false; }
std::uint32_t Ring::Push(const Event&) noexcept { return 0; }
std::size_t Ring::Since(std::uint32_t, Event*, std::size_t, std::uint32_t& lost) const noexcept { lost = 0; return 0; }
void Ring::Clear() noexcept {}
bool ParseCommand(const char*, std::size_t, Parsed&) noexcept { return false; }
std::size_t FormatPoll(const Snapshot&, const Event*, std::size_t, std::uint32_t, std::uint32_t, char*, std::size_t) noexcept { return 0; }
std::size_t FormatLogLine(const Event&, char*, std::size_t) noexcept { return 0; }
} // namespace mgscoop::supply_sync
