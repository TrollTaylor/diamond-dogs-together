// Offline partner spike: partner_player guard logic and crash_probe filtering
// against a synthetic memory map (no game, no game calls). Builds fake P/Q/S
// objects, vtables, globals and records in this process and checks every
// mandatory write guard, every refusal reason, the SEH fault path, the code
// gate (synthetic and the real contract laid into a reserved image) and the
// log formatting.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "mgscoop/partner_player.hpp"
#include "mgscoop/crash_probe.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/player_sync.hpp"
#include "mgscoop/udp_transport.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace pp = mgscoop::partner_player;
namespace c = mgscoop::partner_player_contract;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
template <class T>
void Put(unsigned char* object, std::size_t offset, T value) { std::memcpy(object + offset, &value, sizeof(value)); }
template <class T>
T Get(const unsigned char* object, std::size_t offset) { T value{}; std::memcpy(&value, object + offset, sizeof(value)); return value; }
std::uintptr_t Addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
template <class R, class... A>
std::uintptr_t Fn(R (*f)(A...)) { return reinterpret_cast<std::uintptr_t>(f); }

// Fake game call targets for the census world (defined after World). They
// record their arguments; FakeSetStance also emulates the real 0xFF5B10 stores.
struct CallLog {
    unsigned rot, pose, reset_a0, reset_c0, stance, request, sequence;
    unsigned rot_at, pose_at, c0_at, a0_at;  // call order (sequence numbers)
    std::uintptr_t rot_this, pose_this, c0_this, a0_this, stance_this, request_this;
    std::uintptr_t pos_addr, quat_addr;
    float pos[4], quat[4];
    std::uint32_t c0_id, a0_id, stance_id, stance_value, request_index, request_value;
    unsigned setpos;                 // round 4: T.vt+0x58 (position only)
    std::uintptr_t setpos_this, setpos_addr;
    float setpos_pos[4];
};
CallLog g_calls{};
void FakeSetRot(void* t, const float* quat);
void FakeSetPose(void* t, const float* pos, const float* quat);
void FakeSetPos(void* t, const float* pos);
void FakeResetA0(void* object, std::uint32_t id);
void FakeResetC0(void* object, std::uint32_t id);
void FakeSetStance(void* ps, std::uint32_t id, std::uint32_t stance);
void* FakeComponent(void* ps);
void FakeRequest(void* x, std::uint32_t index, std::uint32_t stance);
void FakeFault(void* t, const float* quat);
void FakeNever() {}

// One synthetic world. Objects live in a committed region so the test can flip
// page protection to exercise the SEH path.
struct World {
    unsigned char* region = nullptr;
    unsigned char *P, *Q, *S, *exists, *local_bits, *bitset, *bitset_data, *realizer, *policy, *g, *x, *vars, *records;
    float* slots;
    std::uint16_t* ids;
    std::uint8_t *pool, *parts;
    unsigned char *a18, *a28, *a30, *a38, *ad8, *r1d8, *r1e0, *r1f8;
    unsigned char *lt, *lp, *lc, *lh, *lf, *le;  // look arrays (u8, u16 face)
    // Component census world (blocks, PS, CS, T, A0/C0, stance component).
    unsigned char *blocks, *table, *ps[2], *cs, *entries, *tobj[2], *a0[2], *c0[2], *words[2], *sub, *rec738, *rec280, *b2d0,
        *b2d8, *comp, *inner, *object, *xc[2], *equip, *vel;
    std::uintptr_t block_vt[4]{}, ps_vt[32]{}, cs_vt[0x400 / 8]{}, t_vt[16]{}, t_vt2[16]{}, a0_vt[32]{}, c0_vt[32]{}, sub_vt[2]{},
        object_vt[8]{}, x_vt[4]{}, x_vt2[4]{}, equip_vt[2]{};
    std::uintptr_t p_vt[64]{}, q_vt[16]{}, policy_vt[4]{}, policy_base_vt[4]{}, realizer_vt[4]{}, bitset_vt[2]{};
    unsigned char policy_function_code[16]{};
    std::uintptr_t slot = 0, gptr = 0, varsptr = 0, mirrorptr = 0;
    std::uint32_t glob = 0;
    std::int32_t bank = 0;
    pp::Addresses addresses{};

    World() {
        region = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!region) std::exit(2);
        // Separate pages: P, Q, S+arrays, slots, policy/realizer, globals, records.
        P = region;                    // 0x0000
        Q = region + 0x1000;           // own page (read-only fault test)
        S = region + 0x2000;
        exists = region + 0x3000;      // own page
        slots = reinterpret_cast<float*>(region + 0x4000);  // own page, 16-aligned
        local_bits = region + 0x2100;
        ids = reinterpret_cast<std::uint16_t*>(region + 0x2200);
        pool = region + 0x2210;
        parts = region + 0x2220;
        bitset = region + 0x2300;
        bitset_data = region + 0x2340;
        a18 = region + 0x2400; a28 = region + 0x2410; a30 = region + 0x2420; a38 = region + 0x2430; ad8 = region + 0x2440;
        r1d8 = region + 0x2500; r1e0 = region + 0x2580; r1f8 = region + 0x25C0;
        realizer = region + 0x5000;
        policy = region + 0x5100;      // policy interface pointer (object+0x20 in the game)
        g = region + 0x6000;
        x = region + 0x7000;           // needs +0x10C8
        vars = region + 0x9000;
        records = region + 0xA000;     // 2*16*0xE0 = 0x1C00
        lt = region + 0x2600; lp = region + 0x2610; lc = region + 0x2620; lh = region + 0x2630; lf = region + 0x2640; le = region + 0x2650;
        blocks = region + 0x10000;     // 2 * 0x6A8
        table = region + 0x11000;
        ps[0] = region + 0x12000; ps[1] = region + 0x13000;
        cs = region + 0x14000; entries = region + 0x14100;
        tobj[0] = region + 0x15000; tobj[1] = region + 0x15100;
        a0[0] = region + 0x16000; a0[1] = region + 0x16040; c0[0] = region + 0x16080; c0[1] = region + 0x160C0;
        words[0] = region + 0x16200; words[1] = region + 0x16210;
        sub = region + 0x16300; rec738 = region + 0x16400; rec280 = region + 0x16500; b2d0 = region + 0x16600; b2d8 = region + 0x16610;
        comp = region + 0x16700; inner = region + 0x16800; object = region + 0x16900;
        xc[0] = region + 0x16A00; xc[1] = region + 0x16B00; equip = region + 0x16C00;
        vel = region + 0x16D00;        // PS+0x268 velocity arrays (16-byte entries)
        Reset();
    }
    ~World() { VirtualFree(region, 0, MEM_RELEASE); }
    void Reset() {
        DWORD old = 0;
        VirtualProtect(region, 0x20000, PAGE_READWRITE, &old);
        std::memset(region, 0, 0x20000);
        policy_function_code[0] = 0xC3;
        policy_vt[2] = Addr(policy_function_code);
        policy_base_vt[2] = Addr(policy_function_code);
        Put<std::uintptr_t>(P, 0, Addr(p_vt));
        Put<std::uint32_t>(P, c::p_flags, c::flag_valid | c::flag_all_active);
        Put<std::uint32_t>(P, c::p_count, 2);
        Put<std::uint32_t>(P, c::p_realized_count, 2);
        Put<std::uint32_t>(P, c::p_local, 0);
        Put<std::uintptr_t>(P, c::p_q, Addr(Q));
        Put<std::uintptr_t>(P, c::p_ids, Addr(ids));
        Put<std::uintptr_t>(P, c::p_pool, Addr(pool));
        Put<std::uintptr_t>(P, c::p_parts, Addr(parts));
        Put<std::uintptr_t>(P, c::p_realizer, Addr(realizer));
        Put<std::uintptr_t>(P, c::p_sync, 0);
        Put<std::uintptr_t>(P, c::p_bitset, Addr(bitset));
        ids[0] = 0x0000; ids[1] = 0xFFFF;
        pool[0] = 0x00; pool[1] = 0xFF;
        parts[0] = 3; parts[1] = 0;
        Put<std::uintptr_t>(bitset, 0, Addr(bitset_vt));
        Put<std::uintptr_t>(bitset, c::bitset_data, Addr(bitset_data));
        Put<std::uint32_t>(bitset_data, 0, 0x1);
        Put<std::uintptr_t>(Q, 0, Addr(q_vt));
        Put<std::uint32_t>(Q, c::q_exists_mask, 0x3);
        Put<std::uint32_t>(Q, c::q_parts_dirty, 0x0);
        Put<std::uint32_t>(Q, c::q_bits_a, 0x1);
        Put<std::uint32_t>(Q, c::q_bits_b, 0x1);
        Put<std::uintptr_t>(Q, c::q_s, Addr(S));
        Put<std::uintptr_t>(Q, c::q_anim_18, Addr(a18));
        Put<std::uintptr_t>(Q, c::q_anim_28, Addr(a28));
        Put<std::uintptr_t>(Q, c::q_anim_30, Addr(a30));
        Put<std::uintptr_t>(Q, c::q_anim_38, Addr(a38));
        Put<std::uintptr_t>(Q, c::q_anim_d8, Addr(ad8));
        Put<std::uintptr_t>(Q, c::q_rec_1d8, Addr(r1d8));
        Put<std::uintptr_t>(Q, c::q_rec_1e0, Addr(r1e0));
        Put<std::uintptr_t>(Q, c::q_rec_1f8, Addr(r1f8));
        // Per-slot arrays (u16 act, u8 q28/stance/speed, u16 qd8): slot 0 crouch-walking, slot 1 idle stand.
        Put<std::uint16_t>(a18, 0, 0x0004); Put<std::uint16_t>(a18, 2, 0x0001); a28[0] = 0x56; a28[1] = 0;
        a30[0] = 2; a30[1] = 1; a38[0] = 2; a38[1] = 0;
        Put<std::uint16_t>(ad8, 0, 0xBEEF); Put<std::uint16_t>(ad8, 2, 0xFFFF);
        Put<std::uint32_t>(S, c::s_count, 2);
        // Looks: slot 0 Snake (scarf, olive drab, normal hand, face 5, headgear 2), slot 1 unset (0xFF/0x7FFF).
        Put<std::uintptr_t>(Q, c::q_look_type, Addr(lt)); Put<std::uintptr_t>(Q, c::q_look_parts, Addr(lp));
        Put<std::uintptr_t>(Q, c::q_look_camo, Addr(lc)); Put<std::uintptr_t>(Q, c::q_look_hand, Addr(lh));
        Put<std::uintptr_t>(Q, c::q_look_face, Addr(lf)); Put<std::uintptr_t>(Q, c::q_look_face_equip, Addr(le));
        lt[0] = 0; lt[1] = 0xFF; lp[0] = 1; lp[1] = 0xFF; lc[0] = 0; lc[1] = 0xFF; lh[0] = 1; lh[1] = 0xFF;
        Put<std::uint16_t>(lf, 0, 5); Put<std::uint16_t>(lf, 2, 0x7FFF); le[0] = 2; le[1] = 0;
        // Component census (instance 1 realized only when the census tests ask for it).
        block_vt[0] = Fn(&FakeNever);
        ps_vt[0] = Fn(&FakeSetStance);
        ps_vt[0xC8 / 8] = Fn(&FakeComponent);
        cs_vt[0x388 / 8] = Fn(&FakeNever);
        t_vt[0x68 / 8] = Fn(&FakeSetPose); t_vt[0x70 / 8] = Fn(&FakeSetRot); t_vt[0x58 / 8] = Fn(&FakeSetPos);
        t_vt2[0x68 / 8] = Fn(&FakeSetPose); t_vt2[0x70 / 8] = Fn(&FakeSetRot); t_vt2[0x58 / 8] = Fn(&FakeSetPos);
        a0_vt[0x80 / 8] = Fn(&FakeResetA0);
        c0_vt[0x90 / 8] = Fn(&FakeResetC0);
        sub_vt[0] = Fn(&FakeNever);
        object_vt[0x18 / 8] = Fn(&FakeNever);
        x_vt[0x10 / 8] = Fn(&FakeRequest); x_vt2[0x10 / 8] = Fn(&FakeRequest);
        Put<std::uintptr_t>(P, c::p_blocks, Addr(blocks));
        Put<std::uintptr_t>(P, c::p_block_table, Addr(table));
        Put<std::uintptr_t>(P, c::p_local_block, Addr(blocks));
        Put<std::uintptr_t>(P, c::p_equip, Addr(equip));
        Put<std::uintptr_t>(equip, 0, Addr(equip_vt));
        Put<std::uintptr_t>(cs, 0, Addr(cs_vt));
        Put<std::uint32_t>(cs, 8, 2);
        Put<std::uint32_t>(cs, c::cs_base_id, 0);
        Put<std::uintptr_t>(cs, c::cs_entries, Addr(entries));
        Put<std::uintptr_t>(comp, c::comp_inner, Addr(inner));
        Put<std::uintptr_t>(inner, c::comp_object, Addr(object));
        Put<std::uintptr_t>(object, 0, Addr(object_vt));
        Put<std::uintptr_t>(sub, 0, Addr(sub_vt));
        for (unsigned k = 0; k < 2; ++k) {
            unsigned char* blk = blocks + k * c::block_stride;
            Put<std::uintptr_t>(table, 8 * k, Addr(blk));
            Put<std::uintptr_t>(blk, 0, Addr(block_vt));
            Put<std::uint32_t>(blk, c::blk_id_count, 1);
            Put<std::uint32_t>(blk, c::blk_base_id, k);
            Put<std::uintptr_t>(blk, c::blk_ps, Addr(ps[k]));
            Put<std::uintptr_t>(blk, c::blk_a0, Addr(a0[k]));
            Put<std::uintptr_t>(blk, c::blk_c0, Addr(c0[k]));
            Put<std::uintptr_t>(blk, c::blk_owner, Addr(P));
            Put<std::uintptr_t>(ps[k], 0, Addr(ps_vt));
            Put<std::uint32_t>(ps[k], c::ps_base_id, k);
            Put<std::uintptr_t>(ps[k], c::ps_cs, Addr(cs));
            Put<std::uintptr_t>(ps[k], c::ps_sub260, Addr(sub));
            Put<std::uintptr_t>(ps[k], c::ps_rec280, Addr(rec280 + k * c::ps_rec280_stride));
            Put<std::uintptr_t>(ps[k], c::ps_stance, Addr(words[k]));
            Put<std::uintptr_t>(ps[k], c::ps_byte2d0, Addr(b2d0 + k));
            Put<std::uintptr_t>(ps[k], c::ps_byte2d8, Addr(b2d8 + k));
            Put<std::uintptr_t>(ps[k], c::ps_blk, Addr(blk));
            Put<std::uintptr_t>(ps[k], c::ps_comp, Addr(comp));
            Put<std::uintptr_t>(ps[k], c::ps_rec738, Addr(rec738 + k * c::ps_rec738_stride));
            Put<std::uint32_t>(ps[k], c::ps_mask740, 0x10u + k);
            Put<std::uintptr_t>(ps[k], c::ps_vel268, Addr(vel + k * 0x20));
            const float v[4] = {0.3f * static_cast<float>(k + 1), -0.1f, 0.4f * static_cast<float>(k + 1), 0.0f};
            std::memcpy(vel + k * 0x20, v, sizeof(v));
            words[k][0] = 0x11;
            Put<std::uintptr_t>(entries, k * c::cs_entry_stride + c::cs_entry_transform, Addr(tobj[k]));
            Put<std::uintptr_t>(tobj[k], c::transform_view, Addr(t_vt));
            Put<std::uintptr_t>(a0[k], 0, Addr(a0_vt));
            Put<std::uintptr_t>(c0[k], 0, Addr(c0_vt));
            Put<std::uintptr_t>(xc[k], 0, Addr(x_vt));
        }
        const float body_pos[4] = {4.0f, 5.0f, 6.0f, 0.0f}, body_quat[4] = {0.0f, 0.7071068f, 0.0f, 0.7071068f};
        std::memcpy(tobj[1] + c::transform_view + c::t_pos, body_pos, sizeof(body_pos));
        std::memcpy(tobj[1] + c::transform_view + c::t_quat, body_quat, sizeof(body_quat));
        const float r738[2] = {0.25f, -1.5f};
        std::memcpy(rec738 + c::ps_rec738_stride, r738, sizeof(r738));
        b2d0[1] = 7; b2d8[1] = 9;
        Put<std::uintptr_t>(S, c::s_exists, Addr(exists));
        Put<std::uintptr_t>(S, c::s_local, Addr(local_bits));
        Put<std::uintptr_t>(S, c::s_pos, Addr(slots));
        exists[0] = 0x01;
        local_bits[0] = 0x01;
        const float initial[8] = {1.0f, 2.0f, 3.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f};
        std::memcpy(slots, initial, sizeof(initial));
        Put<std::uintptr_t>(realizer, 0, Addr(realizer_vt));
        Put<std::uintptr_t>(realizer, c::realizer_policy, Addr(policy));
        Put<std::uintptr_t>(policy, 0, Addr(policy_vt));
        Put<std::uintptr_t>(policy, c::policy_owner, Addr(P));
        policy[c::policy_relax] = 0;
        glob = 0;
        gptr = Addr(g);
        Put<std::uintptr_t>(g, c::g_x, Addr(x));
        Put<std::uint64_t>(x, c::x_guard_ptr, 0);
        Put<std::uint32_t>(x, c::x_guard_mode, 0);
        Put<std::uint32_t>(x, c::x_flag, 0); Put<std::uint32_t>(x, c::x_flag + 4, 5);
        Put<std::uint32_t>(x, c::x_state, 3); Put<std::uint32_t>(x, c::x_state + 4, 1);
        varsptr = Addr(vars);
        Put<float>(vars, c::vars_pos_x, 10.0f); Put<float>(vars, c::vars_pos_y, 20.0f); Put<float>(vars, c::vars_pos_z, -30.0f);
        bank = 0;
        const float rec[4] = {7.0f, 8.0f, 9.0f, 1.0f};
        std::memcpy(records + (0 * 16 + 1) * c::record_stride + c::record_pos, rec, sizeof(rec));
        Put<std::uint32_t>(records, (0 * 16 + 1) * c::record_stride + c::record_status, 0xA5A5A5A5u);
        slot = Addr(P);
        mirrorptr = Addr(region + 0xF000);
        addresses = pp::Addresses{Addr(&slot), Addr(p_vt), Addr(q_vt), Addr(policy_vt), Addr(policy_function_code),
                                  Addr(realizer_vt), Addr(&glob), Addr(&gptr), Addr(&varsptr), Addr(&bank), Addr(records),
                                  Addr(&mirrorptr)};
        // The test process is the "image": every address is in range.
        addresses.code_begin = 1; addresses.code_end = UINTPTR_MAX;
        addresses.image_begin = 1; addresses.image_end = UINTPTR_MAX;
        addresses.block_vtable = Addr(block_vt);
        addresses.ps_vtable = Addr(ps_vt);
        addresses.set_stance = Fn(&FakeSetStance);
        addresses.ps_component = Fn(&FakeComponent);
        addresses.cs_vtable = Addr(cs_vt);
        addresses.get_transform = Fn(&FakeNever);
        g_calls = CallLog{};
    }
    // Instance 1 realized with its parts loaded (the precondition of every game call).
    void RealizePartner() {
        ids[1] = 0x0001; pool[1] = 0x01; parts[1] = 3;
    }
    std::uint32_t Flags() const { return Get<std::uint32_t>(P, c::p_flags); }
};
World* g_world = nullptr;

void FakeSetRot(void* t, const float* quat) {
    g_calls.rot_at = ++g_calls.sequence; ++g_calls.rot; g_calls.rot_this = Addr(t); g_calls.quat_addr = Addr(quat);
    std::memcpy(g_calls.quat, quat, sizeof(g_calls.quat));
}
void FakeSetPose(void* t, const float* pos, const float* quat) {
    g_calls.pose_at = ++g_calls.sequence; ++g_calls.pose; g_calls.pose_this = Addr(t); g_calls.pos_addr = Addr(pos);
    std::memcpy(g_calls.pos, pos, sizeof(g_calls.pos));
    if (Addr(quat) != g_calls.quat_addr) g_calls.quat_addr = 1;  // must be the same aligned quaternion
}
void FakeSetPos(void* t, const float* pos) {
    ++g_calls.setpos; g_calls.setpos_this = Addr(t); g_calls.setpos_addr = Addr(pos);
    std::memcpy(g_calls.setpos_pos, pos, sizeof(g_calls.setpos_pos));
}
void FakeResetA0(void* object, std::uint32_t id) { g_calls.a0_at = ++g_calls.sequence; ++g_calls.reset_a0; g_calls.a0_this = Addr(object); g_calls.a0_id = id; }
void FakeResetC0(void* object, std::uint32_t id) { g_calls.c0_at = ++g_calls.sequence; ++g_calls.reset_c0; g_calls.c0_this = Addr(object); g_calls.c0_id = id; }
void FakeSetStance(void* ps, std::uint32_t id, std::uint32_t stance) {
    ++g_calls.stance; g_calls.stance_this = Addr(ps); g_calls.stance_id = id; g_calls.stance_value = stance;
    // 0xFF5B10: stance word low byte = s | s << 4 (index id - PS+8), Q+0x30[id] = s.
    if (g_world && id < 2) {
        g_world->words[id][0] = static_cast<unsigned char>((stance & 0xF) | ((stance & 0xF) << 4));
        g_world->a30[id] = static_cast<unsigned char>(stance);
    }
}
void* FakeComponent(void* ps) {
    if (!g_world) return nullptr;
    return Addr(ps) == Addr(g_world->ps[1]) ? static_cast<void*>(g_world->xc[1]) : static_cast<void*>(g_world->xc[0]);
}
void FakeRequest(void* x, std::uint32_t index, std::uint32_t stance) {
    ++g_calls.request; g_calls.request_this = Addr(x); g_calls.request_index = index; g_calls.request_value = stance;
}
void FakeFault(void*, const float*) {
    volatile int* nowhere = nullptr;
    *nowhere = 1;
}

pp::Refusal Read(World& w, pp::State& s) { return pp::ReadState(w.addresses, s); }
bool Contains(const char* text, const char* part) { return std::strstr(text, part) != nullptr; }

// --- partner_net loopback harness (127.0.0.1 only; test ports 48190/48191).
SRWLOCK capture_lock = SRWLOCK_INIT;
char captured[64][320]{};
unsigned captured_count = 0;
void Capture(const char* line) noexcept {
    AcquireSRWLockExclusive(&capture_lock);
    if (captured_count < 64) lstrcpynA(captured[captured_count++], line, 320);
    ReleaseSRWLockExclusive(&capture_lock);
}
bool Captured(const char* text) {
    AcquireSRWLockShared(&capture_lock);
    bool found = false;
    for (unsigned i = 0; i < captured_count && !found; ++i) found = std::strcmp(captured[i], text) == 0;
    ReleaseSRWLockShared(&capture_lock);
    return found;
}
unsigned CapturedPrefix(const char* prefix) {
    AcquireSRWLockShared(&capture_lock);
    unsigned n = 0;
    for (unsigned i = 0; i < captured_count; ++i) if (std::strncmp(captured[i], prefix, std::strlen(prefix)) == 0) ++n;
    ReleaseSRWLockShared(&capture_lock);
    return n;
}
struct PeerInbox {
    SRWLOCK lock = SRWLOCK_INIT;
    std::uint8_t frames[32][64]{};
    std::size_t sizes[32]{};
    unsigned count = 0;
};
PeerInbox inbox;
void PeerDatagram(void*, const std::uint8_t* data, std::size_t size, const mgscoop::lab::Endpoint&) noexcept {
    AcquireSRWLockExclusive(&inbox.lock);
    if (inbox.count < 32 && size <= 64) {
        std::memcpy(inbox.frames[inbox.count], data, size);
        inbox.sizes[inbox.count++] = size;
    }
    ReleaseSRWLockExclusive(&inbox.lock);
}
unsigned InboxCount() {
    AcquireSRWLockShared(&inbox.lock);
    const unsigned n = inbox.count;
    ReleaseSRWLockShared(&inbox.lock);
    return n;
}
template <class Predicate>
bool WaitFor(Predicate predicate, DWORD timeout_ms = 2000) {
    const ULONGLONG until = GetTickCount64() + timeout_ms;
    while (!predicate()) {
        if (GetTickCount64() > until) return false;
        Sleep(5);
    }
    return true;
}
void SendFrame(mgscoop::lab::UdpTransport& peer, std::uint64_t session, std::uint32_t epoch, std::uint32_t sequence,
               std::uint32_t tick_ms, float x) {
    mgscoop::sync::PlayerTransformPacket packet{};
    packet.sequence = sequence;
    packet.x = x; packet.y = 1.0f; packet.z = -x; packet.rot_y = 0.5f; packet.stance = 1; packet.tick_ms = tick_ms;
    std::uint8_t frame[mgscoop::sync::transform_frame_size];
    if (!mgscoop::sync::encode_transform_frame(session, epoch, packet, frame)) std::exit(3);
    const mgscoop::lab::WireChunk chunk{frame, sizeof(frame)};
    peer.transmit(&chunk, 1, false);
}

void NetTests() {
    namespace net = mgscoop::partner_net;
    using mgscoop::pose::Status;
    Check(!net::Running() && !net::SubmitLocal(mgscoop::partner::Tick{}, mgscoop::sync::AnimState{}), "not running before Start");
    Check(net::Start(net::Config{48190, 48191, 30, 0, 0, Capture}), "partner_net starts");
    Check(Captured("partner_net=listening; port=48190; peer_port=48191\r\n"), "listening line");
    Check(net::Start(net::Config{1, 1, 1, 0, 0, Capture}) && CapturedPrefix("partner_net=") == 1, "second Start is a no-op");
    static mgscoop::lab::UdpTransport peer;
    mgscoop::lab::TransportConfig config{};
    config.listen_port = 48191;
    config.peer_port = 48190;
    Check(peer.start(config, mgscoop::lab::DatagramSink{&PeerDatagram, nullptr, nullptr}), "peer transport starts");

    // Send path: epoch 0 sends nothing; then one frame per rate interval.
    const mgscoop::partner::Tick tick{1, 1500, -250, 3000, 9000, 0, 2, 3, 0};
    const mgscoop::sync::AnimState none{};  // invalid anim: v1 frames
    Check(!net::SubmitLocal(tick, none), "no epoch, no frame");
    net::SetEpoch(30050);
    Check(net::SubmitLocal(tick, none), "frame submitted");
    Check(!net::SubmitLocal(tick, none), "rate limited within one interval");
    Check(WaitFor([] { return InboxCount() >= 1; }), "peer received the frame");
    {
        AcquireSRWLockShared(&inbox.lock);
        std::uint64_t session = 0; std::uint32_t epoch = 0; mgscoop::sync::PlayerTransformPacket got{};
        const bool decoded = inbox.sizes[0] == mgscoop::sync::transform_frame_size &&
            mgscoop::sync::decode_transform_frame(inbox.frames[0], inbox.sizes[0], session, epoch, got);
        ReleaseSRWLockShared(&inbox.lock);
        Check(decoded && session == net::kSession && epoch == 30050 && got.sequence == 1 && got.x == 1.5f && got.y == -0.25f &&
              got.z == 3.0f && got.stance == 2 && got.action_flags == mgscoop::sync::ActionFlag::Sprinting &&
              std::fabs(got.rot_y - 1.5708f) < 1e-3f, "frame contents (metres, radians, sprint)");
    }
    Sleep(40);
    Check(net::SubmitLocal(tick, none), "next interval sends again");

    // Receive path.
    Check(!net::SampleRemote(0).valid, "nothing received yet");
    SendFrame(peer, net::kSession, 30050, 1, 5000, 2.0f);
    Check(WaitFor([] { return net::SampleRemote(0).valid; }), "frame accepted");
    auto sample = net::SampleRemote(0);
    Check(sample.x == 2.0f && sample.z == -2.0f && sample.stance == 1 && sample.status == Status::live, "remote pose");
    Check(WaitFor([] { return Captured("partner_net=first_rx; seq=1; epoch=30050\r\n"); }), "first_rx line");
    SendFrame(peer, net::kSession, 10010, 2, 5100, 3.0f);        // other mission
    SendFrame(peer, net::kSession + 1, 30050, 3, 5200, 4.0f);    // other session
    SendFrame(peer, net::kSession, 30050, 1, 5300, 5.0f);        // repeated sequence
    std::uint8_t keepalive[mgscoop::sync::keepalive_frame_size];
    mgscoop::sync::encode_keepalive_frame(net::kSession, 30050, keepalive);
    const mgscoop::lab::WireChunk ka{keepalive, sizeof(keepalive)};
    peer.transmit(&ka, 1, false);
    const std::uint8_t junk[5] = {1, 2, 3, 4, 5};
    const mgscoop::lab::WireChunk junk_chunk{junk, sizeof(junk)};
    peer.transmit(&junk_chunk, 1, false);
    SendFrame(peer, net::kSession, 30050, 4, 5400, 6.0f);        // newer: accepted
    Check(WaitFor([] { return net::SampleRemote(0).x == 6.0f; }), "newer sequence accepted after the drops");
    Check(CapturedPrefix("partner_net=first_rx") == 1, "first_rx once");
    SendFrame(peer, net::kSession, 30050, 5, 5350, 7.0f);        // newer sequence, older sender clock
    Sleep(100);
    Check(net::SampleRemote(0).x == 6.0f, "older sender clock dropped");
    // Mission boundary: epoch 0 drops everything; reset clears the ring.
    net::ResetRemote();
    Check(!net::SampleRemote(0).valid, "reset clears the remote pose");
    net::SetEpoch(0);
    SendFrame(peer, net::kSession, 30050, 6, 5500, 8.0f);
    Sleep(100);
    Check(!net::SampleRemote(0).valid, "epoch 0 drops frames");
    net::SetEpoch(30050);
    SendFrame(peer, net::kSession, 30050, 1, 100, 9.0f);         // after reset any sequence starts fresh
    Check(WaitFor([] { return net::SampleRemote(0).x == 9.0f; }), "fresh sequence after reset");
    // The receive thread publishes the pose before invoking the log callback.
    // Waiting for the pose cannot establish that the callback has completed.
    Check(WaitFor([] { return CapturedPrefix("partner_net=first_rx") == 2; }), "first_rx again after reset");
    // Peer restart: an older sequence is accepted only after a 2 s gap. The
    // same wait proves the keepalive (no local send for 1 s).
    const unsigned before = InboxCount();
    SendFrame(peer, net::kSession, 30050, 0, 50, 10.0f);
    Sleep(100);
    Check(net::SampleRemote(0).x == 9.0f, "older sequence within the gap dropped");
    Sleep(2100);
    SendFrame(peer, net::kSession, 30050, 0, 60, 11.0f);
    Check(WaitFor([] { return net::SampleRemote(0).x == 11.0f; }), "restart accepted after the gap");
    sample = net::SampleRemote(0);
    Check(sample.status == Status::live, "restarted peer is live");
    bool keepalive_seen = false;
    AcquireSRWLockShared(&inbox.lock);
    for (unsigned i = before; i < inbox.count; ++i) {
        std::uint64_t session = 0;
        if (mgscoop::sync::decode_keepalive_frame(inbox.frames[i], inbox.sizes[i], session) && session == net::kSession) keepalive_seen = true;
    }
    ReleaseSRWLockShared(&inbox.lock);
    Check(keepalive_seen, "keepalive after 1 s without a send");
    net::RecordNativeMicros(10);
    net::RecordNativeMicros(30);
    Check(net::NowMs() > 0, "clock");

    // Partner wire v2: a valid anim state sends a 64-byte frame.
    mgscoop::sync::AnimState anim{};
    anim.act = 4; anim.stance = 2; anim.speed = 2; anim.flags = mgscoop::sync::AnimFlag::Valid;
    Sleep(40);
    const unsigned before_v2 = InboxCount();
    Check(net::SubmitLocal(tick, anim), "v2 frame submitted");
    Check(WaitFor([before_v2] { return InboxCount() > before_v2; }), "peer received the v2 frame");
    {
        AcquireSRWLockShared(&inbox.lock);
        std::uint64_t session = 0; std::uint32_t epoch = 0; mgscoop::sync::PlayerTransformPacket got{};
        mgscoop::sync::AnimState got_anim{}; bool has = false;
        bool found = false;
        for (unsigned i = before_v2; i < inbox.count && !found; ++i)
            found = inbox.sizes[i] == mgscoop::sync::transform_anim_frame_size &&
                    mgscoop::sync::decode_partner_transform_frame(inbox.frames[i], inbox.sizes[i], session, epoch, got, got_anim, has);
        ReleaseSRWLockShared(&inbox.lock);
        Check(found && has && got_anim.act == 4 && got_anim.stance == 2 && got_anim.speed == 2 && got.x == 1.5f, "v2 frame contents");
    }
    // Receiving a v2 frame fills the sample's anim fields.
    {
        mgscoop::sync::PlayerTransformPacket packet{};
        packet.sequence = 10; packet.x = 12.0f; packet.y = 1.0f; packet.z = -12.0f; packet.rot_y = 0.5f; packet.stance = 1; packet.tick_ms = 900;
        std::uint8_t frame[mgscoop::sync::transform_anim_frame_size];
        Check(mgscoop::sync::encode_transform_anim_frame(net::kSession, 30050, packet, anim, frame) != 0, "encode v2 for the receiver");
        const mgscoop::lab::WireChunk chunk{frame, sizeof(frame)};
        peer.transmit(&chunk, 1, false);
        Check(WaitFor([] { return net::SampleRemote(0).x == 12.0f; }), "v2 frame accepted");
        const auto with_anim = net::SampleRemote(0);
        Check(with_anim.anim_valid && with_anim.act == 4 && with_anim.anim_stance == 2 && with_anim.anim_speed == 2, "remote anim sampled");
    }
    // Looks: sent at once, then not again until changed or the 2 s heartbeat.
    const mgscoop::partner::Look snake{0, 1, 0, 1, 5, 2};
    const unsigned before_look = InboxCount();
    Check(net::SubmitLook(snake), "look sent");
    Check(!net::SubmitLook(snake), "unchanged look not resent within the heartbeat");
    Check(WaitFor([before_look] { return InboxCount() > before_look; }), "peer received the look");
    auto look_at = [](unsigned from, mgscoop::sync::LookPacket& out) {
        AcquireSRWLockShared(&inbox.lock);
        bool found = false;
        for (unsigned i = from; i < inbox.count && !found; ++i) {
            std::uint64_t session = 0; std::uint32_t epoch = 0;
            found = mgscoop::sync::decode_look_frame(inbox.frames[i], inbox.sizes[i], session, epoch, out) && session == net::kSession &&
                    epoch == 30050;
        }
        ReleaseSRWLockShared(&inbox.lock);
        return found;
    };
    mgscoop::sync::LookPacket sent{};
    Check(look_at(before_look, sent) && sent.type == 0 && sent.parts == 1 && sent.face == 5 && sent.face_equip == 2 &&
          sent.flags == mgscoop::sync::LookFlag::PartsLoaded, "look frame contents");
    const std::uint16_t first_rev = sent.revision;
    const unsigned before_change = InboxCount();
    Check(net::SubmitLook(mgscoop::partner::quiet_look), "changed look sent at once");
    Check(WaitFor([before_change] { return InboxCount() > before_change; }) && look_at(before_change, sent) && sent.type == 6 &&
          sent.revision == static_cast<std::uint16_t>(first_rev + 1), "revision bumps on change");
    // Receive side: the newest look of the current epoch, logged on change.
    Check(!net::SampleLook().valid, "no look received yet");
    {
        mgscoop::sync::LookPacket remote{6, 27, 116, 0, 0, 0, mgscoop::sync::LookFlag::PartsLoaded, 3};
        std::uint8_t frame[mgscoop::sync::look_frame_size];
        Check(mgscoop::sync::encode_look_frame(net::kSession, 30050, remote, frame) != 0, "encode remote look");
        const mgscoop::lab::WireChunk chunk{frame, sizeof(frame)};
        peer.transmit(&chunk, 1, false);
        Check(WaitFor([] { return net::SampleLook().valid; }), "look received");
        const auto got = net::SampleLook();
        Check(got.look.type == 6 && got.look.parts == 27 && got.look.camo == 116 && got.revision == 3, "remote look values");
        Check(WaitFor([] { return Captured("partner_net=look_rx; type=6; parts=27; camo=116; hand=0; face=0; fe=0; rev=3; loaded=1\r\n"); }),
              "look_rx line");
        peer.transmit(&chunk, 1, false);  // same revision again: no second line
        mgscoop::sync::LookPacket other_epoch = remote;
        other_epoch.revision = 4;
        std::uint8_t frame2[mgscoop::sync::look_frame_size];
        mgscoop::sync::encode_look_frame(net::kSession, 10010, other_epoch, frame2);
        const mgscoop::lab::WireChunk chunk2{frame2, sizeof(frame2)};
        peer.transmit(&chunk2, 1, false);  // other mission: dropped
        Sleep(100);
        Check(CapturedPrefix("partner_net=look_rx") == 1 && net::SampleLook().revision == 3, "repeat and other-epoch looks ignored");
    }
    net::ResetRemote();
    Check(!net::SampleLook().valid, "mission reset clears the received look");
    Check(net::SubmitLook(mgscoop::partner::quiet_look), "after a reset the unchanged look is sent at once");
    peer.stop();
}
} // namespace

int main() {
    World w;
    pp::State s{};

    // --- Ready state and diagnostics.
    Check(Read(w, s) == pp::Refusal::none, "synthetic world is ready");
    Check(s.P == Addr(w.P) && s.Q == Addr(w.Q) && s.S == Addr(w.S) && s.slots == Addr(w.slots), "pointer chain");
    Check(s.flags == (c::flag_valid | c::flag_all_active) && s.count == 2 && s.realized_count == 2 && s.local == 0, "header fields");
    Check(s.arrays_ok && s.ids[0] == 0 && s.ids[1] == 0xFFFF && s.pool[1] == 0xFF && s.parts[0] == 3, "arrays");
    Check(pp::Realized(s, 0) && !pp::Realized(s, 1) && !pp::Realized(s, 2), "realized guard");
    Check(s.rbits == 1 && s.s0 == 1 && s.s8 == 1 && s.q170 == 3 && s.q194 == 1 && s.q198 == 1, "bitsets");
    Check(s.slots_ok && s.slot[0][0] == 1.0f && s.slot[0][3] == 0.5f, "slots");
    Check(s.glob_ok && s.glob == 0 && s.pb_ok && s.pb_guard == 1 && s.pb_flag[1] == 5 && s.pb_state[0] == 3 && s.pb_state[1] == 1,
          "parts-block diagnostics");
    Put<std::uint64_t>(w.x, c::x_guard_ptr, 0x1234);
    Check(Read(w, s) == pp::Refusal::none && s.pb_guard == 0, "guard: pointer set, mode 0");
    Put<std::uint32_t>(w.x, c::x_guard_mode, 2);
    Check(Read(w, s) == pp::Refusal::none && s.pb_guard == 1, "guard: mode 2");
    Check(s.rec_ok && s.rec1[0] == 7.0f && s.rec1_status == 0xA5A5A5A5u, "record 1, bank 0");
    w.bank = 1;
    std::memset(w.records + (16 + 1) * c::record_stride + c::record_pos, 0, 16);
    Put<float>(w.records, (16 + 1) * c::record_stride + c::record_pos, 4.0f);
    Check(Read(w, s) == pp::Refusal::none && s.rec_ok && s.rec1[0] == 4.0f, "record 1, bank 1");
    w.bank = 2;
    Check(Read(w, s) == pp::Refusal::none && !s.rec_ok, "invalid bank is not read");
    Check(s.vars_ok && s.vars_pos[0] == 10.0f && s.vars_pos[2] == -30.0f, "vars position");
    Check(s.mirror_object == Addr(w.region + 0xF000), "engine mirror object read");
    w.mirrorptr = 0;
    Check(Read(w, s) == pp::Refusal::none && s.mirror_object == 0, "null engine mirror object is optional");
    w.addresses.mirror_object = 0x10;
    Check(Read(w, s) == pp::Refusal::none && s.mirror_object == 0, "unreadable engine mirror object reads 0");
    w.Reset();
    w.gptr = 0;
    Check(Read(w, s) == pp::Refusal::none && !s.pb_ok, "missing parts globals are optional");
    w.Reset();

    // --- Every refusal, in guard order; nothing is written.
    struct Case { const char* name; void (*mutate)(World&); pp::Refusal expected; };
    const Case cases[] = {
        {"slot empty", [](World& v) { v.slot = 0; }, pp::Refusal::player_absent},
        {"P unmapped", [](World& v) { v.slot = 0x10; }, pp::Refusal::player_absent},
        {"P vtable", [](World& v) { Put<std::uintptr_t>(v.P, 0, Addr(v.q_vt)); }, pp::Refusal::vtable},
        {"not valid", [](World& v) { Put<std::uint32_t>(v.P, c::p_flags, c::flag_all_active); }, pp::Refusal::player_absent},
        {"online bit", [](World& v) { Put<std::uint32_t>(v.P, c::p_flags, c::flag_valid | c::flag_online); }, pp::Refusal::online_bit},
        {"online bit before sync", [](World& v) {
            Put<std::uint32_t>(v.P, c::p_flags, c::flag_valid | c::flag_online); Put<std::uintptr_t>(v.P, c::p_sync, 0x1000); },
         pp::Refusal::online_bit},
        {"synchronizer", [](World& v) { Put<std::uintptr_t>(v.P, c::p_sync, Addr(v.g)); }, pp::Refusal::sync_present},
        {"count 1", [](World& v) { Put<std::uint32_t>(v.P, c::p_count, 1); }, pp::Refusal::count},
        {"count 4", [](World& v) { Put<std::uint32_t>(v.P, c::p_count, 4); }, pp::Refusal::count},
        {"local 1", [](World& v) { Put<std::uint32_t>(v.P, c::p_local, 1); }, pp::Refusal::local_index},
        {"Q null", [](World& v) { Put<std::uintptr_t>(v.P, c::p_q, 0); }, pp::Refusal::q_vtable},
        {"Q vtable", [](World& v) { Put<std::uintptr_t>(v.Q, 0, Addr(v.p_vt)); }, pp::Refusal::q_vtable},
        {"S null", [](World& v) { Put<std::uintptr_t>(v.Q, c::q_s, 0); }, pp::Refusal::s_null},
        {"*S null", [](World& v) { Put<std::uintptr_t>(v.S, c::s_exists, 0); }, pp::Refusal::s_null},
        {"*(S+0x20) null", [](World& v) { Put<std::uintptr_t>(v.S, c::s_pos, 0); }, pp::Refusal::s_null},
    };
    for (const Case& test : cases) {
        w.Reset();
        test.mutate(w);
        const std::uint32_t flags_before = w.Flags();
        const pp::Refusal refusal = Read(w, s);
        Check(refusal == test.expected, test.name);
        std::uint32_t q194 = 0, q198 = 0;
        const char* reason = pp::OrRealizeBits(w.addresses, 1, false, q194, q198);
        Check(reason && std::strcmp(reason, pp::RefusalName(test.expected)) == 0, "write refused with the state reason");
        const float xyzw[4] = {1, 2, 3, 4};
        Check(pp::WriteSlot(w.addresses, 1, false, xyzw) != nullptr && pp::ForceExists(w.addresses, 1, false) != nullptr &&
              pp::ClearRealizeBits(w.addresses, 1, false, q194, q198) != nullptr, "every write refused");
        std::uint8_t previous = 0;
        Check(pp::SetPolicyRelax(w.addresses, true, 0, false, previous) != nullptr, "policy refused");
        Check(Get<std::uint32_t>(w.Q, c::q_bits_a) == 1 && Get<std::uint32_t>(w.Q, c::q_bits_b) == 1 && w.exists[0] == 1 &&
              w.slots[4] == 0.0f && w.policy[c::policy_relax] == 0, "nothing written");
        Check(w.Flags() == flags_before, "P+0x204 untouched");
    }
    Check(std::strcmp(pp::RefusalName(pp::Refusal::none), "ok") == 0 &&
          std::strcmp(pp::RefusalName(pp::Refusal::sync_present), "sync_present") == 0, "refusal names");

    // --- Pure guard ordering.
    w.Reset();
    Check(Read(w, s) == pp::Refusal::none, "reset ready");
    Check(pp::CheckWrite(pp::Refusal::none, s, 1, false, pp::WriteKind::set) == nullptr, "allowed");
    Check(std::strcmp(pp::CheckWrite(pp::Refusal::none, s, 0, false, pp::WriteKind::set), "index") == 0, "index 0 (local)");
    Check(std::strcmp(pp::CheckWrite(pp::Refusal::none, s, 2, false, pp::WriteKind::set), "index") == 0, "index 2");
    Check(std::strcmp(pp::CheckWrite(pp::Refusal::none, s, 1, true, pp::WriteKind::set), "terminating") == 0, "terminating set");
    Check(pp::CheckWrite(pp::Refusal::none, s, 1, true, pp::WriteKind::restore) == nullptr, "terminating restore allowed");
    Check(std::strcmp(pp::CheckWrite(pp::Refusal::online_bit, s, 2, true, pp::WriteKind::set), "online_bit") == 0, "state first");
    {
        pp::State forged = s;
        forged.flags |= c::flag_online;
        Check(std::strcmp(pp::CheckWrite(pp::Refusal::none, forged, 1, false, pp::WriteKind::restore), "online_bit") == 0,
              "online bit re-asserted");
        forged = s; forged.sync = 1;
        Check(std::strcmp(pp::CheckWrite(pp::Refusal::none, forged, 1, false, pp::WriteKind::restore), "sync_present") == 0,
              "synchronizer re-asserted");
        forged = s; forged.count = 3;
        Check(std::strcmp(pp::CheckWrite(pp::Refusal::none, forged, 1, false, pp::WriteKind::set), "count") == 0, "count re-asserted");
        forged = s; forged.slots = 0;
        Check(std::strcmp(pp::CheckWrite(pp::Refusal::none, forged, 1, false, pp::WriteKind::set), "s_null") == 0, "slots re-asserted");
    }

    // --- Writes on a ready world.
    w.Reset();
    const std::uint32_t flags_before = w.Flags();
    std::uint32_t q194 = 0, q198 = 0;
    Check(pp::OrRealizeBits(w.addresses, 1, false, q194, q198) == nullptr && q194 == 3 && q198 == 3, "bits set, others kept");
    Check(Get<std::uint32_t>(w.Q, c::q_bits_a) == 3 && Get<std::uint32_t>(w.Q, c::q_bits_b) == 3, "bits in memory");
    Check(std::strcmp(pp::OrRealizeBits(w.addresses, 1, true, q194, q198), "terminating") == 0, "no set while terminating");
    Check(std::strcmp(pp::OrRealizeBits(w.addresses, 0, false, q194, q198), "index") == 0, "R must be 1");
    Check(pp::ClearRealizeBits(w.addresses, 1, true, q194, q198) == nullptr && q194 == 1 && q198 == 1, "clear while terminating");
    Put<std::uint32_t>(w.Q, c::q_bits_a, 0xF0F3);
    Check(pp::ClearRealizeBits(w.addresses, 1, false, q194, q198) == nullptr && q194 == 0xF0F1, "clear only bit 1");
    const float xyzw[4] = {12.5f, -3.0f, 400.25f, 1.25f};
    Check(pp::WriteSlot(w.addresses, 1, false, xyzw) == nullptr, "slot written");
    Check(w.slots[4] == 12.5f && w.slots[5] == -3.0f && w.slots[6] == 400.25f && w.slots[7] == 1.25f && w.slots[0] == 1.0f &&
          w.slots[3] == 0.5f, "slot 1 only");
    Check(std::strcmp(pp::WriteSlot(w.addresses, 1, true, xyzw), "terminating") == 0, "no slot while terminating");
    const float bad[4] = {NAN, 0, 0, 0};
    Check(std::strcmp(pp::WriteSlot(w.addresses, 1, false, bad), "non_finite") == 0, "non-finite refused");
    Put<std::uintptr_t>(w.S, c::s_pos, Addr(w.slots) + 4);
    Check(std::strcmp(pp::WriteSlot(w.addresses, 1, false, xyzw), "alignment") == 0, "misaligned slot refused");
    Put<std::uintptr_t>(w.S, c::s_pos, Addr(w.slots));
    Check(pp::ForceExists(w.addresses, 1, false) == nullptr && w.exists[0] == 0x03, "exists bit 1 forced, bit 0 kept");
    Check(std::strcmp(pp::ForceExists(w.addresses, 1, true), "terminating") == 0, "no force while terminating");
    std::uint8_t previous = 0xEE;
    Check(pp::SetPolicyRelax(w.addresses, true, 0, false, previous) == nullptr && previous == 0 && w.policy[c::policy_relax] == 1,
          "policy relaxed");
    Check(std::strcmp(pp::SetPolicyRelax(w.addresses, true, 0, true, previous), "terminating") == 0, "no relax while terminating");
    Check(pp::SetPolicyRelax(w.addresses, false, 0, true, previous) == nullptr && previous == 1 && w.policy[c::policy_relax] == 0,
          "policy restored while terminating");
    // The base-class vtable is only present while the policy object is being
    // constructed or destroyed (0x121b4b0): refused for relax and restore.
    Put<std::uintptr_t>(w.policy, 0, Addr(w.policy_base_vt));
    w.policy[c::policy_relax] = 1;
    Check(std::strcmp(pp::SetPolicyRelax(w.addresses, true, 0, false, previous), "policy_vtable") == 0, "base policy vtable refused");
    Check(std::strcmp(pp::SetPolicyRelax(w.addresses, false, 0, true, previous), "policy_vtable") == 0 &&
          w.policy[c::policy_relax] == 1, "no restore into a policy object under destruction");
    w.policy[c::policy_relax] = 0;
    Put<std::uintptr_t>(w.policy, 0, Addr(w.q_vt));
    Check(std::strcmp(pp::SetPolicyRelax(w.addresses, true, 0, false, previous), "policy_vtable") == 0, "foreign policy vtable");
    Put<std::uintptr_t>(w.policy, 0, Addr(w.policy_vt));
    w.policy_vt[2] = Addr(w.p_vt);
    Check(std::strcmp(pp::SetPolicyRelax(w.addresses, true, 0, false, previous), "policy_vtable") == 0, "policy slot +0x10 checked");
    w.policy_vt[2] = Addr(w.policy_function_code);
    Put<std::uintptr_t>(w.policy, c::policy_owner, Addr(w.Q));
    Check(std::strcmp(pp::SetPolicyRelax(w.addresses, true, 0, false, previous), "policy_owner") == 0, "policy owner checked");
    Put<std::uintptr_t>(w.policy, c::policy_owner, Addr(w.P));
    Put<std::uintptr_t>(w.realizer, 0, Addr(w.q_vt));
    Check(std::strcmp(pp::SetPolicyRelax(w.addresses, true, 0, false, previous), "realizer_vtable") == 0, "realizer vtable checked");
    Check(w.Flags() == flags_before, "no write ever touched P+0x204");

    // --- SEH: readable but not writable targets fault safely.
    w.Reset();
    DWORD old = 0;
    Check(VirtualProtect(w.Q, 0x1000, PAGE_READONLY, &old) != 0, "Q page read-only");
    Check(Read(w, s) == pp::Refusal::none, "read-only Q still reads");
    Check(std::strcmp(pp::OrRealizeBits(w.addresses, 1, false, q194, q198), "fault") == 0, "bits write fault contained");
    Check(VirtualProtect(w.exists, 0x1000, PAGE_READONLY, &old) != 0 && std::strcmp(pp::ForceExists(w.addresses, 1, false), "fault") == 0,
          "exists write fault contained");
    Check(VirtualProtect(w.slots, 0x1000, PAGE_READONLY, &old) != 0 && std::strcmp(pp::WriteSlot(w.addresses, 1, false, xyzw), "fault") == 0,
          "slot write fault contained");
    SetLastError(0x1234);
    pp::ForceExists(w.addresses, 1, false);
    Read(w, s);
    Check(GetLastError() == 0x1234, "last-error preserved");
    w.Reset();

    // --- Animation fields, per slot (u16 Q+0x18, u8 Q+0x28/0x30/0x38, u16 Q+0xD8).
    pp::Anim anim{};
    Check(Read(w, s) == pp::Refusal::none && pp::ReadAnim(s, anim), "anim read");
    Check(anim.act[0] == 4 && anim.act[1] == 1 && anim.q28[0] == 0x56 && anim.stance[0] == 2 && anim.stance[1] == 1 &&
          anim.speed[0] == 2 && anim.speed[1] == 0 && anim.qd8[0] == 0xBEEF && anim.qd8[1] == 0xFFFF, "per-slot anim arrays");
    const std::uint32_t zero_hash_0x40 = [] { std::uint32_t h = 2166136261u; for (int i = 0; i < 0x40; ++i) { h ^= 0; h *= 16777619u; } return h; }();
    Check(anim.m1d8 == zero_hash_0x40, "record hash");
    {
        const auto slot0 = pp::AnimSlot(anim, 0);
        Check(slot0.act == 4 && slot0.stance == 2 && slot0.speed == 2 && slot0.flags == mgscoop::sync::AnimFlag::Valid, "slot 0 sync form");
        pp::Anim odd = anim;
        odd.stance[1] = 0;
        const auto unknown = pp::AnimSlot(odd, 1);
        Check(unknown.act == 1 && unknown.stance == 0 && unknown.flags == 0, "stance 0 (unknown) is not valid");
        odd.stance[1] = 2; odd.speed[1] = 5;
        Check(pp::AnimSlot(odd, 1).flags == 0 && pp::AnimSlot(anim, 2).flags == 0, "speed 5 and index 2 invalid");
    }
    w.r1d8[5] = 1;
    pp::Anim changed{};
    Check(pp::ReadAnim(s, changed) && changed.m1d8 != anim.m1d8 && changed.m1e0 == anim.m1e0, "record hash changes");
    Put<std::uintptr_t>(w.Q, c::q_anim_28, 0);
    Check(!pp::ReadAnim(s, anim) && anim.act[0] == 0, "unreadable anim");
    w.Reset();

    // --- Looks (u8 Q+0x40/48/50/58, u16 Q+0x60, u8 Q+0x68), both slots.
    {
        pp::Looks looks{};
        Check(Read(w, s) == pp::Refusal::none && pp::ReadLooks(s, looks), "looks read");
        Check(mgscoop::partner::SameLook(looks.look[0], mgscoop::partner::Look{0, 1, 0, 1, 5, 2}) && looks.look[1].type == 0xFF &&
              looks.look[1].face == 0x7FFF, "slot 0 Snake, slot 1 unset");
        Put<std::uintptr_t>(w.Q, c::q_look_face, 0);
        Check(!pp::ReadLooks(s, looks), "unreadable face array");
        w.Reset();
    }

    // --- Writes of the slot-1 anim/look fields: guards, slot 1 only, SEH.
    {
        Check(pp::WriteAnim(w.addresses, 1, false, 6, 3, 2) == nullptr, "anim written");
        Check(Get<std::uint16_t>(w.a18, 2) == 6 && w.a30[1] == 3 && w.a38[1] == 2 && Get<std::uint16_t>(w.a18, 0) == 4 && w.a30[0] == 2 &&
              w.a38[0] == 2, "slot 1 only");
        Check(std::strcmp(pp::WriteAnim(w.addresses, 0, false, 1, 1, 0), "index") == 0, "never the local slot");
        Check(std::strcmp(pp::WriteAnim(w.addresses, 1, true, 1, 1, 0), "terminating") == 0, "not while terminating");
        Put<std::uint32_t>(w.P, c::p_flags, c::flag_valid | c::flag_online);
        Check(std::strcmp(pp::WriteAnim(w.addresses, 1, false, 1, 1, 0), "online_bit") == 0, "online bit refuses");
        Put<std::uint32_t>(w.P, c::p_flags, c::flag_valid);
        Put<std::uint32_t>(w.S, c::s_count, 3);
        Check(std::strcmp(pp::WriteAnim(w.addresses, 1, false, 1, 1, 0), "count") == 0 &&
              std::strcmp(pp::WriteFaceEquip(w.addresses, 1, false, 1), "count") == 0, "n must be 2");
        Put<std::uint32_t>(w.S, c::s_count, 2);
        Put<std::uintptr_t>(w.Q, c::q_anim_38, 0);
        Check(std::strcmp(pp::WriteAnim(w.addresses, 1, false, 1, 1, 0), "s_null") == 0, "null array refused");
        Put<std::uintptr_t>(w.Q, c::q_anim_38, Addr(w.a38));
        Put<std::uintptr_t>(w.Q, c::q_anim_18, Addr(w.a18) + 1);
        Check(std::strcmp(pp::WriteAnim(w.addresses, 1, false, 1, 1, 0), "alignment") == 0, "odd u16 array refused");
        Put<std::uintptr_t>(w.Q, c::q_anim_18, Addr(w.a18));
        Check(pp::WriteFaceEquip(w.addresses, 1, false, 9) == nullptr && w.le[1] == 9 && w.le[0] == 2, "face_equip slot 1 only");
        DWORD old_protect = 0;
        Check(VirtualProtect(w.S, 0x1000, PAGE_READONLY, &old_protect) != 0, "S page read-only");
        Check(std::strcmp(pp::WriteAnim(w.addresses, 1, false, 1, 1, 0), "fault") == 0 &&
              std::strcmp(pp::WriteFaceEquip(w.addresses, 1, false, 1), "fault") == 0, "array write faults contained");
        w.Reset();
    }

    // --- Component census (pure reads) on the synthetic blocks.
    g_world = &w;
    {
        w.RealizePartner();
        Check(Read(w, s) == pp::Refusal::none, "census state");
        pp::Census census{};
        Check(pp::ResolveCensus(w.addresses, s, census) == nullptr && census.refusal == nullptr, "census passes");
        const pp::Block& b1 = census.k[1];
        Check(b1.id == 1 && b1.base_id == 1 && b1.id_count == 1 && b1.blk == Addr(w.blocks + c::block_stride) && b1.ps == Addr(w.ps[1]) &&
              b1.cs == Addr(w.cs) && b1.t == Addr(w.tobj[1]) + c::transform_view && b1.t68 == Fn(&FakeSetPose) &&
              b1.t70 == Fn(&FakeSetRot) && b1.a080 == Fn(&FakeResetA0) && b1.c090 == Fn(&FakeResetC0) && b1.psw == 0x11, "block 1 chain");
        Check(census.k[0].t == Addr(w.tobj[0]) + c::transform_view && census.equip_vt == Addr(w.equip_vt), "block 0 and equipment");
        char line[pp::census_capacity]{};
        Check(pp::FormatCensus(w.addresses, census, 1, line, sizeof(line)) && Contains(line, "partner_census; k=1; id=1; base=1; count=1; blk=ok;") &&
              Contains(line, "; t=set;") && Contains(line, "; stance=ok; psw=0x11;") &&
              Contains(line, "same_t=1; same_a0=1; same_c0=1; verdict=ok\r\n") && census.k[1].stance_refusal == nullptr, "census line");
        pp::Addresses based = w.addresses;
        based.image_base = Addr(w.block_vt) - 0x100;
        Check(pp::FormatCensus(based, census, 0, line, sizeof(line)) && Contains(line, "blk_vt=0x100;"), "census RVAs relative to the image base");
        char tiny[8]{};
        Check(pp::FormatCensus(w.addresses, census, 0, tiny, sizeof(tiny)) == 0 && tiny[0] == 0 &&
              pp::FormatCensus(w.addresses, census, 2, line, sizeof(line)) == 0, "census format refusals");
        // Every identity, in chain order.
        struct CensusCase { const char* name; void (*mutate)(World&); const char* reason; };
        const CensusCase census_cases[] = {
            {"unrealized", [](World& v) { v.pool[1] = 0xFF; }, "unrealized"},
            {"no blocks", [](World& v) { Put<std::uintptr_t>(v.P, c::p_blocks, 0); }, "blk_vtable"},
            {"block vtable", [](World& v) { Put<std::uintptr_t>(v.blocks + c::block_stride, 0, Addr(v.ps_vt)); }, "blk_vtable"},
            {"table entry", [](World& v) { Put<std::uintptr_t>(v.table, 8, Addr(v.blocks)); }, "blk_owner"},
            {"block owner", [](World& v) { Put<std::uintptr_t>(v.blocks + c::block_stride, c::blk_owner, Addr(v.Q)); }, "blk_owner"},
            {"block range", [](World& v) { Put<std::uint32_t>(v.blocks + c::block_stride, c::blk_base_id, 5); }, "blk_index"},
            {"PS null", [](World& v) { Put<std::uintptr_t>(v.blocks + c::block_stride, c::blk_ps, 0); }, "ps_vtable"},
            {"PS vtable", [](World& v) { Put<std::uintptr_t>(v.ps[1], 0, Addr(v.cs_vt)); }, "ps_vtable"},
            {"PS slot 0", [](World& v) { v.ps_vt[0] = Fn(&FakeNever); }, "ps_vtable"},
            {"PS back pointer", [](World& v) { Put<std::uintptr_t>(v.ps[1], c::ps_blk, Addr(v.blocks)); }, "ps_back"},
            {"PS base id", [](World& v) { Put<std::uint32_t>(v.ps[1], c::ps_base_id, 0); }, "ps_back"},
            {"CS vtable", [](World& v) { Put<std::uintptr_t>(v.cs, 0, Addr(v.ps_vt)); }, "cs_vtable"},
            {"CS slot", [](World& v) { v.cs_vt[0x388 / 8] = Fn(&FakeSetPose); }, "cs_vtable"},
            {"CS range", [](World& v) { Put<std::uint32_t>(v.cs, 8, 1); }, "entry"},
            {"T null", [](World& v) { Put<std::uintptr_t>(v.entries, c::cs_entry_stride + c::cs_entry_transform, 0); }, "t_null"},
            {"T vtable", [](World& v) { Put<std::uintptr_t>(v.tobj[1], c::transform_view, 0); }, "t_class"},
            {"T other class", [](World& v) { Put<std::uintptr_t>(v.tobj[1], c::transform_view, Addr(v.t_vt2)); }, "t_class"},
            {"T slot", [](World& v) { v.t_vt[0x68 / 8] = 0; }, "t_class"},
            {"local block", [](World& v) { Put<std::uintptr_t>(v.blocks, 0, 0); }, "blk_vtable"},
        };
        for (const CensusCase& test : census_cases) {
            w.Reset();
            w.RealizePartner();
            test.mutate(w);
            Check(Read(w, s) == pp::Refusal::none, "census case state");
            Check(pp::ResolveCensus(w.addresses, s, census) && std::strcmp(census.refusal, test.reason) == 0, test.name);
        }
        // The SetStance prerequisites are a separate verdict: the transform
        // chain (xform/hybrid, ReadBody) does not need them.
        struct StanceCase { const char* name; void (*mutate)(World&); const char* reason; };
        const StanceCase stance_cases[] = {
            {"stance words null", [](World& v) { Put<std::uintptr_t>(v.ps[1], c::ps_stance, 0); }, "ps_words"},
            {"sub-object null", [](World& v) { Put<std::uintptr_t>(v.ps[1], c::ps_sub260, 0); }, "ps_sub"},
        };
        for (const StanceCase& test : stance_cases) {
            w.Reset();
            w.RealizePartner();
            test.mutate(w);
            Check(Read(w, s) == pp::Refusal::none, "stance case state");
            Check(pp::ResolveCensus(w.addresses, s, census) == nullptr && census.k[1].refusal == nullptr &&
                  census.k[1].stance_refusal && std::strcmp(census.k[1].stance_refusal, test.reason) == 0 && census.k[0].stance_refusal == nullptr,
                  test.name);
            Check(pp::FormatCensus(w.addresses, census, 1, line, sizeof(line)) && Contains(line, "; stance=") && Contains(line, test.reason) &&
                  Contains(line, "verdict=ok\r\n"), "census logs the stance verdict separately");
            const float pos[4] = {1.0f, 0.0f, 2.0f, 0.0f};
            const float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            g_calls = CallLog{};
            Check(pp::ApplyTransform(w.addresses, 1, false, pos, quat, mgscoop::PartnerReset::none) == nullptr && g_calls.pose == 1,
                  "xform still runs without the stance prerequisites");
            float body[4]{}, rot[4]{};
            Check(pp::ReadBody(w.addresses, s, 1, body, rot), "body readback without the stance prerequisites");
            std::uint8_t psw = 0, q30 = 0;
            Check(std::strcmp(pp::SetStance(w.addresses, 1, false, 2, psw, q30), test.reason) == 0 && g_calls.stance == 0,
                  "SetStance refused without its prerequisites");
            Check(std::strcmp(pp::RequestStance(w.addresses, 1, false, 2), test.reason) == 0 && g_calls.request == 0,
                  "stance request refused without the prerequisites");
        }
        // A broken identity refuses both verdicts.
        w.Reset();
        w.RealizePartner();
        Put<std::uintptr_t>(w.ps[1], c::ps_blk, Addr(w.blocks));
        Check(Read(w, s) == pp::Refusal::none && pp::ResolveCensus(w.addresses, s, census) &&
              std::strcmp(census.k[1].stance_refusal, "ps_back") == 0 && std::strcmp(census.k[1].refusal, "ps_back") == 0,
              "identity refusal applies to both verdicts");
        // ids[1] == 1 (instance index == body id) is required.
        w.Reset();
        w.RealizePartner();
        w.ids[1] = 2;
        Put<std::uint32_t>(w.blocks + c::block_stride, c::blk_base_id, 2);
        Put<std::uint32_t>(w.ps[1], c::ps_base_id, 2);
        Put<std::uint32_t>(w.cs, 8, 3);
        Put<std::uintptr_t>(w.entries, 2 * c::cs_entry_stride + c::cs_entry_transform, Addr(w.tobj[1]));
        Check(Read(w, s) == pp::Refusal::none && pp::ResolveCensus(w.addresses, s, census) &&
              std::strcmp(census.refusal, "id_mismatch") == 0, "ids[1] must be 1");
        // Calls need targets inside the code range and (once pinned) the pinned vtable.
        w.Reset();
        w.RealizePartner();
        Check(Read(w, s) == pp::Refusal::none, "pin state");
        pp::Addresses narrow = w.addresses;
        narrow.code_end = 2;
        Check(pp::ResolveCensus(narrow, s, census) && std::strcmp(census.refusal, "t_class") == 0, "targets outside the code range");
        pp::Addresses pinned = w.addresses;
        pinned.t_vtable = Addr(w.t_vt2);
        Check(pp::ResolveCensus(pinned, s, census) && std::strcmp(census.refusal, "t_class") == 0, "another pinned T vtable");
        pinned.t_vtable = Addr(w.t_vt);
        Check(pp::ResolveCensus(pinned, s, census) == nullptr, "the pinned T vtable");
    }

    // --- Native puppet reads (pure): identity, instance 1's look slot, the
    // sender's look source and the gun node (docs/native-puppet.md).
    {
        w.Reset();
        w.RealizePartner();
        Check(Read(w, s) == pp::Refusal::none, "native state");
        unsigned char* roots = w.region + 0x17000;
        unsigned char* looks = w.region + 0x17100;
        unsigned char* gor = w.region + 0x17200;   // GO primary view; P' = GO + 0x20
        unsigned char* gun = w.region + 0x17400;   // node, iface/prim, record, bitmap
        static std::uintptr_t go_vt[16]{}, node_vt[4]{}, prim_vt[4]{}, iface_vt[4]{};
        Put<std::uint32_t>(roots, c::node_base, 0); Put<std::uint32_t>(roots, c::node_count, 1);
        Put<std::uint32_t>(roots + 0x40, c::node_base, 1); Put<std::uint32_t>(roots + 0x40, c::node_count, 1);
        for (unsigned k = 0; k < 2; ++k) {
            Put<std::uintptr_t>(w.ps[k], c::ps_phase_comp, Addr(w.ps_vt) + c::ps_phase_comp_vt_delta);
            Put<std::uintptr_t>(w.ps[k], c::ps_look, Addr(looks + 0x10 * k));
            Put<std::uintptr_t>(w.blocks + k * c::block_stride, c::blk_root, Addr(roots + 0x40 * k));
        }
        const float look0[2] = {-0.25f, 1.75f};
        std::memcpy(looks, look0, sizeof(look0));
        Put<std::uintptr_t>(gor, 0, Addr(go_vt));
        pp::Addresses a = w.addresses;
        a.go_vtable = Addr(go_vt);
        a.gun_node_vtable = Addr(node_vt);
        a.gun_prim_vtable = Addr(prim_vt);
        a.gun_iface_vtable = Addr(iface_vt);
        pp::State s2 = s;
        s2.P = Addr(gor) + c::go_to_p;
        pp::InputTargets t{};
        t.blk0 = Addr(w.blocks); t.blk1 = Addr(w.blocks + c::block_stride);
        t.root0 = Addr(roots); t.root1 = Addr(roots + 0x40);
        pp::NativeTargets n{};
        Check(pp::ResolveNative(a, s2, t, n) == nullptr && !n.refusal && n.go == Addr(gor) && n.go_vt == Addr(go_vt) && n.pool0 == 0 &&
              n.pool1 == 1 && n.ps0 == Addr(w.ps[0]) && n.ps1 == Addr(w.ps[1]) && n.ps1_look == Addr(looks + 0x10) &&
              n.ps1_rec == Addr(w.rec280 + c::ps_rec280_stride) && n.ps1_base == 1 && n.ps1_idx == 0,
              "native identity (GO = P - 0x20, pools) and instance 1's look slot");
        pp::InputTargets tr = t;
        tr.refusal = "parts";
        Check(std::strcmp(pp::ResolveNative(a, s2, tr, n), "parts") == 0 && n.refusal && !n.go, "an input-target refusal passes through");
        Check(std::strcmp(pp::ResolveNative(a, s, t, n), "go_vtable") == 0, "GO must carry the primary vtable");
        Put<std::uintptr_t>(w.ps[1], c::ps_phase_comp, Addr(w.ps_vt) + 0x1D8);
        Check(pp::ResolveNative(a, s2, t, n) == nullptr && !n.refusal && std::strcmp(n.look_refusal, "ps_comp") == 0,
              "a wrong phase component refuses only the look slot");
        Put<std::uintptr_t>(w.ps[1], c::ps_phase_comp, Addr(w.ps_vt) + c::ps_phase_comp_vt_delta);
        Put<std::uint32_t>(roots + 0x40, c::node_base, 2);
        Check(pp::ResolveNative(a, s2, t, n) == nullptr && std::strcmp(n.look_refusal, "ps_base") == 0, "PS1+8 must equal root1+0x24");
        Put<std::uint32_t>(roots + 0x40, c::node_base, 1);
        Put<std::uint32_t>(roots + 0x40, c::node_count, 0);
        Check(pp::ResolveNative(a, s2, t, n) == nullptr && std::strcmp(n.look_refusal, "ps_range") == 0, "id 1 must be in root1's range");
        Put<std::uint32_t>(roots + 0x40, c::node_count, 1);
        Put<std::uintptr_t>(w.ps[1], c::ps_look, 0);
        Check(pp::ResolveNative(a, s2, t, n) == nullptr && std::strcmp(n.look_refusal, "ps_arrays") == 0, "null look array refused");
        Put<std::uintptr_t>(w.ps[1], c::ps_look, Addr(looks + 0x10));
        Put<std::uintptr_t>(w.blocks + c::block_stride, c::blk_ps, Addr(w.ps[0]));
        Check(pp::ResolveNative(a, s2, t, n) == nullptr && n.look_refusal, "PS1 == PS0 (or its base mismatch) refused");
        Put<std::uintptr_t>(w.blocks + c::block_stride, c::blk_ps, Addr(w.ps[1]));
        // Sender look source (local 0).
        pp::LookSource src{};
        Check(pp::ReadLookSource(a, s, src) && src.look_ok && src.pitch == -0.25f && src.yaw == 1.75f && !src.cam_ok && !src.heading_ok,
              "sender look angles from the local PS slot");
        // Gun node of instance 1.
        unsigned char* node = gun;
        unsigned char* prim = gun + 0x100;
        unsigned char* rec = gun + 0x200;
        unsigned char* bitmap = gun + 0x180;
        Put<std::uintptr_t>(node, 0, Addr(node_vt));
        Put<std::uint32_t>(node, 0x20, 1); Put<std::uint32_t>(node, c::gun_node_body, 1); Put<std::uint32_t>(node, c::gun_node_row, 0x3D);
        Put<std::uintptr_t>(node, c::gun_node_bitmap, Addr(bitmap));
        Put<std::uint32_t>(bitmap, 0, 1);
        Put<std::uintptr_t>(prim, 0, Addr(prim_vt));
        Put<std::uintptr_t>(prim, c::prim_blk, Addr(w.blocks + c::block_stride));
        Put<std::uintptr_t>(prim, c::prim_node, Addr(node));
        Put<std::uintptr_t>(prim, c::prim_gun_rec, Addr(rec));
        Put<std::uintptr_t>(prim, c::gun_iface_prim, Addr(iface_vt));
        Put<std::uint32_t>(rec, c::gun_flags, 0x40004u);
        rec[c::gun_mode] = 2;
        Put<std::uintptr_t>(w.blocks + c::block_stride, c::blk_gun_node, Addr(node));
        Put<std::uintptr_t>(w.blocks + c::block_stride, c::blk_gun_iface, Addr(prim) + c::gun_iface_prim);
        pp::Gun1 g{};
        Check(pp::ReadGun1(a, s, g) && !g.refusal && g.node_active && g.gun3c0 == 0x40004u && g.mode == 2, "gun node, record and bitmap");
        Put<std::uint32_t>(node, c::gun_node_row, 0x3C);
        Check(!pp::ReadGun1(a, s, g) && std::strcmp(g.refusal, "gun_node") == 0, "wrong builder row refused");
        Put<std::uint32_t>(node, c::gun_node_row, 0x3D);
        Put<std::uintptr_t>(prim, c::prim_node, Addr(rec));
        Check(!pp::ReadGun1(a, s, g) && std::strcmp(g.refusal, "gun_prim") == 0, "prim+0x38 must be the node");
        w.Reset();
    }

    // --- Native calls on the synthetic world (fake targets record the calls).
    {
        w.Reset();
        w.RealizePartner();
        const float pos[4] = {1.5f, -2.0f, 3.25f, 0.0f};
        const float quat[4] = {0.0f, 0.5f, 0.0f, 0.8660254f};
        Check(pp::ApplyTransform(w.addresses, 1, false, pos, quat, mgscoop::PartnerReset::none) == nullptr, "transform applied");
        Check(g_calls.rot == 1 && g_calls.pose == 1 && g_calls.rot_at < g_calls.pose_at && g_calls.reset_a0 == 0 && g_calls.reset_c0 == 0,
              "T.vt+0x70 then T.vt+0x68, no resets");
        Check(g_calls.rot_this == Addr(w.tobj[1]) + c::transform_view && g_calls.pose_this == g_calls.rot_this, "called on T1");
        Check(g_calls.pos_addr % 16 == 0 && g_calls.quat_addr % 16 == 0 && g_calls.quat_addr != 1, "16-byte aligned vectors, one quaternion");
        Check(std::memcmp(g_calls.pos, pos, sizeof(pos)) == 0 && std::memcmp(g_calls.quat, quat, sizeof(quat)) == 0, "argument values");
        g_calls = CallLog{};
        Check(pp::ApplyTransform(w.addresses, 1, false, pos, quat, mgscoop::PartnerReset::ac) == nullptr && g_calls.reset_c0 == 1 &&
              g_calls.reset_a0 == 1 && g_calls.pose_at < g_calls.c0_at && g_calls.c0_at < g_calls.a0_at && g_calls.c0_id == 1 &&
              g_calls.a0_id == 1 && g_calls.c0_this == Addr(w.c0[1]) && g_calls.a0_this == Addr(w.a0[1]), "resets in Warp's order (C0, A0)");
        g_calls = CallLog{};
        Check(pp::ApplyTransform(w.addresses, 1, false, pos, quat, mgscoop::PartnerReset::a) == nullptr && g_calls.reset_a0 == 1 &&
              g_calls.reset_c0 == 0, "reset=a");
        // Refusals: nothing is called.
        g_calls = CallLog{};
        Check(std::strcmp(pp::ApplyTransform(w.addresses, 1, true, pos, quat, mgscoop::PartnerReset::none), "terminating") == 0 &&
              std::strcmp(pp::ApplyTransform(w.addresses, 0, false, pos, quat, mgscoop::PartnerReset::none), "index") == 0, "guards");
        const float nan4[4] = {NAN, 0, 0, 0};
        Check(std::strcmp(pp::ApplyTransform(w.addresses, 1, false, nan4, quat, mgscoop::PartnerReset::none), "non_finite") == 0 &&
              std::strcmp(pp::ApplyTransform(w.addresses, 1, false, pos, nan4, mgscoop::PartnerReset::none), "non_finite") == 0, "non-finite");
        w.parts[1] = 1;
        Check(std::strcmp(pp::ApplyTransform(w.addresses, 1, false, pos, quat, mgscoop::PartnerReset::none), "parts") == 0, "parts not loaded");
        w.parts[1] = 3;
        Put<std::uintptr_t>(w.blocks + c::block_stride, c::blk_c0, Addr(w.a0[1]));  // C0 now has A0's class
        Check(std::strcmp(pp::ApplyTransform(w.addresses, 1, false, pos, quat, mgscoop::PartnerReset::c), "c0_class") == 0, "C0 class differs");
        Put<std::uintptr_t>(w.blocks + c::block_stride, c::blk_c0, Addr(w.c0[1]));
        Put<std::uintptr_t>(w.blocks + c::block_stride, c::blk_a0, 0);
        Check(std::strcmp(pp::ApplyTransform(w.addresses, 1, false, pos, quat, mgscoop::PartnerReset::a), "a0_class") == 0, "A0 missing");
        Check(g_calls.rot == 0 && g_calls.pose == 0, "no call on any refusal");
        Put<std::uintptr_t>(w.blocks + c::block_stride, c::blk_a0, Addr(w.a0[1]));
        Put<std::uint32_t>(w.P, c::p_flags, c::flag_valid | c::flag_online);
        Check(std::strcmp(pp::ApplyTransform(w.addresses, 1, false, pos, quat, mgscoop::PartnerReset::none), "online_bit") == 0 &&
              g_calls.rot == 0, "online bit: no call");
        Put<std::uint32_t>(w.P, c::p_flags, c::flag_valid);
        // A faulting target is contained.
        w.t_vt[0x70 / 8] = Fn(&FakeFault);
        Check(std::strcmp(pp::ApplyTransform(w.addresses, 1, false, pos, quat, mgscoop::PartnerReset::none), "fault") == 0, "call fault contained");
        w.t_vt[0x70 / 8] = Fn(&FakeSetRot);
        // Round 4: the position-only set (T.vt+0x58) for nudges.
        g_calls = CallLog{};
        const float npos[4] = {7.5f, 1.0f, -2.5f, 0.0f};
        Check(pp::ApplyPosition(w.addresses, 1, false, npos) == nullptr && g_calls.setpos == 1 && g_calls.rot == 0 && g_calls.pose == 0,
              "ApplyPosition: T.vt+0x58 only (no rotation request, no set pose)");
        Check(g_calls.setpos_this == Addr(w.tobj[1]) + c::transform_view && g_calls.setpos_addr % 16 == 0 &&
              std::memcmp(g_calls.setpos_pos, npos, sizeof(npos)) == 0, "on T1 with an aligned position");
        const float nanp[4] = {NAN, 0, 0, 0};
        Check(std::strcmp(pp::ApplyPosition(w.addresses, 1, true, npos), "terminating") == 0 &&
              std::strcmp(pp::ApplyPosition(w.addresses, 0, false, npos), "index") == 0 &&
              std::strcmp(pp::ApplyPosition(w.addresses, 1, false, nanp), "non_finite") == 0 && g_calls.setpos == 1, "ApplyPosition guards");
        w.t_vt[0x58 / 8] = 0;
        Check(pp::ApplyPosition(w.addresses, 1, false, npos) && g_calls.setpos == 1, "slot +0x58 outside the code range: refused");
        w.t_vt[0x58 / 8] = Fn(&FakeSetPos);
        // Body readback (T+0x20, T+0x60).
        Check(Read(w, s) == pp::Refusal::none, "body state");
        float velocity[4]{};
        Check(pp::ReadVelocity(w.addresses, s, 1, velocity) && velocity[0] == 0.6f && velocity[2] == 0.8f, "PS+0x268 velocity of instance 1");
        Put<std::uintptr_t>(w.ps[1], c::ps_vel268, 0);
        Check(!pp::ReadVelocity(w.addresses, s, 1, velocity), "no velocity array");
        Put<std::uintptr_t>(w.ps[1], c::ps_vel268, Addr(w.vel + 0x20));
        float body[4]{}, rot[4]{};
        Check(pp::ReadBody(w.addresses, s, 1, body, rot) && body[0] == 4.0f && body[2] == 6.0f && rot[1] == 0.7071068f, "body readback");
        w.ids[1] = 0xFFFF;
        Check(Read(w, s) == pp::Refusal::none && !pp::ReadBody(w.addresses, s, 1, body, rot), "no body while unrealized");
    }
    {
        // SetStance through the pinned 0xFF5B10 (here: the fake), with readback.
        w.Reset();
        w.RealizePartner();
        std::uint8_t psw = 0, q30 = 0;
        Check(pp::SetStance(w.addresses, 1, false, 2, psw, q30) == nullptr && g_calls.stance == 1 && g_calls.stance_this == Addr(w.ps[1]) &&
              g_calls.stance_id == 1 && g_calls.stance_value == 2, "SetStance(PS1, 1, 2)");
        Check(psw == 0x22 && q30 == 2 && w.a30[0] == 2, "stance word and Q+0x30[1] read back");
        Check(std::strcmp(pp::SetStance(w.addresses, 1, false, 0, psw, q30), "stance") == 0 &&
              std::strcmp(pp::SetStance(w.addresses, 1, false, 4, psw, q30), "stance") == 0, "stance 1..3 only");
        Check(std::strcmp(pp::SetStance(w.addresses, 0, false, 1, psw, q30), "index") == 0 &&
              std::strcmp(pp::SetStance(w.addresses, 1, true, 1, psw, q30), "terminating") == 0, "stance guards");
        Put<std::uintptr_t>(w.sub, 0, 0);
        Check(std::strcmp(pp::SetStance(w.addresses, 1, false, 1, psw, q30), "ps_sub") == 0, "sub-object vtable required");
        Put<std::uintptr_t>(w.sub, 0, Addr(w.sub_vt));
        pp::Addresses wrong = w.addresses;
        wrong.set_stance = Fn(&FakeNever);
        Check(std::strcmp(pp::SetStance(wrong, 1, false, 1, psw, q30), "ps_vtable") == 0, "PS slot 0 must be the pinned SetStance");
        Check(g_calls.stance == 1, "no further SetStance on refusals");
        // Stance request (aset=r): X = PS1.vt+0xC8(), X.vt+0x10(X, 1, s).
        Check(pp::RequestStance(w.addresses, 1, false, 3) == nullptr && g_calls.request == 1 && g_calls.request_this == Addr(w.xc[1]) &&
              g_calls.request_index == 1 && g_calls.request_value == 3, "stance request");
        Put<std::uintptr_t>(w.xc[1], 0, Addr(w.x_vt2));
        Check(std::strcmp(pp::RequestStance(w.addresses, 1, false, 3), "x_class") == 0, "X class must match the local's");
        Put<std::uintptr_t>(w.xc[1], 0, Addr(w.x_vt));
        Put<std::uintptr_t>(w.inner, c::comp_object, 0);
        Check(std::strcmp(pp::RequestStance(w.addresses, 1, false, 3), "x_null") == 0 && g_calls.request == 1, "component chain read first");
        Put<std::uintptr_t>(w.inner, c::comp_object, Addr(w.object));
        Check(std::strcmp(pp::RequestStance(w.addresses, 1, false, 9), "stance") == 0, "request stance bound");
        // probe=ps line.
        Check(Read(w, s) == pp::Refusal::none, "ps probe state");
        char ps_line[512]{};
        Check(pp::FormatPsProbe(w.addresses, s, 1, ps_line, sizeof(ps_line)) &&
              Contains(ps_line, "partner_ps; k=1; id=1; mask740=0x11; r738_milli=250,-1500,0,0,0,0,0,0,0,0; ") &&
              Contains(ps_line, "b2d0=7; b2d8=9; psw=0x"), "ps probe line");
        w.pool[1] = 0xFF;
        Check(Read(w, s) == pp::Refusal::none && pp::FormatPsProbe(w.addresses, s, 1, ps_line, sizeof(ps_line)) &&
              std::strcmp(ps_line, "partner_ps; k=1; refused=unrealized\r\n") == 0, "ps probe refusal");
    }
    g_world = nullptr;
    w.Reset();

    // --- Formatting.
    Check(Read(w, s) == pp::Refusal::none, "format state");
    char line[pp::probe_capacity]{};
    Check(pp::FormatReady(s, line, sizeof(line)) && std::strcmp(line,
          "partner_cfg=ready; flags=0x00002001; count=2; realized_count=2; local=0; R=1; online_bit=0; sync=null; code_gate=ok\r\n") == 0,
          "ready line");
    w.slots[4] = 1.5f; w.slots[5] = -0.25f; w.slots[6] = 3.0f; w.slots[7] = -1.5708f;
    Check(Read(w, s) == pp::Refusal::none && pp::FormatProbe(s, 123456, 3, line, sizeof(line)), "probe formats");
    Check(std::strncmp(line, "partner_probe=state; t_ms=123456; stage=3; ids=0000,ffff; pool=00,ff; parts=3,0; rbits=0x1; s0=0x01; s8=0x01; "
                             "q170=0x3; q180=0x0; q194=0x1; q198=0x1; glob=0; pb_guard=1; pb_flag=0,5; pb_state=3,1; "
                             "pos0_mm=1000,2000,3000; yaw0_mrad=500; pos1_mm=1500,-250,3000; yaw1_mrad=-1571; rec1_mm=7000,8000,9000; "
                             "rec1_status=0xa5a5a5a5; req1=none\r\n", 400) == 0, "probe text");
    Check(Contains(line, "rec1_status=0xa5a5a5a5; req1=none\r\n") && std::strlen(line) < pp::probe_capacity, "probe tail");
    char small[16]{};
    Check(pp::FormatProbe(s, 1, 0, small, sizeof(small)) == 0 && small[0] == 0, "probe too small");
    Check(pp::ToMm(1.5f) == 1500 && pp::ToMm(-0.0005f) == -1 && pp::ToMm(NAN) == INT32_MIN && pp::ToMm(1e12f) == INT32_MAX &&
          pp::ToMm(-1e12f) == -INT32_MAX, "ToMm");

    // --- Code gate on a synthetic image (detail seam).
    {
        unsigned char image[0x200]{};
        const unsigned char code[4] = {0x48, 0x8b, 0xc1, 0xc3};
        std::memcpy(image + 0x40, code, sizeof(code));
        Put<std::uintptr_t>(image, 0x100, Addr(image) + 0x40);
        const pp::detail::GateSite sites[] = {{0x40, 4, code}};
        const pp::detail::GateSlot slots[] = {{0x100, 0, 0x40}};
        std::uintptr_t mismatch = 0xFFFF;
        Check(pp::detail::CodeGateWith(image, sizeof(image), sites, 1, slots, 1, mismatch) && mismatch == 0, "synthetic gate ok");
        image[0x42] ^= 1;
        Check(!pp::detail::CodeGateWith(image, sizeof(image), sites, 1, slots, 1, mismatch) && mismatch == 0x40, "byte mismatch");
        image[0x42] ^= 1;
        Put<std::uintptr_t>(image, 0x100, Addr(image) + 0x44);
        Check(!pp::detail::CodeGateWith(image, sizeof(image), sites, 1, slots, 1, mismatch) && mismatch == 0x100, "slot mismatch");
        const pp::detail::GateSite outside[] = {{0x1FE, 4, code}};
        Check(!pp::detail::CodeGateWith(image, sizeof(image), outside, 1, nullptr, 0, mismatch) && mismatch == 0x1FE, "site outside span");
        Check(!pp::detail::CodeGateWith(nullptr, 0, nullptr, 0, nullptr, 0, mismatch), "null image");
    }
    // --- The real contract laid into a reserved image of the game's span.
    {
        std::size_t span = 0;
        for (const auto& site : c::code_sites) if (site.rva + site.size > span) span = site.rva + site.size;
        for (const auto& slot : c::vtable_slots) if (slot.vtable_rva + slot.offset + 8 > span) span = slot.vtable_rva + slot.offset + 8;
        span = (span + 0xFFFF) & ~static_cast<std::size_t>(0xFFFF);
        auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(image != nullptr, "image reserved");
        for (const auto& site : c::code_sites) std::memcpy(image + site.rva, site.bytes, site.size);
        for (const auto& slot : c::vtable_slots) Put<std::uintptr_t>(image, slot.vtable_rva + slot.offset, Addr(image) + slot.target_rva);
        std::uintptr_t mismatch = 1;
        Check(pp::CodeGate(image, span, mismatch) && mismatch == 0, "contract gate accepts the verified bytes");
        Check(!pp::CodeGate(image, c::code_sites[0].rva, mismatch), "contract gate refuses a short span");
        image[0x12cc63a] ^= 0xFF;  // inside the all-active branch site 0x12cc633
        Check(!pp::CodeGate(image, span, mismatch) && mismatch == 0x12cc633, "contract gate names the changed site");
        image[0x12cc63a] ^= 0xFF;
        Put<std::uintptr_t>(image, 0x2295490 + 0x1b0, Addr(image) + 0x9ceae1);
        Check(!pp::CodeGate(image, span, mismatch) && mismatch == 0x2295490, "contract gate checks the Warp slot");
        const pp::Addresses real = pp::AddressesFor(image);
        Check(real.player_slot == Addr(image) + 0x2a82c30 && real.policy_vtable == Addr(image) + 0x23ca1b8 &&
              real.mirror_object == Addr(image) + 0x2c2ef80 && real.policy_function == Addr(image) + 0x121b5e0 &&
              real.glob_suppress == Addr(image) + 0x2c1b0f0 && real.record_base == Addr(image) + 0x2bd3ff0 &&
              real.vars_slot == Addr(image) + 0x2c16920, "contract addresses");
        // The empty slot in an image without a player is a refusal, not a fault.
        Check(pp::ReadState(real, s) == pp::Refusal::player_absent, "no player in a blank image");
        VirtualFree(image, 0, MEM_RELEASE);
        Check(pp::AddressesFor(nullptr).player_slot == 0, "null base");
    }

    // --- ReadPad (the isend sender): PP/src chain and the exact PP vtable.
    {
        w.Reset();
        unsigned char* pad = w.region + 0x17000;   // PP (0x4E0 bytes)
        unsigned char* src = w.region + 0x17800;   // raw pad device
        static std::uintptr_t pad_vt[43]{};
        static std::uintptr_t copy_vt[43]{};
        w.addresses.pp_vtable = Addr(pad_vt);
        Put<std::uintptr_t>(w.P, c::p_pad, Addr(pad));
        Put<std::uintptr_t>(pad, 0, Addr(pad_vt));
        Put<std::uintptr_t>(pad, c::pp_owner, Addr(w.P));
        Put<std::uintptr_t>(pad, c::pp_src, Addr(src));
        Put<std::uint32_t>(src, 0x48, 0x4);
        Put<float>(src, 0x5C, -1.0f);
        Put<float>(pad, 0x10, 1.0f);
        Put<std::uint32_t>(pad, 0x28, 0x10);
        Put<std::uint8_t>(pad, c::pp_kb_flags, 0x10);
        Check(Read(w, s) == pp::Refusal::none, "pad: state ready");
        mgscoop::partner_input::PadRaw raw{};
        Check(pp::ReadPad(w.addresses, s, raw) && raw.src_w[0] == 0x4 && raw.src_f[1] == -1.0f && raw.pp_derived[0] == 1.0f &&
              raw.pp_mask[0] == 0x10 && raw.kb_flags == 0x10, "pad: words, axes, derived, masks and flags read");
        // A vtable other than the image's (the hook's per-window copy) means a
        // window was open across the read: refused, never accepted.
        Put<std::uintptr_t>(pad, 0, Addr(&copy_vt[1]));
        Check(!pp::ReadPad(w.addresses, s, raw), "pad: the hook's vtable copy is refused");
        Put<std::uintptr_t>(pad, 0, Addr(pad_vt));
        Put<std::uintptr_t>(pad, c::pp_owner, 0);
        Check(!pp::ReadPad(w.addresses, s, raw), "pad: owner mismatch refused");
        // The DM probe line carries the camera connect fields.
        pp::DmState dm{};
        dm.cam_ok = true;
        dm.cam1f0 = 0x8800000000001234ull;
        dm.cam200 = 1;
        char dm_line[400]{};
        Check(pp::FormatDmProbe(dm, true, dm_line, sizeof(dm_line)) &&
              std::strstr(dm_line, "; cam1f0=0x8800000000001234; cam200=1; cam_ok=1") != nullptr, "dm probe: camera connect fields");
        w.Reset();
    }

    // --- Crash probe filter and format (no handler is installed here).
    {
        namespace cp = mgscoop::crash_probe;
        volatile long seen[cp::kMaxDistinct]{};
        char out[224]{};
        const std::uintptr_t base = 0x140000000;
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x12cc6d6, 0, 0x1c8, base, 0x9e7f000, 77, 3, 1, 3, seen, out,
                                   sizeof(out)) > 0, "AV in image reported");
        Check(std::strcmp(out, "crash_probe=av; rva=0x12cc6d6; access=read; target=0x1c8; thread=77; stage=3; realized=1; ctx=0x3\r\n") == 0,
              "AV line");
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x12cc6d6, 1, 0, base, 0x9e7f000, 1, 0, 0, 0, seen, out, sizeof(out)) == 0,
              "repeated RVA silent");
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x100, 1, 0x10, base, 0x9e7f000, 1, 0, 0, 0, seen, out, sizeof(out)) > 0 &&
              Contains(out, "access=write"), "write access");
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x200, 8, 0x10, base, 0x9e7f000, 1, 0, 0, 0, seen, out, sizeof(out)) > 0 &&
              Contains(out, "access=execute"), "execute access");
        Check(cp::detail::Describe(EXCEPTION_BREAKPOINT, base + 0x300, 0, 0, base, 0x9e7f000, 1, 0, 0, 0, seen, out, sizeof(out)) == 0,
              "non-AV ignored");
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base - 1, 0, 0, base, 0x9e7f000, 1, 0, 0, 0, seen, out, sizeof(out)) == 0 &&
              cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x9e7f000, 0, 0, base, 0x9e7f000, 1, 0, 0, 0, seen, out, sizeof(out)) == 0,
              "outside the image ignored");
        unsigned reported = 3;
        for (unsigned i = 0; i < 40; ++i)
            if (cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x1000 + i * 16, 0, 0, base, 0x9e7f000, 1, 0, 0, 0, seen, out,
                                     sizeof(out))) ++reported;
        Check(reported == cp::kMaxDistinct, "first 16 distinct RVAs only");
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x5000, 0, 0, 0, 0, 1, 0, 0, 0, seen, out, sizeof(out)) == 0,
              "no base, no report");
        Check(!cp::Install(nullptr, 0, nullptr), "install needs a base and a log");
    }

    // --- partner_net over 127.0.0.1 with a peer transport.
    NetTests();

    std::printf("%u partner player checks passed: ready state, every refusal in guard order, write guards (index, terminating, "
                "online bit, synchronizer), interlocked bit/slot/exists/policy stores, SEH fault containment, P+0x204 never "
                "written, code gate (synthetic and contract image), formatting, crash probe filter, partner_net loopback "
                "(send, rate limit, epoch/session/sequence drops, restart gap, reset, keepalive, first_rx); "
                "synthetic memory and 127.0.0.1 only\n",
                checks);
    return 0;
}
