#include "mgscoop/debug_menu.hpp"
#include "mgscoop/debug_actions.hpp"
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

    // Receipts: LOG codes 0 refused, 1 ok, 2 interim; queue refusals are status 3 with id 0.
    debug::Configure(true, true, true); debug::BeginMission(30010);
    const auto first_id = debug::QueueCommandId("teleport 1 2 3 0");
    Check(first_id != 0 && debug::QueueCommandId("position") == first_id + 1, "QueueCommandId returns increasing ids");
    Check(debug::Pop(command) && command.id == first_id, "id popped in order");
    debug::Result(first_id, 2, "submitted_warp");
    auto rs = debug::GetSnapshot();
    Check(rs.interim == 1 && rs.completed == 0 && rs.receipts[0].id == first_id && rs.receipts[0].status == 2,
          "interim result is a status-2 receipt and not completed");
    Check(std::strcmp(rs.receipts[0].command, "teleport 1 2 3 0") == 0, "receipt carries the dispatched command text");
    debug::Result(first_id, 1, "warp_position_readback;error_m=0.100");
    rs = debug::GetSnapshot();
    Check(rs.completed == 1 && rs.receipts[0].id == first_id && rs.receipts[0].status == 0 && rs.receipts[1].id == 0,
          "final result replaces its interim receipt");
    Check(debug::Pop(command), "second command dispatched");
    debug::Result(command.id, 0, "position_unreadable");
    rs = debug::GetSnapshot();
    Check(rs.completed == 2 && rs.rejected == 1 && rs.receipts[0].status == 1 && std::strcmp(rs.receipts[0].command, "position") == 0,
          "refused final result counted and paired by id");
    Check(debug::QueueCommandId("bad;text") == 0, "invalid text returns id 0");
    rs = debug::GetSnapshot();
    Check(rs.receipts[0].id == 0 && rs.receipts[0].status == 3 && rs.rejected == 2 && rs.receipts[0].command[0] == 0,
          "queue refusal is a status-3 receipt with id 0 and no unsafe text");
    debug::Result(99999, 3, "bad code");
    Check(debug::GetSnapshot().receipts[0].status == 3, "unknown result code ignored");
    for (unsigned i = 0; i < 20; ++i) { Check(debug::QueueCommand("position") && debug::Pop(command), "receipt fill"); debug::Result(command.id, 1, "position=1,2,3;yaw=0"); }
    rs = debug::GetSnapshot();
    Check(rs.receipts[15].id != 0 && rs.receipts[0].id == command.id, "receipt ring is bounded and newest first");
    debug::ClearHistory();
    Check(debug::GetSnapshot().receipts[0].id == 0 && debug::GetSnapshot().completed == rs.completed, "ClearHistory empties only the log");

    // STATE_V1: exactly 2 (legacy) or 12 bounded fields.
    debug::LuaFlags flags{};
    const auto parse = [&](const char* text) { return debug::ParseLuaState(text, std::strlen(text), flags); };
    Check(parse("1 129") && flags.infinite_ammo && flags.bookmark_mask == 129, "legacy two-field state accepted");
    Check(parse("1 3 4294967295 warp 34 1 0 1 43200 3000 6000 ok") && flags.pending_id == 4294967295u && std::strcmp(flags.pending_kind, "warp") == 0 &&
          flags.pending_frames == 34 && flags.god_mode && !flags.fulton_infinite && flags.slowmo_active && flags.game_clock_s == 43200 &&
          flags.player_life == 3000 && flags.player_life_max == 6000 && std::strcmp(flags.guard_reason, "ok") == 0, "twelve-field state parsed");
    Check(parse("0 0 0 none 0 0 0 0 0 -1 -1 on_foot") && flags.player_life == -1 && std::strcmp(flags.guard_reason, "on_foot") == 0, "unreadable life is -1");
    for (const auto* bad : {"1", "1 2 3", "0 0 0 none 0 0 0 0 0 -1 -1", "0 0 0 none 0 0 0 0 0 -1 -1 ok extra", "2 0", "1 256",
         "0 0 0 teleport 0 0 0 0 0 -1 -1 ok", "0 0 0 none 0 2 0 0 0 -1 -1 ok", "0 0 0 none 0 0 0 0 86400 -1 -1 ok",
         "0 0 0 none 0 0 0 0 0 -2 -1 ok", "0 0 0 none 0 0 0 0 0 -1 -1 OK", "0  0", " 0 0", "0 0 ", "0 0 0 none 0 0 0 0 0 -1 -1 ok;x",
         "0 0 0 none 100001 0 0 0 0 -1 -1 ok", "0 0 0 none 0 0 0 0 0 -1 -1 aaaaaaaaaaaaaaaaaaaaaaaa"})
        Check(!parse(bad), "malformed or out-of-range state rejected");
    const char twelve[] = "1 5 7 spawn 12 1 1 0 3600 10 20 ok";
    Check(debug::LuaStateReceipt(twelve, sizeof(twelve) - 1), "state receipt applied");
    rs = debug::GetSnapshot();
    Check(rs.infinite_ammo && rs.bookmark_mask == 5 && rs.pending_id == 7 && std::strcmp(rs.pending_kind, "spawn") == 0 && rs.god_mode &&
          rs.fulton_infinite && rs.game_clock_s == 3600 && rs.player_life == 10 && rs.player_life_max == 20, "Lua-owned flags visible");
    debug::Snapshot over{}; over.god_mode = false; over.player_life = 1; over.pending_id = 0; over.overlay_open = true; over.receipts[0].id = 77;
    over.enemy_nearby_count = 5; over.enemy_camps_loaded = true; over.interim = 9;
    debug::Publish(over);
    rs = debug::GetSnapshot();
    Check(rs.god_mode && rs.player_life == 10 && rs.pending_id == 7 && !rs.overlay_open && rs.receipts[0].id != 77 &&
          rs.enemy_nearby_count == 0 && !rs.enemy_camps_loaded && rs.interim == 1 && rs.published_tick64 != 0,
          "Publish preserves Lua-, receipt-, enemy- and overlay-owned fields");
    debug::OverlayStatus overlay{}; overlay.open = true; overlay.frames = 4; strcpy_s(overlay.category, "enemies");
    const auto before_overlay = debug::GetSnapshot();
    const auto serial = debug::RequestMenu("menu open enemies", before_overlay.mission, before_overlay.generation);
    std::uint32_t taken = 0; char menu_text[debug::kCommandCapacity]{};
    Check(serial && debug::TakeMenuRequest(taken, menu_text) && taken == serial && !std::strcmp(menu_text, "menu open enemies") &&
          !debug::TakeMenuRequest(taken, menu_text), "menu request taken once");
    const auto busy = debug::RequestMenu("menu close", before_overlay.mission, before_overlay.generation);
    Check(busy == serial + 1 && !std::strcmp(debug::GetSnapshot().overlay_result, "menu_busy") && debug::GetSnapshot().overlay_result_serial == busy,
          "single menu slot refuses a second request until the first resolves");
    debug::ReportMenuResult(serial, "menu_open;category=enemies;frames=4");
    const auto stale = debug::RequestMenu("menu close", before_overlay.mission, before_overlay.generation + 1);
    Check(!std::strcmp(debug::GetSnapshot().overlay_result, "menu_stale") && debug::GetSnapshot().overlay_result_serial == stale, "stale menu request refused");
    const auto malformed = debug::RequestMenu("menu press nothing", before_overlay.mission, before_overlay.generation);
    Check(!std::strcmp(debug::GetSnapshot().overlay_result, "menu_args") && debug::GetSnapshot().overlay_result_serial == malformed &&
          !debug::TakeMenuRequest(taken, menu_text), "malformed menu request refused without a slot");
    const auto ok = debug::RequestMenu("menu close", before_overlay.mission, before_overlay.generation);
    Check(debug::TakeMenuRequest(taken, menu_text) && taken == ok, "slot free after the result");
    debug::ReportMenuResult(ok, "menu_open;category=enemies;frames=4");
    debug::SetOverlayStatus(overlay); debug::SetLastUiAction("spawn_enemy_here");
    debug::Publish(over);
    rs = debug::GetSnapshot();
    Check(rs.overlay_open && rs.overlay_frames == 4 && rs.overlay_result_serial == ok && !std::strcmp(rs.overlay_result, "menu_open;category=enemies;frames=4") && std::strcmp(rs.overlay_category, "enemies") == 0 &&
          std::strcmp(rs.last_ui_action, "spawn_enemy_here") == 0, "overlay status survives telemetry");
    Check(std::strcmp(rs.last_command, before_overlay.last_command) == 0 && std::strcmp(rs.last_result, before_overlay.last_result) == 0 &&
          rs.receipts[0].id == before_overlay.receipts[0].id, "overlay status never touches last_command/last_result/receipts");
    debug::BeginMission(30020);
    rs = debug::GetSnapshot();
    Check(!rs.god_mode && rs.player_life == -1 && std::strcmp(rs.pending_kind, "none") == 0, "mission change resets Lua-owned state");

    // Nearby soldier transaction.
    Check(debug::QueueCommand("enemy_list 120") && debug::Pop(command), "nearby list dispatched");
    const auto list_id = command.id;
    Check(!receipt(list_id, "nearby 30010 120"), "nearby bound to the current mission");
    Check(!receipt(list_id, "nearby 30020 9") && !receipt(list_id, "nearby 30020 301"), "nearby radius bounded");
    Check(!receipt(list_id, "soldier 1 5 1000 0 0 sol_a"), "soldier requires an open list");
    Check(receipt(list_id, "nearby 30020 120"), "nearby list opened");
    Check(!receipt(list_id, "soldier 2 5 1000 0 0 sol_a"), "soldier indices ordered from 1");
    Check(receipt(list_id, "soldier 1 4294967295 1500 5 7 sol_a"), "dead marked placed soldier accepted");
    for (const auto* bad : {"soldier 2 6 1000 1 0 sol_b", "soldier 2 6 1000 6 0 sol_b", "soldier 2 6 1000 9 0 sol_b",
         "soldier 2 6 -1 0 0 sol_b", "soldier 2 6 1000 0 8 sol_b", "soldier 2 6 1000 0 0 bad/name", "soldier 2 6 1000 0 0 sol_b extra"})
        Check(!receipt(list_id, bad), "soldier code/flags/name bounded to Lua wire values");
    char soldier[96]{};
    const unsigned codes[] = {0, 2, 3, 4, 8};
    for (unsigned i = 2; i <= debug::kNearbyCount; ++i) {
        std::snprintf(soldier, sizeof(soldier), "soldier %u %u %u %u 0 sol_%u", i, 100 + i, i * 1000, codes[i % 5], i);
        Check(receipt(list_id, soldier), "nearby list accepts 12 ordered soldiers");
    }
    Check(!receipt(list_id, "soldier 13 1 1 0 0 sol_x"), "nearby capacity never overflows");
    Check(receipt(list_id, "done") && !receipt(list_id, "done"), "nearby list closes once");
    rs = debug::GetSnapshot();
    Check(rs.enemy_nearby_count == 12 && rs.enemy_nearby_radius == 120 && rs.enemy_nearby[0].hash == 4294967295u &&
          rs.enemy_nearby[0].state == 5 && rs.enemy_nearby[0].flags == 7 && std::strcmp(rs.enemy_nearby[0].name, "sol_a") == 0, "nearby copied");
    Check(debug::QueueCommand("position") && debug::Pop(command) && !receipt(command.id, "nearby 30020 80"), "nearby author must be enemy_list");
    Check(debug::QueueCommand("enemy_camps") && debug::Pop(command), "camps dispatched");
    Check(receipt(command.id, "clear 30020") && receipt(command.id, "camp mafr_a_cp 1000 2"), "camps listed");
    Check(!debug::GetSnapshot().enemy_camps_loaded, "camps not loaded before done");
    Check(receipt(command.id, "done") && debug::GetSnapshot().enemy_camps_loaded, "camps done sets enemy_camps_loaded");
    Check(debug::GetSnapshot().enemy_nearby_count == 12, "camp refresh keeps the nearby table");
    debug::BeginMission(30020);
    Check(!debug::GetSnapshot().enemy_camps_loaded && debug::GetSnapshot().enemy_nearby_count == 0, "mission change clears enemy metadata");

    // Registry, search, availability and formatters.
    namespace act = mgscoop::debug_actions;
    std::size_t action_count = 0;
    const auto* actions = act::Actions(action_count);
    bool ids_unique = action_count > 60, fixed_valid = true;
    unsigned per_category[act::kCategoryCount]{};
    for (std::size_t i = 0; i < action_count; ++i) {
        for (std::size_t j = i + 1; j < action_count; ++j) if (std::strcmp(actions[i].id, actions[j].id) == 0) ids_unique = false;
        ++per_category[static_cast<unsigned>(actions[i].category)];
        if (actions[i].command && !(actions[i].flags & act::kParam))
            fixed_valid = fixed_valid && debug::ValidText(actions[i].command) && std::strlen(actions[i].command) <= 200;
        Check(act::FindAction(actions[i].id) == &actions[i], "every action is findable");
    }
    Check(ids_unique, "action ids unique"); Check(fixed_valid, "fixed commands pass the queue charset and size");
    for (unsigned c = 0; c < act::kCategoryCount; ++c) Check(per_category[c] > 0, "every category has actions");
    const char* expected_quick[] = {"player_position", "spawn_enemy_here", "enemy_kill_nearest", "enemy_sleep_nearest", "player_heal",
        "bookmark_save", "bookmark_return", "partner_goto", "world_time_noon", "enemy_clear", "cp_phase_alert", "mission_status"};
    for (const auto* id : expected_quick) Check(act::FindAction(id) != nullptr, "spec action id present");
    Check(std::strcmp(act::FindAction("enemy_kill_nearest")->command, "enemy kill nearest") == 0 &&
          std::strcmp(act::FindAction("enemy_unmark_all")->command, "enemy_unmark") == 0 &&
          std::strcmp(act::FindAction("mission_status")->command, "status") == 0 &&
          std::strcmp(act::FindAction("world_time_noon")->command, "time 43200") == 0, "registry command texts");
    const act::ActionDef* found[32]{};
    auto has = [&](std::size_t n, const char* id) { for (std::size_t i = 0; i < n && i < 32; ++i) if (!std::strcmp(found[i]->id, id)) return true; return false; };
    auto n = act::SearchActions("spawn", found, 32);
    Check(n >= 3 && has(n, "spawn_enemy_here") && has(n, "spawn_enemy_closest") && has(n, "spawn_enemy_route"), "search spawn lists the three spawn rows");
    n = act::SearchActions("NOON", found, 32); Check(n == 1 && has(n, "world_time_noon"), "search is case-insensitive");
    n = act::SearchActions("kill near", found, 32); Check(n == 1 && has(n, "enemy_kill_nearest"), "every word must match");
    Check(act::SearchActions("", found, 32) == 0 && act::SearchActions("   ", found, 32) == 0 && act::SearchActions(nullptr, found, 32) == 0, "empty search matches nothing");
    Check(act::SearchActions("zzzz", found, 32) == 0, "unknown word matches nothing");
    debug::Snapshot avail{}; avail.enabled = avail.gameplay_ready = avail.host = true;
    const auto& teleport = *act::FindAction("player_teleport");
    const auto reason = [&](const char* id, bool selection = false) { const char* r = act::DisabledReason(*act::FindAction(id), avail, selection); return r ? r : "ok"; };
    Check(!std::strcmp(reason("player_position"), "ok") && !act::DisabledReason(teleport, avail, false), "gameplay host can act");
    avail.gameplay_ready = false;
    Check(!std::strcmp(reason("player_position"), "gameplay") && !std::strcmp(reason("tools_clear_log"), "ok"), "UI-only rows work outside gameplay");
    avail.gameplay_ready = true; avail.host = false;
    Check(!std::strcmp(reason("spawn_enemy_here"), "host_only") && !std::strcmp(reason("enemy_list"), "ok") &&
          !std::strcmp(reason("enemy_selected_info", true), "ok") && !std::strcmp(reason("enemy_refresh_camps"), "ok") &&
          !std::strcmp(reason("world_time_noon"), "host_only") && !std::strcmp(reason("partner_show"), "ok"), "guest host-only rule");
    avail.host = true;
    Check(!std::strcmp(reason("player_use_current"), "position") && !std::strcmp(reason("enemy_goto_spawn"), "no_spawn") &&
          !std::strcmp(reason("enemy_goto_camp"), "no_camp") && !std::strcmp(reason("spawn_enemy_route"), "no_routes") &&
          !std::strcmp(reason("partner_goto"), "partner_pose") && !std::strcmp(reason("bookmark_return"), "bookmark_empty") &&
          !std::strcmp(reason("partner_clip_play"), "clip_unavailable") && !std::strcmp(reason("enemy_selected_kill"), "selection"), "missing prerequisites named");
    avail.remote_valid = avail.world_paired = true; strcpy_s(avail.link_state, "connected"); avail.remote_age_ms = 1000;
    Check(!std::strcmp(reason("partner_goto"), "ok"), "partner pose ready");
    avail.remote_age_ms = 1001; Check(!std::strcmp(reason("partner_goto"), "partner_pose"), "stale partner pose refused");
    avail.remote_age_ms = 10; strcpy_s(avail.link_state, "lost"); Check(!std::strcmp(reason("partner_goto"), "partner_pose"), "link down refused");
    act::Category cat{};
    Check(act::ParseCategory("enemies", cat) && cat == act::Category::enemies && act::ParseCategory("coop", cat) && cat == act::Category::partner &&
          act::ParseCategory("diag", cat) && cat == act::Category::tools && act::ParseCategory("mission", cat) && cat == act::Category::tools &&
          !act::ParseCategory("Enemies", cat) && !act::ParseCategory("", cat), "category names and aliases");
    char text[220]{};
    Check(act::FormatTeleport(text, sizeof(text), -123.5f, 4, 100000, -180) && !std::strcmp(text, "teleport -123.500 4.000 100000.000 -180.000"), "teleport format");
    Check(!act::FormatTeleport(text, sizeof(text), 100001, 0, 0, 0) && !act::FormatTeleport(text, sizeof(text), 0, 0, 0, 181) &&
          !act::FormatTeleport(text, 8, 0, 0, 0, 0), "teleport clamps and buffer");
    Check(act::FormatStep(text, sizeof(text), 0, 3, -100) && !std::strcmp(text, "step 0.000 3.000 -100.000") && !act::FormatStep(text, sizeof(text), 100.5f, 0, 0), "step format");
    Check(act::FormatClock(text, sizeof(text), 45296) && !std::strcmp(text, "12:34:56") && !act::FormatClock(text, sizeof(text), 86400), "clock format");
    Check(act::FormatTimeSet(text, sizeof(text), 23, 59) && !std::strcmp(text, "time 86340") && !act::FormatTimeSet(text, sizeof(text), 24, 0), "time set format");
    Check(act::FormatSlowmo(text, sizeof(text), 0.25f, 3) && !std::strcmp(text, "slowmo 0.25 3.0") && !act::FormatSlowmo(text, sizeof(text), 0.01f, 3) &&
          !act::FormatSlowmo(text, sizeof(text), 0.5f, 31), "slowmo format");
    Check(act::FormatPartnerModes(text, sizeof(text), "xform", "off", "net", "qs", "map", "net", "ac") &&
          !std::strcmp(text, "partner stage=3 source=net apply=xform input=off anim=net aset=qs dm=map look=net reset=ac"), "partner modes with reset");
    Check(act::FormatPartnerModes(text, sizeof(text), "input", "net", "off", "q", "on", "copy", nullptr) && !std::strstr(text, "reset") &&
          !act::FormatPartnerModes(text, sizeof(text), "xform", "off", "net", "qs", "map", "net", "none"), "partner reset values n|a|c|ac only");
    Check(act::FormatEnemyOp(text, sizeof(text), "sleep", 4294967295u) && !std::strcmp(text, "enemy sleep 4294967295") &&
          !act::FormatEnemyOp(text, sizeof(text), "wake", 1), "enemy op format");
    Check(act::FormatClear(text, sizeof(text), 25) && !std::strcmp(text, "clear_enemies 25") && !act::FormatClear(text, sizeof(text), 0) &&
          !act::FormatClear(text, sizeof(text), 101), "clear format");
    act::OverlayRequest request{};
    const struct { const char* text; act::OverlayOp op; const char* arg; } overlay_ok[] = {
        {"menu open", act::OverlayOp::open, ""}, {"menu open enemies", act::OverlayOp::open, "enemies"}, {"menu close", act::OverlayOp::close, ""},
        {"menu toggle", act::OverlayOp::toggle, ""}, {"menu category coop", act::OverlayOp::category, "coop"},
        {"menu search", act::OverlayOp::search, ""}, {"menu search kill near", act::OverlayOp::search, "kill near"},
        {"menu press spawn_enemy_here", act::OverlayOp::press, "spawn_enemy_here"}, {"menu confirm", act::OverlayOp::confirm, ""},
        {"menu cancel", act::OverlayOp::cancel, ""}, {"menu footer 1", act::OverlayOp::footer, "1"}, {"menu select 4294967295", act::OverlayOp::select, "4294967295"}};
    for (const auto& item : overlay_ok)
        Check(act::ParseOverlayRequest(item.text, request) && request.op == item.op && !std::strcmp(request.arg, item.arg), "overlay request grammar");
    for (const auto* bad : {"menu", "menu ", "menu open nowhere", "menu open player extra", "menu close now", "menu category", "menu press",
         "menu press no_such_action", "menu footer 2", "menu select 4294967296", "menu select -1", "menu select 01", "menu bogus",
         "menu search ", "menu  open", "position", "menu search aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "menu search a;b"})
        Check(!act::ParseOverlayRequest(bad, request), "overlay request rejects");
    char long_search[80] = "menu search ";
    std::memset(long_search + 12, 'a', 48);
    Check(act::ParseOverlayRequest(long_search, request) && std::strlen(request.arg) == 48, "48-character search accepted");
    long_search[60] = 'a';
    Check(!act::ParseOverlayRequest(long_search, request), "49-character search refused");
    Check(act::IsMenuText("menu open") && !act::IsMenuText("menuopen") && !act::IsMenuText("position"), "menu text routing");

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
