// SCRATCH PROPOSAL: observation only. No hook, engine call, write or input permission.
// The caller must supply an exact fingerprint/full-body contract receipt and a
// current natural Player owner-thread scope. Static bytes are in actual-player-proof.json.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mgscoop::ride_player_observer {
using Address = std::uintptr_t;
struct Memory {
    void* context{};
    bool (*copy)(void*, Address, void*, std::size_t) noexcept = nullptr;
};
struct Scope {
    Address image{}, player{};
    std::uint32_t mission{}, epoch{};
    bool exact_contract{}, paired_scene{}, owner_thread{};
};
struct Sample {
    std::uint32_t id{}, mission{}, epoch{}, selected_id{}, node_base{}, node_count{};
    Address player{}, block{}, primary{}, node{}, record{}, unit{};
    std::uint16_t selected_target{0xffff}, previous_target{0xffff};
    std::uint16_t unit_rider{0xffff}, unit_previous_rider{0xffff};
    std::uint8_t state{}, next_state{}, replication_kind{};
    std::uint16_t replication_target{0xffff};
    std::uint32_t flags{};
    bool unit_matches_player{}, current{};
    // These deliberately stay false. Observation does not qualify a mount or
    // the overloaded ACTION button. The native consuming site is still open.
    bool mounted{}, permit_action{};
};

inline bool Range(Address p, std::size_t n) noexcept {
    constexpr Address last = 0x00007ffffffeffffULL;
    return p >= 0x10000 && n && p <= last && n - 1 <= last - p;
}
template<class T> bool Read(const Memory& m, Address p, T& value) noexcept {
    return m.copy && Range(p, sizeof(T)) && m.copy(m.context, p, &value, sizeof(T));
}
inline bool Add(Address p, std::size_t off, Address& value) noexcept {
    if (off > std::numeric_limits<Address>::max() - p) return false;
    value = p + off;
    return Range(value, 1);
}
template<class T> bool Field(const Memory& m, Address p, std::size_t off, T& value) noexcept {
    Address at{}; return Add(p, off, at) && Read(m, at, value);
}
inline bool Vtable(const Memory& m, Address object, Address image, Address rva) noexcept {
    Address actual{}, expected{};
    return Add(image, rva, expected) && Read(m, object, actual) && actual == expected;
}

// primary is iface - 0x30 only when an exact natural 0x122DEF0 phase call or
// 0x12300F0 typed-message call supplies that iface. It is primary directly for
// the exact natural target query 0x122D110, or resolved from the current block's
// registered Horse interface by ReadHorseAtInstance. Never synthesize these calls.
// The result is diagnostic data and cannot be used as a mount authorization.
inline const char* ReadHorse(const Memory& m, const Scope& scope, Address primary,
                             std::uint32_t id, Sample& out) noexcept {
    out = {};
    if (!scope.exact_contract || !scope.paired_scene || !scope.owner_thread ||
        !scope.mission || !scope.epoch || id > 1) return "scope";
    if (!Range(scope.image, 1) || !Range(scope.player, 1) || !Range(primary, 0x88)) return "range";
    Address slot{}, current{};
    if (!Add(scope.image, 0x2a82c30, slot) || !Read(m, slot, current) || current != scope.player ||
        !Vtable(m, current, scope.image, 0x2295490)) return "player";
    std::uint32_t flags{}, count{}, local{}, selected{}, blocks_count{};
    Address sync{}, q{}, ids{}, pool{}, parts{}, blocks{}, bitset{}, bitdata{}, source{};
    if (!Field(m, current, 0x204, flags) || !(flags & 1) || (flags & (0x100 | 0x40000)) ||
        !Field(m, current, 0xa8, sync) || sync || !Field(m, current, 0x208, count) || count != 2 ||
        !Field(m, current, 0x214, local) || local != 0 || !Field(m, current, 0x218, selected) || selected > 1 ||
        !Field(m, current, 0x60, q) || !Vtable(m, q, scope.image, 0x23ad970) ||
        !Field(m, q, 0x1e8, source) || !Field(m, source, 0x40, count) || count != 2) return "player_state";
    std::uint32_t exists{}, realized{};
    if (!Field(m, q, 0x170, exists) || !(exists & (1u << id)) ||
        !Field(m, current, 0x380, bitset) || !Vtable(m, bitset, scope.image, 0x23067c0) ||
        !Field(m, bitset, 0x10, bitdata) || !Read(m, bitdata, realized) || !(realized & (1u << id))) return "unrealized";
    std::uint16_t id0{}, id1{};
    std::uint8_t pool_index{}, part{};
    if (!Field(m, current, 0x70, ids) || !Read(m, ids, id0) || !Field(m, ids, 2, id1) || id0 != 0 || id1 != 1 ||
        !Field(m, current, 0x78, pool) || !Field(m, pool, id, pool_index) ||
        !Field(m, current, 0x90, parts) || !Field(m, parts, id, part) || part != 3 ||
        !Field(m, current, 0x210, blocks_count) || !blocks_count || blocks_count > 2 || pool_index >= blocks_count ||
        !Field(m, current, 0x378, blocks)) return "slot";
    Address block{}, owner{}, node{}, records{};
    std::uint32_t block_base{}, block_count{}, node_base{}, node_count{};
    if (!Add(blocks, static_cast<std::size_t>(pool_index) * 0x6a8, block) ||
        !Vtable(m, block, scope.image, 0x2295210) || !Field(m, block, 0x138, owner) || owner != current ||
        !Field(m, block, 0xc, block_base) || !Field(m, block, 8, block_count) || !block_count ||
        block_count > 2 || id < block_base || id - block_base >= block_count) return "block";
    Address iface10{}, iface30{};
    if (!Vtable(m, primary, scope.image, 0x23cb600) || !Add(primary, 0x10, iface10) ||
        !Vtable(m, iface10, scope.image, 0x23cb680) || !Add(primary, 0x30, iface30) ||
        !Vtable(m, iface30, scope.image, 0x23cb6b0) || !Field(m, primary, 8, owner) || owner != block ||
        !Field(m, primary, 0x38, node) || !Field(m, node, 0x24, node_base) ||
        !Field(m, node, 0x20, node_count) || node_base != block_base || node_count != block_count ||
        id < node_base || id - node_base >= node_count) return "controller";
    // Exact builder row: hash 0x297A89F8F53D is at row+8, index 0x25
    // at row+0. Node[0x25] and QI-interface[0x25] are independently stored.
    Address registered_node{}, registered_iface{}, registered_primary{};
    if (!Field(m, block, 0x2b8, registered_node) || registered_node != node ||
        !Field(m, block, 0x4e0, registered_iface) || registered_iface != iface30 ||
        !Field(m, block, 0x110, registered_primary) || registered_primary != primary) return "registration";
    Address bitmap{};
    std::uint32_t occupied{};
    if (!Field(m, node, 8, bitmap) || !Read(m, bitmap, occupied) ||
        !(occupied & (1u << (id - node_base))) || !Field(m, primary, 0x68, records) || !records) return "node";
    Sample candidate{};
    candidate.id = id; candidate.mission = scope.mission; candidate.epoch = scope.epoch;
    candidate.selected_id = selected; candidate.player = current; candidate.block = block;
    candidate.primary = primary; candidate.node = node; candidate.node_base = node_base; candidate.node_count = node_count;
    if (!Add(records, static_cast<std::size_t>(id - node_base) * 0x2b0, candidate.record) ||
        !Field(m, candidate.record, 0x10, candidate.state) || !Field(m, candidate.record, 0x11, candidate.next_state) ||
        !Field(m, candidate.record, 0x8c, candidate.previous_target) ||
        !Field(m, candidate.record, 0x8e, candidate.selected_target) ||
        !Field(m, candidate.record, 0x2a0, candidate.flags) || !Field(m, candidate.record, 0x18, candidate.unit)) return "record";
    // Q's kind/target describe native replication data; they do not prove the
    // offline action chooser selected or mounted the same object.
    Address motion{}, motion_record{};
    if (!Field(m, q, 0x1d8, motion) || !Add(motion, static_cast<std::size_t>(id) * 0x40, motion_record) ||
        !Field(m, motion_record, 8, candidate.replication_kind) ||
        !Field(m, motion_record, 0x16, candidate.replication_target)) return "replication_record";
    if (candidate.unit) {
        Address unit_player{}, local_player{};
        std::uint8_t unit_id{};
        if (!Field(m, candidate.unit, 0x78, candidate.unit_rider) ||
            !Field(m, candidate.unit, 0x7c, candidate.unit_previous_rider) ||
            !Field(m, candidate.unit, 0x80, unit_id) || !Field(m, candidate.unit, 0x60, unit_player) ||
            !Field(m, current, 0x238, local_player)) return "unit";
        candidate.unit_matches_player = unit_id == id && candidate.unit_rider == id &&
            candidate.unit_previous_rider == id && local_player && unit_player == local_player;
    }
    // Double-read identity/topology to refuse a changed load/scene/realize
    // structure. This is not a cross-thread synchronization primitive.
    Address again{}, source_again{}, records_again{}, node_again{}, q_again{}, block_again{}, pool_again{};
    std::uint32_t flags_again{}, count_again{}, local_again{}, base_again{}, node_count_again{}, realized_again{};
    std::uint8_t pool_index_again{}, part_again{};
    if (!Read(m, slot, again) || again != current || !Field(m, current, 0x60, q_again) || q_again != q ||
        !Field(m, current, 0x204, flags_again) || flags_again != flags ||
        !Field(m, current, 0x208, count_again) || count_again != 2 ||
        !Field(m, current, 0x214, local_again) || local_again != 0 ||
        !Field(m, current, 0x378, block_again) || block_again != blocks ||
        !Field(m, current, 0x78, pool_again) || pool_again != pool ||
        !Field(m, pool, id, pool_index_again) || pool_index_again != pool_index ||
        !Field(m, parts, id, part_again) || part_again != part ||
        !Field(m, q, 0x1e8, source_again) || source_again != source ||
        !Field(m, primary, 0x68, records_again) || records_again != records ||
        !Field(m, primary, 0x38, node_again) || node_again != node ||
        !Field(m, node, 0x24, base_again) || base_again != node_base ||
        !Field(m, node, 0x20, node_count_again) || node_count_again != node_count ||
        !Read(m, bitdata, realized_again) || realized_again != realized ||
        !Field(m, block, 0x2b8, again) || again != node ||
        !Field(m, block, 0x4e0, again) || again != iface30 ||
        !Field(m, block, 0x110, again) || again != primary ||
        !Vtable(m, primary, scope.image, 0x23cb600) || !Field(m, primary, 8, again) || again != block ||
        !Field(m, block, 0x138, again) || again != current) return "changed";
    candidate.current = true; out = candidate; return nullptr;
}

// Direct, read-only lookup from the already-qualified current Player. This uses
// the exact natural builder's row 0x25; no game method or additional hook is
// needed. Scope.owner_thread must come from the existing natural Player job,
// not merely the thread that happens to call this diagnostic helper.
inline const char* ReadHorseAtInstance(const Memory& m, const Scope& scope,
                                       std::uint32_t id, Sample& out) noexcept {
    out = {};
    if (!scope.exact_contract || !scope.paired_scene || !scope.owner_thread ||
        !scope.mission || !scope.epoch || id > 1) return "scope";
    Address slot{}, current{};
    if (!Add(scope.image, 0x2a82c30, slot) || !Read(m, slot, current) || current != scope.player ||
        !Vtable(m, current, scope.image, 0x2295490)) return "player";
    Address blocks{}, pool{}, block{}, iface{};
    std::uint32_t count{};
    std::uint8_t index{};
    if (!Field(m, current, 0x378, blocks) || !Field(m, current, 0x78, pool) ||
        !Field(m, current, 0x210, count) || !count || count > 2 ||
        !Field(m, pool, id, index) || index >= count ||
        !Add(blocks, static_cast<std::size_t>(index) * 0x6a8, block) ||
        !Vtable(m, block, scope.image, 0x2295210) ||
        !Field(m, block, 0x4e0, iface) || !Vtable(m, iface, scope.image, 0x23cb6b0) ||
        iface < 0x10030) return "resolver";
    Sample sample{};
    if (const char* refusal = ReadHorse(m, scope, iface - 0x30, id, sample)) return refusal;
    Address again{};
    if (sample.block != block || !Field(m, block, 0x4e0, again) || again != iface ||
        !Read(m, slot, again) || again != current) return "changed";
    out = sample; return nullptr;
}
} // namespace mgscoop::ride_player_observer
