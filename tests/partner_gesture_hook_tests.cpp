// Gestures (docs/native-puppet.md "Gestures"), native side on a fake image and
// fake bodies: body 1's identity, the update filter (entry and caller), the
// submission (IsGraphExistNode, SetCurrentNode / ReserveToResetNode with the
// stock ABI), the readback, the end (clip, speed, deadline, override, lease,
// epoch) with the stateNull return, the refusals, the viewer digest latch,
// another owner's waiting graph-2 request (pending), the window close
// (Shutdown) and the SEH fault latch. No hook is installed and no game code runs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "mgscoop/partner_gesture.hpp"

namespace {
namespace pg = mgscoop::partner_gesture;
namespace gp = mgscoop::partner_gesture_policy;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u: %s\n", checks, what);
        std::exit(1);
    }
}

constexpr std::size_t kImageSpan = 0x2A84000;
constexpr std::uint64_t kNull = gp::kStateNull, kPickup = 0x1E904603EB7DULL, kSignGo = 0xE74E11BABA4BULL, kOther = 0x91A5B8AC42F8ULL;
const std::uint64_t kCodes[] = {kNull, kPickup, kSignGo, kOther, 0x82E84B26E4CEULL};

std::uintptr_t image = 0;
unsigned char* w = nullptr;  // the fake world
template <class T> void Put(std::uintptr_t at, T v) { std::memcpy(reinterpret_cast<void*>(at), &v, sizeof(v)); }
template <class T> T Get(std::uintptr_t at) { T v{}; std::memcpy(&v, reinterpret_cast<const void*>(at), sizeof(v)); return v; }
std::uintptr_t W(std::size_t off) { return reinterpret_cast<std::uintptr_t>(w) + off; }
void PutRel(std::uintptr_t at, std::uintptr_t target) { Put<std::int32_t>(at, static_cast<std::int32_t>(static_cast<std::int64_t>(target) - static_cast<std::int64_t>(at))); }

// Offsets in the world.
constexpr std::size_t P = 0x0, Q = 0x400, ACT = 0x480, UPPER = 0x490, Q28 = 0x4A0, IDS = 0x4B0, POOLS = 0x4C0, TABLE = 0x4D0;
constexpr std::size_t BLOCKS = 0x1000, PS0 = 0x2000, PS1 = 0x2800, CS0 = 0x3000, CS1 = 0x3100, ENTRY0 = 0x3200, ENTRY1 = 0x3300;
constexpr std::size_t GRAPHS0 = 0x3400, GRAPHS1 = 0x3600, LOWER0 = 0x3800, UPPER0 = 0x3A00, LOWER1 = 0x3C00, UPPER1 = 0x3E00;
constexpr std::size_t BANK = 0x4000, DESC = 0x4100, NODES = 0x4400, CODES = 0x4A00, BLEND_A = 0x4C00, BLEND_B = 0x4D00, BLEND_C = 0x4E00;
std::uintptr_t Buffer(unsigned k) { return W(k ? GRAPHS1 : GRAPHS0) + 0x20; }
std::uintptr_t Node(unsigned i) { return W(NODES) + i * 0x48; }
std::uintptr_t NodeFor(std::uint64_t code) {
    for (unsigned i = 0; i < sizeof(kCodes) / sizeof(kCodes[0]); ++i)
        if (kCodes[i] == code) return Node(i);
    return 0;
}

void BuildWorld() {
    std::memset(w, 0, 0x10000);
    Put<std::uintptr_t>(image + 0x2a82c30, W(P));
    Put<std::uintptr_t>(W(P), image + 0x2295490);
    Put<std::uint32_t>(W(P) + 0x204, 0x2001);
    Put<std::uint32_t>(W(P) + 0x208, 2);
    Put<std::uint32_t>(W(P) + 0x214, 0);
    Put<std::uintptr_t>(W(P) + 0x60, W(Q));
    Put<std::uintptr_t>(W(P) + 0x70, W(IDS));
    Put<std::uintptr_t>(W(P) + 0x78, W(POOLS));
    Put<std::uintptr_t>(W(P) + 0x378, W(BLOCKS));
    Put<std::uintptr_t>(W(P) + 0x50, W(TABLE));
    Put<std::uintptr_t>(W(Q), image + 0x23ad970);
    Put<std::uintptr_t>(W(Q) + 0x18, W(ACT));
    Put<std::uintptr_t>(W(Q) + 0x20, W(UPPER));
    Put<std::uintptr_t>(W(Q) + 0x28, W(Q28));
    Put<std::uint16_t>(W(ACT), 3);
    Put<std::uint16_t>(W(ACT) + 2, 1);  // body 1 stands
    Put<std::uint16_t>(W(IDS), 0);
    Put<std::uint16_t>(W(IDS) + 2, 1);
    Put<std::uint8_t>(W(POOLS), 0);
    Put<std::uint8_t>(W(POOLS) + 1, 1);
    const std::size_t ps[2] = {PS0, PS1}, cs[2] = {CS0, CS1}, entry[2] = {ENTRY0, ENTRY1}, graphs[2] = {GRAPHS0, GRAPHS1};
    const std::size_t lower[2] = {LOWER0, LOWER1}, upper[2] = {UPPER0, UPPER1};
    for (unsigned k = 0; k < 2; ++k) {
        const std::uintptr_t blk = W(BLOCKS) + k * 0x6a8;
        Put<std::uintptr_t>(W(TABLE) + k * 8, blk);
        Put<std::uintptr_t>(blk, image + 0x2295210);
        Put<std::uintptr_t>(blk + 0x138, W(P));
        Put<std::uint32_t>(blk + 8, 1);
        Put<std::uint32_t>(blk + 0xc, k);
        Put<std::uintptr_t>(blk + 0x78, W(ps[k]));
        Put<std::uintptr_t>(W(ps[k]), image + (k ? 0x23b0c70 : 0x23b09f0));
        Put<std::uint32_t>(W(ps[k]) + 8, k);
        Put<std::uintptr_t>(W(ps[k]) + 0x408, blk);
        Put<std::uintptr_t>(W(ps[k]) + 0x238, W(cs[k]));
        Put<std::uintptr_t>(W(cs[k]), image + 0x22e6070);
        Put<std::uint32_t>(W(cs[k]) + 8, 1);
        Put<std::uint32_t>(W(cs[k]) + 0xc, k);
        Put<std::uintptr_t>(W(cs[k]) + 0x10, W(entry[k]));
        Put<std::uintptr_t>(W(entry[k]) + 0x90, W(graphs[k]));
        Put<std::uintptr_t>(W(entry[k]) + 0x98, W(graphs[k]) + 0x20);
        Put<std::uint32_t>(W(entry[k]) + 0xb0, 4);
        Put<std::uintptr_t>(W(entry[k]) + 0x70, W(BLEND_A));
        Put<std::uintptr_t>(W(graphs[k]), W(lower[k]));
        Put<std::uintptr_t>(W(graphs[k]) + 0x10, W(upper[k]));
        const std::uintptr_t buffer = Buffer(k);
        Put<std::uint32_t>(buffer + 0xc, 0x28);
        Put<std::uintptr_t>(buffer + 0x10, buffer + 0x20);
        Put<std::uintptr_t>(W(lower[k]), image + 0x250ba50);
        Put<std::uintptr_t>(W(upper[k]), image + 0x250ba50);
        Put<std::uint32_t>(W(upper[k]) + 0xd4, 2);
        Put<std::uintptr_t>(W(upper[k]) + 0x1b8, W(BANK));
        Put<std::uintptr_t>(W(upper[k]) + 0x1a0, Node(0));  // stateNull
    }
    Put<std::uint32_t>(W(BANK) + 0x1c, 4);
    PutRel(W(BANK) + 0x20, W(DESC));
    const std::uintptr_t d2 = W(DESC) + 2 * 0x38 + 8;
    Put<std::uint32_t>(d2, 122);
    PutRel(d2 + 4, W(NODES));
    for (unsigned i = 0; i < sizeof(kCodes) / sizeof(kCodes[0]); ++i) {
        Put<std::uint64_t>(W(CODES) + i * 8, kCodes[i]);
        PutRel(Node(i) + 0x18, W(CODES) + i * 8);
    }
    Put<std::uintptr_t>(W(BLEND_A) + 0x10, W(BLEND_B));
    Put<std::uintptr_t>(W(BLEND_B) + 0xf8 + 3 * 8, W(BLEND_C));
    // The cs vtable slots the calls go through.
    for (const pg::Slot& s : pg::kSlots) Put<std::uintptr_t>(image + s.vtable_rva + s.offset, image + s.target_rva);
}

// --- Fake methods (the stock ABI), recording every call.
struct Call {
    char what;  // e exist, s set, r reserve, m motion_end, p play_end
    std::uintptr_t cs;
    std::uint32_t body, graph;
    std::uint64_t node;
    float blend;
    std::uint32_t option;
    bool tags_ok;
};
std::vector<Call> calls;
bool exist_result = true, motion_end_result = false, play_end_result = false;
bool touch_viewer = false;  // the submission also moves body 0's masks (the uncertain latch)
std::uint64_t pending = 0;  // the request the next fake update applies
// Inside the end test / the exist query: another owner's graph-2 request
// arrives, or the window starts closing (the races the re-reads cover).
std::uint64_t foreign_on_end = 0;
bool shutdown_on_end = false, shutdown_on_exist = false;
// Another owner (stock plugin code outside the update) requests a graph-2 node on body 1.
void Foreign(std::uint64_t node) {
    const std::uintptr_t buffer = Buffer(1);
    Put<std::uint16_t>(buffer, static_cast<std::uint16_t>(Get<std::uint16_t>(buffer) | 0x4));
    Put<std::uint16_t>(buffer + 2, static_cast<std::uint16_t>(Get<std::uint16_t>(buffer + 2) | 0x4));
    Put<std::uint64_t>(buffer + 0x20 + 2 * 0x28 + 0x20, node);
    pending = node;
}
std::uintptr_t BufferOf(void* cs, std::uint32_t body) {
    const auto c = reinterpret_cast<std::uintptr_t>(cs);
    const std::uintptr_t entry = Get<std::uintptr_t>(c + 0x10) + (body - Get<std::uint32_t>(c + 0xc)) * 0xc0;
    return Get<std::uintptr_t>(entry + 0x98);
}
bool FakeExist(void* cs, std::uint32_t body, std::uint32_t graph, std::uint64_t node, const std::uint64_t* tags) {
    calls.push_back({'e', reinterpret_cast<std::uintptr_t>(cs), body, graph, node, 0.0f, 0, tags && tags[0] == 0});
    if (shutdown_on_exist) pg::Shutdown();
    return exist_result;
}
void Request(void* cs, std::uint32_t body, std::uint32_t graph, std::uint64_t node, bool reserve) {
    const std::uintptr_t buffer = BufferOf(cs, body);
    const std::uint16_t bit = static_cast<std::uint16_t>(1u << graph);
    Put<std::uint16_t>(buffer, static_cast<std::uint16_t>(Get<std::uint16_t>(buffer) | bit));
    Put<std::uint16_t>(buffer + 2, static_cast<std::uint16_t>(Get<std::uint16_t>(buffer + 2) | bit));
    Put<std::uint16_t>(buffer + 4, static_cast<std::uint16_t>(reserve ? Get<std::uint16_t>(buffer + 4) | bit : Get<std::uint16_t>(buffer + 4) & ~bit));
    Put<std::uint64_t>(buffer + 0x20 + graph * 0x28 + 0x20, node);
    if (touch_viewer) Put<std::uint16_t>(Buffer(0), static_cast<std::uint16_t>(Get<std::uint16_t>(Buffer(0)) | 0x4));
    pending = node;
}
void FakeSet(void* cs, std::uint32_t body, std::uint32_t graph, std::uint64_t node, float blend, std::uint32_t option) {
    calls.push_back({'s', reinterpret_cast<std::uintptr_t>(cs), body, graph, node, blend, option, true});
    Request(cs, body, graph, node, false);
}
void FakeReserve(void* cs, std::uint32_t body, std::uint32_t graph, std::uint64_t node, float blend, std::uint32_t option) {
    calls.push_back({'r', reinterpret_cast<std::uintptr_t>(cs), body, graph, node, blend, option, true});
    Request(cs, body, graph, node, true);
}
bool FakeMotionEnd(void* cs, std::uint32_t body, std::uint32_t graph) {
    calls.push_back({'m', reinterpret_cast<std::uintptr_t>(cs), body, graph, 0, 0.0f, 0, true});
    if (foreign_on_end) Foreign(foreign_on_end);
    if (shutdown_on_end) pg::Shutdown();
    return motion_end_result;
}
bool FakePlayEnd(void* cs, std::uint32_t body, std::uint32_t layer) {
    calls.push_back({'p', reinterpret_cast<std::uintptr_t>(cs), body, layer, 0, 0.0f, 0, true});
    return play_end_result;
}
bool FaultingExist(void*, std::uint32_t, std::uint32_t, std::uint64_t, const std::uint64_t*) {
    volatile int* bad = nullptr;
    return *bad != 0;
}
// The original update: applies the pending request to body 1's graph 2.
unsigned originals = 0;
std::uintptr_t last_entry = 0;
std::uintptr_t FakeOriginal(void* entry) {
    ++originals;
    last_entry = reinterpret_cast<std::uintptr_t>(entry);
    if (pending && reinterpret_cast<std::uintptr_t>(entry) == W(ENTRY1)) {
        Put<std::uintptr_t>(W(UPPER1) + 0x1a0, NodeFor(pending));
        Put<std::uint16_t>(Buffer(1), 0);
        Put<std::uint16_t>(Buffer(1) + 2, 0);
        Put<std::uint16_t>(Buffer(1) + 4, 0);
        pending = 0;
    }
    SetLastError(0x5150);
    return 0xABCD;
}

pg::detail::Calls Fakes() {
    pg::detail::Calls c{};
    c.exist = FakeExist;
    c.set = FakeSet;
    c.reserve = FakeReserve;
    c.motion_end = FakeMotionEnd;
    c.play_end = FakePlayEnd;
    return c;
}

std::uint64_t now = 100000;
std::uint32_t epoch = 1;
pg::Command Cmd(std::uint32_t seq = 0, gp::Kind kind = gp::Kind::pickup, std::uint64_t node = kPickup,
                gp::Method method = gp::Method::set_current, float blend = 0.5f) {
    pg::Command c{};
    c.enabled = true;
    c.epoch = epoch;
    c.now_ms = now;
    c.request = pg::Request{seq, seq ? kind : gp::Kind::none, seq ? node : 0, seq ? method : gp::Method::none, blend, now};
    c.remote_known = true;
    c.remote_speed = 0;
    return c;
}
pg::Status Step(const pg::Command& c) {
    pg::Status s{};
    pg::Step(c, s);
    return s;
}
std::uintptr_t Body1(std::uint64_t at) { return pg::ForwardUpdate(FakeOriginal, reinterpret_cast<void*>(W(ENTRY1)), image + pg::kCallerRva, at); }
void Reset() {
    pg::detail::ResetForTest();
    BuildWorld();
    pg::detail::BindForTest(image, Fakes());
    calls.clear();
    exist_result = true;
    motion_end_result = play_end_result = touch_viewer = false;
    pending = 0;
    foreign_on_end = 0;
    shutdown_on_end = shutdown_on_exist = false;
    originals = 0;
    ++epoch;
}
const pg::Receipt* Find(const pg::Status& s, pg::Stage stage) {
    for (unsigned i = 0; i < s.count; ++i)
        if (s.receipts[i].stage == stage) return &s.receipts[i];
    return nullptr;
}

void TestResolve() {
    Reset();
    std::uintptr_t entry = 0;
    Check(!pg::detail::ResolveForTest(image, entry) && entry == W(ENTRY1), "body 1's entry resolves");
    struct Case { const char* want; void (*mutate)(); };
    const Case cases[] = {
        {"player", [] { Put<std::uintptr_t>(W(P), image + 0x10); }},
        {"online", [] { Put<std::uint32_t>(W(P) + 0x204, 0x101); }},
        {"online", [] { Put<std::uintptr_t>(W(P) + 0xa8, 1); }},
        {"count", [] { Put<std::uint32_t>(W(P) + 0x214, 1); }},
        {"q", [] { Put<std::uintptr_t>(W(Q), 0); }},
        {"ids", [] { Put<std::uint16_t>(W(IDS) + 2, 2); }},
        {"pool", [] { Put<std::uint8_t>(W(POOLS) + 1, 0); }},
        {"block", [] { Put<std::uint32_t>(W(BLOCKS) + 0x6a8 + 8, 2); }},
        {"ps", [] { Put<std::uint32_t>(W(PS1) + 8, 0); }},
        {"cs", [] { Put<std::uint32_t>(W(CS1) + 0xc, 0); }},
        {"entry", [] { Put<std::uint32_t>(W(ENTRY1) + 0xb0, 3); }},
        {"buffer", [] { Put<std::uint32_t>(Buffer(1) + 0xc, 0x30); }},
        {"graph", [] { Put<std::uintptr_t>(W(UPPER1), image + 0x10); }},
    };
    for (const Case& c : cases) {
        BuildWorld();
        c.mutate();
        const char* why = pg::detail::ResolveForTest(image, entry);
        Check(why && !std::strcmp(why, c.want) && !entry, c.want);
    }
    BuildWorld();
}

void TestFilter() {
    pg::detail::ResetForTest();
    pg::Status s = Step(Cmd());
    Check(!s.installed && s.resolve && !std::strcmp(s.resolve, "not_installed"), "not installed: no entry in the filter");
    Reset();
    s = Step(Cmd());
    Check(s.installed && !s.live && !s.active && !s.count && !s.resolve, "bound without a hook: installed, not live, body 1 resolved");
    SetLastError(0x77);
    Check(pg::ForwardUpdate(FakeOriginal, reinterpret_cast<void*>(W(ENTRY0)), image + pg::kCallerRva, now) == 0xABCD && originals == 1 &&
              last_entry == W(ENTRY0),
          "body 0's update passes straight through");
    Check(pg::ForwardUpdate(FakeOriginal, reinterpret_cast<void*>(W(ENTRY1)), image + 0x123456, now) == 0xABCD && originals == 2,
          "another caller passes through");
    s = Step(Cmd());
    Check(s.body_calls == 0 && s.count == 0 && calls.empty(), "nothing ran for either");
    SetLastError(0x99);
    Check(Body1(now) == 0xABCD && GetLastError() == 0x5150 && originals == 3, "body 1: the original's rax and last error");
    s = Step(Cmd());
    Check(s.body_calls == 1 && s.count == 0 && calls.empty(), "no request: no call");
}

void TestPickupFlow() {
    Reset();
    const std::uintptr_t viewer_before = Get<std::uint16_t>(Buffer(0));
    Step(Cmd(1));
    now += 16;
    Check(Body1(now) == 0xABCD, "forwarded");
    Check(calls.size() == 2 && calls[0].what == 'e' && calls[0].cs == W(CS1) && calls[0].body == 1 && calls[0].graph == 2 && calls[0].node == kPickup &&
              calls[0].tags_ok,
          "IsGraphExistNode(cs1, 1, 2, node, {0,0})");
    Check(calls[1].what == 's' && calls[1].cs == W(CS1) && calls[1].body == 1 && calls[1].graph == 2 && calls[1].node == kPickup &&
              calls[1].blend == 0.5f && calls[1].option == 0,
          "SetCurrentNode(cs1, 1, 2, node, 0.5, 0)");
    pg::Status s = Step(Cmd(1));
    const pg::Receipt* sub = Find(s, pg::Stage::submit);
    const pg::Receipt* app = Find(s, pg::Stage::applied);
    Check(sub && sub->seq == 1 && sub->exist && sub->kind == gp::Kind::pickup && sub->body_act == 1, "submit receipt");
    Check(app && app->ok && app->current == kPickup, "applied: graph 2 shows the pickup after the update");
    Check(s.active && s.active_seq == 1 && s.submits == 1, "active");
    Check(Get<std::uint16_t>(Buffer(0)) == viewer_before, "body 0's request masks untouched");
    // The same seq again is not resubmitted.
    calls.clear();
    now += 16;
    Body1(now);
    Check(calls.empty(), "one attempt per seq; no clip test before kMinPlayMs");
    now += 200;
    Body1(now);
    Check(calls.size() == 1 && calls[0].what == 'm' && calls[0].body == 1 && calls[0].graph == 2, "the pickup end test is vt+0x228(cs1, 1, 2)");
    motion_end_result = true;
    calls.clear();
    now += 16;
    Body1(now);
    Check(calls.size() == 2 && calls[1].what == 's' && calls[1].node == kNull && calls[1].blend == 0.0f && calls[1].cs == W(CS1),
          "motion end: SetCurrentNode(cs1, 1, 2, stateNull, 0.0, 0)");
    s = Step(Cmd());
    const pg::Receipt* end = Find(s, pg::Stage::ended);
    Check(end && end->via == gp::Via::end && !std::strcmp(end->cause, "clip") && end->null_sent && end->seq == 1 && end->ms >= 200,
          "ended; via=end; cause=clip; null=1");
    Check(!s.active && s.ends == 1, "idle again");
    now += 16;
    Body1(now);
    Check(Get<std::uintptr_t>(W(UPPER1) + 0x1a0) == Node(0), "the update applied stateNull");
}

void TestSignFlow() {
    Reset();
    Step(Cmd(7, gp::Kind::sign_go, kSignGo, gp::Method::reserve_reset, 1.0f));
    Body1(now);
    Check(calls.size() == 2 && calls[1].what == 'r' && calls[1].node == kSignGo && calls[1].blend == 1.0f && calls[1].option == 0,
          "ReserveToResetNode(cs1, 1, 2, node, 1.0, 0)");
    Check(Get<std::uint16_t>(Buffer(1)) == 0, "the fake update consumed the reserve");
    calls.clear();
    now += 200;
    pg::Command c = Cmd(7, gp::Kind::sign_go, kSignGo, gp::Method::reserve_reset, 1.0f);
    Step(c);
    Body1(now);
    Check(calls.size() == 1 && calls[0].what == 'p' && calls[0].body == 1 && calls[0].graph == 3, "the sign end test is vt+0x2B8(cs1, 1, 3)");
    // The remote runs: the sign ends (speed), our node returns to stateNull.
    c.remote_speed = 2;
    c.now_ms = now;
    Step(c);
    calls.clear();
    now += 16;
    Body1(now);
    pg::Status s = Step(Cmd());
    const pg::Receipt* end = Find(s, pg::Stage::ended);
    Check(end && end->via == gp::Via::end && !std::strcmp(end->cause, "speed") && end->null_sent, "ended; cause=speed");
    // Deadline.
    Reset();
    Step(Cmd(8, gp::Kind::sign_stop, kSignGo, gp::Method::reserve_reset, 1.0f));
    Body1(now);
    bool timeout = false, early = false;
    std::uint32_t ended_ms = 0;
    for (int i = 0; i < 130; ++i) {
        now += 16;
        s = Step(Cmd(8, gp::Kind::sign_stop, kSignGo, gp::Method::reserve_reset, 1.0f));
        for (unsigned k = 0; k < s.count; ++k)
            if (s.receipts[k].stage == pg::Stage::ended) {
                if (s.receipts[k].via == gp::Via::timeout && s.receipts[k].null_sent) { timeout = true; ended_ms = s.receipts[k].ms; }
                else early = true;
            }
        Body1(now);
    }
    Check(timeout && !early && ended_ms >= 2000 && ended_ms < 2100, "the 2000 ms deadline ends it with stateNull");
}

void TestOverride() {
    Reset();
    Step(Cmd(3));
    Body1(now);
    Step(Cmd(3));
    // Another owner takes graph 2 (a native upper action of body 1).
    Put<std::uintptr_t>(W(UPPER1) + 0x1a0, NodeFor(kOther));
    calls.clear();
    now += 100;
    Body1(now);
    pg::Status s = Step(Cmd());
    Check(!Find(s, pg::Stage::ended) && s.active, "within the grace: still ours");
    now += 300;
    Step(Cmd());
    Body1(now);
    s = Step(Cmd());
    const pg::Receipt* end = Find(s, pg::Stage::ended);
    Check(end && end->via == gp::Via::override_ && !std::strcmp(end->cause, "node") && !end->null_sent && end->current == kOther,
          "another owner: override without a write");
    for (const Call& c : calls) Check(c.what != 's' && c.what != 'r', "no request after the override");
    // Lease lost while ours: the stateNull return.
    Reset();
    Step(Cmd(4));
    Body1(now);
    pg::Command c = Cmd();
    c.enabled = false;
    Step(c);
    calls.clear();
    now += 20;
    Body1(now);
    s = Step(c);
    end = Find(s, pg::Stage::ended);
    Check(end && end->via == gp::Via::override_ && !std::strcmp(end->cause, "lease") && end->null_sent && calls.size() == 1 && calls[0].node == kNull,
          "a disabled command returns our node to stateNull");
    // A new epoch drops a running gesture without any write.
    Reset();
    Step(Cmd(5));
    Body1(now);
    ++epoch;
    Step(Cmd());
    calls.clear();
    now += 20;
    Body1(now);
    s = Step(Cmd());
    end = Find(s, pg::Stage::ended);
    Check(end && !std::strcmp(end->cause, "epoch") && !end->null_sent && calls.empty() && !s.active, "a new epoch: dropped, no write");
}

void Refused(const char* want, void (*mutate)(), pg::Command (*make)() = nullptr) {
    Reset();
    if (mutate) mutate();
    const pg::Command c = make ? make() : Cmd(9);
    Step(c);
    Body1(now);
    const pg::Status s = Step(Cmd());
    const pg::Receipt* r = Find(s, pg::Stage::refused);
    Check(r && r->seq == 9 && r->reason && !std::strcmp(r->reason, want) && !s.active && s.refusals == 1, want);
    for (const Call& call : calls) Check(call.what != 's' && call.what != 'r', "a refusal never submits");
}

void TestRefusals() {
    Refused("box", [] { Put<std::uint16_t>(W(ACT) + 2, 0x4D); });
    Refused("body_act", [] { Put<std::uint16_t>(W(ACT) + 2, 21); });
    Refused("body_q28", [] { Put<std::uint8_t>(W(Q28) + 1, 1); });
    Refused("body_upper", [] { Put<std::uint8_t>(W(UPPER) + 1, 5); });
    Refused("no_node", [] { exist_result = false; });
    Refused("slots", [] { Put<std::uintptr_t>(image + 0x22e6070 + 0x160, image + 0x1234); });
    Refused("lease", nullptr, [] { pg::Command c = Cmd(9); c.now_ms = now - 400; return c; });
    Refused("lease", nullptr, [] { pg::Command c = Cmd(9); c.enabled = false; return c; });
    Refused("stale", nullptr, [] { pg::Command c = Cmd(9); c.request.issued_ms = now - 600; return c; });
    Refused("method", nullptr, [] { pg::Command c = Cmd(9); c.request.method = gp::Method::none; return c; });
    // Body 1's identity breaks between the game thread's publication and the update.
    Reset();
    Step(Cmd(9));
    Put<std::uint32_t>(W(PS1) + 8, 0);
    Body1(now);
    pg::Status s = Step(Cmd());
    const pg::Receipt* r = Find(s, pg::Stage::refused);
    Check(r && !std::strcmp(r->reason, "ps") && calls.empty(), "the update re-resolves body 1 (ps)");
    Check(s.resolve && !std::strcmp(s.resolve, "ps"), "the step reports the identity refusal");
    // Busy: a second request while one runs.
    Reset();
    Step(Cmd(1));
    Body1(now);
    Step(Cmd(2));
    calls.clear();
    now += 16;
    Body1(now);
    s = Step(Cmd());
    r = Find(s, pg::Stage::refused);
    Check(r && r->seq == 2 && !std::strcmp(r->reason, "busy") && s.active && s.active_seq == 1, "busy while a gesture runs");
}

void TestUncertain() {
    Reset();
    touch_viewer = true;
    Step(Cmd(1));
    Body1(now);
    pg::Status s = Step(Cmd());
    const pg::Receipt* u = Find(s, pg::Stage::uncertain);
    Check(u && !std::strcmp(u->reason, "viewer_changed") && s.uncertain && !s.active && !Find(s, pg::Stage::submit), "the viewer's masks moved: uncertain");
    touch_viewer = false;
    Step(Cmd(2));
    calls.clear();
    Body1(now);
    s = Step(Cmd());
    const pg::Receipt* r = Find(s, pg::Stage::refused);
    Check(r && !std::strcmp(r->reason, "uncertain") && calls.empty(), "latched: no call this epoch");
    ++epoch;
    Step(Cmd(3));
    Body1(now);
    s = Step(Cmd());
    Check(Find(s, pg::Stage::submit) && !s.uncertain, "a new epoch clears the latch");
}

// Another owner's graph-2 request still waits in body 1's buffer at the
// update's entry (stock plugin code submits outside the motion update, and
// SetRequest / ReserveToReset overwrite a waiting record): never overwritten.
void TestPending() {
    Reset();
    Foreign(kOther);
    Step(Cmd(9));
    Body1(now);
    pg::Status s = Step(Cmd());
    const pg::Receipt* r = Find(s, pg::Stage::refused);
    Check(r && r->seq == 9 && r->reason && !std::strcmp(r->reason, "pending") && r->exist && !s.active && s.refusals == 1,
          "a waiting foreign request: the submission is refused (pending)");
    for (const Call& c : calls) Check(c.what != 's' && c.what != 'r', "pending: never submitted");
    Check(Get<std::uintptr_t>(W(UPPER1) + 0x1a0) == NodeFor(kOther), "the update applied the other owner's request");
    // A running gesture: the waiting request ends it at once, without the stateNull return.
    Reset();
    Step(Cmd(3));
    Body1(now);
    Step(Cmd(3));
    motion_end_result = true;
    Foreign(kOther);
    calls.clear();
    now += 200;
    Body1(now);
    s = Step(Cmd());
    const pg::Receipt* end = Find(s, pg::Stage::ended);
    Check(end && end->via == gp::Via::override_ && !std::strcmp(end->cause, "pending") && !end->null_sent && end->current == kPickup && !s.active,
          "a waiting foreign request ends ours: override, pending, no write");
    Check(calls.empty(), "no end test and no request while another owner's request waits");
    Check(Get<std::uintptr_t>(W(UPPER1) + 0x1a0) == NodeFor(kOther), "the other owner's request survives the end");
    // The lease is lost on the same update: still no write.
    Reset();
    Step(Cmd(4));
    Body1(now);
    pg::Command c = Cmd();
    c.enabled = false;
    Step(c);
    Foreign(kOther);
    calls.clear();
    now += 20;
    Body1(now);
    s = Step(c);
    end = Find(s, pg::Stage::ended);
    Check(end && !std::strcmp(end->cause, "pending") && !end->null_sent && calls.empty(), "a lost lease with a waiting request: no write");
    // The request arrives during the end test: the re-read right before the write keeps it.
    Reset();
    Step(Cmd(5));
    Body1(now);
    Step(Cmd(5));
    motion_end_result = true;
    foreign_on_end = kOther;
    calls.clear();
    now += 200;
    Body1(now);
    s = Step(Cmd());
    end = Find(s, pg::Stage::ended);
    Check(end && end->via == gp::Via::end && !std::strcmp(end->cause, "clip") && !end->null_sent && end->reason && !std::strcmp(end->reason, "pending"),
          "a request arriving during the end test: ended without the write (note=pending)");
    Check(calls.size() == 1 && calls[0].what == 'm', "only the end test ran");
    Check(Get<std::uintptr_t>(W(UPPER1) + 0x1a0) == NodeFor(kOther), "the late request is applied, not overwritten");
}

// The game window closes (PartnerShutdown on another thread): no game call and
// no write from then on, a running gesture included.
void TestShutdown() {
    Reset();
    Step(Cmd(1));
    Body1(now);
    Step(Cmd(1));
    pg::Shutdown();
    pg::Command c = Cmd();
    c.enabled = false;  // the lost lease would otherwise return our node to stateNull
    const std::uint32_t body_calls = Step(c).body_calls;
    calls.clear();
    const unsigned before = originals;
    now += 300;
    SetLastError(0x42);
    Check(Body1(now) == 0xABCD && GetLastError() == 0x5150 && originals == before + 1, "shut down: the original update still runs");
    pg::Status s = Step(Cmd(2));
    Check(calls.empty() && s.count == 0 && s.body_calls == body_calls, "shut down: straight through, no call, no receipt");
    now += 16;
    Body1(now);
    s = Step(Cmd());
    Check(calls.empty() && s.count == 0 && Get<std::uintptr_t>(W(UPPER1) + 0x1a0) == NodeFor(kPickup),
          "shut down: a new request is never submitted and our node is never returned");
    // The window starts closing during the end test: dropped without the stateNull return.
    Reset();
    Step(Cmd(3));
    Body1(now);
    Step(Cmd(3));
    motion_end_result = true;
    shutdown_on_end = true;
    calls.clear();
    now += 200;
    Body1(now);
    s = Step(Cmd());
    const pg::Receipt* end = Find(s, pg::Stage::ended);
    Check(end && end->via == gp::Via::override_ && !std::strcmp(end->cause, "shutdown") && !end->null_sent && !s.active && calls.size() == 1 &&
              calls[0].what == 'm',
          "closing during the end test: override, shutdown, no write");
    // ... or during the exist query: refused, never submitted.
    Reset();
    shutdown_on_exist = true;
    Step(Cmd(4));
    Body1(now);
    s = Step(Cmd());
    const pg::Receipt* r = Find(s, pg::Stage::refused);
    Check(r && r->seq == 4 && r->reason && !std::strcmp(r->reason, "shutdown") && !s.active && calls.size() == 1 && calls[0].what == 'e',
          "closing during the exist query: refused (shutdown), never submitted");
}

void TestFault() {
    Reset();
    pg::detail::Calls c = Fakes();
    c.exist = FaultingExist;
    pg::detail::BindForTest(image, c);
    Step(Cmd(1));
    SetLastError(0x31);
    Check(Body1(now) == 0xABCD && GetLastError() == 0x5150, "a faulting call still forwards");
    pg::Status s = Step(Cmd());
    const pg::Receipt* f = Find(s, pg::Stage::fault);
    Check(f && !std::strcmp(f->reason, "exist_call") && s.faulted, "fault latched");
    Step(Cmd(2));
    const unsigned before = originals;
    Body1(now);
    s = Step(Cmd());
    Check(originals == before + 1 && s.count == 0 && s.body_calls == 1, "faulted: pure passthrough for the process");
    Check(s.resolve && !std::strcmp(s.resolve, "faulted"), "faulted: no entry in the filter");
}

} // namespace

int main() {
    image = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, kImageSpan, MEM_RESERVE, PAGE_NOACCESS));
    Check(image != 0, "reserve the fake image");
    Check(VirtualAlloc(reinterpret_cast<void*>(image + 0x2a82000), 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit the player slot page");
    Check(VirtualAlloc(reinterpret_cast<void*>(image + 0x22e6000), 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit the cs vtable page");
    w = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(w != nullptr, "world");
    TestResolve();
    TestFilter();
    TestPickupFlow();
    TestSignFlow();
    TestOverride();
    TestRefusals();
    TestUncertain();
    TestPending();
    TestShutdown();
    TestFault();
    pg::detail::ResetForTest();
    std::printf("partner_gesture_hook_tests: %u checks passed\n", checks);
    return 0;
}
