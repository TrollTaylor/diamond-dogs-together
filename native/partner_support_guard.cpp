// Original MGS Co-op native partner throws: audited non-consuming stock branch.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstring>
#include "mgscoop/partner_support_guard.hpp"
#include "mgscoop/near_code.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_support_guard {
namespace {
namespace c = mgscoop::partner_player_contract;
struct alignas(8) State {
    volatile LONG id, hits, tid, refused;
    volatile LONG64 p;
    volatile LONG key;
};
static_assert(sizeof(LONG) == 4 && offsetof(State, hits) == 4 && offsetof(State, tid) == 8 &&
              offsetof(State, refused) == 0xC && offsetof(State, p) == 0x10 && offsetof(State, key) == 0x18, "stub state layout");
State g_state{-1, 0, 0, 0, 0, 0};
volatile LONG& g_key = g_state.key;
volatile LONG g_installed = 0, g_window_thread = 0, g_windows = 0;
volatile LONG64 g_armed_p = 0;
bool g_ready = false;
std::uintptr_t g_base = 0;
std::size_t g_span = 0;
void* g_stub = nullptr;

bool InImage(std::uintptr_t rva, std::size_t size) noexcept {
    return g_base && rva <= g_span && size <= g_span - rva;
}
bool BytesOk() noexcept {
    // Include the instruction's untouched tail, source P chain, original
    // continuation and the complete remote branch/epilogue.
    static constexpr unsigned char entry[] = {
        0x48,0x89,0x4C,0x24,0x08,0x55,0x56,0x41,0x55,0x48,0x83,0xEC,0x50,
        0x48,0x8B,0x41,0x08,0x45,0x8B,0xE8,0x48,0x8B,0xF2,0x4C,0x8B,0x88,0x38,0x01,0,0,
        0x48,0x8D,0xAA,0xB0,0x01,0,0,0x41,0x8B,0x81,0x04,0x02,0,0,
        0xC1,0xE8,0x08,0xA8,0x01,0x74,0x27,0x45,0x3B,0x81,0x18,0x02,0,0,0x74,0x1E,
        0x48,0xC7,0x85,0xAC,0,0,0,0xFF,0xFF,0xFF,0xFF,0x81,0x8A,0xC0,0x03,0,0,0,0,0,0x20,
        0x48,0x83,0xC4,0x50,0x41,0x5D,0x5E,0x5D,0xC3};
    static_assert(sizeof(entry) == 0x5A, "guard entry span");
    if (!InImage(c::support_stock_fn_rva, sizeof(entry)) ||
        !InImage(c::support_stock_pad_begin_rva - 1, c::support_stock_pad_end_rva - c::support_stock_pad_begin_rva + 1))
        return false;
    if (std::memcmp(reinterpret_cast<const void*>(g_base + c::support_stock_fn_rva), entry, sizeof(entry))) return false;
    const auto* pad = reinterpret_cast<const unsigned char*>(g_base + c::support_stock_pad_begin_rva);
    if (pad[-1] != 0xC3) return false;
    for (std::size_t i = 0; i < c::support_stock_pad_end_rva - c::support_stock_pad_begin_rva; ++i)
        if (pad[i] != 0xCC) return false;
    return true;
}
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
LONG Atomic(volatile LONG& value) noexcept { return InterlockedCompareExchange(&value, 0, 0); }
LONG64 Atomic64(volatile LONG64& value) noexcept { return InterlockedCompareExchange64(&value, 0, 0); }
} // namespace
namespace detail {
void BuildStub(unsigned char (&s)[kStubSize], std::uintptr_t state,
               std::uintptr_t back, std::uintptr_t remote) noexcept {
    static constexpr unsigned char code[kStubSize] = {
        0x41,0x8B,0x81,0x04,0x02,0,0,                    // 00 mov eax,[r9+0x204] (replayed)
        0x51,0x50,                                      // 07 push rcx; push rax (loaded eax)
        0x48,0xB9,0,0,0,0,0,0,0,0,                     // 09 mov rcx,state
        0x41,0x83,0xF8,0x01,0x75,0x4C,                 // 13 cmp r8d,1; jne done
        0x0F,0x1F,0x44,0,0,                            // 19 nop5 (was the drain-window test: the guard
                                                        //    now covers every body-1 job, review-1ca3ce6)
        0x83,0x79,0x18,0x01,0x75,0x3D,                 // 1E cmp dword [rcx+0x18],1; jne refused (key)
        0x4C,0x3B,0x49,0x10,0x75,0x37,                 // 24 cmp r9,[rcx+0x10]; jne refused
        0x0F,0x1F,0x84,0,0,0,0,0,                      // 2A nop8 (was the TEB thread read)
        0x0F,0x1F,0x44,0,0,                            // 32 nop5 (was the drain-thread compare)
        0x8B,0x82,0x7C,0x02,0,0,                       // 37 mov eax,[rdx+0x27C]
        0xC1,0xE8,0x06,0x83,0xE0,0x07,                 // 3D shr eax,6; and eax,7
        0x83,0xF8,0x02,0x74,0x05,                      // 43 cmp eax,2; je force
        0x83,0xF8,0x03,0x75,0x14,                      // 48 cmp eax,3; jne refused
        0xF0,0xFF,0x41,0x04,                           // 4D force: lock inc [rcx+4]
        0x58,0x59,0xFF,0x25,0,0,0,0,                   // 51 pop rax; pop rcx; jmp [rip]
        0,0,0,0,0,0,0,0,                               // 59 remote = 0x108C8FC
        0xF0,0xFF,0x41,0x0C,                           // 61 refused: lock inc [rcx+0xC]
        0x58,0x59,0xFF,0x25,0,0,0,0,                   // 65 done: pop rax; pop rcx; jmp [rip]
        0,0,0,0,0,0,0,0};                              // 6D back = 0x108C8EC
    std::memcpy(s, code, sizeof(code));
    std::memcpy(s + kStubStateAt, &state, 8);
    std::memcpy(s + kStubRemoteAt, &remote, 8);
    std::memcpy(s + kStubBackAt, &back, 8);
}
bool BuildJump(unsigned char (&out)[5], std::uintptr_t site, std::uintptr_t target) noexcept {
    std::int32_t rel = 0;
    if (!near_code::Rel32(site + 5, target, rel)) return false;
    out[0] = 0xE9; std::memcpy(out + 1, &rel, 4); return true;
}
void ResetForTest() noexcept {
    Disarm();
    InterlockedExchange(&g_installed, 0);
    InterlockedExchange(&g_state.hits, 0); InterlockedExchange(&g_state.refused, 0);
    InterlockedExchange(&g_windows, 0);
    g_ready = false; g_base = 0; g_span = 0; g_stub = nullptr;
}
} // namespace detail

const char* Install(const void* base, std::size_t span, bool fingerprint, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0) return g_ready ? nullptr : "disabled";
    const DWORD error = GetLastError();
    g_base = reinterpret_cast<std::uintptr_t>(base); g_span = span;
    const char* result = nullptr;
    const auto site = g_base + c::support_stock_load_rva, pad = g_base + c::support_stock_pad_rva;
    if (!fingerprint) result = "fingerprint";
    else if (!BytesOk()) result = "bytes";
    else if (near_code::CrossesCacheLine(site, 4) || near_code::CrossesCacheLine(pad, 5)) result = "cache_line";
    else {
        void* stub = near_code::AllocateNear(pad + 5, 0x80);
        unsigned char jump[5]{};
        if (!stub) result = "allocate_near";
        else if (!detail::BuildJump(jump, pad, reinterpret_cast<std::uintptr_t>(stub))) {
            VirtualFree(stub, 0, MEM_RELEASE); result = "range";
        } else {
            unsigned char code[detail::kStubSize]{};
            detail::BuildStub(code, reinterpret_cast<std::uintptr_t>(&g_state),
                              g_base + c::support_stock_back_rva, g_base + c::support_stock_remote_rva);
            std::memcpy(stub, code, sizeof(code));
            DWORD previous = 0;
            if (!VirtualProtect(stub, 0x80, PAGE_EXECUTE_READ, &previous)) {
                VirtualFree(stub, 0, MEM_RELEASE); result = "protect";
            } else {
                FlushInstructionCache(GetCurrentProcess(), stub, 0x80);
                g_stub = stub; // Permanent from publication; never removed/freed.
                if (const char* why = WritePadJump(pad, jump)) result = why;
                else if (const char* patch_reason = near_code::PatchDword(reinterpret_cast<void*>(site),
                            c::support_stock_load_orig, c::support_stock_load_new))
                    result = std::strcmp(patch_reason, "changed") == 0 ? "exchange" : patch_reason;
            }
        }
    }
    if (!result || (std::strcmp(result, "protect_restore") == 0 && Live())) g_ready = true;
    if (log) {
        char line[256]{};
        StringCchPrintfA(line, ARRAYSIZE(line),
            "partner_support=%s; slot=3; equip=0; type=12; reason=%s; site=0x108c8e5; pad=0x108c888; stub=0x%llx\r\n",
            g_ready ? "guard" : "refused", result ? result : "installed",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_stub)));
        log(line);
    }
    SetLastError(error);
    return g_ready ? nullptr : result;
}
bool Installed() noexcept { return g_ready; }
bool Live() noexcept {
    if (!g_stub || !InImage(c::support_stock_load_rva, 7) || !InImage(c::support_stock_pad_rva, 8)) return false;
    std::uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(g_base + c::support_stock_load_rva), 4);
    static constexpr unsigned char tail[]{0x02,0,0};
    unsigned char want[5]{};
    return value == c::support_stock_load_new &&
           std::memcmp(reinterpret_cast<const void*>(g_base + c::support_stock_load_rva + 4), tail, sizeof(tail)) == 0 &&
           detail::BuildJump(want, g_base + c::support_stock_pad_rva, reinterpret_cast<std::uintptr_t>(g_stub)) &&
           std::memcmp(reinterpret_cast<const void*>(g_base + c::support_stock_pad_rva), want, sizeof(want)) == 0;
}
bool Arm(std::uintptr_t p, std::uint32_t id) noexcept {
    if (!p || id != 1 || !g_ready || !Live()) { Disarm(); return false; }
    if (Atomic(g_key) && static_cast<std::uintptr_t>(Atomic64(g_armed_p)) == p &&
        static_cast<std::uintptr_t>(Atomic64(g_state.p)) == p) return true;
    Disarm(); InterlockedExchange64(&g_armed_p, static_cast<LONG64>(p));
    // The stub compares r9 with state.p: publish P before the key.
    InterlockedExchange64(&g_state.p, static_cast<LONG64>(p));
    InterlockedExchange(&g_key, 1); return true;
}
bool Armed(std::uintptr_t p, std::uint32_t id) noexcept {
    return p && id == 1 && Atomic(g_key) != 0 && static_cast<std::uintptr_t>(Atomic64(g_armed_p)) == p;
}
void Disarm() noexcept {
    InterlockedExchange(&g_state.id, -1); // Close before withdrawing identity/key.
    InterlockedExchange(&g_key, 0); InterlockedExchange64(&g_state.p, 0); InterlockedExchange64(&g_armed_p, 0);
    InterlockedExchange(&g_window_thread, 0);
}
bool Enter(std::uintptr_t p, std::uint32_t id) noexcept {
    if (!Armed(p, id) || !g_ready || !Live()) return false;
    const LONG thread = static_cast<LONG>(GetCurrentThreadId());
    if (InterlockedCompareExchange(&g_window_thread, thread, 0) != 0) {
        InterlockedIncrement(&g_state.refused); return false;
    }
    // Telemetry only: the stub no longer reads the window flag or the thread;
    // state.p is owned by Arm.
    InterlockedExchange(&g_state.tid, thread);
    InterlockedExchange(&g_state.id, 1);
    InterlockedIncrement(&g_windows); return true;
}
void Leave() noexcept {
    if (Atomic(g_window_thread) != static_cast<LONG>(GetCurrentThreadId())) return;
    InterlockedExchange(&g_state.id, -1); InterlockedExchange(&g_window_thread, 0);
}
void ReadStats(Stats& out) noexcept {
    out = {g_ready, Live(), Atomic(g_key) != 0, Atomic(g_state.id) == 1,
           static_cast<std::uint32_t>(Atomic(g_state.hits)), static_cast<std::uint32_t>(Atomic(g_state.refused)),
           static_cast<std::uint32_t>(Atomic(g_windows))};
}
} // namespace mgscoop::partner_support_guard
