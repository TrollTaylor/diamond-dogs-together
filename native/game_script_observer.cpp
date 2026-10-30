// Project-authored adapter. Boundary provenance: docs/script-load-bridge.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <atomic>
#include "mgscoop/game_script_observer.hpp"
#include "mgscoop/script_load_bridge.hpp"
#include "mgscoop/bootstrap_patch.hpp"
#include "mgscoop/archive_mount.hpp"
#include "mgscoop/host_session_trial.hpp"
#include "mgscoop/dynamite_session_port.hpp"
#include "mgscoop/client_session_request.hpp"
#include "mgscoop/player_team_patch.hpp"
#include "mgscoop/damage_patch.hpp"
#include "mgscoop/downed_player.hpp"
#include "mgscoop/coop_carry.hpp"
#include "coop_passenger.hpp"
#include "coop_vehicle_mirror.hpp"
#include "vehicle_root_wiring.hpp"
#include "horse_player_physical_read.hpp"
#include "horse_cargo_native_read.hpp"
#include "horse_cargo_pair_read.hpp"
#include "horse_cargo_flow_probe.hpp"
#include "prologue_provider_read.hpp"
#include "mgscoop/native_pose_dual_parts.hpp"
#include "horse_physical_profile_read.hpp"
#include "horse_physical_probe_glue.hpp"
#include "mgscoop/coop_sleep.hpp"
#include "coop_team_wake.hpp"
#include "mgscoop/carry_info.hpp"
#include "mgscoop/sleep_info.hpp"
#include "mgscoop/sleep_partner_test.hpp"
#include "mgscoop/native_pose_local_carry_slice.hpp"
#include "mgscoop/native_pose_sleep_parts.hpp"
#include "mgscoop/native_pose_carry_cold.hpp"
#include "sleep_partner_bridge.hpp"
#include "sleep_motion_asset_read.hpp"
#include "sleep_standing_cycle_asset.hpp"
#include "sleep_unique_bind_runtime.hpp"
#include "qualified_sleep_lookup_runtime.hpp"
#include "native_motion_frame_runtime.hpp"
#include "native_work_timing.hpp"
#include <bcrypt.h>
#include "sleep_deferred_carry_proof.hpp"
#include "sleep_queued_carry_proof.hpp"
#include "sleep_seed_contract.hpp"
#include "sleep_body0_transition_contract.hpp"
#include "mgscoop/vehicle_info.hpp"
#include "mgscoop/downed_damage.hpp"
#include "mgscoop/buddy_pose.hpp"
#include "mgscoop/buddy_dog_command_pins.hpp"
#include "mgscoop/dog_guard.hpp"
#include "mgscoop/buddy_capacity_census.hpp"
#include "mgscoop/buddy_capacity_walker.hpp"
#include "mgscoop/buddy_capacity_pins.hpp"
#include "mgscoop/buddy_ride_read.hpp"
#include "mgscoop/ride_player_observer.hpp"
#include "mgscoop/ride_player_contract.hpp"
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/teammate_targeting.hpp"
#include "mgscoop/enemy_targeting.hpp"
#include "mgscoop/respawn_patch.hpp"
#include "mgscoop/result_patch.hpp"
#include "mgscoop/full_close_patch.hpp"
#include "mgscoop/marker_loading_patch.hpp"
#include "mgscoop/character_receipt.hpp"
#include "mgscoop/player_type_patch.hpp"
#include "mgscoop/soldier_patch.hpp"
#include "mgscoop/mission_termination.hpp"
#include "mgscoop/mission_initialization.hpp"
#include "mgscoop/equipment_allocation_probe.hpp"
#include "mgscoop/equipment_stock_probe.hpp"
#include "mgscoop/equipment_stock_contract.hpp"
#include "mgscoop/equipment_stock_writer.hpp"
#include "mgscoop/equipment_preload_contract.hpp"
#include "equipment_observation_source.hpp"
#include "mgscoop/lab_config.hpp"
#include "mgscoop/lab_boot.hpp"
#include "mgscoop/lab_status.hpp"
#include "mgscoop/client_reset.hpp"
#include "mgscoop/partner_receipt.hpp"
#include "mgscoop/partner_player.hpp"
#include "mgscoop/partner_missions.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/partner_action_events.hpp"
#include "mgscoop/partner_driver.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/native_pose_window.hpp"
#include "mgscoop/partner_defer.hpp"
#include "mgscoop/partner_local_detour.hpp"
#include "mgscoop/partner_icon.hpp"
#include "mgscoop/partner_ui_filter.hpp"
#include "mgscoop/partner_look_source.hpp"
#include "mgscoop/partner_aim_point.hpp"
#include "mgscoop/partner_gesture_lever.hpp"
#include "mgscoop/partner_fire_filter.hpp"
#include "mgscoop/partner_support_equip.hpp"
#include "mgscoop/partner_cbox_equip.hpp"
#include "mgscoop/partner_support_guard.hpp"
#include "mgscoop/partner_support_policy.hpp"
#include "mgscoop/partner_placed_census.hpp"
#include "mgscoop/partner_seat_aim.hpp"
#include "mgscoop/partner_dm_patch.hpp"
#include "mgscoop/partner_ride_seat.hpp"
#include "mgscoop/partner_vehicle.hpp"
#include "mgscoop/partner_board.hpp"
#include "mgscoop/vehicle_door.hpp"
#include "mgscoop/partner_pass_camera.hpp"
#include "mgscoop/partner_upper_probe.hpp"
#include "mgscoop/partner_upper_end_observer.hpp"
#include "mgscoop/partner_upper_phase_observer.hpp"
#include "mgscoop/partner_upper_manual_idle.hpp"
#include "mgscoop/crash_probe.hpp"
#include "mgscoop/debug_overlay.hpp"
#include "mgscoop/early_patches.hpp"
#include "mgscoop/world_link.hpp"
#include "mgscoop/save_handoff.hpp"
#include "mgscoop/save_redirect.hpp"
#include "mgscoop/world_receipt.hpp"
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/soldier_warp.hpp"
#include "mgscoop/worker_debug.hpp"
#include "mgscoop/worker_scenario.hpp"
#include "mgscoop/nametag.hpp"
#include "mgscoop/nametag_game.hpp"
#include "mgscoop/steam_observer.hpp"
#include "mgscoop/steam_net_callbacks.hpp"
#include "mgscoop/coop_hud.hpp"
#include "mgscoop/steam_online.hpp"
#include <charconv>
#include <cmath>
#include <cstdarg>

namespace {
namespace lab = mgscoop::lab;
HANDLE script_log = INVALID_HANDLE_VALUE;
volatile LONG64 entries = 0, returns = 0;
bool bootstrap_enabled = false;
bool archive_mounted = false;
wchar_t unique_bind_directory[32768]{};
bool allocation_enabled = false;
bool host_enabled = false;
bool heap_test_enabled = false;
bool host_create_enabled = false;
bool host_admission_enabled = false;
bool client_enabled = false;
bool client_check_enabled = false;
// offline-v1: host-v1 preparation (allocation, archive, TppMain splice,
// mission boundary, runtime pinning, two-slot package) with no session at all.
bool offline_enabled = false;
bool downed_hook_ok = false;
bool buddy_pose_pins_ok = false;
bool dog_guard_ok = false;
bool buddy_capacity_pins_ok = false;
bool ride_player_pins_ok = false;
bool buddy_ride_pins_ok = false;
void ClearBuddyCapacityIdentity() noexcept;
mgscoop::buddy_pose::Scope buddy_pose_owner{};
bool client_join_armed = false; // serialized mission receipts only
bool client_key_released = false;
bool client_join_consumed = false; // process lifetime: no automatic retries
bool mission_terminating = false;
bool gameplay_restore_pending = false; // retained across mission boundaries
std::uint64_t client_host = 0;
// A source compilation thread is not an execution owner. OnAllocate can run
// on a worker, then OnInitialize/updates run on the main thread. Serialize only
// native selectors: never hold this lock across original Lua calls or yields.
SRWLOCK receipt_lock = SRWLOCK_INIT;
bool host_source_selected = false;
enum class MissionPhase { idle, allocating, allocated, running };
MissionPhase mission_phase = MissionPhase::idle;
std::uint32_t worker_loading_generation = 0; // No wrap: exhaustion disables resume.
bool worker_loading_exhausted = false;
DWORD allocation_thread = 0, mission_thread = 0;
DWORD cleanup_thread = 0;
mgscoop::MissionTermination termination_scope;
mgscoop::MissionInitialization initialization_scope;
mgscoop::equipment_allocation_probe::Authorization equipment_allocation_authorization;
mgscoop::equipment_stock_probe::Authorization stock46_authorization;
mgscoop::equipment_stock_contract::Snapshot stock46_before;
bool stock46_groups_authorized=false,stock46_groups_returned=false;
// The generalized preload (W2; docs/native-puppet.md "Generalized preload").
mgscoop::equipment_stock_probe::GeneralAuthorization equip_preload_authorization;
mgscoop::equipment_preload_contract::Snapshot equip_preload_before;
mgscoop::equipment_preload_contract::Snapshot equip_preload_after;
std::uint64_t equipment_observation_requested_ms = 0;
unsigned short equipment_observation_mission = 0;
unsigned short allocation_mission = 0;
void* game_base = nullptr;
std::size_t game_span = 0;
// Lab observability (docs/lab-protocol.md). All of this is inert unless lab.ini
// is present and enabled; with no lab.ini the loader behaves exactly as before.
bool lab_present = false;
bool lab_active = false;                    // lab_present, the log is open and the paths fit
// <exeDir>\mgs-coop\<name>, built once at install. Sized so "<path>.tmp" fits
// the atomic writer's bound; static so no hot path needs a large stack frame.
wchar_t lab_status_path[lab::kMaxAtomicPath]{};
wchar_t join_trigger_path[lab::kMaxAtomicPath]{};
wchar_t reset_trigger_path[lab::kMaxAtomicPath]{};
wchar_t host_steamid_path[lab::kMaxAtomicPath]{};
lab::Status lab_status{};
lab::PublishTracker lab_publish{};
lab::SessionStatus logged_session = lab::SessionStatus::none;
bool session_status_logged = false;
mgscoop::ClientResetMachine reset_machine{};
mgscoop::ClientAutoResetWatch auto_reset_watch{}; // lab only
bool reset_key_released = false;            // Ctrl+F9 release latch (edge-triggered)
unsigned join_attempts = 0;                 // consumed join triggers, any mode
std::uint64_t armed_tick_ms = 0;            // when the guest became armed (for auto join)
bool auto_join_fired = false;
bool host_steamid_pending = false;          // lab host created; file not yet written
bool host_steamid_failure_logged = false;
lab::RetryThrottle host_steamid_retry{};
constexpr std::size_t load_rva = 0x1a178e0;
constexpr unsigned char expected_body[] = {
    0x48,0x83,0xec,0x38,0x48,0x89,0x54,0x24,0x20,0x4c,0x89,0x44,0x24,0x28,
    0x4c,0x8d,0x44,0x24,0x20,0x48,0x8d,0x15,0xa6,0xf2,0xff,0xff,
    0xe8,0x01,0x9c,0xff,0xff,0x48,0x83,0xc4,0x38,0xc3};

void Log(const char* line) noexcept {
    DWORD written = 0;
    if (script_log != INVALID_HANDLE_VALUE)
        WriteFile(script_log, line, static_cast<DWORD>(lstrlenA(line)), &written, nullptr);
}
bool Sample(LONG64 count) noexcept { return count <= 32 || count == 256 || count == 1024 || count == 4096; }
void LogFormat(const char* format, ...) noexcept {
    char line[512]{};
    va_list arguments;
    va_start(arguments, format);
    const HRESULT result = StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    if (SUCCEEDED(result)) Log(line);
}

// --- Lab observability helpers (all no-ops unless lab_active) -----------------

bool ReadMission(unsigned short& mission) noexcept;  // defined below
bool DownedAdmitted(std::uint32_t mission,std::uint16_t epoch) noexcept;

// Builds <directory>\mgs-coop\<name> so that "<path>.tmp" still fits the atomic
// writer's bound. Install time only.
bool BuildCoopPath(const wchar_t* directory, const wchar_t* name, wchar_t (&out)[lab::kMaxAtomicPath]) noexcept {
    out[0] = 0;
    return SUCCEEDED(StringCchPrintfW(out, lab::kMaxAtomicPath - 4, L"%s\\mgs-coop\\%s", directory, name));
}
// Rewrites lab-status.json when a field other than the timestamp differs from
// the last successful write. A failed write is retried on a later poll (at
// most once per second) instead of being treated as written.
void SyncLabStatus() noexcept {
    if (!lab_active) return;
    const std::uint64_t now = GetTickCount64();
    if (!lab_publish.NeedsWrite(lab_status, now)) return;
    const DWORD error = GetLastError();
    lab_status.updated_tick_ms = now;
    char json[lab::kStatusJsonCapacity]{};
    std::size_t length = 0;
    const bool written = lab::FormatStatusJson(lab_status, json, sizeof(json), length) &&
                         lab::WriteFileAtomic(lab_status_path, json, length);
    lab_publish.Record(lab_status, written, now);
    SetLastError(error);
}
// Logs the session_status line on change and refreshes lab-status.json.
void ApplySessionStatus(lab::SessionStatus mapped) noexcept {
    if (!lab_active) return;
    if (!session_status_logged || mapped != logged_session) {
        char line[64]{};
        if (lab::FormatSessionStatusLine(mapped, line, sizeof(line))) Log(line);
        logged_session = mapped;
        session_status_logged = true;
    }
    unsigned short mission = 0;
    if (ReadMission(mission)) lab_status.mission = mission;
    lab_status.session = mapped;
    lab_status.members = lab::MembersFor(mapped);
    lab_status.join = lab::JoinAfterSession(lab_status.join, mapped);
    SyncLabStatus();
}
// After the game released the owned session (observed through the singleton
// slots only), report it as closed.
void ApplySessionReleased() noexcept {
    if (lab_active && session_status_logged) ApplySessionStatus(lab::SessionAfterRelease(logged_session));
}
void WriteHostSteamIdFile() noexcept;  // defined below
// Maps a ReadDynamiteSessionStatus literal to the protocol vocabulary.
// 'unavailable' keeps the previous status, unless the game has released the
// owned session, which is reported as closed.
void UpdateSessionStatus(const char* literal) noexcept {
    if (!lab_active) return;
    lab::SessionStatus mapped = logged_session;
    if (lab::SessionStatusFromLiteral(literal, mapped)) ApplySessionStatus(mapped);
    else if (OwnedSessionReleasedByGame(game_base, game_span)) ApplySessionReleased();
    else SyncLabStatus();  // retries a failed write
    WriteHostSteamIdFile(); // retries until the host identity file is written
}
void SetLabJoin(lab::JoinStatus join) noexcept {
    if (!lab_active) return;
    lab_status.join = join;
    SyncLabStatus();
}
// Records a refusal reason; `join` is the resulting join status.
void SetLabRefusal(const char* reason, lab::JoinStatus join) noexcept {
    if (!lab_active) return;
    lab::SetRefusal(lab_status, reason);
    lab_status.join = join;
    SyncLabStatus();
}
// Records a refusal reason without changing the join status.
void SetLabLastRefusal(const char* reason) noexcept {
    if (!lab_active) return;
    lab::SetRefusal(lab_status, reason);
    SyncLabStatus();
}
bool ConsumeTriggerFile(const wchar_t* path) noexcept {
    if (!lab_active || !path[0]) return false;
    const DWORD error = GetLastError();
    bool consumed = false;
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        consumed = DeleteFileW(path) != 0;
    SetLastError(error);
    return consumed;
}
// Reads the local SteamID64 through the steam_api64 SteamUser() accessor and
// ISteamUser vtable slot 2 (GetSteamID), which under MSVC x64 returns CSteamID
// through a hidden return pointer. Returns 0 on any failure. Never logs the id.
std::uint64_t ReadLocalSteamId() noexcept {
    const DWORD error = GetLastError();
    std::uint64_t id = 0;
    const HMODULE steam = GetModuleHandleW(L"steam_api64.dll");
    if (steam) {
        using SteamUserFn = void* (*)();
        const auto SteamUser = reinterpret_cast<SteamUserFn>(
            reinterpret_cast<void*>(GetProcAddress(steam, "SteamUser")));
        if (SteamUser) {
            void* iface = nullptr;
            __try { iface = SteamUser(); } __except (EXCEPTION_EXECUTE_HANDLER) { iface = nullptr; }
            if (iface) {
                __try {
                    void** vtable = *reinterpret_cast<void***>(iface);
                    using GetSteamIdFn = std::uint64_t* (*)(void*, std::uint64_t*);
                    const auto GetSteamID = reinterpret_cast<GetSteamIdFn>(vtable[2]);
                    std::uint64_t out = 0;
                    GetSteamID(iface, &out);
                    id = out;
                } __except (EXCEPTION_EXECUTE_HANDLER) { id = 0; }
            }
        }
    }
    SetLastError(error);
    return id;
}
// Writes host-steamid.txt once the lab host session exists. A failed read or
// write is retried from later host status polls (at most once per second);
// the first failure is logged once. The id itself is never logged.
void WriteHostSteamIdFile() noexcept {
    if (!lab_active || !host_steamid_pending) return;
    const std::uint64_t now = GetTickCount64();
    if (!host_steamid_retry.Due(now)) return;
    const std::uint64_t id = ReadLocalSteamId();
    const bool valid = mgscoop::IsClientHostIdentity(id);
    char line[32]{};
    std::size_t length = 0;
    const bool written = valid && lab::FormatSteamIdLine(id, line, sizeof(line), length) &&
                         lab::WriteFileAtomic(host_steamid_path, line, length);
    if (written) {
        host_steamid_pending = false;
        host_steamid_retry.Succeeded();
        Log("host_identity=written; host_steamid_txt=1\r\n");
        return;
    }
    host_steamid_retry.Failed(now);
    if (!host_steamid_failure_logged) {
        host_steamid_failure_logged = true;
        Log(valid ? "host_identity=write_failed; host_steamid_txt=0; retry=1\r\n"
                  : "host_identity=unavailable; host_steamid_txt=0; retry=1\r\n");
    }
}

bool RestoreGameplayRules() noexcept {
    mgscoop::marker_loading::End();
    client_join_armed=false;
    client_key_released=false;
    // Co-op rules are ending (loss, termination or failure): no automatic
    // reset may carry a watched request into a later scene.
    auto_reset_watch.Stop();
    // Attempt every restoration even when an earlier one fails. These calls
    // restore only this adapter's owned code; they never retry CloseSession.
    const bool team=mgscoop::player_team::SetActive(false);
    const bool respawn=mgscoop::respawn_patch::SetActive(game_base,game_span,false,Log);
    const bool damage=mgscoop::damage::SetActive(game_base,game_span,false,Log);
    const bool close=mgscoop::full_close_patch::Deactivate();
    gameplay_restore_pending=!(team && respawn && damage && close);
    char line[224]{};
    StringCchPrintfA(line,sizeof(line),"gameplay_cleanup=attempted; team_restored=%u respawn_restored=%u damage_restored=%u close_branch_restored=%u retry_pending=%u\r\n",
        unsigned(team),unsigned(respawn),unsigned(damage),unsigned(close),unsigned(gameplay_restore_pending));
    Log(line);
    return !gameplay_restore_pending;
}

bool OnAllocationThread() noexcept {
    return mission_phase==MissionPhase::allocating && allocation_thread==GetCurrentThreadId();
}
bool OnRuntimeThread() noexcept {
    return mission_phase==MissionPhase::running && !termination_scope.Active() && mission_thread==GetCurrentThreadId();
}
bool OnTerminationThread() noexcept { return termination_scope.OnThread(GetCurrentThreadId()); }
bool OnMissionThread() noexcept { return OnAllocationThread() || OnRuntimeThread() || OnTerminationThread(); }

// Character selection receipts (lua/coop/character.lua, docs/characters.md).
// The query is read-only and formatted once from coop.ini (constant for the
// process). The Ocelot/Quiet reset bypass is enabled only for a configured
// Ocelot/Quiet during an accepted allocation (an already active bypass is
// confirmed, e.g. for a checkpoint reload); disabling is always allowed.
// The players guard (lab only) samples the player object read-only so the
// readback calls Player.GetPlayerType, which has no null check, only while
// that object exists. Report/readback receipts are log-only, never replaced.
char character_query[mgscoop::character::max_line]{};
std::size_t character_query_size = 0;
thread_local char character_players[64]{};
bool SelectCharacterReceipt(mgscoop::character::Receipt kind, const char* buffer, std::size_t size,
                            mgscoop::script_bridge::Replacement& replacement) noexcept {
    using mgscoop::character::Receipt;
    static constexpr char yes[]="return true", no[]="return false";
    const auto& coop=mgscoop::GetCoopConfig();
    if(kind==Receipt::query) {
        if(!character_query_size)
            character_query_size=mgscoop::character::FormatQuery(coop,mgscoop::GetLabConfig().present,
                                                                  character_query,sizeof(character_query));
        if(!character_query_size) return false;
        replacement={character_query,character_query_size};
        return true;
    }
    if(kind==Receipt::bypass_on || kind==Receipt::bypass_off) {
        bool accepted=false;
        if(kind==Receipt::bypass_off) accepted=mgscoop::player_type::SetActive(game_base,game_span,false,Log);
        else if(!mgscoop::character::NeedsResetBypass(coop) ||
                (!OnAllocationThread() && !mgscoop::player_type::IsActive()))
            Log("player_type_patch=refused; reason=not_configured_or_not_allocating\r\n");
        else accepted=mgscoop::player_type::SetActive(game_base,game_span,true,Log);
        replacement=accepted ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                             : mgscoop::script_bridge::Replacement{no,sizeof(no)-1};
        return true;
    }
    if(kind==Receipt::players) {
        mgscoop::player_type::TypeSample sample{"lab_disabled",0,0,{0,0}};
        const bool ready=mgscoop::GetLabConfig().present && mgscoop::player_type::SampleTypes(game_base,game_span,sample);
        const auto length=mgscoop::character::FormatPlayersReply(ready,sample.stage,sample.limit,sample.type,sample.count,
                                                                  character_players,sizeof(character_players));
        if(!length) return false;
        replacement={character_players,length};
        return true;
    }
    char line[mgscoop::character::max_line]{};
    if(mgscoop::character::FormatLogLine(kind,buffer,size,line,sizeof(line))) Log(line);
    return false;
}
bool TransferRuleThread(DWORD previous_thread) noexcept {
    // Both directions are needed: allocation -> runtime for gameplay/cleanup,
    // then runtime -> a later allocation worker for retained cleanup. These
    // calls change only adapter metadata after the prior callback returned.
    const bool targeting=mgscoop::enemy_targeting::AdoptMissionThread(previous_thread);
    const bool damage=mgscoop::damage::AdoptMissionThread(previous_thread);
    const bool soldier=mgscoop::soldier_patch::AdoptMissionThread(previous_thread);
    const bool respawn=mgscoop::respawn_patch::AdoptMissionThread(previous_thread);
    const bool close=mgscoop::full_close_patch::AdoptMissionThread(previous_thread);
    char line[256]{};
    StringCchPrintfA(line,sizeof(line),"rule_thread_handoff=attempted; from=%lu to=%lu targeting=%u damage=%u soldier=%u respawn=%u close=%u\r\n",
        previous_thread,GetCurrentThreadId(),unsigned(targeting),unsigned(damage),unsigned(soldier),unsigned(respawn),unsigned(close));
    Log(line);
    return targeting && damage && soldier && respawn && close;
}
bool ReadMission(unsigned short& mission) noexcept {
    constexpr std::size_t rva=0x2c01372;
    SIZE_T read=0;
    return game_base && rva<=game_span && sizeof(mission)<=game_span-rva &&
        ReadProcessMemory(GetCurrentProcess(),static_cast<unsigned char*>(game_base)+rva,
            &mission,sizeof(mission),&read) && read==sizeof(mission);
}

// --- Offline partner spike (docs/offline-partner-spike-plan.md) ---------------
// Everything below is inert unless the offline-v1 marker is active and lab.ini
// has partner=1 (partner_on). It never creates a session, never sets
// S_IS_ONLINE and never touches the synchronizer; the only game writes are the
// guarded ones in partner_player. Receipts arrive inside ObserveEntry under
// receipt_lock; the tick receipt is quiet (no entry/return log lines). The
// decisions live in partner_driver (unit tested); this file supplies threading,
// the receipt plumbing and the real effects below.
namespace partner = mgscoop::partner;
namespace pp = mgscoop::partner_player;
namespace pd = mgscoop::partner_driver;
bool partner_on = false;
bool partner_gate_ok = false;
// Set once when the game window closes (PartnerShutdown), in ANY mission: the
// title and the ACC too, where partner_defer::Shutdown (partner missions only)
// never runs. The early manifest stops with it (ObserveEquipLink, and no
// loadout read for the driver tick's sender either). Never cleared.
volatile LONG partner_window_closing = 0;
pp::Addresses partner_addresses{};
wchar_t partner_trigger_path[lab::kMaxAtomicPath]{};
// Only an exact standalone trigger creates one request; mapper refreshes cannot.
bool upper_idle_requested = false;
std::uint64_t upper_idle_nonce = 0;
thread_local pd::Quiet partner_quiet{};
thread_local char partner_reply[partner::reply_capacity]{};
pd::Driver partner_driver{};

std::uint64_t QpcMicros() noexcept {
    LARGE_INTEGER frequency{}, counter{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 || !QueryPerformanceCounter(&counter)) return 0;
    const auto f = static_cast<std::uint64_t>(frequency.QuadPart);
    const auto c = static_cast<std::uint64_t>(counter.QuadPart);
    return (c / f) * 1000000 + ((c % f) * 1000000) / f;
}
std::uint32_t Clamp32(std::uint64_t value) noexcept { return value > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(value); }

// Real effects for partner_driver (the context is unused: process-level state).
namespace psleep=mgscoop::sleep_partner_test;
namespace psbridge=mgscoop::sleep_partner_bridge;
psbridge::ArmOwner partner_sleep_arm{};
psbridge::Ledger partner_sleep_records{};
bool partner_sleep_configured=false;
std::atomic<bool> vehicle_pose_observer{false},vehicle_wiring_prepared{false},vehicle_wiring_enabled{false};
bool VehicleMirrorPrepare() noexcept;
bool VehicleMirrorEnable(bool) noexcept;
bool PartnerDrainReliableOrigin(void*,std::uint8_t,std::uint8_t*,std::size_t,std::size_t&,mgscoop::partner_net::ReliableOrigin&) noexcept;
bool PartnerReceiveVehicle(void*,const mgscoop::partner_net::ReliableOrigin&,const std::uint8_t*,std::size_t) noexcept;
bool PartnerVehicleAttached(void*,const pp::State&) noexcept;
bool PartnerVehicleJob(void*,const pp::State&,const mgscoop::partner_input::Arm&) noexcept;
bool PartnerSleepPrivate() noexcept {
 return mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&lab::SavesRedirected();
}
bool PartnerSleepEnableAllowed() noexcept;
bool PartnerSleepSuppressed(void*) noexcept {return psleep::SuppressForcedStance();}
void PartnerSleepResetBridge() noexcept {
 partner_sleep_arm={};partner_sleep_records={};
 psleep::Enable(false); // queues retained wake; never discards native lease
 psleep::PublishTarget({});
}
bool PartnerSleepBefore(const mgscoop::partner_input_hook::NativePoseJobContext& c) noexcept {
 return psleep::Before(c)&&mgscoop::coop_vehicle_mirror::Before();
}
bool PartnerSleepAfter(const mgscoop::partner_input_hook::NativePoseJobContext& c) noexcept {return psleep::After(c);}
void PartnerSleepClosed(const mgscoop::partner_input_hook::NativePoseJobContext& c,bool clean) noexcept {
 psleep::Closed(c,clean);mgscoop::coop_vehicle_mirror::Closed(c,clean);
}
bool PartnerSleepCarrierSlice(psleep::CarrierSliceSnapshot& out) noexcept {
 out={};mgscoop::partner_input_hook::NativePoseLocalCarryContext current{};
 if(!mgscoop::partner_input_hook::ReadNativePoseLocalCarrySlice(current)||!current.carrySerial||current.carrySerial!=current.carryLease.serial||
 current.armGeneration!=current.carryLease.generation||current.armEpoch!=current.carryLease.sceneEpoch)return false;
 out.image=current.image;out.player=current.player;out.go=current.go;out.data=current.data;out.block1=current.block1;out.state1=current.state1;
 out.modelCS1=current.modelCS1;out.modelEntry1=current.modelEntry1;out.armGeneration=current.armGeneration;out.armEpoch=current.armEpoch;
 out.thread=current.thread;out.sequence=current.sequence;out.birth1=current.birth1;out.carrierBirth0=current.carrierBirth0;
 out.sliceNonce=current.sliceNonce;out.carrySerial=current.carrySerial;out.realLocal=current.realLocal;out.selected=current.selected;out.sleepHeld=current.sleepHeld;return true;
}
bool partner_sleep_deferred_configured=false;
bool PartnerSleepDeferredCurrent(const psleep::CarryLeaseProof& expected) noexcept {
 return mgscoop::sleep_deferred_carry_proof::Validate(expected,&psleep::ReadDeferredCarrySleepProof,&psleep::DeferredCarrySleepProofCurrent);
}
bool EnsurePartnerDeferredCarryConfigured() noexcept {
 namespace ih=mgscoop::partner_input_hook;
 if(partner_sleep_deferred_configured)return true;const auto status=ih::NativePoseWindowStatus();
 if(!partner_sleep_configured||!PartnerSleepPrivate()||!status.installed||status.required||status.held||status.faulted||!ih::NativePoseDeferredCarryHooksLive())return false;
 if(!psleep::BindDeferredCarryQuery(&ih::ReadNativePoseDeferredCarryRequest)||!ih::BindNativePoseDeferredCarryValidator(&PartnerSleepDeferredCurrent))return false;
 partner_sleep_deferred_configured=true;return true;
}
bool partner_sleep_queued_configured=false;
bool PartnerSleepQueuedCurrent(const psleep::CarryLeaseProof& expected) noexcept {
 return mgscoop::sleep_queued_carry_proof::Validate(expected,&psleep::ReadQueuedCarrySleepProof,&psleep::QueuedCarrySleepProofCurrent);
}
bool EnsurePartnerQueuedCarryConfigured() noexcept {
 namespace ih=mgscoop::partner_input_hook;
 if(partner_sleep_queued_configured)return true;const auto status=ih::NativePoseWindowStatus();
 if(!partner_sleep_configured||!PartnerSleepPrivate()||!status.installed||status.required||status.held||status.faulted||!ih::NativePoseDeferredCarryHooksLive())return false;
 if(!psleep::BindQueuedCarryQuery(&ih::ReadNativePoseQueuedCarryHandshake)||!ih::BindNativePoseQueuedCarryValidator(&PartnerSleepQueuedCurrent))return false;
 // Native tree validation is a separate exact provider owned by the coordinator.
 // Missing tree/source capability still refuses every queued query.
 partner_sleep_queued_configured=true;return true;
}
bool EnsurePartnerSleepConfigured() noexcept {
 if(partner_sleep_configured){EnsurePartnerDeferredCarryConfigured();EnsurePartnerQueuedCarryConfigured();return true;}
 namespace ih=mgscoop::partner_input_hook;const auto status=ih::NativePoseWindowStatus();
 if(!PartnerSleepPrivate()||!offline_enabled||!partner_on||!partner_gate_ok||!downed_hook_ok||!game_base||!game_span||
    !status.installed||status.required||status.held||status.faulted||!ih::NativePoseWholePlayerLifetimeLive()||!ih::NativePoseLocalCacheIsolationLive())return false;
 const auto refused=[](const char* why) noexcept {mgscoop::worker_scenario::Refuse(why);};
 if(!vehicle_pose_observer.load()){
  if(!mgscoop::coop_passenger::RegisterNativePoseObserver(&mgscoop::coop_vehicle_mirror::NativePoseAfter))return false;
  vehicle_pose_observer.store(true);
 }
 if(!psleep::Configure(game_base,game_span,true,{PartnerSleepPrivate,refused},Log))return false;
 psleep::BindLifetimeQuery(&ih::NativePoseWholePlayerLifetimeLive);
 psleep::BindCarryHeldQuery(&ih::NativePoseCarryRecipientHeld);
 if(!psleep::BindCarrierSliceQuery(&PartnerSleepCarrierSlice)||
    !psleep::BindDualPartsQuery(&ih::ReadNativePoseDualPartsPoll))return false;
 // Central composite listener. Root appends Carry/Road here, rather than
 // registering another listener after this one or while any purpose is held.
 if(!ih::RegisterNativePosePartsPollValidator(&psleep::AllowPartsPoll))return false;
 ih::RegisterNativePoseJobHooks(PartnerSleepBefore,PartnerSleepAfter,PartnerSleepClosed);
 partner_sleep_configured=true;EnsurePartnerDeferredCarryConfigured();EnsurePartnerQueuedCarryConfigured();Log("partner_sleep=private_configured;enabled=0;production=0\r\n");return true;
}
void PartnerLog(void*, const char* line) noexcept { Log(line); }
pp::Refusal PartnerReadState(void*, pp::State& out) noexcept { return pp::ReadState(partner_addresses, out); }
const char* PartnerOrBits(void*, bool terminating, std::uint32_t& q194, std::uint32_t& q198) noexcept {
    return pp::OrRealizeBits(partner_addresses, partner::partner_index, terminating, q194, q198);
}
const char* PartnerClearBits(void*, bool terminating, std::uint32_t& q194, std::uint32_t& q198) noexcept {
    return pp::ClearRealizeBits(partner_addresses, partner::partner_index, terminating, q194, q198);
}
const char* PartnerWriteSlot(void*, bool terminating, const float (&xyzw)[4]) noexcept {
    return pp::WriteSlot(partner_addresses, partner::partner_index, terminating, xyzw);
}
const char* PartnerForceExists(void*, bool terminating) noexcept {
    return pp::ForceExists(partner_addresses, partner::partner_index, terminating);
}
const char* PartnerSetPolicy(void*, bool relax, std::uint8_t restore_value, bool terminating, std::uint8_t& previous) noexcept {
    return pp::SetPolicyRelax(partner_addresses, relax, restore_value, terminating, previous);
}
bool PartnerReadAnim(void*, const pp::State& state, pp::Anim& out) noexcept { return pp::ReadAnim(state, out); }
mgscoop::pose::Sample PartnerSampleRemote(void*, std::uint32_t delay_ms) noexcept {
    return mgscoop::partner_net::SampleRemote(delay_ms);
}
mgscoop::pose::RemoteFrame PartnerSampleRemoteWithTurns(void*, std::uint32_t delay_ms,
                                                      const mgscoop::pose::HistoryCursor& cursor) noexcept {
    return mgscoop::partner_net::SampleRemoteWithTurns(delay_ms, cursor);
}
void PartnerSubmitLocal(void*, const partner::Tick& tick, const mgscoop::sync::AnimState& anim) noexcept {
    mgscoop::partner_net::SubmitLocal(tick, anim);
}
bool PartnerReadLooks(void*, const pp::State& state, pp::Looks& out) noexcept { return pp::ReadLooks(state, out); }
bool PartnerSubmitLook(void*, const partner::Look& look) noexcept { return mgscoop::partner_net::SubmitLook(look); }
pd::RemoteLook PartnerSampleLook(void*) noexcept {
    const auto remote = mgscoop::partner_net::SampleLook();
    return pd::RemoteLook{remote.valid, remote.look, remote.revision};
}
const char* PartnerWriteAnim(void*, bool terminating, std::uint16_t act, std::uint8_t stance, std::uint8_t speed) noexcept {
    if(PartnerSleepSuppressed(nullptr))return "native_sleep_held";
    return pp::WriteAnim(partner_addresses, partner::partner_index, terminating, act, stance, speed);
}
const char* PartnerWriteFaceEquip(void*, bool terminating, std::uint8_t value) noexcept {
    return pp::WriteFaceEquip(partner_addresses, partner::partner_index, terminating, value);
}
const char* PartnerSetStance(void*, bool terminating, std::uint8_t stance, std::uint8_t& psw, std::uint8_t& q30) noexcept {
    if(PartnerSleepSuppressed(nullptr))return "native_sleep_held";
    return pp::SetStance(partner_addresses, partner::partner_index, terminating, stance, psw, q30);
}
const char* PartnerRequestStance(void*, bool terminating, std::uint8_t stance) noexcept {
    if(PartnerSleepSuppressed(nullptr))return "native_sleep_held";
    return pp::RequestStance(partner_addresses, partner::partner_index, terminating, stance);
}
const char* PartnerApplyTransform(void*, bool terminating, const float (&pos)[4], const float (&quat)[4],
                                  mgscoop::PartnerReset reset) noexcept {
    return pp::ApplyTransform(partner_addresses, partner::partner_index, terminating, pos, quat, reset);
}
bool PartnerReadBody(void*, const pp::State& state, float (&pos)[4], float (&quat)[4]) noexcept {
    return pp::ReadBody(partner_addresses, state, partner::partner_index, pos, quat);
}
// Read-only: the local instance 0 body transform through the same validated
// block resolution as the census (facing telemetry only).
bool PartnerReadLocalBody(void*, const pp::State& state, float (&pos)[4], float (&quat)[4]) noexcept {
    return pp::ReadBody(partner_addresses, state, 0, pos, quat);
}
const char* PartnerCensus(void*, const pp::State& state, pp::Census& out) noexcept {
    return pp::ResolveCensus(partner_addresses, state, out);
}
std::size_t PartnerFormatCensus(void*, const pp::Census& census, std::uint32_t k, char* out, std::size_t capacity) noexcept {
    return pp::FormatCensus(partner_addresses, census, k, out, capacity);
}
std::size_t PartnerFormatPsProbe(void*, const pp::State& state, std::uint32_t k, char* out, std::size_t capacity) noexcept {
    return pp::FormatPsProbe(partner_addresses, state, k, out, capacity);
}
// --- Round 3 input lever wiring.
// PP and src are swapped by the input window on whatever thread runs the
// player update. The Lua-thread reads below only count when no window was
// open across them (WindowSeq even and unchanged); they wait at most ~2 ms
// for a window to close and give up after kWindowTries reads.
constexpr int kWindowTries = 3;
constexpr std::uint64_t kWindowWaitUs = 2000;
bool WaitWindowClosed(std::uint32_t& seq) noexcept {
    const std::uint64_t start = QpcMicros();
    for (;;) {
        seq = mgscoop::partner_input_hook::WindowSeq();
        if (!(seq & 1u)) return true;
        if (QpcMicros() - start > kWindowWaitUs) return false;
        YieldProcessor();
    }
}
const char* PartnerInputTargets(void*, const pp::State& state, pp::InputTargets& out) noexcept {
    for (int attempt = 0; attempt < kWindowTries; ++attempt) {
        std::uint32_t seq = 0;
        if (!WaitWindowClosed(seq)) break;
        const char* reason = pp::ResolveInputTargets(partner_addresses, state, out);
        if (mgscoop::partner_input_hook::WindowSeq() == seq) return reason;
    }
    out = pp::InputTargets{};
    out.refusal = "busy";
    return out.refusal;
}
std::size_t PartnerFormatInputCensus(void*, const pp::InputTargets& targets, char* out, std::size_t capacity) noexcept {
    return pp::FormatInputCensus(partner_addresses, targets, out, capacity);
}
bool PartnerReadPad(void*, const pp::State& state, mgscoop::partner_input::PadRaw& out) noexcept {
    for (int attempt = 0; attempt < kWindowTries; ++attempt) {
        std::uint32_t seq = 0;
        if (!WaitWindowClosed(seq)) break;
        const bool ok = pp::ReadPad(partner_addresses, state, out);
        if (mgscoop::partner_input_hook::WindowSeq() == seq) return ok;
    }
    out = mgscoop::partner_input::PadRaw{};
    return false;
}
bool PartnerSubmitInput(void*, const mgscoop::partner_input::InputPacket& packet) noexcept {
    return mgscoop::partner_net::SubmitInput(packet);
}
bool PartnerNetRunning(void*) noexcept { return mgscoop::partner_net::Running(); }
// The window consumes the accumulated edge sets with each sample (round 4).
bool PartnerSampleInput(mgscoop::partner_input::Sample& out) noexcept { return mgscoop::partner_net::ConsumeInput(out); }
bool PartnerLatestInput(void*, mgscoop::partner_input::Sample& out) noexcept { return mgscoop::partner_net::LatestInput(out); }
const char* PartnerApplyPosition(void*, bool terminating, const float (&pos)[4]) noexcept {
    return pp::ApplyPosition(partner_addresses, partner::partner_index, terminating, pos);
}
bool PartnerReadVelocity(void*, const pp::State& state, float (&vel)[4]) noexcept {
    return pp::ReadVelocity(partner_addresses, state, partner::partner_index, vel);
}
const char* PartnerInstallWrap(void*) noexcept { return mgscoop::partner_input_hook::InstallWrap(); }
const char* PartnerInstallInputHook(void*) noexcept {
    return mgscoop::partner_input_hook::Install(game_base, Log, &PartnerSampleInput);
}
void PartnerResetInputFault(void*) noexcept {
    mgscoop::partner_input_hook::ResetFault();
    // Native puppet: MissionStart disarms the deferral and clears its fault
    // (never the pending records or the batch flags).
    mgscoop::partner_defer::MissionStart();
}
void PartnerPublishArm(void*, const mgscoop::partner_input::Arm& arm) noexcept {
    mgscoop::partner_input_hook::PublishArm(arm);
    partner_sleep_arm=psbridge::CaptureArm(arm,OnRuntimeThread()&&!mission_terminating,
        GetCurrentThreadId(),GetTickCount64());
}
void PartnerDrainInputStats(void*, pd::InputStats& out) noexcept {
    mgscoop::partner_input_hook::Stats s{};
    mgscoop::partner_input_hook::DrainStats(s);
    out.window = s.window;
    for (unsigned i = 0; i < 8; ++i) out.holds[i] = s.holds[i];
    out.hold_count = s.hold_count;
    out.wrap = s.wrap;
    out.native = s.native;
}
bool PartnerInputHookLive(void*) noexcept { return mgscoop::partner_input_hook::PatchLive(game_base); }
// --- Native puppet wiring (docs/native-puppet.md). Only the driver's tick
// installs, lazily, after the once-per-process code gate passed (Cfg requires
// it): H0 (the input hook), the wraps, then H2 and H1.
mgscoop::partner_defer::Mode DeferMode(mgscoop::PartnerDefer mode) noexcept {
    return mode == mgscoop::PartnerDefer::on ? mgscoop::partner_defer::Mode::on
         : mode == mgscoop::PartnerDefer::census ? mgscoop::partner_defer::Mode::census : mgscoop::partner_defer::Mode::off;
}
const char* PartnerInstallDefer(void*, mgscoop::PartnerDefer mode) noexcept {
    if (!mgscoop::GetLabConfig().partner_input_hook) return "hook_disabled";
    if (mode == mgscoop::PartnerDefer::off) return "disabled";
    if (mgscoop::partner_input_hook::Install(game_base, Log, &PartnerSampleInput)) return "hook";
    if (mgscoop::partner_input_hook::InstallWrap()) return "wrap";
    if (const char* why = mgscoop::partner_defer::Install(game_base, DeferMode(mode), Log)) return why;
    // The phase-1 local detour only where a drain can arm it (defer=on).
    if (mode == mgscoop::PartnerDefer::on)
        if (const char* why = mgscoop::partner_local_detour::Install(game_base, Log)) return why;
    // F3 (docs/native-puppet.md "Traversal icon"): the padop vtable copy for
    // body 1's traversal start, only with the key on (default with the gun
    // profile, lab and play) and a drain (defer=on).
    // No patch; a refusal is logged and leaves the copy unused (no swap).
    if (mode == mgscoop::PartnerDefer::on && mgscoop::GetLabConfig().partner_native_traverse)
        mgscoop::partner_icon::Build(game_base, Log);
    // G1 (docs/native-puppet.md "Partner HUD hints"): the five hint-queue leaf
    // hooks that swallow body 1's HUD hints inside its drain job, only with the
    // lab key on (default off in lab and play) and a drain (defer=on). A
    // refusal is logged once and installs nothing (stock hints, as before).
    if (mode == mgscoop::PartnerDefer::on && mgscoop::GetLabConfig().partner_native_ui_filter)
        mgscoop::partner_ui_filter::Install(game_base, game_span, Log);
    return nullptr;
}
void PartnerNativeInstall(void*, pd::NativeInstall& out) noexcept {
    out.h0 = mgscoop::partner_input_hook::Installed();
    out.wrap = mgscoop::partner_input_hook::WrapInstalled();
    out.h1 = mgscoop::partner_defer::H1Installed();
    out.h2 = mgscoop::partner_defer::H2Installed();
    out.live_h0 = out.h0 && mgscoop::partner_input_hook::PatchLive(game_base);
    const bool live = out.h1 && out.h2 && mgscoop::partner_defer::PatchLive(game_base);
    out.live_h1 = out.live_h2 = live;
    out.p1 = mgscoop::partner_local_detour::Installed();
    out.live_p1 = out.p1 && mgscoop::partner_local_detour::PatchLive(game_base);
}
void PartnerDrainDeferStats(void*, mgscoop::partner_defer::Stats& out) noexcept { mgscoop::partner_defer::DrainStats(out); }
void PartnerSetDefer(void*, bool armed, mgscoop::PartnerDefer mode) noexcept {
    // Leaving disarms first; entering sets the mode before arming.
    if (!armed) mgscoop::partner_defer::SetArmed(false);
    mgscoop::partner_defer::SetMode(DeferMode(mode));
    if (armed) mgscoop::partner_defer::SetArmed(true);
}
void PartnerPublishIdentity(void*, const mgscoop::partner_defer::Identity& identity, bool valid) noexcept {
    if (valid) mgscoop::partner_defer::PublishIdentity(identity);
    else mgscoop::partner_defer::ClearIdentity();
}
void PartnerPublishWrapRoots(void*, std::uintptr_t root0, std::uintptr_t root1, bool census) noexcept {
    mgscoop::partner_input_hook::PublishWrapRoots(root0, root1, census);
}
const char* PartnerNativeTargets(void*, const pp::State& state, const pp::InputTargets& targets, pp::NativeTargets& out) noexcept {
    return pp::ResolveNative(partner_addresses, state, targets, out);
}
bool PartnerReadGun1(void*, const pp::State& state, pp::Gun1& out) noexcept { return pp::ReadGun1(partner_addresses, state, out); }
bool PartnerReadLookSource(void*, const pp::State& state, pp::LookSource& out) noexcept {
    return pp::ReadLookSource(partner_addresses, state, out);
}
std::uint32_t PartnerWindowSeq(void*) noexcept { return mgscoop::partner_input_hook::WindowSeq(); }
bool PartnerReadRates(void*, const pp::State& state, std::uint32_t index, pp::RateRead& out) noexcept {
    return pp::ReadRates(partner_addresses, state, index, out);
}
// The ending mission's epoch (MissionStart/End): the rates mirror and the
// aim-target mirror drop their state without writing; that epoch never
// writes again.
void PartnerForgetRates(void*, std::uint32_t epoch) noexcept {
    mgscoop::partner_input_hook::ForgetRates(epoch);
    mgscoop::partner_aim_point::Forget(epoch);
}
bool PartnerLinkLive(void*) noexcept {
    const auto info = mgscoop::partner_net::GetLinkInfo();
    return info.state == mgscoop::link::LinkState::connected ||
           (info.state == mgscoop::link::LinkState::off && mgscoop::partner_net::Running());
}
std::uint32_t PartnerLinkSession(void*) noexcept { return mgscoop::partner_net::GetLinkInfo().session; }
bool PartnerPeerInputV5(void*) noexcept { return mgscoop::partner_net::PeerSendsInputV5(); }
bool PartnerPeerInputV6(void*) noexcept { return mgscoop::partner_net::PeerSendsInputV6(); }
bool PartnerPeerInputV7(void*) noexcept { return mgscoop::partner_net::PeerSendsInputV7(); }
bool PartnerDrainReliable(void*, std::uint8_t channel, std::uint8_t* out, std::size_t capacity, std::size_t& size) noexcept {
    return mgscoop::partner_net::DrainReliable(channel, out, capacity, size);
}
// --- Native weapons (docs/native-puppet.md "Weapons"). The fire filter
// installs lazily from the driver tick (after the code gate passed, like the
// deferral) and is armed for instance 1's id; reads are pure.
const char* PartnerInstallFire(void*, std::uint16_t owner) noexcept {
    if (const char* why = mgscoop::partner_fire_filter::Install(game_base, Log)) return why;
    return mgscoop::partner_fire_filter::Arm(owner) ? nullptr : "owner";
}
void PartnerFireStats(void*, mgscoop::partner_fire_filter::Stats& out) noexcept {
    mgscoop::partner_fire_filter::ReadStats(game_base, out);
}
void PartnerFireCensus(void*, mgscoop::partner_fire_filter::Census& out) noexcept {
    mgscoop::partner_fire_filter::ReadCensus(game_base, partner_addresses.image_end - partner_addresses.image_begin, out);
}
bool PartnerReadGun(void*, const pp::State& state, std::uint32_t k, pp::Gun1& out) noexcept {
    return pp::ReadGun(partner_addresses, state, k, out);
}
bool PartnerReadSlots(void*, const pp::State& state, std::uint32_t k, pp::Slots& out) noexcept {
    return pp::ReadSlots(partner_addresses, state, k, out);
}
bool PartnerEquipType(void*, std::uint32_t equip, std::uint8_t& type) noexcept {
    return pp::EquipType(partner_addresses, equip, type);
}
bool PartnerReadLoadout(void*, pp::Loadout& out) noexcept {
    // Nothing new is sent once the window closes (the sender reads nothing, so no revision changes).
    if (InterlockedCompareExchange(&partner_window_closing, 0, 0)) return false;
    return pp::ReadLoadout(partner_addresses, out);
}
// Channel 2 carries the equipment manifest and the capability message
// (docs/native-puppet.md "Online"): both append, so neither replaces the
// other's unsent message (latest mode would).
bool PartnerSendReliable(void*, std::uint8_t channel, const std::uint8_t* bytes, std::size_t size) noexcept {
    const auto result = mgscoop::partner_net::SendReliable(channel, bytes, size, mgscoop::link::ReliableMode::append);
    return result == mgscoop::link::ReliableResult::queued || result == mgscoop::link::ReliableResult::replaced;
}
bool PartnerSendReliableAppend(void* context, std::uint8_t channel, const std::uint8_t* bytes, std::size_t size) noexcept {
    return PartnerSendReliable(context, channel, bytes, size);
}
// Teammate carry events (coop_carry) on reliable channel 2, appended like the
// manifest and capability messages.
bool CoopCarrySend(const std::uint8_t* bytes, std::size_t size) noexcept {
    return PartnerSendReliable(nullptr, 2, bytes, size);
}
// --- Native polish (docs/native-puppet.md "Facing", "Weapons"): lazily from
// the driver tick after the code gate passed, like the deferral.
// Also true once the game window is closing (PartnerShutdown): the facing
// governor's xform writes and the look source install stop with the windows
// (MissionEnd may only follow later, on the mission thread).
bool PartnerInBatch(void*) noexcept {
    return mgscoop::partner_defer::InBatch() || mgscoop::partner_defer::ShuttingDown();
}
const char* PartnerInstallLookSource(void*) noexcept { return mgscoop::partner_look_source::Install(game_base, Log); }
const char* PartnerInstallNoise(void*) noexcept { return mgscoop::partner_fire_filter::InstallNoise(game_base, Log); }
// --- Aim parity (docs/native-puppet.md "Facing", the aim-target mirror): the
// driver publishes the target; the drain window writes it (partner_input_hook).
void PartnerPublishAim(void*, const mgscoop::partner_aim_point::Publication& publication) noexcept {
    mgscoop::partner_aim_point::Publish(publication);
}
void PartnerAimStats(void*, mgscoop::partner_aim_point::Stats& out) noexcept { mgscoop::partner_aim_point::ReadStats(out); }
bool PartnerReadAim(void*, const pp::State& state, pp::AimRead& out) noexcept { return pp::ReadAim(partner_addresses, state, out); }
#if defined(MGS_LOCAL_STOCK46_AMMO_TEST) && MGS_LOCAL_STOCK46_AMMO_TEST
constexpr bool kAmmoExperiment = true;
#else
constexpr bool kAmmoExperiment = false;
#endif
// partner_interp=auto: the link's adaptive delay (the configured one otherwise).
std::uint32_t PartnerInterpMs(void*, std::uint32_t configured) noexcept {
    return mgscoop::partner_net::EffectiveInterpMs(configured);
}
// --- Round 3 direct motion wiring.
const char* PartnerDmTargets(void*, const pp::State& state, pp::DmTargets& out) noexcept {
    return pp::ResolveDm(partner_addresses, state, out);
}
bool PartnerReadDm(void*, const pp::State& state, const pp::DmTargets& targets, pp::DmState& out) noexcept {
    return pp::ReadDm(partner_addresses, state, targets, out);
}
std::size_t PartnerFormatDmCensus(void*, const pp::DmTargets& targets, char* out, std::size_t capacity) noexcept {
    return pp::FormatDmCensus(partner_addresses, targets, out, capacity);
}
std::size_t PartnerFormatDmProbe(void*, const pp::DmState& dm, bool patched, char* out, std::size_t capacity) noexcept {
    return pp::FormatDmProbe(dm, patched, out, capacity);
}
bool PartnerDmPatch(void*, bool on, const char* reason) noexcept {
    if (on) mgscoop::partner_dm_patch::Patch(game_base, Log);
    else mgscoop::partner_dm_patch::Revert(game_base, Log, reason);
    return mgscoop::partner_dm_patch::Patched();
}
// The seated vehicle rider's four dwords (partner_ride_seat; docs/anim-round1-seat.md).
bool PartnerRideSeatArm(void*, std::uint8_t seat) noexcept {
    return game_base && mgscoop::partner_ride_seat::Arm(game_base, game_span, seat, Log);
}
void PartnerRideSeatRevert(void*, const char* reason) noexcept {
    if (game_base) mgscoop::partner_ride_seat::Revert(game_base, game_span, Log, reason);
}
bool PartnerDmClearPending(void*) noexcept {
    bool cleared = false;
    mgscoop::partner_player::ClearDmPending(partner_addresses, mission_terminating, cleared);
    return cleared;
}
std::uint8_t PartnerDmClearStale(void*, std::uint8_t mask) noexcept {
    std::uint8_t cleared = 0;
    mgscoop::partner_player::ClearDmBits(partner_addresses, mission_terminating, mask, cleared);
    return cleared;
}
unsigned PartnerDmMotionCount(void*) noexcept { return MountedPlayerMotionCount(); }
// partner.trigger (lab only): reads at most `capacity` bytes, then deletes it.
pd::TriggerRead PartnerTakeTrigger(void*, char* text, std::size_t capacity, std::size_t& length) noexcept {
    length = 0;
    if (!lab_active || !partner_trigger_path[0] || !text) return pd::TriggerRead::none;
    const DWORD error = GetLastError();
    HANDLE file = CreateFileW(partner_trigger_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { SetLastError(error); return pd::TriggerRead::none; }
    DWORD read_bytes = 0;
    LARGE_INTEGER size{};
    const bool read = ReadFile(file, text, static_cast<DWORD>(capacity), &read_bytes, nullptr) &&
                      GetFileSizeEx(file, &size) && size.QuadPart <= static_cast<LONGLONG>(capacity);
    CloseHandle(file);
    DeleteFileW(partner_trigger_path);
    SetLastError(error);
    length = read_bytes;
    if (read && mgscoop::partner_upper_manual_idle::IsTrigger(text, length)) {
        const bool allowed = mgscoop::partner_upper_manual_idle::Compiled() && OnRuntimeThread();
        upper_idle_requested = allowed;
        LogFormat("upper_manual_idle=trigger; queued=%u; standalone=1; driver_changed=0; dm_changed=0; automatic_capability=0\r\n", allowed ? 1u : 0u);
        SetLastError(error); length = 0;
        return pd::TriggerRead::none;
    }
    return read ? pd::TriggerRead::read : pd::TriggerRead::unreadable;
}
bool PartnerFocused(void*) noexcept {
    DWORD foreground = 0;
    const HWND window = GetForegroundWindow();
    if (window) GetWindowThreadProcessId(window, &foreground);
    return foreground == GetCurrentProcessId();
}
// --- WS3 vehicles (docs/vehicle-ws3.md): the read-only vehicle unit/body read
// (the ride census, the ride addendum's category, the seat-by-id gate) and the
// seat-by-id write. Both stay behind the code gate; the write only with
// partner_ride_seat_id=1 (default on, lab and play, after hidden run abb0b7cf;
// the driver offers the id only for a peer whose ride addendum names the
// category), on the player-update thread (the input hook's thread, which every
// hidden run logged equal to the driver's Lua thread), with the seat dwords
// armed (the untargeted entry's camera stores skipped).
void PartnerVehicleRead(void*, std::uint16_t id, mgscoop::partner_vehicle::Read& out) noexcept {
    out = mgscoop::partner_vehicle::Read{};
    if (!game_base || !partner_gate_ok) { out.refusal = "gate"; return; }
    mgscoop::partner_vehicle::ReadLive(game_base, id, out);
}
const char* PartnerSeatTarget(void*, const pp::SeatTargetRequest& request, pp::SeatTargetResult& out) noexcept {
    out = pp::SeatTargetResult{};
    const auto& config = mgscoop::GetLabConfig();
    if (!game_base || !partner_gate_ok) return "gate";
    if (!config.partner_ride_seat_id) return "off";
    if (!OnRuntimeThread() || mission_terminating) return "runtime";
    const std::uint32_t hook = mgscoop::partner_input_hook::HookThread();
    if (!hook || hook != GetCurrentThreadId()) return "thread";
    if (!mgscoop::partner_ride_seat::Armed()) return "seat_unarmed";
    return pp::WriteSeatTarget(partner_addresses, game_base, game_span, mission_terminating, request, out);
}
// F1 vehicles (docs/native-puppet.md "Vehicle boarding", partner_ride_board): the two board
// call-site redirects install lazily from the driver tick behind the code gate (like the
// deferral); the policy word and the counters are atomics; the local passenger report reads
// only (ReadProcessMemory) and resolves the vehicle through partner_vehicle::ReadLive.
const char* PartnerBoardInstall(void*) noexcept {
    if (!game_base || !partner_gate_ok) return "gate";
    // M6 seat 1 (lab.ini partner_ride_front / partner_ride_cab, passenger mode only; off unless named).
    const auto& config = mgscoop::GetLabConfig();
    mgscoop::partner_board::SeatOpts opts{};
    opts.front = config.partner_ride_board == mgscoop::PartnerRideBoard::passenger && config.partner_ride_front;
    opts.cab = config.partner_ride_board == mgscoop::PartnerRideBoard::passenger && config.partner_ride_cab;
    const char* why = mgscoop::partner_board::Install(game_base, game_span, &mgscoop::partner_net::NowMs, Log, opts);
    // M6 seat races (lab.ini partner_ride_race, off unless named; review R1/R2): site C, the pre-query's final
    // answer (the want), the guest's hold and the SEAT deny / grant. Logs partner_board=race once; a refusal
    // leaves the race off and the board as it is.
    if (!why && config.partner_ride_race) mgscoop::partner_board::InstallRace();
    return why;
}
bool PartnerBoardLive(void*) noexcept {
    if (!game_base) return false;
    const bool live = mgscoop::partner_board::PatchLive(game_base);
    // Review R5: site B lost with a phase-1 immediate left at 1 (seat 1): back to the stock 2.
    if (!live && mgscoop::partner_board::RestorePhaseOneIfBLost(game_base)) Log("partner_board=phase_one_restored; reason=site_b_lost\r\n");
    return live;
}
void PartnerBoardPublish(void*, std::uint64_t policy) noexcept { mgscoop::partner_board::Publish(policy); }
void PartnerBoardStats(void*, mgscoop::partner_board::Stats& out) noexcept { mgscoop::partner_board::ReadStats(out); }
bool PartnerLocalPassenger(void*, const pp::State& state, mgscoop::partner_board::LocalPassenger& out) noexcept {
    out = mgscoop::partner_board::LocalPassenger{};
    if (!game_base || !partner_gate_ok) { out.reason = "gate"; return false; }
    auto reader = [](std::uintptr_t address, void* buffer, std::size_t size) noexcept {
        SIZE_T read = 0;
        return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), buffer, size, &read) && read == size;
    };
    auto lookup = [](std::uint16_t id) noexcept -> std::uintptr_t {
        mgscoop::partner_vehicle::Read r{};
        mgscoop::partner_vehicle::ReadLive(game_base, id, r);
        return !r.refusal && r.unit.id == id ? r.unit.rec : 0;
    };
    out = mgscoop::partner_board::ReadLocalPassenger(reader, lookup, state.P, state.Q, state.local,
                                                     mgscoop::partner_board::SeatOne(mgscoop::partner_board::LiveSeatOpts()));
    return true;
}
// M6 review R4: the seat-1 remap skip, from the driver tick only.
bool PartnerBoardRemap(void*, bool skip) noexcept { return game_base && mgscoop::partner_board::SetRemapSkip(skip); }
// M6 review R3: rider[seat] of this copy's vehicle `id` for the v4 seat addendum (ReadProcessMemory only).
bool PartnerSeatRider(void*, std::uint16_t id, std::uint8_t seat, std::uint16_t& out) noexcept {
    out = 0xFFFF;
    if (!game_base || !partner_gate_ok || seat > 3) return false;
    mgscoop::partner_vehicle::Read r{};
    mgscoop::partner_vehicle::ReadLive(game_base, id, r);
    if (r.refusal || r.unit.id != id || !r.unit.rec) return false;
    std::uint16_t value = 0xFFFF;
    SIZE_T read = 0;
    const auto at = r.unit.rec + mgscoop::partner_board::kUnitRiders + 2u * seat;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(at), &value, sizeof(value), &read) || read != sizeof(value))
        return false;
    out = value;
    return true;
}
// F1 passenger camera (docs/native-puppet.md "Passenger camera", partner_ride_board=passenger):
// the pinned-block check runs lazily from the driver tick behind the code gate (like the board;
// nothing is patched); the connect writes only the local camera's ignore-collision handle, on
// this (the driver tick's) thread; the counters are atomics; the camera read only reads.
const char* PartnerPassCameraInstall(void*) noexcept {
    if (!game_base || !partner_gate_ok) return "gate";
    return mgscoop::partner_pass_camera::Install(game_base, game_span, Log);
}
const char* PartnerPassCameraConnect(void*, const pp::State& state, std::uint16_t vehicle, bool on,
                                     mgscoop::partner_pass_camera::Camera& out) noexcept {
    out = mgscoop::partner_pass_camera::Camera{};
    if (!game_base || !partner_gate_ok || !mgscoop::partner_pass_camera::Installed()) return "install";
    return mgscoop::partner_pass_camera::Connect(game_base, state.P, vehicle, on, out);
}
void PartnerPassCameraStats(void*, mgscoop::partner_pass_camera::Stats& out) noexcept { mgscoop::partner_pass_camera::ReadStats(out); }
bool PartnerPassCameraRead(void*, const pp::State& state, mgscoop::partner_pass_camera::Camera& out) noexcept {
    out = mgscoop::partner_pass_camera::Camera{};
    if (!game_base || !partner_gate_ok) return false;
    return mgscoop::partner_pass_camera::ReadCamera(game_base, state.P, out);
}
// Design 8.27: the lab trace's camera read (reads only) and the passcam lever's override.
bool PartnerPassCameraView(void*, const pp::State& state, mgscoop::partner_pass_camera::View& out) noexcept {
    out = mgscoop::partner_pass_camera::View{};
    if (!game_base || !partner_gate_ok) return false;
    return mgscoop::partner_pass_camera::ReadView(game_base, state.P, out);
}
int PartnerPassCameraOverride(void*) noexcept { return mgscoop::partner_pass_camera::Override(); }
bool PartnerPassCameraPitch(void*, const pp::State& state, float pitch) noexcept {
    if (!game_base || !partner_gate_ok || !mgscoop::partner_pass_camera::Installed()) return false;
    return mgscoop::partner_pass_camera::WritePitch(game_base, state.P, pitch);
}
bool PartnerPassCameraRotation(void*, const pp::State& state, const float (&rot)[4]) noexcept {
    if (!game_base || !partner_gate_ok || !mgscoop::partner_pass_camera::Installed()) return false;
    return mgscoop::partner_pass_camera::WriteRotation(game_base, state.P, rot);
}
std::uintptr_t PartnerPassCameraMvt(void*, const pp::State& state) noexcept {
    if (!game_base || !partner_gate_ok) return 0;
    return mgscoop::partner_pass_camera::ModeVtable(game_base, state.P);
}
// Design 8.33 (partner_pass_camera=view): the seated aim view and the get-off camera request.
bool PartnerPassCameraRaiseRead(void*, const pp::State& state, mgscoop::partner_pass_camera::RaiseView& out) noexcept {
    out = mgscoop::partner_pass_camera::RaiseView{};
    if (!game_base || !partner_gate_ok || !mgscoop::partner_pass_camera::Installed()) return false;
    return mgscoop::partner_pass_camera::ReadRaise(game_base, state.P, out);
}
bool PartnerPassCameraRaiseWrite(void*, const pp::State& state, int index, bool on, float raise) noexcept {
    if (!game_base || !partner_gate_ok || !mgscoop::partner_pass_camera::Installed()) return false;
    return mgscoop::partner_pass_camera::WriteRaise(game_base, state.P, index, on, raise);
}
bool PartnerPassCameraLift(void*, const pp::State& state, bool on, float raise) noexcept {
    if (!game_base || !partner_gate_ok || !mgscoop::partner_pass_camera::Installed()) return false;
    return mgscoop::partner_pass_camera::WriteLift(game_base, state.P, on, raise);
}
const char* PartnerPassCameraTurn(void*, const pp::State& state, float pitch, float yaw, float interp) noexcept {
    if (!game_base || !partner_gate_ok || !mgscoop::partner_pass_camera::Installed()) return "install";
    return mgscoop::partner_pass_camera::RequestTurn(game_base, state.P, pitch, yaw, interp);
}
// M7 seated shots, S1 (partner_seat_shot.hpp): the fire filter's publication, its counters and the seatshot lever.
void PartnerSeatShotPublish(void*, const mgscoop::partner_seat_shot::Pub& pub) noexcept { mgscoop::partner_fire_filter::PublishSeatShot(pub); }
void PartnerSeatShotWithdraw(void*) noexcept { mgscoop::partner_fire_filter::WithdrawSeatShot(); }
bool PartnerSeatShotStats(void*, mgscoop::partner_fire_filter::SeatShotStats& out) noexcept {
    return mgscoop::partner_fire_filter::ReadSeatShot(out);
}
int PartnerSeatShotOverride(void*) noexcept { return mgscoop::partner_fire_filter::SeatShotOverride(); }
// WS2 Stage A (partner_support_policy::ModeRefusal, pattern EquipAdmissionReason):
// native type-12 support runs in the one-PC lab, and in two-PC play only while
// link v2 is connected. Both observer gates use it. After a link drop the guard
// and the thrown filter stay installed and armed (queued or unwindowed body-1
// work may still finish); Ready and Follow refuse through ContextFor.
const char* SupportModeReason() noexcept {
    const auto& config=mgscoop::GetLabConfig();
    const bool play=config.mode==mgscoop::LabMode::play;
    return mgscoop::partner_support_policy::ModeRefusal(config.mode==mgscoop::LabMode::lab,play,
        play && mgscoop::partner_net::GetLinkInfo().state==mgscoop::link::LinkState::connected);
}
// The type the support lines name (WS4 v1): 13 for C4 391..395, 14 for arms
// 515..521, else 12 (the writer's own receipts carry the decoded type).
unsigned SupportLineType(std::uint16_t equip) noexcept {
    return mgscoop::partner_support_policy::C4Equip(equip) ? 13u : mgscoop::partner_support_policy::ArmEquip(equip) ? 14u : 12u;
}
// Anim round 2: read-only admission for the driver's support gate. The
// observer writer below is the only allocator; this callback never equips.
// WS4 v1: the writer context names one lane (slot 3, or 6 for an arm); Ready
// answers only for that lane, so asking both is the same as asking the
// context's own.
bool PartnerSupportReady(void*,const pp::State& state,std::uint16_t equip) noexcept {
    const auto& config=mgscoop::GetLabConfig();
    if(!config.partner_native_support || SupportModeReason() || config.partner_input_p218_allow ||
       mgscoop::partner_input_hook::NativePoseWindowStatus().required ||
       mgscoop::partner_input_hook::NativeSupportLatchedOff() ||
       !mgscoop::partner_support_guard::Live() ||
       !mgscoop::partner_support_guard::Armed(state.P,1) ||
       !mgscoop::partner_fire_filter::ThrownPatchLive(game_base) || !mgscoop::partner_fire_filter::ThrownArmed(1)) return false;
    pp::DmTargets targets{};
    if(pp::ResolveDm(partner_addresses,state,targets)) return false;
    const auto base=reinterpret_cast<std::uintptr_t>(game_base);
    return mgscoop::partner_support_equip::Ready(base,state.P,targets.blk1,equip,nullptr,nullptr,3) ||
        mgscoop::partner_support_equip::Ready(base,state.P,targets.blk1,equip,nullptr,nullptr,6);
}
// The driver's support verdict for partner_net's unknown slot/equip debounce.
void PartnerPublishSupportIdentity(void*,std::uint16_t equip) noexcept { mgscoop::partner_net::SetSupportIdentity(equip); }
// F3: the read-only census of block 0's / block 1's action plugin lists (behind the code gate).
void PartnerIconCensus(void*, const pp::InputTargets& targets, mgscoop::partner_icon::Census& out) noexcept {
    out = mgscoop::partner_icon::Census{};
    if (!game_base || !partner_gate_ok) return;
    mgscoop::partner_icon::ReadCensus(game_base, targets.blk0, targets.blk1, out);
}
// Gestures (docs/native-puppet.md "Gestures"): both ids' upper act byte, the
// lazy body-1 update hook (only after the startup partner code gate) and its step.
bool PartnerReadUpper(void*, const pp::State& state, std::uint8_t (&out)[2]) noexcept { return pp::ReadUpper(state, out); }
const char* PartnerGestureInstall(void*) noexcept {
    if (!game_base || !partner_gate_ok) return "gate";
    return mgscoop::partner_gesture::Install(game_base, game_span, Log);
}
void PartnerGestureStep(void*, const mgscoop::partner_gesture::Command& command, mgscoop::partner_gesture::Status& out) noexcept {
    mgscoop::partner_gesture::Step(command, out);
}
// The gesture lab lever (docs/native-puppet.md "Gestures", "Lab lever: knock and
// hand signs"): the debug command "gesture_lever knock|sign_go|sign_stop" on the
// runtime thread. Lab only (the key, which play forces off, mode=lab, lab active,
// copied saves, the offline branch, the partner code gate, gameplay); this
// copy's own player only. One result and one partner_gesture=lever line.
void HandleGestureLever(std::uint32_t command_id, const char* text) noexcept {
    namespace gl = mgscoop::partner_gesture_lever;
    const auto& labc = mgscoop::GetLabConfig();
    const auto snapshot = mgscoop::debug_menu::GetSnapshot();
    const gl::Gate gate{labc.partner_gesture_lever, labc.mode == mgscoop::LabMode::lab, lab_active, lab::SavesRedirected(),
                        offline_enabled, partner_on, partner_gate_ok && game_base != nullptr,
                        snapshot.gameplay_ready && !snapshot.terminating && !mission_terminating,
                        mgscoop::partner_defer::ShuttingDown(), gl::Latched()};
    gl::Outcome outcome{};
    outcome.request = gl::Parse(text);
    if (const char* why = gl::Admit(gate)) outcome.refusal = why;
    else if (outcome.request == gl::Request::none) outcome.refusal = "args";
    else outcome = gl::Fire(reinterpret_cast<std::uintptr_t>(game_base), outcome.request);
    char result[161]{}, line[400]{};
    gl::FormatResult(outcome, result, sizeof(result));
    gl::FormatLog(outcome, line, sizeof(line));
    mgscoop::debug_menu::Result(command_id, outcome.refusal ? 0u : 1u, result[0] ? result : "gesture_lever=refused;reason=format");
    if (line[0]) Log(line);
}
constexpr pd::Hooks partner_hooks{nullptr, PartnerLog, PartnerReadState, PartnerOrBits, PartnerClearBits, PartnerWriteSlot,
    PartnerForceExists, PartnerSetPolicy, PartnerReadAnim, PartnerSampleRemote, PartnerSubmitLocal, PartnerTakeTrigger,
    PartnerFocused, PartnerReadLooks, PartnerSubmitLook, PartnerSampleLook, PartnerWriteAnim, PartnerWriteFaceEquip,
    PartnerSetStance, PartnerRequestStance, PartnerApplyTransform, PartnerReadBody, PartnerCensus, PartnerFormatCensus,
    PartnerFormatPsProbe, PartnerInputTargets, PartnerFormatInputCensus, PartnerReadPad, PartnerSubmitInput, PartnerNetRunning,
    PartnerInstallInputHook, PartnerPublishArm, PartnerDrainInputStats, PartnerInputHookLive, PartnerDmTargets, PartnerReadDm,
    PartnerFormatDmCensus, PartnerFormatDmProbe, PartnerDmPatch, PartnerDmClearPending, PartnerResetInputFault,
    PartnerApplyPosition, PartnerReadVelocity, PartnerLatestInput, PartnerInstallWrap, PartnerInterpMs, PartnerDmMotionCount,
    PartnerSampleRemoteWithTurns, PartnerReadLocalBody, PartnerInstallDefer, PartnerNativeInstall, PartnerDrainDeferStats,
    PartnerSetDefer, PartnerPublishIdentity, PartnerPublishWrapRoots, PartnerNativeTargets, PartnerReadGun1, PartnerReadLookSource,
    PartnerWindowSeq, PartnerLinkLive, PartnerLinkSession, PartnerDrainReliable, PartnerReadRates, PartnerForgetRates, kAmmoExperiment,
    PartnerInstallFire, PartnerFireStats, PartnerFireCensus, PartnerReadGun, PartnerReadLoadout, PartnerSendReliable, PartnerInBatch,
    PartnerInstallLookSource, PartnerInstallNoise, PartnerSendReliableAppend, PartnerPublishAim, PartnerAimStats, PartnerReadAim,
    PartnerReadSlots, PartnerEquipType, PartnerPeerInputV5, PartnerPeerInputV6, PartnerSleepSuppressed,
    PartnerDrainReliableOrigin,PartnerReceiveVehicle,PartnerVehicleAttached,PartnerVehicleJob,PartnerRideSeatArm,PartnerRideSeatRevert,nullptr,PartnerSupportReady,
    PartnerVehicleRead,PartnerSeatTarget,PartnerPublishSupportIdentity,PartnerPeerInputV7,
    PartnerBoardInstall,PartnerBoardLive,PartnerBoardPublish,PartnerBoardStats,PartnerLocalPassenger,PartnerIconCensus,
    PartnerReadUpper, PartnerGestureInstall, PartnerGestureStep,
    PartnerPassCameraInstall, PartnerPassCameraConnect, PartnerPassCameraStats, PartnerPassCameraRead, PartnerDmClearStale,
    PartnerSeatRider, PartnerBoardRemap, PartnerPassCameraView, PartnerPassCameraOverride, PartnerPassCameraPitch,
    PartnerPassCameraMvt, PartnerPassCameraRotation, PartnerSeatShotPublish, PartnerSeatShotWithdraw, PartnerSeatShotStats,
    PartnerSeatShotOverride, PartnerPassCameraRaiseRead, PartnerPassCameraRaiseWrite, PartnerPassCameraTurn,
    PartnerPassCameraLift};
// The mission of the current partner epoch (link-status.json "mission").
std::uint32_t partner_mission = 0;
// World sync (docs/world-sync-protocol.md): configured at install only with
// offline-v1, partner=1, net=1, link v2, redirected saves and lab.ini world>=1.
// World receipts are quiet (TX/RX/LOG log no entry/return lines; CFG does).
bool world_on = false;
thread_local bool world_quiet = false;
thread_local bool debug_quiet = false;
thread_local char debug_reply[400]{};
// Debug "Spawn enemy here" native placement (soldier_warp.cpp, lab.ini
// debug_soldier_warp=1): the contract is verified once at startup.
bool soldier_warp_ready = false;
unsigned soldier_warp_census = 0;  // census lines logged by this process (bounded)
mgscoop::soldier_warp::Gate soldier_warp_gate{};  // Lua owner thread only
// Crash-probe context flags: bit0 input window armed, bit1 DM patch applied.
std::uint32_t PartnerCtxFlags() noexcept {
    return (partner_driver.mission.input_armed ? 1u : 0u) | (partner_driver.mission.dm_patched ? 2u : 0u);
}

namespace upper_end = mgscoop::partner_upper_end_observer;
namespace upper_phase = mgscoop::partner_upper_phase_observer;
namespace upper_idle = mgscoop::partner_upper_manual_idle;
bool UpperPhaseRead(void*, std::uintptr_t address, void* out, std::size_t size) noexcept {
    SIZE_T copied = 0;
    return address && size && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), out, size, &copied) && copied == size;
}
struct UpperEndOwner {
    bool active = false;
    std::uint64_t generation = 0, body_epoch = 0;
    std::uint32_t mission = 0, thread = 0;
} upper_end_owner, upper_phase_owner, upper_idle_owner;
void ClearUpperIdleOwner() noexcept {
    upper_idle::Clear(); upper_idle_owner.active = false; upper_idle_requested = false;
}
void ClearUpperPhaseOwner() noexcept {
    upper_phase::Clear(); upper_phase_owner.active = false;
}
void ClearUpperEndOwner() noexcept {
    ClearUpperIdleOwner();
    if (!upper_end::Compiled()) return;
    // Revoke first. A worker already inside a CS wrapper cannot publish stale
    // readback into a later mission even if its original native call returns.
    ClearUpperPhaseOwner(); upper_end::Clear(); upper_end_owner.active = false;
}
template<std::size_t N> void UpperEndHex(const std::array<std::uint8_t, N>& input, char (&output)[2 * N + 1]) noexcept {
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < N; ++i) { output[2 * i] = digits[input[i] >> 4]; output[2 * i + 1] = digits[input[i] & 15]; }
    output[2 * N] = 0;
}
void DrainUpperIdleReceipts() noexcept {
    if (!upper_idle::Compiled() || !OnRuntimeThread()) return;
    upper_idle::Receipt r{};
    for (unsigned n = 0; n < upper_idle::kReceiptCap && upper_idle::Drain(r); ++n) {
        const auto& s = r.snapshot;
        char upper_record[81]{}, lower_record[81]{};
        UpperEndHex(s.upper.record, upper_record); UpperEndHex(s.lower.record, lower_record);
        const auto base = reinterpret_cast<std::uintptr_t>(game_base);
        const auto caller = r.update.caller >= base ? r.update.caller - base : 0;
        LogFormat("upper_manual_idle=receipt; command=%llu; gen=%llu; epoch=%llu; mission=%u; owner=%u; thread=%u; caller=%llx; stage=%u; t_ms=%llu; submit_attempted=%u; submit_rax=%llx; update_rax=%llx; refusal=%s; immediate_lower_unchanged=%u; idle_current=%u; noqueued=%u; automatic_capability=0; accepted=unverified; stopped=unverified\r\n",
            r.command, r.owner.generation, r.owner.body_epoch, r.owner.mission, r.owner.owner_thread, r.update.thread,
            static_cast<unsigned long long>(caller), unsigned(r.stage), r.now_ms, r.submit_attempted ? 1u : 0u,
            static_cast<unsigned long long>(r.raw_submit_rax), static_cast<unsigned long long>(r.raw_update_rax),
            r.refusal ? r.refusal : "none", r.immediate_lower_unchanged ? 1u : 0u,
            r.upper_idle_current ? 1u : 0u, r.upper_noqueued ? 1u : 0u);
        LogFormat("upper_manual_idle=readback; command=%llu; stage=%u; P=%llx; block=%llx; cs=%llx; entry=%llx; records=%llx; upper_cur=%u,%u,%llx; upper_queued=%u,%u,%llx; lower_cur=%u,%u,%llx; masks=%x,%x,%x,%x; upper_record=%s; lower_record=%s\r\n",
            r.command, unsigned(r.stage), s.identity.P, s.identity.block, s.identity.cs, s.identity.entry, s.identity.records,
            s.upper.current.index, unsigned(s.upper.current.type), s.upper.current.code,
            s.upper.queued.index, unsigned(s.upper.queued.type), s.upper.queued.code,
            s.lower.current.index, unsigned(s.lower.current.type), s.lower.current.code,
            unsigned(s.masks[0]), unsigned(s.masks[1]), unsigned(s.masks[2]), unsigned(s.masks[3]), upper_record, lower_record);
    }
}
void DrainUpperEndObservations() noexcept {
    if (!upper_end::Compiled() || !OnRuntimeThread()) return;
    upper_end::Observation o{};
    for (unsigned i = 0; i < upper_end::kObservationCap && upper_end::Drain(o); ++i) {
        const auto& s = o.snapshot; const auto& id = s.identity;
        const auto actual_thread = o.phase == upper_end::Phase::submission_before || o.phase == upper_end::Phase::submission_after
            ? o.submission.thread : o.update.thread;
        const auto actual_caller = o.phase == upper_end::Phase::submission_before || o.phase == upper_end::Phase::submission_after
            ? o.submission.caller : o.update.caller;
        LogFormat("upper_end_observe=receipt; phase=%u; serial=%llu; end=%llu; gen=%llu; epoch=%llu; mission=%u; t_ms=%llu; owner=%u; thread=%u; caller=%llx; rax=%llx; equip=%u; stance=%u; flags=%u; observe_only=1; acceptance=unverified; completion=unverified; stopped=unverified\r\n",
            unsigned(o.phase), o.serial, o.correlated_end, o.generation, o.body_epoch, o.owner.mission, o.now_ms,
            o.owner.owner_thread, actual_thread, static_cast<unsigned long long>(actual_caller - reinterpret_cast<std::uintptr_t>(game_base)),
            static_cast<unsigned long long>(o.original_rax), unsigned(o.owner.equip), unsigned(o.owner.stance), unsigned(o.owner.flags));
        LogFormat("upper_end_observe=identity; serial=%llu; P=%llx; Q=%llx; S=%llx; block=%llx; ps=%llx; cs=%llx; entry=%llx; allocation=%llx; controllers=%llx; buffer=%llx; records=%llx; lower=%llx; upper=%llx; bank=%llx; lower_table=%llx; upper_table=%llx; counts=%u,%u\r\n",
            o.serial, id.P, id.Q, id.S, id.block, id.ps, id.cs, id.entry, id.allocation, id.controllers, id.buffer,
            id.records, id.lower, id.upper, id.bank, id.lower_table, id.upper_table, id.lower_count, id.upper_count);
        LogFormat("upper_end_observe=upper; serial=%llu; cur=%u,%u,%llx,%llx; queued=%u,%u,%llx,%llx; alternate=%u,%u,%llx,%llx; Fire=%u,%u,%u; End=%u,%llx; entry_flags=%x; masks=%x,%x,%x,%x; slot=%u; channel=%u; blend=%08x; option=%u\r\n",
            o.serial, s.upper.current.index, unsigned(s.upper.current.type), s.upper.current.code, s.upper.current.pointer,
            s.upper.queued.index, unsigned(s.upper.queued.type), s.upper.queued.code, s.upper.queued.pointer,
            s.upper.alternate.index, unsigned(s.upper.alternate.type), s.upper.alternate.code, s.upper.alternate.pointer,
            s.fire_selector.index, unsigned(s.fire_selector.type), s.fire_selector.child_index, s.end_node.index, s.end_node.code,
            s.entry_flags, unsigned(s.masks[0]), unsigned(s.masks[1]), unsigned(s.masks[2]), unsigned(s.masks[3]),
            o.submission.slot190 ? 190u : 170u, o.submission.channel, o.submission.blend_bits, o.submission.option);
        char upper_flags[21]{}, lower_flags[21]{}, upper_record[81]{}, lower_record[81]{};
        UpperEndHex(s.upper.flags, upper_flags); UpperEndHex(s.lower.flags, lower_flags);
        UpperEndHex(s.upper.record, upper_record); UpperEndHex(s.lower.record, lower_record);
        LogFormat("upper_end_observe=records; serial=%llu; upper_flags=%s; lower_flags=%s; upper_record=%s; lower_record=%s; lower_cur=%u,%u,%llx; lower_queued=%u,%u,%llx; lower_alternate=%u,%u,%llx\r\n",
            o.serial, upper_flags, lower_flags, upper_record, lower_record,
            s.lower.current.index, unsigned(s.lower.current.type), s.lower.current.code,
            s.lower.queued.index, unsigned(s.lower.queued.type), s.lower.queued.code,
            s.lower.alternate.index, unsigned(s.lower.alternate.type), s.lower.alternate.code);
    }
}
void DrainUpperPhaseObservations() noexcept {
    if (!upper_phase::Compiled() || !OnRuntimeThread()) return;
    upper_phase::Observation o{};
    for (unsigned n = 0; n < upper_phase::kPairCap * 2 && upper_phase::Drain(o); ++n) {
        const auto& s = o.snapshot; const auto& id = s.identity;
        char upper_flags[21]{}, lower_flags[21]{}, upper_record[81]{}, lower_record[81]{};
        UpperEndHex(s.upper.flags, upper_flags); UpperEndHex(s.lower.flags, lower_flags);
        UpperEndHex(s.upper.record, upper_record); UpperEndHex(s.lower.record, lower_record);
        LogFormat("upper_phase_observe=receipt; body=1; after=%u; serial=%llu; gen=%llu; epoch=%llu; mission=%u; owner=%u; thread=%u; caller=%llx; t_ms=%llu; rax=%llx; observe_only=1; acceptance=unverified; completion=unverified; stopped=unverified\r\n",
            o.after ? 1u : 0u, o.serial, o.owner.generation, o.owner.body_epoch, o.owner.mission, o.owner.owner_thread, o.update.thread,
            static_cast<unsigned long long>(o.update.caller - reinterpret_cast<std::uintptr_t>(game_base)), o.now_ms, static_cast<unsigned long long>(o.original_rax));
        LogFormat("upper_phase_observe=identity; serial=%llu; P=%llx; Q=%llx; S=%llx; block=%llx; ps=%llx; cs=%llx; entry=%llx; allocation=%llx; controllers=%llx; buffer=%llx; records=%llx; lower=%llx; upper=%llx; bank=%llx; counts=%u,%u; entry_flags=%x; masks=%x,%x,%x,%x\r\n",
            o.serial, id.P, id.Q, id.S, id.block, id.ps, id.cs, id.entry, id.allocation, id.controllers, id.buffer, id.records,
            id.lower, id.upper, id.bank, id.lower_count, id.upper_count, s.entry_flags, unsigned(s.masks[0]), unsigned(s.masks[1]), unsigned(s.masks[2]), unsigned(s.masks[3]));
        LogFormat("upper_phase_observe=nodes; serial=%llu; upper_cur=%u,%u,%llx,%llx; upper_queued=%u,%u,%llx,%llx; upper_alt=%u,%u,%llx,%llx; lower_cur=%u,%u,%llx,%llx; lower_queued=%u,%u,%llx,%llx; lower_alt=%u,%u,%llx,%llx\r\n",
            o.serial, s.upper.current.index, unsigned(s.upper.current.type), s.upper.current.code, s.upper.current.pointer,
            s.upper.queued.index, unsigned(s.upper.queued.type), s.upper.queued.code, s.upper.queued.pointer,
            s.upper.alternate.index, unsigned(s.upper.alternate.type), s.upper.alternate.code, s.upper.alternate.pointer,
            s.lower.current.index, unsigned(s.lower.current.type), s.lower.current.code, s.lower.current.pointer,
            s.lower.queued.index, unsigned(s.lower.queued.type), s.lower.queued.code, s.lower.queued.pointer,
            s.lower.alternate.index, unsigned(s.lower.alternate.type), s.lower.alternate.code, s.lower.alternate.pointer);
        LogFormat("upper_phase_observe=records; serial=%llu; upper_flags=%s; lower_flags=%s; upper_record=%s; lower_record=%s; mask6_acceptance=0\r\n",
            o.serial, upper_flags, lower_flags, upper_record, lower_record);
    }
}
void PublishUpperEndOwner(const char* buffer, std::size_t size, const pd::Clock& clock, bool manual_trial, bool manual_safe) noexcept {
    // Drain terminal receipts before a failed lease refresh revokes them.
    DrainUpperIdleReceipts();
    if (!upper_end::Compiled()) return;
    const auto& mission = partner_driver.mission;
    const auto& settings = partner_driver.settings;
    unsigned short live_mission = 0; partner::Tick tick{};
    const bool context = mgscoop::partner_upper_probe::Installed() && offline_enabled && lab_active && lab::SavesRedirected() &&
        partner_on && partner_gate_ok && archive_mounted && host_source_selected && allocation_enabled && OnRuntimeThread() &&
        !mission_terminating && !termination_scope.Active() && !gameplay_restore_pending && !HasDynamiteSessionOwnership() &&
        ReadMission(live_mission) && live_mission == partner_mission && live_mission == allocation_mission &&
        mgscoop::SupportsPartnerBody(live_mission) && mission.cfg_ready && !mission.stopped &&
        mission.mission_code == live_mission && mission.realized && mission.parts_loaded && !mission.range_hidden &&
        !mission.range_needs_place && !mission.lost_hidden && !mission.native_disabled && !mission.input_faulted &&
        settings.stage >= 3 && partner::ParseTick(buffer, size, tick);
    if (!context) { ClearUpperEndOwner(); return; }
    if (upper_end_owner.active && (upper_end_owner.body_epoch != mission.body_epoch ||
        upper_end_owner.mission != live_mission || upper_end_owner.thread != mission_thread)) ClearUpperEndOwner();
    if (!upper_end_owner.active) {
        if (upper_end_owner.generation == UINT64_MAX) return;
        ++upper_end_owner.generation;
    }
    const upper_end::Owner owner{upper_end_owner.generation, mission.body_epoch, live_mission, mission_thread,
        tick.equip_id, tick.stance, tick.action_flags};
    if (!upper_end::PublishOwner(owner, GetCurrentThreadId(), clock.tick_ms)) { ClearUpperEndOwner(); return; }
    // A new manual reply renews only remote read-observation budget. The local
    // End lease/generation and every native playback permission remain unchanged.
    if (upper_phase::Compiled()) {
        if (manual_trial || (upper_phase_owner.active && (upper_phase_owner.body_epoch != mission.body_epoch ||
            upper_phase_owner.mission != live_mission || upper_phase_owner.thread != mission_thread))) ClearUpperPhaseOwner();
        bool phase_ok = upper_phase::Installed();
        if (!upper_phase_owner.active) {
            if (upper_phase_owner.generation == UINT64_MAX) phase_ok = false;
            else ++upper_phase_owner.generation;
        }
        auto phase_owner = owner; phase_owner.generation = upper_phase_owner.generation;
        phase_ok = phase_ok && upper_phase::PublishGhostOwner(phase_owner, GetCurrentThreadId(), clock.tick_ms);
        if (phase_ok) {
            upper_phase_owner.active = true; upper_phase_owner.body_epoch = mission.body_epoch;
            upper_phase_owner.mission = live_mission; upper_phase_owner.thread = mission_thread;
            if (manual_trial) LogFormat("upper_phase_observe=manual_window; gen=%llu; epoch=%llu; owner=%u; read_only=1; native_submission=0\r\n",
                phase_owner.generation, phase_owner.body_epoch, phase_owner.owner_thread);
        } else ClearUpperPhaseOwner();
    }
    if (upper_idle::Compiled()) {
        if (!manual_safe || !upper_phase::Installed()) ClearUpperIdleOwner();
        else {
            if (upper_idle_owner.active && (upper_idle_owner.body_epoch != mission.body_epoch ||
                upper_idle_owner.mission != live_mission || upper_idle_owner.thread != mission_thread)) ClearUpperIdleOwner();
            bool idle_ok = true;
            if (!upper_idle_owner.active) {
                if (upper_idle_owner.generation == UINT64_MAX) idle_ok = false;
                else ++upper_idle_owner.generation;
            }
            auto idle_owner = owner; idle_owner.generation = upper_idle_owner.generation;
            idle_ok = idle_ok && upper_idle::PublishOwner(idle_owner, GetCurrentThreadId(), clock.tick_ms);
            if (!idle_ok) ClearUpperIdleOwner();
            else {
                upper_idle_owner.active = true; upper_idle_owner.body_epoch = mission.body_epoch;
                upper_idle_owner.mission = live_mission; upper_idle_owner.thread = mission_thread;
                if (upper_idle_requested) {
                    upper_idle_requested = false;
                    const bool armed = upper_idle_nonce != UINT64_MAX && upper_idle::Arm(++upper_idle_nonce, GetCurrentThreadId(), clock.tick_ms);
                    LogFormat("upper_manual_idle=arm; command=%llu; gen=%llu; armed=%u; body=1; channel=2; state=c1a2050d3802; blend=0; option=0; dm_changed=0; automatic_capability=0\r\n",
                        upper_idle_nonce, idle_owner.generation, armed ? 1u : 0u);
                }
            }
        }
    }
    upper_end_owner.active = true; upper_end_owner.body_epoch = mission.body_epoch;
    upper_end_owner.mission = live_mission; upper_end_owner.thread = mission_thread;
    // Independent of DM replies, finite lower clips, manual probe TLS and
    // remote event consumption. This publishes read authority only.
    DrainUpperEndObservations(); DrainUpperPhaseObservations();
}

// The newest compact pack-state receipt (lua/coop/player_package_selection.lua):
// mission << 8 | scene for a refusal, 0 after clear/selection. Capped diagnostics
// cannot set state. The newest request wins, including a callback that throws.
// PartnerMissionStart consumes it once (docs/player-package-selection.md).
std::atomic<std::uint32_t> partner_pack_refused{0};
void NotePackReceipt(const char* buffer, std::size_t size) noexcept {
    if (partner::ClassifyPackState(buffer, size).kind != partner::PackReceipt::none)
        partner_pack_refused.store(partner::UpdatePackState(0, buffer, size));
}

void PartnerMissionStart(std::uint32_t mission) noexcept {
    PartnerSleepResetBridge();
    buddy_pose_owner = {}; ClearBuddyCapacityIdentity(); mgscoop::dog_guard::Disarm("mission_start");
    mgscoop::coop_hud::PublishDowned(mgscoop::coop_hud::DownedView{});
    mgscoop::downed_player::Configure(partner_addresses, downed_hook_ok);
    mgscoop::teammate_targeting::SetEnabled(partner_on && partner_gate_ok);
    ClearUpperEndOwner();
    const std::uint32_t pack = partner_pack_refused.exchange(0);
    if (!partner_on) return;
    mgscoop::debug_menu::BeginMission(mission);
    pd::MissionStart(partner_driver, partner_hooks, mission);
    mgscoop::partner_net::ResetRemote();
    mgscoop::partner_net::SetEpoch(mission);
    partner_mission = mission;
    // A partner mission built on a refused pack list has one player slot: say
    // so (toast, link-status partner_reason no_body_pack) instead of partner=none.
    const bool no_body = pack != 0 && (pack >> 8) == mission && mgscoop::SupportsPartnerBody(mission);
    mgscoop::partner_net::PublishPackState(no_body ? mission : 0);
    mgscoop::coop_hud::PublishPartner(mgscoop::coop_hud::PartnerView{});
    mgscoop::partner_net::PublishPartnerState(false, false, mission);
    if (no_body) {
        const char* scene = partner::PackSceneName(static_cast<partner::PackScene>(pack & 0xFFu));
        LogFormat("partner_pack=no_body; mission=%u; scene=%s\r\n", mission, scene[0] ? scene : "other");
        mgscoop::coop_hud::Post(mgscoop::coop_hud::EventKind::no_body, static_cast<std::int32_t>(mission), scene);
    }
    mgscoop::crash_probe::SetContext(partner_driver.settings.stage, 0, PartnerCtxFlags());
    mgscoop::nametag_game::MissionStart(mission);
}
void PartnerMissionEnd() noexcept {
    PartnerSleepResetBridge();
    buddy_pose_owner = {}; ClearBuddyCapacityIdentity(); mgscoop::dog_guard::Disarm("mission_end");
    mgscoop::coop_hud::PublishDowned(mgscoop::coop_hud::DownedView{});
    mgscoop::downed_player::Shutdown();
    mgscoop::teammate_targeting::SetEnabled(false);
    ClearUpperEndOwner();
    if (!partner_on) return;
    mgscoop::debug_menu::EndMission();
    mgscoop::nametag_game::MissionEnd();
    pd::MissionEnd(partner_driver, partner_hooks);
    mgscoop::partner_net::ResetRemote();
    mgscoop::partner_net::SetEpoch(0);
    partner_mission = 0;
    mgscoop::partner_net::PublishPackState(0);
    mgscoop::coop_hud::PublishPartner(mgscoop::coop_hud::PartnerView{});
    mgscoop::partner_net::PublishPartnerState(false, false, 0);
    mgscoop::crash_probe::SetContext(partner_driver.settings.stage, 0, PartnerCtxFlags());
}

// The game window is closing (debug_overlay's WndProc, first WM_CLOSE/WM_DESTROY/
// WM_ENDSESSION): disarm the per-mission native hooks exactly as a MissionEnd
// does, before the engine destroys the objects the deferred block-1 job, the
// drain window and the rate/magazine mirrors name. Main (mission) thread only;
// a run that intermittently crashed at close (guest, 0x21E72 write to 0x38)
// had the native drive still armed.
void PartnerShutdown() noexcept {
    InterlockedExchange(&partner_window_closing, 1);  // the early manifest stops in any mission
    mgscoop::carry_info::Shutdown();
    mgscoop::sleep_info::Shutdown();
    mgscoop::vehicle_info::Shutdown();
    mgscoop::nametag::Shutdown();  // thread-safe flag: the overlay stops drawing the tag
    // Save handoff (docs/save-handoff.md): flags only; the hook thread sends
    // ABORT shutdown when mid-transfer and frees the snapshot.
    mgscoop::save_handoff::Shutdown();
    // Steam online exit hygiene (docs/steam-online.md C11): flags only. The
    // transport thread sends one CLOSE, closes the peer session and parks;
    // the owner context clears the guest beacon once and stops calling Steam.
    mgscoop::partner_net::Shutdown();
    mgscoop::steam_online::Shutdown();
    if (!partner_on || !partner_mission) return;
    // Thread-safe first: no deferral, no window, no mirror write from here on.
    mgscoop::partner_defer::Shutdown();
    mgscoop::partner_input_hook::Shutdown();
    mgscoop::partner_aim_point::Shutdown();
    // F1 vehicles: the board wrappers fall back to the stock searches (an atomic store).
    mgscoop::partner_board::Publish(0);
    // F1 passenger camera: nothing to undo (no patch; the handle is cleared by the stock get-off / FinishState).
    mgscoop::partner_gesture::Shutdown();  // the body-1 update hook passes through: no gesture write
    mgscoop::partner_ui_filter::Shutdown();  // G1: the hint-queue leaf hooks pass every request through
    if (mission_thread && mission_thread != GetCurrentThreadId()) {
        LogFormat("partner_shutdown=hooks_off; teardown=other_thread; thread=%lu; mission_thread=%lu\r\n", GetCurrentThreadId(), mission_thread);
        return;
    }
    Log("partner_shutdown=hooks_off; teardown=mission_end\r\n");
    PartnerMissionEnd();
}

// World sync install (lab.ini world>=1; partner=1 and net=1 already hold).
// Logs world=configured (world_link) or world=disabled with the reason.
void ConfigureWorld(const mgscoop::LabConfig& labc, const wchar_t* directory) noexcept {
    if (!labc.world) return;
    const char* reason = labc.net_link != mgscoop::NetLink::v2 ? "legacy_link" : !lab::SavesRedirected() ? "saves_not_redirected" : nullptr;
    if (reason) {
        LogFormat("world=disabled; reason=%s; t_ms=%llu\r\n", reason, static_cast<unsigned long long>(QpcMicros() / 1000));
        return;
    }
    mgscoop::world_link::Config world{};
    world.role = labc.role == mgscoop::LabRole::guest ? 2 : 1;
    world.level = static_cast<std::uint8_t>(labc.world);
    world.play = labc.mode == mgscoop::LabMode::play;
    world.probe = labc.world_probe;
    world.directory = directory;
    world.lab_triggers = lab_active && labc.mode == mgscoop::LabMode::lab;
    world.saves = 1;
    world.build = mgscoop::world_link::ParseBuild(lab::ProbeVersion());
    world.log = Log;
    world.placed_census = labc.partner_placed_census && labc.mode == mgscoop::LabMode::lab;
    world.vehicle_gone = labc.world_vehicle_gone;
    world.companion = labc.companion_host_owned;
    world.relief = labc.world_relief;
    world.vehicle_state = labc.world_vehicle_state;
    world.vehicle_wreck = labc.world_vehicle_wreck;
    world.vehicle_break = labc.world_vehicle_break;
    world.vehicle_credit = labc.world_vehicle_credit;
    world.loadout_block = labc.world_loadout_block;
    world.vehicle_crew = labc.world_vehicle_crew;
    world.vehicle_damage = labc.world_vehicle_damage;
    mgscoop::world_link::Configure(world);
    mgscoop::partner_net::SetWorldHooks(mgscoop::world_link::Hooks());
    world_on = true;
}

// Automatic save handoff install (docs/save-handoff.md; after ConfigureWorld,
// before partner_net::Start). Always records the role and the travel lock
// (CFG r[13]); the transfer configures only with lab.ini save_handoff set,
// link v2, redirected saves and world on. Logs save_handoff=configured or
// save_handoff=disabled; the hooks are set only when configured.
void ConfigureSaveHandoff(const mgscoop::LabConfig& labc, const wchar_t* directory) noexcept {
    char save_dir[260]{};
    if (!lab::save::current_save_dir(save_dir, sizeof(save_dir))) save_dir[0] = 0;
    mgscoop::save_handoff::Config handoff{};
    handoff.role = labc.role == mgscoop::LabRole::guest ? 2 : 1;
    handoff.mode = labc.save_handoff;
    handoff.play = labc.mode == mgscoop::LabMode::play;
    handoff.net = labc.net;
    handoff.link_v2 = labc.net_link == mgscoop::NetLink::v2;
    handoff.saves_redirected = lab::SavesRedirected();
    handoff.world_on = world_on;
    handoff.travel_lock = mgscoop::EffectiveTravelLock(labc);
    handoff.steam_wait = labc.net_steam_wait;
    handoff.save_dir = save_dir;
    handoff.directory = directory;
    handoff.log = Log;
    if (mgscoop::save_handoff::Configure(handoff)) mgscoop::partner_net::SetHandoffHooks(mgscoop::save_handoff::Hooks());
}

const char* HandlePartnerCfg() noexcept {
    static constexpr char yes[]="return true", no[]="return false";
    unsigned short mission = 0;
    const char* precheck = nullptr;
    if (!OnRuntimeThread() || mission_terminating) precheck = "not_runtime_thread";
    else if (!ReadMission(mission) || !mgscoop::SupportsPartnerBody(mission)) precheck = "mission";
    else if (!partner_gate_ok) precheck = "code_gate";
    if (!pd::Cfg(partner_driver, partner_hooks, precheck, mgscoop::partner_net::NowMs())) return no;
    mgscoop::partner_net::SetEpoch(mission);
    return yes;
}

const char* HandlePartnerLog(const char* buffer, std::size_t size) noexcept {
    static constexpr char yes[]="return true";
    if (!OnRuntimeThread() && !OnTerminationThread()) return nullptr;
    pd::LuaLog(partner_driver, partner_hooks, buffer, size);
    return yes;
}

// The debug seat_dm lever (lua/coop/debug_vehicle.lua): arm or end the manual seat.
const char* HandlePartnerSeat(const char* buffer, std::size_t size) noexcept {
    static constexpr char yes[]="return true";
    static constexpr char no[]="return false";
    if (!OnRuntimeThread()) return nullptr;
    partner::SeatRequest request{};
    if (!partner::ParseSeatReceipt(buffer, size, request)) return no;
    return pd::SeatManual(partner_driver, partner_hooks, request.arm, request.seat, request.vehicle, mgscoop::partner_net::NowMs(),
                          mission_terminating) ? yes : no;
}

const char* HandlePartnerStop() noexcept {
    static constexpr char yes[]="return true";
    // Sent from OnTerminate after MISSION_TERMINATING: allowed while terminating.
    if (!OnRuntimeThread() && !OnTerminationThread()) return nullptr;
    ClearUpperEndOwner();
    pd::Stop(partner_driver, partner_hooks, mgscoop::GetLabConfig().partner_teardown, mission_terminating);
    mgscoop::partner_net::ClearLocalActions();
    mgscoop::partner_net::ResetRemote();
    mgscoop::crash_probe::SetContext(0, partner_driver.mission.realized ? 1u : 0u, PartnerCtxFlags());
    return yes;
}

void ObserveRemoteActionEvents() noexcept {
    if (!mgscoop::partner_net::ActionEventsCompiled() || !OnRuntimeThread()) return;
    mgscoop::partner_net::ActionSample sample{};
    if (!mgscoop::partner_net::SampleActions(partner_mission, sample) || !sample.fresh) return;
    const auto& event = sample.receipt;
    if (event.status == mgscoop::partner_actions::ReceiptStatus::duplicate ||
        event.status == mgscoop::partner_actions::ReceiptStatus::rejected) return;
    static std::uint32_t generation = 0, mission = 0, flags = UINT32_MAX;
    static unsigned lines = 0;
    if (mission != event.context.mission || generation != event.context.generation) {
        mission = event.context.mission; generation = event.context.generation;
        flags = UINT32_MAX; lines = 0;
    }
    const bool changed = flags != event.flags;
    flags = event.flags;
    if (!changed && !event.count && !event.expired &&
        event.status != mgscoop::partner_actions::ReceiptStatus::history_gap) return;
    if (lines++ >= 96) return;
    LogFormat("partner_action_events=rx; mission=%u; generation=%u; equip=%u; stance=%u; flags=0x%x; sequence=%u; status=%u; count=%u; expired=%u; age_ms=%u; fire_source=%u; reload_source=%u; engine_accepted=unverified\r\n",
        unsigned(mission),unsigned(generation),unsigned(event.context.equip),unsigned(event.context.stance),
        unsigned(event.flags),unsigned(event.sequence),unsigned(event.status),unsigned(event.count),unsigned(event.expired),
        unsigned(sample.age_ms),unsigned(event.context.fire_source),unsigned(event.context.reload_source));
    for (unsigned i=0;i<event.count;++i)
        LogFormat("partner_action_event=rx; mission=%u; generation=%u; token=%u; tick=%u; kind=%u; engine_accepted=unverified\r\n",
            unsigned(mission),unsigned(generation),unsigned(event.events[i].token),unsigned(event.events[i].tick_ms),unsigned(event.events[i].kind));
}

// Partner name tag (docs/partner-nametag.md): read-only, after the driver tick
// on the same owner thread. The partner's visibility comes from the driver.
void NametagTick(const char* buffer, std::size_t size) noexcept {
    if (!mgscoop::nametag_game::Ready()) return;
    namespace nt = mgscoop::nametag;
    const auto& m = partner_driver.mission;
    const auto& s = partner_driver.settings;
    nt::Reason hidden = nt::Reason::ok;
    if (!m.cfg_ready || m.stopped || s.stage < 3 || !m.realized || !m.parts_loaded) hidden = nt::Reason::no_partner;
    else if (m.range_hidden || m.range_needs_place) hidden = nt::Reason::range;
    else if (m.lost_hidden) hidden = nt::Reason::lost;
    partner::Tick tick{};
    const bool aim = partner::ParseTick(buffer, size, tick) && (tick.action_flags & mgscoop::sync::ActionFlag::Aiming) != 0;
    // Body 1 seated in a vehicle by the seat flow: the tag uses the seated head height.
    const bool seated = m.ride_seated && m.seat.phase == mgscoop::partner_driver::SeatPhase::seated;
    mgscoop::nametag_game::Tick({&partner_addresses, OnRuntimeThread(), mission_terminating, hidden, mgscoop::debug_menu::LuaHud(),
        mgscoop::partner_net::EffectiveInterpMs(mgscoop::GetLabConfig().partner_interp_ms), aim, seated});
}

// The per-frame tick. Returns the quiet reply, or nullptr to leave the literal
// (Lua then does nothing). The guarded native calls of the movement/stance
// experiments (T.vt+0x70/+0x68, the C0/A0 resets, SetStance, X.vt+0x10) run
// here, inside ObserveEntry with receipt_lock held. A callee that reached
// luaL_loadbuffer on this thread cannot re-enter ObserveEntry: the bridge
// (script_load_bridge.cpp Bridge) forwards any load issued while its selector
// runs unchanged, without calling the selector (thread_local selecting;
// tests/script_load_bridge_tests.cpp selector_reentry), so the
// non-recursive lock is never requested twice on one thread.
void ObserveEquipmentTick() noexcept;
void CoopRescueTick() noexcept;
bool PhysicalScope(mgscoop::carry_plugin_read::Scope&) noexcept;
bool PhysicalCallbackSample(void*,const mgscoop::horse_physical_callback_probe::RawWindow&,mgscoop::horse_physical_callback_probe::Sample&) noexcept;
void RefreshPhysicalProbe() noexcept;
bool physical_probe_prepared=false;
bool SleepAssetLine(char*,std::size_t) noexcept;
bool StandingCycleAssetLine(char*,std::size_t) noexcept;
void PublishPhysicalDual(const mgscoop::partner_input_hook::NativePoseDualPartsContext&) noexcept;
bool HorseCargoNativeDiagnostic(std::uint16_t,unsigned,char*,std::size_t) noexcept;
bool HorseCargoPairDiagnostic(std::uint16_t,char*,std::size_t) noexcept;
bool PrologueProviderLine(char*,std::size_t) noexcept;
bool PhysicalTransformLine(char*,std::size_t) noexcept;


const char* HandlePartnerTick(const char* buffer, std::size_t size) noexcept {
    if (!OnRuntimeThread()) return nullptr;
    const std::uint64_t started_us = QpcMicros();
    const auto upper_body_epoch = partner_driver.mission.body_epoch;
    partner::Reply reply{};
    const pd::Clock clock{mgscoop::partner_net::NowMs(), GetTickCount64()};
    if (!pd::Tick(partner_driver, partner_hooks, mgscoop::GetLabConfig(), buffer, size, clock, mission_terminating, reply)) {
        ClearUpperEndOwner(); mgscoop::coop_hud::PublishPartner(mgscoop::coop_hud::PartnerView{}); return nullptr;
    }
    if(PartnerSleepSuppressed(nullptr))reply.op&=~(partner::op::dm_play|partner::op::dm_stop|partner::op::ride_seat);
    CoopRescueTick();
    mgscoop::coop_carry::Tick();
    mgscoop::coop_sleep::Tick();
    ObserveRemoteActionEvents();
    NametagTick(buffer, size);
    // Service equipment on the natural partner Lua owner tick, independently
    // of the menu module and its snapshot/poll receipts.
    ObserveEquipmentTick();
    if (!partner::FormatReply(reply, partner_reply, sizeof(partner_reply))) {
        ClearUpperEndOwner(); mgscoop::partner_upper_probe::Clear("reply_format");
        mgscoop::coop_hud::PublishPartner(mgscoop::coop_hud::PartnerView{}); return nullptr;
    }
    const auto manual_invalid_ops = partner::op::dm_play | partner::op::dm_stop | partner::op::warp |
        partner::op::set_parts_explicit | partner::op::set_all_active;
    const bool manual_idle_safe = partner_driver.mission.body_epoch == upper_body_epoch &&
        !partner_driver.mission.dmmap_prev_look && !partner_driver.mission.look_verify && !partner_driver.mission.input_armed &&
        partner_driver.settings.input == mgscoop::PartnerInput::off && partner_driver.settings.anim == mgscoop::PartnerAnim::off &&
        !(reply.op & manual_invalid_ops);
    PublishUpperEndOwner(buffer, size, clock,
        partner_driver.settings.dm == mgscoop::PartnerDm::on && (reply.op & partner::op::dm_play), manual_idle_safe);
    const auto& upper_mission = partner_driver.mission;
    const auto& upper_settings = partner_driver.settings;
    const bool upper_context = !mission_terminating && upper_mission.cfg_ready && !upper_mission.stopped &&
        upper_mission.mission_code == partner_mission && upper_mission.realized && upper_mission.parts_loaded &&
        !upper_mission.range_hidden && !upper_mission.range_needs_place && !upper_mission.lost_hidden &&
        !upper_mission.native_disabled && !upper_mission.input_faulted && !upper_mission.input_armed &&
        upper_settings.stage >= 3 && upper_settings.dm == mgscoop::PartnerDm::on &&
        upper_settings.input == mgscoop::PartnerInput::off && upper_settings.anim == mgscoop::PartnerAnim::off;
    const auto upper_invalid_ops = partner::op::dm_stop | partner::op::warp | partner::op::set_parts_explicit |
                                   partner::op::set_all_active;
    const bool upper_look_changed = upper_mission.dmmap_prev_look;
    if (!upper_context || upper_mission.body_epoch != upper_body_epoch || upper_look_changed || (reply.op & upper_invalid_ops))
        mgscoop::partner_upper_probe::Clear("owner_context");
    if (upper_context && !upper_look_changed && !(reply.op & upper_invalid_ops) && (reply.op & partner::op::dm_play) &&
        mgscoop::partner_upper_probe::RequestCode(reply.gani)) {
        // Unique request generation spans mission epochs; only the established
        // natural mission owner reaches this branch. Overflow disables arming.
        static std::uint64_t upper_request_generation = 0;
        if (upper_request_generation != UINT64_MAX)
            mgscoop::partner_upper_probe::ArmRequest(++upper_request_generation, partner_mission, mission_thread, reply.gani,
                mgscoop::partner_upper_probe::RewriteCompiled() &&
                upper_mission.dmmap.phase == mgscoop::partner_dmmap::Phase::idle && !upper_mission.dmmap.owned);
        else mgscoop::partner_upper_probe::Clear("generation_exhausted");
    }
    // Small copied status publication from the natural owner, independent of
    // F9/debug snapshots. Never reads game state on Present or network threads.
    {
        namespace hud = mgscoop::coop_hud;
        namespace slots = mgscoop::partner_slots;
        const auto& m = partner_driver.mission;
        const auto& t = m.tele;
        const auto link = mgscoop::partner_net::GetLinkInfo();
        hud::PartnerFacts facts{};
        facts.session = link.session; facts.mission = partner_mission; facts.publish_ms = clock.tick_ms;
        facts.body_ready = !mission_terminating && m.cfg_ready && !m.stopped && m.realized && m.parts_loaded &&
            !m.range_hidden && !m.range_needs_place && !m.lost_hidden;
        facts.native_selected = partner_driver.settings.drive == mgscoop::PartnerDrive::native;
        facts.native_active = m.native_out.mode == mgscoop::partner_native::Mode::active ||
            m.native_out.mode == mgscoop::partner_native::Mode::entering;
        facts.gun_enabled = facts.native_selected && partner_driver.settings.ngun;
        facts.fresh_gun = clock.tick_ms != 0 && t.gun_tick_ms == clock.tick_ms;
        facts.gun_fields_ok = t.gun_read_ok && t.gun_fields_ok;
        facts.gun_class = t.gun_class; facts.body_equip = t.body_equip;
        facts.switching = t.body_switching;
        facts.remote_live = t.input_live && m.remote_gun_equip != mgscoop::partner_native::kNull && m.remote_gun_equip > 0;
        facts.gate_current = t.weapon_gate_current && facts.remote_live;
        facts.gate = static_cast<std::uint8_t>(m.weapon_gate);
        // TickWeapons judges before TickSlots: recheck the current copied plan
        // and handle so a slot transition cannot report an old match/substitute.
        std::uint8_t r = m.remote_v5 && m.remote_slot < 3 ? m.remote_slot : slots::kNoSlot;
        if (r >= 3 && m.equip.have && facts.remote_live)
            for (std::uint8_t i = 0; i < 3; ++i)
                if (m.equip.manifest.slots[i].alias == static_cast<std::uint32_t>(m.remote_gun_equip) ||
                    m.equip.manifest.slots[i].base_id == static_cast<std::uint32_t>(m.remote_gun_equip)) { r = i; break; }
        const std::uint8_t s = r < 3 && m.slot_plan_ok ? m.slot_plan.map[r] : slots::kNoSlot;
        const auto handle = s < 3 && m.slots1_ok ? slots::Decode(m.slots1.handles[s]) : slots::Handle{};
        const bool current_slot = s < 3 && m.slots1_ok && m.slots1.nibble == s && handle.filled &&
            handle.equip == t.body_equip && m.slot_plan.equip[s] == t.body_equip;
        facts.substitute = current_slot && r == s && m.slot_plan.tier[r] == slots::Tier::sub && m.slot_plan.equip[r] == t.body_equip;
        facts.standin = current_slot && r != s && m.weapon_standin &&
            m.slot_plan.tier[r] != slots::Tier::same && m.slot_plan.tier[r] != slots::Tier::sub;
        if ((m.weapon_gate == slots::Gate::match || m.weapon_gate == slots::Gate::standin) && !current_slot) facts.gate_current = false;
        hud::PublishPartner(hud::MakePartnerView(facts));
    }
    mgscoop::crash_probe::SetContext(partner_driver.settings.stage, partner_driver.mission.realized ? 1u : 0u, PartnerCtxFlags());
    mgscoop::partner_net::PublishPartnerState(partner_driver.mission.realized, partner_driver.mission.parts_loaded, partner_mission);
    {
        // Online play: how each PC shows the partner body (link-status.json).
        const auto& net = partner_driver.mission.tele.net;
        mgscoop::partner_net::PublishNativeState(static_cast<std::uint8_t>(net.local_drive),
            mgscoop::partner_native_net::ReasonCode(net.local_reason), static_cast<std::uint8_t>(net.remote_drive),
            mgscoop::partner_native_net::ReasonCode(net.remote_reason));
    }
    const std::uint64_t ended_us = QpcMicros();
    mgscoop::partner_net::RecordNativeMicros(Clamp32(ended_us >= started_us ? ended_us - started_us : 0));
    return partner_reply;
}

// The gun pieces (offline equipment groups, the stock46 preload and writer,
// the generalized preload and writer; compiled into research and friend
// builds) act only for a configured native gun profile: lab.ini
// partner_drive=native with partner_native_gun=1 (the worker's gun stage,
// Play-OfflineCoop's default, play's partner_native_profile=gun after its
// expansion). A clip partner (-NoNative, Play-Coop -Partner clips) leaves the
// game's equipment untouched; docs/two-pc-play.md "Native partner".
[[maybe_unused]] bool PartnerGunPiecesWanted() noexcept {
    const auto& lab = mgscoop::GetLabConfig();
    return lab.partner_drive == mgscoop::PartnerDrive::native && lab.partner_native_gun;
}

// --- The generalized equipment pieces (docs/native-puppet.md "Generalized
// writer", "Release gating and play mode") ---------------------------------
// What body 1 carries (lab.ini partner_native_equip): stock46 runs the
// qualified stock46 preload and writer, manifest|substitute the generalized ones.
[[maybe_unused]] mgscoop::PartnerNativeEquip EquipMode() noexcept { return mgscoop::GetLabConfig().partner_native_equip; }
// Admission of the generalized preload and writer: the gun profile with the
// generalized setting, the offline-v1 branch on the redirected saves (play
// refuses to run without them), and either the one-PC lab (mode=lab) or
// two-PC play (mode=play) with a paired link v2. The exact-build pins, the
// owner threads, the allocation receipts and every native preflight follow.
// nullptr when admitted, else the first failing admission (a static reason the
// writer and preload report). The window closing (PartnerShutdown) ends it too.
const char* EquipAdmissionReason() noexcept {
    const auto& labc = mgscoop::GetLabConfig();
    if (!PartnerGunPiecesWanted()) return "gun_profile";
    if (labc.partner_native_equip == mgscoop::PartnerNativeEquip::stock46) return "setting_stock46";
    if (!offline_enabled) return "offline_branch";
    if (!lab_active) return "lab_inactive";
    if (!lab::SavesRedirected()) return "saves_not_redirected";
    if (!partner_on) return "partner_off";
    if (!partner_gate_ok) return "code_gate";
    if (mgscoop::partner_defer::ShuttingDown()) return "shutting_down";
    if (labc.mode == mgscoop::LabMode::play)
        return mgscoop::partner_net::GetLinkInfo().state == mgscoop::link::LinkState::connected ? nullptr : "link_not_connected";
    return labc.mode == mgscoop::LabMode::lab ? nullptr : "mode";
}
bool EquipPiecesAdmitted() noexcept { return EquipAdmissionReason() == nullptr; }
// The manifest the generalized pieces plan from: the mission store's (seeded
// at MissionStart from the session cache, replaced by any newer revision) or
// the last one accepted in the same link session (the preload runs at the
// allocation, before the mission; the sender sends from the title and the ACC
// too, so the first mission after connecting has it). Never another session's:
// THIS copy's link session the manifest arrived in (pd::RemoteManifest), never
// the sender's own session number (0.13.5 online: they differ after a relaunch).
bool EquipManifest(mgscoop::partner_equip_wire::Manifest& out) noexcept {
    return pd::RemoteManifest(partner_driver, mgscoop::partner_net::GetLinkInfo().session, out);
}
// The early manifest (docs/native-puppet.md "Loadout mirroring"): on the
// observer's ~100 ms path of ANY mission (the title and the ACC included),
// drain channel 2 and run this copy's loadout sender. Reads vars and the
// type tables only (no partner state); the partner code gate passed at
// configure time. Runtime thread, under receipt_lock. Nothing once the window
// closes (partner_window_closing: also at the title, where the partner-mission
// shutdown flag partner_defer::ShuttingDown never gets set).
void ObserveEquipLink() noexcept {
    if (!partner_on || !partner_gate_ok || InterlockedCompareExchange(&partner_window_closing, 0, 0) ||
        mgscoop::partner_defer::ShuttingDown()) return;
    pd::TickEquipLink(partner_driver, partner_hooks, mgscoop::partner_net::NowMs());
}

void ObserveEquipmentVtables() noexcept {
#ifdef MGS_LOCAL_OFFLINE_EQUIPMENT_TEST
    static std::uint32_t observed_gen=UINT32_MAX;
    static std::uintptr_t observed_player=0,observed_buffer=0;
    const auto& mission=partner_driver.mission;
    if(!offline_enabled || !lab_active || !lab::SavesRedirected() ||
       !partner_gate_ok || !OnRuntimeThread() || mission_terminating || gameplay_restore_pending ||
       HasDynamiteSessionOwnership() || !mgscoop::SupportsPartnerBody(partner_mission) ||
       !mission.parts_loaded || !mission.realized) return;
    pp::State state{};
    pp::DmTargets targets{};
    if(pp::ReadState(partner_addresses,state)!=pp::Refusal::none || !state.arrays_ok ||
       state.count!=2 || state.local!=0 || state.ids[1]!=1 || !pp::Realized(state,1) ||
       state.parts[1]!=3 || pp::ResolveDm(partner_addresses,state,targets)) return;
    const auto read=[](std::uintptr_t at,std::uintptr_t& value) noexcept {
        value=0; SIZE_T count=0;
        return at && at<=UINTPTR_MAX-sizeof(value) &&
            ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),&value,sizeof(value),&count) && count==sizeof(value);
    };
    const auto base=reinterpret_cast<std::uintptr_t>(game_base);
    const auto rva=[&](std::uintptr_t at) noexcept { return at>=base && at-base<game_span ? at-base : 0u; };
    const auto method=[&](std::uintptr_t object,std::size_t slot,std::uintptr_t& vt,std::uintptr_t& fn) noexcept {
        std::uintptr_t table=0,target=0; vt=fn=0;
        if(!read(object,table) || !rva(table) || table>UINTPTR_MAX-slot || !read(table+slot,target)) return;
        vt=rva(table); fn=rva(target);
    };
    std::uintptr_t resource=0,resource_vt=0,resource_fn=0,blocks=0,block_buffer=0;
    if(state.P>UINTPTR_MAX-0xa20 || !read(state.P+0x48,resource) || !read(state.P+0x50,blocks) ||
       !read(state.P+0x378,block_buffer) || !resource || !blocks || !block_buffer ||
       block_buffer>UINTPTR_MAX-0x6a8) return;
    method(resource,0x1b8,resource_vt,resource_fn);
    if(!resource_vt || !resource_fn) return;
    // Exact ChangeEquip reads table[pool] (11AC35E). ResolveDm independently
    // validates remote block identity. Only log local/remote methods; no call.
    if(observed_gen==mission.realize_gen && observed_player==state.P && observed_buffer==block_buffer) return;
    observed_gen=mission.realize_gen;observed_player=state.P;observed_buffer=block_buffer;
    for(unsigned i=0;i<2;++i) {
        if(!pp::Realized(state,i) || state.pool[i]>=2 || state.ids[i]!=i || state.parts[i]!=3) return;
        std::uintptr_t block=0;
        if(blocks>UINTPTR_MAX-8u*state.pool[i] || !read(blocks+8u*state.pool[i],block) || !block ||
           block!=block_buffer+std::uintptr_t(state.pool[i])*0x6a8 ||
           (i==1 && block!=targets.blk1) || block>UINTPTR_MAX-0xf8) return;
        std::uintptr_t predicate=0,writer=0,predicate_vt=0,predicate_fn=0,writer_vt=0,writer_fn=0;
        read(block+0x70,predicate); read(block+0xf0,writer);
        method(predicate,0x30,predicate_vt,predicate_fn); method(writer,0x28,writer_vt,writer_fn);
        LogFormat("equipment_vtables; epoch=%u; instance=%u; id=%u; pool=%u; resource_vt=%llx; resource_1b8=%llx; predicate_vt=%llx; predicate_30=%llx; writer_vt=%llx; writer_28=%llx; read_only=1; remote_readiness=unverified\r\n",
            unsigned(mission.body_epoch),i,unsigned(state.ids[i]),unsigned(state.pool[i]),
            static_cast<unsigned long long>(resource_vt),static_cast<unsigned long long>(resource_fn),
            static_cast<unsigned long long>(predicate_vt),static_cast<unsigned long long>(predicate_fn),
            static_cast<unsigned long long>(writer_vt),static_cast<unsigned long long>(writer_fn));
    }
#endif
}


// Separate opt-in, one-shot stock46 instance1 equipment experiment. This runs
// under the existing receipt lock on the natural mission owner, never a worker.
void ObserveStock46Writer() noexcept {
#ifdef MGS_LOCAL_STOCK46_WRITER_TEST
    mgscoop::equipment_stock_writer::Context context{};
    const auto& mission=partner_driver.mission;
    // partner_native_equip=manifest|substitute: the generalized writer owns body 1's slots.
    context.enabled=PartnerGunPiecesWanted() && EquipMode()==mgscoop::PartnerNativeEquip::stock46;
    context.owner=OnRuntimeThread();
    context.mission=partner_mission;context.body_epoch=mission.body_epoch;
    context.base=reinterpret_cast<std::uintptr_t>(game_base);context.image_span=game_span;
    context.preload_queued=stock46_authorization.QueuedFor(partner_mission);
    unsigned short current_mission=0;
    context.offline_gates=offline_enabled && lab_active && lab::SavesRedirected() &&
        allocation_enabled && archive_mounted && host_source_selected && partner_on && partner_gate_ok &&
        !mission_terminating && !termination_scope.Active() && !gameplay_restore_pending &&
        !HasDynamiteSessionOwnership() && !mgscoop::partner_defer::ShuttingDown() && ReadMission(current_mission) &&
        current_mission==30010 && current_mission==partner_mission && current_mission==allocation_mission;
    pp::State state{};pp::DmTargets targets{};
    if(context.owner && context.offline_gates && mission.parts_loaded && mission.realized &&
       pp::ReadState(partner_addresses,state)==pp::Refusal::none && state.vars_ok && state.arrays_ok &&
       state.count==2 && state.local==0 && (state.flags&1u) && !(state.flags&0x100u) && !state.sync &&
       state.ids[0]==0 && state.ids[1]==1 && state.pool[0]==0 && state.pool[1]==1 &&
       state.parts[0]==3 && state.parts[1]==3 && pp::Realized(state,0) && pp::Realized(state,1) &&
       !pp::ResolveDm(partner_addresses,state,targets)) {
        context.body_ready=true;context.player=state.P;context.remote_block=targets.blk1;
    }
    mgscoop::equipment_stock_writer::Observe(context,Log);
#endif
}

// The generalized writer (W3/W4): per remote slot one dispatch per body
// generation, at most two per allocation; A1/C and qualified shared targets
// run on the natural mission owner, behind the same gates
// as stock46 except the mission (any partner-body mission of this
// allocation) and the admission (lab or paired play; EquipPiecesAdmitted).
void ObserveEquipWriter() noexcept {
#ifdef MGS_LOCAL_STOCK46_WRITER_TEST
    namespace w=mgscoop::equipment_stock_writer;
    w::GeneralContext context{};
    const auto& mission=partner_driver.mission;
    const auto mode=EquipMode();
    context.enabled=PartnerGunPiecesWanted() && mode!=mgscoop::PartnerNativeEquip::stock46;
    context.substitute=mode==mgscoop::PartnerNativeEquip::substitute;
    context.owner=OnRuntimeThread();
    context.mission=partner_mission;context.body_epoch=mission.body_epoch;
    context.realize_gen=mission.realize_gen;
    context.now_ms=mgscoop::partner_net::NowMs();
    const auto& config=mgscoop::GetLabConfig();
    context.shared=config.partner_native_equip_shared==mgscoop::PartnerNativeEquipShared::prefer ? w::SharedMode::prefer
        : config.partner_native_equip_shared==mgscoop::PartnerNativeEquipShared::on ? w::SharedMode::on : w::SharedMode::off;
    context.measured=config.mode==mgscoop::LabMode::lab && config.partner_native_equip_measured;
    // An unread gun mode waits too: never equip through an unknown switch/reload.
    context.body_busy=mission.body_gun_mode==0xFF || mission.body_gun_mode==8 ||
        mission.body_gun_mode==11 || mission.body_gun_mode==12 || mission.body_switching;
    context.base=reinterpret_cast<std::uintptr_t>(game_base);context.image_span=game_span;
    unsigned short current_mission=0;
    // The first failing gate is reported (equip_writer=waiting; reason=..), nothing is spent.
    const char* admission=EquipAdmissionReason();
    const bool mission_ok=ReadMission(current_mission);
    context.gate_reason=admission ? admission
        : !allocation_enabled || !archive_mounted || !host_source_selected ? "allocation_source"
        : !stock46_groups_authorized || !stock46_groups_returned ? "ghost_groups"
        : mission_terminating || termination_scope.Active() || gameplay_restore_pending ? "terminating"
        : HasDynamiteSessionOwnership() ? "dynamite_session"
        : !mission_ok || !mgscoop::SupportsPartnerBody(current_mission) ? "mission"
        : current_mission!=partner_mission || current_mission!=allocation_mission ? "mission_owner" : nullptr;
    context.gates=context.gate_reason==nullptr;
    pp::State state{};pp::DmTargets targets{};
    if(context.owner && context.gates && mission.parts_loaded && mission.realized &&
       pp::ReadState(partner_addresses,state)==pp::Refusal::none && state.vars_ok && state.arrays_ok &&
       state.count==2 && state.local==0 && (state.flags&1u) && !(state.flags&0x100u) && !state.sync &&
       state.ids[0]==0 && state.ids[1]==1 && state.pool[0]==0 && state.pool[1]==1 &&
       state.parts[0]==3 && state.parts[1]==3 && pp::Realized(state,0) && pp::Realized(state,1) &&
       !pp::ResolveDm(partner_addresses,state,targets)) {
        context.body_ready=true;context.player=state.P;context.remote_block=targets.blk1;
    }
    mgscoop::partner_equip_wire::Manifest manifest{};
    context.manifest=EquipManifest(manifest);
    context.manifest_revision=context.manifest ? manifest.revision : std::uint16_t(0);
    context.manifest_epoch=context.manifest ? manifest.epoch : std::uint32_t(0);
    for(unsigned s=0;s<w::kSlots;++s) {
        context.want[s]=context.manifest ? manifest.slots[s].base_id : std::uint16_t(0);
        context.alias[s]=context.manifest ? manifest.slots[s].alias : std::uint16_t(0);
        context.type[s]=context.manifest && mgscoop::partner_equip_wire::GunType(manifest.slots[s].type) ? manifest.slots[s].type : std::uint8_t(0);
        context.receiver[s]=context.manifest ? manifest.slots[s].recipe[0] : std::uint8_t(0);
    }
    // The remote's current slot: the live v5 slot, else the manifest's (never a zeroed manifest's 0).
    context.remote_slot=mission.remote_v5 && mission.remote_slot<w::kSlots ? mission.remote_slot
        : context.manifest && manifest.inv_slot<w::kSlots ? manifest.inv_slot : std::uint8_t(0xFF);
    w::ObserveGeneral(context,Log);
    w::ReadStatus(partner_driver.equip_status);
#endif
}

// Type-12 slot 3: same natural owner, receipt lock and no-batch discipline as
// the existing gun equipment writer. Every native precondition is re-read in
// partner_support_equip; setting P+218 to 1 is explicitly incompatible.
void ObserveNativeSupport() noexcept {
    namespace se=mgscoop::partner_support_equip;
    namespace sg=mgscoop::partner_support_guard;
    namespace ff=mgscoop::partner_fire_filter;
    const auto& config=mgscoop::GetLabConfig();
    const auto& mission=partner_driver.mission;
    se::Context context{};
    const char* mode_reason=SupportModeReason();
    context.enabled=config.partner_native_support && !mode_reason;
    // One line per change of the mode verdict (only with the key selected).
    {
        static const char* logged_reason="";
        static int logged_mode=-1;
        const int mode=config.mode==mgscoop::LabMode::play ? 1 : config.mode==mgscoop::LabMode::lab ? 0 : 2;
        if(config.partner_native_support && (mode!=logged_mode || std::strcmp(mode_reason ? mode_reason : "ok",logged_reason)!=0)) {
            logged_mode=mode;logged_reason=mode_reason ? mode_reason : "ok";
            LogFormat("partner_support=mode; mode=%s; reason=%s\r\n",mode==1 ? "play" : mode==0 ? "lab" : "other",logged_reason);
        }
    }
    context.owner=OnRuntimeThread();context.base=reinterpret_cast<std::uintptr_t>(game_base);context.image_span=game_span;
    context.pins_intact=mgscoop::equipment_stock_writer::PinsIntact;
    context.mission=partner_mission;context.realize_gen=mission.realize_gen;context.now_ms=mgscoop::partner_net::NowMs();
    // Never (re)equip slot 3/6 through a switch, a reload, a running throw, a
    // place/detonation or an arm use mode.
    context.body_busy=mgscoop::partner_support_policy::BodyBusy(mission.body_gun_mode,mission.body_switching);
    // WS4 v1: the C4 and arm lanes (both ride on partner_native_support). The
    // read-only watchdog: body 1 in an arm use mode 52..56 (FIRE is always
    // stripped for an arm, so it must never happen) latches arms off for the
    // mission, here at 10 Hz and in the hook after every windowed job.
    if(context.enabled && config.partner_native_arm && mgscoop::partner_support_policy::ArmUseMode(mission.body_gun_mode) &&
       !mgscoop::partner_input_hook::NativeArmLatchedOff()) {
        mgscoop::partner_input_hook::LatchNativeArmOff();
        LogFormat("partner_arm=refused; slot=6; equip=%u; type=14; reason=use_mode_mission_latched; mode=%u; source=observer\r\n",
            unsigned(mission.remote_gun_equip>0 ? mission.remote_gun_equip : 0),unsigned(mission.body_gun_mode));
    }
    context.placed=config.partner_native_placed;
    context.arm=config.partner_native_arm && !mgscoop::partner_input_hook::NativeArmLatchedOff();
    unsigned short current_mission=0;
    context.gates=context.enabled && PartnerGunPiecesWanted() && offline_enabled && lab_active && lab::SavesRedirected() &&
        partner_on && partner_gate_ok && allocation_enabled && archive_mounted && host_source_selected &&
        !config.partner_input_p218_allow && !mgscoop::partner_input_hook::NativePoseWindowStatus().required &&
        !mgscoop::partner_input_hook::NativeSupportLatchedOff() &&
        !mission_terminating && !termination_scope.Active() && !gameplay_restore_pending &&
        !HasDynamiteSessionOwnership() && !mgscoop::partner_defer::ShuttingDown() &&
        ReadMission(current_mission) && mgscoop::SupportsPartnerBody(current_mission) &&
        current_mission==partner_mission && current_mission==allocation_mission;
    mgscoop::partner_input::Sample input{};
    if(mission.remote_v5 && mission.remote_gun_equip>0 && mission.remote_gun_equip<0x800 &&
       mgscoop::partner_net::LatestInput(input) && input.valid && input.age_ms<=partner_driver.settings.stale_ms &&
       input.packet.version>=5 && (input.packet.xflags&mgscoop::partner_input::XFlag::Equip) &&
       (input.packet.gun_flags&mgscoop::partner_input::GunFlag::Valid) &&
       input.packet.gun_equip==mission.remote_gun_equip && input.packet.inv_slot==mission.remote_slot) {
        context.equip=static_cast<std::uint16_t>(mission.remote_gun_equip);context.slot=mission.remote_slot;
    } else if(mission.support_equip>0 && mgscoop::partner_net::LatestInput(input) && input.valid) {
        // The driver holds support through an input stall (critic #4): keep the
        // context's slot and equip so Ready still answers; the writer finds the
        // equip already allocated and dispatches nothing. Keyed on the driver's
        // last support verdict (support_equip), not on support_stall, which the
        // driver sets only on a tick whose own read was already stale: a frame
        // that crossed stale_ms between the two reads must not clear the context
        // (partner_support_policy::ContextHold, WS2 review).
        const auto& set=partner_driver.settings;
        mgscoop::partner_support_policy::StallHoldIn hold{};
        // settings is the configured copy (native_active only in the effective one).
        hold.stall_hold=pd::NativeActive(partner_driver) && set.nsupport && set.nstall==mgscoop::PartnerNativeStall::hold;
        hold.stale_ms=set.stale_ms;hold.stall_ms=set.stall_ms;hold.age_ms=input.age_ms;
        hold.v5_valid=input.packet.version>=5 && (input.packet.gun_flags&mgscoop::partner_input::GunFlag::Valid);
        hold.slot=input.packet.inv_slot;
        hold.equip=(input.packet.xflags&mgscoop::partner_input::XFlag::Equip) ? input.packet.gun_equip : static_cast<std::uint16_t>(0);
        hold.was_support=true;hold.support_equip=mission.support_equip;
        if(mgscoop::partner_support_policy::ContextHold(hold)) {context.equip=mission.support_equip;context.slot=3;}
    }
    pp::State state{};pp::DmTargets targets{};
    if(context.owner && context.gates && mission.parts_loaded && mission.realized &&
       pp::ReadState(partner_addresses,state)==pp::Refusal::none && state.vars_ok && state.arrays_ok &&
       state.count==2 && state.local==0 && (state.flags&1u) && !(state.flags&0x100u) && !state.sync &&
       state.ids[0]==0 && state.ids[1]==1 && state.pool[0]==0 && state.pool[1]==1 &&
       state.parts[0]==3 && state.parts[1]==3 && pp::Realized(state,0) && pp::Realized(state,1) &&
       !pp::ResolveDm(partner_addresses,state,targets)) {
        context.body_ready=true;context.player=state.P;context.remote_block=targets.blk1;
        const char* guard_reason=sg::Install(game_base,context.image_span,partner_gate_ok,Log);
        const char* filter_reason=ff::InstallThrown(game_base,Log);
        if(!guard_reason && !filter_reason && sg::Arm(state.P,1) && ff::ArmThrown(1))
            context.safety_ready=sg::Live() && sg::Armed(state.P,1) && ff::ThrownPatchLive(game_base) && ff::ThrownArmed(1);
    }
    // Protective patches remain armed across slot/key changes, and after the
    // stock latch: already queued or unwindowed body-1 work may still finish.
    se::Observe(context,Log);
    // A mission-scoped uncertain writer outcome also closes the drain's admission.
    if(se::Uncertain() && !mgscoop::partner_input_hook::NativeSupportLatchedOff()) {
        mgscoop::partner_input_hook::LatchNativeSupportOff();
        LogFormat("partner_support=refused; slot=%u; equip=%u; type=%u; reason=writer_uncertain_mission_latched\r\n",
            unsigned(context.slot==6 ? 6 : 3),unsigned(context.equip),SupportLineType(context.equip));
    }
    ff::Stats totals{};ff::ReadStats(game_base,totals);
    sg::Stats guard_totals{};sg::ReadStats(guard_totals);
    static std::uint32_t summary_mission=0,summary_drop=0,summary_hits=0,summary_refused=0;
    static std::uint64_t summary_ms=0;
    static unsigned summary_lines=0;
    if(summary_mission!=context.mission) {summary_mission=context.mission;summary_ms=0;summary_lines=0;summary_drop=totals.thrown_dropped;summary_hits=guard_totals.hits;summary_refused=guard_totals.refused;}
    if(totals.thrown_armed && summary_lines<3600 && (context.now_ms<summary_ms || context.now_ms-summary_ms>=1000)) {
        summary_ms=context.now_ms;++summary_lines;
        LogFormat("partner_support=guard; slot=%u; equip=%u; type=%u; reason=%s; installed=%u; live=%u; armed=%u; hits=%u; refusals=%u; hit_delta=%u; refusal_delta=%u; windows=%u\r\n",
            unsigned(context.slot==6 ? 6 : 3),unsigned(context.equip),SupportLineType(context.equip),guard_totals.live ? "ok" : "hook_lost",unsigned(guard_totals.installed),unsigned(guard_totals.live),
            unsigned(guard_totals.armed),guard_totals.hits,guard_totals.refused,guard_totals.hits-summary_hits,guard_totals.refused-summary_refused,guard_totals.windows);
        summary_hits=guard_totals.hits;summary_refused=guard_totals.refused;
        LogFormat("partner_fire_filter=summary; kind=thrown; installed=%u; live=%u; armed=%u; owner=%u; dropped=%u; preview_dropped=%u; passed=%u; unread=%u; delta=%u\r\n",
            unsigned(totals.thrown_installed),unsigned(totals.thrown_live),unsigned(totals.thrown_armed),unsigned(totals.thrown_owner),
            totals.thrown_dropped,totals.thrown_preview_dropped,totals.thrown_passed,totals.thrown_unread,totals.thrown_dropped-summary_drop);
        if(totals.thrown_dropped!=summary_drop)
            LogFormat("partner_support=filtered; slot=%u; equip=%u; type=%u; reason=owner; owner=0x1; dropped=%u; preview_dropped=%u\r\n",
                unsigned(context.slot==6 ? 6 : 3),unsigned(context.equip),SupportLineType(context.equip),totals.thrown_dropped-summary_drop,totals.thrown_preview_dropped);
        summary_drop=totals.thrown_dropped;
    }
}

// F2 (docs/native-puppet.md "Cardboard box"): the remote's cardboard box on body 1
// through partner_cbox_equip (ChangeEquip(1, box, flags 4) on, SetCurrentItemIndex(1,
// 0) off). Lab key partner_native_cbox (default on with the gun profile; play is
// admitted like the support lane, only with link v2 connected). Same natural
// owner, 10 Hz tick, admission gates and body lease as ObserveNativeSupport, right
// after it: serialized with the gun and support writers, and busy while either is
// mid-flight (a gun writer slot in readiness / tracking, body 1 switching or
// reloading, or the support lane latched uncertain).
bool CboxEquipType(std::uint16_t equip,std::uint8_t& type) noexcept { return pp::EquipType(partner_addresses,equip,type); }
void ObserveNativeCbox() noexcept {
    namespace cb=mgscoop::partner_cbox_equip;
    namespace pcb=mgscoop::partner_cbox_policy;
    namespace w=mgscoop::equipment_stock_writer;
    const auto& config=mgscoop::GetLabConfig();
    const auto& mission=partner_driver.mission;
    cb::Context context{};
    const char* mode_reason=SupportModeReason();
    context.enabled=config.partner_native_cbox && !mode_reason;
    {
        // One line per change of the verdict (only with the key on).
        static int logged=-1;
        static const char* logged_reason="";
        const int now=context.enabled ? 1 : 0;
        const char* reason=mode_reason ? mode_reason : "ok";
        if(config.partner_native_cbox && (now!=logged || std::strcmp(reason,logged_reason)!=0)) {
            logged=now;logged_reason=reason;
            LogFormat("partner_cbox=mode; mode=%s; enabled=%u; reason=%s\r\n",config.mode==mgscoop::LabMode::play ? "play" :
                config.mode==mgscoop::LabMode::lab ? "lab" : "other",unsigned(now),reason);
        }
    }
    context.owner=OnRuntimeThread();context.base=reinterpret_cast<std::uintptr_t>(game_base);context.image_span=game_span;
    context.pins_intact=mgscoop::equipment_stock_writer::PinsIntact;
    context.equip_type=CboxEquipType;
    context.mission=partner_mission;context.realize_gen=mission.realize_gen;context.now_ms=mgscoop::partner_net::NowMs();
    context.epoch=mission.start_ms;
    context.want=mission.cbox_want;context.want_id=mission.cbox_id;context.seq=mission.cbox_seq;
    unsigned short current_mission=0;
    context.gate_reason=!context.enabled ? "off"
        : !mission.cfg_ready || mission.stopped ? "partner_cfg"
        : !PartnerGunPiecesWanted() ? "gun_profile"
        : !offline_enabled ? "offline_branch"
        : !lab_active ? "lab_inactive"
        : !lab::SavesRedirected() ? "saves_not_redirected"
        : !partner_on ? "partner_off"
        : !partner_gate_ok ? "code_gate"
        : !allocation_enabled || !archive_mounted || !host_source_selected ? "allocation_source"
        : config.partner_input_p218_allow ? "p218_allow"
        : mgscoop::partner_input_hook::NativePoseWindowStatus().required ? "pose_window"
        : mission_terminating || termination_scope.Active() || gameplay_restore_pending ? "terminating"
        : HasDynamiteSessionOwnership() ? "dynamite_session"
        : mgscoop::partner_defer::ShuttingDown() ? "shutting_down"
        : !ReadMission(current_mission) || !mgscoop::SupportsPartnerBody(current_mission) ? "mission"
        : current_mission!=partner_mission || current_mission!=allocation_mission ? "mission_owner" : nullptr;
    context.gates=context.gate_reason==nullptr;
    pcb::BusyIn busy{};
    busy.gun_mode_busy=mgscoop::partner_support_policy::BodyBusy(mission.body_gun_mode,mission.body_switching);
    for(const auto& slot:partner_driver.equip_status.slots)
        if(slot.state==w::SlotState::readiness || slot.state==w::SlotState::tracking) busy.gun_writer_active=true;
    busy.support_uncertain=mgscoop::partner_support_equip::Uncertain();
    context.busy_reason=pcb::BusyReason(busy);
    pp::State state{};pp::DmTargets targets{};
    if(context.owner && context.gates && mission.parts_loaded && mission.realized &&
       pp::ReadState(partner_addresses,state)==pp::Refusal::none && state.vars_ok && state.arrays_ok &&
       state.count==2 && state.local==0 && (state.flags&1u) && !(state.flags&0x100u) && !state.sync &&
       state.ids[0]==0 && state.ids[1]==1 && state.pool[0]==0 && state.pool[1]==1 &&
       state.parts[0]==3 && state.parts[1]==3 && pp::Realized(state,0) && pp::Realized(state,1) &&
       !pp::ResolveDm(partner_addresses,state,targets)) {
        context.body_ready=true;context.player=state.P;context.remote_block=targets.blk1;
    }
    cb::Observe(context,Log);
}

// WS4 step 0 (docs/anim-round2-throws.md "WS4 step 0: placed and arm census"):
// the read-only C4 / bionic-arm census, lab key partner_placed_census=1 in lab
// mode (play forces the key off). Same natural owner and 10 Hz cadence as the
// equipment observers. Reads only: the qualified partner_player::ReadGun for
// both instances plus the plugin latch and record bytes next to it, the
// placed-object service owner table (vtable-checked chain) and the input
// hook's pad sample. No write, no game call. Capped per mission:
//   partner_placed_census=gun; k=..      C4 / arm in hand, place or arm modes, the plugin latch
//   partner_placed_census=service; ..    the owner table, on change
//   partner_placed_census=pad; ..        body 1 in an arm mode, or a shadow verdict change
//   partner_placed_census=status; ..     every 5 s
void ObservePlacedCensus() noexcept {
    namespace pc=mgscoop::partner_placed_census;
    const auto& config=mgscoop::GetLabConfig();
    const bool on=config.partner_placed_census && config.mode==mgscoop::LabMode::lab && lab_active && partner_on && partner_gate_ok;
    pc::SetPadCensus(on);
    if(!on || !game_base) return;
    const auto& mission=partner_driver.mission;
    const std::uint64_t now=mgscoop::partner_net::NowMs();
    struct CensusState {
        std::uint32_t mission=0xFFFFFFFFu,realize_gen=0xFFFFFFFFu;
        bool announced=false,have_gun[2]{},have_service=false,have_pad=false;
        pc::GunSample gun[2]{};
        std::uint32_t service_digest=0;
        pc::Service service{};
        pc::PadSample pad{};
        pc::Budget gun_budget{},service_budget{},pad_budget{},status_budget{};
        std::uint64_t status_ms=0;
    };
    static CensusState st;
    if(st.mission!=partner_mission || st.realize_gen!=mission.realize_gen) {
        st=CensusState{};st.mission=partner_mission;st.realize_gen=mission.realize_gen;
    }
    if(!st.announced) {
        st.announced=true;
        LogFormat("partner_placed_census=on; mission=%u; realize_gen=%u; read_only=1; writes=0; calls=0\r\n",
            unsigned(partner_mission),unsigned(mission.realize_gen));
    }
    const auto read=[](std::uintptr_t at,void* out,std::size_t size) noexcept {
        SIZE_T count=0;
        return at && size && at<=UINTPTR_MAX-size &&
            ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&count) && count==size;
    };
    const auto base=reinterpret_cast<std::uintptr_t>(game_base);
    char line[640]{};
    // Q1 / Q3: both instances' gun records and plugin latches.
    pp::State state{};
    if(mission.realized && pp::ReadState(partner_addresses,state)==pp::Refusal::none && state.arrays_ok && state.count==2 &&
       state.local==0 && state.ids[0]==0 && state.ids[1]==1) {
        pc::GunSample cur[2]{};
        for(std::uint32_t k=0;k<2;++k) {
            pp::Gun1 gun{};
            if(!pp::Realized(state,k) || !pp::ReadGun(partner_addresses,state,k,gun) || !gun.fields_ok) continue;
            pc::GunSample& s=cur[k];
            s.ok=true;s.prim=gun.prim;s.rec=gun.rec;
            s.equip=static_cast<std::uint16_t>(gun.fields.equip);s.w27c=gun.fields.flags27c;s.cls=pc::GunClass(s.w27c);
            s.mode=gun.mode_cur;s.mode2=gun.mode;s.slot=gun.slot;s.hand=gun.hand_slot;s.f3c0=gun.gun3c0;
            std::uint8_t type=0;
            if(pp::EquipType(partner_addresses,s.equip,type)) s.type=type;
            pc::ReadGunExtra(read,s);
        }
        const int same_prim=cur[0].ok && cur[1].ok ? (cur[0].prim==cur[1].prim ? 1 : 0) : -1;
        for(unsigned k=0;k<2;++k) {
            if(!cur[k].ok) continue;  // unread: the previous sample stays the reference
            bool keep=true;
            if(pc::LogGun(st.have_gun[k],st.gun[k],cur[k])) {
                if(pc::Take(st.gun_budget,now,10,2000)) {
                    if(pc::FormatGun(line,sizeof(line),k,cur[k],same_prim)) Log(line);
                } else if(st.gun_budget.capped) {
                    if(pc::CapReport(st.gun_budget)) LogFormat("partner_placed_census=capped; kind=gun; limit=2000\r\n");
                } else {
                    keep=false;  // over this second's budget: retried against the same reference
                }
            }
            if(keep) {st.gun[k]=cur[k];st.have_gun[k]=true;}
        }
    }
    // Q2: the placed-object service's owner table (owner 1 registered?).
    const pc::Service svc=pc::ReadService(read,base);
    if(!st.have_service || svc.digest!=st.service_digest) {
        if(pc::Take(st.service_budget,now,5,600)) {
            if(pc::FormatService(line,sizeof(line),svc)) Log(line);
            st.service_digest=svc.digest;st.have_service=true;
        } else if(st.service_budget.capped) {
            st.service_digest=svc.digest;st.have_service=true;
            if(pc::CapReport(st.service_budget)) LogFormat("partner_placed_census=capped; kind=service; limit=600\r\n");
        }
    }
    st.service=svc;
    // Q5: the stun op-0 pad fields around body 1's job (the hook's sample).
    pc::PadSample pad{};
    if(pc::ReadPad(pad)) {
        bool keep=true;
        if(pc::LogPad(st.have_pad,st.pad,pad)) {
            if(pc::Take(st.pad_budget,now,10,1200)) {
                if(pc::FormatPad(line,sizeof(line),pad)) Log(line);
            } else if(st.pad_budget.capped) {
                if(pc::CapReport(st.pad_budget)) LogFormat("partner_placed_census=capped; kind=pad; limit=1200\r\n");
            } else {
                keep=false;
            }
        }
        if(keep) {st.pad=pad;st.have_pad=true;}
    }
    if((now<st.status_ms || now-st.status_ms>=5000) && pc::Take(st.status_budget,now,1,720)) {
        st.status_ms=now;
        LogFormat("partner_placed_census=status; k0_equip=%u; k0_cls=%u; k0_mode=%u; k0_latched=%u; k1_equip=%u; k1_cls=%u; "
            "k1_mode=%u; k1_latched=%u; service=%s; owners=%u; owner1=%d; live1=%u; pad_windows=%u; pad_differs=%u\r\n",
            unsigned(st.gun[0].equip),unsigned(st.gun[0].cls),unsigned(st.gun[0].mode),pc::Latched(st.gun[0]) ? 1u : 0u,
            unsigned(st.gun[1].equip),unsigned(st.gun[1].cls),unsigned(st.gun[1].mode),pc::Latched(st.gun[1]) ? 1u : 0u,
            st.service.refusal ? st.service.refusal : "ok",st.service.count,st.service.index1,unsigned(st.service.live1),
            st.pad.windows,pc::PadDiffers(st.pad) ? 1u : 0u);
    }
}

// G2 (docs/native-puppet.md "Seat aim"; research complete/re-seat-aim.md): the
// read-only back-seat aim census, lab key partner_seat_aim=1 in lab mode (play
// forces the key off). Same natural owner and 10 Hz cadence as the equipment
// observers. Reads only: Q's act / upper-act arrays, the qualified
// partner_player::ReadGun (plus its rec+0x2C1 / +0x3D4), the per-id flag record
// *(PS+0x280) of ResolveBlock's PS and, for the local player, the camera through
// partner_pass_camera::ReadCamera. No write, no game call. Watched: the local
// player (k = 0) while its act is a vehicle ride act or the native passenger
// report holds a back seat, body 1 (k = 1) while the seat flow holds it.
//   partner_seat_aim=on; ..        once per mission
//   partner_seat_aim=sample; ..    on a change of a watched instance
//   partner_seat_aim=end; ..       when a watch ends (aims, fires, reloads, armed / aim time)
//   partner_seat_aim=status; ..    every 5 s while either instance is watched
void ObserveSeatAimCensus() noexcept {
    namespace sa=mgscoop::partner_seat_aim;
    const auto& config=mgscoop::GetLabConfig();
    const bool on=config.partner_seat_aim && config.mode==mgscoop::LabMode::lab && lab_active && partner_on && partner_gate_ok;
    struct CensusState {
        std::uint32_t mission=0xFFFFFFFFu,realize_gen=0xFFFFFFFFu;
        bool announced=false;
        sa::Track track[2]{};
        sa::Budgets budgets{};
        std::uint64_t status_ms=0;
    };
    static CensusState st;
    const auto emit=[](const char* line) noexcept {Log(line);};
    const std::uint64_t now=mgscoop::partner_net::NowMs();
    if(!on || !game_base) {
        // Turned off mid-mission (or the gate fell): close the open watches once.
        for(std::uint32_t k=0;k<2;++k) sa::Finish(st.track[k],k,now,"off",st.budgets,emit);
        return;
    }
    const auto& mission=partner_driver.mission;
    if(st.mission!=partner_mission || st.realize_gen!=mission.realize_gen) {
        for(std::uint32_t k=0;k<2;++k) sa::Finish(st.track[k],k,now,"mission",st.budgets,emit);
        st=CensusState{};st.mission=partner_mission;st.realize_gen=mission.realize_gen;
    }
    char line[400]{};
    if(!st.announced) {
        st.announced=true;
        if(sa::FormatOn(line,sizeof(line),partner_mission)) Log(line);
    }
    const auto read=[](std::uintptr_t at,void* out,std::size_t size) noexcept {
        SIZE_T count=0;
        return at && size && at<=UINTPTR_MAX-size &&
            ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&count) && count==size;
    };
    pp::State state{};
    const bool state_ok=mission.realized && pp::ReadState(partner_addresses,state)==pp::Refusal::none && state.arrays_ok &&
        state.count==2 && state.local==0 && state.ids[0]==0 && state.ids[1]==1;
    const auto& seat=mission.seat;
    const bool seat_flow=seat.phase==pd::SeatPhase::arm || seat.phase==pd::SeatPhase::play || seat.phase==pd::SeatPhase::seated ||
        seat.phase==pd::SeatPhase::exit;
    const auto& lp=partner_driver.local_passenger;
    for(std::uint32_t k=0;k<2;++k) {
        sa::Sample s{};
        const bool realized=state_ok && pp::Realized(state,k);
        if(realized) s.ok=sa::ReadActs(read,state.Q,state.ids[k],s.act,s.upper);
        // Only a watched instance reads its gun record, flag record and camera.
        const bool watch=k==0 ? sa::WatchLocal(s.ok,s.act,lp.valid) : sa::WatchBody1(mission.ride_seated,seat_flow);
        if(realized && watch) {
            pp::Gun1 gun{};
            if(pp::ReadGun(partner_addresses,state,k,gun) && gun.fields_ok) {
                s.gun_ok=true;
                s.mode=gun.mode_cur;s.equip=static_cast<std::uint16_t>(gun.fields.equip);
                s.cls=mgscoop::partner_placed_census::GunClass(gun.fields.flags27c);s.hand=gun.hand_slot;
                sa::ReadGunExtra(read,gun.rec,s.next,s.posture);
            }
            pp::Block block{};
            if(!pp::ResolveBlock(partner_addresses,state,k,block) && block.ps) {
                const sa::Flags flags=sa::ReadFlags(read,block.ps,block.id);
                s.flags_ok=flags.ok;s.w0=flags.w0;s.w6=flags.w6;
            }
            if(k==0) {
                mgscoop::partner_pass_camera::Camera cam{};
                if(mgscoop::partner_pass_camera::ReadCamera(game_base,state.P,cam) && cam.ok) {
                    s.cam_ok=true;s.cam_mode=cam.mode;s.cam_handle=cam.connect;
                }
                if(lp.valid) {s.seat=lp.seat;s.vehicle=lp.vehicle;}
            } else {
                s.seat=seat.seat;s.vehicle=seat.vehicle;
            }
        }
        sa::Tick(st.track[k],k,watch,s,now,st.budgets,emit);
    }
    if((st.track[0].episode.active || st.track[1].episode.active) && (now<st.status_ms || now-st.status_ms>=sa::k::kStatusMs) &&
       mgscoop::partner_placed_census::Take(st.budgets.other,now,sa::k::kOtherPerSecond,sa::k::kOtherCap)) {
        st.status_ms=now;
        if(sa::FormatStatus(line,sizeof(line),st.track[0],st.track[1])) Log(line);
    }
}

// The existing Lua OnUpdate polls in every mission, including title/ACC.
// This path remains on the natural runtime owner under receipt_lock, independent
// of the menu. Closing, termination and batch execution stop it before any reads.
void ObserveEquipmentTick() noexcept {
    if(!OnRuntimeThread() || mission_terminating || termination_scope.Active() ||
       InterlockedCompareExchange(&partner_window_closing,0,0) || mgscoop::partner_defer::ShuttingDown() ||
       mgscoop::partner_defer::InBatch()) return;
    const auto now=mgscoop::partner_net::NowMs();
    static std::uint64_t observed_ms=0;
    if(now>=observed_ms && now-observed_ms<100) return;
    observed_ms=now;
    ObserveEquipLink();
    ObserveEquipmentVtables();
    ObserveStock46Writer();
    ObserveEquipWriter();
    ObserveNativeSupport();
    ObserveNativeCbox();
    ObservePlacedCensus();
    ObserveSeatAimCensus();
}

void PublishDebugSnapshot() noexcept {
    ObserveEquipmentTick();
    namespace debug = mgscoop::debug_menu;
    if (!debug::Enabled()) return;
    const auto now = mgscoop::partner_net::NowMs();
    static std::uint64_t published_ms = 0;
    if (now >= published_ms && now - published_ms < 100) return;
    published_ms = now;
    debug::Snapshot snapshot{};
    const auto& settings = partner_driver.settings;
    const auto& mission = partner_driver.mission;
    const auto& config = mgscoop::GetLabConfig();
    const auto link = mgscoop::partner_net::GetLinkInfo();
    const auto world_status = mgscoop::world_link::GetStatus();
    snapshot.tick_ms = now;
    snapshot.mission = partner_mission;
    snapshot.stage = settings.stage;
    snapshot.world_level = world_status.configured ? world_status.level : config.world;
    snapshot.world_configured = world_status.configured;
    snapshot.world_paired = world_status.paired;
    snapshot.world_link_up = world_status.link_up;
    snapshot.world_effective = world_status.effective;
    snapshot.world_partner_mission = world_status.remote_mission;
    snapshot.world_pair_wait = world_status.pair_wait;
    snapshot.world_remote_level = world_status.remote_level;
    snapshot.realized = mission.realized;
    snapshot.parts_loaded = mission.parts_loaded;
    snapshot.range_hidden = mission.range_hidden;
    snapshot.input_armed = mission.input_armed;
    snapshot.body_valid = mission.body_ok;
    snapshot.rtt_ms = link.rtt_ms;
    snapshot.jitter_ms = link.jitter_ms;
    snapshot.loss_permille = link.loss_permille;
    snapshot.interp_ms = mgscoop::partner_net::EffectiveInterpMs(config.partner_interp_ms);
    StringCchCopyA(snapshot.link_state, ARRAYSIZE(snapshot.link_state), mgscoop::partner_net::LinkStateName(link.state));
    StringCchCopyA(snapshot.source, ARRAYSIZE(snapshot.source), mgscoop::PartnerSourceName(settings.source));
    StringCchCopyA(snapshot.apply, ARRAYSIZE(snapshot.apply), mgscoop::PartnerApplyName(settings.apply));
    StringCchCopyA(snapshot.anim, ARRAYSIZE(snapshot.anim), mgscoop::PartnerAnimName(settings.anim));
    StringCchCopyA(snapshot.aset, ARRAYSIZE(snapshot.aset), mgscoop::PartnerAsetName(settings.aset));
    StringCchCopyA(snapshot.dm, ARRAYSIZE(snapshot.dm), mgscoop::PartnerDmName(settings.dm));
    StringCchCopyA(snapshot.look, ARRAYSIZE(snapshot.look), mgscoop::PartnerLookName(settings.look));
    if (mission.refusal) StringCchCopyA(snapshot.refusal, ARRAYSIZE(snapshot.refusal), mission.refusal);
    pp::State current{};
    if (pp::ReadState(partner_addresses, current) == pp::Refusal::none && current.vars_ok) {
        snapshot.pose_valid = true;
        for (unsigned i = 0; i < 3; ++i) snapshot.player_pos[i] = current.vars_pos[i];
        if (current.slots_ok) {
            snapshot.player_yaw = current.slot[0][3] * 57.2957795f;
            snapshot.local_yaw_valid = true;
        }
        pp::Anim animation{};
        if (pp::ReadAnim(current, animation)) {
            snapshot.local_anim_valid = true;
            snapshot.local_act = animation.act[0];
            snapshot.local_anim_stance = animation.stance[0];
            snapshot.local_anim_speed = animation.speed[0];
        }
    }
    snapshot.remote_valid = mission.have_cmd;
    if (mission.have_cmd) {
        snapshot.remote_pos[0] = static_cast<float>(mission.cmd.x_mm) * 0.001f;
        snapshot.remote_pos[1] = static_cast<float>(mission.cmd.y_mm) * 0.001f;
        snapshot.remote_pos[2] = static_cast<float>(mission.cmd.z_mm) * 0.001f;
        snapshot.remote_yaw = static_cast<float>(mission.cmd.yaw_mrad) * 0.0572957795f;
    }
    float error = 0;
    for (unsigned i = 0; i < 3; ++i) {
        snapshot.body_pos[i] = mission.body_pos[i];
        const float delta = snapshot.body_pos[i] - snapshot.remote_pos[i];
        error += delta * delta;
    }
    // Errors against the remote pose only mean something once one has arrived.
    snapshot.position_error_m = snapshot.remote_valid ? std::sqrt(error) : 0.0f;
    if (mission.body_ok) {
        const auto& q = mission.body_quat;
        snapshot.body_yaw = std::atan2(2.0f * (q[3] * q[1] + q[0] * q[2]), 1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2])) * 57.2957795f;
        snapshot.heading_error_deg = std::remainder(snapshot.body_yaw - snapshot.remote_yaw, 360.0f);
    }
    const auto sample = mgscoop::partner_net::SampleRemote(snapshot.interp_ms);
    snapshot.remote_age_ms = sample.age_ms;
    snapshot.remote_action_flags = sample.flags;
    snapshot.remote_equip = sample.equip_id;
    snapshot.remote_act = sample.anim_valid ? sample.act : 0;
    snapshot.remote_anim_valid = sample.anim_valid;
    snapshot.remote_stance = sample.stance;
    snapshot.remote_anim_stance = sample.anim_stance;
    snapshot.remote_anim_speed = sample.anim_speed;
    snapshot.mapped_clip = mission.dmmap.cur;
    snapshot.dm_stance = mission.dmmap.cand_cls.stance;
    snapshot.dm_move = mission.dmmap.cand_cls.move;
    snapshot.dm_speed_valid = mgscoop::partner_dmmap::SpeedMmps(mission.dmmap_speed, snapshot.dm_speed_mmps);
    StringCchCopyA(snapshot.dm_phase, ARRAYSIZE(snapshot.dm_phase), mgscoop::partner_dmmap::PhaseName(mission.dmmap.phase));
    snapshot.motion_count = MountedPlayerMotionCount();
    snapshot.facing_active = mission.facing_active;
    snapshot.facing_limit_dps = config.partner_facing_dps;
    snapshot.facing_snaps = mission.facing.snaps;
    StringCchCopyA(snapshot.facing_last_snap, ARRAYSIZE(snapshot.facing_last_snap),
                   mgscoop::partner_facing::SnapName(mission.facing.last_snap));
    if (mission.facing_active) {
        snapshot.facing_target_yaw = mission.facing_target_yaw * mgscoop::partner_facing::kDegPerRad;
        snapshot.facing_applied_yaw = mission.facing_applied_yaw * mgscoop::partner_facing::kDegPerRad;
        snapshot.facing_rate_dps = mission.facing.rate * mgscoop::partner_facing::kDegPerRad;
    }
    snapshot.facing_ring = mission.facing_ring;
    // Native puppet telemetry (docs/native-puppet.md 11.1).
    snapshot.native = mission.tele;
    snapshot.native.configured = settings.drive == mgscoop::PartnerDrive::native;
    // Change-only action receipts expose actual received gun intent during
    // copied-save gameplay, including when the overlay closes on focus loss.
    // The display's intent bits do not establish accepted weapon behavior.
    static std::uint64_t action_epoch = UINT64_MAX;
    static std::uint32_t action_bits = UINT32_MAX, action_equip = UINT32_MAX;
    static unsigned action_lines = 0;
    if (action_epoch != mission.body_epoch) {
        action_epoch = mission.body_epoch;
        action_bits = action_equip = UINT32_MAX;
        action_lines = 0;
    }
    const auto gun_bits = sample.valid && sample.status == mgscoop::pose::Status::live
        ? sample.flags & (mgscoop::sync::ActionFlag::Aiming | mgscoop::sync::ActionFlag::Firing | mgscoop::sync::ActionFlag::Reloading) : 0u;
    const auto gun_equip = sample.valid && sample.status == mgscoop::pose::Status::live ? sample.equip_id : 0u;
    if (gun_bits != action_bits || gun_equip != action_equip) {
        action_bits = gun_bits; action_equip = gun_equip;
        if (action_lines++ < 64)
            LogFormat("partner_action_state; mission=%u; flags=0x%x; equip=%u; act=%u; age_ms=%u; engine_accepted=unverified\r\n",
                unsigned(partner_mission), unsigned(gun_bits), unsigned(gun_equip), unsigned(snapshot.remote_act), unsigned(sample.age_ms));
    }
    debug::Publish(snapshot);
}

// WARP_V1 "<cmd> <goId> <x_mm> <y_mm> <z_mm> <yaw_mrad>" from lua/coop/debug_spawn.lua.
// Reply: {r=<out>,h=<0|1>,leaf="pinned|engine"} or {refused="<reason>"}.
const char* SoldierWarpReceipt(const char* text, std::size_t size) noexcept {
    namespace debug = mgscoop::debug_menu;
    namespace sw = mgscoop::soldier_warp;
    const auto refuse = [](const char* why) noexcept {
        StringCchPrintfA(debug_reply, ARRAYSIZE(debug_reply), "return {refused=\"%s\"}", why);
        return static_cast<const char*>(debug_reply);
    };
    std::uint32_t id = 0;
    sw::Request request{};
    if (!sw::ParseReceipt(text, size, id, request)) return refuse("args");
    const auto& labc = mgscoop::GetLabConfig();
    if (!labc.debug_soldier_warp) return refuse("disabled");
    if (!soldier_warp_ready || !game_base) return refuse("contract");
    unsigned short mission = 0;
    if (labc.role == mgscoop::LabRole::guest || labc.mode != mgscoop::LabMode::lab) return refuse("host");
    if (mission_terminating || !ReadMission(mission) || mission != partner_mission || !mgscoop::SupportsPartnerBody(mission))
        return refuse("mission");
    if (!debug::DispatchedCommand(id, "spawn_enemy here")) return refuse("command");
    const auto snapshot = debug::GetSnapshot();
    if (!snapshot.host || !snapshot.pose_valid) return refuse("pose");
    for (unsigned i = 0; i < 3; ++i) request.player[i] = snapshot.player_pos[i];
    const auto base = reinterpret_cast<std::uintptr_t>(game_base);
    // One command, one guard, bounded attempts; a fault or anomaly latches the feature off.
    sw::Result result{};
    if (const char* gate = soldier_warp_gate.Admit(id, request.go_id)) result.refusal = gate;
    else {
        result = sw::Warp(base, game_span, request);
        if (const char* latched = soldier_warp_gate.Record(result))
            LogFormat("soldier_warp=disabled; reason=%s; id=%u; go=%u; r=%d; h=%u; until=process_exit\r\n", latched, id,
                      request.go_id, result.out, unsigned(result.handled));
    }
    // The Lua side retries every update while the guard realizes: log changes only.
    static std::uint32_t last_id = 0, last_go = 0;
    static std::int32_t last_out = 0;
    static unsigned last_handled = 0;
    static const char* last_refusal = nullptr;  // string literals from soldier_warp
    static bool last_pinned = false;
    if (id != last_id || request.go_id != last_go || result.out != last_out || result.handled != last_handled ||
        result.refusal != last_refusal || result.chain.pinned != last_pinned) {
        last_id = id; last_go = request.go_id; last_out = result.out; last_handled = result.handled;
        last_refusal = result.refusal; last_pinned = result.chain.pinned;
        if (soldier_warp_census < 24 && result.chain.type_object) {
            ++soldier_warp_census;
            char line[512]{};
            if (sw::FormatCensus(base, game_span, request.go_id, result, line, sizeof(line))) Log(line);
        }
        LogFormat("debug_menu=soldier_warp; id=%u; go=%u; r=%d; h=%u; leaf=%s; refused=%s\r\n", id, request.go_id, result.out,
                  unsigned(result.handled), result.chain.pinned ? "pinned" : "engine", result.refusal ? result.refusal : "none");
    }
    const bool unrealized = result.refusal && std::strcmp(result.refusal, "unrealized") == 0;
    if (result.refusal && !unrealized) return refuse(result.refusal);
    StringCchPrintfA(debug_reply, ARRAYSIZE(debug_reply), "return {r=%d,h=%u,leaf=\"%s\"}", unrealized ? -1 : result.out,
                     unsigned(result.handled), result.chain.pinned ? "pinned" : "engine");
    return debug_reply;
}

// WORLD_WARP_V1 "<seq> <goId>" from lua/coop/world_place.lua on the guest (spawn sync,
// docs/debug-menu.md). The target and kind come from the grant world::Core recorded
// when it popped the host's ENEMY_DEBUG; Lua names only the seq and its own copy's
// GameObjectId. The range is checked against the guest's view of the host (the
// remote pose, at most 1 s old). Same Warp(), contract, budget and latch as the host.
// Reply: {r=<out>,h=<0|1>,leaf="pinned|engine"} or {refused="<reason>"}.
const char* GuestWarpReceipt(const char* text, std::size_t size) noexcept {
    namespace debug = mgscoop::debug_menu;
    namespace sw = mgscoop::soldier_warp;
    const auto refuse = [](const char* why) noexcept {
        StringCchPrintfA(debug_reply, ARRAYSIZE(debug_reply), "return {refused=\"%s\"}", why);
        return static_cast<const char*>(debug_reply);
    };
    std::uint16_t seq = 0;
    sw::Request request{};
    if (!mgscoop::world::ParseWarp(text, size, seq, request.go_id)) return refuse("args");
    const auto& labc = mgscoop::GetLabConfig();
    if (labc.role != mgscoop::LabRole::guest || labc.mode != mgscoop::LabMode::lab) return refuse("role");
    if (!labc.debug_soldier_warp) return refuse("disabled");
    if (!soldier_warp_ready || !game_base) return refuse("contract");
    unsigned short mission = 0;
    if (mission_terminating || !ReadMission(mission) || mission != partner_mission || !mgscoop::SupportsPartnerBody(mission))
        return refuse("mission");
    const auto grant = mgscoop::world_link::FindGrant(seq);
    if (!grant.found) return refuse("grant");
    PublishDebugSnapshot();
    const auto snapshot = debug::GetSnapshot();
    const auto now = mgscoop::partner_net::NowMs();
    if (snapshot.host || !snapshot.remote_valid || !snapshot.world_paired || std::strcmp(snapshot.link_state, "connected") != 0 ||
        snapshot.remote_age_ms > 1000 || now < snapshot.tick_ms || now - snapshot.tick_ms > 1000)
        return refuse("pose");
    for (unsigned i = 0; i < 3; ++i) {
        request.pos[i] = grant.pos[i];
        request.player[i] = snapshot.remote_pos[i];
    }
    request.yaw = grant.yaw;
    request.max_range = grant.kind == mgscoop::world::place::correct ? sw::kMaxRangeLimitM : sw::kMaxRangeM;
    const auto base = reinterpret_cast<std::uintptr_t>(game_base);
    // One grant, one guard, bounded attempts. The id is the grant's process-unique
    // serial with bit 31 set: never a menu command id (< 2^31) and never a spent
    // grant of an earlier host process that reused the same epoch and seq.
    if (!grant.serial || grant.serial > 0x7FFFFFFFu) return refuse("grant");
    const std::uint32_t id = 0x80000000u | grant.serial;
    sw::Result result{};
    if (const char* gate = soldier_warp_gate.Admit(id, request.go_id)) result.refusal = gate;
    else {
        result = sw::Warp(base, game_span, request);
        if (const char* latched = soldier_warp_gate.Record(result))
            LogFormat("soldier_warp=disabled; reason=%s; id=%u; go=%u; r=%d; h=%u; until=process_exit\r\n", latched, id,
                      request.go_id, result.out, unsigned(result.handled));
    }
    static std::uint32_t last_id = 0, last_go = 0;
    static std::int32_t last_out = 0;
    static unsigned last_handled = 0;
    static const char* last_refusal = nullptr;  // string literals from soldier_warp
    if (id != last_id || request.go_id != last_go || result.out != last_out || result.handled != last_handled ||
        result.refusal != last_refusal) {
        last_id = id; last_go = request.go_id; last_out = result.out; last_handled = result.handled; last_refusal = result.refusal;
        if (soldier_warp_census < 24 && result.chain.type_object) {
            ++soldier_warp_census;
            char line[512]{};
            if (sw::FormatCensus(base, game_span, request.go_id, result, line, sizeof(line))) Log(line);
        }
        LogFormat("world_place=warp; seq=%u; kind=%u; ent=%u; go=%u; age_ms=%llu; r=%d; h=%u; leaf=%s; refused=%s\r\n", unsigned(seq),
                  unsigned(grant.kind), grant.ent, request.go_id, static_cast<unsigned long long>(grant.age_ms), result.out,
                  unsigned(result.handled), result.chain.pinned ? "pinned" : "engine", result.refusal ? result.refusal : "none");
    }
    const bool unrealized = result.refusal && std::strcmp(result.refusal, "unrealized") == 0;
    if (result.refusal && !unrealized) return refuse(result.refusal);
    StringCchPrintfA(debug_reply, ARRAYSIZE(debug_reply), "return {r=%d,h=%u,leaf=\"%s\"}", unrealized ? -1 : result.out,
                     unsigned(result.handled), result.chain.pinned ? "pinned" : "engine");
    return debug_reply;
}

bool SelectDebugReceipt(const char* buffer, std::size_t size, const char* name,
                        mgscoop::script_bridge::Replacement& replacement) noexcept {
    namespace debug = mgscoop::debug_menu;
    // With the debug menu off only STATE_V1 is taken, and only for the partner
    // name tag's HUD bits (field 13: demo / iDroid; docs/partner-nametag.md).
    const bool debug_on = debug::Enabled();
    if ((!debug_on && !mgscoop::nametag_game::Ready()) || !buffer || !name || size >= 256 || std::strlen(name) != size ||
        std::memcmp(buffer, name, size) != 0) return false;
    constexpr char command[] = "return 'MGS_COOP_DEBUG_CMD_V1'";
    constexpr char poll[] = "return 'MGS_COOP_DEBUG_POLL_V1'";
    constexpr char log[] = "return 'MGS_COOP_DEBUG_LOG_V1' -- ";
    constexpr char state[] = "return 'MGS_COOP_DEBUG_STATE_V1' -- ";
    constexpr char partner_control[] = "return 'MGS_COOP_DEBUG_PARTNER_V1' -- ";
    constexpr char enemy[] = "return 'MGS_COOP_DEBUG_ENEMY_V1' -- ";
    constexpr char warp[] = "return 'MGS_COOP_DEBUG_WARP_V1' -- ";
    constexpr char remote[] = "return 'MGS_COOP_DEBUG_REMOTE_V1' -- ";
    const bool is_command = size == sizeof(command) - 1 && std::memcmp(buffer, command, size) == 0;
    // While Lua has a pending action it polls: telemetry and the worker
    // stay live, but nothing is popped; commands wait in the bounded queue.
    const bool is_poll = size == sizeof(poll) - 1 && std::memcmp(buffer, poll, size) == 0;
    const bool is_log = size >= sizeof(log) - 1 && std::memcmp(buffer, log, sizeof(log) - 1) == 0;
    const bool is_state = size >= sizeof(state) - 1 && std::memcmp(buffer, state, sizeof(state) - 1) == 0;
    const bool is_partner = size >= sizeof(partner_control) - 1 && std::memcmp(buffer, partner_control, sizeof(partner_control) - 1) == 0;
    const bool is_enemy = size >= sizeof(enemy) - 1 && std::memcmp(buffer, enemy, sizeof(enemy) - 1) == 0;
    const bool is_warp = size >= sizeof(warp) - 1 && std::memcmp(buffer, warp, sizeof(warp) - 1) == 0;
    const bool is_remote = size >= sizeof(remote) - 1 && std::memcmp(buffer, remote, sizeof(remote) - 1) == 0;
    if (!is_command && !is_poll && !is_log && !is_state && !is_partner && !is_enemy && !is_warp && !is_remote) return false;
    if (!debug_on && !is_state) return false;
    if (!OnRuntimeThread() && !(is_log || is_state ? OnTerminationThread() : false)) return false;
    const char* reply = "return nil";
    if (is_poll) {
        PublishDebugSnapshot();
        mgscoop::worker_debug::PollOwner();
    } else if (is_command) {
        PublishDebugSnapshot();
        mgscoop::worker_debug::PollOwner();
        unsigned short read_mission = 0;
        debug::Command pending{};
        if (!mission_terminating && ReadMission(read_mission) && read_mission == partner_mission &&
            mgscoop::SupportsPartnerBody(read_mission) && debug::Pop(pending)) {
            // "nametag ..." is handled natively (in-memory overlay settings): Lua never sees it.
            char tag_result[192]{};
            bool tag_ok = false;
            if (mgscoop::nametag_game::HandleCommand(pending.text, tag_result, sizeof(tag_result), tag_ok)) {
                debug::Result(pending.id, tag_ok ? 1u : 0u, tag_result);
                LogFormat("debug_menu=nametag; id=%u; ok=%u; result=%s\r\n", pending.id, tag_ok ? 1u : 0u, tag_result);
                replacement = {reply, std::strlen(reply)};
                debug_quiet = true;
                return true;
            }
            // "aimpt off|look|point|lab|status" (the aim-target mirror's live A/B
            // switch; docs/native-puppet.md "Facing") is handled natively too.
            if (mgscoop::partner_aim_point::HandleCommand(pending.text, tag_result, sizeof(tag_result), tag_ok)) {
                debug::Result(pending.id, tag_ok ? 1u : 0u, tag_result);
                LogFormat("debug_menu=aimpt; id=%u; ok=%u; result=%s\r\n", pending.id, tag_ok ? 1u : 0u, tag_result);
                replacement = {reply, std::strlen(reply)};
                debug_quiet = true;
                return true;
            }
            // "passcam off|connect|aim|lab|status" (the passenger camera's live A/B switch, lab only;
            // docs/native-puppet.md "Passenger camera", design 8.27) is handled natively too.
            if (mgscoop::partner_pass_camera::HandleCommand(pending.text, mgscoop::GetLabConfig().mode == mgscoop::LabMode::lab,
                                                            tag_result, sizeof(tag_result), tag_ok)) {
                debug::Result(pending.id, tag_ok ? 1u : 0u, tag_result);
                LogFormat("debug_menu=passcam; id=%u; ok=%u; result=%s\r\n", pending.id, tag_ok ? 1u : 0u, tag_result);
                replacement = {reply, std::strlen(reply)};
                debug_quiet = true;
                return true;
            }
            // "seatshot on|off|lab|status" (M7 S1's live A/B switch, lab only; docs/native-puppet.md "Seat shot (S1)").
            {
                int next = -1;
                if (mgscoop::partner_seat_shot::HandleCommand(pending.text, mgscoop::GetLabConfig().mode == mgscoop::LabMode::lab,
                                                              mgscoop::partner_fire_filter::SeatShotOverride(), next, tag_result,
                                                              sizeof(tag_result), tag_ok)) {
                    if (tag_ok) mgscoop::partner_fire_filter::SetSeatShotOverride(next);
                    debug::Result(pending.id, tag_ok ? 1u : 0u, tag_result);
                    LogFormat("debug_menu=seatshot; id=%u; ok=%u; result=%s\r\n", pending.id, tag_ok ? 1u : 0u, tag_result);
                    replacement = {reply, std::strlen(reply)};
                    debug_quiet = true;
                    return true;
                }
            }
            // "gesture_lever knock|sign_go|sign_stop" (docs/native-puppet.md "Gestures",
            // "Lab lever: knock and hand signs"): this copy's own knock / hand sign through
            // the stock request slots; lab only (partner_gesture_lever, play forces it off).
            if (mgscoop::partner_gesture_lever::IsCommand(pending.text)) {
                HandleGestureLever(pending.id, pending.text);
                replacement = {reply, std::strlen(reply)};
                debug_quiet = true;
                return true;
            }
        }
        if (pending.id && std::strcmp(pending.text, "steam_status") == 0) {
            // Read-only (docs/steam-online.md 14), like world_status: the self-test line.
            char status[161]{};
            mgscoop::steam_online::StatusLine(status, sizeof(status));
            debug::Result(pending.id, 1, status);
            LogFormat("debug_menu=dispatch; id=%u; text=steam_status; generation=%u; native=1\r\n", pending.id, pending.generation);
        } else if (pending.id && std::strncmp(pending.text, "input_stall ", 12) == 0) {
            // Worker-only (docs/anim-round2-throws.md "Stall test step"): this copy
            // sends no partner input for N ms, so the peer's stall freeze runs on
            // demand. Only the owned private worker of a background-worker build
            // (never the friend/Release loader, which has no background worker).
            const bool worker=mgscoop::worker_scenario::Compiled() && mgscoop::worker_scenario::Active() &&
                offline_enabled && lab::SavesRedirected() && partner_on && partner_gate_ok;
            const char* digits=pending.text+12;
            std::size_t n=0;unsigned ms=0;
            while(n<5 && digits[n]>='0' && digits[n]<='9'){ms=ms*10u+static_cast<unsigned>(digits[n]-'0');++n;}
            const bool parsed=n>=2 && n<=4 && digits[n]==0 && digits[0]!='0';
            const char* why=!worker ? "owned_worker_required" : !parsed ? "args" : mgscoop::partner_net::StallInput(ms);
            char line[161]{};
            if(why) StringCchPrintfA(line,ARRAYSIZE(line),"input_stall=refused;reason=%s",why);
            else StringCchPrintfA(line,ARRAYSIZE(line),"input_stall=started;ms=%u;gameplay=unverified",ms);
            debug::Result(pending.id,why?0u:1u,line);
            LogFormat("debug_menu=input_stall; id=%u; ok=%u; result=%s\r\n",pending.id,why?0u:1u,line);
        } else if (pending.id && std::strncmp(pending.text, "worker_pad ", 11) == 0) {
            // Only the natural owner of a current copied-save private scenario
            // may lease an in-process controller; real desktop input stays blocked.
            const auto snapshot = debug::GetSnapshot();
            const bool admitted = mgscoop::worker_scenario::Compiled() &&
                mgscoop::worker_scenario::ThreadAdmitted() && offline_enabled &&
                lab::SavesRedirected() && partner_on && partner_gate_ok &&
                snapshot.gameplay_ready && !snapshot.terminating &&
                snapshot.mission == partner_mission && pending.generation == snapshot.generation;
            const bool accepted = admitted && mgscoop::worker_scenario::SetVirtualPad(pending.text+11,pending.id);
            debug::Result(pending.id,accepted?1u:0u,accepted ?
                "worker_pad=accepted;source=owned_private_lease;gameplay=unverified" :
                "worker_pad=refused;reason=private_owner_or_canonical_bounds");
        } else if (pending.id && std::strncmp(pending.text, "teammate_quiet ", 15) == 0) {
            const char* value=pending.text+15;
            const bool worker=mgscoop::worker_scenario::Compiled() && mgscoop::worker_scenario::Active() &&
                offline_enabled && lab::SavesRedirected() && partner_on && partner_gate_ok;
            const bool observe=std::strcmp(value,"observe")==0, filter=std::strcmp(value,"filter")==0;
            const bool status=std::strcmp(value,"status")==0;
            if(worker && (observe || filter)) {
                mgscoop::nametag_game::ResetNativeUiCensus();
                mgscoop::teammate_targeting::SetQuietMode(filter ?
                    mgscoop::teammate_targeting::Mode::filter : mgscoop::teammate_targeting::Mode::observe);
            }
            char line[161]{};
            if(!worker || (!observe && !filter && !status))
                StringCchCopyA(line,ARRAYSIZE(line),"quiet=refused;reason=owned_worker_required");
            else mgscoop::teammate_targeting::QuietStatusLine(line,sizeof(line));
            debug::Result(pending.id,worker && (observe || filter || status)?1u:0u,line);
        } else if (pending.id && std::strncmp(pending.text, "teammate_reticle ", 17) == 0) {
            const char* value=pending.text+17;
            const bool worker=mgscoop::worker_scenario::Compiled() && mgscoop::worker_scenario::Active() &&
                offline_enabled && lab::SavesRedirected() && partner_on && partner_gate_ok;
            const bool observe=std::strcmp(value,"observe")==0, friendly=std::strcmp(value,"friendly")==0;
            const bool status=std::strcmp(value,"status")==0;
            if(worker && (observe || friendly)) {
                mgscoop::nametag_game::ResetNativeUiCensus();
                mgscoop::teammate_targeting::SetReticleMode(friendly ?
                    mgscoop::teammate_targeting::Mode::filter : mgscoop::teammate_targeting::Mode::observe);
            }
            char line[161]{};
            if(!worker || (!observe && !friendly && !status))
                StringCchCopyA(line,ARRAYSIZE(line),"reticle=refused;reason=owned_worker_required");
            else mgscoop::teammate_targeting::ReticleStatusLine(line,sizeof(line));
            debug::Result(pending.id,worker && (observe || friendly || status)?1u:0u,line);
        } else if (pending.id && std::strncmp(pending.text, "teammate_aim ", 13) == 0) {
            // Live A/B control is admitted only inside an owned private worker.
            const char* mode=pending.text+13;
            const bool worker=mgscoop::worker_scenario::Compiled() && mgscoop::worker_scenario::Active() &&
                offline_enabled && lab::SavesRedirected() && partner_on && partner_gate_ok;
            const bool observe=std::strcmp(mode,"observe")==0, filter=std::strcmp(mode,"filter")==0;
            const bool status=std::strcmp(mode,"status")==0;
            if(worker && (observe || filter)) {
                mgscoop::nametag_game::ResetNativeUiCensus();
                mgscoop::teammate_targeting::SetMode(filter ?
                    mgscoop::teammate_targeting::Mode::filter : mgscoop::teammate_targeting::Mode::observe);
            }
            char line[161]{};
            if(!worker || (!observe && !filter && !status))
                StringCchCopyA(line,ARRAYSIZE(line),"teammate_aim=refused;reason=owned_worker_required");
            else mgscoop::teammate_targeting::StatusLine(line,sizeof(line));
            debug::Result(pending.id,worker && (observe || filter || status) ? 1u:0u,line);
        } else if (pending.id && std::strcmp(pending.text, "damage_status") == 0) {
            char line[161]{};
            mgscoop::downed_damage::StatusLine(line,sizeof(line));
            debug::Result(pending.id,1,line);
        } else if (pending.id && (std::strcmp(pending.text, "ride_player_status") == 0 ||
                   std::strcmp(pending.text, "ride_partner_status") == 0)) {
            namespace r=mgscoop::ride_player_observer;
            const unsigned id=std::strcmp(pending.text,"ride_partner_status")==0?1u:0u;
            r::Sample sample{};const char* reason="admission";pp::State owner{};unsigned short mission{};
            const auto world=mgscoop::world_link::GetStatus();
            const bool allowed=mgscoop::buddy_capacity_census::Compiled() && MountedBuddyCapacity() && ride_player_pins_ok &&
                mgscoop::worker_scenario::ThreadAdmitted() && offline_enabled && lab::SavesRedirected() && partner_on && partner_gate_ok &&
                OnRuntimeThread() && !mission_terminating && !gameplay_restore_pending && !(mgscoop::debug_menu::LuaHud()&1u) &&
                world.configured && world.paired && world.link_up && world.effective>=3 && world.epoch &&
                ReadMission(mission) && mission==partner_mission && mgscoop::SupportsPartnerBody(mission) &&
                pp::ReadState(partner_addresses,owner)==pp::Refusal::none && owner.local==0 && !owner.sync &&
                owner.ids[0]==0 && owner.ids[1]==1 && pp::Realized(owner,0) && owner.parts[0]==3;
            if(allowed){
                r::Scope scope{reinterpret_cast<std::uintptr_t>(game_base),owner.P,mission,world.epoch,true,true,true};
                reason=r::ReadHorseAtInstance({nullptr,UpperPhaseRead},scope,id,sample);
                pp::State after{};unsigned short afterMission{};const auto afterWorld=mgscoop::world_link::GetStatus();
                if(!OnRuntimeThread() || mission_terminating || !ReadMission(afterMission) || afterMission!=mission ||
                    pp::ReadState(partner_addresses,after)!=pp::Refusal::none || after.P!=owner.P || after.Q!=owner.Q ||
                    after.ids[0]!=0 || after.ids[1]!=1 || !afterWorld.paired || !afterWorld.link_up || afterWorld.epoch!=world.epoch){
                    sample={};reason="scope_changed";
                }
            }
            char status[161]{};StringCchPrintfA(status,ARRAYSIZE(status),
                "rideplayer;id=%u;ready=%u;reason=%s;state=%u,%u;target=%u;unit=0x%llx;own=%u;rider=%u;rep=%u:%u;action=0",
                id,allowed?1u:0u,reason?reason:"ok",unsigned(sample.state),unsigned(sample.next_state),unsigned(sample.selected_target),
                static_cast<unsigned long long>(sample.unit),sample.unit_matches_player?1u:0u,unsigned(sample.unit_rider),
                unsigned(sample.replication_kind),unsigned(sample.replication_target));
            debug::Result(pending.id,reason?0u:1u,status);
        } else if (pending.id && (std::strcmp(pending.text,"pose_window_test 1")==0 || std::strcmp(pending.text,"pose_window_test 0")==0)) {
            const bool on=pending.text[17]=='1';
            // Register the passive listener while the window is still unrequired.
            // A refused sleep setup must not prevent a camera-only worker test.
            if(on) EnsurePartnerSleepConfigured();
            const bool ok=mgscoop::partner_input_hook::SetNativePoseWindowTest(on);
            char status[161]{};StringCchPrintfA(status,ARRAYSIZE(status),
                "posewindow;enabled=%u;sleep_cfg=%u;scope=private_worker;refused=%u",
                ok&&on?1u:0u,partner_sleep_configured?1u:0u,ok?0u:1u);
            debug::Result(pending.id,ok?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"pose_indexed_status")==0) {
            const auto seq=mgscoop::partner_input_hook::WindowSeq();pp::State a{},b{};float p0[4]{},q0[4]{},p1[4]{},q1[4]{};
            const bool ok=partner_gate_ok&&!(seq&1u)&&!mgscoop::partner_defer::InBatch()&&
                pp::ReadState(partner_addresses,a)==pp::Refusal::none&&a.count==2&&a.local==0&&
                pp::ReadBody(partner_addresses,a,0,p0,q0)&&pp::ReadBody(partner_addresses,a,1,p1,q1)&&
                pp::ReadState(partner_addresses,b)==pp::Refusal::none&&a.P==b.P&&a.Q==b.Q&&a.S==b.S&&
                mgscoop::partner_input_hook::WindowSeq()==seq&&!mgscoop::partner_defer::InBatch();
            const auto v=debug::GetSnapshot();char status[161]{};
            const auto formatted=StringCchPrintfA(status,ARRAYSIZE(status),"indexedpose;ready=%u;v=%.1f,%.1f,%.1f;b0=%.1f,%.1f,%.1f;b1=%.1f,%.1f,%.1f;seq=%lu",
                ok?1u:0u,double(v.player_pos[0]),double(v.player_pos[1]),double(v.player_pos[2]),double(p0[0]),double(p0[1]),double(p0[2]),
                double(p1[0]),double(p1[1]),double(p1[2]),(unsigned long)seq);
            debug::Result(pending.id,ok&&SUCCEEDED(formatted)?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"pose_cached_status")==0) {
            const auto seq=mgscoop::partner_input_hook::WindowSeq();pp::State a{},b{};float p0[4]{},q0[4]{};
            const bool ready=partner_gate_ok&&!(seq&1u)&&!mgscoop::partner_defer::InBatch()&&
                pp::ReadState(partner_addresses,a)==pp::Refusal::none&&a.slots_ok&&a.vars_ok&&a.count==2&&a.local==0&&
                pp::ReadBody(partner_addresses,a,0,p0,q0)&&pp::ReadState(partner_addresses,b)==pp::Refusal::none&&
                a.P==b.P&&a.Q==b.Q&&a.S==b.S&&a.slots==b.slots&&
                mgscoop::partner_input_hook::WindowSeq()==seq&&!mgscoop::partner_defer::InBatch();
            const float dx=a.slot[0][0]-p0[0],dy=a.slot[0][1]-p0[1],dz=a.slot[0][2]-p0[2];
            const float gap=std::sqrt(dx*dx+dy*dy+dz*dz);char status[161]{};
            const auto formatted=StringCchPrintfA(status,ARRAYSIZE(status),"cachedpose;ready=%u;local=%u;s0=%.1f,%.1f,%.1f;s1=%.1f,%.1f,%.1f;vars=%.1f,%.1f,%.1f;gap=%.3f;seq=%lu",
                ready?1u:0u,ready&&std::isfinite(gap)&&gap<0.5f?1u:0u,
                double(a.slot[0][0]),double(a.slot[0][1]),double(a.slot[0][2]),double(a.slot[1][0]),double(a.slot[1][1]),double(a.slot[1][2]),
                double(a.vars_pos[0]),double(a.vars_pos[1]),double(a.vars_pos[2]),double(gap),(unsigned long)seq);
            debug::Result(pending.id,ready&&SUCCEEDED(formatted)?1u:0u,status);
        } else if (pending.id && (std::strcmp(pending.text,"pose_camera_status")==0 || std::strcmp(pending.text,"pose_camera_export_status")==0)) {
            struct CameraSample {std::uintptr_t cam=0,c0=0,sub=0;std::uint8_t active=0,requested=0;
                std::uint32_t mode=0,flags=0,target=0,previous=0;std::uintptr_t subVt=0,subGet=0,model=0;float camera[4]{},global[4]{};};
            const auto read=[&](const pp::State& owner,CameraSample& out) noexcept {
                out={};std::uintptr_t vt=0,parent=0;const auto base=reinterpret_cast<std::uintptr_t>(game_base);
                return UpperPhaseRead(nullptr,owner.P+0xb8,&out.cam,8)&&out.cam>=0x20&&
                    UpperPhaseRead(nullptr,out.cam,&vt,8)&&vt==base+0x23b8250&&
                    UpperPhaseRead(nullptr,out.cam-0x20,&vt,8)&&vt==base+0x23b8218&&
                    UpperPhaseRead(nullptr,out.cam+0x348,&parent,8)&&parent==owner.P&&
                    UpperPhaseRead(nullptr,owner.P+0xc0,&out.c0,8)&&out.c0>=0x20&&
                    UpperPhaseRead(nullptr,out.c0,&vt,8)&&vt==base+0x23c7500&&
                    UpperPhaseRead(nullptr,out.c0-0x20,&vt,8)&&vt==base+0x23c74c8&&
                    UpperPhaseRead(nullptr,out.c0+8,&parent,8)&&parent==owner.P&&
                    UpperPhaseRead(nullptr,out.c0+0x6c,&out.active,1)&&UpperPhaseRead(nullptr,out.c0+0x6d,&out.requested,1)&&
                    UpperPhaseRead(nullptr,out.cam+0x20c,&out.mode,4)&&UpperPhaseRead(nullptr,out.cam+0x32c,&out.flags,4)&&
                    UpperPhaseRead(nullptr,out.cam+0x380,&out.target,4)&&UpperPhaseRead(nullptr,out.cam+0x384,&out.previous,4)&&
                    UpperPhaseRead(nullptr,out.cam+0x350,&out.model,8)&&UpperPhaseRead(nullptr,out.cam+0x360,&out.sub,8)&&
                    (!out.sub||(UpperPhaseRead(nullptr,out.sub,&out.subVt,8)&&out.subVt>=base&&out.subVt-base<game_span-8&&UpperPhaseRead(nullptr,out.subVt,&out.subGet,8)))&&
                    UpperPhaseRead(nullptr,out.cam+0x90,out.camera,sizeof(out.camera))&&
                    UpperPhaseRead(nullptr,base+0x2c552e0,out.global,sizeof(out.global));
            };
            const auto seq=mgscoop::partner_input_hook::WindowSeq();pp::State a{},b{};CameraSample x{},y{};
            const bool ready=partner_gate_ok&&!(seq&1u)&&!mgscoop::partner_defer::InBatch()&&
                pp::ReadState(partner_addresses,a)==pp::Refusal::none&&read(a,x)&&read(a,y)&&
                x.cam==y.cam&&x.c0==y.c0&&x.sub==y.sub&&x.active==y.active&&x.requested==y.requested&&x.mode==y.mode&&
                x.flags==y.flags&&x.target==y.target&&x.previous==y.previous&&x.subVt==y.subVt&&x.subGet==y.subGet&&x.model==y.model&&
                std::memcmp(x.camera,y.camera,sizeof(x.camera))==0&&std::memcmp(x.global,y.global,sizeof(x.global))==0&&pp::ReadState(partner_addresses,b)==pp::Refusal::none&&a.P==b.P&&a.Q==b.Q&&a.S==b.S&&
                mgscoop::partner_input_hook::WindowSeq()==seq&&!mgscoop::partner_defer::InBatch();
            char status[161]{};HRESULT formatted;
            if(std::strcmp(pending.text,"pose_camera_export_status")==0)
                formatted=StringCchPrintfA(status,ARRAYSIZE(status),"cameraexport;ready=%u;cam=%.3f,%.3f,%.3f,%.3f;global=%.3f,%.3f,%.3f,%.3f;seq=%lu",
                    ready?1u:0u,double(x.camera[0]),double(x.camera[1]),double(x.camera[2]),double(x.camera[3]),
                    double(x.global[0]),double(x.global[1]),double(x.global[2]),double(x.global[3]),(unsigned long)seq);
            else formatted=StringCchPrintfA(status,ARRAYSIZE(status),"posecamera;ready=%u;c0=%u,%u;mode=%x;flags=%x;target=%x,%x;model=%u;subvt=%llx;get=%llx;seq=%lu",
                ready?1u:0u,unsigned(x.active),unsigned(x.requested),unsigned(x.mode),unsigned(x.flags),unsigned(x.target),unsigned(x.previous),x.model?1u:0u,
                static_cast<unsigned long long>(x.subVt>=reinterpret_cast<std::uintptr_t>(game_base)?x.subVt-reinterpret_cast<std::uintptr_t>(game_base):0),
                static_cast<unsigned long long>(x.subGet>=reinterpret_cast<std::uintptr_t>(game_base)?x.subGet-reinterpret_cast<std::uintptr_t>(game_base):0),(unsigned long)seq);
            debug::Result(pending.id,ready&&SUCCEEDED(formatted)?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"pose_camera_reader_status")==0) {
            const auto r=mgscoop::partner_input_hook::NativePoseWindowStatus();char status[161]{};
            StringCchPrintfA(status,ARRAYSIZE(status),"camerareader;installed=%u;shared=%lu;owned=%lu;caller=%lx;typed=%lu;body=%lu;sender=%lu;filtered=%lu;fault=%u;exports=%lu",
                r.installed?1u:0u,(unsigned long)r.cameraSharedCalls,(unsigned long)r.cameraOwnedCalls,(unsigned long)r.cameraOwnedCallerRva,
                (unsigned long)r.cameraOwnedOwnerMatched,(unsigned long)r.cameraOwnedBody,(unsigned long)r.cameraOwnedSenderMatched,
                (unsigned long)r.cameraOwnedFiltered,r.faulted?1u:0u,(unsigned long)r.localExportsSuppressed);
            debug::Result(pending.id,r.installed?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"pose_activation_source_status")==0) {
            const auto r=mgscoop::partner_input_hook::NativePoseActivationStatusRead();char status[161]{};
            StringCchPrintfA(status,ARRAYSIZE(status),"actsource;stage=%u;body=%u;caller=%x;origin=%u,%u;serial=%llu,%llu;q=%u,%u;clear=%u;entry=%u",
                r.lastStage,r.lastBody,r.lastCallerRva,r.origin[0],r.origin[1],static_cast<unsigned long long>(r.serial[0]),
                static_cast<unsigned long long>(r.serial[1]),r.pending[0]?1u:0u,r.pending[1]?1u:0u,r.invalidated,r.invalidationEntries);
            debug::Result(pending.id,r.compiled&&r.hooks?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"pose_activation_status")==0) {
            const auto r=mgscoop::partner_input_hook::NativePoseActivationStatusRead();char status[161]{};
            StringCchPrintfA(status,ARRAYSIZE(status),"actbirth;on=%u;hooks=%u;r=%u,%u;o=%u,%u;s=%llu,%llu;in=%u;f=%u;p=%u;done=%u;no=%u;ref=%u;inv=%u,%u;ab=%u;q=%u,%u",
                r.compiled?1u:0u,r.hooks?1u:0u,r.ready[0]?1u:0u,r.ready[1]?1u:0u,r.origin[0],r.origin[1],
                static_cast<unsigned long long>(r.serial[0]),static_cast<unsigned long long>(r.serial[1]),r.entered,r.factory,r.staged,r.published,
                r.noop,r.refused,r.invalidationEntries,r.invalidated,r.abnormal,r.pending[0]?1u:0u,r.pending[1]?1u:0u);
            debug::Result(pending.id,r.compiled&&r.hooks?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"pose_birth_status")==0) {
            const auto r=mgscoop::partner_input_hook::NativePoseWindowStatus();char status[161]{};
            StringCchPrintfA(status,ARRAYSIZE(status),"posebirth;in=%lu;ready=%lu;caller=%lu;b1=%lu;model=%lu;done=%lu;race=%lu;count=%lu;id=%lu;from=0x%lx;stage=%s",
                (unsigned long)r.birthEntered,(unsigned long)r.birthReady,(unsigned long)r.birthExactCaller,(unsigned long)r.birthBody1,(unsigned long)r.birthModel,
                (unsigned long)r.birthCompleted,(unsigned long)r.birthOverlap,(unsigned long)r.birthCount,(unsigned long)r.birthBase,(unsigned long)r.birthCallerRva,r.birthStage);
            debug::Result(pending.id,r.installed?1u:0u,status);
        } else if (pending.id && (std::strcmp(pending.text,"carry_cold_status")==0 ||
            std::strcmp(pending.text,"carry_cold_route_status")==0 || std::strcmp(pending.text,"carry_cold_message_status")==0 ||
            std::strcmp(pending.text,"carry_cold_profile_status")==0 || std::strcmp(pending.text,"carry_cold_profile_current_status")==0 || std::strcmp(pending.text,"carry_cold_p238_status")==0)) {
            mgscoop::partner_input_hook::NativePoseCarryColdStatus r{};
            const bool ready=mgscoop::partner_input_hook::ReadNativePoseCarryColdStatus(r);
            char status[161]{};HRESULT formatted;
            if(std::strcmp(pending.text,"carry_cold_route_status")==0)
                formatted=StringCchPrintfA(status,ARRAYSIZE(status),"carryroute;ready=%u;route=%u;idle=%u;registry=%x;iface=%x;mode=%u;flags=%x;count=%u;pending=%llu",
                    ready?1u:0u,r.routeReady?1u:0u,r.routeNoWork?1u:0u,r.registryType,r.routeType,r.routeMode,r.routeFlags,r.routeCount,
                    static_cast<unsigned long long>(r.routePending));
            else if(std::strcmp(pending.text,"carry_cold_p238_status")==0)
                formatted=StringCchPrintfA(status,ARRAYSIZE(status),"carryp238;ready=%u;read=%u;typed=%u;vt=%x;count=%u;base=%u;b0=%u;b1=%u;reason=%u",
                    ready?1u:0u,r.p238Observed?1u:0u,r.p238Typed?1u:0u,r.p238Vt,r.p238Count,r.p238Base,r.p238Body0?1u:0u,r.p238Body1?1u:0u,r.p238Reason);
            else if(std::strcmp(pending.text,"carry_cold_message_status")==0)
                formatted=StringCchPrintfA(status,ARRAYSIZE(status),"carrymessage;ready=%u;message=%u;type=%x;key=%u;enabled=%u",
                    ready?1u:0u,r.messageReady?1u:0u,r.messageType,r.messageKey,r.messageEnabled);
            else if(std::strcmp(pending.text,"carry_cold_profile_status")==0)
                formatted=StringCchPrintfA(status,ARRAYSIZE(status),"carryprofile;ready=%u;read=%u;current=%u;reason=%u;type=%u;parts=%u;camo=%u;aux=%u;face=%u;refresh=%u",
                    ready?1u:0u,r.profileReady?1u:0u,r.profileCurrent?1u:0u,r.profileReason,r.profileType,r.profileParts,r.profileCamo,r.profileAux,r.profileFace,r.profileRefresh);
            else if(std::strcmp(pending.text,"carry_cold_profile_current_status")==0)
                formatted=StringCchPrintfA(status,ARRAYSIZE(status),"carrycurrent;ready=%u;read=%u;type=%u;parts=%u;camo=%u;encoded=%u;aux=%u;face=%u;selector=%u",
                    ready?1u:0u,r.profileReady?1u:0u,r.currentType,r.currentParts,r.currentCamo,r.currentEncoded,r.currentAux,r.currentFace,r.profileSelector);
            else formatted=StringCchPrintfA(status,ARRAYSIZE(status),"carrycold;ready=%u;route=%u;idle=%u;message=%u;vars=%u;domains=%u,%u,%u;word=%u;local=%u;selected=%u;slice=%u",
                ready?1u:0u,r.routeReady?1u:0u,r.routeNoWork?1u:0u,r.messageReady?1u:0u,r.varsReady?1u:0u,
                r.varsDomain?1u:0u,r.messageDomain?1u:0u,r.routeDomain?1u:0u,r.word,r.local,r.selected,r.localSliceActive?1u:0u);
            debug::Result(pending.id,ready&&SUCCEEDED(formatted)?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"pose_window_status")==0) {
            const auto r=mgscoop::partner_input_hook::NativePoseWindowStatus();char status[161]{};
            StringCchPrintfA(status,ARRAYSIZE(status),"posewindow;installed=%u;required=%u;held=%u;fault=%u;born=%u;windows=%lu;skip=%lu;hints=%lu;reason=%s",
                r.installed?1u:0u,r.required?1u:0u,r.held?1u:0u,r.faulted?1u:0u,r.born1?1u:0u,(unsigned long)r.windows,(unsigned long)r.suppressed,(unsigned long)r.hintsSuppressed,r.reason);
            debug::Result(pending.id,r.installed&&!r.faulted?1u:0u,status);
        } else if (pending.id && (std::strcmp(pending.text,"partner_sleep_test 1")==0||std::strcmp(pending.text,"partner_sleep_test 0")==0)) {
            const bool on=std::strcmp(pending.text,"partner_sleep_test 1")==0;
            const bool ok=!on?psleep::Enable(false):EnsurePartnerSleepConfigured()&&PartnerSleepEnableAllowed()&&psleep::Enable(true);
            debug::Result(pending.id,ok?1u:0u,ok?(on?"partnersleep;enabled=1;scope=private_worker":"partnersleep;enabled=0;wake_queued=1"):
                "partnersleep;refused=1;reason=private_gate_or_local_cache_or_dm");
        } else if (pending.id && (std::strcmp(pending.text,"coop_carry 0")==0||std::strcmp(pending.text,"coop_carry 1")==0||std::strcmp(pending.text,"coop_carry 2")==0)) {
            const unsigned mode=static_cast<unsigned>(pending.text[11]-'0');
            const bool ok=partner_on&&partner_gate_ok&&game_base&&game_span&&
                (mgscoop::coop_carry::Installed()||mgscoop::coop_carry::Install(game_base,game_span,Log));
            if(ok){mgscoop::coop_carry::SetSender(&CoopCarrySend);mgscoop::coop_carry::SetMode(mode);}
            char status[161]{};mgscoop::coop_carry::StatusLine(status,sizeof(status));
            debug::Result(pending.id,ok?1u:0u,ok?status:"coopcarry;refused=1;reason=gate_or_install");
        } else if (pending.id && std::strncmp(pending.text,"coop_carry_grant ",17)==0) {
            const unsigned mask=static_cast<unsigned>(std::strtoul(pending.text+17,nullptr,10))&3u;
            mgscoop::coop_carry::SetCarryable(mask);
            char status[161]{};mgscoop::coop_carry::StatusLine(status,sizeof(status));
            debug::Result(pending.id,1u,status);
        } else if (pending.id && std::strcmp(pending.text,"coop_carry_status")==0) {
            char status[161]{};const bool ok=mgscoop::coop_carry::StatusLine(status,sizeof(status));
            debug::Result(pending.id,ok?1u:0u,status);
        } else if (pending.id && std::strncmp(pending.text,"coop_sleep ",11)==0 && pending.text[11]>='0' && pending.text[11]<='1' && pending.text[12]==' ') {
            const unsigned id=static_cast<unsigned>(pending.text[11]-'0');const bool on=pending.text[13]=='1';
            const bool ok=partner_on&&partner_gate_ok&&game_base&&(mgscoop::coop_sleep::Configured()||mgscoop::coop_sleep::Configure(game_base,game_span))&&
                mgscoop::coop_sleep::Hold(id,on);
            char status[161]{};mgscoop::coop_sleep::StatusLine(status,sizeof(status));
            debug::Result(pending.id,ok?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"coop_sleep_status")==0) {
            char status[161]{};const bool ok=mgscoop::coop_sleep::StatusLine(status,sizeof(status));
            debug::Result(pending.id,ok?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"coop_carry_mirror")==0) {
            char status[161]{};const bool ok=mgscoop::coop_carry::MirrorLine(status,sizeof(status));
            debug::Result(pending.id,ok?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"vehicle_mirror_prepare")==0) {
            const bool ok=VehicleMirrorPrepare();char line[161]{};mgscoop::coop_vehicle_mirror::StatusLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"vmirror;prepare_refused=1;requires=cold_private_worker");
        } else if (pending.id && (std::strcmp(pending.text,"vehicle_mirror_test 1")==0||std::strcmp(pending.text,"vehicle_mirror_test 0")==0)) {
            const bool ok=VehicleMirrorEnable(pending.text[20]=='1');char line[161]{};mgscoop::coop_vehicle_mirror::StatusLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"vmirror;enable_refused=1;requires=owned_healthy_required_windows");
        } else if (pending.id && std::strcmp(pending.text,"vehicle_foot_camera_test 1")==0) {
            const bool admitted=mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&
                offline_enabled&&lab::SavesRedirected()&&PartnerSleepPrivate()&&OnRuntimeThread();
            const bool ok=admitted&&mgscoop::partner_input_hook::SetNativePoseVehicleFootCameraTest(true);
            debug::Result(pending.id,ok?1u:0u,ok?"vfoot;enabled=1;production=0":"vfoot;enabled=0;refused=1;details=private_log");
        } else if (pending.id && std::strcmp(pending.text,"vehicle_mirror_status")==0) {
            char line[161]{};const bool ok=mgscoop::coop_vehicle_mirror::StatusLine(line,sizeof(line));debug::Result(pending.id,ok?1u:0u,line);
        } else if (pending.id && std::strcmp(pending.text,"passenger_prepare")==0) {
            const bool admitted=mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&
                offline_enabled&&lab::SavesRedirected()&&partner_on&&partner_gate_ok&&game_base&&game_span;
            const bool ok=admitted&&EnsurePartnerSleepConfigured()&&mgscoop::coop_passenger::Install(game_base,game_span,admitted,Log);
            char line[161]{};mgscoop::coop_passenger::StatusLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"passenger;prepare_refused=1");
        } else if (pending.id && (std::strcmp(pending.text,"passenger_test 1")==0||std::strcmp(pending.text,"passenger_test 0")==0)) {
            const bool ok=mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&
                mgscoop::coop_passenger::SetEnabled(pending.text[15]=='1');
            char line[161]{};mgscoop::coop_passenger::StatusLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"passenger;enable_refused=1");
        } else if (pending.id && std::strcmp(pending.text,"passenger_status")==0) {
            char line[161]{};const bool ok=mgscoop::coop_passenger::StatusLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"passenger;status_refused=1");
        } else if (pending.id && (std::strcmp(pending.text,"coop_sleep_detail0")==0||std::strcmp(pending.text,"coop_sleep_detail1")==0)) {
            char line[161]{};const auto n=std::strlen(pending.text);const bool ok=mgscoop::coop_sleep::DetailLine(pending.text[n-1]=='1'?1u:0u,line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"sleepDetail;status_refused=1");
        } else if (pending.id && std::strcmp(pending.text,"passenger_lookup")==0) {
            char line[161]{};const bool ok=mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&mgscoop::coop_passenger::LookupCensusLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"plookup;read=0;why=private_admission");
         } else if (pending.id && (std::strcmp(pending.text,"passenger_lifetime")==0||std::strcmp(pending.text,"passenger_resource")==0)) {
            char line[161]{};const bool privateWorker=mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted();
            const bool ok=privateWorker&&(std::strcmp(pending.text,"passenger_lifetime")==0?
                mgscoop::coop_passenger::LifetimeCensusLine(line,sizeof(line)):mgscoop::coop_passenger::ResourceCensusLine(line,sizeof(line)));
            debug::Result(pending.id,ok?1u:0u,ok?line:"vehicleCensus;read=0;why=private_admission");
         } else if (pending.id && std::strcmp(pending.text,"passenger_camera")==0) {
            char line[161]{};const bool ok=mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&mgscoop::coop_passenger::CameraCensusLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"pcamera;read=0;why=private_admission");
        } else if (pending.id && std::strcmp(pending.text,"seq_provider")==0) {
            char line[161]{};const bool ok=PrologueProviderLine(line,sizeof(line));debug::Result(pending.id,ok?1u:0u,ok?line:"seqProv;ok=0;why=private_admission");
        } else if (pending.id && (std::strcmp(pending.text,"horse_service_native")==0||std::strcmp(pending.text,"horse_service_rows")==0)) {
            char line[161]{};const bool ok=HorseCargoNativeDiagnostic(6656,std::strcmp(pending.text,"horse_service_rows")==0?2u:1u,line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"horseSvc;ready=0;why=status_refused");
        } else if (pending.id && std::strcmp(pending.text,"horse_flow_prepare")==0) {
            const bool admitted=mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted();
            const bool ready=admitted&&mgscoop::horse_cargo_flow_probe::Prepare(reinterpret_cast<std::uintptr_t>(game_base),game_span);
            debug::Result(pending.id,ready?1u:0u,ready?"horseFlow;prepared=1;writer=off":"horseFlow;prepared=0;writer=off");
        } else if (pending.id && std::strcmp(pending.text,"horse_flow")==0) {
            char line[161]{};const bool ready=mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&mgscoop::horse_cargo_flow_probe::Line(line,sizeof(line));
            debug::Result(pending.id,ready?1u:0u,ready?line:"horseFlow;read=0;why=private_admission");
        } else if (pending.id && std::strcmp(pending.text,"horse_cargo_pair")==0) {
            char line[161]{};const bool ok=HorseCargoPairDiagnostic(6656,line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"cargoPair;ok=0;why=status_refused");
        } else if (pending.id && std::strcmp(pending.text,"horse_cargo_native")==0) {
            char line[161]{};const bool ok=HorseCargoNativeDiagnostic(6656,0,line,sizeof(line));debug::Result(pending.id,ok?1u:0u,ok?line:"horseCargo;ready=0;why=status_refused");
        } else if (pending.id && std::strcmp(pending.text,"phys_birth")==0) {
            mgscoop::partner_input_hook::NativePoseBirthCensus pair{};const bool ready=mgscoop::partner_input_hook::ReadNativePoseBirthCensus(pair);
            char line[161]{};std::snprintf(line,sizeof(line),"physBirth;ready=%u;birth=%llu,%llu;gen=%u;epoch=%u;life=%llu;win=%u",ready?1u:0u,
                static_cast<unsigned long long>(pair.birth[0]),static_cast<unsigned long long>(pair.birth[1]),pair.generation,pair.sceneEpoch,
                static_cast<unsigned long long>(pair.lifecycle),pair.sequence);debug::Result(pending.id,1u,line);
        } else if (pending.id && std::strcmp(pending.text,"phys_transform")==0) {
            char line[161]{};const bool ok=PhysicalTransformLine(line,sizeof(line));debug::Result(pending.id,ok?1u:0u,ok?line:"physTransform;ready=0");
        } else if (pending.id && (std::strcmp(pending.text,"phys_native0")==0||std::strcmp(pending.text,"phys_native1")==0||std::strcmp(pending.text,"phys_life0")==0||std::strcmp(pending.text,"phys_life1")==0)) {
            char line[161]{};const std::size_t n=std::strlen(pending.text);const bool ok=mgscoop::horse_physical_probe_glue::NaturalLine(pending.text[n-1]=='1'?1u:0u,std::strncmp(pending.text,"phys_life",9)==0,line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"physNative;r=0;why=status_refused");
        } else if (pending.id && std::strcmp(pending.text,"passenger_engine")==0) {
            char line[161]{};const bool ok=mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&mgscoop::coop_passenger::EngineCensusLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"pengine;read=0;why=private_admission");
        } else if (pending.id && std::strcmp(pending.text,"passenger_seats")==0) {
            char line[161]{};const bool ok=mgscoop::coop_passenger::SeatCensusLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"pseats;status_refused=1");
        } else if (pending.id && std::strcmp(pending.text,"horse_player_physical")==0) {
            namespace hr=mgscoop::horse_player_physical_read;
            mgscoop::carry_plugin_read::Scope scope{};PhysicalScope(scope);
            const auto reader=[](std::uintptr_t at,void* out,std::size_t size) noexcept {SIZE_T done=0;return at&&size&&
                ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&done)&&done==size;};
            auto read=reader;hr::Observation observed{};const char* reason="scope";const bool ready=hr::Observe(read,scope,observed,&reason);
            char line[161]{};const bool ok=hr::Line(observed,ready,reason,line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"charPhysical;status_refused=1");
        } else if (pending.id && std::strcmp(pending.text,"phys_prepare")==0) {
            mgscoop::carry_plugin_read::Scope scope{};const bool admitted=PhysicalScope(scope);
            const bool ok=admitted&&mgscoop::horse_physical_probe_glue::Install(game_base,game_span,admitted,&PhysicalCallbackSample,nullptr);
            if(ok){physical_probe_prepared=mgscoop::partner_input_hook::RegisterNativePoseDualReadObserver(&PublishPhysicalDual);RefreshPhysicalProbe();}
            char line[161]{};mgscoop::horse_physical_probe_glue::Line(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"physCb;prepare_refused=1");
        } else if (pending.id && (std::strcmp(pending.text,"phys_callback")==0||std::strcmp(pending.text,"phys_raw")==0)) {
            char line[161]{};const bool ok=std::strcmp(pending.text,"phys_raw")==0?
                mgscoop::horse_physical_probe_glue::RawLine(line,sizeof(line)):mgscoop::horse_physical_probe_glue::Line(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"physCb;status_refused=1");
        } else if (pending.id && std::strcmp(pending.text,"horse_player_profile")==0) {
            namespace hp=mgscoop::horse_player_physical_read;namespace pr=mgscoop::horse_physical_profile_read;
            mgscoop::carry_plugin_read::Scope scope{};const bool admitted=PhysicalScope(scope);
            auto code=mgscoop::partner_input_hook::ReadNativePoseOriginalCode;std::uintptr_t mismatch=0;
            const bool gate=physical_probe_prepared||pr::CodeGate(code,scope.image,game_span,mismatch);
            auto reader=[](std::uintptr_t at,void* out,std::size_t size) noexcept {SIZE_T done=0;return at&&size&&
                ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&done)&&done==size;};
            hp::Observation observed{};pr::Profile a{},b{};
            const bool ready=admitted&&gate&&hp::Observe(reader,scope,observed)&&pr::Observe(reader,scope.image,observed.body[0],a)&&pr::Observe(reader,scope.image,observed.body[1],b);
            char line[161]{};const bool ok=pr::Line(a,b,ready,line,sizeof(line));debug::Result(pending.id,ok?1u:0u,ok?line:"charProfile;status_refused=1");
        } else if (pending.id && std::strcmp(pending.text,"coop_carry_npc_predicate")==0) {
            char line[161]{};const bool ok=mgscoop::coop_carry::NpcPredicateLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"carryNpcGate;status_refused=1");
        } else if (pending.id && std::strcmp(pending.text,"coop_carry_horse_finder")==0) {
            char line[161]{};const bool ok=mgscoop::coop_carry::HorseFinderLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"horseFinder;status_refused=1");
        } else if (pending.id && std::strcmp(pending.text,"coop_carry_horse_gate")==0) {
            char line[161]{};const bool ok=mgscoop::coop_carry::HorseGateLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"horseGate;status_refused=1");
        } else if (pending.id && std::strcmp(pending.text,"coop_carry_horse_load")==0) {
            char line[161]{};const bool ok=mgscoop::coop_carry::HorseLoadLine(line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"horseLoad;status_refused=1");
        } else if (pending.id && std::strcmp(pending.text,"coop_carry_load")==0) {
            char status[161]{};const bool ok=mgscoop::coop_carry::LoadLine(status,sizeof(status));
            debug::Result(pending.id,ok?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"coop_carry_sel")==0) {
            char status[161]{};const bool ok=mgscoop::coop_carry::SelectorLine(status,sizeof(status));
            debug::Result(pending.id,ok?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"coop_carry_trace")==0) {
            char status[161]{};const bool ok=mgscoop::coop_carry::TraceLine(status,sizeof(status));
            debug::Result(pending.id,ok?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text,"coop_carry_bodies")==0) {
            char status[161]{};const bool ok=mgscoop::coop_carry::BodyLine(status,sizeof(status));
            debug::Result(pending.id,ok?1u:0u,status);
        } else if(pending.id&&std::strcmp(pending.text,"partner_sleep_wake")==0){
            const bool ok=PartnerSleepPrivate()&&psleep::RequestWake();debug::Result(pending.id,ok?1u:0u,ok?"partnersleep;wake_queued=1":"partnersleep;wake_refused=1");
        } else if(pending.id&&std::strcmp(pending.text,"partner_sleep_stance_status")==0){
            const auto r=psleep::GetStatus();const auto now=GetTickCount64();char status[161]{};
            const auto age=r.stanceAt&&now>=r.stanceAt?Clamp32(now-r.stanceAt):0xffffffffu;
            const auto result=StringCchPrintfA(status,ARRAYSIZE(status),
                "sleepstance;read=%u;packed=%x;ps=%u;q=%u;psact=%u;act=%u;win=%u;age=%u;reason=%s",
                r.stanceObserved?1u:0u,r.psPacked,r.stance,r.qStance,r.psAction,r.qAction,r.window,age,r.reason?r.reason:"unknown");
            debug::Result(pending.id,r.configured&&SUCCEEDED(result)?1u:0u,status);
        } else if(pending.id&&std::strcmp(pending.text,"partner_sleep_status")==0){
            const auto r=psleep::GetStatus();char status[161]{};const auto result=StringCchPrintfA(status,ARRAYSIZE(status),
                "partnersleep;cfg=%u;on=%u;active=%u;phase=%u;mode=%u;life=%u;act=%u;zero=%u;gen=%u;reason=%s",
                r.configured?1u:0u,r.enabled?1u:0u,r.active?1u:0u,r.phase,r.mode,r.lifeState,r.qAction,r.zero,r.peerGeneration,r.reason?r.reason:"unknown");
            debug::Result(pending.id,r.configured&&SUCCEEDED(result)?1u:0u,status);
        } else if (pending.id && (std::strcmp(pending.text, "sleep_native_test 1") == 0 || std::strcmp(pending.text, "sleep_native_test 0") == 0)) {
            const bool enabled=pending.text[18]=='1';
            const bool ok=mgscoop::downed_player::SetNativeSleepTest(enabled);
            debug::Result(pending.id,ok?1u:0u,ok?(enabled?"sleep_native_test;enabled=1;scope=owned_hidden_worker":"sleep_native_test;enabled=0"):
                "sleep_native_test;refused=1;reason=gate_or_owned_cycle");
        } else if(pending.id&&(std::strcmp(pending.text,"sleep_native_rest_state")==0||std::strcmp(pending.text,"sleep_native_rest_receipt")==0||std::strcmp(pending.text,"sleep_native_rest_reason")==0)){
            const auto r=mgscoop::downed_player::GetNativeRestDiagnostics();const auto now=GetTickCount64();char status[161]{};
            const auto age=[&](std::uint64_t at)noexcept{return at&&now>=at?Clamp32(now-at):UINT32_MAX;};HRESULT formatted=E_FAIL;
            if(std::strcmp(pending.text,"sleep_native_rest_state")==0)formatted=StringCchPrintfA(status,ARRAYSIZE(status),
                "localrest;on=%u;active=%u;abort=%u;cas=%llu;checks=%llu;wait=%u;stun=%.0f;max=%.0f",
                r.enabled?1u:0u,r.active?1u:0u,r.aborted?1u:0u,(unsigned long long)r.successfulCas,(unsigned long long)r.checks,
                age(r.pendingAt),double(r.stun),double(r.maximumStun));
            else if(std::strcmp(pending.text,"sleep_native_rest_receipt")==0)formatted=StringCchPrintfA(status,ARRAYSIZE(status),
                "restreceipt;stage=%u;seq=%u;cap=%u;now=%u;gen=%u;capms=%u;readms=%u;desc=%u;flags=%x",
                r.stage,r.sampleSequence,r.capSequence,r.currentSequence,r.generation,age(r.capAt),age(r.sampledAt),r.descriptorObserved?1u:0u,r.descriptorFlags);
            else formatted=StringCchPrintfA(status,ARRAYSIZE(status),"restreason;stage=%u;reason=%s",r.stage,r.reason?r.reason:"none");
            debug::Result(pending.id,SUCCEEDED(formatted)?1u:0u,status);
        } else if (pending.id && std::strcmp(pending.text, "downed_pad_status") == 0) {
            char status[161]{};
            const bool complete=mgscoop::downed_damage::PadStatusLine(status,sizeof(status));
            debug::Result(pending.id,complete?1u:0u,status);
        } else if (pending.id && (std::strcmp(pending.text, "carry_status") == 0 || std::strcmp(pending.text, "carry_plugin_status") == 0 || std::strcmp(pending.text, "carry_policy_status") == 0 || std::strcmp(pending.text, "carry_recipient_status") == 0 || std::strcmp(pending.text, "carry_request_status") == 0 || std::strcmp(pending.text, "carry_model_status") == 0 || std::strcmp(pending.text, "carry_message_status") == 0 || std::strcmp(pending.text, "carry_listener_status") == 0)) {
            const auto owner=mgscoop::downed_player::GetSnapshot();
            mgscoop::carry_info::Reply r{};
            const bool ok=mgscoop::carry_info::Observe(owner.mission,owner.epoch,DownedAdmitted(owner.mission,owner.epoch),mission_thread,r);
            const auto& a=r.bodies[0];const auto& b=r.bodies[1];char line[161]{};
            StringCchPrintfA(line,ARRAYSIZE(line),"carry;ready=%u;gen=%lu;b0=%u,%u,%u,%u;b1=%u,%u,%u,%u;attached=unverified;reason=%s",
                ok?1u:0u,static_cast<unsigned long>(r.generation),a.eligible?1u:0u,a.nativeCarryBit?1u:0u,unsigned(a.gunTarget),a.gunTargetsPartner?1u:0u,
                b.eligible?1u:0u,b.nativeCarryBit?1u:0u,unsigned(b.gunTarget),b.gunTargetsPartner?1u:0u,r.reason?r.reason:"unknown");
            if(std::strcmp(pending.text,"carry_plugin_status")==0){
                StringCchPrintfA(line,ARRAYSIZE(line),"carryplugin;ready=%u;enable=%u;sel=%u,%u,%u;b0=%u,%u,%u;b1=%u,%u,%u;reason=%s",
                    ok&&r.pluginReady?1u:0u,r.playerCarryEnabled?1u:0u,r.selectorOwnerMatches?1u:0u,r.selectorAccepts?1u:0u,unsigned(r.selectorTarget),
                    unsigned(a.carryTarget),a.carryNodeActive?1u:0u,unsigned(a.carryState[0]),
                    unsigned(b.carryTarget),b.carryNodeActive?1u:0u,unsigned(b.carryState[0]),r.pluginReason);
            }
            if(std::strcmp(pending.text,"carry_policy_status")==0)StringCchPrintfA(line,ARRAYSIZE(line),"carrypolicy;ready=%u;present=%u;provider_vt=0x%lx;reason=%s",
                ok&&r.providerReady?1u:0u,r.providerPresent?1u:0u,static_cast<unsigned long>(r.providerVtableRva),r.providerReason);
            if(std::strcmp(pending.text,"carry_recipient_status")==0)StringCchPrintfA(line,ARRAYSIZE(line),"carryrecipient;ready=%u;b0=%u,%u,%u,%u,%u;b1=%u,%u,%u,%u,%u;bound=%u,%u;attachment=unverified;reason=%s",
                ok&&r.recipientReady?1u:0u,unsigned(a.recipientCarrier),unsigned(a.recipientCurrent),unsigned(a.recipientNext),a.recipientActive?1u:0u,a.recipientEndPending?1u:0u,
                unsigned(b.recipientCarrier),unsigned(b.recipientCurrent),unsigned(b.recipientNext),b.recipientActive?1u:0u,b.recipientEndPending?1u:0u,a.reciprocalBinding?1u:0u,b.reciprocalBinding?1u:0u,r.recipientReason);
            if(std::strcmp(pending.text,"carry_request_status")==0)StringCchPrintfA(line,ARRAYSIZE(line),"carryqueue;ready=%u;b0=%u,%u,%u;b1=%u,%u,%u;gameplay=unverified;reason=%s",
                ok&&r.requestReady?1u:0u,unsigned(a.requestKind),unsigned(a.requestSequence),unsigned(a.requestForce),
                unsigned(b.requestKind),unsigned(b.requestSequence),unsigned(b.requestForce),r.requestReason);
            if(std::strcmp(pending.text,"carry_listener_status")==0)StringCchPrintfA(line,ARRAYSIZE(line),
                "carrytypes;msg=%u;listener=%u;vt=0x%lx;shared=%u;vars=%u;word=%u;script=0x%lx;call=unverified",
                ok&&r.messageTypeReady?1u:0u,r.messageListenerPresent?1u:0u,(unsigned long)r.messageListenerVtableRva,ok&&r.sharedCarryTypesReady?1u:0u,
                r.sharedCarryVarsMatch?1u:0u,unsigned(r.sharedControllerWord),(unsigned long)r.scriptSingletonVtableRva);
            if(std::strcmp(pending.text,"carry_model_status")==0)StringCchPrintfA(line,ARRAYSIZE(line),"carrymodel;ready=%u;b0=%u,%u,%u;b1=%u,%u,%u;msg_ready=%u;msg_vt=0x%lx;lifetime=unverified;reason=%s",
                ok&&r.modelReady?1u:0u,a.modelOwnedCallbacks,a.modelFreeCallbacks,a.modelRegistration?1u:0u,
                b.modelOwnedCallbacks,b.modelFreeCallbacks,b.modelRegistration?1u:0u,r.messageTypeReady?1u:0u,
                static_cast<unsigned long>(r.messageVtableRva),r.modelReason);
            if(std::strcmp(pending.text,"carry_message_status")==0)StringCchPrintfA(line,ARRAYSIZE(line),"carrymsg;ready=%u;vt=0x%lx;owner_vt=0x%lx;global=%u;key=%lu;enabled=%u;call=unverified",
                ok&&r.messageTypeReady?1u:0u,static_cast<unsigned long>(r.messageVtableRva),static_cast<unsigned long>(r.messageOwnerVtableRva),
                r.messageOwnerIsGlobal?1u:0u,static_cast<unsigned long>(r.messageKey),unsigned(r.messageEnabled));
            if(ok&&r.pluginReady)LogFormat("carry_plugin;generation=%lu;enable=%u;selector=%u,%u,%u;kind=%lu,%lu;flags=0x%lx,0x%lx;body0=%u,%u,%u,%u,%u,%u,%u,%u;body1=%u,%u,%u,%u,%u,%u,%u,%u;attachment=unverified\r\n",
                static_cast<unsigned long>(r.generation),r.playerCarryEnabled?1u:0u,r.selectorOwnerMatches?1u:0u,r.selectorAccepts?1u:0u,unsigned(r.selectorTarget),
                static_cast<unsigned long>(r.cachedKind),static_cast<unsigned long>(r.currentKind),static_cast<unsigned long>(r.cachedFlags),static_cast<unsigned long>(r.currentFlags),
                unsigned(a.carryTarget),unsigned(a.carryPrevious),unsigned(a.carrier),a.carryNodeActive?1u:0u,unsigned(a.carryState[0]),unsigned(a.carryState[1]),unsigned(a.carryState[2]),unsigned(a.pluginFlags[0]),
                unsigned(b.carryTarget),unsigned(b.carryPrevious),unsigned(b.carrier),b.carryNodeActive?1u:0u,unsigned(b.carryState[0]),unsigned(b.carryState[1]),unsigned(b.carryState[2]),unsigned(b.pluginFlags[0]));
            debug::Result(pending.id,ok?1u:0u,line);
        } else if (pending.id && std::strcmp(pending.text,"vehicle_info_status")==0) {
            const auto owner=mgscoop::downed_player::GetSnapshot();mgscoop::vehicle_info::Reply r{};
            const bool ok=mgscoop::vehicle_info::Observe(owner.mission,owner.epoch,DownedAdmitted(owner.mission,owner.epoch),mission_thread,r);
            const auto& a=r.bodies[0];const auto& b=r.bodies[1];char line[161]{};
            StringCchPrintfA(line,ARRAYSIZE(line),"vehiclectl;ready=%u;gen=%lu;b0=%u,%u,%u,%u,%u,%u;b1=%u,%u,%u,%u,%u,%u;reason=%s",
                ok?1u:0u,static_cast<unsigned long>(r.generation),unsigned(a.actualId),unsigned(a.vehicle),unsigned(a.candidate),unsigned(a.phase),unsigned(a.nextPhase),unsigned(a.entryOrSeat),
                unsigned(b.actualId),unsigned(b.vehicle),unsigned(b.candidate),unsigned(b.phase),unsigned(b.nextPhase),unsigned(b.entryOrSeat),r.reason);
            debug::Result(pending.id,ok?1u:0u,line);
        } else if (pending.id && std::strcmp(pending.text,"sleep_bind_status")==0) {
            char line[161]{};const bool ok=mgscoop::sleep_unique_bind_runtime::BindingStatus(line,sizeof(line));debug::Result(pending.id,ok?1u:0u,ok?line:"sleepBind;ready=0;why=status_refused");
        } else if (pending.id && std::strcmp(pending.text,"sleep_bind_source")==0) {
            char line[161]{};const bool ok=mgscoop::sleep_unique_bind_runtime::SourceStatus(line,sizeof(line));debug::Result(pending.id,ok?1u:0u,ok?line:"bindSource;ready=0;why=status_refused");
        } else if (pending.id && std::strcmp(pending.text,"sleep_bank_status")==0) {
            char line[161]{};const bool ok=mgscoop::sleep_unique_bind_runtime::BankTraceStatus(line,sizeof(line),false);
            debug::Result(pending.id,ok?1u:0u,ok?line:"bankTrace;ok=0;why=status_refused");
        } else if (pending.id && std::strcmp(pending.text,"sleep_bank_snapshot")==0) {
            char line[161]{};const bool ok=mgscoop::sleep_unique_bind_runtime::BankTraceStatus(line,sizeof(line),true);
            debug::Result(pending.id,ok?1u:0u,ok?line:"bankTrace;ok=0;why=snapshot_refused");
        } else if (pending.id && std::strncmp(pending.text,"native_timing_mask ",19)==0) {
            char* end=nullptr;const auto mask=std::strtoull(pending.text+19,&end,10);
            const bool admitted=end&&end!=pending.text+19&&!*end&&mgscoop::worker_scenario::Compiled()&&
                mgscoop::worker_scenario::ThreadAdmitted()&&offline_enabled&&lab::SavesRedirected()&&PartnerSleepPrivate()&&OnRuntimeThread();
            const bool ok=admitted&&mgscoop::native_work_timing::Configure(mask,false);
            debug::Result(pending.id,ok?1u:0u,ok?"nwork;configured=1;cycles=0;production=0":"nwork;configure_refused=1");
        } else if (pending.id && std::strncmp(pending.text,"native_timing_status ",21)==0) {
            char* end=nullptr;const auto index=std::strtoul(pending.text+21,&end,10);char line[240]{};
            const bool ok=end&&end!=pending.text+21&&!*end&&index<mgscoop::native_work_timing::count&&
                mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&
                mgscoop::native_work_timing::Line(static_cast<mgscoop::native_work_timing::Kind>(index),line,sizeof(line));
            debug::Result(pending.id,ok?1u:0u,ok?line:"nwork;status_refused=1");
        } else if (pending.id && std::strcmp(pending.text,"standing_frame_status")==0) {
            char line[161]{};const bool ok=mgscoop::native_motion_frame_runtime::Status(line,sizeof(line));debug::Result(pending.id,ok?1u:0u,ok?line:"sleepFrame;on=0;why=status_refused");
        } else if (pending.id && std::strcmp(pending.text,"standing_cycle_asset_status")==0) {
            char line[161]{};const bool ok=StandingCycleAssetLine(line,sizeof(line));debug::Result(pending.id,ok?1u:0u,ok?line:"sleepCycle;ready=0;why=status_refused");
        } else if (pending.id && std::strcmp(pending.text,"sleep_asset_status")==0) {
            char line[161]{};const bool ok=SleepAssetLine(line,sizeof(line));debug::Result(pending.id,ok?1u:0u,ok?line:"sleepAsset;ready=0;why=status_refused");
        } else if (pending.id && (std::strcmp(pending.text,"sleep_info_status")==0||std::strcmp(pending.text,"sleep_graph_status")==0)) {
            const auto owner=mgscoop::downed_player::GetSnapshot();mgscoop::sleep_info::Reply r{};
            const bool ok=mgscoop::sleep_info::Observe(owner.mission,owner.epoch,DownedAdmitted(owner.mission,owner.epoch),mission_thread,r);
            const auto& a=r.bodies[0];const auto& b=r.bodies[1];char line[161]{};
            StringCchPrintfA(line,ARRAYSIZE(line),"sleepnative;ready=%u;gen=%lu;b0=%u,%u,%u,%u,%lu,%u;b1=%u,%u,%u,%u,%lu,%u;reason=%s",
                ok?1u:0u,static_cast<unsigned long>(r.generation),unsigned(a.mode),unsigned(a.requested),unsigned(a.action),unsigned(a.qAction),static_cast<unsigned long>(a.lifeState),a.nodeActive?1u:0u,
                unsigned(b.mode),unsigned(b.requested),unsigned(b.action),unsigned(b.qAction),static_cast<unsigned long>(b.lifeState),b.nodeActive?1u:0u,r.reason);
            if(std::strcmp(pending.text,"sleep_graph_status")==0)StringCchPrintfA(line,ARRAYSIZE(line),"sleepgraph;ready=%u;b0=%lu,%lu,0x%llx,%u;b1=%lu,%lu,0x%llx,%u;reason=%s",
                ok&&r.graphReady?1u:0u,static_cast<unsigned long>(r.graphCount[0]),static_cast<unsigned long>(r.graphIndex[0]),static_cast<unsigned long long>(r.graphHash[0]),r.graphSleepCandidate[0]?1u:0u,
                static_cast<unsigned long>(r.graphCount[1]),static_cast<unsigned long>(r.graphIndex[1]),static_cast<unsigned long long>(r.graphHash[1]),r.graphSleepCandidate[1]?1u:0u,ok?r.graphReason:r.reason);
            debug::Result(pending.id,ok?1u:0u,line);
        } else if (pending.id && std::strcmp(pending.text, "sleep_native_state") == 0) {
            const auto s=mgscoop::downed_player::GetSnapshot();char line[161]{};
            StringCchPrintfA(line,ARRAYSIZE(line),"sleeplease;enabled=%u;active=%u;phase=%u;stun=%.3f,%.3f;seq=%lu;sample=%llu;reason=%s",
                s.nativeSleepEnabled?1u:0u,s.nativeSleepActive?1u:0u,unsigned(s.nativePhase),double(s.nativeStun),double(s.nativeMaximumStun),
                static_cast<unsigned long>(s.nativeSampleSequence),static_cast<unsigned long long>(s.nativeSampleAt),s.reason?s.reason:"unknown");
            debug::Result(pending.id,1,line);
        } else if (pending.id && std::strcmp(pending.text, "downed_status") == 0) {
            const auto s=mgscoop::downed_player::GetSnapshot();
            char status[161]{};
            StringCchPrintfA(status,ARRAYSIZE(status),"downed;ready=%u;armed=%u;down=%u;gen=%lu;cause=%u;lock=%u;hp=%.0f,%.0f;epoch=%u;state=%s;reason=%s",
                s.ready?1u:0u,s.armed?1u:0u,s.down?1u:0u,static_cast<unsigned long>(s.generation),unsigned(s.cause),
                s.controlsApplied?1u:0u,double(s.life),double(s.maximum),unsigned(s.epoch),s.down?(s.cause==2?"asleep":"wounded"):"healthy",s.reason?s.reason:"unknown");
            debug::Result(pending.id,1,status);
        } else if (pending.id && std::strcmp(pending.text, "revive_status") == 0) {
            const auto owner=mgscoop::downed_player::GetSnapshot();
            mgscoop::downed_player::Reply r{};
            mgscoop::downed_player::Control(owner.mission,owner.epoch,DownedAdmitted(owner.mission,owner.epoch),
                4,mgscoop::downed_player::kReviveActionHash,r);
            char status[161]{};
            StringCchPrintfA(status,ARRAYSIZE(status),"revive;ready=%u;observed=%u;present=%u;applied=%u;held=%u;gen=%lu;reason=%s",
                r.ready?1u:0u,r.reviveMaskObserved?1u:0u,r.reviveMaskPresent?1u:0u,r.applied?1u:0u,r.held?1u:0u,
                static_cast<unsigned long>(r.generation),r.reason?r.reason:"unknown");
            debug::Result(pending.id,1,status);
        } else if (pending.id && std::strncmp(pending.text, "give_item", 9) == 0 && (pending.text[9] == 0 || pending.text[9] == ' ') &&
                   !(mgscoop::GetLabConfig().mode == mgscoop::LabMode::lab && offline_enabled && lab::SavesRedirected())) {
            // F2 lab tooling (debug_equipment.lua "give_item cbox"): it adds a box to the LOCAL
            // player's item list in the running copy, so it is refused here outside the one-PC lab
            // (mode=lab, offline, redirected saves); in the lab it goes on to Lua below.
            debug::Result(pending.id, 0, "give_item=refused;reason=lab_only");
            LogFormat("debug_menu=give_item; id=%u; ok=0; reason=lab_only\r\n", pending.id);
        } else if (pending.id && std::strncmp(pending.text, "vehicle_remove", 14) == 0 && (pending.text[14] == 0 || pending.text[14] == ' ') &&
                   !(mgscoop::GetLabConfig().mode == mgscoop::LabMode::lab && offline_enabled && lab::SavesRedirected())) {
            // 0.13.6 online-vehicle-gone lab lever (debug_vehicle.lua "vehicle_remove nearest [sync]"): it
            // despawns a vehicle of the running copy, so it is refused outside the one-PC lab like
            // give_item (Lua refuses it outside mode=lab too); in the lab it goes on to Lua below.
            debug::Result(pending.id, 0, "vehicle_remove=refused;reason=lab_only");
            LogFormat("debug_menu=vehicle_remove; id=%u; ok=0; reason=lab_only\r\n", pending.id);
        } else if (pending.id && (std::strncmp(pending.text, "vehicle_push", 12) == 0 && (pending.text[12] == 0 || pending.text[12] == ' ') ||
                                  std::strncmp(pending.text, "vehicle_life", 12) == 0 && (pending.text[12] == 0 || pending.text[12] == ' ')) &&
                   !(mgscoop::GetLabConfig().mode == mgscoop::LabMode::lab && offline_enabled && lab::SavesRedirected())) {
            // M4 VEHICLE_STATE lab levers (debug_vehicle.lua "vehicle_push <id> <dx> <dz>", "vehicle_life <id> <life>"):
            // they move / damage a vehicle of the running copy only, so they are refused outside the one-PC lab like
            // vehicle_remove (Lua refuses them outside mode=lab too); in the lab they go on to Lua below.
            debug::Result(pending.id, 0, "vehicle_lever=refused;reason=lab_only");
            LogFormat("debug_menu=vehicle_lever; id=%u; ok=0; reason=lab_only\r\n", pending.id);
        } else if (pending.id && std::strcmp(pending.text, "hud_status") == 0) {
            // Read-only: the in-game status line as the overlay last drew it.
            char status[161]{};
            mgscoop::coop_hud::FormatStatus(mgscoop::coop_hud::GetTelemetry(), status, sizeof(status));
            debug::Result(pending.id, 1, status);
            LogFormat("debug_menu=dispatch; id=%u; text=hud_status; generation=%u; native=1\r\n", pending.id, pending.generation);
        } else if (pending.id) {
            const auto snapshot = debug::GetSnapshot();
            // goto_partner and spawn_sync get the partner pose from the snapshot just
            // published, only for a live, paired, recent network pose (read-only data).
            char pose[96]{};
            if ((std::strcmp(pending.text, "goto_partner") == 0 || std::strcmp(pending.text, "spawn_sync") == 0) && snapshot.remote_valid && snapshot.world_paired &&
                std::strcmp(snapshot.link_state, "connected") == 0 && snapshot.remote_age_ms <= 1000)
                StringCchPrintfA(pose, ARRAYSIZE(pose), ",pp={%ld,%ld,%ld,%u}", std::lround(snapshot.remote_pos[0] * 1000.0f),
                    std::lround(snapshot.remote_pos[1] * 1000.0f), std::lround(snapshot.remote_pos[2] * 1000.0f), snapshot.remote_age_ms);
            // goto_carry (worker test helper) gets the teammate's stock carry point.
            char carry[64]{};float point[3]{};
            if(std::strcmp(pending.text,"goto_carry")==0&&mgscoop::coop_carry::CarryPoint(1,point))
                StringCchPrintfA(carry,ARRAYSIZE(carry),",cp={%ld,%ld,%ld}",std::lround(point[0]*1000.0f),std::lround(point[1]*1000.0f),std::lround(point[2]*1000.0f));
            StringCchPrintfA(debug_reply, ARRAYSIZE(debug_reply), "return {id=%u,text=\"%s\",host=%s,paired=%s,mission=%u%s%s}", pending.id, pending.text,
                snapshot.host ? "true" : "false", snapshot.world_paired ? "true" : "false", partner_mission, pose, carry);
            reply = debug_reply;
            std::uintptr_t selectedPlayer=0;std::uint32_t selected=0xffffffffu;SIZE_T read=0;
            if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(partner_addresses.player_slot),&selectedPlayer,sizeof(selectedPlayer),&read)&&read==sizeof(selectedPlayer)&&selectedPlayer)
                ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(selectedPlayer+0x218),&selected,sizeof(selected),&read);
            LogFormat("debug_menu=dispatch; id=%u; text=%s; generation=%u;window=%lu;selected=%lu\r\n", pending.id, pending.text, pending.generation,
                (unsigned long)mgscoop::partner_input_hook::WindowSeq(),(unsigned long)selected);
        }
    } else if (is_warp) {
        // Lua owner thread only (checked above); every other gate is in the handler.
        reply = SoldierWarpReceipt(buffer + sizeof(warp) - 1, size - (sizeof(warp) - 1));
    } else if (is_remote) {
        // Spawn sync history row (lua/coop/world_place.lua): "<0|1> <message>", never a command.
        unsigned short mission = 0;
        const bool accepted = !mission_terminating && ReadMission(mission) && mission == partner_mission &&
            mgscoop::SupportsPartnerBody(mission) && debug::RemoteReceipt(buffer + sizeof(remote) - 1, size - (sizeof(remote) - 1));
        if (accepted) LogFormat("debug_menu=remote; message=%.*s\r\n", static_cast<int>(size - (sizeof(remote) - 1)), buffer + sizeof(remote) - 1);
        reply = accepted ? "return true" : "return false";
    } else if (is_enemy) {
        unsigned short mission = 0;
        const bool accepted = !mission_terminating && ReadMission(mission) && mission == partner_mission &&
            mgscoop::SupportsPartnerBody(mission) && debug::EnemyReceipt(buffer, size);
        reply = accepted ? "return true" : "return false";
    } else if (is_partner) {
        // The Lua owner emits this only after public gameplay/on-foot/demo checks.
        // Recheck native mission ownership before changing the partner driver.
        reply = "return false";
        const char* start = buffer + sizeof(partner_control) - 1;
        const char* end = buffer + size;
        std::uint32_t id = 0;
        unsigned short mission = 0;
        const auto parsed = std::from_chars(start, end, id);
        if (parsed.ec == std::errc{} && id && parsed.ptr < end && *parsed.ptr == ' ' &&
            end - parsed.ptr - 1 <= 160 && partner_gate_ok && !mission_terminating &&
            ReadMission(mission) && mission == partner_mission && mgscoop::SupportsPartnerBody(mission)) {
            char text[161]{};
            const auto text_size = static_cast<std::size_t>(end - parsed.ptr - 1);
            std::memcpy(text, parsed.ptr + 1, text_size);
            partner::Trigger trigger{};
            const bool accepted = partner::ParseTrigger(text, text_size, trigger);
            if (accepted) pd::ApplyTrigger(partner_driver, partner_hooks, trigger, false);
            reply = accepted ? "return true" : "return false";
            LogFormat("debug_menu=partner; id=%u; accepted=%u; text=%s\r\n", id, unsigned(accepted), text);
        }
    } else if (is_state) {
        // Exactly 2 (legacy), 12 or 13 (HUD bits) bounded fields; anything else is ignored.
        debug::LuaStateReceipt(buffer + sizeof(state) - 1, size - (sizeof(state) - 1));
        reply = "return true";
    } else {
        const char* cursor = buffer + sizeof(log) - 1;
        const char* end = buffer + size;
        std::uint32_t first = 0, second = 0;
        const auto a = std::from_chars(cursor, end, first);
        if (a.ec == std::errc{} && a.ptr < end && *a.ptr == ' ') {
            const auto b = std::from_chars(a.ptr + 1, end, second);
            if (b.ec == std::errc{}) {
                // 0 refused (final), 1 ok (final), 2 ok (interim).
                if (b.ptr < end && *b.ptr == ' ' && second <= 2) {
                    char message[192]{};
                    const auto n = static_cast<std::size_t>(end - b.ptr - 1);
                    if (n < sizeof(message)) {
                        std::memcpy(message, b.ptr + 1, n);
                        debug::Result(first, second, message);
                        LogFormat("debug_menu=result; id=%u; ok=%u; message=%s\r\n", first, second, message);
                    }
                }
            }
        }
        reply = "return true";
    }
    replacement = {reply, std::strlen(reply)};
    debug_quiet = true;
    return true;
}

// Public Lua equipment allocation experiment. No direct native allocator or
// equipment dispatch is called here; this receipt supplies a one-use boolean
// to the already fingerprinted, natural TppMain.OnAllocate insertion.
bool SelectOfflineEquipmentAllocationReceipt(const char* buffer,std::size_t size,const char* name,
                                             mgscoop::script_bridge::Replacement& replacement) noexcept {
    constexpr char query[]="return 'MGS_COOP_OFFLINE_EQUIPMENT_GROUPS_PREPLAYER_V2'";
    if(!buffer || !name || size!=sizeof(query)-1 || std::memcmp(buffer,query,size)!=0 ||
       std::strcmp(name,query)!=0) return false;
    mgscoop::equipment_allocation_probe::Context context{};
#ifdef MGS_LOCAL_OFFLINE_EQUIPMENT_TEST
    context.enabled=PartnerGunPiecesWanted();
#endif
    context.offline=offline_enabled; context.lab=lab_active;
    context.allocation=allocation_enabled; context.archive=archive_mounted;
    context.host_source=host_source_selected; context.partner=partner_on; context.code_gate=partner_gate_ok;
    context.saves_redirected=lab::SavesRedirected(); context.allocation_thread=OnAllocationThread();
    context.terminating=termination_scope.Active(); context.native_session=HasDynamiteSessionOwnership();
    context.cleanup_pending=gameplay_restore_pending;
    unsigned short mission=0;
    context.mission_read=ReadMission(mission); context.mission=mission;
    context.supported_mission=mgscoop::SupportsPartnerBody(mission);
    // PREPLAYER_V2 is emitted only after the protected exact-source Lua
    // checks prove current selected package/direct-read, TPP and mission offline.
    // Both archive hashes accepted by MountPlayerArchive contain two slots.
    context.pre_player_source=true; context.requested_count=2;
    pp::State state{};
    pp::Refusal state_refusal=pp::Refusal::player_absent;
    const bool sample=context.enabled && context.offline && context.lab && context.allocation && context.archive &&
        context.host_source && context.partner && context.code_gate && context.saves_redirected &&
        context.allocation_thread && !context.terminating && !context.native_session && !context.cleanup_pending &&
        context.mission_read && mission==allocation_mission && context.supported_mission;
    std::uintptr_t raw_player=0, manager=0, request_state=0;
    std::uintptr_t groups[3]{}; std::uint32_t group_sizes[3]{};
    const auto read=[&](std::uintptr_t object,std::size_t offset,auto& value) noexcept {
        if(!object || object>UINTPTR_MAX-offset || object+offset>UINTPTR_MAX-sizeof(value)) return false;
        SIZE_T copied=0;
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(object+offset),
            &value,sizeof(value),&copied)!=0 && copied==sizeof(value);
    };
    if(sample) {
        context.player_slot_read=read(partner_addresses.player_slot,0,raw_player);
        context.player_slot_null=context.player_slot_read && !raw_player;
        constexpr std::size_t manager_rva=0x2c1e3b0;
        const bool manager_global=game_base && manager_rva<=game_span && sizeof(manager)<=game_span-manager_rva;
        context.equipment_manager_ready=manager_global &&
            read(reinterpret_cast<std::uintptr_t>(game_base),manager_rva,manager) && manager &&
            read(manager,0xf0,request_state) && request_state &&
            read(manager,0x48,groups[0]) && groups[0] && read(groups[0],0x18,group_sizes[0]) && group_sizes[0] &&
            read(manager,0x50,groups[1]) && groups[1] && read(groups[1],0x18,group_sizes[1]) && group_sizes[1] &&
            read(manager,0x58,groups[2]) && groups[2] && read(groups[2],0x18,group_sizes[2]) && group_sizes[2];
        // Never treat the ambiguous ReadState=player_absent as a null proof.
        // A non-null P must still satisfy every original strict actor guard.
        if(context.player_slot_read && !context.player_slot_null) {
            state_refusal=pp::ReadState(partner_addresses,state);
            context.state_ok=state_refusal==pp::Refusal::none;
            context.state_valid=(state.flags & 0x1u)!=0; context.state_online=(state.flags & 0x100u)!=0;
            context.state_sync=state.sync!=0; context.count=state.count; context.local=state.local;
        }
    }
    const char* reason=equipment_allocation_authorization.Try(context);
    stock46_groups_authorized=reason==nullptr; stock46_groups_returned=false;
    static constexpr char yes[]="return true",no[]="return false";
    replacement=reason ? mgscoop::script_bridge::Replacement{no,sizeof(no)-1}
                       : mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1};
    char line[640]{};
    StringCchPrintfA(line,sizeof(line),"equipment_offline_probe=%s; reason=%s read_state=%s mission=%u allocation_mission=%u count=%u local=%u online=%u sync=%u requested_count=%u slot_read=%u slot_null=%u equipment_manager=%u group_sizes=%u,%u,%u attempt_consumed=%u public_calls=lua_only ghost_count=1 remote_gear_loaded=unverified\r\n",
        reason ? "refused" : "authorized",reason ? reason : "ok",sample ? pp::RefusalName(state_refusal) : "not_sampled",
        unsigned(mission),unsigned(allocation_mission),context.count,context.local,unsigned(context.state_online),
        unsigned(context.state_sync),context.requested_count,unsigned(context.player_slot_read),unsigned(context.player_slot_null),
        unsigned(context.equipment_manager_ready),group_sizes[0],group_sizes[1],group_sizes[2],
        unsigned(equipment_allocation_authorization.Attempted()));
    Log(line);
    return true;
}


// One PUBLIC stock46 load request at the natural copied-save allocation owner.
// Native performs read-only preflight/readback only; Lua owns the public call.
bool Stock46AllocationOwner(unsigned short& mission) noexcept {
    return offline_enabled && lab_active && allocation_enabled && archive_mounted &&
        host_source_selected && partner_on && partner_gate_ok && lab::SavesRedirected() &&
        OnAllocationThread() && !termination_scope.Active() && !gameplay_restore_pending &&
        !HasDynamiteSessionOwnership() && !mgscoop::partner_defer::ShuttingDown() && ReadMission(mission) &&
        mission==30010 && mission==allocation_mission;
}
bool SelectStock46PreloadReceipt(const char* buffer,std::size_t size,const char* name,
                               mgscoop::script_bridge::Replacement& replacement) noexcept {
    constexpr char query[]="return 'MGS_COOP_REMOTE_STOCK46_PRELOAD_QUERY_V1'";
    constexpr char returned[]="return 'MGS_COOP_REMOTE_STOCK46_PRELOAD_RETURNED_V1'";
    constexpr char failed[]="return 'MGS_COOP_REMOTE_STOCK46_PRELOAD_FAILED_V1'";
    const auto equals=[&](const char* value,std::size_t bytes) noexcept {
        return buffer && name && size==bytes && std::memcmp(buffer,value,bytes)==0 && std::strcmp(name,value)==0;
    };
    const bool is_query=equals(query,sizeof(query)-1),is_returned=equals(returned,sizeof(returned)-1);
    if(!is_query && !is_returned && !equals(failed,sizeof(failed)-1)) return false;
    bool enabled=false;
#ifdef MGS_LOCAL_STOCK46_PRELOAD_TEST
    // partner_native_equip=manifest|substitute: the generalized preload answers instead.
    enabled=PartnerGunPiecesWanted() && EquipMode()==mgscoop::PartnerNativeEquip::stock46;
#endif
    unsigned short mission=0;
    const bool owner=enabled && Stock46AllocationOwner(mission);
    const auto reader=[](std::uintptr_t address,void* destination,std::size_t bytes) noexcept {
        if(!address || address>UINTPTR_MAX-bytes) return false;
        SIZE_T copied=0;
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),
            destination,bytes,&copied)!=0 && copied==bytes;
    };
    auto read=reader;
    static constexpr char yes[]="return true",no[]="return false";
    if(is_query) {
        mgscoop::equipment_stock_probe::Context context{};
        context.enabled=enabled; context.allocation_owner=OnAllocationThread();
        context.mission_read=ReadMission(mission); context.mission=mission;
        context.offline_owner_gates=owner;
        context.groups_authorized=stock46_groups_authorized; context.groups_returned=stock46_groups_returned;
        mgscoop::equipment_stock_contract::Snapshot candidate{};
        const char* sample_reason="not_sampled";
        if(owner && stock46_groups_authorized && stock46_groups_returned)
            sample_reason=mgscoop::equipment_stock_contract::Capture(
                reinterpret_cast<std::uintptr_t>(game_base),game_span,read,candidate);
        context.preflight_ok=sample_reason==nullptr;
        const char* reason=stock46_authorization.Try(context);
        if(!reason) stock46_before=candidate;
        replacement=reason ? mgscoop::script_bridge::Replacement{no,sizeof(no)-1}
                           : mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1};
        LogFormat("stock46_preload=%s; reason=%s preflight=%s mission=%u owner=%u groups_authorized=%u groups_returned=%u queue_count=%u queue_capacity=%u cache_index=4 cache_refs=%u attempt_consumed=%u public_calls=lua_only equip=46 packages_loaded=unverified remote_gear_loaded=unverified\r\n",
            reason ? "refused" : "authorized",reason ? reason : "ok",sample_reason ? sample_reason : "ok",
            unsigned(mission),unsigned(owner),unsigned(stock46_groups_authorized),unsigned(stock46_groups_returned),
            candidate.queue_count,candidate.queue_capacity,candidate.cache_refs,unsigned(stock46_authorization.Attempted()));
    } else {
        mgscoop::equipment_stock_contract::Snapshot after{};
        const char* sample_reason="not_sampled";
        if(owner && is_returned && stock46_groups_authorized && stock46_groups_returned)
            sample_reason=mgscoop::equipment_stock_contract::Verify(
                reinterpret_cast<std::uintptr_t>(game_base),game_span,read,stock46_before,after);
        const bool accepted=stock46_authorization.Receipt(owner,mission,is_returned,sample_reason==nullptr);
        replacement=accepted ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                             : mgscoop::script_bridge::Replacement{no,sizeof(no)-1};
        LogFormat("stock46_preload=%s; readback=%s mission=%u public_returned=%u queue_before=%u queue_after=%u hash_mask=%u cache_unchanged=%u public_calls=lua_only equip=46 packages_loaded=unverified remote_gear_loaded=unverified\r\n",
            accepted ? "queued_verified" : "receipt_refused",sample_reason ? sample_reason : "ok",unsigned(mission),
            unsigned(is_returned),stock46_before.queue_count,after.queue_count,after.seen_hashes,unsigned(sample_reason==nullptr));
    }
    return true;
}

// The generalized preload (W2; docs/native-puppet.md "Generalized preload"):
// ONE public RequestLoadToEquipMissionBlock of the received manifest's base
// ids at the natural allocation owner, any partner-body mission. The query
// answers the qualified id list as a Lua table (or false); native performs
// only the read-only preflight (no eviction, queue room, no referenced entry)
// and the readback. MGS_LOCAL_STOCK46_PRELOAD_TEST compiles it in; the
// setting partner_native_equip=manifest|substitute and EquipPiecesAdmitted run it.
bool SelectEquipPreloadReceipt(const char* buffer,std::size_t size,const char* name,
                               mgscoop::script_bridge::Replacement& replacement) noexcept {
    constexpr char query[]="return 'MGS_COOP_REMOTE_EQUIP_PRELOAD_QUERY_V2'";
    constexpr char returned[]="return 'MGS_COOP_REMOTE_EQUIP_PRELOAD_RETURNED_V2'";
    constexpr char failed[]="return 'MGS_COOP_REMOTE_EQUIP_PRELOAD_FAILED_V2'";
    const auto equals=[&](const char* value,std::size_t bytes) noexcept {
        return buffer && name && size==bytes && std::memcmp(buffer,value,bytes)==0 && std::strcmp(name,value)==0;
    };
    const bool is_query=equals(query,sizeof(query)-1),is_returned=equals(returned,sizeof(returned)-1);
    if(!is_query && !is_returned && !equals(failed,sizeof(failed)-1)) return false;
    namespace pc=mgscoop::equipment_preload_contract;
    bool enabled=false;
#ifdef MGS_LOCAL_STOCK46_PRELOAD_TEST
    enabled=true;
#endif
    const char* admission=enabled ? EquipAdmissionReason() : "not_compiled";
    enabled=enabled && admission==nullptr;
    unsigned short mission=0;
    const bool mission_read=ReadMission(mission);
    const bool owner=enabled && allocation_enabled && archive_mounted && host_source_selected &&
        OnAllocationThread() && !termination_scope.Active() && !gameplay_restore_pending &&
        !HasDynamiteSessionOwnership() && mission_read && mgscoop::SupportsPartnerBody(mission) && mission==allocation_mission;
    const auto reader=[](std::uintptr_t address,void* destination,std::size_t bytes) noexcept {
        if(!address || address>UINTPTR_MAX-bytes) return false;
        SIZE_T copied=0;
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),
            destination,bytes,&copied)!=0 && copied==bytes;
    };
    auto read=reader;
    const auto base=reinterpret_cast<std::uintptr_t>(game_base);
    static constexpr char yes[]="return true",no[]="return false";
    static char reply[64]{};
    if(is_query) {
        // The remote's manifest may have arrived just now (it is sent from the title
        // and the ACC too): drain channel 2 once before planning (nothing is sent here).
        if(partner_on && partner_gate_ok) pd::DrainEquipLink(partner_driver,partner_hooks);
        mgscoop::equipment_stock_probe::GeneralContext context{};
        context.enabled=enabled; context.allocation_owner=OnAllocationThread();
        context.mission_read=mission_read; context.mission=mission; context.supported=mgscoop::SupportsPartnerBody(mission);
        context.owner_gates=owner;
        context.groups_authorized=stock46_groups_authorized; context.groups_returned=stock46_groups_returned;
        mgscoop::partner_equip_wire::Manifest manifest{};
        // Two-PC test 2026-10-23 item 2: without the peer's manifest, a peer that
        // restarted on this host's save (a save handoff in this process) loads
        // this copy's own loadout first: its own manifest predicts the ids.
        namespace sp=mgscoop::equipment_stock_probe;
        const bool remote=enabled && EquipManifest(manifest);
        const bool handed=enabled && mgscoop::save_handoff::PeerOnHandedSave();
        mgscoop::partner_equip_wire::Manifest own{};
        const bool own_ok=handed && !remote && pd::OwnManifest(partner_driver,own);
        const sp::PreloadSource source=sp::ChoosePreloadSource(remote,handed,own_ok);
        if(source==sp::PreloadSource::predicted) manifest=own;
        context.manifest=source!=sp::PreloadSource::none;
        // Candidates: each slot's base id that is a stock gun of that slot's type.
        std::uint16_t ids[pc::kIds]{};unsigned count=0;
        char dropped[96]{};
        const char* sample_reason="not_sampled";
        pc::Snapshot& candidate=equip_preload_before;
        candidate={};
        if(context.manifest) {
            mgscoop::equipment_stock_writer_contract::View<decltype(read)> v{read,base,game_span};
            std::uintptr_t container=0,globals=0,services=0,engine=0;
            const bool engine_ok=v.Image(0x2C3A8A0) && v.At(base,0x2C3A8A0,container) && v.At(container,8,globals) &&
                v.At(globals,0x98,services) && v.At(services,0x1E8,engine) && v.Method(engine,0x234B840,0x280,0xDC3840);
            for(unsigned slot=0;slot<pc::kIds;++slot) {
                const std::uint16_t id=manifest.slots[slot].base_id;
                if(!id) continue;
                bool duplicate=false;
                for(unsigned j=0;j<count;++j) duplicate=duplicate || ids[j]==id;
                if(duplicate) continue;
                const char* why=engine_ok ? nullptr : "engine";
                pc::Item item{};
                if(!why) why=pc::Hashes(v,engine,id,item);
                if(!why && mgscoop::equipment_stock_writer_contract::TypeSlot(item.type)!=slot) why="type_slot";
                // Version 2: the slot's own type (never preload a base of another type).
                if(!why && manifest.slots[slot].type && item.type!=manifest.slots[slot].type) why="type_mismatch";
                // Alone it must not evict or touch a referenced entry.
                if(!why && owner) {const std::uint16_t one[1]{id};why=pc::Capture(base,game_span,read,one,1,candidate);}
                if(why) {
                    char item_text[40]{};
                    StringCchPrintfA(item_text,sizeof(item_text),"%s%u:%s",dropped[0] ? "," : "",unsigned(id),why);
                    StringCchCatA(dropped,sizeof(dropped),item_text);
                    continue;
                }
                ids[count++]=id;
            }
        }
        // Together (each build takes the next unused entry): drop from the end until it fits.
        while(owner && count) {
            sample_reason=pc::Capture(base,game_span,read,ids,count,candidate);
            if(!sample_reason || std::strcmp(sample_reason,"cache_evict")!=0) break;
            --count;
        }
        if(!owner || !count) candidate={};
        context.ids=count;
        // The exact code and tables the prediction rests on (A03C40, A02290, DC3700, DB6AC0, ...).
        std::uintptr_t mismatch=0;
        if(owner && count && !sample_reason && !mgscoop::equipment_stock_writer::PinsIntact(base,game_span,mismatch)) sample_reason="full_body_gate";
        context.preflight_ok=owner && count && sample_reason==nullptr;
        const char* reason=equip_preload_authorization.Try(context);
        partner_driver.preload_mission=mission;
        partner_driver.preload_count=0;partner_driver.preload_new_hashes=0;
        char list[48]{};
        if(!reason) {
            for(unsigned i=0;i<count;++i) {
                char one[12]{};StringCchPrintfA(one,sizeof(one),"%s%u",i ? "," : "",unsigned(ids[i]));
                StringCchCatA(list,sizeof(list),one);
                partner_driver.preload_ids[i]=ids[i];
            }
            partner_driver.preload_count=static_cast<std::uint8_t>(count);
            partner_driver.preload_new_hashes=candidate.new_hashes;
            StringCchPrintfA(reply,sizeof(reply),"return {%s}",list);
            replacement={reply,std::strlen(reply)};
        } else {
            replacement={no,sizeof(no)-1};
        }
        StringCchCopyA(partner_driver.preload_verdict,sizeof(partner_driver.preload_verdict),reason ? "refused" : "authorized");
        StringCchCopyA(partner_driver.preload_reason,sizeof(partner_driver.preload_reason),reason ? reason : "ok");
        partner_driver.preload_source=static_cast<std::uint8_t>(source);
        partner_driver.preload_late_watch=enabled && source!=sp::PreloadSource::remote;
        char actions[96]{};
        for(unsigned i=0;i<candidate.item_count && !reason;++i) {
            char one[32]{};
            StringCchPrintfA(one,sizeof(one),"%s%u:%s@%u",i ? "," : "",unsigned(candidate.items[i].equip),
                pc::ActionName(candidate.items[i].action),unsigned(candidate.items[i].entry));
            StringCchCatA(actions,sizeof(actions),one);
        }
        LogFormat("equip_preload=%s; reason=%s; admission=%s; preflight=%s; mission=%u; owner=%u; manifest=%u; source=%s; want=%u,%u,%u; ids=%s; dropped=%s; actions=%s; new_hashes=%u; queue=%u/%u; attempt_consumed=%u; public_calls=lua_only; evict=0\r\n",
            reason ? "refused" : "authorized",reason ? reason : "ok",admission ? admission : "ok",sample_reason ? sample_reason : "ok",unsigned(mission),unsigned(owner),
            unsigned(context.manifest),sp::PreloadSourceName(source),unsigned(manifest.slots[0].base_id),unsigned(manifest.slots[1].base_id),unsigned(manifest.slots[2].base_id),
            list[0] ? list : "none",dropped[0] ? dropped : "none",actions[0] ? actions : "none",candidate.new_hashes,
            candidate.queue_count,candidate.queue_capacity,unsigned(equip_preload_authorization.Attempted()));
    } else {
        pc::Snapshot& after=equip_preload_after;
        after={};
        const char* sample_reason="not_sampled";
        if(owner && is_returned && stock46_groups_authorized && stock46_groups_returned && equip_preload_before.item_count)
            sample_reason=pc::Verify(base,game_span,read,equip_preload_before,after);
        const bool accepted=equip_preload_authorization.Receipt(owner,mission,is_returned,sample_reason==nullptr);
        replacement=accepted ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                             : mgscoop::script_bridge::Replacement{no,sizeof(no)-1};
        StringCchCopyA(partner_driver.preload_verdict,sizeof(partner_driver.preload_verdict),accepted ? "queued_verified" : "receipt_refused");
        StringCchCopyA(partner_driver.preload_reason,sizeof(partner_driver.preload_reason),sample_reason ? sample_reason : "ok");
        LogFormat("equip_preload=%s; readback=%s; mission=%u; public_returned=%u; queue_before=%u; queue_after=%u; new_hashes=%u; packages_loaded=unverified\r\n",
            accepted ? "queued_verified" : "receipt_refused",sample_reason ? sample_reason : "ok",unsigned(mission),
            unsigned(is_returned),equip_preload_before.queue_count,after.queue_count,equip_preload_before.new_hashes);
    }
    return true;
}

bool SelectEquipmentObservationReceipt(const char* buffer,std::size_t size,const char* name,
                                      mgscoop::script_bridge::Replacement& replacement) noexcept {
    constexpr char query[]="return 'MGS_COOP_EQUIPMENT_OBSERVATION_QUERY_V1'";
    constexpr char receipt[]="return 'MGS_COOP_EQUIPMENT_OBSERVATION_RECEIPT_V1' -- ";
    const bool is_query=buffer && size==sizeof(query)-1 && std::memcmp(buffer,query,size)==0;
    const bool is_receipt=buffer && size>sizeof(receipt)-1 && size<768 && std::memcmp(buffer,receipt,sizeof(receipt)-1)==0;
    if((!is_query && !is_receipt) || !name || std::strlen(name)!=size || std::memcmp(buffer,name,size)!=0) return false;
    static constexpr char no[]="return nil",yes[]="return true";
    replacement={no,sizeof(no)-1};
    bool ready=false;
    unsigned short mission=0;
#ifdef MGS_LOCAL_OFFLINE_EQUIPMENT_TEST
    pp::State state{};
    ready=offline_enabled && lab_active && lab::SavesRedirected() && partner_on && partner_gate_ok &&
        archive_mounted && host_source_selected && OnRuntimeThread() && !mission_terminating &&
        !gameplay_restore_pending && !HasDynamiteSessionOwnership() && ReadMission(mission) &&
        mission==partner_mission && mgscoop::SupportsPartnerBody(mission) &&
        pp::ReadState(partner_addresses,state)==pp::Refusal::none && state.count==2 && state.local==0 &&
        (state.flags & 1u) && !(state.flags & 0x100u) && !state.sync && state.vars_ok;
#endif
    if(is_query) {
        equipment_observation_requested_ms=0; equipment_observation_mission=0;
#ifdef MGS_LOCAL_OFFLINE_EQUIPMENT_TEST
        static constexpr char waiting[]="return false";
        replacement={waiting,sizeof(waiting)-1};
#endif
        if(ready) {
            equipment_observation_requested_ms=GetTickCount64(); equipment_observation_mission=mission;
            replacement={equipment_observation_source,sizeof(equipment_observation_source)-1};
        }
        return true;
    }
    const auto now=GetTickCount64();
    const bool pending=equipment_observation_requested_ms && now>=equipment_observation_requested_ms &&
        now-equipment_observation_requested_ms<=2000 && mission==equipment_observation_mission;
    equipment_observation_requested_ms=0; equipment_observation_mission=0;
    if(!ready || !pending) return true;
    const auto payload=buffer+sizeof(receipt)-1;
    const auto payload_size=size-(sizeof(receipt)-1);
    char prefix[80]{};
    StringCchPrintfA(prefix,sizeof(prefix),"MGS_COOP_EQUIPMENT_OBSERVATION_V1;mission=%u;",unsigned(mission));
    const auto prefix_size=std::strlen(prefix);
    if(payload_size<=prefix_size || std::memcmp(payload,prefix,prefix_size)!=0) return true;
    for(std::size_t i=0;i<payload_size;++i) {
        const auto c=static_cast<unsigned char>(payload[i]);
        if(!((c>='0' && c<='9') || (c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_' || c=='=' || c==';' || c==',' || c=='?')) return true;
    }
    LogFormat("equipment_observation=%.*s\r\n",int(payload_size),payload);
    replacement={yes,sizeof(yes)-1};
    return true;
}

// Separate raw-event data experiment. It cannot enable a native gun backend.
// A source-owned natural Lua owner must pass the offline copied-save gate on
// every receipt; packet context never supplies game mission authorization.
bool SelectActionEventsReceipt(const char* buffer,std::size_t size,const char* name,
                               mgscoop::script_bridge::Replacement& replacement) noexcept {
    constexpr char query[]="return 'MGS_COOP_ACTION_EVENTS_CFG_V1'";
    constexpr char clear[]="return 'MGS_COOP_ACTION_EVENTS_CLEAR_V1'";
    constexpr char receipt[]="return 'MGS_COOP_PARTNER_ACTIONS_V1' -- ";
    const bool is_query=buffer && size==sizeof(query)-1 && std::memcmp(buffer,query,size)==0;
    const bool is_clear=buffer && size==sizeof(clear)-1 && std::memcmp(buffer,clear,size)==0;
    const bool is_receipt=buffer && size>sizeof(receipt)-1 && size<=512 && std::memcmp(buffer,receipt,sizeof(receipt)-1)==0;
    if((!is_query && !is_clear && !is_receipt) || !name || std::strlen(name)!=size || std::memcmp(buffer,name,size)!=0) return false;
    static constexpr char disabled[]="return nil",yes[]="return true",no[]="return false";
    replacement={disabled,sizeof(disabled)-1};
    debug_quiet=true;
    if(!mgscoop::partner_net::ActionEventsCompiled()) return true;
    replacement={no,sizeof(no)-1};
    const bool owner=OnRuntimeThread();
    if(is_clear) {
        if((owner || OnTerminationThread()) && offline_enabled && lab_active && lab::SavesRedirected()) {
            mgscoop::partner_net::ClearLocalActions(); replacement={yes,sizeof(yes)-1};
        }
        return true;
    }
    unsigned short mission=0;
    pp::State state{};
    const bool ready=owner && offline_enabled && lab_active && lab::SavesRedirected() &&
        partner_on && partner_gate_ok && archive_mounted && host_source_selected && !mission_terminating &&
        !gameplay_restore_pending && !HasDynamiteSessionOwnership() && ReadMission(mission) &&
        mission==partner_mission && mgscoop::SupportsPartnerBody(mission) &&
        pp::ReadState(partner_addresses,state)==pp::Refusal::none && state.count==2 && state.local==0 &&
        (state.flags & 1u) && !(state.flags & 0x100u) && !state.sync && state.vars_ok;
    if(!ready) { if(owner) mgscoop::partner_net::ClearLocalActions(); return true; }
    if(is_query) {
        replacement={yes,sizeof(yes)-1};
        LogFormat("partner_action_events=authorized; mission=%u; owner=%lu; native_playback=0\r\n",unsigned(mission),GetCurrentThreadId());
        return true;
    }
    mgscoop::partner_actions::Payload payload{};
    const bool accepted=mgscoop::partner_actions::DecodeReceipt(buffer,size,payload) && payload.mission==mission &&
        mgscoop::partner_net::SetLocalActions(payload);
    if(!accepted) { mgscoop::partner_net::ClearLocalActions(); return true; }
    replacement={yes,sizeof(yes)-1};
    static std::uint32_t logged_mission=0, generation=0, cursor=UINT32_MAX, flags=UINT32_MAX;
    static unsigned lines=0;
    if(logged_mission!=payload.mission || generation!=payload.generation) {
        logged_mission=payload.mission; generation=payload.generation; cursor=flags=UINT32_MAX; lines=0;
    }
    if(cursor!=payload.cursor || flags!=payload.flags) {
        cursor=payload.cursor; flags=payload.flags;
        if(lines++<96)
            LogFormat("partner_action_events=local; mission=%u; generation=%u; equip=%u; stance=%u; flags=0x%x; fire_count=%u; reload_count=%u; cursor=%u; history=%u; fire_source=%u; reload_source=%u; native_playback=0\r\n",
                unsigned(payload.mission),unsigned(payload.generation),unsigned(payload.equip),unsigned(payload.stance),unsigned(payload.flags),
                unsigned(payload.fire_count),unsigned(payload.reload_count),unsigned(payload.cursor),unsigned(payload.count),
                unsigned(payload.fire_source),unsigned(payload.reload_source));
    }
    return true;
}

bool SelectMissionBoundary(const char* buffer,std::size_t size,const char* name,
                           mgscoop::script_bridge::Replacement& replacement) noexcept {
    if(!host_source_selected || !buffer || !name || size>=96 ||
       std::strlen(name)!=size || std::memcmp(name,buffer,size)!=0) return false;
    constexpr char begin[]="return 'MGS_COOP_ALLOCATION_ENTER_V1'";
    constexpr char end[]="return 'MGS_COOP_ALLOCATION_RETURNED_V1'";
    constexpr char initialize[]="return 'MGS_COOP_INITIALIZATION_ENTER_V1'";
    constexpr char initialized[]="return 'MGS_COOP_INITIALIZATION_RETURNED_V1'";
    constexpr char start[]="return 'MGS_COOP_RUNTIME_ENTER_V1'";
    constexpr char terminate[]="return 'MGS_COOP_TERMINATION_RETURNED_V1'";
    const auto equals=[&](const auto& value) {return size==sizeof(value)-1 && std::memcmp(buffer,value,size)==0;};
    bool accepted=false;
    const char* boundary=nullptr;
    if(equals(begin)) {
        boundary="allocation_enter";
        unsigned short mission=0;
        if(mission_phase==MissionPhase::idle && !termination_scope.Active() && !HasDynamiteSessionOwnership() && ReadMission(mission) &&
           TransferRuleThread(cleanup_thread ? cleanup_thread : mission_thread)) {
            ClearUpperEndOwner(); mgscoop::partner_upper_probe::Clear("allocation_enter");
            allocation_thread=GetCurrentThreadId(); allocation_mission=mission;
            cleanup_thread=0;
            initialization_scope.Reset();
            mission_phase=MissionPhase::allocating; accepted=true;
            if(!worker_loading_exhausted&&worker_loading_generation!=UINT32_MAX)++worker_loading_generation;
            else {worker_loading_exhausted=true;worker_loading_generation=0;}
            mgscoop::worker_scenario::SetLoadingContext({mgscoop::worker_scenario::LoadingPhase::unavailable,mission,worker_loading_generation});
            equipment_allocation_authorization.Begin(mission);
            stock46_authorization.Begin(mission); stock46_before={};
            equip_preload_authorization.Begin(mission); equip_preload_before={};
            mgscoop::equipment_stock_writer::BeginAllocation(mission);
            stock46_groups_authorized=stock46_groups_returned=false;
            equipment_observation_requested_ms=0; equipment_observation_mission=0;
        }
    } else if(equals(end)) {
        boundary="allocation_returned";
        if(OnAllocationThread()) { equipment_allocation_authorization.End(); stock46_authorization.SealAllocation(); equip_preload_authorization.SealAllocation(); mission_phase=MissionPhase::allocated; accepted=true; }
    } else if(equals(initialize)) {
        boundary="initialization_enter";
        unsigned short mission=0;
        accepted=mission_phase==MissionPhase::allocated && ReadMission(mission) && mission==allocation_mission &&
            initialization_scope.Enter(GetCurrentThreadId());
    } else if(equals(initialized)) {
        boundary="initialization_returned";
        accepted=mission_phase==MissionPhase::allocated && initialization_scope.Returned(GetCurrentThreadId());
        if(accepted&&initialization_scope.Complete()&&!termination_scope.Active())
            mgscoop::worker_scenario::SetLoadingContext({mgscoop::worker_scenario::LoadingPhase::initialized,allocation_mission,worker_loading_generation});
    } else if(equals(start)) {
        boundary="runtime_enter";
        // Revoke before the original runtime callback even if later admission fails.
        mgscoop::worker_scenario::SetLoadingContext({mgscoop::worker_scenario::LoadingPhase::running,allocation_mission,worker_loading_generation});
        unsigned short mission=0;
        // OnInitialize is a loading job, not proof of the gameplay thread.
        // Both loading callbacks must return before this natural game-start
        // entry transfers rule/session metadata from the allocation worker.
        if(mission_phase==MissionPhase::allocated && initialization_scope.Complete() &&
           ReadMission(mission) && mission==allocation_mission) {
            accepted=TransferRuleThread(allocation_thread) &&
                AdoptDynamiteSessionThread(game_base,allocation_thread,Log);
            if(accepted) {
                // A new mission runs: the previous mission's termination is over. Offline
                // nothing else clears this flag (the host-create / result / session-close
                // receipts belong to the online route), and a stale flag refuses the world
                // CFG start of every later mission (40050, 30010).
                mission_terminating=false;
                mission_thread=GetCurrentThreadId(); mission_phase=MissionPhase::running; PartnerMissionStart(allocation_mission);
                if(world_on) mgscoop::world_link::MissionRuntime(allocation_mission);
                mgscoop::save_handoff::NoteMission(allocation_mission,false);
            }
            else RestoreGameplayRules();
        }
    } else if(equals(terminate)) {
        boundary="termination_returned";
        mgscoop::worker_scenario::SetLoadingContext({mgscoop::worker_scenario::LoadingPhase::unavailable,allocation_mission,worker_loading_generation});
        if(OnTerminationThread()) {
            const bool owned_before=HasDynamiteSessionOwnership();
            ReconcileDynamiteSessionRelease(game_base,game_span,Log);
            if(owned_before && !HasDynamiteSessionOwnership()) ApplySessionReleased();
            cleanup_thread=GetCurrentThreadId();
            accepted=termination_scope.Returned(cleanup_thread);
            equipment_allocation_authorization.End();
            stock46_authorization.End(); stock46_before={};
            equip_preload_authorization.End(); equip_preload_before={};
            mgscoop::equipment_stock_writer::End();
            stock46_groups_authorized=stock46_groups_returned=false;
            mission_phase=MissionPhase::idle; allocation_thread=0;
            PartnerMissionEnd();
            if(world_on) mgscoop::world_link::LocalMissionEnd();
            mgscoop::save_handoff::NoteMission(0,true);
        }
    } else return false;
    static constexpr char yes[]="return true",no[]="return false";
    replacement=accepted ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
        : mgscoop::script_bridge::Replacement{no,sizeof(no)-1};
    char line[288]{};
    StringCchPrintfA(line,sizeof(line),"mission_boundary=%s; accepted=%u thread=%lu allocation_thread=%lu runtime_thread=%lu mission=%u phase=%u\r\n",
        boundary,unsigned(accepted),GetCurrentThreadId(),allocation_thread,mission_thread,unsigned(allocation_mission),unsigned(mission_phase));
    Log(line);
    return true;
}

// --- Client join arming, results and reset/re-arm (synthesis gap G11) ---------
// All of these run inside ObserveEntry under receipt_lock on the runtime thread.

// Protocol armed source: auto whenever an auto-join delay is configured, then
// trigger_file when join.trigger is honoured, else the keyboard.
const char* ArmedSource() noexcept {
    const auto& labc=mgscoop::GetLabConfig();
    return labc.auto_join_delay_s>0 ? "auto" : labc.join_trigger ? "trigger_file" : "keyboard";
}
// Lab only: the guest can now be triggered.
void AnnounceArmed() noexcept {
    if(!lab_active) return;
    char line[96]{};
    StringCchPrintfA(line,sizeof(line),"client_join=armed; source=%s\r\n",ArmedSource());
    Log(line);
    SetLabJoin(lab::JoinStatus::armed);
}
// Lab status after a join attempt. A refusal before the native request is
// "refused" with its reason (logged once as a protocol line); a native request
// that returned false is "failed" (its dynamite_client=returned line exists).
void ReportJoinResult(bool accepted) noexcept {
    if(accepted) {
        if(lab_active && !client_check_enabled) auto_reset_watch.Submitted(GetTickCount64());
        return;
    }
    if(!lab_active) return;
    bool logged=false;
    const char* reason=LastClientJoinRefusal(&logged);
    if(!reason || !reason[0]) reason="unspecified";
    const auto join=lab::JoinStatusForRefusal(reason);
    if(join!=lab::JoinStatus::failed && !logged) {
        char line[128]{};
        StringCchPrintfA(line,sizeof(line),"client_join=refused; reason=%s\r\n",reason);
        Log(line);
    }
    SetLabRefusal(reason,join);
}
// Reports a refused reset request and returns the machine to idle. With
// `attempt_refused` the current attempt is marked refused in lab-status.json;
// otherwise only last_refusal changes.
void RefuseReset(const char* refusal,mgscoop::ClientResetReason trigger,bool attempt_refused) noexcept {
    char line[160]{};
    StringCchPrintfA(line,sizeof(line),"client_join=refused; reason=%s; trigger=%s\r\n",
        refusal,mgscoop::ClientResetReasonName(trigger));
    Log(line);
    if(attempt_refused) SetLabRefusal(refusal,lab::JoinStatus::refused);
    else SetLabLastRefusal(refusal);
    reset_machine.Clear();
}
// Runs one reset request. Returns the Lua directive literal after a successful
// re-arm, otherwise nullptr (every refusal is logged with its reason).
const char* RunClientReset(mgscoop::ClientResetReason reason,bool automatic) noexcept {
    if(reset_machine.active()) return nullptr;
    const bool context=!mission_terminating && !gameplay_restore_pending && mgscoop::player_team::IsActive();
    // A client session the game already released no longer backs the target.
    // Clear the adapter's ownership record through the existing slot-only
    // check (no game call, no lazy getter, no dereference of the session).
    if(HasDynamiteSessionOwnership()) {
        ReconcileDynamiteSessionRelease(game_base,game_span,Log);
        if(!HasDynamiteSessionOwnership()) ApplySessionReleased();
    }
    // Any ownership record that could not be reconciled (including one held
    // for another thread) still counts as a live session; client mode never
    // holds a host session.
    const bool recorded=HasRecordedClientRequest();
    const bool owns=mgscoop::ClientResetOwnsSession(HasDynamiteSessionOwnership(),recorded,
        recorded && RecordedClientRequestSlotsEmpty(game_base,game_span));
    switch(reset_machine.Request(reason,client_join_consumed,owns,context)) {
    case mgscoop::ClientResetAction::restore_rearm:
        break;
    case mgscoop::ClientResetAction::close_session:
        // No guest has ever connected; closing a live client session
        // mid-mission is not statically verified safe, so refuse.
        RefuseReset("owned_session_active",reason,true);
        return nullptr;
    case mgscoop::ClientResetAction::refuse:
        // nothing_to_reset or not_ready: the latches and attempt are unchanged.
        RefuseReset(reset_machine.refusal(),reason,false);
        return nullptr;
    default:
        reset_machine.Clear();
        return nullptr;
    }
    if(!RestoreClientRequestFields(game_base,game_span,Log)) {
        RefuseReset("restore_failed",reason,true);
        return nullptr;
    }
    ResetClientAttemptLatch();
    client_join_consumed=false;
    client_join_armed=true;  // Request required a ready scene
    client_key_released=false;
    armed_tick_ms=GetTickCount64();
    auto_join_fired=false;
    auto_reset_watch.Stop();
    if(automatic) auto_reset_watch.CountAutomaticReset();
    reset_machine.CompleteRestore();
    char line[160]{};
    StringCchPrintfA(line,sizeof(line),"client_join=reset; reason=%s; join_attempts=%u\r\n",
        mgscoop::ClientResetReasonName(reason),join_attempts);
    Log(line);
    if(lab_active) lab::SetRefusal(lab_status,nullptr);  // a fresh attempt starts
    AnnounceArmed();
    return mgscoop::ClientRearmLiteral(client_check_enabled);
}
// One reset-poll receipt: reset.trigger (lab, join_trigger=1), the Ctrl+F9
// edge (any client mode), or a lab automatic trigger for the submitted request.
const char* HandleResetPoll() noexcept {
    using Reason=mgscoop::ClientResetReason;
    Reason reason=Reason::none;
    bool automatic=false;
    // Trigger files are honoured only with join_trigger=1 (docs/lab-protocol.md).
    if(lab_active && mgscoop::GetLabConfig().join_trigger && ConsumeTriggerFile(reset_trigger_path))
        reason=Reason::trigger_file;
    else {
        // Ctrl+F9 edge, foreground-gated, available in any client mode.
        DWORD fg=0; const HWND f=GetForegroundWindow(); if(f) GetWindowThreadProcessId(f,&fg);
        if(fg!=GetCurrentProcessId()) reset_key_released=false;
        else if((GetAsyncKeyState(VK_F9)&0x8000)==0) reset_key_released=true;
        else if(reset_key_released && (GetAsyncKeyState(VK_CONTROL)&0x8000)!=0) {
            reset_key_released=false; reason=Reason::manual_key;
        } else reset_key_released=false;
    }
    // Automatic triggers (lab only): session error, a bad state, a released
    // session, or no client_established within the pending timeout. Sampled at
    // most once per second with read-only session reads.
    if(reason==Reason::none && lab_active) {
        const std::uint64_t now=GetTickCount64();
        if(auto_reset_watch.SampleDue(now)) {
            const auto health=ObserveOwnedClientSession(game_base,game_span);
            const auto observed=auto_reset_watch.Observe(health,now);
            if(observed!=Reason::none) {
                char line[128]{};
                StringCchPrintfA(line,sizeof(line),"client_join=reset_requested; trigger=%s; health=%s\r\n",
                    mgscoop::ClientResetReasonName(observed),mgscoop::ClientSessionHealthName(health));
                Log(line);
                if(auto_reset_watch.AutomaticResetAllowed()) { reason=observed; automatic=true; }
                else RefuseReset("auto_reset_limit",observed,true);
            }
        }
    }
    const char* directive=reason!=Reason::none ? RunClientReset(reason,automatic) : nullptr;
    SyncLabStatus();  // retries a failed status write (throttled)
    return directive;
}

// Bounded diagnostic copy only. A bad name cannot prevent original forwarding.
void CopyName(const char* source, char (&output)[241]) noexcept {
    if (!source) { lstrcpyA(output, "<null>"); return; }
    __try {
        for (unsigned i = 0; i < sizeof(output)-1; ++i) {
            const unsigned char c = static_cast<unsigned char>(source[i]);
            if (!c) return;
            output[i] = c >= 32 && c < 127 ? static_cast<char>(c) : '?';
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { lstrcpyA(output, "<unreadable>"); }
}
// Partner receipts (partner_on only; called under receipt_lock). The tick is
// quiet: no entry line, and ObserveReturn skips its return line. CFG, LOG and
// STOP keep the ordinary entry/return lines.
bool SelectPartnerReceipt(partner::Receipt kind, const char* buffer, std::size_t size, const char* name, LONG64 count,
                          mgscoop::script_bridge::Replacement& replacement) noexcept {
    const char* reply = nullptr;
    switch (kind) {
    case partner::Receipt::tick: reply = HandlePartnerTick(buffer, size); break;
    case partner::Receipt::cfg: reply = HandlePartnerCfg(); break;
    case partner::Receipt::log: reply = HandlePartnerLog(buffer, size); break;
    case partner::Receipt::stop: reply = HandlePartnerStop(); break;
    case partner::Receipt::seat: reply = HandlePartnerSeat(buffer, size); break;
    default: break;
    }
    if (reply) replacement = {reply, std::strlen(reply)};
    pd::Arm(partner_quiet, kind, reply != nullptr);
    if (!pd::LogsEntry(kind)) return reply != nullptr;
    char copied[241]{}, line[512]{};
    CopyName(name, copied);
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
        "entry_count=%lld thread=%lu tick_ms=%llu size=%llu name_prefix=%s replacement=%d execution_receipt=%d\r\n",
        count, GetCurrentThreadId(), GetTickCount64(), static_cast<unsigned long long>(size), copied,
        reply ? 1 : 0, 0))) Log(line);
    return reply != nullptr;
}
// World sync receipts (world_on only; called under receipt_lock). CFG query
// runs on any thread, CFG start on the runtime thread before termination,
// TX on the runtime thread (MISSION_END also on the termination thread), RX
// on the runtime thread, LOG on the runtime or termination thread, WARP (guest
// spawn sync) on the runtime thread, NOTE (on-screen world notes) on the
// runtime or termination thread, SAVE (the save handoff report, join-fix
// phase 2 P2-2) on the runtime thread only. A refused receipt stays literal
// (Lua treats it as inactive or not sent).
bool SelectWorldReceipt(mgscoop::world::Receipt kind, const char* buffer, std::size_t size, const char* name, LONG64 count,
                        mgscoop::script_bridge::Replacement& replacement) noexcept {
    namespace world=mgscoop::world;
    const char* reply=nullptr;
    unsigned short mission=0;
    if(kind==world::Receipt::cfg) {
        std::uint32_t lua_mission=0; std::uint8_t phase=0;
        if(world::ParseCfg(buffer,size,lua_mission,phase) && (phase==0 || (OnRuntimeThread() && !mission_terminating))) {
            ReadMission(mission);
            reply=mgscoop::world_link::HandleCfg(buffer,size,mission,mission_terminating);
        }
    } else if(kind==world::Receipt::tx) {
        constexpr char end_prefix[]="return 'MGS_COOP_WORLD_TX_V1' -- 7 ";
        const bool mission_end=size>sizeof(end_prefix)-1 && std::memcmp(buffer,end_prefix,sizeof(end_prefix)-1)==0;
        if(OnRuntimeThread() || (mission_end && OnTerminationThread())) reply=mgscoop::world_link::HandleTx(buffer,size);
    } else if(kind==world::Receipt::rx) {
        if(OnRuntimeThread()) { ReadMission(mission); reply=mgscoop::world_link::HandleRx(buffer,size,mission,mission_terminating); }
    } else if(kind==world::Receipt::log) {
        if(OnRuntimeThread() || OnTerminationThread()) reply=mgscoop::world_link::HandleLog(buffer,size);
    } else if(kind==world::Receipt::warp) {
        // Lua owner thread only; every other gate is in the handler.
        if(OnRuntimeThread()) reply=GuestWarpReceipt(buffer,size);  // ParseWarp checks the prefix itself
    } else if(kind==world::Receipt::note) {
        if(OnRuntimeThread() || OnTerminationThread()) reply=mgscoop::world_link::HandleNote(buffer,size);
    } else if(kind==world::Receipt::save) {
        if(OnRuntimeThread()) reply=mgscoop::save_handoff::HandleLua(buffer,size);
    }
    if(reply) replacement={reply,std::strlen(reply)};
    world_quiet=kind!=world::Receipt::cfg;
    if(world_quiet) return reply!=nullptr;
    char copied[241]{}, line[512]{};
    CopyName(name,copied);
    if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),
        "entry_count=%lld thread=%lu tick_ms=%llu size=%llu name_prefix=%s replacement=%d execution_receipt=%d\r\n",
        count,GetCurrentThreadId(),GetTickCount64(),static_cast<unsigned long long>(size),copied,reply ? 1 : 0,0))) Log(line);
    return reply!=nullptr;
}
// Read-only Dog pose. The fixed receipt derives every owner fact here; Lua
// supplies no pointer, thread, generation or native admission flag.
template<class Pin,std::size_t N> bool BuddyPinsMatch(void* base,std::size_t span,const Pin (&pins)[N]) noexcept {
    unsigned char bytes[16384]{};
    for(const auto& pin:pins) {
        if(!pin.bytes || pin.bytes>sizeof(bytes) || pin.rva>span || pin.bytes>span-pin.rva ||
           !UpperPhaseRead(nullptr,reinterpret_cast<std::uintptr_t>(base)+pin.rva,bytes,pin.bytes)) return false;
        // The research pose window detours 0xBF8A20 (registry constructor) with
        // a 5-byte E9 over its exact 5-byte first instruction; the rest of the
        // pinned body is untouched, so the original head is compared instead.
        static constexpr unsigned char kRegistryHead[5]={0x48,0x89,0x4c,0x24,0x08};
        if(pin.rva==0xbf8a20 && pin.bytes>5 && bytes[0]==0xe9) std::memcpy(bytes,kRegistryHead,sizeof(kRegistryHead));
        std::uint8_t digest[32]{};mgscoop::link::Sha256Digest(bytes,pin.bytes,digest);
        constexpr char hex[]="0123456789abcdef";
        for(unsigned i=0;i<32;++i)
            if(hex[digest[i]>>4]!=pin.sha256[i*2] || hex[digest[i]&15]!=pin.sha256[i*2+1]) return false;
    }
    return true;
}
bool BuddyPoseCodeGate(void* base,std::size_t span) noexcept {
    return BuddyPinsMatch(base,span,mgscoop::buddy_pose::kPins) && BuddyPinsMatch(base,span,mgscoop::buddy_dog_commands::kPins);
}
bool SelectBuddyPoseReceipt(const char* buffer,std::size_t size,mgscoop::script_bridge::Replacement& replacement) noexcept {
    namespace b=mgscoop::buddy_pose;
    constexpr char prefix[]="return 'MGS_COOP_BUDDY_POSE_V1' -- ";
    if(!world_on || !buffer || size<=sizeof(prefix)-1 || size>160 || std::memcmp(buffer,prefix,sizeof(prefix)-1)!=0) return false;
    const char* cursor=buffer+sizeof(prefix)-1;const char* end=buffer+size;
    std::uint32_t v[4]{};
    for(unsigned i=0;i<4;++i) {
        const auto parsed=std::from_chars(cursor,end,v[i]);
        if(parsed.ec!=std::errc() || parsed.ptr==cursor) return false;
        cursor=parsed.ptr;if(i!=3) {if(cursor==end || *cursor!=' ') return false;++cursor;}
    }
    if(cursor!=end || (v[0]!=30010 && v[0]!=30020 && v[0]!=30050) || !v[1] || v[1]>65535 || v[2]!=9728 || v[3]>1) return false;
    const auto status=mgscoop::world_link::GetStatus();
    unsigned short mission{};pp::State player{};
    const bool player_read=pp::ReadState(partner_addresses,player)==pp::Refusal::none;
    if(buddy_pose_owner.P && (!player_read || player.P!=buddy_pose_owner.P)) buddy_pose_owner={};
    b::Scope scope{};scope.P=player_read?player.P:0;scope.mission=v[0];scope.epoch=static_cast<std::uint16_t>(v[1]);
    scope.thread=GetCurrentThreadId();scope.generation=worker_loading_generation;scope.refreshed_ms=GetTickCount64();
    scope.paired=status.configured && status.paired && status.link_up && status.effective>=3 && status.epoch==scope.epoch;
    // M2 (docs/vehicles-companions-design.md 6.2): this copy's own body and the mission, never body 1 (the dog
    // read touches no partner state), so a partner hidden in the heli or on a mount still lets both roles read.
    b::Field field{};field.player_read=player_read;field.runtime_thread=OnRuntimeThread();field.terminating=mission_terminating;
    field.mission_read=ReadMission(mission);field.mission=mission;field.requested=v[0];field.partner_mission=partner_mission;
    if(player_read){field.realized0=pp::Realized(player,0);field.id0=player.ids[0];field.parts0=player.parts[0];}
    scope.field_ready=b::FieldReady(field);
    // B.guard calls the registered DemoDaemon predicate on this same owner
    // before emitting a receipt. The existing native HUD bit adds a veto.
    scope.demo_playing=(mgscoop::debug_menu::LuaHud()&1u)!=0;
    scope.terminating=mission_terminating;scope.exact_executable=offline_enabled && partner_on && partner_gate_ok && lab::SavesRedirected();
    scope.exact_code_pins=buddy_pose_pins_ok;scope.cleanup_only=v[3]==1;
    const char* reason=nullptr;
    if(scope.cleanup_only && (!buddy_pose_owner.P || buddy_pose_owner.P!=scope.P || buddy_pose_owner.mission!=scope.mission ||
       buddy_pose_owner.epoch!=scope.epoch || buddy_pose_owner.generation!=scope.generation || buddy_pose_owner.thread!=scope.thread)) reason="scope";
    b::Pose pose{};
    if(!reason) reason=b::ReadPose({nullptr,UpperPhaseRead},reinterpret_cast<std::uintptr_t>(game_base),scope,
        GetCurrentThreadId(),GetTickCount64(),static_cast<std::uint16_t>(v[2]),pose);
    // Re-check transport and actor owner after the bounded read. A cleanup
    // receipt is permitted only for the cached, retired same-mission lease.
    const auto after=mgscoop::world_link::GetStatus();
    pp::State current{};
    if(!reason && (!OnRuntimeThread() || mission_terminating || !ReadMission(mission) || mission!=scope.mission ||
        pp::ReadState(partner_addresses,current)!=pp::Refusal::none || current.P!=scope.P ||
        worker_loading_generation!=scope.generation || (!scope.cleanup_only &&
        (!after.configured || !after.paired || !after.link_up || after.effective<3 || after.epoch!=scope.epoch)))) reason="scope";
    static char reply[256]{};
    HRESULT formatted{};
    if(reason) formatted=StringCchPrintfA(reply,ARRAYSIZE(reply),"return {ready=false,reason='%s'}",reason);
    else {
        if(!scope.cleanup_only) buddy_pose_owner=scope;
        formatted=StringCchPrintfA(reply,ARRAYSIZE(reply),"return {ready=true,mission=%lu,epoch=%u,id=9728,pos={%ld,%ld,%ld}}",
            static_cast<unsigned long>(scope.mission),unsigned(scope.epoch),
            static_cast<long>(std::floor(double(pose.position[0])*1000.0+0.5)),
            static_cast<long>(std::floor(double(pose.position[1])*1000.0+0.5)),
            static_cast<long>(std::floor(double(pose.position[2])*1000.0+0.5)));
    }
    if(FAILED(formatted)) return false;
    replacement={reply,std::strlen(reply)};world_quiet=true;return true;
}

// D7 dog guard receipt (docs/vehicles-companions-design.md 6.12, 8.9): mission, epoch, role (0 host, 1 guest; must be
// this copy's native role), arm (guest only: 1 the lease of dog_guard::kLeaseFrames dog updates with the host's byte,
// 2 a hold), the host's dog-state byte, a lab op (1 life %, 2 stun %; lab mode only) and its percent. dog_guard::Admit
// decides: arming needs the paired exact scope; through a link loss an armed lease is held drop-only (reason 'hold',
// follow 0, no lab op; review 2026-10-29); a guest's disarm and the host's read of its own record are always answered.
// Reply: the counters and, only from a snapshot under a second old (dog_guard::Fresh), the record 0 fields.
bool SelectDogGuardReceipt(const char* buffer,std::size_t size,mgscoop::script_bridge::Replacement& replacement) noexcept {
    namespace g=mgscoop::dog_guard;
    constexpr char prefix[]="return 'MGS_COOP_DOG_GUARD_V1' -- ";
    if(!world_on || !buffer || size<=sizeof(prefix)-1 || size>160 || std::memcmp(buffer,prefix,sizeof(prefix)-1)!=0) return false;
    const char* cursor=buffer+sizeof(prefix)-1;const char* end=buffer+size;
    std::uint32_t v[7]{};
    for(unsigned i=0;i<7;++i) {
        const auto parsed=std::from_chars(cursor,end,v[i]);
        if(parsed.ec!=std::errc() || parsed.ptr==cursor) return false;
        cursor=parsed.ptr;if(i!=6) {if(cursor==end || *cursor!=' ') return false;++cursor;}
    }
    if(cursor!=end || (v[0]!=30010 && v[0]!=30020 && v[0]!=30050) || !v[1] || v[1]>65535 || v[2]>1 || v[3]>2 || v[4]>255 ||
       v[5]>2 || v[6]>100 || (v[3] && !v[2])) return false;
    const auto& labc=mgscoop::GetLabConfig();
    const bool guest=labc.role==mgscoop::LabRole::guest;
    g::Scope sc{};
    sc.installed=dog_guard_ok && g::Installed();
    sc.labMode=labc.mode==mgscoop::LabMode::lab;
    sc.exact=offline_enabled && partner_on && partner_gate_ok && lab::SavesRedirected();
    if(sc.installed && sc.exact && (v[3] || v[5])) {
        const auto status=mgscoop::world_link::GetStatus();
        unsigned short mission{};
        sc.runtime=OnRuntimeThread();sc.terminating=mission_terminating;
        sc.mission=sc.runtime && !sc.terminating && ReadMission(mission) && mission==v[0] && partner_mission==v[0];
        sc.configured=status.configured;sc.paired=status.paired;sc.linkUp=status.link_up;sc.effective=status.effective;
        sc.epoch=status.epoch==v[1];sc.armed=g::Read().armed;
    }
    const auto a=g::Admit(guest,v[2]==1,static_cast<std::uint8_t>(v[3]),static_cast<std::uint8_t>(v[5]),sc);
    g::Request q{};q.guest=guest;q.arm=a.grant!=g::Grant::none;
    q.follow=a.grant==g::Grant::full?static_cast<std::uint8_t>(v[4]):std::uint8_t{0};
    if(a.lab) {q.labOp=static_cast<std::uint8_t>(v[5]);q.labPct=static_cast<std::uint8_t>(v[6]);}
    const auto s=dog_guard_ok?g::Control(q):g::Status{};
    const bool fresh=g::Fresh(s.snapshot,s.ageMs);
    const unsigned byte=fresh ? g::Encode(s.rec) : 0u;
    char record[128]{};
    if(fresh && FAILED(StringCchPrintfA(record,ARRAYSIZE(record),"life=%u,max=%u,stun=%u,smax=%u,state=%u,flags=%u,",unsigned(s.rec.life),
        unsigned(s.rec.max),unsigned(s.rec.stun),unsigned(s.rec.stunMax),unsigned(s.rec.state),unsigned(s.rec.flags)))) return false;
    const bool ready=!a.reason || a.grant==g::Grant::hold;
    static char reply[400]{};
    const HRESULT formatted=StringCchPrintfA(reply,ARRAYSIZE(reply),
        "return {ready=%s,reason='%s',armed=%u,installed=%u,snap=%u,fresh=%u,age=%lu,%sbyte=%u,"
        "drops=%u,hits=%u,applied=%u,kills=%u,cleared=%u,attacker=%u,attack=%u}",
        ready?"true":"false",a.reason?a.reason:"ok",s.armed?1u:0u,s.installed?1u:0u,s.snapshot?1u:0u,fresh?1u:0u,
        static_cast<unsigned long>(s.ageMs),record,byte,s.drops,s.hits,s.applied,s.kills,s.cleared,unsigned(s.lastAttacker),unsigned(s.lastAttack));
    if(FAILED(formatted)) return false;
    replacement={reply,std::strlen(reply)};world_quiet=true;return true;
}

// Private capacity receipt: all authority comes from this native owner, never
// a worker's requested pointer or Lua-supplied generation/count.
struct BuddyCapacityIdentity {
    std::uintptr_t P{},controller{},receiver{},records{};
    std::uint32_t mission{},generation{},cycle{};std::uint8_t active{};
};
BuddyCapacityIdentity buddy_capacity_identity{};
std::uint32_t buddy_capacity_serial=0;
void ClearBuddyCapacityIdentity() noexcept { buddy_capacity_identity={}; }
bool BuddySelection(std::uintptr_t& controller,std::uint8_t& active) noexcept {
    std::uintptr_t selector{};std::uint8_t pending{};
    const auto base=reinterpret_cast<std::uintptr_t>(game_base);
    return base && UpperPhaseRead(nullptr,base+0x2c28930,&selector,sizeof(selector)) && selector && selector<=UINTPTR_MAX-0x10 &&
        UpperPhaseRead(nullptr,selector+0x10,&controller,sizeof(controller)) && controller && controller<=UINTPTR_MAX-0x17f &&
        UpperPhaseRead(nullptr,controller+0x175,&active,sizeof(active)) && active<=4 &&
        UpperPhaseRead(nullptr,controller+0x17f,&pending,sizeof(pending)) && pending==0;
}
template<class Snap>
bool ObservedRideMounted(unsigned count,unsigned realized,unsigned mask,const char* pack,
    const mgscoop::buddy_ride_read::Snapshot& ride,const Snap& snap) noexcept {
    if(count!=2||realized!=2||mask!=3||!pack||!*pack||!std::strcmp(pack,"unknown")||!ride.current||ride.ride_id==0xffff) return false;
    int hit=-1;
    for(unsigned i=0;i<2;++i){
        const auto& actor=snap.actors[i];
        if(actor.game_id==ride.ride_id&&actor.occupied&&actor.rider!=0xffff) hit=static_cast<int>(i);
    }
    if(hit<0) return false;
    const auto& other=snap.actors[static_cast<unsigned>(1-hit)];
    return !(other.occupied&&other.rider==snap.actors[static_cast<unsigned>(hit)].rider);
}
long RideMilli(float value) noexcept {
    if(!std::isfinite(value)) return 0;
    const auto scaled=std::llround(static_cast<double>(value)*1000.0);
    if(scaled>2147483647LL || scaled<-2147483647LL) return 0;
    return static_cast<long>(scaled);
}
long RideHeading(float value) noexcept {
    const auto milli=RideMilli(value);
    return milli<-31416 || milli>31416 ? 0 : milli;
}
bool SelectRidePoseReceipt(const char* buffer,std::size_t size,mgscoop::script_bridge::Replacement& replacement) noexcept {
    namespace b=mgscoop::buddy_capacity_census;namespace w=mgscoop::buddy_capacity_walker;
    constexpr char prefix[]="return 'MGS_COOP_RIDE_POSE_V1' -- ";
    if(!buffer || size<=sizeof(prefix)-1 || size>160 || std::memcmp(buffer,prefix,sizeof(prefix)-1)!=0) return false;
    world_quiet=true;
    static char reply[768]{};
    const auto finish=[&](const char* text) noexcept {
        replacement={text,std::strlen(text)};return true;
    };
    const char* cursor=buffer+sizeof(prefix)-1;const char* end=buffer+size;std::uint32_t v[3]{};
    bool parsedOk=true;
    for(unsigned i=0;i<3 && parsedOk;++i){
        const auto parsed=std::from_chars(cursor,end,v[i]);
        if(parsed.ec!=std::errc() || parsed.ptr==cursor) parsedOk=false;
        else {cursor=parsed.ptr;if(i!=2){if(cursor==end || *cursor!=' ') parsedOk=false; else ++cursor;}}
    }
    if(!parsedOk) return finish("return {ready=false,reason='request'}");
    if(cursor!=end || (v[0]!=30010 && v[0]!=30020 && v[0]!=30050) || !v[1] || v[1]>65535 || (v[2]!=1 && v[2]!=2))
        return finish("return {ready=false,reason='request'}");
    const auto base=game_base;const auto span=game_span;
    const bool pins=base && span && partner_gate_ok && (v[2]==1
        ? BuddyPinsMatch(base,span,b::kPins) : BuddyPinsMatch(base,span,w::kPins));
    if(!pins) return finish("return {ready=false,reason='pins'}");
    const auto status=mgscoop::world_link::GetStatus();unsigned short mission{};pp::State player{};
    std::uintptr_t controller{};std::uint8_t active{};
    const bool field=OnRuntimeThread() && !mission_terminating &&
        ReadMission(mission) && mission==v[0] && partner_mission==v[0] &&
        status.configured && status.paired && status.link_up && status.effective>=3 && status.epoch==v[1] &&
        pp::ReadState(partner_addresses,player)==pp::Refusal::none && player.local==0 && !player.sync &&
        pp::Realized(player,0) && pp::Realized(player,1) && player.ids[0]==0 && player.ids[1]==1 &&
        player.parts[0]==3 && player.parts[1]==3 && BuddySelection(controller,active);
    if(!field || (v[2]==1 && active!=1) || (v[2]==2 && active!=4))
        return finish("return {ready=false,reason='scope'}");
    const auto now=GetTickCount64();const auto thread=GetCurrentThreadId();
    const auto generation=worker_loading_generation?worker_loading_generation:1u;
    const b::Authorization auth{true,true,true,true,true,true};
    b::Census horse{};w::Census walker{};const char* reason=nullptr;
    if(v[2]==1){
        b::Owner owner{player.P,mission,generation,thread,1,status.epoch,now,true,true,false,false};
        reason=b::ReadPlayPose({nullptr,UpperPhaseRead},reinterpret_cast<std::uintptr_t>(base),auth,owner,thread,now,horse);
    }else{
        w::Owner owner{player.P,mission,generation,thread,1,status.epoch,now,true,true,false,false};
        reason=w::ReadPlayPose({nullptr,UpperPhaseRead},reinterpret_cast<std::uintptr_t>(base),auth,owner,thread,now,walker);
    }
    if(reason){
        if(SUCCEEDED(StringCchPrintfA(reply,ARRAYSIZE(reply),"return {ready=false,reason='%s'}",reason)))
            replacement={reply,std::strlen(reply)};
        else replacement={"return {ready=false,reason='read'}",sizeof("return {ready=false,reason='read'}")-1};
        return true;
    }
    char actors[512]{};const unsigned count=v[2]==1?horse.count:walker.count;
    for(unsigned i=0;i<count && i<2;++i){
        char one[240]{};
        if(v[2]==1){const auto& a=horse.actors[i];StringCchPrintfA(one,ARRAYSIZE(one),
            "%s{id=%u,rider=%u,occupied=%s,realized=%s,x=%ld,y=%ld,z=%ld,yaw=%ld}",i?",":"",
            unsigned(a.game_id),unsigned(a.rider),a.occupied?"true":"false",a.realized?"true":"false",
            RideMilli(a.position[0]),RideMilli(a.position[1]),RideMilli(a.position[2]),RideHeading(a.position[3]));}
        else{const auto& a=walker.actors[i];StringCchPrintfA(one,ARRAYSIZE(one),
            "%s{id=%u,rider=%u,occupied=%s,realized=%s,x=%ld,y=%ld,z=%ld,yaw=%ld}",i?",":"",
            unsigned(a.game_id),unsigned(a.rider),a.occupied?"true":"false",a.realized?"true":"false",
            RideMilli(a.position[0]),RideMilli(a.position[1]),RideMilli(a.position[2]),RideHeading(a.position[3]));}
        if(FAILED(StringCchCatA(actors,ARRAYSIZE(actors),one))) return finish("return {ready=false,reason='read'}");
    }
    if(FAILED(StringCchPrintfA(reply,ARRAYSIZE(reply),
        "return {ready=true,kind=%lu,actors={%s}}",static_cast<unsigned long>(v[2]),actors)))
        return finish("return {ready=false,reason='read'}");
    replacement={reply,std::strlen(reply)};
    return true;
}
// RIDE_TILT (WS3 A1, docs/vehicle-ws3.md; partner_ride_tilt, default on in lab and play):
//   return 'MGS_COOP_RIDE_TILT_V1' -- read <id>             -> return <a44_mrad>,<a48_mrad> | return nil
//   return 'MGS_COOP_RIDE_TILT_V1' -- write <id> <a44> <a48> -> return true | return false
// read: the driver's own vehicle record angles (its veh=), only while the
// peer's ride addendum advertises RIDE_TILT. write: this copy's record +0x44 /
// +0x48 just before Lua's SetPosition, after partner_vehicle::TiltRefusal on
// two equal reads (identity body == record, realized, not the viewer's own
// vehicle, |angle| < pi/2) and the contract blocks re-verified.
struct RideTiltLog { unsigned reads, writes, refusals; const char* last; std::uint32_t mission; } ride_tilt_log{};
void RideTiltRefused(const char* verb, unsigned id, const char* why) noexcept {
    ++ride_tilt_log.refusals;
    if (ride_tilt_log.last == why) return;
    ride_tilt_log.last = why;
    LogFormat("partner_ride_tilt=refused; verb=%s; id=%u; reason=%s; n=%u\r\n", verb, id, why, ride_tilt_log.refusals);
}
bool SelectRideTiltReceipt(const char* buffer, std::size_t size, mgscoop::script_bridge::Replacement& replacement) noexcept {
    namespace v = mgscoop::partner_vehicle;
    constexpr char prefix[] = "return 'MGS_COOP_RIDE_TILT_V1' -- ";
    if (!buffer || size <= sizeof(prefix) - 1 || size > 96 || std::memcmp(buffer, prefix, sizeof(prefix) - 1) != 0) return false;
    world_quiet = true;
    const auto finish = [&](const char* text) noexcept { replacement = {text, std::strlen(text)}; return true; };
    const char* cursor = buffer + sizeof(prefix) - 1;
    const char* end = buffer + size;
    bool write = false;
    if (end - cursor > 5 && std::memcmp(cursor, "read ", 5) == 0) cursor += 5;
    else if (end - cursor > 6 && std::memcmp(cursor, "write ", 6) == 0) { cursor += 6; write = true; }
    else return finish("return nil");
    std::int32_t values[3]{};
    const unsigned count = write ? 3u : 1u;
    for (unsigned i = 0; i < count; ++i) {
        const auto parsed = std::from_chars(cursor, end, values[i]);
        if (parsed.ec != std::errc() || parsed.ptr == cursor) return finish(write ? "return false" : "return nil");
        cursor = parsed.ptr;
        if (i + 1 < count) { if (cursor == end || *cursor != ' ') return finish(write ? "return false" : "return nil"); ++cursor; }
    }
    if (cursor != end || values[0] < 0 || values[0] > 65534) return finish(write ? "return false" : "return nil");
    const auto id = static_cast<std::uint16_t>(values[0]);
    if (ride_tilt_log.mission != partner_mission) ride_tilt_log = RideTiltLog{0, 0, 0, nullptr, partner_mission};
    const auto& config = mgscoop::GetLabConfig();
    // Play admits it too (the key resolves on in both; only the viewer's own copy is written).
    const bool enabled = config.partner_ride_tilt && partner_gate_ok && game_base;
    const bool runtime = OnRuntimeThread() && !mission_terminating;
    const bool peer = pd::RideTiltPeer(partner_driver, PartnerLinkSession(nullptr), mgscoop::partner_net::NowMs());
    const std::uint16_t local = pd::LocalVehicle(partner_driver);
    static char reply[64]{};
    if (!write) {
        v::Read r{};
        if (enabled && runtime && peer && id == local) v::ReadLive(game_base, id, r);
        v::TiltReadIn in{};
        in.enabled = enabled; in.runtime = runtime; in.peer = peer; in.id = id; in.local = local; in.read = &r;
        std::int32_t a = 0, b = 0;
        if (const char* why = v::TiltReadRefusal(in, a, b)) {
            if (enabled) RideTiltRefused("read", id, why);
            // The key is fixed for the process: Lua stops asking after an 'off'.
            return finish(config.partner_ride_tilt ? "return nil" : "return nil,'off'");
        }
        if (++ride_tilt_log.reads <= 4 || ride_tilt_log.reads % 600 == 0)
            LogFormat("partner_ride_tilt=read; id=%u; a44_mrad=%d; a48_mrad=%d; n=%u\r\n", unsigned(id), a, b, ride_tilt_log.reads);
        if (FAILED(StringCchPrintfA(reply, ARRAYSIZE(reply), "return %d,%d", a, b))) return finish("return nil");
        return finish(reply);
    }
    const bool pins = enabled && !v::VerifyImage(game_base, game_span);
    v::Read first{}, second{};
    if (enabled && runtime && pins && peer && id != local) {
        v::ReadLive(game_base, id, first);
        v::ReadLive(game_base, id, second);
    }
    v::TiltIn in{};
    in.enabled = enabled; in.pins = pins; in.runtime = runtime; in.peer = peer; in.id = id; in.local = local;
    in.a44_mrad = values[1]; in.a48_mrad = values[2]; in.first = &first; in.second = &second;
    const char* why = v::TiltRefusal(in);
    if (!why) why = v::WriteTilt(first, values[1], values[2]);
    if (why) {
        if (enabled) RideTiltRefused("write", id, why);
        return finish("return false");
    }
    ride_tilt_log.last = nullptr;
    if (++ride_tilt_log.writes <= 8 || ride_tilt_log.writes % 600 == 0)
        LogFormat("partner_ride_tilt=write; id=%u; a44_mrad=%d; a48_mrad=%d; before=%d,%d; n=%u\r\n", unsigned(id), values[1], values[2],
                  v::Mrad(first.unit.a44), v::Mrad(first.unit.a48), ride_tilt_log.writes);
    return finish("return true");
}
// G4 truck test levers (docs/native-puppet.md "Truck test levers"; lab.ini debug_vehicle_door=1, lab only):
//   return 'MGS_COOP_VEHICLE_DOOR_V1' -- <id|partner> <seat 0|1> <out_cm>
// answers this copy's stock door spot of one vehicle (mgscoop/vehicle_door.hpp) for the lua/coop/debug_vehicle.lua
// goto levers. Off unless named; refused outside mode=lab with offline and redirected saves (the give_item rule),
// off the runtime thread and while the mission terminates. "partner" takes the id from the partner's live ride
// packet. A fault or a digest change in the read-only native calls latches the mission (reason=uncertain).
struct VehicleDoorState { std::uint32_t mission; const char* uncertain; int pins; } vehicle_door_state{0, nullptr, -1};
bool SelectVehicleDoorReceipt(const char* buffer, std::size_t size, mgscoop::script_bridge::Replacement& replacement) noexcept {
    namespace vd = mgscoop::vehicle_door;
    constexpr std::size_t prefix = sizeof(vd::kPrefix) - 1;
    if (!buffer || size <= prefix || size > 96 || std::memcmp(buffer, vd::kPrefix, prefix) != 0) return false;
    world_quiet = true;
    static char reply[400]{};
    const auto finish = [&](const char* text) noexcept { replacement = {text, std::strlen(text)}; return true; };
    vd::Request request{};
    if (!vd::ParseRequest(buffer + prefix, size - prefix, request)) return finish("return {ok=false,reason='request'}");
    if (vehicle_door_state.mission != partner_mission) {
        vehicle_door_state.mission = partner_mission;
        vehicle_door_state.uncertain = nullptr;
    }
    const auto& config = mgscoop::GetLabConfig();
    unsigned short mission = 0;
    vd::GateIn gate{};
    gate.enabled = config.debug_vehicle_door;
    gate.lab = config.mode == mgscoop::LabMode::lab && offline_enabled && lab::SavesRedirected();
    gate.runtime = OnRuntimeThread() && !mission_terminating && ReadMission(mission) && mission == partner_mission;
    // The door blocks are checked once per process, only for an admitted lab request.
    if (gate.enabled && gate.lab && gate.runtime && vehicle_door_state.pins < 0) {
        const char* why = !game_base || !partner_gate_ok ? "gate" : vd::VerifyImage(game_base, game_span);
        vehicle_door_state.pins = why ? 0 : 1;
        LogFormat("partner_vehicle_door=pins; result=%s; reason=%s\r\n", why ? "refused" : "ok", why ? why : "none");
    }
    gate.pins = vehicle_door_state.pins == 1 && game_base && partner_gate_ok;
    gate.uncertain = vehicle_door_state.uncertain != nullptr;
    vd::Answer answer{};
    vd::Facts facts{};
    vd::Spot spot{};
    answer.refusal = vd::GateRefusal(gate);
    std::uint16_t id = request.id;
    if (!answer.refusal && request.target == vd::Target::partner) {
        std::uint16_t vehicle = vd::kNone;
        std::uint8_t seat = vd::kSeatNone;
        const bool live = pd::RidePeerVehicle(partner_driver, PartnerLinkSession(nullptr), mgscoop::partner_net::NowMs(), vehicle, seat);
        answer.refusal = vd::PartnerTarget(live, vehicle, id);
        answer.peer = true;
        answer.peer_seat = seat;
        request.id = id;
    }
    if (!answer.refusal) {
        pp::State player{};
        const bool state = pp::ReadState(partner_addresses, player) == pp::Refusal::none;
        answer.refusal = vd::ReadFacts(game_base, id, state ? player.P : 0, state ? player.Q : 0, state ? player.local : 0, facts);
        if (answer.refusal && (std::strcmp(answer.refusal, "fault") == 0 || std::strcmp(answer.refusal, "changed") == 0)) {
            vehicle_door_state.uncertain = answer.refusal;
            LogFormat("partner_vehicle_door=uncertain; id=%u; reason=%s; latch=mission\r\n", unsigned(id), answer.refusal);
        }
    }
    if (!answer.refusal) {
        spot = vd::DoorSpot(facts, request.seat, request.out_cm);
        answer.refusal = spot.refusal;
        answer.facts = &facts;
        answer.spot = &spot;
    }
    // One line per answer while the key is named (a key-off refusal stays quiet).
    char line[512]{};
    if (gate.enabled && vd::FormatLog(request, answer, line, sizeof(line))) Log(line);
    if (!vd::FormatAnswer(answer, reply, sizeof(reply))) return finish("return {ok=false,reason='format'}");
    return finish(reply);
}
// M6 seat races (lua/coop/world_ride_pose.lua SEAT op 54; lab.ini partner_ride_race=1; partner_board "seat races"):
//   return 'MGS_COOP_BOARD_SEAT_V1' -- poll | clear | deny <vehicle> <ms> | grant <vehicle> <ms> | hold <ms>
//     (partner_board::ParseSeatRequest: vehicle 25600..26111, deny / grant ms 1..5000, hold ms 0..5000; review R8)
//   -> return {ok=true,on=0}                                   (the key off or site C not installed)
//   -> return {ok=true,on=1,want=<id|65535>,age=<ms|-1>,deny=<id|65535>,left=<ms>,hits=<n>,hold=<0|1>,
//              grant=<id|65535>,gleft=<ms>,holds=<n>,ref=<id|65535>,rage=<ms|-1>}
//      (ref / rage: the Fulton guard's vehicle and its age: a refused ride icon or the peer's vehicle; seats on)
//   -> return {ok=false,reason='request'}
// Atomics only: site C writes the want and reads the deny / grant / hold, site B reads them.
bool SelectBoardSeatReceipt(const char* buffer, std::size_t size, mgscoop::script_bridge::Replacement& replacement) noexcept {
    static constexpr char kPrefix[] = "return 'MGS_COOP_BOARD_SEAT_V1' -- ";
    constexpr std::size_t prefix = sizeof(kPrefix) - 1;
    if (!buffer || size <= prefix || size > 96 || std::memcmp(buffer, kPrefix, prefix) != 0) return false;
    world_quiet = true;
    static char reply[200]{};
    const auto finish = [&](const char* text) noexcept { replacement = {text, std::strlen(text)}; return true; };
    namespace pb = mgscoop::partner_board;
    const pb::SeatRequest request = pb::ParseSeatRequest(buffer + prefix, size - prefix);
    if (!pb::LiveRace()) return finish("return {ok=true,on=0}");
    switch (request.cmd) {
    case pb::SeatCmd::poll: break;
    case pb::SeatCmd::clear: pb::RaceDeny(pb::kVehicleUnknown, 0); pb::RaceGrant(pb::kVehicleUnknown, 0); pb::RaceHold(0); break;
    case pb::SeatCmd::deny: pb::RaceDeny(request.vehicle, request.ms); break;
    case pb::SeatCmd::grant: pb::RaceGrant(request.vehicle, request.ms); break;
    case pb::SeatCmd::hold: pb::RaceHold(request.ms); break;
    default: return finish("return {ok=false,reason='request'}");
    }
    pb::RaceState rs{};
    pb::ReadRace(rs);
    const long long age = rs.want == pb::kVehicleUnknown || rs.want_age_ms > 0x7FFFFFFFu ? -1LL : static_cast<long long>(rs.want_age_ms);
    const long long rage = rs.guard == pb::kVehicleUnknown || rs.guard_age_ms > 0x7FFFFFFFu ? -1LL : static_cast<long long>(rs.guard_age_ms);
    if (FAILED(StringCchPrintfA(reply, ARRAYSIZE(reply),
                                "return {ok=true,on=1,want=%u,age=%lld,deny=%u,left=%u,hits=%u,hold=%u,grant=%u,gleft=%u,holds=%u,ref=%u,rage=%lld}",
                                unsigned(rs.want), age, unsigned(rs.deny), rs.deny_left_ms, rs.hits, rs.hold ? 1u : 0u, unsigned(rs.grant),
                                rs.grant_left_ms, rs.holds, unsigned(rs.guard), rage)))
        return finish("return {ok=false,reason='format'}");
    return finish(reply);
}
// M4 VEHICLE_STATE (lua/coop/world_vehicle_state.lua, docs/vehicles-companions-design.md 6.4; lab.ini
// world_vehicle_state=1, lab and play): read-only body life and state words of this copy's TppVehicle2 pool.
//   return 'MGS_COOP_VEHICLE_LIFE_V1' -- <first id 25600..26111> <n 1..32>
//   -> return {c=<pool count>,[id]={life,zero,parts,state},...}   (ids in the pool only)
//   -> return {refused='<reason>'}   (key off, gate, runtime, mission, request, or the first id's chain refusal)
// life is the block's word 0 (SetBodyLife), zero the bits 1..11 of the words that read 0, parts the words
// above 0, state the engine's own ene_veh_state word (partner_vehicle::SvState). Every read goes through
// ReadProcessMemory (partner_vehicle::ReadLive / ReadLifeLive): it never faults and never writes.
struct VehicleLifeLog { std::uint32_t mission; const char* last; unsigned n; } vehicle_life_log{0, nullptr, 0};
bool SelectVehicleLifeReceipt(const char* buffer, std::size_t size, mgscoop::script_bridge::Replacement& replacement) noexcept {
    namespace v = mgscoop::partner_vehicle;
    constexpr char prefix[] = "return 'MGS_COOP_VEHICLE_LIFE_V1' -- ";
    if (!buffer || size <= sizeof(prefix) - 1 || size > 96 || std::memcmp(buffer, prefix, sizeof(prefix) - 1) != 0) return false;
    world_quiet = true;
    static char reply[2048]{};
    const auto finish = [&](const char* text) noexcept { replacement = {text, std::strlen(text)}; return true; };
    if (vehicle_life_log.mission != partner_mission) vehicle_life_log = VehicleLifeLog{partner_mission, nullptr, 0};
    const auto refuse = [&](const char* why) noexcept {
        if (vehicle_life_log.last != why) {
            vehicle_life_log.last = why;
            LogFormat("vehicle_life=refused; reason=%s\r\n", why);
        }
        if (FAILED(StringCchPrintfA(reply, ARRAYSIZE(reply), "return {refused='%s'}", why))) return finish("return {refused='format'}");
        return finish(reply);
    };
    const char* cursor = buffer + sizeof(prefix) - 1;
    const char* end = buffer + size;
    unsigned first = 0, n = 0;
    auto parsed = std::from_chars(cursor, end, first);
    if (parsed.ec != std::errc() || parsed.ptr == cursor || parsed.ptr == end || *parsed.ptr != ' ') return refuse("request");
    cursor = parsed.ptr + 1;
    parsed = std::from_chars(cursor, end, n);
    if (parsed.ec != std::errc() || parsed.ptr != end || first < 25600 || first > 26111 || n < 1 || n > 32) return refuse("request");
    const auto& config = mgscoop::GetLabConfig();
    if (!config.world_vehicle_state) return refuse("off");
    if (!game_base || !partner_gate_ok) return refuse("gate");
    unsigned short mission = 0;
    if (!OnRuntimeThread() || mission_terminating || !ReadMission(mission) || mission != partner_mission) return refuse("runtime");
    if (mission != 30010 && mission != 30020 && mission != 30050) return refuse("mission");
    std::size_t used = 0;
    const auto put = [&](const char* format, auto... args) noexcept {
        if (used >= sizeof(reply)) return false;
        const int w = std::snprintf(reply + used, sizeof(reply) - used, format, args...);
        if (w < 0 || static_cast<std::size_t>(w) >= sizeof(reply) - used) return false;
        used += static_cast<std::size_t>(w);
        return true;
    };
    unsigned count = 0, listed = 0;
    const char* firstRefusal = nullptr;
    bool ok = put("return {");
    for (unsigned id = first; ok && id < first + n && id <= 26111; ++id) {
        v::Read r{};
        v::ReadLive(game_base, static_cast<std::uint16_t>(id), r);
        if (!count && r.unit.count) count = r.unit.count;
        if (r.refusal) {
            // "live" / "index": no vehicle in this slot (or past the pool); anything else names the chain.
            if (!firstRefusal && std::strcmp(r.refusal, "live") != 0 && std::strcmp(r.refusal, "index") != 0) firstRefusal = r.refusal;
            continue;
        }
        v::Life life{};
        v::ReadLifeLive(r, life);
        if (life.refusal) {
            if (!firstRefusal) firstRefusal = life.refusal;
            continue;
        }
        ok = put("[%u]={%u,%u,%u,%u},", id, unsigned(life.words[0]), unsigned(life.zero), unsigned(life.parts), unsigned(life.state));
        ++listed;
    }
    if (!listed && firstRefusal) return refuse(firstRefusal);
    if (!ok || !put("c=%u}", count)) return refuse("format");
    vehicle_life_log.last = nullptr;
    if (++vehicle_life_log.n <= 2 || vehicle_life_log.n % 2000 == 0)
        LogFormat("vehicle_life=read; first=%u; n=%u; listed=%u; count=%u; reads=%u\r\n", first, n, listed, count, vehicle_life_log.n);
    return finish(reply);
}
bool SelectBuddyCapacityReceipt(const char* buffer,std::size_t size,mgscoop::script_bridge::Replacement& replacement) noexcept {
    namespace b=mgscoop::buddy_capacity_census;namespace w=mgscoop::buddy_capacity_walker;
    constexpr char prefix[]="return 'MGS_COOP_BUDDY_CAPACITY_V1' -- ";
    if(!world_on || !buffer || size<=sizeof(prefix)-1 || size>160 || std::memcmp(buffer,prefix,sizeof(prefix)-1)!=0) return false;
    const char* cursor=buffer+sizeof(prefix)-1;const char* end=buffer+size;std::uint32_t v[3]{};
    for(unsigned i=0;i<3;++i) {
        const auto parsed=std::from_chars(cursor,end,v[i]);
        if(parsed.ec!=std::errc() || parsed.ptr==cursor) return false;
        cursor=parsed.ptr;if(i!=2){if(cursor==end || *cursor!=' ')return false;++cursor;}
    }
    if(cursor!=end || (v[0]!=30010 && v[0]!=30020 && v[0]!=30050) || !v[1] || v[1]>65535 || v[2]>2) return false;
    static constexpr char no[]="return {ready=false,admitted=false,reason='admission'}";
    replacement={no,sizeof(no)-1};world_quiet=true;
    // Default-off must return before any player/selection/registry read.
    if(!b::Compiled() || !w::Compiled() || !mgscoop::worker_scenario::ThreadAdmitted() ||
        !MountedBuddyCapacity() || !buddy_capacity_pins_ok || !offline_enabled || !lab::SavesRedirected() ||
        !partner_on || !partner_gate_ok || !archive_mounted) return true;
    const auto status=mgscoop::world_link::GetStatus();unsigned short mission{};pp::State player{};
    std::uintptr_t controller{};std::uint8_t active{};
    const bool field=OnRuntimeThread() && !mission_terminating && !gameplay_restore_pending &&
        (mgscoop::debug_menu::LuaHud()&1u)==0 && ReadMission(mission) && mission==v[0] && partner_mission==v[0] &&
        worker_loading_generation && status.configured && status.paired && status.link_up && status.effective>=3 && status.epoch==v[1] &&
        pp::ReadState(partner_addresses,player)==pp::Refusal::none && player.local==0 && !player.sync &&
        pp::Realized(player,0) && pp::Realized(player,1) && player.ids[0]==0 && player.ids[1]==1 && player.parts[0]==3 && player.parts[1]==3 &&
        BuddySelection(controller,active);
    if(!field){
        static unsigned fieldRefusals=0;
        if(fieldRefusals++<32)LogFormat("buddy_capacity_refused=field; kind=%lu mission=%u expected=%lu active=%u generation=%lu epoch=%u paired=%u\r\n",
            static_cast<unsigned long>(v[2]),unsigned(mission),static_cast<unsigned long>(v[0]),unsigned(active),
            static_cast<unsigned long>(worker_loading_generation),unsigned(status.epoch),unsigned(status.paired));
        buddy_capacity_identity={};return true;
    }
    auto& identity=buddy_capacity_identity;
    if(identity.P!=player.P || identity.mission!=mission || identity.generation!=worker_loading_generation ||
        identity.controller!=controller || identity.active!=active) {
        if(buddy_capacity_serial==UINT32_MAX)return true;
        identity={player.P,controller,0,0,mission,worker_loading_generation,++buddy_capacity_serial,active};
    }
    b::Authorization authorization{true,true,true,true,true,true};
    const auto now=GetTickCount64();const auto thread=GetCurrentThreadId();
    const char* reason=nullptr;b::Census horse{};w::Census walker{};
    if(v[2]==1) {
        b::Owner owner{player.P,mission,worker_loading_generation,thread,identity.cycle,status.epoch,now,true,active==1,false,false};
        reason=b::ReadCurrentCensus({nullptr,UpperPhaseRead},reinterpret_cast<std::uintptr_t>(game_base),authorization,owner,thread,GetTickCount64(),horse);
    }else if(v[2]==2) {
        w::Owner owner{player.P,mission,worker_loading_generation,thread,identity.cycle,status.epoch,now,true,active==4,false,false};
        reason=w::ReadCurrentCensus({nullptr,UpperPhaseRead},reinterpret_cast<std::uintptr_t>(game_base),authorization,owner,thread,GetTickCount64(),walker);
    }
    if(!reason && v[2]) {
        const auto receiver=v[2]==1?horse.receiver:walker.receiver;const auto records=v[2]==1?horse.records:walker.records;
        if(identity.receiver!=receiver || identity.records!=records) {
            if(buddy_capacity_serial==UINT32_MAX)return true;
            identity.receiver=receiver;identity.records=records;identity.cycle=++buddy_capacity_serial;
        }
    }
    mgscoop::buddy_ride_read::Snapshot ride{};const char* rideReason="disabled";
    if(!reason && v[2] && buddy_ride_pins_ok){
        mgscoop::buddy_ride_read::Owner rideOwner{player.P,mission,worker_loading_generation,thread,identity.cycle,status.epoch,GetTickCount64(),active,true,false,false};
        rideReason=mgscoop::buddy_ride_read::ReadCurrent({nullptr,UpperPhaseRead},reinterpret_cast<std::uintptr_t>(game_base),authorization,rideOwner,thread,GetTickCount64(),ride);
    }
    const auto after=mgscoop::world_link::GetStatus();pp::State current{};std::uintptr_t againController{};std::uint8_t againActive{};
    if(!reason && (!mgscoop::worker_scenario::ThreadAdmitted() || !MountedBuddyCapacity() || !OnRuntimeThread() || mission_terminating ||
        gameplay_restore_pending || (mgscoop::debug_menu::LuaHud()&1u) || !ReadMission(mission) || mission!=v[0] || partner_mission!=mission ||
        worker_loading_generation!=identity.generation || pp::ReadState(partner_addresses,current)!=pp::Refusal::none || current.P!=identity.P || current.Q!=player.Q || current.ids[0]!=0 || current.ids[1]!=1 ||
        current.local!=0 || current.sync || !pp::Realized(current,0) || !pp::Realized(current,1) || current.parts[0]!=3 || current.parts[1]!=3 ||
        !after.configured || !after.paired || !after.link_up || after.effective<3 || after.epoch!=v[1] ||
        !BuddySelection(againController,againActive) || againController!=controller || againActive!=active)) reason="scope_changed";
    static char reply[768]{};HRESULT formatted{};
    if(reason){
        static unsigned censusRefusals=0;
        std::uint64_t packHash{};
        const bool packReason=std::strcmp(reason,"pack_unknown")==0||std::strcmp(reason,"pack_changed")==0;
        if(packReason) UpperPhaseRead(nullptr,controller+0x38,&packHash,sizeof(packHash));
        if(censusRefusals++<64){
            if(packReason) LogFormat("buddy_capacity_refused=census;kind=%lu reason=%s;hash=0x%llx\r\n",static_cast<unsigned long>(v[2]),reason,static_cast<unsigned long long>(packHash));
            else LogFormat("buddy_capacity_refused=census;kind=%lu reason=%s\r\n",static_cast<unsigned long>(v[2]),reason);
        }
        formatted=StringCchPrintfA(reply,ARRAYSIZE(reply),"return {ready=false,admitted=false,reason='%s'}",reason);
    }
    else {
        char actors[384]{};unsigned count=0,realized=0,mask=0;
        if(v[2]==1){count=horse.count;realized=horse.realized_count;mask=horse.realized_mask;}
        if(v[2]==2){count=walker.count;realized=walker.realized_count;mask=walker.realized_mask;}
        for(unsigned i=0;i<count;++i){char one[192]{};
            if(v[2]==1){const auto& a=horse.actors[i];StringCchPrintfA(one,ARRAYSIZE(one),
                "%s{id=%u,rider=%u,pending=%u,occupied=%s,realized=%s}",i?",":"",unsigned(a.game_id),unsigned(a.rider),unsigned(a.pending_rider),a.occupied?"true":"false",a.realized?"true":"false");}
            else{const auto& a=walker.actors[i];StringCchPrintfA(one,ARRAYSIZE(one),
                "%s{id=%u,rider=%u,state=%u,occupied=%s,realized=%s}",i?",":"",unsigned(a.game_id),unsigned(a.rider),unsigned(a.ride_state),a.occupied?"true":"false",a.realized?"true":"false");}
            if(FAILED(StringCchCatA(actors,ARRAYSIZE(actors),one)))return true;
        }
        const char* packToken=v[2]==1?horse.pack_token:v[2]==2?walker.pack_token:"";
        if(!packToken) packToken="";
        const bool rideMounted=v[2]==1?ObservedRideMounted(count,realized,mask,packToken,ride,horse):
            v[2]==2&&ObservedRideMounted(count,realized,mask,packToken,ride,walker);
        unsigned remoteRide=255;
        const auto rideNow=mgscoop::partner_net::NowMs();
        const auto rideEpoch=partner_hooks.link_session?partner_hooks.link_session(partner_hooks.context):0;
        if(mgscoop::partner_ride_wire::Live(partner_driver.ride_rx,rideEpoch,rideNow)) remoteRide=static_cast<unsigned>(partner_driver.ride_rx.packet.kind);
        formatted=StringCchPrintfA(reply,ARRAYSIZE(reply),
            "return {ready=true,admitted=true,profile='buddy_capacity_two_v1',commands=true,mission=%lu,epoch=%u,kind=%lu,generation=%lu,cycle=%lu,allocated=%u,realized=%u,mask=%u,actors={%s},pack='%s',rideReady=%s,rideId=%u,rideReason='%s',rideMounted=%s,remoteRide=%u}",
            static_cast<unsigned long>(mission),unsigned(v[1]),static_cast<unsigned long>(v[2]),static_cast<unsigned long>(identity.generation),
            static_cast<unsigned long>(identity.cycle),count,realized,mask,actors,packToken,ride.current?"true":"false",unsigned(ride.ride_id),rideReason?rideReason:"ok",rideMounted?"true":"false",remoteRide);
    }
    if(SUCCEEDED(formatted)){replacement={reply,std::strlen(reply)};}
    return true;
}

bool DownedAdmitted(std::uint32_t mission, std::uint16_t epoch) noexcept {
    unsigned short current=0;
    const auto status=mgscoop::world_link::GetStatus();
    return ReadMission(current) && OnRuntimeThread() && !mission_terminating && offline_enabled &&
        partner_on && partner_gate_ok && lab::SavesRedirected() && partner_mission==mission && current==mission &&
        status.configured && status.paired && status.link_up && status.effective>=3 && status.epoch==epoch;
}
bool DownedReply(const mgscoop::downed_player::Reply& r, mgscoop::script_bridge::Replacement& replacement) noexcept {
    static char reply[512]{};
    if(FAILED(StringCchPrintfA(reply,ARRAYSIZE(reply),
        "return {ready=%s,down=%s,held=%s,applied=%s,generation=%lu,cause=%u,pos={%ld,%ld,%ld},hitSequence=%lu,hitDamageMilli=%lu,hitCause=%u,reviveMaskObserved=%s,reviveMaskPresent=%s,nativeSleepActive=%s,nativeSleepReleaseReady=%s,carry=%u,carryHeld=%s}",
        r.ready?"true":"false",r.down?"true":"false",r.held?"true":"false",r.applied?"true":"false",
        static_cast<unsigned long>(r.generation),unsigned(r.cause),
        static_cast<long>(r.pos[0]),static_cast<long>(r.pos[1]),static_cast<long>(r.pos[2]),
        static_cast<unsigned long>(r.hitSequence),static_cast<unsigned long>(r.hitDamageMilli),unsigned(r.hitCause),
        r.reviveMaskObserved?"true":"false",r.reviveMaskPresent?"true":"false",r.nativeSleepActive?"true":"false",r.nativeSleepReleaseReady?"true":"false",
        mgscoop::coop_carry::HudRole(),r.carryHeld?"true":"false"))) return false;
    replacement={reply,std::strlen(reply)};world_quiet=true;return true;
}
// --- Teammate rescue state (docs/STATUS.md) -----------------------------
// lua/coop/world_downed.lua publishes both players' accepted downed rows every
// tick: "return 'MGS_COOP_TEAM_V1' -- mission epoch localDown localCause
// peerDown peerCause peerFresh". The per-frame policy below keeps the
// teammate's body on this machine in the stock player sleep while they are
// darted and tells the carry adapter which bodies may be picked up.
struct TeamState {
    bool valid=false;
    mgscoop::team_wake::Receipt receipt{};
    std::uint64_t at=0;
};
TeamState team_state{};
bool coop_rescue_configured=false;
bool coop_rescue_pending=false;
bool coop_partner_sleep_held=false;
// Horse cargo (lua/coop/world_horse_cargo.lua): "<hold> <putdown>". hold=1
// keeps the partner driver off instance 1 (Lua places it on the horse);
// putdown=1 asks the stock put-down of the local carry. Reply: carry role.
bool SelectCargoReceipt(const char* buffer,std::size_t size,mgscoop::script_bridge::Replacement& replacement) noexcept {
    static constexpr char prefix[]="return 'MGS_COOP_CARGO_V1' -- ";
    static constexpr char prefix2[]="return 'MGS_COOP_CARGO_V2' -- ";
    constexpr std::size_t n=sizeof(prefix)-1;
    const bool v2=buffer&&size==n+5&&std::memcmp(buffer,prefix2,n)==0;
    if(!world_on||!buffer||size!=n+5||(!v2&&std::memcmp(buffer,prefix,n)!=0)||!(OnRuntimeThread()||OnTerminationThread())) return false;
    const char h=buffer[n],sp=buffer[n+1],d=buffer[n+2],l=buffer[n+4];
    static constexpr char roles[3][9]={"return 0","return 1","return 2"};constexpr char no[]="return false";
    if((h!='0'&&h!='1')||sp!=' '||(d!='0'&&d!='1')||buffer[n+3]!=' '||(l!='0'&&l!='1')){replacement={no,sizeof(no)-1};world_quiet=true;return true;}
    mgscoop::coop_carry::SetCargoHold(h=='1');mgscoop::coop_carry::SetCargoLocal(l=='1');
    if(d=='1') mgscoop::coop_carry::RequestLocalPutDown();
    const unsigned role=mgscoop::coop_carry::CarryRole()<3?mgscoop::coop_carry::CarryRole():0u;
    if(v2){
        const auto state=mgscoop::coop_carry::ReadAttachmentState();static char reply[192]{};
        if(FAILED(StringCchPrintfA(reply,ARRAYSIZE(reply),
            "return {ready=%s,role=%u,carrying=%s,carried=%s,localCarried=%s,peerCarrying=%s}",
            state.ready?"true":"false",role,state.carrying[0]?"true":"false",state.carried[1]?"true":"false",
            state.carried[0]?"true":"false",state.carrying[1]?"true":"false")))return false;
        replacement={reply,std::strlen(reply)};
    }else replacement={roles[role],std::strlen(roles[role])};
    world_quiet=true;return true;
}
bool SelectTeamReceipt(const char* buffer,std::size_t size,mgscoop::script_bridge::Replacement& replacement) noexcept {
    if(!world_on||!mgscoop::team_wake::Recognized(buffer,size)||!(OnRuntimeThread()||OnTerminationThread())) return false;
    mgscoop::team_wake::Receipt receipt{};
    const bool ok=mgscoop::team_wake::Parse(buffer,size,receipt);
    static constexpr char roles[3][9]={"return 0","return 1","return 2"};constexpr char no[]="return false";
    const unsigned role=mgscoop::coop_carry::CarryRole()<3?mgscoop::coop_carry::CarryRole():0u;
    // A malformed owned receipt cannot leave prior wake authorization live.
    team_state.valid=ok;
    if(ok){team_state.receipt=receipt;team_state.at=GetTickCount64();}
    replacement={ok?roles[role]:no,ok?std::strlen(roles[role]):sizeof(no)-1};world_quiet=true;return true;
}
// Equality of actual completed birth receipts; no purpose or writer is acquired.
bool SameColdBirthCensus(const mgscoop::partner_input_hook::NativePoseBirthCensus& a,const mgscoop::partner_input_hook::NativePoseBirthCensus& b) noexcept {
    if(a.image!=b.image||a.player!=b.player||a.data!=b.data||a.state!=b.state||a.lifecycle!=b.lifecycle||
       a.generation!=b.generation||a.sceneEpoch!=b.sceneEpoch||a.thread!=b.thread||a.sequence!=b.sequence)return false;
    for(unsigned k=0;k<2;++k)if(!a.birth[k]||a.birth[k]!=b.birth[k]||a.block[k]!=b.block[k]||a.ps[k]!=b.ps[k]||a.cs[k]!=b.cs[k]||a.entry[k]!=b.entry[k])return false;
    return true;
}
bool HorseCargoNativeDiagnostic(std::uint16_t id,unsigned lineKind,char* line,std::size_t capacity) noexcept {
    namespace horse=mgscoop::buddy_capacity_census;
    namespace cargo=mgscoop::horse_cargo_native_read;
    namespace bridge=mgscoop::horse_native_service_bridge;
    cargo::Observation observed{};const char* reason="admission";observed.gameId=id;
    const auto finish=[&](bool ready) noexcept {
        if(lineKind==2)return bridge::CandidatesLine(observed.bridge,line,capacity);
        return lineKind==1?bridge::Line(observed.bridge,ready,reason,line,capacity):cargo::Line(observed,ready,reason,line,capacity);
    };
    if(!mgscoop::worker_scenario::Compiled()||!mgscoop::worker_scenario::ThreadAdmitted()||
       !offline_enabled||!lab::SavesRedirected()||!partner_on||!partner_gate_ok||!game_base||!game_span)return finish(false);
    auto code=mgscoop::partner_input_hook::ReadNativePoseOriginalCode;std::uintptr_t mismatch=0;
    if(!BuddyPinsMatch(game_base,game_span,horse::kPins)||
       !cargo::CodeGate(code,reinterpret_cast<std::uintptr_t>(game_base),game_span,mismatch)){reason="pins";return finish(false);}
    const auto before=mgscoop::world_link::GetStatus();pp::State state{};unsigned short mission=0;
    std::uintptr_t controller=0;std::uint8_t active=0;reason="scope";
    if(!OnRuntimeThread()||mission_terminating||gameplay_restore_pending||!ReadMission(mission)||mission!=partner_mission||
       !before.configured||!before.paired||!before.link_up||before.effective<3||!before.epoch||
       pp::ReadState(partner_addresses,state)!=pp::Refusal::none||state.local||state.sync||state.ids[0]!=0||state.ids[1]!=1||
       !pp::Realized(state,0)||!pp::Realized(state,1)||state.parts[0]!=3||state.parts[1]!=3||
       !BuddySelection(controller,active)||active!=1)return finish(false);
    mgscoop::partner_input_hook::NativePoseBirthCensus births{};
    if(!mgscoop::partner_input_hook::ReadNativePoseBirthCensus(births)||births.player!=state.P||births.data!=state.Q||births.state!=state.S){reason="birth_scope";return finish(false);}
    const auto thread=GetCurrentThreadId();const auto now=GetTickCount64();
    const auto generation=worker_loading_generation?worker_loading_generation:1u;
    const horse::Authorization authorization{true,true,true,true,true,true};
    const horse::Owner owner{state.P,mission,generation,thread,1,before.epoch,now,true,true,false,false};
    const auto source=[&](horse::Census& current,const char*& why) noexcept {
        const auto currentWorld=mgscoop::world_link::GetStatus();pp::State currentPair{};unsigned short currentMission=0;
        std::uintptr_t currentController=0;std::uint8_t currentActive=0;
        if(!OnRuntimeThread()||mission_terminating||gameplay_restore_pending||!ReadMission(currentMission)||currentMission!=mission||
           !currentWorld.configured||!currentWorld.paired||!currentWorld.link_up||currentWorld.effective<3||currentWorld.epoch!=before.epoch||
           (worker_loading_generation?worker_loading_generation:1u)!=generation||
           pp::ReadState(partner_addresses,currentPair)!=pp::Refusal::none||currentPair.P!=state.P||currentPair.Q!=state.Q||
           currentPair.local||currentPair.sync||!pp::Realized(currentPair,0)||!pp::Realized(currentPair,1)||
           currentPair.parts[0]!=3||currentPair.parts[1]!=3||
           !BuddySelection(currentController,currentActive)||currentController!=controller||currentActive!=active){why="scope_changed";return false;}
        why=horse::ReadPlayPose({nullptr,UpperPhaseRead},reinterpret_cast<std::uintptr_t>(game_base),authorization,owner,thread,GetTickCount64(),current);
        return !why;
    };
    const auto read=[](std::uintptr_t at,void* out,std::size_t size) noexcept {return UpperPhaseRead(nullptr,at,out,size);};
    auto reader=read;
    const bool ready=cargo::ObserveCurrent(reader,reinterpret_cast<std::uintptr_t>(game_base),source,id,observed,reason);
    // Root should retain its already-implemented post-observation live world/
    // current pair/birth/selection closure too. ObserveCurrent closes the
    // native service/topology/actor/unit source itself; it grants no writer.
    mgscoop::partner_input_hook::NativePoseBirthCensus late{};
    if(!mgscoop::partner_input_hook::ReadNativePoseBirthCensus(late)||!SameColdBirthCensus(births,late)){reason="birth_changed";return finish(false);}
    return finish(ready);
}

// Insert in game_script_observer.cpp after HorseCargoNativeDiagnostic.
// Private read only; neither this function nor the reader acquires a purpose.
bool HorseCargoPairDiagnostic(std::uint16_t id,char* line,std::size_t capacity) noexcept {
    namespace pair=mgscoop::horse_cargo_pair_read;
    namespace horse=mgscoop::buddy_capacity_census;
    namespace hk=mgscoop::partner_input_hook;
    pair::Observation observed{};observed.horse.gameId=id;const char* reason="cargo_private_scope";
    if(!mgscoop::worker_scenario::Compiled()||!mgscoop::worker_scenario::ThreadAdmitted()||
       !offline_enabled||!lab::SavesRedirected()||!partner_on||!partner_gate_ok||!game_base||!game_span)
        return pair::Line(observed,false,reason,line,capacity);
    auto original=mgscoop::vehicle_native_protection::ReadOriginal;std::uintptr_t mismatch=0;
    if(!pair::CodeGate(original,reinterpret_cast<std::uintptr_t>(game_base),game_span,mismatch)) {
        char why[48]{};_snprintf_s(why,sizeof(why),_TRUNCATE,"cargo_pins_%llx",static_cast<unsigned long long>(mismatch));
        LogFormat("horse_cargo_pair_gate=refused;rva=0x%llx\r\n",static_cast<unsigned long long>(mismatch));
        return pair::Line(observed,false,why,line,capacity);
    }
    const auto source=[&](pair::Current& c,const char*& why) noexcept {
        c={};why="cargo_current_scope";
        if(!PhysicalScope(c.carry)||gameplay_restore_pending)return false;
        const auto world=mgscoop::world_link::GetStatus();pp::State st{};unsigned short mission=0;
        const auto currentPair=[&](pp::State& value) noexcept {
            return pp::ReadState(partner_addresses,value)==pp::Refusal::none&&value.P==c.carry.player&&
                value.Q==c.carry.data&&value.S==c.carry.state&&!value.local&&!value.sync&&value.ids[0]==0&&value.ids[1]==1&&
                pp::Realized(value,0)&&pp::Realized(value,1)&&value.parts[0]==3&&value.parts[1]==3;
        };
        if(!world.configured||!world.paired||!world.link_up||world.effective<3||world.epoch!=c.carry.epoch||
           !ReadMission(mission)||mission!=c.carry.mission||!currentPair(st)||
           !hk::ReadNativePoseBirthCensus(c.births)||!BuddySelection(c.buddySelection,c.activeBuddy))return false;
        c.loadingGeneration=worker_loading_generation?worker_loading_generation:1u;
        if(const char* denied=pair::Refusal(c)){why=denied;return false;}
        const horse::Authorization auth{true,true,true,true,true,true};
        const horse::Owner owner{c.carry.player,mission,c.loadingGeneration,c.carry.thread,1,
            c.carry.epoch,GetTickCount64(),true,true,false,false};
        why=horse::ReadPlayPose({nullptr,UpperPhaseRead},c.carry.image,auth,owner,c.carry.thread,GetTickCount64(),c.horse);
        if(why)return false;
        why="cargo_capture_scope";
        pair::Current late=c;const auto after=mgscoop::world_link::GetStatus();pp::State latePair{};unsigned short lateMission=0;
        if(!PhysicalScope(late.carry)||gameplay_restore_pending||!after.configured||!after.paired||!after.link_up||
           after.effective<3||after.epoch!=world.epoch||!ReadMission(lateMission)||lateMission!=mission||!currentPair(latePair)||
           !hk::ReadNativePoseBirthCensus(late.births)||!BuddySelection(late.buddySelection,late.activeBuddy))return false;
        late.loadingGeneration=worker_loading_generation?worker_loading_generation:1u;
        // PhysicalScope reads real WindowSeq/InBatch again here. No cached
        // before value is substituted for the actual final engine window.
        if(!pair::SameCurrent(c,late)){why="cargo_capture_changed";return false;}
        c=late;why="cargo_current_observed";return true;
    };
    auto reader=[](std::uintptr_t at,void* out,std::size_t size) noexcept {return UpperPhaseRead(nullptr,at,out,size);};
    const bool ready=pair::Observe(reader,source,id,observed,reason);
    return pair::Line(observed,ready,reason,line,capacity);
}

// Read-only private physical diagnostics. Never supplies writer authority.
bool PhysicalScope(mgscoop::carry_plugin_read::Scope& scope) noexcept {
    namespace hp=mgscoop::horse_player_physical_read;scope={};
    const auto own=mgscoop::downed_player::GetSnapshot();const auto now=GetTickCount64();
    pp::State st{};const bool native=partner_on&&partner_gate_ok&&pp::ReadState(partner_addresses,st)==pp::Refusal::none;
    const auto world=mgscoop::world_link::GetStatus();
    const bool admitted=mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&
        OnRuntimeThread()&&!mission_terminating&&offline_enabled&&lab::SavesRedirected()&&native&&
        world.paired&&world.link_up&&world.effective>=3&&world.epoch==own.epoch;
    auto code=mgscoop::partner_input_hook::ReadNativePoseOriginalCode;std::uintptr_t mismatch=0;
    scope.exactGate=admitted&&hp::CodeGate(code,reinterpret_cast<std::uintptr_t>(game_base),game_span,mismatch);
    scope.admitted=admitted;scope.ready=own.ready;scope.armed=own.armed;scope.batch=mgscoop::partner_defer::InBatch();
    scope.image=reinterpret_cast<std::uintptr_t>(game_base);scope.player=st.P;scope.data=st.Q;scope.state=st.S;
    scope.mission=partner_mission;scope.ownedMission=own.mission;scope.epoch=world.epoch;scope.ownedEpoch=own.epoch;
    scope.generation=scope.ownedGeneration=own.generation;scope.thread=GetCurrentThreadId();scope.ownerThread=mission_thread;
    scope.now=now;scope.poll=own.lastPoll;scope.sequenceBefore=scope.sequenceAfter=mgscoop::partner_input_hook::WindowSeq();
    return mgscoop::carry_plugin_read::ScopeRefusal(scope)==nullptr;
}
bool PhysicalCallbackSample(void*,const mgscoop::horse_physical_callback_probe::RawWindow& raw,mgscoop::horse_physical_callback_probe::Sample& out) noexcept {
    mgscoop::carry_plugin_read::Scope scope{};if(!PhysicalScope(scope))return false;
    auto reader=[](std::uintptr_t at,void* bytes,std::size_t size) noexcept {SIZE_T done=0;return at&&size&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),bytes,size,&done)&&done==size;};
    return mgscoop::horse_physical_callback_probe::SampleCurrent(reader,scope,raw,out);
}
void RefreshPhysicalProbe() noexcept {
    if(!physical_probe_prepared)return;
    static std::uint64_t next=0;const auto now=GetTickCount64();if(now<next)return;next=now+200;
    mgscoop::carry_plugin_read::Scope scope{};mgscoop::horse_player_physical_read::Observation observed{};
    auto reader=[](std::uintptr_t at,void* bytes,std::size_t size) noexcept {SIZE_T done=0;return at&&size&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),bytes,size,&done)&&done==size;};
    const bool ready=PhysicalScope(scope)&&mgscoop::horse_player_physical_read::Observe(reader,scope,observed);
    mgscoop::horse_physical_probe_glue::RefreshCandidates(observed,mission_thread,ready);
    namespace hk=mgscoop::partner_input_hook;namespace nr=mgscoop::horse_physical_natural_read;
    hk::NativePoseBirthCensus births{},late{};bool current=ready&&hk::ReadNativePoseBirthCensus(births)&&
        births.player==scope.player&&births.data==scope.data&&births.state==scope.state&&births.thread==scope.ownerThread;
    for(unsigned k=0;k<2&&current;++k)current=births.block[k]==observed.carry.body[k].block&&births.ps[k]==observed.carry.body[k].ps&&
        births.cs[k]==observed.body[k].cs&&births.entry[k]==observed.body[k].entry;
    current=current&&hk::ReadNativePoseBirthCensus(late)&&late.player==births.player&&late.data==births.data&&
        late.state==births.state&&late.lifecycle==births.lifecycle&&late.generation==births.generation&&late.sceneEpoch==births.sceneEpoch;
    for(unsigned k=0;k<2&&current;++k)current=late.block[k]==births.block[k]&&late.ps[k]==births.ps[k]&&late.cs[k]==births.cs[k]&&
        late.entry[k]==births.entry[k]&&late.birth[k]==births.birth[k];
    if(current){static std::uint64_t serial=0;if(serial==UINT64_MAX){mgscoop::horse_physical_probe_glue::ClearNatural();return;}
        nr::Published p{};p.image=scope.image;p.player=scope.player;p.data=scope.data;p.state=scope.state;
        p.mission=scope.mission;p.generation=scope.generation;p.luaOwnerThread=scope.ownerThread;p.epoch=scope.epoch;
        p.at=GetTickCount64();p.serial=++serial;p.admitted=true;p.exactPins=true;
        for(unsigned k=0;k<2;++k){p.blocks[k]=births.block[k];p.ps[k]=births.ps[k];p.birth[k]=births.birth[k];p.binding[k]=observed.body[k];}
        mgscoop::horse_physical_probe_glue::PublishNatural(p);
    }else mgscoop::horse_physical_probe_glue::ClearNatural();
}
// Cold sequence-manager census retains actual body births and current scene twice.
// It never invokes provider slots, changes the global selector or grants a purpose.
bool PrologueProviderLine(char* out,std::size_t capacity) noexcept {
    namespace pr=mgscoop::prologue_provider_read;namespace hk=mgscoop::partner_input_hook;
    mgscoop::carry_plugin_read::Scope scope{};hk::NativePoseBirthCensus births{},late{};pr::Observation observation{};
    const char* reason="seq_scope";bool ready=PhysicalScope(scope)&&hk::ReadNativePoseBirthCensus(births)&&
        births.player==scope.player&&births.data==scope.data&&births.state==scope.state&&births.sequence==scope.sequenceBefore;
    auto code=hk::ReadNativePoseOriginalCode;std::uintptr_t mismatch=0;
    const bool exact=ready&&pr::CodeGate(code,scope.image,game_span,mismatch);
    if(ready&&!exact)reason="seq_pins";
    auto read=[](std::uintptr_t at,void* bytes,std::size_t size) noexcept {return UpperPhaseRead(nullptr,at,bytes,size);};
    pr::Scope current{scope.image,game_span,exact,ready,ready};
    ready=exact&&pr::Observe(read,current,observation,reason);
    mgscoop::carry_plugin_read::Scope after{};
    if(ready&&(!PhysicalScope(after)||after.player!=scope.player||after.data!=scope.data||after.state!=scope.state||
       after.mission!=scope.mission||after.epoch!=scope.epoch||after.generation!=scope.generation||
       !hk::ReadNativePoseBirthCensus(late)||!SameColdBirthCensus(births,late))){ready=false;reason="seq_owner_changed";}
    return pr::Line(observation,ready,reason,out,capacity);
}
// Publication comes from a real shared dual-parts callback with current births.
// It is read-only context for another engine thread, never a replaced Lua scope.
void PublishPhysicalDual(const mgscoop::partner_input_hook::NativePoseDualPartsContext& context) noexcept {
    namespace hk=mgscoop::partner_input_hook;namespace hp=mgscoop::horse_player_physical_read;
    namespace nr=mgscoop::horse_physical_natural_read;
    if(!physical_probe_prepared)return;
    mgscoop::carry_plugin_read::Scope scope{};hp::Observation physical{};hk::NativePoseDualPartsContext again{};
    auto reader=[](std::uintptr_t at,void* out,std::size_t size) noexcept {SIZE_T done=0;return at&&size&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&done)&&done==size;};
    bool ok=PhysicalScope(scope)&&hp::Observe(reader,scope,physical)&&context.thread==scope.ownerThread&&
        context.player==scope.player&&context.data==scope.data&&context.state==scope.state&&
        hk::ReadNativePoseDualPartsPoll(again)&&hk::SameNativePoseDualPartsPoll(context,again);
    for(unsigned k=0;k<2&&ok;++k)ok=context.birthSerial[k]&&context.block[k]==physical.carry.body[k].block&&
        context.ps[k]==physical.carry.body[k].ps&&context.modelCS[k]==physical.body[k].cs&&context.modelEntry[k]==physical.body[k].entry;
    const auto seq=hk::WindowSeq();ok=ok&&scope.sequenceBefore==seq&&!(seq&1u)&&!mgscoop::partner_defer::InBatch();
    if(!ok){mgscoop::horse_physical_probe_glue::ClearNatural();return;}
    static std::uint64_t serial=0;if(serial==UINT64_MAX){mgscoop::horse_physical_probe_glue::ClearNatural();return;}
    nr::Published p{};p.image=scope.image;p.player=scope.player;p.data=scope.data;p.state=scope.state;
    p.mission=scope.mission;p.generation=scope.generation;p.luaOwnerThread=scope.ownerThread;p.epoch=scope.epoch;
    p.at=GetTickCount64();p.serial=++serial;p.admitted=true;p.exactPins=true;
    for(unsigned k=0;k<2;++k){p.blocks[k]=context.block[k];p.ps[k]=context.ps[k];p.birth[k]=context.birthSerial[k];p.binding[k]=physical.body[k];}
    mgscoop::horse_physical_probe_glue::PublishNatural(p);
}
bool PhysicalTransformLine(char* out,std::size_t capacity) noexcept {
    mgscoop::carry_plugin_read::Scope scope{};mgscoop::horse_player_physical_read::Observation observed{};
    auto reader=[](std::uintptr_t at,void* bytes,std::size_t size) noexcept {SIZE_T done=0;return at&&size&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),bytes,size,&done)&&done==size;};
    std::uintptr_t rows[2][3]{};bool ready=PhysicalScope(scope)&&mgscoop::horse_player_physical_read::Observe(reader,scope,observed);
    for(unsigned k=0;k<2&&ready;++k){std::uintptr_t late[3]{};const auto p=observed.body[k].transform;
        ready=reader(p,&rows[k][0],8)&&reader(p+0x10,&rows[k][1],8)&&reader(rows[k][1]+0x50,&rows[k][2],8)&&
            reader(p,&late[0],8)&&reader(p+0x10,&late[1],8)&&reader(late[1]+0x50,&late[2],8)&&std::memcmp(rows[k],late,sizeof(late))==0;
        for(auto& value:rows[k]){if(value<scope.image||value-scope.image>=game_span)ready=false;value=ready?value-scope.image:0;}}
    ready=ready&&scope.sequenceBefore==mgscoop::partner_input_hook::WindowSeq()&&!mgscoop::partner_defer::InBatch();
    const int n=std::snprintf(out,capacity,"physTransform;ready=%u;b0=%llx,%llx,%llx;b1=%llx,%llx,%llx",ready?1u:0u,
        static_cast<unsigned long long>(rows[0][0]),static_cast<unsigned long long>(rows[0][1]),static_cast<unsigned long long>(rows[0][2]),
        static_cast<unsigned long long>(rows[1][0]),static_cast<unsigned long long>(rows[1][1]),static_cast<unsigned long long>(rows[1][2]));
    return n>=0&&static_cast<std::size_t>(n)<capacity;
}
// Cryptographic fingerprint of a bounded copied source, never raw asset output.
bool SleepAssetHash(const unsigned char* bytes,std::size_t size,char* out) noexcept {
    if(!bytes||!size||size>mgscoop::sleep_standing_cycle_asset::kMainCeiling||!out)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE digest=nullptr;unsigned char result[32]{};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&digest,nullptr,0,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(digest,const_cast<unsigned char*>(bytes),static_cast<ULONG>(size),0)>=0;
    if(ok)ok=BCryptFinishHash(digest,result,sizeof(result),0)>=0;
    if(digest)BCryptDestroyHash(digest);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(!ok)return false;constexpr char alphabet[]="0123456789abcdef";
    for(unsigned i=0;i<32;++i){out[2*i]=alphabet[result[i]>>4];out[2*i+1]=alphabet[result[i]&15];}out[64]=0;return true;
}
bool SleepAssetLine(char* out,std::size_t capacity) noexcept {
    namespace ar=mgscoop::sleep_motion_asset_read;namespace sp=mgscoop::sleep_plugin_read;namespace sg=mgscoop::sleep_graph_read;
    if(!out||!capacity)return false;mgscoop::carry_plugin_read::Scope current{};
    const bool admitted=PhysicalScope(current);ar::Scope scope{};scope.privateCopiedSaveLab=admitted;
    scope.privateUniqueRestProfile=admitted&&MountedMgoUniqueRestContext(unique_bind_directory);
    scope.owner={current.exactGate,current.admitted,current.ready,current.armed,current.batch,current.image,current.player,current.data,current.state,
        current.mission,current.ownedMission,current.generation,current.ownedGeneration,current.thread,current.ownerThread,
        current.epoch,current.ownedEpoch,current.sequenceBefore,current.sequenceAfter,current.now,current.poll};
    auto code=mgscoop::partner_input_hook::ReadNativePoseOriginalCode;std::uintptr_t mismatch=0;
    scope.owner.exactGate=admitted&&sp::CodeGate(code,current.image,game_span,mismatch)&&sg::CodeGate(code,current.image,game_span,mismatch);
    scope.codeGate=scope.owner.exactGate&&ar::CodeGate(code,current.image,game_span,mismatch);
    auto reader=[](std::uintptr_t at,void* bytes,std::size_t size) noexcept {SIZE_T done=0;return at&&size&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),bytes,size,&done)&&done==size;};
    auto hasher=SleepAssetHash;ar::Reply reply{};ar::ReadPair(reader,hasher,scope,reply);
    const int n=std::snprintf(out,capacity,"sleepAsset;ready=%u;sel=%u;b0=%u,%u,%llx;b1=%u,%u,%llx;why=%s",reply.ready?1u:0u,reply.selected?1u:0u,
        unsigned(reply.body[0].origin),reply.body[0].index,static_cast<unsigned long long>(reply.body[0].motion),
        unsigned(reply.body[1].origin),reply.body[1].index,static_cast<unsigned long long>(reply.body[1].motion),reply.reason);
    return n>=0&&static_cast<std::size_t>(n)<capacity;
}
bool StandingCycleAssetLine(char* out,std::size_t capacity) noexcept {
    namespace ar=mgscoop::sleep_standing_cycle_asset;namespace sp=mgscoop::sleep_plugin_read;namespace sg=mgscoop::sleep_graph_read;
    if(!out||!capacity)return false;mgscoop::carry_plugin_read::Scope current{};
    const bool admitted=PhysicalScope(current);ar::Scope scope{};scope.privateCopiedSaveLab=admitted;
    scope.exactStandingProfile=admitted&&MountedMgoStandingCycleContext(unique_bind_directory);
    scope.owner={current.exactGate,current.admitted,current.ready,current.armed,current.batch,current.image,current.player,current.data,current.state,
        current.mission,current.ownedMission,current.generation,current.ownedGeneration,current.thread,current.ownerThread,
        current.epoch,current.ownedEpoch,current.sequenceBefore,current.sequenceAfter,current.now,current.poll};
    auto code=mgscoop::partner_input_hook::ReadNativePoseOriginalCode;std::uintptr_t mismatch=0;
    scope.owner.exactGate=admitted&&sp::CodeGate(code,current.image,game_span,mismatch)&&sg::CodeGate(code,current.image,game_span,mismatch);
    scope.codeGate=scope.owner.exactGate&&ar::CodeGate(code,current.image,game_span,mismatch);
    auto reader=[](std::uintptr_t at,void* bytes,std::size_t size) noexcept {SIZE_T done=0;return at&&size&&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),bytes,size,&done)&&done==size;};
    auto hasher=SleepAssetHash;ar::Reply reply{};ar::ReadPair(reader,hasher,scope,reply);
    const int n=std::snprintf(out,capacity,"sleepCycle;ready=%u;sel=%u;b0=%u,%u,%llx;b1=%u,%u,%llx;why=%s",reply.ready?1u:0u,reply.selected?1u:0u,
        unsigned(reply.body[0].origin),reply.body[0].index,static_cast<unsigned long long>(reply.body[0].motion),
        unsigned(reply.body[1].origin),reply.body[1].index,static_cast<unsigned long long>(reply.body[1].motion),reply.reason);
    return n>=0&&static_cast<std::size_t>(n)<capacity;
}
// Runtime thread, every partner tick.
void CoopRescueTick() noexcept {
    // Runs cold on the ordinary coordinator tick, never from a native hook.
    mgscoop::coop_vehicle_mirror::TickLocal();
    RefreshPhysicalProbe();
    if(!coop_rescue_configured&&coop_rescue_pending&&game_base&&game_span&&!mission_terminating){
        coop_rescue_pending=false;
        if(mgscoop::coop_carry::Installed()||mgscoop::coop_carry::Install(game_base,game_span,Log)){
            mgscoop::coop_carry::SetSender(&CoopCarrySend);
            mgscoop::coop_carry::SetMode(mgscoop::coop_carry::kAnyDowned);
            coop_rescue_configured=true;
        }
        Log(coop_rescue_configured?"coop_rescue=installed; sleep=native; carry=player\r\n":"coop_rescue=install_refused\r\n");
    }
    if(!coop_rescue_configured) return;
    const auto now=GetTickCount64();
    const auto local=mgscoop::downed_player::GetSnapshot();
    const mgscoop::team_wake::Context context{local.mission,local.epoch,now,team_state.at,
        team_state.valid&&local.mission==partner_mission&&DownedAdmitted(local.mission,local.epoch),
        local.ready&&local.armed&&now>=local.lastPoll&&now-local.lastPoll<=500,mission_terminating};
    const auto state=mgscoop::team_wake::Evaluate(team_state.receipt,context);
    mgscoop::coop_passenger::Tick(local.mission,local.epoch,state.known);
    const bool localDown=local.down&&(local.cause==1||local.cause==2);
    // The teammate's body sleeps natively while they are darted (both machines
    // show the stock sleep, and the stock carried state keeps a sleeping body).
    // Unknown/stale/unowned input preserves existing sleep duty. Only a fresh
    // exact-generation grant or actual healthy peer readback may release it.
    if(state.known&&state.peerAsleep!=coop_partner_sleep_held){
        if(mgscoop::coop_sleep::Hold(1,state.peerAsleep)){
            coop_partner_sleep_held=state.peerAsleep;
            LogFormat("coop_rescue=partner_sleep; held=%u; wake_grant=%u\r\n",state.peerAsleep?1u:0u,state.peerWake?1u:0u);
        }
    }
    // Wait for BOTH original falling-asleep graphs to complete before pickup.
    const bool localReady=localDown&&(local.cause==1||mgscoop::coop_sleep::CarryReady(0));
    const bool peerReady=state.peerCarryable&&(team_state.receipt.peerCause==1||mgscoop::coop_sleep::CarryReady(1));
    mgscoop::coop_carry::SetReadinessFrame(local.mission,local.epoch,local.generation,team_state.receipt.peerGeneration,
        localReady,state.known&&team_state.receipt.version2);
    mgscoop::coop_carry::SetCarryable((peerReady&&mgscoop::coop_carry::PeerCarryReady()?2u:0u)|(localReady?1u:0u));
    mgscoop::coop_carry::SetLocalAwakeDown(localDown&&local.cause==1);
    mgscoop::coop_carry::SetLocalAsleep(localDown&&local.cause==2);
    // A carried teammate who was woken or revived is put down (stock put-down).
    if(state.known&&state.putDown&&mgscoop::coop_carry::CarryRole()==1&&mgscoop::coop_carry::RequestLocalPutDown()){
        static std::uint64_t logged=0;
        if(now-logged>2000){logged=now;Log(state.peerWake?"coop_rescue=put_down; reason=teammate_wake_granted\r\n":"coop_rescue=put_down; reason=teammate_recovered\r\n");}
    }
}
bool SelectFriendlyHitReceipt(const char* buffer,std::size_t size,mgscoop::script_bridge::Replacement& replacement) noexcept {
    constexpr char prefix[]="return 'MGS_COOP_FRIENDLY_HIT_V1' -- ";
    if(!world_on || !buffer || size<=sizeof(prefix)-1 || size>192 || std::memcmp(buffer,prefix,sizeof(prefix)-1)!=0) return false;
    const char* cursor=buffer+sizeof(prefix)-1;const char* end=buffer+size;
    std::uint32_t v[7]{};
    for(unsigned i=0;i<7;++i) {
        auto parsed=std::from_chars(cursor,end,v[i]);
        if(parsed.ec!=std::errc() || parsed.ptr==cursor) return false;
        cursor=parsed.ptr;if(i!=6) {if(cursor==end || *cursor!=' ') return false;++cursor;}
    }
    if(cursor!=end || v[0]>99999 || !v[1] || v[1]>65535 || v[2]>1 || !v[3] || v[5]>1000000000u ||
        (v[6]!=0 && v[6]!=2) || !OnRuntimeThread()) return false;
    mgscoop::downed_player::Reply r{};
    const bool admitted=DownedAdmitted(v[0],static_cast<std::uint16_t>(v[1]));
    mgscoop::downed_player::FriendlyControl(v[0],static_cast<std::uint16_t>(v[1]),admitted,
        static_cast<std::uint8_t>(v[2]),v[3],v[4],v[5],static_cast<std::uint8_t>(v[6]),r);
    if(v[2]) LogFormat("friendly_hit=owner;seq=%lu;damage_milli=%lu;cause=%u;applied=%u;down=%u;generation=%lu;reason=%s\r\n",
        static_cast<unsigned long>(v[3]),static_cast<unsigned long>(v[5]),unsigned(v[6]),r.applied?1u:0u,r.down?1u:0u,
        static_cast<unsigned long>(r.generation),r.reason?r.reason:"unknown");
    return DownedReply(r,replacement);
}
bool SelectDownedReceipt(const char* buffer, std::size_t size, mgscoop::script_bridge::Replacement& replacement) noexcept {
    constexpr char prefix[]="return 'MGS_COOP_DOWNED_V1' -- ";
    if(!world_on || size<=sizeof(prefix)-1 || size>160 || std::memcmp(buffer,prefix,sizeof(prefix)-1)!=0) return false;
    const char* cursor=buffer+sizeof(prefix)-1;
    const char* end=buffer+size;
    // mission epoch command generation [posture]: the optional fifth field
    // (commands 0 and 1 only) is the Lua owner's PlayerStatus CBOX, 0 or 1.
    std::uint32_t v[5]{};
    unsigned fields=0;
    for(unsigned i=0;i<5;++i) {
        auto parsed=std::from_chars(cursor,end,v[i]);
        if(parsed.ec!=std::errc() || parsed.ptr==cursor) return false;
        cursor=parsed.ptr;fields=i+1;
        if(cursor==end || i==4) break;
        if(*cursor!=' ') return false;
        ++cursor;
    }
    if(cursor!=end || fields<4 || v[0]>99999 || !v[1] || v[1]>65535 || v[2]>5 || v[4]>1 || (fields==5 && v[2]>1) ||
       !(OnRuntimeThread() || (v[2]==3 && OnTerminationThread()))) return false;
    const bool admitted=DownedAdmitted(v[0],static_cast<std::uint16_t>(v[1]));
    const bool boxed=fields==5 && v[4]==1;
    mgscoop::downed_player::Reply r{};
    mgscoop::downed_player::Control(v[0],static_cast<std::uint16_t>(v[1]),admitted,
        static_cast<std::uint8_t>(v[2]),v[3],r,boxed);
    if(!DownedReply(r,replacement)) return false;
    static std::uint64_t downed_log_at=0;
    static std::uint32_t box_logged_generation=0;
    // Edges are never rate-limited: a controls timeout (the cycle ends) and the
    // first applied lock of a generation in the box posture (item 4).
    const bool timeout=r.reason && std::strncmp(r.reason,"controls_timeout_",17)==0;
    const bool box_edge=boxed && v[2]==1 && r.applied && r.down && r.generation!=box_logged_generation;
    if(box_edge) box_logged_generation=r.generation;
    if(timeout || box_edge ||
       ((!r.ready || (v[2]==1 && !r.applied) || (r.reason && std::strncmp(r.reason,"alive_rebound_",14)==0)) && GetTickCount64()>=downed_log_at)) {
        if(!timeout && !box_edge) downed_log_at=GetTickCount64()+1000;
        LogFormat("downed_control=%s;command=%u;reason=%s;posture=%s;generation=%lu\r\n",r.ready?"observed":"refused",unsigned(v[2]),
            r.reason?r.reason:"unknown",boxed?"box":"none",static_cast<unsigned long>(r.generation));
    }
    world_quiet=true;
    return true;
}
bool PartnerSleepEnableAllowed() noexcept {
 if(vehicle_wiring_enabled.load())return false;
 namespace ih=mgscoop::partner_input_hook;
 const auto pose=ih::NativePoseWindowStatus();const auto world=mgscoop::world_link::GetStatus();
 const auto now=GetTickCount64();const auto owner=mgscoop::downed_player::GetSnapshot();
 if(!PartnerSleepPrivate()||!partner_sleep_configured||!pose.installed||!pose.required||pose.held||pose.faulted||
    pose.windows<2||!ih::NativePoseWholePlayerLifetimeLive()||!ih::NativePoseLocalCacheIsolationLive()||
    !DownedAdmitted(partner_mission,world.epoch)||!owner.ready||!owner.armed||owner.mission!=partner_mission||owner.epoch!=world.epoch||
    !owner.lastPoll||now<owner.lastPoll||now-owner.lastPoll>750||!partner_sleep_arm.ready||now<partner_sleep_arm.at||now-partner_sleep_arm.at>250||
    partner_sleep_arm.thread!=GetCurrentThreadId()||mission_terminating||gameplay_restore_pending||
    !pd::NativeSleepPresentationReady(partner_driver))return false;
 // Actual current physical body0 and cached Q0 must agree after healthy
 // required windows. This is a fresh typed read, not a debug status flag.
 const auto seq=ih::WindowSeq();pp::State a{},b{};float p0[4]{},q0[4]{};
 if((seq&1u)||mgscoop::partner_defer::InBatch()||pp::ReadState(partner_addresses,a)!=pp::Refusal::none||!a.slots_ok||
    !a.vars_ok||a.count!=2||a.local!=0||a.P!=partner_sleep_arm.player||!pp::ReadBody(partner_addresses,a,0,p0,q0)||
    pp::ReadState(partner_addresses,b)!=pp::Refusal::none||a.P!=b.P||a.Q!=b.Q||a.S!=b.S||a.slots!=b.slots||
    ih::WindowSeq()!=seq||mgscoop::partner_defer::InBatch())return false;
 for(unsigned k=0;k<3;++k){if(!std::isfinite(p0[k])||!std::isfinite(a.slot[0][k])||!std::isfinite(a.vars_pos[k])||
     std::fabs(p0[k]-a.slot[0][k])>=0.5f||std::fabs(p0[k]-a.vars_pos[k])>=0.5f||a.slot[0][k]!=b.slot[0][k])return false;}
 return true;
}
#include "vehicle_root_wiring_impl.inc"

bool SelectPartnerSleepReceipt(const char* buffer,std::size_t size,mgscoop::script_bridge::Replacement& replacement) noexcept {
 if(!world_on||!buffer||!(OnRuntimeThread()||OnTerminationThread()))return false;
 static constexpr char yes[]="return true",no[]="return false";
 const auto reply=[&](bool ok) noexcept {replacement={ok?yes:no,ok?sizeof(yes)-1:sizeof(no)-1};world_quiet=true;return true;};
 if(size==sizeof(psbridge::kCapability)-1&&!std::memcmp(buffer,psbridge::kCapability,size)){
  // A first natural downed poll may recover a missed passive early setup.
  // Ensure itself refuses required/held windows; this never opts in or holds.
  if(OnRuntimeThread()&&!mission_terminating&&!gameplay_restore_pending)EnsurePartnerSleepConfigured();
  return reply(partner_sleep_configured&&PartnerSleepPrivate());
 }
 psbridge::Record record{};if(!psbridge::Parse(buffer,size,record))return false;
 if(record.clear){partner_sleep_records={};psleep::Enable(false);psleep::PublishTarget({});return reply(true);}
 const auto owner=mgscoop::downed_player::GetSnapshot();const auto now=GetTickCount64();
 const auto role=psbridge::ConfiguredRole(mgscoop::GetLabConfig().role);
 psbridge::Context context{};context.worker=partner_sleep_configured&&PartnerSleepPrivate();context.naturalOwner=OnRuntimeThread();
 context.admitted=DownedAdmitted(record.mission,record.epoch);
 context.fieldReady=owner.ready&&owner.armed&&owner.mission==record.mission&&owner.epoch==record.epoch&&owner.lastPoll&&now>=owner.lastPoll&&now-owner.lastPoll<=750;
 context.demo=(mgscoop::debug_menu::LuaHud()&1u)!=0;context.stopping=mission_terminating||gameplay_restore_pending;
 context.mission=partner_mission;context.epoch=mgscoop::world_link::GetStatus().epoch;context.role=role;context.thread=GetCurrentThreadId();context.now=now;
 const auto plan=psbridge::Accept(partner_sleep_records,record,context,partner_sleep_arm);
 if(plan.clear){partner_sleep_records=plan.next;psleep::PublishTarget({});return reply(false);}
 if(!plan.ready)return reply(false);
 if(!psleep::PublishTarget(plan.target))return reply(false);
 partner_sleep_records=plan.next;return reply(true);
}
bool SelectDownedHudReceipt(const char* buffer,std::size_t size,mgscoop::script_bridge::Replacement& replacement) noexcept {
    mgscoop::coop_hud::DownedView view{};
    if(!world_on || !mgscoop::coop_hud::ParseDownedHud(buffer,size,view) ||
       !(OnRuntimeThread() || OnTerminationThread())) return false;
    if(view.valid && DownedAdmitted(view.mission,view.epoch)){view.publish_ms=GetTickCount64();view.carry=static_cast<std::uint8_t>(mgscoop::coop_carry::HudRole());}
    else view={};
    mgscoop::coop_hud::PublishDowned(view);
    constexpr char reply[]="return true";
    replacement={reply,sizeof(reply)-1};world_quiet=true;return true;
}
// The developer test boot (docs/test-boot.md): lua/coop/debug_boot.lua asks for
// lab_fast_boot / lab_boot and logs its steps. Only the owned private worker of
// a background-worker build in lab mode with copied saves gets the spec
// (lab_boot::Admitted); everywhere else "return 0" keeps the Lua inert, so a
// friend/Release loader never skips a screen or moves a player.
bool SelectLabBootReceipt(const char* buffer,std::size_t size,const char* name,
                          mgscoop::script_bridge::Replacement& replacement) noexcept {
    namespace lb=mgscoop::lab_boot;
    const auto kind=lb::Classify(buffer,size,name);
    if(kind==lb::Receipt::none) return false;
    static char reply[lb::kReplyCapacity]{};
    static constexpr char inert[]="return 0",yes[]="return true",no[]="return false";
    const auto& config=mgscoop::GetLabConfig();
    lb::Gate gate{};
    gate.worker_compiled=mgscoop::worker_scenario::Compiled();
    gate.worker_active=mgscoop::worker_scenario::Active();
    gate.lab_mode=config.present && config.mode==mgscoop::LabMode::lab;
    gate.lab_active=lab_active;
    gate.offline=offline_enabled;
    gate.copied_saves=lab::SavesRedirected();
    const bool admitted=lb::Admitted(gate);
    world_quiet=true;
    if(kind==lb::Receipt::log) {
        static unsigned lines=0;
        char line[lb::kLogLineCapacity]{};
        const bool ok=admitted && lines<512 && lb::FormatLogLine(buffer,size,line,sizeof(line))!=0;
        if(ok) { ++lines; Log(line); }
        replacement=ok ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1} : mgscoop::script_bridge::Replacement{no,sizeof(no)-1};
        return true;
    }
    const bool named=config.lab_fast_boot || config.lab_boot.mission!=0;
    const auto n=lb::FormatCfgReply(admitted,config.lab_fast_boot,config.lab_boot,config.role==mgscoop::LabRole::host,reply,sizeof(reply));
    replacement=n ? mgscoop::script_bridge::Replacement{reply,n} : mgscoop::script_bridge::Replacement{inert,sizeof(inert)-1};
    static bool logged=false;
    if(named && !logged) {
        logged=true;
        LogFormat("lab_boot=cfg; admitted=%u; fast=%u; mission=%u; pos=%u; clock_s=%d; weather=%s; worker=%u; active=%u; lab=%u; offline=%u; saves=%u; t_ms=%llu\r\n",
            admitted?1u:0u,config.lab_fast_boot?1u:0u,unsigned(config.lab_boot.mission),config.lab_boot.has_pos?1u:0u,int(config.lab_boot.clock_s),
            mgscoop::LabBootWeatherName(config.lab_boot.weather),gate.worker_compiled?1u:0u,gate.worker_active?1u:0u,gate.lab_mode?1u:0u,
            gate.offline?1u:0u,gate.copied_saves?1u:0u,static_cast<unsigned long long>(GetTickCount64()));
    }
    return true;
}

bool ObserveEntry(void*, const char* buffer, std::size_t size, const char* name,
                  mgscoop::script_bridge::Replacement& replacement) noexcept {
    AcquireSRWLockExclusive(&receipt_lock);
    struct Unlock { ~Unlock(){ ReleaseSRWLockExclusive(&receipt_lock); } } unlock;
    const auto count = InterlockedIncrement64(&entries);
    if(SelectLabBootReceipt(buffer,size,name,replacement)) return true;
    if(SelectOfflineEquipmentAllocationReceipt(buffer,size,name,replacement)) return true;
    if(SelectStock46PreloadReceipt(buffer,size,name,replacement)) return true;
    if(SelectEquipPreloadReceipt(buffer,size,name,replacement)) return true;
    if(SelectEquipmentObservationReceipt(buffer,size,name,replacement)) return true;
    if(SelectActionEventsReceipt(buffer,size,name,replacement)) return true;
    if(SelectVehicleNamesReceipt(buffer,size,replacement)) return true;
    if (SelectDebugReceipt(buffer, size, name, replacement)) return true;
    if (SelectRidePoseReceipt(buffer, size, replacement)) return true;
    if (SelectRideTiltReceipt(buffer, size, replacement)) return true;
    if (SelectVehicleDoorReceipt(buffer, size, replacement)) return true;
    if (SelectBoardSeatReceipt(buffer, size, replacement)) return true;
    if (SelectVehicleLifeReceipt(buffer, size, replacement)) return true;
    if (SelectBuddyCapacityReceipt(buffer, size, replacement)) return true;
    if (SelectBuddyPoseReceipt(buffer, size, replacement)) return true;
    if (SelectDogGuardReceipt(buffer, size, replacement)) return true;
    if (SelectDownedReceipt(buffer, size, replacement)) return true;
    if (SelectDownedHudReceipt(buffer, size, replacement)) return true;
    if (SelectPartnerSleepReceipt(buffer, size, replacement)) return true;
    if (SelectTeamReceipt(buffer, size, replacement)) return true;
    if (SelectCargoReceipt(buffer, size, replacement)) return true;
    if (SelectFriendlyHitReceipt(buffer, size, replacement)) return true;
    if(partner_on) {
        const auto partner_kind=partner::Classify(buffer,size,name);
        if(partner_kind!=partner::Receipt::none) return SelectPartnerReceipt(partner_kind,buffer,size,name,count,replacement);
    }
    if(world_on) {
        const auto world_kind=mgscoop::world::Classify(buffer,size,name);
        if(world_kind!=mgscoop::world::Receipt::none) return SelectWorldReceipt(world_kind,buffer,size,name,count,replacement);
    }
    bool selected = bootstrap_enabled && mgscoop::SelectBootstrapPatch(buffer, size, name, replacement, archive_mounted);
    if(!selected)
        selected=mgscoop::SelectDamageParameterPatch(buffer,size,name,replacement,host_create_enabled || client_enabled);
    if(!selected)
        selected=mgscoop::SelectDeathPolicyPatch(buffer,size,name,replacement,host_create_enabled || client_enabled || offline_enabled);
    if (!selected)
        selected = mgscoop::SelectPlayerPackagePatch(buffer, size, name, replacement, allocation_enabled && archive_mounted);
    if (!selected && allocation_enabled && !host_enabled && archive_mounted)
        selected = mgscoop::SelectMissionTracePatch(buffer,size,name,replacement,true);
    if (!selected && host_enabled && archive_mounted) {
        selected = mgscoop::SelectHostTrialPatch(buffer,size,name,replacement,true);
        if(selected) host_source_selected=true;
    }
    if(!selected && host_enabled) selected=SelectMissionBoundary(buffer,size,name,replacement);
    if(host_enabled && buffer && name && size<96 &&
       OnMissionThread() &&
       std::strlen(name)==size && std::memcmp(name,buffer,size)==0) {
        constexpr char snapshot[]="return 'MGS_COOP_TARGET_SNAPSHOT_V1'";
        constexpr char commit0[]="return 'MGS_COOP_TARGET_COMMIT_0_V1'";
        constexpr char commit1[]="return 'MGS_COOP_TARGET_COMMIT_1_V1'";
        constexpr char clear[]="return 'MGS_COOP_TARGET_CLEAR_V1'";
        constexpr char respawn[]="return 'MGS_COOP_RESPAWN_CONTEXT_V1'";
        constexpr char respawn_enable[]="return 'MGS_COOP_RESPAWN_ENABLE_V1'";
        constexpr char respawn_disable[]="return 'MGS_COOP_RESPAWN_DISABLE_V1'";
        constexpr char equipment_returned[]="return 'MGS_COOP_EQUIPMENT_GROUPS_RETURNED_V1'";
        constexpr char equipment_failed[]="return 'MGS_COOP_EQUIPMENT_GROUPS_FAILED_V1'";
        const auto equals=[buffer,size](const char* value,std::size_t bytes) { return size==bytes && std::memcmp(buffer,value,bytes)==0; };
        static constexpr char yes[]="return true",no[]="return false";
        if(equals(equipment_returned,sizeof(equipment_returned)-1) || equals(equipment_failed,sizeof(equipment_failed)-1)) {
            stock46_groups_returned=stock46_groups_authorized && OnAllocationThread() &&
                equals(equipment_returned,sizeof(equipment_returned)-1);
            Log(equals(equipment_returned,sizeof(equipment_returned)-1)
                ? "equipment_groups=lua_calls_returned; ghost_count=1 remote_gear_loaded=unverified\r\n"
                : "equipment_groups=lua_setup_failed; host_admission_and_client_join_withheld=1\r\n");
            replacement={yes,sizeof(yes)-1}; selected=true;
        } else if(equals(respawn_enable,sizeof(respawn_enable)-1) || equals(respawn_disable,sizeof(respawn_disable)-1)) {
            const bool enable=equals(respawn_enable,sizeof(respawn_enable)-1);
            const bool context=!mission_terminating && (host_create_enabled || (client_enabled && !client_check_enabled)) &&
                mgscoop::player_team::IsActive() && std::strcmp(mgscoop::enemy_targeting::RespawnContext(OwnedDynamiteSessionForRespawn()),"return nil")!=0;
            const bool accepted=(!enable || context) && mgscoop::respawn_patch::SetActive(game_base,game_span,enable,Log);
            if(enable && !accepted) mgscoop::respawn_patch::SetActive(game_base,game_span,false,Log);
            replacement=accepted ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                                 : mgscoop::script_bridge::Replacement{no,sizeof(no)-1}; selected=true;
        } else if(equals(respawn,sizeof(respawn)-1)) {
            const char* source="return nil";
            if(!mission_terminating && (host_create_enabled || (client_enabled && !client_check_enabled)) && mgscoop::player_team::IsActive())
                source=mgscoop::enemy_targeting::RespawnContext(OwnedDynamiteSessionForRespawn());
            replacement={source,std::strlen(source)}; selected=true;
        } else if(equals(clear,sizeof(clear)-1)) {
            mgscoop::enemy_targeting::Clear();
            replacement={yes,sizeof(yes)-1}; selected=true;
        } else if(host_create_enabled && mgscoop::player_team::IsActive()) {
            const auto host=OwnedDynamiteHostForTargeting();
            if(equals(snapshot,sizeof(snapshot)-1)) {
                const auto source=mgscoop::enemy_targeting::Snapshot(host);
                if(source) { replacement={source,std::strlen(source)}; selected=true; }
            } else if(equals(commit0,sizeof(commit0)-1) || equals(commit1,sizeof(commit1)-1)) {
                const unsigned index=equals(commit0,sizeof(commit0)-1) ? 0u:1u;
                const bool accepted=mgscoop::enemy_targeting::Commit(host,index);
                if(accepted) Log(index==0 ? "enemy_targeting=selected; player_index=0 lease_ms=1000\r\n"
                                         : "enemy_targeting=selected; player_index=1 lease_ms=1000\r\n");
                replacement=accepted ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                                     : mgscoop::script_bridge::Replacement{no,sizeof(no)-1};
                selected=true;
            }
        }
    }
    constexpr char host_receipt[]="return 'MGS_COOP_HOST_CREATE_V1'";
    if(host_enabled && OnAllocationThread() && buffer && name && size==sizeof(host_receipt)-1 &&
       std::memcmp(buffer,host_receipt,sizeof(host_receipt)-1)==0 && std::strcmp(name,host_receipt)==0) {
        mission_terminating=false;
        const bool needs_player_patch=host_create_enabled || client_enabled;
        const bool cleanup_ready=!gameplay_restore_pending || RestoreGameplayRules();
        const bool player_ready=cleanup_ready && (!needs_player_patch ||
            (mgscoop::marker_loading::IsActive() && PrepareDynamitePlayerLoading(game_base,game_span,Log)));
        // offline-v1 never asks for a session: Lua then never sets S_IS_ONLINE.
        if(offline_enabled) Log("host_create=skipped; mode=offline_v1\r\n");
        const bool created=player_ready && !client_enabled && !offline_enabled && TryCreateHostSession(game_base,game_span,Log,heap_test_enabled,host_create_enabled);
        if(needs_player_patch && (!player_ready || (!client_enabled && !created))) RestoreGameplayRules();
        // Publish the host's own SteamID64 for the lab scripts (lab mode only;
        // the id value itself is never logged). Retried from status polls.
        if(created && host_create_enabled && lab_active) { host_steamid_pending=true; WriteHostSteamIdFile(); }
        static constexpr char success[]="return true", failure[]="return false";
        static constexpr char client[]="return 'client'";
        replacement=player_ready && client_enabled && (client_host || client_check_enabled) ? mgscoop::script_bridge::Replacement{client,sizeof(client)-1}
                   :created ? mgscoop::script_bridge::Replacement{success,sizeof(success)-1}
                            : mgscoop::script_bridge::Replacement{failure,sizeof(failure)-1};
        selected=true;
    }
    constexpr char session_status[]="return 'MGS_COOP_SESSION_STATUS_V1'";
    if((host_create_enabled || (client_enabled && !client_check_enabled)) && !mission_terminating &&
       mgscoop::player_team::IsActive() && buffer && name && size==sizeof(session_status)-1 &&
       OnRuntimeThread() &&
       std::memcmp(buffer,session_status,size)==0 && std::strcmp(name,session_status)==0) {
        const char* status=ReadDynamiteSessionStatus(game_base);
        UpdateSessionStatus(status);
        replacement={status,std::strlen(status)};selected=true;
    }
    constexpr char marker_begin[]="return 'MGS_COOP_MARKER_LOADING_BEGIN_V1'";
    constexpr char marker_end[]="return 'MGS_COOP_MARKER_LOADING_END_V1'";
    if((host_create_enabled || client_enabled) && buffer && name && size<96 &&
       OnMissionThread() &&
       std::strlen(name)==size && std::memcmp(buffer,name,size)==0) {
        bool recognized=false, allowed=false;
        if(size==sizeof(marker_begin)-1 && std::memcmp(buffer,marker_begin,size)==0) {
            recognized=true;
            unsigned short mission=0; SIZE_T read=0;
            const bool mission_ready=ReadProcessMemory(GetCurrentProcess(),static_cast<unsigned char*>(game_base)+0x2c01372,
                &mission,sizeof(mission),&read) && read==sizeof(mission) &&
                (mission==30050 || (offline_enabled && mgscoop::SupportsPartnerBody(mission)));
            const bool cleanup_ready=!gameplay_restore_pending || RestoreGameplayRules();
            allowed=OnAllocationThread() && archive_mounted && allocation_enabled && mission_ready && cleanup_ready &&
                mgscoop::marker_loading::Begin(game_base,game_span,Log);
            Log(allowed ? "marker_loading=active; lifecycle=before_OnAllocate\r\n"
                        : "marker_loading=refused; coop_preparation_disabled=1\r\n");
        } else if(size==sizeof(marker_end)-1 && std::memcmp(buffer,marker_end,size)==0) {
            recognized=true;allowed=true;mgscoop::marker_loading::End();
            Log("marker_loading=inactive; native_marker_forwarding=1\r\n");
        }
        if(recognized) {
            static constexpr char yes[]="return true",no[]="return false";
            replacement=allowed ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                : mgscoop::script_bridge::Replacement{no,sizeof(no)-1}; selected=true;
        }
    }
    constexpr char terminate_boundary[]="return 'MGS_COOP_MISSION_TERMINATING_V1'";
    if(host_enabled && buffer && name && size==sizeof(terminate_boundary)-1 &&
       std::memcmp(buffer,terminate_boundary,size)==0 && std::strcmp(name,terminate_boundary)==0) {
        // Operational lifecycle signal: independent of the bounded diagnostic
        // trace, Lua co-op flags and whether a peer has joined yet.
        mgscoop::worker_scenario::SetLoadingContext({mgscoop::worker_scenario::LoadingPhase::terminating,allocation_mission,worker_loading_generation});
        const bool accepted=termination_scope.Enter(mission_phase==MissionPhase::running,mission_thread,
            GetCurrentThreadId(),HasDynamiteSessionOwnership(),TransferRuleThread);
        bool restored=false;
        if(accepted) {
            ClearUpperEndOwner(); mgscoop::partner_upper_probe::Clear("mission_terminating");
            equipment_allocation_authorization.End();
            stock46_authorization.End(); stock46_before={};
            equip_preload_authorization.End(); equip_preload_before={};
            mgscoop::equipment_stock_writer::End();
            stock46_groups_authorized=stock46_groups_returned=false;
            mission_terminating=true; buddy_pose_owner = {}; ClearBuddyCapacityIdentity(); mgscoop::dog_guard::Disarm("terminating");
            restored=RestoreGameplayRules();
            // The partner tick stops while terminating, so the input window and
            // DM patch must be torn down here (before the Lua STOP receipt),
            // not left live on the game thread.
            if(partner_on) pd::Stop(partner_driver, partner_hooks, mgscoop::GetLabConfig().partner_teardown, true);
        }
        static constexpr char acknowledged[]="return true", refused[]="return false";
        replacement=accepted ? mgscoop::script_bridge::Replacement{acknowledged,sizeof(acknowledged)-1}
            : mgscoop::script_bridge::Replacement{refused,sizeof(refused)-1};selected=true;
        unsigned short current_mission=0;
        const bool current_read=ReadMission(current_mission);
        char line[320]{};
        StringCchPrintfA(line,sizeof(line),"mission_termination=entered; accepted=%u thread=%lu runtime_thread=%lu outgoing_mission=%u current_mission=%u current_read=%u native_rules_restored=%u cleanup_retry_pending=%u diagnostic_trace_required=0\r\n",
            unsigned(accepted),GetCurrentThreadId(),mission_thread,unsigned(allocation_mission),unsigned(current_mission),unsigned(current_read),unsigned(restored),unsigned(gameplay_restore_pending));
        Log(line);
    }
    constexpr char result_boundary[]="return 'MGS_COOP_RESULTS_NEW_MISSION_V1'";
    if((host_create_enabled || client_enabled) && buffer && name && size==sizeof(result_boundary)-1 &&
       OnAllocationThread() &&
       std::memcmp(buffer,result_boundary,size)==0 && std::strcmp(name,result_boundary)==0) {
        mgscoop::result_patch::BeginNextMission();
        mission_terminating=false;
        client_join_armed=false;
        client_key_released=false;
        if(gameplay_restore_pending) RestoreGameplayRules();
        static constexpr char acknowledged[]="return true";
        replacement={acknowledged,sizeof(acknowledged)-1};selected=true;
        Log("result_submission=scope_reset; lifecycle=before_next_Mission_Start\r\n");
    }
    if((host_create_enabled || client_enabled) && buffer && name && size<96 &&
       OnMissionThread() &&
       std::strlen(name)==size && std::memcmp(buffer,name,size)==0) {
        constexpr char lost[]="return 'MGS_COOP_SESSION_LOST_V1'";
        constexpr char restore[]="return 'MGS_COOP_RULES_RESTORE_V1'";
        constexpr char close[]="return 'MGS_COOP_SESSION_CLOSE_V1'";
        constexpr char prepared[]="return 'MGS_COOP_PREPARING_DISCONNECT_RETURNED_V1'";
        constexpr char returned[]="return 'MGS_COOP_SESSION_CLOSE_RETURNED_V1'";
        constexpr char failed[]="return 'MGS_COOP_SESSION_CLOSE_FAILED_V1'";
        const auto equals=[buffer,size](const char* value,std::size_t bytes) {return size==bytes && std::memcmp(buffer,value,bytes)==0;};
        static constexpr char yes[]="return true",no[]="return false";
        if(equals(lost,sizeof(lost)-1)) {
            const bool owned=client_enabled && !client_check_enabled && HasOwnedDynamiteClientSession();
            if(owned) {
                const bool restored=RestoreGameplayRules();
                Log(restored ? "session_lost=client; native_rules_restored=1 reconnect_enabled=0\r\n"
                             : "session_lost=client; native_rules_restored=0 cleanup_retry_pending=1\r\n");
                // The game reported the host disconnect: the lab status no
                // longer describes a live session.
                auto_reset_watch.Stop();
                ApplySessionStatus(lab::SessionStatus::closed);
            }
            replacement=owned ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                              : mgscoop::script_bridge::Replacement{no,sizeof(no)-1}; selected=true;
        } else if(equals(restore,sizeof(restore)-1)) {
            // No active scene can be disabled by an unsolicited receipt.
            // Pending work was established by owned loss/termination/failure.
            const bool restored=!gameplay_restore_pending || RestoreGameplayRules();
            replacement=restored ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                                 : mgscoop::script_bridge::Replacement{no,sizeof(no)-1}; selected=true;
        } else if(equals(close,sizeof(close)-1)) {
            const bool allowed=mission_terminating && CanCloseDynamiteSession(game_base,game_span,allocation_mission,Log);
            replacement=allowed ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                                : mgscoop::script_bridge::Replacement{no,sizeof(no)-1}; selected=true;
        } else if(equals(prepared,sizeof(prepared)-1)) {
            const bool allowed=mission_terminating && CanFinishDynamiteSessionClose(game_base,game_span,allocation_mission,Log);
            replacement=allowed ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                                : mgscoop::script_bridge::Replacement{no,sizeof(no)-1}; selected=true;
        } else if(mission_terminating && (equals(returned,sizeof(returned)-1) || equals(failed,sizeof(failed)-1))) {
            const bool restored=ObserveDynamiteSessionClose(game_base,game_span,Log,equals(returned,sizeof(returned)-1));
            if(!restored) gameplay_restore_pending=true;
            mission_terminating=false;
            replacement=restored ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                                 : mgscoop::script_bridge::Replacement{no,sizeof(no)-1}; selected=true;
        }
    }
    constexpr char damage_on[]="return 'MGS_COOP_DAMAGE_ON_V1'";
    constexpr char damage_off[]="return 'MGS_COOP_DAMAGE_OFF_V1'";
    if((host_create_enabled || client_enabled) && OnMissionThread() &&
       buffer && name && size==sizeof(damage_on)-1 && std::memcmp(buffer,damage_on,size)==0 && std::strcmp(name,damage_on)==0) {
        const bool enabled=mgscoop::player_team::IsActive() && mgscoop::damage::SetActive(game_base,game_span,true,Log);
        static constexpr char yes[]="return true",no[]="return false";
        replacement=enabled ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1} : mgscoop::script_bridge::Replacement{no,sizeof(no)-1};
        selected=true;
    }
    if((host_create_enabled || client_enabled) && OnMissionThread() &&
       buffer && name && size==sizeof(damage_off)-1 && std::memcmp(buffer,damage_off,size)==0 && std::strcmp(name,damage_off)==0) {
        const bool disabled=mgscoop::damage::SetActive(game_base,game_span,false,Log);
        static constexpr char yes[]="return true",no[]="return false";
        replacement=disabled ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1} : mgscoop::script_bridge::Replacement{no,sizeof(no)-1};
        selected=true;
    }
    constexpr char client_ready[]="return 'MGS_COOP_CLIENT_READY_V1'";
    constexpr char client_poll[]="return 'MGS_COOP_CLIENT_POLL_V1'";
    if(client_enabled && OnRuntimeThread() && buffer && name &&
       size<96 && std::strlen(name)==size && std::memcmp(name,buffer,size)==0) {
        const bool context=!mission_terminating && !gameplay_restore_pending && mgscoop::player_team::IsActive();
        if(size==sizeof(client_ready)-1 && std::memcmp(buffer,client_ready,size)==0) {
            const bool was_armed=client_join_armed;
            client_join_armed=context && !client_join_consumed;
            client_key_released=false; // require release after scene entry/focus return
            if(client_join_armed && !was_armed) {
                armed_tick_ms=GetTickCount64();
                auto_join_fired=false;
                AnnounceArmed();
            }
            static constexpr char manual[]="return 'manual'", diagnostic[]="return 'diagnostic_manual'", blocked[]="return 'blocked'";
            replacement=client_join_armed && client_check_enabled ? mgscoop::script_bridge::Replacement{diagnostic,sizeof(diagnostic)-1}
                : client_join_armed ? mgscoop::script_bridge::Replacement{manual,sizeof(manual)-1}
                : mgscoop::script_bridge::Replacement{blocked,sizeof(blocked)-1};
            selected=true;
        } else if(size==sizeof(client_poll)-1 && std::memcmp(buffer,client_poll,size)==0) {
            if(!context || !client_join_armed || client_join_consumed) {
                static constexpr char blocked[]="return 'blocked'";
                replacement={blocked,sizeof(blocked)-1}; selected=true;
                client_join_armed=false;
            } else {
                // Lab-only triggers bypass the foreground-window check. The
                // keyboard path below is byte-for-byte the previous behaviour.
                const char* trigger_key=nullptr;
                if(lab_active) {
                    const auto& labc=mgscoop::GetLabConfig();
                    if(labc.join_trigger && ConsumeTriggerFile(join_trigger_path)) trigger_key="trigger_file";
                    else if(labc.auto_join_delay_s>0 && !auto_join_fired &&
                            GetTickCount64()-armed_tick_ms>=static_cast<std::uint64_t>(labc.auto_join_delay_s)*1000) {
                        trigger_key="auto"; auto_join_fired=true;
                    }
                }
                bool fired=false; const char* key="ctrl_f8";
                if(trigger_key) { fired=true; key=trigger_key; }
                else {
                    DWORD foreground_pid=0;
                    const HWND foreground=GetForegroundWindow();
                    if(foreground) GetWindowThreadProcessId(foreground,&foreground_pid);
                    if(foreground_pid!=GetCurrentProcessId()) client_key_released=false;
                    else if((GetAsyncKeyState(VK_F8)&0x8000)==0) client_key_released=true;
                    else {
                        const bool pressed=client_key_released && (GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
                        client_key_released=false;
                        if(pressed) fired=true;
                    }
                }
                if(fired) {
                    // Consume before entering any native setup, even on refusal.
                    client_join_consumed=true; client_join_armed=false;
                    ++join_attempts;
                    if(client_check_enabled) {
                        char line[128]{};
                        StringCchPrintfA(line,sizeof(line),"client_join=input_received; key=%s; mode=diagnostic; native_join_calls=0\r\n",key);
                        Log(line);
                    } else if(trigger_key) {
                        char line[128]{};
                        StringCchPrintfA(line,sizeof(line),"client_join=input_received; key=%s; retry=reset_available\r\n",key);
                        Log(line);
                    } else {
                        Log("client_join=input_received; key=ctrl_f8; retry=restart_required\r\n");
                    }
                    if(lab_active) lab_status.join_attempts=join_attempts;
                    SetLabJoin(lab::JoinStatus::requested);
                    const bool accepted=TryCreateClientSession(game_base,game_span,client_host,Log,client_check_enabled);
                    ReportJoinResult(accepted);
                    static constexpr char submitted[]="return 'submitted'", failed[]="return 'failed'";
                    static constexpr char checked[]="return 'diagnostic_passed'", check_failed[]="return 'diagnostic_failed'";
                    if(client_check_enabled)
                        replacement=accepted ? mgscoop::script_bridge::Replacement{checked,sizeof(checked)-1}
                            : mgscoop::script_bridge::Replacement{check_failed,sizeof(check_failed)-1};
                    else
                        replacement=accepted ? mgscoop::script_bridge::Replacement{submitted,sizeof(submitted)-1}
                            : mgscoop::script_bridge::Replacement{failed,sizeof(failed)-1};
                    selected=true;
                }
                // No input: leave the original literal receipt unchanged. This
                // also avoids logging a replacement on every gameplay frame.
            }
        }
    }
    // Reset / re-arm poll (synthesis gap G11). client_join.lua sends this every
    // OnUpdate in client mode so a reset can fire after a join was consumed,
    // when the normal join poll has already stopped.
    constexpr char client_reset_poll[]="return 'MGS_COOP_CLIENT_RESET_POLL_V1'";
    if(client_enabled && OnRuntimeThread() && buffer && name && size==sizeof(client_reset_poll)-1 &&
       std::memcmp(buffer,client_reset_poll,size)==0 && std::strcmp(name,client_reset_poll)==0) {
        // Only replace (and therefore log) when a reset actually re-armed.
        // Otherwise leave the literal unchanged so idle polling writes nothing.
        if(const char* directive=HandleResetPoll()) {
            replacement={directive,std::strlen(directive)}; selected=true;
        }
    }
    constexpr char receipt[] = "return 'MGS_COOP_BOOTSTRAP_EXECUTED_V1'";
    constexpr char admission[]="return 'MGS_COOP_HOST_ADMISSION_V1'";
    constexpr char admission_observed[]="return 'MGS_COOP_HOST_ADMISSION_OBSERVED_V1'";
    constexpr char released[]="return 'MGS_COOP_HOST_RELEASE_V1'";
    // Character receipts (query, Ocelot/Quiet reset bypass, readback) are
    // session-free; offline-v1 answers them too. Every session receipt above
    // stays gated on host_create/client.
    if(!selected && mgscoop::character::ReceiptsActive(host_create_enabled,client_enabled,offline_enabled)) {
        const auto character_kind=mgscoop::character::Classify(buffer,size,name);
        if(character_kind!=mgscoop::character::Receipt::none)
            selected=SelectCharacterReceipt(character_kind,buffer,size,replacement);
    }
    if(host_admission_enabled && (OnRuntimeThread() || OnTerminationThread()) && buffer && name && size==sizeof(released)-1 &&
       std::memcmp(buffer,released,size)==0 && std::strcmp(name,released)==0)
        ForgetDynamiteHostAdmission();
    if(host_enabled && buffer && name && size==sizeof(admission)-1 &&
       std::memcmp(buffer,admission,size)==0 && std::strcmp(name,admission)==0) {
        const bool authorized=(OnRuntimeThread() || OnTerminationThread()) && host_admission_enabled && CanChangeDynamiteHostAdmission(game_base,game_span,Log);
        static constexpr char yes[]="return true", no[]="return false";
        replacement=authorized ? mgscoop::script_bridge::Replacement{yes,sizeof(yes)-1}
                               : mgscoop::script_bridge::Replacement{no,sizeof(no)-1};
        selected=true;
    }
    if(host_admission_enabled && buffer && name && size==sizeof(admission_observed)-1 &&
       std::memcmp(buffer,admission_observed,size)==0 && std::strcmp(name,admission_observed)==0) {
        Log("host_admission=lua_call_returned; inspecting_native_flag=1\r\n");
        ObserveDynamiteSession(game_base,Log);
    }
    const bool execution_receipt = bootstrap_enabled && buffer && name && size == sizeof(receipt)-1 &&
        std::memcmp(buffer, receipt, sizeof(receipt)-1) == 0 && std::strcmp(name, receipt) == 0;
    const bool archive_receipt = archive_mounted && buffer && name && size < 256 &&
        size > 23 && std::memcmp(buffer, "return 'MGS_COOP_ARCHIVE_", 23) == 0;
    const bool archive_source = archive_mounted && name && std::strcmp(name, "@/Assets/tpp/script/lib/MgsCoopArchiveProbe.lua") == 0;
    const bool allocation_receipt = allocation_enabled && buffer && name && size < 256 &&
        size > 20 && std::memcmp(buffer, "return 'MGS_COOP_PACK_", 21) == 0;
    // A refused partner pack is shown when its mission starts (PartnerMissionStart).
    if (allocation_receipt) NotePackReceipt(buffer, size);
    constexpr char phase_prefix[] = "return 'MGS_COOP_PHASE_V1' -- ";
    const bool phase_receipt = allocation_enabled && buffer && size < 256 &&
        size >= sizeof(phase_prefix)-1 && std::memcmp(buffer,phase_prefix,sizeof(phase_prefix)-1)==0;
    if(host_enabled && phase_receipt) {
        // Diagnostic receipts never authorize teardown or mutate gameplay rules.
        if(host_create_enabled || client_enabled) ObserveDynamiteSession(game_base,Log);
        constexpr char initialize[]="return 'MGS_COOP_PHASE_V1' -- OnInitialize:enter:30050";
        constexpr char started[]="return 'MGS_COOP_PHASE_V1' -- OnMissionGameStart:enter:30050";
        if((size==sizeof(initialize)-1 && std::memcmp(buffer,initialize,sizeof(initialize)-1)==0) ||
           (size==sizeof(started)-1 && std::memcmp(buffer,started,sizeof(started)-1)==0))
            ObserveHostSessionPreflight(game_base,game_span,Log);
    }
    if (Sample(count) || selected || execution_receipt || archive_receipt || archive_source || allocation_receipt || phase_receipt) {
        char copied[241]{}, line[512]{};
        CopyName(name, copied);
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "entry_count=%lld thread=%lu tick_ms=%llu size=%llu name_prefix=%s replacement=%d execution_receipt=%d\r\n",
            count, GetCurrentThreadId(), GetTickCount64(), static_cast<unsigned long long>(size), copied,
            selected ? 1 : 0, execution_receipt ? 1 : 0))) Log(line);
    }
    return selected;
}
void ObserveReturn(void*, int result, bool replaced) noexcept {
    const bool partner_was_quiet = pd::ConsumeReturn(partner_quiet);
    const bool world_was_quiet = world_quiet;
    world_quiet = false;
    const bool debug_was_quiet = debug_quiet;
    debug_quiet = false;
    if (partner_was_quiet || world_was_quiet || debug_was_quiet) { InterlockedIncrement64(&returns); return; }
    const auto count = InterlockedIncrement64(&returns);
    if (Sample(count) || replaced) {
        char line[256]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "return_count=%lld thread=%lu tick_ms=%llu parser_result=%d replaced=%d entry_pairing=none\r\n",
            count, GetCurrentThreadId(), GetTickCount64(), result, replaced ? 1 : 0))) Log(line);
    }
}
}

bool OfflineScriptObserverEnabled() noexcept { return offline_enabled && game_base && game_span; }

void InstallGameScriptObserver(void* base, std::size_t span, const wchar_t* directory) noexcept {
    const DWORD incoming_error = GetLastError();
    // Explicit development modes. Bootstrap mode changes only fingerprinted
    // Tpp.lua with the compiled project fragment; no loose scripts are loaded.
    wchar_t marker[32768]{}, log_path[32768]{};
    if (!base || !directory || load_rva > span || sizeof(expected_body) > span-load_rva ||
        FAILED(StringCchPrintfW(marker, ARRAYSIZE(marker), L"%s\\mgs-coop\\script-observe.enabled", directory)) ||
        FAILED(StringCchPrintfW(log_path, ARRAYSIZE(log_path), L"%s\\mgs-coop\\script-observer.log", directory))) {
        SetLastError(incoming_error); return;
    }
    HANDLE file = CreateFileW(marker, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    char mode[32]{};
    DWORD read = 0;
    bool enabled = false;
    bool archive_enabled = false;
    bool play_saves_refused = false, offline_saves_warn = false;
    if (file != INVALID_HANDLE_VALUE) {
        if (ReadFile(file, mode, sizeof(mode), &read, nullptr)) {
            heap_test_enabled = read == 8 && std::memcmp(mode, "heap-v1\n", 8) == 0;
            client_check_enabled = read == 16 && std::memcmp(mode, "client-check-v1\n", 16) == 0;
            client_enabled = client_check_enabled || (read == 10 && std::memcmp(mode, "client-v1\n", 10) == 0);
            host_admission_enabled = read == 8 && std::memcmp(mode, "host-v3\n", 8) == 0;
            host_create_enabled = host_admission_enabled || (read == 8 && std::memcmp(mode, "host-v2\n", 8) == 0);
            offline_enabled = read == 11 && std::memcmp(mode, "offline-v1\n", 11) == 0;
            host_enabled = client_enabled || host_create_enabled || heap_test_enabled || offline_enabled ||
                (read == 8 && std::memcmp(mode, "host-v1\n", 8) == 0);
            allocation_enabled = host_enabled || (read == 11 && std::memcmp(mode, "players-v1\n", 11) == 0);
            archive_enabled = allocation_enabled || (read == 11 && std::memcmp(mode, "archive-v1\n", 11) == 0);
            bootstrap_enabled = archive_enabled || (read == 13 && std::memcmp(mode, "bootstrap-v1\n", 13) == 0);
            enabled = bootstrap_enabled || (read == 11 && std::memcmp(mode, "observe-v1\n", 11) == 0);
        }
        CloseHandle(file);
    }
    // Save gate (docs/link-protocol.md): two-PC play runs co-op only on the
    // redirected save copy. If DllMain could not redirect the saves, the
    // offline-v1 marker is pass-through: no splice, archive, partner,
    // character receipts or link; the game is plain offline play. The lab
    // only warns (its defaults are unchanged).
    if (offline_enabled && !lab::SavesRedirected()) {
        if (mgscoop::GetLabConfig().mode == mgscoop::LabMode::play) {
            offline_enabled = host_enabled = allocation_enabled = archive_enabled = bootstrap_enabled = false;
            play_saves_refused = true;
        } else {
            offline_saves_warn = true;
        }
    }
    const auto& debug_config = mgscoop::GetLabConfig();
    mgscoop::debug_menu::Configure(offline_enabled && debug_config.present && debug_config.partner && lab::SavesRedirected(),
        debug_config.role != mgscoop::LabRole::guest, lab::SavesRedirected());
    if (play_saves_refused && script_log == INVALID_HANDLE_VALUE) {
        // Only the refusal is logged; nothing is installed.
        script_log = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (script_log != INVALID_HANDLE_VALUE) {
            LogFormat("\r\nplay=refused; reason=saves_not_redirected; refused=0x%X\r\n", mgscoop::GetLabConfig().play_refused);
            Log("mode=pass_through; gameplay=disabled\r\n");
        }
        SetLastError(incoming_error); return;
    }
    if (enabled && script_log == INVALID_HANDLE_VALUE) {
        script_log = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (script_log != INVALID_HANDLE_VALUE) {
            game_base=base;
            game_span=span;
            if(FAILED(StringCchCopyW(unique_bind_directory,ARRAYSIZE(unique_bind_directory),directory)))unique_bind_directory[0]=0;
            // Lab observability setup (docs/lab-protocol.md). Entirely inert
            // unless lab.ini is present and enabled; with no lab.ini nothing
            // below runs and the loader behaves exactly as before.
            lab_present=mgscoop::GetLabConfig().present;
            if(lab_present &&
               BuildCoopPath(directory,L"lab-status.json",lab_status_path) &&
               BuildCoopPath(directory,L"join.trigger",join_trigger_path) &&
               BuildCoopPath(directory,L"reset.trigger",reset_trigger_path) &&
               BuildCoopPath(directory,L"host-steamid.txt",host_steamid_path)) {
                lab_active=true;
                lab_status.pid=GetCurrentProcessId();
                lab_status.role=host_create_enabled ? lab::Role::host : client_enabled ? lab::Role::guest : lab::Role::none;
                lab_status.mode=host_admission_enabled ? lab::Mode::host_v3
                              : client_check_enabled ? lab::Mode::client_check_v1
                              : client_enabled ? lab::Mode::client_v1 : lab::Mode::off;
                lab_status.local_index=-1;
                SyncLabStatus();
            }
            Log("\r\nMGS Co-op script adapter 0.12.4; executable_fingerprint=accepted; install_after_DirectInput=true; earlier_loads_may_be_missed=true\r\n");
            if(client_check_enabled) Log("mode=client_check_v1; online_player_setup=1 native_join_calls=0 host_identity=none\r\n");
            if(client_enabled && !client_check_enabled) {
                wchar_t host_path[32768]{};
                if(SUCCEEDED(StringCchPrintfW(host_path,ARRAYSIZE(host_path),L"%s\\mgs-coop\\client-host.txt",directory))) {
                    HANDLE host_file=CreateFileW(host_path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                    if(host_file!=INVALID_HANDLE_VALUE) {
                        char text[64]{}; DWORD length=0;
                        if(ReadFile(host_file,text,sizeof(text),&length,nullptr))
                            client_host=mgscoop::ParseClientHostIdentity({text,length});
                        CloseHandle(host_file);
                    }
                }
                if(!client_host) {
                    Log("mode=client_v1; refused=missing_or_invalid_client_host_txt; hooks=0 game_calls=0\r\n");
                    SetLastError(incoming_error); return;
                }
                Log("mode=client_v1; configured_host=1; join=ctrl_f8_after_mission_start; max_players=2; connected=unverified\r\n");
            }
            Log(bootstrap_enabled ? "mode=bootstrap_v1; gameplay=disabled\r\n" : "mode=pass_through; gameplay=disabled\r\n");
            if (archive_enabled) {
                Log("mode=archive_v1; actor_packages_selected=0\r\n");
                archive_mounted = MountPlayerArchive(base, span, directory, Log,
                    offline_enabled && lab_active && lab::SavesRedirected());
                if (allocation_enabled) Log("mode=players_v1; mission_selection_trial=30050; live_actor_count=unverified\r\n");
                if (host_enabled) Log("mode=host_v1; one_attempt=1; peer_acceptance_requested=0\r\n");
                if (heap_test_enabled) Log("mode=heap_v1; paired_heap_trial=1 host_calls=0 allocator_hook=0\r\n");
                if (host_create_enabled) Log("mode=host_v2; dynamite_host_creation=enabled max_players=2 peer_acceptance_requested=0\r\n");
                if (host_admission_enabled) Log("mode=host_v3; peer_acceptance=after_mission_start; max_players=2\r\n");
            }
            if (offline_enabled) {
                // Offline partner spike: needs the lab paths and lab.ini partner=1.
                const auto& labc = mgscoop::GetLabConfig();
                partner_on = lab_active && labc.partner && BuildCoopPath(directory, L"partner.trigger", partner_trigger_path);
                char offline_line[128]{};
                if (SUCCEEDED(StringCchPrintfA(offline_line, ARRAYSIZE(offline_line),
                    "mode=offline_v1; sessions=0; s_is_online=never; partner=%u\r\n", partner_on ? 1u : 0u))) Log(offline_line);
                if (labc.mode == mgscoop::LabMode::play)
                    LogFormat("play=active; role=%s; partner=%u; refused=0x%X\r\n", labc.role == mgscoop::LabRole::host ? "host" : "guest",
                              partner_on ? 1u : 0u, labc.play_refused);
                if (offline_saves_warn) Log("offline_v1=warn; reason=saves_not_redirected\r\n");
                if (partner_on) {
                    pd::Configure(partner_driver, labc);
                    partner_addresses = pp::AddressesFor(base);
                    std::uintptr_t mismatch = 0;
                    partner_gate_ok = pp::CodeGate(base, span, mismatch);
                    buddy_pose_pins_ok = partner_gate_ok && BuddyPoseCodeGate(base,span);
                    buddy_capacity_pins_ok = mgscoop::buddy_capacity_census::Compiled() && partner_gate_ok &&
                        BuddyPinsMatch(base,span,mgscoop::buddy_capacity_routes::kPins);
                    buddy_ride_pins_ok = mgscoop::buddy_ride_read::Compiled() && partner_gate_ok &&
                        BuddyPinsMatch(base,span,mgscoop::buddy_ride_read::kPins);
                    ride_player_pins_ok = mgscoop::buddy_capacity_census::Compiled() && partner_gate_ok &&
                        BuddyPinsMatch(base,span,mgscoop::ride_player_contract::kPins);
                    if(mgscoop::buddy_capacity_census::Compiled()) Log(ride_player_pins_ok ? "ride_player_code_gate=ok;scope=observe_only\r\n" : "ride_player_code_gate=refused\r\n");
                    if(mgscoop::buddy_capacity_census::Compiled()) Log(buddy_capacity_pins_ok ? "buddy_capacity_code_gate=ok;scope=private_candidate\r\n" : "buddy_capacity_code_gate=refused\r\n");
                    Log(buddy_pose_pins_ok ? "buddy_pose_code_gate=ok;scope=read_only_current_dog\r\n" : "buddy_pose_code_gate=refused\r\n");
                    const bool downed_gate = partner_gate_ok && mgscoop::downed_damage::CodeGate(base, span);
                    std::uintptr_t carryMismatch=0;
                    const bool carryGate=downed_gate && mgscoop::carry_info::CodeGate(base,span,carryMismatch);
                    mgscoop::carry_info::Configure(partner_addresses,carryGate,Log);
                    std::uintptr_t vehicleMismatch=0;
                    const bool vehicleGate=downed_gate&&mgscoop::vehicle_info::CodeGate(base,span,vehicleMismatch);
                    mgscoop::vehicle_info::Configure(partner_addresses,vehicleGate,Log);
                    LogFormat("vehicle_info_code_gate=%s;rva=0x%llx;scope=read_only_controller\r\n",vehicleGate?"ok":"refused",static_cast<unsigned long long>(vehicleMismatch));
                    std::uintptr_t sleepMismatch=0;
                    const bool sleepGate=downed_gate&&mgscoop::sleep_info::CodeGate(base,span,sleepMismatch);
                    mgscoop::sleep_info::Configure(partner_addresses,sleepGate,Log);
                    LogFormat("sleep_info_code_gate=%s;rva=0x%llx;scope=read_only_controller\r\n",sleepGate?"ok":"refused",static_cast<unsigned long long>(sleepMismatch));
                    LogFormat("carry_info_code_gate=%s;rva=0x%llx;scope=read_only_query\r\n",carryGate?"ok":"refused",static_cast<unsigned long long>(carryMismatch));
                    downed_hook_ok = downed_gate && mgscoop::downed_damage::Install(base, span, Log);
                    // D7 (docs/vehicles-companions-design.md 6.12, 8.9): the guest's copy of the host's D-Dog takes
                    // no damage; inert until the guest's companion module arms its lease. Installed only under M1's
                    // key companion_host_owned (review 2026-10-29): with the key off (the lab and play default)
                    // neither slot is touched, on the host's PC or the guest's.
                    const char* dogGuardOff = mgscoop::dog_guard::InstallRefusal(partner_gate_ok, labc.companion_host_owned);
                    dog_guard_ok = !dogGuardOff && mgscoop::dog_guard::Install(base, span, Log);
                    if (dogGuardOff && std::strcmp(dogGuardOff, "key_off") == 0) Log("dog_guard=off;reason=key_off;slots=0\r\n");
                    else Log(dog_guard_ok ? "dog_guard_code_gate=ok;scope=guest_mirror_dog_lease\r\n" : "dog_guard_code_gate=refused\r\n");
                    mgscoop::downed_player::Configure(partner_addresses, downed_hook_ok);
                    // Teammate rescue: stock player sleep and player-carries-player
                    // (docs/STATUS.md). The adapters stay inert until a teammate is down.
                    // The carry adapter's vtable slots install lazily on the first
                    // partner tick of a mission, after every startup code gate ran.
                    coop_rescue_pending = downed_hook_ok && mgscoop::coop_sleep::Configure(base, span);
                    Log(coop_rescue_pending ? "coop_rescue=configured; sleep=native; carry=pending_install\r\n" : "coop_rescue=refused\r\n");
                    std::uintptr_t sleepSeedMismatch=0;
                    auto sleepSeedRead=[](std::uintptr_t at,void* target,std::size_t size) noexcept {
                        SIZE_T count=0;return at&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),target,size,&count)&&count==size;
                    };
                    const bool sleepSeedGate=mgscoop::worker_scenario::Compiled()&&downed_hook_ok&&sleepGate&&
                        mgscoop::sleep_seed_contract::CodeGate(sleepSeedRead,reinterpret_cast<std::uintptr_t>(base),span,sleepSeedMismatch)&&
                        mgscoop::sleep_body0_transition_contract::CodeGate(sleepSeedRead,reinterpret_cast<std::uintptr_t>(base),span,sleepSeedMismatch);
                    mgscoop::downed_player::ConfigureNativeSleepTest(sleepSeedGate);
                    LogFormat("native_sleep_seed_gate=%s;rva=0x%llx;scope=worker_only_local0\r\n",sleepSeedGate?"ok":"disabled_or_refused",static_cast<unsigned long long>(sleepSeedMismatch));
                    Log(downed_hook_ok ? "downed_code_gate=ok;scope=paired_free_roam\r\n" : "downed_code_gate=refused\r\n");
                    if (partner_gate_ok) Log("partner_code_gate=ok; scope=contract_sites_and_vtable_slots\r\n");
                    else LogFormat("partner_code_gate=mismatch; rva=0x%llx\r\n", static_cast<unsigned long long>(mismatch));
                    if (partner_gate_ok && (mgscoop::partner_upper_probe::Compiled() || upper_end::Compiled()))
                        mgscoop::partner_upper_probe::Install(base, span,
                            {offline_enabled, lab_active, lab::SavesRedirected()}, Log);
                    if (upper_phase::Compiled() && mgscoop::partner_upper_probe::Installed()) {
                        // Installed UpperProbe already matched the exact executable,
                        // full CS170/B6 writer and End/phase bodies BEFORE hooks.
                        // Do not hash now: their authorized prologues are detoured.
                        std::uintptr_t idle_mismatch = 0;
                        const bool idle_gate = upper_idle::Compiled() && upper_idle::CodeGate(reinterpret_cast<std::uintptr_t>(base), span,
                            {nullptr, UpperPhaseRead}, &idle_mismatch);
                        const bool idle_configured = idle_gate && upper_idle::Configure(reinterpret_cast<std::uintptr_t>(base),
                            {offline_enabled, lab::SavesRedirected(), true, true}, {nullptr, UpperPhaseRead});
                        LogFormat("upper_manual_idle=configure; compiled=%u; extra_code_gate=%u; mismatch=%llx; configured=%u; default_off=1; automatic_capability=0\r\n",
                            upper_idle::Compiled() ? 1u : 0u, idle_gate ? 1u : 0u,
                            static_cast<unsigned long long>(idle_mismatch), idle_configured ? 1u : 0u);
                        const bool configured = upper_phase::Configure(reinterpret_cast<std::uintptr_t>(base),
                            {offline_enabled, lab::SavesRedirected(), true, true}, {nullptr, UpperPhaseRead});
                        const bool phase_installed = configured && upper_phase::Install(const_cast<void*>(base), span);
                        LogFormat("upper_phase_observe=install; configured=%u; installed=%u; observe_only=1; native_submission=0\r\n",
                            configured ? 1u : 0u, phase_installed ? 1u : 0u);
                    }
                    LogFormat("partner=configured; mode=offline_v1; stage=%u; source=%s; apply=%s; rate_hz=%u; interp_ms=%u; offset_cm=%u; teardown=%s; "
                        "anim=%s; aset=%s; reset=%s; look=%s; t_pinned=%u\r\n",
                        partner_driver.settings.stage, mgscoop::PartnerSourceName(partner_driver.settings.source),
                        mgscoop::PartnerApplyName(partner_driver.settings.apply),
                        labc.partner_rate_hz, labc.partner_interp_ms, labc.partner_offset_cm,
                        mgscoop::PartnerTeardownName(labc.partner_teardown), mgscoop::PartnerAnimName(partner_driver.settings.anim),
                        mgscoop::PartnerAsetName(partner_driver.settings.aset), mgscoop::PartnerResetName(partner_driver.settings.reset),
                        mgscoop::PartnerLookName(partner_driver.settings.look), partner_addresses.t_vtable ? 1u : 0u);
                    if (labc.crash_probe) mgscoop::crash_probe::Install(base, span, Log);
                    mgscoop::debug_overlay::SetShutdownHandler(&PartnerShutdown);
                    if (labc.debug_soldier_warp) {
                        // Read-only byte gate; nothing is called until a host "spawn_enemy here"
                        // or a guest spawn-sync grant (GuestWarpReceipt).
                        std::uintptr_t warp_mismatch = 0;
                        const char* warp_reason = mgscoop::soldier_warp::Verify(reinterpret_cast<std::uintptr_t>(base), span, warp_mismatch);
                        soldier_warp_ready = warp_reason == nullptr;
                        if (soldier_warp_ready) Log("soldier_warp=ready; key=debug_soldier_warp; call=manager_vt38_send; hop=runtime_census\r\n");
                        else LogFormat("soldier_warp=refused; reason=%s; rva=0x%llx\r\n", warp_reason, static_cast<unsigned long long>(warp_mismatch));
                    }
                    // Steam online (docs/steam-online.md): play + steam, or the lab self-test.
                    const bool play_steam = labc.mode == mgscoop::LabMode::play && labc.net &&
                                            labc.net_transport == mgscoop::NetTransport::steam && lab::SavesRedirected();
                    if (labc.net_transport_refused) Log("net_transport=udp; reason=steam_play_only\r\n");
                    if (play_steam || (labc.mode == mgscoop::LabMode::lab && labc.steam_selftest)) {
                        mgscoop::steam_online::Config steam{};
                        steam.play_steam = play_steam;
                        steam.host = labc.role == mgscoop::LabRole::host;
                        steam.expected_self = labc.net_steam_self;
                        steam.host_id = labc.net_peer_steam;
                        steam.selftest = labc.mode == mgscoop::LabMode::lab ? labc.steam_selftest : 0;
                        steam.fake_identity = labc.self_steamid != 0;
                        steam.log = Log;
                        steam.dispatch = labc.net_steam_dispatch;
                        steam.selftest_fn = labc.steam_selftest_fn;
                        mgscoop::steam_online::Configure(steam);
                        if (steam.selftest)
                            LogFormat("steam_selftest=configured; level=%u; identity=%s\r\n", steam.selftest, steam.fake_identity ? "fake" : "real");
                    }
                    if (labc.net) {
                        ConfigureWorld(labc, directory);
                        ConfigureSaveHandoff(labc, directory);
                        mgscoop::partner_net::Config net{labc.net_listen_port, labc.net_peer_port, labc.partner_rate_hz,
                                                         labc.net_latency_ms, labc.net_loss_permille, Log};
                        net.link_v2 = labc.net_link == mgscoop::NetLink::v2;
                        net.play = labc.mode == mgscoop::LabMode::play;
                        net.host = labc.role == mgscoop::LabRole::host;
                        net.bind_loopback = labc.net_bind_loopback;
                        net.bind_address = labc.net_bind_address;
                        net.peer_address = labc.net_peer_address;
                        net.impair.jitter_ms = labc.net_jitter_ms;
                        net.impair.dup_permille = labc.net_dup_permille;
                        net.impair.reorder_permille = labc.net_reorder_permille;
                        net.impair.reorder_ms = labc.net_reorder_ms;
                        net.impair.burst = labc.net_burst;
                        net.impair.seed = labc.net_seed;
                        net.impair.burst_model = labc.net_loss_model == mgscoop::NetLossModel::burst;
                        // The epoch reproduction knob: the lab and the worker's play rehearsal only
                        // (lab_config forces it to 0 in any other play; checked here again).
                        net.session_skew = labc.mode == mgscoop::LabMode::lab || labc.play_rehearsal ? labc.link_session_skew : 0u;
                        net.interp_auto = labc.partner_interp == mgscoop::PartnerInterp::auto_;
                        net.interp_ms = labc.partner_interp_ms;
                        net.interp_min_ms = labc.partner_interp_min_ms;
                        net.interp_max_ms = labc.partner_interp_max_ms;
                        net.saves_redirected = lab::SavesRedirected();
                        net.directory = directory;
                        net.build = lab::ProbeVersion();
                        net.hud = mgscoop::coop_hud::ModeFor(labc.coop_hud, mgscoop::GetCoopConfig().hud, net.play);
                        if (play_steam) {
                            net.transport = mgscoop::partner_net::Transport::steam;
                            net.steam_self = labc.net_steam_self;
                            net.steam_peer = labc.net_peer_steam;
                            net.steam_wait = labc.net_steam_wait;
                            net.accepting = labc.net_steam_accept;
                            net.steam_resolve = &mgscoop::steam_online::Resolve;
                            net.steam_requests = &mgscoop::steam_cb::PopSessionRequest;
                            net.steam_dispatch = static_cast<std::uint8_t>(labc.net_steam_dispatch);
                        }
                        mgscoop::partner_net::Start(net);
                    } else {
                        if (labc.play_refused & mgscoop::kPlayRefusedGuestNoPeer) Log("link=disabled; reason=no_peer_addr\r\n");
                        if (labc.play_refused & mgscoop::kPlayRefusedNoPeerSteam) Log("link=disabled; reason=no_peer_steam\r\n");
                        Log("partner_net=disabled; reason=transport_bad_config\r\n");
                        if (labc.world) LogFormat("world=disabled; reason=net_off; t_ms=%llu\r\n", static_cast<unsigned long long>(QpcMicros() / 1000));
                    }
                } else if (labc.world) {
                    LogFormat("world=disabled; reason=partner_off; t_ms=%llu\r\n", static_cast<unsigned long long>(QpcMicros() / 1000));
                }
                // Partner name tag (docs/partner-nametag.md): verifies its camera contract and
                // configures the overlay layer before the overlay installs (nametag=ready|refused).
                mgscoop::nametag_game::Install({base, span, Log, partner_on && partner_gate_ok, labc.role != mgscoop::LabRole::guest, &labc,
                    &mgscoop::GetCoopConfig(), SteamLocalPersonaName});
                mgscoop::teammate_targeting::Install(game_base,game_span,Log,mgscoop::worker_scenario::Compiled());
            }
            if (mgscoop::character::ReceiptsActive(host_create_enabled, client_enabled, offline_enabled)) {
                // coop.ini is read once here (kernel32 file I/O), outside the Lua compile path.
                const auto& coop = mgscoop::GetCoopConfig();
                char character_line[192]{};
                if (SUCCEEDED(StringCchPrintfA(character_line, ARRAYSIZE(character_line),
                    "character_config=%s; fallback=%s; coop_ini=%u; reset_bypass_needed=%u; applied_in=TppMain.OnAllocate\r\n",
                    coop.character == mgscoop::PlayerCharacter::game_default ? "default" : mgscoop::PlayerCharacterName(coop.character),
                    mgscoop::PlayerCharacterName(coop.character_fallback), unsigned(coop.present),
                    unsigned(mgscoop::character::NeedsResetBypass(coop))))) Log(character_line);
            }
            const auto target = reinterpret_cast<mgscoop::script_bridge::LoadBuffer>(static_cast<unsigned char*>(base)+load_rva);
            const auto status = mgscoop::script_bridge::Install(target, expected_body, sizeof(expected_body),
                                                               {nullptr, ObserveEntry, ObserveReturn});
            char line[256]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                "install_result=%d installed=%d pid=%lu thread=%lu tick_ms=%llu target_rva=0x1a178e0 lifetime=process\r\n",
                static_cast<int>(status), status == mgscoop::script_bridge::InstallResult::installed ? 1 : 0,
                GetCurrentProcessId(), GetCurrentThreadId(), GetTickCount64()))) Log(line);
            if(lab_active) {
                const auto& labc=mgscoop::GetLabConfig();
                char lab_line[192]{};
                if(SUCCEEDED(StringCchPrintfA(lab_line,ARRAYSIZE(lab_line),
                    "lab=active; role=%s; join_trigger=%u; auto_join_delay_s=%u; status_json=1\r\n",
                    lab::RoleName(lab_status.role),labc.join_trigger?1u:0u,labc.auto_join_delay_s))) Log(lab_line);
            } else if(lab_present) {
                Log("lab=inactive; reason=path_too_long; status_json=0\r\n");
            }
            // Keep the log and policy alive with the pinned DLL. No live unload.
        }
    }
    SetLastError(incoming_error);
}

void InstallNativePoseWorkerEarly() noexcept {
#if MGS_BACKGROUND_WORKER
    const DWORD error=GetLastError();
    // Initial private worker callback after initialized retail/file/save/desktop
    // admission, before passive native-pose hooks or any actor first common bind.
    // A cold first-bind receipt is read-only and never creates scene authority.
    if(game_base&&game_span&&unique_bind_directory[0]&&(MountedMgoUniqueRestContext(unique_bind_directory)||MountedMgoStandingCycleContext(unique_bind_directory))){
        const auto current=[]() noexcept {const auto& c=mgscoop::GetLabConfig();return
            mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&
            c.mode==mgscoop::LabMode::lab&&offline_enabled&&partner_on&&partner_gate_ok&&lab::SavesRedirected();};
        const bool prepared=mgscoop::sleep_unique_bind_runtime::Prepare(reinterpret_cast<std::uintptr_t>(game_base),game_span,
            unique_bind_directory,{partner_gate_ok,offline_enabled,lab::SavesRedirected(),current},Log);
        LogFormat("unique_bind_early=%s;observer_only=1;append=0;effective_selection=unverified\r\n",prepared?"prepared":"refused");
        if(!prepared)mgscoop::worker_scenario::Refuse("unique_bind_initial_observer_install");
    }
#if defined(MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER)
    // Separate explicitly admitted OFF experiment. Native calls always forward;
    // cold typed owner receipts never create a gameplay/native-window lease.
    if(game_base&&game_span&&unique_bind_directory[0]&&MountedMgoStandingCycleContext(unique_bind_directory)){
        const auto current=[]() noexcept {const auto& c=mgscoop::GetLabConfig();return
            mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&
            c.mode==mgscoop::LabMode::lab&&offline_enabled&&partner_on&&partner_gate_ok&&lab::SavesRedirected();};
        const bool prepared=mgscoop::qualified_sleep_lookup_runtime::Prepare(reinterpret_cast<std::uintptr_t>(game_base),game_span,
            unique_bind_directory,{partner_gate_ok,offline_enabled,lab::SavesRedirected(),current,MountedMgoStandingCycleContext,mgscoop::vehicle_native_protection::ReadOriginal},Log);
        LogFormat("sleep_lookup_early=%s;original_forward_only=1;selection=0;append=0\r\n",prepared?"prepared":"refused");
        if(!prepared)mgscoop::worker_scenario::Refuse("standing_lookup_observer_install");
    }
#endif
#if defined(MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER)
    // Observe the original native callback lifetime; no job/selection lease.
    // A natural stack is retained only as bounded diagnostics, never authority.
    if(game_base&&game_span&&unique_bind_directory[0]&&MountedMgoStandingCycleContext(unique_bind_directory)){
        const auto current=[]() noexcept {const auto& c=mgscoop::GetLabConfig();return
            mgscoop::worker_scenario::Compiled()&&mgscoop::worker_scenario::ThreadAdmitted()&&
            c.mode==mgscoop::LabMode::lab&&offline_enabled&&partner_on&&partner_gate_ok&&lab::SavesRedirected();};
        const bool prepared=mgscoop::native_motion_frame_runtime::Prepare(reinterpret_cast<std::uintptr_t>(game_base),game_span,
            unique_bind_directory,{partner_gate_ok,offline_enabled,lab::SavesRedirected(),current,MountedMgoStandingCycleContext,mgscoop::vehicle_native_protection::ReadOriginal,mgscoop::partner_input_hook::ReadNativePoseBirthCensus},Log);
        LogFormat("sleep_frame_early=%s;original_forward_only=1;selection=0;lease=0;job_fence=unknown\r\n",prepared?"prepared":"refused");
        if(!prepared)mgscoop::worker_scenario::Refuse("standing_frame_observer_install");
    }
#endif
    if(mgscoop::partner_input_hook::NativePoseWindowStatus().installed){EnsurePartnerSleepConfigured();SetLastError(error);return;}
    if (mgscoop::worker_scenario::Compiled() && mgscoop::worker_scenario::ThreadAdmitted() &&
        offline_enabled && partner_on && partner_gate_ok && game_base && game_span &&
        lab::SavesRedirected() && MountedPlayerMotionCount()==21) {
        namespace ih=mgscoop::partner_input_hook;
        const auto admitted=[]() noexcept {return mgscoop::worker_scenario::Compiled() &&
            mgscoop::worker_scenario::ThreadAdmitted() && mgscoop::lab::SavesRedirected();};
        const auto refused=[](const char* why) noexcept {mgscoop::worker_scenario::Refuse(why);};
        const char* why="pose_worker_bind";
        if (ih::BindNativePoseWorker({admitted,refused})) {
            why=ih::Install(game_base,Log,&PartnerSampleInput);
            if (!why) why=ih::InstallNativePoseWindow(game_base,game_span,true);
        }
        LogFormat("native_pose_early=%s;reason=%s;scope=private_passive_hooks\r\n",why?"refused":"installed",why?why:"ok");
        if(why) mgscoop::worker_scenario::Refuse(why);
        else EnsurePartnerSleepConfigured();
    }
    SetLastError(error);
#endif
}
