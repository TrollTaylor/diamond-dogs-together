// Original MGS Co-op native puppet: the phase-1 local decision detour
// (docs/native-puppet.md "Phase-1 detour"; partner_local_detour.hpp).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstring>
#include "mgscoop/partner_local_detour.hpp"
#include "mgscoop/near_code.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_local_detour {
namespace {
namespace c = mgscoop::partner_player_contract;

// The state the stub reads (layout fixed by BuildStub: +0 id, +4 hits, +8 tid,
// +0xC refused, +0x10 P). id -1 = off.
struct alignas(8) State {
    volatile LONG id;
    volatile LONG hits;
    volatile LONG tid;
    volatile LONG refused;
    volatile LONG64 p;
};
static_assert(sizeof(LONG) == 4 && offsetof(State, hits) == 4 && offsetof(State, tid) == 8 && offsetof(State, refused) == 0xC &&
              offsetof(State, p) == 0x10, "stub state layout");
State g_state{-1, 0, 0, 0, 0};
volatile LONG g_windows = 0;
volatile LONG g_installed = 0;
bool g_ready = false;
void* g_stub = nullptr;

// The original bytes the install requires: cmp edx,[rax+0x218]; setne bpl, and
// 13 bytes of int3 padding after the ret of 0x109E060.
inline constexpr unsigned char kCmpSetne[] = {0x3B, 0x90, 0x18, 0x02, 0x00, 0x00, 0x40, 0x0F, 0x95, 0xC5};
inline constexpr std::size_t kPadSize = c::p1_pad_end_rva - c::p1_pad_begin_rva;

bool BytesOk(std::uintptr_t base) noexcept {
    if (std::memcmp(reinterpret_cast<const void*>(base + c::p1_cmp_rva), kCmpSetne, sizeof(kCmpSetne)) != 0) return false;
    const auto* pad = reinterpret_cast<const unsigned char*>(base + c::p1_pad_begin_rva);
    for (std::size_t i = 0; i < kPadSize; ++i)
        if (pad[i] != 0xCC) return false;
    return true;
}

// The padding jump: never executed before step 3 publishes the rel8 jump to
// it, so a plain write under a temporary protection change is enough.
const char* WritePadJump(std::uintptr_t site, const unsigned char (&bytes)[5]) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    DWORD previous = 0, ignored = 0;
    void* const at = reinterpret_cast<void*>(site);
    if (!VirtualQuery(at, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        !VirtualProtect(at, sizeof(bytes), near_code::WritableFor(info.Protect), &previous)) return "protect";
    std::memcpy(at, bytes, sizeof(bytes));
    FlushInstructionCache(GetCurrentProcess(), at, sizeof(bytes));
    return VirtualProtect(at, sizeof(bytes), previous, &ignored) ? nullptr : "protect_restore";
}
} // namespace

namespace detail {
void BuildStub(unsigned char (&s)[kStubSize], std::uintptr_t state, std::uintptr_t back) noexcept {
    static constexpr unsigned char kCode[kStubSize] = {
        0x40, 0x0F, 0x95, 0xC5,                          // 00 setne bpl (flags of the cmp at 0x109E17C)
        0x51,                                            // 04 push rcx
        0x48, 0xB9, 0, 0, 0, 0, 0, 0, 0, 0,              // 05 mov rcx, state
        0x3B, 0x11,                                      // 0F cmp edx, [rcx]        (the forced id; -1 = off)
        0x75, 0x22,                                      // 11 jne 0x35
        0x48, 0x3B, 0x41, 0x10,                          // 13 cmp rax, [rcx+0x10]   (P)
        0x75, 0x18,                                      // 17 jne 0x31
        0x50,                                            // 19 push rax
        0x65, 0x8B, 0x04, 0x25, 0x48, 0x00, 0x00, 0x00,  // 1A mov eax, gs:[0x48]    (TEB thread id)
        0x3B, 0x41, 0x08,                                // 22 cmp eax, [rcx+8]
        0x58,                                            // 25 pop rax (flags kept)
        0x75, 0x09,                                      // 26 jne 0x31
        0x40, 0xB5, 0x00,                                // 28 mov bpl, 0            (local branch)
        0xF0, 0xFF, 0x41, 0x04,                          // 2B lock inc dword [rcx+4] (hits)
        0xEB, 0x04,                                      // 2F jmp 0x35
        0xF0, 0xFF, 0x41, 0x0C,                          // 31 lock inc dword [rcx+0xC] (refused)
        0x59,                                            // 35 pop rcx
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,              // 36 jmp [rip]
        0, 0, 0, 0, 0, 0, 0, 0};                         // 3C back
    std::memcpy(s, kCode, kStubSize);
    std::memcpy(s + kStubStateAt, &state, 8);
    std::memcpy(s + kStubBackAt, &back, 8);
}
bool BuildJump(unsigned char (&out)[5], std::uintptr_t site, std::uintptr_t target) noexcept {
    std::int32_t rel = 0;
    if (!near_code::Rel32(site + 5, target, rel)) return false;
    out[0] = 0xE9;
    std::memcpy(out + 1, &rel, 4);
    return true;
}
void ResetForTest() noexcept {
    InterlockedExchange(&g_installed, 0);
    g_ready = false;
    g_stub = nullptr;
    InterlockedExchange(&g_state.id, -1);
    InterlockedExchange(&g_state.hits, 0);
    InterlockedExchange(&g_state.refused, 0);
    InterlockedExchange(&g_windows, 0);
}
ForceState ReadForceForTest() noexcept {
    return ForceState{static_cast<std::int32_t>(g_state.id), static_cast<std::uint32_t>(g_state.tid),
                      static_cast<std::uintptr_t>(g_state.p)};
}
} // namespace detail

const char* Install(const void* base_ptr, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0) return g_ready ? nullptr : "disabled";
    const DWORD error = GetLastError();
    const auto base = reinterpret_cast<std::uintptr_t>(base_ptr);
    const std::uintptr_t pad = base + c::p1_pad_rva;
    const std::uintptr_t setne = base + c::p1_setne_rva;
    const char* result = nullptr;
    if (!BytesOk(base)) {
        result = "bytes";
    } else if (near_code::CrossesCacheLine(setne, 4) || near_code::CrossesCacheLine(pad, 5)) {
        result = "cache_line";
    } else {
        void* stub = near_code::AllocateNear(pad + 5, 0x80);
        unsigned char jump[5]{};
        if (!stub) {
            result = "allocate_near";
        } else if (!detail::BuildJump(jump, pad, reinterpret_cast<std::uintptr_t>(stub))) {
            VirtualFree(stub, 0, MEM_RELEASE);
            result = "range";
        } else {
            unsigned char code[detail::kStubSize]{};
            detail::BuildStub(code, reinterpret_cast<std::uintptr_t>(&g_state), base + c::p1_back_rva);
            std::memcpy(stub, code, sizeof(code));
            DWORD old = 0;
            if (!VirtualProtect(stub, 0x80, PAGE_EXECUTE_READ, &old)) {
                VirtualFree(stub, 0, MEM_RELEASE);
                result = "protect";
            } else {
                FlushInstructionCache(GetCurrentProcess(), stub, 0x80);
                g_stub = stub;  // from here on the stub is never freed (code may reach it)
                if (const char* why = WritePadJump(pad, jump)) {
                    result = why;
                } else if (const char* swap = near_code::PatchDword(reinterpret_cast<void*>(setne), c::p1_setne_orig, c::p1_setne_new)) {
                    result = std::strcmp(swap, "changed") == 0 ? "exchange" : swap;
                }
            }
        }
    }
    // protect_restore after the exchange: the detour IS live.
    if (!result || (std::strcmp(result, "protect_restore") == 0 && PatchLive(base_ptr))) g_ready = true;
    if (log) {
        char line[192]{};
        if (g_ready) StringCchPrintfA(line, ARRAYSIZE(line),
                                      "partner_local_detour=installed; site=0x109e182; pad=0x109e128; stub=0x%llx; back=0x109e186%s\r\n",
                                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_stub) - base),
                                      result ? "; warn=protect_restore" : "");
        else StringCchPrintfA(line, ARRAYSIZE(line), "partner_local_detour=refused; reason=%s\r\n", result);
        log(line);
    }
    SetLastError(error);
    return g_ready ? nullptr : result;
}

bool Installed() noexcept { return g_ready; }

bool PatchLive(const void* base_ptr) noexcept {
    if (!g_stub) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(base_ptr);
    std::uint32_t setne = 0;
    std::memcpy(&setne, reinterpret_cast<const void*>(base + c::p1_setne_rva), 4);
    unsigned char want[5]{};
    if (!detail::BuildJump(want, base + c::p1_pad_rva, reinterpret_cast<std::uintptr_t>(g_stub))) return false;
    return setne == c::p1_setne_new && std::memcmp(reinterpret_cast<const void*>(base + c::p1_pad_rva), want, sizeof(want)) == 0;
}

void Force(std::uintptr_t p, std::uint32_t id) noexcept {
    InterlockedExchange(&g_state.tid, static_cast<LONG>(GetCurrentThreadId()));
    InterlockedExchange64(&g_state.p, static_cast<LONG64>(p));
    InterlockedExchange(&g_state.id, static_cast<LONG>(id));  // last: the stub tests the id first
    InterlockedIncrement(&g_windows);
}

void Release() noexcept { InterlockedExchange(&g_state.id, -1); }

void ReadStats(Stats& out) noexcept {
    out.installed = g_ready;
    out.forced = static_cast<std::uint32_t>(g_state.hits);
    out.refused = static_cast<std::uint32_t>(g_state.refused);
    out.windows = static_cast<std::uint32_t>(g_windows);
}

} // namespace mgscoop::partner_local_detour
