// Original F9 debug menu layout (Dear ImGui, MIT; pinned in third_party/imgui).
// Drawing and UI state only: every game action is queued as registry text through
// debug_menu, and the natural Lua update owns the game calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cstdarg>
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "imgui.h"
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/debug_actions.hpp"
#include "mgscoop/debug_menu_ui.hpp"
#include "mgscoop/nametag.hpp"
#include "mgscoop/steam_online_state.hpp"
#include "mgscoop/coop_hud.hpp"
#include "mgscoop/coop_reason_text.hpp"

namespace mgscoop::debug_menu_ui {
namespace {
namespace act = debug_actions;
using act::ActionDef;
using act::Category;
using debug_menu::Snapshot;
constexpr std::size_t kText = debug_menu::kCommandCapacity;
constexpr float kRailWidth = 150.0f;
const ImVec4 kGreen(0.40f, 0.85f, 0.45f, 1.0f), kAmber(0.98f, 0.75f, 0.25f, 1.0f), kRed(0.95f, 0.40f, 0.38f, 1.0f),
    kGrey(0.62f, 0.62f, 0.66f, 1.0f), kAccent(0.25f, 0.50f, 0.85f, 1.0f);

struct Ticket { const ActionDef* action{}; std::uint32_t id{}; double time{}; };
struct Recent { const ActionDef* action{}; char command[kText]{}; };
struct Confirmation { const ActionDef* action{}; char command[kText]{}; double expires{}; };
struct PressState { const ActionDef* action{}; unsigned frames{}; bool active{}, ready{}; char result[96]{}; };
struct State {
    Category category{Category::player};
    char search[49]{};
    bool focus_search{}, search_active{}, footer_expanded{}, reset_layout{}, worker{};
    float teleport[3]{}, yaw{}, step[3]{0, 0, 2};
    bool teleport_set{};
    int bookmark{}, payload{1};
    bool spawn_come{};
    int nearby_radius{80}, clear_radius{25};
    std::uint32_t selected{};
    char camp[65]{};
    int route{};
    std::uint32_t refresh_generation{}, refresh_tries{};
    double refresh_time{-100.0};
    int apply{}, input{}, anim{}, aset{3}, dm{}, look{}, reset{};
    bool modes_edited{};
    int clip{};
    float slow_rate{0.25f}, slow_seconds{3.0f};
    int hour{12}, minute{};
    char console[kText]{};
    char history[16][kText]{};
    int history_count{}, history_at{-1};
    int log_filter{};
    Confirmation confirm{};
    Recent recent[6]{};
    Ticket tickets[24]{};
    const ActionDef* last_action{};
    char last_command[kText]{};
    PressState press{};
    bool drawing_page{};
    int page_key{-1};
    const ActionDef* drawn[160]{};
    std::size_t drawn_count{};
};
State ui{};

const char* const kApplyValues[] = {"xform", "warp", "hybrid", "input"};
const char* const kApplyNames[] = {"Transform", "Warp", "Hybrid", "Input"};
const char* const kInputValues[] = {"off", "net"};
const char* const kInputNames[] = {"Off", "Network"};
const char* const kAnimValues[] = {"off", "net", "mirror"};
const char* const kAnimNames[] = {"Off", "Network", "Mirror local"};
const char* const kAsetValues[] = {"q", "s", "r", "qs"};
const char* const kAsetNames[] = {"Quiet set", "Snake set", "Remote set", "Quiet + Snake"};
const char* const kDmValues[] = {"map", "off", "on"};
const char* const kDmNames[] = {"Automatic map", "Off", "Manual"};
const char* const kLookValues[] = {"net", "copy", "quiet", "snake"};
const char* const kLookNames[] = {"Network", "Copy local", "Quiet", "Snake"};
const char* const kResetValues[] = {"", "n", "a", "c", "ac"};
const char* const kResetNames[] = {"No reset", "Reset node", "Reset animation", "Reset controller", "Reset animation + controller"};
const char* const kClipNames[] = {"Stand idle", "Crouch idle", "Prone transition", "Crouch walk", "Crouch run", "Sprint",
    "Crawl", "Prone idle", "Crouch graph state", "Stand graph state", "Walk", "Run", "Jog",
    "Stand turn left 90", "Stand turn right 90", "Stand turn left 180", "Stand turn right 180",
    "Crouch turn left 90", "Crouch turn right 90", "Crouch turn left 180", "Crouch turn right 180",
    "Gun hold (graph probe)", "Gun hold start (graph probe)", "Gun hold end (graph probe)", "Gun fire (graph probe)",
    "Gun reload (graph probe)"};
constexpr int kNamedClips = static_cast<int>(sizeof(kClipNames) / sizeof(kClipNames[0]));
const char* const kQuick[] = {"player_position", "spawn_enemy_here", "enemy_kill_nearest", "enemy_sleep_nearest", "player_heal",
    "bookmark_save", "bookmark_return", "partner_goto", "world_time_noon"};

// Menu time; 0 before the first ImGui context exists (worker requests can arrive first).
double Now() noexcept { return ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0; }
template<std::size_t N> void Copy(char (&dst)[N], const char* src) noexcept {
    std::snprintf(dst, N, "%s", src ? src : "");
}
bool Is(const ActionDef& a, const char* id) noexcept { return std::strcmp(a.id, id) == 0; }
const ActionDef& Def(const char* id) noexcept {
    static const ActionDef missing{"missing", Category::tools, "Missing", nullptr, "", "", 0};
    const auto* a = act::FindAction(id);
    return a ? *a : missing;
}
int IndexOf(const char* value, const char* const* values, int count, int fallback) noexcept {
    for (int i = 0; i < count; ++i) if (std::strcmp(value, values[i]) == 0) return i;
    return fallback;
}
bool InNearby(const Snapshot& s, std::uint32_t hash) noexcept {
    for (std::uint32_t i = 0; i < s.enemy_nearby_count && i < debug_menu::kNearbyCount; ++i)
        if (s.enemy_nearby[i].hash == hash) return true;
    return false;
}
bool RoutesReady(const Snapshot& s) noexcept {
    return ui.camp[0] && std::strcmp(ui.camp, s.enemy_routes_camp) == 0 && s.enemy_route_count;
}
// Registry text for a row, using the current widget values for parameterised rows.
bool Build(const ActionDef& a, const Snapshot& s, char* out, std::size_t cap) noexcept {
    if (!a.command) return false;
    if (!(a.flags & act::kParam)) return std::snprintf(out, cap, "%s", a.command) > 0;
    const auto print = [&](int written) noexcept { return written > 0 && static_cast<std::size_t>(written) < cap; };
    if (Is(a, "player_godmode")) return print(std::snprintf(out, cap, "godmode %d", s.god_mode ? 0 : 1));
    if (Is(a, "player_infinite_ammo")) return print(std::snprintf(out, cap, "infinite_ammo %d", s.infinite_ammo ? 0 : 1));
    if (Is(a, "player_fulton_infinite")) return print(std::snprintf(out, cap, "fulton_infinite %d", s.fulton_infinite ? 0 : 1));
    if (Is(a, "player_teleport")) return ui.teleport_set && act::FormatTeleport(out, cap, ui.teleport[0], ui.teleport[1], ui.teleport[2], ui.yaw);
    if (Is(a, "player_step")) return act::FormatStep(out, cap, ui.step[0], ui.step[1], ui.step[2]);
    if (Is(a, "bookmark_save")) return print(std::snprintf(out, cap, "bookmark %d", ui.bookmark));
    if (Is(a, "bookmark_return")) return print(std::snprintf(out, cap, "return %d", ui.bookmark));
    if (std::strncmp(a.id, "player_drop_", 12) == 0) return print(std::snprintf(out, cap, "drop_item current_%s %d", a.id + 12, ui.payload));
    if (Is(a, "spawn_enemy_here")) return print(std::snprintf(out, cap, "spawn_enemy here%s", ui.spawn_come ? " come" : ""));
    if (Is(a, "spawn_enemy_route")) return RoutesReady(s) && ui.route >= 0 && static_cast<std::uint32_t>(ui.route) < s.enemy_route_count &&
        print(std::snprintf(out, cap, "spawn_enemy %s %u", ui.camp, s.enemy_routes[ui.route].index));
    if (Is(a, "enemy_load_routes")) return ui.camp[0] && print(std::snprintf(out, cap, "enemy_routes %s", ui.camp));
    if (Is(a, "enemy_goto_camp")) return ui.camp[0] && print(std::snprintf(out, cap, "goto_camp %s", ui.camp));
    if (Is(a, "enemy_list")) return print(std::snprintf(out, cap, "enemy_list %d", ui.nearby_radius));
    if (std::strncmp(a.id, "enemy_selected_", 15) == 0) return ui.selected && act::FormatEnemyOp(out, cap, a.id + 15, ui.selected);
    if (Is(a, "enemy_clear")) return act::FormatClear(out, cap, ui.clear_radius);
    if (Is(a, "partner_apply_modes")) return act::FormatPartnerModes(out, cap, kApplyValues[ui.apply], kInputValues[ui.input],
        kAnimValues[ui.anim], kAsetValues[ui.aset], kDmValues[ui.dm], kLookValues[ui.look], kResetValues[ui.reset]);
    if (Is(a, "partner_clip_play")) return print(std::snprintf(out, cap, "partner dm=on dmplay=%d", ui.clip));
    if (Is(a, "partner_slowmo")) return act::FormatSlowmo(out, cap, ui.slow_rate, ui.slow_seconds);
    if (Is(a, "world_time_set")) return act::FormatTimeSet(out, cap, ui.hour, ui.minute);
    if (Is(a, "tools_console")) return debug_menu::ValidText(ui.console) && print(std::snprintf(out, cap, "%s", ui.console));
    return false;
}
// nullptr when available, else a short reason code.
const char* Reason(const ActionDef& a, const Snapshot& s, bool built) noexcept {
    const bool selection = ui.selected && InNearby(s, ui.selected);
    if (const char* r = act::DisabledReason(a, s, selection)) return r;
    if (Is(a, "player_teleport") && !ui.teleport_set) return "set_destination";
    if (Is(a, "bookmark_return") && !(s.bookmark_mask & (1u << ui.bookmark))) return "bookmark_empty";
    if (Is(a, "partner_clip_play") && static_cast<std::uint32_t>(ui.clip) >= s.motion_count) return "clip_unavailable";
    if (Is(a, "player_use_current") && !s.pose_valid) return "position";
    if ((Is(a, "enemy_load_routes") || Is(a, "enemy_goto_camp")) && !ui.camp[0]) return "no_camp";
    if (Is(a, "spawn_enemy_route") && !RoutesReady(s)) return "no_routes";
    if (Is(a, "tools_console") && !debug_menu::ValidText(ui.console)) return ui.console[0] ? "invalid_text" : "empty";
    if (Is(a, "tools_repeat_last") && !ui.last_command[0]) return "nothing_sent";
    if (a.command && !built) return "invalid";
    return nullptr;
}
const char* ReasonText(const char* reason) noexcept {
    struct Entry { const char* code; const char* text; };
    static const Entry entries[] = {
        {"gameplay", "Waiting for supported on-foot gameplay (no loading, demo, vehicle or game over)."},
        {"mission", "F9 commands work only in Mother Base and Afghanistan/Africa free roam, not in story missions or the helicopter."},
        {"partner_elsewhere", "Your partner is in another mission. Meet in the same Mother Base or free roam first."},
        {"host_only", "Host only: use the host's menu."},
        {"position", "Your position is not readable yet."},
        {"no_spawn", "No spawned guard confirmed yet."},
        {"no_camp", "Choose a camp first (Refresh camps)."},
        {"no_routes", "Load the selected camp's routes first."},
        {"partner_pose", "No live partner position (needs a paired link and a pose younger than 1 s)."},
        {"bookmark_empty", "This bookmark slot is empty."},
        {"clip_unavailable", "This clip is not available in this runtime."},
        {"selection", "Select a soldier in the Nearby table first."},
        {"set_destination", "Set a destination first: Use current, From partner or edit the fields."},
        {"invalid_text", "Invalid characters: letters, digits, space and _ - + . = , : only."},
        {"empty", "Type a command first."},
        {"nothing_sent", "Nothing sent from this menu yet."},
        {"invalid", "A value is out of range."}};
    for (const auto& e : entries) if (std::strcmp(e.code, reason) == 0) return e.text;
    return reason;
}
const char* Scope(const ActionDef& a) noexcept {
    if (a.flags & act::kSyncs) return "Syncs to guest";
    if (a.flags & act::kHostOnly) return "Host only";
    return "Local only";
}
void RememberRecent(const ActionDef& a, const char* command) noexcept {
    int found = 5;
    for (int i = 0; i < 6; ++i) if (ui.recent[i].action && std::strcmp(ui.recent[i].command, command) == 0) { found = i; break; }
    for (int i = found; i > 0; --i) ui.recent[i] = ui.recent[i - 1];
    ui.recent[0].action = &a;
    Copy(ui.recent[0].command, command);
}
std::uint32_t Send(const ActionDef& a, const char* command) noexcept {
    const std::uint32_t id = debug_menu::QueueCommandId(command);
    int slot = 23;
    for (int i = 0; i < 24; ++i) if (ui.tickets[i].action == &a || !ui.tickets[i].action) { slot = i; break; }
    for (int i = slot; i > 0; --i) ui.tickets[i] = ui.tickets[i - 1];
    ui.tickets[0] = {&a, id, Now()};
    if (!Is(a, "tools_repeat_last")) {
        RememberRecent(a, command);
        ui.last_action = &a;
        Copy(ui.last_command, command);
    }
    debug_menu::SetLastUiAction(a.id);
    return id;
}
void StartConfirm(const ActionDef& a, const char* command) noexcept {
    ui.confirm.action = &a;
    Copy(ui.confirm.command, command);
    ui.confirm.expires = Now() + 5.0;
}
// Repeats a remembered command; confirm actions open their inline confirmation.
void Repeat(const ActionDef& a, const char* command) noexcept {
    if (a.flags & act::kConfirm) { ui.category = a.category; ui.search[0] = 0; StartConfirm(a, command); }
    else Send(a, command);
}
void UiOnly(const ActionDef& a, const Snapshot& s) noexcept {
    if (Is(a, "player_use_current") && s.pose_valid) {
        std::memcpy(ui.teleport, s.player_pos, sizeof(ui.teleport));
        if (s.local_yaw_valid) ui.yaw = s.player_yaw;
        ui.teleport_set = true;
    } else if (Is(a, "player_from_partner")) {
        std::memcpy(ui.teleport, s.remote_pos, sizeof(ui.teleport));
        ui.yaw = s.remote_yaw;
        ui.teleport_set = true;
    } else if (Is(a, "tools_repeat_last") && ui.last_action) {
        Repeat(*ui.last_action, ui.last_command);
    } else if (Is(a, "tools_clear_log")) {
        debug_menu::ClearHistory();
    } else if (Is(a, "tools_reset_layout")) {
        ui.reset_layout = true;
    }
}
const debug_menu::Receipt* FindReceipt(const Snapshot& s, std::uint32_t id) noexcept {
    for (const auto& r : s.receipts) if (r.id == id && id) return &r;
    return nullptr;
}
// The result chip for the receipt this row last queued (within 20 s).
void Chip(const ActionDef& a, const Snapshot& s) noexcept {
    const Ticket* ticket = nullptr;
    for (const auto& t : ui.tickets) if (t.action == &a) { ticket = &t; break; }
    if (!ticket || Now() - ticket->time > 20.0) return;
    char text[64]{};
    ImVec4 color = kAmber;
    const char* full = "";
    if (!ticket->id) { std::snprintf(text, sizeof(text), "Refused by the queue"); color = kRed; }
    else if (const auto* r = FindReceipt(s, ticket->id)) {
        full = r->result;
        std::snprintf(text, sizeof(text), "%s%.48s", r->status == 2 ? "... " : "", r->result);
        color = r->status == 0 ? kGreen : r->status == 2 ? kAmber : kRed;
    } else std::snprintf(text, sizeof(text), "... queued");
    ImGui::SameLine();
    ImGui::TextColored(color, "%s", text);
    if (full[0]) ImGui::SetItemTooltip("#%u %s", ticket->id, full);
}
void Tooltip(const ActionDef& a, const char* command, const char* reason) noexcept {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(a.tooltip);
    if (a.command) ImGui::TextColored(kGrey, "Command: %s", command[0] ? command : a.command);
    ImGui::TextColored(kGrey, "%s%s", Scope(a), (a.flags & act::kUnverified) ? "  |  unverified in this build" : "");
    if (reason) ImGui::TextColored(kAmber, "Unavailable: %s", ReasonText(reason));
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
void Resolve(const char* text) noexcept;
void NotDrawn() noexcept {
    char text[96]{};
    std::snprintf(text, sizeof(text), "not_drawn;action=%s", ui.press.action->id);
    Resolve(text);
}
void Resolve(const char* text) noexcept {
    std::snprintf(ui.press.result, sizeof(ui.press.result), "%s", text);
    ui.press.ready = true; ui.press.active = false;
}
struct RowStyle { const char* label{}; float width{}; float height{}; bool primary{}; };
// One action button: disabled reason, tooltip, inline confirm and result chip.
// Returns true only for a UI-only row the caller should act on (already handled here).
void Row(const ActionDef& a, const Snapshot& s, RowStyle style = {}) noexcept {
    char command[kText]{};
    const bool built = Build(a, s, command, sizeof(command));
    if (!built) command[0] = 0;
    const char* reason = Reason(a, s, built);
    if (ui.drawing_page && ui.drawn_count < 160) ui.drawn[ui.drawn_count++] = &a;
    const bool pressing = ui.drawing_page && ui.press.active && ui.press.action == &a && ui.press.frames >= 2;
    if (pressing) ImGui::SetScrollHereY(0.3f);
    ImGui::PushID(a.id);
    const char* label = style.label ? style.label : a.label;
    const float width = style.width > 0 ? style.width : std::max(200.0f, ImGui::CalcTextSize(label).x + 24.0f);
    const ImVec2 size(width, style.height > 0 ? style.height : ImGui::GetFrameHeight() * 1.25f);
    const bool confirming = ui.confirm.action == &a && Now() < ui.confirm.expires;
    int colors = 0;
    if (pressing) { ImGui::PushStyleColor(ImGuiCol_Button, kAmber); ++colors; }
    else if (style.primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.58f, 0.32f, 1.0f));
        colors += 2;
    }
    if (confirming) {
        char text[96]{};
        std::snprintf(text, sizeof(text), "Confirm %s?", label);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.22f, 0.20f, 1.0f)); ++colors;
        if (ImGui::Button(text, ImVec2(std::max(width, ImGui::CalcTextSize(text).x + 24.0f), size.y))) {
            Send(a, ui.confirm.command); ui.confirm = {};
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(0, size.y))) ui.confirm = {};
    } else {
        ImGui::BeginDisabled(reason != nullptr);
        const bool clicked = ImGui::Button(label, size);
        ImGui::EndDisabled();
        Tooltip(a, command, reason);
        if (clicked && !reason) {
            if (a.flags & act::kConfirm) StartConfirm(a, command);
            else if (a.command) Send(a, command);
            else UiOnly(a, s);
        }
        if (reason && (std::strcmp(reason, "host_only") == 0 || std::strcmp(reason, "gameplay") == 0)) {
            ImGui::SameLine();
            ImGui::TextColored(kGrey, "%s", std::strcmp(reason, "host_only") == 0 ? "Host only" : "Waiting");
        }
    }
    if (colors) ImGui::PopStyleColor(colors);
    if (pressing && ui.press.frames >= 3) {
        char text[kText + 96]{};
        if (reason) std::snprintf(text, sizeof(text), "disabled;action=%s;reason=%s", a.id, reason);
        else if (a.flags & act::kConfirm) { StartConfirm(a, command); std::snprintf(text, sizeof(text), "needs_confirm;action=%s", a.id); }
        else if (!a.command) { UiOnly(a, s); std::snprintf(text, sizeof(text), "pressed;action=%s;command=none", a.id); }
        else {
            const auto id = Send(a, command);
            if (id) std::snprintf(text, sizeof(text), "pressed;action=%s;command=%s;queued_id=%u", a.id, command, id);
            else std::snprintf(text, sizeof(text), "pressed;action=%s;command=%s;queue_refused", a.id, command);
        }
        Resolve(text);
    }
    Chip(a, s);
    ImGui::PopID();
}
void Row(const char* id, const Snapshot& s, RowStyle style = {}) noexcept { Row(Def(id), s, style); }
void Rows(std::initializer_list<const char*> ids, const Snapshot& s) noexcept {
    for (const char* id : ids) Row(id, s);
}
void Heading(const char* text) noexcept {
    ImGui::Spacing();
    ImGui::TextColored(kAccent, "%s", text);
    ImGui::Separator();
}
void Wrapped(const char* text) noexcept {
    ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
// ---------------------------------------------------------------- header
// Flow layout: continue the current line only when an item of `width` still
// fits, so the header and quick bar wrap instead of clipping at the edge.
void Flow(float width) noexcept {
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right) ImGui::SameLine();
}
float ButtonWidth(const char* label) noexcept {
    return ImGui::CalcTextSize(label, nullptr, true).x + 2.0f * ImGui::GetStyle().FramePadding.x;
}
void Segment(const ImVec4& color, const char* format, ...) noexcept {
    char text[128]{};
    va_list args; va_start(args, format); std::vsnprintf(text, sizeof(text), format, args); va_end(args);
    Flow(ImGui::CalcTextSize(text).x);
    ImGui::TextColored(color, "%s", text);
}
const char* LinkWord(const Snapshot& s) noexcept { return s.link_state[0] ? s.link_state : "unavailable"; }
void Header(const Snapshot& s) noexcept {
    const char* mission = act::MissionName(s.mission);
    ImGui::TextColored(s.host ? kAccent : kAmber, "%s", s.host ? "HOST" : "GUEST");
    const ImVec4 plain = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (mission) Segment(plain, "| %u %s", s.mission, mission); else Segment(plain, "| mission %u", s.mission);
    Segment(plain, "| gen %u", s.generation);
    const bool ready = s.gameplay_ready && !s.terminating && std::strcmp(s.guard_reason, "ok") == 0;
    if (ready) Segment(kGreen, "| READY");
    else Segment(kAmber, "| WAITING: %s", s.terminating ? "mission_end" : !s.gameplay_ready ? "scene" :
        s.guard_reason[0] ? s.guard_reason : "update");
    const bool up = std::strcmp(s.link_state, "connected") == 0;
    if (up) Segment(kGreen, "| Link up %u ms %.1f%% loss", s.rtt_ms, s.loss_permille / 10.0f);
    else Segment(kGrey, "| Link %s", LinkWord(s));
    // World sync: paired, the partner in another mission (the settled code 9),
    // or the pair-wait reason (world_wire pair_wait names).
    char partner_mission[24]{};
    const auto wait = static_cast<std::uint8_t>(s.world_pair_wait > 255 ? 255 : s.world_pair_wait);
    if (!s.world_configured) Segment(kGrey, "| World off");
    else if (s.world_paired) Segment(kGreen, "| World L%u paired", s.world_effective);
    else if (wait == 9 && s.world_partner_mission &&
             coop_text::FormatMissionShort(s.world_partner_mission, partner_mission, sizeof(partner_mission)))
        Segment(kAmber, "| World L%u: partner in %s", s.world_level, partner_mission);
    else Segment(kGrey, "| World L%u waiting (%s)", s.world_level, coop_text::PairWaitName(wait));
    if (s.realized) Segment(kGreen, "| Partner visible clip %u age %u ms", s.mapped_clip, s.remote_age_ms);
    else Segment(kGrey, "| Partner hidden");
    // Line 2: pending state, active toggles and telemetry age.
    if (s.pending_kind[0] && std::strcmp(s.pending_kind, "none") != 0)
        ImGui::TextColored(kAmber, "Pending: %s #%u (frame %u)", s.pending_kind, s.pending_id, s.pending_frames);
    else ImGui::TextColored(kGreen, "Idle");
    Segment(ImGui::GetStyleColorVec4(ImGuiCol_Text), "| Queue %u", s.queue_depth);
    const struct { bool on; const char* text; } toggles[] = {{s.god_mode, "Auto-heal ON"}, {s.infinite_ammo, "Inf ammo ON"},
        {s.fulton_infinite, "Fulton inf ON"}, {s.slowmo_active, "Slowmo ON"}};
    for (const auto& t : toggles) if (t.on) Segment(kAmber, "| %s", t.text);
    const ULONGLONG now = GetTickCount64();
    const double age = s.published_tick64 && now >= s.published_tick64 ? static_cast<double>(now - s.published_tick64) / 1000.0 : -1.0;
    if (age < 0) Segment(kGrey, "| telemetry none");
    else Segment(age > 1.0 ? kAmber : kGrey, "| telemetry %.1f s", age);
    if (age > 1.0) ImGui::SetItemTooltip("No game-thread update for %.0f s (pause, loading or menu).", age);
}
void QuickBar(const Snapshot& s) noexcept {
    ImGui::TextColored(kGrey, "Quick:");
    for (const char* id : kQuick) {
        const auto& a = Def(id);
        Flow(ButtonWidth(a.label));
        char command[kText]{};
        const bool built = Build(a, s, command, sizeof(command));
        const char* reason = Reason(a, s, built);
        ImGui::PushID(id);
        ImGui::BeginDisabled(reason != nullptr);
        if (Is(a, "spawn_enemy_here")) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.25f, 1.0f));
        const bool clicked = ImGui::Button(a.label);
        if (Is(a, "spawn_enemy_here")) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        Tooltip(a, command, reason);
        if (clicked && !reason) Send(a, command);
        ImGui::PopID();
    }
    ImGui::TextColored(kGrey, "Recent:");
    if (!ui.recent[0].action) Segment(kGrey, "(commands you send appear here; Ctrl+R repeats the last)");
    for (int i = 0; i < 6 && ui.recent[i].action; ++i) {
        char label[40]{};
        std::snprintf(label, sizeof(label), "%.28s##recent%d", ui.recent[i].action->label, i);
        Flow(ButtonWidth(label));
        const bool host_blocked = (ui.recent[i].action->flags & act::kHostOnly) && !s.host;
        ImGui::BeginDisabled(host_blocked || !s.gameplay_ready);
        if (ImGui::Button(label)) Repeat(*ui.recent[i].action, ui.recent[i].command);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", ui.recent[i].command);
    }
}
// ---------------------------------------------------------------- pages
void Player(const Snapshot& s) noexcept {
    if (s.pose_valid) ImGui::Text("X %.2f   Y %.2f   Z %.2f m   yaw %s%.1f", s.player_pos[0], s.player_pos[1], s.player_pos[2],
        s.local_yaw_valid ? "" : "? ", s.player_yaw);
    else ImGui::TextColored(kGrey, "Position not readable yet.");
    if (s.player_life >= 0) { ImGui::SameLine(); ImGui::Text("   Life %d / %d", s.player_life, s.player_life_max); }
    Rows({"player_position", "player_heal"}, s);
    Row("player_godmode", s, {s.god_mode ? "Auto-heal: ON (click to stop)" : "Auto-heal: off"});
    Row("player_infinite_ammo", s, {s.infinite_ammo ? "Infinite ammo: ON" : "Infinite ammo: off"});
    Row("player_fulton_infinite", s, {s.fulton_infinite ? "Infinite Fulton: ON" : "Infinite Fulton: off"});
    Heading("Movement");
    Rows({"player_forward_5", "player_up_3"}, s);
    ImGui::TextUnformatted("Teleport destination");
    Row("player_use_current", s);
    Row("player_from_partner", s);
    ImGui::SetNextItemWidth(330);
    if (ImGui::InputFloat3("XYZ (m)", ui.teleport, "%.2f")) ui.teleport_set = true;
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputFloat("Yaw (deg)", &ui.yaw, 0, 0, "%.1f")) ui.teleport_set = true;
    for (auto& v : ui.teleport) v = v < -100000.0f ? -100000.0f : v > 100000.0f ? 100000.0f : v;
    ui.yaw = ui.yaw < -180.0f ? -180.0f : ui.yaw > 180.0f ? 180.0f : ui.yaw;
    Row("player_teleport", s, {nullptr, 0, 0, true});
    ImGui::SetNextItemWidth(330); ImGui::InputFloat3("Offset (m)", ui.step, "%.2f");
    for (auto& v : ui.step) v = v < -100.0f ? -100.0f : v > 100.0f ? 100.0f : v;
    Row("player_step", s);
    Heading("Bookmarks (this mission)");
    ImGui::SetNextItemWidth(200); ImGui::SliderInt("Slot", &ui.bookmark, 0, 7);
    Rows({"bookmark_save", "bookmark_return"}, s);
    for (int n = 0; n < 8; ++n) {
        if (n % 4) ImGui::SameLine();
        ImGui::PushID(n);
        char label[24]{};
        std::snprintf(label, sizeof(label), "Save %d", n);
        ImGui::BeginDisabled(Reason(Def("bookmark_save"), s, true) != nullptr);
        if (ImGui::Button(label, ImVec2(70, 0))) { ui.bookmark = n; char c[16]{}; std::snprintf(c, sizeof(c), "bookmark %d", n); Send(Def("bookmark_save"), c); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        std::snprintf(label, sizeof(label), "Go %d", n);
        ImGui::BeginDisabled(!(s.bookmark_mask & (1u << n)) || !s.gameplay_ready);
        if (ImGui::Button(label, ImVec2(60, 0))) { ui.bookmark = n; char c[16]{}; std::snprintf(c, sizeof(c), "return %d", n); Send(Def("bookmark_return"), c); }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    Heading("Weapons");
    Rows({"player_inventory", "player_refill_hip", "player_refill_back", "player_refill_secondary"}, s);
    ImGui::SetNextItemWidth(220); ImGui::SliderInt("Drop payload", &ui.payload, 1, 10);
    Rows({"player_drop_hip", "player_drop_back", "player_drop_secondary"}, s);
    Wrapped("Refill and drop use the gun already equipped in that slot. Payload changes the dropped item's contents; it does not create more pickups.");
}
const char* StateName(std::uint32_t code) noexcept { return act::LifeCodeName(code); }
void NearbyTable(const Snapshot& s) noexcept {
    ImGui::SetNextItemWidth(220); ImGui::SliderInt("Radius (m)", &ui.nearby_radius, 10, 300);
    ImGui::SameLine(); Row("enemy_list", s);
    if (!s.enemy_nearby_count) { ImGui::TextColored(kGrey, "No list yet. Press List nearby."); return; }
    const float height = std::min(9.0f, static_cast<float>(s.enemy_nearby_count) + 1.5f) * ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::BeginTable("nearby", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY, ImVec2(0, height))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Dist m", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();
        for (std::uint32_t i = 0; i < s.enemy_nearby_count && i < debug_menu::kNearbyCount; ++i) {
            const auto& e = s.enemy_nearby[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char label[96]{};
            std::snprintf(label, sizeof(label), "%s##%u", e.name, e.hash);
            if (ImGui::Selectable(label, ui.selected == e.hash, ImGuiSelectableFlags_SpanAllColumns)) ui.selected = e.hash;
            ImGui::TableNextColumn(); ImGui::Text("%.0f", e.distance_mm * 0.001);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(StateName(e.state));
            ImGui::TableNextColumn(); ImGui::Text("%s%s%s", (e.flags & 1) ? "M" : "", (e.flags & 2) ? "I" : "", (e.flags & 4) ? "P" : "");
        }
        ImGui::EndTable();
    }
    if (ui.selected && InNearby(s, ui.selected)) ImGui::Text("Selected: %u", ui.selected);
    else ImGui::TextColored(kGrey, "Click a row to select a soldier.");
    const char* ops[] = {"kill", "sleep", "faint", "fulton", "come", "ignore", "notice", "disable", "enable", "mark", "unmark", "info"};
    for (int i = 0; i < 12; ++i) {
        char id[40]{};
        std::snprintf(id, sizeof(id), "enemy_selected_%s", ops[i]);
        if (i % 4) ImGui::SameLine();
        Row(id, s, {nullptr, 130.0f});
    }
}
void Camps(const Snapshot& s) noexcept {
    if (!ui.camp[0] && s.enemy_camp_count) Copy(ui.camp, s.enemy_camps[0].name);
    Row("enemy_refresh_camps", s);
    ImGui::SetNextItemWidth(390);
    if (ImGui::BeginCombo("Camp", ui.camp[0] ? ui.camp : "Refresh camps to choose")) {
        for (std::uint32_t i = 0; i < s.enemy_camp_count && i < 32; ++i) {
            const auto& c = s.enemy_camps[i];
            char label[128]{};
            if (c.distance_mm >= 0) std::snprintf(label, sizeof(label), "%s (%.0f m)", c.name, c.distance_mm * 0.001);
            else std::snprintf(label, sizeof(label), "%s (position unavailable)", c.name);
            if (ImGui::Selectable(label, std::strcmp(ui.camp, c.name) == 0)) { Copy(ui.camp, c.name); ui.route = 0; }
        }
        ImGui::EndCombo();
    }
    Rows({"enemy_load_routes", "enemy_goto_camp"}, s);
    if (ui.route < 0 || ui.route >= static_cast<int>(s.enemy_route_count)) ui.route = 0;
    ImGui::SetNextItemWidth(390);
    if (ImGui::BeginCombo("Route", RoutesReady(s) ? s.enemy_routes[ui.route].name : "Load routes for this camp")) {
        if (RoutesReady(s)) for (std::uint32_t i = 0; i < s.enemy_route_count && i < 64; ++i)
            if (ImGui::Selectable(s.enemy_routes[i].name, ui.route == static_cast<int>(i))) ui.route = static_cast<int>(i);
        ImGui::EndCombo();
    }
    Row("spawn_enemy_route", s);
}
void Enemies(const Snapshot& s) noexcept {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.20f, 0.13f, 0.85f));
    if (ImGui::BeginChild("spawn_box", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        ImGui::TextColored(kGreen, "Spawn near you");
        Row("spawn_enemy_here", s, {"Spawn enemy here", 260.0f, ImGui::GetFrameHeight() * 1.8f, true});
        ImGui::SameLine();
        ImGui::Checkbox("He comes at me (hostile)", &ui.spawn_come);
        Wrapped("Moves the nearest living guard 5 m in front of you, facing you (lab.ini debug_soldier_warp=1, engine "
                "set-transform message). Unchecked he ignores you; checked he attacks. If the native placement is off or "
                "refused he is snapped to the nearest patrol route instead (5-60 m). Host only; when the world link is paired "
                "the guest's copy of him is moved the same way (spawn sync; the receipt log shows remote spawn_enemy). At most 8 "
                "per mission. He is armed: enable Auto-heal or keep Kill/Sleep ready.");
        Rows({"spawn_enemy_closest", "enemy_goto_spawn"}, s);
        if (s.enemy_spawn_valid) ImGui::Text("Last guard %u at X %.1f Y %.1f Z %.1f m", s.enemy_spawn_hash,
            s.enemy_spawn_pos_mm[0] * 0.001, s.enemy_spawn_pos_mm[1] * 0.001, s.enemy_spawn_pos_mm[2] * 0.001);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    Heading("Nearby soldiers");
    NearbyTable(s);
    Heading("Nearest soldier (within 150 m)");
    Rows({"enemy_kill_nearest", "enemy_sleep_nearest", "enemy_come_nearest", "enemy_disable_nearest", "enemy_mark_nearest"}, s);
    Heading("Area");
    ImGui::SetNextItemWidth(220); ImGui::SliderInt("Clear radius (m)", &ui.clear_radius, 1, 100);
    Rows({"enemy_clear", "enemy_unmark_all", "cp_phase_sneak", "cp_phase_caution", "cp_phase_alert"}, s);
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Camps & routes")) Camps(s);
    if (!s.host) Wrapped("Guest: listing, info and travel work here; changing guards needs the host's menu.");
}
// The host's join code to the clipboard (secret-bearing buffers are cleared).
bool CopyJoinCode() noexcept {
    char code[80]{};
    if (!steam_state::BuildCode(code, sizeof(code))) return false;
    const std::size_t n = std::strlen(code) + 1;
    bool ok = false;
    if (OpenClipboard(nullptr)) {
        if (EmptyClipboard()) {
            if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, n)) {
                if (void* at = GlobalLock(memory)) {
                    std::memcpy(at, code, n);
                    GlobalUnlock(memory);
                    ok = SetClipboardData(CF_TEXT, memory) != nullptr;
                }
                if (!ok) GlobalFree(memory);
            }
        }
        CloseClipboard();
    }
    SecureZeroMemory(code, sizeof(code));
    return ok;
}
// Partner / Co-op -> Online (play mode with the Steam transport; docs/steam-online.md 10.2).
// Copied data only: every Steam action is a request the owner context performs.
void Online() noexcept {
    const steam_state::OnlineSnapshot o = steam_state::GetOnline();
    if (!o.available) return;
    if (!ImGui::CollapsingHeader("Online (Steam)", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const bool connected = std::strcmp(o.state, "connected") == 0;
    ImGui::TextColored(connected ? kGreen : std::strcmp(o.steam, "ok") == 0 || std::strcmp(o.steam, "acquiring") == 0 ? kAmber : kRed,
        "Steam %s   link %s   route %s   RTT %u ms", o.steam, o.state[0] ? o.state : "-", o.route[0] ? o.route : "none", o.rtt_ms);
    if (o.peer[0]) ImGui::Text("Partner %s  (%s)", o.peer_name[0] ? o.peer_name : "name pending", o.peer);
    if (o.host) {
        bool accepting = o.accepting;
        if (ImGui::Checkbox("Accept joins", &accepting)) {
            steam_state::RequestAccepting(accepting);
            debug_menu::SetLastUiAction(accepting ? "steam_accept_on" : "steam_accept_off");
        }
        ImGui::BeginDisabled(!o.code_ready);
        if (ImGui::Button("Copy join code")) {
            const bool copied = CopyJoinCode();
            debug_menu::SetLastUiAction(copied ? "steam_code_copied" : "steam_code_failed");
            if (copied) coop_hud::Post(coop_hud::EventKind::code_copied, 0, "");
        }
        ImGui::SameLine();
        ImGui::Button("Reveal (hold)");
        const bool reveal = ImGui::IsItemActive();
        ImGui::EndDisabled();
        if (reveal) {
            char code[80]{};
            if (steam_state::BuildCode(code, sizeof(code))) ImGui::TextUnformatted(code);
            SecureZeroMemory(code, sizeof(code));
        } else {
            ImGui::TextColored(kGrey, o.code_ready ? "MGS2-*****-*****-*****-*****-**@<your SteamID>" : "The code is ready once Steam networking is up.");
        }
        if (ImGui::TreeNode("Invite a friend")) {
            steam_state::NoteInvitePage(GetTickCount64());
            if (!o.friend_count) ImGui::TextColored(kGrey, o.friends_loading ? "Loading friends..." : "No friends listed yet.");
            for (std::uint32_t i = 0; i < o.friend_count && i < steam_state::kMaxFriends; ++i) {
                const steam_state::Friend& f = o.friends[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::BeginDisabled(f.invited);
                if (ImGui::SmallButton(f.invited ? "Invited" : "Invite")) {
                    steam_state::RequestInvite(f.id);
                    debug_menu::SetLastUiAction("steam_invite");
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (f.in_game) ImGui::TextColored(kGreen, "%s  (in MGSV)", f.name);
                else ImGui::TextUnformatted(f.name);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        Wrapped("Send the code to your friend, or invite them here. Only someone with the code can join.");
    } else {
        if (o.waiting) ImGui::TextColored(kAmber, "Waiting for an invite or a code.");
        if (std::strcmp(o.state, "rejected") == 0) ImGui::TextColored(kRed, "The host refused this code; paste a new one.");
        static char paste[steam_state::kPasteCapacity]{};
        ImGui::SetNextItemWidth(360);
        const bool enter = ImGui::InputTextWithHint("##steam_code", "Paste the host's MGS2 code", paste, sizeof(paste),
            ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const bool join = ImGui::Button("Join");
        if ((enter || join) && paste[0]) {
            steam_state::RequestJoin(paste);
            SecureZeroMemory(paste, sizeof(paste));
            debug_menu::SetLastUiAction("steam_join");
        }
        Wrapped("Or accept the host's invite (Shift+Tab -> chat).");
    }
    if (o.last_result[0]) ImGui::TextWrapped("%s", o.last_result);
    ImGui::Spacing();
}
void Partner(const Snapshot& s) noexcept {
    Online();
    const bool up = std::strcmp(s.link_state, "connected") == 0;
    ImGui::Text("Link %s   RTT %u ms   jitter %u ms   loss %.1f%%   interpolation %u ms", LinkWord(s), s.rtt_ms, s.jitter_ms,
        s.loss_permille / 10.0f, s.interp_ms);
    ImGui::Text("Partner %s   appearance %s   range %s   pose age %u ms", s.realized ? "visible" : "hidden",
        s.parts_loaded ? "loaded" : "pending", s.range_hidden ? "too far" : "near", s.remote_age_ms);
    if (!s.world_paired && s.world_pair_wait == 9 && s.world_partner_mission && s.world_partner_mission != s.mission) {
        char elsewhere[256]{};
        if (coop_text::FormatWorldNote(16, s.mission, s.world_partner_mission, s.world_partner_mission, s.mission, elsewhere,
                                       sizeof(elsewhere))) {
            ImGui::PushStyleColor(ImGuiCol_Text, kAmber);
            ImGui::TextWrapped("%s", elsewhere);
            ImGui::PopStyleColor();
        }
    }
    if (s.remote_valid) {
        ImGui::Text("Remote X %.2f Y %.2f Z %.2f   yaw %.1f", s.remote_pos[0], s.remote_pos[1], s.remote_pos[2], s.remote_yaw);
        if (s.body_valid) ImGui::Text("Body error %.3f m   heading error %.1f deg", s.position_error_m, s.heading_error_deg);
    } else ImGui::TextColored(kGrey, "No partner pose received yet%s.", up ? "" : " (link down)");
    ImGui::Text("Act %u   gun %u   flags 0x%02x   mapped clip %u", s.remote_act, s.remote_equip, s.remote_action_flags, s.mapped_clip);
    Row("partner_goto", s, {nullptr, 0, 0, true});
    Heading("Wounded and sleeping teammates");
    Rows({"coop_downed_status", "coop_revive_status", "coop_downed_pad_status", "coop_sleep_info_status", "coop_sleep_graph_status", "coop_carry_status", "coop_carry_plugin_status", "coop_carry_policy_status", "coop_carry_recipient_status", "coop_carry_request_status", "coop_carry_model_status"}, s);
    Wrapped("Wounded players crawl slowly. Sleep darts immobilize without losing health; sleepers wake after 30 seconds of paired gameplay. Close F9 and hold ACTION (E by default) within 3 m for 3 seconds to revive or wake your teammate.");
    Heading("Companion tests");
    Row("buddy_horse_spawn", s, {nullptr, 260.0f, ImGui::GetFrameHeight() * 1.5f, true});
    Rows({"buddy_horse_status", "buddy_follow_status", "vehicle_seats_status", "vehicle_info_status"}, s);
    Wrapped("Summon selects your local D-Horse beside you. Close F9, wait for it, then use the normal ACTION button to mount. "
        "This release uses the stock single-buddy pack; two independently synchronized mounts are still being developed. "
        "Horse status reports actual local posture and availability, never an invented remote rider.");
    for (const auto& r : s.receipts) {
        if (!std::strcmp(r.command, "horse_status") || !std::strcmp(r.command, "horse_spawn")) {
            ImGui::TextWrapped("Last horse test: %s", r.result); break;
        }
    }
    Heading("Name tag");
    {
        namespace tag = mgscoop::nametag;
        const auto settings = tag::GetSettings();
        const auto t = tag::GetTelemetry();
        if (!tag::Enabled()) ImGui::TextColored(kGrey, "Unavailable here (no partner session, or the camera contract refused).");
        else ImGui::Text("Name tag %s   %s (%s)   \"%s\"   %.1f m%s", settings.on ? "on" : "off",
            t.visible ? "shown" : "hidden", tag::ReasonName(t.reason), t.name, t.dist_m, t.hover ? "   on target" : "");
        Rows({"partner_nametag_on", "partner_nametag_off", "partner_nametag_status"}, s);
    }
    Heading("Presets");
    Rows({"partner_show", "partner_hide", "partner_default_net", "partner_warp_mode"}, s);
    Heading("Modes");
    if (!ui.modes_edited) {
        ui.apply = IndexOf(s.apply, kApplyValues, 4, 0); ui.anim = IndexOf(s.anim, kAnimValues, 3, 0);
        ui.aset = IndexOf(s.aset, kAsetValues, 4, 3); ui.dm = IndexOf(s.dm, kDmValues, 3, 0);
        ui.look = IndexOf(s.look, kLookValues, 4, 0); ui.input = s.input_armed ? 1 : 0;
    }
    bool edited = false;
    ImGui::SetNextItemWidth(210); edited |= ImGui::Combo("Movement", &ui.apply, kApplyNames, 4);
    ImGui::SetNextItemWidth(210); edited |= ImGui::Combo("Input", &ui.input, kInputNames, 2);
    ImGui::SetNextItemWidth(210); edited |= ImGui::Combo("Animation", &ui.anim, kAnimNames, 3);
    ImGui::SetNextItemWidth(210); edited |= ImGui::Combo("Animation set", &ui.aset, kAsetNames, 4);
    ImGui::SetNextItemWidth(210); edited |= ImGui::Combo("Direct motion", &ui.dm, kDmNames, 3);
    ImGui::SetNextItemWidth(210); edited |= ImGui::Combo("Look", &ui.look, kLookNames, 4);
    if (ImGui::TreeNode("Advanced")) {
        ImGui::SetNextItemWidth(260); edited |= ImGui::Combo("Reset", &ui.reset, kResetNames, 5);
        ImGui::TreePop();
    }
    if (edited) ui.modes_edited = true;
    Row("partner_apply_modes", s);
    if (ui.modes_edited) { ImGui::SameLine(); if (ImGui::SmallButton("Sync from game")) { ui.modes_edited = false; ui.reset = 0; } }
    Heading("Animation workbench");
    int clips = static_cast<int>(s.motion_count < 31 ? s.motion_count : 31);
    if (clips < kNamedClips) clips = kNamedClips;
    char preview[48]{};
    if (ui.clip < kNamedClips) Copy(preview, kClipNames[ui.clip]); else std::snprintf(preview, sizeof(preview), "Clip %d", ui.clip);
    ImGui::SetNextItemWidth(280);
    if (ImGui::BeginCombo("Clip", preview)) {
        for (int i = 0; i < clips; ++i) {
            char label[48]{};
            if (i < kNamedClips) std::snprintf(label, sizeof(label), "%d %s", i, kClipNames[i]); else std::snprintf(label, sizeof(label), "Clip %d", i);
            const bool available = static_cast<std::uint32_t>(i) < s.motion_count;
            if (ImGui::Selectable(label, ui.clip == i, available ? 0 : ImGuiSelectableFlags_Disabled)) ui.clip = i;
            if (i == 24) ImGui::SetItemTooltip("Fire may need two standalone requests (capacity priming).");
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(); ImGui::TextColored(kGrey, "%u clips available", s.motion_count);
    Rows({"partner_clip_play", "partner_clip_stop"}, s);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.22f, 0.16f, 0.06f, 0.7f));
    if (ImGui::BeginChild("experimental", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        ImGui::TextColored(kAmber, "Experimental: slow motion");
        ImGui::SetNextItemWidth(220); ImGui::SliderFloat("Rate", &ui.slow_rate, 0.05f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(220); ImGui::SliderFloat("Seconds", &ui.slow_seconds, 0.5f, 30.0f, "%.1f");
        Rows({"partner_slowmo", "partner_slowmo_off"}, s);
        Wrapped("Slows this PC's whole world (camera effects unverified). The other player sees your movement slowed.");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
void World(const Snapshot& s) noexcept {
    char clock[16]{};
    act::FormatClock(clock, sizeof(clock), s.game_clock_s % 86400);
    char handshake[40]{};
    if (s.world_paired) Copy(handshake, "paired");
    else std::snprintf(handshake, sizeof(handshake), "waiting (%s)",
                       coop_text::PairWaitName(static_cast<std::uint8_t>(s.world_pair_wait > 255 ? 255 : s.world_pair_wait)));
    ImGui::Text("Clock %s   world level %u   active level %u   handshake %s / link %s", clock, s.world_level, s.world_effective,
        handshake, s.world_link_up ? "connected" : "disconnected");
    if (s.world_partner_mission) {
        char partner_mission[32]{};
        if (coop_text::FormatMission(s.world_partner_mission, partner_mission, sizeof(partner_mission)))
            ImGui::Text("Partner mission %u (%s)   partner world level %u", s.world_partner_mission, partner_mission, s.world_remote_level);
    }
    Heading("Time");
    const char* times[] = {"world_time_dawn", "world_time_noon", "world_time_dusk", "world_time_midnight", "world_time_add_1h"};
    for (int i = 0; i < 5; ++i) { if (i) ImGui::SameLine(); Row(times[i], s, {nullptr, 150.0f}); }
    ImGui::SetNextItemWidth(110); ImGui::InputInt("HH", &ui.hour);
    ImGui::SameLine(); ImGui::SetNextItemWidth(110); ImGui::InputInt("MM", &ui.minute);
    ui.hour = ui.hour < 0 ? 0 : ui.hour > 23 ? 23 : ui.hour;
    ui.minute = ui.minute < 0 ? 0 : ui.minute > 59 ? 59 : ui.minute;
    ImGui::SameLine(); Row("world_time_set", s);
    Heading("Weather");
    const char* weather[] = {"world_weather_sunny", "world_weather_cloudy", "world_weather_rainy", "world_weather_sandstorm",
        "world_weather_foggy", "world_weather_auto"};
    int shown = 0;
    for (const char* id : weather) {
        if (s.mission == 30020 && std::strcmp(id, "world_weather_foggy") == 0) continue;
        if (shown++ % 3) ImGui::SameLine();
        Row(id, s, {nullptr, 150.0f});
    }
    Wrapped("Time and weather follow to the guest at world level 3. Only the host can change them.");
}
int ConsoleHistory(ImGuiInputTextCallbackData* data) noexcept {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || !ui.history_count) return 0;
    if (data->EventKey == ImGuiKey_UpArrow) ui.history_at = ui.history_at + 1 < ui.history_count ? ui.history_at + 1 : ui.history_at;
    else if (data->EventKey == ImGuiKey_DownArrow) ui.history_at = ui.history_at > 0 ? ui.history_at - 1 : -1;
    data->DeleteChars(0, data->BufTextLen);
    if (ui.history_at >= 0) data->InsertChars(0, ui.history[ui.history_at]);
    return 0;
}
void Tools(const Snapshot& s) noexcept {
    ImGui::Text("Mission %u   generation %u   ready %s   terminating %s   guard %s", s.mission, s.generation,
        s.gameplay_ready ? "yes" : "no", s.terminating ? "yes" : "no", s.guard_reason[0] ? s.guard_reason : "-");
    ImGui::Text("Role %s   offline %s   saves redirected %s   queued %u   completed %u   refused %u   interim %u", s.role,
        s.offline ? "yes" : "no", s.saves_redirected ? "yes" : "no", s.queue_depth, s.completed, s.rejected, s.interim);
    Row("mission_status", s);
    Heading("Console");
    ImGui::SetNextItemWidth(420);
    const bool enter = ImGui::InputTextWithHint("##console", "Type a text command, Enter sends", ui.console, sizeof(ui.console),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, &ConsoleHistory);
    ImGui::SameLine();
    const auto& send = Def("tools_console");
    char command[kText]{};
    const bool built = Build(send, s, command, sizeof(command));
    const char* reason = Reason(send, s, built);
    ImGui::BeginDisabled(reason != nullptr);
    const bool clicked = ImGui::Button("Send##console");
    ImGui::EndDisabled();
    Tooltip(send, command, reason);
    if ((enter || clicked) && ui.console[0]) {
        if (reason) debug_menu::SetLastUiAction("console_invalid");
        else {
            Send(send, command);
            for (int i = 15; i > 0; --i) Copy(ui.history[i], ui.history[i - 1]);
            Copy(ui.history[0], command);
            ui.history_count = ui.history_count < 16 ? ui.history_count + 1 : 16;
            ui.history_at = -1; ui.console[0] = 0;
        }
    }
    if (reason && ui.console[0] && std::strcmp(reason, "invalid_text") == 0) ImGui::TextColored(kRed, "Invalid characters");
    Chip(send, s);
    if (ImGui::CollapsingHeader("Command reference")) {
        std::size_t count = 0;
        const auto* all = act::Actions(count);
        for (std::size_t i = 0; i < count; ++i) {
            if (!all[i].command) continue;
            char line[260]{};
            std::snprintf(line, sizeof(line), "%-34s %s##ref%zu", all[i].command, all[i].label, i);
            if (ImGui::Selectable(line)) Copy(ui.console, all[i].command);
        }
        ImGui::TextColored(kGrey, "Click a line to paste it into the console.");
    }
    Heading("Receipt log");
    const char* filters[] = {"All", "OK", "Refused", "Pending"};
    ImGui::SetNextItemWidth(160); ImGui::Combo("Filter", &ui.log_filter, filters, 4);
    ImGui::SameLine(); Row("tools_clear_log", s);
    for (const auto& r : s.receipts) {
        if (!r.result[0]) continue;
        const int kind = r.status == 0 ? 1 : r.status == 2 ? 3 : 2;
        if (ui.log_filter && ui.log_filter != kind) continue;
        ImGui::TextColored(r.status == 0 ? kGreen : r.status == 2 ? kAmber : kRed, "#%u %s -> %s", r.id, r.command[0] ? r.command : "-", r.result);
    }
    Heading("Telemetry");
    ImGui::Text("Overlay frames %u   you X %.2f Y %.2f Z %.2f yaw %.1f", s.overlay_frames, s.player_pos[0], s.player_pos[1], s.player_pos[2], s.player_yaw);
    ImGui::Text("Remote X %.2f Y %.2f Z %.2f yaw %.1f   body X %.2f Y %.2f Z %.2f yaw %.1f", s.remote_pos[0], s.remote_pos[1], s.remote_pos[2],
        s.remote_yaw, s.body_pos[0], s.body_pos[1], s.body_pos[2], s.body_yaw);
    if (s.refusal[0]) ImGui::TextWrapped("Partner readiness: %s", s.refusal);
    Row("tools_reset_layout", s);
    Wrapped("Mission travel, restart and checkpoint saves are not in this menu.");
}
void Page(const Snapshot& s) noexcept {
    switch (ui.category) {
    case Category::player: Player(s); break;
    case Category::enemies: Enemies(s); break;
    case Category::partner: Partner(s); break;
    case Category::world: World(s); break;
    default: Tools(s); break;
    }
}
void SearchResults(const Snapshot& s) noexcept {
    const ActionDef* found[128]{};
    const auto n = act::SearchActions(ui.search, found, 128);
    ImGui::Text("Results for '%s' (%zu)", ui.search, n);
    for (std::size_t c = 0; c < act::kCategoryCount; ++c) {
        bool heading = false;
        for (std::size_t i = 0; i < n && i < 128; ++i) {
            if (found[i]->category != static_cast<Category>(c)) continue;
            if (s.mission == 30020 && Is(*found[i], "world_weather_foggy")) continue;
            if (!heading) { Heading(act::CategoryLabel(static_cast<Category>(c))); heading = true; }
            Row(*found[i], s);
        }
    }
    if (!n) ImGui::TextColored(kGrey, "No action matches. Esc clears the search.");
}
void Rail(const Snapshot& s) noexcept {
    if (ui.focus_search) { ImGui::SetKeyboardFocusHere(); ui.focus_search = false; }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search...", ui.search, sizeof(ui.search), ImGuiInputTextFlags_EscapeClearsAll);
    ui.search_active = ImGui::IsItemActive();
    ImGui::Spacing();
    for (std::size_t i = 0; i < act::kCategoryCount; ++i) {
        const auto c = static_cast<Category>(i);
        char label[48]{};
        if (c == Category::enemies && s.enemy_nearby_count) std::snprintf(label, sizeof(label), "%s (%u)", act::CategoryLabel(c), s.enemy_nearby_count);
        else std::snprintf(label, sizeof(label), "%s", act::CategoryLabel(c));
        if (ImGui::Selectable(label, ui.category == c && !ui.search[0], 0, ImVec2(0, ImGui::GetFrameHeight() * 1.3f))) {
            ui.category = c; ui.search[0] = 0;
        }
        if (c == Category::partner) {
            const bool up = std::strcmp(s.link_state, "connected") == 0;
            ImGui::SameLine(kRailWidth - 30.0f);
            ImGui::TextColored(up ? kGreen : s.link_state[0] ? kAmber : kGrey, "*");
        }
    }
    if (ImGui::IsWindowFocused() && !ui.search_active) {
        const int at = static_cast<int>(ui.category);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && at > 0) ui.category = static_cast<Category>(at - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && at + 1 < static_cast<int>(act::kCategoryCount)) ui.category = static_cast<Category>(at + 1);
    }
    ImGui::Spacing();
    ImGui::TextColored(kGrey, "Ctrl+F search");
    ImGui::TextColored(kGrey, "Ctrl+1..5 pages");
    ImGui::TextColored(kGrey, "Ctrl+R repeat");
    ImGui::TextColored(kGrey, "Esc / F9 close");
}
void Footer(const Snapshot& s) noexcept {
    const auto line = [](const debug_menu::Receipt& r) noexcept {
        const char* tag = r.status == 0 ? "OK" : r.status == 1 ? "REFUSED" : r.status == 2 ? "PENDING" : "QUEUE";
        const ImVec4 color = r.status == 0 ? kGreen : r.status == 2 ? kAmber : kRed;
        ImGui::TextColored(color, "[%s]", tag);
        ImGui::SameLine();
        ImGui::Text("#%u %s -> %s", r.id, r.command[0] ? r.command : "-", r.result);
        ImGui::SetItemTooltip("%s", r.result);
    };
    if (ImGui::SmallButton(ui.footer_expanded ? "v##footer" : "^##footer")) ui.footer_expanded = !ui.footer_expanded;
    ImGui::SetItemTooltip("Show the latest 6 results");
    ImGui::SameLine();
    if (!s.receipts[0].result[0]) { ImGui::TextColored(kGrey, "Results appear here and next to each button."); return; }
    line(s.receipts[0]);
    if (ui.footer_expanded) for (int i = 1; i < 6; ++i) if (s.receipts[i].result[0]) line(s.receipts[i]);
}
void Keyboard() noexcept {
    if (ui.worker) return;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) ui.focus_search = true;
    const ImGuiKey digits[] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5};
    for (int i = 0; i < 5; ++i) if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | digits[i])) { ui.category = static_cast<Category>(i); ui.search[0] = 0; }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_R) && ui.last_action) Repeat(*ui.last_action, ui.last_command);
}
void AutoRefresh(const Snapshot& s) noexcept {
    if (ui.worker || ui.category != Category::enemies || ui.search[0]) return;
    if (ui.refresh_generation != s.generation) { ui.refresh_generation = s.generation; ui.refresh_tries = 0; ui.camp[0] = 0; }
    const double now = Now();
    if (!s.gameplay_ready || std::strcmp(s.guard_reason, "ok") != 0 || s.enemy_camps_loaded || s.queue_depth ||
        now - ui.refresh_time < 5.0 || ui.refresh_tries >= 3) return;
    ui.refresh_time = now; ++ui.refresh_tries;
    const auto& a = Def("enemy_refresh_camps");
    const std::uint32_t id = debug_menu::QueueCommandId(a.command);
    for (int i = 23; i > 0; --i) ui.tickets[i] = ui.tickets[i - 1];
    ui.tickets[0] = {&a, id, now};
}
} // namespace

bool Draw(const Options& options) noexcept {
    ui.worker = options.worker;
    ui.drawn_count = 0;
    const auto s = debug_menu::GetSnapshot();
    if (ui.confirm.action && Now() >= ui.confirm.expires) ui.confirm = {};
    if (ui.press.active && ++ui.press.frames == 1) { ui.category = ui.press.action->category; ui.search[0] = 0; }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (options.worker) {
        ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Always);
    } else {
        const ImGuiCond cond = ui.reset_layout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        const float w = display.x - 48 < 1040 ? display.x - 48 : 1040, h = display.y - 48 < 620 ? display.y - 48 : 620;
        ImGui::SetNextWindowPos(ImVec2(24, 24), cond);
        ImGui::SetNextWindowSize(ImVec2(w < 640 ? 640 : w, h < 420 ? 420 : h), cond);
        ImGui::SetNextWindowSizeConstraints(ImVec2(640, 420), ImVec2(FLT_MAX, FLT_MAX));
        ui.reset_layout = false;
    }
    bool visible = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("MGS Co-op Debug | F9", &visible, flags)) { ImGui::End(); return visible; }
    bool close = !visible;
    // Esc inside a focused search clears it (the field handles it); otherwise it closes the menu.
    if (!options.worker && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ui.search_active) close = true;
    Keyboard();
    AutoRefresh(s);
    Header(s);
    QuickBar(s);
    ImGui::Separator();
    const float footer = ui.footer_expanded ? 170.0f : ImGui::GetFrameHeightWithSpacing() * 1.5f;
    float body = ImGui::GetContentRegionAvail().y - footer - ImGui::GetStyle().ItemSpacing.y;
    if (body < 80.0f) body = 80.0f;
    if (ImGui::BeginChild("rail", ImVec2(kRailWidth, body), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar)) Rail(s);
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("page", ImVec2(0, body), ImGuiChildFlags_Borders)) {
        // A new page or search starts at the top (a scripted press then scrolls to its row).
        const int key = ui.search[0] ? 100 : static_cast<int>(ui.category);
        if (key != ui.page_key) { ImGui::SetScrollY(0.0f); ui.page_key = key; }
        ui.drawing_page = true;
        if (ui.search[0]) SearchResults(s); else Page(s);
        ui.drawing_page = false;
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("results", ImVec2(0, footer), ImGuiChildFlags_Borders,
        ui.footer_expanded ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) Footer(s);
    ImGui::EndChild();
    ImGui::End();
    if (ui.press.active && ui.press.frames >= 3) NotDrawn();
    return !close;
}
void OnClosed() noexcept { ui.focus_search = false; ui.search_active = false; }
void SelectCategory(Category value) noexcept { ui.category = value; ui.search[0] = 0; }
Category CurrentCategory() noexcept { return ui.category; }
std::size_t SetSearch(const char* text) noexcept {
    Copy(ui.search, text);
    return ui.search[0] ? act::SearchActions(ui.search, nullptr, 0) : 0;
}
const char* Search() noexcept { return ui.search; }
void SetFooterExpanded(bool value) noexcept { ui.footer_expanded = value; }
bool BeginPress(const char* action_id) noexcept {
    const auto* a = act::FindAction(action_id);
    if (!a) return false;
    ui.press = {};
    ui.press.action = a; ui.press.active = true;
    return true;
}
bool TakePressResult(char* out, std::size_t capacity) noexcept {
    if (!ui.press.ready) return false;
    std::snprintf(out, capacity, "%s", ui.press.result);
    ui.press.ready = false;
    return true;
}
void NoteUndrawnFrame() noexcept {
    if (ui.press.active && ++ui.press.frames >= 3) NotDrawn();
}
void Confirm(char* out, std::size_t capacity) noexcept {
    if (!ui.confirm.action) { std::snprintf(out, capacity, "no_confirm_pending"); return; }
    const auto* a = ui.confirm.action;
    const auto id = Send(*a, ui.confirm.command);
    ui.confirm = {};
    std::snprintf(out, capacity, "confirmed;action=%s;queued_id=%u", a->id, id);
}
void Cancel(char* out, std::size_t capacity) noexcept {
    std::snprintf(out, capacity, ui.confirm.action ? "cancelled" : "no_confirm_pending");
    ui.confirm = {};
}
bool Select(std::uint32_t hash) noexcept {
    if (!InNearby(debug_menu::GetSnapshot(), hash)) return false;
    ui.selected = hash;
    return true;
}
bool HasPendingConfirm() noexcept { return ui.confirm.action != nullptr; }
void ResetForTest() noexcept { ui = State{}; }
bool RowDrawnForTest(const char* action_id) noexcept {
    for (std::size_t i = 0; i < ui.drawn_count; ++i) if (std::strcmp(ui.drawn[i]->id, action_id) == 0) return true;
    return false;
}
} // namespace mgscoop::debug_menu_ui
