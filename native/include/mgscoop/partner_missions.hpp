#pragma once

#include <cstdint>

namespace mgscoop {
// The locally generated runtime contains one two-slot controller for each
// normal free-roam pack. ACCs, story missions and the f30011 demo stay out.
// Lua additionally checks that this load selected the replacement pack.
constexpr bool SupportsPartnerBody(std::uint32_t mission) noexcept {
    return mission == 30010 || mission == 30020 || mission == 30050;
}
} // namespace mgscoop
