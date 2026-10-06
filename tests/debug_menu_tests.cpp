#include "mgscoop/debug_menu.hpp"
#include <cstdio>
#include <cstring>
#include <atomic>
#include <initializer_list>
#include <thread>

namespace debug = mgscoop::debug_menu;
namespace {
unsigned checks{}, failures{};
void Check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
}
int main() {
    debug::Configure(false, true, true);
    Check(!debug::Enabled(), "disabled by default");
    Check(!debug::QueueCommand("position"), "off rejects commands");
    debug::Configure(true, true, false);
    Check(!debug::Enabled(), "save gate enforced");
    debug::Configure(true, true, true);
    Check(debug::Enabled(), "copied save debug enabled");
    Check(!debug::QueueCommand("position"), "title has no gameplay actions");
    debug::BeginMission(40010);
    Check(!debug::QueueCommand("position"), "ACC action gate");
    debug::BeginMission(30011);
    Check(!debug::QueueCommand("position"), "alternate mission action gate");
    debug::BeginMission(30010);
    for (const auto* bad : {"", "teleport 0 0 0 0;evil()", "weapon hip \"x\"", "position\n", "position\\", "position\t"})
        Check(!debug::QueueCommand(bad), "reject injection and control bytes");
    char huge[debug::kCommandCapacity + 1]{};
    std::memset(huge, 'a', sizeof(huge) - 1);
    Check(!debug::QueueCommand(huge), "reject oversize command");
    Check(debug::QueueCommand("teleport -123.5 456 789 180"), "numeric command queued");
    debug::Command command{};
    Check(debug::Pop(command), "owner drains command");
    Check(std::strcmp(command.text, "teleport -123.5 456 789 180") == 0, "text preserved");
    Check(command.generation == debug::GetSnapshot().generation, "mission generation bound");
    debug::Result(command.id, true, "verified_position");
    Check(std::strcmp(debug::GetSnapshot().last_result, "verified_position") == 0, "accepted result visible");
    const auto before = debug::GetSnapshot();
    debug::Snapshot telemetry{};
    telemetry.mission = 30010;
    telemetry.stage = 3;
    telemetry.player_pos[0] = 123.5f;
    debug::Publish(telemetry);
    const auto after = debug::GetSnapshot();
    Check(after.enabled && after.gameplay_ready && after.host && after.saves_redirected, "telemetry cannot disable lifecycle gates");
    Check(after.stage == 3 && after.player_pos[0] == 123.5f, "copied telemetry published");
    Check(after.last_id == before.last_id && after.completed == before.completed, "telemetry keeps action history");
    for (unsigned i = 0; i < debug::kQueueCapacity; ++i) Check(debug::QueueCommand("position"), "bounded queue accepts capacity");
    Check(!debug::QueueCommand("position"), "bounded queue refuses overflow");
    std::uint32_t previous = 0;
    for (unsigned i = 0; i < debug::kQueueCapacity; ++i) {
        Check(debug::Pop(command), "FIFO drains");
        Check(command.id > previous, "IDs ordered");
        previous = command.id;
    }
    Check(!debug::Pop(command), "empty queue returns no command");
    Check(debug::QueueCommand("step 0 0 10"), "pre-travel command accepted");
    const auto old_generation = debug::GetSnapshot().generation;
    debug::BeginMission(30020);
    Check(debug::GetSnapshot().generation != old_generation && !debug::Pop(command), "travel discards old teleport");
    debug::LuaState(true, 255);
    Check(debug::GetSnapshot().infinite_ammo && debug::GetSnapshot().bookmark_mask == 255, "Lua-owned cheat state copied");
    debug::EndMission();
    Check(!debug::QueueCommand("position") && !debug::Pop(command), "termination refuses actions");
    debug::BeginMission(30050);
    Check(!debug::GetSnapshot().infinite_ammo && debug::GetSnapshot().bookmark_mask == 0, "cheat state resets across scenes");

    // Real producer/consumer interleaving checks the UI/runtime exchange.
    std::atomic<bool> done{false};
    std::atomic<unsigned> accepted{0};
    std::thread producer([&] {
        for (unsigned i = 0; i < 1000; ++i) {
            if (debug::QueueCommand("position")) accepted.fetch_add(1);
            std::this_thread::yield();
        }
        done.store(true);
    });
    unsigned received = 0;
    previous = 0;
    bool unique = true;
    while (!done.load() || debug::GetSnapshot().queue_depth) {
        if (debug::Pop(command)) { unique = unique && command.id > previous; previous = command.id; ++received; }
        else std::this_thread::yield();
    }
    producer.join();
    Check(unique && received == accepted.load(), "concurrent accepted requests delivered once in order");
    debug::Configure(false, false, true);
    Check(!debug::Enabled() && debug::GetSnapshot().queue_depth == 0, "disable clears all actions");
    std::printf("debug menu: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
