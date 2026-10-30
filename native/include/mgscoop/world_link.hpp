#pragma once
// World sync glue (docs/world-sync-protocol.md): one world::Core behind a leaf
// SRW lock, the partner_net world hooks (receive thread), the reliable pump,
// the lab world.trigger and the receipt handlers the script observer calls.
//
// Threads and locks: receipts arrive on the game's Lua threads inside the
// observer's receipt lock; the hooks run on the link's receive thread with no
// partner_net lock held. g_world_lock is a leaf: nothing calls partner_net,
// coop_hud, logs or touches a file while holding it; the core's log lines,
// partner_net::PublishWorldState and coop_hud::PostWorldNote run after it is
// released. Lock order: receipt lock -> (world lock, released) ->
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
    // WS4 step 0: lab key partner_placed_census (lab mode only): CFG r[14] = 1
    // asks the Lua observer for its event=obs_att attacker census lines.
    bool placed_census = false;
    // 0.13.6 online-vehicle-gone: lab.ini world_vehicle_gone (lab and play): CFG
    // r[15] = 1 turns on the Lua VEHICLE_GONE module (lua/coop/world_vehicle.lua).
    bool vehicle_gone = false;
    // M1 host-owned companion: lab.ini companion_host_owned (lab and play): CFG r[16] = 1
    // turns on the Lua companion module (lua/coop/world_companion.lua).
    bool companion = false;
    // M3 sortie vehicle: lab.ini world_relief (lab and play): CFG r[17] = 1 turns on the Lua
    // relief module (lua/coop/world_relief_sync.lua).
    bool relief = false;
    // M4 (docs/vehicles-companions-design.md 6.4): lab.ini world_vehicle_state (lab
    // and play): CFG r[15] bit 2 turns on lua/coop/world_vehicle_state.lua.
    bool vehicle_state = false;
    // M4 review: lab.ini world_vehicle_wreck (lab and play): CFG r[15] bit 4, VEHICLE_GONE's
    // wreck parity for a destroyed vehicle (else a Despawn).
    bool vehicle_wreck = false;
    // The user's vehicle decisions of 2026-10-29 (lua/coop/world_vehicle_rules.lua), lab and play:
    // CFG r[15] bit 8 world_vehicle_break (D12), bit 16 world_vehicle_credit (D14), bit 32
    // world_loadout_block (D11).
    bool vehicle_break = false;
    bool vehicle_credit = false;
    bool loadout_block = false;
};

// "0.12.0" -> 1200 (major*10000 + minor*100 + patch); 0 when malformed.
std::uint32_t ParseBuild(const char* version) noexcept;
// Install time (before partner_net::Start). Logs world=configured.
void Configure(const Config& config) noexcept;
bool Configured() noexcept;
// Copied status under the world leaf lock, without game/network calls.
// linked .. remote_location are world::Core's observers; core_wait is the raw
// pair-wait code (world::pair_wait), pair_wait the published one (11 settling
// until a different-missions pair lasted 15 s); progress 0 unknown, 1 ok,
// 2 story_start and title_mission come from the last PROGRESS note. Not
// configured: {} (pair_wait 1 off).
struct Status {
    bool configured{}, paired{}, link_up{};
    std::uint8_t level{}, effective{};
    std::uint16_t epoch{};
    bool linked{};
    std::uint8_t remote_level{}, core_wait{1}, pair_wait{1}, progress{};
    std::uint32_t local_mission{}, remote_mission{}, remote_location{}, title_mission{};
};
Status GetStatus() noexcept;
// The receive-thread hooks for partner_net::SetWorldHooks.
const partner_net::WorldHooks* Hooks() noexcept;

// Receipt handlers (under the observer's receipt lock; the observer checks the
// thread first). nullptr leaves the receipt literal. HandleCfg fills the
// reply's r[12..13] from save_handoff::LuaCode and LockAllowed (P2-1).
const char* HandleCfg(const char* source, std::size_t size, std::uint32_t read_mission, bool terminating) noexcept;
const char* HandleTx(const char* source, std::size_t size) noexcept;
const char* HandleRx(const char* source, std::size_t size, std::uint32_t read_mission, bool terminating) noexcept;
const char* HandleLog(const char* source, std::size_t size) noexcept;
// NOTE_V1 (runtime or termination thread only; the observer checks): rate
// gated (world::NoteGate), logs world=note, posts the known codes 1..12 as a
// coop_hud world note after releasing the world lock (code 1 records the
// title progress and posts only for a story start, and not while
// save_handoff::GuestHandoffActive). "return true" for codes 1..12 (5..12:
// join-fix phase 2 P2-3), "return false" for 13..15, nullptr when malformed or
// not configured.
const char* HandleNote(const char* source, std::size_t size) noexcept;

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
    std::uint32_t serial{};  // world::Grant::serial (1..0x7FFFFFFF, process-unique)
};
WarpGrant FindGrant(std::uint16_t seq) noexcept;

// Observer boundaries: the runtime entry (next to PartnerMissionStart) and the
// returned termination (next to PartnerMissionEnd).
void MissionRuntime(std::uint32_t mission) noexcept;
void LocalMissionEnd() noexcept;

} // namespace mgscoop::world_link
