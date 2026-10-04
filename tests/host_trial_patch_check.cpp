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
        check(!mgscoop::SelectHostTrialPatch(source.data(), source.size(), name, patch, false));
        check(!mgscoop::SelectHostTrialPatch(nullptr, source.size(), name, patch, true));
        check(!mgscoop::SelectHostTrialPatch(source.data(), source.size(), nullptr, patch, true));
        check(!mgscoop::SelectHostTrialPatch(source.data(), source.size(), "@TppMain.lua", patch, true));
        check(!mgscoop::SelectHostTrialPatch(source.data(), source.size()-1, name, patch, true));
        auto changed = source; changed[10] ^= 1;
        check(!mgscoop::SelectHostTrialPatch(changed.data(), changed.size(), name, patch, true));
        check(mgscoop::SelectHostTrialPatch(source.data(), source.size(), name, patch, true));
        const std::string output(patch.bytes, patch.size);
        const auto address = patch.bytes;
        const auto insertion=source.find("Mission.Start()");
        check(output.substr(0,insertion)==source.substr(0,insertion));
        const auto resumed=output.find("Mission.Start()",insertion);
        // Equipment preparation and marker guarding now add two further
        // insertion sites. Every interval of original code must survive intact.
        const auto equipment=source.find("TppEquip.StartLoadingToEquipMissionBlock()");
        const auto marker=source.find("TppMarker.RestoreMarkerLocator()");
        // Character selection (lua/coop/character.lua) follows MissionPrepare.
        const std::string character_anchor="if t(n.sequence.MissionPrepare)then\nn.sequence.MissionPrepare()end\n"
            "if t(n.sequence.OnEndMissionPrepareSequence)then\n"
            "TppSequence.SetOnEndMissionPrepareFunction(n.sequence.OnEndMissionPrepareSequence)end\nend\n";
        const auto character_at=source.find(character_anchor);
        check(character_at!=std::string::npos && source.find(character_anchor,character_at+1)==std::string::npos);
        const auto character=character_at+character_anchor.size();
        check(resumed!=std::string::npos && character>insertion && equipment>character && marker>equipment);
        check(output.substr(resumed,character-insertion)==source.substr(insertion,character-insertion));
        const auto character_out=resumed+(character-insertion);
        const std::string character_header="\n-- MGS co-op character selection (original).";
        check(output.compare(character_out,character_header.size(),character_header)==0);
        const auto character_resumed=output.find(source.substr(character,equipment-character),character_out);
        check(character_resumed!=std::string::npos);
        const auto equipment_out=output.find(source.substr(equipment,marker-equipment),character_resumed);
        check(equipment_out!=std::string::npos);
        const std::string marker_call="TppMarker.RestoreMarkerLocator()";
        const auto marker_out=output.find(marker_call,equipment_out);
        check(marker_out!=std::string::npos && output.substr(marker_out+marker_call.size(),5)=="\nend\n");
        const auto tail=marker+marker_call.size();
        check(output.substr(marker_out+marker_call.size()+5,source.size()-tail-8)==source.substr(tail,source.size()-tail-8));
        check(output.find("local receipts = 0",resumed)!=std::string::npos);
        check(output.find("\n-- MGS co-op character readback (original).",equipment_out)!=std::string::npos);
        check(output.substr(output.size()-8) == "return e");
        check(!mgscoop::SelectHostTrialPatch(changed.data(), changed.size(), name, patch, true));
        check(!patch.bytes && !patch.size);
        check(!mgscoop::SelectHostTrialPatch(source.data(), source.size(), name, patch, false));
        std::atomic<unsigned> failures{0};
        std::vector<std::thread> workers;
        for (unsigned i=0; i<4; ++i) workers.emplace_back([&] {
            for (unsigned j=0; j<100; ++j) {
                mgscoop::script_bridge::Replacement current{};
                if (!mgscoop::SelectHostTrialPatch(source.data(), source.size(), name, current, true) ||
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
