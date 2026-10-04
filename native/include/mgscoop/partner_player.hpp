#pragma once
// Offline partner spike: guarded reads and writes of the game's own second
// player instance (R = 1) for docs/offline-partner-spike-plan.md 2.3 item 6.
//
// Every read goes through ReadProcessMemory on the current process, so a null,
// freed or unmapped pointer becomes a refusal instead of a fault. Every write
// re-reads the whole chain first and then passes ALL mandatory guards:
//   P vtable; P+0x204 bit 0 set; P+0x204 bit 0x100 (S_IS_ONLINE) CLEAR;
//   P+0xA8 (synchronizer) == 0; count == 2; local index == 0; R == 1;
//   Q vtable; S, *S and *(S+0x20) non-null; not mission_terminating
//   (restorations - clearing our bits, restoring the policy byte - are allowed
//   while terminating; everything that adds state is not).
// This module never writes P+0x204 (no flag bit at all, in particular never
// 0x100) and never creates or touches the synchronizer. The only stores are:
// interlocked dword OR/AND of bit R in Q+0x194/Q+0x198, one aligned 16-byte
// store of (*(S+0x20))[R], an interlocked byte OR of bit R in **S, and the
// policy relax byte (vtable-checked; last resort). Each store runs inside SEH.
// Callers serialize (the script observer's receipt lock); last-error is kept.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_player {

// Absolute addresses (image base + contract RVA, or a synthetic map in tests).
struct Addresses {
    std::uintptr_t player_slot;
    std::uintptr_t p_vtable;
    std::uintptr_t q_vtable;
    std::uintptr_t policy_vtable;       // final interface vtable
    std::uintptr_t policy_base_vtable;  // base class vtable
    std::uintptr_t policy_function;     // expected value of policy vtable +0x10
    std::uintptr_t realizer_vtable;
    std::uintptr_t glob_suppress;
    std::uintptr_t glob_parts;
    std::uintptr_t vars_slot;
    std::uintptr_t record_bank;
    std::uintptr_t record_base;
};
Addresses AddressesFor(const void* base) noexcept;

// Exact-byte gate: every contract code site and vtable slot in the running
// image (partner_player_contract.hpp). False on the first mismatch, with its
// RVA (or the vtable RVA for a slot). Run once before any other call.
bool CodeGate(const void* base, std::size_t span, std::uintptr_t& mismatch_rva) noexcept;

enum class Refusal : std::uint8_t { none, player_absent, vtable, count, local_index, online_bit, sync_present, q_vtable, s_null };
// Log spelling: player_absent|vtable|count|local_index|online_bit|sync_present|q_vtable|s_null ("ok" for none).
const char* RefusalName(Refusal refusal) noexcept;

struct State {
    std::uintptr_t P, Q, S;
    std::uintptr_t exists, local_bits, slots;  // *S, *(S+8), *(S+0x20)
    std::uintptr_t sync, realizer;
    std::uint32_t flags, count, realized_count, local;
    // Valid when ReadState returned none.
    std::uint16_t ids[2];
    std::uint8_t pool[2];
    std::uint8_t parts[2];
    std::uint32_t rbits;
    std::uint8_t s0, s8;
    std::uint32_t q170, q180, q194, q198;
    float slot[2][4];  // (*(S+0x20))[0..1] = {x, y, z, yaw}
    bool arrays_ok, bits_ok, slots_ok;
    // Optional diagnostics; each has its own validity flag.
    std::uint32_t glob;  // dword 0x2C1B0F0
    bool glob_ok;
    std::uint32_t pb_guard, pb_flag[2], pb_state[2];
    bool pb_ok;
    float rec1[4];
    std::uint32_t rec1_status;
    bool rec_ok;
    float vars_pos[3];
    bool vars_ok;
};
// Reads only. Returns the first failed guard in the order listed above (the
// fields read so far stay filled for logging).
Refusal ReadState(const Addresses& addresses, State& out) noexcept;
// ids[index] != 0xFFFF and pool[index] != 0xFF (the 0x9CD160 / Warp guard).
bool Realized(const State& state, std::uint32_t index) noexcept;

enum class WriteKind : std::uint8_t { set, restore };
// Pure ordering of the mandatory guards for a write: the state refusal first,
// then the index (must be 1), then mission termination (set kind only).
// nullptr = allowed; otherwise a static reason (a RefusalName, "index" or
// "terminating").
const char* CheckWrite(Refusal refusal, const State& state, std::uint32_t index, bool terminating, WriteKind kind) noexcept;

// Writes. nullptr on success, otherwise a static reason (CheckWrite reasons,
// "alignment" or "fault"). q194/q198 receive the values read back afterwards.
const char* OrRealizeBits(const Addresses& addresses, std::uint32_t index, bool terminating,
                          std::uint32_t& q194, std::uint32_t& q198) noexcept;
const char* ClearRealizeBits(const Addresses& addresses, std::uint32_t index, bool terminating,
                             std::uint32_t& q194, std::uint32_t& q198) noexcept;
const char* WriteSlot(const Addresses& addresses, std::uint32_t index, bool terminating, const float (&xyzw)[4]) noexcept;
const char* ForceExists(const Addresses& addresses, std::uint32_t index, bool terminating) noexcept;
// policy = *(*(P+0xA0)+0x50); requires the realizer vtable, a policy vtable
// whose +0x10 is the policy function, and policy+8 == P. relax=true stores 1
// (set kind); relax=false stores restore_value (restore kind). previous gets
// the byte found before the store.
const char* SetPolicyRelax(const Addresses& addresses, bool relax, std::uint8_t restore_value, bool terminating,
                           std::uint8_t& previous) noexcept;

// Read-only animation-slot survey for instance 0 (E10). Element types [S].
struct Anim {
    std::uint16_t q18, qd8;
    std::uint8_t q28;
    std::uint32_t q30, q38;
    std::uint32_t m1d8, m1e0, m1f8;  // FNV-1a of the instance-0 motion records
};
bool ReadAnim(const State& state, Anim& out) noexcept;

// Integer conversions for log lines (no %f; non-finite -> INT32_MIN).
std::int32_t ToMm(float metres) noexcept;
std::int32_t ToMrad(float radians) noexcept;
// "partner_cfg=ready; flags=...; code_gate=ok\r\n"
std::size_t FormatReady(const State& state, char* out, std::size_t capacity) noexcept;
// "partner_probe=state; t_ms=..; ...; rec1_status=0x........\r\n"
std::size_t FormatProbe(const State& state, std::uint64_t t_ms, std::uint32_t stage, char* out, std::size_t capacity) noexcept;
inline constexpr std::size_t probe_capacity = 768;

namespace detail {
// Test seam for CodeGate on a synthetic image.
struct GateSite { std::uintptr_t rva; std::size_t size; const unsigned char* bytes; };
struct GateSlot { std::uintptr_t vtable_rva; std::size_t offset; std::uintptr_t target_rva; };
bool CodeGateWith(const void* base, std::size_t span, const GateSite* sites, std::size_t site_count,
                  const GateSlot* slots, std::size_t slot_count, std::uintptr_t& mismatch_rva) noexcept;
}

} // namespace mgscoop::partner_player
