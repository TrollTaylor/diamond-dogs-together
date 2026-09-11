// Checks exact-source gating with a local extracted script; no engine calls.
#include <fstream>
#include <iterator>
#include <string>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include "mgscoop/bootstrap_patch.hpp"
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    if (!input || source.empty()) return 2;
    constexpr auto name = "@/Assets/tpp/script/lib/TppMain.lua";
    mgscoop::script_bridge::Replacement patch{};
    unsigned checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) throw checks; };
    try {
        check(!mgscoop::SelectMissionTracePatch(source.data(), source.size(), name, patch, false));
        check(!mgscoop::SelectMissionTracePatch(nullptr, source.size(), name, patch, true));
        check(!mgscoop::SelectMissionTracePatch(source.data(), source.size(), nullptr, patch, true));
        check(!mgscoop::SelectMissionTracePatch(source.data(), source.size(), "@TppMain.lua", patch, true));
        check(!mgscoop::SelectMissionTracePatch(source.data(), source.size()-1, name, patch, true));
        auto changed = source; changed[10] ^= 1;
        check(!mgscoop::SelectMissionTracePatch(changed.data(), changed.size(), name, patch, true));
        check(mgscoop::SelectMissionTracePatch(source.data(), source.size(), name, patch, true));
        const std::string output(patch.bytes, patch.size);
        const auto address = patch.bytes;
        const auto insertion=source.size()-8;
        check(output.substr(0,insertion)==source.substr(0,insertion));
        check(output.substr(output.size()-(source.size()-insertion))==source.substr(insertion));
        check(output.substr(output.size()-8) == "return e");
        check(!mgscoop::SelectMissionTracePatch(changed.data(), changed.size(), name, patch, true));
        check(!patch.bytes && !patch.size);
        check(!mgscoop::SelectMissionTracePatch(source.data(), source.size(), name, patch, false));
        std::atomic<unsigned> failures{0};
        std::vector<std::thread> workers;
        for (unsigned i=0; i<4; ++i) workers.emplace_back([&] {
            for (unsigned j=0; j<100; ++j) {
                mgscoop::script_bridge::Replacement current{};
                if (!mgscoop::SelectMissionTracePatch(source.data(), source.size(), name, current, true) ||
                    current.bytes != address || std::string(current.bytes,current.size) != output) ++failures;
            }
        });
        for (auto& worker : workers) worker.join();
        check(failures == 0);
        std::ofstream saved(argv[2], std::ios::binary); saved << output;
        check(saved.good());
        std::cout << checks << " exact-source checks; 400 concurrent selections; no game calls\n";
        return 0;
    } catch (unsigned n) { std::cerr << "failed check " << n << '\n'; return 1; }
}
