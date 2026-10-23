// The gesture lab lever (docs/native-puppet.md "Gestures", "Lab lever: knock and
// hand signs"), native side on a fake image and fake player: the identity
// checks, the stock slot check, the preconditions, the one call with this
// copy's plugin and id 0, the readback, instance 1's guard (other_changed latch)
// and the SEH fault latch. No game code runs: the called function is a fake
// bound through detail::SetCallForTest (the slot is still checked).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "mgscoop/partner_gesture_lever.hpp"

namespace {
namespace gl = mgscoop::partner_gesture_lever;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u: %s\n", checks, what);
        std::exit(1);
    }
}
bool Same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

constexpr std::size_t kImageSpan = 0x2A84000;
std::uintptr_t image = 0;
unsigned char* w = nullptr;  // the fake world
template <class T> void Put(std::uintptr_t at, T v) { std::memcpy(reinterpret_cast<void*>(at), &v, sizeof(v)); }
template <class T> T Get(std::uintptr_t at) { T v{}; std::memcpy(&v, reinterpret_cast<const void*>(at), sizeof(v)); return v; }
std::uintptr_t W(std::size_t off) { return reinterpret_cast<std::uintptr_t>(w) + off; }

// Offsets in the world.
constexpr std::size_t P = 0x0, Q = 0x400, ACT = 0x480, UPPER = 0x490, Q28 = 0x4A0, STANCE = 0x4B0, SPEED = 0x4C0, IDS = 0x4D0,
                      POOLS = 0x4E0, TABLE = 0x4F0, BLOCKS = 0x1000, PS0 = 0x2000, PS1 = 0x2800, RECS = 0x3000, PLUGIN0 = 0x3400,
                      PLUGIN1 = 0x3600, BASE0 = 0x3800, BASE1 = 0x3880, ROW0 = 0x3900, ROW1 = 0x3A00;
std::uintptr_t Blk(unsigned k) { return W(BLOCKS) + k * 0x6a8; }
std::uintptr_t Row(unsigned k) { return W(k ? ROW1 : ROW0); }

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
    Put<std::uintptr_t>(W(Q) + 0x30, W(STANCE));
    Put<std::uintptr_t>(W(Q) + 0x38, W(SPEED));
    Put<std::uint16_t>(W(ACT), 1);      // this copy stands
    Put<std::uint16_t>(W(ACT) + 2, 1);  // body 1 stands
    Put<std::uint8_t>(W(STANCE), 1);
    Put<std::uint8_t>(W(STANCE) + 1, 1);
    Put<std::uint16_t>(W(IDS), 0);
    Put<std::uint16_t>(W(IDS) + 2, 1);
    Put<std::uint8_t>(W(POOLS), 0);
    Put<std::uint8_t>(W(POOLS) + 1, 1);
    const std::size_t ps[2] = {PS0, PS1}, plugin[2] = {PLUGIN0, PLUGIN1}, base[2] = {BASE0, BASE1}, row[2] = {ROW0, ROW1};
    for (unsigned k = 0; k < 2; ++k) {
        const std::uintptr_t blk = Blk(k);
        Put<std::uintptr_t>(W(TABLE) + k * 8, blk);
        Put<std::uintptr_t>(blk, image + 0x2295210);
        Put<std::uintptr_t>(blk + 0x138, W(P));
        Put<std::uint32_t>(blk + 8, 1);
        Put<std::uint32_t>(blk + 0xc, k);
        Put<std::uintptr_t>(blk + 0x78, W(ps[k]));
        Put<std::uintptr_t>(blk + 0xf8, W(plugin[k]));
        Put<std::uintptr_t>(W(ps[k]), image + (k ? 0x23b0c70 : 0x23b09f0));
        Put<std::uint32_t>(W(ps[k]) + 8, k);
        Put<std::uintptr_t>(W(ps[k]) + 0x408, blk);
        Put<std::uintptr_t>(W(ps[k]) + 0x280, W(RECS) + k * 0x100);
        Put<std::uintptr_t>(W(plugin[k]), image + gl::kPluginVtableRva);
        Put<std::uintptr_t>(W(plugin[k]) + 8, blk);
        Put<std::uintptr_t>(W(plugin[k]) + 0x38, W(base[k]));
        Put<std::uint32_t>(W(base[k]) + 0x24, k);
        Put<std::uintptr_t>(W(plugin[k]) + 0x68, W(row[k]));
    }
    Put<std::uint32_t>(W(RECS) + 0xc, 0x00100000);
    for (gl::Request r : {gl::Request::knock, gl::Request::sign_go, gl::Request::sign_stop}) {
        const gl::Slot s = gl::SlotFor(r);
        Put<std::uintptr_t>(image + gl::kPluginVtableRva + s.offset, image + s.target_rva);
    }
    Put<std::uintptr_t>(image + gl::kPluginVtableRva + 0x58, image + gl::kPollingRva);
}

// --- The fake stock request (RequestKnock / Go / Stop: 0x11E5100 / 0x11E5060 / 0x11E50B0).
unsigned calls = 0;
std::uintptr_t called_plugin = 0;
std::uint32_t called_id = 0xFFFFFFFF;
std::uint8_t fake_kind = 1;
bool touch_other = false, touch_upper = false;
void FakeRequest(void* plugin, std::uint32_t id) {
    ++calls;
    called_plugin = reinterpret_cast<std::uintptr_t>(plugin);
    called_id = id;
    const std::uintptr_t p = called_plugin;
    const std::uintptr_t row = Get<std::uintptr_t>(p + 0x68) + (id - Get<std::uint32_t>(Get<std::uintptr_t>(p + 0x38) + 0x24)) * 0xE0;
    Put<std::uint8_t>(row + 0xd0, static_cast<std::uint8_t>(Get<std::uint8_t>(row + 0xd0) | 1));
    Put<std::uint8_t>(row + 0x24, fake_kind);
    Put<std::uint64_t>(row + 0x18, 777);
    Put<std::uint32_t>(row + 0x20, 2);
    if (touch_other) Put<std::uint8_t>(Row(1) + 0x24, 1);
    if (touch_upper) Put<std::uint8_t>(W(UPPER) + 1, 0x17);
}
void FaultRequest(void*, std::uint32_t) {
    ++calls;
    volatile std::uint8_t* bad = reinterpret_cast<volatile std::uint8_t*>(static_cast<std::uintptr_t>(0x10));
    *bad = 1;
}

void Reset(gl::detail::RequestFn fn = FakeRequest) {
    BuildWorld();
    calls = 0;
    called_plugin = 0;
    called_id = 0xFFFFFFFF;
    fake_kind = 1;
    touch_other = touch_upper = false;
    gl::detail::SetCallForTest(fn);
}

void TestRequests() {
    struct Case { gl::Request request; std::uint8_t kind; };
    for (const Case& c : {Case{gl::Request::knock, 1}, Case{gl::Request::sign_go, 2}, Case{gl::Request::sign_stop, 3}}) {
        Reset();
        fake_kind = c.kind;
        const gl::Outcome o = gl::Fire(image, c.request);
        Check(o.refusal == nullptr && o.called && o.other_same && o.request == c.request && o.id == 0, "requested");
        Check(calls == 1 && called_plugin == W(PLUGIN0) && called_id == 0, "one call: this copy's plugin, id 0");
        Check(o.row_kind == c.kind && (o.row_d0 & 1) && o.row_life == 2 && o.row_frame == 777, "the row readback");
        Check(o.local.act == 1 && o.local.stance == 1 && o.local.upper == 0 && o.recc == 0x00100000, "the local arrays and PS rec");
        Check(Get<std::uint8_t>(Row(1) + 0x24) == 0 && (Get<std::uint8_t>(Row(1) + 0xd0) & 1) == 0 && Get<std::uint8_t>(W(UPPER) + 1) == 0,
              "instance 1 untouched");
        Check(!gl::Latched(), "no latch");
    }
}

void TestIdentityRefusals() {
    struct Case { const char* reason; void (*mutate)(); };
    const Case cases[] = {
        {"player", [] { Put<std::uintptr_t>(W(P), image + 0x1234); }},
        {"player", [] { Put<std::uintptr_t>(image + 0x2a82c30, 0); }},
        {"online", [] { Put<std::uint32_t>(W(P) + 0x204, 0x2101); }},
        {"online", [] { Put<std::uintptr_t>(W(P) + 0xa8, W(Q)); }},
        {"count", [] { Put<std::uint32_t>(W(P) + 0x214, 1); }},  // this copy is not id 0
        {"count", [] { Put<std::uint32_t>(W(P) + 0x208, 1); }},
        {"q", [] { Put<std::uintptr_t>(W(Q), image + 0x1234); }},
        {"ids", [] { Put<std::uint16_t>(W(IDS) + 2, 2); }},
        {"pool", [] { Put<std::uint8_t>(W(POOLS) + 1, 0); }},
        {"block", [] { Put<std::uintptr_t>(Blk(0), image + 0x1234); }},
        {"block", [] { Put<std::uint32_t>(Blk(1) + 0xc, 0); }},
        {"plugin", [] { Put<std::uintptr_t>(W(PLUGIN0), image + 0x1234); }},
        {"plugin", [] { Put<std::uintptr_t>(W(PLUGIN1) + 8, Blk(0)); }},  // block 1's plugin claims block 0
        {"plugin", [] { Put<std::uintptr_t>(Blk(1) + 0xf8, 0); }},
        {"row", [] { Put<std::uint32_t>(W(BASE0) + 0x24, 1); }},
        {"row", [] { Put<std::uintptr_t>(W(PLUGIN0) + 0x68, 0); }},
        {"plugin", [] { Put<std::uintptr_t>(W(PLUGIN1) + 0x68, W(ROW0)); }},  // a shared row
        {"ps", [] { Put<std::uintptr_t>(W(PS0) + 0x408, Blk(1)); }},
        {"ps", [] { Put<std::uintptr_t>(W(PS0) + 0x280, 0); }},
        {"slot", [] { Put<std::uintptr_t>(image + gl::kPluginVtableRva + 0x28, image + 0x11E5060); }},
        {"slot", [] { Put<std::uintptr_t>(image + gl::kPluginVtableRva + 0x58, image + 0x1234); }},
        {"arrays", [] { Put<std::uintptr_t>(W(Q) + 0x38, 0); }},
    };
    for (const Case& c : cases) {
        Reset();
        c.mutate();
        const gl::Outcome o = gl::Fire(image, gl::Request::knock);
        Check(Same(o.refusal, c.reason), c.reason);
        Check(calls == 0 && !o.called && !gl::Latched(), "a refusal makes no call and no latch");
    }
}

void TestPreconditions() {
    struct Case { gl::Request request; const char* reason; void (*mutate)(); };
    const Case cases[] = {
        {gl::Request::knock, "act", [] { Put<std::uint16_t>(W(ACT), 0x4D); }},
        {gl::Request::knock, "upper", [] { Put<std::uint8_t>(W(UPPER), 5); }},
        {gl::Request::knock, "q28", [] { Put<std::uint8_t>(W(Q28), 1); }},
        {gl::Request::knock, "speed", [] { Put<std::uint8_t>(W(SPEED), 2); }},
        {gl::Request::sign_go, "speed", [] { Put<std::uint8_t>(W(SPEED), 2); }},
        {gl::Request::sign_go, "act", [] { Put<std::uint16_t>(W(ACT), 0x79); }},
        {gl::Request::sign_stop, "stance", [] { Put<std::uint8_t>(W(STANCE), 4); }},
        // The world's PS rec+0xC has bit 20 set: a crawl reads as GetStance 4.
        {gl::Request::sign_go, "stance", [] { Put<std::uint8_t>(W(STANCE), 3); }},
        {gl::Request::sign_stop, "rec_c28", [] { Put<std::uint32_t>(W(RECS) + 0xc, 0x10000000); }},
        {gl::Request::sign_stop, "upper", [] { Put<std::uint8_t>(W(UPPER), 0x19); }},
    };
    for (const Case& c : cases) {
        Reset();
        c.mutate();
        const gl::Outcome o = gl::Fire(image, c.request);
        Check(Same(o.refusal, c.reason) && calls == 0, c.reason);
    }
    // A crawl without rec+0xC bit 20 is GetStance 3: the sign is requested; the knock ignores both bits.
    Reset();
    Put<std::uint8_t>(W(STANCE), 3);
    Put<std::uint32_t>(W(RECS) + 0xc, 0);
    Check(gl::Fire(image, gl::Request::sign_go).refusal == nullptr && calls == 1, "a crawl without bit 20: sign requested");
    Reset();
    Put<std::uint8_t>(W(STANCE), 3);
    Check(gl::Fire(image, gl::Request::knock).refusal == nullptr && calls == 1, "a crawl with bit 20: knock requested");
    // Body 1's own state never gates this copy's request.
    Reset();
    Put<std::uint16_t>(W(ACT) + 2, 0x4D);
    Put<std::uint8_t>(W(UPPER) + 1, 0x15);
    Check(gl::Fire(image, gl::Request::knock).refusal == nullptr && calls == 1, "body 1's act / upper do not matter");
}

void TestGuards() {
    // The request moved instance 1's request fields: refused and latched until reset.
    Reset();
    touch_other = true;
    gl::Outcome o = gl::Fire(image, gl::Request::knock);
    Check(Same(o.refusal, "other_changed") && o.called && !o.other_same && gl::Latched(), "instance 1's row changed: latched");
    touch_other = false;
    o = gl::Fire(image, gl::Request::knock);
    Check(Same(o.refusal, "latched") && calls == 1, "a latched lever makes no further call");
    Reset();
    touch_upper = true;
    o = gl::Fire(image, gl::Request::sign_go);
    Check(Same(o.refusal, "other_changed") && gl::Latched(), "instance 1's upper byte changed: latched");
    // A fault in the call: refused and latched.
    Reset(FaultRequest);
    o = gl::Fire(image, gl::Request::knock);
    Check(Same(o.refusal, "fault") && calls == 1 && gl::Latched(), "a fault latches the lever");
    Reset();
    Check(gl::Fire(image, gl::Request::none).refusal != nullptr && calls == 0, "none: refused without a call");
    Check(Same(gl::Fire(0, gl::Request::knock).refusal, "player") && calls == 0, "no image: player");
}
} // namespace

int main() {
    image = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, kImageSpan, MEM_RESERVE, PAGE_NOACCESS));
    Check(image != 0, "reserve the fake image");
    Check(VirtualAlloc(reinterpret_cast<void*>(image + 0x2a82000), 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit the player slot page");
    Check(VirtualAlloc(reinterpret_cast<void*>(image + 0x23c5000), 0x1000, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit the plugin vtable page");
    w = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(w != nullptr, "the fake world");
    TestRequests();
    TestIdentityRefusals();
    TestPreconditions();
    TestGuards();
    gl::detail::SetCallForTest(nullptr);
    std::printf("partner_gesture_lever_fire_tests: %u checks passed\n", checks);
    return 0;
}
