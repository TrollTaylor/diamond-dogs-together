// Native puppet job deferral (partner_defer, docs/native-puppet.md 4) and the
// native window rules of the input hook (5.1, 5.4), on a synthetic image laid
// out from the generated contract: no game, no real install. Fake originals
// stand in for the batch 0xBF4F20 (worker threads plus inline calls), the job
// 0x9C1B90 and the TLS scratch scope; the job body's call is a tiny thunk
// that calls through the (patched) GO vtable slot +0x50 so W50 sees a real
// return address.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include "mgscoop/nametag_game.hpp"
#include "mgscoop/partner_aim.hpp"
#include "mgscoop/partner_aim_point.hpp"
#include "mgscoop/partner_defer.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_local_detour.hpp"
#include "mgscoop/partner_icon.hpp"
#include "mgscoop/partner_ui_filter.hpp"
#include "mgscoop/partner_placed_census.hpp"
#include "mgscoop/partner_player.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace pd = mgscoop::partner_defer;
namespace hk = mgscoop::partner_input_hook;
namespace pi = mgscoop::partner_input;
namespace c = mgscoop::partner_player_contract;

unsigned checks = 0;
bool g_pose_nested_fixture=false,g_pose_held_gen_fixture=false;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
template <class T>
void Put(std::uintptr_t a, T v) { std::memcpy(reinterpret_cast<void*>(a), &v, sizeof(v)); }
template <class T>
T Get(std::uintptr_t a) { T v{}; std::memcpy(&v, reinterpret_cast<const void*>(a), sizeof(v)); return v; }
std::uintptr_t Addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

// --- Synthetic image -------------------------------------------------------------
struct Image {
    unsigned char* mem = nullptr;
    std::uintptr_t base = 0;
    std::size_t span = 0;
    Image() {
        span = c::scope_tls_index_rva + 0x100;
        for (const auto& s : c::code_sites) if (s.rva + s.size > span) span = s.rva + s.size;
        for (const auto& s : c::vtable_slots) if (s.vtable_rva + s.offset + 8 > span) span = s.vtable_rva + s.offset + 8;
        span = (span + 0xFFFF) & ~static_cast<std::size_t>(0xFFFF);
        mem = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!mem) { std::fprintf(stderr, "image allocation failed\n"); std::exit(2); }
        base = Addr(mem);
        for (const auto& s : c::code_sites) std::memcpy(mem + s.rva, s.bytes, s.size);
        for (const auto& s : c::vtable_slots) Put<std::uintptr_t>(base + s.vtable_rva + s.offset, base + s.target_rva);
    }
    ~Image() { VirtualFree(mem, 0, MEM_RELEASE); }
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
};

std::vector<char> g_log;
std::mutex g_log_lock;
void Log(const char* line) noexcept {
    std::lock_guard<std::mutex> guard(g_log_lock);
    g_log.insert(g_log.end(), line, line + std::strlen(line));
}
bool Logged(const char* part) {
    std::lock_guard<std::mutex> guard(g_log_lock);
    const std::string text(g_log.begin(), g_log.end());
    return text.find(part) != std::string::npos;
}
std::size_t LogCount(const char* part) {
    std::lock_guard<std::mutex> guard(g_log_lock);
    const std::string text(g_log.begin(), g_log.end());
    std::size_t n = 0;
    for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) ++n;
    return n;
}

// --- The job body's call (48 8B 01 / FF 50 50): rcx = GO, rdx = &param --------
using Thunk = void (*)(void* go, const pd::JobParam* param);
Thunk g_thunk = nullptr;
std::uintptr_t g_thunk_return = 0;
void BuildThunk() {
    auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    const unsigned char bytes[] = {0x48, 0x83, 0xEC, 0x28,  // sub rsp, 0x28
                                   0x48, 0x8B, 0x01,        // mov rax, [rcx]
                                   0xFF, 0x50, 0x50,        // call [rax+0x50]
                                   0x48, 0x83, 0xC4, 0x28,  // add rsp, 0x28
                                   0xC3};
    std::memcpy(code, bytes, sizeof(bytes));
    FlushInstructionCache(GetCurrentProcess(), code, sizeof(bytes));
    g_thunk = reinterpret_cast<Thunk>(code);
    g_thunk_return = Addr(code) + 10;
}

// --- Fake game objects: GO (vtable, flags), the group entry, the group -------
struct World {
    alignas(16) unsigned char go[0x40]{};
    alignas(16) unsigned char flags[0x40]{};
    alignas(16) unsigned char entry[0x40]{};
    alignas(16) unsigned char other_entry[0x40]{};
    std::uint8_t bits[4]{};
    std::uintptr_t entries[3]{};
    std::uintptr_t group[2]{};  // [begin, end) of the entry pointer vector
    void Reset(const Image& img) {
        std::memset(this, 0, sizeof(*this));
        Put<std::uintptr_t>(Addr(go), img.base + c::go_vtable_rva);
        Put<std::uintptr_t>(Addr(go) + c::go_flags_ptr, Addr(flags));
        Put<std::uint32_t>(Addr(flags) + c::go_flags, c::go_flags_bit);
        bits[0] = 0x2;  // id 1's bit = 1 (pass 1 runs its phases)
        Put<std::uint32_t>(Addr(entry) + c::entry_nbits, 8);
        Put<std::uint32_t>(Addr(entry) + c::entry_jobs, 2);
        Put<std::uint32_t>(Addr(entry) + c::entry_subs, 1);
        Put<std::uintptr_t>(Addr(entry) + c::entry_go, Addr(go));
        Put<std::uintptr_t>(Addr(entry) + c::entry_bits, Addr(bits));
        Put<std::uintptr_t>(Addr(other_entry) + c::entry_go, 0x1234);
        entries[0] = Addr(other_entry);
        entries[1] = 0;  // null entries are skipped
        entries[2] = Addr(entry);
        group[0] = Addr(entries);
        group[1] = Addr(entries) + sizeof(entries);
    }
};
World g_world;

// --- Fake originals ----------------------------------------------------------------
// With the input hook installed (TestJobWindow) the jobs also record what the
// shared pad looks like when they run: src+0x48 (held), PP+0x27C (HOLD latch)
// and the PP vtable pointer (the window installs a copy).
std::uintptr_t g_watch_src = 0, g_watch_pp = 0, g_watch_rec = 0;
struct JobCall {
    std::uint32_t tid, block, sub, mode, nbits;
    int scope_depth;
    LONG lock;
    bool in_batch;
    std::uint32_t held, latch;
    std::uintptr_t pp_vt;
    // Instance 1's isLocal word (rec1+0x2AC) and the phase-1 detour's force
    // state while the job runs.
    std::uint32_t rec_local;
    mgscoop::partner_local_detour::detail::ForceState p1;
};
std::mutex g_calls_lock;
std::vector<JobCall> g_calls;
std::uint32_t g_main_tid = 0;
std::uint32_t MainTid() noexcept { return g_main_tid; }

thread_local std::uintptr_t t_scope[16];
thread_local int t_depth = 0;
std::atomic<int> g_scope_errors{0};
std::atomic<LONG> g_lock_at_dtor{-99};
std::atomic<int> g_ctor_count{0}, g_dtor_count{0};
void* FakeScopeCtor(void* scope) noexcept {
    if (t_depth < 16) t_scope[t_depth++] = Addr(scope);
    else ++g_scope_errors;
    ++g_ctor_count;
    return scope;
}
void FakeScopeDtor(void* scope) noexcept {
    if (t_depth <= 0 || t_scope[t_depth - 1] != Addr(scope)) ++g_scope_errors;  // strict LIFO
    else --t_depth;
    g_lock_at_dtor = Get<LONG>(Addr(g_world.entry) + c::entry_lock);
    ++g_dtor_count;
}
std::uintptr_t FakeScopeRead() noexcept { return t_depth ? t_scope[t_depth - 1] : 0; }

std::atomic<bool> g_raise{false};
void FakeJob(void* go, const pd::JobParam* p) noexcept {
    JobCall call{};
    call.tid = GetCurrentThreadId();
    call.block = p->block;
    call.sub = p->sub;
    call.mode = p->mode;
    call.nbits = p->nbits;
    call.scope_depth = t_depth;
    call.lock = Get<LONG>(Addr(g_world.entry) + c::entry_lock);
    call.in_batch = pd::InBatch();
    if (g_watch_src) call.held = Get<std::uint32_t>(g_watch_src + 0x48);
    if (g_watch_pp) {
        call.latch = Get<std::uint32_t>(g_watch_pp + c::pp_hold_latch);
        call.pp_vt = Get<std::uintptr_t>(g_watch_pp);
    }
    if (g_watch_rec) call.rec_local = Get<std::uint32_t>(g_watch_rec + c::rec_local);
    call.p1 = mgscoop::partner_local_detour::detail::ReadForceForTest();
    (void)go;
    {
        std::lock_guard<std::mutex> guard(g_calls_lock);
        g_calls.push_back(call);
    }
    if (g_raise && call.tid == g_main_tid && p->block == 1) {
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    }
}

// What the fake batch does (the job body's per-(entry, block) job, then the
// optional disturbances after the join).
struct Scenario {
    std::uint32_t subs = 1;
    bool block1_inline = false;   // block 1's job runs on the batch thread (inline during the join)
    bool dup = false;             // block 1's job submitted twice
    bool mission_start = false;   // MissionStart after the join
    bool drop_entry = false, close_gate = false, change_nbits = false, hold_lock = false;
    bool ret_other = false;       // also call W50 directly (another return address)
    bool wrap_probe = false;      // call the walk7 wrapper on the batch thread during the batch
    bool svc_probe = false;       // call the svc28 wrapper for root1 on the batch thread during the batch (block 1's job inline)
    bool nest = false;            // after the join, a nested hook-thread batch (pass nest_mode) inside this one
    std::uint32_t nest_mode = 0;
    bool raise_after_join = false;  // an access violation escapes the batch after its join
    bool foreign_hold = false;    // a batch on another thread stays in flight until released (no jobs)
    bool window_probe = false;    // RunJobWindow called during the batch (TestJobWindow)
    bool dispatch_probe = false;  // the main window (0x9C2B51 stub) called during the batch (TestJobWindow)
};
Scenario g_sc;
std::thread g_releaser;
std::atomic<bool> g_nesting{false}, g_inner_in_batch{false};
std::atomic<bool> g_foreign_entered{false}, g_foreign_release{false};
std::atomic<int> g_probe_ran{0};
std::atomic<bool> g_probe_windowed{false};
void* g_job_go = nullptr;  // TestJobWindow: the GO the job body calls (default g_world.go)
void* JobGo() { return g_job_go ? g_job_go : static_cast<void*>(g_world.go); }
void ProbeJob(void*, const void*) noexcept { ++g_probe_ran; }
void Job(std::uint32_t block, std::uint32_t sub, std::uint32_t mode) {
    pd::JobParam param{block, sub, mode, Get<std::uint32_t>(Addr(g_world.entry) + c::entry_nbits),
                       Get<const std::uint8_t*>(Addr(g_world.entry) + c::entry_bits)};
    g_thunk(JobGo(), &param);
}
using DispatchEntry = void (*)(void* root, std::uint32_t phase);
void* g_probe_root = nullptr;
void FakeBatch(void* group, std::uint32_t mode) noexcept {
    if (g_sc.foreign_hold && GetCurrentThreadId() != g_main_tid) {
        g_foreign_entered = true;
        while (!g_foreign_release) Sleep(1);
        return;
    }
    std::vector<std::thread> workers;
    for (std::uint32_t block = 0; block < 2; ++block)
        for (std::uint32_t sub = 0; sub < g_sc.subs; ++sub) {
            if (block == 1 && g_sc.block1_inline) Job(block, sub, mode);
            else workers.emplace_back([=] { Job(block, sub, mode); });
            if (block == 1 && g_sc.dup) workers.emplace_back([=] { Job(block, sub, mode); });
        }
    if (g_sc.ret_other) {
        pd::JobParam param{1, 0, mode, 8, g_world.bits};
        reinterpret_cast<pd::detail::JobFn>(pd::detail::W50Entry())(g_world.go, &param);
    }
    if (g_sc.wrap_probe) hk::detail::CallWrapForTest(3, nullptr, reinterpret_cast<void*>(0x5000), 7, 0);
    if (g_sc.svc_probe) hk::detail::CallWrapForTest(0, nullptr, reinterpret_cast<void*>(0x5000), 3, 0);
    if (g_sc.window_probe) {
        pd::JobParam param{1, 0, mode, 8, g_world.bits};
        g_probe_windowed = hk::RunJobWindow(JobGo(), &param, 1, &ProbeJob, pi::Consumer::drain1);
    }
    if (g_sc.dispatch_probe) reinterpret_cast<DispatchEntry>(hk::detail::DispatchStubForTest())(g_probe_root, 8);
    for (auto& w : workers) w.join();
    if (g_sc.nest && !g_nesting.exchange(true)) {
        reinterpret_cast<pd::detail::BatchFn>(pd::detail::WrapBatchEntry())(group, g_sc.nest_mode);
        g_inner_in_batch = pd::InBatch();
        g_nesting = false;
    }
    if (g_sc.raise_after_join) {
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    }
    if (g_sc.mission_start) pd::MissionStart();
    if (g_sc.drop_entry) g_world.group[1] = g_world.group[0];
    if (g_sc.close_gate) Put<std::uint32_t>(Addr(g_world.flags) + c::go_flags, 0);
    if (g_sc.change_nbits) Put<std::uint32_t>(Addr(g_world.entry) + c::entry_nbits, 16);
    if (g_sc.hold_lock) {
        Put<LONG>(Addr(g_world.entry) + c::entry_lock, -1);
        g_releaser = std::thread([] {
            Sleep(5);
            InterlockedExchange(reinterpret_cast<volatile LONG*>(Addr(g_world.entry) + c::entry_lock), 0);
        });
    }
}

void RunBatch(std::uint32_t mode) {
    reinterpret_cast<pd::detail::BatchFn>(pd::detail::WrapBatchEntry())(g_world.group, mode);
    if (g_releaser.joinable()) g_releaser.join();
}
// The batch with an exception escaping the job (POD only: SEH).
bool RunBatchCatching(std::uint32_t mode) {
    bool caught = false;
    __try {
        reinterpret_cast<pd::detail::BatchFn>(pd::detail::WrapBatchEntry())(g_world.group, mode);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        caught = true;
    }
    return caught;
}
// G1 (TestJobWindow): a drain window whose job raises a non-game exception (not a fatal code, so the input hook
// records no fault); it unwinds through GuardedCall and RunJobWindowOutcome to this handler (POD only: SEH).
inline constexpr DWORD kUiJobRaise = 0xE0471001u;
bool RunJobWindowCatching(void* go, const void* param, std::uint32_t block, hk::JobCallFn job) {
    bool caught = false;
    __try {
        hk::RunJobWindow(go, param, block, job, pi::Consumer::drain0);
    } __except (GetExceptionCode() == kUiJobRaise ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        caught = true;
    }
    return caught;
}

std::vector<JobCall> TakeCalls() {
    std::lock_guard<std::mutex> guard(g_calls_lock);
    std::vector<JobCall> out;
    out.swap(g_calls);
    return out;
}
const JobCall* Find(const std::vector<JobCall>& calls, std::uint32_t block, std::uint32_t sub = 0) {
    for (const auto& c0 : calls) if (c0.block == block && c0.sub == sub) return &c0;
    return nullptr;
}
int Count(const std::vector<JobCall>& calls, std::uint32_t block) {
    int n = 0;
    for (const auto& c0 : calls) if (c0.block == block) ++n;
    return n;
}

// Installs on a fresh image with fakes bound; armed with an identity (pool0 0, pool1 1).
void Setup(Image& img, pd::Mode mode = pd::Mode::on) {
    pd::detail::ResetForTest();
    g_log.clear();
    g_world.Reset(img);
    g_sc = Scenario{};
    g_raise = false;
    TakeCalls();
    Check(pd::Install(reinterpret_cast<void*>(img.base), mode, &Log) == nullptr, "install on the synthetic image");
    pd::detail::Bindings b{};
    b.batch = &FakeBatch;
    b.job = &FakeJob;
    b.scope_ctor = &FakeScopeCtor;
    b.scope_dtor = &FakeScopeDtor;
    b.scope_read = &FakeScopeRead;
    b.job_return = g_thunk_return;
    b.hook_thread = &MainTid;
    pd::detail::BindForTest(b);
    pd::PublishIdentity(pd::Identity{Addr(g_world.go), 0, 1});
    pd::SetArmed(true);
}

void TestPureSeams() {
    using pd::detail::Pass;
    pd::detail::DecisionIn in{};
    in.mode = pd::Mode::on; in.armed = true; in.h2_ready = true;
    in.ret = in.expected_ret = 0x1000; in.pmode = 1; in.batch_active = true; in.identity = true;
    in.go = in.id_go = 0x5000; in.block = in.id_pool1 = 1;
    Check(pd::detail::DeferDecision(in) == Pass::defer, "every condition holds: defer");
    struct Case { Pass expect; void (*edit)(pd::detail::DecisionIn&); };
    const Case cases[] = {
        {Pass::mode, [](pd::detail::DecisionIn& d) { d.mode = pd::Mode::census; }},
        {Pass::mode, [](pd::detail::DecisionIn& d) { d.mode = pd::Mode::off; }},
        {Pass::unarmed, [](pd::detail::DecisionIn& d) { d.armed = false; }},
        {Pass::faulted, [](pd::detail::DecisionIn& d) { d.faulted = true; }},
        {Pass::h2, [](pd::detail::DecisionIn& d) { d.h2_ready = false; }},
        {Pass::ret_other, [](pd::detail::DecisionIn& d) { d.ret = 0x1001; }},
        {Pass::ret_other, [](pd::detail::DecisionIn& d) { d.expected_ret = 0; d.ret = 0; }},
        {Pass::pmode, [](pd::detail::DecisionIn& d) { d.pmode = 2; }},
        {Pass::no_batch, [](pd::detail::DecisionIn& d) { d.batch_active = false; }},
        {Pass::nested, [](pd::detail::DecisionIn& d) { d.nested = true; }},
        {Pass::foreign, [](pd::detail::DecisionIn& d) { d.foreign = true; }},
        {Pass::sub, [](pd::detail::DecisionIn& d) { d.sub = 1; }},
        {Pass::identity, [](pd::detail::DecisionIn& d) { d.identity = false; }},
        {Pass::go, [](pd::detail::DecisionIn& d) { d.go = 0x6000; }},
        {Pass::block, [](pd::detail::DecisionIn& d) { d.block = 0; }},
    };
    for (const Case& k : cases) {
        pd::detail::DecisionIn e = in;
        k.edit(e);
        Check(pd::detail::DeferDecision(e) == k.expect, pd::detail::PassName(k.expect));
    }
    // FindEntry: bounds, null entries, misalignment.
    std::uintptr_t go_a[8]{}, go_b[8]{};
    go_a[c::entry_go / 8] = 0xAAAA;
    go_b[c::entry_go / 8] = 0xBBBB;
    std::uintptr_t vec[3] = {0, Addr(go_a), Addr(go_b)};
    Check(pd::detail::FindEntry(Addr(vec), Addr(vec) + sizeof(vec), 0xBBBB) == Addr(go_b), "found past a null entry");
    Check(pd::detail::FindEntry(Addr(vec), Addr(vec) + sizeof(vec), 0xCCCC) == 0, "absent go");
    Check(pd::detail::FindEntry(Addr(vec), Addr(vec) + 12, 0xAAAA) == 0, "misaligned span refused");
    Check(pd::detail::FindEntry(0, 8, 0xAAAA) == 0 && pd::detail::FindEntry(Addr(vec) + 8, Addr(vec), 0xAAAA) == 0, "null/reversed span");
    Check(pd::detail::FindEntry(Addr(vec), Addr(vec) + 8 * (pd::detail::kMaxEntries + 1), 0xAAAA) == 0, "more than 4096 entries refused");
    // ClassifyPatch: protect_restore = installed (the patch is live).
    auto v = pd::detail::ClassifyPatch(nullptr, true);
    Check(v.installed && !v.warn && !v.reason, "patched");
    v = pd::detail::ClassifyPatch("protect_restore", false);
    Check(v.installed && v.warn, "protect_restore is installed with a warning");
    v = pd::detail::ClassifyPatch("changed", true);
    Check(!v.installed && std::strcmp(v.reason, "slot") == 0, "a changed H1 slot is 'slot'");
    v = pd::detail::ClassifyPatch("alignment", true);
    Check(!v.installed && std::strcmp(v.reason, "alignment") == 0, "alignment refused");
    v = pd::detail::ClassifyPatch("exchange", false);
    Check(!v.installed && std::strcmp(v.reason, "exchange") == 0, "H2 exchange refused");
    Check(std::strcmp(pd::ModeName(pd::Mode::census), "census") == 0 && std::strcmp(pd::FaultPhaseName(7), "job") == 0, "names");
}

// The phase-1 detour's hand-assembled stub and its jumps (pure; the install
// and the forced branch run in TestJobWindow on the contract image).
void TestDetourPure() {
    namespace p1 = mgscoop::partner_local_detour;
    unsigned char s[p1::detail::kStubSize]{};
    p1::detail::BuildStub(s, 0x1122334455667788ull, 0x0000000140A9E186ull);
    const unsigned char setne[] = {0x40, 0x0F, 0x95, 0xC5};
    Check(std::memcmp(s, setne, 4) == 0, "stub: the displaced setne bpl runs first, on the original flags");
    Check(s[0x05] == 0x48 && s[0x06] == 0xB9 && Get<std::uint64_t>(Addr(s) + p1::detail::kStubStateAt) == 0x1122334455667788ull,
          "stub: mov rcx, state carries the state address");
    Check(s[0x0F] == 0x3B && s[0x10] == 0x11 && s[0x11] == 0x75 && 0x13 + s[0x12] == 0x35, "stub: an id mismatch skips to pop rcx");
    Check(s[0x13] == 0x48 && s[0x14] == 0x3B && s[0x15] == 0x41 && s[0x16] == 0x10 && 0x19 + s[0x18] == 0x31,
          "stub: a P mismatch counts a refusal");
    Check(s[0x1A] == 0x65 && s[0x1D] == 0x25 && Get<std::uint32_t>(Addr(s) + 0x1E) == 0x48 && 0x28 + s[0x27] == 0x31,
          "stub: the thread check reads TEB+0x48 (ClientId.UniqueThread)");
    Check(s[0x28] == 0x40 && s[0x29] == 0xB5 && s[0x2A] == 0x00, "stub: the forced local branch is mov bpl, 0");
    Check(s[0x35] == 0x59 && s[0x36] == 0xFF && s[0x37] == 0x25 && Get<std::uint32_t>(Addr(s) + 0x38) == 0 &&
          Get<std::uint64_t>(Addr(s) + p1::detail::kStubBackAt) == 0x0000000140A9E186ull, "stub: pop rcx; jmp [rip] back");
    unsigned char j[5]{};
    Check(p1::detail::BuildJump(j, 0x1000, 0x2000) && j[0] == 0xE9 && Get<std::int32_t>(Addr(j) + 1) == 0x2000 - 0x1005,
          "rel32 jump forward");
    Check(p1::detail::BuildJump(j, 0x2000, 0x1000) && Get<std::int32_t>(Addr(j) + 1) == 0x1000 - 0x2005, "rel32 jump backward");
    Check(!p1::detail::BuildJump(j, 0x1000, 0x1000 + 0x100000000ull), "out of rel32 reach refused");
    const auto rel8 = static_cast<std::int8_t>((c::p1_setne_new >> 8) & 0xFF);
    Check((c::p1_setne_new & 0xFF) == 0xEB && c::p1_setne_rva + 2 + rel8 == c::p1_pad_rva && c::p1_back_rva == c::p1_setne_rva + 4,
          "the 2-byte jmp replacing setne lands on the padding; the stub returns after the 4 replaced bytes");
}
void TestInstall() {
    Image img;
    pd::detail::ResetForTest();
    g_log.clear();
    // The code gate covers the H1/H2 sites, so it must run once per process
    // before any lazy install (the observer runs it at configure): before the
    // install it passes, after it the patched sites no longer match.
    std::uintptr_t mismatch = 0;
    Check(mgscoop::partner_player::CodeGate(reinterpret_cast<void*>(img.base), img.span, mismatch) && mismatch == 0,
          "the code gate passes on the pristine contract image");
    Check(pd::Install(reinterpret_cast<void*>(img.base), pd::Mode::census, &Log) == nullptr && pd::H2Installed() && pd::H1Installed(),
          "install succeeds on the contract bytes");
    Check(!mgscoop::partner_player::CodeGate(reinterpret_cast<void*>(img.base), img.span, mismatch) &&
          (mismatch == c::batch_block_rva || mismatch == c::go_vtable_rva),
          "after the install the gate sees the patched H2 block or H1 slot (never re-run it after an install)");
    Check(Get<std::uintptr_t>(img.base + c::job_slot_rva) == Addr(pd::detail::W50Entry()), "H1 slot holds W50");
    std::int32_t rel = 0;
    std::memcpy(&rel, img.mem + c::batch_site + 1, 4);
    const std::uintptr_t stub = img.base + c::batch_next_ip + static_cast<std::intptr_t>(rel);
    std::uintptr_t target = 0;
    std::memcpy(&target, reinterpret_cast<const void*>(stub + 6), 8);
    Check(img.mem[c::batch_site] == 0xE8 && Get<std::uint16_t>(stub) == 0x25FF && target == Addr(pd::detail::WrapBatchEntry()),
          "H2 rel32 reaches a near stub jumping to WrapBatch");
    Check(pd::PatchLive(reinterpret_cast<void*>(img.base)), "patch live");
    Check(Logged("partner_defer=installed; mode=census; h2=0xbf4d55") && Logged("h1=0x2295420"), "installed line");
    Check(pd::Install(reinterpret_cast<void*>(img.base), pd::Mode::on, &Log) == nullptr && pd::CurrentMode() == pd::Mode::census,
          "a second install is idempotent (no re-patch, mode unchanged)");
    Put<std::uintptr_t>(img.base + c::job_slot_rva, img.base + c::job_rva);
    Check(!pd::PatchLive(reinterpret_cast<void*>(img.base)), "a restored slot is not live");
}

void TestRefusals() {
    struct Flip { std::uintptr_t rva; const char* reason; bool h2_after; };
    const Flip flips[] = {
        {c::batch_block_rva + 1, "bytes", false},      // H2 block (outside the rel32)
        {c::batch_target_rva + 3, "bytes", false},     // batch prologue
        {c::batch_join_rva + 10, "bytes", false},      // the join loop
        {c::batch_scope_ctor_site + 6, "scope", false},
        {c::batch_scope_dtor_site + 6, "scope", false},
        {c::scope_ctor_rva + 2, "scope", false},
        {c::scope_dtor_rva + 2, "scope", false},
        {c::scope_push_rva + 8, "scope", false},
        {c::scope_pop_rva + 8, "scope", false},
        {c::job_call_rva + 9, "bytes", true},          // job body vt+0x50 call (H1 site)
        {c::job_lock_rva + 3, "bytes", true},
        {c::job_prologue_rva + 4, "bytes", true},
        {c::job_bit_rva + 2, "bytes", true},
    };
    for (const Flip& f : flips) {
        Image img;
        pd::detail::ResetForTest();
        img.mem[f.rva] ^= 0x01;
        std::uint32_t rel_before = 0;
        std::memcpy(&rel_before, img.mem + c::batch_site + 1, 4);
        const std::uintptr_t slot_before = Get<std::uintptr_t>(img.base + c::job_slot_rva);
        const char* why = pd::Install(reinterpret_cast<void*>(img.base), pd::Mode::on, &Log);
        Check(why && std::strcmp(why, f.reason) == 0, f.reason);
        Check(pd::H2Installed() == f.h2_after && !pd::H1Installed() && Get<std::uintptr_t>(img.base + c::job_slot_rva) == slot_before,
              "H1 never patched after a refusal");
        std::uint32_t rel_after = 0;
        std::memcpy(&rel_after, img.mem + c::batch_site + 1, 4);
        Check(f.h2_after ? rel_after != rel_before : rel_after == rel_before, "H2 patched only when its sites matched");
        Check(std::strcmp(pd::InstallRefusal(), f.reason) == 0, "sticky refusal");
    }
    // A wrong slot value: 'slot', H2 stays installed as a pass-through.
    Image img;
    pd::detail::ResetForTest();
    g_log.clear();
    Put<std::uintptr_t>(img.base + c::job_slot_rva, img.base + 0x123456);
    const char* why = pd::Install(reinterpret_cast<void*>(img.base), pd::Mode::on, &Log);
    Check(why && std::strcmp(why, "slot") == 0 && pd::H2Installed() && !pd::H1Installed() &&
          Get<std::uintptr_t>(img.base + c::job_slot_rva) == img.base + 0x123456, "wrong slot value refused; H2 pass-through");
    Check(Logged("reason=slot; at=h1; installed_before=h2_pass_through"), "slot refusal line");
    // A pass-through H2 forwards every batch untouched (no identity, nothing
    // deferred); the slot calls the job directly, as the original would.
    Put<std::uintptr_t>(img.base + c::job_slot_rva, Addr(reinterpret_cast<void*>(&FakeJob)));
    g_world.Reset(img);
    pd::detail::Bindings b{};
    b.batch = &FakeBatch; b.job = &FakeJob; b.scope_ctor = &FakeScopeCtor; b.scope_dtor = &FakeScopeDtor;
    b.scope_read = &FakeScopeRead; b.hook_thread = &MainTid; b.job_return = g_thunk_return;
    pd::detail::BindForTest(b);
    g_sc = Scenario{};
    pd::Stats st{};
    pd::DrainStats(st);
    const std::uint32_t batches = st.batches;
    RunBatch(0);
    auto calls = TakeCalls();
    pd::DrainStats(st);
    Check(st.batches == batches + 1 && st.deferred == 0 && st.w50_calls == 0 && Count(calls, 0) == 1 && Count(calls, 1) == 1,
          "the pass-through batch wrapper only counts; both jobs ran through the original slot");
}

void TestEndToEnd() {
    Image img;
    Setup(img);
    // Pass 0 then pass 1 (the ticker's order).
    RunBatch(0);
    auto calls = TakeCalls();
    const JobCall* b0 = Find(calls, 0);
    const JobCall* b1 = Find(calls, 1);
    Check(calls.size() == 2 && b0 && b1, "both blocks ran exactly once");
    Check(b0->tid != g_main_tid && b0->scope_depth == 0, "block 0 ran on its worker, never deferred");
    Check(b1->tid == g_main_tid && b1->mode == 0 && b1->sub == 0 && b1->nbits == 8, "block 1 ran on the batch (main) thread");
    Check(b1->scope_depth == 1 && b1->lock == 1 && !b1->in_batch, "... after the join, inside a pushed scope and the entry read lock");
    Check(t_depth == 0 && Get<LONG>(Addr(g_world.entry) + c::entry_lock) == 0 && g_scope_errors == 0, "scope popped, lock released");
    pd::Stats st{};
    pd::DrainStats(st);
    Check(st.deferred == 1 && st.drains[0] == 1 && st.drains[1] == 0 && st.unwindowed == 1 && st.windowed == 0 && st.lost == 0,
          "one deferral drained (unwindowed: no input hook in the fixture)");
    Check(st.w50_b0_s0 == 1 && st.w50_b1_s0 == 1 && st.w50_subs == 0 && st.ret_other == 0 && st.w50_worker >= 1,
          "sub-0 block counters");
    Check(st.batches == 1 && st.batch_threads_seen == 1 && st.batch_thread == g_main_tid && st.hook_thread == g_main_tid,
          "batch thread == hook thread");
    Check(!(st.id1_pass & 1u), "pass 0 does not carry id 1 (its bit is 1)");
    RunBatch(1);
    calls = TakeCalls();
    b1 = Find(calls, 1);
    pd::DrainStats(st);
    Check(b1 && b1->tid == g_main_tid && b1->mode == 1 && st.drains[1] == 1 && (st.id1_pass & 2u), "pass 1 drained; id1_pass bit 1");
    // Not armed / census: pass-through, counted.
    pd::SetArmed(false);
    RunBatch(0);
    calls = TakeCalls();
    Check(Find(calls, 1) && Find(calls, 1)->tid != g_main_tid, "disarmed: block 1 stays on its worker");
    pd::SetArmed(true);
    pd::SetMode(pd::Mode::census);
    RunBatch(0);
    calls = TakeCalls();
    pd::DrainStats(st);
    Check(Find(calls, 1) && Find(calls, 1)->tid != g_main_tid && st.deferred == 2, "census: counted, never deferred");
    Check(st.pass_reason[static_cast<unsigned>(pd::detail::Pass::mode)] >= 1 &&
          st.pass_reason[static_cast<unsigned>(pd::detail::Pass::unarmed)] >= 1, "pass reasons counted");
    pd::SetMode(pd::Mode::on);
    // Sub counting: entry+0x14 = 2 -> sub 1 never deferred, health uses sub 0.
    g_sc.subs = 2;
    pd::DrainStats(st);
    const std::uint32_t b1s0 = st.w50_b1_s0, subs = st.w50_subs, deferred = st.deferred;
    RunBatch(0);
    calls = TakeCalls();
    pd::DrainStats(st);
    Check(st.w50_b1_s0 == b1s0 + 1 && st.w50_subs == subs + 2 && st.deferred == deferred + 1, "w50 counted at sub 0 only");
    Check(Find(calls, 1, 1) && Find(calls, 1, 1)->tid != g_main_tid && Find(calls, 1, 0)->tid == g_main_tid,
          "sub 1 runs on its worker, sub 0 is deferred");
    g_sc.subs = 1;
}

void TestAnomalies() {
    {
        Image img;
        Setup(img);
        g_sc.dup = true;
        RunBatch(0);
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(st.deferred == 1 && st.dup == 1 && Count(calls, 1) == 2, "a second claim in the same pass is counted and runs where it is");
    }
    {
        Image img;
        Setup(img);
        g_sc.ret_other = true;
        RunBatch(0);
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(st.ret_other == 1 && st.deferred == 1 && Count(calls, 1) == 2, "a W50 call from another return address passes through");
    }
    {
        Image img;
        Setup(img);
        g_sc.block1_inline = true;
        RunBatch(0);
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(st.deferred == 1 && st.w50_inline >= 1 && Count(calls, 1) == 1 && Find(calls, 1)->scope_depth == 1,
              "an inline-run job (main thread during the join) is deferred too and drained after the batch");
    }
    {
        Image img;
        Setup(img);
        g_sc.mission_start = true;
        RunBatch(0);
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(!pd::Armed() && st.deferred == 1 && st.drains[0] == 1 && Find(calls, 1) && Find(calls, 1)->tid == g_main_tid,
              "MissionStart during a batch disarms but never drops the deferred record");
    }
    {
        Image img;
        Setup(img);
        g_sc.drop_entry = true;
        RunBatch(0);
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(st.lost == 1 && st.faulted && st.fault_phase == static_cast<std::uint32_t>(pd::FaultPhase::lost) && Count(calls, 1) == 0,
              "a missing entry: lost, nothing called, deferral faulted");
        g_world.Reset(img);
        RunBatch(0);
        calls = TakeCalls();
        Check(Find(calls, 1) && Find(calls, 1)->tid != g_main_tid, "after the fault nothing is deferred");
        pd::MissionStart();
        pd::SetArmed(true);
        pd::DrainStats(st);
        Check(!st.faulted, "MissionStart clears the deferral fault");
    }
    {
        Image img;
        Setup(img);
        g_sc.close_gate = true;
        RunBatch(0);
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(st.gate_closed == 1 && st.faulted && Count(calls, 1) == 0 && Get<LONG>(Addr(g_world.entry) + c::entry_lock) == 0 && t_depth == 0,
              "gate closed between join and drain: counted, not called, lock and scope balanced");
    }
    {
        Image img;
        Setup(img);
        g_sc.change_nbits = true;
        RunBatch(0);
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(st.entry_changed == 1 && Find(calls, 1) && Find(calls, 1)->nbits == 16, "live nbits/bits win; entry_changed counted");
    }
    {
        Image img;
        Setup(img);
        g_sc.hold_lock = true;
        RunBatch(0);
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(Find(calls, 1) && Find(calls, 1)->lock == 1 && st.spin_max > 0 && Get<LONG>(Addr(g_world.entry) + c::entry_lock) == 0,
              "the drain spins while the entry is write-locked (-1), then takes and releases a read lock");
    }
    {
        // An exception from the job: passed on; the lock is released before the
        // scope is popped (both on unwind); the deferral faults.
        Image img;
        Setup(img);
        g_raise = true;
        g_lock_at_dtor = -99;
        const int dtors = g_dtor_count;
        Check(RunBatchCatching(0), "the job's exception reaches the outer handler");
        g_raise = false;
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(Count(calls, 1) == 1 && g_dtor_count == dtors + 1 && g_lock_at_dtor == 0 && t_depth == 0 &&
              Get<LONG>(Addr(g_world.entry) + c::entry_lock) == 0, "unwind: unlock, then scope pop");
        Check(st.faulted && st.fault_phase == static_cast<std::uint32_t>(pd::FaultPhase::job), "fault_phase=job");
        Check(!pd::InBatch(), "batch_active cleared");
    }
    {
        // A batch on another thread: never marks itself active, never drains.
        Image img;
        Setup(img);
        std::thread other([] { RunBatch(0); });
        other.join();
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(st.deferred == 0 && st.drains[0] == 0 && Find(calls, 1) && Find(calls, 1)->tid != g_main_tid &&
              st.pass_reason[static_cast<unsigned>(pd::detail::Pass::no_batch)] == 2 && st.batch_threads_seen == 1 &&
              st.batch_thread != g_main_tid, "a non-hook-thread batch only passes through");
    }
    {
        // Identity: the published pool decides; no identity, nothing deferred.
        Image img;
        Setup(img);
        pd::ClearIdentity();
        RunBatch(0);
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(st.deferred == 0 && st.w50_b1_s0 == 0 && st.w50_other_s0 == 2, "no identity: no block classes, no deferral");
        pd::PublishIdentity(pd::Identity{Addr(g_world.go), 1, 0});
        RunBatch(0);
        calls = TakeCalls();
        pd::DrainStats(st);
        Check(st.deferred == 1 && Find(calls, 0) && Find(calls, 0)->tid == g_main_tid && Find(calls, 1)->tid != g_main_tid,
              "pool1 = 0: block 0 of the batch is the partner's");
    }
}

// --- Input hook: native WrapEnter rules and the post-phase-0 look write --------
struct WrapLog {
    std::atomic<int> svc28{0}, svc30{0}, svc40{0}, walk7{0}, walk11{0};
    std::atomic<std::uint32_t> last_phase{0};
};
WrapLog g_wrap;
void FakeSvc28(void*, void*, std::uint32_t phase) noexcept { ++g_wrap.svc28; g_wrap.last_phase = phase; }
void FakeSvc30(void*, void*, std::uint32_t) noexcept { ++g_wrap.svc30; }
void FakeSvc40(void*, void*, std::uint32_t, std::uint32_t) noexcept { ++g_wrap.svc40; }
void FakeWalk7(void*, std::uint32_t) noexcept { ++g_wrap.walk7; }
void FakeWalk11(void*, std::uint32_t, std::uint32_t) noexcept { ++g_wrap.walk11; }

void TestWrapRules() {
    void* const root0 = reinterpret_cast<void*>(0x4000);
    void* const root1 = reinterpret_cast<void*>(0x5000);
    hk::detail::BindWrapForTest(&FakeSvc28, &FakeSvc30, &FakeSvc40, &FakeWalk7, &FakeWalk11, g_main_tid);
    hk::Stats st{};
    // Census without an enabled Arm: counts.
    hk::PublishWrapRoots(0x4000, 0x5000, true);
    hk::DrainStats(st);
    const hk::WrapStats base = st.wrap;
    hk::detail::CallWrapForTest(0, nullptr, root1, 3, 0);
    hk::detail::CallWrapForTest(0, nullptr, root0, 3, 0);
    hk::detail::CallWrapForTest(3, nullptr, root1, 7, 0);
    hk::detail::CallWrapForTest(1, nullptr, reinterpret_cast<void*>(0x6000), 7, 0);
    hk::DrainStats(st);
    Check(st.wrap.calls1[0] == base.calls1[0] + 1 && st.wrap.calls0[0] == base.calls0[0] + 1 && st.wrap.calls1[3] == base.calls1[3] + 1 &&
          st.wrap.other_calls == base.other_calls + 1 && st.wrap.same_thread[0] == base.same_thread[0] + 1,
          "PublishWrapRoots: census counts without an enabled Arm");
    Check(g_wrap.svc28 == 2 && g_wrap.walk7 == 1 && g_wrap.svc30 == 1, "every original still called once");
    // Legacy window Arm: walk7 on the hook thread would window; while a batch
    // is in flight it must not (wrap_in_batch).
    pi::Arm arm{};
    arm.enabled = true;
    arm.wrap = mgscoop::PartnerWrap::window;
    arm.root0 = 0x4000;
    arm.root1 = 0x5000;
    hk::PublishArm(arm);
    Image img;
    Setup(img);
    g_sc.wrap_probe = true;
    hk::DrainStats(st);
    const std::uint32_t in_batch = st.wrap.wrap_in_batch;
    const int walk7 = g_wrap.walk7;
    RunBatch(0);
    TakeCalls();
    hk::DrainStats(st);
    Check(st.wrap.wrap_in_batch == in_batch + 1 && g_wrap.walk7 == walk7 + 1, "no window while a hook-thread batch is in flight");
    hk::detail::CallWrapForTest(3, nullptr, root1, 7, 0);
    hk::DrainStats(st);
    Check(st.wrap.wrap_in_batch == in_batch + 1 && g_wrap.walk7 == walk7 + 2, "outside the batch the walk proceeds (no frame: forwarded)");
    // Native Arm: svc entries never window (block 1's job runs only in drains).
    arm.native = true;
    hk::PublishArm(arm);
    hk::DrainStats(st);
    const std::uint32_t skips = st.wrap.svc_native_skips;
    hk::detail::CallWrapForTest(0, nullptr, root1, 2, 0);
    hk::detail::CallWrapForTest(1, nullptr, root1, 2, 0);
    hk::detail::CallWrapForTest(2, nullptr, root1, 2, 1);
    hk::DrainStats(st);
    Check(st.wrap.svc_native_skips == skips + 3 && st.wrap.windows[0] == base.windows[0], "native: svc28/30/40 never open a window");
    // Run 27c64c8d (latch wrap_in_batch=7 at re-entry): in the frame between
    // the native Arm and the deferral, block 1's job ran inline in a batch on
    // the hook thread and each of its 7 svc28 calls counted as an in-batch
    // refusal (a hard leave). An svc entry never windows under a native Arm,
    // so nothing is swapped: it is a native skip, never wrap_in_batch.
    {
        pd::SetArmed(false);
        g_sc = Scenario{};
        g_sc.svc_probe = true;
        hk::DrainStats(st);
        const std::uint32_t in_batch0 = st.wrap.wrap_in_batch, skips0 = st.wrap.svc_native_skips;
        const int svc28_0 = g_wrap.svc28;
        RunBatch(0);
        TakeCalls();
        hk::DrainStats(st);
        Check(st.wrap.wrap_in_batch == in_batch0 && st.wrap.svc_native_skips == skips0 + 1 && g_wrap.svc28 == svc28_0 + 1,
              "native svc28 of block 1 inline in a batch: a native skip, not wrap_in_batch");
        // A walk inline in a batch would window: still refused and counted.
        g_sc = Scenario{};
        g_sc.wrap_probe = true;
        const int walk7_0 = g_wrap.walk7;
        RunBatch(0);
        TakeCalls();
        hk::DrainStats(st);
        Check(st.wrap.wrap_in_batch == in_batch0 + 1 && g_wrap.walk7 == walk7_0 + 1, "native walk7 inline in a batch: wrap_in_batch");
        g_sc = Scenario{};
        pd::SetArmed(true);
    }
    // The look write after svc28(root1, phase 0) nested in a drain.
    alignas(16) unsigned char ps[0x400]{};
    alignas(16) std::uint32_t rec[16]{};
    alignas(16) float look[4] = {9.0f, 9.0f, 9.0f, 9.0f};
    const std::uintptr_t vt = c::ps_vtable_alt_rva;  // the hook's image base is 0 in the fixture
    Put<std::uintptr_t>(Addr(ps), vt);
    Put<std::uintptr_t>(Addr(ps) + c::ps_phase_comp, vt + c::ps_phase_comp_vt_delta);
    Put<std::uint32_t>(Addr(ps) + c::ps_base_id, 1);
    Put<std::uintptr_t>(Addr(ps) + c::ps_rec, Addr(rec));
    Put<std::uintptr_t>(Addr(ps) + c::ps_look, Addr(look));
    pi::Arm da = arm;
    da.naim = mgscoop::PartnerNativeAim::look;
    da.ps1 = Addr(ps);
    da.ps1_idx = 0;
    pi::Injection inj{};
    inj.write = true;
    inj.look_valid = true;
    inj.look_pitch = 0.5f;
    inj.look_yaw = -2.0f;
    hk::detail::SetDrainForTest(da, inj, true);
    const int svc28 = g_wrap.svc28;
    hk::detail::CallWrapForTest(0, nullptr, root1, 1, 0);
    Check(look[0] == 9.0f && g_wrap.svc28 == svc28 + 1, "phase 1: no look write");
    hk::detail::CallWrapForTest(0, nullptr, root0, 0, 0);
    Check(look[0] == 9.0f, "root0: no look write");
    hk::detail::CallWrapForTest(0, nullptr, root1, 0, 0);
    hk::DrainStats(st);
    Check(look[0] == 0.5f && look[1] == -2.0f && look[2] == 9.0f && st.native.look_writes == 1 && g_wrap.last_phase == 0,
          "after svc28(root1, phase 0) in a drain: the remote look in instance 1's slot");
    look[0] = look[1] = 9.0f;
    rec[0] = 0x2;
    hk::detail::CallWrapForTest(0, nullptr, root1, 0, 0);
    hk::DrainStats(st);
    Check(look[0] == 9.0f && st.native.look_skipped_rec == 1, "record bit 1 set: the game skipped its own computation, so do we");
    rec[0] = 0;
    inj.look_valid = false;
    inj.stale = false;
    hk::detail::SetDrainForTest(da, inj, true);
    hk::detail::CallWrapForTest(0, nullptr, root1, 0, 0);
    hk::DrainStats(st);
    Check(look[0] == 9.0f && st.native.look_last == hk::LookRefusal::no_v3, "no v3 look from the peer: refused (no_v3)");
    inj.look_valid = true;
    Put<std::uintptr_t>(Addr(ps) + c::ps_phase_comp, vt);
    hk::detail::SetDrainForTest(da, inj, true);
    hk::detail::CallWrapForTest(0, nullptr, root1, 0, 0);
    hk::DrainStats(st);
    Check(look[0] == 9.0f && st.native.look_last == hk::LookRefusal::ps, "a wrong PS identity refuses the write");
    Put<std::uintptr_t>(Addr(ps) + c::ps_phase_comp, vt + c::ps_phase_comp_vt_delta);
    hk::detail::SetDrainForTest(da, inj, false);
    hk::detail::CallWrapForTest(0, nullptr, root1, 0, 0);
    Check(look[0] == 9.0f, "outside a drain: no look write");
    da.naim = mgscoop::PartnerNativeAim::off;
    hk::detail::SetDrainForTest(da, inj, true);
    hk::detail::CallWrapForTest(0, nullptr, root1, 0, 0);
    Check(look[0] == 9.0f, "naim=off: no look write");
    // R1: instance 1's gun record right after svc28(root1, phase 1) in a drain:
    // the reload request bit 20, the trigger's bit 24 and its count exist only
    // there (phase 6 clears them before the job returns).
    {
        alignas(16) static unsigned char p1_prim[0x100], p1_rec[c::gun_rec_size];
        static std::uintptr_t s_p1_rec = 0;
        s_p1_rec = Addr(p1_rec);
        struct P1 {
            static void Svc(void*, void*, std::uint32_t phase) noexcept {
                ++g_wrap.svc28;
                g_wrap.last_phase = phase;
                if (phase != 1) return;
                Put<std::uint32_t>(s_p1_rec + c::gun_flags, c::gun3c0_reload | c::gun3c0_shot);
                Put<std::uint8_t>(s_p1_rec + c::gun_shots, 2);
                Put<std::uint8_t>(s_p1_rec + c::gun_mode_cur, c::gun_mode_reload);
            }
        };
        hk::detail::BindWrapForTest(&P1::Svc, &FakeSvc30, &FakeSvc40, &FakeWalk7, &FakeWalk11, g_main_tid);
        Put<std::uintptr_t>(Addr(p1_prim) + c::prim_gun_rec, Addr(p1_rec));
        Put<std::uint8_t>(Addr(p1_rec) + c::gun_mode_cur, c::gun_mode_aim);
        pi::Arm pa = da;
        pa.gun_prim = Addr(p1_prim);
        pa.gun_rec = Addr(p1_rec);
        hk::detail::SetDrainForTest(pa, inj, true);
        hk::DrainStats(st);
        const hk::NativeStats b = st.native;
        hk::detail::CallWrapForTest(0, nullptr, root1, 1, 0);
        hk::DrainStats(st);
        Check(st.native.p1_b20 == b.p1_b20 + 1 && st.native.p1_b24 == b.p1_b24 + 1 && st.native.p1_shots == b.p1_shots + 2 &&
                  st.native.p1_mode_changes == b.p1_mode_changes + 1 && st.native.p1_mode == c::gun_mode_reload && g_wrap.last_phase == 1,
              "R1: after phase 1 in a drain: bit 20, bit 24, the shot count and the mode change seen");
        Put<std::uint32_t>(Addr(p1_rec) + c::gun_flags, 0);
        hk::detail::CallWrapForTest(0, nullptr, root1, 2, 0);
        hk::detail::SetDrainForTest(pa, inj, false);
        hk::detail::CallWrapForTest(0, nullptr, root1, 1, 0);
        hk::DrainStats(st);
        Check(st.native.p1_b20 == b.p1_b20 + 1 && st.native.p1_mode_changes == b.p1_mode_changes + 1,
              "R1: no probe after another phase or outside a drain");
        Put<std::uintptr_t>(Addr(p1_prim) + c::prim_gun_rec, Addr(p1_rec) + 0x10);
        hk::detail::SetDrainForTest(pa, inj, true);
        hk::detail::CallWrapForTest(0, nullptr, root1, 1, 0);
        hk::DrainStats(st);
        Check(st.native.p1_b20 == b.p1_b20 + 1, "R1: a changed prim chain is never read");
        hk::detail::SetDrainForTest(pa, inj, false);
        hk::detail::BindWrapForTest(&FakeSvc28, &FakeSvc30, &FakeSvc40, &FakeWalk7, &FakeWalk11, g_main_tid);
    }
    hk::detail::SetDrainForTest(da, inj, false);
    hk::PublishArm(pi::Arm{});
    hk::PublishWrapRoots(0, 0, false);
}

// Review fixes: batches are counted (nesting, other threads), a nested batch
// never defers or drains, and a batch that unwinds drops its record.
void TestNestingForeignUnwind() {
    {
        // A hook-thread batch nested in another (a job run inline during the
        // join ticks again): the outer record survives the inner batch and is
        // drained once by the outer; the inner never defers.
        Image img;
        Setup(img);
        g_sc.nest = true;
        g_sc.nest_mode = 1;
        RunBatch(0);
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(g_inner_in_batch && st.nested_batches == 1 && st.deferred == 1 && st.drains[0] == 1 && st.drains[1] == 0 &&
              st.pass_reason[static_cast<unsigned>(pd::detail::Pass::nested)] >= 1, "nested: no deferral inside, the outer drains once");
        int main_b1 = 0, worker_b1 = 0;
        for (const auto& k : calls) if (k.block == 1) (k.tid == g_main_tid ? main_b1 : worker_b1) += 1;
        Check(Count(calls, 1) == 2 && main_b1 == 1 && worker_b1 == 1 && !pd::InBatch(),
              "nested: the inner block 1 ran in place, the outer one after the outer join");
    }
    {
        // A batch on another thread in flight: InBatch, nothing deferred meanwhile.
        Image img;
        Setup(img);
        g_sc.foreign_hold = true;
        g_foreign_entered = false;
        g_foreign_release = false;
        std::thread other([] { RunBatch(0); });
        while (!g_foreign_entered) Sleep(1);
        Check(pd::InBatch(), "a batch on another thread is in flight");
        RunBatch(1);
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(st.deferred == 0 && st.foreign_batches == 1 && Find(calls, 1) && Find(calls, 1)->tid != g_main_tid &&
              st.pass_reason[static_cast<unsigned>(pd::detail::Pass::foreign)] >= 1, "foreign batch in flight: no deferral");
        g_foreign_release = true;
        other.join();
        Check(!pd::InBatch(), "foreign batch done");
    }
    {
        // An exception through the join: the published record is dropped (never
        // drained in a later frame); the next batch runs block 1 exactly once.
        Image img;
        Setup(img);
        g_sc.raise_after_join = true;
        Check(RunBatchCatching(0), "the exception reaches the outer handler");
        g_sc.raise_after_join = false;
        auto calls = TakeCalls();
        pd::Stats st{};
        pd::DrainStats(st);
        Check(Count(calls, 1) == 0 && st.unwound == 1 && st.torn == 1 && st.faulted &&
              st.fault_phase == static_cast<std::uint32_t>(pd::FaultPhase::torn) && !pd::InBatch(),
              "unwound batch: the record is dropped, counted torn, the deferral faults");
        pd::MissionStart();
        pd::SetArmed(true);
        RunBatch(0);
        calls = TakeCalls();
        pd::DrainStats(st);
        Check(Count(calls, 1) == 1 && st.drains[0] == 1 && Find(calls, 1)->tid == g_main_tid, "no stale record drained afterwards");
    }
}

// --- The drain window with the real input hook ----------------------------------
// The hook installs once per process, so this runs last on an image that is
// never freed; partner_defer is installed on the same image (H2 + H1), so the
// job body's call reaches W50 through the patched GO vtable and the drain runs
// the job inside RunJobWindow. A fake world satisfies Validate (P, PP, src,
// CAM, ids/pools/parts, blocks, plugin, records) with host sentinels.
pi::Sample g_hook_sample{};
bool FakeSample(pi::Sample& out) noexcept { out = g_hook_sample; return true; }
std::atomic<int> g_dispatch_calls{0};
std::uint32_t g_dispatch_held = 0;
void FakeDispatch(void*, std::uint32_t) noexcept {
    ++g_dispatch_calls;
    g_dispatch_held = g_watch_src ? Get<std::uint32_t>(g_watch_src + 0x48) : 0;
}
struct PadWorld {
    alignas(16) unsigned char go[0x1000];  // P = go + 0x20
    alignas(16) unsigned char flags[0x40];
    alignas(16) unsigned char q[0x40];
    alignas(16) unsigned char pp[0x600];
    alignas(16) unsigned char src[0x100];
    alignas(16) unsigned char cam[0x1000];
    alignas(16) unsigned char ids[16], pool[16], parts[16];
    alignas(16) unsigned char blocks[2 * c::block_stride];
    alignas(16) unsigned char plugin[0x200];
    alignas(16) unsigned char node[0x40];
    alignas(16) unsigned char recs[2 * c::plugin_rec_stride];
    alignas(16) unsigned char root0[0x40], root1[0x40];
    // The magazine store (F2): X = *(P+0xC8), Y = *(X+8) with the store iface vtable.
    alignas(16) unsigned char store_x[0x20], store_y[0x80];
    alignas(16) std::uint32_t store_types[8];
    alignas(16) std::uint16_t store_rows[8 * 3];
    // The weapon switch (W4): a Q with the equipment rows (Q+0xC0) and the
    // dirty mask (Q+0x17C), the rows of ids 0/1, an inventory of stride 7.
    alignas(16) unsigned char q2[0x200];
    alignas(16) unsigned char equip_rows[0x80];  // 2 rows of 0x3A
    alignas(16) std::uint32_t inv_handles[16];
    alignas(16) std::uint16_t inv_rows[16 * 3];
};
// The store row of instance 1's slot 0 (stride 2: idx 2).
std::uint16_t StoreWord(const PadWorld* w, unsigned word) { return w->store_rows[2 * 3 + word]; }
inline constexpr std::uint32_t kHostHeld = 0x11111111u;  // the local player's held word in src+0x48
inline constexpr std::uint32_t kRemoteHeld = 0x48u;      // HOLD + RELOAD from the remote
constexpr std::uint32_t partner_native_fire_bit() { return mgscoop::partner_native::kPadFire; }

// The job of the rates checks: the dash rate instance 1's job reads.
std::uintptr_t g_rates_probe_rec = 0;
float g_rates_probe_dash = 0.0f;
void RatesProbeJob(void*, const void*) noexcept { g_rates_probe_dash = Get<float>(g_rates_probe_rec + 0x48); }

void TestJobWindow() {
    static Image* img = new Image();  // never freed: the input hook keeps its base
    static PadWorld* w = new PadWorld();
    std::memset(w, 0, sizeof(*w));
    const std::uintptr_t base = img->base;
    const std::uintptr_t P = Addr(w->go) + c::go_to_p;
    Put<std::uintptr_t>(Addr(w->go), base + c::go_vtable_rva);
    Put<std::uintptr_t>(Addr(w->go) + c::go_flags_ptr, Addr(w->flags));
    Put<std::uint32_t>(Addr(w->flags) + c::go_flags, c::go_flags_bit);
    Put<std::uintptr_t>(base + c::player_slot_rva, P);
    const std::uintptr_t p_vt = base + c::p_vtable_rva, cam_vt = base + c::cam_vtable_rva, pp_vt = base + c::pp_vtable_rva;
    Put<std::uintptr_t>(P, p_vt);
    Put<std::uint32_t>(P + c::p_count, 2);
    Put<std::uintptr_t>(P + c::p_q, Addr(w->q));
    Put<std::uintptr_t>(Addr(w->q), base + c::q_vtable_rva);
    Put<std::uintptr_t>(P + c::p_pad, Addr(w->pp));
    Put<std::uintptr_t>(Addr(w->pp), pp_vt);
    Put<std::uintptr_t>(Addr(w->pp) + c::pp_src, Addr(w->src));
    Put<std::uintptr_t>(P + c::p_cam, Addr(w->cam));
    Put<std::uintptr_t>(Addr(w->cam), cam_vt);
    Put<std::uintptr_t>(P + c::p_ids, Addr(w->ids));
    Put<std::uint16_t>(Addr(w->ids) + 2, 1);
    Put<std::uintptr_t>(P + c::p_pool, Addr(w->pool));
    w->pool[0] = 0;
    w->pool[1] = 1;
    Put<std::uintptr_t>(P + c::p_parts, Addr(w->parts));
    w->parts[1] = c::parts_loaded_state;
    Put<std::uintptr_t>(P + c::p_blocks, Addr(w->blocks));
    const std::uintptr_t blk1 = Addr(w->blocks) + c::block_stride;
    Put<std::uintptr_t>(Addr(w->blocks) + c::blk_root, Addr(w->root0));
    Put<std::uintptr_t>(blk1 + c::blk_root, Addr(w->root1));
    const std::uintptr_t plugin_vt[4] = {base + c::plugin_vt0_rva, base + c::plugin_vt1_rva, base + c::plugin_vt2_rva, base + c::plugin_vt3_rva};
    static constexpr std::size_t off[4] = {0x0, 0x10, 0x30, 0x48};
    for (unsigned i = 0; i < 4; ++i) Put<std::uintptr_t>(Addr(w->plugin) + off[i], plugin_vt[i]);
    Put<std::uintptr_t>(Addr(w->plugin) + c::plugin_node, Addr(w->node));
    Put<std::uint32_t>(Addr(w->node) + c::node_base, 0);
    Put<std::uintptr_t>(Addr(w->plugin) + c::plugin_recs, Addr(w->recs));
    const std::uintptr_t rec1 = Addr(w->recs) + c::plugin_rec_stride;
    // Host sentinels: the local player's pad and the HOLD latch.
    Put<std::uint32_t>(Addr(w->src) + 0x48, kHostHeld);
    Put<std::uint32_t>(Addr(w->pp) + c::pp_hold_latch, 0xA0u);
    Put<std::uint32_t>(Addr(w->pp) + c::pp_masks + 4, 0x100u);  // the host's stick mask PP+0x2C
    // Instance 1's base action record as run 27c64c8d's census showed it
    // (rec1_2ac=0x4: not local), with an act and a sampled move intent.
    Put<std::uint32_t>(rec1 + c::rec_local, 0x4u);
    Put<std::uint8_t>(rec1 + c::rec_act, 2);
    Put<std::uint8_t>(rec1 + c::rec_act_next, 3);
    Put<float>(rec1 + c::rec_move_mag, 0.75f);
    Put<float>(rec1 + c::rec_move_heading, 1.5f);
    // Install the input hook and the deferral on the same image.
    g_log.clear();
    Check(hk::Install(reinterpret_cast<void*>(base), &Log, &FakeSample) == nullptr && hk::Installed(), "input hook on the image");
    hk::detail::BindDispatchForTest(&FakeDispatch);
    pd::detail::ResetForTest();
    Check(pd::Install(reinterpret_cast<void*>(base), pd::Mode::on, &Log) == nullptr && pd::H1Installed(), "deferral on the same image");
    namespace p1 = mgscoop::partner_local_detour;
    Check(p1::Install(reinterpret_cast<void*>(base), &Log) == nullptr && p1::Installed() && p1::PatchLive(reinterpret_cast<void*>(base)) &&
          p1::detail::ReadForceForTest().id == -1, "phase-1 detour on the same image (contract bytes), not forced");
    pd::detail::Bindings b{};
    b.batch = &FakeBatch; b.job = &FakeJob; b.scope_ctor = &FakeScopeCtor; b.scope_dtor = &FakeScopeDtor;
    b.scope_read = &FakeScopeRead; b.job_return = g_thunk_return; b.hook_thread = &MainTid;
    pd::detail::BindForTest(b);
    g_world.Reset(*img);
    Put<std::uintptr_t>(Addr(g_world.entry) + c::entry_go, Addr(w->go));
    g_job_go = w->go;
    g_sc = Scenario{};
    TakeCalls();
    g_watch_src = Addr(w->src);
    g_watch_pp = Addr(w->pp);
    g_watch_rec = rec1;
    pd::PublishIdentity(pd::Identity{Addr(w->go), 0, 1});
    pd::SetArmed(true);
    // The native Arm (as the driver builds it).
    pi::Arm a{};
    a.gen = 1;
    a.enabled = true;
    a.native = true;
    a.cam = mgscoop::PartnerInputCam::sandbox;
    a.hold = mgscoop::PartnerInputHold::own;
    a.wrap = mgscoop::PartnerWrap::window;
    a.P = P; a.pp = Addr(w->pp); a.src = Addr(w->src); a.cam_object = Addr(w->cam);
    a.root0 = Addr(w->root0); a.root1 = Addr(w->root1); a.blk1 = blk1; a.rec1 = rec1; a.plugin1 = Addr(w->plugin);
    a.p_vtable = p_vt; a.pp_vtable = pp_vt; a.cam_vtable = cam_vt;
    for (unsigned i = 0; i < 4; ++i) a.plugin_vt[i] = plugin_vt[i];
    a.pool1 = 1;
    a.config.mode = mgscoop::PartnerInput::net;
    a.config.mask = 0xFFFFFFFFu;
    a.config.words[0] = pi::Word::held; a.config.words[1] = pi::Word::press; a.config.words[2] = pi::Word::release;
    a.config.stale_ms = 1000;
    a.go = Addr(w->go);
    a.mag_scale = 1.0;
    hk::PublishArm(a);
    g_hook_sample = pi::Sample{};
    g_hook_sample.valid = true;
    g_hook_sample.age_ms = 5;
    g_hook_sample.packet.version = 3;
    g_hook_sample.packet.held = kRemoteHeld;
    // The main window (phase 8 for root1) composes the frame injection.
    const auto dispatch = reinterpret_cast<DispatchEntry>(hk::detail::DispatchStubForTest());
    Check(dispatch != nullptr, "dispatch stub");
    dispatch(w->root1, 8);
    hk::Stats hs{};
    hk::DrainStats(hs);
    Check(g_dispatch_calls == 1 && g_dispatch_held == kRemoteHeld && hs.window.windows == 1 && hs.window.faults == 0 &&
          hs.native.frames == 1, "main window: instance 1's update saw the remote's held word");
    Check(Get<std::uint32_t>(Addr(w->src) + 0x48) == kHostHeld && Get<std::uintptr_t>(Addr(w->pp)) == pp_vt &&
          Get<std::uint32_t>(Addr(w->pp) + c::pp_hold_latch) == 0xA0u, "after the main window: the host's pad is back");
    // Block 0's update (root0) is never windowed.
    dispatch(w->root0, 8);
    Check(g_dispatch_calls == 2 && g_dispatch_held == kHostHeld, "root0: the local player's pad, untouched");
    // The deferred job end to end: block 0 on its worker with the host's pad,
    // block 1 after the join inside the drain window with the remote's.
    RunBatch(0);
    auto calls = TakeCalls();
    const JobCall* b0 = Find(calls, 0);
    const JobCall* b1 = Find(calls, 1);
    pd::Stats st{};
    pd::DrainStats(st);
    hk::DrainStats(hs);
    Check(b0 && b1 && b0->tid != g_main_tid && b0->held == kHostHeld && (b0->latch & 1u) == 0 && b0->pp_vt == pp_vt,
          "instance 0's job never sees the remote's pad");
    Check(b1->tid == g_main_tid && b1->held == kRemoteHeld && (b1->latch & 1u) == 1u && (b1->latch & ~1u) == 0xA0u &&
          b1->pp_vt != pp_vt && !b1->in_batch, "instance 1's deferred job ran in the drain window (remote held, HOLD latch, vtable copy)");
    Check(st.windowed == 1 && st.unwindowed == 0 && hs.native.drain_windows == 1 && hs.native.drain_refused == 0,
          "one windowed drain");
    Check(Get<std::uint32_t>(Addr(w->src) + 0x48) == kHostHeld && Get<std::uintptr_t>(Addr(w->pp)) == pp_vt &&
          Get<std::uint32_t>(Addr(w->pp) + c::pp_hold_latch) == 0xA0u, "after the drain: the host's pad and latch are back");
    // Run 27c64c8d (no movement): the drain must make instance 1 local for its
    // update (isLocal, whatever partner_input_local says) and force phase 1's
    // pad-sampling branch for exactly (P, id 1, the hook thread); block 0's
    // job never sees either.
    Check((b1->rec_local & 1u) == 1u && (b1->rec_local & ~1u) == 0x4u && (b0->rec_local & 1u) == 0u,
          "drain: isLocal set for instance 1's job (local=main), never for instance 0's");
    Check(b1->p1.id == 1 && b1->p1.p == P && b1->p1.tid == g_main_tid && b0->p1.id == -1,
          "drain: the phase-1 detour forced for (P, id 1, hook thread) during block 1's job only");
    Check(Get<std::uint32_t>(rec1 + c::rec_local) == 0x4u && p1::detail::ReadForceForTest().id == -1,
          "after the drain: isLocal restored, the detour released");
    Check(hs.native.p1_installed && hs.native.p1_windows == 1 && hs.native.drain_held == kRemoteHeld && hs.native.drain_view == 0 &&
          hs.native.drain_local_pre == 0x4u && hs.native.rec_ok && hs.native.rec_act == 2 && hs.native.rec_next == 3 &&
          hs.native.rec_mag == 750 && hs.native.rec_heading_mrad == 1500,
          "drain telemetry: detour windows, the injection, isLocal before, instance 1's record after its update");
    // A job whose block is not instance 1's never gets a window, even with a
    // valid Arm: instance 0's block (pool 0), or anything else.
    {
        g_probe_ran = 0;
        g_watch_src = Addr(w->src);
        pd::JobParam param{0, 0, 0, 8, g_world.bits};
        const bool windowed = hk::RunJobWindow(w->go, &param, 0, &ProbeJob, pi::Consumer::drain0);
        hk::DrainStats(hs);
        Check(!windowed && g_probe_ran == 1 && hs.native.drain_block_refused == 1, "block 0: refused, run once without a window");
        param.block = 2;
        Check(!hk::RunJobWindow(w->go, &param, 2, &ProbeJob, pi::Consumer::drain0) && g_probe_ran == 2, "block 2: refused");
        w->pool[0] = 1;  // the live pools collide: refused even for the Arm's pool1
        param.block = 1;
        Check(!hk::RunJobWindow(w->go, &param, 1, &ProbeJob, pi::Consumer::drain0) && g_probe_ran == 3, "pool0 == block: refused");
        w->pool[0] = 0;
        Check(hk::RunJobWindow(w->go, &param, 1, &ProbeJob, pi::Consumer::drain0) && g_probe_ran == 4, "instance 1's block: windowed");
    }
    // WS4 step 0 (lab key partner_placed_census, read-only): the stun op-0 pad
    // fields around body 1's job. Off: nothing is published. On: the viewer's
    // PP+0x10 / +0x2C come from the window's saved host copy, body 1's are what
    // its job saw, and the host's pad is byte-identical after the window.
    {
        namespace ppc = mgscoop::partner_placed_census;
        ppc::detail::ResetPadForTest();
        Put<float>(Addr(w->pp) + 0x10, 0.25f);  // the host's move magnitude
        pd::JobParam param{0, 0, 0, 8, g_world.bits};
        param.block = 1;
        ppc::PadSample pad{};
        Check(hk::RunJobWindow(w->go, &param, 1, &ProbeJob, pi::Consumer::drain0) && !ppc::ReadPad(pad),
              "pad census off: a windowed drain publishes nothing");
        static unsigned char pp_before[sizeof(w->pp)];
        std::memcpy(pp_before, w->pp, sizeof(pp_before));
        ppc::SetPadCensus(true);
        const bool windowed = hk::RunJobWindow(w->go, &param, 1, &ProbeJob, pi::Consumer::drain0);
        ppc::SetPadCensus(false);
        Check(windowed && ppc::ReadPad(pad) && pad.windows == 1 && pad.pre_ok && pad.post_ok, "pad census on: one sample per windowed drain");
        Check(pad.viewer_mag == 0.25f && pad.viewer_mask == 0x100u, "pad census: the viewer's PP+0x10 / +0x2C from the window's saved copy");
        Check(pad.body_mag_pre != 0.25f && pad.body_mag_pre == pad.body_mag_post && (pad.body_mask_pre & 0x100u) == 0 &&
              ppc::PadDiffers(pad), "pad census: body 1's job saw its own (shadowed) PP+0x10 / +0x2C");
        Check(pad.mode_pre == 0xFF && pad.mode_post == 0xFF, "pad census: no gun prim in the Arm, the modes stay unread");
        Check(std::memcmp(pp_before, w->pp, sizeof(pp_before)) == 0, "pad census: the host's pad is byte-identical after the window");
        Check(Get<float>(Addr(w->pp) + 0x10) == 0.25f && Get<std::uint32_t>(Addr(w->pp) + c::pp_masks + 4) == 0x100u,
              "pad census: the host's PP+0x10 / +0x2C are back");
        Check(hk::RunJobWindow(w->go, &param, 1, &ProbeJob, pi::Consumer::drain0) && ppc::ReadPad(pad) && pad.windows == 1,
              "pad census off again: no new sample");
    }
    // WS4 v1 (docs/anim-round2-throws.md "WS4 v1"): the arm lane's read-only watchdog in the real drain window.
    // With partner_native_arm (and native_support) in the Arm, body 1's gun record in the arm's aim pose never
    // trips; in an arm use mode (52..56; FIRE is always stripped, so it must never happen) the job latches arms off
    // for the mission, and only arms (the support latch is untouched). The job itself still runs once.
    {
        alignas(16) static unsigned char arm_rec[c::gun_rec_size];
        std::memset(arm_rec, 0, sizeof(arm_rec));
        pi::Arm armed = a;
        armed.native_support = armed.native_arm = true;
        armed.gun_rec = Addr(arm_rec);  // the watchdog reads only rec+0x2C0 (and rec+0x24C for its line)
        hk::PublishArm(armed);
        pd::JobParam param{0, 0, 0, 8, g_world.bits};
        param.block = 1;
        Check(!hk::NativeArmLatchedOff(), "arm watchdog: no latch before any arm job");
        for (const std::uint8_t mode : {std::uint8_t(0), std::uint8_t(1), std::uint8_t(2), std::uint8_t(3), std::uint8_t(51), std::uint8_t(57)}) {
            arm_rec[c::gun_mode_cur] = mode;
            Check(hk::RunJobWindow(w->go, &param, 1, &ProbeJob, pi::Consumer::drain0) && !hk::NativeArmLatchedOff(),
                  "arm watchdog: the aim pose and other modes never trip");
        }
        arm_rec[c::gun_mode_cur] = 55;  // Jehuty's use mode
        const int ran = g_probe_ran;
        Check(hk::RunJobWindow(w->go, &param, 1, &ProbeJob, pi::Consumer::drain0) && g_probe_ran == ran + 1 && hk::NativeArmLatchedOff() &&
                  !hk::NativeSupportLatchedOff(),
              "arm watchdog: a use mode latches arms off for the mission (support untouched)");
        arm_rec[c::gun_mode_cur] = 1;
        Check(hk::RunJobWindow(w->go, &param, 1, &ProbeJob, pi::Consumer::drain0) && hk::NativeArmLatchedOff(),
              "arm watchdog: the latch stays for the mission");
        hk::PublishArm(a);
        Check(hk::RunJobWindow(w->go, &param, 1, &ProbeJob, pi::Consumer::drain0), "arm watchdog: the old Arm runs as before");
    }
    // WS4 v2 (docs/anim-round2-throws.md "WS4 v2"): the place / detonate mirror in the real drain window. This image
    // admits no support lane (no guard lease, filter or writer), so the remote's 46 edge (a v7 frame with the place
    // word) arms an episode and logs it, but body 1's rec+0x3CC is never written (lane refusal) and the job runs once.
    {
        alignas(16) static unsigned char c4_rec[c::gun_rec_size];
        std::memset(c4_rec, 0, sizeof(c4_rec));
        c4_rec[c::gun_mode_cur] = 44;
        Put<std::uint16_t>(Addr(c4_rec) + c::gun_equip, 395);
        Put<std::uint32_t>(Addr(c4_rec) + c::gun_flags27c, 3u << 6);
        Put<std::uint32_t>(Addr(c4_rec) + c::gun_surface, 0x1000060u);
        pi::Arm armed = a;
        armed.native_support = armed.native_placed = armed.native_place_mirror = true;
        armed.gun_rec = Addr(c4_rec);
        hk::PublishArm(armed);
        const pi::Sample saved = g_hook_sample;
        g_hook_sample.packet.version = 7;
        g_hook_sample.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::InHand | pi::GunFlag::Place;
        g_hook_sample.packet.gun_mode = 46;
        g_hook_sample.packet.place_bits = 0x14;
        g_hook_sample.packet.inv_slot = 3;
        dispatch(w->root1, 8);  // a new frame with the gun group and the place word
        pd::JobParam param{0, 0, 0, 8, g_world.bits};
        param.block = 1;
        const int ran = g_probe_ran;
        Check(hk::RunJobWindow(w->go, &param, 1, &ProbeJob, pi::Consumer::drain0) && g_probe_ran == ran + 1,
              "place mirror: the job runs once in its window");
        Check(Logged("partner_place_mirror=edge; act=place; remote_mode=46; surface=1; default=0; place_bits=0x14; v7=1"),
              "place mirror: the remote's 46 edge is logged with the v7 class");
        Check(Get<std::uint32_t>(Addr(c4_rec) + c::gun_surface) == 0x1000060u && !Logged("partner_place_mirror=write"),
              "place mirror: no admitted C4 lane, body 1's word is never written");
        armed.native_place_mirror = false;
        hk::PublishArm(armed);
        Check(hk::RunJobWindow(w->go, &param, 1, &ProbeJob, pi::Consumer::drain0) &&
                  Get<std::uint32_t>(Addr(c4_rec) + c::gun_surface) == 0x1000060u && LogCount("partner_place_mirror=edge") == 1,
              "place mirror: the key off does nothing");
        g_hook_sample = saved;
        hk::PublishArm(a);
        dispatch(w->root1, 8);
    }
    // A refused nested job from the pre-dispatch HUD probe keeps its own
    // callback; it must not replace the outer drain's pending original.
    {
        static void* s_go = nullptr;
        static const void* s_outer_param = nullptr;
        static unsigned s_before = 0, s_outer = 0, s_nested = 0, s_order = 0;
        static bool s_nested_windowed = true, s_outer_args = false, s_nested_args = false;
        static std::uint32_t s_outer_held = 0;
        s_go = w->go; s_before = s_outer = s_nested = s_order = 0;
        s_nested_windowed = true; s_outer_args = s_nested_args = false; s_outer_held = 0;
        struct CallbackOwnership {
            static void Nested(void* go, const void* raw) noexcept {
                ++s_nested;
                const auto* p = static_cast<const pd::JobParam*>(raw);
                s_nested_args = go == s_go && p && p->block == 0;
                if (s_order == 1) s_order = 2;
            }
            static mgscoop::nametag_game::HudJobProbe Before(std::uintptr_t) noexcept {
                ++s_before; s_order = 1;
                pd::JobParam nested{0, 0, 0, 8, g_world.bits};
                s_nested_windowed = hk::RunJobWindow(s_go, &nested, 0, &Nested, pi::Consumer::drain0);
                return {};
            }
            static void Outer(void* go, const void* raw) noexcept {
                ++s_outer;
                s_outer_args = go == s_go && raw == s_outer_param;
                s_outer_held = Get<std::uint32_t>(g_watch_src + 0x48);
                if (s_order == 2) s_order = 3;
            }
        };
        dispatch(w->root1, 8);  // refresh the qualified outer injection
        pd::JobParam param{1, 0, 0, 8, g_world.bits};
        s_outer_param = &param;
        hk::RegisterHudProbe(&CallbackOwnership::Before, nullptr);
        const bool windowed = hk::RunJobWindow(w->go, &param, 1, &CallbackOwnership::Outer, pi::Consumer::drain0);
        hk::RegisterHudProbe(nullptr, nullptr);
        Check(windowed && s_before == 1 && !s_nested_windowed && s_nested == 1 && s_nested_args,
              "callback ownership: refused nested block0 executes only its own callback once");
        Check(s_outer == 1 && s_outer_args && s_order == 3 && s_outer_held == kRemoteHeld,
              "callback ownership: outer dispatch retains its callback, arguments and remote input");
        Check(Get<std::uint32_t>(Addr(w->src) + 0x48) == kHostHeld &&
                  Get<std::uintptr_t>(Addr(w->pp)) == pp_vt &&
                  Get<std::uint32_t>(rec1 + c::rec_local) == 0x4u &&
                  p1::detail::ReadForceForTest().id == -1,
              "callback ownership: nested refusal does not prevent outer cleanup");
        s_go = nullptr; s_outer_param = nullptr;
    }
    // Never a window (main or drain) while any batch is in flight.
    {
        g_sc.window_probe = true;
        g_sc.dispatch_probe = true;
        g_probe_root = w->root1;
        g_probe_ran = 0;
        hk::DrainStats(hs);
        const std::uint32_t in_batch = hs.wrap.wrap_in_batch, windows = hs.window.windows;
        const int dispatches = g_dispatch_calls;
        RunBatch(1);
        TakeCalls();
        hk::DrainStats(hs);
        Check(g_probe_ran == 1 && !g_probe_windowed && hs.native.drain_in_batch == 1, "RunJobWindow in a batch: run without a window");
        Check(g_dispatch_calls == dispatches + 1 && g_dispatch_held == kHostHeld && hs.window.windows == windows &&
              hs.wrap.wrap_in_batch == in_batch + 2, "main window in a batch: no window; both counted (wrap_in_batch)");
        g_sc = Scenario{};
    }
    // A sub-controller replaced inside the window is never read or restored.
    {
        alignas(16) static unsigned char sub_a[0x100], sub_b[0x100];
        std::memset(sub_a, 0x5A, sizeof(sub_a));
        std::memset(sub_b, 0x3C, sizeof(sub_b));
        Put<std::uintptr_t>(Addr(w->cam) + c::cam_aim_sub, Addr(sub_a));
        a.naim = mgscoop::PartnerNativeAim::look;
        a.ps1 = 0x1;  // not used by the window itself
        hk::PublishArm(a);
        dispatch(w->root1, 8);  // a new frame for the Arm's gen
        struct Swap {
            static void Job(void* go, const void*) noexcept {
                const std::uintptr_t cam = Get<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(go) + c::go_to_p + c::p_cam);
                std::memset(sub_a + c::cam_sub_begin, 0, 4);  // the old object is changed, then replaced
                Put<std::uintptr_t>(cam + c::cam_aim_sub, Addr(sub_b));
            }
        };
        pd::JobParam param{1, 0, 0, 8, g_world.bits};
        hk::DrainStats(hs);
        const std::uint32_t dirty_before = hs.native.camsub_dirty_or;
        Check(hk::RunJobWindow(w->go, &param, 1, &Swap::Job, pi::Consumer::drain0), "aim window ran");
        hk::DrainStats(hs);
        Check((hs.native.camsub_dirty_or & 4u) && !(dirty_before & 4u) && sub_a[c::cam_sub_begin] == 0 && sub_b[c::cam_sub_begin] == 0x3C &&
              Get<std::uintptr_t>(Addr(w->cam) + c::cam_aim_sub) != Addr(sub_a),
              "replaced sub-controller: flagged (bit 2), neither object written back");
        Put<std::uintptr_t>(Addr(w->cam) + c::cam_aim_sub, 0);
    }
    // Instance 1's private stick mask (PP+0x2C bit 0 zeroes the sampled move
    // magnitude) is reported after the drain; the host's mask comes back.
    {
        struct Mask {
            static void Job(void* go, const void*) noexcept {
                const std::uintptr_t pp = Get<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(go) + c::go_to_p + c::p_pad);
                Put<std::uint32_t>(pp + c::pp_masks + 4, Get<std::uint32_t>(pp + c::pp_masks + 4) | 1u);
            }
        };
        dispatch(w->root1, 8);
        pd::JobParam param{1, 0, 0, 8, g_world.bits};
        Check(hk::RunJobWindow(w->go, &param, 1, &Mask::Job, pi::Consumer::drain0), "mask window ran");
        hk::DrainStats(hs);
        Check(hs.native.drain_mask2c == 1u && hs.native.drain_mask28 == 0u && Get<std::uint32_t>(Addr(w->pp) + c::pp_masks + 4) == 0x100u,
              "drain telemetry: instance 1's private stick mask; the host's PP+0x2C restored");
    }
    // Native rates (docs/native-puppet.md "Rates"): instance 1's rate record
    // gets the remote's rates before its job runs; a game rewrite is
    // reapplied; a disarmed Arm restores at the next main dispatch; a record
    // whose vtable changed is never written; forget drops without writing and
    // the forgotten epoch's Arm never applies again; a legacy (non-native)
    // Arm restores too; only instance 1's own record of its own block PS is
    // ever written (instance 0's record never changes).
    {
        alignas(16) static unsigned char sp_obj[0x40], sp_other[0x40], rate_recs[2 * c::rate_rec_stride], ps1_obj[0x500];
        std::memset(sp_obj, 0, sizeof(sp_obj));
        std::memset(sp_other, 0, sizeof(sp_other));
        std::memset(rate_recs, 0, sizeof(rate_recs));
        std::memset(ps1_obj, 0, sizeof(ps1_obj));
        const std::uintptr_t sp = Addr(sp_obj);
        Put<std::uintptr_t>(sp, base + c::rate_sp_vtable_rva);
        Put<std::uint32_t>(sp + c::sp_base_id, 0);
        Put<std::uint32_t>(sp + c::sp_count, 2);
        Put<std::uintptr_t>(sp + c::sp_records, Addr(rate_recs));
        std::memcpy(sp_other, sp_obj, sizeof(sp_other));  // a second rate service over the same records
        // Instance 1's block PS: blk1+0x78 -> PS1 (player PS vtable, PS1+0x408 == blk1), PS1+0x260 -> sp.
        const std::uintptr_t ps1 = Addr(ps1_obj);
        Put<std::uintptr_t>(ps1, base + c::ps_vtable_alt_rva);
        Put<std::uintptr_t>(ps1 + c::ps_blk, blk1);
        Put<std::uintptr_t>(ps1 + c::ps_rates, sp);
        Put<std::uintptr_t>(blk1 + c::blk_ps, ps1);
        const std::uintptr_t r0 = Addr(rate_recs);                        // id 0 (the local player)
        const std::uintptr_t rr = Addr(rate_recs) + c::rate_rec_stride;  // id 1
        Put<std::uintptr_t>(r0, base + c::rate_rec_vtable_rva);
        Put<std::uintptr_t>(rr, base + c::rate_rec_vtable_rva);
        const float game[6] = {1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f};  // crawl, crawl factor, crawl rate, dash, dash factor, dash rate
        const float own0[6] = {1.2f, 1.0f, 1.2f, 1.2f, 1.0f, 1.2f};   // instance 0's own record (sentinel)
        std::memcpy(reinterpret_cast<void*>(rr + c::rate_first), game, sizeof(game));
        std::memcpy(reinterpret_cast<void*>(r0 + c::rate_first), own0, sizeof(own0));
        const auto rate = [&](std::size_t off) { return Get<float>(rr + off); };
        const auto id0_untouched = [&]() { return std::memcmp(reinterpret_cast<const void*>(r0 + c::rate_first), own0, sizeof(own0)) == 0; };
        g_rates_probe_rec = rr;
        a.naim = mgscoop::PartnerNativeAim::off;
        a.ps1 = 0;
        a.rates_mirror = true;
        a.rates_sp = sp;
        a.rates_rec = rr;
        a.rates_id = 1;
        a.rates_epoch = 1;
        a.rates_remote[0] = 1200; a.rates_remote[1] = 1000; a.rates_remote[2] = 1200;
        hk::PublishArm(a);
        dispatch(w->root1, 8);
        pd::JobParam param{1, 0, 0, 8, g_world.bits};
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0), "rates: drain window ran");
        hk::DrainStats(hs);
        Check(std::fabs(g_rates_probe_dash - 1.2f) < 1e-6f && std::fabs(rate(0x48) - 1.2f) < 1e-6f && std::fabs(rate(0x50) - 0.6f) < 1e-6f &&
              rate(0x3C) == 1.0f && std::fabs(rate(0x40) - 1.2f) < 1e-6f && std::fabs(rate(0x44) - 1.2f) < 1e-6f && rate(0x4C) == 0.5f,
              "rates: the job already sees the remote's dash rate; +0x50/+0x44 by the game's formulas; +0x4C untouched");
        Check(hs.native.rates_applied && !hs.native.rates_latched && hs.native.rates_writes == 1 && hs.native.rates_written[0] == 1200 &&
              id0_untouched(), "rates: applied, one write, instance 0's record untouched");
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0), "rates: second drain");
        hk::DrainStats(hs);
        Check(hs.native.rates_writes == 1 && hs.native.rates_reapplied == 0, "rates: steady, no write");
        Put<float>(rr + 0x48, 1.1f);  // the instance setup rewrote the dash rate (provider)
        Put<float>(rr + 0x50, 0.55f);
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0), "rates: drain after a game rewrite");
        hk::DrainStats(hs);
        Check(hs.native.rates_reapplied == 1 && hs.native.rates_writes == 2 && std::fabs(rate(0x48) - 1.2f) < 1e-6f &&
              std::fabs(g_rates_probe_dash - 1.2f) < 1e-6f, "rates: a game rewrite is reapplied before the job");
        // Disarmed: the next main dispatch (no batch in flight) restores the game's values.
        hk::PublishArm(pi::Arm{});
        dispatch(w->root0, 8);
        hk::DrainStats(hs);
        Check(!hs.native.rates_applied && hs.native.rates_restores == 1 && std::fabs(rate(0x48) - 1.1f) < 1e-6f &&
              std::fabs(rate(0x50) - 0.55f) < 1e-6f && rate(0x40) == 1.0f && rate(0x44) == 1.0f && rate(0x3C) == 1.0f,
              "rates: disarmed -> restored at the main dispatch (the rewrite's value kept)");
        // A legacy (non-native) Arm after leaving the native drive: no drain
        // runs any more, so the main dispatch restores too.
        a.gen = 2;
        hk::PublishArm(a);
        dispatch(w->root1, 8);
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0) && std::fabs(rate(0x48) - 1.2f) < 1e-6f,
              "rates: applied before the legacy Arm");
        pi::Arm legacy = a;
        legacy.gen = 3;
        legacy.native = false;
        legacy.rates_mirror = false;
        hk::PublishArm(legacy);
        dispatch(w->root0, 8);
        hk::DrainStats(hs);
        Check(!hs.native.rates_applied && hs.native.rates_restores == 2 && std::fabs(rate(0x48) - 1.1f) < 1e-6f && id0_untouched(),
              "rates: an enabled legacy Arm restores at the main dispatch");
        // A record whose vtable changed (destroyed) is never written: latched.
        a.gen = 4;
        hk::PublishArm(a);
        dispatch(w->root1, 8);
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0), "rates: re-applied");
        hk::DrainStats(hs);
        Check(hs.native.rates_applied && std::fabs(rate(0x48) - 1.2f) < 1e-6f, "rates: applied again");
        Put<std::uintptr_t>(rr, base + 0x23AD260);  // the record's base vtable (construction/destruction)
        Put<float>(rr + 0x48, 7.0f);
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0), "rates: drain with a destroyed record");
        hk::DrainStats(hs);
        Check(hs.native.rates_latched && !hs.native.rates_applied && hs.native.rates_reason == 4u /* rec_vtable */ &&
              hs.native.rates_drops == 1 && rate(0x48) == 7.0f, "rates: a changed record vtable latches, nothing written, dropped");
        // Forget (MissionEnd of epoch 1): the epoch-1 Arm still published
        // (the driver's disarm not landed yet) never applies again ...
        hk::ForgetRates(1);
        Put<std::uintptr_t>(rr, base + c::rate_rec_vtable_rva);
        Put<float>(rr + 0x48, 1.0f);
        const unsigned writes = hs.native.rates_writes;
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0), "rates: drain with the forgotten epoch's Arm");
        hk::DrainStats(hs);
        Check(!hs.native.rates_applied && !hs.native.rates_latched && rate(0x48) == 1.0f && hs.native.rates_writes == writes,
              "rates: the forgotten epoch's Arm never writes");
        // ... and a new epoch applies again.
        a.rates_epoch = 2;
        a.gen = 5;
        hk::PublishArm(a);
        dispatch(w->root1, 8);
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0), "rates: drain in a new epoch");
        hk::DrainStats(hs);
        Check(hs.native.rates_applied && !hs.native.rates_latched && std::fabs(rate(0x48) - 1.2f) < 1e-6f, "rates: a new epoch applies again");
        // Forget while applied: dropped, never written back; the stale Arm stays refused.
        hk::ForgetRates(2);
        hk::PublishArm(pi::Arm{});
        dispatch(w->root0, 8);
        hk::DrainStats(hs);
        Check(!hs.native.rates_applied && hs.native.rates_drops == 2 && std::fabs(rate(0x48) - 1.2f) < 1e-6f,
              "rates: forget drops the state without writing");
        Put<float>(rr + 0x48, 1.0f);
        a.gen = 6;
        hk::PublishArm(a);
        dispatch(w->root1, 8);
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0), "rates: drain with the stale epoch-2 Arm");
        hk::DrainStats(hs);
        Check(!hs.native.rates_applied && rate(0x48) == 1.0f, "rates: a republished forgotten epoch never applies");
        // Instance 0's record (id 0) is never written, even when the Arm names it.
        a.rates_epoch = 3;
        a.rates_rec = r0;
        a.rates_id = 0;
        a.gen = 7;
        hk::PublishArm(a);
        dispatch(w->root1, 8);
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0), "rates: drain with an id-0 Arm");
        hk::DrainStats(hs);
        Check(hs.native.rates_latched && !hs.native.rates_applied && hs.native.rates_reason == 9u /* owner */ && id0_untouched(),
              "rates: an id other than 1 latches (owner), instance 0's record untouched");
        // A rate service that is not instance 1's block PS's is refused too.
        a.rates_epoch = 4;
        a.rates_rec = rr;
        a.rates_id = 1;
        a.rates_sp = Addr(sp_other);
        a.gen = 8;
        hk::PublishArm(a);
        dispatch(w->root1, 8);
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0), "rates: drain with a foreign rate service");
        hk::DrainStats(hs);
        Check(hs.native.rates_latched && hs.native.rates_reason == 9u && rate(0x48) == 1.0f, "rates: a foreign rate service latches (owner)");
        // A restore whose chain no longer holds (instance 1's pool changed) drops without writing.
        a.rates_epoch = 5;
        a.rates_sp = sp;
        a.gen = 9;
        hk::PublishArm(a);
        dispatch(w->root1, 8);
        Check(hk::RunJobWindow(w->go, &param, 1, &RatesProbeJob, pi::Consumer::drain0), "rates: applied in epoch 5");
        hk::DrainStats(hs);
        Check(hs.native.rates_applied && std::fabs(rate(0x48) - 1.2f) < 1e-6f, "rates: applied (epoch 5)");
        const unsigned drops = hs.native.rates_drops, restores = hs.native.rates_restores;
        w->pool[1] = 0;  // instance 1 now in instance 0's pool
        hk::PublishArm(pi::Arm{});
        dispatch(w->root0, 8);
        hk::DrainStats(hs);
        Check(!hs.native.rates_applied && hs.native.rates_drops == drops + 1 && hs.native.rates_restores == restores &&
              std::fabs(rate(0x48) - 1.2f) < 1e-6f && id0_untouched(), "rates: a broken chain drops without writing");
        w->pool[1] = 1;
        Put<std::uintptr_t>(blk1 + c::blk_ps, 0);
        a.rates_mirror = false;
        a.rates_sp = a.rates_rec = 0;
        a.rates_id = 0;
    }
    // Aim parity (docs/native-puppet.md "Facing", the aim-target mirror): the
    // published target reaches instance 1's network aim slot Q+0x10[1] and its
    // sample record (current = previous = target, timers 0) before block 1's
    // job runs; instance 0's slot and record are never touched; a stale target
    // holds, a target no longer wanted restores the originals, as does a
    // disarm at the main dispatch; a slot the game rewrote is counted
    // (overwritten); every chain refusal (owner, slots, count, alignment)
    // writes nothing; forget drops without writing and its epoch never writes
    // again; an access fault latches until the next forget.
    {
        namespace pap = mgscoop::partner_aim_point;
        namespace pam = mgscoop::partner_aim;
        pap::detail::ResetForTest();
        alignas(16) static unsigned char q_obj[0x200], s_obj[0x80], aim_arr[2 * c::q_aim_stride], aim_recs[2 * c::q_aim_rec_stride];
        std::memset(q_obj, 0, sizeof(q_obj));
        std::memset(s_obj, 0, sizeof(s_obj));
        const std::uintptr_t q = Addr(q_obj), arr = Addr(aim_arr), recs = Addr(aim_recs);
        Put<std::uintptr_t>(q, base + c::q_vtable_rva);
        Put<std::uintptr_t>(q + c::q_s, Addr(s_obj));
        Put<std::uint32_t>(Addr(s_obj) + c::s_count, 2);
        Put<std::uintptr_t>(q + c::q_aim, arr);
        Put<std::uintptr_t>(q + c::q_aim_rec, recs);
        const float slot0[4] = {7.0f, 8.0f, 9.0f, 1.0f}, slot1[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const auto reset_slots = [&]() {
            std::memcpy(aim_arr, slot0, sizeof(slot0));
            std::memcpy(aim_arr + c::q_aim_stride, slot1, sizeof(slot1));
            std::memset(aim_recs, 0, sizeof(aim_recs));
            for (unsigned k = 0; k < 2; ++k) Put<float>(recs + k * c::q_aim_rec_stride + 0x24, -1.0f);  // the allocator's initial duration
            std::memset(aim_recs, 0x5A, 0x20);  // instance 0's record: a sentinel
        };
        reset_slots();
        unsigned char rec0_before[0x30];
        std::memcpy(rec0_before, aim_recs, sizeof(rec0_before));
        const auto inst0_untouched = [&]() {
            return std::memcmp(aim_arr, slot0, sizeof(slot0)) == 0 && std::memcmp(aim_recs, rec0_before, sizeof(rec0_before)) == 0;
        };
        const std::uintptr_t q_before = Get<std::uintptr_t>(P + c::p_q);
        Put<std::uintptr_t>(P + c::p_q, q);  // ValidatePad checks only Q's class
        static std::uintptr_t s_arr = 0;
        static float s_job_x = 0.0f;
        s_arr = arr;
        struct AimJob { static void Job(void*, const void*) noexcept { s_job_x = Get<float>(s_arr + c::q_aim_stride); } };
        const float target[3] = {12.5f, 3.25f, -88.0f};
        std::uint32_t epoch = 1;
        const auto publish = [&](bool want, bool valid, const float (&p)[3], std::uint64_t age_ms) {
            pap::Publication pub{};
            pub.want = want;
            pub.valid = valid;
            std::memcpy(pub.p, p, sizeof(pub.p));
            pub.src = static_cast<std::uint8_t>(pam::Source::look);
            pub.reason = static_cast<std::uint8_t>(valid ? pam::Reason::none : want ? pam::Reason::no_look : pam::Reason::off);
            pub.ms = pap::detail::NowMs() - age_ms;
            pub.epoch = epoch;
            pap::Publish(pub);
        };
        pd::JobParam param{1, 0, 0, 8, g_world.bits};
        pap::Stats as{};
        const auto drain = [&]() {
            dispatch(w->root1, 8);
            const bool windowed = hk::RunJobWindow(w->go, &param, 1, &AimJob::Job, pi::Consumer::drain0);
            pap::ReadStats(as);
            return windowed;
        };
        const auto slot1_is = [&](const float (&p)[3]) {
            float v[4]{};
            std::memcpy(v, aim_arr + c::q_aim_stride, sizeof(v));
            return v[0] == p[0] && v[1] == p[1] && v[2] == p[2] && v[3] == 1.0f;
        };
        const auto rec1_is = [&](const float (&p)[3]) {
            const std::uintptr_t r = recs + c::q_aim_rec_stride;
            for (unsigned i = 0; i < 3; ++i)
                if (Get<float>(r + 4 * i) != p[i] || Get<float>(r + c::q_aim_rec_prev + 4 * i) != p[i]) return false;
            return Get<float>(r + 0x24) == 0.0f && Get<float>(r + 0x28) == 0.0f && Get<float>(r + 0x2C) == 0.0f;
        };
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        a.gen = 10;
        hk::PublishArm(a);
        // Nothing published wanted yet: nothing written.
        publish(false, false, origin, 0);
        Check(drain() && slot1_is(origin) && as.writes == 0 && !as.applied, "aim: off -> the slot untouched");
        publish(true, true, target, 0);
        Check(drain() && s_job_x == target[0] && slot1_is(target) && rec1_is(target) && inst0_untouched() && as.writes == 1 && as.applied &&
                  as.state == static_cast<std::uint8_t>(pap::State::ok) && Logged("partner_aim=installed"),
              "aim: the target is in Q+0x10[1] and record 1 (cur = prev, timers 0) before the job; instance 0 untouched");
        Check(drain() && as.writes == 2 && as.overwritten == 0, "aim: a second drain writes again, nothing overwritten");
        Put<float>(arr + c::q_aim_stride, 99.0f);  // something else wrote the slot
        Check(drain() && as.overwritten == 1 && slot1_is(target) && Logged("partner_aim=overwritten"), "aim: a foreign write is counted and replaced");
        // A wanted target without a look keeps the slot; a stale publication too.
        const float moved[3] = {13.5f, 3.25f, -88.0f};
        publish(true, false, moved, 0);
        Check(drain() && slot1_is(target) && as.state == static_cast<std::uint8_t>(pap::State::hold), "aim: no look -> hold (the last target stays)");
        publish(true, true, moved, 5000);
        Check(drain() && slot1_is(target) && as.reason == static_cast<std::uint8_t>(pam::Reason::stale), "aim: a stale publication holds");
        const float wild[3] = {70000.0f, 0.0f, 0.0f};
        publish(true, true, wild, 0);
        Check(drain() && slot1_is(target) && as.reason == static_cast<std::uint8_t>(pam::Reason::range), "aim: an out-of-range target refuses");
        // Off again: the originals come back (fields the game did not rewrite).
        publish(false, false, origin, 0);
        Check(drain() && slot1_is(origin) && Get<float>(recs + c::q_aim_rec_stride + 0x24) == -1.0f && as.restores == 1 && !as.applied &&
                  inst0_untouched() && Logged("partner_aim=restored"),
              "aim: a target no longer wanted restores Q+0x10[1] and record 1 (duration -1.0 back)");
        // A disarm restores at the next main dispatch (no drain runs any more).
        publish(true, true, target, 0);
        Check(drain() && slot1_is(target) && as.applied, "aim: applied again");
        hk::PublishArm(pi::Arm{});
        dispatch(w->root0, 8);
        pap::ReadStats(as);
        Check(slot1_is(origin) && as.restores == 2 && !as.applied && inst0_untouched(), "aim: the disarmed Arm restores at the main dispatch");
        a.gen = 11;
        hk::PublishArm(a);
        // Chain refusals write nothing.
        const auto refused = [&](pam::Reason why) {
            reset_slots();
            publish(true, true, target, 0);
            const bool windowed = drain();
            return windowed && slot1_is(origin) && as.reason == static_cast<std::uint8_t>(why) && !as.applied;
        };
        Put<std::uint32_t>(P + c::p_local_id, 1);
        Check(refused(pam::Reason::owner), "aim: instance 1 as the local id refuses (owner)");
        Put<std::uint32_t>(P + c::p_local_id, 0);
        Put<std::uintptr_t>(base + c::q_vtable_rva + c::q_vt_aim_lerp, base + 0x40);
        Check(refused(pam::Reason::slots), "aim: a Q vtable whose synchronizer slot changed refuses (slots)");
        Put<std::uintptr_t>(base + c::q_vtable_rva + c::q_vt_aim_lerp, base + c::q_aim_lerp_rva);
        Put<std::uint32_t>(Addr(s_obj) + c::s_count, 1);
        Check(refused(pam::Reason::count), "aim: a Q without id 1 refuses (count)");
        Put<std::uint32_t>(Addr(s_obj) + c::s_count, 2);
        Put<std::uintptr_t>(q + c::q_aim, arr + 8);
        Check(refused(pam::Reason::array), "aim: a misaligned array refuses");
        Put<std::uintptr_t>(q + c::q_aim, arr);
        Put<std::uintptr_t>(q + c::q_aim_rec, 0);
        Check(refused(pam::Reason::record), "aim: no record array refuses");
        Put<std::uintptr_t>(q + c::q_aim_rec, recs);
        Check(as.refused >= 5 && Logged("partner_aim=refused; reason=owner"), "aim: refusals counted and logged");
        // Forget (MissionStart/End): dropped without writing; that epoch never writes again; a new one does.
        reset_slots();
        publish(true, true, target, 0);
        Check(drain() && slot1_is(target), "aim: applied before the forget");
        pap::Forget(epoch);
        Check(drain() && slot1_is(target) && !as.applied && as.drops >= 1, "aim: forget drops the state without writing (the slot keeps the target)");
        reset_slots();
        Check(drain() && slot1_is(origin) && as.writes > 0, "aim: the forgotten epoch never writes again");
        epoch = 2;
        publish(true, true, target, 0);
        Check(drain() && slot1_is(target) && as.applied, "aim: a new epoch writes");
        // An access fault latches the mirror (until the next forget); the job still runs.
        Put<std::uintptr_t>(q + c::q_aim_rec, 0x8000);  // 16-aligned, inside the never-mapped first 64 KB
        s_job_x = -1.0f;
        Check(drain() && s_job_x == target[0] && as.latched && as.state == static_cast<std::uint8_t>(pap::State::latched) &&
                  Logged("partner_aim=fault"),
              "aim: an access fault latches (the job still ran)");
        Put<std::uintptr_t>(q + c::q_aim_rec, recs);
        reset_slots();
        Check(drain() && slot1_is(origin) && as.latched, "aim: latched: nothing written");
        pap::Forget(epoch);
        epoch = 3;
        publish(true, true, target, 0);
        Check(drain() && !as.latched && slot1_is(target), "aim: the next epoch clears the latch");
        // The debug override and its command.
        char result[192]{};
        bool ok = false;
        Check(pap::HandleCommand("aimpt off", result, sizeof(result), ok) && ok && pap::Override() == 0 &&
                  std::strncmp(result, "aimpt=off; override=set", 23) == 0, "aim: 'aimpt off' sets the override");
        Check(pap::HandleCommand("aimpt point", result, sizeof(result), ok) && ok && pap::Override() == 2, "aim: 'aimpt point'");
        Check(pap::HandleCommand("aimpt lab", result, sizeof(result), ok) && ok && pap::Override() == -1 &&
                  std::strncmp(result, "aimpt=lab; override=none", 24) == 0, "aim: 'aimpt lab' clears the override");
        Check(pap::HandleCommand("aimpt bogus", result, sizeof(result), ok) && !ok && pap::Override() == -1, "aim: a bad argument refuses");
        Check(!pap::HandleCommand("aimptx", result, sizeof(result), ok) && !pap::HandleCommand("nametag on", result, sizeof(result), ok),
              "aim: other commands are not aimpt");
        // A restore whose chain changed (Q now names another aim array) drops
        // the state without writing either array and logs why.
        reset_slots();
        publish(true, true, target, 0);
        Check(drain() && slot1_is(target) && as.applied, "aim: applied before the chain changes");
        alignas(16) static unsigned char aim_arr2[2 * c::q_aim_stride];
        std::memset(aim_arr2, 0x33, sizeof(aim_arr2));
        unsigned char arr2_before[sizeof(aim_arr2)];
        std::memcpy(arr2_before, aim_arr2, sizeof(arr2_before));
        Put<std::uintptr_t>(q + c::q_aim, Addr(aim_arr2));
        const std::uint32_t drops_before = as.drops, restores_before = as.restores;
        publish(false, false, origin, 0);
        Check(drain() && slot1_is(target) && std::memcmp(aim_arr2, arr2_before, sizeof(arr2_before)) == 0 && !as.applied &&
                  as.drops == drops_before + 1 && as.restores == restores_before && inst0_untouched() &&
                  Logged("partner_aim=restore_dropped; reason=chain; changed=object; written=0"),
              "aim: a restore through a changed chain drops without writing and logs why");
        Put<std::uintptr_t>(q + c::q_aim, arr);
        // A chain flipping between refused and ok every frame: the event lines
        // (installed, refused, ...) stop at the per-epoch budget.
        reset_slots();
        const std::size_t lines_before = LogCount("partner_aim=");
        for (int i = 0; i < 60; ++i) {
            publish(true, true, target, 0);
            Put<std::uint32_t>(P + c::p_local_id, 1);
            drain();
            Put<std::uint32_t>(P + c::p_local_id, 0);
            drain();
        }
        Check(LogCount("partner_aim=") - lines_before <= 64 && LogCount("further=suppressed") == 1 && as.applied && slot1_is(target) &&
                  as.refused >= 60 + 5,
              "aim: refusals stay counted, their lines stop at the per-epoch budget");
        pap::Forget(epoch);
        epoch = 4;
        reset_slots();
        const std::size_t installed_before = LogCount("partner_aim=installed");
        publish(true, true, target, 0);
        Check(drain() && as.applied && LogCount("partner_aim=installed") == installed_before + 1,
              "aim: a new epoch has a new line budget");
        // Leave: restored, Q back.
        publish(false, false, origin, 0);
        Check(drain() && slot1_is(origin), "aim: restored at the end");
        // The game window closes (PartnerShutdown sets the flag on the window
        // thread): no write and no restore from then on; block 1's job still runs.
        publish(true, true, target, 0);
        Check(drain() && slot1_is(target) && as.applied, "aim: applied before the window closes");
        pap::Shutdown();
        const float moved2[3] = {14.5f, 3.25f, -88.0f};
        publish(true, true, moved2, 0);
        s_job_x = -1.0f;
        Check(drain() && s_job_x == target[0] && slot1_is(target) && as.reason == static_cast<std::uint8_t>(pam::Reason::shutdown),
              "aim: after the window closes nothing is written (the job still ran)");
        const std::uint32_t restores_at_close = as.restores;
        publish(false, false, origin, 0);
        Check(drain() && slot1_is(target), "aim: after the window closes an unwanted target is not restored in the window");
        hk::PublishArm(pi::Arm{});
        dispatch(w->root0, 8);
        pap::ReadStats(as);
        Check(slot1_is(target) && !as.applied && as.restores == restores_at_close && inst0_untouched(),
              "aim: after the window closes the main dispatch drops without restoring");
        pap::detail::ResetForTest();
        hk::PublishArm(a);
        Put<std::uintptr_t>(P + c::p_q, q_before);
    }
    // Native weapons (docs/native-puppet.md "Weapons"): the magazine mirror
    // writes instance 1's live magazine before its job (on a FIRE or RELOAD
    // press edge or a changed remote magazine), never through a changed prim
    // chain or into a full-magazine gun; the viewer's shoot count and
    // instance 1's reload request are watched around the job.
    {
        alignas(16) static unsigned char gun_prim[0x100], gun_rec[c::gun_rec_size], vars[c::vars_ammo_end + 0x10];
        std::memset(gun_prim, 0, sizeof(gun_prim));
        std::memset(gun_rec, 0, sizeof(gun_rec));
        std::memset(vars, 0, sizeof(vars));
        Put<std::uintptr_t>(Addr(gun_prim), base + c::gun_prim_vt_rva);
        Put<std::uintptr_t>(Addr(gun_prim) + c::prim_blk, blk1);
        Put<std::uintptr_t>(Addr(gun_prim) + c::prim_gun_rec, Addr(gun_rec));
        Put<std::uint32_t>(Addr(gun_rec) + c::gun_flags27c, 1u << 6);  // class 1
        Put<std::uint16_t>(Addr(gun_rec) + c::gun_cap, 5);
        Put<std::uintptr_t>(base + c::vars_slot_rva, Addr(vars));
        Put<std::uint32_t>(Addr(vars) + c::vars_shoot_count, 7);
        static std::uintptr_t s_rec = 0, s_vars = 0;
        static std::uint16_t s_seen = 0;
        static bool s_shoot = false, s_reload = false, s_ammo = false, s_item = false;
        s_rec = Addr(gun_rec);
        s_vars = Addr(vars);
        struct GunJob {
            static void Job(void*, const void*) noexcept {
                s_seen = Get<std::uint16_t>(s_rec + c::gun_mag);
                if (s_shoot) Put<std::uint32_t>(s_vars + c::vars_shoot_count, Get<std::uint32_t>(s_vars + c::vars_shoot_count) + 1);
                if (s_reload) Put<std::uint32_t>(s_rec + c::gun_flags, Get<std::uint32_t>(s_rec + c::gun_flags) | c::gun3c0_reload);
                // A cross-instance ammo write (what the ammo digest must catch): the viewer's
                // vars.ammoInWeapons[0] at +0x554.
                if (s_ammo) Put<std::uint16_t>(s_vars + 0x554, static_cast<std::uint16_t>(Get<std::uint16_t>(s_vars + 0x554) + 1));
                // Item 3: the viewer's own item stock 0xE + 1 (ammoStockCounts[15] at +0x632; its box
                // knocked off in a collision inside this job): benign, never a leak.
                if (s_item) Put<std::uint16_t>(s_vars + 0x632, static_cast<std::uint16_t>(Get<std::uint16_t>(s_vars + 0x632) + 1));
            }
        };
        a.naim = mgscoop::PartnerNativeAim::off;
        a.ps1 = 0;
        a.mag_mirror = true;
        a.gun_prim = Addr(gun_prim);
        a.gun_rec = Addr(gun_rec);
        hk::PublishArm(a);
        // F2: the store row gun phase 0 copies into the record every frame.
        Put<std::uintptr_t>(P + c::p_equip, Addr(w->store_x));
        Put<std::uintptr_t>(Addr(w->store_x) + c::store_y, Addr(w->store_y));
        Put<std::uintptr_t>(Addr(w->store_y), base + c::store_vt_rva);
        Put<std::uintptr_t>(base + c::store_vt_rva + c::store_vt_set, base + c::store_set_rva);
        Put<std::uint32_t>(Addr(w->store_y) + c::store_stride, 2);
        Put<std::uint32_t>(Addr(w->store_y) + c::store_total, 4);  // two ids * stride 2 (the init's row count)
        Put<std::uintptr_t>(Addr(w->store_y) + c::store_types, Addr(w->store_types));
        Put<std::uintptr_t>(Addr(w->store_y) + c::store_rows, Addr(w->store_rows));
        w->store_types[2] = 1;  // id 1, slot 0: a magazine row (type 1)
        g_hook_sample.packet.version = 4;
        g_hook_sample.packet.xflags = pi::XFlag::Mag;
        g_hook_sample.packet.mag = 20;
        g_hook_sample.packet.mag_cap = 30;
        g_hook_sample.packet.held = kRemoteHeld;
        pd::JobParam param{1, 0, 0, 8, g_world.bits};
        const auto run = [&]() {
            dispatch(w->root1, 8);  // a new frame injection from the sample
            const bool windowed = hk::RunJobWindow(w->go, &param, 1, &GunJob::Job, pi::Consumer::drain0);
            hk::DrainStats(hs);
            return windowed;
        };
        Check(run() && s_seen == 4 && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 4 && hs.native.mag_writes == 1 &&
              hs.native.mag_written == 4 && hs.native.mag_cap == 5 && hs.native.mag_remote == 20 && hs.native.mag_remote_cap == 30 &&
              hs.native.mag_last == hk::MagRefusal::none, "first v4 sample: 20/30 mapped onto capacity 5 -> 4, written before the job");
        Check(StoreWord(w, 0) == 4 && StoreWord(w, 1) == 0 && hs.native.store_writes == 1 && hs.native.store_last == hk::StoreRefusal::none &&
                  hs.native.mag_inj == 20 && hs.native.mag_inj_cap == 30,
              "F2: the store row word 0 carries the mirror (the game copies it every frame), the injected magazine reported");
        Put<std::uint16_t>(Addr(gun_rec) + c::gun_mag, 2);  // the game changed it; the remote did not
        Check(run() && s_seen == 2 && hs.native.mag_writes == 1, "no edge and no remote change: no write");
        g_hook_sample.packet.held = kRemoteHeld | partner_native_fire_bit();
        Check(run() && s_seen == 4 && hs.native.mag_writes == 2 && hs.native.mag_fire_edges == 1, "a FIRE press edge re-asserts the mirror");
        g_hook_sample.packet.held = kRemoteHeld;
        g_hook_sample.packet.mag = 0;
        Check(run() && s_seen == 0 && hs.native.mag_writes == 3 && hs.native.mag_written == 0, "the remote ran dry: the partner too");
        g_hook_sample.packet.held = kRemoteHeld | partner_native_fire_bit();
        Check(run() && s_seen == 0 && hs.native.mag_writes == 3 && hs.native.mag_fire_edges == 1,
              "a FIRE edge on an already empty remote is a dry fire: the partner stays empty (no visual shot)");
        g_hook_sample.packet.held = 0x08u;  // FIRE and RELOAD released: no press edge, the remote unchanged
        Check(run() && s_seen == 0 && hs.native.mag_writes == 3, "releases write nothing");
        Put<std::uint16_t>(Addr(gun_rec) + c::gun_mag, 3);  // the game changed it
        g_hook_sample.packet.held = kRemoteHeld;  // RELOAD pressed (edge) with the remote still empty
        Check(run() && s_seen == 0 && hs.native.mag_writes == 4 && hs.native.mag_reload_edges == 1,
              "a RELOAD press edge re-asserts the remote's magazine (empty: reloadable)");
        g_hook_sample.packet.mag = 1;  // one round left on the remote
        Check(run() && s_seen == 1 && hs.native.mag_writes == 5, "one remote round maps to 1");
        Put<std::uint16_t>(Addr(gun_rec) + c::gun_mag, 0);
        g_hook_sample.packet.held = kRemoteHeld | partner_native_fire_bit();
        g_hook_sample.packet.mag = 0;  // the press spent the last round in the same sample
        Check(run() && s_seen == 1 && hs.native.mag_writes == 6 && hs.native.mag_fire_edges == 2,
              "a FIRE edge that spent the remote's last round still shows that shot");
        g_hook_sample.packet.held = kRemoteHeld;
        g_hook_sample.packet.mag = 30;
        Check(run() && s_seen == 5 && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 5, "the remote reloaded: full");
        // The sub magazine (flags bit 9) is the field written then.
        Put<std::uint32_t>(Addr(gun_rec) + c::gun_flags27c, (1u << 6) | c::gun27c_sub);
        Put<std::uint16_t>(Addr(gun_rec) + c::gun_cap_sub, 3);
        g_hook_sample.packet.mag = 10;
        Check(run() && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag_sub) == 1 && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 5 &&
                  StoreWord(w, 1) == 1 && StoreWord(w, 0) == 5,
              "flags bit 9: the sub magazine rec+0x25E (capacity 3) and store word 1 are written, word 0 untouched");
        Put<std::uint32_t>(Addr(gun_rec) + c::gun_flags27c, 1u << 6);
        // Refusals: a changed prim chain, a full-magazine gun, a non-gun class.
        const std::uint32_t writes = hs.native.mag_writes;
        Put<std::uintptr_t>(Addr(gun_prim) + c::prim_gun_rec, Addr(gun_rec) + 0x10);
        g_hook_sample.packet.mag = 11;
        Check(run() && hs.native.mag_writes == writes && hs.native.mag_last == hk::MagRefusal::prim && s_seen == 5,
              "prim+0x58 changed: refused, nothing written");
        Put<std::uintptr_t>(Addr(gun_prim) + c::prim_gun_rec, Addr(gun_rec));
        Put<std::uint32_t>(Addr(gun_rec) + c::gun_flags, c::gun3c0_full_mag);
        g_hook_sample.packet.mag = 12;
        Check(run() && hs.native.mag_writes == writes && hs.native.mag_last == hk::MagRefusal::full, "full-magazine gun: refused");
        Put<std::uint32_t>(Addr(gun_rec) + c::gun_flags, 0);
        Put<std::uint32_t>(Addr(gun_rec) + c::gun_flags27c, 2u << 6);
        g_hook_sample.packet.mag = 13;
        Check(run() && hs.native.mag_writes == writes && hs.native.mag_last == hk::MagRefusal::cls, "class 2: refused");
        Put<std::uint32_t>(Addr(gun_rec) + c::gun_flags27c, 1u << 6);
        // F2 refusals: each check of the store row refuses the whole write (the record alone would be overwritten).
        {
            const std::uint16_t before = StoreWord(w, 0), rec_before = Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag);
            const auto refused = [&](std::uint16_t mag, hk::StoreRefusal want) {
                g_hook_sample.packet.mag = mag;
                return run() && hs.native.mag_writes == writes && hs.native.mag_last == hk::MagRefusal::store && hs.native.store_last == want &&
                       StoreWord(w, 0) == before && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == rec_before;
            };
            Put<std::uintptr_t>(Addr(w->store_y), base + 0x40);
            Check(refused(21, hk::StoreRefusal::y_vt), "F2: another store class refuses (y_vt)");
            Put<std::uintptr_t>(Addr(w->store_y), base + c::store_vt_derived_rva);
            Put<std::uintptr_t>(base + c::store_vt_derived_rva + c::store_vt_set, base + 0x50);
            Check(refused(22, hk::StoreRefusal::y_vt), "F2: a derived class whose +0x20 is not the setter refuses");
            Put<std::uintptr_t>(base + c::store_vt_derived_rva + c::store_vt_set, base + c::store_set_rva);
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_slot, 2);
            Check(refused(23, hk::StoreRefusal::slot), "F2: a slot beyond the stride refuses");
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_slot, 0);
            Put<std::uint32_t>(Addr(w->store_y) + c::store_total, 2);  // a store sized for one id only
            Check(refused(28, hk::StoreRefusal::slot), "F2: a store without id 1's row (row count Y+0x10) refuses");
            Put<std::uint32_t>(Addr(w->store_y) + c::store_total, 4);
            w->store_types[2] = 0;
            Check(refused(24, hk::StoreRefusal::type), "F2: a row of type 0 refuses");
            w->store_types[2] = 1;
            Put<std::uint32_t>(Addr(gun_rec) + c::gun_tamper, c::gun3d0_tamper_check);
            Check(refused(25, hk::StoreRefusal::tamper), "F2: rec+0x3D0 bit 27 (the tamper check) refuses");
            Put<std::uint32_t>(Addr(gun_rec) + c::gun_tamper, 0);
            Put<std::uint32_t>(P + c::p_local_id, 1);
            Check(refused(26, hk::StoreRefusal::owner), "F2: instance 1 as the local id refuses");
            Put<std::uint32_t>(P + c::p_local_id, 0);
            Check(hs.native.store_refused >= 6, "F2: refusals counted");
            g_hook_sample.packet.mag = 30;
            Check(run() && StoreWord(w, 0) == 5 && hs.native.mag_last == hk::MagRefusal::none, "F2: the derived store class writes");
            // F4: the weapon gate freezes the mirror and strips FIRE/RELOAD from the injection.
            static std::uint32_t s_held_seen = 0;
            struct HeldJob { static void Job(void*, const void*) noexcept { s_held_seen = Get<std::uint32_t>(g_watch_src + 0x48); } };
            a.strip = mgscoop::partner_native::kPadFire | mgscoop::partner_native::kPadReload;
            hk::PublishArm(a);
            g_hook_sample.packet.mag = 2;
            g_hook_sample.packet.held = kRemoteHeld | partner_native_fire_bit();
            dispatch(w->root1, 8);
            Check(hk::RunJobWindow(w->go, &param, 1, &HeldJob::Job, pi::Consumer::drain0), "F4: windowed");
            hk::DrainStats(hs);
            Check(s_held_seen == (kRemoteHeld & ~0x50u) && hs.native.mag_last == hk::MagRefusal::weapon && StoreWord(w, 0) == 5 &&
                      hs.native.strip_windows >= 1 && hs.native.strip_bits == 0x50u,
                  "F4: FIRE and RELOAD stripped, the mirror frozen (nothing written)");
            a.strip = 0;
            hk::PublishArm(a);
            g_hook_sample.packet.held = kRemoteHeld;
        }
        // R4: the freeze while either side reloads and the reconcile with rec+0x3E8 (reload=raw|event).
        {
            g_hook_sample.packet.version = 5;
            g_hook_sample.packet.xflags = pi::XFlag::Mag;
            g_hook_sample.packet.mag = 3;
            g_hook_sample.packet.mag_cap = 5;
            g_hook_sample.packet.held = 0x08u;
            g_hook_sample.packet.gun_flags = pi::GunFlag::Valid;
            g_hook_sample.packet.gun_mode = c::gun_mode_aim;
            a.reload = mgscoop::PartnerNativeReload::raw;
            hk::PublishArm(a);
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_mode_cur, c::gun_mode_aim);
            Check(run() && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 3 && hs.native.mag_last == hk::MagRefusal::none,
                  "R4: nothing reloading: the mirror writes (3/5)");
            const hk::NativeStats f0 = hs.native;
            // The remote enters its reload (v5 Reloading) and presses R: frozen, the R edge counted but never written.
            g_hook_sample.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::Reloading;
            g_hook_sample.packet.gun_mode = c::gun_mode_reload;
            g_hook_sample.packet.held = 0x48u;
            Check(run() && hs.native.mag_last == hk::MagRefusal::reload && hs.native.mag_frozen == f0.mag_frozen + 1 &&
                      hs.native.mag_reload_seen == f0.mag_reload_seen + 1 && hs.native.mag_writes == f0.mag_writes,
                  "R4: the remote reloading freezes the mirror");
            // Body 1 enters its own reload (rec+0x2C0 = 11); the remote's refill arrives meanwhile: still frozen.
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_mode_cur, c::gun_mode_reload);
            g_hook_sample.packet.gun_flags = pi::GunFlag::Valid;
            g_hook_sample.packet.gun_mode = c::gun_mode_aim;
            g_hook_sample.packet.mag = 5;
            Put<std::uint16_t>(Addr(gun_rec) + c::gun_mag, 2);  // the partner's own reload in progress
            Check(run() && hs.native.mag_last == hk::MagRefusal::reload && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 2 &&
                      hs.native.mag_writes == f0.mag_writes, "R4: body 1 reloading: frozen (the remote's refill is not written)");
            // Body 1's completion fills it and the mode leaves: one reconcile write with rec+0x3E8.
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_mode_cur, c::gun_mode_aim);
            Put<std::uint16_t>(Addr(gun_rec) + c::gun_m3e8, 1);
            Check(run() && hs.native.mag_reconciles == f0.mag_reconciles + 1 && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 5 &&
                      Get<std::uint16_t>(Addr(gun_rec) + c::gun_m3e8) == 5 && hs.native.mag_m3e8_writes == f0.mag_m3e8_writes + 1 &&
                      hs.native.mag_last == hk::MagRefusal::none, "R4: the reconcile writes the magazine and rec+0x3E8");
            Check(run() && hs.native.mag_reconciles == f0.mag_reconciles + 1, "R4: one reconcile only");
            // F1: body 1's reload mode freezes however long it lasts (a stuck report never releases it;
            // the reconcile only follows the native exit), also with the queued tap's reconcile.
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_mode_cur, c::gun_mode_reload2);
            a.reload_reconcile = true;
            hk::PublishArm(a);
            g_hook_sample.packet.mag = 4;
            Check(run() && hs.native.mag_last == hk::MagRefusal::reload && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 5,
                  "F1: body 1 still reloading: frozen (never a reconcile at stuck)");
            // The queued tap's reconcile: the remote reloads again, body 1 left its reload with a full magazine;
            // the mirror writes the remote's magazine (body 1's next reload needs it below capacity).
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_mode_cur, c::gun_mode_aim);
            g_hook_sample.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::Reloading;
            g_hook_sample.packet.gun_mode = c::gun_mode_reload;
            Check(run() && hs.native.mag_last == hk::MagRefusal::none && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 4,
                  "F1: the reconcile writes the remote's magazine although the remote reloads");
            a.reload_reconcile = false;
            hk::PublishArm(a);
            g_hook_sample.packet.mag = 2;
            Check(run() && hs.native.mag_last == hk::MagRefusal::reload && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 4,
                  "F1: without the reconcile the remote's reload freezes again");
            g_hook_sample.packet.gun_flags = pi::GunFlag::Valid;
            g_hook_sample.packet.gun_mode = c::gun_mode_aim;
            // A lost exit: body 1 wedged in 12 freezes until the driver's release (release_ms); then the mirror
            // writes again, the first write being the reconcile (rec+0x3E8 too). The release never overrides the
            // remote's own reload.
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_mode_cur, c::gun_mode_reload2);
            Check(run() && hs.native.mag_last == hk::MagRefusal::reload && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 4,
                  "F1: body 1 wedged in its reload mode: frozen");
            a.reload_release = true;
            hk::PublishArm(a);
            g_hook_sample.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::Reloading;
            Check(run() && hs.native.mag_last == hk::MagRefusal::reload && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 4,
                  "F1: the release never overrides the remote's reload");
            g_hook_sample.packet.gun_flags = pi::GunFlag::Valid;
            {
                const hk::NativeStats r0 = hs.native;
                Check(run() && hs.native.mag_last == hk::MagRefusal::none && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 2 &&
                          Get<std::uint16_t>(Addr(gun_rec) + c::gun_m3e8) == 2 && hs.native.mag_reconciles == r0.mag_reconciles + 1,
                      "F1: released: the mirror reconciles although body 1 still shows 12 (never frozen forever)");
            }
            a.reload_release = false;
            hk::PublishArm(a);
            a.reload_hold = true;
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_mode_cur, c::gun_mode_aim);
            hk::PublishArm(a);
            g_hook_sample.packet.mag = 3;
            Check(run() && hs.native.mag_last == hk::MagRefusal::reload, "R4: the driver's hold freezes");
            // mirror: never frozen, and the R edge re-asserts as before.
            a.reload_hold = false;
            a.reload = mgscoop::PartnerNativeReload::mirror;
            hk::PublishArm(a);
            g_hook_sample.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::Reloading;
            Check(run() && hs.native.mag_last == hk::MagRefusal::none && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 3,
                  "R4: reload=mirror never freezes");
            g_hook_sample.packet.gun_flags = 0;
            g_hook_sample.packet.gun_mode = 0;
            g_hook_sample.packet.held = kRemoteHeld;
        }
        // W4: the slot follower (Q+0xC0 rows, the inventory handles, the equip controller).
        {
            std::memset(w->q2, 0, sizeof(w->q2));
            Put<std::uintptr_t>(Addr(w->q2), base + c::q_vtable_rva);
            Put<std::uintptr_t>(P + c::p_q, Addr(w->q2));
            Put<std::uintptr_t>(Addr(w->q2) + c::q_equip_rows, Addr(w->equip_rows));
            Put<std::uintptr_t>(Addr(w->store_x), base + c::equip_ctl_vt_rva);
            Put<std::uintptr_t>(Addr(w->store_x) + c::ctl_player, P);
            Put<std::uint32_t>(Addr(w->store_y) + c::store_stride, 7);
            Put<std::uint32_t>(Addr(w->store_y) + c::store_total, 14);
            Put<std::uintptr_t>(Addr(w->store_y) + c::inv_handles, Addr(w->inv_handles));
            Put<std::uintptr_t>(Addr(w->store_y) + c::store_rows, Addr(w->inv_rows));
            for (auto& h : w->inv_handles) h = 0xFFFF0000u;
            const std::uintptr_t row1 = Addr(w->equip_rows) + c::equip_row_stride;
            // Body 1: 46 in the back slot (current), 9 in the secondary.
            w->inv_handles[7 + 1] = (4u << 16) | (46u << 5) | 4u;
            w->inv_handles[7 + 2] = (5u << 16) | (9u << 5) | 2u;
            Put<std::uint16_t>(row1 + c::equip_row_slots + 2, 46);
            Put<std::uint16_t>(row1 + c::equip_row_slots + 4, 9);
            Put<std::uint16_t>(row1 + c::equip_row_current, 46);
            Put<std::uint8_t>(row1 + c::equip_row_nibble, 0x51);  // high nibble kept, slot 1
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_slot_src, c::gun_slot_follow);
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_slot, 1);
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_mode_cur, c::gun_mode_aim);
            Put<std::uint32_t>(Addr(gun_rec) + c::gun_flags, 0);
            a.slot_follow = mgscoop::PartnerNativeSlot::write;
            a.slot_ok = 0x6;
            a.slot_equip[1] = 46;
            a.slot_equip[2] = 9;
            hk::PublishArm(a);
            g_hook_sample.packet.version = 5;
            g_hook_sample.packet.gun_flags = pi::GunFlag::Valid;
            g_hook_sample.packet.inv_slot = 1;
            g_hook_sample.packet.switch_seq = 1;
            const auto slot_run = [&](std::uint8_t target, std::uint8_t seq) {
                g_hook_sample.packet.inv_slot = target;
                g_hook_sample.packet.switch_seq = seq;
                return run();
            };
            Check(slot_run(1, 1) && hs.native.slot_last == hk::SlotRefusal::same && hs.native.slot_writes == 0,
                  "W4: already in the remote's slot: nothing written");
            Check(slot_run(2, 2) && hs.native.slot_writes == 1 && hs.native.slot_last == hk::SlotRefusal::none &&
                      (Get<std::uint8_t>(row1 + c::equip_row_nibble) & 0xFu) == 2 && (Get<std::uint8_t>(row1 + c::equip_row_nibble) & 0xF0u) == 0x50 &&
                      Get<std::uint16_t>(row1 + c::equip_row_current) == 9 && (Get<std::uint32_t>(Addr(w->q2) + c::q_equip_dirty) & 2u) &&
                      hs.native.slot_target == 2,
                  "W4: the secondary: the nibble, row+0 and Q+0x17C bit 1 written like SetCurrentSlot (non-local)");
            Check(slot_run(2, 2) && hs.native.slot_writes == 1 && hs.native.slot_last == hk::SlotRefusal::same, "W4: then the same slot");
            const std::uint32_t refused0 = hs.native.slot_refused;
            // The empty hip slot reports empty before the plan (the live handle is read first).
            Check(slot_run(0, 3) && hs.native.slot_last == hk::SlotRefusal::empty && hs.native.slot_refused == refused0 + 1 &&
                      (Get<std::uint8_t>(row1 + c::equip_row_nibble) & 0xFu) == 2, "W4: an empty slot the plan does not admit: empty");
            Check(slot_run(0, 3) && hs.native.slot_refused == refused0 + 1, "W4: a refusal counts once per episode");
            // A filled hip slot the plan does not admit: plan.
            w->inv_handles[7 + 0] = (6u << 16) | (343u << 5) | 1u;
            Put<std::uint16_t>(row1 + c::equip_row_slots, 343);
            Check(slot_run(0, 31) && hs.native.slot_last == hk::SlotRefusal::plan && hs.native.slot_writes == 1 &&
                      (Get<std::uint8_t>(row1 + c::equip_row_nibble) & 0xFu) == 2, "W4: a filled slot the plan does not admit: refused");
            // Handle 0 (a slot body 1 never had, the runtime shape) is empty even when the plan admits the slot.
            w->inv_handles[7 + 0] = 0u;
            Put<std::uint16_t>(row1 + c::equip_row_slots, 0);
            a.slot_ok = 0x7;
            hk::PublishArm(a);
            Check(slot_run(0, 4) && hs.native.slot_last == hk::SlotRefusal::empty && hs.native.slot_writes == 1 &&
                      (Get<std::uint8_t>(row1 + c::equip_row_nibble) & 0xFu) == 2, "W4: an empty slot (handle 0): refused");
            w->inv_handles[7 + 0] = 0xFFFF0000u;
            Check(slot_run(0, 41) && hs.native.slot_last == hk::SlotRefusal::empty && hs.native.slot_writes == 1,
                  "W4: an empty slot (0xFFFF0000): refused");
            Put<std::uint32_t>(Addr(gun_rec) + c::gun_flags, c::gun3c0_switch);
            Check(slot_run(1, 5) && hs.native.slot_last == hk::SlotRefusal::wait && hs.native.slot_writes == 1, "W4: mid-switch: waits");
            Put<std::uint32_t>(Addr(gun_rec) + c::gun_flags, 0);
            a.slot_equip[1] = 47;
            hk::PublishArm(a);
            Check(slot_run(1, 6) && hs.native.slot_last == hk::SlotRefusal::mismatch, "W4: the handle is not the plan's weapon: refused");
            a.slot_equip[1] = 46;
            hk::PublishArm(a);
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_slot_src, 3);
            Check(slot_run(1, 7) && hs.native.slot_last == hk::SlotRefusal::mode, "W4: rec+0x3DE != 7: refused");
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_slot_src, c::gun_slot_follow);
            Put<std::uintptr_t>(Addr(w->store_x), base + 0x40);
            Check(slot_run(1, 8) && hs.native.slot_last == hk::SlotRefusal::chain, "W4: another controller class: refused");
            Put<std::uintptr_t>(Addr(w->store_x), base + c::equip_ctl_vt_rva);
            // The call variant refuses while the controller's slot +0x10 is not SetCurrentSlot (never called
            // here: the fixture image holds no executable code at 0x11AFA80).
            a.slot_follow = mgscoop::PartnerNativeSlot::call;
            hk::PublishArm(a);
            Put<std::uintptr_t>(base + c::equip_ctl_vt_rva + 0x10, base + 0x50);
            Check(slot_run(1, 9) && hs.native.slot_last == hk::SlotRefusal::chain && hs.native.slot_writes == 1 &&
                      (Get<std::uint8_t>(row1 + c::equip_row_nibble) & 0xFu) == 2, "W4: call without the pinned slot: refused, nothing called");
            Put<std::uintptr_t>(base + c::equip_ctl_vt_rva + 0x10, base + c::set_current_slot_rva);
            a.slot_follow = mgscoop::PartnerNativeSlot::write;
            hk::PublishArm(a);
            Check(slot_run(1, 10) && hs.native.slot_writes == 2 && (Get<std::uint8_t>(row1 + c::equip_row_nibble) & 0xFu) == 1 &&
                      Get<std::uint16_t>(row1 + c::equip_row_current) == 46, "W4: back to the back slot");
            g_hook_sample.packet.version = 4;
            Check(slot_run(2, 11) && hs.native.slot_last == hk::SlotRefusal::no_v5 && hs.native.slot_writes == 2, "W4: no v5: nothing");
            a.slot_follow = mgscoop::PartnerNativeSlot::off;
            a.slot_ok = 0;
            hk::PublishArm(a);
            // Restore the mirror's store (stride 2) and Q.
            Put<std::uintptr_t>(P + c::p_q, Addr(w->q));
            Put<std::uint32_t>(Addr(w->store_y) + c::store_stride, 2);
            Put<std::uint32_t>(Addr(w->store_y) + c::store_total, 4);
            Put<std::uintptr_t>(Addr(w->store_y) + c::store_types, Addr(w->store_types));
            Put<std::uintptr_t>(Addr(w->store_y) + c::store_rows, Addr(w->store_rows));
            Put<std::uint8_t>(Addr(gun_rec) + c::gun_slot, 0);
            g_hook_sample.packet.gun_flags = 0;
            g_hook_sample.packet.inv_slot = 0;
            g_hook_sample.packet.switch_seq = 0;
            // The state the leak checks below start from (the F2 block left a full magazine of 5).
            Put<std::uint16_t>(Addr(gun_rec) + c::gun_mag, 5);
            w->store_rows[2 * 3] = 5;
        }
        const std::uint32_t writes_after_store = hs.native.mag_writes;
        // A v3 sample carries no magazine: nothing happens.
        g_hook_sample.packet.version = 3;
        g_hook_sample.packet.xflags = 0;
        g_hook_sample.packet.mag = g_hook_sample.packet.mag_cap = 0;
        Check(run() && hs.native.mag_writes == writes_after_store && hs.native.mag_last == hk::MagRefusal::no_v4, "v3: no_v4, no write");
        // Leak and reload evidence around the job.
        const std::uint32_t leaks = hs.native.shot_leaks, reloads = hs.native.reload_drains;
        const std::uint32_t ammo_leaks = hs.native.ammo_leaks;
        Check(run() && hs.native.shot_leaks == leaks && hs.native.shot_count == 7, "the viewer's shoot count is read, unchanged");
        Check(hs.native.ammo_leaks == ammo_leaks && hs.native.ammo_digest != 0, "the viewer's weapon/ammo vars block is read, unchanged");
        const std::uint32_t digest = hs.native.ammo_digest;
        const std::uint32_t benign = hs.native.ammo_benign, mission_ammo = hs.native.ammo_leaks_mission;
        s_item = true;
        Check(run() && hs.native.ammo_leaks == ammo_leaks && hs.native.ammo_leaks_mission == mission_ammo &&
                  hs.native.ammo_benign == benign + 1 && hs.native.ammo_digest != digest && !hs.native.mag_leak_off,
              "item 3: a job that changes only the viewer's item stock is benign: no ammo leak, the mirror stays on");
        s_item = false;
        s_ammo = true;
        Check(run() && hs.native.ammo_leaks == ammo_leaks + 1 && hs.native.ammo_leaks_mission == mission_ammo + 1 &&
                  hs.native.ammo_digest != digest && hs.native.shot_leaks == leaks,
              "a job that changes the viewer's magazine (ammoInWeapons) is an ammo leak, counted for the mission too");
        s_ammo = false;
        s_item = s_ammo = true;
        Check(run() && hs.native.ammo_leaks == ammo_leaks + 2 && hs.native.ammo_benign == benign + 1,
              "an item change in the same job never masks an ammo leak");
        s_item = s_ammo = false;
        s_shoot = true;
        s_reload = true;
        Check(run() && hs.native.shot_leaks == leaks + 1 && hs.native.shot_count == 8 && hs.native.reload_drains == reloads + 1,
              "a job that bumps vars.playerShootCountInMission is a leak; the reload request bit is counted");
        s_shoot = s_reload = false;
        // The ammo leak (under a mirror Arm) turned the mirror off until MissionStart.
        g_hook_sample.packet.version = 4;
        g_hook_sample.packet.xflags = pi::XFlag::Mag;
        g_hook_sample.packet.mag = 14;
        g_hook_sample.packet.mag_cap = 30;
        Check(run() && hs.native.mag_leak_off && hs.native.mag_writes == writes_after_store && hs.native.mag_last == hk::MagRefusal::leak &&
                  Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 5,
              "after an ammo leak the mirror refuses (leak), nothing written");
        hk::ResetFault();  // MissionStart
        Check(!hk::NativeArmLatchedOff(), "MissionStart (ResetFault) clears the WS4 v1 arm latch");
        Check(run() && !hs.native.mag_leak_off && hs.native.mag_writes == writes_after_store + 1 &&
                  Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 3 && StoreWord(w, 0) == 3,
              "MissionStart re-enables the mirror (14/30 -> 3 of 5)");
        const std::uint32_t writes2 = hs.native.mag_writes;
        // The mirror is off in an Arm without it.
        a.mag_mirror = false;
        a.gun_prim = a.gun_rec = 0;
        hk::PublishArm(a);
        g_hook_sample.packet.mag = 1;
        Check(run() && hs.native.mag_writes == writes2 && hs.native.mag_last == hk::MagRefusal::off, "mirror off: no write");
        Put<std::uintptr_t>(base + c::vars_slot_rva, 0);
        g_hook_sample.packet = pi::InputPacket{};
        g_hook_sample.packet.version = 3;
        g_hook_sample.packet.held = kRemoteHeld;
    }
    // F1 (docs/native-puppet.md "Weapons"): a drain window points the src
    // sample rings (the trigger's history path, the right-stick history) at
    // instance 1's own: count 1, entry 0 the injected held word, the older
    // entries 0 for a keyboard remote or the previous drain's held word for a
    // gamepad remote; everything restored after the job.
    {
        alignas(16) static std::uint32_t host_held[120];
        alignas(16) static float host_f[6][120];
        for (auto& v : host_held) v = 0x10u;  // the host's FIRE in every sample (a gamepad host)
        for (auto& r : host_f) for (auto& v : r) v = 0.5f;
        const std::uintptr_t src = Addr(w->src);
        Put<std::uintptr_t>(src + c::src_rings, Addr(host_held));
        for (unsigned r = 0; r < 6; ++r) Put<std::uintptr_t>(src + c::src_rings + 8 + 8 * r, Addr(host_f[r]));
        Put<std::uint32_t>(src + c::src_ring_cap, 120);
        Put<std::uint32_t>(src + c::src_ring_new, 3);
        static std::uintptr_t s_ring = 0;
        static std::uint32_t s_count = 0, s_e0 = 0, s_e1 = 0;
        static float s_x1 = -1.0f;
        struct RingJob {
            static void Job(void*, const void*) noexcept {
                s_ring = Get<std::uintptr_t>(g_watch_src + c::src_rings);
                s_count = Get<std::uint32_t>(g_watch_src + c::src_ring_new);
                s_e0 = Get<std::uint32_t>(s_ring);
                s_e1 = Get<std::uint32_t>(s_ring + 4);
                s_x1 = Get<float>(Get<std::uintptr_t>(g_watch_src + c::src_rings + 16));
            }
        };
        a.mag_mirror = false;
        a.gun_prim = a.gun_rec = 0;
        hk::PublishArm(a);
        g_hook_sample.packet = pi::InputPacket{};
        g_hook_sample.packet.version = 4;
        g_hook_sample.packet.held = kRemoteHeld;
        pd::JobParam param{1, 0, 0, 8, g_world.bits};
        const auto drain = [&](pi::Consumer consumer) {
            dispatch(w->root1, 8);
            const bool windowed = hk::RunJobWindow(w->go, &param, 1, &RingJob::Job, consumer);
            hk::DrainStats(hs);
            return windowed;
        };
        const std::uint32_t ring_windows = hs.native.ring_windows;
        Check(drain(pi::Consumer::drain0) && s_ring != Addr(host_held) && s_count == 1 && s_e0 == kRemoteHeld && s_e1 == 0 && s_x1 == 0.0f &&
                  hs.native.ring_on && hs.native.ring_windows == ring_windows + 1,
              "F1: the job reads instance 1's own rings (count 1, the injected level, keyboard: older entry 0)");
        Check(Get<std::uintptr_t>(src + c::src_rings) == Addr(host_held) && Get<std::uint32_t>(src + c::src_ring_new) == 3 &&
                  Get<std::uintptr_t>(src + c::src_rings + 48) == Addr(host_f[5]), "F1: the host's rings and count restored after the job");
        g_hook_sample.packet.xflags = pi::XFlag::Gamepad;
        Check(drain(pi::Consumer::drain0) && s_e0 == kRemoteHeld && s_e1 == kRemoteHeld,
              "F1: a gamepad remote's older entry is this consumer's previous drain (a held FIRE never re-fires a semi-auto)");
        Check(drain(pi::Consumer::drain1) && s_e1 == 0, "F1: per consumer (drain1 has no previous held yet)");
        Put<std::uint32_t>(src + c::src_ring_cap, 200);
        Check(drain(pi::Consumer::drain0) && s_ring == Addr(host_held) && s_count == 3 && !hs.native.ring_on &&
                  hs.native.ring_last == hk::RingRefusal::cap, "F1: a capacity outside 2..128 refuses (nothing swapped)");
        Put<std::uintptr_t>(src + c::src_rings, 0);
        for (unsigned r = 0; r < 6; ++r) Put<std::uintptr_t>(src + c::src_rings + 8 + 8 * r, 0);
        Put<std::uint32_t>(src + c::src_ring_cap, 0);
        Put<std::uint32_t>(src + c::src_ring_new, 0);
        g_hook_sample.packet = pi::InputPacket{};
        g_hook_sample.packet.version = 3;
        g_hook_sample.packet.held = kRemoteHeld;
    }
    // Native actions and weapons together (the integrated gun stage): one Arm
    // requests both mirrors and one remote v4 sample carries both groups; in
    // block 1's drain window the rates mirror writes instance 1's rate record,
    // then the magazine mirror its gun record, both before the job reads them;
    // neither write is a shoot-count or ammo-vars leak; a disarm restores the
    // rates at the next main dispatch and leaves the magazine alone.
    {
        alignas(16) static unsigned char sp_obj[0x40], rate_recs[2 * c::rate_rec_stride], ps1_obj[0x500];
        alignas(16) static unsigned char gun_prim[0x100], gun_rec[c::gun_rec_size], vars[c::vars_ammo_end + 0x10];
        std::memset(sp_obj, 0, sizeof(sp_obj));
        std::memset(rate_recs, 0, sizeof(rate_recs));
        std::memset(ps1_obj, 0, sizeof(ps1_obj));
        std::memset(gun_prim, 0, sizeof(gun_prim));
        std::memset(gun_rec, 0, sizeof(gun_rec));
        std::memset(vars, 0, sizeof(vars));
        const std::uintptr_t sp = Addr(sp_obj), ps1 = Addr(ps1_obj);
        Put<std::uintptr_t>(sp, base + c::rate_sp_vtable_rva);
        Put<std::uint32_t>(sp + c::sp_base_id, 0);
        Put<std::uint32_t>(sp + c::sp_count, 2);
        Put<std::uintptr_t>(sp + c::sp_records, Addr(rate_recs));
        Put<std::uintptr_t>(ps1, base + c::ps_vtable_alt_rva);
        Put<std::uintptr_t>(ps1 + c::ps_blk, blk1);
        Put<std::uintptr_t>(ps1 + c::ps_rates, sp);
        Put<std::uintptr_t>(blk1 + c::blk_ps, ps1);
        const std::uintptr_t r0 = Addr(rate_recs), rr = Addr(rate_recs) + c::rate_rec_stride;
        Put<std::uintptr_t>(r0, base + c::rate_rec_vtable_rva);
        Put<std::uintptr_t>(rr, base + c::rate_rec_vtable_rva);
        const float game[6] = {1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f};  // crawl, crawl factor, crawl rate, dash, dash factor, dash rate
        const float own0[6] = {1.2f, 1.0f, 1.2f, 1.2f, 1.0f, 1.2f};
        std::memcpy(reinterpret_cast<void*>(rr + c::rate_first), game, sizeof(game));
        std::memcpy(reinterpret_cast<void*>(r0 + c::rate_first), own0, sizeof(own0));
        Put<std::uintptr_t>(Addr(gun_prim), base + c::gun_prim_vt_rva);
        Put<std::uintptr_t>(Addr(gun_prim) + c::prim_blk, blk1);
        Put<std::uintptr_t>(Addr(gun_prim) + c::prim_gun_rec, Addr(gun_rec));
        Put<std::uint32_t>(Addr(gun_rec) + c::gun_flags27c, 1u << 6);  // class 1
        Put<std::uint16_t>(Addr(gun_rec) + c::gun_cap, 5);
        Put<std::uintptr_t>(base + c::vars_slot_rva, Addr(vars));
        Put<std::uint32_t>(Addr(vars) + c::vars_shoot_count, 7);
        static std::uintptr_t s_rate_rec = 0, s_gun_rec = 0;
        static float s_dash = 0.0f;
        static std::uint16_t s_mag = 0;
        s_rate_rec = rr;
        s_gun_rec = Addr(gun_rec);
        struct BothJob {
            static void Job(void*, const void*) noexcept {
                s_dash = Get<float>(s_rate_rec + 0x48);
                s_mag = Get<std::uint16_t>(s_gun_rec + c::gun_mag);
            }
        };
        a.naim = mgscoop::PartnerNativeAim::off;
        a.ps1 = 0;
        a.gen = 10;
        a.rates_mirror = true;
        a.rates_sp = sp;
        a.rates_rec = rr;
        a.rates_id = 1;
        a.rates_epoch = 6;
        a.rates_remote[0] = 1200; a.rates_remote[1] = 1000; a.rates_remote[2] = 1200;
        a.mag_mirror = true;
        a.gun_prim = Addr(gun_prim);
        a.gun_rec = Addr(gun_rec);
        hk::PublishArm(a);
        g_hook_sample.packet = pi::InputPacket{};
        g_hook_sample.packet.version = 4;
        g_hook_sample.packet.flags = pi::Flag::Valid;
        g_hook_sample.packet.xflags = static_cast<std::uint8_t>(pi::XFlag::Rates | pi::XFlag::Mag);
        g_hook_sample.packet.rate_dash = 1200;
        g_hook_sample.packet.rate_crawl = 1000;
        g_hook_sample.packet.rate_crawl_factor = 1200;
        g_hook_sample.packet.mag = 20;
        g_hook_sample.packet.mag_cap = 30;
        g_hook_sample.packet.held = kRemoteHeld;
        Check(pi::ValidPacket(g_hook_sample.packet), "both: the remote's v4 sample (rates and magazine) is valid");
        hk::DrainStats(hs);
        const std::uint32_t rates_writes = hs.native.rates_writes, rates_restores = hs.native.rates_restores;
        const std::uint32_t mag_writes = hs.native.mag_writes, shot_leaks = hs.native.shot_leaks, ammo_leaks = hs.native.ammo_leaks;
        dispatch(w->root1, 8);
        pd::JobParam param{1, 0, 0, 8, g_world.bits};
        Check(hk::RunJobWindow(w->go, &param, 1, &BothJob::Job, pi::Consumer::drain0), "both: drain window ran");
        hk::DrainStats(hs);
        Check(std::fabs(s_dash - 1.2f) < 1e-6f && s_mag == 4 && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 4 &&
                  std::fabs(Get<float>(rr + 0x50) - 0.6f) < 1e-6f &&
                  std::memcmp(reinterpret_cast<const void*>(r0 + c::rate_first), own0, sizeof(own0)) == 0,
              "both: the job sees the remote's dash rate and the mapped magazine (20/30 -> 4 of 5); instance 0's record untouched");
        Check(hs.native.rates_applied && !hs.native.rates_latched && hs.native.rates_writes == rates_writes + 1 &&
                  hs.native.mag_writes == mag_writes + 1 && hs.native.mag_last == hk::MagRefusal::none && !hs.native.mag_leak_off,
              "both: one rates write and one magazine write in the same window");
        Check(hs.native.shot_leaks == shot_leaks && hs.native.ammo_leaks == ammo_leaks && hs.native.shot_count == 7,
              "both: neither mirror is a shoot-count or ammo-vars leak");
        hk::PublishArm(pi::Arm{});
        dispatch(w->root0, 8);
        hk::DrainStats(hs);
        Check(!hs.native.rates_applied && hs.native.rates_restores == rates_restores + 1 && Get<float>(rr + 0x48) == 1.0f &&
                  Get<float>(rr + 0x50) == 0.5f && Get<std::uint16_t>(Addr(gun_rec) + c::gun_mag) == 4 && hs.native.mag_writes == mag_writes + 1,
              "both: the disarm restores the rates at the main dispatch, the magazine stays");
        Put<std::uintptr_t>(blk1 + c::blk_ps, 0);
        Put<std::uintptr_t>(base + c::vars_slot_rva, 0);
        a.rates_mirror = false;
        a.rates_sp = a.rates_rec = 0;
        a.rates_id = 0;
        a.mag_mirror = false;
        a.gun_prim = a.gun_rec = 0;
        g_hook_sample.packet = pi::InputPacket{};
        g_hook_sample.packet.version = 3;
        g_hook_sample.packet.held = kRemoteHeld;
    }
    // Required native-pose jobs use the actual atomic writer, C0 snapshot and
    // existing full pad/camera Post. Synthetic callbacks add no native calls.
    // A refused Before or invalid Arm must never run the legacy original.
    {
        const auto oldArm=a;
        alignas(16) static unsigned char c0[0xc0]{}, ps[0x500]{}, cs[0x40]{}, entries[2*0xc0]{};
        const auto iface=Addr(c0)+0x20;
        Put<std::uintptr_t>(Addr(c0),base+0x23c74c8);
        Put<std::uintptr_t>(iface,base+0x23c7500);
        Put<std::uintptr_t>(iface+8,P);
        Put<std::uint8_t>(iface+0x6d,9);
        Put<std::uintptr_t>(base+0x23c7500+0xf8,base+0x11f6520);
        Put<std::uintptr_t>(P+0xc0,iface);
        Put<std::uintptr_t>(P+0xa8,0);
        Put<std::uint32_t>(P+0x204,1);
        Put<std::uint32_t>(P+0x208,2);
        Put<std::uint32_t>(P+0x20c,2);
        Put<std::uint32_t>(P+0x214,0);
        Put<std::uint32_t>(P+c::p_local_id,0);
        Put<std::uintptr_t>(Addr(ps),base+c::ps_vtable_alt_rva);
        Put<std::uintptr_t>(Addr(ps)+0x138,0xdecafbad); // deliberately not a Player pointer
        Put<std::uintptr_t>(Addr(ps)+c::ps_blk,blk1);
        Put<std::uintptr_t>(blk1+0x138,P);
        Put<std::uintptr_t>(blk1,base+c::block_vtable_rva);
        Put<std::uintptr_t>(base+c::block_vtable_rva+0xf8,base+0x9cc850);
        Put<std::uintptr_t>(blk1+c::blk_ps,Addr(ps));
        Put<std::uintptr_t>(Addr(ps)+0x238,Addr(cs));
        Put<std::uintptr_t>(blk1+0x10,Addr(cs));
        Put<std::uintptr_t>(Addr(cs),base+0x22e6070);
        Put<std::uint32_t>(Addr(cs)+8,2);
        Put<std::uint32_t>(Addr(cs)+0xc,0);
        Put<std::uintptr_t>(Addr(cs)+0x10,Addr(entries));
        a.ps1=Addr(ps);a.rates_epoch=77;a.p218=false;
        a.rates_mirror=false;a.mag_mirror=false;a.naim=mgscoop::PartnerNativeAim::off;
        a.slot_follow=mgscoop::PartnerNativeSlot::off;
        static std::uintptr_t s_iface=0,s_cam=0,s_P=0,s_cs=0,s_entry=0;
        static unsigned s_before=0,s_jobs=0,s_after=0,s_closed=0,s_refusals=0;
        static bool s_beforeAccept=true,s_setHeld=false,s_clear=false,s_closedGood=false,s_closedHeld=false,s_nestedForeign=false;
        static bool s_beforeOwned=false,s_beforeSelected=false,s_afterOwned=false,s_closedEven=false,s_zero=false;
        s_iface=iface;s_cam=Addr(w->cam);s_P=P;s_cs=Addr(cs);s_entry=Addr(entries)+0xc0;
        struct PoseFixture {
            static void Refuse(const char*) noexcept {++s_refusals;}
            static bool Before(const hk::NativePoseJobContext& context) noexcept {
                ++s_before;
                s_beforeOwned=hk::NativePoseOwnedWindow()&&(context.sequence&1u)&&
                    context.player==s_P&&context.pp==g_watch_pp&&context.source==g_watch_src&&context.windowPPVT==Get<std::uintptr_t>(g_watch_pp)&&context.modelReady&&context.modelCS==s_cs&&context.modelEntry==s_entry;
                s_beforeSelected=context.selectedBodyOwned&&context.selectedBody==1&&
                    Get<std::uint32_t>(s_P+c::p_local_id)==1;
                Put<std::uint8_t>(s_iface+0x6d,4);
                Put<std::uint8_t>(s_cam+pi::kCamRanges[0].begin,0x55);
                if(s_setHeld&&!hk::SetNativePoseLeaseHeld())return false;
                if(s_nestedForeign){
                    pd::JobParam foreign{0,0,0,8,g_world.bits};
                    reinterpret_cast<pd::detail::JobFn>(pd::detail::W50Entry())(reinterpret_cast<void*>(context.go),&foreign);
                }
                return s_beforeAccept;
            }
            static void Job(void*,const void*) noexcept {
                ++s_jobs;s_zero=Get<std::uint32_t>(g_watch_src+0x48)==0&&Get<float>(g_watch_pp+0x10)==0.f;
            }
            static bool After(const hk::NativePoseJobContext&) noexcept {
                ++s_after;s_afterOwned=hk::NativePoseOwnedWindow();
                return !s_clear||hk::RequestNativePoseLeaseClear();
            }
            static void Closed(const hk::NativePoseJobContext& context,bool clean) noexcept {
                ++s_closed;s_closedGood=clean&&!hk::NativePoseOwnedWindow()&&Get<std::uint8_t>(s_iface+0x6d)==9&&
                    Get<std::uint32_t>(g_watch_src+0x48)==kHostHeld&&
                    Get<std::uint32_t>(s_P+c::p_local_id)==0;
                s_closedEven=!(context.sequence&1u);s_closedHeld=hk::NativePoseWindowStatus().held;
            }
        };
        hk::detail::BindNativePoseForTest({true,&PoseFixture::Refuse});
        hk::RegisterNativePoseJobHooks(&PoseFixture::Before,&PoseFixture::After,&PoseFixture::Closed);
        hk::PublishArm(a);dispatch(w->root1,8);
        Check(!hk::SetNativePoseWindowTest(true)&&!hk::NativePoseWindowStatus().required,
              "pose: current typed actor without completed post-install birth receipt cannot opt in");
        Check(hk::detail::CompleteNativePoseBirthForTest(a),"pose: typed birth uses PS408/block138 ownership despite unrelated PS138");
        Check(hk::SetNativePoseWindowTest(true)&&hk::NativePoseWindowStatus().required,
              "pose: explicit synthetic opt-in binds current owner before any native lease");
        pd::JobParam param{1,0,0,8,g_world.bits};
        ++a.gen;hk::PublishArm(a);dispatch(w->root1,8);
        Check(hk::NativePoseWindowStatus().required&&!hk::NativePoseWindowStatus().faulted&&
              !hk::NativePoseOwnedWindow()&&hk::NativePoseWindowStatus().born1,
              "pose: inactive same-birth same-scene config generation adopts only under exclusive admission");
        const auto initialCam=Get<std::uint8_t>(Addr(w->cam)+pi::kCamRanges[0].begin);
        s_before=s_jobs=s_after=s_closed=s_refusals=0;s_setHeld=true;s_clear=false;
        auto outcome=hk::RunJobWindowOutcome(w->go,&param,1,&PoseFixture::Job,pi::Consumer::drain0);
        Check(outcome==hk::JobWindowOutcome::windowed&&s_before==1&&s_jobs==1&&s_after==1&&s_closed==1&&
              s_beforeOwned&&s_beforeSelected&&s_afterOwned&&s_closedGood&&s_closedEven&&s_closedHeld&&s_refusals==0,
              "pose: Before/Job/After run under real odd writer; Closed receives successful cleanup and retains live lease");
        Check(Get<std::uint8_t>(Addr(w->cam)+pi::kCamRanges[0].begin)==initialCam&&
              Get<std::uint32_t>(rec1+c::rec_local)==0x4u,
              "pose: camera and base-local record restore before Closed");
        Check(!hk::SetNativePoseWindowTest(false)&&!hk::detail::ResetNativePoseForTest(),
              "pose: neither off nor fixture reset may discard a held native lease");
        if(g_pose_held_gen_fixture){
            const auto jobs=s_jobs;++a.gen;hk::PublishArm(a);
            outcome=hk::RunJobWindowOutcome(w->go,&param,1,&PoseFixture::Job,pi::Consumer::drain0);
            Check(outcome==hk::JobWindowOutcome::suppressed&&s_jobs==jobs&&hk::NativePoseWindowStatus().held&&
                  hk::NativePoseWindowStatus().required&&hk::NativePoseWindowStatus().faulted&&
                  !hk::detail::ResetNativePoseForTest(),
                  "pose heldgen: generation drift never rebinds or releases a live feature");
            std::printf("partner_defer held-generation checks=%u\n",checks);std::exit(0);
        }
        // Held phase has a remote-only neutral injection; release request is
        // pending until cleanup, so this job itself must still see neutral.
        s_setHeld=false;s_clear=true;s_zero=false;
        dispatch(w->root1,8);
        outcome=hk::RunJobWindowOutcome(w->go,&param,1,&PoseFixture::Job,pi::Consumer::drain0);
        Check(outcome==hk::JobWindowOutcome::windowed&&s_zero&&s_closedGood&&s_closedEven&&!s_closedHeld&&
              !hk::NativePoseWindowStatus().held,
              "pose: held job reads neutral; deferred clear takes effect only after Post+C0 restore+EndWrite");
        // Before may reject after a presentation setter; SEH-finally still
        // restores both presentation request and host pad without Job/After.
        s_clear=false;s_beforeAccept=false;
        const auto jobs=s_jobs,after=s_after,closed=s_closed;
        outcome=hk::RunJobWindowOutcome(w->go,&param,1,&PoseFixture::Job,pi::Consumer::drain0);
        Check(outcome==hk::JobWindowOutcome::suppressed&&s_jobs==jobs&&s_after==after&&s_closed==closed+1&&
              s_closedGood&&s_closedEven&&!hk::NativePoseOwnedWindow(),
              "pose: refused Before never falls through to original; successful finally reports Closed");
        s_beforeAccept=true;
        // Typed C0 refusal happens before Pre and preserves the ordinary job.
        Put<std::uintptr_t>(iface,0);
        const auto before=s_before;
        outcome=hk::RunJobWindowOutcome(w->go,&param,1,&PoseFixture::Job,pi::Consumer::drain0);
        Check(outcome==hk::JobWindowOutcome::suppressed&&s_before==before&&s_jobs==jobs&&
              Get<std::uint32_t>(g_watch_src+0x48)==kHostHeld,
              "pose: typed C0 refusal suppresses mandatory job before any pad swap");
        Put<std::uintptr_t>(iface,base+0x23c7500);
        // Ordinary block0 jobs still forward once under a shared reader.
        outcome=hk::RunJobWindowOutcome(w->go,&param,0,&PoseFixture::Job,pi::Consumer::drain0);
        Check(outcome==hk::JobWindowOutcome::legacy&&s_jobs==jobs+1&&
              Get<std::uint32_t>(g_watch_src+0x48)==kHostHeld,
              "pose: ordinary block0 refusal preserves legacy original exactly once");
        if(g_pose_nested_fixture){
            const auto count=TakeCalls().size();(void)count;s_nestedForeign=true;
            const auto originalJobs=s_jobs;
            outcome=hk::RunJobWindowOutcome(w->go,&param,1,&PoseFixture::Job,pi::Consumer::drain0);
            Check(outcome==hk::JobWindowOutcome::suppressed&&s_jobs==originalJobs&&TakeCalls().empty()&&s_refusals>0&&
                  hk::NativePoseWindowStatus().faulted&&hk::NativePoseWindowStatus().required&&
                  !s_closedGood&&!hk::detail::ResetNativePoseForTest(),
                  "pose nested: body0 W50 never executes inside exclusive body1 writer; cleanup is poisoned and retained");
            Check(Get<std::uint8_t>(iface+0x6d)==9&&Get<std::uint32_t>(g_watch_src+0x48)==kHostHeld&&
                  Get<std::uint32_t>(P+c::p_local_id)==0,
                  "pose nested: SEH finally still restores C0/pad/selected body before private abort");
            std::printf("partner_defer nested-foreign checks=%u\n",checks);
            std::exit(0); // retained fatal writer belongs to this fixture process
        }
        // Same P/block/config can still hide a model/PS replacement.
        // No fresh constructor receipt means required must refuse before Pre.
        alignas(16) static unsigned char replacedPS[0x500]{};
        std::memcpy(replacedPS,ps,sizeof(ps));a.ps1=Addr(replacedPS);
        Put<std::uintptr_t>(blk1+c::blk_ps,a.ps1);hk::PublishArm(a);
        const auto jobsBeforeRebind=s_jobs;
        outcome=hk::RunJobWindowOutcome(w->go,&param,1,&PoseFixture::Job,pi::Consumer::drain0);
        Check(outcome==hk::JobWindowOutcome::suppressed&&s_jobs==jobsBeforeRebind&&
              hk::NativePoseWindowStatus().faulted&&hk::NativePoseWindowStatus().required&&
              !hk::NativePoseOwnedWindow(),
              "pose: post-optin unheld PS rebind without fresh same-serial birth suppresses before original");
        a.ps1=Addr(ps);Put<std::uintptr_t>(blk1+c::blk_ps,a.ps1);
        // Invalidated generation must keep the requirement and suppress,
        // rather than use its stale retained pointer in a legacy update.
        ++a.gen;hk::PublishArm(a);
        const auto afterLegacy=s_jobs;
        outcome=hk::RunJobWindowOutcome(w->go,&param,1,&PoseFixture::Job,pi::Consumer::drain0);
        Check(outcome==hk::JobWindowOutcome::suppressed&&s_jobs==afterLegacy&&s_refusals>0&&
              hk::NativePoseWindowStatus().required&&hk::NativePoseWindowStatus().faulted,
              "pose: generation invalidation latches private fault and never forwards unwrapped native pose");
        Check(hk::detail::ResetNativePoseForTest(),"pose: inactive unheld synthetic reset preserves atomic domain");
        hk::detail::BindNativePoseForTest({});hk::RegisterNativePoseJobHooks(nullptr,nullptr,nullptr);
        a=oldArm;Put<std::uintptr_t>(blk1+c::blk_ps,0);
        Put<std::uintptr_t>(P+0xc0,0);Put<std::uint32_t>(P+0x204,0);
        hk::PublishArm(a);
    }
    // F3 (docs/native-puppet.md "Traversal icon"): the padop vtable copy in the real drain window. With a pending,
    // unexpired icon request in the Arm, block 1's job sees P's padop through the copy (body 1's ladder query runs the
    // plugin's own Exec for idx 1, once per request id; its step-on force is swallowed); the image vtable is back
    // after the window; no request, an expired request or a padop of another P never swap.
    {
        namespace ic = mgscoop::partner_icon;
        struct IconWorld {
            alignas(16) unsigned char padop[0x3F30];
            alignas(16) unsigned char res[0x60];
            std::uintptr_t vt_ladder[2];
            std::uintptr_t ladder1[2];  // {vtable, block}
        };
        static IconWorld* iw = new IconWorld();
        std::memset(iw, 0, sizeof(*iw));
        static std::uintptr_t s_vptr = 0;
        static bool s_result = false;
        static unsigned s_exec = 0, s_force_orig = 0, s_completed_orig = 0;
        struct IconJob {
            static std::uint8_t Prio(void*, std::uint32_t, std::uint32_t kt) noexcept { return kt == 0 ? 6 : 0; }
            static bool Exec(void*, std::uint32_t idx, std::uint32_t kt, void*) noexcept {
                ++s_exec;
                return idx == 1 && kt == 0;
            }
            static bool Completed(void*, void*, std::int32_t, void*) noexcept {
                ++s_completed_orig;
                return false;
            }
            static void Force(void*, std::int32_t, bool, void*) noexcept { ++s_force_orig; }
            // Block 1's job: the polls call [P+0xD0] vt+0x28 (and the step-on vt+0x50 after a completion).
            static void Job(void*, const void*) noexcept {
                s_vptr = Get<std::uintptr_t>(Addr(iw->padop));
                s_result = false;
                if (s_vptr != ic::CopyTable()) return;  // the synthetic image's own slots are not callable
                using CompletedFn = bool (*)(void*, void*, std::int32_t, void*);
                using ForceFn = void (*)(void*, std::int32_t, bool, void*);
                s_result = reinterpret_cast<CompletedFn>(Get<std::uintptr_t>(s_vptr + ic::kSlotCompleted))(iw->padop, iw->ladder1, 0, iw->res);
                if (s_result) reinterpret_cast<ForceFn>(Get<std::uintptr_t>(s_vptr + ic::kSlotForce))(iw->padop, 0, true, iw->res);
            }
        };
        const std::uintptr_t padop = Addr(iw->padop);
        const std::uintptr_t saved_padop = Get<std::uintptr_t>(P + ic::kPPadop);
        const std::uint32_t saved_flags = Get<std::uint32_t>(P + c::p_flags);
        Put<std::uintptr_t>(P + ic::kPPadop, padop);
        Put<std::uintptr_t>(padop, base + ic::kPadopVtableRva);
        Put<std::uintptr_t>(padop + ic::kPadopOwner, P);
        Put<std::uint32_t>(P + c::p_flags, saved_flags | ic::kPFlagIcons);
        iw->vt_ladder[0] = Addr(reinterpret_cast<const void*>(&IconJob::Prio));
        iw->vt_ladder[1] = Addr(reinterpret_cast<const void*>(&IconJob::Exec));
        iw->ladder1[0] = Addr(iw->vt_ladder);
        iw->ladder1[1] = blk1;
        const std::uintptr_t exec[ic::kKinds] = {0, iw->vt_ladder[1], 0x7001, 0x7002, 0x7003};
        ic::detail::BindForTest(reinterpret_cast<void*>(base), &IconJob::Completed, &IconJob::Force, exec);
        pd::JobParam param{1, 0, 0, 8, g_world.bits};
        hk::PublishArm(a);
        g_hook_sample.packet.held = kRemoteHeld;
        dispatch(w->root1, 8);
        Check(hk::RunJobWindow(w->go, &param, 1, &IconJob::Job, pi::Consumer::drain0) && s_vptr == base + ic::kPadopVtableRva && s_exec == 0,
              "icon: no request in the Arm, the job sees the image vtable");
        pi::Arm ia = a;
        ia.icon_kind = static_cast<std::uint8_t>(ic::Kind::ladder);
        ia.icon_seq = 77;
        ia.icon_until_ms = ~0ull;
        hk::PublishArm(ia);
        hk::DrainStats(hs);
        const ic::Stats before = hs.native.icon;
        Check(hk::RunJobWindow(w->go, &param, 1, &IconJob::Job, pi::Consumer::drain0) && s_vptr == ic::CopyTable() && s_result &&
                  s_exec == 1 && s_force_orig == 0 && s_completed_orig == 0,
              "icon: the copy answers body 1's ladder query with the plugin's Exec (idx 1) and swallows its force");
        Check(Get<std::uintptr_t>(padop) == base + ic::kPadopVtableRva, "icon: the image vtable is back after the window");
        hk::DrainStats(hs);
        Check(hs.native.icon.swaps == before.swaps + 1 && hs.native.icon.windows == before.windows + 1 &&
                  hs.native.icon.granted == before.granted + 1 && hs.native.icon.granted_seq == 77 && hs.native.icon.force_swallowed == before.force_swallowed + 1,
              "icon: the drained stats count the swap, the scope and the grant");
        Check(hk::RunJobWindow(w->go, &param, 1, &IconJob::Job, pi::Consumer::drain0) && s_vptr == ic::CopyTable() && !s_result && s_exec == 1,
              "icon: the same request id is never granted twice");
        // Outside any window the copy is not installed; a call through it (no scope) goes to the original.
        using CompletedFn = bool (*)(void*, void*, std::int32_t, void*);
        Check(!reinterpret_cast<CompletedFn>(Get<std::uintptr_t>(ic::CopyTable() + ic::kSlotCompleted))(iw->padop, iw->ladder1, 0, iw->res) &&
                  s_completed_orig == 1 && s_exec == 1,
              "icon: outside the drain scope the copy forwards to the original");
        ia.icon_seq = 78;
        ia.icon_until_ms = 1;  // expired
        hk::PublishArm(ia);
        Check(hk::RunJobWindow(w->go, &param, 1, &IconJob::Job, pi::Consumer::drain0) && s_vptr == base + ic::kPadopVtableRva && s_exec == 1,
              "icon: an expired request never swaps");
        ia.icon_until_ms = ~0ull;
        Put<std::uintptr_t>(padop + ic::kPadopOwner, P + 8);
        hk::PublishArm(ia);
        hk::DrainStats(hs);
        const std::uint32_t refused = hs.native.icon.swap_refused;
        Check(hk::RunJobWindow(w->go, &param, 1, &IconJob::Job, pi::Consumer::drain0) && s_vptr == base + ic::kPadopVtableRva && s_exec == 1,
              "icon: a padop of another P never swaps");
        hk::DrainStats(hs);
        Check(hs.native.icon.swap_refused == refused + 1, "icon: the refused swap is counted");
        Put<std::uintptr_t>(padop + ic::kPadopOwner, P);
        Check(hk::RunJobWindow(w->go, &param, 1, &IconJob::Job, pi::Consumer::drain0) && s_vptr == ic::CopyTable() && s_result && s_exec == 2 &&
                  Get<std::uintptr_t>(padop) == base + ic::kPadopVtableRva,
              "icon: a new request id grants again; the image vtable is back");
        // A job that leaves a foreign vptr in the padop (the object replaced inside the window): ApplyPost
        // restores the padop first and only over the copy, so the foreign vptr stays; counted and latched.
        static std::uintptr_t s_foreign = 0;
        struct ForeignJob {
            static void Job(void* foreign_go, const void* foreign_param) noexcept {
                IconJob::Job(foreign_go, foreign_param);
                Put<std::uintptr_t>(Addr(iw->padop), s_foreign);
            }
        };
        s_foreign = base + ic::kPadopVtableRva + 0x300;
        ia.icon_seq = 79;
        hk::PublishArm(ia);
        hk::DrainStats(hs);
        const ic::Stats before_foreign = hs.native.icon;
        Check(hk::RunJobWindow(w->go, &param, 1, &ForeignJob::Job, pi::Consumer::drain0) && s_vptr == ic::CopyTable() && s_result &&
                  s_exec == 3 && Get<std::uintptr_t>(padop) == s_foreign,
              "icon: a foreign padop vptr after the job is never overwritten");
        hk::DrainStats(hs);
        Check(hs.native.icon.restore_mismatch == before_foreign.restore_mismatch + 1 && hs.native.icon.latched,
              "icon: the foreign vptr is counted and latches the mission");
        Put<std::uintptr_t>(padop, base + ic::kPadopVtableRva);
        ic::ResetMission();
        ic::detail::ResetForTest();
        Put<std::uintptr_t>(P + ic::kPPadop, saved_padop);
        Put<std::uint32_t>(P + c::p_flags, saved_flags);
        hk::PublishArm(a);
    }
    // G1 (docs/native-puppet.md "Partner HUD hints"): the hint-queue scope in the real drain window. Begin(a.P) right
    // before block 1's job opens it for P+0xD8's UiControllerImpl, so body 1's vt+0x260 request from that job is
    // swallowed. A refused job (block 0, or block 1 without an armed Arm: the legacy GuardedJobCall path), the same
    // thread after the window, and the thread after a job that unwound through GuardedCall all reach the original leaf
    // (the UI object is shared with the viewer's own update).
    {
        namespace uf = mgscoop::partner_ui_filter;
        struct UiWorld {
            alignas(16) unsigned char obj[0x400];  // the primary at obj, the UiControllerImpl (P+0xD8) at obj+0x20
        };
        static UiWorld* uw = new UiWorld();
        std::memset(uw, 0, sizeof(*uw));
        static std::uintptr_t s_ui = 0;
        static unsigned s_orig = 0, s_jobs = 0;
        struct UiJob {
            static void Original(void*, std::uint16_t, std::uint32_t, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) { ++s_orig; }
            // Body 1's ladder state: [P+0xD8] vt+0x260(ui, 3, 8, 0, 1, 0, 0) through the hooked leaf.
            static void Post(void*, const void*) noexcept {
                ++s_jobs;
                uf::detail::Detour(4)(reinterpret_cast<void*>(s_ui), 3, 8, 0, 1, 0, 0);
            }
            static void Raise(void* go, const void* param) noexcept {
                Post(go, param);
                RaiseException(kUiJobRaise, 0, 0, nullptr);
            }
        };
        const std::uintptr_t ui = Addr(uw->obj) + uf::kUiPrimary;
        s_ui = ui;
        Put<std::uintptr_t>(Addr(uw->obj), base + uf::kUiPrimaryVtableRva);
        Put<std::uintptr_t>(ui, base + uf::kUiVtableRva);
        Put<std::uintptr_t>(ui + uf::kUiOwner, P);
        const std::uintptr_t saved_ui = Get<std::uintptr_t>(P + uf::kPUi);
        Put<std::uintptr_t>(P + uf::kPUi, ui);
        const uf::detail::LeafFn originals[uf::kQueues] = {&UiJob::Original, &UiJob::Original, &UiJob::Original, &UiJob::Original,
                                                           &UiJob::Original};
        uf::detail::BindForTest(reinterpret_cast<void*>(base), originals);
        const auto post = [&] { uf::detail::Detour(4)(reinterpret_cast<void*>(ui), 3, 8, 0, 1, 0, 0); };
        pd::JobParam param{1, 0, 0, 8, g_world.bits};
        hk::PublishArm(a);
        g_hook_sample.packet.held = kRemoteHeld;
        dispatch(w->root1, 8);
        hk::DrainStats(hs);
        const uf::Stats u0 = hs.native.ui_filter;
        Check(hk::RunJobWindow(w->go, &param, 1, &UiJob::Post, pi::Consumer::drain0) && s_jobs == 1 && s_orig == 0,
              "ui filter: body 1's hint request inside block 1's drain job is swallowed");
        hk::DrainStats(hs);
        Check(hs.native.ui_filter.windows == u0.windows + 1 && hs.native.ui_filter.swallowed[4] == u0.swallowed[4] + 1 &&
                  hs.native.ui_filter.refused == u0.refused && hs.native.ui_filter.foreign == u0.foreign,
              "ui filter: one scope for the window, one vt+0x260 request swallowed");
        post();
        Check(s_orig == 1, "ui filter: after the window the same thread's request reaches the original");
        pd::JobParam param0{0, 0, 0, 8, g_world.bits};
        Check(!hk::RunJobWindow(w->go, &param0, 0, &UiJob::Post, pi::Consumer::drain0) && s_jobs == 2 && s_orig == 2,
              "ui filter: block 0's job (refused, run without a window) reaches the original");
        pi::Arm disarmed = a;
        disarmed.enabled = false;
        hk::PublishArm(disarmed);
        Check(!hk::RunJobWindow(w->go, &param, 1, &UiJob::Post, pi::Consumer::drain0) && s_jobs == 3 && s_orig == 3,
              "ui filter: a refused block-1 job (no armed Arm, the legacy call) reaches the original");
        hk::PublishArm(a);
        dispatch(w->root1, 8);
        Check(RunJobWindowCatching(w->go, &param, 1, &UiJob::Raise) && s_jobs == 4 && s_orig == 3,
              "ui filter: a job that unwinds: its request was swallowed, the exception reaches the outer handler");
        post();
        Check(s_orig == 4, "ui filter: after a job unwound through GuardedCall the scope is closed");
        Check(hk::RunJobWindow(w->go, &param, 1, &UiJob::Post, pi::Consumer::drain0) && s_jobs == 5 && s_orig == 4,
              "ui filter: the next window swallows again");
        hk::DrainStats(hs);
        Check(hs.native.ui_filter.windows == u0.windows + 3 && hs.native.ui_filter.swallowed[4] == u0.swallowed[4] + 3 &&
                  hs.native.ui_filter.faults == u0.faults && !hs.native.ui_filter.latched,
              "ui filter: three scopes, three swallowed, no fault");
        uf::detail::ResetForTest();
        Put<std::uintptr_t>(P + uf::kPUi, saved_ui);
        hk::PublishArm(a);
    }
    // The game window closes (debug_overlay WM_CLOSE -> PartnerShutdown on
    // the window thread): the thread-safe flags end every window for good
    // (last in this program: they are never cleared). Block 1's job still
    // runs once per drain, on the viewer's untouched pad, and the drain is
    // not an unwindowed native drain (no arbiter reason from a shutdown).
    {
        static std::uint32_t s_seen = 0;
        static unsigned s_runs = 0;
        struct SeenJob {
            static void Job(void*, const void*) noexcept {
                ++s_runs;
                s_seen = Get<std::uint32_t>(g_watch_src + 0x48);
            }
        };
        hk::PublishArm(a);
        g_hook_sample.packet.held = kRemoteHeld;
        dispatch(w->root1, 8);
        pd::JobParam param{1, 0, 0, 8, g_world.bits};
        Check(!pd::ShuttingDown() && hk::RunJobWindow(w->go, &param, 1, &SeenJob::Job, pi::Consumer::drain0) && s_seen == kRemoteHeld,
              "shutdown: before the window closes block 1's job runs windowed");
        hk::DrainStats(hs);
        const std::uint32_t disarmed = hs.native.drain_disarmed, refused = hs.native.drain_refused;
        pd::Shutdown();
        hk::Shutdown();
        pd::SetArmed(true);
        Check(pd::ShuttingDown() && !pd::Armed(), "shutdown: the deferral stays disarmed for good");
        s_runs = 0;
        s_seen = 0;
        dispatch(w->root1, 8);
        Check(!hk::RunJobWindow(w->go, &param, 1, &SeenJob::Job, pi::Consumer::drain0) && s_runs == 1 && s_seen == kHostHeld &&
                  Get<std::uint32_t>(Addr(w->src) + 0x48) == kHostHeld,
              "shutdown: the job runs once without a window (the viewer's pad untouched)");
        hk::DrainStats(hs);
        Check(hs.native.drain_disarmed == disarmed && hs.native.drain_refused == refused + 1,
              "shutdown: refused, never counted as an unwindowed native drain");
    }
    hk::DrainStats(hs);
    Check(hs.window.faults == 0, "no window fault in the fixture");
    g_watch_src = g_watch_pp = g_watch_rec = 0;
    g_job_go = nullptr;
    hk::PublishArm(pi::Arm{});
}

} // namespace

int main(int argc,char** argv) {
    g_pose_nested_fixture=argc==2&&std::strcmp(argv[1],"--pose-nested-foreign")==0;
    g_pose_held_gen_fixture=argc==2&&std::strcmp(argv[1],"--pose-held-gen-drift")==0;
    g_main_tid = GetCurrentThreadId();
    BuildThunk();
    TestPureSeams();
    TestDetourPure();
    TestInstall();
    TestRefusals();
    TestEndToEnd();
    TestAnomalies();
    TestWrapRules();
    TestNestingForeignUnwind();
    TestJobWindow();
    std::printf("%u partner defer checks passed: decision truth table, FindEntry bounds, patch verdicts, install on the contract "
                "image (H2 stub, H1 slot, live check, idempotent), byte/scope/slot refusals, end-to-end deferral on real threads "
                "(scope + read lock, sub-0 counters, census, disarm), dup, ret_other, inline jobs, MissionStart mid-batch, lost, "
                "gate closed, live nbits, write-lock spin, exception unwind order, foreign-thread batch, identity, nested batch, "
                "foreign batch in flight, unwound batch; hook: census counting, no window in a batch, native svc rule (an svc "
                "entry inline in a batch is a native skip, never wrap_in_batch), post-phase-0 look write; drain window with the "
                "real hook (instance 0 never sees the remote pad, instance 1's job windowed with isLocal and the phase-1 detour "
                "forced, drain telemetry, block refusals, no window in a batch, replaced aim sub-controller, private masks, rates "
                "mirror before the job, reapply, restore at the main dispatch (disarmed or legacy Arm), record-vtable latch, forget "
                "with the forgotten epoch refused, owner refusals, instance 0's record untouched, broken restore chain, magazine "
                "mirror with edges/refusals/dry fire, the shoot-count and ammo-vars leak checks, mirror off after an ammo leak, both mirrors in one window without a leak; the aim-target mirror (Q+0x10[1] and record 1 before the job, instance 0 untouched, "
                "hold, stale, range, restore when unwanted or disarmed, overwritten, chain refusals, forget/epoch, fault latch, the aimpt command, "
                "a restore through a changed chain dropped and logged, the per-epoch line budget, nothing written or restored after the window closes); "
                "window-close shutdown flags)\n", checks);
    return 0;
}
