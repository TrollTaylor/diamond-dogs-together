// Original MGS Co-op: the gesture lab lever, native part (partner_gesture_lever.hpp,
// docs/native-puppet.md "Gestures", "Lab lever: knock and hand signs").
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstring>
#include "mgscoop/partner_gesture_lever.hpp"

namespace mgscoop::partner_gesture_lever {
namespace {
constexpr std::uintptr_t kMax = 0x7fffffffffff;
constexpr std::size_t kSpan = 0x2a84000;  // covers the player slot
// Player layout (partner_player_contract; the same identities partner_gesture resolves).
constexpr std::uintptr_t kPlayerSlot = 0x2a82c30, kPVtable = 0x2295490, kQVtable = 0x23ad970, kBlockVtable = 0x2295210,
                         kPsVtable = 0x23b09f0, kPsVtableAlt = 0x23b0c70;

std::atomic<bool> g_latched{false};
std::atomic<detail::RequestFn> g_call{nullptr};

int AccessFilter(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
bool Raw(std::uintptr_t at, void* out, std::size_t size) noexcept {
    if (!at || at > kMax || !size || size > kMax - at) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(at), size);
        return true;
    } __except (AccessFilter(GetExceptionCode())) {
        return false;
    }
}
template <class T>
bool Rd(std::uintptr_t at, T& out) noexcept { return Raw(at, &out, sizeof(T)); }

// The request fields of one KnockActionPluginImpl work row (only the request
// functions write +0x18 / +0x20 / +0x24 and set +0xD0 bit 0; the polling clears
// bit 0 when it takes the request).
struct Row {
    std::uint64_t frame;
    std::uint32_t life;
    std::uint8_t kind, d0;
};
bool ReadRow(std::uintptr_t row, Row& out) noexcept {
    return Rd(row + 0x18, out.frame) && Rd(row + 0x20, out.life) && Rd(row + 0x24, out.kind) && Rd(row + 0xd0, out.d0);
}
bool SameRow(const Row& a, const Row& b) noexcept {
    return a.frame == b.frame && a.life == b.life && a.kind == b.kind && ((a.d0 ^ b.d0) & 1u) == 0;
}

struct Identity {
    std::uintptr_t P, Q, blk[2], plugin[2], row[2], ps;
};

// Offline, two instances, this copy's own player at id 0 (P+0x214 == 0), one
// id per block; both blocks' KnockActionPluginImpl with their back pointers and
// distinct work rows; this copy's PS.
const char* Resolve(std::uintptr_t image, Identity& id) noexcept {
    id = Identity{};
    std::uintptr_t vt = 0, sync = 0, ids_at = 0, pools_at = 0, blocks = 0, table = 0;
    std::uint32_t flags = 0, count = 0, local = 0;
    std::uint16_t ids[2]{};
    std::uint8_t pool[2]{};
    if (!image || image > kMax - kSpan || !Rd(image + kPlayerSlot, id.P) || !id.P || !Rd(id.P, vt) || vt != image + kPVtable) return "player";
    if (!Rd(id.P + 0x204, flags) || !(flags & 1u) || (flags & 0x100u) || !Rd(id.P + 0xa8, sync) || sync) return "online";
    if (!Rd(id.P + 0x208, count) || count != 2 || !Rd(id.P + 0x214, local) || local != 0) return "count";
    if (!Rd(id.P + 0x60, id.Q) || !id.Q || !Rd(id.Q, vt) || vt != image + kQVtable) return "q";
    if (!Rd(id.P + 0x70, ids_at) || !Raw(ids_at, ids, sizeof(ids)) || ids[0] != 0 || ids[1] != 1) return "ids";
    if (!Rd(id.P + 0x78, pools_at) || !Raw(pools_at, pool, sizeof(pool)) || pool[0] > 1 || pool[1] > 1 || pool[0] == pool[1]) return "pool";
    if (!Rd(id.P + 0x378, blocks) || !blocks || blocks > kMax - 2 * 0x6a8 || !Rd(id.P + 0x50, table) || !table) return "blocks";
    for (std::uint32_t k = 0; k < 2; ++k) {
        const std::uintptr_t blk = blocks + static_cast<std::uintptr_t>(pool[k]) * 0x6a8;
        std::uintptr_t slot = 0, owner = 0, plugin = 0, base_at = 0, work = 0;
        std::uint32_t n = 0, base = 0;
        if (!Rd(table + static_cast<std::uintptr_t>(pool[k]) * 8, slot) || slot != blk || !Rd(blk, vt) || vt != image + kBlockVtable ||
            !Rd(blk + 0x138, owner) || owner != id.P || !Rd(blk + 8, n) || n != 1 || !Rd(blk + 0xc, base) || base != k) return "block";
        if (!Rd(blk + kBlockPlugin, plugin) || !plugin || !Rd(plugin, vt) || vt != image + kPluginVtableRva || !Rd(plugin + 8, owner) ||
            owner != blk) return "plugin";
        if (!Rd(plugin + 0x38, base_at) || !base_at || !Rd(base_at + 0x24, base) || base != k || !Rd(plugin + 0x68, work) || !work ||
            work > kMax - kRowStride) return "row";
        id.blk[k] = blk;
        id.plugin[k] = plugin;
        id.row[k] = work;  // (id - base) == 0: the block's only id
    }
    if (id.plugin[0] == id.plugin[1] || id.row[0] == id.row[1]) return "plugin";
    std::uintptr_t ps = 0, owner = 0;
    std::uint32_t ps_id = 0;
    if (!Rd(id.blk[0] + 0x78, ps) || !ps || !Rd(ps, vt) || (vt != image + kPsVtable && vt != image + kPsVtableAlt) || !Rd(ps + 8, ps_id) ||
        ps_id != 0 || !Rd(ps + 0x408, owner) || owner != id.blk[0]) return "ps";
    id.ps = ps;
    return nullptr;
}

bool ReadLocal(std::uintptr_t Q, std::uint32_t k, Local& out, std::uint8_t& upper_other) noexcept {
    std::uintptr_t act = 0, upper = 0, q28 = 0, stance = 0, speed = 0;
    return Rd(Q + 0x18, act) && act && Rd(Q + 0x20, upper) && upper && Rd(Q + 0x28, q28) && q28 && Rd(Q + 0x30, stance) && stance &&
           Rd(Q + 0x38, speed) && speed && Rd(act + 2 * k, out.act) && Rd(upper + k, out.upper) && Rd(q28 + k, out.q28) &&
           Rd(stance + k, out.stance) && Rd(speed + k, out.speed) && Rd(upper + (1 - k), upper_other);
}

// The only game call. A separate frame: no C++ object needs unwinding here.
bool CallGuarded(detail::RequestFn fn, std::uintptr_t plugin, std::uint32_t id) noexcept {
    __try {
        fn(reinterpret_cast<void*>(plugin), id);
        return true;
    } __except (AccessFilter(GetExceptionCode())) {
        return false;
    }
}

Outcome Refuse(Outcome o, const char* reason) noexcept {
    o.refusal = reason;
    return o;
}
} // namespace

bool Latched() noexcept { return g_latched.load(std::memory_order_acquire); }

Outcome Fire(std::uintptr_t image, Request request) noexcept {
    Outcome o{};
    o.request = request;
    if (Latched()) return Refuse(o, "latched");
    const Slot slot = SlotFor(request);
    if (!slot.offset) return Refuse(o, "args");
    Identity id{};
    if (const char* why = Resolve(image, id)) return Refuse(o, why);
    // The stock request function behind the slot, and the polling that takes it.
    std::uintptr_t fn = 0, polling = 0;
    if (!Rd(image + kPluginVtableRva + slot.offset, fn) || fn != image + slot.target_rva || !Rd(image + kPluginVtableRva + 0x58, polling) ||
        polling != image + kPollingRva) return Refuse(o, "slot");
    std::uint8_t upper_other = 0;
    if (!ReadLocal(id.Q, 0, o.local, upper_other)) return Refuse(o, "arrays");
    std::uintptr_t recs = 0;
    std::uint32_t ps_base = 0;
    if (!Rd(id.ps + 8, ps_base) || ps_base != 0 || !Rd(id.ps + 0x280, recs) || !recs || !Rd(recs + 4, o.rec4) || !Rd(recs + 8, o.rec8) ||
        !Rd(recs + 0xc, o.recc)) return Refuse(o, "ps");
    if (const char* why = Precondition(request, o.local, o.recc)) return Refuse(o, why);
    Row mine{}, other{};
    if (!ReadRow(id.row[0], mine) || !ReadRow(id.row[1], other)) return Refuse(o, "row");
    detail::RequestFn call = g_call.load(std::memory_order_acquire);
    if (!call) call = reinterpret_cast<detail::RequestFn>(fn);
    if (!CallGuarded(call, id.plugin[0], 0)) {
        g_latched.store(true, std::memory_order_release);
        return Refuse(o, "fault");
    }
    o.called = true;
    Row after{}, other_after{};
    Local local_after{};
    std::uint8_t upper_other_after = 0;
    o.other_same = ReadRow(id.row[1], other_after) && SameRow(other, other_after) && ReadLocal(id.Q, 0, local_after, upper_other_after) &&
                   upper_other_after == upper_other;
    if (!o.other_same) {
        g_latched.store(true, std::memory_order_release);
        return Refuse(o, "other_changed");
    }
    if (ReadRow(id.row[0], after)) {
        o.row_kind = after.kind;
        o.row_d0 = after.d0;
        o.row_life = static_cast<std::uint8_t>(after.life > 0xFF ? 0xFF : after.life);
        o.row_frame = static_cast<std::uint32_t>(after.frame);
    }
    o.id = 0;
    return o;
}

namespace detail {
void SetCallForTest(RequestFn fn) noexcept {
    g_call.store(fn, std::memory_order_release);
    g_latched.store(false, std::memory_order_release);
}
}  // namespace detail

}  // namespace mgscoop::partner_gesture_lever
