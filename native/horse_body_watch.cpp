// BODY0 round: the live write watch on body 0's transform (native/include/mgscoop/horse_body_watch.hpp). A vectored
// handler (first in the chain) takes only the single-step traps of our two debug registers; a watcher thread arms and
// disarms them on every thread of the process (Toolhelp snapshot, suspend, the debug registers only, resume).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstring>
#include "mgscoop/horse_body_watch.hpp"
#include "mgscoop/horse_drive.hpp"

namespace mgscoop::horse_body_watch {
namespace {
using mgscoop::horse_drive::kBodyTVtableRva;
std::atomic<std::uintptr_t> g_image{0}, g_t{0};
std::atomic<std::size_t> g_span{0};
std::atomic<bool> g_pin{false};
std::atomic<std::uint32_t> g_ord{0}, g_threads{0};
std::atomic<std::uint64_t> g_lastMs{0};
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
Table g_table{};
alignas(16) float g_centre[4]{}, g_foot[4]{};
float g_footY = 0.f;
PVOID g_veh = nullptr;
HANDLE g_watcher = nullptr;
DWORD g_watcherId = 0;
thread_local bool tl_in = false;

void Lock() noexcept { while (g_lock.test_and_set(std::memory_order_acquire)) YieldProcessor(); }
void Unlock() noexcept { g_lock.clear(std::memory_order_release); }

LONG CALLBACK Handler(EXCEPTION_POINTERS* e) {
    if (!e || !e->ExceptionRecord || !e->ContextRecord || e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = e->ContextRecord;
    if (!Ours(c->Dr6)) return EXCEPTION_CONTINUE_SEARCH;
    c->Dr6 &= ~0xFull;
    if (tl_in) return EXCEPTION_CONTINUE_EXECUTION;  // the pin's own stores below
    tl_in = true;
    const std::uintptr_t image = g_image.load(std::memory_order_relaxed);
    const std::size_t span = g_span.load(std::memory_order_relaxed);
    const std::uintptr_t rip = static_cast<std::uintptr_t>(c->Rip);
    const std::uint32_t rva = (image && rip >= image && rip - image < span) ? static_cast<std::uint32_t>(rip - image) : 0u;
    std::uint32_t ret[kRets]{};
    StackRets(reinterpret_cast<const std::uint64_t*>(c->Rsp), 48, image, 0x1000, 0x2000000, ret);
    const std::uint32_t ord = g_ord.fetch_add(1, std::memory_order_relaxed);
    const std::uintptr_t t = g_t.load(std::memory_order_acquire);
    // This thread resumes with the context's debug registers: follow a disarm or a new T here too.
    if (!t) {
        c->Dr7 = Dr7(c->Dr7, false);
        c->Dr0 = 0;
        c->Dr1 = 0;
    } else if (c->Dr0 != t + 0x24 || c->Dr1 != t + 0x14) {
        c->Dr0 = t + 0x24;
        c->Dr1 = t + 0x14;
    }
    // Pin only a live body-0 transform (its vtable at T+0: a freed or reused block is never written).
    std::uintptr_t vt = 0;
    SIZE_T got = 0;
    const bool live = t && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(t), &vt, sizeof(vt), &got) &&
                      got == sizeof(vt) && vt == image + kBodyTVtableRva;
    Lock();
    Note(g_table, rva, GetCurrentThreadId(), ord, ret);
    if (live && g_pin.load(std::memory_order_relaxed) && PinLive(g_lastMs.load(std::memory_order_relaxed), GetTickCount64())) {
        // Body 0 back where hold0 put it (these stores trap again on this thread; tl_in passes them).
        std::memcpy(reinterpret_cast<void*>(t + 0x20), g_centre, sizeof(g_centre));
        std::memcpy(reinterpret_cast<void*>(t + 0x10), g_foot, sizeof(g_foot));
        std::memcpy(reinterpret_cast<void*>(t + 0x174), &g_footY, sizeof(g_footY));
        ++g_table.pinned;
    }
    Unlock();
    tl_in = false;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Every thread but the watcher: DR0 = t+0x24, DR1 = t+0x14 and our DR7 bits (t != 0), or our bits cleared (t == 0).
std::uint32_t ArmAll(std::uintptr_t t) noexcept {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    std::uint32_t armed = 0;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
        if (!h) continue;
        if (SuspendThread(h) != static_cast<DWORD>(-1)) {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(h, &ctx)) {
                if (t) {
                    ctx.Dr0 = t + 0x24;
                    ctx.Dr1 = t + 0x14;
                } else {
                    ctx.Dr0 = 0;
                    ctx.Dr1 = 0;
                }
                ctx.Dr7 = Dr7(ctx.Dr7, t != 0);
                if (SetThreadContext(h, &ctx)) ++armed;
            }
            ResumeThread(h);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return armed;
}

DWORD WINAPI Watcher(LPVOID) {
    std::uintptr_t armed = 0;
    for (;;) {
        Sleep(50);
        const std::uint64_t now = GetTickCount64(), last = g_lastMs.load(std::memory_order_relaxed);
        std::uintptr_t want = g_t.load(std::memory_order_acquire);
        if (want && (!last || now - last > kIdleMs)) {
            g_t.store(0, std::memory_order_release);
            want = 0;
        }
        if (want == armed) continue;
        g_threads.store(ArmAll(want), std::memory_order_relaxed);
        armed = want;
    }
}
} // namespace

const char* BeforeHold(std::uintptr_t image, std::size_t span, std::uintptr_t t, float x, float y, float z, bool pin) noexcept {
    if (!image || !span || !t) return "watch_t";
    if (!g_veh) {
        g_image.store(image, std::memory_order_relaxed);
        g_span.store(span, std::memory_order_relaxed);
        g_veh = AddVectoredExceptionHandler(1, &Handler);
        if (!g_veh) return "watch_veh";
    }
    if (!g_watcher) {
        g_watcher = CreateThread(nullptr, 0, &Watcher, nullptr, 0, &g_watcherId);
        if (!g_watcher) return "watch_thread";
    }
    float h = 0.f;
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(t + 0x160), &h, sizeof(h), &read) || read != sizeof(h) ||
        !(h >= 0.f && h < 3.f))
        return "watch_t";
    alignas(16) float centre[4], foot[4];
    float fy = 0.f;
    Target(x, y, z, h, centre, foot, fy);
    Lock();
    std::memcpy(g_centre, centre, sizeof(centre));
    std::memcpy(g_foot, foot, sizeof(foot));
    g_footY = fy;
    Unlock();
    g_pin.store(pin, std::memory_order_relaxed);
    g_ord.store(0, std::memory_order_relaxed);
    g_lastMs.store(GetTickCount64(), std::memory_order_relaxed);
    g_t.store(t, std::memory_order_release);
    return nullptr;
}

void Release() noexcept {
    g_pin.store(false, std::memory_order_relaxed);
    g_lastMs.store(0, std::memory_order_relaxed);
    g_t.store(0, std::memory_order_release);
}

bool Take(Table& out, std::uint32_t& threads) noexcept {
    threads = g_threads.load(std::memory_order_relaxed);
    Lock();
    out = g_table;
    g_table = Table{};
    Unlock();
    return out.traps != 0;
}

} // namespace mgscoop::horse_body_watch
