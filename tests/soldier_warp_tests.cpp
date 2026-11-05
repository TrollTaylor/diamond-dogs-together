// Execute the fingerprinted SendCommand dispatch bytes in a private allocation
// laid out like the image (contract sites, data slots, manager/registry
// statics). The per-type objects are heap stand-ins, the count call, the hop
// and the soldier handler are recorders. No MGSV process is involved.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/soldier_warp.hpp"
#include "soldier_relay_contract.hpp"
#include "soldier_warp_contract.hpp"

namespace {
namespace c = mgscoop::soldier_warp_contract;
namespace rc = mgscoop::soldier_relay_contract;
namespace sw = mgscoop::soldier_warp;
unsigned checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
template <class T> void Put(unsigned char* at, T value) { std::memcpy(at, &value, sizeof(value)); }
template <class T> T Get(const unsigned char* at) { T value{}; std::memcpy(&value, at, sizeof(value)); return value; }

struct Seen {
    unsigned calls = 0;
    void* self = nullptr;
    std::int32_t* out = nullptr;
    std::uint32_t index = 0;
    unsigned char message[c::message_size]{};
    bool aligned = false;
    std::int32_t answer = 0;
    unsigned char handled = 1;
} seen, engine;
std::uint32_t counts[2] = {0, 0x200};
void* CountFn(void*, void*) { return counts; }
std::int32_t* Record(Seen& s, void* self, std::int32_t* out, std::uint32_t index, unsigned char* message) {
    ++s.calls;
    s.self = self; s.out = out; s.index = index;
    s.aligned = (reinterpret_cast<std::uintptr_t>(message) & 15u) == 0;
    std::memcpy(s.message, message, sizeof(s.message));
    *out = s.answer;
    if (s.answer == 0) message[c::message_handled] = s.handled;
    return out;
}
std::int32_t* Handler(void* self, std::int32_t* out, std::uint32_t index, unsigned char* message) { return Record(seen, self, out, index, message); }
std::int32_t* Engine(void* self, std::int32_t* out, std::uint32_t index, unsigned char* message) { return Record(engine, self, out, index, message); }
// The shape of the soldier vtable +0x10 getter (0x1351f30): stores a qword into *out, never handles.
std::int32_t* Getter(void*, std::int32_t* out, std::uint32_t, unsigned char*) {
    const std::uint64_t value = 0x100000000ull;
    std::memcpy(out, &value, sizeof(value));
    return out;
}
// mov rax, imm64; jmp rax
void Stub(unsigned char* at, const void* target) {
    const unsigned char code[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    std::memcpy(at, code, sizeof(code));
    Put(at + 2, reinterpret_cast<std::uintptr_t>(target));
}
alignas(16) std::uintptr_t registry[0x30 / 8 + 0x80]{};
std::uintptr_t entry[2]{}, entry_inner[8]{}, type_object[4]{}, type_vt[16]{}, x_object[8]{}, handle_object[16]{}, handle_vt[8]{};
alignas(16) unsigned char leaf[0x100]{}, realize[0x10]{}, bits[0x40]{}, records_header[0x18]{}, records[0x70 * 4]{};
}

int main() {
    constexpr std::size_t span = 0x2c40000;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Check(image != nullptr, "fixture allocation");
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    for (const auto& site : c::sites) std::memcpy(image + site.rva, site.bytes, site.size);
    for (const auto& slot : c::slots) Put(image + slot.rva, base + slot.target);
    for (const auto& site : rc::sites) std::memcpy(image + site.rva, site.bytes, site.size);
    for (const auto& slot : rc::slots) Put(image + slot.rva, base + slot.target);
    std::uintptr_t mismatch = 1;
    Check(sw::Verify(base, span, mismatch) == nullptr && mismatch == 0, "exact sites and slots verify");
    image[0x13619e5] ^= 1;
    Check(std::strcmp(sw::Verify(base, span, mismatch), "body_contract") == 0 && mismatch == 0x13619be, "one-byte handler change refuses");
    image[0x13619e5] ^= 1;
    image[0xbf39f3] ^= 1;
    Check(std::strcmp(sw::Verify(base, span, mismatch), "body_contract") == 0 && mismatch == 0xbf39d0, "one-byte dispatch change refuses");
    image[0xbf39f3] ^= 1;
    Put(image + 0x2305ea8, base + 0xbf3a81);
    Check(std::strcmp(sw::Verify(base, span, mismatch), "slot_contract") == 0 && mismatch == 0x2305ea8, "manager slot change refuses");
    Put(image + 0x2305ea8, base + c::send_rva);
    Check(std::strcmp(sw::Verify(base, 0x2c38000, mismatch), "image") == 0, "truncated span refuses");
    Check(std::strcmp(sw::Verify(0, span, mismatch), "image") == 0, "null image refuses");
    Check(sw::Verify(base, span, mismatch) == nullptr, "contract restored");
    // Enemy latch: the relay contract (the typed hop 0xbf5450, the dispatcher 0x9e9ab0) is verified as well.
    image[0x9e9bed] ^= 1;
    Check(std::strcmp(sw::Verify(base, span, mismatch), "body_contract") == 0 && mismatch == 0x9e9ab0, "one-byte dispatcher change refuses");
    image[0x9e9bed] ^= 1;
    Put(image + 0x229c520, base + 0x9e9ab1);
    Check(std::strcmp(sw::Verify(base, span, mismatch), "slot_contract") == 0 && mismatch == 0x229c520, "type vtable +0x70 change refuses");
    Put(image + 0x229c520, base + rc::dispatcher_rva);
    Check(sw::Verify(base, span, mismatch) == nullptr, "relay contract restored");

    // Replace the handler body and give the count call / engine hop image addresses.
    Stub(image + c::handler_rva, reinterpret_cast<const void*>(&Handler));
    Stub(image + 0x100, reinterpret_cast<const void*>(&CountFn));
    Stub(image + 0x200, reinterpret_cast<const void*>(&Engine));
    Stub(image + 0x300, reinterpret_cast<const void*>(&Getter));
    // Manager static (0xbf3920 constructed) and registry.
    Put(image + c::manager_object_rva, base + c::manager_vtable_rva);
    Put<std::uint32_t>(image + c::manager_guard_rva, 1);
    Put(image + c::registry_slot_rva, reinterpret_cast<std::uintptr_t>(registry));
    registry[0] = base + c::registry_vtable_rva;
    registry[0x30 / 8 + 2] = reinterpret_cast<std::uintptr_t>(entry);
    entry[1] = reinterpret_cast<std::uintptr_t>(entry_inner);
    entry_inner[0x20 / 8] = reinterpret_cast<std::uintptr_t>(type_object);
    type_object[0] = reinterpret_cast<std::uintptr_t>(type_vt);
    type_object[1] = reinterpret_cast<std::uintptr_t>(x_object);
    type_vt[0x40 / 8] = base + 0x100;
    x_object[0x28 / 8] = reinterpret_cast<std::uintptr_t>(handle_object);
    handle_object[0] = reinterpret_cast<std::uintptr_t>(handle_vt);
    handle_vt[0x10 / 8] = base + c::forwarder_rva;
    handle_object[0x48 / 8] = reinterpret_cast<std::uintptr_t>(leaf);
    // Soldier subobject: vtable A, count, realized bits, stance records.
    Put(leaf, base + c::soldier_vtable_a_rva);
    Put<std::uint32_t>(leaf + c::leaf_count, 0x200);
    Put(leaf + c::leaf_realize, reinterpret_cast<std::uintptr_t>(realize));
    Put(realize + c::realize_bits, reinterpret_cast<std::uintptr_t>(bits));
    Put(leaf + c::leaf_records, reinterpret_cast<std::uintptr_t>(records_header));
    Put(records_header + c::records_data, reinterpret_cast<std::uintptr_t>(records));
    Put<std::uint32_t>(records_header + c::records_count, 4);
    const std::uint32_t go = (2u << 9) | 3u;
    bits[0] = 1u << 3;
    Check(FlushInstructionCache(GetCurrentProcess(), image, span) != 0, "fixture instruction cache");

    sw::Request request{};
    request.go_id = go;
    request.pos[0] = 13.5f; request.pos[1] = 20.2f; request.pos[2] = 34.0f;
    request.player[0] = 10.0f; request.player[1] = 20.0f; request.player[2] = 30.0f;
    request.yaw = 3.14159265f;
    auto r = sw::Warp(base, span, request);
    Check(r.refusal == nullptr && r.out == 0 && r.handled == 1, "pinned send through the real dispatch bytes is handled");
    Check(r.chain.pinned && r.chain.leaf == reinterpret_cast<std::uintptr_t>(leaf) && r.chain.realized == 1 && r.chain.stance == 0,
          "census pins the forwarder hop to the soldier handler");
    Check(seen.calls == 1 && seen.self == leaf && seen.index == 3 && seen.aligned, "handler gets the subobject, idx = id & 0x1ff, aligned message");
    Check(Get<std::uint64_t>(seen.message) == c::message_hash, "message hash");
    Check(Get<float>(seen.message + 0x10) == 13.5f && Get<float>(seen.message + 0x14) == 20.2f && Get<float>(seen.message + 0x18) == 34.0f &&
          Get<float>(seen.message + 0x1c) == 0.0f, "ground position {x, y, z, 0}");
    Check(Get<float>(seen.message + 0x20) == 0.0f && std::fabs(Get<float>(seen.message + 0x24) - 1.0f) < 1e-6f &&
          Get<float>(seen.message + 0x28) == 0.0f && std::fabs(Get<float>(seen.message + 0x2c)) < 1e-6f, "yaw quaternion {0, sin, 0, cos}");
    bool zero = true;
    for (std::size_t i = 8; i < 0x10; ++i) zero = zero && seen.message[i] == 0;
    for (std::size_t i = 0x30; i < c::message_size; ++i) zero = zero && seen.message[i] == 0;
    Check(zero, "padding, player index and handled start at zero");
    char line[512]{};
    Check(sw::FormatCensus(base, span, go, r, line, sizeof(line)) > 0 && std::strstr(line, "leaf=pinned") &&
          std::strstr(line, "hop=0xc15500") && std::strstr(line, "hop_bytes=488b4948488b0148ffa0a8000000") &&
          std::strstr(line, "leaf_vt=0x23e78f0") && std::strstr(line, "handled=1") && std::strstr(line, "refusal=none"), "census line");
    Check(std::strcmp(sw::LeafName(r.chain), "pinned") == 0, "the warp log lines name a pinned hop leaf=pinned");

    // Vtable B is the second soldier class.
    Put(leaf, base + c::soldier_vtable_b_rva);
    Check(sw::Warp(base, span, request).chain.pinned && seen.calls == 2, "second soldier vtable pinned");
    Put(leaf, base + c::soldier_vtable_a_rva);
    // The handler's own refusals, mirrored without sending.
    bits[0] = 0;
    r = sw::Warp(base, span, request);
    Check(std::strcmp(r.refusal, "unrealized") == 0 && r.out == -1 && seen.calls == 2, "unrealized: r=-1 and nothing sent");
    bits[0] = 1u << 3;
    for (std::uint32_t stance = 1; stance <= 4; ++stance) {
        Put(records + 3 * c::record_stride + c::record_stance, 0x120u | stance);
        Check(std::strcmp(sw::Warp(base, span, request).refusal, "stance") == 0 && seen.calls == 2, "stance 1-4 refused unsent");
    }
    Put(records + 3 * c::record_stride + c::record_stance, 0x125u);
    Check(sw::Warp(base, span, request).refusal == nullptr && seen.calls == 3, "plain stance nibble 5 sent");
    Put<std::uint32_t>(records + 3 * c::record_stride + c::record_stance, 0);
    Put<std::uint32_t>(leaf + c::leaf_count, 3);
    Check(std::strcmp(sw::Warp(base, span, request).refusal, "index") == 0 && seen.calls == 3, "index beyond the handler count");
    Put<std::uint32_t>(leaf + c::leaf_count, 0x200);
    // The engine's count check (0xbf4270): a missing handle answers -1.
    counts[1] = 3;
    r = sw::Warp(base, span, request);
    Check(r.refusal == nullptr && r.out == -1 && r.handled == 0 && seen.calls == 3, "SendCommand missing handle answers -1");
    counts[1] = 0x200;
    // Request gates.
    sw::Request bad = request;
    bad.go_id = (3u << 9) | 3u;
    Check(std::strcmp(sw::Warp(base, span, bad).refusal, "type") == 0, "player/hostage/other types refused");
    bad = request; bad.go_id = 0x10403;
    Check(std::strcmp(sw::Warp(base, span, bad).refusal, "type") == 0, "ids above u16 refused");
    bad = request; bad.pos[0] = request.player[0] + 20.5f;
    Check(std::strcmp(sw::Warp(base, span, bad).refusal, "range") == 0, "more than 20 m from the player refused");
    // Guest spawn-sync corrections: the range is the request's, at most 60 m (kMaxRangeLimitM).
    bad.max_range = sw::kMaxRangeLimitM;
    const unsigned calls = seen.calls;
    r = sw::Warp(base, span, bad);
    Check(r.refusal == nullptr && r.sent && seen.calls == calls + 1, "a 60 m correction range admits 20.5 m");
    bad.pos[0] = request.player[0] + 60.5f;
    Check(std::strcmp(sw::Warp(base, span, bad).refusal, "range") == 0 && seen.calls == calls + 1, "more than 60 m refused");
    const float limits[] = {0.0f, -1.0f, 60.01f, NAN, INFINITY};
    for (const float limit : limits) {
        bad = request; bad.max_range = limit;
        Check(std::strcmp(sw::Warp(base, span, bad).refusal, "input") == 0 && seen.calls == calls + 1, "range outside (0, 60] refused");
    }
    Check(sw::Request{}.max_range == sw::kMaxRangeM && sw::kMaxRangeM == 20.0f, "the host default stays 20 m");
    seen.calls = calls;  // the checks below count handler calls from here
    bad = request; bad.pos[1] = NAN;
    Check(std::strcmp(sw::Warp(base, span, bad).refusal, "input") == 0, "non-finite position refused");
    bad = request; bad.yaw = 9.0f;
    Check(std::strcmp(sw::Warp(base, span, bad).refusal, "input") == 0, "yaw bound");
    bad = request; bad.player[2] = INFINITY;
    Check(std::strcmp(sw::Warp(base, span, bad).refusal, "input") == 0, "non-finite player refused");
    // Manager and registry gates.
    Put<std::uint32_t>(image + c::manager_guard_rva, 0);
    Check(std::strcmp(sw::Warp(base, span, request).refusal, "manager") == 0, "unconstructed manager refused");
    Put<std::uint32_t>(image + c::manager_guard_rva, 1);
    Put(image + c::manager_object_rva, base + 0x23db440);
    Check(std::strcmp(sw::Warp(base, span, request).refusal, "manager") == 0, "base-class manager vtable refused");
    Put(image + c::manager_object_rva, base + c::manager_vtable_rva);
    registry[0] = base + 0x23073c8;
    Check(std::strcmp(sw::Warp(base, span, request).refusal, "registry") == 0, "foreign registry vtable refused");
    registry[0] = base + c::registry_vtable_rva;
    Put<std::uintptr_t>(image + c::registry_slot_rva, 0);
    Check(std::strcmp(sw::Warp(base, span, request).refusal, "registry") == 0, "missing registry refused");
    Put(image + c::registry_slot_rva, reinterpret_cast<std::uintptr_t>(registry));
    entry_inner[0x20 / 8] = 0;
    Check(std::strcmp(sw::Warp(base, span, request).refusal, "chain") == 0, "unregistered soldier type refused");
    entry_inner[0x20 / 8] = reinterpret_cast<std::uintptr_t>(type_object);
    type_vt[0x40 / 8] = reinterpret_cast<std::uintptr_t>(&CountFn);
    Check(std::strcmp(sw::Warp(base, span, request).refusal, "chain") == 0, "count call outside the image refused");
    type_vt[0x40 / 8] = base + 0x100;
    handle_vt[0x10 / 8] = reinterpret_cast<std::uintptr_t>(&Engine);
    Check(std::strcmp(sw::Warp(base, span, request).refusal, "chain") == 0 && engine.calls == 0, "hop outside the image refused");
    // An unrecognized in-image hop: the engine dispatch is used, census says engine.
    handle_vt[0x10 / 8] = base + 0x200;
    r = sw::Warp(base, span, request);
    Check(r.refusal == nullptr && !r.chain.pinned && engine.calls == 1 && engine.self == handle_object && engine.index == 3 && engine.aligned &&
          r.out == 0 && r.handled == 1 && seen.calls == 3, "unpinned hop: the engine's own dispatch answers");
    Check(sw::FormatCensus(base, span, go, r, line, sizeof(line)) > 0 && std::strstr(line, "leaf=engine") && std::strstr(line, "hop=0x200"),
          "census marks the unpinned hop");
    Check(std::strcmp(sw::LeafName(r.chain), "engine") == 0, "the warp log lines name an unpinned, untyped hop leaf=engine");
    engine.answer = -1;
    r = sw::Warp(base, span, request);
    Check(r.refusal == nullptr && r.sent && r.out == -1 && r.handled == 0, "unpinned unrealized answer passes through");
    // Any other answer from the unpinned dispatch is an anomaly (sent, then latched by the gate).
    engine.answer = 7;
    r = sw::Warp(base, span, request);
    Check(std::strcmp(r.refusal, "anomaly") == 0 && r.sent && r.out == 7 && engine.calls == 3, "unexpected out value is an anomaly");
    engine.answer = 0; engine.handled = 0;
    r = sw::Warp(base, span, request);
    Check(std::strcmp(r.refusal, "anomaly") == 0 && r.out == 0 && r.handled == 0, "out=0 without the handled flag is an anomaly");
    engine.handled = 1;
    handle_vt[0x10 / 8] = base + 0x300;
    r = sw::Warp(base, span, request);
    Check(std::strcmp(r.refusal, "anomaly") == 0 && r.out == 0 && r.handled == 0 && seen.calls == 3,
          "a getter-shaped hop writes a qword into the 16-byte answer and is an anomaly");
    // Enemy latch (run e583ee62, every census line hop=0xbf5450): the live typed dispatch. The real hop bytes call the
    // object at handle+0x20 (a soldier type: vtable 0x229c4b0, +0x70 = the dispatcher, here the engine recorder).
    {
        const sw::Result getter_answer = r;  // the gate checks below record it
        const unsigned engine_calls = engine.calls;  // and count the engine calls from here
        Stub(image + rc::dispatcher_rva, reinterpret_cast<const void*>(&Engine));
        Check(FlushInstructionCache(GetCurrentProcess(), image, span) != 0, "typed fixture instruction cache");
        std::uintptr_t typed_type[2] = {base + rc::type_vtable_rva, 0};
        const std::uintptr_t saved_vt = handle_object[0];
        handle_object[0] = base + rc::handle_vtable_rva;
        handle_object[rc::handle_type_object / 8] = reinterpret_cast<std::uintptr_t>(typed_type);
        engine.answer = 0; engine.handled = 1;
        const unsigned before = engine.calls;
        r = sw::Warp(base, span, request);
        Check(r.refusal == nullptr && r.chain.typed && !r.chain.pinned && r.out == 0 && r.handled == 1 && engine.calls == before + 1 &&
              engine.self == typed_type && engine.index == 3, "typed: the real hop reaches the soldier type dispatcher; moved");
        Check(sw::FormatCensus(base, span, go, r, line, sizeof(line)) > 0 && std::strstr(line, "leaf=typed") &&
              std::strstr(line, "hop=0xbf5450"), "census names the typed dispatch");
        Check(std::strcmp(sw::LeafName(r.chain), "typed") == 0,
              "the warp log lines (world_place=host_warp ...) name the typed dispatch leaf=typed, as the census does");
        // out=0 handled=0: the dispatcher relayed it to the guard's unrealized form (0xbf6490), nothing moved.
        engine.handled = 0;
        r = sw::Warp(base, span, request);
        Check(r.refusal && std::strcmp(r.refusal, "relayed") == 0 && r.sent && r.out == 0 && r.handled == 0,
              "typed out=0 handled=0 is relayed, not an anomaly");
        // The handle's mode 2 (cmp [handle+0x30],2): the hop answers 0x8ce64801 without calling anything.
        handle_object[rc::handle_mode / 8] = 2;
        const unsigned mode_calls = engine.calls;
        r = sw::Warp(base, span, request);
        Check(r.refusal && std::strcmp(r.refusal, "relayed") == 0 && static_cast<std::uint32_t>(r.out) == rc::mode2_answer &&
              r.handled == 0 && engine.calls == mode_calls, "mode 2 answer is relayed, nothing called");
        handle_object[rc::handle_mode / 8] = 0;
        // Typed, any other answer is still an anomaly.
        engine.answer = 7;
        r = sw::Warp(base, span, request);
        Check(r.refusal && std::strcmp(r.refusal, "anomaly") == 0 && r.out == 7, "typed: an unexplained out value is an anomaly");
        engine.answer = -1;
        r = sw::Warp(base, span, request);
        Check(r.refusal == nullptr && r.out == -1 && r.handled == 0, "typed: out=-1 waits as before");
        // A look-alike: handle+0x20 is no soldier type (its vtable is not 0x229c4b0): out=0 handled=0 stays an anomaly.
        std::uintptr_t other_type_vt[0x80 / 8]{};
        other_type_vt[rc::type_dispatch_slot / 8] = base + 0x200;
        typed_type[0] = reinterpret_cast<std::uintptr_t>(other_type_vt);
        engine.answer = 0; engine.handled = 0;
        r = sw::Warp(base, span, request);
        Check(r.refusal && std::strcmp(r.refusal, "anomaly") == 0 && !r.chain.typed && r.out == 0 && r.handled == 0,
              "untyped out=0 handled=0 stays an anomaly");
        typed_type[0] = base + rc::type_vtable_rva;
        r = sw::Warp(base, span, request);
        Check(r.refusal && std::strcmp(r.refusal, "relayed") == 0 && r.chain.typed, "typed again: relayed");
        const sw::Result relayed = r;
        // The gate: a relay never latches anything. It backs only that guard off: 3 s, doubled per relay in a row up
        // to 30 s; a move clears it. Six relays on six guards (e583ee62 latched the process at its fifth) latch nothing.
        sw::Gate g{};
        for (unsigned i = 0; i < 6; ++i)
            Check(g.Admit(100 + i, go + i, 1000) == nullptr && g.Record(relayed) && std::strcmp(g.Record(relayed), "relayed") == 0,
                  "each relay is recorded as a relay");
        Check(!g.latched && g.anomalies == 0 && g.relayed == 12 && g.SoldierAnomalies(go) == 0 && g.soldier_count == 0,
              "six guards relayed twice each: no guard and no process latch");
        sw::Gate b{};
        Check(b.Admit(1, go, 1000) == nullptr && std::strcmp(b.Record(relayed), "relayed") == 0 && b.RelayStreak(go) == 1 &&
              b.BackoffLeft(go, 1000) == sw::kRelayBackoffMs, "first relay: 3 s");
        Check(std::strcmp(b.Admit(2, go, 3999), "backoff") == 0 && b.Admit(3, go + 1, 3999) == nullptr,
              "that guard waits (refused unsent), the others do not");
        Check(std::strcmp(b.AdmitDrive(go, 3999), "backoff") == 0, "the drive waits too");
        Check(b.Admit(4, go, 4000) == nullptr && std::strcmp(b.Record(relayed), "relayed") == 0 && b.RelayStreak(go) == 2 &&
              b.BackoffLeft(go, 4000) == 2 * sw::kRelayBackoffMs, "retried after 3 s; a second relay in a row: 6 s");
        std::uint64_t t = 10000;
        for (unsigned i = 0; i < 6; ++i) {
            Check(b.AdmitDrive(go, t) == nullptr && std::strcmp(b.Record(relayed), "relayed") == 0, "relayed again after its backoff");
            t += b.BackoffLeft(go, t);
        }
        Check(b.RelayStreak(go) == 8 && b.BackoffLeft(go, t - sw::kRelayBackoffMaxMs) == sw::kRelayBackoffMaxMs && !b.latched,
              "repeated relays: at most 30 s between tries, never a latch");
        sw::Result moved{};
        moved.out = 0; moved.handled = 1;
        Check(b.AdmitDrive(go, t) == nullptr && b.Record(moved) == nullptr && b.RelayStreak(go) == 0 && b.BackoffLeft(go, t) == 0,
              "a move clears that guard's backoff");
        // Many relayed guards: the table reuses the backoff that ends first; nothing latches.
        sw::Gate m{};
        for (unsigned i = 0; i < 2 * sw::kRelayTracked; ++i)
            Check(m.Admit(500 + i, (2u << 9) + (i % 0x1ff), 1000 + i) == nullptr && m.Record(relayed), "many guards relayed");
        Check(!m.latched && m.relayed == 2 * sw::kRelayTracked && m.BackoffLeft((2u << 9) + ((2 * sw::kRelayTracked - 1) % 0x1ff),
              1000 + 2 * sw::kRelayTracked) > 0, "the newest relayed guard waits; no latch");
        // Unexplained answers still latch: a guard at its second, the process at its fifth (relays never count).
        sw::Result unexplained{};
        unexplained.refusal = "anomaly"; unexplained.sent = true; unexplained.out = 7;
        for (unsigned i = 0; i < 4; ++i)
            Check(m.Admit(900 + i, go + 0x100 + i, 99999) == nullptr && m.Record(unexplained) == nullptr, "an unexplained answer counts");
        Check(!m.latched && m.anomalies == 4, "relays never add to the process latch count");
        Check(m.Admit(904, go + 0x104, 99999) == nullptr && std::strcmp(m.Record(unexplained), "anomaly") == 0 && m.latched,
              "the fifth unexplained answer still latches the process (a global fault)");
        handle_object[0] = saved_vt;
        handle_object[rc::handle_type_object / 8] = 0;
        engine.answer = 0; engine.handled = 1;
        r = getter_answer;
        engine.calls = engine_calls;
    }
    // Enemy follow multiplies warps: an anomaly counts against its guard; the second on one guard latches that guard off,
    // the fifth across guards latches the feature off.
    sw::Gate latch{};
    Check(latch.Admit(1, go) == nullptr && latch.Record(r) == nullptr && latch.SoldierAnomalies(go) == 1 && latch.anomalies == 1 &&
          !latch.latched && latch.Admit(2, go) == nullptr, "a first anomaly counts against its guard only");
    Check(std::strcmp(latch.Record(r), "soldier") == 0 && latch.SoldierAnomalies(go) == 2 && !latch.latched &&
          std::strcmp(latch.Admit(3, go), "soldier") == 0, "a guard's second anomaly latches that guard off");
    Check(latch.Admit(4, go + 1) == nullptr && latch.Record(r) == nullptr && latch.Admit(5, go + 2) == nullptr &&
          latch.Record(r) == nullptr && latch.anomalies == 4 && !latch.latched, "other guards still move");
    Check(latch.Admit(6, go + 3) == nullptr && std::strcmp(latch.Record(r), "anomaly") == 0 && latch.anomalies == 5 &&
          std::strcmp(latch.Admit(7, go + 4), "latched") == 0, "the fifth anomaly latches the feature off");
    sw::Gate faulted{};
    sw::Result fault_result{};
    fault_result.refusal = "fault";
    Check(faulted.Admit(1, go) == nullptr && std::strcmp(faulted.Record(fault_result), "fault") == 0 &&
          std::strcmp(faulted.Admit(2, go + 1), "latched") == 0, "a fault still latches the feature off at once");
    // A recognized forwarder that does not reach the verified handler on a soldier subobject refuses unsent.
    handle_vt[0x10 / 8] = base + c::forwarder_rva;
    alignas(16) static unsigned char other[0x100]{};
    std::uintptr_t other_vt[0x20]{};
    other_vt[0xa8 / 8] = base + 0x200;
    Put(other, reinterpret_cast<std::uintptr_t>(other_vt));
    handle_object[0x48 / 8] = reinterpret_cast<std::uintptr_t>(other);
    r = sw::Warp(base, span, request);
    Check(std::strcmp(r.refusal, "leaf") == 0 && !r.sent && !r.chain.pinned && engine.calls == 4 && seen.calls == 3,
          "forwarder to a non-soldier refuses unsent");
    handle_object[0x48 / 8] = 0;
    r = sw::Warp(base, span, request);
    Check(std::strcmp(r.refusal, "leaf") == 0 && !r.sent && engine.calls == 4, "forwarder to a missing object refuses unsent");
    handle_object[0x48 / 8] = reinterpret_cast<std::uintptr_t>(leaf);
    r = sw::Warp(base, span, request);
    Check(r.refusal == nullptr && r.sent && r.chain.pinned && seen.calls == 4, "pinned again");

    // Per-command gate: one guard, bounded attempts, nothing after landing, latch on fault.
    sw::Gate gate{};
    Check(std::strcmp(gate.Admit(0, go), "target") == 0, "command id 0 is never admitted");
    Check(gate.Admit(7, go) == nullptr && gate.attempts == 1, "first attempt admitted");
    Check(std::strcmp(gate.Admit(7, go + 1), "target") == 0, "one guard per command");
    for (unsigned i = 1; i < sw::kMaxAttempts; ++i) Check(gate.Admit(7, go) == nullptr, "retries within the budget");
    Check(std::strcmp(gate.Admit(7, go), "budget") == 0 && gate.attempts == sw::kMaxAttempts, "attempt budget");
    Check(gate.Admit(8, go + 1) == nullptr && gate.attempts == 1 && gate.go_id == go + 1, "a new command starts a fresh budget");
    sw::Result answer{};
    answer.out = -1;
    Check(gate.Record(answer) == nullptr && !gate.landed && gate.Admit(8, go + 1) == nullptr, "waiting answer keeps retrying");
    answer.refusal = "unrealized";
    Check(gate.Record(answer) == nullptr && !gate.latched, "a refusal before sending does not latch");
    answer.refusal = nullptr; answer.out = 0; answer.handled = 1;
    Check(gate.Record(answer) == nullptr && gate.landed, "handled answer lands");
    Check(std::strcmp(gate.Admit(8, go + 1), "landed") == 0, "nothing is sent after the guard landed");
    answer = {}; answer.refusal = "fault";
    Check(std::strcmp(gate.Record(answer), "fault") == 0 && gate.Record(answer) == nullptr, "a fault latches once");
    Check(std::strcmp(gate.Admit(9, go), "latched") == 0, "latched until process exit");
    // WARP_V1 receipt grammar (the Lua -> native trust boundary).
    const auto parse = [](const char* text, std::uint32_t& command, sw::Request& out) {
        out = {}; command = 0;
        return sw::ParseReceipt(text, std::strlen(text), command, out);
    };
    std::uint32_t command = 0;
    sw::Request parsed{};
    Check(parse("12 1027 5670 20200 -32500 2094", command, parsed) && command == 12 && parsed.go_id == 1027 &&
          std::fabs(parsed.pos[0] - 5.67f) < 1e-4f && std::fabs(parsed.pos[1] - 20.2f) < 1e-4f && std::fabs(parsed.pos[2] + 32.5f) < 1e-4f &&
          std::fabs(parsed.yaw - 2.094f) < 1e-6f && parsed.player[0] == 0.0f, "receipt parsed (player left to the caller)");
    Check(parse("4294967295 65535 -100000000 100000000 0 -8000", command, parsed) && command == 0xffffffffu, "receipt bounds inclusive");
    const char* const bad_receipts[] = {"", "0 1027 0 0 0 0", "-1 1027 0 0 0 0", "4294967296 1027 0 0 0 0", "1 65536 0 0 0 0",
        "1 -1 0 0 0 0", "1 1027 100000001 0 0 0", "1 1027 0 0 -100000001 0", "1 1027 0 0 0 8001", "1 1027 0 0 0 -8001",
        "1 1027 0 0 0", "1 1027 0 0 0 0 0", "1 1027 0 0 0 0 ", " 1 1027 0 0 0 0", "1  1027 0 0 0 0", "1 +1027 0 0 0 0",
        "1 1027 0.5 0 0 0", "1 1027 0 0 0 1e3", "1 1027 0 0 0 99999999999999999999", "1 1027 0 0 0 0x10", "1 1027 0 0 0 --1"};
    for (const char* text : bad_receipts) Check(!parse(text, command, parsed), text);
    Check(!sw::ParseReceipt(nullptr, 4, command, parsed) && !sw::ParseReceipt("1 1027 0 0 0 0", 0, command, parsed),
          "null or empty receipt refused");
    // Guest grant admission (GuestWarpReceipt): the lab key rule (any kind), the enemy follow rule (play or lab with
    // soldier_follow, 30010 / 30020 / 30050, corrections only) and the Mother Base staff rule (play or lab, mission
    // 30050, corrections only: each staff member stands where the host's copy is).
    using G = sw::GuestGrant;
    G staff = G::staff;
    Check(sw::GuestAdmit({true, false, true, 30010, false}, staff) == nullptr && staff == G::lab, "lab key: a warp grant anywhere");
    Check(sw::GuestAdmit({true, false, true, 30050, true}, staff) == nullptr && staff == G::lab, "lab key first: never the staff rule");
    Check(std::strcmp(sw::GuestAdmit({true, false, false, 30010, true}, staff), "disabled") == 0 && staff == G::none,
          "lab without the key, free roam");
    Check(sw::GuestAdmit({false, true, false, 30050, true}, staff) == nullptr && staff == G::staff, "play at Mother Base: a correction");
    Check(sw::GuestAdmit({true, false, false, 30050, true}, staff) == nullptr && staff == G::staff,
          "lab without the key at Mother Base: a correction");
    Check(std::strcmp(sw::GuestAdmit({false, true, false, 30050, false}, staff), "disabled") == 0 && staff == G::none,
          "play at Mother Base: never a warp, route or come grant");
    Check(std::strcmp(sw::GuestAdmit({false, true, true, 30010, true}, staff), "disabled") == 0, "play in free roam: refused (key ignored)");
    Check(std::strcmp(sw::GuestAdmit({false, false, true, 30050, true}, staff), "role") == 0, "neither play nor lab: refused");
    // Enemy follow (docs/world-sync-design.md 7.8).
    const unsigned short follow_missions[] = {30010, 30020, 30050};
    for (const unsigned short mission : follow_missions) {
        Check(sw::GuestAdmit({false, true, false, mission, true, true}, staff) == nullptr && staff == G::follow,
              "follow: a play correction in free roam and at Mother Base");
        Check(sw::GuestAdmit({true, false, false, mission, true, true}, staff) == nullptr && staff == G::follow,
              "follow: a lab correction without the lab key");
        Check(std::strcmp(sw::GuestAdmit({false, true, false, mission, false, true}, staff), "disabled") == 0 && staff == G::none,
              "follow: never a warp, route or come grant");
    }
    Check(std::strcmp(sw::GuestAdmit({false, true, false, 30010, true, false}, staff), "disabled") == 0, "follow off: free roam refused");
    Check(std::strcmp(sw::GuestAdmit({false, true, false, 10033, true, true}, staff), "disabled") == 0 &&
          std::strcmp(sw::GuestAdmit({false, true, false, 40010, true, true}, staff), "disabled") == 0,
          "follow: never in a story mission or an ACC");
    Check(std::strcmp(sw::GuestAdmit({false, false, false, 30010, true, true}, staff), "role") == 0, "follow: neither play nor lab refused");
    Check(sw::GuestAdmit({true, false, true, 30010, true, true}, staff) == nullptr && staff == G::lab, "the lab key comes first");
    Check(sw::FollowMission(30010) && sw::FollowMission(30020) && sw::FollowMission(30050) && !sw::FollowMission(40050) &&
          !sw::FollowMission(10010), "follow missions: free roam and Mother Base");
    // Enemy sync M4 (docs/world-sync-design.md 7.10): the soft follow's target, step and admission, the drive gate.
    {
        sw::DriveSample a{true, 1000, {0.0f, 1.0f, 0.0f}, 0.5f}, b{true, 1100, {1.0f, 1.0f, 0.0f}, 0.6f}, none{};
        float out[3]{}, yaw = 0.0f;
        Check(sw::DriveTarget(a, b, 1050, out, yaw) && std::fabs(out[0] - 0.5f) < 1e-4f && yaw == 0.6f, "drive: halfway between");
        Check(sw::DriveTarget(a, b, 1150, out, yaw) && std::fabs(out[0] - 1.5f) < 1e-4f, "drive: extrapolated 50 ms");
        Check(sw::DriveTarget(a, b, 1600, out, yaw) && std::fabs(out[0] - 3.5f) < 1e-4f, "drive: extrapolation capped at 250 ms");
        Check(sw::DriveTarget(none, b, 1050, out, yaw) && out[0] == 1.0f, "drive: one sample");
        Check(!sw::DriveTarget(a, b, 1100 + sw::kDriveStaleMs + sw::kDriveDelayMs + 1, out, yaw) && !sw::DriveTarget(a, none, 1000, out, yaw),
              "drive: stale or none");
        const float here[3] = {0.0f, 1.8f, 0.0f};
        const float near_to[3] = {1.0f, 1.4f, 0.0f}, far_to[3] = {3.0f, 1.4f, 4.0f}, snap_to[3] = {9.0f, 1.4f, 0.0f};
        const float up_to[3] = {2.0f, 5.0f, 0.0f}, close_to[3] = {0.35f, 1.4f, 0.0f};
        auto ds = sw::DriveStep(here, near_to, false, 1.5f, 0.3f, 0.4f, 8.0f, 0.1f);
        Check(!ds.move && !ds.active && std::fabs(ds.error - 1.0f) < 1e-4f, "drive: 1 m at rest stays");
        ds = sw::DriveStep(here, far_to, false, 1.5f, 0.3f, 0.4f, 8.0f, 0.1f);
        Check(ds.move && ds.active && !ds.hard && std::fabs(ds.error - 5.0f) < 1e-4f && std::fabs(ds.pos[0] - 0.24f) < 1e-4f &&
                  std::fabs(ds.pos[2] - 0.32f) < 1e-4f && std::fabs(ds.pos[1] - 1.1f) < 1e-4f,
              "drive: 5 m off: a 0.4 m step on its own ground (feet + 0.1 m)");
        ds = sw::DriveStep(here, near_to, true, 1.5f, 0.3f, 0.4f, 8.0f, 0.1f);
        Check(ds.move && std::fabs(ds.pos[0] - 0.4f) < 1e-4f && ds.active, "drive: moving, 1 m off: still stepping");
        ds = sw::DriveStep(here, close_to, true, 1.5f, 0.3f, 0.4f, 8.0f, 0.1f);
        Check(ds.move && std::fabs(ds.pos[0] - 0.35f) < 1e-4f && !ds.active, "drive: the last step arrives, then rests");
        ds = sw::DriveStep(here, snap_to, false, 1.5f, 0.3f, 0.4f, 8.0f, 0.1f);
        Check(ds.move && ds.hard && ds.pos[0] == 9.0f && ds.pos[1] == 1.4f, "drive: beyond 8 m the whole way (the target y)");
        ds = sw::DriveStep(here, up_to, false, 1.5f, 0.3f, 0.4f, 8.0f, 0.1f);
        Check(ds.move && ds.pos[1] == 5.0f, "drive: a target over 1 m off this copy's feet takes its own y");
        // Enemy gaps (place_lift): the level y is this copy's own GetPosition y + 0.02 m, not 0.7 m under it.
        const float lh[3] = {0.0f, 268.0f, 0.0f}, lt[3] = {3.0f, 268.0f, 0.0f}, lup[3] = {3.0f, 269.5f, 0.0f};
        ds = sw::DriveStep(lh, lt, false, 1.5f, 0.3f, 0.4f, 8.0f, 0.1f);
        Check(ds.move && std::fabs(ds.pos[1] - 267.3f) < 1e-3f, "drive: the old level y (feet + 0.1 m = 267.3)");
        ds = sw::DriveStep(lh, lt, false, 1.5f, 0.3f, 0.4f, 8.0f, sw::kLiftLevelM, sw::kLiftFeetM);
        Check(ds.move && std::fabs(ds.pos[1] - 268.02f) < 1e-3f, "drive (place_lift): its own height + 0.02 m = 268.02");
        ds = sw::DriveStep(lh, lup, false, 1.5f, 0.3f, 0.4f, 8.0f, sw::kLiftLevelM, sw::kLiftFeetM);
        Check(ds.move && ds.pos[1] == 269.5f, "drive (place_lift): a target 1.5 m above takes its y");
        // Enemy gaps 3 (drive_vert, docs/world-sync-design.md 7.18): at rest in x/z (1.05 m) but 3.3 m under the target
        // (drive_sink of 2929558510 at the defaults in 6348f361 / 41f2b07f / 31a799e3, e 102-105 cm): without vert_m nothing
        // moves (as before), with it the target's spot; 0.9 m apart in y stays.
        const float vh[3] = {0.0f, 268.3f, 0.0f}, vt[3] = {1.05f, 271.6f, 0.0f}, vlow[3] = {1.05f, 269.2f, 0.0f};
        ds = sw::DriveStep(vh, vt, false, 1.5f, 0.3f, 0.4f, 8.0f, sw::kLiftLevelM, sw::kLiftFeetM);
        Check(!ds.move && !ds.vert, "drive: 3.3 m under the target at rest in x/z is not moved without drive_vert (drive_sink)");
        ds = sw::DriveStep(vh, vt, false, 1.5f, 0.3f, 0.4f, 8.0f, sw::kLiftLevelM, sw::kLiftFeetM, sw::kDriveVertM);
        Check(ds.move && ds.vert && !ds.hard && ds.pos[0] == vt[0] && ds.pos[1] == vt[1] && ds.pos[2] == vt[2],
              "drive_vert: 3.3 m under the target at rest: put on the target's spot");
        ds = sw::DriveStep(vh, vlow, false, 1.5f, 0.3f, 0.4f, 8.0f, sw::kLiftLevelM, sw::kLiftFeetM, sw::kDriveVertM);
        Check(!ds.move && !ds.vert, "drive_vert: 0.9 m apart in y at rest stays");
        {
            std::uint64_t since = 0;
            const bool v0 = sw::DriveVertDue(3.3f, 5000, since), v1 = sw::DriveVertDue(-3.3f, 5900, since),
                       v2 = sw::DriveVertDue(3.3f, 6000, since), v3 = sw::DriveVertDue(0.4f, 6100, since),
                       v4 = sw::DriveVertDue(3.3f, 6200, since);
            Check(!v0 && !v1 && v2 && !v3 && !v4 && since == 6200,
                  "drive_vert: due once more than 1.5 m apart in y for 1 s; a smaller gap starts over");
            // Enemy round 7 track A (design 7.20): no thrash. Round 6's copies popped up and dropped again 1.4-1.7 s after a
            // snap (2929558510 -2.47 then +1.80 m; 2942862796 2.61 / 4.06 / 5.80 m in 3 s): at most 2 snaps of one copy in
            // 10 s from the first; a third due in that window is not taken (the caller logs world_drive=vert_stop once).
            std::uint64_t win = 0;
            unsigned n = 0;
            const bool a0 = sw::DriveVertAllow(10000, win, n);
            sw::DriveVertCount(10000, win, n);
            const bool a1 = sw::DriveVertAllow(11500, win, n);
            sw::DriveVertCount(11500, win, n);
            const bool a2 = sw::DriveVertAllow(13000, win, n), a3 = sw::DriveVertAllow(19999, win, n);
            const bool a4 = sw::DriveVertAllow(20000, win, n);
            Check(a0 && a1 && !a2 && !a3 && a4 && n == 0 && win == 0,
                  "drive_vert: at most 2 snaps of a copy in 10 s from the first; the window then starts over");
            sw::DriveVertCount(20100, win, n);
            Check(win == 20100 && n == 1, "drive_vert: the next snap opens a new window");
            char reply[64]{};
            const bool b1 = sw::FormatRefusal(reply, sizeof(reply), "backoff", 2500) &&
                            std::strcmp(reply, "return {refused=\"backoff\",b=2500}") == 0;
            const bool b0 = sw::FormatRefusal(reply, sizeof(reply), "relayed", 0) && std::strcmp(reply, "return {refused=\"relayed\"}") == 0;
            Check(b1 && b0, "place_reach: a backoff refusal names the ms left of the guard's backoff (b)");
        }
        Check(sw::DriveAdmit({false, true, false, 30010, true, false}) == nullptr &&
                  sw::DriveAdmit({true, false, true, 30050, true, true}) == nullptr,
              "drive: a play guest; a lab host with soldier_owner");
        // FM1: the mirror drive (mode 2) needs soldier_act and is a guest's only; it never steps under 5 m, the whole way
        // beyond.
        {
            sw::DriveRule m{false, true, false, 30010, true, false};
            m.mirror = true;
            const char* noKey = sw::DriveAdmit(m);
            m.act_key = true;
            const char* ok = sw::DriveAdmit(m);
            m.host = true;
            m.owner = true;
            const char* host = sw::DriveAdmit(m);
            Check(noKey && std::strcmp(noKey, "mirror") == 0 && ok == nullptr && host && std::strcmp(host, "mirror") == 0,
                  "FM1: the mirror drive needs soldier_act, a guest only");
            const float origin[3] = {0, 10, 0}, inside[3] = {4.9f, 10, 0}, beyond[3] = {5.2f, 10, 0};
            const auto rest = sw::DriveStep(origin, inside, false, sw::kMirrorSnapM, sw::kMirrorSnapM, 0.0f, sw::kMirrorSnapM, 0.1f);
            const auto snap = sw::DriveStep(origin, beyond, false, sw::kMirrorSnapM, sw::kMirrorSnapM, 0.0f, sw::kMirrorSnapM, 0.1f);
            Check(!rest.move && snap.move && snap.hard && snap.pos[0] == beyond[0], "FM1: no step under 5 m, the whole way beyond");
        }
        Check(sw::DriveAdmit({false, true, false, 30010, true, false, true, true}) == nullptr &&
                  std::strcmp(sw::DriveAdmit({false, true, false, 30010, true, false, true, false}), "puppet") == 0 &&
                  std::strcmp(sw::DriveAdmit({false, true, true, 30010, true, true, true, true}), "puppet") == 0,
              "drive: the M7 puppet drive only on a guest with soldier_puppet");
        Check(std::strcmp(sw::DriveAdmit({false, true, true, 30010, true, false}), "disabled") == 0 &&
                  std::strcmp(sw::DriveAdmit({false, true, false, 30010, false, false}), "disabled") == 0 &&
                  std::strcmp(sw::DriveAdmit({false, true, false, 10033, true, false}), "mission") == 0 &&
                  std::strcmp(sw::DriveAdmit({false, false, false, 30010, true, false}), "role") == 0,
              "drive: a host without soldier_owner, the key off, a story mission, neither play nor lab");
        sw::Gate g{};
        Check(g.AdmitDrive(1100) == nullptr && g.go_id == 1100, "drive gate: admits and names the guard");
        sw::Result drive_bad{};
        drive_bad.refusal = "anomaly";
        g.Record(drive_bad);
        Check(g.AdmitDrive(1100) == nullptr, "drive gate: one anomaly");
        g.Record(drive_bad);
        Check(std::strcmp(g.AdmitDrive(1100), "soldier") == 0 && g.AdmitDrive(1101) == nullptr, "drive gate: the second latches the guard");
    }
    // Enemy sync M3 (docs/world-sync-design.md 7.9): the host's warp of its own copy of a guest-owned guard.
    for (const unsigned short mission : follow_missions) {
        Check(sw::HostAdmit({false, true, true, mission, true, true}) == nullptr, "owner: a play correction on the host");
        Check(sw::HostAdmit({true, false, true, mission, true, true}) == nullptr, "owner: a lab correction on the host");
    }
    Check(std::strcmp(sw::HostAdmit({false, true, false, 30010, true, true}), "role") == 0, "owner: never on the guest");
    Check(std::strcmp(sw::HostAdmit({false, false, true, 30010, true, true}), "role") == 0, "owner: neither play nor lab");
    Check(std::strcmp(sw::HostAdmit({false, true, true, 30010, true, false}), "disabled") == 0, "owner: the key off");
    Check(std::strcmp(sw::HostAdmit({false, true, true, 10033, true, true}), "mission") == 0 &&
              std::strcmp(sw::HostAdmit({false, true, true, 40050, true, true}), "mission") == 0,
          "owner: never in a story mission or an ACC");
    Check(std::strcmp(sw::HostAdmit({false, true, true, 30010, false, true}), "kind") == 0, "owner: corrections only");
    // The nearer player (the follow and staff range) and the 250 m far bound.
    const float target[3] = {100.0f, 0.0f, 0.0f}, host_at[3] = {0.0f, 0.0f, 0.0f}, self_at[3] = {90.0f, 0.0f, 0.0f};
    bool self_nearer = false;
    Check(std::fabs(sw::NearerSquared(target, host_at, self_at, true, true, self_nearer) - 100.0f) < 1e-3f && self_nearer,
          "the guest's own player is nearer");
    Check(std::fabs(sw::NearerSquared(target, host_at, self_at, true, false, self_nearer) - 10000.0f) < 1e-2f && !self_nearer,
          "no own pose: the host's remote pose");
    const float host_near[3] = {99.0f, 0.0f, 0.0f};
    Check(std::fabs(sw::NearerSquared(target, host_near, self_at, true, true, self_nearer) - 1.0f) < 1e-3f && !self_nearer,
          "the host nearer: the host's remote pose");
    Check(std::fabs(sw::NearerSquared(target, host_near, self_at, false, true, self_nearer) - 100.0f) < 1e-3f && self_nearer,
          "a stale remote pose: only the guest's own player counts");
    Check(!(sw::NearerSquared(target, host_near, self_at, false, false, self_nearer) <= sw::kFollowFarM * sw::kFollowFarM) &&
          !self_nearer, "neither pose: beyond every bound");
    const float far_target[3] = {400.0f, 0.0f, 0.0f};
    Check(!(sw::NearerSquared(far_target, host_at, self_at, true, true, self_nearer) <= sw::kFollowFarM * sw::kFollowFarM) &&
          sw::kFollowFarM == 250.0f, "a target 310 m from the nearer player is beyond the far bound");
    Check(sw::GuestMayWarp(true, false, true) && sw::GuestMayWarp(false, true, false) && sw::GuestMayWarp(true, false, false) &&
          !sw::GuestMayWarp(false, false, true), "Verify runs for a play or lab guest");
    VirtualFree(image, 0, MEM_RELEASE);
    // Enemy round 8 track A (range_flat, docs/world-sync-design.md 7.22): run ddcdec94, the guest fell 178 m below the
    // outpost (x/z 436.1, 2181.7) and stayed the owner (x/z rule) of guards 5-30 m from them on the ledge; the 3D rule
    // made the host (70 m away) the nearer player and refused every placement `range`. On the ground plane the guest is.
    {
        const float guard[3] = {440.8f, 270.0f, 2175.8f}, host_p[3] = {418.3f, 268.3f, 2083.5f}, guest_p[3] = {436.1f, 92.1f, 2181.7f};
        bool g_near = true;
        const float d3 = sw::NearerSquared(guard, host_p, guest_p, true, true, g_near);
        Check(!g_near && !(d3 <= sw::kMaxRangeLimitM * sw::kMaxRangeLimitM), "3D: the fallen guest is not nearer; out of range");
        const float flat = sw::NearerSquaredFlat(guard, host_p, guest_p, true, true, g_near);
        Check(g_near && std::fabs(flat - (4.7f * 4.7f + 5.9f * 5.9f)) < 0.05f, "flat: the guest on the ground plane, 7.5 m");
        Check(std::fabs(sw::NearerSquaredFlat(guard, host_p, guest_p, true, false, g_near) - (22.5f * 22.5f + 92.3f * 92.3f)) < 1.0f &&
                  !g_near, "flat: a stale guest pose leaves the host's leg");
        sw::Request r{};
        for (unsigned i = 0; i < 3; ++i) { r.pos[i] = guard[i]; r.player[i] = guest_p[i]; }
        sw::FlattenRange(r);
        Check(r.player[1] == r.pos[1] && r.player[0] == guest_p[0] && r.player[2] == guest_p[2],
              "FlattenRange: Warp's range is then measured on the ground plane");
    }
    std::printf("soldier_warp_tests: PASS %u checks\n", checks);
    return 0;
}
