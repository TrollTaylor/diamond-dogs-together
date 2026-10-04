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
    std::int32_t bank = -1;
    s.rec_ok = ReadValue(a.record_bank, bank) && (bank == 0 || bank == 1);
    if (s.rec_ok) {
        const std::uintptr_t record = a.record_base + (static_cast<std::uintptr_t>(bank) * c::record_bank_entries + 1) * c::record_stride;
        s.rec_ok = ReadBlock(record + c::record_pos, s.rec1, sizeof(s.rec1)) && ReadValue(record + c::record_status, s.rec1_status);
    }
    std::uintptr_t vars = 0;
    s.vars_ok = ReadValue(a.vars_slot, vars) && ReadValue(vars + c::vars_pos_x, s.vars_pos[0]) &&
                ReadValue(vars + c::vars_pos_y, s.vars_pos[1]) && ReadValue(vars + c::vars_pos_z, s.vars_pos[2]);
}

void LogMm(char* out, std::size_t capacity, const float* xyz) noexcept {
    StringCchPrintfA(out, capacity, "%d,%d,%d", ToMm(xyz[0]), ToMm(xyz[1]), ToMm(xyz[2]));
}

struct Preserve {
    DWORD error = GetLastError();
    ~Preserve() { SetLastError(error); }
};

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
    a.policy_base_vtable = b + c::policy_base_vtable_rva;
    a.policy_function = b + c::policy_function_rva;
    a.realizer_vtable = b + c::realizer_vtable_rva;
    a.glob_suppress = b + c::glob_suppress_rva;
    a.glob_parts = b + c::glob_parts_rva;
    a.vars_slot = b + c::vars_slot_rva;
    a.record_bank = b + c::record_bank_rva;
    a.record_base = b + c::record_base_rva;
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
        unsigned char copy[64]{};
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
        (policy_vtable != a.policy_vtable && policy_vtable != a.policy_base_vtable) ||
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
                    ReadValue(p18, out.q18) && ReadValue(p28, out.q28) && ReadValue(p30, out.q30) &&
                    ReadValue(p38, out.q38) && ReadValue(pd8, out.qd8) && ReadBlock(r1d8, r1, sizeof(r1)) &&
                    ReadBlock(r1e0, r2, sizeof(r2)) && ReadBlock(r1f8, r3, sizeof(r3));
    if (!ok) { out = Anim{}; return false; }
    out.m1d8 = Fnv1a(r1, sizeof(r1));
    out.m1e0 = Fnv1a(r2, sizeof(r2));
    out.m1f8 = Fnv1a(r3, sizeof(r3));
    return true;
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
    char pos0[48]{}, pos1[48]{}, rec1[48]{};
    LogMm(pos0, sizeof(pos0), s.slot[0]);
    LogMm(pos1, sizeof(pos1), s.slot[1]);
    LogMm(rec1, sizeof(rec1), s.rec1);
    if (FAILED(StringCchPrintfA(out, capacity,
            "partner_probe=state; t_ms=%llu; stage=%u; ids=%04x,%04x; pool=%02x,%02x; parts=%u,%u; rbits=0x%x; s0=0x%02x; "
            "s8=0x%02x; q170=0x%x; q180=0x%x; q194=0x%x; q198=0x%x; glob=%u; pb_guard=%u; pb_flag=%u,%u; pb_state=%u,%u; "
            "pos0_mm=%s; yaw0_mrad=%d; pos1_mm=%s; yaw1_mrad=%d; rec1_mm=%s; rec1_status=0x%08x\r\n",
            static_cast<unsigned long long>(t_ms), stage, unsigned(s.ids[0]), unsigned(s.ids[1]), unsigned(s.pool[0]),
            unsigned(s.pool[1]), unsigned(s.parts[0]), unsigned(s.parts[1]), s.rbits, unsigned(s.s0), unsigned(s.s8), s.q170,
            s.q180, s.q194, s.q198, s.glob, s.pb_guard, s.pb_flag[0], s.pb_flag[1], s.pb_state[0], s.pb_state[1], pos0,
            ToMrad(s.slot[0][3]), pos1, ToMrad(s.slot[1][3]), rec1, s.rec1_status))) {
        out[0] = 0;
        return 0;
    }
    return std::strlen(out);
}

} // namespace mgscoop::partner_player
