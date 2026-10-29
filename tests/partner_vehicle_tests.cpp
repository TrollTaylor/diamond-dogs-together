// WS3 vehicles (docs/vehicle-ws3.md): the vehicle unit/body reader on a
// synthetic address map (every link and refusal), the census line, the tilt
// gates and write, the image verification on a fixture image, the ride wire
// v3 addendum codec/store, and the seat-by-id plan and its id-then-flag write
// order. Nothing here touches MGSV; the fixture buffers are never executed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "mgscoop/partner_ride_seat.hpp"
#include "mgscoop/partner_ride_wire.hpp"
#include "mgscoop/partner_vehicle.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace v = mgscoop::partner_vehicle;
namespace rw = mgscoop::partner_ride_wire;
namespace prs = mgscoop::partner_ride_seat;
namespace c = mgscoop::partner_player_contract;
unsigned checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %u: %s\n", checks, message); std::exit(1); }
}

// A sparse byte map standing in for the game's memory: a read succeeds only
// when every byte was written (an unmapped pointer fails like a bad read).
struct Memory {
    std::map<std::uintptr_t, std::uint8_t> bytes;
    unsigned reads = 0;
    template <class T> void Put(std::uintptr_t at, T value) {
        unsigned char raw[sizeof(T)]{};
        std::memcpy(raw, &value, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes[at + i] = raw[i];
    }
    void Fill(std::uintptr_t at, std::size_t n, std::uint8_t value = 0) { for (std::size_t i = 0; i < n; ++i) bytes[at + i] = value; }
    void Erase(std::uintptr_t at, std::size_t n) { for (std::size_t i = 0; i < n; ++i) bytes.erase(at + i); }
    bool operator()(std::uintptr_t at, void* out, std::size_t n) noexcept {
        ++reads;
        auto* o = static_cast<unsigned char*>(out);
        for (std::size_t i = 0; i < n; ++i) {
            const auto it = bytes.find(at + i);
            if (it == bytes.end()) return false;
            o[i] = it->second;
        }
        return true;
    }
};

constexpr std::uintptr_t kImage = 0x140000000ull;
constexpr std::uint16_t kId = 25600;  // the sortie vehicle (type 50, index 0)
// The synthetic heap objects.
constexpr std::uintptr_t kRoot = 0x10000000, kWorld = 0x10001000, kManager = 0x10002000, kService = 0x10003000, kObject = 0x10004000,
                         kBitmap = 0x10005000, kRecords = 0x10006000, kV = 0x10008000, kRb = 0x10009000, kCats = 0x1000A000,
                         kCat = 0x1000B000, kSlots = 0x1000C000, kOtherBody = 0x1000D000;

// A complete, realized chain where the slot body IS the unit record (the census's hypothesis).
Memory World(bool same = true, std::uint8_t realized = 3) {
    Memory m;
    m.Put<std::uintptr_t>(kImage + v::kServiceRootRva, kRoot);
    m.Put<std::uintptr_t>(kRoot + 8, kWorld);
    m.Put<std::uintptr_t>(kWorld + 0x98, kManager);
    m.Put<std::uintptr_t>(kManager + 0x2A8, kService);
    m.Put<std::uintptr_t>(kService, kImage + v::kServiceVtRva);
    m.Put<std::uintptr_t>(kImage + v::kServiceVtRva + 0x18, kImage + v::kLookupRva);
    m.Put<std::uintptr_t>(kService + 0xB0, kObject);
    m.Put<std::uintptr_t>(kObject, kImage + v::kObjectVtRva);
    m.Put<std::uintptr_t>(kImage + v::kObjectVtRva, kImage + v::kCountFnRva);
    m.Put<std::uintptr_t>(kImage + v::kObjectVtRva + 0x78, kImage + v::kSetPosFnRva);
    m.Put<std::uint16_t>(kObject + 0x1C0, 8);
    m.Put<std::uintptr_t>(kObject + 0x28, kBitmap);
    m.Put<std::uint32_t>(kBitmap, 0x1);
    m.Put<std::uintptr_t>(kObject + 0x10, kRecords);
    m.Fill(kRecords, 8 * 0x80);
    const std::uintptr_t rec = kRecords;  // index 0
    const float pos[4] = {802.5f, 338.25f, 1642.0f, 1.5f};
    for (unsigned i = 0; i < 4; ++i) m.Put<float>(rec + 0x20 + 4 * i, pos[i]);
    m.Put<float>(rec + 0x44, 0.125f);
    m.Put<float>(rec + 0x48, -0.0625f);
    m.Put<std::uint8_t>(rec + 0x6C, 0x14);  // category 1, variant 4
    m.Put<std::uint8_t>(rec + 0x6E, realized);
    m.Put<std::uint8_t>(rec + 0x6F, 2);
    m.Put<std::uint16_t>(rec + 0x78, 0);
    m.Put<std::uintptr_t>(kObject + 0x1A8, kV);
    m.Put<std::uintptr_t>(kV, kImage + v::kVVtRvas[1]);
    m.Put<std::uintptr_t>(kImage + v::kVVtRvas[1] + 0x70, kImage + v::kVFnRva);
    m.Put<std::uintptr_t>(kV + 0x98, kRb);
    m.Put<std::uintptr_t>(kRb, kImage + v::kRbVtRvas[0]);
    m.Put<std::uintptr_t>(kImage + v::kRbVtRvas[0] + 0x78, kImage + v::kRbFnRva);
    m.Put<std::uintptr_t>(kRb + 0x70, kCats);
    m.Put<std::uintptr_t>(kCats + 8 * 1, kCat);
    m.Put<std::uintptr_t>(kCat, kImage + v::kCatVtRvas[2]);
    m.Put<std::uintptr_t>(kImage + v::kCatVtRvas[2] + 0x1F0, kImage + v::kCatFnRva);
    m.Put<std::uintptr_t>(kCat + 0x1C8, kSlots);
    m.Put<std::uintptr_t>(kSlots + 8 * 2, same ? rec : kOtherBody);
    m.Fill(kOtherBody, 0x80);
    m.Put<float>(kOtherBody + 0x44, 0.5f);
    return m;
}

void TestReader() {
    Memory m = World();
    v::Read r{};
    v::ReadVehicle(m, kImage, kId, r);
    Check(!r.refusal && !r.body_refusal && r.image == kImage, "the full chain reads");
    Check(r.unit.rec == kRecords && r.unit.type == 0x14 && v::Category(r.unit.type) == 1 && v::Variant(r.unit.type) == 4 &&
              r.unit.realized == 3 && r.unit.slot == 2 && r.unit.count == 8 && std::fabs(r.unit.pos[0] - 802.5f) < 1e-3f &&
              std::fabs(r.unit.pos[3] - 1.5f) < 1e-6f && r.unit.a44 == 0.125f && r.unit.a48 == -0.0625f,
          "the unit record fields (+0x20 pos, +0x2C yaw, +0x44/+0x48, +0x6C type, +0x6E realized, +0x6F slot)");
    Check(r.body.cat == kCat && r.body.body == kRecords && r.body.cat_fn == kImage + v::kCatFnRva && r.Same(),
          "the realized slot body (cat+0x1C8)[slot] is the record: same");
    v::Read again{};
    v::ReadVehicle(m, kImage, kId, again);
    Check(v::SameRead(r, again), "two reads of an unchanged chain are the same");
    // Index 1 of the same object (25601): its live bit is clear.
    v::Read other{};
    v::ReadVehicle(m, kImage, kId + 1, other);
    Check(other.refusal && std::strcmp(other.refusal, "live") == 0, "a record whose live bit is clear is refused (0xA272A0)");
    Memory d = World(false);
    v::Read distinct{};
    v::ReadVehicle(d, kImage, kId, distinct);
    Check(!distinct.refusal && !distinct.body_refusal && !distinct.Same() && distinct.body.body == kOtherBody && distinct.body.a44 == 0.5f,
          "a slot body distinct from the record reads but is not the same object");
    Check(!v::SameRead(r, distinct), "a different body is a different read");
    Memory u = World(true, 0xFF);
    v::Read unrealized{};
    v::ReadVehicle(u, kImage, kId, unrealized);
    Check(!unrealized.refusal && unrealized.body_refusal && std::strcmp(unrealized.body_refusal, "unrealized") == 0 && !unrealized.Same(),
          "an unrealized record (rec+0x6E == 0xFF) has no body");
    // Every link refuses with its own reason.
    struct Case { const char* why; void (*mutate)(Memory&); std::uint16_t id; bool body; };
    const Case cases[] = {
        {"type", [](Memory&) {}, 6656, false},
        {"type", [](Memory&) {}, 0xFFFF, false},
        {"service", [](Memory& mm) { mm.Erase(kManager + 0x2A8, 8); }, kId, false},
        {"service", [](Memory& mm) { mm.Put<std::uintptr_t>(kRoot + 8, 0); }, kId, false},
        {"service_vt", [](Memory& mm) { mm.Put<std::uintptr_t>(kService, kImage + 0x22AC8E8); }, kId, false},
        {"lookup", [](Memory& mm) { mm.Put<std::uintptr_t>(kImage + v::kServiceVtRva + 0x18, kImage + 0xA272A1); }, kId, false},
        {"object", [](Memory& mm) { mm.Put<std::uintptr_t>(kService + 0xB0, 0); }, kId, false},
        {"object_vt", [](Memory& mm) { mm.Put<std::uintptr_t>(kObject, kImage + 0x22ED930); }, kId, false},
        {"count_fn", [](Memory& mm) { mm.Put<std::uintptr_t>(kImage + v::kObjectVtRva, kImage + 0xB2DD31); }, kId, false},
        {"setpos_fn", [](Memory& mm) { mm.Put<std::uintptr_t>(kImage + v::kObjectVtRva + 0x78, 0); }, kId, false},
        {"index", [](Memory& mm) { mm.Put<std::uint16_t>(kObject + 0x1C0, 0); }, kId, false},
        {"index", [](Memory&) {}, static_cast<std::uint16_t>(25600 + 8), false},
        {"bitmap", [](Memory& mm) { mm.Erase(kBitmap, 4); }, kId, false},
        {"records", [](Memory& mm) { mm.Put<std::uintptr_t>(kObject + 0x10, 0); }, kId, false},
        {"record", [](Memory& mm) { mm.Erase(kRecords + 0x7F, 1); }, kId, false},
        {"v", [](Memory& mm) { mm.Put<std::uintptr_t>(kObject + 0x1A8, 0); }, kId, true},
        {"v_vt", [](Memory& mm) { mm.Put<std::uintptr_t>(kV, kImage + 0x2441000); }, kId, true},
        {"v_fn", [](Memory& mm) { mm.Put<std::uintptr_t>(kImage + v::kVVtRvas[1] + 0x70, 0); }, kId, true},
        {"rb", [](Memory& mm) { mm.Put<std::uintptr_t>(kV + 0x98, 0); }, kId, true},
        {"rb_vt", [](Memory& mm) { mm.Put<std::uintptr_t>(kRb, kImage + v::kRbVtRvas[0] + 8); }, kId, true},
        {"rb_fn", [](Memory& mm) { mm.Put<std::uintptr_t>(kImage + v::kRbVtRvas[0] + 0x78, kImage + 0x16FDDC4); }, kId, true},
        {"cat", [](Memory& mm) { mm.Put<std::uintptr_t>(kCats + 8, 0); }, kId, true},
        {"cat_vt", [](Memory& mm) { mm.Put<std::uintptr_t>(kCat, kImage + 0x22F7F98); }, kId, true},
        {"cat_fn", [](Memory& mm) { mm.Put<std::uintptr_t>(kImage + v::kCatVtRvas[2] + 0x1F0, kImage + 0xBA9711); }, kId, true},
        {"body", [](Memory& mm) { mm.Put<std::uintptr_t>(kSlots + 16, 0); }, kId, true},
    };
    for (const Case& k : cases) {
        Memory w = World();
        k.mutate(w);
        v::Read out{};
        v::ReadVehicle(w, kImage, k.id, out);
        const char* got = k.body ? out.body_refusal : out.refusal;
        Check(got && std::strcmp(got, k.why) == 0 && (!k.body || !out.refusal) && !out.Same(), k.why);
    }
    v::Read none{};
    Memory empty;
    v::ReadVehicle(empty, 0, kId, none);
    Check(none.refusal && std::strcmp(none.refusal, "image") == 0, "no image refuses");
}

// M4 VEHICLE_STATE: the life block through the object's vt+0x98 (0xB2DD00, pinned bytes) and
// the engine's ene_veh_state word (StoreToSVars 0xB35C90..0xB35D62).
constexpr std::uintptr_t kLife = 0x1000E000;
void LifeWorld(Memory& m) {
    m.Put<std::uintptr_t>(kImage + v::kObjectVtRva + 0x98, kImage + v::kLifeFnRva);
    for (unsigned i = 0; i < sizeof(v::kLifeFnBytes); ++i) m.Put<std::uint8_t>(kImage + v::kLifeFnRva + i, v::kLifeFnBytes[i]);
    m.Put<std::uintptr_t>(kObject + 0x150, kLife);
    m.Fill(kLife, 8 * 0x18);
    // Index 0 (25600): body 3000, four parts of 1000, one broken part (word 3), words 6..11 unused (0).
    const std::uint16_t words[12] = {3000, 1000, 1000, 0, 1000, 1000, 0, 0, 0, 0, 0, 0};
    for (unsigned i = 0; i < 12; ++i) m.Put<std::uint16_t>(kLife + 2 * i, words[i]);
    m.Put<std::uint16_t>(kLife + 0x18, 777);  // index 1 starts 0x18 further
}
void TestLife() {
    Check(v::SvState(0, 0) == 1 && v::SvState(0x40000, 0) == 0 && v::SvState(0x80000, 0) == 0 && v::SvState(0, 0x4000) == 3 &&
              v::SvState(0x8, 0) == 5 && v::SvState(0x200, 0) == 0x11 && v::SvState(0x4000, 0) == 0x201 && v::SvState(0x7E08, 0x4000) == 0x3F7 &&
              v::SvState(0x800004, 0) == 1 && v::SvState(0x100, 0) == 1,
          "SvState: bit 0 not 0xC0000, bit 1 rec+0x7C bit 14, bit 2 bit 3, bits 4..9 bits 9..14; other bits ignored");
    Memory m = World();
    LifeWorld(m);
    m.Put<std::uint32_t>(kRecords + 0x70, 0x00000208);
    m.Put<std::uint16_t>(kRecords + 0x7C, 0x4000);
    v::Read r{};
    v::ReadVehicle(m, kImage, kId, r);
    Check(!r.refusal && r.unit.flags70 == 0x208 && r.unit.flags7c == 0x4000, "the unit carries rec+0x70 and rec+0x7C");
    v::Life life{};
    v::ReadLife(m, r, life);
    Check(!life.refusal && life.words[0] == 3000 && life.words[1] == 1000 && life.words[3] == 0 && life.parts == 4 &&
              life.zero == ((1u << 3) | 0xFC0u) && life.state == (1 | 2 | 4 | 0x10),
          "the body life (word 0), the parts above 0, the zero-parts mask (bits 1..11) and the state word");
    // Index 1 reads 0x18 further (the vt+0x98 stride: index * 3 * 4 * 2).
    m.Put<std::uint32_t>(kBitmap, 0x3);
    m.Fill(kRecords + 0x80, 0x80);
    v::Read r1{};
    v::ReadVehicle(m, kImage, kId + 1, r1);
    v::Life l1{};
    v::ReadLife(m, r1, l1);
    Check(!r1.refusal && !l1.refusal && l1.words[0] == 777 && l1.state == 1, "index 1's block is 0x18 bytes on");
    struct Case { const char* why; void (*mutate)(Memory&); };
    const Case cases[] = {
        {"life_fn", [](Memory& mm) { mm.Put<std::uintptr_t>(kImage + v::kObjectVtRva + 0x98, kImage + v::kLifeFnRva + 1); }},
        {"life_fn", [](Memory& mm) { mm.Erase(kImage + v::kObjectVtRva + 0x98, 8); }},
        {"life_code", [](Memory& mm) { mm.Put<std::uint8_t>(kImage + v::kLifeFnRva + 9, 0x58); }},
        {"life_code", [](Memory& mm) { mm.Erase(kImage + v::kLifeFnRva + 23, 1); }},
        {"life_base", [](Memory& mm) { mm.Put<std::uintptr_t>(kObject + 0x150, 0); }},
        {"life_read", [](Memory& mm) { mm.Erase(kLife + 22, 1); }},
    };
    for (const Case& k : cases) {
        Memory w = World();
        LifeWorld(w);
        k.mutate(w);
        v::Read out{};
        v::ReadVehicle(w, kImage, kId, out);
        v::Life l{};
        v::ReadLife(w, out, l);
        Check(!out.refusal && l.refusal && std::strcmp(l.refusal, k.why) == 0 && l.words[0] == 0, k.why);
    }
    v::Read dead{};
    v::ReadVehicle(m, kImage, kId + 2, dead);
    v::Life dl{};
    v::ReadLife(m, dead, dl);
    Check(dl.refusal && std::strcmp(dl.refusal, "live") == 0, "a unit read refusal carries over");
}

void TestCensus() {
    Memory m = World();
    v::Read r{};
    v::ReadVehicle(m, kImage, kId, r);
    char line[512]{};
    const std::size_t n = v::FormatCensus("remote", r, line, sizeof(line));
    const std::string text(line);
    Check(n > 0 && n < 480 && text.rfind("partner_ride_census; who=remote; id=25600; ref=ok; type=0x14; cat=1; var=4; realized=3; slot=2; idx78=0; rec=0x", 0) == 0,
          "the census line head");
    Check(text.find("pos_mm=802500,338250,1642000; yaw_mrad=1500; a44_mrad=125; a48_mrad=-63;") != std::string::npos ||
              text.find("pos_mm=802500,338250,1642000; yaw_mrad=1500; a44_mrad=125; a48_mrad=-62;") != std::string::npos,
          "the record pose in mm/mrad");
    Check(text.find("body_ref=ok; v_vt=0x2441120; rb_vt=0x24416b0; cat_vt=0x22f8630; cat_fn=0xba9710;") != std::string::npos &&
              text.find("same=1\r\n") != std::string::npos,
          "the body chain as rvas and same=1");
    Memory u = World(true, 0xFF);
    v::Read unrealized{};
    v::ReadVehicle(u, kImage, kId, unrealized);
    v::FormatCensus("local", unrealized, line, sizeof(line));
    Check(std::string(line).find("who=local; id=25600; ref=ok;") != std::string::npos &&
              std::string(line).find("realized=255;") != std::string::npos && std::string(line).find("body_ref=unrealized;") != std::string::npos &&
              std::string(line).find("same=0") != std::string::npos,
          "an unrealized vehicle: body_ref=unrealized, same=0");
    char tiny[32]{};
    Check(v::FormatCensus("x", r, tiny, sizeof(tiny)) == 0 && tiny[0] == 0, "overflow: empty");
    Check(v::Mrad(1.5707f) == 1571 && v::Mrad(-0.0004f) == 0 && v::Mrad(NAN) == INT32_MIN && v::Mm(-1.25f) == -1250, "mrad/mm rounding");
}

void TestTiltGates() {
    Memory m = World();
    v::Read a{}, b{};
    v::ReadVehicle(m, kImage, kId, a);
    v::ReadVehicle(m, kImage, kId, b);
    v::TiltIn in{};
    in.enabled = in.pins = in.runtime = in.peer = true;
    in.id = kId;
    in.local = 0xFFFF;
    in.a44_mrad = 100;
    in.a48_mrad = -200;
    in.first = &a;
    in.second = &b;
    Check(v::TiltRefusal(in) == nullptr, "a realized, identical, same-object copy may be tilted");
    const auto refused = [&](v::TiltIn t, const char* why) {
        const char* got = v::TiltRefusal(t);
        return got && std::strcmp(got, why) == 0;
    };
    v::TiltIn t = in; t.enabled = false; Check(refused(t, "off"), "off");
    t = in; t.pins = false; Check(refused(t, "pins"), "pins");
    t = in; t.runtime = false; Check(refused(t, "runtime"), "runtime");
    t = in; t.peer = false; Check(refused(t, "peer"), "peer");
    t = in; t.second = nullptr; Check(refused(t, "read"), "read");
    t = in; t.id = 6656; Check(refused(t, "type"), "type");
    t = in; t.local = kId; Check(refused(t, "local"), "the viewer's own vehicle is never written");
    t = in; t.a44_mrad = 1571; Check(refused(t, "angle"), "angle a44");
    t = in; t.a48_mrad = -1571; Check(refused(t, "angle"), "angle a48");
    Memory d = World(false);
    v::Read da{}, db{};
    v::ReadVehicle(d, kImage, kId, da);
    v::ReadVehicle(d, kImage, kId, db);
    t = in; t.first = &da; t.second = &db; Check(refused(t, "identity"), "critic #16: a body distinct from the record is never written");
    t = in; t.second = &da; Check(refused(t, "changed"), "two different reads refuse");
    Memory u = World(true, 0xFF);
    v::Read ua{};
    v::ReadVehicle(u, kImage, kId, ua);
    t = in; t.first = t.second = &ua; Check(refused(t, "unrealized"), "unrealized");
    Memory bad = World();
    bad.Put<std::uintptr_t>(kImage + v::kCatVtRvas[2] + 0x1F0, 0);
    v::Read ba{};
    v::ReadVehicle(bad, kImage, kId, ba);
    t = in; t.first = t.second = &ba; Check(refused(t, "cat_fn"), "a body chain refusal");
    Memory nf = World();
    nf.Put<float>(kRecords + 0x44, NAN);
    v::Read na{};
    v::ReadVehicle(nf, kImage, kId, na);
    t = in; t.first = t.second = &na; Check(refused(t, "nonfinite"), "non-finite record angles");
    Memory dead;
    v::Read dr{};
    v::ReadVehicle(dead, kImage, kId, dr);
    t = in; t.first = t.second = &dr; Check(refused(t, "service"), "a unit refusal");
    // The sender's read gate (its own vehicle; no body needed).
    v::TiltReadIn rin{};
    rin.enabled = rin.runtime = rin.peer = true;
    rin.id = rin.local = kId;
    rin.read = &a;
    std::int32_t x = 0, y = 0;
    Check(v::TiltReadRefusal(rin, x, y) == nullptr && x == 125 && (y == -63 || y == -62), "the driver reads its own record angles");
    v::TiltReadIn r2 = rin; r2.local = 0xFFFF;
    Check(v::TiltReadRefusal(r2, x, y) && x == 0 && y == 0, "not riding that vehicle: nothing to send");
    r2 = rin; r2.peer = false; Check(std::strcmp(v::TiltReadRefusal(r2, x, y), "peer") == 0, "a peer that did not ask gets nothing");
    r2 = rin; r2.enabled = false; Check(std::strcmp(v::TiltReadRefusal(r2, x, y), "off") == 0, "off");
    r2 = rin; r2.read = &ua; Check(v::TiltReadRefusal(r2, x, y) == nullptr, "an unrealized own record is still read (the record is the source)");
    Memory steep = World();
    steep.Put<float>(kRecords + 0x48, 1.6f);
    v::Read sr{};
    v::ReadVehicle(steep, kImage, kId, sr);
    r2 = rin; r2.read = &sr; Check(std::strcmp(v::TiltReadRefusal(r2, x, y), "angle") == 0, "|angle| >= pi/2 is not sent");
    r2 = rin; r2.read = &na; Check(std::strcmp(v::TiltReadRefusal(r2, x, y), "nonfinite") == 0, "non-finite is not sent");
}

void TestWriteTilt() {
    alignas(16) unsigned char rec[0x80]{};
    v::Read r{};
    r.refusal = r.body_refusal = nullptr;
    r.unit.rec = reinterpret_cast<std::uintptr_t>(rec);
    r.body.body = r.unit.rec;
    Check(v::WriteTilt(r, 250, -125) == nullptr, "the gated write");
    float a = 0, b = 0;
    std::memcpy(&a, rec + 0x44, 4);
    std::memcpy(&b, rec + 0x48, 4);
    Check(std::fabs(a - 0.25f) < 1e-6f && std::fabs(b + 0.125f) < 1e-6f, "rec+0x44 / +0x48 hold the radians");
    unsigned char other[0x80]{};
    v::Read d = r;
    d.body.body = reinterpret_cast<std::uintptr_t>(other);
    std::memset(rec, 0, sizeof(rec));
    Check(v::WriteTilt(d, 1, 1) && std::strcmp(v::WriteTilt(d, 1, 1), "identity") == 0, "a distinct body is refused at the write too");
    Check(std::memcmp(rec, other, sizeof(rec)) == 0, "nothing written on refusal");
    // A faulting store is contained.
    void* page = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY);
    Check(page != nullptr, "read-only page");
    v::Read ro = r;
    ro.unit.rec = ro.body.body = reinterpret_cast<std::uintptr_t>(page);
    Check(v::WriteTilt(ro, 1, 1) && std::strcmp(v::WriteTilt(ro, 1, 1), "fault") == 0, "a fault is a refusal, not a crash");
    VirtualFree(page, 0, MEM_RELEASE);
}

void TestVerifyImage() {
    constexpr std::size_t kSpan = 0x2100000;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, kSpan, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != nullptr, "fixture image");
    std::memset(image, 0xCC, kSpan);
    for (const auto& site : c::code_sites)
        if (site.rva + site.size <= kSpan) std::memcpy(image + site.rva, site.bytes, site.size);
    for (const auto& d : c::vehicle_data) std::memcpy(image + d.rva, &d.value, 4);
    Check(v::VerifyImage(image, kSpan) == nullptr, "the vehicle blocks and thresholds verify");
    Check(sizeof(c::vehicle_blocks) / sizeof(c::vehicle_blocks[0]) == 17 && sizeof(c::vehicle_data) / sizeof(c::vehicle_data[0]) == 2,
          "17 vehicle blocks and 2 data words are pinned");
    image[0xBA9747] ^= 1;
    Check(v::VerifyImage(image, kSpan) && std::strcmp(v::VerifyImage(image, kSpan), "block") == 0, "a changed block byte refuses");
    image[0xBA9747] ^= 1;
    image[0x20A72CC] ^= 1;
    Check(v::VerifyImage(image, kSpan) && std::strcmp(v::VerifyImage(image, kSpan), "threshold") == 0, "a changed threshold refuses");
    image[0x20A72CC] ^= 1;
    Check(v::VerifyImage(image, 0x1000000) && std::strcmp(v::VerifyImage(image, 0x1000000), "span") == 0, "a short span refuses");
    Check(v::VerifyImage(nullptr, kSpan) && std::strcmp(v::VerifyImage(nullptr, kSpan), "base") == 0, "no image");
    VirtualFree(image, 0, MEM_RELEASE);
}

void TestRideExt() {
    rw::Ext e{};
    e.features = rw::ExtFeature::SeatId | rw::ExtFeature::Tilt;
    e.category = 1; e.type = 0x14; e.seat = 0; e.revision = 7; e.vehicle = 25600; e.sender_ms = 0x01020304;
    std::uint8_t b[rw::kExtSize]{};
    Check(rw::EncodeExt(e, b) && b[0] == 0x52 && b[1] == 3 && b[2] == 3 && b[3] == 1 && b[4] == 7 && b[5] == 0 && b[6] == 0x00 && b[7] == 0x64 &&
              b[8] == 0 && b[9] == 0x14 && b[10] == 0 && b[11] == 0 && b[12] == 4 && b[15] == 1,
          "the v3 addendum layout");
    rw::Ext back{};
    Check(rw::IsExt(b, sizeof(b)) && rw::IsRide(b, sizeof(b)) && rw::DecodeExt(b, sizeof(b), back) && rw::SameExt(back, e) &&
              back.sender_ms == e.sender_ms, "round trip");
    // An older receiver's v1/v2 decoder drops it (size and version) and keeps its state.
    rw::Packet p{};
    Check(!rw::Decode(b, sizeof(b), p), "an older ride decoder refuses the addendum");
    rw::Store old{};
    rw::Packet ride{rw::Kind::vehicle, 7, 1000};
    ride.seat = 0; ride.vehicle = 25600;
    std::uint8_t v2[rw::kSize]{};
    Check(rw::Encode(ride, v2) && rw::Accept(old, v2, sizeof(v2), 9, 10), "the v2 packet is accepted");
    Check(!rw::Accept(old, b, sizeof(b), 9, 11) && old.have && old.packet.vehicle == 25600 && old.invalid == 1,
          "the addendum is only counted invalid by the v1/v2 store, the stored ride stays");
    // Field rules.
    rw::Ext bad = e; bad.revision = 0; Check(!rw::EncodeExt(bad, b), "revision 0");
    bad = e; bad.features = 4; Check(!rw::EncodeExt(bad, b), "unknown feature");
    bad = e; bad.seat = 4; Check(!rw::EncodeExt(bad, b), "seat 4");
    bad = e; bad.vehicle = 6656; Check(!rw::EncodeExt(bad, b), "a non-vehicle id");
    bad = e; bad.category = 16; Check(!rw::EncodeExt(bad, b), "category beyond 15");
    bad = e; bad.type = 0x24; Check(!rw::EncodeExt(bad, b), "type and category disagree");
    rw::Ext unknown{};
    unknown.revision = 1;
    Check(rw::EncodeExt(unknown, b) && b[3] == 0xFF && b[6] == 0xFF && b[7] == 0xFF && b[8] == 0xFF && b[9] == 0xFF,
          "on foot: unknown category, vehicle, seat and type");
    Check(rw::EncodeExt(e, b), "re-encode");
    b[10] = 1;
    Check(!rw::DecodeExt(b, sizeof(b), back), "reserved bytes must be zero");
    Check(rw::EncodeExt(e, b), "re-encode");
    // Store, lease and the category for exactly the stored ride.
    rw::ExtStore s{};
    Check(!rw::AcceptExt(s, b, sizeof(b), 0, 10) && s.invalid == 1, "no link session: refused");
    Check(rw::AcceptExt(s, b, sizeof(b), 9, 100) && rw::ExtLive(s, 9, 100 + rw::kLeaseMs) && !rw::ExtLive(s, 9, 101 + rw::kLeaseMs) &&
              !rw::ExtLive(s, 8, 100), "stored for its session, live for the lease");
    rw::Ext older = e; older.sender_ms = e.sender_ms - 5;
    std::uint8_t ob[rw::kExtSize]{};
    Check(rw::EncodeExt(older, ob) && !rw::AcceptExt(s, ob, sizeof(ob), 9, 101) && s.stale == 1, "an older addendum is stale");
    Check(rw::RemoteCategory(s, ride, 9, 200) == 1, "the category of exactly this ride (revision, vehicle, seat)");
    rw::Packet moved = ride; moved.revision = 8;
    Check(rw::RemoteCategory(s, moved, 9, 200) == rw::kCategoryUnknown, "another revision: unknown");
    moved = ride; moved.vehicle = 25601;
    Check(rw::RemoteCategory(s, moved, 9, 200) == rw::kCategoryUnknown, "another vehicle: unknown");
    moved = ride; moved.seat = 1;
    Check(rw::RemoteCategory(s, moved, 9, 200) == rw::kCategoryUnknown, "another seat: unknown");
    Check(rw::RemoteCategory(s, ride, 9, 200 + rw::kLeaseMs) == rw::kCategoryUnknown, "an expired addendum: unknown");
    rw::Ext tiltOnly = e; tiltOnly.features = rw::ExtFeature::Tilt; tiltOnly.sender_ms += 10;
    Check(rw::EncodeExt(tiltOnly, b) && rw::AcceptExt(s, b, sizeof(b), 9, 300) && rw::RemoteCategory(s, ride, 9, 300) == rw::kCategoryUnknown,
          "a peer without the seat-id feature offers no category");
}

struct Recorder {
    std::vector<std::string> ops;
    bool fail_at_flag = false;
    bool Word(std::uintptr_t at, std::uint16_t value) noexcept {
        char t[48]{};
        std::snprintf(t, sizeof(t), "id:+0x%x=%u", unsigned(at - 0x1000), unsigned(value));
        ops.emplace_back(t);
        return true;
    }
    bool OrByte(std::uintptr_t at, std::uint8_t value) noexcept {
        if (fail_at_flag) return false;
        char t[48]{};
        std::snprintf(t, sizeof(t), "flag:+0x%x|%u", unsigned(at - 0x1000), unsigned(value));
        ops.emplace_back(t);
        return true;
    }
};

void TestSeatTarget() {
    constexpr std::uint64_t kLoop0 = 0x489157d7d94full, kGetOn0 = 0xdfb7fc6173d0ull, kGetOff0 = 0x3b9161fab2a7ull;
    prs::EntryView view{};
    const auto chain = [&](std::uint8_t entries) {
        view = prs::EntryView{};
        const std::uint64_t states[3] = {kGetOn0, kLoop0, kGetOff0};
        for (unsigned i = 0; i < 8; ++i) view.ids[i] = 0xFFFF;
        for (unsigned i = 0; i < entries; ++i) {
            view.flags[i] = prs::kEntryFlagState;
            view.cnp[i] = 0xABCD000000000000ull | prs::kCnpPposC;  // the high bits are not part of the hash compare
            view.state[i] = entries == 3 ? states[i] : entries == 2 ? states[i + 1] : kLoop0;
        }
        view.cur = 0;
        view.rec18 = view.state[0];
    };
    chain(1);
    auto plan = prs::PlanSeatTarget(view, 25600, 0, 1);
    Check(!plan.refusal && plan.write && plan.entries == 1, "an untargeted seat loop is written");
    chain(3);
    plan = prs::PlanSeatTarget(view, 25600, 0, 3);
    Check(!plan.refusal && plan.write, "GetOn, Loop, GetOff: every entry");
    view.cur = 1; view.rec18 = kLoop0;
    Check(!prs::PlanSeatTarget(view, 25600, 0, 3).refusal, "the loop entry current");
    chain(1);
    for (unsigned i = 0; i < 1; ++i) { view.ids[i] = 25608; view.flags[i] |= prs::kEntryFlagTarget; }
    plan = prs::PlanSeatTarget(view, 25608, 0, 1);
    Check(!plan.refusal && !plan.write, "a named locator's targeted chain needs no write");
    const auto refusal = [&](std::uint16_t vehicle, std::uint8_t seat, std::uint8_t entries, const char* why) {
        const auto p = prs::PlanSeatTarget(view, vehicle, seat, entries);
        return p.refusal && std::strcmp(p.refusal, why) == 0 && !p.write;
    };
    chain(1);
    Check(refusal(25600, 0, 0, "entries") && refusal(25600, 0, 4, "entries"), "1..3 entries");
    Check(refusal(6656, 0, 1, "vehicle") && refusal(0xFFFF, 0, 1, "vehicle") && refusal(25600, 4, 1, "vehicle"), "a vehicle id and a seat");
    view.cur = 1; Check(refusal(25600, 0, 1, "cur"), "the current entry inside the chain"); view.cur = 0;
    view.rec18 = 0x123; Check(refusal(25600, 0, 1, "state"), "not a seated state"); view.rec18 = kLoop0;
    Check(refusal(25600, 1, 1, "state"), "the seat-0 loop is not seat 1's");
    view.cnp[0] = 0x1234; Check(refusal(25600, 0, 1, "cnp"), "CNP_ppos_c only"); view.cnp[0] = prs::kCnpPposC;
    view.flags[0] = 0; Check(refusal(25600, 0, 1, "entry_state"), "a named state entry only (flag 8)"); view.flags[0] = prs::kEntryFlagState;
    view.state[0] = prs::kForbiddenRearSeatState; Check(refusal(25600, 0, 1, "entry_state"), "never the s10150 rear seat"); view.state[0] = kLoop0;
    view.ids[0] = 25601; Check(refusal(25600, 0, 1, "foreign_target"), "an entry targeted elsewhere is never overwritten");
    view.ids[0] = 0xFFFF; view.flags[0] |= prs::kEntryFlagTarget; Check(refusal(25600, 0, 1, "foreign_target"), "flag 2 without an id");
    chain(2);
    view.ids[1] = 25600; view.flags[1] |= prs::kEntryFlagTarget;
    Check(refusal(25600, 0, 2, "foreign_target"), "a half-targeted chain is refused");
    Check(prs::ChainEntries(0) == 1 && prs::ChainEntries(1) == 2 && prs::ChainEntries(2) == 2 && prs::ChainEntries(3) == 3 &&
              prs::ChainEntries(4 | 3) == 3, "chain entries from the entry bits (by_id ignored)");
    // The write order: every target id first, then every flag 2 (critic #17).
    Recorder rec;
    Check(prs::ApplySeatTarget(rec, 0x1000, 25600, 3), "applied");
    const std::vector<std::string> want = {"id:+0xc0=25600", "id:+0xc2=25600", "id:+0xc4=25600", "flag:+0xb8|2", "flag:+0xb9|2", "flag:+0xba|2"};
    Check(rec.ops == want, "ids rec+0xC0+2i first, then flag 2 at rec+0xB8+i");
    Recorder fails;
    fails.fail_at_flag = true;
    Check(!prs::ApplySeatTarget(fails, 0x1000, 25600, 1) && fails.ops.size() == 1 && fails.ops[0] == "id:+0xc0=25600",
          "a failed flag store reports failure after the id (the flag never precedes it)");
}
} // namespace

int main() {
    TestReader();
    TestCensus();
    TestLife();
    TestTiltGates();
    TestWriteTilt();
    TestVerifyImage();
    TestRideExt();
    TestSeatTarget();
    std::printf("partner_vehicle_tests: %u checks passed\n", checks);
    return 0;
}
