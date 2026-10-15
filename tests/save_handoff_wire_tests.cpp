// Join-fix phase 2 save handoff tests (spec track A): the channel-4 codec
// (round trips, golden bytes, every P2-5 refusal), the P2-6 slot table and
// set hash against tests/fixtures/handoff-set.json, IsProgressName and
// RemoteBuildAtLeast. Pure; no Win32.
#include "mgscoop/save_handoff_wire.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mgscoop/link_crypto.hpp"

#ifndef MGS_HANDOFF_SET_JSON
#error "MGS_HANDOFF_SET_JSON must name tests/fixtures/handoff-set.json"
#endif

namespace {
namespace wire = mgscoop::save_handoff_wire;
using Bytes = std::vector<std::uint8_t>;

unsigned checks = 0;
void Check(bool value, const char* what, int line) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u (line %d): %s\n", checks, line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

wire::FileEntry Entry(std::uint8_t slot, std::uint32_t size, std::uint8_t fill) {
    wire::FileEntry e{};
    e.slot = slot;
    e.size = size;
    for (unsigned i = 0; i < wire::kShaSize; ++i) e.sha256[i] = static_cast<std::uint8_t>(fill + i);
    return e;
}

wire::Message StateMsg() {
    wire::Message m{};
    m.kind = wire::Kind::state;
    m.xfer = 0;
    m.state.role = wire::Role::guest;
    m.state.phase = wire::Phase::title;
    m.state.story = wire::Story::open;
    m.state.mode = wire::Mode::scene;
    m.state.mission = 40050;
    m.state.bound_mission = 30050;
    m.state.set_known = true;
    m.state.need = 1;
    m.state.elig = wire::Elig::unknown;
    for (unsigned i = 0; i < wire::kShaSize; ++i) m.state.set[i] = static_cast<std::uint8_t>(0xA0 + i);
    return m;
}
wire::Message OfferMsg(unsigned n = 6) {
    wire::Message m{};
    m.kind = wire::Kind::offer;
    m.xfer = 0x0A1B2C3Du;
    m.offer.n = static_cast<std::uint8_t>(n);
    m.offer.bound_mission = 30010;
    std::uint32_t total = 0;
    for (unsigned i = 0; i < n; ++i) {
        m.offer.files[i] = Entry(static_cast<std::uint8_t>(i), 1000 + i * 17, static_cast<std::uint8_t>(i * 40));
        total += m.offer.files[i].size;
    }
    m.offer.total = total;
    for (unsigned i = 0; i < wire::kShaSize; ++i) m.offer.set[i] = static_cast<std::uint8_t>(0x55 ^ i);
    return m;
}
wire::Message Small(wire::Kind kind, std::uint8_t value, std::uint8_t reason = 0) {
    wire::Message m{};
    m.kind = kind;
    m.xfer = 0xDEADBEEFu;
    switch (kind) {
    case wire::Kind::want: m.want.mask = value; break;
    case wire::Kind::decline: m.decline.reason = static_cast<wire::DeclineReason>(value); break;
    case wire::Kind::result:
        m.result.status = static_cast<wire::ResultStatus>(value);
        m.result.reason = static_cast<wire::Fail>(reason);
        break;
    case wire::Kind::abort: m.abort.reason = static_cast<wire::AbortReason>(value); break;
    case wire::Kind::end: m.end.total = 608012; break;
    default: break;
    }
    return m;
}
std::uint8_t g_payload[wire::kChunk];
wire::Message ChunkMsg(std::uint16_t len = 1000, std::uint32_t offset = 4000) {
    for (unsigned i = 0; i < wire::kChunk; ++i) g_payload[i] = static_cast<std::uint8_t>(i * 7 + 3);
    wire::Message m{};
    m.kind = wire::Kind::chunk;
    m.xfer = 0x01020304u;
    m.chunk.slot = 4;
    m.chunk.len = len;
    m.chunk.offset = offset;
    m.chunk.payload = g_payload;
    return m;
}

Bytes Enc(const wire::Message& m) {
    Bytes out(wire::kMaxMessage + 64);
    const std::size_t n = wire::Encode(m, out.data(), out.size());
    out.resize(n);
    return out;
}
wire::Decode Dec(const Bytes& b, wire::Message* out = nullptr) {
    wire::Message m{};
    const wire::Decode d = wire::DecodeMessage(b.data(), b.size(), m);
    if (out) *out = m;
    return d;
}
bool Refused(const Bytes& b) { return Dec(b) != wire::Decode::ok; }
void PutU32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
void PutU16(Bytes& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v);
    b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

// ---- Round trips and golden bytes.
void TestRoundTrips() {
    {  // STATE
        const wire::Message m = StateMsg();
        const Bytes b = Enc(m);
        CHECK(b.size() == wire::kStateSize);
        // Golden header and fields.
        const std::uint8_t golden[] = {0x53, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  // magic version kind flags xfer=0
                                       0x02, 0x02, 0x02, 0x02,                          // role guest, title, open, scene
                                       0x72, 0x9C, 0x00, 0x00,                          // mission 40050 (0x9C72)
                                       0x62, 0x75, 0x00, 0x00,                          // bound 30050 (0x7562)
                                       0x01, 0x01, 0x00, 0x00};                         // set_known need elig reserved
        CHECK(std::memcmp(b.data(), golden, sizeof(golden)) == 0);
        CHECK(b[24] == 0xA0 && b[55] == 0xA0 + 31);
        for (std::size_t i = 56; i < 64; ++i) CHECK(b[i] == 0);
        wire::Message d{};
        CHECK(Dec(b, &d) == wire::Decode::ok);
        CHECK(d.kind == wire::Kind::state && d.xfer == 0);
        CHECK(d.state.role == wire::Role::guest && d.state.phase == wire::Phase::title && d.state.story == wire::Story::open);
        CHECK(d.state.mode == wire::Mode::scene && d.state.mission == 40050 && d.state.bound_mission == 30050);
        CHECK(d.state.set_known && d.state.need == 1 && d.state.elig == wire::Elig::unknown);
        CHECK(std::memcmp(d.state.set, m.state.set, wire::kShaSize) == 0);
        // Host STATE with every elig value.
        for (unsigned e = 0; e <= 9; ++e) {
            wire::Message h = StateMsg();
            h.state.role = wire::Role::host;
            h.state.need = 0;
            h.state.elig = static_cast<wire::Elig>(e);
            wire::Message hd{};
            CHECK(Dec(Enc(h), &hd) == wire::Decode::ok && hd.state.elig == h.state.elig);
        }
        // set_known 0 carries a zero set.
        wire::Message u = StateMsg();
        u.state.set_known = false;
        std::memset(u.state.set, 0, sizeof(u.state.set));
        wire::Message ud{};
        CHECK(Dec(Enc(u), &ud) == wire::Decode::ok && !ud.state.set_known);
    }
    for (unsigned n = 1; n <= 6; ++n) {  // OFFER n = 1..6
        const wire::Message m = OfferMsg(n);
        const Bytes b = Enc(m);
        CHECK(b.size() == wire::kOfferBaseSize + wire::kOfferEntrySize * n);
        wire::Message d{};
        CHECK(Dec(b, &d) == wire::Decode::ok);
        CHECK(d.kind == wire::Kind::offer && d.xfer == m.xfer && d.offer.n == n && d.offer.total == m.offer.total);
        CHECK(d.offer.bound_mission == 30010 && std::memcmp(d.offer.set, m.offer.set, wire::kShaSize) == 0);
        for (unsigned i = 0; i < n; ++i) {
            CHECK(d.offer.files[i].slot == m.offer.files[i].slot && d.offer.files[i].size == m.offer.files[i].size);
            CHECK(std::memcmp(d.offer.files[i].sha256, m.offer.files[i].sha256, wire::kShaSize) == 0);
        }
    }
    {  // OFFER without slot 0 but with slot 1 is fine.
        wire::Message m = OfferMsg(2);
        m.offer.files[0] = Entry(1, 10, 1);
        m.offer.files[1] = Entry(5, 20, 2);
        m.offer.total = 30;
        CHECK(Dec(Enc(m)) == wire::Decode::ok);
    }
    for (std::uint8_t mask : {std::uint8_t{1}, std::uint8_t{0x3F}, std::uint8_t{0x11}}) {  // WANT
        wire::Message d{};
        const Bytes b = Enc(Small(wire::Kind::want, mask));
        CHECK(b.size() == wire::kSmallSize && Dec(b, &d) == wire::Decode::ok && d.want.mask == mask && d.xfer == 0xDEADBEEFu);
    }
    for (unsigned r = 1; r <= 8; ++r) {  // DECLINE
        wire::Message d{};
        CHECK(Dec(Enc(Small(wire::Kind::decline, static_cast<std::uint8_t>(r))), &d) == wire::Decode::ok);
        CHECK(static_cast<unsigned>(d.decline.reason) == r);
    }
    {  // CHUNK golden header and payload
        const wire::Message m = ChunkMsg();
        const Bytes b = Enc(m);
        CHECK(b.size() == wire::kChunkHeaderSize + 1000);
        const std::uint8_t golden[] = {0x53, 0x01, 0x05, 0x00, 0x04, 0x03, 0x02, 0x01,   // xfer 0x01020304
                                       0x04, 0x00, 0xE8, 0x03, 0xA0, 0x0F, 0x00, 0x00};  // slot 4, len 1000, offset 4000
        CHECK(std::memcmp(b.data(), golden, sizeof(golden)) == 0);
        CHECK(std::memcmp(b.data() + 16, g_payload, 1000) == 0);
        wire::Message d{};
        CHECK(Dec(b, &d) == wire::Decode::ok);
        CHECK(d.chunk.slot == 4 && d.chunk.len == 1000 && d.chunk.offset == 4000);
        CHECK(d.chunk.payload == b.data() + 16);
        // One byte and the largest offset that still fits.
        CHECK(Dec(Enc(ChunkMsg(1, 0))) == wire::Decode::ok);
        CHECK(Dec(Enc(ChunkMsg(1000, wire::kMaxFileBytes - 1000))) == wire::Decode::ok);
        // In-place encode (payload aliasing out + 16).
        Bytes inplace(wire::kMaxMessage);
        for (unsigned i = 0; i < 10; ++i) inplace[16 + i] = static_cast<std::uint8_t>(0xC0 + i);
        wire::Message c = ChunkMsg(10, 0);
        c.chunk.payload = inplace.data() + 16;
        CHECK(wire::Encode(c, inplace.data(), inplace.size()) == 26 && inplace[16] == 0xC0 && inplace[25] == 0xC9);
    }
    {  // END
        wire::Message d{};
        CHECK(Dec(Enc(Small(wire::Kind::end, 0)), &d) == wire::Decode::ok && d.end.total == 608012);
    }
    {  // RESULT
        wire::Message d{};
        CHECK(Dec(Enc(Small(wire::Kind::result, 0)), &d) == wire::Decode::ok && d.result.status == wire::ResultStatus::staged);
        CHECK(Dec(Enc(Small(wire::Kind::result, 1)), &d) == wire::Decode::ok && d.result.status == wire::ResultStatus::exiting);
        CHECK(Dec(Enc(Small(wire::Kind::result, 2, 1)), &d) == wire::Decode::ok && d.result.status == wire::ResultStatus::failed &&
              d.result.reason == wire::Fail::hash_mismatch);
        CHECK(Dec(Enc(Small(wire::Kind::result, 2, 12)), &d) == wire::Decode::ok && d.result.reason == wire::Fail::partner);
    }
    for (unsigned r = 1; r <= 7; ++r) {  // ABORT
        wire::Message d{};
        CHECK(Dec(Enc(Small(wire::Kind::abort, static_cast<std::uint8_t>(r))), &d) == wire::Decode::ok);
        CHECK(static_cast<unsigned>(d.abort.reason) == r);
    }
}

// ---- Every encoded message fits the link's 1024-byte reliable message.
void TestSizes() {
    CHECK(wire::kOfferMaxSize == 292 && wire::kMaxMessage == 1016);
    CHECK(Enc(OfferMsg(6)).size() == 292);
    CHECK(Enc(ChunkMsg(1000)).size() == 1016);
    for (const Bytes& b : {Enc(StateMsg()), Enc(OfferMsg(6)), Enc(ChunkMsg()), Enc(Small(wire::Kind::want, 1)),
                           Enc(Small(wire::Kind::end, 0)), Enc(Small(wire::Kind::abort, 1))})
        CHECK(!b.empty() && b.size() <= 1024);
    // Capacity too small: nothing.
    std::uint8_t tiny[63];
    CHECK(wire::Encode(StateMsg(), tiny, sizeof(tiny)) == 0);
    std::uint8_t mid[291];
    CHECK(wire::Encode(OfferMsg(6), mid, sizeof(mid)) == 0);
}

// ---- P2-5 refusals.
void TestRefusals() {
    const Bytes state = Enc(StateMsg());
    const Bytes offer = Enc(OfferMsg(6));
    const Bytes chunk = Enc(ChunkMsg());
    const Bytes smalls[] = {Enc(Small(wire::Kind::want, 1)), Enc(Small(wire::Kind::decline, 1)), Enc(Small(wire::Kind::end, 0)),
                            Enc(Small(wire::Kind::result, 0)), Enc(Small(wire::Kind::abort, 1))};
    // One byte short or long for every kind.
    for (const Bytes* b : {&state, &offer, &chunk, &smalls[0], &smalls[1], &smalls[2], &smalls[3], &smalls[4]}) {
        Bytes s = *b;
        s.pop_back();
        CHECK(Dec(s) == wire::Decode::size);
        Bytes l = *b;
        l.push_back(0);
        CHECK(Dec(l) == wire::Decode::size);
    }
    // Shorter than the header.
    CHECK(Dec(Bytes(state.begin(), state.begin() + 7)) == wire::Decode::short_);
    CHECK(Dec(Bytes()) == wire::Decode::short_);
    wire::Message ignored{};
    CHECK(wire::DecodeMessage(nullptr, 64, ignored) == wire::Decode::short_);
    // Header-only OFFER / CHUNK.
    CHECK(Dec(Bytes(offer.begin(), offer.begin() + 8)) == wire::Decode::size);
    CHECK(Dec(Bytes(chunk.begin(), chunk.begin() + 15)) == wire::Decode::size);
    {  // magic, version 2, kind 0 / 9, flags
        Bytes b = state;
        b[0] = 0x54;
        CHECK(Dec(b) == wire::Decode::magic);
        b = state;
        b[1] = 2;
        CHECK(Dec(b) == wire::Decode::version);
        b = state;
        b[2] = 0;
        CHECK(Dec(b) == wire::Decode::kind);
        b[2] = 9;
        CHECK(Dec(b) == wire::Decode::kind);
        b = state;
        b[3] = 1;
        CHECK(Dec(b) == wire::Decode::kind);
    }
    {  // xfer: nonzero on STATE, zero elsewhere.
        Bytes b = state;
        b[4] = 1;
        CHECK(Dec(b) == wire::Decode::bounds);
        for (const Bytes* m : {&offer, &chunk, &smalls[0], &smalls[1], &smalls[2], &smalls[3], &smalls[4]}) {
            Bytes z = *m;
            PutU32(z, 4, 0);
            CHECK(Dec(z) == wire::Decode::bounds);
        }
    }
    {  // STATE fields and pads.
        const struct { std::size_t at; std::uint8_t value; } bad[] = {
            {8, 0}, {8, 3}, {9, 4}, {10, 3}, {11, 4}, {20, 2}, {21, 2}, {22, 10}, {23, 1}, {56, 1}, {63, 1},
        };
        for (const auto& c : bad) {
            Bytes b = state;
            b[c.at] = c.value;
            CHECK(Dec(b) == wire::Decode::bounds);
        }
        Bytes b = state;
        PutU32(b, 12, 100000);  // mission > 99999
        CHECK(Dec(b) == wire::Decode::bounds);
        b = state;
        PutU32(b, 16, 100000);
        CHECK(Dec(b) == wire::Decode::bounds);
        b = state;
        b[20] = 0;  // set_known 0 with a nonzero set
        CHECK(Dec(b) == wire::Decode::bounds);
    }
    {  // OFFER: n = 0 / 7, pads, slot 6, duplicate / unsorted slots, sizes, sums, no slot 0/1.
        Bytes b = offer;
        b[8] = 7;
        CHECK(Dec(b) == wire::Decode::bounds);
        Bytes seven = offer;
        seven.resize(wire::kOfferBaseSize + wire::kOfferEntrySize * 7);
        seven[8] = 7;
        CHECK(Refused(seven));
        b = offer;
        b[8] = 0;
        CHECK(Dec(b) == wire::Decode::bounds);
        for (std::size_t at : {std::size_t{9}, std::size_t{10}, std::size_t{11}, wire::kOfferBaseSize + 1, wire::kOfferBaseSize + 3}) {
            b = offer;
            b[at] = 1;
            CHECK(Dec(b) == wire::Decode::bounds);
        }
        auto entry = [](std::size_t i) { return wire::kOfferBaseSize + wire::kOfferEntrySize * i; };
        b = offer;
        b[entry(5)] = 6;  // slot 6
        CHECK(Dec(b) == wire::Decode::bounds);
        b = offer;
        b[entry(2)] = 1;  // duplicate slot 1
        CHECK(Dec(b) == wire::Decode::bounds);
        b = offer;
        b[entry(0)] = 1;  // unsorted (1, 1 ...)
        b[entry(1)] = 0;
        CHECK(Dec(b) == wire::Decode::bounds);
        // size 0 (total adjusted so only the size is wrong)
        wire::Message m = OfferMsg(2);
        m.offer.total -= m.offer.files[1].size;
        Bytes z = Enc(OfferMsg(2));
        PutU32(z, entry(1) + 4, 0);
        PutU32(z, 12, m.offer.total);
        CHECK(Dec(z) == wire::Decode::bounds);
        // size 4 MiB + 1 (total adjusted)
        Bytes big = Enc(OfferMsg(1));
        PutU32(big, entry(0) + 4, wire::kMaxFileBytes + 1);
        PutU32(big, 12, wire::kMaxFileBytes + 1);
        CHECK(Dec(big) == wire::Decode::bounds);
        // exactly 4 MiB is fine; two of them (8 MiB total) too; plus one byte more is not.
        wire::Message two = OfferMsg(2);
        two.offer.files[0].size = wire::kMaxFileBytes;
        two.offer.files[1].size = wire::kMaxFileBytes;
        two.offer.total = wire::kMaxTotal;
        CHECK(Dec(Enc(two)) == wire::Decode::ok);
        wire::Message three = OfferMsg(3);
        three.offer.files[0].size = wire::kMaxFileBytes;
        three.offer.files[1].size = wire::kMaxFileBytes;
        three.offer.files[2].size = 1;
        three.offer.total = wire::kMaxTotal + 1;
        CHECK(wire::Encode(three, b.data(), b.size()) == 0);
        Bytes over = Enc(OfferMsg(3));
        PutU32(over, entry(0) + 4, wire::kMaxFileBytes);
        PutU32(over, entry(1) + 4, wire::kMaxFileBytes);
        PutU32(over, entry(2) + 4, 1);
        PutU32(over, 12, wire::kMaxTotal + 1);
        CHECK(Dec(over) == wire::Decode::bounds);
        // sum mismatch
        b = offer;
        PutU32(b, 12, OfferMsg(6).offer.total + 1);
        CHECK(Dec(b) == wire::Decode::bounds);
        // no slot 0/1
        wire::Message noprimary = OfferMsg(2);
        noprimary.offer.files[0] = Entry(2, 10, 1);
        noprimary.offer.files[1] = Entry(4, 10, 2);
        noprimary.offer.total = 20;
        CHECK(wire::Encode(noprimary, b.data(), b.size()) == 0);
        Bytes np = Enc(OfferMsg(2));
        np[entry(0)] = 2;
        np[entry(1)] = 4;
        CHECK(Dec(np) == wire::Decode::bounds);
        // bound mission > 99999
        b = offer;
        PutU32(b, 16, 100000);
        CHECK(Dec(b) == wire::Decode::bounds);
    }
    {  // CHUNK: len 0 / 1001, pad, slot 6, offset beyond 4 MiB.
        Bytes b = chunk;
        PutU16(b, 10, 0);
        CHECK(Dec(Bytes(b.begin(), b.begin() + 16)) == wire::Decode::bounds);
        CHECK(Dec(b) == wire::Decode::bounds);
        Bytes l = chunk;
        l.push_back(0);
        PutU16(l, 10, 1001);
        CHECK(Dec(l) == wire::Decode::bounds);
        b = chunk;
        b[9] = 1;
        CHECK(Dec(b) == wire::Decode::bounds);
        b = chunk;
        b[8] = 6;
        CHECK(Dec(b) == wire::Decode::bounds);
        b = chunk;
        PutU32(b, 12, wire::kMaxFileBytes - 999);
        CHECK(Dec(b) == wire::Decode::bounds);
        CHECK(Enc(ChunkMsg(0)).empty());
        CHECK(Enc(ChunkMsg(1001)).empty());
        wire::Message nopayload = ChunkMsg();
        nopayload.chunk.payload = nullptr;
        CHECK(Enc(nopayload).empty());
    }
    {  // WANT mask 0 / bit 6; DECLINE 0 / 9; RESULT; ABORT 0 / 8; END 0 / > 8 MiB; pads.
        Bytes b = smalls[0];
        b[8] = 0;
        CHECK(Dec(b) == wire::Decode::bounds);
        b[8] = 0x40;
        CHECK(Dec(b) == wire::Decode::bounds);
        b = smalls[1];
        b[8] = 0;
        CHECK(Dec(b) == wire::Decode::bounds);
        b[8] = 9;
        CHECK(Dec(b) == wire::Decode::bounds);
        b = smalls[3];
        b[8] = 3;  // status 3
        CHECK(Dec(b) == wire::Decode::bounds);
        b = smalls[3];
        b[9] = 1;  // staged with a reason
        CHECK(Dec(b) == wire::Decode::bounds);
        b = smalls[3];
        b[8] = 2;  // failed without a reason
        CHECK(Dec(b) == wire::Decode::bounds);
        b[9] = 13;  // reason above the last Fail
        CHECK(Dec(b) == wire::Decode::bounds);
        b = smalls[4];
        b[8] = 0;
        CHECK(Dec(b) == wire::Decode::bounds);
        b[8] = 8;
        CHECK(Dec(b) == wire::Decode::bounds);
        b = smalls[2];
        PutU32(b, 8, 0);
        CHECK(Dec(b) == wire::Decode::bounds);
        PutU32(b, 8, wire::kMaxTotal + 1);
        CHECK(Dec(b) == wire::Decode::bounds);
        for (unsigned k : {0u, 1u, 3u, 4u}) {
            for (std::size_t at : {std::size_t{9}, std::size_t{10}, std::size_t{11}}) {
                if (k == 3 && at == 9) continue;  // RESULT [9] is the reason
                b = smalls[k];
                b[at] = 1;
                CHECK(Dec(b) == wire::Decode::bounds);
            }
        }
        b = smalls[3];
        b[10] = 1;
        CHECK(Dec(b) == wire::Decode::bounds);
        CHECK(Enc(Small(wire::Kind::want, 0)).empty());
        CHECK(Enc(Small(wire::Kind::decline, 0)).empty());
        CHECK(Enc(Small(wire::Kind::result, 2, 0)).empty());
        CHECK(Enc(Small(wire::Kind::abort, 0)).empty());
    }
    {  // Encode xfer rules.
        wire::Message s = StateMsg();
        s.xfer = 1;
        CHECK(Enc(s).empty());
        wire::Message o = OfferMsg(1);
        o.xfer = 0;
        CHECK(Enc(o).empty());
    }
    {  // A refused decode never writes the output.
        wire::Message out = StateMsg();
        Bytes b = offer;
        b[0] = 0;
        CHECK(wire::DecodeMessage(b.data(), b.size(), out) == wire::Decode::magic);
        CHECK(out.kind == wire::Kind::state && out.state.mission == 40050);
    }
    CHECK(std::strcmp(wire::DecodeName(wire::Decode::short_), "short") == 0);
    CHECK(std::strcmp(wire::DecodeName(wire::Decode::bounds), "bounds") == 0);
}

// ---- Slot table and names.
void TestSlots() {
    const char* entries[] = {"287700/local/TPP_GAME_DATA0", "287700/local/TPP_GAME_DATA1", "287700/local/PERSONAL_DATA0",
                             "287700/local/PERSONAL_DATA1", "remote/TPP_GAME_DATA",        "remote/PERSONAL_DATA"};
    const char* paths[] = {"287700\\local\\TPP_GAME_DATA0", "287700\\local\\TPP_GAME_DATA1", "287700\\local\\PERSONAL_DATA0",
                           "287700\\local\\PERSONAL_DATA1", "remote\\TPP_GAME_DATA",         "remote\\PERSONAL_DATA"};
    for (std::uint8_t s = 0; s < wire::kSlots; ++s) {
        CHECK(std::strcmp(wire::SlotEntry(s), entries[s]) == 0);
        CHECK(std::strcmp(wire::SlotPath(s), paths[s]) == 0);
        const char* slash = std::strrchr(entries[s], '/');
        CHECK(std::strcmp(wire::SlotName(s), slash + 1) == 0);
        CHECK(wire::IsProgressName(wire::SlotName(s)));
        CHECK(wire::SlotIsRemote(s) == (s >= 4));
    }
    CHECK(!wire::SlotEntry(6) && !wire::SlotPath(6) && !wire::SlotName(255));
    // IsProgressName (Share-Save's ^(TPP_GAME_DATA[0-9]?|PERSONAL_DATA[0-9]?)$).
    for (const char* ok : {"TPP_GAME_DATA", "TPP_GAME_DATA0", "TPP_GAME_DATA9", "PERSONAL_DATA", "PERSONAL_DATA0", "PERSONAL_DATA9"})
        CHECK(wire::IsProgressName(ok));
    for (int d = 0; d <= 9; ++d) {
        char game[20], personal[20];
        std::snprintf(game, sizeof(game), "TPP_GAME_DATA%d", d);
        std::snprintf(personal, sizeof(personal), "PERSONAL_DATA%d", d);
        CHECK(wire::IsProgressName(game) && wire::IsProgressName(personal));
    }
    for (const char* bad : {"TPP_CONFIG_DATA0", "TPP_CONFIG_DATA", "TPP_GRAPHICS_CONFIG", "MGO_GAME_DATA0", "tpp_game_data0",
                            "Tpp_Game_Data", "personal_data", "..\\x", "..\\TPP_GAME_DATA0", "TPP_GAME_DATA10", "TPP_GAME_DATAx",
                            "TPP_GAME_DATA0.bak", "TPP_GAME_DAT", "", " TPP_GAME_DATA", "TPP_GAME_DATA0 ", "PERSONAL_DATA00"})
        CHECK(!wire::IsProgressName(bad));
    CHECK(!wire::IsProgressName(static_cast<const char*>(nullptr)));
    CHECK(wire::IsProgressName(L"TPP_GAME_DATA1") && wire::IsProgressName(L"PERSONAL_DATA") && wire::IsProgressName(L"PERSONAL_DATA3"));
    CHECK(!wire::IsProgressName(L"TPP_CONFIG_DATA0") && !wire::IsProgressName(L"tpp_game_data") && !wire::IsProgressName(L"..\\x"));
    CHECK(!wire::IsProgressName(static_cast<const wchar_t*>(nullptr)));
    // Names.
    CHECK(std::strcmp(wire::KindName(wire::Kind::chunk), "chunk") == 0);
    CHECK(std::strcmp(wire::RoleName(wire::Role::guest), "guest") == 0);
    CHECK(std::strcmp(wire::ModeName(wire::Mode::always), "always") == 0);
    CHECK(std::strcmp(wire::EligName(wire::Elig::continue_unsupported), "continue_unsupported") == 0);
    CHECK(std::strcmp(wire::DeclineName(wire::DeclineReason::same_scene), "same_scene") == 0);
    CHECK(std::strcmp(wire::ResultStatusName(wire::ResultStatus::exiting), "exiting") == 0);
    CHECK(std::strcmp(wire::AbortName(wire::AbortReason::not_title), "not_title") == 0);
    CHECK(std::strcmp(wire::FailName(wire::Fail::hash_mismatch), "hash_mismatch") == 0);
    CHECK(std::strcmp(wire::FailName(wire::Fail::partner), "partner") == 0);
}

// ---- tests/fixtures/handoff-set.json (python json.dumps output; CRLF tolerated).
std::string ReadFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}
std::size_t ValueAt(const std::string& doc, const char* key, std::size_t from) {
    const std::string needle = std::string("\"") + key + "\"";
    std::size_t at = doc.find(needle, from);
    if (at == std::string::npos) return at;
    at += needle.size();
    while (at < doc.size() && (doc[at] == ' ' || doc[at] == ':')) ++at;
    return at;
}
std::string StringAt(const std::string& doc, std::size_t at) {
    if (at == std::string::npos || doc[at] != '"') return {};
    const std::size_t end = doc.find('"', at + 1);
    return doc.substr(at + 1, end - at - 1);
}
unsigned long NumberAt(const std::string& doc, std::size_t at) { return std::strtoul(doc.c_str() + at, nullptr, 10); }

void TestFixture() {
    const std::string doc = ReadFile(MGS_HANDOFF_SET_JSON);
    CHECK(!doc.empty());
    CHECK(StringAt(doc, ValueAt(doc, "format", 0)) == "mgs-coop-handoff-set-fixture");
    wire::FileEntry files[wire::kMaxFiles]{};
    std::size_t pos = ValueAt(doc, "files", 0);
    CHECK(pos != std::string::npos);
    for (unsigned i = 0; i < 6; ++i) {
        const std::size_t slot_at = ValueAt(doc, "slot", pos);
        CHECK(slot_at != std::string::npos);
        const unsigned long slot = NumberAt(doc, slot_at);
        CHECK(slot == i);
        const std::string entry = StringAt(doc, ValueAt(doc, "entry", slot_at));
        CHECK(entry == wire::SlotEntry(static_cast<std::uint8_t>(i)));
        const std::string content = StringAt(doc, ValueAt(doc, "content_ascii", slot_at));
        const unsigned long bytes = NumberAt(doc, ValueAt(doc, "bytes", slot_at));
        const std::size_t sha_at = ValueAt(doc, "sha256", slot_at);
        const std::string sha = StringAt(doc, sha_at);
        CHECK(content.size() == bytes && content.find('\\') == std::string::npos);
        std::uint8_t digest[wire::kShaSize];
        mgscoop::link::Sha256Digest(content.data(), content.size(), digest);
        char hex[wire::kShaHexChars + 1];
        wire::FormatShaHex(digest, hex);
        CHECK(sha == hex);  // upper-case hex like Get-FileHash
        files[i].slot = static_cast<std::uint8_t>(i);
        files[i].size = static_cast<std::uint32_t>(bytes);
        CHECK(wire::ParseShaHex(sha.c_str(), sha.size(), files[i].sha256));
        pos = sha_at;
    }
    // The full set (the first set_sha256 after the files).
    const std::size_t set_at = ValueAt(doc, "set_sha256", pos);
    const std::string expected = StringAt(doc, set_at);
    std::uint8_t set[wire::kShaSize];
    CHECK(wire::SetSha256(files, 6, set));
    char hex[wire::kShaHexChars + 1];
    wire::FormatShaHex(set, hex);
    CHECK(expected == hex);
    // Subsets.
    unsigned subsets = 0;
    for (std::size_t at = ValueAt(doc, "slots", set_at); at != std::string::npos; at = ValueAt(doc, "slots", at)) {
        const std::size_t close = doc.find(']', at);
        wire::FileEntry sub[wire::kMaxFiles]{};
        unsigned n = 0;
        for (std::size_t p = at + 1; p < close; ++p)
            if (doc[p] >= '0' && doc[p] <= '9') sub[n++] = files[doc[p] - '0'];
        std::uint8_t subset[wire::kShaSize];
        CHECK(wire::SetSha256(sub, n, subset));
        wire::FormatShaHex(subset, hex);
        CHECK(StringAt(doc, ValueAt(doc, "set_sha256", close)) == hex);
        ++subsets;
        at = close;
    }
    CHECK(subsets == 3);
    // SetSha256 refusals: n = 0, n > 6, unsorted, duplicate, slot 6 (out zeroed).
    std::uint8_t out[wire::kShaSize];
    CHECK(!wire::SetSha256(files, 0, out));
    CHECK(!wire::SetSha256(files, 7, out));
    CHECK(!wire::SetSha256(nullptr, 1, out));
    wire::FileEntry swapped[2] = {files[1], files[0]};
    CHECK(!wire::SetSha256(swapped, 2, out));
    wire::FileEntry dup[2] = {files[1], files[1]};
    CHECK(!wire::SetSha256(dup, 2, out));
    wire::FileEntry six[1] = {files[0]};
    six[0].slot = 6;
    CHECK(!wire::SetSha256(six, 1, out));
    bool zero = true;
    for (std::uint8_t b : out) zero = zero && b == 0;
    CHECK(zero);
    // The size takes part: one byte more changes the set.
    wire::FileEntry grown[6];
    std::memcpy(grown, files, sizeof(grown));
    ++grown[3].size;
    CHECK(wire::SetSha256(grown, 6, out) && std::memcmp(out, set, sizeof(out)) != 0);
}

void TestHex() {
    std::uint8_t sha[wire::kShaSize];
    for (unsigned i = 0; i < wire::kShaSize; ++i) sha[i] = static_cast<std::uint8_t>(i * 9);
    char hex[wire::kShaHexChars + 1];
    wire::FormatShaHex(sha, hex);
    CHECK(std::strlen(hex) == 64 && std::strncmp(hex, "0009121B", 8) == 0);
    std::uint8_t back[wire::kShaSize]{};
    CHECK(wire::ParseShaHex(hex, 64, back) && std::memcmp(back, sha, sizeof(sha)) == 0);
    std::string lower(hex);
    for (char& c : lower) c = static_cast<char>(c >= 'A' && c <= 'F' ? c + 32 : c);
    CHECK(wire::ParseShaHex(lower.c_str(), 64, back) && std::memcmp(back, sha, sizeof(sha)) == 0);
    std::uint8_t keep[wire::kShaSize];
    std::memset(keep, 0x77, sizeof(keep));
    CHECK(!wire::ParseShaHex(hex, 63, keep));
    CHECK(!wire::ParseShaHex(nullptr, 64, keep));
    std::string bad(hex);
    bad[10] = 'G';
    CHECK(!wire::ParseShaHex(bad.c_str(), 64, keep));
    CHECK(keep[0] == 0x77);
}

void TestRemoteBuild() {
    CHECK(!wire::RemoteBuildAtLeast("0.12.6+c07d0a2", 0, 13, 0));
    CHECK(wire::RemoteBuildAtLeast("0.13.0+x", 0, 13, 0));
    CHECK(wire::RemoteBuildAtLeast("0.13.0", 0, 13, 0));
    CHECK(wire::RemoteBuildAtLeast("0.13.1+abc", 0, 13, 0));
    CHECK(wire::RemoteBuildAtLeast("0.14.0+abc", 0, 13, 0));
    CHECK(wire::RemoteBuildAtLeast("1.0.0", 0, 13, 0));
    CHECK(wire::RemoteBuildAtLeast("0.13.10-dirty", 0, 13, 9));
    CHECK(!wire::RemoteBuildAtLeast("0.9.99", 0, 13, 0));
    CHECK(wire::RemoteBuildAtLeast("0.12.6+c07d0a2", 0, 12, 6));
    CHECK(!wire::RemoteBuildAtLeast("0.12.5", 0, 12, 6));
    for (const char* garbage : {"", "garbage", "0.13", "0.13.", ".13.0", "v0.13.0", "0..13.0", "0.13.x", "-1.13.0", "0,13,0",
                                " 0.13.0", "9999999999.0.0"})
        CHECK(!wire::RemoteBuildAtLeast(garbage, 0, 13, 0));
    CHECK(!wire::RemoteBuildAtLeast(nullptr, 0, 0, 0));
    CHECK(wire::RemoteBuildAtLeast("0.0.0", 0, 0, 0));
    CHECK(wire::kHandoffMinMajor == 0 && wire::kHandoffMinMinor == 13 && wire::kHandoffMinPatch == 0);
}

}  // namespace

int main() {
    TestRoundTrips();
    TestSizes();
    TestRefusals();
    TestSlots();
    TestFixture();
    TestHex();
    TestRemoteBuild();
    std::printf("save_handoff_wire_tests: %u checks passed\n", checks);
    return 0;
}
