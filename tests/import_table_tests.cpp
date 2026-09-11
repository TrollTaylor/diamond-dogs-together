#include "mgscoop/import_table.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace mgscoop;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error("Failed at line " + std::to_string(__LINE__)); } while(false)
template<class T> void put(std::vector<std::uint8_t>& bytes, std::size_t at, T value) { std::memcpy(bytes.data() + at, &value, sizeof(value)); }
static std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> p(4096);
    put<std::uint16_t>(p, 0, 0x5a4d); put<std::uint32_t>(p, 0x3c, 0x80);
    put<std::uint32_t>(p, 0x80, 0x4550); put<std::uint16_t>(p, 0x84, 0x8664);
    put<std::uint16_t>(p, 0x94, 240); put<std::uint16_t>(p, 0x98, 0x20b);
    put<std::uint32_t>(p, 0x98 + 56, 4096); put<std::uint32_t>(p, 0x98 + 108, 16);
    put<std::uint32_t>(p, 0x98 + 120, 512); put<std::uint32_t>(p, 0x98 + 124, 40);
    put<std::uint32_t>(p, 512, 768); put<std::uint32_t>(p, 524, 640); put<std::uint32_t>(p, 528, 800);
    std::memcpy(p.data() + 640, "steam_api64.dll", 16);
    put<std::uint64_t>(p, 768, 900); put<std::uint64_t>(p, 800, 0x7fff00001234);
    std::memcpy(p.data() + 902, "SteamAPI_RunCallbacks", 22);
    return p;
}
static ImportSlot find(const std::vector<std::uint8_t>& p) { return find_import_slot(p.data(), p.size(), "steam_api64.dll", "SteamAPI_RunCallbacks"); }
static bool deny_byte(const void* context, std::size_t offset, std::size_t count) noexcept {
    const auto denied = *static_cast<const std::size_t*>(context);
    return denied < offset || denied - offset >= count;
}
int main() {
    try {
        auto p = fixture(); auto r = find(p); CHECK(r.status == ImportStatus::found && r.offset == 800);
        for (std::size_t denied : {std::size_t(0),std::size_t(128),std::size_t(160),std::size_t(512),std::size_t(640),std::size_t(768),std::size_t(800),std::size_t(902),std::size_t(919)})
            CHECK(find_import_slot(p.data(), p.size(), "steam_api64.dll", "SteamAPI_RunCallbacks", deny_byte, &denied).status == ImportStatus::invalid_image);
        CHECK(find_import_slot(p.data(), p.size(), "STEAM_API64.DLL", "SteamAPI_RunCallbacks").status == ImportStatus::found);
        CHECK(find_import_slot(p.data(), p.size(), "steam_api64.dll", "SteamAPI_Unknown").status == ImportStatus::not_found);
        CHECK(find_import_slot(nullptr, 0, "a", "b").status == ImportStatus::invalid_argument);
        for (std::size_t n = 0; n < p.size(); ++n) CHECK(find_import_slot(p.data(), n, "steam_api64.dll", "SteamAPI_RunCallbacks").status == ImportStatus::invalid_image);
        for (const auto at : {0x3cu, 0x98u + 56u, 0x98u + 120u, 0x98u + 124u, 512u, 524u, 528u}) {
            auto bad = p; put<std::uint32_t>(bad, at, 0xfffffff0u); CHECK(find(bad).status == ImportStatus::invalid_image);
        }
        auto bad = p; put<std::uint32_t>(bad, 528, 801); CHECK(find(bad).status == ImportStatus::invalid_image);
        bad = p; put<std::uint64_t>(bad, 776, 900); CHECK(find(bad).status == ImportStatus::ambiguous);
        bad = p; put<std::uint64_t>(bad, 768, 0x8000000000000001ull); CHECK(find(bad).status == ImportStatus::not_found);
        bad = p; put<std::uint32_t>(bad, 512, 0); CHECK(find(bad).status == ImportStatus::invalid_image);
        bad = p; put<std::uint32_t>(bad, 0x98 + 124, 20); CHECK(find(bad).status == ImportStatus::invalid_image);
        bad = p; std::memset(bad.data() + 902, 'x', bad.size() - 902); CHECK(find(bad).status == ImportStatus::invalid_image);
        std::cout << checks << " mapped import parser checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
