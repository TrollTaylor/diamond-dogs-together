#pragma once
// Throw stock protection for instance 1. Installed once, never removed: the
// seven-byte load at 0x108C8E5 takes a permanent near stub through eight
// verified int3 bytes at 0x108C888. One locked dword publishes EB A1 CC CC;
// its unreachable original tail (02 00 00) stays untouched. The stub replays
// mov eax,[r9+0x204], preserves rcx and the loaded eax, then selects the
// game's own non-consuming remote-player branch for id 1 with the armed P,
// the armed key and support class 2/3, in EVERY body-1 job (windowed or not:
// review-1ca3ce6 found unwindowed/refused/pass-through jobs unguarded). Other
// calls resume 0x108C8EC. Key and P lifetime: Arm/Disarm. Enter/Leave only
// count drain windows (telemetry); input admission stays window-bound in the
// hook. No on-disk executable or viewer inventory write.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_support_guard {
using LogFn = void (*)(const char*) noexcept;
const char* Install(const void* base, std::size_t span, bool fingerprint, LogFn log) noexcept;
bool Installed() noexcept;
bool Live() noexcept;
bool Arm(std::uintptr_t p, std::uint32_t id = 1) noexcept;
bool Armed(std::uintptr_t p, std::uint32_t id = 1) noexcept;
void Disarm() noexcept;
bool Enter(std::uintptr_t p, std::uint32_t id = 1) noexcept;
void Leave() noexcept;

struct Stats {
    bool installed, live, armed, in_window;
    std::uint32_t hits, refused, windows;
};
void ReadStats(Stats& out) noexcept;
namespace detail {
// Pure statement of the stub policy; support_class is already decoded. The
// drain window and thread are deliberately not inputs (review-1ca3ce6).
inline bool ShouldForce(std::uint32_t id, std::uint32_t support_class,
                        std::uintptr_t armed_p, std::uintptr_t current_p, bool key) noexcept {
    return key && id == 1 && armed_p != 0 && armed_p == current_p &&
           (support_class == 2 || support_class == 3);
}
inline constexpr std::size_t kStubSize = 0x75, kStubStateAt = 0x0B,
                             kStubRemoteAt = 0x59, kStubBackAt = 0x6D;
void BuildStub(unsigned char (&out)[kStubSize], std::uintptr_t state,
               std::uintptr_t back, std::uintptr_t remote) noexcept;
bool BuildJump(unsigned char (&out)[5], std::uintptr_t site, std::uintptr_t target) noexcept;
// Synthetic-image fixtures only; a retail installation is never withdrawn.
void ResetForTest() noexcept;
}
} // namespace mgscoop::partner_support_guard
