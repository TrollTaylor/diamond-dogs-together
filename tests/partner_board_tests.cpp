// F1 vehicles (docs/native-puppet.md "Vehicle boarding"): the board policy
// (pack, lease, publication, the per-search decision, the back seats), both
// wrappers with bound fakes (block, passenger, truck fallback, peer-seat
// exclusion, lease expiry, wrong vehicle, SEH faults, last error), the pinned
// blocks and the install with its A-revert on a synthetic image, the local
// passenger report on a sparse memory map, the receiver gates and the surprise
// latch. Nothing here touches MGSV; the fixture buffers are never executed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "mgscoop/partner_board.hpp"
#include "mgscoop/partner_board_policy.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace pb = mgscoop::partner_board;
namespace c = mgscoop::partner_player_contract;
unsigned checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %u: %s\n", checks, message); std::exit(1); }
}
bool Same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

void TestPackAndLease() {
    pb::Policy p{};
    p.vehicle = 25603; p.peer_seat = 0; p.mode = pb::Mode::passenger; p.until_ms = 0xFEDCBA98u;
    const pb::Policy q = pb::Unpack(pb::Pack(p));
    Check(q.vehicle == 25603 && q.peer_seat == 0 && q.mode == pb::Mode::passenger && q.until_ms == 0xFEDCBA98u, "pack round trip");
    Check(pb::Unpack(0).mode == pb::Mode::off, "word 0 is off");
    Check(pb::Unpack(pb::Pack(p) | (0xFFull << 24)).mode == pb::Mode::off, "an unknown mode byte reads as off");
    // Review R3: the peer driver's seat-1 handshake rides in the mode byte's top bits.
    Check(!q.front_ok && !q.cab_ok, "no seat-1 handshake by default");
    pb::Policy f = p;
    f.front_ok = true;
    pb::Policy u = pb::Unpack(pb::Pack(f));
    Check(u.front_ok && !u.cab_ok && u.mode == pb::Mode::passenger && u.vehicle == 25603 && u.until_ms == 0xFEDCBA98u, "front_ok packs");
    f.front_ok = false; f.cab_ok = true;
    u = pb::Unpack(pb::Pack(f));
    Check(!u.front_ok && u.cab_ok && u.mode == pb::Mode::passenger, "cab_ok packs");
    Check(pb::LeaseLive(1000, 900) && pb::LeaseLive(1000, 1000) && !pb::LeaseLive(1000, 1001), "the lease ends at until");
    Check(pb::LeaseLive(6000, 1000) && !pb::LeaseLive(6001, 1000), "a lease further than 5 s ahead is a foreign clock");
    Check(pb::LeaseLive(3, 0xFFFFFFF0u) && !pb::LeaseLive(0xFFFFFFF0u, 3), "the lease is wrap-safe");
}

void TestPolicyFor() {
    pb::PublishIn in{};
    in.mode = pb::Mode::block; in.ride_live = true; in.vehicle_kind = true; in.seat = 0; in.vehicle = 25603; in.received_ms = 0x1'0000'1000ull;
    pb::Policy p = pb::PolicyFor(in);
    Check(p.mode == pb::Mode::block && p.vehicle == 25603 && p.peer_seat == 0 && p.until_ms == 0x1000u + pb::kLeaseMs,
          "block: the peer's live vehicle ride, until = received + lease (low 32 bits)");
    pb::PublishIn x = in; x.mode = pb::Mode::off;
    Check(pb::PolicyFor(x).mode == pb::Mode::off, "key off: nothing");
    x = in; x.ride_live = false;
    Check(pb::PolicyFor(x).mode == pb::Mode::off, "a stale ride: nothing");
    x = in; x.vehicle_kind = false;
    Check(pb::PolicyFor(x).mode == pb::Mode::off, "on foot / heli / horse: nothing");
    x = in; x.seat = 0xFF;
    Check(pb::PolicyFor(x).mode == pb::Mode::off, "an unknown seat: nothing");
    x = in; x.vehicle = 0xFFFF;
    Check(pb::PolicyFor(x).mode == pb::Mode::off, "an unknown vehicle: nothing");
    x = in; x.vehicle = 0x2003;
    Check(pb::PolicyFor(x).mode == pb::Mode::off, "a non-vehicle GameObject type: nothing");
    x = in; x.mode = pb::Mode::passenger;
    Check(pb::PolicyFor(x).mode == pb::Mode::passenger, "passenger");
    x.uncertain = true;
    Check(pb::PolicyFor(x).mode == pb::Mode::block, "the uncertain latch degrades passenger to block");
    x = in; x.seat = 2;
    Check(pb::PolicyFor(x).peer_seat == 2 && pb::PolicyFor(x).mode == pb::Mode::block, "a peer passenger is published with its seat");
    // Review R3: seat 1 only where the driver's copy said it draws a seat-1 passenger and its seat 1 is empty.
    x = in; x.mode = pb::Mode::passenger;
    Check(!pb::PolicyFor(x).front_ok && !pb::PolicyFor(x).cab_ok, "no seat addendum from the driver: no seat 1");
    x.seat_flags = pb::kSeatFlagFront;
    Check(pb::PolicyFor(x).front_ok && !pb::PolicyFor(x).cab_ok, "the driver's copy draws a light vehicle's seat 1: front_ok");
    x.seat_flags = pb::kSeatFlagCab;
    Check(!pb::PolicyFor(x).front_ok && pb::PolicyFor(x).cab_ok, "the truck cab: cab_ok");
    x.seat_flags = pb::kSeatFlagFront | pb::kSeatFlagCab | pb::kSeatFlagOneTaken;
    Check(!pb::PolicyFor(x).front_ok && !pb::PolicyFor(x).cab_ok, "seat 1 taken on the driver's copy (its D-Dog): neither");
    x.seat_flags = pb::kSeatFlagFront;
    x.seat = 2;
    Check(!pb::PolicyFor(x).front_ok, "only for a peer driver");
    x.seat = 0; x.uncertain = true;
    Check(pb::PolicyFor(x).mode == pb::Mode::block && !pb::PolicyFor(x).front_ok, "block never offers seat 1");
}

void TestDecide() {
    pb::Policy p{};
    p.vehicle = 25603; p.peer_seat = 0; p.mode = pb::Mode::block; p.until_ms = 5000;
    const char* why = nullptr;
    Check(pb::DecideVehicle(p, 1000, true, 3, why) == pb::Verdict::block && Same(why, "block"), "block: the peer drives this unit");
    Check(pb::DecideVehicle(p, 1000, true, 4, why) == pb::Verdict::stock && Same(why, "vehicle"), "another vehicle: stock");
    Check(pb::DecideVehicle(p, 1000, true, 0x203, why) == pb::Verdict::stock && Same(why, "vehicle"), "an index beyond 0x1FF: stock");
    Check(pb::DecideVehicle(p, 1000, false, 3, why) == pb::Verdict::stock && Same(why, "index_read"), "an unreadable index: stock");
    Check(pb::DecideVehicle(p, 5001, true, 3, why) == pb::Verdict::stock && Same(why, "lease"), "the lease expired: stock");
    pb::Policy q = p; q.peer_seat = 2;
    Check(pb::DecideVehicle(q, 1000, true, 3, why) == pb::Verdict::stock && Same(why, "peer_seat"),
          "a peer passenger leaves the driver's seat to the viewer");
    q = p; q.mode = pb::Mode::off;
    Check(pb::DecideVehicle(q, 1000, true, 3, why) == pb::Verdict::stock && Same(why, "off"), "off: stock");
    q = p; q.vehicle = 0x2003;
    Check(pb::DecideVehicle(q, 1000, true, 3, why) == pb::Verdict::stock && Same(why, "type"), "a non-vehicle policy id: stock");
    q = p; q.mode = pb::Mode::passenger;
    Check(pb::DecideVehicle(q, 1000, true, 3, why) == pb::Verdict::seats && Same(why, "passenger"), "passenger asks for the seats");
    // The seats: light vehicles with 3..4 seats only; the rest blocks.
    pb::Range r = pb::DecideSeats(q, true, 1, 4);
    Check(r.ok && r.first == 2 && r.last == 3, "a 4-seat light vehicle: back seats 2..3");
    r = pb::DecideSeats(q, true, 2, 3);
    Check(r.ok && r.first == 2 && r.last == 2, "a 3-seat light vehicle: seat 2 only");
    Check(!pb::DecideSeats(q, true, 3, 4).ok && Same(pb::DecideSeats(q, true, 3, 4).reason, "type"), "a truck blocks");
    Check(!pb::DecideSeats(q, true, 4, 4).ok && !pb::DecideSeats(q, true, 5, 2).ok && !pb::DecideSeats(q, true, 8, 4).ok,
          "trucks and armoured vehicles block");
    Check(!pb::DecideSeats(q, true, 0, 4).ok, "type 0 (no category) blocks");
    Check(!pb::DecideSeats(q, true, 1, 2).ok && Same(pb::DecideSeats(q, true, 1, 2).reason, "seats"), "two seats: block");
    Check(!pb::DecideSeats(q, true, 1, 5).ok, "five seats: block");
    Check(!pb::DecideSeats(q, false, 1, 4).ok && Same(pb::DecideSeats(q, false, 1, 4).reason, "facts_read"), "unread facts: block");
    // Peer-seat exclusion (a policy for a peer in a back seat never searches its seat).
    std::uint8_t first = 0, last = 0;
    Check(pb::BackSeats(4, 2, first, last) && first == 3 && last == 3, "peer in 2: only 3");
    Check(pb::BackSeats(4, 3, first, last) && first == 2 && last == 2, "peer in 3: only 2");
    Check(pb::BackSeats(4, 0, first, last) && first == 2 && last == 3, "peer driving: 2..3");
    Check(!pb::BackSeats(3, 2, first, last), "a 3-seat vehicle with the peer in 2: none");
    Check(!pb::BackSeats(2, 0, first, last), "no back seat");
}

// --- The wrappers with bound fakes ---------------------------------------------
struct Fakes {
    unsigned nearest = 0, driver = 0, type = 0, count = 0;
    std::uint8_t first = 0xEE, last = 0xEE;
    std::uint8_t nearest_result = 2, driver_result = 0, type_value = 1;
    int count_value = 4;
    bool type_faults = false;
    DWORD error_seen = 0;
    std::uint64_t now = 1000;
} g;
volatile std::uint8_t* volatile g_null = nullptr;
std::uint8_t FakeNearest(void*, const float*, std::uint8_t first, std::uint8_t last) {
    ++g.nearest; g.first = first; g.last = last; g.error_seen = GetLastError();
    SetLastError(77);
    return g.nearest_result;
}
std::uint8_t FakeDriver(void*, const float*) { ++g.driver; g.error_seen = GetLastError(); return g.driver_result; }
std::uint8_t FakeType(void*) {
    ++g.type;
    if (g.type_faults) return *g_null;  // an access fault inside the getter
    return g.type_value;
}
int FakeCount(void*) { ++g.count; return g.count_value; }
std::uint64_t FakeNow() noexcept { return g.now; }

void Bind() {
    pb::detail::ResetForTest();
    g = Fakes{};
    pb::detail::BindForTest(&FakeNearest, &FakeDriver, &FakeType, &FakeCount, &FakeNow);
}
void Policy(pb::Mode mode, std::uint16_t vehicle = 25603, std::uint8_t seat = 0, std::uint32_t until = 4000, bool one_ok = false) {
    pb::Policy p{};
    p.vehicle = vehicle; p.peer_seat = seat; p.mode = mode; p.until_ms = until;
    p.front_ok = p.cab_ok = one_ok;
    pb::Publish(mode == pb::Mode::off ? 0 : pb::Pack(p));
}

void TestWrappers() {
    unsigned char unit[0x80]{};
    const std::uint16_t index = 3;  // 0x6400 | 3 = 25603
    std::memcpy(unit + pb::kUnitIndex, &index, 2);
    const float pos[4]{1, 2, 3, 0};
    pb::Stats s{};
    // Off: both are the stock calls with their own arguments; nothing read.
    Bind();
    Check(pb::detail::SiteAForTest(unit, pos, 0, 1) == 2 && g.nearest == 1 && g.first == 0 && g.last == 1, "off: A is the stock search");
    Check(pb::detail::SiteBForTest(unit, pos) == 0 && g.driver == 1 && g.type == 0 && g.count == 0, "off: B is the stock driver search");
    pb::ReadStats(s);
    Check(s.calls[0] == 1 && s.calls[1] == 1 && s.block[0] == 0 && s.steer[1] == 0 && s.faults == 0, "off: counted, no decision");
    // Block (host drives 25603, guest presses E): 4 at both sites, no search, no getter.
    Bind();
    Policy(pb::Mode::block);
    Check(pb::detail::SiteAForTest(unit, pos, 0, 1) == pb::kNoSeat && g.nearest == 0, "block: no ride icon (the pre-query fails)");
    Check(pb::detail::SiteBForTest(unit, pos) == pb::kNoSeat && g.driver == 0 && g.type == 0, "block: phase 1 takes the no-seat exit");
    pb::ReadStats(s);
    Check(s.block[0] == 1 && s.block[1] == 1 && s.last_vehicle == 25603 && Same(s.last_reason, "block"), "block counted per site");
    // Wrong vehicle: stock.
    unsigned char other[0x80]{};
    const std::uint16_t index4 = 4;
    std::memcpy(other + pb::kUnitIndex, &index4, 2);
    Check(pb::detail::SiteAForTest(other, pos, 0, 1) == 2 && g.nearest == 1 && g.first == 0 && g.last == 1, "another vehicle: stock");
    // Lease expiry: stock again.
    g.now = 4001;
    Check(pb::detail::SiteBForTest(unit, pos) == 0 && g.driver == 1, "the lease expired: stock");
    g.now = 1000;
    // The peer in a back seat: stock (the viewer may drive).
    Policy(pb::Mode::block, 25603, 2);
    Check(pb::detail::SiteBForTest(unit, pos) == 0 && g.driver == 2, "a peer passenger: stock");
    // Passenger (host drives, guest boards a back seat): the nearest search over 2..3 at both sites.
    Bind();
    Policy(pb::Mode::passenger);
    g.nearest_result = 3;
    Check(pb::detail::SiteAForTest(unit, pos, 0, 1) == 3 && g.nearest == 1 && g.first == 2 && g.last == 3 && g.type == 1 && g.count == 1,
          "passenger: A offers the nearest free back seat");
    g.nearest_result = 2;
    Check(pb::detail::SiteBForTest(unit, pos) == 2 && g.driver == 0 && g.nearest == 2 && g.first == 2 && g.last == 3,
          "passenger: B searches the back seats, never the driver's");
    pb::ReadStats(s);
    Check(s.steer[0] == 1 && s.steer[1] == 1 && s.last_seat[0] == 3 && s.last_seat[1] == 2 && s.last_type == 1 && s.last_count == 4 &&
              s.block[0] == 0, "passenger counted with the seat, type and seat count");
    // No free back seat within 10 m: 4, counted as a block.
    g.nearest_result = pb::kNoSeat;
    Check(pb::detail::SiteBForTest(unit, pos) == pb::kNoSeat && g.driver == 0, "no free back seat: 4");
    pb::ReadStats(s);
    Check(s.block[1] == 1 && s.steer[1] == 1, "a back-seat miss counts as a block");
    // A 3-seat light vehicle: only seat 2.
    g.count_value = 3; g.nearest_result = 2;
    Check(pb::detail::SiteAForTest(unit, pos, 0, 1) == 2 && g.first == 2 && g.last == 2, "3 seats: seat 2 only");
    // Truck -> block (no search).
    g.type_value = 3; g.count_value = 4;
    const unsigned searches = g.nearest;
    Check(pb::detail::SiteAForTest(unit, pos, 0, 1) == pb::kNoSeat && g.nearest == searches, "a truck falls back to block");
    pb::ReadStats(s);
    Check(Same(s.last_reason, "type") && s.last_type == 3, "the truck's reason and type");
    // A faulting getter: SEH, block, one fault.
    g.type_value = 1;
    g.type_faults = true;
    Check(pb::detail::SiteBForTest(unit, pos) == pb::kNoSeat && g.driver == 0, "a faulting getter blocks");
    pb::ReadStats(s);
    Check(s.faults == 1 && Same(s.last_reason, "facts_read") && s.last_type == 0xFF, "the fault is counted");
    g.type_faults = false;
    // An unreadable unit: SEH on the index read, stock search, one fault.
    void* bad = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x10));
    Check(pb::detail::SiteAForTest(bad, pos, 0, 1) == 2 && g.first == 0 && g.last == 1, "an unreadable unit index: stock");
    pb::ReadStats(s);
    Check(s.faults == 2, "the index fault is counted");
    // The last error: the original sees the caller's; a block returns it unchanged.
    Policy(pb::Mode::passenger);
    SetLastError(0x1234);
    pb::detail::SiteAForTest(unit, pos, 0, 1);
    Check(g.error_seen == 0x1234, "the original search sees the caller's last error");
    Policy(pb::Mode::block);
    SetLastError(0x4321);
    pb::detail::SiteBForTest(unit, pos);
    Check(GetLastError() == 0x4321, "a block keeps the caller's last error");
    pb::detail::ResetForTest();
}

// --- The pinned blocks and the install on a synthetic image -------------------
struct Image {
    unsigned char* base = nullptr;
    std::size_t span = 0;
    Image() {
        for (const std::uintptr_t rva : c::board_blocks)
            for (const auto& s : c::code_sites)
                if (s.rva == rva && s.rva + s.size > span) span = s.rva + s.size;
        span = (span + 0xFFFF) & ~static_cast<std::size_t>(0xFFFF);
        base = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(base != nullptr, "fixture image");
        std::memset(base, 0xCC, span);
        for (const auto& s : c::code_sites)
            if (s.rva + s.size <= span) std::memcpy(base + s.rva, s.bytes, s.size);
    }
    ~Image() { if (base) VirtualFree(base, 0, MEM_RELEASE); }
    std::uint32_t Rel(std::uintptr_t site) const { std::uint32_t v = 0; std::memcpy(&v, base + site + 1, 4); return v; }
};
std::vector<std::string> g_lines;
void Log(const char* line) noexcept { g_lines.emplace_back(line); }
bool Logged(const char* part) {
    for (const auto& l : g_lines) if (l.find(part) != std::string::npos) return true;
    return false;
}

void TestVerifyAndInstall() {
    Check(sizeof(c::board_blocks) / sizeof(c::board_blocks[0]) == 20, "20 board blocks are pinned");
    Check(c::board_a_site == 0x12458ac && c::board_a_next_ip == 0x12458b1 && c::board_a_rel32_orig == 0x00005aefu &&
              c::board_b_site == 0x1251479 && c::board_b_next_ip == 0x125147e && c::board_b_rel32_orig == 0xffff9ee2u,
          "the two sites and their original rel32");
    Check(c::board_a_next_ip + c::board_a_rel32_orig == c::board_nearest_rva &&
              static_cast<std::uint32_t>(c::board_b_next_ip + c::board_b_rel32_orig) == c::board_driver_rva,
          "the rel32 operands reach 0x124B3A0 and 0x124B360");
    {
        Image img;
        std::uintptr_t mismatch = 0;
        Check(pb::detail::VerifyImage(img.base, img.span, mismatch) == nullptr && mismatch == 0, "the board blocks verify");
        img.base[c::board_a_site + 2] ^= 0x40;
        Check(pb::detail::VerifyImage(img.base, img.span, mismatch) == nullptr, "the site A rel32 operand is masked");
        img.base[c::board_a_site + 2] ^= 0x40;
        img.base[c::board_a_site] ^= 1;
        Check(Same(pb::detail::VerifyImage(img.base, img.span, mismatch), "bytes") && mismatch == c::board_a_block_rva,
              "the call opcode itself is pinned");
        img.base[c::board_a_site] ^= 1;
        img.base[0x55E7F0] ^= 1;
        Check(Same(pb::detail::VerifyImage(img.base, img.span, mismatch), "bytes") && mismatch == 0x55E7F0, "a changed getter refuses");
        img.base[0x55E7F0] ^= 1;
        Check(Same(pb::detail::VerifyImage(img.base, 0x1000000, mismatch), "span"), "a short span refuses");
        Check(Same(pb::detail::VerifyImage(nullptr, img.span, mismatch), "base"), "no image");
    }
    {
        // Install: both sites retargeted to near stubs, live; a second call returns the first result.
        pb::detail::ResetForTest();
        g_lines.clear();
        Image img;
        Check(pb::Install(img.base, img.span, &FakeNow, &Log) == nullptr && pb::Installed() && pb::PatchLive(img.base),
              "installed and live on the synthetic image");
        Check(img.Rel(c::board_a_site) != c::board_a_rel32_orig && img.Rel(c::board_b_site) != c::board_b_rel32_orig &&
                  img.base[c::board_a_site] == 0xE8 && img.base[c::board_b_site] == 0xE8, "both rel32 retargeted, still E8 calls");
        Check(Logged("partner_board=installed; a=0x12458ac; b=0x1251479;"), "the install line");
        Check(pb::Install(img.base, img.span, &FakeNow, &Log) == nullptr, "a second install is a no-op");
        std::uint32_t saved = img.Rel(c::board_b_site);
        img.base[c::board_b_site + 1] ^= 1;
        Check(!pb::PatchLive(img.base), "a changed site B is not live");
        std::memcpy(img.base + c::board_b_site + 1, &saved, 4);
        Check(pb::PatchLive(img.base), "live again");
        pb::detail::ResetForTest();
    }
    {
        // B refused (its rel32 is not the original): A is reverted, nothing stays patched.
        g_lines.clear();
        Image img;
        const std::uint32_t foreign = c::board_b_rel32_orig + 0x10;
        std::memcpy(img.base + c::board_b_site + 1, &foreign, 4);
        const char* why = pb::Install(img.base, img.span, &FakeNow, &Log);
        Check(Same(why, "b_failed") && !pb::Installed() && !pb::PatchLive(img.base), "B refused: not installed");
        Check(img.Rel(c::board_a_site) == c::board_a_rel32_orig, "A reverted to the stock call");
        Check(Logged("partner_board=refused; reason=b_failed; at=b; why=bytes; a_reverted=1;"), "the refusal line");
        Check(Same(pb::Install(img.base, img.span, &FakeNow, &Log), "b_failed"), "the refusal sticks for the process");
        pb::detail::ResetForTest();
    }
    {
        // A pinned block differs: nothing patched.
        g_lines.clear();
        Image img;
        img.base[0x1250E96] ^= 1;
        Check(Same(pb::Install(img.base, img.span, &FakeNow, &Log), "bytes") && img.Rel(c::board_a_site) == c::board_a_rel32_orig &&
                  Logged("partner_board=refused; reason=bytes; rva=0x1250e96;"), "a changed block refuses before any patch");
        pb::detail::ResetForTest();
    }
    {
        // Every VerifyImage refusal keeps its own reason (the driver's board_install line).
        g_lines.clear();
        Image img;
        Check(Same(pb::Install(img.base, 0x1000000, &FakeNow, &Log), "span") && Same(pb::InstallRefusal(), "span") &&
                  img.Rel(c::board_a_site) == c::board_a_rel32_orig && Logged("partner_board=refused; reason=span;"),
              "a short span refuses as span, nothing patched");
        pb::detail::ResetForTest();
    }
}

// --- The local passenger report ------------------------------------------------
struct Memory {
    std::map<std::uintptr_t, std::uint8_t> bytes;
    template <class T> void Put(std::uintptr_t at, T value) {
        unsigned char raw[sizeof(T)]{};
        std::memcpy(raw, &value, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes[at + i] = raw[i];
    }
    bool operator()(std::uintptr_t at, void* out, std::size_t size) const {
        auto* o = static_cast<unsigned char*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = bytes.find(at + i);
            if (it == bytes.end()) return false;
            o[i] = it->second;
        }
        return true;
    }
};
struct Lookup {
    std::map<std::uint16_t, std::uintptr_t> recs;
    unsigned calls = 0;
    std::uintptr_t operator()(std::uint16_t id) {
        ++calls;
        const auto it = recs.find(id);
        return it == recs.end() ? 0 : it->second;
    }
};
constexpr std::uintptr_t kP = 0x10000, kQ = 0x20000, kActs = 0x30000, kUnits = 0x40000, kUnit = 0x50000, kIds = 0x60000, kOther = 0x70000;
void Riders(Memory& m, std::uintptr_t rec, std::uint16_t r0, std::uint16_t r1, std::uint16_t r2, std::uint16_t r3) {
    m.Put<std::uint16_t>(rec + pb::kUnitRiders, r0);
    m.Put<std::uint16_t>(rec + pb::kUnitRiders + 2, r1);
    m.Put<std::uint16_t>(rec + pb::kUnitRiders + 4, r2);
    m.Put<std::uint16_t>(rec + pb::kUnitRiders + 6, r3);
}
Memory Passenger() {
    Memory m;
    m.Put<std::uintptr_t>(kQ + pb::kQAct, kActs);
    m.Put<std::uint16_t>(kActs, pb::kActPassenger);
    m.Put<std::uint16_t>(kActs + 2, 1);
    m.Put<std::uintptr_t>(kP + pb::kPUnits, kUnits);
    m.Put<std::uintptr_t>(kUnits, kUnit);
    m.Put<std::uintptr_t>(kUnits + 8, 0);
    m.Put<std::uint16_t>(kUnit + pb::kUnitIndex, 3);
    m.Put<std::uintptr_t>(kQ + pb::kQVehicle, kIds);
    m.Put<std::uint16_t>(kIds, 25603);
    m.Put<std::uint16_t>(kIds + 2, 0xFFFF);
    Riders(m, kUnit, 0xFFFF, 0xFFFF, 0, 0xFFFF);
    return m;
}

void TestLocalPassenger() {
    {
        Memory m = Passenger();
        Lookup l;
        l.recs[25603] = kUnit;
        const pb::LocalPassenger lp = pb::ReadLocalPassenger(m, l, kP, kQ, 0);
        Check(lp.valid && lp.vehicle == 25603 && lp.seat == 2 && lp.via == 1 && Same(lp.reason, "ok"),
              "act 0x6E, P+0x120 -> 0x6400|3, rider[2] == 0: passenger in seat 2");
        Riders(m, kUnit, 0xFFFF, 0xFFFF, 0xFFFF, 0);
        const pb::LocalPassenger r = pb::ReadLocalPassenger(m, l, kP, kQ, 0);
        Check(r.valid && r.seat == 3, "rider[3]: seat 3");
        Riders(m, kUnit, 0xFFFF, 0, 0xFFFF, 0xFFFF);
        Check(!pb::ReadLocalPassenger(m, l, kP, kQ, 0).valid && Same(pb::ReadLocalPassenger(m, l, kP, kQ, 0).reason, "rider"),
              "only seats 2/3 count (seat 1 is never a player's)");
        Riders(m, kUnit, 0, 0xFFFF, 0xFFFF, 0xFFFF);
        Check(!pb::ReadLocalPassenger(m, l, kP, kQ, 0).valid, "the driver's seat is not a passenger report");
        Riders(m, kUnit, 0xFFFF, 0xFFFF, 1, 0xFFFF);
        Check(!pb::ReadLocalPassenger(m, l, kP, kQ, 0).valid, "another body's id in the back seat");
    }
    {
        Memory m = Passenger();
        Lookup l;
        l.recs[25603] = kUnit;
        m.Put<std::uint16_t>(kActs, 0x6F);
        const pb::LocalPassenger lp = pb::ReadLocalPassenger(m, l, kP, kQ, 0);
        Check(!lp.valid && Same(lp.reason, "act") && l.calls == 0, "act 0x6F (the driver): no report, no lookup");
        m.Put<std::uint16_t>(kActs, 1);
        Check(!pb::ReadLocalPassenger(m, l, kP, kQ, 0).valid, "on foot: no report");
    }
    {
        // P+0x120 does not resolve (stale or another record): the vehicle id from Q+0xE0.
        Memory m = Passenger();
        Lookup l;
        l.recs[25603] = kOther;
        Riders(m, kOther, 0xFFFF, 0xFFFF, 0xFFFF, 0);
        const pb::LocalPassenger lp = pb::ReadLocalPassenger(m, l, kP, kQ, 0);
        Check(lp.valid && lp.vehicle == 25603 && lp.seat == 3 && lp.via == 2, "the P+0x120 unit is not the live record: Q+0xE0");
        m.Put<std::uintptr_t>(kUnits, 0);
        Check(pb::ReadLocalPassenger(m, l, kP, kQ, 0).via == 2, "no P+0x120 unit: Q+0xE0");
        m.Put<std::uint16_t>(kIds, 0x2003);
        const pb::LocalPassenger none = pb::ReadLocalPassenger(m, l, kP, kQ, 0);
        Check(!none.valid && Same(none.reason, "vehicle"), "Q+0xE0 not a vehicle id: no report");
    }
    {
        Memory m = Passenger();
        Lookup l;
        Check(!pb::ReadLocalPassenger(m, l, kP, kQ, 0).valid, "the vehicle is not live: no report");
        l.recs[25603] = kUnit;
        Check(!pb::ReadLocalPassenger(m, l, 0, kQ, 0).valid && !pb::ReadLocalPassenger(m, l, kP, kQ, 2).valid, "bad state: no report");
        Memory empty;
        Check(Same(pb::ReadLocalPassenger(empty, l, kP, kQ, 0).reason, "act_read"), "unreadable acts");
        // Body 1 is never the local player in a valid state, but the index follows `local`.
        m.Put<std::uint16_t>(kActs + 2, pb::kActPassenger);
        m.Put<std::uintptr_t>(kUnits + 8, kUnit);
        Riders(m, kUnit, 0xFFFF, 0xFFFF, 0xFFFF, 1);
        const pb::LocalPassenger one = pb::ReadLocalPassenger(m, l, kP, kQ, 1);
        Check(one.valid && one.seat == 3, "local index 1 reads entry 1 and rider id 1");
    }
}

void TestReceiverGates() {
    Check(pb::LocalVehicleSeatAllowed(pb::Mode::passenger, 2) && pb::LocalVehicleSeatAllowed(pb::Mode::passenger, 3),
          "passenger: the peer in a back seat of the viewer's own vehicle");
    Check(!pb::LocalVehicleSeatAllowed(pb::Mode::passenger, 0) && !pb::LocalVehicleSeatAllowed(pb::Mode::passenger, 1) &&
              !pb::LocalVehicleSeatAllowed(pb::Mode::block, 2) && !pb::LocalVehicleSeatAllowed(pb::Mode::off, 3),
          "every other case keeps the local_vehicle refusal");
    pb::LocalPassenger lp{};
    lp.valid = true; lp.vehicle = 25603; lp.seat = 2;
    Check(pb::SharedWithLocal(lp, 25603, 2) && !pb::SharedWithLocal(lp, 25603, 3) && !pb::SharedWithLocal(lp, 25603, 0) &&
              !pb::SharedWithLocal(lp, 25604, 2), "shared_seat: the viewer's own back seat only");
    pb::LocalPassenger none{};
    Check(!pb::SharedWithLocal(none, 0xFFFF, 0xFF), "no report: never shared");
    Check(pb::FarExempt(pb::Mode::passenger, 25603, 25603, none) && pb::FarExempt(pb::Mode::passenger, 25603, 0xFFFF, lp),
          "far exempt: the viewer drives it, or rides that copy");
    Check(!pb::FarExempt(pb::Mode::passenger, 25604, 25603, lp) && !pb::FarExempt(pb::Mode::block, 25603, 25603, lp) &&
              !pb::FarExempt(pb::Mode::passenger, 0xFFFF, 0xFFFF, none), "far kept for any other vehicle and outside passenger mode");
}

// M6 seat 1 (docs/vehicles-companions-design.md 6.6, E6): the light vehicle's front seat and the truck cab seat.
void TestSeatOneDecide() {
    pb::Policy p{};
    p.vehicle = 25603; p.peer_seat = 0; p.mode = pb::Mode::passenger; p.until_ms = 5000;
    pb::SeatOpts none{}, front{}, cab{}, both{};
    front.front = true; cab.cab = true; both.front = both.cab = true;
    Check(!pb::SeatOne(none) && pb::SeatOne(front) && pb::SeatOne(cab) && pb::PackOpts(both) == 3 && pb::UnpackOpts(2).cab &&
              !pb::UnpackOpts(2).front, "the seat-1 options pack");
    // Review R3: without the driver's handshake (an older driver, its key off, its seat 1 taken) seat 1 is never offered.
    pb::Range h = pb::DecideSeats(p, true, 2, 4, front);
    Check(h.ok && h.first == 2 && h.last == 3 && Same(h.reason, "back_seats"), "front key, no handshake: the back seats only");
    h = pb::DecideSeats(p, true, 1, 2, front);
    Check(!h.ok && Same(h.reason, "front_peer"), "front key, no handshake, a 2-seat light vehicle: blocked (front_peer)");
    h = pb::DecideSeats(p, true, 4, 2, cab);
    Check(!h.ok && Same(h.reason, "cab_peer"), "cab key, no handshake: the truck blocks (cab_peer)");
    pb::Policy only_cab = p;
    only_cab.cab_ok = true;
    h = pb::DecideSeats(only_cab, true, 2, 4, both);
    Check(h.ok && h.first == 2 && Same(h.reason, "back_seats"), "the driver accepts the cab seat only: a light vehicle's back seats");
    p.front_ok = p.cab_ok = true;
    pb::Range r = pb::DecideSeats(p, true, 2, 4, front);
    Check(r.ok && r.first == 1 && r.last == 3 && Same(r.reason, "front_seat"), "front: seats 1..3 of a 4-seat light vehicle");
    r = pb::DecideSeats(p, true, 1, 2, front);
    Check(r.ok && r.first == 1 && r.last == 1, "front: a 2-seat light vehicle offers seat 1 only");
    r = pb::DecideSeats(p, true, 2, 4, none);
    Check(r.ok && r.first == 2 && r.last == 3 && Same(r.reason, "back_seats"), "without the key: the back seats only");
    Check(!pb::DecideSeats(p, true, 1, 2, none).ok && Same(pb::DecideSeats(p, true, 1, 2, none).reason, "seats"),
          "without the key a 2-seat light vehicle blocks");
    Check(!pb::DecideSeats(p, true, 4, 2, front).ok && Same(pb::DecideSeats(p, true, 4, 2, front).reason, "type"),
          "the front key alone: a truck blocks");
    r = pb::DecideSeats(p, true, 4, 2, cab);
    Check(r.ok && r.first == 1 && r.last == 1 && Same(r.reason, "cab_seat"), "cab: truck seat 1");
    r = pb::DecideSeats(p, true, 3, 4, cab);
    Check(r.ok && r.first == 1 && r.last == 1, "cab: never a seat 2/3 of a truck (its bed is not a seat)");
    Check(!pb::DecideSeats(p, true, 4, 1, cab).ok && Same(pb::DecideSeats(p, true, 4, 1, cab).reason, "seats"), "a one-seat truck blocks");
    r = pb::DecideSeats(p, true, 2, 4, cab);
    Check(r.ok && r.first == 2 && r.last == 3, "the cab key alone: a light vehicle keeps its back seats");
    Check(!pb::DecideSeats(p, true, 5, 2, both).ok && !pb::DecideSeats(p, true, 8, 4, both).ok &&
              Same(pb::DecideSeats(p, true, 7, 2, both).reason, "type"), "armoured vehicles stay blocked");
    Check(!pb::DecideSeats(p, false, 4, 2, cab).ok && Same(pb::DecideSeats(p, false, 4, 2, cab).reason, "facts_read"), "unread facts");
    pb::Policy q = p; q.peer_seat = 1;
    Check(!pb::DecideSeats(q, true, 4, 2, cab).ok && Same(pb::DecideSeats(q, true, 4, 2, cab).reason, "no_seat"),
          "the peer in the cab seat: none");
    r = pb::DecideSeats(q, true, 2, 4, front);
    Check(r.ok && r.first == 2 && r.last == 3 && Same(r.reason, "back_seats"), "the peer in seat 1: the front seat is excluded");
    // The phase-1 immediates.
    Check(pb::PhaseOneLimit(pb::Verdict::seats, 1, true) == 1, "a steered seat 1: cmp al,1 at both sites");
    Check(pb::PhaseOneLimit(pb::Verdict::seats, 2, true) == 2 && pb::PhaseOneLimit(pb::Verdict::seats, 4, true) == 2 &&
              pb::PhaseOneLimit(pb::Verdict::stock, 1, true) == 2 && pb::PhaseOneLimit(pb::Verdict::block, 4, true) == 2 &&
              pb::PhaseOneLimit(pb::Verdict::seats, 1, false) == 2, "every other call: stock cmp al,2");
    Check(pb::PhaseOneDword(c::board_seat1_pass_orig, 1) == 0x0872013Cu && pb::PhaseOneDword(c::board_seat1_phase_orig, 1) == 0x920F013Cu &&
              pb::PhaseOneDword(0x0872013Cu, 2) == c::board_seat1_pass_orig, "the immediate is byte 1 of each dword");
    Check(c::board_seat1_pass_orig == 0x0872023Cu && c::board_seat1_phase_orig == 0x920F023Cu && c::board_remap_orig == 0x7501FF80u &&
              c::board_remap_skip == 0xEB01FF80u && c::board_seat1_pass_rva == 0x1251834 && c::board_seat1_phase_rva == 0x1251894 &&
              c::board_remap_rva == 0xB79A40, "the contract's seat-1 dwords");
    // The receiver gate.
    Check(pb::LocalVehicleSeatAllowed(pb::Mode::passenger, 1, true) && !pb::LocalVehicleSeatAllowed(pb::Mode::passenger, 1, false) &&
              !pb::LocalVehicleSeatAllowed(pb::Mode::block, 1, true) && !pb::LocalVehicleSeatAllowed(pb::Mode::passenger, 0, true) &&
              pb::LocalVehicleSeatAllowed(pb::Mode::passenger, 2, true), "seat 1 of the viewer's own vehicle only with seat 1 on");
}

std::vector<std::uint8_t> g_limits;
bool g_limit_ok = true;
bool FakeLimit(std::uint8_t limit) noexcept {
    g_limits.push_back(limit);
    return g_limit_ok;
}
void TestSeatOneWrappers() {
    unsigned char unit[0x80]{};
    const std::uint16_t index = 3;  // 25603
    std::memcpy(unit + pb::kUnitIndex, &index, 2);
    const float pos[4]{1, 2, 3, 0};
    pb::SeatOpts front{};
    front.front = true;
    {
        Bind();
        g_limits.clear();
        g_limit_ok = true;
        pb::detail::BindSeatOneForTest(front, &FakeLimit);
        Policy(pb::Mode::passenger, 25603, 0, 4000, true);
        g.type_value = 2; g.count_value = 4; g.nearest_result = 1;
        Check(pb::detail::SiteBForTest(unit, pos) == 1 && g.first == 1 && g.last == 3, "site B searches 1..3 and steers seat 1");
        Check(g_limits.size() == 1 && g_limits[0] == 1, "both phase-1 immediates go to 1 for this call");
        pb::Stats s{};
        pb::ReadStats(s);
        Check(s.seat_one == 1 && s.steer[1] == 1 && s.last_seat[1] == 1 && s.faults == 0, "counted as a seat-1 steer");
        g.nearest_result = 2;
        Check(pb::detail::SiteBForTest(unit, pos) == 2 && g_limits.size() == 2 && g_limits[1] == 2, "a back seat: stock 2");
        Policy(pb::Mode::off);
        g.driver_result = 1;
        Check(pb::detail::SiteBForTest(unit, pos) == 1 && g.driver == 1 && g_limits.size() == 3 && g_limits[2] == 2,
              "policy off: the stock driver search, stock 2 (a right-door driver stays a driver)");
        g.nearest_result = 1;
        g.first = g.last = 0xEE;
        Policy(pb::Mode::passenger, 25603, 0, 4000, true);
        Check(pb::detail::SiteAForTest(unit, pos, 0, 1) == 1 && g.first == 1 && g.last == 3 && g_limits.size() == 3,
              "site A offers seat 1 too and never touches the immediates");
        g_limit_ok = false;
        Check(pb::detail::SiteBForTest(unit, pos) == pb::kNoSeat, "a failed immediate write never seats the viewer in seat 1");
        pb::ReadStats(s);
        Check(s.faults == 1 && s.seat_one == 1, "the failed write is a fault");
        g.nearest_result = 2;
        Check(pb::detail::SiteBForTest(unit, pos) == 2, "a back seat passes even when the restore failed");
        pb::detail::ResetForTest();
    }
    {
        // Seat 1 off: the wrappers never call the writer and never offer seat 1.
        Bind();
        g_limits.clear();
        Policy(pb::Mode::passenger, 25603, 0, 4000, true);
        g.type_value = 2; g.count_value = 4; g.nearest_result = 2;
        Check(pb::detail::SiteBForTest(unit, pos) == 2 && g.first == 2 && g.last == 3 && g_limits.empty(), "off: back seats, no writer");
        pb::detail::ResetForTest();
    }
    {
        // The truck cab with the cab key.
        Bind();
        g_limits.clear();
        g_limit_ok = true;
        pb::SeatOpts cab{};
        cab.cab = true;
        pb::detail::BindSeatOneForTest(cab, &FakeLimit);
        Policy(pb::Mode::passenger, 25603, 0, 4000, true);
        g.type_value = 4; g.count_value = 2; g.nearest_result = 1;
        Check(pb::detail::SiteBForTest(unit, pos) == 1 && g.first == 1 && g.last == 1 && g_limits.size() == 1 && g_limits[0] == 1,
              "cab: truck seat 1 with both immediates at 1");
        pb::detail::ResetForTest();
    }
}

void TestSeatOneInstall() {
    {
        pb::detail::ResetForTest();
        g_lines.clear();
        Image img;
        pb::SeatOpts front{};
        front.front = true;
        Check(img.base[0xB79A43] == 0x75 && img.base[0x1251835] == 2 && img.base[0x1251895] == 2, "the stock seat-1 bytes");
        Check(pb::Install(img.base, img.span, &FakeNow, &Log, front) == nullptr && pb::LiveSeatOpts().front && !pb::LiveSeatOpts().cab,
              "installed with seat 1");
        Check(img.base[0xB79A43] == 0xEB && img.base[0x1251835] == 2 && img.base[0x1251895] == 2, "the remap is a jmp; phase 1 still stock");
        Check(Logged("partner_board=seat_one; result=ok; reason=none; front=1; cab=0; remap=0xb79a40; sites=0x1251834,0x1251894"),
              "the seat_one line");
        std::uintptr_t mismatch = 0;
        img.base[0x1251835] = 1;
        Check(pb::detail::VerifyImage(img.base, img.span, mismatch) == nullptr, "a phase-1 immediate at 1 still verifies");
        img.base[0x1251835] = 2;
        // Review R5: the owned bytes verify only in one of their two states.
        img.base[0x1251895] = 3;
        Check(Same(pb::detail::VerifyImage(img.base, img.span, mismatch), "bytes") && mismatch == 0x125188e,
              "a phase-1 immediate that is neither 1 nor 2 refuses");
        img.base[0x1251895] = 0;
        Check(Same(pb::detail::VerifyImage(img.base, img.span, mismatch), "bytes"), "an immediate of 0 refuses");
        img.base[0x1251895] = 2;
        // Review R5: site B lost while a steered call left the immediates at 1: the tick puts them back to 2.
        img.base[0x1251835] = 1;
        img.base[0x1251895] = 1;
        Check(!pb::RestorePhaseOneIfBLost(img.base) && img.base[0x1251835] == 1, "site B live: the immediates stay site B's");
        std::uint32_t saved = img.Rel(c::board_b_site);
        img.base[c::board_b_site + 1] ^= 1;
        Check(pb::RestorePhaseOneIfBLost(img.base) && img.base[0x1251835] == 2 && img.base[0x1251895] == 2,
              "site B lost: both immediates back to the stock 2");
        Check(!pb::RestorePhaseOneIfBLost(img.base), "once: nothing left to restore");
        std::memcpy(img.base + c::board_b_site + 1, &saved, 4);
        pb::detail::ResetForTest();
    }
    {
        // A changed seat-1 block: the back seats stay, seat 1 is refused, nothing of it written.
        pb::detail::ResetForTest();
        g_lines.clear();
        Image img;
        pb::SeatOpts cab{};
        cab.cab = true;
        img.base[0xB79A57] ^= 1;
        Check(pb::Install(img.base, img.span, &FakeNow, &Log, cab) == nullptr && pb::Installed() && !pb::SeatOne(pb::LiveSeatOpts()),
              "the board installs, seat 1 does not");
        Check(img.base[0xB79A43] == 0x75 && Logged("partner_board=seat_one; result=refused; reason=bytes; front=0; cab=1;"),
              "the remap is untouched and the refusal is logged");
        pb::detail::ResetForTest();
    }
    {
        // Review R5: the remap opcode is owned in two states only (jne 0x75, jmp 0xEB).
        pb::detail::ResetForTest();
        g_lines.clear();
        Image img;
        pb::SeatOpts front{};
        front.front = true;
        img.base[0xB79A43] = 0x74;
        Check(pb::Install(img.base, img.span, &FakeNow, &Log, front) == nullptr && !pb::SeatOne(pb::LiveSeatOpts()) &&
                  img.base[0xB79A43] == 0x74 && Logged("partner_board=seat_one; result=refused; reason=bytes;"),
              "a remap opcode that is neither jne nor jmp refuses seat 1");
        pb::detail::ResetForTest();
    }
    {
        // Without the options nothing of seat 1 is touched or logged.
        pb::detail::ResetForTest();
        g_lines.clear();
        Image img;
        Check(pb::Install(img.base, img.span, &FakeNow, &Log) == nullptr && img.base[0xB79A43] == 0x75 && !Logged("partner_board=seat_one"),
              "no options: no seat-1 install");
        pb::detail::ResetForTest();
    }
}

// M6 seat races (partner_ride_race; review R1/R2): site C (the pre-query's final answer) records the want and
// answers false for a denied vehicle or, on a holding guest, for an ungranted one; site B is the backstop;
// site A no longer races. Stock verdict only (a peer-driver policy for the vehicle decides as before).
struct PreFake {
    unsigned calls = 0;
    bool ok = true;
    std::uint8_t seat = 0;
    DWORD error_seen = 0;
} pf;
bool FakePrequery(void*, std::uint32_t body, void*, std::uint16_t id, char* seat) noexcept {
    ++pf.calls;
    pf.error_seen = GetLastError();
    Check(body == 0 && id == 7, "site C passes the caller's body and id through");
    if (seat) *seat = static_cast<char>(pf.seat);
    SetLastError(55);
    return pf.ok;
}
void TestRace() {
    unsigned char unit[0x80]{};
    const std::uint16_t index = 8;  // 25608
    std::memcpy(unit + pb::kUnitIndex, &index, 2);
    unsigned char other[0x80]{};
    const std::uint16_t index9 = 9;
    std::memcpy(other + pb::kUnitIndex, &index9, 2);
    const float pos[4]{1, 2, 3, 0};
    char seat = 4;
    const auto C = [&](void* u) { seat = 4; return pb::detail::SiteCForTest(nullptr, 0, u, 7, &seat); };
    Check(pb::RaceDenied(25608, 4000, 1000, 25608) && !pb::RaceDenied(25608, 4000, 1000, 25609) && !pb::RaceDenied(25608, 999, 1000, 25608) &&
              !pb::RaceDenied(pb::kVehicleUnknown, 4000, 1000, 0xFFFF) && !pb::RaceDenied(25608, 7000, 1000, 25608),
          "a deny is that vehicle, inside its window (at most 5 s ahead)");
    // The pure verdict: deny first, then a hold unless that vehicle is granted.
    pb::RaceIn ri{};
    ri.vehicle = 25608; ri.now_ms = 1000;
    Check(pb::RaceDecide(ri) == pb::RaceVerdict::pass, "nothing live: pass");
    ri.hold_on = true; ri.hold_until = 2000;
    Check(pb::RaceDecide(ri) == pb::RaceVerdict::hold, "a live hold without a grant: hold");
    ri.grant_vehicle = 25609; ri.grant_until = 3000;
    Check(pb::RaceDecide(ri) == pb::RaceVerdict::hold, "a grant for another vehicle: still hold");
    ri.grant_vehicle = 25608;
    Check(pb::RaceDecide(ri) == pb::RaceVerdict::pass, "the vehicle granted: pass");
    ri.deny_vehicle = 25608; ri.deny_until = 2500;
    Check(pb::RaceDecide(ri) == pb::RaceVerdict::deny, "a deny wins over a grant");
    ri.deny_vehicle = pb::kVehicleUnknown; ri.grant_vehicle = pb::kVehicleUnknown; ri.hold_until = 999;
    Check(pb::RaceDecide(ri) == pb::RaceVerdict::pass, "an expired hold: pass");
    ri.hold_until = 1000 + pb::kRaceDenyMaxMs + 1;
    Check(pb::RaceDecide(ri) == pb::RaceVerdict::pass, "a hold further than 5 s ahead (a foreign clock): pass");
    Bind();
    pf = PreFake{};
    pb::detail::BindPrequeryForTest(&FakePrequery);
    pb::RaceState rs{};
    pb::ReadRace(rs);
    Check(!rs.on && rs.want == pb::kVehicleUnknown && rs.want_age_ms == 0xFFFFFFFFu && rs.deny == pb::kVehicleUnknown && !rs.hold &&
              rs.grant == pb::kVehicleUnknown, "off, nothing wanted, held or granted");
    Check(C(unit) && seat == 0 && pf.calls == 1, "race off: site C is the stock pre-query");
    pb::ReadRace(rs);
    Check(rs.want == pb::kVehicleUnknown, "race off: no want recorded");
    pb::SetRace(true);
    SetLastError(0x2222);
    Check(C(unit) && seat == 0, "race on, nothing live: the stock answer");
    Check(pf.error_seen == 0x2222 && GetLastError() == 55, "the original sees the caller's last error and sets its own");
    pb::ReadRace(rs);
    Check(rs.on && rs.want == 25608 && rs.want_age_ms == 0, "the pre-query's final answer (true, seat 0) is the want");
    g.now = 1300;
    pb::ReadRace(rs);
    Check(rs.want_age_ms == 300, "the want ages");
    pf.ok = false;
    Check(!C(other), "the pre-query refused (no usable door, the line check)");
    pb::ReadRace(rs);
    Check(rs.want == 25608 && rs.want_age_ms == 300, "a refused pre-query is no want (review R2)");
    pf.ok = true; pf.seat = 2;
    Check(C(other) && seat == 2, "a back seat answer");
    pb::ReadRace(rs);
    Check(rs.want == 25608, "is no want");
    pf.seat = 1;
    Check(C(other), "seat 1 from the stock search (phase 1 makes it the driver)");
    pb::ReadRace(rs);
    Check(rs.want == 25609 && rs.want_age_ms == 0, "is a want");
    pf.seat = 0;
    // Site A no longer takes part in the race.
    pb::RaceDeny(25608, 3000);
    g.nearest_result = 0;
    Check(pb::detail::SiteAForTest(unit, pos, 0, 1) == 0, "site A: the stock nearest search even while denied");
    pb::ReadRace(rs);
    Check(rs.deny == 25608 && rs.deny_left_ms == 3000 && rs.hits == 0, "the deny is live for 3 s");
    Check(!C(unit), "denied: site C answers false (no ride icon)");
    pb::ReadRace(rs);
    Check(rs.hits == 1 && rs.want == 25608, "counted; the denied player still wants it (host_first stays true for the host)");
    g.driver_result = 1;
    Check(pb::detail::SiteBForTest(unit, pos) == pb::kNoSeat, "denied: phase 1 takes the no-seat exit");
    Check(C(other), "another vehicle is not denied");
    pb::ReadRace(rs);
    Check(rs.hits == 2, "site B's deny counted");
    // The guest's hold (review R1): no driver's seat of an empty vehicle without the host's grant.
    pb::RaceDeny(pb::kVehicleUnknown, 0);
    pb::RaceHold(2000);
    pb::ReadRace(rs);
    Check(rs.hold && rs.hold_left_ms == 2000, "the hold lease");
    Check(!C(unit), "held: no ride icon for an ungranted vehicle");
    pb::ReadRace(rs);
    Check(rs.holds == 1 && rs.want == 25608 && rs.hits == 2, "a hold is counted apart from the deny; the want is recorded");
    g.driver_result = 0;
    Check(pb::detail::SiteBForTest(unit, pos) == pb::kNoSeat, "held: phase 1 never seats the guest as the driver of the wanted vehicle");
    Check(pb::detail::SiteBForTest(other, pos) == 0, "site B holds only the vehicle site C saw (no want for it: stock)");
    pb::RaceGrant(25608, 3000);
    pb::ReadRace(rs);
    Check(rs.grant == 25608 && rs.grant_left_ms == 3000, "the host's grant");
    Check(C(unit) && seat == 0, "granted: the ride icon");
    Check(pb::detail::SiteBForTest(unit, pos) == 0, "granted: phase 1 seats the driver");
    pb::RaceDeny(25608, 3000);
    pb::ReadRace(rs);
    Check(rs.grant == pb::kVehicleUnknown && rs.deny == 25608, "a deny for the granted vehicle withdraws the grant");
    pb::RaceDeny(pb::kVehicleUnknown, 0);
    Check(!C(unit), "held again");
    g.now = 3301;
    Check(C(unit), "the hold lease ran out: stock (a silent copy never holds the guest for good)");
    pb::RaceHold(2000);
    pb::RaceHold(0);
    pb::ReadRace(rs);
    Check(!rs.hold && C(unit), "hold 0 clears");
    // A peer-driver policy for the vehicle: no race at all (the passenger policy decides; no want, no hold).
    pb::RaceHold(2000);
    Policy(pb::Mode::passenger, 25608, 0, 6000);
    pb::ReadRace(rs);
    const std::uint32_t holds = rs.holds;
    g.now = 3400;
    pf.seat = 1;
    Check(C(unit) && seat == 1, "the peer drives it: a steered seat-1 passenger answer passes");
    pb::ReadRace(rs);
    Check(rs.holds == holds && rs.want_age_ms > 0, "and is neither held nor a want");
    Policy(pb::Mode::off);
    pb::RaceHold(0);
    pf.seat = 0;
    pb::RaceDeny(25608, 60000);
    pb::ReadRace(rs);
    Check(rs.deny == 25608 && rs.deny_left_ms == pb::kRaceDenyMaxMs, "a deny is capped at 5 s");
    pb::RaceGrant(25608, 60000);
    pb::ReadRace(rs);
    Check(rs.grant == 25608 && rs.grant_left_ms == pb::kRaceDenyMaxMs && rs.deny == pb::kVehicleUnknown,
          "a grant is capped at 5 s and withdraws the deny for that vehicle");
    pb::RaceGrant(pb::kVehicleUnknown, 0);
    pb::RaceDeny(pb::kVehicleUnknown, 0);
    pb::ReadRace(rs);
    Check(rs.deny == pb::kVehicleUnknown && rs.grant == pb::kVehicleUnknown, "cleared");
    pb::RaceDeny(0x2003, 1000);
    pb::RaceGrant(0x2003, 1000);
    pb::ReadRace(rs);
    Check(rs.deny == pb::kVehicleUnknown && rs.grant == pb::kVehicleUnknown, "a non-vehicle id is never denied or granted");
    pb::detail::ResetForTest();
}

// Review R1/R2: site C is installed only with the race key, after A and B; its operand is masked like theirs.
void TestRaceInstall() {
    Check(c::board_c_site == 0x1246a4f && c::board_c_next_ip == 0x1246a54 && c::board_c_rel32_orig == 0xffffedccu &&
              static_cast<std::uint32_t>(c::board_c_next_ip + c::board_c_rel32_orig) == c::board_prequery_rva &&
              c::board_c_block_rva == 0x1246a3f, "site C: the CanExec call of the pre-query 0x1245820");
    {
        pb::detail::ResetForTest();
        g_lines.clear();
        Image img;
        std::uintptr_t mismatch = 0;
        img.base[c::board_c_site + 3] ^= 0x10;
        Check(pb::detail::VerifyImage(img.base, img.span, mismatch) == nullptr, "the site C rel32 operand is masked");
        img.base[c::board_c_site + 3] ^= 0x10;
        Check(Same(pb::InstallRace(), "board"), "no race before the board is installed");
        Check(pb::Install(img.base, img.span, &FakeNow, &Log) == nullptr && img.Rel(c::board_c_site) == c::board_c_rel32_orig,
              "the board alone leaves site C stock");
        Check(pb::InstallRace() == nullptr && pb::LiveRace() && img.Rel(c::board_c_site) != c::board_c_rel32_orig &&
                  img.base[c::board_c_site] == 0xE8 && pb::PatchLive(img.base), "the race retargets site C, live");
        Check(Logged("partner_board=race; result=ok; c=0x1246a4f;"), "the race line");
        Check(pb::InstallRace() == nullptr, "a second race install is a no-op");
        const std::uint32_t saved = img.Rel(c::board_c_site);
        img.base[c::board_c_site + 1] ^= 1;
        Check(!pb::PatchLive(img.base), "a changed site C is not live");
        std::memcpy(img.base + c::board_c_site + 1, &saved, 4);
        pb::detail::ResetForTest();
    }
    {
        pb::detail::ResetForTest();
        g_lines.clear();
        Image img;
        const std::uint32_t foreign = c::board_c_rel32_orig + 0x10;
        Check(pb::Install(img.base, img.span, &FakeNow, &Log) == nullptr, "installed");
        std::memcpy(img.base + c::board_c_site + 1, &foreign, 4);
        Check(Same(pb::InstallRace(), "c_bytes") && !pb::LiveRace() && pb::PatchLive(img.base) &&
                  Logged("partner_board=race; result=refused; reason=c_bytes;"), "a foreign site C: no race, the board stays");
        pb::detail::ResetForTest();
    }
}

// Review R8: the MGS_COOP_BOARD_SEAT_V1 request parser (game_script_observer.cpp SelectBoardSeatReceipt).
pb::SeatRequest Req(const char* text) { return pb::ParseSeatRequest(text, std::strlen(text)); }
void TestSeatRequest() {
    Check(Req("poll").cmd == pb::SeatCmd::poll && Req("clear").cmd == pb::SeatCmd::clear, "poll and clear");
    pb::SeatRequest r = Req("deny 25608 3000");
    Check(r.cmd == pb::SeatCmd::deny && r.vehicle == 25608 && r.ms == 3000, "deny <vehicle> <ms>");
    r = Req("grant 26111 5000");
    Check(r.cmd == pb::SeatCmd::grant && r.vehicle == 26111 && r.ms == 5000, "grant <vehicle> <ms> (the top id, the cap)");
    r = Req("deny 25600 1");
    Check(r.cmd == pb::SeatCmd::deny && r.vehicle == 25600 && r.ms == 1, "the first id, 1 ms");
    r = Req("hold 2000");
    Check(r.cmd == pb::SeatCmd::hold && r.ms == 2000 && r.vehicle == pb::kVehicleUnknown, "hold <ms>");
    r = Req("hold 0");
    Check(r.cmd == pb::SeatCmd::hold && r.ms == 0, "hold 0 clears");
    const char* bad[] = {"", "pol", "polls", "poll ", " poll", "clear 1", "deny", "deny 25608", "deny 25608 ", "deny 25608 3000 ",
                         "deny 25608 3000x", "deny 25608 3000 1", "deny  25608 3000", "deny 25608  3000", "deny 25599 3000",
                         "deny 26112 3000", "deny 25608 0", "deny 25608 5001", "deny -25608 3000", "deny +25608 3000",
                         "deny 25608 -1", "deny 0x6408 100", "deny 25608 99999999999", "deny 4294992904 100", "grant 25608 0",
                         "grant 25608", "hold", "hold 5001", "hold -1", "hold 1 2", "HOLD 100", "deny 25608 3000\n"};
    for (const char* b : bad) {
        if (Req(b).cmd != pb::SeatCmd::bad) {
            std::fprintf(stderr, "accepted: '%s'\n", b);
            Check(false, "a malformed request is refused");
        }
    }
    Check(pb::ParseSeatRequest(nullptr, 4).cmd == pb::SeatCmd::bad, "no text");
    Check(pb::ParseSeatRequest("poll", 3).cmd == pb::SeatCmd::bad, "the length is the request's (no NUL needed)");
    char unterminated[4] = {'p', 'o', 'l', 'l'};
    Check(pb::ParseSeatRequest(unterminated, 4).cmd == pb::SeatCmd::poll, "an unterminated buffer");
}

void TestSeatOneLocalPassenger() {
    Memory m = Passenger();
    Lookup l;
    l.recs[25603] = kUnit;
    Riders(m, kUnit, 0xFFFF, 0, 0xFFFF, 0xFFFF);
    Check(!pb::ReadLocalPassenger(m, l, kP, kQ, 0).valid, "seat 1 off: rider[1] is not a passenger report");
    const pb::LocalPassenger lp = pb::ReadLocalPassenger(m, l, kP, kQ, 0, true);
    Check(lp.valid && lp.vehicle == 25603 && lp.seat == 1 && Same(lp.reason, "ok"), "seat 1 on: rider[1] == local is seat 1");
    Riders(m, kUnit, 0xFFFF, 0xFFFF, 0, 0xFFFF);
    Check(pb::ReadLocalPassenger(m, l, kP, kQ, 0, true).seat == 2, "seat 1 on: the back seats still read");
    Riders(m, kUnit, 0, 0xFFFF, 0xFFFF, 0xFFFF);
    Check(!pb::ReadLocalPassenger(m, l, kP, kQ, 0, true).valid, "seat 1 on: the driver's seat is still no passenger");
}

void TestSurprise() {
    pb::SurpriseIn in{};
    in.installed = in.live = true;
    Check(pb::SurpriseReason(in) == nullptr, "nothing unusual");
    pb::SurpriseIn x = in; x.live = false;
    Check(Same(pb::SurpriseReason(x), "patch_lost"), "a lost patch");
    x = in; x.new_faults = 1;
    Check(Same(pb::SurpriseReason(x), "fault"), "a hook fault");
    x = in; x.peer_driver = true; x.policy_vehicle = 25603; x.local_vehicle = 25603;
    Check(Same(pb::SurpriseReason(x), "two_drivers"), "the viewer drives the vehicle the peer drives");
    x.local_vehicle = 25604;
    Check(pb::SurpriseReason(x) == nullptr, "another vehicle");
    x = in; x.steer_pending = true; x.steer_seat = 2; x.steer_ms = 1000; x.now_ms = 3999;
    Check(pb::SurpriseReason(x) == nullptr, "a steered seat inside its confirmation window");
    x.now_ms = 4000;
    Check(Same(pb::SurpriseReason(x), "steer_unconfirmed"), "the steered seat never showed up");
    x.lp.valid = true; x.lp.vehicle = 25603; x.lp.seat = 3;
    Check(Same(pb::SurpriseReason(x), "seat_mismatch"), "another seat than the steered one");
    x.lp.seat = 2;
    Check(pb::SurpriseReason(x) == nullptr, "confirmed");
}
} // namespace

int main() {
    TestPackAndLease();
    TestPolicyFor();
    TestDecide();
    TestWrappers();
    TestVerifyAndInstall();
    TestLocalPassenger();
    TestReceiverGates();
    TestSeatOneDecide();
    TestSeatOneWrappers();
    TestSeatOneInstall();
    TestSeatOneLocalPassenger();
    TestRace();
    TestRaceInstall();
    TestSeatRequest();
    TestSurprise();
    std::printf("partner_board_tests: %u checks passed\n", checks);
    return 0;
}
