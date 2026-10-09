// Original MGS Co-op offline partner spike: guarded player-instance access.
// Contract: native/partner_player_contract.hpp (tools/generate_partner_contract.py).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <xmmintrin.h>
#include <strsafe.h>
#include <cmath>
#include <cstring>
#include "mgscoop/partner_player.hpp"
#include "mgscoop/partner_equip_wire.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_player {
namespace {
namespace c = partner_player_contract;

template <class T>
bool ReadValue(std::uintptr_t address, T& value) noexcept {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), &value,
                                        sizeof(value), &read) && read == sizeof(value);
}
bool ReadBlock(std::uintptr_t address, void* out, std::size_t size) noexcept {
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &read) &&
           read == size;
}
template <class T>
T Field(const unsigned char* block, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, block + offset, sizeof(value));
    return value;
}

// One interlocked or aligned store each; SEH turns a stale pointer into false.
bool InterlockedOrDword(std::uintptr_t address, long value) noexcept {
    __try { _InterlockedOr(reinterpret_cast<volatile long*>(address), value); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool InterlockedAndDword(std::uintptr_t address, long value) noexcept {
    __try { _InterlockedAnd(reinterpret_cast<volatile long*>(address), value); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool InterlockedOrByte(std::uintptr_t address, char value) noexcept {
    __try { _InterlockedOr8(reinterpret_cast<volatile char*>(address), value); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ExchangeByte(std::uintptr_t address, char value) noexcept {
    __try { _InterlockedExchange8(reinterpret_cast<volatile char*>(address), value); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool StoreAligned16(std::uintptr_t address, const float* values) noexcept {
    __try { _mm_store_ps(reinterpret_cast<float*>(address), _mm_loadu_ps(values)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::uint32_t Fnv1a(const unsigned char* bytes, std::size_t size) noexcept {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 16777619u; }
    return hash;
}

// P block covers +0x00..+0x218 (Q +0x60 .. local +0x214); Q block +0x170..+0x1F0.
constexpr std::size_t kPBlock = c::p_local + 4;
constexpr std::size_t kQBase = c::q_exists_mask;
constexpr std::size_t kQBlock = c::q_s + 8 - kQBase;
constexpr std::size_t kSBlock = c::s_pos + 8;
static_assert(c::p_q < kPBlock && c::p_sync < kPBlock && c::p_flags < kPBlock && c::p_count < kPBlock);
static_assert(c::q_bits_b > kQBase && c::q_parts_dirty > kQBase);

void ReadDiagnostics(const Addresses& a, State& s) noexcept {
    std::uintptr_t ids = 0, pool = 0, parts = 0, bitset = 0, bitset_data = 0;
    unsigned char pblock[c::p_parts + 8 - c::p_ids]{};
    s.arrays_ok = ReadBlock(s.P + c::p_ids, pblock, sizeof(pblock));
    if (s.arrays_ok) {
        ids = Field<std::uintptr_t>(pblock, 0);
        pool = Field<std::uintptr_t>(pblock, c::p_pool - c::p_ids);
        parts = Field<std::uintptr_t>(pblock, c::p_parts - c::p_ids);
        s.arrays_ok = ReadBlock(ids, s.ids, sizeof(s.ids)) && ReadBlock(pool, s.pool, sizeof(s.pool)) &&
                      ReadBlock(parts, s.parts, sizeof(s.parts));
    }
    if (!s.arrays_ok) {
        s.ids[0] = s.ids[1] = 0xFFFF;
        s.pool[0] = s.pool[1] = 0xFF;
        s.parts[0] = s.parts[1] = 0;
    }
    if (!ReadValue(s.P + c::p_bitset, bitset) || !ReadValue(bitset + c::bitset_data, bitset_data) ||
        !ReadValue(bitset_data, s.rbits)) s.rbits = 0;
    if (!ReadValue(s.exists, s.s0)) s.s0 = 0;
    if (!s.local_bits || !ReadValue(s.local_bits, s.s8)) s.s8 = 0;
    s.slots_ok = ReadBlock(s.slots, s.slot, sizeof(s.slot));
    if (!s.slots_ok) std::memset(s.slot, 0, sizeof(s.slot));
    s.glob_ok = ReadValue(a.glob_suppress, s.glob);
    std::uintptr_t g = 0, x = 0;
    std::uint64_t guard_ptr = 0;
    std::uint32_t guard_mode = 0;
    s.pb_ok = ReadValue(a.glob_parts, g) && ReadValue(g + c::g_x, x) && ReadValue(x + c::x_guard_ptr, guard_ptr) &&
              ReadValue(x + c::x_guard_mode, guard_mode) && ReadBlock(x + c::x_flag, s.pb_flag, sizeof(s.pb_flag)) &&
              ReadBlock(x + c::x_state, s.pb_state, sizeof(s.pb_state));
    s.pb_guard = s.pb_ok ? static_cast<std::uint32_t>(guard_ptr == 0 || guard_mode == 2) : 0;
    // The requested (descriptor) look of instance 1's parts slot (0x9B47D0
    // slot mapping, 0x9B442D store); null before the first request.
    s.req_ok = false;
    std::uint32_t local = 0;
    std::uintptr_t req = 0;
    unsigned char look[7]{};
    if (x && ReadValue(g + c::g_local, local)) {
        const std::uintptr_t slot = local == 1 ? 0 : (local > 1 ? 2 : 1);
        if (ReadValue(x + c::x_req_look + 8 * slot, req) && req && ReadBlock(req, look, sizeof(look))) {
            s.req_ok = true;
            s.req_look = partner::Look{look[0], look[1], look[2], look[3], Field<std::uint16_t>(look, 4), look[6]};
        }
    }
    std::int32_t bank = -1;
    s.rec_ok = ReadValue(a.record_bank, bank) && (bank == 0 || bank == 1);
    if (s.rec_ok) {
        const std::uintptr_t record = a.record_base + (static_cast<std::uintptr_t>(bank) * c::record_bank_entries + 1) * c::record_stride;
        s.rec_ok = ReadBlock(record + c::record_pos, s.rec1, sizeof(s.rec1)) && ReadValue(record + c::record_status, s.rec1_status);
    }
    std::uintptr_t vars = 0;
    s.vars_ok = ReadValue(a.vars_slot, vars) && ReadValue(vars + c::vars_pos_x, s.vars_pos[0]) &&
                ReadValue(vars + c::vars_pos_y, s.vars_pos[1]) && ReadValue(vars + c::vars_pos_z, s.vars_pos[2]);
    if (!ReadValue(a.mirror_object, s.mirror_object)) s.mirror_object = 0;
}

void LogMm(char* out, std::size_t capacity, const float* xyz) noexcept {
    StringCchPrintfA(out, capacity, "%d,%d,%d", ToMm(xyz[0]), ToMm(xyz[1]), ToMm(xyz[2]));
}

struct Preserve {
    DWORD error = GetLastError();
    ~Preserve() { SetLastError(error); }
};

// 0 (unpinned) stays 0.
std::uintptr_t Pinned(std::uintptr_t base, std::uintptr_t rva) noexcept { return rva ? base + rva : 0; }

// Shared prologue of every write: fresh read, then the mandatory guards.
const char* Prepare(const Addresses& a, std::uint32_t index, bool terminating, WriteKind kind, State& state) noexcept {
    state = State{};
    const Refusal refusal = ReadState(a, state);
    return CheckWrite(refusal, state, index, terminating, kind);
}
} // namespace

Addresses AddressesFor(const void* base) noexcept {
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    Addresses a{};
    if (!b) return a;
    a.player_slot = b + c::player_slot_rva;
    a.p_vtable = b + c::p_vtable_rva;
    a.q_vtable = b + c::q_vtable_rva;
    a.policy_vtable = b + c::policy_vtable_rva;
    a.policy_function = b + c::policy_function_rva;
    a.realizer_vtable = b + c::realizer_vtable_rva;
    a.glob_suppress = b + c::glob_suppress_rva;
    a.glob_parts = b + c::glob_parts_rva;
    a.vars_slot = b + c::vars_slot_rva;
    a.record_bank = b + c::record_bank_rva;
    a.record_base = b + c::record_base_rva;
    a.mirror_object = b + c::mirror_object_rva;
    a.image_base = b;
    a.code_begin = b + c::code_begin_rva;
    a.code_end = b + c::code_end_rva;
    a.image_begin = b;
    a.image_end = b + c::image_span;
    a.block_vtable = b + c::block_vtable_rva;
    a.ps_vtable = b + c::ps_vtable_rva;
    a.ps_vtable_alt = b + c::ps_vtable_alt_rva;
    a.set_stance = b + c::set_stance_rva;
    a.ps_component = b + c::ps_component_rva;
    a.cs_vtable = b + c::cs_vtable_rva;
    a.get_transform = b + c::get_transform_rva;
    a.t_vtable = Pinned(b, c::t_vtable_rva);
    a.a0_vtable = Pinned(b, c::a0_vtable_rva);
    a.c0_vtable = Pinned(b, c::c0_vtable_rva);
    a.pp_vtable = b + c::pp_vtable_rva;
    a.cam_vtable = b + c::cam_vtable_rva;
    a.plugin_vt[0] = b + c::plugin_vt0_rva;
    a.plugin_vt[1] = b + c::plugin_vt1_rva;
    a.plugin_vt[2] = b + c::plugin_vt2_rva;
    a.plugin_vt[3] = b + c::plugin_vt3_rva;
    a.dm_prim_vtable = b + c::dm_prim_vt_rva;
    a.dm_iface_vtable = b + c::dm_iface_vt_rva;
    a.script_object = b + c::g_rva;
    a.go_vtable = b + c::go_vtable_rva;
    a.gun_node_vtable = b + c::gun_node_vtable_rva;
    a.gun_prim_vtable = b + c::gun_prim_vt_rva;
    a.gun_iface_vtable = b + c::gun_iface_vt_rva;
    a.rate_sp_vtable = b + c::rate_sp_vtable_rva;
    a.rate_rec_vtable = b + c::rate_rec_vtable_rva;
    a.equip_types = b + c::equip_types_rva;
    a.equip_types_vtable = b + c::equip_types_vt_rva;
    a.gun_basic = b + c::gun_basic_rva;
    a.alias_table = b + c::alias_table_rva;
    a.equip_resource = b + c::equip_resource_rva;
    a.equip_ctl_vtable = b + c::equip_ctl_vt_rva;
    a.store_vtable = b + c::store_vt_rva;
    a.store_vtable_alt = b + c::store_vt_derived_rva;
    return a;
}

namespace detail {
bool CodeGateWith(const void* base, std::size_t span, const GateSite* sites, std::size_t site_count,
                  const GateSlot* slots, std::size_t slot_count, std::uintptr_t& mismatch_rva) noexcept {
    const Preserve preserve;
    mismatch_rva = 0;
    if (!base || (site_count && !sites) || (slot_count && !slots)) return false;
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    for (std::size_t i = 0; i < site_count; ++i) {
        const GateSite& site = sites[i];
        unsigned char copy[96]{};
        if (site.size == 0 || site.size > sizeof(copy) || site.rva > span || site.size > span - site.rva ||
            !ReadBlock(b + site.rva, copy, site.size) || std::memcmp(copy, site.bytes, site.size) != 0) {
            mismatch_rva = site.rva;
            return false;
        }
    }
    for (std::size_t i = 0; i < slot_count; ++i) {
        const GateSlot& slot = slots[i];
        std::uintptr_t value = 0;
        if (slot.vtable_rva > span || slot.offset > span - slot.vtable_rva || 8 > span - slot.vtable_rva - slot.offset ||
            slot.target_rva >= span || !ReadValue(b + slot.vtable_rva + slot.offset, value) || value != b + slot.target_rva) {
            mismatch_rva = slot.vtable_rva;
            return false;
        }
    }
    return true;
}
} // namespace detail

bool CodeGate(const void* base, std::size_t span, std::uintptr_t& mismatch_rva) noexcept {
    detail::GateSite sites[sizeof(c::code_sites) / sizeof(c::code_sites[0])]{};
    detail::GateSlot slots[sizeof(c::vtable_slots) / sizeof(c::vtable_slots[0])]{};
    for (std::size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i)
        sites[i] = {c::code_sites[i].rva, c::code_sites[i].size, c::code_sites[i].bytes};
    for (std::size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i)
        slots[i] = {c::vtable_slots[i].vtable_rva, c::vtable_slots[i].offset, c::vtable_slots[i].target_rva};
    return detail::CodeGateWith(base, span, sites, sizeof(sites) / sizeof(sites[0]), slots,
                                sizeof(slots) / sizeof(slots[0]), mismatch_rva);
}

const char* RefusalName(Refusal refusal) noexcept {
    switch (refusal) {
    case Refusal::none: return "ok";
    case Refusal::player_absent: return "player_absent";
    case Refusal::vtable: return "vtable";
    case Refusal::count: return "count";
    case Refusal::local_index: return "local_index";
    case Refusal::online_bit: return "online_bit";
    case Refusal::sync_present: return "sync_present";
    case Refusal::q_vtable: return "q_vtable";
    case Refusal::s_null: return "s_null";
    }
    return "unknown";
}

Refusal ReadState(const Addresses& a, State& s) noexcept {
    const Preserve preserve;
    s = State{};
    s.ids[0] = s.ids[1] = 0xFFFF;
    s.pool[0] = s.pool[1] = 0xFF;
    if (!ReadValue(a.player_slot, s.P) || !s.P) return Refusal::player_absent;
    unsigned char p[kPBlock]{};
    if (!ReadBlock(s.P, p, sizeof(p))) return Refusal::player_absent;
    if (Field<std::uintptr_t>(p, 0) != a.p_vtable) return Refusal::vtable;
    s.flags = Field<std::uint32_t>(p, c::p_flags);
    s.count = Field<std::uint32_t>(p, c::p_count);
    s.realized_count = Field<std::uint32_t>(p, c::p_realized_count);
    s.local = Field<std::uint32_t>(p, c::p_local);
    s.sync = Field<std::uintptr_t>(p, c::p_sync);
    s.realizer = Field<std::uintptr_t>(p, c::p_realizer);
    s.Q = Field<std::uintptr_t>(p, c::p_q);
    if (!(s.flags & c::flag_valid)) return Refusal::player_absent;
    if (s.flags & c::flag_online) return Refusal::online_bit;
    if (s.sync != 0) return Refusal::sync_present;
    if (s.count != 2) return Refusal::count;
    if (s.local != 0) return Refusal::local_index;
    std::uintptr_t q_vtable = 0;
    if (!s.Q || !ReadValue(s.Q, q_vtable) || q_vtable != a.q_vtable) return Refusal::q_vtable;
    unsigned char q[kQBlock]{};
    if (!ReadBlock(s.Q + kQBase, q, sizeof(q))) return Refusal::q_vtable;
    s.q170 = Field<std::uint32_t>(q, c::q_exists_mask - kQBase);
    s.q180 = Field<std::uint32_t>(q, c::q_parts_dirty - kQBase);
    s.q194 = Field<std::uint32_t>(q, c::q_bits_a - kQBase);
    s.q198 = Field<std::uint32_t>(q, c::q_bits_b - kQBase);
    s.bits_ok = true;
    s.S = Field<std::uintptr_t>(q, c::q_s - kQBase);
    unsigned char sblock[kSBlock]{};
    if (!s.S || !ReadBlock(s.S, sblock, sizeof(sblock))) return Refusal::s_null;
    s.exists = Field<std::uintptr_t>(sblock, c::s_exists);
    s.local_bits = Field<std::uintptr_t>(sblock, c::s_local);
    s.slots = Field<std::uintptr_t>(sblock, c::s_pos);
    if (!s.exists || !s.slots) return Refusal::s_null;
    ReadDiagnostics(a, s);
    return Refusal::none;
}

bool Realized(const State& state, std::uint32_t index) noexcept {
    return index < 2 && state.arrays_ok && state.ids[index] != 0xFFFF && state.pool[index] != 0xFF;
}

const char* CheckWrite(Refusal refusal, const State& state, std::uint32_t index, bool terminating, WriteKind kind) noexcept {
    if (refusal != Refusal::none) return RefusalName(refusal);
    // Defence in depth: re-assert the hard rules on the state itself.
    if (state.flags & c::flag_online) return RefusalName(Refusal::online_bit);
    if (state.sync != 0) return RefusalName(Refusal::sync_present);
    if (!(state.flags & c::flag_valid)) return RefusalName(Refusal::player_absent);
    if (state.count != 2) return RefusalName(Refusal::count);
    if (state.local != 0) return RefusalName(Refusal::local_index);
    if (!state.Q || !state.S || !state.exists || !state.slots) return RefusalName(Refusal::s_null);
    if (index != 1 || index == state.local || index >= state.count) return "index";
    if (terminating && kind == WriteKind::set) return "terminating";
    return nullptr;
}

const char* OrRealizeBits(const Addresses& a, std::uint32_t index, bool terminating, std::uint32_t& q194,
                          std::uint32_t& q198) noexcept {
    const Preserve preserve;
    State s{};
    if (const char* reason = Prepare(a, index, terminating, WriteKind::set, s)) return reason;
    if (s.Q % 4) return "alignment";
    const long bit = 1L << index;
    if (!InterlockedOrDword(s.Q + c::q_bits_a, bit) || !InterlockedOrDword(s.Q + c::q_bits_b, bit)) return "fault";
    if (!ReadValue(s.Q + c::q_bits_a, q194) || !ReadValue(s.Q + c::q_bits_b, q198)) return "fault";
    return nullptr;
}

const char* ClearRealizeBits(const Addresses& a, std::uint32_t index, bool terminating, std::uint32_t& q194,
                             std::uint32_t& q198) noexcept {
    const Preserve preserve;
    State s{};
    if (const char* reason = Prepare(a, index, terminating, WriteKind::restore, s)) return reason;
    if (s.Q % 4) return "alignment";
    const long mask = ~(1L << index);
    if (!InterlockedAndDword(s.Q + c::q_bits_a, mask) || !InterlockedAndDword(s.Q + c::q_bits_b, mask)) return "fault";
    if (!ReadValue(s.Q + c::q_bits_a, q194) || !ReadValue(s.Q + c::q_bits_b, q198)) return "fault";
    return nullptr;
}

const char* WriteSlot(const Addresses& a, std::uint32_t index, bool terminating, const float (&xyzw)[4]) noexcept {
    const Preserve preserve;
    State s{};
    if (const char* reason = Prepare(a, index, terminating, WriteKind::set, s)) return reason;
    for (float value : xyzw) if (!std::isfinite(value)) return "non_finite";
    const std::uintptr_t slot = s.slots + static_cast<std::uintptr_t>(index) * c::slot_stride;
    if (slot % 16) return "alignment";
    return StoreAligned16(slot, xyzw) ? nullptr : "fault";
}

const char* ForceExists(const Addresses& a, std::uint32_t index, bool terminating) noexcept {
    const Preserve preserve;
    State s{};
    if (const char* reason = Prepare(a, index, terminating, WriteKind::set, s)) return reason;
    return InterlockedOrByte(s.exists + index / 8, static_cast<char>(1u << (index % 8))) ? nullptr : "fault";
}

const char* SetPolicyRelax(const Addresses& a, bool relax, std::uint8_t restore_value, bool terminating,
                           std::uint8_t& previous) noexcept {
    const Preserve preserve;
    State s{};
    if (const char* reason = Prepare(a, 1, terminating, relax ? WriteKind::set : WriteKind::restore, s)) return reason;
    std::uintptr_t realizer_vtable = 0, policy = 0, policy_vtable = 0, function = 0, owner = 0;
    if (!s.realizer || !ReadValue(s.realizer, realizer_vtable) || realizer_vtable != a.realizer_vtable) return "realizer_vtable";
    if (!ReadValue(s.realizer + c::realizer_policy, policy) || !policy || !ReadValue(policy, policy_vtable) ||
        policy_vtable != a.policy_vtable ||
        !ReadValue(policy_vtable + 0x10, function) || function != a.policy_function) return "policy_vtable";
    if (!ReadValue(policy + c::policy_owner, owner) || owner != s.P) return "policy_owner";
    if (!ReadValue(policy + c::policy_relax, previous)) return "fault";
    return ExchangeByte(policy + c::policy_relax, static_cast<char>(relax ? 1 : restore_value)) ? nullptr : "fault";
}

bool ReadAnim(const State& s, Anim& out) noexcept {
    const Preserve preserve;
    out = Anim{};
    if (!s.Q) return false;
    std::uintptr_t p18 = 0, p28 = 0, p30 = 0, p38 = 0, pd8 = 0, r1d8 = 0, r1e0 = 0, r1f8 = 0;
    unsigned char r1[c::q_rec_1d8_size]{}, r2[c::q_rec_1e0_size]{}, r3[c::q_rec_1f8_size]{};
    const bool ok = ReadValue(s.Q + c::q_anim_18, p18) && ReadValue(s.Q + c::q_anim_28, p28) &&
                    ReadValue(s.Q + c::q_anim_30, p30) && ReadValue(s.Q + c::q_anim_38, p38) &&
                    ReadValue(s.Q + c::q_anim_d8, pd8) && ReadValue(s.Q + c::q_rec_1d8, r1d8) &&
                    ReadValue(s.Q + c::q_rec_1e0, r1e0) && ReadValue(s.Q + c::q_rec_1f8, r1f8) &&
                    ReadBlock(p18, out.act, sizeof(out.act)) && ReadBlock(p28, out.q28, sizeof(out.q28)) &&
                    ReadBlock(p30, out.stance, sizeof(out.stance)) && ReadBlock(p38, out.speed, sizeof(out.speed)) &&
                    ReadBlock(pd8, out.qd8, sizeof(out.qd8)) && ReadBlock(r1d8, r1, sizeof(r1)) &&
                    ReadBlock(r1e0, r2, sizeof(r2)) && ReadBlock(r1f8, r3, sizeof(r3));
    if (!ok) { out = Anim{}; return false; }
    out.m1d8 = Fnv1a(r1, sizeof(r1));
    out.m1e0 = Fnv1a(r2, sizeof(r2));
    out.m1f8 = Fnv1a(r3, sizeof(r3));
    return true;
}

sync::AnimState AnimSlot(const Anim& anim, std::uint32_t index) noexcept {
    sync::AnimState state{};
    if (index >= 2) return state;
    state.act = anim.act[index];
    if (anim.stance[index] >= 1 && anim.stance[index] <= 3 && anim.speed[index] <= 3) {
        state.stance = anim.stance[index];
        state.speed = anim.speed[index];
        state.flags = sync::AnimFlag::Valid;
    }
    return state;
}

bool ReadLooks(const State& s, Looks& out) noexcept {
    const Preserve preserve;
    out = Looks{};
    if (!s.Q) return false;
    std::uintptr_t pt = 0, pp_ = 0, pc = 0, ph = 0, pf = 0, pe = 0;
    std::uint8_t type[2]{}, parts[2]{}, camo[2]{}, hand[2]{}, face_equip[2]{};
    std::uint16_t face[2]{};
    const bool ok = ReadValue(s.Q + c::q_look_type, pt) && ReadValue(s.Q + c::q_look_parts, pp_) &&
                    ReadValue(s.Q + c::q_look_camo, pc) && ReadValue(s.Q + c::q_look_hand, ph) &&
                    ReadValue(s.Q + c::q_look_face, pf) && ReadValue(s.Q + c::q_look_face_equip, pe) &&
                    ReadBlock(pt, type, sizeof(type)) && ReadBlock(pp_, parts, sizeof(parts)) && ReadBlock(pc, camo, sizeof(camo)) &&
                    ReadBlock(ph, hand, sizeof(hand)) && ReadBlock(pf, face, sizeof(face)) &&
                    ReadBlock(pe, face_equip, sizeof(face_equip));
    if (!ok) return false;
    for (unsigned i = 0; i < 2; ++i) out.look[i] = partner::Look{type[i], parts[i], camo[i], hand[i], face[i], face_equip[i]};
    return true;
}

namespace {
bool InRange(std::uintptr_t value, std::uintptr_t begin, std::uintptr_t end) noexcept {
    return begin < end && value >= begin && value < end;
}
std::uintptr_t Rva(const Addresses& a, std::uintptr_t value) noexcept {
    return a.image_base && value >= a.image_base ? value - a.image_base : value;
}
// A component callable through `slot` (and optionally `slot2`): non-null, its
// vtable inside the image (equal to `pinned` when pinned), targets in code.
bool Callable(const Addresses& a, std::uintptr_t object, std::size_t slot, std::size_t slot2, std::uintptr_t pinned,
              std::uintptr_t& vt, std::uintptr_t& target, std::uintptr_t& target2) noexcept {
    vt = target = target2 = 0;
    if (!object || !ReadValue(object, vt) || !InRange(vt, a.image_begin, a.image_end)) return false;
    if (pinned && vt != pinned) return false;
    if (!ReadValue(vt + slot, target) || !InRange(target, a.code_begin, a.code_end)) return false;
    if (slot2 && (!ReadValue(vt + slot2, target2) || !InRange(target2, a.code_begin, a.code_end))) return false;
    return true;
}
// The core chain (block, PS) of instance k; nullptr when it holds.
const char* ResolveCore(const Addresses& a, const State& s, std::uint32_t k, Block& b) noexcept {
    if (k >= 2 || !Realized(s, k)) return "unrealized";
    b.id = s.ids[k];
    std::uintptr_t blocks = 0, table = 0, listed = 0, owner = 0;
    if (!ReadValue(s.P + c::p_blocks, blocks) || !blocks) return "blk_vtable";
    b.blk = blocks + static_cast<std::uintptr_t>(s.pool[k]) * c::block_stride;
    if (!ReadValue(b.blk, b.blk_vt) || b.blk_vt != a.block_vtable) return "blk_vtable";
    if (!ReadValue(s.P + c::p_block_table, table) || !ReadValue(table + 8 * static_cast<std::uintptr_t>(s.pool[k]), listed) ||
        listed != b.blk || !ReadValue(b.blk + c::blk_owner, owner) || owner != s.P) return "blk_owner";
    if (!ReadValue(b.blk + c::blk_id_count, b.id_count) || !ReadValue(b.blk + c::blk_base_id, b.base_id) ||
        b.id < b.base_id || b.id - b.base_id >= b.id_count) return "blk_index";
    std::uintptr_t slot0 = 0, back = 0;
    std::uint32_t ps_base = 0;
    if (!ReadValue(b.blk + c::blk_ps, b.ps) || !b.ps || !ReadValue(b.ps, b.ps_vt) || (b.ps_vt != a.ps_vtable && b.ps_vt != a.ps_vtable_alt) ||
        !ReadValue(b.ps_vt, slot0) || slot0 != a.set_stance) return "ps_vtable";
    if (!ReadValue(b.ps + c::ps_blk, back) || back != b.blk || !ReadValue(b.ps + c::ps_base_id, ps_base) || ps_base != b.base_id)
        return "ps_back";
    // Optional components (reset targets, logged in the census).
    std::uintptr_t unused = 0;
    if (ReadValue(b.blk + c::blk_a0, b.a0) && b.a0) Callable(a, b.a0, c::a0_vt_reset, 0, a.a0_vtable, b.a0_vt, b.a080, unused);
    if (ReadValue(b.blk + c::blk_c0, b.c0) && b.c0) Callable(a, b.c0, c::c0_vt_reset, 0, a.c0_vtable, b.c0_vt, b.c090, unused);
    if (ReadValue(b.blk + c::blk_b70, b.b70) && b.b70) ReadValue(b.b70, b.b70_vt);
    return nullptr;
}
// The SetStance prerequisites of instance k after ResolveCore: the stance
// words (with this id's word read into b.psw) and the PS+0x260 component.
const char* ResolveStanceParts(Block& b) noexcept {
    std::uintptr_t words = 0, sub = 0;
    std::uint8_t psw = 0;
    if (!ReadValue(b.ps + c::ps_stance, words) || !words ||
        !ReadValue(words + 8 * static_cast<std::uintptr_t>(b.id - b.base_id), psw)) return "ps_words";
    if (!ReadValue(b.ps + c::ps_sub260, sub) || !sub) return "ps_sub";
    b.psw = psw;
    return nullptr;
}
// The transform chain (CS, entry, T) of instance k after ResolveCore.
const char* ResolveTransform(const Addresses& a, Block& b) noexcept {
    std::uintptr_t slot = 0, entries = 0, object = 0;
    std::uint32_t cs_count = 0, cs_base = 0;
    if (!ReadValue(b.ps + c::ps_cs, b.cs) || !b.cs || !ReadValue(b.cs, b.cs_vt) || b.cs_vt != a.cs_vtable ||
        !ReadValue(b.cs_vt + 0x388, slot) || slot != a.get_transform) return "cs_vtable";
    if (!ReadValue(b.cs + 8, cs_count) || !ReadValue(b.cs + c::cs_base_id, cs_base) || b.id < cs_base ||
        b.id - cs_base >= cs_count || !ReadValue(b.cs + c::cs_entries, entries) || !entries) return "entry";
    b.entry = entries + static_cast<std::uintptr_t>(b.id - cs_base) * c::cs_entry_stride;
    if (!ReadValue(b.entry + c::cs_entry_transform, object)) return "entry";
    if (!object) return "t_null";
    b.t = object + c::transform_view;
    if (!Callable(a, b.t, c::t_vt_set_pose, c::t_vt_set_rot, a.t_vtable, b.t_vt, b.t68, b.t70)) return "t_class";
    if (!ReadValue(b.t_vt + c::t_vt_set_position, b.t58) || !InRange(b.t58, a.code_begin, a.code_end)) return "t_class";
    return nullptr;
}

using SetRotFn = void (*)(void*, const float*);
using SetPoseFn = void (*)(void*, const float*, const float*);
using ResetFn = void (*)(void*, std::uint32_t);
using StanceFn = void (*)(void*, std::uint32_t, std::uint32_t);
using ComponentFn = void* (*)(void*);

// SEH wrappers: POD arguments only (no unwinding objects in a __try frame).
bool CallTransform(std::uintptr_t t, std::uintptr_t set_rot, std::uintptr_t set_pose, const float* pos, const float* quat,
                   std::uintptr_t c0, std::uintptr_t c0_reset, std::uintptr_t a0, std::uintptr_t a0_reset,
                   std::uint32_t id) noexcept {
    __try {
        reinterpret_cast<SetRotFn>(set_rot)(reinterpret_cast<void*>(t), quat);
        reinterpret_cast<SetPoseFn>(set_pose)(reinterpret_cast<void*>(t), pos, quat);
        if (c0_reset) reinterpret_cast<ResetFn>(c0_reset)(reinterpret_cast<void*>(c0), id);
        if (a0_reset) reinterpret_cast<ResetFn>(a0_reset)(reinterpret_cast<void*>(a0), id);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallPosition(std::uintptr_t t, std::uintptr_t set_pos, const float* pos) noexcept {
    __try {
        reinterpret_cast<SetRotFn>(set_pos)(reinterpret_cast<void*>(t), pos);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallStance(std::uintptr_t function, std::uintptr_t object, std::uint32_t id, std::uint32_t stance) noexcept {
    __try { reinterpret_cast<StanceFn>(function)(reinterpret_cast<void*>(object), id, stance); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallComponent(std::uintptr_t function, std::uintptr_t ps, std::uintptr_t& out) noexcept {
    __try { out = reinterpret_cast<std::uintptr_t>(reinterpret_cast<ComponentFn>(function)(reinterpret_cast<void*>(ps))); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
}
bool StoreU16(std::uintptr_t address, std::uint16_t value) noexcept {
    __try { *reinterpret_cast<volatile std::uint16_t*>(address) = value; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool StoreU8(std::uintptr_t address, std::uint8_t value) noexcept {
    __try { *reinterpret_cast<volatile std::uint8_t*>(address) = value; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Shared prologue of the instance-1 game calls: write guards, realized with
// parts loaded, ids[index] == index.
const char* PrepareCall(const Addresses& a, std::uint32_t index, bool terminating, State& s) noexcept {
    if (const char* reason = Prepare(a, index, terminating, WriteKind::set, s)) return reason;
    if (!Realized(s, index)) return "unrealized";
    if (s.ids[index] != index) return "id_mismatch";
    if (s.parts[index] != c::parts_loaded_state) return "parts";
    return nullptr;
}
// The component chain of *(S... *(PS+0x658)+0x30)+0x10 that 0xFF3220 reads
// before its lookup call, read non-null (0xFF3220 itself only null-checks +0x30).
bool ComponentChain(const Addresses& a, std::uintptr_t ps) noexcept {
    std::uintptr_t comp = 0, inner = 0, object = 0, vt = 0, lookup = 0;
    return ReadValue(ps + c::ps_comp, comp) && comp && ReadValue(comp + c::comp_inner, inner) && inner &&
           ReadValue(inner + c::comp_object, object) && object && ReadValue(object, vt) && InRange(vt, a.image_begin, a.image_end) &&
           ReadValue(vt + 0x18, lookup) && InRange(lookup, a.code_begin, a.code_end);
}
// Arrays of the Q allocator with n == 2 (S+0x40).
const char* ArrayCount(const State& s) noexcept {
    std::uint32_t n = 0;
    if (!ReadValue(s.S + c::s_count, n) || n != 2) return "count";
    return nullptr;
}
} // namespace

const char* ResolveBlock(const Addresses& a, const State& s, std::uint32_t k, Block& out) noexcept {
    const Preserve preserve;
    out = Block{};
    out.refusal = ResolveCore(a, s, k, out);
    out.stance_refusal = out.refusal;
    if (!out.refusal) {
        out.stance_refusal = ResolveStanceParts(out);
        out.refusal = ResolveTransform(a, out);
    }
    return out.refusal;
}

const char* ResolveCensus(const Addresses& a, const State& s, Census& out) noexcept {
    const Preserve preserve;
    out = Census{};
    std::uintptr_t equip = 0;
    if (ReadValue(s.P + c::p_equip, equip) && equip) ReadValue(equip, out.equip_vt);
    ResolveBlock(a, s, 0, out.k[0]);
    ResolveBlock(a, s, 1, out.k[1]);
    if (out.k[1].refusal) return out.refusal = out.k[1].refusal;
    if (out.k[1].id != 1) return out.refusal = "id_mismatch";
    if (out.k[0].refusal) return out.refusal = out.k[0].refusal;
    if (out.k[0].t_vt != out.k[1].t_vt || out.k[0].t68 != out.k[1].t68 || out.k[0].t70 != out.k[1].t70 ||
        out.k[0].t58 != out.k[1].t58)
        return out.refusal = "t_class";
    return out.refusal = nullptr;
}

std::size_t FormatCensus(const Addresses& a, const Census& census, std::uint32_t k, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity || k >= 2) return 0;
    const Preserve preserve;
    const Block& b = census.k[k];
    const Block& o = census.k[k ^ 1u];
    if (FAILED(StringCchPrintfA(out, capacity,
            "partner_census; k=%u; id=%u; base=%u; count=%u; blk=%s; blk_vt=0x%llx; ps_vt=0x%llx; cs_vt=0x%llx; "
            "t=%s; t_vt=0x%llx; t68=0x%llx; t70=0x%llx; a0_vt=0x%llx; a080=0x%llx; c0_vt=0x%llx; c090=0x%llx; b70_vt=0x%llx; "
            "stance=%s; psw=0x%02x; equip_vt=0x%llx; same_t=%u; same_a0=%u; same_c0=%u; verdict=%s\r\n",
            k, b.id, b.base_id, b.id_count, b.refusal ? b.refusal : "ok", static_cast<unsigned long long>(Rva(a, b.blk_vt)),
            static_cast<unsigned long long>(Rva(a, b.ps_vt)), static_cast<unsigned long long>(Rva(a, b.cs_vt)),
            b.t ? "set" : "null", static_cast<unsigned long long>(Rva(a, b.t_vt)), static_cast<unsigned long long>(Rva(a, b.t68)),
            static_cast<unsigned long long>(Rva(a, b.t70)), static_cast<unsigned long long>(Rva(a, b.a0_vt)),
            static_cast<unsigned long long>(Rva(a, b.a080)), static_cast<unsigned long long>(Rva(a, b.c0_vt)),
            static_cast<unsigned long long>(Rva(a, b.c090)), static_cast<unsigned long long>(Rva(a, b.b70_vt)),
            b.stance_refusal ? b.stance_refusal : "ok", unsigned(b.psw),
            static_cast<unsigned long long>(Rva(a, census.equip_vt)), (b.t_vt && b.t_vt == o.t_vt) ? 1u : 0u,
            (b.a0_vt && b.a0_vt == o.a0_vt) ? 1u : 0u, (b.c0_vt && b.c0_vt == o.c0_vt) ? 1u : 0u,
            census.refusal ? census.refusal : "ok"))) {
        out[0] = 0;
        return 0;
    }
    return std::strlen(out);
}

const char* WriteAnim(const Addresses& a, std::uint32_t index, bool terminating, std::uint16_t act, std::uint8_t stance,
                      std::uint8_t speed) noexcept {
    const Preserve preserve;
    State s{};
    if (const char* reason = Prepare(a, index, terminating, WriteKind::set, s)) return reason;
    if (const char* reason = ArrayCount(s)) return reason;
    std::uintptr_t p18 = 0, p30 = 0, p38 = 0;
    if (!ReadValue(s.Q + c::q_anim_18, p18) || !ReadValue(s.Q + c::q_anim_30, p30) || !ReadValue(s.Q + c::q_anim_38, p38) ||
        !p18 || !p30 || !p38) return "s_null";
    if (p18 % 2) return "alignment";
    if (!StoreU16(p18 + 2 * static_cast<std::uintptr_t>(index), act) || !StoreU8(p30 + index, stance) || !StoreU8(p38 + index, speed))
        return "fault";
    return nullptr;
}

const char* WriteFaceEquip(const Addresses& a, std::uint32_t index, bool terminating, std::uint8_t value) noexcept {
    const Preserve preserve;
    State s{};
    if (const char* reason = Prepare(a, index, terminating, WriteKind::set, s)) return reason;
    if (const char* reason = ArrayCount(s)) return reason;
    std::uintptr_t p68 = 0;
    if (!ReadValue(s.Q + c::q_look_face_equip, p68) || !p68) return "s_null";
    return StoreU8(p68 + index, value) ? nullptr : "fault";
}

const char* SetStance(const Addresses& a, std::uint32_t index, bool terminating, std::uint8_t stance, std::uint8_t& psw_after,
                      std::uint8_t& q30_after) noexcept {
    const Preserve preserve;
    psw_after = q30_after = 0;
    State s{};
    if (const char* reason = PrepareCall(a, index, terminating, s)) return reason;
    if (stance < 1 || stance > 3) return "stance";
    Block b{};
    if (const char* reason = ResolveCore(a, s, index, b)) return reason;
    if (const char* reason = ResolveStanceParts(b)) return reason;
    // SetStance calls *(PS+0x260)->vt[0] and stores at the result + 0x64.
    std::uintptr_t sub = 0, sub_vt = 0, sub_fn = 0, unused = 0;
    if (!ReadValue(b.ps + c::ps_sub260, sub) || !Callable(a, sub, 0, 0, 0, sub_vt, sub_fn, unused)) return "ps_sub";
    if (!a.set_stance || !InRange(a.set_stance, a.code_begin, a.code_end)) return "ps_vtable";
    if (!CallStance(a.set_stance, b.ps, b.id, stance)) return "fault";
    std::uintptr_t words = 0, p30 = 0;
    if (ReadValue(b.ps + c::ps_stance, words)) ReadValue(words + 8 * static_cast<std::uintptr_t>(b.id - b.base_id), psw_after);
    if (ReadValue(s.Q + c::q_anim_30, p30)) ReadValue(p30 + index, q30_after);
    return nullptr;
}

const char* RequestStance(const Addresses& a, std::uint32_t index, bool terminating, std::uint8_t stance) noexcept {
    const Preserve preserve;
    State s{};
    if (const char* reason = PrepareCall(a, index, terminating, s)) return reason;
    if (stance < 1 || stance > 3) return "stance";
    Block b0{}, b1{};
    if (const char* reason = ResolveCore(a, s, index, b1)) return reason;
    if (const char* reason = ResolveStanceParts(b1)) return reason;
    if (const char* reason = ResolveCore(a, s, s.local, b0)) return reason;
    if (!a.ps_component || !InRange(a.ps_component, a.code_begin, a.code_end)) return "ps_vtable";
    std::uintptr_t ps_slot = 0;
    if (!ReadValue(a.ps_vtable + 0xC8, ps_slot) || ps_slot != a.ps_component) return "ps_vtable";
    if (!ComponentChain(a, b1.ps) || !ComponentChain(a, b0.ps)) return "x_null";
    std::uintptr_t x0 = 0, x1 = 0;
    if (!CallComponent(a.ps_component, b0.ps, x0) || !CallComponent(a.ps_component, b1.ps, x1)) return "fault";
    if (!x0 || !x1) return "x_null";
    std::uintptr_t x0_vt = 0, x1_vt = 0, request = 0, unused = 0;
    if (!Callable(a, x1, c::x_vt_request, 0, 0, x1_vt, request, unused) || !ReadValue(x0, x0_vt) || x0_vt != x1_vt) return "x_class";
    return CallStance(request, x1, index, stance) ? nullptr : "fault";
}

const char* ApplyTransform(const Addresses& a, std::uint32_t index, bool terminating, const float (&pos)[4], const float (&quat)[4],
                           PartnerReset reset) noexcept {
    const Preserve preserve;
    State s{};
    if (const char* reason = PrepareCall(a, index, terminating, s)) return reason;
    for (float v : pos) if (!std::isfinite(v)) return "non_finite";
    for (float v : quat) if (!std::isfinite(v)) return "non_finite";
    Census census{};
    if (const char* reason = ResolveCensus(a, s, census)) return reason;
    const Block& b = census.k[index];
    const Block& local = census.k[s.local];
    const bool want_c0 = reset == PartnerReset::c || reset == PartnerReset::ac;
    const bool want_a0 = reset == PartnerReset::a || reset == PartnerReset::ac;
    if (want_c0 && (!b.c090 || b.c0_vt != local.c0_vt || b.c090 != local.c090)) return "c0_class";
    if (want_a0 && (!b.a080 || b.a0_vt != local.a0_vt || b.a080 != local.a080)) return "a0_class";
    alignas(16) float p[4] = {pos[0], pos[1], pos[2], pos[3]};
    alignas(16) float q[4] = {quat[0], quat[1], quat[2], quat[3]};
    return CallTransform(b.t, b.t70, b.t68, p, q, b.c0, want_c0 ? b.c090 : 0, b.a0, want_a0 ? b.a080 : 0, b.id) ? nullptr : "fault";
}

const char* ApplyPosition(const Addresses& a, std::uint32_t index, bool terminating, const float (&pos)[4]) noexcept {
    const Preserve preserve;
    State s{};
    if (const char* reason = PrepareCall(a, index, terminating, s)) return reason;
    for (float v : pos) if (!std::isfinite(v)) return "non_finite";
    Census census{};
    if (const char* reason = ResolveCensus(a, s, census)) return reason;
    const Block& b = census.k[index];
    if (!b.t58) return "t_class";
    alignas(16) float p[4] = {pos[0], pos[1], pos[2], pos[3]};
    return CallPosition(b.t, b.t58, p) ? nullptr : "fault";
}

bool ReadBody(const Addresses& a, const State& s, std::uint32_t index, float (&pos)[4], float (&quat)[4]) noexcept {
    const Preserve preserve;
    Block b{};
    if (ResolveBlock(a, s, index, b)) return false;
    return ReadBlock(b.t + c::t_pos, pos, sizeof(pos)) && ReadBlock(b.t + c::t_quat, quat, sizeof(quat));
}

bool ReadVelocity(const Addresses& a, const State& s, std::uint32_t index, float (&vel)[4]) noexcept {
    const Preserve preserve;
    Block b{};
    if (ResolveCore(a, s, index, b)) return false;
    std::uintptr_t array = 0;
    if (!ReadValue(b.ps + c::ps_vel268, array) || !array) return false;
    const std::uintptr_t rel = b.id - b.base_id;
    return ReadBlock(array + rel * c::ps_vel268_stride, vel, sizeof(vel));
}

std::size_t FormatPsProbe(const Addresses& a, const State& s, std::uint32_t k, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const Preserve preserve;
    out[0] = 0;
    Block b{};
    if (const char* reason = ResolveCore(a, s, k, b)) {
        if (FAILED(StringCchPrintfA(out, capacity, "partner_ps; k=%u; refused=%s\r\n", k, reason))) { out[0] = 0; return 0; }
        return std::strlen(out);
    }
    ResolveStanceParts(b);  // psw stays 0 when the stance words are missing
    const std::uintptr_t rel = b.id - b.base_id;
    std::uint32_t mask = 0;
    float r738[10]{};
    std::uint32_t r280[8]{};
    std::uint8_t b2d0 = 0, b2d8 = 0;
    std::uintptr_t p738 = 0, p280 = 0, p2d0 = 0, p2d8 = 0;
    const bool m_ok = ReadValue(b.ps + c::ps_mask740, mask);
    const bool r738_ok = ReadValue(b.ps + c::ps_rec738, p738) && ReadBlock(p738 + rel * c::ps_rec738_stride, r738, sizeof(r738));
    const bool r280_ok = ReadValue(b.ps + c::ps_rec280, p280) && ReadBlock(p280 + rel * c::ps_rec280_stride, r280, sizeof(r280));
    const bool d0_ok = ReadValue(b.ps + c::ps_byte2d0, p2d0) && ReadValue(p2d0 + rel, b2d0);
    const bool d8_ok = ReadValue(b.ps + c::ps_byte2d8, p2d8) && ReadValue(p2d8 + rel, b2d8);
    int f[10]{};
    for (unsigned i = 0; i < 10; ++i) f[i] = r738_ok ? ToMm(r738[i]) : INT32_MIN;
    if (!r280_ok) std::memset(r280, 0xFF, sizeof(r280));
    if (FAILED(StringCchPrintfA(out, capacity,
            "partner_ps; k=%u; id=%u; mask740=0x%x; r738_milli=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d; "
            "r280=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x; b2d0=%d; b2d8=%d; psw=0x%02x\r\n",
            k, b.id, m_ok ? mask : 0u, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], r280[0], r280[1], r280[2], r280[3],
            r280[4], r280[5], r280[6], r280[7], d0_ok ? int(b2d0) : -1, d8_ok ? int(b2d8) : -1, unsigned(b.psw)))) {
        out[0] = 0;
        return 0;
    }
    return std::strlen(out);
}

std::int32_t ToMm(float metres) noexcept {
    const double value = static_cast<double>(metres) * 1000.0;
    if (!std::isfinite(value)) return INT32_MIN;
    if (value >= 2147483647.0) return INT32_MAX;
    if (value <= -2147483647.0) return -INT32_MAX;
    return static_cast<std::int32_t>(std::llround(value));
}

std::int32_t ToMrad(float radians) noexcept { return ToMm(radians); }

std::size_t FormatReady(const State& s, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const Preserve preserve;
    if (FAILED(StringCchPrintfA(out, capacity,
            "partner_cfg=ready; flags=0x%08x; count=%u; realized_count=%u; local=%u; R=1; online_bit=%u; sync=%s; code_gate=ok\r\n",
            s.flags, s.count, s.realized_count, s.local, (s.flags & c::flag_online) ? 1u : 0u, s.sync ? "set" : "null"))) {
        out[0] = 0;
        return 0;
    }
    return std::strlen(out);
}

std::size_t FormatProbe(const State& s, std::uint64_t t_ms, std::uint32_t stage, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const Preserve preserve;
    char pos0[48]{}, pos1[48]{}, rec1[48]{}, req[48]{};
    LogMm(pos0, sizeof(pos0), s.slot[0]);
    LogMm(pos1, sizeof(pos1), s.slot[1]);
    LogMm(rec1, sizeof(rec1), s.rec1);
    if (s.req_ok)
        StringCchPrintfA(req, sizeof(req), "%u,%u,%u,%u,%u,%u", unsigned(s.req_look.type), unsigned(s.req_look.parts),
                         unsigned(s.req_look.camo), unsigned(s.req_look.hand), unsigned(s.req_look.face),
                         unsigned(s.req_look.face_equip));
    else
        StringCchCopyA(req, sizeof(req), "none");
    if (FAILED(StringCchPrintfA(out, capacity,
            "partner_probe=state; t_ms=%llu; stage=%u; ids=%04x,%04x; pool=%02x,%02x; parts=%u,%u; rbits=0x%x; s0=0x%02x; "
            "s8=0x%02x; q170=0x%x; q180=0x%x; q194=0x%x; q198=0x%x; glob=%u; pb_guard=%u; pb_flag=%u,%u; pb_state=%u,%u; "
            "pos0_mm=%s; yaw0_mrad=%d; pos1_mm=%s; yaw1_mrad=%d; rec1_mm=%s; rec1_status=0x%08x; req1=%s\r\n",
            static_cast<unsigned long long>(t_ms), stage, unsigned(s.ids[0]), unsigned(s.ids[1]), unsigned(s.pool[0]),
            unsigned(s.pool[1]), unsigned(s.parts[0]), unsigned(s.parts[1]), s.rbits, unsigned(s.s0), unsigned(s.s8), s.q170,
            s.q180, s.q194, s.q198, s.glob, s.pb_guard, s.pb_flag[0], s.pb_flag[1], s.pb_state[0], s.pb_state[1], pos0,
            ToMrad(s.slot[0][3]), pos1, ToMrad(s.slot[1][3]), rec1, s.rec1_status, req))) {
        out[0] = 0;
        return 0;
    }
    return std::strlen(out);
}

// --- Round 3: input lever targets, the local pad, direct motion -------------
namespace {
// The base action plugin of block `b` (after ResolveCore) and the record of
// `id`: obj = *(*(*(PS+0x658)+0x30)+0x10) (PS.vt+0xC8 0xFF3220 reads this
// chain), plugin = obj - 0x10, rec = *(plugin+0x1A0) + (id - base)*0x390.
const char* ResolvePlugin(const Addresses& a, const Block& b, std::uint32_t id, std::uintptr_t& obj, std::uintptr_t& obj_vt,
                          std::uintptr_t& plugin, std::uintptr_t (&vts)[4], std::uintptr_t& rec, std::uint32_t& idx,
                          std::uint32_t& count, std::uint32_t& rec_2ac) noexcept {
    std::uintptr_t comp = 0, inner = 0, node = 0, recs = 0, owner = 0;
    obj = obj_vt = plugin = rec = 0;
    idx = count = rec_2ac = 0;
    if (!ReadValue(b.ps + c::ps_comp, comp) || !comp || !ReadValue(comp + c::comp_inner, inner) || !inner ||
        !ReadValue(inner + c::comp_object, obj) || !obj || !ReadValue(obj, obj_vt) || obj_vt != a.plugin_vt[1]) return "obj_vt";
    plugin = obj - c::plugin_obj_off;
    static constexpr std::size_t offsets[4] = {0x0, 0x10, 0x30, 0x48};
    for (unsigned i = 0; i < 4; ++i)
        if (!ReadValue(plugin + offsets[i], vts[i]) || vts[i] != a.plugin_vt[i]) return "plugin_vt";
    if (!ReadValue(plugin + c::plugin_blk, owner) || owner != b.blk) return "plugin_blk";
    std::uint32_t base = 0;
    if (!ReadValue(plugin + c::plugin_node, node) || !node || !ReadValue(node + c::node_base, base) ||
        !ReadValue(node + c::node_count, count) || id < base || id - base >= count) return "rec_idx";
    idx = id - base;
    if (!ReadValue(plugin + c::plugin_recs, recs) || !recs) return "rec";
    rec = recs + static_cast<std::uintptr_t>(idx) * c::plugin_rec_stride;
    if (!ReadValue(rec + c::rec_local, rec_2ac)) return "rec";
    return nullptr;
}
// Exactly the image vtable: the hook's per-window copies are only live inside
// a window, and a Lua-thread read that sees one raced a window.
bool PpVtableOk(const Addresses& a, std::uintptr_t vt) noexcept { return vt && vt == a.pp_vtable; }
// The PP / pp_owner / src checks shared by ResolveInputTargets and ReadPad.
const char* ResolvePad(const Addresses& a, const State& s, std::uintptr_t& pp, std::uintptr_t& pp_vt, std::uintptr_t& src,
                       bool& owner_ok) noexcept {
    std::uintptr_t owner = 0;
    pp = pp_vt = src = 0;
    owner_ok = false;
    if (!ReadValue(s.P + c::p_pad, pp) || !pp || !ReadValue(pp, pp_vt) || !PpVtableOk(a, pp_vt)) return "pp_vt";
    owner_ok = ReadValue(pp + c::pp_owner, owner) && owner == s.P;
    if (!owner_ok) return "pp_owner";
    if (!ReadValue(pp + c::pp_src, src) || !src) return "src";
    return nullptr;
}
} // namespace

const char* ResolveInputTargets(const Addresses& a, const State& s, InputTargets& out) noexcept {
    const Preserve preserve;
    out = InputTargets{};
    out.P = s.P;
    out.flags = s.flags;
    out.alt_dispatch = (s.flags & c::flag_alt_dispatch) != 0;
    if (const char* reason = CheckWrite(Refusal::none, s, 1, false, WriteKind::set)) return out.refusal = reason;
    ReadValue(s.P, out.p_vt);
    // Block 0 (logged only).
    Block b0{};
    if (Realized(s, 0) && !ResolveCore(a, s, 0, b0)) {
        out.blk0 = b0.blk;
        ReadValue(b0.blk + c::blk_root, out.root0);
        std::uintptr_t obj = 0, obj_vt = 0, plugin = 0, vts[4]{};
        std::uint32_t idx = 0, count = 0;
        out.rec0_ok = !ResolvePlugin(a, b0, b0.id, obj, obj_vt, plugin, vts, out.rec0, idx, count, out.rec0_2ac);
    }
    if (!Realized(s, 1)) return out.refusal = "unrealized";
    if (s.ids[1] != 1) return out.refusal = "id";
    if (s.parts[1] != c::parts_loaded_state) return out.refusal = "parts";
    out.pool1 = s.pool[1];
    if (out.alt_dispatch) return out.refusal = "alt_dispatch";
    if (const char* reason = ResolvePad(a, s, out.pp, out.pp_vt, out.src, out.pp_owner_ok)) return out.refusal = reason;
    ReadValue(out.src, out.src_vt);
    if (!ReadValue(s.P + c::p_cam, out.cam) || !out.cam || !ReadValue(out.cam, out.cam_vt) || out.cam_vt != a.cam_vtable)
        return out.refusal = "cam_vt";
    ReadValue(out.cam + c::cam_mode, out.cam_mode);
    Block b1{};
    if (const char* reason = ResolveCore(a, s, 1, b1)) return out.refusal = reason;
    out.blk1 = b1.blk;
    out.ps1 = b1.ps;
    out.ps1_vt = b1.ps_vt;
    if (!ReadValue(b1.blk + c::blk_root, out.root1) || !out.root1) return out.refusal = "root";
    if (const char* reason = ResolvePlugin(a, b1, b1.id, out.obj, out.obj_vt, out.plugin1, out.plugin_vt, out.rec1, out.rec_idx,
                                           out.rec_count, out.rec1_2ac)) return out.refusal = reason;
    return out.refusal = nullptr;
}

std::size_t FormatInputCensus(const Addresses& a, const InputTargets& t, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const Preserve preserve;
    const auto r = [&](std::uintptr_t v) { return static_cast<unsigned long long>(Rva(a, v)); };
    if (FAILED(StringCchPrintfA(out, capacity,
            "partner_input_census; P=0x%llx; flags=0x%08x; alt=%u; pp=0x%llx; pp_vt=0x%llx; pp_owner=%u; src=0x%llx; src_vt=0x%llx; "
            "cam=0x%llx; cam_vt=0x%llx; cam_mode=%u; root0=0x%llx; root1=0x%llx; ps1_vt=0x%llx; obj_vt=0x%llx; "
            "plugin_vt=0x%llx,0x%llx,0x%llx,0x%llx; rec_idx=%u/%u; rec0_2ac=0x%x; rec1_2ac=0x%x; verdict=%s\r\n",
            static_cast<unsigned long long>(t.P), t.flags, t.alt_dispatch ? 1u : 0u, static_cast<unsigned long long>(t.pp), r(t.pp_vt),
            t.pp_owner_ok ? 1u : 0u, static_cast<unsigned long long>(t.src), r(t.src_vt), static_cast<unsigned long long>(t.cam),
            r(t.cam_vt), t.cam_mode, static_cast<unsigned long long>(t.root0), static_cast<unsigned long long>(t.root1), r(t.ps1_vt),
            r(t.obj_vt), r(t.plugin_vt[0]), r(t.plugin_vt[1]), r(t.plugin_vt[2]), r(t.plugin_vt[3]), t.rec_idx, t.rec_count,
            t.rec0_2ac, t.rec1_2ac, t.refusal ? t.refusal : "ok"))) {
        out[0] = 0;
        return 0;
    }
    return std::strlen(out);
}

bool ReadPad(const Addresses& a, const State& s, partner_input::PadRaw& out) noexcept {
    const Preserve preserve;
    out = partner_input::PadRaw{};
    if (CheckWrite(Refusal::none, s, 1, false, WriteKind::restore)) return false;
    std::uintptr_t pp = 0, pp_vt = 0, src = 0;
    bool owner = false;
    if (ResolvePad(a, s, pp, pp_vt, src, owner)) return false;
    unsigned char srcb[c::src_size]{}, derived[c::pp_derived_size + c::pp_masks_size]{}, kb[c::pp_kb_size + 1]{};
    static_assert(c::pp_derived + c::pp_derived_size == c::pp_masks, "PP+0x10..+0x33 is one block");
    static_assert(c::pp_kb + c::pp_kb_size == c::pp_kb_flags, "PP+0x460..+0x488 is one block");
    if (!ReadBlock(src + c::src_words, srcb, sizeof(srcb)) || !ReadBlock(pp + c::pp_derived, derived, sizeof(derived)) ||
        !ReadBlock(pp + c::pp_kb, kb, sizeof(kb))) return false;
    std::memcpy(out.src_w, srcb, sizeof(out.src_w));
    std::memcpy(out.src_f, srcb + 0x10, sizeof(out.src_f));
    std::memcpy(out.pp_derived, derived, sizeof(out.pp_derived));
    std::memcpy(out.pp_mask, derived + c::pp_derived_size, sizeof(out.pp_mask));
    std::memcpy(out.kb_w, kb, sizeof(out.kb_w));
    std::memcpy(out.kb_f, kb + 0x10, sizeof(out.kb_f));
    out.kb_flags = kb[c::pp_kb_size];
    return true;
}

namespace {
// PS of a block validated as the look owner of `id` against the block root.
const char* LookSlot(const Addresses& a, std::uintptr_t blk, std::uintptr_t root, std::uint32_t id, std::uintptr_t& ps,
                     std::uintptr_t& ps_vt, std::uintptr_t& look, std::uintptr_t& rec, std::uint32_t& base) noexcept {
    std::uintptr_t comp = 0;
    std::uint32_t root_base = 0, root_count = 0;
    if (!ReadValue(blk + c::blk_ps, ps) || !ps || !ReadValue(ps, ps_vt) || (ps_vt != a.ps_vtable && ps_vt != a.ps_vtable_alt))
        return "ps_vtable";
    if (!ReadValue(ps + c::ps_phase_comp, comp) || comp != ps_vt + c::ps_phase_comp_vt_delta) return "ps_comp";
    if (!ReadValue(ps + c::ps_base_id, base) || !ReadValue(root + c::node_base, root_base) ||
        !ReadValue(root + c::node_count, root_count) || base != root_base) return "ps_base";
    if (id < root_base || id - root_base >= root_count) return "ps_range";
    if (!ReadValue(ps + c::ps_look, look) || !look || !ReadValue(ps + c::ps_rec, rec) || !rec) return "ps_arrays";
    return nullptr;
}
} // namespace

const char* LookSlotOf(const Addresses& a, const State& s, const InputTargets& t, NativeTargets& out) noexcept {
    std::uintptr_t ps0_vt = 0, l0 = 0, r0 = 0, ps1_vt = 0;
    std::uint32_t b0 = 0;
    if (!t.blk0 || !t.root0 || LookSlot(a, t.blk0, t.root0, s.ids[0], out.ps0, ps0_vt, l0, r0, b0)) return "ps0";
    if (const char* why = LookSlot(a, t.blk1, t.root1, 1, out.ps1, ps1_vt, out.ps1_look, out.ps1_rec, out.ps1_base)) return why;
    out.ps1_vt = ps1_vt;
    if (out.ps1 == out.ps0) return "ps_shared";
    out.ps1_idx = 1 - out.ps1_base;
    return nullptr;
}

const char* ResolveNative(const Addresses& a, const State& s, const InputTargets& t, NativeTargets& out) noexcept {
    const Preserve preserve;
    out = NativeTargets{};
    if (t.refusal) return out.refusal = out.look_refusal = t.refusal;
    out.go = s.P - c::go_to_p;
    if (!ReadValue(out.go, out.go_vt) || out.go_vt != a.go_vtable) return out.refusal = out.look_refusal = "go_vtable";
    if (!Realized(s, 0)) return out.refusal = out.look_refusal = "unrealized0";
    out.pool0 = s.pool[0];
    out.pool1 = s.pool[1];
    if (out.pool0 == out.pool1) return out.refusal = out.look_refusal = "pool";
    // The identity holds from here on; the look slot has its own verdict.
    out.refusal = nullptr;
    out.look_refusal = LookSlotOf(a, s, t, out);
    return nullptr;
}

bool ReadLookSource(const Addresses& a, const State& s, LookSource& out) noexcept {
    const Preserve preserve;
    out = LookSource{};
    if (CheckWrite(Refusal::none, s, 1, false, WriteKind::restore)) return false;
    const std::uint32_t local = s.local;
    if (local >= 2 || !Realized(s, local)) return false;
    Block b{};
    if (!ResolveCore(a, s, local, b)) {
        std::uintptr_t root = 0, ps = 0, ps_vt = 0, look = 0, rec = 0;
        std::uint32_t base = 0;
        if (ReadValue(b.blk + c::blk_root, root) && root && !LookSlot(a, b.blk, root, s.ids[local], ps, ps_vt, look, rec, base)) {
            float pair[2]{};
            if (ReadBlock(look + static_cast<std::uintptr_t>(s.ids[local] - base) * c::ps_look_stride, pair, sizeof(pair)) &&
                std::isfinite(pair[0]) && std::isfinite(pair[1])) {
                out.pitch = pair[0];
                out.yaw = pair[1];
                out.look_ok = true;
            }
        }
    }
    std::uintptr_t cam = 0, cam_vt = 0, pp = 0, pp_vt = 0;
    float cams[2]{};
    if (ReadValue(s.P + c::p_cam, cam) && cam && ReadValue(cam, cam_vt) && cam_vt == a.cam_vtable &&
        ReadBlock(cam + c::cam270, cams, sizeof(cams)) && std::isfinite(cams[0]) && std::isfinite(cams[1])) {
        out.cam270 = cams[0];
        out.cam274 = cams[1];
        out.cam_ok = true;
    }
    float heading = 0.0f;
    if (ReadValue(s.P + c::p_pad, pp) && pp && ReadValue(pp, pp_vt) && pp_vt == a.pp_vtable && ReadValue(pp + 0x14, heading) &&
        std::isfinite(heading)) {
        out.heading = heading;
        out.heading_ok = true;
    }
    return out.look_ok || out.cam_ok || out.heading_ok;
}

bool ReadGun1(const Addresses& a, const State& s, Gun1& out) noexcept { return ReadGun(a, s, 1, out); }

bool ReadGun(const Addresses& a, const State& s, std::uint32_t k, Gun1& out) noexcept {
    const Preserve preserve;
    out = Gun1{};
    if (k > 1) { out.refusal = "instance"; return false; }
    if (const char* why = CheckWrite(Refusal::none, s, 1, false, WriteKind::restore)) { out.refusal = why; return false; }
    if (k == 0 && !Realized(s, 0)) { out.refusal = "unrealized0"; return false; }
    Block b{};
    if (const char* why = ResolveCore(a, s, k, b)) { out.refusal = why; return false; }
    std::uintptr_t node = 0, node_vt = 0, iface = 0, iface_vt = 0, prim_vt = 0, prim_blk = 0, prim_node = 0, rec = 0, bitmap = 0;
    std::uint32_t one = 0, id = 0, row = 0, bits = 0;
    if (!ReadValue(b.blk + c::blk_gun_node, node) || !node || !ReadValue(node, node_vt) || node_vt != a.gun_node_vtable ||
        !ReadValue(node + 0x20, one) || one != 1 || !ReadValue(node + c::gun_node_body, id) || id != b.id ||
        !ReadValue(node + c::gun_node_row, row) || row != 0x3D) { out.refusal = "gun_node"; return false; }
    if (!ReadValue(b.blk + c::blk_gun_iface, iface) || !iface || !ReadValue(iface, iface_vt) || iface_vt != a.gun_iface_vtable) {
        out.refusal = "gun_iface";
        return false;
    }
    const std::uintptr_t prim = iface - c::gun_iface_prim;
    if (!ReadValue(prim, prim_vt) || prim_vt != a.gun_prim_vtable || !ReadValue(prim + c::prim_blk, prim_blk) || prim_blk != b.blk ||
        !ReadValue(prim + c::prim_node, prim_node) || prim_node != node) { out.refusal = "gun_prim"; return false; }
    if (!ReadValue(prim + c::prim_gun_rec, rec) || !rec || !ReadValue(rec + c::gun_flags, out.gun3c0) ||
        !ReadValue(rec + c::gun_mode, out.mode)) { out.refusal = "gun_rec"; return false; }
    if (ReadValue(node + c::gun_node_bitmap, bitmap) && bitmap && ReadValue(bitmap, bits)) out.node_active = (bits & 1u) != 0;
    out.prim = prim;
    out.rec = rec;
    partner_native::GunFields& g = out.fields;
    out.fields_ok = ReadValue(rec + c::gun_flags27c, g.flags27c) && ReadValue(rec + c::gun_flags280, g.flags280) &&
                    ReadValue(rec + c::gun_equip, g.equip) && ReadValue(rec + c::gun_cap, g.cap) &&
                    ReadValue(rec + c::gun_cap_sub, g.cap_sub) && ReadValue(rec + c::gun_mag, g.mag) &&
                    ReadValue(rec + c::gun_mag_sub, g.mag_sub) && ReadValue(rec + c::gun_stock, g.stock) &&
                    ReadValue(rec + c::gun_stock_sub, g.stock_sub) && ReadValue(rec + 0x23A, g.w23a) &&
                    ReadValue(rec + c::gun_b3d5, out.b3d5) && ReadValue(rec + c::gun_slot, out.slot) &&
                    ReadValue(rec + c::gun_mode_cur, out.mode_cur) && ReadValue(rec + c::gun_slot_hand, out.hand_slot) &&
                    ReadValue(rec + c::gun_slot_src, out.slot_src) && ReadValue(rec + c::gun_m3e4, out.m3e4) &&
                    ReadValue(rec + c::gun_m3e6, out.m3e6) && ReadValue(rec + c::gun_m3e8, out.m3e8);
    g.gun3c0 = out.gun3c0;
    // The upper-body request byte of this id (Q+0x20 is a per-id u8 array; 5 = reload).
    std::uintptr_t upper = 0;
    out.upper_ok = b.id < 2 && ReadValue(s.Q + c::q_upper, upper) && upper && ReadValue(upper + b.id, out.upper_req);
    return true;
}

bool ReadSlots(const Addresses& a, const State& s, std::uint32_t k, Slots& out) noexcept {
    const Preserve preserve;
    out = Slots{};
    if (k > 1) { out.refusal = "instance"; return false; }
    if (const char* why = CheckWrite(Refusal::none, s, 1, false, WriteKind::restore)) { out.refusal = why; return false; }
    if (s.ids[k] != k) { out.refusal = "ids"; return false; }
    std::uintptr_t ctl = 0, ctl_vt = 0, player = 0, inv = 0, inv_vt = 0, handles = 0, rows = 0;
    if (!ReadValue(s.P + c::p_equip, ctl) || !ctl) { out.refusal = "ctl"; return false; }
    if (!ReadValue(ctl, ctl_vt) || ctl_vt != a.equip_ctl_vtable || !ReadValue(ctl + c::ctl_player, player) || player != s.P) {
        out.refusal = "ctl_vtable";
        return false;
    }
    if (!ReadValue(ctl + c::ctl_inventory, inv) || !inv || !ReadValue(inv, inv_vt) ||
        (inv_vt != a.store_vtable && inv_vt != a.store_vtable_alt)) { out.refusal = "inventory"; return false; }
    std::uint32_t stride = 0, total = 0;
    if (!ReadValue(inv + c::store_stride, stride) || stride < 3 || stride > 16 || !ReadValue(inv + c::store_total, total) ||
        static_cast<std::uint64_t>(k) * stride + 3 > total || !ReadValue(inv + c::inv_handles, handles) || !handles ||
        !ReadBlock(handles + (static_cast<std::uintptr_t>(k) * stride) * 4, out.handles, sizeof(out.handles))) {
        out.refusal = "stride";
        return false;
    }
    unsigned char row[c::equip_row_stride]{};
    if (!ReadValue(s.Q + c::q_equip_rows, rows) || !rows ||
        !ReadBlock(rows + static_cast<std::uintptr_t>(k) * c::equip_row_stride, row, sizeof(row))) { out.refusal = "rows"; return false; }
    out.ctl = ctl;
    out.inventory = inv;
    out.row = rows + static_cast<std::uintptr_t>(k) * c::equip_row_stride;
    out.stride = stride;
    std::memcpy(&out.row_current, row + c::equip_row_current, 2);
    std::memcpy(out.row_slots, row + c::equip_row_slots, sizeof(out.row_slots));
    out.nibble = static_cast<std::uint8_t>(row[c::equip_row_nibble] & 0xF);
    return true;
}

bool EquipType(const Addresses& a, std::uint32_t equip, std::uint8_t& type) noexcept {
    const Preserve preserve;
    type = 0;
    std::uintptr_t vt = 0;
    std::uint16_t packed46 = 0, packed = 0;
    if (!a.equip_types || equip == 0 || equip >= c::stock_ids || !ReadValue(a.equip_types, vt) || vt != a.equip_types_vtable ||
        !ReadValue(a.equip_types + c::equip_types_packed + 46 * 2, packed46) || packed46 != c::stock46_packed ||
        !ReadValue(a.equip_types + c::equip_types_packed + equip * 2, packed) || !packed)
        return false;
    type = static_cast<std::uint8_t>(packed & 0x3Fu);
    return type != 0;
}

namespace {
// A2A3E0's alias path (contract-pinned, 0xA2A3E6..0xA2A432): the alias's
// receiver from the alias table, then the equipment resource's part record
// (1-based, 6 bytes, the kind at +4) and kind record (3 bytes, type & 0x1F).
// The resource must hold GunBasic at +8 (the writer contract's identity).
bool AliasType(const Addresses& a, std::uint16_t alias, std::uint8_t& receiver, std::uint8_t& type) noexcept {
    receiver = type = 0;
    if (!a.alias_table || !a.equip_resource || alias < c::alias_first || alias - c::alias_first >= c::alias_count) return false;
    std::uintptr_t basic = 0, parts = 0, kinds = 0;
    std::uint8_t kind = 0, value = 0;
    if (!ReadValue(a.alias_table + static_cast<std::uintptr_t>(alias - c::alias_first) * c::alias_stride + c::alias_receiver, receiver) ||
        !receiver || !ReadValue(a.equip_resource + c::res_basic, basic) || basic != a.gun_basic ||
        !ReadValue(a.equip_resource + c::res_parts, parts) || !parts || !ReadValue(a.equip_resource + c::res_kinds, kinds) || !kinds ||
        !ReadValue(parts + static_cast<std::uintptr_t>(receiver) * c::part_stride - c::part_kind_back, kind) ||
        !ReadValue(kinds + static_cast<std::uintptr_t>(kind) * c::kind_stride, value))
        return false;
    type = static_cast<std::uint8_t>(value & c::alias_type_mask);
    return true;
}
} // namespace

bool ReadLoadout(const Addresses& a, Loadout& out) noexcept {
    const Preserve preserve;
    out = Loadout{};
    for (const char*& scan : out.scan) scan = "none";
    std::uintptr_t vars = 0;
    if (!ReadValue(a.vars_slot, vars) || !vars || !ReadBlock(vars + c::vars_weapons, out.weapons, sizeof(out.weapons)) ||
        !ReadBlock(vars + c::vars_customized, out.customized, sizeof(out.customized)) ||
        !ReadBlock(vars + c::vars_chimera, out.chimera, sizeof(out.chimera))) return false;
    out.ok = true;
    // The base-id tables, admitted only on the stock46 sanity checks.
    std::uintptr_t types_vt = 0;
    std::uint16_t packed46 = 0;
    unsigned char row393[c::gun_basic_row]{};
    out.tables_ok = a.equip_types && a.gun_basic && ReadValue(a.equip_types, types_vt) && types_vt == a.equip_types_vtable &&
                    ReadValue(a.equip_types + c::equip_types_packed + 46 * 2, packed46) && packed46 == c::stock46_packed &&
                    ReadBlock(a.gun_basic + (393 - 1) * c::gun_basic_row, row393, sizeof(row393)) &&
                    std::memcmp(row393, c::stock46_recipe, sizeof(row393)) == 0;
    // Module storage, not the caller's stack (the driver tick is serialized).
    static std::uint16_t packed[c::stock_ids];
    static unsigned char receivers[c::gun_basic_rows];
    static unsigned char basic[c::gun_basic_rows * c::gun_basic_row];
    std::memset(packed, 0, sizeof(packed));
    std::memset(receivers, 0, sizeof(receivers));
    if (out.tables_ok) {
        out.tables_ok = ReadBlock(a.equip_types + c::equip_types_packed, packed, sizeof(packed)) &&
                        ReadBlock(a.gun_basic, basic, sizeof(basic));
        for (std::size_t r = 0; r < c::gun_basic_rows; ++r) receivers[r] = basic[r * c::gun_basic_row];
    }
    namespace ew = partner_equip_wire;
    for (unsigned i = 0; i < 3; ++i) {
        const std::uint16_t alias = out.weapons[i];
        if (!alias) continue;
        if (alias < c::types_index_mid) {
            // A stock id: itself, only when its packed type is a gun type.
            if (!out.tables_ok) { out.scan[i] = "tables"; continue; }
            const std::uint8_t type = static_cast<std::uint8_t>(packed[alias] & 0x3Fu);
            if (!ew::GunType(type)) { out.scan[i] = "non_gun"; continue; }
            out.base[i] = alias;
            out.type[i] = type;
            out.scan[i] = "stock";
            continue;
        }
        if (alias < c::alias_first || alias - c::alias_first >= c::alias_count) { out.scan[i] = "range"; continue; }
        // A customized alias: its own type, then the first stock gun of that type with the slot's receiver.
        std::uint8_t type = 0;
        if (!AliasType(a, alias, out.alias_receiver[i], type) || !ew::GunType(type)) { out.scan[i] = "alias_type"; continue; }
        out.type[i] = type;
        if (!out.tables_ok) { out.scan[i] = "tables"; continue; }
        out.base[i] = ew::BaseFromTables(packed, c::stock_ids, receivers, c::gun_basic_rows, type, out.chimera[i * 12]);
        out.scan[i] = out.base[i] ? "gun_types" : "no_match";
    }
    return true;
}

bool ReadRates(const Addresses& a, const State& s, std::uint32_t index, RateRead& out) noexcept {
    const Preserve preserve;
    out = RateRead{};
    static_assert(c::rate_count == 6 && sizeof(out.f) == 6 * sizeof(float), "rec+0x3C..+0x53");
    if (!a.rate_sp_vtable || !a.rate_rec_vtable) { out.refusal = "unpinned"; return false; }
    // The state guards (index 1 = the write guards' partner index; the read itself may be instance 0, the sender).
    if (const char* why = CheckWrite(Refusal::none, s, 1, false, WriteKind::restore)) { out.refusal = why; return false; }
    if (index >= 2) { out.refusal = "index"; return false; }
    Block b{};
    if (const char* why = ResolveCore(a, s, index, b)) { out.refusal = why; return false; }
    std::uintptr_t sp = 0, sp_vt = 0, recs = 0, rec_vt = 0;
    std::uint32_t base = 0, count = 0;
    if (!ReadValue(b.ps + c::ps_rates, sp) || !sp) { out.refusal = "sp"; return false; }
    if (!ReadValue(sp, sp_vt) || sp_vt != a.rate_sp_vtable) { out.refusal = "sp_vtable"; return false; }
    if (!ReadValue(sp + c::sp_base_id, base) || !ReadValue(sp + c::sp_count, count) || b.id < base || b.id - base >= count ||
        count > 64) { out.refusal = "bounds"; return false; }
    if (!ReadValue(sp + c::sp_records, recs) || !recs) { out.refusal = "rec"; return false; }
    const std::uintptr_t rec = recs + static_cast<std::uintptr_t>(b.id - base) * c::rate_rec_stride;
    if (!ReadValue(rec, rec_vt) || rec_vt != a.rate_rec_vtable) { out.refusal = "rec_vtable"; return false; }
    float f[6]{};
    if (!ReadBlock(rec + c::rate_first, f, sizeof(f))) { out.refusal = "rec"; return false; }
    for (float v : f)
        if (!std::isfinite(v)) { out.refusal = "nonfinite"; return false; }
    out.sp = sp;
    out.rec = rec;
    out.id = b.id;
    std::memcpy(out.f, f, sizeof(f));
    return true;
}

const char* ResolveDm(const Addresses& a, const State& s, DmTargets& out) noexcept {
    const Preserve preserve;

    out = DmTargets{};
    if (const char* reason = CheckWrite(Refusal::none, s, 1, false, WriteKind::restore)) return out.refusal = reason;
    out.g_ok = ReadValue(s.P + c::p_g, out.g_value) && out.g_value && out.g_value == a.script_object;
    out.g = out.g_ok ? out.g_value : 0;
    if (!out.g_ok) return out.refusal = "g";
    Block b{};
    if (!Realized(s, 1)) return out.refusal = "unrealized";
    if (s.ids[1] != 1) return out.refusal = "id";
    if (const char* reason = ResolveCore(a, s, 1, b)) return out.refusal = reason;
    out.blk1 = b.blk;
    if (!ReadValue(b.blk + c::blk_iface + 8 * c::dm_iface_index, out.iface) || !out.iface || !ReadValue(out.iface, out.iface_vt) ||
        out.iface_vt != a.dm_iface_vtable) return out.refusal = "iface_vt";
    out.prim = out.iface - c::dm_iface_prim;
    std::uintptr_t owner = 0, node = 0, recs = 0;
    if (!ReadValue(out.prim, out.prim_vt) || out.prim_vt != a.dm_prim_vtable) return out.refusal = "prim_vt";
    out.prim_blk_ok = ReadValue(out.prim + c::dm_prim_blk, owner) && owner == b.blk;
    if (!out.prim_blk_ok) return out.refusal = "prim_blk";
    std::uint32_t base = 0;
    if (!ReadValue(out.prim + c::dm_prim_node, node) || !node || !ReadValue(node + c::node_base, base) ||
        !ReadValue(node + c::node_count, out.rec_count) || b.id < base || b.id - base >= out.rec_count) return out.refusal = "rec_idx";
    out.rec_idx = b.id - base;
    if (!ReadValue(out.prim + c::dm_prim_recs, recs) || !recs) return out.refusal = "rec";
    out.rec = recs + static_cast<std::uintptr_t>(out.rec_idx) * c::dm_rec_stride;
    return out.refusal = nullptr;
}

bool ReadDm(const Addresses& a, const State& s, const DmTargets& t, DmState& out) noexcept {
    const Preserve preserve;
    out = DmState{};
    out.ps2d8[0] = out.ps2d8[1] = -1;
    if (!t.g) return false;
    std::uintptr_t q18 = 0;
    out.ok = ReadValue(t.g + c::g_flags, out.g388) && ReadValue(t.g + c::g_req, out.req0) && ReadValue(t.g + c::g_name, out.name) &&
             ReadValue(s.Q + c::q_act, q18) && ReadBlock(q18, out.q18, sizeof(out.q18));
    for (std::uint32_t k = 0; k < 2; ++k) {
        Block b{};
        std::uintptr_t p2d8 = 0;
        std::uint8_t value = 0;
        if (!ResolveCore(a, s, k, b) && ReadValue(b.ps + c::ps_byte2d8, p2d8) && ReadValue(p2d8 + (b.id - b.base_id), value))
            out.ps2d8[k] = value;
    }
    std::uintptr_t cam = 0, cam_vt = 0;
    out.cam_ok = ReadValue(s.P + c::p_cam, cam) && cam && ReadValue(cam, cam_vt) && cam_vt == a.cam_vtable &&
                 ReadValue(cam + c::cam_connect, out.cam1f0) && ReadValue(cam + c::cam_connect_on, out.cam200);
    if (t.rec) {
        out.rec_ok = ReadValue(t.rec + 0x10, out.rec10) && ReadValue(t.rec + 0x11, out.rec11) && ReadValue(t.rec + 0x18, out.rec18) &&
                     ReadValue(t.rec + 0x1A0, out.rec1a0) && ReadValue(t.rec + 0x248, out.rec248);
    }
    return out.ok;
}

std::size_t FormatDmCensus(const Addresses& a, const DmTargets& t, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const Preserve preserve;
    if (FAILED(StringCchPrintfA(out, capacity,
            "partner_dm_census; g=0x%llx; g_ok=%u; iface_vt=0x%llx; prim_vt=0x%llx; prim_blk=%u; rec_idx=%u/%u; verdict=%s\r\n",
            static_cast<unsigned long long>(Rva(a, t.g_value)), t.g_ok ? 1u : 0u, static_cast<unsigned long long>(Rva(a, t.iface_vt)),
            static_cast<unsigned long long>(Rva(a, t.prim_vt)), t.prim_blk_ok ? 1u : 0u, t.rec_idx, t.rec_count,
            t.refusal ? t.refusal : "ok"))) {
        out[0] = 0;
        return 0;
    }
    return std::strlen(out);
}

std::size_t FormatDmProbe(const DmState& d, bool patched, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const Preserve preserve;
    if (FAILED(StringCchPrintfA(out, capacity,
            "partner_dm; patched=%u; g388=0x%02x; req0=%u; q18=0x%x,0x%x; ps2d8=%d,%d; rec10=%u; rec11=%u; rec18=0x%llx; "
            "rec1a0=%u; rec248=0x%02x; cam1f0=0x%llx; cam200=%u; cam_ok=%u\r\n",
            patched ? 1u : 0u, unsigned(d.g388), unsigned(d.req0), unsigned(d.q18[0]), unsigned(d.q18[1]), d.ps2d8[0], d.ps2d8[1],
            unsigned(d.rec10), unsigned(d.rec11), static_cast<unsigned long long>(d.rec18), unsigned(d.rec1a0), unsigned(d.rec248),
            static_cast<unsigned long long>(d.cam1f0), d.cam200, d.cam_ok ? 1u : 0u))) {
        out[0] = 0;
        return 0;
    }
    return std::strlen(out);
}

namespace {
bool AndByte(std::uintptr_t address, char mask) noexcept {
    __try { _InterlockedAnd8(reinterpret_cast<volatile char*>(address), mask); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
} // namespace

const char* ClearDmPending(const Addresses& a, bool terminating, bool& cleared) noexcept {
    const Preserve preserve;
    cleared = false;
    State s{};
    if (const char* reason = Prepare(a, 1, terminating, WriteKind::restore, s)) return reason;
    std::uintptr_t g = 0;
    std::uint8_t flags = 0, req0 = 0;
    if (!ReadValue(s.P + c::p_g, g) || !g || g != a.script_object) return "g";
    if (!ReadValue(g + c::g_flags, flags) || !ReadValue(g + c::g_req, req0)) return "fault";
    if (!(flags & 4) || req0 != 1) return nullptr;  // nothing of ours pending
    if (!AndByte(g + c::g_flags, static_cast<char>(~4))) return "fault";
    cleared = true;
    return nullptr;
}

bool ReadAim(const Addresses& a, const State& s, AimRead& out) noexcept {
    const Preserve preserve;
    out = AimRead{};
    std::uintptr_t q_vt = 0, arr = 0;
    if (s.Q && ReadValue(s.Q, q_vt) && q_vt == a.q_vtable && ReadValue(s.Q + c::q_aim, arr) && arr) {
        float v[2][4]{};
        out.q_ok = ReadBlock(arr, v, sizeof(v));
        for (unsigned k = 0; out.q_ok && k < 2; ++k)
            for (unsigned i = 0; i < 3; ++i) out.q10[k][i] = v[k][i];
    }
    for (std::uint32_t k = 0; k < 2; ++k) {
        Gun1 g{};
        if (ReadGun(a, s, k, g) && g.rec) {
            float p[4]{};
            out.gun_ok[k] = ReadBlock(g.rec + c::gun_aim_point, p, sizeof(p)) && ReadValue(g.rec + c::gun_mode_cur, out.gun_state[k]) &&
                            ReadValue(g.rec + c::gun_path_flags, out.path_flags[k]) && ReadValue(g.rec + c::gun_b3d5, out.b3d5[k]) &&
                            ReadValue(g.rec + c::gun_class, out.gun_class[k]) && ReadValue(g.rec + c::gun_stance, out.stance[k]);
            out.gun_flags[k] = g.gun3c0;
            for (unsigned i = 0; out.gun_ok[k] && i < 3; ++i) out.aimpt[k][i] = p[i];
        }
        RateRead r{};
        if (ReadRates(a, s, k, r) && r.rec)
            out.rate_ok[k] = ReadValue(r.rec + c::rate_aim_pitch, out.r5c[k]) && ReadValue(r.rec + c::rate_aim_yaw, out.r60[k]);
        float pos[4]{}, quat[4]{};
        if (Realized(s, k) && ReadBody(a, s, k, pos, quat)) {
            out.body_ok[k] = true;
            for (unsigned i = 0; i < 3; ++i) out.body[k][i] = pos[i];
            const double x = quat[0], y = quat[1], z = quat[2], w = quat[3];
            const double rx = 2.0 * (x * z + w * y), rz = 1.0 - 2.0 * (x * x + y * y);
            out.body_ok[k] = std::isfinite(rx) && std::isfinite(rz);
            out.body_yaw[k] = out.body_ok[k] ? static_cast<float>(std::atan2(rx, rz)) : 0.0f;
        }
    }
    return out.q_ok || out.gun_ok[0] || out.gun_ok[1] || out.rate_ok[0] || out.rate_ok[1];
}

} // namespace mgscoop::partner_player
