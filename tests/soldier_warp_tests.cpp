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
#include "soldier_warp_contract.hpp"

namespace {
namespace c = mgscoop::soldier_warp_contract;
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
    sw::Gate latch{};
    Check(latch.Admit(1, go) == nullptr && std::strcmp(latch.Record(r), "anomaly") == 0 &&
          std::strcmp(latch.Admit(2, go), "latched") == 0, "an anomaly latches the feature off");
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
    VirtualFree(image, 0, MEM_RELEASE);
    std::printf("soldier_warp_tests: PASS %u checks\n", checks);
    return 0;
}
