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

    debug::Configure(true, true, true);
    debug::BeginMission(30010);
    const auto receipt = [](std::uint32_t id, const char* payload) {
        char text[300]{};
        const auto n = std::snprintf(text, sizeof(text), "return 'MGS_COOP_DEBUG_ENEMY_V1' -- %u %s", id, payload);
        return n > 0 && debug::EnemyReceipt(text, static_cast<std::size_t>(n));
    };
    Check(!receipt(1, "clear 30010"), "undispatched metadata cannot authorize a UI list");
    Check(debug::QueueCommand("enemy_camps"), "camp request queued");
    const auto queued_id = debug::GetSnapshot().last_id;
    Check(!receipt(queued_id, "clear 30010"), "queued metadata must wait until owner dispatch");
    Check(debug::Pop(command), "camp owner dispatch");
    const auto camps_id = command.id;
    Check(!receipt(camps_id, "clear 30020"), "wrong mission metadata refused");
    Check(receipt(camps_id, "clear 30010"), "current dispatched camp list accepted");
    Check(!receipt(camps_id, "clear 30010"), "duplicate clear cannot erase accepted entries");
    Check(receipt(camps_id, "camp afgh_test_cp -1 64"), "safe camp with unknown loaded distance");
    for (const auto* bad : {"camp afgh_test_cp 5 2", "camp bad/name 5 2", "camp bad 5 0", "camp bad 5 65",
         "camp bad -2 2", "camp bad 1000000001 2", "camp bad nan 2", "camp bad 5 2 extra", "camp bad\t 5 2"})
        Check(!receipt(camps_id, bad), "malformed/duplicate camp is rejected");
    char payload[200]{};
    for (unsigned i = 1; i < 32; ++i) {
        std::snprintf(payload, sizeof(payload), "camp afgh_test_%u_cp 1000000000 1", i);
        Check(receipt(camps_id, payload), "camp list accepts bounded capacity");
    }
    Check(!receipt(camps_id, "camp overflow_cp 1 1"), "camp capacity never overflows");
    Check(receipt(camps_id, "done"), "camp transaction closes");
    Check(!receipt(camps_id, "camp late_cp 1 1"), "closed list rejects late entry");
    Check(debug::QueueCommand("enemy_routes afgh_test_cp") && debug::Pop(command), "selected route owner dispatch");
    const auto routes_id = command.id;
    Check(!receipt(routes_id, "routes wrong_cp"), "route list bound to requested camp");
    Check(receipt(routes_id, "routes afgh_test_cp"), "selected current camp route list accepted");
    Check(!receipt(camps_id, "clear 30010"), "older clear cannot erase newer selected routes");
    Check(!receipt(routes_id, "route 0 rt_zero"), "route index0 rejected");
    Check(!receipt(routes_id, "route 2 rt_gap"), "route gaps rejected");
    for (unsigned i = 1; i <= 64; ++i) {
        std::snprintf(payload, sizeof(payload), "route %u rt_%u", i, i);
        Check(receipt(routes_id, payload), "route list accepts bounded capacity");
    }
    Check(!receipt(routes_id, "route 65 rt_overflow"), "route capacity never overflows");
    Check(receipt(routes_id, "done"), "route transaction closes");
    Check(!receipt(routes_id, "done"), "duplicate completion rejected");
    Check(debug::QueueCommand("spawn_enemy closest") && debug::Pop(command), "spawn owner dispatch");
    const auto spawn_request = command.id;
    Check(!receipt(spawn_request, "spawned 1000000001 0 0 1"), "spawn coordinate overflow rejected");
    Check(!receipt(spawn_request, "spawned 1 2 3 4294967296"), "unsigned entity overflow rejected");
    Check(receipt(spawn_request, "spawned -1000000000 0 1000000000 4294967295"), "signed coordinate and unsigned hash boundaries accepted");
    Check(!receipt(spawn_request, "spawned 1 2 3 1"), "same spawn cannot replay metadata");
    auto enemy_snapshot = debug::GetSnapshot();
    Check(enemy_snapshot.enemy_camp_count == 32 && enemy_snapshot.enemy_route_count == 64 &&
          std::strcmp(enemy_snapshot.enemy_routes_camp, "afgh_test_cp") == 0, "copied camps/routes readable by render thread");
    Check(enemy_snapshot.enemy_spawn_valid && enemy_snapshot.enemy_spawn_hash == 0xffffffffu &&
          enemy_snapshot.enemy_spawn_pos_mm[0] == -1000000000 && enemy_snapshot.enemy_spawn_pos_mm[2] == 1000000000,
          "spawn readback remains exact signed mm");
    telemetry = {}; telemetry.mission = 99999; telemetry.world_configured = true; telemetry.world_effective = 3;
    debug::Publish(telemetry);
    enemy_snapshot = debug::GetSnapshot();
    Check(enemy_snapshot.mission == 30010 && enemy_snapshot.enemy_camp_count == 32 && enemy_snapshot.enemy_route_count == 64 &&
          enemy_snapshot.enemy_spawn_valid, "telemetry cannot clobber owner mission or Lua enemy metadata");
    Check(enemy_snapshot.world_configured && enemy_snapshot.world_effective == 3, "new world telemetry published independently");
    debug::BeginMission(30020);
    Check(!receipt(spawn_request, "spawned 1 2 3 1") && !debug::GetSnapshot().enemy_spawn_valid &&
          debug::GetSnapshot().enemy_camp_count == 0, "travel clears metadata and previous-generation authorization");
    Check(debug::QueueCommand("position") && debug::Pop(command), "unrelated owner request");
    Check(!receipt(command.id, "clear 30020"), "unrelated command cannot forge camp metadata");
    Check(debug::QueueCommand("enemy_routes absent_cp") && debug::Pop(command), "unknown camp request delivered for guarded refusal");
    Check(!receipt(command.id, "routes absent_cp"), "metadata requires listed camp");
    Check(debug::QueueCommand("enemy_camps") && debug::Pop(command), "metadata request dispatched before cache-expiry test");
    const auto expired_id = command.id;
    for (unsigned i = 0; i < 64; ++i) Check(debug::QueueCommand("position") && debug::Pop(command), "bounded dispatch authorization cache advances");
    Check(!receipt(expired_id, "clear 30020"), "expired dispatch cannot replay a metadata transaction");
    char oversized[300]{}; std::memset(oversized, 'a', sizeof(oversized));
    Check(!debug::EnemyReceipt(oversized, sizeof(oversized)), "oversize receipt rejected before parsing");
    Check(!debug::EnemyReceipt(nullptr, 0), "null receipt rejected");
    debug::Configure(true, false, true); debug::BeginMission(30010);
    Check(debug::QueueCommand("spawn_enemy closest") && debug::Pop(command), "guest queues action for explicit Lua refusal");
    Check(!receipt(command.id, "spawned 1 2 3 1"), "guest cannot publish local guard mutation");
    debug::EndMission(); Check(!receipt(command.id, "clear 30010"), "termination rejects all metadata");
    debug::Configure(true, true, true); debug::BeginMission(30050);

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
