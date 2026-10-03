// Checks exact-source gating of DamageParameterTables.lua with a local extracted
// script; no engine calls. Writes the composed source for the Lua 5.1 check.
#include <atomic>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#include "mgscoop/bootstrap_patch.hpp"

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: damage_parameters_patch_check DamageParameterTables.lua composed.lua\n"; return 2; }
    std::ifstream input(argv[1], std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    if (!input || source.empty()) return 2;
    constexpr auto name = "@/Assets/tpp/level_asset/damage/ParameterTables/DamageParameterTables.lua";
    const std::string prefix = "this={}TppDamage.ReloadDamageParameter";
    const std::string replaced_prefix = "this={} local mgsDamage=";
    const std::string tail = "return this";
    mgscoop::script_bridge::Replacement patch{};
    unsigned checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) throw checks; };
    try {
        check(source.size() == 82834);
        check(!mgscoop::SelectDamageParameterPatch(source.data(), source.size(), name, patch, false));
        check(!patch.bytes && !patch.size);
        check(!mgscoop::SelectDamageParameterPatch(nullptr, source.size(), name, patch, true));
        check(!mgscoop::SelectDamageParameterPatch(source.data(), source.size(), nullptr, patch, true));
        check(!mgscoop::SelectDamageParameterPatch(source.data(), source.size(), "@DamageParameterTables.lua", patch, true));
        check(!mgscoop::SelectDamageParameterPatch(source.data(), source.size()-1, name, patch, true));
        auto changed = source; changed[source.size()/2] ^= 1;
        check(!mgscoop::SelectDamageParameterPatch(changed.data(), changed.size(), name, patch, true));
        check(mgscoop::SelectDamageParameterPatch(source.data(), source.size(), name, patch, true));
        const std::string output(patch.bytes, patch.size);
        const auto address = patch.bytes;
        // The original table literal survives byte for byte between the
        // replaced call prefix and the appended adapter.
        const auto table_size = source.size() - prefix.size() - tail.size();
        check(source.compare(0, prefix.size(), prefix) == 0);
        check(output.compare(0, replaced_prefix.size(), replaced_prefix) == 0);
        check(output.compare(replaced_prefix.size(), table_size, source, prefix.size(), table_size) == 0);
        check(output.size() > replaced_prefix.size() + table_size + tail.size());
        check(output.compare(output.size()-tail.size(), tail.size(), tail) == 0);
        const auto adapter = output.substr(replaced_prefix.size() + table_size,
                                           output.size() - replaced_prefix.size() - table_size - tail.size());
        check(adapter.find("MGS_COOP_DAMAGE_ON_V1") != std::string::npos);
        check(adapter.find("MGS_COOP_DAMAGE_OFF_V1") != std::string::npos);
        check(adapter.find("CLEAR_IGNORE_DEFENDER_FLAG") != std::string::npos);
        check(adapter.find("_G.MgsCoopDamage = control") != std::string::npos);
        // A populated cache must not admit a changed original on later loads.
        check(!mgscoop::SelectDamageParameterPatch(changed.data(), changed.size(), name, patch, true));
        check(!patch.bytes && !patch.size);
        check(!mgscoop::SelectDamageParameterPatch(source.data(), source.size(), name, patch, false));
        std::atomic<unsigned> failures{0};
        std::vector<std::thread> workers;
        for (unsigned i = 0; i < 4; ++i) workers.emplace_back([&] {
            for (unsigned j = 0; j < 100; ++j) {
                mgscoop::script_bridge::Replacement current{};
                if (!mgscoop::SelectDamageParameterPatch(source.data(), source.size(), name, current, true) ||
                    current.bytes != address || std::string(current.bytes, current.size) != output) ++failures;
            }
        });
        for (auto& worker : workers) worker.join();
        check(failures == 0);
        std::ofstream saved(argv[2], std::ios::binary | std::ios::trunc);
        saved.write(output.data(), static_cast<std::streamsize>(output.size()));
        check(saved.good());
        std::cout << checks << " exact-source checks; 400 concurrent selections; table bytes preserved; no game calls\n";
        return 0;
    } catch (unsigned n) { std::cerr << "failed check " << n << '\n'; return 1; }
}
