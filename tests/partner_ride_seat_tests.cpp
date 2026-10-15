// The seated vehicle rider's patch module (native/partner_ride_seat.cpp,
// docs/anim-round1-seat.md) on a fixture image: the four dwords, the
// masked-block verification (an armed image re-validates), seat rewrites,
// idempotent revert and every refusal path (bad block byte, foreign site
// dword, short span, bad seat). The seated state table. When the
// fingerprinted executable is installed, the same paths run on an in-memory
// copy of its .xtext (read-only file access). Nothing is executed from the
// buffers; no MGSV process is involved.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "mgscoop/partner_ride_seat.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace prs = mgscoop::partner_ride_seat;
namespace c = mgscoop::partner_player_contract;
unsigned checks = 0;
std::string logged;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %u: %s\n%s", checks, message, logged.c_str()); std::exit(1); }
}
void Log(const char* line) noexcept {
    try { logged += line; } catch (...) {}
}
bool Logged(const char* text) { return logged.find(text) != std::string::npos; }
std::uint32_t Dword(const unsigned char* base, std::uintptr_t rva) {
    std::uint32_t v = 0;
    std::memcpy(&v, base + rva, 4);
    return v;
}
void SetDword(unsigned char* base, std::uintptr_t rva, std::uint32_t v) { std::memcpy(base + rva, &v, 4); }

constexpr std::size_t kSpan = 0x1200000;

unsigned char* FixtureImage() {
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, kSpan, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != nullptr, "fixture allocation");
    std::memset(image, 0xCC, kSpan);
    for (const auto& site : c::code_sites)
        if (site.rva + site.size <= kSpan) std::memcpy(image + site.rva, site.bytes, site.size);
    return image;
}

bool AllOriginal(const unsigned char* image) {
    for (unsigned i = 0; i < prs::kSites; ++i)
        if (prs::StateOf(image, static_cast<prs::SiteId>(i)) != prs::SiteState::original) return false;
    return true;
}

void Sha256(const unsigned char* bytes, std::size_t size, unsigned char (&out)[32]) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash, const_cast<PUCHAR>(bytes), static_cast<ULONG>(size), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, out, sizeof(out), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    Check(ok, "test SHA-256");
}

void TestTable() {
    using prs::SeatClass;
    const auto* lv0 = prs::StatesFor(SeatClass::light, 0);
    Check(lv0 && std::strcmp(lv0->loop, "stateVehicleFrontLeftLoop") == 0 && lv0->loop_code == 0x489157d7d94full &&
              std::strcmp(lv0->get_on, "stateVehicleGetOnFrontLeft") == 0 && std::strcmp(lv0->get_off, "stateVehicleGetOffFrontLeft") == 0,
          "light seat 0 is the driver (front left)");
    Check(prs::StatesFor(SeatClass::light, 3) && std::strcmp(prs::StatesFor(SeatClass::light, 3)->loop, "stateVehicleBackRightLoop") == 0,
          "light seat 3 rear right");
    Check(!prs::StatesFor(SeatClass::light, 4), "no light seat 4");
    const auto* trc0 = prs::StatesFor(SeatClass::truck, 0);
    const auto* trc1 = prs::StatesFor(SeatClass::truck, 1);
    Check(trc0 && std::strcmp(trc0->loop, "stateTruckFrontLeftLoop") == 0 && trc0->loop_code == 0x9df6a2daa633ull,
          "truck driver state");
    Check(trc1 && std::strcmp(trc1->loop, "stateVehicleFrontRightLoop") == 0 && std::strcmp(trc1->get_on, "stateTruckGetOnFrontRight") == 0,
          "truck seat 1 guess: the light front-right loop, the truck get-on");
    Check(!prs::StatesFor(SeatClass::truck, 2) && !prs::StatesFor(SeatClass::truck, 3), "trucks have no rear seats");
    for (unsigned seat = 0; seat < 4; ++seat) {
        const auto* s = prs::StatesFor(SeatClass::light, static_cast<std::uint8_t>(seat));
        Check(prs::IsSeatState(s->loop_code, static_cast<std::uint8_t>(seat)) && prs::IsSeatState(s->get_on_code, static_cast<std::uint8_t>(seat)) &&
                  prs::IsSeatState(s->get_off_code, static_cast<std::uint8_t>(seat)), "every light seat state confirms its own seat");
        Check(!prs::IsSeatState(s->loop_code, static_cast<std::uint8_t>((seat + 2) % 4)), "another seat's loop does not confirm");
    }
    Check(prs::IsSeatState(0xABCD000000000000ull | trc0->loop_code, 0), "only the low 48 bits are compared");
    Check(prs::IsSeatState(trc1->get_off_code, 1) && prs::IsSeatState(0x4470e2406e9dull, 1), "truck seat 1 codes confirm seat 1");
    Check(!prs::IsSeatState(prs::kForbiddenRearSeatState, 2) && !prs::IsSeatState(0, 0) && !prs::IsSeatState(0x81582ffa24b9ull, 0),
          "the s10150 rear-seat state, zero and stateDirectPlay never confirm");
    Check(prs::SeatDword(0) == 0x00b04130u && prs::SeatDword(2) == c::ride_seat_old && prs::SeatDword(3) == 0x03b04130u,
          "seat dword: 30 41 b0 <seat>");
}

void TestFixture() {
    unsigned char* image = FixtureImage();
    Check(prs::VerifyImage(image, kSpan) == nullptr && AllOriginal(image) && !prs::Armed() && prs::ArmedSeat() == 0xFF,
          "fixture image verifies and is original");
    Check(std::strcmp(prs::VerifyImage(image, 0x1199000) ? prs::VerifyImage(image, 0x1199000) : "", "span") == 0, "short span refused");
    Check(prs::VerifyImage(nullptr, kSpan) != nullptr, "null base refused");
    // Arm for the driver seat.
    logged.clear();
    Check(prs::Arm(image, kSpan, 0, Log) && prs::Armed() && prs::ArmedSeat() == 0, "arm seat 0");
    Check(Dword(image, c::ride_seat_rva) == 0x00b04130u && Dword(image, c::ride_cam_play_rva) == c::ride_cam_play_new &&
              Dword(image, c::ride_cam_plain_rva) == c::ride_cam_plain_new && Dword(image, c::ride_cam_end_rva) == c::ride_cam_end_new,
          "four dwords written");
    Check(image[c::ride_cam_play_rva] == 0xEB && image[c::ride_cam_play_rva + 1] == 0x64 && image[c::ride_cam_plain_rva + 1] == 0x39 &&
              image[c::ride_cam_end_rva + 1] == 0x0C && image[c::ride_seat_rva + 3] == 0x00,
          "jmp rel8 64/39/0c and seat immediate 0");
    Check(Logged("partner_ride_seat_patch=applied; seat=0;"), "applied line");
    Check(prs::VerifyImage(image, kSpan) == nullptr, "an armed image re-validates (site dwords masked)");
    for (unsigned i = 0; i < prs::kSites; ++i)
        Check(prs::StateOf(image, static_cast<prs::SiteId>(i)) == prs::SiteState::patched, "every site reads patched");
    // A seat change rewrites the seat dword only.
    Check(prs::Arm(image, kSpan, 3, Log) && prs::ArmedSeat() == 3 && Dword(image, c::ride_seat_rva) == 0x03b04130u &&
              Dword(image, c::ride_cam_end_rva) == c::ride_cam_end_new, "re-arm seat 3");
    // Revert, then idempotent.
    logged.clear();
    prs::Revert(image, kSpan, Log, "test");
    Check(AllOriginal(image) && !prs::Armed() && prs::ArmedSeat() == 0xFF && Logged("partner_ride_seat_patch=reverted; reason=test"),
          "revert restores all four");
    logged.clear();
    prs::Revert(image, kSpan, Log, "again");
    Check(AllOriginal(image) && logged.empty(), "a second revert is silent");
    // The retail seat (2) needs no seat write.
    Check(prs::Arm(image, kSpan, 2, Log) && Dword(image, c::ride_seat_rva) == c::ride_seat_old && prs::ArmedSeat() == 2 &&
              Dword(image, c::ride_cam_play_rva) == c::ride_cam_play_new, "seat 2 keeps the retail immediate");
    prs::Revert(image, kSpan, Log, "seat2");
    Check(AllOriginal(image) && !prs::Armed(), "seat 2 reverted");
    // Refusals write nothing.
    logged.clear();
    Check(!prs::Arm(image, kSpan, 4, Log) && Logged("reason=args") && AllOriginal(image), "seat 4 refused");
    Check(!prs::Arm(nullptr, kSpan, 0, Log), "null base refused");
    for (const std::uintptr_t block : c::ride_seat_blocks) {
        // A byte outside the four dwords: the first byte of each block (none starts on a site).
        const unsigned char prior = image[block];
        image[block] = static_cast<unsigned char>(prior ^ 0x5A);
        logged.clear();
        char want[48]{};
        std::snprintf(want, sizeof(want), "reason=block_0x%llx", static_cast<unsigned long long>(block));
        Check(!prs::Arm(image, kSpan, 0, Log) && Logged(want) && AllOriginal(image) && !prs::Armed(), "a changed block refuses and writes nothing");
        image[block] = prior;
    }
    // The jump targets themselves are pinned (epilogue 0x1198DBC, End continuation 0x11992F7).
    for (const std::uintptr_t target : {std::uintptr_t{0x1198dbc}, std::uintptr_t{0x11992f7}}) {
        const unsigned char prior = image[target];
        image[target] = 0x90;
        Check(!prs::Arm(image, kSpan, 0, Log) && AllOriginal(image), "a changed jump target refuses");
        image[target] = prior;
    }
    const struct { std::uintptr_t rva; const char* reason; } foreign[] = {
        {c::ride_seat_rva, "reason=site_seat"}, {c::ride_cam_play_rva, "reason=site_cam_play"},
        {c::ride_cam_plain_rva, "reason=site_cam_plain"}, {c::ride_cam_end_rva, "reason=site_cam_end"}};
    for (const auto& f : foreign) {
        const std::uint32_t prior = Dword(image, f.rva);
        SetDword(image, f.rva, 0x90909090u);
        logged.clear();
        Check(!prs::Arm(image, kSpan, 1, Log) && Logged(f.reason) && Dword(image, f.rva) == 0x90909090u, "a foreign site dword refuses");
        // Revert never touches a dword that is not ours.
        prs::Revert(image, kSpan, Log, "foreign");
        Check(Dword(image, f.rva) == 0x90909090u && Logged("partner_ride_seat_patch=refused; reason=revert_site_"),
              "revert leaves a foreign dword and refuses");
        SetDword(image, f.rva, prior);
    }
    // A seat byte above 3 in our pattern is foreign too.
    SetDword(image, c::ride_seat_rva, 0x07b04130u);
    Check(prs::StateOf(image, prs::SiteId::seat) == prs::SiteState::other && !prs::Arm(image, kSpan, 0, Log), "seat byte 7 is foreign");
    SetDword(image, c::ride_seat_rva, c::ride_seat_old);
    // A page made read-only (code pages are execute-read) is still patched and its protection restored.
    DWORD old = 0;
    Check(VirtualProtect(image + 0x1198000, 0x2000, PAGE_READONLY, &old) != 0, "fixture protect");
    Check(prs::Arm(image, kSpan, 1, Log) && Dword(image, c::ride_seat_rva) == 0x01b04130u, "arm on read-only pages");
    MEMORY_BASIC_INFORMATION info{};
    Check(VirtualQuery(image + 0x1198d56, &info, sizeof(info)) && info.Protect == PAGE_READONLY, "protection restored");
    prs::Revert(image, kSpan, Log, "readonly");
    Check(AllOriginal(image) && !prs::Armed(), "revert on read-only pages");
    VirtualFree(image, 0, MEM_RELEASE);
}

// The installed, fingerprinted executable (read-only): map .xtext at its rva.
void TestRetailCopy() {
    const wchar_t* path = L"D:\\Games\\steamapps\\common\\MGS_TPP\\mgsvtpp.exe";
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { std::printf("retail copy: executable not installed; skipped\n"); return; }
    LARGE_INTEGER size{};
    std::vector<unsigned char> data;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (1ll << 30)) {
        data.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) || read != data.size()) data.clear();
    }
    CloseHandle(file);
    if (data.empty()) { std::printf("retail copy: unreadable; skipped\n"); return; }
    unsigned char hash[32]{};
    Sha256(data.data(), data.size(), hash);
    static constexpr unsigned char expected[32] = {0x08, 0x5c, 0x2f, 0x82, 0xd1, 0xc9, 0x63, 0xc4, 0x0b, 0x3d, 0x2d, 0x55, 0x78, 0x66, 0x61, 0xdf,
                                                   0xee, 0x2b, 0x18, 0xcb, 0xbf, 0x38, 0x8a, 0x71, 0x0c, 0x00, 0xfa, 0x76, 0xc5, 0xe9, 0xbb, 0x45};
    if (std::memcmp(hash, expected, sizeof(hash)) != 0) { std::printf("retail copy: different executable; skipped\n"); return; }
    // .xtext: rva 0x1000, raw 0x400 (tools/generate_partner_contract.py reads the same sections).
    const std::uint32_t e = *reinterpret_cast<const std::uint32_t*>(&data[0x3c]);
    const std::uint16_t sections = *reinterpret_cast<const std::uint16_t*>(&data[e + 6]);
    const std::uint16_t optional = *reinterpret_cast<const std::uint16_t*>(&data[e + 20]);
    unsigned char* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, kSpan, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != nullptr, "retail copy allocation");
    for (std::uint16_t i = 0; i < sections; ++i) {
        const std::size_t o = e + 24 + optional + i * 40u;
        std::uint32_t vsize = 0, va = 0, rsize = 0, raw = 0;
        std::memcpy(&vsize, &data[o + 8], 4); std::memcpy(&va, &data[o + 12], 4);
        std::memcpy(&rsize, &data[o + 16], 4); std::memcpy(&raw, &data[o + 20], 4);
        if (va >= kSpan) continue;
        const std::size_t n = std::min<std::size_t>(rsize, kSpan - va);
        if (raw + n <= data.size()) std::memcpy(image + va, &data[raw], n);
    }
    Check(prs::VerifyImage(image, kSpan) == nullptr && AllOriginal(image), "the retail image matches every pinned block");
    Check(prs::Arm(image, kSpan, 1, Log) && Dword(image, c::ride_seat_rva) == 0x01b04130u, "arm on the retail copy");
    prs::Revert(image, kSpan, Log, "retail");
    Check(AllOriginal(image), "revert on the retail copy");
    VirtualFree(image, 0, MEM_RELEASE);
    std::printf("retail copy: verified\n");
}
} // namespace

int main() {
    TestTable();
    TestFixture();
    TestRetailCopy();
    std::printf("partner_ride_seat_tests: %u checks passed\n", checks);
    return 0;
}
