// Supply drops synced (r1101, docs/world-sync-protocol.md section 25): the supply-box observer. Contract:
// native/supply_cbox_contract.hpp (tools/generate_supply_cbox_contract.py, tools/supply_cbox_sites.json).
// Observe only: the detours record each box request / reset and call the original with its own arguments; nothing in
// the game's flow changes. The one write is the lab remove lever (the box core's flags |= 2, the game's own reset bit),
// on Lua's request for a synced box the partner took.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <MinHook.h>
#include "mgscoop/supply_cbox.hpp"
#include "mgscoop/supply_sync.hpp"
#include "supply_cbox_contract.hpp"

#pragma intrinsic(_ReturnAddress)

namespace mgscoop::supply_cbox {
namespace {
namespace c = supply_cbox_contract;
namespace ss = supply_sync;
using CoreFn = std::uintptr_t (*)(void*, void*);
using ResetFn = std::uintptr_t (*)(void*);
CoreFn g_request = nullptr;
ResetFn g_reset = nullptr;
std::atomic<bool> g_ready{false}, g_enabled{false}, g_mirror{false};
std::atomic<std::uintptr_t> g_core{0};
std::uintptr_t g_base = 0;
std::size_t g_span = 0;
void (*g_log)(const char*) noexcept = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;   // the ring
SRWLOCK g_install = SRWLOCK_INIT;
ss::Ring g_ring;
std::atomic<unsigned> g_lines{0};
constexpr unsigned kMaxLines = 400;

struct Preserve {
    DWORD error = GetLastError();
    ~Preserve() { SetLastError(error); }
};
bool Read(std::uintptr_t address, void* out, std::size_t size) noexcept {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &read) && read == size;
}
bool ReadU32(std::uintptr_t address, std::uint32_t& out) noexcept { return Read(address, &out, sizeof(out)); }
std::uint32_t CallerRva(void* ret) noexcept {
    const auto r = reinterpret_cast<std::uintptr_t>(ret);
    return r >= g_base && r - g_base < g_span ? static_cast<std::uint32_t>(r - g_base) : 0;
}
void LogLine(const char* text) noexcept {
    if (!g_log || !text) return;
    if (g_lines.fetch_add(1, std::memory_order_relaxed) >= kMaxLines) return;
    g_log(text);
}
void Record(ss::Event& e) noexcept {
    e.t_ms = GetTickCount64();
    AcquireSRWLockExclusive(&g_lock);
    e.seq = g_ring.Push(e);
    ReleaseSRWLockExclusive(&g_lock);
    char line[320]{};
    if (ss::FormatLogLine(e, line, sizeof(line))) LogLine(line);
}

std::uintptr_t RequestDetour(void* core, void* request) {
    Preserve keep;
    if (g_ready.load(std::memory_order_acquire) && g_enabled.load(std::memory_order_relaxed)) {
        const auto z = reinterpret_cast<std::uintptr_t>(core);
        unsigned char bytes[ss::kRequestSize]{};
        ss::Event e{};
        if (Read(reinterpret_cast<std::uintptr_t>(request), bytes, sizeof(bytes)) && ss::DecodeRequest(bytes, sizeof(bytes), e)) {
            const auto previous = g_core.exchange(z, std::memory_order_acq_rel);
            if (previous != z) {
                char line[96]{};
                std::snprintf(line, sizeof(line), "supply=core;changed=%u\r\n", previous ? 1u : 0u);
                LogLine(line);
            }
            e.mirror = g_mirror.load(std::memory_order_relaxed) ? 1 : 0;
            e.caller_rva = CallerRva(_ReturnAddress());
            ReadU32(z + c::box_state, e.state);
            Record(e);
        } else {
            LogLine("supply=request_unreadable\r\n");
        }
    }
    return g_request(core, request);
}

std::uintptr_t ResetDetour(void* core) {
    Preserve keep;
    if (g_ready.load(std::memory_order_acquire) && g_enabled.load(std::memory_order_relaxed)) {
        const auto z = reinterpret_cast<std::uintptr_t>(core);
        const auto known = g_core.load(std::memory_order_acquire);
        if (known == 0 || known == z) {
            ss::Event e{};
            e.kind = ss::kReset;
            e.mirror = g_mirror.load(std::memory_order_relaxed) ? 1 : 0;
            e.caller_rva = CallerRva(_ReturnAddress());
            ReadU32(z + c::box_state, e.state);
            Record(e);
        } else {
            LogLine("supply=reset_other_core\r\n");
        }
    }
    return g_reset(core);
}

bool Match(std::uintptr_t rva, const unsigned char* expected, std::size_t size) noexcept {
    if (rva > g_span || size > g_span - rva) return false;
    unsigned char bytes[512]{};
    return size <= sizeof(bytes) && Read(g_base + rva, bytes, size) && std::memcmp(bytes, expected, size) == 0;
}

ss::Snapshot Snap() noexcept {
    ss::Snapshot s{};
    s.on = g_enabled.load(std::memory_order_relaxed);
    s.installed = g_ready.load(std::memory_order_acquire);
    const auto z = g_core.load(std::memory_order_acquire);
    if (z && ReadU32(z + c::box_state, s.state) && ReadU32(z + c::box_flags, s.flags)) s.box_known = true;
    return s;
}

// The lab remove lever: the game's own reset bit; the update resets the box under its own lock.
const char* Remove(std::uint32_t& state) noexcept {
    if (!g_ready.load(std::memory_order_acquire)) return "not_installed";
    const auto z = g_core.load(std::memory_order_acquire);
    if (!z) return "no_core";
    std::uint32_t flags = 0;
    if (!ReadU32(z + c::box_state, state) || !ReadU32(z + c::box_flags, flags)) return "unreadable";
    if (state == 0) return "no_box";
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(z + c::box_flags), &info, sizeof(info)) || info.State != MEM_COMMIT ||
        !(info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) || (info.Protect & PAGE_GUARD))
        return "not_writable";
    __try {
        _InterlockedOr(reinterpret_cast<volatile long*>(z + c::box_flags), static_cast<long>(c::reset_bit));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return "fault"; }
    return nullptr;
}
} // namespace

bool Install(void* base, std::size_t span, void (*log)(const char*) noexcept) noexcept {
    if (!base || !span || !log) return false;
    Preserve keep;
    AcquireSRWLockExclusive(&g_install);
    struct Finally { ~Finally() { ReleaseSRWLockExclusive(&g_install); } } finally;
    if (g_ready.load(std::memory_order_acquire)) return true;
    g_base = reinterpret_cast<std::uintptr_t>(base);
    g_span = span;
    g_log = log;
    for (const auto& s : c::sites) {
        if (!Match(s.rva, s.bytes, s.size)) {
            char line[96]{};
            std::snprintf(line, sizeof(line), "supply=refused;reason=site;rva=0x%llx\r\n", static_cast<unsigned long long>(s.rva));
            log(line);
            return false;
        }
    }
    for (const auto& s : c::slots) {
        std::uintptr_t actual = 0;
        if (!Read(g_base + s.vtable_rva + s.offset, &actual, sizeof(actual)) || actual != g_base + s.target_rva) {
            log("supply=refused;reason=slot\r\n");
            return false;
        }
    }
    const auto init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) { log("supply=failed;reason=minhook\r\n"); return false; }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&Install), &pinned)) return false;
    void* request = reinterpret_cast<void*>(g_base + c::request_core_rva);
    void* reset = reinterpret_cast<void*>(g_base + c::box_reset_rva);
    if (MH_CreateHook(request, reinterpret_cast<void*>(&RequestDetour), reinterpret_cast<void**>(&g_request)) != MH_OK) {
        log("supply=failed;reason=create_request\r\n");
        return false;
    }
    if (MH_CreateHook(reset, reinterpret_cast<void*>(&ResetDetour), reinterpret_cast<void**>(&g_reset)) != MH_OK) {
        MH_RemoveHook(request);
        log("supply=failed;reason=create_reset\r\n");
        return false;
    }
    // Ready before enabling: a detour that runs at once must see its trampoline (both are set by MH_CreateHook).
    g_ready.store(true, std::memory_order_release);
    if (MH_EnableHook(request) != MH_OK || MH_EnableHook(reset) != MH_OK) {
        // A forwarding detour may be in flight; keep both trampolines. The detours only observe.
        log("supply=failed;reason=enable_hook\r\n");
        return false;
    }
    log("supply=installed;detours=2;observe_only=1;remove_lever=reset_bit\r\n");
    return true;
}

bool Installed() noexcept { return g_ready.load(std::memory_order_acquire); }
void SetEnabled(bool on) noexcept { g_enabled.store(on, std::memory_order_relaxed); }
bool Enabled() noexcept { return g_enabled.load(std::memory_order_relaxed); }

std::size_t HandleReceipt(const char* text, std::size_t size, std::uint64_t, char* out, std::size_t capacity) noexcept {
    ss::Parsed p{};
    if (!out || !capacity || !ss::ParseCommand(text, size, p)) return 0;
    if (p.cmd == ss::Command::poll) {
        const auto s = Snap();
        ss::Event events[ss::kRing]{};
        std::uint32_t lost = 0, last = 0;
        std::size_t n = 0;
        if (s.on) {
            AcquireSRWLockShared(&g_lock);
            n = g_ring.Since(p.arg, events, ss::kRing, lost);
            last = g_ring.Last();
            ReleaseSRWLockShared(&g_lock);
        }
        return ss::FormatPoll(s, events, n, last, lost, out, capacity);
    }
    int w = 0;
    if (!g_enabled.load(std::memory_order_relaxed)) {
        w = std::snprintf(out, capacity, "return {ok=0,why='off'}");
    } else if (p.cmd == ss::Command::mirror) {
        g_mirror.store(p.arg == 1, std::memory_order_relaxed);
        w = std::snprintf(out, capacity, "return {ok=1}");
    } else {
        std::uint32_t state = 0;
        const char* why = Remove(state);
        char line[96]{};
        std::snprintf(line, sizeof(line), "supply=remove;token=%lu;ok=%u;state=%lu;why=%s\r\n", static_cast<unsigned long>(p.arg),
                      why ? 0u : 1u, static_cast<unsigned long>(state), why ? why : "none");
        LogLine(line);
        w = why ? std::snprintf(out, capacity, "return {ok=0,why='%s',st=%lu}", why, static_cast<unsigned long>(state))
                : std::snprintf(out, capacity, "return {ok=1,st=%lu}", static_cast<unsigned long>(state));
    }
    return w > 0 && static_cast<std::size_t>(w) < capacity ? static_cast<std::size_t>(w) : 0;
}

} // namespace mgscoop::supply_cbox
