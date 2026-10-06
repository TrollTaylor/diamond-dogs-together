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
struct Snapshot {
    bool enabled{}, offline{}, saves_redirected{}, host{}, gameplay_ready{}, terminating{};
    bool realized{}, parts_loaded{}, range_hidden{}, input_armed{}, body_valid{}, pose_valid{};
    bool infinite_ammo{};
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
// Configure/lifecycle/publish/pop/results: natural observer thread only.
void Configure(bool enabled, bool host, bool saves_redirected) noexcept;
void BeginMission(std::uint32_t mission) noexcept;
void EndMission() noexcept;
void Publish(const Snapshot& value) noexcept;
bool Pop(Command& value) noexcept;
void Result(std::uint32_t id, bool ok, const char* message) noexcept;
void LuaState(bool infinite_ammo, std::uint32_t bookmark_mask) noexcept;
} // namespace mgscoop::debug_menu
