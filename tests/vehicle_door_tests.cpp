// G4 truck test levers (docs/native-puppet.md "Truck test levers"; mgscoop/vehicle_door.hpp): the
// receipt grammar, the gate order, the partner target, the stock door point and stand spot, the
// reply and log lines, the digest and the image verification on a fixture image. Nothing here
// touches MGSV; the fixture buffers are never executed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "mgscoop/vehicle_door.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace vd = mgscoop::vehicle_door;
namespace c = mgscoop::partner_player_contract;
unsigned checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL %u: %s\n", checks, message);
        std::exit(1);
    }
}
bool Parse(const char* text, vd::Request& out) { return vd::ParseRequest(text, std::strlen(text), out); }
bool Near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

void TestParse() {
    vd::Request r{};
    Check(Parse("25602 0 60", r) && r.target == vd::Target::id && r.id == 25602 && r.seat == 0 && r.out_cm == 60, "an id, the driver door, 60 cm");
    Check(Parse("partner 1 0", r) && r.target == vd::Target::partner && r.id == vd::kNone && r.seat == 1 && r.out_cm == 0,
          "the partner, the passenger door, the door point");
    Check(Parse("25600 1 150", r) && r.id == 25600 && r.out_cm == 150, "the first vehicle id and the largest distance");
    Check(Parse("26111 0 5", r) && r.id == 26111, "the last vehicle-type id (0x65FF)");
    for (const char* bad : {"", "25602 0", "25602 2 60", "25602 0 151", "25602  0 60", "025602 0 60", "25602 0 060", "-1 0 60",
                            "6656 0 60", "65535 0 60", "26112 0 1", "25599 0 1", "partner", "partners 0 1", "partner 0 1 ", "25602 0 60x",
                            " 25602 0 60", "partner 01 1", "25602 0 9999999999", "partner -1 1"}) {
        r.id = 1;
        Check(!Parse(bad, r) && r.id == vd::kNone, bad);
    }
    Check(!vd::ParseRequest(nullptr, 3, r), "no text");
    const std::string longText(40, '1');
    Check(!vd::ParseRequest(longText.data(), longText.size(), r), "an overlong request");
}

void TestGate() {
    vd::GateIn in{};
    Check(std::strcmp(vd::GateRefusal(in), "off") == 0, "off first");
    in.enabled = true;
    Check(std::strcmp(vd::GateRefusal(in), "lab_only") == 0, "then the lab");
    in.lab = true;
    Check(std::strcmp(vd::GateRefusal(in), "runtime") == 0, "then the runtime thread");
    in.runtime = true;
    Check(std::strcmp(vd::GateRefusal(in), "pins") == 0, "then the pins");
    in.pins = true;
    Check(vd::GateRefusal(in) == nullptr, "admitted");
    in.uncertain = true;
    Check(std::strcmp(vd::GateRefusal(in), "uncertain") == 0, "a latched mission refuses");
    in = vd::GateIn{};
    in.lab = in.runtime = in.pins = true;
    Check(std::strcmp(vd::GateRefusal(in), "off") == 0, "the key off wins over everything");
}

void TestPartnerTarget() {
    std::uint16_t id = 1;
    Check(vd::PartnerTarget(true, 25602, id) == nullptr && id == 25602, "a live vehicle ride names the id");
    Check(std::strcmp(vd::PartnerTarget(false, 25602, id), "partner_ride") == 0 && id == vd::kNone, "no live ride");
    Check(std::strcmp(vd::PartnerTarget(true, vd::kNone, id), "partner_ride") == 0 && id == vd::kNone, "an unknown vehicle");
    Check(std::strcmp(vd::PartnerTarget(true, 6656, id), "partner_ride") == 0 && id == vd::kNone, "a horse id is no vehicle");
}

vd::Facts Truck() {
    vd::Facts f{};
    f.id = 25602;
    f.type = 3;
    f.seats = 2;
    f.rider0 = 0xFFFF;
    const float rec[4] = {10.0f, 1.0f, 20.0f, 0.0f};
    const float s0[3] = {10.5f, 2.0f, 22.0f}, s1[3] = {9.5f, 2.0f, 22.0f};
    std::memcpy(f.rec, rec, sizeof(rec));
    std::memcpy(f.seat0, s0, sizeof(s0));
    std::memcpy(f.seat1, s1, sizeof(s1));
    return f;
}
// The stock truck gate (CanExec 0x1246B13) for a player position.
bool StockGate(const vd::Facts& f, unsigned seat, const float player[3]) {
    const float* a = seat ? f.seat1 : f.seat0;
    const float* b = seat ? f.seat0 : f.seat1;
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float door[3];
    for (int i = 0; i < 3; ++i) door[i] = a[i] + c::door_offset_m * d[i] / len;
    const float p[3] = {player[0] - door[0], player[1] - door[1], player[2] - door[2]};
    const float dist = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    return dist < c::door_radius_m && p[0] * d[0] + p[2] * d[2] >= 0.0f;
}

void TestDoorSpot() {
    const vd::Facts f = Truck();
    vd::Spot s = vd::DoorSpot(f, 0, 60);
    Check(s.refusal == nullptr && Near(s.door[0], 10.9f) && Near(s.door[1], 2.0f) && Near(s.door[2], 22.0f), "the driver door point: seat0 + 0.4 m outward");
    Check(Near(s.axis[0], 1.0f) && Near(s.axis[1], 0.0f), "the driver door faces away from seat 1");
    Check(Near(s.stand[0], 11.5f) && Near(s.stand[1], 1.5f) && Near(s.stand[2], 22.0f), "the stand spot 60 cm out, 0.5 m above the record");
    Check(Near(s.face, -1.5707963f), "facing the driver seat (-x)");
    Check(StockGate(f, 0, s.stand), "the stand spot passes the stock gate (0.78 m from the door point, outer side)");
    s = vd::DoorSpot(f, 1, 0);
    Check(s.refusal == nullptr && Near(s.door[0], 9.1f) && Near(s.axis[0], -1.0f) && Near(s.stand[0], 9.1f) && Near(s.face, 1.5707963f),
          "the passenger door mirrors it");
    // A truck turned 90 degrees and lifted: the door is still 0.4 m from the seat along the seat axis.
    vd::Facts r = f;
    const float s0[3] = {-3.0f, 5.0f, 7.55f}, s1[3] = {-3.0f, 5.0f, 6.45f};
    std::memcpy(r.seat0, s0, sizeof(s0));
    std::memcpy(r.seat1, s1, sizeof(s1));
    r.rec[1] = 4.0f;
    s = vd::DoorSpot(r, 0, 100);
    Check(s.refusal == nullptr && Near(s.door[2], 7.95f) && Near(s.axis[1], 1.0f) && Near(s.stand[2], 8.95f) && Near(s.stand[1], 4.5f) &&
              Near(std::fabs(s.face), 3.1415927f, 1e-3f),
          "a turned truck: +z door, facing back toward -z");
    Check(StockGate(r, 0, s.stand), "1 m out and 0.5 m below the door point is still inside the stock 1.4 m");
    s = vd::DoorSpot(r, 0, 150);
    Check(s.refusal == nullptr && !StockGate(r, 0, s.stand), "150 cm out is beyond the stock gate (far)");
    s = vd::DoorSpot(r, 1, 60);
    Check(s.refusal == nullptr && Near(s.axis[1], -1.0f) && StockGate(r, 1, s.stand), "the passenger door of the turned truck");
    // A tilted seat axis (3D): the door offset is 3D, the outward axis flat.
    vd::Facts t = f;
    t.seat0[1] = 2.3f;
    s = vd::DoorSpot(t, 0, 0);
    const float len = std::sqrt(1.0f + 0.09f);
    Check(s.refusal == nullptr && Near(s.door[0], 10.5f + 0.4f / len) && Near(s.door[1], 2.3f + 0.4f * 0.3f / len) && Near(s.axis[0], 1.0f),
          "the stock offset uses the 3D seat axis");
    // Refusals.
    Check(std::strcmp(vd::DoorSpot(f, 2, 60).refusal, "seat") == 0, "seat 2 has no front door");
    Check(std::strcmp(vd::DoorSpot(f, 0, 151).refusal, "out") == 0, "too far out");
    vd::Facts n = f;
    n.seat1[2] = std::nanf("");
    Check(std::strcmp(vd::DoorSpot(n, 0, 60).refusal, "nonfinite") == 0, "a NaN seat");
    n = f;
    n.rec[1] = INFINITY;
    Check(std::strcmp(vd::DoorSpot(n, 0, 60).refusal, "nonfinite") == 0, "an infinite record");
    n = f;
    std::memcpy(n.seat1, n.seat0, sizeof(n.seat0));
    Check(std::strcmp(vd::DoorSpot(n, 0, 60).refusal, "seat_axis") == 0, "seat 1 remapped onto seat 0 (a player in rider[1])");
    n.seat1[1] += 1.0f;
    Check(std::strcmp(vd::DoorSpot(n, 1, 60).refusal, "seat_axis") == 0, "stacked seats have no flat axis");
}

void TestFormat() {
    const vd::Facts f = Truck();
    const vd::Spot s = vd::DoorSpot(f, 0, 60);
    vd::Answer a{};
    a.facts = &f;
    a.spot = &s;
    char out[400]{};
    const std::size_t n = vd::FormatAnswer(a, out, sizeof(out));
    Check(n == std::strlen(out) &&
              std::strcmp(out, "return {ok=true,id=25602,peer=0,pseat=255,type=3,seats=2,rider0=65535,x=11500,y=1500,z=22000,face=-1571,"
                               "dx=10900,dy=2000,dz=22000,ax=1000,az=0,rx=10000,ry=1000,rz=20000}") == 0,
          out);
    a.peer = true;
    a.peer_seat = 0;
    Check(vd::FormatAnswer(a, out, sizeof(out)) && std::strstr(out, "peer=1,pseat=0,") != nullptr, "the partner target");
    vd::Answer refused{};
    refused.refusal = "lab_only";
    Check(vd::FormatAnswer(refused, out, sizeof(out)) && std::strcmp(out, "return {ok=false,reason='lab_only'}") == 0, "a refusal");
    vd::Spot bad{};
    bad.refusal = "seat_axis";
    vd::Answer spot{};
    spot.facts = &f;
    spot.spot = &bad;
    Check(vd::FormatAnswer(spot, out, sizeof(out)) && std::strcmp(out, "return {ok=false,reason='seat_axis'}") == 0, "a spot refusal");
    Check(vd::FormatAnswer(vd::Answer{}, out, sizeof(out)) && std::strcmp(out, "return {ok=false,reason='answer'}") == 0, "no facts");
    Check(vd::FormatAnswer(a, out, 40) == 0 && out[0] == 0, "no room: empty");
    // The log lines.
    vd::Request q{};
    q.id = 25602;
    q.out_cm = 60;
    a.peer = false;
    a.peer_seat = vd::kSeatNone;
    char line[512]{};
    Check(vd::FormatLog(q, a, line, sizeof(line)) &&
              std::strcmp(line, "partner_vehicle_door=answer; target=id; id=25602; seat=0; out_cm=60; type=3; seats=2; rider0=65535; pseat=255; "
                                "rec_mm=10000,1000,20000; yaw_mrad=0; seat0_mm=10500,2000,22000; seat1_mm=9500,2000,22000; "
                                "door_mm=10900,2000,22000; stand_mm=11500,1500,22000; face_mrad=-1571\r\n") == 0,
          line);
    q.target = vd::Target::partner;
    q.seat = 1;
    Check(vd::FormatLog(q, refused, line, sizeof(line)) &&
              std::strcmp(line, "partner_vehicle_door=refused; target=partner; id=25602; seat=1; reason=lab_only\r\n") == 0,
          line);
    Check(vd::FormatLog(q, a, line, 30) == 0 && line[0] == 0, "no room for the log line");
}

void TestDigest() {
    vd::Digest a{};
    a.riders[0] = 0xFFFF;
    a.index = 2;
    a.viewer_act = 1;
    a.viewer_unit = 0x1000;
    vd::Digest b = a;
    Check(vd::SameDigest(a, b), "equal digests");
    b.riders[3] = 5;
    Check(!vd::SameDigest(a, b), "a rider change");
    b = a;
    b.realized = 0xFF;
    Check(!vd::SameDigest(a, b), "a realized-slot change");
    b = a;
    b.viewer_act = 0x6F;
    Check(!vd::SameDigest(a, b), "the viewer's act changed");
    b = a;
    b.viewer_unit = 0;
    Check(!vd::SameDigest(a, b), "the viewer's ridden unit changed");
}

void TestVerifyImage() {
    constexpr std::size_t kSpan = 0x2200000;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, kSpan, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != nullptr, "fixture image");
    std::memset(image, 0xCC, kSpan);
    for (const auto& site : c::code_sites)
        if (site.rva + site.size <= kSpan) std::memcpy(image + site.rva, site.bytes, site.size);
    for (const auto& d : c::door_data) std::memcpy(image + d.rva, &d.value, 4);
    Check(vd::VerifyImage(image, kSpan) == nullptr, "the door blocks and constants verify");
    Check(sizeof(c::door_blocks) / sizeof(c::door_blocks[0]) == 13 && sizeof(c::door_data) / sizeof(c::door_data[0]) == 4,
          "13 door blocks and 4 data words are pinned");
    Check(c::door_seat_matrix_rva == 0xB7AF90 && c::door_gate_rva == 0x1246B13 && c::door_offset_m == 0.4f && c::door_radius_m == 1.4f,
          "the contract's door facts");
    image[0x1246C2A] ^= 1;
    Check(vd::VerifyImage(image, kSpan) && std::strcmp(vd::VerifyImage(image, kSpan), "block") == 0, "a changed gate byte refuses");
    image[0x1246C2A] ^= 1;
    image[0x218D51C] ^= 1;
    Check(vd::VerifyImage(image, kSpan) && std::strcmp(vd::VerifyImage(image, kSpan), "data") == 0, "a changed radius refuses");
    image[0x218D51C] ^= 1;
    // M6 seat 1: the board turns the seat matrix's seat-1 remap jne (0xB79A43) into a jmp for the process.
    Check(c::board_remap_rva == 0xB79A40 && image[0xB79A43] == 0x75, "the remap opcode byte is a jne in the stock image");
    image[0xB79A43] = 0xEB;
    Check(vd::VerifyImage(image, kSpan) == nullptr, "the board's remap jmp still verifies");
    image[0xB79A43] = 0x74;
    Check(vd::VerifyImage(image, kSpan) && std::strcmp(vd::VerifyImage(image, kSpan), "block") == 0, "any other remap opcode refuses");
    image[0xB79A43] = 0x75;
    Check(vd::VerifyImage(image, 0x1000000) && std::strcmp(vd::VerifyImage(image, 0x1000000), "span") == 0, "a short span refuses");
    Check(vd::VerifyImage(nullptr, kSpan) && std::strcmp(vd::VerifyImage(nullptr, kSpan), "base") == 0, "no image");
    VirtualFree(image, 0, MEM_RELEASE);
}

void TestReadFactsRefusals() {
    vd::Facts f{};
    Check(std::strcmp(vd::ReadFacts(nullptr, 25602, 0, 0, 0, f), "image") == 0 && f.id == 25602 && f.type == 0xFF,
          "no image: the vehicle read refuses before any call");
    char fake[16]{};
    Check(std::strcmp(vd::ReadFacts(fake, 6656, 0, 0, 0, f), "type") == 0, "a horse id is no vehicle");
}
} // namespace

int main() {
    TestParse();
    TestGate();
    TestPartnerTarget();
    TestDoorSpot();
    TestFormat();
    TestDigest();
    TestVerifyImage();
    TestReadFactsRefusals();
    std::printf("vehicle_door_tests: %u checks PASS\n", checks);
    return 0;
}
