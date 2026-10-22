// partner_icon (F3, docs/native-puppet.md "Traversal icon"): the pure scope /
// kind / grant decisions, the census verdicts, the contract pins, and the real
// copy slots (IconCompletedBody1 / IconForceBody1) on fake objects with bound
// originals: routing by thread and block, one grant per request id, the ray
// cache restore, the viewer digest latch, faults, and the force swallow.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "mgscoop/partner_icon.hpp"
#include "mgscoop/partner_traverse.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace ic = mgscoop::partner_icon;
namespace tv = mgscoop::partner_traverse;
namespace c = mgscoop::partner_player_contract;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u: %s\n", checks, what);
        std::exit(1);
    }
}
std::uintptr_t Addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
template <class T> void Put(std::uintptr_t at, T v) { std::memcpy(reinterpret_cast<void*>(at), &v, sizeof(T)); }
template <class T> T Get(std::uintptr_t at) {
    T v;
    std::memcpy(&v, reinterpret_cast<const void*>(at), sizeof(T));
    return v;
}

void TestPure() {
    // Kinds mirror partner_traverse::Icon.
    static_assert(static_cast<int>(ic::Kind::ladder) == static_cast<int>(tv::Icon::ladder) &&
                      static_cast<int>(ic::Kind::pipe) == static_cast<int>(tv::Icon::pipe) &&
                      static_cast<int>(ic::Kind::climb) == static_cast<int>(tv::Icon::climb) &&
                      static_cast<int>(ic::Kind::step_on) == static_cast<int>(tv::Icon::step_on),
                  "partner_icon::Kind == partner_traverse::Icon");
    // Route: only in scope, on the scoped thread, for the Arm's blk1.
    Check(ic::RouteQuery(true, true, 0x5000, 0x5000) == ic::Route::body1, "scope + thread + blk1: body 1");
    Check(ic::RouteQuery(false, true, 0x5000, 0x5000) == ic::Route::original, "out of scope: original");
    Check(ic::RouteQuery(true, false, 0x5000, 0x5000) == ic::Route::original, "another thread: original");
    Check(ic::RouteQuery(true, true, 0x4000, 0x5000) == ic::Route::original, "block 0's plugin: original");
    Check(ic::RouteQuery(true, true, 0, 0) == ic::Route::original, "no blk1: original");
    // Grant checks in order.
    ic::GrantIn g{};
    g.kt = 0; g.kind = ic::Kind::ladder; g.seq = 5; g.granted_seq = 4;
    g.exec_match = g.owner_ok = g.p_flag = g.res_aligned = true;
    Check(ic::GrantCheck(g) == ic::Deny::none, "a pending ladder request: Exec");
    { auto x = g; x.kt = 1; Check(ic::GrantCheck(x) == ic::Deny::kt, "kt 1: false (stock)"); }
    { auto x = g; x.kind = ic::Kind::none; Check(ic::GrantCheck(x) == ic::Deny::no_request, "no request: false"); }
    { auto x = g; x.seq = 0; Check(ic::GrantCheck(x) == ic::Deny::no_request, "seq 0: no request"); }
    { auto x = g; x.kind = static_cast<ic::Kind>(9); Check(ic::GrantCheck(x) == ic::Deny::no_request, "unknown kind: no request"); }
    { auto x = g; x.latched = true; Check(ic::GrantCheck(x) == ic::Deny::latched, "latched: false"); }
    { auto x = g; x.granted_seq = 5; Check(ic::GrantCheck(x) == ic::Deny::granted, "granted once per seq"); }
    { auto x = g; x.exec_match = false; Check(ic::GrantCheck(x) == ic::Deny::kind, "another plugin: false"); }
    { auto x = g; x.owner_ok = false; Check(ic::GrantCheck(x) == ic::Deny::owner, "padop of another P: false"); }
    { auto x = g; x.p_flag = false; Check(ic::GrantCheck(x) == ic::Deny::p_flag, "P+0x204 bit 10 clear: false (stock gate)"); }
    { auto x = g; x.res_aligned = false; Check(ic::GrantCheck(x) == ic::Deny::align, "res not 16-byte aligned: false"); }
    { auto x = g; x.kt = 1; x.latched = true; Check(ic::GrantCheck(x) == ic::Deny::kt, "kt checked first"); }
    Check(ic::ForceSwallowed(true, true) && !ic::ForceSwallowed(true, false) && !ic::ForceSwallowed(false, true), "force: swallowed in scope only");
    // Swap: a native Arm with a pending, unexpired request of a known kind.
    Check(ic::SwapWanted(true, 1, 3, 100, 200), "swap: pending ladder");
    Check(!ic::SwapWanted(false, 1, 3, 100, 200) && !ic::SwapWanted(true, 0, 3, 100, 200) && !ic::SwapWanted(true, 5, 3, 100, 200) &&
              !ic::SwapWanted(true, 1, 0, 100, 200) && !ic::SwapWanted(true, 1, 3, 200, 200) && !ic::SwapWanted(true, 1, 3, 100, 0),
          "swap refusals: not native, no kind, unknown kind, no seq, expired, no expiry");
    // Restore: the saved vtable goes back only over our copy; already back is left; anything else is never overwritten.
    Check(ic::RestoreAction(0x5000, 0x5000, 0x9000) == ic::Restore::write, "restore: our copy, write the saved vtable");
    Check(ic::RestoreAction(0x9000, 0x5000, 0x9000) == ic::Restore::already, "restore: already back (second post), leave it");
    Check(ic::RestoreAction(0x7000, 0x5000, 0x9000) == ic::Restore::mismatch, "restore: a foreign vptr, mismatch");
    Check(ic::RestoreAction(0, 0, 0x9000) == ic::Restore::mismatch, "restore: no copy built, never write");
    // Exec RVAs by kind.
    Check(ic::KindOfExec(0x11EA140) == ic::Kind::ladder && ic::KindOfExec(0x1211820) == ic::Kind::pipe &&
              ic::KindOfExec(0x114BEC0) == ic::Kind::climb && ic::KindOfExec(0x1293F60) == ic::Kind::step_on &&
              ic::KindOfExec(0) == ic::Kind::none && ic::KindOfExec(0xAFD870) == ic::Kind::none,
          "KindOfExec");
    Check(std::strcmp(ic::KindName(ic::Kind::step_on), "step_on") == 0 && std::strcmp(ic::DenyName(ic::Deny::granted), "granted") == 0,
          "names");
}

void TestCensus() {
    ic::Census s{};
    Check(std::strcmp(ic::CensusVerdict(s), "unread") == 0, "census: unread");
    s.read = true;
    Check(std::strcmp(ic::CensusVerdict(s), "empty") == 0, "census: empty");
    const std::uintptr_t blk0 = 0x1000, blk1 = 0x2000;
    // Four traversal plugins per block, distinct objects, each with its own block; one other plugin; nulls.
    for (unsigned k = 1; k < ic::kKinds; ++k)
        ic::CensusAdd(s, 0x10000 + k, 0x20000 + k, ic::kExecRva[k], ic::kExecRva[k], blk0, blk1, blk0, blk1);
    ic::CensusAdd(s, 0x10010, 0x20010, 0x123456, 0x123456, blk0, blk1, blk0, blk1);
    ic::CensusAdd(s, 0, 0, 0, 0, 0, 0, blk0, blk1);
    Check(s.plugins == 5 && s.distinct == 5 && s.null0 == 1 && s.null1 == 1 && s.found0 == ic::kAllKinds && s.found1 == ic::kAllKinds &&
              s.blk_ok0 == ic::kAllKinds && s.blk_ok1 == ic::kAllKinds && std::strcmp(ic::CensusVerdict(s), "ok") == 0,
          "census: distinct lists, all kinds with their block: ok");
    ic::Census shared = s;
    ic::CensusAdd(shared, 0x30000, 0x30000, 0x9, 0x9, blk0, blk0, blk0, blk1);
    Check(std::strcmp(ic::CensusVerdict(shared), "shared") == 0, "census: one shared object: shared");
    ic::Census wrong{};
    wrong.read = true;
    for (unsigned k = 1; k < ic::kKinds; ++k)
        ic::CensusAdd(wrong, 0x10000 + k, 0x20000 + k, ic::kExecRva[k], ic::kExecRva[k], blk0, k == 2 ? blk0 : blk1, blk0, blk1);
    Check(wrong.blk_ok1 == (ic::kAllKinds & ~0x4u) && std::strcmp(ic::CensusVerdict(wrong), "blk1") == 0, "census: a block-1 pipe on block 0: blk1");
    ic::Census missing{};
    missing.read = true;
    ic::CensusAdd(missing, 0x10001, 0x20001, ic::kExecRva[1], ic::kExecRva[1], blk0, blk1, blk0, blk1);
    Check(std::strcmp(ic::CensusVerdict(missing), "missing0") == 0, "census: only a ladder: missing0");
}

// The generated contract carries every slot and pin the module relies on.
void TestContract() {
    const auto has_slot = [](std::uintptr_t vt, std::size_t off, std::uintptr_t target) {
        for (const auto& s : c::vtable_slots)
            if (s.vtable_rva == vt && s.offset == off && s.target_rva == target) return true;
        return false;
    };
    const auto has_site = [](std::uintptr_t rva) {
        for (const auto& s : c::code_sites)
            if (s.rva == rva) return true;
        return false;
    };
    Check(has_slot(ic::kPadopVtableRva, ic::kSlotCompleted, ic::kCompletedRva) && has_slot(ic::kPadopVtableRva, ic::kSlotForce, ic::kForceRva),
          "contract: padop +0x28 / +0x50");
    for (const std::uintptr_t vt : c::icon_plugin_vts) {
        Check(has_slot(vt, 0, ic::kPriorityRva), "contract: ActionPlugin vt[0] = priority");
        bool exec = false;
        for (unsigned k = 1; k < ic::kKinds; ++k) exec = exec || has_slot(vt, 8, ic::kExecRva[k]);
        Check(exec, "contract: ActionPlugin vt[1] = a traversal Exec");
    }
    for (unsigned k = 1; k < ic::kKinds; ++k) Check(has_site(ic::kExecRva[k]), "contract: Exec prologue pinned");
    Check(has_site(0x114DEB3) && has_site(0x11EAFF8) && has_site(0x1213DA8) && has_site(0x129BF69) && has_site(0x129C10E),
          "contract: the four poll call sites and the step-on force call");
    Check(sizeof(c::icon_bodies) / sizeof(c::icon_bodies[0]) == 3 && c::icon_bodies[0].rva == ic::kCompletedRva &&
              c::icon_bodies[0].size == 0x1AE && c::icon_bodies[1].rva == ic::kForceRva && c::icon_bodies[2].rva == ic::kPriorityRva,
          "contract: complete-body pins of the forwarded functions");
}

// --- The copy slots on fake objects --------------------------------------------
struct Plugin {
    std::uintptr_t vt;
    std::uintptr_t block;
};
alignas(16) unsigned char g_padop[0x3F30];
alignas(16) unsigned char g_P[0x300];
alignas(16) unsigned char g_Q[0x40];
alignas(16) std::uint16_t g_acts[2];
alignas(16) std::uint8_t g_upper[2];
alignas(16) unsigned char g_blk0[0x100], g_blk1[0x100];
std::uintptr_t g_vt_ladder[2], g_vt_pipe[2], g_vt_other[2];
Plugin g_ladder1, g_pipe1, g_ladder0;

int g_orig_calls = 0, g_force_calls = 0, g_prio_calls = 0;
int g_exec_calls = 0;
std::uint32_t g_exec_idx = 0, g_exec_kt = 0;
bool g_exec_result = true;
enum class ExecMode { normal, touch_cache, touch_viewer, fault };
ExecMode g_exec_mode = ExecMode::normal;
std::uint8_t g_prio_result = 6;

bool FakeCompleted(void*, void*, std::int32_t, void*) {
    ++g_orig_calls;
    return false;
}
void FakeForce(void*, std::int32_t, bool, void*) { ++g_force_calls; }
std::uint8_t FakePrio(void*, std::uint32_t, std::uint32_t kt) {
    ++g_prio_calls;
    return kt == 0 ? g_prio_result : 0;
}
bool FakeExec(void* plugin, std::uint32_t idx, std::uint32_t kt, void* res) {
    (void)plugin;
    ++g_exec_calls;
    g_exec_idx = idx;
    g_exec_kt = kt;
    Put<std::uint32_t>(Addr(res) + 0x2C, 0xD);  // the ladder's icon id
    switch (g_exec_mode) {
    case ExecMode::touch_cache: std::memset(g_padop + ic::kRayCache, 0x5A, ic::kRayCacheSize); break;
    case ExecMode::touch_viewer: g_padop[ic::kSlots + 4] ^= 0xFF; break;
    case ExecMode::fault:
        std::memset(g_padop + ic::kRayCache, 0x33, 8);
        *reinterpret_cast<volatile int*>(static_cast<std::uintptr_t>(0x10)) = 1;
        break;
    default: break;
    }
    return g_exec_result;
}
int g_pipe_calls = 0;  // keeps the two Exec stand-ins distinct (no identical-code folding)
bool FakeExecPipe(void* plugin, std::uint32_t idx, std::uint32_t kt, void* res) {
    ++g_pipe_calls;
    return FakeExec(plugin, idx, kt, res);
}

using CompletedFn = bool (*)(void*, void*, std::int32_t, void*);
using ForceFn = void (*)(void*, std::int32_t, bool, void*);
CompletedFn Slot28() { return reinterpret_cast<CompletedFn>(Get<std::uintptr_t>(ic::CopyTable() + ic::kSlotCompleted)); }
ForceFn Slot50() { return reinterpret_cast<ForceFn>(Get<std::uintptr_t>(ic::CopyTable() + ic::kSlotForce)); }

struct alignas(16) Res {
    unsigned char bytes[0x60];
};

void TestCopySlots() {
    // A fake image: only the vtable page range is committed (FillCopy reads vt[-1..95]).
    const std::size_t span = 0x2300000;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE, PAGE_NOACCESS));
    Check(image != nullptr, "image reserve");
    Check(VirtualAlloc(image + 0x22E8000, 0x2000, MEM_COMMIT, PAGE_READWRITE) != nullptr, "image vtable page");
    const std::uintptr_t base = Addr(image);
    for (unsigned i = 0; i < ic::kPadopSlots; ++i) Put<std::uintptr_t>(base + ic::kPadopVtableRva + 8u * i, base + 0x100000 + i);
    Put<std::uintptr_t>(base + ic::kPadopVtableRva - 8, base + 0x1A05976);
    Check(ic::CopyTable() == 0 && !ic::Ready(), "no copy before Build/bind");
    const std::uintptr_t exec[ic::kKinds] = {0, Addr(reinterpret_cast<const void*>(&FakeExec)), Addr(reinterpret_cast<const void*>(&FakeExecPipe)),
                                              0x7001, 0x7002};
    ic::detail::BindForTest(reinterpret_cast<void*>(base), &FakeCompleted, &FakeForce, exec);
    const std::uintptr_t table = ic::CopyTable();
    Check(ic::Ready() && table != 0 && (table & 7u) == 0, "copy bound");
    for (unsigned i = 0; i < ic::kPadopSlots; ++i) {
        const std::uintptr_t v = Get<std::uintptr_t>(table + 8u * i);
        if (8u * i == ic::kSlotCompleted) Check(v == ic::detail::CompletedSlot(), "copy +0x28 = IconCompletedBody1");
        else if (8u * i == ic::kSlotForce) Check(v == ic::detail::ForceSlot(), "copy +0x50 = IconForceBody1");
        else Check(v == base + 0x100000 + i, "every other slot copied unchanged");
    }
    Check(Get<std::uintptr_t>(table - 8) == base + 0x1A05976, "vt[-1] copied unchanged");
    // The world: P (flags bit 10, local index 0, Q with act words), padop owned by P, block-1 plugins.
    const std::uintptr_t P = Addr(g_P), padop = Addr(g_padop);
    Put<std::uintptr_t>(padop + ic::kPadopOwner, P);
    Put<std::uint32_t>(P + ic::kPFlags, ic::kPFlagIcons);
    Put<std::uintptr_t>(P + c::p_q, Addr(g_Q));
    Put<std::uint32_t>(P + c::p_local, 0);
    Put<std::uintptr_t>(Addr(g_Q) + c::q_act, Addr(g_acts));
    Put<std::uintptr_t>(Addr(g_Q) + c::q_upper, Addr(g_upper));
    g_acts[0] = 2;
    g_vt_ladder[0] = Addr(reinterpret_cast<const void*>(&FakePrio));
    g_vt_ladder[1] = exec[1];
    g_vt_pipe[0] = Addr(reinterpret_cast<const void*>(&FakePrio));
    g_vt_pipe[1] = exec[2];
    g_ladder1 = Plugin{Addr(g_vt_ladder), Addr(g_blk1)};
    g_pipe1 = Plugin{Addr(g_vt_pipe), Addr(g_blk1)};
    g_ladder0 = Plugin{Addr(g_vt_ladder), Addr(g_blk0)};
    std::memset(g_padop + ic::kRayCache, 0x11, ic::kRayCacheSize);
    Res res{};
    // Out of scope: every query goes to the original.
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_orig_calls == 1 && g_exec_calls == 0, "out of scope: the original");
    Slot50()(g_padop, 0, true, &res);
    Check(g_force_calls == 1, "out of scope: the original force");
    ic::Stats st{};
    // In scope: a ladder request with id 7.
    ic::Begin(P, Addr(g_blk1), static_cast<std::uint8_t>(ic::Kind::ladder), 7);
    Check(!Slot28()(g_padop, &g_ladder0, 0, &res) && g_orig_calls == 2 && g_exec_calls == 0, "block 0's plugin: the original");
    Check(!Slot28()(g_padop, &g_pipe1, 0, &res) && g_orig_calls == 2 && g_exec_calls == 0, "body 1's pipe while a ladder is requested: false");
    Check(!Slot28()(g_padop, &g_ladder1, 1, &res) && g_exec_calls == 0, "body 1's ladder with kt 1: false");
    Check(!Slot28()(g_padop, &g_ladder1, 0, reinterpret_cast<unsigned char*>(&res) + 8) && g_exec_calls == 0, "unaligned res: false");
    // Another thread in the window: the original.
    bool other = true;
    std::thread([&] { other = Slot28()(g_padop, &g_ladder1, 0, &res); }).join();
    Check(!other && g_orig_calls == 3 && g_exec_calls == 0, "another thread: the original");
    // Exec false: not granted, retried on the next query.
    g_exec_result = false;
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 1 && g_prio_calls == 1 && g_exec_idx == 1 && g_exec_kt == 0,
          "Exec(plugin, 1, 0, res) false: false");
    g_exec_result = true;
    g_exec_mode = ExecMode::touch_cache;
    Check(Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 2 && Get<std::uint32_t>(Addr(&res) + 0x2C) == 0xD,
          "Exec true: granted (res filled by Exec)");
    bool cache_ok = true;
    for (std::size_t i = 0; i < ic::kRayCacheSize; ++i) cache_ok = cache_ok && g_padop[ic::kRayCache + i] == 0x11;
    Check(cache_ok, "the shared ray-cache entry padop+0x7F0 is restored after Exec");
    g_exec_mode = ExecMode::normal;
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 2, "granted once per request id");
    // Body 1's step-on force in scope: swallowed.
    Slot50()(g_padop, 0, true, &res);
    Check(g_force_calls == 1, "in scope: the force-complete is swallowed");
    ic::ReadStats(st);
    Check(st.ready && st.windows == 1 && st.exec_ok == 1 && st.exec_fail == 1 && st.granted == 1 && st.granted_seq == 7 &&
              st.force_swallowed == 1 && st.force_passed == 1 && !st.latched && st.last_kind == 1 &&
              st.last_deny == static_cast<std::uint8_t>(ic::Deny::granted),
          "stats after one grant");
    ic::End();
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_orig_calls == 4, "after End: the original");
    // A new request id grants again; the P flag and the padop owner gate it.
    ic::Begin(P, Addr(g_blk1), static_cast<std::uint8_t>(ic::Kind::ladder), 8);
    ic::Stats b{};
    ic::ReadStats(b);
    Put<std::uint32_t>(P + ic::kPFlags, 0);
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 2, "P+0x204 bit 10 clear: false");
    // Body 1's pipe poll runs after the requested ladder's in the same job: it counts in denied, but the
    // requested plugin's deny stays p_flag.
    Check(!Slot28()(g_padop, &g_pipe1, 0, &res) && g_exec_calls == 2, "another body-1 poll after the refusal: false");
    ic::ReadStats(st);
    Check(st.queries == b.queries + 2 && st.denied == b.denied + 2 && st.last_deny == static_cast<std::uint8_t>(ic::Deny::kind) &&
              st.req_queries == b.req_queries + 1 && st.req_denied == b.req_denied + 1 &&
              st.req_deny == static_cast<std::uint8_t>(ic::Deny::p_flag),
          "the requested plugin's deny is kept apart from body 1's other polls");
    Put<std::uint32_t>(P + ic::kPFlags, ic::kPFlagIcons);
    Put<std::uintptr_t>(padop + ic::kPadopOwner, P + 8);
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 2, "padop of another P: false");
    Put<std::uintptr_t>(padop + ic::kPadopOwner, P);
    g_prio_result = 0;
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 2, "priority 0: false, no Exec");
    g_prio_result = 6;
    // A viewer change during Exec: no grant, latched for the mission.
    g_exec_mode = ExecMode::touch_viewer;
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 3, "viewer slot changed by Exec: false");
    g_exec_mode = ExecMode::normal;
    ic::ReadStats(st);
    Check(st.latched && st.viewer_changed == 1 && st.granted == 1, "viewer change latched, not granted");
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 3, "latched: no Exec");
    ic::ResetMission();
    Check(Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 4, "MissionStart clears the latch: granted");
    ic::End();
    // A fault inside Exec: caught, the ray cache restored, latched.
    ic::Begin(P, Addr(g_blk1), static_cast<std::uint8_t>(ic::Kind::ladder), 9);
    g_exec_mode = ExecMode::fault;
    std::memset(g_padop + ic::kRayCache, 0x22, ic::kRayCacheSize);
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 5, "Exec fault: false");
    g_exec_mode = ExecMode::normal;
    cache_ok = true;
    for (std::size_t i = 0; i < ic::kRayCacheSize; ++i) cache_ok = cache_ok && g_padop[ic::kRayCache + i] == 0x22;
    ic::ReadStats(st);
    Check(cache_ok && st.faults == 1 && st.latched, "fault: the ray cache restored, latched");
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 5, "faulted: no Exec");
    ic::ResetMission();
    ic::End();
    // A pipe request answers the pipe plugin only.
    ic::Begin(P, Addr(g_blk1), static_cast<std::uint8_t>(ic::Kind::pipe), 10);
    Check(!Slot28()(g_padop, &g_ladder1, 0, &res) && g_exec_calls == 5, "pipe request: body 1's ladder false");
    Check(Slot28()(g_padop, &g_pipe1, 0, &res) && g_exec_calls == 6 && g_pipe_calls == 1, "pipe request: body 1's pipe granted");
    ic::End();
    ic::ReadStats(st);
    Check(st.granted == 3 && st.granted_seq == 10 && st.last_kind == static_cast<std::uint8_t>(ic::Kind::pipe), "three grants");
    // ApplyPost's restore outcomes: a write or an already-restored vptr is quiet; a foreign vptr and a fault latch.
    ic::Stats r0{};
    ic::ReadStats(r0);
    ic::NoteRestore(ic::Restore::write);
    ic::NoteRestore(ic::Restore::already);
    ic::ReadStats(st);
    Check(!st.latched && st.restore_mismatch == r0.restore_mismatch && st.faults == r0.faults, "restore write/already: quiet");
    ic::NoteRestore(ic::Restore::mismatch);
    ic::ReadStats(st);
    Check(st.latched && st.restore_mismatch == r0.restore_mismatch + 1 && st.faults == r0.faults, "restore mismatch: counted, latched");
    ic::ResetMission();
    ic::NoteRestore(ic::Restore::fault);
    ic::ReadStats(st);
    Check(st.latched && st.faults == r0.faults + 1, "restore fault: counted, latched");
    ic::ResetMission();
    ic::detail::ResetForTest();
    Check(!ic::Ready() && ic::CopyTable() == 0, "reset");
    VirtualFree(image, 0, MEM_RELEASE);
}

void TestReadCensus() {
    alignas(16) static unsigned char ps0[0x300], ps1[0x300];
    alignas(16) static Plugin p0[ic::kKinds], p1[ic::kKinds];
    alignas(16) static std::uintptr_t vts[ic::kKinds][2];
    const std::uintptr_t base = 0x140000000ull;
    Put<std::uintptr_t>(Addr(g_blk0) + ic::kBlockPs, Addr(ps0));
    Put<std::uintptr_t>(Addr(g_blk1) + ic::kBlockPs, Addr(ps1));
    for (unsigned k = 1; k < ic::kKinds; ++k) {
        vts[k][1] = base + ic::kExecRva[k];
        p0[k] = Plugin{Addr(vts[k]), Addr(g_blk0)};
        p1[k] = Plugin{Addr(vts[k]), Addr(g_blk1)};
        Put<std::uintptr_t>(Addr(ps0) + ic::kPsPlugins + 8u * (3 * k), Addr(&p0[k]));
        Put<std::uintptr_t>(Addr(ps1) + ic::kPsPlugins + 8u * (3 * k), Addr(&p1[k]));
    }
    ic::Census s{};
    ic::ReadCensus(reinterpret_cast<void*>(base), Addr(g_blk0), Addr(g_blk1), s);
    Check(s.read && s.plugins == 4 && s.distinct == 4 && s.null0 == ic::kPlugins - 4 && std::strcmp(ic::CensusVerdict(s), "ok") == 0,
          "ReadCensus: fake lists, ok");
    Put<std::uintptr_t>(Addr(ps1) + ic::kPsPlugins + 8u * 3, Addr(&p0[1]));  // block 1 shares block 0's ladder
    ic::ReadCensus(reinterpret_cast<void*>(base), Addr(g_blk0), Addr(g_blk1), s);
    Check(s.read && s.distinct == 3 && std::strcmp(ic::CensusVerdict(s), "shared") == 0, "ReadCensus: a shared ladder");
    Put<std::uintptr_t>(Addr(g_blk1) + ic::kBlockPs, 0x10);  // unreadable PS
    ic::ReadCensus(reinterpret_cast<void*>(base), Addr(g_blk0), Addr(g_blk1), s);
    Check(!s.read && std::strcmp(ic::CensusVerdict(s), "unread") == 0, "ReadCensus: a fault is unread");
}

} // namespace

int main() {
    TestPure();
    TestCensus();
    TestContract();
    TestCopySlots();
    TestReadCensus();
    std::printf("%u partner icon checks passed: route/grant/force/swap decisions, census verdicts, contract slots and pins, the copy "
                "slots on fake objects (original out of scope / other thread / block 0, one grant per id, ray cache restore, viewer "
                "latch, fault latch, force swallow, the requested plugin's deny), the padop restore outcomes, the read-only census "
                "reader\n",
                checks);
    return 0;
}
