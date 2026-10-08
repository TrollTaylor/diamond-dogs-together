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
// store of (*(S+0x20))[R], an interlocked byte OR of bit R in **S, the
// policy relax byte (vtable-checked; last resort), the slot-R elements of the
// Q animation arrays (u16 Q+0x18, u8 Q+0x30/0x38) and the slot-R face_equip
// byte Q+0x68. Each store runs inside SEH.
// Native game calls (animation/movement experiments, all for instance R = 1
// only, never for the local instance): PS.vt+0 SetStance (0xFF5B10), the
// stance request component (PS.vt+0xC8 = 0xFF3220, then X.vt+0x10), and the
// body transform T.vt+0x70/+0x68 plus Warp's resets *(blk+0xC0).vt+0x90 /
// *(blk+0xA0).vt+0x80 exactly as Warp 0x9CEAE0 calls them (0x9CEBD4..0x9CEC32).
// Each call re-reads the whole chain, passes the write guards above, needs
// instance R realized with ids[R] == R, and every component identity of the
// census (block, PS, CS vtables and back pointers; T/A0/C0 vtables equal to
// the local instance's and their slot targets inside the code section, or
// equal to the pinned RVAs once pinned). Each call runs inside SEH.
// Callers serialize (the script observer's receipt lock); last-error is kept.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_input.hpp"
#include "mgscoop/partner_receipt.hpp"

namespace mgscoop::partner_player {

// Absolute addresses (image base + contract RVA, or a synthetic map in tests).
struct Addresses {
    std::uintptr_t player_slot;
    std::uintptr_t p_vtable;
    std::uintptr_t q_vtable;
    std::uintptr_t policy_vtable;       // final interface vtable (the only one accepted)
    std::uintptr_t policy_function;     // expected value of policy vtable +0x10
    std::uintptr_t realizer_vtable;
    std::uintptr_t glob_suppress;
    std::uintptr_t glob_parts;
    std::uintptr_t vars_slot;
    std::uintptr_t record_bank;
    std::uintptr_t record_base;
    std::uintptr_t mirror_object;       // engine mirror object pointer (mirror off needs it non-null)
    // Component census and native calls (contract RVAs; synthetic in tests).
    std::uintptr_t image_base;          // for RVA log fields (0 logs absolute values)
    std::uintptr_t code_begin, code_end;    // call targets must lie in [begin, end)
    std::uintptr_t image_begin, image_end;  // vtables must lie in [begin, end)
    std::uintptr_t block_vtable;
    std::uintptr_t ps_vtable;
    std::uintptr_t ps_vtable_alt;  // live player subclass; slot 0 and +0xC8 identical to the base
    std::uintptr_t set_stance;          // PS.vt+0 (SetStance)
    std::uintptr_t ps_component;        // PS.vt+0xC8 (stance request component getter)
    std::uintptr_t cs_vtable;
    std::uintptr_t get_transform;       // CS.vt+0x388
    std::uintptr_t t_vtable, a0_vtable, c0_vtable;  // pinned after a reviewed census; 0 = unpinned
    // Round 3 (input lever, direct motion).
    std::uintptr_t pp_vtable;           // PlayerPad 0x23C8D40
    std::uintptr_t cam_vtable;          // 0x23B8250
    std::uintptr_t plugin_vt[4];        // base action plugin +0/+0x10/+0x30/+0x48
    std::uintptr_t dm_prim_vtable, dm_iface_vtable;
    std::uintptr_t script_object;       // g = 0x2C1BF20 (P+0x48)
    // Native puppet (docs/native-puppet.md).
    std::uintptr_t go_vtable;           // 0x22953D0 (GO = P - 0x20, primary view)
    std::uintptr_t gun_node_vtable;     // 0x2306628
    std::uintptr_t gun_prim_vtable;     // 0x23B3E80
    std::uintptr_t gun_iface_vtable;    // 0x23B3FA0 (prim+0x30)
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
    std::uintptr_t mirror_object;  // *(mirror_object); 0 when unreadable
    // The look the parts block of instance 1's slot was requested with: the
    // descriptor look *(M+0x1100+8*slot) (0x9B442D; bytes type, parts, camo,
    // hand, face u16, face_equip), slot = (1 == local) ? 0 : (1 < local ? 2 : 1).
    bool req_ok;
    partner::Look req_look;
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
// policy = *(*(P+0xA0)+0x50); requires the realizer vtable, the FINAL policy
// vtable (the base-class vtable means the object is being constructed or
// destroyed: refused) whose +0x10 is the policy function, and policy+8 == P.
// relax=true stores 1
// (set kind); relax=false stores restore_value (restore kind). previous gets
// the byte found before the store.
const char* SetPolicyRelax(const Addresses& addresses, bool relax, std::uint8_t restore_value, bool terminating,
                           std::uint8_t& previous) noexcept;

// Per-slot animation fields. Element types and n = *(u32*)(S+0x40) = 2 from
// the Q allocator 0xFDF010 (arrays packed back to back with natural
// alignment): u16 *(Q+0x18)[i] action/locomotion id, u8 *(Q+0x28)[i],
// u8 *(Q+0x30)[i] stance (1 stand, 2 squat, 3 crawl), u8 *(Q+0x38)[i] speed
// class (0 idle, 2 walk, 3 run), u16 *(Q+0xD8)[i] (diagnostic).
struct Anim {
    std::uint16_t act[2];
    std::uint8_t q28[2];
    std::uint8_t stance[2];
    std::uint8_t speed[2];
    std::uint16_t qd8[2];
    std::uint32_t m1d8, m1e0, m1f8;  // FNV-1a of the instance-0 motion records
};
bool ReadAnim(const State& state, Anim& out) noexcept;
// The sync form of slot `index` (stance/speed clamped to 0..3; flags valid).
sync::AnimState AnimSlot(const Anim& anim, std::uint32_t index) noexcept;

// Both instances' appearance arrays: u8 Q+0x40 type, Q+0x48 parts, Q+0x50
// camo, Q+0x58 hand, u16 Q+0x60 face, u8 Q+0x68 face_equip (same allocator).
struct Looks {
    partner::Look look[2];
};
bool ReadLooks(const State& state, Looks& out) noexcept;

// Component census of one realized instance k (pure reads; no game call):
//   blk = *(P+0x378) + pool[k]*0x6A8 == *(*(P+0x50) + 8*pool[k]), vtable
//   0x2295210, blk+0x138 == P, blk+0xC <= ids[k] < blk+0xC + blk+8;
//   PS = *(blk+0x78), vtable 0x23B09F0 (slot 0 SetStance), PS+0x408 == blk,
//   PS+8 == blk+0xC;
//   CS = *(PS+0x238), vtable 0x22E6070 (slot +0x388 GetTransform 0xAF1130),
//   entry = *(CS+0x10) + (ids[k] - *(CS+0xC))*0xC0, T = *(entry+0x60) + 0x10
//   (0xAF1130 replicated without calling it); A0 = *(blk+0xA0), C0 = *(blk+0xC0).
// Two independent verdicts: `refusal` covers the block/PS identities plus the
// transform chain (what xform/hybrid and ReadBody need); `stance_refusal`
// covers the block/PS identities plus the SetStance prerequisites the
// transform path never touches: the stance words *(PS+0x298) non-null with the
// word for this id readable (0xFF5B10 writes it) and the component
// *(PS+0x260) non-null (0xFF5B10 calls its vt[0]; set lazily by a hash lookup
// at 0xFFF346, so it may still be null while the transform chain is fine).
struct Block {
    const char* refusal;         // nullptr when the identities and the transform chain hold
    const char* stance_refusal;  // nullptr when the identities and the stance prerequisites hold
    std::uint32_t id, base_id, id_count;
    std::uintptr_t blk, ps, cs, entry, t, a0, c0, b70;
    std::uintptr_t blk_vt, ps_vt, cs_vt;  // as read (0 when unreadable)
    std::uintptr_t t_vt, t68, t70, a0_vt, a080, c0_vt, c090, b70_vt;
    std::uintptr_t t58;   // T.vt+0x58: set position only (0x1AD5660)
    std::uint8_t psw;     // low byte of the stance word *(PS+0x298)[id - base] (0 unless stance_refusal is null)
};
struct Census {
    const char* refusal;  // nullptr when both blocks pass and T/A0/C0 are callable
    Block k[2];
    std::uintptr_t equip_vt;  // *(P+0xC8) vtable (logged only)
};
// Refusal reasons: unrealized|blk_vtable|blk_owner|blk_index|ps_vtable|ps_back|
// cs_vtable|entry|t_null|t_class|a0_class|c0_class|id_mismatch; stance
// refusals: the identity reasons or ps_words|ps_sub.
const char* ResolveBlock(const Addresses& addresses, const State& state, std::uint32_t k, Block& out) noexcept;
const char* ResolveCensus(const Addresses& addresses, const State& state, Census& out) noexcept;
// "partner_census; k=..; ..." with RVAs relative to addresses.image_base.
std::size_t FormatCensus(const Addresses& addresses, const Census& census, std::uint32_t k, char* out, std::size_t capacity) noexcept;
inline constexpr std::size_t census_capacity = 640;

// Writes (same guards as WriteSlot, slot `index` only, which must be 1).
// aset=q: u16 *(Q+0x18)[1] = act, u8 *(Q+0x30)[1] = stance, u8 *(Q+0x38)[1] = speed;
// refused unless n == *(u32*)(S+0x40) == 2 and the three array pointers are non-null.
const char* WriteAnim(const Addresses& addresses, std::uint32_t index, bool terminating, std::uint16_t act, std::uint8_t stance,
                      std::uint8_t speed) noexcept;
// u8 *(Q+0x68)[1] = face_equip (the one look field no Lua binding writes).
const char* WriteFaceEquip(const Addresses& addresses, std::uint32_t index, bool terminating, std::uint8_t value) noexcept;

// Native calls. nullptr on success, otherwise a static reason (CheckWrite or
// census reasons, "stance", "alignment", "x_null", "x_class" or "fault").
// "fault" means the call raised inside SEH: the caller disables native calls.
// SetStance(PS1, ids[1], stance 1..3) through the pinned 0xFF5B10, then reads
// back the stance word low byte and Q+0x30[1].
const char* SetStance(const Addresses& addresses, std::uint32_t index, bool terminating, std::uint8_t stance,
                      std::uint8_t& psw_after, std::uint8_t& q30_after) noexcept;
// X = PS1.vt+0xC8() (pinned 0xFF3220, after its own pointer chain was read
// non-null), X0 the same for PS0; X != 0, *X == *X0, X.vt+0x10 in code;
// then X.vt+0x10(X, index, stance), as Lua RequestToSetTargetStance (0x9DB170) does.
const char* RequestStance(const Addresses& addresses, std::uint32_t index, bool terminating, std::uint8_t stance) noexcept;
// T.vt+0x70(T, quat) then T.vt+0x68(T, pos, quat) on instance `index`'s
// transform (Warp 0x9CEBF8/0x9CEC0A), then the resets in Warp's order
// (C0.vt+0x90(id) before A0.vt+0x80(id)) as `reset` selects. pos/quat are
// copied to 16-byte aligned buffers (Warp passes aligned vectors).
const char* ApplyTransform(const Addresses& addresses, std::uint32_t index, bool terminating, const float (&pos)[4],
                           const float (&quat)[4], PartnerReset reset) noexcept;
// Round 4: T.vt+0x58(T, pos) only (0x1AD5660, verified): T+0x20 = pos,
// T+0x10 = pos - (0, T+0x160, 0), T+0x174 = that foot y, T+0x1A4 = 0,
// T+0x1F0 = 0, T+0x1A0 &= ~4. The rotation (T+0x60), the pending rotation
// request (T+0x80 / T+0x178 bit 0x100) and the locomotion state are left as
// the game has them: a nudge moves the body without turning it. Same guards
// as ApplyTransform (census identities, slot +0x58 equal to the local
// instance's and inside the code section).
const char* ApplyPosition(const Addresses& addresses, std::uint32_t index, bool terminating, const float (&pos)[4]) noexcept;
// Read-only: T+0x20 (position) and T+0x60 (rotation quaternion) of `index`.
bool ReadBody(const Addresses& addresses, const State& state, std::uint32_t index, float (&pos)[4], float (&quat)[4]) noexcept;
// Read-only diagnostic: the game's per-id velocity PS+0x268[id - PS+8]
// (16-byte stride), which the PS per-id update stores as T+0x40 divided by
// S+0x38[id] * S+0x48 (0xFF2364..0xFF23CD). That it is m/s is inferred.
bool ReadVelocity(const Addresses& addresses, const State& state, std::uint32_t index, float (&vel)[4]) noexcept;

// --- Round 3: input lever targets (pure reads) -------------------------------
// The objects the block-1 context window touches, resolved in this order
// (refusal reasons in parentheses): the write guards of the state (their
// RefusalName or "index"); instance 1 realized (unrealized), ids[1] == 1
// (id), parts[1] == 3 (parts); P+0x204 & 0x40000 clear (alt_dispatch);
// PP = *(P+0xB0) with vtable 0x23C8D40 exactly (pp_vt; the hook's per-window
// copies are refused: they are only live inside a window),
// *(PP+0x280) == P (pp_owner), src = *(PP+8) != 0 (src); CAM = *(P+0xB8)
// with vtable 0x23B8250 (cam_vt); block 1 as ResolveBlock checks it (its
// reasons), root1 = *(blk1+0x190) != 0 (root); obj = *(*(*(PS1+0x658)+0x30)+0x10)
// with vtable 0x23B5060 (obj_vt), plugin1 = obj - 0x10 with the vtables
// 0x23B5038/0x23B5060/0x23B5090/0x23B50D0 (plugin_vt) and *(plugin1+8) ==
// blk1 (plugin_blk); idx = 1 - *(u32*)(*(plugin1+0x38)+0x24) below
// *(u32*)(node+0x20) (rec_idx); rec1 = *(plugin1+0x1A0) + idx*0x390 readable
// (rec). Block 0 is resolved the same way for root0/rec0 (logged only).
struct InputTargets {
    const char* refusal;
    std::uintptr_t P, pp, src, cam, blk0, blk1, root0, root1, ps1, obj, plugin1, rec1, rec0;
    std::uintptr_t p_vt, pp_vt, src_vt, cam_vt, ps1_vt, obj_vt, plugin_vt[4];
    std::uint32_t flags, cam_mode, rec_idx, rec_count, rec0_2ac, rec1_2ac;
    std::uint8_t pool1;
    bool alt_dispatch, pp_owner_ok, rec0_ok;
};
const char* ResolveInputTargets(const Addresses& addresses, const State& state, InputTargets& out) noexcept;
// "partner_input_census; P=..; ...; verdict=.." with vtables as RVAs.
std::size_t FormatInputCensus(const Addresses& addresses, const InputTargets& targets, char* out, std::size_t capacity) noexcept;
inline constexpr std::size_t input_census_capacity = 768;
// The local pad (sender side): PP/pp_owner/src as above, then src+0x48..+0x6F,
// PP+0x10..+0x33 and PP+0x460..+0x488. Needs only the state guards.
bool ReadPad(const Addresses& addresses, const State& state, partner_input::PadRaw& out) noexcept;

// --- Native puppet (docs/native-puppet.md; pure reads) ------------------------
// Identity the deferral needs (GO = P - 0x20 with the primary vtable, both
// pool indices) and instance 1's look slot: PS1 = *(blk1+0x78) with the PS or
// player-PS vtable and *(PS1+0x3E8) == that vtable + 0x1E0, PS1 != PS0,
// *(u32*)(PS1+8) == root1+0x24 with root1+0x24 <= 1 < root1+0x24 + root1+0x20,
// and the look (*(PS1+0x290)) and record (*(PS1+0x280)) arrays non-null.
// `refusal` covers the identity; `look_refusal` the look slot only.
struct NativeTargets {
    const char* refusal;
    const char* look_refusal;
    std::uintptr_t go, go_vt;
    std::uint8_t pool0, pool1;
    std::uintptr_t ps0, ps1, ps1_vt, ps1_look, ps1_rec;
    std::uint32_t ps1_base, ps1_idx;
};
const char* ResolveNative(const Addresses& addresses, const State& state, const InputTargets& targets, NativeTargets& out) noexcept;
// The sender's v3 fields (the local instance): its look angles {pitch, yaw}
// at *(PS0+0x290) + (local - PS0+8)*8 (PS0 validated like PS1 against root0),
// CAM+0x270/+0x274 and PP+0x14 (the caller gates PP on an even WindowSeq).
struct LookSource {
    bool look_ok, cam_ok, heading_ok;
    float pitch, yaw, cam270, cam274, heading;
};
bool ReadLookSource(const Addresses& addresses, const State& state, LookSource& out) noexcept;
// Instance 1's gun node (builder row 0x3D) for telemetry: node = *(blk1+0x378)
// (vtable 0x2306628, +0x20 == 1, +0x24 == id, +0x2C == 0x3D), iface =
// *(blk1+0x5A0) (vtable 0x23B3FA0), prim = iface - 0x30 (vtable 0x23B3E80,
// prim+8 == blk1, prim+0x38 == node), rec = *(prim+0x58): Gun3C0 = rec+0x3C0,
// mode = rec+0x2C1, active = *(u32*)*(node+8) bit 0.
struct Gun1 {
    const char* refusal;
    bool node_active;
    std::uint32_t gun3c0;
    std::uint8_t mode;
};
bool ReadGun1(const Addresses& addresses, const State& state, Gun1& out) noexcept;

// --- Round 3: direct motion (pure reads plus one restoration write) ---------
// g = *(P+0x48) == 0x2C1BF20 (g); block 1 as ResolveBlock checks it; iface =
// *(blk1+0x3B8+8*5) with vtable 0x23BFF00 (iface_vt); prim = iface - 0x30
// with vtable 0x23BFEA8 (prim_vt) and *(prim+8) == blk1 (prim_blk); idx =
// 1 - *(u32*)(*(prim+0x38)+0x24) below *(u32*)(node+0x20) (rec_idx); rec =
// *(prim+0x68) + idx*0x250 (rec).
struct DmTargets {
    const char* refusal;
    std::uintptr_t g, blk1, iface, prim, rec;
    std::uintptr_t g_value, iface_vt, prim_vt;
    std::uint32_t rec_idx, rec_count;
    bool g_ok, prim_blk_ok;
};
const char* ResolveDm(const Addresses& addresses, const State& state, DmTargets& out) noexcept;
// cam1f0/cam200: the camera connect handle CAM+0x1F0 (qword) and its on flag
// CAM+0x200 (dword) of CAM = *(P+0xB8) (vtable 0x23B8250, else cam_ok=false).
// The DM entry playback without a locator (0x1198D9D) and DM End (0x11992E9)
// write both to 0 for ANY id, so a partner play is refused unless both are 0.
struct DmState {
    std::uint8_t g388, req0;
    std::uint64_t cam1f0;
    std::uint32_t cam200;
    bool cam_ok;
    std::uint64_t name;
    std::uint16_t q18[2];
    std::int32_t ps2d8[2];  // -1 unreadable
    std::uint8_t rec10, rec11, rec1a0, rec248;
    std::uint64_t rec18;
    bool ok, rec_ok;
};
bool ReadDm(const Addresses& addresses, const State& state, const DmTargets& targets, DmState& out) noexcept;
std::size_t FormatDmCensus(const Addresses& addresses, const DmTargets& targets, char* out, std::size_t capacity) noexcept;
// "partner_dm; patched=..; g388=0x..; ...\r\n"
std::size_t FormatDmProbe(const DmState& dm, bool patched, char* out, std::size_t capacity) noexcept;
// Our pending direct-motion request (g+0x388 bit 4 with request byte 0 == 1)
// is cleared with one locked byte AND (restoration: allowed while
// terminating). nullptr on success or when nothing of ours is pending.
const char* ClearDmPending(const Addresses& addresses, bool terminating, bool& cleared) noexcept;

// probe=ps (read-only, layouts [S] except +0x738/+0x740 from the setters
// 0xFF5220/0xFF5710/0xFF56E0): per instance the PS+0x740 dirty mask, the
// 10-float record *(PS+0x738)+(id-base)*0x28, the 32-byte record
// *(PS+0x280)+(id-base)*0x20, the bytes *(PS+0x2D0)[id-base] and
// *(PS+0x2D8)[id-base] and the stance word low byte.
std::size_t FormatPsProbe(const Addresses& addresses, const State& state, std::uint32_t k, char* out, std::size_t capacity) noexcept;

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
