#pragma once
// Stock player sleep for either co-op body (MGO/FOB player sleep path).
//
// A player body sleeps natively (Q action 0x84, sleep graph, Zzz, carried-state
// eligibility) when its life record (*(Q+8) + id*0x4C) has stun +0x2C at zero
// and life state +0x34 at 2. The record's stun recovers on its own; while a
// co-op sleep is held it is kept at zero, and waking writes life state 2 -> 0
// so the stock wake-up animation plays. The carried-player state (0x1127336)
// drops a body whose life state returns to 0, so a carried teammate must stay
// in this state until put down. See docs/coop-carry.md.
#include <cstddef>
#include <cstdint>

namespace mgscoop::coop_sleep {

bool Configure(const void* image, std::size_t span) noexcept;
bool Configured() noexcept;

// Holds body id (0 local, 1 teammate) asleep. Idempotent; returns false when
// the body cannot be read. A wounded (not darted) body uses the same hold only
// while it is carried ("limp"), so the stock carried state keeps it.
bool Hold(unsigned id, bool on) noexcept;
bool Held(unsigned id) noexcept;
// Per-frame maintenance on the game's runtime thread.
void Tick() noexcept;

struct Body {
    bool read = false, held = false;
    float stun = 0, stunMax = 0;
    std::uint32_t lifeState = 0;
    std::uint16_t action = 0;
};
Body Read(unsigned id) noexcept;
bool StatusLine(char* out, std::size_t capacity) noexcept;

} // namespace mgscoop::coop_sleep
