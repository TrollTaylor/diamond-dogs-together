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

// One synthetic world. Objects live in a committed region so the test can flip
// page protection to exercise the SEH path.
struct World {
    unsigned char* region = nullptr;
    unsigned char *P, *Q, *S, *exists, *local_bits, *bitset, *bitset_data, *realizer, *policy, *g, *x, *vars, *records;
    float* slots;
    std::uint16_t* ids;
    std::uint8_t *pool, *parts;
    unsigned char *a18, *a28, *a30, *a38, *ad8, *r1d8, *r1e0, *r1f8;
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
        Put<std::uint16_t>(a18, 0, 0x1234); a28[0] = 0x56; Put<std::uint32_t>(a30, 0, 7); Put<std::uint16_t>(a38, 0, 9);
        Put<std::uint16_t>(ad8, 0, 0xBEEF);
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
    }
    std::uint32_t Flags() const { return Get<std::uint32_t>(P, c::p_flags); }
};

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
void PeerDatagram(void*, const std::uint8_t* data, std::size_t size) noexcept {
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
    Check(!net::Running() && !net::SubmitLocal(mgscoop::partner::Tick{}), "not running before Start");
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
    Check(!net::SubmitLocal(tick), "no epoch, no frame");
    net::SetEpoch(30050);
    Check(net::SubmitLocal(tick), "frame submitted");
    Check(!net::SubmitLocal(tick), "rate limited within one interval");
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
    Check(net::SubmitLocal(tick), "next interval sends again");

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
    Check(CapturedPrefix("partner_net=first_rx") == 2, "first_rx again after reset");
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

    // --- Anim survey.
    pp::Anim anim{};
    Check(Read(w, s) == pp::Refusal::none && pp::ReadAnim(s, anim), "anim read");
    Check(anim.q18 == 0x1234 && anim.q28 == 0x56 && anim.q30 == 7 && anim.q38 == 9 && anim.qd8 == 0xBEEF, "anim arrays");
    const std::uint32_t zero_hash_0x40 = [] { std::uint32_t h = 2166136261u; for (int i = 0; i < 0x40; ++i) { h ^= 0; h *= 16777619u; } return h; }();
    Check(anim.m1d8 == zero_hash_0x40, "record hash");
    w.r1d8[5] = 1;
    pp::Anim changed{};
    Check(pp::ReadAnim(s, changed) && changed.m1d8 != anim.m1d8 && changed.m1e0 == anim.m1e0, "record hash changes");
    Put<std::uintptr_t>(w.Q, c::q_anim_28, 0);
    Check(!pp::ReadAnim(s, anim) && anim.q18 == 0, "unreadable anim");
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
                             "rec1_status=0xa5a5a5a5\r\n", 400) == 0, "probe text");
    Check(Contains(line, "rec1_status=0xa5a5a5a5\r\n") && std::strlen(line) < pp::probe_capacity, "probe tail");
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

    // --- Crash probe filter and format (no handler is installed here).
    {
        namespace cp = mgscoop::crash_probe;
        volatile long seen[cp::kMaxDistinct]{};
        char out[224]{};
        const std::uintptr_t base = 0x140000000;
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x12cc6d6, 0, 0x1c8, base, 0x9e7f000, 77, 3, 1, seen, out,
                                   sizeof(out)) > 0, "AV in image reported");
        Check(std::strcmp(out, "crash_probe=av; rva=0x12cc6d6; access=read; target=0x1c8; thread=77; stage=3; realized=1\r\n") == 0,
              "AV line");
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x12cc6d6, 1, 0, base, 0x9e7f000, 1, 0, 0, seen, out, sizeof(out)) == 0,
              "repeated RVA silent");
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x100, 1, 0x10, base, 0x9e7f000, 1, 0, 0, seen, out, sizeof(out)) > 0 &&
              Contains(out, "access=write"), "write access");
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x200, 8, 0x10, base, 0x9e7f000, 1, 0, 0, seen, out, sizeof(out)) > 0 &&
              Contains(out, "access=execute"), "execute access");
        Check(cp::detail::Describe(EXCEPTION_BREAKPOINT, base + 0x300, 0, 0, base, 0x9e7f000, 1, 0, 0, seen, out, sizeof(out)) == 0,
              "non-AV ignored");
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base - 1, 0, 0, base, 0x9e7f000, 1, 0, 0, seen, out, sizeof(out)) == 0 &&
              cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x9e7f000, 0, 0, base, 0x9e7f000, 1, 0, 0, seen, out, sizeof(out)) == 0,
              "outside the image ignored");
        unsigned reported = 3;
        for (unsigned i = 0; i < 40; ++i)
            if (cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x1000 + i * 16, 0, 0, base, 0x9e7f000, 1, 0, 0, seen, out,
                                     sizeof(out))) ++reported;
        Check(reported == cp::kMaxDistinct, "first 16 distinct RVAs only");
        Check(cp::detail::Describe(EXCEPTION_ACCESS_VIOLATION, base + 0x5000, 0, 0, 0, 0, 1, 0, 0, seen, out, sizeof(out)) == 0,
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
