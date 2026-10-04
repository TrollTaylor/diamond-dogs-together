// Original MGS Co-op crash probe: log-only vectored handler for game-image AVs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstring>
#include "mgscoop/crash_probe.hpp"

namespace mgscoop::crash_probe {
namespace {
// Constant-initialized process state; the handler is never removed.
volatile LONG g_installed = 0;
PVOID g_handle = nullptr;
std::uintptr_t g_base = 0;
std::size_t g_span = 0;
LogFn g_log = nullptr;
volatile LONG g_stage = 0, g_realized = 0;
volatile long g_seen[kMaxDistinct]{};

LONG CALLBACK Handler(PEXCEPTION_POINTERS info) noexcept {
    if (!info || !info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    if (record.ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !g_log) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD error = GetLastError();
    const std::uintptr_t access = record.NumberParameters >= 1 ? record.ExceptionInformation[0] : 0;
    const std::uintptr_t target = record.NumberParameters >= 2 ? record.ExceptionInformation[1] : 0;
    char line[224]{};
    if (detail::Describe(record.ExceptionCode, reinterpret_cast<std::uintptr_t>(record.ExceptionAddress), access, target,
                         g_base, g_span, GetCurrentThreadId(), static_cast<std::uint32_t>(g_stage),
                         static_cast<std::uint32_t>(g_realized), g_seen, line, sizeof(line)))
        g_log(line);
    SetLastError(error);
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

namespace detail {
std::size_t Describe(std::uint32_t code, std::uintptr_t address, std::uintptr_t access, std::uintptr_t target,
                     std::uintptr_t base, std::size_t span, std::uint32_t thread, std::uint32_t stage,
                     std::uint32_t realized, volatile long* seen, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity || !seen) return 0;
    out[0] = 0;
    if (code != EXCEPTION_ACCESS_VIOLATION || !base || address < base || address - base >= span) return 0;
    const std::uintptr_t rva = address - base;
    if (rva == 0 || rva > 0x7FFFFFFF) return 0;
    // Claim a slot for a new RVA; an RVA already present (or a full table) is silent.
    bool claimed = false;
    for (unsigned i = 0; i < kMaxDistinct && !claimed; ++i) {
        const long current = seen[i];
        if (current == static_cast<long>(rva)) return 0;
        if (current == 0) {
            const long previous = InterlockedCompareExchange(&seen[i], static_cast<long>(rva), 0);
            if (previous == 0) claimed = true;
            else if (previous == static_cast<long>(rva)) return 0;
        }
    }
    if (!claimed) return 0;
    const char* kind = access == 1 ? "write" : access == 8 ? "execute" : "read";
    if (FAILED(StringCchPrintfA(out, capacity,
            "crash_probe=av; rva=0x%llx; access=%s; target=0x%llx; thread=%lu; stage=%u; realized=%u\r\n",
            static_cast<unsigned long long>(rva), kind, static_cast<unsigned long long>(target),
            static_cast<unsigned long>(thread), stage, realized))) {
        out[0] = 0;
        return 0;
    }
    return std::strlen(out);
}
} // namespace detail

bool Install(const void* image_base, std::size_t image_span, LogFn log) noexcept {
    if (!image_base || !image_span || !log) return false;
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0) return g_handle != nullptr;
    const DWORD error = GetLastError();
    g_base = reinterpret_cast<std::uintptr_t>(image_base);
    g_span = image_span;
    g_log = log;
    // First = 0: called after any earlier vectored handlers; never handles.
    g_handle = AddVectoredExceptionHandler(0, &Handler);
    log(g_handle ? "crash_probe=installed\r\n" : "crash_probe=unavailable; reason=add_handler\r\n");
    SetLastError(error);
    return g_handle != nullptr;
}

void SetContext(std::uint32_t stage, std::uint32_t realized) noexcept {
    InterlockedExchange(&g_stage, static_cast<LONG>(stage));
    InterlockedExchange(&g_realized, static_cast<LONG>(realized));
}

} // namespace mgscoop::crash_probe
