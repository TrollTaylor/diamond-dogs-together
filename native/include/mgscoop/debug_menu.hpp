#pragma once
#include <cstddef>
#include <cstdint>

// UI exchanges copied data only. The natural Lua runtime thread owns actions.
namespace mgscoop::debug_menu {
inline constexpr std::size_t kCommandCapacity = 201;
inline constexpr std::size_t kQueueCapacity = 16;
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
struct Snapshot {
    bool enabled{}, offline{}, saves_redirected{}, host{}, gameplay_ready{}, terminating{};
    bool realized{}, parts_loaded{}, range_hidden{}, input_armed{}, body_valid{}, pose_valid{};
    bool infinite_ammo{};
    bool world_configured{}, world_paired{}, world_link_up{}, enemy_spawn_valid{};
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
    std::uint32_t mission{}, generation{}, stage{}, world_level{}, bookmark_mask{};
    std::uint32_t rtt_ms{}, jitter_ms{}, loss_permille{}, interp_ms{}, remote_age_ms{};
    std::uint32_t queue_depth{}, last_id{}, completed{}, rejected{};
    std::uint64_t tick_ms{};
    float player_pos[3]{}, remote_pos[3]{}, body_pos[3]{};
    float player_yaw{}, remote_yaw{}, body_yaw{}, heading_error_deg{}, position_error_m{};
    char role[16]{}, link_state[24]{}, source[16]{}, apply[16]{}, anim[16]{}, dm[16]{}, look[16]{};
    char last_command[kCommandCapacity]{}, last_result[192]{}, refusal[80]{};
    char history[8][240]{};
};
bool Enabled() noexcept;
Snapshot GetSnapshot() noexcept;
bool QueueCommand(const char* text) noexcept;
bool QueueCommandForGeneration(const char* text, std::uint32_t mission, std::uint32_t generation) noexcept;
// Configure/lifecycle/publish/pop/results: natural observer thread only.
void Configure(bool enabled, bool host, bool saves_redirected) noexcept;
void BeginMission(std::uint32_t mission) noexcept;
void EndMission() noexcept;
void Publish(const Snapshot& value) noexcept;
bool Pop(Command& value) noexcept;
void Result(std::uint32_t id, bool ok, const char* message) noexcept;
void LuaState(bool infinite_ammo, std::uint32_t bookmark_mask) noexcept;
// Called only by the owner-thread observer after its offline/save/context gate.
bool EnemyReceipt(const char* text, std::size_t size) noexcept;
} // namespace mgscoop::debug_menu
