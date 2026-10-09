// Original MGS Co-op native puppet: the look source hook (facing D3,
// docs/native-puppet.md "Facing"; partner_look_source.hpp).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstring>
#include "mgscoop/partner_look_source.hpp"
#include "mgscoop/near_code.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_look_source {
namespace {
namespace c = mgscoop::partner_player_contract;

volatile LONG g_installed = 0;
bool g_ready = false;
void* g_block = nullptr;  // stub + unwind data + RUNTIME_FUNCTION (never freed once registered)
volatile LONG64 g_base = 0;
// The published look (Publish/Withdraw on the hook thread; read by LookAfter
// on any thread). g_on is cleared first and set last.
volatile LONG g_on = 0, g_tid = 0, g_id = 0, g_idx = 0;
volatile LONG64 g_ps = 0;
volatile LONG g_pitch = 0, g_yaw = 0;  // float bits
volatile LONG g_calls = 0, g_hits = 0, g_skipped = 0, g_refused = 0, g_faulted = 0;

// Layout of the near block: the stub, its UNWIND_INFO (DWORD aligned) and the RUNTIME_FUNCTION.
inline constexpr std::size_t kBlockSize = 0x80;
inline constexpr std::size_t kUnwindAt = 0x30;
inline constexpr std::size_t kFunctionAt = 0x40;
static_assert(detail::kStubSize <= kUnwindAt && kUnwindAt + detail::kUnwindSize <= kFunctionAt &&
                  kFunctionAt + sizeof(RUNTIME_FUNCTION) <= kBlockSize, "near block layout");

template <class T>
T Load(std::uintptr_t a) noexcept { T v{}; std::memcpy(&v, reinterpret_cast<const void*>(a), sizeof(v)); return v; }

const c::Site* FindSite(std::uintptr_t rva) noexcept {
    for (const c::Site& s : c::code_sites)
        if (s.rva == rva) return &s;
    return nullptr;
}
bool SiteMatches(std::uintptr_t base, std::uintptr_t rva) noexcept {
    const c::Site* s = FindSite(rva);
    return s && std::memcmp(reinterpret_cast<const void*>(base + rva), s->bytes, s->size) == 0;
}

// The padding jump: never executed before the qword exchange publishes the
// retargeted call, so a plain write under a temporary protection change.
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
bool BuildJump(unsigned char (&out)[5], std::uintptr_t site, std::uintptr_t target) noexcept {
    std::int32_t rel = 0;
    if (!near_code::Rel32(site + 5, target, rel)) return false;
    out[0] = 0xE9;
    std::memcpy(out + 1, &rel, 4);
    return true;
}
int AccessFilter(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

namespace detail {
void BuildStub(unsigned char (&s)[kStubSize], std::uintptr_t target, std::uintptr_t after) noexcept {
    static constexpr unsigned char kCode[kStubSize] = {
        0x53,                                      // 00 push rbx
        0x56,                                      // 01 push rsi
        0x48, 0x83, 0xEC, 0x28,                    // 02 sub rsp, 0x28 (rsp 16-aligned at both calls)
        0x48, 0x8B, 0xD9,                          // 06 mov rbx, rcx      (PS)
        0x8B, 0xF2,                                // 09 mov esi, edx      (id)
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,        // 0B mov rax, 0xFFAC00
        0xFF, 0xD0,                                // 15 call rax          (the game's look computation)
        0x48, 0x8B, 0xCB,                          // 17 mov rcx, rbx
        0x8B, 0xD6,                                // 1A mov edx, esi
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,        // 1C mov rax, LookAfter
        0xFF, 0xD0,                                // 26 call rax
        0x48, 0x83, 0xC4, 0x28,                    // 28 add rsp, 0x28
        0x5E,                                      // 2C pop rsi
        0x5B,                                      // 2D pop rbx
        0xC3,                                      // 2E ret (rax is not read by the caller)
        0xCC};                                     // 2F
    std::memcpy(s, kCode, kStubSize);
    std::memcpy(s + kStubTargetAt, &target, 8);
    std::memcpy(s + kStubAfterAt, &after, 8);
}
void BuildUnwind(unsigned char (&u)[kUnwindSize]) noexcept {
    // Version 1, no flags; prolog 6 bytes; 3 codes (reverse order), padded to 4 slots.
    static constexpr unsigned char kInfo[kUnwindSize] = {
        0x01, 0x06, 0x03, 0x00,
        0x06, 0x42,  // at 6: UWOP_ALLOC_SMALL (2), (0x28 - 8) / 8 = 4
        0x02, 0x60,  // at 2: UWOP_PUSH_NONVOL (0), RSI (6)
        0x01, 0x30,  // at 1: UWOP_PUSH_NONVOL (0), RBX (3)
        0x00, 0x00};
    std::memcpy(u, kInfo, kUnwindSize);
}
std::uint64_t RetargetQword(std::uint64_t original) noexcept {
    if (original != c::look_src_qword_orig) return 0;
    return c::look_src_qword_new;
}
void LookAfter(void* ps_ptr, std::uint32_t id) noexcept {
    InterlockedIncrement(&g_calls);
    if (!g_on || g_faulted) return;
    if (static_cast<LONG>(GetCurrentThreadId()) != g_tid) return;
    const auto ps = reinterpret_cast<std::uintptr_t>(ps_ptr);
    if (static_cast<std::uintptr_t>(g_ps) != ps || static_cast<std::uint32_t>(g_id) != id) {
        InterlockedIncrement(&g_refused);
        return;
    }
    const auto base = static_cast<std::uintptr_t>(g_base);
    bool refused = false;
    __try {
        const std::uintptr_t vt = Load<std::uintptr_t>(ps);
        const std::uint32_t first = Load<std::uint32_t>(ps + c::ps_base_id);
        const std::uint32_t idx = static_cast<std::uint32_t>(g_idx);
        const std::uintptr_t rec = Load<std::uintptr_t>(ps + c::ps_rec);
        const std::uintptr_t look = Load<std::uintptr_t>(ps + c::ps_look);
        if ((vt != base + c::ps_vtable_rva && vt != base + c::ps_vtable_alt_rva) || id < first || id - first != idx || !rec || !look) {
            refused = true;
        } else if (Load<std::uint32_t>(rec + static_cast<std::uintptr_t>(idx) * c::ps_rec_stride) & c::ps_rec_skip_bit) {
            InterlockedIncrement(&g_skipped);
        } else {
            const LONG pair[2] = {g_pitch, g_yaw};
            std::memcpy(reinterpret_cast<void*>(look + static_cast<std::uintptr_t>(idx) * c::ps_look_stride), pair, sizeof(pair));
            InterlockedIncrement(&g_hits);
        }
    } __except (AccessFilter(GetExceptionCode())) {
        InterlockedExchange(&g_faulted, 1);
    }
    if (refused) InterlockedIncrement(&g_refused);
}
void ResetForTest() noexcept {
    // A test frees its synthetic image: forget the install (the registered
    // block itself is never freed: a stub may still be on some stack).
    InterlockedExchange(&g_installed, 0);
    g_ready = false;
    g_block = nullptr;
    InterlockedExchange(&g_on, 0);
    InterlockedExchange(&g_calls, 0);
    InterlockedExchange(&g_hits, 0);
    InterlockedExchange(&g_skipped, 0);
    InterlockedExchange(&g_refused, 0);
    InterlockedExchange(&g_faulted, 0);
}
} // namespace detail

const char* Install(const void* base_ptr, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0) return g_ready ? nullptr : "disabled";
    const DWORD error = GetLastError();
    const auto base = reinterpret_cast<std::uintptr_t>(base_ptr);
    InterlockedExchange64(&g_base, static_cast<LONG64>(base));
    const std::uintptr_t pad = base + c::look_pad_rva;
    const std::uintptr_t qword = base + c::look_src_qword_rva;
    const char* result = nullptr;
    // The pinned sites: the call and the instruction after it, 0xFFAC00's
    // prologue, its offline branch, the camera chain and the store tail.
    static constexpr std::uintptr_t kSites[] = {c::look_src_call_block_rva, c::look_src_next_ip, c::look_fn_rva, c::look_fn_offline_rva,
                                                c::look_fn_cam_rva, c::look_fn_store_rva};
    for (const std::uintptr_t rva : kSites)
        if (!result && !SiteMatches(base, rva)) result = "bytes";
    if (!result) {
        const auto* p = reinterpret_cast<const unsigned char*>(pad);
        if (p[-1] != 0xC3) result = "padding";
        for (std::size_t i = 0; !result && i < c::look_pad_end_rva - c::look_pad_rva; ++i)
            if (p[i] != 0xCC) result = "padding";
    }
    if (!result && (qword & 7u) != 0) result = "align";
    if (!result && Load<std::uint64_t>(qword) != c::look_src_qword_orig) result = "caller";
    if (!result && (near_code::CrossesCacheLine(qword, 8) || near_code::CrossesCacheLine(pad, 5))) result = "cache_line";
    if (!result) {
        void* block = near_code::AllocateNear(pad + 5, kBlockSize);
        unsigned char jump[5]{};
        if (!block) {
            result = "allocate_near";
        } else if (!BuildJump(jump, pad, reinterpret_cast<std::uintptr_t>(block))) {
            VirtualFree(block, 0, MEM_RELEASE);
            result = "range";
        } else {
            auto* b = static_cast<unsigned char*>(block);
            unsigned char stub[detail::kStubSize]{};
            detail::BuildStub(stub, base + c::look_fn_rva, reinterpret_cast<std::uintptr_t>(&detail::LookAfter));
            std::memcpy(b, stub, sizeof(stub));
            unsigned char unwind[detail::kUnwindSize]{};
            detail::BuildUnwind(unwind);
            std::memcpy(b + kUnwindAt, unwind, sizeof(unwind));
            RUNTIME_FUNCTION fn{};
            fn.BeginAddress = 0;
            fn.EndAddress = static_cast<DWORD>(detail::kStubSize - 1);
            fn.UnwindData = static_cast<DWORD>(kUnwindAt);
            std::memcpy(b + kFunctionAt, &fn, sizeof(fn));
            DWORD old = 0;
            if (!VirtualProtect(block, kBlockSize, PAGE_EXECUTE_READ, &old)) {
                VirtualFree(block, 0, MEM_RELEASE);
                result = "protect";
            } else if (!RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(b + kFunctionAt), 1,
                                            static_cast<DWORD64>(reinterpret_cast<std::uintptr_t>(block)))) {
                VirtualFree(block, 0, MEM_RELEASE);
                result = "unwind";
            } else {
                FlushInstructionCache(GetCurrentProcess(), block, kBlockSize);
                g_block = block;  // from here on never freed (code may reach it)
                if (const char* why = WritePadJump(pad, jump)) {
                    result = why;
                } else if (const char* swap = near_code::PatchQword(reinterpret_cast<void*>(qword), c::look_src_qword_orig,
                                                                    c::look_src_qword_new)) {
                    result = swap;
                }
                FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(qword), 8);
            }
        }
    }
    // protect_restore after the exchange: the hook IS live.
    if (!result || (std::strcmp(result, "protect_restore") == 0 && PatchLive(base_ptr))) g_ready = true;
    if (log) {
        char line[224]{};
        if (g_ready)
            StringCchPrintfA(line, ARRAYSIZE(line),
                             "look_src=installed; site=0xff29fc; qword=0xff29f8; pad=0xffad66; stub=0x%llx; fn=0xffac00%s\r\n",
                             static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_block) - base),
                             result ? "; warn=protect_restore" : "");
        else
            StringCchPrintfA(line, ARRAYSIZE(line), "look_src=refused; reason=%s; fallback=post_phase0_write\r\n", result);
        log(line);
    }
    SetLastError(error);
    return g_ready ? nullptr : result;
}

bool Installed() noexcept { return g_ready; }

bool PatchLive(const void* base_ptr) noexcept {
    if (!g_block) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(base_ptr);
    unsigned char want[5]{};
    if (!BuildJump(want, base + c::look_pad_rva, reinterpret_cast<std::uintptr_t>(g_block))) return false;
    return Load<std::uint64_t>(base + c::look_src_qword_rva) == c::look_src_qword_new &&
           std::memcmp(reinterpret_cast<const void*>(base + c::look_pad_rva), want, sizeof(want)) == 0;
}

void Publish(std::uintptr_t ps, std::uint32_t id, std::uint32_t idx, float pitch, float yaw) noexcept {
    InterlockedExchange(&g_on, 0);
    LONG p = 0, y = 0;
    std::memcpy(&p, &pitch, 4);
    std::memcpy(&y, &yaw, 4);
    InterlockedExchange64(&g_ps, static_cast<LONG64>(ps));
    InterlockedExchange(&g_id, static_cast<LONG>(id));
    InterlockedExchange(&g_idx, static_cast<LONG>(idx));
    InterlockedExchange(&g_pitch, p);
    InterlockedExchange(&g_yaw, y);
    InterlockedExchange(&g_tid, static_cast<LONG>(GetCurrentThreadId()));
    InterlockedExchange(&g_on, 1);
}

void Withdraw() noexcept { InterlockedExchange(&g_on, 0); }

void ReadStats(Stats& out) noexcept {
    out = Stats{};
    out.installed = g_ready;
    out.calls = static_cast<std::uint32_t>(g_calls);
    out.hits = static_cast<std::uint32_t>(g_hits);
    out.skipped = static_cast<std::uint32_t>(g_skipped);
    out.refused = static_cast<std::uint32_t>(g_refused);
    out.faulted = g_faulted != 0;
}

} // namespace mgscoop::partner_look_source
