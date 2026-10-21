#pragma once
// F2: the remote's cardboard box on body 1 (instance 1), native (docs/native-puppet.md
// "Cardboard box"; static contract re-cbox.md). Pure policy of partner_cbox_equip:
// the viewer-box mapping, body 1's item sub, the one attempt per remote edge, the
// readback verdicts, the entry watch, the viewer watch latch and the recon hold.
// No game memory and no Windows here.
//
// The engine rule behind the mapping: body 1 wears a box when its OWN current
// item (row1 +0x1C + 2*(row1[0x37] >> 4)) is a box 0x1C8..0x1E2 AND
// GetItemStockCount (0x11AD660, ctl vt+0xC8) has stock for that id. That getter
// scans the VIEWER's item list ([P+0x214], 0x11AD67E), so body 1 can only wear a
// box id the viewer owns with stock > 0.
#include <cstdint>

namespace mgscoop::partner_cbox_policy {

inline constexpr std::uint16_t kBoxFirst = 0x1C8, kBoxLast = 0x1E2;  // EQP_IT_CBox .. (0x10DF517)
inline constexpr std::uint8_t kItemType = 0x11;                       // EQP_TYPE_Item
inline constexpr unsigned kSubs = 8;                                  // item subs 0..7; sub 0 is always "no item"
inline constexpr std::uint32_t kEnterMs = 1500;     // body 1 enters act 77/78 this long after an apply
inline constexpr std::uint32_t kHoldGraceMs = 3000;  // the recon hold outlives the remote's off (exit 77 -> 61 -> 3)

constexpr bool BoxEquip(std::uint32_t id) noexcept { return id >= kBoxFirst && id <= kBoxLast; }
// CBox Wake writes 0x4D (77, 0x110BDD3); 78 is the moving box (stateCBoxMove).
constexpr bool BoxAct(std::uint16_t act) noexcept { return act == 77 || act == 78; }

enum class Mapped : std::uint8_t { none, same, viewer_box };
constexpr const char* MappedName(Mapped m) noexcept {
    return m == Mapped::same ? "same" : m == Mapped::viewer_box ? "viewer_box" : "none";
}
// The box the viewer can lend body 1 (subs 1..7 only; GetItemStockCount also
// scans sub 0, which is always 0): the remote's own id `want` when the viewer
// holds it with stock (same), else the viewer's first box with stock
// (viewer_box: the camo differs from the remote's), else none (viewer_no_box).
constexpr Mapped MapViewerBox(std::uint16_t want, const std::uint16_t (&items)[kSubs], const std::uint16_t (&stocks)[kSubs],
                              std::uint16_t& box) noexcept {
    box = 0;
    if (BoxEquip(want))
        for (unsigned k = 1; k < kSubs; ++k)
            if (items[k] == want && stocks[k] != 0) { box = want; return Mapped::same; }
    for (unsigned k = 1; k < kSubs; ++k)
        if (BoxEquip(items[k]) && stocks[k] != 0) { box = items[k]; return Mapped::viewer_box; }
    return Mapped::none;
}
// Body 1's item sub for `box`: the sub (1..7) already holding it, else the
// first empty one; false when neither (body_items_full). The pickup's "replace
// the current item" path (no free sub) is never used.
constexpr bool BodySubFor(std::uint16_t box, const std::uint16_t (&items)[kSubs], std::uint8_t& sub) noexcept {
    sub = 0;
    if (!BoxEquip(box)) return false;
    for (unsigned k = 1; k < kSubs; ++k)
        if (items[k] == box) { sub = static_cast<std::uint8_t>(k); return true; }
    for (unsigned k = 1; k < kSubs; ++k)
        if (items[k] == 0) { sub = static_cast<std::uint8_t>(k); return true; }
    return false;
}
// The viewer's sub holding `box` (1..7), or 0.
constexpr unsigned ViewerSub(std::uint16_t box, const std::uint16_t (&items)[kSubs]) noexcept {
    for (unsigned k = 1; k < kSubs; ++k)
        if (box && items[k] == box) return k;
    return 0;
}

// --- The edge / attempt state (mission-scoped; the module owns it) ---------
struct State {
    std::uint32_t mission = 0, realize_gen = 0;
    std::uint64_t epoch = 0;       // the driver's mission start (Mission::start_ms): a same-code mission is new
    bool done = false;             // the remote edge `done_seq` was attempted (on or off)
    std::uint32_t done_seq = 0;
    std::uint16_t applied = 0;     // the box this module put on body 1 (0: none)
    std::uint64_t on_ms = 0;       // when it was applied (the entry watch)
    bool entered = false, enter_logged = false, exit_logged = false;
    // A dispatch bracket surprise (the viewer, another body-1 slot or the
    // readback): no further box call this mission, not even an off.
    bool uncertain = false;
    // A viewer change while body 1 was boxed (the per-tick watch, WatchLatch):
    // no new "on" this mission; taking off a box we put on stays allowed.
    bool watch_latched = false;
};
// A new mission (code or start epoch: the driver's edge counter restarts with
// it) clears everything; a new body realize generation re-arms the edge (the
// next tick attempts the remote's current state once more) and keeps the box we
// put on (body 1's equipment row lives in Q, not in the realized body).
constexpr void Enter(State& s, std::uint32_t mission, std::uint64_t epoch, std::uint32_t realize_gen) noexcept {
    if (s.mission != mission || s.epoch != epoch) {
        s = State{};
        s.mission = mission;
        s.epoch = epoch;
        s.realize_gen = realize_gen;
        return;
    }
    if (s.realize_gen != realize_gen) {
        s.realize_gen = realize_gen;
        s.done = false;
        s.done_seq = 0;
    }
}

enum class Step : std::uint8_t { idle, keep, refuse, on, off };
struct DecideIn {
    bool want = false;             // the remote is boxed (manifest cbox_active with a box id)
    std::uint32_t seq = 0;         // the driver's edge counter (Mission::cbox_seq)
    std::uint16_t body_current = 0;  // body 1's current item (row1 +0x1C + 2*(row1[0x37] >> 4))
    std::uint16_t box = 0;         // MapViewerBox's choice (0: none)
};
struct Decision {
    Step step = Step::idle;
    const char* reason = nullptr;  // keep / refuse: why (static)
    bool final = false;            // refuse: marks the edge attempted (never retried for this seq)
};
// On only for a fresh edge and never while the box is already body 1's current
// item (a second ChangeEquip resets the box life record Q+0x160[8+k] and runs
// OnRemoveItem/OnEquipItem while boxed); off only for a box we put on.
constexpr Decision Decide(const State& s, const DecideIn& in) noexcept {
    if (in.want) {
        if (BoxEquip(in.body_current) && (in.body_current == s.applied || in.body_current == in.box))
            return {Step::keep, "current", false};
        if (s.done && s.done_seq == in.seq) return {Step::keep, "attempted", false};
        // The mission latches answer each edge once (final: logged once per edge).
        if (s.uncertain) return {Step::refuse, "uncertain_latched", true};
        if (s.watch_latched) return {Step::refuse, "viewer_changed_latched", true};
        if (!in.box) return {Step::refuse, "viewer_no_box", true};
        return {Step::on, nullptr, false};
    }
    if (!s.applied || in.body_current != s.applied) return {Step::idle, nullptr, false};
    if (s.done && s.done_seq == in.seq) return {Step::keep, "attempted", false};
    if (s.uncertain) return {Step::refuse, "uncertain_latched", true};
    return {Step::off, nullptr, false};
}
// Refusals before a dispatch that are final for the edge (retrying cannot help);
// the rest (restriction, busy, lease, gates, reads) retry on the next tick.
constexpr bool FinalRefusal(const char* why) noexcept {
    if (!why) return false;
    const char* finals[] = {"viewer_no_box", "type", "body_items_full", "full_body_gate"};
    for (const char* f : finals) {
        const char* a = why;
        const char* b = f;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == 0 && *b == 0) return true;
    }
    return false;
}

// --- Readback verdicts -------------------------------------------------------
struct OnReadback {
    bool called = false;           // the SEH-wrapped call returned (ChangeEquip returns its result pointer)
    std::int32_t result = INT32_MIN;
    const char* capture = nullptr;  // the after-capture's refusal (nullptr: read)
    bool same_viewer = false;       // the strict viewer digest (row 0, handles, stocks, vars, records, act)
    bool same_body_slots = false;   // body 1's handles of slots 0..6 except 4 (the item slot)
    std::uint16_t after_current = 0, box = 0;
};
// nullptr = on. ChangeEquip returns 0 even when the box was not found (k = 0),
// so only the row decides: the box must be body 1's current item.
constexpr const char* OnVerdict(const OnReadback& r) noexcept {
    return !r.called ? "seh_fault" : r.result != 0 ? "dispatch_result" : r.capture ? r.capture
         : !r.same_viewer ? "viewer_changed" : !r.same_body_slots ? "body_slots_changed"
         : r.after_current != r.box ? "not_current" : nullptr;
}
// Every surprise latches the mission-scoped uncertain flag except not_current
// (the viewer and body 1's other slots were proven unchanged: nothing to fear,
// the edge is simply done).
constexpr bool LatchUncertain(const char* why) noexcept {
    if (!why) return false;
    const char* a = why;
    const char* b = "not_current";
    while (*a && *a == *b) { ++a; ++b; }
    return !(*a == 0 && *b == 0);
}
struct OffReadback {
    bool called = false;
    const char* capture = nullptr;
    bool same_viewer = false, same_body_slots = false;
    std::uint8_t after_sub = 0xFF;      // row1[0x37] >> 4 after the call
    std::uint16_t after_current = 0xFFFF;
};
// nullptr = off: sub 0 is current (its item is always 0, so state 2's
// can-wear check fails and the box exits through state 0xB).
constexpr const char* OffVerdict(const OffReadback& r) noexcept {
    return !r.called ? "seh_fault" : r.capture ? r.capture : !r.same_viewer ? "viewer_changed"
         : !r.same_body_slots ? "body_slots_changed" : r.after_sub != 0 || r.after_current != 0 ? "still_current" : nullptr;
}

// --- Watches while our box is body 1's current item --------------------------
enum class Entry : std::uint8_t { waiting, entered, timeout };
constexpr Entry EntryVerdict(std::uint64_t now, std::uint64_t on_ms, std::uint16_t body_act) noexcept {
    if (BoxAct(body_act)) return Entry::entered;
    return now >= on_ms && now - on_ms >= kEnterMs ? Entry::timeout : Entry::waiting;
}
// The per-tick viewer digest fields (partner_cbox=viewer_changed; fields=..).
namespace field {
inline constexpr std::uint32_t row_items = 0x1;     // viewer row +0x1C..+0x34 (items, item bytes)
inline constexpr std::uint32_t row_current = 0x2;   // viewer row +0x37 high nibble (its current item sub)
inline constexpr std::uint32_t stocks = 0x4;        // the viewer's item stock words
inline constexpr std::uint32_t vars_items = 0x8;    // vars.items (+0x5A4)
inline constexpr std::uint32_t vars_current = 0x10; // vars.currentItemIndex (+0x67C)
inline constexpr std::uint32_t records = 0x20;      // the viewer's item records Q+0x160[0..7]
inline constexpr std::uint32_t item_handle = 0x40;  // the viewer's slot-4 handle
inline constexpr std::uint32_t row_other = 0x80;    // the rest of viewer row 0 (its weapons, slot nibble)
inline constexpr std::uint32_t handles_other = 0x100;  // the viewer's other six slot handles
// The item state body 1's box could touch (the latching set); the rest is the
// viewer's own selection or weapons (logged only).
inline constexpr std::uint32_t item_state = row_items | stocks | vars_items | records | item_handle;
inline constexpr std::uint32_t selection = row_current | vars_current;
}  // namespace field
// A change the viewer did not select: anything in the item state while the
// viewer's act and its own item selection (row nibble, vars.currentItemIndex)
// stayed the same latches watch_latched (a lab heuristic: in the hidden runs the
// viewer films and touches nothing; a viewer that picks, uses or swaps an item
// changes its act or its selection, and a weapon switch only touches row_other /
// handles_other: those are only logged).
constexpr bool WatchLatch(std::uint32_t changed, std::uint16_t act_before, std::uint16_t act_now) noexcept {
    if (!(changed & field::item_state)) return false;
    if (changed & field::selection) return false;
    return act_before == act_now;
}

// --- The driver's reconciliation hold ----------------------------------------
// No far / vertical / runaway placement while body 1 is boxed (a placement
// would teleport the box): body 1 in 77/78 while the remote wants the box, or
// within kHoldGraceMs of the remote's off. Only with the key (no behaviour
// change while partner_native_cbox=0).
constexpr bool ReconHold(bool enabled, bool body_ok, std::uint16_t body_act, bool want, std::uint64_t now,
                         std::uint64_t off_ms) noexcept {
    if (!enabled || !body_ok || !BoxAct(body_act)) return false;
    return want || (off_ms && now >= off_ms && now - off_ms < kHoldGraceMs);
}

// --- Busy: the other body-1 equipment writers own the tick -------------------
// The box allocates nothing itself, but its Wake allocates an engine handle and
// OnRemoveItem touches body 1's slot-4 handle, which the gun writer's and the
// support lane's digests compare: never dispatch while either is mid-flight.
struct BusyIn {
    bool gun_mode_busy = false;      // body 1 switches / reloads / throws (partner_support_policy::BodyBusy)
    bool gun_writer_active = false;  // a gun writer slot in readiness or tracking
    bool support_uncertain = false;  // the support lane latched uncertain this mission
};
constexpr const char* BusyReason(const BusyIn& b) noexcept {
    return b.gun_mode_busy ? "body_busy" : b.gun_writer_active ? "gun_writer_active" : b.support_uncertain ? "support_uncertain" : nullptr;
}

}  // namespace mgscoop::partner_cbox_policy
