// Local check using the user's extracted game source, never a running game.
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include "mgscoop/bootstrap_patch.hpp"

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) { std::cerr << "usage: bootstrap_patch_check original.lua patched.lua [archive-patched.lua]\n"; return 2; }
    std::ifstream input(argv[1], std::ios::binary);
    const std::string original{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (!input || original.empty()) return 2;
    constexpr char name[] = "@/Assets/tpp/script/lib/Tpp.lua";
    mgscoop::script_bridge::Replacement patch{};
    unsigned checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) { std::cerr << "check=" << checks << '\n'; throw "patch check failed"; } };
    try {
        check(!mgscoop::SelectBootstrapPatch(nullptr, original.size(), name, patch));
        check(!mgscoop::SelectBootstrapPatch(original.data(), original.size(), nullptr, patch));
        check(!mgscoop::SelectBootstrapPatch(original.data(), original.size(), "@Tpp.lua", patch));
        check(!mgscoop::SelectBootstrapPatch(original.data(), original.size()-1, name, patch));
        auto changed = original; changed[10] ^= 1;
        check(!mgscoop::SelectBootstrapPatch(changed.data(), changed.size(), name, patch));
        check(mgscoop::SelectBootstrapPatch(original.data(), original.size(), name, patch));
        const auto* address = patch.bytes;
        const std::string output(patch.bytes, patch.size);
        check(output.size() > original.size());
        check(output.substr(0, original.size()-8) == original.substr(0, original.size()-8));
        check(output.substr(output.size()-8) == "return e");
        check(output.find("MGS_COOP_BOOTSTRAP_EXECUTED_V1") != std::string::npos);
        // A populated cache must not admit a changed original on later loads.
        check(!mgscoop::SelectBootstrapPatch(changed.data(), changed.size(), name, patch));
        check(patch.bytes == nullptr && patch.size == 0);
        std::atomic<unsigned> concurrent_failures{0};
        std::vector<std::thread> workers;
        for (unsigned i = 0; i < 4; ++i) workers.emplace_back([&] {
            for (unsigned j = 0; j < 100; ++j) {
                mgscoop::script_bridge::Replacement current{};
                if (!mgscoop::SelectBootstrapPatch(original.data(), original.size(), name, current) ||
                    current.bytes != address || std::string(current.bytes, current.size) != output)
                    concurrent_failures.fetch_add(1);
            }
        });
        for (auto& worker : workers) worker.join();
        check(concurrent_failures.load() == 0);
        std::ofstream saved(argv[2], std::ios::binary | std::ios::trunc);
        saved.write(output.data(), static_cast<std::streamsize>(output.size()));
        check(saved.good());
        if (argc == 4) {
            check(mgscoop::SelectBootstrapPatch(original.data(), original.size(), name, patch, true));
            const std::string archive_output(patch.bytes, patch.size);
            const auto* archive_address = patch.bytes;
            check(archive_address != address);
            check(archive_output.substr(0, original.size()-8) == original.substr(0, original.size()-8));
            check(archive_output.substr(archive_output.size()-8) == "return e");
            check(archive_output.find("MgsCoopArchiveProbe.lua") != std::string::npos);
            check(output.find("MgsCoopArchiveProbe.lua") == std::string::npos);
            check(!mgscoop::SelectBootstrapPatch(changed.data(), changed.size(), name, patch, true));
            workers.clear();
            for (unsigned i = 0; i < 4; ++i) workers.emplace_back([&, i] {
                const bool mounted = i % 2 != 0;
                for (unsigned j = 0; j < 100; ++j) {
                    mgscoop::script_bridge::Replacement current{};
                    if (!mgscoop::SelectBootstrapPatch(original.data(), original.size(), name, current, mounted) ||
                        current.bytes != (mounted ? archive_address : address) ||
                        std::string(current.bytes, current.size) != (mounted ? archive_output : output))
                        concurrent_failures.fetch_add(1);
                }
            });
            for (auto& worker : workers) worker.join();
            check(concurrent_failures.load() == 0);
            std::ofstream archive_saved(argv[3], std::ios::binary | std::ios::trunc);
            archive_saved.write(archive_output.data(), static_cast<std::streamsize>(archive_output.size()));
            check(archive_saved.good());
        }
        std::cout << checks << " patch checks, 400 concurrent selections; source bytes preserved; no game calls\n";
        return 0;
    } catch (const char* error) { std::cerr << error << '\n'; return 1; }
}
