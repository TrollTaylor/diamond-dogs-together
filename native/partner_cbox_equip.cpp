// F2: the remote's cardboard box on body 1 (docs/native-puppet.md "Cardboard box").
// Static contract: re-cbox.md (ChangeEquip(1, box, flags 4) writes only body 1's
// item list, its +0x37 item nibble, record 8+k of the array *(Q+0x160) and the
// Q+0x17C bit; the viewer's writes in SetEquipState / SetCurrentItemIndex are
// gated on idx == [P+0x214]). Q+0x17C is the shared equipment dirty mask (both
// calls OR in 1 << idx, the engine clears it): never part of a viewer digest.
// Template: partner_support_equip.cpp (Capture / SameViewer / Dispatch, SRW lock,
// SEH, a mission-scoped uncertain latch and a late re-capture).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <array>
#include <cstdarg>
#include <cstring>
#include "mgscoop/partner_cbox_equip.hpp"
#include "mgscoop/equipment_stock_writer_contract.hpp"
#include "partner_player_contract.hpp"
namespace mgscoop::partner_cbox_equip {
namespace {
namespace c = partner_player_contract;
namespace ec = equipment_stock_writer_contract;
namespace pol = partner_cbox_policy;
SRWLOCK g_lock = SRWLOCK_INIT;
pol::State g_state{};
std::uint32_t g_dispatches_on = 0, g_dispatches_off = 0;
unsigned g_lines = 0, g_key_lines = 0;
std::uint64_t g_log_ms = 0;
const char* g_reason = nullptr;
std::uint16_t g_carried = 0;  // a box carried into a new mission, logged once (partner_cbox=carried)
bool Read(std::uintptr_t address, void* out, std::size_t size) noexcept {
    if (!address || address > UINTPTR_MAX - size) return false;
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &copied) && copied == size;
}
using Row = std::array<std::uint8_t, c::equip_row_stride>;
struct Snapshot {
    std::uintptr_t ctl = 0, inventory = 0, stocks = 0, handles = 0, q = 0, rows = 0, vars = 0;
    std::uintptr_t records = 0;                  // *(Q+0x160): the per-(id, sub) item records, (sub + id*8)*0x10
    Row row0{}, row1{};
    std::array<std::uint32_t, 7> local_handles{}, remote_handles{};
    std::uint16_t vstock[pol::kSubs]{};          // the viewer's item stocks *(stocks + (0xE + k)*2)
    std::array<std::uint8_t, 16> vars_items{};   // vars.items u16[8] (+0x5A4)
    std::uint32_t vars_current = 0;              // vars.currentItemIndex (+0x67C)
    std::array<std::uint8_t, c::vars_ammo_end - c::vars_ammo_begin> vars_ammo{};
    std::array<std::uint8_t, pol::kSubs * c::q_item_record_size> records0{};  // records + 0..0x7F (the viewer's, id 0)
    std::uint16_t act[2]{}, qd8[2]{};
    std::uint8_t restriction = 1;
    bool rec_ok = false;
    std::uint32_t rec8 = 0, recc = 0;            // body 1's PS record +8 / +0xC (the can-wear gates)
};
std::uint16_t Item(const Row& row, unsigned sub) noexcept { return ec::Word16(row.data() + c::equip_row_items + 2 * (sub & 7)); }
unsigned CurrentSub(const Row& row) noexcept { return row[c::equip_row_item_nibble] >> 4; }
std::uint16_t Current(const Row& row) noexcept { return Item(row, CurrentSub(row)); }
void Items(const Row& row, std::uint16_t (&out)[pol::kSubs]) noexcept {
    for (unsigned k = 0; k < pol::kSubs; ++k) out[k] = Item(row, k);
}
// Every chain the two calls depend on, re-read (the support Capture's chain plus
// the item methods), then the pure reads both digests use.
const char* Capture(const Context& x, Snapshot& s) noexcept {
    s = Snapshot{};
    auto reader = Read;
    ec::View<decltype(reader)> v{reader, x.base, x.image_span};
    std::uintptr_t current = 0, ids = 0, pools = 0, blocks = 0, buffer = 0;
    std::uint32_t count = 0, flags = 0, local = 1, local_id = 1, body_count = 0, body_base = 0;
    std::uint16_t body_id = 0xFFFF;
    std::uint8_t pool = 0xFF;
    if (!v.Image(c::player_slot_rva) || !v.At(x.base, c::player_slot_rva, current) || current != x.player ||
        !v.Method(x.player, 0x2295490, 0x28, 0x9CCB40) || !v.At(x.player, 0x208, count) || count != 2 ||
        !v.At(x.player, 0x204, flags) || !v.At(x.player, 0x214, local) || !v.At(x.player, 0x218, local_id) ||
        !v.At(x.player, 0x70, ids) || !v.At(ids, 2, body_id) || !v.At(x.player, 0x78, pools) || !v.At(pools, 1, pool)) return "player_lease";
    if (!(flags & 1) || (flags & 0x100)) return "offline";
    // The viewer is instance 0 (row 0, handles 0..6); body 1 is never local.
    if (local != 0) return "p214";
    if (local_id == 1) return "p218";
    if (body_id != 1) return "body_id";
    if (pool == 0xFF || pool >= 2) return "pool";
    if (!v.At(x.player, 0x50, blocks) || !v.At(x.player, 0x378, buffer) || !buffer ||
        !v.At(blocks, std::size_t(pool) * 8, current) || current != x.remote_block || current != buffer + std::size_t(pool) * 0x6A8 ||
        !v.Method(x.remote_block, 0x2295210, 0x180, 0x9C3B40) || !v.At(x.remote_block, 8, body_count) || body_count != 1 ||
        !v.At(x.remote_block, 0xC, body_base) || body_base != 1 || !v.At(x.remote_block, 0x138, current) || current != x.player) return "body_chain";
    // ChangeEquip's busy predicate (0x1156640 row: nonzero returns -1) and the
    // lower writer it calls for body 1 (DoPickUpWithPickableInfo, block+0xF0).
    std::uintptr_t predicate = 0, node = 0, pred_rows = 0, writer = 0;
    if (!v.At(x.remote_block, 0x70, predicate) || !v.Method(predicate, 0x23BC1B0, 0x30, 0x1156640) ||
        !v.At(predicate, 0x168, node) || node != x.remote_block || !v.At(predicate, 0x1A8, pred_rows) ||
        !v.At(pred_rows, 0, s.restriction) || !v.At(x.remote_block, 0xF0, writer) ||
        !v.Method(writer, 0x23C95D8, 0x28, 0x1209EC0) || !v.At(writer, 8, current) || current != x.remote_block) return "restriction_chain";
    if (s.restriction) return "restriction";
    if (!v.At(x.player, 0xC8, s.ctl) || !v.Method(s.ctl, c::equip_ctl_vt_rva, c::ctl_vt_change_equip, c::change_equip_rva) ||
        !v.Method(s.ctl, c::equip_ctl_vt_rva, c::ctl_vt_set_item, c::set_current_item_index_rva) ||
        !v.Method(s.ctl, c::equip_ctl_vt_rva, c::ctl_vt_equip_id_slot, c::get_equip_id_slot_rva) ||
        !v.Method(s.ctl, c::equip_ctl_vt_rva, c::ctl_vt_item_stock, c::get_item_stock_count_rva) ||
        !v.Method(s.ctl, c::equip_ctl_vt_rva, c::ctl_vt_item_index, c::item_index_forward_rva) ||
        !v.Method(s.ctl, c::equip_ctl_vt_rva, c::ctl_vt_unset_slot, c::unset_slot_rva) ||
        !v.Method(s.ctl, c::equip_ctl_vt_rva, 0x98, 0x11AFF30) ||
        !v.At(s.ctl, c::ctl_player, current) || current != x.player || !v.At(s.ctl, c::ctl_inventory, s.inventory)) return "equipment_interface";
    std::uintptr_t inv_vt = 0, engine = 0, types = 0;
    std::uint32_t stride = 0, total = 0, inventory_local = 1;
    if (!v.At(s.inventory, 0, inv_vt) || (inv_vt != x.base + c::store_vt_rva && inv_vt != x.base + c::store_vt_derived_rva) ||
        !v.At(inv_vt, c::store_vt_index, current) || current != x.base + c::store_index_rva ||
        !v.At(inv_vt, c::store_vt_count, current) || current != x.base + c::store_count_read_rva ||
        !v.At(inv_vt, 0x10, current) || current != x.base + 0xEE9D70 ||
        !v.At(s.inventory, 0x30, inventory_local) || !v.At(s.inventory, 0xC, stride) || stride != 7 ||
        !v.At(s.inventory, 0x10, total) || total != 14 || !v.At(s.inventory, c::inv_handles, s.handles) || !s.handles ||
        !v.At(s.inventory, c::inv_stocks, s.stocks) || !s.stocks || !v.At(s.inventory, 0x58, engine) || !engine) return "inventory_chain";
    if (inventory_local == 1) return "inventory_local";
    // ChangeEquip's own type lookup: ctl+0x28 = engine, *(engine+8) = the type
    // table GetEquipTypeId 0xA2A3E0 reads (the same table pp::EquipType reads).
    if (!v.At(s.ctl, 0x28, current) || current != engine || !v.At(engine, 8, types) || types != x.base + c::equip_types_rva ||
        !v.Method(types, c::equip_types_vt_rva, 0, 0xA2A3E0)) return "controller_engine";
    std::uintptr_t acts = 0, qd8 = 0;
    if (!v.At(x.player, 0x60, s.q) || !v.At(s.q, 0, current) || current != x.base + c::q_vtable_rva ||
        !v.At(s.q, c::q_equip_rows, s.rows) || !s.rows || !v.At(s.rows, 0, s.row0) || !v.At(s.rows, c::equip_row_stride, s.row1) ||
        !v.At(s.handles, 0, s.local_handles) || !v.At(s.handles, 7 * 4, s.remote_handles) ||
        !v.At(x.base, c::vars_slot_rva, s.vars) || !s.vars || !v.At(s.ctl, c::ctl_vars, current) || current != s.vars ||
        !v.At(s.vars, c::vars_items, s.vars_items) || !v.At(s.vars, c::vars_current_item, s.vars_current) ||
        !Read(s.vars + c::vars_ammo_begin, s.vars_ammo.data(), s.vars_ammo.size()) ||
        !v.At(s.q, c::q_item_records, s.records) || !s.records || !v.At(s.records, 0, s.records0) ||
        !v.At(s.q, c::q_anim_18, acts) || !v.At(acts, 0, s.act) || !v.At(s.q, c::q_box_object, qd8) || !v.At(qd8, 0, s.qd8)) return "local_snapshot";
    for (unsigned k = 0; k < pol::kSubs; ++k)
        if (!v.At(s.stocks, (c::inv_item_stock_base + k) * 2, s.vstock[k])) return "viewer_stocks";
    // Body 1's PS record (the can-wear gates; logged on an entry timeout only).
    std::uintptr_t ps = 0, rec280 = 0;
    std::uint32_t ps_base = 0;
    s.rec_ok = v.At(x.remote_block, c::block_ps, ps) && v.At(ps, c::ps_base_id, ps_base) && ps_base <= 1 &&
               v.At(ps, c::ps_rec280, rec280) && v.At(rec280, (1 - ps_base) * c::ps_rec280_stride + 8, s.rec8) &&
               v.At(rec280, (1 - ps_base) * c::ps_rec280_stride + 0xC, s.recc);
    return nullptr;
}
// The viewer's whole own state the two calls must never touch: row 0, its seven
// handles, its item stocks, vars (items, current item, the ammo block), its item
// records, its act and box object word.
bool SameViewer(const Snapshot& a, const Snapshot& b) noexcept {
    return a.row0 == b.row0 && a.local_handles == b.local_handles && std::memcmp(a.vstock, b.vstock, sizeof(a.vstock)) == 0 &&
           a.vars_items == b.vars_items && a.vars_current == b.vars_current && a.vars_ammo == b.vars_ammo &&
           a.records0 == b.records0 && a.act[0] == b.act[0] && a.qd8[0] == b.qd8[0];
}
// Body 1's handles of every slot but 4 (OnRemoveItem's UnsetSlot(1, 4) may touch the item slot).
bool SameBodySlots(const Snapshot& a, const Snapshot& b) noexcept {
    for (unsigned i = 0; i < 7; ++i)
        if (i != c::slot_type_item && a.remote_handles[i] != b.remote_handles[i]) return false;
    return true;
}
bool SameLease(const Snapshot& a, const Snapshot& b) noexcept {
    return a.ctl == b.ctl && a.inventory == b.inventory && a.stocks == b.stocks && a.handles == b.handles && a.q == b.q &&
           a.rows == b.rows && a.vars == b.vars && a.records == b.records && a.row1 == b.row1 && a.remote_handles == b.remote_handles &&
           SameViewer(a, b);
}
// The per-tick watch digest (field masks, partner_cbox_policy::field).
struct Watch {
    bool have = false;
    Row row0{};
    std::uint16_t vstock[pol::kSubs]{};
    std::array<std::uint8_t, 16> vars_items{};
    std::uint32_t vars_current = 0;
    std::array<std::uint8_t, pol::kSubs * c::q_item_record_size> records0{};
    std::array<std::uint32_t, 7> handles{};
    std::uint16_t act0 = 0;
};
Watch g_watch{};
Watch WatchOf(const Snapshot& s) noexcept {
    Watch w{};
    w.have = true;
    w.row0 = s.row0;
    std::memcpy(w.vstock, s.vstock, sizeof(w.vstock));
    w.vars_items = s.vars_items;
    w.vars_current = s.vars_current;
    w.records0 = s.records0;
    w.handles = s.local_handles;
    w.act0 = s.act[0];
    return w;
}
// The viewer's row 0 split: the item list and item bytes (+0x1C..+0x34), the
// item nibble (+0x37 high), and everything else (weapons, slot nibble, +0x38).
std::uint32_t Changed(const Watch& a, const Watch& b) noexcept {
    namespace f = pol::field;
    std::uint32_t m = 0;
    for (std::size_t i = 0; i < a.row0.size(); ++i) {
        if (a.row0[i] == b.row0[i]) continue;
        if (i >= c::equip_row_items && i < c::equip_row_item_bytes + pol::kSubs) m |= f::row_items;
        else if (i == c::equip_row_item_nibble) m |= (a.row0[i] >> 4) != (b.row0[i] >> 4) ? f::row_current : 0u;
        else m |= f::row_other;
        if (i == c::equip_row_item_nibble && (a.row0[i] & 0xF) != (b.row0[i] & 0xF)) m |= f::row_other;
    }
    if (std::memcmp(a.vstock, b.vstock, sizeof(a.vstock)) != 0) m |= f::stocks;
    if (a.vars_items != b.vars_items) m |= f::vars_items;
    if (a.vars_current != b.vars_current) m |= f::vars_current;
    if (a.records0 != b.records0) m |= f::records;
    for (unsigned i = 0; i < 7; ++i)
        if (a.handles[i] != b.handles[i]) m |= i == c::slot_type_item ? f::item_handle : f::handles_other;
    return m;
}
bool IdleReason(const char* reason) noexcept {
    return reason && (!std::strcmp(reason, "body_busy") || !std::strcmp(reason, "gun_writer_active") ||
                      !std::strcmp(reason, "restriction") || !std::strcmp(reason, "lease_changed") ||
                      !std::strncmp(reason, "gates", 5) || !std::strcmp(reason, "body") || !std::strcmp(reason, "thread"));
}
// One line per decision. Transient refusals (busy, restriction, lease, gates)
// log only when the reason changes, under their own small cap; dispatches,
// final refusals and watch events keep a separate cap.
void Line(LogFn log, const Context& x, const char* reason, bool idle, const char* format, ...) noexcept {
    if (!log) return;
    const bool repeated = g_reason && reason && std::strcmp(g_reason, reason) == 0;
    if (idle && repeated) return;
    if (!idle && repeated && reason && x.now_ms >= g_log_ms && x.now_ms - g_log_ms < 1000) return;
    unsigned& lines = idle ? g_lines : g_key_lines;
    if (lines >= (idle ? 64u : 512u)) return;
    g_log_ms = x.now_ms;
    g_reason = reason;
    ++lines;
    char line[512]{};
    va_list arguments;
    va_start(arguments, format);
    StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    log(line);
}
void Refused(LogFn log, const Context& x, const char* why, std::uint16_t box, std::uint16_t current, const char* step) noexcept {
    Line(log, x, why, IdleReason(why),
         "partner_cbox=refused; reason=%s; step=%s; want=%u; id=%u; seq=%u; body_item=%u; mission=%u\r\n", why, step,
         unsigned(x.want_id), unsigned(box), x.seq, unsigned(current), x.mission);
}
// Event lines (watch / apply / off) bypass the change-only filter (each is one decision).
void Event(LogFn log, const char* format, ...) noexcept {
    if (!log || g_key_lines >= 512u) return;
    ++g_key_lines;
    char line[512]{};
    va_list arguments;
    va_start(arguments, format);
    StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    log(line);
}
#pragma pack(push, 1)
struct Request {
    std::uint64_t type = 0x326C48DA30C0ull;
    std::uint16_t equip = 0, stock = 0, stock_sub = 0, ammo = 0, ammo_sub = 0;
    std::uint8_t suppression = 0, flags = 4;  // +0x13 bit 2 toActive only (no drop, no temporary slot)
};
#pragma pack(pop)
static_assert(sizeof(Request) == 20 && offsetof(Request, equip) == 8 && offsetof(Request, flags) == 0x13);
// RCX ctl, RDX &result, R8D 1 (the player index), R9 the request; returns RDX.
bool DispatchOn(std::uintptr_t function, std::uintptr_t ctl, const Request* request, std::int32_t* result) noexcept {
    using Fn = std::int32_t* (*)(void*, std::int32_t*, std::uint32_t, const Request*);
    __try {
        return reinterpret_cast<Fn>(function)(reinterpret_cast<void*>(ctl), result, 1, request) == result;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// RCX ctl, EDX 1 (the player index), R8D 0 (sub 0: always "no item"), R9B 0.
bool DispatchOff(std::uintptr_t function, std::uintptr_t ctl) noexcept {
    using Fn = void (*)(void*, std::uint32_t, std::uint32_t, std::uint8_t);
    __try {
        reinterpret_cast<Fn>(function)(reinterpret_cast<void*>(ctl), 1, 0, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void MarkDone(const Context& x) noexcept {
    g_state.done = true;
    g_state.done_seq = x.seq;
}
// While our box is body 1's current item: the entry verdict once, the engine's
// own exit once, and the viewer digest every tick.
void WatchTick(const Context& x, const Snapshot& s, LogFn log) noexcept {
    const std::uint16_t current = Current(s.row1);
    if (!g_state.applied || current != g_state.applied) {
        g_watch = Watch{};
        return;
    }
    const std::uint16_t act1 = s.act[1];
    if (!g_state.enter_logged) {
        const auto verdict = pol::EntryVerdict(x.now_ms, g_state.on_ms, act1);
        const std::uint64_t ms = x.now_ms >= g_state.on_ms ? x.now_ms - g_state.on_ms : 0;
        if (verdict == pol::Entry::entered) {
            g_state.entered = g_state.enter_logged = true;
            Event(log, "partner_cbox=entered; act=%u; ms=%llu; qd8=%u; id=%u; mission=%u\r\n", unsigned(act1),
                  static_cast<unsigned long long>(ms), unsigned(s.qd8[1]), unsigned(current), x.mission);
        } else if (verdict == pol::Entry::timeout) {
            g_state.enter_logged = true;
            std::uint16_t items[pol::kSubs]{};
            Items(s.row0, items);
            const unsigned k0 = pol::ViewerSub(current, items);
            Event(log,
                  "partner_cbox=entered; ok=0; act=%u; ms=%llu; rec8=0x%x; recC=0x%x; rec_ok=%u; vstock=%u; qd8=%u; id=%u; mission=%u\r\n",
                  unsigned(act1), static_cast<unsigned long long>(ms), s.rec8, s.recc, s.rec_ok ? 1u : 0u,
                  k0 ? unsigned(s.vstock[k0]) : 0u, unsigned(s.qd8[1]), unsigned(current), x.mission);
        }
    } else if (g_state.entered && !g_state.exit_logged && !pol::BoxAct(act1)) {
        // The engine took body 1 out of the box while it stays current (the viewer's
        // stock, a damage exit, a blocked space); logged once per apply.
        g_state.exit_logged = true;
        std::uint16_t items[pol::kSubs]{};
        Items(s.row0, items);
        const unsigned k0 = pol::ViewerSub(current, items);
        Event(log, "partner_cbox=exited; act=%u; want=%u; vstock=%u; rec8=0x%x; recC=0x%x; id=%u; mission=%u\r\n", unsigned(act1),
              x.want ? 1u : 0u, k0 ? unsigned(s.vstock[k0]) : 0u, s.rec8, s.recc, unsigned(current), x.mission);
    }
    const Watch now = WatchOf(s);
    if (g_watch.have) {
        const std::uint32_t changed = Changed(g_watch, now);
        if (changed) {
            const bool latch = pol::WatchLatch(changed, g_watch.act0, now.act0);
            if (latch) g_state.watch_latched = true;
            Event(log, "partner_cbox=viewer_changed; fields=0x%x; viewer_act=%u>%u; latched=%u; body_act=%u; id=%u; mission=%u\r\n", changed,
                  unsigned(g_watch.act0), unsigned(now.act0), latch ? 1u : 0u, unsigned(act1), unsigned(current), x.mission);
        }
    }
    g_watch = now;
}
void ApplyOn(const Context& x, const Snapshot& before, std::uint16_t box, pol::Mapped mapped, LogFn log) noexcept {
    const std::uint16_t current = Current(before.row1);
    std::uint8_t type = 0;
    if (!x.equip_type || !x.equip_type(box, type) || type != pol::kItemType) {
        MarkDone(x);
        Refused(log, x, "type", box, current, "on");
        return;
    }
    std::uint16_t body_items[pol::kSubs]{};
    Items(before.row1, body_items);
    std::uint8_t sub = 0;
    if (!pol::BodySubFor(box, body_items, sub)) {
        MarkDone(x);
        Refused(log, x, "body_items_full", box, current, "on");
        return;
    }
    if (x.busy_reason) {
        Refused(log, x, x.busy_reason, box, current, "on");
        return;
    }
    std::uintptr_t mismatch = 0;
    if (!(x.pins_intact && x.pins_intact(x.base, x.image_span, mismatch))) {
        MarkDone(x);
        Refused(log, x, "full_body_gate", box, current, "on");
        return;
    }
    Snapshot late{};
    const char* why = Capture(x, late);
    if (!why && !SameLease(before, late)) why = "lease_changed";
    if (why) {
        Refused(log, x, why, box, current, "on");
        return;
    }
    Request request{};
    request.equip = box;
    std::int32_t result = INT32_MIN;
    const bool called = DispatchOn(x.base + c::change_equip_rva, late.ctl, &request, &result);
    ++g_dispatches_on;
    Snapshot after{};
    pol::OnReadback r{};
    r.called = called;
    r.result = result;
    r.capture = Capture(x, after);
    r.same_viewer = !r.capture && SameViewer(late, after);
    r.same_body_slots = !r.capture && SameBodySlots(late, after);
    r.after_current = r.capture ? 0 : Current(after.row1);
    r.box = box;
    why = pol::OnVerdict(r);
    MarkDone(x);
    if (pol::LatchUncertain(why)) g_state.uncertain = true;
    if (!why) {
        g_state.applied = box;
        g_state.on_ms = x.now_ms;
        g_state.entered = g_state.enter_logged = g_state.exit_logged = false;
        g_watch = WatchOf(after);
    }
    const unsigned after_sub = r.capture ? 0xFFu : CurrentSub(after.row1);
    Event(log,
          "partner_cbox=apply; id=%u; want=%u; mapped=%s; via=native; sub=%u; body_sub=%u; result=%d; reason=%s; act1=%u; "
          "h4=%08x>%08x; uncertain=%u; seq=%u; mission=%u\r\n",
          unsigned(box), unsigned(x.want_id), pol::MappedName(mapped), unsigned(sub), after_sub, result, why ? why : "ok",
          unsigned(r.capture ? late.act[1] : after.act[1]), late.remote_handles[c::slot_type_item],
          r.capture ? 0u : after.remote_handles[c::slot_type_item], g_state.uncertain ? 1u : 0u, x.seq, x.mission);
    g_reason = nullptr;
}
void ApplyOff(const Context& x, const Snapshot& before, LogFn log) noexcept {
    const std::uint16_t box = g_state.applied;
    if (x.busy_reason) {
        Refused(log, x, x.busy_reason, box, Current(before.row1), "off");
        return;
    }
    std::uintptr_t mismatch = 0;
    if (!(x.pins_intact && x.pins_intact(x.base, x.image_span, mismatch))) {
        MarkDone(x);
        Refused(log, x, "full_body_gate", box, Current(before.row1), "off");
        return;
    }
    Snapshot late{};
    const char* why = Capture(x, late);
    if (!why && !SameLease(before, late)) why = "lease_changed";
    if (why) {
        Refused(log, x, why, box, Current(before.row1), "off");
        return;
    }
    const bool called = DispatchOff(x.base + c::set_current_item_index_rva, late.ctl);
    ++g_dispatches_off;
    Snapshot after{};
    pol::OffReadback r{};
    r.called = called;
    r.capture = Capture(x, after);
    r.same_viewer = !r.capture && SameViewer(late, after);
    r.same_body_slots = !r.capture && SameBodySlots(late, after);
    r.after_sub = r.capture ? 0xFF : static_cast<std::uint8_t>(CurrentSub(after.row1));
    r.after_current = r.capture ? 0xFFFF : Current(after.row1);
    why = pol::OffVerdict(r);
    MarkDone(x);
    if (why) g_state.uncertain = true;
    else {
        g_state.applied = 0;
        g_watch = Watch{};
    }
    Event(log, "partner_cbox=off; id=%u; via=native; reason=%s; act1=%u; h4=%08x>%08x; uncertain=%u; seq=%u; mission=%u\r\n",
          unsigned(box), why ? why : "ok", unsigned(r.capture ? late.act[1] : after.act[1]), late.remote_handles[c::slot_type_item],
          r.capture ? 0u : after.remote_handles[c::slot_type_item], g_state.uncertain ? 1u : 0u, x.seq, x.mission);
    g_reason = nullptr;
}
}  // namespace
void Observe(const Context& x, LogFn log) noexcept {
    if (!TryAcquireSRWLockExclusive(&g_lock)) return;
    if (g_state.mission != x.mission || g_state.epoch != x.epoch) {
        g_lines = g_key_lines = 0;
        g_reason = nullptr;
        g_watch = Watch{};
        pol::Enter(g_state, x.mission, x.epoch, x.realize_gen);
        // A box this lane put on in the previous mission stays ours (partner_cbox_policy::Enter).
        g_carried = g_state.applied;
    } else {
        pol::Enter(g_state, x.mission, x.epoch, x.realize_gen);
    }
    if (!x.enabled) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (g_carried) {
        Event(log, "partner_cbox=carried; id=%u; mission=%u\r\n", unsigned(g_carried), x.mission);
        g_carried = 0;
    }
    // Work is pending for a fresh remote edge, or for an off of our own box.
    const bool pending = (x.want && !(g_state.done && g_state.done_seq == x.seq)) || (!x.want && g_state.applied);
    // The admission gates' own reason (the observer's first failing gate) is the
    // step field of a gates refusal: one change-only line, never a call.
    const char* why = !x.owner ? "thread" : !x.gates ? "gates" : !x.body_ready ? "body" : nullptr;
    Snapshot s{};
    if (!why) why = Capture(x, s);
    if (why) {
        if (pending) Refused(log, x, why, 0, 0, !x.gates && x.gate_reason ? x.gate_reason : x.want ? "on" : "off");
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    WatchTick(x, s, log);
    const std::uint16_t current = Current(s.row1);
    if (g_state.applied && current != g_state.applied) {
        // Our box is no longer body 1's current item (a re-realize or a restarted
        // mission reset the row, or another equip changed it): nothing of ours is
        // on any more.
        Event(log, "partner_cbox=released; id=%u; body_item=%u; act1=%u; mission=%u\r\n", unsigned(g_state.applied), unsigned(current),
              unsigned(s.act[1]), x.mission);
        g_state.applied = 0;
    }
    std::uint16_t box = 0;
    pol::Mapped mapped = pol::Mapped::none;
    if (x.want) {
        std::uint16_t items[pol::kSubs]{};
        Items(s.row0, items);
        mapped = pol::MapViewerBox(x.want_id, items, s.vstock, box);
    }
    const pol::Decision d = pol::Decide(g_state, {x.want, x.seq, current, box});
    if (d.step == pol::Step::refuse) {
        if (d.final) MarkDone(x);
        Refused(log, x, d.reason, box, current, x.want ? "on" : "off");
    } else if (d.step == pol::Step::on) {
        ApplyOn(x, s, box, mapped, log);
    } else if (d.step == pol::Step::off) {
        ApplyOff(x, s, log);
    }
    ReleaseSRWLockExclusive(&g_lock);
}
bool Uncertain() noexcept {
    if (!TryAcquireSRWLockShared(&g_lock)) return false;
    const bool uncertain = g_state.uncertain;
    ReleaseSRWLockShared(&g_lock);
    return uncertain;
}
Status GetStatus() noexcept {
    Status out{};
    if (!TryAcquireSRWLockShared(&g_lock)) return out;
    out.mission = g_state.mission;
    out.dispatches_on = g_dispatches_on;
    out.dispatches_off = g_dispatches_off;
    out.applied = g_state.applied;
    out.entered = g_state.entered;
    out.uncertain = g_state.uncertain;
    out.watch_latched = g_state.watch_latched;
    ReleaseSRWLockShared(&g_lock);
    return out;
}
}  // namespace mgscoop::partner_cbox_equip
