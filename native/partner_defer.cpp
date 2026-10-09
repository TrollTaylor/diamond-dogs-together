// Original MGS Co-op native puppet: block 1's per-block job deferred onto the
// main thread (docs/native-puppet.md 4). H2 wraps the only call of the batch
// 0xBF4F20 (rel32 at 0xBF4D55); H1 replaces the player GameObject's job slot
// (qword 0x2295420 -> 0x9C1B90) with W50. Contract bytes come from the
// generated partner_player_contract.hpp (re-verified here at install).
//
// Exceptions. Our own reads (finding the entry) handle an access fault
// themselves (the deferral is then disabled for the mission and the record is
// reported lost). An exception raised by the game's job inside a drain is
// recorded (fault_phase=job) and passed on (EXCEPTION_CONTINUE_SEARCH), as on a
// worker; if an outer handler unwinds through the drain, __finally blocks
// release the entry read lock and pop the scratch scope, in that order.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <strsafe.h>
#include <cstring>
#include "mgscoop/partner_defer.hpp"
#include "mgscoop/near_code.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "partner_player_contract.hpp"

#pragma intrinsic(_ReturnAddress)

namespace mgscoop::partner_defer {
namespace {
namespace c = mgscoop::partner_player_contract;
namespace hk = mgscoop::partner_input_hook;

// --- Process state (the DLL is pinned; nothing is torn down) ------------------
volatile LONG g_install_tried = 0;
volatile LONG g_h2 = 0, g_h1 = 0;
const char* g_refusal = nullptr;
std::uintptr_t g_base = 0;
LogFn g_log = nullptr;
void* g_h2_stub = nullptr;
detail::BatchFn g_batch_orig = nullptr;
detail::JobFn g_job_orig = nullptr;
detail::ScopeCtorFn g_scope_ctor = nullptr;
detail::ScopeDtorFn g_scope_dtor = nullptr;
detail::ScopeReadFn g_scope_read = nullptr;
detail::ThreadFn g_hook_thread_fn = nullptr;
std::uintptr_t g_job_return = 0;

volatile LONG g_mode = 0;          // Mode
volatile LONG g_armed = 0;
volatile LONG g_shutdown = 0;  // set once at window close; never cleared
volatile LONG g_faulted = 0;
volatile LONG g_fault_phase = 0, g_fault_code = 0;
// Identity: go (64-bit) plus pools | valid << 16, written identity-first.
volatile LONG64 g_id_go = 0;
volatile LONG g_id_pools = 0;

// Batches in flight and pending records (per pass/mode 0..1). g_depth[m] and
// g_main_depth count hook-thread batches (nesting included), g_foreign the
// batches running on any other thread.
volatile LONG g_depth[2] = {0, 0};
volatile LONG g_main_depth = 0;
volatile LONG g_foreign = 0;
volatile LONG g_pending[2] = {0, 0};   // 0 free, 2 claimed, 1 published
struct Rec {
    std::uintptr_t go;
    std::uint32_t block, nbits;
    const std::uint8_t* bits;
    std::uint32_t tid;
};
Rec g_def[2]{};

// Counters.
volatile LONG g_batches = 0, g_batch_thread = 0, g_threads[4] = {0, 0, 0, 0}, g_thread_count = 0;
volatile LONG g_w50 = 0, g_b0 = 0, g_b1 = 0, g_other = 0, g_subs = 0, g_inline = 0, g_worker = 0, g_ret_other = 0;
volatile LONG g_deferred = 0, g_dup = 0, g_pass[16]{};
volatile LONG g_drains[2] = {0, 0}, g_windowed = 0, g_unwindowed = 0, g_lost = 0, g_gate_closed = 0, g_torn = 0;
volatile LONG g_entry_changed = 0, g_scope_unbalanced = 0, g_id1_pass = 0;
volatile LONG g_drain_us_max = 0, g_batch_us_max = 0, g_spin_max = 0;
volatile LONG g_nested_batches = 0, g_foreign_batches = 0, g_unwound = 0;

std::uint64_t NowUs() noexcept {
    LARGE_INTEGER f{}, cc{};
    if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0 || !QueryPerformanceCounter(&cc)) return GetTickCount64() * 1000;
    return static_cast<std::uint64_t>(cc.QuadPart) / static_cast<std::uint64_t>(f.QuadPart) * 1000000 +
           (static_cast<std::uint64_t>(cc.QuadPart) % static_cast<std::uint64_t>(f.QuadPart)) * 1000000 /
               static_cast<std::uint64_t>(f.QuadPart);
}
void Max(volatile LONG& slot, LONG value) noexcept {
    LONG seen = slot;
    while (value > seen) {
        const LONG prev = InterlockedCompareExchange(&slot, value, seen);
        if (prev == seen) break;
        seen = prev;
    }
}
template <class T>
T Load(std::uintptr_t a) noexcept { T v{}; std::memcpy(&v, reinterpret_cast<const void*>(a), sizeof(v)); return v; }

void Log(const char* line) noexcept { if (g_log) g_log(line); }
void LogFormat(const char* format, ...) noexcept {
    if (!g_log) return;
    char line[256]{};
    va_list arguments;
    va_start(arguments, format);
    const HRESULT result = StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    if (SUCCEEDED(result)) g_log(line);
}

// The deferral fault: once per mission (until MissionStart).
void DeferFault(FaultPhase phase, std::uint32_t code) noexcept {
    if (InterlockedExchange(&g_faulted, 1) == 0) {
        InterlockedExchange(&g_fault_phase, static_cast<LONG>(phase));
        InterlockedExchange(&g_fault_code, static_cast<LONG>(code));
    }
}
bool IsAccessFault(DWORD code) noexcept { return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR; }
bool IsFatalCode(DWORD code) noexcept {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_IN_PAGE_ERROR: case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION: case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_STACK_OVERFLOW: case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: case EXCEPTION_DATATYPE_MISALIGNMENT:
        return true;
    default:
        return false;
    }
}
int OwnFilter(FaultPhase phase, DWORD code) noexcept {
    if (!IsAccessFault(code)) return EXCEPTION_CONTINUE_SEARCH;
    DeferFault(phase, code);
    return EXCEPTION_EXECUTE_HANDLER;
}
int GameFilter(DWORD code) noexcept {
    if (IsFatalCode(code)) DeferFault(FaultPhase::job, code);
    return EXCEPTION_CONTINUE_SEARCH;
}

std::uint32_t HookThread() noexcept { return g_hook_thread_fn ? g_hook_thread_fn() : hk::HookThread(); }

// The contract site with this RVA (generated table).
const c::Site* SiteAt(std::uintptr_t rva) noexcept {
    for (const c::Site& s : c::code_sites)
        if (s.rva == rva) return &s;
    return nullptr;
}
bool SiteOk(std::uintptr_t base, std::uintptr_t rva) noexcept {
    const c::Site* s = SiteAt(rva);
    return s && std::memcmp(reinterpret_cast<const void*>(base + rva), s->bytes, s->size) == 0;
}

void NoteThread(std::uint32_t tid) noexcept {
    for (int i = 0; i < 4; ++i) {
        const LONG seen = g_threads[i];
        if (seen == static_cast<LONG>(tid)) return;
        if (seen == 0) {
            const LONG prev = InterlockedCompareExchange(&g_threads[i], static_cast<LONG>(tid), 0);
            if (prev == 0) { InterlockedIncrement(&g_thread_count); return; }
            if (prev == static_cast<LONG>(tid)) return;
        }
    }
    // More than four distinct threads: count saturates at 5.
    if (g_thread_count < 5) InterlockedCompareExchange(&g_thread_count, 5, 4);
}

// --- TLS scratch scope (read-only telemetry / balance check) -------------------
std::uintptr_t ReadTlsScope() noexcept {
    std::uintptr_t value = 0;
    __try {
        const std::uint32_t index = Load<std::uint32_t>(g_base + c::scope_tls_index_rva);
        const auto array = static_cast<std::uintptr_t>(__readgsqword(0x58));
        const std::uintptr_t block = Load<std::uintptr_t>(array + 8 * static_cast<std::uintptr_t>(index));
        value = Load<std::uintptr_t>(block + c::scope_tls_slot);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        value = 0;
    }
    return value;
}
std::uintptr_t TlsScope() noexcept { return g_scope_read ? g_scope_read() : ReadTlsScope(); }

// --- Entry read lock (0xBF4CB0 / 0x21EA0) --------------------------------------
void ReadLock(std::uintptr_t entry) noexcept {
    auto* lock = reinterpret_cast<volatile LONG*>(entry + c::entry_lock);
    LONG spins = 0;
    for (;;) {
        const LONG v = *lock;
        if (v == -1) { ++spins; YieldProcessor(); continue; }
        if (InterlockedCompareExchange(lock, v + 1, v) == v) break;
    }
    if (spins) Max(g_spin_max, spins);
}
void ReadUnlock(std::uintptr_t entry) noexcept {
    InterlockedDecrement(reinterpret_cast<volatile LONG*>(entry + c::entry_lock));
}

// The job without a window: a fatal game code is recorded and passed on.
void GuardedJob(void* go, const JobParam* param) noexcept {
    __try { g_job_orig(go, param); }
    __except (GameFilter(GetExceptionCode())) {}
}
void HookJob(void* go, const void* param) noexcept { g_job_orig(go, static_cast<const JobParam*>(param)); }

// Inside the read lock and the scope: the gate, the param, the job (windowed
// when possible). Returns false only when nothing ran (gate closed).
// Our own reads of the drain's inputs (gate, live nbits/bits, id 1's bit) under
// SEH: an access fault faults the deferral and nothing runs (lost-like).
enum class Inputs : std::uint8_t { ok, gate_closed, fault };
Inputs ReadInputs(std::uintptr_t entry, const Rec& rec, std::uint32_t mode, JobParam& param, std::uint32_t& bit1) noexcept {
    Inputs result = Inputs::fault;
    __try {
        const std::uintptr_t flags_ptr = Load<std::uintptr_t>(rec.go + c::go_flags_ptr);
        if (!flags_ptr || !(Load<std::uint32_t>(flags_ptr + c::go_flags) & c::go_flags_bit)) {
            result = Inputs::gate_closed;
        } else {
            param.block = rec.block;
            param.sub = 0;
            param.mode = mode;
            param.nbits = Load<std::uint32_t>(entry + c::entry_nbits);
            param.bits = Load<const std::uint8_t*>(entry + c::entry_bits);
            // id 1's bit as 0x9C1C15 reads it: bit = 1 < nbits ? bits[0] >> 1 & 1 : 1.
            bit1 = param.nbits > 1 && param.bits ? (Load<std::uint8_t>(reinterpret_cast<std::uintptr_t>(param.bits)) >> 1) & 1u : 1u;
            result = Inputs::ok;
        }
    } __except (OwnFilter(FaultPhase::find, GetExceptionCode())) {
        result = Inputs::fault;
    }
    return result;
}

void DrainCall(std::uintptr_t entry, const Rec& rec, std::uint32_t mode) noexcept {
    void* const go = reinterpret_cast<void*>(rec.go);
    JobParam param{};
    std::uint32_t bit1 = 1;
    const Inputs inputs = ReadInputs(entry, rec, mode, param, bit1);
    if (inputs == Inputs::fault) {
        InterlockedIncrement(&g_lost);
        return;
    }
    if (inputs == Inputs::gate_closed) {
        InterlockedIncrement(&g_gate_closed);
        DeferFault(FaultPhase::gate, 0);
        return;
    }
    if (param.nbits != rec.nbits || param.bits != rec.bits) InterlockedIncrement(&g_entry_changed);
    if (bit1 == mode) InterlockedOr(&g_id1_pass, static_cast<LONG>(1u << mode));
    else InterlockedAnd(&g_id1_pass, static_cast<LONG>(~(1u << mode)));
    const bool windowable = static_cast<std::uint32_t>(GetCurrentThreadId()) == HookThread() && g_armed && !g_shutdown && hk::Installed();
    const auto consumer = mode == 0 ? partner_input::Consumer::drain0 : partner_input::Consumer::drain1;
    if (windowable && hk::RunJobWindow(go, &param, param.block, &HookJob, consumer)) InterlockedIncrement(&g_windowed);
    else {
        if (!windowable) GuardedJob(go, &param);
        InterlockedIncrement(&g_unwindowed);
    }
}

// Read lock and scope around DrainCall, released/popped in that order even on
// unwind. POD only (C2712).
void DrainLocked(std::uintptr_t entry, const Rec& rec, std::uint32_t mode) noexcept {
    alignas(16) unsigned char scope[c::scope_buffer];
    std::memset(scope, 0, sizeof(scope));
    g_scope_ctor(scope);
    __try {
        ReadLock(entry);
        __try {
            DrainCall(entry, rec, mode);
        } __finally {
            ReadUnlock(entry);
        }
    } __finally {
        g_scope_dtor(scope);
    }
}

void Drain(void* group, std::uint32_t mode) noexcept {
    Rec rec{};
    std::memcpy(&rec, const_cast<const Rec*>(&g_def[mode]), sizeof(rec));
    const std::uint64_t t0 = NowUs();
    std::uintptr_t entry = 0;
    __try {
        const auto g = reinterpret_cast<std::uintptr_t>(group);
        entry = detail::FindEntry(Load<std::uintptr_t>(g), Load<std::uintptr_t>(g + 8), rec.go);
    } __except (OwnFilter(FaultPhase::find, GetExceptionCode())) {
        entry = 0;
    }
    if (!entry) {
        // Never call: the arbiter leaves on lost > 0.
        InterlockedIncrement(&g_lost);
        DeferFault(FaultPhase::lost, 0);
        return;
    }
    const std::uintptr_t before = TlsScope();
    DrainLocked(entry, rec, mode);
    if (TlsScope() != before) {
        InterlockedIncrement(&g_scope_unbalanced);
        DeferFault(FaultPhase::scope, 0);
    }
    InterlockedIncrement(&g_drains[mode]);
    Max(g_drain_us_max, static_cast<LONG>(NowUs() - t0));
}

// A record whose batch unwound (an exception through the join) is dropped,
// never drained in a later frame: counted as torn, the deferral faults.
void DropPending(std::uint32_t mode) noexcept {
    if (InterlockedExchange(&g_pending[mode], 0) != 0) {
        InterlockedIncrement(&g_unwound);
        InterlockedIncrement(&g_torn);
        DeferFault(FaultPhase::torn, 0);
    }
}

// --- H2: the batch wrapper ------------------------------------------------------
void WrapBatch(void* group, std::uint32_t mode) noexcept {
    const auto tid = static_cast<std::uint32_t>(GetCurrentThreadId());
    InterlockedIncrement(&g_batches);
    InterlockedCompareExchange(&g_batch_thread, static_cast<LONG>(tid), 0);
    NoteThread(tid);
    const std::uint32_t hook = HookThread();
    const bool main = hook != 0 && tid == hook && mode <= 1;
    if (!main) {
        // Any other thread: counted while in flight (InBatch, W50 foreign),
        // never deferred into and never drained.
        InterlockedIncrement(&g_foreign_batches);
        InterlockedIncrement(&g_foreign);
        __try {
            g_batch_orig(group, mode);
        } __finally {
            InterlockedDecrement(&g_foreign);
        }
        return;
    }
    const std::uint64_t t0 = NowUs();
    InterlockedIncrement(&g_depth[mode]);
    if (InterlockedIncrement(&g_main_depth) > 1) InterlockedIncrement(&g_nested_batches);
    LONG left = 0;
    __try {
        g_batch_orig(group, mode);
    } __finally {
        InterlockedDecrement(&g_depth[mode]);
        left = InterlockedDecrement(&g_main_depth);
        if (AbnormalTermination() && left == 0) {
            DropPending(0);
            DropPending(1);
        }
    }
    // A nested batch never drains: the outermost one does after its own join.
    if (left != 0) return;
    Max(g_batch_us_max, static_cast<LONG>(NowUs() - t0));
    const LONG s = InterlockedCompareExchange(&g_pending[mode], 0, 1);
    if (s == 1) {
        Drain(group, mode);
    } else if (s == 2) {
        // Claimed but never published: impossible after the join.
        InterlockedIncrement(&g_torn);
        InterlockedExchange(&g_pending[mode], 0);
        DeferFault(FaultPhase::torn, 0);
    }
}

// --- H1: the job slot wrapper ---------------------------------------------------
void W50(void* go, const JobParam* p) noexcept {
    const auto ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    InterlockedIncrement(&g_w50);
    const auto tid = static_cast<std::uint32_t>(GetCurrentThreadId());
    const std::uint32_t hook = HookThread();
    InterlockedIncrement(hook && tid == hook ? &g_inline : &g_worker);
    const LONG pools = g_id_pools;
    const auto id_go = static_cast<std::uintptr_t>(g_id_go);
    const bool identity = (pools & 0x10000) != 0;
    const bool mine = identity && reinterpret_cast<std::uintptr_t>(go) == id_go;
    if (p->sub != 0) InterlockedIncrement(&g_subs);
    else if (mine && p->block == static_cast<std::uint32_t>(pools & 0xFF)) InterlockedIncrement(&g_b0);
    else if (mine && p->block == static_cast<std::uint32_t>((pools >> 8) & 0xFF)) InterlockedIncrement(&g_b1);
    else InterlockedIncrement(&g_other);
    detail::DecisionIn in{};
    in.mode = static_cast<Mode>(g_mode);
    in.armed = g_armed != 0 && !g_shutdown;
    in.faulted = g_faulted != 0;
    in.h2_ready = g_h2 != 0;
    in.ret = ret;
    in.expected_ret = g_job_return;
    in.pmode = p->mode;
    in.sub = p->sub;
    in.block = p->block;
    in.batch_active = p->mode <= 1 && g_depth[p->mode] != 0;
    in.nested = g_main_depth > 1;
    in.foreign = g_foreign != 0;
    in.identity = identity;
    in.go = reinterpret_cast<std::uintptr_t>(go);
    in.id_go = id_go;
    in.id_pool1 = static_cast<std::uint8_t>((pools >> 8) & 0xFF);
    const detail::Pass why = detail::DeferDecision(in);
    if (why == detail::Pass::defer) {
        if (InterlockedCompareExchange(&g_pending[p->mode], 2, 0) == 0) {
            Rec& r = g_def[p->mode];
            r.go = in.go;
            r.block = p->block;
            r.nbits = p->nbits;
            r.bits = p->bits;
            r.tid = tid;
            InterlockedExchange(&g_pending[p->mode], 1);
            InterlockedIncrement(&g_deferred);
            return;
        }
        InterlockedIncrement(&g_dup);
    } else {
        if (why == detail::Pass::ret_other) InterlockedIncrement(&g_ret_other);
        InterlockedIncrement(&g_pass[static_cast<unsigned>(why) & 15u]);
    }
    g_job_orig(go, p);
}

const char* InstallH2() noexcept {
    std::uintptr_t mismatch = 0;
    if (const char* why = detail::H2SitesMatch(g_base, mismatch)) {
        LogFormat("partner_defer=refused; reason=%s; at=h2; rva=0x%llx; installed_before=none\r\n", why,
                  static_cast<unsigned long long>(mismatch));
        return why;
    }
    g_batch_orig = reinterpret_cast<detail::BatchFn>(g_base + c::batch_target_rva);
    g_scope_ctor = reinterpret_cast<detail::ScopeCtorFn>(g_base + c::scope_ctor_rva);
    g_scope_dtor = reinterpret_cast<detail::ScopeDtorFn>(g_base + c::scope_dtor_rva);
    void* stub = nullptr;
    const detail::Verdict v = detail::ClassifyPatch(
        hk::detail::RetargetCall(g_base + c::batch_site, g_base + c::batch_next_ip, c::batch_rel32_orig,
                                 reinterpret_cast<const void*>(&WrapBatch), stub), false);
    if (!v.installed) {
        LogFormat("partner_defer=refused; reason=%s; at=h2; installed_before=none\r\n", v.reason);
        return v.reason;
    }
    g_h2_stub = stub;
    if (v.warn) Log("partner_defer=warn; reason=protect_restore; at=h2\r\n");
    InterlockedExchange(&g_h2, 1);
    return nullptr;
}

const char* InstallH1() noexcept {
    std::uintptr_t mismatch = 0;
    if (const char* why = detail::H1SitesMatch(g_base, mismatch)) {
        LogFormat("partner_defer=refused; reason=%s; at=h1; rva=0x%llx; installed_before=h2_pass_through\r\n", why,
                  static_cast<unsigned long long>(mismatch));
        return why;
    }
    const std::uintptr_t slot = g_base + c::job_slot_rva;
    if (Load<std::uintptr_t>(slot) != g_base + c::job_rva) {
        Log("partner_defer=refused; reason=slot; at=h1; installed_before=h2_pass_through\r\n");
        return "slot";
    }
    g_job_orig = reinterpret_cast<detail::JobFn>(g_base + c::job_rva);
    g_job_return = g_base + c::job_call_return_rva;
    const detail::Verdict v = detail::ClassifyPatch(
        near_code::PatchQword(reinterpret_cast<void*>(slot), g_base + c::job_rva, reinterpret_cast<std::uintptr_t>(&W50)), true);
    if (!v.installed) {
        LogFormat("partner_defer=refused; reason=%s; at=h1; installed_before=h2_pass_through\r\n", v.reason);
        return v.reason;
    }
    if (v.warn) Log("partner_defer=warn; reason=protect_restore; at=h1\r\n");
    InterlockedExchange(&g_h1, 1);
    return nullptr;
}

} // namespace

const char* ModeName(Mode mode) noexcept {
    switch (mode) {
    case Mode::census: return "census";
    case Mode::on: return "on";
    default: return "off";
    }
}

const char* FaultPhaseName(std::uint32_t phase) noexcept {
    switch (static_cast<FaultPhase>(phase)) {
    case FaultPhase::lost: return "lost";
    case FaultPhase::torn: return "torn";
    case FaultPhase::gate: return "gate";
    case FaultPhase::scope: return "scope";
    case FaultPhase::find: return "find";
    case FaultPhase::lock: return "lock";
    case FaultPhase::job: return "job";
    default: return "none";
    }
}

const char* Install(const void* base, Mode mode, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_install_tried, 1, 0) != 0) return g_refusal;
    const DWORD error = GetLastError();
    g_base = reinterpret_cast<std::uintptr_t>(base);
    g_log = log;
    InterlockedExchange(&g_mode, static_cast<LONG>(mode));
    const char* result = InstallH2();
    if (!result) result = InstallH1();
    g_refusal = result;
    if (!result)
        LogFormat("partner_defer=installed; mode=%s; h2=0xbf4d55; stub=0x%llx; h1=0x2295420; w50=0x%llx; scope=0x34470/0x34550\r\n",
                  ModeName(mode), static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_h2_stub)),
                  static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&W50)));
    SetLastError(error);
    return result;
}

bool H2Installed() noexcept { return g_h2 != 0; }
bool H1Installed() noexcept { return g_h1 != 0; }
const char* InstallRefusal() noexcept { return g_refusal; }
void SetMode(Mode mode) noexcept { InterlockedExchange(&g_mode, static_cast<LONG>(mode)); }
Mode CurrentMode() noexcept { return static_cast<Mode>(g_mode); }

bool PatchLive(const void* base) noexcept {
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    if (!g_h1 || !g_h2 || !g_h2_stub) return false;
    if (Load<std::uintptr_t>(b + c::job_slot_rva) != reinterpret_cast<std::uintptr_t>(&W50)) return false;
    std::int32_t rel = 0;
    if (!near_code::Rel32(b + c::batch_next_ip, reinterpret_cast<std::uintptr_t>(g_h2_stub), rel)) return false;
    const auto* site = reinterpret_cast<const unsigned char*>(b + c::batch_site);
    std::int32_t live = 0;
    std::memcpy(&live, site + 1, 4);
    return site[0] == 0xE8 && live == rel;
}

void PublishIdentity(const Identity& id) noexcept {
    InterlockedExchange64(&g_id_go, static_cast<LONG64>(id.go));
    InterlockedExchange(&g_id_pools, static_cast<LONG>(id.pool0 | (id.pool1 << 8) | 0x10000));
}
void ClearIdentity() noexcept {
    InterlockedExchange(&g_id_pools, 0);
    InterlockedExchange64(&g_id_go, 0);
}
void SetArmed(bool armed) noexcept { InterlockedExchange(&g_armed, armed && !g_shutdown ? 1 : 0); }
void Shutdown() noexcept {
    InterlockedExchange(&g_shutdown, 1);
    InterlockedExchange(&g_armed, 0);
}
bool Armed() noexcept { return g_armed != 0; }

void MissionStart() noexcept {
    InterlockedExchange(&g_armed, 0);
    InterlockedExchange(&g_fault_phase, 0);
    InterlockedExchange(&g_fault_code, 0);
    InterlockedExchange(&g_faulted, 0);
}

bool InBatch() noexcept { return (g_main_depth | g_foreign) != 0; }

void DrainStats(Stats& out) noexcept {
    out = Stats{};
    out.h1 = g_h1 != 0;
    out.h2 = g_h2 != 0;
    out.mode = static_cast<Mode>(g_mode);
    out.armed = g_armed != 0;
    out.faulted = g_faulted != 0;
    out.fault_phase = static_cast<std::uint32_t>(g_fault_phase);
    out.fault_code = static_cast<std::uint32_t>(g_fault_code);
    out.batches = static_cast<std::uint32_t>(g_batches);
    out.batch_threads_seen = static_cast<std::uint32_t>(g_thread_count);
    out.batch_thread = static_cast<std::uint32_t>(g_batch_thread);
    out.hook_thread = HookThread();
    out.w50_calls = static_cast<std::uint32_t>(g_w50);
    out.w50_b0_s0 = static_cast<std::uint32_t>(g_b0);
    out.w50_b1_s0 = static_cast<std::uint32_t>(g_b1);
    out.w50_other_s0 = static_cast<std::uint32_t>(g_other);
    out.w50_subs = static_cast<std::uint32_t>(g_subs);
    out.w50_inline = static_cast<std::uint32_t>(g_inline);
    out.w50_worker = static_cast<std::uint32_t>(g_worker);
    out.ret_other = static_cast<std::uint32_t>(g_ret_other);
    out.deferred = static_cast<std::uint32_t>(g_deferred);
    out.dup = static_cast<std::uint32_t>(g_dup);
    for (unsigned i = 0; i < 16; ++i) out.pass_reason[i] = static_cast<std::uint32_t>(g_pass[i]);
    out.drains[0] = static_cast<std::uint32_t>(g_drains[0]);
    out.drains[1] = static_cast<std::uint32_t>(g_drains[1]);
    out.windowed = static_cast<std::uint32_t>(g_windowed);
    out.unwindowed = static_cast<std::uint32_t>(g_unwindowed);
    out.lost = static_cast<std::uint32_t>(g_lost);
    out.gate_closed = static_cast<std::uint32_t>(g_gate_closed);
    out.torn = static_cast<std::uint32_t>(g_torn);
    out.entry_changed = static_cast<std::uint32_t>(g_entry_changed);
    out.scope_unbalanced = static_cast<std::uint32_t>(g_scope_unbalanced);
    out.id1_pass = static_cast<std::uint32_t>(g_id1_pass);
    out.drain_us_max = static_cast<std::uint32_t>(InterlockedExchange(&g_drain_us_max, 0));
    out.batch_us_max = static_cast<std::uint32_t>(InterlockedExchange(&g_batch_us_max, 0));
    out.spin_max = static_cast<std::uint32_t>(InterlockedExchange(&g_spin_max, 0));
    out.nested_batches = static_cast<std::uint32_t>(g_nested_batches);
    out.foreign_batches = static_cast<std::uint32_t>(g_foreign_batches);
    out.unwound = static_cast<std::uint32_t>(g_unwound);
}

namespace detail {
const char* PassName(Pass pass) noexcept {
    switch (pass) {
    case Pass::defer: return "defer";
    case Pass::mode: return "mode";
    case Pass::unarmed: return "unarmed";
    case Pass::faulted: return "faulted";
    case Pass::h2: return "h2";
    case Pass::ret_other: return "ret_other";
    case Pass::pmode: return "pmode";
    case Pass::no_batch: return "no_batch";
    case Pass::nested: return "nested";
    case Pass::foreign: return "foreign";
    case Pass::sub: return "sub";
    case Pass::identity: return "identity";
    case Pass::go: return "go";
    case Pass::block: return "block";
    default: return "?";
    }
}

Pass DeferDecision(const DecisionIn& in) noexcept {
    if (in.mode != Mode::on) return Pass::mode;
    if (!in.armed) return Pass::unarmed;
    if (in.faulted) return Pass::faulted;
    if (!in.h2_ready) return Pass::h2;
    if (!in.expected_ret || in.ret != in.expected_ret) return Pass::ret_other;
    if (in.pmode > 1) return Pass::pmode;
    if (!in.batch_active) return Pass::no_batch;
    if (in.nested) return Pass::nested;
    if (in.foreign) return Pass::foreign;
    if (in.sub != 0) return Pass::sub;
    if (!in.identity) return Pass::identity;
    if (in.go != in.id_go) return Pass::go;
    if (in.block != in.id_pool1) return Pass::block;
    return Pass::defer;
}

std::uintptr_t FindEntry(std::uintptr_t begin, std::uintptr_t end, std::uintptr_t go) noexcept {
    if (!begin || end < begin || ((end - begin) & 7u) != 0) return 0;
    const std::size_t n = static_cast<std::size_t>((end - begin) / 8);
    if (n > kMaxEntries) return 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uintptr_t entry = Load<std::uintptr_t>(begin + 8 * i);
        if (!entry) continue;
        if (Load<std::uintptr_t>(entry + c::entry_go) == go) return entry;
    }
    return 0;
}

bool BatchBlockMatches(const unsigned char* block) noexcept {
    const c::Site* s = SiteAt(c::batch_block_rva);
    if (!s || s->size != 17) return false;
    for (std::size_t i = 0; i < s->size; ++i) {
        if (i >= 6 && i < 10) continue;  // the retargetable rel32 (0xBF4D56)
        if (block[i] != s->bytes[i]) return false;
    }
    return block[5] == 0xE8;
}

const char* H2SitesMatch(std::uintptr_t base, std::uintptr_t& mismatch) noexcept {
    mismatch = c::batch_block_rva;
    if (!BatchBlockMatches(reinterpret_cast<const unsigned char*>(base + c::batch_block_rva))) return "bytes";
    static constexpr std::uintptr_t kBytes[] = {c::batch_target_rva, 0xbf4f53, c::batch_join_rva, 0xbf4fd0, 0xbf4d20, 0xbf4d40};
    for (const std::uintptr_t rva : kBytes) if (!SiteOk(base, rva)) { mismatch = rva; return "bytes"; }
    static constexpr std::uintptr_t kScope[] = {c::batch_scope_ctor_site, c::batch_scope_dtor_site, c::scope_ctor_rva,
                                                c::scope_dtor_rva, c::scope_push_rva, c::scope_pop_rva, 0x34349};
    for (const std::uintptr_t rva : kScope) if (!SiteOk(base, rva)) { mismatch = rva; return "scope"; }
    mismatch = 0;
    return nullptr;
}

const char* H1SitesMatch(std::uintptr_t base, std::uintptr_t& mismatch) noexcept {
    static constexpr std::uintptr_t kSites[] = {c::job_entry_rva, c::job_param_rva, c::job_lock_rva, c::job_gate_rva, c::job_call_rva,
                                                c::job_unlock_rva, c::lock_helper_rva, c::unlock_helper_rva, c::job_prologue_rva,
                                                c::job_sub_rva, c::job_blk_rva, c::job_bit_rva, c::job_driver_rva, c::inline_run_rva,
                                                0xbf4ff0, 0xbf508c};
    for (const std::uintptr_t rva : kSites) if (!SiteOk(base, rva)) { mismatch = rva; return "bytes"; }
    mismatch = 0;
    return nullptr;
}

Verdict ClassifyPatch(const char* why, bool slot) noexcept {
    if (!why) return Verdict{true, false, nullptr};
    if (std::strcmp(why, "protect_restore") == 0) return Verdict{true, true, nullptr};
    if (slot && std::strcmp(why, "changed") == 0) return Verdict{false, false, "slot"};
    return Verdict{false, false, why};
}

void BindForTest(const Bindings& b) noexcept {
    if (b.batch) g_batch_orig = b.batch;
    if (b.job) g_job_orig = b.job;
    if (b.scope_ctor) g_scope_ctor = b.scope_ctor;
    if (b.scope_dtor) g_scope_dtor = b.scope_dtor;
    if (b.scope_read) g_scope_read = b.scope_read;
    if (b.job_return) g_job_return = b.job_return;
    if (b.hook_thread) g_hook_thread_fn = b.hook_thread;
}

void ResetForTest() noexcept {
    g_install_tried = g_h2 = g_h1 = 0;
    g_refusal = nullptr;
    g_base = 0;
    g_h2_stub = nullptr;
    g_batch_orig = nullptr;
    g_job_orig = nullptr;
    g_scope_ctor = nullptr;
    g_scope_dtor = nullptr;
    g_scope_read = nullptr;
    g_hook_thread_fn = nullptr;
    g_job_return = 0;
    g_mode = g_armed = g_faulted = g_fault_phase = g_fault_code = 0;
    g_id_go = 0;
    g_id_pools = 0;
    g_depth[0] = g_depth[1] = g_main_depth = g_foreign = g_pending[0] = g_pending[1] = 0;
    g_nested_batches = g_foreign_batches = g_unwound = 0;
    std::memset(const_cast<Rec*>(g_def), 0, sizeof(g_def));
    g_batches = g_batch_thread = g_thread_count = 0;
    for (auto& t : g_threads) t = 0;
    g_w50 = g_b0 = g_b1 = g_other = g_subs = g_inline = g_worker = g_ret_other = 0;
    g_deferred = g_dup = 0;
    for (auto& p : g_pass) p = 0;
    g_drains[0] = g_drains[1] = g_windowed = g_unwindowed = g_lost = g_gate_closed = g_torn = 0;
    g_entry_changed = g_scope_unbalanced = g_id1_pass = 0;
    g_drain_us_max = g_batch_us_max = g_spin_max = 0;
}

void* WrapBatchEntry() noexcept { return reinterpret_cast<void*>(&WrapBatch); }
void* W50Entry() noexcept { return reinterpret_cast<void*>(&W50); }
} // namespace detail

} // namespace mgscoop::partner_defer
