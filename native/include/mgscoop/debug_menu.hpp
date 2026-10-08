#pragma once
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_facing.hpp"

// UI exchanges copied data only. The natural Lua runtime thread owns actions.
namespace mgscoop::debug_menu {
inline constexpr std::size_t kCommandCapacity = 201;
inline constexpr std::size_t kQueueCapacity = 16;
inline constexpr std::size_t kReceiptCount = 16;
inline constexpr std::size_t kNearbyCount = 12;
struct Command {
    std::uint32_t id{}, generation{};
    char text[kCommandCapacity]{};
};
struct EnemyCamp {
    char name[65]{};
    std::int32_t distance_mm{-1};
    std::uint32_t route_count{};
};
struct EnemyRoute {
    char name[65]{};
    std::uint32_t index{};
};
// Lua wire codes: 0 normal, 2 sleep, 3 faint, 4 dying, 5 dead, 8 disabled.
// Flags: bit0 marked, bit1 ignoring, bit2 placed by spawn_enemy.
struct EnemySoldier {
    char name[65]{};
    std::uint32_t hash{}, state{}, flags{};
    std::int32_t distance_mm{};
};
// Receipt status: 0 ok (final), 1 refused (final), 2 interim, 3 queue refusal (id 0).
struct Receipt {
    std::uint32_t id{};
    std::uint8_t status{};
    char command[64]{};
    char result[161]{};
};
struct Snapshot {
    bool enabled{}, offline{}, saves_redirected{}, host{}, gameplay_ready{}, terminating{};
    bool realized{}, parts_loaded{}, range_hidden{}, input_armed{}, body_valid{}, pose_valid{};
    bool infinite_ammo{};
    bool world_configured{}, world_paired{}, world_link_up{}, enemy_spawn_valid{};
    bool remote_valid{}, enemy_camps_loaded{}, god_mode{}, fulton_infinite{}, slowmo_active{}, overlay_open{};
    std::uint32_t world_effective{}, enemy_camp_count{}, enemy_route_count{}, enemy_spawn_hash{};
    std::uint32_t remote_action_flags{}, remote_equip{}, remote_act{}, mapped_clip{};
    bool local_anim_valid{}, remote_anim_valid{}, local_yaw_valid{};
    std::uint32_t local_act{}, local_anim_stance{}, local_anim_speed{};
    std::uint32_t remote_stance{}, remote_anim_stance{}, remote_anim_speed{}, dm_stance{}, dm_move{};
    bool dm_speed_valid{};
    std::uint32_t dm_speed_mmps{};
    char dm_phase[24]{};
    std::uint32_t motion_count{};
    EnemyCamp enemy_camps[32]{};
    EnemyRoute enemy_routes[64]{};
    char enemy_routes_camp[65]{};
    std::int32_t enemy_spawn_pos_mm[3]{};
    EnemySoldier enemy_nearby[kNearbyCount]{};
    std::uint32_t enemy_nearby_count{}, enemy_nearby_radius{};
    std::uint32_t mission{}, generation{}, stage{}, world_level{}, bookmark_mask{};
    std::uint32_t rtt_ms{}, jitter_ms{}, loss_permille{}, interp_ms{}, remote_age_ms{};
    std::uint32_t queue_depth{}, last_id{}, completed{}, rejected{}, interim{};
    std::uint32_t pending_id{}, pending_frames{}, game_clock_s{};
    std::uint32_t overlay_frames{}, overlay_request_serial{}, overlay_result_serial{};
    std::int32_t player_life{-1}, player_life_max{-1};
    std::uint64_t tick_ms{}, published_tick64{};
    float player_pos[3]{}, remote_pos[3]{}, body_pos[3]{};
    float player_yaw{}, remote_yaw{}, body_yaw{}, heading_error_deg{}, position_error_m{};
    // Receiver facing (partner_facing.hpp): remote_yaw above is the applied
    // yaw; target is the interpolated remote yaw. Degrees; ring oldest first.
    bool facing_active{};
    std::uint32_t facing_limit_dps{}, facing_snaps{};
    char facing_last_snap[16]{};
    float facing_target_yaw{}, facing_applied_yaw{}, facing_rate_dps{};
    partner_facing::Ring facing_ring{};
    char role[16]{}, link_state[24]{}, source[16]{}, apply[16]{}, anim[16]{}, aset[16]{}, dm[16]{}, look[16]{};
    char pending_kind[16]{}, guard_reason[24]{};
    char overlay_category[16]{}, overlay_search[49]{}, overlay_result[96]{}, last_ui_action[40]{};
    char last_command[kCommandCapacity]{}, last_result[192]{}, refusal[80]{};
    Receipt receipts[kReceiptCount]{}; // newest first
};
// Lua-owned state from a STATE_V1 receipt.
struct LuaFlags {
    bool infinite_ammo{}, god_mode{}, fulton_infinite{}, slowmo_active{};
    std::uint32_t bookmark_mask{}, pending_id{}, pending_frames{}, game_clock_s{};
    std::int32_t player_life{-1}, player_life_max{-1};
    char pending_kind[16]{}, guard_reason[24]{};
};
// Overlay-owned fields. The overlay never writes last_command/last_result/receipts.
struct OverlayStatus {
    bool open{};
    std::uint32_t frames{};
    char category[16]{}, search[49]{};
};
bool Enabled() noexcept;
Snapshot GetSnapshot() noexcept;
// Charset/size check shared by the queue, the console and the worker.
bool ValidText(const char* text) noexcept;
bool QueueCommand(const char* text) noexcept;
// Returns the queued id, or 0 when refused (the refusal becomes a status-3 receipt).
std::uint32_t QueueCommandId(const char* text) noexcept;
std::uint32_t QueueCommandForGeneration(const char* text, std::uint32_t mission, std::uint32_t generation) noexcept;
void ClearHistory() noexcept;
void SetOverlayStatus(const OverlayStatus& value) noexcept;
// Worker-only `menu ...` requests: one slot, consumed by the overlay on its render
// thread. Every request gets a serial; refusals (menu_args, menu_busy, menu_stale,
// menu_unavailable;reason=disabled) are reported at once under that serial.
std::uint32_t RequestMenu(const char* text, std::uint32_t mission, std::uint32_t generation) noexcept;
bool TakeMenuRequest(std::uint32_t& serial, char (&text)[kCommandCapacity]) noexcept;
// Final overlay result for a taken request; frees the slot.
void ReportMenuResult(std::uint32_t serial, const char* result) noexcept;
void SetLastUiAction(const char* action) noexcept;
// Configure/lifecycle/publish/pop/results: natural observer thread only.
void Configure(bool enabled, bool host, bool saves_redirected) noexcept;
void BeginMission(std::uint32_t mission) noexcept;
void EndMission() noexcept;
// Copies telemetry; every Lua-, receipt- and overlay-owned field is preserved.
void Publish(const Snapshot& value) noexcept;
bool Pop(Command& value) noexcept;
// code: 0 refused (final), 1 ok (final), 2 ok (interim).
void Result(std::uint32_t id, unsigned code, const char* message) noexcept;
void LuaState(bool infinite_ammo, std::uint32_t bookmark_mask) noexcept;
void LuaState(const LuaFlags& value) noexcept;
// Parses the STATE_V1 fields after the prefix: exactly 2 (legacy) or 12 bounded fields.
bool ParseLuaState(const char* text, std::size_t size, LuaFlags& out) noexcept;
bool LuaStateReceipt(const char* text, std::size_t size) noexcept;
// Called only by the owner-thread observer after its offline/save/context gate.
bool EnemyReceipt(const char* text, std::size_t size) noexcept;
} // namespace mgscoop::debug_menu
