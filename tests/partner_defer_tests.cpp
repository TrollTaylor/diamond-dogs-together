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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include "mgscoop/partner_defer.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_player.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace pd = mgscoop::partner_defer;
namespace hk = mgscoop::partner_input_hook;
namespace pi = mgscoop::partner_input;
namespace c = mgscoop::partner_player_contract;

unsigned checks = 0;
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
struct JobCall {
    std::uint32_t tid, block, sub, mode, nbits;
    int scope_depth;
    LONG lock;
    bool in_batch;
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
};
Scenario g_sc;
std::thread g_releaser;
void Job(std::uint32_t block, std::uint32_t sub, std::uint32_t mode) {
    pd::JobParam param{block, sub, mode, Get<std::uint32_t>(Addr(g_world.entry) + c::entry_nbits),
                       Get<const std::uint8_t*>(Addr(g_world.entry) + c::entry_bits)};
    g_thunk(g_world.go, &param);
}
void FakeBatch(void* group, std::uint32_t mode) noexcept {
    (void)group;
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
    for (auto& w : workers) w.join();
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
    hk::detail::SetDrainForTest(da, inj, false);
    hk::PublishArm(pi::Arm{});
    hk::PublishWrapRoots(0, 0, false);
}

} // namespace

int main() {
    g_main_tid = GetCurrentThreadId();
    BuildThunk();
    TestPureSeams();
    TestInstall();
    TestRefusals();
    TestEndToEnd();
    TestAnomalies();
    TestWrapRules();
    std::printf("%u partner defer checks passed: decision truth table, FindEntry bounds, patch verdicts, install on the contract "
                "image (H2 stub, H1 slot, live check, idempotent), byte/scope/slot refusals, end-to-end deferral on real threads "
                "(scope + read lock, sub-0 counters, census, disarm), dup, ret_other, inline jobs, MissionStart mid-batch, lost, "
                "gate closed, live nbits, write-lock spin, exception unwind order, foreign-thread batch, identity; hook: census "
                "counting, no window in a batch, native svc rule, post-phase-0 look write\n", checks);
    return 0;
}
