// F2: the cardboard box on body 1 (docs/native-puppet.md "Cardboard box").
// 1. The pure policy (partner_cbox_policy.hpp): mapping, body sub, edges, verdicts, watches, recon hold, busy.
// 2. The real module (partner_cbox_equip Observe) on a fake world: a synthetic image (vtables and slots at their
//    contract RVAs, read through the module's own ReadProcessMemory reader) and fake game objects in local memory.
//    ChangeEquip 0x11AC2E0 and SetCurrentItemIndex 0x11AF9A0 are the only executable bytes: an absolute jump to the
//    fakes below, which play the native effects for id 1 (re-cbox.md section 3: body 1's item list and the +0x37
//    item nibble). No game code runs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "mgscoop/partner_cbox_equip.hpp"
#include "mgscoop/partner_cbox_policy.hpp"
#include "partner_player_contract.hpp"
#include "equipment_stock_writer_pins.hpp"
namespace pol = mgscoop::partner_cbox_policy;
namespace cb = mgscoop::partner_cbox_equip;
namespace c = mgscoop::partner_player_contract;
unsigned checks = 0;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "failed: %s\n", label);
        std::exit(1);
    }
}
bool Same(const char* a, const char* b) { return a && b ? std::strcmp(a, b) == 0 : a == b; }

// --- 1. Pure policy --------------------------------------------------------------------------------------------
void TestRanges() {
    Check(pol::BoxEquip(0x1C8) && pol::BoxEquip(0x1E2) && pol::BoxEquip(468) && !pol::BoxEquip(0x1C7) && !pol::BoxEquip(0x1E3) &&
              !pol::BoxEquip(0),
          "box ids 0x1C8..0x1E2 (the can-wear range check 0x10DF517)");
    Check(pol::BoxAct(77) && pol::BoxAct(78) && !pol::BoxAct(76) && !pol::BoxAct(79) && !pol::BoxAct(61), "box acts 77 / 78");
    static_assert(pol::kItemType == c::equip_type_item && pol::kBoxFirst == c::cbox_first && pol::kBoxLast == c::cbox_last &&
                      c::act_cbox == 77 && c::act_cbox_move == 78,
                  "the policy and the generated contract agree");
}
void TestMapping() {
    std::uint16_t box = 0;
    const std::uint16_t items[8] = {0, 468, 451, 492, 483, 484, 472, 466};
    const std::uint16_t stocks[8] = {0, 1, 3, 1, 1, 2, 1, 1};
    Check(pol::MapViewerBox(472, items, stocks, box) == pol::Mapped::same && box == 472, "the remote's own box when the viewer has it");
    Check(pol::MapViewerBox(0x1C8, items, stocks, box) == pol::Mapped::viewer_box && box == 468,
          "another remote box: the viewer's first box with stock");
    const std::uint16_t no_stock[8] = {0, 0, 3, 1, 1, 2, 0, 1};
    Check(pol::MapViewerBox(468, items, no_stock, box) == pol::Mapped::viewer_box && box == 466,
          "stock 0 never lends (GetItemStockCount would refuse the wake)");
    const std::uint16_t sub0_only[8] = {468, 451, 0, 0, 0, 0, 0, 0};
    const std::uint16_t all[8] = {5, 5, 5, 5, 5, 5, 5, 5};
    Check(pol::MapViewerBox(468, sub0_only, all, box) == pol::Mapped::none && box == 0, "sub 0 is never scanned");
    const std::uint16_t none[8] = {0, 451, 492, 0, 0, 0, 0, 0};
    Check(pol::MapViewerBox(468, none, all, box) == pol::Mapped::none && box == 0, "no box: viewer_no_box");
    Check(pol::MapViewerBox(451, items, stocks, box) == pol::Mapped::viewer_box && box == 468, "a non-box want maps to the viewer's box");
    Check(Same(pol::MappedName(pol::Mapped::same), "same") && Same(pol::MappedName(pol::Mapped::viewer_box), "viewer_box") &&
              Same(pol::MappedName(pol::Mapped::none), "none"),
          "mapped names");
    Check(pol::ViewerSub(466, items) == 7 && pol::ViewerSub(500, items) == 0 && pol::ViewerSub(0, items) == 0, "the viewer's sub of a box");
}
void TestBodySub() {
    std::uint8_t sub = 0xFF;
    const std::uint16_t empty[8] = {};
    Check(pol::BodySubFor(468, empty, sub) && sub == 1, "an empty body list: sub 1 (never sub 0)");
    const std::uint16_t holds[8] = {0, 451, 0, 468, 0, 0, 0, 0};
    Check(pol::BodySubFor(468, holds, sub) && sub == 3, "the sub already holding the box first (no new row write)");
    Check(pol::BodySubFor(472, holds, sub) && sub == 2, "else the first empty sub");
    const std::uint16_t full[8] = {0, 451, 492, 483, 484, 1, 2, 3};
    Check(!pol::BodySubFor(468, full, sub) && sub == 0, "no free sub: body_items_full (never the replace-current pickup)");
    const std::uint16_t full_with[8] = {0, 451, 492, 483, 484, 1, 2, 468};
    Check(pol::BodySubFor(468, full_with, sub) && sub == 7, "a full list that holds the box is fine");
    Check(!pol::BodySubFor(451, empty, sub), "only boxes");
}
void TestDecide() {
    pol::State s{};
    pol::Enter(s, 30010, 7000, 1);
    auto on = pol::Decide(s, {true, 1, 0, 468});
    Check(on.step == pol::Step::on && !on.reason, "a fresh edge with a mapped box: on");
    auto none = pol::Decide(s, {true, 1, 0, 0});
    Check(none.step == pol::Step::refuse && Same(none.reason, "viewer_no_box") && none.final, "no viewer box: final refusal");
    s.done = true;
    s.done_seq = 1;
    Check(pol::Decide(s, {true, 1, 0, 468}).step == pol::Step::keep, "one attempt per remote edge");
    Check(pol::Decide(s, {true, 2, 0, 468}).step == pol::Step::on, "the next edge attempts again");
    s.applied = 468;
    auto keep = pol::Decide(s, {true, 3, 468, 468});
    Check(keep.step == pol::Step::keep && Same(keep.reason, "current"), "never re-dispatch while the box is current");
    pol::State fresh{};
    pol::Enter(fresh, 30010, 7000, 1);
    Check(pol::Decide(fresh, {true, 1, 472, 472}).step == pol::Step::keep, "a box already current (not ours) is never re-dispatched");
    // Off: only for our box while current.
    auto off = pol::Decide(s, {false, 3, 468, 0});
    Check(off.step == pol::Step::off, "the remote's off: take our box off");
    Check(pol::Decide(s, {false, 3, 451, 0}).step == pol::Step::idle, "our box not current: nothing to take off");
    Check(pol::Decide(fresh, {false, 1, 472, 0}).step == pol::Step::idle, "a box we did not put on is never taken off");
    s.done_seq = 3;
    Check(pol::Decide(s, {false, 3, 468, 0}).step == pol::Step::keep, "one off attempt per edge");
    // Latches.
    pol::State u{};
    pol::Enter(u, 30010, 7000, 1);
    u.uncertain = true;
    auto ul = pol::Decide(u, {true, 1, 0, 468});
    Check(ul.step == pol::Step::refuse && Same(ul.reason, "uncertain_latched") && ul.final, "uncertain: no on");
    u.applied = 468;
    auto uo = pol::Decide(u, {false, 2, 468, 0});
    Check(uo.step == pol::Step::refuse && Same(uo.reason, "uncertain_latched"), "uncertain: not even an off");
    pol::State w{};
    pol::Enter(w, 30010, 7000, 1);
    w.watch_latched = true;
    auto wl = pol::Decide(w, {true, 1, 0, 468});
    Check(wl.step == pol::Step::refuse && Same(wl.reason, "viewer_changed_latched"), "a viewer-change latch: no new on");
    w.applied = 468;
    Check(pol::Decide(w, {false, 2, 468, 0}).step == pol::Step::off, "a viewer-change latch still takes our box off");
}
void TestEnter() {
    pol::State s{};
    pol::Enter(s, 30010, 7000, 4);
    s.done = true;
    s.done_seq = 9;
    s.applied = 468;
    s.uncertain = true;
    pol::Enter(s, 30010, 7000, 5);
    Check(!s.done && s.applied == 468 && s.uncertain && s.realize_gen == 5, "a re-realize re-arms the edge, keeps the box and latches");
    s.done = true;
    s.watch_latched = true;
    s.on_ms = 123;
    s.entered = true;
    pol::Enter(s, 30010, 9000, 5);
    Check(!s.done && !s.done_seq && !s.uncertain && !s.watch_latched && s.epoch == 9000 && s.applied == 468 && s.enter_logged &&
              !s.entered && !s.exit_logged && !s.on_ms,
          "a new mission with the same code (a new start epoch) clears the edge and the latches, carries our box (no watch)");
    s.uncertain = true;
    pol::Enter(s, 30020, 9000, 5);
    Check(!s.done && !s.uncertain && s.mission == 30020 && s.applied == 468, "a new mission code carries our box too");
    s.applied = 0;
    pol::Enter(s, 30010, 9000, 5);
    Check(!s.applied && !s.enter_logged && s.mission == 30010, "nothing carried without a box of ours");
    // A carried box is still ours: off when the remote is not boxed, kept when it is (never re-dispatched).
    s.applied = 468;
    Check(pol::Decide(s, {false, 0, 468, 0}).step == pol::Step::off, "a carried box comes off when the remote is not boxed");
    Check(pol::Decide(s, {true, 1, 468, 468}).step == pol::Step::keep, "a carried box stays while the remote is boxed");
}
void TestVerdicts() {
    Check(pol::FinalRefusal("viewer_no_box") && pol::FinalRefusal("type") && pol::FinalRefusal("body_items_full") &&
              pol::FinalRefusal("full_body_gate") && !pol::FinalRefusal("body_busy") && !pol::FinalRefusal("lease_changed") &&
              !pol::FinalRefusal("restriction") && !pol::FinalRefusal(nullptr) && !pol::FinalRefusal("type2"),
          "final refusals");
    pol::OnReadback r{};
    r.called = true;
    r.result = 0;
    r.same_viewer = r.same_body_slots = true;
    r.after_current = r.box = 468;
    Check(!pol::OnVerdict(r), "on: called, result 0, viewer and other slots unchanged, the box current");
    auto v = r;
    v.called = false;
    Check(Same(pol::OnVerdict(v), "seh_fault"), "a fault");
    v = r;
    v.result = -1;
    Check(Same(pol::OnVerdict(v), "dispatch_result"), "result -1 (busy predicate, pool 0xFF, type)");
    v = r;
    v.capture = "player_lease";
    Check(Same(pol::OnVerdict(v), "player_lease"), "the after-capture's refusal");
    v = r;
    v.same_viewer = false;
    Check(Same(pol::OnVerdict(v), "viewer_changed"), "the viewer changed in the call");
    v = r;
    v.same_body_slots = false;
    Check(Same(pol::OnVerdict(v), "body_slots_changed"), "another body-1 slot changed in the call");
    v = r;
    v.after_current = 0;
    Check(Same(pol::OnVerdict(v), "not_current"), "result 0 is not proof: the row decides");
    Check(!pol::LatchUncertain(nullptr) && !pol::LatchUncertain("not_current") && pol::LatchUncertain("viewer_changed") &&
              pol::LatchUncertain("seh_fault") && pol::LatchUncertain("dispatch_result") && pol::LatchUncertain("player_lease"),
          "every surprise latches except not_current");
    pol::OffReadback o{};
    o.called = true;
    o.same_viewer = o.same_body_slots = true;
    o.after_sub = 0;
    o.after_current = 0;
    Check(!pol::OffVerdict(o), "off: sub 0 current, nothing else changed");
    auto p = o;
    p.after_sub = 1;
    p.after_current = 468;
    Check(Same(pol::OffVerdict(p), "still_current"), "off: the box still current");
    p = o;
    p.same_viewer = false;
    Check(Same(pol::OffVerdict(p), "viewer_changed"), "off: the viewer changed");
    p = o;
    p.called = false;
    Check(Same(pol::OffVerdict(p), "seh_fault"), "off: a fault");
}
void TestWatches() {
    Check(pol::EntryVerdict(1000, 1000, 77) == pol::Entry::entered && pol::EntryVerdict(2499, 1000, 3) == pol::Entry::waiting &&
              pol::EntryVerdict(2500, 1000, 3) == pol::Entry::timeout && pol::EntryVerdict(1200, 1000, 78) == pol::Entry::entered,
          "the entry watch: 77/78 within 1500 ms");
    namespace f = pol::field;
    Check(!pol::WatchLatch(0, 1, 1), "no change: no latch");
    Check(pol::WatchLatch(f::stocks, 3, 3), "a stock change with the viewer's act unchanged: latch");
    Check(pol::WatchLatch(f::records | f::item_handle, 1, 1), "records / the slot-4 handle: latch");
    Check(!pol::WatchLatch(f::stocks, 3, 77), "the viewer's act changed (it acts itself): log only");
    Check(!pol::WatchLatch(f::vars_current | f::row_items, 3, 3), "the viewer selected another item: log only");
    Check(!pol::WatchLatch(f::row_current, 1, 1), "the viewer's own item nibble: log only");
    Check(!pol::WatchLatch(f::row_other | f::handles_other, 3, 3), "the viewer's weapons (row 0 outside the items, other handles): log only");
    Check(pol::WatchLatch(f::row_other | f::stocks, 3, 3), "a weapon switch does not hide an item stock change");
}
void TestReconHold() {
    Check(pol::ReconHold(true, true, 77, true, 5000, 0) && pol::ReconHold(true, true, 78, true, 5000, 0), "boxed and wanted: hold");
    Check(!pol::ReconHold(false, true, 77, true, 5000, 0), "key off: no hold (no behaviour change)");
    Check(!pol::ReconHold(true, false, 77, true, 5000, 0), "unread body: no hold");
    Check(!pol::ReconHold(true, true, 3, true, 5000, 0), "not boxed: no hold");
    Check(pol::ReconHold(true, true, 77, false, 5000, 4000) && !pol::ReconHold(true, true, 77, false, 7000, 4000),
          "after the remote's off: 3 s of grace");
    Check(!pol::ReconHold(true, true, 77, false, 5000, 0), "never wanted (a box we did not put on): no hold");
}
void TestBusy() {
    Check(!pol::BusyReason({}), "idle");
    Check(Same(pol::BusyReason({true, true, true}), "body_busy") && Same(pol::BusyReason({false, true, true}), "gun_writer_active") &&
              Same(pol::BusyReason({false, false, true}), "support_uncertain"),
          "busy reasons in order");
}

// --- 2. The module on a fake world -------------------------------------------------------------------------------
namespace world {
template <class T> void Put(std::uintptr_t a, T v) { std::memcpy(reinterpret_cast<void*>(a), &v, sizeof(v)); }
template <class T> T Get(std::uintptr_t a) {
    T v{};
    std::memcpy(&v, reinterpret_cast<const void*>(a), sizeof(v));
    return v;
}
std::uintptr_t Addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
constexpr std::size_t kSpan = 0x2C40000;
static_assert(c::vars_slot_rva + 8 < kSpan && c::player_slot_rva + 8 < kSpan && c::equip_types_rva + 8 + 2 * c::stock_ids < kSpan &&
                  c::change_equip_rva + 16 < kSpan && c::set_current_item_index_rva + 16 < kSpan,
              "the fake image covers every RVA the module reads");
#pragma pack(push, 1)
struct Request {
    std::uint64_t type;
    std::uint16_t equip, stock, stock_sub, ammo, ammo_sub;
    std::uint8_t suppression, flags;
};
#pragma pack(pop)
static_assert(sizeof(Request) == 20 && offsetof(Request, flags) == 19, "ChangeEquip's request (partner_cbox_equip.cpp)");
struct Objects {
    unsigned char player[0x400];
    unsigned char ids[8], pools[8];
    std::uintptr_t blocks[2];
    unsigned char buffer[0x6A8 * 2];  // body 1's block is buffer + 1*0x6A8
    unsigned char predicate[0x200], pred_rows[8], writer[0x20];
    unsigned char ctl[0x40], inventory[0xA0], engine[0x20], q[0x300];
    std::uint32_t handles[14];
    std::uint16_t stocks[0x20];       // Y+0x98: the item stocks at 0xE + sub
    unsigned char rows[0x3A * 2];     // instance 0's row, then body 1's
    unsigned char vars[0x700];
    std::uint16_t acts[2], qd8[2];
    unsigned char ps[0x300];
    unsigned char rec280[0x40];
    unsigned char records[0x10 * 16];  // *(Q+0x160): the per-(id, sub) item records, (sub + id*8)*0x10
};
unsigned char* g_image = nullptr;
std::uintptr_t g_base = 0;
Objects* g_o = nullptr;
std::string g_log;
void Log(const char* line) noexcept { g_log += line; }
bool Logged(const char* part) { return g_log.find(part) != std::string::npos; }
bool g_pins_ok = true;
bool Pins(std::uintptr_t base, std::size_t span, std::uintptr_t& mismatch) noexcept {
    mismatch = 0;
    return base == g_base && span == kSpan && g_pins_ok;
}
std::uint8_t g_types[0x300]{};
bool Type(std::uint16_t id, std::uint8_t& type) noexcept {
    type = id < sizeof(g_types) ? g_types[id] : 0;
    return type != 0;
}
unsigned g_on = 0, g_off = 0;
std::uintptr_t g_call_ctl = 0;
std::uint32_t g_call_id = 0, g_off_sub = 0xFF;
std::uint16_t g_call_equip = 0;
std::uint8_t g_call_flags = 0, g_off_flag = 0xFF;
bool g_touch_viewer = false, g_touch_slot1 = false, g_no_activate = false, g_touch_record0 = false;
std::uintptr_t Row(unsigned id) { return Addr(g_o->rows) + id * 0x3A; }
// Both calls OR the shared equipment dirty mask Q+0x17C with 1 << id (0x11AFF64, 0x11AF9D9).
void Dirty(std::uint32_t id) {
    const std::uintptr_t mask = Addr(g_o->q) + 0x17C;
    Put<std::uint32_t>(mask, Get<std::uint32_t>(mask) | (1u << id));
}
std::uint16_t ItemAt(unsigned id, unsigned sub) { return Get<std::uint16_t>(Row(id) + 0x1C + 2 * sub); }
unsigned SubOf(unsigned id) { return Get<std::uint8_t>(Row(id) + 0x37) >> 4; }
void SetSub(unsigned id, unsigned sub) {
    const std::uint8_t b = Get<std::uint8_t>(Row(id) + 0x37);
    Put<std::uint8_t>(Row(id) + 0x37, static_cast<std::uint8_t>((b & 0xF) | (sub << 4)));
}
// The non-local item path (re-cbox.md 3): the box id goes into body 1's first free sub (or the one holding it),
// SetEquipState's records, then SetCurrentItemIndex(1, k).
std::int32_t* FakeChangeEquip(void* ctl, std::int32_t* result, std::uint32_t id, const Request* request) noexcept {
    ++g_on;
    g_call_ctl = Addr(ctl);
    g_call_id = id;
    g_call_equip = request->equip;
    g_call_flags = request->flags;
    Dirty(id);  // SetEquipState
    unsigned k = 0;
    for (unsigned s = 1; s < 8 && !k; ++s)
        if (ItemAt(id, s) == request->equip) k = s;
    for (unsigned s = 1; s < 8 && !k; ++s)
        if (!ItemAt(id, s)) {
            k = s;
            Put<std::uint16_t>(Row(id) + 0x1C + 2 * s, request->equip);
        }
    std::memset(g_o->records + (k + id * 8) * 0x10, 0x5A, 0x10);  // body 1's record (never the viewer's 0..7)
    if (!g_no_activate) {
        Dirty(id);  // SetCurrentItemIndex
        SetSub(id, k);
    }
    if (g_touch_viewer) g_o->stocks[0xE + 1] ^= 1;  // the viewer's stock word
    if (g_touch_record0) g_o->records[1 * 0x10 + 4] ^= 1;  // the viewer's own record (id 0, sub 1)
    if (g_touch_slot1) g_o->handles[7 + 1] ^= 0x10000u;
    *result = 0;
    return result;
}
void FakeSetCurrentItemIndex(void* ctl, std::uint32_t id, std::uint32_t sub, std::uint8_t flag) noexcept {
    ++g_off;
    g_call_ctl = Addr(ctl);
    g_call_id = id;
    g_off_sub = sub;
    g_off_flag = flag;
    Dirty(id);
    SetSub(id, sub);
    g_o->handles[7 + 4] = 0;  // OnRemoveItem -> UnsetSlot(1, 4)
}
void Jump(std::uintptr_t rva, std::uintptr_t to) {
    unsigned char* at = g_image + rva;
    DWORD old = 0;
    Check(VirtualProtect(at, 16, PAGE_EXECUTE_READWRITE, &old) != 0, "fake image: executable stub page");
    const unsigned char stub[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};  // mov rax, imm64; jmp rax
    std::memcpy(at, stub, sizeof(stub));
    std::memcpy(at + 2, &to, 8);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof(stub));
}
void Method(std::uintptr_t object, std::uintptr_t vt, std::size_t slot, std::uintptr_t fn) {
    Put<std::uintptr_t>(object, g_base + vt);
    Put<std::uintptr_t>(g_base + vt + slot, g_base + fn);
}
void Build() {
    if (!g_image) {
        g_image = static_cast<unsigned char*>(VirtualAlloc(nullptr, kSpan, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(g_image != nullptr, "fake image allocation");
        g_base = Addr(g_image);
        g_o = new Objects();
        Jump(c::change_equip_rva, reinterpret_cast<std::uintptr_t>(&FakeChangeEquip));
        Jump(c::set_current_item_index_rva, reinterpret_cast<std::uintptr_t>(&FakeSetCurrentItemIndex));
    }
    std::memset(g_o, 0, sizeof(*g_o));
    Objects& o = *g_o;
    const std::uintptr_t player = Addr(o.player), block = Addr(o.buffer) + 0x6A8;
    Put<std::uintptr_t>(g_base + c::player_slot_rva, player);
    Method(player, 0x2295490, 0x28, 0x9CCB40);
    Put<std::uint32_t>(player + 0x208, 2);
    Put<std::uint32_t>(player + 0x204, 1);
    Put<std::uint32_t>(player + 0x214, 0);
    Put<std::uint32_t>(player + 0x218, 0);
    Put<std::uintptr_t>(player + 0x70, Addr(o.ids));
    Put<std::uint16_t>(Addr(o.ids) + 2, 1);
    Put<std::uintptr_t>(player + 0x78, Addr(o.pools));
    o.pools[1] = 1;
    Put<std::uintptr_t>(player + 0x50, Addr(o.blocks));
    Put<std::uintptr_t>(player + 0x378, Addr(o.buffer));
    o.blocks[1] = block;
    Method(block, 0x2295210, 0x180, 0x9C3B40);
    Put<std::uint32_t>(block + 8, 1);
    Put<std::uint32_t>(block + 0xC, 1);
    Put<std::uintptr_t>(block + 0x138, player);
    Put<std::uintptr_t>(block + 0x70, Addr(o.predicate));
    Method(Addr(o.predicate), 0x23BC1B0, 0x30, 0x1156640);
    Put<std::uintptr_t>(Addr(o.predicate) + 0x168, block);
    Put<std::uintptr_t>(Addr(o.predicate) + 0x1A8, Addr(o.pred_rows));
    Put<std::uintptr_t>(block + 0xF0, Addr(o.writer));
    Method(Addr(o.writer), 0x23C95D8, 0x28, 0x1209EC0);
    Put<std::uintptr_t>(Addr(o.writer) + 8, block);
    // Body 1's PS record (the can-wear gates: clear).
    Put<std::uintptr_t>(block + 0x78, Addr(o.ps));
    Put<std::uint32_t>(Addr(o.ps) + 8, 0);
    Put<std::uintptr_t>(Addr(o.ps) + 0x280, Addr(o.rec280));
    const std::uintptr_t ctl = Addr(o.ctl);
    Put<std::uintptr_t>(player + 0xC8, ctl);
    Method(ctl, c::equip_ctl_vt_rva, c::ctl_vt_change_equip, c::change_equip_rva);
    Method(ctl, c::equip_ctl_vt_rva, c::ctl_vt_set_item, c::set_current_item_index_rva);
    Method(ctl, c::equip_ctl_vt_rva, c::ctl_vt_equip_id_slot, c::get_equip_id_slot_rva);
    Method(ctl, c::equip_ctl_vt_rva, c::ctl_vt_item_stock, c::get_item_stock_count_rva);
    Method(ctl, c::equip_ctl_vt_rva, c::ctl_vt_item_index, c::item_index_forward_rva);
    Method(ctl, c::equip_ctl_vt_rva, c::ctl_vt_unset_slot, c::unset_slot_rva);
    Method(ctl, c::equip_ctl_vt_rva, 0x98, 0x11AFF30);
    Put<std::uintptr_t>(ctl + 0x18, player);
    Put<std::uintptr_t>(ctl + 8, Addr(o.inventory));
    Put<std::uintptr_t>(ctl + 0x28, Addr(o.engine));
    Put<std::uintptr_t>(ctl + 0x20, Addr(o.vars));
    const std::uintptr_t inv = Addr(o.inventory);
    Method(inv, c::store_vt_rva, c::store_vt_index, c::store_index_rva);
    Put<std::uintptr_t>(g_base + c::store_vt_rva + c::store_vt_count, g_base + c::store_count_read_rva);
    Put<std::uintptr_t>(g_base + c::store_vt_rva + 0x10, g_base + 0xEE9D70);
    Put<std::uint32_t>(inv + 0x30, 0);
    Put<std::uint32_t>(inv + 0xC, 7);
    Put<std::uint32_t>(inv + 0x10, 14);
    Put<std::uintptr_t>(inv + 0x40, Addr(o.handles));
    Put<std::uintptr_t>(inv + 0x98, Addr(o.stocks));
    Put<std::uintptr_t>(inv + 0x58, Addr(o.engine));
    Put<std::uintptr_t>(Addr(o.engine) + 8, g_base + c::equip_types_rva);
    Method(g_base + c::equip_types_rva, c::equip_types_vt_rva, 0, 0xA2A3E0);
    const std::uintptr_t q = Addr(o.q);
    Put<std::uintptr_t>(player + 0x60, q);
    Put<std::uintptr_t>(q, g_base + c::q_vtable_rva);
    Put<std::uintptr_t>(q + 0xC0, Addr(o.rows));
    Put<std::uintptr_t>(q + 0x18, Addr(o.acts));
    Put<std::uintptr_t>(q + 0xD8, Addr(o.qd8));
    Put<std::uintptr_t>(g_base + c::vars_slot_rva, Addr(o.vars));
    for (std::size_t k = 0; k < sizeof(o.vars); ++k) o.vars[k] = static_cast<unsigned char>(k * 7);
    // The viewer (instance 0): the lab seed's items (subs 1..7) with stocks, its gun current, act 3.
    const std::uint16_t seed[8] = {0, 468, 451, 492, 483, 484, 472, 466};
    for (unsigned k = 0; k < 8; ++k) {
        Put<std::uint16_t>(Row(0) + 0x1C + 2 * k, seed[k]);
        o.stocks[0xE + k] = seed[k] ? 1 : 0;
    }
    Put<std::uint16_t>(Row(0), 46);
    o.handles[0] = 0x00040000u | (46u << 5) | 1u;
    // Q+0x160 is a pointer to the record array (0x11B0345 mov rax,[rdx+0x160]); the dirty mask Q+0x17C starts clear.
    Put<std::uintptr_t>(q + 0x160, Addr(o.records));
    std::memset(o.records, 0x11, 8 * 0x10);  // the viewer's item records (id 0, subs 0..7)
    o.acts[0] = 3;
    o.acts[1] = 3;
    // Body 1: its gun, no items, slot-4 handle empty.
    Put<std::uint16_t>(Row(1), 46);
    o.handles[7 + 1] = 0x00050000u | (46u << 5) | 1u;
    std::memset(g_types, 0, sizeof(g_types));
    for (unsigned id = 0x1C8; id <= 0x1E2; ++id) g_types[id] = 0x11;
    g_types[451] = 0x11;
    g_on = g_off = 0;
    g_touch_viewer = g_touch_slot1 = g_no_activate = g_touch_record0 = false;
    g_pins_ok = true;
    g_call_id = g_off_sub = 0;
    g_call_equip = 0;
    g_log.clear();
}
std::uint64_t g_now = 100000;
std::uint32_t g_mission = 50000;
cb::Context Ctx(bool want, std::uint16_t id, std::uint32_t seq, std::uint32_t mission) {
    cb::Context x{};
    x.enabled = x.owner = x.gates = x.body_ready = true;
    x.want = want;
    x.want_id = id;
    x.seq = seq;
    x.base = g_base;
    x.player = Addr(g_o->player);
    x.remote_block = Addr(g_o->buffer) + 0x6A8;
    x.image_span = kSpan;
    x.mission = mission;
    x.epoch = 1000u + mission;
    x.realize_gen = 1;
    x.now_ms = (g_now += 100);
    x.pins_intact = &Pins;
    x.equip_type = &Type;
    return x;
}
}  // namespace world

void TestOnOff() {
    using namespace world;
    Build();
    const std::uint32_t m = ++g_mission;
    unsigned char viewer_row[0x3A];
    std::memcpy(viewer_row, reinterpret_cast<const void*>(Row(0)), sizeof(viewer_row));
    unsigned char vars[0x700];
    std::memcpy(vars, g_o->vars, sizeof(vars));
    const std::uint32_t viewer_h0 = g_o->handles[0];
    // Key on, the remote not boxed: nothing.
    cb::Observe(Ctx(false, 0, 0, m), &Log);
    Check(g_on == 0 && g_off == 0 && g_log.empty(), "nothing to do: no call, no line");
    // 1. The remote boxes (468, the viewer owns it): one ChangeEquip(ctl, &r, 1, {468, flags 4}).
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 1 && g_call_ctl == Addr(g_o->ctl) && g_call_id == 1 && g_call_equip == 468 && g_call_flags == 4,
          "on: ChangeEquip(ctl, id 1, 468, flags 4)");
    Check(Logged("partner_cbox=apply; id=468; want=468; mapped=same; via=native; sub=1; body_sub=1; result=0; reason=ok;"),
          "on: the apply line");
    Check(ItemAt(1, 1) == 468 && SubOf(1) == 1 && std::memcmp(viewer_row, reinterpret_cast<const void*>(Row(0)), sizeof(viewer_row)) == 0 &&
              std::memcmp(vars, g_o->vars, sizeof(vars)) == 0 && g_o->handles[0] == viewer_h0 && !cb::Uncertain(),
          "on: body 1's row only; the viewer's row, vars and handles untouched");
    // 2. Same edge again: never re-dispatched; the entry watch reports act 77.
    g_o->acts[1] = 77;
    g_o->qd8[1] = 0x1234;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 1 && Logged("partner_cbox=entered; act=77;") && Logged("qd8=4660"), "entered: act 77, the box object word");
    // A later edge while the box is current (a remote re-box of the same id): still no dispatch.
    cb::Observe(Ctx(true, 468, 2, m), &Log);
    Check(g_on == 1, "never re-dispatch while the box is body 1's current item");
    // 3. The remote unboxes: SetCurrentItemIndex(ctl, 1, 0, 0).
    g_o->handles[7 + 4] = 0x00070000u | (468u << 5) | 0x11u;
    cb::Observe(Ctx(false, 468, 3, m), &Log);
    Check(g_off == 1 && g_call_id == 1 && g_off_sub == 0 && g_off_flag == 0 && g_call_ctl == Addr(g_o->ctl),
          "off: SetCurrentItemIndex(ctl, 1, 0, 0)");
    Check(Logged("partner_cbox=off; id=468; via=native; reason=ok;") && Logged("h4=00073a91>00000000") && SubOf(1) == 0 &&
              ItemAt(1, 1) == 468 && !cb::Uncertain(),
          "off: sub 0 current, the box stays in body 1's list, the slot-4 handle change is allowed");
    cb::Observe(Ctx(false, 468, 3, m), &Log);
    Check(g_off == 1, "off once");
    // 4. A second on finds the box in body 1's list (sub 1): no new row write.
    g_o->acts[1] = 3;
    cb::Observe(Ctx(true, 468, 4, m), &Log);
    Check(g_on == 2 && SubOf(1) == 1 && ItemAt(1, 2) == 0, "the next on reuses sub 1");
    const auto status = cb::GetStatus();
    Check(status.applied == 468 && status.dispatches_on >= 2 && status.dispatches_off >= 1 && status.mission == m, "status");
}
void TestMappingAndRefusals() {
    using namespace world;
    // The remote's box id the viewer lacks: the viewer's first box.
    Build();
    std::uint32_t m = ++g_mission;
    cb::Observe(Ctx(true, 0x1C8, 1, m), &Log);
    Check(g_on == 1 && g_call_equip == 468 && Logged("partner_cbox=apply; id=468; want=456; mapped=viewer_box;"), "viewer_box mapping");
    // No viewer box: refused once per edge, no call.
    Build();
    m = ++g_mission;
    for (unsigned k = 1; k < 8; ++k)
        if (pol::BoxEquip(ItemAt(0, k))) Put<std::uint16_t>(Row(0) + 0x1C + 2 * k, 0);
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 0 && Logged("partner_cbox=refused; reason=viewer_no_box; step=on; want=468;") &&
              g_log.find("viewer_no_box") == g_log.rfind("viewer_no_box"),
          "viewer_no_box: no call, one line per edge");
    // The viewer's box with stock 0 is never lent.
    Build();
    m = ++g_mission;
    for (unsigned k = 1; k < 8; ++k) g_o->stocks[0xE + k] = 0;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 0 && Logged("reason=viewer_no_box"), "stock 0: viewer_no_box");
    // Body 1's item list full.
    Build();
    m = ++g_mission;
    for (unsigned k = 1; k < 8; ++k) Put<std::uint16_t>(Row(1) + 0x1C + 2 * k, static_cast<std::uint16_t>(500 + k));
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 0 && Logged("partner_cbox=refused; reason=body_items_full;"), "body_items_full");
    // A box whose packed type is not 17.
    Build();
    m = ++g_mission;
    g_types[468] = 0x0C;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 0 && Logged("partner_cbox=refused; reason=type;"), "type != 17: refused");
    // Pins.
    Build();
    m = ++g_mission;
    g_pins_ok = false;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 0 && Logged("reason=full_body_gate"), "pins refuse before the call");
    // Busy (the gun / support writers): transient, retried once idle.
    Build();
    m = ++g_mission;
    auto x = Ctx(true, 468, 1, m);
    x.busy_reason = "gun_writer_active";
    cb::Observe(x, &Log);
    Check(g_on == 0 && Logged("partner_cbox=refused; reason=gun_writer_active;"), "busy: refused");
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 1, "busy is transient: the same edge dispatches once idle");
    // The restriction predicate's row (ChangeEquip would return -1): transient.
    Build();
    m = ++g_mission;
    g_o->pred_rows[0] = 1;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 0 && Logged("reason=restriction"), "restriction: refused");
    g_o->pred_rows[0] = 0;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 1, "restriction is transient");
    // Gates (the observer's admission): one line, no call.
    Build();
    m = ++g_mission;
    x = Ctx(true, 468, 1, m);
    x.gates = false;
    x.gate_reason = "partner_off";
    cb::Observe(x, &Log);
    Check(g_on == 0 && Logged("partner_cbox=refused; reason=gates; step=partner_off;"), "gates: refused with the gate");
    // The viewer is not instance 0.
    Build();
    m = ++g_mission;
    Put<std::uint32_t>(Addr(g_o->player) + 0x214, 1);
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 0 && Logged("reason=p214"), "p214: refused");
    // Online (P+0x204 bit 8).
    Build();
    m = ++g_mission;
    Put<std::uint32_t>(Addr(g_o->player) + 0x204, 0x101);
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 0 && Logged("reason=offline"), "online: refused");
    // Key off: silent.
    Build();
    m = ++g_mission;
    x = Ctx(true, 468, 1, m);
    x.enabled = false;
    cb::Observe(x, &Log);
    Check(g_on == 0 && g_log.empty(), "key off: silent");
}
void TestUncertain() {
    using namespace world;
    // The viewer's stock word changes inside the call: viewer_changed, latched for the mission (not even an off).
    Build();
    std::uint32_t m = ++g_mission;
    g_touch_viewer = true;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 1 && Logged("partner_cbox=apply; id=468;") && Logged("reason=viewer_changed;") && cb::Uncertain(),
          "a viewer change in the call latches uncertain");
    g_touch_viewer = false;
    cb::Observe(Ctx(false, 468, 2, m), &Log);
    Check(g_off == 0, "uncertain: nothing of ours to take off");
    cb::Observe(Ctx(true, 468, 3, m), &Log);
    Check(g_on == 1 && Logged("reason=uncertain_latched"), "uncertain: no further on");
    // The same mission code with a new start epoch (a restarted mission; the driver's cbox_seq restarts): clean.
    Build();
    auto restarted = Ctx(true, 468, 1, m);
    restarted.epoch += 7;
    cb::Observe(restarted, &Log);
    Check(g_on == 1 && !cb::Uncertain() && Logged("partner_cbox=apply; id=468;") && Logged("reason=ok"),
          "a restarted mission (same code, new epoch) clears the latch and the edge state");
    // A new mission clears the latch.
    m = ++g_mission;
    Build();
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 1 && !cb::Uncertain() && Logged("reason=ok"), "the latch is mission-scoped");
    // Another body-1 slot changes in the call.
    Build();
    m = ++g_mission;
    g_touch_slot1 = true;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(Logged("reason=body_slots_changed;") && cb::Uncertain(), "body slot 1 changed in the call: latched");
    // ChangeEquip returned 0 but the box is not current: not_current (logged, edge done, not latched).
    Build();
    m = ++g_mission;
    g_no_activate = true;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(Logged("reason=not_current;") && !cb::Uncertain(), "not_current: not latched");
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 1, "not_current: the edge is done");
}
void TestWatchesWorld() {
    using namespace world;
    // Entry timeout: body 1 never enters 77/78 within 1500 ms (the PS record bits are logged).
    Build();
    std::uint32_t m = ++g_mission;
    Put<std::uint32_t>(Addr(g_o->rec280) + 0x20 + 0xC, 0x04000000u);  // rec = rec280 + (1 - base 0)*0x20
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    for (int i = 0; i < 20; ++i) cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(Logged("partner_cbox=entered; ok=0; act=3;") && Logged("recC=0x4000000; rec_ok=1; vstock=1;"), "entry timeout with the record bits");
    Check(g_log.find("entered; ok=0") == g_log.rfind("entered; ok=0"), "the timeout is logged once");
    // The viewer's box stock changes while body 1 is boxed, the viewer idle: viewer_changed, latched (no new on).
    Build();
    m = ++g_mission;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    g_o->acts[1] = 77;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    g_o->stocks[0xE + 1] = 0;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(Logged("partner_cbox=viewer_changed; fields=0x4; viewer_act=3>3; latched=1;"), "a viewer stock change while boxed: latched");
    // The engine exits the box (stock 0): logged once.
    g_o->acts[1] = 61;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(Logged("partner_cbox=exited; act=61; want=1; vstock=0;"), "the engine's own exit is logged");
    // The latch: our box still comes off; no new on.
    cb::Observe(Ctx(false, 468, 2, m), &Log);
    Check(g_off == 1 && Logged("partner_cbox=off; id=468; via=native; reason=ok;"), "watch latch: the off still runs");
    cb::Observe(Ctx(true, 468, 3, m), &Log);
    Check(g_on == 1 && Logged("reason=viewer_changed_latched"), "watch latch: no new on");
    // The viewer selects another item itself (its act changes): logged, not latched.
    Build();
    m = ++g_mission;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    g_o->acts[0] = 20;
    Put<std::uint32_t>(Addr(g_o->vars) + 0x67C, 2);
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(Logged("partner_cbox=viewer_changed; fields=0x10; viewer_act=3>20; latched=0;"), "the viewer's own selection: logged only");
    // The viewer switches weapons (row 0 +0 and its slot nibble, a slot handle), act unchanged: logged, not latched.
    Put<std::uint16_t>(Row(0), 101);
    Put<std::uint8_t>(Row(0) + 0x36, 2);
    g_o->handles[2] ^= 0x10000u;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(Logged("partner_cbox=viewer_changed; fields=0x180; viewer_act=20>20; latched=0;"), "the viewer's weapon switch: logged only");
    const auto st = cb::GetStatus();
    Check(!st.watch_latched && st.applied == 468, "no latch for the viewer's own selection or weapons");
    // Our box no longer current (the row was reset): released, never taken off.
    Put<std::uint16_t>(Row(1) + 0x1C + 2, 0);
    cb::Observe(Ctx(false, 468, 2, m), &Log);
    Check(Logged("partner_cbox=released; id=468; body_item=0;") && g_off == 0, "released: nothing to take off");
}
void TestCarryWorld() {
    using namespace world;
    // A restarted mission (same code, new start epoch: the driver's edge counter restarts at 0) whose body-1 row kept
    // our box: carried, and taken off because the remote is not boxed.
    Build();
    std::uint32_t m = ++g_mission;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 1 && cb::GetStatus().applied == 468, "carry: the box is on");
    auto restarted = Ctx(false, 0, 0, m);
    restarted.epoch += 7;
    cb::Observe(restarted, &Log);
    Check(Logged("partner_cbox=carried; id=468;") && g_off == 1 && Logged("partner_cbox=off; id=468; via=native; reason=ok;") &&
              SubOf(1) == 0 && cb::GetStatus().applied == 0,
          "a restarted mission that kept body 1's row: our carried box comes off");
    // A new mission whose remote is boxed again: the carried box stays (never re-dispatched, no entry watch).
    Build();
    m = ++g_mission;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    m = ++g_mission;
    for (int i = 0; i < 20; ++i) cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 1 && g_off == 0 && Logged("partner_cbox=carried; id=468;") && !Logged("entered; ok=0") &&
              cb::GetStatus().applied == 468,
          "a carried box while the remote is boxed: kept, no second ChangeEquip, no entry timeout");
    // A new mission that rebuilt body 1's row: carried, then released; nothing to take off.
    Build();
    m = ++g_mission;
    cb::Observe(Ctx(false, 0, 0, m), &Log);
    Check(Logged("partner_cbox=carried; id=468;") && Logged("partner_cbox=released; id=468; body_item=0;") && g_off == 0 &&
              cb::GetStatus().applied == 0,
          "a rebuilt row: the carried box is released, no off call");
    // Key off: a carried box is not even logged.
    Build();
    m = ++g_mission;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Build();
    m = ++g_mission;
    auto off = Ctx(false, 0, 0, m);
    off.enabled = false;
    cb::Observe(off, &Log);
    Check(g_log.empty() && g_off == 0, "key off: silent, also with a carried box");
    cb::Observe(Ctx(false, 0, 0, m), &Log);
    Check(Logged("partner_cbox=carried; id=468;") && Logged("partner_cbox=released;"), "the carry is reported once the key is on");
    cb::Observe(Ctx(false, 0, 0, m), &Log);
    Check(g_log.find("carried") == g_log.rfind("carried"), "the carry is logged once");
}
void TestRecordsWorld() {
    using namespace world;
    // Q+0x17C (the dirty mask) is set by both calls for body 1, by any other equip write, and cleared by the engine:
    // the apply, the watch and the off never see it.
    Build();
    std::uint32_t m = ++g_mission;
    const std::uintptr_t mask = Addr(g_o->q) + 0x17C;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 1 && (Get<std::uint32_t>(mask) & 2u) != 0 && Logged("partner_cbox=apply; id=468;") && Logged("reason=ok;") &&
              !cb::Uncertain() && cb::GetStatus().applied == 468,
          "dirty mask: the call's own Q+0x17C bit 1 is not a viewer change");
    g_o->acts[1] = 77;
    Put<std::uint32_t>(mask, 0);  // the engine's clear (Q vt slot 0 = 0x140FE0070)
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Put<std::uint32_t>(mask, 2u);  // another body-1 equip write (the W4 slot follower's SetCurrentSlot)
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Put<std::uint32_t>(mask, 3u);  // and one for the viewer's id
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(!Logged("viewer_changed") && !cb::GetStatus().watch_latched, "dirty mask: the watch ignores Q+0x17C");
    // Body 1's own record (8 + k) changes while boxed (its box life): not the viewer's.
    g_o->records[(1 + 8) * 0x10] ^= 0x40;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(!Logged("viewer_changed"), "body 1's own record is not a viewer change");
    cb::Observe(Ctx(false, 468, 2, m), &Log);
    Check(g_off == 1 && Logged("partner_cbox=off; id=468; via=native; reason=ok;") && !cb::Uncertain() && cb::GetStatus().applied == 0,
          "dirty mask: the off still reads ok");
    // The viewer's real record (id 0, sub 1 at *(Q+0x160) + 0x10) changes while boxed, the viewer idle: latched.
    Build();
    m = ++g_mission;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    g_o->acts[1] = 77;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    g_o->records[1 * 0x10 + 8] ^= 1;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(Logged("partner_cbox=viewer_changed; fields=0x20; viewer_act=3>3; latched=1;") && cb::GetStatus().watch_latched,
          "the viewer's own record (id 0) while boxed: records, latched");
    // The viewer's real record changes inside the call: viewer_changed, latched for the mission.
    Build();
    m = ++g_mission;
    g_touch_record0 = true;
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 1 && Logged("partner_cbox=apply; id=468;") && Logged("reason=viewer_changed;") && cb::Uncertain(),
          "the viewer's own record in the call: viewer_changed");
    // No record array: no call.
    Build();
    m = ++g_mission;
    Put<std::uintptr_t>(Addr(g_o->q) + 0x160, 0);
    cb::Observe(Ctx(true, 468, 1, m), &Log);
    Check(g_on == 0 && Logged("partner_cbox=refused; reason=local_snapshot;"), "a null *(Q+0x160): refused before the call");
}
void TestPins() {
    // The writer's SHA list covers the off call and the box chain whole (two generators, the same retail bytes).
    const std::uintptr_t want[] = {c::change_equip_rva, c::set_current_item_index_rva, c::get_item_stock_count_rva, c::get_equip_id_slot_rva,
                                   0x11AF000, 0x11AEF10, c::unset_slot_rva, 0x11AFF30};
    for (const auto rva : want) {
        bool found = false;
        for (const auto& b : mgscoop::equipment_stock_writer_pins::bodies)
            if (b.rva == rva) found = true;
        Check(found, "the box chain is in the writer's SHA list");
    }
    bool slot68 = false, slotc8 = false, site_viewer = false, site_act = false;
    for (const auto& s : c::vtable_slots) {
        if (s.vtable_rva == c::equip_ctl_vt_rva && s.offset == 0x68 && s.target_rva == c::set_current_item_index_rva) slot68 = true;
        if (s.vtable_rva == c::equip_ctl_vt_rva && s.offset == 0xC8 && s.target_rva == c::get_item_stock_count_rva) slotc8 = true;
    }
    for (const auto& s : c::code_sites) {
        if (s.rva == 0x11AD675 && s.size == 15 && s.bytes[11] == 0x14 && s.bytes[12] == 0x02) site_viewer = true;
        if (s.rva == 0x110BDD3 && s.bytes[0] == 0xB8 && s.bytes[1] == 0x4D) site_act = true;
    }
    Check(slot68 && slotc8 && site_viewer && site_act, "the contract pins the box slots and sites");
}

int main() {
    TestRanges();
    TestMapping();
    TestBodySub();
    TestDecide();
    TestEnter();
    TestVerdicts();
    TestWatches();
    TestReconHold();
    TestBusy();
    TestOnOff();
    TestMappingAndRefusals();
    TestUncertain();
    TestWatchesWorld();
    TestCarryWorld();
    TestRecordsWorld();
    TestPins();
    std::printf("partner_cbox_tests: %u checks passed\n", checks);
    return 0;
}
