// Original F9 menu registry and command formatting. Pure: no game, UI or OS calls.
#include "mgscoop/debug_actions.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace mgscoop::debug_actions {
namespace {
using C = Category;
constexpr std::uint32_t H = kHostOnly, P = kParam, T = kToggle, U = kUnverified, S = kSyncs;
// Labels are short button text; tooltips explain scope. Host-only rows refuse on the guest.
constexpr ActionDef kActions[] = {
    // Player
    {"player_position", C::player, "Read position", "position", "pose where coordinates xyz yaw", "Reads your exact position and yaw.", 0},
    {"player_heal", C::player, "Heal", "heal", "health life restore", "Sets life to its maximum.", U},
    {"player_godmode", C::player, "Auto-heal", "godmode 1", "god mode invincible health", "Refills life on every update while alive. Lethal hits still kill.", P | T | U},
    {"player_infinite_ammo", C::player, "Infinite ammo", "infinite_ammo 1", "ammunition bullets reload", "Script infinite ammunition for your player.", P | T},
    {"player_fulton_infinite", C::player, "Infinite Fulton", "fulton_infinite 1", "fulton balloon extract", "Unlimited Fulton count for your player.", P | T | U},
    {"player_forward_5", C::player, "Forward 5 m", "forward 5", "move teleport ahead front", "Teleports 5 m in the direction you face.", 0},
    {"player_up_3", C::player, "Up 3 m", "step 0 3 0", "move teleport above height climb", "Teleports 3 m straight up.", 0},
    {"player_use_current", C::player, "Use current", nullptr, "teleport form copy position", "Copies your current position into the teleport form.", kNeedsPose},
    {"player_from_partner", C::player, "From partner", nullptr, "teleport form partner position", "Copies the partner's last network position into the teleport form.", kNeedsPartner},
    {"player_teleport", C::player, "Teleport", "teleport x y z yaw", "warp move coordinates", "Teleports to the form's XYZ and yaw (degrees).", P},
    {"player_step", C::player, "Move by offset", "step dx dy dz", "relative offset move", "Moves by the offset form values (metres).", P},
    {"bookmark_save", C::player, "Save bookmark", "bookmark n", "bookmark save slot position", "Saves your position in the selected slot (this mission only).", P},
    {"bookmark_return", C::player, "Go to bookmark", "return n", "bookmark go load slot return", "Teleports to the selected slot.", P | kNeedsBookmark},
    {"player_inventory", C::player, "Read inventory", "inventory", "weapons equipment loadout", "Reads the equipped hip, back and secondary weapons.", 0},
    {"player_refill_hip", C::player, "Refill hip", "weapon hip current", "reload ammo primary weapon", "Refills the current hip weapon.", 0},
    {"player_refill_back", C::player, "Refill back", "weapon back current", "reload ammo primary weapon", "Refills the current back weapon.", 0},
    {"player_refill_secondary", C::player, "Refill secondary", "weapon secondary current", "reload ammo pistol weapon", "Refills the current secondary weapon.", 0},
    {"player_drop_hip", C::player, "Drop hip gun", "drop_item current_hip n", "drop pickup weapon", "Drops a pickup of the current hip weapon.", P},
    {"player_drop_back", C::player, "Drop back gun", "drop_item current_back n", "drop pickup weapon", "Drops a pickup of the current back weapon.", P},
    {"player_drop_secondary", C::player, "Drop secondary gun", "drop_item current_secondary n", "drop pickup weapon pistol", "Drops a pickup of the current secondary weapon.", P},
    // Enemies
    {"spawn_enemy_here", C::enemies, "Spawn enemy here", "spawn_enemy here", "enemy soldier guard create bring near me warp",
     "Moves the nearest living guard (15-500 m) 5 m in front of you, facing you, armed. Needs lab.ini debug_soldier_warp=1; otherwise or when refused he is snapped to the nearest patrol route (5-60 m). The guest's copy of him is moved the same way (spawn sync).", H | P | S},
    {"spawn_enemy_closest", C::enemies, "Spawn at closest camp", "spawn_enemy closest", "enemy soldier guard camp", "Moves a guard of the closest camp onto its patrol route.", H | S},
    {"spawn_enemy_route", C::enemies, "Spawn on selected route", "spawn_enemy camp route", "enemy soldier guard patrol", "Moves a guard onto the selected camp route (point 0).", H | P | kNeedsRoutes | S},
    {"enemy_goto_spawn", C::enemies, "Go to spawned guard", "goto_spawn", "teleport travel guard", "Teleports next to the last confirmed spawned guard.", kNeedsSpawn},
    {"enemy_refresh_camps", C::enemies, "Refresh camps", "enemy_camps", "camp list outposts command post", "Reloads the camp list sorted by distance.", 0},
    {"enemy_load_routes", C::enemies, "Load routes", "enemy_routes camp", "camp patrol route list", "Loads the selected camp's patrol routes.", P | kNeedsCamp},
    {"enemy_goto_camp", C::enemies, "Go to camp", "goto_camp camp", "teleport travel outpost", "Teleports to the selected camp (guard position or checkpoint).", P | kNeedsCamp},
    {"enemy_list", C::enemies, "List nearby", "enemy_list r", "nearby soldiers scan table enemies", "Lists up to 12 nearby soldiers with state and flags.", P},
    {"enemy_kill_nearest", C::enemies, "Kill nearest", "enemy kill nearest", "dead eliminate", "Kills the nearest living soldier within 150 m.", H | S},
    {"enemy_sleep_nearest", C::enemies, "Sleep nearest", "enemy sleep nearest", "tranquilize asleep", "Puts the nearest living soldier within 150 m to sleep.", H | S},
    {"enemy_come_nearest", C::enemies, "Nearest comes at me", "enemy come nearest", "hostile alert discover", "Makes the nearest soldier discover you (he attacks).", H | U | S},
    {"enemy_disable_nearest", C::enemies, "Disable nearest", "enemy disable nearest", "remove hide", "Disables the nearest living soldier.", H | S},
    {"enemy_mark_nearest", C::enemies, "Mark nearest", "enemy mark nearest", "marker tag", "Marks the nearest living soldier on the map and world (host-local).", H | U},
    {"enemy_selected_kill", C::enemies, "Kill", "enemy kill <hash>", "selected dead", "Kills the selected soldier.", H | P | kNeedsSelection | S},
    {"enemy_selected_sleep", C::enemies, "Sleep", "enemy sleep <hash>", "selected asleep", "Puts the selected soldier to sleep.", H | P | kNeedsSelection | S},
    {"enemy_selected_faint", C::enemies, "Faint", "enemy faint <hash>", "selected stun", "Makes the selected soldier faint.", H | P | kNeedsSelection | U | S},
    {"enemy_selected_fulton", C::enemies, "Fulton", "enemy fulton <hash>", "selected extract recover", "Fultons the selected soldier (adds staff in the copied save).", H | P | kNeedsSelection | kConfirm | S},
    {"enemy_selected_come", C::enemies, "Come at me", "enemy come <hash>", "selected hostile discover", "Makes the selected soldier discover you.", H | P | kNeedsSelection | U | S},
    {"enemy_selected_ignore", C::enemies, "Ignore me", "enemy ignore <hash>", "selected restrict notice", "Restricts the selected soldier's noticing.", H | P | kNeedsSelection | S},
    {"enemy_selected_notice", C::enemies, "Notice again", "enemy notice <hash>", "selected restrict notice", "Restores the selected soldier's noticing.", H | P | kNeedsSelection | S},
    {"enemy_selected_disable", C::enemies, "Disable", "enemy disable <hash>", "selected remove", "Disables the selected soldier.", H | P | kNeedsSelection | S},
    {"enemy_selected_enable", C::enemies, "Enable", "enemy enable <hash>", "selected restore", "Enables the selected soldier when he is still usable.", H | P | kNeedsSelection | S},
    {"enemy_selected_mark", C::enemies, "Mark", "enemy mark <hash>", "selected marker", "Marks the selected soldier (host-local).", H | P | kNeedsSelection | U},
    {"enemy_selected_unmark", C::enemies, "Unmark", "enemy unmark <hash>", "selected marker", "Removes the selected soldier's marker.", H | P | kNeedsSelection | U},
    {"enemy_selected_info", C::enemies, "Info", "enemy info <hash>", "selected details", "Reads the selected soldier's name, camp, state and distance.", P | kNeedsSelection},
    {"enemy_clear", C::enemies, "Clear nearby", "clear_enemies r", "dead all area wipe", "Kills up to 16 living soldiers within the radius.", H | P | kConfirm | S},
    {"enemy_unmark_all", C::enemies, "Unmark all", "enemy_unmark", "markers remove", "Removes every marker this menu placed.", H | U},
    {"cp_phase_sneak", C::enemies, "Phase: sneak", "cp_phase nearest sneak", "command post alert reset calm", "Sets the nearest command post to sneak.", H | S},
    {"cp_phase_caution", C::enemies, "Phase: caution", "cp_phase nearest caution", "command post alert", "Sets the nearest command post to caution.", H | S},
    {"cp_phase_alert", C::enemies, "Phase: alert", "cp_phase nearest alert", "command post combat", "Sets the nearest command post to alert.", H | S},
    // Partner / co-op
    {"partner_goto", C::partner, "Go to partner", "goto_partner", "teleport travel join coop", "Teleports 2 m from the partner, facing him.", kNeedsPartner},
    {"partner_show", C::partner, "Show partner", "partner stage=3 source=net look=net", "visible body", "Shows the partner body driven by the network.", 0},
    {"partner_hide", C::partner, "Hide partner", "partner stage=0", "invisible body", "Hides the partner body.", 0},
    {"partner_default_net", C::partner, "Default network movement", "partner stage=3 source=net apply=xform anim=off input=off dm=map look=net", "preset animation", "The default animated partner profile.", 0},
    {"partner_warp_mode", C::partner, "Warp movement", "partner stage=3 source=net apply=warp anim=off input=off dm=off look=net", "preset", "Moves the partner by warps without animation.", 0},
    {"partner_apply_modes", C::partner, "Apply modes", "partner stage=3 source=net apply=m input=i anim=a aset=s dm=d look=l", "movement animation mode", "Applies the selected partner modes.", P},
    {"partner_clip_play", C::partner, "Play clip", "partner dm=on dmplay=n", "animation motion clip", "Plays the selected direct-motion clip on the partner body.", P | kNeedsClip},
    {"partner_clip_stop", C::partner, "Stop clip", "partner dmstop=1", "animation motion clip", "Stops the manual clip.", 0},
    {"partner_slowmo", C::partner, "Slow motion", "slowmo rate seconds", "slow time camera animation", "Experimental: slows this PC's whole world.", P | U},
    {"partner_slowmo_off", C::partner, "Slow motion off", "slowmo off", "slow time cancel", "Cancels the experimental slow motion.", U},
    // Name tag (docs/partner-nametag.md): handled natively, this PC only, in memory.
    {"partner_nametag_on", C::partner, "Name tag on", "nametag on", "name tag label overlay player", "Shows the partner's name above his head on this PC.", 0},
    {"partner_nametag_off", C::partner, "Name tag off", "nametag off", "name tag label overlay hide", "Hides the name tag on this PC.", 0},
    {"partner_nametag_status", C::partner, "Name tag status", "nametag status", "name tag label readout camera",
     "Reports the name tag state, the partner's name, distance, hover and camera.", 0},
    {"coop_downed_status", C::partner, "Incapacitation status", "downed_status", "down crawl hurt health revive sleep wake asleep", "Reads wounded versus asleep state, native health and control lock.", 0},
    {"coop_sleep_graph_status", C::partner, "Sleep animation state details", "sleep_graph_status", "sleep asleep graph native animation state",
     "Reads the current native animation state. A matching sleep-state hash does not prove a playing animation.", 0},
    {"coop_sleep_info_status", C::partner, "Native sleep controller", "sleep_info_status", "sleep asleep animation mode native controller stun",
     "Reads native sleep mode, requested mode, player action, motion action, life state and activity. Values do not prove a playing animation.", 0},
    {"coop_carry_status", C::partner, "Carry eligibility", "carry_status", "carry pickup sleeping wounded teammate body target",
     "Reads both native player carry queries. Each body shows eligible, carrier flag, gun target ID and gun target match. Attachment remains unverified.", 0},
    {"coop_carry_policy_status", C::partner, "Pickup policy details", "carry_policy_status", "carry pickup policy provider native",
     "Reads whether the native pickup policy provider exists. It does not change pickup permissions or attach a player.", 0},
    {"coop_carry_plugin_status", C::partner, "Carry target selection", "carry_plugin_status", "carry pickup selector target body shoulder native",
     "Reads the actual shoulder-carry controller target and native selection state. It does not pick up or attach a player.", 0},
    {"coop_carry_recipient_status", C::partner, "Carried-player state", "carry_recipient_status", "carry carried pickup teammate attachment carrier",
     "Reads both players' carry binding and pending exit. A matching binding still requires attachment confirmation.", 0},
    {"coop_carry_request_status", C::partner, "Pickup request state", "carry_request_status", "carry pickup request queued retry cleanup",
     "Reads the currently queued pickup requests. This does not start, cancel or complete a pickup.", 0},
    {"coop_carry_model_status", C::partner, "Carry animation ownership", "carry_model_status", "carry model animation ownership callbacks cleanup",
     "Reads current carry animation ownership and registered callbacks. Pickup and attachment remain unfinished.", 0},
    {"coop_revive_status", C::partner, "Revive input status", "revive_status", "revive action key e held input", "Reads whether the game observed your revive input. Close F9 and hold ACTION near your downed teammate for 3 seconds.", 0},
    {"coop_downed_pad_status", C::partner, "Downed movement input", "downed_pad_status", "down crawl slow movement input speed magnitude", "Reads the latest native movement input before and after the downed cap. Close F9 to test movement.", 0},
    // Normal-release companion controls; no research capacity or force mount.
    {"buddy_horse_spawn", C::partner, "Summon / recall D-Horse", "horse_spawn", "horse buddy spawn summon recall retry ride mount near me",
     "Requests the local D-Horse 4 m beside you. Close F9 and wait. If it is selected but missing, repeat this action to recall it. Use ACTION after it arrives. A selected horse is not a confirmed arrival or a second horse.", 0},
    {"buddy_horse_status", C::partner, "Horse status", "horse_status", "horse buddy count capacity riding mount loading debug",
     "Reads selected buddy, horse availability and your mounted posture. Arrival stays unverified until mounting; if missing, use Summon / recall again. Horse count and remote riders are not inferred.", 0},
    {"vehicle_info_status", C::partner, "Vehicle controller details", "vehicle_info_status", "vehicle passenger driver seat native debug",
     "Reads raw native vehicle selection and phase for each player body. These values do not prove a mounted or attached passenger.", 0},
    {"vehicle_seats_status", C::partner, "Current vehicle seats", "vehicle_seats", "vehicle seats passenger rider driver mount debug",
     "Reads the current vehicle native seats and your actual seat. Shared riding is still unverified.", 0},
    {"buddy_follow_status", C::partner, "D-Dog follow status", "buddy_status", "dog buddy follow sync companion",
     "Reads the host dog follow state and guest position error.", 0},
    // World
    {"world_time_dawn", C::world, "Dawn 06:00", "time 21600", "clock morning", "Sets the clock to 06:00.", H | S},
    {"world_time_noon", C::world, "Noon 12:00", "time 43200", "clock day midday", "Sets the clock to 12:00.", H | S},
    {"world_time_dusk", C::world, "Dusk 18:00", "time 64800", "clock evening", "Sets the clock to 18:00.", H | S},
    {"world_time_midnight", C::world, "Midnight", "time 0", "clock night", "Sets the clock to 00:00.", H | S},
    {"world_time_add_1h", C::world, "+1 hour", "time_add 3600", "clock forward advance", "Advances the clock by one hour.", H | S},
    {"world_time_set", C::world, "Set time", "time s", "clock hour minute", "Sets the clock to the HH:MM fields.", H | P | S},
    {"world_weather_sunny", C::world, "Sunny", "weather sunny", "weather clear", "Forces sunny weather.", H | S},
    {"world_weather_cloudy", C::world, "Cloudy", "weather cloudy", "weather overcast", "Forces cloudy weather.", H | S},
    {"world_weather_rainy", C::world, "Rainy", "weather rainy", "weather rain storm", "Forces rain.", H | S},
    {"world_weather_sandstorm", C::world, "Sandstorm", "weather sandstorm", "weather sand dust", "Forces a sandstorm.", H | S},
    {"world_weather_foggy", C::world, "Foggy", "weather foggy", "weather fog mist", "Forces fog (not in Africa).", H | S},
    {"world_weather_auto", C::world, "Weather auto", "weather auto", "weather release normal", "Releases the forced weather.", H | S},
    // Tools
    {"mission_status", C::tools, "Status", "status", "session guard readiness mission", "Reports mission, guard state and active toggles.", 0},
    {"tools_console", C::tools, "Send", "console text", "console command type", "Sends the console line as a text command.", P},
    {"tools_repeat_last", C::tools, "Repeat last", nullptr, "again redo ctrl r", "Repeats the last command sent from this menu.", 0},
    {"tools_clear_log", C::tools, "Clear log", nullptr, "receipts history", "Clears the receipt log.", 0},
    {"tools_reset_layout", C::tools, "Reset layout", nullptr, "window size position", "Restores the default window size and position.", 0},
};
constexpr std::size_t kActionCount = sizeof(kActions) / sizeof(kActions[0]);
char Lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
bool Contains(const char* haystack, const char* word, std::size_t size) noexcept {
    if (!haystack || !size) return false;
    for (const char* h = haystack; *h; ++h) {
        std::size_t i = 0;
        while (i < size && h[i] && Lower(h[i]) == Lower(word[i])) ++i;
        if (i == size) return true;
    }
    return false;
}
bool Matches(const ActionDef& a, const char* query) noexcept {
    bool any = false;
    for (const char* p = query; *p;) {
        while (*p == ' ') ++p;
        const char* start = p;
        while (*p && *p != ' ') ++p;
        const auto size = static_cast<std::size_t>(p - start);
        if (!size) continue;
        any = true;
        if (!Contains(a.label, start, size) && !Contains(a.command, start, size) && !Contains(a.keywords, start, size) &&
            !Contains(CategoryLabel(a.category), start, size)) return false;
    }
    return any;
}
bool OneOf(const char* value, std::initializer_list<const char*> allowed) noexcept {
    if (!value) return false;
    for (const char* a : allowed) if (std::strcmp(value, a) == 0) return true;
    return false;
}
bool Print(char* out, std::size_t capacity, int written) noexcept {
    return out && written > 0 && static_cast<std::size_t>(written) < capacity;
}
bool Finite(float v) noexcept { return std::isfinite(v) != 0; }
}

const ActionDef* Actions(std::size_t& count) noexcept { count = kActionCount; return kActions; }
const ActionDef* FindAction(const char* id) noexcept {
    if (!id) return nullptr;
    for (const auto& a : kActions) if (std::strcmp(a.id, id) == 0) return &a;
    return nullptr;
}
std::size_t SearchActions(const char* query, const ActionDef** out, std::size_t capacity) noexcept {
    if (!query) return 0;
    std::size_t n = 0;
    for (const auto& a : kActions) {
        if (!Matches(a, query)) continue;
        if (out && n < capacity) out[n] = &a;
        ++n;
    }
    return n;
}
bool PartnerPoseReady(const debug_menu::Snapshot& s) noexcept {
    return s.remote_valid && s.world_paired && std::strcmp(s.link_state, "connected") == 0 && s.remote_age_ms <= 1000;
}
const char* DisabledReason(const ActionDef& a, const debug_menu::Snapshot& s, bool has_selection) noexcept {
    // UI-only rows (no command) work outside gameplay; everything sent waits for it.
    if (a.command && (!s.enabled || !s.gameplay_ready || s.terminating)) return "gameplay";
    if ((a.flags & kHostOnly) && !s.host) return "host_only";
    if ((a.flags & kNeedsPose) && !s.pose_valid) return "position";
    if ((a.flags & kNeedsSpawn) && !s.enemy_spawn_valid) return "no_spawn";
    if ((a.flags & kNeedsCamp) && !s.enemy_camp_count) return "no_camp";
    if ((a.flags & kNeedsRoutes) && !s.enemy_route_count) return "no_routes";
    if ((a.flags & kNeedsPartner) && !PartnerPoseReady(s)) return "partner_pose";
    if ((a.flags & kNeedsBookmark) && !s.bookmark_mask) return "bookmark_empty";
    if ((a.flags & kNeedsClip) && !s.motion_count) return "clip_unavailable";
    if ((a.flags & kNeedsSelection) && !has_selection) return "selection";
    return nullptr;
}
const char* CategoryName(Category value) noexcept {
    switch (value) {
    case Category::player: return "player";
    case Category::enemies: return "enemies";
    case Category::partner: return "partner";
    case Category::world: return "world";
    default: return "tools";
    }
}
const char* CategoryLabel(Category value) noexcept {
    switch (value) {
    case Category::player: return "Player";
    case Category::enemies: return "Enemies";
    case Category::partner: return "Partner / Co-op";
    case Category::world: return "World";
    default: return "Tools";
    }
}
bool ParseCategory(const char* text, Category& out) noexcept {
    if (!text) return false;
    for (std::size_t i = 0; i < kCategoryCount; ++i) {
        const auto c = static_cast<Category>(i);
        if (std::strcmp(text, CategoryName(c)) == 0) { out = c; return true; }
    }
    if (std::strcmp(text, "coop") == 0) { out = Category::partner; return true; }
    if (std::strcmp(text, "diag") == 0 || std::strcmp(text, "mission") == 0) { out = Category::tools; return true; }
    return false;
}
const char* MissionName(std::uint32_t mission) noexcept {
    switch (mission) {
    case 30010: return "Afghanistan";
    case 30020: return "Africa";
    case 30050: return "Mother Base";
    default: return nullptr;
    }
}
const char* LifeCodeName(std::uint32_t code) noexcept {
    switch (code) {
    case 0: return "normal";
    case 2: return "sleep";
    case 3: return "faint";
    case 4: return "dying";
    case 5: return "dead";
    case 8: return "disabled";
    default: return "unknown";
    }
}
bool IsMenuText(const char* text) noexcept {
    return text && std::strncmp(text, "menu ", 5) == 0;
}
bool ParseOverlayRequest(const char* text, OverlayRequest& out) noexcept {
    out = {};
    if (!IsMenuText(text) || !debug_menu::ValidText(text)) return false;
    const char* p = text + 5;
    const char* verb = p;
    while (*p && *p != ' ') ++p;
    const auto verb_size = static_cast<std::size_t>(p - verb);
    const char* arg = *p == ' ' ? p + 1 : p;
    const auto arg_size = std::strlen(arg);
    if (*p == ' ' && (!arg_size || arg[0] == ' ' || arg[arg_size - 1] == ' ')) return false;
    auto is = [&](const char* word) noexcept { return verb_size == std::strlen(word) && std::memcmp(verb, word, verb_size) == 0; };
    auto single = [&]() noexcept { return arg_size && !std::strchr(arg, ' ') && arg_size < sizeof(out.arg); };
    Category category{};
    if (is("open")) {
        if (arg_size && (!single() || !ParseCategory(arg, category))) return false;
        out.op = OverlayOp::open;
    } else if (is("close") || is("toggle") || is("confirm") || is("cancel")) {
        if (arg_size) return false;
        out.op = is("close") ? OverlayOp::close : is("toggle") ? OverlayOp::toggle : is("confirm") ? OverlayOp::confirm : OverlayOp::cancel;
    } else if (is("category")) {
        if (!single() || !ParseCategory(arg, category)) return false;
        out.op = OverlayOp::category;
    } else if (is("search")) {
        if (arg_size > 48) return false;
        out.op = OverlayOp::search;
    } else if (is("press")) {
        if (!single() || !FindAction(arg)) return false;
        out.op = OverlayOp::press;
    } else if (is("footer")) {
        if (std::strcmp(arg, "0") != 0 && std::strcmp(arg, "1") != 0) return false;
        out.op = OverlayOp::footer;
    } else if (is("select")) {
        if (!single() || arg_size > 10) return false;
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < arg_size; ++i) {
            if (arg[i] < '0' || arg[i] > '9') return false;
            value = value * 10 + static_cast<unsigned>(arg[i] - '0');
        }
        if (value > 0xffffffffull || (arg_size > 1 && arg[0] == '0')) return false;
        out.op = OverlayOp::select;
    } else return false;
    std::memcpy(out.arg, arg, arg_size + 1);
    return true;
}
bool FormatTeleport(char* out, std::size_t capacity, float x, float y, float z, float yaw) noexcept {
    for (const float v : {x, y, z}) if (!Finite(v) || std::fabs(v) > 100000.0f) return false;
    if (!Finite(yaw) || yaw < -180.0f || yaw > 180.0f) return false;
    return Print(out, capacity, std::snprintf(out, capacity, "teleport %.3f %.3f %.3f %.3f", x, y, z, yaw));
}
bool FormatStep(char* out, std::size_t capacity, float dx, float dy, float dz) noexcept {
    for (const float v : {dx, dy, dz}) if (!Finite(v) || std::fabs(v) > 100.0f) return false;
    return Print(out, capacity, std::snprintf(out, capacity, "step %.3f %.3f %.3f", dx, dy, dz));
}
bool FormatClock(char* out, std::size_t capacity, std::uint32_t seconds) noexcept {
    if (seconds >= 86400) return false;
    return Print(out, capacity, std::snprintf(out, capacity, "%02u:%02u:%02u", seconds / 3600, seconds / 60 % 60, seconds % 60));
}
bool FormatTimeSet(char* out, std::size_t capacity, int hours, int minutes) noexcept {
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) return false;
    return Print(out, capacity, std::snprintf(out, capacity, "time %d", hours * 3600 + minutes * 60));
}
bool FormatSlowmo(char* out, std::size_t capacity, float rate, float seconds) noexcept {
    if (!Finite(rate) || !Finite(seconds) || rate < 0.05f || rate > 1.0f || seconds < 0.1f || seconds > 30.0f) return false;
    return Print(out, capacity, std::snprintf(out, capacity, "slowmo %.2f %.1f", rate, seconds));
}
bool FormatPartnerModes(char* out, std::size_t capacity, const char* apply, const char* input, const char* anim,
                        const char* aset, const char* dm, const char* look, const char* reset) noexcept {
    if (!OneOf(apply, {"xform", "warp", "hybrid", "input"}) || !OneOf(input, {"off", "net"}) ||
        !OneOf(anim, {"off", "net", "mirror"}) || !OneOf(aset, {"q", "s", "qs", "r"}) || !OneOf(dm, {"map", "off", "on"}) ||
        !OneOf(look, {"net", "copy", "quiet", "snake"}) || (reset && *reset && !OneOf(reset, {"n", "a", "c", "ac"}))) return false;
    const bool with_reset = reset && *reset;
    return Print(out, capacity, std::snprintf(out, capacity, "partner stage=3 source=net apply=%s input=%s anim=%s aset=%s dm=%s look=%s%s%s",
        apply, input, anim, aset, dm, look, with_reset ? " reset=" : "", with_reset ? reset : ""));
}
bool FormatEnemyOp(char* out, std::size_t capacity, const char* op, std::uint32_t hash) noexcept {
    if (!OneOf(op, {"kill", "sleep", "faint", "fulton", "come", "ignore", "notice", "disable", "enable", "mark", "unmark", "info"})) return false;
    return Print(out, capacity, std::snprintf(out, capacity, "enemy %s %u", op, hash));
}
bool FormatClear(char* out, std::size_t capacity, int radius) noexcept {
    if (radius < 1 || radius > 100) return false;
    return Print(out, capacity, std::snprintf(out, capacity, "clear_enemies %d", radius));
}
} // namespace mgscoop::debug_actions
