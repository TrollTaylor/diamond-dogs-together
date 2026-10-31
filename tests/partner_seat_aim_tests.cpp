// G2 back-seat aim: the read-only census (partner_seat_aim.hpp) on fake memory. Synthetic only:
// a byte map stands in for the game; nothing here runs or reads game code. Covers the flag record,
// act and gun-record readers, what is watched, the flag / aim / fire classification, the change key,
// the per-watch episode (armed / aim time, rising edges), the Tick lines (first sample, changes,
// the per-second retry, the mission cap, the end line) and the status line. Also checks the generated
// contract carries the same offsets and bits.
#include "mgscoop/partner_seat_aim.hpp"
#include "partner_player_contract.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace sa = mgscoop::partner_seat_aim;
namespace k = mgscoop::partner_seat_aim::k;
namespace c = mgscoop::partner_player_contract;

namespace {
unsigned checks = 0;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "failed: %s\n", label);
        std::exit(1);
    }
}
bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// A sparse fake address space: a read succeeds only when every byte was put.
struct Fake {
    std::map<std::uintptr_t, unsigned char> bytes;
    unsigned reads = 0;
    template <class T>
    void Put(std::uintptr_t at, T value) {
        unsigned char raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes[at + i] = raw[i];
    }
    void Erase(std::uintptr_t at, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) bytes.erase(at + i);
    }
    bool Read(std::uintptr_t at, void* out, std::size_t size) {
        ++reads;
        auto* o = static_cast<unsigned char*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = bytes.find(at + i);
            if (it == bytes.end()) return false;
            o[i] = it->second;
        }
        return true;
    }
};
auto Reader(Fake& m) {
    return [&m](std::uintptr_t at, void* out, std::size_t size) noexcept { return m.Read(at, out, size); };
}

constexpr std::uintptr_t kPs = 0x7FF000010000ull, kRecords = 0x7FF000020000ull, kQ = 0x7FF000030000ull;
constexpr std::uintptr_t kActs = 0x7FF000040000ull, kUppers = 0x7FF000050000ull, kGunRec = 0x7FF000060000ull;

void TestContract() {
    Check(c::ps_rec280 == k::kPsFlags && c::ps_rec280_stride == k::kFlagStride && c::ps_base_id == k::kPsBaseId,
          "the flag record offsets match the contract");
    Check(c::q_anim_18 == k::kQActs && c::q_upper == k::kQUpper, "the Q act / upper arrays match the contract");
    Check(c::gun_class == k::kRecPosture && c::gun_next_state == k::kRecNext && c::flag_word6 == k::kFlagWord6,
          "the gun record posture / next state and the flag word match the contract");
    Check(c::flag_w0_armed == k::kW0Armed && c::flag_w6_c2 == k::kW6NoWeaponB && c::flag_w6_c3 == k::kW6NoWeapon &&
              c::flag_w6_cf == k::kW6NoHold && c::flag_w6_d8 == k::kW6Slow,
          "the flag bits match the contract");
    Check(c::act_ride_passenger == k::kActPassenger && c::act_ride_driver == k::kActDriver && c::act_ride_getoff == k::kActGetOff &&
              c::upper_gun_hold == k::kUpperGunHold,
          "the acts and the gun-hold upper act match the contract");
    Check(c::gun_mode_aim == k::kModeAim && c::gun_mode_fire == k::kModeFire && c::gun_mode_fire2 == k::kModeFire2 &&
              c::gun_mode_reload == k::kModeReload && c::gun_mode_reload2 == k::kModeReload2,
          "the gun modes match the contract");
    // Flag n is word n >> 5, bit n & 31.
    Check((0x18 >> 5) == 0 && (1u << (0x18 & 31)) == k::kW0Armed, "flag 0x18 is word 0 bit 24");
    Check((0xC2 >> 5) == 6 && (1u << (0xC2 & 31)) == k::kW6NoWeaponB && (1u << (0xC3 & 31)) == k::kW6NoWeapon &&
              (1u << (0xCF & 31)) == k::kW6NoHold && (1u << (0xD8 & 31)) == k::kW6Slow && k::kFlagWord6 == 6u * 4u,
          "flags 0xC2 / 0xC3 / 0xCF / 0xD8 are word 6 bits 2 / 3 / 15 / 24");
    bool listed = false;
    for (const std::uintptr_t rva : c::seat_aim_blocks) listed = listed || rva == 0x1250FC4;
    Check(listed && sizeof(c::seat_aim_blocks) / sizeof(c::seat_aim_blocks[0]) == 16, "the contract lists the 16 seat aim blocks");
}

void TestReaders() {
    Fake m;
    const auto read = Reader(m);
    // PS: records at +0x280, base id at +8; id 1 is record 1.
    m.Put<std::uintptr_t>(kPs + 0x280, kRecords);
    m.Put<std::uint32_t>(kPs + 0x8, 0);
    m.Put<std::uint32_t>(kRecords + 0x20 + 0x0, 0x1000000u);
    m.Put<std::uint32_t>(kRecords + 0x20 + 0x18, 0x8u | 0x1000000u);
    m.Put<std::uint32_t>(kRecords + 0x0, 0u);
    m.Put<std::uint32_t>(kRecords + 0x18, 0x8000u);
    const sa::Flags f1 = sa::ReadFlags(read, kPs, 1);
    Check(f1.ok && f1.w0 == 0x1000000u && f1.w6 == 0x1000008u, "id 1 reads record 1 (base 0)");
    const sa::Flags f0 = sa::ReadFlags(read, kPs, 0);
    Check(f0.ok && f0.w0 == 0 && f0.w6 == 0x8000u, "id 0 reads record 0");
    m.Put<std::uint32_t>(kPs + 0x8, 1);
    Check(!sa::ReadFlags(read, kPs, 0).ok, "an id below the PS base id is refused");
    const sa::Flags rebased = sa::ReadFlags(read, kPs, 1);
    Check(rebased.ok && rebased.w0 == 0 && rebased.w6 == 0x8000u, "base 1: id 1 is record 0");
    Check(!sa::ReadFlags(read, kPs, 65).ok, "an index past the bound is refused");
    Check(!sa::ReadFlags(read, 0, 1).ok, "a null PS is refused");
    m.Put<std::uint32_t>(kPs + 0x8, 0);
    m.Erase(kRecords + 0x20 + 0x18, 4);
    const sa::Flags torn = sa::ReadFlags(read, kPs, 1);
    Check(!torn.ok && torn.w0 == 0 && torn.w6 == 0, "an unreadable word 6 leaves nothing half-read");
    m.Put<std::uintptr_t>(kPs + 0x280, 0);
    Check(!sa::ReadFlags(read, kPs, 1).ok, "a null record array is refused");

    // Q arrays: u16 acts, u8 uppers.
    m.Put<std::uintptr_t>(kQ + 0x18, kActs);
    m.Put<std::uintptr_t>(kQ + 0x20, kUppers);
    m.Put<std::uint16_t>(kActs + 0, 0x6E);
    m.Put<std::uint16_t>(kActs + 2, 0x3);
    m.Put<std::uint8_t>(kUppers + 0, 2);
    m.Put<std::uint8_t>(kUppers + 1, 0);
    std::uint16_t act = 0;
    std::uint8_t upper = 0xFF;
    Check(sa::ReadActs(read, kQ, 0, act, upper) && act == 0x6E && upper == 2, "id 0's act and upper act");
    Check(sa::ReadActs(read, kQ, 1, act, upper) && act == 0x3 && upper == 0, "id 1's act and upper act");
    act = 7;
    Check(!sa::ReadActs(read, kQ, 2, act, upper) && act == 7 && upper == 0, "an unreadable id leaves the outputs");
    Check(!sa::ReadActs(read, 0, 0, act, upper), "a null Q is refused");

    // The gun record's next state and posture.
    m.Put<std::uint8_t>(kGunRec + 0x2C1, 6);
    m.Put<std::uint8_t>(kGunRec + 0x3D4, 0);
    std::uint8_t next = 0xFF, posture = 0xFF;
    Check(sa::ReadGunExtra(read, kGunRec, next, posture) && next == 6 && posture == 0, "rec+0x2C1 and rec+0x3D4");
    m.Erase(kGunRec + 0x3D4, 1);
    next = posture = 0xEE;
    Check(!sa::ReadGunExtra(read, kGunRec, next, posture) && next == 0xEE && posture == 0xEE, "a torn gun read writes nothing");
    Check(!sa::ReadGunExtra(read, 0, next, posture), "a null gun record is refused");
}

void TestWatchAndClasses() {
    Check(sa::RideAct(0x6E) && sa::RideAct(0x6F) && sa::RideAct(0x70) && !sa::RideAct(0x6D) && !sa::RideAct(0x71) && !sa::RideAct(99),
          "the ride acts are 0x6E..0x70");
    Check(sa::WatchLocal(true, 0x6E, false) && sa::WatchLocal(true, 0x6F, false) && !sa::WatchLocal(true, 1, false),
          "the local player is watched in a ride act");
    Check(sa::WatchLocal(false, 0, true) && sa::WatchLocal(true, 1, true), "the passenger report alone watches");
    Check(!sa::WatchLocal(false, 0x6E, false), "an unread act is not a ride act");
    Check(sa::WatchBody1(true, false) && sa::WatchBody1(false, true) && !sa::WatchBody1(false, false), "body 1 in the seat flow");
    Check(std::strcmp(sa::WhoName(0), "local") == 0 && std::strcmp(sa::WhoName(1), "body1") == 0, "who names");

    sa::Sample s{};
    s.ok = true;
    s.act = 0x6E;
    s.upper = 0;
    Check(!sa::Armed(s) && !sa::Aiming(s) && !sa::Firing(s) && !sa::Reloading(s), "an unread gun / flag sample is nothing");
    s.flags_ok = true;
    s.w0 = 0x1000000u;
    s.w6 = 0x8u;
    Check(sa::Armed(s) && sa::Bit(s, k::kW6NoWeapon) && !sa::Bit(s, k::kW6NoWeaponB), "armed and flag 0xC3");
    s.flags_ok = false;
    Check(!sa::Armed(s) && !sa::Bit(s, k::kW6NoWeapon), "unread flags are never set");
    s.upper = 2;
    Check(sa::Aiming(s), "the gun-hold upper act is aiming");
    s.upper = 0;
    s.gun_ok = true;
    s.mode = 7;
    Check(sa::Aiming(s) && !sa::Firing(s), "gun mode 7 is aiming");
    s.mode = 9;
    Check(sa::Firing(s) && !sa::Aiming(s), "gun mode 9 fires");
    s.mode = 10;
    Check(sa::Firing(s), "gun mode 10 fires");
    s.mode = 11;
    Check(sa::Reloading(s) && !sa::Firing(s), "gun mode 11 reloads");
    s.mode = 12;
    Check(sa::Reloading(s), "gun mode 12 reloads");
}

sa::Sample Seated(std::uint8_t mode, std::uint8_t upper, bool armed, bool c3) {
    sa::Sample s{};
    s.ok = s.gun_ok = s.flags_ok = true;
    s.act = 0x6E;
    s.upper = upper;
    s.mode = mode;
    s.next = 0;
    s.posture = 0;
    s.cls = 1;
    s.equip = 2;
    s.hand = 0;
    s.w0 = armed ? 0x1000000u : 0;
    s.w6 = c3 ? 0x8u : 0;
    s.seat = 2;
    s.vehicle = 25608;
    return s;
}

void TestSameKey() {
    const sa::Sample a = Seated(0, 0, true, false);
    sa::Sample b = a;
    Check(sa::SameKey(a, b), "a copy is the same key");
    b.w6 |= 0x2u;  // an unreported bit
    Check(sa::SameKey(a, b), "an unreported flag bit is not a change");
    b = a;
    b.w6 |= 0x8u;
    Check(!sa::SameKey(a, b), "flag 0xC3 is a change");
    b = a;
    b.w0 = 0;
    Check(!sa::SameKey(a, b), "armed is a change");
    b = a;
    b.upper = 2;
    Check(!sa::SameKey(a, b), "the upper act is a change");
    b = a;
    b.mode = 7;
    Check(!sa::SameKey(a, b), "the gun mode is a change");
    b = a;
    b.next = 6;
    Check(sa::SameKey(a, b), "the requested next state alone is not a change");
    b = a;
    b.cam_ok = true;
    Check(!sa::SameKey(a, b), "the camera read is a change");
    sa::Sample cam1 = a, cam2 = a;
    cam1.cam_ok = cam2.cam_ok = true;
    cam1.cam_handle = 0x8800000000006408ull;
    cam2.cam_handle = 0x8800000000006409ull;
    Check(sa::SameKey(cam1, cam2), "only whether a camera handle is held is compared");
    cam2.cam_handle = 0;
    Check(!sa::SameKey(cam1, cam2), "a camera handle that went away is a change");
    b = a;
    b.seat = 3;
    Check(!sa::SameKey(a, b), "the seat is a change");
}

void TestEpisode() {
    sa::Episode e{};
    sa::Step(e, Seated(0, 0, true, false), 100);
    Check(e.samples == 0, "an inactive episode does not step");
    sa::Begin(e, 1000);
    sa::Sample unread{};
    sa::Step(e, unread, 1050);
    Check(e.samples == 0 && e.last_ms == 1000, "an unread sample does not step");
    sa::Step(e, Seated(0, 0, false, true), 1100);  // GetOn: not armed yet
    Check(e.samples == 1 && e.first_act == 0x6E && e.armed_ms == 0 && e.aims == 0 && e.seat == 2 && e.vehicle == 25608,
          "the first sample sets the first act");
    sa::Step(e, Seated(0, 0, true, false), 1200);  // loop: armed
    Check(e.armed_ms == 0, "time counts from the step after a sample was armed");
    sa::Step(e, Seated(7, 2, true, false), 1300);  // aim
    Check(e.armed_ms == 100 && e.aims == 1 && e.aim_posture == 0, "armed 100 ms, then an aim");
    sa::Step(e, Seated(9, 2, true, false), 1400);  // fire (upper still 2: still aiming)
    Check(e.aim_ms == 100 && e.fires == 1 && e.aims == 1 && e.armed_ms == 200, "a fire while aiming is no new aim");
    sa::Step(e, Seated(7, 2, true, false), 1500);
    sa::Step(e, Seated(9, 2, true, false), 1600);
    Check(e.fires == 2 && e.aim_ms == 300, "a second fire");
    sa::Step(e, Seated(11, 0, true, false), 1700);
    Check(e.reloads == 1 && e.aim_ms == 400, "a reload ends the aim");
    sa::Step(e, Seated(0, 0, true, false), 5000);
    Check(e.armed_ms == 500 + k::kMaxStepMs && e.aim_ms == 400, "a stalled step adds at most kMaxStepMs");
    sa::Step(e, Seated(7, 2, true, false), 5100);
    Check(e.aims == 2, "a new aim after the reload");
    sa::Step(e, Seated(7, 2, true, false), 4000);
    Check(e.last_ms == 4000 && e.aim_ms == 400, "a clock going back adds nothing");
}

void TestTickLines() {
    sa::Track t{};
    sa::Budgets b{};
    std::vector<std::string> lines;
    const auto emit = [&lines](const char* line) noexcept { lines.emplace_back(line); };
    // Not watched, nothing open: nothing.
    sa::Tick(t, 0, false, sa::Sample{}, 1000, b, emit);
    Check(lines.empty() && !t.episode.active, "no watch, no line");
    // Watched but unread: the episode opens, no line.
    sa::Tick(t, 0, true, sa::Sample{}, 1100, b, emit);
    Check(lines.empty() && t.episode.active && !t.have, "an unread watched sample opens the episode silently");
    // The first read sample is a line.
    sa::Sample s = Seated(0, 0, false, true);
    s.cam_ok = true;
    s.cam_mode = 5;
    s.cam_handle = 0x8800000000006408ull;
    sa::Tick(t, 0, true, s, 1200, b, emit);
    Check(lines.size() == 1 &&
              lines[0] ==
                  "partner_seat_aim=sample; who=local; k=0; act=0x6e; upper=0x00; mode=0; next=0; posture=0; cls=1; equip=2; hand=0; "
                  "armed=0; c2=0; c3=1; cf=0; d8=0; seat=2; vehicle=25608; cam_mode=5; cam_handle=0x8800000000006408\r\n",
          "the first sample line");
    sa::Tick(t, 0, true, s, 1300, b, emit);
    Check(lines.size() == 1, "an unchanged sample is no line");
    s.w6 = 0;
    s.w0 = 0x1000000u;
    sa::Tick(t, 0, true, s, 1400, b, emit);
    Check(lines.size() == 2 && Has(lines[1], "armed=1; c2=0; c3=0;"), "the weapon enabled after GetOn is a line");
    // Per-second budget: more than ten changes inside one second retry against the same reference.
    for (unsigned i = 0; i < 12; ++i) {
        s.equip = static_cast<std::uint16_t>(100 + i);
        sa::Tick(t, 0, true, s, 3000 + i, b, emit);
    }
    Check(lines.size() == 2 + 10 && t.last.equip == 109, "ten lines a second, the rest retried");
    sa::Tick(t, 0, true, s, 4001, b, emit);
    Check(lines.size() == 13 && t.last.equip == 111, "the retried change logs in the next second");
    // The end line when the watch ends.
    sa::Tick(t, 0, false, sa::Sample{}, 5000, b, emit);
    Check(lines.size() == 14 && Has(lines[13], "partner_seat_aim=end; who=local; k=0; reason=watch; ms=3900; samples=") &&
              Has(lines[13], "aims=0; fires=0; reloads=0; posture=-1; first_act=0x6e; seat=2; vehicle=25608\r\n") &&
              !t.episode.active && !t.have,
          "the end line closes the episode");
    // A new episode logs its first sample again, and body 1's lines name it.
    sa::Sample one = Seated(0, 0, true, false);
    one.act = 0x2A;
    sa::Tick(t, 1, true, one, 6000, b, emit);
    Check(lines.size() == 15 && Has(lines[14], "partner_seat_aim=sample; who=body1; k=1; act=0x2a;") && !Has(lines[14], "cam_mode"),
          "body 1's sample has no camera group");
    Check(sa::Finish(t, 1, 7000, "mission", b, emit) && Has(lines.back(), "who=body1; k=1; reason=mission; ms=1000;"), "Finish");
    Check(!sa::Finish(t, 1, 7000, "mission", b, emit), "Finish on a closed track is a no-op");
}

void TestCaps() {
    sa::Track t{};
    sa::Budgets b{};
    std::vector<std::string> lines;
    const auto emit = [&lines](const char* line) noexcept { lines.emplace_back(line); };
    sa::Sample s = Seated(0, 0, true, false);
    std::uint64_t now = 10000;
    for (unsigned i = 0; i < k::kSampleCap + 5; ++i) {
        s.equip = static_cast<std::uint16_t>(i & 1 ? 1 : 2);
        sa::Tick(t, 0, true, s, now, b, emit);
        now += 200;  // five a second: never the per-second limit
    }
    unsigned samples = 0, capped = 0;
    for (const auto& line : lines) {
        if (Has(line, "partner_seat_aim=sample;")) ++samples;
        if (line == "partner_seat_aim=capped; kind=sample; limit=1200\r\n") ++capped;
    }
    Check(samples == k::kSampleCap && capped == 1, "the mission cap, then one capped line");
    Check(t.last.equip == s.equip, "after the cap the reference still moves");
    // The other budget: end lines are capped separately.
    sa::Budgets ob{};
    ob.other.total = k::kOtherCap;
    sa::Track open{};
    sa::Begin(open.episode, 1);
    lines.clear();
    Check(!sa::Finish(open, 0, 2, "watch", ob, emit) && lines.size() == 1 && lines[0] == "partner_seat_aim=capped; kind=other; limit=400\r\n" &&
              !open.episode.active,
          "an end line over the cap says capped once and still closes");
    sa::Begin(open.episode, 3);
    lines.clear();
    sa::Finish(open, 0, 4, "watch", ob, emit);
    Check(lines.empty(), "capped is said once");
}

void TestFormats() {
    char line[400]{};
    Check(sa::FormatOn(line, sizeof(line), 7) && std::string(line) == "partner_seat_aim=on; mission=7; read_only=1; writes=0; calls=0\r\n",
          "the on line");
    sa::Sample s{};
    s.ok = true;
    s.act = 0x6F;
    s.upper = 0;
    Check(sa::FormatSample(line, sizeof(line), 0, s) &&
              std::string(line) ==
                  "partner_seat_aim=sample; who=local; k=0; act=0x6f; upper=0x00; mode=-1; next=-1; posture=-1; cls=-1; equip=-1; "
                  "hand=-1; armed=na; c2=na; c3=na; cf=na; d8=na; seat=-1; vehicle=-1\r\n",
          "unread groups");
    s = Seated(7, 2, true, false);
    s.w6 = 0x4u | 0x8000u | 0x1000000u;
    Check(sa::FormatSample(line, sizeof(line), 1, s) && Has(line, "who=body1; k=1; act=0x6e; upper=0x02; mode=7;") &&
              Has(line, "armed=1; c2=1; c3=0; cf=1; d8=1; seat=2; vehicle=25608\r\n"),
          "the flag bits");
    Check(!sa::FormatSample(line, 40, 0, s), "a short buffer refuses");
    Check(!sa::FormatSample(nullptr, 40, 0, s) && !sa::FormatOn(nullptr, 10, 1), "a null buffer refuses");
    sa::Episode e{};
    sa::Begin(e, 100);
    Check(sa::FormatEnd(line, sizeof(line), 0, e, 50, nullptr) && Has(line, "reason=watch; ms=0; samples=0;") &&
              Has(line, "posture=-1; first_act=0xffff; seat=-1; vehicle=-1\r\n"),
          "an empty episode, a clock before its start");
    sa::Track local{}, body1{};
    Check(sa::FormatStatus(line, sizeof(line), local, body1) &&
              std::string(line) ==
                  "partner_seat_aim=status; local=0; body1=0; k0_act=0xffff; k0_upper=0xff; k0_mode=-1; k0_armed=-1; k0_c3=-1; "
                  "k1_act=0xffff; k1_upper=0xff; k1_mode=-1; k1_armed=-1; k1_c3=-1\r\n",
          "an empty status line");
    local.have = true;
    local.last = Seated(7, 2, true, true);
    sa::Begin(local.episode, 1);
    Check(sa::FormatStatus(line, sizeof(line), local, body1) &&
              Has(line, "local=1; body1=0; k0_act=0x6e; k0_upper=0x02; k0_mode=7; k0_armed=1; k0_c3=1; k1_act=0xffff;"),
          "the status line");
}
} // namespace

int main() {
    TestContract();
    TestReaders();
    TestWatchAndClasses();
    TestSameKey();
    TestEpisode();
    TestTickLines();
    TestCaps();
    TestFormats();
    std::printf("partner_seat_aim_tests: %u checks passed\n", checks);
    return 0;
}
