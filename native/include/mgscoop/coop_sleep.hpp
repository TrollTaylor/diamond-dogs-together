#pragma once
// Native online-player sleep controller from the fingerprinted TPP executable.
// Only current typed two-body offline owners are eligible. The game's original
// onset/rest/wake graph runs naturally; no mode, graph or callback is forced.
#include <cstddef>
#include <cstdint>
namespace mgscoop::coop_sleep {
bool Configure(const void* image,std::size_t span) noexcept;
bool Configured() noexcept;
// Holds a current healthy body asleep. Repeated calls never reseed a wake.
// False while releasing means native put-down/wake/retirement is still pending;
// callers must retain their logical sleep and input restrictions and retry.
bool Hold(unsigned id,bool on) noexcept;
bool Held(unsigned id) noexcept;
bool Active(unsigned id) noexcept;
bool WakeReady(unsigned id) noexcept;
bool CarryReady(unsigned id) noexcept;
bool DetailLine(unsigned id,char*,std::size_t) noexcept;
void Tick() noexcept;
struct Body {
 bool read=false,held=false;
 float stun=0,stunMax=0;
 std::uint32_t lifeState=0;
 std::uint16_t action=0;
 bool sleeping=false,waking=false,retired=false;
 const char* reason="absent";
};
Body Read(unsigned id) noexcept;
bool StatusLine(char* out,std::size_t capacity) noexcept;
} // namespace mgscoop::coop_sleep
