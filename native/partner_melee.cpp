// Melee on the partner: the attacker-side reach test (partner_melee.hpp). Scaffold.
#include "mgscoop/partner_melee.hpp"

namespace mgscoop::partner_melee {

bool StrikeAct(std::uint16_t) noexcept { return false; }

const char* VerdictName(Verdict) noexcept { return "none"; }

void Reset(State& s) noexcept { s = State{}; }

Out Step(State&, const In&) noexcept { return Out{}; }

std::size_t FormatStrike(const State&, std::uint16_t, const Out&, bool, char* out, std::size_t capacity) noexcept {
    if (out && capacity) out[0] = 0;
    return 0;
}

} // namespace mgscoop::partner_melee
