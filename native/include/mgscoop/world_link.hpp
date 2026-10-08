#pragma once
// World sync glue (docs/world-sync-protocol.md): one world::Core behind a leaf
// SRW lock, the partner_net world hooks (receive thread), the reliable pump,
// the lab world.trigger and the receipt handlers the script observer calls.
//
// Threads and locks: receipts arrive on the game's Lua threads inside the
// observer's receipt lock; the hooks run on the link's receive thread with no
// partner_net lock held. g_world_lock is a leaf: nothing calls partner_net,
// logs or touches a file while holding it; the core's log lines are written
// after it is released. Lock order: receipt lock -> (world lock, released) ->
// partner_net g_tx_lock -> g_lock. Inert until Configure (world=0 never calls it).
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_net.hpp"

namespace mgscoop::world_link {

using LogFn = void (*)(const char* line) noexcept;

struct Config {
    std::uint8_t role;           // 1 host, 2 guest
    std::uint8_t level;          // lab.ini world (1..3 when configured)
    bool play;                   // mode=play
    WorldProbe probe;            // lab.ini world_probe (lab only)
    const wchar_t* directory;    // game folder (<dir>\mgs-coop\world.trigger)
    bool lab_triggers;           // lab mode with the lab paths: world.trigger honoured
    std::uint8_t saves;          // the save gate held (always 1 here)
    std::uint32_t build;         // ParseBuild(lab::ProbeVersion())
    LogFn log;
};

// "0.11.1" -> 1101 (major*10000 + minor*100 + patch); 0 when malformed.
std::uint32_t ParseBuild(const char* version) noexcept;
// Install time (before partner_net::Start). Logs world=configured.
void Configure(const Config& config) noexcept;
bool Configured() noexcept;
// Copied status under the world leaf lock, without game/network calls.
struct Status { bool configured{}, paired{}, link_up{}; std::uint8_t level{}, effective{}; std::uint16_t epoch{}; };
Status GetStatus() noexcept;
// The receive-thread hooks for partner_net::SetWorldHooks.
const partner_net::WorldHooks* Hooks() noexcept;

// Receipt handlers (under the observer's receipt lock; the observer checks the
// thread first). nullptr leaves the receipt literal.
const char* HandleCfg(const char* source, std::size_t size, std::uint32_t read_mission, bool terminating) noexcept;
const char* HandleTx(const char* source, std::size_t size) noexcept;
const char* HandleRx(const char* source, std::size_t size, std::uint32_t read_mission, bool terminating) noexcept;
const char* HandleLog(const char* source, std::size_t size) noexcept;

// Guest spawn sync: the grant world::Core recorded for ENEMY_DEBUG `seq` in the
// current paired epoch (copied under the world leaf lock; found=false otherwise).
struct WarpGrant {
    bool found{};
    std::uint16_t seq{}, epoch{};
    std::uint8_t kind{}, flags{};
    std::uint32_t ent{};
    float pos[3]{};      // metres
    float yaw{};         // radians
    std::uint64_t age_ms{};
};
WarpGrant FindGrant(std::uint16_t seq) noexcept;

// Observer boundaries: the runtime entry (next to PartnerMissionStart) and the
// returned termination (next to PartnerMissionEnd).
void MissionRuntime(std::uint32_t mission) noexcept;
void LocalMissionEnd() noexcept;

} // namespace mgscoop::world_link
