// partner_ui_filter (G1, docs/native-puppet.md "Partner HUD hints"): the pure
// route / identity / key / log decisions, the contract pins, and the real leaf
// detours on fake objects with bound originals: swallowed only in the drain
// scope on its thread for the scoped UiControllerImpl, the viewer's requests
// (outside the scope, another thread, another receiver) forwarded unchanged
// with their full ABI, Begin's identity refusals, the fault latch, the
// records digest, the census keys, Shutdown, and Install's refusals on a fake
// image (no hook is ever created on the fake).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "mgscoop/partner_ui_filter.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace uf = mgscoop::partner_ui_filter;
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

void TestPure() {
    // Route: only in scope, on the scoped thread, not shut down, for the scoped UI.
    Check(uf::RouteHint(true, true, false, 0x5000, 0x5000) == uf::Route::swallow, "scope + thread + ui: swallow");
    Check(uf::RouteHint(false, true, false, 0x5000, 0x5000) == uf::Route::original, "out of scope: original");
    Check(uf::RouteHint(true, false, false, 0x5000, 0x5000) == uf::Route::original, "another thread: original");
    Check(uf::RouteHint(true, true, true, 0x5000, 0x5000) == uf::Route::original, "shut down: original");
    Check(uf::RouteHint(true, true, false, 0x6000, 0x5000) == uf::Route::original, "another receiver: original");
    Check(uf::RouteHint(true, true, false, 0, 0) == uf::Route::original, "no scoped ui: original");
    // Identity: the stock UiControllerImpl owned by the window's P.
    const std::uintptr_t image = 0x140000000;
    uf::Identity id{0x9000, 0x7020, image + uf::kUiVtableRva, image + uf::kUiPrimaryVtableRva, 0x9000};
    Check(uf::CheckIdentity(id, image) == uf::Refusal::none, "identity: stock UI of P");
    { auto x = id; x.P = 0; Check(uf::CheckIdentity(x, image) == uf::Refusal::player, "identity: no P"); }
    { auto x = id; x.ui = 0; Check(uf::CheckIdentity(x, image) == uf::Refusal::ui, "identity: null UI"); }
    { auto x = id; x.ui = 0x18; Check(uf::CheckIdentity(x, image) == uf::Refusal::ui, "identity: UI below its primary offset"); }
    { auto x = id; x.ui = 0x7024; Check(uf::CheckIdentity(x, image) == uf::Refusal::align, "identity: unaligned UI"); }
    { auto x = id; x.ui_vt = image + 0x23AF358; Check(uf::CheckIdentity(x, image) == uf::Refusal::vtable, "identity: another vtable"); }
    { auto x = id; x.primary_vt = image + uf::kUiVtableRva; Check(uf::CheckIdentity(x, image) == uf::Refusal::primary, "identity: wrong primary"); }
    { auto x = id; x.owner = 0x9100; Check(uf::CheckIdentity(x, image) == uf::Refusal::owner, "identity: another P's UI"); }
    Check(std::strcmp(uf::RefusalName(uf::Refusal::vtable), "vtable") == 0 && std::strcmp(uf::RefusalName(uf::Refusal::none), "none") == 0 &&
              std::strcmp(uf::RefusalName(uf::Refusal::fault), "fault") == 0 && std::strcmp(uf::RefusalName(uf::Refusal::latched), "latched") == 0,
          "refusal names");
    // Census key packing.
    const std::uint64_t k = uf::Key(4, 3, 8, 0x11EC60C);
    Check(uf::KeyQueue(k) == 4 && uf::KeyId(k) == 3 && uf::KeyPriority(k) == 8 && uf::KeyCaller(k) == 0x11EC60C, "key round trip");
    Check(uf::KeyPriority(uf::Key(0, 0xFFFF, 0x1234, 0)) == 0xFF && uf::KeyId(uf::Key(0, 0xFFFF, 1, 0)) == 0xFFFF, "key saturates the priority");
    Check(uf::Key(4, 3, 8, 0x11EC60C) != uf::Key(4, 3, 8, 0x11EB5A6) && uf::Key(3, 3, 8, 1) != uf::Key(4, 3, 8, 1), "keys differ by caller and queue");
    // Caller names (the static census's return addresses).
    for (std::uint32_t rva : {0x11EB5A6u, 0x11EBFF4u, 0x11EC60Cu, 0x11EDB17u}) Check(std::strcmp(uf::CallerName(rva), "ladder") == 0, "ladder callers");
    Check(std::strcmp(uf::CallerName(0x114FC4E), "climb") == 0 && std::strcmp(uf::CallerName(0x1216899), "pipe") == 0 &&
              std::strcmp(uf::CallerName(0x11A7922), "hang") == 0 && std::strcmp(uf::CallerName(0x124C816), "ride_vehicle") == 0 &&
              std::strcmp(uf::CallerName(0x12C2130), "trash_box") == 0 && std::strcmp(uf::CallerName(0x11D9DB8), "interactive") == 0 &&
              std::strcmp(uf::CallerName(0x1053CE4), "attack") == 0 && std::strcmp(uf::CallerName(0x11EC606), "other") == 0 &&
              std::strcmp(uf::CallerName(0), "other") == 0,
          "caller names");
    // Log decision: the first line at once, then at most one per gap, only when moved.
    Check(uf::LogDue(true, 5000, 0) && !uf::LogDue(false, 5000, 0), "log: first moved line at once, nothing when still");
    Check(!uf::LogDue(true, 5999, 5000) && uf::LogDue(true, 6000, 5000) && !uf::LogDue(false, 9000, 5000), "log: one per kLogGapMs");
    // Queues: the five unfiltered UiControllerImpl slots, records 0x50 apart in one block.
    for (unsigned q = 0; q < uf::kQueues; ++q) {
        Check(uf::kQueue[q].slot == 0x240 + 8 * q && uf::kQueue[q].record == uf::kRecordsBegin + 0x50 * q, "queue slot / record layout");
    }
    Check(uf::kRecordsEnd == uf::kQueue[uf::kQueues - 1].record + 0x50, "records end after the last record");
}

void TestContract() {
    // The bodies Install hashes are the five leaves in kQueue order, 0x70 bytes each.
    Check(sizeof(c::ui_filter_bodies) / sizeof(c::ui_filter_bodies[0]) == uf::kQueues, "five pinned leaves");
    for (unsigned q = 0; q < uf::kQueues; ++q)
        Check(c::ui_filter_bodies[q].rva == uf::kQueue[q].leaf_rva && c::ui_filter_bodies[q].size == uf::kLeafSize, "pin q: rva and size");
    // The code gate checks the slots and the prologues.
    for (const uf::Queue& q : uf::kQueue) {
        bool slot = false, site = false;
        for (const auto& s : c::vtable_slots)
            if (s.vtable_rva == uf::kUiVtableRva && s.offset == q.slot && s.target_rva == q.leaf_rva) slot = true;
        for (const auto& s : c::code_sites)
            if (s.rva == q.leaf_rva && s.size == 16) site = true;
        Check(slot && site, "each leaf: its UiControllerImpl slot and prologue are in the contract");
    }
    // The prologue's priority operand is the record's priority byte (record + 0x33).
    for (unsigned q = 0; q < uf::kQueues; ++q) {
        for (const auto& s : c::code_sites) {
            if (s.rva != uf::kQueue[q].leaf_rva) continue;
            std::uint32_t disp = 0;
            std::memcpy(&disp, s.bytes + 3, 4);
            Check(s.bytes[0] == 0x0f && s.bytes[1] == 0xb6 && s.bytes[2] == 0x81 && disp == uf::kQueue[q].record + 0x33,
                  "prologue: movzx eax, byte [rcx + record + 0x33]");
        }
    }
    // The census call sites (ladder state 9: the 0.13.5 frames' hint).
    bool ladder = false;
    for (const auto& s : c::code_sites)
        if (s.rva == 0x11EC5D6 && s.size >= 6 && std::memcmp(s.bytes + s.size - 6, "\xff\x90\x60\x02\x00\x00", 6) == 0) ladder = true;
    Check(ladder, "the ladder state-9 call through vt+0x260 is pinned");
}

// --- Detours on fake objects -------------------------------------------------
struct CallRecord {
    unsigned queue;
    void* ui;
    std::uint16_t id;
    std::uint32_t priority;
    std::uint8_t flag, a5, a6, a7;
    DWORD thread;
};
std::vector<CallRecord> g_calls;
template <unsigned Q>
void FakeLeaf(void* ui, std::uint16_t id, std::uint32_t priority, std::uint8_t flag, std::uint8_t a5, std::uint8_t a6, std::uint8_t a7) {
    g_calls.push_back({Q, ui, id, priority, flag, a5, a6, a7, GetCurrentThreadId()});
}
constexpr uf::detail::LeafFn kFakes[uf::kQueues] = {&FakeLeaf<0>, &FakeLeaf<1>, &FakeLeaf<2>, &FakeLeaf<3>, &FakeLeaf<4>};

struct World {
    std::uintptr_t image = 0x10000;  // fake image base (only compared, never read)
    // One committed page: the player at +0 (0x300 bytes), the UI object at +0x800 (primary at +0, UiControllerImpl
    // at +0x20, 0x600 bytes).
    unsigned char* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    unsigned char* player = page;
    unsigned char* ui_object = page + 0x800;
    std::uintptr_t P() const { return Addr(player); }
    std::uintptr_t Ui() const { return Addr(ui_object) + 0x20; }
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    ~World() { VirtualFree(page, 0, MEM_RELEASE); }
    World() {
        Check(page != nullptr, "commit the fake player / UI page");
        Put<std::uintptr_t>(P() + uf::kPUi, Ui());
        Put<std::uintptr_t>(Ui(), image + uf::kUiVtableRva);
        Put<std::uintptr_t>(Ui() - uf::kUiPrimary, image + uf::kUiPrimaryVtableRva);
        Put<std::uintptr_t>(Ui() + uf::kUiOwner, P());
        // Idle records: priority 0xFF.
        for (const uf::Queue& q : uf::kQueue) ui_object[0x20 + q.record + 0x33] = 0xFF;
    }
};

uf::Stats Stats() {
    uf::Stats s{};
    uf::ReadStats(s);
    return s;
}
std::uint32_t Swallowed(const uf::Stats& s) {
    std::uint32_t n = 0;
    for (std::uint32_t v : s.swallowed) n += v;
    return n;
}

void TestDetours() {
    uf::detail::ResetForTest();
    World w;
    // Not installed: Begin is silent and every request passes.
    uf::Begin(w.P());
    Check(Stats().windows == 0 && Stats().refused == 0, "not installed: Begin opens nothing and counts nothing");
    uf::End();
    uf::detail::BindForTest(reinterpret_cast<const void*>(w.image), kFakes);
    void* ui = reinterpret_cast<void*>(w.Ui());
    g_calls.clear();
    // Outside the scope: the viewer's own request goes to the leaf with its full ABI.
    uf::detail::Detour(4)(ui, 3, 8, 0, 1, 0, 0);
    uf::detail::Detour(0)(ui, 0x52, 8, 1, 0, 1, 1);
    Check(g_calls.size() == 2 && g_calls[0].queue == 4 && g_calls[0].ui == ui && g_calls[0].id == 3 && g_calls[0].priority == 8 &&
              g_calls[0].flag == 0 && g_calls[0].a5 == 1 && g_calls[0].a6 == 0 && g_calls[0].a7 == 0 && g_calls[1].queue == 0 &&
              g_calls[1].id == 0x52 && g_calls[1].flag == 1 && g_calls[1].a5 == 0 && g_calls[1].a6 == 1 && g_calls[1].a7 == 1,
          "outside the scope: forwarded unchanged");
    Check(Stats().passed == 2 && Swallowed(Stats()) == 0, "outside: counted as passed");
    // In the scope on this thread: body 1's requests never reach the leaf.
    uf::Begin(w.P());
    uf::Stats s = Stats();
    Check(s.windows == 1 && s.refused == 0, "Begin opens the scope for the stock UI of P");
    g_calls.clear();
    for (unsigned q = 0; q < uf::kQueues; ++q) uf::detail::Detour(q)(ui, static_cast<std::uint16_t>(3 + q), 8, 0, 1, 0, 0);
    uf::detail::Detour(4)(ui, 7, 8, 0, 1, 0, 0);  // the same request again (every frame)
    s = Stats();
    Check(g_calls.empty() && s.swallowed[0] == 1 && s.swallowed[1] == 1 && s.swallowed[2] == 1 && s.swallowed[3] == 1 && s.swallowed[4] == 2,
          "in scope: every queue's body-1 request swallowed");
    Check(s.keys == 5 && uf::KeyQueue(s.key[0]) == 0 && uf::KeyId(s.key[0]) == 3 && uf::KeyQueue(s.key[4]) == 4 && uf::KeyId(s.key[4]) == 7 &&
              uf::KeyPriority(s.key[4]) == 8 && uf::KeyCaller(s.key[4]) == 0,
          "in scope: one key per distinct request (the test caller is outside the fake image: rva 0)");
    // Another receiver in scope (never the viewer's UI): forwarded, counted foreign.
    alignas(16) unsigned char other[0x400]{};
    uf::detail::Detour(4)(other + 0x20, 3, 8, 0, 1, 0, 0);
    Check(g_calls.size() == 1 && g_calls[0].ui == other + 0x20 && Stats().foreign == 1, "in scope, another receiver: forwarded, foreign");
    // Another thread during the scope (the viewer's own job, the UI thread): forwarded.
    g_calls.clear();
    std::thread t([&] { uf::detail::Detour(4)(ui, 3, 8, 0, 1, 0, 0); });
    t.join();
    Check(g_calls.size() == 1 && g_calls[0].thread != GetCurrentThreadId() && Stats().swallowed[4] == 2 && Stats().foreign == 1,
          "in scope, another thread: forwarded");
    // End: the scope closes (idempotent); the next request passes.
    uf::End();
    uf::End();
    g_calls.clear();
    uf::detail::Detour(4)(ui, 3, 8, 0, 1, 0, 0);
    Check(g_calls.size() == 1 && Stats().swallowed[4] == 2, "after End: forwarded");
    Check(Stats().changed == 0, "the records did not change during the job");
    // A record written during the scope (an unhooked writer) is counted at End.
    uf::Begin(w.P());
    w.ui_object[0x20 + 0x320] = 3;
    w.ui_object[0x20 + 0x353] = 8;
    uf::End();
    Check(Stats().changed == 1 && Stats().windows == 2, "a record changed during the scope: changed");
    // Begin while open closes the previous scope first.
    uf::Begin(w.P());
    uf::Begin(w.P());
    Check(Stats().windows == 4, "a second Begin reopens");
    uf::End();
    // Identity refusals: the scope stays closed and requests pass.
    const auto refused = [&](const char* what, uf::Refusal want) {
        const std::uint32_t before = Stats().refused;
        uf::Begin(w.P());
        g_calls.clear();
        uf::detail::Detour(4)(ui, 3, 8, 0, 1, 0, 0);
        const uf::Stats r = Stats();
        Check(r.refused == before + 1 && r.last_refusal == static_cast<std::uint8_t>(want) && g_calls.size() == 1, what);
        uf::End();
    };
    Put<std::uintptr_t>(w.Ui(), w.image + 0x23AF358);
    refused("another vtable: refused", uf::Refusal::vtable);
    Put<std::uintptr_t>(w.Ui(), w.image + uf::kUiVtableRva);
    Put<std::uintptr_t>(w.Ui() - uf::kUiPrimary, 0);
    refused("another primary: refused", uf::Refusal::primary);
    Put<std::uintptr_t>(w.Ui() - uf::kUiPrimary, w.image + uf::kUiPrimaryVtableRva);
    Put<std::uintptr_t>(w.Ui() + uf::kUiOwner, w.P() + 8);
    refused("another owner: refused", uf::Refusal::owner);
    Put<std::uintptr_t>(w.Ui() + uf::kUiOwner, w.P());
    Put<std::uintptr_t>(w.P() + uf::kPUi, 0);
    refused("no UI: refused", uf::Refusal::ui);
    Put<std::uintptr_t>(w.P() + uf::kPUi, w.Ui() + 4);
    refused("unaligned UI: refused", uf::Refusal::align);
    Put<std::uintptr_t>(w.P() + uf::kPUi, w.Ui());
    // A fault reading the identity latches the mission; MissionStart clears it.
    void* noaccess = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
    Check(noaccess != nullptr, "reserve a no-access page");
    const std::uint32_t faults = Stats().faults;
    uf::Begin(Addr(noaccess));
    Check(Stats().faults == faults + 1 && Stats().latched && Stats().last_refusal == static_cast<std::uint8_t>(uf::Refusal::fault),
          "fault: counted and latched");
    refused("latched: refused until MissionStart", uf::Refusal::latched);
    uf::ResetMission();
    uf::Begin(w.P());
    Check(!Stats().latched && Stats().last_refusal == static_cast<std::uint8_t>(uf::Refusal::latched), "MissionStart clears the latch");
    g_calls.clear();
    uf::detail::Detour(4)(ui, 3, 8, 0, 1, 0, 0);
    Check(g_calls.empty(), "after MissionStart: swallowed again");
    // Shutdown: everything passes, Begin is silent.
    uf::Shutdown();
    g_calls.clear();
    uf::detail::Detour(4)(ui, 3, 8, 0, 1, 0, 0);
    const std::uint32_t windows = Stats().windows;
    uf::Begin(w.P());
    uf::detail::Detour(4)(ui, 3, 8, 0, 1, 0, 0);
    Check(g_calls.size() == 2 && Stats().windows == windows, "shut down: forwarded, no scope");
    VirtualFree(noaccess, 0, MEM_RELEASE);
    // The census keys are capped (kKeys distinct per process).
    uf::detail::ResetForTest();
    uf::detail::BindForTest(reinterpret_cast<const void*>(w.image), kFakes);
    uf::Begin(w.P());
    for (unsigned i = 0; i < uf::kKeys + 8; ++i) uf::detail::Detour(4)(ui, static_cast<std::uint16_t>(i), 8, 0, 1, 0, 0);
    uf::End();
    Check(Stats().keys == uf::kKeys && Stats().swallowed[4] == uf::kKeys + 8, "keys capped, every request still swallowed");
    uf::detail::ResetForTest();
}

std::string g_lines;
void CaptureLog(const char* line) noexcept { g_lines += line; }

void TestInstallRefusals() {
    uf::detail::ResetForTest();
    g_lines.clear();
    Check(std::strcmp(uf::Install(nullptr, 0, &CaptureLog), "image") == 0 && !uf::Installed(), "no image: refused");
    Check(g_lines.find("partner_ui_filter=install_refused; reason=image; hooks=none") != std::string::npos, "the refusal is logged once");
    Check(std::strcmp(uf::Install(nullptr, 0, &CaptureLog), "image") == 0, "a second Install returns the first reason");
    // A fake image: the UiControllerImpl vtable page committed. Wrong slots first, then the right slots with zeroed
    // leaves (the pins refuse): no hook is created on the fake.
    const std::size_t span = 0x2400000;
    auto* base = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE, PAGE_NOACCESS));
    Check(base != nullptr, "reserve the fake image");
    Check(VirtualAlloc(base + 0x23AF000, 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit the vtable page");
    uf::detail::ResetForTest();
    Check(std::strcmp(uf::Install(base, span, &CaptureLog), "slots") == 0, "slots not the leaves: refused");
    for (const uf::Queue& q : uf::kQueue) Put<std::uintptr_t>(Addr(base) + uf::kUiVtableRva + q.slot, Addr(base) + q.leaf_rva);
    uf::detail::ResetForTest();
    Check(std::strcmp(uf::Install(base, span, &CaptureLog), "pins") == 0, "leaf pages unreadable: the pins refuse");
    Check(VirtualAlloc(base + 0xFE8000, 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit the leaf page");
    uf::detail::ResetForTest();
    Check(std::strcmp(uf::Install(base, span, &CaptureLog), "pins") == 0 && !uf::Installed() && !uf::PatchLive(),
          "zeroed leaves: the pins refuse, nothing installed");
    VirtualFree(base, 0, MEM_RELEASE);
    uf::detail::ResetForTest();
}
} // namespace

int main() {
    TestPure();
    TestContract();
    TestDetours();
    TestInstallRefusals();
    std::printf("partner_ui_filter_tests: %u checks passed\n", checks);
    return 0;
}
