// Checks exact-source gating of TppMission.lua for the death-policy wrapper
// with a local extracted script; no engine calls. Writes the composed source
// for the Lua 5.1 check (death_policy_composed_lua_check).
#include <atomic>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#include "mgscoop/bootstrap_patch.hpp"

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: death_policy_patch_check TppMission.lua composed.lua\n"; return 2; }
    std::ifstream input(argv[1], std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    if (!input || source.empty()) return 2;
    constexpr auto name = "@/Assets/tpp/script/lib/TppMission.lua";
    const std::string ending = "return e";
    mgscoop::script_bridge::Replacement patch{};
    unsigned checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) throw checks; };
    try {
        check(source.size() == 104102);
        check(source.compare(source.size()-ending.size(), ending.size(), ending) == 0);
        check(!mgscoop::SelectDeathPolicyPatch(source.data(), source.size(), name, patch, false));
        check(!patch.bytes && !patch.size);
        check(!mgscoop::SelectDeathPolicyPatch(nullptr, source.size(), name, patch, true));
        check(!mgscoop::SelectDeathPolicyPatch(source.data(), source.size(), nullptr, patch, true));
        check(!mgscoop::SelectDeathPolicyPatch(source.data(), source.size(), "@TppMission.lua", patch, true));
        check(!mgscoop::SelectDeathPolicyPatch(source.data(), source.size(), "@/Assets/tpp/script/lib/TppMain.lua", patch, true));
        check(!mgscoop::SelectDeathPolicyPatch(source.data(), source.size()-1, name, patch, true));
        auto changed = source; changed[source.size()/2] ^= 1;
        check(!mgscoop::SelectDeathPolicyPatch(changed.data(), changed.size(), name, patch, true));
        check(!patch.bytes && !patch.size);
        check(mgscoop::SelectDeathPolicyPatch(source.data(), source.size(), name, patch, true));
        const std::string output(patch.bytes, patch.size);
        const auto address = patch.bytes;
        // The original module survives byte for byte up to its final
        // `return e`; the wrapper is spliced in front of that statement.
        const auto prefix = source.size() - ending.size();
        check(output.size() > source.size());
        check(output.compare(0, prefix, source, 0, prefix) == 0);
        check(output.compare(output.size()-ending.size(), ending.size(), ending) == 0);
        const auto fragment = output.substr(prefix, output.size() - source.size());
        check(fragment.find("local original = e.OnPlayerDead") != std::string::npos);
        check(fragment.find("MgsCoopRespawn") != std::string::npos);
        // A populated cache must not admit a changed original on later loads.
        check(!mgscoop::SelectDeathPolicyPatch(changed.data(), changed.size(), name, patch, true));
        check(!patch.bytes && !patch.size);
        check(!mgscoop::SelectDeathPolicyPatch(source.data(), source.size(), name, patch, false));
        std::atomic<unsigned> failures{0};
        std::vector<std::thread> workers;
        for (unsigned i = 0; i < 4; ++i) workers.emplace_back([&] {
            for (unsigned j = 0; j < 100; ++j) {
                mgscoop::script_bridge::Replacement current{};
                if (!mgscoop::SelectDeathPolicyPatch(source.data(), source.size(), name, current, true) ||
                    current.bytes != address || std::string(current.bytes, current.size) != output) ++failures;
            }
        });
        for (auto& worker : workers) worker.join();
        check(failures == 0);
        std::ofstream saved(argv[2], std::ios::binary | std::ios::trunc);
        saved.write(output.data(), static_cast<std::streamsize>(output.size()));
        check(saved.good());
        std::cout << checks << " exact-source checks; 400 concurrent selections; module bytes preserved; no game calls\n";
        return 0;
    } catch (unsigned n) { std::cerr << "failed check " << n << '\n'; return 1; }
}
