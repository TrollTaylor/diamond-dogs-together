#pragma once
// Offline partner spike crash probe (docs/offline-partner-spike-plan.md 2.3
// item 8). A vectored exception handler that only LOGS: an access violation
// whose faulting instruction lies inside the game image is reported once per
// distinct RVA (first 16), then the search continues unchanged
// (EXCEPTION_CONTINUE_SEARCH). It never handles, resumes or alters anything.
//   crash_probe=installed
//   crash_probe=av; rva=0x..; access=read|write|execute; target=0x..; thread=..; stage=..; realized=..; ctx=0x..
#include <cstddef>
#include <cstdint>

namespace mgscoop::crash_probe {

using LogFn = void (*)(const char* line) noexcept;
inline constexpr unsigned kMaxDistinct = 16;

// Installs once for the process lifetime (the DLL is pinned). Returns true
// when the handler is installed (now or earlier).
bool Install(const void* image_base, std::size_t image_span, LogFn log) noexcept;
// Context copied into each line; updated by the partner tick handler. flags
// bit0 = input window armed, bit1 = DM patch applied.
void SetContext(std::uint32_t stage, std::uint32_t realized, std::uint32_t flags) noexcept;

namespace detail {
// Pure filter/formatter used by the handler (test seam). Returns the length
// of the line written, or 0 when the exception is not reported. `seen` is the
// distinct-RVA table (kMaxDistinct entries, 0 = free) updated atomically.
std::size_t Describe(std::uint32_t code, std::uintptr_t address, std::uintptr_t access, std::uintptr_t target,
                     std::uintptr_t base, std::size_t span, std::uint32_t thread, std::uint32_t stage,
                     std::uint32_t realized, std::uint32_t flags, volatile long* seen, char* out, std::size_t capacity) noexcept;
}

} // namespace mgscoop::crash_probe
